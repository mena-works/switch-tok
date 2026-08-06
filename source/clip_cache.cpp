#include "clip_cache.hpp"

#include <algorithm>
#include <cstring>

#include <borealis.hpp>
#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include "net.hpp"
#include "task.hpp"

namespace
{

constexpr const char* kProtocol = "feedcache";

// Clips are normally a few MB. Anything past this is not a short video, and
// pulling it over the console's wifi costs the player more than caching saves.
constexpr size_t kMaxClipBytes = 12 * 1024 * 1024;

// Total resident budget. Held in RAM, so this is the real cost of the feature.
constexpr size_t kBudgetBytes = 64 * 1024 * 1024;

constexpr long kDownloadTimeoutSeconds = 25;

// One reader per open. Holding the shared_ptr means a clip evicted from the
// cache mid-playback stays alive until mpv closes the stream.
struct Cursor
{
    std::shared_ptr<const ClipData> data;
    int64_t                         pos = 0;
};

int64_t readFn(void* cookie, char* buf, uint64_t nbytes)
{
    auto* cursor = static_cast<Cursor*>(cookie);

    const int64_t size      = static_cast<int64_t>(cursor->data->size());
    const int64_t remaining = size - cursor->pos;
    if (remaining <= 0)
        return 0;

    const int64_t count = std::min<int64_t>(static_cast<int64_t>(nbytes), remaining);
    std::memcpy(buf, cursor->data->data() + cursor->pos, static_cast<size_t>(count));
    cursor->pos += count;
    return count;
}

int64_t seekFn(void* cookie, int64_t offset)
{
    auto* cursor = static_cast<Cursor*>(cookie);

    if (offset < 0 || offset > static_cast<int64_t>(cursor->data->size()))
        return MPV_ERROR_GENERIC;

    cursor->pos = offset;
    return offset;
}

int64_t sizeFn(void* cookie)
{
    return static_cast<int64_t>(static_cast<Cursor*>(cookie)->data->size());
}

void closeFn(void* cookie)
{
    delete static_cast<Cursor*>(cookie);
}

int openFn(void* userData, char* uri, mpv_stream_cb_info* info)
{
    auto* cache = static_cast<ClipCache*>(userData);

    const std::string full = uri;
    const std::string mark = std::string(kProtocol) + "://";
    if (full.rfind(mark, 0) != 0)
        return MPV_ERROR_LOADING_FAILED;

    auto data = cache->lookup(full.substr(mark.size()));
    if (!data)
        return MPV_ERROR_LOADING_FAILED;

    auto* cursor = new Cursor { std::move(data), 0 };

    info->cookie   = cursor;
    info->read_fn  = readFn;
    info->seek_fn  = seekFn;
    info->size_fn  = sizeFn;
    info->close_fn = closeFn;

    return 0;
}

} // namespace

ClipCache& ClipCache::instance()
{
    static ClipCache cache;
    return cache;
}

bool ClipCache::attach(mpv_handle* mpv)
{
    const int rc = mpv_stream_cb_add_ro(mpv, kProtocol, this, openFn);
    if (rc < 0)
    {
        brls::Logger::error("clip cache: protocol registration failed: {}", mpv_error_string(rc));
        return false;
    }

    brls::Logger::info("clip cache: {}:// registered", kProtocol);
    return true;
}

void ClipCache::touchLocked(const std::string& id)
{
    const auto it = entries.find(id);
    if (it != entries.end())
        it->second.used = ++clock;
}

std::shared_ptr<const ClipData> ClipCache::lookup(const std::string& id)
{
    std::lock_guard<std::mutex> lock(mutex);

    const auto it = entries.find(id);
    if (it == entries.end())
        return nullptr;

    it->second.used = ++clock;
    return it->second.data;
}

std::string ClipCache::uriFor(const std::string& id, const std::string& url)
{
    if (id.empty())
        return url;

    std::lock_guard<std::mutex> lock(mutex);

    if (!entries.count(id))
        return url;

    touchLocked(id);
    return std::string(kProtocol) + "://" + id;
}

void ClipCache::evictLocked()
{
    // Least recently used first. An index window was tried and thrashed: moving
    // back one step dropped a clip fetched seconds earlier, and the same file
    // was downloaded four times in half a minute. Usage order survives whatever
    // pattern the viewer actually navigates in.
    while (totalBytes > kBudgetBytes && entries.size() > 1)
    {
        auto oldest = entries.begin();
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->second.used < oldest->second.used)
                oldest = it;

        totalBytes -= oldest->second.data->size();
        entries.erase(oldest);
    }
}

void ClipCache::setAllowed(bool value)
{
    std::lock_guard<std::mutex> lock(mutex);

    allowed = value;
    if (allowed)
        pumpLocked();
}

void ClipCache::pumpLocked()
{
    if (busy || !allowed || queue.empty())
        return;

    const auto job = queue.front();
    queue.pop_front();
    busy = true;

    runDetached([this, job]() {
        ClipData bytes;
        const bool ok = net::get(job.second, bytes, kDownloadTimeoutSeconds, kMaxClipBytes);

        {
            std::lock_guard<std::mutex> lock(mutex);

            queued.erase(job.first);
            busy = false;

            if (ok && !bytes.empty())
            {
                totalBytes += bytes.size();
                entries[job.first] = Entry { std::make_shared<const ClipData>(std::move(bytes)),
                                             ++clock };
                evictLocked();
            }
            else
            {
                // Remembered, so a clip that cannot be cached is not retried on
                // every single navigation. Not fatal either way: uriFor falls
                // back to the CDN URL.
                failed.insert(job.first);
                brls::Logger::warning("clip cache: {} skipped", job.first);
            }

            pumpLocked();
        }
    });
}

void ClipCache::prefetch(const std::string& id, const std::string& url)
{
    if (id.empty() || url.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex);

    if (entries.count(id) || failed.count(id) || !queued.insert(id).second)
        return;

    queue.emplace_back(id, url);
    pumpLocked();
}

#include "feed_activity.hpp"

#include <memory>
#include <unordered_set>

#include <extern/nlohmann/json.hpp>

#include "clip_cache.hpp"
#include "config.hpp"
#include "mpv_player.hpp"
#include "net.hpp"
#include "settings.hpp"
#include "task.hpp"
#include "video_view.hpp"
#include "menu_activity.hpp"

namespace
{

// nlohmann's value(key, default) throws when the stored type does not match the
// default's, and this feed is not uniform: ads and commercial items carry
// objects where ordinary posts carry strings. One odd item must not lose the
// other nineteen, so read defensively rather than trusting the shape.
std::string readString(const nlohmann::json& obj, const char* key, std::string fallback = "")
{
    if (!obj.is_object() || !obj.contains(key))
        return fallback;

    const auto& value = obj.at(key);

    if (value.is_string())
        return value.get<std::string>();
    if (value.is_number_integer())
        return std::to_string(value.get<long long>());

    return fallback;
}

} // namespace

FeedActivity::FeedActivity()
    : region(settings::get().region)
{
}

std::string FeedActivity::feedUrl() const
{
    const std::string base  = kApiBase;
    const std::string count = std::to_string(kBatchSize);

    if (!query.empty())
        return base + "/api/search?keywords=" + net::urlEncode(query) + "&count=" + count;

    // Personalised when the bridge sees our sessionid (sent as X-Session-Id by
    // net::get), generic otherwise. region is a hint for the generic fallback.
    return base + "/api/foryou?region=" + region + "&count=" + count;
}

void FeedActivity::restart()
{
    items.clear();
    index = 0;
    video->setPosition(0, 0);
    video->setContext(query.empty() ? region : "\"" + query + "\"");
    loadFeed(false);
}

void FeedActivity::openSearch()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            query = text;
            video->flashStatus(query.empty() ? "region feed" : "search: " + query);
            restart();
        },
        "Search TikTok", "Leave empty to go back to the region feed", 64, query);
}

void FeedActivity::openLogin()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            if (text.empty()) return;
            
            video->flashStatus("Logging in with PIN...");
            std::string url = "https://tok.menaworks.xyz/api/get?pin=" + net::urlEncode(text);
            
            auto token = alive;
            runDetached([this, url, token]() {
                std::string body;
                if (net::getString(url, body)) {
                    try {
                        auto json = nlohmann::json::parse(body);
                        if (json.contains("sessionid")) {
                            std::string sid = json["sessionid"].get<std::string>();
                            
                            if (FILE* f = fopen("sdmc:/switch/switch-tok.sessionid", "wb")) {
                                fwrite(sid.data(), 1, sid.size(), f);
                                fclose(f);
                            }
                            
                            brls::sync([this, token]() {
                                if (!*token) return;
                                net::loadSessionId();
                                video->flashStatus("Login successful! Reloading...");
                                restart();
                            });
                        } else {
                            brls::sync([this, token]() { if (*token) video->flashStatus("Invalid PIN"); });
                        }
                    } catch (...) {
                        brls::sync([this, token]() { if (*token) video->flashStatus("Error parsing PIN response"); });
                    }
                } else {
                    brls::sync([this, token]() { if (*token) video->flashStatus("Network error checking PIN"); });
                }
            });
        },
        "TikTok Login PIN", "tok.menaworks.xyz/login adresinden aldiginiz PIN'i girin", 6, "");
}

void FeedActivity::cycleRegion()
{
    // Searching ignores region, so changing it while a query is active would
    // look like it did nothing. Drop the query too and say so.
    query.clear();

    int at = 0;
    for (int i = 0; i < kRegionCount; ++i)
        if (region == kRegions[i])
            at = i;

    region                  = kRegions[(at + 1) % kRegionCount];
    settings::get().region  = region;
    settings::save();

    video->flashStatus("region: " + region);
    restart();
}

FeedActivity::~FeedActivity()
{
    *alive = false;
    MpvPlayer::instance().stop();
}

brls::View* FeedActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setBackgroundColor(nvgRGB(0, 0, 0));
    root->setGrow(1.0f);
    root->setFocusable(true);

    video = new VideoView();
    video->setGrow(1.0f);
    video->setWidthPercentage(100.0f);
    root->addView(video);

    // The view recognises the gesture; the feed stays the only thing that knows
    // where in the list we are.
    video->setOnNavigate([this](int delta) { showIndex(index + delta); });
    video->setOnTogglePause([this]() {
        static bool paused = false;
        paused             = !paused;
        MpvPlayer::instance().setPaused(paused);
    });

    // allowRepeating = true so holding the stick keeps scrolling the feed.
    root->registerAction(
        "Next", brls::BUTTON_DOWN, [this](brls::View*) { showIndex(index + 1); return true; },
        false, true);

    root->registerAction(
        "Previous", brls::BUTTON_UP, [this](brls::View*) { showIndex(index - 1); return true; },
        false, true);

    root->registerAction(
        "Pause", brls::BUTTON_A,
        [this](brls::View*) {
            static bool paused = false;
            paused = !paused;
            MpvPlayer::instance().setPaused(paused);
            return true;
        },
        false, false);

    root->registerAction(
        "Mute", brls::BUTTON_X,
        [this](brls::View*) {
            MpvPlayer& player = MpvPlayer::instance();
            player.toggleMute();
            settings::get().muted = player.muted();
            settings::save();
            video->flashStatus(player.muted() ? "muted" : "unmuted");
            return true;
        },
        false, false);

    root->registerAction(
        "Volume down", brls::BUTTON_LB,
        [this](brls::View*) {
            MpvPlayer& player = MpvPlayer::instance();
            player.adjustVolume(-10);
            settings::get().volume = player.volume();
            settings::get().muted  = player.muted();
            settings::save();
            video->flashStatus("volume " + std::to_string(player.volume()));
            return true;
        },
        false, true);

    root->registerAction(
        "Volume up", brls::BUTTON_RB,
        [this](brls::View*) {
            MpvPlayer& player = MpvPlayer::instance();
            player.adjustVolume(10);
            settings::get().volume = player.volume();
            settings::get().muted  = player.muted();
            settings::save();
            video->flashStatus("volume " + std::to_string(player.volume()));
            return true;
        },
        false, true);

    root->registerAction(
        "Rewind", brls::BUTTON_LEFT,
        [this](brls::View*) { MpvPlayer::instance().seek(-5.0); return true; },
        false, true);

    root->registerAction(
        "Forward", brls::BUTTON_RIGHT,
        [this](brls::View*) { MpvPlayer::instance().seek(5.0); return true; },
        false, true);

    root->registerAction(
        "Refresh", brls::BUTTON_LT,
        [this](brls::View*) {
            video->flashStatus("refreshing...");
            restart();
            return true;
        },
        false, false);

    root->registerAction(
        "Menü", brls::BUTTON_Y, [this](brls::View*) { 
            brls::Application::pushActivity(new MenuActivity(this));
            return true; 
        }, false, false);

    video->setContext(region);
    loadFeed(false);
    return root;
}

void FeedActivity::loadFeed(bool append)
{
    if (loading)
        return;

    loading = true;

    const std::string url = feedUrl();
    auto token            = alive;

    runDetached([this, url, token, append]() {
        std::string body;
        const bool ok = net::getString(url, body);
        brls::Logger::info("feed fetch ok={} bytes={}", ok, body.size());

        std::vector<FeedItem> parsed;
        std::string error;

        if (!ok)
        {
            error = "feed request failed";
        }
        else
        {
            // The log went quiet exactly here once: fetch reported success and
            // nothing followed. Marking both ends of the parse says whether the
            // body is bad or the handoff to the UI thread never happens.
            brls::Logger::debug("parse start, first bytes: {}", body.substr(0, 48));

            try
            {
                const auto json = nlohmann::json::parse(body);
                brls::Logger::debug("parse ok");

                // The service answers 200 with a non-zero code for rate limits
                // and region problems, so the HTTP status alone means nothing.
                if (json.value("code", -1) != 0)
                {
                    error = "feed rejected: " + json.value("msg", std::string("unknown"));
                }
                else
                {
                    // The two endpoints disagree on shape: the region feed puts
                    // the clips directly in "data", while search wraps them as
                    // {videos, cursor, hasMore}. Iterating an object yields its
                    // values, so the wrong shape parses to zero items without
                    // throwing -- silent, and it looked like an empty feed.
                    const auto&           payload = json.at("data");
                    const nlohmann::json* list    = nullptr;

                    if (payload.is_array())
                        list = &payload;
                    else if (payload.is_object() && payload.contains("videos"))
                        list = &payload.at("videos");

                    if (!list)
                        throw std::runtime_error("unrecognised data shape");

                    for (const auto& entry : *list)
                    {
                        FeedItem item;
                        item.videoUrl = readString(entry, "play");
                        if (item.videoUrl.empty())
                            continue;

                        item.id          = readString(entry, "video_id");
                        item.description = readString(entry, "title");
                        item.author      = entry.contains("author")
                                             ? readString(entry["author"], "unique_id", "unknown")
                                             : "unknown";

                        parsed.push_back(std::move(item));
                    }
                }
            }
            catch (const std::exception& e)
            {
                error = std::string("bad feed json: ") + e.what();
            }
        }

        // Hop back to the UI thread: borealis views are not thread safe.
        // mutable: the captured batch is moved from when appending.
        brls::sync([this, token, parsed = std::move(parsed), error, append]() mutable {
            if (!*token)
                return;

            loading = false;

            if (!error.empty())
            {
                brls::Logger::error("{}", error);
                if (!append)
                    video->setStatus(error);
                return;
            }

            if (parsed.empty())
            {
                // Logged, not just shown: an empty result used to be the one
                // outcome that left no trace, which is exactly the case that
                // needed one.
                brls::Logger::warning("feed produced no items (append={})", append);
                if (!append)
                    video->setStatus("no videos found");
                return;
            }

            if (append)
            {
                // Each call to the service returns a fresh batch rather than
                // the next page, so the same clip can come back while it is
                // still trending. Drop the ones already in hand.
                std::unordered_set<std::string> seen;
                for (const FeedItem& item : items)
                    seen.insert(item.id);

                int added = 0;
                for (FeedItem& item : parsed)
                {
                    if (item.id.empty() || seen.insert(item.id).second)
                    {
                        items.push_back(std::move(item));
                        ++added;
                    }
                }

                video->setPosition(index, static_cast<int>(items.size()));
                brls::Logger::info("feed grew by {} to {}", added, items.size());
                return;
            }

            items = parsed;
            index = 0;
            video->setPosition(0, static_cast<int>(items.size()));
            video->setItem(items[0]);
            primeCache();
            brls::Logger::info("feed loaded: {} items", items.size());
        });
    });
}

void FeedActivity::showIndex(int next)
{
    if (items.empty())
        return;

    const int count = static_cast<int>(items.size());

    // Forward runs off the end into new material; backward stops at the start,
    // because wrapping to the newest clip when you meant to go back is
    // disorienting.
    index = next < 0 ? 0 : (next >= count ? count - 1 : next);

    // Fetch before the end actually arrives, so the new batch is in place by
    // the time it is needed.
    //
    // Only for the region feed: that endpoint returns a fresh batch every call,
    // while a search returns the same matches, so growing it would spend a
    // request to add nothing.
    constexpr int kLookahead = 4;
    if (query.empty() && index >= count - kLookahead)
        loadFeed(true);

    video->setPosition(index, static_cast<int>(items.size()));
    video->setItem(items[index]);

    primeCache();
}

void FeedActivity::primeCache()
{
    if (!kPrefetchEnabled)
        return;

    // One ahead only. Two was greedy: on a console with a dozen BSD sockets
    // total, the extra download bought little and cost the player its stream.
    // The cache decides what to drop on its own, by usage and a byte budget.
    constexpr int kAhead = 1;

    ClipCache& cache = ClipCache::instance();
    const int  count = static_cast<int>(items.size());

    for (int offset = 1; offset <= kAhead; ++offset)
    {
        const int at = index + offset;
        if (at < count)
            cache.prefetch(items[at].id, items[at].videoUrl);
    }
}

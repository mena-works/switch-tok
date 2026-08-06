#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct mpv_handle;

using ClipData = std::vector<uint8_t>;

// Downloads upcoming clips into RAM and serves them to mpv through a custom
// stream protocol.
//
// mpv can prefetch playlist entries by itself, but only as the current file
// approaches its end -- and with loop-file=inf it never does. Doing the fetch
// here sidesteps that entirely, and a cached clip starts with no DNS lookup, no
// TLS handshake and no CDN round trip, which is most of the gap between swipes.
class ClipCache
{
  public:
    static ClipCache& instance();

    // Registers the feedcache:// protocol. Call once, after mpv_initialize.
    bool attach(mpv_handle* mpv);

    // Queues a download. No-op if already held, already queued, or previously
    // failed. Downloads only start while allowed() is true.
    void prefetch(const std::string& id, const std::string& url);

    // Prefetching shares the console's small BSD socket pool with the player.
    // Competing with it produced mbedtls_ssl_handshake -0x7280 (peer closed
    // during handshake) for mpv's own stream, so downloads hold off until the
    // player is settled.
    void setAllowed(bool allowed);

    // feedcache://<id> once the bytes are in hand, otherwise the original URL,
    // so a swipe that outruns the download still plays.
    std::string uriFor(const std::string& id, const std::string& url);

    std::shared_ptr<const ClipData> lookup(const std::string& id);

  private:
    ClipCache() = default;

    void pumpLocked();
    void evictLocked();
    void touchLocked(const std::string& id);

    struct Entry
    {
        std::shared_ptr<const ClipData> data;
        uint64_t                        used = 0;
    };

    std::mutex                   mutex;
    std::map<std::string, Entry> entries;

    // Downloads run one at a time. Two parallel fetches plus mpv streaming a
    // third starved the player's own connection: mbedtls_ssl_read returned 0
    // mid-file and the decoder was handed a truncated H.264 stream.
    std::deque<std::pair<std::string, std::string>> queue;
    std::set<std::string>                           queued;
    std::set<std::string>                           failed;
    bool                                            busy    = false;
    bool                                            allowed = false;

    uint64_t clock      = 0;
    size_t   totalBytes = 0;
};

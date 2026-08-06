#pragma once

// Public, unauthenticated TikTok mirror. A plain HTTPS GET returns JSON with
// direct, watermark-free mp4 URLs, so the console needs no helper machine, no
// request signing (X-Bogus / A-Bogus / msToken) and no JS engine.
//
// Measured before wiring this up: the CDN serves H.264 + AAC with moov ahead of
// mdat and accepts a bare GET -- no Referer, cookie or User-Agent required.
//
// It is someone else's free service. Rate limits and downtime are normal
// operating conditions here, not exceptions; a failed fetch must not look like
// a crash.
constexpr const char* kApiBase = "https://www.tikwm.com";

constexpr int kBatchSize = 20;

// Regions the service accepts. Each was checked rather than assumed: the
// endpoint answers "region empty" instead of falling back to a default, so a
// wrong code yields a silently empty feed rather than an error.
constexpr const char* kRegions[]   = { "US", "TR", "GB", "DE", "FR", "JP", "BR", "IN", "ID", "RU" };
constexpr int         kRegionCount = sizeof(kRegions) / sizeof(kRegions[0]);

constexpr int kDefaultVolume = 70;

// Prefetching the next clip into RAM removes the CDN round trip from a swipe,
// but it shares the console's small BSD socket pool with the player. Getting
// that balance wrong does not degrade gracefully -- it starves mpv's own TLS
// connection and the video breaks. Kept behind a switch so it can be turned off
// without touching the playback path.
constexpr bool kPrefetchEnabled = true;

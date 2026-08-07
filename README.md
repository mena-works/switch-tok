# switch-tok

A vertical, swipe-style short-video feed client for the Nintendo Switch
(Tegra X1 / Atmosphère). **borealis** for the UI, **libmpv** for playback,
**tikwm.com** as the feed source, and optional **TikTok PIN login** through a
companion Chrome extension.

## Download & install

Grab the latest zip from
[Releases](https://github.com/metinxsezdin/switch-tok/releases), extract it to
the root of your SD card (it lands as `switch/switch-tok/switch-tok.nro`) and
launch it from hbmenu.

## Architecture

No helper machine. The `.nro` is self-sufficient:

```
  Switch (.nro)
        |
        |  1. HTTPS GET  https://www.tikwm.com/api/feed/list?region=..&count=..
        |     -> JSON: a direct, watermark-free mp4 URL per video
        |     libcurl + mbedtls, CA bundle at romfs:/cacert.pem
        |
        |  2. mpv streams that URL straight from the CDN
        v
  borealis : UI, input, focus
  libmpv   : demux, decode, A/V sync, audio, cache
```

### Login (PIN) and the Chrome extension

TikTok's WAF makes logging in with a password from the console impossible, and
the `sessionid` cookie is HttpOnly, so a bookmarklet can't read it either. The
workaround is a **companion Chrome extension** plus a small Vercel bridge:

- **`switch-feed-web/`** is a Next.js app (tok.menaworks.xyz) acting as the
  bridge.
- **`chrome-extension/`** is the extension.

1. Load `chrome-extension/` as an unpacked extension in Chrome (Developer
   Mode → "Load unpacked").
2. Log in to tiktok.com in that browser with your own account.
3. Click the extension and press **"Transfer session (get PIN)"** — you get a
   **6-digit PIN**.
4. On the Switch, press **`Y` → "Log in (TikTok PIN)"** and enter the PIN. The
   app fetches your `sessionid` in the background and your "For You" feed is
   unlocked persistently.

### Why this route instead of tiktok.com directly

Measured (August 2026):

| Endpoint | Result |
|---|---|
| `www.tiktok.com/@user` (full browser headers) | 1462 B **WAF challenge** — `SlardarWAF`, `_wafchallengeid` |
| `www.tiktok.com/oembed` | 200, real JSON (but no playback URL) |
| `api16-normal-*.tiktokv.com/aweme/v1/feed` | 429 ratelimit — no WAF, but wants device registration + signing |
| `tikwm.com/api/feed/list` | **200, direct mp4 URLs** |

As a control, google/instagram/devkitpro all return full pages from the same
machine, so this is not an IP-reputation issue: tiktok.com cuts non-browser
clients at the WAF layer. The challenge fingerprints `navigator`, `screen`,
canvas/WebGL; running a JS engine (QuickJS) on the console would not solve it,
because the challenge measures exactly the fakeness of those surfaces.

Measured on the CDN side: **H.264 + AAC**, **720x1280**, `moov` before `mdat`
(faststart), and **a bare GET suffices** — no Referer, cookie or User-Agent.

### The cost of that choice

`tikwm` is a third party's free service. It gets rate-limited, can go down,
can change its schema. The code treats that as a normal operating condition,
not an exception: `code != 0` inside an HTTP 200 is checked separately and
surfaces as an on-screen error. If the service disappears for good, the single
parse function in [source/feed_activity.cpp](source/feed_activity.cpp) is what
changes; the playback chain is unaffected.

## Why libmpv, not a hand-rolled ffmpeg pipeline

devkitPro's stock `switch-ffmpeg` / `switch-mpv` packages **cannot play video
from the network**. wiliwili (a third-party Bilibili client that runs on the
Switch) ships its own package builds for exactly this reason, and those builds
are readily available. Writing an ffmpeg pipeline by hand would have meant
solving A/V sync, audio underruns, seeking, demuxer caching and network stalls
from scratch — mpv already solves all of it, and `loop-file=inf` gives the
TikTok-style infinite loop for free.

## Prerequisites

### 1. devkitPro

On Windows the graphical installer (`devkitProUpdater`) **does not work in
silent mode** — with `/S` no components are selected, so it exits 0 having
installed nothing. Either click through the wizard manually, or install by
hand:

```sh
# fetch the MSYS2 base (version number comes from devkitProUpdate.ini)
curl -LO https://downloads.devkitpro.org/msys-2.10.0.1.7z
7z x msys-2.10.0.1.7z -oC:\devkitPro
```

Then fix the template line in `C:\devkitPro\msys2\etc\fstab` to the real path —
the installer normally does this, manual installs skip it:

```
# map devkitPro to /opt
C:/devkitPro	/opt/devkitpro
```

Write the file **without a BOM**, or every shell start greets you with
`fstab_read_flags: invalid fstab option`.

Packages install into `/opt/devkitpro`; thanks to the mount that is
`C:\devkitPro\` from the Windows side.

### 2. Packages

```sh
pacman-key --init && pacman-key --populate msys2 devkitPro
pacman -Syu --noconfirm
pacman -S --needed --noconfirm \
    switch-dev switch-cmake switch-glfw switch-glm \
    switch-curl switch-mbedtls switch-libwebp \
    switch-libplacebo ninja
```

`switch-libplacebo` is mandatory: `libmpv.a` depends on it with ~107 undefined
symbols, but `switch-ffmpeg` does not pull it in as a dependency because ffmpeg
itself doesn't need it. Without it `mpv.pc` fails to resolve and configure
blows up.

### 3. Custom mpv/ffmpeg

Stock devkitPro ffmpeg/mpv **cannot stream from the network**. Take the pinned
assets from wiliwili's `v0.1.0` tag (recent releases don't carry them):

```sh
BASE=https://github.com/xfangfang/wiliwili/releases/download/v0.1.0/
curl -LO ${BASE}switch-ffmpeg-7.1-1-any.pkg.tar.zst
curl -LO ${BASE}switch-libmpv-0.36.0-3-any.pkg.tar.zst
pacman -U --noconfirm switch-ffmpeg-*.pkg.tar.zst switch-libmpv-*.pkg.tar.zst
```

The `._` file and "Cannot restore extended attributes" warnings during install
are harmless (macOS leftovers in the tarball + NTFS not doing xattrs).

### 4. borealis

```sh
git submodule update --init --recursive
```

## Building

**Use Ninja, not make.** devkitA64 is a native Windows binary and writes
`C:/...` into its depfiles; MSYS make reads those as relative paths, producing
targets like `lib/borealis/library/C:/Users/...` and dying on the drive colon
with `multiple target patterns`. Ninja consumes depfiles itself and is
unaffected.

```sh
cmake -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/switch-tok.nro` (~34 MB). To send it to the console:

```sh
nxlink -s build/switch-tok.nro
```

## Configuration

`kApiBase`, `kBatchSize`, the region list and the prefetch switch live in
[source/config.hpp](source/config.hpp). Note that the feed service rejects an
empty `region` with `"region empty"` instead of falling back to a default.

## Optional: local server

[server/feed_server.py](server/feed_server.py) is **no longer required** — a
leftover from the first architecture. It stays in case you want to serve your
own mp4s over LAN (Range-capable, stdlib only):
`python3 feed_server.py --media ./media --port 8080`, then point the feed URL
at it and return the parse to the `{"items":[...]}` schema.

### Transcode profile (only if you use the local server)

NVDEC is not wired up in this build, so **everything is software decode**. For
the Cortex-A57 a baseline profile (no CABAC, no B-frames) makes a visible
difference:

```sh
ffmpeg -i input.mp4 \
  -vf "scale=-2:960,fps=30" \
  -c:v libx264 -profile:v baseline -level 3.1 -preset slow -crf 24 \
  -maxrate 2500k -bufsize 5000k -pix_fmt yuv420p \
  -c:a aac -b:a 128k -ar 48000 -ac 2 \
  -movflags +faststart \
  media/out.mp4
```

`+faststart` is mandatory: it moves the moov atom to the front; without it mpv
has to fetch the end of the file before playback can start.

## Controls

| Button | Action |
|--------|--------|
| ↑ / ↓ | previous / next video |
| ← / → | seek 5 s back / forward |
| A | pause / resume |
| X | mute |
| L / R | volume down / up |
| ZL | refresh the feed |
| Y | open the menu |
| + | quit |

The `Y` menu contains: **Log in (TikTok PIN)**, **Search** (account or
hashtag), **Change region**, **Refresh**, **Log out**.

Rotate the console sideways in handheld mode and the picture rotates with it,
so vertical video fills the whole screen.

### Touch

| Gesture | Action |
|---|---|
| Swipe up / down | next / previous video |
| Horizontal drag | scrub through time |
| Tap | pause / resume |

A single `PanAxis::ANY` gesture recognizer is used. Separate horizontal and
vertical recognizers race for the same touch and the first winner locks out
the other; instead, which gesture it is gets decided after the first 24 pixels
of travel. Deciding earlier mistakes a slightly diagonal swipe for a scrub and
skips the video.

While scrubbing, seeks are issued every sixth frame — seeking every frame
resets the decoder continuously and the picture never settles — but the bar
and the handle track the finger immediately, so no latency is felt.

## Infinite feed

The service does not paginate — the `cursor` parameter is ignored and **every
call returns a completely different batch**. Measured:

```
call 1: 20 videos -> 916180, 705991, 762254...
call 2: 16 videos -> 354006, 859412, 459284...   (same params, all new)
```

So the feed simply grows: 4 videos before the end of the list, a new batch is
fetched in the background and appended with `video_id` duplicates dropped.
Going forward reaches new material; going back stops at the beginning.

The service may return fewer videos than `count` asked for (request 20, get
16), so never rely on a fixed number.

## Backend assumptions

Verified against the vendored borealis revision:

- On Switch the default backend is GLFW + OpenGL (`glfw3 EGL glapi drm_nouveau nx`)
- nanovg is compiled with `NANOVG_GL3_IMPLEMENTATION` → `nvglCreateImageFromHandleGL3`
- The `Box::draw(..., Style style, FrameContext* ctx)` signature matches
- `brls::sync(const std::function<void()>&)` exists
- `BRLS_PLATFORM_RESOURCES_PATH` = `romfs:/`

The `nvglCreateImageFromHandleGL3` choice is also verified at link time — the
wrong variant would produce an undefined reference.

All of this holds **only with the GLFW/GL backend selected**. That is why
CMakeLists.txt pins `BOREALIS_USE_DEKO3D` and `USE_SDL2` to OFF; moving to the
deko3d path means rewriting [source/mpv_player.cpp](source/mpv_player.cpp)
(deko3d has no GL FBO).

## Verified on hardware

Run on a real console (fw 19.0.1, Atmosphère 1.8.0, title takeover):

- borealis + GL comes up: nouveau / NV120 / GL 4.3 Core / Mesa 20.1.0-rc3
  / GLFW 3.3.4, 1280x720
- **The mpv render context is created on top of borealis's GL context** — the
  riskiest assumption, and it held
- HTTPS from the console: DNS, TCP, TLS handshake, reading `romfs:/cacert.pem`
- The feed is fetched and parsed on device: ~70 KB, 20 videos
- **Video renders on screen** (mpv → FBO → nanovg) and ↑/↓ navigation between
  videos works

### The feed's shape is not uniform

In one 20-video sample, one record's `author.unique_id` came back as an
**object** instead of a string. nlohmann's `value(key, default)` throws on a
type mismatch, so one malformed record used to take down the whole feed.

`readString()` in [source/feed_activity.cpp](source/feed_activity.cpp) reads
every field with a type check. A third-party source offers no schema
guarantee; one odd record must not cost the other nineteen.

### Aspect ratio: scale in exactly one place

The FBO is created at the video's **own resolution**, mpv renders 1:1, and
scaling happens exactly once, at draw time. Create the FBO at view size and
mpv fits the frame once, you fit it again — the result is a smeared image.

### The most expensive lesson: `switch_wrapper.c`

borealis does not put this file into the library; the application has to
compile it (explicitly added in CMakeLists). Its `userAppInit()` initializes
`pl:u`, `set`, `setsys`, `romfs`, sockets and `psm`.

Forget it and **you get no link error**. At runtime borealis asks the
never-initialized font service for a buffer, hands it to nanovg, and
stb_truetype crashes parsing the garbage. The symptom does not look like
fonts — it looks like an unrelated data abort.

### From the fatal screen to a source line

The addresses on Atmosphère's crash screen are directly usable:

```sh
# offset = PC - "Backtrace - Start Address"
aarch64-none-elf-addr2line -e build/switch-tok.elf -f -C -i 0xE985C
```

Caveat: in an `-O2` build part of the backtrace is stale stack residue. The
way to spot the real frame is that the PC resolves to a **consistent inline
chain**. If you change the code and the offsets don't move at all, the crash
is not where you touched.

## Swipe fluidity: the clip cache

mpv's own playlist prefetch is useless here: it kicks in when the file nears
its end, but with `loop-file=inf` the file never ends. The two cancel each
other out.

The fix is doing the prefetch ourselves. This libmpv build includes
`mpv_stream_cb_add_ro`, so we can register our own protocol and serve mpv the
data from RAM: [source/clip_cache.cpp](source/clip_cache.cpp) provides
`feedcache://<video_id>`, and the next two clips download in the background
via libcurl. A cached clip starts with no DNS, TLS or CDN round trip — that
is almost the entirety of the wait on a swipe.

Three things were learned on hardware and changed the design:

**Downloads must be sequential.** Two parallel downloads plus mpv's own stream
race on the console's wifi. The symptom did not look like a network error:
`mbedtls_ssl_read returned -0x0`, then `Invalid NAL unit size` — half-finished
H.264 reaching the decoder.

**The size limit must apply during transfer.** Checking after the download is
useless; one clip pulled 136 MB and timed out. Chunked responses carry no
`Content-Length`, so `CURLOPT_MAXFILESIZE` is not enough either — the cut has
to happen in the write callback.

**Eviction must go by usage, not by index.** The first version kept a
`[current, +2]` window; going backwards discarded a clip downloaded seconds
earlier, and the same file downloaded four times in half a minute. Replaced
with a byte budget (64 MB) and least-recently-used eviction — correct under
any navigation pattern.

The cache is an accelerator, not a dependency: if swiping outruns the
download, `uriFor` silently falls back to the CDN URL.

## Why there is no native password login

Researched and measured (August 2026). The obstacle to a personalized
"For You" feed is not the cookie, it is the **request signature**:

| Route | Result |
|---|---|
| Web endpoints + cookie | WAF challenge — closed |
| Mobile endpoints, full app params, unsigned | `429 ratelimit` |
| Cookie alone | Not enough; the signature is required independently of the session |
| Official Display API (OAuth) | Works, but only your own videos; no feed |

The mobile API demands four headers **together**: `X-Argus` (protobuf → SM3 →
SIMON → AES), `X-Gorgon`, `X-Ladon`, `X-Khronos`. X-Gorgon alone no longer
suffices.

These are pure algorithms — no native `.so`, no remote signing service — so
they could be ported to C++, and AES is already in mbedtls. In other words it
is **technically possible**. The reason it isn't done is cost: the protobuf
field layout must match exactly, the algorithms rotate periodically and each
rotation means re-porting, and unusual client signatures can get a real
account flagged.

That is why login works by **carrying the session cookie** instead (the same
approach academic tools like traktok use): you log in inside your own browser,
the extension hands the `sessionid` to the console via a PIN, and it stays on
the SD card — never sent to any third party.

## Known gaps

- **Decode headroom.** The source is 720x1280 H.264 and NVDEC is not wired up
  in this build, so it is software decode on 3 cores. If frames drop, in
  order: `vd-lavc-skiploopfilter=all`, `framedrop=vo`, and as a last resort
  downscaling with `--vf=scale`.
- **Stream interruptions.** The log occasionally shows `mpv/ffmpeg: tls:
  mbedtls_ssl_read returned -0x0` followed by `Packet corrupt` / `Invalid NAL
  unit size`; mpv reconnects and recovers. The clip cache's socket contention
  was suspected first, but **it persists with prefetch fully disabled** — so
  the cause is not the app. A PC on the same network (Ethernet) gets clean CDN
  responses while the console is on WiFi; the difference is probably there.
- **Audio loop artifact (cosmetic).** Every loop wrap logs
  `ao/hos: audio end or underrun` → `starting AO` → `Error writing audio to
  device`. Audible output is unaffected; the audio stream ends as the clip
  wraps, the AO reopens and loses one write in that instant.
  `audio-stream-silence=yes` was tried, **did not fix it**, and mpv itself
  warns the option breaks some player behaviors — so it is not used.
- **romfs bloat.** CMake copies `lib/borealis/resources/` wholesale, which
  includes borealis demo assets (`img/pokemon/`, `img/tiles.png`, `xml/`).
  `img/sys/` is needed (status bar icons), the rest could be dropped for a
  ~1.9 MB saving.

## Notes

- This is a homebrew project: it requires Atmosphère, cannot ship on the
  eShop, and carries a ban risk.
- Wiring up a real TikTok source violates TikTok's terms of service. The
  official API (Display API) only returns the authorizing user's own videos;
  the "For You" feed is not public.
- Reference implementation: [wiliwili](https://github.com/xfangfang/wiliwili)
  — same stack (borealis + libmpv), production quality.

# switch-tok

A vertical, swipe-style short-video feed client for the Nintendo Switch
(Tegra X1 / Atmosphère). **borealis** for the UI, **libmpv** for playback, and a
small **self-hosted bridge** that signs TikTok's mobile API so the console can
pull your real personalized **For You** feed after a one-time login.

## Download & install

Grab the latest zip from
[Releases](https://github.com/metinxsezdin/switch-tok/releases), extract it to
the root of your SD card (it lands as `switch/switch-tok/switch-tok.nro`) and
launch it from hbmenu.

## Architecture

The console never talks to TikTok's signed API directly. It talks to one bridge
host — `tok.menaworks.xyz`, a tiny always-on service — which does the signing
and returns plain JSON:

```
  Switch (.nro)
        |
        |  HTTPS GET https://tok.menaworks.xyz/api/foryou?count=..
        |     header: X-Session-Id: <your sessionid>   (only when logged in)
        |     libcurl + mbedtls, CA bundle at romfs:/cacert.pem
        v
  bridge (server/tokserver.py, behind Caddy)
        |  logged in : sign /aweme/v1/feed with SignerPy (X-Argus/Ladon/Gorgon)
        |              using your sessionid -> your real For You feed
        |  logged out: fall back to a public generic feed (tikwm)
        v
  JSON: { code:0, data:[ {play, video_id, title, author:{unique_id}}, ... ] }
        |
        |  mpv streams each `play` URL straight from TikTok's CDN
        v
  borealis : UI, input, focus
  libmpv   : demux, decode, A/V sync, audio, cache
```

The `play` URLs point at TikTok's CDN and are fetched directly by mpv (a bare GET
with a User-Agent returns the MP4, Range-capable). The bridge only ever returns
JSON — no media passes through it.

### Why a bridge, and why the signing lives there

TikTok's personalized feed is behind its mobile API, which rejects any request
missing four signature headers assembled together — `X-Argus` (protobuf → SM3 →
SIMON → AES), `X-Gorgon`, `X-Ladon`, `X-Khronos`. These are pure algorithms, but
they **rotate periodically**. Putting them on the console would mean porting the
whole stack to C++ and reshipping a new `.nro` every time TikTok changes it.

On the bridge they are one `pip` dependency ([SignerPy](https://pypi.org/project/SignerPy/)):
when the algorithm rotates you update the server, not the firmware. Measured
Aug 2026: a signed `/aweme/v1/feed` call with a valid `sessionid` returns the
real For You feed with playable MP4 URLs — verified both from a home IP and from
the bridge's own datacenter IP, so hosting the signing server-side works.

One detail the bridge handles for you: the request host must match the account's
datacenter (`tt-target-idc`) — `api16-normal-c-<idc>.tiktokv.com`. A mismatched
host or a request with no session gets `429 ratelimit triggered`. The bridge
discovers the working idc by sweeping the candidates and remembers it per
session.

### Login (PIN) and the Chrome extension

The `sessionid` cookie is HttpOnly, so nothing running in a page — bookmarklet or
site JS — can read it. A **companion Chrome extension** reads it and hands it to
the bridge, which returns a short-lived PIN you type on the console:

- **`chrome-extension/`** — the extension.
- **`switch-feed-web/`** — the login page (a static Next.js export served by the
  bridge at `tok.menaworks.xyz`).
- **`server/tokserver.py`** — the bridge itself (login handoff + feed signing).

1. Load `chrome-extension/` as an unpacked extension in Chrome (Developer
   Mode → "Load unpacked"), or download it from the login page.
2. Log in to tiktok.com in that browser with your own account.
3. Click the extension and press **"Transfer session (get PIN)"** — you get a
   **6-digit PIN**.
4. On the Switch, press **`Y` → "Log in (TikTok PIN)"** and enter the PIN. The
   console fetches your `sessionid` from the bridge, stores it on the SD card,
   and every feed request from then on is personalized.

The PIN↔sessionid handoff lives in the bridge process's memory, single-use, with
a 10-minute TTL — no third-party datastore, and the sessionid is sent only to
our own host (`X-Session-Id`), never to any other party.

### Without logging in

Skip the login and the feed still works: the bridge serves a public generic feed
(mirrored via `tikwm.com`) so first launch shows videos immediately. Logging in
swaps that for your own For You.

`tikwm` is a third party's free service used only for the logged-out fallback and
for keyword search. It gets rate-limited and can change; the code treats a
non-zero `code` inside an HTTP 200 as an error and surfaces it, and the personal
feed path does not depend on it at all.

## The bridge (server/tokserver.py)

A single long-running Python process (stdlib HTTP + SignerPy), behind Caddy for
automatic HTTPS. Deployed here on an Oracle Cloud "Always Free" VM as a systemd
service, reachable at `tok.menaworks.xyz`.

Routes (Caddy serves the static login UI itself and reverse-proxies `/api/*`):

| Route | Purpose |
|---|---|
| `GET /api/foryou?count=..` | personalized feed (needs `X-Session-Id`), generic fallback otherwise |
| `GET /api/search?keywords=..` | keyword search (via tikwm) |
| `GET /api/save?sid=..` | extension mints a PIN |
| `GET /api/get?pin=..` | console redeems a PIN to a sessionid (single-use) |

Run it anywhere with Python 3 and a domain:

```sh
python3 -m venv venv && ./venv/bin/pip install SignerPy
./venv/bin/python server/tokserver.py     # listens on 127.0.0.1:8090
```

Then point a reverse proxy with HTTPS (Caddy, nginx) at it and set the console's
`kApiBase` to that host. Rate limiting is real even on success, so the bridge
caches each session's feed briefly and spaces out upstream hits.

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

The bridge host and endpoints live in [source/config.hpp](source/config.hpp):
`kApiBase` (default `https://tok.menaworks.xyz`) and `kBridgeHost`. If you
self-host the bridge, change both to your domain and rebuild. `net.cpp` sends
the `sessionid` as `X-Session-Id` only to `kBridgeHost`, and as a cookie only to
real TikTok hosts — never to anyone else.

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

Each call returns a fresh batch rather than the next page, so the feed simply
grows: 4 videos before the end of the list, a new batch is fetched in the
background and appended with `video_id` duplicates dropped. Going forward reaches
new material; going back stops at the beginning. The service may return fewer
videos than `count` asked for, so never rely on a fixed number.

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
- The feed is fetched and parsed on device
- **Video renders on screen** (mpv → FBO → nanovg) and ↑/↓ navigation between
  videos works

### The feed's shape is not uniform

A third-party source offers no schema guarantee — in one sample a record's
`author.unique_id` came back as an object instead of a string, and nlohmann's
`value(key, default)` throws on a type mismatch, so one malformed record used to
take down the whole feed. `readString()` in
[source/feed_activity.cpp](source/feed_activity.cpp) reads every field with a
type check; one odd record must not cost the other nineteen.

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

## Known gaps

- **Decode headroom.** The source is 720x1280 H.264 and NVDEC is not wired up
  in this build, so it is software decode on 3 cores. If frames drop, in
  order: `vd-lavc-skiploopfilter=all`, `framedrop=vo`, and as a last resort
  downscaling with `--vf=scale`.
- **Stream interruptions.** The log occasionally shows `mpv/ffmpeg: tls:
  mbedtls_ssl_read returned -0x0` followed by `Packet corrupt` / `Invalid NAL
  unit size`; mpv reconnects and recovers. It persists with prefetch fully
  disabled, so the cause is not the app — a PC on the same network (Ethernet)
  gets clean CDN responses while the console is on WiFi.
- **Signing rotation.** The mobile-API signature algorithms change from time to
  time. When they do, `pip install -U SignerPy` on the bridge (or a patch) is
  the fix — the console is unaffected.
- **romfs bloat.** CMake copies `lib/borealis/resources/` wholesale, which
  includes borealis demo assets. `img/sys/` is needed (status bar icons), the
  rest could be dropped for a ~1.9 MB saving.

## Notes

- This is a homebrew project: it requires Atmosphère, cannot ship on the
  eShop, and carries a ban risk.
- Using the personalized feed drives TikTok's mobile API with your own session
  and violates TikTok's terms of service; unusual client signatures can get an
  account flagged. Use an account you are willing to risk.
- Reference implementation: [wiliwili](https://github.com/xfangfang/wiliwili)
  — same stack (borealis + libmpv), production quality.

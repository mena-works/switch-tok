#!/usr/bin/env python3
"""
switch-tok backend (Oracle box, behind Caddy at tok.menaworks.xyz).

Replaces the Vercel bridge entirely. One long-running process, so:
  - PIN <-> sessionid handoff lives in memory (no third-party KV, no crypto
    dance): the sessionid never leaves this box's RAM, is single-use, 10 min TTL.
  - The For You feed is fetched here: SignerPy signs /aweme/v1/feed with the
    account's sessionid, the working idc is discovered and remembered, results
    are cached briefly and rate-limited so a burst of swipes can't trip TikTok.

Routes (Caddy reverse-proxies /api/* here; it serves the static UI itself):
  GET /api/save?sid=...[&format=json]  -> mint PIN (extension hits this)
  GET /api/get?pin=...                 -> redeem PIN to sessionid (Switch polls)
  GET /api/foryou?count=..             -> personalized feed; sid via X-Session-Id
  GET /api/search?keywords=..&count=.. -> keyword search (tikwm, no session)
  GET /healthz

Feed JSON matches what the Switch already parses:
  {"code":0,"data":[{"play","video_id","title","author":{"unique_id"}}, ...]}
"""
import json, time, threading, gzip, random
import urllib.request, urllib.error
from urllib.parse import urlparse, parse_qs, urlencode
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import SignerPy

PORT = 8090
PIN_TTL = 10 * 60
FEED_CACHE_TTL = 25          # seconds a signed feed result is reused per session
MIN_GAP = 3.0                # min seconds between upstream feed hits per session
UA = ("com.zhiliaoapp.musically/2023205030 (Linux; U; Android 13; en; "
      "Pixel 7; Build/TQ3A.230805.001; Cronet/58.0.2991.0)")

# idc -> idc-matched host (a mismatched host 429s). Ordered by observed success.
IDC_HOSTS = [
    ("useast2a", "api16-normal-c-useast2a.tiktokv.com"),
    ("useast5",  "api16-normal-useast5.us.tiktokv.com"),
    ("useast1a", "api16-normal-c-useast1a.tiktokv.com"),
    ("alisg",    "api16-normal-c-alisg.tiktokv.com"),
]

# ---------------------------------------------------------------- state
_pins = {}                    # pin -> {"sid":.., "ts":..}
_pin_lock = threading.Lock()
_sess = {}                    # sid -> {"idc":.., "feed":.., "feed_ts":.., "last":..}
_sess_lock = threading.Lock()


def _now():
    return time.time()


def mint_pin(sid):
    with _pin_lock:
        for p, v in list(_pins.items()):
            if _now() - v["ts"] > PIN_TTL:
                del _pins[p]
        pin = str(random.randint(100000, 999999))
        _pins[pin] = {"sid": sid, "ts": _now()}
        return pin


def redeem_pin(pin):
    with _pin_lock:
        v = _pins.get(pin)
        if not v or _now() - v["ts"] > PIN_TTL:
            _pins.pop(pin, None)
            return None
        del _pins[pin]           # single use
        return v["sid"]


# ---------------------------------------------------------------- upstream
def _http_get(url, headers, timeout=25):
    try:
        r = urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=timeout)
        raw, st = r.read(), r.status
    except urllib.error.HTTPError as e:
        raw, st = e.read(), e.code
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return st, raw


def _feed_params():
    p = {
        "type": "0", "count": "12", "feed_style": "0",
        "max_cursor": "0", "min_cursor": "0", "pull_type": "0", "aweme_length": "0",
        "aid": "1233", "app_name": "trill",
        "version_code": "320503", "version_name": "32.5.3",
        "manifest_version_code": "2023205030", "update_version_code": "2023205030",
        "app_type": "normal", "channel": "googleplay",
        "device_platform": "android", "device_type": "Pixel 7", "device_brand": "Google",
        "os": "android", "os_version": "13", "os_api": "33",
        "resolution": "1080*2400", "dpi": "420",
        "region": "US", "sys_region": "US", "carrier_region": "US",
        "language": "en", "locale": "en",
        "timezone_name": "America/New_York", "timezone_offset": "-14400",
        "ac": "wifi", "ssmix": "a",
    }
    return SignerPy.get(p)


def _fetch_aweme(sid, idc, host):
    params = _feed_params()
    cookie = f"sessionid={sid}; tt-target-idc={idc}"
    signed = SignerPy.sign(params=params, cookie=cookie)
    url = f"https://{host}/aweme/v1/feed/?{urlencode(params)}"
    headers = {"User-Agent": UA, "Accept-Encoding": "gzip",
               "Cookie": cookie, "sdk-version": "2", **signed}
    st, raw = _http_get(url, headers)
    if st != 200:
        return None
    try:
        d = json.loads(raw.decode("utf-8", "replace"))
    except Exception:
        return None
    if d.get("status_code") != 0:
        return None
    return d.get("aweme_list") or []


def _normalize(aweme_list):
    out = []
    for v in aweme_list:
        vid = v.get("video") or {}
        urls = (vid.get("play_addr") or {}).get("url_list") or []
        if not urls:
            continue
        out.append({
            "play": urls[0],
            "video_id": str(v.get("aweme_id") or ""),
            "title": v.get("desc") or "",
            "author": {"unique_id": (v.get("author") or {}).get("unique_id") or "unknown"},
        })
    return out


def get_foryou(sid):
    with _sess_lock:
        s = _sess.setdefault(sid, {})
        # serve cache if fresh
        if s.get("feed") and _now() - s.get("feed_ts", 0) < FEED_CACHE_TTL:
            return {"code": 0, "data": s["feed"]}
        # rate limit per session: if hit too recently, serve last good if any
        if _now() - s.get("last", 0) < MIN_GAP and s.get("feed"):
            return {"code": 0, "data": s["feed"]}
        s["last"] = _now()
        known = s.get("idc")

    order = ([p for p in IDC_HOSTS if p[0] == known] +
             [p for p in IDC_HOSTS if p[0] != known]) if known else IDC_HOSTS

    for idc, host in order:
        aweme = _fetch_aweme(sid, idc, host)
        if aweme:
            data = _normalize(aweme)
            with _sess_lock:
                s = _sess.setdefault(sid, {})
                s["idc"], s["feed"], s["feed_ts"] = idc, data, _now()
            return {"code": 0, "data": data}

    # everything failed: last good cache, else an error the Switch surfaces
    with _sess_lock:
        s = _sess.get(sid) or {}
        if s.get("feed"):
            return {"code": 0, "data": s["feed"]}
    return {"code": -1, "msg": "feed unavailable (rate limited or session invalid)"}


def get_generic(region, count):
    # Logged-out fallback: tikwm's public generic feed, already in the shape the
    # Switch parses ({code:0, data:[{play,video_id,title,author:{unique_id}}]}).
    region = region or "US"
    url = f"https://www.tikwm.com/api/feed/list?region={region}&count={count}"
    st, raw = _http_get(url, {"User-Agent": UA})
    try:
        return json.loads(raw.decode("utf-8", "replace"))
    except Exception:
        return {"code": -1, "msg": "feed unavailable"}


def get_search(keywords, count):
    url = f"https://www.tikwm.com/api/feed/search?keywords={urlencode({'k': keywords})[2:]}&count={count}"
    st, raw = _http_get(url, {"User-Agent": UA})
    try:
        d = json.loads(raw.decode("utf-8", "replace"))
    except Exception:
        return {"code": -1, "msg": "search failed"}
    return d


# ---------------------------------------------------------------- http
class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _send(self, code, obj, ctype="application/json; charset=utf-8"):
        body = obj if isinstance(obj, bytes) else json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _redirect(self, location):
        self.send_response(302)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *a):
        pass

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        path = u.path

        if path == "/healthz":
            return self._send(200, {"ok": True})

        if path == "/api/save":
            sid = (q.get("sid") or [""])[0].strip()
            if not sid:
                return self._send(400, {"error": "missing sid"})
            pin = mint_pin(sid)
            if (q.get("format") or [""])[0] == "json":
                return self._send(200, {"pin": pin})
            return self._redirect(f"/success?pin={pin}")

        if path == "/api/get":
            pin = (q.get("pin") or [""])[0].strip()
            if not pin:
                return self._send(400, {"error": "missing pin"})
            sid = redeem_pin(pin)
            if not sid:
                return self._send(404, {"error": "invalid or expired PIN"})
            return self._send(200, {"sessionid": sid})

        if path == "/api/foryou":
            sid = (self.headers.get("X-Session-Id") or "").strip()
            count = (q.get("count") or ["12"])[0]
            region = (q.get("region") or [""])[0]
            if not sid:
                # logged out: generic public feed, so first launch still works
                return self._send(200, get_generic(region, count))
            res = get_foryou(sid)
            if res.get("code") != 0:
                res = get_generic(region, count)   # rate-limited/invalid -> generic
            return self._send(200, res)

        if path == "/api/search":
            kw = (q.get("keywords") or [""])[0]
            count = (q.get("count") or ["12"])[0]
            if not kw:
                return self._send(400, {"code": -1, "msg": "empty search"})
            return self._send(200, get_search(kw, count))

        return self._send(404, {"error": "not found"})


if __name__ == "__main__":
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), H)
    print(f"switch-tok backend on 127.0.0.1:{PORT}")
    srv.serve_forever()

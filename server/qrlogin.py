#!/usr/bin/env python3
"""
TikTok TV QR-login for the switch-tok bridge — "real TV" model.

The bridge owns ONE registered TV device (server/device.json, captured from a
real TikTok TV run; device_register anti-fraud blocks synthetic registration).
A user opens the Switch app, sees a QR, scans it with their logged-in TikTok
phone and confirms — the bridge polls check_qrconnect and captures the account
sessionid. No Chrome extension, no manual sessionid paste.

idc handling (multi-idc, sequential): we don't know the scanning user's account
idc up front, and the QR token is idc-local (a token minted on useast2a is
invisible to a useast1a phone). So a QR session cycles candidate idcs: it shows
the QR for idc[k], and if the phone hasn't scanned within ROTATE_SECS it re-mints
on idc[k+1] and updates the QR. Whichever idc the user is on eventually matches,
advances to `scanned` -> `confirmed`, and yields the sessionid.

Endpoints (wired in tokserver.py):
  POST/GET /api/qr/start            -> {qr_id, png_b64, idc, expires_in}
  GET      /api/qr/poll?qr_id=..    -> {status, idc, png_b64?, sessionid?}
       status: new | scanned | confirmed | expired | error
"""
import os, json, time, base64, gzip, threading, random
import urllib.request, urllib.error
from urllib.parse import urlencode

import SignerPy

HERE = os.path.dirname(os.path.abspath(__file__))
DEVICE_PATH = os.path.join(HERE, "device.json")

AID = 1233
UA = ("com.tiktok.tv/1203071 (Linux; U; Android 13; en; AFTKA; "
      "Build/TQ3A.230805.001; Cronet/58.0.2991.0)")
SERVICE = "https://tv.tiktok.com"          # APK sabiti (LoginModel)
SCENE = "0"                                 # APK sabiti (GetQRCodeJob)

# QR token'i denenecek idc host'lari. Kullanicinin hesabi hangisindeyse orada
# 'scanned' olur. useast1a onde (elimizdeki cihazin bilinen-calisan idc'si).
IDC_HOSTS = [
    ("useast1a", "api16-normal-c-useast1a.tiktokv.com"),
    ("useast2a", "api16-normal-c-useast2a.tiktokv.com"),
    ("useast5",  "api16-normal-useast5.us.tiktokv.com"),
    ("alisg",    "api16-normal-c-alisg.tiktokv.com"),
    ("no1a",     "api16-normal-no1a.tiktokv.eu"),
]

ROTATE_SECS = 18          # bir idc'de 'new' kalirsa sonraki idc'ye gec + QR tazele
SESSION_TTL = 8 * 60      # QR oturumu yasam suresi
POLL_MIN_GAP = 2.0        # ayni qr_id icin check_qrconnect'e min aralik


def _load_device():
    if os.path.exists(DEVICE_PATH):
        try:
            return json.load(open(DEVICE_PATH, encoding="utf-8"))
        except Exception:
            pass
    return {}


DEVICE = _load_device()


def _tv_params():
    p = {
        "aid": str(AID), "app_name": "tiktok_tv",
        "version_code": "1203071", "version_name": "12.3.7.1",
        "channel": "googleplay", "device_platform": "android",
        "device_type": "AFTKA", "device_brand": "Amazon",
        "os_version": "13", "os_api": "33",
        "resolution": "1920*1080", "dpi": "320",
        "region": "US", "sys_region": "US", "carrier_region": "US",
        "language": "en", "locale": "en",
        "timezone_name": "America/New_York", "timezone_offset": "-14400",
        "ac": "wifi", "ssmix": "a",
    }
    p = SignerPy.get(p)                       # rastgele device_id/iid/openudid/cdid
    if DEVICE.get("device_id"):
        p["device_id"] = str(DEVICE["device_id"])
    if DEVICE.get("install_id"):
        p["iid"] = str(DEVICE["install_id"])
    if DEVICE.get("openudid"):
        p["openudid"] = str(DEVICE["openudid"])
    if DEVICE.get("cdid"):
        p["cdid"] = str(DEVICE["cdid"])
    return p


def _call(host, path, extra, headers=None, timeout=20):
    p = _tv_params()
    p.update(extra)
    signed = SignerPy.sign(params=p, cookie="", aid=AID)
    url = f"https://{host}{path}?{urlencode(p)}"
    hdr = {"User-Agent": UA, "Accept-Encoding": "gzip", "sdk-version": "2", **signed}
    if headers:
        hdr.update(headers)
    req = urllib.request.Request(url, headers=hdr)
    try:
        r = urllib.request.urlopen(req, timeout=timeout)
        raw, cookies = r.read(), (r.headers.get_all("Set-Cookie") or [])
    except urllib.error.HTTPError as e:
        raw, cookies = e.read(), (e.headers.get_all("Set-Cookie") or [])
    except Exception:
        return {}, []
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    try:
        return json.loads(raw.decode("utf-8", "replace")), cookies
    except Exception:
        return {"_raw": raw[:160].decode("latin1", "ignore")}, cookies


def _get_qrcode(host):
    d, _ = _call(host, "/passport/mobile/get_qrcode/",
                 {"service": SERVICE, "scene": SCENE, "use_long_expiration": "true"})
    data = d.get("data", {})
    if data.get("token") and data.get("qrcode"):
        return data
    return None


def _check(host, token):
    d, cookies = _call(host, "/passport/mobile/check_qrconnect/",
                       {"token": token, "service": SERVICE}, {"x-tt-bypass-dp": "1"})
    data = d.get("data", {})
    sid = None
    for c in cookies:
        if "sessionid=" in c:
            sid = c.split("sessionid=", 1)[1].split(";", 1)[0]
            break
    return data.get("status"), sid, data


# ---------------------------------------------------------------- manager
class QRLogin:
    """QR oturumlarini yonetir. on_session(sid) -> confirmed olunca cagirilir
    (bridge sid'i _sess'e koyup feed'i hazir etsin diye)."""

    def __init__(self, on_session=None):
        self._s = {}                      # qr_id -> session dict
        self._lock = threading.Lock()
        self._on_session = on_session

    def _mint(self, sess):
        """sess["idc_idx"] host'unda yeni token+QR uret; basarisizsa sonrakini dene."""
        n = len(IDC_HOSTS)
        for _ in range(n):
            idc, host = IDC_HOSTS[sess["idc_idx"] % n]
            data = _get_qrcode(host)
            if data:
                sess.update(idc=idc, host=host, token=data["token"],
                            png=data["qrcode"], idc_ts=time.time())
                return True
            sess["idc_idx"] += 1
        return False

    def start(self):
        self._gc()
        if not DEVICE.get("device_id"):
            return {"error": "no_device", "msg": "server/device.json yok (kayitli TV cihazi gerekli)"}
        qr_id = "%08x" % random.randint(0, 0xFFFFFFFF)
        sess = {"idc_idx": 0, "created": time.time(), "last_poll": 0.0,
                "status": "new", "sid": None}
        if not self._mint(sess):
            return {"error": "mint_failed", "msg": "get_qrcode hicbir idc'de basarili olmadi"}
        with self._lock:
            self._s[qr_id] = sess
        return {"qr_id": qr_id, "png_b64": sess["png"], "idc": sess["idc"],
                "expires_in": SESSION_TTL}

    def poll(self, qr_id):
        with self._lock:
            sess = self._s.get(qr_id)
        if not sess:
            return {"status": "expired"}
        if time.time() - sess["created"] > SESSION_TTL:
            with self._lock:
                self._s.pop(qr_id, None)
            return {"status": "expired"}
        if sess["status"] == "confirmed":
            return {"status": "confirmed", "sessionid": sess["sid"], "idc": sess["idc"]}

        # rate limit per qr_id
        if time.time() - sess["last_poll"] < POLL_MIN_GAP:
            return {"status": sess["status"], "idc": sess["idc"]}
        sess["last_poll"] = time.time()

        status, sid, _ = _check(sess["host"], sess["token"])

        if sid or status == "confirmed":
            sess["status"] = "confirmed"
            sess["sid"] = sid
            if sid and self._on_session:
                try:
                    self._on_session(sid, sess["idc"])
                except Exception:
                    pass
            return {"status": "confirmed", "sessionid": sid, "idc": sess["idc"]}

        if status == "scanned":
            sess["status"] = "scanned"          # kullanici onay ekraninda; QR'i sabit tut
            return {"status": "scanned", "idc": sess["idc"]}

        if status in ("expired", "refuse"):
            # bu token dustu -> ayni idc'de yeni token uret (QR tazele)
            if self._mint(sess):
                sess["status"] = "new"
                return {"status": "new", "idc": sess["idc"], "png_b64": sess["png"]}
            return {"status": "expired"}

        # status new/None: bir sure sonra sonraki idc'ye gec (kullanicinin idc'sini ara)
        if time.time() - sess["idc_ts"] > ROTATE_SECS:
            sess["idc_idx"] += 1
            if self._mint(sess):
                sess["status"] = "new"
                return {"status": "new", "idc": sess["idc"], "png_b64": sess["png"]}
        return {"status": "new", "idc": sess["idc"]}

    def _gc(self):
        now = time.time()
        with self._lock:
            for k, v in list(self._s.items()):
                if now - v["created"] > SESSION_TTL:
                    del self._s[k]

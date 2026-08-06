#!/usr/bin/env python3
"""Feed service for switch-feed.

The console asks this for a list of clips and then streams them over plain
HTTP. Everything fragile -- scraping, request signing, rate limits -- belongs
here rather than on the Switch, because this can be redeployed in seconds while
an .nro cannot.

Stdlib only, so it runs anywhere with python3:

    python3 feed_server.py --media ./media --port 8080
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

RANGE_RE = re.compile(r"bytes=(\d*)-(\d*)")

MEDIA_DIR = "media"


def load_items(host: str) -> list[dict]:
    """Build the feed.

    Right now this is just whatever .mp4 files sit in the media directory, with
    optional metadata from media/meta.json:

        {"clip1.mp4": {"author": "someone", "description": "..."}}

    This function is the seam where a real source plugs in. If you wire TikTok
    in here, do the signing and the transcode on this side and keep handing the
    console a plain URL to a pre-transcoded file -- see README.md.
    """
    meta_path = os.path.join(MEDIA_DIR, "meta.json")
    meta = {}
    if os.path.exists(meta_path):
        with open(meta_path, encoding="utf-8") as handle:
            meta = json.load(handle)

    items = []
    for name in sorted(os.listdir(MEDIA_DIR)):
        if not name.lower().endswith((".mp4", ".mkv", ".webm")):
            continue

        entry = meta.get(name, {})
        items.append(
            {
                "id": os.path.splitext(name)[0],
                "author": entry.get("author", "local"),
                "description": entry.get("description", name),
                "video_url": f"http://{host}/video/{name}",
            }
        )
    return items


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quieter than the default
        print(f"{self.address_string()} {fmt % args}")

    def do_GET(self):
        if self.path == "/feed":
            self.send_feed()
        elif self.path.startswith("/video/"):
            self.send_video(self.path[len("/video/") :])
        else:
            self.send_error(404)

    def send_feed(self):
        # The console needs a URL it can actually reach, so echo back whatever
        # address it used to get here instead of hardcoding one.
        host = self.headers.get("Host") or f"{self.server.server_address[0]}:{self.server.server_address[1]}"
        payload = json.dumps({"items": load_items(host)}).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def send_video(self, name: str):
        # No traversal: only a bare filename inside the media directory.
        if "/" in name or "\\" in name or name.startswith("."):
            self.send_error(400)
            return

        path = os.path.join(MEDIA_DIR, name)
        if not os.path.isfile(path):
            self.send_error(404)
            return

        size = os.path.getsize(path)
        ctype = mimetypes.guess_type(path)[0] or "application/octet-stream"

        # mpv issues Range requests for seeking and for its demuxer cache.
        # Answering them properly is the difference between instant playback
        # and mpv refetching the whole file.
        start, end = 0, size - 1
        status = 200
        match = RANGE_RE.match(self.headers.get("Range", ""))
        if match:
            first, last = match.groups()
            if first:
                start = int(first)
                if last:
                    end = int(last)
            elif last:  # suffix range
                start = max(0, size - int(last))

            if start >= size:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.end_headers()
                return
            status = 206

        length = end - start + 1

        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()

        with open(path, "rb") as handle:
            handle.seek(start)
            remaining = length
            while remaining > 0:
                chunk = handle.read(min(64 * 1024, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)


def main():
    global MEDIA_DIR

    parser = argparse.ArgumentParser()
    parser.add_argument("--media", default="media")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--host", default="0.0.0.0")
    args = parser.parse_args()

    MEDIA_DIR = args.media
    os.makedirs(MEDIA_DIR, exist_ok=True)

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"serving {os.path.abspath(MEDIA_DIR)} on http://{args.host}:{args.port}")
    print("point kFeedBaseUrl in source/config.hpp at this machine's LAN address")
    server.serve_forever()


if __name__ == "__main__":
    main()

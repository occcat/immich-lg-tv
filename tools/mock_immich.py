#!/usr/bin/env python3
"""Minimal Immich-compatible fixture server. This is NOT an Immich deployment.

The implemented contracts follow Immich v3.3.5's OpenAPI, with a v2 response
mode for compatibility tests. Every request is kept in memory for assertions.
Authentication uses the public fixture key 'immich-tv-demo'. Mutations fail.
"""
from argparse import ArgumentParser
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, parse_qs
import hashlib
import io
import json
import re
import threading
import uuid

ROOT = Path(__file__).resolve().parent.parent
MEDIA = ROOT / "fixtures" / "media"
DEMO_KEY = "immich-tv-demo"

def ident(value):
    return str(uuid.UUID(bytes=hashlib.md5(value.encode()).digest(), version=4))

def dataset():
    places = ["阿尔卑斯山", "湖畔", "山间", "雪山", "旅途中", "日落时分", "山野", "周末", "海边", "远方"]
    user = ident("demo-user")
    assets = []
    # More than a page, including a partly-filled final row and page.
    for i in range(47):
        video = i in [3, 17, 31]
        stamp = (datetime(2026, 10, 24, 10, 30, tzinfo=timezone.utc) - timedelta(days=i)).isoformat().replace("+00:00", "Z")
        assets.append({"id": ident(f"asset-{i}"), "type": "VIDEO" if video else "IMAGE", "originalFileName": f"旅途片段_{i+1:02d}.mp4" if video else f"生活的第{i+1:02d}帧.jpg", "ownerId": user, "checksum": "ZGVtby1maXh0dXJl", "createdAt": stamp, "updatedAt": stamp, "fileCreatedAt": stamp, "fileModifiedAt": stamp, "localDateTime": stamp, "duration": 12000 if video else None, "width": 1280 if video else 1920, "height": 720 if video else 1080, "isFavorite": i % 4 == 0, "isArchived": False, "isTrashed": False, "isOffline": False, "isEdited": False, "hasMetadata": True, "visibility": "timeline", "originalPath": f"/synthetic-fixtures/{i}.jpg", "thumbhash": None, "exifInfo": {"city": places[i % len(places)], "model": "Demo Camera", "exifImageWidth": 1920, "exifImageHeight": 1080}, "_photo": i % 20})
    names = ["山野来信", "沿着海岸线", "平凡的一天", "和世界碰个面", "会动的回忆", "空相册"]
    albums = []
    for i, name in enumerate(names):
        members = [a["id"] for a in assets if (a["type"] == "VIDEO" if i == 4 else int(a["_photo"]) % 4 == i)] if i < 5 else []
        albums.append({"id": ident(f"album-{i}"), "albumName": name, "description": "仅用于客户端测试的合成图库", "assetCount": len(members), "albumThumbnailAssetId": members[0] if members else None, "shared": i == 2, "albumUsers": [{"user": {"id": user, "name": "测试用户"}, "role": "owner"}], "createdAt": "2026-10-01T00:00:00.000Z", "updatedAt": "2026-10-24T00:00:00.000Z", "isActivityEnabled": False, "hasSharedLink": False, "_ids": members})
    return assets, albums

ASSETS, ALBUMS = dataset()

def public(value):
    return {k: v for k, v in value.items() if not k.startswith("_")}

class FixtureServer(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, version=3, prefix=""):
        super().__init__(address, Handler)
        self.version = version
        self.prefix = prefix.rstrip("/")
        self.requests = []
        self.request_lock = threading.Lock()

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        # No headers, API keys, or credentials enter logs.
        pass

    def respond(self, status, body, content_type="application/json", extra=None):
        data = json.dumps(body, ensure_ascii=False).encode() if content_type == "application/json" else body
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "private, max-age=0")
        if extra:
            for k, v in extra.items(): self.send_header(k, str(v))
        self.end_headers()
        if self.command != "HEAD":
            try: self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError): pass

    def request_info(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if self.server.prefix and path.startswith(self.server.prefix + "/"): path = path[len(self.server.prefix):]
        size = int(self.headers.get("Content-Length", "0"))
        if size > 65536:
            self.respond(413, {"message": "Request too large"}); return None
        data = self.rfile.read(size) if size else b""
        try: body = json.loads(data) if data else {}
        except json.JSONDecodeError:
            self.respond(400, {"message": "Invalid JSON"}); return None
        with self.server.request_lock:
            self.server.requests.append({"method": self.command, "path": path, "body": body, "authenticated": self.headers.get("x-api-key") == DEMO_KEY, "range": self.headers.get("Range")})
        return path, parse_qs(parsed.query), body

    def do_GET(self): self.route()
    def do_HEAD(self): self.route()
    def do_POST(self): self.route()
    def do_PUT(self): self.route()
    def do_PATCH(self): self.route()
    def do_DELETE(self): self.route()

    def route(self):
        info = self.request_info()
        if info is None: return
        path, query, body = info
        if path == "/health":
            writes = [r for r in self.server.requests if r["method"] not in ["GET", "HEAD"] and not (r["method"] == "POST" and r["path"] == "/api/search/metadata")]
            self.respond(200, {"fixture": True, "version": self.server.version, "assets": len(ASSETS), "requests": len(self.server.requests), "mutations": len(writes)}); return
        if self.headers.get("x-api-key") != DEMO_KEY:
            self.respond(401, {"message": "Invalid API key"}); return
        if path == "/diagnostics/requests" and self.command == "GET":
            self.respond(200, self.server.requests); return
        if self.command not in ["GET", "HEAD"] and not (self.command == "POST" and path == "/api/search/metadata"):
            self.respond(405, {"message": "Read-only fixture: mutations are disabled"}); return
        if path == "/api/server/version":
            self.respond(200, {"major": self.server.version, "minor": 3 if self.server.version >= 3 else 5, "patch": 5}); return
        if path == "/api/server/ping": self.respond(200, {"res": "pong"}); return
        if path == "/api/albums": self.respond(200, [public(a) for a in ALBUMS]); return
        if path.startswith("/api/albums/"):
            album = next((a for a in ALBUMS if a["id"] == path.rsplit("/", 1)[1]), None)
            self.respond(200 if album else 404, public(album) if album else {"message": "Not found"}); return
        if path == "/api/search/metadata" and self.command == "POST":
            selected = list(ASSETS)
            if self.server.version >= 3:
                allowed = {"size", "cursor", "filter", "orderBy", "withExif"}
                if set(body) - allowed: self.respond(400, {"message": "Unexpected v3 fields"}); return
                filt = body.get("filter", {})
                allowed_visibility = filt.get("visibility", {}).get("eq") == "timeline" or filt.get("visibility", {}).get("in") == ["timeline", "archive"]
                if not allowed_visibility or filt.get("trashedAt", {}).get("eq", "not-null") is not None:
                    self.respond(400, {"message": "Unsafe visibility filter"}); return
                favorite = filt.get("isFavorite", {}).get("eq")
                kind = filt.get("type", {}).get("eq")
                album_ids = filt.get("albumIds", {}).get("any", [])
                cursor = body.get("cursor", "0")
                try: offset = int(cursor)
                except ValueError: self.respond(400, {"message": "Bad cursor"}); return
            else:
                favorite = body.get("isFavorite")
                kind = body.get("type")
                album_ids = body.get("albumIds", [])
                offset = (int(body.get("page", 1)) - 1) * int(body.get("size", 40))
            if favorite: selected = [a for a in selected if a["isFavorite"]]
            if kind: selected = [a for a in selected if a["type"] == kind]
            if album_ids:
                members = {aid for album in ALBUMS if album["id"] in album_ids for aid in album["_ids"]}
                selected = [a for a in selected if a["id"] in members]
            size = min(1000, max(1, int(body.get("size", 40))))
            items = selected[offset:offset+size]
            next_offset = offset + size if offset + size < len(selected) else None
            results = [public(a) for a in items]
            if self.server.version == 2:
                for a in results:
                    a["duration"] = "00:00:12.000" if a["type"] == "VIDEO" else "0:00:00.000"
            self.respond(200, {"albums": {"items": [], "total": 0, "count": 0, "facets": []}, "assets": {"items": results, "total": len(selected), "count": len(items), "facets": [], "nextCursor": str(next_offset) if next_offset is not None and self.server.version >= 3 else None, "nextPage": str(offset // size + 2) if next_offset is not None and self.server.version == 2 else None}}); return
        match = re.fullmatch(r"/api/assets/([a-f0-9-]+)(/thumbnail|/video/playback)?", path)
        if match:
            asset = next((a for a in ASSETS if a["id"] == match[1]), None)
            if not asset: self.respond(404, {"message": "Not found"}); return
            if not match[2]: self.respond(200, public(asset)); return
            if match[2] == "/thumbnail":
                preview = query.get("size", [""])[0] in ["preview", "fullsize"]
                name = f"photo-{asset['_photo']:02d}.jpg" if preview else f"thumb-{asset['_photo']:02d}.webp"
                file = MEDIA / name
                if not file.exists(): self.respond(503, {"message": "Run make fixtures first"}); return
                self.respond(200, file.read_bytes(), "image/jpeg" if preview else "image/webp"); return
            if asset["type"] != "VIDEO": self.respond(404, {"message": "Not a video"}); return
            file = MEDIA / ("sample-audio.mp4" if asset["id"] == ident("asset-17") else "sample.mp4")
            if not file.exists(): self.respond(503, {"message": "Video fixture missing"}); return
            self.video(file); return
        self.respond(404, {"message": "Unsupported fixture endpoint"})

    def video(self, file):
        data = file.read_bytes()
        range_header = self.headers.get("Range")
        if not range_header:
            self.respond(200, data, "video/mp4", {"Accept-Ranges": "bytes"}); return
        match = re.fullmatch(r"bytes=(\d*)-(\d*)", range_header)
        if not match or not any(match.groups()):
            self.respond(416, b"", "video/mp4", {"Content-Range": f"bytes */{len(data)}"}); return
        start = int(match[1]) if match[1] else max(0, len(data)-int(match[2]))
        end = min(len(data)-1, int(match[2])) if match[1] and match[2] else len(data)-1
        if start > end or start >= len(data):
            self.respond(416, b"", "video/mp4", {"Content-Range": f"bytes */{len(data)}"}); return
        self.respond(206, data[start:end+1], "video/mp4", {"Accept-Ranges": "bytes", "Content-Range": f"bytes {start}-{end}/{len(data)}"})

def main():
    parser = ArgumentParser(); parser.add_argument("--host", default="127.0.0.1"); parser.add_argument("--port", type=int, default=22830); parser.add_argument("--version", type=int, choices=[2,3], default=3); parser.add_argument("--prefix", default=""); parser.add_argument("--request-log", type=Path)
    args = parser.parse_args()
    server = FixtureServer((args.host, args.port), args.version, args.prefix)
    print(f"Immich-compatible TEST FIXTURE http://{args.host}:{args.port}{args.prefix} / API key: {DEMO_KEY}", flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        if args.request_log: args.request_log.write_text(json.dumps(server.requests, ensure_ascii=False, indent=2) + "\n")
        server.server_close()

if __name__ == "__main__": main()

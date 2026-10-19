#!/usr/bin/env python3
"""Standalone browser viewer for CavEX navigation JSON snapshots.

Usage:
    python3 debug/nav_viewer_server.py
    python3 debug/nav_viewer_server.py --port 8765 --data-dir /tmp/cavex_nav
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


SCRIPT_DIR = Path(__file__).resolve().parent


class NavViewerHandler(SimpleHTTPRequestHandler):
    server_version = "CavEXNavViewer/1.0"

    def __init__(self, *args, data_dir: Path, **kwargs):
        self.data_dir = data_dir
        super().__init__(*args, directory=str(SCRIPT_DIR), **kwargs)

    def _json(self, value, status=200):
        payload = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/list":
            files = sorted(
                p.name for p in self.data_dir.glob("*.json")
                if p.is_file()
            )
            return self._json(files)

        if parsed.path.startswith("/api/map/"):
            name = Path(unquote(parsed.path[len("/api/map/"):])).name
            if not name.endswith(".json") or name != Path(name).name:
                return self._json({"error": "invalid map name"}, 400)
            source = self.data_dir / name
            if not source.is_file():
                return self._json({"error": "map not found"}, 404)
            try:
                with source.open("r", encoding="utf-8") as stream:
                    return self._json(json.load(stream))
            except (OSError, json.JSONDecodeError) as exc:
                return self._json({"error": str(exc)}, 500)

        if parsed.path == "/" or parsed.path == "/index.html":
            self.path = "/nav_viewer.html"
        return super().do_GET()

    def log_message(self, fmt, *args):
        print("[nav-viewer] " + (fmt % args))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--data-dir", default="/tmp/cavex_nav")
    args = parser.parse_args()

    data_dir = Path(args.data_dir).expanduser().resolve()
    if not data_dir.is_dir():
        parser.error("JSON directory does not exist: %s" % data_dir)

    def handler(*handler_args, **handler_kwargs):
        NavViewerHandler(*handler_args, data_dir=data_dir, **handler_kwargs)

    server = ThreadingHTTPServer((args.host, args.port), handler)
    print("CavEX navigation viewer: http://%s:%d" % (args.host, args.port))
    print("JSON directory: %s" % data_dir)
    print("Beenden mit Ctrl+C")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[nav-viewer] beendet")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

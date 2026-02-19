#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
#
# SPDX-License-Identifier: MIT

from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse
import json
import os


HOST = os.environ.get("OPENEMOTE_MOCK_HOST", "127.0.0.1")
PORT = int(os.environ.get("OPENEMOTE_MOCK_PORT", "18080"))


GLOBAL_EMOTES = {
    "emotes": [
        {
            "code": "OPENHYPE",
            "urls": {
                "1x": "https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/1.0",
                "2x": "https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0",
                "4x": "https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/3.0",
            },
            "tooltip": "OpenEmote mock global",
            "homepage": "https://example.com/openhype",
        }
    ]
}


CHANNEL_EMOTES = {
    "emotes": [
        {
            "code": "OPENWAVE",
            "urls": {
                "1x": "https://static-cdn.jtvnw.net/emoticons/v2/1902/default/dark/1.0",
                "2x": "https://static-cdn.jtvnw.net/emoticons/v2/1902/default/dark/2.0",
                "4x": "https://static-cdn.jtvnw.net/emoticons/v2/1902/default/dark/3.0",
            },
            "tooltip": "OpenEmote mock channel",
            "homepage": "https://example.com/openwave",
        }
    ]
}


class Handler(BaseHTTPRequestHandler):
    def _write_json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        path = urlparse(self.path).path

        if path in {"/health", "/api/health"}:
            self._write_json(200, {"ok": True, "name": "openemote-mock"})
            return

        if path in {"/v1/emotes/global", "/api/emotes/global"}:
            self._write_json(200, GLOBAL_EMOTES)
            return

        if path.startswith("/v1/emotes/twitch/") or path.startswith("/api/emotes/channel/"):
            self._write_json(200, CHANNEL_EMOTES)
            return

        self._write_json(404, {"ok": False, "error": "not_found"})

    def log_message(self, _fmt: str, *_args) -> None:
        return


def main() -> None:
    server = HTTPServer((HOST, PORT), Handler)
    print(f"openemote-mock listening on http://{HOST}:{PORT}")
    server.serve_forever()


if __name__ == "__main__":
    main()

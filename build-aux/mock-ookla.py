#!/usr/bin/env python3
"""Tiny stand-in for an Ookla (speedtest.net) server, for testing Velocity locally.

Serves the endpoints Ookla servers expose under /speedtest/: latency.txt,
random4000x4000.jpg (download) and upload.php (upload).

    ./build-aux/mock-ookla.py [port]
    VELOCITY_SERVER_URL=http://127.0.0.1:8080/speedtest/ VELOCITY_SERVER_TYPE=ookla ./build/src/velocity
"""

import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

FILE_SIZE = 32 * 1024 * 1024
BLOCK = os.urandom(1024 * 1024)
CHUNK_SIZE = 1 << 16


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _common(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Access-Control-Allow-Origin", "*")

    def _reply(self, body=b"", ctype="text/plain"):
        self.send_response(200)
        self._common()
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path.endswith("/latency.txt"):
            self._reply(b"test=test\n")
        elif path.endswith("/random4000x4000.jpg"):
            self.send_response(200)
            self._common()
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(FILE_SIZE))
            self.end_headers()
            sent = 0
            try:
                while sent < FILE_SIZE:
                    n = min(CHUNK_SIZE, FILE_SIZE - sent)
                    self.wfile.write(BLOCK[:n])
                    sent += n
            except (BrokenPipeError, ConnectionResetError):
                pass
        else:
            self.send_error(404)

    def do_POST(self):
        if not urlparse(self.path).path.endswith("/upload.php"):
            self.send_error(404)
            return
        remaining = size = int(self.headers.get("Content-Length", "0"))
        try:
            while remaining > 0:
                chunk = self.rfile.read(min(CHUNK_SIZE, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            return
        try:
            self._reply(f"size={size}\n".encode())
        except (BrokenPipeError, ConnectionResetError):
            pass  # the client stops uploading when the test phase ends


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()

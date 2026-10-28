#!/usr/bin/env python3

import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

# Speed configuration
TARGET_MBPS = 100  # Target speed cap in Mbps
BYTES_PER_SEC = (TARGET_MBPS * 1_000_000) / 8

BLOCK = os.urandom(1024 * 1024)
CHUNK_SIZE = 1 << 16


class SpeedtestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _send_common_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Encoding, Content-Type")

    def do_OPTIONS(self):
        self.send_response(200)
        self._send_common_headers()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        u = urlparse(self.path)

        if u.path.endswith("empty.php"):
            self.send_response(200)
            self._send_common_headers()
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        if u.path.endswith("garbage.php"):
            q = parse_qs(u.query)
            try:
                n = max(1, min(int(q.get("ckSize", ["4"])[0]), 1024))
            except ValueError:
                n = 4

            total_bytes = n * len(BLOCK)
            self.send_response(200)
            self._send_common_headers()
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition", "attachment; filename=random.dat")
            self.send_header("Content-Transfer-Encoding", "binary")
            self.send_header("Content-Length", str(total_bytes))
            self.end_headers()

            sent = 0
            start_time = time.perf_counter()
            try:
                while sent < total_bytes:
                    to_send = min(CHUNK_SIZE, total_bytes - sent)
                    self.wfile.write(BLOCK[:to_send])
                    sent += to_send

                    expected_time = sent / BYTES_PER_SEC
                    elapsed = time.perf_counter() - start_time
                    if expected_time > elapsed:
                        time.sleep(expected_time - elapsed)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return

        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        try:
            left = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            left = 0

        read_total = 0
        start_time = time.perf_counter()
        try:
            while left > 0:
                d = self.rfile.read(min(left, CHUNK_SIZE))
                if not d:
                    break
                read_total += len(d)
                left -= len(d)

                expected_time = read_total / BYTES_PER_SEC
                elapsed = time.perf_counter() - start_time
                if expected_time > elapsed:
                    time.sleep(expected_time - elapsed)

            self.send_response(200)
            self._send_common_headers()
            self.send_header("Content-Length", "0")
            self.end_headers()
        except (BrokenPipeError, ConnectionResetError):
            pass


class FastThreadingHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    server = FastThreadingHTTPServer(("127.0.0.1", port), SpeedtestHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()

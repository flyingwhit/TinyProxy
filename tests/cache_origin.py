#!/usr/bin/env python3
"""Tiny origin server used by the proxy cache test.

Each GET increments a counter stored in a file. If the proxy cache works,
two identical client requests should produce only one origin hit.
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
from pathlib import Path


class CacheTestHandler(BaseHTTPRequestHandler):
    counter_file: Path

    def do_GET(self):
        count = 0
        if self.counter_file.exists():
            count = int(self.counter_file.read_text().strip() or "0")

        count += 1
        self.counter_file.write_text(f"{count}\n")

        body = b"cacheable object\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--counter-file", type=Path, required=True)
    args = parser.parse_args()

    CacheTestHandler.counter_file = args.counter_file
    server = ThreadingHTTPServer(("127.0.0.1", args.port), CacheTestHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()

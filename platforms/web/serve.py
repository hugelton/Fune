#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# A static server for this folder that never lets the browser cache (a rebuilt dist/kreisel.js is seen at once).
import http.server
import sys


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8123
http.server.ThreadingHTTPServer(("", port), NoCache).serve_forever()

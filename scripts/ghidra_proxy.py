#!/usr/bin/env python3
"""Token-checking reverse proxy (token in `X-Ghidra-Token` or `Authorization: Bearer <token>`) in front of the local GhidraMCP backend (used by ghidra_tunnel.sh).
usage: ghidra_proxy.py <token> [listen-port=8090] [backend=http://127.0.0.1:8089]"""
import http.server, sys, urllib.request, urllib.error
TOKEN = sys.argv[1]
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8090
BACKEND = sys.argv[3] if len(sys.argv) > 3 else "http://127.0.0.1:8089"

class Handler(http.server.BaseHTTPRequestHandler):
    def _forward(self):
        auth = self.headers.get("Authorization", "")
        supplied = self.headers.get("X-Ghidra-Token") or (auth[7:] if auth.startswith("Bearer ") else None)
        if supplied != TOKEN:
            self.send_response(403); self.end_headers(); return
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else None
        req = urllib.request.Request(BACKEND + self.path, body, method=self.command)
        if self.headers.get("Content-Type"): req.add_header("Content-Type", self.headers["Content-Type"])
        try:
            r = urllib.request.urlopen(req, timeout=600); code, data, ctype = r.status, r.read(), r.headers.get("Content-Type")
        except urllib.error.HTTPError as e:
            code, data, ctype = e.code, e.read(), e.headers.get("Content-Type")
        except Exception as e:
            code, data, ctype = 502, str(e).encode(), "text/plain"
        self.send_response(code)
        if ctype: self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
    do_GET = do_POST = _forward
    def log_message(self, *a): pass

http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()

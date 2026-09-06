"""Proxy TLS cho bot mẫu — bot nói HTTP thường, proxy nói HTTPS với judge.

Bot mẫu của ban tổ chức (`hexudon-bot-cpp/http.hpp`) không có TLS và mặc định
port 80: đưa cho nó `https://...` thì nó nối cổng 80 và nhận 301, rồi thoát.
Proxy này nhận HTTP thường ở localhost, **sửa lại header Host** cho đúng vhost,
rồi chuyển tiếp qua HTTPS.

    python3 -m tools.tls_proxy --upstream procon.ptit.edu.vn --port 8099
    ./hexudon-bot-cpp/bot http://127.0.0.1:8099 <MATCH_ID> <TOKEN>

Chỉ dùng stdlib. Bind localhost, không mở ra ngoài.
"""
import argparse
import http.client
import ssl
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UPSTREAM = None
HOP_BY_HOP = {
    "connection", "keep-alive", "proxy-authenticate", "proxy-authorization",
    "te", "trailers", "transfer-encoding", "upgrade", "host",
}
_lock = threading.Lock()


class Proxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"   # bot dùng HTTP/1.0 + Connection: close

    def log_message(self, fmt, *args):
        sys.stderr.write("[proxy] %s\n" % (fmt % args))

    def _forward(self, method):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None

        headers = {
            k: v for k, v in self.headers.items()
            if k.lower() not in HOP_BY_HOP
        }
        headers["Host"] = UPSTREAM          # điểm mấu chốt: vhost phải đúng

        conn = http.client.HTTPSConnection(
            UPSTREAM, 443, timeout=30, context=ssl.create_default_context()
        )
        try:
            conn.request(method, self.path, body=body, headers=headers)
            resp = conn.getresponse()
            payload = resp.read()
            self.send_response(resp.status)
            self.send_header("Content-Type",
                             resp.getheader("Content-Type", "application/json"))
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as exc:
            self.send_response(502)
            self.send_header("Content-Length", "0")
            self.end_headers()
            with _lock:
                sys.stderr.write(f"[proxy] loi upstream: {type(exc).__name__}\n")
        finally:
            conn.close()

    def do_GET(self):
        self._forward("GET")

    def do_POST(self):
        self._forward("POST")


def main():
    global UPSTREAM
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", default="procon.ptit.edu.vn")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8099)
    args = ap.parse_args()
    UPSTREAM = args.upstream

    server = ThreadingHTTPServer((args.host, args.port), Proxy)
    sys.stderr.write(
        f"[proxy] http://{args.host}:{args.port}  ->  https://{args.upstream}\n"
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

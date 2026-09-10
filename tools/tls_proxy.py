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
_pool = []                   # ket noi HTTPS dung chung, dung lai giua cac request
_pool_lock = threading.Lock()


class Proxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"   # bot dùng HTTP/1.0 + Connection: close

    def log_message(self, fmt, *args):
        sys.stderr.write("[proxy] %s\n" % (fmt % args))

    def _take_upstream(self):
        """Lấy một kết nối HTTPS từ pool dùng chung, hoặc mở mới.

        Mở kết nối mới cho từng request nghĩa là bắt tay TCP+TLS lại từ đầu —
        vài vòng khứ hồi cho mỗi lần gọi. Đo trên trận thật: `response_ms_total`
        1954ms trong khi lập kế hoạch chỉ tốn 143ms, phần còn lại gần như toàn
        là mạng, và `response_ms` là tiêu chí xếp hạng thứ 4.

        Pool phải dùng CHUNG chứ không theo thread: bot nói HTTP/1.0 với
        `Connection: close` nên mỗi request là một kết nối mới tới proxy, tức
        một thread mới — giữ theo thread-local thì không bao giờ dùng lại được.
        """
        with _pool_lock:
            while _pool:
                conn = _pool.pop()
                if conn.sock is not None:
                    return conn
                conn.close()
        return http.client.HTTPSConnection(
            UPSTREAM, 443, timeout=30, context=ssl.create_default_context()
        )

    @staticmethod
    def _return_upstream(conn):
        with _pool_lock:
            if len(_pool) < 4:
                _pool.append(conn)
                return
        conn.close()

    def _forward(self, method):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None

        headers = {
            k: v for k, v in self.headers.items()
            if k.lower() not in HOP_BY_HOP
        }
        headers["Host"] = UPSTREAM          # điểm mấu chốt: vhost phải đúng
        headers["Connection"] = "keep-alive"

        # Kết nối đang giữ có thể đã bị phía kia đóng; thử lại một lần với
        # kết nối mới trước khi coi là lỗi thật.
        for attempt in (0, 1):
            conn = None
            try:
                conn = self._take_upstream()
                conn.request(method, self.path, body=body, headers=headers)
                resp = conn.getresponse()
                payload = resp.read()
                if resp.will_close:
                    conn.close()
                else:
                    self._return_upstream(conn)
                self.send_response(resp.status)
                self.send_header("Content-Type",
                                 resp.getheader("Content-Type", "application/json"))
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
                return
            except Exception as exc:
                if conn is not None:
                    try:
                        conn.close()
                    except Exception:
                        pass
                if attempt == 1:
                    self.send_response(502)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    with _lock:
                        sys.stderr.write(f"[proxy] loi upstream: {type(exc).__name__}\n")

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

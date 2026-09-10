"""Cầu WebSocket→HTTP: bot C++ giữ nguyên, transport đổi sang WS.

Vì sao: đo trên trận thật, `response_ms_total` là 1096ms trong khi lập kế hoạch
chỉ tốn 66ms — 94% là chờ phát hiện ngày mới cộng vòng khứ hồi `GET /state`.
Judge có transport WebSocket đẩy `day_state` ngay khi ngày mở, và khi trả lời
qua đó server đo **6ms** thay vì ~274ms.

Viết lại bot C++ để nói WSS thì phải kéo OpenSSL vào, mất tính "không thư viện
ngoài" của bot mẫu. Cầu này giữ WS ở phía judge và dựng lại đúng REST API cũ ở
localhost, nên `main.cpp` không phải sửa một dòng nào.

Mấu chốt là `GET /state` **treo** cho tới khi có ngày mới (long-poll) thay vì
trả về ngay ngày cũ. Bot mẫu `recv` không đặt timeout nên nó chờ thoải mái, và
độ trễ phát hiện ngày về gần bằng không.

    python3 -m tools.ws_bridge --match m-0001 --port 8099
    ./hexudon-bot-cpp/bot http://127.0.0.1:8099 m-0001 <token>

Token đọc từ $MATCH_TOKEN hoặc $API_TOKEN, không nhận qua dòng lệnh.
Chỉ stdlib. Bind localhost.
"""
import argparse
import json
import os
import queue
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .ws import WebSocket, WebSocketError

STATE = {
    "setup": None,
    "day_state": None,
    "seq": 0,          # tăng mỗi lần có day_state mới
    "served": 0,       # seq mà bot đã nhận
    "done": False,
    "result": None,
}
_cv = threading.Condition()
_acks = queue.Queue()
_ws = None
_ws_send_lock = threading.Lock()


def _classify(msg):
    """Khung WS không có trường `type` thống nhất — phân biệt theo trường có mặt."""
    if not isinstance(msg, dict):
        return "unknown"
    if "map" in msg and "spots" in msg:
        return "setup"
    if msg.get("type") == "action_result" or "valid" in msg:
        return "ack"
    if "day" in msg and "agents" in msg and "traffics" in msg:
        return "day_state"
    return "unknown"


def _reader(ws, verbose):
    while True:
        try:
            msg = ws.recv_json()
        except Exception as exc:
            if verbose:
                sys.stderr.write(f"[ws] kết nối dừng: {type(exc).__name__}\n")
            msg = None
        if msg is None:
            with _cv:
                STATE["done"] = True
                _cv.notify_all()
            _acks.put(None)
            return

        kind = _classify(msg)
        if kind == "setup":
            with _cv:
                STATE["setup"] = msg
                _cv.notify_all()
        elif kind == "ack":
            _acks.put(msg)
        elif kind == "day_state":
            with _cv:
                STATE["day_state"] = msg
                STATE["seq"] += 1
                _cv.notify_all()
            if verbose:
                sys.stderr.write(f"[ws] ngày {msg.get('day')} được đẩy tới\n")
        else:
            with _cv:
                STATE["result"] = msg
                STATE["done"] = True
                _cv.notify_all()
            if verbose:
                sys.stderr.write("[ws] nhận khung kết thúc\n")


def _wait_ack(timeout):
    try:
        return _acks.get(timeout=timeout)
    except queue.Empty:
        return None


class Bridge(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"      # bot dùng HTTP/1.0 + Connection: close
    hold_timeout = 120.0

    def log_message(self, fmt, *args):
        pass

    def _send(self, status, payload):
        body = json.dumps(payload, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except BrokenPipeError:
            pass

    def do_GET(self):
        if self.path.endswith("/setup"):
            deadline = time.time() + self.hold_timeout
            with _cv:
                while STATE["setup"] is None and not STATE["done"]:
                    if not _cv.wait(timeout=max(0.1, deadline - time.time())):
                        break
                setup = STATE["setup"]
            # 425 = chưa mở; bot mẫu coi đó là "chờ tiếp", đúng ý ở đây.
            return self._send(200, setup) if setup else self._send(425, {})

        if self.path.endswith("/state"):
            deadline = time.time() + self.hold_timeout
            with _cv:
                # Treo tới khi có day_state MỚI. Đây là chỗ cắt được toàn bộ
                # độ trễ polling: bot không hỏi lại, nó được đánh thức.
                while STATE["seq"] == STATE["served"] and not STATE["done"]:
                    if not _cv.wait(timeout=max(0.1, deadline - time.time())):
                        break
                if STATE["seq"] != STATE["served"]:
                    STATE["served"] = STATE["seq"]
                    return self._send(200, STATE["day_state"])
                done = STATE["done"]
            # Hết trận: trả khác 200 để bot chuyển sang hỏi /result rồi thoát.
            return self._send(410 if done else 425, {})

        if self.path.endswith("/result"):
            with _cv:
                if STATE["done"]:
                    return self._send(200, STATE["result"] or {})
            return self._send(425, {})

        return self._send(404, {})

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            payload = json.loads(raw.decode() or "null")
        except ValueError:
            return self._send(400, {"valid": False, "reason": "JSON hong"})

        with _ws_send_lock:
            try:
                _ws.send_json(payload)
            except Exception as exc:
                return self._send(502, {"valid": False,
                                        "reason": f"gui WS hong: {type(exc).__name__}"})

        ack = _wait_ack(self.hold_timeout)
        if ack is None:
            return self._send(504, {"valid": False, "reason": "khong nhan duoc ack"})
        return self._send(200, ack)


def main():
    global _ws
    ap = argparse.ArgumentParser()
    ap.add_argument("--match", required=True)
    ap.add_argument("--upstream", default="procon.ptit.edu.vn")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--plain", action="store_true", help="ws:// thay vì wss://")
    ap.add_argument("--hold-timeout", type=float, default=120.0)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    token = os.environ.get("MATCH_TOKEN") or os.environ.get("API_TOKEN")
    if not token:
        sys.exit("thiếu MATCH_TOKEN / API_TOKEN")

    path = f"/ws/v1/matches/{args.match}?token={token}"
    try:
        _ws = WebSocket(args.upstream, path, port=80 if args.plain else 443,
                        tls=not args.plain)
    except WebSocketError as exc:
        sys.exit(f"không mở được WebSocket: {exc}")

    Bridge.hold_timeout = args.hold_timeout
    threading.Thread(target=_reader, args=(_ws, not args.quiet), daemon=True).start()

    server = ThreadingHTTPServer((args.host, args.port), Bridge)
    if not args.quiet:
        sys.stderr.write(f"[ws] http://{args.host}:{args.port} -> "
                         f"wss://{args.upstream}/ws/v1/matches/{args.match}\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        _ws.close()


if __name__ == "__main__":
    main()

"""Client WebSocket tối giản (RFC 6455) — chỉ dùng stdlib.

Judge mở `wss://<host>/ws/v1/matches/{id}?token=…`: server đẩy khung, bot gửi
lại qua cùng kết nối. So với HTTP polling thì bỏ được cả độ trễ phát hiện ngày
mới lẫn một vòng khứ hồi `GET /state` — hai thứ chiếm ~94% `response_ms` đo
được trên trận thật.

Đủ dùng cho một client: không hỗ trợ mảnh (continuation), không nén.
"""
import base64
import json
import os
import socket
import ssl
import struct

OP_TEXT = 0x1
OP_BINARY = 0x2
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA


class WebSocketError(Exception):
    pass


class WebSocket:
    def __init__(self, host, path, port=443, tls=True, timeout=30):
        raw = socket.create_connection((host, port), timeout=timeout)
        raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock = (ssl.create_default_context().wrap_socket(raw, server_hostname=host)
                     if tls else raw)
        self._buf = b""
        self._handshake(host, path)

    def _handshake(self, host, path):
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n"
            f"\r\n"
        )
        self.sock.sendall(req.encode())
        while b"\r\n\r\n" not in self._buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WebSocketError("server đóng kết nối khi đang bắt tay")
            self._buf += chunk
        head, self._buf = self._buf.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n")[0].decode("utf-8", "replace")
        if "101" not in status:
            raise WebSocketError(f"bắt tay hỏng: {status}")

    # ------------------------------------------------------------ đọc/ghi
    def _recv_exact(self, n):
        while len(self._buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise WebSocketError("server đóng kết nối")
            self._buf += chunk
        out, self._buf = self._buf[:n], self._buf[n:]
        return out

    def recv(self):
        """Trả (opcode, payload bytes). Tự trả lời ping."""
        while True:
            b0, b1 = self._recv_exact(2)
            opcode = b0 & 0x0F
            masked = b1 & 0x80
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._recv_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._recv_exact(8))[0]
            mask = self._recv_exact(4) if masked else None
            data = self._recv_exact(length) if length else b""
            if mask:
                data = bytes(c ^ mask[i % 4] for i, c in enumerate(data))
            if opcode == OP_PING:
                self._send_frame(OP_PONG, data)
                continue
            if opcode == OP_PONG:
                continue
            return opcode, data

    def recv_json(self):
        opcode, data = self.recv()
        if opcode == OP_CLOSE:
            return None
        if opcode not in (OP_TEXT, OP_BINARY):
            return {}
        return json.loads(data.decode("utf-8"))

    def _send_frame(self, opcode, payload):
        mask = os.urandom(4)
        masked = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
        n = len(payload)
        if n < 126:
            header = struct.pack("!BB", 0x80 | opcode, 0x80 | n)
        elif n < (1 << 16):
            header = struct.pack("!BBH", 0x80 | opcode, 0x80 | 126, n)
        else:
            header = struct.pack("!BBQ", 0x80 | opcode, 0x80 | 127, n)
        self.sock.sendall(header + mask + masked)

    def send_json(self, obj):
        self._send_frame(OP_TEXT, json.dumps(obj, separators=(",", ":")).encode())

    def send_text(self, text):
        self._send_frame(OP_TEXT, text.encode())

    def close(self):
        try:
            self._send_frame(OP_CLOSE, b"")
        except Exception:
            pass
        try:
            self.sock.close()
        except Exception:
            pass

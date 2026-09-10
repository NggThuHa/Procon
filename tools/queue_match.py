"""Xếp hàng đấu với đội THẬT rồi in match id ra stdout.

Khác `live_match`: không tự tạo trận với bot AI của ban tổ chức, mà vào hàng
chờ ghép cặp với đội khác. Arena offline không có đối thủ (`others: []`) nên
không mô phỏng được traffic do xe đội bạn tạo ra — muốn biết bot chạy thế nào
khi có đối kháng thì phải qua đây.

    export PROCON_USERNAME=... PROCON_PASSWORD=...
    python3 -m tools.queue_match

Queue ghép xong là trận CHẠY NGAY, nối bot chậm là ăn `E_STALE_DAY` và mất
trắng. Vì vậy script chỉ in match id rồi thoát, để caller bật bot lập tức —
xem `play.sh --queue`.

Không in token.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

BASE = os.environ.get("PROCON_URL", "http://localhost:8000")


def _call(method, path, payload=None, token=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method)
    req.add_header("Accept", "application/json")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=30) as r:
        body = r.read().decode()
    return json.loads(body) if body.strip() else {}


def login():
    user = os.environ.get("PROCON_USERNAME")
    pwd = os.environ.get("PROCON_PASSWORD")
    if not user or not pwd:
        sys.exit("thiếu PROCON_USERNAME / PROCON_PASSWORD")
    return _call("POST", "/auth/login", {"username": user, "password": pwd})["token"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=int, default=300,
                    help="giây chờ ghép cặp trước khi bỏ cuộc")
    ap.add_argument("--poll", type=float, default=2.0)
    args = ap.parse_args()

    token = login()
    deadline = time.time() + args.timeout
    last = None
    while time.time() < deadline:
        try:
            info = _call("POST", "/practice/queue", {}, token)
        except urllib.error.HTTPError as exc:
            print(f"queue -> HTTP {exc.code}", file=sys.stderr)
            return 1

        status = info.get("status")
        if status != last:
            print(f"hàng chờ: {status}", file=sys.stderr)
            last = status
        if info.get("match_id") and status == "matched":
            print(info["match_id"])          # stdout: chỉ match id, cho caller
            return 0
        time.sleep(args.poll)

    print("hết giờ chờ ghép cặp", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())

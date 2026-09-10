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


def _match_state(token, match_id):
    """Trạng thái thật của trận, hoặc None nếu chưa xuất hiện trong danh sách.

    Trận vừa ghép có thể chưa kịp vào `/team/matches`; None nghĩa là "chưa biết,
    coi như còn mới" chứ không phải "đã hỏng".
    """
    try:
        for m in _call("GET", "/team/matches", token=token)["matches"]:
            if m.get("id") == match_id:
                return m.get("status")
    except Exception:
        return None
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=int, default=300,
                    help="giây chờ ghép cặp trước khi bỏ cuộc")
    ap.add_argument("--poll", type=float, default=2.0)
    args = ap.parse_args()

    token = login()
    deadline = time.time() + args.timeout
    last = None
    stale_warned = set()
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

        mid = info.get("match_id")
        if mid and status == "matched":
            # `matched` KHÔNG có nghĩa là trận còn vào được: endpoint giữ lại
            # entry cũ sau khi trận đã chạy xong, và nối vào trận `done` thì
            # WebSocket trả 401 còn HTTP trả E_STALE_DAY. Phải soi trạng thái
            # thật trước khi bảo caller bật bot.
            if _match_state(token, mid) in ("done", "cancelled"):
                if mid not in stale_warned:
                    print(f"bỏ qua {mid}: đã {_match_state(token, mid)} — "
                          f"hàng chờ đang trả entry cũ, đợi trận mới",
                          file=sys.stderr)
                    stale_warned.add(mid)
                time.sleep(args.poll)
                continue
            print(mid)                       # stdout: chỉ match id, cho caller
            return 0
        time.sleep(args.poll)

    print("hết giờ chờ ghép cặp", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())

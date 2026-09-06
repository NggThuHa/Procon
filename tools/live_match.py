"""Chạy một trận luyện tập thật rồi in bảng điểm.

Tự tạo trận (POST /practice), cho bot nối vào, đợi xong, đọc `standings`.
Dùng để KIỂM CHỨNG bot trên luật thật — không phải để so hai chiến thuật
(so sánh thì dùng arena offline, xem docs/plan/KIENNT.md).

    export PROCON_USERNAME=<tài khoản đội>
    export PROCON_PASSWORD=<mật khẩu>
    python3 -m tools.live_match --bot hexudon-bot-cpp/bot

Không in token ra stdout/stderr.
"""
import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

BASE = os.environ.get("PROCON_URL", "https://procon.ptit.edu.vn")


def _post(path, payload, token=None):
    req = urllib.request.Request(
        BASE + path,
        data=json.dumps(payload).encode(),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def _get(path, token):
    req = urllib.request.Request(BASE + path)
    req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def login():
    user = os.environ.get("PROCON_USERNAME")
    pwd = os.environ.get("PROCON_PASSWORD")
    if not user or not pwd:
        sys.exit("thiếu PROCON_USERNAME / PROCON_PASSWORD")
    return _post("/auth/login", {"username": user, "password": pwd})["token"]


def create_practice(token, params):
    body = {k: v for k, v in params.items() if v is not None}
    match = _post("/practice", body, token)
    print(f"đã tạo trận {match['match_id']} "
          f"({match.get('difficulty')}, {match.get('opponents')} bot)")
    return match


def wait_for_result(token, match_id, timeout_s):
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        for m in _get("/team/matches", token)["matches"]:
            if m["id"] != match_id:
                continue
            if m["status"] != last:
                print(f"  trạng thái: {m['status']}")
                last = m["status"]
            if m["status"] in ("done", "cancelled"):
                return m
        time.sleep(5)
    return None


def render(match):
    rows = match.get("standings") or []
    if not rows:
        return "Không có standings."
    head = ("| # | Đội | Loại udon | Lũy kế loại | Tổng phần | Thời gian (ms) |\n"
            "|---:|---|---:|---:|---:|---:|\n")
    body = "".join(
        f"| {r['rank']} | {r['team_id']} | {r['udon_types']} | "
        f"{r['daily_types_sum']} | {r['udon_total']} | {r['response_ms_total']} |\n"
        for r in sorted(rows, key=lambda x: x["rank"])
    )
    return head + body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", default="hexudon-bot-cpp/bot")
    ap.add_argument("--bot-url", default=None,
                    help="URL bot dùng để nối. Bot mẫu không có TLS nên phải "
                         "trỏ qua tools.tls_proxy, ví dụ http://127.0.0.1:8099")
    ap.add_argument("--days", type=int)
    ap.add_argument("--difficulty")
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--out", help="ghi bảng điểm ra file markdown")
    args = ap.parse_args()

    token = login()
    match = create_practice(token, {"days": args.days, "difficulty": args.difficulty})
    mid, mtoken = match["match_id"], match["your_token"]

    bot_url = args.bot_url or BASE
    if bot_url.startswith("https://"):
        print("CẢNH BÁO: bot mẫu không hỗ trợ TLS; dùng --bot-url qua tls_proxy",
              file=sys.stderr)
    print(f"chạy bot: {args.bot} -> {bot_url}")
    bot = subprocess.Popen(
        [args.bot, bot_url, mid, mtoken],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
    )
    try:
        final = wait_for_result(token, mid, args.timeout)
    finally:
        if bot.poll() is None:
            bot.terminate()
            try:
                bot.wait(timeout=10)
            except subprocess.TimeoutExpired:
                bot.kill()

    if final is None:
        print("hết giờ chờ, trận chưa xong", file=sys.stderr)
        return 1

    table = render(final)
    print(table)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(f"# Trận {mid} ({final.get('kind')}, {final.get('days')} ngày)\n\n")
            fh.write(table)
    # Đội mình phải thu được ít nhất một loại udon, nếu không là bot hỏng.
    mine = [r for r in (final.get("standings") or [])
            if r["team_id"] == final.get("team_id")]
    if mine and mine[0]["udon_types"] == 0:
        print("CẢNH BÁO: đội mình thu được 0 loại udon", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

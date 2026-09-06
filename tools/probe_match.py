"""Chạy một trận có kiểm soát để đọc ngược luật cộng điểm.

Bot mẫu chơi bằng heuristic nên nhìn kết quả không suy ra được luật. Probe này
làm ngược lại: mỗi ngày cho ĐÚNG MỘT xe đi tới ĐÚNG MỘT spot, ghi lại toàn bộ
/state trước và sau, rồi đối chiếu với standings cuối trận.

Nhờ vậy trả lời được:
  - Xe thu udon khi nào: đi vào ô spot, hay phải dừng lại ở đó?
  - Mỗi lần thu được bao nhiêu phần?
  - Stock có nạp lại đầu ngày không?
  - daily_types_sum cộng theo công thức nào?

    export PROCON_USERNAME=<tài khoản đội>
    export PROCON_PASSWORD=<mật khẩu>
    python3 -m tools.probe_match --days 4 --out docs/observed

Không in token.
"""
import argparse
import heapq
import json
import os
import pathlib
import sys
import time
import urllib.request

from .rules import TERRAIN_POND, TRAFFIC_CLEAR, move_cost, neighbor

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


def cheapest_path(src, dst, cells, w, h, traffic):
    """Dijkstra theo chi phí bước. Trả về danh sách mã hướng."""
    dist = {src: 0}
    prev = {}
    pq = [(0, src)]
    while pq:
        d, cur = heapq.heappop(pq)
        if cur == dst:
            break
        if d > dist.get(cur, 1 << 30):
            continue
        cost = move_cost(cells[cur], traffic.get(cur, TRAFFIC_CLEAR))
        if cost is None:
            continue
        for direction in range(6):
            nxt = neighbor(cur, direction, w, h)
            if nxt is None or cells[nxt] == TERRAIN_POND:
                continue
            nd = d + cost[0]
            if nd < dist.get(nxt, 1 << 30):
                dist[nxt] = nd
                prev[nxt] = (cur, direction)
                heapq.heappush(pq, (nd, nxt))
    if dst not in prev and dst != src:
        return []
    out = []
    node = dst
    while node != src:
        node, direction = prev[node]
        out.append(direction)
    return out[::-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", type=int, default=4)
    ap.add_argument("--difficulty", default="hard")
    ap.add_argument("--out", default="docs/observed")
    args = ap.parse_args()

    user, pwd = os.environ.get("PROCON_USERNAME"), os.environ.get("PROCON_PASSWORD")
    if not user or not pwd:
        sys.exit("thiếu PROCON_USERNAME / PROCON_PASSWORD")
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    web = _call("POST", "/auth/login", {"username": user, "password": pwd})["token"]
    match = _call("POST", "/practice",
                  {"days": args.days, "difficulty": args.difficulty}, web)
    mid, mtok = match["match_id"], match["your_token"]
    print(f"trận {mid}")

    setup = _call("GET", f"/api/v1/matches/{mid}/setup", token=mtok)
    (out / "probe-setup.json").write_text(
        json.dumps(setup, indent=2, ensure_ascii=False) + "\n")
    w, h = setup["map"]["width"], setup["map"]["height"]
    cells = [c for row in setup["map"]["cells"] for c in row]
    n_agents = len(setup["agents"])
    spots = setup["spots"]
    print(f"  map {w}x{h}, {n_agents} xe, {len(spots)} spot, "
          f"brand {sorted({s['brand'] for s in spots})}")

    # Tất cả là xe tuần tra: bỏ biến số xe tiếp tế ra khỏi thí nghiệm.
    _call("POST", f"/api/v1/matches/{mid}/assignment", [0] * n_agents, mtok)

    log = []
    seen_day = -1
    deadline = time.time() + 60 * 15
    while time.time() < deadline:
        try:
            state = _call("GET", f"/api/v1/matches/{mid}/state", token=mtok)
        except Exception:
            break
        day = state.get("day")
        if day is None or day == seen_day:
            time.sleep(0.4)
            continue
        seen_day = day
        (out / f"probe-state-day{day}.json").write_text(
            json.dumps(state, indent=2, ensure_ascii=False) + "\n")

        budget = state.get("daySteps")
        if isinstance(budget, list):
            budget = budget[day]
        budget = budget or setup["daySteps"][day]
        traffic = {t["pos"]: t["status"] for t in state.get("traffics", [])}
        agents = state["agents"]
        pos0 = agents[0]["pos"] if isinstance(agents[0], dict) else agents[0]

        # THÍ NGHIỆM: chỉ xe 0 di chuyển, tới spot rẻ nhất còn stock.
        cur_spots = state.get("spots") or spots
        best, best_path = None, None
        for sp in cur_spots:
            if sp.get("stocks", 1) <= 0:
                continue
            path = cheapest_path(pos0, sp["pos"], cells, w, h, traffic)
            if not path and pos0 != sp["pos"]:
                continue
            cost = 0
            node = pos0
            for d in path:
                cost += move_cost(cells[node], traffic.get(node, TRAFFIC_CLEAR))[0]
                node = neighbor(node, d, w, h)
            if cost <= budget and (best is None or cost < best):
                best, best_path = cost, path

        moves = best_path or []
        rest = budget - (best or 0)
        act0 = moves + ([-rest] if rest > 0 else [])
        actions = [act0] + [[-budget] for _ in range(n_agents - 1)]
        resp = _call("POST", f"/api/v1/matches/{mid}/actions", actions, mtok)
        entry = {"day": day, "agent0_from": pos0, "action": actions[0],
                 "steps_used": best, "response": resp,
                 "spots_before": cur_spots}
        log.append(entry)
        print(f"  ngày {day}: xe0 từ {pos0} đi {len(moves)} bước -> {resp}")

    (out / "probe-log.json").write_text(
        json.dumps(log, indent=2, ensure_ascii=False) + "\n")

    final = None
    for _ in range(60):
        for m in _call("GET", "/team/matches", token=web)["matches"]:
            if m["id"] == mid and m["status"] in ("done", "cancelled"):
                final = m
        if final:
            break
        time.sleep(10)
    if final:
        clean = {k: v for k, v in final.items() if "token" not in k.lower()}
        (out / "probe-result.json").write_text(
            json.dumps(clean, indent=2, ensure_ascii=False) + "\n")
        print("\nstandings:")
        for r in sorted(final.get("standings") or [], key=lambda x: x["rank"]):
            print(f"  #{r['rank']} {r['team_id']:<8} types={r['udon_types']} "
                  f"daily={r['daily_types_sum']} tổng={r['udon_total']}")
    else:
        print("chưa có kết quả cuối", file=sys.stderr)


if __name__ == "__main__":
    main()

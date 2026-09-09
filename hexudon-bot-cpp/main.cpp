// Bot mẫu HEXUDON (format BTC gốc) — C++17, transport HTTP polling. KHÔNG thư viện ngoài (raw socket).
// Chạy:  ./bot <URL> <MATCH_ID> <TOKEN>      (vd ./bot http://judge:8099 m-0001 <token>)
// Build: g++ -std=c++17 -O2 -o bot main.cpp   (Windows nối thêm -lws2_32)
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "minijson.hpp"
#include "http.hpp"
using namespace std;

// Hướng BTC gốc: 0 trên-trái,1 trên-phải,2 phải,3 dưới-phải,4 dưới-trái,5 trái.
// Hình học EVEN-R (hàng CHẴN lệch phải — khớp BTC Q1). DE=hàng chẵn, DO=hàng lẻ.
static const int DE[6][2] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,0}};
static const int DO[6][2] = {{-1,-1},{0,-1},{1,0},{0,1},{-1,1},{-1,0}};

struct Spot {
    int brand = 0;
    int pos = 0;
    int stocks = 1;
};

struct Route {
    bool ok = false;
    vector<int> dirs;
    vector<int> cells;
    int steps = 0;
    int fuel = 0;
};

static int W, H, g_nAgents;
static vector<int> g_cells;    // phẳng row*W+col (0 đất,1 đường,2 núi,3 ao)
static vector<Spot> g_spots;   // các điểm udon: brand/pos/stocks
static vector<int> g_daySteps; // số bước mỗi ngày
static set<int> g_collectedBrands;
static set<int> g_pendingBrands;
static int g_pendingDay = -1;

static int neighbor(int pos, int d) {
    if (W <= 0) return -1;
    int r = pos / W, c = pos % W;
    const int (*dl)[2] = (r % 2) ? DO : DE;
    int nc = c + dl[d][0], nr = r + dl[d][1];
    if (nc < 0 || nc >= W || nr < 0 || nr >= H) return -1;
    return nr * W + nc;
}
static pair<int,int> moveCost(int pos, int status) { // Bảng 1 cố định; {-1,-1}=ao
    if (pos < 0 || pos >= (int)g_cells.size()) return {-1, -1};
    switch (g_cells[pos]) {
        case 0: return {2, 1};
        case 2: return {3, 2};
        case 1: return status == 1 ? make_pair(2,2) : status == 2 ? make_pair(4,2) : make_pair(1,2);
    }
    return {-1, -1};
}
struct Node {
    int steps;
    int fuel;
    int pos;
};
struct NodeCmp {
    bool operator()(const Node& a, const Node& b) const {
        if (a.steps != b.steps) return a.steps > b.steps;
        return a.fuel > b.fuel;
    }
};

static Route dijkstraRoute(int src, int dst, const map<int,int>& status, int stepLimit, int fuelLimit) {
    Route out;
    int n = (int)g_cells.size();
    if (src < 0 || dst < 0 || src >= n || dst >= n || stepLimit < 0 || fuelLimit < 0) return out;
    if (g_cells[src] == 3 || g_cells[dst] == 3) return out;
    if (src == dst) { out.ok = true; return out; }

    const int INF = numeric_limits<int>::max() / 4;
    int maxFuel = min(fuelLimit, max(0, stepLimit) * 2);
    int stride = maxFuel + 1;
    int total = n * stride;
    vector<int> dist(total, INF), prevState(total, -1), prevDir(total, -1);
    priority_queue<Node, vector<Node>, NodeCmp> pq;

    auto id = [stride](int pos, int fuel) { return pos * stride + fuel; };
    int start = id(src, 0);
    dist[start] = 0;
    pq.push({0, 0, src});

    int goal = -1;
    while (!pq.empty()) {
        Node cur = pq.top(); pq.pop();
        int curId = id(cur.pos, cur.fuel);
        if (cur.steps != dist[curId]) continue;
        if (cur.pos == dst) { goal = curId; break; }

        auto c = moveCost(cur.pos, status.count(cur.pos) ? status.at(cur.pos) : 0);
        if (c.first < 0) continue;
        for (int d = 0; d < 6; d++) {
            int nb = neighbor(cur.pos, d);
            if (nb < 0 || g_cells[nb] == 3) continue;
            int ns = cur.steps + c.first;
            int nf = cur.fuel + c.second;
            if (ns > stepLimit || nf > maxFuel) continue;
            int nextId = id(nb, nf);
            if (ns < dist[nextId]) {
                dist[nextId] = ns;
                prevState[nextId] = curId;
                prevDir[nextId] = d;
                pq.push({ns, nf, nb});
            }
        }
    }

    if (goal < 0) return out;
    vector<int> dirs;
    for (int at = goal; at != start; at = prevState[at]) dirs.push_back(prevDir[at]);
    reverse(dirs.begin(), dirs.end());

    int cur = src;
    for (int d : dirs) {
        cur = neighbor(cur, d);
        out.cells.push_back(cur);
    }
    out.ok = true;
    out.dirs = dirs;
    out.steps = dist[goal];
    out.fuel = goal % stride;
    return out;
}

static vector<vector<int>> waitPlan(size_t nAgents, int steps) {
    vector<vector<int>> plan(nAgents);
    for (auto& cmds : plan) if (steps > 0) cmds.push_back(-steps);
    return plan;
}

static string renderPlan(const vector<vector<int>>& plan) {
    ostringstream out;
    out << "[";
    for (size_t i = 0; i < plan.size(); i++) {
        if (i) out << ",";
        out << "[";
        for (size_t j = 0; j < plan[i].size(); j++) {
            if (j) out << ",";
            out << plan[i][j];
        }
        out << "]";
    }
    out << "]";
    return out.str();
}

static bool validatePlan(const vector<vector<int>>& plan, const mj::Value& state,
                         int budget, const map<int,int>& status, string* err) {
    const mj::Value& ags = state["agents"];
    if (plan.size() != ags.size()) {
        if (err) *err = "sai so luong agent";
        return false;
    }
    for (size_t i = 0; i < plan.size(); i++) {
        int kind = ags[i]["kind"].asInt();
        int pos = ags[i]["pos"].asInt();
        int fuel = ags[i]["fuel"].isNull() ? (1 << 28) : ags[i]["fuel"].asInt();
        int used = 0;
        for (int cmd : plan[i]) {
            if (cmd > 5) {
                if (err) *err = "ma huong > 5";
                return false;
            }
            if (cmd < 0) {
                used += -cmd;
                if (used > budget) {
                    if (err) *err = "vuot daySteps khi dung yen";
                    return false;
                }
                continue;
            }

            int dst = neighbor(pos, cmd);
            if (dst < 0) {
                if (err) *err = "di ra ngoai map";
                return false;
            }
            if (g_cells[dst] == 3) {
                if (err) *err = "di vao ao";
                return false;
            }
            auto c = moveCost(pos, status.count(pos) ? status.at(pos) : 0);
            if (c.first < 0) {
                if (err) *err = "o nguon khong di duoc";
                return false;
            }
            used += c.first;
            if (used > budget) {
                if (err) *err = "vuot daySteps";
                return false;
            }
            if (kind == 0) {
                if (fuel < c.second) {
                    if (err) *err = "khong du nhien lieu";
                    return false;
                }
                fuel -= c.second;
            }
            pos = dst;
        }
    }
    return true;
}

static vector<vector<int>> buildBrandGreedyPlan(const mj::Value& state, const map<int,int>& status, int steps) {
    const mj::Value& ags = state["agents"];
    vector<vector<int>> plan(ags.size());
    map<int,int> spotUseToday;
    set<int> lifetimeBrands = g_collectedBrands;
    set<int> dailyBrands;

    g_pendingBrands.clear();
    g_pendingDay = state["day"].asInt();

    for (size_t i = 0; i < ags.size(); i++) {
        int kind = ags[i]["kind"].asInt();
        int cur = ags[i]["pos"].asInt();
        int fuel = ags[i]["fuel"].isNull() ? (1 << 28) : ags[i]["fuel"].asInt();
        int used = 0;
        set<int> visitedSpotsByThisAgent;

        if (kind != 0) {
            if (steps > 0) plan[i].push_back(-steps);
            continue;
        }

        while (used < steps) {
            int bestSpot = -1;
            int bestScore = numeric_limits<int>::min();
            Route bestRoute;

            for (size_t si = 0; si < g_spots.size(); si++) {
                const Spot& sp = g_spots[si];
                if (visitedSpotsByThisAgent.count(sp.pos)) continue;
                int stockCap = max(1, sp.stocks);
                if (spotUseToday[sp.pos] >= stockCap) continue;

                Route r = dijkstraRoute(cur, sp.pos, status, steps - used, fuel);
                if (!r.ok) continue;

                bool newLifetimeBrand = lifetimeBrands.count(sp.brand) == 0;
                bool newDailyBrand = dailyBrands.count(sp.brand) == 0;
                int score = 0;
                if (newLifetimeBrand) score += 1000000;
                if (newDailyBrand) score += 100000;
                score += min(stockCap, 8) * 1000;
                score -= r.steps * 25;
                score -= r.fuel * 5;
                score -= spotUseToday[sp.pos] * 500;

                if (score > bestScore ||
                    (score == bestScore && (bestSpot < 0 ||
                     r.steps < bestRoute.steps ||
                     (r.steps == bestRoute.steps && r.fuel < bestRoute.fuel)))) {
                    bestScore = score;
                    bestSpot = (int)si;
                    bestRoute = r;
                }
            }

            if (bestSpot < 0) break;

            const Spot& sp = g_spots[bestSpot];
            if (bestRoute.steps == 0 && used < steps) {
                plan[i].push_back(-1); // dam bao xe dung tren spot toi thieu 1 step truoc khi roi di
                used += 1;
            }
            for (int d : bestRoute.dirs) plan[i].push_back(d);
            used += bestRoute.steps;
            fuel -= bestRoute.fuel;
            cur = sp.pos;

            visitedSpotsByThisAgent.insert(sp.pos);
            spotUseToday[sp.pos]++;
            lifetimeBrands.insert(sp.brand);
            dailyBrands.insert(sp.brand);
            g_pendingBrands.insert(sp.brand);
        }

        if (used < steps) plan[i].push_back(-(steps - used));
        if (plan[i].empty() && steps > 0) plan[i].push_back(-steps);
    }
    return plan;
}

static void commitPendingBrands(int day) {
    if (g_pendingDay != day) return;
    for (int brand : g_pendingBrands) g_collectedBrands.insert(brand);
    g_pendingBrands.clear();
    g_pendingDay = -1;
}

static bool actionAccepted(const http::Response& r, string* reason) {
    if (r.status != 200) return false;
    try {
        auto v = mj::parse(r.body);
        const mj::Value& valid = (*v)["valid"];
        if (valid.type == mj::Value::BOOL && !valid.boolean) {
            const mj::Value& why = (*v)["reason"];
            if (reason && why.type == mj::Value::STR) *reason = why.str;
            return false;
        }
    } catch (...) {
        // Mot so server mau cu co the tra 200 voi body rong.
    }
    return true;
}

// parseSetup: đọc setup, đặt globals, TRẢ mảng loại agent (phẳng "[0,..,1]") để POST /assignment.
static string parseSetup(const mj::Value& m) {
    g_collectedBrands.clear();
    g_pendingBrands.clear();
    g_pendingDay = -1;

    const mj::Value& mp = m["map"];
    W = mp["width"].asInt(); H = mp["height"].asInt();
    g_cells.assign((size_t)W * H, 0);
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++)
            g_cells[r * W + c] = mp["cells"][r][c].asInt();
    g_spots.clear();
    for (size_t i = 0; i < m["spots"].size(); i++) {
        Spot s;
        s.brand = m["spots"][i]["brand"].asInt();
        s.pos = m["spots"][i]["pos"].asInt();
        s.stocks = max(1, m["spots"][i]["stocks"].asInt());
        g_spots.push_back(s);
    }
    g_daySteps.clear();
    for (size_t i = 0; i < m["daySteps"].size(); i++) g_daySteps.push_back(m["daySteps"][i].asInt());
    g_nAgents = (int)m["agents"].size();
    // Gán loại: xe cuối tiếp tế (1), còn lại tuần tra (0).
    ostringstream out; out << "[";
    for (int i = 0; i < g_nAgents; i++) { if (i) out << ","; out << (i == g_nAgents - 1 && g_nAgents > 1 ? 1 : 0); }
    out << "]";
    return out.str();
}

// planActions: Dijkstra theo chi phi that, uu tien mo brand moi va validate truoc khi gui.
static string planActions(const mj::Value& m) {
    int day = m["day"].asInt();
    int steps = (day >= 0 && day < (int)g_daySteps.size()) ? g_daySteps[day] : 30;
    map<int,int> status;
    for (size_t i = 0; i < m["traffics"].size(); i++)
        status[m["traffics"][i]["pos"].asInt()] = m["traffics"][i]["status"].asInt();
    const mj::Value& ags = m["agents"];

    vector<vector<int>> plan = buildBrandGreedyPlan(m, status, steps);
    string err;
    if (!validatePlan(plan, m, steps, status, &err)) {
        fprintf(stderr, "validator chan plan ngay %d: %s; dung yen fallback\n", day, err.c_str());
        g_pendingBrands.clear();
        g_pendingDay = day;
        plan = waitPlan(ags.size(), steps);
    }
    return renderPlan(plan);
}

static void sleepMs(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "dung: %s <URL> <MATCH_ID> <TOKEN>\n", argv[0]); return 2; }
    string base = string(argv[1]) + "/api/v1/matches/" + argv[2];
    string token = argv[3];
    const int POLL_MS = 250; // >= 200ms tránh 429

    // 1) SETUP — chờ tới khi lấy được (425 = bản đồ chưa mở / trận chưa tới giờ).
    string assignBody;
    for (;;) {
        auto r = http::request(base, "GET", "/setup", token, "");
        if (r.status == 200) { auto v = mj::parse(r.body); assignBody = parseSetup(*v); break; }
        if (r.status == 0 || r.status == 425 || r.status == 429) { sleepMs(POLL_MS); continue; }
        fprintf(stderr, "GET /setup -> HTTP %d (token sai / match khong hop le?)\n", r.status); return 1;
    }
    fprintf(stderr, "nhan setup: %dx%d o, %zu diem, %d xe\n", W, H, g_spots.size(), g_nAgents);

    // 2) ASSIGNMENT — gửi loại agent (cố định cả trận).
    for (;;) {
        auto r = http::request(base, "POST", "/assignment", token, assignBody);
        if (r.status == 200) break;
        if (r.status == 0 || r.status == 429) { sleepMs(POLL_MS); continue; }
        fprintf(stderr, "POST /assignment -> HTTP %d\n", r.status); return 1;
    }

    // 3) VÒNG NGÀY — poll /state; mỗi ngày mới gửi kế hoạch. Hết trận -> /result trả 200.
    int lastDay = -1;
    for (;;) {
        auto r = http::request(base, "GET", "/state", token, "");
        if (r.status == 200) {
            auto v = mj::parse(r.body);
            int day = (*v)["day"].asInt();
            if (day != lastDay) {
                string acts = planActions(*v);
                auto pr = http::request(base, "POST", "/actions", token, acts);
                string reason;
                if (actionAccepted(pr, &reason)) {
                    commitPendingBrands(day);
                    lastDay = day;
                    fprintf(stderr, "ngay %d: da gui ke hoach, da biet %zu brand\n", day, g_collectedBrands.size());
                } else if (pr.status == 200) {
                    fprintf(stderr, "ngay %d: server tu choi action%s%s\n",
                            day, reason.empty() ? "" : ": ", reason.c_str());
                }
                // 429/lỗi: giữ lastDay để vòng sau gửi lại.
            }
        } else if (r.status != 429 && r.status != 0) {
            auto rr = http::request(base, "GET", "/result", token, "");
            if (rr.status == 200) { fprintf(stderr, "ket thuc tran\n"); break; }
        }
        sleepMs(POLL_MS);
    }
    return 0;
}

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

struct AgentDraft {
    int start = 0;
    int end = 0;
    int usedSteps = 0;
    int startFuel = 0;
    int fuelLeft = 0;
};

struct PendingTrace {
    int day = -1;
    set<int> brands;
    map<int,int> spotVisits;
};

static int W, H, g_nAgents;
static vector<int> g_cells;    // phẳng row*W+col (0 đất,1 đường,2 núi,3 ao)
static vector<Spot> g_spots;   // các điểm udon: brand/pos/stocks
static vector<int> g_daySteps; // số bước mỗi ngày
static int g_fuelLimit = 0;
static int g_patrolCount = 0;
static int g_supplyCount = 0;
static int g_totalBrands = 0;
static set<int> g_collectedBrands;
static PendingTrace g_pendingTrace;

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

static int iabs(int x) { return x < 0 ? -x : x; }

static int roughDistance(int a, int b) {
    if (W <= 0) return 0;
    int ar = a / W, ac = a % W;
    int br = b / W, bc = b % W;
    return iabs(ar - br) + iabs(ac - bc);
}

static const Spot* spotAt(int pos) {
    for (const auto& sp : g_spots) if (sp.pos == pos) return &sp;
    return nullptr;
}

static bool recordVisit(int pos, set<int>& seenByAgent, map<int,int>& spotUseToday,
                        set<int>& lifetimeBrands, set<int>& dailyBrands,
                        set<int>* collectedNow) {
    const Spot* sp = spotAt(pos);
    if (!sp || seenByAgent.count(pos)) return false;
    int stockCap = max(1, sp->stocks);
    if (spotUseToday[pos] >= stockCap) return false;

    seenByAgent.insert(pos);
    spotUseToday[pos]++;
    lifetimeBrands.insert(sp->brand);
    dailyBrands.insert(sp->brand);
    if (collectedNow) collectedNow->insert(sp->brand);
    return true;
}

static PendingTrace tracePlan(const vector<vector<int>>& plan, const mj::Value& state,
                              const map<int,int>& status) {
    PendingTrace trace;
    trace.day = state["day"].asInt();
    const mj::Value& ags = state["agents"];
    set<int> lifetime = g_collectedBrands;
    set<int> daily;

    for (size_t i = 0; i < plan.size() && i < ags.size(); i++) {
        if (ags[i]["kind"].asInt() != 0) continue;
        int pos = ags[i]["pos"].asInt();
        set<int> seenByAgent;
        recordVisit(pos, seenByAgent, trace.spotVisits, lifetime, daily, &trace.brands);

        for (int cmd : plan[i]) {
            if (cmd < 0) {
                recordVisit(pos, seenByAgent, trace.spotVisits, lifetime, daily, &trace.brands);
                continue;
            }
            int dst = neighbor(pos, cmd);
            if (dst < 0) break;
            auto c = moveCost(pos, status.count(pos) ? status.at(pos) : 0);
            if (c.first < 0) break;
            pos = dst;
            recordVisit(pos, seenByAgent, trace.spotVisits, lifetime, daily, &trace.brands);
        }
    }
    return trace;
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

static void appendRouteCommands(vector<int>& cmds, const Route& route) {
    for (int d : route.dirs) cmds.push_back(d);
}

static int maxDaySteps() {
    int mx = 0;
    for (int steps : g_daySteps) mx = max(mx, steps);
    return mx;
}

static int chooseSupplyCount() {
    if (g_nAgents <= 1) return 0;

    int longestDay = maxDaySteps();
    bool smallMap = W * H <= 100 || max(W, H) <= 10;
    bool fullDayFuel = longestDay > 0 && g_fuelLimit >= longestDay * 2;
    bool tightFuel = longestDay > 0 && g_fuelLimit > 0 &&
                     g_fuelLimit <= longestDay + max(3, longestDay / 5);

    int supply = 1;
    if (g_nAgents >= 6) supply = 2;
    if (g_nAgents >= 8) supply = 3;
    if (g_nAgents >= 8 && smallMap && fullDayFuel && !tightFuel) supply = 2;

    supply = min(supply, g_nAgents - 1);
    return max(1, supply);
}

static bool shouldReserveRefuelStep(int fuel, int steps) {
    if (g_supplyCount <= 0 || g_fuelLimit <= 0) return false;
    if (fuel <= max(2, g_fuelLimit / 3)) return true;
    return steps > 0 && fuel <= max(3, steps / 2);
}

static vector<vector<int>> buildSplitRefuelPlan(const mj::Value& state, const map<int,int>& status, int steps) {
    const mj::Value& ags = state["agents"];
    vector<vector<int>> plan(ags.size());
    vector<AgentDraft> draft(ags.size());
    vector<int> patrols, supplies;
    map<int,int> spotUseToday;
    set<int> lifetimeBrands = g_collectedBrands;
    set<int> dailyBrands;

    for (size_t i = 0; i < ags.size(); i++) {
        int kind = ags[i]["kind"].asInt();
        int cur = ags[i]["pos"].asInt();
        int fuel = ags[i]["fuel"].isNull() ? (1 << 28) : ags[i]["fuel"].asInt();
        draft[i].start = draft[i].end = cur;
        draft[i].startFuel = draft[i].fuelLeft = fuel;
        if (kind == 0) patrols.push_back((int)i);
        else supplies.push_back((int)i);
    }

    vector<int> patrolAnchors;

    for (int idx : patrols) {
        int cur = draft[idx].start;
        int fuel = draft[idx].startFuel;
        int used = 0;
        int anchor = -1;
        int patrolReserve = shouldReserveRefuelStep(fuel, steps) ? 1 : 0;
        set<int> visitedSpotsByThisAgent;

        if (spotAt(cur) && used < steps) {
            plan[idx].push_back(-1);
            used++;
            recordVisit(cur, visitedSpotsByThisAgent, spotUseToday, lifetimeBrands, dailyBrands, nullptr);
            anchor = cur;
        }

        while (used + patrolReserve < steps) {
            int bestSpot = -1;
            int bestScore = numeric_limits<int>::min();
            Route bestRoute;
            int routeBudget = steps - used - patrolReserve;

            for (size_t si = 0; si < g_spots.size(); si++) {
                const Spot& sp = g_spots[si];
                if (visitedSpotsByThisAgent.count(sp.pos)) continue;
                int stockCap = max(1, sp.stocks);
                if (spotUseToday[sp.pos] >= stockCap) continue;

                Route r = dijkstraRoute(cur, sp.pos, status, routeBudget, fuel);
                if (!r.ok) continue;

                bool newLifetimeBrand = lifetimeBrands.count(sp.brand) == 0;
                bool newDailyBrand = dailyBrands.count(sp.brand) == 0;
                int remainingStock = stockCap - spotUseToday[sp.pos];
                bool reusedSpotByTeam = spotUseToday[sp.pos] > 0;
                bool allKnownBrands = g_totalBrands > 0 &&
                                      (int)lifetimeBrands.size() >= g_totalBrands;
                bool allDailyBrands = g_totalBrands > 0 &&
                                      (int)dailyBrands.size() >= g_totalBrands;

                int score = 60000; // moi slot stock con lai la mot phan udon that.
                if (newLifetimeBrand) score += 1200000;
                if (newDailyBrand) score += 260000;
                if (allKnownBrands && allDailyBrands) score += 50000;
                score += remainingStock * (reusedSpotByTeam ? 26000 : 14000);
                score += min(stockCap, 8) * 5000;
                if (reusedSpotByTeam) score += 45000;
                score -= r.steps * (allDailyBrands ? 1600 : 750);
                score -= r.fuel * 120;

                if (!patrolAnchors.empty() && !reusedSpotByTeam) {
                    int sep = numeric_limits<int>::max();
                    for (int other : patrolAnchors) sep = min(sep, roughDistance(sp.pos, other));
                    score += min(sep, max(W, H)) * 900;
                    if (sep <= 2) score -= 18000;
                    else if (sep <= 4) score -= 5000;
                }

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
                plan[idx].push_back(-1);
                used += 1;
            }
            appendRouteCommands(plan[idx], bestRoute);
            used += bestRoute.steps;
            fuel -= bestRoute.fuel;
            cur = sp.pos;

            if (anchor < 0) anchor = sp.pos;
            if (bestRoute.cells.empty()) {
                recordVisit(cur, visitedSpotsByThisAgent, spotUseToday, lifetimeBrands, dailyBrands, nullptr);
            } else {
                for (int cell : bestRoute.cells)
                    recordVisit(cell, visitedSpotsByThisAgent, spotUseToday, lifetimeBrands, dailyBrands, nullptr);
            }
        }

        if (used < steps) plan[idx].push_back(-(steps - used));
        if (plan[idx].empty() && steps > 0) plan[idx].push_back(-steps);
        draft[idx].end = cur;
        draft[idx].usedSteps = used;
        draft[idx].fuelLeft = fuel;
        patrolAnchors.push_back(anchor >= 0 ? anchor : cur);
    }

    vector<int> served(ags.size(), 0);
    for (int idx : supplies) {
        int cur = draft[idx].start;
        int bestPatrol = -1;
        int bestScore = numeric_limits<int>::min();
        Route bestRoute;

        for (int pass = 0; pass < 2 && bestPatrol < 0; pass++) {
            int supplyBudget = steps - (pass == 0 ? 1 : 0);
            if (supplyBudget < 0) continue;
            for (int pidx : patrols) {
                int target = draft[pidx].end;
                Route r = dijkstraRoute(cur, target, status, supplyBudget, 1 << 28);
                if (!r.ok) continue;

                int overlap = steps - max(draft[pidx].usedSteps, r.steps);
                int missingFuel = max(0, g_fuelLimit - draft[pidx].fuelLeft);
                bool lowFuel = g_fuelLimit > 0 && draft[pidx].fuelLeft <= max(2, g_fuelLimit / 3);
                int score = missingFuel * 1000 - r.steps * 40 - served[pidx] * 70000;
                if (lowFuel) score += 120000;
                if (overlap > 0) score += 60000;
                if (pass == 1) score -= 30000; // fallback: co the chi dung chung o dau ngay sau

                if (score > bestScore ||
                    (score == bestScore && (bestPatrol < 0 || r.steps < bestRoute.steps))) {
                    bestScore = score;
                    bestPatrol = pidx;
                    bestRoute = r;
                }
            }
        }

        if (bestPatrol >= 0) {
            appendRouteCommands(plan[idx], bestRoute);
            int used = bestRoute.steps;
            if (used < steps) plan[idx].push_back(-(steps - used));
            served[bestPatrol]++;
            draft[idx].end = draft[bestPatrol].end;
            draft[idx].usedSteps = used;
        } else if (steps > 0) {
            plan[idx].push_back(-steps);
        }
    }

    return plan;
}

static void commitPendingTrace(int day) {
    if (g_pendingTrace.day != day) return;
    for (int brand : g_pendingTrace.brands) g_collectedBrands.insert(brand);
    g_pendingTrace = PendingTrace();
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
    g_pendingTrace = PendingTrace();

    const mj::Value& mp = m["map"];
    W = mp["width"].asInt(); H = mp["height"].asInt();
    g_cells.assign((size_t)W * H, 0);
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++)
            g_cells[r * W + c] = mp["cells"][r][c].asInt();
    g_spots.clear();
    set<int> setupBrands;
    for (size_t i = 0; i < m["spots"].size(); i++) {
        Spot s;
        s.brand = m["spots"][i]["brand"].asInt();
        s.pos = m["spots"][i]["pos"].asInt();
        s.stocks = max(1, m["spots"][i]["stocks"].asInt());
        g_spots.push_back(s);
        setupBrands.insert(s.brand);
    }
    g_totalBrands = (int)setupBrands.size();
    g_daySteps.clear();
    for (size_t i = 0; i < m["daySteps"].size(); i++) g_daySteps.push_back(m["daySteps"][i].asInt());
    g_fuelLimit = m["fuelLimits"].asInt();
    g_nAgents = (int)m["agents"].size();
    // Uu tien xe tuan tra vi moi xe co the thu phan udon rieng theo stock/ngay.
    g_supplyCount = chooseSupplyCount();
    g_patrolCount = g_nAgents - g_supplyCount;
    ostringstream out; out << "[";
    for (int i = 0; i < g_nAgents; i++) { if (i) out << ","; out << (i < g_patrolCount ? 0 : 1); }
    out << "]";
    return out.str();
}

// planActions: chia xe thu udon/cap nhien lieu, Dijkstra theo chi phi that, validate truoc khi gui.
static string planActions(const mj::Value& m) {
    int day = m["day"].asInt();
    int steps = (day >= 0 && day < (int)g_daySteps.size()) ? g_daySteps[day] : 30;
    map<int,int> status;
    for (size_t i = 0; i < m["traffics"].size(); i++)
        status[m["traffics"][i]["pos"].asInt()] = m["traffics"][i]["status"].asInt();
    const mj::Value& ags = m["agents"];

    vector<vector<int>> plan = buildSplitRefuelPlan(m, status, steps);
    string err;
    if (!validatePlan(plan, m, steps, status, &err)) {
        fprintf(stderr, "validator chan plan ngay %d: %s; dung yen fallback\n", day, err.c_str());
        plan = waitPlan(ags.size(), steps);
    }
    g_pendingTrace = tracePlan(plan, m, status);
    return renderPlan(plan);
}

static void sleepMs(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "dung: %s <URL> <MATCH_ID> <TOKEN>\n", argv[0]); return 2; }
    string base = string(argv[1]) + "/api/v1/matches/" + argv[2];
    string token = argv[3];
    const int POLL_MS = 205; // >= 200ms, giam do tre nhan state moi

    // 1) SETUP — chờ tới khi lấy được (425 = bản đồ chưa mở / trận chưa tới giờ).
    string assignBody;
    for (;;) {
        auto r = http::request(base, "GET", "/setup", token, "");
        if (r.status == 200) { auto v = mj::parse(r.body); assignBody = parseSetup(*v); break; }
        if (r.status == 0 || r.status == 425 || r.status == 429) { sleepMs(POLL_MS); continue; }
        fprintf(stderr, "GET /setup -> HTTP %d (token sai / match khong hop le?)\n", r.status); return 1;
    }
    fprintf(stderr, "nhan setup: %dx%d o, %zu diem, %d xe (%d tuan tra, %d tiep te)\n",
            W, H, g_spots.size(), g_nAgents, g_patrolCount, g_supplyCount);

    // 2) ASSIGNMENT — gửi loại agent (cố định cả trận).
    for (;;) {
        auto r = http::request(base, "POST", "/assignment", token, assignBody);
        string reason;
        if (actionAccepted(r, &reason)) break;
        if (r.status == 200) {
            fprintf(stderr, "POST /assignment bi tu choi%s%s\n",
                    reason.empty() ? "" : ": ", reason.c_str());
            return 1;
        }
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
                    commitPendingTrace(day);
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

// Bot mẫu HEXUDON (format BTC gốc) — C++17, transport HTTP polling. KHÔNG thư viện ngoài (raw socket).
// Chạy:  ./bot <URL> <MATCH_ID> <TOKEN>      (vd ./bot http://judge:8099 m-0001 <token>)
// Build: g++ -std=c++17 -O2 -o bot main.cpp   (Windows nối thêm -lws2_32)
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

enum FuelMode {
    FUEL_LOW,
    FUEL_MEDIUM,
    FUEL_HIGH
};

// Ngan sach thoi gian cho mot ngay. Truoc day beam chay den het maxDepth bat ke
// dong ho, tren map du lon la vuot daySeconds va mat trang mot ngay.
static chrono::steady_clock::time_point g_planDeadline;
static bool g_planDeadlineSet = false;

static bool planTimeUp() {
    return g_planDeadlineSet && chrono::steady_clock::now() >= g_planDeadline;
}

static int envInt(const char* name, int fallback) {
    const char* raw = getenv(name);
    if (!raw || !*raw) return fallback;
    char* end = nullptr;
    long v = strtol(raw, &end, 10);
    if (end == raw || v < 0 || v > (1 << 30)) return fallback;
    return (int)v;
}

static int W, H, g_nAgents;
static vector<int> g_cells;    // phẳng row*W+col (0 đất,1 đường,2 núi,3 ao)
static vector<Spot> g_spots;   // các điểm udon: brand/pos/stocks
static vector<int> g_spotAt;   // pos -> chỉ số spot, -1 nếu ô không có spot
static vector<int> g_daySteps; // số bước mỗi ngày
static vector<int> g_daySeconds; // giây được phép trả lời mỗi ngày
static int g_fuelLimit = 0;
static int g_patrolCount = 0;
static int g_supplyCount = 0;
static int g_totalBrands = 0;
static int g_daysLeft = 1;     // so ngay con lai ke ca ngay dang lap ke hoach
static int g_lastPlanMs = 0;   // thoi gian lap ke hoach ngay vua roi
static FuelMode g_fuelMode = FUEL_MEDIUM;
static set<int> g_collectedBrands;
static PendingTrace g_pendingTrace;

static int neighbor(int pos, int d) {
    if (W <= 0 || d < 0 || d >= 6) return -1;
    int r = pos / W, c = pos % W;
    static constexpr int DE[6][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 0}};
    static constexpr int DO[6][2] = {{-1, -1}, {0, -1}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}};
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
// Duyet theo NHIEN LIEU truoc, buoc sau. Nhien lieu la ngan sach ca tran con
// buoc chi la ngan sach mot ngay: do tren fixture that, xe tieu het nhien lieu
// khi con thua 1/3 so buoc. Duong di re buoc (1 buoc) nhung dat nhien lieu
// (2 don vi), dat thi nguoc lai — toi thieu hoa buoc la chon dung cai dat thu
// dang thieu.
struct Node {
    int fuel;
    int steps;
    int pos;
};
// Duyet theo tai nguyen dang THIEU truoc. Nhien lieu la ngan sach ca tran, buoc
// la ngan sach mot ngay; tuy map ma cai nao can truoc. Bang chi phi lam hai
// chieu nay nguoc nhau (duong: 1 buoc/2 nhien lieu, dat: 2 buoc/1 nhien lieu)
// nen chon nham chieu la toi uu dung cai dang du.
static bool g_routeByFuel = true;
static bool g_fuelTight = true;
struct NodeCmp {
    bool operator()(const Node& a, const Node& b) const {
        if (g_routeByFuel) {
            if (a.fuel != b.fuel) return a.fuel > b.fuel;
            return a.steps > b.steps;
        }
        if (a.steps != b.steps) return a.steps > b.steps;
        return a.fuel > b.fuel;
    }
};

// Dijkstra tren khong gian (o, nhien lieu da dung). Chay MOT lan cho moi nguon,
// khong dung som, roi dung lai duong toi bat ky dich nao — thay cho viec goi
// lai ca ham cho tung cap (nguon, dich).
struct DijkstraField {
    bool ok = false;
    int src = -1;
    int stride = 0;
    vector<int> dist, prevState, prevDir;
    vector<int> bestAtPos;   // pos -> id trang thai tot nhat da chot, -1 neu chua toi
};

static DijkstraField dijkstraField(int src, const map<int,int>& status, int stepLimit, int fuelLimit) {
    DijkstraField f;
    int n = (int)g_cells.size();
    if (src < 0 || src >= n || stepLimit < 0 || fuelLimit < 0) return f;
    if (g_cells[src] == 3) return f;

    const int INF = numeric_limits<int>::max() / 4;
    // Chieu trang thai la SO BUOC da dung; khoang cach Dijkstra la nhien lieu.
    f.ok = true;
    f.src = src;
    f.stride = stepLimit + 1;
    int total = n * f.stride;
    f.dist.assign(total, INF);
    f.prevState.assign(total, -1);
    f.prevDir.assign(total, -1);
    f.bestAtPos.assign(n, -1);

    priority_queue<Node, vector<Node>, NodeCmp> pq;
    int start = src * f.stride;
    f.dist[start] = 0;
    pq.push({0, 0, src});

    while (!pq.empty()) {
        Node cur = pq.top(); pq.pop();
        int curId = cur.pos * f.stride + cur.steps;
        if (cur.fuel != f.dist[curId]) continue;
        // Dijkstra chot theo thu tu (fuel, steps) tang dan, nen lan chot dau
        // tien tai mot o chinh la duong re nhien lieu nhat toi o do.
        if (f.bestAtPos[cur.pos] < 0) f.bestAtPos[cur.pos] = curId;

        auto c = moveCost(cur.pos, status.count(cur.pos) ? status.at(cur.pos) : 0);
        if (c.first < 0) continue;
        for (int d = 0; d < 6; d++) {
            int nb = neighbor(cur.pos, d);
            if (nb < 0 || g_cells[nb] == 3) continue;
            int ns = cur.steps + c.first;
            int nf = cur.fuel + c.second;
            if (ns > stepLimit || nf > fuelLimit) continue;
            int nextId = nb * f.stride + ns;
            if (nf < f.dist[nextId]) {
                f.dist[nextId] = nf;
                f.prevState[nextId] = curId;
                f.prevDir[nextId] = d;
                pq.push({nf, ns, nb});
            }
        }
    }
    return f;
}

static Route routeFromField(const DijkstraField& f, int dst) {
    Route out;
    if (!f.ok || dst < 0 || dst >= (int)f.bestAtPos.size()) return out;
    if (dst == f.src) { out.ok = true; return out; }
    int goal = f.bestAtPos[dst];
    if (goal < 0) return out;

    int start = f.src * f.stride;
    vector<int> dirs;
    for (int at = goal; at != start; at = f.prevState[at]) dirs.push_back(f.prevDir[at]);
    reverse(dirs.begin(), dirs.end());

    int cur = f.src;
    for (int d : dirs) {
        cur = neighbor(cur, d);
        out.cells.push_back(cur);
    }
    out.ok = true;
    out.dirs = dirs;
    out.steps = goal % f.stride;
    out.fuel = f.dist[goal];
    return out;
}

static Route dijkstraRoute(int src, int dst, const map<int,int>& status, int stepLimit, int fuelLimit) {
    if (src == dst) { Route out; out.ok = (src >= 0 && src < (int)g_cells.size()); return out; }
    return routeFromField(dijkstraField(src, status, stepLimit, fuelLimit), dst);
}

// Tra spot theo ô bằng bảng phẳng: hai ham nay nam trong vong trong cua beam,
// quet tuyen tinh o day tung la phan lon thoi gian tren map lon.
static int spotIndexAt(int pos) {
    if (pos < 0 || pos >= (int)g_spotAt.size()) return -1;
    return g_spotAt[pos];
}

static const Spot* spotAt(int pos) {
    int idx = spotIndexAt(pos);
    return idx < 0 ? nullptr : &g_spots[idx];
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

static FuelMode classifyFuelMode() {
    int longestDay = maxDaySteps();
    if (longestDay <= 0 || g_fuelLimit <= 0) return FUEL_MEDIUM;

    int lowLimit = longestDay + max(3, longestDay / 5);
    int mediumLimit = longestDay * 2 + max(2, longestDay / 10);
    if (g_fuelLimit <= lowLimit) return FUEL_LOW;
    if (g_fuelLimit <= mediumLimit) return FUEL_MEDIUM;
    return FUEL_HIGH;
}

static const char* fuelModeName(FuelMode mode) {
    switch (mode) {
        case FUEL_LOW: return "low";
        case FUEL_HIGH: return "high";
        default: return "medium";
    }
}

// Tong so buoc cua ca tran, khong phai cua mot ngay.
static int totalDaySteps() {
    int sum = 0;
    for (int steps : g_daySteps) sum += steps;
    return sum;
}

// Nhien lieu co phai thu dang thieu khong? Tra loi bang bang chi phi VA dia hinh
// co that tren map, khong chot cung: dia hinh re nhien lieu nhat quyet dinh can
// bao nhieu nhien lieu de tieu het ngan sach buoc cua ca tran.
//   dat  {2 buoc, 1 nhien lieu} -> 0.50 nhien lieu/buoc
//   nui  {3 buoc, 2 nhien lieu} -> 0.67
//   duong{1 buoc, 2 nhien lieu} -> 2.00
// Map toan duong can gap 4 lan nhien lieu so voi map toan dat cho cung so buoc.
static bool computeFuelTight() {
    static const int STEP_OF[3] = {2, 1, 3};
    static const int FUEL_OF[3] = {1, 2, 2};
    // Tinh theo TY LE dia hinh that tren map, khong theo dia hinh re nhat co
    // mat: map 82% duong ma tinh theo 10% dat con lai thi uoc luong hut gan ba
    // lan, va bot se dinh tuyen theo nhung thu no dang du.
    long long fuelSum = 0, stepSum = 0;
    for (int c : g_cells) {
        if (c < 0 || c > 2) continue;
        fuelSum += FUEL_OF[c];
        stepSum += STEP_OF[c];
    }
    if (stepSum <= 0 || g_fuelLimit <= 0) return false;
    // need = totalSteps * (fuelSum / stepSum), lam tron len.
    long long need = ((long long)totalDaySteps() * fuelSum + stepSum - 1) / stepSum;
    return g_fuelLimit < need;
}

static int chooseSupplyCount() {
    int forced = envInt("PROCON_SUPPLY", -1);
    if (forced >= 0) return min(forced, max(0, g_nAgents - 1));
    if (g_nAgents <= 1) return 0;

    // Mac dinh KHONG dung xe tiep te. Ly do, theo do manh yeu dan:
    //   1. Xe tiep te khong bao gio thu udon — dieu nay chac chan.
    //   2. Tren MOI map da do, bo no deu hon: fixture that cua tran queue an
    //      tron 60/60 voi 4 xe tuan tra, con 3 tuan tra + 1 tiep te chi 59.
    //   3. Khong co bang chung nao cho thay no co ich: arena khong mo phong
    //      viec nap nhien lieu, va chua ai thu tren judge that.
    // Diem 3 la mot lo hong that: neu judge that cho nap nhien lieu hieu qua
    // thi tren map CUC thieu nhien lieu, xe tiep te co the dang gia. Chua do
    // duoc thi khong doan — dat PROCON_SUPPLY=1 de thu tren tran that.
    //
    // Chu y: "thieu nhien lieu" theo nghia khong du de di het moi buoc moi ngay
    // KHONG keo theo can xe tiep te. Chi can du de cham tran ton kho moi ngay.
    return 0;
}

// `fuelLimits` la ngan sach cho CA TRAN chu khong phai moi ngay: tren map lon,
// mot ngay 64 buoc toan duong (1 buoc = 2 nhien lieu) ngon tron 128 nhien lieu
// cua ca tran. Neu tieu tham lam theo ngay thi hai ngay cuoi xe dung yen, va
// dung yen la mat phan udon. Chia deu cho so ngay con lai, cho phep vuot mot
// he so nho de con bam duoc cum spot o xa; ngay cuoi thi tieu het.
static int fuelBudgetToday(int fuelLeft, int daysLeft) {
    if (fuelLeft <= 0) return 0;
    if (daysLeft <= 1) return fuelLeft;
    int even = (fuelLeft + daysLeft - 1) / daysLeft;
    int slack = envInt("PROCON_FUEL_SLACK", 100);   // phan tram cua phan chia deu
    long long allowed = (long long)even * slack / 100;
    return (int)min<long long>(fuelLeft, max<long long>(1, allowed));
}

static bool shouldReserveRefuelStep(int fuel, int steps) {
    if (g_supplyCount <= 0 || g_fuelLimit <= 0) return false;
    if (fuel <= max(2, g_fuelLimit / 3)) return true;
    return steps > 0 && fuel <= max(3, steps / 2);
}

struct BeamAgent {
    int pos = 0;
    int fuel = 0;        // nhien lieu con lai cho CA TRAN
    int fuelToday = 0;   // phan duoc phep tieu trong ngay hom nay
    int used = 0;
    vector<int> cmds;
    set<int> seenSpots;
};

struct BeamState {
    vector<BeamAgent> agents;
    vector<int> spotUse;
    set<int> lifetimeBrands;
    set<int> dailyBrands;
    int portions = 0;
    int usedStepsTotal = 0;
    int fuelUsed = 0;
    long long score = 0;   // beamScore(*this), tinh mot lan khi state thay doi
};

struct BeamGain {
    int portions = 0;
    int newLifetime = 0;
    int newDaily = 0;
    int reusedPortions = 0;
};

struct BeamExpansion {
    int agent = 0;
    Route route;
    BeamGain gain;
    long long score = 0;
};

static bool recordBeamVisitLite(int spotIdx, set<int>& seenSpots, vector<int>& spotUse,
                                set<int>& lifetimeBrands, set<int>& dailyBrands,
                                BeamGain* gain) {
    if (spotIdx < 0 || spotIdx >= (int)g_spots.size()) return false;
    const Spot& sp = g_spots[spotIdx];
    if (seenSpots.count(spotIdx)) return false;
    if (spotUse[spotIdx] >= max(1, sp.stocks)) return false;

    bool reused = spotUse[spotIdx] > 0;
    bool newLifetime = lifetimeBrands.count(sp.brand) == 0;
    bool newDaily = dailyBrands.count(sp.brand) == 0;

    seenSpots.insert(spotIdx);
    spotUse[spotIdx]++;
    lifetimeBrands.insert(sp.brand);
    dailyBrands.insert(sp.brand);

    if (gain) {
        gain->portions++;
        if (newLifetime) gain->newLifetime++;
        if (newDaily) gain->newDaily++;
        if (reused) gain->reusedPortions++;
    }
    return true;
}

static bool recordBeamVisit(int spotIdx, BeamState& st, int agentIdx, BeamGain* gain) {
    bool ok = recordBeamVisitLite(spotIdx, st.agents[agentIdx].seenSpots, st.spotUse,
                                  st.lifetimeBrands, st.dailyBrands, gain);
    if (ok) st.portions++;
    return ok;
}

static BeamGain measureBeamRoute(const BeamState& st, int agentIdx, const Route& route) {
    set<int> seenSpots = st.agents[agentIdx].seenSpots;
    vector<int> spotUse = st.spotUse;
    set<int> lifetimeBrands = st.lifetimeBrands;
    set<int> dailyBrands = st.dailyBrands;
    BeamGain gain;
    if (route.cells.empty()) {
        recordBeamVisitLite(spotIndexAt(st.agents[agentIdx].pos), seenSpots, spotUse,
                            lifetimeBrands, dailyBrands, &gain);
    } else {
        for (int cell : route.cells)
            recordBeamVisitLite(spotIndexAt(cell), seenSpots, spotUse,
                                lifetimeBrands, dailyBrands, &gain);
    }
    return gain;
}

static long long beamScore(const BeamState& st) {
    long long score = 0;
    int newLifetime = max(0, (int)st.lifetimeBrands.size() - (int)g_collectedBrands.size());
    int daily = (int)st.dailyBrands.size();

    score += (long long)newLifetime * 100000000000LL;
    score += (long long)daily * 4500000000LL;
    if (g_totalBrands > 0 && daily >= g_totalBrands) score += 900000000LL;
    score += (long long)st.portions * 140000000LL;

    for (const BeamAgent& ag : st.agents) {
        int si = spotIndexAt(ag.pos);
        if (si >= 0) score += (long long)min(max(1, g_spots[si].stocks), 8) * 6000000LL;
    }

    score -= (long long)st.usedStepsTotal * (g_fuelMode == FUEL_LOW ? 450000LL : 900000LL);
    score -= (long long)st.fuelUsed * (g_fuelMode == FUEL_LOW ? 220000LL : 320000LL);
    return score;
}

static long long beamExpansionScore(const BeamState& st, const Route& route, const BeamGain& gain) {
    bool dailyPhase = g_totalBrands <= 0 || (int)st.dailyBrands.size() < g_totalBrands;
    long long score = 0;
    score += (long long)gain.newLifetime * 100000000000LL;
    score += (long long)gain.newDaily * 5500000000LL;
    score += (long long)gain.portions * 220000000LL;

    if (dailyPhase && gain.newDaily == 0) score -= 2500000000LL;
    if (!dailyPhase || (int)st.dailyBrands.size() + gain.newDaily >= g_totalBrands) {
        score += (long long)gain.reusedPortions * 90000000LL;
    }

    int stepPenalty = dailyPhase ? 1100000 : (g_fuelMode == FUEL_LOW ? 1700000 : 3200000);
    int fuelPenalty = dailyPhase ? 180000 : (g_fuelMode == FUEL_LOW ? 260000 : 420000);
    score -= (long long)route.steps * stepPenalty;
    score -= (long long)route.fuel * fuelPenalty;
    return score;
}

static map<pair<int,int>, Route> buildRouteCache(const vector<int>& sourcePositions,
                                                 const map<int,int>& status, int steps) {
    set<int> sources;
    for (int src : sourcePositions) sources.insert(src);
    for (const Spot& sp : g_spots) sources.insert(sp.pos);

    map<pair<int,int>, Route> cache;
    int fuelCap = g_fuelLimit > 0 ? g_fuelLimit : max(1, steps * 2);
    for (int src : sources) {
        DijkstraField f = dijkstraField(src, status, steps, fuelCap);
        for (const Spot& sp : g_spots)
            cache[make_pair(src, sp.pos)] = routeFromField(f, sp.pos);
    }
    return cache;
}

static void applyBeamRoute(BeamState& st, int agentIdx, const Route& route, int steps) {
    BeamAgent& ag = st.agents[agentIdx];
    if (route.steps == 0) {
        if (ag.used < steps) {
            ag.cmds.push_back(-1);
            ag.used++;
            st.usedStepsTotal++;
            recordBeamVisit(spotIndexAt(ag.pos), st, agentIdx, nullptr);
            st.score = beamScore(st);
        }
        return;
    }

    appendRouteCommands(ag.cmds, route);
    ag.used += route.steps;
    ag.fuel -= route.fuel;
    ag.fuelToday -= route.fuel;
    st.usedStepsTotal += route.steps;
    st.fuelUsed += route.fuel;
    for (int cell : route.cells) recordBeamVisit(spotIndexAt(cell), st, agentIdx, nullptr);
    if (!route.cells.empty()) ag.pos = route.cells.back();
    st.score = beamScore(st);
}

static void planPatrolsWithBeam(const vector<int>& patrols, const map<int,int>& status, int steps,
                                vector<vector<int>>& plan, vector<AgentDraft>& draft) {
    if (patrols.empty()) return;

    BeamState start;
    start.spotUse.assign(g_spots.size(), 0);
    start.lifetimeBrands = g_collectedBrands;

    vector<int> sourcePositions;
    for (int idx : patrols) {
        BeamAgent ag;
        ag.pos = draft[idx].start;
        ag.fuel = draft[idx].startFuel;
        ag.fuelToday = fuelBudgetToday(ag.fuel, g_daysLeft);
        sourcePositions.push_back(ag.pos);
        start.agents.push_back(ag);
        if (steps > 0) {
            int si = spotIndexAt(ag.pos);
            if (si >= 0) {
                int localIdx = (int)start.agents.size() - 1;
                if (recordBeamVisit(si, start, localIdx, nullptr)) {
                    start.agents[localIdx].cmds.push_back(-1);
                    start.agents[localIdx].used = 1;
                    start.usedStepsTotal++;
                }
            }
        }
    }

    start.score = beamScore(start);
    map<pair<int,int>, Route> routeCache = buildRouteCache(sourcePositions, status, steps);
    vector<BeamState> beam(1, start);
    BeamState best = start;

    int patrolN = (int)patrols.size();
    int stockSlots = 0;
    for (const Spot& sp : g_spots) stockSlots += min(max(1, sp.stocks), patrolN);
    int maxDepth = min(stockSlots, max(10, steps * max(1, patrolN) / 2));
    maxDepth = min(maxDepth, envInt("PROCON_BEAM_DEPTH", W * H >= 400 ? 64 : 90));
    // Dijkstra mot-nguon-nhieu-dich lam moi tang beam re di nhieu lan, nen map
    // lon gio chay duoc beam rong bang map nho thay vi bi cat con mot nua.
    int beamWidth = max(1, envInt("PROCON_BEAM_WIDTH", 144));
    int branchLimit = max(1, envInt("PROCON_BEAM_BRANCH", 16));
    if (g_fuelMode == FUEL_LOW) {
        beamWidth = min(beamWidth, 64);
        branchLimit = min(branchLimit, 10);
    }

    // Moi tang beam chi giao duong cho MOT xe, nen phai chay it nhat patrolN
    // tang thi ca doi moi co viec lam. Cat som hon la tra ve ke hoach dung yen
    // — dung yen la mat phan udon, te hon ca mot ke hoach tham lam voi va.
    int minDepth = min(maxDepth, patrolN);

    for (int depth = 0; depth < maxDepth; depth++) {
        bool timeUp = planTimeUp();
        if (timeUp && depth >= minDepth) break;
        if (timeUp) { beamWidth = 1; branchLimit = 1; }  // het gio: ha ve tham lam
        vector<BeamState> next;
        for (const BeamState& st : beam) {
            vector<BeamExpansion> cand;
            for (int ai = 0; ai < patrolN; ai++) {
                const BeamAgent& ag = st.agents[ai];
                int reserve = shouldReserveRefuelStep(ag.fuel, steps - ag.used) ? 1 : 0;
                int routeBudget = steps - ag.used - reserve;
                if (routeBudget < 0) continue;

                for (size_t si = 0; si < g_spots.size(); si++) {
                    if (ag.seenSpots.count((int)si)) continue;
                    if (st.spotUse[si] >= max(1, g_spots[si].stocks)) continue;

                    Route route;
                    auto it = routeCache.find(make_pair(ag.pos, g_spots[si].pos));
                    if (it != routeCache.end()) route = it->second;
                    if (!route.ok || route.steps > routeBudget || route.fuel > ag.fuelToday) {
                        if (g_fuelMode != FUEL_LOW) continue;
                        route = dijkstraRoute(ag.pos, g_spots[si].pos, status, routeBudget, ag.fuelToday);
                    }
                    if (!route.ok || route.steps > routeBudget || route.fuel > ag.fuelToday) continue;
                    if (route.steps == 0 && ag.used >= steps) continue;

                    BeamGain gain = measureBeamRoute(st, ai, route);
                    if (gain.portions <= 0) continue;

                    BeamExpansion ex;
                    ex.agent = ai;
                    ex.route = route;
                    ex.gain = gain;
                    ex.score = beamExpansionScore(st, route, gain);
                    cand.push_back(ex);
                }
            }

            sort(cand.begin(), cand.end(), [](const BeamExpansion& a, const BeamExpansion& b) {
                if (a.score != b.score) return a.score > b.score;
                if (a.gain.portions != b.gain.portions) return a.gain.portions > b.gain.portions;
                return a.route.steps < b.route.steps;
            });
            if ((int)cand.size() > branchLimit) cand.resize(branchLimit);

            for (const BeamExpansion& ex : cand) {
                BeamState ns = st;
                applyBeamRoute(ns, ex.agent, ex.route, steps);
                next.push_back(ns);
            }
        }

        if (next.empty()) break;
        sort(next.begin(), next.end(), [](const BeamState& a, const BeamState& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.usedStepsTotal < b.usedStepsTotal;
        });
        if ((int)next.size() > beamWidth) next.resize(beamWidth);
        beam.swap(next);
        if (beam[0].score > best.score) best = beam[0];
    }

    for (int ai = 0; ai < patrolN; ai++) {
        int idx = patrols[ai];
        const BeamAgent& ag = best.agents[ai];
        plan[idx] = ag.cmds;
        if (ag.used < steps) plan[idx].push_back(-(steps - ag.used));
        if (plan[idx].empty() && steps > 0) plan[idx].push_back(-steps);
        draft[idx].end = ag.pos;
        draft[idx].usedSteps = ag.used;
        draft[idx].fuelLeft = ag.fuel;
    }
}

static vector<vector<int>> buildSplitRefuelPlan(const mj::Value& state, const map<int,int>& status, int steps) {
    const mj::Value& ags = state["agents"];
    vector<vector<int>> plan(ags.size());
    vector<AgentDraft> draft(ags.size());
    vector<int> patrols, supplies;
    for (size_t i = 0; i < ags.size(); i++) {
        int kind = ags[i]["kind"].asInt();
        int cur = ags[i]["pos"].asInt();
        int fuel = ags[i]["fuel"].isNull() ? (1 << 28) : ags[i]["fuel"].asInt();
        draft[i].start = draft[i].end = cur;
        draft[i].startFuel = draft[i].fuelLeft = fuel;
        if (kind == 0) patrols.push_back((int)i);
        else supplies.push_back((int)i);
    }

    planPatrolsWithBeam(patrols, status, steps, plan, draft);

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
    // Bang tra spot theo o; giu spot dau tien neu hai spot trung o.
    g_spotAt.assign(g_cells.size(), -1);
    for (size_t i = 0; i < g_spots.size(); i++) {
        int p = g_spots[i].pos;
        if (p >= 0 && p < (int)g_spotAt.size() && g_spotAt[p] < 0) g_spotAt[p] = (int)i;
    }
    g_totalBrands = (int)setupBrands.size();
    g_daySteps.clear();
    for (size_t i = 0; i < m["daySteps"].size(); i++) g_daySteps.push_back(m["daySteps"][i].asInt());
    g_daySeconds.clear();
    for (size_t i = 0; i < m["daySeconds"].size(); i++) g_daySeconds.push_back(m["daySeconds"][i].asInt());
    g_fuelLimit = m["fuelLimits"].asInt();
    g_nAgents = (int)m["agents"].size();
    // Uu tien xe tuan tra vi moi xe co the thu phan udon rieng theo stock/ngay.
    g_fuelTight = computeFuelTight();
    // Dinh tuyen theo tai nguyen dang thieu; PROCON_ROUTE_BY=1 ep theo nhien
    // lieu, =0 ep theo buoc, khong dat thi tu chon.
    g_routeByFuel = envInt("PROCON_ROUTE_BY", g_fuelTight ? 1 : 0) != 0;
    g_fuelMode = classifyFuelMode();
    g_supplyCount = chooseSupplyCount();
    g_patrolCount = g_nAgents - g_supplyCount;
    ostringstream out; out << "[";
    for (int i = 0; i < g_nAgents; i++) { if (i) out << ","; out << (i < g_patrolCount ? 0 : 1); }
    out << "]";
    return out.str();
}

// planActions: chia xe thu udon/cap nhien lieu, Dijkstra theo chi phi that, validate truoc khi gui.
static string planActions(const mj::Value& m) {
    auto planStart = chrono::steady_clock::now();
    int day = m["day"].asInt();
    int steps = (day >= 0 && day < (int)g_daySteps.size()) ? g_daySteps[day] : 30;

    // Ngan sach suy nghi. response_ms_total la tieu chi xep hang thu 4, DUOI
    // udon_total, nen doi them thoi gian lay them phan udon la co loi — nhung
    // chi trong gioi han daySeconds, va chua mot bien an toan de con kip POST.
    g_daysLeft = max(1, (int)g_daySteps.size() - max(0, day));

    int daySec = (day >= 0 && day < (int)g_daySeconds.size()) ? g_daySeconds[day] : 0;
    int budgetMs = envInt("PROCON_PLAN_MS", daySec > 0 ? max(1000, daySec * 1000 / 4) : 5000);
    if (daySec > 0) budgetMs = min(budgetMs, daySec * 1000 - 2000);
    g_planDeadline = chrono::steady_clock::now() + chrono::milliseconds(max(200, budgetMs));
    g_planDeadlineSet = true;
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
    g_lastPlanMs = (int)chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - planStart).count();
    return renderPlan(plan);
}

static void sleepMs(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "dung: %s <URL> <MATCH_ID> <TOKEN>\n", argv[0]); return 2; }
    string base = string(argv[1]) + "/api/v1/matches/" + argv[2];
    string token = argv[3];
    const int POLL_MS = envInt("PROCON_POLL_MS", 205); // >= 200ms luc binh thuong
    // Do tren fixture that: lap ke hoach chi ton 15-28 ms, nhung response_ms
    // that la ~242 ms/ngay — gan het la nam cho POLL_MS. Ngay moi thuong toi
    // ngay sau khi cac doi nop xong, nen bam sat trong mot cua ngan ke tu luc
    // MINH nop, roi lui ve nhip thuong de khong bi 429.
    const int BURST_MS = envInt("PROCON_BURST_MS", 40);
    const int BURST_WINDOW_MS = envInt("PROCON_BURST_WINDOW_MS", 2000);
    // Giua ngay thi khong co gi de doi: poll thua cho do ton request, de danh
    // nhip nhanh cho dung hai luc ngay CO THE sang.
    const int IDLE_MS = envInt("PROCON_IDLE_MS", 600);
    auto lastSubmit = chrono::steady_clock::time_point::min();
    auto dayFirstSeen = chrono::steady_clock::time_point::min();
    int dayLenMs = 0;

    // 1) SETUP — chờ tới khi lấy được (425 = bản đồ chưa mở / trận chưa tới giờ).
    string assignBody;
    for (;;) {
        auto r = http::request(base, "GET", "/setup", token, "");
        if (r.status == 200) { auto v = mj::parse(r.body); assignBody = parseSetup(*v); break; }
        if (r.status == 0 || r.status == 425 || r.status == 429) { sleepMs(POLL_MS); continue; }
        fprintf(stderr, "GET /setup -> HTTP %d (token sai / match khong hop le?)\n", r.status); return 1;
    }
    fprintf(stderr, "nhan setup: %dx%d o, %zu diem, %d xe (%d tuan tra, %d tiep te), fuel %d/%s\n",
            W, H, g_spots.size(), g_nAgents, g_patrolCount, g_supplyCount,
            g_fuelLimit, fuelModeName(g_fuelMode));
    fprintf(stderr, "nhien lieu %s, dinh tuyen theo %s\n",
            g_fuelTight ? "THIEU" : "du", g_routeByFuel ? "nhien lieu" : "buoc");

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
                // Moc thoi gian ngay theo dong ho CUA MINH: dung daySeconds
                // chu khong dung `endsAt`, de khong phu thuoc dong ho may minh
                // co lech voi server hay khong.
                dayFirstSeen = chrono::steady_clock::now();
                dayLenMs = (day >= 0 && day < (int)g_daySeconds.size())
                           ? g_daySeconds[day] * 1000 : 0;
                string acts = planActions(*v);
                auto pr = http::request(base, "POST", "/actions", token, acts);
                string reason;
                if (actionAccepted(pr, &reason)) {
                    commitPendingTrace(day);
                    lastDay = day;
                    lastSubmit = chrono::steady_clock::now();
                    fprintf(stderr, "ngay %d: da gui ke hoach (%d ms lap ke hoach), da biet %zu brand\n", day, g_lastPlanMs, g_collectedBrands.size());
                } else if (pr.status == 200) {
                    fprintf(stderr, "ngay %d: server tu choi action%s%s\n",
                            day, reason.empty() ? "" : ": ", reason.c_str());
                }
                // 429/lỗi: giữ lastDay để vòng sau gửi lại.
            }
        } else if (r.status == 429) {
            // Bi chan nhip: thoat che do bam sat ngay, quay ve nhip thuong.
            lastSubmit = chrono::steady_clock::time_point::min();
        } else if (r.status != 0) {
            auto rr = http::request(base, "GET", "/result", token, "");
            if (rr.status == 200) { fprintf(stderr, "ket thuc tran\n"); break; }
        }
        // Ngay sang o dung hai truong hop: moi doi da nop xong (ngay sau khi
        // MINH nop), hoac het gio ngay. Bam sat ca hai, thua ra o quang giua.
        auto now = chrono::steady_clock::now();
        int wait = POLL_MS;
        bool nearSwitch = false;
        if (lastSubmit != chrono::steady_clock::time_point::min()) {
            auto since = chrono::duration_cast<chrono::milliseconds>(now - lastSubmit).count();
            if (since < BURST_WINDOW_MS) nearSwitch = true;
        }
        if (dayLenMs > 0 && dayFirstSeen != chrono::steady_clock::time_point::min()) {
            auto into = chrono::duration_cast<chrono::milliseconds>(now - dayFirstSeen).count();
            long long leftMs = dayLenMs - into;
            if (leftMs <= 1500) nearSwitch = true;          // sap het gio ngay
            else if (!nearSwitch && leftMs > 3000) wait = IDLE_MS;
        }
        if (nearSwitch) wait = BURST_MS;
        sleepMs(wait);
    }
    return 0;
}

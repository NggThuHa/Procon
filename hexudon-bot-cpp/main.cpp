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
#include "strategy/hungarian.hpp"
using namespace std;

static strategy::Setup g_setup;
static strategy::State g_history;
static int g_nAgents;

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

static int W, H, g_nAgents;
static vector<int> g_cells;    // phẳng row*W+col (0 đất,1 đường,2 núi,3 ao)
static vector<Spot> g_spots;   // các điểm udon: brand/pos/stocks
static vector<int> g_daySteps; // số bước mỗi ngày
static int g_fuelLimit = 0;
static int g_patrolCount = 0;
static int g_supplyCount = 0;
static int g_totalBrands = 0;
static FuelMode g_fuelMode = FUEL_MEDIUM;
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

static const Spot* spotAt(int pos) {
    for (const auto& sp : g_spots) if (sp.pos == pos) return &sp;
    return nullptr;
}

static int spotIndexAt(int pos) {
    for (size_t i = 0; i < g_spots.size(); i++)
        if (g_spots[i].pos == pos) return (int)i;
    return -1;
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

static int chooseSupplyCount() {
    if (g_nAgents <= 1) return 0;

    if (g_fuelMode != FUEL_LOW) return 1;

    int supply = 1;
    if (g_nAgents >= 6) supply = 2;
    if (g_nAgents >= 8) supply = 3;

    supply = min(supply, g_nAgents - 1);
    return max(1, supply);
}

static bool shouldReserveRefuelStep(int fuel, int steps) {
    if (g_supplyCount <= 0 || g_fuelLimit <= 0) return false;
    if (fuel <= max(2, g_fuelLimit / 3)) return true;
    return steps > 0 && fuel <= max(3, steps / 2);
}

struct BeamAgent {
    int pos = 0;
    int fuel = 0;
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
    for (int src : sources)
        for (const Spot& sp : g_spots)
            cache[make_pair(src, sp.pos)] = dijkstraRoute(src, sp.pos, status, steps, fuelCap);
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
        }
        return;
    }

    appendRouteCommands(ag.cmds, route);
    ag.used += route.steps;
    ag.fuel -= route.fuel;
    st.usedStepsTotal += route.steps;
    st.fuelUsed += route.fuel;
    for (int cell : route.cells) recordBeamVisit(spotIndexAt(cell), st, agentIdx, nullptr);
    if (!route.cells.empty()) ag.pos = route.cells.back();
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

    map<pair<int,int>, Route> routeCache = buildRouteCache(sourcePositions, status, steps);
    vector<BeamState> beam(1, start);
    BeamState best = start;

    int patrolN = (int)patrols.size();
    int stockSlots = 0;
    for (const Spot& sp : g_spots) stockSlots += min(max(1, sp.stocks), patrolN);
    int maxDepth = min(stockSlots, max(10, steps * max(1, patrolN) / 2));
    maxDepth = min(maxDepth, W * H >= 400 ? 64 : 90);
    int beamWidth = W * H >= 400 ? 72 : 120;
    int branchLimit = W * H >= 400 ? 10 : 16;
    if (g_fuelMode == FUEL_LOW) {
        beamWidth = min(beamWidth, 64);
        branchLimit = min(branchLimit, 10);
    }

    for (int depth = 0; depth < maxDepth; depth++) {
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
                    if (!route.ok || route.steps > routeBudget || route.fuel > ag.fuel) {
                        if (g_fuelMode != FUEL_LOW) continue;
                        route = dijkstraRoute(ag.pos, g_spots[si].pos, status, routeBudget, ag.fuel);
                    }
                    if (!route.ok || route.steps > routeBudget || route.fuel > ag.fuel) continue;
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
            long long as = beamScore(a), bs = beamScore(b);
            if (as != bs) return as > bs;
            return a.usedStepsTotal < b.usedStepsTotal;
        });
        if ((int)next.size() > beamWidth) next.resize(beamWidth);
        beam.swap(next);
        if (beamScore(beam[0]) > beamScore(best)) best = beam[0];
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
    g_totalBrands = (int)setupBrands.size();
    g_daySteps.clear();
    for (size_t i = 0; i < m["daySteps"].size(); i++) g_daySteps.push_back(m["daySteps"][i].asInt());
    g_fuelLimit = m["fuelLimits"].asInt();
    g_nAgents = (int)m["agents"].size();
    // Uu tien xe tuan tra vi moi xe co the thu phan udon rieng theo stock/ngay.
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

static bool actionAccepted(const http::Response& response) {
    if (response.status != 200) return false;
    try {
        auto body = mj::parse(response.body);
        const mj::Value& valid = (*body)["valid"];
        return valid.type == mj::Value::BOOL && valid.boolean;
    } catch (...) {
        return false;
    }
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
    fprintf(stderr, "nhan setup: %dx%d o, %zu diem, %d xe (%d tuan tra, %d tiep te), fuel %d/%s\n",
            W, H, g_spots.size(), g_nAgents, g_patrolCount, g_supplyCount,
            g_fuelLimit, fuelModeName(g_fuelMode));

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

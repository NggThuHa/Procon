// Bot mẫu HEXUDON (format BTC gốc) — C++17, transport HTTP polling. KHÔNG thư viện ngoài (raw socket).
// Chạy:  ./bot <URL> <MATCH_ID> <TOKEN>      (vd ./bot http://judge:8099 m-0001 <token>)
// Build: g++ -std=c++17 -O2 -o bot main.cpp   (Windows nối thêm -lws2_32)
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "minijson.hpp"
#include "http.hpp"
#include "strategy/hungarian.hpp"
using namespace std;

static strategy::Setup g_setup;
static int g_nAgents;

// parseSetup: đọc setup, đặt globals, TRẢ mảng loại agent (phẳng "[0,..,1]") để POST /assignment.
static string parseSetup(const mj::Value& m) {
    const mj::Value& mp = m["map"];
    g_setup.width = mp["width"].asInt();
    g_setup.height = mp["height"].asInt();
    g_setup.cells.assign(static_cast<size_t>(g_setup.width) * g_setup.height, 0);
    for (int r = 0; r < g_setup.height; r++)
        for (int c = 0; c < g_setup.width; c++)
            g_setup.cells[r * g_setup.width + c] = mp["cells"][r][c].asInt();
    g_setup.spots.clear();
    for (size_t i = 0; i < m["spots"].size(); i++) {
        strategy::Spot spot;
        spot.pos = m["spots"][i]["pos"].asInt();
        spot.brand = m["spots"][i]["brand"].asInt();
        g_setup.spots.push_back(spot);
    }
    g_setup.daySteps.clear();
    for (size_t i = 0; i < m["daySteps"].size(); i++)
        g_setup.daySteps.push_back(m["daySteps"][i].asInt());
    g_nAgents = static_cast<int>(m["agents"].size());
    // Gán loại: xe cuối tiếp tế (1), còn lại tuần tra (0).
    ostringstream out; out << "[";
    for (int i = 0; i < g_nAgents; i++) { if (i) out << ","; out << (i == g_nAgents - 1 && g_nAgents > 1 ? 1 : 0); }
    out << "]";
    return out.str();
}

// planActions: chuyển state JSON sang model và gọi chiến thuật Hungarian.
static string planActions(const mj::Value& m) {
    int day = m["day"].asInt();
    const mj::Value& ags = m["agents"];
    strategy::State state;
    state.day = day;
    for (size_t i = 0; i < m["traffics"].size(); i++)
        state.traffic[m["traffics"][i]["pos"].asInt()] = m["traffics"][i]["status"].asInt();
    for (size_t i = 0; i < ags.size(); i++) {
        strategy::Agent agent;
        agent.kind = ags[i]["kind"].asInt();
        agent.pos = ags[i]["pos"].asInt();
        agent.fuel = ags[i]["fuel"].isNull() ? (1 << 30) : ags[i]["fuel"].asInt();
        state.agents.push_back(agent);
    }

    const strategy::Plan plan = strategy::planHungarian(g_setup, state);
    ostringstream out;
    out << "[";
    for (size_t i = 0; i < plan.actions.size(); i++) {
        if (i) out << ",";
        out << "[";
        for (size_t j = 0; j < plan.actions[i].size(); j++) {
            if (j) out << ",";
            out << plan.actions[i][j];
        }
        out << "]";
    }
    out << "]";
    return out.str();
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
    fprintf(stderr, "nhan setup: %dx%d o, %zu diem, %d xe\n", g_setup.width, g_setup.height, g_setup.spots.size(), g_nAgents);

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
                if (pr.status == 200) { lastDay = day; fprintf(stderr, "ngay %d: da gui ke hoach\n", day); }
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

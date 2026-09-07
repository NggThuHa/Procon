#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace strategy {

struct Spot {
    int pos = 0;
    int brand = 0;
};

struct Setup {
    int width = 0;
    int height = 0;
    std::vector<int> cells;
    std::vector<Spot> spots;
    std::vector<int> daySteps;
};

struct Agent {
    int kind = 0;
    int pos = 0;
    int fuel = 0;
};

struct State {
    int day = 0;
    std::vector<Agent> agents;
    std::map<int, int> traffic;
};

struct Plan {
    std::vector<std::vector<int>> actions;
};

struct Route {
    std::vector<int> directions;
    int steps = 0;
    int fuel = 0;
};

namespace detail {

static constexpr int INF = std::numeric_limits<int>::max() / 4;
static constexpr int DE[6][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 0}};
static constexpr int DO[6][2] = {{-1, -1}, {0, -1}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}};

inline int neighbor(const Setup& setup, int pos, int direction) {
    if (setup.width <= 0 || setup.height <= 0 || direction < 0 || direction >= 6 ||
        pos < 0 || pos >= static_cast<int>(setup.cells.size())) {
        return -1;
    }
    const int row = pos / setup.width;
    const int col = pos % setup.width;
    const int (*delta)[2] = (row % 2) ? DO : DE;
    const int nextCol = col + delta[direction][0];
    const int nextRow = row + delta[direction][1];
    if (nextCol < 0 || nextCol >= setup.width || nextRow < 0 || nextRow >= setup.height) {
        return -1;
    }
    return nextRow * setup.width + nextCol;
}

inline std::pair<int, int> moveCost(const Setup& setup, const State& state, int pos) {
    if (pos < 0 || pos >= static_cast<int>(setup.cells.size())) return {-1, -1};
    const int terrain = setup.cells[pos];
    if (terrain == 0) return {2, 1};
    if (terrain == 2) return {3, 2};
    if (terrain == 1) {
        const auto it = state.traffic.find(pos);
        const int status = it == state.traffic.end() ? 0 : it->second;
        if (status == 1) return {2, 2};
        if (status == 2) return {4, 2};
        return {1, 2};
    }
    return {-1, -1};
}

struct QueueItem {
    int steps;
    int fuel;
    int pos;
    bool operator>(const QueueItem& other) const {
        if (steps != other.steps) return steps > other.steps;
        if (fuel != other.fuel) return fuel > other.fuel;
        return pos > other.pos;
    }
};

struct Previous {
    int pos = -1;
    int fuel = -1;
    int direction = -1;
};

inline bool findRoute(const Setup& setup, const State& state, int source, int target,
                      int maxSteps, int maxFuel, Route& route) {
    const int count = static_cast<int>(setup.cells.size());
    if (source < 0 || source >= count || target < 0 || target >= count ||
        setup.cells[source] == 3 || setup.cells[target] == 3 || maxSteps < 0 || maxFuel < 0) {
        return false;
    }
    if (source == target) {
        route = {};
        return true;
    }

    // A shortest valid route never needs to revisit a cell. Since every move
    // consumes at most two fuel, this bounds the state space even when a
    // missing/null fuel value was converted to a very large integer.
    const int usefulMoves = std::min(maxSteps, std::max(0, count - 1));
    const int fuelLimit = std::min(maxFuel, usefulMoves * 2);
    if (fuelLimit <= 0) return false;

    std::vector<std::vector<int>> bestSteps(
        count, std::vector<int>(static_cast<size_t>(fuelLimit) + 1, INF));
    std::vector<std::vector<Previous>> previous(
        count, std::vector<Previous>(static_cast<size_t>(fuelLimit) + 1));
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    bestSteps[source][0] = 0;
    queue.push({0, 0, source});

    int targetFuel = -1;
    while (!queue.empty()) {
        const QueueItem current = queue.top();
        queue.pop();
        if (current.steps != bestSteps[current.pos][current.fuel]) continue;
        if (current.pos == target) {
            targetFuel = current.fuel;
            break;
        }

        const auto cost = moveCost(setup, state, current.pos);
        if (cost.first < 0) continue;
        for (int direction = 0; direction < 6; ++direction) {
            const int next = neighbor(setup, current.pos, direction);
            if (next < 0 || setup.cells[next] == 3) continue;
            const int nextSteps = current.steps + cost.first;
            const int nextFuel = current.fuel + cost.second;
            if (nextSteps > maxSteps || nextFuel > fuelLimit) continue;
            if (nextSteps >= bestSteps[next][nextFuel]) continue;
            bestSteps[next][nextFuel] = nextSteps;
            previous[next][nextFuel] = {current.pos, current.fuel, direction};
            queue.push({nextSteps, nextFuel, next});
        }
    }

    if (targetFuel < 0) return false;
    std::vector<int> reversed;
    int current = target;
    int fuel = targetFuel;
    while (current != source || fuel != 0) {
        const Previous& step = previous[current][fuel];
        if (step.pos < 0 || step.fuel < 0 || step.direction < 0) return false;
        reversed.push_back(step.direction);
        current = step.pos;
        fuel = step.fuel;
    }
    route.directions.assign(reversed.rbegin(), reversed.rend());
    route.steps = bestSteps[target][targetFuel];
    route.fuel = targetFuel;
    return true;
}

inline std::vector<int> hungarian(const std::vector<std::vector<int>>& costs) {
    const int rows = static_cast<int>(costs.size());
    if (rows == 0) return {};
    const int columns = static_cast<int>(costs[0].size());
    if (columns == 0) return std::vector<int>(rows, -1);

    // This implementation expects a square matrix. Callers add dummy columns.
    const int size = std::max(rows, columns);
    std::vector<std::vector<int64_t>> matrix(size, std::vector<int64_t>(size, INF));
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column)
            matrix[row][column] = costs[row][column];
    for (int row = rows; row < size; ++row)
        for (int column = 0; column < size; ++column)
            matrix[row][column] = 0;
    for (int row = 0; row < size; ++row)
        for (int column = columns; column < size; ++column)
            matrix[row][column] = 0;

    const int64_t large = static_cast<int64_t>(INF) * 4;
    std::vector<int64_t> u(size + 1), v(size + 1);
    std::vector<int> p(size + 1), way(size + 1);
    for (int row = 1; row <= size; ++row) {
        p[0] = row;
        int column0 = 0;
        std::vector<int64_t> minv(size + 1, large);
        std::vector<bool> used(size + 1, false);
        do {
            used[column0] = true;
            const int row0 = p[column0];
            int column1 = 0;
            int64_t delta = large;
            for (int column = 1; column <= size; ++column) {
                if (used[column]) continue;
                const int64_t current = matrix[row0 - 1][column - 1] - u[row0] - v[column];
                if (current < minv[column]) {
                    minv[column] = current;
                    way[column] = column0;
                }
                if (minv[column] < delta) {
                    delta = minv[column];
                    column1 = column;
                }
            }
            for (int column = 0; column <= size; ++column) {
                if (used[column]) {
                    u[p[column]] += delta;
                    v[column] -= delta;
                } else {
                    minv[column] -= delta;
                }
            }
            column0 = column1;
        } while (p[column0] != 0);
        do {
            const int previousColumn = way[column0];
            p[column0] = p[previousColumn];
            column0 = previousColumn;
        } while (column0 != 0);
    }

    std::vector<int> assignment(rows, -1);
    for (int column = 1; column <= size; ++column) {
        if (p[column] >= 1 && p[column] <= rows && column <= columns)
            assignment[p[column] - 1] = column - 1;
    }
    return assignment;
}

} // namespace detail

inline Plan planHungarian(const Setup& setup, const State& state) {
    Plan plan;
    const int agentCount = static_cast<int>(state.agents.size());
    plan.actions.assign(agentCount, {});
    const int steps = state.day >= 0 && state.day < static_cast<int>(setup.daySteps.size())
        ? setup.daySteps[state.day]
        : 0;
    if (steps < 0) return plan;

    std::vector<int> patrolIndices;
    for (int index = 0; index < agentCount; ++index) {
        if (steps > 0) plan.actions[index] = {-steps};
        if (state.agents[index].kind == 0) patrolIndices.push_back(index);
    }
    if (patrolIndices.empty() || setup.spots.empty()) return plan;

    // Only keep one entry for a position: the server collects from the spot
    // where the vehicle stops, so duplicate positions are the same target.
    std::vector<int> spots;
    std::set<int> seenSpots;
    for (const Spot& spot : setup.spots)
        if (seenSpots.insert(spot.pos).second) spots.push_back(spot.pos);

    const int rowCount = static_cast<int>(patrolIndices.size());
    const int spotCount = static_cast<int>(spots.size());
    std::vector<std::vector<int>> costs(rowCount, std::vector<int>(spotCount + rowCount, detail::INF));
    std::vector<std::vector<Route>> routes(rowCount, std::vector<Route>(spotCount));

    for (int row = 0; row < rowCount; ++row) {
        const Agent& agent = state.agents[patrolIndices[row]];
        for (int spot = 0; spot < spotCount; ++spot) {
            Route route;
            if (!detail::findRoute(setup, state, agent.pos, spots[spot],
                                    steps, agent.fuel, route)) continue;
            routes[row][spot] = route;
            costs[row][spot] = route.steps;
        }
        // A dummy target means this vehicle safely waits instead of taking an
        // infeasible route. Its cost is larger than any feasible route.
        for (int dummy = 0; dummy < rowCount; ++dummy) costs[row][spotCount + dummy] = steps + 1;
    }

    const std::vector<int> assignment = detail::hungarian(costs);
    for (int row = 0; row < rowCount; ++row) {
        const int column = row < static_cast<int>(assignment.size()) ? assignment[row] : -1;
        if (column < 0 || column >= spotCount || costs[row][column] >= detail::INF) continue;

        const Route& route = routes[row][column];
        std::vector<int>& action = plan.actions[patrolIndices[row]];
        action = route.directions;
        const int remaining = steps - route.steps;
        if (remaining > 0) action.push_back(-remaining);
    }
    return plan;
}

} // namespace strategy

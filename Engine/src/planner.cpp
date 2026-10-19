#include "warehouse/planner.hpp"

#include "warehouse/search.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

namespace wh {

std::string_view toString(PlannerKind kind)
{
    switch (kind) {
    case PlannerKind::AStarManhattan: return "astar-manhattan";
    case PlannerKind::AStarTrueDistance: return "astar-true-distance";
    case PlannerKind::Bfs: return "bfs";
    }
    return "unknown";
}

std::string_view toString(RobotOrder order)
{
    switch (order) {
    case RobotOrder::Input: return "input";
    case RobotOrder::LongestTaskFirst: return "longest-first";
    }
    return "unknown";
}

std::string_view displayName(PlannerKind kind)
{
    switch (kind) {
    case PlannerKind::AStarManhattan: return "A* (Manhattan)";
    case PlannerKind::AStarTrueDistance: return "A* (true distance)";
    case PlannerKind::Bfs: return "BFS (no heuristic)";
    }
    return "unknown";
}

std::string_view displayName(RobotOrder order)
{
    switch (order) {
    case RobotOrder::Input: return "Input order";
    case RobotOrder::LongestTaskFirst: return "Longest first";
    }
    return "unknown";
}

std::optional<PlannerKind> plannerFromString(std::string_view text)
{
    for (PlannerKind kind : kAllPlanners) {
        if (text == toString(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

std::optional<RobotOrder> orderFromString(std::string_view text)
{
    for (RobotOrder order : kAllOrders) {
        if (text == toString(order)) {
            return order;
        }
    }
    return std::nullopt;
}

const char* toString(PlanStatus status)
{
    switch (status) {
    case PlanStatus::Success: return "Success";
    case PlanStatus::NoPath: return "NoPath";
    case PlanStatus::InvalidScenario: return "InvalidScenario";
    }
    return "Unknown";
}

namespace {

// Estimate of the remaining moves from a cell to the goal.
// Returns -1 when the goal is known to be unreachable from that cell.
class Heuristic {
public:
    Heuristic(const Grid& grid, Cell goal, PlannerKind kind) : grid_(grid), goal_(goal), kind_(kind)
    {
        if (kind == PlannerKind::AStarTrueDistance) {
            distances_ = distanceMap(grid, goal);
        }
    }

    int operator()(Cell cell) const
    {
        switch (kind_) {
        case PlannerKind::AStarManhattan: return manhattanDistance(cell, goal_);
        case PlannerKind::AStarTrueDistance: return distances_[static_cast<std::size_t>(grid_.index(cell))];
        case PlannerKind::Bfs: return 0;
        }
        return 0;
    }

private:
    const Grid& grid_;
    Cell goal_;
    PlannerKind kind_;
    std::vector<int> distances_;
};

struct OpenEntry {
    int f;
    int g;
    int cell;
    std::uint64_t key;
};

// Smallest f first; ties go to the larger g (closer to the goal), then the
// smaller cell index. Every tie is broken, so results are identical on
// every compiler.
struct WorseEntry {
    bool operator()(const OpenEntry& a, const OpenEntry& b) const
    {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        if (a.g != b.g) {
            return a.g < b.g;
        }
        return a.cell > b.cell;
    }
};

struct NodeInfo {
    int g;
    int cell;
    std::uint64_t parent;
    bool closed;
};

constexpr std::uint64_t kNoParent = std::numeric_limits<std::uint64_t>::max();

} // namespace

LegResult searchLeg(const Grid& grid, const ReservationTable& reservations, Cell from, int startTime, Cell goal,
                    PlannerKind planner, int maxTime)
{
    LegResult result;
    if (!grid.isPassable(from) || !grid.isPassable(goal) || startTime > maxTime) {
        return result;
    }

    const Heuristic heuristic(grid, goal, planner);
    if (heuristic(from) < 0) {
        return result; // walls or shelves separate the robot from its goal
    }

    // After the last reserved time step nothing can block the robot any
    // more, so (cell, t) and (cell, t + 1) behave the same. Treating all such
    // times as one state keeps the search small and lets it finish quickly
    // even when no route exists.
    const int settledTime = std::max(startTime, reservations.lastReservedTime() + 1);
    const auto cellCount = static_cast<std::uint64_t>(grid.cellCount());
    auto stateKey = [&](int cell, int time) {
        return static_cast<std::uint64_t>(std::min(time, settledTime)) * cellCount + static_cast<std::uint64_t>(cell);
    };

    std::unordered_map<std::uint64_t, NodeInfo> nodes;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, WorseEntry> open;

    const int startCell = grid.index(from);
    const int goalCell = grid.index(goal);
    const std::uint64_t startKey = stateKey(startCell, startTime);
    nodes[startKey] = {0, startCell, kNoParent, false};
    open.push({heuristic(from), 0, startCell, startKey});

    while (!open.empty()) {
        const OpenEntry entry = open.top();
        open.pop();
        NodeInfo& node = nodes[entry.key];
        if (node.closed || entry.g != node.g) {
            continue; // a better copy of this state was already handled
        }
        node.closed = true;
        ++result.expandedNodes;

        if (entry.cell == goalCell) {
            for (std::uint64_t key = entry.key; key != kNoParent; key = nodes[key].parent) {
                result.path.push_back(grid.cellAt(nodes[key].cell));
            }
            std::reverse(result.path.begin(), result.path.end());
            result.found = true;
            return result;
        }

        const int time = startTime + entry.g;
        if (time >= maxTime) {
            result.reachedTimeLimit = true;
            continue;
        }

        const Cell current = grid.cellAt(entry.cell);
        // Four moves, then waiting in place.
        for (int option = 0; option < 5; ++option) {
            const Cell next = option < 4 ? step(current, kMoves[static_cast<std::size_t>(option)]) : current;
            if (!grid.isPassable(next) || !reservations.canMove(current, next, time)) {
                continue;
            }
            const int h = heuristic(next);
            if (h < 0) {
                continue;
            }
            const int nextCell = grid.index(next);
            const std::uint64_t nextKey = stateKey(nextCell, time + 1);
            const int g = entry.g + 1;
            const auto found = nodes.find(nextKey);
            if (found != nodes.end() && (found->second.closed || found->second.g <= g)) {
                continue;
            }
            nodes[nextKey] = {g, nextCell, entry.key, false};
            open.push({g + h, g, nextCell, nextKey});
        }
    }
    return result;
}

std::vector<int> planningOrder(const Scenario& scenario, RobotOrder order)
{
    std::vector<int> indices(scenario.robots.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<int>(i);
    }
    if (order == RobotOrder::Input) {
        return indices;
    }

    // Task length ignoring other robots; unreachable legs count as very long
    // so those robots are tried first and fail fast.
    std::vector<int> taskLength(scenario.robots.size());
    for (std::size_t i = 0; i < scenario.robots.size(); ++i) {
        const RobotTask& task = scenario.robots[i];
        const int toPickup = findPathBfs(scenario.grid, task.start, task.pickup).length();
        const int toStation = findPathBfs(scenario.grid, task.pickup, task.station).length();
        taskLength[i] = (toPickup < 0 || toStation < 0) ? std::numeric_limits<int>::max() : toPickup + toStation;
    }
    // Stable sort keeps input order for equal lengths, so the order is deterministic.
    std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) {
        return taskLength[static_cast<std::size_t>(a)] > taskLength[static_cast<std::size_t>(b)];
    });
    return indices;
}

PlanResult planScenario(const Scenario& scenario, const PlanOptions& options)
{
    const auto clockStart = std::chrono::steady_clock::now();
    PlanResult result;
    result.robots.resize(scenario.robots.size());

    // Fills in the status and metrics. Path metrics are only reported for a
    // complete plan; search effort and runtime are always reported.
    auto finish = [&](PlanStatus status) -> PlanResult {
        const long long expanded = result.metrics.expandedNodes;
        result.status = status;
        result.metrics = status == PlanStatus::Success ? computeMetrics(result.robots) : PlanMetrics{};
        result.metrics.success = status == PlanStatus::Success;
        result.metrics.expandedNodes = expanded;
        const auto elapsed = std::chrono::steady_clock::now() - clockStart;
        result.metrics.runtimeMs = std::chrono::duration<double, std::milli>(elapsed).count();
        return std::move(result);
    };

    const std::string problem = validateScenario(scenario);
    if (!problem.empty()) {
        result.message = "invalid scenario: " + problem;
        return finish(PlanStatus::InvalidScenario);
    }

    const Grid& grid = scenario.grid;
    const int maxTime = options.maxTime > 0 ? options.maxTime : grid.width() * grid.height() * 4;
    result.planningOrder = planningOrder(scenario, options.order);

    ReservationTable reservations(grid);
    for (const RobotTask& task : scenario.robots) {
        reservations.reserveCell(task.start, 0);
    }

    for (int robot : result.planningOrder) {
        const RobotTask& task = scenario.robots[static_cast<std::size_t>(robot)];

        const LegResult toPickup = searchLeg(grid, reservations, task.start, 0, task.pickup, options.planner, maxTime);
        result.metrics.expandedNodes += toPickup.expandedNodes;
        LegResult toStation;
        if (toPickup.found) {
            const int pickupTime = static_cast<int>(toPickup.path.size()) - 1;
            toStation = searchLeg(grid, reservations, task.pickup, pickupTime, task.station, options.planner, maxTime);
            result.metrics.expandedNodes += toStation.expandedNodes;
        }

        if (!toPickup.found || !toStation.found) {
            const bool limit = toPickup.reachedTimeLimit || toStation.reachedTimeLimit;
            result.failedRobot = robot;
            result.message = "robot " + std::to_string(robot) + ": no collision-free route to its " +
                             (toPickup.found ? "packing station" : "pickup cell") +
                             (limit ? " within " + std::to_string(maxTime) + " time steps" : "");
            return finish(PlanStatus::NoPath);
        }

        RobotPlan& plan = result.robots[static_cast<std::size_t>(robot)];
        plan.planned = true;
        plan.path = toPickup.path;
        plan.path.insert(plan.path.end(), toStation.path.begin() + 1, toStation.path.end());
        plan.pickupTime = static_cast<int>(toPickup.path.size()) - 1;
        plan.finishTime = static_cast<int>(plan.path.size()) - 1;

        reservations.reservePath(plan.path, 0);
    }

    return finish(PlanStatus::Success);
}

} // namespace wh

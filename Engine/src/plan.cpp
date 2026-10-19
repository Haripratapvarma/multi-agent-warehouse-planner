#include "warehouse/plan.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

namespace wh {

PlanMetrics computeMetrics(const std::vector<RobotPlan>& robots)
{
    PlanMetrics metrics;
    for (const RobotPlan& robot : robots) {
        if (!robot.planned || robot.path.empty()) {
            continue;
        }
        const int steps = static_cast<int>(robot.path.size()) - 1;
        metrics.makespan = std::max(metrics.makespan, steps);
        metrics.totalPathLength += steps;
        for (std::size_t t = 1; t < robot.path.size(); ++t) {
            if (robot.path[t] == robot.path[t - 1]) {
                ++metrics.waits;
            }
        }
    }
    return metrics;
}

namespace {

ValidationResult fail(const std::string& message)
{
    return {false, message};
}

std::string robotText(std::size_t robot)
{
    return "robot " + std::to_string(robot);
}

std::string cellText(Cell cell)
{
    std::ostringstream out;
    out << cell;
    return out.str();
}

ValidationResult checkSingleRobot(const Grid& grid, const RobotTask& task, const std::vector<Cell>& path,
                                  std::size_t robot)
{
    const std::string who = robotText(robot);
    if (path.empty()) {
        return fail(who + ": path is empty");
    }
    if (path.front() != task.start) {
        return fail(who + ": path does not begin at its start cell");
    }
    for (std::size_t t = 0; t < path.size(); ++t) {
        if (!grid.isPassable(path[t])) {
            return fail(who + ": cell " + cellText(path[t]) + " at time " + std::to_string(t) +
                        " is a wall, shelf or outside the grid");
        }
        if (t > 0 && manhattanDistance(path[t - 1], path[t]) > 1) {
            return fail(who + ": illegal move from " + cellText(path[t - 1]) + " to " + cellText(path[t]) +
                        " at time " + std::to_string(t - 1));
        }
    }

    const auto pickup = std::find(path.begin(), path.end(), task.pickup);
    if (pickup == path.end()) {
        return fail(who + ": never visits its pickup cell");
    }
    if (path.back() != task.station) {
        return fail(who + ": does not end at its packing station");
    }
    // Reaching the station after the pickup means the robot has delivered,
    // so that must be the last step of its path.
    const auto station = std::find(pickup, path.end(), task.station);
    if (station != path.end() - 1) {
        return fail(who + ": reaches its station after pickup but keeps moving");
    }
    return {};
}

} // namespace

ValidationResult validatePlan(const Scenario& scenario, const std::vector<std::vector<Cell>>& paths)
{
    if (paths.size() != scenario.robots.size()) {
        return fail("plan has " + std::to_string(paths.size()) + " paths but the scenario has " +
                    std::to_string(scenario.robots.size()) + " robots");
    }

    std::size_t makespan = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        ValidationResult single = checkSingleRobot(scenario.grid, scenario.robots[i], paths[i], i);
        if (!single) {
            return single;
        }
        makespan = std::max(makespan, paths[i].size() - 1);
    }

    // Step through time and look at every robot still on the grid.
    for (std::size_t t = 0; t <= makespan; ++t) {
        std::map<Cell, std::size_t> occupied;         // cell -> robot at time t
        std::map<std::pair<Cell, Cell>, std::size_t> moves; // (from, to) -> robot, for t -> t+1

        for (std::size_t i = 0; i < paths.size(); ++i) {
            const std::vector<Cell>& path = paths[i];
            if (t >= path.size()) {
                continue; // delivered and removed
            }
            const auto [it, inserted] = occupied.emplace(path[t], i);
            if (!inserted) {
                return fail(robotText(it->second) + " and " + robotText(i) + " are both in cell " +
                            cellText(path[t]) + " at time " + std::to_string(t));
            }
            if (t + 1 < path.size() && path[t] != path[t + 1]) {
                const auto opposite = moves.find({path[t + 1], path[t]});
                if (opposite != moves.end()) {
                    return fail(robotText(opposite->second) + " and " + robotText(i) + " swap cells " +
                                cellText(path[t]) + " and " + cellText(path[t + 1]) + " between time " +
                                std::to_string(t) + " and " + std::to_string(t + 1));
                }
                moves.emplace(std::make_pair(path[t], path[t + 1]), i);
            }
        }
    }
    return {};
}

ValidationResult validatePlan(const Scenario& scenario, const std::vector<RobotPlan>& robots)
{
    std::vector<std::vector<Cell>> paths;
    paths.reserve(robots.size());
    for (std::size_t i = 0; i < robots.size(); ++i) {
        if (!robots[i].planned) {
            return fail(robotText(i) + " has no plan");
        }
        paths.push_back(robots[i].path);
    }
    return validatePlan(scenario, paths);
}

} // namespace wh

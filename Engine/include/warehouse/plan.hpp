#pragma once

#include "warehouse/grid.hpp"
#include "warehouse/scenario.hpp"

#include <string>
#include <vector>

namespace wh {

// The planned route of one robot. path[t] is the robot's cell at time t,
// from its start (t = 0) to its packing station (t = finishTime). After
// finishTime the robot has delivered and is removed from the grid.
struct RobotPlan {
    bool planned = false;
    std::vector<Cell> path;
    int pickupTime = -1; // time step the robot reaches its pickup cell
    int finishTime = -1; // time step the robot reaches its station

    friend bool operator==(const RobotPlan&, const RobotPlan&) = default;
};

struct PlanMetrics {
    bool success = false;
    int makespan = 0;        // time step when the last robot finishes
    int totalPathLength = 0; // sum over robots of steps taken, waits included
    int waits = 0;           // steps where a robot stayed in the same cell
    double runtimeMs = 0.0;  // wall-clock planning time
    long long expandedNodes = 0; // search effort over all robots

    // Everything except runtime, which changes from run to run.
    bool sameResultAs(const PlanMetrics& other) const
    {
        return success == other.success && makespan == other.makespan &&
               totalPathLength == other.totalPathLength && waits == other.waits &&
               expandedNodes == other.expandedNodes;
    }
};

// Fills makespan, total path length and waits from the robots' paths.
// Robots that were not planned are skipped.
PlanMetrics computeMetrics(const std::vector<RobotPlan>& robots);

// Result of checking a plan against the rules.
struct ValidationResult {
    bool valid = true;
    std::string message; // empty when valid, otherwise the first problem found

    explicit operator bool() const { return valid; }
};

// Checks any plan step by step, independent of how it was produced:
//   - one path per robot, starting at the robot's start cell
//   - legal moves only: up, down, left, right or wait, never into a wall or shelf
//   - the pickup cell is visited before the robot ends at its station
//   - no two robots in the same cell at the same time
//   - no two robots swapping cells head-on between two time steps
// A robot is on the grid from t = 0 until the last cell of its path.
ValidationResult validatePlan(const Scenario& scenario, const std::vector<std::vector<Cell>>& paths);
ValidationResult validatePlan(const Scenario& scenario, const std::vector<RobotPlan>& robots);

} // namespace wh

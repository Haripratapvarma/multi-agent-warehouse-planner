#pragma once

#include "warehouse/plan.hpp"
#include "warehouse/reservation_table.hpp"
#include "warehouse/scenario.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wh {

// The three cooperative planners. All use the same reservation rules, so
// all produce collision-free plans; they differ only in how the search
// is guided.
enum class PlannerKind {
    AStarManhattan,    // A* with |dx| + |dy| as the estimate
    AStarTrueDistance, // A* with exact distances from a backward BFS (ignores other robots)
    Bfs,               // no estimate at all: breadth-first / uniform-cost search
};

// Prioritized planning plans robots one at a time, so the order matters.
enum class RobotOrder {
    Input,           // order the robots appear in the scenario
    LongestTaskFirst // robots with the longest start -> pickup -> station distance go first
};

inline constexpr PlannerKind kAllPlanners[] = {PlannerKind::AStarManhattan, PlannerKind::AStarTrueDistance,
                                               PlannerKind::Bfs};
inline constexpr RobotOrder kAllOrders[] = {RobotOrder::Input, RobotOrder::LongestTaskFirst};

// Short names used on the command line and in CSV files.
std::string_view toString(PlannerKind kind);
std::string_view toString(RobotOrder order);
// Longer names for tables.
std::string_view displayName(PlannerKind kind);
std::string_view displayName(RobotOrder order);
std::optional<PlannerKind> plannerFromString(std::string_view text);
std::optional<RobotOrder> orderFromString(std::string_view text);

struct PlanOptions {
    PlannerKind planner = PlannerKind::AStarManhattan;
    RobotOrder order = RobotOrder::Input;
    // Latest time step any search may reach. 0 means width * height * 4.
    // Guarantees planning always ends with a result instead of running forever.
    int maxTime = 0;
};

enum class PlanStatus {
    Success,
    NoPath,          // some robot could not reach its pickup or station
    InvalidScenario, // the scenario failed validateScenario
};

const char* toString(PlanStatus status);

struct PlanResult {
    PlanStatus status = PlanStatus::NoPath;
    std::vector<RobotPlan> robots;  // indexed like scenario.robots
    std::vector<int> planningOrder; // robot indices in the order they were planned
    int failedRobot = -1;           // robot that could not be planned, or -1
    std::string message;            // explanation when status is not Success
    PlanMetrics metrics;

    bool success() const { return status == PlanStatus::Success; }
};

// The order robots will be planned in.
std::vector<int> planningOrder(const Scenario& scenario, RobotOrder order);

// Prioritized planning ("cooperative A*"):
//   1. Reserve every robot's start cell at t = 0.
//   2. For each robot in priority order, search over (cell, time) for the
//      shortest route start -> pickup, then pickup -> station starting from
//      the time it reached the pickup. Moves into reserved cells and head-on
//      swaps with reserved moves are not allowed; waiting in place is.
//   3. Reserve the robot's whole route so the following robots avoid it.
// Fast, and each robot gets its best route given the robots before it, but
// the overall plan is not guaranteed to be optimal, and a robot can fail if
// earlier robots block it in.
PlanResult planScenario(const Scenario& scenario, const PlanOptions& options);

// One leg of a robot's route through (cell, time) space, avoiding the
// reservations. Exposed for testing. path[i] is the cell at startTime + i.
struct LegResult {
    bool found = false;
    bool reachedTimeLimit = false;
    std::vector<Cell> path;
    long long expandedNodes = 0;
};

LegResult searchLeg(const Grid& grid, const ReservationTable& reservations, Cell from, int startTime, Cell goal,
                    PlannerKind planner, int maxTime);

} // namespace wh

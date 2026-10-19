#include "test_framework.hpp"

#include "warehouse/generator.hpp"
#include "warehouse/planner.hpp"
#include "warehouse/scenario_io.hpp"
#include "warehouse/search.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace wh;

namespace {

const std::filesystem::path kScenarioDir = WH_SCENARIO_DIR;

Scenario generated(std::uint32_t seed, int width, int height, int robots, double density)
{
    GeneratorSettings s;
    s.seed = seed;
    s.width = width;
    s.height = height;
    s.robotCount = robots;
    s.shelfDensity = density;
    return generateScenario(s);
}

PlanOptions options(PlannerKind planner, RobotOrder order, int maxTime = 0)
{
    PlanOptions o;
    o.planner = planner;
    o.order = order;
    o.maxTime = maxTime;
    return o;
}

// Independent reference: earliest time a robot leaving `from` at startTime
// can stand on `goal`, found by expanding the set of reachable cells one
// time step at a time. Uses no heuristic and no shortcuts. -1 if never.
int earliestArrival(const Grid& grid, const ReservationTable& table, Cell from, int startTime, Cell goal,
                    int maxTime)
{
    std::set<Cell> reachable{from};
    for (int t = startTime; t <= maxTime; ++t) {
        if (reachable.count(goal) != 0) {
            return t;
        }
        std::set<Cell> next;
        for (Cell cell : reachable) {
            const Cell choices[] = {cell, step(cell, kMoves[0]), step(cell, kMoves[1]), step(cell, kMoves[2]),
                                    step(cell, kMoves[3])};
            for (Cell to : choices) {
                if (grid.isPassable(to) && table.canMove(cell, to, t)) {
                    next.insert(to);
                }
            }
        }
        if (next.empty()) {
            return -1;
        }
        reachable = std::move(next);
    }
    return -1;
}

} // namespace

TEST_CASE(planner_names_round_trip)
{
    for (PlannerKind kind : kAllPlanners) {
        CHECK(plannerFromString(toString(kind)) == kind);
    }
    for (RobotOrder order : kAllOrders) {
        CHECK(orderFromString(toString(order)) == order);
    }
    CHECK(!plannerFromString("dijkstra").has_value());
    CHECK(!orderFromString("random").has_value());
}

TEST_CASE(single_robot_gets_shortest_route_with_no_waits)
{
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        const Scenario s = generated(seed, 20, 12, 1, 0.4);
        const RobotTask& task = s.robots[0];
        const int shortest =
            findPathAStar(s.grid, task.start, task.pickup).length() + findPathAStar(s.grid, task.pickup, task.station).length();
        for (PlannerKind planner : kAllPlanners) {
            const PlanResult result = planScenario(s, options(planner, RobotOrder::Input));
            REQUIRE(result.success());
            CHECK_EQ(result.metrics.makespan, shortest);
            CHECK_EQ(result.metrics.waits, 0);
            CHECK(validatePlan(s, result.robots).valid);
        }
    }
}

TEST_CASE(successful_plans_pass_the_validator)
{
    int runs = 0;
    int successes = 0;
    for (std::uint32_t seed = 1; seed <= 25; ++seed) {
        const Scenario s = generated(seed, 20, 12, 8, 0.4);
        for (PlannerKind planner : kAllPlanners) {
            for (RobotOrder order : kAllOrders) {
                const PlanResult result = planScenario(s, options(planner, order));
                ++runs;
                if (result.success()) {
                    ++successes;
                    const ValidationResult check = validatePlan(s, result.robots);
                    CHECK(check.valid);
                    if (!check.valid) {
                        std::cout << "    seed " << seed << ": " << check.message << '\n';
                    }
                    for (std::size_t i = 0; i < result.robots.size(); ++i) {
                        const RobotPlan& robot = result.robots[i];
                        CHECK_EQ(robot.path[static_cast<std::size_t>(robot.pickupTime)], s.robots[i].pickup);
                    }
                } else {
                    CHECK(result.failedRobot >= 0);
                    CHECK(!result.message.empty());
                }
            }
        }
    }
    // Prioritized planning can fail, but on these warehouses it rarely does.
    CHECK(successes * 10 >= runs * 9);
}

// Each robot's route must be the earliest possible arrival given the robots
// planned before it. Checked leg by leg against a brute-force reference.
TEST_CASE(each_leg_is_earliest_possible_given_reservations)
{
    for (std::uint32_t seed = 1; seed <= 15; ++seed) {
        const Scenario s = generated(seed, 20, 12, 8, 0.4);
        const int maxTime = s.grid.width() * s.grid.height() * 4;
        for (PlannerKind planner : kAllPlanners) {
            const PlanResult result = planScenario(s, options(planner, RobotOrder::Input));

            ReservationTable table(s.grid);
            for (const RobotTask& task : s.robots) {
                table.reserveCell(task.start, 0);
            }
            for (int robot : result.planningOrder) {
                const RobotTask& task = s.robots[static_cast<std::size_t>(robot)];
                const int pickupTime = earliestArrival(s.grid, table, task.start, 0, task.pickup, maxTime);
                if (robot == result.failedRobot) {
                    // Either the pickup or (if the pickup was reachable) the station is unreachable.
                    if (pickupTime >= 0) {
                        CHECK_EQ(earliestArrival(s.grid, table, task.pickup, pickupTime, task.station, maxTime), -1);
                    }
                    break;
                }
                const RobotPlan& plan = result.robots[static_cast<std::size_t>(robot)];
                CHECK_EQ(plan.pickupTime, pickupTime);
                CHECK_EQ(plan.finishTime, earliestArrival(s.grid, table, task.pickup, pickupTime, task.station, maxTime));
                table.reservePath(plan.path, 0);
            }
        }
    }
}

TEST_CASE(narrow_corridor_needs_waits)
{
    const Scenario s = loadScenarioFile(kScenarioDir / "narrow_corridor.txt");
    REQUIRE(s.robots.size() == 2);

    // Planned on their own, the two shortest routes crash into each other.
    std::vector<std::vector<Cell>> independent;
    for (const RobotTask& task : s.robots) {
        std::vector<Cell> path = findPathAStar(s.grid, task.start, task.pickup).path;
        const std::vector<Cell> second = findPathAStar(s.grid, task.pickup, task.station).path;
        path.insert(path.end(), second.begin() + 1, second.end());
        independent.push_back(path);
    }
    CHECK(!validatePlan(s, independent).valid);

    // Planned cooperatively, one robot waits and the plan is valid.
    for (PlannerKind planner : kAllPlanners) {
        for (RobotOrder order : kAllOrders) {
            const PlanResult result = planScenario(s, options(planner, order));
            REQUIRE(result.success());
            CHECK(validatePlan(s, result.robots).valid);
            CHECK(result.metrics.waits > 0);
            CHECK(result.metrics.totalPathLength > static_cast<int>(independent[0].size() + independent[1].size()) - 2);
        }
    }
}

TEST_CASE(same_seed_gives_identical_plan)
{
    for (std::uint32_t seed = 1; seed <= 5; ++seed) {
        for (PlannerKind planner : kAllPlanners) {
            for (RobotOrder order : kAllOrders) {
                const PlanResult a = planScenario(generated(seed, 25, 15, 8, 0.4), options(planner, order));
                const PlanResult b = planScenario(generated(seed, 25, 15, 8, 0.4), options(planner, order));
                CHECK(a.status == b.status);
                CHECK(a.robots == b.robots);
                CHECK(a.planningOrder == b.planningOrder);
                CHECK(a.metrics.sameResultAs(b.metrics));
            }
        }
    }
}

TEST_CASE(saved_and_loaded_scenario_gives_identical_plan)
{
    const Scenario original = generated(42, 30, 20, 10, 0.4);
    const Scenario loaded = scenarioFromString(scenarioToString(original));
    const PlanResult a = planScenario(original, options(PlannerKind::AStarManhattan, RobotOrder::LongestTaskFirst));
    const PlanResult b = planScenario(loaded, options(PlannerKind::AStarManhattan, RobotOrder::LongestTaskFirst));
    CHECK(a.robots == b.robots);
}

TEST_CASE(longest_task_first_orders_by_task_length)
{
    const Scenario s = generated(5, 25, 15, 8, 0.4);
    const std::vector<int> input = planningOrder(s, RobotOrder::Input);
    for (std::size_t i = 0; i < input.size(); ++i) {
        CHECK_EQ(input[i], static_cast<int>(i));
    }

    auto taskLength = [&](int robot) {
        const RobotTask& t = s.robots[static_cast<std::size_t>(robot)];
        return findPathBfs(s.grid, t.start, t.pickup).length() + findPathBfs(s.grid, t.pickup, t.station).length();
    };
    const std::vector<int> longest = planningOrder(s, RobotOrder::LongestTaskFirst);
    REQUIRE(longest.size() == s.robots.size());
    for (std::size_t i = 1; i < longest.size(); ++i) {
        CHECK(taskLength(longest[i - 1]) >= taskLength(longest[i]));
    }
    // Every robot appears exactly once.
    CHECK_EQ(std::set<int>(longest.begin(), longest.end()).size(), s.robots.size());
}

TEST_CASE(robot_order_can_decide_success)
{
    // Found by search: with input order a robot gets boxed in, with the
    // longest task first every robot gets through.
    const Scenario s = generated(6, 20, 12, 8, 0.4);
    for (PlannerKind planner : kAllPlanners) {
        const PlanResult input = planScenario(s, options(planner, RobotOrder::Input));
        const PlanResult longest = planScenario(s, options(planner, RobotOrder::LongestTaskFirst));
        CHECK_EQ(std::string(toString(input.status)), std::string("NoPath"));
        CHECK(input.failedRobot >= 0);
        CHECK(!input.metrics.success);
        CHECK(longest.success());
        CHECK(validatePlan(s, longest.robots).valid);
    }
}

TEST_CASE(time_limit_stops_the_search)
{
    const Scenario s = generated(1, 30, 20, 5, 0.3);
    for (PlannerKind planner : kAllPlanners) {
        const PlanResult result = planScenario(s, options(planner, RobotOrder::Input, 5));
        CHECK(!result.success());
        CHECK(result.message.find("within 5 time steps") != std::string::npos);
    }
}

TEST_CASE(boxed_in_robot_fails_cleanly)
{
    // Dead-end corridor:   S...    robot 1 starts at (0,1), the far end.
    //                      .....   Robot 0 (planned first) drives into (0,1)
    //                      ###P    to pick up, so robot 1 cannot get out.
    Scenario s;
    s.grid = Grid(4, 3, CellType::Wall);
    s.grid.set({0, 0}, CellType::Shelf);
    for (int x = 0; x < 4; ++x) {
        s.grid.set({x, 1}, CellType::Free);
    }
    s.grid.set({3, 2}, CellType::Station);
    s.robots = {
        {{1, 1}, {0, 0}, {0, 1}, {3, 2}},
        {{0, 1}, {0, 0}, {0, 1}, {3, 2}},
    };
    REQUIRE(validateScenario(s).empty());

    for (PlannerKind planner : kAllPlanners) {
        const PlanResult result = planScenario(s, options(planner, RobotOrder::Input));
        CHECK(result.status == PlanStatus::NoPath);
        CHECK_EQ(result.failedRobot, 1);
        CHECK(result.robots[0].planned);
        CHECK(!result.robots[1].planned);
        CHECK_EQ(result.metrics.makespan, 0);
    }
}

TEST_CASE(invalid_scenario_is_reported)
{
    Scenario s = generated(1, 12, 8, 2, 0.3);
    s.robots[1].start = s.robots[0].start;
    const PlanResult result = planScenario(s, options(PlannerKind::AStarManhattan, RobotOrder::Input));
    CHECK(result.status == PlanStatus::InvalidScenario);
    CHECK(!result.message.empty());
}

TEST_CASE(search_waits_for_a_reserved_cell)
{
    // Corridor 5x1. Another robot sits in (2,0) at times 1-3, then drives
    // off to the right, so a robot from (0,0) must wait before reaching (2,0).
    const Grid grid(5, 1);
    ReservationTable table(grid);
    table.reservePath({{3, 0}, {2, 0}, {2, 0}, {2, 0}, {3, 0}, {4, 0}}, 0);
    for (PlannerKind planner : kAllPlanners) {
        const LegResult leg = searchLeg(grid, table, {0, 0}, 0, {2, 0}, planner, 100);
        REQUIRE(leg.found);
        // Cell (2,0) is free again from t = 4.
        CHECK_EQ(static_cast<int>(leg.path.size()) - 1, 4);
        CHECK(leg.path.back() == (Cell{2, 0}));
    }
}

TEST_CASE(search_refuses_head_on_swap)
{
    // Corridor 3x2 with a side cell. Another robot moves (1,0) -> (0,0)
    // between t=0 and t=1. Our robot at (0,0) wants (2,0); moving to (1,0)
    // at t=0 would swap, so it must wait or step aside first.
    Grid grid(3, 2, CellType::Wall);
    grid.set({0, 0}, CellType::Free);
    grid.set({1, 0}, CellType::Free);
    grid.set({2, 0}, CellType::Free);
    ReservationTable table(grid);
    table.reservePath({{1, 0}, {0, 0}}, 0);
    // With nowhere to step aside and the other robot arriving in our cell,
    // there is no legal first move at all.
    const LegResult leg = searchLeg(grid, table, {0, 0}, 0, {2, 0}, PlannerKind::AStarManhattan, 100);
    CHECK(!leg.found);

    // Without the swap rule the robot could have gone straight through.
    ReservationTable cellsOnly(grid);
    cellsOnly.reserveCell({1, 0}, 0);
    cellsOnly.reserveCell({0, 0}, 1);
    CHECK(searchLeg(grid, cellsOnly, {0, 0}, 0, {2, 0}, PlannerKind::AStarManhattan, 100).found);
}

TEST_CASE(heuristics_reduce_search_effort)
{
    long long manhattan = 0;
    long long trueDistance = 0;
    long long bfs = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        const Scenario s = generated(seed, 25, 15, 6, 0.4);
        manhattan += planScenario(s, options(PlannerKind::AStarManhattan, RobotOrder::Input)).metrics.expandedNodes;
        trueDistance += planScenario(s, options(PlannerKind::AStarTrueDistance, RobotOrder::Input)).metrics.expandedNodes;
        bfs += planScenario(s, options(PlannerKind::Bfs, RobotOrder::Input)).metrics.expandedNodes;
    }
    CHECK(trueDistance <= manhattan);
    CHECK(manhattan < bfs);
}

int main() { return wh::test::runAll(); }

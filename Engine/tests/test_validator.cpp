#include "test_framework.hpp"

#include "warehouse/plan.hpp"

#include <string>
#include <vector>

using namespace wh;

namespace {

using Path = std::vector<Cell>;

// A 7x3 corridor:
//   #S###S#      shelves at (1,0) and (5,0)
//   .......      corridor, row 1
//   P#####P      stations at (0,2) and (6,2)
Scenario corridor()
{
    Scenario s;
    s.grid = Grid(7, 3, CellType::Wall);
    for (int x = 0; x < 7; ++x) {
        s.grid.set({x, 1}, CellType::Free);
    }
    s.grid.set({1, 0}, CellType::Shelf);
    s.grid.set({5, 0}, CellType::Shelf);
    s.grid.set({0, 2}, CellType::Station);
    s.grid.set({6, 2}, CellType::Station);
    return s;
}

// Robot that drives left to right: pickup (5,1), station (6,2).
RobotTask eastbound(Cell start) { return {start, {5, 0}, {5, 1}, {6, 2}}; }
// Robot that drives right to left: pickup (1,1), station (0,2).
RobotTask westbound(Cell start) { return {start, {1, 0}, {1, 1}, {0, 2}}; }

// Straight along row 1 from x = fromX to x = toX, then down to the station.
Path rowPath(int fromX, int toX, Cell station)
{
    Path path;
    const int direction = toX >= fromX ? 1 : -1;
    for (int x = fromX; x != toX + direction; x += direction) {
        path.push_back({x, 1});
    }
    path.push_back(station);
    return path;
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE(single_valid_path_passes)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    const ValidationResult result = validatePlan(s, std::vector<Path>{rowPath(0, 6, {6, 2})});
    CHECK(result.valid);
    CHECK(result.message.empty());
}

TEST_CASE(wrong_number_of_paths_fails)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    CHECK(!validatePlan(s, std::vector<Path>{}));
}

TEST_CASE(path_must_start_at_start_cell)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    const ValidationResult result = validatePlan(s, std::vector<Path>{rowPath(1, 6, {6, 2})});
    CHECK(!result);
    CHECK(contains(result.message, "start"));
}

TEST_CASE(jumps_and_diagonal_moves_fail)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};

    Path jump = rowPath(0, 6, {6, 2});
    jump.erase(jump.begin() + 2); // (1,1) -> (3,1)
    CHECK(!validatePlan(s, std::vector<Path>{jump}));

    // (5,1) -> (6,2) diagonally instead of via (6,1)
    const Path diagonal{{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 2}};
    const ValidationResult result = validatePlan(s, std::vector<Path>{diagonal});
    CHECK(!result);
    CHECK(contains(result.message, "illegal move"));
}

TEST_CASE(driving_into_walls_or_shelves_fails)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    Path intoShelf = rowPath(0, 6, {6, 2});
    intoShelf.insert(intoShelf.begin() + 2, Cell{1, 0}); // (1,1) -> shelf (1,0) -> (1,1)
    intoShelf.insert(intoShelf.begin() + 3, Cell{1, 1});
    CHECK(!validatePlan(s, std::vector<Path>{intoShelf}));
}

TEST_CASE(pickup_must_be_visited)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    s.robots[0].pickup = {3, 1}; // pretend the pickup is somewhere the path skips
    s.robots[0].shelf = {3, 0};
    s.grid.set({3, 0}, CellType::Shelf);
    const Path skipsPickup{{0, 1}, {0, 2}};
    s.robots[0].station = {0, 2};
    const ValidationResult result = validatePlan(s, std::vector<Path>{skipsPickup});
    CHECK(!result);
    CHECK(contains(result.message, "pickup"));
}

TEST_CASE(path_must_end_at_station)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    Path path = rowPath(0, 6, {6, 2});
    path.pop_back();
    CHECK(!validatePlan(s, std::vector<Path>{path}));
}

TEST_CASE(robot_stops_once_it_delivers)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    Path path = rowPath(0, 6, {6, 2});
    path.push_back({6, 1}); // leaves the station after delivering...
    path.push_back({6, 2}); // ...and comes back
    CHECK(!validatePlan(s, std::vector<Path>{path}));
}

TEST_CASE(passing_the_station_before_pickup_is_fine)
{
    Scenario s = corridor();
    // Westbound robot starting at the east end, next to the east station.
    s.robots = {{{6, 1}, {5, 0}, {5, 1}, {6, 2}}};
    // Visits its station (6,2) first, then picks up at (5,1), then delivers.
    const Path path{{6, 1}, {6, 2}, {6, 1}, {5, 1}, {6, 1}, {6, 2}};
    CHECK(validatePlan(s, std::vector<Path>{path}).valid);
}

TEST_CASE(two_robots_in_one_cell_fails)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1}), westbound({6, 1})};
    // Driving straight at each other they meet in (3,1) at time 3.
    const ValidationResult result =
        validatePlan(s, std::vector<Path>{rowPath(0, 6, {6, 2}), rowPath(6, 0, {0, 2})});
    CHECK(!result);
    CHECK(contains(result.message, "both in cell (3,1) at time 3"));
}

TEST_CASE(head_on_swap_fails)
{
    Scenario s = corridor();
    // Starting one cell closer, they never share a cell at a whole time step,
    // but between t=2 and t=3 they swap (2,1) and (3,1): they pass through each other.
    s.robots = {eastbound({0, 1}), westbound({5, 1})};
    const ValidationResult result =
        validatePlan(s, std::vector<Path>{rowPath(0, 6, {6, 2}), rowPath(5, 0, {0, 2})});
    CHECK(!result);
    CHECK(contains(result.message, "swap"));
}

TEST_CASE(following_one_cell_behind_is_fine)
{
    Scenario s = corridor();
    // Each step the follower moves into the cell the leader just left.
    s.robots = {eastbound({1, 1}), eastbound({0, 1})};
    CHECK(validatePlan(s, std::vector<Path>{rowPath(1, 6, {6, 2}), rowPath(0, 6, {6, 2})}).valid);
}

TEST_CASE(delivered_robots_leave_the_grid)
{
    Scenario s = corridor();
    // Robot 1 starts at (4,1) and delivers at (6,2) at t=3. Robot 0 reaches
    // the same station at t=7: allowed because robot 1 is gone by then.
    s.robots = {eastbound({0, 1}), eastbound({4, 1})};
    const Path quick{{4, 1}, {5, 1}, {6, 1}, {6, 2}};
    CHECK(validatePlan(s, std::vector<Path>{rowPath(0, 6, {6, 2}), quick}).valid);
}

TEST_CASE(robot_plans_must_all_be_present)
{
    Scenario s = corridor();
    s.robots = {eastbound({0, 1})};
    std::vector<RobotPlan> robots(1); // planned == false
    CHECK(!validatePlan(s, robots));
}

TEST_CASE(metrics_count_makespan_length_and_waits)
{
    RobotPlan a;
    a.planned = true;
    a.path = {{0, 0}, {1, 0}, {1, 0}, {2, 0}}; // 3 steps, 1 wait
    RobotPlan b;
    b.planned = true;
    b.path = {{0, 1}, {0, 1}, {0, 1}, {1, 1}, {2, 1}, {3, 1}}; // 5 steps, 2 waits
    RobotPlan notPlanned;

    const PlanMetrics m = computeMetrics({a, b, notPlanned});
    CHECK_EQ(m.makespan, 5);
    CHECK_EQ(m.totalPathLength, 8);
    CHECK_EQ(m.waits, 3);
}

int main() { return wh::test::runAll(); }

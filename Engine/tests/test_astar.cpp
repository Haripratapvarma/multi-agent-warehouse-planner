#include "test_framework.hpp"

#include "warehouse/generator.hpp"
#include "warehouse/search.hpp"

#include <cstdint>
#include <random>

using namespace wh;

namespace {

// A path is valid when it starts and ends in the right place, every cell is
// passable, and each step moves exactly one cell up, down, left or right.
bool isValidPath(const Grid& grid, const std::vector<Cell>& path, Cell start, Cell goal)
{
    if (path.empty() || path.front() != start || path.back() != goal) {
        return false;
    }
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!grid.isPassable(path[i])) {
            return false;
        }
        if (i > 0 && manhattanDistance(path[i - 1], path[i]) != 1) {
            return false;
        }
    }
    return true;
}

// Grid with walls scattered at random. Some start/goal pairs will be
// unreachable, which tests that both searches agree on "no path" too.
Grid randomWallGrid(std::mt19937& rng, int width, int height, int wallPercent)
{
    Grid grid(width, height);
    for (int i = 0; i < grid.cellCount(); ++i) {
        if (static_cast<int>(rng() % 100) < wallPercent) {
            grid.set(grid.cellAt(i), CellType::Wall);
        }
    }
    return grid;
}

Cell randomPassableCell(std::mt19937& rng, const Grid& grid)
{
    while (true) {
        const Cell c = grid.cellAt(static_cast<int>(rng() % static_cast<std::uint32_t>(grid.cellCount())));
        if (grid.isPassable(c)) {
            return c;
        }
    }
}

} // namespace

TEST_CASE(straight_line_on_empty_grid)
{
    const Grid grid(10, 1);
    const PathResult result = findPathAStar(grid, {0, 0}, {9, 0});
    REQUIRE(result.found());
    CHECK_EQ(result.length(), 9);
    CHECK(isValidPath(grid, result.path, {0, 0}, {9, 0}));
}

TEST_CASE(start_equals_goal_gives_zero_length_path)
{
    const Grid grid(3, 3);
    const PathResult result = findPathAStar(grid, {1, 1}, {1, 1});
    REQUIRE(result.found());
    CHECK_EQ(result.length(), 0);
    CHECK_EQ(result.path.size(), std::size_t{1});
}

TEST_CASE(goes_around_a_wall)
{
    // .....
    // .###.
    // S.#.G    straight line is blocked; shortest detour is 8 moves
    Grid grid(5, 3);
    grid.set({1, 1}, CellType::Wall);
    grid.set({2, 1}, CellType::Wall);
    grid.set({3, 1}, CellType::Wall);
    grid.set({2, 2}, CellType::Wall);
    const PathResult result = findPathAStar(grid, {0, 2}, {4, 2});
    REQUIRE(result.found());
    CHECK_EQ(result.length(), 8);
    CHECK(isValidPath(grid, result.path, {0, 2}, {4, 2}));
}

TEST_CASE(walled_off_goal_returns_no_path)
{
    // The goal at (3,2) is surrounded by walls on all four sides.
    Grid grid(7, 5);
    grid.set({3, 1}, CellType::Wall);
    grid.set({3, 3}, CellType::Wall);
    grid.set({2, 2}, CellType::Wall);
    grid.set({4, 2}, CellType::Wall);

    const PathResult astar = findPathAStar(grid, {0, 0}, {3, 2});
    CHECK_EQ(astar.status, SearchStatus::NoPath);
    CHECK(astar.path.empty());
    CHECK_EQ(astar.length(), -1);

    const PathResult bfs = findPathBfs(grid, {0, 0}, {3, 2});
    CHECK_EQ(bfs.status, SearchStatus::NoPath);
}

TEST_CASE(shelves_block_paths)
{
    // A column of shelves splits the grid in two.
    Grid grid(3, 3);
    for (int y = 0; y < 3; ++y) {
        grid.set({1, y}, CellType::Shelf);
    }
    CHECK_EQ(findPathAStar(grid, {0, 0}, {2, 2}).status, SearchStatus::NoPath);
}

TEST_CASE(stations_are_passable)
{
    Grid grid(3, 1);
    grid.set({1, 0}, CellType::Station);
    CHECK_EQ(findPathAStar(grid, {0, 0}, {2, 0}).length(), 2);
}

TEST_CASE(invalid_start_or_goal_is_reported)
{
    Grid grid(4, 4);
    grid.set({1, 1}, CellType::Wall);
    CHECK_EQ(findPathAStar(grid, {1, 1}, {3, 3}).status, SearchStatus::InvalidInput); // start on a wall
    CHECK_EQ(findPathAStar(grid, {0, 0}, {1, 1}).status, SearchStatus::InvalidInput); // goal on a wall
    CHECK_EQ(findPathAStar(grid, {-1, 0}, {3, 3}).status, SearchStatus::InvalidInput); // outside
    CHECK_EQ(findPathBfs(grid, {0, 0}, {9, 9}).status, SearchStatus::InvalidInput);
}

// The main optimality check: on many random grids, A* must find a path of
// exactly the same length as BFS (which is known to be shortest when every
// move costs 1), and both must agree when there is no path.
TEST_CASE(astar_length_matches_bfs_on_random_wall_grids)
{
    std::mt19937 rng(12345);
    int compared = 0;
    int unreachable = 0;
    for (int trial = 0; trial < 500; ++trial) {
        const int width = 5 + static_cast<int>(rng() % 26);
        const int height = 5 + static_cast<int>(rng() % 21);
        Grid grid = randomWallGrid(rng, width, height, 30);
        grid.set({0, 0}, CellType::Free); // guarantees at least one passable cell
        const Cell start = randomPassableCell(rng, grid);
        const Cell goal = randomPassableCell(rng, grid);

        const PathResult astar = findPathAStar(grid, start, goal);
        const PathResult bfs = findPathBfs(grid, start, goal);
        CHECK_EQ(astar.status, bfs.status);
        CHECK_EQ(astar.length(), bfs.length());
        if (astar.found()) {
            CHECK(isValidPath(grid, astar.path, start, goal));
            CHECK(isValidPath(grid, bfs.path, start, goal));
        } else {
            ++unreachable;
        }
        ++compared;
    }
    CHECK_EQ(compared, 500);
    CHECK(unreachable > 0); // make sure the "no path" case was really exercised
}

TEST_CASE(astar_length_matches_bfs_on_generated_warehouses)
{
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        GeneratorSettings settings;
        settings.seed = seed;
        settings.width = 30;
        settings.height = 20;
        settings.robotCount = 10;
        settings.shelfDensity = 0.5;
        const Scenario scenario = generateScenario(settings);

        for (const RobotTask& task : scenario.robots) {
            const Cell legs[][2] = {{task.start, task.pickup}, {task.pickup, task.station}};
            for (const auto& leg : legs) {
                const PathResult astar = findPathAStar(scenario.grid, leg[0], leg[1]);
                const PathResult bfs = findPathBfs(scenario.grid, leg[0], leg[1]);
                REQUIRE(astar.found());
                CHECK_EQ(astar.length(), bfs.length());
                CHECK(isValidPath(scenario.grid, astar.path, leg[0], leg[1]));
            }
        }
    }
}

// The heuristic should make A* do less work than BFS overall.
TEST_CASE(astar_expands_fewer_cells_than_bfs_in_total)
{
    long long astarTotal = 0;
    long long bfsTotal = 0;
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        GeneratorSettings settings;
        settings.seed = seed;
        settings.width = 30;
        settings.height = 20;
        settings.robotCount = 10;
        const Scenario scenario = generateScenario(settings);
        for (const RobotTask& task : scenario.robots) {
            astarTotal += findPathAStar(scenario.grid, task.start, task.pickup).expandedNodes;
            bfsTotal += findPathBfs(scenario.grid, task.start, task.pickup).expandedNodes;
        }
    }
    CHECK(astarTotal < bfsTotal);
}

TEST_CASE(search_is_deterministic)
{
    GeneratorSettings settings;
    settings.seed = 77;
    const Scenario scenario = generateScenario(settings);
    const RobotTask& task = scenario.robots[0];
    const PathResult first = findPathAStar(scenario.grid, task.start, task.station);
    const PathResult second = findPathAStar(scenario.grid, task.start, task.station);
    CHECK(first.path == second.path);
    CHECK_EQ(first.expandedNodes, second.expandedNodes);
}

int main() { return wh::test::runAll(); }

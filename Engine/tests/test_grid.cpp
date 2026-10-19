#include "test_framework.hpp"

#include "warehouse/grid.hpp"
#include "warehouse/scenario.hpp"

#include <stdexcept>

using namespace wh;

TEST_CASE(new_grid_is_filled_with_the_given_type)
{
    const Grid grid(4, 3, CellType::Free);
    CHECK_EQ(grid.width(), 4);
    CHECK_EQ(grid.height(), 3);
    CHECK_EQ(grid.cellCount(), 12);
    for (int i = 0; i < grid.cellCount(); ++i) {
        CHECK(grid.at(grid.cellAt(i)) == CellType::Free);
    }
}

TEST_CASE(invalid_grid_size_throws)
{
    CHECK_THROWS(Grid(0, 5), std::invalid_argument);
    CHECK_THROWS(Grid(5, -1), std::invalid_argument);
}

TEST_CASE(in_bounds_checks_all_four_edges)
{
    const Grid grid(3, 2);
    CHECK(grid.inBounds({0, 0}));
    CHECK(grid.inBounds({2, 1}));
    CHECK(!grid.inBounds({-1, 0}));
    CHECK(!grid.inBounds({0, -1}));
    CHECK(!grid.inBounds({3, 0}));
    CHECK(!grid.inBounds({0, 2}));
}

TEST_CASE(set_outside_grid_throws)
{
    Grid grid(3, 3);
    CHECK_THROWS(grid.set({3, 0}, CellType::Wall), std::out_of_range);
}

TEST_CASE(only_free_and_station_cells_are_passable)
{
    Grid grid(4, 1);
    grid.set({0, 0}, CellType::Free);
    grid.set({1, 0}, CellType::Wall);
    grid.set({2, 0}, CellType::Shelf);
    grid.set({3, 0}, CellType::Station);
    CHECK(grid.isPassable({0, 0}));
    CHECK(!grid.isPassable({1, 0}));
    CHECK(!grid.isPassable({2, 0}));
    CHECK(grid.isPassable({3, 0}));
    CHECK(!grid.isPassable({4, 0})); // outside
}

TEST_CASE(index_and_cell_at_round_trip)
{
    const Grid grid(7, 5);
    for (int i = 0; i < grid.cellCount(); ++i) {
        CHECK_EQ(grid.index(grid.cellAt(i)), i);
    }
    CHECK_EQ(grid.index({2, 3}), 3 * 7 + 2);
}

TEST_CASE(manhattan_distance_is_dx_plus_dy)
{
    CHECK_EQ(manhattanDistance({0, 0}, {0, 0}), 0);
    CHECK_EQ(manhattanDistance({1, 2}, {4, 6}), 7);
    CHECK_EQ(manhattanDistance({4, 6}, {1, 2}), 7);
}

TEST_CASE(moves_are_up_down_left_right)
{
    CHECK_EQ(step({5, 5}, kMoves[0]), (Cell{5, 4}));
    CHECK_EQ(step({5, 5}, kMoves[1]), (Cell{5, 6}));
    CHECK_EQ(step({5, 5}, kMoves[2]), (Cell{4, 5}));
    CHECK_EQ(step({5, 5}, kMoves[3]), (Cell{6, 5}));
}

TEST_CASE(cell_characters_round_trip)
{
    for (CellType type : {CellType::Free, CellType::Wall, CellType::Shelf, CellType::Station}) {
        const auto parsed = cellTypeFromChar(toChar(type));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == type);
    }
    CHECK(!cellTypeFromChar('?').has_value());
}

namespace {

// A 5x3 room with a shelf at (2,1) and a station at (4,1).
Scenario smallScenario()
{
    Scenario s;
    s.grid = Grid(5, 3);
    s.grid.set({2, 1}, CellType::Shelf);
    s.grid.set({4, 1}, CellType::Station);
    s.robots.push_back({{0, 0}, {2, 1}, {2, 0}, {4, 1}});
    return s;
}

} // namespace

TEST_CASE(valid_scenario_passes_validation)
{
    CHECK_EQ(validateScenario(smallScenario()), std::string());
}

TEST_CASE(validation_catches_bad_tasks)
{
    Scenario s = smallScenario();
    s.robots[0].pickup = {0, 2}; // not next to the shelf
    CHECK(!validateScenario(s).empty());

    s = smallScenario();
    s.robots[0].station = {3, 1}; // a free cell, not a station
    CHECK(!validateScenario(s).empty());

    s = smallScenario();
    s.robots[0].shelf = {1, 1}; // not a shelf
    CHECK(!validateScenario(s).empty());

    s = smallScenario();
    s.robots.push_back(s.robots[0]); // two robots on one start cell
    CHECK(!validateScenario(s).empty());

    s = smallScenario();
    s.robots.clear();
    CHECK(!validateScenario(s).empty());
}

int main() { return wh::test::runAll(); }

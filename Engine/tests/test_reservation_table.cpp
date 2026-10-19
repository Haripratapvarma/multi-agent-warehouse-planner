#include "test_framework.hpp"

#include "warehouse/reservation_table.hpp"

#include <stdexcept>

using namespace wh;

TEST_CASE(empty_table_reserves_nothing)
{
    const Grid grid(5, 5);
    const ReservationTable table(grid);
    CHECK_EQ(table.lastReservedTime(), -1);
    CHECK(!table.isCellReserved({2, 2}, 0));
    CHECK(table.canMove({2, 2}, {2, 3}, 0));
}

TEST_CASE(cell_reservation_only_blocks_that_cell_at_that_time)
{
    const Grid grid(5, 5);
    ReservationTable table(grid);
    table.reserveCell({2, 2}, 3);

    CHECK(table.isCellReserved({2, 2}, 3));
    CHECK(!table.isCellReserved({2, 2}, 2));
    CHECK(!table.isCellReserved({2, 2}, 4));
    CHECK(!table.isCellReserved({2, 3}, 3));
    CHECK_EQ(table.lastReservedTime(), 3);

    // Moving into (2,2) so as to arrive at time 3 is blocked; arriving at 4 is fine.
    CHECK(!table.canMove({2, 1}, {2, 2}, 2));
    CHECK(table.canMove({2, 1}, {2, 2}, 3));
}

TEST_CASE(waiting_in_a_reserved_cell_is_blocked)
{
    const Grid grid(3, 3);
    ReservationTable table(grid);
    table.reserveCell({1, 1}, 5);
    CHECK(!table.canMove({1, 1}, {1, 1}, 4)); // a wait from t=4 to t=5
    CHECK(table.canMove({1, 1}, {1, 1}, 5));
}

TEST_CASE(head_on_swap_is_blocked)
{
    const Grid grid(5, 1);
    ReservationTable table(grid);
    // Robot A moves (1,0) -> (2,0) between t=4 and t=5.
    table.reserveMove({1, 0}, {2, 0}, 4);

    // Robot B moving the other way at the same time would pass through A.
    CHECK(table.isSwapBlocked({2, 0}, {1, 0}, 4));
    CHECK(!table.canMove({2, 0}, {1, 0}, 4));

    // Same move one step earlier or later is a different time: not a swap.
    CHECK(!table.isSwapBlocked({2, 0}, {1, 0}, 3));
    CHECK(!table.isSwapBlocked({2, 0}, {1, 0}, 5));
}

TEST_CASE(following_another_robot_is_allowed)
{
    const Grid grid(5, 1);
    ReservationTable table(grid);
    // Robot A moves (2,0) -> (3,0) between t=0 and t=1.
    table.reservePath({{2, 0}, {3, 0}}, 0);
    // Robot B moves into the cell A is leaving, in the same direction.
    CHECK(table.canMove({1, 0}, {2, 0}, 0));
}

TEST_CASE(waits_are_not_stored_as_moves)
{
    const Grid grid(3, 3);
    ReservationTable table(grid);
    table.reserveMove({1, 1}, {1, 1}, 2);
    CHECK_EQ(table.moveReservationCount(), std::size_t{0});
    CHECK(!table.isSwapBlocked({1, 1}, {1, 1}, 2));
}

TEST_CASE(reserve_path_reserves_every_cell_and_move)
{
    const Grid grid(5, 5);
    ReservationTable table(grid);
    // Right, wait, down; starting at time 10.
    table.reservePath({{0, 0}, {1, 0}, {1, 0}, {1, 1}}, 10);

    CHECK(table.isCellReserved({0, 0}, 10));
    CHECK(table.isCellReserved({1, 0}, 11));
    CHECK(table.isCellReserved({1, 0}, 12));
    CHECK(table.isCellReserved({1, 1}, 13));
    CHECK(!table.isCellReserved({0, 0}, 11));
    CHECK_EQ(table.cellReservationCount(), std::size_t{4});
    CHECK_EQ(table.moveReservationCount(), std::size_t{2}); // the wait is not a move

    CHECK(table.isSwapBlocked({1, 0}, {0, 0}, 10));
    CHECK(table.isSwapBlocked({1, 1}, {1, 0}, 12));
    CHECK_EQ(table.lastReservedTime(), 13);
}

TEST_CASE(cells_and_times_never_mix_up)
{
    // Every (cell, time) pair on a small grid must be reserved independently.
    const Grid grid(4, 3);
    ReservationTable table(grid);
    table.reserveCell({3, 2}, 0); // last cell, time 0
    table.reserveCell({0, 0}, 1); // first cell, time 1
    for (int time = 0; time <= 2; ++time) {
        for (int i = 0; i < grid.cellCount(); ++i) {
            const Cell cell = grid.cellAt(i);
            const bool expected = (cell == Cell{3, 2} && time == 0) || (cell == Cell{0, 0} && time == 1);
            CHECK_EQ(table.isCellReserved(cell, time), expected);
        }
    }
}

TEST_CASE(clear_removes_everything)
{
    const Grid grid(3, 3);
    ReservationTable table(grid);
    table.reservePath({{0, 0}, {1, 0}}, 0);
    table.clear();
    CHECK_EQ(table.lastReservedTime(), -1);
    CHECK(!table.isCellReserved({0, 0}, 0));
    CHECK(!table.isSwapBlocked({0, 0}, {1, 0}, 0) && !table.isSwapBlocked({1, 0}, {0, 0}, 0));
}

TEST_CASE(bad_cells_and_times_throw)
{
    const Grid grid(3, 3);
    ReservationTable table(grid);
    CHECK_THROWS(table.reserveCell({3, 0}, 0), std::out_of_range);
    CHECK_THROWS(table.reserveCell({0, -1}, 0), std::out_of_range);
    CHECK_THROWS(table.reserveCell({0, 0}, -1), std::out_of_range);
    CHECK_THROWS(table.reserveMove({0, 0}, {0, 3}, 0), std::out_of_range);
}

int main() { return wh::test::runAll(); }

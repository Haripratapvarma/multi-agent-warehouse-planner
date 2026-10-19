#pragma once

#include "warehouse/grid.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace wh {

// Records where already-planned robots will be, so later robots can avoid them.
//
// Two kinds of reservation are stored:
//   - (cell, time): a robot is in this cell at this time step.
//     Stops two robots being in one cell at once (a "vertex conflict").
//   - (from, to, time): a robot moves from `from` (at time t) to `to` (at t+1).
//     Stops two robots swapping cells head-on, which would mean they pass
//     through each other (a "swap" or "edge conflict").
class ReservationTable {
public:
    explicit ReservationTable(const Grid& grid);

    void reserveCell(Cell cell, int time);
    // Robot is at `from` at `time` and at `to` at `time + 1`. Waits (from == to)
    // are not stored as moves; the cell reservation already covers them.
    void reserveMove(Cell from, Cell to, int time);
    // Reserves every cell of the path and every move between them.
    // path[i] is the robot's cell at time startTime + i.
    void reservePath(const std::vector<Cell>& path, int startTime = 0);

    bool isCellReserved(Cell cell, int time) const;
    // True when another robot reserved the opposite move (to -> from) at the
    // same time, so moving from -> to now would be a head-on swap.
    bool isSwapBlocked(Cell from, Cell to, int time) const;
    // A robot at `from` at `time` may be at `to` at `time + 1`.
    bool canMove(Cell from, Cell to, int time) const;

    // Latest time step with any reservation, or -1 when the table is empty.
    // After this time nothing is reserved, which the search uses to stop early.
    int lastReservedTime() const { return lastTime_; }

    std::size_t cellReservationCount() const { return cells_.size(); }
    std::size_t moveReservationCount() const { return moves_.size(); }

    void clear();

private:
    std::uint64_t cellKey(Cell cell, int time) const;
    std::uint64_t moveKey(Cell from, Cell to, int time) const;
    void checkCell(Cell cell) const;
    void checkTime(int time) const;

    int width_;
    int height_;
    std::uint64_t cellCount_;
    std::unordered_set<std::uint64_t> cells_;
    std::unordered_set<std::uint64_t> moves_;
    int lastTime_ = -1;
};

} // namespace wh

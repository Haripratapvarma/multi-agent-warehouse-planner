#include "warehouse/reservation_table.hpp"

#include <algorithm>
#include <stdexcept>

namespace wh {

ReservationTable::ReservationTable(const Grid& grid)
    : width_(grid.width()),
      height_(grid.height()),
      cellCount_(static_cast<std::uint64_t>(grid.cellCount()))
{
}

void ReservationTable::checkCell(Cell cell) const
{
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) {
        throw std::out_of_range("reservation cell is outside the grid");
    }
}

void ReservationTable::checkTime(int time) const
{
    if (time < 0) {
        throw std::out_of_range("reservation time cannot be negative");
    }
}

// Keys pack (time, cell) or (time, from, to) into one 64-bit number.
// Grids are at most 100x100 (10,000 cells), so this cannot overflow for any
// realistic time step.
std::uint64_t ReservationTable::cellKey(Cell cell, int time) const
{
    const auto index = static_cast<std::uint64_t>(cell.y * width_ + cell.x);
    return static_cast<std::uint64_t>(time) * cellCount_ + index;
}

std::uint64_t ReservationTable::moveKey(Cell from, Cell to, int time) const
{
    const auto fromIndex = static_cast<std::uint64_t>(from.y * width_ + from.x);
    const auto toIndex = static_cast<std::uint64_t>(to.y * width_ + to.x);
    return (static_cast<std::uint64_t>(time) * cellCount_ + fromIndex) * cellCount_ + toIndex;
}

void ReservationTable::reserveCell(Cell cell, int time)
{
    checkCell(cell);
    checkTime(time);
    cells_.insert(cellKey(cell, time));
    lastTime_ = std::max(lastTime_, time);
}

void ReservationTable::reserveMove(Cell from, Cell to, int time)
{
    checkCell(from);
    checkCell(to);
    checkTime(time);
    if (from == to) {
        return;
    }
    moves_.insert(moveKey(from, to, time));
    lastTime_ = std::max(lastTime_, time + 1);
}

void ReservationTable::reservePath(const std::vector<Cell>& path, int startTime)
{
    for (std::size_t i = 0; i < path.size(); ++i) {
        const int time = startTime + static_cast<int>(i);
        reserveCell(path[i], time);
        if (i + 1 < path.size()) {
            reserveMove(path[i], path[i + 1], time);
        }
    }
}

bool ReservationTable::isCellReserved(Cell cell, int time) const
{
    if (time < 0 || time > lastTime_) {
        return false;
    }
    return cells_.count(cellKey(cell, time)) != 0;
}

bool ReservationTable::isSwapBlocked(Cell from, Cell to, int time) const
{
    if (from == to || time < 0 || time > lastTime_) {
        return false;
    }
    return moves_.count(moveKey(to, from, time)) != 0;
}

bool ReservationTable::canMove(Cell from, Cell to, int time) const
{
    return !isCellReserved(to, time + 1) && !isSwapBlocked(from, to, time);
}

void ReservationTable::clear()
{
    cells_.clear();
    moves_.clear();
    lastTime_ = -1;
}

} // namespace wh

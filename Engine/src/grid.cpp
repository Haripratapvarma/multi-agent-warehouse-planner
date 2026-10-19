#include "warehouse/grid.hpp"

#include <cstdlib>
#include <stdexcept>

namespace wh {

std::ostream& operator<<(std::ostream& out, Cell cell)
{
    return out << '(' << cell.x << ',' << cell.y << ')';
}

int manhattanDistance(Cell a, Cell b)
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

char toChar(CellType type)
{
    switch (type) {
    case CellType::Free: return '.';
    case CellType::Wall: return '#';
    case CellType::Shelf: return 'S';
    case CellType::Station: return 'P';
    }
    return '?';
}

std::optional<CellType> cellTypeFromChar(char c)
{
    switch (c) {
    case '.': return CellType::Free;
    case '#': return CellType::Wall;
    case 'S': return CellType::Shelf;
    case 'P': return CellType::Station;
    default: return std::nullopt;
    }
}

Grid::Grid(int width, int height, CellType fill)
    : width_(width), height_(height)
{
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("grid width and height must be positive");
    }
    cells_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill);
}

void Grid::set(Cell c, CellType type)
{
    if (!inBounds(c)) {
        throw std::out_of_range("cell is outside the grid");
    }
    cells_[static_cast<std::size_t>(index(c))] = type;
}

bool Grid::isPassable(Cell c) const
{
    if (!inBounds(c)) {
        return false;
    }
    const CellType type = at(c);
    return type == CellType::Free || type == CellType::Station;
}

} // namespace wh

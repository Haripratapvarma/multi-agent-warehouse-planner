#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

namespace wh {

// A cell position on the grid. x grows to the right, y grows downwards
// (row 0 is the top row), matching how the map is printed.
struct Cell {
    int x = 0;
    int y = 0;

    friend auto operator<=>(const Cell&, const Cell&) = default;
};

std::ostream& operator<<(std::ostream& out, Cell cell);

// |dx| + |dy|: the number of moves needed on an empty 4-connected grid.
int manhattanDistance(Cell a, Cell b);

// The four moves a robot can make, in a fixed order (up, down, left, right).
// A fixed order keeps every search deterministic.
inline constexpr std::array<Cell, 4> kMoves{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};

inline Cell step(Cell from, Cell move) { return {from.x + move.x, from.y + move.y}; }

enum class CellType : std::uint8_t {
    Free,    // robots can drive here
    Wall,    // obstacle
    Shelf,   // obstacle; robots pick up from a free cell next to it
    Station, // packing station; robots can drive here
};

char toChar(CellType type);
std::optional<CellType> cellTypeFromChar(char c);

class Grid {
public:
    Grid() = default;
    Grid(int width, int height, CellType fill = CellType::Free);

    int width() const { return width_; }
    int height() const { return height_; }
    int cellCount() const { return width_ * height_; }

    bool inBounds(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_; }

    // Precondition: inBounds(c).
    CellType at(Cell c) const { return cells_[static_cast<std::size_t>(index(c))]; }
    void set(Cell c, CellType type);

    // True when c is inside the grid and a robot may stand on it.
    bool isPassable(Cell c) const;

    // Row-major index, used by the searches for flat arrays.
    int index(Cell c) const { return c.y * width_ + c.x; }
    Cell cellAt(int index) const { return {index % width_, index / width_}; }

    friend bool operator==(const Grid&, const Grid&) = default;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<CellType> cells_;
};

} // namespace wh

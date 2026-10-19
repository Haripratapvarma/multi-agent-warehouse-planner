#include "warehouse/generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <queue>
#include <random>
#include <stdexcept>
#include <vector>

namespace wh {

namespace {

constexpr int kMinWidth = 5;
constexpr int kMinHeight = 6;
constexpr int kMaxSize = 100;
constexpr double kMaxShelfDensity = 0.9;

// Small wrapper around std::mt19937.
//
// The raw numbers produced by std::mt19937 are fixed by the C++ standard,
// but std::uniform_int_distribution and std::shuffle may give different
// results on different compilers. Doing the range reduction and shuffling
// ourselves keeps scenarios identical on Windows and Linux.
class SeededRandom {
public:
    explicit SeededRandom(std::uint32_t seed) : engine_(seed) {}

    // Uniform integer in [0, n). Uses rejection to avoid modulo bias.
    std::uint32_t below(std::uint32_t n)
    {
        const std::uint64_t range = std::uint64_t{1} << 32;
        const std::uint64_t limit = range - (range % n);
        while (true) {
            const std::uint64_t value = engine_();
            if (value < limit) {
                return static_cast<std::uint32_t>(value % n);
            }
        }
    }

    // Fisher-Yates shuffle.
    template <typename T>
    void shuffle(std::vector<T>& items)
    {
        for (std::size_t i = items.size(); i > 1; --i) {
            const std::size_t j = below(static_cast<std::uint32_t>(i));
            std::swap(items[i - 1], items[j]);
        }
    }

private:
    std::mt19937 engine_;
};

void validateSettings(const GeneratorSettings& s)
{
    if (s.width < kMinWidth || s.width > kMaxSize) {
        throw std::invalid_argument("width must be between 5 and 100");
    }
    if (s.height < kMinHeight || s.height > kMaxSize) {
        throw std::invalid_argument("height must be between 6 and 100");
    }
    if (s.robotCount < 1) {
        throw std::invalid_argument("robot count must be at least 1");
    }
    if (s.robotCount > s.width - 2) {
        throw std::invalid_argument("robot count must not exceed width - 2 (one start cell per robot in the top row)");
    }
    if (!(s.shelfDensity >= 0.0 && s.shelfDensity <= kMaxShelfDensity)) {
        throw std::invalid_argument("shelf density must be between 0.0 and 0.9");
    }
}

// True when every passable cell can be reached from every other one.
bool isConnected(const Grid& grid)
{
    int passableCount = 0;
    int first = -1;
    for (int i = 0; i < grid.cellCount(); ++i) {
        if (grid.isPassable(grid.cellAt(i))) {
            ++passableCount;
            if (first < 0) {
                first = i;
            }
        }
    }
    if (passableCount == 0) {
        return false;
    }

    std::vector<bool> seen(static_cast<std::size_t>(grid.cellCount()), false);
    std::queue<Cell> open;
    open.push(grid.cellAt(first));
    seen[static_cast<std::size_t>(first)] = true;
    int reached = 1;
    while (!open.empty()) {
        const Cell current = open.front();
        open.pop();
        for (Cell move : kMoves) {
            const Cell next = step(current, move);
            if (grid.isPassable(next) && !seen[static_cast<std::size_t>(grid.index(next))]) {
                seen[static_cast<std::size_t>(grid.index(next))] = true;
                ++reached;
                open.push(next);
            }
        }
    }
    return reached == passableCount;
}

void addBorderWalls(Grid& grid)
{
    for (int x = 0; x < grid.width(); ++x) {
        grid.set({x, 0}, CellType::Wall);
        grid.set({x, grid.height() - 1}, CellType::Wall);
    }
    for (int y = 0; y < grid.height(); ++y) {
        grid.set({0, y}, CellType::Wall);
        grid.set({grid.width() - 1, y}, CellType::Wall);
    }
}

// Packing stations are spread evenly along the row above the bottom wall.
std::vector<Cell> addStations(Grid& grid)
{
    const int inner = grid.width() - 2;
    const int count = std::max(1, inner / 4);
    const int row = grid.height() - 2;
    std::vector<Cell> stations;
    for (int i = 0; i < count; ++i) {
        const Cell cell{1 + ((2 * i + 1) * inner) / (2 * count), row};
        grid.set(cell, CellType::Station);
        stations.push_back(cell);
    }
    return stations;
}

// Rack cells are the middle rows, except every third column which is kept
// as an aisle. Row 1 (robot starts) and the two bottom rows stay free.
std::vector<Cell> rackCells(const Grid& grid)
{
    std::vector<Cell> cells;
    for (int y = 2; y <= grid.height() - 4; ++y) {
        for (int x = 1; x <= grid.width() - 2; ++x) {
            if ((x - 1) % 3 != 0) {
                cells.push_back({x, y});
            }
        }
    }
    return cells;
}

// Quick local test used before the full connectivity check. Looks only at
// the 3x3 box around a cell that has just become an obstacle: if its free
// neighbours can still reach each other inside that box, the rest of the
// map is certainly still connected. If not, the caller does a full check.
bool neighboursStayConnectedLocally(const Grid& grid, Cell center)
{
    auto inBox = [&](Cell c) { return std::abs(c.x - center.x) <= 1 && std::abs(c.y - center.y) <= 1; };

    std::vector<Cell> targets;
    for (Cell move : kMoves) {
        const Cell next = step(center, move);
        if (grid.isPassable(next)) {
            targets.push_back(next);
        }
    }
    if (targets.empty()) {
        return false;
    }

    std::vector<Cell> reached{targets.front()};
    std::vector<Cell> open{targets.front()};
    while (!open.empty()) {
        const Cell current = open.back();
        open.pop_back();
        for (Cell move : kMoves) {
            const Cell next = step(current, move);
            if (next != center && inBox(next) && grid.isPassable(next) &&
                std::find(reached.begin(), reached.end(), next) == reached.end()) {
                reached.push_back(next);
                open.push_back(next);
            }
        }
    }
    for (Cell target : targets) {
        if (std::find(reached.begin(), reached.end(), target) == reached.end()) {
            return false;
        }
    }
    return true;
}

// Places shelves in random rack cells. A shelf is only kept if the free
// cells are still all connected, so every task can be reached.
void addShelves(Grid& grid, double density, SeededRandom& random)
{
    std::vector<Cell> candidates = rackCells(grid);
    random.shuffle(candidates);

    const long long wanted = std::llround(density * static_cast<double>(candidates.size()));
    const std::size_t target = static_cast<std::size_t>(std::max(1LL, wanted));

    std::size_t placed = 0;
    for (Cell cell : candidates) {
        if (placed == target) {
            break;
        }
        grid.set(cell, CellType::Shelf);
        if (neighboursStayConnectedLocally(grid, cell) || isConnected(grid)) {
            ++placed;
        } else {
            grid.set(cell, CellType::Free);
        }
    }
    if (placed == 0) {
        throw std::runtime_error("could not place any shelf");
    }
}

// Free cells next to a shelf where a robot could stand to pick up.
std::vector<Cell> pickupCellsFor(const Grid& grid, Cell shelf)
{
    std::vector<Cell> cells;
    for (Cell move : kMoves) {
        const Cell next = step(shelf, move);
        if (grid.inBounds(next) && grid.at(next) == CellType::Free) {
            cells.push_back(next);
        }
    }
    return cells;
}

} // namespace

Scenario generateScenario(const GeneratorSettings& settings)
{
    validateSettings(settings);

    SeededRandom random(settings.seed);
    Scenario scenario;
    scenario.seed = settings.seed;
    scenario.grid = Grid(settings.width, settings.height, CellType::Free);
    Grid& grid = scenario.grid;

    addBorderWalls(grid);
    const std::vector<Cell> stations = addStations(grid);
    addShelves(grid, settings.shelfDensity, random);

    // Shelves that a robot can actually reach, in row-major order.
    std::vector<Cell> usableShelves;
    for (int i = 0; i < grid.cellCount(); ++i) {
        const Cell cell = grid.cellAt(i);
        if (grid.at(cell) == CellType::Shelf && !pickupCellsFor(grid, cell).empty()) {
            usableShelves.push_back(cell);
        }
    }
    if (usableShelves.empty()) {
        throw std::runtime_error("no shelf can be reached");
    }

    // Robots start in the free top row.
    std::vector<Cell> starts;
    for (int x = 1; x <= grid.width() - 2; ++x) {
        starts.push_back({x, 1});
    }
    random.shuffle(starts);
    random.shuffle(usableShelves);

    for (int i = 0; i < settings.robotCount; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        RobotTask task;
        task.start = starts[index];
        // Different shelves while there are enough of them.
        task.shelf = usableShelves[index % usableShelves.size()];
        const std::vector<Cell> pickups = pickupCellsFor(grid, task.shelf);
        task.pickup = pickups[random.below(static_cast<std::uint32_t>(pickups.size()))];
        task.station = stations[random.below(static_cast<std::uint32_t>(stations.size()))];
        scenario.robots.push_back(task);
    }
    return scenario;
}

} // namespace wh

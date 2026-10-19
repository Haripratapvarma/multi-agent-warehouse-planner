#include "warehouse/search.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <vector>

namespace wh {

const char* toString(SearchStatus status)
{
    switch (status) {
    case SearchStatus::Found: return "Found";
    case SearchStatus::NoPath: return "NoPath";
    case SearchStatus::InvalidInput: return "InvalidInput";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& out, SearchStatus status)
{
    return out << toString(status);
}

namespace {

constexpr int kUnvisited = -1;

// Follows parent links back from the goal and returns start ... goal.
std::vector<Cell> buildPath(const Grid& grid, const std::vector<int>& parent, int startIndex, int goalIndex)
{
    std::vector<Cell> path;
    for (int i = goalIndex; i != startIndex; i = parent[static_cast<std::size_t>(i)]) {
        path.push_back(grid.cellAt(i));
    }
    path.push_back(grid.cellAt(startIndex));
    std::reverse(path.begin(), path.end());
    return path;
}

bool validEndpoints(const Grid& grid, Cell start, Cell goal)
{
    return grid.isPassable(start) && grid.isPassable(goal);
}

// An entry in the A* open list.
struct OpenNode {
    int f;     // g + h: estimated total path length through this cell
    int g;     // moves from the start so far
    int index; // cell index
};

// Orders the priority queue so the smallest f comes out first.
// Ties prefer the larger g (closer to the goal), then the smaller index.
// Every tie is broken, so the pop order is the same on every compiler.
struct WorseThan {
    bool operator()(const OpenNode& a, const OpenNode& b) const
    {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        if (a.g != b.g) {
            return a.g < b.g;
        }
        return a.index > b.index;
    }
};

} // namespace

PathResult findPathAStar(const Grid& grid, Cell start, Cell goal)
{
    PathResult result;
    if (!validEndpoints(grid, start, goal)) {
        result.status = SearchStatus::InvalidInput;
        return result;
    }

    const std::size_t cellCount = static_cast<std::size_t>(grid.cellCount());
    std::vector<int> bestG(cellCount, std::numeric_limits<int>::max());
    std::vector<int> parent(cellCount, kUnvisited);
    std::vector<bool> closed(cellCount, false);
    std::priority_queue<OpenNode, std::vector<OpenNode>, WorseThan> open;

    const int startIndex = grid.index(start);
    const int goalIndex = grid.index(goal);
    bestG[static_cast<std::size_t>(startIndex)] = 0;
    open.push({manhattanDistance(start, goal), 0, startIndex});

    while (!open.empty()) {
        const OpenNode node = open.top();
        open.pop();
        if (closed[static_cast<std::size_t>(node.index)]) {
            continue; // an older, worse copy of a cell we already expanded
        }
        closed[static_cast<std::size_t>(node.index)] = true;
        ++result.expandedNodes;

        if (node.index == goalIndex) {
            result.status = SearchStatus::Found;
            result.path = buildPath(grid, parent, startIndex, goalIndex);
            return result;
        }

        const Cell current = grid.cellAt(node.index);
        for (Cell move : kMoves) {
            const Cell next = step(current, move);
            if (!grid.isPassable(next)) {
                continue;
            }
            const std::size_t nextIndex = static_cast<std::size_t>(grid.index(next));
            const int g = node.g + 1;
            if (g < bestG[nextIndex]) {
                bestG[nextIndex] = g;
                parent[nextIndex] = node.index;
                open.push({g + manhattanDistance(next, goal), g, grid.index(next)});
            }
        }
    }

    result.status = SearchStatus::NoPath;
    return result;
}

PathResult findPathBfs(const Grid& grid, Cell start, Cell goal)
{
    PathResult result;
    if (!validEndpoints(grid, start, goal)) {
        result.status = SearchStatus::InvalidInput;
        return result;
    }

    const std::size_t cellCount = static_cast<std::size_t>(grid.cellCount());
    std::vector<int> parent(cellCount, kUnvisited);
    std::vector<bool> seen(cellCount, false);
    std::queue<int> open;

    const int startIndex = grid.index(start);
    const int goalIndex = grid.index(goal);
    seen[static_cast<std::size_t>(startIndex)] = true;
    open.push(startIndex);

    while (!open.empty()) {
        const int index = open.front();
        open.pop();
        ++result.expandedNodes;

        if (index == goalIndex) {
            result.status = SearchStatus::Found;
            result.path = buildPath(grid, parent, startIndex, goalIndex);
            return result;
        }

        const Cell current = grid.cellAt(index);
        for (Cell move : kMoves) {
            const Cell next = step(current, move);
            if (!grid.isPassable(next)) {
                continue;
            }
            const std::size_t nextIndex = static_cast<std::size_t>(grid.index(next));
            if (!seen[nextIndex]) {
                seen[nextIndex] = true;
                parent[nextIndex] = index;
                open.push(grid.index(next));
            }
        }
    }

    result.status = SearchStatus::NoPath;
    return result;
}

std::vector<int> distanceMap(const Grid& grid, Cell goal)
{
    std::vector<int> distance(static_cast<std::size_t>(grid.cellCount()), -1);
    if (!grid.isPassable(goal)) {
        return distance;
    }
    std::queue<Cell> open;
    distance[static_cast<std::size_t>(grid.index(goal))] = 0;
    open.push(goal);
    while (!open.empty()) {
        const Cell current = open.front();
        open.pop();
        const int nextDistance = distance[static_cast<std::size_t>(grid.index(current))] + 1;
        for (Cell move : kMoves) {
            const Cell next = step(current, move);
            if (grid.isPassable(next) && distance[static_cast<std::size_t>(grid.index(next))] < 0) {
                distance[static_cast<std::size_t>(grid.index(next))] = nextDistance;
                open.push(next);
            }
        }
    }
    return distance;
}

} // namespace wh

#pragma once

#include "warehouse/grid.hpp"

#include <ostream>
#include <vector>

namespace wh {

enum class SearchStatus {
    Found,        // path holds a shortest path
    NoPath,       // the goal cannot be reached
    InvalidInput, // start or goal is outside the grid or not passable
};

const char* toString(SearchStatus status);
std::ostream& operator<<(std::ostream& out, SearchStatus status);

struct PathResult {
    SearchStatus status = SearchStatus::NoPath;
    std::vector<Cell> path; // start ... goal when Found, otherwise empty
    int expandedNodes = 0;  // cells taken off the open list; shows search effort

    bool found() const { return status == SearchStatus::Found; }
    // Number of moves, or -1 when no path was found.
    int length() const { return found() ? static_cast<int>(path.size()) - 1 : -1; }
};

// Single-robot A* search with the Manhattan distance heuristic.
// Every move costs 1, so the heuristic never overestimates and the
// returned path is a shortest one.
PathResult findPathAStar(const Grid& grid, Cell start, Cell goal);

// Breadth-first search. Used as a reference: on a grid where every move
// costs 1 it also returns a shortest path, just with more work.
PathResult findPathBfs(const Grid& grid, Cell start, Cell goal);

// Distance (number of moves) from every cell to `goal`, found with one
// backward BFS. Indexed by grid.index(cell); -1 where the goal cannot be
// reached or the cell is not passable. Ignores other robots.
std::vector<int> distanceMap(const Grid& grid, Cell goal);

} // namespace wh

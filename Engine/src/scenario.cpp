#include "warehouse/scenario.hpp"

#include <set>
#include <sstream>

namespace wh {

namespace {

std::string describe(std::size_t robot, const std::string& problem)
{
    std::ostringstream out;
    out << "robot " << robot << ": " << problem;
    return out.str();
}

} // namespace

std::string validateScenario(const Scenario& scenario)
{
    const Grid& grid = scenario.grid;
    if (grid.cellCount() == 0) {
        return "grid is empty";
    }
    if (scenario.robots.empty()) {
        return "scenario has no robots";
    }

    std::set<Cell> starts;
    for (std::size_t i = 0; i < scenario.robots.size(); ++i) {
        const RobotTask& task = scenario.robots[i];

        if (!grid.isPassable(task.start)) {
            return describe(i, "start cell is not a free cell inside the grid");
        }
        if (!starts.insert(task.start).second) {
            return describe(i, "start cell is shared with another robot");
        }
        if (!grid.inBounds(task.shelf) || grid.at(task.shelf) != CellType::Shelf) {
            return describe(i, "shelf cell is not a shelf");
        }
        if (!grid.inBounds(task.pickup) || grid.at(task.pickup) != CellType::Free) {
            return describe(i, "pickup cell is not a free cell");
        }
        if (manhattanDistance(task.pickup, task.shelf) != 1) {
            return describe(i, "pickup cell is not next to its shelf");
        }
        if (!grid.inBounds(task.station) || grid.at(task.station) != CellType::Station) {
            return describe(i, "station cell is not a packing station");
        }
    }
    return {};
}

} // namespace wh

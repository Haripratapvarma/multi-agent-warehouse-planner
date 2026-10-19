#pragma once

#include "warehouse/grid.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wh {

// One robot's job: drive from start to the pickup cell next to its shelf,
// then on to its packing station.
struct RobotTask {
    Cell start;
    Cell shelf;   // the shelf being picked from (an obstacle)
    Cell pickup;  // free cell next to the shelf where the robot stops
    Cell station; // packing station where the robot delivers

    friend bool operator==(const RobotTask&, const RobotTask&) = default;
};

struct Scenario {
    std::uint32_t seed = 0; // 0 for hand-made scenarios
    Grid grid;
    std::vector<RobotTask> robots;

    friend bool operator==(const Scenario&, const Scenario&) = default;
};

// Checks that a scenario makes sense: every task cell has the right type,
// pickups are next to their shelves and start cells are all different.
// Returns an empty string when valid, otherwise a description of the first problem.
std::string validateScenario(const Scenario& scenario);

} // namespace wh

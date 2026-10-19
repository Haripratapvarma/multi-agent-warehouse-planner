#pragma once

#include "warehouse/scenario.hpp"

#include <cstdint>

namespace wh {

struct GeneratorSettings {
    std::uint32_t seed = 1;
    int width = 20;
    int height = 12;
    int robotCount = 4;
    double shelfDensity = 0.3; // fraction of rack cells that hold a shelf, 0.0 - 0.9
};

// Builds a warehouse layout and robot tasks from the settings.
//
// Layout: walls around the edge, a free row at the top where robots start,
// racks of shelves separated by aisles in the middle, and packing stations
// along the bottom. Every passable cell is reachable from every other one.
//
// The result depends only on the settings: the same settings always give
// exactly the same scenario, on any platform.
// Throws std::invalid_argument for settings outside the supported range.
Scenario generateScenario(const GeneratorSettings& settings);

} // namespace wh

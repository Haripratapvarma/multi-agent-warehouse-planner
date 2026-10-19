#include "test_framework.hpp"

#include "warehouse/generator.hpp"
#include "warehouse/scenario_io.hpp"
#include "warehouse/search.hpp"

#include <set>
#include <stdexcept>

using namespace wh;

namespace {

GeneratorSettings settings(std::uint32_t seed, int width, int height, int robots, double density)
{
    GeneratorSettings s;
    s.seed = seed;
    s.width = width;
    s.height = height;
    s.robotCount = robots;
    s.shelfDensity = density;
    return s;
}

} // namespace

TEST_CASE(same_seed_gives_identical_scenario)
{
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        const GeneratorSettings s = settings(seed, 30, 20, 10, 0.4);
        const Scenario a = generateScenario(s);
        const Scenario b = generateScenario(s);
        CHECK(a == b);
        CHECK_EQ(scenarioToString(a), scenarioToString(b));
    }
}

TEST_CASE(different_seeds_give_different_scenarios)
{
    const Scenario a = generateScenario(settings(1, 30, 20, 10, 0.4));
    const Scenario b = generateScenario(settings(2, 30, 20, 10, 0.4));
    CHECK(!(a == b));
}

TEST_CASE(seed_is_stored_in_the_scenario)
{
    CHECK_EQ(generateScenario(settings(1234, 12, 8, 3, 0.3)).seed, 1234u);
}

TEST_CASE(generated_scenarios_are_valid)
{
    const int sizes[][2] = {{5, 6}, {12, 8}, {20, 12}, {30, 20}, {40, 25}};
    for (const auto& size : sizes) {
        for (std::uint32_t seed = 1; seed <= 40; ++seed) {
            const int robots = size[0] - 2 < 8 ? size[0] - 2 : 8;
            const Scenario s = generateScenario(settings(seed, size[0], size[1], robots, 0.5));
            CHECK_EQ(validateScenario(s), std::string());
            CHECK_EQ(static_cast<int>(s.robots.size()), robots);
        }
    }
}

TEST_CASE(border_is_all_walls)
{
    const Scenario s = generateScenario(settings(9, 15, 10, 4, 0.3));
    const Grid& g = s.grid;
    for (int x = 0; x < g.width(); ++x) {
        CHECK(g.at({x, 0}) == CellType::Wall);
        CHECK(g.at({x, g.height() - 1}) == CellType::Wall);
    }
    for (int y = 0; y < g.height(); ++y) {
        CHECK(g.at({0, y}) == CellType::Wall);
        CHECK(g.at({g.width() - 1, y}) == CellType::Wall);
    }
}

TEST_CASE(every_task_can_be_reached)
{
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        const Scenario s = generateScenario(settings(seed, 20, 12, 6, 0.7));
        for (const RobotTask& task : s.robots) {
            CHECK(findPathBfs(s.grid, task.start, task.pickup).found());
            CHECK(findPathBfs(s.grid, task.pickup, task.station).found());
        }
    }
}

TEST_CASE(shelf_density_changes_shelf_count)
{
    auto countShelves = [](const Scenario& s) {
        int count = 0;
        for (int i = 0; i < s.grid.cellCount(); ++i) {
            count += s.grid.at(s.grid.cellAt(i)) == CellType::Shelf ? 1 : 0;
        }
        return count;
    };
    const int sparse = countShelves(generateScenario(settings(5, 30, 20, 5, 0.1)));
    const int dense = countShelves(generateScenario(settings(5, 30, 20, 5, 0.6)));
    CHECK(sparse >= 1);
    CHECK(dense > sparse);
}

TEST_CASE(start_cells_are_all_different)
{
    const Scenario s = generateScenario(settings(3, 14, 9, 12, 0.3));
    std::set<Cell> starts;
    for (const RobotTask& task : s.robots) {
        starts.insert(task.start);
    }
    CHECK_EQ(starts.size(), s.robots.size());
}

TEST_CASE(invalid_settings_throw)
{
    CHECK_THROWS(generateScenario(settings(1, 4, 10, 1, 0.3)), std::invalid_argument);   // too narrow
    CHECK_THROWS(generateScenario(settings(1, 10, 5, 1, 0.3)), std::invalid_argument);   // too short
    CHECK_THROWS(generateScenario(settings(1, 101, 10, 1, 0.3)), std::invalid_argument); // too wide
    CHECK_THROWS(generateScenario(settings(1, 10, 10, 0, 0.3)), std::invalid_argument);  // no robots
    CHECK_THROWS(generateScenario(settings(1, 10, 10, 9, 0.3)), std::invalid_argument);  // more robots than start cells
    CHECK_THROWS(generateScenario(settings(1, 10, 10, 2, -0.1)), std::invalid_argument);
    CHECK_THROWS(generateScenario(settings(1, 10, 10, 2, 0.95)), std::invalid_argument);
}

int main() { return wh::test::runAll(); }

#include "test_framework.hpp"

#include "warehouse/generator.hpp"
#include "warehouse/scenario_io.hpp"
#include "warehouse/search.hpp"

#include <filesystem>
#include <string>

using namespace wh;

namespace {

const std::filesystem::path kScenarioDir = WH_SCENARIO_DIR;

const char* kSmallText =
    "# a comment line\n"
    "warehouse-scenario 1\n"
    "seed 0\n"
    "size 5 4\n"
    "map\n"
    "#####\n"
    "#.S.#\n"
    "#..P#\n"
    "#####\n"
    "end\n"
    "robots 1\n"
    "robot start 1 1 shelf 2 1 pickup 3 1 station 3 2\n";

// Returns the error line number, or -1 if the text loaded without error.
int errorLine(const std::string& text)
{
    try {
        scenarioFromString(text);
    } catch (const ScenarioFormatError& e) {
        return e.line();
    }
    return -1;
}

std::string replace(std::string text, const std::string& from, const std::string& to)
{
    const auto pos = text.find(from);
    if (pos != std::string::npos) {
        text.replace(pos, from.size(), to);
    }
    return text;
}

} // namespace

TEST_CASE(reads_a_small_scenario)
{
    const Scenario s = scenarioFromString(kSmallText);
    CHECK_EQ(s.grid.width(), 5);
    CHECK_EQ(s.grid.height(), 4);
    CHECK(s.grid.at({2, 1}) == CellType::Shelf);
    CHECK(s.grid.at({3, 2}) == CellType::Station);
    REQUIRE(s.robots.size() == 1);
    CHECK_EQ(s.robots[0].start, (Cell{1, 1}));
    CHECK_EQ(s.robots[0].pickup, (Cell{3, 1}));
}

TEST_CASE(windows_line_endings_load_the_same)
{
    std::string crlf;
    for (const char* p = kSmallText; *p != '\0'; ++p) {
        if (*p == '\n') {
            crlf += '\r';
        }
        crlf += *p;
    }
    CHECK(scenarioFromString(crlf) == scenarioFromString(kSmallText));
}

TEST_CASE(save_and_load_round_trip)
{
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        GeneratorSettings settings;
        settings.seed = seed;
        settings.width = 25;
        settings.height = 15;
        settings.robotCount = 7;
        const Scenario original = generateScenario(settings);
        const Scenario loaded = scenarioFromString(scenarioToString(original));
        CHECK(loaded == original);
    }
}

TEST_CASE(file_round_trip)
{
    GeneratorSettings settings;
    settings.seed = 99;
    const Scenario original = generateScenario(settings);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "wh_test_round_trip.txt";
    saveScenarioFile(path, original);
    const Scenario loaded = loadScenarioFile(path);
    std::filesystem::remove(path);
    CHECK(loaded == original);
}

TEST_CASE(errors_report_the_line_number)
{
    CHECK_EQ(errorLine(kSmallText), -1);
    CHECK_EQ(errorLine(replace(kSmallText, "warehouse-scenario 1", "something-else 1")), 2);
    CHECK_EQ(errorLine(replace(kSmallText, "size 5 4", "size five 4")), 4);
    CHECK_EQ(errorLine(replace(kSmallText, "#..P#", "#..P")), 8);  // short map row
    CHECK_EQ(errorLine(replace(kSmallText, "#..P#", "#..Q#")), 8);  // unknown character
    CHECK_EQ(errorLine(replace(kSmallText, "robots 1", "robots 2")), 12); // file ends early
    CHECK_EQ(errorLine(replace(kSmallText, "pickup 3 1", "pickip 3 1")), 12);
}

TEST_CASE(invalid_tasks_are_rejected)
{
    // Pickup (1,2) is not next to the shelf at (2,1).
    CHECK_EQ(errorLine(replace(kSmallText, "pickup 3 1", "pickup 1 2")), 0);
    // Station must be a 'P' cell.
    CHECK_EQ(errorLine(replace(kSmallText, "station 3 2", "station 2 2")), 0);
}

TEST_CASE(missing_file_throws)
{
    CHECK_THROWS(loadScenarioFile(kScenarioDir / "does_not_exist.txt"), std::runtime_error);
}

TEST_CASE(example_scenario_files_load_and_are_solvable)
{
    const char* files[] = {"narrow_corridor.txt", "small_warehouse.txt", "medium_warehouse.txt"};
    for (const char* name : files) {
        const Scenario s = loadScenarioFile(kScenarioDir / name);
        CHECK(!s.robots.empty());
        for (const RobotTask& task : s.robots) {
            CHECK(findPathAStar(s.grid, task.start, task.pickup).found());
            CHECK(findPathAStar(s.grid, task.pickup, task.station).found());
        }
    }
}

TEST_CASE(generated_example_files_match_their_seed)
{
    // These two files were saved by wh_demo; regenerating them from the
    // same settings must give exactly the same scenario.
    struct Example {
        const char* file;
        GeneratorSettings settings;
    };
    const Example examples[] = {
        {"small_warehouse.txt", {7, 12, 8, 3, 0.3}},
        {"medium_warehouse.txt", {42, 30, 20, 10, 0.4}},
    };
    for (const Example& example : examples) {
        CHECK(loadScenarioFile(kScenarioDir / example.file) == generateScenario(example.settings));
    }
}

int main() { return wh::test::runAll(); }

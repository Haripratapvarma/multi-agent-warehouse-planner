// Tests the DLL only through its C interface, the same way the C# wrapper
// uses it: status codes, error messages, buffers and freeing.

#include "test_framework.hpp"

#include "warehouse/c_api.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

const std::filesystem::path kScenarioDir = WH_SCENARIO_DIR;

std::string lastError()
{
    int32_t required = 0;
    wh_last_error(nullptr, 0, &required);
    std::string text(static_cast<std::size_t>(required), '\0');
    wh_last_error(text.data(), required, &required);
    text.resize(static_cast<std::size_t>(required - 1));
    return text;
}

WhScenario* generate(uint32_t seed, int32_t width, int32_t height, int32_t robots)
{
    const WhGeneratorSettings settings{seed, width, height, robots, 0.3};
    WhScenario* scenario = nullptr;
    REQUIRE(wh_scenario_generate(&settings, &scenario) == WH_OK);
    return scenario;
}

std::string pathString(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace

TEST_CASE(version_and_struct_sizes)
{
    CHECK_EQ(wh_api_version(), WH_API_VERSION);
    CHECK_EQ(wh_struct_size(WH_STRUCT_CELL), 8);
    CHECK_EQ(wh_struct_size(WH_STRUCT_GENERATOR_SETTINGS), 24);
    CHECK_EQ(wh_struct_size(WH_STRUCT_ROBOT_TASK), 32);
    CHECK_EQ(wh_struct_size(WH_STRUCT_SCENARIO_INFO), 16);
    CHECK_EQ(wh_struct_size(WH_STRUCT_PLAN_METRICS), 40);
    CHECK_EQ(wh_struct_size(WH_STRUCT_ROBOT_PLAN_INFO), 16);
    CHECK_EQ(wh_struct_size(99), -1);
}

TEST_CASE(generate_read_back_and_free)
{
    const int32_t before = wh_live_object_count();
    WhScenario* scenario = generate(42, 20, 12, 5);
    CHECK_EQ(wh_live_object_count(), before + 1);

    WhScenarioInfo info{};
    REQUIRE(wh_scenario_get_info(scenario, &info) == WH_OK);
    CHECK_EQ(info.seed, 42u);
    CHECK_EQ(info.width, 20);
    CHECK_EQ(info.height, 12);
    CHECK_EQ(info.robotCount, 5);

    std::vector<uint8_t> cells(20 * 12);
    REQUIRE(wh_scenario_get_cells(scenario, cells.data(), static_cast<int32_t>(cells.size())) == WH_OK);
    CHECK_EQ(cells[0], WH_CELL_WALL); // corner

    std::vector<WhRobotTask> robots(5);
    REQUIRE(wh_scenario_get_robots(scenario, robots.data(), 5) == WH_OK);
    CHECK_EQ(robots[0].start.y, 1); // robots start in the top row

    wh_scenario_free(scenario);
    CHECK_EQ(wh_live_object_count(), before);
}

TEST_CASE(create_from_data_round_trips)
{
    WhScenario* original = generate(7, 12, 8, 3);
    WhScenarioInfo info{};
    wh_scenario_get_info(original, &info);
    std::vector<uint8_t> cells(static_cast<std::size_t>(info.width * info.height));
    std::vector<WhRobotTask> robots(static_cast<std::size_t>(info.robotCount));
    wh_scenario_get_cells(original, cells.data(), static_cast<int32_t>(cells.size()));
    wh_scenario_get_robots(original, robots.data(), info.robotCount);

    WhScenario* copy = nullptr;
    REQUIRE(wh_scenario_create(info.seed, info.width, info.height, cells.data(), robots.data(), info.robotCount,
                               &copy) == WH_OK);

    // Plans of the original and the copy must be identical.
    WhPlan* a = nullptr;
    WhPlan* b = nullptr;
    REQUIRE(wh_plan(original, WH_PLANNER_ASTAR_MANHATTAN, WH_ORDER_INPUT, 0, &a) == WH_OK);
    REQUIRE(wh_plan(copy, WH_PLANNER_ASTAR_MANHATTAN, WH_ORDER_INPUT, 0, &b) == WH_OK);
    WhPlanMetrics ma{};
    WhPlanMetrics mb{};
    wh_plan_get_metrics(a, &ma);
    wh_plan_get_metrics(b, &mb);
    CHECK_EQ(ma.makespan, mb.makespan);
    CHECK_EQ(ma.totalPathLength, mb.totalPathLength);
    CHECK_EQ(ma.expandedNodes, mb.expandedNodes);

    wh_plan_free(a);
    wh_plan_free(b);
    wh_scenario_free(copy);
    wh_scenario_free(original);
}

TEST_CASE(create_rejects_inconsistent_data)
{
    // 3x3 room: pickup (1,1) is not next to the shelf at (0,0).
    const uint8_t cells[9] = {WH_CELL_SHELF, WH_CELL_FREE, WH_CELL_FREE,    WH_CELL_FREE, WH_CELL_FREE,
                              WH_CELL_FREE,  WH_CELL_FREE, WH_CELL_FREE, WH_CELL_STATION};
    const WhRobotTask robot{{0, 2}, {0, 0}, {1, 1}, {2, 2}};
    WhScenario* scenario = reinterpret_cast<WhScenario*>(1); // must be reset to NULL
    CHECK_EQ(wh_scenario_create(0, 3, 3, cells, &robot, 1, &scenario), WH_ERROR_INVALID_ARGUMENT);
    CHECK(scenario == nullptr);
    CHECK(lastError().find("pickup cell is not next to its shelf") != std::string::npos);

    const uint8_t badCell[9] = {7, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK_EQ(wh_scenario_create(0, 3, 3, badCell, &robot, 1, &scenario), WH_ERROR_INVALID_ARGUMENT);
    CHECK(lastError().find("unknown cell value") != std::string::npos);
}

TEST_CASE(plan_reports_paths_metrics_and_order)
{
    WhScenario* scenario = nullptr;
    REQUIRE(wh_scenario_load(pathString(kScenarioDir / "narrow_corridor.txt").c_str(), &scenario) == WH_OK);

    WhPlan* plan = nullptr;
    REQUIRE(wh_plan(scenario, WH_PLANNER_ASTAR_MANHATTAN, WH_ORDER_INPUT, 0, &plan) == WH_OK);

    WhPlanMetrics metrics{};
    REQUIRE(wh_plan_get_metrics(plan, &metrics) == WH_OK);
    CHECK_EQ(metrics.status, WH_PLAN_SUCCESS);
    CHECK_EQ(metrics.success, 1);
    CHECK_EQ(metrics.makespan, 22);
    CHECK_EQ(metrics.totalPathLength, 35);
    CHECK_EQ(metrics.waits, 5);
    CHECK_EQ(metrics.failedRobot, -1);
    CHECK(metrics.runtimeMs >= 0.0);
    CHECK(metrics.expandedNodes > 0);

    int32_t count = 0;
    REQUIRE(wh_plan_get_robot_count(plan, &count) == WH_OK);
    REQUIRE(count == 2);

    std::vector<std::vector<WhCell>> paths;
    std::vector<int32_t> lengths;
    for (int32_t robot = 0; robot < count; ++robot) {
        WhRobotPlanInfo info{};
        REQUIRE(wh_plan_get_robot_info(plan, robot, &info) == WH_OK);
        CHECK_EQ(info.planned, 1);
        CHECK_EQ(info.pathLength, info.finishTime + 1);
        std::vector<WhCell> path(static_cast<std::size_t>(info.pathLength));
        REQUIRE(wh_plan_get_path(plan, robot, path.data(), info.pathLength) == WH_OK);
        paths.push_back(path);
        lengths.push_back(info.pathLength);
    }
    CHECK_EQ(paths[0].front().x, 1); // robot 0 starts at (1,2)
    CHECK_EQ(paths[0].front().y, 2);

    int32_t order[2] = {-1, -1};
    int32_t written = 0;
    REQUIRE(wh_plan_get_order(plan, order, 2, &written) == WH_OK);
    CHECK_EQ(written, 2);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(order[1], 1);

    char message[8] = "x";
    int32_t required = 0;
    REQUIRE(wh_plan_get_message(plan, message, 8, &required) == WH_OK);
    CHECK_EQ(std::string(message), std::string());
    CHECK_EQ(required, 1);

    // The returned routes pass the validator through the C interface too.
    std::vector<WhCell> flat;
    for (const auto& path : paths) {
        flat.insert(flat.end(), path.begin(), path.end());
    }
    int32_t valid = 0;
    REQUIRE(wh_validate_paths(scenario, flat.data(), lengths.data(), count, &valid, nullptr, 0, nullptr) == WH_OK);
    CHECK_EQ(valid, 1);

    // Make the robots crash: robot 1 never waits, so the validator objects.
    std::vector<WhCell> crash = paths[0];
    std::vector<WhCell> straight;
    for (int x = 11; x >= 2; --x) {
        straight.push_back({x, 2});
    }
    straight.push_back({2, 1});
    straight.push_back({2, 2});
    straight.push_back({1, 2});
    straight.push_back({1, 3});
    crash.insert(crash.end(), straight.begin(), straight.end());
    const int32_t crashLengths[2] = {lengths[0], static_cast<int32_t>(straight.size())};
    char reason[200] = {};
    REQUIRE(wh_validate_paths(scenario, crash.data(), crashLengths, 2, &valid, reason, 200, &required) == WH_OK);
    CHECK_EQ(valid, 0);
    CHECK(std::string(reason).find("robot 0 and robot 1") != std::string::npos);

    wh_plan_free(plan);
    wh_scenario_free(scenario);
}

TEST_CASE(failed_planning_is_not_an_error)
{
    WhScenario* scenario = generate(1, 30, 20, 5);
    WhPlan* plan = nullptr;
    // A time limit of 5 steps is far too short.
    REQUIRE(wh_plan(scenario, WH_PLANNER_BFS, WH_ORDER_LONGEST_FIRST, 5, &plan) == WH_OK);
    WhPlanMetrics metrics{};
    wh_plan_get_metrics(plan, &metrics);
    CHECK_EQ(metrics.status, WH_PLAN_NO_PATH);
    CHECK_EQ(metrics.success, 0);
    CHECK(metrics.failedRobot >= 0);

    int32_t required = 0;
    CHECK_EQ(wh_plan_get_message(plan, nullptr, 0, &required), WH_ERROR_BUFFER_TOO_SMALL);
    std::string message(static_cast<std::size_t>(required), '\0');
    REQUIRE(wh_plan_get_message(plan, message.data(), required, &required) == WH_OK);
    CHECK(message.find("within 5 time steps") != std::string::npos);

    // Robots after the failed one have no route, which is reported, not crashed on.
    int32_t written = 0;
    int32_t order[5] = {};
    wh_plan_get_order(plan, order, 5, &written);
    CHECK_EQ(written, 5);

    wh_plan_free(plan);
    wh_scenario_free(scenario);
}

TEST_CASE(bad_arguments_return_status_codes)
{
    WhScenario* scenario = generate(3, 12, 8, 2);
    WhPlan* plan = nullptr;

    CHECK_EQ(wh_scenario_generate(nullptr, &scenario), WH_ERROR_INVALID_ARGUMENT);
    CHECK_EQ(wh_plan(nullptr, 0, 0, 0, &plan), WH_ERROR_INVALID_ARGUMENT);
    CHECK_EQ(wh_plan(scenario, 9, 0, 0, &plan), WH_ERROR_INVALID_ARGUMENT);
    CHECK(lastError().find("unknown planner") != std::string::npos);
    CHECK_EQ(wh_plan(scenario, 0, 5, 0, &plan), WH_ERROR_INVALID_ARGUMENT);
    CHECK_EQ(wh_plan(scenario, 0, 0, -1, &plan), WH_ERROR_INVALID_ARGUMENT);
    CHECK(plan == nullptr);

    REQUIRE(wh_plan(scenario, 0, 0, 0, &plan) == WH_OK);
    WhRobotPlanInfo info{};
    CHECK_EQ(wh_plan_get_robot_info(plan, 2, &info), WH_ERROR_OUT_OF_RANGE);
    CHECK_EQ(wh_plan_get_robot_info(plan, -1, &info), WH_ERROR_OUT_OF_RANGE);
    CHECK(lastError().find("out of range") != std::string::npos);

    WhCell tooSmall[1];
    CHECK_EQ(wh_plan_get_path(plan, 0, tooSmall, 1), WH_ERROR_BUFFER_TOO_SMALL);
    uint8_t cells[4];
    CHECK_EQ(wh_scenario_get_cells(scenario, cells, 4), WH_ERROR_BUFFER_TOO_SMALL);

    // Freeing NULL is allowed.
    wh_plan_free(nullptr);
    wh_scenario_free(nullptr);

    wh_plan_free(plan);
    wh_scenario_free(scenario);
}

TEST_CASE(engine_exceptions_become_status_codes)
{
    // The generator throws std::invalid_argument for these settings; the
    // exception must stop at the boundary and come back as a code + message.
    const WhGeneratorSettings tooNarrow{1, 3, 10, 1, 0.3};
    WhScenario* scenario = nullptr;
    CHECK_EQ(wh_scenario_generate(&tooNarrow, &scenario), WH_ERROR_INVALID_ARGUMENT);
    CHECK(scenario == nullptr);
    CHECK(lastError().find("width must be between") != std::string::npos);
}

TEST_CASE(file_errors)
{
    WhScenario* scenario = nullptr;
    const std::string missing = pathString(kScenarioDir / "does_not_exist.txt");
    CHECK_EQ(wh_scenario_load(missing.c_str(), &scenario), WH_ERROR_IO);
    CHECK(lastError().find("cannot open") != std::string::npos);

    // A file that is not a scenario.
    const std::filesystem::path bad = std::filesystem::temp_directory_path() / "wh_c_api_bad.txt";
    {
        std::ofstream file(bad);
        REQUIRE(static_cast<bool>(file));
        file << "hello\n";
    }
    CHECK_EQ(wh_scenario_load(pathString(bad).c_str(), &scenario), WH_ERROR_FORMAT);
    CHECK(lastError().find("line 1") != std::string::npos);
    std::filesystem::remove(bad);
}

TEST_CASE(save_and_load_through_the_interface)
{
    WhScenario* original = generate(11, 15, 10, 4);
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "wh_c_api_round_trip.txt";
    REQUIRE(wh_scenario_save(original, pathString(file).c_str()) == WH_OK);

    WhScenario* loaded = nullptr;
    REQUIRE(wh_scenario_load(pathString(file).c_str(), &loaded) == WH_OK);
    std::filesystem::remove(file);

    std::vector<uint8_t> a(150);
    std::vector<uint8_t> b(150);
    wh_scenario_get_cells(original, a.data(), 150);
    wh_scenario_get_cells(loaded, b.data(), 150);
    CHECK(a == b);
    WhScenarioInfo info{};
    wh_scenario_get_info(loaded, &info);
    CHECK_EQ(info.seed, 11u);

    wh_scenario_free(loaded);
    wh_scenario_free(original);
}

TEST_CASE(last_error_is_per_thread_and_cleared_on_success)
{
    WhScenario* scenario = nullptr;
    CHECK_EQ(wh_scenario_generate(nullptr, &scenario), WH_ERROR_INVALID_ARGUMENT);
    const std::string mainError = lastError();
    CHECK(!mainError.empty());

    std::string otherError = "not set";
    std::thread other([&] {
        otherError = lastError(); // a fresh thread has no error
        WhPlan* plan = nullptr;
        wh_plan(nullptr, 0, 0, 0, &plan); // sets an error on this thread only
    });
    other.join();
    CHECK_EQ(otherError, std::string());
    CHECK_EQ(lastError(), mainError); // unchanged by the other thread

    WhScenario* ok = generate(1, 12, 8, 1);
    CHECK_EQ(lastError(), std::string()); // a successful call clears it
    wh_scenario_free(ok);
}

TEST_CASE(last_error_reports_needed_size)
{
    WhScenario* scenario = nullptr;
    wh_scenario_generate(nullptr, &scenario);
    int32_t required = 0;
    char small[4] = {};
    CHECK_EQ(wh_last_error(small, 4, &required), WH_ERROR_BUFFER_TOO_SMALL);
    CHECK_EQ(required, static_cast<int32_t>(std::string("settings is null").size() + 1));
    CHECK_EQ(std::string(small), std::string("set")); // truncated but still terminated
}

TEST_CASE(planning_from_many_threads_at_once)
{
    WhScenario* scenario = generate(5, 30, 20, 10);
    WhPlan* reference = nullptr;
    REQUIRE(wh_plan(scenario, WH_PLANNER_ASTAR_TRUE_DISTANCE, WH_ORDER_LONGEST_FIRST, 0, &reference) == WH_OK);
    WhPlanMetrics expected{};
    wh_plan_get_metrics(reference, &expected);

    std::vector<int> matches(4, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 5; ++i) {
                WhPlan* plan = nullptr;
                if (wh_plan(scenario, WH_PLANNER_ASTAR_TRUE_DISTANCE, WH_ORDER_LONGEST_FIRST, 0, &plan) != WH_OK) {
                    continue;
                }
                WhPlanMetrics m{};
                wh_plan_get_metrics(plan, &m);
                matches[static_cast<std::size_t>(t)] +=
                    (m.makespan == expected.makespan && m.expandedNodes == expected.expandedNodes) ? 1 : 0;
                wh_plan_free(plan);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (int count : matches) {
        CHECK_EQ(count, 5);
    }
    wh_plan_free(reference);
    wh_scenario_free(scenario);
}

int main() { return wh::test::runAll(); }

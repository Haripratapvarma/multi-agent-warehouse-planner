// Implementation of the C interface. Each function converts between the
// plain C structs and the engine's C++ types, and catches every exception so
// nothing ever crosses the DLL boundary.

#include "warehouse/c_api.h"

#include "warehouse/generator.hpp"
#include "warehouse/planner.hpp"
#include "warehouse/scenario_io.hpp"

#include <atomic>
#include <cstring>
#include <exception>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// The opaque handle types simply wrap the C++ objects.
struct WhScenario {
    wh::Scenario scenario;
};

struct WhPlan {
    wh::PlanResult result;
};

namespace {

thread_local std::string lastError;
std::atomic<int> liveObjects{0};

// Lets a C++ function report a specific status code through an exception.
class ApiError : public std::runtime_error {
public:
    ApiError(WhStatus status, const std::string& message) : std::runtime_error(message), status_(status) {}
    WhStatus status() const { return status_; }

private:
    WhStatus status_;
};

// Runs `body`, turning every exception into a status code plus a message.
template <typename Body>
int32_t guarded(Body&& body)
{
    try {
        body();
        lastError.clear();
        return WH_OK;
    } catch (const ApiError& e) {
        lastError = e.what();
        return e.status();
    } catch (const wh::ScenarioFormatError& e) {
        lastError = e.what();
        return WH_ERROR_FORMAT;
    } catch (const std::invalid_argument& e) {
        lastError = e.what();
        return WH_ERROR_INVALID_ARGUMENT;
    } catch (const std::out_of_range& e) {
        lastError = e.what();
        return WH_ERROR_OUT_OF_RANGE;
    } catch (const std::bad_alloc&) {
        lastError = "out of memory";
        return WH_ERROR_INTERNAL;
    } catch (const std::exception& e) {
        lastError = e.what();
        return WH_ERROR_INTERNAL;
    } catch (...) {
        lastError = "unknown error";
        return WH_ERROR_INTERNAL;
    }
}

void require(bool condition, const char* message)
{
    if (!condition) {
        throw ApiError(WH_ERROR_INVALID_ARGUMENT, message);
    }
}

// Writes a string into a caller buffer, always zero-terminated.
void copyString(const std::string& text, char* buffer, int32_t capacity, int32_t* required)
{
    const auto needed = static_cast<int32_t>(text.size() + 1);
    if (required != nullptr) {
        *required = needed;
    }
    if (buffer == nullptr || capacity <= 0) {
        if (capacity > 0) {
            throw ApiError(WH_ERROR_INVALID_ARGUMENT, "buffer is null");
        }
        throw ApiError(WH_ERROR_BUFFER_TOO_SMALL, "string buffer is too small");
    }
    if (capacity < needed) {
        std::memcpy(buffer, text.data(), static_cast<std::size_t>(capacity - 1));
        buffer[capacity - 1] = '\0';
        throw ApiError(WH_ERROR_BUFFER_TOO_SMALL, "string buffer is too small");
    }
    std::memcpy(buffer, text.c_str(), static_cast<std::size_t>(needed));
}

wh::Cell toCell(WhCell c) { return {c.x, c.y}; }
WhCell fromCell(wh::Cell c) { return {c.x, c.y}; }

wh::RobotTask toTask(const WhRobotTask& t)
{
    return {toCell(t.start), toCell(t.shelf), toCell(t.pickup), toCell(t.station)};
}

WhRobotTask fromTask(const wh::RobotTask& t)
{
    return {fromCell(t.start), fromCell(t.shelf), fromCell(t.pickup), fromCell(t.station)};
}

uint8_t fromCellType(wh::CellType type)
{
    switch (type) {
    case wh::CellType::Free: return WH_CELL_FREE;
    case wh::CellType::Wall: return WH_CELL_WALL;
    case wh::CellType::Shelf: return WH_CELL_SHELF;
    case wh::CellType::Station: return WH_CELL_STATION;
    }
    return WH_CELL_WALL;
}

wh::CellType toCellType(uint8_t value)
{
    switch (value) {
    case WH_CELL_FREE: return wh::CellType::Free;
    case WH_CELL_WALL: return wh::CellType::Wall;
    case WH_CELL_SHELF: return wh::CellType::Shelf;
    case WH_CELL_STATION: return wh::CellType::Station;
    default: throw ApiError(WH_ERROR_INVALID_ARGUMENT, "unknown cell value " + std::to_string(value));
    }
}

// Hands a new object to the caller and counts it as live.
template <typename T>
void publish(T* object, T** out)
{
    *out = object;
    liveObjects.fetch_add(1);
}

const wh::RobotPlan& robotPlan(const WhPlan* plan, int32_t robot)
{
    require(plan != nullptr, "plan is null");
    if (robot < 0 || static_cast<std::size_t>(robot) >= plan->result.robots.size()) {
        throw ApiError(WH_ERROR_OUT_OF_RANGE, "robot index " + std::to_string(robot) + " is out of range");
    }
    return plan->result.robots[static_cast<std::size_t>(robot)];
}

} // namespace

// The C# side checks these same numbers; keep them in sync with c_api.h.
static_assert(sizeof(WhCell) == 8);
static_assert(sizeof(WhGeneratorSettings) == 24);
static_assert(sizeof(WhRobotTask) == 32);
static_assert(sizeof(WhScenarioInfo) == 16);
static_assert(sizeof(WhPlanMetrics) == 40);
static_assert(sizeof(WhRobotPlanInfo) == 16);

extern "C" {

int32_t wh_api_version(void) { return WH_API_VERSION; }

int32_t wh_struct_size(int32_t structId)
{
    switch (structId) {
    case WH_STRUCT_CELL: return sizeof(WhCell);
    case WH_STRUCT_GENERATOR_SETTINGS: return sizeof(WhGeneratorSettings);
    case WH_STRUCT_ROBOT_TASK: return sizeof(WhRobotTask);
    case WH_STRUCT_SCENARIO_INFO: return sizeof(WhScenarioInfo);
    case WH_STRUCT_PLAN_METRICS: return sizeof(WhPlanMetrics);
    case WH_STRUCT_ROBOT_PLAN_INFO: return sizeof(WhRobotPlanInfo);
    default: return -1;
    }
}

int32_t wh_last_error(char* buffer, int32_t capacity, int32_t* required)
{
    // Must not overwrite the error it is reporting, so it does not use guarded().
    const auto needed = static_cast<int32_t>(lastError.size() + 1);
    if (required != nullptr) {
        *required = needed;
    }
    if (buffer == nullptr || capacity < needed) {
        if (buffer != nullptr && capacity > 0) {
            std::memcpy(buffer, lastError.data(), static_cast<std::size_t>(capacity - 1));
            buffer[capacity - 1] = '\0';
        }
        return WH_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, lastError.c_str(), static_cast<std::size_t>(needed));
    return WH_OK;
}

int32_t wh_live_object_count(void) { return liveObjects.load(); }

int32_t wh_scenario_generate(const WhGeneratorSettings* settings, WhScenario** out)
{
    return guarded([&] {
        require(settings != nullptr, "settings is null");
        require(out != nullptr, "out is null");
        *out = nullptr;
        wh::GeneratorSettings s;
        s.seed = settings->seed;
        s.width = settings->width;
        s.height = settings->height;
        s.robotCount = settings->robotCount;
        s.shelfDensity = settings->shelfDensity;
        publish(new WhScenario{wh::generateScenario(s)}, out);
    });
}

int32_t wh_scenario_create(uint32_t seed, int32_t width, int32_t height, const uint8_t* cells,
                           const WhRobotTask* robots, int32_t robotCount, WhScenario** out)
{
    return guarded([&] {
        require(out != nullptr, "out is null");
        *out = nullptr;
        require(width > 0 && height > 0 && width <= 1000 && height <= 1000, "width and height must be 1-1000");
        require(cells != nullptr, "cells is null");
        require(robotCount >= 0, "robot count cannot be negative");
        require(robots != nullptr || robotCount == 0, "robots is null");

        wh::Scenario scenario;
        scenario.seed = seed;
        scenario.grid = wh::Grid(width, height);
        for (int i = 0; i < width * height; ++i) {
            scenario.grid.set(scenario.grid.cellAt(i), toCellType(cells[i]));
        }
        for (int32_t i = 0; i < robotCount; ++i) {
            scenario.robots.push_back(toTask(robots[i]));
        }
        const std::string problem = wh::validateScenario(scenario);
        if (!problem.empty()) {
            throw ApiError(WH_ERROR_INVALID_ARGUMENT, "invalid scenario: " + problem);
        }
        publish(new WhScenario{std::move(scenario)}, out);
    });
}

int32_t wh_scenario_load(const char* utf8Path, WhScenario** out)
{
    return guarded([&] {
        require(utf8Path != nullptr, "path is null");
        require(out != nullptr, "out is null");
        *out = nullptr;
        // u8path keeps non-English characters in Windows paths working.
        const auto path = std::filesystem::path(reinterpret_cast<const char8_t*>(utf8Path));
        wh::Scenario scenario;
        try {
            scenario = wh::loadScenarioFile(path);
        } catch (const wh::ScenarioFormatError&) {
            throw;
        } catch (const std::runtime_error& e) {
            throw ApiError(WH_ERROR_IO, e.what());
        }
        publish(new WhScenario{std::move(scenario)}, out);
    });
}

int32_t wh_scenario_save(const WhScenario* scenario, const char* utf8Path)
{
    return guarded([&] {
        require(scenario != nullptr, "scenario is null");
        require(utf8Path != nullptr, "path is null");
        const auto path = std::filesystem::path(reinterpret_cast<const char8_t*>(utf8Path));
        try {
            wh::saveScenarioFile(path, scenario->scenario);
        } catch (const std::runtime_error& e) {
            throw ApiError(WH_ERROR_IO, e.what());
        }
    });
}

void wh_scenario_free(WhScenario* scenario)
{
    if (scenario != nullptr) {
        delete scenario;
        liveObjects.fetch_sub(1);
    }
}

int32_t wh_scenario_get_info(const WhScenario* scenario, WhScenarioInfo* out)
{
    return guarded([&] {
        require(scenario != nullptr, "scenario is null");
        require(out != nullptr, "out is null");
        const wh::Scenario& s = scenario->scenario;
        *out = {s.seed, s.grid.width(), s.grid.height(), static_cast<int32_t>(s.robots.size())};
    });
}

int32_t wh_scenario_get_cells(const WhScenario* scenario, uint8_t* buffer, int32_t capacity)
{
    return guarded([&] {
        require(scenario != nullptr, "scenario is null");
        require(buffer != nullptr, "buffer is null");
        const wh::Grid& grid = scenario->scenario.grid;
        if (capacity < grid.cellCount()) {
            throw ApiError(WH_ERROR_BUFFER_TOO_SMALL, "cell buffer is too small");
        }
        for (int i = 0; i < grid.cellCount(); ++i) {
            buffer[i] = fromCellType(grid.at(grid.cellAt(i)));
        }
    });
}

int32_t wh_scenario_get_robots(const WhScenario* scenario, WhRobotTask* buffer, int32_t capacity)
{
    return guarded([&] {
        require(scenario != nullptr, "scenario is null");
        const auto& robots = scenario->scenario.robots;
        require(buffer != nullptr || robots.empty(), "buffer is null");
        if (capacity < static_cast<int32_t>(robots.size())) {
            throw ApiError(WH_ERROR_BUFFER_TOO_SMALL, "robot buffer is too small");
        }
        for (std::size_t i = 0; i < robots.size(); ++i) {
            buffer[i] = fromTask(robots[i]);
        }
    });
}

int32_t wh_plan(const WhScenario* scenario, int32_t planner, int32_t order, int32_t maxTime, WhPlan** out)
{
    return guarded([&] {
        require(scenario != nullptr, "scenario is null");
        require(out != nullptr, "out is null");
        *out = nullptr;
        require(planner >= WH_PLANNER_ASTAR_MANHATTAN && planner <= WH_PLANNER_BFS, "unknown planner");
        require(order == WH_ORDER_INPUT || order == WH_ORDER_LONGEST_FIRST, "unknown robot order");
        require(maxTime >= 0, "max time cannot be negative");

        wh::PlanOptions options;
        options.planner = static_cast<wh::PlannerKind>(planner);
        options.order = static_cast<wh::RobotOrder>(order);
        options.maxTime = maxTime;
        publish(new WhPlan{wh::planScenario(scenario->scenario, options)}, out);
    });
}

void wh_plan_free(WhPlan* plan)
{
    if (plan != nullptr) {
        delete plan;
        liveObjects.fetch_sub(1);
    }
}

int32_t wh_plan_get_metrics(const WhPlan* plan, WhPlanMetrics* out)
{
    return guarded([&] {
        require(plan != nullptr, "plan is null");
        require(out != nullptr, "out is null");
        const wh::PlanResult& r = plan->result;
        const wh::PlanMetrics& m = r.metrics;
        int32_t status = WH_PLAN_SUCCESS;
        if (r.status == wh::PlanStatus::NoPath) {
            status = WH_PLAN_NO_PATH;
        } else if (r.status == wh::PlanStatus::InvalidScenario) {
            status = WH_PLAN_INVALID_SCENARIO;
        }
        *out = {status,       m.success ? 1 : 0, m.makespan,  m.totalPathLength,
                m.waits,      r.failedRobot,     m.runtimeMs, m.expandedNodes};
    });
}

int32_t wh_plan_get_message(const WhPlan* plan, char* buffer, int32_t capacity, int32_t* required)
{
    return guarded([&] {
        require(plan != nullptr, "plan is null");
        copyString(plan->result.message, buffer, capacity, required);
    });
}

int32_t wh_plan_get_robot_count(const WhPlan* plan, int32_t* out)
{
    return guarded([&] {
        require(plan != nullptr, "plan is null");
        require(out != nullptr, "out is null");
        *out = static_cast<int32_t>(plan->result.robots.size());
    });
}

int32_t wh_plan_get_robot_info(const WhPlan* plan, int32_t robot, WhRobotPlanInfo* out)
{
    return guarded([&] {
        const wh::RobotPlan& r = robotPlan(plan, robot);
        require(out != nullptr, "out is null");
        *out = {r.planned ? 1 : 0, r.pickupTime, r.finishTime, static_cast<int32_t>(r.path.size())};
    });
}

int32_t wh_plan_get_path(const WhPlan* plan, int32_t robot, WhCell* buffer, int32_t capacity)
{
    return guarded([&] {
        const wh::RobotPlan& r = robotPlan(plan, robot);
        require(buffer != nullptr || r.path.empty(), "buffer is null");
        if (capacity < static_cast<int32_t>(r.path.size())) {
            throw ApiError(WH_ERROR_BUFFER_TOO_SMALL, "path buffer is too small");
        }
        for (std::size_t i = 0; i < r.path.size(); ++i) {
            buffer[i] = fromCell(r.path[i]);
        }
    });
}

int32_t wh_plan_get_order(const WhPlan* plan, int32_t* buffer, int32_t capacity, int32_t* written)
{
    return guarded([&] {
        require(plan != nullptr, "plan is null");
        const std::vector<int>& order = plan->result.planningOrder;
        if (written != nullptr) {
            *written = static_cast<int32_t>(order.size());
        }
        require(buffer != nullptr || order.empty(), "buffer is null");
        if (capacity < static_cast<int32_t>(order.size())) {
            throw ApiError(WH_ERROR_BUFFER_TOO_SMALL, "order buffer is too small");
        }
        for (std::size_t i = 0; i < order.size(); ++i) {
            buffer[i] = order[i];
        }
    });
}

int32_t wh_validate_paths(const WhScenario* scenario, const WhCell* cells, const int32_t* pathLengths,
                          int32_t robotCount, int32_t* valid, char* message, int32_t capacity, int32_t* required)
{
    return guarded([&] {
        require(scenario != nullptr, "scenario is null");
        require(valid != nullptr, "valid is null");
        require(robotCount >= 0, "robot count cannot be negative");
        require(pathLengths != nullptr || robotCount == 0, "path lengths is null");

        std::vector<std::vector<wh::Cell>> paths(static_cast<std::size_t>(robotCount));
        std::size_t offset = 0;
        for (int32_t i = 0; i < robotCount; ++i) {
            require(pathLengths[i] >= 0, "path length cannot be negative");
            require(cells != nullptr || pathLengths[i] == 0, "cells is null");
            for (int32_t j = 0; j < pathLengths[i]; ++j) {
                paths[static_cast<std::size_t>(i)].push_back(toCell(cells[offset++]));
            }
        }
        const wh::ValidationResult result = wh::validatePlan(scenario->scenario, paths);
        *valid = result.valid ? 1 : 0;
        if (required != nullptr) {
            *required = static_cast<int32_t>(result.message.size() + 1);
        }
        if (message != nullptr) {
            copyString(result.message, message, capacity, required);
        }
    });
}

} // extern "C"

/*
 * C interface of the warehouse engine DLL.
 *
 * Rules every function follows:
 *   - Returns a WhStatus code. WH_OK (0) means success.
 *   - Never lets a C++ exception escape. On failure, a message describing the
 *     problem is stored for the calling thread; read it with wh_last_error.
 *   - Objects created by the DLL (scenarios, plans) must be released with the
 *     matching wh_..._free function. Passing NULL to a free function is fine.
 *   - Strings are UTF-8. Output strings use a caller-provided buffer; the
 *     number of bytes needed (including the terminating zero) is reported
 *     through `required` so the caller can retry with a bigger buffer.
 *   - All structs contain only 32/64-bit numbers so they have the same layout
 *     in C and C#. wh_struct_size lets the C# side check this.
 *   - Functions are safe to call from several threads at once, as long as one
 *     object is not freed while another thread is using it.
 */
#ifndef WAREHOUSE_C_API_H
#define WAREHOUSE_C_API_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(WH_BUILDING_DLL)
#    define WH_API __declspec(dllexport)
#  else
#    define WH_API __declspec(dllimport)
#  endif
#else
#  define WH_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define WH_API_VERSION 1

typedef enum WhStatus {
    WH_OK = 0,
    WH_ERROR_INVALID_ARGUMENT = 1, /* NULL pointer, bad setting, invalid scenario */
    WH_ERROR_OUT_OF_RANGE = 2,     /* robot index or similar outside the valid range */
    WH_ERROR_BUFFER_TOO_SMALL = 3, /* output buffer too small; see `required` */
    WH_ERROR_IO = 4,               /* a file could not be opened, read or written */
    WH_ERROR_FORMAT = 5,           /* a scenario file is not in the expected format */
    WH_ERROR_INTERNAL = 6          /* unexpected error inside the engine */
} WhStatus;

/* Cell values used in cell arrays (one byte per cell, row-major). */
enum { WH_CELL_FREE = 0, WH_CELL_WALL = 1, WH_CELL_SHELF = 2, WH_CELL_STATION = 3 };

/* Planner and ordering values for wh_plan. */
enum { WH_PLANNER_ASTAR_MANHATTAN = 0, WH_PLANNER_ASTAR_TRUE_DISTANCE = 1, WH_PLANNER_BFS = 2 };
enum { WH_ORDER_INPUT = 0, WH_ORDER_LONGEST_FIRST = 1 };

/* Plan status values reported in WhPlanMetrics.status. */
enum { WH_PLAN_SUCCESS = 0, WH_PLAN_NO_PATH = 1, WH_PLAN_INVALID_SCENARIO = 2 };

typedef struct WhCell {
    int32_t x;
    int32_t y;
} WhCell; /* 8 bytes */

typedef struct WhGeneratorSettings {
    uint32_t seed;
    int32_t width;
    int32_t height;
    int32_t robotCount;
    double shelfDensity;
} WhGeneratorSettings; /* 24 bytes */

typedef struct WhRobotTask {
    WhCell start;
    WhCell shelf;
    WhCell pickup;
    WhCell station;
} WhRobotTask; /* 32 bytes */

typedef struct WhScenarioInfo {
    uint32_t seed;
    int32_t width;
    int32_t height;
    int32_t robotCount;
} WhScenarioInfo; /* 16 bytes */

typedef struct WhPlanMetrics {
    int32_t status;          /* WH_PLAN_... */
    int32_t success;         /* 1 when every robot has a route */
    int32_t makespan;        /* time step when the last robot finishes */
    int32_t totalPathLength; /* steps of all robots, waits included */
    int32_t waits;
    int32_t failedRobot;     /* robot that could not be planned, or -1 */
    double runtimeMs;
    int64_t expandedNodes;
} WhPlanMetrics; /* 40 bytes */

typedef struct WhRobotPlanInfo {
    int32_t planned;    /* 1 when this robot has a route */
    int32_t pickupTime;
    int32_t finishTime;
    int32_t pathLength; /* number of cells in the route (finishTime + 1) */
} WhRobotPlanInfo; /* 16 bytes */

/* Struct ids for wh_struct_size. */
enum {
    WH_STRUCT_CELL = 0,
    WH_STRUCT_GENERATOR_SETTINGS = 1,
    WH_STRUCT_ROBOT_TASK = 2,
    WH_STRUCT_SCENARIO_INFO = 3,
    WH_STRUCT_PLAN_METRICS = 4,
    WH_STRUCT_ROBOT_PLAN_INFO = 5
};

typedef struct WhScenario WhScenario; /* opaque */
typedef struct WhPlan WhPlan;         /* opaque */

/* ---- General ------------------------------------------------------------ */

WH_API int32_t wh_api_version(void);
/* Size in bytes of one of the structs above, or -1 for an unknown id. */
WH_API int32_t wh_struct_size(int32_t structId);
/* Last error message of the calling thread (empty if none). */
WH_API int32_t wh_last_error(char* buffer, int32_t capacity, int32_t* required);
/* Number of scenarios and plans not yet freed. Used by tests to prove that
   the C# side always releases native memory. */
WH_API int32_t wh_live_object_count(void);

/* ---- Scenarios ---------------------------------------------------------- */

WH_API int32_t wh_scenario_generate(const WhGeneratorSettings* settings, WhScenario** out);
/* Builds a scenario from plain data: width * height cells (WH_CELL_...,
   row-major) and robotCount tasks. The scenario is checked for consistency. */
WH_API int32_t wh_scenario_create(uint32_t seed, int32_t width, int32_t height, const uint8_t* cells,
                                  const WhRobotTask* robots, int32_t robotCount, WhScenario** out);
WH_API int32_t wh_scenario_load(const char* utf8Path, WhScenario** out);
WH_API int32_t wh_scenario_save(const WhScenario* scenario, const char* utf8Path);
WH_API void wh_scenario_free(WhScenario* scenario);

WH_API int32_t wh_scenario_get_info(const WhScenario* scenario, WhScenarioInfo* out);
/* Copies width * height cell bytes. */
WH_API int32_t wh_scenario_get_cells(const WhScenario* scenario, uint8_t* buffer, int32_t capacity);
/* Copies robotCount tasks. */
WH_API int32_t wh_scenario_get_robots(const WhScenario* scenario, WhRobotTask* buffer, int32_t capacity);

/* ---- Planning ----------------------------------------------------------- */

/* Plans every robot. A planning failure (a robot with no route) is NOT an
   error: the call returns WH_OK and the metrics report the failure. maxTime
   0 means width * height * 4. */
WH_API int32_t wh_plan(const WhScenario* scenario, int32_t planner, int32_t order, int32_t maxTime, WhPlan** out);
WH_API void wh_plan_free(WhPlan* plan);

WH_API int32_t wh_plan_get_metrics(const WhPlan* plan, WhPlanMetrics* out);
/* Explanation when planning failed, empty on success. */
WH_API int32_t wh_plan_get_message(const WhPlan* plan, char* buffer, int32_t capacity, int32_t* required);
WH_API int32_t wh_plan_get_robot_count(const WhPlan* plan, int32_t* out);
WH_API int32_t wh_plan_get_robot_info(const WhPlan* plan, int32_t robot, WhRobotPlanInfo* out);
/* Copies the robot's route; capacity must be at least pathLength. */
WH_API int32_t wh_plan_get_path(const WhPlan* plan, int32_t robot, WhCell* buffer, int32_t capacity);
/* Robot indices in the order they were planned; copies robotCount values
   (fewer if planning stopped early, see `written`). */
WH_API int32_t wh_plan_get_order(const WhPlan* plan, int32_t* buffer, int32_t capacity, int32_t* written);

/* ---- Validation --------------------------------------------------------- */

/* Checks any set of routes against the scenario's rules. Routes are passed
   back to back in `cells`; pathLengths[i] is the number of cells of robot i.
   *valid is set to 1 or 0. When invalid, the reason is copied to `message`
   if it is not NULL; `required` (may be NULL) gets the size it needs. */
WH_API int32_t wh_validate_paths(const WhScenario* scenario, const WhCell* cells, const int32_t* pathLengths,
                                 int32_t robotCount, int32_t* valid, char* message, int32_t capacity,
                                 int32_t* required);

#ifdef __cplusplus
}
#endif

#endif /* WAREHOUSE_C_API_H */

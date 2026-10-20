using System.Runtime.InteropServices;

namespace Warehouse.Interop.Native;

/// <summary>
/// P/Invoke declarations for the C interface in c_api.h. The LibraryImport
/// source generator writes the marshalling code at compile time.
/// "warehouse_engine" resolves to warehouse_engine.dll on Windows and
/// libwarehouse_engine.so on Linux.
/// </summary>
internal static unsafe partial class NativeMethods
{
    private const string Library = "warehouse_engine";

    public const int ExpectedApiVersion = 1;

    // ---- General ----

    [LibraryImport(Library)]
    public static partial int wh_api_version();

    [LibraryImport(Library)]
    public static partial int wh_struct_size(int structId);

    [LibraryImport(Library)]
    public static partial int wh_last_error(byte* buffer, int capacity, out int required);

    [LibraryImport(Library)]
    public static partial int wh_live_object_count();

    // ---- Scenarios ----

    [LibraryImport(Library)]
    public static partial int wh_scenario_generate(in WhGeneratorSettings settings, out ScenarioSafeHandle scenario);

    [LibraryImport(Library)]
    public static partial int wh_scenario_create(uint seed, int width, int height, byte* cells, WhRobotTask* robots,
        int robotCount, out ScenarioSafeHandle scenario);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int wh_scenario_load(string utf8Path, out ScenarioSafeHandle scenario);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int wh_scenario_save(ScenarioSafeHandle scenario, string utf8Path);

    // Takes the raw pointer: only called from ScenarioSafeHandle.ReleaseHandle.
    [LibraryImport(Library)]
    public static partial void wh_scenario_free(IntPtr scenario);

    [LibraryImport(Library)]
    public static partial int wh_scenario_get_info(ScenarioSafeHandle scenario, out WhScenarioInfo info);

    [LibraryImport(Library)]
    public static partial int wh_scenario_get_cells(ScenarioSafeHandle scenario, byte* buffer, int capacity);

    [LibraryImport(Library)]
    public static partial int wh_scenario_get_robots(ScenarioSafeHandle scenario, WhRobotTask* buffer, int capacity);

    // ---- Planning ----

    [LibraryImport(Library)]
    public static partial int wh_plan(ScenarioSafeHandle scenario, int planner, int order, int maxTime,
        out PlanSafeHandle plan);

    [LibraryImport(Library)]
    public static partial void wh_plan_free(IntPtr plan);

    [LibraryImport(Library)]
    public static partial int wh_plan_get_metrics(PlanSafeHandle plan, out WhPlanMetrics metrics);

    [LibraryImport(Library)]
    public static partial int wh_plan_get_message(PlanSafeHandle plan, byte* buffer, int capacity, out int required);

    [LibraryImport(Library)]
    public static partial int wh_plan_get_robot_count(PlanSafeHandle plan, out int count);

    [LibraryImport(Library)]
    public static partial int wh_plan_get_robot_info(PlanSafeHandle plan, int robot, out WhRobotPlanInfo info);

    [LibraryImport(Library)]
    public static partial int wh_plan_get_path(PlanSafeHandle plan, int robot, WhCell* buffer, int capacity);

    [LibraryImport(Library)]
    public static partial int wh_plan_get_order(PlanSafeHandle plan, int* buffer, int capacity, out int written);

    // ---- Validation ----

    [LibraryImport(Library)]
    public static partial int wh_validate_paths(ScenarioSafeHandle scenario, WhCell* cells, int* pathLengths,
        int robotCount, out int valid, byte* message, int capacity, out int required);
}

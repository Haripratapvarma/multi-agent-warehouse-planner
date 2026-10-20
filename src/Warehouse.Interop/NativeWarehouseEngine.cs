using System.Text;
using Warehouse.Interop.Native;

namespace Warehouse.Interop;

/// <summary>
/// <see cref="IWarehouseEngine"/> backed by the C++ engine DLL.
///
/// Every call creates the native objects it needs, copies the results into
/// plain C# objects and frees the native objects again (through SafeHandles),
/// so no native memory outlives a call. The native engine has no shared
/// state, so one instance can be used from many threads at once.
/// </summary>
public sealed class NativeWarehouseEngine : IWarehouseEngine
{
    public NativeWarehouseEngine()
    {
        NativeLayout.Verify();
    }

    public WarehouseScenario Generate(GeneratorOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var settings = new WhGeneratorSettings
        {
            Seed = options.Seed,
            Width = options.Width,
            Height = options.Height,
            RobotCount = options.RobotCount,
            ShelfDensity = options.ShelfDensity,
        };
        int status = NativeMethods.wh_scenario_generate(in settings, out ScenarioSafeHandle handle);
        using (handle)
        {
            NativeErrors.Check(status);
            return ReadScenario(handle);
        }
    }

    public PlanOutcome Plan(WarehouseScenario scenario, PlannerType planner, RobotOrdering ordering, int maxTime = 0)
    {
        ArgumentNullException.ThrowIfNull(scenario);
        using ScenarioSafeHandle native = CreateNativeScenario(scenario);
        int status = NativeMethods.wh_plan(native, (int)planner, (int)ordering, maxTime, out PlanSafeHandle plan);
        using (plan)
        {
            NativeErrors.Check(status);
            return ReadPlan(plan);
        }
    }

    public unsafe PlanValidation Validate(WarehouseScenario scenario, IReadOnlyList<IReadOnlyList<GridPosition>> paths)
    {
        ArgumentNullException.ThrowIfNull(scenario);
        ArgumentNullException.ThrowIfNull(paths);

        WhCell[] cells = paths.SelectMany(path => path).Select(p => new WhCell(p.X, p.Y)).ToArray();
        int[] lengths = paths.Select(path => path.Count).ToArray();

        using ScenarioSafeHandle native = CreateNativeScenario(scenario);
        fixed (WhCell* cellPointer = cells)
        fixed (int* lengthPointer = lengths)
        {
            NativeErrors.Check(NativeMethods.wh_validate_paths(native, cellPointer, lengthPointer, lengths.Length,
                out int valid, null, 0, out int required));
            if (valid == 1)
            {
                return new PlanValidation(true, string.Empty);
            }

            byte[] message = new byte[required];
            fixed (byte* messagePointer = message)
            {
                NativeErrors.Check(NativeMethods.wh_validate_paths(native, cellPointer, lengthPointer, lengths.Length,
                    out valid, messagePointer, message.Length, out required));
            }
            return new PlanValidation(false, NativeErrors.DecodeUtf8(message));
        }
    }

    public WarehouseScenario LoadScenario(string path)
    {
        ArgumentException.ThrowIfNullOrEmpty(path);
        int status = NativeMethods.wh_scenario_load(Path.GetFullPath(path), out ScenarioSafeHandle handle);
        using (handle)
        {
            NativeErrors.Check(status);
            return ReadScenario(handle);
        }
    }

    public void SaveScenario(WarehouseScenario scenario, string path)
    {
        ArgumentNullException.ThrowIfNull(scenario);
        ArgumentException.ThrowIfNullOrEmpty(path);
        using ScenarioSafeHandle native = CreateNativeScenario(scenario);
        NativeErrors.Check(NativeMethods.wh_scenario_save(native, Path.GetFullPath(path)));
    }

    /// <summary>Number of native scenarios and plans not yet freed (for tests).</summary>
    internal static int LiveNativeObjects => NativeMethods.wh_live_object_count();

    // ---- Conversions between C# objects and native objects ----

    internal static unsafe ScenarioSafeHandle CreateNativeScenario(WarehouseScenario scenario)
    {
        byte[] cells = scenario.Cells.Select(c => (byte)c).ToArray();
        WhRobotTask[] robots = scenario.Robots.Select(ToNative).ToArray();

        int status;
        ScenarioSafeHandle handle;
        fixed (byte* cellPointer = cells)
        fixed (WhRobotTask* robotPointer = robots)
        {
            status = NativeMethods.wh_scenario_create(scenario.Seed, scenario.Width, scenario.Height, cellPointer,
                robotPointer, robots.Length, out handle);
        }
        if (status != 0)
        {
            handle.Dispose();
            NativeErrors.Check(status);
        }
        return handle;
    }

    private static unsafe WarehouseScenario ReadScenario(ScenarioSafeHandle handle)
    {
        NativeErrors.Check(NativeMethods.wh_scenario_get_info(handle, out WhScenarioInfo info));

        byte[] cells = new byte[info.Width * info.Height];
        fixed (byte* pointer = cells)
        {
            NativeErrors.Check(NativeMethods.wh_scenario_get_cells(handle, pointer, cells.Length));
        }

        WhRobotTask[] robots = new WhRobotTask[info.RobotCount];
        fixed (WhRobotTask* pointer = robots)
        {
            NativeErrors.Check(NativeMethods.wh_scenario_get_robots(handle, pointer, robots.Length));
        }

        return new WarehouseScenario(info.Seed, info.Width, info.Height,
            cells.Select(c => (CellKind)c).ToArray(), robots.Select(FromNative).ToArray());
    }

    private static unsafe PlanOutcome ReadPlan(PlanSafeHandle plan)
    {
        NativeErrors.Check(NativeMethods.wh_plan_get_metrics(plan, out WhPlanMetrics m));
        NativeErrors.Check(NativeMethods.wh_plan_get_robot_count(plan, out int robotCount));

        var routes = new List<RobotRoute>(robotCount);
        for (int robot = 0; robot < robotCount; robot++)
        {
            NativeErrors.Check(NativeMethods.wh_plan_get_robot_info(plan, robot, out WhRobotPlanInfo info));
            WhCell[] path = new WhCell[info.PathLength];
            fixed (WhCell* pointer = path)
            {
                NativeErrors.Check(NativeMethods.wh_plan_get_path(plan, robot, pointer, path.Length));
            }
            routes.Add(new RobotRoute(info.Planned == 1, info.PickupTime, info.FinishTime,
                path.Select(c => new GridPosition(c.X, c.Y)).ToArray()));
        }

        int[] order = new int[robotCount];
        int written;
        fixed (int* pointer = order)
        {
            NativeErrors.Check(NativeMethods.wh_plan_get_order(plan, pointer, order.Length, out written));
        }

        var metrics = new PlanMetrics(m.Success == 1, m.Makespan, m.TotalPathLength, m.Waits, m.RuntimeMs,
            m.ExpandedNodes);
        return new PlanOutcome((PlanStatus)m.Status, ReadPlanMessage(plan), m.FailedRobot, metrics, routes,
            order.Take(written).ToArray());
    }

    private static unsafe string ReadPlanMessage(PlanSafeHandle plan)
    {
        // First call asks for the size, second call fills the buffer.
        NativeMethods.wh_plan_get_message(plan, null, 0, out int required);
        byte[] buffer = new byte[required];
        fixed (byte* pointer = buffer)
        {
            NativeErrors.Check(NativeMethods.wh_plan_get_message(plan, pointer, buffer.Length, out _));
        }
        return NativeErrors.DecodeUtf8(buffer);
    }

    private static WhRobotTask ToNative(RobotTask task) => new()
    {
        Start = new WhCell(task.Start.X, task.Start.Y),
        Shelf = new WhCell(task.Shelf.X, task.Shelf.Y),
        Pickup = new WhCell(task.Pickup.X, task.Pickup.Y),
        Station = new WhCell(task.Station.X, task.Station.Y),
    };

    private static RobotTask FromNative(WhRobotTask task) => new(
        new GridPosition(task.Start.X, task.Start.Y),
        new GridPosition(task.Shelf.X, task.Shelf.Y),
        new GridPosition(task.Pickup.X, task.Pickup.Y),
        new GridPosition(task.Station.X, task.Station.Y));
}

/// <summary>Turns native status codes into exceptions.</summary>
internal static class NativeErrors
{
    public static void Check(int status)
    {
        if (status != (int)EngineStatus.Ok)
        {
            throw new WarehouseEngineException((EngineStatus)status, LastError());
        }
    }

    /// <summary>The error message stored by the engine for the current thread.</summary>
    public static unsafe string LastError()
    {
        NativeMethods.wh_last_error(null, 0, out int required);
        byte[] buffer = new byte[required];
        fixed (byte* pointer = buffer)
        {
            NativeMethods.wh_last_error(pointer, buffer.Length, out _);
        }
        string message = DecodeUtf8(buffer);
        return message.Length > 0 ? message : "The warehouse engine reported an error.";
    }

    /// <summary>Decodes a zero-terminated UTF-8 buffer.</summary>
    public static string DecodeUtf8(byte[] buffer)
    {
        int length = Array.IndexOf(buffer, (byte)0);
        return Encoding.UTF8.GetString(buffer, 0, length < 0 ? buffer.Length : length);
    }
}

using System.Runtime.CompilerServices;
using Warehouse.Interop.Native;

namespace Warehouse.Interop.Tests;

/// <summary>Struct layout, error codes and native memory ownership.</summary>
public class ErrorAndMemoryTests
{
    private readonly NativeWarehouseEngine _engine = new();

    [Fact]
    public void StructSizes_MatchOnBothSides()
    {
        Assert.Equal(1, NativeMethods.wh_api_version());
        foreach ((NativeStructId id, int managedSize) in NativeLayout.ManagedSizes)
        {
            Assert.Equal(NativeMethods.wh_struct_size((int)id), managedSize);
        }
        // Also pin the expected numbers so a change on both sides is noticed.
        Assert.Equal(8, Unsafe.SizeOf<WhCell>());
        Assert.Equal(24, Unsafe.SizeOf<WhGeneratorSettings>());
        Assert.Equal(32, Unsafe.SizeOf<WhRobotTask>());
        Assert.Equal(16, Unsafe.SizeOf<WhScenarioInfo>());
        Assert.Equal(40, Unsafe.SizeOf<WhPlanMetrics>());
        Assert.Equal(16, Unsafe.SizeOf<WhRobotPlanInfo>());
    }

    [Fact]
    public void InvalidGeneratorSettings_BecomeException()
    {
        var error = Assert.Throws<WarehouseEngineException>(
            () => _engine.Generate(new GeneratorOptions(1, 3, 10, 1, 0.3)));

        Assert.Equal(EngineStatus.InvalidArgument, error.Status);
        Assert.Contains("width must be between", error.Message);
    }

    [Fact]
    public void InconsistentScenario_BecomesException()
    {
        var cells = Enumerable.Repeat(CellKind.Free, 9).ToArray();
        cells[0] = CellKind.Shelf;
        cells[8] = CellKind.Station;
        // Pickup (1,1) is not next to the shelf at (0,0).
        var scenario = new WarehouseScenario(0, 3, 3, cells, [new RobotTask(new(0, 2), new(0, 0), new(1, 1), new(2, 2))]);

        var error = Assert.Throws<WarehouseEngineException>(
            () => _engine.Plan(scenario, PlannerType.AStarManhattan, RobotOrdering.InputOrder));

        Assert.Equal(EngineStatus.InvalidArgument, error.Status);
        Assert.Contains("pickup cell is not next to its shelf", error.Message);
    }

    [Fact]
    public void UnknownPlanner_BecomesException()
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(1, 12, 8, 2, 0.3));

        var error = Assert.Throws<WarehouseEngineException>(
            () => _engine.Plan(scenario, (PlannerType)42, RobotOrdering.InputOrder));

        Assert.Equal(EngineStatus.InvalidArgument, error.Status);
        Assert.Contains("unknown planner", error.Message);
    }

    [Fact]
    public void MissingFile_BecomesIoError()
    {
        string missing = Path.Combine(Path.GetTempPath(), "wh_definitely_missing_file.txt");

        var error = Assert.Throws<WarehouseEngineException>(() => _engine.LoadScenario(missing));

        Assert.Equal(EngineStatus.IoError, error.Status);
        Assert.Contains("cannot open", error.Message);
    }

    [Fact]
    public void BadFile_BecomesFormatErrorWithLineNumber()
    {
        string file = Path.Combine(Path.GetTempPath(), $"wh_bad_{Guid.NewGuid():N}.txt");
        File.WriteAllText(file, "warehouse-scenario 1\nseed 1\nsize 3 three\n");
        try
        {
            var error = Assert.Throws<WarehouseEngineException>(() => _engine.LoadScenario(file));

            Assert.Equal(EngineStatus.FormatError, error.Status);
            Assert.Contains("line 3", error.Message);
        }
        finally
        {
            File.Delete(file);
        }
    }

    [Fact]
    public void ErrorMessage_DoesNotLeakIntoLaterCalls()
    {
        Assert.Throws<WarehouseEngineException>(() => _engine.Generate(new GeneratorOptions(1, 3, 10, 1, 0.3)));
        _engine.Generate(new GeneratorOptions(1, 12, 8, 1, 0.3)); // succeeds and clears the error

        Assert.Equal("The warehouse engine reported an error.", NativeErrors.LastError());
    }

    [Fact]
    public void EngineCalls_FreeAllNativeObjects()
    {
        int before = NativeWarehouseEngine.LiveNativeObjects;

        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(5, 20, 12, 6, 0.3));
        PlanOutcome plan = _engine.Plan(scenario, PlannerType.AStarManhattan, RobotOrdering.InputOrder);
        _engine.Validate(scenario, plan.Routes.Select(r => r.Path).ToList());
        Assert.Throws<WarehouseEngineException>(() => _engine.Generate(new GeneratorOptions(1, 3, 10, 1, 0.3)));

        Assert.Equal(before, NativeWarehouseEngine.LiveNativeObjects);
    }

    [Fact]
    public void SafeHandle_Dispose_FreesNativeObject()
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(5, 20, 12, 6, 0.3));
        int before = NativeWarehouseEngine.LiveNativeObjects;

        ScenarioSafeHandle handle = NativeWarehouseEngine.CreateNativeScenario(scenario);
        Assert.Equal(before + 1, NativeWarehouseEngine.LiveNativeObjects);
        Assert.False(handle.IsInvalid);

        handle.Dispose();
        Assert.Equal(before, NativeWarehouseEngine.LiveNativeObjects);
        Assert.True(handle.IsClosed);

        handle.Dispose(); // a second Dispose must not free twice
        Assert.Equal(before, NativeWarehouseEngine.LiveNativeObjects);
    }

    [Fact]
    public void SafeHandle_Finalizer_FreesForgottenObject()
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(5, 20, 12, 6, 0.3));
        int before = NativeWarehouseEngine.LiveNativeObjects;

        CreateAndForget(scenario);
        Assert.Equal(before + 1, NativeWarehouseEngine.LiveNativeObjects);

        for (int attempt = 0; attempt < 10 && NativeWarehouseEngine.LiveNativeObjects != before; attempt++)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
        Assert.Equal(before, NativeWarehouseEngine.LiveNativeObjects);
    }

    // Kept in its own method so no reference to the handle survives on the stack.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CreateAndForget(WarehouseScenario scenario)
    {
        _ = NativeWarehouseEngine.CreateNativeScenario(scenario);
    }

    [Fact]
    public async Task Engine_CanBeUsedFromManyThreads()
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(9, 30, 20, 10, 0.3));
        PlanOutcome expected = _engine.Plan(scenario, PlannerType.AStarTrueDistance, RobotOrdering.LongestTaskFirst);

        PlanOutcome[] results = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(
            () => _engine.Plan(scenario, PlannerType.AStarTrueDistance, RobotOrdering.LongestTaskFirst))));

        Assert.All(results, r =>
        {
            Assert.Equal(expected.Metrics.Makespan, r.Metrics.Makespan);
            Assert.Equal(expected.Metrics.ExpandedNodes, r.Metrics.ExpandedNodes);
        });
    }
}

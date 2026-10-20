using Warehouse.Interop;
using Warehouse.ViewModels.Services;

namespace Warehouse.ViewModels.Tests;

/// <summary>
/// Stand-in for the C++ engine. Generates an empty room with robots along
/// the top row and returns simple hand-made plans: robot i reaches its
/// pickup at t = 1 and its station at t = 2 + i, waiting once on the way.
/// </summary>
public sealed class FakeEngine : IWarehouseEngine
{
    // The compare screen calls the engine from many threads at once,
    // so every list is guarded by this lock.
    private readonly object _lock = new();
    private readonly List<GeneratorOptions> _generateCalls = [];
    private readonly List<(PlannerType Planner, RobotOrdering Ordering)> _planCalls = [];
    private readonly HashSet<int> _planThreadIds = [];

    public IReadOnlyList<GeneratorOptions> GenerateCalls { get { lock (_lock) return _generateCalls.ToList(); } }
    public IReadOnlyList<(PlannerType Planner, RobotOrdering Ordering)> PlanCalls { get { lock (_lock) return _planCalls.ToList(); } }
    public IReadOnlySet<int> PlanThreadIds { get { lock (_lock) return _planThreadIds.ToHashSet(); } }
    public int? PlanThreadId => PlanThreadIds.Count > 0 ? PlanThreadIds.First() : null;
    public List<(WarehouseScenario Scenario, string Path)> Saved { get; } = [];

    /// <summary>When set, Generate/Load/Plan throw this.</summary>
    public WarehouseEngineException? Failure { get; set; }

    /// <summary>When true, Plan reports that robot 0 could not be planned.</summary>
    public bool PlanFails { get; set; }

    /// <summary>When set, Plan waits for this before returning (to observe a run in progress).</summary>
    public TaskCompletionSource? PlanGate { get; set; }

    /// <summary>Completed when the first Plan call starts.</summary>
    public TaskCompletionSource PlanStarted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public WarehouseScenario? ScenarioToLoad { get; set; }

    public WarehouseScenario Generate(GeneratorOptions options)
    {
        lock (_lock)
        {
            _generateCalls.Add(options);
        }
        ThrowIfFailing();
        return MakeScenario(options.Seed, options.Width, options.Height, options.RobotCount);
    }

    public PlanOutcome Plan(WarehouseScenario scenario, PlannerType planner, RobotOrdering ordering, int maxTime = 0)
    {
        lock (_lock)
        {
            _planThreadIds.Add(Environment.CurrentManagedThreadId);
            _planCalls.Add((planner, ordering));
        }
        PlanStarted.TrySetResult();
        // Wait for the test to open the gate, but never forever: if the code
        // under test wrongly blocks the test's own thread, fail instead of hanging.
        if (PlanGate is { } gate && !gate.Task.Wait(TimeSpan.FromSeconds(10)))
        {
            throw new TimeoutException("The test never opened PlanGate.");
        }
        ThrowIfFailing();
        return PlanFails ? FailedPlan(scenario) : MakePlan(scenario);
    }

    public PlanValidation Validate(WarehouseScenario scenario, IReadOnlyList<IReadOnlyList<GridPosition>> paths) =>
        new(true, string.Empty);

    public WarehouseScenario LoadScenario(string path)
    {
        ThrowIfFailing();
        return ScenarioToLoad ?? throw new WarehouseEngineException(EngineStatus.IoError, $"cannot open '{path}'");
    }

    public void SaveScenario(WarehouseScenario scenario, string path)
    {
        ThrowIfFailing();
        Saved.Add((scenario, path));
    }

    private void ThrowIfFailing()
    {
        if (Failure is not null)
        {
            throw Failure;
        }
    }

    public static WarehouseScenario MakeScenario(uint seed, int width, int height, int robots)
    {
        var cells = Enumerable.Repeat(CellKind.Free, width * height).ToArray();
        var tasks = Enumerable.Range(0, robots)
            .Select(i => new RobotTask(new(i + 1, 1), new(i + 1, 3), new(i + 1, 2), new(i + 1, height - 2)))
            .ToArray();
        return new WarehouseScenario(seed, width, height, cells, tasks);
    }

    public static PlanOutcome MakePlan(WarehouseScenario scenario)
    {
        var routes = new List<RobotRoute>();
        for (int i = 0; i < scenario.Robots.Count; i++)
        {
            RobotTask task = scenario.Robots[i];
            // start -> pickup (t=1), then wait i times, then one step to "station".
            var path = new List<GridPosition> { task.Start, task.Pickup };
            for (int w = 0; w < i; w++)
            {
                path.Add(task.Pickup);
            }
            path.Add(task.Station);
            routes.Add(new RobotRoute(true, 1, path.Count - 1, path));
        }
        int makespan = routes.Max(r => r.FinishTime);
        var metrics = new PlanMetrics(true, makespan, routes.Sum(r => r.FinishTime), routes.Sum(r => r.FinishTime - 2),
            0.5, 100);
        return new PlanOutcome(PlanStatus.Success, string.Empty, -1, metrics, routes,
            Enumerable.Range(0, routes.Count).ToArray());
    }

    private static PlanOutcome FailedPlan(WarehouseScenario scenario)
    {
        var routes = scenario.Robots.Select(_ => new RobotRoute(false, -1, -1, [])).ToArray();
        return new PlanOutcome(PlanStatus.NoPath, "robot 0: no collision-free route to its pickup cell", 0,
            new PlanMetrics(false, 0, 0, 0, 0.1, 10), routes, [0]);
    }
}

/// <summary>Timer that only ticks when the test says so.</summary>
public sealed class FakeTimer : IPlaybackTimer
{
    public TimeSpan Interval { get; set; }
    public bool IsRunning { get; private set; }
    public event EventHandler? Tick;

    public void Start() => IsRunning = true;
    public void Stop() => IsRunning = false;

    /// <summary>Raises one tick if running, like a real timer would.</summary>
    public void Fire()
    {
        if (IsRunning)
        {
            Tick?.Invoke(this, EventArgs.Empty);
        }
    }
}

/// <summary>File dialogs that return preset answers (null = cancelled).</summary>
public sealed class FakeDialogs : IFileDialogService
{
    public string? OpenAnswer { get; set; }
    public string? SaveAnswer { get; set; }
    public string? CsvAnswer { get; set; }
    public string? SuggestedName { get; private set; }

    public string? PickScenarioToOpen() => OpenAnswer;

    public string? PickScenarioToSave(string suggestedFileName)
    {
        SuggestedName = suggestedFileName;
        return SaveAnswer;
    }

    public string? PickCsvToSave(string suggestedFileName)
    {
        SuggestedName = suggestedFileName;
        return CsvAnswer;
    }
}

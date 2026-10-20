namespace Warehouse.Interop;

// Plain data types shared by the engine wrapper, the app and the tests.
// They hold no native resources, so they can be passed between threads and
// created freely in tests (for example by a fake engine).

/// <summary>A cell position. X grows to the right, Y grows downwards.</summary>
public readonly record struct GridPosition(int X, int Y)
{
    public override string ToString() => $"({X},{Y})";
}

/// <summary>What a grid cell contains. Values match the native WH_CELL_ constants.</summary>
public enum CellKind : byte
{
    Free = 0,
    Wall = 1,
    Shelf = 2,
    Station = 3,
}

/// <summary>One robot's job: start → pickup cell next to the shelf → packing station.</summary>
public sealed record RobotTask(GridPosition Start, GridPosition Shelf, GridPosition Pickup, GridPosition Station);

/// <summary>Settings for the seeded scenario generator.</summary>
public sealed record GeneratorOptions(uint Seed, int Width, int Height, int RobotCount, double ShelfDensity);

/// <summary>A warehouse layout plus the robots' tasks.</summary>
public sealed class WarehouseScenario
{
    private readonly CellKind[] _cells;

    public WarehouseScenario(uint seed, int width, int height, IReadOnlyList<CellKind> cells,
        IReadOnlyList<RobotTask> robots)
    {
        if (width <= 0 || height <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(width), "Width and height must be positive.");
        }
        ArgumentNullException.ThrowIfNull(cells);
        ArgumentNullException.ThrowIfNull(robots);
        if (cells.Count != width * height)
        {
            throw new ArgumentException($"Expected {width * height} cells but got {cells.Count}.", nameof(cells));
        }

        Seed = seed;
        Width = width;
        Height = height;
        _cells = cells.ToArray();
        Robots = robots.ToArray();
    }

    public uint Seed { get; }
    public int Width { get; }
    public int Height { get; }

    /// <summary>Cells in row-major order (index = y * Width + x).</summary>
    public IReadOnlyList<CellKind> Cells => _cells;

    public IReadOnlyList<RobotTask> Robots { get; }

    public CellKind this[int x, int y] => _cells[y * Width + x];
    public CellKind this[GridPosition p] => this[p.X, p.Y];
}

/// <summary>The three cooperative planners. Values match the native WH_PLANNER_ constants.</summary>
public enum PlannerType
{
    AStarManhattan = 0,
    AStarTrueDistance = 1,
    Bfs = 2,
}

/// <summary>Order robots are planned in. Values match the native WH_ORDER_ constants.</summary>
public enum RobotOrdering
{
    InputOrder = 0,
    LongestTaskFirst = 1,
}

/// <summary>Overall result of planning. Values match the native WH_PLAN_ constants.</summary>
public enum PlanStatus
{
    Success = 0,
    NoPath = 1,
    InvalidScenario = 2,
}

/// <summary>Numbers that describe one planning run.</summary>
public sealed record PlanMetrics(
    bool Success,
    int Makespan,
    int TotalPathLength,
    int Waits,
    double RuntimeMs,
    long ExpandedNodes);

/// <summary>One robot's planned route. Path[t] is the robot's cell at time step t.</summary>
public sealed record RobotRoute(bool Planned, int PickupTime, int FinishTime, IReadOnlyList<GridPosition> Path);

/// <summary>Everything a planning run produced.</summary>
public sealed record PlanOutcome(
    PlanStatus Status,
    string Message,
    int FailedRobot,
    PlanMetrics Metrics,
    IReadOnlyList<RobotRoute> Routes,
    IReadOnlyList<int> PlanningOrder)
{
    public bool Success => Status == PlanStatus.Success;
}

/// <summary>Result of checking routes with the engine's plan validator.</summary>
public sealed record PlanValidation(bool IsValid, string Message);

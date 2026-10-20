namespace Warehouse.Interop;

/// <summary>
/// Everything the app needs from the planning engine. The real
/// implementation is <see cref="NativeWarehouseEngine"/>; tests can use a
/// fake. Implementations must be safe to call from several threads at once.
/// </summary>
public interface IWarehouseEngine
{
    /// <summary>Builds a scenario from a seed and settings. Same options give the same scenario.</summary>
    WarehouseScenario Generate(GeneratorOptions options);

    /// <summary>Plans collision-free routes for every robot.</summary>
    /// <param name="maxTime">Latest time step a search may reach; 0 means width × height × 4.</param>
    PlanOutcome Plan(WarehouseScenario scenario, PlannerType planner, RobotOrdering ordering, int maxTime = 0);

    /// <summary>Checks routes against the rules (no shared cells, no swaps, legal moves, pickup first).</summary>
    PlanValidation Validate(WarehouseScenario scenario, IReadOnlyList<IReadOnlyList<GridPosition>> paths);

    WarehouseScenario LoadScenario(string path);

    void SaveScenario(WarehouseScenario scenario, string path);
}

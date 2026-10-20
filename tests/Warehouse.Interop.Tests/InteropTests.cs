using Warehouse.Interop.Native;

namespace Warehouse.Interop.Tests;

/// <summary>Round trips through the C interface: C# → DLL → C#.</summary>
public class InteropTests
{
    private readonly NativeWarehouseEngine _engine = new();

    private static string ScenarioFile(string name) =>
        Path.Combine(AppContext.BaseDirectory, "scenarios", name);

    [Fact]
    public void Generate_ReturnsScenarioMatchingTheOptions()
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(42, 20, 12, 5, 0.3));

        Assert.Equal(42u, scenario.Seed);
        Assert.Equal(20, scenario.Width);
        Assert.Equal(12, scenario.Height);
        Assert.Equal(20 * 12, scenario.Cells.Count);
        Assert.Equal(5, scenario.Robots.Count);
        Assert.Equal(CellKind.Wall, scenario[0, 0]);
        Assert.All(scenario.Robots, robot => Assert.Equal(1, robot.Start.Y)); // robots start in the top row
        Assert.All(scenario.Robots, robot => Assert.Equal(CellKind.Shelf, scenario[robot.Shelf]));
        Assert.All(scenario.Robots, robot => Assert.Equal(CellKind.Station, scenario[robot.Station]));
    }

    [Fact]
    public void Generate_SameSeedGivesSameScenario()
    {
        var options = new GeneratorOptions(7, 30, 20, 10, 0.4);
        WarehouseScenario a = _engine.Generate(options);
        WarehouseScenario b = _engine.Generate(options);

        Assert.Equal(a.Cells, b.Cells);
        Assert.Equal(a.Robots, b.Robots);
    }

    [Fact]
    public void Plan_NarrowCorridor_MatchesTheNativeDemo()
    {
        WarehouseScenario scenario = _engine.LoadScenario(ScenarioFile("narrow_corridor.txt"));

        PlanOutcome plan = _engine.Plan(scenario, PlannerType.AStarManhattan, RobotOrdering.InputOrder);

        // Same numbers wh_demo prints for this file.
        Assert.True(plan.Success);
        Assert.Equal(PlanStatus.Success, plan.Status);
        Assert.Equal(string.Empty, plan.Message);
        Assert.Equal(-1, plan.FailedRobot);
        Assert.Equal(22, plan.Metrics.Makespan);
        Assert.Equal(35, plan.Metrics.TotalPathLength);
        Assert.Equal(5, plan.Metrics.Waits);
        Assert.True(plan.Metrics.ExpandedNodes > 0);
        Assert.True(plan.Metrics.RuntimeMs >= 0);
        Assert.Equal(new[] { 0, 1 }, plan.PlanningOrder);

        Assert.Equal(2, plan.Routes.Count);
        RobotRoute first = plan.Routes[0];
        Assert.True(first.Planned);
        Assert.Equal(scenario.Robots[0].Start, first.Path[0]);
        Assert.Equal(scenario.Robots[0].Pickup, first.Path[first.PickupTime]);
        Assert.Equal(scenario.Robots[0].Station, first.Path[^1]);
        Assert.Equal(first.FinishTime + 1, first.Path.Count);
    }

    [Theory]
    [InlineData(PlannerType.AStarManhattan, RobotOrdering.InputOrder)]
    [InlineData(PlannerType.AStarManhattan, RobotOrdering.LongestTaskFirst)]
    [InlineData(PlannerType.AStarTrueDistance, RobotOrdering.InputOrder)]
    [InlineData(PlannerType.AStarTrueDistance, RobotOrdering.LongestTaskFirst)]
    [InlineData(PlannerType.Bfs, RobotOrdering.InputOrder)]
    [InlineData(PlannerType.Bfs, RobotOrdering.LongestTaskFirst)]
    public void Plan_EveryPlannerAndOrdering_ProducesValidPlan(PlannerType planner, RobotOrdering ordering)
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(42, 30, 20, 10, 0.4));

        PlanOutcome plan = _engine.Plan(scenario, planner, ordering);

        Assert.True(plan.Success, plan.Message);
        Assert.Equal(10, plan.PlanningOrder.Count);
        PlanValidation check = _engine.Validate(scenario, plan.Routes.Select(r => r.Path).ToList());
        Assert.True(check.IsValid, check.Message);
    }

    [Fact]
    public void Plan_PlanningFailure_IsReportedNotThrown()
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(1, 30, 20, 5, 0.3));

        PlanOutcome plan = _engine.Plan(scenario, PlannerType.AStarManhattan, RobotOrdering.InputOrder, maxTime: 5);

        Assert.False(plan.Success);
        Assert.Equal(PlanStatus.NoPath, plan.Status);
        Assert.False(plan.Metrics.Success);
        Assert.InRange(plan.FailedRobot, 0, 4);
        Assert.Contains("within 5 time steps", plan.Message);
        Assert.False(plan.Routes[plan.FailedRobot].Planned);
    }

    [Fact]
    public void Plan_SameInputs_GiveSameRoutes()
    {
        WarehouseScenario scenario = _engine.Generate(new GeneratorOptions(3, 25, 15, 8, 0.4));

        PlanOutcome a = _engine.Plan(scenario, PlannerType.AStarTrueDistance, RobotOrdering.LongestTaskFirst);
        PlanOutcome b = _engine.Plan(scenario, PlannerType.AStarTrueDistance, RobotOrdering.LongestTaskFirst);

        Assert.Equal(a.Routes.Count, b.Routes.Count);
        for (int i = 0; i < a.Routes.Count; i++)
        {
            Assert.Equal(a.Routes[i].Path, b.Routes[i].Path);
        }
        Assert.Equal(a.Metrics.ExpandedNodes, b.Metrics.ExpandedNodes);
    }

    [Fact]
    public void Validate_DetectsRobotsCrashing()
    {
        WarehouseScenario scenario = _engine.LoadScenario(ScenarioFile("narrow_corridor.txt"));

        // Each robot's shortest route on its own: they meet head-on in the corridor.
        var robot0 = new List<GridPosition>();
        for (int x = 1; x <= 10; x++) robot0.Add(new GridPosition(x, 2));
        robot0.AddRange([new(10, 1), new(10, 2), new(10, 3), new(11, 3)]);
        var robot1 = new List<GridPosition>();
        for (int x = 11; x >= 2; x--) robot1.Add(new GridPosition(x, 2));
        robot1.AddRange([new(2, 1), new(2, 2), new(1, 2), new(1, 3)]);

        PlanValidation check = _engine.Validate(scenario, [robot0, robot1]);

        Assert.False(check.IsValid);
        Assert.Contains("robot 0 and robot 1", check.Message);
    }

    [Fact]
    public void Validate_DetectsIllegalMove()
    {
        WarehouseScenario scenario = _engine.LoadScenario(ScenarioFile("narrow_corridor.txt"));
        PlanOutcome plan = _engine.Plan(scenario, PlannerType.AStarManhattan, RobotOrdering.InputOrder);
        var paths = plan.Routes.Select(r => (IReadOnlyList<GridPosition>)r.Path.ToList()).ToList();
        var jumped = paths[0].ToList();
        jumped.RemoveAt(3); // skip a cell: a two-cell jump

        PlanValidation check = _engine.Validate(scenario, [jumped, paths[1]]);

        Assert.False(check.IsValid);
        Assert.Contains("illegal move", check.Message);
    }

    [Fact]
    public void SaveAndLoad_RoundTrip()
    {
        WarehouseScenario original = _engine.Generate(new GeneratorOptions(11, 15, 10, 4, 0.3));
        string file = Path.Combine(Path.GetTempPath(), $"wh_interop_{Guid.NewGuid():N}.txt");
        try
        {
            _engine.SaveScenario(original, file);
            WarehouseScenario loaded = _engine.LoadScenario(file);

            Assert.Equal(original.Seed, loaded.Seed);
            Assert.Equal(original.Width, loaded.Width);
            Assert.Equal(original.Cells, loaded.Cells);
            Assert.Equal(original.Robots, loaded.Robots);
        }
        finally
        {
            File.Delete(file);
        }
    }

    [Fact]
    public void ScenarioCreatedInCSharp_CanBePlanned()
    {
        // 5x3 room built by hand:   S...P
        //                           .....
        //                           .....
        var cells = Enumerable.Repeat(CellKind.Free, 15).ToArray();
        cells[0] = CellKind.Shelf;
        cells[4] = CellKind.Station;
        var task = new RobotTask(new(0, 2), new(0, 0), new(1, 0), new(4, 0));
        var scenario = new WarehouseScenario(0, 5, 3, cells, [task]);

        PlanOutcome plan = _engine.Plan(scenario, PlannerType.Bfs, RobotOrdering.InputOrder);

        Assert.True(plan.Success);
        Assert.Equal(6, plan.Metrics.Makespan); // 3 moves to the pickup, 3 to the station
        Assert.Equal(0, plan.Metrics.Waits);
    }
}

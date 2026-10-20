using Warehouse.Interop;

namespace Warehouse.ViewModels.Tests;

public class SimulationViewModelTests
{
    private readonly FakeEngine _engine = new();
    private readonly FakeDialogs _dialogs = new();
    private readonly FakeTimer _timer = new();
    private readonly SimulationViewModel _vm;

    public SimulationViewModelTests()
    {
        _vm = new SimulationViewModel(_engine, _dialogs, _timer)
        {
            Seed = 7,
            Width = 12,
            Height = 8,
            RobotCount = 3,
            ShelfDensity = 0.25,
        };
    }

    /// <summary>Generate + plan with the fake engine; makespan is 4 for 3 robots.</summary>
    private async Task GenerateAndPlanAsync()
    {
        _vm.GenerateCommand.Execute(null);
        await _vm.PlanCommand.ExecuteAsync(null);
    }

    // ---- Start-up ----

    [Fact]
    public void StartsWithDefaultsAndNothingToPlay()
    {
        var vm = new SimulationViewModel(_engine, _dialogs, _timer);

        Assert.Equal(42, vm.Seed);
        Assert.Equal(PlannerType.AStarManhattan, vm.SelectedPlanner.Value);
        Assert.Equal(RobotOrdering.InputOrder, vm.SelectedOrdering.Value);
        Assert.Null(vm.Scenario);
        Assert.Empty(vm.Robots);
        Assert.True(vm.GenerateCommand.CanExecute(null));
        Assert.False(vm.PlanCommand.CanExecute(null));
        Assert.False(vm.PlayPauseCommand.CanExecute(null));
        Assert.False(vm.StepCommand.CanExecute(null));
        Assert.False(vm.SaveScenarioCommand.CanExecute(null));
        Assert.Equal(3, vm.Planners.Count);
        Assert.Equal(2, vm.Orderings.Count);
    }

    // ---- Generate ----

    [Fact]
    public void Generate_PassesSettingsToEngineAndShowsScenario()
    {
        _vm.GenerateCommand.Execute(null);

        GeneratorOptions call = Assert.Single(_engine.GenerateCalls);
        Assert.Equal(new GeneratorOptions(7, 12, 8, 3, 0.25), call);
        Assert.NotNull(_vm.Scenario);
        Assert.Equal(3, _vm.Robots.Count);
        Assert.Equal(_vm.Scenario!.Robots[1].Start, _vm.Robots[1].Position);
        Assert.False(_vm.IsError);
        Assert.Contains("Generated seed 7", _vm.StatusMessage);
        Assert.True(_vm.PlanCommand.CanExecute(null));
        Assert.True(_vm.SaveScenarioCommand.CanExecute(null));
        Assert.False(_vm.PlayPauseCommand.CanExecute(null)); // nothing planned yet
    }

    [Theory]
    [InlineData(4, 8, 2, 0.3, "Width")]
    [InlineData(101, 8, 2, 0.3, "Width")]
    [InlineData(12, 5, 2, 0.3, "Height")]
    [InlineData(12, 8, 0, 0.3, "Robots")]
    [InlineData(12, 8, 11, 0.3, "Robots")]
    [InlineData(12, 8, 2, 0.95, "Shelf density")]
    [InlineData(12, 8, 2, -0.1, "Shelf density")]
    public void Generate_InvalidSettings_ShowsErrorWithoutCallingEngine(int width, int height, int robots,
        double density, string expectedWord)
    {
        _vm.Width = width;
        _vm.Height = height;
        _vm.RobotCount = robots;
        _vm.ShelfDensity = density;

        _vm.GenerateCommand.Execute(null);

        Assert.Empty(_engine.GenerateCalls);
        Assert.True(_vm.IsError);
        Assert.StartsWith(expectedWord, _vm.StatusMessage);
        Assert.Null(_vm.Scenario);
    }

    [Fact]
    public void Generate_EngineError_IsShown()
    {
        _engine.Failure = new WarehouseEngineException(EngineStatus.InvalidArgument, "something went wrong");

        _vm.GenerateCommand.Execute(null);

        Assert.True(_vm.IsError);
        Assert.Contains("something went wrong", _vm.StatusMessage);
    }

    [Fact]
    public async Task Generate_ClearsThePreviousPlan()
    {
        await GenerateAndPlanAsync();
        _vm.StepCommand.Execute(null);

        _vm.GenerateCommand.Execute(null);

        Assert.Null(_vm.Plan);
        Assert.Equal(0, _vm.CurrentTime);
        Assert.False(_vm.PlayPauseCommand.CanExecute(null));
    }

    // ---- Plan ----

    [Fact]
    public async Task Plan_UsesSelectedPlannerAndOrdering()
    {
        _vm.GenerateCommand.Execute(null);
        _vm.SelectedPlanner = _vm.Planners[2];
        _vm.SelectedOrdering = _vm.Orderings[1];

        await _vm.PlanCommand.ExecuteAsync(null);

        Assert.Equal((PlannerType.Bfs, RobotOrdering.LongestTaskFirst), Assert.Single(_engine.PlanCalls));
        Assert.True(_vm.HasSuccessfulPlan);
        Assert.Equal(4, _vm.LastTime);
        Assert.Equal("Time step 0 / 4", _vm.TimeText);
        Assert.Equal("0, 1, 2", _vm.PlanningOrderText);
        Assert.False(_vm.IsError);
        Assert.Contains("BFS (no heuristic)", _vm.StatusMessage);
        Assert.True(_vm.PlayPauseCommand.CanExecute(null));
        Assert.True(_vm.StepCommand.CanExecute(null));
        Assert.False(_vm.ResetCommand.CanExecute(null)); // already at the start
    }

    [Fact]
    public async Task Plan_RunsOnABackgroundThread()
    {
        _vm.GenerateCommand.Execute(null);

        await _vm.PlanCommand.ExecuteAsync(null);

        Assert.NotNull(_engine.PlanThreadId);
        Assert.NotEqual(Environment.CurrentManagedThreadId, _engine.PlanThreadId);
    }

    [Fact]
    public async Task Plan_IsBusyWhileRunning_AndButtonsAreDisabled()
    {
        _vm.GenerateCommand.Execute(null);
        _engine.PlanGate = new TaskCompletionSource();

        Task planning = _vm.PlanCommand.ExecuteAsync(null);
        Assert.True(_vm.IsBusy);
        Assert.False(_vm.GenerateCommand.CanExecute(null));
        Assert.False(_vm.PlanCommand.CanExecute(null));
        Assert.False(_vm.LoadScenarioCommand.CanExecute(null));

        _engine.PlanGate.SetResult();
        await planning;
        Assert.False(_vm.IsBusy);
        Assert.True(_vm.PlanCommand.CanExecute(null));
    }

    [Fact]
    public async Task Plan_Failure_ShowsClearMessageAndDisablesPlayback()
    {
        _vm.GenerateCommand.Execute(null);
        _engine.PlanFails = true;

        await _vm.PlanCommand.ExecuteAsync(null);

        Assert.True(_vm.IsError);
        Assert.Contains("Planning failed", _vm.StatusMessage);
        Assert.Contains("no collision-free route", _vm.StatusMessage);
        Assert.False(_vm.HasSuccessfulPlan);
        Assert.False(_vm.PlayPauseCommand.CanExecute(null));
        Assert.Equal("Time step –", _vm.TimeText);
        // Robots stay drawn at their start cells.
        Assert.Equal(_vm.Scenario!.Robots[0].Start, _vm.Robots[0].Position);
    }

    [Fact]
    public async Task Plan_EngineException_IsShownNotThrown()
    {
        _vm.GenerateCommand.Execute(null);
        _engine.Failure = new WarehouseEngineException(EngineStatus.InternalError, "boom");

        await _vm.PlanCommand.ExecuteAsync(null);

        Assert.True(_vm.IsError);
        Assert.Contains("boom", _vm.StatusMessage);
        Assert.Null(_vm.Plan);
        Assert.False(_vm.IsBusy);
    }

    // ---- Step / Play / Reset ----

    [Fact]
    public async Task Step_MovesRobotsAlongTheirPaths()
    {
        await GenerateAndPlanAsync();
        RobotTask task = _vm.Scenario!.Robots[2];

        _vm.StepCommand.Execute(null);
        Assert.Equal(1, _vm.CurrentTime);
        Assert.Equal(task.Pickup, _vm.Robots[2].Position);
        Assert.True(_vm.Robots[2].HasPickedUp);
        Assert.True(_vm.ResetCommand.CanExecute(null));

        _vm.StepCommand.Execute(null);
        _vm.StepCommand.Execute(null);
        // Robot 0 finished at t = 2 and has left the grid; robot 2 is still waiting.
        Assert.True(_vm.Robots[0].Delivered);
        Assert.False(_vm.Robots[2].Delivered);
        Assert.Equal(task.Pickup, _vm.Robots[2].Position);
    }

    [Fact]
    public async Task Step_StopsAtTheLastTimeStep()
    {
        await GenerateAndPlanAsync();

        for (int i = 0; i < 10; i++)
        {
            if (_vm.StepCommand.CanExecute(null))
            {
                _vm.StepCommand.Execute(null);
            }
        }

        Assert.Equal(4, _vm.CurrentTime);
        Assert.False(_vm.StepCommand.CanExecute(null));
        Assert.Equal(_vm.Scenario!.Robots[2].Station, _vm.Robots[2].Position);
        Assert.Equal("Time step 4 / 4", _vm.TimeText);
    }

    [Fact]
    public async Task Play_AdvancesOnEachTickAndStopsAtTheEnd()
    {
        await GenerateAndPlanAsync();

        _vm.PlayPauseCommand.Execute(null);
        Assert.True(_vm.IsPlaying);
        Assert.True(_timer.IsRunning);
        Assert.Equal("Pause", _vm.PlayPauseText);

        _timer.Fire();
        _timer.Fire();
        Assert.Equal(2, _vm.CurrentTime);

        _timer.Fire();
        _timer.Fire();
        Assert.Equal(4, _vm.CurrentTime);
        Assert.False(_vm.IsPlaying);
        Assert.False(_timer.IsRunning);
        Assert.Equal("Play", _vm.PlayPauseText);
    }

    [Fact]
    public async Task PlayPause_PausesAndResumes()
    {
        await GenerateAndPlanAsync();
        _vm.PlayPauseCommand.Execute(null);
        _timer.Fire();

        _vm.PlayPauseCommand.Execute(null); // pause
        Assert.False(_vm.IsPlaying);
        _timer.Fire();                      // a stopped timer does not tick
        Assert.Equal(1, _vm.CurrentTime);

        _vm.PlayPauseCommand.Execute(null); // resume
        _timer.Fire();
        Assert.Equal(2, _vm.CurrentTime);
    }

    [Fact]
    public async Task Play_AtTheEnd_StartsAgainFromZero()
    {
        await GenerateAndPlanAsync();
        for (int i = 0; i < 4; i++)
        {
            _vm.StepCommand.Execute(null);
        }

        _vm.PlayPauseCommand.Execute(null);

        Assert.Equal(0, _vm.CurrentTime);
        Assert.True(_vm.IsPlaying);
    }

    [Fact]
    public async Task Step_WhilePlaying_Pauses()
    {
        await GenerateAndPlanAsync();
        _vm.PlayPauseCommand.Execute(null);

        _vm.StepCommand.Execute(null);

        Assert.False(_vm.IsPlaying);
        Assert.Equal(1, _vm.CurrentTime);
    }

    [Fact]
    public async Task Reset_GoesBackToTheStartAndStops()
    {
        await GenerateAndPlanAsync();
        _vm.PlayPauseCommand.Execute(null);
        _timer.Fire();
        _timer.Fire();

        _vm.ResetCommand.Execute(null);

        Assert.Equal(0, _vm.CurrentTime);
        Assert.False(_vm.IsPlaying);
        Assert.False(_timer.IsRunning);
        Assert.All(_vm.Robots, r => Assert.Equal(_vm.Scenario!.Robots[r.Index].Start, r.Position));
        Assert.False(_vm.ResetCommand.CanExecute(null));
    }

    [Theory]
    [InlineData(4.0, 250)]
    [InlineData(1.0, 1000)]
    [InlineData(10.0, 100)]
    [InlineData(1000.0, 1000.0 / SimulationViewModel.MaxSpeed)]
    public void Speed_SetsTimerInterval(double stepsPerSecond, double expectedMs)
    {
        _vm.StepsPerSecond = stepsPerSecond;

        Assert.Equal(expectedMs, _timer.Interval.TotalMilliseconds, precision: 3);
    }

    [Fact]
    public async Task PropertyChanged_IsRaisedForTheTimeDisplay()
    {
        await GenerateAndPlanAsync();
        var changed = new List<string?>();
        _vm.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        _vm.StepCommand.Execute(null);

        Assert.Contains(nameof(SimulationViewModel.CurrentTime), changed);
        Assert.Contains(nameof(SimulationViewModel.TimeText), changed);
        Assert.Contains(nameof(SimulationViewModel.Robots), changed);
    }

    // ---- Load / Save ----

    [Fact]
    public void Save_WritesTheScenarioToTheChosenFile()
    {
        _vm.GenerateCommand.Execute(null);
        _dialogs.SaveAnswer = "C:/temp/my_scenario.txt";

        _vm.SaveScenarioCommand.Execute(null);

        var (scenario, path) = Assert.Single(_engine.Saved);
        Assert.Same(_vm.Scenario, scenario);
        Assert.Equal("C:/temp/my_scenario.txt", path);
        Assert.Equal("scenario_seed7.txt", _dialogs.SuggestedName);
        Assert.Contains("Saved my_scenario.txt", _vm.StatusMessage);
    }

    [Fact]
    public void Save_Cancelled_DoesNothing()
    {
        _vm.GenerateCommand.Execute(null);
        _dialogs.SaveAnswer = null;

        _vm.SaveScenarioCommand.Execute(null);

        Assert.Empty(_engine.Saved);
    }

    [Fact]
    public void Load_ShowsTheScenarioAndCopiesItsSettings()
    {
        _engine.ScenarioToLoad = FakeEngine.MakeScenario(99, 20, 10, 5);
        _dialogs.OpenAnswer = "corridor.txt";

        _vm.LoadScenarioCommand.Execute(null);

        Assert.Same(_engine.ScenarioToLoad, _vm.Scenario);
        Assert.Equal(99, _vm.Seed);
        Assert.Equal(20, _vm.Width);
        Assert.Equal(10, _vm.Height);
        Assert.Equal(5, _vm.RobotCount);
        Assert.Contains("Loaded corridor.txt", _vm.StatusMessage);
    }

    [Fact]
    public void Load_Error_IsShownAndKeepsTheOldScenario()
    {
        _vm.GenerateCommand.Execute(null);
        WarehouseScenario? before = _vm.Scenario;
        _dialogs.OpenAnswer = "missing.txt";

        _vm.LoadScenarioCommand.Execute(null); // the fake engine has nothing to load

        Assert.True(_vm.IsError);
        Assert.Contains("cannot open", _vm.StatusMessage);
        Assert.Same(before, _vm.Scenario);
    }

    [Fact]
    public void Load_Cancelled_DoesNothing()
    {
        _dialogs.OpenAnswer = null;

        _vm.LoadScenarioCommand.Execute(null);

        Assert.Null(_vm.Scenario);
        Assert.False(_vm.IsError);
    }
}

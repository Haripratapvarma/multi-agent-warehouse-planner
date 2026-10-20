using Warehouse.Interop;

namespace Warehouse.ViewModels.Tests;

public class CompareViewModelTests
{
    private readonly FakeEngine _engine = new();
    private readonly FakeDialogs _dialogs = new();
    private readonly CompareViewModel _vm;

    public CompareViewModelTests()
    {
        _vm = new CompareViewModel(_engine, _dialogs)
        {
            SeedCount = 4,
            FirstSeed = 10,
            Width = 12,
            Height = 8,
            RobotCount = 3,
            ShelfDensity = 0.3,
            ThreadCount = 4,
        };
    }

    [Fact]
    public void StartsEmpty()
    {
        var vm = new CompareViewModel(_engine, _dialogs);

        Assert.Equal(20, vm.SeedCount);
        Assert.Equal(Environment.ProcessorCount, vm.ThreadCount);
        Assert.Empty(vm.Results);
        Assert.Equal(0, vm.ProgressPercent);
        Assert.True(vm.RunCommand.CanExecute(null));
        Assert.False(vm.RunCancelCommand.CanExecute(null));
        Assert.False(vm.ExportCsvCommand.CanExecute(null));
    }

    [Fact]
    public async Task Run_PlansEverySeedWithEveryPlannerAndOrdering()
    {
        await _vm.RunCommand.ExecuteAsync(null);

        // 4 seeds generated, seeds 10..13, with the chosen settings.
        Assert.Equal([10u, 11u, 12u, 13u], _engine.GenerateCalls.Select(c => c.Seed).Order().ToArray());
        Assert.All(_engine.GenerateCalls, c => Assert.Equal((12, 8, 3, 0.3), (c.Width, c.Height, c.RobotCount, c.ShelfDensity)));

        // 4 seeds × 3 planners × 2 orderings = 24 plans, 4 for each pair.
        Assert.Equal(24, _engine.PlanCalls.Count);
        Assert.All(_engine.PlanCalls.GroupBy(c => c), group => Assert.Equal(4, group.Count()));
        Assert.Equal(6, _engine.PlanCalls.Distinct().Count());

        Assert.Equal(6, _vm.Results.Count);
        Assert.All(_vm.Results, row =>
        {
            Assert.Equal(4, row.Runs);
            Assert.Equal(1.0, row.SuccessRate);
            Assert.Equal(4.0, row.AvgMakespan); // the fake plan for 3 robots ends at t = 4
        });
        Assert.Equal(24, _vm.CompletedRuns);
        Assert.Equal(24, _vm.TotalRuns);
        Assert.Equal(100, _vm.ProgressPercent);
        Assert.Equal("24 / 24 plans", _vm.ProgressText);
        Assert.Contains("Finished 24 plans", _vm.StatusMessage);
        Assert.Contains("averages over the 4 seeds", _vm.ResultsNote);
        Assert.False(_vm.IsRunning);
        Assert.True(_vm.ExportCsvCommand.CanExecute(null));
    }

    [Fact]
    public async Task Run_UsesBackgroundThreads()
    {
        int testThread = Environment.CurrentManagedThreadId;

        await _vm.RunCommand.ExecuteAsync(null);

        Assert.NotEmpty(_engine.PlanThreadIds);
        Assert.DoesNotContain(testThread, _engine.PlanThreadIds);
    }

    [Fact]
    public async Task Run_WhileRunning_ShowsProgressAndOnlyCancelIsAvailable()
    {
        _vm.ThreadCount = 1;
        _engine.PlanGate = new TaskCompletionSource();

        Task running = _vm.RunCommand.ExecuteAsync(null);
        await _engine.PlanStarted.Task.WaitAsync(TimeSpan.FromSeconds(10));

        // The caller got control back while plans are still running: the UI stays responsive.
        Assert.False(running.IsCompleted);
        Assert.True(_vm.IsRunning);
        Assert.False(_vm.IsIdle);
        Assert.Equal(24, _vm.TotalRuns);
        Assert.False(_vm.RunCommand.CanExecute(null));
        Assert.True(_vm.RunCancelCommand.CanExecute(null));
        Assert.False(_vm.ExportCsvCommand.CanExecute(null));

        _engine.PlanGate.SetResult();
        await running;
        Assert.False(_vm.IsRunning);
        Assert.Equal(6, _vm.Results.Count);
    }

    [Fact]
    public async Task Cancel_StopsTheRunAndKeepsNoResults()
    {
        _vm.ThreadCount = 1;
        _vm.SeedCount = 50; // 300 plans: far more than can finish before the cancel
        _engine.PlanGate = new TaskCompletionSource();

        Task running = _vm.RunCommand.ExecuteAsync(null);
        await _engine.PlanStarted.Task.WaitAsync(TimeSpan.FromSeconds(10));
        _vm.RunCancelCommand.Execute(null);
        _engine.PlanGate.SetResult(); // let the plan already in progress finish
        await running.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.False(_vm.IsRunning);
        Assert.False(_vm.IsError);
        Assert.Contains("Cancelled after", _vm.StatusMessage);
        Assert.Empty(_vm.Results);
        Assert.True(_engine.PlanCalls.Count < 300);
        Assert.True(_vm.RunCommand.CanExecute(null)); // can run again
        Assert.False(_vm.ExportCsvCommand.CanExecute(null));
    }

    [Fact]
    public async Task Run_AfterCancel_WorksAgain()
    {
        _vm.ThreadCount = 1;
        _engine.PlanGate = new TaskCompletionSource();
        Task first = _vm.RunCommand.ExecuteAsync(null);
        await _engine.PlanStarted.Task.WaitAsync(TimeSpan.FromSeconds(10));
        _vm.RunCancelCommand.Execute(null);
        _engine.PlanGate.SetResult();
        await first;

        _engine.PlanGate = null;
        await _vm.RunCommand.ExecuteAsync(null);

        Assert.Equal(6, _vm.Results.Count);
        Assert.Contains("Finished", _vm.StatusMessage);
    }

    [Theory]
    [InlineData(0, 0, 4, "Number of seeds")]
    [InlineData(4, -1, 4, "First seed")]
    [InlineData(4, 0, 0, "Threads")]
    public async Task Run_InvalidSettings_ShowsErrorWithoutCallingEngine(int seeds, int firstSeed, int threads,
        string expectedStart)
    {
        _vm.SeedCount = seeds;
        _vm.FirstSeed = firstSeed;
        _vm.ThreadCount = threads;

        await _vm.RunCommand.ExecuteAsync(null);

        Assert.True(_vm.IsError);
        Assert.StartsWith(expectedStart, _vm.StatusMessage);
        Assert.Empty(_engine.GenerateCalls);
    }

    [Fact]
    public async Task Run_InvalidGridSettings_UseTheSameRulesAsTheSimulationTab()
    {
        _vm.RobotCount = 11; // more than width - 2

        await _vm.RunCommand.ExecuteAsync(null);

        Assert.True(_vm.IsError);
        Assert.StartsWith("Robots must be between 1 and 10", _vm.StatusMessage);
    }

    [Fact]
    public async Task Run_EngineError_IsShownNotThrown()
    {
        _engine.Failure = new WarehouseEngineException(EngineStatus.InternalError, "engine exploded");

        await _vm.RunCommand.ExecuteAsync(null);

        Assert.True(_vm.IsError);
        Assert.Contains("engine exploded", _vm.StatusMessage);
        Assert.False(_vm.IsRunning);
        Assert.Empty(_vm.Results);
    }

    [Fact]
    public async Task Run_FailedPlans_LowerTheSuccessRate()
    {
        _engine.PlanFails = true;

        await _vm.RunCommand.ExecuteAsync(null);

        Assert.All(_vm.Results, row =>
        {
            Assert.Equal(0, row.SuccessRate);
            Assert.Null(row.AvgMakespan); // no seed where everyone succeeded
        });
        Assert.Contains("averages over the 0 seeds", _vm.ResultsNote);
    }

    [Fact]
    public async Task ExportCsv_WritesTheTable()
    {
        await _vm.RunCommand.ExecuteAsync(null);
        string file = Path.Combine(Path.GetTempPath(), $"wh_compare_{Guid.NewGuid():N}.csv");
        _dialogs.CsvAnswer = file;
        try
        {
            _vm.ExportCsvCommand.Execute(null);

            string[] lines = File.ReadAllLines(file);
            Assert.Equal(7, lines.Length); // header + 6 rows
            Assert.Equal(ComparisonSummary.CsvHeader, lines[0]);
            Assert.StartsWith("astar-manhattan,input,4,4,1.0000,4.00,", lines[1]);
            Assert.StartsWith("bfs,longest-first,4,4,", lines[6]);
            Assert.Equal("planner_comparison.csv", _dialogs.SuggestedName);
            Assert.Contains("Saved", _vm.StatusMessage);
        }
        finally
        {
            File.Delete(file);
        }
    }

    [Fact]
    public async Task ExportCsv_Cancelled_WritesNothing()
    {
        await _vm.RunCommand.ExecuteAsync(null);
        _dialogs.CsvAnswer = null;

        _vm.ExportCsvCommand.Execute(null);

        Assert.DoesNotContain("Saved", _vm.StatusMessage);
    }

    [Fact]
    public async Task ExportCsv_BadPath_ShowsError()
    {
        await _vm.RunCommand.ExecuteAsync(null);
        _dialogs.CsvAnswer = Path.Combine(Path.GetTempPath(), "no_such_folder_" + Guid.NewGuid().ToString("N"), "x.csv");

        _vm.ExportCsvCommand.Execute(null);

        Assert.True(_vm.IsError);
        Assert.Contains("Could not save the CSV file", _vm.StatusMessage);
    }
}

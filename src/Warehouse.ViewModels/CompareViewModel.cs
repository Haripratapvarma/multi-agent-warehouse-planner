using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Warehouse.Interop;
using Warehouse.ViewModels.Services;

namespace Warehouse.ViewModels;

/// <summary>
/// The "Compare planners" screen: runs N seeds × every planner × both
/// orderings on background threads, shows progress, can be cancelled, and
/// summarises the results in a table that can be exported to CSV.
/// </summary>
public sealed partial class CompareViewModel : ObservableObject
{
    public const int MaxSeedCount = 10_000;
    public const int MaxThreads = 256;

    private readonly IWarehouseEngine _engine;
    private readonly IFileDialogService _dialogs;
    private int _finishedPlans; // updated from worker threads with Interlocked

    public CompareViewModel(IWarehouseEngine engine, IFileDialogService dialogs)
    {
        _engine = engine ?? throw new ArgumentNullException(nameof(engine));
        _dialogs = dialogs ?? throw new ArgumentNullException(nameof(dialogs));

        SeedCount = 20;
        FirstSeed = 1;
        Width = 30;
        Height = 20;
        RobotCount = 10;
        ShelfDensity = 0.3;
        ThreadCount = Environment.ProcessorCount;
        Results = [];
        ResultsNote = string.Empty;
        StatusMessage = "Choose the settings and press Run.";
    }

    // ---- Settings ----

    [ObservableProperty]
    public partial int SeedCount { get; set; }

    [ObservableProperty]
    public partial int FirstSeed { get; set; }

    [ObservableProperty]
    public partial int Width { get; set; }

    [ObservableProperty]
    public partial int Height { get; set; }

    [ObservableProperty]
    public partial int RobotCount { get; set; }

    [ObservableProperty]
    public partial double ShelfDensity { get; set; }

    /// <summary>How many plans may run at the same time. 1 gives the cleanest runtime numbers.</summary>
    [ObservableProperty]
    public partial int ThreadCount { get; set; }

    // ---- State ----

    [ObservableProperty]
    public partial IReadOnlyList<ComparisonRow> Results { get; private set; }

    [ObservableProperty]
    public partial int CompletedRuns { get; private set; }

    [ObservableProperty]
    public partial int TotalRuns { get; private set; }

    [ObservableProperty]
    public partial bool IsRunning { get; private set; }

    [ObservableProperty]
    public partial string StatusMessage { get; private set; }

    [ObservableProperty]
    public partial bool IsError { get; private set; }

    /// <summary>Explains how the table was averaged; empty before the first run.</summary>
    [ObservableProperty]
    public partial string ResultsNote { get; private set; }

    public bool IsIdle => !IsRunning;

    public double ProgressPercent => TotalRuns == 0 ? 0 : 100.0 * CompletedRuns / TotalRuns;

    public string ProgressText => TotalRuns == 0 ? string.Empty : $"{CompletedRuns} / {TotalRuns} plans";

    // ---- Commands ----

    /// <summary>Runs the comparison. The toolkit also generates RunCancelCommand for the Cancel button.</summary>
    [RelayCommand(IncludeCancelCommand = true, CanExecute = nameof(IsIdle))]
    private async Task RunAsync(CancellationToken cancellationToken)
    {
        string? problem = CheckSettings();
        if (problem is not null)
        {
            ShowError(problem);
            return;
        }

        // Copy the settings so changes in the window cannot affect a running comparison.
        var settings = new RunSettings((uint)FirstSeed, SeedCount, Width, Height, RobotCount, ShelfDensity, ThreadCount);
        int total = settings.SeedCount * ComparisonSummary.Combinations.Count;

        Results = [];
        ResultsNote = string.Empty;
        _finishedPlans = 0;
        CompletedRuns = 0;
        TotalRuns = total;
        IsRunning = true;
        ShowInfo($"Running {total} plans on up to {settings.Threads} threads…");

        // Progress<T> captures the UI thread here and reports back on it,
        // so worker threads never touch bound properties directly.
        var progress = new Progress<int>(finished =>
        {
            if (finished > CompletedRuns)
            {
                CompletedRuns = finished;
            }
        });

        var clock = Stopwatch.StartNew();
        try
        {
            IReadOnlyList<ComparisonRun> runs =
                await Task.Run(() => RunAll(settings, progress, cancellationToken), cancellationToken);

            CompletedRuns = total;
            Results = ComparisonSummary.Summarize(runs);
            int common = ComparisonSummary.CommonSuccessSeeds(runs).Count;
            ResultsNote = $"Success rate is over all {settings.SeedCount} seeds. The other columns are averages over " +
                          $"the {common} seeds where every planner and ordering succeeded.";
            ShowInfo($"Finished {total} plans in {clock.Elapsed.TotalSeconds:0.0} s.");
        }
        catch (OperationCanceledException)
        {
            ShowInfo($"Cancelled after {Volatile.Read(ref _finishedPlans)} of {total} plans. No results were kept.");
        }
        catch (Exception e) when (EngineError(e) is { } engineError)
        {
            ShowError($"The comparison stopped: {engineError.Message}");
        }
        finally
        {
            IsRunning = false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanExport))]
    private void ExportCsv()
    {
        string? path = _dialogs.PickCsvToSave("planner_comparison.csv");
        if (path is null)
        {
            return;
        }
        try
        {
            File.WriteAllText(path, ComparisonSummary.ToCsv(Results));
            ShowInfo($"Saved {Path.GetFileName(path)}.");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            ShowError($"Could not save the CSV file: {e.Message}");
        }
    }

    private bool CanExport() => !IsRunning && Results.Count > 0;

    // ---- Work done on background threads ----

    private sealed record RunSettings(uint FirstSeed, int SeedCount, int Width, int Height, int Robots,
        double Density, int Threads);

    /// <summary>
    /// Generates every scenario, then plans every (seed, planner, ordering)
    /// combination in parallel. Parallel.For stops starting new work as soon
    /// as the token is cancelled and then throws OperationCanceledException.
    /// </summary>
    private IReadOnlyList<ComparisonRun> RunAll(RunSettings settings, IProgress<int> progress,
        CancellationToken cancellationToken)
    {
        var parallel = new ParallelOptions
        {
            MaxDegreeOfParallelism = settings.Threads,
            CancellationToken = cancellationToken,
        };

        var scenarios = new WarehouseScenario[settings.SeedCount];
        Parallel.For(0, settings.SeedCount, parallel, i =>
        {
            var options = new GeneratorOptions(settings.FirstSeed + (uint)i, settings.Width, settings.Height,
                settings.Robots, settings.Density);
            scenarios[i] = _engine.Generate(options);
        });

        var combinations = ComparisonSummary.Combinations;
        var runs = new ComparisonRun[settings.SeedCount * combinations.Count];
        Parallel.For(0, runs.Length, parallel, job =>
        {
            WarehouseScenario scenario = scenarios[job / combinations.Count];
            var (planner, ordering) = combinations[job % combinations.Count];
            PlanOutcome outcome = _engine.Plan(scenario, planner, ordering);
            runs[job] = new ComparisonRun(scenario.Seed, planner, ordering, outcome.Metrics);
            progress.Report(Interlocked.Increment(ref _finishedPlans));
        });
        return runs;
    }

    // ---- Helpers ----

    public string? CheckSettings()
    {
        if (SeedCount < 1 || SeedCount > MaxSeedCount)
        {
            return $"Number of seeds must be between 1 and {MaxSeedCount}.";
        }
        if (FirstSeed < 0)
        {
            return "First seed must be 0 or more.";
        }
        if (ThreadCount < 1 || ThreadCount > MaxThreads)
        {
            return $"Threads must be between 1 and {MaxThreads}.";
        }
        return ScenarioLimits.Check(Width, Height, RobotCount, ShelfDensity);
    }

    /// <summary>Finds an engine error, also when Parallel.For wrapped it in an AggregateException.</summary>
    private static WarehouseEngineException? EngineError(Exception e) => e switch
    {
        WarehouseEngineException engine => engine,
        AggregateException aggregate => aggregate.Flatten().InnerExceptions.OfType<WarehouseEngineException>()
            .FirstOrDefault(),
        _ => null,
    };

    private void ShowInfo(string message)
    {
        StatusMessage = message;
        IsError = false;
    }

    private void ShowError(string message)
    {
        StatusMessage = message;
        IsError = true;
    }

    partial void OnIsRunningChanged(bool value)
    {
        OnPropertyChanged(nameof(IsIdle));
        RunCommand.NotifyCanExecuteChanged();
        ExportCsvCommand.NotifyCanExecuteChanged();
    }

    partial void OnResultsChanged(IReadOnlyList<ComparisonRow> value) => ExportCsvCommand.NotifyCanExecuteChanged();

    partial void OnCompletedRunsChanged(int value)
    {
        OnPropertyChanged(nameof(ProgressPercent));
        OnPropertyChanged(nameof(ProgressText));
    }

    partial void OnTotalRunsChanged(int value)
    {
        OnPropertyChanged(nameof(ProgressPercent));
        OnPropertyChanged(nameof(ProgressText));
    }
}

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Warehouse.Interop;
using Warehouse.ViewModels.Services;

namespace Warehouse.ViewModels;

/// <summary>
/// The "Simulation" screen: configure a scenario, generate it, plan it and
/// watch the robots move one time step at a time.
/// </summary>
public sealed partial class SimulationViewModel : ObservableObject
{
    // Limits match the C++ generator, so most mistakes are caught here with
    // a friendly message before the engine is called.
    public const int MinWidth = 5;
    public const int MinHeight = 6;
    public const int MaxSize = 100;
    public const double MaxShelfDensity = 0.9;
    public const double MinSpeed = 0.5;
    public const double MaxSpeed = 30;

    private readonly IWarehouseEngine _engine;
    private readonly IFileDialogService _dialogs;
    private readonly IPlaybackTimer _timer;

    public SimulationViewModel(IWarehouseEngine engine, IFileDialogService dialogs, IPlaybackTimer timer)
    {
        _engine = engine ?? throw new ArgumentNullException(nameof(engine));
        _dialogs = dialogs ?? throw new ArgumentNullException(nameof(dialogs));
        _timer = timer ?? throw new ArgumentNullException(nameof(timer));
        _timer.Tick += OnTimerTick;

        Seed = 42;
        Width = 30;
        Height = 20;
        RobotCount = 10;
        ShelfDensity = 0.3;
        SelectedPlanner = Planners[0];
        SelectedOrdering = Orderings[0];
        StepsPerSecond = 4;
        Robots = [];
        StatusMessage = "Choose the settings and press Generate.";
    }

    public IReadOnlyList<Choice<PlannerType>> Planners => Names.Planners;
    public IReadOnlyList<Choice<RobotOrdering>> Orderings => Names.Orderings;

    // ---- Settings (two-way bound to the input fields) ----

    [ObservableProperty]
    public partial int Seed { get; set; }

    [ObservableProperty]
    public partial int Width { get; set; }

    [ObservableProperty]
    public partial int Height { get; set; }

    [ObservableProperty]
    public partial int RobotCount { get; set; }

    [ObservableProperty]
    public partial double ShelfDensity { get; set; }

    [ObservableProperty]
    public partial Choice<PlannerType> SelectedPlanner { get; set; }

    [ObservableProperty]
    public partial Choice<RobotOrdering> SelectedOrdering { get; set; }

    /// <summary>Playback speed in time steps per second.</summary>
    [ObservableProperty]
    public partial double StepsPerSecond { get; set; }

    // ---- State shown on screen ----

    [ObservableProperty]
    public partial WarehouseScenario? Scenario { get; private set; }

    [ObservableProperty]
    public partial PlanOutcome? Plan { get; private set; }

    [ObservableProperty]
    public partial int CurrentTime { get; private set; }

    [ObservableProperty]
    public partial bool IsPlaying { get; private set; }

    /// <summary>True while planning runs on a background thread.</summary>
    [ObservableProperty]
    public partial bool IsBusy { get; private set; }

    [ObservableProperty]
    public partial string StatusMessage { get; private set; }

    [ObservableProperty]
    public partial bool IsError { get; private set; }

    /// <summary>Every robot's position at <see cref="CurrentTime"/>.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<RobotState> Robots { get; private set; }

    public bool HasSuccessfulPlan => Plan?.Success == true;

    /// <summary>Last time step of the plan (the makespan), or 0 without a plan.</summary>
    public int LastTime => HasSuccessfulPlan ? Plan!.Metrics.Makespan : 0;

    public string TimeText => HasSuccessfulPlan ? $"Time step {CurrentTime} / {LastTime}" : "Time step –";

    public string PlayPauseText => IsPlaying ? "Pause" : "Play";

    public string PlanningOrderText => Plan is null ? string.Empty : string.Join(", ", Plan.PlanningOrder);

    // ---- Commands ----

    [RelayCommand(CanExecute = nameof(IsIdle))]
    private void Generate()
    {
        StopPlayback();
        string? problem = CheckSettings();
        if (problem is not null)
        {
            ShowError(problem);
            return;
        }

        try
        {
            var options = new GeneratorOptions((uint)Seed, Width, Height, RobotCount, ShelfDensity);
            ShowScenario(_engine.Generate(options));
            ShowInfo($"Generated seed {Seed}: {Width} × {Height} grid, {RobotCount} robots. Press Plan.");
        }
        catch (WarehouseEngineException e)
        {
            ShowError($"Could not generate the scenario: {e.Message}");
        }
    }

    [RelayCommand(CanExecute = nameof(CanPlan))]
    private async Task PlanAsync()
    {
        if (Scenario is not { } scenario)
        {
            return;
        }
        StopPlayback();
        PlannerType planner = SelectedPlanner.Value;
        RobotOrdering ordering = SelectedOrdering.Value;

        IsBusy = true;
        ShowInfo("Planning…");
        try
        {
            // Planning runs on a worker thread so the window stays responsive.
            PlanOutcome outcome = await Task.Run(() => _engine.Plan(scenario, planner, ordering));
            CurrentTime = 0;
            Plan = outcome;
            if (outcome.Success)
            {
                ShowInfo($"Planned {scenario.Robots.Count} robots with {Names.Of(planner)}, " +
                         $"{Names.Of(ordering).ToLowerInvariant()}. Press Play or Step.");
            }
            else
            {
                ShowError($"Planning failed: {outcome.Message}");
            }
        }
        catch (WarehouseEngineException e)
        {
            Plan = null;
            ShowError($"Planning failed: {e.Message}");
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanPlayPause))]
    private void PlayPause()
    {
        if (IsPlaying)
        {
            StopPlayback();
            return;
        }
        if (CurrentTime >= LastTime)
        {
            CurrentTime = 0; // play again from the start
        }
        _timer.Start();
        IsPlaying = true;
    }

    [RelayCommand(CanExecute = nameof(CanStep))]
    private void Step()
    {
        StopPlayback();
        Advance();
    }

    [RelayCommand(CanExecute = nameof(CanReset))]
    private void Reset()
    {
        StopPlayback();
        CurrentTime = 0;
    }

    [RelayCommand(CanExecute = nameof(IsIdle))]
    private void LoadScenario()
    {
        string? path = _dialogs.PickScenarioToOpen();
        if (path is null)
        {
            return;
        }
        StopPlayback();
        try
        {
            WarehouseScenario scenario = _engine.LoadScenario(path);
            Seed = (int)Math.Min(scenario.Seed, int.MaxValue);
            Width = scenario.Width;
            Height = scenario.Height;
            RobotCount = scenario.Robots.Count;
            ShowScenario(scenario);
            ShowInfo($"Loaded {System.IO.Path.GetFileName(path)}. Press Plan.");
        }
        catch (WarehouseEngineException e)
        {
            ShowError($"Could not load the scenario: {e.Message}");
        }
    }

    [RelayCommand(CanExecute = nameof(CanSave))]
    private void SaveScenario()
    {
        if (Scenario is not { } scenario)
        {
            return;
        }
        string? path = _dialogs.PickScenarioToSave($"scenario_seed{scenario.Seed}.txt");
        if (path is null)
        {
            return;
        }
        try
        {
            _engine.SaveScenario(scenario, path);
            ShowInfo($"Saved {System.IO.Path.GetFileName(path)}.");
        }
        catch (WarehouseEngineException e)
        {
            ShowError($"Could not save the scenario: {e.Message}");
        }
    }

    private bool IsIdle() => !IsBusy;
    private bool CanPlan() => !IsBusy && Scenario is not null;
    private bool CanPlayPause() => !IsBusy && HasSuccessfulPlan;
    private bool CanStep() => !IsBusy && HasSuccessfulPlan && CurrentTime < LastTime;
    private bool CanReset() => !IsBusy && HasSuccessfulPlan && (CurrentTime > 0 || IsPlaying);
    private bool CanSave() => !IsBusy && Scenario is not null;

    // ---- Helpers ----

    /// <summary>Returns a message describing the first invalid setting, or null.</summary>
    public string? CheckSettings()
    {
        if (Seed < 0)
        {
            return "Seed must be 0 or more.";
        }
        if (Width < MinWidth || Width > MaxSize)
        {
            return $"Width must be between {MinWidth} and {MaxSize}.";
        }
        if (Height < MinHeight || Height > MaxSize)
        {
            return $"Height must be between {MinHeight} and {MaxSize}.";
        }
        if (RobotCount < 1 || RobotCount > Width - 2)
        {
            return $"Robots must be between 1 and {Width - 2} (width − 2) for this width.";
        }
        if (double.IsNaN(ShelfDensity) || ShelfDensity < 0 || ShelfDensity > MaxShelfDensity)
        {
            return $"Shelf density must be between 0 and {MaxShelfDensity}.";
        }
        return null;
    }

    private void OnTimerTick(object? sender, EventArgs e)
    {
        if (IsPlaying)
        {
            Advance();
        }
    }

    private void Advance()
    {
        if (CurrentTime < LastTime)
        {
            CurrentTime++;
        }
        if (CurrentTime >= LastTime)
        {
            StopPlayback();
        }
    }

    private void StopPlayback()
    {
        _timer.Stop();
        IsPlaying = false;
    }

    private void ShowScenario(WarehouseScenario scenario)
    {
        Plan = null;
        CurrentTime = 0;
        Scenario = scenario;
    }

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

    private void UpdateRobots()
    {
        if (Scenario is not { } scenario)
        {
            Robots = [];
            return;
        }

        var robots = new List<RobotState>(scenario.Robots.Count);
        for (int i = 0; i < scenario.Robots.Count; i++)
        {
            if (HasSuccessfulPlan && i < Plan!.Routes.Count)
            {
                RobotRoute route = Plan.Routes[i];
                GridPosition position = route.Path[Math.Min(CurrentTime, route.Path.Count - 1)];
                robots.Add(new RobotState(i, position, CurrentTime >= route.PickupTime,
                    Delivered: CurrentTime > route.FinishTime));
            }
            else
            {
                robots.Add(new RobotState(i, scenario.Robots[i].Start, false, false));
            }
        }
        Robots = robots;
    }

    private void RefreshCommands()
    {
        GenerateCommand.NotifyCanExecuteChanged();
        PlanCommand.NotifyCanExecuteChanged();
        PlayPauseCommand.NotifyCanExecuteChanged();
        StepCommand.NotifyCanExecuteChanged();
        ResetCommand.NotifyCanExecuteChanged();
        LoadScenarioCommand.NotifyCanExecuteChanged();
        SaveScenarioCommand.NotifyCanExecuteChanged();
    }

    // Generated by the toolkit: called after each property changes.

    partial void OnStepsPerSecondChanged(double value)
    {
        double speed = double.IsNaN(value) ? 1 : Math.Clamp(value, MinSpeed, MaxSpeed);
        _timer.Interval = TimeSpan.FromSeconds(1.0 / speed);
    }

    partial void OnScenarioChanged(WarehouseScenario? value)
    {
        UpdateRobots();
        RefreshCommands();
    }

    partial void OnPlanChanged(PlanOutcome? value)
    {
        UpdateRobots();
        OnPropertyChanged(nameof(HasSuccessfulPlan));
        OnPropertyChanged(nameof(LastTime));
        OnPropertyChanged(nameof(TimeText));
        OnPropertyChanged(nameof(PlanningOrderText));
        RefreshCommands();
    }

    partial void OnCurrentTimeChanged(int value)
    {
        UpdateRobots();
        OnPropertyChanged(nameof(TimeText));
        RefreshCommands();
    }

    partial void OnIsPlayingChanged(bool value)
    {
        OnPropertyChanged(nameof(PlayPauseText));
        RefreshCommands();
    }

    partial void OnIsBusyChanged(bool value) => RefreshCommands();
}

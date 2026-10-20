namespace Warehouse.ViewModels;

/// <summary>Root view model of the main window: one property per tab.</summary>
public sealed class MainViewModel
{
    public MainViewModel(SimulationViewModel simulation, CompareViewModel compare)
    {
        Simulation = simulation ?? throw new ArgumentNullException(nameof(simulation));
        Compare = compare ?? throw new ArgumentNullException(nameof(compare));
    }

    public SimulationViewModel Simulation { get; }

    public CompareViewModel Compare { get; }
}

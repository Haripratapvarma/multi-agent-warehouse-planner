namespace Warehouse.ViewModels;

/// <summary>Root view model of the main window: one property per tab.</summary>
public sealed class MainViewModel
{
    public MainViewModel(SimulationViewModel simulation)
    {
        Simulation = simulation ?? throw new ArgumentNullException(nameof(simulation));
    }

    public SimulationViewModel Simulation { get; }
}

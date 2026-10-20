using System.Windows;
using System.Windows.Threading;
using Warehouse.Desktop.Services;
using Warehouse.Interop;
using Warehouse.ViewModels;

namespace Warehouse.Desktop;

/// <summary>
/// Start-up: creates the engine, the Windows services and the view models,
/// then shows the main window. This is the only place they are wired together.
/// </summary>
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        DispatcherUnhandledException += OnUnhandledException;

        IWarehouseEngine engine;
        try
        {
            engine = new NativeWarehouseEngine();
        }
        catch (InvalidOperationException ex)
        {
            // For example warehouse_engine.dll missing or an old version.
            MessageBox.Show(ex.Message, "Warehouse Planner", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
            return;
        }

        var dialogs = new FileDialogService();
        var simulation = new SimulationViewModel(engine, dialogs, new DispatcherPlaybackTimer());
        var compare = new CompareViewModel(engine, dialogs);
        var window = new MainWindow { DataContext = new MainViewModel(simulation, compare) };
        window.Show();
    }

    private static void OnUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        MessageBox.Show(e.Exception.Message, "Unexpected error", MessageBoxButton.OK, MessageBoxImage.Error);
        e.Handled = true;
    }
}

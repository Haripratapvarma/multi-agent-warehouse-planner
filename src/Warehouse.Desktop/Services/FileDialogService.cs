using System.IO;
using Microsoft.Win32;
using Warehouse.ViewModels.Services;

namespace Warehouse.Desktop.Services;

/// <summary>Standard Windows open/save dialogs for scenario files.</summary>
public sealed class FileDialogService : IFileDialogService
{
    private const string Filter = "Scenario files (*.txt)|*.txt|All files (*.*)|*.*";

    public string? PickScenarioToOpen()
    {
        var dialog = new OpenFileDialog
        {
            Title = "Load scenario",
            Filter = Filter,
            InitialDirectory = ExampleScenarioFolder(),
        };
        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    public string? PickScenarioToSave(string suggestedFileName)
    {
        var dialog = new SaveFileDialog
        {
            Title = "Save scenario",
            Filter = Filter,
            FileName = suggestedFileName,
            DefaultExt = ".txt",
            InitialDirectory = ExampleScenarioFolder(),
        };
        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    /// <summary>
    /// Engine/scenarios of this repository when running from a build folder
    /// (it holds the example files), otherwise the Documents folder.
    /// </summary>
    private static string ExampleScenarioFolder()
    {
        var folder = new DirectoryInfo(AppContext.BaseDirectory);
        for (int level = 0; level < 8 && folder is not null; level++, folder = folder.Parent)
        {
            string candidate = Path.Combine(folder.FullName, "Engine", "scenarios");
            if (Directory.Exists(candidate))
            {
                return candidate;
            }
        }
        return Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
    }
}

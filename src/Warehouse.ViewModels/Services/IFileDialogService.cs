namespace Warehouse.ViewModels.Services;

/// <summary>Asks the user for a file. Returns null when the dialog is cancelled.</summary>
public interface IFileDialogService
{
    string? PickScenarioToOpen();

    string? PickScenarioToSave(string suggestedFileName);

    string? PickCsvToSave(string suggestedFileName);
}

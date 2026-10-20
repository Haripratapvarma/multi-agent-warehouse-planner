namespace Warehouse.ViewModels;

/// <summary>
/// Allowed generator settings. They match the C++ generator, so most
/// mistakes are caught with a friendly message before the engine is called.
/// </summary>
public static class ScenarioLimits
{
    public const int MinWidth = 5;
    public const int MinHeight = 6;
    public const int MaxSize = 100;
    public const double MaxShelfDensity = 0.9;

    /// <summary>Returns a message describing the first invalid setting, or null when all are valid.</summary>
    public static string? Check(int width, int height, int robotCount, double shelfDensity)
    {
        if (width < MinWidth || width > MaxSize)
        {
            return $"Width must be between {MinWidth} and {MaxSize}.";
        }
        if (height < MinHeight || height > MaxSize)
        {
            return $"Height must be between {MinHeight} and {MaxSize}.";
        }
        if (robotCount < 1 || robotCount > width - 2)
        {
            return $"Robots must be between 1 and {width - 2} (width − 2) for this width.";
        }
        if (double.IsNaN(shelfDensity) || shelfDensity < 0 || shelfDensity > MaxShelfDensity)
        {
            return $"Shelf density must be between 0 and {MaxShelfDensity}.";
        }
        return null;
    }
}

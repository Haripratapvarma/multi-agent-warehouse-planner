using System.Runtime.CompilerServices;

namespace Warehouse.Interop.Native;

/// <summary>
/// Checks once that the loaded DLL is the expected version and that every
/// struct has the same size in C# as in C. A mismatch would silently corrupt
/// data, so it is better to fail loudly at startup.
/// </summary>
internal static class NativeLayout
{
    private static readonly Lazy<string?> Problem = new(FindProblem);

    /// <summary>(struct id, C# size) for every struct shared with the DLL.</summary>
    public static IReadOnlyList<(NativeStructId Id, int ManagedSize)> ManagedSizes { get; } =
    [
        (NativeStructId.Cell, Unsafe.SizeOf<WhCell>()),
        (NativeStructId.GeneratorSettings, Unsafe.SizeOf<WhGeneratorSettings>()),
        (NativeStructId.RobotTask, Unsafe.SizeOf<WhRobotTask>()),
        (NativeStructId.ScenarioInfo, Unsafe.SizeOf<WhScenarioInfo>()),
        (NativeStructId.PlanMetrics, Unsafe.SizeOf<WhPlanMetrics>()),
        (NativeStructId.RobotPlanInfo, Unsafe.SizeOf<WhRobotPlanInfo>()),
    ];

    public static void Verify()
    {
        if (Problem.Value is { } problem)
        {
            throw new InvalidOperationException(problem);
        }
    }

    private static string? FindProblem()
    {
        int version;
        try
        {
            version = NativeMethods.wh_api_version();
        }
        catch (DllNotFoundException)
        {
            return "The native engine (warehouse_engine.dll) was not found next to the program. " +
                   "Build the C++ project in Engine/ first, then rebuild the C# projects.";
        }

        if (version != NativeMethods.ExpectedApiVersion)
        {
            return $"Native engine API version is {version}, expected {NativeMethods.ExpectedApiVersion}.";
        }

        foreach ((NativeStructId id, int managedSize) in ManagedSizes)
        {
            int nativeSize = NativeMethods.wh_struct_size((int)id);
            if (nativeSize != managedSize)
            {
                return $"Struct {id} is {nativeSize} bytes in C but {managedSize} bytes in C#.";
            }
        }
        return null;
    }
}

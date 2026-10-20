using System.Runtime.InteropServices;

namespace Warehouse.Interop.Native;

// These structs mirror Engine/include/warehouse/c_api.h field by field.
// They only contain 32/64-bit numbers, so C and C# lay them out the same
// way. NativeLayout.Verify checks the sizes against the DLL at startup.

[StructLayout(LayoutKind.Sequential)]
internal struct WhCell
{
    public int X;
    public int Y;

    public WhCell(int x, int y)
    {
        X = x;
        Y = y;
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct WhGeneratorSettings
{
    public uint Seed;
    public int Width;
    public int Height;
    public int RobotCount;
    public double ShelfDensity;
}

[StructLayout(LayoutKind.Sequential)]
internal struct WhRobotTask
{
    public WhCell Start;
    public WhCell Shelf;
    public WhCell Pickup;
    public WhCell Station;
}

[StructLayout(LayoutKind.Sequential)]
internal struct WhScenarioInfo
{
    public uint Seed;
    public int Width;
    public int Height;
    public int RobotCount;
}

[StructLayout(LayoutKind.Sequential)]
internal struct WhPlanMetrics
{
    public int Status;
    public int Success;
    public int Makespan;
    public int TotalPathLength;
    public int Waits;
    public int FailedRobot;
    public double RuntimeMs;
    public long ExpandedNodes;
}

[StructLayout(LayoutKind.Sequential)]
internal struct WhRobotPlanInfo
{
    public int Planned;
    public int PickupTime;
    public int FinishTime;
    public int PathLength;
}

/// <summary>Struct ids understood by wh_struct_size.</summary>
internal enum NativeStructId
{
    Cell = 0,
    GeneratorSettings = 1,
    RobotTask = 2,
    ScenarioInfo = 3,
    PlanMetrics = 4,
    RobotPlanInfo = 5,
}

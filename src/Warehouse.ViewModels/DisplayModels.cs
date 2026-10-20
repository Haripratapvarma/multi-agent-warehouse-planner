using Warehouse.Interop;

namespace Warehouse.ViewModels;

/// <summary>An item for a drop-down list: the value plus the text shown for it.</summary>
public sealed record Choice<T>(T Value, string Name)
{
    public override string ToString() => Name;
}

/// <summary>Where one robot is at the current time step, for drawing.</summary>
/// <param name="Index">Robot number (0-based), also used to pick its colour.</param>
/// <param name="Position">Cell the robot is in.</param>
/// <param name="HasPickedUp">True once the robot has reached its pickup cell.</param>
/// <param name="Delivered">True once the robot reached its station and left the grid.</param>
public sealed record RobotState(int Index, GridPosition Position, bool HasPickedUp, bool Delivered);

/// <summary>Display names shared by the screens.</summary>
public static class Names
{
    public static IReadOnlyList<Choice<PlannerType>> Planners { get; } =
    [
        new(PlannerType.AStarManhattan, "A* (Manhattan)"),
        new(PlannerType.AStarTrueDistance, "A* (true distance)"),
        new(PlannerType.Bfs, "BFS (no heuristic)"),
    ];

    public static IReadOnlyList<Choice<RobotOrdering>> Orderings { get; } =
    [
        new(RobotOrdering.InputOrder, "Input order"),
        new(RobotOrdering.LongestTaskFirst, "Longest task first"),
    ];

    public static string Of(PlannerType planner) => Planners.First(p => p.Value == planner).Name;

    public static string Of(RobotOrdering ordering) => Orderings.First(o => o.Value == ordering).Name;
}

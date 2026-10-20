using System.Globalization;
using System.Text;
using Warehouse.Interop;

namespace Warehouse.ViewModels;

/// <summary>One planning run inside a comparison: a seed, a planner, an ordering and its metrics.</summary>
public sealed record ComparisonRun(uint Seed, PlannerType Planner, RobotOrdering Ordering, PlanMetrics Metrics);

/// <summary>One row of the comparison table: a planner/ordering pair summarised over all seeds.</summary>
/// <remarks>
/// Success rate is over every seed. The averages only use the seeds where
/// every planner and ordering succeeded, so all rows are averaged over the
/// same scenarios and can be compared fairly. They are null when no seed
/// succeeded for everyone.
/// </remarks>
public sealed record ComparisonRow(
    PlannerType Planner,
    RobotOrdering Ordering,
    int Runs,
    int Successes,
    double? AvgMakespan,
    double? AvgPathLength,
    double? AvgWaits,
    double? AvgRuntimeMs,
    double? AvgExpandedNodes)
{
    public string PlannerName => Names.Of(Planner);
    public string OrderingName => Names.Of(Ordering);
    public double SuccessRate => Runs == 0 ? 0 : (double)Successes / Runs;
}

/// <summary>Builds the comparison table and writes it as CSV. Pure functions, easy to test.</summary>
public static class ComparisonSummary
{
    /// <summary>Every planner/ordering pair, in table order.</summary>
    public static IReadOnlyList<(PlannerType Planner, RobotOrdering Ordering)> Combinations { get; } =
        (from planner in Names.Planners
         from ordering in Names.Orderings
         select (planner.Value, ordering.Value)).ToArray();

    /// <summary>Seeds on which every planner and ordering found a plan.</summary>
    public static IReadOnlySet<uint> CommonSuccessSeeds(IReadOnlyCollection<ComparisonRun> runs) =>
        runs.GroupBy(r => r.Seed)
            .Where(g => g.Count() == Combinations.Count && g.All(r => r.Metrics.Success))
            .Select(g => g.Key)
            .ToHashSet();

    public static IReadOnlyList<ComparisonRow> Summarize(IReadOnlyCollection<ComparisonRun> runs)
    {
        IReadOnlySet<uint> common = CommonSuccessSeeds(runs);
        var rows = new List<ComparisonRow>();
        foreach ((PlannerType planner, RobotOrdering ordering) in Combinations)
        {
            var mine = runs.Where(r => r.Planner == planner && r.Ordering == ordering).ToList();
            var fair = mine.Where(r => common.Contains(r.Seed)).Select(r => r.Metrics).ToList();
            rows.Add(new ComparisonRow(
                planner,
                ordering,
                mine.Count,
                mine.Count(r => r.Metrics.Success),
                Average(fair, m => m.Makespan),
                Average(fair, m => m.TotalPathLength),
                Average(fair, m => m.Waits),
                Average(fair, m => m.RuntimeMs),
                Average(fair, m => m.ExpandedNodes)));
        }
        return rows;
    }

    private static double? Average(List<PlanMetrics> metrics, Func<PlanMetrics, double> value) =>
        metrics.Count == 0 ? null : metrics.Average(value);

    public const string CsvHeader =
        "planner,ordering,runs,successes,success_rate,avg_makespan,avg_path_length,avg_waits,avg_runtime_ms,avg_expanded_nodes";

    /// <summary>CSV with the same columns as the wh_bench tool. Numbers use '.' on every machine.</summary>
    public static string ToCsv(IEnumerable<ComparisonRow> rows)
    {
        var csv = new StringBuilder();
        csv.Append(CsvHeader).Append('\n');
        foreach (ComparisonRow row in rows)
        {
            csv.Append(CsvName(row.Planner)).Append(',')
               .Append(CsvName(row.Ordering)).Append(',')
               .Append(row.Runs.ToString(CultureInfo.InvariantCulture)).Append(',')
               .Append(row.Successes.ToString(CultureInfo.InvariantCulture)).Append(',')
               .Append(Number(row.SuccessRate, 4)).Append(',')
               .Append(Number(row.AvgMakespan, 2)).Append(',')
               .Append(Number(row.AvgPathLength, 2)).Append(',')
               .Append(Number(row.AvgWaits, 2)).Append(',')
               .Append(Number(row.AvgRuntimeMs, 4)).Append(',')
               .Append(Number(row.AvgExpandedNodes, 1)).Append('\n');
        }
        return csv.ToString();
    }

    // Same short names as the C++ tools use.
    private static string CsvName(PlannerType planner) => planner switch
    {
        PlannerType.AStarManhattan => "astar-manhattan",
        PlannerType.AStarTrueDistance => "astar-true-distance",
        PlannerType.Bfs => "bfs",
        _ => planner.ToString(),
    };

    private static string CsvName(RobotOrdering ordering) => ordering switch
    {
        RobotOrdering.InputOrder => "input",
        RobotOrdering.LongestTaskFirst => "longest-first",
        _ => ordering.ToString(),
    };

    // Missing averages are written as empty cells.
    private static string Number(double? value, int decimals) =>
        value is { } v ? v.ToString("F" + decimals, CultureInfo.InvariantCulture) : string.Empty;
}

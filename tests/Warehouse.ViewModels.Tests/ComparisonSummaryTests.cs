using System.Globalization;
using Warehouse.Interop;

namespace Warehouse.ViewModels.Tests;

public class ComparisonSummaryTests
{
    private static PlanMetrics Ok(int makespan, int length, int waits, double ms, long expanded) =>
        new(true, makespan, length, waits, ms, expanded);

    private static readonly PlanMetrics Failed = new(false, 0, 0, 0, 1.0, 50);

    /// <summary>Same metrics for every planner/ordering pair on one seed.</summary>
    private static IEnumerable<ComparisonRun> AllPairs(uint seed, PlanMetrics metrics) =>
        ComparisonSummary.Combinations.Select(c => new ComparisonRun(seed, c.Planner, c.Ordering, metrics));

    [Fact]
    public void Combinations_AreThreePlannersTimesTwoOrderings()
    {
        Assert.Equal(6, ComparisonSummary.Combinations.Count);
        Assert.Equal(6, ComparisonSummary.Combinations.Distinct().Count());
    }

    [Fact]
    public void Summarize_AveragesEachPair()
    {
        var runs = AllPairs(1, Ok(10, 40, 2, 1.0, 100))
            .Concat(AllPairs(2, Ok(20, 60, 4, 3.0, 300)))
            .ToList();

        IReadOnlyList<ComparisonRow> rows = ComparisonSummary.Summarize(runs);

        Assert.Equal(6, rows.Count);
        Assert.All(rows, row =>
        {
            Assert.Equal(2, row.Runs);
            Assert.Equal(2, row.Successes);
            Assert.Equal(1.0, row.SuccessRate);
            Assert.Equal(15.0, row.AvgMakespan);
            Assert.Equal(50.0, row.AvgPathLength);
            Assert.Equal(3.0, row.AvgWaits);
            Assert.Equal(2.0, row.AvgRuntimeMs);
            Assert.Equal(200.0, row.AvgExpandedNodes);
        });
        Assert.Equal("A* (Manhattan)", rows[0].PlannerName);
        Assert.Equal("Input order", rows[0].OrderingName);
    }

    [Fact]
    public void Summarize_AveragesOnlyOverSeedsWhereEveryoneSucceeded()
    {
        // Seed 1: everyone succeeds. Seed 2: BFS + input order fails, so
        // seed 2 is left out of every average (but still counts for success rate).
        var runs = AllPairs(1, Ok(10, 40, 2, 1.0, 100)).ToList();
        foreach (var (planner, ordering) in ComparisonSummary.Combinations)
        {
            bool fails = planner == PlannerType.Bfs && ordering == RobotOrdering.InputOrder;
            runs.Add(new ComparisonRun(2, planner, ordering, fails ? Failed : Ok(99, 999, 9, 9.0, 999)));
        }

        IReadOnlyList<ComparisonRow> rows = ComparisonSummary.Summarize(runs);

        Assert.Equal([1u], ComparisonSummary.CommonSuccessSeeds(runs));
        Assert.All(rows, row => Assert.Equal(10.0, row.AvgMakespan));
        ComparisonRow bfsInput = rows.Single(r => r.Planner == PlannerType.Bfs && r.Ordering == RobotOrdering.InputOrder);
        Assert.Equal(0.5, bfsInput.SuccessRate);
        ComparisonRow other = rows.Single(r => r.Planner == PlannerType.AStarManhattan && r.Ordering == RobotOrdering.InputOrder);
        Assert.Equal(1.0, other.SuccessRate);
    }

    [Fact]
    public void Summarize_NoCommonSeeds_GivesEmptyAverages()
    {
        var runs = AllPairs(1, Failed).ToList();

        IReadOnlyList<ComparisonRow> rows = ComparisonSummary.Summarize(runs);

        Assert.All(rows, row =>
        {
            Assert.Equal(0.0, row.SuccessRate);
            Assert.Null(row.AvgMakespan);
            Assert.Null(row.AvgRuntimeMs);
        });
    }

    [Fact]
    public void Csv_HasHeaderAndOneLinePerRow()
    {
        var runs = AllPairs(1, Ok(10, 40, 2, 1.23456, 100)).ToList();

        string[] lines = ComparisonSummary.ToCsv(ComparisonSummary.Summarize(runs))
            .Split('\n', StringSplitOptions.RemoveEmptyEntries);

        Assert.Equal(7, lines.Length);
        Assert.Equal(ComparisonSummary.CsvHeader, lines[0]);
        Assert.Equal("astar-manhattan,input,1,1,1.0000,10.00,40.00,2.00,1.2346,100.0", lines[1]);
        Assert.Equal("astar-true-distance,longest-first,1,1,1.0000,10.00,40.00,2.00,1.2346,100.0", lines[4]);
        Assert.All(lines, line => Assert.Equal(10, line.Split(',').Length));
    }

    [Fact]
    public void Csv_UsesDotDecimalsEvenOnCommaCultures()
    {
        CultureInfo original = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("de-DE"); // uses ',' for decimals
            string csv = ComparisonSummary.ToCsv(ComparisonSummary.Summarize(AllPairs(1, Ok(10, 40, 2, 1.5, 100)).ToList()));

            Assert.Contains(",1.5000,", csv);
            Assert.DoesNotContain("1,5000", csv);
        }
        finally
        {
            CultureInfo.CurrentCulture = original;
        }
    }

    [Fact]
    public void Csv_MissingAveragesAreEmptyCells()
    {
        string[] lines = ComparisonSummary.ToCsv(ComparisonSummary.Summarize(AllPairs(1, Failed).ToList()))
            .Split('\n', StringSplitOptions.RemoveEmptyEntries);

        Assert.Equal("astar-manhattan,input,1,0,0.0000,,,,,", lines[1]);
    }
}

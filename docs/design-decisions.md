# Design decisions

## Prioritized planning instead of joint planning

Planning all robots jointly is optimal but grows exponentially with the
number of robots. Prioritized planning ("cooperative A*") plans one robot at
a time against the routes already reserved, so each robot costs roughly one
single-agent search over (cell, time). It is fast and simple, and it is easy
to check with a validator.

The price is that it is neither complete nor optimal:

- an earlier robot can block a later one completely, so planning fails even
  though a solution exists;
- each robot takes its best route given the earlier ones, which can make the
  plan as a whole longer than necessary.

**Conflict-Based Search (CBS)** is the standard optimal alternative. It plans
each robot alone, finds the first collision between two routes, and branches
into two subproblems, each forbidding one of the robots from being in that
place at that time. It returns an optimal plan but can explode on crowded
maps. It is a natural next step and fits behind the same validator and
metrics, but it is not implemented.

## Robot ordering

Because results depend on the order, two orderings are offered: the order in
the scenario, and longest task first (by start → pickup → station distance
ignoring other robots). Long tasks are the most likely to become the makespan,
so planning them first gives them the free routes. Ties keep input order
(stable sort) so the result is deterministic.

## Searching over (cell, time)

A robot may wait, so the search state is the pair (cell, time step), and a
wait is a move to the same cell at t + 1. All moves, including waits, cost 1,
so the Manhattan and true-distance estimates never overestimate and stay
consistent.

**Ending the search.** Every search has a hard time limit
(width × height × 4 steps by default). In addition, after the last reserved
time step nothing can block the robot any more, so (cell, t) and
(cell, t + 1) behave the same; all such states are treated as one. This keeps
a search finite and small even when the goal is unreachable, so "no path" is
reported quickly instead of after exploring every time step.

**Two legs.** The route start → pickup is found first, then pickup → station
starting at the pickup time. This keeps each search simple. It means the
earliest pickup is always chosen, even in the rare case where picking up
later would make the second leg shorter.

**Delivered robots leave the grid.** Once a robot reaches its station it is
removed, so it never blocks the station or anything else afterwards. The
validator uses the same rule.

## Reservation table

Two hash sets of 64-bit keys:

- (cell, t) blocks two robots in one cell at the same time;
- (from, to, t) blocks the opposite move by another robot at the same time,
  which would be a head-on swap.

Following directly behind another robot (moving into the cell it is leaving)
is allowed, as in the standard multi-agent path finding model. Waits are not
stored as moves because the cell reservation already covers them.

## Determinism

Same seed + same settings ⇒ identical scenario and identical plan, on every
machine:

- The generator uses `std::mt19937` seeded with the seed (never
  `std::random_device`). `std::uniform_int_distribution` and `std::shuffle`
  are allowed to differ between standard libraries, so the generator does its
  own range reduction and Fisher–Yates shuffle on top of the raw
  `mt19937` output, which the C++ standard fixes exactly.
- Every search breaks ties completely (by f, then g, then cell index) and
  explores neighbours in a fixed order, so the order of hash containers never
  affects the result.

The README's numbers can be reproduced exactly on another machine, except
for runtimes.

## Fair benchmark averages

A planner that fails on the hardest seeds would look better if its averages
were taken only over its own successes. Both `wh_bench` and the Compare tab
therefore report success rate over all seeds, but average every other column
only over the seeds where *every* planner and ordering succeeded, so all rows
describe the same scenarios.

## C interface instead of C++/CLI

A plain C interface works with `LibraryImport` on Windows and Linux, keeps the
C# side independent of the C++ compiler, and makes ownership explicit. Copying
results into C# records costs a little time, but it means no native memory is
held by the UI and the C# objects can be shared across threads safely.

## Keeping the UI testable

View models never reference WPF. Dialogs and the playback timer are
interfaces, and the engine is `IWarehouseEngine`, so tests use fakes and run
on any operating system without opening a window. The fakes can pause the
engine mid-call, which lets tests observe a run in progress and cancel it
deterministically.

## Parallel comparison

The compare screen uses `Parallel.For` with a `CancellationToken` and a
maximum degree of parallelism. The native engine has no shared mutable state
(only a thread-local error message and an atomic object counter), so several
threads can plan at once. Running plans in parallel does not change any
result, only how long each takes: for clean runtime numbers use one thread,
as `wh_bench` does.

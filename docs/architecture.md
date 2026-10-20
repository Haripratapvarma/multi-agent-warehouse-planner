# Architecture

The project has three layers. Each one only talks to the layer below it.

```
┌──────────────────────────────────────────────────────────────┐
│ Warehouse.Desktop (WPF, Windows)                             │
│   MainWindow, SimulationView, CompareView, WarehouseCanvas   │
│   FileDialogService, DispatcherPlaybackTimer                 │
└───────────────▲──────────────────────────────────────────────┘
                │ data binding, commands
┌───────────────┴──────────────────────────────────────────────┐
│ Warehouse.ViewModels (net10.0, no WPF types)                 │
│   SimulationViewModel, CompareViewModel, ComparisonSummary   │
│   IFileDialogService, IPlaybackTimer                         │
└───────────────▲──────────────────────────────────────────────┘
                │ IWarehouseEngine
┌───────────────┴──────────────────────────────────────────────┐
│ Warehouse.Interop (net10.0)                                  │
│   NativeWarehouseEngine, LibraryImport declarations,         │
│   SafeHandles, plain C# data records                         │
└───────────────▲──────────────────────────────────────────────┘
                │ C interface (c_api.h): status codes, opaque handles
┌───────────────┴──────────────────────────────────────────────┐
│ warehouse_engine.dll / libwarehouse_engine.so (C++)          │
│   grid, generator, scenario files, A* / BFS,                 │
│   reservation table, prioritized planner, validator          │
└──────────────────────────────────────────────────────────────┘
```

## C++ engine (`Engine/`)

| File | Responsibility |
|---|---|
| `grid` | Cell types, bounds, passability, row-major indexing |
| `scenario` | Robot tasks (start, shelf, pickup, station) and consistency checks |
| `generator` | Seeded layout and task generation |
| `scenario_io` | Text format for saving and loading scenarios |
| `search` | Single-robot A* and BFS, backward distance maps |
| `reservation_table` | Reserved (cell, time) and (from, to, time) entries |
| `planner` | Leg search over (cell, time), robot ordering, prioritized planning |
| `plan` | Route and metrics types, the plan validator |
| `c_api` | The C interface exported from the DLL |

The engine core (`wh_core`) is a static library. It is linked into the DLL,
the command-line tools and the native tests, so the tests exercise exactly the
code that ships.

## The C interface

`c_api.h` is the only thing the C# side knows about. The rules:

- Every function returns a status code. A planning *failure* (some robot has
  no route) is a normal result reported in the metrics, not an error.
- No C++ exception crosses the boundary: each function body runs inside a
  wrapper that turns exceptions into a status code and stores the message in
  a thread-local "last error".
- Scenarios and plans are opaque handles created and freed by the DLL. A
  live-object counter lets tests prove that every handle is released.
- Structs contain only 32/64-bit numbers. `wh_struct_size` reports their
  sizes so the C# side can check its own layout at start-up.
- Strings are UTF-8 in caller-provided buffers; the function reports the size
  it needs so the caller can retry with a bigger buffer.

## C# interop (`src/Warehouse.Interop`)

`NativeWarehouseEngine` implements `IWarehouseEngine`. Each call creates the
native objects it needs, copies the results into plain C# records
(`WarehouseScenario`, `PlanOutcome`, …) and frees the native objects again.
No native memory outlives a call, and the records can be shared freely between
threads.

Native handles are wrapped in `SafeHandle` subclasses. They are freed on
`Dispose`, by the finalizer if a handle is forgotten, and never while a native
call is still using them.

On first use, `NativeLayout` checks the DLL's API version and compares every
struct size with the C# definition, and fails with a clear message on a
mismatch instead of corrupting data.

The project file copies the native library from `Engine/build/bin` next to
any program that references it (the tests and the desktop app).

## View models (`src/Warehouse.ViewModels`)

All screen logic lives here, written with CommunityToolkit.Mvvm. Nothing in
this project references WPF, so it builds and is tested on any operating
system. Things that need Windows are behind interfaces:

- `IFileDialogService`: open/save dialogs (the tests return preset answers);
- `IPlaybackTimer`: the timer that drives *Play* (the tests tick it by hand).

`SimulationViewModel` generates, plans and plays back one scenario. Planning
runs with `Task.Run` so the window stays responsive.

`CompareViewModel` runs N seeds × 3 planners × 2 orderings:

1. Scenarios are generated with `Parallel.For`.
2. Every (seed, planner, ordering) job is planned with `Parallel.For`, at most
   *Threads* jobs at a time.
3. Each finished job increments a counter with `Interlocked.Increment` and
   reports it through `Progress<int>`, which posts back to the UI thread, so
   worker threads never touch bound properties.
4. The Cancel button cancels a `CancellationToken`; `Parallel.For` stops
   starting new jobs and the run ends with a "Cancelled" message.
5. `ComparisonSummary` (pure functions) builds the table and the CSV.

## Desktop app (`src/Warehouse.Desktop`)

`App.xaml.cs` is the composition root: it creates the engine, the Windows
services and the view models, and shows the window. `WarehouseCanvas` is a
small `FrameworkElement` that draws the grid, the faint planned paths, pickup
markers and numbered robot circles from bound data in `OnRender`.

## Tests and CI

| Suite | What it covers |
|---|---|
| `Engine/tests` (CTest) | grid, generator determinism, A* vs BFS optimality, scenario files, reservation table, validator, cooperative planning, the C interface |
| `tests/Warehouse.Interop.Tests` | interop round trips, struct sizes, error codes → exceptions, SafeHandle freeing, use from many threads |
| `tests/Warehouse.ViewModels.Tests` | simulation screen and compare screen with a fake engine, including cancellation; summary and CSV |

GitHub Actions (`.github/workflows/ci.yml`) builds and tests everything on
Windows, runs the native tests on Linux under AddressSanitizer and
UndefinedBehaviorSanitizer, and runs the cross-platform C# tests on Linux.

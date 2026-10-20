# Multi-Agent Warehouse Planning Simulator

Robot agents plan routes through a grid warehouse: from a start cell to a
pickup cell next to a shelf, then on to a packing station.

The planning engine is written in C++ and lives in `Engine/`.

## Current contents

- Grid world with walls, shelves and packing stations
- Seeded scenario generator (same seed and settings give the same scenario)
- Scenario files in a small text format (`Engine/scenarios/`)
- Single-robot A* search (Manhattan distance heuristic) and BFS for comparison
- Multi-robot prioritized planning ("cooperative A*") over (cell, time) with a
  reservation table that blocks shared cells and head-on swaps
- Three planners: A* with Manhattan distance, A* with true distance, and BFS;
  two robot orderings: input order and longest task first
- A plan validator that checks any plan step by step
- Metrics: makespan, total path length, waits, planning time, success
- `wh_bench` command-line benchmark comparing all planners and orderings
- The engine built as a DLL with a small C interface (`Engine/include/warehouse/c_api.h`)
- A C# wrapper library (`src/Warehouse.Interop`) using `LibraryImport` and
  `SafeHandle`, so native memory is always released
- A WPF desktop app (`src/Warehouse.Desktop`) to configure scenarios, plan them
  and watch the robots move step by step; its screen logic lives in
  `src/Warehouse.ViewModels` (MVVM, no WPF types) so it can be tested
- Native tests using a small self-written test helper, registered with CTest,
  and xUnit tests for the C# wrapper and the view models

## Build and test

Open the repository folder in VS Code with the CMake Tools extension. The
`.vscode/settings.json` file points CMake at `Engine/`. Build, then run the
tests with CTest. All programs are written to `Engine/build/bin`.

From a terminal with CMake available:

```
cmake -S Engine -B Engine/build
cmake --build Engine/build
ctest --test-dir Engine/build
```

### C# projects

Build the C++ engine first: the C# build copies `warehouse_engine.dll`
(`libwarehouse_engine.so` on Linux) from `Engine/build/bin` next to every
program that uses it. Then, from the repository root:

```
dotnet test
```

`Warehouse.CrossPlatform.slnf` lists every C# project except the Windows-only
desktop app, so the libraries and their tests also build on Linux:

```
dotnet test Warehouse.CrossPlatform.slnf
```

### Desktop app (Windows)

```
dotnet run --project src/Warehouse.Desktop
```

## Demo

```
Engine/build/bin/wh_demo --seed 42 --size 20x10 --robots 4 --density 0.3
Engine/build/bin/wh_demo --load Engine/scenarios/narrow_corridor.txt --show-steps
Engine/build/bin/wh_bench --seeds 100 --robots 10 --size 30x20 --csv results.csv
```

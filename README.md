# Multi-Agent Warehouse Planning Simulator

Robot agents plan routes through a grid warehouse: from a start cell to a
pickup cell next to a shelf, then on to a packing station.

The planning engine is written in C++ and lives in `Engine/`.

## Current contents

- Grid world with walls, shelves and packing stations
- Seeded scenario generator (same seed and settings give the same scenario)
- Scenario files in a small text format (`Engine/scenarios/`)
- Single-robot A* search (Manhattan distance heuristic) and BFS for comparison
- Native tests using a small self-written test helper, registered with CTest

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

## Demo

```
Engine/build/bin/wh_demo --seed 42 --size 20x10 --robots 4 --density 0.3
Engine/build/bin/wh_demo --load Engine/scenarios/narrow_corridor.txt
```

cmake_minimum_required(VERSION 3.20)
project(WarehousePlanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put every executable (and later the DLL) in build/bin, including for
# multi-config generators such as Visual Studio, which would otherwise
# add a Debug/ or Release/ subfolder.
set(WH_BIN_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${WH_BIN_DIR})
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${WH_BIN_DIR})
endforeach()

option(WH_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang only)" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(WH_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ---- Core engine library ----------------------------------------------------
add_library(wh_core STATIC
    src/grid.cpp
    src/scenario.cpp
    src/generator.cpp
    src/search.cpp
    src/scenario_io.cpp
    src/reservation_table.cpp
    src/plan.cpp
    src/planner.cpp
)
target_include_directories(wh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Needed later so the core can be linked into a shared library on Linux.
set_target_properties(wh_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Command-line tools ----------------------------------------------------
add_executable(wh_demo tools/wh_demo.cpp)
target_link_libraries(wh_demo PRIVATE wh_core)

add_executable(wh_bench tools/wh_bench.cpp)
target_link_libraries(wh_bench PRIVATE wh_core)

# ---- Tests ------------------------------------------------------------------
# include(CTest) enables testing and writes DartConfiguration.tcl, which stops
# a harmless "Cannot find file" message when VS Code runs the tests.
# The property stops it adding extra dashboard targets to the build.
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
include(CTest)

function(wh_add_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE wh_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE
        WH_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenarios")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

wh_add_test(test_grid)
wh_add_test(test_generator)
wh_add_test(test_astar)
wh_add_test(test_scenario_io)
wh_add_test(test_reservation_table)
wh_add_test(test_validator)
wh_add_test(test_cooperative)

// wh_demo: generates or loads a warehouse scenario, prints the map,
// compares single-robot A* with BFS for every robot's two legs
// (start -> pickup -> station), then plans all robots together without
// collisions and checks the plan with the validator.
//
// Examples:
//   wh_demo --seed 42 --size 20x10 --robots 4 --density 0.3
//   wh_demo --load Engine/scenarios/narrow_corridor.txt --show-steps
//   wh_demo --seed 7 --size 12x8 --robots 3 --planner bfs --order longest-first
//   wh_demo --seed 7 --size 12x8 --robots 3 --save my_scenario.txt

#include "warehouse/generator.hpp"
#include "warehouse/planner.hpp"
#include "warehouse/scenario_io.hpp"
#include "warehouse/search.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Options {
    wh::GeneratorSettings settings{42, 20, 10, 4, 0.3};
    std::string loadPath;
    std::string savePath;
    wh::PlanOptions plan;
    bool showSteps = false;
    bool help = false;
};

void printUsage()
{
    std::cout << "Usage: wh_demo [options]\n"
                 "  --seed N           random seed (default 42)\n"
                 "  --size WxH         grid size, e.g. 20x10 (default 20x10)\n"
                 "  --robots N         number of robots (default 4)\n"
                 "  --density D        shelf density 0.0-0.9 (default 0.3)\n"
                 "  --load FILE        load a scenario file instead of generating one\n"
                 "  --save FILE        save the scenario to a file\n"
                 "  --planner NAME     astar-manhattan (default), astar-true-distance or bfs\n"
                 "  --order NAME       input (default) or longest-first\n"
                 "  --show-steps       print the map at every time step of the plan\n"
                 "  --help             show this message\n";
}

Options parseArguments(int argc, char** argv)
{
    Options options;
    auto value = [&](int& i) -> std::string {
        if (i + 1 >= argc) {
            throw std::invalid_argument(std::string("missing value after ") + argv[i]);
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed") {
            options.settings.seed = static_cast<std::uint32_t>(std::stoul(value(i)));
        } else if (arg == "--size") {
            const std::string size = value(i);
            const auto x = size.find('x');
            if (x == std::string::npos) {
                throw std::invalid_argument("size must look like 20x10");
            }
            options.settings.width = std::stoi(size.substr(0, x));
            options.settings.height = std::stoi(size.substr(x + 1));
        } else if (arg == "--robots") {
            options.settings.robotCount = std::stoi(value(i));
        } else if (arg == "--density") {
            options.settings.shelfDensity = std::stod(value(i));
        } else if (arg == "--load") {
            options.loadPath = value(i);
        } else if (arg == "--save") {
            options.savePath = value(i);
        } else if (arg == "--planner") {
            const auto planner = wh::plannerFromString(value(i));
            if (!planner) {
                throw std::invalid_argument("unknown planner (use astar-manhattan, astar-true-distance or bfs)");
            }
            options.plan.planner = *planner;
        } else if (arg == "--order") {
            const auto order = wh::orderFromString(value(i));
            if (!order) {
                throw std::invalid_argument("unknown order (use input or longest-first)");
            }
            options.plan.order = *order;
        } else if (arg == "--show-steps") {
            options.showSteps = true;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else {
            throw std::invalid_argument("unknown option " + arg);
        }
    }
    return options;
}

// Robot start cells are shown as digits (robot number modulo 10).
void printMap(const wh::Scenario& scenario)
{
    std::vector<std::string> rows;
    for (int y = 0; y < scenario.grid.height(); ++y) {
        std::string row;
        for (int x = 0; x < scenario.grid.width(); ++x) {
            row += wh::toChar(scenario.grid.at({x, y}));
        }
        rows.push_back(row);
    }
    for (std::size_t i = 0; i < scenario.robots.size(); ++i) {
        const wh::Cell start = scenario.robots[i].start;
        rows[static_cast<std::size_t>(start.y)][static_cast<std::size_t>(start.x)] =
            static_cast<char>('0' + i % 10);
    }
    for (const std::string& row : rows) {
        std::cout << "  " << row << '\n';
    }
}

// Shows one robot's full route: '*' path, 'R' start, 'p' pickup, 'P' station.
void printRoute(const wh::Scenario& scenario, const std::vector<wh::Cell>& route, const wh::RobotTask& task)
{
    std::vector<std::string> rows;
    for (int y = 0; y < scenario.grid.height(); ++y) {
        std::string row;
        for (int x = 0; x < scenario.grid.width(); ++x) {
            row += wh::toChar(scenario.grid.at({x, y}));
        }
        rows.push_back(row);
    }
    auto mark = [&](wh::Cell c, char ch) {
        rows[static_cast<std::size_t>(c.y)][static_cast<std::size_t>(c.x)] = ch;
    };
    for (wh::Cell c : route) {
        mark(c, '*');
    }
    mark(task.start, 'R');
    mark(task.pickup, 'p');
    mark(task.station, 'P');
    for (const std::string& row : rows) {
        std::cout << "  " << row << '\n';
    }
}

std::string lengthText(const wh::PathResult& result)
{
    return result.found() ? std::to_string(result.length()) : std::string("no path");
}

void printPlan(const wh::Scenario& scenario, const wh::PlanResult& result)
{
    std::cout << "Planning order:";
    for (int robot : result.planningOrder) {
        std::cout << ' ' << robot;
    }
    std::cout << '\n';
    if (!result.success()) {
        std::cout << "Planning FAILED: " << result.message << '\n';
        return;
    }

    std::cout << std::left << std::setw(7) << "Robot" << std::right << std::setw(13) << "Pickup at t"
              << std::setw(13) << "Done at t" << std::setw(8) << "Waits" << '\n';
    for (std::size_t i = 0; i < result.robots.size(); ++i) {
        const wh::RobotPlan& robot = result.robots[i];
        int waits = 0;
        for (std::size_t t = 1; t < robot.path.size(); ++t) {
            waits += robot.path[t] == robot.path[t - 1] ? 1 : 0;
        }
        std::cout << std::left << std::setw(7) << i << std::right << std::setw(13) << robot.pickupTime
                  << std::setw(13) << robot.finishTime << std::setw(8) << waits << '\n';
    }

    const wh::PlanMetrics& m = result.metrics;
    std::cout << "Makespan " << m.makespan << ", total path length " << m.totalPathLength << ", waits " << m.waits
              << ", cells expanded " << m.expandedNodes << ", planning time " << std::fixed << std::setprecision(3)
              << m.runtimeMs << " ms\n";
    std::cout.unsetf(std::ios::fixed);
    const wh::ValidationResult check = wh::validatePlan(scenario, result.robots);
    std::cout << "Validator: " << (check.valid ? "plan is collision-free" : "INVALID - " + check.message) << '\n';
}

// Prints the grid at every time step with robots drawn as digits.
void printSteps(const wh::Scenario& scenario, const wh::PlanResult& result)
{
    for (int t = 0; t <= result.metrics.makespan; ++t) {
        std::vector<std::string> rows;
        for (int y = 0; y < scenario.grid.height(); ++y) {
            std::string row;
            for (int x = 0; x < scenario.grid.width(); ++x) {
                row += wh::toChar(scenario.grid.at({x, y}));
            }
            rows.push_back(row);
        }
        for (std::size_t i = 0; i < result.robots.size(); ++i) {
            const std::vector<wh::Cell>& path = result.robots[i].path;
            if (static_cast<std::size_t>(t) < path.size()) {
                const wh::Cell c = path[static_cast<std::size_t>(t)];
                rows[static_cast<std::size_t>(c.y)][static_cast<std::size_t>(c.x)] = static_cast<char>('0' + i % 10);
            }
        }
        std::cout << "t = " << t << '\n';
        for (const std::string& row : rows) {
            std::cout << "  " << row << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parseArguments(argc, argv);
        if (options.help) {
            printUsage();
            return 0;
        }

        wh::Scenario scenario;
        if (!options.loadPath.empty()) {
            scenario = wh::loadScenarioFile(options.loadPath);
            std::cout << "Loaded scenario: " << options.loadPath << '\n';
        } else {
            scenario = wh::generateScenario(options.settings);
            std::cout << "Generated scenario: seed " << options.settings.seed << ", "
                      << options.settings.width << 'x' << options.settings.height << ", "
                      << options.settings.robotCount << " robots, shelf density "
                      << options.settings.shelfDensity << '\n';
        }
        std::cout << "Grid " << scenario.grid.width() << 'x' << scenario.grid.height() << ", "
                  << scenario.robots.size() << " robots ('#' wall, 'S' shelf, 'P' station, digits = robot starts)\n";
        printMap(scenario);

        if (!options.savePath.empty()) {
            wh::saveScenarioFile(options.savePath, scenario);
            std::cout << "Saved scenario to " << options.savePath << '\n';
        }

        std::cout << "\nSingle-robot shortest paths (each robot planned alone, ignoring the others)\n";
        std::cout << std::left << std::setw(7) << "Robot" << std::setw(18) << "Leg" << std::right
                  << std::setw(9) << "A* len" << std::setw(13) << "A* expanded" << std::setw(10)
                  << "BFS len" << std::setw(14) << "BFS expanded" << '\n';

        bool allMatch = true;
        bool allFound = true;
        int totalAStar = 0;
        int totalBfs = 0;
        std::vector<wh::Cell> firstRoute;

        for (std::size_t i = 0; i < scenario.robots.size(); ++i) {
            const wh::RobotTask& task = scenario.robots[i];
            const struct {
                const char* name;
                wh::Cell from;
                wh::Cell to;
            } legs[] = {{"start->pickup", task.start, task.pickup}, {"pickup->station", task.pickup, task.station}};

            for (const auto& leg : legs) {
                const wh::PathResult astar = wh::findPathAStar(scenario.grid, leg.from, leg.to);
                const wh::PathResult bfs = wh::findPathBfs(scenario.grid, leg.from, leg.to);
                allMatch = allMatch && astar.status == bfs.status && astar.length() == bfs.length();
                allFound = allFound && astar.found();
                totalAStar += astar.expandedNodes;
                totalBfs += bfs.expandedNodes;

                std::cout << std::left << std::setw(7) << i << std::setw(18) << leg.name << std::right
                          << std::setw(9) << lengthText(astar) << std::setw(13) << astar.expandedNodes
                          << std::setw(10) << lengthText(bfs) << std::setw(14) << bfs.expandedNodes << '\n';

                if (i == 0 && astar.found()) {
                    firstRoute.insert(firstRoute.end(), astar.path.begin(), astar.path.end());
                }
            }
        }

        std::cout << "\nTotal cells expanded: A* " << totalAStar << ", BFS " << totalBfs << '\n';
        std::cout << "A* path lengths match BFS: " << (allMatch ? "yes" : "NO") << '\n';

        if (!firstRoute.empty()) {
            std::cout << "\nRoute of robot 0 ('R' start, 'p' pickup, 'P' station, '*' path)\n";
            printRoute(scenario, firstRoute, scenario.robots[0]);
        }

        std::cout << "\nCooperative plan: " << wh::displayName(options.plan.planner) << ", "
                  << wh::displayName(options.plan.order) << '\n';
        const wh::PlanResult plan = wh::planScenario(scenario, options.plan);
        printPlan(scenario, plan);
        if (options.showSteps && plan.success()) {
            std::cout << "\nStep by step (digits = robots; a robot disappears once it delivers)\n";
            printSteps(scenario, plan);
        }

        return (allMatch && allFound) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        std::cerr << "Run with --help to see the options.\n";
        return 2;
    }
}

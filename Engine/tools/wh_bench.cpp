// wh_bench: runs every planner with both robot orderings on many seeded
// scenarios and prints a comparison table (optionally also a CSV file).
//
// Example:
//   wh_bench --seeds 100 --robots 10 --size 30x20
//   wh_bench --seeds 100 --robots 10 --size 30x20 --csv results.csv

#include "warehouse/generator.hpp"
#include "warehouse/planner.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
    int seeds = 100;
    std::uint32_t firstSeed = 1;
    int width = 30;
    int height = 20;
    int robots = 10;
    double density = 0.3;
    std::string csvPath;
    bool help = false;
};

void printUsage()
{
    std::cout << "Usage: wh_bench [options]\n"
                 "  --seeds N          number of scenarios (default 100)\n"
                 "  --first-seed N     first seed; seeds run N, N+1, ... (default 1)\n"
                 "  --size WxH         grid size (default 30x20)\n"
                 "  --robots N         robots per scenario (default 10)\n"
                 "  --density D        shelf density 0.0-0.9 (default 0.3)\n"
                 "  --csv FILE         also write the results table to a CSV file\n"
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
        if (arg == "--seeds") {
            options.seeds = std::stoi(value(i));
        } else if (arg == "--first-seed") {
            options.firstSeed = static_cast<std::uint32_t>(std::stoul(value(i)));
        } else if (arg == "--size") {
            const std::string size = value(i);
            const auto x = size.find('x');
            if (x == std::string::npos) {
                throw std::invalid_argument("size must look like 30x20");
            }
            options.width = std::stoi(size.substr(0, x));
            options.height = std::stoi(size.substr(x + 1));
        } else if (arg == "--robots") {
            options.robots = std::stoi(value(i));
        } else if (arg == "--density") {
            options.density = std::stod(value(i));
        } else if (arg == "--csv") {
            options.csvPath = value(i);
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else {
            throw std::invalid_argument("unknown option " + arg);
        }
    }
    if (options.seeds < 1) {
        throw std::invalid_argument("--seeds must be at least 1");
    }
    return options;
}

struct Combination {
    wh::PlannerKind planner;
    wh::RobotOrder order;
    std::vector<wh::PlanMetrics> runs; // one per seed, in seed order
};

struct Summary {
    int runs = 0;
    int successes = 0;
    double makespan = 0;
    double pathLength = 0;
    double waits = 0;
    double runtimeMs = 0;
    double expanded = 0;
};

// Success rate uses every run. The other averages only use seeds where
// every planner/ordering succeeded, so all rows are averaged over the same
// scenarios and can be compared fairly.
Summary summarize(const Combination& combination, const std::vector<bool>& commonSeed)
{
    Summary s;
    int counted = 0;
    for (std::size_t i = 0; i < combination.runs.size(); ++i) {
        const wh::PlanMetrics& m = combination.runs[i];
        ++s.runs;
        s.successes += m.success ? 1 : 0;
        if (commonSeed[i]) {
            ++counted;
            s.makespan += m.makespan;
            s.pathLength += m.totalPathLength;
            s.waits += m.waits;
            s.runtimeMs += m.runtimeMs;
            s.expanded += static_cast<double>(m.expandedNodes);
        }
    }
    if (counted > 0) {
        s.makespan /= counted;
        s.pathLength /= counted;
        s.waits /= counted;
        s.runtimeMs /= counted;
        s.expanded /= counted;
    }
    return s;
}

std::string fixed(double value, int decimals)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
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

        std::vector<Combination> combinations;
        for (wh::PlannerKind planner : wh::kAllPlanners) {
            for (wh::RobotOrder order : wh::kAllOrders) {
                combinations.push_back({planner, order, {}});
            }
        }

        std::cout << "Running " << options.seeds << " seeds (" << options.firstSeed << " to "
                  << options.firstSeed + static_cast<std::uint32_t>(options.seeds) - 1 << "), " << options.width << 'x'
                  << options.height << " grid, " << options.robots << " robots, shelf density " << options.density
                  << " ...\n";

        int invalidPlans = 0;
        std::vector<bool> commonSeed;
        for (int i = 0; i < options.seeds; ++i) {
            wh::GeneratorSettings settings;
            settings.seed = options.firstSeed + static_cast<std::uint32_t>(i);
            settings.width = options.width;
            settings.height = options.height;
            settings.robotCount = options.robots;
            settings.shelfDensity = options.density;
            const wh::Scenario scenario = wh::generateScenario(settings);

            bool allSucceeded = true;
            for (Combination& combination : combinations) {
                const wh::PlanResult result = wh::planScenario(scenario, {combination.planner, combination.order, 0});
                // Every successful plan is checked independently.
                if (result.success() && !wh::validatePlan(scenario, result.robots)) {
                    ++invalidPlans;
                }
                allSucceeded = allSucceeded && result.success();
                combination.runs.push_back(result.metrics);
            }
            commonSeed.push_back(allSucceeded);
        }

        int commonCount = 0;
        for (bool common : commonSeed) {
            commonCount += common ? 1 : 0;
        }

        std::cout << '\n'
                  << std::left << std::setw(20) << "Planner" << std::setw(15) << "Ordering" << std::right
                  << std::setw(9) << "Success" << std::setw(10) << "Makespan" << std::setw(12) << "Path length"
                  << std::setw(8) << "Waits" << std::setw(13) << "Runtime ms" << std::setw(11) << "Expanded"
                  << '\n';
        std::cout << std::string(98, '-') << '\n';

        std::ofstream csv;
        if (!options.csvPath.empty()) {
            csv.open(options.csvPath);
            if (!csv) {
                throw std::runtime_error("cannot write " + options.csvPath);
            }
            csv << "planner,ordering,runs,successes,success_rate,avg_makespan,avg_path_length,avg_waits,"
                   "avg_runtime_ms,avg_expanded_nodes\n";
        }

        for (const Combination& combination : combinations) {
            const Summary s = summarize(combination, commonSeed);
            const double rate = 100.0 * s.successes / s.runs;
            std::cout << std::left << std::setw(20) << wh::displayName(combination.planner) << std::setw(15)
                      << wh::displayName(combination.order) << std::right << std::setw(8) << fixed(rate, 1) << '%'
                      << std::setw(10) << fixed(s.makespan, 1) << std::setw(12) << fixed(s.pathLength, 1)
                      << std::setw(8) << fixed(s.waits, 1) << std::setw(13) << fixed(s.runtimeMs, 3) << std::setw(11)
                      << fixed(s.expanded, 0) << '\n';
            if (csv) {
                csv << wh::toString(combination.planner) << ',' << wh::toString(combination.order) << ',' << s.runs
                    << ',' << s.successes << ',' << fixed(rate / 100.0, 4) << ',' << fixed(s.makespan, 2) << ','
                    << fixed(s.pathLength, 2) << ',' << fixed(s.waits, 2) << ',' << fixed(s.runtimeMs, 4) << ','
                    << fixed(s.expanded, 1) << '\n';
            }
        }

        std::cout << "\nSuccess is over all " << options.seeds << " seeds. Other columns are averages over the "
                  << commonCount << " seeds where every planner and ordering succeeded.\n";
        std::cout << "Path length = total steps of all robots (waits included). Runtime = planning time per scenario.\n";
        std::cout << "All successful plans passed the validator: " << (invalidPlans == 0 ? "yes" : "NO") << '\n';
        if (!options.csvPath.empty()) {
            std::cout << "Wrote " << options.csvPath << '\n';
        }
        return invalidPlans == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        std::cerr << "Run with --help to see the options.\n";
        return 2;
    }
}

#pragma once

#include "warehouse/scenario.hpp"

#include <filesystem>
#include <iosfwd>
#include <stdexcept>
#include <string>

namespace wh {

// Thrown when a scenario file cannot be read. line() is the 1-based line
// number of the problem, or 0 when the problem is with the file as a whole.
class ScenarioFormatError : public std::runtime_error {
public:
    ScenarioFormatError(int line, const std::string& message);
    int line() const { return line_; }

private:
    int line_;
};

// Text format (see Engine/scenarios for examples):
//
//   warehouse-scenario 1
//   seed 42
//   size <width> <height>
//   map
//   <height rows of width characters: '#' wall, '.' free, 'S' shelf, 'P' station>
//   end
//   robots <count>
//   robot start <x> <y> shelf <x> <y> pickup <x> <y> station <x> <y>
//
// Lines starting with '#' outside the map block are comments.
void writeScenario(std::ostream& out, const Scenario& scenario);
Scenario readScenario(std::istream& in);

std::string scenarioToString(const Scenario& scenario);
Scenario scenarioFromString(const std::string& text);

void saveScenarioFile(const std::filesystem::path& path, const Scenario& scenario);
Scenario loadScenarioFile(const std::filesystem::path& path);

} // namespace wh

#include "warehouse/scenario_io.hpp"

#include <fstream>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace wh {

namespace {

constexpr const char* kHeader = "warehouse-scenario";
constexpr int kFormatVersion = 1;

std::string withLine(int line, const std::string& message)
{
    if (line <= 0) {
        return message;
    }
    return "line " + std::to_string(line) + ": " + message;
}

// Reads lines one at a time, tracking the line number and removing a
// trailing '\r' so files with Windows line endings load the same way.
class LineReader {
public:
    explicit LineReader(std::istream& in) : in_(in) {}

    bool next(std::string& line)
    {
        if (!std::getline(in_, line)) {
            return false;
        }
        ++lineNumber_;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return true;
    }

    // Next line that is not blank and not a comment.
    bool nextContent(std::string& line)
    {
        while (next(line)) {
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line[first] != '#') {
                return true;
            }
        }
        return false;
    }

    int lineNumber() const { return lineNumber_; }

private:
    std::istream& in_;
    int lineNumber_ = 0;
};

class Parser {
public:
    explicit Parser(std::istream& in) : reader_(in) {}

    Scenario parse()
    {
        Scenario scenario;

        auto header = expectLine("the 'warehouse-scenario 1' header");
        if (word(header) != kHeader || number(header, "format version") != kFormatVersion) {
            fail("expected 'warehouse-scenario 1'");
        }

        auto seedLine = expectKeyword("seed");
        const long long seed = number(seedLine, "seed");
        if (seed < 0 || seed > 0xFFFFFFFFLL) {
            fail("seed must be between 0 and 4294967295");
        }
        scenario.seed = static_cast<std::uint32_t>(seed);

        auto sizeLine = expectKeyword("size");
        const int width = smallNumber(sizeLine, "width");
        const int height = smallNumber(sizeLine, "height");
        if (width <= 0 || height <= 0) {
            fail("width and height must be positive");
        }
        scenario.grid = Grid(width, height);

        expectKeyword("map");
        readMap(scenario.grid);

        auto robotsLine = expectKeyword("robots");
        const int robotCount = smallNumber(robotsLine, "robot count");
        if (robotCount < 0) {
            fail("robot count cannot be negative");
        }
        for (int i = 0; i < robotCount; ++i) {
            auto robotLine = expectKeyword("robot");
            scenario.robots.push_back(readRobot(robotLine));
        }

        std::string extra;
        if (reader_.nextContent(extra)) {
            fail("unexpected text after the last robot");
        }

        const std::string problem = validateScenario(scenario);
        if (!problem.empty()) {
            throw ScenarioFormatError(0, "invalid scenario: " + problem);
        }
        return scenario;
    }

private:
    [[noreturn]] void fail(const std::string& message) const
    {
        throw ScenarioFormatError(reader_.lineNumber(), message);
    }

    std::istringstream expectLine(const std::string& what)
    {
        std::string line;
        if (!reader_.nextContent(line)) {
            throw ScenarioFormatError(reader_.lineNumber(), "file ended before " + what);
        }
        return std::istringstream(line);
    }

    std::istringstream expectKeyword(const std::string& keyword)
    {
        auto line = expectLine("'" + keyword + "'");
        const std::string found = word(line);
        if (found != keyword) {
            fail("expected '" + keyword + "' but found '" + found + "'");
        }
        return line;
    }

    std::string word(std::istringstream& line) const
    {
        std::string text;
        line >> text;
        return text;
    }

    long long number(std::istringstream& line, const std::string& what) const
    {
        long long value = 0;
        if (!(line >> value)) {
            fail("expected a number for " + what);
        }
        return value;
    }

    int smallNumber(std::istringstream& line, const std::string& what) const
    {
        const long long value = number(line, what);
        if (value < -100000 || value > 100000) {
            fail(what + " is out of range");
        }
        return static_cast<int>(value);
    }

    void readMap(Grid& grid)
    {
        // Map rows are read exactly as written; '#' here is a wall, not a comment.
        for (int y = 0; y < grid.height(); ++y) {
            std::string row;
            if (!reader_.next(row)) {
                fail("file ended inside the map");
            }
            if (static_cast<int>(row.size()) != grid.width()) {
                fail("map row has " + std::to_string(row.size()) + " characters, expected " +
                     std::to_string(grid.width()));
            }
            for (int x = 0; x < grid.width(); ++x) {
                const std::optional<CellType> type = cellTypeFromChar(row[static_cast<std::size_t>(x)]);
                if (!type) {
                    fail(std::string("unknown map character '") + row[static_cast<std::size_t>(x)] + "'");
                }
                grid.set({x, y}, *type);
            }
        }
        expectKeyword("end");
    }

    Cell namedCell(std::istringstream& line, const std::string& name) const
    {
        const std::string found = word(line);
        if (found != name) {
            fail("expected '" + name + "' in robot line");
        }
        Cell cell;
        cell.x = smallNumber(line, name + " x");
        cell.y = smallNumber(line, name + " y");
        return cell;
    }

    RobotTask readRobot(std::istringstream& line) const
    {
        RobotTask task;
        task.start = namedCell(line, "start");
        task.shelf = namedCell(line, "shelf");
        task.pickup = namedCell(line, "pickup");
        task.station = namedCell(line, "station");
        return task;
    }

    LineReader reader_;
};

void writeCell(std::ostream& out, const char* name, Cell cell)
{
    out << ' ' << name << ' ' << cell.x << ' ' << cell.y;
}

} // namespace

ScenarioFormatError::ScenarioFormatError(int line, const std::string& message)
    : std::runtime_error(withLine(line, message)), line_(line)
{
}

void writeScenario(std::ostream& out, const Scenario& scenario)
{
    out << "# Warehouse scenario\n";
    out << "# Map: '#' wall, '.' free, 'S' shelf, 'P' packing station\n";
    out << kHeader << ' ' << kFormatVersion << '\n';
    out << "seed " << scenario.seed << '\n';
    out << "size " << scenario.grid.width() << ' ' << scenario.grid.height() << '\n';
    out << "map\n";
    for (int y = 0; y < scenario.grid.height(); ++y) {
        for (int x = 0; x < scenario.grid.width(); ++x) {
            out << toChar(scenario.grid.at({x, y}));
        }
        out << '\n';
    }
    out << "end\n";
    out << "robots " << scenario.robots.size() << '\n';
    for (const RobotTask& task : scenario.robots) {
        out << "robot";
        writeCell(out, "start", task.start);
        writeCell(out, "shelf", task.shelf);
        writeCell(out, "pickup", task.pickup);
        writeCell(out, "station", task.station);
        out << '\n';
    }
}

Scenario readScenario(std::istream& in)
{
    return Parser(in).parse();
}

std::string scenarioToString(const Scenario& scenario)
{
    std::ostringstream out;
    writeScenario(out, scenario);
    return out.str();
}

Scenario scenarioFromString(const std::string& text)
{
    std::istringstream in(text);
    return readScenario(in);
}

void saveScenarioFile(const std::filesystem::path& path, const Scenario& scenario)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open '" + path.string() + "' for writing");
    }
    writeScenario(out, scenario);
    if (!out) {
        throw std::runtime_error("failed to write '" + path.string() + "'");
    }
}

Scenario loadScenarioFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open '" + path.string() + "'");
    }
    return readScenario(in);
}

} // namespace wh

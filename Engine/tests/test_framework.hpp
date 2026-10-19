#pragma once

// A tiny self-written test helper, so the engine has no external test
// dependencies. Each test file defines tests with TEST_CASE and ends with
//
//   int main() { return wh::test::runAll(); }
//
// The program prints one line per test and returns 0 only if all pass,
// which is what CTest checks.

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace wh::test {

struct TestCase {
    const char* name;
    void (*function)();
};

// Thrown by REQUIRE to stop the current test early.
struct AbortTest {};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& currentFailures()
{
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, void (*function)()) { registry().push_back({name, function}); }
};

inline const char* fileName(const char* path)
{
    const char* name = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

inline void reportFailure(const char* file, int line, const std::string& message)
{
    ++currentFailures();
    std::cout << "    " << fileName(file) << ":" << line << ": " << message << '\n';
}

inline int runAll()
{
    int failedTests = 0;
    for (const TestCase& test : registry()) {
        currentFailures() = 0;
        try {
            test.function();
        } catch (const AbortTest&) {
            // failure already reported
        } catch (const std::exception& e) {
            reportFailure("test", 0, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            reportFailure("test", 0, "unexpected unknown exception");
        }
        const bool passed = currentFailures() == 0;
        if (!passed) {
            ++failedTests;
        }
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << test.name << '\n';
    }
    std::cout << registry().size() << " tests, " << failedTests << " failed\n";
    return failedTests == 0 ? 0 : 1;
}

} // namespace wh::test

#define TEST_CASE(name)                                               \
    static void name();                                               \
    static const wh::test::Registrar name##_registrar(#name, &name);  \
    static void name()

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            wh::test::reportFailure(__FILE__, __LINE__, "CHECK failed: " #condition); \
        }                                                                             \
    } while (0)

#define REQUIRE(condition)                                                              \
    do {                                                                                \
        if (!(condition)) {                                                             \
            wh::test::reportFailure(__FILE__, __LINE__, "REQUIRE failed: " #condition); \
            throw wh::test::AbortTest{};                                                \
        }                                                                               \
    } while (0)

#define CHECK_EQ(actual, expected)                                                         \
    do {                                                                                   \
        const auto& wh_actual = (actual);                                                  \
        const auto& wh_expected = (expected);                                              \
        if (!(wh_actual == wh_expected)) {                                                 \
            std::ostringstream wh_message;                                                 \
            wh_message << "CHECK_EQ failed: " #actual " == " #expected " (got " << wh_actual \
                       << ", expected " << wh_expected << ")";                             \
            wh::test::reportFailure(__FILE__, __LINE__, wh_message.str());                 \
        }                                                                                  \
    } while (0)

#define CHECK_THROWS(expression, ExceptionType)                                              \
    do {                                                                                     \
        bool wh_threw = false;                                                               \
        try {                                                                                \
            (void)(expression);                                                              \
        } catch (const ExceptionType&) {                                                     \
            wh_threw = true;                                                                 \
        } catch (...) {                                                                      \
        }                                                                                    \
        if (!wh_threw) {                                                                     \
            wh::test::reportFailure(__FILE__, __LINE__,                                      \
                                    "CHECK_THROWS failed: " #expression " did not throw " #ExceptionType); \
        }                                                                                    \
    } while (0)

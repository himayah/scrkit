#pragma once
// Minimal, dependency-free unit test harness used only by this project's
// core-logic tests (see docs/DESIGN.md, テスト方針). No network fetch, no
// external test library -- just enough to register cases, run them, and
// report a process exit code ctest can key off of.

#include <cstdio>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline int& FailureCount() {
    static int count = 0;
    return count;
}

inline int& CurrentCaseFailures() {
    static int count = 0;
    return count;
}

inline void ReportFailure(const char* file, int line, const std::string& message) {
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, message.c_str());
    ++FailureCount();
    ++CurrentCaseFailures();
}

inline int RunAll() {
    int total = 0;
    int failedCases = 0;
    for (const auto& tc : Registry()) {
        CurrentCaseFailures() = 0;
        std::printf("[ RUN  ] %s\n", tc.name.c_str());
        tc.fn();
        ++total;
        if (CurrentCaseFailures() > 0) {
            ++failedCases;
            std::printf("[ FAIL ] %s\n", tc.name.c_str());
        } else {
            std::printf("[  OK  ] %s\n", tc.name.c_str());
        }
    }
    std::printf("---\n%d test case(s) run, %d failed, %d assertion failure(s) total.\n",
                total, failedCases, FailureCount());
    return failedCases == 0 ? 0 : 1;
}

} // namespace testfw

#define TEST_CASE(name)                                                          \
    static void name();                                                         \
    static ::testfw::Registrar registrar_##name(#name, &name);                  \
    static void name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ::testfw::ReportFailure(__FILE__, __LINE__, "CHECK failed: " #cond); \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        if (!((a) == (b))) {                                                     \
            ::testfw::ReportFailure(__FILE__, __LINE__,                          \
                                     std::string("CHECK_EQ failed: ") + #a + " != " + #b); \
        }                                                                         \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                     \
    do {                                                                         \
        auto diff__ = (a) - (b);                                                 \
        if (diff__ < 0) diff__ = -diff__;                                        \
        if (diff__ > (eps)) {                                                    \
            ::testfw::ReportFailure(__FILE__, __LINE__,                          \
                                     std::string("CHECK_NEAR failed: ") + #a + " vs " + #b); \
        }                                                                         \
    } while (0)

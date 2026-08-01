#pragma once

// A deliberately small test harness: no external dependency, since
// third_party/ is reserved for real submodules and none has been pulled in
// yet. Swap this out for Catch2 or GoogleTest once a real test framework is
// added as a submodule; the CHECK/RUN_TEST macros below are meant to be
// easy to delete at that point, not to become their own subsystem.

#include <cstdio>
#include <string>

namespace archtest {

inline int& failure_count() {
    static int count = 0;
    return count;
}

inline void report_failure(const char* file, int line, const std::string& message) {
    std::fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, message.c_str());
    failure_count()++;
}

}  // namespace archtest

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                      \
            archtest::report_failure(__FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        auto actual_value = (actual);                                       \
        auto expected_value = (expected);                                   \
        if (!(actual_value == expected_value)) {                            \
            std::string message = #actual " == " #expected;                 \
            archtest::report_failure(__FILE__, __LINE__, message);    \
        }                                                                    \
    } while (0)

// Like CHECK_EQ, but returns from the calling (void-returning) test function
// on failure instead of continuing. Use this for a size/precondition check
// that later indexed access depends on: a wrong size only gets logged once
// instead of being followed by out-of-bounds reads on the next lines.
#define REQUIRE_EQ(actual, expected)                                         \
    do {                                                                     \
        auto actual_value = (actual);                                       \
        auto expected_value = (expected);                                   \
        if (!(actual_value == expected_value)) {                            \
            std::string message = #actual " == " #expected " (required)";   \
            archtest::report_failure(__FILE__, __LINE__, message);    \
            return;                                                         \
        }                                                                    \
    } while (0)

#define RUN_TEST(fn)                                                         \
    do {                                                                     \
        std::fprintf(stderr, "-- %s --\n", #fn);                            \
        fn();                                                                \
    } while (0)

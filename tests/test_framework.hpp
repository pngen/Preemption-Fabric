#pragma once

#include <cstdio>
#include <string>

// Minimal, dependency-free test harness. Each test is a standalone executable
// returning a nonzero exit code on failure. No test timeouts are ever used.

namespace pf_test {
inline int g_failures = 0;
inline const char* g_ctx = "test";
}  // namespace pf_test

#define TEST_CONTEXT(name) pf_test::g_ctx = (name)

#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      ++pf_test::g_failures;                                                          \
      std::printf("FAIL [%s] %s:%d: %s\n", pf_test::g_ctx, __FILE__, __LINE__, #cond); \
    }                                                                                 \
  } while (0)

#define CHECK_EQ(a, b)                                                        \
  do {                                                                        \
    if (!((a) == (b))) {                                                      \
      ++pf_test::g_failures;                                                  \
      std::printf("FAIL [%s] %s:%d: %s == %s\n", pf_test::g_ctx, __FILE__,   \
                  __LINE__, #a, #b);                                          \
    }                                                                         \
  } while (0)

#define CHECK_MSG(cond, msg)                                                            \
  do {                                                                                 \
    if (!(cond)) {                                                                     \
      ++pf_test::g_failures;                                                           \
      std::printf("FAIL [%s] %s:%d: %s -- %s\n", pf_test::g_ctx, __FILE__, __LINE__,  \
                  #cond, (msg));                                                       \
    }                                                                                  \
  } while (0)

#define CHECK_THROWS(expr)                                                            \
  do {                                                                                \
    bool caught = false;                                                              \
    try { (void)(expr); } catch (...) { caught = true; }                              \
    if (!caught) {                                                                    \
      ++pf_test::g_failures;                                                          \
      std::printf("FAIL [%s] %s:%d: expected throw: %s\n", pf_test::g_ctx, __FILE__, \
                  __LINE__, #expr);                                                   \
    }                                                                                 \
  } while (0)

#define PF_TEST_RETURN() return pf_test::g_failures == 0 ? 0 : 1
#define PF_TEST_SUMMARY() (pf_test::g_failures)

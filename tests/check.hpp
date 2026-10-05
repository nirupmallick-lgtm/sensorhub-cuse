// Minimal dependency-free test helpers.
#pragma once
#include <cstdio>

inline int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)
#define RUN(fn)                                                                  \
    do {                                                                         \
        int before = g_failures;                                                 \
        fn();                                                                    \
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", #fn);   \
    } while (0)
#define TEST_MAIN_END() return g_failures == 0 ? 0 : 1

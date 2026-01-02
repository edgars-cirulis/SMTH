#pragma once
#include <cstdio>

#if defined(CFGC_DIAGNOSTICS)
#define CFGC_LOGF(...)                     \
    do {                                   \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n");        \
    } while (0)
#else
#define CFGC_LOGF(...) \
    do {               \
    } while (0)
#endif

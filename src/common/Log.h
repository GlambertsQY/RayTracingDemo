#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

inline void rtLog(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

#define RT_LOG(...) ::rtLog(__VA_ARGS__)
#define RT_FATAL(...)                    \
    do {                                 \
        ::rtLog(__VA_ARGS__);            \
        std::abort();                    \
    } while (0)

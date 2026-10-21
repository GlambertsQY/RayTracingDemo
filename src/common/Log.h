#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>

#define RT_LOG_TAG "RayTracingDemo"

inline void rtLog(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, RT_LOG_TAG, fmt, args);
    va_end(args);
}

#define RT_LOG(...) ::rtLog(__VA_ARGS__)
#define RT_FATAL(...)                                                                    \
    do {                                                                                 \
        ::rtLog(__VA_ARGS__);                                                            \
        std::abort();                                                                    \
    } while (0)
#else
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
#endif

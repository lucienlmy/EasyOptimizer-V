#ifndef EO_LOG_H
#define EO_LOG_H

#include <stdio.h>
#include <windows.h>

static inline void log_timestamp(FILE *f) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

#define LOG(fmt, ...) do { \
    log_timestamp(stdout); \
    fprintf(stdout, fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)

#define LOG_ERR(fmt, ...) do { \
    log_timestamp(stderr); \
    fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
} while (0)

static inline void log_init_console(void) {
    AllocConsole();
    SetConsoleTitleW(L"EasyOptimizer-V \u2014 Debug Log");
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    LOG("Debug console initialized");
}

#endif

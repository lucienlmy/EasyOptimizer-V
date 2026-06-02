#ifndef EO_LOG_H
#define EO_LOG_H

#include <stdio.h>
#include <windows.h>

extern FILE *g_log_file;

/* Last archive-load failure reason, set by the loaders and surfaced by the UI. */
extern char g_load_error[256];
#define SET_LOAD_ERR(...) do { _snprintf(g_load_error, sizeof(g_load_error), __VA_ARGS__); g_load_error[sizeof(g_load_error)-1] = 0; } while (0)

static inline void log_timestamp(FILE *f) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (g_log_file) {
        fprintf(g_log_file, "[%02d:%02d:%02d.%03d] ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
}

#define LOG(fmt, ...) do { \
    log_timestamp(stdout); \
    fprintf(stdout, fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
    if (g_log_file) { \
        fprintf(g_log_file, fmt "\n", ##__VA_ARGS__); \
        fflush(g_log_file); \
    } \
} while (0)

#define LOG_ERR(fmt, ...) do { \
    log_timestamp(stderr); \
    fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
    if (g_log_file) { \
        fprintf(g_log_file, "[ERR] " fmt "\n", ##__VA_ARGS__); \
        fflush(g_log_file); \
    } \
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

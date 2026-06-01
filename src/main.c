#include <windows.h>
#include "gui.h"
#include "bc7enc_wrapper.h"
#include "nvtt_c_wrapper.h"
#include "log.h"

FILE *g_log_file = NULL;

LONG WINAPI CrashHandler(EXCEPTION_POINTERS *ExceptionInfo) {
    unsigned long code = ExceptionInfo->ExceptionRecord->ExceptionCode;
    void *addr = ExceptionInfo->ExceptionRecord->ExceptionAddress;

    if (g_log_file) {
        fprintf(g_log_file, "\n========================================\n");
        fprintf(g_log_file, "CRITICAL ERROR: Application Crashed!\n");
        fprintf(g_log_file, "Exception Code: 0x%08lX\n", code);
        fprintf(g_log_file, "Exception Address: 0x%p\n", addr);
        fprintf(g_log_file, "========================================\n");
        fflush(g_log_file);
    }

    /* Persist a copy that is NOT overwritten on the next launch, so the crash
     * can still be studied after restarting the app. */
    FILE *cf = fopen("EasyOptimizer_crash.log", "a");
    if (cf) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(cf, "[%04d-%02d-%02d %02d:%02d:%02d] crash: code=0x%08lX addr=0x%p\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, code, addr);
        fclose(cf);
    }

    MessageBoxA(NULL, "O aplicativo encontrou um erro crítico e será fechado.\nLogs salvos em EasyOptimizer.log e EasyOptimizer_crash.log.", "Crash", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // Force current directory to be the executable's directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
        SetCurrentDirectoryW(exePath);
    }
    
    g_log_file = fopen("EasyOptimizer.log", "w");
    if (g_log_file) {
        setvbuf(g_log_file, NULL, _IONBF, 0); // Disable buffering completely
    }
    SetUnhandledExceptionFilter(CrashHandler);

    log_init_console();
    LOG("Initializing bc7enc (ISPC BC7 + rgbcx)...");
    bc7enc_init();
    LOG("bc7enc ready");
    LOG("Default encoder: CPU (bc7enc ISPC). Toggle 'Encoder' in the sidebar to use GPU (NVTT/CUDA).");
    nvtt_wrapper_probe();

    gui_init(hInstance);
    LOG("GUI initialized, entering message loop");
    gui_run();

    LOG("Shutting down");
    nvtt_wrapper_shutdown();
    FreeConsole();
    if (g_log_file) fclose(g_log_file);
    return 0;
}

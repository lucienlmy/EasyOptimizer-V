#include <windows.h>
#include "gui.h"
#include "bc7enc_wrapper.h"
#include "log.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    log_init_console();
    LOG("Initializing bc7enc (ISPC BC7 + rgbcx)...");
    bc7enc_init();
    LOG("bc7enc ready");

    gui_init(hInstance);
    LOG("GUI initialized, entering message loop");
    gui_run();

    LOG("Shutting down");
    FreeConsole();
    return 0;
}

#include <windows.h>

#include "browser_app.h"
#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"

#if !defined(CEF_USE_BOOTSTRAP)
#error "This target must be built as the sandboxed bootstrap DLL (USE_SANDBOX=ON)."
#endif

namespace {

    int RunMain(HINSTANCE instance, void* sandbox_info) {
        CefMainArgs main_args(instance);

        // Subprocesses (renderer, GPU, ...) run here and return.
        const int exit_code = CefExecuteProcess(main_args, nullptr, sandbox_info);
        if (exit_code >= 0) {
            return exit_code;
        }

        // Browser process only. Declare DPI awareness before any window exists.
        // (Harmlessly fails if the exe's manifest already declared it.)
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        // Fail closed: never run the browser without the sandbox.
        if (!sandbox_info) {
            MessageBoxW(nullptr,
                L"The Chromium sandbox is not available, so the browser "
                L"will not start.\n\nLaunch the app through its .exe "
                L"(the bootstrap), not the DLL directly.",
                L"GlassBrowser", MB_ICONERROR | MB_OK);
            return 1;
        }

        CefSettings settings;
        settings.log_severity = LOGSEVERITY_WARNING;  // writes debug.log next to the exe

        CefRefPtr<BrowserApp> app(new BrowserApp);
        if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
            return CefGetExitCode();
        }

        CefRunMessageLoop();  // returns after CefQuitMessageLoop()
        CefShutdown();
        return 0;
    }

}  // namespace

// Entry point called by bootstrap.exe in every process it starts.
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,
    LPWSTR /*command_line*/,
    int /*show_command*/,
    void* sandbox_info,
    cef_version_info_t* /*version_info*/) {
    return RunMain(instance, sandbox_info);
}
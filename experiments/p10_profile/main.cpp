#include <windows.h>

#include <memory>

#include "app_ids.h"
#include "browser_app.h"
#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"
#include "profile.h"

#if !defined(CEF_USE_BOOTSTRAP)
#error "This target must be built as the sandboxed bootstrap DLL (USE_SANDBOX=ON)."
#endif

namespace {

    // ===== PROFILE MODE: change this one line, rebuild, re-test (Phase 10 tests) =====
    //   ProfileMode::Temporary  - in-memory profile, deleted on exit (the default)
    //   ProfileMode::Persistent - saved on disk under %LOCALAPPDATA%\GlassBrowser\User Data
    constexpr ProfileMode kProfileMode = ProfileMode::Temporary;
    // ==================================================================================

    // Holds a named mutex for the life of the app. A second launch sees that the
    // mutex already exists and knows another copy is running.
    class SingleInstance {
    public:
        SingleInstance() {
            mutex_ = CreateMutexW(nullptr, FALSE, app_ids::kSingleInstanceMutex);
            already_running_ = (mutex_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);
        }
        ~SingleInstance() {
            if (mutex_) {
                CloseHandle(mutex_);
            }
        }
        SingleInstance(const SingleInstance&) = delete;
        SingleInstance& operator=(const SingleInstance&) = delete;

        bool already_running() const { return already_running_; }

    private:
        HANDLE mutex_ = nullptr;
        bool already_running_ = false;
    };

    // Asks the running copy to bring its window to the front.
    void ActivateRunningInstance() {
        const HWND hwnd = FindWindowW(app_ids::kMainWindowClass, nullptr);
        if (hwnd) {
            AllowSetForegroundWindow(ASFW_ANY);  // let the running copy take the focus
            PostMessageW(hwnd, RegisterWindowMessageW(app_ids::kActivateMessage), 0, 0);
        }
    }

    int RunMain(HINSTANCE instance, void* sandbox_info) {
        CefMainArgs main_args(instance);

        // Subprocesses (renderer, GPU, ...) run here and return.
        const int exit_code = CefExecuteProcess(main_args, nullptr, sandbox_info);
        if (exit_code >= 0) {
            return exit_code;
        }

        // ---- Browser process only from here on ----

        // Declare DPI awareness before any window exists.
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

        // Only one copy at a time. (Locals below are destroyed in reverse order, so the
        // profile is cleaned up first and the mutex is released last.)
        SingleInstance single_instance;
        if (single_instance.already_running()) {
            ActivateRunningInstance();
            return 0;
        }

        std::wstring profile_error;
        const std::unique_ptr<Profile> profile = Profile::Create(kProfileMode, &profile_error);
        if (!profile) {
            MessageBoxW(nullptr, profile_error.c_str(), L"GlassBrowser", MB_ICONERROR | MB_OK);
            return 1;
        }

        CefSettings settings;
        settings.log_severity = LOGSEVERITY_WARNING;  // writes debug.log next to the exe
        profile->ApplyTo(settings);

        CefRefPtr<BrowserApp> app(new BrowserApp(profile.get()));
        if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
            return CefGetExitCode();
        }

        CefRunMessageLoop();  // returns after CefQuitMessageLoop()
        CefShutdown();
        return 0;  // ~Profile runs after this: a temporary profile's folder is deleted
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
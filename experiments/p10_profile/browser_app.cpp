#include "browser_app.h"

#include <windows.h>

#include "include/wrapper/cef_helpers.h"
#include "main_window.h"

namespace {
    // Also the Home page.
    constexpr char kStartUrl[] = "https://duckduckgo.com";
}

// Defined here, where MainWindow is a complete type.
BrowserApp::BrowserApp(const Profile* profile) : profile_(profile) {}
BrowserApp::~BrowserApp() = default;

void BrowserApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    window_ = std::make_unique<MainWindow>();
    if (!window_->Create(GetModuleHandleW(nullptr), HidingMode::HiddenOwner, kStartUrl,
        profile_)) {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"GlassBrowser",
            MB_ICONERROR | MB_OK);
        CefQuitMessageLoop();
    }
}

// With a persistent profile the root folder is fixed, so Chromium itself can hand a
// second launch to this instance. (The mutex in main.cpp normally catches it first.)
bool BrowserApp::OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> /*command_line*/,
    const CefString& /*current_directory*/) {
    CEF_REQUIRE_UI_THREAD();
    if (window_) {
        window_->BringToFront();
    }
    return true;  // handled: no extra window
}
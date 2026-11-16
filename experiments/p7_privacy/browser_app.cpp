#include "browser_app.h"

#include <windows.h>

#include "include/wrapper/cef_helpers.h"
#include "main_window.h"

namespace {
    constexpr char kStartUrl[] = "https://example.com";
}

// Defined here, where MainWindow is a complete type.
BrowserApp::BrowserApp() = default;
BrowserApp::~BrowserApp() = default;

void BrowserApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    window_ = std::make_unique<MainWindow>();
    if (!window_->Create(GetModuleHandleW(nullptr), HidingMode::HiddenOwner, kStartUrl)) {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"GlassBrowser",
            MB_ICONERROR | MB_OK);
        CefQuitMessageLoop();
    }
}
#include "simple_app.h"

#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"
#include "simple_client.h"

namespace {
    constexpr char kStartUrl[] = "https://example.com";
}

void SimpleApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    CefRefPtr<SimpleClient> client(new SimpleClient);

    // Phase 4 only: let CEF create its own top-level window.
    // (Phase 5 switches to SetAsChild inside our own Win32 window.)
    CefWindowInfo window_info;
    window_info.SetAsPopup(nullptr, "GlassBrowser - Phase 4");

    // Alloy style = web content only, no Chrome toolbar/tabs.
    window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    CefBrowserSettings browser_settings;
    CefBrowserHost::CreateBrowser(window_info, client, kStartUrl, browser_settings,
        nullptr, nullptr);
}
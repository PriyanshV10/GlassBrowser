#pragma once

#include "include/cef_app.h"

// Process-wide CEF callbacks. Also acts as the browser-process handler so we
// learn when CEF is ready to create browsers.
class SimpleApp : public CefApp, public CefBrowserProcessHandler {
public:
    SimpleApp() = default;
    SimpleApp(const SimpleApp&) = delete;
    SimpleApp& operator=(const SimpleApp&) = delete;

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    // CefBrowserProcessHandler
    void OnContextInitialized() override;

private:
    IMPLEMENT_REFCOUNTING(SimpleApp);
};
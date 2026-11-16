#pragma once

#include <memory>

#include "include/cef_app.h"
#include "include/cef_command_line.h"

class MainWindow;

// Process-wide CEF callbacks. When CEF is ready it creates our window.
class BrowserApp : public CefApp, public CefBrowserProcessHandler {
public:
    BrowserApp();
    ~BrowserApp() override;
    BrowserApp(const BrowserApp&) = delete;
    BrowserApp& operator=(const BrowserApp&) = delete;

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    // CefBrowserProcessHandler
    void OnContextInitialized() override;
    bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> command_line,
        const CefString& current_directory) override;

private:
    std::unique_ptr<MainWindow> window_;

    IMPLEMENT_REFCOUNTING(BrowserApp);
};
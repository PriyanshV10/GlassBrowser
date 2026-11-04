#pragma once

#include <list>

#include "include/cef_client.h"

// Per-browser callbacks. CefClient itself only hands out handler objects;
// here one class implements three handlers and returns `this` for each.
class SimpleClient : public CefClient,
    public CefDisplayHandler,
    public CefLifeSpanHandler,
    public CefLoadHandler {
public:
    SimpleClient() = default;
    SimpleClient(const SimpleClient&) = delete;
    SimpleClient& operator=(const SimpleClient&) = delete;

    // CefClient
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

    // CefDisplayHandler
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefLoadHandler
    void OnLoadError(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        ErrorCode error_code,
        const CefString& error_text,
        const CefString& failed_url) override;

private:
    std::list<CefRefPtr<CefBrowser>> browsers_;

    IMPLEMENT_REFCOUNTING(SimpleClient);
};
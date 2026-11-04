#pragma once

#include <list>
#include <string>

#include "include/cef_client.h"

// Implemented by MainWindow. CEF-side code talks to the UI only through this.
// All calls happen on the CEF UI thread, which is also our window's thread.
class BrowserDelegate {
public:
    virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnTitleChanged(const std::wstring& title) = 0;
    virtual void OnAddressChanged(const std::wstring& url) = 0;
    virtual void OnLoadingStateChanged(bool is_loading, bool can_go_back,
        bool can_go_forward) = 0;

protected:
    ~BrowserDelegate() = default;
};

// Per-browser callbacks.
class BrowserClient : public CefClient,
    public CefDisplayHandler,
    public CefLifeSpanHandler,
    public CefLoadHandler {
public:
    explicit BrowserClient(BrowserDelegate* delegate) : delegate_(delegate) {}
    BrowserClient(const BrowserClient&) = delete;
    BrowserClient& operator=(const BrowserClient&) = delete;

    // Called when the window is destroyed; later events are then ignored.
    void DetachDelegate() { delegate_ = nullptr; }

    // CefClient
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

    // CefDisplayHandler
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
    void OnAddressChange(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& url) override;

    // CefLifeSpanHandler
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        int popup_id,
        const CefString& target_url,
        const CefString& target_frame_name,
        WindowOpenDisposition target_disposition,
        bool user_gesture,
        const CefPopupFeatures& popup_features,
        CefWindowInfo& window_info,
        CefRefPtr<CefClient>& client,
        CefBrowserSettings& settings,
        CefRefPtr<CefDictionaryValue>& extra_info,
        bool* no_javascript_access) override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefLoadHandler
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
        bool is_loading,
        bool can_go_back,
        bool can_go_forward) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        ErrorCode error_code,
        const CefString& error_text,
        const CefString& failed_url) override;

private:
    BrowserDelegate* delegate_;  // not owned; cleared by DetachDelegate()
    std::list<CefRefPtr<CefBrowser>> browsers_;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};
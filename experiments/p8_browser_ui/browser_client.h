#pragma once

#include <cstdint>
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
    virtual void OnLoadingProgress(double progress) = 0;           // 0.0 - 1.0
    virtual void OnStatusMessage(const std::wstring& text) = 0;    // hovered link
    virtual void OnFindResult(int count, int active_ordinal, bool final_update) = 0;
    // Called before the page sees a key. Return true if it was an app shortcut.
    virtual bool OnBrowserKey(int windows_key_code, uint32_t modifiers) = 0;

protected:
    ~BrowserDelegate() = default;
};

// Per-browser callbacks.
class BrowserClient : public CefClient,
    public CefDisplayHandler,
    public CefLifeSpanHandler,
    public CefLoadHandler,
    public CefKeyboardHandler,
    public CefFindHandler {
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
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
    CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }

    // CefDisplayHandler
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
    void OnAddressChange(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& url) override;
    void OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) override;
    void OnLoadingProgressChange(CefRefPtr<CefBrowser> browser, double progress) override;

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

    // CefKeyboardHandler
    bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
        const CefKeyEvent& event,
        CefEventHandle os_event,
        bool* is_keyboard_shortcut) override;

    // CefFindHandler
    void OnFindResult(CefRefPtr<CefBrowser> browser,
        int identifier,
        int count,
        const CefRect& selection_rect,
        int active_match_ordinal,
        bool final_update) override;

private:
    BrowserDelegate* delegate_;  // not owned; cleared by DetachDelegate()
    std::list<CefRefPtr<CefBrowser>> browsers_;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};
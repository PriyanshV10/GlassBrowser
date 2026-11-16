#pragma once

#include <cstdint>
#include <string>

#include "include/cef_client.h"

// Implemented by MainWindow. CEF-side code talks to the UI only through this.
// Every call is tagged with the id of the tab it came from. All calls happen on
// the CEF UI thread, which is also our window's thread.
class BrowserDelegate {
public:
    virtual void OnTabBrowserCreated(int tab_id, CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnTabTitleChanged(int tab_id, const std::wstring& title) = 0;
    virtual void OnTabAddressChanged(int tab_id, const std::wstring& url) = 0;
    virtual void OnTabLoadingStateChanged(int tab_id, bool is_loading, bool can_go_back,
        bool can_go_forward) = 0;
    virtual void OnTabLoadingProgress(int tab_id, double progress) = 0;  // 0.0 - 1.0
    virtual void OnTabStatusMessage(int tab_id, const std::wstring& text) = 0;
    virtual void OnTabFindResult(int tab_id, int count, int active_ordinal,
        bool final_update) = 0;
    // Called before the page sees a key. Return true if it was an app shortcut.
    virtual bool OnTabKey(int tab_id, int windows_key_code, uint32_t modifiers) = 0;
    // A page asked for a new window; we open it as a tab instead.
    virtual void OnTabNewTabRequested(int source_tab_id, const std::wstring& url,
        bool foreground) = 0;
    // The tab's close was approved (after unload handlers ran); see MainWindow.
    virtual void OnTabCloseApproved(int tab_id) = 0;
    // The tab's browser is gone (OnBeforeClose).
    virtual void OnTabClosed(int tab_id) = 0;

protected:
    ~BrowserDelegate() = default;
};

// Per-tab CEF callbacks. One instance per tab.
class BrowserClient : public CefClient,
    public CefDisplayHandler,
    public CefLifeSpanHandler,
    public CefLoadHandler,
    public CefKeyboardHandler,
    public CefFindHandler {
public:
    BrowserClient(BrowserDelegate* delegate, int tab_id)
        : delegate_(delegate), tab_id_(tab_id) {
    }
    BrowserClient(const BrowserClient&) = delete;
    BrowserClient& operator=(const BrowserClient&) = delete;

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
    BrowserDelegate* delegate_;  // not owned; the window outlives every client
    const int tab_id_;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};
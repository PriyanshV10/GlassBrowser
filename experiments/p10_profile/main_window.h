#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "browser_client.h"
#include "capture_privacy.h"
#include "include/cef_browser.h"
#include "opacity_controller.h"
#include "profile.h"
#include "tab_strip.h"

// How the window is kept off the taskbar and Alt+Tab (see Phase 2).
enum class HidingMode {
    HiddenOwner,  // owned by an invisible window (our default)
    ToolWindow,   // WS_EX_TOOLWINDOW (documented fallback)
};

class MainWindow : public BrowserDelegate, public TabStripDelegate {
public:
    MainWindow() = default;
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Creates the window and its first tab. `start_url` is also the Home page.
    bool Create(HINSTANCE instance, HidingMode mode, const char* start_url,
        const Profile* profile);

    // Shows and raises the window (used when the app is launched a second time).
    void BringToFront();

    // BrowserDelegate (one call per tab event)
    void OnTabBrowserCreated(int tab_id, CefRefPtr<CefBrowser> browser) override;
    void OnTabTitleChanged(int tab_id, const std::wstring& title) override;
    void OnTabAddressChanged(int tab_id, const std::wstring& url) override;
    void OnTabLoadingStateChanged(int tab_id, bool is_loading, bool can_go_back,
        bool can_go_forward) override;
    void OnTabLoadingProgress(int tab_id, double progress) override;
    void OnTabStatusMessage(int tab_id, const std::wstring& text) override;
    void OnTabFindResult(int tab_id, int count, int active_ordinal,
        bool final_update) override;
    bool OnTabKey(int tab_id, int windows_key_code, uint32_t modifiers) override;
    void OnTabNewTabRequested(int source_tab_id, const std::wstring& url,
        bool foreground) override;
    void OnTabCloseApproved(int tab_id) override;
    void OnTabClosed(int tab_id) override;

    // TabStripDelegate
    void OnTabStripSelect(int tab_id) override;
    void OnTabStripClose(int tab_id) override;
    void OnTabStripNewTab() override;

private:
    // Everything we know about one tab.
    struct Tab {
        int id = 0;
        CefRefPtr<BrowserClient> client;
        CefRefPtr<CefBrowser> browser;   // null until OnTabBrowserCreated
        std::wstring url;
        std::wstring title;
        bool loading = false;
        double progress = 0.0;
        bool can_go_back = false;
        bool can_go_forward = false;
        bool focus_address = false;      // focus the address bar when created (Ctrl+T)
        bool close_requested = false;    // we asked CEF to close it
        bool close_approved = false;     // CEF called DoClose: it is going away
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
        LPARAM lparam, UINT_PTR subclass_id,
        DWORD_PTR ref_data);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // Tabs
    int NewTab(const std::wstring& url, bool activate, bool focus_address);
    void SelectTab(int tab_id);
    void CloseTab(int tab_id);
    void ReopenClosedTab();
    void SelectRelativeTab(int delta);
    void SelectTabByNumber(int number);
    Tab* FindTab(int tab_id);
    Tab* ActiveTab();
    CefRefPtr<CefBrowser> ActiveBrowser();
    int LiveTabCount() const;
    std::vector<int> NavigableTabIds() const;
    void ShowOnlyActiveBrowser();
    void RefreshStrip();
    void SyncToolbarToActiveTab();

    // Layout and controls
    void CreateControls(HWND parent);
    void Layout();
    RECT ComputeBrowserRect() const;

    // Commands
    void OnCommand(int id);
    void ShowMenu();
    bool HandleShortcut(int key, uint32_t modifiers);
    void NavigateFromAddressBar();
    void FocusAddressBar();
    void ShowFindBar(bool show);
    void Find(bool forward, bool find_next);
    void ToggleDevTools();
    void ZoomBy(double delta);
    void ZoomReset();
    void OpenTestPage();
    void OpenProfileFolder();

    // Status, title, opacity, privacy, hotkeys
    void UpdateTitle();
    void UpdateOpacityUi();   // slider + label + title, from opacity_
    void UpdateStatus();
    void SetPrivacyMode(bool on);
    void RegisterHotkeys();
    void UnregisterHotkeys();
    void RunCommand(int command_index);
    void ToggleVisibility();

    HINSTANCE instance_ = nullptr;
    HidingMode mode_ = HidingMode::HiddenOwner;
    const Profile* profile_ = nullptr;  // not owned
    UINT dpi_ = 96;

    HWND owner_ = nullptr;    // invisible owner window
    HWND hwnd_ = nullptr;     // our top-level window

    TabStrip tab_strip_;

    // Toolbar
    HWND back_ = nullptr;
    HWND forward_ = nullptr;
    HWND reload_ = nullptr;
    HWND home_ = nullptr;
    HWND address_ = nullptr;
    HWND slider_ = nullptr;
    HWND opacity_label_ = nullptr;
    HWND menu_ = nullptr;
    // Find bar (hidden until Ctrl+F)
    HWND find_label_ = nullptr;
    HWND find_edit_ = nullptr;
    HWND find_status_ = nullptr;
    HWND find_prev_ = nullptr;
    HWND find_next_ = nullptr;
    HWND find_close_ = nullptr;
    // Status bar
    HWND status_text_ = nullptr;
    HWND progress_ = nullptr;

    HFONT font_ = nullptr;

    std::vector<Tab> tabs_;
    int active_id_ = 0;
    int next_tab_id_ = 1;
    std::vector<std::wstring> closed_urls_;   // for Ctrl+Shift+T

    OpacityController opacity_;
    CapturePrivacy capture_privacy_;
    bool find_visible_ = false;
    bool closing_app_ = false;        // the user asked to close the whole window
    bool window_destroyed_ = false;   // WM_DESTROY has run
    std::wstring home_url_;
    std::wstring status_hover_;
    std::wstring hotkeys_failed_;
};
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "browser_client.h"
#include "capture_privacy.h"
#include "include/cef_browser.h"
#include "opacity_controller.h"

// How the window is kept off the taskbar and Alt+Tab (see Phase 2).
enum class HidingMode {
    HiddenOwner,  // owned by an invisible window (our default)
    ToolWindow,   // WS_EX_TOOLWINDOW (documented fallback)
};

class MainWindow : public BrowserDelegate {
public:
    MainWindow() = default;
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Creates the window and starts creating the browser inside it.
    // `start_url` is also the Home page.
    bool Create(HINSTANCE instance, HidingMode mode, const char* start_url);

    // Shows and raises the window (used when the app is launched a second time).
    void BringToFront();

    // BrowserDelegate
    void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
    void OnTitleChanged(const std::wstring& title) override;
    void OnAddressChanged(const std::wstring& url) override;
    void OnLoadingStateChanged(bool is_loading, bool can_go_back,
        bool can_go_forward) override;
    void OnLoadingProgress(double progress) override;
    void OnStatusMessage(const std::wstring& text) override;
    void OnFindResult(int count, int active_ordinal, bool final_update) override;
    bool OnBrowserKey(int windows_key_code, uint32_t modifiers) override;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
        LPARAM lparam, UINT_PTR subclass_id,
        DWORD_PTR ref_data);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void CreateControls(HWND parent);
    void Layout();
    RECT ComputeBrowserRect() const;

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
    UINT dpi_ = 96;

    HWND owner_ = nullptr;    // invisible owner window
    HWND hwnd_ = nullptr;     // our top-level window

    // Row 1: toolbar
    HWND back_ = nullptr;
    HWND forward_ = nullptr;
    HWND reload_ = nullptr;
    HWND home_ = nullptr;
    HWND address_ = nullptr;
    HWND slider_ = nullptr;
    HWND opacity_label_ = nullptr;
    HWND menu_ = nullptr;
    // Row 2: find bar (hidden until Ctrl+F)
    HWND find_label_ = nullptr;
    HWND find_edit_ = nullptr;
    HWND find_status_ = nullptr;
    HWND find_prev_ = nullptr;
    HWND find_next_ = nullptr;
    HWND find_close_ = nullptr;
    // Row 4: status bar
    HWND status_text_ = nullptr;
    HWND progress_ = nullptr;

    HFONT font_ = nullptr;

    CefRefPtr<BrowserClient> client_;
    CefRefPtr<CefBrowser> browser_;

    OpacityController opacity_;
    CapturePrivacy capture_privacy_;
    bool loading_ = false;
    bool find_visible_ = false;
    std::string home_url_;
    std::wstring page_title_;
    std::wstring current_url_;
    std::wstring status_hover_;
    std::wstring hotkeys_failed_;
};
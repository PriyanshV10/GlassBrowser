#pragma once

#include <windows.h>

#include <string>

#include "browser_client.h"
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
    bool Create(HINSTANCE instance, HidingMode mode, const char* start_url);

    // BrowserDelegate
    void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
    void OnTitleChanged(const std::wstring& title) override;
    void OnAddressChanged(const std::wstring& url) override;
    void OnLoadingStateChanged(bool is_loading, bool can_go_back,
        bool can_go_forward) override;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK AddressSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
        LPARAM lparam, UINT_PTR subclass_id,
        DWORD_PTR ref_data);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void CreateControls(HWND parent);
    void Layout();
    void UpdateTitle();
    void UpdateOpacityUi();   // slider + label + title, from opacity_
    void OnButton(int id);
    void OpenTestPage();
    void NavigateFromAddressBar();

    void RegisterHotkeys();
    void UnregisterHotkeys();
    void RunCommand(int command_index);
    void ToggleVisibility();

    HINSTANCE instance_ = nullptr;
    HidingMode mode_ = HidingMode::HiddenOwner;
    UINT dpi_ = 96;

    HWND owner_ = nullptr;    // invisible owner window
    HWND hwnd_ = nullptr;     // our top-level window
    HWND back_ = nullptr;
    HWND forward_ = nullptr;
    HWND reload_ = nullptr;
    HWND test_ = nullptr;
    HWND address_ = nullptr;
    HWND slider_ = nullptr;
    HWND opacity_label_ = nullptr;
    HFONT font_ = nullptr;

    CefRefPtr<BrowserClient> client_;
    CefRefPtr<CefBrowser> browser_;

    OpacityController opacity_;
    bool loading_ = false;
    std::wstring page_title_;
    std::wstring current_url_;
    std::wstring hotkeys_failed_;
};
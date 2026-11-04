#include "main_window.h"

#include <commctrl.h>

#include <algorithm>
#include <iterator>

#include "include/cef_app.h"
#include "include/cef_parser.h"

#pragma comment(lib, "comctl32.lib")

namespace {

    constexpr wchar_t kMainClassName[] = L"GlassBrowserMainWindow";
    constexpr wchar_t kOwnerClassName[] = L"GlassBrowserHiddenOwner";
    constexpr wchar_t kAppName[] = L"GlassBrowser";
    constexpr char kSearchUrl[] = "https://duckduckgo.com/?q=";

    constexpr int kIdBack = 101;
    constexpr int kIdForward = 102;
    constexpr int kIdReload = 103;

    // Sizes are in "96-DPI pixels" and scaled to the window's DPI at runtime.
    constexpr int kToolbarHeight = 40;
    constexpr int kPadding = 6;
    constexpr int kBackWidth = 56;
    constexpr int kForwardWidth = 72;
    constexpr int kReloadWidth = 72;
    constexpr int kInitialWidth = 1100;
    constexpr int kInitialHeight = 760;

    int Scale(int value, UINT dpi) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    // ---- Address-bar input -> URL ---------------------------------------------

    bool StartsWith(const std::wstring& text, const wchar_t* prefix) {
        return text.rfind(prefix, 0) == 0;
    }

    // Turns what the user typed into a URL: a full URL is used as-is, something
    // host-like gets https://, anything else becomes a web search.
    CefString ResolveInput(const std::wstring& input) {
        const size_t first = input.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) {
            return CefString();
        }
        const size_t last = input.find_last_not_of(L" \t\r\n");
        const std::wstring text = input.substr(first, last - first + 1);

        if (text.find(L"://") != std::wstring::npos || StartsWith(text, L"about:") ||
            StartsWith(text, L"data:") || StartsWith(text, L"file:")) {
            return CefString(text);
        }

        const bool has_space = text.find(L' ') != std::wstring::npos;
        const bool is_local = StartsWith(text, L"localhost");
        if (!has_space && (is_local || text.find(L'.') != std::wstring::npos)) {
            return CefString((is_local ? L"http://" : L"https://") + text);
        }

        const CefString query = CefURIEncode(CefString(text), true);
        return CefString(std::string(kSearchUrl) + query.ToString());
    }

    // ---- Global hotkeys (same as Phase 3) -------------------------------------

    enum class Command {
        ToggleVisibility,
        OpacityUp,
        OpacityDown,
        OpacityReset,
        Preset100,
        Preset75,
        Preset50,
        Preset25,
    };

    struct HotkeyDef {
        Command command;
        UINT modifiers;
        UINT vk;
        const wchar_t* label;
    };

    constexpr UINT kCtrlAlt = MOD_CONTROL | MOD_ALT;

    constexpr HotkeyDef kHotkeys[] = {
        {Command::ToggleVisibility, kCtrlAlt | MOD_NOREPEAT, 'B',      L"Ctrl+Alt+B"},
        {Command::OpacityUp,        kCtrlAlt,                VK_PRIOR, L"Ctrl+Alt+PageUp"},
        {Command::OpacityDown,      kCtrlAlt,                VK_NEXT,  L"Ctrl+Alt+PageDown"},
        {Command::OpacityReset,     kCtrlAlt | MOD_NOREPEAT, VK_HOME,  L"Ctrl+Alt+Home"},
        {Command::Preset100,        kCtrlAlt | MOD_NOREPEAT, '1',      L"Ctrl+Alt+1"},
        {Command::Preset75,         kCtrlAlt | MOD_NOREPEAT, '2',      L"Ctrl+Alt+2"},
        {Command::Preset50,         kCtrlAlt | MOD_NOREPEAT, '3',      L"Ctrl+Alt+3"},
        {Command::Preset25,         kCtrlAlt | MOD_NOREPEAT, '4',      L"Ctrl+Alt+4"},
    };

}  // namespace

// ---------------------------------------------------------------------------
// Creation / destruction
// ---------------------------------------------------------------------------

MainWindow::~MainWindow() {
    // Runs after CefShutdown, so touch no CEF objects here.
    if (owner_ && IsWindow(owner_)) {
        DestroyWindow(owner_);
    }
    if (font_) {
        DeleteObject(font_);
    }
}

bool MainWindow::Create(HINSTANCE instance, HidingMode mode, const char* start_url) {
    instance_ = instance;
    mode_ = mode;

    // 1. The invisible owner (never shown).
    if (mode == HidingMode::HiddenOwner) {
        WNDCLASSEXW owner_class{};
        owner_class.cbSize = sizeof(owner_class);
        owner_class.lpfnWndProc = &DefWindowProcW;
        owner_class.hInstance = instance;
        owner_class.lpszClassName = kOwnerClassName;
        if (!RegisterClassExW(&owner_class)) {
            return false;
        }
        owner_ = CreateWindowExW(WS_EX_TOOLWINDOW, kOwnerClassName, L"", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
        if (!owner_) {
            return false;
        }
    }

    // 2. The main window class and window.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainWindow::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
    wc.lpszClassName = kMainClassName;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    DWORD ex_style = WS_EX_LAYERED;  // needed for SetLayeredWindowAttributes
    HWND owner_for_window = nullptr;
    if (mode == HidingMode::ToolWindow) {
        ex_style |= WS_EX_TOOLWINDOW;
    }
    else {
        owner_for_window = owner_;
    }

    const UINT system_dpi = GetDpiForSystem();
    hwnd_ = CreateWindowExW(
        ex_style, kMainClassName, kAppName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,  // CLIPCHILDREN: don't paint under the browser
        CW_USEDEFAULT, CW_USEDEFAULT,
        Scale(kInitialWidth, system_dpi), Scale(kInitialHeight, system_dpi),
        owner_for_window, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }

    // A layered window must have its attributes set before it is shown.
    if (!opacity_.Attach(hwnd_)) {
        return false;
    }
    UpdateTitle();
    Layout();

    // 3. The browser, as a CHILD window of ours, below the toolbar.
    RECT client_rect{};
    GetClientRect(hwnd_, &client_rect);
    const int width = static_cast<int>(client_rect.right);
    const int height = static_cast<int>(client_rect.bottom);
    const int bar = Scale(kToolbarHeight, dpi_);

    client_ = new BrowserClient(this);

    CefWindowInfo window_info;
    window_info.SetAsChild(hwnd_, CefRect(0, bar, std::max(width, 0), std::max(height - bar, 0)));
    window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    CefBrowserSettings browser_settings;
    if (!CefBrowserHost::CreateBrowser(window_info, client_, start_url, browser_settings,
        nullptr, nullptr)) {
        return false;
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void MainWindow::CreateControls(HWND parent) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_)) {
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    }

    const DWORD button_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    back_ = CreateWindowExW(0, L"BUTTON", L"Back", button_style, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBack)),
        instance_, nullptr);
    forward_ = CreateWindowExW(0, L"BUTTON", L"Forward", button_style, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdForward)),
        instance_, nullptr);
    reload_ = CreateWindowExW(0, L"BUTTON", L"Reload", button_style, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReload)),
        instance_, nullptr);
    address_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, parent, nullptr, instance_, nullptr);

    // Subclass the edit control so we can see Enter and Esc.
    SetWindowSubclass(address_, &MainWindow::AddressSubclassProc, 1,
        reinterpret_cast<DWORD_PTR>(this));

    if (font_) {
        for (const HWND control : {back_, forward_, reload_, address_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
    EnableWindow(back_, FALSE);
    EnableWindow(forward_, FALSE);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void MainWindow::Layout() {
    if (!hwnd_ || !address_) {
        return;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int width = static_cast<int>(rc.right);
    const int height = static_cast<int>(rc.bottom);

    const int bar = Scale(kToolbarHeight, dpi_);
    const int pad = Scale(kPadding, dpi_);
    const int control_height = bar - 2 * pad;

    int x = pad;
    const int back_width = Scale(kBackWidth, dpi_);
    MoveWindow(back_, x, pad, back_width, control_height, TRUE);
    x += back_width + pad;

    const int forward_width = Scale(kForwardWidth, dpi_);
    MoveWindow(forward_, x, pad, forward_width, control_height, TRUE);
    x += forward_width + pad;

    const int reload_width = Scale(kReloadWidth, dpi_);
    MoveWindow(reload_, x, pad, reload_width, control_height, TRUE);
    x += reload_width + pad;

    MoveWindow(address_, x, pad, std::max(width - x - pad, 0), control_height, TRUE);

    // The browser fills everything below the toolbar. We must move it
    // ourselves: a child window does not follow its parent's size.
    if (browser_) {
        const HWND browser_hwnd = browser_->GetHost()->GetWindowHandle();
        if (browser_hwnd) {
            SetWindowPos(browser_hwnd, nullptr, 0, bar, width, std::max(height - bar, 0),
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        dpi_ = GetDpiForWindow(hwnd);
        CreateControls(hwnd);
        RegisterHotkeys();
        return 0;

    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            Layout();
        }
        return 0;

    case WM_SETFOCUS:
        // Our window got focus: hand it to the browser so typing works.
        if (browser_) {
            browser_->GetHost()->SetFocus(true);
        }
        return 0;

    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            OnButton(static_cast<int>(LOWORD(wparam)));
            return 0;
        }
        break;

    case WM_HOTKEY: {
        const size_t index = static_cast<size_t>(wparam) - 1;  // ids start at 1
        if (index < std::size(kHotkeys)) {
            RunCommand(static_cast<int>(index));
        }
        return 0;
    }

    case WM_CLOSE:
        // Let the browser run its unload handlers first. TryCloseBrowser()
        // returns false while that is pending (we cancel this close); CEF then
        // sends WM_CLOSE again, TryCloseBrowser() returns true, and we fall
        // through to the default handler, which destroys the window.
        if (browser_ && !browser_->GetHost()->TryCloseBrowser()) {
            return 0;
        }
        break;

    case WM_DESTROY:
        UnregisterHotkeys();
        if (client_) {
            client_->DetachDelegate();
        }
        if (!browser_) {
            // No browser to wait for (never created): nothing will call
            // OnBeforeClose, so end the message loop ourselves.
            CefQuitMessageLoop();
        }
        // Release our reference now; CEF needs all browser references gone
        // before CefShutdown. (OnBeforeClose will quit the loop otherwise.)
        browser_ = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK MainWindow::AddressSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
    LPARAM lparam, UINT_PTR subclass_id,
    DWORD_PTR ref_data) {
    auto* self = reinterpret_cast<MainWindow*>(ref_data);

    switch (msg) {
    case WM_CHAR:
        if (wparam == VK_RETURN) {
            self->NavigateFromAddressBar();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            // Restore the current address and select it.
            SetWindowTextW(hwnd, self->current_url_.c_str());
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, &MainWindow::AddressSubclassProc, subclass_id);
        break;
    }

    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Toolbar actions
// ---------------------------------------------------------------------------

void MainWindow::OnButton(int id) {
    if (!browser_) {
        return;
    }

    switch (id) {
    case kIdBack:
        browser_->GoBack();
        break;
    case kIdForward:
        browser_->GoForward();
        break;
    case kIdReload:
        if (loading_) {
            browser_->StopLoad();
        }
        else {
            browser_->Reload();
        }
        break;
    default:
        return;
    }
    browser_->GetHost()->SetFocus(true);  // clicking a button stole focus
}

void MainWindow::NavigateFromAddressBar() {
    if (!browser_) {
        return;
    }

    const int length = GetWindowTextLengthW(address_);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(address_, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));

    const CefString url = ResolveInput(text);
    if (url.empty()) {
        return;
    }
    browser_->GetMainFrame()->LoadURL(url);
    browser_->GetHost()->SetFocus(true);
}

// ---------------------------------------------------------------------------
// Hotkeys and opacity
// ---------------------------------------------------------------------------

void MainWindow::RegisterHotkeys() {
    int id = 1;
    for (const HotkeyDef& def : kHotkeys) {
        if (!RegisterHotKey(hwnd_, id, def.modifiers, def.vk)) {
            hotkeys_failed_ += def.label;
            hotkeys_failed_ += L" ";
        }
        ++id;
    }
}

void MainWindow::UnregisterHotkeys() {
    for (int id = 1; id <= static_cast<int>(std::size(kHotkeys)); ++id) {
        UnregisterHotKey(hwnd_, id);  // harmless if that id was never registered
    }
}

void MainWindow::RunCommand(int command_index) {
    switch (kHotkeys[command_index].command) {
    case Command::ToggleVisibility:
        ToggleVisibility();
        return;
    case Command::OpacityUp:    opacity_.Adjust(+OpacityController::kStep); break;
    case Command::OpacityDown:  opacity_.Adjust(-OpacityController::kStep); break;
    case Command::OpacityReset: opacity_.SetPercent(100); break;
    case Command::Preset100:    opacity_.SetPercent(100); break;
    case Command::Preset75:     opacity_.SetPercent(75); break;
    case Command::Preset50:     opacity_.SetPercent(50); break;
    case Command::Preset25:     opacity_.SetPercent(25); break;
    }
    UpdateTitle();
}

void MainWindow::ToggleVisibility() {
    const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);

    if (visible && GetForegroundWindow() == hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);  // in front and focused: hide it
        return;
    }
    // Hidden, minimized, or behind other windows: bring it back.
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void MainWindow::UpdateTitle() {
    std::wstring title =
        page_title_.empty() ? std::wstring(kAppName) : page_title_ + L" - " + kAppName;
    title += L"  [" + std::to_wstring(opacity_.percent()) + L"%]";
    if (!hotkeys_failed_.empty()) {
        title += L"  [hotkey conflict: " + hotkeys_failed_ + L"]";
    }
    SetWindowTextW(hwnd_, title.c_str());
}

// ---------------------------------------------------------------------------
// BrowserDelegate (called by BrowserClient on the UI thread)
// ---------------------------------------------------------------------------

void MainWindow::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
    browser_ = browser;
    Layout();
    browser_->GetHost()->SetFocus(true);
}

void MainWindow::OnTitleChanged(const std::wstring& title) {
    page_title_ = title;
    UpdateTitle();
}

void MainWindow::OnAddressChanged(const std::wstring& url) {
    current_url_ = url;
    // Don't overwrite what the user is typing.
    if (GetFocus() != address_) {
        SetWindowTextW(address_, url.c_str());
    }
}

void MainWindow::OnLoadingStateChanged(bool is_loading, bool can_go_back,
    bool can_go_forward) {
    loading_ = is_loading;
    EnableWindow(back_, can_go_back ? TRUE : FALSE);
    EnableWindow(forward_, can_go_forward ? TRUE : FALSE);
    SetWindowTextW(reload_, is_loading ? L"Stop" : L"Reload");
}
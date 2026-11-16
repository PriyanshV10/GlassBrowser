#include "main_window.h"

#include <commctrl.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include "include/cef_app.h"
#include "include/cef_parser.h"

#pragma comment(lib, "comctl32.lib")

namespace {

    constexpr wchar_t kMainClassName[] = L"GlassBrowserMainWindow";
    constexpr wchar_t kOwnerClassName[] = L"GlassBrowserHiddenOwner";
    constexpr wchar_t kAppName[] = L"GlassBrowser";
    constexpr char kSearchUrl[] = "https://duckduckgo.com/?q=";

    // Control ids (buttons, edits) and menu command ids. All unique.
    constexpr int kIdBack = 101;
    constexpr int kIdForward = 102;
    constexpr int kIdReload = 103;
    constexpr int kIdHome = 104;
    constexpr int kIdMenu = 105;
    constexpr int kIdOpacity = 106;
    constexpr int kIdFindEdit = 107;
    constexpr int kIdFindPrev = 108;
    constexpr int kIdFindNext = 109;
    constexpr int kIdFindClose = 110;
    constexpr int kCmdFind = 201;
    constexpr int kCmdDevTools = 202;
    constexpr int kCmdPrivacy = 203;
    constexpr int kCmdTest = 204;
    constexpr int kCmdNewTab = 205;
    constexpr int kCmdReopenTab = 206;
    constexpr int kCmdCloseTab = 207;

    // Sizes are in "96-DPI pixels" and scaled to the window's DPI at runtime.
    constexpr int kTabStripHeight = 34;
    constexpr int kToolbarHeight = 40;
    constexpr int kFindBarHeight = 36;
    constexpr int kStatusHeight = 24;
    constexpr int kPadding = 6;
    constexpr int kBackWidth = 56;
    constexpr int kForwardWidth = 72;
    constexpr int kReloadWidth = 72;
    constexpr int kHomeWidth = 60;
    constexpr int kMenuWidth = 64;
    constexpr int kSliderWidth = 150;
    constexpr int kLabelWidth = 48;
    constexpr int kFindLabelWidth = 48;
    constexpr int kFindEditWidth = 260;
    constexpr int kFindStatusWidth = 90;
    constexpr int kFindButtonWidth = 60;
    constexpr int kProgressWidth = 140;
    constexpr int kInitialWidth = 1100;
    constexpr int kInitialHeight = 760;
    constexpr size_t kMaxClosedTabs = 20;

    int Scale(int value, UINT dpi) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    std::wstring GetText(HWND hwnd) {
        const int length = GetWindowTextLengthW(hwnd);
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(hwnd, text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        return text;
    }

    // Current Ctrl/Alt/Shift state, in CEF's flag format (for keys seen by our own controls).
    uint32_t CurrentModifiers() {
        uint32_t modifiers = 0;
        if (GetKeyState(VK_CONTROL) < 0) modifiers |= EVENTFLAG_CONTROL_DOWN;
        if (GetKeyState(VK_MENU) < 0)    modifiers |= EVENTFLAG_ALT_DOWN;
        if (GetKeyState(VK_SHIFT) < 0)   modifiers |= EVENTFLAG_SHIFT_DOWN;
        return modifiers;
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

    // ---- Global hotkeys (work even when another app is in front) --------------

    enum class Command {
        ToggleVisibility,
        OpacityUp,
        OpacityDown,
        OpacityReset,
        Preset100,
        Preset75,
        Preset50,
        Preset25,
        TogglePrivacy,
    };

    struct HotkeyDef {
        Command command;
        UINT modifiers;
        UINT vk;
        const wchar_t* label;
    };

    constexpr UINT kCtrlAlt = MOD_CONTROL | MOD_ALT;

    constexpr HotkeyDef kHotkeys[] = {
        { Command::ToggleVisibility, kCtrlAlt | MOD_NOREPEAT, 'B',      L"Ctrl+Alt+B" },
        { Command::OpacityUp,        kCtrlAlt,                VK_PRIOR, L"Ctrl+Alt+PageUp" },
        { Command::OpacityDown,      kCtrlAlt,                VK_NEXT,  L"Ctrl+Alt+PageDown" },
        { Command::OpacityReset,     kCtrlAlt | MOD_NOREPEAT, VK_HOME,  L"Ctrl+Alt+Home" },
        { Command::Preset100,        kCtrlAlt | MOD_NOREPEAT, '1',      L"Ctrl+Alt+1" },
        { Command::Preset75,         kCtrlAlt | MOD_NOREPEAT, '2',      L"Ctrl+Alt+2" },
        { Command::Preset50,         kCtrlAlt | MOD_NOREPEAT, '3',      L"Ctrl+Alt+3" },
        { Command::Preset25,         kCtrlAlt | MOD_NOREPEAT, '4',      L"Ctrl+Alt+4" },
        { Command::TogglePrivacy,    kCtrlAlt | MOD_NOREPEAT, 'P',      L"Ctrl+Alt+P" },
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
    home_url_ = CefString(start_url).ToWString();

    // Register the slider (trackbar) and progress-bar classes.
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

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
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,  // CLIPCHILDREN: don't paint under the browsers
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
    UpdateOpacityUi();
    Layout();

    // 3. The first tab.
    if (NewTab(home_url_, true, false) == 0) {
        return false;
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void MainWindow::BringToFront() {
    if (!hwnd_) {
        return;
    }
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void MainWindow::CreateControls(HWND parent) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_)) {
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    }

    // Row 0: the tab strip.
    tab_strip_.Create(parent, instance_, this, dpi_);
    if (font_) {
        tab_strip_.SetFont(font_);
    }

    const auto make_button = [&](const wchar_t* text, int id, DWORD style) {
        return CreateWindowExW(0, L"BUTTON", text, style, 0, 0, 0, 0, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_, nullptr);
        };
    const DWORD shown = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    const DWORD hidden = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON;  // find-bar buttons

    // Row 1: toolbar
    back_ = make_button(L"Back", kIdBack, shown);
    forward_ = make_button(L"Forward", kIdForward, shown);
    reload_ = make_button(L"Reload", kIdReload, shown);
    home_ = make_button(L"Home", kIdHome, shown);
    address_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, parent, nullptr, instance_, nullptr);
    slider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpacity)),
        instance_, nullptr);
    SendMessageW(slider_, TBM_SETRANGE, TRUE,
        MAKELPARAM(OpacityController::kMinPercent, OpacityController::kMaxPercent));
    SendMessageW(slider_, TBM_SETLINESIZE, 0, OpacityController::kStep);
    SendMessageW(slider_, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(slider_, TBM_SETPOS, TRUE, OpacityController::kMaxPercent);
    opacity_label_ = CreateWindowExW(0, L"STATIC", L"100%",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 0, 0, parent, nullptr, instance_, nullptr);
    menu_ = make_button(L"Menu", kIdMenu, shown);

    // Row 2: find bar (created hidden; ShowFindBar reveals it)
    find_label_ = CreateWindowExW(0, L"STATIC", L"Find:", WS_CHILD | SS_CENTERIMAGE,
        0, 0, 0, 0, parent, nullptr, instance_, nullptr);
    find_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFindEdit)),
        instance_, nullptr);
    find_status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_CENTERIMAGE,
        0, 0, 0, 0, parent, nullptr, instance_, nullptr);
    find_prev_ = make_button(L"Prev", kIdFindPrev, hidden);
    find_next_ = make_button(L"Next", kIdFindNext, hidden);
    find_close_ = make_button(L"Close", kIdFindClose, hidden);

    // Status bar
    status_text_ = CreateWindowExW(0, L"STATIC", L"Ready",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE |
        SS_ENDELLIPSIS,
        0, 0, 0, 0, parent, nullptr, instance_, nullptr);

    // Created last: Layout() uses this one as its "controls exist" check.
    progress_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH,
        0, 0, 0, 0, parent, nullptr, instance_, nullptr);

    // The address and find boxes see keys first so shortcuts work while typing.
    SetWindowSubclass(address_, &MainWindow::EditSubclassProc, 1,
        reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(find_edit_, &MainWindow::EditSubclassProc, 1,
        reinterpret_cast<DWORD_PTR>(this));

    if (font_) {
        for (const HWND control :
        {back_, forward_, reload_, home_, address_, opacity_label_, menu_, find_label_,
            find_edit_, find_status_, find_prev_, find_next_, find_close_, status_text_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
    EnableWindow(back_, FALSE);
    EnableWindow(forward_, FALSE);
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

MainWindow::Tab* MainWindow::FindTab(int tab_id) {
    for (Tab& tab : tabs_) {
        if (tab.id == tab_id) {
            return &tab;
        }
    }
    return nullptr;
}

MainWindow::Tab* MainWindow::ActiveTab() {
    return FindTab(active_id_);
}

CefRefPtr<CefBrowser> MainWindow::ActiveBrowser() {
    Tab* tab = ActiveTab();
    return tab ? tab->browser : nullptr;
}

// Tabs that count for "is this the last one?" (not already being closed).
int MainWindow::LiveTabCount() const {
    int count = 0;
    for (const Tab& tab : tabs_) {
        if (!tab.close_requested && !tab.close_approved) {
            ++count;
        }
    }
    return count;
}

// Tabs the user can switch to, in strip order.
std::vector<int> MainWindow::NavigableTabIds() const {
    std::vector<int> ids;
    for (const Tab& tab : tabs_) {
        if (!tab.close_approved) {
            ids.push_back(tab.id);
        }
    }
    return ids;
}

// Creates a tab (a new CEF browser, as a child window of ours).
int MainWindow::NewTab(const std::wstring& url, bool activate, bool focus_address) {
    Tab tab;
    tab.id = next_tab_id_++;
    tab.url = url;
    tab.focus_address = focus_address;
    tab.client = new BrowserClient(this, tab.id);

    const int id = tab.id;
    const CefRefPtr<BrowserClient> client = tab.client;
    tabs_.push_back(std::move(tab));  // registered BEFORE the browser exists

    const RECT area = ComputeBrowserRect();
    CefWindowInfo window_info;
    window_info.SetAsChild(hwnd_, CefRect(static_cast<int>(area.left), static_cast<int>(area.top),
        static_cast<int>(area.right - area.left),
        static_cast<int>(area.bottom - area.top)));
    window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    CefBrowserSettings browser_settings;
    if (!CefBrowserHost::CreateBrowser(window_info, client, CefString(url), browser_settings,
        nullptr, nullptr)) {
        tabs_.pop_back();
        return 0;
    }

    if (activate) {
        if (find_visible_) {
            ShowFindBar(false);  // find belongs to the page we are leaving
        }
        active_id_ = id;
        ShowOnlyActiveBrowser();
        SyncToolbarToActiveTab();
        if (focus_address) {
            FocusAddressBar();
        }
    }
    RefreshStrip();
    return id;
}

void MainWindow::SelectTab(int tab_id) {
    Tab* tab = FindTab(tab_id);
    if (!tab || tab->close_approved || tab_id == active_id_) {
        return;
    }
    if (find_visible_) {
        ShowFindBar(false);  // stops the search in the tab we are leaving
    }
    active_id_ = tab_id;
    ShowOnlyActiveBrowser();
    Layout();
    RefreshStrip();
    SyncToolbarToActiveTab();
    if (tab->browser) {
        tab->browser->GetHost()->SetFocus(true);
    }
}

void MainWindow::SelectRelativeTab(int delta) {
    const std::vector<int> ids = NavigableTabIds();
    if (ids.size() < 2) {
        return;
    }
    size_t index = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == active_id_) {
            index = i;
        }
    }
    const int count = static_cast<int>(ids.size());
    const int next = (static_cast<int>(index) + delta + count) % count;
    SelectTab(ids[static_cast<size_t>(next)]);
}

// Ctrl+1 .. Ctrl+8 = that tab; Ctrl+9 = the last tab.
void MainWindow::SelectTabByNumber(int number) {
    const std::vector<int> ids = NavigableTabIds();
    if (ids.empty()) {
        return;
    }
    if (number == 9) {
        SelectTab(ids.back());
    }
    else if (number >= 1 && static_cast<size_t>(number) <= ids.size()) {
        SelectTab(ids[static_cast<size_t>(number) - 1]);
    }
}

void MainWindow::CloseTab(int tab_id) {
    Tab* tab = FindTab(tab_id);
    if (!tab || tab->close_requested || tab->close_approved) {
        return;
    }

    // Never end up with zero tabs: open a fresh home tab first.
    if (LiveTabCount() <= 1) {
        NewTab(home_url_, true, false);
        tab = FindTab(tab_id);  // the vector may have reallocated
        if (!tab) {
            return;
        }
    }

    // Remember it for Ctrl+Shift+T.
    if (!tab->url.empty() && tab->url != L"about:blank") {
        closed_urls_.push_back(tab->url);
        if (closed_urls_.size() > kMaxClosedTabs) {
            closed_urls_.erase(closed_urls_.begin());
        }
    }

    tab->close_requested = true;
    if (tab->browser) {
        tab->browser->GetHost()->CloseBrowser(false);  // runs unload handlers, then DoClose
    }
    // (If the browser does not exist yet, OnTabBrowserCreated closes it on arrival.)
}

void MainWindow::ReopenClosedTab() {
    if (closed_urls_.empty()) {
        return;
    }
    const std::wstring url = closed_urls_.back();
    closed_urls_.pop_back();
    NewTab(url, true, false);
}

// Shows the active tab's browser window and hides all the others.
void MainWindow::ShowOnlyActiveBrowser() {
    for (Tab& tab : tabs_) {
        if (!tab.browser) {
            continue;
        }
        const HWND browser_hwnd = tab.browser->GetHost()->GetWindowHandle();
        if (browser_hwnd && IsWindow(browser_hwnd)) {
            const bool show = (tab.id == active_id_) && !tab.close_approved;
            ShowWindow(browser_hwnd, show ? SW_SHOW : SW_HIDE);
        }
    }
}

// Pushes the current tab list to the strip.
void MainWindow::RefreshStrip() {
    if (window_destroyed_) {
        return;
    }
    std::vector<TabStripItem> items;
    for (const Tab& tab : tabs_) {
        if (tab.close_approved) {
            continue;
        }
        TabStripItem item;
        item.id = tab.id;
        item.title = !tab.title.empty() ? tab.title
            : (!tab.url.empty() ? tab.url : std::wstring(L"New tab"));
        item.loading = tab.loading;
        items.push_back(std::move(item));
    }
    tab_strip_.SetItems(std::move(items), active_id_);
}

// Makes the toolbar, status bar and title show the ACTIVE tab's state.
void MainWindow::SyncToolbarToActiveTab() {
    if (window_destroyed_ || !progress_) {
        return;
    }
    const Tab* tab = ActiveTab();
    const bool loading = tab && tab->loading;

    EnableWindow(back_, (tab && tab->can_go_back) ? TRUE : FALSE);
    EnableWindow(forward_, (tab && tab->can_go_forward) ? TRUE : FALSE);
    SetWindowTextW(reload_, loading ? L"Stop" : L"Reload");

    ShowWindow(progress_, loading ? SW_SHOW : SW_HIDE);
    SendMessageW(progress_, PBM_SETPOS,
        static_cast<WPARAM>(tab ? tab->progress * 100.0 : 0.0), 0);

    if (GetFocus() != address_) {  // don't overwrite what the user is typing
        SetWindowTextW(address_, tab ? tab->url.c_str() : L"");
    }
    status_hover_.clear();
    UpdateStatus();
    UpdateTitle();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

// The rectangle (client coordinates) the browser should fill.
RECT MainWindow::ComputeBrowserRect() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int width = static_cast<int>(rc.right);
    const int height = static_cast<int>(rc.bottom);

    const int top = Scale(kTabStripHeight, dpi_) + Scale(kToolbarHeight, dpi_) +
        (find_visible_ ? Scale(kFindBarHeight, dpi_) : 0);
    const int bottom = height - Scale(kStatusHeight, dpi_);

    RECT area{ 0, top, width, std::max(bottom, top) };
    return area;
}

void MainWindow::Layout() {
    if (!hwnd_ || !progress_) {
        return;  // controls not created yet
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int width = static_cast<int>(rc.right);
    const int height = static_cast<int>(rc.bottom);

    const int pad = Scale(kPadding, dpi_);
    const int strip_height = Scale(kTabStripHeight, dpi_);
    const int bar = Scale(kToolbarHeight, dpi_);
    const int row_height = bar - 2 * pad;

    // ---- Row 0: tab strip ----
    MoveWindow(tab_strip_.hwnd(), 0, 0, width, strip_height, TRUE);

    // ---- Row 1: toolbar ----
    const int toolbar_y = strip_height + pad;
    int x = pad;
    const auto place = [&](HWND control, int control_width) {
        MoveWindow(control, x, toolbar_y, control_width, row_height, TRUE);
        x += control_width + pad;
        };
    place(back_, Scale(kBackWidth, dpi_));
    place(forward_, Scale(kForwardWidth, dpi_));
    place(reload_, Scale(kReloadWidth, dpi_));
    place(home_, Scale(kHomeWidth, dpi_));

    // Menu, opacity label and slider sit at the right edge; the address bar fills the rest.
    const int menu_width = Scale(kMenuWidth, dpi_);
    const int label_width = Scale(kLabelWidth, dpi_);
    const int slider_width = Scale(kSliderWidth, dpi_);
    const int menu_x = std::max(width - pad - menu_width, x);
    const int label_x = std::max(menu_x - pad - label_width, x);
    const int slider_x = std::max(label_x - pad - slider_width, x);
    MoveWindow(menu_, menu_x, toolbar_y, menu_width, row_height, TRUE);
    MoveWindow(opacity_label_, label_x, toolbar_y, label_width, row_height, TRUE);
    MoveWindow(slider_, slider_x, toolbar_y, slider_width, row_height, TRUE);
    MoveWindow(address_, x, toolbar_y, std::max(slider_x - pad - x, 0), row_height, TRUE);

    // ---- Row 2: find bar ----
    const HWND find_controls[] = { find_label_, find_edit_, find_status_,
                                  find_prev_,  find_next_, find_close_ };
    if (find_visible_) {
        const int find_height = Scale(kFindBarHeight, dpi_);
        const int find_y = strip_height + bar + (find_height - row_height) / 2;
        int find_x = pad;
        const auto place_find = [&](HWND control, int control_width) {
            MoveWindow(control, find_x, find_y, control_width, row_height, TRUE);
            find_x += control_width + pad;
            };
        place_find(find_label_, Scale(kFindLabelWidth, dpi_));
        place_find(find_edit_, Scale(kFindEditWidth, dpi_));
        place_find(find_status_, Scale(kFindStatusWidth, dpi_));
        place_find(find_prev_, Scale(kFindButtonWidth, dpi_));
        place_find(find_next_, Scale(kFindButtonWidth, dpi_));
        place_find(find_close_, Scale(kFindButtonWidth, dpi_));
        for (const HWND control : find_controls) {
            ShowWindow(control, SW_SHOW);
        }
    }
    else {
        for (const HWND control : find_controls) {
            ShowWindow(control, SW_HIDE);
        }
    }

    // ---- Status bar ----
    const int status_height = Scale(kStatusHeight, dpi_);
    const int status_y = height - status_height;
    const int progress_width = Scale(kProgressWidth, dpi_);
    const int progress_inset = Scale(5, dpi_);
    MoveWindow(progress_, std::max(width - pad - progress_width, 0), status_y + progress_inset,
        progress_width, status_height - 2 * progress_inset, TRUE);
    MoveWindow(status_text_, pad, status_y, std::max(width - 3 * pad - progress_width, 0),
        status_height, TRUE);

    // ---- The browsers. We must move them ourselves: a child window does not
    //      follow its parent's size. Every tab is sized, shown or not. ----
    const RECT area = ComputeBrowserRect();
    for (Tab& tab : tabs_) {
        if (!tab.browser) {
            continue;
        }
        const HWND browser_hwnd = tab.browser->GetHost()->GetWindowHandle();
        if (browser_hwnd && IsWindow(browser_hwnd)) {
            SetWindowPos(browser_hwnd, nullptr, static_cast<int>(area.left),
                static_cast<int>(area.top), static_cast<int>(area.right - area.left),
                static_cast<int>(area.bottom - area.top),
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

    case WM_SETFOCUS: {
        // Our window got focus: hand it to the active browser so typing works.
        const CefRefPtr<CefBrowser> browser = ActiveBrowser();
        if (browser) {
            browser->GetHost()->SetFocus(true);
        }
        return 0;
    }

    case WM_COMMAND: {
        const int id = static_cast<int>(LOWORD(wparam));
        const int code = static_cast<int>(HIWORD(wparam));
        if (code == EN_CHANGE && reinterpret_cast<HWND>(lparam) == find_edit_) {
            Find(true, false);  // live search as you type
            return 0;
        }
        // 0 covers both a button click (BN_CLICKED) and a menu item.
        if (code == 0) {
            OnCommand(id);
            return 0;
        }
        break;
    }

    case WM_HSCROLL:
        // Sent by the trackbar while it is dragged or keyed.
        if (reinterpret_cast<HWND>(lparam) == slider_) {
            opacity_.SetPercent(static_cast<int>(SendMessageW(slider_, TBM_GETPOS, 0, 0)));
            UpdateOpacityUi();
            if (LOWORD(wparam) == TB_ENDTRACK) {
                const CefRefPtr<CefBrowser> browser = ActiveBrowser();
                if (browser) {
                    browser->GetHost()->SetFocus(true);  // give typing back to the page
                }
            }
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

    case WM_CLOSE: {
        // WM_CLOSE reaches us in two situations:
        //  (a) CEF tells us a single tab's close was approved (after DoClose), or
        //  (b) the user closed the window (X / Alt+F4).
        if (!closing_app_) {
            bool destroyed_a_tab = false;
            for (Tab& tab : tabs_) {
                if (tab.close_approved && tab.browser) {
                    const HWND browser_hwnd = tab.browser->GetHost()->GetWindowHandle();
                    if (browser_hwnd && IsWindow(browser_hwnd)) {
                        // Destroying just this tab's window makes CEF release the browser.
                        DestroyWindow(browser_hwnd);
                        destroyed_a_tab = true;
                    }
                }
            }
            if (destroyed_a_tab) {
                return 0;  // a tab closed; the app stays open
            }
            closing_app_ = true;  // (b): the user wants to quit
        }

        // Quitting: every browser must agree. TryCloseBrowser() returns false while
        // a browser's unload handlers are pending; CEF then sends another WM_CLOSE
        // when that browser is ready. Only when ALL are ready may the window close.
        bool all_ready = true;
        for (Tab& tab : tabs_) {
            if (tab.browser && !tab.browser->GetHost()->TryCloseBrowser()) {
                all_ready = false;
            }
        }
        if (!all_ready) {
            return 0;
        }
        // A tab whose browser never got created will not produce OnBeforeClose.
        std::erase_if(tabs_, [](const Tab& tab) { return !tab.browser; });
        break;  // default handling destroys the window, and with it the browsers
    }

    case WM_DESTROY:
        window_destroyed_ = true;
        UnregisterHotkeys();
        // Quit only once every browser has finished closing (OnTabClosed does that
        // for the rest); the tabs hold the browser references until then.
        if (tabs_.empty()) {
            CefQuitMessageLoop();
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// Sees every key typed into the address bar and the find box.
LRESULT CALLBACK MainWindow::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
    LPARAM lparam, UINT_PTR subclass_id,
    DWORD_PTR ref_data) {
    auto* self = reinterpret_cast<MainWindow*>(ref_data);

    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        // App shortcuts first; everything else (typing, Ctrl+C/V, ...) stays normal.
        if (self->HandleShortcut(static_cast<int>(wparam), CurrentModifiers())) {
            return 0;
        }
        break;

    case WM_CHAR:
        if (hwnd == self->address_) {
            if (wparam == VK_RETURN) {
                self->NavigateFromAddressBar();
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                // Restore the active tab's address and select it.
                const Tab* tab = self->ActiveTab();
                SetWindowTextW(hwnd, tab ? tab->url.c_str() : L"");
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
        }
        else if (hwnd == self->find_edit_) {
            if (wparam == VK_RETURN) {
                self->Find(GetKeyState(VK_SHIFT) >= 0, true);  // Shift+Enter = previous
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                return 0;  // handled on key-down; swallow the character
            }
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, &MainWindow::EditSubclassProc, subclass_id);
        break;
    }

    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Commands, shortcuts, menu
// ---------------------------------------------------------------------------

void MainWindow::OnCommand(int id) {
    // These don't need a page.
    switch (id) {
    case kIdMenu: {
        ShowMenu();
        const CefRefPtr<CefBrowser> browser = ActiveBrowser();
        if (browser) {
            browser->GetHost()->SetFocus(true);  // the chosen item runs after this
        }
        return;
    }
    case kCmdPrivacy:
        SetPrivacyMode(!capture_privacy_.enabled());
        return;
    case kCmdNewTab:
        NewTab(home_url_, true, true);
        return;
    case kCmdReopenTab:
        ReopenClosedTab();
        return;
    case kCmdCloseTab:
        CloseTab(active_id_);
        return;
    default:
        break;
    }

    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (!browser) {
        return;
    }
    const Tab* tab = ActiveTab();

    switch (id) {
    case kIdBack:
        browser->GoBack();
        break;
    case kIdForward:
        browser->GoForward();
        break;
    case kIdReload:
        if (tab && tab->loading) {
            browser->StopLoad();
        }
        else {
            browser->Reload();
        }
        break;
    case kIdHome:
        browser->GetMainFrame()->LoadURL(CefString(home_url_));
        break;
    case kCmdTest:
        OpenTestPage();
        break;
    case kCmdFind:
        ShowFindBar(true);
        return;
    case kCmdDevTools:
        ToggleDevTools();
        return;  // the DevTools window takes focus
    case kIdFindPrev:
        Find(false, true);
        return;
    case kIdFindNext:
        Find(true, true);
        return;
    case kIdFindClose:
        ShowFindBar(false);
        return;
    default:
        return;
    }
    browser->GetHost()->SetFocus(true);  // clicking a button stole focus
}

void MainWindow::ShowMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdNewTab), L"New tab\tCtrl+T");
    AppendMenuW(menu, MF_STRING | (closed_urls_.empty() ? MF_GRAYED : MF_ENABLED),
        static_cast<UINT_PTR>(kCmdReopenTab), L"Reopen closed tab\tCtrl+Shift+T");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdCloseTab), L"Close tab\tCtrl+W");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdFind), L"Find in page\tCtrl+F");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdDevTools), L"Developer tools\tF12");
    AppendMenuW(menu, MF_STRING | (capture_privacy_.enabled() ? MF_CHECKED : MF_UNCHECKED),
        static_cast<UINT_PTR>(kCmdPrivacy), L"Privacy mode\tCtrl+Alt+P");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdTest), L"Open test page");

    RECT button{};
    GetWindowRect(menu_, &button);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, static_cast<int>(button.right),
        static_cast<int>(button.bottom), 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

// Returns true if the key was an app shortcut (and so must not reach the page / edit box).
bool MainWindow::HandleShortcut(int key, uint32_t modifiers) {
    const bool ctrl = (modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
    const bool alt = (modifiers & EVENTFLAG_ALT_DOWN) != 0;
    const bool shift = (modifiers & EVENTFLAG_SHIFT_DOWN) != 0;

    // ---- Tab shortcuts: these don't need a page to exist ----
    if (ctrl && !alt) {
        if (key == 'T') {
            if (shift) {
                ReopenClosedTab();
            }
            else {
                NewTab(home_url_, true, true);
            }
            return true;
        }
        if (key == 'W' && !shift) {
            CloseTab(active_id_);
            return true;
        }
        if (key == VK_TAB) {
            SelectRelativeTab(shift ? -1 : 1);
            return true;
        }
        if (!shift && key == VK_PRIOR) {
            SelectRelativeTab(-1);
            return true;
        }
        if (!shift && key == VK_NEXT) {
            SelectRelativeTab(1);
            return true;
        }
        if (!shift && key >= '1' && key <= '9') {
            SelectTabByNumber(key - '0');
            return true;
        }
    }

    // ---- Everything below acts on the active page ----
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (!browser) {
        return false;
    }

    if (ctrl && !alt) {
        // Zoom and hard reload accept Shift too (Ctrl+Shift+= is Ctrl+Plus on many keyboards).
        switch (key) {
        case VK_OEM_PLUS:
        case VK_ADD:
            ZoomBy(+0.5);
            return true;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            ZoomBy(-0.5);
            return true;
        case '0':
        case VK_NUMPAD0:
            ZoomReset();
            return true;
        case VK_F5:
            browser->ReloadIgnoreCache();
            return true;
        default:
            break;
        }
        if (!shift) {
            switch (key) {
            case 'L':
                FocusAddressBar();
                return true;
            case 'R':
                browser->Reload();
                return true;
            case 'F':
                ShowFindBar(true);
                return true;
            default:
                break;
            }
        }
        else {
            switch (key) {
            case 'R':
                browser->ReloadIgnoreCache();
                return true;
            case 'I':
                ToggleDevTools();
                return true;
            default:
                break;
            }
        }
        return false;
    }

    if (alt && !ctrl && !shift) {
        switch (key) {
        case VK_LEFT:
            browser->GoBack();
            return true;
        case VK_RIGHT:
            browser->GoForward();
            return true;
        case VK_HOME:
            browser->GetMainFrame()->LoadURL(CefString(home_url_));
            return true;
        case 'D':
            FocusAddressBar();
            return true;
        default:
            return false;
        }
    }

    if (!ctrl && !alt && !shift) {
        switch (key) {
        case VK_F5:
            browser->Reload();
            return true;
        case VK_F6:
            FocusAddressBar();
            return true;
        case VK_F12:
            ToggleDevTools();
            return true;
        case VK_ESCAPE:
            // Close the find bar, unless the user is in the address bar (Esc restores it there).
            if (find_visible_ && GetFocus() != address_) {
                ShowFindBar(false);
                return true;
            }
            return false;
        default:
            break;
        }
    }
    return false;
}

void MainWindow::FocusAddressBar() {
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (browser) {
        browser->GetHost()->SetFocus(false);  // blur the page
    }
    SetFocus(address_);
    SendMessageW(address_, EM_SETSEL, 0, -1);
}

void MainWindow::NavigateFromAddressBar() {
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (!browser) {
        return;
    }
    const CefString url = ResolveInput(GetText(address_));
    if (url.empty()) {
        return;
    }
    browser->GetMainFrame()->LoadURL(url);
    browser->GetHost()->SetFocus(true);
}

void MainWindow::ShowFindBar(bool show) {
    if (!show && !find_visible_) {
        return;
    }
    find_visible_ = show;
    Layout();

    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (show) {
        if (browser) {
            browser->GetHost()->SetFocus(false);
        }
        SetFocus(find_edit_);
        SendMessageW(find_edit_, EM_SETSEL, 0, -1);
    }
    else {
        SetWindowTextW(find_status_, L"");
        if (browser) {
            browser->GetHost()->StopFinding(true);  // clear the highlights
            browser->GetHost()->SetFocus(true);
        }
    }
}

void MainWindow::Find(bool forward, bool find_next) {
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (!browser) {
        return;
    }
    const std::wstring text = GetText(find_edit_);
    if (text.empty()) {
        browser->GetHost()->StopFinding(true);
        SetWindowTextW(find_status_, L"");
        return;
    }
    browser->GetHost()->Find(CefString(text), forward, false /*match case*/, find_next);
}

void MainWindow::ToggleDevTools() {
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (!browser) {
        return;
    }
    const CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (host->HasDevTools()) {
        host->CloseDevTools();
        return;
    }
    // Owned by our window => no taskbar button (same rule as Phase 2).
    CefWindowInfo info;
    info.SetAsPopup(hwnd_, "GlassBrowser DevTools");
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    host->ShowDevTools(info, nullptr, CefBrowserSettings(), CefPoint());
}

void MainWindow::ZoomBy(double delta) {
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (browser) {
        const CefRefPtr<CefBrowserHost> host = browser->GetHost();
        host->SetZoomLevel(host->GetZoomLevel() + delta);
    }
}

void MainWindow::ZoomReset() {
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (browser) {
        browser->GetHost()->SetZoomLevel(0.0);
    }
}

// Loads test_page.html from the folder this exe lives in (CMake copies it there).
void MainWindow::OpenTestPage() {
    const CefRefPtr<CefBrowser> browser = ActiveBrowser();
    if (!browser) {
        return;
    }

    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }

    std::wstring file(path, length);
    const size_t slash = file.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return;
    }
    file.resize(slash + 1);
    file += L"test_page.html";

    // C:\dir\x.html -> file:///C:/dir/x.html (spaces encoded)
    std::wstring url = L"file:///";
    for (const wchar_t c : file) {
        if (c == L'\\') {
            url += L'/';
        }
        else if (c == L' ') {
            url += L"%20";
        }
        else {
            url += c;
        }
    }
    browser->GetMainFrame()->LoadURL(CefString(url));
}

// ---------------------------------------------------------------------------
// Global hotkeys, opacity and privacy mode
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
    case Command::TogglePrivacy:
        SetPrivacyMode(!capture_privacy_.enabled());
        return;
    case Command::OpacityUp:    opacity_.Adjust(+OpacityController::kStep); break;
    case Command::OpacityDown:  opacity_.Adjust(-OpacityController::kStep); break;
    case Command::OpacityReset: opacity_.SetPercent(100); break;
    case Command::Preset100:    opacity_.SetPercent(100); break;
    case Command::Preset75:     opacity_.SetPercent(75); break;
    case Command::Preset50:     opacity_.SetPercent(50); break;
    case Command::Preset25:     opacity_.SetPercent(25); break;
    }
    UpdateOpacityUi();
}

void MainWindow::SetPrivacyMode(bool on) {
    DWORD error = 0;
    if (!capture_privacy_.SetEnabled(hwnd_, on, &error)) {
        const std::wstring message =
            L"Windows refused the capture-exclusion request (error " +
            std::to_wstring(error) +
            L").\n\nThis needs Windows 10 version 2004 or later with desktop "
            L"composition on. Privacy mode stays off.";
        MessageBoxW(hwnd_, message.c_str(), kAppName, MB_ICONWARNING | MB_OK);
    }
    UpdateTitle();  // the title shows the REAL state, even after a failure
}

void MainWindow::ToggleVisibility() {
    const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);

    if (visible && GetForegroundWindow() == hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);  // in front and focused: hide it
        return;
    }
    // Hidden, minimized, or behind other windows: bring it back.
    BringToFront();
}

// Makes the slider, its label and the title agree with opacity_.
void MainWindow::UpdateOpacityUi() {
    const int percent = opacity_.percent();

    if (slider_ && static_cast<int>(SendMessageW(slider_, TBM_GETPOS, 0, 0)) != percent) {
        SendMessageW(slider_, TBM_SETPOS, TRUE, percent);
    }
    if (opacity_label_) {
        const std::wstring text = std::to_wstring(percent) + L"%";
        SetWindowTextW(opacity_label_, text.c_str());
    }
    UpdateTitle();
}

void MainWindow::UpdateTitle() {
    const Tab* tab = ActiveTab();
    std::wstring title = (tab && !tab->title.empty()) ? tab->title + L" - " + kAppName
        : std::wstring(kAppName);
    title += L"  [" + std::to_wstring(opacity_.percent()) + L"%]";

    // Shown only when Windows itself reports the flag as set (a read-back).
    if (CapturePrivacy::IsExcluded(hwnd_)) {
        title += L"  [privacy ON]";
    }
    if (!hotkeys_failed_.empty()) {
        title += L"  [hotkey conflict: " + hotkeys_failed_ + L"]";
    }
    SetWindowTextW(hwnd_, title.c_str());
}

void MainWindow::UpdateStatus() {
    if (!status_text_) {
        return;
    }
    const Tab* tab = ActiveTab();
    const wchar_t* fallback = (tab && tab->loading) ? L"Loading..." : L"Ready";
    SetWindowTextW(status_text_, status_hover_.empty() ? fallback : status_hover_.c_str());
}

// ---------------------------------------------------------------------------
// BrowserDelegate (called by each tab's BrowserClient on the UI thread)
// ---------------------------------------------------------------------------

void MainWindow::OnTabBrowserCreated(int tab_id, CefRefPtr<CefBrowser> browser) {
    Tab* tab = FindTab(tab_id);
    if (!tab) {
        return;
    }
    tab->browser = browser;

    if (tab->close_requested) {
        // The user closed this tab before its browser existed.
        browser->GetHost()->CloseBrowser(false);
        return;
    }
    if (window_destroyed_) {
        return;
    }

    ShowOnlyActiveBrowser();  // a background tab's window is hidden right away
    Layout();
    if (tab_id == active_id_) {
        if (tab->focus_address) {
            FocusAddressBar();  // Ctrl+T: the page must not steal the focus
        }
        else {
            browser->GetHost()->SetFocus(true);
        }
    }
}

void MainWindow::OnTabTitleChanged(int tab_id, const std::wstring& title) {
    Tab* tab = FindTab(tab_id);
    if (!tab || window_destroyed_) {
        return;
    }
    tab->title = title;
    RefreshStrip();
    if (tab_id == active_id_) {
        UpdateTitle();
    }
}

void MainWindow::OnTabAddressChanged(int tab_id, const std::wstring& url) {
    Tab* tab = FindTab(tab_id);
    if (!tab || window_destroyed_) {
        return;
    }
    tab->url = url;
    if (tab->title.empty()) {
        RefreshStrip();  // the strip shows the URL until a title arrives
    }
    // Don't overwrite what the user is typing.
    if (tab_id == active_id_ && GetFocus() != address_) {
        SetWindowTextW(address_, url.c_str());
    }
}

void MainWindow::OnTabLoadingStateChanged(int tab_id, bool is_loading, bool can_go_back,
    bool can_go_forward) {
    Tab* tab = FindTab(tab_id);
    if (!tab || window_destroyed_) {
        return;
    }
    tab->loading = is_loading;
    tab->can_go_back = can_go_back;
    tab->can_go_forward = can_go_forward;
    if (!is_loading) {
        tab->progress = 0.0;
    }
    RefreshStrip();
    if (tab_id == active_id_) {
        SyncToolbarToActiveTab();
    }
}

void MainWindow::OnTabLoadingProgress(int tab_id, double progress) {
    Tab* tab = FindTab(tab_id);
    if (!tab || window_destroyed_) {
        return;
    }
    tab->progress = progress;
    if (tab_id == active_id_) {
        SendMessageW(progress_, PBM_SETPOS, static_cast<WPARAM>(progress * 100.0), 0);
    }
}

void MainWindow::OnTabStatusMessage(int tab_id, const std::wstring& text) {
    if (window_destroyed_ || tab_id != active_id_) {
        return;
    }
    status_hover_ = text;
    UpdateStatus();
}

void MainWindow::OnTabFindResult(int tab_id, int count, int active_ordinal,
    bool /*final_update*/) {
    if (window_destroyed_ || tab_id != active_id_) {
        return;
    }
    std::wstring text;
    if (!GetText(find_edit_).empty()) {
        text = count > 0 ? std::to_wstring(active_ordinal) + L"/" + std::to_wstring(count)
            : std::wstring(L"No matches");
    }
    SetWindowTextW(find_status_, text.c_str());
}

bool MainWindow::OnTabKey(int /*tab_id*/, int windows_key_code, uint32_t modifiers) {
    return HandleShortcut(windows_key_code, modifiers);
}

void MainWindow::OnTabNewTabRequested(int /*source_tab_id*/, const std::wstring& url,
    bool foreground) {
    if (window_destroyed_ || closing_app_) {
        return;
    }
    NewTab(url.empty() ? std::wstring(L"about:blank") : url, foreground, false);
}

// DoClose ran: the tab is going away. Drop it from the strip and move to a neighbor.
// CEF now posts WM_CLOSE to our window, where the tab's browser window gets destroyed.
void MainWindow::OnTabCloseApproved(int tab_id) {
    Tab* tab = FindTab(tab_id);
    if (!tab) {
        return;
    }
    tab->close_approved = true;
    if (window_destroyed_ || closing_app_) {
        return;
    }

    if (tab_id == active_id_) {
        const size_t index = static_cast<size_t>(tab - tabs_.data());
        int next = 0;
        for (size_t i = index + 1; i < tabs_.size() && next == 0; ++i) {
            if (!tabs_[i].close_approved) {
                next = tabs_[i].id;
            }
        }
        for (size_t i = index; i-- > 0 && next == 0;) {
            if (!tabs_[i].close_approved) {
                next = tabs_[i].id;
            }
        }
        active_id_ = next;
        if (find_visible_) {
            ShowFindBar(false);
        }
        ShowOnlyActiveBrowser();
        Layout();
        SyncToolbarToActiveTab();
        const CefRefPtr<CefBrowser> browser = ActiveBrowser();
        if (browser) {
            browser->GetHost()->SetFocus(true);
        }
    }
    ShowOnlyActiveBrowser();  // hides the closing tab's window
    RefreshStrip();
}

// OnBeforeClose: the browser is gone; release our references.
void MainWindow::OnTabClosed(int tab_id) {
    for (auto it = tabs_.begin(); it != tabs_.end(); ++it) {
        if (it->id == tab_id) {
            tabs_.erase(it);
            break;
        }
    }
    if (window_destroyed_) {
        if (tabs_.empty()) {
            CefQuitMessageLoop();  // last browser gone and the window is gone
        }
        return;
    }
    RefreshStrip();
}

// ---------------------------------------------------------------------------
// TabStripDelegate
// ---------------------------------------------------------------------------

void MainWindow::OnTabStripSelect(int tab_id) {
    SelectTab(tab_id);
}

void MainWindow::OnTabStripClose(int tab_id) {
    CloseTab(tab_id);
}

void MainWindow::OnTabStripNewTab() {
    NewTab(home_url_, true, true);
}
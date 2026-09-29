#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <string>

namespace {

    enum class HidingMode {
        Normal,       // control: behaves like any ordinary app
        ToolWindow,   // WS_EX_TOOLWINDOW
        HiddenOwner,  // owned by an invisible window
    };

    constexpr wchar_t kMainClassName[] = L"GlassBrowserMainWindow";
    constexpr wchar_t kOwnerClassName[] = L"GlassBrowserHiddenOwner";
    constexpr int kHotkeyId = 1;

    const wchar_t* ModeName(HidingMode mode) {
        switch (mode) {
        case HidingMode::Normal:      return L"Normal (control: should appear everywhere)";
        case HidingMode::ToolWindow:  return L"ToolWindow (WS_EX_TOOLWINDOW)";
        case HidingMode::HiddenOwner: return L"HiddenOwner (owned by an invisible window)";
        }
        return L"?";
    }

    const wchar_t* YesNo(bool value) { return value ? L"yes" : L"no"; }

    // An invisible top-level window that is never shown. Its only job is to be
    // the owner of the main window. RAII: the destructor destroys it.
    class HiddenOwner {
    public:
        HiddenOwner() = default;
        ~HiddenOwner() {
            if (hwnd_) {
                DestroyWindow(hwnd_);
            }
        }
        HiddenOwner(const HiddenOwner&) = delete;
        HiddenOwner& operator=(const HiddenOwner&) = delete;

        bool Create(HINSTANCE instance) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &DefWindowProcW;  // no custom behavior needed
            wc.hInstance = instance;
            wc.lpszClassName = kOwnerClassName;
            if (!RegisterClassExW(&wc)) {
                return false;
            }
            hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kOwnerClassName, L"", WS_POPUP,
                0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
            return hwnd_ != nullptr;
        }

        HWND hwnd() const { return hwnd_; }

    private:
        HWND hwnd_ = nullptr;
    };

    class MainWindow {
    public:
        bool Create(HINSTANCE instance, HWND owner, HidingMode mode, int show_command);

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
        void OnPaint();
        void ToggleVisibility();

        HWND hwnd_ = nullptr;
        HidingMode mode_ = HidingMode::Normal;
        bool active_ = false;
        bool hotkey_registered_ = false;
        POINT mouse_{};
        int left_clicks_ = 0;
        UINT last_key_ = 0;
    };

    bool MainWindow::Create(HINSTANCE instance, HWND owner, HidingMode mode, int show_command) {
        mode_ = mode;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &MainWindow::WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kMainClassName;  // no background brush: we paint everything
        if (!RegisterClassExW(&wc)) {
            return false;
        }

        DWORD ex_style = 0;
        HWND owner_for_window = nullptr;
        if (mode == HidingMode::ToolWindow) {
            ex_style |= WS_EX_TOOLWINDOW;
        }
        else if (mode == HidingMode::HiddenOwner) {
            owner_for_window = owner;  // no WS_CHILD, so this is an OWNER, not a parent
        }

        hwnd_ = CreateWindowExW(
            ex_style,
            kMainClassName,
            L"GlassBrowser - Phase 2",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            800, 500,
            owner_for_window,
            nullptr,
            instance,
            this);
        if (!hwnd_) {
            return false;
        }

        ShowWindow(hwnd_, show_command);
        UpdateWindow(hwnd_);
        return true;
    }

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
            hotkey_registered_ =
                RegisterHotKey(hwnd, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B') != FALSE;
            return 0;  // 0 = continue creating the window

        case WM_HOTKEY:
            if (static_cast<int>(wparam) == kHotkeyId) {
                ToggleVisibility();
            }
            return 0;

        case WM_ACTIVATE:
            active_ = (LOWORD(wparam) != WA_INACTIVE);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;  // NOT return: DefWindowProc gives us keyboard focus on activation

        case WM_MOUSEMOVE:
            mouse_.x = GET_X_LPARAM(lparam);
            mouse_.y = GET_Y_LPARAM(lparam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN:
            ++left_clicks_;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            else {
                last_key_ = static_cast<UINT>(wparam);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;  // "handled": OnPaint fills the whole client area itself

        case WM_PAINT:
            OnPaint();
            return 0;

        case WM_DESTROY:
            if (hotkey_registered_) {
                UnregisterHotKey(hwnd, kHotkeyId);
            }
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    void MainWindow::ToggleVisibility() {
        const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);

        if (visible && GetForegroundWindow() == hwnd_) {
            ShowWindow(hwnd_, SW_HIDE);  // in front and focused: hide it
            return;
        }
        // Hidden, minimized, or just behind other windows: bring it back.
        ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd_);
    }

    void MainWindow::OnPaint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);

        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(hdc, &client, reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1)));

        const LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        const bool has_owner = GetWindow(hwnd_, GW_OWNER) != nullptr;

        std::wstring text;
        text += L"Phase 2: window management\n\n";
        text += L"Mode: ";                       text += ModeName(mode_);
        text += L"\nWS_EX_TOOLWINDOW set: ";     text += YesNo((ex_style & WS_EX_TOOLWINDOW) != 0);
        text += L"\nWS_EX_APPWINDOW set: ";      text += YesNo((ex_style & WS_EX_APPWINDOW) != 0);
        text += L"\nHas an owner window: ";      text += YesNo(has_owner);
        text += L"\n\nWindow is active: ";       text += YesNo(active_);
        text += L"\nMouse (client coords): ";
        text += std::to_wstring(mouse_.x) + L", " + std::to_wstring(mouse_.y);
        text += L"\nLeft clicks: ";              text += std::to_wstring(left_clicks_);
        text += L"\nLast key (virtual-key code): ";
        text += std::to_wstring(last_key_);
        text += L"\n\nHotkey Ctrl+Alt+B registered: ";
        text += hotkey_registered_ ? L"yes" : L"NO (another app may already use it)";
        text += L"\nCtrl+Alt+B hides / shows / brings this window to front.";
        text += L"\nEsc closes the window.";

        RECT margin = client;
        InflateRect(&margin, -16, -16);
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, text.c_str(), -1, &margin, DT_LEFT | DT_TOP | DT_NOPREFIX);

        EndPaint(hwnd_, &ps);
    }

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prev_instance*/,
    PWSTR /*command_line*/, int show_command) {
    // ===== EXPERIMENT SWITCH: change this one line, rebuild, re-test =====
    //   HidingMode::Normal / HidingMode::ToolWindow / HidingMode::HiddenOwner
    const HidingMode mode = HidingMode::Normal;
    // =====================================================================

    HiddenOwner owner;  // declared before the window => destroyed after it
    if (!owner.Create(instance)) {
        MessageBoxW(nullptr, L"Failed to create the hidden owner window.", L"Error",
            MB_ICONERROR | MB_OK);
        return 1;
    }

    MainWindow window;
    if (!window.Create(instance, owner.hwnd(), mode, show_command)) {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"Error",
            MB_ICONERROR | MB_OK);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
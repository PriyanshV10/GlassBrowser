#include <windows.h>

#include <algorithm>
#include <iterator>
#include <string>

namespace {

    enum class HidingMode {
        ToolWindow,   // WS_EX_TOOLWINDOW (documented fallback)
        HiddenOwner,  // owned by an invisible window (our default)
    };

    constexpr wchar_t kMainClassName[] = L"GlassBrowserOpacityWindow";
    constexpr wchar_t kOwnerClassName[] = L"GlassBrowserHiddenOwner";

    const wchar_t* ModeName(HidingMode mode) {
        switch (mode) {
        case HidingMode::ToolWindow:  return L"ToolWindow (WS_EX_TOOLWINDOW)";
        case HidingMode::HiddenOwner: return L"HiddenOwner (invisible owner window)";
        }
        return L"?";
    }

    const wchar_t* YesNo(bool value) { return value ? L"yes" : L"no"; }

    // ---------------------------------------------------------------------------
    // Invisible owner window (same idea as Phase 2). RAII: destructor destroys it.
    // ---------------------------------------------------------------------------
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
            wc.lpfnWndProc = &DefWindowProcW;
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

    // ---------------------------------------------------------------------------
    // Owns the "how see-through is this window" state and applies it to the HWND.
    // The window must already have WS_EX_LAYERED before Attach() is called.
    // ---------------------------------------------------------------------------
    class OpacityController {
    public:
        static constexpr int kMinPercent = 10;
        static constexpr int kMaxPercent = 100;
        static constexpr int kStep = 5;

        bool Attach(HWND hwnd) {
            hwnd_ = hwnd;
            return Apply();
        }

        void SetPercent(int percent) {
            const int clamped = std::clamp(percent, kMinPercent, kMaxPercent);
            if (clamped == percent_) {
                return;
            }
            percent_ = clamped;
            Apply();
        }

        void Adjust(int delta) { SetPercent(percent_ + delta); }
        int percent() const { return percent_; }

    private:
        bool Apply() {
            // percent (0-100) -> alpha (0-255), rounded to nearest.
            const auto alpha = static_cast<BYTE>((percent_ * 255 + 50) / 100);
            return SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA) != FALSE;
        }

        HWND hwnd_ = nullptr;
        int percent_ = 100;
    };

    // ---------------------------------------------------------------------------
    // Commands and their global hotkeys (table-driven).
    // The hotkey id passed to Windows is (table index + 1).
    // ---------------------------------------------------------------------------
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

    class MainWindow {
    public:
        MainWindow() = default;
        ~MainWindow() {
            if (font_) {
                DeleteObject(font_);
            }
        }
        MainWindow(const MainWindow&) = delete;
        MainWindow& operator=(const MainWindow&) = delete;

        bool Create(HINSTANCE instance, HWND owner, HidingMode mode, int show_command);

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        void RegisterHotkeys();
        void UnregisterHotkeys();
        void RunCommand(Command command);
        void ToggleVisibility();
        void OnOpacityChanged();
        void OnPaint();

        HWND hwnd_ = nullptr;
        HWND edit_ = nullptr;
        HFONT font_ = nullptr;
        HidingMode mode_ = HidingMode::HiddenOwner;
        OpacityController opacity_;
        int hotkeys_registered_ = 0;
        std::wstring hotkeys_failed_;
    };

    bool MainWindow::Create(HINSTANCE instance, HWND owner, HidingMode mode, int show_command) {
        mode_ = mode;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &MainWindow::WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kMainClassName;
        if (!RegisterClassExW(&wc)) {
            return false;
        }

        DWORD ex_style = WS_EX_LAYERED;  // required for SetLayeredWindowAttributes
        HWND owner_for_window = nullptr;
        if (mode == HidingMode::ToolWindow) {
            ex_style |= WS_EX_TOOLWINDOW;
        }
        else {
            owner_for_window = owner;
        }

        hwnd_ = CreateWindowExW(
            ex_style,
            kMainClassName,
            L"GlassBrowser - Phase 3",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT,
            800, 600,
            owner_for_window,
            nullptr,
            instance,
            this);
        if (!hwnd_) {
            return false;
        }

        // A layered window is not shown until its attributes have been set,
        // so this must happen before ShowWindow.
        if (!opacity_.Attach(hwnd_)) {
            return false;
        }
        OnOpacityChanged();  // put the percentage in the title bar

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
        case WM_CREATE: {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);

            font_ = CreateFontW(-56, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            // An ordinary child window. It is sized later in WM_SIZE.
            edit_ = CreateWindowExW(
                0, L"EDIT",
                L"Type here. This is a child window; it should fade with its parent.",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
                0, 0, 0, 0, hwnd, nullptr, create->hInstance, nullptr);

            RegisterHotkeys();
            return 0;  // 0 = continue creating the window
        }

        case WM_SIZE: {
            if (edit_) {
                const int cx = LOWORD(lparam);
                const int cy = HIWORD(lparam);
                constexpr int kEditHeight = 120;
                constexpr int kMargin = 16;
                const int top = (cy > kEditHeight + kMargin) ? cy - kEditHeight - kMargin : 0;
                MoveWindow(edit_, kMargin, top, std::max(cx - 2 * kMargin, 0), kEditHeight, TRUE);
            }
            return 0;
        }

        case WM_HOTKEY: {
            const size_t index = static_cast<size_t>(wparam) - 1;  // ids start at 1
            if (index < std::size(kHotkeys)) {
                RunCommand(kHotkeys[index].command);
            }
            return 0;
        }

        case WM_KEYDOWN:
            // Only arrives when the *main window* (not the edit box) has focus.
            // Click the edit box and Esc stops working: a preview of the CEF focus issue.
            if (wparam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;  // OnPaint fills the whole client area itself

        case WM_PAINT:
            OnPaint();
            return 0;

        case WM_DESTROY:
            UnregisterHotkeys();
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    void MainWindow::RegisterHotkeys() {
        int id = 1;
        for (const HotkeyDef& def : kHotkeys) {
            if (RegisterHotKey(hwnd_, id, def.modifiers, def.vk)) {
                ++hotkeys_registered_;
            }
            else {
                hotkeys_failed_ += def.label;
                hotkeys_failed_ += L"  ";
            }
            ++id;
        }
    }

    void MainWindow::UnregisterHotkeys() {
        for (int id = 1; id <= static_cast<int>(std::size(kHotkeys)); ++id) {
            UnregisterHotKey(hwnd_, id);  // harmless if that id was never registered
        }
    }

    void MainWindow::RunCommand(Command command) {
        switch (command) {
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
        OnOpacityChanged();
    }

    void MainWindow::ToggleVisibility() {
        const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);

        if (visible && GetForegroundWindow() == hwnd_) {
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }
        ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd_);
    }

    void MainWindow::OnOpacityChanged() {
        const std::wstring title =
            L"Opacity " + std::to_wstring(opacity_.percent()) + L"% - GlassBrowser Phase 3";
        SetWindowTextW(hwnd_, title.c_str());
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MainWindow::OnPaint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);

        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(hdc, &client, reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1)));
        SetBkMode(hdc, TRANSPARENT);

        // 1. Big percentage.
        {
            const std::wstring big = std::to_wstring(opacity_.percent()) + L"%";
            RECT rect = client;
            rect.top = 8;
            rect.bottom = 90;
            HGDIOBJ old_font = SelectObject(hdc, font_);
            DrawTextW(hdc, big.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old_font);
        }

        // 2. Solid colour swatches, so you can judge the blend against the background.
        {
            const COLORREF colors[] = { RGB(200, 40, 40), RGB(40, 90, 200), RGB(0, 0, 0) };
            int x = 16;
            for (const COLORREF color : colors) {
                HBRUSH brush = CreateSolidBrush(color);
                RECT swatch{ x, 100, x + 100, 160 };
                FillRect(hdc, &swatch, brush);
                DeleteObject(brush);
                x += 116;
            }
        }

        // 3. Status text.
        {
            COLORREF key = 0;
            BYTE alpha = 0;
            DWORD flags = 0;
            GetLayeredWindowAttributes(hwnd_, &key, &alpha, &flags);
            const LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

            std::wstring text;
            text += L"Mode: ";
            text += ModeName(mode_);
            text += L"\nWS_EX_LAYERED set: ";
            text += YesNo((ex_style & WS_EX_LAYERED) != 0);
            text += L"      Applied alpha: " + std::to_wstring(alpha) + L" / 255";
            text += L"\nHotkeys registered: " + std::to_wstring(hotkeys_registered_) + L" of " +
                std::to_wstring(std::size(kHotkeys));
            if (!hotkeys_failed_.empty()) {
                text += L"\n  FAILED: " + hotkeys_failed_;
            }
            text += L"\n\nCtrl+Alt+PageUp / PageDown : more / less opaque (5% steps, minimum 10%)";
            text += L"\nCtrl+Alt+Home              : reset to 100%";
            text += L"\nCtrl+Alt+1 / 2 / 3 / 4     : 100% / 75% / 50% / 25%";
            text += L"\nCtrl+Alt+B                 : hide / show / bring to front";
            text += L"\nEsc closes (only when the edit box does NOT have focus)";

            RECT rect = client;
            rect.left = 16;
            rect.top = 176;
            rect.right -= 16;
            DrawTextW(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX);
        }

        EndPaint(hwnd_, &ps);
    }

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prev_instance*/,
    PWSTR /*command_line*/, int show_command) {
    const HidingMode mode = HidingMode::HiddenOwner;

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
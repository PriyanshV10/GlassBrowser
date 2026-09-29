#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kWindowClassName[] = L"GlassBrowserHelloWindow";

class Window {
public:
    bool Create(HINSTANCE instance, int show_command);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void OnPaint(HWND hwnd);

    HWND hwnd_ = nullptr;
};

bool Window::Create(HINSTANCE instance, int show_command) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"Hello, Win32",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 500,
        nullptr,
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

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<Window*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT Window::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Window::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT client{};
    GetClientRect(hwnd, &client);

    const std::wstring text =
        L"Hello, Win32!   Client area: " +
        std::to_wstring(client.right - client.left) + L" x " +
        std::to_wstring(client.bottom - client.top) +
        L"   (press Esc to close)";

    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), -1, &client,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    EndPaint(hwnd, &ps);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prev_instance*/,
                    PWSTR /*command_line*/, int show_command) {
    Window window;
    if (!window.Create(instance, show_command)) {
        MessageBoxW(nullptr, L"Failed to create the window.", L"Error",
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
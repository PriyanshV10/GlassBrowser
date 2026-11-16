#include "tab_strip.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>
#include <utility>

namespace {

    constexpr wchar_t kClassName[] = L"GlassBrowserTabStrip";

    // Sizes in "96-DPI pixels".
    constexpr int kLeftMargin = 6;
    constexpr int kTopMargin = 6;
    constexpr int kMinTabWidth = 90;
    constexpr int kMaxTabWidth = 220;
    constexpr int kNewTabWidth = 32;
    constexpr int kNewTabHeight = 24;
    constexpr int kCloseSize = 16;
    constexpr int kTextPadding = 12;
    constexpr int kDotSize = 8;

    constexpr COLORREF kStripBackground = RGB(0xDE, 0xE1, 0xE6);
    constexpr COLORREF kActiveFill = RGB(0xFF, 0xFF, 0xFF);
    constexpr COLORREF kHoverFill = RGB(0xEC, 0xEE, 0xF1);
    constexpr COLORREF kButtonHover = RGB(0xC8, 0xCB, 0xD0);
    constexpr COLORREF kTextColor = RGB(0x20, 0x21, 0x24);
    constexpr COLORREF kAccent = RGB(0x1A, 0x73, 0xE8);

}  // namespace

bool TabStrip::Create(HWND parent, HINSTANCE instance, TabStripDelegate* delegate, UINT dpi) {
    delegate_ = delegate;
    dpi_ = dpi;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TabStrip::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
        nullptr, instance, this);
    return hwnd_ != nullptr;
}

void TabStrip::SetItems(std::vector<TabStripItem> items, int active_id) {
    items_ = std::move(items);
    active_id_ = active_id;
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TabStrip::SetFont(HFONT font) {
    font_ = font;
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TabStrip::SetDpi(UINT dpi) {
    dpi_ = dpi;
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

int TabStrip::S(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

// ---- Geometry ---------------------------------------------------------------

int TabStrip::TabWidth(int client_width) const {
    if (items_.empty()) {
        return S(kMaxTabWidth);
    }
    const int available = client_width - S(kNewTabWidth) - S(kLeftMargin) - S(8);
    const int per_tab = available / static_cast<int>(items_.size());
    return std::clamp(per_tab, S(kMinTabWidth), S(kMaxTabWidth));
}

RECT TabStrip::TabRect(size_t index, int client_width, int client_height) const {
    const int width = TabWidth(client_width);
    const int left = S(kLeftMargin) + static_cast<int>(index) * width;
    return RECT{ left, S(kTopMargin), left + width, client_height };
}

RECT TabStrip::CloseRect(const RECT& tab) const {
    const int size = S(kCloseSize);
    const int right = static_cast<int>(tab.right) - S(8);
    const int top = static_cast<int>(tab.top) + (static_cast<int>(tab.bottom - tab.top) - size) / 2;
    return RECT{ right - size, top, right, top + size };
}

RECT TabStrip::NewTabRect(int client_width, int client_height) const {
    const int tab_width = TabWidth(client_width);
    const int button_width = S(kNewTabWidth);
    const int button_height = S(kNewTabHeight);
    int left = S(kLeftMargin) + static_cast<int>(items_.size()) * tab_width + S(4);
    left = std::min(left, client_width - button_width - S(2));
    const int top = S(kTopMargin) + (client_height - S(kTopMargin) - button_height) / 2;
    return RECT{ left, top, left + button_width, top + button_height };
}

TabStrip::Hit TabStrip::HitTest(POINT pt) const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = static_cast<int>(client.right);
    const int height = static_cast<int>(client.bottom);

    const RECT plus = NewTabRect(width, height);
    if (PtInRect(&plus, pt)) {
        return Hit{ HitKind::NewTab, 0 };
    }
    for (size_t i = 0; i < items_.size(); ++i) {
        const RECT tab = TabRect(i, width, height);
        if (!PtInRect(&tab, pt)) {
            continue;
        }
        const RECT close = CloseRect(tab);
        return Hit{ PtInRect(&close, pt) ? HitKind::Close : HitKind::Tab, items_[i].id };
    }
    return Hit{};
}

void TabStrip::SetHover(const Hit& hit) {
    if (hit.kind == hover_.kind && hit.tab_id == hover_.tab_id) {
        return;
    }
    hover_ = hit;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Messages ---------------------------------------------------------------

LRESULT CALLBACK TabStrip::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    TabStrip* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<TabStrip*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<TabStrip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT TabStrip::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_ERASEBKGND:
        return 1;  // OnPaint fills everything

    case WM_MOUSEMOVE: {
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd_;
            if (TrackMouseEvent(&track)) {
                tracking_mouse_ = true;
            }
        }
        const POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        SetHover(HitTest(pt));
        return 0;
    }

    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        SetHover(Hit{});
        return 0;

    case WM_LBUTTONDOWN: {
        const POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const Hit hit = HitTest(pt);
        if (!delegate_) {
            return 0;
        }
        switch (hit.kind) {
        case HitKind::Tab:    delegate_->OnTabStripSelect(hit.tab_id); break;
        case HitKind::Close:  delegate_->OnTabStripClose(hit.tab_id);  break;
        case HitKind::NewTab: delegate_->OnTabStripNewTab();           break;
        case HitKind::None:   break;
        }
        return 0;
    }

    case WM_MBUTTONUP: {
        const POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const Hit hit = HitTest(pt);
        if (delegate_ && (hit.kind == HitKind::Tab || hit.kind == HitKind::Close)) {
            delegate_->OnTabStripClose(hit.tab_id);  // middle-click closes
        }
        return 0;
    }
    }

    return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

// ---- Painting ---------------------------------------------------------------

void TabStrip::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = static_cast<int>(client.right);
    const int height = static_cast<int>(client.bottom);

    // Draw into an off-screen bitmap first, then copy it over: no flicker.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ old_bitmap = SelectObject(mem, bitmap);

    HBRUSH background = CreateSolidBrush(kStripBackground);
    FillRect(mem, &client, background);
    DeleteObject(background);

    HGDIOBJ old_font = font_ ? SelectObject(mem, font_) : nullptr;
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, kTextColor);

    HPEN pen = CreatePen(PS_SOLID, std::max(1, S(1)), kTextColor);
    HGDIOBJ null_pen = GetStockObject(NULL_PEN);
    HBRUSH accent = CreateSolidBrush(kAccent);
    HBRUSH button_hover = CreateSolidBrush(kButtonHover);

    for (size_t i = 0; i < items_.size(); ++i) {
        const TabStripItem& item = items_[i];
        const RECT tab = TabRect(i, width, height);
        const bool active = (item.id == active_id_);
        const bool hovered = hover_.tab_id == item.id &&
            (hover_.kind == HitKind::Tab || hover_.kind == HitKind::Close);

        // Tab body: rounded rectangle that overshoots the bottom so only the top corners show.
        HBRUSH fill = CreateSolidBrush(active ? kActiveFill
            : (hovered ? kHoverFill : kStripBackground));
        HGDIOBJ old_brush = SelectObject(mem, fill);
        HGDIOBJ old_pen = SelectObject(mem, null_pen);
        RoundRect(mem, tab.left, tab.top, tab.right, tab.bottom + S(12), S(10), S(10));
        SelectObject(mem, old_pen);
        SelectObject(mem, old_brush);
        DeleteObject(fill);

        // Loading indicator: a small accent-colored dot.
        int text_left = static_cast<int>(tab.left) + S(kTextPadding);
        if (item.loading) {
            const int dot_top =
                static_cast<int>(tab.top) + (static_cast<int>(tab.bottom - tab.top) - S(kDotSize)) / 2;
            old_brush = SelectObject(mem, accent);
            old_pen = SelectObject(mem, null_pen);
            Ellipse(mem, text_left, dot_top, text_left + S(kDotSize), dot_top + S(kDotSize));
            SelectObject(mem, old_pen);
            SelectObject(mem, old_brush);
            text_left += S(kDotSize) + S(6);
        }

        // Title, clipped with "..." before the close button.
        const RECT close = CloseRect(tab);
        RECT text_rect{ text_left, tab.top, static_cast<int>(close.left) - S(4), tab.bottom };
        DrawTextW(mem, item.title.c_str(), static_cast<int>(item.title.size()), &text_rect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        // Close button: only on the active tab or the one under the mouse.
        if (active || hovered) {
            if (hover_.kind == HitKind::Close && hover_.tab_id == item.id) {
                old_brush = SelectObject(mem, button_hover);
                old_pen = SelectObject(mem, null_pen);
                Ellipse(mem, close.left, close.top, close.right, close.bottom);
                SelectObject(mem, old_pen);
                SelectObject(mem, old_brush);
            }
            const int inset = S(4);
            old_pen = SelectObject(mem, pen);
            MoveToEx(mem, close.left + inset, close.top + inset, nullptr);
            LineTo(mem, close.right - inset, close.bottom - inset);
            MoveToEx(mem, close.right - inset, close.top + inset, nullptr);
            LineTo(mem, close.left + inset, close.bottom - inset);
            SelectObject(mem, old_pen);
        }
    }

    // The "+" (new tab) button.
    const RECT plus = NewTabRect(width, height);
    if (hover_.kind == HitKind::NewTab) {
        HGDIOBJ old_brush = SelectObject(mem, button_hover);
        HGDIOBJ old_pen = SelectObject(mem, null_pen);
        RoundRect(mem, plus.left, plus.top, plus.right, plus.bottom, S(8), S(8));
        SelectObject(mem, old_pen);
        SelectObject(mem, old_brush);
    }
    {
        const int center_x = static_cast<int>(plus.left + plus.right) / 2;
        const int center_y = static_cast<int>(plus.top + plus.bottom) / 2;
        const int arm = S(5);
        HGDIOBJ old_pen = SelectObject(mem, pen);
        MoveToEx(mem, center_x - arm, center_y, nullptr);
        LineTo(mem, center_x + arm + 1, center_y);
        MoveToEx(mem, center_x, center_y - arm, nullptr);
        LineTo(mem, center_x, center_y + arm + 1);
        SelectObject(mem, old_pen);
    }

    DeleteObject(pen);
    DeleteObject(accent);
    DeleteObject(button_hover);
    if (old_font) {
        SelectObject(mem, old_font);
    }

    BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}
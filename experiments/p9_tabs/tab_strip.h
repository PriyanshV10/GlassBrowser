#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct TabStripItem {
    int id = 0;
    std::wstring title;
    bool loading = false;
};

// Implemented by MainWindow: what to do when the user clicks the strip.
class TabStripDelegate {
public:
    virtual void OnTabStripSelect(int tab_id) = 0;
    virtual void OnTabStripClose(int tab_id) = 0;
    virtual void OnTabStripNewTab() = 0;

protected:
    ~TabStripDelegate() = default;
};

// A small owner-drawn tab bar: tab titles, a close button on each, a "+" button.
// It holds no tab state of its own beyond what MainWindow hands it via SetItems().
class TabStrip {
public:
    TabStrip() = default;
    TabStrip(const TabStrip&) = delete;
    TabStrip& operator=(const TabStrip&) = delete;

    bool Create(HWND parent, HINSTANCE instance, TabStripDelegate* delegate, UINT dpi);
    HWND hwnd() const { return hwnd_; }

    void SetItems(std::vector<TabStripItem> items, int active_id);
    void SetFont(HFONT font);  // not owned
    void SetDpi(UINT dpi);

private:
    enum class HitKind { None, Tab, Close, NewTab };
    struct Hit {
        HitKind kind = HitKind::None;
        int tab_id = 0;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

    int S(int value) const;  // scale a 96-DPI value to the current DPI
    int TabWidth(int client_width) const;
    RECT TabRect(size_t index, int client_width, int client_height) const;
    RECT CloseRect(const RECT& tab) const;
    RECT NewTabRect(int client_width, int client_height) const;
    Hit HitTest(POINT pt) const;

    void OnPaint();
    void SetHover(const Hit& hit);

    HWND hwnd_ = nullptr;
    TabStripDelegate* delegate_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;

    std::vector<TabStripItem> items_;
    int active_id_ = 0;
    Hit hover_;
    bool tracking_mouse_ = false;
};
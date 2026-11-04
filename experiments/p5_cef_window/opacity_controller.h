#pragma once

#include <windows.h>

#include <algorithm>

// Applies one constant alpha (10-100 %) to a WS_EX_LAYERED window.
// Same idea as Phase 3, now in its own header.
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
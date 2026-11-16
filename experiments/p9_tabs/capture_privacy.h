#pragma once

#include <windows.h>

// "Privacy mode": asks Windows to leave this process's windows out of screen
// capture (WDA_EXCLUDEFROMCAPTURE). It is a documented request honored by the
// compositor for supported capture paths. It is NOT invisibility: cameras,
// capture cards and capture outside this Windows session are unaffected.
class CapturePrivacy {
public:
    CapturePrivacy() = default;
    ~CapturePrivacy();
    CapturePrivacy(const CapturePrivacy&) = delete;
    CapturePrivacy& operator=(const CapturePrivacy&) = delete;

    // Turns the setting on or off. `main_window` must belong to this process.
    // Enabling returns false (and leaves everything unchanged) if Windows
    // refuses for the main window; *error then holds the Windows error code.
    bool SetEnabled(HWND main_window, bool enabled, DWORD* error);

    bool enabled() const { return enabled_; }

    // True if Windows reports the flag as set on `hwnd` (a read-back, not a guess).
    static bool IsExcluded(HWND hwnd);

private:
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG id_object, LONG id_child,
        DWORD event_thread, DWORD event_time);
    static BOOL CALLBACK CollectOwnWindows(HWND hwnd, LPARAM lparam);
    static bool Apply(HWND hwnd, DWORD affinity, DWORD* error);

    bool enabled_ = false;
    HWINEVENTHOOK hook_ = nullptr;
};
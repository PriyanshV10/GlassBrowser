#include "capture_privacy.h"

#include <vector>

// Defined in newer SDK headers; spelled out here so older ones still compile.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

CapturePrivacy::~CapturePrivacy() {
    if (hook_) {
        UnhookWinEvent(hook_);
    }
}

bool CapturePrivacy::Apply(HWND hwnd, DWORD affinity, DWORD* error) {
    if (SetWindowDisplayAffinity(hwnd, affinity)) {
        return true;
    }
    if (error) {
        *error = GetLastError();
    }
    return false;
}

bool CapturePrivacy::IsExcluded(HWND hwnd) {
    DWORD affinity = 0;
    return GetWindowDisplayAffinity(hwnd, &affinity) && affinity == WDA_EXCLUDEFROMCAPTURE;
}

// EnumWindows callback: collect the top-level windows owned by this process.
BOOL CALLBACK CapturePrivacy::CollectOwnWindows(HWND hwnd, LPARAM lparam) {
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == GetCurrentProcessId()) {
        reinterpret_cast<std::vector<HWND>*>(lparam)->push_back(hwnd);
    }
    return TRUE;  // keep enumerating
}

// Runs for window events in this process. Chromium shows dropdowns, context
// menus and tooltips as separate top-level windows; this puts the flag on them
// as they appear. (Events arrive asynchronously, so a popup can be on screen
// for a moment before this runs.)
void CALLBACK CapturePrivacy::OnWinEvent(HWINEVENTHOOK /*hook*/, DWORD event, HWND hwnd,
    LONG id_object, LONG id_child,
    DWORD /*event_thread*/, DWORD /*event_time*/) {
    if (event == EVENT_OBJECT_DESTROY || !hwnd || id_object != OBJID_WINDOW ||
        id_child != CHILDID_SELF) {
        return;
    }
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) {
        Apply(root, WDA_EXCLUDEFROMCAPTURE, nullptr);
    }
}

bool CapturePrivacy::SetEnabled(HWND main_window, bool enabled, DWORD* error) {
    if (enabled == enabled_) {
        return true;
    }

    if (!enabled) {
        // Turning off: best effort on every window, then stop watching.
        if (hook_) {
            UnhookWinEvent(hook_);
            hook_ = nullptr;
        }
        std::vector<HWND> windows;
        EnumWindows(&CapturePrivacy::CollectOwnWindows, reinterpret_cast<LPARAM>(&windows));
        for (const HWND hwnd : windows) {
            Apply(hwnd, WDA_NONE, nullptr);
        }
        enabled_ = false;
        return true;
    }

    // Turning on. Start watching first so windows created while we work are covered.
    hook_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr,
        &CapturePrivacy::OnWinEvent, GetCurrentProcessId(), 0,
        WINEVENT_OUTOFCONTEXT);
    if (!hook_) {
        if (error) {
            *error = GetLastError();
        }
        return false;
    }

    // The main window must succeed, and we verify by reading the value back.
    DWORD apply_error = 0;
    if (!Apply(main_window, WDA_EXCLUDEFROMCAPTURE, &apply_error) ||
        !IsExcluded(main_window)) {
        if (error) {
            *error = apply_error ? apply_error : ERROR_NOT_SUPPORTED;
        }
        UnhookWinEvent(hook_);
        hook_ = nullptr;
        Apply(main_window, WDA_NONE, nullptr);
        return false;
    }

    // Every other window that already exists (e.g. an open popup): best effort.
    std::vector<HWND> windows;
    EnumWindows(&CapturePrivacy::CollectOwnWindows, reinterpret_cast<LPARAM>(&windows));
    for (const HWND hwnd : windows) {
        Apply(hwnd, WDA_EXCLUDEFROMCAPTURE, nullptr);
    }

    enabled_ = true;
    return true;
}
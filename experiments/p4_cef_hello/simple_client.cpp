#include "simple_client.h"

#include <windows.h>

#include <string>

#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace {

    std::string HtmlEscape(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (const char c : in) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out += c;       break;
            }
        }
        return out;
    }

}  // namespace

void SimpleClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();

    // In windowed mode this handle is the browser's top-level window here.
    const HWND hwnd = browser->GetHost()->GetWindowHandle();
    if (hwnd) {
        SetWindowTextW(hwnd, title.ToWString().c_str());
    }
}

void SimpleClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browsers_.push_back(browser);
}

bool SimpleClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    return false;  // false = let CEF proceed with closing the window
}

void SimpleClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();

    for (auto it = browsers_.begin(); it != browsers_.end(); ++it) {
        if ((*it)->IsSame(browser)) {
            browsers_.erase(it);
            break;
        }
    }

    // Last browser gone: stop the message loop so wWinMain can call CefShutdown.
    if (browsers_.empty()) {
        CefQuitMessageLoop();
    }
}

void SimpleClient::OnLoadError(CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> frame,
    ErrorCode error_code,
    const CefString& error_text,
    const CefString& failed_url) {
    CEF_REQUIRE_UI_THREAD();

    // Ignore sub-frames and cancelled navigations.
    if (!frame->IsMain() || error_code == ERR_ABORTED) {
        return;
    }

    // Alloy style shows nothing on failure, so show our own tiny error page.
    const std::string html =
        "<html><body style='font-family:sans-serif'><h2>Failed to load page</h2><p>" +
        HtmlEscape(failed_url.ToString()) + "</p><p>" +
        HtmlEscape(error_text.ToString()) + " (error " +
        std::to_string(static_cast<int>(error_code)) + ")</p></body></html>";

    frame->LoadURL("data:text/html;base64," +
        CefURIEncode(CefBase64Encode(html.data(), html.size()), false).ToString());
}
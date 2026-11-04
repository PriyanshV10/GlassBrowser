#include "browser_client.h"

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

// ---- CefDisplayHandler ----------------------------------------------------

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> /*browser*/, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();
    if (delegate_) {
        delegate_->OnTitleChanged(title.ToWString());
    }
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> frame,
    const CefString& url) {
    CEF_REQUIRE_UI_THREAD();
    if (delegate_ && frame->IsMain()) {
        delegate_->OnAddressChanged(url.ToWString());
    }
}

// ---- CefLifeSpanHandler ---------------------------------------------------

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> /*frame*/,
    int /*popup_id*/,
    const CefString& target_url,
    const CefString& /*target_frame_name*/,
    WindowOpenDisposition /*target_disposition*/,
    bool /*user_gesture*/,
    const CefPopupFeatures& /*popup_features*/,
    CefWindowInfo& /*window_info*/,
    CefRefPtr<CefClient>& /*client*/,
    CefBrowserSettings& /*settings*/,
    CefRefPtr<CefDictionaryValue>& /*extra_info*/,
    bool* /*no_javascript_access*/) {
    CEF_REQUIRE_UI_THREAD();

    // A real popup would be a new top-level window (taskbar button!).
    // Phase 5: open the target in this browser instead. Phase 9 adds tabs.
    browser->GetMainFrame()->LoadURL(target_url);
    return true;  // true = cancel the popup
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browsers_.push_back(browser);
    if (delegate_) {
        delegate_->OnBrowserCreated(browser);
    }
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    // false = let CEF send WM_CLOSE to our top-level window (see MainWindow).
    return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();

    for (auto it = browsers_.begin(); it != browsers_.end(); ++it) {
        if ((*it)->IsSame(browser)) {
            browsers_.erase(it);
            break;
        }
    }

    // Last browser gone: stop the message loop so RunMain can call CefShutdown.
    if (browsers_.empty()) {
        CefQuitMessageLoop();
    }
}

// ---- CefLoadHandler -------------------------------------------------------

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> /*browser*/,
    bool is_loading,
    bool can_go_back,
    bool can_go_forward) {
    CEF_REQUIRE_UI_THREAD();
    if (delegate_) {
        delegate_->OnLoadingStateChanged(is_loading, can_go_back, can_go_forward);
    }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> /*browser*/,
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
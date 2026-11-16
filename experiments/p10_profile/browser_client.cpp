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
    delegate_->OnTabTitleChanged(tab_id_, title.ToWString());
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> frame,
    const CefString& url) {
    CEF_REQUIRE_UI_THREAD();
    if (frame->IsMain()) {
        delegate_->OnTabAddressChanged(tab_id_, url.ToWString());
    }
}

void BrowserClient::OnStatusMessage(CefRefPtr<CefBrowser> /*browser*/, const CefString& value) {
    CEF_REQUIRE_UI_THREAD();
    delegate_->OnTabStatusMessage(tab_id_, value.ToWString());
}

void BrowserClient::OnLoadingProgressChange(CefRefPtr<CefBrowser> /*browser*/, double progress) {
    CEF_REQUIRE_UI_THREAD();
    delegate_->OnTabLoadingProgress(tab_id_, progress);
}

// ---- CefLifeSpanHandler ---------------------------------------------------

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> /*frame*/,
    int /*popup_id*/,
    const CefString& target_url,
    const CefString& /*target_frame_name*/,
    WindowOpenDisposition target_disposition,
    bool user_gesture,
    const CefPopupFeatures& /*popup_features*/,
    CefWindowInfo& /*window_info*/,
    CefRefPtr<CefClient>& /*client*/,
    CefBrowserSettings& /*settings*/,
    CefRefPtr<CefDictionaryValue>& /*extra_info*/,
    bool* /*no_javascript_access*/) {
    CEF_REQUIRE_UI_THREAD();

    // A real popup would be a new top-level window (taskbar button!).
    // Block popups the user did not ask for; open the rest as tabs.
    if (user_gesture) {
        const bool background = (target_disposition == CEF_WOD_NEW_BACKGROUND_TAB);
        delegate_->OnTabNewTabRequested(tab_id_, target_url.ToWString(), !background);
    }
    return true;  // true = cancel the real popup
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    delegate_->OnTabBrowserCreated(tab_id_, browser);
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    delegate_->OnTabCloseApproved(tab_id_);
    // false = let CEF send WM_CLOSE to our top-level window (see MainWindow).
    return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    delegate_->OnTabClosed(tab_id_);
}

// ---- CefLoadHandler -------------------------------------------------------

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> /*browser*/,
    bool is_loading,
    bool can_go_back,
    bool can_go_forward) {
    CEF_REQUIRE_UI_THREAD();
    delegate_->OnTabLoadingStateChanged(tab_id_, is_loading, can_go_back, can_go_forward);
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

// ---- CefKeyboardHandler ---------------------------------------------------

bool BrowserClient::OnPreKeyEvent(CefRefPtr<CefBrowser> /*browser*/,
    const CefKeyEvent& event,
    CefEventHandle /*os_event*/,
    bool* /*is_keyboard_shortcut*/) {
    CEF_REQUIRE_UI_THREAD();
    if (event.type != KEYEVENT_RAWKEYDOWN) {
        return false;
    }
    return delegate_->OnTabKey(tab_id_, event.windows_key_code, event.modifiers);
}

// ---- CefFindHandler -------------------------------------------------------

void BrowserClient::OnFindResult(CefRefPtr<CefBrowser> /*browser*/,
    int /*identifier*/,
    int count,
    const CefRect& /*selection_rect*/,
    int active_match_ordinal,
    bool final_update) {
    CEF_REQUIRE_UI_THREAD();
    delegate_->OnTabFindResult(tab_id_, count, active_match_ordinal, final_update);
}
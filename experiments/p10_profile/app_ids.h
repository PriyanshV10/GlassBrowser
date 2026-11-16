#pragma once

// Names shared by the window code and the startup code, so they cannot drift apart.
namespace app_ids {

	inline constexpr wchar_t kMainWindowClass[] = L"GlassBrowserMainWindow";

	// Posted to the running instance's window by a second launch: "bring yourself to the front".
	inline constexpr wchar_t kActivateMessage[] = L"GlassBrowser.Activate";

	// Held by the running instance for its whole life.
	inline constexpr wchar_t kSingleInstanceMutex[] = L"Local\\GlassBrowser.SingleInstance";

}  // namespace app_ids
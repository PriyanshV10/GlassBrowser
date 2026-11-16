#include "profile.h"

#include <windows.h>

namespace {

    // Only folders under ...\GlassBrowser\sessions\... are ever deleted recursively.
    bool IsSessionPath(const std::wstring& path) {
        const std::wstring marker = L"\\GlassBrowser\\sessions\\";
        const size_t position = path.find(marker);
        return position != std::wstring::npos && path.size() > position + marker.size();
    }

    std::wstring EnvVar(const wchar_t* name) {
        const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
        if (size == 0) {
            return std::wstring();
        }
        std::wstring value(size, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
        value.resize(written);
        return value;
    }

    // %LOCALAPPDATA%\GlassBrowser (falls back to the temp folder).
    std::wstring BaseDirectory() {
        std::wstring base = EnvVar(L"LOCALAPPDATA");
        if (base.empty()) {
            wchar_t temp[MAX_PATH + 1];
            const DWORD length = GetTempPathW(MAX_PATH, temp);
            if (length == 0 || length > MAX_PATH) {
                return std::wstring();
            }
            base.assign(temp, length);
            while (!base.empty() && base.back() == L'\\') {
                base.pop_back();
            }
        }
        return base + L"\\GlassBrowser";
    }

    bool EnsureDirectory(const std::wstring& path) {
        if (CreateDirectoryW(path.c_str(), nullptr)) {
            return true;
        }
        return GetLastError() == ERROR_ALREADY_EXISTS;
    }

    // Deletes a folder and everything in it. Links are removed, never followed.
    // Returns true if the folder itself could be removed.
    bool DeleteTree(const std::wstring& directory) {
        WIN32_FIND_DATAW data;
        const HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..") {
                    continue;
                }
                const std::wstring path = directory + L"\\" + name;
                const bool is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                const bool is_link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                if (is_directory && !is_link) {
                    DeleteTree(path);
                }
                else {
                    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);  // clear read-only
                    if (is_directory) {
                        RemoveDirectoryW(path.c_str());  // a directory link
                    }
                    else {
                        DeleteFileW(path.c_str());
                    }
                }
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
        SetFileAttributesW(directory.c_str(), FILE_ATTRIBUTE_NORMAL);
        return RemoveDirectoryW(directory.c_str()) != FALSE;
    }

    // Removes session folders left behind by an earlier run that was killed.
    // Safe because only ONE instance of the app can run at a time (see main.cpp).
    void SweepStaleSessions(const std::wstring& sessions_directory) {
        WIN32_FIND_DATAW data;
        const HANDLE find = FindFirstFileW((sessions_directory + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            return;
        }
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            const bool is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool is_link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (!is_directory || is_link) {
                continue;  // only plain folders
            }
            const std::wstring path = sessions_directory + L"\\" + name;
            if (IsSessionPath(path)) {
                DeleteTree(path);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

}  // namespace

std::unique_ptr<Profile> Profile::Create(ProfileMode mode, std::wstring* error) {
    const auto fail = [&](const wchar_t* message) {
        if (error) {
            *error = message;
        }
        return std::unique_ptr<Profile>();
        };

    const std::wstring base = BaseDirectory();
    if (base.empty() || !EnsureDirectory(base)) {
        return fail(L"Could not create the data folder under %LOCALAPPDATA%\\GlassBrowser.");
    }

    std::unique_ptr<Profile> profile(new Profile());
    profile->mode_ = mode;

    if (mode == ProfileMode::Persistent) {
        const std::wstring root = base + L"\\User Data";
        const std::wstring cache = root + L"\\Default";
        if (!EnsureDirectory(root) || !EnsureDirectory(cache)) {
            return fail(L"Could not create the persistent profile folder.");
        }
        profile->root_path_ = root;
        profile->cache_path_ = cache;
        return profile;
    }

    // Temporary: a fresh session folder; cache_path stays empty = in-memory profile.
    const std::wstring sessions = base + L"\\sessions";
    if (!EnsureDirectory(sessions)) {
        return fail(L"Could not create the temporary session folder.");
    }
    SweepStaleSessions(sessions);

    const std::wstring session = sessions + L"\\" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64());
    if (!EnsureDirectory(session)) {
        return fail(L"Could not create the temporary session folder.");
    }
    profile->root_path_ = session;
    profile->owns_root_ = true;
    return profile;
}

Profile::~Profile() {
    if (!owns_root_ || !IsSessionPath(root_path_)) {
        return;
    }
    // CEF's helper processes can take a moment to release their files after
    // CefShutdown, so retry for up to ~5 seconds. If it still fails, the sweep
    // at the next start removes the folder.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (GetFileAttributesW(root_path_.c_str()) == INVALID_FILE_ATTRIBUTES) {
            break;  // already gone
        }
        if (DeleteTree(root_path_)) {
            break;
        }
        Sleep(250);
    }
}

void Profile::ApplyTo(CefSettings& settings) const {
    CefString(&settings.root_cache_path).FromWString(root_path_);
    if (!cache_path_.empty()) {
        CefString(&settings.cache_path).FromWString(cache_path_);
    }
}

std::wstring Profile::Describe() const {
    return mode_ == ProfileMode::Temporary
        ? std::wstring(L"Profile: temporary (in memory, deleted on exit)")
        : std::wstring(L"Profile: persistent (saved on disk)");
}
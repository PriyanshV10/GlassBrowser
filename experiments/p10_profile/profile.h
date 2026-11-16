#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

enum class ProfileMode {
    Temporary,   // in-memory profile; the session folder is deleted on exit
    Persistent,  // saved on disk under %LOCALAPPDATA%\GlassBrowser\User Data
};

// Chooses and prepares the folders CEF will use, and cleans up after itself.
// Create it BEFORE CefInitialize and let it be destroyed AFTER CefShutdown
// (the destructor deletes a temporary profile's folder).
class Profile {
public:
    // Returns null on failure and fills *error with a message for the user.
    static std::unique_ptr<Profile> Create(ProfileMode mode, std::wstring* error);
    ~Profile();
    Profile(const Profile&) = delete;
    Profile& operator=(const Profile&) = delete;

    ProfileMode mode() const { return mode_; }
    const std::wstring& root_path() const { return root_path_; }
    const std::wstring& cache_path() const { return cache_path_; }  // empty = in memory

    void ApplyTo(CefSettings& settings) const;
    std::wstring Describe() const;  // one line for the UI

private:
    Profile() = default;

    ProfileMode mode_ = ProfileMode::Temporary;
    std::wstring root_path_;
    std::wstring cache_path_;
    bool owns_root_ = false;  // true: delete root_path_ on exit
};
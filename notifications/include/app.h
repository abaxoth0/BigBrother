/**
 * @file app.h
 * @brief Notification identities, resources, registration and diagnostics.
 */

#pragma once

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <string>
#include <string_view>

namespace notifications {

/** @brief HRESULT failure with a borrowed operation description (normally a string literal). */
struct failure {
    HRESULT code;
    const wchar_t* operation;
};

/** @brief Throw failure for a failed HRESULT, preserving the operation description. */
inline void check(HRESULT result, const wchar_t* operation) {
    if (FAILED(result)) {
        throw failure{result, operation};
    }
}

/** @brief Throw failure for a false Win32 result using GetLastError(). */
inline void check_win32(BOOL result, const wchar_t* operation) {
    if (!result) {
        DWORD error = GetLastError();
        throw failure{HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE), operation};
    }
}

/** @brief Move-only Windows handle owner. get() borrows; put() exposes storage without closing it. */
class handle {
    HANDLE value_;

public:
    explicit handle(HANDLE value = nullptr) : value_(value) {}
    ~handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;
    handle(handle&& other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }
    HANDLE get() const {
        return value_;
    }
    HANDLE* put() {
        return &value_;
    }
};

/** @brief Noncopyable owner of an HSTRING created from a UTF-16 view. */
class hstring {
    HSTRING value_{};

public:
    explicit hstring(std::wstring_view text) {
        check(WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), &value_),
              L"WindowsCreateString");
    }
    ~hstring() {
        WindowsDeleteString(value_);
    }
    hstring(const hstring&) = delete;
    hstring& operator=(const hstring&) = delete;
    HSTRING get() const {
        return value_;
    }
};

/** @brief Notification identity, activation URI and icon metadata for one application. */
struct product {
    const wchar_t* name;
    const wchar_t* app_id;
    const wchar_t* activator;
    const wchar_t* scheme;
    int icon_index;
    std::wstring display_name() const {
        return std::wstring(L"BigBrother ") + name;
    }
    std::wstring uri() const {
        return std::wstring(scheme) + L":open";
    }
};

/** @brief Resolve Client/Server identity; throw failure for an unknown product. */
inline product find_product(std::wstring_view name) {
    if (name == L"Client") {
        return {L"Client", L"BigBrother.Client", L"{2172F3A2-9073-4665-9BA2-B503F7083176}",
                L"bigbrother-client", 0};
    }
    if (name == L"Server") {
        return {L"Server", L"BigBrother.Server", L"{79663FA3-2350-46E7-9040-00B879F513DF}",
                L"bigbrother-server", 1};
    }
    throw failure{E_INVALIDARG, L"Unknown notification product"};
}

/** @brief Return the current executable path, throwing failure if it cannot be read. */
inline std::wstring executable_path() {
    std::wstring path(32768, 0);
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    check_win32(length != 0 && length < path.size(), L"GetModuleFileName");
    path.resize(length);
    return path;
}

/** @brief Return the executable directory without a trailing separator. */
inline std::wstring app_directory() {
    auto path = executable_path();
    return path.substr(0, path.find_last_of(L'\\'));
}

/** @brief Ensure toast/activation registration; force_user bypasses existing machine registration. */
void ensure_registration(const product& app, bool force_user = false);
/** @brief Remove the current user's notification registration and shortcuts. */
void unregister_user(const product& app);
/** @brief Remove one profile's registration using a borrowed classes hive and shortcut folder. */
void unregister_profile(const product& app, HKEY classes, const std::wstring& folder);
/**
 * @brief Remove registrations across Windows profiles; requires backup/restore privileges.
 * @return ERROR_SUCCESS or a reported cleanup error; setup failures throw failure.
 */
DWORD unregister_all(const product& app);
/** @brief Register and submit a toast in the current user session; throw failure on delivery errors. */
void show_toast(const product& app, const std::wstring& title, const std::wstring& body);
/** @brief Log an error and publish it to the configured report mapping; return its HRESULT as DWORD. */
DWORD report_failure(const failure& error);
/** @brief Use existing stderr or attach the parent console; return whether output is available. */
bool attach_console_output();
/** @brief Enable or disable detailed diagnostic tracing for this helper process. */
void set_diagnostics(bool enabled);
/** @brief Return whether detailed tracing is enabled. */
bool diagnostics_enabled();
/** @brief Write a diagnostic to available stderr and the rotating notification log. */
void print_message(std::wstring_view message);
/** @brief Set the shared report mapping used to publish child diagnostics. */
void set_report_target(std::wstring target);
#ifdef DEBUG
/** @brief Record a debug trace only when detailed diagnostics are enabled. */
void trace(std::wstring_view message);
#define BB_TRACE(...) ::notifications::trace(__VA_ARGS__)
#else
#define BB_TRACE(...)                                                                                        \
    do {                                                                                                     \
    } while (0)
#endif
/** @brief Return whether the current process token belongs to LocalSystem. */
bool is_system();

}

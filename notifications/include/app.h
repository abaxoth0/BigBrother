#pragma once

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <string>
#include <string_view>

namespace notifications {

struct failure {
    HRESULT code;
    const wchar_t* operation;
};

inline void check(HRESULT result, const wchar_t* operation) {
    if (FAILED(result)) throw failure{result, operation};
}

inline void check_win32(BOOL result, const wchar_t* operation) {
    if (!result) {
        DWORD error = GetLastError();
        throw failure{HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE), operation};
    }
}

class handle {
    HANDLE value_;
public:
    explicit handle(HANDLE value = nullptr) : value_(value) {}
    ~handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;
    handle(handle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    HANDLE get() const { return value_; }
    HANDLE* put() { return &value_; }
};

class hstring {
    HSTRING value_{};
public:
    explicit hstring(std::wstring_view text) {
        check(WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), &value_), L"WindowsCreateString");
    }
    ~hstring() { WindowsDeleteString(value_); }
    hstring(const hstring&) = delete;
    hstring& operator=(const hstring&) = delete;
    HSTRING get() const { return value_; }
};

struct product {
    const wchar_t* name;
    const wchar_t* app_id;
    const wchar_t* activator;
    const wchar_t* scheme;
    int icon_index;
    std::wstring display_name() const { return std::wstring(L"BigBrother ") + name; }
    std::wstring uri() const { return std::wstring(scheme) + L":open"; }
};

inline product find_product(std::wstring_view name) {
    if (name == L"Client") return {L"Client", L"BigBrother.Client", L"{2172F3A2-9073-4665-9BA2-B503F7083176}", L"bigbrother-client", 0};
    if (name == L"Server") return {L"Server", L"BigBrother.Server", L"{79663FA3-2350-46E7-9040-00B879F513DF}", L"bigbrother-server", 1};
    throw failure{E_INVALIDARG, L"Unknown notification product"};
}

inline std::wstring executable_path() {
    std::wstring path(32768, 0);
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    check_win32(length != 0 && length < path.size(), L"GetModuleFileName");
    path.resize(length);
    return path;
}

inline std::wstring app_directory() {
    auto path = executable_path();
    return path.substr(0, path.find_last_of(L'\\'));
}

void ensure_registration(const product& app, bool force_user = false);
void unregister_user(const product& app);
void show_toast(const product& app, const std::wstring& title, const std::wstring& body);
DWORD report_failure(const failure& error);
bool attach_console_output();
void set_diagnostics(bool enabled);
bool diagnostics_enabled();
void trace(std::wstring_view message);
bool is_system();

}

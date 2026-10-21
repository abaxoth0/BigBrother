#include "app.h"
#include <shlobj.h>

namespace notifications {
namespace {

class profile_hive {
    std::wstring mount_;
    bool writable_ = false;
    DWORD& status_;
public:
    HKEY key = nullptr;
    profile_hive(const std::wstring& sid, const std::wstring& file, REGSAM access, DWORD& status) : status_(status) {
        LSTATUS result = RegOpenKeyExW(HKEY_USERS, sid.c_str(), 0, access, &key);
        if (result == ERROR_SUCCESS) return;
        check(HRESULT_FROM_WIN32(result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result), L"Open user registry hive");
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
            throw failure{HRESULT_FROM_WIN32(error), L"Locate user registry hive"};
        }
        GUID id{};
        check(CoCreateGuid(&id), L"Create registry cleanup identifier");
        wchar_t guid[40]{};
        StringFromGUID2(id, guid, ARRAYSIZE(guid));
        auto mount = std::wstring(L"BigBrother.NotificationCleanup.") + guid;
        check(HRESULT_FROM_WIN32(RegLoadKeyW(HKEY_USERS, mount.c_str(), file.c_str())), L"Load user registry for notification cleanup");
        result = RegOpenKeyExW(HKEY_USERS, mount.c_str(), 0, access, &key);
        if (result != ERROR_SUCCESS) {
            RegUnLoadKeyW(HKEY_USERS, mount.c_str());
            throw failure{HRESULT_FROM_WIN32(result), L"Open user registry for notification cleanup"};
        }
        mount_ = std::move(mount);
        writable_ = (access & KEY_SET_VALUE) != 0;
    }
    ~profile_hive() {
        if (key && writable_) {
            LSTATUS result = RegFlushKey(key);
            if (result != ERROR_SUCCESS) status_ = report_failure({HRESULT_FROM_WIN32(result), L"Flush notification registry cleanup"});
        }
        if (key) RegCloseKey(key);
        if (!mount_.empty()) {
            LSTATUS result = RegUnLoadKeyW(HKEY_USERS, mount_.c_str());
            if (result != ERROR_SUCCESS) status_ = report_failure({HRESULT_FROM_WIN32(result), L"Unload user registry after notification cleanup"});
        }
    }
    profile_hive(const profile_hive&) = delete;
    profile_hive& operator=(const profile_hive&) = delete;
};

void enable_cleanup_privilege(const wchar_t* name) {
    handle token;
    check_win32(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.put()), L"Open notification cleanup token");
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    check_win32(LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid), L"Find registry cleanup privilege");
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    check_win32(AdjustTokenPrivileges(token.get(), FALSE, &privileges, 0, nullptr, nullptr), L"Enable registry cleanup privilege");
    check(HRESULT_FROM_WIN32(GetLastError()), L"Acquire registry cleanup privilege");
}

std::wstring replace_variable(std::wstring value, const wchar_t* name, const std::wstring& replacement) {
    size_t length = wcslen(name);
    for (size_t i = 0; i + length <= value.size();) {
        if (!_wcsnicmp(value.c_str() + i, name, length)) {
            value.replace(i, length, replacement);
            i += replacement.size();
        } else i++;
    }
    return value;
}

std::wstring shell_folder(HKEY user, const wchar_t* name, const std::wstring& fallback,
                          const std::wstring& profile, const std::wstring& roaming, const std::wstring& local) {
    wchar_t value[32768];
    DWORD size = sizeof(value);
    if (!user || RegGetValueW(user, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders", name,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, value, &size) != ERROR_SUCCESS) return fallback;
    auto path = replace_variable(value, L"%USERPROFILE%", profile);
    path = replace_variable(path, L"%APPDATA%", roaming);
    path = replace_variable(path, L"%LOCALAPPDATA%", local);
    wchar_t expanded[32768];
    DWORD length = ExpandEnvironmentStringsW(path.c_str(), expanded, ARRAYSIZE(expanded));
    return length && length <= ARRAYSIZE(expanded) ? expanded : path;
}
}

DWORD unregister_all(const product& app) {
    enable_cleanup_privilege(SE_BACKUP_NAME);
    enable_cleanup_privilege(SE_RESTORE_NAME);
    HKEY profiles;
    check(HRESULT_FROM_WIN32(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
        0, KEY_READ | KEY_WOW64_64KEY, &profiles)), L"Enumerate notification user profiles");
    struct close_key { HKEY key; ~close_key() { RegCloseKey(key); } } closer{profiles};
    DWORD status = ERROR_SUCCESS;
    for (DWORD index = 0;; index++) {
        wchar_t sid[256];
        DWORD length = ARRAYSIZE(sid);
        LSTATUS result = RegEnumKeyExW(profiles, index, sid, &length, nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        check(HRESULT_FROM_WIN32(result), L"Read notification user profile");
        wchar_t path[32768];
        DWORD size = sizeof(path);
        result = RegGetValueW(profiles, sid, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, path, &size);
        if (result == ERROR_FILE_NOT_FOUND) continue;
        try {
            check(HRESULT_FROM_WIN32(result), L"Read notification profile directory");
            wchar_t expanded[32768];
            DWORD count = ExpandEnvironmentStringsW(path, expanded, ARRAYSIZE(expanded));
            check_win32(count && count <= ARRAYSIZE(expanded), L"Expand notification profile directory");
            std::wstring profile(expanded);
            profile_hive user(sid, profile + L"\\NTUSER.DAT", KEY_READ, status);
            auto roaming = shell_folder(user.key, L"AppData", profile + L"\\AppData\\Roaming", profile, profile + L"\\AppData\\Roaming", profile + L"\\AppData\\Local");
            auto local = shell_folder(user.key, L"Local AppData", profile + L"\\AppData\\Local", profile, roaming, profile + L"\\AppData\\Local");
            auto programs = shell_folder(user.key, L"Programs", roaming + L"\\Microsoft\\Windows\\Start Menu\\Programs", profile, roaming, local);
            profile_hive classes(std::wstring(sid) + L"_Classes", local + L"\\Microsoft\\Windows\\UsrClass.dat", KEY_READ | KEY_WRITE, status);
            unregister_profile(app, classes.key, programs + L"\\" + app.display_name());
        } catch (const failure& error) {
            print_message(std::wstring(L"Notification cleanup for profile ") + sid);
            status = report_failure(error);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return status;
}
}

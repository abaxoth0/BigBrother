#include "app.h"
#include <shlobj.h>
#include <cassert>
#include <cstdio>

void profile_cleanup_tests() {
    using namespace notifications;
    handle token;
    check_win32(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.put()), L"Open profile test token");
    for (const wchar_t* name : {SE_BACKUP_NAME, SE_RESTORE_NAME}) {
        TOKEN_PRIVILEGES privilege{};
        privilege.PrivilegeCount = 1;
        check_win32(LookupPrivilegeValueW(nullptr, name, &privilege.Privileges[0].Luid), L"Find profile test privilege");
        privilege.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        if (!AdjustTokenPrivileges(token.get(), FALSE, &privilege, 0, nullptr, nullptr) || GetLastError() != ERROR_SUCCESS) {
            puts("SKIP: offline profile cleanup needs backup and restore privileges.");
            return;
        }
    }
    wchar_t temporary[MAX_PATH];
    check_win32(GetTempPathW(ARRAYSIZE(temporary), temporary) != 0, L"Locate profile fixture");
    GUID id{};
    check(CoCreateGuid(&id), L"Create profile fixture identifier");
    auto suffix = std::to_wstring(id.Data1) + L"-" + std::to_wstring(id.Data2) + L"-" + std::to_wstring(id.Data3);
    auto sid = L"S-1-5-21-" + suffix + L"-" + std::to_wstring(GetCurrentProcessId());
    auto verify = L"BigBrother.OfflineCleanupVerify." + suffix;
    auto fixture_key = L"Software\\BigBrother.OfflineCleanupTest." + suffix;
    auto profile = std::wstring(temporary) + L"BigBrother.OfflineCleanupTest." + suffix;
    auto local = profile + L"\\AppData\\Local\\Microsoft\\Windows";
    auto user_file = profile + L"\\NTUSER.DAT";
    auto classes_file = local + L"\\UsrClass.dat";
    SHCreateDirectoryExW(nullptr, local.c_str(), nullptr);
    HKEY fixture;
    check(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CURRENT_USER, fixture_key.c_str(), 0, nullptr, 0,
        KEY_ALL_ACCESS, nullptr, &fixture, nullptr)), L"Create profile fixture registry");
    HKEY machine, user, classes;
    check(HRESULT_FROM_WIN32(RegCreateKeyExW(fixture, L"Machine", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &machine, nullptr)), L"Create machine fixture");
    check(HRESULT_FROM_WIN32(RegCreateKeyExW(fixture, L"User", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &user, nullptr)), L"Create user fixture");
    check(HRESULT_FROM_WIN32(RegCreateKeyExW(fixture, L"Classes", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &classes, nullptr)), L"Create classes fixture");
    auto set = [](HKEY key, const wchar_t* path, const wchar_t* name, const std::wstring& value) {
        check(HRESULT_FROM_WIN32(RegSetKeyValueW(key, path, name, REG_SZ, value.c_str(), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))), L"Write profile fixture");
    };
    set(machine, (L"Software\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\" + sid).c_str(), L"ProfileImagePath", profile);
    product app{L"OfflineCleanupTest", L"BigBrother.OfflineCleanupTest", L"{946DEF97-69AA-4F51-B1E1-2EDD849AA378}", L"bigbrother-offline-cleanup-test", 0};
    set(classes, L"AppUserModelId\\BigBrother.OfflineCleanupTest", L"BigBrotherNotificationHost", executable_path());
    set(classes, L"bigbrother-offline-cleanup-test\\shell\\open\\command", nullptr, L"\"" + executable_path() + L"\" --open OfflineCleanupTest \"%1\"");
    DeleteFileW(user_file.c_str());
    DeleteFileW(classes_file.c_str());
    check(HRESULT_FROM_WIN32(RegSaveKeyW(user, user_file.c_str(), nullptr)), L"Save offline user fixture");
    check(HRESULT_FROM_WIN32(RegSaveKeyW(classes, classes_file.c_str(), nullptr)), L"Save offline classes fixture");
    RegCloseKey(user);
    RegCloseKey(classes);
    check(HRESULT_FROM_WIN32(RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine)), L"Isolate profile enumeration");
    DWORD result;
    try { result = unregister_all(app); }
    catch (...) { RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr); throw; }
    RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
    assert(result == ERROR_SUCCESS);
    check(HRESULT_FROM_WIN32(RegLoadKeyW(HKEY_USERS, verify.c_str(), classes_file.c_str())), L"Verify offline classes cleanup");
    wchar_t value[256];
    DWORD size = sizeof(value);
    LSTATUS query = RegGetValueW(HKEY_USERS, (verify + L"\\AppUserModelId\\BigBrother.OfflineCleanupTest").c_str(), L"BigBrotherNotificationHost",
        RRF_RT_REG_SZ, nullptr, value, &size);
    bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    if (wine) puts("SKIP: Wine does not persist changes to loaded hive files; verify offline hive persistence on Windows.");
    else assert(query == ERROR_FILE_NOT_FOUND);
    assert(RegUnLoadKeyW(HKEY_USERS, verify.c_str()) == ERROR_SUCCESS);
    auto loaded_sid = sid + L"_Classes";
    check(HRESULT_FROM_WIN32(RegLoadKeyW(HKEY_USERS, loaded_sid.c_str(), classes_file.c_str())), L"Load signed-in profile fixture");
    check(HRESULT_FROM_WIN32(RegOpenKeyExW(HKEY_USERS, loaded_sid.c_str(), 0, KEY_ALL_ACCESS, &classes)), L"Open signed-in profile fixture");
    set(classes, L"AppUserModelId\\BigBrother.OfflineCleanupTest", L"BigBrotherNotificationHost", executable_path());
    check(HRESULT_FROM_WIN32(RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine)), L"Isolate signed-in profile enumeration");
    try { result = unregister_all(app); }
    catch (...) { RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr); throw; }
    RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
    assert(result == ERROR_SUCCESS);
    size = sizeof(value);
    assert(RegGetValueW(classes, L"AppUserModelId\\BigBrother.OfflineCleanupTest", L"BigBrotherNotificationHost",
        RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_FILE_NOT_FOUND);
    RegCloseKey(classes);
    LSTATUS unload = RegUnLoadKeyW(HKEY_USERS, loaded_sid.c_str());
    if (wine && unload != ERROR_SUCCESS) {
        printf("SKIP: Wine cannot unload the synthetic classes hive (%ld); removing the fixture branch.\n", unload);
        RegDeleteTreeW(HKEY_USERS, loaded_sid.c_str());
    } else assert(unload == ERROR_SUCCESS);
    RegCloseKey(machine);
    RegCloseKey(fixture);
    RegDeleteTreeW(HKEY_CURRENT_USER, fixture_key.c_str());
    DeleteFileW(user_file.c_str());
    DeleteFileW(classes_file.c_str());
    puts("PASS: user profile enumeration, hive loading and signed-in profile cleanup.");
}

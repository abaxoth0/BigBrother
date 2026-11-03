#include "app.h"
#include <shlobj.h>
#include <cassert>
#include <cstdio>

void cleanup_tests() {
    using namespace notifications;
    using Microsoft::WRL::ComPtr;
    check(RoInitialize(RO_INIT_MULTITHREADED), L"Initialize cleanup test");
    product app{L"CleanupTest", L"BigBrother.CleanupTest", L"{946DEF97-69AA-4F51-B1E1-2EDD849AA378}", L"bigbrother-cleanup-test", 0};
    HKEY classes;
    check(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\BigBrother.NotificationCleanupTest", 0, nullptr, 0,
        KEY_ALL_ACCESS, nullptr, &classes, nullptr)), L"Create cleanup fixture");
    auto set = [&](const wchar_t* path, const wchar_t* name, const std::wstring& value) {
        check(HRESULT_FROM_WIN32(RegSetKeyValueW(classes, path, name, REG_SZ, value.c_str(), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))), L"Write cleanup fixture");
    };
    wchar_t temporary[MAX_PATH];
    check_win32(GetTempPathW(ARRAYSIZE(temporary), temporary) != 0, L"Locate cleanup test directory");
    auto folder = std::wstring(temporary) + L"BigBrother.NotificationCleanupTest";
    auto shortcut = folder + L"\\" + app.display_name() + L".lnk";
    auto save_shortcut = [&](const std::wstring& target) {
        CreateDirectoryW(folder.c_str(), nullptr);
        ComPtr<IShellLinkW> link;
        check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)), L"Create cleanup shortcut");
        check(link->SetPath(target.c_str()), L"Set cleanup shortcut target");
        ComPtr<IPersistFile> file;
        check(link.As(&file), L"Save cleanup shortcut");
        check(file->Save(shortcut.c_str(), TRUE), L"Write cleanup shortcut");
    };
    auto identity = L"AppUserModelId\\BigBrother.CleanupTest";
    auto protocol = L"bigbrother-cleanup-test\\shell\\open\\command";
    set(identity, L"BigBrotherNotificationHost", executable_path());
    set(protocol, nullptr, L"\"" + executable_path() + L"\" --open CleanupTest \"%1\"");
    save_shortcut(executable_path());
    unregister_profile(app, classes, folder);
    wchar_t value[32768];
    DWORD size = sizeof(value);
    assert(RegGetValueW(classes, identity, L"BigBrotherNotificationHost", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_FILE_NOT_FOUND);
    assert(GetFileAttributesW(shortcut.c_str()) == INVALID_FILE_ATTRIBUTES);
    unregister_profile(app, classes, folder);
    auto foreign = app_directory() + L"\\another-installation.exe";
    set(identity, L"BigBrotherNotificationHost", executable_path());
    set(protocol, nullptr, L"\"" + foreign + L"\" --open CleanupTest \"%1\"");
    save_shortcut(executable_path());
    unregister_profile(app, classes, folder);
    size = sizeof(value);
    assert(RegGetValueW(classes, protocol, nullptr, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS);
    assert(GetFileAttributesW(shortcut.c_str()) == INVALID_FILE_ATTRIBUTES);
    set(identity, L"BigBrotherNotificationHost", foreign);
    set(protocol, nullptr, L"\"" + foreign + L"\" --open CleanupTest \"%1\"");
    save_shortcut(foreign);
    unregister_profile(app, classes, folder);
    size = sizeof(value);
    assert(RegGetValueW(classes, identity, L"BigBrotherNotificationHost", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS);
    assert(GetFileAttributesW(shortcut.c_str()) != INVALID_FILE_ATTRIBUTES);
    RegCloseKey(classes);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\BigBrother.NotificationCleanupTest");
    DeleteFileW(shortcut.c_str());
    RemoveDirectoryW(folder.c_str());
    RoUninitialize();
    puts("PASS: registration cleanup is idempotent and preserves other installations.");
}

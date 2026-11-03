#include "app.h"
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>

namespace notifications {
using Microsoft::WRL::ComPtr;

static std::wstring programs_path(REFKNOWNFOLDERID folder, const product& app) {
    PWSTR path = nullptr;
    check(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &path), L"Locate Start menu");
    std::wstring result(path);
    CoTaskMemFree(path);
    return result + L"\\" + app.display_name();
}

static std::wstring shortcut_path(REFKNOWNFOLDERID folder, const product& app) {
    return programs_path(folder, app) + L"\\" + app.display_name() + L".lnk";
}

static ComPtr<IShellLinkW> shell_link() {
    ComPtr<IShellLinkW> link;
    check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)), L"Create Start shortcut");
    return link;
}

static bool shortcut_identity(REFKNOWNFOLDERID folder, const product& app) {
    auto link = shell_link();
    ComPtr<IPersistFile> file;
    check(link.As(&file), L"Query shortcut file");
    if (FAILED(file->Load(shortcut_path(folder, app).c_str(), STGM_READ))) return false;
    ComPtr<IPropertyStore> properties;
    if (FAILED(link.As(&properties))) return false;
    PROPVARIANT id{};
    HRESULT result = properties->GetValue(PKEY_AppUserModel_ID, &id);
    bool matches = SUCCEEDED(result) && id.vt == VT_LPWSTR && id.pwszVal && !wcscmp(id.pwszVal, app.app_id);
    PropVariantClear(&id);
    if (!matches) return false;
    PROPVARIANT activator{};
    result = properties->GetValue(PKEY_AppUserModel_ToastActivatorCLSID, &activator);
    CLSID expected{};
    check(CLSIDFromString(app.activator, &expected), L"Parse toast activator");
    matches = SUCCEEDED(result) && activator.vt == VT_CLSID && activator.puuid && IsEqualGUID(*activator.puuid, expected);
    PropVariantClear(&activator);
    return matches;
}

static std::wstring protocol_command(const product& app) {
    return L"\"" + executable_path() + L"\" --open " + app.name + L" \"%1\"";
}

static void registry_string(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    LSTATUS result = RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    check(HRESULT_FROM_WIN32(result), L"Register notification activation protocol");
}

void ensure_registration(const product& app, bool force_user) {
    auto mutex_name = std::wstring(L"Local\\BigBrother.ToastRegistration.") + app.name;
    handle mutex(CreateMutexW(nullptr, FALSE, mutex_name.c_str()));
    check_win32(mutex.get() != nullptr, L"Create notification registration mutex");
    DWORD wait = WaitForSingleObject(mutex.get(), 5000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        throw failure{HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Wait for notification registration"};
    struct unlock { HANDLE mutex; ~unlock() { ReleaseMutex(mutex); } } lock{mutex.get()};
    auto identity = std::wstring(L"Software\\Classes\\AppUserModelId\\") + app.app_id;
    wchar_t existing[32768];
    DWORD size = sizeof(existing);
    bool protocol_matches = RegGetValueW(HKEY_CLASSES_ROOT, (std::wstring(app.scheme) + L"\\shell\\open\\command").c_str(),
        nullptr, RRF_RT_REG_SZ, nullptr, existing, &size) == ERROR_SUCCESS && existing == protocol_command(app);
    size = sizeof(existing);
    bool identity_matches = RegGetValueW(HKEY_CLASSES_ROOT, (std::wstring(L"AppUserModelId\\") + app.app_id).c_str(),
        L"CustomActivator", RRF_RT_REG_SZ, nullptr, existing, &size) == ERROR_SUCCESS && !_wcsicmp(existing, app.activator);
    if (!force_user && identity_matches && protocol_matches && (shortcut_identity(FOLDERID_CommonPrograms, app) || shortcut_identity(FOLDERID_Programs, app))) {
        BB_TRACE(L"Existing shortcut identity and activation protocol validated");
        return;
    }
    registry_string(identity, L"DisplayName", app.display_name());
    registry_string(identity, L"CustomActivator", app.activator);
    registry_string(identity, L"BigBrotherNotificationHost", executable_path());
    std::wstring folder = programs_path(FOLDERID_Programs, app);
    check_win32(CreateDirectoryW(folder.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS, L"Create Start menu folder");
    auto link = shell_link();
    check(link->SetPath(executable_path().c_str()), L"Set notification shortcut path");
    check(link->SetArguments((std::wstring(L"--open ") + app.name).c_str()), L"Set notification shortcut arguments");
    check(link->SetWorkingDirectory(app_directory().c_str()), L"Set notification shortcut directory");
    check(link->SetIconLocation(executable_path().c_str(), app.icon_index), L"Set notification icon");
    ComPtr<IPropertyStore> properties;
    check(link.As(&properties), L"Query shortcut properties");
    PROPVARIANT value{};
    check(InitPropVariantFromString(app.app_id, &value), L"Create notification identity");
    HRESULT result = properties->SetValue(PKEY_AppUserModel_ID, value);
    PropVariantClear(&value);
    check(result, L"Set notification identity");
    CLSID activator{};
    check(CLSIDFromString(app.activator, &activator), L"Parse toast activator");
    check(InitPropVariantFromCLSID(activator, &value), L"Create toast activator property");
    result = properties->SetValue(PKEY_AppUserModel_ToastActivatorCLSID, value);
    PropVariantClear(&value);
    check(result, L"Set toast activator");
    check(properties->Commit(), L"Save notification properties");
    ComPtr<IPersistFile> file;
    check(link.As(&file), L"Query shortcut file");
    check(file->Save(shortcut_path(FOLDERID_Programs, app).c_str(), TRUE), L"Save notification shortcut");
    BB_TRACE(L"Saved Start menu shortcut: " + shortcut_path(FOLDERID_Programs, app));
    auto key = std::wstring(L"Software\\Classes\\") + app.scheme;
    registry_string(key, nullptr, L"URL:" + app.display_name());
    registry_string(key, L"URL Protocol", L"");
    registry_string(key + L"\\shell\\open\\command", nullptr, protocol_command(app));
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void unregister_profile(const product& app, HKEY classes, const std::wstring& folder) {
    auto identity = std::wstring(L"AppUserModelId\\") + app.app_id;
    wchar_t owner[32768];
    DWORD owner_size = sizeof(owner);
    if (classes && RegGetValueW(classes, identity.c_str(), L"BigBrotherNotificationHost", RRF_RT_REG_SZ,
        nullptr, owner, &owner_size) == ERROR_SUCCESS && !_wcsicmp(owner, executable_path().c_str()))
        check(HRESULT_FROM_WIN32(RegDeleteTreeW(classes, identity.c_str())), L"Remove notification identity");
    wchar_t command[32768];
    DWORD size = sizeof(command);
    if (classes && RegGetValueW(classes, (std::wstring(app.scheme) + L"\\shell\\open\\command").c_str(), nullptr,
        RRF_RT_REG_SZ, nullptr, command, &size) == ERROR_SUCCESS && command == protocol_command(app))
        check(HRESULT_FROM_WIN32(RegDeleteTreeW(classes, app.scheme)), L"Remove notification protocol");
    auto path = folder + L"\\" + app.display_name() + L".lnk";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        auto link = shell_link();
        ComPtr<IPersistFile> file;
        check(link.As(&file), L"Query shortcut file");
        if (SUCCEEDED(file->Load(path.c_str(), STGM_READ))) {
            wchar_t target[32768];
            if (SUCCEEDED(link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH)) &&
                !_wcsicmp(target, executable_path().c_str())) {
                check_win32(DeleteFileW(path.c_str()), L"Remove notification shortcut");
                RemoveDirectoryW(folder.c_str());
            }
        }
    }
}

void unregister_user(const product& app) {
    HKEY classes = nullptr;
    LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_READ | KEY_WRITE, &classes);
    if (result != ERROR_FILE_NOT_FOUND) check(HRESULT_FROM_WIN32(result), L"Open user notification registration");
    try { unregister_profile(app, classes, programs_path(FOLDERID_Programs, app)); }
    catch (...) { if (classes) RegCloseKey(classes); throw; }
    if (classes) RegCloseKey(classes);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
}

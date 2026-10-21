#include "transport.h"
#include "app.h"
#include "command_line.h"
#include "report.h"
#include <shlobj.h>
#include <propkey.h>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <vector>

void delivery_tests();
void process_tests();

template<class Action> static void rejects(Action action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

static void command_line_tests() {
    using namespace notifications;
    auto parse = [](std::initializer_list<const wchar_t*> arguments) {
        std::vector<wchar_t*> args;
        for (auto argument : arguments) args.push_back(const_cast<wchar_t*>(argument));
        return parse_command(static_cast<int>(args.size()), args.data());
    };
    assert(parse({L"helper.exe"}).kind == command_kind::test);
    assert(parse({L"helper.exe", L"--show"}).kind == command_kind::test);
    assert(parse({L"helper.exe", L"--test"}).kind == command_kind::test);
    auto server = parse({L"helper.exe", L"--show", L"Server"});
    assert(server.kind == command_kind::test && !wcscmp(server.app.name, L"Server"));
    auto internal = parse({L"helper.exe", L"--show", L"Client", L"54657374", L"626f6479"});
    assert(internal.kind == command_kind::show && internal.title == L"54657374" && internal.body == L"626f6479");
    assert(parse({L"helper.exe", L"--send", L"Server", L"54657374", L"626f6479"}).kind == command_kind::send);
    assert(parse({L"helper.exe", L"--help"}).kind == command_kind::help);
    bool rejected = false;
    try { parse({L"helper.exe", L"--send"}); } catch (const failure& error) { rejected = error.code == E_INVALIDARG; }
    assert(rejected);
    rejects([&] { parse({L"helper.exe", L"--show", L"Client", L"bad", L"626f6479"}); });
}

static void registration_test() {
    using namespace notifications;
    using Microsoft::WRL::ComPtr;
    check(RoInitialize(RO_INIT_MULTITHREADED), L"Initialize registration test");
    product app{L"NotificationRegistrationTest", L"BigBrother.NotificationRegistrationTest",
        L"{946DEF97-69AA-4F51-B1E1-2EDD849AA378}", L"bigbrother-notification-registration-test", 0};
    ensure_registration(app, true);
    PWSTR programs;
    check(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs), L"Read test Start menu");
    std::wstring path = std::wstring(programs) + L"\\BigBrother NotificationRegistrationTest\\BigBrother NotificationRegistrationTest.lnk";
    CoTaskMemFree(programs);
    ComPtr<IShellLinkW> link;
    check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)), L"Load test shortcut");
    ComPtr<IPersistFile> file;
    check(link.As(&file), L"Read test shortcut file");
    check(file->Load(path.c_str(), STGM_READ), L"Open test shortcut");
    ComPtr<IPropertyStore> properties;
    check(link.As(&properties), L"Read test identity");
    PROPVARIANT value{};
    HRESULT property_result = properties->GetValue(PKEY_AppUserModel_ID, &value);
    bool wine_property_stub = property_result == E_NOTIMPL && GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version");
    if (wine_property_stub) puts("SKIP: Wine does not implement reading shortcut identity properties; verify these on Windows");
    else {
        check(property_result, L"Read test AUMID");
        assert(value.vt == VT_LPWSTR && !wcscmp(value.pwszVal, app.app_id));
    }
    PropVariantClear(&value);
    CLSID expected{};
    check(CLSIDFromString(app.activator, &expected), L"Parse test activator");
    if (!wine_property_stub) {
        check(properties->GetValue(PKEY_AppUserModel_ToastActivatorCLSID, &value), L"Read test toast activator");
        assert(value.vt == VT_CLSID && IsEqualGUID(*value.puuid, expected));
    }
    PropVariantClear(&value);
    wchar_t command[32768];
    DWORD size = sizeof(command);
    assert(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\bigbrother-notification-registration-test\\shell\\open\\command",
        nullptr, RRF_RT_REG_SZ, nullptr, command, &size) == ERROR_SUCCESS);
    assert(command == L"\"" + executable_path() + L"\" --open NotificationRegistrationTest \"%1\"");
    assert(RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\AppUserModelId\\BigBrother.NotificationRegistrationTest") == ERROR_SUCCESS);
    ensure_registration(app);
    size = sizeof(command);
    assert(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\AppUserModelId\\BigBrother.NotificationRegistrationTest",
        L"DisplayName", RRF_RT_REG_SZ, nullptr, command, &size) == ERROR_SUCCESS);
    assert(!wcscmp(command, L"BigBrother NotificationRegistrationTest"));
    size = sizeof(command);
    assert(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\AppUserModelId\\BigBrother.NotificationRegistrationTest",
        L"CustomActivator", RRF_RT_REG_SZ, nullptr, command, &size) == ERROR_SUCCESS);
    assert(!wcscmp(command, app.activator));
    unregister_user(app);
    assert(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES);
    size = sizeof(command);
    assert(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\AppUserModelId\\BigBrother.NotificationRegistrationTest",
        L"DisplayName", RRF_RT_REG_SZ, nullptr, command, &size) == ERROR_FILE_NOT_FOUND);
    RoUninitialize();
}

int main(int argc, char** argv) {
    using namespace notifications;
    if (argc == 3 && !strcmp(argv[1], "--report-child")) {
        std::wstring name;
        for (const char* c = argv[2]; *c; c++) name += static_cast<unsigned char>(*c);
        write_report(name, L"Simulated child failure: доступ запрещён");
        return static_cast<int>(E_ACCESSDENIED);
    }
    if (argc == 3 && !strcmp(argv[1], "--watchdog-child")) {
        std::wstring name;
        for (const char* c = argv[2]; *c; c++) name += static_cast<unsigned char>(*c);
        parent_watch watchdog(name);
        Sleep(60000);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--wait-child")) { Sleep(60000); return 0; }
    process_tests();
    command_line_tests();
    delivery_tests();
    const char* text = "Русский текст: \\\" & <test> 😀";
    auto hex = encode(text, 256);
    assert(decode(hex, 256) == L"Русский текст: \\\" & <test> 😀");
    for (auto bad : {L"", L"a", L"zz", L"00", L"c0af", L"610062"})
        rejects([&] { decode(bad, 256); });
    rejects([] { decode(L"616263", 2); });
    rejects([] { encode("abcd", 3); });
    rejects([] { encode(std::string("a\0b", 3), 256); });
    rejects([] { encode("\xff", 256); });
    rejects([] { toast_xml(L"bigbrother-client:open", L"bad\x01", L"text"); });
    auto xml = toast_xml(L"bigbrother-client:open", L"A & <B>", L"\"quoted\" 'text'");
    assert(xml.find(L"activationType=\"protocol\"") != xml.npos);
    assert(xml.find(L"template=\"ToastGeneric\"") != xml.npos);
    assert(xml.find(L"A &amp; &lt;B&gt;") != xml.npos);
    assert(xml.find(L"&quot;quoted&quot; &apos;text&apos;") != xml.npos);
    assert(std::wstring(find_product(L"Client").app_id) != find_product(L"Server").app_id);
    if (argc == 2 && !strcmp(argv[1], "--registration")) {
        try { registration_test(); }
        catch (const failure& error) {
            fwprintf(stderr, L"FAIL: %ls: 0x%08lX\n", error.operation, static_cast<unsigned long>(error.code));
            return 1;
        }
    }
    puts("PASS: payload validation and simulated delivery (no real notification was sent).");
    if (argc == 2 && !strcmp(argv[1], "--registration"))
        puts("Registration checks completed; toast delivery was not tested.");
    return 0;
}

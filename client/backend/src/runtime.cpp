#include "runtime.h"
#include "resource.h"
#include <charconv>
#include <string>

bool RequestBackendRestart() {
    wchar_t path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length == MAX_PATH) return false;
    std::wstring command = L"\"" + std::wstring(path) + L"\" --restart-service " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child)) return false;
    client::Handle process(child.hProcess), thread(child.hThread);
    return true;
}

int RestartBackendService(const char* process_id) {
    DWORD pid = 0;
    std::string_view text(process_id);
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), pid);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !pid) return 1;
    client::Handle previous(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (previous && WaitForSingleObject(previous.get(), 45000) != WAIT_OBJECT_0) return 1;
    client::ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return 1;
    client::ServiceHandle service(OpenServiceW(manager.get(), L"BigBrother.Client", SERVICE_START | SERVICE_QUERY_STATUS));
    if (!service) return 1;
    SERVICE_STATUS status{};
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!QueryServiceStatus(service.get(), &status)) return 1;
        if (status.dwCurrentState == SERVICE_STOPPED)
            return StartServiceW(service.get(), 0, nullptr) ? 0 : 1;
        Sleep(100);
    }
    return 1;
}

#include <windows.h>
#include "resource.h"
#include "../../../common/backend_lifetime.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static SERVICE_STATUS service_status{};
static SERVICE_STATUS_HANDLE service_handle;
static HANDLE stop_event;

static void WINAPI control(DWORD code) {
    if (code == SERVICE_CONTROL_STOP) SetEvent(stop_event);
}

static DWORD WINAPI serve_firewall(void*) {
    while (WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
        client::Handle pipe(CreateNamedPipeW(L"\\\\.\\pipe\\BigBrother.Firewall", PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr));
        assert(pipe);
        if (!ConnectNamedPipe(pipe.get(), nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) continue;
        char request[4096]{};
        DWORD count;
        if (!ReadFile(pipe.get(), request, sizeof(request) - 1, &count, nullptr)) continue;
        const char* reply = "OK\n";
        if (!strncmp(request, "GET_FILTRATION", 14)) reply = "0\n";
        if (!strncmp(request, "GET_IPV6_BLOCK", 14)) reply = "1\n";
        if (!strncmp(request, "GET_WHITELIST", 13)) reply = "WHITELIST\nexample.com\n";
        WriteFile(pipe.get(), reply, static_cast<DWORD>(strlen(reply)), &count, nullptr);
        FlushFileBuffers(pipe.get());
        DisconnectNamedPipe(pipe.get());
    }
    return 0;
}

static void WINAPI firewall_service(DWORD, LPWSTR*) {
    service_handle = RegisterServiceCtrlHandlerW(L"BigBrother Firewall", control);
    assert(service_handle);
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwCurrentState = SERVICE_RUNNING;
    service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    assert(SetServiceStatus(service_handle, &service_status));
    client::Handle watcher(CreateThread(nullptr, 0, bb_watch_backend, stop_event, 0, nullptr));
    client::Handle listener(CreateThread(nullptr, 0, serve_firewall, nullptr, 0, nullptr));
    WaitForSingleObject(stop_event, INFINITE);
    CancelSynchronousIo(listener.get());
    WaitForSingleObject(listener.get(), 5000);
    WaitForSingleObject(watcher.get(), 5000);
    service_status.dwCurrentState = SERVICE_STOPPED;
    service_status.dwControlsAccepted = 0;
    SetServiceStatus(service_handle, &service_status);
}

static DWORD state(SC_HANDLE service) {
    SERVICE_STATUS status{};
    assert(QueryServiceStatus(service, &status));
    return status.dwCurrentState;
}

static void wait_state(SC_HANDLE service, DWORD expected) {
    auto deadline = GetTickCount64() + 45000;
    while (state(service) != expected && GetTickCount64() < deadline) Sleep(100);
    if (state(service) != expected) {
        fprintf(stderr, "Service state %lu, expected %lu (error %lu)\n", state(service), expected, GetLastError());
        ExitProcess(1);
    }
}

static std::string request(const std::string& command) {
    HANDLE raw = INVALID_HANDLE_VALUE;
    auto deadline = GetTickCount64() + 10000;
    do {
        raw = CreateFileW(L"\\\\.\\pipe\\BigBrother.Client.Backend", GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (raw != INVALID_HANDLE_VALUE) break;
        Sleep(100);
    } while (GetTickCount64() < deadline);
    client::Handle connection(raw);
    assert(connection);
    DWORD count;
    assert(WriteFile(raw, command.data(), static_cast<DWORD>(command.size()), &count, nullptr));
    std::string response;
    char byte;
    while (ReadFile(raw, &byte, 1, &count, nullptr) && count) response += byte;
    return response;
}

static void stop_service(SC_HANDLE service) {
    SERVICE_STATUS status{};
    if (state(service) != SERVICE_STOPPED) assert(ControlService(service, SERVICE_CONTROL_STOP, &status));
    wait_state(service, SERVICE_STOPPED);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--prepare-update") {
        auto approval = request("GET_APPROVED_UPDATE\n\n");
        wchar_t path[MAX_PATH]{};
        assert(GetModuleFileNameW(nullptr, path, MAX_PATH));
        auto directory = std::filesystem::path(path).parent_path();
        { std::ofstream file(directory / "approval.txt"); file << approval; }
        { std::ofstream file(directory / "worker.txt"); file << GetCurrentProcessId(); }
        client::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        bb_watch_backend(stop.get());
        return 0;
    }
    if (argc == 2) {
        SERVICE_TABLE_ENTRYW table[] = {
            {const_cast<wchar_t*>(L"BigBrother Firewall"), firewall_service}, {nullptr, nullptr}
        };
        return StartServiceCtrlDispatcherW(table) ? 0 : 1;
    }
    wchar_t executable[MAX_PATH]{};
    assert(GetModuleFileNameW(nullptr, executable, MAX_PATH));
    auto directory = std::filesystem::path(executable).parent_path();
    std::wstring backend_command = L"\"" + (directory / "BigBrother Client Daemon.exe").wstring() + L"\"";
    std::wstring firewall_command = L"\"" + std::wstring(executable) + L"\" --firewall-service";
    client::ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS));
    assert(manager);
    client::ServiceHandle backend(CreateServiceW(manager.get(), L"BigBrother.Client", L"Backend lifecycle test",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        backend_command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr));
    assert(backend);
    client::ServiceHandle firewall(CreateServiceW(manager.get(), L"BigBrother Firewall", L"Firewall lifecycle test",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        firewall_command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr));
    assert(firewall);
    assert(StartServiceW(backend.get(), 0, nullptr));
    wait_state(backend.get(), SERVICE_RUNNING);
    assert(state(firewall.get()) == SERVICE_STOPPED);
    assert(request("PING\n\n") == "OK\n\n");
    assert(request("SET_FILTRATION\n1\n1\n\n") == "OK\n\n");
    assert(request("GET_STATUS\n\n").starts_with("OK\n"));
    assert(state(firewall.get()) == SERVICE_STOPPED);
    assert(request("START_FIREWALL\n\n") == "OK\n\n");
    wait_state(firewall.get(), SERVICE_RUNNING);
    assert(request("STOP_FIREWALL\n\n") == "OK\n\n");
    wait_state(firewall.get(), SERVICE_STOPPED);
    Sleep(1000);
    assert(state(firewall.get()) == SERVICE_STOPPED);
    assert(request("RESTART_FIREWALL\n\n") == "OK\n\n");
    wait_state(firewall.get(), SERVICE_RUNNING);
    assert(request("GET_APPROVED_UPDATE\n\n").starts_with("ERROR\n"));
    auto approval_file = directory / "GUI/approval.txt";
    auto worker_file = directory / "GUI/worker.txt";
    std::filesystem::remove(approval_file);
    std::filesystem::remove(worker_file);
    const std::string metadata = "{\"version\":\"3.0.0\"}";
    assert(request("INSTALL_CLIENT_UPDATE\n" + std::to_string(metadata.size()) + "\n" + metadata + "\n\n") == "OK\n\n");
    auto deadline = GetTickCount64() + 10000;
    while (!std::filesystem::exists(worker_file) && GetTickCount64() < deadline) Sleep(100);
    assert(std::filesystem::exists(worker_file));
    std::ifstream approval_input(approval_file);
    std::string approval((std::istreambuf_iterator<char>(approval_input)), {});
    assert(approval == "OK\n" + std::to_string(metadata.size()) + "\n" + metadata + "\n\n");
    DWORD worker_pid = 0;
    { std::ifstream file(worker_file); file >> worker_pid; }
    client::Handle worker(OpenProcess(SYNCHRONIZE, FALSE, worker_pid));
    assert(worker && WaitForSingleObject(worker.get(), 0) == WAIT_TIMEOUT);
    assert(request("GET_APPROVED_UPDATE\n\n").starts_with("ERROR\n"));
    stop_service(backend.get());
    wait_state(firewall.get(), SERVICE_STOPPED);
    assert(WaitForSingleObject(worker.get(), 5000) == WAIT_OBJECT_0);
    assert(StartServiceW(backend.get(), 0, nullptr));
    wait_state(backend.get(), SERVICE_RUNNING);
    assert(state(firewall.get()) == SERVICE_STOPPED);
    assert(request("START_FIREWALL\n\n") == "OK\n\n");
    SERVICE_STATUS_PROCESS before_restart{};
    DWORD restart_bytes;
    assert(QueryServiceStatusEx(backend.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&before_restart), sizeof(before_restart), &restart_bytes));
    client::Handle previous(OpenProcess(SYNCHRONIZE, FALSE, before_restart.dwProcessId));
    assert(request("RESTART_CLIENT\n\n") == "OK\n\n");
    assert(WaitForSingleObject(previous.get(), 45000) == WAIT_OBJECT_0);
    Sleep(1000);
    wait_state(backend.get(), SERVICE_RUNNING);
    assert(state(firewall.get()) == SERVICE_STOPPED);
    assert(request("START_FIREWALL\n\n") == "OK\n\n");
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes;
    assert(QueryServiceStatusEx(backend.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &bytes));
    client::Handle process(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, status.dwProcessId));
    assert(process && TerminateProcess(process.get(), 1));
    assert(WaitForSingleObject(process.get(), 10000) == WAIT_OBJECT_0);
    wait_state(firewall.get(), SERVICE_STOPPED);
    wait_state(backend.get(), SERVICE_STOPPED);
    assert(DeleteService(firewall.get()));
    assert(DeleteService(backend.get()));
    puts("PASS: real backend service, explicit controls, no respawn, worker approval/lifetime, backend restart, graceful stop and crash cascade");
}

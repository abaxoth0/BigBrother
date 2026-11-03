#include "process.h"
#include "report.h"
#include <cassert>
#include <cstdio>

namespace {
notifications::handle launch(notifications::process_group& group, std::wstring arguments, notifications::delivery_report* report = nullptr,
                             ULONGLONG deadline = 0, HANDLE parent = GetCurrentProcess()) {
    auto path = notifications::executable_path();
    auto command = L"\"" + path + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    notifications::check_win32(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process), L"Launch process test child");
    if (report) report->supervise(process.hProcess, deadline, parent);
    return group.resume(process);
}
}

void process_tests() {
    using namespace notifications;
    handle token;
    check_win32(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()), L"Read test token");
    delivery_report report(token.get(), false);
    {
        process_group group;
        auto child = launch(group, L"--report-child " + report.name);
        assert(WaitForSingleObject(child.get(), 10000) == WAIT_OBJECT_0);
        DWORD code = 0;
        assert(GetExitCodeProcess(child.get(), &code) && code == static_cast<DWORD>(E_ACCESSDENIED));
        assert(report.read() == L"Simulated child failure: доступ запрещён");
    }
    handle child;
    {
        process_group group;
        auto running = launch(group, L"--wait-child");
        *child.put() = running.get();
        *running.put() = nullptr;
        assert(WaitForSingleObject(child.get(), 100) == WAIT_TIMEOUT);
    }
    assert(WaitForSingleObject(child.get(), 5000) == WAIT_OBJECT_0);
    process_group watcher_group;
    handle watcher;
    {
        process_group parent_group;
        auto parent = launch(parent_group, L"--wait-child");
        auto running = launch(watcher_group, L"--watchdog-child " + report.name, &report, GetTickCount64() + 10000, parent.get());
        *watcher.put() = running.get();
        *running.put() = nullptr;
        assert(WaitForSingleObject(watcher.get(), 100) == WAIT_TIMEOUT);
    }
    assert(WaitForSingleObject(watcher.get(), 5000) == WAIT_OBJECT_0);
    DWORD code = 0;
    assert(GetExitCodeProcess(watcher.get(), &code) && code == ERROR_CANCELLED);
    auto timed = launch(watcher_group, L"--watchdog-child " + report.name, &report, GetTickCount64() + 300);
    assert(WaitForSingleObject(timed.get(), 5000) == WAIT_OBJECT_0);
    assert(GetExitCodeProcess(timed.get(), &code) && code == ERROR_TIMEOUT);
    puts("PASS: child error relay, process-job cleanup, parent death and delivery deadline.");
}

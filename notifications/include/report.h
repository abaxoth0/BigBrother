#pragma once

#include "app.h"
#include <sddl.h>
#include <vector>
#include <cstring>
#include <thread>
#include <algorithm>

namespace notifications {

inline bool report_name(std::wstring_view name) {
    return name.find(L"Global\\BigBrother.ToastReport.") == 0 || name.find(L"Local\\BigBrother.ToastReport.") == 0;
}

struct delivery_result {
    HANDLE parent;
    ULONGLONG deadline;
    char message[4080];
};
static_assert(sizeof(delivery_result) == 4096);

class delivery_report {
    handle mapping_;
    delivery_result* data_ = nullptr;
public:
    std::wstring name;
    explicit delivery_report(HANDLE token, bool global = true) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> buffer(size);
        check_win32(GetTokenInformation(token, TokenUser, buffer.data(), size, &size), L"Read notification recipient identity");
        LPWSTR sid = nullptr;
        check_win32(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid), L"Encode notification recipient identity");
        std::wstring acl = std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid + L")S:(ML;;NW;;;ME)";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        check_win32(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr),
            L"Secure notification result");
        GUID id{};
        HRESULT result = CoCreateGuid(&id);
        wchar_t guid[40]{};
        if (SUCCEEDED(result)) StringFromGUID2(id, guid, ARRAYSIZE(guid));
        name = std::wstring(global ? L"Global\\" : L"Local\\") + L"BigBrother.ToastReport." + guid;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        HANDLE mapping = SUCCEEDED(result) ? CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, 4096, name.c_str()) : nullptr;
        DWORD error = GetLastError();
        LocalFree(descriptor);
        check(result, L"Create notification result identifier");
        check(HRESULT_FROM_WIN32(mapping ? ERROR_SUCCESS : error), L"Create notification result mapping");
        *mapping_.put() = mapping;
        data_ = static_cast<delivery_result*>(MapViewOfFile(mapping_.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 4096));
        check_win32(data_ != nullptr, L"Map notification result");
        std::memset(data_, 0, 4096);
    }
    ~delivery_report() { if (data_) UnmapViewOfFile(data_); }
    delivery_report(const delivery_report&) = delete;
    delivery_report& operator=(const delivery_report&) = delete;
    void supervise(HANDLE child, ULONGLONG deadline, HANDLE parent = GetCurrentProcess()) {
        check_win32(DuplicateHandle(GetCurrentProcess(), parent, child, &data_->parent, SYNCHRONIZE, FALSE, 0),
            L"Give notification child a parent lifetime handle");
        data_->deadline = deadline;
    }
    static std::pair<HANDLE, ULONGLONG> parent(const std::wstring& name) {
        handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
        check_win32(mapping.get() != nullptr, L"Open notification supervision");
        auto data = static_cast<delivery_result*>(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 4096));
        check_win32(data != nullptr, L"Read notification supervision");
        auto result = std::make_pair(data->parent, data->deadline);
        UnmapViewOfFile(data);
        return result;
    }
    std::wstring read() const {
        char snapshot[4080];
        std::memcpy(snapshot, data_->message, sizeof(snapshot));
        snapshot[4079] = 0;
        int length = static_cast<int>(std::strlen(snapshot));
        if (!length) return {};
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, snapshot, length, nullptr, 0);
        if (!count) return L"Invalid notification child diagnostic";
        std::wstring text(count, 0);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, snapshot, length, text.data(), count);
        return text;
    }
};

inline void write_report(const std::wstring& name, std::wstring_view text) {
    if (!report_name(name)) return;
    handle mapping(OpenFileMappingW(FILE_MAP_WRITE, FALSE, name.c_str()));
    if (!mapping.get()) return;
    auto data = static_cast<delivery_result*>(MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, 4096));
    if (!data) return;
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count > 0 && count < 4080) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), data->message, count, nullptr, nullptr);
        data->message[count] = 0;
    }
    UnmapViewOfFile(data);
}

class parent_watch {
    handle stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker_;
public:
    explicit parent_watch(const std::wstring& report) {
        check_win32(stop_.get() != nullptr, L"Create notification watchdog event");
        auto [parent, deadline] = delivery_report::parent(report);
        check_win32(parent != nullptr, L"Read notification parent handle");
        worker_ = std::thread([parent, deadline, stop = stop_.get()] {
            HANDLE events[] = {stop, parent};
            ULONGLONG now = GetTickCount64();
            DWORD remaining = static_cast<DWORD>(deadline > now ? std::min<ULONGLONG>(deadline - now, 30000) : 0);
            DWORD result = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, remaining);
            CloseHandle(parent);
            if (result != WAIT_OBJECT_0) ExitProcess(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_CANCELLED);
        });
    }
    ~parent_watch() { SetEvent(stop_.get()); if (worker_.joinable()) worker_.join(); }
};

}

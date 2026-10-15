#include "app.h"
#include <shlobj.h>

namespace notifications {
namespace {
bool detailed = false;

bool output_available() {
    HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    if (!output || output == INVALID_HANDLE_VALUE) return false;
    DWORD type = GetFileType(output), mode;
    if (type == FILE_TYPE_CHAR) return GetConsoleMode(output, &mode);
    return type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK;
}

void record(std::wstring_view message) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t prefix[96];
    swprintf(prefix, ARRAYSIZE(prefix), L"%04u-%02u-%02u %02u:%02u:%02u.%03u [pid %lu] ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    std::wstring line = prefix + std::wstring(message.substr(0, 4096)) + L"\r\n";
    OutputDebugStringW(line.c_str());
    std::string utf8(WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr), 0);
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
    DWORD written, mode;
    HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    if (GetConsoleMode(output, &mode)) WriteConsoleW(output, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    else if (output_available()) WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local))) return;
    std::wstring folder = std::wstring(local) + L"\\BigBrother";
    CoTaskMemFree(local);
    CreateDirectoryW(folder.c_str(), nullptr);
    auto path = folder + L"\\notifications.log";
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) &&
        (attributes.nFileSizeHigh || attributes.nFileSizeLow >= 65536))
        MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    handle log(CreateFileW(path.c_str(), FILE_APPEND_DATA | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (log.get() == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size;
    if (GetFileSizeEx(log.get(), &size) && size.QuadPart < 65536)
        WriteFile(log.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}
}

bool attach_console_output() {
    if (output_available()) return true;
    return AttachConsole(ATTACH_PARENT_PROCESS) && output_available();
}

void set_diagnostics(bool enabled) { detailed = enabled; }
bool diagnostics_enabled() { return detailed; }
void trace(std::wstring_view message) { if (detailed) record(message); }

DWORD report_failure(const failure& error) {
    wchar_t code[32];
    swprintf(code, ARRAYSIZE(code), L"0x%08lX", static_cast<unsigned long>(error.code));
    record(std::wstring(error.operation) + L" failed: " + code);
    return static_cast<DWORD>(error.code);
}
}

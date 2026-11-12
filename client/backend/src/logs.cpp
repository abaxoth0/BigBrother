#include "logs.h"
#include "resource.h"
#include <charconv>

namespace client {

std::optional<Reply> LogsModule::dispatch(std::string_view command, Arguments args, HANDLE) {
    if (command != "READ_LOG") return std::nullopt;
    if (args.size() != 3 || (args[0] != "client" && args[0] != "firewall")) return Reply::error("expected log source, offset and identity");
    long long offset = 0;
    auto parsed = std::from_chars(args[1].data(), args[1].data() + args[1].size(), offset);
    if (parsed.ec != std::errc{} || parsed.ptr != args[1].data() + args[1].size() || offset < -1) return Reply::error("invalid offset");
    wchar_t executable[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length == MAX_PATH) return Reply::error("could not locate installation");
    std::wstring path(executable);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += args[0] == "client" ? L"logs\\client.binlog" : L"logs\\firewall.binlog";
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
            return Reply{true, {"", "0", "0", ""}};
        return Reply::error("could not read log");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return Reply::error("invalid log file");
    std::string identity = std::to_string(info.dwVolumeSerialNumber) + ":" + std::to_string(info.nFileIndexHigh) + ":" +
        std::to_string(info.nFileIndexLow) + ":" + std::to_string(info.ftCreationTime.dwHighDateTime) + ":" + std::to_string(info.ftCreationTime.dwLowDateTime);
    LARGE_INTEGER size{}, position{};
    if (!GetFileSizeEx(file.get(), &size)) return Reply::error("could not size log");
    if (offset == -1) offset = size.QuadPart > 512 * 1024 ? size.QuadPart - 512 * 1024 : 0;
    else if ((!args[2].empty() && args[2] != identity) || offset > size.QuadPart) offset = 0;
    position.QuadPart = offset;
    if (!SetFilePointerEx(file.get(), position, nullptr, FILE_BEGIN)) return Reply::error("could not seek log");
    std::string bytes(64 * 1024, '\0');
    DWORD count = 0;
    if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) return Reply::error("could not read log");
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex(count * 2, '\0');
    for (DWORD i = 0; i < count; ++i) {
        auto byte = static_cast<unsigned char>(bytes[i]);
        hex[i * 2] = digits[byte >> 4];
        hex[i * 2 + 1] = digits[byte & 15];
    }
    return Reply{true, {identity, std::to_string(offset + count), std::to_string(size.QuadPart), std::move(hex)}};
}

}

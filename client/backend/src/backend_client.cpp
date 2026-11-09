#include "backend_client.h"
#include "resource.h"
#include <charconv>

namespace client {

Reply SendBackendCommand(std::string_view command, Arguments arguments) {
    Handle pipe(CreateFileW(L"\\\\.\\pipe\\BigBrother.Client.Backend", GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!pipe) return Reply::error("client backend is unavailable");
    std::string request(command);
    request += '\n';
    for (auto argument : arguments) {
        request += std::to_string(argument.size()) + '\n';
        request += argument;
        request += '\n';
    }
    request += '\n';
    DWORD written;
    if (!WriteFile(pipe.get(), request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
        written != request.size()) return Reply::error("failed to send backend command");
    std::string response;
    char buffer[4096];
    DWORD read;
    while (ReadFile(pipe.get(), buffer, sizeof(buffer), &read, nullptr) && read) {
        if (response.size() + read > 32 * 1024 * 1024) return Reply::error("backend response is too large");
        response.append(buffer, read);
    }
    size_t position = response.find('\n');
    if (position == std::string::npos) return Reply::error("invalid backend response");
    auto status = response.substr(0, position++);
    if (status != "OK" && status != "ERROR") return Reply::error("invalid backend status");
    Reply reply{status == "OK", {}};
    while (position < response.size()) {
        size_t end = response.find('\n', position);
        if (end == position) return reply;
        if (end == std::string::npos) break;
        size_t length = 0;
        auto result = std::from_chars(response.data() + position, response.data() + end, length);
        position = end + 1;
        if (result.ec != std::errc{} || result.ptr != response.data() + end ||
            length >= response.size() - position || response[position + length] != '\n') break;
        reply.values.push_back(response.substr(position, length));
        position += length + 1;
    }
    return Reply::error("truncated backend response");
}

}

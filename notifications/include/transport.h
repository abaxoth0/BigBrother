#pragma once

#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace notifications {

inline std::string prepare_text(std::string_view text, size_t limit, std::string_view fallback) {
    auto sample = text.substr(0, std::min(text.size(), limit + 4));
    int count = MultiByteToWideChar(CP_UTF8, 0, sample.data(), static_cast<int>(sample.size()), nullptr, 0);
    std::wstring wide(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, sample.data(), static_cast<int>(sample.size()), wide.data(), count);
    for (auto& c : wide) {
        if (c < 32 && c != L'\t' && c != L'\n' && c != L'\r') c = L' ';
        if (c == 0xfffe || c == 0xffff) c = 0xfffd;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, nullptr, 0, nullptr, nullptr);
    std::string result(bytes, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, result.data(), bytes, nullptr, nullptr);
    if (result.find_first_not_of(" \t\r\n") == result.npos) result = fallback;
    if (result.size() > limit || sample.size() < text.size()) {
        size_t end = std::min(result.size(), limit >= 3 ? limit - 3 : limit);
        while (end && end < result.size() && (static_cast<unsigned char>(result[end]) & 0xc0) == 0x80) end--;
        result.resize(end);
        if (limit >= 3) result += "\xe2\x80\xa6";
    }
    return result;
}

inline std::wstring encode(std::string_view text, size_t limit) {
    if (text.empty() || text.size() > limit || text.find('\0') != text.npos ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0))
        throw std::invalid_argument("Invalid notification text");
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring hex;
    hex.reserve(text.size() * 2);
    for (unsigned char byte : text) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    return hex;
}

inline std::wstring decode(std::wstring_view hex, size_t limit) {
    if (hex.empty() || hex.size() % 2 || hex.size() / 2 > limit)
        throw std::invalid_argument("Invalid notification payload size");
    auto digit = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        throw std::invalid_argument("Invalid notification payload");
    };
    std::string bytes;
    for (size_t i = 0; i < hex.size(); i += 2) {
        char byte = static_cast<char>((digit(hex[i]) << 4) | digit(hex[i + 1]));
        if (!byte) throw std::invalid_argument("Embedded NUL in notification");
        bytes += byte;
    }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!count) throw std::invalid_argument("Invalid UTF-8 in notification");
    std::wstring text(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), count);
    return text;
}

inline std::wstring xml_escape(std::wstring_view text) {
    std::wstring escaped;
    for (wchar_t c : text) {
        if (c < 32 && c != L'\t' && c != L'\n' && c != L'\r')
            throw std::invalid_argument("Invalid XML character in notification");
        switch (c) {
            case L'&': escaped += L"&amp;"; break;
            case L'<': escaped += L"&lt;"; break;
            case L'>': escaped += L"&gt;"; break;
            case L'\"': escaped += L"&quot;"; break;
            case L'\'': escaped += L"&apos;"; break;
            default: escaped += c;
        }
    }
    return escaped;
}

inline std::wstring toast_xml(std::wstring_view protocol, std::wstring_view title, std::wstring_view body) {
    return L"<toast activationType=\"protocol\" launch=\"" + xml_escape(protocol) +
        L"\"><visual><binding template=\"ToastGeneric\"><text>" + xml_escape(title) +
        L"</text><text>" + xml_escape(body) + L"</text></binding></visual></toast>";
}

}

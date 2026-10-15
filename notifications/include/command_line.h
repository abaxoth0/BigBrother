#pragma once

#include "app.h"
#include "transport.h"

namespace notifications {

enum class command_kind { test, send, show, open, register_user, unregister_user, help, version };

struct command {
    command_kind kind;
    product app = find_product(L"Client");
    std::wstring title{};
    std::wstring body{};
};

inline command parse_command(int argc, wchar_t** args) {
    if (argc == 1) return {command_kind::test};
    std::wstring_view mode(args[1]);
    if (argc == 2 && (mode == L"--help" || mode == L"-h")) return {command_kind::help};
    if (argc == 2 && mode == L"--version") return {command_kind::version};
    if ((mode == L"--test" || mode == L"--show") && argc <= 3)
        return {command_kind::test, argc == 3 ? find_product(args[2]) : find_product(L"Client")};
    if ((mode == L"--send" || mode == L"--show") && argc == 5) {
        decode(args[3], 256);
        decode(args[4], 1024);
        return {mode == L"--send" ? command_kind::send : command_kind::show, find_product(args[2]), args[3], args[4]};
    }
    if (argc == 3 && mode == L"--register") return {command_kind::register_user, find_product(args[2])};
    if (argc == 3 && mode == L"--unregister") return {command_kind::unregister_user, find_product(args[2])};
    if ((argc == 3 || argc == 4) && mode == L"--open") {
        auto app = find_product(args[2]);
        if (argc == 3 || args[3] == app.uri()) return {command_kind::open, app};
    }
    throw failure{E_INVALIDARG, L"Invalid notification command; use --help"};
}

inline bool manual_test_command(int argc, wchar_t** args) {
    return argc == 1 || (argc >= 2 && argc <= 3 &&
        (std::wstring_view(args[1]) == L"--show" || std::wstring_view(args[1]) == L"--test"));
}

}

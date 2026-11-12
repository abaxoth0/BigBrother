/**
 * @file ipc_client.cpp
 * @brief Named pipe server for Client Frontend & Backend communication.
 */

#include "../include/ipc_client.h"
#include "../include/ipc_daemon.h"
#include "../include/net_client.h"
#include "../../../common/pipe_security.h"
#include "../../../common/log/log.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "hub.h"
#include "resource.h"
#include "runtime.h"
#include <algorithm>
#include <charconv>

#define CLIENT_PIPE_NAME "\\\\.\\pipe\\BigBrother.Client.Backend"
#define CLIENT_PIPE_BUFFER_SIZE 4096

#define MAX_EVENT_SUBSCRIBERS 8

static HANDLE g_shutdown_event = NULL;
static HANDLE g_listener = NULL;
static std::vector<HANDLE> g_handlers;

static bool client_server_stopping() {
    return g_shutdown_event && WaitForSingleObject(g_shutdown_event, 0) == WAIT_OBJECT_0;
}

static bool write_pipe(HANDLE pipe, const char* data, DWORD length) {
    if (client_server_stopping()) return false;
    DWORD written;
    return WriteFile(pipe, data, length, &written, nullptr) && written == length;
}

// State change notification. SRWLOCK is statically initializable, so
// NotifyStateChanged can be called from DaemonRun before client_server_thread
// runs (which would be a race with a runtime-initialized CRITICAL_SECTION).
static volatile unsigned int g_state_generation = 0;
static HANDLE g_sub_events[MAX_EVENT_SUBSCRIBERS] = {NULL};
static SRWLOCK g_sub_lock = SRWLOCK_INIT;

void SignalClientServerShutdown(void) {
    if (g_shutdown_event) {
        SetEvent(g_shutdown_event);
    }

    // Connect to our own pipe to wake up ConnectNamedPipe so the listener
    // thread can exit on service stop even when no frontend is connected.
    HANDLE wake = CreateFile(CLIENT_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
    if (wake != INVALID_HANDLE_VALUE) {
        CloseHandle(wake);
    }
}

void NotifyStateChanged(void) {
    InterlockedIncrement((volatile LONG*)&g_state_generation);
    AcquireSRWLockExclusive(&g_sub_lock);
    for (int i = 0; i < MAX_EVENT_SUBSCRIBERS; i++) {
        if (g_sub_events[i]) {
            SetEvent(g_sub_events[i]);
        }
    }
    ReleaseSRWLockExclusive(&g_sub_lock);
}

// Write response: status\n[TLV data...\n]<empty line>
// Assembles into a fixed buffer when it fits; otherwise streams field-by-field
// so large responses (e.g. a full whitelist) are never truncated.
static void write_response_tlv(HANDLE pipe, const char* status, const char** data, size_t data_count) {
    if (client_server_stopping()) return;

    // Compute total size first.
    size_t total = strlen(status) + 2; // status + \n + trailing \n
    for (size_t i = 0; i < data_count; i++) {
        if (data[i]) {
            size_t vlen = strlen(data[i]);
            total += 1 + 32; // \n + len line (digits + \n) — worst case
            total += vlen + 1;
        }
    }
    if (total <= 64 * 1024) {
        // Single-buffer path (common, small responses).
        char buffer[64 * 1024];
        size_t pos = 0;
        int n = snprintf(buffer + pos, sizeof(buffer) - pos, "%s\n", status);
        if (n > 0) pos += (size_t)n;
        for (size_t i = 0; i < data_count; i++) {
            if (data[i] && pos < sizeof(buffer)) {
                int len = snprintf(buffer + pos, sizeof(buffer) - pos, "%zu\n", strlen(data[i]));
                if (len > 0) pos += (size_t)len;
                if (pos < sizeof(buffer)) {
                    size_t vlen = strlen(data[i]);
                    if (vlen > sizeof(buffer) - pos) vlen = sizeof(buffer) - pos - 1;
                    memcpy(buffer + pos, data[i], vlen);
                    pos += vlen;
                }
                if (pos < sizeof(buffer)) {
                    buffer[pos++] = '\n';
                }
            }
        }
        if (pos < sizeof(buffer)) {
            buffer[pos++] = '\n';
        }
        if (write_pipe(pipe, buffer, (DWORD)pos) && !client_server_stopping()) FlushFileBuffers(pipe);
        return;
    }

    // Streaming path: large payloads (whitelist relays). Write per field.
    if (!write_pipe(pipe, status, (DWORD)strlen(status)) || !write_pipe(pipe, "\n", 1)) return;
    for (size_t i = 0; i < data_count; i++) {
        if (!data[i]) continue;
        char len_buf[32];
        int len = snprintf(len_buf, sizeof(len_buf), "%zu\n", strlen(data[i]));
        if (!write_pipe(pipe, len_buf, (DWORD)len) ||
            !write_pipe(pipe, data[i], (DWORD)strlen(data[i])) || !write_pipe(pipe, "\n", 1)) return;
    }
    if (write_pipe(pipe, "\n", 1) && !client_server_stopping()) FlushFileBuffers(pipe);
}

static void write_error_tlv(HANDLE pipe, const char* error) {
    if (!error) error = "Unknown error";
    const char* data[1] = {error};
    write_response_tlv(pipe, "ERROR", data, 1);
}

static void write_ok(HANDLE pipe) {
    write_response_tlv(pipe, "OK", NULL, 0);
}

// Buffered byte reader over a pipe: reads in chunks instead of one byte per
// ReadFile() call.
typedef struct {
    HANDLE pipe;
    char buf[4096];
    size_t pos;
    size_t len;
} PipeByteReader;

static void pipe_reader_init(PipeByteReader* r, HANDLE pipe) {
    r->pipe = pipe;
    r->pos = 0;
    r->len = 0;
}

static int pipe_reader_fill(PipeByteReader* r) {
    if (client_server_stopping()) return 0;
    r->pos = 0;
    r->len = 0;
    DWORD bytes_read;
    if (!ReadFile(r->pipe, r->buf, sizeof(r->buf), &bytes_read, NULL) || bytes_read == 0) {
        return 0;
    }
    r->len = (size_t)bytes_read;
    return 1;
}

static int pipe_reader_getc(PipeByteReader* r, char* out) {
    if (r->pos >= r->len) {
        if (!pipe_reader_fill(r)) return 0;
    }
    *out = r->buf[r->pos++];
    return 1;
}

static int pipe_reader_readline(PipeByteReader* r, char* out, size_t out_size) {
    size_t used = 0;
    for (;;) {
        char c;
        if (!pipe_reader_getc(r, &c)) return 0;
        if (c == '\n') {
            out[used] = '\0';
            return 1;
        }
        if (c == '\r') continue;
        if (used + 1 >= out_size) return 0;
        out[used++] = c;
    }
}

static int pipe_reader_read(PipeByteReader* r, char* out, size_t n) {
    size_t got = 0;
    while (got < n) {
        if (r->pos >= r->len) {
            if (!pipe_reader_fill(r)) return 0;
        }
        size_t avail = r->len - r->pos;
        size_t want = n - got;
        size_t copy = avail < want ? avail : want;
        memcpy(out + got, r->buf + r->pos, copy);
        r->pos += copy;
        got += copy;
    }
    return 1;
}

static int read_tlv_request(HANDLE pipe, char* command, size_t capacity,
                            std::vector<std::string>& arguments) {
    PipeByteReader reader;
    pipe_reader_init(&reader, pipe);
    if (!pipe_reader_readline(&reader, command, capacity) || !command[0]) return -1;
    size_t total = 0;
    for (;;) {
        char line[32]{};
        if (!pipe_reader_readline(&reader, line, sizeof(line))) return -1;
        if (!line[0]) return static_cast<int>(arguments.size());
        size_t length = 0;
        auto end = line + strlen(line);
        auto parsed = std::from_chars(line, end, length);
        if (parsed.ec != std::errc{} || parsed.ptr != end ||
            length > DAEMON_MAX_MESSAGE_SIZE - total || arguments.size() >= 64) return -1;
        std::string value(length, '\0');
        if (!pipe_reader_read(&reader, value.data(), length) || value.find('\0') != std::string::npos) return -1;
        char separator;
        if (!pipe_reader_getc(&reader, &separator)) return -1;
        if (separator == '\r' && !pipe_reader_getc(&reader, &separator)) return -1;
        if (separator != '\n') return -1;
        total += length;
        arguments.push_back(std::move(value));
    }
}

// GUI executable ("BigBrother Client.exe"). Compare by file name (case
// insensitive) so the GUI can run from any directory during development.
static int peer_is_client_gui(HANDLE pipe) {
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid) || pid == 0) return 0;

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return 0;

    wchar_t path[MAX_PATH];
    DWORD sz = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &sz);
    CloseHandle(proc);
    if (!ok) return 0;

    // Extract the file name component.
    size_t len = wcslen(path);
    size_t cut = len;
    while (cut > 0 && path[cut - 1] != L'\\' && path[cut - 1] != L'/') cut--;
    const wchar_t* name = path + cut;
    return _wcsicmp(name, L"BigBrother Client.exe") == 0 ||
        _wcsicmp(name, L"BigBrother Client Daemon.exe") == 0;
}

static DWORD handle_client(LPVOID param) {
    HANDLE pipe = (HANDLE)param;
    client::Handle connection(pipe);
    char buffer[CLIENT_PIPE_BUFFER_SIZE];

    // Reject unknown peer processes (defense-in-depth on top of the DACL).
    if (!peer_is_client_gui(pipe)) {
        return 1;
    }

    // Read TLV request
    std::vector<std::string> arguments;
    int arg_count = read_tlv_request(pipe, buffer, sizeof(buffer), arguments);
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    char** args = pointers.data();

    if (arg_count < 0) {
        write_error_tlv(pipe, "failed to parse request");
        return 1;
    }

    if (g_ServiceStopEvent != INVALID_HANDLE_VALUE &&
        WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) return 0;

    std::vector<std::string_view> values;
    for (int i = 0; i < arg_count; ++i) values.emplace_back(args[i]);
    auto reply = client::hub().dispatch(buffer, values, pipe);
    if (reply) {
        std::vector<const char*> fields;
        for (const auto& value : reply->values) fields.push_back(value.c_str());
        write_response_tlv(pipe, reply->ok ? "OK" : "ERROR", fields.data(), fields.size());
    } else if (strcmp(buffer, "GET_CLIENT_UPDATE") == 0) {
        size_t capacity = 128 * 1024;
        char* response = static_cast<char*>(calloc(1, capacity));
        if (!response || arg_count != 0 || ServerGetUpdate(response, capacity) != 0) {
            write_error_tlv(pipe, "could not fetch client update");
        } else if (!response[0]) {
            write_ok(pipe);
        } else {
            const char* data[] = {response};
            write_response_tlv(pipe, "OK", data, 1);
        }
        free(response);
    } else if (strcmp(buffer, "GET_STATUS") == 0) {
        int daemon_ok = (PingDaemon() == 0);
        int server_ok = IsServerSessionActive();

        char client_name[128] = {};
        LoadUserName(client_name, sizeof(client_name));

        // data[0] = ClientName, data[1] = IpAddress, data[2] = DaemonStatus
        // data[3] = Backend status ("running"), data[4] = WhitelistRevision
        // data[5] = PID, data[6] = ServerRunning, data[7] = ServerSession
        // data[8] = FiltrationEnabled
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%lu", GetCurrentProcessId());

        char rev_str[32];
        snprintf(rev_str, sizeof(rev_str), "%u", g_whitelist_revision);

        char filt_str[8];
        int filt_val = RefreshFiltrationEffective();
        snprintf(filt_str, sizeof(filt_str), "%d", filt_val);
        LOGF("[IPC] GET_STATUS: filtration=%d (effective)", filt_val);

        const char* data[9] = {
            client_name[0] ? client_name : "unknown",
            "127.0.0.1",
            daemon_ok ? "running" : "not_running",
            "running",
            rev_str,
            pid_str,
            server_ok ? "running" : "not_running",
            IsServerSessionActive() ? "connected" : "not_connected",
            filt_str
        };
        write_response_tlv(pipe, "OK", data, 9);

    } else if (strcmp(buffer, "RESTART_CLIENT") == 0) {
        if (!RequestBackendRestart()) write_error_tlv(pipe, "failed to schedule backend restart");
        else {
            write_ok(pipe);
            SetEvent(g_ServiceStopEvent);
        }

    } else if (strcmp(buffer, "PING") == 0) {
        write_ok(pipe);

    } else if (strcmp(buffer, "GET_LOG_PATH") == 0) {
        char exe_path[MAX_PATH];
        char* name = exe_path + GetModuleFileName(NULL, exe_path, MAX_PATH);
        while (name > exe_path && *(name - 1) != '\\') name--;
        *name = '\0';

        char client_log[MAX_PATH];
        snprintf(client_log, sizeof(client_log), "%s\\logs\\client.binlog", exe_path);

        char firewall_log[MAX_PATH];
        snprintf(firewall_log, sizeof(firewall_log), "%s\\logs\\firewall.binlog", exe_path);

        const char* data[2] = {client_log, firewall_log};
        write_response_tlv(pipe, "OK", data, 2);

    } else if (strcmp(buffer, "REGISTER") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing username");
        } else if (ServerRegister(args[0]) != 0) {
            write_error_tlv(pipe, "failed to register");
        } else {
            SaveUserName(args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "CONNECT") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0) {
            write_error_tlv(pipe, "no saved username");
        } else {
            ResumeServerConnection();
            int r = ServerConnect(username);
            if (r == 0) {
                write_ok(pipe);
            } else {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "connect failed: server error (or see log)");
                write_error_tlv(pipe, err_msg);
            }
        }

    } else if (strcmp(buffer, "DISCONNECT") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0) {
            write_error_tlv(pipe, "no saved username");
        } else if (ServerDisconnect(username) != 0) {
            write_error_tlv(pipe, "failed to disconnect");
        } else {
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "REFRESH") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0) {
            write_error_tlv(pipe, "not connected");
        } else if (ServerRefresh(username) != 0) {
            write_error_tlv(pipe, "failed to refresh");
        } else {
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "SET_SERVER_ADDR") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing server address");
        } else {
            const char* new_ip = args[0];
            // If this is the local machine's IP, use "." (localhost TCP)
            if (strcmp(new_ip, "127.0.0.1") != 0 && strcmp(new_ip, ".") != 0) {
                char local_ip[64] = {};
                GetLocalIp(new_ip, local_ip, sizeof(local_ip));
                if (local_ip[0] && strcmp(new_ip, local_ip) == 0) {
                    SetServerIp(".");
                    write_ok(pipe);
                    return 0;
                }
            }
            SetServerIp(args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_SERVER_ADDR") == 0) {
        const char* addr = GetServerIp();
        const char* data[1] = {addr ? addr : ""};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_USERNAME") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing username");
        } else {
            SaveUserName(args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_USERNAME") == 0) {
        char buf[128] = {};
        const char* data[1] = {buf};
        if (LoadUserName(buf, sizeof(buf)) == 0) {
            write_response_tlv(pipe, "OK", data, 1);
        } else {
            write_response_tlv(pipe, "OK", data, 1); // empty string = not set
        }

    } else if (strcmp(buffer, "SET_TOKEN") == 0) {
        if (arg_count < 1 || !args[0]) {
            write_error_tlv(pipe, "missing token");
        } else {
            ini_set_string("client", "token", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_TOKEN") == 0) {
        char buf[256] = {};
        ini_get_string("client", "token", buf, sizeof(buf));
        const char* data[1] = {buf};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "GET_WINDOWS_NOTIFICATIONS") == 0) {
        const char* data[] = {IsWindowsNotificationsEnabled() ? "1" : "0"};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_WINDOWS_NOTIFICATIONS") == 0) {
        if (arg_count != 1 || !args[0] ||
            (strcmp(args[0], "0") != 0 && strcmp(args[0], "1") != 0)) {
            write_error_tlv(pipe, "expected value 0 or 1");
        } else if (SetWindowsNotificationsEnabled(args[0][0] == '1') != 0) {
            write_error_tlv(pipe, "failed to save Windows notification preference");
        } else {
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "DISCOVER_SERVERS") == 0) {
        int timeout_ms = 2000;
        if (arg_count >= 1 && args[0]) {
            timeout_ms = atoi(args[0]);
            if (timeout_ms <= 0) timeout_ms = 2000;
        }

        char local_ip[64] = {};
        char bcast_list[4096] = {};

        GetAllBroadcastAddresses(local_ip, sizeof(local_ip), bcast_list, sizeof(bcast_list));
        {
            char response[8192] = {};
            int count = DiscoverServers(bcast_list, 42069, timeout_ms, response, sizeof(response));
            if (count == 0) {
                LOGF("[Discovery] UDP returned 0, trying TCP subnet scan...");
                count = DiscoverServersTCP(response, sizeof(response), timeout_ms);
            }

            if (count > 0) {
                LOGF("[Discovery] local_ip='%s', first result: %s", local_ip, response);
                // Replace server IP with "." if it matches local machine IP (use local loopback TCP)
                if (local_ip[0] != '\0') {
                    char* line = response;
                    while (line && *line) {
                        char* pipe_c = strchr(line, '|');
                        if (pipe_c) {
                            char* ip = pipe_c + 1;
                            char* nl = strchr(ip, '\n');
                            if (nl) *nl = '\0';
                            LOGF("[Discovery] Checking IP='%s' vs local_ip='%s'", ip, local_ip);
                            if (strcmp(ip, local_ip) == 0) {
                                LOGF("[Discovery] Replacing %s with '.'", ip);
                                // Replace IP with "." for localhost TCP
                                memmove(pipe_c + 2, pipe_c + strlen(pipe_c + 1) + 1,
                                    strlen(pipe_c + 1) + 1);
                                pipe_c[1] = '.';
                            }
                            if (nl) *nl = '\n';
                        }
                        char* next = strchr(line, '\n');
                        line = next ? next + 1 : NULL;
                    }
                }

                // Parse "name|ip|port\n..." lines into TLV values
                char* lines[DISCOVERY_MAX_SERVERS];
                int line_count = 0;
                char* p = response;
                while (p && *p && line_count < DISCOVERY_MAX_SERVERS) {
                    lines[line_count++] = p;
                    char* nl = strchr(p, '\n');
                    if (nl) {
                        *nl = '\0';
                        p = nl + 1;
                    } else {
                        break;
                    }
                }
                write_response_tlv(pipe, "OK", (const char**)lines, line_count);
            } else {
                write_error_tlv(pipe, "no servers found");
            }
        }

    } else if (strcmp(buffer, "SET_SERVER_PORT") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing port number");
        } else {
            ini_set_string("server", "port", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_SERVER_PORT") == 0) {
        char buf[16] = {};
        int port = get_server_port();
        snprintf(buf, sizeof(buf), "%d", port);
        const char* data[1] = {buf};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_DISCOVERY_ENABLED") == 0) {
        if (arg_count < 1 || !args[0]) {
            write_error_tlv(pipe, "missing value (0 or 1)");
        } else {
            ini_set_string("discovery", "enabled", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_DISCOVERY_ENABLED") == 0) {
        const char* val = is_discovery_enabled() ? "1" : "0";
        const char* data[1] = {val};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_NETWORK_AUTO") == 0) {
        if (arg_count < 1 || !args[0]) {
            write_error_tlv(pipe, "missing value (0 or 1)");
        } else {
            ini_set_string("network", "auto", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_NETWORK_AUTO") == 0) {
        char buf[8] = {};
        int auto_val = 1;
        if (ini_get_string("network", "auto", buf, sizeof(buf)) && buf[0])
            auto_val = (buf[0] == '1' || buf[0] == 't' || buf[0] == 'y');
        const char* data[1] = {auto_val ? "1" : "0"};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_NETWORK_GATEWAY") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing gateway address");
        } else {
            ini_set_string("network", "gateway", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_NETWORK_GATEWAY") == 0) {
        char buf[64] = {};
        ini_get_string("network", "gateway", buf, sizeof(buf));
        const char* data[1] = {buf};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_NETWORK_MASK") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing subnet mask");
        } else {
            ini_set_string("network", "mask", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_NETWORK_MASK") == 0) {
        char buf[64] = {};
        ini_get_string("network", "mask", buf, sizeof(buf));
        const char* data[1] = {buf};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "GET_SAVED_SERVER_FINGERPRINT") == 0) {
        char pinned[65] = {};
        ini_get_string("server", "fingerprint", pinned, sizeof(pinned));
        const char* data[] = {pinned};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "INSPECT_SERVER_CERTIFICATE") == 0) {
        char address[64], pinned[65], observed[65], port_value[16];
        int port;
        if (InspectServerCertificate(address, &port, pinned, observed) != 0) {
            write_error_tlv(pipe, "failed to inspect server certificate; saved trust is unchanged");
        } else {
            snprintf(port_value, sizeof(port_value), "%d", port);
            const char* data[] = {address, port_value, pinned, observed};
            write_response_tlv(pipe, "OK", data, 4);
        }

    } else if (strcmp(buffer, "UPDATE_SERVER_FINGERPRINT") == 0) {
        if (arg_count != 4 || !args[0] || !args[1] || !args[2] || !args[3]) {
            write_error_tlv(pipe, "missing endpoint, original pin or verified fingerprint");
        } else {
            char* end;
            long port = strtol(args[1], &end, 10);
            if (*end || end == args[1] || port < 1 || port > 65535 ||
                UpdateServerFingerprint(args[0], (int)port, args[2], args[3]) != 0) {
                write_error_tlv(pipe, "certificate or settings changed, verification failed, or pin could not be saved; inspect again");
            } else {
                write_ok(pipe);
            }
        }

    } else if (strcmp(buffer, "SET_SERVER_NAME") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing server name");
        } else {
            ini_set_string("server", "name", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_SERVER_NAME") == 0) {
        char buf[128] = {};
        if (ini_get_string("server", "name", buf, sizeof(buf)) && buf[0]) {
            const char* data[1] = {buf};
            write_response_tlv(pipe, "OK", data, 1);
        } else {
            write_error_tlv(pipe, "no server name set");
        }

    } else if (strcmp(buffer, "CHANGE_NAME") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing new name");
        } else {
            char old_name[128];
            if (LoadUserName(old_name, sizeof(old_name))) {
                write_error_tlv(pipe, "no saved username");
            } else if (ServerChangeName(old_name, args[0]) != 0) {
                write_error_tlv(pipe, "failed to change name");
            } else {
                SaveUserName(args[0]);
                write_ok(pipe);
            }
        }

    } else if (strcmp(buffer, "SUBSCRIBE") == 0) {
        HANDLE sub_event = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!sub_event) {
            write_error_tlv(pipe, "failed to create subscriber event");
        } else {
            write_ok(pipe);

            AcquireSRWLockExclusive(&g_sub_lock);
            int slot = -1;
            for (int i = 0; i < MAX_EVENT_SUBSCRIBERS; i++) {
                if (!g_sub_events[i]) {
                    g_sub_events[i] = sub_event;
                    slot = i;
                    break;
                }
            }
            ReleaseSRWLockExclusive(&g_sub_lock);

            if (slot < 0) {
                CloseHandle(sub_event);
                write_error_tlv(pipe, "too many subscribers");
            } else {
                unsigned int last_generation = g_state_generation;
                int keep_going = 1;
                while (keep_going) {
                    HANDLE waits[2] = {sub_event, g_shutdown_event};
                    DWORD w = WaitForMultipleObjects(2, waits, FALSE, 1000);
                    if (w == WAIT_OBJECT_0) {
                        if (g_state_generation != last_generation) {
                            last_generation = g_state_generation;
                            ResetEvent(sub_event);
                            const char* payload = "EVENT\n13\nSTATE_CHANGED\n\n";
                            if (!write_pipe(pipe, payload, (DWORD)strlen(payload))) {
                                keep_going = 0;
                            }
                        }
                    } else if (w == WAIT_OBJECT_0 + 1) {
                        keep_going = 0;
                    }
                    // Detect client disconnect
                    DWORD bytes_avail = 0;
                    if (keep_going &&
                        (!PeekNamedPipe(pipe, NULL, 0, NULL, &bytes_avail, NULL) &&
                         GetLastError() == ERROR_BROKEN_PIPE)) {
                        keep_going = 0;
                    }
                }

                AcquireSRWLockExclusive(&g_sub_lock);
                if (g_sub_events[slot] == sub_event) g_sub_events[slot] = NULL;
                ReleaseSRWLockExclusive(&g_sub_lock);
                CloseHandle(sub_event);
            }
        }
        return 0;

    } else {
        write_error_tlv(pipe, "unknown command");
    }


    return 0;
}

DWORD WINAPI client_handler(LPVOID param) {
    try { return handle_client(param); }
    catch (...) { return 1; }
}

DWORD WINAPI client_server_thread(LPVOID param) {
    (void)param;


    // Restrict the pipe to SYSTEM + Administrators + Owner so local processes
    // can't drive the client daemon through its frontend pipe.
    SECURITY_ATTRIBUTES sec_attrs;
    int have_sec = (bb_restrict_pipe_security(&sec_attrs) == 0);
    if (!have_sec) return 1;

    while (1) {
        HANDLE pipe = CreateNamedPipe(
            CLIENT_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            CLIENT_PIPE_BUFFER_SIZE,
            CLIENT_PIPE_BUFFER_SIZE,
            0,
            have_sec ? &sec_attrs : NULL
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        // Check for shutdown before waiting for connection
        if (WaitForSingleObject(g_shutdown_event, 0) == WAIT_OBJECT_0) {
            CloseHandle(pipe);
            break;
        }

        // Use timeout-based approach to allow shutdown
        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            // Check again for shutdown
            if (WaitForSingleObject(g_shutdown_event, 0) == WAIT_OBJECT_0) {
                CloseHandle(pipe);
                break;
            }
            HANDLE thread = CreateThread(NULL, 0, client_handler, pipe, 0, NULL);
            if (thread) g_handlers.push_back(thread);
            else CloseHandle(pipe);
            std::erase_if(g_handlers, [](HANDLE worker) {
                if (WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) return false;
                CloseHandle(worker);
                return true;
            });
        } else {
            CloseHandle(pipe);
        }

        // Small sleep to prevent CPU spinning
        Sleep(100);
    }

    if (have_sec) {
        LocalFree(sec_attrs.lpSecurityDescriptor);
    }
    return 0;
}

int StartClientServer(void) {
    g_shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_shutdown_event) return -1;
    g_listener = CreateThread(NULL, 0, client_server_thread, NULL, 0, NULL);
    return g_listener ? 0 : -1;
}

void StopClientServer(void) {
    SignalClientServerShutdown();
    if (g_listener) {
        WaitForSingleObject(g_listener, INFINITE);
        CloseHandle(g_listener);
        g_listener = NULL;
    }
    while (!g_handlers.empty()) {
        for (HANDLE worker : g_handlers) CancelSynchronousIo(worker);
        std::erase_if(g_handlers, [](HANDLE worker) {
            if (WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) return false;
            CloseHandle(worker);
            return true;
        });
        if (!g_handlers.empty()) Sleep(10);
    }
    if (g_shutdown_event) {
        CloseHandle(g_shutdown_event);
        g_shutdown_event = NULL;
    }
}

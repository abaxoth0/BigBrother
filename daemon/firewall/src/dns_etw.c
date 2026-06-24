#include "../include/dns_etw.h"
#include "../include/allowlist.h"
#include "../include/firewall.h"
#include "../include/dns.h"
#include <log/log.h>
#include <winevt.h>
#include <string.h>
#include <stdlib.h>

#define DNS_ETW_BUFFER_SIZE 8192

static HANDLE g_subscription = NULL;
static HANDLE g_etw_stop_event = NULL;
static HANDLE g_etw_thread = NULL;

static const WCHAR* g_dns_query =
    L"*[System[Provider[@Name='Microsoft-Windows-DNS-Client'] and EventID=3008]]";

static DWORD WINAPI etw_subscription_thread(LPVOID param) {
    (void)param;

    // Use a manual-reset event so EvtSubscribe wakes on new event or stop signal
    HANDLE signal_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!signal_event) {
        LOGF("[DNS-ETW] Failed to create signal event");
        return 1;
    }

    HANDLE wait_handles[2] = { signal_event, g_etw_stop_event };

    g_subscription = EvtSubscribe(
        NULL,                          // session (NULL = local)
        signal_event,                  // signal event
        L"Microsoft-Windows-DNS-Client/Operational", // channel path
        g_dns_query,                   // query
        NULL,                          // bookmark
        NULL,                          // callback context
        NULL,                          // callback (NULL = use signal event)
        EvtSubscribeToFutureEvents      // flags: only future events
    );

    if (!g_subscription) {
        LOGF("[DNS-ETW] EvtSubscribe failed: %lu", (unsigned long)GetLastError());
        CloseHandle(signal_event);
        return 1;
    }

    LOGF("[DNS-ETW] Subscription started, waiting for DNS events...");

    EVT_HANDLE events[16];
    DWORD count = 0;

    while (1) {
        DWORD result = WaitForMultipleObjects(2, wait_handles, FALSE, 1000);
        if (result == WAIT_OBJECT_0 + 1) {
            // Stop event signalled
            break;
        }
        if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT) {
            break;
        }

        // Process available events
        while (1) {
            ResetEvent(signal_event);
            if (!EvtNext(g_subscription, 16, events, INFINITE, 0, &count)) {
                DWORD err = GetLastError();
                if (err == ERROR_NO_MORE_ITEMS) break;
                if (err == ERROR_INVALID_OPERATION) break;
                break;
            }

            for (DWORD i = 0; i < count; i++) {
                // Render event as XML
                DWORD buf_size = DNS_ETW_BUFFER_SIZE;
                WCHAR* xml = (WCHAR*)malloc(buf_size);
                if (!xml) { EvtClose(events[i]); continue; }

                DWORD used = 0;
                if (!EvtRender(NULL, events[i], EvtRenderEventXml, buf_size, xml, &used, NULL)) {
                    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && used > 0) {
                        free(xml);
                        buf_size = used;
                        xml = (WCHAR*)malloc(buf_size);
                        if (xml) {
                            EvtRender(NULL, events[i], EvtRenderEventXml, buf_size, xml, &used, NULL);
                        }
                    }
                }

                if (xml && xml[0]) {
                    // Extract QueryName
                    const WCHAR* name_start = wcsstr(xml, L"<Data Name=\"QueryName\">");
                    const WCHAR* results_start = wcsstr(xml, L"<Data Name=\"QueryResults\">");

                    if (name_start && results_start) {
                        name_start += 23; // skip "<Data Name=\"QueryName\">"
                        const WCHAR* name_end = wcsstr(name_start, L"</Data>");
                        results_start += 26; // skip "<Data Name=\"QueryResults\">"
                        const WCHAR* results_end = wcsstr(results_start, L"</Data>");

                        if (name_end && results_end) {
                            size_t name_len = name_end - name_start;
                            size_t results_len = results_end - results_start;

                            char domain[256] = {0};
                            char results[1024] = {0};

                            // Convert wide to narrow
                            int domain_chars = WideCharToMultiByte(CP_UTF8, 0, name_start, (int)name_len, domain, sizeof(domain) - 1, NULL, NULL);
                            if (domain_chars > 0) domain[domain_chars] = '\0';

                            int results_chars = WideCharToMultiByte(CP_UTF8, 0, results_start, (int)results_len, results, sizeof(results) - 1, NULL, NULL);
                            if (results_chars > 0) results[results_chars] = '\0';

                            // Lowercase the domain
                            for (char* p = domain; *p; p++) {
                                if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
                            }

                            // Check if domain is whitelisted (not excepted)
                            int whitelisted = 0;
                            for (size_t w = 0; w < g_Whitelist.count; w++) {
                                if (!g_Whitelist.entries[w].is_exception &&
                                    DnsCheckDomain(domain, g_Whitelist.entries[w].domain)) {
                                    whitelisted = 1;
                                    break;
                                }
                            }

                            if (whitelisted) {
                                // Parse IPs from QueryResults
                                // Format: "type:  5 93.184.216.34;type:  5 93.184.216.35;"
                                // or just IP addresses separated by semicolons or spaces
                                char* p = results;
                                while (*p) {
                                    // Skip non-digit characters to find IPs
                                    while (*p && !(*p >= '0' && *p <= '9')) p++;
                                    if (!*p) break;

                                    // Try to parse an IPv4 address
                                    unsigned int a, b, c, d;
                                    if (sscanf(p, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
                                        uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;
                                        // Use TTL of 300s (minimum) for ETW-resolved IPs
                                        IpAllowlistAdd(&g_IpAllowlist, ip, domain, 300);
                                        DLOGF("[DNS-ETW] Added %u.%u.%u.%u for %s (DoH/ETW)", a, b, c, d, domain);
                                        p += 15; // skip past IP
                                    } else {
                                        p++;
                                    }
                                }
                            }
                        }
                    }
                }

                if (xml) free(xml);
                EvtClose(events[i]);
            }
        }
    }

    if (g_subscription) {
        EvtClose(g_subscription);
        g_subscription = NULL;
    }
    CloseHandle(signal_event);

    LOGF("[DNS-ETW] Subscription thread stopped");

    return 0;
}

int DnsEtwStart(HANDLE stop_event) {
    if (g_etw_thread) {
        LOGF("[DNS-ETW] Already running");
        return 0;
    }

    g_etw_stop_event = stop_event;

    g_etw_thread = CreateThread(NULL, 0, etw_subscription_thread, NULL, 0, NULL);
    if (!g_etw_thread) {
        LOGF("[DNS-ETW] Failed to create thread: %lu", (unsigned long)GetLastError());
        return -1;
    }

    LOGF("[DNS-ETW] ETW DNS monitor started");
    return 0;
}

void DnsEtwStop(void) {
    if (g_etw_thread) {
        WaitForSingleObject(g_etw_thread, 5000);
        CloseHandle(g_etw_thread);
        g_etw_thread = NULL;
    }
}

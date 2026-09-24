# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

### Fixed

- Client frontend: filtration status label no longer stuck on "Вкл" — an inline `Text` value was overriding the style trigger, so it never reflected the real state
- Client frontend: turning filtration off no longer shows "Отсутствует подключение к серверу" — a dedicated "Фильтрация отключена" health state now reports it accurately

- Server backend: changing a user's name/address no longer crashes the whole server — `changeUserProperty` dereferenced a nil user when the target name/address wasn't in the DB (reachable via `CHANGE_NAME` to a free name or `CONNECT` with a new client IP)
- Server backend: in-memory connection manager is now mutex-guarded — concurrent handler/cleanup access to the shared map caused `concurrent map writes` crashes under load
- Server backend: DB transaction helper now rolls back the open transaction on a prep error (leaked transactions kept the SQLite write lock and caused `database is locked`)
- Server backend: subscribers with a broken/stalled connection are dropped on write failure (`notification` write loop), no leaked goroutine/connection
- Server backend: request TLV parsing uses a large scanner buffer and surfaces scanner errors — large whitelist saves no longer fail at the default 64KB scanner cap

### Changed

- Server backend: SQLite uses WAL journaling + 5s busy timeout for resilience to write contention (deliberately not pinned to a single connection, which would deadlock the read-inside-transaction paths)
- Server backend: RPC server lifecycle hardened — idempotent `Start`/`Stop`, safe WaitGroup usage, and listener + active connections closed on shutdown so subscribed clients and idle peers exit cleanly; connection manager stopped on shutdown; discovery listener `Stop` idempotent
- Server backend: per-request 60s read deadline in the command loops (cleared inside `SUBSCRIBE` so subscribed clients are unaffected)
- Server backend: dedup and cleanup — shared `deleteUsers` helper; removed dead code (`sqlite.Test`, unused `writeStatus`/`readTLV`, dead backend-handler methods, unused package-level `pendingUsers` map, dormant per-user whitelist query); consolidated server-name default
- Server backend: added tests — connection manager, pending users, DB transactions (incl. rollback-on-error), protocol framing (incl. >64KB values), sqlite integration, broken-subscriber handling; all files gofmt'd

- Server frontend: full GUI rework — dark "rose-pine" theme (reuses the shared dark surfaces from the renamed `ThemeDark.xaml`, with its own rose/iris accents via the new `ServerAccent.xaml`), custom title bar, and tabs replaced with a right-side navigation menu
- Server frontend: users area reworked into a single table with a mode dropdown (connected clients / pending registrations) plus a name/address search box
- Server frontend: "Главная" shows a service/filtration health banner, log console with level-filter toggles (INFO/DEBUG/TRACE/WARNING/ERROR/FATAL) and jump-to-bottom, and a bottom status bar with a per-state users counter
- Server frontend: whitelist table reworked with "make active" toolbar action and highlighted active whitelist; dark table/chrome styling
- Frontends: custom-window-chrome base `ChromeWindow` moved into the shared library and reused by both frontends; shared dark theme renamed `ClientDark.xaml` → `ThemeDark.xaml`
- Server frontend: removed dead server-name/port save commands and dropped per-refresh log spam

- Client backend/firewall: whitelist relay no longer capped at 64KB — the server-sync buffer and firewall IPC reader use growing (heap) buffers with a 16MB soft bound, so large server whitelists aren't truncated
- Build: new committed `publish.sh` — one-shot publish of all daemons + self-contained win-x64 frontends into `dist/{client,server}-frontend`, then NSIS installers (previously the frontends had no scripted build step)

## [1.2.0] - 10-09-2026

### Fixed

- Client frontend: log-level filter toggle style no longer inherits the `Button`-based style (startup `XamlParseException`)
- Client frontend: combo-box dropdown hover showed dark-grey background with dark gold text (unreadable); hover is now gold with dark text
- Client frontend: gold buttons rendered white text — implicit `TextBlock` style overrode inherited foreground; removed it so text inherits the control color

- Client backend: frontend pipe TLV parsing is now buffered (chunked `ReadFile` instead of byte-at-a-time) with a dedicated pipe reader
- Client backend: server event line reader drains over-long lines so event framing stays intact (a long line no longer splits into a bogus follow-up line)
- Client backend: `send_to_server_tlv` connect retries reduced (3×200ms, was 10×500ms) so the subscribe loop isn't stalled up to 5s on an unreachable server
- Client backend: frontend pipe server accept loop is shutdown-safe — uses an overlapped `ConnectNamedPipe` waited on alongside the shutdown event, so the daemon exits cleanly on service stop even when no frontend is connected
- Client backend: `set` command checks file size and `malloc` result before reading (no more crash on oversized files or OOM)
- Client backend: `log_init` no longer leaks the `FILE*` from the log-path probe
- Client backend: frontend TLV responses stream field-by-field when they exceed 64KB, so large whitelist relays are no longer silently truncated
- Client backend: server IPC response parsing is now buffered and bounds-checked — length lines no longer overflow a 32-byte stack buffer, and oversized TLV values are drained so the parser stays in sync with the terminating empty line
- Client backend: `read_tlv_request` length lines are bounds-checked and value sizes capped (a malformed peer can no longer overflow the length buffer or force a giant allocation / blocking read)
- Client backend: whitelist relay to the frontend no longer caps at 256 domains (was silently dropping domains from large whitelists)
- Client backend: client log rotation size raised from a leftover 2KB test value to the standard 10MB
- Client backend: config.ini access is serialized with a critical section and rewritten atomically (`MoveFileEx` replace), so concurrent handler threads can't corrupt or temporarily delete the config file

### Changed

- Client frontend: GUI rework — truthful status indicators (dots reflected actual daemon/server state instead of being hardcoded green), overall health banner, log console readability (timestamps, level chips, level filter toggles, jump-to-bottom), collapsible control panel, removal of redundant status rows and duplicated "last update"/refresh controls
- Client frontend: tabs replaced with a right-side navigation menu; window widened ~20% (1000×700 → 1200×700, min 950×500)
- Client frontend: custom dark title bar (WindowChrome) with minimize/close and no maximize — replaces the default Windows chrome on the main window and dialogs
- Client frontend: dark "Command Gold" theme — new `ClientDark.xaml` palette (dark graphite surfaces, gold accent, consistent control styles: buttons, inputs, checkbox, combo box + dropdown, scrollbar, group boxes, expander) merged after the shared `Theme.xaml`; shared `Theme.xaml` keeps the original light values so the server frontend is visually unaffected
- Client frontend: removed the top menu bar (Файл/Вид) — actions remain available on the log header and status bar
- Client frontend: removed dead/unbound settings save commands; settings saved through one consolidated path
- Client frontend: window title normalized to "BigBrother"

- Client backend: adapter enumeration calls are cached (`get_adapters()`) so `GetLocalIp`, `is_local_ip`, `find_adapter_by_gateway`, `GetAllBroadcastAddresses`, and `DiscoverServersTCP` reuse the same query instead of each doing a probe+alloc+fill cycle
- Client backend: `GetLocalIp` uses adapter enumeration instead of a UDP connect to port 445 (works when port 445 is filtered)

## [1.1.0] - 19-08-2026

### Fixed

- Firewall: `IpcSetWhitelist` allowlist purge no longer deletes literal-IP allow rules (e.g. `8.8.8.8` from the whitelist file) — only domain-learned entries whose domain is no longer whitelisted are removed
- Firewall: DNS response parser skips unused answer records (CNAME, AAAA, etc.) so they no longer consume answer slots — a response with many extra records before the A record can no longer cause the domain's IPv4 addresses to be dropped from the allowlist
- Client backend: server-sourced whitelist buffers raised to 64KB (`g_last_whitelist`, GET_WHITELIST response) so a server whitelist larger than 8KB is no longer truncated before being pushed to the firewall
- Client backend: firewall IPC response reads are now message-mode with ERROR_MORE_DATA looping and 64KB buffers — whitelist dumps larger than 8KB are no longer silently truncated (removed domains from a big whitelist previously vanished mid-stream)
- Firewall: `IpcSetWhitelist` now purges allowlist entries whose domain is no longer whitelisted, so a domain dropped from the server-pushed whitelist stops being reachable immediately instead of lingering until its IP TTL expires
- Firewall: pre-resolve re-validates each resolved domain against the current whitelist before applying, so a concurrent whitelist change cannot (re)allow a removed domain
- Firewall: blocked-packet log throttle uses a wraparound-safe tick comparison (GetTickCount unsigned underflow no longer disables throttling after ~49 days uptime)
- Firewall: exception (`!domain`) rules now actually apply — IPs resolved for an excepted domain are kept in a blocklist checked before the allowlist, so the domain stays blocked even when it shares a CDN IP with an allowed domain
- Firewall IPC: fix deadlock in the named-pipe request reader — the message-mode read loop kept waiting for a second request after reading a complete one, so every client-backend ↔ firewall call (PING/GET_STATUS/GET_WHITELIST/SET_WHITELIST) hung and the client backend appeared down while the firewall service stayed up
- Installer: split into separate client (BigBrother-Client.nsi) and server (BigBrother-Server.nsi) installers
- Installer: service start type fixed to demand (manual), installers install to separate directories
- Server: reduce connection TTL from 10min to 15s with periodic cleanup goroutine (stale clients drop off within 15s)
- Client frontend: single save button for all settings, enabled only when a setting changed
- Client frontend: register button disabled while in-flight, success/error dialogs
- Client frontend: checkboxes go through save button (consistent with text fields)
- Server frontend: single save button for server name/port, enabled only when a setting changed
- Server frontend: context menu no longer loses focus on list refresh (in-place DataGrid updates)
- Server frontend: reload log path when service starts (no longer requires app restart)
- Server frontend: remove diagnostic spam from log reader (silent retry)
- Frontends: add `BigBrother.Frontend.Shared` project to both `.slnx` solutions (Visual Studio restore failed with NU1105 otherwise)
- Server frontend: pass service name to `ServiceManager` (fixes startup XamlParseException)
- Server frontend: main status indicator, label, uptime, and client/pending counters update immediately on service state change (was lagging behind the service-status text)

### Changed

- Firewall: `is_local` private-range check computed lazily (only when filtration is active) instead of on every packet
- Firewall: removed dead 1500-byte per-packet payload buffer and dead `DnsCheckDomain` wrapper
- Firewall: whitelist dedup is case-insensitive (uses the pre-lowered pattern)
- Firewall: packet hot path performs a single IP lookup with a cached timestamp (`IpAllowlistLookup`) instead of three separate `time()`-heavy `Contains`/`GetDomain` calls
- Firewall: blocked-packet logging is rate-limited per destination IP (once/second) so heavy blocking no longer saturates the log critical section
- Firewall: DNS response path uses a single lock acquisition (was shared+exclusive round-trips)
- Firewall: removed dead code (`IsAllowed`, `WhitelistContains`, `match_domain`, `DnsCheckDomains`)
- Firewall: pre-resolve no longer holds the allowlist lock during blocking `getaddrinfo` calls — domains are snapshotted under the shared lock, resolved outside it, then applied under the exclusive lock (whitelist reload/set no longer stalls the packet thread)
- Firewall: `inet_ntop` moved out of the per-packet hot path (only formatted for debug logs and blocked-packet logs)
- Firewall: whitelist/blacklist patterns are pre-lowercased at load time (`WhitelistEntry.pattern_lower`) and matched via a new `DnsCheckDomainLower` — the domain is lowercased once per lookup instead of per entry on the packet/DNS hot paths
- Firewall: IP allowlist/blocklist switched from a dynamic array to a uthash hash table keyed by IP — per-packet `Contains`/`GetDomain` lookups are O(1) average instead of O(n); expired entries are handled lazily and swept at most once a minute (was a full O(n) compaction on every add)
- Firewall: whitelist and IP allowlist/blocklist are now dynamic arrays (initial capacity 1000, realloc-doubling) instead of fixed-size static arrays — no more wasted memory from the 32768-entry IP allowlist and no hard cap on whitelist size
- Firewall: exception (`!domain`) rules moved into a separate `g_Blacklist` — allow-rule matching (DNS whitelist check, packet-path exception scan) no longer iterates over exception entries; the packet-path exception loop now only scans blacklist rules
- Firewall: cleanup pass fixing memory-safety, races, and functional bugs — DNS name parsing clamped against packet bounds and RFC 1035 label limits (fixes remote heap over-read via crafted UDP/53); log ring buffer is wrap-aware with entry-length validation and serialized producers; allowlist reads are pure (cleanup only under the exclusive writer lock); IPC reads full messages in a loop handling `ERROR_MORE_DATA` with 64KB pipe buffers; DoH-canary spoof appends the answer after the question so the 0xC00C pointer and UDP/IP lengths stay valid; whitelist reload holds the exclusive lock and no longer uses `strtok`; WinDivert recv/send error handling with backoff and drop counters; threads joined before closing event handles; log rotation size raised to 10MB
- Event-driven push replaces client daemon ↔ server polling: daemon subscribes (`SUBSCRIBE`) to server events (`WHITELIST_CHANGED`, `FILTRATION_TOGGLED`, `USER_APPROVED`) instead of polling `GET_WHITELIST`/`GET_FILTRATION`
- Client daemon: no longer polls the server on a fixed interval (`-d <ip> [poll-interval]` legacy arg dropped)
- Server frontend: dashboard refresh is event-driven over the frontend-pipe `SUBSCRIBE` feed; 5s full poll reduced to a 30s safety net
- Client frontend: status is event-driven via daemon pipe `SUBSCRIBE` (`STATE_CHANGED`); the two redundant 10s status timers consolidated into one shared service with a 60s safety fallback
- Frontends: non-blocking named-pipe connect probe (`ConnectAsync(0)`) instead of timed connects that busy-wait while the service is stopped
- Frontends: log tailing/perf — FileSystemWatcher (server) and 500ms poll (client) with debounce, incremental list updates, StringBuilder parsing
- Server: shared notification bus publishes user/pending/whitelist/filtration events to both TCP and pipe `SUBSCRIBE` clients
- Server: whitelist entries saved atomically (single transaction) via `ReplaceWhitelistEntries`; active whitelist read directly from DB (removed cached field)
- Client frontend: status refresh event marshaled onto the UI thread (no race with log-reader start/stop)
- Client daemon: removed dead `poll_interval` parameter and unused variables (builds warning-free)
- Frontends: extract shared `BigBrother.Frontend.Shared` library (ViewModelBase, RelayCommand, ServiceManager, LogReader/LogParser, models, converters, theme brushes); both apps now reference one copy
- Server frontend: log tailing extracted to `ServerLogTailer` service; whitelist dialog decoupled via `IWhitelistDialogService`
- Client frontend: daemon control, log readers, and log-history viewer moved into ViewModels (code-behind now thin); dead buttons/placeholders removed; `LogViewWindow` uses XAML template

## [1.0.0] - 27-07-2026

### Added
- Server UDP broadcast discovery on port 42069 (`[server]`)
- Server name setting in DB with frontend IPC (`GET_SERVER_NAME`/`SET_SERVER_NAME`)
- Client UDP discovery with all-adapter broadcast (`GetAllBroadcastAddresses`)
- Server selection dialog on client frontend ("Сменить" button)
- Configurable TCP port for client↔server communication (`--port` CLI flag, `config.ini [server] port`)
- Port field in settings UI on both client and server frontends
- Auto admin elevation (app.manifest `requireAdministrator` + runtime fallback) for both frontends
- Startup and periodic stats diagnostics in firewall
- `CHANGELOG.md`
- TCP subnet scan fallback (parallel /24 scan ~1.6s)
- Manual network settings: gateway IP, subnet mask, auto/manual toggle (`[network]` section in config.ini)
- Discovery toggle (`[discovery] enabled`)
- `NetClient.c`/`NetClient.h` — networking code extracted from `ipc_daemon.c`
- Whitelist import from `.wl` files (multi-select, parses `# Whitelist: Name` sections)
- Whitelist export to `.wl` files (checked items or all if none checked)
- Bulk delete button for whitelists with confirmation dialog
- Select-all checkbox in whitelist DataGrid header (bound to `IsAllSelected`)
- Whitelist exception rules: `!domain` entries exclude subdomains from wildcard matches
- Server backend: `GET_LOG_PATH` IPC command for frontend log access
- Firewall: filtration enable/disable toggle via IPC (frontend buttons on client & server)
- Client: auto-disable filtration on server disconnect (config option, checkbox in settings)
- Firewall: pre-resolve exact-match whitelist domains via `getaddrinfo` at startup and IPC update (handles DoH)
- Firewall: spoof `use-application-dns.net` DNS queries with `127.0.0.1` to force browsers to disable DoH

### Changed
- Client ↔ server transport: SMB named pipes replaced with TCP on port 1984
- Server RPC supports both `"pipe"` and `"tcp"` networks (`ServerConfig.Network`)
- Discovery response format: `name|ip|port` (was `name|ip`)
- Unified config files into single `config\config.ini` (replaced `server.cfg`, `user.cfg`, `config.txt`)
- Whitelist file automatically created with header if missing (firewall no longer crashes)
- Server backend reads port from DB on startup (CLI `--port` overrides, DB fallback, default 1984)
- Removed CLI `server <ip>` command (replaced by frontend settings UI)
- `GET_STATUS` IPC uses cached `IsServerSessionActive()` instead of blocking `PingServer()`
- Whitelist DataGrid: added checkbox column for multi-select, adjusted row height (26px min), fixed vertical alignment
- Export/delete buttons disabled when no whitelist items are selected (CanExecute predicates)
- Server backend runs as Windows service (`BigBrother Server`) with console fallback
- NSIS installer creates server service (was previously commented out)
- Server frontend: service start/stop/restart buttons on Главная tab; reworked into Главная/Пользователи/Списки/Настройки layout
- Server frontend: added live server backend log viewer (JSON-lines), removed old "Логи" tab
- Server frontend: moved filtration status + toggle to server status bar
- Firewall: gate DNS IP addition on filtration state; clear allowlist on re-enable
- Client/server: sync filtration state from server to client backend in DaemonRun loop
- Installer: split into separate client (BigBrother-Client.nsi) and server (BigBrother-Server.nsi) installers
- Installer: service start type fixed to demand (manual), installers install to separate directories
- Updated dependency: `ain` v1.2.1 → v1.2.2 (reduces idle CPU spin from 10µs to 10ms sleep)

### Fixed
- Firewall blocking all local traffic due to byte order mismatch (`ntohl` on `ip_hdr->DstAddr`)
- Firewall byte order in DNS IP parser (IPv4 addresses from DNS responses)
- Firewall `is_local` check using `||` instead of `&&` (local-to-remote was never blocked)
- Firewall allowlist lookup failing for static IP entries from whitelist file
- Firewall `inet_ntoa` debug log showing wrong IP (missing `htonl`)
- Client frontend log entries duplicating 10-15x (`OnNewLine` subscribed multiple times)
- Client frontend settings showing stale data when backend disconnected
- Client frontend hanging on shutdown (deadlock in `IpcService.Dispose`)
- Client backend not spawning when no server IP configured
- Client backend connection to server via actual IP (auto-converts to `"."` for localhost TCP)
- Client backend `send_to_server_tlv` Winsock not initialized (`WSAStartup`)
- Discovery returning 3 duplicate entries (dedup for `name|ip|port` format)
- Discovery not working with virtual network adapters (skip virtual adapters in auto-mode discovery)
- Server frontend process not exiting on window close (timer deadlock)
- Server frontend server name resetting every 5s (moved to one-time load)
- Synchronous `.Result` deadlocks in `RestartClientAsync`/`PingAsync`
- Missing `_serverPort` field declaration in client frontend ViewModel
- `;` comments not handled in whitelist file parser
- Server discovery across LAN (server firewall blocked outbound UDP response to non-local client IP)
- TCP scan: WSAStartup missing, response parsing (skipped length line), graceful close (shutdown+closesocket)
- TCP scan: 127.0.0.1 dedup (skip if same server already found on adapter IP)
- TCP scan: buffer full now breaks scan loop
- `GetAdaptersAddresses` now uses `GAA_FLAG_INCLUDE_GATEWAYS` to return gateway info
- `htons` cast changed from `(short)` to `(unsigned short)` to avoid overflow on ports > 32767
- Server discovery picking wrong adapter when server is on same machine: local IP detection replaces matching IPs with `127.0.0.1`; removed unconditional `127.0.0.1` UDP probe; fixed TCP manual mode skipping localhost probe
- Name collision error on whitelist import now reports explicit duplicate name message
- `WhitelistInfo.IsSelected` now fires `PropertyChanged` (checkbox binding actually works)
- Import/export buttons layout fixed (orphaned tags, button overlap)
- Client connection to local server no longer fails with "already connected" when daemon auto-connects first
- Server user registration: overwrite existing user by name or addr instead of failing on UNIQUE constraint
- Server `changeUserProperty` checks unique constraints on name/addr before updating
- Firewall: enforce minimum 300s TTL for all DNS-resolved IPs (not just zero-TTL)
- Firewall: add SRWLOCK thread safety for `g_Whitelist`/`g_IpAllowlist` (prevents race between filter loop and IPC updates)
- Firewall: copy domain from `IpAllowlistGetDomain` to local buffer before use (fixes dangling pointer)
- Firewall: don't clear IP allowlist on non-empty whitelist updates (existing connections keep working)
- Firewall: increase IP allowlist size from 1024 to 32768 entries
- Firewall: force cleanup and retry when IP allowlist is full

## [0.0.0] - 20-05-2026

- Initial release

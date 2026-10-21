# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

### Added

- Client/server settings: disable Windows notifications independently; preferences persist across restarts and apply immediately after saving, including while frontends are closed

- Server settings to publish and withdraw client update packages from an existing network share, with file selection, UNC paths and Windows credential prompts when required
- Client «Обновления» tab with explicit update checks, installed and available versions, release notes and manual installation with progress
- Self-contained client update executables and companion `.manifest.json` files containing package identity and release notes parsed from CHANGELOG.md
- Installation backups, rollback on failure and recovery after an interrupted update
- Common application version in root `VERSION`, embedded in all binaries, displayed at the bottom of frontend sidebars and available through `--version` in daemons and the notification helper

- Server whitelist tab: stage activation changes locally with «Сохранить» and «Отмена»; save all changed flags in one transaction and send clients a single refresh event; retain drafts across refreshes and failed saves

- Server whitelist tab: domain/IPv4 checker opens in a separate window and tests the composed active whitelist; removed the checker from the whitelist editor and ordered table columns as «Название», «Записей», «Активный»

- Server: compose all enabled whitelists into one client whitelist, remove case-insensitive duplicates and both sides of exact allow/exception conflicts; add an «Активный» checkbox column and a resizable conflict view showing source lists; migrate the previously active whitelist automatically

- Server: Windows notifications for client connections, disconnections and registration requests, sent by the backend without requiring a running frontend
- Client: Windows notifications for server connections, disconnections, filtration changes and whitelist updates, sent by the backend without requiring a running frontend
- Notifications: shared C++ helper uses WinRT toasts in active console and Remote Desktop user sessions; notifications persist in Notification Center and open the corresponding frontend when clicked; installers register separate Client/Server identities, with automatic per-user registration for standalone builds
- Notification testing: launching the helper without arguments sends a Client test toast; `--show [Client|Server]` and `--test [Client|Server]` select the identity; console traces and a rotating `notifications.log` report delivery stages and errors, with an error dialog for failed tests launched without a console

- Client/server frontends, client/server backends and firewall: embedded their dedicated application icons from `assets/icons` into Windows executables
- Server whitelist table: double-clicking a row opens its editor
- Server whitelist editor: full-line backgrounds mark errors in red and warnings in amber, with errors taking priority; highlights update with validation and follow scrolling without changing text colours
- Server whitelist editor: line numbers stay aligned with entries while editing, resizing and scrolling; long rules scroll horizontally
- Server whitelist editor: live validation rejects malformed entries, duplicate rules and directly conflicting exceptions before saving; overlapping exceptions show warnings, and imports use the same validation

### Fixed

- Client: relay empty firewall whitelist responses correctly and clear the frontend when all server lists are disabled; show «Белый список пуст. Нет активных правил.» in both formats, preserve the current display and retry after a failed whitelist request

- Client/server live log views: support multiple-row selection, Ctrl+A, Ctrl+C and themed right-click Copy, preserving displayed order and log metadata

- Client log history: show local date/time on every entry; support multiple-row selection, Ctrl+A, Ctrl+C and right-click Copy, including timestamps and source/level in copied and exported records

- Server: log frontend connections only when the event subscription is established, avoiding repeated messages from health checks and ordinary pipe requests
- Client/server frontends: prevent live logs from stalling until service shutdown with periodic read retries and batched UI updates; keep the newest pending entries under overload and preserve split binary records/JSON lines
- Client history: avoid UI deadlocks when resolving log paths; load complete log files in the background with loading status and cancellation when the window closes
- Client log viewer: use dark tabs and content borders, fully rounded headers, gold selection and visible hover/keyboard-focus states
- Frontend buttons: honor content alignment so «К последнему» stays centered in the log toolbar

- Server frontend: skip whitelist/conflict refreshes while the backend is unavailable and use dark theme colors for the whitelist conflict divider, including hover, focus and drag states

- Server domain checker: use readable theme colors for the domain/IPv4 label and show all source whitelist names for each matching rule, including deduplicated rules

- Server discovery: rate limits now share a budget across UDP source ports from the same IP, with bounded source tracking; replies advertise the running TCP port instead of a pending saved value
- Server deployment: startup synchronizes program-scoped Windows Firewall rules with the effective TCP port, removes the legacy default-port rule, and limits UDP discovery to local subnets on Private/Domain profiles; firewall setup failures prevent startup and uninstall removes both current and legacy rules
- Server settings: invalid TCP ports and failed database writes are rejected; saving a valid port explains that a service restart is required

- Client backend: new identity keys are private from creation, and unreadable/corrupt existing keys are no longer silently regenerated
- Client backend: fixed INI updates writing keys into later sections (e.g. `fingerprint` and `token` under `[network]`), which made saved credentials appear missing after restart; updates now stay within the requested section and preserve same-named keys in other sections
- Client backend: automatically repairs valid, unambiguous misplaced credentials into `[server] fingerprint` and `[client] token` when read; existing values, including explicit empty values, remain authoritative, and malformed/conflicting misplaced values do not silently enable first-use certificate trust
- Client frontend: reopening Settings retries saved fingerprint retrieval and reports an empty config value or daemon error explicitly instead of silently leaving the field blank
- Client frontend: saved certificate fingerprint loads from local config when settings open, including after GUI restart or while the server is offline; failed inspection no longer clears the displayed saved pin
- Client/server: token rotation now recovers automatically through the registered signing key after explicit authentication rejection, with bounded retries and stale-cache invalidation; active subscriptions revalidate on heartbeat and the server GUI no longer requests manual token entry
- Client backend: failed atomic config replacement preserves the original file and reports failure, allowing replacement credentials to remain usable in memory
- Server frontend: backend connectivity and settings availability now follow pipe responses independently of Windows service state; bounded asynchronous health checks detect unavailable/stalled backends and refresh data on reconnect
- Security: remote TCP clients can no longer change filtration with `SET_FILTRATION`; the command is restricted to the administrator frontend pipe, accepts only `0` or `1`, and reports database write failures without publishing a change event
- Client settings: outbound IPv6 blocking is configurable and enabled by default while filtration is active; disabling it allows IPv6 without whitelist checks while IPv4 filtering continues; the preference persists across restarts
- Firewall: removing a literal-IP rule from a nonempty whitelist immediately revokes its cached permission; retained literal-IP rules remain allowed
- Firewall: pending packet receives are cancelled on service stop, so shutdown no longer waits for network traffic
- Firewall: whitelist reload frees existing IP allowlist/blocklist entries instead of leaking their allocations
- Firewall: IPC whitelist replacement preserves the current rules on allocation failure and reports the failure instead of installing a partial list
- Client frontend: no more delayed old logs on startup — the firewall/client daemons now rotate a pre-existing log file on start, so each session writes to a fresh log and the frontend never replays a previous session's entries in batches
- Client: filtration status now reports "Выкл" while the firewall is not running (default no longer "Вкл"), and re-aligns the firewall state on the next status report when it comes back

### Changed

- Server whitelist table: highlight all checked rows with the teal selection background and cyan text

- Server whitelist table: context-menu Delete applies to all checked lists and replaces the toolbar Delete button; Delete and activation toggles clear checkboxes; Ctrl-click toggles a checkbox and Shift-click selects a range in the current table order

- Server whitelist table: show green «Да» / red «Нет» activation labels; the context-menu activation toggle applies to all checked lists, falling back to the current row when none are checked

- Build scripts require a Debug or Release configuration; native and Go builds select matching compiler settings, keep mode-specific objects separate and reject installer payloads built in another mode
- Release builds compile out debug/trace logging in daemons, the notification helper and server frontend while retaining normal operational logs; Debug builds enable diagnostics

- Client/server installers offer Manual (on demand) or Automatic service startup, default to Automatic for the firewall and Manual for the server, and leave services stopped after installation and upgrades
- Build scripts and Makefiles use all available CPU cores, compile C/C++ and mbedTLS sources in parallel, and enable parallel installer compression
- Native, Go, Visual Studio and installer builds read the common version from `VERSION`; installers reject application payloads with mismatched versions
- Installer builds accept an optional changelog path instead of a version argument; release notes use the matching version heading or fall back to `Unreleased`

- Notification helper: moved the app to the top-level `notifications` directory with `include`, `src`, `resources` and `tests` subdirectories, keeping its shared C interface in `common/notify.h`; updated build and client integration paths

- Installer: replaced NSIS with separate Inno Setup client/server installers; Wine compiler installation and installer-only build scripts support custom payload/output directories, publishing now builds installers through Wine

- Client installer: installs the frontend in the `GUI` subdirectory and points the desktop shortcut there, matching the server layout
- Server status bar: connected users now appear before pending registrations in the labels and counters
- Server tables: centered cell content and column headers horizontally and vertically, including whitelist selection checkboxes
- Server tables: removed the default blue focus outline around individual cells
- Server whitelist editor: removed the highlighting legend label to reduce clutter
- Security: explicit Windows ACLs restrict client identity/config and server database/TLS storage to SYSTEM/Administrators, with protected inheritance and Administrators ownership; existing permissions are repaired and installers secure storage before service startup
- Server: active database and TLS identity migrate into a private `data` directory, preserving committed WAL data and certificate identity; restricted originals are retained for recovery
- Security: verified certificate rotation — server settings expose the running certificate fingerprint; client settings inspect certificates without changing trust or sending credentials and accept a separately verified fingerprint only after rechecking the peer and saved endpoint/pin
- Server frontend: cyan primary accents (`#04D9FF`) with contained Save buttons, selected navigation items and log filters using black text; dark teal (`#083B47`) selection/highlight backgrounds, light cyan (`#70E8FA`) accent labels, cyan focused input borders, and distinct cyan DEBUG and darker teal TRACE (`#4096A6`) log text and level badges
- Client: filtration connection-sync (was "auto-disable on server disconnect") — filtration now stays off whenever the server connection is down for any reason (including a manual "Отключиться") and re-enables on reconnect, while a user's manual OFF is preserved until explicitly re-enabled; the effective state is re-applied on every status report; the option is enabled by default and the settings checkbox/help text were reworded to match
- Security: TLS over the client<=>server TCP channel — the server uses a self-signed EC certificate (generated on first run) and the listener requires TLS1.2+; the client uses vendored mbedTLS with server-certificate fingerprint pinning (trust-on-first-use)
- Security: per-user token authentication — `CONNECT`/`SUBSCRIBE`/`DISCONNECT`/`REFRESH`/`GET_WHITELIST` require a per-user token (constant-time check); `REGISTER` stays open for onboarding
- Security: automatic token delivery — the client generates a persistent ECDSA-P256 identity key, shares its public key at registration (bound at approval and refreshed on re-approval), then signs a single-use server challenge to receive its token over TLS; no manual configuration needed
- Security: the client caches the fetched token in memory (with best-effort persistence to `config.ini`) so CONNECT/SUBSCRIBE use it immediately; the manual "Токен сервера" settings field was removed
- Server: per-user `token` and `sign_public` columns (auto-migrated), `GET_TOKEN_CHALLENGE`/`GET_TOKEN` commands with ECDSA-P256 signature verification
- Security: restricted local IPC — the server frontend pipe, client backend pipe and firewall pipe now allow only SYSTEM/Administrators/Owner (were Everyone / default), and the client backend pipe only talks to the client GUI process (by executable name)
- Security: added `check_ipc_access.py` (and a `.bat` launcher) to probe the named pipes and verify the restrictions
- Security: resilience limits — TCP listener caps concurrent connections (default 256) and rate-limits connections per source IP; the event bus caps push subscribers (128); discovery replies are rate-limited per source
- Security: tokens are never broadcast — approval events no longer carry the token (the client obtains it via the signed `GET_TOKEN` challenge); documented TLS/rotation hygiene in `docs/BBMP.md`/`docs/TODO.md`

### Security

- Client updates require administrator rights, copy packages to local storage and verify their size and SHA-256 before installation; SYSTEM/Administrators DACLs protect staged packages, updater executables, backups and recovery journals

## [1.3.0] - 27-09-2026

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

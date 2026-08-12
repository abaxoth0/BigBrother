# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

### Fixed

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

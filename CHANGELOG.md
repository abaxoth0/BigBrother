# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
- Virtual adapter detection: skip Hyper-V, VMware, VirtualBox adapters in auto-mode discovery
- Local IP detection in UDP discovery responses: replace matching local IPs with `127.0.0.1`

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
- Discovery not working with virtual network adapters (now broadcasts on all active adapters)
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
- Server discovery picking wrong adapter when server is on same machine: removed unconditional `127.0.0.1` UDP probe, fixed TCP manual mode skipping localhost probe
- Name collision error on whitelist import now reports explicit duplicate name message
- `WhitelistInfo.IsSelected` now fires `PropertyChanged` (checkbox binding actually works)
- Import/export buttons layout fixed (orphaned tags, button overlap)
- Server backend runs as Windows service (`BigBrother Server`) with console fallback (detects service mode via `svc.IsWindowsService()`)
- NSIS installer creates server service (was previously commented out)
- Server frontend: service start/stop/restart buttons with 2s status polling
- Whitelist exception rules: `!domain` entries exclude subdomains from wildcard matches
- Client connection to local server no longer fails with "already connected" when daemon auto-connects first
- Server user registration: overwrite existing user by name or addr instead of failing on UNIQUE constraint
- Server `changeUserProperty` checks unique constraints on name/addr before updating (returns conflict error if already claimed by another user)

## [0.0.0] - 20-05-2026

- Initial release

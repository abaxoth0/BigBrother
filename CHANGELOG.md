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

### Changed
- Client <-> server transport: SMB named pipes replaced with TCP on port 1984
- Server RPC supports both `"pipe"` and `"tcp"` networks (`ServerConfig.Network`)
- Discovery response format: `name|ip|port` (was `name|ip`)
- Unified config files into single `config\config.ini` (replaced `server.cfg`, `user.cfg`, `config.txt`)
- Whitelist file automatically created with header if missing (firewall no longer crashes)
- Server backend reads port from DB on startup (CLI `--port` overrides, DB fallback, default 1984)
- Removed CLI `server <ip>` command (replaced by frontend settings UI)

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

## [0.0.0] - 20-05-2026

- Initial release

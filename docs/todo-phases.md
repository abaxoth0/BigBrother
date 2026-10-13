# TODO implementation phases

Each phase should be delivered on its own branch and tested in the Windows VM before the next phase is applied. Take a VM snapshot before testing. Do not combine installer or credential changes with the GUI-only first phase.

| Phase | Scope | VM acceptance checks | Status |
|---|---|---|---|
| 1 | Server GUI connectivity: separate backend reachability from service state; bounded pipe health checks; clear disconnected data and refresh on reconnect | Service and console-mode startup, shutdown, reconnect, and stalled/malformed pipe responses | Implemented; VM verified |
| 2 | Token rotation: invalidate persisted and in-memory stale tokens on explicit authentication rejection; bounded signed-challenge recovery; correct server GUI instructions | Rotate a connected client's token, reconnect without editing config, reject a client with the wrong identity key, and avoid retry loops | Merged |
| 3 | Certificate rotation: administrator-controlled inspection and verification of the replacement fingerprint before changing the pin | Reject an unexpected replacement certificate; accept a verified replacement; persist the new pin across restart | Merged |
| 4 | Windows secret permissions: explicit ACLs for existing/new server and client keys; review database/config token storage and installer integration | Elevated services/frontends still work; an ordinary user cannot read, replace, or modify protected secrets; reinstall preserves access | Merged; VM verified |
| 5 | Discovery and deployment rules: throttle discovery by source IP; synchronize Windows Firewall rules with the configured TCP port; document router/proxy checks | Vary UDP source ports without bypassing the rate limit; configure a nondefault TCP port and reconnect; check firewall profiles and uninstall cleanup | Implemented; awaiting VM verification |

Router/proxy exposure must be checked in the actual deployment; it cannot be verified from this repository alone. The phases cover the older TODOs, not the separate feature ideas.

## Phase 1: server GUI connectivity

Branch: `bugfix/server-connectivity`.

Only the server frontend needs updating. Keep the existing backend, client, credentials, and installer for this phase. Close the GUI before replacing its published files, or run the new GUI from a separate extracted directory as administrator. Keep only one GUI instance open during the checks.

The GUI checks the backend pipe every two seconds, with a 1.5-second request deadline. Service status remains a separate field. Allow about four seconds for a transition on an idle VM; the backend must answer successfully before the GUI reports it as connected.

1. **Normal service run**: start the installed server service and open the GUI. Confirm the backend reports connected, uptime is displayed, and settings are available. Leave it running for at least 30 seconds; it must stay connected.
2. **Stop**: stop the server service while leaving the GUI open. Confirm the backend becomes disconnected, uptime/counters/active whitelist are cleared, and settings become unavailable.
3. **Console backend after GUI startup**: with the service stopped, run the existing `BigBrother Server Daemon.exe` as administrator in console mode. Confirm the GUI reconnects without clicking Refresh. The service field may remain `Stopped` or `Not Found`, while backend status and settings are available.
4. **GUI startup with a console backend**: close and reopen the GUI while the console backend is running. Confirm it stays connected for at least 30 seconds, even though the service is stopped or not registered.
5. **Reconnect**: stop the console backend and confirm the GUI disconnects. Relaunch it and confirm client/whitelist data and server settings reload automatically. Repeat with the registered service.
6. **Unavailable pipe with a running service**: if the backend's pipe is unavailable or stalled, the GUI must not report connected merely because Service Control Manager says `Running`. The automated tests below cover a stalled pipe without modifying the installed service.

### Automated verification

Run from the repository root with .NET 10 SDK installed:

```shell
dotnet run --project server/frontend.tests/BigBrother.Server.Connectivity.Tests.csproj
dotnet build "server/frontend/BigBrother Server.csproj"
```

The regression executable uses an isolated pipe name and checks missing, healthy, malformed, rejected, stalled, reconnected, and closed pipes. It does not stop or modify the installed service. The WPF/service-state checks above still require the Windows VM.

Record VM results before proceeding to Phase 2. A backend that stalls a normal administration command can still stall that command; this phase bounds the independent health request only.


## Phase 2: token rotation recovery

Branch: `bugfix/token-rotation-recovery`.

Update the client daemon, server daemon, and server GUI together. Stop the client firewall service (which owns the daemon) and server service, close the GUIs, back up the existing executables, and replace only these binaries/published GUI files. Keep `config.ini`, `client_sigkey.key`, the server database, and server TLS files intact. Restart services and reconnect. The package does not contain credentials, databases, or installer changes.

1. **Connected rotation**: approve/connect a client, then use the server's token-reset action. The GUI must explain automatic recovery rather than instructing you to enter a token. Within about 30 seconds, the client should reconnect, fetch a replacement through its registered identity key, and resume whitelist/event synchronization without editing config. A short disconnected interval is expected.
2. **Saved stale token**: stop the client firewall service, replace only `[client] token` in its config with an old token, and restart. Confirm automatic recovery. Restart again and confirm the replacement was persisted and works immediately.
3. **Repeated rotation**: rotate the same client's token several times, allowing it to recover between rotations. Confirm it resumes events and no longer retains a subscription authorized with the old token.
4. **Wrong identity**: on a disposable VM snapshot, stop the client firewall service, back up/move `client_sigkey.key`, and put a stale token in config. Restart to generate a different identity. Do not approve/re-approve that identity on the server. It must not obtain the replacement token; failed signed fetches must be spaced at least five seconds apart. Stop the service, restore the original key, and restart; recovery should succeed.
5. **Network/TLS failure**: briefly disconnect the server/network without rotating the token. Confirm the client retains its cached token, does not log a successful replacement, and reconnects when connectivity returns. A certificate mismatch must not clear the token or fingerprint pin.
6. **Policy/event synchronization**: after recovery, change the active whitelist and filtration state on the server. Confirm the recovered client receives and applies the updates.

Automated checks:

```shell
make -C client/backend token-test-build
client/backend/build/token-recovery-test/test.exe
cd server/backend
go test ./...
```

Run the Windows C executable on Windows (or through Wine). The client test simulates authenticated transport responses while exercising real identity signing, cache invalidation, retry limits, and failed config persistence in an isolated build directory. Windows RPC tests verify that a registered identity can fetch the rotated token, a different identity cannot, and an existing subscription closes on the next heartbeat after rotation. Linux Go tests exclude Windows-only handlers.

Record VM results before proceeding to certificate rotation.


## Phase 3: verified certificate rotation

Branch: `feature/certificate-rotation`.

Update both daemons and both published GUIs together. Stop the client firewall service and server service before replacing binaries, and close the GUIs. Preserve the server database, server TLS files, client identity key, and client config. Restart services. No installer or key-ACL changes are included.

1. **Baseline**: connect the client normally. In server settings, copy the SHA-256 fingerprint under **Сертификат сервера**. In client settings, click **Проверить сертификат**. Confirm the saved and observed fingerprints match the server value. Inspection must not change `[server] fingerprint` or `[client] token` in config.
2. **Unexpected replacement**: take a VM snapshot and stop the server service. Locate its actual data directory using the backend's database-path log, back up `tls/server.crt` and `tls/server.key`, and move the originals out of those paths. Restart the service to generate a replacement certificate. Keep the database and client identity key intact. Confirm the client rejects the changed certificate and does not silently replace its pin or clear its token.
3. **Verified rotation**: after restart, copy the new running fingerprint from server settings through a trusted channel. In client settings, inspect the certificate again. Paste the trusted server value into **Проверенный отпечаток из настроек сервера**, then click **Применить проверенный отпечаток**. The button must remain disabled for an empty, malformed, or different fingerprint. Confirm successful reconnect and resumed whitelist/events without editing config or re-approving the client identity.
4. **Persistence**: verify `fingerprint` is under `[server]` and `token` is under `[client]` in client config. Older builds could append both under `[network]`; the updated daemon repairs valid, unambiguous misplaced values when read. Restart the client firewall service and GUI. Confirm the saved pin is displayed immediately in settings, before clicking Inspect. Stop the server and reopen the client GUI; the saved pin must still appear. A failed inspection must retain that displayed saved value. Restore the server and confirm the new pin matches the observed certificate and the connection works immediately.
5. **Stale inspection**: inspect, then change/save the server endpoint or replace/restart the certificate again before applying. Acceptance must fail; inspect the new state and supply its independently verified fingerprint. Changing a pin concurrently also invalidates the old inspection.
6. **Running versus on-disk certificate**: replacing certificate files while the server is still running must not change the fingerprint reported by its GUI. The GUI reports the listener's startup certificate, not arbitrary file contents. After restarting, it must show the new running fingerprint, including when the restart is too fast for service polling to observe a disconnected state.
7. **Offline/stalled server**: inspection should fail within roughly eight seconds on an idle VM and preserve trust. It sends a TLS handshake only, never a token or signed challenge to an unverified peer.
8. **Rollback**: restore the original certificate/key pair with the server stopped, restart it, and repeat the explicit fingerprint-verification procedure if the client has already accepted the replacement.

The client inspects its currently configured, saved endpoint. Save address/port edits before inspection. Existing first-use pinning remains available for normal onboarding; inspection itself never creates or clears trust.

Automated checks:

```shell
make -C client/backend certificate-test-build
client/backend/build/certificate-state-test/test.exe
python3 client/backend/tests/certificate_probe_test.py
```

The Python test requires Python and OpenSSL; on Linux it additionally uses Wine. It generates two temporary test certificates and checks actual TLS fingerprint inspection, matching-pin acceptance, replacement rejection, no application-data transmission, and a bounded stalled handshake. The state test covers malformed fingerprints, endpoint/pin/peer changes, concurrent updates, failed persistence, and token preservation. Windows RPC tests cover reporting the active fingerprint through the restricted administrator pipe.

Record VM results before proceeding to Windows secret permissions.


## Phase 4: Windows secret permissions

Branch: `feature/windows-secret-permissions`.

Take a VM snapshot before this phase: the active server database location changes. Stop the server service, the client firewall service, and any console backend instances. Replace both daemon executables. GUIs and client config/key locations do not change. Run each updated daemon as administrator with `--secure-storage` before restarting services; a nonzero result means setup failed and must be resolved before continuing. The installers do this automatically and do not start services on setup failure.

### Storage layout

- Client: the existing `config` directory (including token-bearing `config.ini`/temporary files) and `client_sigkey.key` are restricted to SYSTEM/Administrators. Application-directory permissions allow ordinary users to read/execute but not replace private child directories or files. Existing key bytes are preserved. Existing unreadable/corrupt keys are rejected instead of regenerated.
- Server: the existing database's parent is retained, with active storage under its private `data` child: `data/bb-server.db`, SQLite sidecars, and `data/tls/server.crt`/`server.key`. The actual location is printed by `--secure-storage` and logged on startup. Private directories and repaired files use protected DACLs for SYSTEM/Administrators and an Administrators owner. Newly created config/SQLite sidecars inherit the private parent ACL; their creator may be SYSTEM or the elevated administrator.
- Migration takes a consistent SQLite snapshot, including committed WAL data, and publishes it only after completion. TLS certificate/key bytes are copied unchanged. Original database/TLS files remain restricted for recovery; the updated backend uses only the active `data` directory. Do not run older backend versions alongside the new one, or roll back just the executable: the original database becomes stale once the new server writes data. Use the VM snapshot for a complete rollback.
- Future certificate rotation uses the active `data/tls` directory. Retired original TLS files are not the running identity.

### VM checks

1. **Migration**: note client names, whitelists, active whitelist, settings, and the server fingerprint before updating. After setup/restart, confirm all are preserved and clients reconnect without re-approval, token changes, or pin changes.
2. **Ownership and ACLs**: from an elevated PowerShell, inspect the client config directory/file, identity key, server `data` directory/database, and server TLS key using `Get-Acl` or `icacls`. Repaired directories/keys/database should be owned by BUILTIN/Administrators with protected ACLs; new config/SQLite sidecars may be owned by SYSTEM or the elevated creator and inherit the private parent ACL. Secret access must be granted only to SYSTEM and Administrators.
3. **Standard-user denial**: log in as a separate local standard-user account (not an administrator with a filtered token). Reading, appending, deleting, renaming, replacing, or changing the ACL/owner of private keys, config, database and sidecars must fail. Replacing/renaming the private `config` or server `data` directory must also fail. Public application files should remain readable.
4. **Elevated operation**: services run as SYSTEM and elevated GUIs should still work. Test settings saves, token rotation, verified certificate rotation, and whitelist synchronization; restart services and repeat the ACL checks.
5. **Existing permissions repair**: on a disposable snapshot, grant a standard-user read ACE to an existing config/key, then run elevated `--secure-storage` again. It must remove that access without changing the identity or credentials.
6. **No silent key replacement**: on a disposable snapshot, corrupt an existing client identity-key file. The client must report failure rather than overwrite it with a new identity. Restore the original key and rerun setup.
7. **Installer integration**: install/reinstall the newly built installers in the VM. Services must be stopped before file replacement and started only after successful permission setup. Existing data/identities must survive. Do not use a pre-change installer for this check.
8. **ACL regression executable**: run `tests/secret-permissions-test.exe` from the package as administrator. It checks ownership/protected ACLs and uses a restricted token to verify that read/write/delete/owner/DACL operations on a private test key are denied. It touches only its own test directory. Wine explicitly skips these assertions because it does not reproduce Windows ownership/access enforcement.

The package also contains `tests/secretfs.test.exe` for migration regression checks on Windows. Linux/Windows migration tests cover WAL contents, TLS identity preservation, repeat startup, retired originals, and rejection of incomplete identities. Credential regressions still pass; actual NTFS enforcement requires the VM checks above.

Record VM results before proceeding to discovery and firewall rules.


## Phase 5: discovery and firewall rules

Branch: `bugfix/discovery-firewall-handling`.

Update the server daemon and server GUI, then use the updated server installer for the upgrade/uninstall checks. The client binaries are unchanged. Preserve credentials, database and TLS files. Take a VM snapshot before the uninstall check: the existing uninstaller removes the installation directory, including its private data.

### Behavior

- Each source IP may receive one discovery response every 500 ms, regardless of its UDP source port. IPv4 and its IPv4-mapped IPv6 representation share a budget. At most 4096 sources are tracked; a full table rejects new sources until a budget expires.
- Discovery reports the port of the running TCP listener. Saving a new port does not advertise it prematurely. Restart the server service to apply it. The validated `--port` argument still overrides the saved setting in console/service executable arguments.
- Before starting listeners, the daemon replaces rules named `BigBrother Server (TCP)`, `BigBrother Server (TCP 1984)` and `BigBrother Discovery (UDP 42069)`. The replacement rules are restricted to the running executable and Private/Domain profiles. TCP uses the effective port; UDP uses 42069 and accepts only LocalSubnet sources. Other rules are untouched.
- Firewall setup uses the built-in Windows PowerShell NetSecurity module with a 30-second deadline. Service startup stays pending until setup succeeds; a failure stops startup and is logged. Elevated console mode performs the same setup. `--secure-storage` only prepares storage and does not change firewall rules.
- The installer no longer creates a hardcoded TCP rule. Service startup installs the rules; setup reports service-start failure. Uninstall removes the current names and legacy TCP name.

### VM checks

1. **Default startup and upgrade**: install the updated server, start it, and connect the client. Inspect the rules from elevated PowerShell:

   ```powershell
   $names = 'BigBrother Server (TCP)', 'BigBrother Server (TCP 1984)', 'BigBrother Discovery (UDP 42069)'
   $rules = Get-NetFirewallRule | Where-Object { $_.DisplayName -in $names }
   $rules | Format-Table DisplayName, Enabled, Profile, Direction, Action
   $rules | Get-NetFirewallPortFilter | Format-Table Protocol, LocalPort
   $rules | Get-NetFirewallApplicationFilter | Format-Table Program
   $rules | Get-NetFirewallAddressFilter | Format-Table RemoteAddress
   ```

   Expect one TCP and one UDP managed rule, no legacy TCP rule, Private/Domain profiles, the installed daemon path, TCP 1984 (or the existing configured port), UDP 42069, and LocalSubnet for UDP. Restart twice; rules must not accumulate.
2. **Port change**: save 4443 in server settings. Before restart, discovery must still advertise the old active port and the client must still connect there. Restart the service. Confirm TCP rule is now 4443, discovery returns 4443, and a client on another VM connects using that port. Check there is no remaining managed TCP 1984 rule. Return to the original port and restart. Test a client across VMs, since same-machine connections do not validate inbound firewall handling.
3. **Override**: stop the service and run the daemon elevated with `--port 4444`. Confirm the log, discovery and TCP rule all use 4444 while the saved port remains unchanged. Stop console mode and restart the service; rules/discovery must return to the saved port. Invalid values such as 0, 65536 or `1984junk` must be rejected rather than opening a different port.
4. **Rate limit**: run `discovery-test.exe -test.v` from the package. It sends requests from two sockets with different UDP source ports, checks that the second is throttled, and checks the advertised active port. It uses an isolated loopback socket and does not change installed settings. Source-table capacity, expired-budget recovery and IPv4-mapped address normalization also have automated checks.
5. **Profiles and deployment**: verify cross-VM discovery/connectivity with the network marked Private or Domain. The managed rules must not allow inbound traffic on Public. Review other local/GPO rules that could independently grant access. In the real deployment, check router forwarding, reverse-proxy listeners and upstream ACLs separately: TCP exposure should match the intended clients, and UDP discovery should remain on the LAN. These external settings cannot be verified from the repository. See [Microsoft's Windows Firewall command guidance](https://learn.microsoft.com/en-us/troubleshoot/windows-server/networking/netsh-advfirewall-firewall-control-firewall-behavior) for program, profile and subnet rule scoping.
6. **Setup failure**: on a disposable snapshot, make the NetSecurity module/firewall service unavailable. Startup must fail with a firewall error, and the installer must report failure. Restore the environment before retesting; do not alter production policy for this check.
7. **Uninstall**: on the disposable snapshot, uninstall. Verify all three managed rule names are absent, including after a nondefault port was used. Restore the snapshot afterward to retain data.

Automated checks:

```shell
cd server/backend
go test ./...
go test -race ./packages/presentation/discovery ./packages/application/settings ./packages/infrastructure/firewall
```

The UDP regression passes on Linux and is compiled for Windows. Wine cannot run the socket integration check in this environment (`wsaioctl`, error 10045); the limiter unit tests pass there, while actual Windows UDP behavior remains a VM check. Windows RPC tests pass under Wine and cover port validation, persistence and database-write failure. Windows Firewall application/enforcement and installer lifecycle require the VM checks.

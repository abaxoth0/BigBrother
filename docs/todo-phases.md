# TODO implementation phases

Each phase should be delivered on its own branch and tested in the Windows VM before the next phase is applied. Take a VM snapshot before testing. Do not combine installer or credential changes with the GUI-only first phase.

| Phase | Scope | VM acceptance checks | Status |
|---|---|---|---|
| 1 | Server GUI connectivity: separate backend reachability from service state; bounded pipe health checks; clear disconnected data and refresh on reconnect | Service and console-mode startup, shutdown, reconnect, and stalled/malformed pipe responses | Implemented; VM verified |
| 2 | Token rotation: invalidate persisted and in-memory stale tokens on explicit authentication rejection; bounded signed-challenge recovery; correct server GUI instructions | Rotate a connected client's token, reconnect without editing config, reject a client with the wrong identity key, and avoid retry loops | Implemented; awaiting VM verification |
| 3 | Certificate rotation: administrator-controlled inspection and verification of the replacement fingerprint before changing the pin | Reject an unexpected replacement certificate; accept a verified replacement; persist the new pin across restart | Planned |
| 4 | Windows secret permissions: explicit ACLs for existing/new server and client keys; review database/config token storage and installer integration | Elevated services/frontends still work; an ordinary user cannot read, replace, or modify protected secrets; reinstall preserves access | Planned |
| 5 | Discovery and deployment rules: throttle discovery by source IP; synchronize Windows Firewall rules with the configured TCP port; document router/proxy checks | Vary UDP source ports without bypassing the rate limit; configure a nondefault TCP port and reconnect; check firewall profiles and uninstall cleanup | Planned |

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

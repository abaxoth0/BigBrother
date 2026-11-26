# Client backend

BigBrother Client is the Windows service for BigBrother Client Daemon.exe and
the hub for client applications. It owns the server session, settings, policy delivery,
notifications, firewall controls, log access and update preparation.

~~~mermaid
flowchart LR
    GUI[Client frontend] <--> Hub[Client backend service]
    CLI[Client command line] <--> Hub
    Hub <--> Server[Server backend]
    Hub --> Firewall[Firewall service]
    Hub --> Monitor[Process monitor per interactive session]
    Hub --> Notifications[Notification helper]
    Hub --> Updates[Update preparation worker]
    Hub --> Logs[Client and firewall logs]
~~~

The frontend opens without starting or requiring the backend service. It shows
the disconnected state while the backend is stopped or unavailable and reconnects
when the backend becomes available. The main control panel has separate
Start/Stop/Restart controls for **Клиентская служба** (`BigBrother Client`) and
**Firewall**. Backend controls use Windows Service Control Manager and remain
available without IPC; firewall controls send requests through the running
backend. Operations disable conflicting controls until completion and status
refresh, with errors shown in the log. Command-line operations require a running
backend and use its pipe. Neither starting the backend nor opening the frontend
starts the firewall. Only explicit firewall Start/Restart requests start it. Filtration
toggles, settings, server policy, status checks and reconnects never launch it.
A stopped or crashed firewall stays stopped until another Start/Restart request.

The installer defaults the backend to Automatic, with a Manual option. The
firewall is always Manual. Its lifetime depends on BigBrother Client: it watches
the backend service state and holds a handle to that backend process. It exits on
backend stop or crash, even if the backend has already restarted. There is no SCM
dependency, so Windows can deliver a backend Stop request while the firewall is
running. The update preparation worker follows the same lifetime rule; the normal
frontend stays open and displays the disconnected state. Backend shutdown
explicitly stops the firewall and cancels and joins IPC handlers. Notification
processes belong to backend-owned jobs, and notification children watch their
parent. Restarting the backend closes its workers and leaves the firewall stopped.

Starting or restarting the firewall invalidates its cached policy state. When
offline with fallback disabled, the backend reapplies the empty whitelist before
refreshing filtration. Shutdown cancels unfinished pipe requests and unread
responses before joining their handlers.

The separately staged updater runs independently so it can stop and replace the
backend itself. It restores the previous backend and firewall running states
after installation or rollback. Restarting the firewall during an approved
update is an explicit maintenance operation.
Normal frontend startup checks for an interrupted update before opening the main
window, so recovery also works when the backend cannot start.

Use the full Client Setup installer once to migrate an installation where the
firewall launched the backend. It replaces both service definitions. The old
update executor cannot create this service layout; subsequent update payloads
work with the new backend service.
Setup also removes the earlier feature-branch service named BigBrother.Client
before registering BigBrother Client.

## Application process blacklist

The client backend fetches the server's process policy when its authenticated
subscription opens or receives a change event. A periodic refresh repairs a
missed event. A failed fetch retains the previous policy within that subscription.
An empty list disables enforcement. Ending the subscription or stopping the
backend releases its monitors. Reconnection starts with a fresh policy.

The backend copies each complete server response into a subscription-scoped shared
memory channel. Each running monitor opens a read-only view, validates the UTF-8
rules and switches to the new blacklist before its next scan. Invalid updates
leave the monitor's last valid rules in use. A nonempty update reaches existing
instances without restarting them.

The installed LocalSystem service launches `BigBrother Process Monitor.exe`
with the interactive user's non-elevated token and a separate Windows job for
each active console or RDP session. The monitor scans once a second and checks
the opened process's executable name, user, session, elevation and critical
status before terminating a match. The firewall and client frontend can be
closed. Rules stay in memory for the lifetime of the server subscription.

## Code

The backend uses ordinary C++20 headers and translation units, without C++
language modules. Both Debug and Release use -Wall -Wextra -std=c++20.
Shared logging/encoding utilities and vendored mbedTLS remain C libraries.
The C++ runtime is linked statically.

Hub routes commands to CommandModule implementations. FirewallModule owns
firewall commands, service operations and the firewall wire adapter. LogsModule
reads bounded binary chunks, reporting file identities and offsets to detect
replacement and truncation. UpdatesModule launches a preparation worker in the
requester's Windows session and restricts approved metadata to that process.
Server/session and configuration operations remain behind the backend pipe.

A future client application needs its adapter and command handler added as a
module in Hub. Its frontend should know only backend commands. There is no
generic application registration or plugin protocol. The firewall pipe accepts
only the current backend service process. The backend pipe retains its
privileged DACL and GUI/command-line executable checks.

The existing status, whitelist, reload, set, register, connect, disconnect,
refresh, change-name and whoami CLI commands now use the backend pipe.
start-firewall, stop-firewall and restart-firewall provide explicit controls.
The -d option runs a console backend for development; installed dependent
applications use the Windows service lifecycle.

## Verification

~~~sh
make -C app/client/process-monitor debug test-build
make -C app/client/backend debug regression-build lifecycle-test-build
make -C app/client/backend release
make -C app/firewall debug test-build lifetime-test-build
python3 app/client/backend/tests/run_tests.py
python3 app/client/backend/tests/run_tests.py --lifecycle-only
dotnet run --project app/client/frontend.tests/BigBrother.Client.Certificate.Tests.csproj
dotnet run --project app/client/updates.tests/BigBrother.Client.Updates.Tests.csproj
dotnet run --project lib/cs/BigBrother.Frontend.Shared.Tests/BigBrother.Frontend.Shared.Tests.csproj
~~~

Run `app/client/process-monitor/build/test.exe` for matcher tests. On native Windows,
run `enforcement-test.exe` beside the monitor in an interactive desktop session to
exercise real process termination and exceptions using two disposable test processes.
Under Wine, use `enforcement-test.exe --updates-only` to run the live-update
fixture with both test processes exempt. Full enforcement uses a Windows
critical-process query unavailable in Wine.

The lifecycle fixture creates services named BigBrother Client and BigBrother
Firewall. Run it only in an isolated Wine prefix with neither service already
installed. The default prefix is /tmp/bigbrother-client-tests-wine; WINEPREFIX
overrides it. It runs the real backend with a simulated firewall service,
verifies no implicit launch, exercises explicit controls and backend restart,
then checks normal-stop and process-termination cascades. Packet and TLS tests
remain separate. Actual WinDivert driver operation requires Windows.

The WPF regression project tests broker-backed history loading and UI
responsiveness alongside existing frontend checks, including offline service controls,
backend/firewall command routing and control-panel layout at supported font sizes.

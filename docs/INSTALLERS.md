# Building Windows installers on Linux

Install Wine with 64-bit Windows support, Python 3, curl, and coreutils. For a headless
machine, also install Xvfb (`xvfb-run`). Then install the pinned Inno Setup 7.1.0
compiler:

```sh
./install-inno-setup.sh
```

The script downloads the official release, checks its pinned SHA-256 checksum,
and silently installs it to `C:\InnoSetup` in a dedicated Wine prefix:
`${XDG_DATA_HOME:-$HOME/.local/share}/bigbrother/inno-setup/wine`.
It leaves your default Wine prefix alone. Set `INNO_WINEPREFIX` to use another
absolute prefix path; use the same setting for both installation and builds.
`INNO_ISCC` optionally selects an existing compiler by its absolute Linux path.
Wine and Inno Setup are build dependencies; target Windows computers need neither.

To build both installers from already published binaries:

```sh
./build-installers.sh
./build-installers.sh /path/to/payload /path/to/installers
```

The arguments are the payload directory (default: repository `dist`), installer
output directory (default: repository `dist/installers`), optional version
(must match repository `VERSION`), optional changelog version, and optional
changelog path (default: repository `CHANGELOG.md`). By default, notes come from
the matching release heading, or `Unreleased` if that release is absent. An explicit
changelog version must exist. Paths containing
spaces work, and scripts can be invoked from outside the repository.

All application binaries must be rebuilt for the version in `VERSION`. The compiler
checks their embedded product versions and rejects stale payloads. Build/publish
the frontends normally in Visual Studio in the Windows VM, then copy the published
files into `dist/{client,server}-frontend`. The shared MSBuild files read `VERSION`
automatically; no shell command or version edit in Visual Studio is needed.
See [VERSIONING.md](VERSIONING.md) for the release workflow.

The payload must contain:

- `BigBrother Firewall.exe`, `WinDivert.dll`, and `WinDivert64.sys`
- `BigBrother Client Daemon.exe` and `BigBrother Server Daemon.exe`
- `BigBrother Notifications.exe` (built by `make -C notifications`)
- `client-frontend/BigBrother Client.exe` (prebuilt self-contained single file)
- `server-frontend/BigBrother Server.exe` (prebuilt self-contained single file)

Missing required files fail the build. Each installer packages its prebuilt frontend
executable and these five required native WPF DLLs from the same frontend directory:

- `D3DCompiler_47_cor3.dll`
- `PenImc_cor3.dll`
- `PresentationNative_cor3.dll`
- `vcruntime140_cor3.dll`
- `wpfgfx_cor3.dll`

Missing native DLLs also fail the build. Installation replaces the managed `GUI`
directory with this payload to remove unrelated files from older packages.
The WinDivert development library is not installed on target machines.

To rebuild daemons and installers using the prebuilt frontend executables in
repository `dist/{client,server}-frontend`:

```sh
./publish.sh
./publish.sh /path/to/payload
```

The script does not rebuild frontends. With a custom payload directory it copies
the prebuilt executables and their five native DLLs from repository `dist` into
that directory. It places installers in `<payload>/installers` and fails if the
compiler is unavailable or either installer cannot compile. The outputs are:

- `BigBrother-<version>-Client-Setup.exe`
- `BigBrother-<version>-Server-Setup.exe`
- `BigBrother-<version>-Client-Update.exe` and its JSON manifest

Client update publication, local staging, elevation and rollback are described
in [CLIENT-UPDATES.md](CLIENT-UPDATES.md).

# Installation and upgrades

Both installers require administrator privileges and x64-compatible Windows.
They keep the existing default locations (`Program Files\BigBrother` for the
client, `Program Files\BigBrother Server` for the server), with frontends in `GUI`.
Desktop and Start menu shortcuts launch the corresponding frontend.

Both backends launch `BigBrother Notifications.exe` from their installation
directory to deliver Windows notifications independently of the frontends.
When running as a LocalSystem service, the helper starts a display process with
the signed-in user's token in each active console or Remote Desktop session.
The helper uses C++ and the Windows Runtime toast APIs, with separate
`BigBrother.Client` and `BigBrother.Server` identities. The installer puts the
AUMID and a stub toast activator CLSID on each Start menu shortcut and registers
its activation protocol and desktop notification identity. The helper also
registers the identity's display name and activator for the signed-in user,
including when an existing shortcut is reused. Toasts persist in Notification Center; clicking one
opens the corresponding GUI. Neither GUI needs to be running to send a toast.
It never displays UI as SYSTEM. Events without an active user session are not
replayed at logon. Windows notification settings and Do Not Disturb still apply.

For standalone builds, the helper automatically registers a per-user Start menu
shortcut and protocol when machine-wide registration is absent. Run
`"BigBrother Notifications.exe" --unregister Client` (or `Server`) as that user
to remove these standalone registrations before deleting or moving the build.
The helper and C API are built with MinGW C++17; the runtime is linked statically,
so no additional C++ runtime DLLs need to be shipped. The C backend links
`notifications/build/libbbnotify.a` through the shared `common/notify.h` interface.
The notification app lives in the top-level `notifications/` directory, with its
internal headers in `include/`, implementation files in `src/`, Windows resources
in `resources/`, tests in `tests/`, and generated files in `build/`.

Launching `BigBrother Notifications.exe` without arguments or with `--show`
sends a test toast using the Client identity. `--show Client`, `--show Server`
and `--test Client|Server` select the identity explicitly. These commands also
work when launched directly from Command Prompt. The helper attaches to the
parent console and prints its build time, executable path, selected identity,
Windows session, registration steps, submission result and asynchronous errors.
Tests in the interactive process wait up to five seconds for asynchronous errors. Notification
settings are read only as an optional diagnostic after submission.
When launched by double-click, a failed test shows an error dialog instead.
`--help` lists commands and `--version` prints build information.
The separate `tests.exe` validates payloads; it does not send notifications.

To select the identity explicitly on Windows, run this from the installation directory in PowerShell,
first as the signed-in user, then as SYSTEM to exercise service delivery.
To wait for completion in Command Prompt and inspect the exit status:

```bat
start "" /wait "BigBrother Notifications.exe" --show
echo %ERRORLEVEL%
```

In PowerShell:

```powershell
$test = Start-Process '.\BigBrother Notifications.exe' -ArgumentList '--test', 'Client' -Wait -PassThru
$test.ExitCode
```

Use `Server` for the server installation. Zero means that Windows accepted the
toast request, not that the user saw a banner. The helper reports nonzero exit
codes for failed session launches and WinRT delivery errors. Windows enforces
notification preferences when the toast is submitted; the helper does not
require an existing notification-settings record before submitting the first toast.
Client delivery errors go to the backend log; the server records the helper's
exit status and diagnostic output. Manual-test traces and all errors are also
written to `%LOCALAPPDATA%\BigBrother\notifications.log` with timestamps and
process IDs. The file rotates at approximately 64 KiB to `notifications.log.old`.
A manual service-context test with no active signed-in session reports an error
instead of reporting success without submitting anything.
Wine can verify transport, C integration, shortcut creation and protocol
registration, but cannot verify Windows toast display or read shortcut AUMID
properties. Run `notifications/build/tests.exe --registration` on Windows
for the complete registration checks. The registration fixture uses its own test
identity, protocol and shortcut; it does not replace the Client or Server registration.

Upgrades reuse the previous Inno installation directory, or the directory recorded
by the legacy NSIS uninstall entry. They stop and remove the existing service
before replacing binaries, then recreate the service with a quoted executable
path and manual startup. Secret permissions are secured before service startup;
failures in that step or service setup/startup fail installation. Legacy NSIS
uninstall registration and `uninstall.exe` are replaced by Inno's uninstaller.

The client retains its existing firewall-rule setup. The server backend continues
to synchronize firewall rules with its configured port on startup. Uninstall
stops/removes the corresponding service and removes its firewall rules.
Server uninstall removes its `data` and `logs` directories, including server data
in `ProgramData\BigBrother\data` when that storage location is used. Client
uninstall removes `config`, `logs`, `client_sigkey.key`, and `whitelist.txt` from
its installation directory. Cleanup also runs during silent uninstall.

Inno Setup compiler and download documentation:
[downloads](https://jrsoftware.org/isdl.php),
[download verification](https://jrsoftware.org/isdl-verify.php), and
[compiler options](https://jrsoftware.org/ishelp/topic_compilercmdline.htm).

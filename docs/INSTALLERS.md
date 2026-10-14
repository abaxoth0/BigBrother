# Building Windows installers on Linux

Install Wine with 64-bit Windows support, curl, and coreutils. For a headless
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
./build-installers.sh /path/to/payload /path/to/installers 1.2.3
```

The arguments are the payload directory (default: repository `dist`), installer
output directory (default: repository `dist/installers`), and version (default:
`1.0.0`). Three or four numeric version components are accepted. Paths containing
spaces work, and scripts can be invoked from outside the repository.

The payload must contain:

- `BigBrother Firewall.exe`, `WinDivert.dll`, and `WinDivert64.sys`
- `BigBrother Client Daemon.exe` and `BigBrother Server Daemon.exe`
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

# Installation and upgrades

Both installers require administrator privileges and x64-compatible Windows.
They keep the existing default locations (`Program Files\BigBrother` for the
client, `Program Files\BigBrother Server` for the server), with frontends in `GUI`.
Desktop and Start menu shortcuts launch the corresponding frontend.

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

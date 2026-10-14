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
- `client-frontend/BigBrother Client.exe` and its published dependencies
- `server-frontend/BigBrother Server.exe` and its published dependencies

Missing required files fail the build. Frontends must be published self-contained
for `win-x64`. The installers include their published trees except `.pdb` files.
The WinDivert development library is not installed on target machines.

For a complete publish of daemons, frontends, and installers:

```sh
./publish.sh
./publish.sh /path/to/payload
```

The full publish places installers in `<payload>/installers` and fails if the
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
Application-created data and configuration are retained; the uninstaller removes
only installer-managed files. Silent installs and uninstalls do not delete data.

Inno Setup compiler and download documentation:
[downloads](https://jrsoftware.org/isdl.php),
[download verification](https://jrsoftware.org/isdl-verify.php), and
[compiler options](https://jrsoftware.org/ishelp/topic_compilercmdline.htm).

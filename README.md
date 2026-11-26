# BigBrother

| Directory | Contents |
| --- | --- |
| `app/` | Client and server frontends and backends, firewall, process monitor, and notification helper |
| `lib/` | Shared native code in `common/`, shared .NET projects in `cs/`, and third-party libraries |
| `scripts/` | Build, publish, installer, diagnostic, and development scripts |
| `installer/` | Inno Setup definitions for client setup, server setup, and client updates |
| `config/` | Project-wide Doxygen configuration |
| `assets/` | Application icons |
| `docs/` | Build instructions, protocol references, and usage guides |

From the repository root, run `scripts/build.sh Debug` to build the daemons, process monitor and notification helper into `dist/`.
For frontend publishing and release packaging, see the [versioning guide](docs/VERSIONING.md) and [installer guide](docs/INSTALLERS.md).

Administrators can manage a process blacklist under **Списки → Приложения → Чёрный список**. Connected clients enforce the saved rules in their users’ desktop sessions independently of network filtration. [Usage guide](docs/guide/usage-ru.md#sec-server-wls)

The standalone Windows client app builds with `make -C app/process-monitor release` and produces `BigBrother Process Monitor.exe`. It reads policies delivered by the client backend and owns process matching and termination.

# BigBrother

| Directory | Contents |
| --- | --- |
| `app/` | Client and server frontends and backends, firewall, and notification helper |
| `lib/` | Shared native code in `common/`, shared .NET projects in `cs/`, and third-party libraries |
| `scripts/` | Build, publish, installer, diagnostic, and development scripts |
| `installer/` | Inno Setup definitions for client setup, server setup, and client updates |
| `config/` | Project-wide Doxygen configuration |
| `assets/` | Application icons |
| `docs/` | Build instructions, protocol references, and usage guides |

From the repository root, run `scripts/build.sh Debug` to build the daemons and notification helper into `dist/`.
For frontend publishing and release packaging, see the [versioning guide](docs/VERSIONING.md) and [installer guide](docs/INSTALLERS.md).

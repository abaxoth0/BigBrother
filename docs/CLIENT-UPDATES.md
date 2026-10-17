# Client updates from a shared directory

The server publishes a package location; it does not configure a Windows share,
copy the package, or serve the executable. The source can be a UNC path or a
mapped drive, for example `Z:\shared\tmp\BigBrother Updater.exe`. The same path
must resolve on every target client. Clients only execute their verified local
copies. UNC paths, mapped network drives, junctions and symbolic links are
rejected for the installation and local staging locations.

## Build and publish

Build/publish both updated frontends first; `publish.sh` continues to use the
prebuilt self-contained frontends in `dist`, as described in `INSTALLERS.md`.
Then run:

```sh
./build-installers.sh /path/to/payload /path/to/output 1.4.0 Unreleased
```

The fourth argument selects the CHANGELOG.md section for release notes; it
otherwise defaults to the package version. Use `1.3.0` to read `## [1.3.0]`, or
`Unreleased` for development packages. A missing version fails the build. An
optional fifth argument selects another changelog file. For example:

```sh
./build-installers.sh /path/to/payload /path/to/output 1.3.0 1.3.0 /path/to/CHANGELOG.md
```

Added, Removed, Changed, Fixed, Deprecated and Security bullets become structured
`release_notes` in the JSON manifest. Wrapped lines and nested bullets are
preserved; common inline Markdown is converted to plain text. The server retains
these notes when publishing, and the client displays translated section headings.
Older manifests without notes still work and show that no description was supplied.

Alongside the two normal installers this produces:

- `BigBrother-1.4.0-Client-Update.exe`
- `BigBrother-1.4.0-Client-Update.json`

The update EXE includes all client executables, native WPF DLLs, WinDivert files,
and `client-version.txt`. Its Inno instructions extract this fixed payload into
local staging; it does not change services, registry entries or uninstall data.
The independent updater handles replacement and recovery. This package requires
the updater-capable client installed by the normal installer; older clients need
that initial manual upgrade.

Copy the EXE and JSON to the already configured shared directory. They must
share the same basename; renaming them both is supported. In server settings,
click **Обзор…** under **Обновления клиентов**, select the EXE, then click
**Опубликовать**. The path can still be edited when needed.
The server frontend reads the adjacent JSON and checks the executable's size
and SHA-256 before publishing the location, version, size and hash. The server
service never needs access to the mapped drive. Metadata is saved through the
administrator frontend pipe and delivered to authenticated clients over pinned
TLS. Remote clients cannot publish an update. **Убрать обновление** stops
advertising the package without deleting shared files.

Both the server frontend and client frontend run elevated. Drive mappings must
therefore exist in their elevated sessions; a mapping present only in an
unelevated Explorer session may be unavailable. A UNC path avoids that mapping
dependency. Normal share permissions and network access still apply.

## Installation and recovery

Open the client's **Обновления** tab and click **Проверить обновления**. This
loads metadata and release notes without copying or executing the shared package.
The tab shows installed/available versions and the selected changelog sections.
Click **Установить обновление** to copy and install the reviewed package. The
client rechecks its version, hash, size and location before copying; if the server
changed the package, check again to review the current release. Background update
polling, automatic installation and backend update announcements are disabled.
Recovery of an already interrupted installation still runs at startup.

The update runs with required administrator rights and an explicit `runas`
launch. Windows may reuse elevation when the launching GUI is already elevated.
No SYSTEM service launches an installer to bypass user elevation.

The client copies the package into a unique directory below
`%ProgramData%\BigBrotherClientUpdates`, verifies its size and SHA-256, and runs
an independent copy of the frontend as the updater. Protected DACLs and
Administrators ownership restrict staged packages, runners, journals and backups
to SYSTEM/Administrators. Insecure pre-existing staging paths are rejected.
The package runs with a protected local temporary directory and cannot reboot
the machine automatically.

After extraction and payload validation, the updater closes installed client
processes, stops the firewall service, and backs up the entire installation,
including configuration, identity keys and whitelist. Backups are flushed and
hashed before the journal permits any replacement. It saves file/directory
permissions, ownership, attributes and last-write times. Unchanged files, including unchanged driver files, are left in place. Changed
installed files are replaced through local temporary files; original security is applied before
publishing each replacement. New executables receive local read/execute rights.
The service must restart and its daemon must answer the local status request
before the update is marked complete. Its previous running/stopped state is
preserved.

On failure, the updater verifies the full backup, restores the old files and
configuration, removes files created by the failed version, restores security,
and returns the service to its previous state. Retrying requires another
explicit check and installation action; failed packages are never retried in
the background.

Journals survive termination/reboot. The next GUI launch starts the retained
runner to finish recovery. If the installed GUI cannot start, run the retained
copy from an elevated PowerShell session, substituting the transaction ID:

```powershell
$work = "$env:ProgramData\BigBrotherClientUpdates\<transaction-id>"
& "$work\runner\BigBrother Client.exe" --apply-update $work
```

Backups and journals are retained after success or failure. Administrators may
remove finished (`complete`, `restored`, `failed`) transaction directories after
reviewing the result. Keep pending (`backing-up`, `applying`, `restoring`)
transactions until recovery finishes. If restoration itself fails, its journal
and backups remain pending and the progress window reports their location;
disk or permission failures cannot guarantee automatic recovery.

## Validation

```sh
python3 -m unittest discover -s scripts/tests -p '*_test.py'
dotnet run --project client/updates.tests/BigBrother.Client.Updates.Tests.csproj
cd server/backend
go test ./...
```

The parser and portable tests exercise release selection, structured Unicode
notes, explicit checks without installation, stale-state rejection, partial
replacement, configuration changes, repeated recovery, damaged backup rejection,
metadata and unsafe paths. Windows
RPC tests also cover publication permissions, authentication, invalid metadata,
write failure and withdrawal. The installer can be compiled and extraction tested
under Wine. Real Windows validation is still needed for UAC, DACL enforcement,
mapped drives, locked files, service restart/rollback, interruption and WinDivert
driver replacement. An in-use driver that cannot be replaced causes rollback;
the updater never schedules untracked replacements for reboot.

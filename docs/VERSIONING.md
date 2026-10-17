# Common application version

The repository root `VERSION` contains the common release version, currently
`2.0.0`. Use three numeric components, such as `2.1.0`, without quotes or a `v`
prefix. Each component must be between 0 and 65534. Commit this file with the
release changes and rebuild all applications after changing it.

The version is embedded at build time. Installed applications do not need access
to the repository or a separate `VERSION` file at runtime. WinDivert and the
native WPF dependencies retain their own upstream versions.

## Visual Studio in the Windows VM

Keep `VERSION`, `Directory.Build.props` and `Directory.Build.targets` in the
repository root above the frontend projects. Open either existing solution in
Visual Studio and use **Build**, **Rebuild** or **Publish** normally. MSBuild reads
the common version for both frontends and their shared library; Python, Make and
Linux build scripts are not required for this step. The version file is also an
input to Visual Studio's up-to-date check, so editing it triggers a new build.

Both window titles show the version. The client Updates tab uses the running
frontend's embedded version. Assembly and Windows file versions use four
components (`2.0.0.0`), while the informational/product version remains `2.0.0`.
Remove any separate version overrides from project or publish settings.

Publish both frontends as self-contained Windows x64 single files using the
existing profiles. Copy the executable and required native WPF DLLs from each
publish directory into `dist/client-frontend` or `dist/server-frontend` before
running `publish.sh` or `build-installers.sh` on Linux.

## Native and Go builds

`build.sh` builds all daemons and the notification helper with the common version.
Individual builds also work:

```sh
make -C firewall
make -C notifications
make -C client/backend
make -C server/backend
```

The native Makefiles validate `VERSION` with Python 3, pass `BB_VERSION` to the
C/C++ compiler and embed Windows version resources. The Go Makefile generates
the same resources and passes `-ldflags '-X main.productVersion=2.0.0'` with the
actual version read from the file. A bare `go build` without this linker flag
reports `development`; use the Makefile for release binaries.

All four native/Go applications support `--version` without starting their
normal work. Windows Explorer's executable properties also show the product
version for all six applications.

## Installers and update manifests

```sh
./build-installers.sh /path/to/payload /path/to/installers
```

Installers and update manifests read `VERSION` automatically. Inno Setup checks
the embedded product version of every BigBrother executable in the payload and
fails when a binary is stale, including a frontend copied from the VM. The
optional legacy version argument must match `VERSION`.

Release notes use the matching CHANGELOG.md release heading when present,
otherwise `Unreleased`. To select notes explicitly, pass the common version and
the desired changelog heading as the third and fourth arguments. See
[INSTALLERS.md](INSTALLERS.md) and [CLIENT-UPDATES.md](CLIENT-UPDATES.md).

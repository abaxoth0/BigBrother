# Native code maintenance

The first-party C/C++ code lives in `firewall`, `client/backend`, `notifications`
and `common`. Third-party code under `lib` and `firewall/include/third-party`
keeps its upstream style.

## clangd

From a Linux development environment with Python 3, Make and the MinGW-w64 GCC
C/C++ toolchain on `PATH`, generate the editor compilation database:

```sh
python3 scripts/compile_commands.py
```

The root `.clangd` uses this database for the client backend, firewall,
notifications and shared native code. The generator reads debug build and test
commands from Make dry runs, including each target's language standard, include
paths, Windows defines and the current `VERSION`. It queries the installed MinGW
compilers for their system headers, so no checkout path, GCC version or editor
`--query-driver` option needs to be hardcoded. It does not compile the applications.
Third-party translation units are excluded so their build flags cannot be
mistakenly inferred for similarly named project headers such as `version.h`.

`compile_commands.json` contains local absolute paths and is ignored by Git.
Regenerate it after changing build flags, source lists, `VERSION`, the toolchain
or the checkout location, then restart clangd if diagnostics have not refreshed.
Run the generator once on each new checkout before opening native files.

Verify the setup with:

```sh
python3 -m unittest discover -s scripts/tests -p 'compile_commands_test.py'
clangd --check=client/backend/src/ipc_client.cpp --tweaks=none
clangd --check=firewall/src/firewall.c --tweaks=none
```

The `--tweaks=none` option skips clangd's refactoring self-tests while retaining
compiler diagnostics, indexing and other checks.

## Readability

Use the root `.clang-format` for files being cleaned up (clang-format 15 or newer,
which supports `InsertBraces`). It uses four-space indentation, attached braces,
explicit control-flow blocks and a 110-column limit while preserving include order.
Apply formatting to the files you change, keeping unrelated formatting out of
functional changes:

```sh
clang-format -i firewall/src/allowlist.c
clang-format --dry-run --Werror firewall/src/allowlist.c
```

Separate validation, parsing, resource acquisition and result construction with
blank lines. Review these boundaries manually: the formatter does not identify
logical groups. Keep an operation and its error check together, then leave a
blank line before the next step. For example, TLV readers separate the command,
field length, payload, separator and argument accumulation. Keep short functions
that perform one operation compact.

Extract a helper when a function mixes distinct operations, as with
port-expression parsing in `allowlist.c`. Keep ownership and failure cleanup easy
to trace; formatting changes should preserve behavior.

## Doxygen

Document shared interfaces at their declarations. Include return/error values,
buffer sizes, ownership, byte order and synchronization requirements where they
matter. Document internal helpers when they encode a non-obvious contract; avoid
repeating the declaration in prose. Add `@file` documentation so Doxygen includes
free functions. Keep implementation comments focused on reasons and invariants.

Generate the combined native API reference from the repository root:

```sh
mkdir -p build/native-docs
doxygen Doxyfile
```

Open `build/native-docs/html/index.html`. The root configuration includes only
first-party native code and validates documentation syntax. Undocumented legacy
symbols do not fail generation; this is not a documentation-coverage check. The
existing `firewall/Doxyfile` remains available for firewall-only documentation.

## Verification

Build the affected native modules and their existing regression executables:

```sh
make -C firewall debug test-build lifetime-test-build
make -C client/backend debug regression-build lifecycle-test-build
make -C notifications debug test-build
python3 client/backend/tests/run_tests.py
python3 client/backend/tests/run_tests.py --lifecycle-only
```

The Python runner executes the backend, firewall and live TLS suites on Windows
or Wine. The lifecycle fixture requires an isolated Wine prefix. Run
`notifications/build/tests.exe` separately on Windows or with Wine; native Windows
is needed to verify actual toast delivery and driver-backed packet interception.

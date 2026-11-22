# Native code maintenance

The first-party C/C++ code lives in `firewall`, `client/backend`, `notifications`
and `common`. Third-party code under `lib` and `firewall/include/third-party`
keeps its upstream style.

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

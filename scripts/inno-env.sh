#!/usr/bin/env bash

export WINEPREFIX="${INNO_WINEPREFIX:-${XDG_DATA_HOME:-$HOME/.local/share}/bigbrother/inno-setup/wine}"
if [[ "$WINEPREFIX" != /* ]]; then
    echo "INNO_WINEPREFIX must be an absolute path." >&2
    exit 1
fi
export WINEDEBUG="${WINEDEBUG:--all}"
INNO_ISCC="${INNO_ISCC:-$WINEPREFIX/drive_c/InnoSetup/ISCC.exe}"
INNO_WINE_RUNNER=(wine)

for tool in wine winepath; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing tool: $tool" >&2
        exit 1
    fi
done

if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "No DISPLAY available; install Xvfb (xvfb-run) for headless Wine." >&2
        exit 1
    fi
    INNO_WINE_RUNNER=(xvfb-run -a wine)
fi

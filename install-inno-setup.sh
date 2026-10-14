#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/scripts/inno-env.sh"

VERSION=7.1.0
SHA256=0362a383ed217d4c4239b5933866dd96d3eb2102737da92f80f6057a4b40df2f
DOWNLOAD_DIR="$(mktemp -d)"
trap 'rm -rf -- "$DOWNLOAD_DIR"' EXIT
INSTALLER="$DOWNLOAD_DIR/innosetup-$VERSION-x64.exe"

curl --fail --location --retry 3 \
    "https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-$VERSION-x64.exe" \
    --output "$INSTALLER"
if ! printf '%s  %s\n' "$SHA256" "$INSTALLER" | sha256sum --check --status; then
    echo "Inno Setup download failed SHA-256 verification." >&2
    exit 1
fi
mkdir -p "$WINEPREFIX"
"${INNO_WINE_RUNNER[@]}" "$INSTALLER" /SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART \
    /CURRENTUSER /PORTABLE=1 /NOICONS /TASKS= '/DIR=C:\InnoSetup'

INSTALLED_COMPILER="$WINEPREFIX/drive_c/InnoSetup/ISCC.exe"
if [[ ! -f "$INSTALLED_COMPILER" ]]; then
    echo "Compiler not found after installation: $INSTALLED_COMPILER" >&2
    exit 1
fi
"${INNO_WINE_RUNNER[@]}" "$INSTALLED_COMPILER" --version
echo "Installed Inno Setup in $WINEPREFIX"

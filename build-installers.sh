#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" == --help ]]; then
    echo "Usage: $0 [payload-dir [installer-output-dir [version]]]"
    echo "Defaults: $SCRIPT_DIR/dist, $SCRIPT_DIR/dist/installers, 1.0.0"
    exit 0
fi
if [[ $# -gt 3 ]]; then
    echo "Too many arguments; use --help for usage." >&2
    exit 2
fi
source "$SCRIPT_DIR/scripts/inno-env.sh"
source "$SCRIPT_DIR/scripts/frontend-files.sh"
PAYLOAD_DIR="$(realpath -e "${1:-$SCRIPT_DIR/dist}")"
INSTALLER_OUTPUT_DIR="${2:-$SCRIPT_DIR/dist/installers}"
VERSION="${3:-1.0.0}"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
    echo "Version must contain three or four numeric components." >&2
    exit 1
fi
if [[ ! -f "$INNO_ISCC" ]]; then
    echo "Inno Setup compiler not found: $INNO_ISCC" >&2
    echo "Run $SCRIPT_DIR/install-inno-setup.sh first." >&2
    exit 1
fi
for file in 'BigBrother Firewall.exe' 'BigBrother Client Daemon.exe' \
    'BigBrother Server Daemon.exe' WinDivert.dll WinDivert64.sys \
    'client-frontend/BigBrother Client.exe' 'server-frontend/BigBrother Server.exe'; do
    if [[ ! -f "$PAYLOAD_DIR/$file" ]]; then
        echo "Missing installer payload: $PAYLOAD_DIR/$file" >&2
        exit 1
    fi
done
for frontend in client-frontend server-frontend; do
    for file in "${FRONTEND_NATIVE_DLLS[@]}"; do
        if [[ ! -f "$PAYLOAD_DIR/$frontend/$file" ]]; then
            echo "Missing installer payload: $PAYLOAD_DIR/$frontend/$file" >&2
            exit 1
        fi
    done
done
mkdir -p "$INSTALLER_OUTPUT_DIR"
INSTALLER_OUTPUT_DIR="$(realpath -e "$INSTALLER_OUTPUT_DIR")"
PAYLOAD_WINDOWS="$(winepath -w "$PAYLOAD_DIR")"
OUTPUT_WINDOWS="$(winepath -w "$INSTALLER_OUTPUT_DIR")"
for product in Client Server; do
    SCRIPT_WINDOWS="$(winepath -w "$SCRIPT_DIR/BigBrother-$product.iss")"
    "${INNO_WINE_RUNNER[@]}" "$INNO_ISCC" --quiet-progress \
        "--define=PayloadDir=$PAYLOAD_WINDOWS" "--define=ProductVersion=$VERSION" \
        "--output-dir=$OUTPUT_WINDOWS" "$SCRIPT_WINDOWS"
done
echo "Installers built in $INSTALLER_OUTPUT_DIR"

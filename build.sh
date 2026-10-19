#!/bin/bash
# Build script for BigBrother firewall system
# Usage: ./build.sh <Debug|Release> [output-dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ "${1:-}" == --help ]]; then
    echo "Usage: $0 <Debug|Release> [output-dir]"
    exit 0
fi
source "$SCRIPT_DIR/scripts/build-configuration.sh"
if [[ $# -gt 2 ]]; then
    echo "Too many arguments; use --help for usage." >&2
    exit 2
fi
OUTPUT_DIR="${2:-$SCRIPT_DIR/dist}"
BUILD_JOBS="$(nproc)"
python3 "$SCRIPT_DIR/scripts/version.py"

echo "Building BigBrother..."
echo "Configuration: $BUILD_CONFIGURATION"
echo "Output directory: $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath -e "$OUTPUT_DIR")"
: > "$OUTPUT_DIR/build-configuration.txt"

echo "Building firewall daemon..."
make -C "$SCRIPT_DIR/firewall" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$SCRIPT_DIR/firewall/build/BigBrother Firewall.exe" "$OUTPUT_DIR/"

echo "Building notification helper..."
make -C "$SCRIPT_DIR/notifications" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$SCRIPT_DIR/notifications/build/BigBrother Notifications.exe" "$OUTPUT_DIR/"

echo "Building client backend..."
make -C "$SCRIPT_DIR/client/backend" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$SCRIPT_DIR/client/backend/build/BigBrother Client Daemon.exe" "$OUTPUT_DIR/"

echo "Building server backend..."
make -C "$SCRIPT_DIR/server/backend" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$SCRIPT_DIR/server/backend/build/BigBrother Server Daemon.exe" "$OUTPUT_DIR/"

WD_DIR="$SCRIPT_DIR/firewall/include/third-party/WinDivert-2.2.2-A"
if [ -d "$WD_DIR/x64" ]; then
    cp "$WD_DIR/x64/WinDivert.dll"  "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert64.sys" "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert.lib"   "$OUTPUT_DIR/"
fi

printf '%s\n' "$BUILD_CONFIGURATION" > "$OUTPUT_DIR/build-configuration.txt"
echo "Build complete!"
ls -la "$OUTPUT_DIR"

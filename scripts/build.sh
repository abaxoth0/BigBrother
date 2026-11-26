#!/bin/bash
# Build script for BigBrother firewall system
# Usage: ./scripts/build.sh <Debug|Release> [output-dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
if [[ "${1:-}" == --help ]]; then
    echo "Usage: $0 <Debug|Release> [output-dir]"
    exit 0
fi
source "$SCRIPT_DIR/build-configuration.sh"
if [[ $# -gt 2 ]]; then
    echo "Too many arguments; use --help for usage." >&2
    exit 2
fi
OUTPUT_DIR="${2:-$REPO_ROOT/dist}"
BUILD_JOBS="$(nproc)"
python3 "$SCRIPT_DIR/version.py"

echo "Building BigBrother..."
echo "Configuration: $BUILD_CONFIGURATION"
echo "Output directory: $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath -e "$OUTPUT_DIR")"
: > "$OUTPUT_DIR/build-configuration.txt"

echo "Building firewall daemon..."
make -C "$REPO_ROOT/app/firewall" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$REPO_ROOT/app/firewall/build/BigBrother Firewall.exe" "$OUTPUT_DIR/"

echo "Building notification helper..."
make -C "$REPO_ROOT/app/notifications" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$REPO_ROOT/app/notifications/build/BigBrother Notifications.exe" "$OUTPUT_DIR/"

echo "Building process monitor..."
make -C "$REPO_ROOT/app/process-monitor" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$REPO_ROOT/app/process-monitor/build/BigBrother Process Monitor.exe" "$OUTPUT_DIR/"

echo "Building client backend..."
make -C "$REPO_ROOT/app/client/backend" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$REPO_ROOT/app/client/backend/build/BigBrother Client Daemon.exe" "$OUTPUT_DIR/"

echo "Building server backend..."
make -C "$REPO_ROOT/app/server/backend" -j"$BUILD_JOBS" "$BUILD_MODE"
cp "$REPO_ROOT/app/server/backend/build/BigBrother Server Daemon.exe" "$OUTPUT_DIR/"

WD_DIR="$REPO_ROOT/app/firewall/include/third-party/WinDivert-2.2.2-A"
if [ -d "$WD_DIR/x64" ]; then
    cp "$WD_DIR/x64/WinDivert.dll"  "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert64.sys" "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert.lib"   "$OUTPUT_DIR/"
fi

printf '%s\n' "$BUILD_CONFIGURATION" > "$OUTPUT_DIR/build-configuration.txt"
echo "Build complete!"
ls -la "$OUTPUT_DIR"

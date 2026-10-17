#!/bin/bash
# Build script for BigBrother firewall system
# Usage: ./build.sh [output-dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${1:-$SCRIPT_DIR/dist}"
python3 "$SCRIPT_DIR/scripts/version.py"

echo "Building BigBrother..."
echo "Output directory: $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath -e "$OUTPUT_DIR")"

echo "Building firewall daemon..."
cd "$SCRIPT_DIR/firewall" && make clean && make
cp build/*.exe "$OUTPUT_DIR/"

echo "Building notification helper..."
make -C "$SCRIPT_DIR/notifications"
cp "$SCRIPT_DIR/notifications/build/BigBrother Notifications.exe" "$OUTPUT_DIR/"

echo "Building client backend..."
cd "$SCRIPT_DIR/client/backend" && make clean && make
cp build/*.exe "$OUTPUT_DIR/"

echo "Building server backend..."
make -C "$SCRIPT_DIR/server/backend"
cp "$SCRIPT_DIR/server/backend/build/BigBrother Server Daemon.exe" "$OUTPUT_DIR/"

WD_DIR="$SCRIPT_DIR/firewall/include/third-party/WinDivert-2.2.2-A"
if [ -d "$WD_DIR/x64" ]; then
    cp "$WD_DIR/x64/WinDivert.dll"  "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert64.sys" "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert.lib"   "$OUTPUT_DIR/"
fi

echo "Build complete!"
ls -la "$OUTPUT_DIR"

#!/bin/bash
# Build script for BigBrother firewall system
# Usage: ./build.sh [output-dir]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${1:-./dist}"

echo "Building BigBrother..."
echo "Output directory: $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"

echo "Building firewall daemon..."
cd "$SCRIPT_DIR/daemon/firewall" && make clean && make
cp build/*.exe "$OUTPUT_DIR/"

echo "Building client backend..."
cd "$SCRIPT_DIR/daemon/client/backend" && make clean && make
cp build/*.exe "$OUTPUT_DIR/"

echo "Building server backend..."
cd "$SCRIPT_DIR/server/backend"
CGO_ENABLED=1 CC=clang GOOS=windows GOARCH=amd64 go build -o "$OUTPUT_DIR/BigBrother Server Daemon.exe" ./cmd

WD_DIR="$SCRIPT_DIR/daemon/firewall/include/third-party/WinDivert-2.2.2-A"
if [ -d "$WD_DIR/x64" ]; then
    cp "$WD_DIR/x64/WinDivert.dll"  "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert64.sys" "$OUTPUT_DIR/"
    cp "$WD_DIR/x64/WinDivert.lib"   "$OUTPUT_DIR/"
fi

echo "Build complete!"
ls -la "$OUTPUT_DIR"

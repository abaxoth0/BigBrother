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
cd "$SCRIPT_DIR/firewall" && make clean && make
cp build/firewall-service.exe "$OUTPUT_DIR/"

echo "Building client backend..."
cd "$SCRIPT_DIR/client/backend" && make clean && make
cp build/bb-client.exe "$OUTPUT_DIR/"

echo "Building server backend..."
cd "$SCRIPT_DIR/server/backend"
GOOS=windows GOARCH=amd64 go build -o "$OUTPUT_DIR/bb-server.exe" ./cmd

if [ -f "$SCRIPT_DIR/firewall/include/third-party/WinDivert-2.2.2-A/x64/WinDivert.dll" ]; then
    cp "$SCRIPT_DIR/firewall/include/third-party/WinDivert-2.2.2-A/x64/WinDivert.dll" "$OUTPUT_DIR/"
fi

echo "Build complete!"
ls -la "$OUTPUT_DIR"

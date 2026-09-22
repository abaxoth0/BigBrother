#!/bin/bash
# One-shot publish: builds all daemons + frontends, then builds the NSIS installers.
# Usage: ./publish.sh [output-dir]
#   output-dir defaults to ./dist
#
# Frontends are published self-contained (win-x64) because the installers do not
# install the .NET Desktop runtime on the target machine.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${1:-$SCRIPT_DIR/dist}"
RID="win-x64"
CONFIG="Release"

echo "=== BigBrother publish ==="
echo "Repo:      $SCRIPT_DIR"
echo "Output:    $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"

# 1) Daemons + firewall (C/go) — build.sh puts them straight into $OUTPUT_DIR
echo "--- Building daemons ---"
"$SCRIPT_DIR/build.sh" "$OUTPUT_DIR"

# 2) Frontends — self-contained win-x64, into dist\{client,server}-frontend
echo "--- Publishing client frontend ---"
dotnet publish "$SCRIPT_DIR/client/frontend/BigBrother Client.csproj" \
    -c "$CONFIG" -r "$RID" --self-contained true \
    -p:EnableWindowsTargeting=true \
    -o "$OUTPUT_DIR/client-frontend"

echo "--- Publishing server frontend ---"
dotnet publish "$SCRIPT_DIR/server/frontend/BigBrother Server.csproj" \
    -c "$CONFIG" -r "$RID" --self-contained true \
    -p:EnableWindowsTargeting=true \
    -o "$OUTPUT_DIR/server-frontend"

# 3) Installers (relative to the repo root, where the .nsi live)
if command -v makensis >/dev/null 2>&1; then
    echo "--- Building installers ---"
    (
        cd "$SCRIPT_DIR"
        makensis "$SCRIPT_DIR/BigBrother-Client.nsi"
        makensis "$SCRIPT_DIR/BigBrother-Server.nsi"
    )
else
    echo "WARNING: makensis not found — skipping installer build."
fi

echo "=== Publish complete ==="
ls -la "$OUTPUT_DIR"
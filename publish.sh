#!/bin/bash
# Build daemons and installers using the prebuilt frontend executables in dist.
# Usage: ./publish.sh <Debug|Release> [output-dir]
#   output-dir defaults to ./dist
#
# Frontends in dist contain prebuilt executables and their native WPF DLLs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
source "$SCRIPT_DIR/scripts/frontend-files.sh"
python3 "$SCRIPT_DIR/scripts/version.py"

echo "=== BigBrother publish ==="
echo "Repo:      $SCRIPT_DIR"
echo "Output:    $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath -e "$OUTPUT_DIR")"

# 1) Daemons + firewall (C/go) — build.sh puts them straight into $OUTPUT_DIR
echo "--- Building daemons ---"
"$SCRIPT_DIR/build.sh" "$BUILD_CONFIGURATION" "$OUTPUT_DIR"

echo "--- Using prebuilt frontends ---"
for product in Client Server; do
    frontend_dir="${product,,}-frontend"
    mkdir -p "$OUTPUT_DIR/$frontend_dir"
    for file in "BigBrother $product.exe" "${FRONTEND_NATIVE_DLLS[@]}"; do
        source_file="$SCRIPT_DIR/dist/$frontend_dir/$file"
        if [[ ! -f "$source_file" ]]; then
            echo "Missing prebuilt frontend file: $source_file" >&2
            exit 1
        fi
        target_file="$OUTPUT_DIR/$frontend_dir/$file"
        if [[ "$source_file" != "$target_file" ]]; then
            cp "$source_file" "$target_file"
        fi
    done
done

echo "--- Building installers ---"
"$SCRIPT_DIR/build-installers.sh" "$BUILD_CONFIGURATION" "$OUTPUT_DIR" "$OUTPUT_DIR/installers"

echo "=== Publish complete ==="
ls -la "$OUTPUT_DIR"

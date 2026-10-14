#!/bin/bash
# Build daemons and installers using the prebuilt frontend executables in dist.
# Usage: ./publish.sh [output-dir]
#   output-dir defaults to ./dist
#
# Frontends in dist contain prebuilt executables and their native WPF DLLs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${1:-$SCRIPT_DIR/dist}"
source "$SCRIPT_DIR/scripts/frontend-files.sh"

echo "=== BigBrother publish ==="
echo "Repo:      $SCRIPT_DIR"
echo "Output:    $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath -e "$OUTPUT_DIR")"

# 1) Daemons + firewall (C/go) — build.sh puts them straight into $OUTPUT_DIR
echo "--- Building daemons ---"
"$SCRIPT_DIR/build.sh" "$OUTPUT_DIR"

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
"$SCRIPT_DIR/build-installers.sh" "$OUTPUT_DIR" "$OUTPUT_DIR/installers"

echo "=== Publish complete ==="
ls -la "$OUTPUT_DIR"

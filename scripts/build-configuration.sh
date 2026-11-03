#!/usr/bin/env bash

case "${1:-}" in
    Debug) BUILD_CONFIGURATION=Debug; BUILD_MODE=debug ;;
    Release) BUILD_CONFIGURATION=Release; BUILD_MODE=release ;;
    *)
        echo "A build configuration is required: Debug or Release. Use --help for usage." >&2
        exit 2
        ;;
esac

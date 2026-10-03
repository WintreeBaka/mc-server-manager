#!/usr/bin/env bash
# Builds the whole project (backend CLI + desktop app) on Linux / macOS.
#
#   ./scripts/build-unix.sh [QtPrefix] [BuildDir]
#
set -euo pipefail

QT_PREFIX="${1:-}"
BUILD_DIR="${2:-build}"

configure_args=(-S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release)
if [[ -n "$QT_PREFIX" ]]; then
    configure_args+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
fi

echo "==> configuring"
cmake "${configure_args[@]}"

echo "==> building"
cmake --build "$BUILD_DIR" --parallel

echo
echo "done:"
echo "  backend : $BUILD_DIR/bin/mcsm-cli"
echo "  desktop : $BUILD_DIR/bin/McServerManager"

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="$SCRIPT_DIR/build/cpp/lili-gui/lili-gui"

if [ ! -f "$BINARY" ]; then
    echo "Building lili-gui..."
    cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build" -DCMAKE_BUILD_TYPE=Debug
    cmake --build "$SCRIPT_DIR/build" --parallel
fi

exec "$BINARY" "$@"

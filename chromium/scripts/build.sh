#!/usr/bin/env bash
# Configure and build Prism.app.
#
#   chromium/scripts/build.sh [arm64|x64]   (default: host arch)
#
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

ARCH="${1:-$(uname -m)}"
[ "$ARCH" = "arm64" ] || ARCH="x64"
OUT_DIR="$PRISM_CHROMIUM_SRC/out/Prism-$ARCH"
ARGS_FILE="$PRISM_REPO_ROOT/chromium/args/args-$ARCH.gn"

cd "$PRISM_CHROMIUM_SRC"

echo "==> gn gen $OUT_DIR (args: $ARGS_FILE)"
gn gen "$OUT_DIR" --args="$(tr '\n' ' ' < "$ARGS_FILE")"

echo "==> autoninja chrome"
autoninja -C "$OUT_DIR" chrome

echo "==> done: $OUT_DIR"

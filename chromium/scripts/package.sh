#!/usr/bin/env bash
# Package the built app into a signed, notarized dmg.
# TODO(phase-2): Developer ID signing identity, notarytool credentials,
# create-dmg/hdiutil packaging. Requires an Apple Developer account.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

ARCH="${1:-$(uname -m)}"
[ "$ARCH" = "arm64" ] || ARCH="x64"
OUT_DIR="$PRISM_CHROMIUM_SRC/out/Prism-$ARCH"

echo "package.sh: not implemented yet (Phase 2 packaging step)"
echo "built app expected at: $OUT_DIR"
exit 1

#!/usr/bin/env bash
# Apply Prism's patch series and source overlay onto the synced chromium tree.
# Idempotent: patches carry a marker check, the overlay is an rsync.
#
#   chromium/scripts/apply.sh
#
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

PATCHES_DIR="$PRISM_REPO_ROOT/chromium/patches"
OVERLAY_DIR="$PRISM_REPO_ROOT/chromium/src-overlay"

cd "$PRISM_CHROMIUM_SRC"

# 1. source overlay (new files, mainly //prism/)
if [ -d "$OVERLAY_DIR" ] && [ -n "$(ls -A "$OVERLAY_DIR" 2>/dev/null)" ]; then
  echo "==> copying src-overlay into the chromium tree"
  rsync -a "$OVERLAY_DIR/" "$PRISM_CHROMIUM_SRC/"
else
  echo "==> src-overlay is empty, skipping"
fi

# 2. patch series, in filename order
if [ -d "$PATCHES_DIR" ] && compgen -G "$PATCHES_DIR/*.patch" > /dev/null; then
  for patch in "$PATCHES_DIR"/*.patch; do
    name="$(basename "$patch")"
    if git apply --check "$patch" 2>/dev/null; then
      echo "==> applying $name"
      git apply "$patch"
    elif git apply --check --reverse "$patch" 2>/dev/null; then
      echo "==> $name already applied, skipping"
    else
      echo "ERROR: $name applies neither forward nor in reverse" >&2
      exit 1
    fi
  done
else
  echo "==> no patches found, skipping"
fi

echo "==> done. Build with chromium/scripts/build.sh"

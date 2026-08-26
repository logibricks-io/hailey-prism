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

# 2. patch series, in filename order (quilt semantics)
#
# The series may stack (a later patch can touch lines an earlier patch added),
# which breaks per-patch reverse probing. So application state is tracked in a
# stamp file listing the applied prefix; content checks stay as the backstop
# for stamp loss or manual tree edits.
STAMP="$PRISM_CHROMIUM_ROOT/.prism-applied-patches"
if [ -d "$PATCHES_DIR" ] && compgen -G "$PATCHES_DIR/*.patch" > /dev/null; then
  PATCHES=()
  for patch in "$PATCHES_DIR"/*.patch; do
    PATCHES+=("$patch")
  done

  # Trim the stamp to the actual series prefix: drop entries not on disk.
  STAMPED=()
  if [ -f "$STAMP" ]; then
    while IFS= read -r line; do
      [ -n "$line" ] && STAMPED+=("$line")
    done < "$STAMP"
  fi

  for patch in "${PATCHES[@]}"; do
    name="$(basename "$patch")"
    stamped=0
    for stamped_name in "${STAMPED[@]:-}"; do
      [ "$stamped_name" = "$name" ] && stamped=1 && break
    done
    if [ "$stamped" -eq 1 ]; then
      # Stamp says applied; verify by content when possible. Under stacking an
      # individual reverse check can legitimately fail, so only trust a
      # FORWARD check as a "stamp is stale" signal.
      if git apply --check "$patch" 2>/dev/null; then
        echo "==> $name: stamp stale (content missing), re-applying"
        git apply "$patch"
      fi
      continue
    fi
    if git apply --check "$patch" 2>/dev/null; then
      echo "==> applying $name"
      git apply "$patch" && echo "$name" >> "$STAMP"
    elif git apply --check --reverse "$patch" 2>/dev/null; then
      # Content present without a stamp entry (e.g. stamp lost): record it.
      echo "==> $name already applied (recovered), stamping"
      echo "$name" >> "$STAMP"
    else
      echo "ERROR: $name does not apply forward; if the series was applied" \
           "without the stamp, delete $STAMP and re-run" >&2
      exit 1
    fi
  done
else
  echo "==> no patches found, skipping"
fi

echo "==> done. Build with chromium/scripts/build.sh"

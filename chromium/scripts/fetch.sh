#!/usr/bin/env bash
# One-time fetch of the Chromium source tree, synced to the pinned revision.
#
# RESUMABILITY (read this before running):
#   Every step is idempotent and wrapped in a retry loop. If the download is
#   interrupted (network drop, laptop sleep, killed shell), just run this
#   script again — completed dep repositories persist and are skipped.
#   Caveat: git discards a partially-downloaded pack when a single repo's
#   transfer is interrupted, so an interrupted repo restarts its own pack;
#   everything else is kept. Re-running always converges.
#
#   chromium/scripts/fetch.sh
#
# Progress log: $PRISM_CHROMIUM_ROOT/fetch.log
#
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

MAX_ATTEMPTS="${PRISM_FETCH_MAX_ATTEMPTS:-50}"
RETRY_SLEEP="${PRISM_FETCH_RETRY_SLEEP:-20}"

log() { echo "[$(date '+%H:%M:%S')] $*"; }

# retry <command...>: run until success, with a pause between attempts.
retry() {
  local attempt=1
  until "$@"; do
    if [ "$attempt" -ge "$MAX_ATTEMPTS" ]; then
      log "FATAL: '$*' failed after $MAX_ATTEMPTS attempts"
      return 1
    fi
    log "attempt $attempt failed; retrying in ${RETRY_SLEEP}s: $*"
    sleep "$RETRY_SLEEP"
    attempt=$((attempt + 1))
  done
}

log "Prism chromium fetch (pin: $PRISM_PIN)"
log "depot_tools : $DEPOT_TOOLS_PATH"
log "checkout    : $PRISM_CHROMIUM_SRC"

# Network robustness knobs for giant packs over HTTP. Note: server-side pack
# generation for a repo this size can stay silent for minutes before the first
# byte flows, so the stall detector must be generous (100 B/s for 15 min =
# genuinely dead); anything tighter aborts healthy fetches.
git config --global http.postBuffer 524288000 || true
git config --global http.lowSpeedLimit 100 || true
git config --global http.lowSpeedTime 900 || true

# 1. depot_tools
if [ ! -d "$DEPOT_TOOLS_PATH/.git" ]; then
  log "cloning depot_tools"
  mkdir -p "$(dirname "$DEPOT_TOOLS_PATH")"
  retry git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_PATH"
else
  log "depot_tools already present, skipping clone"
fi

# 2. initial checkout. `fetch` refuses to touch a directory that already has
# gclient state, so once .gclient exists we switch to `gclient sync`, which IS
# designed for partial checkouts (it moves broken trees into _bad_scm/ and
# re-clones them). This is what makes re-running this script resume cleanly.
mkdir -p "$PRISM_CHROMIUM_ROOT"
cd "$PRISM_CHROMIUM_ROOT"
if [ ! -f "$PRISM_CHROMIUM_ROOT/.gclient" ]; then
  if [ "$PRISM_SRC_GIT_URL" != "$PRISM_SRC_GIT_URL_OFFICIAL" ]; then
    # Mirror path: author .gclient ourselves and skip fetch.py entirely —
    # gclient sync does the clone from the mirror.
    log "writing .gclient for mirror: $PRISM_SRC_GIT_URL"
    cat > "$PRISM_CHROMIUM_ROOT/.gclient" <<EOF
solutions = [
  {
    "name": "src",
    "url": "$PRISM_SRC_GIT_URL",
    "custom_deps": {},
    "custom_vars": {},
  },
]
EOF
  else
    log "fetch --no-history chromium (the multi-hour step)"
    retry fetch --no-history chromium
  fi
else
  log ".gclient already present, using gclient sync directly (resumable)"
fi

# Keep the solution URL in sync with PRISM_SRC_GIT_URL (idempotent rewrite).
if [ -f "$PRISM_CHROMIUM_ROOT/.gclient" ]; then
  sed -i '' "s|\"url\": \"[^\"]*\"|\"url\": \"$PRISM_SRC_GIT_URL\"|" "$PRISM_CHROMIUM_ROOT/.gclient"
  if [ -d "$PRISM_CHROMIUM_SRC/.git" ]; then
    git -C "$PRISM_CHROMIUM_SRC" remote set-url origin "$PRISM_SRC_GIT_URL" || true
  fi
fi

# 3. sync to the pinned revision
cd "$PRISM_CHROMIUM_SRC" 2>/dev/null || cd "$PRISM_CHROMIUM_ROOT"
log "gclient sync to src@refs/tags/$PRISM_PIN"
retry gclient sync --no-history --nohooks --revision "src@refs/tags/$PRISM_PIN" -D

# 4. run hooks (cipd downloads clang/rust/mac SDK; cipd keeps completed
# packages in its cache, so retries are cheap)
log "running gclient hooks (toolchain download)"
retry gclient runhooks

log "done. Next: chromium/scripts/apply.sh && chromium/scripts/build.sh"

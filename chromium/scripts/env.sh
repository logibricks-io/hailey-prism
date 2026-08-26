#!/usr/bin/env bash
# Shared environment for all Prism chromium scripts. Source this file.
set -euo pipefail

# The Chromium checkout lives outside the repo (paths with spaces break the
# toolchain). Override with PRISM_CHROMIUM_ROOT / PRISM_CHROMIUM_SRC.
export PRISM_CHROMIUM_ROOT="${PRISM_CHROMIUM_ROOT:-$HOME/chromium/prism}"
export PRISM_CHROMIUM_SRC="${PRISM_CHROMIUM_SRC:-$PRISM_CHROMIUM_ROOT/src}"
export DEPOT_TOOLS_PATH="${DEPOT_TOOLS_PATH:-$HOME/chromium/depot_tools}"

# depot_tools must be on PATH for gclient/gn/autoninja.
export PATH="$DEPOT_TOOLS_PATH:$PATH"

# gclient behavior knobs.
export DEPOT_TOOLS_UPDATE=0        # do not self-update on every gclient call
export GCLIENT_SUPPRESS_GCE_VERSION_CHECK=1

# Where chromium/src.git comes from. Default is the official host; set
# PRISM_SRC_GIT_URL to use a mirror, e.g. the gitcode mirror:
#   https://gitcode.com/gh_mirrors/chr/chromium.git   (domestic-friendly)
# NOTE: this only covers the main src repo. gclient still fetches the ~100
# DEPS dependency repos and the CIPD toolchains from their original hosts.
export PRISM_SRC_GIT_URL="${PRISM_SRC_GIT_URL:-https://chromium.googlesource.com/chromium/src.git}"
export PRISM_SRC_GIT_URL_OFFICIAL="https://chromium.googlesource.com/chromium/src.git"

PRISM_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export PRISM_REPO_ROOT

PRISM_PIN="$(tr -d '[:space:]' < "$PRISM_REPO_ROOT/chromium/DEPS.pin")"
export PRISM_PIN

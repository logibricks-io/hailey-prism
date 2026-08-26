#!/bin/sh
# Prism installer: downloads the Prism.app dmg from the GitHub Releases
# "latest" channel, installs it, and registers the prism-browser CLI.
#
#   sh install.sh                 install (or reuse) + launch
#   PRISM_INSTALL_DRY_RUN=1 sh install.sh   print the steps without executing
#
# Layout notes:
#   - the app lands in /Applications (falling back to ~/Applications without
#     sudo when /Applications is not writable);
#   - the CLI is the bundle-embedded copy at
#     Prism.app/Contents/Resources/prism-browser (added to the dmg by
#     chromium/scripts/package.sh), symlinked to ~/.local/bin/prism-browser.

set -eu

APP_NAME="Prism"
APP_BUNDLE_NAME="$APP_NAME.app"
APP_PATH="/Applications/$APP_BUNDLE_NAME"
USER_APP_PATH="$HOME/Applications/$APP_BUNDLE_NAME"
RELEASES_BASE="https://github.com/logibricks-io/hailey-prism/releases/latest/download"
DMG_URL_ARM64="$RELEASES_BASE/Prism-mac-arm64.dmg"
DMG_URL_X64="$RELEASES_BASE/Prism-mac-x64.dmg"
# The in-bundle CLI the dmg ships (package.sh stage). TODO: until the CLI is
# bundled, the symlink step is skipped with a printed note.
BUNDLED_CLI="Contents/Resources/prism-browser"
CLI_LINK_DIR="$HOME/.local/bin"
CLI_LINK="$CLI_LINK_DIR/prism-browser"

DRY_RUN="${PRISM_INSTALL_DRY_RUN:-}"

TEMP_DIR=""
MOUNT_DIR=""
DMG_ATTACHED=""

log() {
	printf '%s\n' "$*" >&2
}

step() {
	# Dry-run marker + log line in one place.
	if [ -n "$DRY_RUN" ]; then
		printf '[dry-run] %s\n' "$*" >&2
	else
		log "$*"
	fi
}

die() {
	log "error: $*"
	exit 1
}

maybe() {
	# Run the command unless this is a dry run.
	if [ -n "$DRY_RUN" ]; then
		printf '[dry-run] would run: %s\n' "$*" >&2
		return 0
	fi
	"$@"
}

require_command() {
	command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

select_dmg_url() {
	if [ "$(uname -m)" = "arm64" ]; then
		printf '%s\n' "$DMG_URL_ARM64"
	else
		printf '%s\n' "$DMG_URL_X64"
	fi
}

run_with_sudo_if_needed() {
	# Try without elevated privileges first; fall back to sudo to avoid
	# unnecessary prompts.
	if "$@"; then
		return 0
	fi

	if [ "$(id -u)" -eq 0 ]; then
		return 1
	fi

	require_command sudo
	sudo "$@"
}

cleanup() {
	if [ "$DMG_ATTACHED" = "1" ]; then
		if ! hdiutil detach "$MOUNT_DIR" -quiet >/dev/null 2>&1; then
			log "warning: failed to detach $MOUNT_DIR"
		fi
		DMGA_ATTACHED=""
	fi

	if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
		rm -rf "$TEMP_DIR" >/dev/null 2>&1 ||
			log "warning: failed to remove temporary directory: $TEMP_DIR"
	fi
}

strip_quarantine_attributes() {
	app_path="$1"
	# Unsigned internal builds: strip the quarantine bit so Gatekeeper does
	# not block the first launch. Signed+notarized builds don't need this.
	run_with_sudo_if_needed xattr -dr com.apple.quarantine "$app_path" \
		>/dev/null 2>&1 || true
}

trap cleanup EXIT HUP INT TERM

is_prism_app() {
	app_path="$1"
	[ -d "$app_path/Contents/MacOS/Prism" ] || [ -f "$app_path/Contents/MacOS/Prism" ] || return 1
}

find_prism_app() {
	for app_path in "$APP_PATH" "$USER_APP_PATH"; do
		if is_prism_app "$app_path"; then
			printf '%s\n' "$app_path"
			return 0
		fi
	done
	return 1
}

install_prism() {
	require_command curl
	require_command hdiutil

	temp_base_dir=${TMPDIR:-/tmp}
	temp_base_dir=${temp_base_dir%/}
	dmg_url=$(select_dmg_url)

	if [ -n "$DRY_RUN" ]; then
		step "download $dmg_url"
		step "mount the dmg"
		step "copy $APP_BUNDLE_NAME to $APP_PATH (fallback: $USER_APP_PATH)"
		step "strip quarantine attributes"
		step "symlink the bundled CLI to $CLI_LINK"
		step "open $APP_NAME"
		return 0
	fi

	TEMP_DIR=$(mktemp -d "$temp_base_dir/prism-install.XXXXXX")
	MOUNT_DIR="$TEMP_DIR/mount"
	dmg_path="$TEMP_DIR/prism.dmg"
	mkdir -p "$MOUNT_DIR"

	log "Downloading $dmg_url ..."
	curl -fL --retry 3 --output "$dmg_path" "$dmg_url" ||
		die "failed to download $APP_NAME from $dmg_url"

	log "Mounting installer ..."
	hdiutil attach "$dmg_path" -nobrowse -readonly -mountpoint "$MOUNT_DIR" \
		>/dev/null
	DMG_ATTACHED="1"

	app_in_dmg=$(find "$MOUNT_DIR" -maxdepth 2 -type d -name "$APP_BUNDLE_NAME" |
		head -n 1)
	[ -n "$app_in_dmg" ] || die "cannot find $APP_BUNDLE_NAME in mounted DMG"

	# Prefer /Applications; fall back to ~/Applications when it is not
	# writable (no sudo prompt for the per-user install).
	target="$APP_PATH"
	if [ ! -w "$(dirname "$APP_PATH")" ]; then
		target="$USER_APP_PATH"
		mkdir -p "$USER_APP_PATH"
	fi

	staged_app="$TEMP_DIR/$APP_BUNDLE_NAME"
	log "Installing $APP_NAME to $target ..."
	ditto "$app_in_dmg" "$staged_app" ||
		die "failed to stage $APP_NAME from the installer"

	log "Removing quarantine attributes ..."
	xattr -dr com.apple.quarantine "$staged_app" >/dev/null 2>&1 || true

	if [ -d "$target" ]; then
		run_with_sudo_if_needed rm -rf "$target" ||
			die "failed to replace existing $target"
	fi
	run_with_sudo_if_needed mv "$staged_app" "$target" ||
		die "failed to move $APP_NAME to $target"
}

register_cli() {
	installed_app_path="$1"
	bundled="$installed_app_path/$BUNDLED_CLI"
	if [ ! -x "$bundled" ]; then
		log "note: this build has no bundled CLI at $BUNDLED_CLI yet" \
			"(packaging TODO); skipping the prism-browser symlink"
		return 0
	fi
	step "link $bundled -> $CLI_LINK"
	maybe mkdir -p "$CLI_LINK_DIR"
	maybe ln -sf "$bundled" "$CLI_LINK"
	log "CLI registered: $CLI_LINK (ensure $CLI_LINK_DIR is on your PATH)"
}

main() {
	[ "$(uname -s)" = "Darwin" ] || die "this script only supports macOS"

	installed_app_path=$(find_prism_app || true)
	if [ -z "$installed_app_path" ]; then
		install_prism
		if [ -n "$DRY_RUN" ]; then
			return 0
		fi
		installed_app_path=$(find_prism_app || true)
		[ -n "$installed_app_path" ] ||
			die "$APP_NAME install completed, but the app was not found"
	else
		step "$APP_NAME already installed at $installed_app_path"
	fi

	if [ -z "$DRY_RUN" ]; then
		strip_quarantine_attributes "$installed_app_path"
	fi
	register_cli "$installed_app_path"
	cleanup

	if [ -n "$DRY_RUN" ]; then
		return 0
	fi

	log "Launching $APP_NAME ..."
	exec open "$installed_app_path"
}

main

#!/usr/bin/env bash
# Package the built Prism.app into a distributable dmg.
#
#   chromium/scripts/package.sh [arm64|x64]   (default: host arch)
#
# Input : $PRISM_CHROMIUM_SRC/out/Prism-<arch>/Prism.app
# Output: chromium/dist/Prism-<version>-mac-<arch>.dmg
#
# Signing is an optional stage: with PRISM_SIGNING_IDENTITY set to a
# "Developer ID Application: ..." identity the app is codesigned (hardened
# runtime) and, when PRISM_NOTARY_PROFILE names a keychain profile, notarized
# + stapled. Without the identity the dmg is produced unsigned — fine for
# internal testing, but Gatekeeper will block it on other machines.
#
# Idempotent: the staging dir and dmg are rebuilt from scratch each run.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

ARCH="${1:-$(uname -m)}"
[ "$ARCH" = "arm64" ] || ARCH="x64"
OUT_DIR="$PRISM_CHROMIUM_SRC/out/Prism-$ARCH"
APP="$OUT_DIR/Prism.app"
DIST_DIR="$PRISM_REPO_ROOT/chromium/dist"

if [ ! -d "$APP" ]; then
  echo "ERROR: $APP not found — build first: chromium/scripts/build.sh $ARCH" >&2
  exit 1
fi

VERSION="$(defaults read "$APP/Contents/Info" CFBundleShortVersionString 2>/dev/null || true)"
if [ -z "$VERSION" ]; then
  echo "ERROR: could not read CFBundleShortVersionString from $APP" >&2
  exit 1
fi

DMG_NAME="Prism-$VERSION-mac-$ARCH.dmg"
DMG_PATH="$DIST_DIR/$DMG_NAME"
STAGING="$DIST_DIR/.staging-$ARCH"

echo "==> packaging $APP"
echo "    version: $VERSION"

# --- optional signing stage -------------------------------------------------
if [ -n "${PRISM_SIGNING_IDENTITY:-}" ]; then
  echo "==> codesigning with '$PRISM_SIGNING_IDENTITY' (hardened runtime)"
  # Deep-sign: helpers and framework first, then the outer bundle.
  codesign --sign "$PRISM_SIGNING_IDENTITY" --timestamp --options runtime \
    --force --deep "$APP"

  if [ -n "${PRISM_NOTARY_PROFILE:-}" ]; then
    echo "==> notarizing via keychain profile '$PRISM_NOTARY_PROFILE'"
    NOTARY_ZIP="$DIST_DIR/.notary-$ARCH.zip"
    rm -f "$NOTARY_ZIP"
    ditto -c -k --keepParent "$APP" "$NOTARY_ZIP"
    xcrun notarytool submit "$NOTARY_ZIP" --keychain-profile "$PRISM_NOTARY_PROFILE" --wait
    xcrun stapler staple "$APP"
    rm -f "$NOTARY_ZIP"
  else
    echo "==> PRISM_NOTARY_PROFILE not set; skipping notarization"
  fi
else
  echo "==> PRISM_SIGNING_IDENTITY not set; producing an UNSIGNED dmg"
  echo "    (internal testing only — Gatekeeper will block it elsewhere)"
fi

# --- dmg assembly ------------------------------------------------------------
echo "==> assembling dmg"
rm -rf "$STAGING"
mkdir -p "$STAGING"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"

mkdir -p "$DIST_DIR"
rm -f "$DMG_PATH"
hdiutil create "$DMG_PATH" \
  -volname "Prism" \
  -srcfolder "$STAGING" \
  -ov -format UDZO -imagekey zlib-level=9 -quiet

rm -rf "$STAGING"

# --- verification ------------------------------------------------------------
echo "==> verifying dmg mounts"
ATTACH_OUT="$(hdiutil attach "$DMG_PATH" -nobrowse -readonly -mountrandom /tmp)"
MOUNT_POINT="$(echo "$ATTACH_OUT" | tail -1 | awk -F'\t' '{print $NF}')"
if [ -z "$MOUNT_POINT" ] || [ ! -d "$MOUNT_POINT/Prism.app" ]; then
  echo "ERROR: dmg mounted but Prism.app is missing inside it" >&2
  exit 1
fi
hdiutil detach "$MOUNT_POINT" -quiet || true

echo "==> done: $DMG_PATH"
ls -lh "$DMG_PATH"

#!/bin/sh
# build-appimage.sh — build the universal AppImage.
#
#   chmod +x vted-<version>-<arch>.AppImage
#   ./vted-<version>-<arch>.AppImage
#
# Runs on any Linux x86_64 distribution without installing anything.
# The AppImage carries the static binary, so it needs nothing from the
# host system except a terminal.
set -eu
. "$(dirname "$0")/lib.sh"
need curl
require_static_bin

# Pinned so builds stay reproducible.
LINUXDEPLOY_VERSION=1-alpha-20251107-1
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
APPDIR="$WORK/AppDir"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

install -m 755 "$STATIC_BIN" "$APPDIR/usr/bin/vted"
install -m 644 "$ROOT/packaging/assets/vted.desktop" "$APPDIR/usr/share/applications/vted.desktop"
install -m 644 "$ROOT/packaging/assets/vted.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/vted.png"
cp "$ROOT/packaging/assets/vted.png" "$APPDIR/vted.png"

LINUXDEPLOY="$WORK/linuxdeploy.AppImage"
curl -fsSL -o "$LINUXDEPLOY" "$LINUXDEPLOY_URL" || die "could not download linuxdeploy $LINUXDEPLOY_VERSION"
chmod +x "$LINUXDEPLOY"

OUT="$WORK/out"
mkdir -p "$OUT"
cd "$OUT"
# APPIMAGE_EXTRACT_AND_RUN=1 lets the AppImage run without FUSE;
# NO_STRIP=1 keeps linuxdeploy from touching the static binary.
APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 ARCH=x86_64 \
  "$LINUXDEPLOY" \
    --executable "$APPDIR/usr/bin/vted" \
    --desktop-file "$APPDIR/usr/share/applications/vted.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/vted.png" \
    --appdir "$APPDIR" \
    --output appimage

APPIMAGE="$(find "$OUT" -maxdepth 1 -type f -name '*.AppImage' | head -n 1)"
[ -n "$APPIMAGE" ] || die 'linuxdeploy produced no AppImage'

mkdir -p "$DIST"
TARGET="$DIST/vted-$VERSION-$ARCH.AppImage"
mv "$APPIMAGE" "$TARGET"

# quick self-check: the payload must extract and contain the binary
(cd "$WORK" && "$TARGET" --appimage-extract >/dev/null 2>&1) \
  || die 'the AppImage does not extract'
[ -x "$WORK/squashfs-root/usr/bin/vted" ] || die 'the AppImage is missing the vted binary'
rm -rf "$WORK/squashfs-root"

printf 'packaging: built %s (%s)\n' "$TARGET" "$(du -h "$TARGET" | cut -f1)"

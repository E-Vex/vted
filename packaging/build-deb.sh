#!/bin/sh
# build-deb.sh — build the Debian package.
#
#   sudo apt install ./vted_<version>_<arch>.deb
#
# Debian, Ubuntu, Mint, Pop!_OS and every other deb based distro. The
# package ships the static binary, so it carries no library
# dependencies at all: one file, /usr/bin/vted, plus the manual page.
set -eu
. "$(dirname "$0")/lib.sh"
need dpkg-deb
need install
require_static_bin

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE="$WORK/pkg"

mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/share/man/man1" \
         "$STAGE/usr/share/doc/vted"

install -m 755 "$STATIC_BIN" "$STAGE/usr/bin/vted"
gzip -9n -c "$ROOT/docs/vted.1" > "$STAGE/usr/share/man/man1/vted.1.gz"

{
  sed "s/@VERSION@/$VERSION/g" "$ROOT/packaging/deb/copyright.in"
  sed 's/^/ /' "$ROOT/LICENSE"
} > "$STAGE/usr/share/doc/vted/copyright"

SIZE="$(du -sk "$STAGE" | cut -f1)"
sed -e "s/@VERSION@/$VERSION/g" \
    -e "s/@ARCH@/$DEB_ARCH/g" \
    -e "s/@SIZE@/$SIZE/g" \
    "$ROOT/packaging/deb/control" > "$STAGE/DEBIAN/control"

mkdir -p "$DIST"
DEB="$DIST/vted_${VERSION}_${DEB_ARCH}.deb"
dpkg-deb --root-owner-group --build "$STAGE" "$DEB"

# quick self-check: the binary must be in there
dpkg-deb -c "$DEB" | grep -q 'usr/bin/vted' || die 'the .deb is missing /usr/bin/vted'
printf 'packaging: built %s (%s)\n' "$DEB" "$(dpkg-deb -f "$DEB" Architecture)"

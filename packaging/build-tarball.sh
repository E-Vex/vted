#!/bin/sh
# build-tarball.sh — build the static binary tarball.
#
#   tar xzf vted-<version>-linux-<arch>.tar.gz
#   cd vted-<version>-linux-<arch>
#   sudo ./install.sh
#
# The fallback for every distribution without .deb or .rpm support:
# Arch, Alpine, Gentoo, Void, chroots, rescue disks — anywhere a
# static binary and a man page will be at home.
set -eu
. "$(dirname "$0")/lib.sh"
need tar
require_static_bin

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
DIR="vted-$VERSION-linux-$ARCH"
STAGE="$WORK/$DIR"
mkdir -p "$STAGE"

install -m 755 "$STATIC_BIN" "$STAGE/vted"
install -m 644 "$ROOT/docs/vted.1" "$STAGE/vted.1"
install -m 644 "$ROOT/LICENSE" "$STAGE/LICENSE"
install -m 755 "$ROOT/packaging/tarball/install.sh" "$STAGE/install.sh"
sed "s/@VERSION@/$VERSION/g" "$ROOT/packaging/tarball/README.md" > "$STAGE/README.md"

mkdir -p "$DIST"
TARBALL="$DIST/vted-$VERSION-linux-$ARCH.tar.gz"
tar -czf "$TARBALL" -C "$WORK" "$DIR"

# quick self-check: the tarball must contain the binary
tar -tzf "$TARBALL" | grep -q "^$DIR/vted$" || die 'the tarball is missing the vted binary'
printf 'packaging: built %s\n' "$TARBALL"

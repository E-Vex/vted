#!/bin/sh
# install.sh — install vted from this tarball.
#
#   sudo ./install.sh                    # into /usr/local (the default)
#   PREFIX="$HOME/.local" ./install.sh   # into your home directory
#   sudo DESTDIR=/tmp/stage ./install.sh # stage somewhere else
set -eu

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

cd "$(dirname "$0")"

mkdir -p "$DESTDIR$PREFIX/bin" "$DESTDIR$PREFIX/share/man/man1"
install -m 755 vted "$DESTDIR$PREFIX/bin/vted"
install -m 644 vted.1 "$DESTDIR$PREFIX/share/man/man1/vted.1"

printf 'installed %s%s/bin/vted\n' "$DESTDIR" "$PREFIX"
printf 'installed %s%s/share/man/man1/vted.1\n' "$DESTDIR" "$PREFIX"

case "$PREFIX" in
  /usr|/usr/local) : ;;
  *) printf 'note: make sure %s/bin is on your PATH\n' "$PREFIX" ;;
esac

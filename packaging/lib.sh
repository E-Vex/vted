# lib.sh — shared helpers for the vted packaging scripts.
#
# Every build-*.sh sources this file. It only sets variables and
# provides a few tiny checks; see packaging/README.md for the big
# picture.

set -eu

die() { printf 'packaging: error: %s\n' "$1" >&2; exit 1; }

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"

# The version comes from the Makefile, the single source of truth.
VERSION="$(sed -n 's/^VERSION *= *//p' "$ROOT/Makefile" | tr -d '[:space:]')"
[ -n "$VERSION" ] || die 'could not read VERSION from the Makefile'

# uname says x86_64; the formats want their own naming.
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64)  DEB_ARCH=amd64 ;;
  aarch64) DEB_ARCH=arm64 ;;
  *)       DEB_ARCH="$ARCH" ;;
esac

die() { printf 'packaging: error: %s\n' "$1" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "'$1' is required but not installed"; }

# The .deb, the AppImage and the tarball all ship the statically
# linked binary that build-static.sh leaves in the repo root.
STATIC_BIN="$ROOT/vted"
require_static_bin() {
  [ -x "$STATIC_BIN" ] \
    || die "no ./vted binary - run packaging/build-static.sh first"
  file "$STATIC_BIN" 2>/dev/null | grep -q 'statically linked' \
    || die "./vted is not statically linked - run packaging/build-static.sh first"
}

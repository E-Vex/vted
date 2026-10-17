#!/bin/sh
# build-static.sh — build the fully static binary that the packages ship.
#
# Uses musl-gcc when it is available (apt install musl-tools); without
# it, falls back to a glibc static build, which works just as well for
# vted because it touches nothing glibc links dynamically.
set -eu
. "$(dirname "$0")/lib.sh"
need make

if command -v musl-gcc >/dev/null 2>&1; then
  CC=musl-gcc
else
  printf 'packaging: musl-gcc not found, falling back to a glibc static build\n' >&2
  CC=gcc
fi

cd "$ROOT"
make clean
make CC="$CC" CFLAGS="-O2 -static -Wall -Wextra -Wconversion -std=c11 -I."
require_static_bin

printf 'packaging: static binary ready: %s\n' "$(file -b vted | sed 's/, version.*//')"

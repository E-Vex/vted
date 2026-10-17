#!/bin/sh
# build-rpm.sh — build the RPM package.
#
#   sudo dnf install ./vted-<version>-1.<arch>.rpm    # Fedora, RHEL
#   sudo zypper install ./vted-<version>-1.<arch>.rpm # openSUSE
#
# Unlike the other artifacts, the RPM is built from source against the
# building system's C library — the way RPM based distros expect. CI
# builds it on a stock glibc system, which is what every rpm distro
# runs.
set -eu
. "$(dirname "$0")/lib.sh"
need rpmbuild
need git

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
TOP="$WORK/rpmbuild"
mkdir -p "$TOP/BUILD" "$TOP/RPMS" "$TOP/SOURCES" "$TOP/SPECS" "$TOP/SRPMS"

# The source the RPM builds from: the current commit, tarred the way
# rpmbuild expects it (vted-<version>/...).
git archive --prefix="vted-$VERSION/" -o "$TOP/SOURCES/vted-$VERSION.tar.gz" HEAD

sed -e "s/@VERSION@/$VERSION/g" \
    -e "s/@DATE@/$(date '+%a %b %d %Y')/g" \
    "$ROOT/packaging/rpm/vted.spec" > "$TOP/SPECS/vted.spec"

if ! rpmbuild --define "_topdir $TOP" \
              --define "debug_package %{nil}" \
              -bb "$TOP/SPECS/vted.spec" > "$TOP/rpmbuild.log" 2>&1; then
  tail -n 40 "$TOP/rpmbuild.log" >&2
  die 'rpmbuild failed (log above)'
fi

RPM="$(find "$TOP/RPMS" -type f -name '*.rpm' | head -n 1)"
[ -n "$RPM" ] || die 'rpmbuild produced no rpm'

mkdir -p "$DIST"
cp "$RPM" "$DIST/"
printf 'packaging: built %s\n' "$DIST/$(basename "$RPM")"

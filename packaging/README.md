# Packaging

Scripts that turn the vted source tree into the installable packages
attached to every release. All of them run on a plain checkout with no
configuration, and CI drives them on every tag push — see
`.github/workflows/release.yml`.

| Script                | Produces                            | Installs on                          |
| --------------------- | ----------------------------------- | ------------------------------------ |
| `build-static.sh`     | `./vted` (static binary)            | — (input for everything below)       |
| `build-deb.sh`        | `dist/vted_<v>_<arch>.deb`          | Debian, Ubuntu, Mint, Pop!_OS, ...   |
| `build-rpm.sh`        | `dist/vted-<v>-1.<arch>.rpm`        | Fedora, RHEL, CentOS, openSUSE, ...  |
| `build-appimage.sh`   | `dist/vted-<v>-<arch>.AppImage`     | any x86_64 distro, nothing to install |
| `build-tarball.sh`    | `dist/vted-<v>-linux-<arch>.tar.gz` | everything else (Arch, Alpine, ...)  |

Run them in this order:

```sh
./packaging/build-static.sh
./packaging/build-deb.sh
./packaging/build-rpm.sh
./packaging/build-appimage.sh
./packaging/build-tarball.sh
```

Requirements: `make` and a C compiler; `musl-tools` for the static
build (the scripts fall back to a glibc static build without it);
`dpkg-deb` (part of dpkg), `rpmbuild` (part of rpm), `file`, `curl`
for the AppImage tool download.

## What ships in what

- The `.deb`, the AppImage and the tarball carry the **statically
  linked** binary built by `build-static.sh` — no library
  dependencies, runs on glibc and musl systems alike.
- The `.rpm` is **built from source** by `rpmbuild` against the
  building system's C library, the way RPM based distros expect.

## Layout

```
lib.sh              shared variables and checks, sourced by the scripts
deb/                the .deb control and copyright files
rpm/                the RPM spec (version and date filled in at build time)
tarball/            install.sh and README.md that ship inside the tarball
assets/             icon and desktop file for the AppImage
```

# vted — vex terminal text editor

A small, fast terminal text editor for Linux: one C11 binary, no
dependencies beyond the C standard library and POSIX. No curses, no
autotools, no cmake — just `make`.

[![build](https://github.com/E-Vex/vted/actions/workflows/build.yml/badge.svg)](https://github.com/E-Vex/vted/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/E-Vex/vted?color=blue)](https://github.com/E-Vex/vted/releases)
[![standard](https://img.shields.io/badge/standard-C11-28399b)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![license](https://img.shields.io/badge/license-MIT-yellow)](LICENSE)

<p align="center">
<img src="docs/screenshot.svg" alt="vted editing its own source with an active search for 'cursor'">
</p>

## Install

Ready-made packages are attached to every
[release](https://github.com/E-Vex/vted/releases). They install
`vted` and the manual page. Each release also carries a
`SHA256SUMS.txt` so the downloads can be checked with `sha256sum -c`:

| File                              | Installs on                          | How                                        |
| --------------------------------- | ------------------------------------ | ------------------------------------------ |
| `vted_<v>_<arch>.deb`             | Debian, Ubuntu, Mint, Pop!_OS, ...   | `sudo apt install ./vted_<v>_<arch>.deb`   |
| `vted-<v>-1.<arch>.rpm`           | Fedora, RHEL, openSUSE, ...          | `sudo dnf install ./vted-<v>-1.<arch>.rpm` |
| `vted-<v>-<arch>.AppImage`        | any x86_64 distro                    | `chmod +x` and run                         |
| `vted-<v>-linux-<arch>.tar.gz`    | any x86_64 distro                    | unpack, then `sudo ./install.sh`           |

The examples below use version 0.2.0; grab the file names from the
latest release page.

### Debian, Ubuntu, Mint and friends (.deb)

```sh
sudo apt install ./vted_0.2.0_amd64.deb
```

The package carries a statically linked binary, so `apt` has nothing
to resolve and no repositories need to be added — it installs straight
into `/usr/bin` with the manual page in place. `dpkg -i` works just
the same. Remove it again with `sudo apt remove vted`.

### Fedora, RHEL, CentOS, openSUSE (.rpm)

```sh
sudo dnf install ./vted-0.2.0-1.x86_64.rpm     # Fedora, RHEL, CentOS
sudo zypper install ./vted-0.2.0-1.x86_64.rpm  # openSUSE
```

The RPM is built from source against the system C library, the way RPM
distributions expect, so its dependencies are checked honestly at
install time. Remove it with `sudo dnf remove vted` or
`sudo zypper remove vted`.

### Any distro — AppImage

```sh
chmod +x vted-0.2.0-x86_64.AppImage
./vted-0.2.0-x86_64.AppImage
```

Nothing is installed and nothing is extracted — it just runs. To use
it as plain `vted`, put it on your `PATH`:

```sh
install -m 755 vted-0.2.0-x86_64.AppImage ~/.local/bin/vted
```

### Any distro — static binary tarball

For everything without `.deb` or `.rpm` support — Arch, Alpine,
Gentoo, Void, a chroot, a rescue disk:

```sh
tar xzf vted-0.2.0-linux-x86_64.tar.gz
cd vted-0.2.0-linux-x86_64
sudo ./install.sh                          # or: PREFIX="$HOME/.local" ./install.sh
```

The binary inside is statically linked (musl), so it runs on glibc and
musl systems alike with zero dependencies. Uninstall with
`sudo rm /usr/local/bin/vted /usr/local/share/man/man1/vted.1`.

### From git (the usual way)

```sh
git clone https://github.com/E-Vex/vted.git
cd vted
make
sudo make install
```

This installs:

- `/usr/local/bin/vted` — the editor
- `/usr/local/share/man/man1/vted.1` — the manual (`man vted`)

To try it before installing anything, just run `./vted` after `make`.

### From a release source tarball

```sh
curl -LO https://github.com/E-Vex/vted/archive/refs/tags/v0.1.0.tar.gz
tar xzf v0.1.0.tar.gz
cd vted-0.1.0
make && sudo make install
```

See the [releases page](https://github.com/E-Vex/vted/releases) for all
versions.

### Without sudo

Install into your home directory instead:

```sh
make PREFIX="$HOME/.local" install
```

Make sure `~/.local/bin` is on your `PATH`. The install locations are
fully controlled by `PREFIX` and `DESTDIR` (useful for package
builders).

### Uninstall

```sh
sudo make uninstall
```

### Requirements

- Linux
- a C11 compiler (`gcc` or `clang`)
- `make`

That is the whole list.

## Usage

```
vted            # start with an empty buffer
vted file.c     # open a file
man vted        # the full manual
```

## Keys

| Key             | Action                                           |
| --------------- | ------------------------------------------------ |
| arrows          | move the cursor (wraps across lines)             |
| Ctrl-Left/Right | move by word (Alt-arrows and Alt-b/f work too)   |
| Home / End      | jump to start / end of line                      |
| PageUp/Down     | move one viewport up / down                      |
| Enter           | split the line at the cursor, keeping its indent |
| Tab             | insert a tab, shown up to the next tab stop      |
| Backspace       | delete the character on the left, merge lines    |
| Delete          | delete the character under the cursor            |
| Ctrl-K          | delete the whole line under the cursor           |
| Ctrl-D          | duplicate the line under the cursor              |
| Ctrl-Z          | undo the last change (the last 200 are kept)     |
| Ctrl-Y          | redo the last undone change                      |
| typing          | insert at the cursor                             |
| Ctrl-S          | save the buffer (asks for a name when unnamed)   |
| Ctrl-G          | go to a line number (ESC cancels)                |
| Ctrl-F          | incremental search (arrows jump, ESC restores)   |
| Ctrl-Q          | quit (three extra presses when modified)         |

## Features

- Raw terminal mode with automatic settings restore on exit
- Alternate screen buffer: your shell history stays untouched
- Open files from the command line, edit, and save them back
- Full cursor movement: arrows, Home, End, PageUp, PageDown
- Word movement with Ctrl or Alt and the arrows, or Alt-b and Alt-f; a word
  is a run of letters, digits and underscores, or a run of punctuation
- Vertical and horizontal scrolling for files of any size
- Insert and delete text, split lines with Enter, merge with Backspace
- Delete the whole line with Ctrl-K, duplicate it with Ctrl-D
- Undo and redo with Ctrl-Z and Ctrl-Y; the cursor follows along
- Line endings are preserved: LF, CRLF, CR and mixed files save back
  byte for byte, files without a trailing newline keep having none, and
  new lines take the ending of the line they came from
- Auto-indent: Enter starts the new line with the indent of the current one
- UTF-8 aware: the cursor and deletion move by whole characters, wide
  (CJK, emoji) and combining characters take the right number of cells
- Tabs expand to tab stops of 8; control characters show as `^X` and
  invalid bytes as `�`, so nothing odd ever reaches your terminal
- The cursor keeps its display column across short lines when moving
  up and down
- Status bar with filename, line count, modified marker and the cursor's line
  and display column
- Message bar for status and warnings
- Incremental search (Ctrl-F) with match highlighting
- Go to a line with Ctrl-G; a number past the end lands on the last line
- Saving an unnamed buffer asks for a file name; the buffer stays unnamed
  if the file cannot be written
- Saves are atomic: the text goes to a temporary file that is renamed
  over the target, so a failed save never destroys the original, and the
  file's permissions are kept
- Quit protection: unsaved changes require repeated Ctrl-Q

## Documentation

- [docs/architecture.md](docs/architecture.md) — module lifecycle and the edit loop
- [docs/rendering.md](docs/rendering.md) — how a frame reaches the terminal
- [docs/terminal-modes.md](docs/terminal-modes.md) — raw mode and the alternate screen
- [docs/vted.1](docs/vted.1) — the manual page source

| Module            | Responsibility                              |
| ----------------- | ------------------------------------------- |
| `vted_terminal`   | raw mode, terminal save/restore, fatal exit |
| `vted_keyboard`   | raw byte reading, escape sequence parsing   |
| `vted_screen`     | viewport, scrolling, rendering, bars        |
| `vted_row`        | text buffer, row editing and cell layout    |
| `vted_utf8`       | UTF-8 decoding, boundaries and cell widths  |
| `vted_file`       | loading and saving files                    |
| `vted`            | the editor core: key dispatch and editing   |

## Roadmap

vted is under active development. Planned next:

- Syntax highlighting and line numbers

## Contributing

The project is built one small step at a time — every commit does one
thing. If you pitch in, keep it that way: a change should build clean
(`make CFLAGS="-Wall -Wextra -Wconversion -Werror"` is enforced by CI)
and be one focused commit.

## License

MIT — see [LICENSE](LICENSE).

# vted - vex terminal text editor

A small, fast terminal text editor for Linux written in C11 with no
dependencies beyond the C standard library and POSIX. Built module by
module with a commit per step.

[![License: MIT](https://img.shields.io/github/license/gcla/termshark.svg?color=yellow)](LICENSE)

## Build and run

```
make            # build the editor
./vted          # start with an empty buffer
./vted file.c   # open a file
```

Requires `gcc` (or any C11 compiler) and Linux.

## Features

- Raw terminal mode with automatic settings restore on exit
- Alternate screen buffer: your shell history stays untouched
- Open files from the command line, edit, and save them back
- Full cursor movement: arrows, Home, End, PageUp, PageDown
- Vertical and horizontal scrolling for files of any size
- Insert and delete text, split lines with Enter, merge with Backspace
- UTF-8 aware: multibyte characters can be typed and displayed
- Status bar with filename, line count, modified marker and cursor position
- Message bar for status and warnings
- Incremental search (Ctrl-F) with match highlighting
- Quit protection: unsaved changes require repeated Ctrl-Q

## Keys

| Key            | Action                                      |
| -------------- | ------------------------------------------- |
| arrows         | move the cursor (wraps across lines)       |
| Home / End     | jump to start / end of line                |
| PageUp/Down    | move one viewport up / down                |
| Enter          | split the line at the cursor               |
| Backspace      | delete left, merge lines at column zero    |
| Delete         | delete under the cursor                    |
| typing         | insert at the cursor                       |
| Ctrl-S         | save the buffer to its file                |
| Ctrl-F         | incremental search (arrows jump, ESC cancels) |
| Ctrl-Q         | quit (three extra presses when modified)   |

## Architecture

See [docs/architecture.md](docs/architecture.md) for the module
lifecycle and [docs/rendering.md](docs/rendering.md) for how frames
reach the terminal.

| Module            | Responsibility                            |
| ----------------- | ----------------------------------------- |
| `vted_terminal`   | raw mode, terminal save/restore, fatal exit |
| `vted_keyboard`   | raw byte reading, escape sequence parsing  |
| `vted_screen`     | viewport, scrolling, rendering, bars       |
| `vted_row`        | the text buffer and row editing primitives |
| `vted_file`       | loading and saving files                   |
| `vted`            | the editor core: key dispatch and editing ops |

## LICENSE

MIT, see [LICENSE](LICENSE).

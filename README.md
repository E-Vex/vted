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

The examples below use version 0.3.0; grab the file names from the
latest release page.

### Debian, Ubuntu, Mint and friends (.deb)

```sh
sudo apt install ./vted_0.3.0_amd64.deb
```

The package carries a statically linked binary, so `apt` has nothing
to resolve and no repositories need to be added — it installs straight
into `/usr/bin` with the manual page in place. `dpkg -i` works just
the same. Remove it again with `sudo apt remove vted`.

### Fedora, RHEL, CentOS, openSUSE (.rpm)

```sh
sudo dnf install ./vted-0.3.0-1.x86_64.rpm     # Fedora, RHEL, CentOS
sudo zypper install ./vted-0.3.0-1.x86_64.rpm  # openSUSE
```

The RPM is built from source against the system C library, the way RPM
distributions expect, so its dependencies are checked honestly at
install time. Remove it with `sudo dnf remove vted` or
`sudo zypper remove vted`.

### Any distro — AppImage

```sh
chmod +x vted-0.3.0-x86_64.AppImage
./vted-0.3.0-x86_64.AppImage
```

Nothing is installed and nothing is extracted — it just runs. To use
it as plain `vted`, put it on your `PATH`:

```sh
install -m 755 vted-0.3.0-x86_64.AppImage ~/.local/bin/vted
```

### Any distro — static binary tarball

For everything without `.deb` or `.rpm` support — Arch, Alpine,
Gentoo, Void, a chroot, a rescue disk:

```sh
tar xzf vted-0.3.0-linux-x86_64.tar.gz
cd vted-0.3.0-linux-x86_64
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

### Building from source, for contributors

```sh
make check
```

builds the editor and the unit tests, runs every unit test, then walks
the editor over a pseudo terminal the way the CI does — the smoke test
opens a file, quits cleanly, and completes a word from another buffer.
A change is only done when `make check` says so; `make test` runs the
unit tests alone, `make bench` times a ~100 MB file.

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
vted                    # start with an empty buffer
vted file.c             # open a file
vted a.c b.c c.c        # open several files, one buffer each
vted +42 file.c         # open it with the cursor on line 42
grep TODO *.c | vted -  # edit what another command prints
vted --help             # the command line in short
vted --version          # the version
man vted                # the full manual
```

Text piped in on standard input becomes an unnamed buffer: Ctrl-S
then asks where to save it. It can sit beside named files
(`vted - notes.txt`), each in a buffer of its own.

## Settings

Settings live in `$XDG_CONFIG_HOME/vted/vtedrc`
(`~/.config/vted/vtedrc` by default) and are read at startup:

```
set tab-width 4
set insert-spaces on
set line-numbers off
set show-tabs on
set wrap on
set wrap-indent off
set wrap-column 80
set indent-guides on
set ignore-case on
set electric-brackets off
set bracket-color red
set theme gruvbox
```

Blank lines and `#` comments are skipped, a later line wins over an
earlier one, and a line that cannot be used is reported in the
message bar. The keys are the same things Ctrl-W, Ctrl-T, Ctrl-N,
Alt-t, Ctrl-L, Alt-i, Alt-g, Alt-c, Alt-s and Alt-( set while editing.

## Themes

A theme is a set of colors for the kinds of text the highlighter tells
apart. `set theme NAME` in the settings file picks one, and Alt-s picks
one while editing. Six come with the editor: `default` (the built-in
colors), `gruvbox`, `nord`, `dracula`, `solarized-dark` and `light`.

Your own live in `$XDG_CONFIG_HOME/vted/themes/NAME.theme`
(`~/.config/vted/themes/NAME.theme` by default), and a file there
stands in for a built-in theme of the same name. A theme names only
what it changes, one color per line:

```
# ~/.config/vted/themes/mine.theme
color keyword  #fb4934
color string   green
color comment  245
color match    #7c5e10
```

A color is `#rrggbb`, `#rgb`, a name (black, red, green, yellow, blue,
magenta, cyan, white, gray, orange) or a number from 0 to 255 of the
256-color palette. Whatever way it is written, it reaches the terminal
in the richest form it understands: truecolor, the nearest color of the
256-color palette, or the nearest of the 16 terminal colors.

The classes are `comment`, `string`, `number`, `keyword`, `type`,
`preproc`, `removed`, `context`, `function`, `match`, `tab`, `indent`
and `marker`. `match` (what a search found) and `marker` (the wrap
column) are backgrounds; the rest color the text. The bracket match
keeps its own setting, `bracket-color`. Blank lines and `#` comments
are skipped, a later line wins, and the message bar reports lines that
could not be used. A theme's name is made of letters, digits, `-` and
`_`, so it can never name a file outside the themes directory.

## Keys

| Key             | Action                                           |
| --------------- | ------------------------------------------------ |
| arrows          | move the cursor (wraps across lines); a vertical
  aim that falls inside a row's indent lands past it, on
  the first non-blank                         |
| Ctrl-Left/Right | move by word (Alt-arrows and Alt-b/f work too)   |
| Home / End      | the first non-blank of the line, then its very
  start on a second press; End ends it — or of the wrapped
  screen line it sits on                             |
| PageUp/Down     | move one viewport up / down, of screen
  lines when wrapping is on                       |
| Alt-h / Alt-m / Alt-l | jump to the top, middle or bottom screen
  line of the viewport (H, M and L in vi)         |
| Alt-e / Alt-y   | scroll the viewport one screen line down / up,
  the way Ctrl-E and Ctrl-Y do in vi (those close
  the buffer and redo here)                       |
| Alt-t           | show or hide the tab characters: a shown tab
  carries a » and a trail of ·, in the gray of a comment |
| Alt-i           | keep the indent on the wrapped lines: a row
  that flows over several screen lines continues under
  its own first line's text                     |
| Alt-(           | turn the electric bracket pairs on or off: an
  opening bracket typed lays its closer in right behind
  it, the cursor between the two — same setting the rc
  file's `set electric-brackets off` answers |
| Alt-g           | dot the nesting levels the code sits in: the
  last cell of every indent a row and the rows above
  it keep open carries a ·, in a quiet slate blue  |
| Alt-c           | set the color the bracket match lights up with;
  the prompt opens with the color in place, so Enter
  keeps it and only a name changes it              |
| Alt-%           | jump to the partner of the bracket under the
  cursor (or right behind it), the way % does in vi; a partner off
  screen lands mid-viewport                         |
| Alt-n / Alt-p   | step to the match after and before the one the
  cursor sits on, the way n and N do in vi: the last search runs
  again from the cursor, wrapping around the buffer, without the
  prompt; every match keeps its light until the next key that is not
  one of the search's own turns it off              |
| Alt-s           | pick the color scheme: the prompt opens with the
  theme in use, so Enter alone loads it again (handy
  while writing a theme file) and a name changes it |
| Enter           | split the line at the cursor, keeping its indent |
| Tab             | insert a tab, shown up to the next tab stop; with
  a selection, every row the selection spans takes an indent
  step in instead — the same bytes the lone Tab inserts. In the
  Open and Save-as prompts it completes the path typed so far,
  the way the shell's Tab does: the names of the directory the
  path names that start with what follows its slash answer — a
  name that starts with a dot only a prefix that starts with
  one. One name lands whole, a directory with a slash behind it
  so the next Tab walks into it; several land the part they
  agree on, the menu the buffer picker uses listing them over
  the bottom of the viewport, a directory carrying its slash:
  the arrows and the wheel step through it, Enter or a left
  click takes a line into the answer, ESC takes the menu down
  with the prompt standing — a second ESC cancelling it the way
  it always did — and any other key takes it down, the answer
  going on and a fresh Tab listing again. A Tab that finds
  nothing, or a directory that cannot be read, says so in the
  bar until the next key |
| Shift-Tab       | outdent: one indent step comes off every row the
  selection spans — the tab a row starts with, or up to the tab
  width of its spaces — or off the row of the cursor when
  nothing is selected                                       |
| Backspace       | delete the character on the left, merge lines — and
  with the electric pairs on, the empty bracket pair the cursor
  sits inside goes as one, the way it was laid in |
| Alt-Backspace   | delete the run of blanks the cursor sits behind
  and the word before them, one undo step — back to the word's own
  start when the cursor sits inside it, the way the shells erase a
  word; at column zero the line merges into the one above |
| Alt-d           | delete the text the word move right crosses
  (Ctrl-Right, Alt-f): the run the cursor sits in and the blanks
  behind it, up to the start of the next word, one undo step; at the
  end of a line the line below joins it |
| Alt-/           | complete the word before the cursor with the
  words of the open buffers, the way dabbrev-expand does in Emacs:
  the menu the Ctrl-B picker uses lists every distinct word that
  starts with it — the words before the cursor
  and the rows above it, then the ones after it, ranked by how
  often the buffer says them, the ones it says most first, a saying
  counted under either spelling and words said as often as one
  another keeping the near order — then the other open
  buffers, the next one first around the ring, every word of theirs
  followed by its buffer's name so the menu says where it came from,
  while the words of the buffer being edited stand bare, their
  source being where you already are; a repeated Alt-/ steps through
  the words the way dabbrev-expand does in Emacs, the arrows and the
  wheel move through them, Enter or a left click takes one and the
  cursor lands behind it, ESC leaves the word as it was typed; a
  word is what the word moves cross (letters, digits, underscore,
  every script), the match folds the case — a letter of the word
  agrees with either of its cases in a candidate, the fold the
  search uses, so Foo completes foo and FOO completes it too, every
  spelling keeping its own — the word the cursor
  sits inside is not offered, and the whole pick is one undo step |
| Delete          | delete the character under the cursor            |
| Ctrl-K          | delete the whole line under the cursor           |
| Ctrl-D          | duplicate the line under the cursor, or the whole
  block it encloses when the cursor sits on a bracket (or right
  behind one) whose partner the highlight marks |
| Ctrl-/          | comment the line under the cursor, or every line the
  selection spans, and take the comment off again; the
  token of the language goes in front of the first
  non-blank, a space behind it                |
| Ctrl-Up/Down    | move the current line up or down                 |
| Shift-Arrows    | start or extend a character selection            |
| mouse           | a click places the cursor at the position drawn
under it — the gutter counting as the first text cell of its row, a
wrapped line as a screen line of its own, the tildes below the text
as the row past the end; a double click selects
the word under it, a blank run or a run of punctuation a word of
its own; the middle button pastes at the click what the last copy
or cut kept, a selection giving way first the way every paste
replaces one; holding the left button down and moving the
mouse selects from where the click went down — a pointer that runs
onto the bars at the bottom scrolling the viewport a screen line
along at a report — and the wheel scrolls
the viewport three screen lines at a notch, the way Alt-e and Alt-y
scroll one; over the Ctrl-B buffer list and the Alt-/ completion
menu a click takes the line drawn under it, the wheel stepping the
selection a line at a notch. Hold Shift for the terminal's own
selection    |
| Ctrl-C / Ctrl-X | copy or cut the selection (or the current line);
  the text is kept, and the middle button pastes it at the click  |
| Ctrl-R          | search and replace, one match at a time: the
  walk starts at the cursor and wraps around the buffer, every
  match lit the way the search lights them, the one being asked
  about inverted, the bar counting where it stands ("2 of 17");
  y replaces it, n leaves it, a replaces it and the rest in one
  pass, without asking and without stepping, q or ESC stops with
  what is done; each replacement one undo step; the prompts open
  with the last query and answer, so Enter alone repeats a
  replace as it stands                       |
| Ctrl-Z          | undo (a typing burst is one change)              |
| Ctrl-Y          | redo the last undone change                      |
| typing          | insert at the cursor (replaces the selection); with
  the electric pairs on, an opening bracket closes itself right
  away — the cursor between the two — and a closing one already
  under the cursor, its opener behind it, steps over instead of
  laying another one in; over a selection an opening bracket wraps
  it in its pair instead of taking it out, and a quote character
  wraps it in its own — the same one both halves, laying no empty
  pair when nothing is selected — the selection riding the text so
  the wrapped text answers the next key the way the bare one did |
| Ctrl-S          | save the buffer (asks for a name when unnamed)   |
| Ctrl-O          | open another file as a buffer of its own         |
| Alt-o           | open the file the cursor sits on, the way gf does
  in vi: the run of path bytes under the cursor — or right
  behind it, so the cursor at a name's end still takes it — names
  the file. A relative name stands beside the file being edited
  first, the way an #include does, and as it stands, in the working
  directory, when that one does not answer; a file that is already
  open just comes back into view, and a name that is no file at
  all — one that is not there, or a directory — says so in the bar:
  gf goes to a file that is there, it does not start one. The name
  can carry a ":line:column" tail, the way a compiler names a spot,
  and the open lands the cursor on the spot it says — a line alone
  or a line and a column, the way Ctrl-G lands one — a line past
  the end on the last one, a column past a row's end on that row's
  end. The prompts take Alt-o too: the answer as it stands names
  the file — a search's query among them — and an open ends the
  prompt with the bar saying what opened, a name that opens no
  file saying so while the prompt stands |
| Ctrl-E          | close the buffer being edited                    |
| Ctrl-B          | pick a buffer from a list of what is open        |
| Alt-, / Alt-.   | step through the open buffers (wraps around)     |
| Alt-1 .. Alt-9  | jump to the buffer with that number               |
| Ctrl-G          | go to a line number, or a line with a ":column"
  tail behind it — the way a jump tail carries one, counting from
  one the way the bar counts — landing mid-viewport, a number past
  the end on the last line, a column past a row's end on that row's
  end (ESC cancels)                            |
| Ctrl-F          | incremental search: every match lights up while
  you type and the prompt counts them, the left and right arrows step
  between matches, a match that left the viewport lands mid-viewport,
  ESC restores; the search opens with the last query, so Enter alone
  keeps its match; the up and down arrows walk the queries the
  searches have settled on, the way a shell walks the commands it was
  given — up to the older ones, down back to the newer, and past the
  newest to the query the walk began on — a recalled query searching
  at once and its matches lighting up the way a typed one's do; the
  queries the replace asks for go into the same history, and one the
  newest entry already says is not said again; Ctrl-T in the prompt
  reads the query as a regular
  expression, the way grep -E speaks one, and back as the text
  itself; Alt-o in the prompt opens the file the query names, the
  way it does at the cursor, and the search ends with the open     |
| Ctrl-N          | show or hide the line numbers                    |
| Ctrl-L          | wrap long lines over several screen lines,
  breaking at word boundaries                       |
| Ctrl-T          | toggle Tab between spaces and a tab character    |
| Ctrl-W          | set the tab width in cells (per buffer); the
  prompt opens with the width in place, so Enter keeps it and only
  the digits change it                       |
| Ctrl-Q          | quit (three extra presses when modified)         |
| Alt-z           | suspend to the shell: the screen, the keys, the
  paste and the mouse all go back the shell's way and
  the editor stops the way Ctrl-Z used to — `fg`
  brings it back, the screen whole again. A SIGTSTP
  from outside (kill -TSTP) lands the same way  |

With several buffers open, Ctrl-B lists them with their numbers and
names (a `+` marks unsaved changes): the arrows wrap around the list,
the wheel steps the selection the way they do without the wrap, a
left click takes the buffer drawn under it, Enter takes the one
under the selection, Ctrl-E closes it without leaving the list
(unsaved changes ask for three more presses first) and ESC stays
where you were.
Alt-, and Alt-. step through them and Alt-1 to Alt-9 jump to one by
number, while the status bar shows which one is being edited
("2/3, Ln 5/120, Col 12"). Ctrl-E closes the buffer being edited:
unsaved changes ask for three more presses first, the way Ctrl-Q
does, and closing the last buffer starts a fresh empty one.

## Features

New in 0.3.4:

- Electric bracket pairs: an opening bracket typed with the pairs on
  closes itself right away, `()` landing with the cursor between the
  two, and the whole pair is one undo step that folds into the typing
  around it. A closing bracket typed over the same closer already
  under the cursor — its opener behind it, the match the bracket scan
  finds — steps over instead of doubling it, and Backspace inside an
  empty pair takes both halves back out as one. Over a selection an
  opening bracket wraps it in its pair instead of taking it out:
  `(` around a selected `word` lands `(word)` with the word still
  selected, the selection riding the text, so another opener wraps
  again and a typed letter replaces what is between the halves; the
  wrap is one undo step too. On by default;
  `set electric-brackets off` in the rc file turns it off, and Alt-(
  turns it either way while editing, the bracket itself telling its
  own kind what to do

- Search history: the up and down arrows of the search prompt walk
  the queries the searches have settled on, the way a shell walks the
  commands it was given — up to the older ones, down back to the
  newer, and past the newest to the query the walk began on. The
  search opens on the last query, which stands as the newest entry,
  so the first up walks to the query before it the way vim's own /
  prompt does; a recalled query searches at once, its matches lighting
  up the way a typed one's do, and typing edits it where it stands.
  The queries the replace asks for go into the same history, and one
  the newest entry already says is not said again; the oldest falls
  off when the room runs out (64 of them)

- The file under the cursor: Alt-o opens the file the cursor sits
  on, the way gf does in vi. The run of path bytes under the cursor —
  or right behind it, so the cursor at a name's end still takes it —
  names the file: letters, digits, the marks a path carries (/ . - _
  + ~ # % =) and the bytes of a name in another script, with quotes,
  braces and parentheses ending it. A relative name stands beside the
  file being edited first, the way an #include does, and as it
  stands, in the working directory, when that one does not answer; a
  file that is already open just comes back into view, and a name
  that is no file at all says so in the bar instead of starting an
  empty buffer. The name can carry a ":line:column" tail, the way a
  compiler names the spot in a file it speaks about, and the open
  lands the cursor on the spot the tail says — `two.txt:3:7` opening
  the file and landing on its third line, seventh column, the
  column counting the cells the bar shows; the line alone lands the
  start of it

- Path completion: Tab in the Open and Save-as prompts completes the
  path typed so far, the way the shell's Tab does. The directory the
  path names answers with the names that start with what follows its
  slash — a name that starts with a dot only a prefix that starts
  with one — one landing whole, a directory with a slash behind it so
  the next Tab walks into it, and several landing the part they agree
  on first, the menu the buffer picker uses listing them for the
  choosing: the arrows and the wheel step through it, Enter or a left
  click takes a line into the answer, ESC takes the menu down with
  the prompt standing, and any other key takes it down, a fresh Tab
  listing again. A Tab that finds nothing, or a directory that cannot
  be read, says so in the bar until the next key
- Replace with confirmation: Ctrl-R asks about one match at a time
  instead of laying every occurrence in at once. The walk starts at
  the cursor and wraps around the buffer the way the search does,
  every match lit while it runs and the one being asked about
  inverted, the bar counting where it stands ("2 of 17") the way the
  search prompt counts. y replaces the match, n leaves it, a
  replaces it and every one after it in one pass, without asking
  and without stepping — the replace-all of before in one key —
  and q or ESC stops with what is done. Each replacement is one
  undo step, as before, so Ctrl-Z
  walks the whole thing back one occurrence at a time; a query with
  no matches says so in the bar instead of reporting zero

New in 0.3.3:

- Suspend to the shell: Alt-z hands the terminal back the shell's way
  (screen, cooked keys, paste and mouse) and stops the editor the way
  Ctrl-Z did before the terminal went raw; fg brings it back with the
  screen drawn whole again, and a SIGTSTP from outside lands the same
  way
- Block indent: Tab indents every row the selection spans with the
  same bytes it would insert alone — a tab character, or spaces up to
  the next tab stop — and Shift-Tab takes one step back off them, off
  the row of the cursor when nothing is selected. A row that is only
  blanks keeps none of it, the whole span is one undo step, and the
  selection and the cursor ride the text as it shifts
- Word completion: Alt-/ completes the word before the cursor with
  the words of the open buffers, the way dabbrev-expand does in
  Emacs. The menu the Ctrl-B picker uses lists every distinct word
  that starts with the one being typed — the words
  before the cursor and the rows above it, then the ones after it,
  ranked by how often the buffer says them, the ones it says most
  first, a saying counted under either spelling and words said as
  often as one another keeping the near order — then the
  other open buffers, the next one first around the ring,
  each read from its first row to its last, every word of theirs
  followed by its buffer's name while the words of the buffer being
  edited stand bare, so the menu says where each answer came from;
  a repeated Alt-/ steps through the answers the way dabbrev-expand
  does in Emacs, the arrows and the wheel move through them, Enter
  or a left click takes one, the mouse over the menu answering the
  way it answers the buffer list, and ESC leaves the word as it was
  typed. A word is what the word moves cross (letters, digits,
  underscore, every script), the match folds the case — a letter of
  the word agrees with either of its cases in a candidate, the fold
  the search uses, so Foo completes foo and FOO completes it too,
  every spelling keeping its own — the
  word the cursor sits inside is not offered, and the whole pick is
  one undo step
- Mouse support: a click places the cursor at the position the
  renderer drew under it — the number gutter, a wrapped line, a
  viewport scrolled sideways and the tildes past the end all
  included — a drag with the left button held selects from where the
  click went down (Ctrl-C, Ctrl-X, Ctrl-/ and typing take it the way
  they take the shift selections), a pointer that runs onto the bars
  scrolling the viewport a screen line along at a report, and the
  wheel scrolls the viewport three screen lines at a notch. A
  double click selects the word under it, a blank run and a run of
  punctuation words of their own; the middle button pastes at the
  click what the last copy or cut kept, a selection giving way
  first, the way every paste replaces one — "Nothing copied yet"
  in the bar when nothing has been. The
  Ctrl-B buffer list takes a left click on the buffer drawn under
  it, the wheel stepping its selection a buffer at a notch. The
  terminal is asked for the reports at startup and gives them back
  on every way out, so the shell's own selections keep working; most
  terminals also let Shift bypass the reporting while the editor
  runs
- A selection ending past the last row — the way Shift-Down onto the
  tildes below the text makes — reads as the end of that last row,
  which is what it selected all along; cutting it no longer reaches
  past the end of the rows for the tail that rides onto the top row

New in 0.3.2:

- Color themes: `set theme NAME`, or Alt-s while editing, picks the
  colors the text is drawn in. `gruvbox`, `nord`, `dracula`,
  `solarized-dark` and `light` come with the editor, and a file in
  `~/.config/vted/themes/` makes a theme of your own, with colors
  written as `#rrggbb`, a name or a 256-color palette number and
  fitted to what the terminal can show
- The search folds its case when asked: `set ignore-case on` takes
  the letters of the query for either of their cases, in Ctrl-F, its
  match counting, the highlight on screen and Ctrl-R replace all,
  which undo puts back the bytes each match really had
- The search reads its query as a POSIX extended regular expression
  when asked: Ctrl-T inside the search turns the reading over and
  `set search-regex on` opens every search that way. A pattern
  matches within a row, `^` and `$` anchoring its head and its end,
  every match standing on the boundaries of the characters, a match
  of no bytes at all never counting; a pattern that cannot stand
  says so in the bar and leaves the cursor where it was, and Ctrl-R
  fills the escapes of the replacement with the groups of the match,
  `\0` the whole match and `\1` through `\9` its parentheses
- Alt-n / Alt-p repeat the last search without the prompt, the way n
  and N do in vi: the next match after, the one before, wrapping
  around the buffer, every match keeping its light until the next key
  that is not one of the search's own turns it off
- Alt-Backspace deletes the run of blanks the cursor sits behind and
  the word before them, back to the word's own start when the cursor
  sits inside one, the way the shells erase a word; Alt-d deletes the
  text the word move right crosses; at the edges of a line the lines
  merge, and each press is one undo step

New in 0.3.1:

- Ctrl-D duplicates the whole block: a bracket under the cursor (or
  right behind one) copies every row its partner encloses, each line
  keeping its own ending, as one undo step
- The bracket match takes a color of your choosing: `set bracket-color
  red` — or green, yellow, blue, magenta, cyan or gray — lights the
  partner with that color instead of inverted video, and `invert` puts
  the video back
- Alt-% jumps to the partner of the bracket under the cursor (or the
  one right behind it), the way % does in vi: the partner the highlight
  marks, given the middle of the viewport when the jump leaves it
- The Ctrl-W prompt opens with the tab width in place, so Enter alone
  keeps the width and only the digits change it

New in 0.3.0:

- Word wrapping: long lines break at word boundaries, the cursor and
  Home/End/PageUp/PageDown walk the wrapped screen lines, and a vertical
  move keeps its wrapped column; Alt-i wraps the continuation lines
  under the row's own indent, and Alt-t marks the wrap column
- Indent guides: Alt-g dots every stop the row's indent crosses,
  following the nesting levels the row sits in rather than the tab stops
- Smart Home: a press lands on the first non-blank character, a second
  on the true start of the line
- Search lights up every match while it searches and the prompt counts
  them; Alt-e / Alt-y scroll the viewport, Alt-h / Alt-m / Alt-l jump
  the cursor across it
- Syntax for PHP, Java, C#, XML, SQL, YAML, TOML, INI, diffs,
  Dockerfiles and dotenv files, and the names functions are defined by
  color at their call sites

- Several files in one session: every file named on the command line
  opens in a buffer of its own, Ctrl-O opens another from within the
  editor, and Alt-, / Alt-. or Alt-1 to Alt-9 move between them; Ctrl-B
  picks one from a list over the bottom of the viewport. A
  buffer keeps its cursor, viewport and undo history while it waits,
  the status bar says which one is on, and opening a path that is
  already open switches to that buffer instead of opening it twice;
  Ctrl-E closes the buffer being edited — unsaved changes hold the
  close back the way they hold a quit back — and closing the last
  buffer starts a fresh empty one; unsaved changes in any buffer hold
  a quit back
- Raw terminal mode with automatic settings restore on exit
- Alternate screen buffer: your shell history stays untouched
- Open files from the command line, edit, and save them back
- Full cursor movement: arrows, Home, End, PageUp, PageDown
- Word movement with Ctrl or Alt and the arrows, or Alt-b and Alt-f; a word
  is a run of letters, digits and underscores, or a run of punctuation
- Vertical and horizontal scrolling for files of any size; long lines
  wrap over several screen lines instead with Ctrl-L, breaking at word
  boundaries so a word never straddles two lines — one longer than a
  line breaks by character — the up and down arrows then stepping over
  the wrapped lines and PageUp/PageDown paging a viewport of them, and
  a line taller than the viewport scrolling inside itself; Home lands
  on the first non-blank of the screen line the cursor sits on and a
  second press on its very start, and End ends it
- Viewport-relative jumps with Alt-h, Alt-m and Alt-l: the cursor lands
  on the start of the top, the middle or the bottom screen line of the
  viewport, the way H, M and L work in vi; a wrapped layout is walked
  line by line, so a viewport that starts inside a tall row counts from
  the line it starts on, and a jump past the end of the text lands on
  its last line
- Viewport scrolling with Alt-e and Alt-y, the letters vi spells
  Ctrl-E and Ctrl-Y (which close a buffer and redo here): the viewport
  moves one screen line at a time over the wrapped layout while the
  cursor keeps its place in the text, coming along only when the scroll
  would leave it off screen, and stopping at the first line of the text
  and where the last line, with the phantom row past the end, reaches
  the bottom of the viewport
- Insert and delete text, split lines with Enter, merge with Backspace
- Comment the line or the selection with Ctrl-/: the token of the
  language goes in front of the first non-blank, a space behind it,
  and comes off again on a second press — one undo step, the selection
  and the cursor riding the shift; a language that comments only in
  blocks, or none at all, is named in the message bar
- Delete the whole line with Ctrl-K, duplicate it with Ctrl-D, move it up
  or down with Ctrl-Up/Down; a bracket under the cursor duplicates the
  whole block its partner closes, each line keeping its own ending, as
  one undo step
- Select text with Shift-Arrows; copy or cut the selection (or the current
  line) to the system clipboard with Ctrl-C / Ctrl-X via OSC 52
- Search and replace every occurrence with Ctrl-R; each replacement is one
  undo step, and the prompts open with the last query and the last
  answer, so Enter alone repeats a replace as it stands
- Undo and redo with Ctrl-Z and Ctrl-Y; the cursor follows along, and a
  run of typing or backspace keystrokes in one place undoes as one step
- Line endings are preserved: LF, CRLF, CR and mixed files save back
  byte for byte, files without a trailing newline keep having none, and
  new lines take the ending of the line they came from
- Auto-indent: Enter starts the new line with the indent of the current one
- UTF-8 aware: the cursor and deletion move by whole characters, wide
  (CJK, emoji) and combining characters take the right number of cells
- Tabs expand to configurable tab stops (8 cells by default; Ctrl-W sets
  the width, the prompt opening with the one in place so Enter alone
  keeps it, and each buffer keeps its own; Ctrl-T toggles Tab between
  spaces up to the next stop and a
  real tab character); Alt-t shows the tabs where they sit, a » and a
  trail of · in the gray of a comment taking the same cells either way;
  control characters show as `^X` and invalid bytes
  as `�`, so nothing odd ever reaches your terminal
- Indent guides with Alt-g (`set indent-guides on`): the last cell of
  every nesting level a row sits in carries a quiet ·, its own indent
  edge and every shallower one still open above it, so the levels of
  the nesting line up as dotted rails down the screen whatever widths
  the file indents with — tabs, spaces or both; the cells they sit in
  take the same room either way, and a tab shown by Alt-t keeps its
  own » and trail
- The cursor keeps its display column across short lines when moving
  up and down, and an aim that falls inside a row's indent lands past
  it, on the first non-blank glyph, so the cursor never rests between
  the blanks — a scroll that drags the cursor along keeps the spot it
  lands on
- Line numbers in a gutter, the current one in bold; Ctrl-N hides them and
  a terminal too narrow for them drops them on its own
- The bracket the cursor sits on (or the one right behind it) has its
  partner highlighted, across as many lines as nesting takes; a match
  farther than a megabyte away counts as missing. `set bracket-color
  red` — or green, yellow, blue, magenta, cyan or gray — lights the
  partner with that color instead of inverted video, and `invert`
  puts the video back; Alt-c answers the same names while editing,
  opening with the color in place so Enter alone keeps it. Alt-% jumps
  to the partner, landing it mid-viewport when it left the screen
- Syntax highlighting picked from the file name: C, C++, Python, shell
  scripts, Makefiles, JavaScript, TypeScript, Rust, Go, JSON,
  Markdown, HTML, XML, CSS, Lua, Ruby, YAML, TOML, INI, SQL,
  Dockerfiles, C#, Java, PHP, dotenv files and diffs; the name a
  function is defined by colors at its definition sites in the C-like
  languages; truecolor when COLORTERM says so, with 256-color and
  16-color fallbacks
- Status bar with filename, line count, modified marker and the cursor's line
  and display column
- A column marker at a set width (`set wrap-column 80`): a quiet gray
  background over that text cell of every row on screen, carried by
  the glyph that crosses it and padded into the blank cells past the
  text, so it is plain where a line should end
- Message bar for status and warnings
- Incremental search (Ctrl-F): every match of the query lights up
  while it is being typed — amber behind the text, the one under the
  cursor inverted — and the prompt counts where that match stands
  among them all ("3 of 17", "no matches"); arrows step through
  them, a match that left the viewport lands it mid-viewport, the
  wrapped rows around it counted at their real heights; the search
  opens with the last query, so Enter alone keeps its match and the
  arrows step through a search made earlier; Ctrl-T reads the query
  as a POSIX extended regular expression instead of the text itself,
  the matches lit the same way, each as long as the pattern finds it
- Go to a line with Ctrl-G, a ":column" tail behind the number
  naming the column to land on; a number past the end lands on the
  last line, a column past a row's end on that row's end, and the
  spot lands mid-viewport, the tall rows of wrapping counted at
  their real heights
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
- [docs/benchmarks.md](docs/benchmarks.md) — what the editor costs to run
- [docs/vted.1](docs/vted.1) — the manual page source

| Module            | Responsibility                              |
| ----------------- | ------------------------------------------- |
| `vted_terminal`   | raw mode, terminal save/restore, fatal exit |
| `vted_keyboard`   | raw byte reading, escape sequence parsing   |
| `vted_screen`     | viewport, scrolling, rendering, bars        |
| `vted_row`        | text buffer, row editing and cell layout    |
| `vted_undo`       | the undo and redo history                   |
| `vted_utf8`       | UTF-8 decoding, boundaries and cell widths  |
| `vted_file`       | loading and saving files                    |
| `vted_syntax`     | coloring text by the language of the file   |
| `vted_config`     | the settings file                           |
| `vted_buffers`    | the open files and switching between them  |
| `vted_cli`        | the command line: options, usage, version   |
| `vted`            | the editor core: key dispatch and editing   |

## Contributing

The project is built one small step at a time — every commit does one
thing. If you pitch in, keep it that way: a change should build clean
(`make CFLAGS="-Wall -Wextra -Wconversion -Werror"` is enforced by CI)
and be one focused commit.

## License

MIT — see [LICENSE](LICENSE).

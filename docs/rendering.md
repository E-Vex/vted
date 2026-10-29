Rendering
=========

How vted turns the editor state into text on a terminal screen.

Introduction
------------

vted is a full-screen terminal application: it puts the terminal into raw
mode, takes over the alternate screen buffer, and draws every frame of the
interface itself. The main loop in `src/vted.c` calls `RefreshScreen()`
after every key press and on each idle timeout (the raw-mode read returns
after 100 ms with no input, see `docs/terminal-modes.md`), so the screen
is repainted continuously while the editor runs.

The rendering code lives in `src/vted_screen.c`. It lays out text with
helpers from `src/vted_row.c` (`RowCharWidth`, `RowCxToRx`,
`RowRxToCx`), decodes UTF-8 through `src/vted_utf8.c`, and gets syntax
classes and colors from `src/vted_syntax.c`. Terminal handling (raw mode,
signal handling, window-size queries) is in `src/vted_terminal.c`.
Details of `termios` and raw mode are not repeated here; see
`docs/terminal-modes.md`.

A row of the buffer holds raw bytes; a terminal shows cells. One byte is
not one character and one character is not always one cell: tabs expand,
control characters are shown as `^X`, combining marks occupy no cell of
their own, and some characters are two cells wide. The rendering code
keeps byte positions, display-cell positions, and terminal cursor
positions separate throughout.

The rendering cycle
-------------------

Every call to `RefreshScreen()` builds one frame in an append buffer and
ships it to the terminal:

1. `RefreshWindowSize()` asks the terminal for its size with `ioctl()`
   (`TIOCGWINSZ` on `STDOUT_FILENO`). Two bottom rows are reserved for
   the status bar and the message bar, so the text viewport is
   `screen_rows = rows - 2` and `screen_cols = cols`. The size is
   compared against the viewport the raw count produces, so it only
   reports a change when the terminal really was resized. The function
   returns -1 on error, 0 when the size is unchanged, and 1 when it
   changed.
2. If the size changed, `ScreenMarkAllDirty()` marks every text row for
   repainting.
3. `UpdateGutterWidth()` recomputes the gutter and text width
   (`text_cols = screen_cols - gutter_width`). The width depends on the
   buffer as well as the terminal — it widens when the line count
   crosses a digit boundary and collapses when line numbers are toggled
   off — so this runs every frame; a width different from the last
   frame marks everything dirty.
4. `UpdateScroll()` moves `row_offset` and `col_offset` so the cursor
   stays inside the viewport, and computes `cursor_rx`, the cell column
   the cursor is drawn at. Offsets that moved mark the whole viewport
   dirty (what every screen line shows has changed); a cursor that
   changed line marks the rows it left and entered, so the bold line
   number in the gutter follows it.
4. The cursor is hidden (`ESC[?25l`) and homed to the top-left
   (`ESC[H`) so it does not visibly jump while the frame is assembled.
5. `DrawRows()` renders the text viewport: text rows, tildes below the
   buffer, and the welcome banner for an empty buffer. Only dirty rows
   are drawn; each drawn row is placed explicitly (see below).
6. `DrawStatusBar()` renders the status bar.
7. `DrawMessageBar()` renders the message bar.
8. The terminal cursor is moved to its viewport-relative position
   (`ESC[<row>;<col>H`) and shown again (`ESC[?25h`).
9. The whole frame is written to the terminal with a single
   `write(STDOUT_FILENO, ab.data, ab.len)`.

### Dirty rows

Each buffer row has a dirty flag: 1 when what the terminal shows for
that row is stale and must be repainted, 0 when the screen already
matches the buffer. `ScreenMarkRowDirty()` marks one row,
`ScreenMarkRowsDirtyFrom()` marks one row and every row below it, and
`ScreenMarkAllDirty()` marks every row. The edit operations in
`src/vted_row.c` call these when the buffer changes, `ScreenSetSelection()`
marks the rows touched by a selection change, the search code in
`src/vted.c` marks the row of a search match, and `RefreshScreen()`
itself marks rows when the viewport moves (scrolling), when the cursor
changes line (the bold gutter number), and when the gutter width
changes. `DrawRows()` clears a row's flag after repainting it.

Before the first frame nothing has been painted yet, so marks made while
the editor starts up are dropped: `screen_active` is set to 1 in
`InitVtedEditor()` only after `ScreenMarkAllDirty()` has prepared that
first full repaint.

Rows below the end of the buffer (the tildes) and the welcome banner are
drawn every frame: they depend on nothing in the buffer, so they are not
tracked by the dirty flags. The status bar and message bar are likewise
redrawn unconditionally on every cycle.

### Placing a drawn row

Skipping a clean row leaves the terminal cursor where the last drawn row
ended, so the next drawn row cannot simply follow it. `DrawRows()` there
fore positions every row it draws with `GotoScreenRow()`, which emits
`ESC[<row>;1H` only when the cursor is not already on that screen line —
the common case of consecutive dirty rows costs nothing. The status bar
positions itself the same way, because a frame in which every text row
was clean emits nothing above it.

Such an all-clean frame — an idle tick with nothing changed — is small:
the cursor hidden and homed, the two bars, the cursor placement, the
cursor shown again. A few hundred bytes, against a full repaint of the
viewport when the buffer or the viewport moves.

### What a cycle touches

Per cycle, in order: terminal size query, gutter width, scroll
adjustment, text viewport, status bar, message bar, terminal cursor
placement. Nothing outside the screen module is rendered here: file
loading, saving, undo, and editing happen between cycles and only mark
rows dirty.

The viewport and scrolling
--------------------------

### Dimensions

- `screen_rows` — text rows visible at once: terminal rows minus two
  (the last two rows belong to the bars).
- `screen_cols` — terminal width in cells.
- `gutter_width` — cells at the left edge used by line numbers; 0 when
  line numbers are off or the terminal is too narrow.
- `text_cols` — cells left for the text: `screen_cols - gutter_width`.

The gutter holds right-aligned line numbers followed by one space. Its
width is `max(3, digits(num_rows + 1)) + 1`: at least three digits so it
does not jump when a file grows past 9 or 99 lines, and one more row
beyond the last so a line about to be created fits too. Line numbers are
hidden (`show_line_numbers == 0`) or the gutter collapses to 0 when
`cols - width < 20`. The width is recomputed every frame from the
current line count and terminal width; a change marks everything dirty,
since every row's number and text shifted left or right.

### Offsets

- `row_offset` — index of the first buffer row visible at the top of
  the viewport.
- `col_offset` — first display-cell column of the text area visible at
  the left edge.

`cursor_x` is a byte index into the current row; `cursor_y` is a buffer
row index and may equal `num_rows`, the phantom row just past the end of
the buffer (the row the cursor lands on after the last line). `cursor_rx`
is the same spot as a display-cell column within the row, recomputed
every frame by `RowCxToRx()`. `preferred_rx` is the display column
vertical moves aim for; it is -1 when unset and is forgotten by every
non-vertical key.

`UpdateScroll()` clamps the viewport around the cursor:

- vertically, so `row_offset <= cursor_y < row_offset + screen_rows`;
- horizontally, so `col_offset <= cursor_rx < col_offset + text_cols`,
  using the cursor's cell column, not its byte index.

Long lines therefore scroll horizontally: only the cells
`[col_offset, col_offset + text_cols)` of a row are emitted, and every
other row shares the same `col_offset`. A short row whose cells all fall
left of `col_offset` draws nothing at all for its text.

The terminal cursor is 1-based and lands at

    row:    cursor_y - row_offset + 1
    col:    cursor_rx - col_offset + gutter_width + 1

The `+ gutter_width` part places it inside the text area, past the line
numbers; `cursor_rx - col_offset` counts cells within that area.

Rendering a text row
--------------------

`DrawRowText()` walks a row one character at a time. For each character
at byte index `at`:

1. `Utf8NextBoundary()` finds the byte just past the character, so a
   multibyte UTF-8 sequence is never split.
2. `RowCharWidth(row, at, cell)` returns how many terminal cells the
   character takes when it starts at text-area cell `cell`.
3. The character is clipped to the visible cells
   `[col_offset, col_offset + text_cols)` and emitted.

Rows hold raw bytes, so byte offsets, Unicode code points, and terminal
cells are tracked separately:

- A tab is blank space up to the next multiple of `tab_width`
  (`VTED_TAB_STOP`, 8 by default). Its width depends on the cell where
  it starts: `tab_width - (cell % tab_width)`.
- A control character (byte `< 0x20` except tab, or byte `0x7F`) takes
  two cells and is drawn as `^X` in inverted video; DEL is `^?`. It can
  therefore never reach the terminal as a raw control code.
- A byte sequence that is not valid UTF-8, and a C1 control character
  (`U+0080`–`U+009F`), is drawn as the replacement character
  (U+FFFD, bytes `\xEF\xBF\xBD`) in one cell. Each invalid byte is its
  own one-byte character.
- A combining mark takes zero cells and rides on the cell before it; it
  is drawn only when that base cell is visible.
- An East Asian wide character or emoji takes two cells. If a viewport
  edge cuts it, the visible part is filled with blanks instead of
  showing half a glyph.
- Every other character takes one cell and its raw bytes are emitted.

Characters are emitted contiguously starting from the left edge of the
text area: nothing is written for cells left of `col_offset`, so
horizontal scrolling works by omission, not by moving the terminal
cursor around. The row ends with `ESC[K` (erase from the cursor to the
end of the line), so shorter content leaves no residue behind, followed
by `\r\n` — raw mode disables output post-processing, so the carriage
return is written explicitly (see `docs/terminal-modes.md`).

Text styling
------------

### Syntax highlighting

Each byte of the drawn row gets a class from the `HlClass` enum:
`HlNormal`, `HlComment`, `HlString`, `HlNumber`, `HlKeyword`, `HlType`,
`HlPreproc`. `SyntaxHighlightRow()` scans the row when it is drawn; the
language is chosen from the file name (extensions, or exact names such
as `Makefile`), and a block comment that crosses rows carries its state
forward, so a `/* ... */` comment stays colored across lines. Editing a
row invalidates the scanned state of that row and the rows below it
(`SyntaxInvalidate()`), and they are recolored the next time they are
drawn.

When the style changes mid-row, `AppendStyle()` emits `ESC[m` to reset
every attribute, then `ESC[7m` when the run is inverted, then the color
escape for the class. No attribute of a previous style can linger into
the next run, and the row ends with `ESC[m` when the last run was not
plain text.

Colors depend on what the terminal advertises: `COLORTERM=truecolor` or
`24bit` asks for truecolor escapes (`ESC[38;2;r;g;bm`), a `*256color*`
terminal gets 256-color escapes (`ESC[38;5;nm`), and anything else gets
the 16-color escapes (comments bright black, strings green, numbers
magenta, keywords yellow, types cyan, preprocessor lines bright blue;
normal text has no color escape).

### Selection

The editor core hands the selection to the renderer with
`ScreenSetSelection(top, left, bottom, right)` — normalized anchor and
cursor ends as byte indexes. The renderer draws the selected bytes in
inverted video: from `left` to the end of the top row, every byte of the
rows in between, and from the start of the bottom row to `right`.

### Search match

`match_row`, `match_col`, and `match_len` describe one highlighted
search match. When `match_len > 0`, the bytes `[match_col,
match_col + match_len)` of `match_row` are drawn inverted.

### Other attributes

Control characters are drawn inverted (see above), so they stand out and
cannot act on the terminal. Line numbers in the gutter are bold
(`ESC[1m`) on the cursor's line and gray (`ESC[90m`) elsewhere.

Text styling only assigns visual attributes; the features it colors
(undo, search, selection, syntax) are implemented in their own modules
and merely hand the renderer what to draw.

The status and message bars
---------------------------

### Status bar

Drawn in inverted video across the full terminal width:

    [filename] - N lines [-- MODIFIED --]

The filename is shown for at most 20 characters (`%.20s`), or `[No Name]`
for an unnamed buffer. When text exists, the right side shows
`Ln %d/%d, Col %d`: the cursor line out of the total, and the display
column the cursor is drawn at (`cursor_rx + 1`, one-based cells — after a
tab or a wide character it matches what the screen shows). When the
right side does not fit beside the file information it falls back to
`N/M`, and when even that does not fit it is omitted. The bar is padded
with spaces to the full width and closed with `ESC[m` and `\r\n`.

### Message bar

The last screen row. It is cleared with `ESC[K` and then shows the most
recent status message, clipped to `screen_cols` bytes. Messages are set
with `SetStatusMessage()` (a 256-byte buffer, `vsnprintf()`); the same
line is used by the prompts (`Ctrl-S`, `Ctrl-F`, `Ctrl-G`, `Ctrl-W`),
which rewrite it and call `RefreshScreen()` on every keystroke.

Terminal output
---------------

Escape sequences used while drawing a frame:

- `ESC[?25l` / `ESC[?25h` — hide / show the terminal cursor
  (DECTCEM), so the cursor does not visibly jump while a frame is
  assembled.
- `ESC[H` — home the cursor to the top-left corner (1-based `ESC[1;1H`).
- `ESC[<row>;<col>H` — absolute cursor position (CUP): `GotoScreenRow()`
  uses it to place each drawn row whose predecessor was skipped, the
  status bar uses it to reach its own line, and `RefreshScreen()` uses
  it for the text cursor after the bars.
- `ESC[K` — erase from the cursor to the end of the line (EL), used
  after every text row and at the start of the message bar.
- `ESC[m` — reset all graphic attributes (SGR 0).
- `ESC[7m` — inverted video (SGR 7), used for the status bar, control
  characters, the search match, and the selection.
- `ESC[1m` / `ESC[90m` — bold / bright black, for the current line
  number and the other line numbers in the gutter.
- `ESC[38;2;<r>;<g>;<b>m` / `ESC[38;5;<n>m` — truecolor and 256-color
  foreground escapes for syntax classes, chosen by `ColorMode()`.

### The append buffer

All of a frame's output is collected in one growable heap buffer
(`append_buffer_t` in `src/vted_screen.c`) and handed to the terminal
with a single `write()` call. Writing many small chunks instead would
let the terminal paint each chunk as it arrives, which shows up as
flicker. The buffer grows with `realloc()` and is freed after the write.

A single `write()` is attempted per frame; the code does not verify that
every byte was delivered or retry a short write — a failing `write()`
(-1) terminates the editor through `KillApp()`.

The alternate screen buffer (`ESC[?1049h` on startup, `ESC[?1049l` on
exit) and bracketed paste (`ESC[?2004h` / `ESC[?2004l`) are written
directly, outside the per-frame append buffer. Because raw mode disables
output post-processing, rows and the status bar are terminated with the
two-byte sequence `\r\n`; the message bar is not, so the terminal cursor
is left at the end of the message. The raw-mode setup and the reason for
the explicit carriage returns are described in `docs/terminal-modes.md`.

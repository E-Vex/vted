Terminal Rendering
==================

How a frame travels from the text buffer to the terminal screen.

The refresh cycle
-----------------

RefreshScreen() repaints the whole viewport on every call (the main
loop calls it after every key press, and about ten times per second
while idle):

1. RefreshWindowSize() queries the terminal size with ioctl() and
   stores it minus two rows, which are reserved for the status bar
   and the message bar.
2. UpdateScroll() shifts row_offset / col_offset so the cursor always
   stays inside the viewport.
3. The cursor is hidden with ESC[?25l so it does not visibly jump
   while the frame is being repainted.
4. The cursor is homed to the top-left corner with ESC[H.
5. DrawRows() renders each viewport row.
6. DrawStatusBar() and DrawMessageBar() render the two reserved rows.
7. The cursor is placed at its viewport-relative position with
   ESC[<row>;<col>H and shown again with ESC[?25h.

The append buffer
-----------------

All of the above is collected into one growable heap buffer (the
append buffer) and handed to the terminal with a single write() call.
Writing many small chunks instead would let the terminal paint each
chunk as it arrives, which shows up as flicker.

Row rendering
-------------

Rows hold raw bytes, but a terminal shows cells. DrawRowText() walks
a row one character at a time, lays each character out in cells and
clips the result to the viewport cells [col_offset, col_offset +
screen_cols). Long lines scroll horizontally instead of wrapping.

How a character is laid out (RowCharWidth()):

- A tab is blank space up to the next multiple of VTED_TAB_STOP (8)
  cells, so its width depends on the cell where it starts.
- A control character takes two cells and is drawn as ^X in inverted
  video (DEL is ^?), so it can never act on the terminal.
- A UTF-8 character takes the cells its code point needs: one, two
  for East Asian wide characters and emoji, none for combining marks,
  which ride on the cell before them.
- A byte that is not valid UTF-8, or a C1 control, is drawn as the
  replacement character (U+FFFD) in one cell.
- A wide character cut by the left or right edge of the viewport is
  replaced by blanks instead of showing half of it.

The cursor keeps a byte index (cursor_x). Every frame UpdateScroll()
converts it to the cell column it is drawn at (cursor_rx, via
RowCxToRx()) and scrolls by that column. Vertical moves aim for the
remembered display column (preferred_rx) and map it back to a byte
index with RowRxToCx().

Rows past the end of the text render a tilde, except the middle row
of an empty buffer which carries the welcome banner (clipped to the
terminal width). Each row is followed by ESC[K, which erases from
the cursor to the end of the line, so shorter content leaves no
residue behind. The row holding the active search match draws the
match itself in inverted video (ESC[7m ... ESC[m).

The bars
--------

The status bar uses inverted video across the full terminal width:
filename (or [No Name]), the line count, a MODIFIED marker when the
buffer has unsaved changes, and the cursor line out of the total on
the right. The message bar is the last screen row and shows the most
recent status message set via SetStatusMessage().

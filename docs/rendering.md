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

Every viewport row is drawn as a slice of its buffer row:

- Text rows render the bytes [col_offset, col_offset + screen_cols),
  clipped to the actual row length. Long lines scroll horizontally
  instead of wrapping.
- Rows past the end of the text render a tilde, except the middle row
  of an empty buffer which carries the welcome banner.
- Each row is followed by ESC[K, which erases from the cursor to the
  end of the line, so shorter content leaves no residue behind.
- The row holding the active search match renders in three segments
  with the match itself wrapped in ESC[7m ... ESC[m (inverted video).

The bars
--------

The status bar uses inverted video across the full terminal width:
filename (or [No Name]), the line count, a MODIFIED marker when the
buffer has unsaved changes, and the cursor line out of the total on
the right. The message bar is the last screen row and shows the most
recent status message set via SetStatusMessage().

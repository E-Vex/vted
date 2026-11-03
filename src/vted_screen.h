#ifndef VTED_SCREEN_H
#define VTED_SCREEN_H

typedef struct
{
  int cursor_x, cursor_y; /* cursor_x is a byte index into the row */
  int cursor_rx;          /* the same spot as a display cell column, refreshed every frame */
  int preferred_rx;       /* display column vertical moves aim for, -1 when unset */

  int screen_rows;
  int screen_cols; /* width of the whole terminal in cells */

  int show_line_numbers; /* 1 to draw the line number gutter */
  int gutter_width;      /* cells at the left edge taken by line numbers, 0 when none */
  int text_cols;         /* cells left for the text: screen_cols - gutter_width */

  int row_offset; /* first buffer row visible at the top of the viewport */
  int col_offset; /* first display cell column visible at the left edge */
  int line_offset; /* wrapped lines of the top row the viewport skipped: a
                      row taller than the viewport is entered mid-row */

  int match_row; /* row of the highlighted search match, -1 when none */
  int match_col; /* column where the highlighted match starts */
  int match_len; /* length of the highlighted match in bytes */

  int tab_width;     /* cells between tab stops; RowCharWidth() uses it */
  int insert_spaces; /* 1: the Tab key inserts spaces instead of a tab */
  int show_tabs;     /* 1: tab characters are drawn as dotted runs */
  int wrap;          /* 1: long lines flow over several screen lines instead
                        of scrolling the viewport sideways */
  int wrap_column;   /* text cells that fit before the column marker is
                        drawn, 0 to hide it */
  int wrap_indent;   /* 1: the lines a row wraps onto keep the row's own
                        indent at their start */
  int indent_guides; /* 1: dot the last cell of every nesting level the
                        rows sit in */

  char status_message[256]; /* most recent message shown in the message bar */
} vted_editor_Config_t;

extern vted_editor_Config_t vted_editor_config;

int GetWindowSize(int *rows, int *cols);
int RefreshWindowSize(void);

void EnterToAlternateScreenBuffer(void);
void ExitAlternateScreenBuffer(void);

void MoveCursor(int key);

/* scrolls the viewport so the cursor is on screen: whole rows, and
   with wrapping on also the lines of a row taller than the viewport */
void UpdateScroll(void);

/* puts the cursor mid-viewport: a cursor that is already on screen
   keeps the viewport where it is, one that is not gets the middle
   of it instead of an edge */
void ScreenCenterCursor(void);

/* scrolls the viewport count screen lines down (a positive count) or
   up (a negative one) without moving the cursor through the text: the
   lines a row wraps into count as lines of their own, and the cursor
   only comes along when the scroll would leave it off screen. The
   scroll stops at the first line of the text and where the last line
   of the text, with the phantom row past the end, reaches the
   viewport's bottom */
void ScreenScroll(int count);

/* maps a screen cell to the buffer position the renderer draws under
   it, the way a click asks for it: the gutter counts as the first
   text cell of its row, a cell past the right edge as the last one
   on screen, and the tildes below the text answer with the phantom
   row past the end. Returns 1 with the row and column in *out_y and
   *out_x, 0 when the cell is over the bars or off the screen */
int ScreenHitTest(int col, int row, int *out_y, int *out_x);

void SetStatusMessage(const char *format, ...);

void RefreshScreen(void);
void InitVtedEditor(void);

/* marks a buffer row as needing a repaint on the next frame; rows
   that are not dirty keep whatever is already on the terminal screen,
   so a frame only repaints what actually changed */
void ScreenMarkRowDirty(int row);
void ScreenMarkRowsDirtyFrom(int row);
void ScreenMarkAllDirty(void);

/* hands the selection over from the editor core: top/left and
   bottom/right are the normalized anchor and cursor ends; a negative
   top clears it */
void ScreenSetSelection(int top, int left, int bottom, int right);

/* hands the matching bracket over the same way: the byte at row and
   col is drawn inverted until the next handover; a negative row
   clears it */
void ScreenSetBracketMatch(int row, int col);

/* hands the text the editor core is searching for over: every match
   of it on screen is drawn with a background of its own, the one the
   cursor sits on inverted as before. The text is borrowed, not
   copied, and has to stay valid until the next call; NULL, or an
   empty one, turns the highlighting off */
void ScreenSetSearchQuery(const char *query, int len);

/* shows a menu of lines over the bottom of the viewport: the line at
   the selected index is drawn inverted, and lines beyond what fits
   are the last ones. The lines are borrowed, not copied, and have to
   stay valid until the next call; a count of 0 (or NULL) turns the
   menu off again */
void ScreenShowMenu(const char *const *lines, int count, int selected);

/* the line of that menu the screen row is drawing, counted among the
   lines handed over by ScreenShowMenu(), -1 when the row is not
   covered by it: the menu holds the bottom rows of the viewport, and
   lines beyond what fits are the last ones, the way the frame draws
   them */
int ScreenMenuLineAt(int row);

/* gives back the memory the module holds between frames */
void ScreenFree(void);

#endif


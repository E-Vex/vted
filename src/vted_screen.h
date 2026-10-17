#ifndef VTED_SCREEN_H
#define VTED_SCREEN_H

typedef struct
{
  int cursor_x, cursor_y; /* cursor_x is a byte index into the row */
  int cursor_rx;          /* the same spot as a display cell column, refreshed every frame */
  int preferred_rx;       /* display column vertical moves aim for, -1 when unset */

  int screen_rows;
  int screen_cols;

  int row_offset; /* first buffer row visible at the top of the viewport */
  int col_offset; /* first display cell column visible at the left edge */

  int match_row; /* row of the highlighted search match, -1 when none */
  int match_col; /* column where the highlighted match starts */
  int match_len; /* length of the highlighted match in bytes */

  char status_message[256]; /* most recent message shown in the message bar */
} vted_editor_Config_t;

extern vted_editor_Config_t vted_editor_config;

int GetWindowSize(int *rows, int *cols);
int RefreshWindowSize(void);

void EnterToAlternateScreenBuffer(void);
void ExitAlternateScreenBuffer(void);

void MoveCursor(int key);

void SetStatusMessage(const char *format, ...);

void RefreshScreen(void);
void InitVtedEditor(void);

#endif
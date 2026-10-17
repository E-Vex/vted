#ifndef VTED_SCREEN_H
#define VTED_SCREEN_H

typedef struct
{
  int cursor_x, cursor_y;

  int screen_rows;
  int screen_cols;

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
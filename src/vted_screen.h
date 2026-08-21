#ifndef VTED_SCREEN_H
#define VTED_SCREEN_H

typedef struct
{
    int cursor_x;
    int cursor_y;
    int screen_rows;
    int screen_cols;
} vted_editor_Config_t;

extern vted_editor_Config_t vted_editor_config;

int GetWindowSize(int *rows, int *cols);
void InitVtedEditor(void);
void RefreshScreen(void);
void EnterToAlternateScreenBuffer(void);
void ExitAlternateScreenBuffer(void);
void RefreshWindowSize(void);
void MoveCursor(int key);

#endif
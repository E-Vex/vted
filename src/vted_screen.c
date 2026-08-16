#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"

vted_editor_Config_t vted_editor_config;

int GetWindowSize(int *rows, int *cols)
{
    struct winsize window_size;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == -1 || window_size.ws_col == 0 || window_size.ws_row == 0)
    {
        return -1;
    }

    *cols = window_size.ws_col;
    *rows = window_size.ws_row;
    return 0;
}

void InitVtedEditor(void)
{
    /* Initialize the cursor position */
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;

    if (GetWindowSize(&vted_editor_config.screen_rows, &vted_editor_config.screen_cols) == -1)
    {
        KillApp("GetWindowSize");
    }
}
void RefreshWindowSize(void)
{
    if (GetWindowSize(&vted_editor_config.screen_rows, &vted_editor_config.screen_cols) == -1)
        KillApp("GetWindowSize");
}
static void DrawRows(void)
{
    int row;
    for (row = 0; row < vted_editor_config.screen_rows; row++)
    {
        write(STDOUT_FILENO, "~", 1);
        if (row < vted_editor_config.screen_rows - 1)
        {
            write(STDOUT_FILENO, "\r\n", 2);
        }
    }
}
void RefreshScreen(void)
{
    DrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3); /* cursor back to top-left */
}
void ClearScreen(void)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}
void EnterToAlternateScreenBuffer(void)
{
    write(STDOUT_FILENO, "\x1b[?1049h", 8);
}
void ExitAlternateScreenBuffer(void)
{
    write(STDOUT_FILENO, "\x1b[?1049l", 8);
}
void MoveCursor(int key)
{
    switch (key)
    {
    case ArrowLeft:
        if (vted_editor_config.cursor_x > 0)
            vted_editor_config.cursor_x--;
        break;
    case ArrowRight:
        if (vted_editor_config.cursor_x < vted_editor_config.screen_cols - 1)
            vted_editor_config.cursor_x++;
        break;
    case ArrowUp:
        if (vted_editor_config.cursor_y > 0)
            vted_editor_config.cursor_y--;
        break;
    case ArrowDown:
        if (vted_editor_config.cursor_y < vted_editor_config.screen_rows - 1)
            vted_editor_config.cursor_y++;
        break;
    }
}
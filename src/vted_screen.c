/*===============================*/
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
/*===============================*/

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
int RefreshWindowSize(void)
{
    int pre_cols = vted_editor_config.screen_cols;
    int pre_rows = vted_editor_config.screen_rows;

    if (GetWindowSize(&vted_editor_config.screen_rows, &vted_editor_config.screen_cols) == -1)
    {
        return -1;
    }

    if (pre_cols != vted_editor_config.screen_cols || pre_rows != vted_editor_config.screen_rows)
    {
        return 0; /* no changed */
    }

    return 1; /* window size changed */
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
    /* moving the cursor logically */
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

void RefreshScreen(void)
{
    char cursor_position_buffer[32];

    int RefreshWindowSize_return = RefreshWindowSize();
    if (RefreshWindowSize_return == -1)
    {
        KillApp("GetWindowSize");
    }
    if (RefreshWindowSize_return == 1)
    {
        DrawRows();
    }

    /* display the cursor position on terminal screen */

    /* cursor_x/y + 1 here because the terminal starts from 1,1 not 0,0 */
    snprintf(cursor_position_buffer, sizeof(cursor_position_buffer), "\x1b[%d;%dH", /* ESC + [ + <row> + ; + <col> */
             vted_editor_config.cursor_y + 1, vted_editor_config.cursor_x + 1);

    /* ANSI escape sequences must be written directly to the terminal,
     bypassing any output buffering, so the cursor moves immediately */
    write(STDOUT_FILENO, cursor_position_buffer, strlen(cursor_position_buffer));
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

    DrawRows();
}
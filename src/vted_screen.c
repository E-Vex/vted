#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "vted_terminal.h"
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
    int GetWindowSize_return;

    GetWindowSize_return = GetWindowSize(&vted_editor_config.screen_rows, &vted_editor_config.screen_cols);

    if (GetWindowSize_return == -1)
    {
        KillApp("GetWindowSize");
    }
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
    write(STDOUT_FILENO, "\x1b[2J", 4); /* clear entire screen */
    write(STDOUT_FILENO, "\x1b[H", 3);  /* cursor to row 1, col 1 */

    DrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3); /* cursor back to top-left */
}
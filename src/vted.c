/*===============================*/
#include <stdio.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_file.h"
/*===============================*/

static void EditorInsertChar(int c)
{
    /* typing on the phantom row past the end starts a new line */
    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        if (AppendRow("", 0) == -1)
        {
            KillApp("AppendRow");
        }
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

    if (RowInsertChar(row, vted_editor_config.cursor_x, c) == -1)
    {
        KillApp("RowInsertChar");
    }

    vted_buffer.dirty = 1;
    vted_editor_config.cursor_x++;
}

static void EditorInsertNewline(void)
{
    if (vted_editor_config.cursor_x == 0)
    {
        /* new empty line at the cursor */
        if (InsertRow(vted_editor_config.cursor_y, "", 0) == -1)
        {
            KillApp("InsertRow");
        }
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        /* split: the tail of the row becomes the start of a new row */
        if (InsertRow(vted_editor_config.cursor_y + 1,
                      &row->chars[vted_editor_config.cursor_x],
                      row->size - vted_editor_config.cursor_x) == -1)
        {
            KillApp("InsertRow");
        }

        /* the original row keeps the head; InsertRow may have moved
           the rows array, so take a fresh pointer before shrinking */
        row = &vted_buffer.rows[vted_editor_config.cursor_y];
        row->size = vted_editor_config.cursor_x;
        row->chars[row->size] = '\0';
    }

    vted_buffer.dirty = 1;
    vted_editor_config.cursor_y++;
    vted_editor_config.cursor_x = 0;
}

int main(int argc, char *argv[])
{
    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();
    EnterToAlternateScreenBuffer();
    InitBuffer();

    if (argc >= 2)
    {
        if (OpenFile(argv[1]) == -1)
        {
            KillApp("OpenFile");
        }
    }

    InitVtedEditor();

    while (1)
    {
        int c_in = ReadKeyPress();
        if (c_in == CTRL_KEY('q') || c_in == CTRL_KEY('Q'))
        {
            ExitAlternateScreenBuffer();
            FreeBuffer();
            return 0;
        }

        if (c_in == ArrowUp || c_in == ArrowDown || c_in == ArrowLeft || c_in == ArrowRight ||
            c_in == PageUp || c_in == PageDown || c_in == HomeKey || c_in == EndKey)
        {
            MoveCursor(c_in);
        }

        /* printable characters and UTF-8 bytes land in the text */
        if ((c_in >= 32 && c_in != 127) || c_in >= 128)
        {
            EditorInsertChar(c_in);
        }

        /* Enter splits the current line at the cursor */
        if (c_in == '\r' || c_in == '\n')
        {
            EditorInsertNewline();
        }

        RefreshScreen();
    }

    return 0;
}
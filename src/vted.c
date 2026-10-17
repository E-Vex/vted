/*===============================*/
#include <stdio.h>
#include <string.h>
#include <errno.h>
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

static void EditorDelChar(void)
{
    /* deletes the character left of the cursor, or merges the
       current line into the one above when at column zero */

    if (vted_editor_config.cursor_y == 0 && vted_editor_config.cursor_x == 0)
    {
        return; /* nothing before the start of the file */
    }

    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        return; /* nothing exists past the end of the file */
    }

    if (vted_editor_config.cursor_x == 0)
    {
        /* start of a line: merge it into the line above */
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
        vted_row_t *prev = &vted_buffer.rows[vted_editor_config.cursor_y - 1];

        int prev_size = prev->size; /* cursor will land here after the merge */

        if (RowAppendString(prev, row->chars, row->size) == -1)
        {
            KillApp("RowAppendString");
        }

        RemoveRow(vted_editor_config.cursor_y);

        vted_editor_config.cursor_y--;
        vted_editor_config.cursor_x = prev_size;
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        if (RowRemoveChar(row, vted_editor_config.cursor_x - 1) == -1)
        {
            KillApp("RowRemoveChar");
        }

        vted_editor_config.cursor_x--;
    }

    vted_buffer.dirty = 1;
}

static void EditorSave(void)
{
    if (vted_buffer.filename == NULL)
    {
        SetStatusMessage("No filename, nothing to save");
        return;
    }

    if (SaveFile() == -1)
    {
        SetStatusMessage("Save failed: %s", strerror(errno));
        return;
    }

    vted_buffer.dirty = 0;
    SetStatusMessage("Saved %s", vted_buffer.filename);
}

/* pressing Ctrl-Q this many times in a row overrides unsaved changes */
#define QUIT_CONFIRMATIONS 3

static int FindMatch(const char *query, int start_row, int direction,
                     int *match_row, int *match_col)
{
    /* scans every row exactly once, starting from start_row and
       wrapping around in the given direction */
    int row_count = vted_buffer.num_rows;
    int counted;

    if (row_count == 0 || query[0] == '\0')
    {
        return -1;
    }

    int current = start_row;

    for (counted = 0; counted < row_count; counted++)
    {
        if (current < 0)
        {
            current = row_count - 1;
        }
        if (current >= row_count)
        {
            current = 0;
        }

        const char *hit = strstr(vted_buffer.rows[current].chars, query);

        if (hit != NULL)
        {
            *match_row = current;
            *match_col = (int)(hit - vted_buffer.rows[current].chars);
            return 0;
        }

        current += direction;
    }

    return -1;
}

static void EditorFind(void)
{
    char query[64];
    int query_len = 0;
    int direction = 1; /* 1 forward, -1 backward */
    int last_match_row = -1;

    /* remember where we were, ESC puts everything back */
    int saved_x = vted_editor_config.cursor_x;
    int saved_y = vted_editor_config.cursor_y;
    int saved_row_offset = vted_editor_config.row_offset;
    int saved_col_offset = vted_editor_config.col_offset;

    query[0] = '\0';

    while (1)
    {
        SetStatusMessage("Find: %s (ESC cancel, Enter keep, arrows next/prev)", query);
        RefreshScreen();

        int c = ReadKeyPress();

        if (c == 0)
        {
            /* idle read timeout: repaint the prompt, do not touch
               the search state or the cursor would drift forward */
            continue;
        }

        if (c == 27) /* ESC: cancel and restore the previous position */
        {
            vted_editor_config.cursor_x = saved_x;
            vted_editor_config.cursor_y = saved_y;
            vted_editor_config.row_offset = saved_row_offset;
            vted_editor_config.col_offset = saved_col_offset;
            break;
        }

        if (c == '\r' || c == '\n') /* Enter: keep the current position */
        {
            break;
        }

        if (c == ArrowRight || c == ArrowDown)
        {
            direction = 1;
        }
        else if (c == ArrowLeft || c == ArrowUp)
        {
            direction = -1;
        }
        else if (c == 127) /* backspace edits the query */
        {
            if (query_len > 0)
            {
                query[--query_len] = '\0';
                last_match_row = -1; /* restart from the cursor */
            }
        }
        else if (c >= 32 && c <= 126 && query_len < 63)
        {
            query[query_len++] = (char)c;
            query[query_len] = '\0';
            last_match_row = -1; /* restart from the cursor */
        }

        /* (re)run the search: from the last match when jumping with
           arrows, from the cursor when the query itself changed */
        int search_start;
        if (last_match_row >= 0)
        {
            search_start = last_match_row + direction;
        }
        else
        {
            search_start = vted_editor_config.cursor_y + (direction > 0 ? 0 : -1);
        }

        int match_row;
        int match_col;

        if (FindMatch(query, search_start, direction, &match_row, &match_col) == 0)
        {
            last_match_row = match_row;
            vted_editor_config.cursor_y = match_row;
            vted_editor_config.cursor_x = match_col;
        }
        else
        {
            last_match_row = -1;
        }
    }

    SetStatusMessage("");
}

int main(int argc, char *argv[])
{
    int quit_confirmations = QUIT_CONFIRMATIONS;

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
            if (vted_buffer.dirty != 0 && quit_confirmations > 0)
            {
                SetStatusMessage("WARNING! Unsaved changes. Press Ctrl-Q %d more times to quit.",
                                 quit_confirmations);
                quit_confirmations--;
            }
            else
            {
                ExitAlternateScreenBuffer();
                FreeBuffer();
                return 0;
            }
        }
        else
        {
            /* any other key restarts the quit confirmation count */
            quit_confirmations = QUIT_CONFIRMATIONS;

            /* one key, one action: the dispatch is a chain so special
               key codes are never mistaken for text bytes */
            if (c_in == ArrowUp || c_in == ArrowDown || c_in == ArrowLeft || c_in == ArrowRight ||
                c_in == PageUp || c_in == PageDown || c_in == HomeKey || c_in == EndKey)
            {
                MoveCursor(c_in);
            }
            else if (c_in == '\r' || c_in == '\n')
            {
                /* Enter splits the current line at the cursor */
                EditorInsertNewline();
            }
            else if (c_in == 127)
            {
                /* Backspace removes the character left of the cursor */
                EditorDelChar();
            }
            else if (c_in == DeleteKey)
            {
                /* Delete removes the character at the cursor: stepping
                   right first turns it into a delete-left operation */
                MoveCursor(ArrowRight);
                EditorDelChar();
            }
            else if (c_in == CTRL_KEY('s') || c_in == CTRL_KEY('S'))
            {
                /* Ctrl-S writes the buffer back to its file */
                EditorSave();
            }
            else if (c_in == CTRL_KEY('f') || c_in == CTRL_KEY('F'))
            {
                /* Ctrl-F searches the buffer incrementally */
                EditorFind();
            }
            else if ((c_in >= 32 && c_in <= 126) || (c_in >= 128 && c_in <= 255))
            {
                /* printable ASCII and UTF-8 bytes land in the text;
                   key codes at 1000 and above never reach this branch */
                EditorInsertChar(c_in);
            }
        }

        RefreshScreen();
    }

    return 0;
}
/*===============================*/
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_utf8.h"
#include "vted_edit.h"
/*===============================*/

void EditorInsertChar(int c)
{
    /* typing on the phantom row past the end starts a new line */
    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        /* the new line ends the way the last real one does, so a
           CRLF file keeps growing CRLF lines at the bottom */
        int eol = vted_buffer.num_rows > 0
                      ? vted_buffer.rows[vted_buffer.num_rows - 1].eol
                      : VTED_EOL_LF;

        if (AppendRow("", 0, eol) == -1)
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

void EditorInsertNewline(void)
{
    int indent = 0;

    /* the break Enter creates ends the new line, so it takes the
       ending of the row it is made from — the last real row when
       the cursor sits on the phantom one: a CRLF file splits into
       CRLF lines */
    int source = vted_editor_config.cursor_y < vted_buffer.num_rows
                     ? vted_editor_config.cursor_y
                     : vted_buffer.num_rows - 1;
    int eol = source >= 0 ? vted_buffer.rows[source].eol : VTED_EOL_LF;

    if (vted_editor_config.cursor_x == 0)
    {
        /* new empty line at the cursor */
        if (InsertRow(vted_editor_config.cursor_y, "", 0, eol) == -1)
        {
            KillApp("InsertRow");
        }
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        /* the new line starts with the blanks the current line starts
           with, but only those left of the cursor: splitting inside
           the indent must not copy more than what stays behind */
        indent = RowIndentLength(row, vted_editor_config.cursor_x);

        int tail = row->size - vted_editor_config.cursor_x;
        char *line = malloc((size_t)(indent + tail) + 1);

        if (line == NULL)
        {
            KillApp("malloc");
        }

        memcpy(line, row->chars, (size_t)indent);
        memcpy(line + indent, &row->chars[vted_editor_config.cursor_x], (size_t)tail);

        /* split: the tail of the row becomes the start of a new row */
        if (InsertRow(vted_editor_config.cursor_y + 1, line, indent + tail, eol) == -1)
        {
            KillApp("InsertRow");
        }

        free(line);

        /* the original row keeps the head; InsertRow may have moved
           the rows array, so take a fresh pointer before shrinking */
        row = &vted_buffer.rows[vted_editor_config.cursor_y];
        row->size = vted_editor_config.cursor_x;
        row->chars[row->size] = '\0';
    }

    vted_buffer.dirty = 1;
    vted_editor_config.cursor_y++;
    vted_editor_config.cursor_x = indent;
}

void EditorDelChar(void)
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
        int eol = row->eol;         /* the lower line's ending survives */

        if (RowAppendString(prev, row->chars, row->size) == -1)
        {
            KillApp("RowAppendString");
        }

        RemoveRow(vted_editor_config.cursor_y);

        /* the merged line is now terminated the way the line below
           was, not the way the one above ended before the merge */
        prev->eol = eol;

        vted_editor_config.cursor_y--;
        vted_editor_config.cursor_x = prev_size;
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        /* a character can span several bytes: remove all of them so a
           multibyte sequence is never left half deleted */
        int start = Utf8PrevBoundary(row->chars, vted_editor_config.cursor_x);
        int count = vted_editor_config.cursor_x - start;

        while (count-- > 0)
        {
            if (RowRemoveChar(row, start) == -1)
            {
                KillApp("RowRemoveChar");
            }
        }

        vted_editor_config.cursor_x = start;
    }

    vted_buffer.dirty = 1;
}

void EditorDeleteLine(void)
{
    /* Ctrl-K removes the whole line under the cursor */

    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        return; /* on the phantom row past the end: nothing to remove */
    }

    if (vted_buffer.num_rows == 1)
    {
        /* the only line of the file is emptied, not removed: the
           buffer always keeps at least one row for the cursor to
           sit on */
        vted_row_t *row = &vted_buffer.rows[0];

        if (row->size == 0)
        {
            return; /* already empty: the buffer stays clean */
        }

        row->size = 0;
        row->chars[0] = '\0';
        vted_editor_config.cursor_x = 0;
    }
    else
    {
        RemoveRow(vted_editor_config.cursor_y);

        /* removing the last row lands the cursor on the new last
           one; the column must also fit inside the row it lands on */
        if (vted_editor_config.cursor_y == vted_buffer.num_rows)
        {
            vted_editor_config.cursor_y--;
        }

        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        if (vted_editor_config.cursor_x > row->size)
        {
            vted_editor_config.cursor_x = row->size;
        }
    }

    vted_buffer.dirty = 1;
}

void EditorDuplicateLine(void)
{
    /* Ctrl-D copies the line under the cursor below itself and
       moves the cursor onto the copy */

    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        return; /* on the phantom row past the end: nothing to copy */
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

    if (InsertRow(vted_editor_config.cursor_y + 1, row->chars, row->size, row->eol) == -1)
    {
        KillApp("InsertRow");
    }

    /* the copy holds the same text, so the column stays where it
       was; only the line changes */
    vted_editor_config.cursor_y++;
    vted_buffer.dirty = 1;
}

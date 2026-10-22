/*===============================*/
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_utf8.h"
#include "vted_edit.h"
#include "vted_undo.h"
/*===============================*/

void EditorInsertChar(int c)
{
    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    char byte = (char)c;
    int row_created = 0;
    int eol = VTED_EOL_LF;

    /* typing on the phantom row past the end starts a new line */
    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        /* the new line ends the way the last real one does, so a
           CRLF file keeps growing CRLF lines at the bottom */
        eol = vted_buffer.num_rows > 0
                  ? vted_buffer.rows[vted_buffer.num_rows - 1].eol
                  : VTED_EOL_LF;

        if (AppendRow("", 0, eol) == -1)
        {
            KillApp("AppendRow");
        }

        row_created = 1;
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

    if (RowInsertChar(row, vted_editor_config.cursor_x, c) == -1)
    {
        KillApp("RowInsertChar");
    }

    vted_buffer.dirty = 1;
    vted_editor_config.cursor_x++;

    /* undo sees the whole line when typing created it, and the
       single byte when it did not */
    UndoEntry entry = {
        .kind = row_created ? UndoKindInsertRow : UndoKindInsertBytes,
        .row = row_created ? vted_editor_config.cursor_y : before_y,
        .cx = before_x,
        .bytes = &byte,
        .length = 1,
        .eol = eol,
        .fold = row_created, /* typing may keep growing the new line */
        .undo_y = before_y,
        .undo_x = before_x,
        .redo_y = vted_editor_config.cursor_y,
        .redo_x = vted_editor_config.cursor_x,
    };
    UndoPushTyping(&entry);
}

void EditorInsertNewline(void)
{
    int indent = 0;
    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    char *recorded = NULL; /* the line Enter built, indent and all */
    int recorded_len = 0;

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
        RowTruncate(row, vted_editor_config.cursor_x);

        /* a replay must put the line back exactly as Enter built
           it, indent and all — the split alone would lose them */
        recorded = vted_buffer.rows[vted_editor_config.cursor_y + 1].chars;
        recorded_len = indent + tail;
    }

    vted_buffer.dirty = 1;
    vted_editor_config.cursor_y++;
    vted_editor_config.cursor_x = indent;

    /* Enter at column zero made a row appear; anywhere else it
       split the row in two. The empty row is the one kind of line
       creation whose undo entry holds no text yet — typing may fill
       it and fold in */
    UndoEntry entry = {
        .kind = before_x == 0 ? UndoKindInsertRow : UndoKindSplitRow,
        .row = before_y,
        .cx = before_x,
        .bytes = recorded,
        .length = recorded_len,
        .eol = eol,
        .indent = indent,
        .fold = before_x == 0,
        .undo_y = before_y,
        .undo_x = before_x,
        .redo_y = vted_editor_config.cursor_y,
        .redo_x = vted_editor_config.cursor_x,
    };
    UndoPush(&entry);
}

void EditorInsertText(const char *text, int length)
{
    /* the text is cleaned on its way in, into the one form the buffer
       and the undo history share: lines separated by a line feed */
    char *clean = malloc((size_t)length + 1);
    int clean_len = 0;
    int i;

    if (clean == NULL)
    {
        KillApp("malloc");
    }

    for (i = 0; i < length; i++)
    {
        unsigned char byte = (unsigned char)text[i];

        if (byte == '\r')
        {
            /* CR LF is one break, and so is a lone CR */
            if (i + 1 < length && text[i + 1] == '\n')
            {
                i++;
            }

            clean[clean_len++] = '\n';
        }
        else if (byte == '\n' || byte == '\t' || (byte >= 32 && byte != 127))
        {
            clean[clean_len++] = (char)byte;
        }
        /* any other control byte would draw as ^X, or end a row early
           when it is a NUL: it is dropped */
    }

    if (clean_len == 0)
    {
        free(clean);
        return; /* nothing left to paste: the buffer stays clean */
    }

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    int made_row = 0;

    /* the lines the paste makes end the way the line it lands on does
       (the last real one when the cursor sits past the end) */
    int source = before_y < vted_buffer.num_rows ? before_y : vted_buffer.num_rows - 1;
    int eol = source >= 0 ? vted_buffer.rows[source].eol : VTED_EOL_LF;

    /* a line break at the very end of a text pasted on the empty row
       past the end only ends its last line: the cursor goes back to
       the row past the end, the way a file read from disk leaves it,
       and no empty line appears below what was pasted */
    int ends_the_file = 0;

    /* pasting on the empty row past the end starts a new line */
    if (before_y == vted_buffer.num_rows)
    {
        if (AppendRow("", 0, eol) == -1)
        {
            KillApp("AppendRow");
        }

        made_row = 1;

        if (clean[clean_len - 1] == '\n')
        {
            clean_len--;
            ends_the_file = 1;
        }
    }

    if (InsertText(before_y, before_x, clean, clean_len, eol) == -1)
    {
        KillApp("InsertText");
    }

    /* the cursor lands right after the text: on the same row when it
       had one line, at the end of the last line when it had several */
    int lines = 0;
    int last_start = 0;

    for (i = 0; i < clean_len; i++)
    {
        if (clean[i] == '\n')
        {
            lines++;
            last_start = i + 1;
        }
    }

    if (ends_the_file)
    {
        vted_editor_config.cursor_y = before_y + lines + 1;
        vted_editor_config.cursor_x = 0;
    }
    else
    {
        vted_editor_config.cursor_y = before_y + lines;
        vted_editor_config.cursor_x = lines == 0 ? before_x + clean_len
                                                 : clean_len - last_start;
    }

    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindInsertText,
        .row = before_y,
        .cx = before_x,
        .bytes = clean,
        .length = clean_len,
        .eol = eol,
        .made_row = made_row,
        .undo_y = before_y,
        .undo_x = before_x,
        .redo_y = vted_editor_config.cursor_y,
        .redo_x = vted_editor_config.cursor_x,
    };
    UndoPush(&entry);

    free(clean);
}

void EditorDelChar(void)
{
    /* deletes the character left of the cursor, or merges the
       current line into the one above when at column zero */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;

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
        int prev_eol = prev->eol;   /* the one the merge replaces */

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

        vted_buffer.dirty = 1;

        UndoEntry entry = {
            .kind = UndoKindMergeRows,
            .row = before_y - 1,
            .cx = prev_size,
            .eol = eol,
            .prev_eol = prev_eol,
            .undo_y = before_y,
            .undo_x = 0,
            .redo_y = vted_editor_config.cursor_y,
            .redo_x = vted_editor_config.cursor_x,
        };
        UndoPush(&entry);
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        /* a character can span several bytes: remove all of them so a
           multibyte sequence is never left half deleted */
        int start = Utf8PrevBoundary(row->chars, vted_editor_config.cursor_x);
        int count = vted_editor_config.cursor_x - start;
        char removed[4]; /* a UTF-8 character is at most four bytes */

        memcpy(removed, &row->chars[start], (size_t)count);

        while (count-- > 0)
        {
            if (RowRemoveChar(row, start) == -1)
            {
                KillApp("RowRemoveChar");
            }
        }

        vted_editor_config.cursor_x = start;

        vted_buffer.dirty = 1;

        UndoEntry entry = {
            .kind = UndoKindRemoveBytes,
            .row = before_y,
            .cx = start,
            .bytes = removed,
            .length = before_x - start,
            .undo_y = before_y,
            .undo_x = before_x,
            .redo_y = vted_editor_config.cursor_y,
            .redo_x = vted_editor_config.cursor_x,
        };
        UndoPushTyping(&entry);
    }
}

void EditorDeleteLine(void)
{
    /* Ctrl-K removes the whole line under the cursor */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;

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

        /* the text is recorded before it is cleared away */
        UndoEntry entry = {
            .kind = UndoKindReplaceRow,
            .row = 0,
            .bytes = row->chars,
            .length = row->size,
            .undo_y = before_y,
            .undo_x = before_x,
            .redo_y = 0,
            .redo_x = 0,
        };
        UndoPush(&entry);

        RowTruncate(row, 0);
        vted_editor_config.cursor_x = 0;

        vted_buffer.dirty = 1;
        return;
    }

    /* the text is held aside: the removal frees the row it lives in */
    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
    int length = row->size;
    int eol = row->eol;
    char *saved = malloc((size_t)(length > 0 ? length : 1));

    if (saved == NULL)
    {
        KillApp("malloc");
    }

    memcpy(saved, row->chars, (size_t)length);

    RemoveRow(vted_editor_config.cursor_y);

    /* removing the last row lands the cursor on the new last
       one; the column must also fit inside the row it lands on */
    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        vted_editor_config.cursor_y--;
    }

    row = &vted_buffer.rows[vted_editor_config.cursor_y];

    if (vted_editor_config.cursor_x > row->size)
    {
        vted_editor_config.cursor_x = row->size;
    }

    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindRemoveRow,
        .row = before_y,
        .bytes = saved,
        .length = length,
        .eol = eol,
        .undo_y = before_y,
        .undo_x = before_x,
        .redo_y = vted_editor_config.cursor_y,
        .redo_x = vted_editor_config.cursor_x,
    };
    UndoPush(&entry);

    free(saved);
}

void EditorDuplicateLine(void)
{
    /* Ctrl-D copies the line under the cursor below itself and
       moves the cursor onto the copy */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;

    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        return; /* on the phantom row past the end: nothing to copy */
    }

    /* InsertRow may move the rows array, so read through it only
       before the call */
    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
    char *chars = row->chars;
    int size = row->size;
    int eol = row->eol;

    if (InsertRow(vted_editor_config.cursor_y + 1, chars, size, eol) == -1)
    {
        KillApp("InsertRow");
    }

    /* the copy holds the same text, so the column stays where it
       was; only the line changes */
    vted_editor_config.cursor_y++;
    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindInsertRow,
        .row = before_y + 1,
        .bytes = chars,
        .length = size,
        .eol = eol,
        .undo_y = before_y,
        .undo_x = before_x,
        .redo_y = vted_editor_config.cursor_y,
        .redo_x = vted_editor_config.cursor_x,
    };
    UndoPush(&entry);
}

static void EditorMoveLine(int delta)
{
    /* moves the current line up (delta < 0) or down (delta > 0); the
       cursor rides the line to its new row */

    if (vted_buffer.num_rows < 2)
    {
        return; /* nothing to move it past */
    }

    int row = vted_editor_config.cursor_y;
    int target = row + delta;

    if (target < 0 || target >= vted_buffer.num_rows)
    {
        return; /* already at the edge */
    }

    vted_row_t *line = &vted_buffer.rows[row];
    int length = line->size;
    int eol = line->eol;
    char *saved = malloc((size_t)(length > 0 ? length : 1));

    if (saved == NULL)
    {
        KillApp("malloc");
    }

    memcpy(saved, line->chars, (size_t)length);

    RemoveRow(row);
    InsertRow(target, saved, length, eol);

    free(saved);

    vted_editor_config.cursor_y = target;

    if (vted_editor_config.cursor_x > vted_buffer.rows[target].size)
    {
        vted_editor_config.cursor_x = vted_buffer.rows[target].size;
    }

    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindMoveRow,
        .row = row,
        .bytes = NULL,
        .length = 0,
        .eol = eol,
        .move_delta = delta,
        .undo_y = row,
        .undo_x = vted_editor_config.cursor_x,
        .redo_y = target,
        .redo_x = vted_editor_config.cursor_x,
    };
    UndoPush(&entry);
}

void EditorMoveLineUp(void)
{
    EditorMoveLine(-1);
}

void EditorMoveLineDown(void)
{
    EditorMoveLine(+1);
}

/*==========================================================
|                    Selection                           |
|                                                          |
| The selection runs from an anchor to the cursor. The      |
| anchor is set by the first shift movement of a run; every |
| other movement clears it again.                           |
==========================================================*/

/* the anchor end of the selection; the cursor is the other end */
static int sel_active;
static int sel_anchor_y;
static int sel_anchor_x;

void EditSetAnchor(int y, int x)
{
    sel_active = 1;
    sel_anchor_y = y;
    sel_anchor_x = x;
}

void EditClearSelection(void)
{
    sel_active = 0;
}

int EditSelectionActive(void)
{
    return sel_active;
}

/* normalizes the anchor and the cursor into the top/left and
   bottom/right ends of the selection */
void EditSelectionRange(int *top, int *left, int *bottom, int *right)
{
    int cy = vted_editor_config.cursor_y;
    int cx = vted_editor_config.cursor_x;

    if (sel_anchor_y < cy ||
        (sel_anchor_y == cy && sel_anchor_x <= cx))
    {
        *top = sel_anchor_y;
        *left = sel_anchor_x;
        *bottom = cy;
        *right = cx;
    }
    else
    {
        *top = cy;
        *left = cx;
        *bottom = sel_anchor_y;
        *right = sel_anchor_x;
    }
}

/* joins the selected span with line feeds into a heap buffer the
   caller frees; NULL when there is nothing selected */
char *EditCopyText(int *out_length)
{
    int top;
    int left;
    int bottom;
    int right;
    int length = 0;
    int row;
    char *text;
    int at = 0;

    EditSelectionRange(&top, &left, &bottom, &right);

    for (row = top; row <= bottom; row++)
    {
        int start = (row == top) ? left : 0;
        int end = (row == bottom) ? right : vted_buffer.rows[row].size;

        if (end > start)
        {
            length += end - start + (row < bottom ? 1 : 0);
        }
    }

    *out_length = length;

    if (length == 0)
    {
        return NULL;
    }

    text = malloc((size_t)length + 1);

    if (text == NULL)
    {
        KillApp("malloc");
    }

    for (row = top; row <= bottom; row++)
    {
        int start = (row == top) ? left : 0;
        int end = (row == bottom) ? right : vted_buffer.rows[row].size;
        int chunk = end - start;

        if (chunk > 0)
        {
            memcpy(text + at, &vted_buffer.rows[row].chars[start], (size_t)chunk);
            at += chunk;
        }

        if (row < bottom)
        {
            text[at++] = '\n';
        }
    }

    text[length] = '\0';
    return text;
}

/* takes the selection out of the buffer as one change: the tail of
   the top row, the middle rows and the head of the bottom row vanish,
   and the cursor lands at the top/left end of what was selected */
void EditDeleteSelection(void)
{
    int top;
    int left;
    int bottom;
    int right;
    int total = 0;
    int row;
    char *removed;
    int at = 0;

    int was_active = sel_active;

    EditSelectionRange(&top, &left, &bottom, &right);
    EditClearSelection();

    if (!was_active || (top == bottom && left == right))
    {
        return;
    }

    for (row = top; row <= bottom; row++)
    {
        int start = (row == top) ? left : 0;
        int end = (row == bottom) ? right : vted_buffer.rows[row].size;

        if (end > start)
        {
            total += end - start + (row < bottom ? 1 : 0);
        }
    }

    removed = malloc((size_t)total + 1);

    if (removed == NULL)
    {
        KillApp("malloc");
    }

    for (row = top; row <= bottom; row++)
    {
        int start = (row == top) ? left : 0;
        int end = (row == bottom) ? right : vted_buffer.rows[row].size;
        int chunk = end - start;

        if (chunk > 0)
        {
            memcpy(removed + at, &vted_buffer.rows[row].chars[start], (size_t)chunk);
            at += chunk;
        }

        if (row < bottom)
        {
            removed[at++] = '\n';
        }
    }

    UndoEntry entry = {
        .kind = UndoKindRemoveText,
        .row = top,
        .cx = left,
        .bytes = removed,
        .length = total,
        .eol = vted_buffer.rows[top].eol,
        .made_row = 0,
        .undo_y = bottom,
        .undo_x = right,
        .redo_y = top,
        .redo_x = left,
    };

    UndoPush(&entry);
    free(removed);

    /* how much rides onto the top row from the bottom of the
       selection (for a single-row selection it is the top row's own
       tail); capture it before truncating the top row */
    int tail_row = (bottom == top) ? top : bottom;
    int tail_len = vted_buffer.rows[tail_row].size - right;

    RowTruncate(&vted_buffer.rows[top], left);

    if (tail_len > 0)
    {
        /* copy the tail out first: appending may reallocate the very
           buffer we are reading when top == tail_row */
        char *tail = malloc((size_t)tail_len);

        if (tail == NULL)
        {
            KillApp("malloc");
        }

        memcpy(tail, &vted_buffer.rows[tail_row].chars[right], (size_t)tail_len);
        RowAppendString(&vted_buffer.rows[top], tail, (size_t)tail_len);
        free(tail);
    }

    /* drop the rows the selection spanned */
    while (bottom-- > top)
    {
        RemoveRow(top + 1);
    }

    vted_editor_config.cursor_y = top;
    vted_editor_config.cursor_x = left;

    vted_buffer.dirty = 1;
    EditClearSelection();
}

void EditorCopy(void)
{
    /* copies the selection to the clipboard; with nothing selected,
       the whole current line goes instead */
    int length = 0;
    char *text = EditCopyText(&length);

    if (text == NULL)
    {
        if (vted_buffer.num_rows == 0)
        {
            return;
        }

        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        length = row->size + 1;
        text = malloc((size_t)length + 1);

        if (text == NULL)
        {
            KillApp("malloc");
        }

        memcpy(text, row->chars, (size_t)row->size);
        text[row->size] = '\n';
        text[length] = '\0';
    }

    TerminalCopy(text, length);
    free(text);

    SetStatusMessage("Copied %d bytes", length);
}

void EditorCut(void)
{
    if (EditSelectionActive())
    {
        int length = 0;
        char *text = EditCopyText(&length);

        if (text != NULL)
        {
            TerminalCopy(text, length);
            free(text);
        }

        EditDeleteSelection();
        SetStatusMessage("Cut %d bytes", length);
        return;
    }

    /* nothing selected: cut the whole current line */
    if (vted_editor_config.cursor_y == vted_buffer.num_rows ||
        vted_buffer.num_rows == 0)
    {
        return;
    }

    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
        int length = row->size + 1;
        char *text = malloc((size_t)length + 1);

        if (text == NULL)
        {
            KillApp("malloc");
        }

        memcpy(text, row->chars, (size_t)row->size);
        text[row->size] = '\n';
        text[length] = '\0';

        TerminalCopy(text, length);
        free(text);

        EditorDeleteLine();
        SetStatusMessage("Cut %d bytes", length);
    }
}

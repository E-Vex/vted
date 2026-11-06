/*===============================*/
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_screen.h"
#include "vted_keyboard.h"
#include "vted_row.h"
#include "vted_regex.h"
#include "vted_utf8.h"
#include "vted_syntax.h"
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

/* the electric pair: an opening bracket laid in with its closing half
   right behind it, the cursor between the two — one undoable step
   that folds into the typing around it the way a pair of keystrokes
   would. On the phantom row past the end the pair starts a new line
   the way a single character does, ending the way the last real one
   does */
void EditorInsertPair(int opener, int closer)
{
    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    char pair[2] = {(char)opener, (char)closer};
    int row_created = 0;
    int eol = VTED_EOL_LF;

    /* typing on the phantom row past the end starts a new line */
    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        eol = vted_buffer.num_rows > 0
                  ? vted_buffer.rows[vted_buffer.num_rows - 1].eol
                  : VTED_EOL_LF;

        if (AppendRow(pair, 2, eol) == -1)
        {
            KillApp("AppendRow");
        }

        row_created = 1;
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        if (RowInsertString(row, vted_editor_config.cursor_x, pair, 2) == -1)
        {
            KillApp("RowInsertString");
        }
    }

    vted_buffer.dirty = 1;

    /* the cursor lands between the two, so the next thing typed goes
       inside the pair */
    vted_editor_config.cursor_x++;

    /* undo sees the whole line when typing created it, and the pair
       when it did not */
    UndoEntry entry = {
        .kind = row_created ? UndoKindInsertRow : UndoKindInsertBytes,
        .row = row_created ? vted_editor_config.cursor_y : before_y,
        .cx = before_x,
        .bytes = pair,
        .length = 2,
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

/* takes the empty bracket pair the cursor sits inside — the opener
   left of it, the closer under it — back out as one undoable step,
   the way the electric typing laid it in as one. Returns 1 when a
   pair went, 0 when the cursor sits inside none: the caller falls
   back on the plain backspace then */
int EditorDeletePair(void)
{
    if (vted_editor_config.cursor_y >= vted_buffer.num_rows)
    {
        return 0; /* the phantom row past the end holds no pair */
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
    int x = vted_editor_config.cursor_x;

    /* the opener left of the cursor with its own closer under it */
    if (x < 1 || x >= row->size ||
        RowBracketCloser(row->chars[x - 1]) != row->chars[x])
    {
        return 0;
    }

    int before_y = vted_editor_config.cursor_y;
    char removed[2] = {row->chars[x - 1], row->chars[x]};

    if (RowRemoveChar(row, x) == -1)
    {
        KillApp("RowRemoveChar");
    }

    if (RowRemoveChar(row, x - 1) == -1)
    {
        KillApp("RowRemoveChar");
    }

    vted_editor_config.cursor_x = x - 1;
    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindRemoveBytes,
        .row = before_y,
        .cx = x - 1,
        .bytes = removed,
        .length = 2,
        .undo_y = before_y,
        .undo_x = x,
        .redo_y = vted_editor_config.cursor_y,
        .redo_x = vted_editor_config.cursor_x,
    };
    UndoPushTyping(&entry);

    return 1;
}

void EditorDeleteWordLeft(void)
{
    /* Alt-Backspace takes the run of blanks and the word before them
       on the left of the cursor in one step, the way the shells and
       readline erase a word: back to the start of the word the cursor
       sits in, or over the blanks and the word before them when it
       sits past one */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;

    if (before_y == 0 && before_x == 0)
    {
        return; /* nothing before the start of the file */
    }

    if (before_y >= vted_buffer.num_rows)
    {
        return; /* the phantom row past the end holds nothing */
    }

    if (before_x == 0)
    {
        /* the start of a line: the word left of the cursor ends on
           the line above, so the lines merge the way Backspace does */
        EditorDelChar();
        return;
    }

    vted_row_t *row = &vted_buffer.rows[before_y];
    int start = RowWordLeft(row, before_x);
    int count = before_x - start;
    char *removed = malloc((size_t)count);

    if (removed == NULL)
    {
        KillApp("malloc");
    }

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
        .redo_y = before_y,
        .redo_x = start,
    };
    /* a word at a time, not a keystroke of a burst: two presses undo
       as two steps, the way the shell takes its backspaces back */
    UndoPush(&entry);

    free(removed);
}

void EditorDeleteWordRight(void)
{
    /* Alt-d takes the text the word move right crosses, the way the
       editor's own Ctrl-Right and Alt-f cross it: the run the cursor
       sits in, then the blanks behind it, up to the start of the
       next word */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;

    if (before_y >= vted_buffer.num_rows)
    {
        return; /* the phantom row past the end holds nothing */
    }

    vted_row_t *row = &vted_buffer.rows[before_y];
    int end = RowWordRight(row, before_x);

    if (end == before_x)
    {
        /* nothing right of the cursor in the row: the word it wants
           starts on the line below, so the lines join the way the
           Delete key joins them */
        MoveCursor(ArrowRight);

        if (vted_editor_config.cursor_x != before_x ||
            vted_editor_config.cursor_y != before_y)
        {
            EditorDelChar();
        }

        return;
    }

    int count = end - before_x;
    char *removed = malloc((size_t)count);

    if (removed == NULL)
    {
        KillApp("malloc");
    }

    memcpy(removed, &row->chars[before_x], (size_t)count);

    while (count-- > 0)
    {
        if (RowRemoveChar(row, before_x) == -1)
        {
            KillApp("RowRemoveChar");
        }
    }

    /* the cursor keeps its place: what it was looking at is gone */

    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindRemoveBytes,
        .row = before_y,
        .cx = before_x,
        .bytes = removed,
        .length = end - before_x,
        .undo_y = before_y,
        .undo_x = before_x,
        .redo_y = before_y,
        .redo_x = before_x,
    };
    /* a word at a time, like the deletion on the left of the cursor */
    UndoPush(&entry);

    free(removed);
}

void EditorInsertCompletion(const char *word, int length, int prefix_len)
{
    /* the word the menu picked lands at the cursor: the prefix is
       already in the text, so only the bytes past it arrive */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    int tail_len = length - prefix_len;

    if (length <= prefix_len || prefix_len < 0)
    {
        return; /* the word extends nothing: nothing to place */
    }

    if (before_y >= vted_buffer.num_rows)
    {
        return; /* the phantom row past the end holds no prefix */
    }

    char *tail = malloc((size_t)tail_len);

    if (tail == NULL)
    {
        KillApp("malloc");
    }

    memcpy(tail, word + prefix_len, (size_t)tail_len);

    /* the cursor keeps its row; the tail of the row rides along */
    vted_row_t *row = &vted_buffer.rows[before_y];

    if (RowInsertString(row, before_x, tail, tail_len) == -1)
    {
        KillApp("RowInsertString");
    }

    vted_editor_config.cursor_x = before_x + tail_len;

    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindInsertBytes,
        .row = before_y,
        .cx = before_x,
        .bytes = tail,
        .length = tail_len,
        .undo_y = before_y,
        .undo_x = before_x,
        .redo_y = before_y,
        .redo_x = vted_editor_config.cursor_x,
    };
    /* a word at a time, the way the word deletions undo: two picks
       undo as two steps */
    UndoPush(&entry);

    free(tail);
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
    /* Ctrl-D copies the line under the cursor below itself and moves
       onto the copy; a bracket under the cursor, or right behind one,
       widens the copy to the whole block it and its partner enclose,
       the same partner the highlight marks on screen */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    int first = before_y;
    int last = before_y;
    int copies;

    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        return; /* on the phantom row past the end: nothing to copy */
    }

    /* a bracket without a partner leaves the block at the line under
       the cursor, and so does no bracket at all */
    BufferFindBracketBlock(before_y, before_x, &first, &last);

    if (BufferDuplicateRows(first, last) == -1)
    {
        KillApp("BufferDuplicateRows");
    }

    /* the copies hold the same text, so the column stays where it
       was; the cursor lands on the copy of its row */
    copies = last - first + 1;
    vted_editor_config.cursor_y += copies;
    vted_buffer.dirty = 1;

    UndoEntry entry = {
        .kind = UndoKindDuplicateRows,
        .row = first,
        .cx = last,
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

/* an end of the selection that rests on the phantom row past the
   text — where a shift or a drag onto the tildes below the last line
   leaves it — is the end of the last row: that is what it selects,
   and no reader of the range ever has to look at a row that is not
   there. An empty buffer has no last row to point at, and its only
   selection is the empty one anyway */
static void NormalizePhantomRowEnd(int *y, int *x)
{
    if (*y == vted_buffer.num_rows && vted_buffer.num_rows > 0)
    {
        *y = vted_buffer.num_rows - 1;
        *x = vted_buffer.rows[*y].size;
    }
}

/* normalizes the anchor and the cursor into the top/left and
   bottom/right ends of the selection */
void EditSelectionRange(int *top, int *left, int *bottom, int *right)
{
    int cy = vted_editor_config.cursor_y;
    int cx = vted_editor_config.cursor_x;
    int ay = sel_anchor_y;
    int ax = sel_anchor_x;

    NormalizePhantomRowEnd(&cy, &cx);
    NormalizePhantomRowEnd(&ay, &ax);

    if (ay < cy ||
        (ay == cy && ax <= cx))
    {
        *top = ay;
        *left = ax;
        *bottom = cy;
        *right = cx;
    }
    else
    {
        *top = cy;
        *left = cx;
        *bottom = ay;
        *right = ax;
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

    /* the light goes out with the selection: the callers that delete
       one mid-keystroke — a paste, a typed letter, a cut — would
       otherwise leave the span lit over whatever lands in its place */
    ScreenSetSelection(-1, 0, -1, 0);

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

/* moves a selection end past the halves a wrap laid in: the opener
   went in at the top/left end of the span, so an end on the top row
   at or past it lands behind the opener, and the closer at the
   bottom/right end, so an end beyond that boundary lands behind the
   closer. An end on the boundary itself stays before the closer: the
   closer went in behind the selection, and the boundary is where the
   selection ends */
static void ShiftPastHalves(int *y, int *x, int top, int left,
                            int bottom, int right)
{
    int shift = 0;

    if (*y == top && *x >= left)
    {
        shift++; /* behind the opener */
    }
    if (*y == bottom && *x > right)
    {
        shift++; /* behind the closer */
    }

    *x += shift;
}

int EditorWrapSelection(int opener, int closer)
{
    int top;
    int left;
    int bottom;
    int right;
    int anchor_y;
    int anchor_x;
    int cursor_y;
    int cursor_x;
    char halves[2] = {(char)opener, (char)closer};

    EditSelectionRange(&top, &left, &bottom, &right);

    /* nothing selected is nothing to wrap, and a buffer without rows
       has not even a spot for the pair: the caller lays it in at the
       cursor instead */
    if (!sel_active || vted_buffer.num_rows == 0 ||
        (top == bottom && left == right))
    {
        return 0;
    }

    /* the closer goes in at the bottom/right end first, so the opener
       landing at the top/left end cannot shift it */
    if (RowInsertChar(&vted_buffer.rows[bottom], right, closer) == -1)
    {
        KillApp("RowInsertChar");
    }

    if (RowInsertChar(&vted_buffer.rows[top], left, opener) == -1)
    {
        KillApp("RowInsertChar");
    }

    vted_buffer.dirty = 1;

    /* the selection rides the text: both of its ends move past the
       halves that went in around them, the anchor and the cursor
       keeping the ends they had */
    anchor_y = sel_anchor_y;
    anchor_x = sel_anchor_x;
    ShiftPastHalves(&anchor_y, &anchor_x, top, left, bottom, right);

    cursor_y = vted_editor_config.cursor_y;
    cursor_x = vted_editor_config.cursor_x;
    ShiftPastHalves(&cursor_y, &cursor_x, top, left, bottom, right);

    EditSetAnchor(anchor_y, anchor_x);
    vted_editor_config.cursor_y = cursor_y;
    vted_editor_config.cursor_x = cursor_x;

    /* the light follows the span it drew before the halves went in;
       the undo entry keeps the geometry as it was, before the shifts
       the halves laid into it */
    {
        int light_top;
        int light_left;
        int light_bottom;
        int light_right;

        EditSelectionRange(&light_top, &light_left,
                           &light_bottom, &light_right);
        ScreenSetSelection(light_top, light_left,
                           light_bottom, light_right);
    }

    /* undo takes both halves back out as one, the cursor landing at
       the bottom/right end of what was wrapped — the end it lands on
       undoing a deletion too */
    UndoEntry entry = {
        .kind = UndoKindWrapSelection,
        .row = top,
        .cx = left,
        .bytes = halves,
        .length = 2,
        .undo_y = bottom,
        .undo_x = right,
        .redo_y = cursor_y,
        .redo_x = cursor_x,
    };
    UndoPush(&entry);

    return 1;
}

/* the copy the editor keeps: the last text that went out to the
   clipboard, held on to so a paste of its own can come from it — the
   middle button pastes it where it is clicked. Only a copy or a cut
   fills it; a paste from the terminal does not */
static char *kept_copy = NULL;
static int kept_copy_length = 0;

/* swaps the kept copy for the text going out, so a paste of its own
   pastes what the last copy or cut carried; a copy that cannot be
   kept still goes out to the terminal */
static void KeepCopy(const char *text, int length)
{
    char *copy = malloc((size_t)length + 1);

    if (copy == NULL)
    {
        return;
    }

    memcpy(copy, text, (size_t)length);
    copy[length] = '\0';

    free(kept_copy);
    kept_copy = copy;
    kept_copy_length = length;
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

    KeepCopy(text, length);
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
            KeepCopy(text, length);
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

        KeepCopy(text, length);
        TerminalCopy(text, length);
        free(text);

        EditorDeleteLine();
        SetStatusMessage("Cut %d bytes", length);
    }
}

/* pastes the copy the editor kept at the cursor, the way a paste
   from the terminal arrives: the text goes in as it is, the whole
   of it one undo step. Returns the number of bytes laid in — 0
   when no copy has been kept yet */
int EditorPasteKeptCopy(void)
{
    if (kept_copy == NULL)
    {
        return 0;
    }

    EditorInsertText(kept_copy, kept_copy_length);
    return kept_copy_length;
}

/* gives the kept copy back on the way out, the way the exit handlers
   give the rest of the editor state */
void EditFreeKeptCopy(void)
{
    free(kept_copy);
    kept_copy = NULL;
    kept_copy_length = 0;
}

/* how a column rides the comment toggle of its row: the text past
   the row's indent shifts with the token, so a column at or past
   where the token went in moves along with it, one inside the
   indent stays put, and a column the token came off moves back, no
   further than the indent */
static int ShiftForComment(int old_indent, int old_size, int new_size, int x)
{
    if (new_size > old_size)
    {
        return x >= old_indent ? x + (new_size - old_size) : x;
    }

    if (new_size < old_size && x > old_indent)
    {
        int back = x - (old_size - new_size);

        return back > old_indent ? back : old_indent;
    }

    return x;
}

void EditorToggleComment(void)
{
    /* Ctrl-/ comments the line under the cursor — or every line the
       selection spans — with the token the language comments lines
       with, and takes the token off again when the lines carry it.
       The whole span is one undo step, and the selection and the
       cursor ride the text as it shifts */

    const char *token = SyntaxLineComment();
    const char *language = SyntaxLanguageName();
    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    int had_selection = EditSelectionActive();
    int first;
    int last;
    int changed;
    int count;
    int total;
    int at;
    int i;
    char *blob;

    if (token == NULL)
    {
        /* a language that comments only in blocks, or none at all,
           has nothing for a line comment to work with */
        SetStatusMessage(language != NULL
                             ? "No line comment in %s"
                             : "No line comment for plain text",
                         language);
        return;
    }

    if (vted_buffer.num_rows == 0 ||
        vted_editor_config.cursor_y >= vted_buffer.num_rows)
    {
        return; /* on the phantom row past the end: nothing to comment */
    }

    /* the selection says which rows, whole ones whatever part of
       them it covers; without one, the row of the cursor */
    if (had_selection)
    {
        int left;
        int right;

        EditSelectionRange(&first, &left, &last, &right);
    }
    else
    {
        first = last = vted_editor_config.cursor_y;
    }

    if (last >= vted_buffer.num_rows)
    {
        last = vted_buffer.num_rows - 1;
    }

    /* the old texts of the span, joined by newlines the way the
       undo entry wants them: a row never holds one itself. The sizes
       and the indents of the rows the cursor and the anchor sit on
       are kept along the way, so their columns can ride the shift */
    count = last - first + 1;
    total = count - 1;

    for (i = first; i <= last; i++)
    {
        total += vted_buffer.rows[i].size;
    }

    blob = malloc((size_t)(total > 0 ? total : 1));

    if (blob == NULL)
    {
        KillApp("malloc");
    }

    {
        int cursor_old_size = -1;
        int cursor_old_indent = 0;
        int anchor_old_size = -1;
        int anchor_old_indent = 0;

        at = 0;
        for (i = first; i <= last; i++)
        {
            vted_row_t *line = &vted_buffer.rows[i];

            if (i > first)
            {
                blob[at++] = '\n';
            }

            memcpy(blob + at, line->chars, (size_t)line->size);
            at += line->size;

            if (i == vted_editor_config.cursor_y)
            {
                cursor_old_size = line->size;
                cursor_old_indent = RowIndentLength(line, line->size);
            }

            if (i == sel_anchor_y)
            {
                anchor_old_size = line->size;
                anchor_old_indent = RowIndentLength(line, line->size);
            }
        }

        if (BufferToggleComment(first, last, token, SyntaxLineComment2(),
                                &changed) == -1)
        {
            KillApp("BufferToggleComment");
        }

        if (changed == 0)
        {
            /* nothing but blank rows: nothing to comment, nothing to
               take back */
            free(blob);
            SetStatusMessage("Nothing to comment");
            return;
        }

        /* the cursor rides the shift of its own row */
        if (cursor_old_size >= 0)
        {
            int new_size = vted_buffer.rows[vted_editor_config.cursor_y].size;

            vted_editor_config.cursor_x =
                ShiftForComment(cursor_old_indent, cursor_old_size,
                                new_size, vted_editor_config.cursor_x);

            if (vted_editor_config.cursor_x > new_size)
            {
                vted_editor_config.cursor_x = new_size;
            }
        }

        /* and so does the anchor end of the selection, which lives
           on to take the comment off with a second press */
        if (anchor_old_size >= 0)
        {
            int new_size = vted_buffer.rows[sel_anchor_y].size;

            sel_anchor_x = ShiftForComment(anchor_old_indent, anchor_old_size,
                                           new_size, sel_anchor_x);

            if (sel_anchor_x > new_size)
            {
                sel_anchor_x = new_size;
            }

            {
                int top;
                int left;
                int bottom;
                int right;

                EditSelectionRange(&top, &left, &bottom, &right);
                ScreenSetSelection(top, left, bottom, right);
            }
        }
    }

    {
        UndoEntry entry = {
            .kind = UndoKindCommentRows,
            .row = first,
            .cx = last,
            .bytes = blob,
            .length = total,
            .undo_y = before_y,
            .undo_x = before_x,
            .redo_y = vted_editor_config.cursor_y,
            .redo_x = vted_editor_config.cursor_x,
        };

        UndoPush(&entry);
    }

    free(blob);

    vted_buffer.dirty = 1;

    if (changed > 0)
    {
        SetStatusMessage("Commented %d line%s", changed,
                         changed == 1 ? "" : "s");
    }
    else
    {
        SetStatusMessage("Uncommented %d line%s", -changed,
                         changed == -1 ? "" : "s");
    }
}

/* how a column rides the indent that went in front of its row: every
   byte of the row moved right by what came in, so the column moves
   with the byte it sat on, no further than the row's new end */
static int ShiftForIndent(int old_size, int new_size, int x)
{
    int moved = x + (new_size - old_size);

    return moved > new_size ? new_size : moved;
}

/* and how a column rides the indent its row gave back: the bytes left
   of it moved left by what came off, and a column the removal reached
   lands at the start of the row */
static int ShiftForOutdent(int removed, int x)
{
    return x > removed ? x - removed : 0;
}

void EditorIndentSelection(void)
{
    /* Tab with a selection puts one indent step — the bytes the Tab
       key inserts — in front of every row the selection spans, whole
       rows whatever part of them the selection covers. The whole span
       is one undo step, and the selection and the cursor ride the
       text as it shifts */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    int first;
    int last;
    int left;
    int right;
    int changed;
    int count;
    int total;
    int at;
    int i;
    char *blob;
    char unit[VTED_TAB_WIDTH_MAX];
    int unit_len;

    if (vted_buffer.num_rows == 0 ||
        vted_editor_config.cursor_y >= vted_buffer.num_rows)
    {
        return; /* on the phantom row past the end: nothing to indent */
    }

    /* the selection says which rows; the dispatch calls here with one
       active, and without one the row of the cursor takes the step */
    if (EditSelectionActive())
    {
        EditSelectionRange(&first, &left, &last, &right);
    }
    else
    {
        first = last = vted_editor_config.cursor_y;
    }

    if (last >= vted_buffer.num_rows)
    {
        last = vted_buffer.num_rows - 1;
    }

    RowIndentUnit(unit, &unit_len, vted_editor_config.insert_spaces);

    /* the old texts of the span, joined by newlines the way the
       undo entry wants them: a row never holds one itself. The sizes
       of the rows the cursor and the anchor sit on are kept along
       the way, so their columns can ride the shift */
    count = last - first + 1;
    total = count - 1;

    for (i = first; i <= last; i++)
    {
        total += vted_buffer.rows[i].size;
    }

    blob = malloc((size_t)(total > 0 ? total : 1));

    if (blob == NULL)
    {
        KillApp("malloc");
    }

    {
        int cursor_old_size = -1;
        int anchor_old_size = -1;

        at = 0;
        for (i = first; i <= last; i++)
        {
            vted_row_t *line = &vted_buffer.rows[i];

            if (i > first)
            {
                blob[at++] = '\n';
            }

            memcpy(blob + at, line->chars, (size_t)line->size);
            at += line->size;

            if (i == vted_editor_config.cursor_y)
            {
                cursor_old_size = line->size;
            }

            if (i == sel_anchor_y)
            {
                anchor_old_size = line->size;
            }
        }

        if (BufferIndentRows(first, last, unit, unit_len, &changed) == -1)
        {
            KillApp("BufferIndentRows");
        }

        if (changed == 0)
        {
            /* nothing but blank rows: nothing to indent, nothing to
               take back */
            free(blob);
            SetStatusMessage("Nothing to indent");
            return;
        }

        /* the cursor rides the shift of its own row */
        if (cursor_old_size >= 0)
        {
            int new_size = vted_buffer.rows[vted_editor_config.cursor_y].size;

            vted_editor_config.cursor_x =
                ShiftForIndent(cursor_old_size, new_size,
                               vted_editor_config.cursor_x);
        }

        /* and so does the anchor end of the selection, which lives on
           to take the step back with Shift-Tab */
        if (anchor_old_size >= 0)
        {
            int new_size = vted_buffer.rows[sel_anchor_y].size;

            sel_anchor_x = ShiftForIndent(anchor_old_size, new_size,
                                          sel_anchor_x);
        }

        {
            int top;
            int bottom;

            EditSelectionRange(&top, &left, &bottom, &right);
            ScreenSetSelection(top, left, bottom, right);
        }
    }

    {
        UndoEntry entry = {
            .kind = UndoKindIndentRows,
            .row = first,
            .cx = last,
            .bytes = blob,
            .length = total,
            .undo_y = before_y,
            .undo_x = before_x,
            .redo_y = vted_editor_config.cursor_y,
            .redo_x = vted_editor_config.cursor_x,
        };

        UndoPush(&entry);
    }

    free(blob);

    vted_buffer.dirty = 1;

    SetStatusMessage("Indented %d line%s", changed, changed == 1 ? "" : "s");
}

void EditorOutdentSelection(void)
{
    /* Shift-Tab takes one indent step off every row the selection
       spans — the tab a row starts with, or else up to the tab width
       of its spaces — or off the row of the cursor when nothing is
       selected. The whole span is one undo step, and the selection
       and the cursor ride the text as it shrinks */

    int before_y = vted_editor_config.cursor_y;
    int before_x = vted_editor_config.cursor_x;
    int had_selection = EditSelectionActive();
    int first;
    int last;
    int left;
    int right;
    int changed;
    int count;
    int total;
    int at;
    int i;
    char *blob;

    if (vted_buffer.num_rows == 0 ||
        vted_editor_config.cursor_y >= vted_buffer.num_rows)
    {
        return; /* on the phantom row past the end: nothing to outdent */
    }

    /* the selection says which rows, whole ones whatever part of
       them it covers; without one, the row of the cursor */
    if (had_selection)
    {
        EditSelectionRange(&first, &left, &last, &right);
    }
    else
    {
        first = last = vted_editor_config.cursor_y;
    }

    if (last >= vted_buffer.num_rows)
    {
        last = vted_buffer.num_rows - 1;
    }

    /* the old texts of the span, joined by newlines the way the
       undo entry wants them: a row never holds one itself. The sizes
       of the rows the cursor and the anchor sit on are kept along
       the way, so their columns can ride the shrink */
    count = last - first + 1;
    total = count - 1;

    for (i = first; i <= last; i++)
    {
        total += vted_buffer.rows[i].size;
    }

    blob = malloc((size_t)(total > 0 ? total : 1));

    if (blob == NULL)
    {
        KillApp("malloc");
    }

    {
        int cursor_old_size = -1;
        int anchor_old_size = -1;

        at = 0;
        for (i = first; i <= last; i++)
        {
            vted_row_t *line = &vted_buffer.rows[i];

            if (i > first)
            {
                blob[at++] = '\n';
            }

            memcpy(blob + at, line->chars, (size_t)line->size);
            at += line->size;

            if (i == vted_editor_config.cursor_y)
            {
                cursor_old_size = line->size;
            }

            if (i == sel_anchor_y)
            {
                anchor_old_size = line->size;
            }
        }

        if (BufferOutdentRows(first, last, vted_editor_config.tab_width,
                              &changed) == -1)
        {
            KillApp("BufferOutdentRows");
        }

        if (changed == 0)
        {
            /* no row of the span carries an indent: nothing to take
               off, nothing to take back */
            free(blob);
            SetStatusMessage("Nothing to outdent");
            return;
        }

        /* the cursor rides the shrink of its own row */
        if (cursor_old_size >= 0)
        {
            int new_size = vted_buffer.rows[vted_editor_config.cursor_y].size;

            vted_editor_config.cursor_x =
                ShiftForOutdent(cursor_old_size - new_size,
                                vted_editor_config.cursor_x);

            if (vted_editor_config.cursor_x > new_size)
            {
                vted_editor_config.cursor_x = new_size;
            }
        }

        /* and so does the anchor end of the selection, which lives on
           to take the step in again with Tab */
        if (anchor_old_size >= 0)
        {
            int new_size = vted_buffer.rows[sel_anchor_y].size;

            sel_anchor_x = ShiftForOutdent(anchor_old_size - new_size,
                                           sel_anchor_x);

            if (sel_anchor_x > new_size)
            {
                sel_anchor_x = new_size;
            }
        }

        {
            int top;
            int bottom;

            EditSelectionRange(&top, &left, &bottom, &right);
            ScreenSetSelection(top, left, bottom, right);
        }
    }

    {
        UndoEntry entry = {
            .kind = UndoKindOutdentRows,
            .row = first,
            .cx = last,
            .bytes = blob,
            .length = total,
            .undo_y = before_y,
            .undo_x = before_x,
            .redo_y = vted_editor_config.cursor_y,
            .redo_x = vted_editor_config.cursor_x,
        };

        UndoPush(&entry);
    }

    free(blob);

    vted_buffer.dirty = 1;

    SetStatusMessage("Outdented %d line%s", changed, changed == 1 ? "" : "s");
}

/* splices one occurrence into a row: the bytes the match stood for go
   into the undo step, taken from the row — a folded search matches
   letters of either case, and undo has to put back the very bytes
   that were there — then the old text comes out and the new goes in
   its place. Both replaces walk their matches through here, one
   undoable step per occurrence */
static void ReplaceSpanInRow(int row_index, int at, int old_len,
                             const char *new_text, int new_len)
{
    vted_row_t *row = &vted_buffer.rows[row_index];
    char *old_text = malloc((size_t)old_len);

    if (old_text == NULL)
    {
        KillApp("malloc");
    }

    memcpy(old_text, row->chars + at, (size_t)old_len);

    UndoEntry entry = {
        .kind = UndoKindReplaceBytes,
        .row = row_index,
        .cx = at,
        .bytes = old_text,
        .length = old_len,
        .new_bytes = (char *)new_text,
        .new_length = new_len,
        .undo_y = row_index,
        .undo_x = at,
        .redo_y = row_index,
        .redo_x = at + new_len,
    };

    UndoPush(&entry);

    free(old_text);

    { /* remove the old text at `at` */
        int i;

        for (i = 0; i < old_len; i++)
        {
            RowRemoveChar(row, at);
        }
    }

    RowInsertString(row, at, new_text, new_len);
}

/* replaces every non-overlapping occurrence of query in the buffer with
   replacement, one undoable step per occurrence, and returns the number
   of replacements made. query must not contain a line break; an empty
   query replaces nothing */
int BufferReplaceAll(const char *query, const char *replacement)
{
    int count = 0;
    int query_len = (int)strlen(query);
    int replacement_len = (int)strlen(replacement);
    int row_index;

    if (query_len == 0)
    {
        return 0;
    }

    for (row_index = 0; row_index < vted_buffer.num_rows; row_index++)
    {
        vted_row_t *row = &vted_buffer.rows[row_index];
        int start = 0; /* keep searching after the last replacement */

        while (start + query_len <= row->size)
        {
            /* the search runs in the row module, so a case the
               setting folds folds here the same way */
            int hit = RowFindNext(row, start, row->size, query, query_len);

            if (hit < 0)
            {
                break;
            }

            ReplaceSpanInRow(row_index, hit, query_len, replacement,
                             replacement_len);

            count++;
            start = hit + replacement_len; /* continue after the new text */
        }
    }

    if (count > 0)
    {
        vted_buffer.dirty = 1;
    }

    return count;
}

/* writes the replacement out with the groups of the match in place
   of their escapes: \0 through \9 name the groups, \\ a backslash
   of its own, and any other letter behind a slash stands for itself.
   A group that took no part in the match writes nothing. The text
   comes back in a heap buffer the caller frees */
static char *ExpandReplacement(const char *replacement, const char *chars,
                               const vted_regex_group_t *groups)
{
    size_t room = strlen(replacement) + 1;
    int i;

    for (i = 0; i < VTED_REGEX_GROUPS; i++)
    {
        if (groups[i].start >= 0)
        {
            room += (size_t)(groups[i].end - groups[i].start);
        }
    }

    char *out = malloc(room);

    if (out == NULL)
    {
        KillApp("malloc");
    }

    size_t at = 0;

    for (i = 0; replacement[i] != '\0'; i++)
    {
        if (replacement[i] != '\\')
        {
            out[at++] = replacement[i];
            continue;
        }

        i++; /* the letter the escape speaks of */

        if (replacement[i] == '\0')
        {
            out[at++] = '\\'; /* a slash at the end stands alone */
            break;
        }

        if (replacement[i] >= '0' && replacement[i] <= '9')
        {
            int group = replacement[i] - '0';

            if (groups[group].start >= 0)
            {
                size_t len =
                    (size_t)(groups[group].end - groups[group].start);

                memcpy(out + at, chars + groups[group].start, len);
                at += len;
            }
        }
        else
        {
            out[at++] = replacement[i];
        }
    }

    out[at] = '\0';
    return out;
}

/* replaces every match of the pattern that stands compiled in the
   regex module with the replacement written out, the escapes of it
   filled with the groups of the match they name, one undoable step
   per occurrence, and returns the number of replacements made */
int BufferReplaceRegex(const char *replacement)
{
    int count = 0;
    int row_index;

    if (!RegexHasPattern())
    {
        return 0;
    }

    for (row_index = 0; row_index < vted_buffer.num_rows; row_index++)
    {
        vted_row_t *row = &vted_buffer.rows[row_index];
        int start = 0; /* keep searching after the last replacement */

        for (;;)
        {
            int match_len = 0;
            vted_regex_group_t groups[VTED_REGEX_GROUPS];
            int hit = RegexFindNext(row->chars, start, row->size,
                                    &match_len, groups);

            if (hit < 0)
            {
                break;
            }

            char *text = ExpandReplacement(replacement, row->chars, groups);
            int text_len = (int)strlen(text);

            ReplaceSpanInRow(row_index, hit, match_len, text, text_len);

            free(text);

            count++;
            start = hit + text_len; /* continue after the new text */
        }
    }

    if (count > 0)
    {
        vted_buffer.dirty = 1;
    }

    return count;
}

/* replaces the old_len bytes the row holds at `at` with new_text, one
   undoable step: the bytes that were there go into the step, taken
   from the row, so undo puts back exactly what the span held. Returns
   the number of bytes written — 0 when the new text is empty and the
   span is deleted — or -1 when the span sits outside the row */
int BufferReplaceSpan(int row_index, int at, int old_len,
                      const char *new_text)
{
    vted_row_t *row;
    int new_len;

    if (row_index < 0 || row_index >= vted_buffer.num_rows)
    {
        return -1;
    }

    row = &vted_buffer.rows[row_index];

    if (at < 0 || old_len < 1 || at + old_len > row->size)
    {
        return -1;
    }

    new_len = (int)strlen(new_text);

    ReplaceSpanInRow(row_index, at, old_len, new_text, new_len);

    vted_buffer.dirty = 1;
    return new_len;
}

/* the same splice for the pattern that stands compiled in the regex
   module: the groups of the match that starts at `at` come from the
   row again, so the walk that asks about one match at a time writes
   each one with the groups of its own — the match the caller walked
   there is the one that stands at `at`, the finder being the same.
   Returns the number of bytes written, or -1 when no match starts
   there or the span sits outside the row */
int BufferReplaceSpanRegex(int row_index, int at, int match_len,
                           const char *replacement)
{
    vted_row_t *row;
    vted_regex_group_t groups[VTED_REGEX_GROUPS];
    int found_len = 0;
    int hit;
    char *text;
    int text_len;

    if (row_index < 0 || row_index >= vted_buffer.num_rows)
    {
        return -1;
    }

    row = &vted_buffer.rows[row_index];

    if (at < 0 || match_len < 1 || at + match_len > row->size)
    {
        return -1;
    }

    hit = RegexFindNext(row->chars, at, row->size, &found_len, groups);

    if (hit != at)
    {
        return -1;
    }

    text = ExpandReplacement(replacement, row->chars, groups);
    text_len = (int)strlen(text);

    ReplaceSpanInRow(row_index, at, match_len, text, text_len);

    free(text);

    vted_buffer.dirty = 1;
    return text_len;
}

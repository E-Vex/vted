/*===============================*/
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_undo.h"
/*===============================*/

/* how many changes are remembered; the oldest are dropped first */
#define UNDO_LIMIT 200

static UndoEntry undo_stack[UNDO_LIMIT];
static int undo_count;

static UndoEntry redo_stack[UNDO_LIMIT];
static int redo_count;

static char *CopyBytes(const UndoEntry *entry)
{
    if (entry->length <= 0)
    {
        return NULL;
    }

    char *copy = malloc((size_t)entry->length);

    if (copy == NULL)
    {
        KillApp("malloc");
    }

    memcpy(copy, entry->bytes, (size_t)entry->length);
    return copy;
}

void UndoPush(UndoEntry *entry)
{
    /* a new change makes the redo history unreachable */
    while (redo_count > 0)
    {
        free(redo_stack[--redo_count].bytes);
    }

    /* at the limit the oldest change is the first to go */
    if (undo_count == UNDO_LIMIT)
    {
        free(undo_stack[0].bytes);
        memmove(&undo_stack[0], &undo_stack[1],
                (size_t)(UNDO_LIMIT - 1) * sizeof(UndoEntry));
        undo_count--;
    }

    undo_stack[undo_count] = *entry;
    undo_stack[undo_count].bytes = CopyBytes(entry);
    undo_count++;
}

void UndoReset(void)
{
    while (undo_count > 0)
    {
        free(undo_stack[--undo_count].bytes);
    }

    while (redo_count > 0)
    {
        free(redo_stack[--redo_count].bytes);
    }
}

/* places bytes inside a row, one by one, like typing them */
static void PlaceBytes(int row, int cx, const char *bytes, int length)
{
    vted_row_t *target = &vted_buffer.rows[row];
    int i;

    for (i = 0; i < length; i++)
    {
        if (RowInsertChar(target, cx + i, (unsigned char)bytes[i]) == -1)
        {
            KillApp("RowInsertChar");
        }
    }
}

/* takes bytes out of a row; the span is known, no boundaries needed */
static void TakeBytes(int row, int cx, int length)
{
    vted_row_t *target = &vted_buffer.rows[row];

    while (length-- > 0)
    {
        if (RowRemoveChar(target, cx) == -1)
        {
            KillApp("RowRemoveChar");
        }
    }
}

/* splits rows[row] at column cx: the tail moves to a new row below */
static void SplitRowAt(int row, int cx, int eol)
{
    vted_row_t *upper = &vted_buffer.rows[row];

    int tail = upper->size - cx;
    char *line = malloc((size_t)tail + 1);

    if (line == NULL)
    {
        KillApp("malloc");
    }

    memcpy(line, &upper->chars[cx], (size_t)tail);

    if (InsertRow(row + 1, line, tail, eol) == -1)
    {
        KillApp("InsertRow");
    }

    free(line);

    /* InsertRow may have moved the rows array */
    upper = &vted_buffer.rows[row];
    RowTruncate(upper, cx);
}

/* rows[row] takes the text of rows[row + 1], which disappears; the
   merged line ends the way the lower one did */
static void MergeRowIntoNext(int row)
{
    vted_row_t *upper = &vted_buffer.rows[row];
    vted_row_t *lower = &vted_buffer.rows[row + 1];
    int eol = lower->eol;

    if (RowAppendString(upper, lower->chars, lower->size) == -1)
    {
        KillApp("RowAppendString");
    }

    RemoveRow(row + 1);

    vted_buffer.rows[row].eol = eol;
}

/* puts a row's whole text back the way it was recorded */
static void RestoreRowText(const UndoEntry *entry)
{
    vted_row_t *row = &vted_buffer.rows[entry->row];

    if (RowSetText(row, entry->bytes, entry->length) == -1)
    {
        KillApp("RowSetText");
    }
}

/* replays a recorded change */
static void Apply(const UndoEntry *entry)
{
    switch (entry->kind)
    {
    case UndoKindInsertBytes:
        PlaceBytes(entry->row, entry->cx, entry->bytes, entry->length);
        break;
    case UndoKindRemoveBytes:
        TakeBytes(entry->row, entry->cx, entry->length);
        break;
    case UndoKindSplitRow:
    {
        /* the recorded line is the one Enter built, indent and all;
           restoring it verbatim is what makes a replay exact */
        if (InsertRow(entry->row + 1, entry->bytes != NULL ? entry->bytes : "",
                      entry->length, entry->eol) == -1)
        {
            KillApp("InsertRow");
        }

        /* the original row keeps whatever sits left of the split */
        RowTruncate(&vted_buffer.rows[entry->row], entry->cx);
        break;
    }
    case UndoKindMergeRows:
        MergeRowIntoNext(entry->row);
        break;
    case UndoKindInsertRow:
        if (InsertRow(entry->row, entry->bytes != NULL ? entry->bytes : "",
                      entry->length, entry->eol) == -1)
        {
            KillApp("InsertRow");
        }
        break;
    case UndoKindRemoveRow:
        RemoveRow(entry->row);
        break;
    case UndoKindReplaceRow:
    {
        RowTruncate(&vted_buffer.rows[entry->row], 0);
        break;
    }
    }
}

/* inverts a recorded change */
static void Revert(const UndoEntry *entry)
{
    switch (entry->kind)
    {
    case UndoKindInsertBytes:
        TakeBytes(entry->row, entry->cx, entry->length);
        break;
    case UndoKindRemoveBytes:
        PlaceBytes(entry->row, entry->cx, entry->bytes, entry->length);
        break;
    case UndoKindSplitRow:
    {
        /* the indent was added for the new line; merging back
           must skip it so the original row returns exactly */
        vted_row_t *upper = &vted_buffer.rows[entry->row];

        if (entry->length > entry->indent)
        {
            if (RowAppendString(upper, entry->bytes + entry->indent,
                                entry->length - entry->indent) == -1)
            {
                KillApp("RowAppendString");
            }
        }

        RemoveRow(entry->row + 1);
        break;
    }
    case UndoKindMergeRows:
    {
        /* the split puts back what the merge joined: the lower row
           ends the way the merged line does now, the upper one the
           way it did before the merge */
        int eol = vted_buffer.rows[entry->row].eol;
        SplitRowAt(entry->row, entry->cx, eol);
        vted_buffer.rows[entry->row].eol = entry->prev_eol;
        break;
    }
    case UndoKindInsertRow:
        RemoveRow(entry->row);
        break;
    case UndoKindRemoveRow:
        if (InsertRow(entry->row, entry->bytes != NULL ? entry->bytes : "",
                      entry->length, entry->eol) == -1)
        {
            KillApp("InsertRow");
        }
        break;
    case UndoKindReplaceRow:
        RestoreRowText(entry);
        break;
    }
}

int EditorUndo(void)
{
    if (undo_count == 0)
    {
        return 0;
    }

    /* the entry moves to the redo stack, bytes and all */
    UndoEntry entry = undo_stack[undo_count - 1];
    undo_count--;

    Revert(&entry);
    redo_stack[redo_count++] = entry;

    vted_editor_config.cursor_y = entry.undo_y;
    vted_editor_config.cursor_x = entry.undo_x;

    vted_buffer.dirty = 1;
    return 1;
}

int EditorRedo(void)
{
    if (redo_count == 0)
    {
        return 0;
    }

    UndoEntry entry = redo_stack[redo_count - 1];
    redo_count--;

    Apply(&entry);
    undo_stack[undo_count++] = entry;

    vted_editor_config.cursor_y = entry.redo_y;
    vted_editor_config.cursor_x = entry.redo_x;

    vted_buffer.dirty = 1;
    return 1;
}

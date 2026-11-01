/* realpath() is guarded by the X/Open extensions in glibc, not by
   ISO C11 */
#define _XOPEN_SOURCE 700
/*===============================*/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_edit.h"
#include "vted_undo.h"
#include "vted_syntax.h"
#include "vted_file.h"
#include "vted_buffers.h"
/*===============================*/

/* what belongs to one open file: its text buffer, where its cursor
   and viewport were, and its undo history while it waits */
typedef struct
{
    vted_buffer_t text;
    int cursor_x;
    int cursor_y;
    int row_offset;
    int col_offset;
    int preferred_rx;
    void *history; /* the undo history, NULL while this buffer is live */
} buffer_slot_t;

static buffer_slot_t *slots;
static int slot_count;
static int slot_capacity;

/* the index of the buffer being edited, -1 until the first one */
static int current = -1;

/* makes room for one more buffer; the new slot's fields are the
   caller's to fill */
static void ReserveSlot(void)
{
    if (slot_count < slot_capacity)
    {
        return;
    }

    int capacity = slot_capacity > 0 ? slot_capacity * 2 : 4;
    buffer_slot_t *grown = realloc(slots, (size_t)capacity * sizeof *slots);

    if (grown == NULL)
    {
        KillApp("realloc");
    }

    slots = grown;
    slot_capacity = capacity;
}

/* state that belongs to whichever buffer was live: the selection, a
   highlighted search match, the bracket highlight. None of it
   survives a buffer change */
static void ClearTransientState(void)
{
    EditClearSelection();
    ScreenSetSelection(-1, 0, -1, 0);
    vted_editor_config.match_row = -1;
    vted_editor_config.match_col = 0;
    vted_editor_config.match_len = 0;
    ScreenSetBracketMatch(-1, 0);
    ScreenMarkAllDirty();
}

/* takes the live buffer out of the editor's globals into its slot:
   the rows, the file name, the cursor and viewport, and the undo
   history. The globals are left empty, with everything they held now
   belonging to the slot */
static void SuspendCurrent(void)
{
    if (current < 0)
    {
        return; /* nothing is live yet: the editor is still starting */
    }

    slots[current].text = vted_buffer;
    slots[current].cursor_x = vted_editor_config.cursor_x;
    slots[current].cursor_y = vted_editor_config.cursor_y;
    slots[current].row_offset = vted_editor_config.row_offset;
    slots[current].col_offset = vted_editor_config.col_offset;
    slots[current].preferred_rx = vted_editor_config.preferred_rx;
    slots[current].history = UndoDetach();

    /* the buffer moved as a whole: its memory belongs to the slot now,
       so the globals are cleared, not freed */
    InitBuffer();
}

/* brings the buffer of a slot back into the editor's globals: its
   text, its cursor and viewport, and its history */
static void Install(int index)
{
    vted_buffer = slots[index].text;
    vted_editor_config.cursor_x = slots[index].cursor_x;
    vted_editor_config.cursor_y = slots[index].cursor_y;
    vted_editor_config.row_offset = slots[index].row_offset;
    vted_editor_config.col_offset = slots[index].col_offset;
    vted_editor_config.preferred_rx = slots[index].preferred_rx;
    UndoAttach(slots[index].history);
    slots[index].history = NULL;

    /* the language follows the file's name, and the rows' colors are
       worked out again from scratch */
    SyntaxSelectForFile(vted_buffer.filename);

    ClearTransientState();

    current = index;
}

/* the globals hold a freshly loaded buffer: give it a slot of its own
   and make it the live one, starting at the top of the file */
static void AdoptLoaded(void)
{
    ReserveSlot();

    slots[slot_count].text = vted_buffer;
    slots[slot_count].cursor_x = 0;
    slots[slot_count].cursor_y = 0;
    slots[slot_count].row_offset = 0;
    slots[slot_count].col_offset = 0;
    slots[slot_count].preferred_rx = -1;
    slots[slot_count].history = NULL; /* live: the history sits in the undo module */
    slot_count++;

    current = slot_count - 1;

    /* a fresh buffer starts at the top, with nothing marked */
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;
    vted_editor_config.row_offset = 0;
    vted_editor_config.col_offset = 0;
    vted_editor_config.preferred_rx = -1;

    /* the file's name tells which language its text is in */
    SyntaxSelectForFile(vted_buffer.filename);

    ClearTransientState();
}

/* brings the buffer that was live before a failed load back, and
   gives back whatever the failure left in the globals */
static int RollBackFailedOpen(void)
{
    int saved_errno = errno;

    /* a load that failed half way (memory ran out mid-file) may have
       left rows behind: they belong to no buffer now */
    FreeBuffer();

    if (current >= 0)
    {
        Install(current);
    }

    errno = saved_errno;
    return -1;
}

/* is path the same file as other: the same literal name, or two
   spellings that resolve to the same file ("x" and "./x") */
static int SameFile(const char *path, const char *other)
{
    char resolved[PATH_MAX];
    char resolved_other[PATH_MAX];

    if (other == NULL)
    {
        return 0; /* an unnamed buffer never matches a path */
    }

    if (strcmp(path, other) == 0)
    {
        return 1;
    }

    /* a path that does not exist yet (a new file) resolves to
       nothing, and stays its own buffer */
    if (realpath(path, resolved) != NULL &&
        realpath(other, resolved_other) != NULL)
    {
        return strcmp(resolved, resolved_other) == 0;
    }

    return 0;
}

/* the slot whose file is the one asked for, -1 when none matches */
static int FindOpenBuffer(const char *filename)
{
    int i;

    for (i = 0; i < slot_count; i++)
    {
        const char *other = i == current ? vted_buffer.filename
                                         : slots[i].text.filename;

        if (SameFile(filename, other))
        {
            return i;
        }
    }

    return -1;
}

int BuffersOpen(const char *filename)
{
    /* a file that is already open just comes back into view: two
       buffers of one file would race over its bytes when saving */
    int already = FindOpenBuffer(filename);

    if (already >= 0)
    {
        BuffersSwitch(already);
        return 0;
    }

    SuspendCurrent();

    /* the globals are empty now, so the file lands in a buffer of its
       own; a failure brings the previous buffer straight back */
    if (OpenFile(filename) == -1)
    {
        return RollBackFailedOpen();
    }

    AdoptLoaded();

    return 0;
}

int BuffersOpenStream(FILE *stream)
{
    SuspendCurrent();

    if (ReadStream(stream) == -1)
    {
        return RollBackFailedOpen();
    }

    AdoptLoaded();

    return 0;
}

void BuffersStartEmpty(void)
{
    if (current >= 0)
    {
        return; /* something is already open */
    }

    /* the globals hold the unnamed empty buffer the editor starts
       with: it takes a slot like any other, so switching away and
       back treats it the same */
    AdoptLoaded();
}

int BuffersSwitch(int index)
{
    if (index < 0 || index >= slot_count || index == current)
    {
        return 0;
    }

    SuspendCurrent();
    Install(index);

    return 1;
}

int BuffersCount(void)
{
    return slot_count;
}

int BuffersCurrent(void)
{
    return current;
}

const char *BuffersName(int index)
{
    if (index < 0 || index >= slot_count)
    {
        return NULL;
    }

    /* the live buffer's name lives in the globals; a suspended one's
       lives in its slot */
    return index == current ? vted_buffer.filename : slots[index].text.filename;
}

int BuffersDirty(int index)
{
    if (index < 0 || index >= slot_count)
    {
        return 0;
    }

    /* the same split for the unsaved marker */
    return index == current ? vted_buffer.dirty : slots[index].text.dirty;
}

int BuffersDirtyCount(void)
{
    int count = 0;
    int i;

    for (i = 0; i < slot_count; i++)
    {
        if (BuffersDirty(i))
        {
            count++;
        }
    }

    return count;
}

/* frees what a waiting buffer holds: its rows, its name and its undo
   history */
static void FreeSlot(int index)
{
    int row_index;

    UndoDiscard(slots[index].history);

    for (row_index = 0; row_index < slots[index].text.num_rows; row_index++)
    {
        FreeRow(&slots[index].text.rows[row_index]);
    }

    free(slots[index].text.rows);
    free(slots[index].text.filename);
}

int BuffersClose(int index)
{
    if (index < 0 || index >= slot_count)
    {
        return 0;
    }

    if (index == current)
    {
        /* the live buffer's text, name and history sit in the editor's
           globals: give those back the way the editor core does on its
           way out */
        UndoReset();
        FreeBuffer();
    }
    else
    {
        FreeSlot(index);
    }

    /* the slots above slide down over the closed one */
    memmove(&slots[index], &slots[index + 1],
            (size_t)(slot_count - index - 1) * sizeof *slots);
    slot_count--;

    if (index != current)
    {
        /* a buffer before the live one closed: the live one's index
           slid down with it, and the live one itself is untouched */
        if (index < current)
        {
            current--;
        }

        return 1;
    }

    /* the live buffer closed: nothing is live until another one takes
       its place */
    current = -1;

    if (slot_count == 0)
    {
        /* the last buffer closed: the editor goes on with a fresh empty
           one, the way it starts */
        BuffersStartEmpty();
        return 1;
    }

    /* the neighbor takes the closed one's place; the last slot's
       neighbor is the one before it */
    if (index == slot_count)
    {
        index = slot_count - 1;
    }

    Install(index);

    return 1;
}

void BuffersFreeOthers(void)
{
    int i;

    for (i = 0; i < slot_count; i++)
    {
        int row_index;

        if (i == current)
        {
            continue; /* the live buffer is freed by the editor core */
        }

        UndoDiscard(slots[i].history);

        for (row_index = 0; row_index < slots[i].text.num_rows; row_index++)
        {
            FreeRow(&slots[i].text.rows[row_index]);
        }

        free(slots[i].text.rows);
        free(slots[i].text.filename);
    }

    free(slots);
    slots = NULL;
    slot_count = 0;
    slot_capacity = 0;
    current = -1;
}

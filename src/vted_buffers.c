/*===============================*/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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

int BuffersOpen(const char *filename)
{
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

int BuffersDirtyCount(void)
{
    int count = 0;
    int i;

    for (i = 0; i < slot_count; i++)
    {
        /* the live buffer's flags live in the globals; a suspended
           one's live in its slot */
        int dirty = i == current ? vted_buffer.dirty : slots[i].text.dirty;

        if (dirty != 0)
        {
            count++;
        }
    }

    return count;
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

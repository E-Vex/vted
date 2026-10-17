/*===============================*/
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_row.h"
/*===============================*/

vted_buffer_t vted_buffer;

void InitBuffer(void)
{
    vted_buffer.rows = NULL;
    vted_buffer.num_rows = 0;
    vted_buffer.filename = NULL;
    vted_buffer.dirty = 0;
}

static int RowAlloc(vted_row_t *row, const char *chars, int size)
{
    /* size + 1: the bytes plus the terminating null, so rows can
       also be handled with plain string helpers */
    row->chars = malloc((size_t)size + 1);

    if (row->chars == NULL)
    {
        return -1;
    }

    memcpy(row->chars, chars, (size_t)size);
    row->chars[size] = '\0';
    row->size = size;
    return 0;
}

int InsertRow(int at, const char *chars, int size)
{
    if (at < 0 || at > vted_buffer.num_rows)
    {
        return -1;
    }

    /* grow the rows array by one slot */
    vted_row_t *new_rows = realloc(vted_buffer.rows, (size_t)(vted_buffer.num_rows + 1) * sizeof(vted_row_t));

    if (new_rows == NULL)
    {
        return -1;
    }

    vted_buffer.rows = new_rows;

    /* open a gap at the insertion point */
    memmove(&vted_buffer.rows[at + 1], &vted_buffer.rows[at],
            (size_t)(vted_buffer.num_rows - at) * sizeof(vted_row_t));

    if (RowAlloc(&vted_buffer.rows[at], chars, size) == -1)
    {
        return -1;
    }

    vted_buffer.num_rows++;
    return 0;
}

int AppendRow(const char *chars, int size)
{
    return InsertRow(vted_buffer.num_rows, chars, size);
}

void RemoveRow(int at)
{
    if (at < 0 || at >= vted_buffer.num_rows)
    {
        return;
    }

    FreeRow(&vted_buffer.rows[at]);

    /* close the gap left by the removed row */
    memmove(&vted_buffer.rows[at], &vted_buffer.rows[at + 1],
            (size_t)(vted_buffer.num_rows - at - 1) * sizeof(vted_row_t));

    vted_buffer.num_rows--;
}

int RowInsertChar(vted_row_t *row, int at, int c)
{
    if (at < 0 || at > row->size)
    {
        return -1;
    }

    /* room for one more byte plus the null terminator */
    char *new_chars = realloc(row->chars, (size_t)row->size + 2);

    if (new_chars == NULL)
    {
        return -1;
    }

    row->chars = new_chars;

    /* shift the tail to the right and place the new byte */
    memmove(&row->chars[at + 1], &row->chars[at], (size_t)(row->size - at));
    row->chars[at] = (char)c;
    row->size++;
    row->chars[row->size] = '\0';
    return 0;
}

int RowRemoveChar(vted_row_t *row, int at)
{
    if (at < 0 || at >= row->size)
    {
        return -1;
    }

    /* shift the tail over the removed byte */
    memmove(&row->chars[at], &row->chars[at + 1], (size_t)(row->size - at));
    row->size--;
    row->chars[row->size] = '\0';
    return 0;
}

int RowAppendString(vted_row_t *row, const char *s, int len)
{
    /* room for the appended bytes plus the null terminator */
    char *new_chars = realloc(row->chars, (size_t)row->size + (size_t)len + 1);

    if (new_chars == NULL)
    {
        return -1;
    }

    row->chars = new_chars;

    memcpy(&row->chars[row->size], s, (size_t)len);
    row->size += len;
    row->chars[row->size] = '\0';
    return 0;
}

void FreeRow(vted_row_t *row)
{
    free(row->chars);
    row->chars = NULL;
    row->size = 0;
}

void FreeBuffer(void)
{
    int row_index;

    for (row_index = 0; row_index < vted_buffer.num_rows; row_index++)
    {
        FreeRow(&vted_buffer.rows[row_index]);
    }

    free(vted_buffer.rows);

    free(vted_buffer.filename);

    InitBuffer();
}

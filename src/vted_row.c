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

int AppendRow(const char *chars, int size)
{
    /* grow the rows array by one slot */
    vted_row_t *new_rows = realloc(vted_buffer.rows, (size_t)(vted_buffer.num_rows + 1) * sizeof(vted_row_t));

    if (new_rows == NULL)
    {
        return -1;
    }

    vted_buffer.rows = new_rows;

    vted_row_t *row = &vted_buffer.rows[vted_buffer.num_rows];

    row->size = size;
    row->chars = malloc((size_t)size);

    if (row->chars == NULL)
    {
        return -1;
    }

    memcpy(row->chars, chars, (size_t)size);

    vted_buffer.num_rows++;
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

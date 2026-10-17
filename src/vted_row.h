#ifndef VTED_ROW_H
#define VTED_ROW_H

/*==========================================================
|                     Text Buffer Module                   |
|                                                          |
| Holds the document being edited: a dynamic array of      |
| text rows. Each row owns a heap buffer of raw bytes      |
| (not null-terminated) so the text may contain any       |
| byte value. The screen module renders these rows, the    |
| file module loads and stores them.                       |
==========================================================*/

typedef struct
{
    char *chars; /* heap buffer with the raw text of the row */
    int size;    /* number of bytes stored in chars */
} vted_row_t;

typedef struct
{
    vted_row_t *rows; /* dynamic array of text rows */
    int num_rows;     /* number of rows currently stored */

    char *filename; /* path of the loaded file, NULL when unnamed */
    int dirty;      /* 1 when the buffer has unsaved changes */
} vted_buffer_t;

extern vted_buffer_t vted_buffer;

void InitBuffer(void);
int AppendRow(const char *chars, int size);
void FreeRow(vted_row_t *row);
void FreeBuffer(void);

#endif

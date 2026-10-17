#ifndef VTED_ROW_H
#define VTED_ROW_H

/*==========================================================
|                     Text Buffer Module                   |
|                                                          |
| Holds the document being edited: a dynamic array of      |
| text rows. Each row owns a heap buffer of raw bytes,     |
| always null-terminated so string helpers can be used     |
| on it as well. The screen module renders these rows,     |
| the file module loads and stores them.                   |
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
int InsertRow(int at, const char *chars, int size);
void RemoveRow(int at);

int RowInsertChar(vted_row_t *row, int at, int c);
int RowRemoveChar(vted_row_t *row, int at);
int RowAppendString(vted_row_t *row, const char *s, int len);

void FreeRow(vted_row_t *row);
void FreeBuffer(void);

#endif

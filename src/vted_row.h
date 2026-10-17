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

/* a tab advances the text to the next multiple of this many cells */
#define VTED_TAB_STOP 8

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

/* Display layout. Rows hold raw bytes, but the terminal shows cells:
   a tab spans up to the next tab stop, a control character takes two
   cells (drawn as ^X) and UTF-8 characters take their own width. The
   cursor keeps byte indexes (cx), the screen needs cell columns (rx). */

/* cells taken by the character at byte index at, when it starts at
   cell column cell (a tab's width depends on where it starts) */
int RowCharWidth(const vted_row_t *row, int at, int cell);

/* cell column where the character at byte index cx starts */
int RowCxToRx(const vted_row_t *row, int cx);

/* byte index of the character that covers cell column rx; the end of
   the row when rx lies past the text */
int RowRxToCx(const vted_row_t *row, int rx);

/* number of leading blanks (spaces and tabs) of the row, looking at
   no more than the first limit bytes; the indent a new line copies */
int RowIndentLength(const vted_row_t *row, int limit);

void FreeRow(vted_row_t *row);
void FreeBuffer(void);

#endif

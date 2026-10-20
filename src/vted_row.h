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

/* how a row is terminated when the buffer is written back out */
enum
{
    VTED_EOL_LF,  /* \n, the way Unix writes text */
    VTED_EOL_CRLF, /* \r\n, the way Windows writes it */
    VTED_EOL_CR   /* a lone \r, the way classic Mac OS did */
};

typedef struct
{
    char *chars; /* heap buffer with the raw text of the row */
    int size;    /* number of bytes stored in chars */
    int eol;     /* how this row ends (VTED_EOL_*), used on save */
} vted_row_t;

typedef struct
{
    vted_row_t *rows; /* dynamic array of text rows */
    int num_rows;     /* number of rows currently stored */

    char *filename; /* path of the loaded file, NULL when unnamed */
    int dirty;      /* 1 when the buffer has unsaved changes */

    /* 1 when the file ends with a line terminator; the last row's
       ending is written out only then, so a file with no trailing
       newline keeps having none */
    int final_newline;
} vted_buffer_t;

extern vted_buffer_t vted_buffer;

void InitBuffer(void);

int AppendRow(const char *chars, int size, int eol);
int InsertRow(int at, const char *chars, int size, int eol);
void RemoveRow(int at);

int RowInsertChar(vted_row_t *row, int at, int c);
/* places len bytes at byte index at in one step, which a whole run of
   text needs instead of a call per byte; the row is left alone and -1
   returned when at is outside it or memory runs out */
int RowInsertString(vted_row_t *row, int at, const char *s, int len);
int RowRemoveChar(vted_row_t *row, int at);
int RowAppendString(vted_row_t *row, const char *s, int len);

/* cuts the row down to its first at bytes; nothing happens when at lies
   at or past the end */
void RowTruncate(vted_row_t *row, int at);

/* replaces the whole text of the row with a copy of the given bytes;
   the row is left alone and -1 returned when memory runs out */
int RowSetText(vted_row_t *row, const char *chars, int size);

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

/* word movement: a word is a run of characters of one class (see
   Utf8CharClass), so "foo_bar" is one word and "->" is another.
   RowWordRight gives the byte index of the start of the next word,
   RowWordLeft the start of the word before cx. Both stay inside the
   row: they return its end or its start when there is no further
   word, and the caller decides whether to wrap to another line */
int RowWordRight(const vted_row_t *row, int cx);
int RowWordLeft(const vted_row_t *row, int cx);

void FreeRow(vted_row_t *row);
void FreeBuffer(void);

#endif

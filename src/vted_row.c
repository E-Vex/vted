/*===============================*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_row.h"
#include "vted_screen.h"
#include "vted_syntax.h"
#include "vted_utf8.h"
/*===============================*/

vted_buffer_t vted_buffer;

void InitBuffer(void)
{
    vted_buffer.rows = NULL;
    vted_buffer.num_rows = 0;
    vted_buffer.filename = NULL;
    vted_buffer.dirty = 0;
    vted_buffer.final_newline = 1;

    /* a new buffer has no colors worked out yet */
    SyntaxInvalidate(0);
}

/* tells the syntax module that a row was edited, so its colors, and
   those of the rows below it, are worked out again. Rows that live
   outside the buffer (a scratch row) are nobody's business */
static void NoteRowChange(const vted_row_t *row)
{
    uintptr_t at = (uintptr_t)row;
    uintptr_t first = (uintptr_t)vted_buffer.rows;
    uintptr_t index;

    if (vted_buffer.rows == NULL || at < first)
    {
        return;
    }

    index = (at - first) / sizeof(vted_row_t);

    if (index < (uintptr_t)vted_buffer.num_rows)
    {
        int row = (int)index;

        SyntaxInvalidate(row);

        /* this row changed, and a block comment started or ended here
           can recolor every row below it, so repaint everything from
           here down: the bytes are cheap, the redraw is only the
           viewport rows that are actually on screen */
        ScreenMarkRowsDirtyFrom(row);
    }
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

int InsertRow(int at, const char *chars, int size, int eol)
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

    vted_buffer.rows[at].eol = eol;

    vted_buffer.num_rows++;
    SyntaxInvalidate(at);
    /* rows below this one moved to a different screen line: repaint it all */
    ScreenMarkAllDirty();
    return 0;
}

int AppendRow(const char *chars, int size, int eol)
{
    return InsertRow(vted_buffer.num_rows, chars, size, eol);
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
    SyntaxInvalidate(at);
    /* rows below this one moved to a different screen line: repaint it all */
    ScreenMarkAllDirty();
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
    NoteRowChange(row);
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
    NoteRowChange(row);
    return 0;
}

int RowInsertString(vted_row_t *row, int at, const char *s, int len)
{
    if (at < 0 || at > row->size || len < 0)
    {
        return -1;
    }

    if (len == 0)
    {
        return 0;
    }

    /* room for the new bytes plus the null terminator */
    char *new_chars = realloc(row->chars, (size_t)row->size + (size_t)len + 1);

    if (new_chars == NULL)
    {
        return -1;
    }

    row->chars = new_chars;

    /* one shift of the tail makes the gap, one copy fills it */
    memmove(&row->chars[at + len], &row->chars[at], (size_t)(row->size - at));
    memcpy(&row->chars[at], s, (size_t)len);
    row->size += len;
    row->chars[row->size] = '\0';
    NoteRowChange(row);
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
    NoteRowChange(row);
    return 0;
}

void RowTruncate(vted_row_t *row, int at)
{
    if (at < 0 || at >= row->size)
    {
        return;
    }

    row->size = at;
    row->chars[at] = '\0';
    NoteRowChange(row);
}

int RowSetText(vted_row_t *row, const char *chars, int size)
{
    char *new_chars = malloc((size_t)size + 1);

    if (new_chars == NULL)
    {
        return -1;
    }

    if (size > 0)
    {
        memcpy(new_chars, chars, (size_t)size);
    }

    new_chars[size] = '\0';

    free(row->chars);
    row->chars = new_chars;
    row->size = size;
    NoteRowChange(row);
    return 0;
}

int SplitRow(int row, int cx, int eol)
{
    vted_row_t *upper = &vted_buffer.rows[row];

    int tail = upper->size - cx;
    char *line = malloc((size_t)tail + 1);

    if (line == NULL)
    {
        return -1;
    }

    memcpy(line, &upper->chars[cx], (size_t)tail);

    if (InsertRow(row + 1, line, tail, eol) == -1)
    {
        free(line);
        return -1;
    }

    free(line);

    /* InsertRow may have moved the rows array */
    RowTruncate(&vted_buffer.rows[row], cx);
    return 0;
}

int InsertText(int row, int cx, const char *text, int length, int eol)
{
    int start = 0; /* where the line being placed begins in the text */
    int i;

    for (i = 0; i < length; i++)
    {
        if (text[i] != '\n')
        {
            continue;
        }

        if (RowInsertString(&vted_buffer.rows[row], cx, text + start, i - start) == -1)
        {
            return -1;
        }

        if (SplitRow(row, cx + (i - start), eol) == -1)
        {
            return -1;
        }

        row++;
        cx = 0;
        start = i + 1;
    }

    return RowInsertString(&vted_buffer.rows[row], cx, text + start, length - start);
}

int RemoveText(int row, int cx, const char *text, int length)
{
    int lines = 0;      /* line breaks inside the text */
    int last_start = 0; /* where its last line begins */
    int i;

    for (i = 0; i < length; i++)
    {
        if (text[i] == '\n')
        {
            lines++;
            last_start = i + 1;
        }
    }

    vted_row_t *upper = &vted_buffer.rows[row];

    if (lines == 0)
    {
        /* one line: close the gap it leaves, terminator included */
        memmove(&upper->chars[cx], &upper->chars[cx + length],
                (size_t)(upper->size - cx - length) + 1);
        upper->size -= length;
        NoteRowChange(upper);
        return 0;
    }

    vted_row_t *lower = &vted_buffer.rows[row + lines];
    int last_length = length - last_start;

    /* the first line of the text goes, then the tail that follows the
       last line is joined on in its place */
    RowTruncate(upper, cx);

    if (RowAppendString(upper, lower->chars + last_length,
                        lower->size - last_length) == -1)
    {
        return -1;
    }

    while (lines-- > 0)
    {
        RemoveRow(row + 1);
    }

    return 0;
}

int RowCharWidth(const vted_row_t *row, int at, int cell)
{
    unsigned char byte = (unsigned char)row->chars[at];
    int code_point;

    if (byte == '\t')
    {
        return VTED_TAB_STOP - (cell % VTED_TAB_STOP);
    }

    if (byte < 0x20 || byte == 0x7F)
    {
        return 2; /* shown as ^X */
    }

    Utf8Decode(&row->chars[at], row->size - at, &code_point);

    /* C1 controls (U+0080-U+009F) would be executed by a terminal,
       so they are drawn as a replacement character, one cell wide */
    if (code_point >= 0x80 && code_point < 0xA0)
    {
        return 1;
    }

    return Utf8CharWidth(code_point);
}

int RowCxToRx(const vted_row_t *row, int cx)
{
    int rx = 0;
    int at = 0;

    while (at < cx && at < row->size)
    {
        rx += RowCharWidth(row, at, rx);
        at = Utf8NextBoundary(row->chars, row->size, at);
    }

    return rx;
}

int RowRxToCx(const vted_row_t *row, int rx)
{
    int cell = 0;
    int at = 0;

    while (at < row->size)
    {
        int width = RowCharWidth(row, at, cell);

        /* rx falls inside [cell, cell + width): this is the character */
        if (cell + width > rx)
        {
            return at;
        }

        cell += width;
        at = Utf8NextBoundary(row->chars, row->size, at);
    }

    return row->size;
}

int RowIndentLength(const vted_row_t *row, int limit)
{
    int length = 0;

    while (length < row->size && length < limit &&
           (row->chars[length] == ' ' || row->chars[length] == '\t'))
    {
        length++;
    }

    return length;
}

static Utf8Class RowClassAt(const vted_row_t *row, int at)
{
    int code_point;

    Utf8Decode(&row->chars[at], row->size - at, &code_point);
    return Utf8CharClass(code_point);
}

int RowWordRight(const vted_row_t *row, int cx)
{
    int at = cx;

    if (at >= row->size)
    {
        return row->size;
    }

    /* leave the run the cursor is in (a blank is not a run: the
       blanks are skipped below) */
    Utf8Class run = RowClassAt(row, at);

    if (run != Utf8ClassBlank)
    {
        while (at < row->size && RowClassAt(row, at) == run)
        {
            at = Utf8NextBoundary(row->chars, row->size, at);
        }
    }

    /* then the blanks up to the start of the next word */
    while (at < row->size && RowClassAt(row, at) == Utf8ClassBlank)
    {
        at = Utf8NextBoundary(row->chars, row->size, at);
    }

    return at;
}

int RowWordLeft(const vted_row_t *row, int cx)
{
    int at = cx;
    int prev;

    /* the blanks left of the cursor first */
    while (at > 0)
    {
        prev = Utf8PrevBoundary(row->chars, at);

        if (RowClassAt(row, prev) != Utf8ClassBlank)
        {
            break;
        }

        at = prev;
    }

    if (at == 0)
    {
        return 0;
    }

    /* then back over the run before them, to its first character */
    Utf8Class run = RowClassAt(row, Utf8PrevBoundary(row->chars, at));

    while (at > 0)
    {
        prev = Utf8PrevBoundary(row->chars, at);

        if (RowClassAt(row, prev) != run)
        {
            break;
        }

        at = prev;
    }

    return at;
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

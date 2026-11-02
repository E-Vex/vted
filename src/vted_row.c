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

/* the tab stop in cells; RowCharWidth() converts byte positions to
   display columns with it, so it is the single place tabs are sized */
static int row_tab_stop = VTED_TAB_STOP;

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
        return row_tab_stop - (cell % row_tab_stop);
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

/* advances one glyph through the row the way wrapping lays it out:
   cols cells per screen line, with a glyph that would cross a line's
   right edge moved onto the next line instead of being cut in half
   (a tab, being blank space, may run to the edge and on past it).
   The walk's byte index and cell are kept in *at and *cell and moved
   past the glyph; the cell it was placed at is returned */
static int WrapAdvance(const vted_row_t *row, int cols, int *at, int *cell)
{
    int width = RowCharWidth(row, *at, *cell);
    int placed;

    if (cols < 1)
    {
        cols = 1;
    }

    if (row->chars[*at] != '\t' && *cell + width > (*cell / cols + 1) * cols)
    {
        /* the glyph would cross the right edge of its line: the
           next line is where it starts */
        *cell = (*cell / cols + 1) * cols;
    }

    placed = *cell;
    *cell += width;
    *at = Utf8NextBoundary(row->chars, row->size, *at);

    return placed;
}

/* walks the row the way the renderer wraps it: cols cells per screen
   line, with a glyph that would cross a line's right edge moved onto
   the next line instead of being cut in half (a tab, being blank
   space, may run to the edge and on past it). Reports the number of
   screen lines the row takes, and where byte index cx lands: the
   screen line it is on and the cell column within that line */
static void WrapWalk(const vted_row_t *row, int cols, int cx,
                     int *out_lines, int *out_line, int *out_col)
{
    int lines = 1;
    int cell = 0;
    int at = 0;

    if (cols < 1)
    {
        cols = 1;
    }

    while (at < row->size)
    {
        int glyph_at = at;
        int placed = WrapAdvance(row, cols, &at, &cell);
        int line = placed / cols;

        if (line + 1 > lines)
        {
            lines = line + 1;
        }

        if (glyph_at == cx)
        {
            *out_line = line;
            *out_col = placed - line * cols;
        }
    }

    if (cx >= row->size)
    {
        /* one past the last glyph: where the next one would start,
           which is the line after the row when the text ends exactly
           at a line edge */
        *out_line = cell / cols;
        *out_col = cell % cols;
    }

    if (cell > 0 && (cell - 1) / cols + 1 > lines)
    {
        /* a trailing tab ran onto lines of its own */
        lines = (cell - 1) / cols + 1;
    }

    *out_lines = lines;
}

int RowWrapCount(const vted_row_t *row, int cols)
{
    int lines;
    int line;
    int col;

    WrapWalk(row, cols, row->size, &lines, &line, &col);

    return lines;
}

void RowCxToWrapped(const vted_row_t *row, int cx, int cols, int *line, int *col)
{
    int lines;

    WrapWalk(row, cols, cx, &lines, line, col);
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

int RowWrapRunStart(const vted_row_t *row, int at, int start_cell, int cols)
{
    /* the run's cells, counted only as far as one line's worth: past
       that the run is longer than a line, breaks at the line's edge
       instead, and its exact size no longer matters */
    int cells = 0;
    int i = at;

    if (cols < 1)
    {
        cols = 1;
    }

    while (i < row->size && RowClassAt(row, i) != Utf8ClassBlank)
    {
        /* a non-blank glyph's width does not depend on the cell it
           starts at, so the cells add up the same wherever the run
           is placed */
        cells += RowCharWidth(row, i, 0);

        if (cells > cols)
        {
            break; /* longer than a line */
        }

        i = Utf8NextBoundary(row->chars, row->size, i);
    }

    if (start_cell % cols != 0 && cells <= cols &&
        start_cell + cells > (start_cell / cols + 1) * cols)
    {
        /* the run does not fit the rest of its line: the next line's
           first cell is where it starts */
        return (start_cell / cols + 1) * cols;
    }

    return start_cell;
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

void RowSetTabWidth(int width)
{
    if (width < 1)
    {
        width = 1;
    }

    row_tab_stop = width;
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

/* the three bracket pairs the match works on */
static const char bracket_openers[] = "([{";
static const char bracket_closers[] = ")]}";

/* which pair a byte belongs to, -1 when it is no bracket; NUL never
   counts, or strchr() would find the string terminator */
static int BracketIndex(char c)
{
    const char *at;

    if (c == '\0')
    {
        return -1;
    }

    at = strchr(bracket_openers, c);

    if (at != NULL)
    {
        return (int)(at - bracket_openers);
    }

    at = strchr(bracket_closers, c);

    if (at != NULL)
    {
        return (int)(at - bracket_closers);
    }

    return -1;
}

int BufferFindBracketMatch(int cy, int cx, int *match_cy, int *match_cx)
{
    if (cy < 0 || cy >= vted_buffer.num_rows)
    {
        return 0;
    }

    const vted_row_t *row = &vted_buffer.rows[cy];

    if (cx < 0 || cx > row->size)
    {
        return 0;
    }

    /* the bracket under the cursor decides; when there is none, the
       one right before it does, so the cursor resting after a closing
       bracket still shows its partner */
    int at = cx;
    int pair = -1;

    if (at < row->size)
    {
        pair = BracketIndex(row->chars[at]);
    }

    if (pair == -1 && at > 0)
    {
        at--;
        pair = BracketIndex(row->chars[at]);
    }

    if (pair == -1)
    {
        return 0;
    }

    char open = bracket_openers[pair];
    char close = bracket_closers[pair];
    int forward = row->chars[at] == open; /* an opener looks ahead */
    int depth = 1;                        /* the bracket itself counts */
    long scanned = 0;
    int y = cy;
    int x;

    while (y >= 0 && y < vted_buffer.num_rows && scanned < VTED_BRACKET_SCAN_BYTES)
    {
        const vted_row_t *scan_row = &vted_buffer.rows[y];

        x = (y == cy) ? (forward ? at + 1 : at - 1)
                      : (forward ? 0 : scan_row->size - 1);

        while (x >= 0 && x < scan_row->size && scanned < VTED_BRACKET_SCAN_BYTES)
        {
            char c = scan_row->chars[x];

            if (c == open)
            {
                depth += forward ? 1 : -1;
            }
            else if (c == close)
            {
                depth += forward ? -1 : 1;
            }

            if (depth == 0)
            {
                *match_cy = y;
                *match_cx = x;
                return 1;
            }

            x += forward ? 1 : -1;
            scanned++;
        }

        /* the line break between rows counts toward the limit as well */
        y += forward ? 1 : -1;
        scanned++;
    }

    return 0;
}

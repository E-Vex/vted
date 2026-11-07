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

/* 1 when the search matches letters without regard to their case;
   off by default, the way a plain byte search behaves */
static int find_fold_case = 0;

/* the byte a folded search treats this one as: a letter and the other
   of its cases count as the same, and the lower case is the one kept */
static unsigned char FoldByte(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (unsigned char)(c + ('a' - 'A'));
    }

    return c;
}

/* do the len bytes at text and at query agree, letters taken without
   regard to their case? Both are read only within their len bytes */
static int BytesFoldEqual(const char *text, const char *query, int len)
{
    int i;

    for (i = 0; i < len; i++)
    {
        if (FoldByte((unsigned char)text[i]) != FoldByte((unsigned char)query[i]))
        {
            return 0;
        }
    }

    return 1;
}

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

static Utf8Class RowClassAt(const vted_row_t *row, int at)
{
    int code_point;

    Utf8Decode(&row->chars[at], row->size - at, &code_point);
    return Utf8CharClass(code_point);
}

/* advances one glyph through the row as the wrapped layout places
   it: cols cells per screen line, with wrapping breaking at word
   boundaries — a run of non-blank glyphs that does not fit the rest
   of its line starts the next one whole, and only a run longer than
   a line, like a wide glyph at the very edge, breaks mid-run (a tab,
   being blank space, may run to the edge and on past it). With wrap 0
   the row lays out on one endless line instead, which is what a
   viewport that scrolls sideways shows. The indent is the cells every
   line after the first keeps blank at its start: a walk that lands
   in that padding skips over it, and a glyph the layout puts past
   the first line starts at the next line's first cell after it. The
   walk's byte index and cell are kept in *at and *cell and moved
   past the glyph, and *in_run keeps whether it is inside a run of
   non-blank glyphs: it starts at 0 and is carried from glyph to
   glyph. The cell the glyph was placed at is returned */
int RowWrapAdvance(const vted_row_t *row, int wrap, int cols, int indent,
                   int *at, int *cell, int *in_run)
{
    int width;
    int placed;

    if (wrap)
    {
        if (cols < 1)
        {
            cols = 1;
        }

        if (indent < 0)
        {
            indent = 0;
        }

        if (indent >= cols)
        {
            /* every line keeps at least one content cell */
            indent = cols - 1;
        }

        if (indent > 0 && *cell / cols > 0 && *cell % cols < indent)
        {
            /* the walk reached the padding a continuation line keeps
               for its indent: the first cell past it is where its
               content starts */
            *cell = (*cell / cols) * cols + indent;
        }
    }

    width = RowCharWidth(row, *at, *cell);

    if (wrap)
    {
        if (RowClassAt(row, *at) == Utf8ClassBlank)
        {
            *in_run = 0;
        }
        else if (*in_run == 0)
        {
            /* the glyph starts a run: the run moves down whole when
               it does not fit the rest of its line */
            *cell = RowWrapRunStart(row, *at, *cell, cols, indent);
            *in_run = 1;
        }

        if (row->chars[*at] != '\t' && *cell + width > (*cell / cols + 1) * cols)
        {
            /* the glyph would cross the right edge of its line: the
               next line's first content cell is where it starts */
            *cell = (*cell / cols + 1) * cols + indent;
        }
    }

    placed = *cell;
    *cell += width;
    *at = Utf8NextBoundary(row->chars, row->size, *at);

    return placed;
}

/* walks the row the way the renderer wraps it: cols cells per
   screen line, with wrapping breaking at word boundaries — a run of
   non-blank glyphs that does not fit the rest of a line starts the
   next one whole, and only a run longer than a line breaks at the
   edge, the way a wide glyph does too (a tab, being blank space, may
   run to the edge and on past it). The indent is the cells every
   line after the first keeps blank at its start. Reports the number
   of screen lines the row takes, and where byte index cx lands: the
   screen line it is on and the cell column within that line */
static void WrapWalk(const vted_row_t *row, int cols, int indent, int cx,
                     int *out_lines, int *out_line, int *out_col)
{
    int lines = 1;
    int cell = 0;
    int at = 0;
    int in_run = 0;

    if (cols < 1)
    {
        cols = 1;
    }

    if (indent < 0)
    {
        indent = 0;
    }

    if (indent >= cols)
    {
        indent = cols - 1;
    }

    while (at < row->size)
    {
        int glyph_at = at;
        int placed = RowWrapAdvance(row, 1, cols, indent, &at, &cell, &in_run);
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

        if (*out_line > 0 && *out_col < indent)
        {
            /* the walk stopped in the padding a continuation line
               keeps for its indent: the cursor rests right after it */
            *out_col = indent;
        }
    }

    if (cell > 0 && (cell - 1) / cols + 1 > lines)
    {
        /* a trailing tab ran onto lines of its own */
        lines = (cell - 1) / cols + 1;
    }

    *out_lines = lines;
}

int RowWrapCount(const vted_row_t *row, int cols, int indent)
{
    int lines;
    int line;
    int col;

    WrapWalk(row, cols, indent, row->size, &lines, &line, &col);

    return lines;
}

void RowCxToWrapped(const vted_row_t *row, int cx, int cols, int indent,
                    int *line, int *col)
{
    int lines;

    WrapWalk(row, cols, indent, cx, &lines, line, col);
}

int RowWrappedToCx(const vted_row_t *row, int line, int col, int cols,
                   int indent)
{
    /* the spot given as a screen line of the wrapped layout and a
       cell column within it, turned into the byte index of the glyph
       that sits there: the glyph that covers the spot, the one
       before it when the spot is the padding a moved run left at its
       line's end, the line's own first glyph when the spot is the
       padding its continuation keeps for the indent, or the end of
       the row when the spot lies past its text */
    int target = line * cols + col;
    int cell = 0;
    int at = 0;
    int in_run = 0;
    int prev = 0;

    if (cols < 1)
    {
        cols = 1;
    }

    if (indent < 0)
    {
        indent = 0;
    }

    if (indent >= cols)
    {
        indent = cols - 1;
    }

    while (at < row->size)
    {
        int glyph_at = at;
        int prev_end = cell;
        int placed = RowWrapAdvance(row, 1, cols, indent, &at, &cell, &in_run);
        int width = cell - placed;

        if (placed == target)
        {
            return glyph_at; /* the glyph starts right there */
        }

        if (placed < target && placed + width > target)
        {
            return glyph_at; /* a wide glyph, or a tab, covers it */
        }

        if (placed > target)
        {
            if (line > 0 && prev_end <= line * cols)
            {
                /* the spot is in the padding the line keeps for its
                   indent, and the walk crossed into the line to get
                   here: its own first glyph covers the spot */
                return glyph_at;
            }

            return prev; /* the padding at the line's end */
        }

        prev = glyph_at;
    }

    return row->size; /* past the row's last glyph */
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

int RowWrapIndentCells(const vted_row_t *row, int cols)
{
    int leading;
    int cells;

    if (cols < 1)
    {
        cols = 1;
    }

    if (row == NULL || row->size == 0)
    {
        return 0;
    }

    leading = RowIndentLength(row, row->size);

    if (leading >= row->size)
    {
        /* nothing but blanks: there is no glyph to line up under */
        return 0;
    }

    cells = RowCxToRx(row, leading);

    if (cells > cols - 1)
    {
        /* every line keeps at least one content cell */
        cells = cols - 1;
    }

    return cells;
}

/* the cell column where a row's indent ends: its leading whitespace
   laid out the way the row is drawn. A row of nothing but blanks
   keeps the whole of it — the level its blanks sit at — and an empty
   row none at all */
static int RowIndentEdge(const vted_row_t *row)
{
    return RowCxToRx(row, RowIndentLength(row, row->size));
}

int BufferIndentEdges(int row, int *edges, int max)
{
    /* the nesting levels the row sits in, one cell column each: its
       own indent edge first, then the edge of every shallower indent
       still open above it, so guides drawn on them follow the widths
       the file really indents with — tabs, spaces or both — instead
       of the fixed tab stops. An empty row, or one of nothing but
       blanks, holds no level of its own: blank lines inside a block
       do not end the levels above them. At most max edges are kept,
       the deepest ones; a row outside the buffer has none */
    int count = 0;
    int shallowest;
    int at;

    if (row < 0 || row >= vted_buffer.num_rows || max <= 0)
    {
        return 0;
    }

    shallowest = RowIndentEdge(&vted_buffer.rows[row]);

    if (shallowest < 1)
    {
        return 0; /* no indent of its own: no level to mark */
    }

    edges[count++] = shallowest;

    for (at = row - 1; at >= 0 && shallowest > 1 && count < max; at--)
    {
        const vted_row_t *above = &vted_buffer.rows[at];
        int leading = RowIndentLength(above, above->size);
        int edge;

        if (leading >= above->size)
        {
            continue; /* empty, or nothing but blanks */
        }

        edge = RowCxToRx(above, leading);

        if (edge == 0)
        {
            break; /* level zero: nothing shallower to mark */
        }

        if (edge >= shallowest)
        {
            continue; /* same or deeper: not a level this row sits in */
        }

        edges[count++] = edge;
        shallowest = edge;
    }

    return count;
}

int RowWrapRunStart(const vted_row_t *row, int at, int start_cell, int cols,
                    int indent)
{
    /* the run's cells, counted only as far as one line's worth: past
       that the run is longer than a line, breaks at the line's edge
       instead, and its exact size no longer matters */
    int cells = 0;
    int i = at;
    int room;
    int line;
    int content_start;

    if (cols < 1)
    {
        cols = 1;
    }

    if (indent < 0)
    {
        indent = 0;
    }

    if (indent >= cols)
    {
        indent = cols - 1;
    }

    /* the cells a continuation line offers its content */
    room = cols - indent;

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

    line = start_cell / cols;
    content_start = line * cols + (line > 0 ? indent : 0);

    if (start_cell != content_start && cells <= room &&
        start_cell + cells > (line + 1) * cols)
    {
        /* the run does not fit the rest of its line, and a line
           offers room enough for it past the indent: the next
           line's first content cell is where it starts */
        return (line + 1) * cols + indent;
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

/* the run of the class of the glyph at cx, from its first byte index
   to one past its last: the runs the word moves jump over, taken as
   a span instead, so a double click can lift the word it lands in
   off it. A blank run or a run of punctuation is a span of its own,
   the way it is a word of its own for the moves; a cx at or past the
   end of the row spans nothing */
void RowWordSpan(const vted_row_t *row, int cx, int *start, int *end)
{
    int at = cx;
    int prev;

    if (at < 0)
    {
        at = 0;
    }

    if (at >= row->size)
    {
        *start = row->size;
        *end = row->size;
        return;
    }

    /* forward over the run the glyph sits in, to one past its end */
    {
        Utf8Class run = RowClassAt(row, at);

        while (at < row->size && RowClassAt(row, at) == run)
        {
            at = Utf8NextBoundary(row->chars, row->size, at);
        }

        *end = at;
    }

    /* and back over it again, to its first glyph */
    at = cx;

    while (at > 0)
    {
        prev = Utf8PrevBoundary(row->chars, at);

        if (RowClassAt(row, prev) != RowClassAt(row, cx))
        {
            break;
        }

        at = prev;
    }

    *start = at;
}

/* is the byte one a file name is made of: a letter, a digit, one of
   the punctuation marks a path carries, or a byte of a name in
   another script. The marks a shell gives a meaning of its own —
   quotes, braces, colons, and the rest — are left out, so a name in
   quotes or between parentheses comes out clean */
static int PathByte(char c)
{
    unsigned char b = (unsigned char)c;

    if (b >= 0x80)
    {
        return 1; /* the bytes of a name in another script */
    }

    if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
        (b >= '0' && b <= '9'))
    {
        return 1;
    }

    return b == '/' || b == '.' || b == '-' || b == '_' || b == '+' ||
           b == '~' || b == '#' || b == '%' || b == '=';
}

/* the run of path bytes around the cursor, from the first byte of the
   file name it sits in (or right behind) to one past its last: the
   name a gf-style open takes. The cursor on a byte that is no path
   byte — a quote, a space — falls back to the glyph before it, the
   way the bracket match does, so the cursor at the end of a name
   still takes it; a cursor with no path byte under or behind it spans
   nothing, both indexes coming back where the cursor stands */
void RowPathSpan(const vted_row_t *row, int cx, int *start, int *end)
{
    int at = cx;

    if (at < 0)
    {
        at = 0;
    }

    if (at > row->size)
    {
        at = row->size;
    }

    /* the glyph under the cursor, or the one right behind it, decides */
    if (at >= row->size || !PathByte(row->chars[at]))
    {
        at = Utf8PrevBoundary(row->chars, at);
    }

    if (at >= row->size || !PathByte(row->chars[at]))
    {
        *start = cx;
        *end = cx;
        return;
    }

    /* forward to the last path byte of the run */
    int last = at;

    while (last < row->size && PathByte(row->chars[last]))
    {
        last++;
    }

    /* and back to its first */
    int first = at;

    while (first > 0 && PathByte(row->chars[first - 1]))
    {
        first--;
    }

    *start = first;
    *end = last;
}

/* the run of digits starting at `at`, one past its last digit coming
   back: the number it spells comes out saturated near a thousand
   million, so a tail of absurd digits still lands somewhere instead
   of overflowing its way past every line the file has */
#define JUMP_TAIL_MAX 1000000000

static int DigitRun(const char *chars, int size, int at, int *value)
{
    int number = 0;

    while (at < size && chars[at] >= '0' && chars[at] <= '9')
    {
        if (number <= (JUMP_TAIL_MAX - 9) / 10)
        {
            number = number * 10 + (chars[at] - '0');
        }
        at++;
    }

    *value = number;
    return at;
}

/* the ":line:column" tail a path name can carry, the way a compiler
   names the spot in a file it speaks about: a colon and a line number
   right behind the name, and behind that another colon and a column,
   both numbers counting from one. Returns 1 with the line in *line —
   and the column in *column, 0 when the name carries none — and 0
   when the bytes at `at` open no such tail: nothing there, a colon
   without a number behind it, or anything else that is no digit */
int RowPathJump(const vted_row_t *row, int at, int *line, int *column)
{
    int past_line;

    *line = 0;
    *column = 0;

    if (at < 0 || at >= row->size || row->chars[at] != ':')
    {
        return 0; /* no colon where a tail would open */
    }

    past_line = DigitRun(row->chars, row->size, at + 1, line);

    if (past_line == at + 1)
    {
        return 0; /* the colon with no number behind it */
    }

    /* the column behind a second colon, when a number is there; a
       second colon with none, or no second colon at all, leaves the
       tail as the line alone */
    if (past_line < row->size && row->chars[past_line] == ':')
    {
        DigitRun(row->chars, row->size, past_line + 1, column);
    }

    return 1;
}

/* the ":line:column" tail a NUL-terminated name can carry at its
   end, read the way RowPathJump reads one off a row: digits at the
   very end, a colon behind them, and behind that colon another run
   of digits behind another colon naming the line the first run
   gives the column of. Returns the length of the name itself — the
   bytes before the colon that opens the tail — with the line in
   *line and the column in *column, both counting from one; the
   whole length when the name carries no tail */
int RowNameTail(const char *name, int *line, int *column)
{
    int len = (int)strlen(name);
    int col_end = len;
    int col_at = len;

    *line = 0;
    *column = 0;

    /* the digits the name ends with, when it ends with any */
    while (col_at > 0 && name[col_at - 1] >= '0' &&
           name[col_at - 1] <= '9')
    {
        col_at--;
    }

    if (col_at == col_end || col_at == 0 || name[col_at - 1] != ':')
    {
        return len; /* no digits at the end, or no colon behind them */
    }

    /* another run of digits behind another colon: the line the
       first run gives the column of */
    {
        int line_end = col_at - 1;
        int line_at = line_end;

        while (line_at > 0 && name[line_at - 1] >= '0' &&
               name[line_at - 1] <= '9')
        {
            line_at--;
        }

        if (line_at < line_end && line_at > 0 &&
            name[line_at - 1] == ':')
        {
            DigitRun(name, len, line_at, line);
            DigitRun(name, len, col_at, column);
            return line_at - 1;
        }
    }

    /* the tail is the line alone */
    DigitRun(name, len, col_at, line);
    return col_at - 1;
}

/* the words collected so far and what they are matched against: one
   array of fixed lines the scanners fill, the prefix they extend */
typedef struct
{
    const char *prefix; /* the word being completed, borrowed */
    int prefix_len;
    char *words;  /* max_words lines of word_size bytes each */
    int max_words;
    int word_size;
    int count; /* lines filled so far */
} completion_set_t;

/* the start of the run of word characters that ends at cx: cx itself
   when the character left of the cursor is not a word character */
static int WordRunEndingAt(const vted_row_t *row, int cx)
{
    int at = cx;
    int prev;

    while (at > 0)
    {
        prev = Utf8PrevBoundary(row->chars, at);

        if (RowClassAt(row, prev) != Utf8ClassWord)
        {
            break;
        }

        at = prev;
    }

    return at;
}

/* the run [start, end) of the row offered as a completion: a word
   that starts with the prefix, is longer than it, fits a line of the
   array and has not been collected before — the first one stands as
   the nearest. Anything else changes nothing */
static void OfferWord(const vted_row_t *row, int start, int end,
                      completion_set_t *set)
{
    int len = end - start;
    char *line;

    if (RowClassAt(row, start) != Utf8ClassWord)
    {
        return; /* punctuation and symbols are not words to complete */
    }

    if (len <= set->prefix_len || len + 1 > set->word_size)
    {
        return; /* it extends nothing, or fits no line of the array */
    }

    if (!BytesFoldEqual(row->chars + start, set->prefix, set->prefix_len))
    {
        return; /* the match folds the case, the way the search does */
    }

    for (line = set->words; line < set->words + set->count * set->word_size;
         line += set->word_size)
    {
        if (strncmp(line, row->chars + start, (size_t)len) == 0 &&
            line[len] == '\0')
        {
            return; /* collected before: the nearer one stands */
        }
    }

    memcpy(set->words + set->count * set->word_size, row->chars + start,
           (size_t)len);
    set->words[set->count * set->word_size + len] = '\0';
    set->count++;
}

/* walks the runs of the row right to left, from byte index from down
   to its start, offering each word. A run that reaches past the byte
   index past is skipped whole: on the row of the cursor that is the
   word it sits inside, which would only complete to itself */
static void CollectRowBackward(const vted_row_t *row, int from, int past,
                               completion_set_t *set)
{
    int at = from;

    while (at > 0 && set->count < set->max_words)
    {
        int start = RowWordLeft(row, at);
        int end;
        Utf8Class run;

        if (start >= at)
        {
            break; /* nothing but blanks left of at */
        }

        /* the run RowWordLeft found ends where its class first
           changes again */
        run = RowClassAt(row, start);
        end = start;

        while (end < row->size && RowClassAt(row, end) == run)
        {
            end = Utf8NextBoundary(row->chars, row->size, end);
        }

        if (end <= past)
        {
            OfferWord(row, start, end, set);
        }

        at = start;
    }
}

/* walks the runs of the row left to right, from byte index from to
   its end, offering each word. The run from sits inside, when it does,
   began before it: the backward scan of the cursor's row offered it
   already, so it is stepped over whole */
static void CollectRowForward(const vted_row_t *row, int from,
                              completion_set_t *set)
{
    int at = from;

    if (at > 0 && RowClassAt(row, at) != Utf8ClassBlank &&
        RowClassAt(row, Utf8PrevBoundary(row->chars, at)) == RowClassAt(row, at))
    {
        Utf8Class run = RowClassAt(row, at);

        while (at < row->size && RowClassAt(row, at) == run)
        {
            at = Utf8NextBoundary(row->chars, row->size, at);
        }
    }

    while (at < row->size && set->count < set->max_words)
    {
        Utf8Class run = RowClassAt(row, at);
        int start;

        if (run == Utf8ClassBlank)
        {
            at = Utf8NextBoundary(row->chars, row->size, at);
            continue;
        }

        start = at;

        while (at < row->size && RowClassAt(row, at) == run)
        {
            at = Utf8NextBoundary(row->chars, row->size, at);
        }

        OfferWord(row, start, at, set);
    }
}

/* the words of the text around the cursor at (cy, cx), the nearest
   first: the text before it on its own row, then the rows above it,
   all read backwards, then the rest of its own row and the rows
   below it, read forwards */
static void CollectNearestFirst(const vted_buffer_t *text, int cy, int cx,
                                completion_set_t *set)
{
    const vted_row_t *row = &text->rows[cy];
    int i;

    CollectRowBackward(row, cx, cx, set);

    for (i = cy - 1; i >= 0 && set->count < set->max_words; i--)
    {
        CollectRowBackward(&text->rows[i],
                           text->rows[i].size,
                           text->rows[i].size, set);
    }

    CollectRowForward(row, cx, set);

    for (i = cy + 1; i < text->num_rows && set->count < set->max_words; i++)
    {
        CollectRowForward(&text->rows[i], 0, set);
    }
}

/* one pass over the rows, every run of word characters raising the
   count of every word its length and bytes fold to — the same fold
   that collected the words, so a saying under either spelling lifts
   the word it was collected as */
static void CountWordRuns(const vted_buffer_t *text,
                          const char *words, int word_size,
                          int *counts, const int *lens, int count)
{
    int r;
    int i;

    for (r = 0; r < text->num_rows; r++)
    {
        const vted_row_t *row = &text->rows[r];
        int at = 0;

        while (at < row->size)
        {
            Utf8Class run = RowClassAt(row, at);
            int start;

            if (run == Utf8ClassBlank)
            {
                at = Utf8NextBoundary(row->chars, row->size, at);
                continue;
            }

            start = at;

            while (at < row->size && RowClassAt(row, at) == run)
            {
                at = Utf8NextBoundary(row->chars, row->size, at);
            }

            if (run != Utf8ClassWord)
            {
                continue; /* punctuation is nobody's word */
            }

            for (i = 0; i < count; i++)
            {
                if (at - start == lens[i] &&
                    BytesFoldEqual(row->chars + start,
                                   words + (size_t)i * (size_t)word_size,
                                   lens[i]))
                {
                    counts[i]++;
                }
            }
        }
    }
}

/* the words ranked by how often the text says them, the ones it says
   most first: a saying lifts a word over a nearer one said seldom,
   and words said as often as one another keep the order the scan
   gave them, the nearest still first. No room to rank in, the scan
   order stands */
static void RankWordsByFrequency(const vted_buffer_t *text,
                                 char *words, int count, int word_size)
{
    int *scratch;
    int *counts;
    int *lens;
    char *line;
    int i;

    if (count < 2)
    {
        return; /* a word alone has no rank to trade */
    }

    /* one scratch holds the sayings, the length of every word, and
       the line a swap moves */
    scratch = malloc((size_t)count * (2 * sizeof(int)) + (size_t)word_size);

    if (scratch == NULL)
    {
        return; /* no room to rank: the scan order stands */
    }

    counts = scratch;
    lens = scratch + count;
    line = (char *)(scratch + 2 * count);

    for (i = 0; i < count; i++)
    {
        counts[i] = 0;
        lens[i] = (int)strlen(words + (size_t)i * (size_t)word_size);
    }

    CountWordRuns(text, words, word_size, counts, lens, count);

    /* insertion sort, the most said first: only a word said oftener
       than the one before it moves, so equals keep their places */
    for (i = 1; i < count; i++)
    {
        int said = counts[i];
        int j = i - 1;

        if (counts[j] >= said)
        {
            continue;
        }

        memcpy(line, words + (size_t)i * (size_t)word_size,
               (size_t)word_size);

        while (j >= 0 && counts[j] < said)
        {
            counts[j + 1] = counts[j];
            memcpy(words + (size_t)(j + 1) * (size_t)word_size,
                   words + (size_t)j * (size_t)word_size, (size_t)word_size);
            j--;
        }

        counts[j + 1] = said;
        memcpy(words + (size_t)(j + 1) * (size_t)word_size, line,
               (size_t)word_size);
    }

    free(scratch);
}

int BufferCollectCompletions(int cy, int cx,
                             char *words, int max_words, int word_size,
                             int *out_prefix_len)
{
    completion_set_t set;
    const vted_row_t *row;
    int prefix_start;

    if (out_prefix_len != NULL)
    {
        *out_prefix_len = 0;
    }

    /* no rows at all, a place past them, or the very start of a row:
       no word sits left of the cursor */
    if (cy < 0 || cy >= vted_buffer.num_rows || cx <= 0)
    {
        return 0;
    }

    row = &vted_buffer.rows[cy];

    if (cx > row->size)
    {
        cx = row->size;
    }

    prefix_start = WordRunEndingAt(row, cx);

    if (prefix_start == cx)
    {
        return 0; /* the character left of the cursor is not a word */
    }

    if (out_prefix_len != NULL)
    {
        *out_prefix_len = cx - prefix_start;
    }

    set.prefix = &row->chars[prefix_start];
    set.prefix_len = cx - prefix_start;
    set.words = words;
    set.max_words = max_words;
    set.word_size = word_size;
    set.count = 0;

    CollectNearestFirst(&vted_buffer, cy, cx, &set);

    /* the words the buffer being edited offered are ranked by how
       often it says them; the words of the other buffers, collected
       by BufferCollectTextWords after this returns, keep the order
       their rows read in */
    RankWordsByFrequency(&vted_buffer, words, set.count, word_size);

    return set.count;
}

int BufferCollectTextWords(const vted_buffer_t *text,
                           const char *prefix, int prefix_len,
                           char *words, int max_words, int word_size,
                           int count)
{
    completion_set_t set;
    int i;

    /* nothing to scan, or no line left to fill */
    if (text == NULL || prefix == NULL || prefix_len <= 0 ||
        count < 0 || count >= max_words)
    {
        return count;
    }

    set.prefix = prefix;
    set.prefix_len = prefix_len;
    set.words = words;
    set.max_words = max_words;
    set.word_size = word_size;
    set.count = count;

    /* a text no cursor sits in has no near or far: its rows read
       from the first to the last */
    for (i = 0; i < text->num_rows && set.count < set.max_words; i++)
    {
        CollectRowForward(&text->rows[i], 0, &set);
    }

    return set.count;
}

int RowFindNext(const vted_row_t *row, int from, int to,
                const char *query, int query_len)
{
    /* the last byte a match may start on: it has to end inside the
       row, and inside the bound the caller gave */
    int last;

    if (query_len <= 0)
    {
        return -1;
    }

    if (from < 0)
    {
        from = 0;
    }

    if (to > row->size)
    {
        to = row->size;
    }

    last = to < row->size - query_len + 1 ? to : row->size - query_len + 1;
    last--; /* now inclusive */

    if (find_fold_case)
    {
        /* a folded first byte may sit in the row as either of its
           cases, so both are looked for and the nearer one wins */
        unsigned char first = (unsigned char)query[0];
        unsigned char low = FoldByte(first);
        unsigned char high;

        if (low != first)
        {
            high = first; /* the query names the upper case itself */
        }
        else if (first >= 'a' && first <= 'z')
        {
            high = (unsigned char)(first - ('a' - 'A'));
        }
        else
        {
            high = first; /* a byte without a case has no other */
        }

        while (from <= last)
        {
            const char *hit = memchr(row->chars + from, low,
                                     (size_t)(last - from + 1));
            const char *other = high == low ? NULL
                : memchr(row->chars + from, high,
                         (size_t)(last - from + 1));

            if (hit == NULL || (other != NULL && other < hit))
            {
                hit = other;
            }

            if (hit == NULL)
            {
                return -1;
            }

            if (BytesFoldEqual(hit, query, query_len))
            {
                return (int)(hit - row->chars);
            }

            from = (int)(hit - row->chars) + 1;
        }

        return -1;
    }

    /* memchr finds the query's first byte fast; only positions a
       whole match could start from are looked at */
    while (from <= last)
    {
        const char *hit = memchr(row->chars + from, query[0],
                                 (size_t)(last - from + 1));

        if (hit == NULL)
        {
            return -1;
        }

        if (memcmp(hit, query, (size_t)query_len) == 0)
        {
            return (int)(hit - row->chars);
        }

        from = (int)(hit - row->chars) + 1;
    }

    return -1;
}

int RowFindPrev(const vted_row_t *row, int from, int to,
                const char *query, int query_len)
{
    int at;
    int last;

    if (query_len <= 0)
    {
        return -1;
    }

    if (from < 0)
    {
        from = 0;
    }

    if (to > row->size)
    {
        to = row->size;
    }

    last = to < row->size - query_len + 1 ? to : row->size - query_len + 1;

    for (at = last - 1; at >= from; at--)
    {
        int hit = find_fold_case
            ? BytesFoldEqual(row->chars + at, query, query_len)
            : memcmp(row->chars + at, query, (size_t)query_len) == 0;

        if (hit)
        {
            return at;
        }
    }

    return -1;
}

void RowSetTabWidth(int width)
{
    if (width < 1)
    {
        width = 1;
    }

    row_tab_stop = width;
}

/* the bytes one indent step is made of: a tab character, or a run of
   spaces a tab stop wide, the same bytes the Tab key puts in front of
   the cursor. insert_spaces picks between them; the unit is written
   into out — a buffer of VTED_TAB_WIDTH_MAX bytes holds any of them —
   and its length through *out_len */
void RowIndentUnit(char *out, int *out_len, int insert_spaces)
{
    int i;

    if (insert_spaces)
    {
        int width = row_tab_stop < VTED_TAB_WIDTH_MAX ? row_tab_stop
                                                      : VTED_TAB_WIDTH_MAX;

        for (i = 0; i < width; i++)
        {
            out[i] = ' ';
        }

        *out_len = width;
    }
    else
    {
        out[0] = '\t';
        *out_len = 1;
    }
}

void RowSetIgnoreCase(int fold)
{
    find_fold_case = fold != 0;
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

/* the closer that answers the opener, 0 when the byte opens no
   bracket: one of the three pairs the match works on, the pair an
   electric typing lays in */
char RowBracketCloser(char opener)
{
    const char *at =
        opener != '\0' ? strchr(bracket_openers, opener) : NULL;

    return at != NULL ? bracket_closers[at - bracket_openers] : 0;
}

/* the quote characters that wrap a selection as a pair of their
   own: each answers itself as both halves, the way a quoted span is
   written */
static const char wrap_quotes[] = "\"'";

/* the closer that answers the opener for wrapping a selection: the
   bracket pair an electric typing lays in, or a quote answering
   itself as its own — the same byte both halves. 0 when the byte
   opens no wrap */
char RowWrapCloser(char opener)
{
    if (opener == '\0')
    {
        return 0; /* NUL never counts, or strchr() would find the
                     string terminator */
    }

    if (strchr(wrap_quotes, opener) != NULL)
    {
        return opener;
    }

    return RowBracketCloser(opener);
}

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

/* the bracket under the cursor (or the one right behind it) and its
   partner, when both are there: the scan counts brackets of the same
   pair, so nesting comes out right, and it reaches a megabyte before
   a match counts as missing. Returns 0 without touching the outputs
   when there is no bracket, or no partner for it */
static int FindBracketPair(int cy, int cx,
                           int *bracket_cy, int *bracket_cx,
                           int *match_cy, int *match_cx)
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

    *bracket_cy = cy;
    *bracket_cx = at;

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

int BufferFindBracketMatch(int cy, int cx, int *match_cy, int *match_cx)
{
    int bracket_cy;
    int bracket_cx;

    return FindBracketPair(cy, cx, &bracket_cy, &bracket_cx,
                           match_cy, match_cx);
}

/* the quote characters the match lights up the far end of: each
   pairs with the next of its own byte, the span between them the
   quoted one a delimiter holds */
static const char match_quotes[] = "\"'";

/* whether the byte is one of the quotes the match works on; NUL
   never counts, or strchr() would find the string terminator */
static int IsMatchQuote(char c)
{
    return c != '\0' && strchr(match_quotes, c) != NULL;
}

/* whether the quote at byte x of the row carries an odd run of
   backslashes before it: escaped the way a quoted span says one, it
   is no delimiter — the match neither starts on it nor counts it */
static int QuoteEscapedAt(const vted_row_t *row, int x)
{
    int slashes = 0;
    int at = x - 1;

    while (at >= 0 && row->chars[at] == '\\')
    {
        slashes++;
        at--;
    }

    return (slashes & 1) != 0;
}

/* the count of quotes of the byte before row cy byte x, the rows
   above it walked backwards: the parity of the count says which half
   of the span the quote there is. A count that reaches
   VTED_BRACKET_SCAN_BYTES without arriving at the top of the buffer
   stands as it landed, the way a bracket past the reach counts as
   missing */
static int CountQuotesBefore(int cy, int x, char quote)
{
    long scanned = 0;
    int count = 0;
    int y = cy;

    x--; /* the quote itself does not count */

    while (y >= 0 && scanned < VTED_BRACKET_SCAN_BYTES)
    {
        const vted_row_t *scan_row = &vted_buffer.rows[y];

        while (x >= 0 && scanned < VTED_BRACKET_SCAN_BYTES)
        {
            if (scan_row->chars[x] == quote && !QuoteEscapedAt(scan_row, x))
            {
                count++;
            }

            x--;
            scanned++;
        }

        /* the line break between rows counts toward the limit as
           well */
        y--;
        x = y >= 0 ? vted_buffer.rows[y].size - 1 : 0;
        scanned++;
    }

    return count;
}

int BufferFindQuoteMatch(int cy, int cx, int *match_cy, int *match_cx)
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

    /* the quote under the cursor decides; when there is none, the
       one right before it does, so the cursor resting after a
       closing quote still shows its partner — and one a backslash
       escapes matches nothing at all, for it is no delimiter */
    int at = cx;
    char quote = 0;

    if (at < row->size && IsMatchQuote(row->chars[at]))
    {
        quote = QuoteEscapedAt(row, at) ? 0 : row->chars[at];
    }

    if (quote == 0 && at > 0 && IsMatchQuote(row->chars[at - 1]))
    {
        at--;
        quote = QuoteEscapedAt(row, at) ? 0 : row->chars[at];
    }

    if (quote == 0)
    {
        return 0;
    }

    /* which half of the span the quote is: an even count of its own
       byte before it opens one, an odd count closes it */
    int forward = (CountQuotesBefore(cy, at, quote) & 1) == 0;
    long scanned = 0;
    int y = cy;
    int x;

    while (y >= 0 && y < vted_buffer.num_rows &&
           scanned < VTED_BRACKET_SCAN_BYTES)
    {
        const vted_row_t *scan_row = &vted_buffer.rows[y];

        x = (y == cy) ? (forward ? at + 1 : at - 1)
                      : (forward ? 0 : scan_row->size - 1);

        while (x >= 0 && x < scan_row->size &&
               scanned < VTED_BRACKET_SCAN_BYTES)
        {
            if (scan_row->chars[x] == quote && !QuoteEscapedAt(scan_row, x))
            {
                *match_cy = y;
                *match_cx = x;
                return 1;
            }

            x += forward ? 1 : -1;
            scanned++;
        }

        /* the line break between rows counts toward the limit as
           well */
        y += forward ? 1 : -1;
        scanned++;
    }

    return 0;
}

int BufferFindBracketBlock(int cy, int cx, int *first_row, int *last_row)
{
    /* whole rows, from the earlier of the bracket and its partner to
       the later one, everything in between included */
    int bracket_cy;
    int bracket_cx;
    int match_cy;
    int match_cx;

    if (!FindBracketPair(cy, cx, &bracket_cy, &bracket_cx,
                         &match_cy, &match_cx))
    {
        return 0;
    }

    if (bracket_cy <= match_cy)
    {
        *first_row = bracket_cy;
        *last_row = match_cy;
    }
    else
    {
        *first_row = match_cy;
        *last_row = bracket_cy;
    }

    return 1;
}

int BufferDuplicateRows(int first, int last)
{
    /* places copies of the rows first through last below them, in
       order and each keeping its own text and ending; the rows they
       were made from stay where they are. Returns 0, or -1 when a
       copy could not be made */
    int at;

    for (at = first; at <= last; at++)
    {
        /* InsertRow may move the rows array, so the source row is
           read through it right at the call; the copies land below
           the last source row, one further down each time */
        vted_row_t *row = &vted_buffer.rows[at];

        if (InsertRow(last + 1 + (at - first), row->chars, row->size,
                      row->eol) == -1)
        {
            return -1;
        }
    }

    return 0;
}

/* does the row's text at byte index at start with the token? */
static int RowStartsWith(const vted_row_t *row, int at, const char *token)
{
    size_t length = strlen(token);

    return (size_t)(row->size - at) >= length &&
           memcmp(&row->chars[at], token, length) == 0;
}

/* how wide the line comment the row carries at its indent is: the
   spelling its text starts with there, or 0 when it starts with
   neither */
static int RowCommentWidth(const vted_row_t *row, int indent,
                           const char *token, const char *token2)
{
    if (token != NULL && RowStartsWith(row, indent, token))
    {
        return (int)strlen(token);
    }

    if (token2 != NULL && RowStartsWith(row, indent, token2))
    {
        return (int)strlen(token2);
    }

    return 0;
}

int BufferToggleComment(int first, int last, const char *token,
                        const char *token2, int *out_changed)
{
    /* puts the line comment in front of the first non-blank of every
       row first through last — or takes it off the rows that carry
       it, when the first row that is not all blanks already does. The
       token goes in with a space behind it and comes off with that
       space when it is there; a row of only blanks is never given
       one, and a row that already carries a token, either spelling,
       keeps the one it has, so a mixed span comes out of one press
       all one way. How many rows changed is reported through
       *out_changed, negative when they were uncommented. Returns 0,
       or -1 when memory ran out half way */
    int direction = 0; /* 1 the token goes in, -1 it comes off */
    int changed = 0;
    int row;

    *out_changed = 0;

    if (token == NULL || vted_buffer.num_rows == 0)
    {
        return 0;
    }

    /* only the rows the span names that are really there */
    if (first < 0)
    {
        first = 0;
    }
    if (last > vted_buffer.num_rows - 1)
    {
        last = vted_buffer.num_rows - 1;
    }
    if (first > last)
    {
        return 0;
    }

    /* the first row that is not all blanks says which way it goes: a
       commented one takes the token off the span, an uncommented one
       puts it in */
    for (row = first; row <= last; row++)
    {
        vted_row_t *line = &vted_buffer.rows[row];
        int indent = RowIndentLength(line, line->size);

        if (indent < line->size)
        {
            direction = RowCommentWidth(line, indent, token, token2) > 0 ? -1 : 1;
            break;
        }
    }

    if (direction == 0)
    {
        return 0; /* nothing but blanks: nothing to comment */
    }

    for (row = first; row <= last; row++)
    {
        vted_row_t *line = &vted_buffer.rows[row];
        int indent = RowIndentLength(line, line->size);

        if (direction > 0)
        {
            int token_len = (int)strlen(token);

            if (indent == line->size ||
                RowCommentWidth(line, indent, token, token2) > 0)
            {
                continue; /* all blanks, or commented already */
            }

            if (RowInsertString(line, indent, token, token_len) == -1 ||
                RowInsertString(line, indent + token_len, " ", 1) == -1)
            {
                return -1;
            }

            changed++;
        }
        else
        {
            int width = RowCommentWidth(line, indent, token, token2);
            int removing = width;

            if (width == 0)
            {
                continue; /* no comment to take off */
            }

            /* the space the commenter left behind comes off with the
               token */
            if (indent + width < line->size &&
                line->chars[indent + width] == ' ')
            {
                removing++;
            }

            while (removing-- > 0)
            {
                if (RowRemoveChar(line, indent) == -1)
                {
                    return -1;
                }
            }

            changed--;
        }
    }

    *out_changed = changed;
    return 0;
}

int BufferIndentRows(int first, int last, const char *unit, int unit_len,
                     int *out_changed)
{
    /* puts one indent unit in front of every row first through last
       that is not all blanks — a row of only blanks keeps none, the
       way it is never given a comment. How many rows changed is
       reported through *out_changed. Returns 0, or -1 when memory
       ran out half way */
    int changed = 0;
    int row;

    *out_changed = 0;

    if (unit == NULL || unit_len < 1 || vted_buffer.num_rows == 0)
    {
        return 0;
    }

    /* only the rows the span names that are really there */
    if (first < 0)
    {
        first = 0;
    }
    if (last > vted_buffer.num_rows - 1)
    {
        last = vted_buffer.num_rows - 1;
    }
    if (first > last)
    {
        return 0;
    }

    for (row = first; row <= last; row++)
    {
        vted_row_t *line = &vted_buffer.rows[row];

        if (RowIndentLength(line, line->size) == line->size)
        {
            continue; /* all blanks: nothing to indent */
        }

        if (RowInsertString(line, 0, unit, unit_len) == -1)
        {
            return -1;
        }

        changed++;
    }

    *out_changed = changed;
    return 0;
}

int BufferOutdentRows(int first, int last, int tab_width, int *out_changed)
{
    /* takes one indent step off every row first through last: the tab
       a row starts with, or else up to tab_width of the spaces it
       starts with, whichever kind it opens with. A row of only blanks
       keeps nothing to give, and a row without blanks to take keeps
       as it is. How many rows changed is reported through
       *out_changed. Returns 0, or -1 when memory ran out half way */
    int changed = 0;
    int row;

    *out_changed = 0;

    if (tab_width < 1 || vted_buffer.num_rows == 0)
    {
        return 0;
    }

    /* only the rows the span names that are really there */
    if (first < 0)
    {
        first = 0;
    }
    if (last > vted_buffer.num_rows - 1)
    {
        last = vted_buffer.num_rows - 1;
    }
    if (first > last)
    {
        return 0;
    }

    for (row = first; row <= last; row++)
    {
        vted_row_t *line = &vted_buffer.rows[row];
        int removing = 0;

        if (line->size == 0 ||
            RowIndentLength(line, line->size) == line->size)
        {
            continue; /* empty, or nothing but blanks */
        }

        if (line->chars[0] == '\t')
        {
            removing = 1; /* the tab a row starts with goes first */
        }
        else
        {
            while (removing < tab_width && removing < line->size &&
                   line->chars[removing] == ' ')
            {
                removing++;
            }
        }

        if (removing == 0)
        {
            continue; /* no indent to take off */
        }

        while (removing-- > 0)
        {
            if (RowRemoveChar(line, 0) == -1)
            {
                return -1;
            }
        }

        changed++;
    }

    *out_changed = changed;
    return 0;
}

void BufferCountMatches(const char *query, int query_len,
                        int row, int col, int *total, int *before)
{
    int total_count = 0;
    int before_count = 0;
    int i;

    for (i = 0; i < vted_buffer.num_rows; i++)
    {
        const vted_row_t *scan_row = &vted_buffer.rows[i];
        int at = 0;

        for (;;)
        {
            at = RowFindNext(scan_row, at, scan_row->size, query, query_len);

            if (at < 0)
            {
                break;
            }

            if (i < row || (i == row && at < col))
            {
                before_count++;
            }

            total_count++;
            at++; /* matches that share bytes count each */
        }
    }

    *total = total_count;
    *before = before_count;
}

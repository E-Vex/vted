/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_regex.h"
#include "vted_syntax.h"
#include "vted_utf8.h"
#include "vted_buffers.h"
/*===============================*/

vted_editor_Config_t vted_editor_config;

#define VTED_VERSION "0.3.3"

/*==========================================================
|                      Append Buffer                       |
|                                                          |
| Collects a whole frame of output in one growable heap    |
| buffer so it can be shipped to the terminal with a       |
| single write() call. Writing many small chunks one at a  |
| time makes the screen flicker, because the terminal      |
| paints each chunk the moment it arrives.                 |
==========================================================*/

typedef struct
{
    char *data; /* heap buffer holding the bytes to write */
    int len;    /* number of bytes currently stored */
} append_buffer_t;

#define ABUF_INIT {NULL, 0}

static void AppendToBuffer(append_buffer_t *ab, const char *s, int len)
{
    char *new_data = realloc(ab->data, (size_t)(ab->len + len));

    if (new_data == NULL)
    {
        KillApp("realloc");
    }

    /* append after the bytes already stored */
    memcpy(&new_data[ab->len], s, (size_t)len);
    ab->data = new_data;
    ab->len += len;
}

static void FreeAppendBuffer(append_buffer_t *ab)
{
    free(ab->data);
}

int GetWindowSize(int *rows, int *cols)
{
    struct winsize window_size;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == -1 || window_size.ws_col == 0 || window_size.ws_row == 0)
    {
        return -1;
    }

    *cols = window_size.ws_col;
    *rows = window_size.ws_row;
    return 0;
}
/* line numbers are at least this many digits wide, so the gutter does
   not jump when a file grows past 9 or 99 lines */
#define GUTTER_MIN_DIGITS 3

/* a terminal narrower than this many text cells drops the gutter, the
   text matters more than the numbers */
#define GUTTER_MIN_TEXT_COLS 20

/* the most nesting levels one row's indent guides can mark: deeper
   nesting than this is unheard of, and the outermost levels are the
   ones left out */
#define INDENT_LEVELS_MAX 64

/* one byte per buffer row: 1 while what is on the terminal for that
   row is stale and must be repainted; 0 when the screen already shows
   the row exactly as it is in the buffer */
static unsigned char *dirty_rows;
static int dirty_rows_capacity;

/* set once the first frame has been painted: before that nothing is
   on the screen, so edits while loading must not repaint row by row */
static int screen_active = 0;

/* the offsets the last frame was drawn at; -1 before the first frame */
static int last_row_offset = -1;
static int last_col_offset = -1;

/* the line the top row was entered at the last frame; -1 before the
   first frame */
static int last_line_offset = -1;

/* the line the cursor stood on when the last frame was drawn: the
   gutter draws that row's number bold, so a move repaints both rows;
   -1 before the first frame */
static int last_cursor_row = -1;

/* the gutter width the last frame was drawn with; -1 before the first
   frame */
static int last_gutter_width = -1;

static void EnsureDirtyRows(int rows)
{
    if (rows <= dirty_rows_capacity)
    {
        return;
    }

    unsigned char *grown = realloc(dirty_rows, (size_t)rows);

    if (grown == NULL)
    {
        KillApp("realloc");
    }

    /* rows the array never held have never been drawn: mark them dirty */
    memset(&grown[dirty_rows_capacity], 1, (size_t)(rows - dirty_rows_capacity));

    dirty_rows = grown;
    dirty_rows_capacity = rows;
}

void ScreenMarkRowDirty(int row)
{
    if (!screen_active)
    {
        return; /* nothing has been painted yet: the first frame
                   repaints everything through InitVtedEditor */
    }

    if (row < 0 || row >= vted_buffer.num_rows)
    {
        return;
    }

    EnsureDirtyRows(vted_buffer.num_rows);
    dirty_rows[row] = 1;
}

void ScreenMarkRowsDirtyFrom(int row)
{
    if (!screen_active)
    {
        return; /* nothing has been painted yet: the first frame
                   repaints everything through InitVtedEditor */
    }

    if (row < 0 || row >= vted_buffer.num_rows)
    {
        return;
    }

    EnsureDirtyRows(vted_buffer.num_rows);
    memset(&dirty_rows[(size_t)row], 1, (size_t)(vted_buffer.num_rows - row));
}

void ScreenMarkAllDirty(void)
{
    if (!screen_active)
    {
        return; /* nothing has been painted yet: the first frame
                   repaints everything through InitVtedEditor */
    }

    EnsureDirtyRows(vted_buffer.num_rows);
    memset(dirty_rows, 1, (size_t)vted_buffer.num_rows);
}

static int CountDigits(int number)
{
    int digits = 1;

    while (number >= 10)
    {
        number /= 10;
        digits++;
    }

    return digits;
}

static void UpdateGutterWidth(int cols)
{
    /* the numbers, one blank to separate them from the text; the
       number of the line the cursor may be about to create (one past
       the last row) has to fit as well */
    int digits = CountDigits(vted_buffer.num_rows + 1);
    int width;

    if (digits < GUTTER_MIN_DIGITS)
    {
        digits = GUTTER_MIN_DIGITS;
    }

    width = digits + 1;

    if (vted_editor_config.show_line_numbers == 0 || cols - width < GUTTER_MIN_TEXT_COLS)
    {
        width = 0;
    }

    vted_editor_config.gutter_width = width;

    /* whatever the gutter does not use is the width the text scrolls in */
    vted_editor_config.text_cols = cols - width;
}

/* -1 on error, 0 when the size is unchanged, 1 when it changed */
int RefreshWindowSize(void)
{
    int rows;
    int cols;

    if (GetWindowSize(&rows, &cols) == -1)
    {
        return -1;
    }

    /* the two bottom rows are reserved for the status bar and the
       message bar, so the text viewport is two rows shorter; the raw
       terminal size is compared against the viewport it would
       produce, not against screen_rows as stored */
    if (rows - 2 == vted_editor_config.screen_rows && cols == vted_editor_config.screen_cols)
    {
        return 0;
    }

    vted_editor_config.screen_rows = rows - 2;
    vted_editor_config.screen_cols = cols;

    return 1;
}

/* the cells every wrapped line after the first of a row keeps blank
   at its start: the row's own leading whitespace laid out, the one
   the renderer draws and the cursor math aims for, while the
   wrap-indent setting asks for them */
static int WrapIndentOf(const vted_row_t *row, int cols)
{
    if (vted_editor_config.wrap_indent == 0)
    {
        return 0;
    }

    return RowWrapIndentCells(row, cols);
}

/* how many screen lines the buffer row takes: one, or as many as it
   wraps into at the text width */
static int RowLines(int buffer_row)
{
    if (vted_editor_config.wrap == 0)
    {
        return 1;
    }

    return RowWrapCount(&vted_buffer.rows[buffer_row], vted_editor_config.text_cols,
                        WrapIndentOf(&vted_buffer.rows[buffer_row],
                                     vted_editor_config.text_cols));
}

void UpdateScroll(void)
{
    /* keep the cursor inside the viewport by moving the offsets;
       the cursor itself keeps absolute buffer coordinates. Rows are
       scrolled in display cells, so the byte index of the cursor is
       first converted to the cell column it is drawn at. A wrapped
       viewport may also start inside its top row, when that row is
       taller than the whole viewport: the lines it skipped are the
       line offset */

    vted_editor_config.cursor_rx = 0;

    if (vted_editor_config.cursor_y < vted_buffer.num_rows)
    {
        vted_editor_config.cursor_rx = RowCxToRx(&vted_buffer.rows[vted_editor_config.cursor_y],
                                                 vted_editor_config.cursor_x);
    }

    if (vted_editor_config.wrap != 0)
    {
        /* a wrapped viewport has no sideways scrolling: every row
           flows from the left edge */
        vted_editor_config.col_offset = 0;

        /* the cursor's own screen line inside its row; the position
           past the last glyph rests on the row's last line, the way
           the renderer draws it */
        int cursor_line = 0;

        if (vted_editor_config.cursor_y < vted_buffer.num_rows)
        {
            int cursor_col;
            int lines = RowLines(vted_editor_config.cursor_y);

            RowCxToWrapped(&vted_buffer.rows[vted_editor_config.cursor_y],
                           vted_editor_config.cursor_x,
                           vted_editor_config.text_cols,
                           WrapIndentOf(&vted_buffer.rows[vted_editor_config.cursor_y],
                                        vted_editor_config.text_cols),
                           &cursor_line, &cursor_col);

            if (cursor_line >= lines)
            {
                cursor_line = lines - 1;
            }
        }

        /* a stale line offset (the row shrank under an edit, or the
           viewport arrived from another buffer) would skip lines
           that are not there */
        if (vted_editor_config.row_offset < vted_buffer.num_rows &&
            vted_editor_config.line_offset >= RowLines(vted_editor_config.row_offset))
        {
            vted_editor_config.line_offset = 0;
        }

        /* the cursor above the viewport puts its row on top, and what
           follows then keeps the cursor's own line in view, also when
           that row is taller than the viewport */
        if (vted_editor_config.cursor_y < vted_editor_config.row_offset)
        {
            vted_editor_config.row_offset = vted_editor_config.cursor_y;
            vted_editor_config.line_offset = 0;
        }

        /* the cursor inside the top row: keep its own line in view,
           also when the row is taller than the whole viewport and
           the top sits inside it */
        if (vted_editor_config.cursor_y == vted_editor_config.row_offset &&
            vted_editor_config.row_offset < vted_buffer.num_rows)
        {
            if (cursor_line < vted_editor_config.line_offset)
            {
                vted_editor_config.line_offset = cursor_line;
            }
            else if (cursor_line >
                     vted_editor_config.line_offset + vted_editor_config.screen_rows - 1)
            {
                vted_editor_config.line_offset =
                    cursor_line - (vted_editor_config.screen_rows - 1);
            }

            return;
        }

        /* walk up from the cursor: the rows that fit above it stay,
           the one that does not becomes the top. A cursor deep inside
           a row taller than the viewport makes that row the top,
           scrolled down to the cursor's line, so the viewport can
           start in the middle of a row */
        {
            int taken = cursor_line;
            int row;

            for (row = vted_editor_config.cursor_y - 1;
                 row >= vted_editor_config.row_offset;
                 row--)
            {
                int lines = RowLines(row);

                if (row == vted_editor_config.row_offset)
                {
                    /* the top row enters past the lines it skipped */
                    lines -= vted_editor_config.line_offset;
                }

                if (taken + lines > vted_editor_config.screen_rows - 1)
                {
                    if (row + 1 == vted_editor_config.cursor_y &&
                        cursor_line > vted_editor_config.screen_rows - 1)
                    {
                        vted_editor_config.row_offset = vted_editor_config.cursor_y;
                        vted_editor_config.line_offset =
                            cursor_line - (vted_editor_config.screen_rows - 1);
                    }
                    else
                    {
                        vted_editor_config.row_offset = row + 1;
                        vted_editor_config.line_offset = 0;
                    }
                    break;
                }

                taken += lines;
            }
        }

        return;
    }

    if (vted_editor_config.cursor_y < vted_editor_config.row_offset)
    {
        vted_editor_config.row_offset = vted_editor_config.cursor_y;
    }

    if (vted_editor_config.cursor_y >= vted_editor_config.row_offset + vted_editor_config.screen_rows)
    {
        vted_editor_config.row_offset = vted_editor_config.cursor_y - vted_editor_config.screen_rows + 1;
    }

    if (vted_editor_config.cursor_rx < vted_editor_config.col_offset)
    {
        vted_editor_config.col_offset = vted_editor_config.cursor_rx;
    }

    if (vted_editor_config.cursor_rx >= vted_editor_config.col_offset + vted_editor_config.text_cols)
    {
        vted_editor_config.col_offset = vted_editor_config.cursor_rx - vted_editor_config.text_cols + 1;
    }
}

/* the cursor's own screen line inside its row; the position past the
   row's last glyph rests on its last line, and the phantom row past
   the end has the one line it has */
static int CursorLineInRow(void)
{
    int line = 0;

    if (vted_editor_config.cursor_y < vted_buffer.num_rows)
    {
        int col;
        int lines = RowLines(vted_editor_config.cursor_y);

        RowCxToWrapped(&vted_buffer.rows[vted_editor_config.cursor_y],
                       vted_editor_config.cursor_x,
                       vted_editor_config.text_cols,
                       WrapIndentOf(&vted_buffer.rows[vted_editor_config.cursor_y],
                                    vted_editor_config.text_cols),
                       &line, &col);

        if (line >= lines)
        {
            line = lines - 1;
        }
    }

    return line;
}

/* is the cursor's own screen line inside the viewport: the rows
   between the top and the cursor count at their real heights, so a
   tall row above can push the cursor off screen even when its row
   number alone says otherwise */
static int CursorOnScreen(void)
{
    int used = CursorLineInRow() - vted_editor_config.line_offset;
    int row;

    if (vted_editor_config.cursor_y < vted_editor_config.row_offset)
    {
        return 0;
    }

    for (row = vted_editor_config.row_offset;
         row < vted_editor_config.cursor_y && row < vted_buffer.num_rows;
         row++)
    {
        used += RowLines(row);

        if (used >= vted_editor_config.screen_rows)
        {
            return 0;
        }
    }

    return used >= 0 && used < vted_editor_config.screen_rows;
}

/* a line that is already on screen stays where it is; one that is
   not gets the middle of the viewport, so there is context around
   it instead of the line sitting on the very edge. With wrapping on
   the rows count at their real heights: a tall row above the cursor
   pushes the top up line by line, and may be entered mid-row */
void ScreenCenterCursor(void)
{
    int first = vted_editor_config.row_offset;
    int last = first + vted_editor_config.screen_rows;

    if (vted_editor_config.wrap != 0)
    {
        int above = vted_editor_config.screen_rows / 2;
        int line = CursorLineInRow();
        int remaining = above - line;
        int row = vted_editor_config.cursor_y - 1;

        if (CursorOnScreen())
        {
            return;
        }

        /* half a viewport of screen lines belongs above the cursor's
           own line: when its own row already has that many lines
           above it, that row is the top, entered at the line the
           half ended on */
        if (line >= above)
        {
            vted_editor_config.row_offset = vted_editor_config.cursor_y;
            vted_editor_config.line_offset = line - above;
            return;
        }

        /* whole rows above, while they fit in the half */
        while (row >= 0)
        {
            int lines = RowLines(row);

            if (remaining < lines)
            {
                break;
            }

            remaining -= lines;
            row--;
        }

        if (row < 0)
        {
            /* the buffer ran out above: the first row is the top */
            vted_editor_config.row_offset = 0;
            vted_editor_config.line_offset = 0;
        }
        else if (remaining == 0)
        {
            /* the rows above filled the half exactly: the next one
               down is the top, from its own first line */
            vted_editor_config.row_offset = row + 1;
            vted_editor_config.line_offset = 0;
        }
        else
        {
            /* the half ends inside this row: the viewport enters it
               mid-row */
            vted_editor_config.row_offset = row;
            vted_editor_config.line_offset = RowLines(row) - remaining;
        }

        return;
    }

    if (vted_editor_config.cursor_y < first || vted_editor_config.cursor_y >= last)
    {
        int offset = vted_editor_config.cursor_y - vted_editor_config.screen_rows / 2;
        int max_offset = vted_buffer.num_rows - vted_editor_config.screen_rows;

        if (offset > max_offset)
        {
            offset = max_offset; /* no empty screen below the last line */
        }
        if (offset < 0)
        {
            offset = 0;
        }

        vted_editor_config.row_offset = offset;
    }
}

/* the Unicode replacement character, drawn for bytes that are not
   valid UTF-8 so they can never reach the terminal as raw codes */
#define REPLACEMENT_CHARACTER "\xEF\xBF\xBD"

/* does the cell carry one of the indent guides' dots: the last cell
   of a nesting level, where its indent ends */
static int CellIsGuideEdge(int cell, const int *edges, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        if (edges[i] >= 1 && cell == edges[i] - 1)
        {
            return 1;
        }
    }

    return 0;
}

/* does any cell the glyph takes carry one of the indent guides' dots */
static int GlyphCarriesGuideDot(int cell, int width, const int *edges, int count)
{
    int offset;

    for (offset = 0; offset < width; offset++)
    {
        if (CellIsGuideEdge(cell + offset, edges, count))
        {
            return 1;
        }
    }

    return 0;
}

static void AppendGlyph(append_buffer_t *ab, const vted_row_t *row, int at, int next,
                        int cell, int width, int left, int right,
                        const int *guide_edges, int guide_count)
{
    /* draws the character at byte index at, which occupies the cells
       [cell, cell + width), clipped to the viewport cells [left,
       right). guide_edges names the nesting levels whose last cell
       the indent guides dot; an empty list (guide_count 0, the
       pointer may be NULL) says the glyph is not one they mark */
    unsigned char byte = (unsigned char)row->chars[at];
    int code_point;
    int offset;

    if (byte == '\t')
    {
        /* a tab is blank space up to the next tab stop; when the tabs
           are asked to be shown, the space is dotted so the tab is
           there to see: a marker on its first cell, dots on the rest.
           The cells it takes do not change, so every measurement of
           the row stays as it was */
        for (offset = 0; offset < width; offset++)
        {
            if (cell + offset >= left && cell + offset < right)
            {
                if (vted_editor_config.show_tabs)
                {
                    AppendToBuffer(ab, offset == 0 ? "\xC2\xBB" : "\xC2\xB7",
                                   2); /* » and · */
                }
                else if (CellIsGuideEdge(cell + offset, guide_edges, guide_count))
                {
                    /* the cell ends a nesting level: a dot, wherever
                       in the tab's run it sits */
                    AppendToBuffer(ab, "\xC2\xB7", 2); /* · */
                }
                else
                {
                    AppendToBuffer(ab, " ", 1);
                }
            }
        }
        return;
    }

    if (byte < 0x20 || byte == 0x7F)
    {
        /* control characters are drawn as ^X, and DEL as ^? */
        char caret[2];

        caret[0] = '^';
        caret[1] = byte == 0x7F ? '?' : (char)(byte + '@');

        for (offset = 0; offset < 2; offset++)
        {
            if (cell + offset >= left && cell + offset < right)
            {
                AppendToBuffer(ab, &caret[offset], 1);
            }
        }
        return;
    }

    if (CellIsGuideEdge(cell, guide_edges, guide_count))
    {
        /* a space that ends a nesting level is a guide dot; the cell
           it takes does not change, so every measurement of the row
           stays as it was */
        if (cell >= left && cell + width <= right)
        {
            AppendToBuffer(ab, "\xC2\xB7", 2); /* · */
        }
        return;
    }

    Utf8Decode(&row->chars[at], row->size - at, &code_point);

    if (width == 0)
    {
        /* a combining mark rides on the cell before it: draw it only
           when that cell is on screen */
        if (cell > left && cell <= right)
        {
            AppendToBuffer(ab, &row->chars[at], next - at);
        }
        return;
    }

    if (cell >= left && cell + width <= right)
    {
        if (code_point < 0 || (code_point >= 0x80 && code_point < 0xA0))
        {
            AppendToBuffer(ab, REPLACEMENT_CHARACTER, 3);
        }
        else
        {
            AppendToBuffer(ab, &row->chars[at], next - at);
        }
        return;
    }

    /* a wide character cut by a viewport edge: fill the visible half
       with blanks instead of drawing half of it */
    if (cell + width > left && cell < right)
    {
        int first = cell > left ? cell : left;
        int last = cell + width < right ? cell + width : right;

        for (offset = first; offset < last; offset++)
        {
            AppendToBuffer(ab, " ", 1);
        }
    }
}

/* the class of every byte of the row being drawn, kept between frames so
   that drawing does not allocate */
static unsigned char *highlight_classes;
static int highlight_capacity;

static const unsigned char *ClassesOfRow(int buffer_row, int size)
{
    if (size > highlight_capacity)
    {
        unsigned char *grown = realloc(highlight_classes, (size_t)size);

        if (grown == NULL)
        {
            KillApp("realloc");
        }

        highlight_classes = grown;
        highlight_capacity = size;
    }

    SyntaxHighlightRow(buffer_row, highlight_classes);
    return highlight_classes;
}

/* the text being searched for, borrowed from the editor core while
   the search runs: every match of it is drawn with the HlMatch
   background, the one the cursor sits on inverted as before. When
   the search reads a pattern instead, the text stands idle and the
   compiled pattern answers where the matches are */
static const char *search_query;
static int search_query_len;
static int search_is_regex = 0;

/* the byte the matches of the query start at in the row being drawn,
   in order, kept between frames so that drawing does not allocate;
   the byte each of them ends at walks beside them, for a match of a
   pattern is as long as the pattern finds it, not as long as the
   query is typed */
static int *query_match_starts;
static int *query_match_ends;
static int query_match_capacity;

void ScreenSetSearchQuery(const char *query, int len)
{
    /* rows that carried matches of the old query repaint without
       them and rows with matches of the new one with them; marking
       everything is simpler than tracking which rows match, and the
       viewport is what gets painted anyway */
    if (search_query != NULL || query != NULL)
    {
        ScreenMarkAllDirty();
    }

    if (len <= 0)
    {
        query = NULL; /* an empty query matches nothing */
    }

    search_query = query;
    search_query_len = query != NULL ? len : 0;
}

void ScreenSetSearchRegex(int on)
{
    if (search_is_regex != (on != 0))
    {
        ScreenMarkAllDirty();
    }

    search_is_regex = on != 0;
}

/* collects where the search matches in the row, in order: matches
   may overlap the way the search steps through them, and there can
   be no more of them than the row has bytes. Returns NULL when there
   are none */
static const int *QueryMatchesOfRow(const vted_row_t *row, int *count)
{
    int found = 0;
    int at = 0;

    if (row->size > query_match_capacity)
    {
        int *grown_starts = realloc(query_match_starts,
                                    (size_t)row->size * sizeof(int));
        int *grown_ends = realloc(query_match_ends,
                                  (size_t)row->size * sizeof(int));

        if (grown_starts == NULL || grown_ends == NULL)
        {
            KillApp("realloc");
        }

        query_match_starts = grown_starts;
        query_match_ends = grown_ends;
        query_match_capacity = row->size;
    }

    if (search_is_regex)
    {
        /* the compiled pattern answers where the matches are, each as
           long as it turns out to be; the walk steps to the next
           character after every one of them, so none is met twice */
        for (;;)
        {
            int len = 0;
            int hit = RegexFindNext(row->chars, at, row->size, &len, NULL);

            if (hit < 0)
            {
                break;
            }

            query_match_starts[found] = hit;
            query_match_ends[found] = hit + len;
            found++;
            at = Utf8NextBoundary(row->chars, row->size, hit);

            if (found >= row->size)
            {
                break;
            }
        }
    }
    else
    {
        while (found < row->size)
        {
            at = RowFindNext(row, at, row->size, search_query,
                             search_query_len);

            if (at < 0)
            {
                break;
            }

            query_match_starts[found] = at;
            query_match_ends[found] = at + search_query_len;
            found++;
            at++; /* the next match may share bytes with this one */
        }
    }

    *count = found;
    return found > 0 ? query_match_starts : NULL;
}

static void AppendStyle(append_buffer_t *ab, int hl_class, int inverted,
                        int marked)
{
    /* every change starts from plain text, so no attribute of the
       previous style can linger; the wrap column marker's background
       goes on top of the color, except under an inversion, where the
       cell already stands out on its own */
    const char *color = SyntaxStyle(hl_class);

    AppendToBuffer(ab, "\x1b[m", 3);

    if (inverted)
    {
        AppendToBuffer(ab, "\x1b[7m", 4);
    }

    AppendToBuffer(ab, color, (int)strlen(color));

    if (marked && !inverted)
    {
        const char *marker = SyntaxStyle(HlMarker);

        AppendToBuffer(ab, marker, (int)strlen(marker));
    }
}

/* the selection the editor core hands over after every key press; the
   renderer draws it inverted */
static int sel_active;
static int sel_y0;
static int sel_x0;
static int sel_y1;
static int sel_x1;

/* the matching bracket the editor core found for the cursor's
   bracket, the same way */
static int bracket_row = -1;
static int bracket_col;

/* the menu the editor core shows over the bottom of the viewport, if
   any: its lines are borrowed from the core and only the pointers are
   kept here, valid until the next ScreenShowMenu() call */
static const char *const *menu_lines;
static int menu_count;
static int menu_selected;

void ScreenShowMenu(const char *const *lines, int count, int selected)
{
    /* rows the menu covered were skipped by their own dirty flags
       while it was up: once it is gone they have to be painted again */
    if (menu_count > 0 && count == 0)
    {
        ScreenMarkAllDirty();
    }

    menu_lines = lines;
    menu_count = count;
    menu_selected = selected;
}

int ScreenMenuLineAt(int row)
{
    /*=================================================
    | The menu covers the bottom rows of the         |
    | viewport, drawing the last of its lines when it |
    | holds more than the screen shows — the same    |
    | window the frame draws, walked backwards here, |
    | so a click always lands on the very line it    |
    | was aimed at                                   |
    =================================================*/

    int shown = menu_count;
    int first;

    if (shown <= 0)
    {
        return -1; /* no menu is up */
    }

    if (shown > vted_editor_config.screen_rows)
    {
        shown = vted_editor_config.screen_rows;
    }

    first = menu_count - shown;

    if (row < vted_editor_config.screen_rows - shown)
    {
        return -1; /* a row of the text above the menu */
    }

    if (row >= vted_editor_config.screen_rows)
    {
        return -1; /* the bars below the viewport */
    }

    return first + row - (vted_editor_config.screen_rows - shown);
}

void ScreenSetBracketMatch(int row, int col)
{
    /* the row the old highlight was on is repainted without it and
       the new one with it; a match that is not there (row < 0) only
       clears */
    if (bracket_row >= 0)
    {
        ScreenMarkRowDirty(bracket_row);
    }

    bracket_row = row;
    bracket_col = col;

    if (bracket_row >= 0)
    {
        ScreenMarkRowDirty(bracket_row);
    }
}

void ScreenSetSelection(int top, int left, int bottom, int right)
{
    int row;

    if (sel_active)
    {
        for (row = sel_y0; row <= sel_y1; row++)
        {
            ScreenMarkRowDirty(row);
        }
    }

    sel_active = top >= 0;
    sel_y0 = top;
    sel_x0 = left;
    sel_y1 = bottom;
    sel_x1 = right;

    if (sel_active)
    {
        for (row = top; row <= bottom; row++)
        {
            ScreenMarkRowDirty(row);
        }
    }
}

static void DrawRowText(append_buffer_t *ab, int buffer_row, const vted_row_t *row,
                        int has_match, int left, int right)
{
    /* one pass over the row: every character is laid out in terminal
       cells, then clipped to the cells [left, right). Syntax colors
       come from the highlight classes; control characters, the
       selection and the active search match are drawn in inverted
       video, the bracket match too unless a color was chosen for it,
       and the other matches of the search query with a background of
       their own */
    const unsigned char *classes = ClassesOfRow(buffer_row, row->size);
    int match_end = vted_editor_config.match_col + vted_editor_config.match_len;
    const int *query_matches = NULL;
    int query_match_count = 0;
    int next_query_match = 0;
    /* the wrap column marker: the text cell it sits on, or nothing
       when it is hidden; it is drawn on top of whatever glyph covers
       it, and over the blank tail of the line when none does */
    int marker = vted_editor_config.wrap_column;
    /* the cells every wrapped line after the first keeps blank at
       its start: the row's own leading whitespace laid out, so the
       continuation lines line up under the first one's text. The
       walk places every glyph past the padding, and the padding
       itself stays blank */
    int indent = vted_editor_config.wrap != 0
                     ? WrapIndentOf(row, vted_editor_config.text_cols)
                     : 0;
    /* the bytes of leading whitespace the indent guides mark, and
       the nesting levels whose last cell they dot: nothing to mark
       when they are off, and a row without an indent keeps none */
    int indent_bytes = vted_editor_config.indent_guides
                           ? RowIndentLength(row, row->size)
                           : 0;
    int guide_edges[INDENT_LEVELS_MAX];
    int guide_count = vted_editor_config.indent_guides
                          ? BufferIndentEdges(buffer_row, guide_edges,
                                              INDENT_LEVELS_MAX)
                          : 0;
    int emitted = left; /* the next cell the stream of output reaches */
    int cell = 0;
    int at = 0;
    int in_run = 0;
    int inverted = 0;
    int is_marked = 0;
    int style = HlNormal;
    int sel_start = -1;
    int sel_end = -1;

    /* the matches of the search query in this row, when a search is
       on: they are walked along with the glyphs, which are in order
       too, so each match is only looked at once */
    if (search_query != NULL || (search_is_regex && RegexHasPattern()))
    {
        query_matches = QueryMatchesOfRow(row, &query_match_count);
    }

    /* which bytes of this row fall inside the selection */
    if (sel_active && buffer_row >= sel_y0 && buffer_row <= sel_y1)
    {
        if (buffer_row == sel_y0 && buffer_row == sel_y1)
        {
            sel_start = sel_x0;
            sel_end = sel_x1;
        }
        else if (buffer_row == sel_y0)
        {
            sel_start = sel_x0;
            sel_end = row->size;
        }
        else if (buffer_row == sel_y1)
        {
            sel_start = 0;
            sel_end = sel_x1;
        }
        else
        {
            sel_start = 0;
            sel_end = row->size;
        }
    }

    while (at < row->size && cell < right)
    {
        int next = Utf8NextBoundary(row->chars, row->size, at);
        int glyph_at = at;
        /* the shared wrap walk places the glyph and moves past it in
           one step, so the cells it lays out are the same ones every
           other part of the editor measures; the cells between the
           placement and where the walk ended up tell its width */
        int placed = RowWrapAdvance(row, vted_editor_config.wrap,
                                    vted_editor_config.text_cols, indent,
                                    &at, &cell, &in_run);
        int width = cell - placed;
        unsigned char byte = (unsigned char)row->chars[glyph_at];

        int is_control = (byte < 0x20 && byte != '\t') || byte == 0x7F;
        int in_match = has_match && glyph_at >= vted_editor_config.match_col && glyph_at < match_end;
        int in_bracket = buffer_row == bracket_row && glyph_at == bracket_col;
        /* a color was chosen for the match: its cell takes that
           background instead of inverted video */
        int bracket_colored = in_bracket && SyntaxBracketColor() > 0;
        int in_query_match = 0;
        int want_inverted = is_control || in_match ||
                            (in_bracket && !bracket_colored) ||
                            (sel_start >= 0 && glyph_at >= sel_start && glyph_at < sel_end);
        int marked = marker > 0 && marker >= left && marker < right &&
                     placed <= marker && marker < placed + width;
        int glyph_class;

        if (placed > emitted)
        {
            /* the layout left cells blank since the stream's end: the
               padding a continuation line keeps for its indent. They
               are written out so the glyphs after them land where the
               layout put them, plain as the background they are */
            int pad;

            if (inverted || style != HlNormal || is_marked)
            {
                AppendToBuffer(ab, "\x1b[m", 3);
                inverted = 0;
                style = HlNormal;
                is_marked = 0;
            }

            for (pad = emitted; pad < placed; pad++)
            {
                if (pad >= left && pad < right)
                {
                    AppendToBuffer(ab, " ", 1);
                }
            }

            emitted = placed;
        }

        if (query_match_count > 0)
        {
            /* skip the matches that end before this glyph; the first
               one left, when it starts at or before the glyph, holds
               it */
            while (next_query_match < query_match_count &&
                   query_match_ends[next_query_match] <= glyph_at)
            {
                next_query_match++;
            }

            in_query_match = next_query_match < query_match_count &&
                             query_matches[next_query_match] <= glyph_at;
        }

        /* a glyph that stands out in inverted video keeps the color
           of its text under the inversion; a plain one that is the
           partner of the bracket the cursor sits on takes the color
           chosen for it, one inside a match of the query gets the
           match background, a tab whose run is dotted in gets the tab
           gray, and a blank the indent guides mark gets their blue */
        glyph_class = classes[glyph_at];

        if (!want_inverted)
        {
            if (bracket_colored)
            {
                glyph_class = HlBracket;
            }
            else if (in_query_match)
            {
                glyph_class = HlMatch;
            }
            else if (byte == '\t' && vted_editor_config.show_tabs)
            {
                glyph_class = HlTab;
            }
            else if (glyph_at < indent_bytes &&
                     GlyphCarriesGuideDot(placed, width, guide_edges, guide_count))
            {
                /* leading whitespace that covers the last cell of a
                   nesting level: the guides dot it, the way a shown
                   tab dots the cells of its run */
                glyph_class = HlIndent;
            }
        }

        if (want_inverted != inverted || glyph_class != style ||
            marked != is_marked)
        {
            style = glyph_class;
            inverted = want_inverted;
            is_marked = marked;
            AppendStyle(ab, style, inverted, is_marked);
        }

        AppendGlyph(ab, row, glyph_at, next, placed, width, left, right,
                    glyph_class == HlIndent ? guide_edges : NULL,
                    glyph_class == HlIndent ? guide_count : 0);

        if (placed <= emitted && placed + width > emitted)
        {
            /* the stream of output reaches past this glyph, never past
               the viewport's right edge; a glyph the wrap moved down
               starts past the padding it left, and the padding stays
               blank: the marker may have to be drawn there */
            emitted = placed + width < right ? placed + width : right;
        }
    }

    /* the marker over the blank tail of the line: past the text, or
       past the run a wrap moved down, no glyph carries it there */
    if (marker > 0 && marker >= left && marker < right && marker >= emitted)
    {
        int pad;

        if (inverted || style != HlNormal)
        {
            AppendToBuffer(ab, "\x1b[m", 3);
            inverted = 0;
            style = HlNormal;
            is_marked = 0;
        }

        for (pad = emitted; pad < marker; pad++)
        {
            AppendToBuffer(ab, " ", 1);
        }

        {
            const char *marker_style = SyntaxStyle(HlMarker);

            AppendToBuffer(ab, marker_style, (int)strlen(marker_style));
            AppendToBuffer(ab, " ", 1);
        }

        /* the blank cell after the marker is erased with ESC[K, which
           keeps the background it finds: back to plain before then */
        AppendToBuffer(ab, "\x1b[m", 3);
    }

    if (inverted || style != HlNormal)
    {
        AppendToBuffer(ab, "\x1b[m", 3);
    }
}

static void DrawGutter(append_buffer_t *ab, int buffer_row)
{
    /* the line number, right-aligned and followed by one blank. The
       number of the line the cursor sits on is bold, the others are
       gray so they stay out of the way of the text */
    char number[24];
    int digits = vted_editor_config.gutter_width - 1;
    int is_current = buffer_row == vted_editor_config.cursor_y;
    int length;

    if (vted_editor_config.gutter_width == 0)
    {
        return;
    }

    length = snprintf(number, sizeof(number), "%*d ", digits, buffer_row + 1);

    if (length < 0)
    {
        return;
    }

    AppendToBuffer(ab, is_current ? "\x1b[1m" : "\x1b[90m", is_current ? 4 : 5);
    AppendToBuffer(ab, number, length);
    AppendToBuffer(ab, "\x1b[m", 3);
}

/* the lines a wrapped row continues on get the gutter's width in
   blanks, so the text lines up under the first line's */
static void DrawGutterBlank(append_buffer_t *ab)
{
    int cell;

    if (vted_editor_config.gutter_width == 0)
    {
        return;
    }

    for (cell = 0; cell < vted_editor_config.gutter_width; cell++)
    {
        AppendToBuffer(ab, " ", 1);
    }
}

/* positions the terminal cursor at the start of screen line row, unless
   the cursor is already there from the previous row drawn */
static void GotoScreenRow(append_buffer_t *ab, int *at, int row)
{
    if (*at == row)
    {
        return;
    }

    char position[32];
    int position_len = snprintf(position, sizeof(position), "\x1b[%d;1H", row + 1);

    if (position_len > 0)
    {
        AppendToBuffer(ab, position, (size_t)position_len);
    }

    *at = row;
}

static void DrawRows(append_buffer_t *ab, int *at)
{
    int screen_row = 0;
    int buffer_row = vted_editor_config.row_offset;

    while (screen_row < vted_editor_config.screen_rows)
    {
        if (buffer_row >= vted_buffer.num_rows)
        {
            /* tildes below the text and the welcome banner are drawn
               every frame: they depend on nothing in the buffer */
            GotoScreenRow(ab, at, screen_row);

            if (buffer_row == vted_buffer.num_rows &&
                vted_editor_config.cursor_y == buffer_row &&
                vted_editor_config.gutter_width > 0)
            {
                /* the line the cursor stands on past the end of the
                   text: it gets its number too, so the cursor never
                   sits in an empty gutter */
                DrawGutter(ab, buffer_row);
            }
            else if (vted_buffer.num_rows == 0 &&
                     screen_row == vted_editor_config.screen_rows / 2)
            {
                /* empty buffer: show a welcome banner in the middle
                   row; a loaded file only gets the tildes below its
                   text */
                char welcome[80];
                int welcome_len = snprintf(welcome, sizeof(welcome),
                                           "vted -- version %s", VTED_VERSION);

                if (welcome_len < 0)
                {
                    welcome_len = 0;
                }
                if (welcome_len > (int)sizeof(welcome))
                {
                    welcome_len = (int)sizeof(welcome);
                }

                /* on a terminal narrower than the banner it would wrap
                   onto the next row and scroll the whole screen */
                if (welcome_len > vted_editor_config.screen_cols)
                {
                    welcome_len = vted_editor_config.screen_cols;
                }

                int padding = (vted_editor_config.screen_cols - welcome_len) / 2;

                /* center the banner, one space after the tilde */
                if (padding > 0)
                {
                    AppendToBuffer(ab, "~", 1);
                    padding--;
                }
                while (padding-- > 0)
                {
                    AppendToBuffer(ab, " ", 1);
                }

                AppendToBuffer(ab, welcome, welcome_len);
            }
            else
            {
                AppendToBuffer(ab, "~", 1);
            }

            AppendToBuffer(ab, "\x1b[K", 3); /* erase from cursor to end of line */

            AppendToBuffer(ab, "\r\n", 2);
            (*at)++;
            screen_row++;
            buffer_row++;
            continue;
        }

        EnsureDirtyRows(vted_buffer.num_rows);

        if (dirty_rows[buffer_row] == 0)
        {
            /* a clean row is not painted, but the screen lines it
               takes still scroll by — fewer for the top row, whose
               first lines the viewport skipped */
            int skipped = buffer_row == vted_editor_config.row_offset
                              ? vted_editor_config.line_offset
                              : 0;

            screen_row += RowLines(buffer_row) - skipped;
            buffer_row++;
            continue;
        }

        /* the row paints on one screen line, or on as many as it
           wraps into; the top one paints from the line the viewport
           starts at, every row below from its first */
        {
            vted_row_t *text_row = &vted_buffer.rows[buffer_row];
            int has_match = buffer_row == vted_editor_config.match_row &&
                            vted_editor_config.match_len > 0;
            int lines = RowLines(buffer_row);
            int first_line = buffer_row == vted_editor_config.row_offset
                                 ? vted_editor_config.line_offset
                                 : 0;
            int line;

            if (first_line > lines)
            {
                first_line = lines; /* nothing of the row is on screen */
            }

            for (line = first_line; line < lines && screen_row < vted_editor_config.screen_rows; line++)
            {
                /* clean rows skipped above leave the terminal cursor
                   wherever the last drawn row ended: this line is
                   placed explicitly, and the common case of
                   consecutive rows costs nothing */
                GotoScreenRow(ab, at, screen_row);

                if (line == 0)
                {
                    DrawGutter(ab, buffer_row);
                }
                else
                {
                    DrawGutterBlank(ab);
                }

                if (vted_editor_config.wrap != 0)
                {
                    /* line k of the wrapped row is the cells
                       [k*text_cols, (k+1)*text_cols) */
                    DrawRowText(ab, buffer_row, text_row, has_match,
                                line * vted_editor_config.text_cols,
                                (line + 1) * vted_editor_config.text_cols);
                }
                else
                {
                    DrawRowText(ab, buffer_row, text_row, has_match,
                                vted_editor_config.col_offset,
                                vted_editor_config.col_offset + vted_editor_config.text_cols);
                }

                AppendToBuffer(ab, "\x1b[K", 3); /* erase from cursor to end of line */

                AppendToBuffer(ab, "\r\n", 2);
                (*at)++;
                screen_row++;
            }

            dirty_rows[buffer_row] = 0; /* the screen now matches the buffer */
            buffer_row++;
        }
    }
}

/* draws the menu over the bottom rows of the viewport: one borrowed
   line per row, the selected one inverted over the full width. It is
   drawn every frame, whatever the dirty flags say, because the rows
   underneath it do not know they are covered */
static void DrawMenu(append_buffer_t *ab, int *at)
{
    int shown = menu_count;
    int first;
    int i;

    if (shown <= 0)
    {
        return;
    }

    /* more lines than viewport rows: the last ones are the ones drawn */
    if (shown > vted_editor_config.screen_rows)
    {
        shown = vted_editor_config.screen_rows;
    }

    first = menu_count - shown;

    for (i = 0; i < shown; i++)
    {
        const char *line = menu_lines[first + i] != NULL ? menu_lines[first + i] : "";
        int len = (int)strlen(line);
        int screen_row = vted_editor_config.screen_rows - shown + i;

        if (len > vted_editor_config.screen_cols)
        {
            len = vted_editor_config.screen_cols;
        }

        GotoScreenRow(ab, at, screen_row);

        /* every line starts plain, so no attribute of the row drawn
           before it can linger */
        AppendToBuffer(ab, "\x1b[m", 3);

        if (first + i == menu_selected)
        {
            AppendToBuffer(ab, "\x1b[7m", 4);
        }

        AppendToBuffer(ab, line, len);

        if (first + i == menu_selected)
        {
            /* the selection stands out over the whole width */
            int padding = vted_editor_config.screen_cols - len;

            while (padding-- > 0)
            {
                AppendToBuffer(ab, " ", 1);
            }

            AppendToBuffer(ab, "\x1b[m", 3);
        }

        AppendToBuffer(ab, "\x1b[K", 3);
    }
}

static void DrawStatusBar(append_buffer_t *ab, int *at)
{
    /* with text rows skipped, the terminal cursor is not necessarily
       at the top of the bar: place it there explicitly */
    GotoScreenRow(ab, at, vted_editor_config.screen_rows);

    /* inverted video makes the bar stand out from the text */
    AppendToBuffer(ab, "\x1b[7m", 4);

    char status[80];
    int status_len = snprintf(status, sizeof(status), " %.20s - %d lines %s",
                              vted_buffer.filename != NULL ? vted_buffer.filename : "[No Name]",
                              vted_buffer.num_rows,
                              vted_buffer.dirty != 0 ? "-- MODIFIED --" : "");

    if (status_len < 0)
    {
        status_len = 0;
    }
    if (status_len > (int)sizeof(status))
    {
        status_len = (int)sizeof(status);
    }
    if (status_len > vted_editor_config.screen_cols)
    {
        status_len = vted_editor_config.screen_cols;
    }

    AppendToBuffer(ab, status, status_len);

    /* right side, when text exists: the line out of the total and the
       display column the cursor is drawn at ("Ln 5/120, Col 12"). The
       column counts cells, not bytes, so after a tab or a wide
       character it matches what the screen shows. With several
       buffers open, which one of them this is comes first, as
       "2/3, Ln 5/120, Col 12" */
    char right_status[64];
    int right_len = 0;

    if (vted_buffer.num_rows > 0)
    {
        int buffers = BuffersCount();
        int prefix = 0;

        if (buffers > 1)
        {
            prefix = snprintf(right_status, sizeof(right_status), "%d/%d, ",
                              BuffersCurrent() + 1, buffers);

            if (prefix < 0)
            {
                prefix = 0;
            }
        }

        right_len = snprintf(right_status + prefix,
                              sizeof(right_status) - (size_t)prefix,
                              "Ln %d/%d, Col %d",
                              vted_editor_config.cursor_y + 1, vted_buffer.num_rows,
                              vted_editor_config.cursor_rx + 1);

        if (right_len >= 0)
        {
            right_len += prefix;
        }

        /* beside the file info the long form may not fit: fall back to
           the line alone */
        if (right_len < 0 || status_len + right_len > vted_editor_config.screen_cols)
        {
            right_len = snprintf(right_status, sizeof(right_status), "%d/%d",
                                 vted_editor_config.cursor_y + 1, vted_buffer.num_rows);
        }

        if (right_len < 0)
        {
            right_len = 0;
        }
    }

    /* on a narrow terminal even that does not fit: drawing it anyway
       would wrap the bar and scroll the screen */
    if (status_len + right_len > vted_editor_config.screen_cols)
    {
        right_len = 0;
    }

    /* pad the inverted bar to the full width, position at the far end */
    int padding = vted_editor_config.screen_cols - status_len - right_len;

    while (padding-- > 0)
    {
        AppendToBuffer(ab, " ", 1);
    }

    if (right_len > 0)
    {
        AppendToBuffer(ab, right_status, right_len);
    }

    AppendToBuffer(ab, "\x1b[m", 3); /* reset colors */
    AppendToBuffer(ab, "\r\n", 2);
    *at = vted_editor_config.screen_rows + 1;
}

static void DrawMessageBar(append_buffer_t *ab)
{
    /* the message bar is the last screen row; erase it, then show
       the most recent status message clipped to the viewport */
    AppendToBuffer(ab, "\x1b[K", 3);

    int message_len = (int)strlen(vted_editor_config.status_message);

    if (message_len > vted_editor_config.screen_cols)
    {
        message_len = vted_editor_config.screen_cols;
    }

    if (message_len > 0)
    {
        AppendToBuffer(ab, vted_editor_config.status_message, message_len);
    }
}

void SetStatusMessage(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(vted_editor_config.status_message, sizeof(vted_editor_config.status_message), format, args);
    va_end(args);
}

/* where a vertical aim lands when it would fall inside the row's
   leading indent: past it, on the first non-blank glyph, so the
   cursor never rests between the blanks. Only a remembered column
   does this — the one a run of vertical moves keeps aiming for —
   because a cursor a scroll drags along or a jump places keeps the
   spot it was given */
static int AimPastIndent(const vted_row_t *row, int cx)
{
    int indent;

    if (vted_editor_config.preferred_rx < 0)
    {
        return cx; /* no aim in flight: nothing to move past */
    }

    indent = RowIndentLength(row, row->size);

    return cx < indent ? indent : cx;
}

/* the byte a vertical aim lands on in the wrapped layout of a row,
   moved past the row's leading indent the same way */
static int WrappedAim(const vted_row_t *row, int line, int col, int cols,
                      int indent)
{
    return AimPastIndent(row, RowWrappedToCx(row, line, col, cols, indent));
}

static void ClampCursorToRow(void)
{
    /* after a vertical move the cursor lands on the character that
       covers the preferred display column in the new row, or at its
       end when the row is shorter. Aiming for the column and not the
       byte index keeps tabs and wide characters from making the
       cursor jump sideways */
    if (vted_editor_config.cursor_y >= vted_buffer.num_rows)
    {
        /* one line past the end of the text: only column 0 exists,
           pressing Enter there will create a new line */
        vted_editor_config.cursor_x = 0;
        return;
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
    int target = vted_editor_config.preferred_rx > 0 ? vted_editor_config.preferred_rx : 0;

    vted_editor_config.cursor_x = AimPastIndent(row, RowRxToCx(row, target));
}

/* moves the cursor one screen line through the wrapped layout: the
   lines a row flows over count as lines of their own. The cursor aims
   for the column it started from within the line, the way it aims for
   the display column across rows */
static void MoveDisplayLine(int direction)
{
    int cols = vted_editor_config.text_cols;
    int preferred = vted_editor_config.preferred_rx > 0
                        ? vted_editor_config.preferred_rx % cols
                        : 0;

    if (direction > 0)
    {
        if (vted_editor_config.cursor_y >= vted_buffer.num_rows)
        {
            return; /* the phantom row past the end: nothing below it */
        }

        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
        int cursor_line;
        int cursor_col;

        RowCxToWrapped(row, vted_editor_config.cursor_x, cols,
                       WrapIndentOf(row, cols),
                       &cursor_line, &cursor_col);

        if (cursor_line + 1 < RowLines(vted_editor_config.cursor_y))
        {
            /* the next line of this row */
            vted_editor_config.cursor_x =
                WrappedAim(row, cursor_line + 1, preferred, cols,
                           WrapIndentOf(row, cols));
            return;
        }

        /* past the row's last line: the next row's first */
        vted_editor_config.cursor_y++;

        if (vted_editor_config.cursor_y < vted_buffer.num_rows)
        {
            vted_editor_config.cursor_x =
                WrappedAim(&vted_buffer.rows[vted_editor_config.cursor_y],
                           0, preferred, cols,
                           WrapIndentOf(&vted_buffer.rows[vted_editor_config.cursor_y],
                                        cols));
        }
        else
        {
            vted_editor_config.cursor_x = 0; /* the phantom row */
        }
    }
    else
    {
        if (vted_editor_config.cursor_y < vted_buffer.num_rows)
        {
            vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
            int cursor_line;
            int cursor_col;

            RowCxToWrapped(row, vted_editor_config.cursor_x, cols,
                           WrapIndentOf(row, cols),
                           &cursor_line, &cursor_col);

            if (cursor_line > 0)
            {
                /* the line above within this row */
                vted_editor_config.cursor_x =
                    WrappedAim(row, cursor_line - 1, preferred, cols,
                               WrapIndentOf(row, cols));
                return;
            }
        }

        /* the row's first line: the last line of the row above */
        if (vted_editor_config.cursor_y > 0)
        {
            vted_editor_config.cursor_y--;
            vted_editor_config.cursor_x =
                WrappedAim(&vted_buffer.rows[vted_editor_config.cursor_y],
                           RowLines(vted_editor_config.cursor_y) - 1,
                           preferred, cols, 0);
        }
    }
}

/* moves the cursor count screen lines through the wrapped layout in
   one walk, the way MoveDisplayLine moves one: whole rows are
   crossed without a scan per line, and the cursor aims for the
   column it started from within its line. A count past either end
   stops at the first or last line there is */
static void MoveDisplayLines(int count)
{
    int cols = vted_editor_config.text_cols;
    int preferred = vted_editor_config.preferred_rx > 0
                        ? vted_editor_config.preferred_rx % cols
                        : 0;

    if (count > 0)
    {
        int remaining = count;

        if (vted_editor_config.cursor_y >= vted_buffer.num_rows)
        {
            return; /* the phantom row past the end: nothing below it */
        }

        /* the lines of this row below the cursor's own: the position
           past the row's last glyph sits at the row's bottom edge,
           one move away from the next row */
        {
            vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
            int line;
            int col;
            int lines = RowLines(vted_editor_config.cursor_y);
            int below;

            RowCxToWrapped(row, vted_editor_config.cursor_x, cols,
                           WrapIndentOf(row, cols), &line, &col);

            below = line < lines ? lines - 1 - line : 0;

            if (remaining <= below)
            {
                vted_editor_config.cursor_x =
                    WrappedAim(row, line + remaining, preferred, cols,
                               WrapIndentOf(row, cols));
                return;
            }

            remaining -= below + 1; /* the rest of this row, its last line counted */
        }

        /* the rows below, crossed whole until the count runs out
           inside one */
        vted_editor_config.cursor_y++;

        while (vted_editor_config.cursor_y < vted_buffer.num_rows)
        {
            vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
            int lines = RowLines(vted_editor_config.cursor_y);

            if (remaining < lines)
            {
                vted_editor_config.cursor_x =
                    WrappedAim(row, remaining, preferred, cols,
                               WrapIndentOf(row, cols));
                return;
            }

            remaining -= lines;
            vted_editor_config.cursor_y++;
        }

        /* past the last row: the phantom row past the end */
        vted_editor_config.cursor_x = 0;
    }
    else if (count < 0)
    {
        int remaining = -count;

        /* the phantom row past the end: one line, above it the last
           row's last line */
        if (vted_editor_config.cursor_y >= vted_buffer.num_rows)
        {
            if (vted_buffer.num_rows == 0)
            {
                return; /* nothing above an empty buffer */
            }

            vted_editor_config.cursor_y--;
            remaining--;
        }
        else
        {
            /* the lines of this row above the cursor's own: the
               position past the row's last glyph counts as a line of
               its own, the way the single-line move leaves it */
            vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
            int line;
            int col;

            RowCxToWrapped(row, vted_editor_config.cursor_x, cols,
                           WrapIndentOf(row, cols), &line, &col);

            if (remaining <= line)
            {
                vted_editor_config.cursor_x =
                    WrappedAim(row, line - remaining, preferred, cols,
                               WrapIndentOf(row, cols));
                return;
            }

            remaining -= line + 1; /* this row's lines above, its first line counted */
            vted_editor_config.cursor_y--;
        }

        /* the rows above, crossed whole until the count runs out
           inside one */
        while (vted_editor_config.cursor_y >= 0)
        {
            vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
            int lines = RowLines(vted_editor_config.cursor_y);

            if (remaining < lines)
            {
                vted_editor_config.cursor_x =
                    WrappedAim(row, lines - 1 - remaining, preferred, cols,
                               WrapIndentOf(row, cols));
                return;
            }

            remaining -= lines;
            vted_editor_config.cursor_y--;
        }

        /* past the first row: its first line, the column kept */
        vted_editor_config.cursor_y = 0;
        vted_editor_config.cursor_x =
            WrappedAim(&vted_buffer.rows[0], 0, preferred, cols,
                       WrapIndentOf(&vted_buffer.rows[0], cols));
    }
}

/* places the cursor on the top, the middle or the bottom screen line
   of the viewport: a wrapped layout is walked line by line, so a
   viewport that starts inside a tall row is counted from the line it
   starts on, and a jump past the end of the text lands on its last
   line instead of the phantom row past it */
static void JumpToViewportLine(int key)
{
    int target;

    if (key == ViewportTop)
    {
        target = 0;
    }
    else if (key == ViewportMiddle)
    {
        target = vted_editor_config.screen_rows / 2;
    }
    else
    {
        target = vted_editor_config.screen_rows - 1;
    }

    if (target < 0)
    {
        target = 0; /* a viewport without lines: the top is all there is */
    }

    /* an empty buffer has only the phantom row past the end */
    if (vted_buffer.num_rows == 0)
    {
        vted_editor_config.cursor_y = 0;
        vted_editor_config.cursor_x = 0;
        return;
    }

    if (vted_editor_config.wrap == 0)
    {
        /* one row per screen line: the row target rows below the top,
           or the last one when the viewport opens below the text */
        int row = vted_editor_config.row_offset + target;

        if (row >= vted_buffer.num_rows)
        {
            row = vted_buffer.num_rows - 1;
        }
        if (row < 0)
        {
            row = 0;
        }

        vted_editor_config.cursor_y = row;
        vted_editor_config.cursor_x = 0;
        return;
    }

    /* wrapped: walk the screen lines from the top of the viewport,
       the top row entered at the line the viewport starts on */
    {
        int row = vted_editor_config.row_offset;
        int line = vted_editor_config.line_offset;
        int remaining = target;

        if (row >= vted_buffer.num_rows)
        {
            row = vted_buffer.num_rows - 1; /* a stale offset past the end */
        }

        if (line >= RowLines(row))
        {
            line = 0; /* a stale offset: the row shrank under an edit */
        }

        while (1)
        {
            int visible = RowLines(row) - line; /* the row's lines from here down */

            if (remaining < visible)
            {
                /* the target line is inside this row: the start of
                   it, the byte at its first cell */
                vted_editor_config.cursor_y = row;
                vted_editor_config.cursor_x =
                    RowWrappedToCx(&vted_buffer.rows[row],
                                   line + remaining, 0,
                                   vted_editor_config.text_cols,
                                   WrapIndentOf(&vted_buffer.rows[row],
                                                vted_editor_config.text_cols));
                return;
            }

            remaining -= visible;
            row++;

            if (row >= vted_buffer.num_rows)
            {
                /* the viewport opens below the text: the last line
                   of the last row */
                vted_editor_config.cursor_y = row - 1;
                vted_editor_config.cursor_x =
                    RowWrappedToCx(&vted_buffer.rows[row - 1],
                                   RowLines(row - 1) - 1, 0,
                                   vted_editor_config.text_cols,
                                   WrapIndentOf(&vted_buffer.rows[row - 1],
                                                vted_editor_config.text_cols));
                return;
            }

            line = 0;
        }
    }
}

/* where Home puts the cursor: on the first non-blank glyph of the
   line — the screen line the cursor sits on when wrapping is on, the
   row itself when it is not — or, when the cursor already rests
   there, on the line's very start, so Home toggles between the two
   the way it does in the editors people know. A line that holds
   nothing but blanks has no first non-blank: Home just starts it */
static int HomeTarget(const vted_row_t *row, int cx, int cols, int indent)
{
    int start; /* the byte the line starts at */
    int end;   /* one past the line's last byte */
    int at;

    if (cols <= 0)
    {
        /* wrapping off: the row is one endless line */
        start = 0;
        end = row->size;
    }
    else
    {
        int line;
        int col;
        int lines = RowWrapCount(row, cols, indent);

        RowCxToWrapped(row, cx, cols, indent, &line, &col);

        if (line >= lines)
        {
            line = lines - 1; /* past the end rests on the last line */
        }

        start = RowWrappedToCx(row, line, 0, cols, indent);
        end = line + 1 < lines
                  ? RowWrappedToCx(row, line + 1, 0, cols, indent)
                  : row->size;
    }

    at = start;

    while (at < end && (row->chars[at] == ' ' || row->chars[at] == '\t'))
    {
        at++;
    }

    if (at < end && cx != at)
    {
        return at; /* the line's first non-blank */
    }

    return start; /* already there, or nothing but blanks: the line's start */
}

void MoveCursor(int key)
{
    /* moving the cursor logically, bounded by the text in the buffer.
       Shift variants move the cursor exactly like the plain key; the
       editor core treats them as selection extensions instead. */

    switch (key)
    {
    case ShiftArrowLeft:
        key = ArrowLeft;
        break;
    case ShiftArrowRight:
        key = ArrowRight;
        break;
    case ShiftArrowUp:
        key = ArrowUp;
        break;
    case ShiftArrowDown:
        key = ArrowDown;
        break;
    case ShiftHome:
        key = HomeKey;
        break;
    case ShiftEnd:
        key = EndKey;
        break;
    default:
        break;
    }

    vted_row_t *row = NULL;

    if (vted_editor_config.cursor_y < vted_buffer.num_rows)
    {
        row = &vted_buffer.rows[vted_editor_config.cursor_y];
    }

    /* the first vertical move of a run remembers the column it started
       from; the main loop clears it again on any other key. Short rows
       in between squeeze the cursor left, but it springs back to the
       remembered column on the next longer row. With wrapping on the
       column remembered is the one within the cursor's own screen
       line: the padding a moved run leaves would otherwise make the
       cell stream and the layout disagree about where that is */
    if ((key == ArrowUp || key == ArrowDown || key == PageUp || key == PageDown) &&
        vted_editor_config.preferred_rx < 0)
    {
        if (vted_editor_config.wrap != 0 && row != NULL)
        {
            int line;
            int col;

            RowCxToWrapped(row, vted_editor_config.cursor_x,
                           vted_editor_config.text_cols,
                           WrapIndentOf(row, vted_editor_config.text_cols),
                           &line, &col);

            vted_editor_config.preferred_rx = col;
        }
        else
        {
            vted_editor_config.preferred_rx = row != NULL ? RowCxToRx(row, vted_editor_config.cursor_x) : 0;
        }
    }

    switch (key)
    {
    case ArrowLeft:
        if (vted_editor_config.cursor_x > 0)
        {
            /* a whole character, never into the middle of a sequence */
            vted_editor_config.cursor_x = Utf8PrevBoundary(row->chars, vted_editor_config.cursor_x);
        }
        else if (vted_editor_config.cursor_y > 0)
        {
            /* wrap to the end of the previous line */
            vted_editor_config.cursor_y--;
            vted_editor_config.cursor_x = vted_buffer.rows[vted_editor_config.cursor_y].size;
        }
        break;

    case ArrowRight:
        if (row != NULL)
        {
            if (vted_editor_config.cursor_x < row->size)
            {
                vted_editor_config.cursor_x = Utf8NextBoundary(row->chars, row->size,
                                                               vted_editor_config.cursor_x);
            }
            else if (vted_editor_config.cursor_y < vted_buffer.num_rows - 1)
            {
                /* wrap to the start of the next line */
                vted_editor_config.cursor_y++;
                vted_editor_config.cursor_x = 0;
            }
        }
        break;

    case WordLeft:
        if (vted_editor_config.cursor_x > 0)
        {
            vted_editor_config.cursor_x = RowWordLeft(row, vted_editor_config.cursor_x);
        }
        else if (vted_editor_config.cursor_y > 0)
        {
            /* nothing left of the cursor in this row: wrap like the
               left arrow does, to the end of the previous line */
            vted_editor_config.cursor_y--;
            vted_editor_config.cursor_x = vted_buffer.rows[vted_editor_config.cursor_y].size;
        }
        break;

    case WordRight:
        if (row != NULL)
        {
            if (vted_editor_config.cursor_x < row->size)
            {
                vted_editor_config.cursor_x = RowWordRight(row, vted_editor_config.cursor_x);
            }
            else if (vted_editor_config.cursor_y < vted_buffer.num_rows - 1)
            {
                /* already at the end of the row: wrap to the start of
                   the next line */
                vted_editor_config.cursor_y++;
                vted_editor_config.cursor_x = 0;
            }
        }
        break;

    case ArrowUp:
        if (vted_editor_config.wrap != 0)
        {
            /* the lines a row wraps into are lines of their own */
            MoveDisplayLine(-1);
        }
        else if (vted_editor_config.cursor_y > 0)
        {
            vted_editor_config.cursor_y--;
            ClampCursorToRow();
        }
        break;

    case ArrowDown:
        if (vted_editor_config.wrap != 0)
        {
            MoveDisplayLine(1);
        }
        else if (vted_editor_config.cursor_y < vted_buffer.num_rows)
        {
            vted_editor_config.cursor_y++;
            ClampCursorToRow();
        }
        break;

    case PageUp:
        if (vted_editor_config.wrap != 0)
        {
            /* a viewport of screen lines towards the start, the
               lines a row wraps into counted as lines of their own */
            MoveDisplayLines(-vted_editor_config.screen_rows);
        }
        else
        {
            vted_editor_config.cursor_y -= vted_editor_config.screen_rows;
            if (vted_editor_config.cursor_y < 0)
            {
                vted_editor_config.cursor_y = 0;
            }
            ClampCursorToRow();
        }
        break;

    case PageDown:
        if (vted_editor_config.wrap != 0)
        {
            /* a viewport of screen lines towards the end, the phantom
               row past the last line included */
            MoveDisplayLines(vted_editor_config.screen_rows);
        }
        else
        {
            vted_editor_config.cursor_y += vted_editor_config.screen_rows;
            if (vted_editor_config.cursor_y > vted_buffer.num_rows)
            {
                vted_editor_config.cursor_y = vted_buffer.num_rows;
            }
            ClampCursorToRow();
        }
        break;

    case HomeKey:
        if (row == NULL)
        {
            vted_editor_config.cursor_x = 0; /* the phantom row keeps column zero */
        }
        else if (vted_editor_config.wrap != 0)
        {
            /* the first non-blank of the screen line the cursor is
               on, then its very start on a second press: the rows
               flow over lines of their own, so a line has a first
               non-blank of its own. The position past the row's last
               glyph rests on the last line */
            vted_editor_config.cursor_x =
                HomeTarget(row, vted_editor_config.cursor_x,
                           vted_editor_config.text_cols,
                           WrapIndentOf(row, vted_editor_config.text_cols));
        }
        else
        {
            /* without wrapping the row is the line: the toggle is
               between its first non-blank and column zero */
            vted_editor_config.cursor_x =
                HomeTarget(row, vted_editor_config.cursor_x, 0, 0);
        }
        break;

    case EndKey:
        if (row != NULL)
        {
            if (vted_editor_config.wrap == 0)
            {
                vted_editor_config.cursor_x = row->size;
            }
            else
            {
                /* the end of the screen line the cursor is on: one
                   past its last byte, which is the first byte of the
                   next one, the way the right arrow crosses a wrap
                   edge */
                int line;
                int col;
                int lines = RowWrapCount(row, vted_editor_config.text_cols,
                                         WrapIndentOf(row, vted_editor_config.text_cols));

                RowCxToWrapped(row, vted_editor_config.cursor_x,
                               vted_editor_config.text_cols,
                               WrapIndentOf(row, vted_editor_config.text_cols),
                               &line, &col);

                if (line >= lines)
                {
                    line = lines - 1; /* past the end rests on the last line */
                }

                if (line + 1 < lines)
                {
                    vted_editor_config.cursor_x =
                        RowWrappedToCx(row, line + 1, 0,
                                       vted_editor_config.text_cols,
                                       WrapIndentOf(row, vted_editor_config.text_cols));
                }
                else
                {
                    vted_editor_config.cursor_x = row->size;
                }
            }
        }
        break;

    case ViewportTop:
    case ViewportMiddle:
    case ViewportBottom:
        /* the jump lands on the start of its screen line, the way
           Home lands on the start of the one the cursor is on */
        JumpToViewportLine(key);
        break;
    }
}

/* is there a screen line below the viewport: the rows it shows, laid
   out from the line the top one is entered at, are followed by more
   lines. The phantom row past the end of the text counts as the last
   line there is, so the viewport stops scrolling once the last of the
   text and the phantom row sit on screen */
static int LayoutHasALineBelow(void)
{
    int row = vted_editor_config.row_offset;
    int line = vted_editor_config.line_offset;
    int left = vted_editor_config.screen_rows + 1; /* one line more than the viewport shows */

    if (row > vted_buffer.num_rows)
    {
        return 0; /* a stale offset past the phantom row */
    }

    while (left > 0 && row <= vted_buffer.num_rows)
    {
        int lines = row < vted_buffer.num_rows
                        ? RowLines(row) - (row == vted_editor_config.row_offset ? line : 0)
                        : 1; /* the phantom row past the end */

        if (lines >= left)
        {
            return 1; /* the row's lines alone reach past the bottom */
        }

        left -= lines;
        row++;
    }

    return 0; /* the layout ran out before the viewport did */
}

/* scrolls the viewport one screen line down (a positive direction) or
   up (a negative one) without moving the cursor through the text: the
   lines a row wraps into count as lines of their own. The cursor
   keeps its place and only comes along when the scroll would leave it
   off screen. Returns 1 when the viewport moved */
static int ScrollOneLine(int direction)
{
    if (direction > 0)
    {
        if (LayoutHasALineBelow() == 0)
        {
            return 0; /* the last line is on screen: nothing below it */
        }

        /* the top moves one screen line down: deeper into the top
           row, or past it into the next one */
        if (vted_editor_config.row_offset < vted_buffer.num_rows &&
            vted_editor_config.line_offset + 1 < RowLines(vted_editor_config.row_offset))
        {
            vted_editor_config.line_offset++;
        }
        else
        {
            vted_editor_config.row_offset++;
            vted_editor_config.line_offset = 0;
        }

        /* a cursor scrolled off the top comes one line along */
        if (CursorOnScreen() == 0)
        {
            if (vted_editor_config.wrap != 0)
            {
                MoveDisplayLines(1);
            }
            else
            {
                vted_editor_config.cursor_y++;
                ClampCursorToRow();
            }
        }
    }
    else
    {
        /* a stale offset past the phantom row counts as the phantom
           row itself: the line above it is the last row's last */
        if (vted_editor_config.row_offset > vted_buffer.num_rows)
        {
            vted_editor_config.row_offset = vted_buffer.num_rows;
        }

        if (vted_editor_config.row_offset == 0 && vted_editor_config.line_offset == 0)
        {
            return 0; /* the first line is already the top */
        }

        /* the top moves one screen line up: into the lines the top
           row starts with, or onto the last one of the row above */
        if (vted_editor_config.line_offset > 0)
        {
            vted_editor_config.line_offset--;
        }
        else
        {
            vted_editor_config.row_offset--;
            vted_editor_config.line_offset = RowLines(vted_editor_config.row_offset) - 1;
        }

        /* a cursor scrolled off the bottom comes one line along */
        if (CursorOnScreen() == 0)
        {
            if (vted_editor_config.wrap != 0)
            {
                MoveDisplayLines(-1);
            }
            else
            {
                vted_editor_config.cursor_y--;
                ClampCursorToRow();
            }
        }
    }

    return 1;
}

void ScreenScroll(int count)
{
    /* scrolls the viewport count screen lines down (a positive count)
       or up (a negative one) through the layout ScrollOneLine walks */
    while (count > 0 && ScrollOneLine(1))
    {
        count--;
    }

    while (count < 0 && ScrollOneLine(-1))
    {
        count++;
    }
}

int ScreenHitTest(int col, int row, int *out_y, int *out_x)
{
    /*=================================================
    | Maps a screen cell to the buffer position the  |
    | renderer draws under it: the walk the frame     |
    | takes over the rows, walked again here, so the  |
    | answer always agrees with what is on screen —   |
    | the gutter skipped at the left edge, the text   |
    | cells counted from the column the viewport is   |
    | scrolled to, and a wrapped row's lines counted  |
    | as lines of their own, entered mid-row by the   |
    | lines the viewport skipped of it.               |
    =================================================*/

    if (row < 0 || row >= vted_editor_config.screen_rows)
    {
        return 0; /* the bars below the viewport, or off screen */
    }

    /* a click on the gutter is a click on its row's first text cell,
       and a click past the right edge on the last one on screen */
    if (col < vted_editor_config.gutter_width)
    {
        col = vted_editor_config.gutter_width;
    }

    col -= vted_editor_config.gutter_width;

    if (col >= vted_editor_config.text_cols)
    {
        col = vted_editor_config.text_cols - 1;
    }

    if (col < 0)
    {
        col = 0;
    }

    /* an empty buffer has only the phantom row past the end */
    if (vted_buffer.num_rows == 0)
    {
        *out_y = 0;
        *out_x = 0;
        return 1;
    }

    {
        int buffer_row = vted_editor_config.row_offset;
        int screen_line = 0; /* lines the rows above this one took */

        while (buffer_row < vted_buffer.num_rows)
        {
            int lines = RowLines(buffer_row);
            /* the viewport is entered mid-row at its top, when that
               row is taller than the whole of it */
            int skipped = buffer_row == vted_editor_config.row_offset
                              ? vted_editor_config.line_offset
                              : 0;

            if (row < screen_line + lines - skipped)
            {
                /* the click landed inside this row, on the line the
                   row's own walk places at its screen line */
                vted_row_t *text_row = &vted_buffer.rows[buffer_row];
                int line = row - screen_line + skipped;

                if (vted_editor_config.wrap != 0)
                {
                    *out_x = RowWrappedToCx(text_row, line, col,
                                            vted_editor_config.text_cols,
                                            WrapIndentOf(text_row,
                                                         vted_editor_config.text_cols));
                }
                else
                {
                    *out_x = RowRxToCx(text_row,
                                       col + vted_editor_config.col_offset);
                }

                *out_y = buffer_row;
                return 1;
            }

            screen_line += lines - skipped;
            buffer_row++;
        }

        /* below the text: the phantom row past the end, the one the
           cursor takes its place on */
        *out_y = vted_buffer.num_rows;
        *out_x = 0;
        return 1;
    }
}

void ScreenFree(void)
{
    /* the highlight classes are kept between frames so that drawing
       does not allocate; they are given back when the editor leaves */
    free(highlight_classes);
    highlight_classes = NULL;
    highlight_capacity = 0;

    free(query_match_starts);
    query_match_starts = NULL;
    query_match_ends = NULL;
    query_match_capacity = 0;

    free(dirty_rows);
    dirty_rows = NULL;
    dirty_rows_capacity = 0;
    screen_active = 0;
    last_row_offset = -1;
    last_col_offset = -1;
    last_line_offset = -1;
    last_cursor_row = -1;
    last_gutter_width = -1;

    /* the menu only borrows its lines, so only the borrowing ends */
    menu_lines = NULL;
    menu_count = 0;
    menu_selected = 0;

    /* and so does the search query */
    search_query = NULL;
    search_query_len = 0;
    search_is_regex = 0;
}

void RefreshScreen(void)
{
    append_buffer_t ab = ABUF_INIT;
    char cursor_position_buffer[32];
    int cursor_position_len;
    /* the screen line the terminal cursor sits on while the frame is
       assembled; it starts homed to the top-left */
    int at = 0;

    int size_change = RefreshWindowSize();

    if (size_change == -1)
    {
        KillApp("GetWindowSize");
    }

    if (size_change == 1)
    {
        /* the terminal was resized: nothing on screen lines up anymore */
        ScreenMarkAllDirty();
    }

    /* the gutter depends on the buffer as well: it widens when the
       line count crosses a digit boundary and collapses when the
       numbers are hidden, so its width is recomputed every frame,
       before anything lays text out in the columns it leaves */
    UpdateGutterWidth(vted_editor_config.screen_cols);

    if (vted_editor_config.gutter_width != last_gutter_width)
    {
        last_gutter_width = vted_editor_config.gutter_width;
        /* every row's number and text shifted left or right */
        ScreenMarkAllDirty();
    }

    UpdateScroll();

    /* scrolling moved the viewport over the buffer: what each screen
       line shows changed even though no buffer row was edited, so
       everything on screen repaints. With wrapping on that includes
       the viewport starting on a different line of its top row */
    if (vted_editor_config.row_offset != last_row_offset ||
        vted_editor_config.col_offset != last_col_offset ||
        vted_editor_config.line_offset != last_line_offset)
    {
        last_row_offset = vted_editor_config.row_offset;
        last_col_offset = vted_editor_config.col_offset;
        last_line_offset = vted_editor_config.line_offset;
        ScreenMarkAllDirty();
    }

    if (vted_editor_config.cursor_y != last_cursor_row)
    {
        /* the bold line number follows the cursor between rows: the
           row it leaves and the row it enters repaint, even though
           their text did not change */
        ScreenMarkRowDirty(last_cursor_row);
        ScreenMarkRowDirty(vted_editor_config.cursor_y);
        last_cursor_row = vted_editor_config.cursor_y;
    }

    /* hide the cursor while the frame is repainted, otherwise it
       visibly jumps around between the old and the new position */
    AppendToBuffer(&ab, "\x1b[?25l", 6);

    /* home the cursor to the top-left, every row repaints from there */
    AppendToBuffer(&ab, "\x1b[H", 3);

    DrawRows(&ab, &at);

    DrawMenu(&ab, &at);

    DrawStatusBar(&ab, &at);

    DrawMessageBar(&ab);

    /* display the cursor position on terminal screen */

    /* offsets translate the absolute buffer coordinates into viewport
       coordinates; + 1 because the terminal starts from 1,1 not 0,0.
       A wrapped row sits on as many screen lines as it takes, so the
       cursor's line counts the wrapped lines of the rows above it */
    {
        int cursor_screen_row;
        int cursor_screen_col;

        if (vted_editor_config.wrap != 0)
        {
            int cursor_line = 0;
            int cursor_col = 0;
            int above = 0;
            int row;

            for (row = vted_editor_config.row_offset;
                 row < vted_editor_config.cursor_y && row < vted_buffer.num_rows;
                 row++)
            {
                above += RowLines(row);

                if (row == vted_editor_config.row_offset)
                {
                    /* the top row enters past the lines it skipped */
                    above -= vted_editor_config.line_offset;
                }
            }

            if (vted_editor_config.cursor_y < vted_buffer.num_rows)
            {
                RowCxToWrapped(&vted_buffer.rows[vted_editor_config.cursor_y],
                               vted_editor_config.cursor_x,
                               vted_editor_config.text_cols,
                               WrapIndentOf(&vted_buffer.rows[vted_editor_config.cursor_y],
                                            vted_editor_config.text_cols),
                               &cursor_line, &cursor_col);

                /* the position past the last glyph sits on a line of
                   its own when the text ends exactly at a wrap edge:
                   the cell after the line's last one is not on screen,
                   so the cursor parks on that last cell instead of
                   dropping onto the next row's first line */
                if (cursor_line >= RowLines(vted_editor_config.cursor_y))
                {
                    cursor_line = RowLines(vted_editor_config.cursor_y) - 1;
                    cursor_col = vted_editor_config.text_cols - 1;
                }
            }

            cursor_screen_row = above + cursor_line;

            /* the cursor's own row starts at its skipped lines when
               it is the top one */
            if (vted_editor_config.cursor_y == vted_editor_config.row_offset)
            {
                cursor_screen_row -= vted_editor_config.line_offset;
            }

            /* a row with more lines than the whole viewport cannot
               show them all: the cursor waits at the bottom edge */
            if (cursor_screen_row > vted_editor_config.screen_rows - 1)
            {
                cursor_screen_row = vted_editor_config.screen_rows - 1;
            }

            cursor_screen_col = cursor_col;
        }
        else
        {
            cursor_screen_row = vted_editor_config.cursor_y - vted_editor_config.row_offset;
            cursor_screen_col = vted_editor_config.cursor_rx - vted_editor_config.col_offset;
        }

        cursor_position_len = snprintf(cursor_position_buffer, sizeof(cursor_position_buffer),
                                       "\x1b[%d;%dH", /* ESC + [ + <row> + ; + <col> */
                                       cursor_screen_row + 1,
                                       cursor_screen_col + vted_editor_config.gutter_width + 1);
    }

    if (cursor_position_len > 0)
    {
        AppendToBuffer(&ab, cursor_position_buffer, cursor_position_len);
    }

    AppendToBuffer(&ab, "\x1b[?25h", 6); /* show the cursor again */

    /* the whole frame goes out with one write() to avoid flicker */
    if (write(STDOUT_FILENO, ab.data, (size_t)ab.len) == -1)
    {
        KillApp("write");
    }

    FreeAppendBuffer(&ab);
}
void InitVtedEditor(void)
{
    /* Initialize the cursor position and the viewport offsets */
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;
    vted_editor_config.cursor_rx = 0;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.col_offset = 0;
    vted_editor_config.line_offset = 0;

    /* line numbers are on until the person turns them off */
    vted_editor_config.show_line_numbers = 1;
    vted_editor_config.gutter_width = 0;

    /* the tab stop and whether Tab inserts spaces instead of a tab,
       whether the tabs are drawn as dotted runs, whether the indent
       guides dot the nesting levels, whether long lines
       wrap over several screen lines, and the column the marker sits
       on, 0 for none */
    vted_editor_config.tab_width = VTED_TAB_STOP;
    vted_editor_config.insert_spaces = 0;
    vted_editor_config.show_tabs = 0;
    vted_editor_config.wrap = 0;
    vted_editor_config.wrap_column = 0;
    vted_editor_config.wrap_indent = 1;
    vted_editor_config.indent_guides = 0;
    RowSetTabWidth(vted_editor_config.tab_width);

    /* no search match highlighted yet */
    vted_editor_config.match_row = -1;
    vted_editor_config.match_col = 0;
    vted_editor_config.match_len = 0;

    /* and no query for the search highlight either */
    search_query = NULL;
    search_query_len = 0;
    search_is_regex = 0;

    /* and no matching bracket until the cursor lands on one */
    bracket_row = -1;
    bracket_col = 0;

    if (RefreshWindowSize() == -1)
    {
        KillApp("GetWindowSize");
    }

    /* nothing has been painted yet: the first frame repaints everything */
    screen_active = 1;
    ScreenMarkAllDirty();

    SetStatusMessage("HELP: Ctrl-S save | Ctrl-F find | Ctrl-G go to line | Ctrl-Q quit");

    /* the first RefreshScreen() call from the main loop paints the frame */
}
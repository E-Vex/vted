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
#include "vted_syntax.h"
#include "vted_utf8.h"
/*===============================*/

vted_editor_Config_t vted_editor_config;

#define VTED_VERSION "0.2.0"

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

static void UpdateScroll(void)
{
    /* keep the cursor inside the viewport by moving the offsets;
       the cursor itself keeps absolute buffer coordinates. Rows are
       scrolled in display cells, so the byte index of the cursor is
       first converted to the cell column it is drawn at */

    vted_editor_config.cursor_rx = 0;

    if (vted_editor_config.cursor_y < vted_buffer.num_rows)
    {
        vted_editor_config.cursor_rx = RowCxToRx(&vted_buffer.rows[vted_editor_config.cursor_y],
                                                 vted_editor_config.cursor_x);
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

/* the Unicode replacement character, drawn for bytes that are not
   valid UTF-8 so they can never reach the terminal as raw codes */
#define REPLACEMENT_CHARACTER "\xEF\xBF\xBD"

static void AppendGlyph(append_buffer_t *ab, const vted_row_t *row, int at, int next,
                        int cell, int width, int left, int right)
{
    /* draws the character at byte index at, which occupies the cells
       [cell, cell + width), clipped to the viewport cells [left, right) */
    unsigned char byte = (unsigned char)row->chars[at];
    int code_point;
    int offset;

    if (byte == '\t')
    {
        /* a tab is blank space up to the next tab stop */
        for (offset = 0; offset < width; offset++)
        {
            if (cell + offset >= left && cell + offset < right)
            {
                AppendToBuffer(ab, " ", 1);
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

static void AppendStyle(append_buffer_t *ab, int hl_class, int inverted)
{
    /* every change starts from plain text, so no attribute of the
       previous style can linger */
    const char *color = SyntaxStyle(hl_class);

    AppendToBuffer(ab, "\x1b[m", 3);

    if (inverted)
    {
        AppendToBuffer(ab, "\x1b[7m", 4);
    }

    AppendToBuffer(ab, color, (int)strlen(color));
}

/* the selection the editor core hands over after every key press; the
   renderer draws it inverted */
static int sel_active;
static int sel_y0;
static int sel_x0;
static int sel_y1;
static int sel_x1;

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

static void DrawRowText(append_buffer_t *ab, int buffer_row, const vted_row_t *row, int has_match)
{
    /* one pass over the row: every character is laid out in terminal
       cells, then clipped to the viewport [col_offset, col_offset +
       text_cols). Syntax colors come from the highlight classes;
       control characters and the active search match are drawn in
       inverted video */
    const unsigned char *classes = ClassesOfRow(buffer_row, row->size);
    int left = vted_editor_config.col_offset;
    int right = left + vted_editor_config.text_cols;
    int match_end = vted_editor_config.match_col + vted_editor_config.match_len;
    int cell = 0;
    int at = 0;
    int inverted = 0;
    int style = HlNormal;
    int sel_start = -1;
    int sel_end = -1;

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
        unsigned char byte = (unsigned char)row->chars[at];
        int next = Utf8NextBoundary(row->chars, row->size, at);
        int width = RowCharWidth(row, at, cell);

        int is_control = (byte < 0x20 && byte != '\t') || byte == 0x7F;
        int in_match = has_match && at >= vted_editor_config.match_col && at < match_end;
        int want_inverted = is_control || in_match ||
                            (sel_start >= 0 && at >= sel_start && at < sel_end);

        if (want_inverted != inverted || classes[at] != style)
        {
            style = classes[at];
            inverted = want_inverted;
            AppendStyle(ab, style, inverted);
        }

        AppendGlyph(ab, row, at, next, cell, width, left, right);

        cell += width;
        at = next;
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
    int screen_row;

    for (screen_row = 0; screen_row < vted_editor_config.screen_rows; screen_row++)
    {
        int buffer_row = screen_row + vted_editor_config.row_offset;
        int draw;

        if (buffer_row >= vted_buffer.num_rows)
        {
            /* tildes below the text and the welcome banner are drawn
               every frame: they depend on nothing in the buffer */
            draw = 1;
        }
        else
        {
            EnsureDirtyRows(vted_buffer.num_rows);
            draw = dirty_rows[buffer_row] != 0;
        }

        if (!draw)
        {
            continue;
        }

        /* clean rows skipped above leave the terminal cursor wherever
           the last drawn row ended: this row is placed explicitly, and
           the common case of consecutive rows costs nothing */
        GotoScreenRow(ab, at, screen_row);

        if (buffer_row < vted_buffer.num_rows)
        {
            vted_row_t *text_row = &vted_buffer.rows[buffer_row];

            DrawGutter(ab, buffer_row);
            DrawRowText(ab, buffer_row, text_row,
                        buffer_row == vted_editor_config.match_row &&
                            vted_editor_config.match_len > 0);

            dirty_rows[buffer_row] = 0; /* the screen now matches the buffer */
        }
        else if (buffer_row == vted_buffer.num_rows && vted_editor_config.cursor_y == buffer_row &&
                 vted_editor_config.gutter_width > 0)
        {
            /* the line the cursor stands on past the end of the text:
               it gets its number too, so the cursor never sits in an
               empty gutter */
            DrawGutter(ab, buffer_row);
        }
        else
        {
            /* empty buffer: show a welcome banner in the middle row;
               a loaded file only gets the tildes below its text */
            if (vted_buffer.num_rows == 0 && screen_row == vted_editor_config.screen_rows / 2)
            {
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
        }

        AppendToBuffer(ab, "\x1b[K", 3); /* erase from cursor to end of line */

        AppendToBuffer(ab, "\r\n", 2);
        (*at)++;
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
       character it matches what the screen shows */
    char right_status[64];
    int right_len = 0;

    if (vted_buffer.num_rows > 0)
    {
        right_len = snprintf(right_status, sizeof(right_status), "Ln %d/%d, Col %d",
                             vted_editor_config.cursor_y + 1, vted_buffer.num_rows,
                             vted_editor_config.cursor_rx + 1);

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
void EnterToAlternateScreenBuffer(void)
{
    write(STDOUT_FILENO, "\x1b[?1049h", 8);
}
void ExitAlternateScreenBuffer(void)
{
    write(STDOUT_FILENO, "\x1b[?1049l", 8);
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

    vted_editor_config.cursor_x = RowRxToCx(row, target);
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
       remembered column on the next longer row */
    if ((key == ArrowUp || key == ArrowDown || key == PageUp || key == PageDown) &&
        vted_editor_config.preferred_rx < 0)
    {
        vted_editor_config.preferred_rx = row != NULL ? RowCxToRx(row, vted_editor_config.cursor_x) : 0;
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
        if (vted_editor_config.cursor_y > 0)
        {
            vted_editor_config.cursor_y--;
            ClampCursorToRow();
        }
        break;

    case ArrowDown:
        if (vted_editor_config.cursor_y < vted_buffer.num_rows)
        {
            vted_editor_config.cursor_y++;
            ClampCursorToRow();
        }
        break;

    case PageUp:
        /* jump one viewport of lines towards the start */
        vted_editor_config.cursor_y -= vted_editor_config.screen_rows;
        if (vted_editor_config.cursor_y < 0)
        {
            vted_editor_config.cursor_y = 0;
        }
        ClampCursorToRow();
        break;

    case PageDown:
        /* jump one viewport of lines towards the end, the phantom
           row past the last line included */
        vted_editor_config.cursor_y += vted_editor_config.screen_rows;
        if (vted_editor_config.cursor_y > vted_buffer.num_rows)
        {
            vted_editor_config.cursor_y = vted_buffer.num_rows;
        }
        ClampCursorToRow();
        break;

    case HomeKey:
        vted_editor_config.cursor_x = 0;
        break;

    case EndKey:
        if (row != NULL)
        {
            vted_editor_config.cursor_x = row->size;
        }
        break;
    }
}

void ScreenFree(void)
{
    /* the highlight classes are kept between frames so that drawing
       does not allocate; they are given back when the editor leaves */
    free(highlight_classes);
    highlight_classes = NULL;
    highlight_capacity = 0;

    free(dirty_rows);
    dirty_rows = NULL;
    dirty_rows_capacity = 0;
    screen_active = 0;
    last_row_offset = -1;
    last_col_offset = -1;
    last_cursor_row = -1;
    last_gutter_width = -1;
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
       everything on screen repaints */
    if (vted_editor_config.row_offset != last_row_offset ||
        vted_editor_config.col_offset != last_col_offset)
    {
        last_row_offset = vted_editor_config.row_offset;
        last_col_offset = vted_editor_config.col_offset;
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

    DrawStatusBar(&ab, &at);

    DrawMessageBar(&ab);

    /* display the cursor position on terminal screen */

    /* offsets translate the absolute buffer coordinates into viewport
       coordinates; + 1 because the terminal starts from 1,1 not 0,0 */
    cursor_position_len = snprintf(cursor_position_buffer, sizeof(cursor_position_buffer), "\x1b[%d;%dH", /* ESC + [ + <row> + ; + <col> */
                                   vted_editor_config.cursor_y - vted_editor_config.row_offset + 1,
                                   vted_editor_config.cursor_rx - vted_editor_config.col_offset +
                                       vted_editor_config.gutter_width + 1);

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

    /* line numbers are on until the person turns them off */
    vted_editor_config.show_line_numbers = 1;
    vted_editor_config.gutter_width = 0;

    /* the tab stop and whether Tab inserts spaces instead of a tab */
    vted_editor_config.tab_width = VTED_TAB_STOP;
    vted_editor_config.insert_spaces = 0;
    RowSetTabWidth(vted_editor_config.tab_width);

    /* no search match highlighted yet */
    vted_editor_config.match_row = -1;
    vted_editor_config.match_col = 0;
    vted_editor_config.match_len = 0;

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
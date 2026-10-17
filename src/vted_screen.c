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
int RefreshWindowSize(void)
{
    int rows;
    int cols;

    if (GetWindowSize(&rows, &cols) == -1)
    {
        return -1;
    }

    /* the two bottom rows are reserved for the status bar and
       the message bar, so the text viewport is two rows shorter */
    vted_editor_config.screen_rows = rows - 2;
    vted_editor_config.screen_cols = cols;

    return 0;
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

    if (vted_editor_config.cursor_rx >= vted_editor_config.col_offset + vted_editor_config.screen_cols)
    {
        vted_editor_config.col_offset = vted_editor_config.cursor_rx - vted_editor_config.screen_cols + 1;
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

static void DrawRowText(append_buffer_t *ab, const vted_row_t *row, int has_match)
{
    /* one pass over the row: every character is laid out in terminal
       cells, then clipped to the viewport [col_offset, col_offset +
       screen_cols). Control characters and the active search match
       are drawn in inverted video */
    int left = vted_editor_config.col_offset;
    int right = left + vted_editor_config.screen_cols;
    int match_end = vted_editor_config.match_col + vted_editor_config.match_len;
    int cell = 0;
    int at = 0;
    int inverted = 0;

    while (at < row->size && cell < right)
    {
        unsigned char byte = (unsigned char)row->chars[at];
        int next = Utf8NextBoundary(row->chars, row->size, at);
        int width = RowCharWidth(row, at, cell);

        int is_control = (byte < 0x20 && byte != '\t') || byte == 0x7F;
        int in_match = has_match && at >= vted_editor_config.match_col && at < match_end;
        int want_inverted = is_control || in_match;

        if (want_inverted != inverted)
        {
            AppendToBuffer(ab, want_inverted ? "\x1b[7m" : "\x1b[m", want_inverted ? 4 : 3);
            inverted = want_inverted;
        }

        AppendGlyph(ab, row, at, next, cell, width, left, right);

        cell += width;
        at = next;
    }

    if (inverted)
    {
        AppendToBuffer(ab, "\x1b[m", 3);
    }
}

static void DrawRows(append_buffer_t *ab)
{
    int row;

    for (row = 0; row < vted_editor_config.screen_rows; row++)
    {
        int buffer_row = row + vted_editor_config.row_offset;

        if (buffer_row < vted_buffer.num_rows)
        {
            vted_row_t *text_row = &vted_buffer.rows[buffer_row];

            DrawRowText(ab, text_row,
                        buffer_row == vted_editor_config.match_row &&
                            vted_editor_config.match_len > 0);
        }
        else
        {
            /* empty buffer: show a welcome banner in the middle row;
               a loaded file only gets the tildes below its text */
            if (vted_buffer.num_rows == 0 && row == vted_editor_config.screen_rows / 2)
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
    }
}

static void DrawStatusBar(append_buffer_t *ab)
{
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
    /* moving the cursor logically, bounded by the text in the buffer */

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

void RefreshScreen(void)
{
    append_buffer_t ab = ABUF_INIT;
    char cursor_position_buffer[32];
    int cursor_position_len;

    if (RefreshWindowSize() == -1)
    {
        KillApp("GetWindowSize");
    }

    UpdateScroll();

    /* hide the cursor while the frame is repainted, otherwise it
       visibly jumps around between the old and the new position */
    AppendToBuffer(&ab, "\x1b[?25l", 6);

    /* home the cursor to the top-left, every row repaints from there */
    AppendToBuffer(&ab, "\x1b[H", 3);

    DrawRows(&ab);

    DrawStatusBar(&ab);

    DrawMessageBar(&ab);

    /* display the cursor position on terminal screen */

    /* offsets translate the absolute buffer coordinates into viewport
       coordinates; + 1 because the terminal starts from 1,1 not 0,0 */
    cursor_position_len = snprintf(cursor_position_buffer, sizeof(cursor_position_buffer), "\x1b[%d;%dH", /* ESC + [ + <row> + ; + <col> */
                                   vted_editor_config.cursor_y - vted_editor_config.row_offset + 1,
                                   vted_editor_config.cursor_rx - vted_editor_config.col_offset + 1);

    if (cursor_position_len > 0)
    {
        AppendToBuffer(&ab, cursor_position_buffer, cursor_position_len);
    }

    AppendToBuffer(&ab, "\x1b[?25h", 6); /* show the cursor again */

    /* the whole frame goes out with one write() to avoid flicker */
    write(STDOUT_FILENO, ab.data, (size_t)ab.len);

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

    /* no search match highlighted yet */
    vted_editor_config.match_row = -1;
    vted_editor_config.match_col = 0;
    vted_editor_config.match_len = 0;

    if (RefreshWindowSize() == -1)
    {
        KillApp("GetWindowSize");
    }

    SetStatusMessage("HELP: Ctrl-S save | Ctrl-F find | Ctrl-G go to line | Ctrl-Q quit");

    /* the first RefreshScreen() call from the main loop paints the frame */
}
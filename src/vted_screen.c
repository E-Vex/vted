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
/*===============================*/

vted_editor_Config_t vted_editor_config;

#define VTED_VERSION "0.1"

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

static void DrawRows(append_buffer_t *ab)
{
    int row;

    for (row = 0; row < vted_editor_config.screen_rows; row++)
    {
        if (row < vted_buffer.num_rows)
        {
            /* render the text row, clipped to the viewport width so
               long lines wrap-free; full scrolling comes later */
            vted_row_t *text_row = &vted_buffer.rows[row];

            int len = text_row->size;

            if (len > vted_editor_config.screen_cols)
            {
                len = vted_editor_config.screen_cols;
            }

            if (len > 0)
            {
                AppendToBuffer(ab, text_row->chars, len);
            }
        }
        else
        {
            /* empty buffer: show a welcome banner in the middle row */
            if (row == vted_editor_config.screen_rows / 2)
            {
                char welcome[80];
                int welcome_len = snprintf(welcome, sizeof(welcome),
                                           "vted -- version %s", VTED_VERSION);
                int padding = (vted_editor_config.screen_cols - welcome_len) / 2;

                if (welcome_len < 0)
                {
                    welcome_len = 0;
                }
                if (welcome_len > (int)sizeof(welcome))
                {
                    welcome_len = (int)sizeof(welcome);
                }

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

    /* right side: current line out of the total, when text exists */
    if (vted_buffer.num_rows > 0)
    {
        char right_status[32];
        int right_len = snprintf(right_status, sizeof(right_status), "%d/%d",
                                 vted_editor_config.cursor_y + 1, vted_buffer.num_rows);

        if (right_len < 0)
        {
            right_len = 0;
        }

        int padding = vted_editor_config.screen_cols - status_len - right_len;

        while (padding-- > 0)
        {
            AppendToBuffer(ab, " ", 1);
        }

        AppendToBuffer(ab, right_status, right_len);
    }
    else
    {
        /* pad the inverted bar to the full width */
        int padding = vted_editor_config.screen_cols - status_len;

        while (padding-- > 0)
        {
            AppendToBuffer(ab, " ", 1);
        }
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

void MoveCursor(int key)
{
    /* moving the cursor logically */
    switch (key)
    {
    case ArrowLeft:
        if (vted_editor_config.cursor_x > 0)
            vted_editor_config.cursor_x--;
        break;
    case ArrowRight:
        if (vted_editor_config.cursor_x < vted_editor_config.screen_cols - 1)
            vted_editor_config.cursor_x++;
        break;
    case ArrowUp:
        if (vted_editor_config.cursor_y > 0)
            vted_editor_config.cursor_y--;
        break;
    case ArrowDown:
        if (vted_editor_config.cursor_y < vted_editor_config.screen_rows - 1)
            vted_editor_config.cursor_y++;
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

    /* hide the cursor while the frame is repainted, otherwise it
       visibly jumps around between the old and the new position */
    AppendToBuffer(&ab, "\x1b[?25l", 6);

    /* home the cursor to the top-left, every row repaints from there */
    AppendToBuffer(&ab, "\x1b[H", 3);

    DrawRows(&ab);

    DrawStatusBar(&ab);

    DrawMessageBar(&ab);

    /* display the cursor position on terminal screen */

    /* cursor_x/y + 1 here because the terminal starts from 1,1 not 0,0 */
    cursor_position_len = snprintf(cursor_position_buffer, sizeof(cursor_position_buffer), "\x1b[%d;%dH", /* ESC + [ + <row> + ; + <col> */
                                   vted_editor_config.cursor_y + 1, vted_editor_config.cursor_x + 1);

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
    /* Initialize the cursor position */
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;

    if (RefreshWindowSize() == -1)
    {
        KillApp("GetWindowSize");
    }

    SetStatusMessage("HELP: Ctrl-Q = quit");

    /* the first RefreshScreen() call from the main loop paints the frame */
}
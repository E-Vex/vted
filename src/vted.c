/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_file.h"
#include "vted_utf8.h"
/*===============================*/

static void EditorInsertChar(int c)
{
    /* typing on the phantom row past the end starts a new line */
    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        if (AppendRow("", 0) == -1)
        {
            KillApp("AppendRow");
        }
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

    if (RowInsertChar(row, vted_editor_config.cursor_x, c) == -1)
    {
        KillApp("RowInsertChar");
    }

    vted_buffer.dirty = 1;
    vted_editor_config.cursor_x++;
}

static void EditorInsertNewline(void)
{
    int indent = 0;

    if (vted_editor_config.cursor_x == 0)
    {
        /* new empty line at the cursor */
        if (InsertRow(vted_editor_config.cursor_y, "", 0) == -1)
        {
            KillApp("InsertRow");
        }
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        /* the new line starts with the blanks the current line starts
           with, but only those left of the cursor: splitting inside
           the indent must not copy more than what stays behind */
        indent = RowIndentLength(row, vted_editor_config.cursor_x);

        int tail = row->size - vted_editor_config.cursor_x;
        char *line = malloc((size_t)(indent + tail) + 1);

        if (line == NULL)
        {
            KillApp("malloc");
        }

        memcpy(line, row->chars, (size_t)indent);
        memcpy(line + indent, &row->chars[vted_editor_config.cursor_x], (size_t)tail);

        /* split: the tail of the row becomes the start of a new row */
        if (InsertRow(vted_editor_config.cursor_y + 1, line, indent + tail) == -1)
        {
            KillApp("InsertRow");
        }

        free(line);

        /* the original row keeps the head; InsertRow may have moved
           the rows array, so take a fresh pointer before shrinking */
        row = &vted_buffer.rows[vted_editor_config.cursor_y];
        row->size = vted_editor_config.cursor_x;
        row->chars[row->size] = '\0';
    }

    vted_buffer.dirty = 1;
    vted_editor_config.cursor_y++;
    vted_editor_config.cursor_x = indent;
}

static void EditorDelChar(void)
{
    /* deletes the character left of the cursor, or merges the
       current line into the one above when at column zero */

    if (vted_editor_config.cursor_y == 0 && vted_editor_config.cursor_x == 0)
    {
        return; /* nothing before the start of the file */
    }

    if (vted_editor_config.cursor_y == vted_buffer.num_rows)
    {
        return; /* nothing exists past the end of the file */
    }

    if (vted_editor_config.cursor_x == 0)
    {
        /* start of a line: merge it into the line above */
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
        vted_row_t *prev = &vted_buffer.rows[vted_editor_config.cursor_y - 1];

        int prev_size = prev->size; /* cursor will land here after the merge */

        if (RowAppendString(prev, row->chars, row->size) == -1)
        {
            KillApp("RowAppendString");
        }

        RemoveRow(vted_editor_config.cursor_y);

        vted_editor_config.cursor_y--;
        vted_editor_config.cursor_x = prev_size;
    }
    else
    {
        vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

        /* a character can span several bytes: remove all of them so a
           multibyte sequence is never left half deleted */
        int start = Utf8PrevBoundary(row->chars, vted_editor_config.cursor_x);
        int count = vted_editor_config.cursor_x - start;

        while (count-- > 0)
        {
            if (RowRemoveChar(row, start) == -1)
            {
                KillApp("RowRemoveChar");
            }
        }

        vted_editor_config.cursor_x = start;
    }

    vted_buffer.dirty = 1;
}

/* longest answer a prompt accepts, in bytes (a path fits comfortably) */
#define PROMPT_MAX 1024

static char *EditorPrompt(const char *label)
{
    /* asks for one line of text in the message bar. Returns it in a
       heap buffer the caller frees, or NULL when the person cancels
       with ESC or confirms an empty answer */
    char *answer = malloc(PROMPT_MAX + 1);
    int length = 0;

    if (answer == NULL)
    {
        KillApp("malloc");
    }

    answer[0] = '\0';

    while (1)
    {
        SetStatusMessage("%s%s", label, answer);
        RefreshScreen();

        int c = ReadKeyPress();

        if (c == 0)
        {
            continue; /* idle read timeout: just repaint the prompt */
        }

        if (c == 27) /* ESC cancels */
        {
            SetStatusMessage("");
            free(answer);
            return NULL;
        }

        if (c == '\r' || c == '\n')
        {
            SetStatusMessage("");

            if (length == 0)
            {
                free(answer);
                return NULL;
            }

            /* the answer may outlive the prompt (a file name does):
               give back the unused part of the buffer */
            char *exact = realloc(answer, (size_t)length + 1);

            return exact != NULL ? exact : answer;
        }

        if (c == 127) /* Backspace removes one whole character */
        {
            length = Utf8PrevBoundary(answer, length);
            answer[length] = '\0';
        }
        else if (((c >= 32 && c <= 126) || (c >= 128 && c <= 255)) && length < PROMPT_MAX)
        {
            answer[length++] = (char)c;
            answer[length] = '\0';
        }
    }
}

static void EditorGoToLine(void)
{
    char *answer = EditorPrompt("Go to line: ");

    if (answer == NULL)
    {
        return;
    }

    char *end;
    long line = strtol(answer, &end, 10);

    if (*end != '\0' || end == answer || line < 1)
    {
        SetStatusMessage("Not a line number: %s", answer);
        free(answer);
        return;
    }

    free(answer);

    if (vted_buffer.num_rows == 0)
    {
        return;
    }

    /* a number past the end lands on the last line */
    if (line > vted_buffer.num_rows)
    {
        line = vted_buffer.num_rows;
    }

    vted_editor_config.cursor_y = (int)line - 1;
    vted_editor_config.cursor_x = 0;

    /* a line that is already on screen stays where it is; one that is
       not gets the middle of the viewport, so there is context around
       it instead of the line sitting on the very edge */
    int first = vted_editor_config.row_offset;
    int last = first + vted_editor_config.screen_rows;

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

static void EditorSave(void)
{
    int named_here = 0;

    if (vted_buffer.filename == NULL)
    {
        /* an unnamed buffer asks where to go; the buffer takes the
           name only when the file could actually be written */
        char *name = EditorPrompt("Save as: ");

        if (name == NULL)
        {
            SetStatusMessage("Save cancelled");
            return;
        }

        vted_buffer.filename = name;
        named_here = 1;
    }

    if (SaveFile() == -1)
    {
        int saved_errno = errno;

        if (named_here)
        {
            free(vted_buffer.filename);
            vted_buffer.filename = NULL;
        }

        SetStatusMessage("Save failed: %s", strerror(saved_errno));
        return;
    }

    vted_buffer.dirty = 0;
    SetStatusMessage("Saved %s", vted_buffer.filename);
}

/* pressing Ctrl-Q this many times in a row overrides unsaved changes */
#define QUIT_CONFIRMATIONS 3

static int FindMatch(const char *query, int start_row, int direction,
                     int *match_row, int *match_col)
{
    /* scans every row exactly once, starting from start_row and
       wrapping around in the given direction */
    int row_count = vted_buffer.num_rows;
    int counted;

    if (row_count == 0 || query[0] == '\0')
    {
        return -1;
    }

    int current = start_row;

    for (counted = 0; counted < row_count; counted++)
    {
        if (current < 0)
        {
            current = row_count - 1;
        }
        if (current >= row_count)
        {
            current = 0;
        }

        const char *hit = strstr(vted_buffer.rows[current].chars, query);

        if (hit != NULL)
        {
            *match_row = current;
            *match_col = (int)(hit - vted_buffer.rows[current].chars);
            return 0;
        }

        current += direction;
    }

    return -1;
}

static void EditorFind(void)
{
    char query[64];
    int query_len = 0;
    int direction = 1; /* 1 forward, -1 backward */
    int last_match_row = -1;

    /* remember where we were, ESC puts everything back */
    int saved_x = vted_editor_config.cursor_x;
    int saved_y = vted_editor_config.cursor_y;
    int saved_row_offset = vted_editor_config.row_offset;
    int saved_col_offset = vted_editor_config.col_offset;

    query[0] = '\0';

    while (1)
    {
        SetStatusMessage("Find: %s (ESC cancel, Enter keep, arrows next/prev)", query);
        RefreshScreen();

        int c = ReadKeyPress();

        if (c == 0)
        {
            /* idle read timeout: repaint the prompt, do not touch
               the search state or the cursor would drift forward */
            continue;
        }

        if (c == 27) /* ESC: cancel and restore the previous position */
        {
            vted_editor_config.cursor_x = saved_x;
            vted_editor_config.cursor_y = saved_y;
            vted_editor_config.row_offset = saved_row_offset;
            vted_editor_config.col_offset = saved_col_offset;
            break;
        }

        if (c == '\r' || c == '\n') /* Enter: keep the current position */
        {
            break;
        }

        if (c == ArrowRight || c == ArrowDown)
        {
            direction = 1;
        }
        else if (c == ArrowLeft || c == ArrowUp)
        {
            direction = -1;
        }
        else if (c == 127) /* backspace edits the query */
        {
            if (query_len > 0)
            {
                query[--query_len] = '\0';
                last_match_row = -1; /* restart from the cursor */
            }
        }
        else if (c >= 32 && c <= 126 && query_len < 63)
        {
            query[query_len++] = (char)c;
            query[query_len] = '\0';
            last_match_row = -1; /* restart from the cursor */
        }

        /* (re)run the search: from the last match when jumping with
           arrows, from the cursor when the query itself changed */
        int search_start;
        if (last_match_row >= 0)
        {
            search_start = last_match_row + direction;
        }
        else
        {
            search_start = vted_editor_config.cursor_y + (direction > 0 ? 0 : -1);
        }

        int match_row;
        int match_col;

        if (FindMatch(query, search_start, direction, &match_row, &match_col) == 0)
        {
            last_match_row = match_row;
            vted_editor_config.cursor_y = match_row;
            vted_editor_config.cursor_x = match_col;

            /* highlight the match on screen */
            vted_editor_config.match_row = match_row;
            vted_editor_config.match_col = match_col;
            vted_editor_config.match_len = query_len;
        }
        else
        {
            last_match_row = -1;

            /* no match for the current query: drop the highlight */
            vted_editor_config.match_row = -1;
            vted_editor_config.match_len = 0;
        }
    }

    /* leaving the search clears the highlight */
    vted_editor_config.match_row = -1;
    vted_editor_config.match_len = 0;

    SetStatusMessage("");
}

int main(int argc, char *argv[])
{
    int quit_confirmations = QUIT_CONFIRMATIONS;

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();
    EnterToAlternateScreenBuffer();
    InitBuffer();

    if (argc >= 2)
    {
        if (OpenFile(argv[1]) == -1)
        {
            KillApp("OpenFile");
        }
    }

    InitVtedEditor();

    while (1)
    {
        int c_in = ReadKeyPress();

        if (c_in == 0)
        {
            /* idle read timeout: nothing was typed, only keep the
               screen fresh (resizes repaint here too). The quit
               confirmation count must survive idle ticks, so they
               never fall through to the key dispatch. */
            RefreshScreen();
            continue;
        }

        if (c_in == CTRL_KEY('q') || c_in == CTRL_KEY('Q'))
        {
            if (vted_buffer.dirty != 0 && quit_confirmations > 0)
            {
                SetStatusMessage("WARNING! Unsaved changes. Press Ctrl-Q %d more times to quit.",
                                 quit_confirmations);
                quit_confirmations--;
            }
            else
            {
                ExitAlternateScreenBuffer();
                FreeBuffer();
                return 0;
            }
        }
        else
        {
            /* any other key restarts the quit confirmation count */
            quit_confirmations = QUIT_CONFIRMATIONS;

            /* only a run of vertical moves keeps aiming for the same
               display column; every other key forgets it */
            if (c_in != ArrowUp && c_in != ArrowDown && c_in != PageUp && c_in != PageDown)
            {
                vted_editor_config.preferred_rx = -1;
            }

            /* one key, one action: the dispatch is a chain so special
               key codes are never mistaken for text bytes */
            if (c_in == ArrowUp || c_in == ArrowDown || c_in == ArrowLeft || c_in == ArrowRight ||
                c_in == PageUp || c_in == PageDown || c_in == HomeKey || c_in == EndKey)
            {
                MoveCursor(c_in);
            }
            else if (c_in == '\r' || c_in == '\n')
            {
                /* Enter splits the current line at the cursor */
                EditorInsertNewline();
            }
            else if (c_in == 127)
            {
                /* Backspace removes the character left of the cursor */
                EditorDelChar();
            }
            else if (c_in == DeleteKey)
            {
                /* Delete removes the character at the cursor: stepping
                   right first turns it into a delete-left operation.
                   At the very end of the file the step does not move
                   the cursor, and deleting then would remove the
                   character on the left, so do nothing instead */
                int before_x = vted_editor_config.cursor_x;
                int before_y = vted_editor_config.cursor_y;

                MoveCursor(ArrowRight);

                if (vted_editor_config.cursor_x != before_x ||
                    vted_editor_config.cursor_y != before_y)
                {
                    EditorDelChar();
                }
            }
            else if (c_in == '\t')
            {
                /* Tab inserts a real tab character; the renderer lays
                   it out up to the next tab stop */
                EditorInsertChar('\t');
            }
            else if (c_in == CTRL_KEY('s') || c_in == CTRL_KEY('S'))
            {
                /* Ctrl-S writes the buffer back to its file */
                EditorSave();
            }
            else if (c_in == CTRL_KEY('f') || c_in == CTRL_KEY('F'))
            {
                /* Ctrl-F searches the buffer incrementally */
                EditorFind();
            }
            else if (c_in == CTRL_KEY('g') || c_in == CTRL_KEY('G'))
            {
                /* Ctrl-G jumps to a line number */
                EditorGoToLine();
            }
            else if ((c_in >= 32 && c_in <= 126) || (c_in >= 128 && c_in <= 255))
            {
                /* printable ASCII and UTF-8 bytes land in the text;
                   key codes at 1000 and above never reach this branch */
                EditorInsertChar(c_in);
            }
        }

        RefreshScreen();
    }

    return 0;
}
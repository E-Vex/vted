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
#include "vted_edit.h"
#include "vted_undo.h"
#include "vted_file.h"
#include "vted_syntax.h"
#include "vted_utf8.h"
/*===============================*/

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

    /* a buffer that just got its name may now have a language */
    if (named_here)
    {
        SyntaxSelectForFile(vted_buffer.filename);
    }

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

    /* the search moves the cursor on its own: the selection does not
       survive it */
    EditClearSelection();
    ScreenSetSelection(-1, 0, -1, 0);
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
            /* drop the old highlight before the new one is painted */
            if (vted_editor_config.match_row >= 0)
            {
                ScreenMarkRowDirty(vted_editor_config.match_row);
            }

            last_match_row = match_row;
            vted_editor_config.cursor_y = match_row;
            vted_editor_config.cursor_x = match_col;

            /* highlight the match on screen */
            vted_editor_config.match_row = match_row;
            vted_editor_config.match_col = match_col;
            vted_editor_config.match_len = query_len;

            ScreenMarkRowDirty(match_row);
        }
        else
        {
            last_match_row = -1;

            /* no match for the current query: drop the highlight */
            if (vted_editor_config.match_row >= 0)
            {
                ScreenMarkRowDirty(vted_editor_config.match_row);
            }

            vted_editor_config.match_row = -1;
            vted_editor_config.match_len = 0;
        }
    }

    /* leaving the search clears the highlight */
    if (vted_editor_config.match_row >= 0)
    {
        ScreenMarkRowDirty(vted_editor_config.match_row);
    }

    vted_editor_config.match_row = -1;
    vted_editor_config.match_len = 0;

    SetStatusMessage("");
}

static void EditorReplace(void)
{
    /* search and replace every occurrence: the replacement is applied
       as one undoable step per occurrence, so the whole operation can
       be walked back with Ctrl-Z */
    EditClearSelection();
    ScreenSetSelection(-1, 0, -1, 0);

    char *query = EditorPrompt("Replace: ");

    if (query == NULL)
    {
        SetStatusMessage("Replace cancelled");
        return;
    }

    char *replacement = EditorPrompt("with: ");

    if (replacement == NULL)
    {
        free(query);
        SetStatusMessage("Replace cancelled");
        return;
    }

    int count = BufferReplaceAll(query, replacement);

    free(query);
    free(replacement);

    SetStatusMessage("Replaced %d occurrence%s", count, count == 1 ? "" : "s");
}

/* gives back everything the editor holds: the undo history, the
   colors of the syntax module, the scratch space of the screen
   module and the text buffer itself. It runs once, on the way out,
   whichever way the editor leaves */
static void FreeEditorState(void)
{
    UndoReset();
    SyntaxFree();
    ScreenFree();
    FreeBuffer();
}

int main(int argc, char *argv[])
{
    int quit_confirmations = QUIT_CONFIRMATIONS;

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();
    SetupExitSignals();
    SetupResizeSignal();
    EnterToAlternateScreenBuffer();
    EnableBracketedPaste();
    InitBuffer();

    /* registered right after the buffer exists, so the state is
       released even when a fatal error ends the editor early */
    atexit(FreeEditorState);

    if (argc >= 2)
    {
        if (OpenFile(argv[1]) == -1)
        {
            KillApp("OpenFile");
        }
    }

    /* the file's name tells which language its text is in */
    SyntaxSelectForFile(vted_buffer.filename);

    /* whatever was edited before this file is unreachable now */
    UndoReset();

    InitVtedEditor();

    while (1)
    {
        int c_in = ReadKeyPress();

        if (c_in == 0)
        {
            /* idle read timeout: nothing was typed. A signal or a
               closed stdin asks the editor to leave: the exit
               handlers restore the terminal and free the state. A
               terminal resize repaints at the new size. */
            if (TerminalWantsExit())
            {
                break;
            }

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
                return 0; /* the state is freed by FreeEditorState */
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
            if (c_in == ShiftArrowLeft || c_in == ShiftArrowRight || c_in == ShiftArrowUp ||
                c_in == ShiftArrowDown || c_in == ShiftHome || c_in == ShiftEnd)
            {
                /* shift movements start or extend the selection: the
                   anchor is set where the run began, the cursor end
                   follows the move */
                if (!EditSelectionActive())
                {
                    EditSetAnchor(vted_editor_config.cursor_y,
                                  vted_editor_config.cursor_x);
                }

                MoveCursor(c_in);

                int top, left, bottom, right;

                EditSelectionRange(&top, &left, &bottom, &right);
                ScreenSetSelection(top, left, bottom, right);
            }
            else if (c_in == ArrowUp || c_in == ArrowDown || c_in == ArrowLeft || c_in == ArrowRight ||
                     c_in == PageUp || c_in == PageDown || c_in == HomeKey || c_in == EndKey ||
                     c_in == WordLeft || c_in == WordRight)
            {
                /* anything else forgets the selection */
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
                MoveCursor(c_in);
            }
            else if (c_in == MoveLineUp || c_in == MoveLineDown)
            {
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);

                if (c_in == MoveLineUp)
                {
                    EditorMoveLineUp();
                }
                else
                {
                    EditorMoveLineDown();
                }
            }
            else if (c_in == PasteBegin)
            {
                /* a paste replaces the selection first, then arrives
                   whole between its two markers */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }

                int pasted_length = 0;
                char *pasted = ReadPastedText(&pasted_length);

                if (pasted != NULL)
                {
                    EditorInsertText(pasted, pasted_length);
                    free(pasted);
                }
            }
            else if (c_in == '\r' || c_in == '\n')
            {
                /* Enter replaces the selection first, then splits */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }

                EditorInsertNewline();
            }
            else if (c_in == 127)
            {
                /* Backspace removes the selection, or the character
                   left of the cursor */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }
                else
                {
                    EditorDelChar();
                }
            }
            else if (c_in == DeleteKey)
            {
                /* Delete removes the selection, or the character under
                   the cursor */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }
                else
                {
                    int before_x = vted_editor_config.cursor_x;
                    int before_y = vted_editor_config.cursor_y;

                    MoveCursor(ArrowRight);

                    if (vted_editor_config.cursor_x != before_x ||
                        vted_editor_config.cursor_y != before_y)
                    {
                        EditorDelChar();
                    }
                }
            }
            else if (c_in == '\t')
            {
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }

                if (vted_editor_config.insert_spaces != 0)
                {
                    /* Tab inserts spaces up to the next tab stop;
                       each keystroke is contiguous, so they undo as
                       one burst */
                    int i;

                    for (i = 0; i < vted_editor_config.tab_width; i++)
                    {
                        EditorInsertChar(' ');
                    }
                }
                else
                {
                    /* Tab inserts a real tab character; the renderer
                       lays it out up to the next tab stop */
                    EditorInsertChar('\t');
                }
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
            else if (c_in == CTRL_KEY('r') || c_in == CTRL_KEY('R'))
            {
                /* Ctrl-R searches and replaces every occurrence */
                EditorReplace();
            }
            else if (c_in == CTRL_KEY('g') || c_in == CTRL_KEY('G'))
            {
                /* Ctrl-G jumps to a line number */
                EditorGoToLine();
            }
            else if (c_in == CTRL_KEY('n') || c_in == CTRL_KEY('N'))
            {
                /* Ctrl-N shows or hides the line number gutter */
                vted_editor_config.show_line_numbers = !vted_editor_config.show_line_numbers;
                SetStatusMessage("Line numbers %s",
                                 vted_editor_config.show_line_numbers ? "on" : "off");
            }
            else if (c_in == MoveLineUp || c_in == MoveLineDown)
            {
                /* Ctrl-Up / Ctrl-Down move the current line and ride
                   the cursor along with it */
                if (c_in == MoveLineUp)
                {
                    EditorMoveLineUp();
                }
                else
                {
                    EditorMoveLineDown();
                }
            }
            else if (c_in == CTRL_KEY('k') || c_in == CTRL_KEY('K'))
            {
                /* Ctrl-K deletes the whole line under the cursor */
                if (EditSelectionActive())
                {
                    EditClearSelection();
                    ScreenSetSelection(-1, 0, -1, 0);
                }

                EditorDeleteLine();
            }
            else if (c_in == CTRL_KEY('d') || c_in == CTRL_KEY('D'))
            {
                /* Ctrl-D duplicates the line under the cursor */
                if (EditSelectionActive())
                {
                    EditClearSelection();
                    ScreenSetSelection(-1, 0, -1, 0);
                }

                EditorDuplicateLine();
            }
            else if (c_in == CTRL_KEY('z') || c_in == CTRL_KEY('Z'))
            {
                /* Ctrl-Z steps the buffer back through its changes */
                if (EditorUndo() == 0)
                {
                    SetStatusMessage("Nothing to undo");
                }

                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
            }
            else if (c_in == CTRL_KEY('y') || c_in == CTRL_KEY('Y'))
            {
                /* Ctrl-Y steps the buffer forward through undone
                   changes */
                if (EditorRedo() == 0)
                {
                    SetStatusMessage("Nothing to redo");
                }

                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
            }
            else if (c_in == CTRL_KEY('c') || c_in == CTRL_KEY('C'))
            {
                /* Ctrl-C copies the selection (or the current line) to
                   the clipboard with an OSC 52 escape sequence */
                EditorCopy();
            }
            else if (c_in == CTRL_KEY('x') || c_in == CTRL_KEY('X'))
            {
                /* Ctrl-X cuts the selection (or the current line) to
                   the clipboard */
                EditorCut();
            }
            else if ((c_in >= 32 && c_in <= 126) || (c_in >= 128 && c_in <= 255))
            {
                /* printable ASCII and UTF-8 bytes land in the text,
                   replacing the selection first */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }

                EditorInsertChar(c_in);
            }
        }

        RefreshScreen();
    }

    /* leaving for good: leave the alternate screen so the shell
       resumes on the main screen; the exit handlers restore the
       terminal settings and free the editor state */
    ExitAlternateScreenBuffer();
    return 0;
}
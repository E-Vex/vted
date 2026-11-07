/* isatty() is POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <limits.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_regex.h"
#include "vted_edit.h"
#include "vted_undo.h"
#include "vted_file.h"
#include "vted_path.h"
#include "vted_syntax.h"
#include "vted_utf8.h"
#include "vted_cli.h"
#include "vted_config.h"
#include "vted_theme.h"
#include "vted_buffers.h"
/*===============================*/

/* longest answer a prompt accepts, in bytes (a path fits comfortably) */
#define PROMPT_MAX 1024

/* how many lines the menu of a path listing draws, and the room one
   of its lines has: the window the buffer picker's menu works with,
   a directory carrying its slash so the menu says which answers
   walk further */
#define PATH_MENU_LINES 16
#define PATH_MENU_WIDTH (VTED_PATH_NAME_MAX + 2)

/* how many screen lines one notch of the wheel scrolls, the way a
   terminal of its own scrolls a page the mouse is over */
#define MOUSE_WHEEL_LINES 3

/* how many milliseconds a second press may follow the one before it
   and still count as a double click, the way a desktop tells them
   apart */
#define DOUBLE_CLICK_MS 400

/* the answer the search prompts keep, so the next one opens with
   it: Enter alone repeats the search as it stands */
static char last_query[PROMPT_MAX + 1];
static char last_replacement[PROMPT_MAX + 1];

/* the queries the searches have settled on, newest last: the up and
   down arrows of the search walk them, the way a shell walks the
   commands it was given */
#define SEARCH_HISTORY_MAX 64
static char search_history[SEARCH_HISTORY_MAX][64];
static int search_history_count = 0;

/* a query the search settles on goes into the history, newest last:
   one the newest entry already says is not said again, and the oldest
   falls off when the room runs out */
static void SearchHistoryPush(const char *query)
{
    if (query[0] == '\0')
    {
        return;
    }

    if (search_history_count > 0 &&
        strcmp(search_history[search_history_count - 1], query) == 0)
    {
        return; /* the newest query already says it */
    }

    if (search_history_count == SEARCH_HISTORY_MAX)
    {
        memmove(search_history[0], search_history[1],
                (SEARCH_HISTORY_MAX - 1) * sizeof(search_history[0]));
        search_history_count--;
    }

    snprintf(search_history[search_history_count],
             sizeof(search_history[0]), "%s", query);
    search_history_count++;
}

/* copies a query into the search's own buffer, which holds 63 bytes
   the way typing fills it, and never a cut inside a character;
   returns the length it came to */
static int CopyQuery(char *query, const char *from)
{
    int length = (int)strlen(from);

    if (length > 63)
    {
        length = 63;
    }

    while (length > 0 && ((unsigned char)from[length - 1] & 0xC0) == 0x80)
    {
        length--; /* the cut ended inside a character */
    }

    memcpy(query, from, (size_t)length);
    query[length] = '\0';
    return length;
}

/* the way the search reads its query: 1 reads it as a regular
   expression the way grep -E speaks one, 0 as the text itself. The
   search prompt flips it and the next search opens the way the last
   one left it */
static int search_regex_mode = 0;

/* the prompt that holds a pattern asks this how its label reads with
   the mode turned over: the search's own prompt answers for itself,
   and the replace installs the answer while its question is open */
static const char *(*PromptModeToggle)(void) = NULL;

/* the Alt-o a prompt took opened a file: an answer of NULL is then
   no cancel but the open, and the callers that say why a prompt
   came back empty stay quiet, so the open's own message keeps the
   bar */
static int prompt_opened = 0;

/* says why a prompt came back empty, unless its Alt-o opened a file
   and the open's own message keeps the bar */
static void PromptCancelled(const char *why)
{
    if (!prompt_opened)
    {
        SetStatusMessage("%s", why);
    }
}

/* keeps a copy of the answer for the next prompt to open with */
static void RememberAnswer(char *slot, const char *answer)
{
    snprintf(slot, PROMPT_MAX + 1, "%s", answer);
}

/* carries a suspend request out where it arrives: Alt-z asks for it
   while editing, and a kill -TSTP from outside leaves only a flag
   behind. Every read loop answers it right after reading a key, so
   no prompt keeps the process awake when the shell wants it back */
static void EditorSuspendIfAsked(int c)
{
    if (c == SuspendEditor || TerminalSuspendRequested())
    {
        TerminalSuspend();
    }
}

/* the bytes of an answer that name the directory — everything up to
   and including its slash, or none when the path names the working
   directory */
static int PromptDirSpan(const char *answer)
{
    const char *last_slash = strrchr(answer, '/');

    return last_slash != NULL ? (int)(last_slash - answer) + 1 : 0;
}

/* lays one match of a path listing into the answer behind its
   directory: the name whole, a directory with a slash behind it, so
   the next Tab walks into it and Enter never hands a directory to
   the open. Returns the length the answer came to, never inside a
   character */
static int PromptTakeMatch(char *answer, const vted_path_match_t *match)
{
    int dir_span = PromptDirSpan(answer);
    int room = PROMPT_MAX + 1 - dir_span;
    int written = snprintf(answer + dir_span, (size_t)room, "%s%s",
                           match->name, match->is_dir ? "/" : "");

    if (written >= room)
    {
        written = room - 1; /* the room ended before the name did */
    }

    while (written > 0 &&
           ((unsigned char)answer[dir_span + written - 1] & 0xC0) == 0x80)
    {
        written--; /* the room ended inside a character */
    }

    answer[dir_span + written] = '\0';
    return dir_span + written;
}

/* shows the matches of a path listing over the bottom of the
   viewport, a window that follows the selection the way the buffer
   picker's window does. The lines live in the caller's storage,
   borrowed by the screen until it is told otherwise */
static void PromptShowPathMenu(const vted_path_match_t *matches,
                               int count, int selection,
                               char lines[][PATH_MENU_WIDTH],
                               const char *shown_lines[PATH_MENU_LINES])
{
    int shown = count;
    int first;
    int i;

    if (shown > PATH_MENU_LINES)
    {
        shown = PATH_MENU_LINES;
    }
    if (shown > vted_editor_config.screen_rows - 2)
    {
        shown = vted_editor_config.screen_rows - 2;
    }
    if (shown < 1)
    {
        shown = 1;
    }

    first = selection - shown / 2;
    if (first < 0)
    {
        first = 0;
    }
    if (first > count - shown)
    {
        first = count - shown;
    }

    for (i = 0; i < shown; i++)
    {
        snprintf(lines[i], PATH_MENU_WIDTH, "%s%s",
                 matches[first + i].name,
                 matches[first + i].is_dir ? "/" : "");
        shown_lines[i] = lines[i];
    }

    ScreenShowMenu(shown_lines, shown, selection - first);
}

/* says whether the path names a file that is there to open: stat
   answers for a regular file, and everything else — a directory, a
   name that is not there — is no file to go to */
static int RegularFileThere(const char *path)
{
    struct stat info;

    return path != NULL && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

/* lands the cursor on the line and column a jump names — the tail a
   name carries, or the answer Ctrl-G takes: a line past the end
   lands on the last one, a column past a row's end on that row's
   end, and the spot lands mid-viewport */
static void EditorLandJump(int line, int column)
{
    vted_row_t *row;

    if (line < 1 || vted_buffer.num_rows == 0)
    {
        return;
    }

    if (line > vted_buffer.num_rows)
    {
        line = vted_buffer.num_rows;
    }

    vted_editor_config.cursor_y = line - 1;

    /* the column counts the cells the bar shows, so the spot it names
       is where it lands; a row's end is as far as it goes */
    row = &vted_buffer.rows[vted_editor_config.cursor_y];
    vted_editor_config.cursor_x = column > 0
                                      ? RowRxToCx(row, column - 1)
                                      : 0;

    ScreenCenterCursor();
}

/* opens the name the way the Alt-o keys do: a relative name stands
   beside the file being edited first — the way an #include does —
   and as it stands, in the working directory, when that one does
   not answer. A file that is already open just comes back into
   view, and a name that is no file at all says so instead: gf goes
   to a file that is there, it does not start one. The line and
   column name the spot the open lands on. Says what happened in
   message — "Opened ..." or the reason no file came open — for the
   caller to put in the bar. Returns 1 when a file came open, 0
   when the name answered as none */
static int EditorOpenNamed(const char *name, int line, int column,
                            char *message, size_t message_size)
{
    char beside[PATH_MAX];
    const char *dir = NULL; /* the spelling beside the file being edited */
    const char *open;
    struct stat info;

    /* a relative name beside a file that has a directory of its own:
       the directory the file was opened from answers for it */
    if (name[0] != '/' && vted_buffer.filename != NULL)
    {
        int dir_span = PromptDirSpan(vted_buffer.filename);

        if (dir_span > 0 &&
            snprintf(beside, sizeof(beside), "%.*s%s",
                     dir_span, vted_buffer.filename, name) < (int)sizeof(beside))
        {
            dir = beside;
        }
    }

    /* gf goes to a file that is there: beside the file being edited
       first, then as it stands */
    if (RegularFileThere(dir))
    {
        open = dir;
    }
    else if (RegularFileThere(name))
    {
        open = name;
    }
    else
    {
        /* a name that answered, but as no file: a directory; one that
           answered nowhere is simply not there */
        if (stat(name, &info) == 0 || (dir != NULL && stat(dir, &info) == 0))
        {
            snprintf(message, message_size,
                     "A directory, not a file: %s", name);
        }
        else
        {
            snprintf(message, message_size, "No such file: %s", name);
        }

        return 0;
    }

    if (BuffersOpen(open) == -1)
    {
        int saved_errno = errno;

        snprintf(message, message_size, "Could not open: %s",
                 saved_errno != 0 ? strerror(saved_errno)
                                  : "not a regular file");
        return 0;
    }

    /* the line and column name the spot the open lands on, in the
       buffer that just came into view */
    EditorLandJump(line, column);

    snprintf(message, message_size, "Opened %s (buffer %d of %d)",
             vted_buffer.filename, BuffersCurrent() + 1,
             BuffersCount());
    return 1;
}

/* one round of the completion a path answer takes: the directory
   the typed path names answers with the names that start with what
   follows its slash, the way the shell's Tab asks — one landing
   whole, a directory with a slash behind it so the next round
   walks in, several landing the part they agree on first with the
   menu left standing for the choosing. Returns the length the
   answer came to and reports the listing: *menu stands at the
   count the round left the menu with, 0 saying the menu is down,
   *selection at the line it stands on, and a notice is laid when
   nothing answered */
static int PromptCompletePath(char *answer, int length,
                              vted_path_match_t *matches,
                              int *menu, int *selection,
                              char *notice, size_t notice_size)
{
    int found = PathListMatches(answer, matches, VTED_PATH_MATCHES);
    int dir_span = PromptDirSpan(answer);

    *menu = 0;

    if (found < 0)
    {
        /* the directory itself could not be read: the reason says
           so, the answer stands as it was typed */
        snprintf(notice, notice_size,
                 "Cannot read the directory: %s", strerror(errno));
    }
    else if (found == 0)
    {
        snprintf(notice, notice_size, "No match for '%s'", answer);
    }
    else if (found == 1)
    {
        /* one name answers: it lands whole, a directory with a
           slash behind it so the next round walks in */
        length = PromptTakeMatch(answer, &matches[0]);
    }
    else
    {
        /* several answer: the part they agree on lands first,
           walked back to a boundary should they part inside a
           character, and the menu lists them for the choosing */
        int stem = PathSharedStem(matches, found);
        int typed = length - dir_span;

        if (stem > typed)
        {
            memcpy(answer + dir_span, matches[0].name,
                   (size_t)stem);
            length = dir_span + stem;

            while (length > dir_span &&
                   ((unsigned char)answer[length - 1] & 0xC0) == 0x80)
            {
                length--; /* the stem ended inside a character */
            }

            answer[length] = '\0';
        }

        *menu = found;
        *selection = 0;
    }

    return length;
}

/* asks for one line of text in the message bar, opening with the
   given answer when there is one: Enter takes it as it stands and a
   backspace takes it apart. Returns it in a heap buffer the caller
   frees, or NULL when the person cancels with ESC or confirms an
   empty answer — or when its Alt-o opened the file the answer
   names, which prompt_opened then says */
static char *EditorPromptEx(const char *label, const char *initial,
                            int completes_paths)
{
    char *answer = malloc(PROMPT_MAX + 1);
    int length = 0;

    prompt_opened = 0;

    /* the label the prompt carries until Ctrl-T asks the search's
       mode to turn and the turned label comes back */
    const char *shown = label;

    /* the matches of the last Tab and the menu they were listed in:
       a count of 0 means the menu is down. The lines are borrowed by
       the screen until the next call, so they live as long as the
       prompt does */
    vted_path_match_t matches[VTED_PATH_MATCHES];
    char menu_lines[PATH_MENU_LINES][PATH_MENU_WIDTH];
    const char *menu_shown[PATH_MENU_LINES];
    int match_count = 0;
    int selection = 0;

    /* a word the bar holds until the next key: what a Tab that found
       nothing says, the prompt drawn again behind it */
    char notice[PROMPT_MAX + 1];

    notice[0] = '\0';

    if (answer == NULL)
    {
        KillApp("malloc");
    }

    answer[0] = '\0';

    if (initial != NULL)
    {
        /* the answer opens with the initial text, kept inside the room
           a prompt has and never cut inside a character */
        length = (int)strlen(initial);

        if (length > PROMPT_MAX)
        {
            length = PROMPT_MAX;
        }

        while (length > 0 && ((unsigned char)initial[length - 1] & 0xC0) == 0x80)
        {
            length--; /* a continuation byte: the cut split a character */
        }

        memcpy(answer, initial, (size_t)length);
        answer[length] = '\0';
    }

    while (1)
    {
        if (notice[0] != '\0')
        {
            SetStatusMessage("%s", notice);
        }
        else
        {
            SetStatusMessage("%s%s", shown, answer);
        }

        RefreshScreen();

        int c = ReadKeyPress();

        EditorSuspendIfAsked(c);

        if (c == 0)
        {
            /* idle read timeout: just repaint the prompt — unless a
               signal asked the editor to leave, which a prompt must
               not outwait */
            if (TerminalWantsExit())
            {
                ScreenShowMenu(NULL, 0, 0);
                SetStatusMessage("");
                free(answer);
                return NULL;
            }

            continue;
        }

        /* a real key answers the notice the last Tab left */
        notice[0] = '\0';

        if (c == '\t' && completes_paths)
        {
            /* Tab completes the path typed so far, the way the
               shell's Tab does: the round does the asking, and the
               menu it leaves standing — or the notice it laid —
               goes up around it */
            length = PromptCompletePath(answer, length, matches,
                                        &match_count, &selection,
                                        notice, sizeof(notice));

            if (match_count > 0)
            {
                PromptShowPathMenu(matches, match_count, selection,
                                   menu_lines, menu_shown);
            }
            else
            {
                ScreenShowMenu(NULL, 0, 0);
            }

            continue;
        }

        if (match_count > 0)
        {
            /* the menu stands: ESC takes it down with the prompt
               standing, Enter or a left click takes the line under
               them into the answer, the arrows and the wheel step
               through it, and any other key takes it down — the
               answer going on in the prompt, a fresh Tab listing
               again */
            if (c == 27 || c == '\r' || c == '\n' || c == ArrowUp ||
                c == ArrowDown || c == MouseWheelUp ||
                c == MouseWheelDown || c == MousePress)
            {
                if (c == 27)
                {
                    ScreenShowMenu(NULL, 0, 0);
                    match_count = 0;
                    continue;
                }

                if (c == '\r' || c == '\n')
                {
                    length = PromptTakeMatch(answer, &matches[selection]);
                    ScreenShowMenu(NULL, 0, 0);
                    match_count = 0;
                    continue;
                }

                if (c == MousePress)
                {
                    /* a left click takes the line drawn under it, the
                       way Enter takes the one under the selection; a
                       click of another button, or past the list, keeps
                       the menu waiting */
                    if (MouseLastButton() == 0)
                    {
                        int line = ScreenMenuLineAt(MouseLastY());

                        if (line >= 0)
                        {
                            length = PromptTakeMatch(answer,
                                                     &matches[line]);
                            ScreenShowMenu(NULL, 0, 0);
                            match_count = 0;
                        }
                    }

                    continue;
                }

                if (c == MouseWheelUp && selection > 0)
                {
                    selection--;
                }
                else if (c == MouseWheelDown &&
                         selection < match_count - 1)
                {
                    selection++;
                }
                else if (c == ArrowUp)
                {
                    selection = selection > 0 ? selection - 1
                                              : match_count - 1;
                }
                else if (c == ArrowDown)
                {
                    selection = selection < match_count - 1
                                    ? selection + 1
                                    : 0;
                }

                PromptShowPathMenu(matches, match_count, selection,
                                   menu_lines, menu_shown);
                continue;
            }

            ScreenShowMenu(NULL, 0, 0);
            match_count = 0;
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

        if (c == 20) /* Ctrl-T turns the reading of the query over */
        {
            if (PromptModeToggle != NULL)
            {
                shown = PromptModeToggle();
            }

            continue;
        }

        if (c == OpenFileAtCursor)
        {
            /* Alt-o opens the file the answer names, the way it does
               at the cursor: the answer as it stands, its tail naming
               the spot, the same beside-first standing it takes there.
               A name that opens no file says so in the notice while
               the prompt stands; an empty answer says there is
               nothing to open */
            int jump_line;
            int jump_column;
            int name_len = RowNameTail(answer, &jump_line, &jump_column);
            char message[PROMPT_MAX + 1]; /* sized to the notice it
                                             lands in when no file
                                             came open */
            char beyond;
            int opened;

            if (name_len == 0)
            {
                snprintf(notice, sizeof(notice),
                         "No file name in the answer");
                ScreenShowMenu(NULL, 0, 0);
                match_count = 0;
                continue;
            }

            beyond = answer[name_len];
            answer[name_len] = '\0';

            opened = EditorOpenNamed(answer, jump_line, jump_column,
                                     message, sizeof(message));

            answer[name_len] = beyond; /* the tail goes back on */

            if (opened)
            {
                /* the open ends the prompt the way ESC does, the bar
                   saying where it landed */
                ScreenShowMenu(NULL, 0, 0);
                SetStatusMessage("%s", message);
                prompt_opened = 1;
                free(answer);
                return NULL;
            }

            snprintf(notice, sizeof(notice), "%s", message);
            ScreenShowMenu(NULL, 0, 0);
            match_count = 0;
            continue;
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

static char *EditorPrompt(const char *label, const char *initial)
{
    /* the prompts without a path to complete answer as they always
       did */
    return EditorPromptEx(label, initial, 0);
}

static char *EditorPromptPath(const char *label, const char *initial)
{
    /* the prompts that take a path answer Tab with the names of the
       directory the path names */
    return EditorPromptEx(label, initial, 1);
}

/* copies the settings into the editor state; the tab width the rows
   lay out with and the gutter the next frame draws both come from
   them */
static void ApplySettings(const vted_settings_t *settings)
{
    vted_editor_config.insert_spaces = settings->insert_spaces;
    vted_editor_config.show_line_numbers = settings->show_line_numbers;
    vted_editor_config.show_tabs = settings->show_tabs;
    vted_editor_config.wrap = settings->wrap;
    vted_editor_config.wrap_column = settings->wrap_column;
    vted_editor_config.wrap_indent = settings->wrap_indent;
    vted_editor_config.indent_guides = settings->indent_guides;
    vted_editor_config.electric_brackets = settings->electric_brackets;

    /* the color of the bracket match reaches the syntax module, which
       holds it the way it holds the language of the file */
    SyntaxSetBracketColor(settings->bracket_color);

    /* the search takes its case from the setting, and the replace
       follows, both through the row module the search runs in; the
       regex one takes it in at the compile of its pattern */
    RowSetIgnoreCase(settings->ignore_case);
    RegexSetIgnoreCase(settings->ignore_case);

    /* the way a search opens reading its query comes from the
       setting too, and Ctrl-T turns it either way from there */
    search_regex_mode = settings->search_regex != 0;

    /* the width reaches every buffer open at startup, so they all
       begin with it; Ctrl-W later sets the live one's alone */
    BuffersSetTabWidth(settings->tab_width);

    ScreenMarkAllDirty();
}

/* highlights the partner of the byte the cursor sits on — a
   bracket's own kind, a quote's far end — or the one right behind
   it; a cursor without such a byte clears the highlight */
static void UpdateBracketMatch(void)
{
    int match_y = 0;
    int match_x = 0;

    if (BufferFindPartner(vted_editor_config.cursor_y,
                          vted_editor_config.cursor_x,
                          &match_y, &match_x))
    {
        ScreenSetBracketMatch(match_y, match_x);
    }
    else
    {
        ScreenSetBracketMatch(-1, 0);
    }
}

/* Alt-% lands the cursor on the partner of the byte it sits on, or
   the one right behind it: the partner the highlight marks — a
   bracket's own kind, a quote's far end — given the middle of the
   viewport when the jump leaves it — a partner that stays on screen
   keeps the viewport as it is */
static void EditorJumpToBracket(void)
{
    int match_y = 0;
    int match_x = 0;

    if (!BufferFindPartner(vted_editor_config.cursor_y,
                           vted_editor_config.cursor_x,
                           &match_y, &match_x))
    {
        SetStatusMessage("No bracket or quote to jump to");
        return;
    }

    vted_editor_config.cursor_y = match_y;
    vted_editor_config.cursor_x = match_x;

    ScreenCenterCursor();
}

/* says whether the typed closing bracket is already under the cursor,
   paired with its opener behind it: the typing steps over it then,
   instead of laying another one in. The match the bracket scan finds
   for the closer under the cursor is its opener, which only ever sits
   before it, so a match at all is the answer */
static int CursorStepsOverCloser(int closer)
{
    int match_y;
    int match_x;

    if (closer != ')' && closer != ']' && closer != '}')
    {
        return 0; /* only a closing half steps over */
    }

    if (vted_editor_config.cursor_y >= vted_buffer.num_rows)
    {
        return 0; /* the phantom row past the end holds no closer */
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

    if (vted_editor_config.cursor_x >= row->size ||
        row->chars[vted_editor_config.cursor_x] != (char)closer)
    {
        return 0; /* the closer is not the one under the cursor */
    }

    return BufferFindBracketMatch(vted_editor_config.cursor_y,
                                  vted_editor_config.cursor_x,
                                  &match_y, &match_x) &&
           (match_y < vted_editor_config.cursor_y ||
            (match_y == vted_editor_config.cursor_y &&
             match_x < vted_editor_config.cursor_x));
}

/* the mouse over the viewport: a click places the cursor at the
   buffer position the renderer drew under it, a notch of the wheel
   scrolls the viewport, and a drag carries a selection from where
   the click went down, one that runs onto the bars scrolling the
   viewport a screen line along at a report. The hit test walks the
   same layout the frame draws, so a click lands on the very glyph
   it was aimed at — the gutter, a wrapped line, a viewport scrolled
   sideways, the phantom row past the end, all of it */

/* set from the press to the release of a held left button: the
   selection follows the cursor from the anchor the press left */
static int mouse_dragging = 0;

/* when the last left press went down, in milliseconds the editor has
   been up: a second one inside DOUBLE_CLICK_MS of it is a double
   click. -1 when there was none, and a press of another button puts
   the chain back to none */
static long mouse_last_press_ms = -1;

/* the clock a double click is told from: the monotonic one, so the
   wall clock moving under it changes nothing */
static long NowMs(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void EditorHandleMouse(int key)
{
    int hit_y;
    int hit_x;
    int row = MouseLastY();

    if (key == MouseWheelUp || key == MouseWheelDown)
    {
        /* the wheel scrolls whole screen lines, the scroll Alt-e and
           Alt-y make, three of them at a turn; the selection is gone
           with it, the way those keys take it */
        EditClearSelection();
        ScreenSetSelection(-1, 0, -1, 0);
        ScreenScroll(key == MouseWheelDown ? MOUSE_WHEEL_LINES
                                           : -MOUSE_WHEEL_LINES);
        return;
    }

    if (key == MouseRelease)
    {
        /* the button came up: whatever the drag selected stays */
        mouse_dragging = 0;
        return;
    }

    /* a drag carrying a selection that leaves the text for the bars
       pulls the viewport one screen line down at a report — they are
       the only edge past the viewport a terminal reports, the top
       being where its pointer stops — and the cell under the pointer
       is then the bottom row, whatever the scroll left under it */
    if (key == MouseDrag && mouse_dragging &&
        row >= vted_editor_config.screen_rows)
    {
        ScreenScroll(1);
        row = vted_editor_config.screen_rows - 1;
    }

    if (!ScreenHitTest(MouseLastX(), row, &hit_y, &hit_x))
    {
        /* over the bars: nothing to place, and a drag keeps waiting
           for the pointer to come back over the text */
        return;
    }

    if (key == MousePress)
    {
        /* the left button places the cursor and anchors a selection
           there — empty until a drag carries the cursor away from
           it, so a plain click selects nothing */
        if (MouseLastButton() == 1)
        {
            /* the middle button pastes the kept copy where it is
               clicked, the way the primary selection pastes elsewhere
               in the terminal: a selection gives way first — the way
               every paste replaces one — the cursor lands at the
               click, and the kept copy arrives at it */
            int laid;

            if (EditSelectionActive())
            {
                EditDeleteSelection();
            }

            vted_editor_config.cursor_y = hit_y;
            vted_editor_config.cursor_x = hit_x;

            laid = EditorPasteKeptCopy();

            if (laid > 0)
            {
                SetStatusMessage("Pasted %d bytes", laid);
            }
            else
            {
                SetStatusMessage("Nothing copied yet");
            }

            mouse_last_press_ms = -1;
            return;
        }

        if (MouseLastButton() != 0)
        {
            /* the right button arrives unbound, and a press of it
               breaks the chain the double click is told from */
            mouse_last_press_ms = -1;
            return;
        }

        /* the second press inside DOUBLE_CLICK_MS of the one before
           it selects the word it lands in: the run of the class of
           the glyph under it, the way the word moves cross it. The
           anchor takes the near end and the cursor the far one, and
           a drag after carries the selection on from there; a press
           past the end of the row, where no word is, falls back to
           the plain click below */
        long now = NowMs();

        if (mouse_last_press_ms >= 0 &&
            now - mouse_last_press_ms <= DOUBLE_CLICK_MS &&
            hit_y < vted_buffer.num_rows)
        {
            vted_row_t *row = &vted_buffer.rows[hit_y];
            int start;
            int end;

            RowWordSpan(row, hit_x, &start, &end);

            if (start < end)
            {
                int top;
                int left;
                int bottom;
                int right;

                EditSetAnchor(hit_y, start);
                vted_editor_config.cursor_y = hit_y;
                vted_editor_config.cursor_x = end;

                EditSelectionRange(&top, &left, &bottom, &right);
                ScreenSetSelection(top, left, bottom, right);

                mouse_last_press_ms = now;
                mouse_dragging = 1;
                return;
            }
        }

        mouse_last_press_ms = now;

        EditClearSelection();
        ScreenSetSelection(-1, 0, -1, 0);

        vted_editor_config.cursor_y = hit_y;
        vted_editor_config.cursor_x = hit_x;

        EditSetAnchor(hit_y, hit_x);
        mouse_dragging = 1;
        return;
    }

    /* the drag carries the cursor, and the selection follows from
       the anchor the press left, the way the shift moves extend one */
    if (mouse_dragging)
    {
        int top;
        int left;
        int bottom;
        int right;

        vted_editor_config.cursor_y = hit_y;
        vted_editor_config.cursor_x = hit_x;

        EditSelectionRange(&top, &left, &bottom, &right);
        ScreenSetSelection(top, left, bottom, right);
    }
}

static void EditorLoadSettings(void)
{
    vted_settings_t settings = SettingsDefaults();
    int bad_lines = 0;

    /* the settings file is where the desktop conventions put it; a
       file that is not there is no error, it is a fresh install */
    const char *path = SettingsPath();

    if (path != NULL)
    {
        FILE *file = fopen(path, "r");

        if (file != NULL)
        {
            bad_lines = SettingsLoad(&settings, file);
            fclose(file);
        }
    }

    ApplySettings(&settings);

    /* the colors of the theme the settings name; a theme that is not
       there leaves the built-in colors and is worth hearing about, as
       is a theme file with lines in it that could not be used */
    if (ThemeSelect(settings.theme) != 0)
    {
        SetStatusMessage("Theme not found: %s", settings.theme);
    }
    else if (ThemeBadLines() > 0)
    {
        SetStatusMessage("%d line%s of theme %s not understood",
                         ThemeBadLines(), ThemeBadLines() == 1 ? "" : "s",
                         settings.theme);
    }
    /* a line that could not be used is worth hearing about: the
       setting it wanted quietly stays at its default */
    else if (bad_lines > 0)
    {
        SetStatusMessage("%d line%s of the settings file not understood",
                         bad_lines, bad_lines == 1 ? "" : "s");
    }
}

static void EditorSetTabWidth(void)
{
    /* the width the buffer lays out with opens as the answer, so Enter
       alone keeps it and the digits are only typed to change it */
    char current[12];

    snprintf(current, sizeof(current), "%d", vted_editor_config.tab_width);

    char *answer = EditorPrompt("Tab width (1-32): ", current);

    if (answer == NULL)
    {
        PromptCancelled("Tab width unchanged");
        return;
    }

    char *end;
    long width = strtol(answer, &end, 10);
    const char *bad = answer;

    if (*end != '\0' || end == answer || width < 1 || width > 32)
    {
        SetStatusMessage("Not a tab width: %s", bad);
        free(answer);
        return;
    }

    free(answer);

    /* Enter on the width already in place keeps it: nothing relayouts,
       the way a cancelled prompt does */
    if ((int)width == vted_editor_config.tab_width)
    {
        SetStatusMessage("Tab width unchanged");
        return;
    }

    vted_editor_config.tab_width = (int)width;
    RowSetTabWidth(vted_editor_config.tab_width);
    /* every tab on screen changes width with the new stop, so the
       rows already drawn lay out differently now */
    ScreenMarkAllDirty();
    SetStatusMessage("Tab width %d", vted_editor_config.tab_width);
}

/* sets the color the matching bracket lights up with while editing:
   the same setting the rc file holds, taken from an answer here */
static void EditorSetBracketColor(void)
{
    /* the prompt opens with the color in place, so Enter alone keeps
       it and a name is only typed to change it */
    char *answer = EditorPrompt(
        "Bracket color (invert/red/green/yellow/blue/magenta/cyan/gray): ",
        SettingsBracketColorName(SyntaxBracketColor()));

    if (answer == NULL)
    {
        PromptCancelled("Bracket color unchanged");
        return;
    }

    int color = SettingsParseBracketColor(answer);

    if (color < 0)
    {
        SetStatusMessage("Not a color: %s", answer);
        free(answer);
        return;
    }

    free(answer);

    /* Enter on the color already in place keeps it: nothing repaints,
       the way a cancelled prompt does */
    if (color == SyntaxBracketColor())
    {
        SetStatusMessage("Bracket color unchanged");
        return;
    }

    /* the color reaches the syntax module the way the rc file's
       setting does, and the bracket on screen takes it on the next
       frame */
    SyntaxSetBracketColor(color);
    ScreenMarkAllDirty();

    SetStatusMessage("Bracket color: %s", SettingsBracketColorName(color));
}

/* picks the color scheme while editing: the theme the rc file names
   with set theme, answered here. Naming the theme already in use loads
   it again, so a theme file being written can be tried by saving it
   and answering with its own name */
static void EditorSetTheme(void)
{
    /* the prompt lists the built-in themes, and a theme file in the
       themes directory goes by its name as well */
    char question[160] = "Theme (";
    size_t used = strlen(question);
    int i;

    for (i = 0; i < ThemeBuiltinCount(); i++)
    {
        int written = snprintf(question + used, sizeof(question) - used, "%s%s",
                               i > 0 ? "/" : "", ThemeBuiltinName(i));

        if (written < 0 || (size_t)written >= sizeof(question) - used)
        {
            break;
        }

        used += (size_t)written;
    }

    snprintf(question + used, sizeof(question) - used, "): ");

    /* the theme in use opens as the answer, so Enter alone loads it
       again and a name is only typed to change it */
    char *answer = EditorPrompt(question, ThemeCurrent());

    if (answer == NULL)
    {
        PromptCancelled("Theme unchanged");
        return;
    }

    if (ThemeSelect(answer) != 0)
    {
        SetStatusMessage("Theme not found: %s", answer);
        free(answer);
        return;
    }

    free(answer);

    /* every cell on screen may be drawn in another color now */
    ScreenMarkAllDirty();

    if (ThemeBadLines() > 0)
    {
        SetStatusMessage("Theme %s: %d line%s not understood", ThemeCurrent(),
                         ThemeBadLines(), ThemeBadLines() == 1 ? "" : "s");
    }
    else
    {
        SetStatusMessage("Theme: %s", ThemeCurrent());
    }
}

static void EditorSave(void)
{
    int named_here = 0;

    if (vted_buffer.filename == NULL)
    {
        /* an unnamed buffer asks where to go; the buffer takes the
           name only when the file could actually be written */
        char *name = EditorPromptPath("Save as: ", NULL);

        if (name == NULL)
        {
            PromptCancelled("Save cancelled");
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

static void EditorOpenFile(void)
{
    char *path = EditorPromptPath("Open: ", NULL);

    if (path == NULL)
    {
        PromptCancelled("Open cancelled");
        return;
    }

    if (BuffersOpen(path) == -1)
    {
        int saved_errno = errno;

        /* a failed syscall explains itself; a path that is not a
           regular file (a directory, say) fails without one */
        SetStatusMessage("Could not open: %s",
                         saved_errno != 0 ? strerror(saved_errno)
                                          : "not a regular file");
    }
    else
    {
        SetStatusMessage("Opened %s (buffer %d of %d)",
                         vted_buffer.filename, BuffersCurrent() + 1,
                         BuffersCount());
    }

    free(path);
}

/* the number a run of digits spells, one past its last digit coming
   back: it comes out saturated near a thousand million, the way a
   jump tail's digits do, so an absurd answer still lands somewhere
   instead of overflowing its way past every line the file has */
#define ANSWER_NUMBER_MAX 1000000000

static const char *AnswerNumber(const char *at, int *value)
{
    int number = 0;

    while (*at >= '0' && *at <= '9')
    {
        if (number <= (ANSWER_NUMBER_MAX - 9) / 10)
        {
            number = number * 10 + (*at - '0');
        }
        at++;
    }

    *value = number;
    return at;
}

/* Ctrl-G asks for a place to land: a line number, a line with a
   ":column" tail behind it the way a jump tail carries one, or the
   name of a file — opened the way the Alt-o keys open one, the
   tail it carries naming the spot the open lands on. The counts
   start at one the way the bar counts them, and an answer that
   spells none of them told so in the bar */
static void EditorGoToLine(void)
{
    char *answer = EditorPrompt("Go to line or file: ", NULL);

    if (answer == NULL)
    {
        return;
    }

    /* the line, and behind a colon the column when the answer
       carries one: digits, digits behind a colon, and nothing
       behind them */
    int line = 0;
    int column = 0;
    const char *at = AnswerNumber(answer, &line);

    if (at == answer)
    {
        /* no digits at the head: the answer names a file, the way
           the Alt-o keys take a name — beside the file being edited
           first, the tail behind it naming the spot — and the bar
           says what came of the open */
        int jump_line;
        int jump_column;
        int name_len = RowNameTail(answer, &jump_line, &jump_column);
        char message[PROMPT_MAX + 1]; /* sized to the notices an
                                         open that fails lands in */

        if (name_len == 0)
        {
            SetStatusMessage("Not a line number: %s", answer);
            free(answer);
            return;
        }

        answer[name_len] = '\0';
        EditorOpenNamed(answer, jump_line, jump_column,
                        message, sizeof(message));

        SetStatusMessage("%s", message);
        free(answer);
        return;
    }

    int ok = 1;

    if (*at == ':')
    {
        const char *past = AnswerNumber(at + 1, &column);

        ok = past != at + 1;
        at = past;
    }

    ok = ok && *at == '\0' && line >= 1;

    if (!ok)
    {
        SetStatusMessage("Not a line number: %s", answer);
        free(answer);
        return;
    }

    free(answer);

    EditorLandJump(line, column);
}

/* Alt-o opens the file the cursor sits on, the way gf does in vi: the
   run of path bytes under the cursor (or right behind it) names the
   file. The ":line:column" tail the name can carry, the way a
   compiler names a spot, says where in the file the open lands */
static void EditorOpenFileAtCursor(void)
{
    char name[PATH_MAX];
    char message[PATH_MAX + 64];
    int span_start;
    int span_end;
    int jump_line = 0;
    int jump_column = 0;

    if (vted_editor_config.cursor_y >= vted_buffer.num_rows)
    {
        SetStatusMessage("No file name under the cursor");
        return;
    }

    vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];

    RowPathSpan(row, vted_editor_config.cursor_x, &span_start, &span_end);

    /* no name under the cursor, or one too long to say: nothing to
       open */
    if (span_start == span_end || span_end - span_start >= (int)sizeof(name))
    {
        SetStatusMessage("No file name under the cursor");
        return;
    }

    memcpy(name, &row->chars[span_start], (size_t)(span_end - span_start));
    name[span_end - span_start] = '\0';

    /* the tail behind the name, when it carries one: the line and the
       column the open lands on */
    RowPathJump(row, span_end, &jump_line, &jump_column);

    /* the open, the message it leaves put in the bar */
    EditorOpenNamed(name, jump_line, jump_column,
                    message, sizeof(message));
    SetStatusMessage("%s", message);
}

/* steps through the open buffers in the direction the key says,
   wrapping around both ends, and says where the cursor landed */
static void EditorStepBuffer(int direction)
{
    int count = BuffersCount();

    if (count < 2)
    {
        SetStatusMessage("No other buffer");
        return;
    }

    int next = BuffersCurrent() + direction;

    if (next < 0)
    {
        next = count - 1; /* wrap around to the last one */
    }
    if (next >= count)
    {
        next = 0; /* and back to the first */
    }

    if (BuffersSwitch(next))
    {
        SetStatusMessage("%s (buffer %d of %d)",
                         vted_buffer.filename != NULL ? vted_buffer.filename
                                                      : "[No Name]",
                         next + 1, count);
    }
}

/* closes the buffer being edited; the guard against unsaved changes
   is answered by the caller, which counts the confirmations */
static void EditorCloseBuffer(void)
{
    char closed[64];

    /* the name is wanted after the close, when its buffer is already
       gone and the name with it */
    snprintf(closed, sizeof(closed), "%.60s",
             vted_buffer.filename != NULL ? vted_buffer.filename
                                          : "[No Name]");

    BuffersClose(BuffersCurrent());

    if (BuffersCount() == 1 && vted_buffer.filename == NULL)
    {
        SetStatusMessage("Closed %s; a new empty buffer", closed);
    }
    else
    {
        SetStatusMessage("Closed %s; now on %s (buffer %d of %d)",
                         closed,
                         vted_buffer.filename != NULL ? vted_buffer.filename
                                                      : "[No Name]",
                         BuffersCurrent() + 1, BuffersCount());
    }
}

/* the most lines the buffer picker shows, and the room one line of it
   takes: a number, a modified marker and a path */
#define PICKER_LINES 16
#define PICKER_WIDTH 96

/* the most words the completion menu offers, the most lines of them
   it shows, the room one of its words takes, and the room one of
   its lines takes: a word, and behind it the name of the buffer it
   was collected from */
#define COMPLETION_WORDS 64
#define COMPLETION_LINES 16
#define COMPLETION_WIDTH 64
#define COMPLETION_MENU_WIDTH 104

/* pressing Ctrl-Q, or Ctrl-E on a modified buffer, this many times in
   a row overrides unsaved changes */
#define QUIT_CONFIRMATIONS 3
#define CLOSE_CONFIRMATIONS 3

/* picks one of the open buffers from a list: the arrows and the wheel
   move through them, a left click takes the buffer drawn under it,
   Enter takes the one under the selection, Ctrl-E closes the one
   under it without leaving the list, and ESC puts the buffer that was
   live back */
static void EditorPickBuffer(void)
{
    char lines[PICKER_LINES][PICKER_WIDTH];
    const char *shown_lines[PICKER_LINES];
    int count = BuffersCount();
    int selection = BuffersCurrent();
    int started_at = selection;
    int close_confirmations = CLOSE_CONFIRMATIONS;
    int close_warning = 0; /* the presses a dirty buffer still asks for */
    int closed_count = 0;

    if (count < 2)
    {
        SetStatusMessage("No other buffer");
        return;
    }

    while (1)
    {
        /* a window of the list that follows the selection, so many
           open buffers stay reachable without a huge menu */
        int shown = count;
        int first;
        int i;

        if (shown > PICKER_LINES)
        {
            shown = PICKER_LINES;
        }
        if (shown > vted_editor_config.screen_rows - 2)
        {
            shown = vted_editor_config.screen_rows - 2;
        }
        if (shown < 1)
        {
            shown = 1;
        }

        first = selection - shown / 2;
        if (first < 0)
        {
            first = 0;
        }
        if (first > count - shown)
        {
            first = count - shown;
        }

        for (i = 0; i < shown; i++)
        {
            const char *name = BuffersName(first + i);

            snprintf(lines[i], sizeof(lines[i]), "%2d %s %.80s",
                     first + i + 1,
                     BuffersDirty(first + i) ? "+" : " ",
                     name != NULL ? name : "[No Name]");
            shown_lines[i] = lines[i];
        }

        ScreenShowMenu(shown_lines, shown, selection - first);

        /* the hint, or the warning a dirty buffer gave for its close */
        if (close_warning > 0)
        {
            SetStatusMessage("WARNING! Unsaved changes. Press Ctrl-E %d more times to close.",
                             close_warning);
        }
        else
        {
            SetStatusMessage("Switch buffer: arrows move, Enter or a click picks, ESC cancels, Ctrl-E closes");
        }

        RefreshScreen();

        int c = ReadKeyPress();

        EditorSuspendIfAsked(c);

        if (c == 0)
        {
            /* idle read timeout: just repaint the menu — unless a
               signal asked the editor to leave, which the list must
               not outwait */
            if (TerminalWantsExit())
            {
                /* the buffer that was live stays live, the way ESC
                   leaves it */
                selection = started_at >= 0 && started_at < count
                                ? started_at
                                : BuffersCurrent();
                break;
            }

            continue; /* idle read timeout: just repaint the menu */
        }

        if (c == 27) /* ESC: the buffer that was live stays live */
        {
            /* the one the picker started on may have been closed since
               (or slid to another slot): what is live now stays */
            selection = started_at >= 0 && started_at < count
                            ? started_at
                            : BuffersCurrent();
            break;
        }

        if (c == '\r' || c == '\n')
        {
            break;
        }

        if (c == CTRL_KEY('e') || c == CTRL_KEY('E'))
        {
            /* close the buffer under the selection without leaving
               the list: unsaved changes hold the close back the way
               they hold one back outside */
            if (BuffersDirty(selection) != 0 && close_confirmations > 0)
            {
                close_warning = close_confirmations;
                close_confirmations--;
                continue;
            }

            BuffersClose(selection);
            count = BuffersCount();
            closed_count++;

            /* the next buffer starts its close count over */
            close_confirmations = CLOSE_CONFIRMATIONS;
            close_warning = 0;

            /* the ESC anchor slides with the buffers, and one that was
               closed is simply gone */
            if (started_at == selection)
            {
                started_at = -1;
            }
            else if (started_at > selection)
            {
                started_at--;
            }

            if (selection >= count)
            {
                selection = count - 1; /* wrap to the last one */
            }

            if (count < 2)
            {
                break; /* nothing left to pick */
            }

            continue;
        }

        /* any other key restarts the close count: a warning left half
           way through is not a promise */
        close_warning = 0;
        close_confirmations = CLOSE_CONFIRMATIONS;

        /* Alt-1 to Alt-9 take their buffer straight away, the way they
           do outside the picker */
        if (c >= BufferSelect1 && c <= BufferSelect9)
        {
            int wanted = c - BufferSelect1 + 1;

            if (wanted <= count)
            {
                selection = wanted - 1;
                break;
            }
            continue;
        }

        /* a left click takes the buffer drawn under it, the way Enter
           takes the one under the selection; the middle and the right
           button arrive unbound, the way they do over the text, and a
           click past the list keeps it waiting */
        if (c == MousePress)
        {
            if (MouseLastButton() == 0)
            {
                int line = ScreenMenuLineAt(MouseLastY());

                if (line >= 0)
                {
                    selection = first + line;
                    break;
                }
            }

            continue;
        }

        /* a wheel notch steps the selection one buffer, the way an
           arrow does without its wrap: the wheel stops at the first
           and the last line, the way the scroll it makes over the
           text stops at both ends */
        if (c == MouseWheelUp || c == MouseWheelDown)
        {
            if (c == MouseWheelUp && selection > 0)
            {
                selection--;
            }

            if (c == MouseWheelDown && selection < count - 1)
            {
                selection++;
            }

            continue;
        }

        if (c == ArrowUp)
        {
            selection = selection > 0 ? selection - 1 : count - 1;
        }
        else if (c == ArrowDown)
        {
            selection = selection < count - 1 ? selection + 1 : 0;
        }
    }

    /* the lines were only borrowed, and the rows under the menu need
       their own paint back */
    ScreenShowMenu(NULL, 0, 0);

    BuffersSwitch(selection);

    if (closed_count > 0)
    {
        if (BuffersCount() == 1 && vted_buffer.filename == NULL)
        {
            SetStatusMessage("Closed %d buffer%s; a new empty buffer",
                             closed_count, closed_count == 1 ? "" : "s");
        }
        else
        {
            SetStatusMessage("Closed %d buffer%s; now on %s (buffer %d of %d)",
                             closed_count, closed_count == 1 ? "" : "s",
                             vted_buffer.filename != NULL ? vted_buffer.filename
                                                          : "[No Name]",
                             BuffersCurrent() + 1, BuffersCount());
        }
    }
    else
    {
        SetStatusMessage("%s (buffer %d of %d)",
                         vted_buffer.filename != NULL ? vted_buffer.filename
                                                      : "[No Name]",
                         BuffersCurrent() + 1, BuffersCount());
    }
}

/* completes the word before the cursor with the words of the open
   buffers, dabbrev style: the menu the buffer picker uses lists every
   distinct word that starts with it — the buffer being edited around
   the cursor, the nearest first, then the other buffers, each of
   whose words is followed by its buffer's name. A repeated Alt-/
   steps through the answers the way Emacs walks its own, and Enter
   or a click takes one. ESC leaves the word as it was typed */
static void EditorCompleteWord(void)
{
    char words[COMPLETION_WORDS][COMPLETION_WIDTH];
    char lines[COMPLETION_LINES][COMPLETION_MENU_WIDTH];
    const char *shown_lines[COMPLETION_LINES];
    int origins[COMPLETION_WORDS]; /* the buffer each word came from */
    char prefix[COMPLETION_WIDTH];
    int prefix_len = 0;
    int shown_len;
    int count;
    int live = BuffersCurrent();
    int selection = 0;
    int chosen = -1;
    int i;
    int j;

    count = BufferCollectCompletions(vted_editor_config.cursor_y,
                                     vted_editor_config.cursor_x,
                                     &words[0][0], COMPLETION_WORDS,
                                     COMPLETION_WIDTH, &prefix_len);

    /* every word collected so far came from the buffer being edited,
       the only one scanned yet */
    for (j = 0; j < count; j++)
    {
        origins[j] = live;
    }

    if (prefix_len == 0)
    {
        SetStatusMessage("No word to complete");
        return;
    }

    /* the word being completed, for the hint, the answers and the
       other buffers' scan: a word too long for a line of the menu
       has no completion anyway, so the copy keeps what fits */
    shown_len = prefix_len < COMPLETION_WIDTH ? prefix_len
                                              : COMPLETION_WIDTH - 1;
    {
        const vted_row_t *row = &vted_buffer.rows[vted_editor_config.cursor_y];
        int from = vted_editor_config.cursor_x - prefix_len;

        memcpy(prefix, &row->chars[from], (size_t)shown_len);
        prefix[shown_len] = '\0';
    }

    /* dabbrev goes on past the buffer being edited, once it has had
       its say: the other open buffers follow, the next one first
       around the ring, filling the lines the menu has left, each
       read from its first row to its last, and every word one of
       them gave keeps the buffer it came from, for the menu to
       say it */
    for (i = 1; count < COMPLETION_WORDS && i < BuffersCount(); i++)
    {
        int at = (live + i) % BuffersCount();
        const vted_buffer_t *text = BuffersText(at);
        int before = count;

        if (text == NULL)
        {
            continue; /* the live one, scanned above already */
        }

        count = BufferCollectTextWords(text, prefix, shown_len,
                                       &words[0][0], COMPLETION_WORDS,
                                       COMPLETION_WIDTH, count);

        for (j = before; j < count; j++)
        {
            origins[j] = at;
        }
    }

    if (count == 0)
    {
        SetStatusMessage("No completion for '%s'", prefix);
        return;
    }

    while (1)
    {
        /* a window of the words that follows the selection, the way
           the buffer picker's list does */
        int shown = count;
        int first;

        if (shown > COMPLETION_LINES)
        {
            shown = COMPLETION_LINES;
        }
        if (shown > vted_editor_config.screen_rows - 2)
        {
            shown = vted_editor_config.screen_rows - 2;
        }
        if (shown < 1)
        {
            shown = 1;
        }

        first = selection - shown / 2;
        if (first < 0)
        {
            first = 0;
        }
        if (first > count - shown)
        {
            first = count - shown;
        }

        for (i = 0; i < shown; i++)
        {
            int word = first + i;

            /* a word another buffer gave is followed by its name, so
               the menu says where it came from; the words of the
               buffer being edited stand bare, their source being
               where the cursor already is */
            if (origins[word] == live)
            {
                shown_lines[i] = words[word];
            }
            else
            {
                const char *name = BuffersName(origins[word]);

                snprintf(lines[i], sizeof(lines[i]), "%s - %.32s",
                         words[word], name != NULL ? name : "[No Name]");
                shown_lines[i] = lines[i];
            }
        }

        ScreenShowMenu(shown_lines, shown, selection - first);

        SetStatusMessage("Complete '%s': Alt-/ steps, arrows move, Enter or a click takes, ESC cancels",
                         prefix);

        RefreshScreen();

        int c = ReadKeyPress();

        EditorSuspendIfAsked(c);

        if (c == 0)
        {
            /* idle read timeout: just repaint the menu — unless a
               signal asked the editor to leave, which the menu must
               not outwait */
            if (TerminalWantsExit())
            {
                break;
            }

            continue;
        }

        if (c == 27) /* ESC: the word stays as it was typed */
        {
            break;
        }

        if (c == '\r' || c == '\n')
        {
            chosen = selection;
            break;
        }

        /* a left click takes the word drawn under it, the way Enter
           takes the one under the selection; a click past the list
           keeps it waiting */
        if (c == MousePress)
        {
            if (MouseLastButton() == 0)
            {
                int line = ScreenMenuLineAt(MouseLastY());

                if (line >= 0)
                {
                    chosen = first + line;
                    break;
                }
            }

            continue;
        }

        /* a wheel notch steps the selection a word, the way an arrow
           does without its wrap */
        if (c == MouseWheelUp || c == MouseWheelDown)
        {
            if (c == MouseWheelUp && selection > 0)
            {
                selection--;
            }

            if (c == MouseWheelDown && selection < count - 1)
            {
                selection++;
            }

            continue;
        }

        /* a repeated Alt-/ steps the selection down and wraps
           around, the way a repeated dabbrev-expand in Emacs walks its
           own answers: the key that opened the menu keeps it moving */
        if (c == WordComplete)
        {
            selection = selection < count - 1 ? selection + 1 : 0;
            continue;
        }

        if (c == ArrowUp)
        {
            selection = selection > 0 ? selection - 1 : count - 1;
        }
        else if (c == ArrowDown)
        {
            selection = selection < count - 1 ? selection + 1 : 0;
        }
    }

    /* the lines were only borrowed, and the rows under the menu need
       their own paint back */
    ScreenShowMenu(NULL, 0, 0);

    if (chosen < 0)
    {
        SetStatusMessage("");
        return;
    }

    EditorInsertCompletion(words[chosen], (int)strlen(words[chosen]),
                           prefix_len);

    SetStatusMessage("Completed '%s' as '%s'", prefix, words[chosen]);
}

/* the match light a repeated search left on: it stays until the next
   key that is not one of the search's own turns it off, the way the
   leaving of the search itself does */
static int search_highlight_on = 0;

/* tells the screen what the search looks for and how it reads it:
   the query itself for the literal mode, and the way the mode stands
   either way, so a pattern has its matches drawn as long as the
   pattern itself finds them */
static void ShowSearchOnScreen(const char *query, int len)
{
    ScreenSetSearchRegex(search_regex_mode);
    ScreenSetSearchQuery(query, len);
}

/* takes the light off every match and drops the inverted one: the
   rows it stood on repaint without it */
static void ClearSearchHighlight(void)
{
    if (vted_editor_config.match_row >= 0)
    {
        ScreenMarkRowDirty(vted_editor_config.match_row);
    }

    vted_editor_config.match_row = -1;
    vted_editor_config.match_len = 0;

    ShowSearchOnScreen(NULL, 0);
    search_highlight_on = 0;
}

/* counts the matches of the pattern in the buffer, and how many of
   them sit before the cursor: the same bookkeeping the literal count
   keeps in the row module, walked through the regex one instead. The
   walk steps to the next character after every match it counts, so
   the same match is never counted twice */
static void CountMatchesRegex(int row, int col, int *total, int *before)
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
            int len = 0;
            int hit = RegexFindNext(scan_row->chars, at, scan_row->size,
                                    &len, NULL);

            if (hit < 0)
            {
                break;
            }

            if (i < row || (i == row && hit < col))
            {
                before_count++;
            }

            total_count++;
            at = Utf8NextBoundary(scan_row->chars, scan_row->size, hit);
        }
    }

    *total = total_count;
    *before = before_count;
}

/* one row of the search: the first match of the query in the window
   [from, to) going forward, the last one before `to` going backward.
   Returns the byte the match starts at, -1 when the row holds none,
   and its length in bytes through *hit_len — the length of the query
   itself while the search runs on its literal path */
static int FindInRow(const vted_row_t *row, int from, int to,
                     int direction, const char *query, int query_len,
                     int *hit_len)
{
    if (search_regex_mode)
    {
        /* a pattern that stands compiled answers for the row; a bad
           one answers nothing, the way an empty query does */
        if (!RegexHasPattern())
        {
            return -1;
        }

        return direction > 0
                   ? RegexFindNext(row->chars, from, to, hit_len, NULL)
                   : RegexFindPrev(row->chars, from, to, hit_len, NULL);
    }

    *hit_len = query_len;

    return direction > 0
               ? RowFindNext(row, from, to, query, query_len)
               : RowFindPrev(row, from, to, query, query_len);
}

static int FindMatch(const char *query, int start_row, int start_col,
                     int direction, int *match_row, int *match_col,
                     int *match_len)
{
    /* forward finds the first match at or after (start_row, start_col),
       backward the last one before it; both scan the whole buffer row
       by row and wrap around, so every match is reachable */
    int row_count = vted_buffer.num_rows;
    int query_len = (int)strlen(query);
    int row;
    int hit;
    int hit_len = 0;

    if (row_count == 0 || query_len == 0)
    {
        return -1;
    }

    /* the cursor may rest on the phantom row past the end */
    if (start_row >= row_count)
    {
        start_row = row_count - 1;
    }

    if (direction > 0)
    {
        hit = FindInRow(&vted_buffer.rows[start_row], start_col,
                        vted_buffer.rows[start_row].size, direction, query,
                        query_len, &hit_len);
        if (hit >= 0)
        {
            *match_row = start_row;
            *match_col = hit;
            *match_len = hit_len;
            return 0;
        }

        for (row = start_row + 1; row < row_count; row++)
        {
            hit = FindInRow(&vted_buffer.rows[row], 0,
                            vted_buffer.rows[row].size, direction, query,
                            query_len, &hit_len);
            if (hit >= 0)
            {
                *match_row = row;
                *match_col = hit;
                *match_len = hit_len;
                return 0;
            }
        }

        for (row = 0; row < start_row; row++)
        {
            hit = FindInRow(&vted_buffer.rows[row], 0,
                            vted_buffer.rows[row].size, direction, query,
                            query_len, &hit_len);
            if (hit >= 0)
            {
                *match_row = row;
                *match_col = hit;
                *match_len = hit_len;
                return 0;
            }
        }

        /* the head of the starting row: a match before the one the
           search started from */
        hit = FindInRow(&vted_buffer.rows[start_row], 0, start_col,
                        direction, query, query_len, &hit_len);
        if (hit >= 0)
        {
            *match_row = start_row;
            *match_col = hit;
            *match_len = hit_len;
            return 0;
        }
    }
    else
    {
        hit = FindInRow(&vted_buffer.rows[start_row], 0, start_col,
                        direction, query, query_len, &hit_len);
        if (hit >= 0)
        {
            *match_row = start_row;
            *match_col = hit;
            *match_len = hit_len;
            return 0;
        }

        for (row = start_row - 1; row >= 0; row--)
        {
            hit = FindInRow(&vted_buffer.rows[row], 0,
                            vted_buffer.rows[row].size, direction, query,
                            query_len, &hit_len);
            if (hit >= 0)
            {
                *match_row = row;
                *match_col = hit;
                *match_len = hit_len;
                return 0;
            }
        }

        for (row = row_count - 1; row > start_row; row--)
        {
            hit = FindInRow(&vted_buffer.rows[row], 0,
                            vted_buffer.rows[row].size, direction, query,
                            query_len, &hit_len);
            if (hit >= 0)
            {
                *match_row = row;
                *match_col = hit;
                *match_len = hit_len;
                return 0;
            }
        }
    }

    return -1;
}

/* runs the search as it stands and lands the cursor on its match:
   a jump with the arrows steps from the match the cursor sits on,
   so every match on a line is reachable; a changed query searches
   afresh from where the search started, so it refines the match in
   place. Returns the row of the match it left the cursor on, -1
   when the query has none */
static int SearchStep(const char *query, int direction,
                      int last_match_row, int saved_y, int saved_x)
{
    int search_row;
    int search_col;
    int match_row;
    int match_col;
    int match_len = 0;

    if (last_match_row >= 0)
    {
        search_row = vted_editor_config.cursor_y;
        search_col = vted_editor_config.cursor_x + (direction > 0 ? 1 : 0);
    }
    else
    {
        search_row = saved_y;
        search_col = direction > 0 ? 0 : saved_x;
    }

    if (FindMatch(query, search_row, search_col, direction,
                  &match_row, &match_col, &match_len) == 0)
    {
        /* drop the old highlight before the new one is painted */
        if (vted_editor_config.match_row >= 0)
        {
            ScreenMarkRowDirty(vted_editor_config.match_row);
        }

        vted_editor_config.cursor_y = match_row;
        vted_editor_config.cursor_x = match_col;

        /* a match that left the viewport comes back mid-viewport,
           the wrapped rows around it counted at their real
           heights, the way Ctrl-G lands a line; one still on
           screen keeps the viewport where it is */
        ScreenCenterCursor();

        /* highlight the match on screen */
        vted_editor_config.match_row = match_row;
        vted_editor_config.match_col = match_col;
        vted_editor_config.match_len = match_len;

        ScreenMarkRowDirty(match_row);
        return match_row;
    }

    /* no match for the current query: drop the highlight */
    if (vted_editor_config.match_row >= 0)
    {
        ScreenMarkRowDirty(vted_editor_config.match_row);
    }

    vted_editor_config.match_row = -1;
    vted_editor_config.match_len = 0;
    return -1;
}

static void EditorFind(void)
{
    char query[64];
    int query_len = 0;
    int direction = 1; /* 1 forward, -1 backward */
    int last_match_row = -1;
    int query_changed = 0;
    int pattern_ok = 1; /* the pattern of the regex mode stands */
    char notice[PROMPT_MAX + 1]; /* what the last Alt-o said, standing
                                    until a real key answers it the
                                    way a prompt's does */
    int opened = 0; /* Alt-o opened the query's file: the bar keeps
                       the open's own message */

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
    notice[0] = '\0';

    /* the search opens with the last query, so Enter alone keeps its
       match and the arrows step through its matches without a single
       keystroke; backspace takes it apart the way a typed one */
    if (last_query[0] != '\0')
    {
        query_len = CopyQuery(query, last_query);

        /* a remembered pattern is compiled before the first step runs
           on it, so the search opens the way it left */
        if (search_regex_mode)
        {
            pattern_ok = RegexCompile(query) == 0;
        }

        /* a remembered query searches at once, so its matches light
           up the way they do while it is being typed */
        ShowSearchOnScreen(query, query_len);

        last_match_row = SearchStep(query, direction,
                                    last_match_row, saved_y, saved_x);
    }

    /* where the up and down arrows stand in the history: the search
       opens on the last query, which stands as the newest entry when
       it is one, so the first up walks to the query before it the
       way vim's own / prompt does. The query as it stands is the
       working line the down walk comes back to */
    int history_pos = search_history_count;
    char working[64];

    snprintf(working, sizeof(working), "%s", query);

    if (search_history_count > 0 &&
        strcmp(working, search_history[search_history_count - 1]) == 0)
    {
        history_pos = search_history_count - 1;
    }

    while (1)
    {
        /* the prompt says where the match the cursor sits on stands
           among all of them, so typing tells at a glance whether the
           query is any good before a single arrow is pressed — unless
           a notice stands, saying what the last Alt-o left */
        if (notice[0] != '\0')
        {
            SetStatusMessage("Find%s: %s — %s",
                             search_regex_mode ? " (regex)" : "",
                             query, notice);
        }
        else if (query_len > 0 && search_regex_mode && !pattern_ok)
        {
            SetStatusMessage("Find (regex): %s — %s", query, RegexError());
        }
        else if (query_len > 0)
        {
            int total;
            int before;

            if (search_regex_mode)
            {
                CountMatchesRegex(vted_editor_config.cursor_y,
                                  vted_editor_config.cursor_x,
                                  &total, &before);
            }
            else
            {
                BufferCountMatches(query, query_len,
                                   vted_editor_config.cursor_y,
                                   vted_editor_config.cursor_x,
                                   &total, &before);
            }

            if (total > 0)
            {
                SetStatusMessage("Find%s: %s (%d of %d, Ctrl-T %s, ESC cancel, Enter keep, ←/→ next/prev, ↑/↓ history)",
                                 search_regex_mode ? " (regex)" : "", query,
                                 before + 1, total,
                                 search_regex_mode ? "text" : "regex");
            }
            else
            {
                SetStatusMessage("Find%s: %s (no matches, Ctrl-T %s, ESC cancel, Enter keep, ←/→ next/prev, ↑/↓ history)",
                                 search_regex_mode ? " (regex)" : "", query,
                                 search_regex_mode ? "text" : "regex");
            }
        }
        else
        {
            SetStatusMessage("Find%s: %s (Ctrl-T %s, ESC cancel, Enter keep, ←/→ next/prev, ↑/↓ history)",
                             search_regex_mode ? " (regex)" : "", query,
                             search_regex_mode ? "text" : "regex");
        }
        RefreshScreen();

        int c = ReadKeyPress();

        EditorSuspendIfAsked(c);

        if (c == 0)
        {
            /* idle read timeout: repaint the prompt, do not touch
               the search state or the cursor would drift forward —
               unless a signal asked the editor to leave, which the
               search must not outwait */
            if (TerminalWantsExit())
            {
                break;
            }

            continue;
        }

        /* a real key answers the notice the last Alt-o left */
        notice[0] = '\0';

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

        if (c == OpenFileAtCursor)
        {
            /* Alt-o opens the file the query names, the way it does
               at the cursor: the query as it stands, its tail naming
               the spot, the same beside-first standing it takes
               there. A name that opens no file says so in the notice
               while the search stands */
            int jump_line;
            int jump_column;
            int name_len = RowNameTail(query, &jump_line, &jump_column);
            char message[PROMPT_MAX + 1]; /* sized to the notice it
                                             lands in when no file
                                             came open */
            char beyond;
            int came_open;

            if (name_len == 0)
            {
                snprintf(notice, sizeof(notice),
                         "No file name in the query");
                continue;
            }

            beyond = query[name_len];
            query[name_len] = '\0';

            came_open = EditorOpenNamed(query, jump_line, jump_column,
                                        message, sizeof(message));

            query[name_len] = beyond; /* the tail goes back on */

            if (came_open)
            {
                /* the open ends the search, the bar saying where it
                   landed */
                SetStatusMessage("%s", message);
                opened = 1;
                break;
            }

            snprintf(notice, sizeof(notice), "%s", message);
            continue;
        }

        if (c == ArrowRight)
        {
            direction = 1;
        }
        else if (c == ArrowLeft)
        {
            direction = -1;
        }
        else if (c == ArrowUp || c == ArrowDown)
        {
            /* the up and down arrows walk the queries the searches
               have settled on, the way a shell walks the commands it
               was given — up to the older ones, down back to the
               newer, and past the newest to the query the walk began
               on. A recalled query searches at once, the way the one
               the search opens with does */
            int moved = 0;

            if (c == ArrowUp && history_pos > 0)
            {
                if (history_pos == search_history_count)
                {
                    /* leaving the working line: the query as it
                       stands is kept for the way back */
                    snprintf(working, sizeof(working), "%s", query);
                }

                history_pos--;
                moved = 1;
            }
            else if (c == ArrowDown && history_pos < search_history_count)
            {
                history_pos++;
                moved = 1;
            }

            if (moved)
            {
                query_len = CopyQuery(
                    query, history_pos < search_history_count
                               ? search_history[history_pos]
                               : working);
                last_match_row = -1; /* the recalled query searches afresh */
                query_changed = 1;
            }
        }
        else if (c == 127) /* backspace edits the query */
        {
            if (query_len > 0)
            {
                query[--query_len] = '\0';
                last_match_row = -1; /* restart from the cursor */
                query_changed = 1;
            }
        }
        else if (c == 20) /* Ctrl-T reads the query the other way */
        {
            search_regex_mode = !search_regex_mode;
            last_match_row = -1; /* the other reading searches afresh */
            query_changed = 1;
        }
        else if (c >= 32 && c <= 126 && query_len < 63)
        {
            query[query_len++] = (char)c;
            query[query_len] = '\0';
            last_match_row = -1; /* restart from the cursor */
            query_changed = 1;
        }

        /* the query changed: every match of it lights up from here
           on, the one the cursor is on inverted as before; in the
           regex mode the pattern is compiled the way it stands, and
           a bad one leaves the cursor where it is until it is fixed */
        if (query_changed)
        {
            if (search_regex_mode)
            {
                pattern_ok = query_len == 0 || RegexCompile(query) == 0;
            }

            ShowSearchOnScreen(query, query_len);
            query_changed = 0;
        }

        /* (re)run the search: a jump with the arrows steps from the
           match the cursor sits on, so every match on a line is
           reachable; a changed query searches afresh from where the
           search started, so it refines the match in place */
        last_match_row = SearchStep(query, direction,
                                    last_match_row, saved_y, saved_x);
    }

    /* the query stays for the next prompt, whichever way this one
       ended: a cancelled search is as worth repeating as a kept one,
       and it goes into the history the arrows walk */
    if (query_len > 0)
    {
        RememberAnswer(last_query, query);
        SearchHistoryPush(query);
    }

    /* leaving the search clears the highlight */
    ClearSearchHighlight();

    /* the bar goes quiet — unless the Alt-o that ended the search
       left the open's own message standing */
    if (!opened)
    {
        SetStatusMessage("");
    }
}

/* Alt-n and Alt-p step the cursor to the match after and before the
   one it sits on, the way n and N do in vi: the last search's query
   runs again from where the cursor stands, wrapping around the
   buffer, and every match of it lights up until the next key that is
   not one of the search's own turns the light off */
static void EditorSearchRepeat(int direction)
{
    int query_len = (int)strlen(last_query);
    int search_row = vted_editor_config.cursor_y;
    int search_col = vted_editor_config.cursor_x;
    int match_row;
    int match_col;
    int match_len = 0;

    if (query_len == 0)
    {
        SetStatusMessage("No search to repeat");
        return;
    }

    /* the pattern of the regex mode is asked for again before the
       step runs on it: it stands from the search that left it, but
       the case setting may have turned since */
    if (search_regex_mode && RegexCompile(last_query) != 0)
    {
        ClearSearchHighlight();
        SetStatusMessage("Bad pattern: %s — %s", last_query, RegexError());
        return;
    }

    /* the cursor may rest on the phantom row past the end */
    if (search_row >= vted_buffer.num_rows)
    {
        search_row = vted_buffer.num_rows - 1;
    }

    /* a match under the cursor is not the one the step is looking
       for: the forward step starts past it, the backward one before */
    search_col += direction > 0 ? 1 : 0;

    if (FindMatch(last_query, search_row, search_col, direction,
                  &match_row, &match_col, &match_len) != 0)
    {
        ClearSearchHighlight();
        SetStatusMessage("No matches: %s", last_query);
        return;
    }

    /* the match that was lit repaints without it, and the one the
       cursor lands on is inverted, the way the search leaves it */
    if (vted_editor_config.match_row >= 0)
    {
        ScreenMarkRowDirty(vted_editor_config.match_row);
    }

    vted_editor_config.cursor_y = match_row;
    vted_editor_config.cursor_x = match_col;

    /* a match that left the viewport comes back mid-viewport, the
       way a step inside the search lands one */
    ScreenCenterCursor();

    vted_editor_config.match_row = match_row;
    vted_editor_config.match_col = match_col;
    vted_editor_config.match_len = match_len;
    ScreenMarkRowDirty(match_row);

    /* the matches of the query keep their light, the text as it is
       or the places the pattern finds */
    ShowSearchOnScreen(last_query, query_len);

    search_highlight_on = 1;

    SetStatusMessage("Find%s: %s", search_regex_mode ? " (regex)" : "",
                     last_query);
}

/* the label the replace prompt carries with the mode turned over:
   the replacement line takes no pattern, so only the question the
   pattern is typed into changes its words */
static const char *ReplacePromptLabel(void)
{
    search_regex_mode = !search_regex_mode;

    return search_regex_mode ? "Replace (regex): " : "Replace: ";
}

static void EditorReplace(void)
{
    /* search and replace, one match at a time: the walk asks about
       each of them and every replacement is one undoable step, so the
       whole operation can be walked back with Ctrl-Z, one occurrence
       at a time */
    EditClearSelection();
    ScreenSetSelection(-1, 0, -1, 0);

    /* Ctrl-T turns the reading of the query over while it is asked
       for, the way it does inside the search */
    const char *query_label =
        search_regex_mode ? "Replace (regex): " : "Replace: ";

    PromptModeToggle = ReplacePromptLabel;

    char *query = EditorPrompt(query_label, last_query);

    PromptModeToggle = NULL;

    if (query == NULL)
    {
        PromptCancelled("Replace cancelled");
        return;
    }

    /* the query is settled even while the answer is still open: a
       replace cancelled at the answer keeps the query for the next
       one, the way a cancelled search keeps its own, and it goes
       into the history the search's arrows walk */
    RememberAnswer(last_query, query);
    SearchHistoryPush(query);

    char *replacement = EditorPrompt("with: ", last_replacement);

    if (replacement == NULL)
    {
        free(query);
        PromptCancelled("Replace cancelled");
        return;
    }

    RememberAnswer(last_replacement, replacement);

    /* the pattern is compiled the way the query stands before the
       walk runs on it: a bad one turns the whole replace away,
       the text left as it was */
    if (search_regex_mode && RegexCompile(query) != 0)
    {
        SetStatusMessage("Bad pattern: %s — %s", query, RegexError());
        free(query);
        free(replacement);
        return;
    }

    /* the walk asks about one match at a time, starting at the cursor
       and wrapping around the buffer the way the search does: y takes
       it, n leaves it, a takes it and every one after it without
       asking again and without stepping — the rest go in one pass,
       the end of the walk painted once — q or ESC stops with what
       is done. Every match keeps its light while the walk runs, the
       one being asked about inverted, and the numbering of the bar
       counts every place a match starts, the way the search prompt
       counts them */
    int query_len = (int)strlen(query);
    int total = 0;
    int ignored_before = 0;
    int replaced = 0;
    int seen = 0;    /* the matches asked about or taken so far */
    int asking = 1;  /* a drops it for the rest of the walk */
    int instant = 0; /* a also drops the frame per match: the rest go
                        in one pass, the replace-all of it all */
    int stopped = 0;
    int pos_row = vted_editor_config.cursor_y;
    int pos_col = vted_editor_config.cursor_x;

    if (query_len == 0)
    {
        free(query);
        free(replacement);
        SetStatusMessage("Replaced 0 occurrences");
        return;
    }

    if (search_regex_mode)
    {
        CountMatchesRegex(pos_row, pos_col, &total, &ignored_before);
    }
    else
    {
        BufferCountMatches(query, query_len, pos_row, pos_col,
                           &total, &ignored_before);
    }

    if (total == 0)
    {
        SetStatusMessage("No matches: %s", query);
        free(query);
        free(replacement);
        return;
    }

    ShowSearchOnScreen(query, query_len);

    while (!stopped && seen < total)
    {
        int m_row;
        int m_col;
        int m_len = 0;
        int take;
        int written;

        /* the next match from where the walk stands; a replacement
           that took the rest of them away ends the walk here */
        if (FindMatch(query, pos_row, pos_col, 1,
                      &m_row, &m_col, &m_len) != 0)
        {
            break;
        }

        seen++;

        if (instant)
        {
            /* the a answer turned the walk into a replace-all: no
               frame per match. The cursor tracks the walk all the
               same, so it ends where the walk would have, and the
               row that takes a replacement is marked below for the
               one repaint that shows them all */
            vted_editor_config.cursor_y = m_row;
            vted_editor_config.cursor_x = m_col;
        }
        else
        {
            /* drop the old light before the new one is painted */
            if (vted_editor_config.match_row >= 0)
            {
                ScreenMarkRowDirty(vted_editor_config.match_row);
            }

            vted_editor_config.cursor_y = m_row;
            vted_editor_config.cursor_x = m_col;

            /* a match that left the viewport comes back mid-viewport,
               the way a step of the search lands one */
            ScreenCenterCursor();

            vted_editor_config.match_row = m_row;
            vted_editor_config.match_col = m_col;
            vted_editor_config.match_len = m_len;
            ScreenMarkRowDirty(m_row);

            SetStatusMessage("Replace%s: %s (%d of %d) — y/n/a/q",
                             search_regex_mode ? " (regex)" : "", query,
                             seen, total);
            RefreshScreen();
        }

        /* y takes it, n leaves it, a takes it and the rest in one
           pass, q or ESC stops; anything else is ignored */
        take = 0;
        written = -1;

        if (asking)
        {
            int answered = 0;

            while (!answered)
            {
                int c = ReadKeyPress();

                EditorSuspendIfAsked(c);

                if (c == 0)
                {
                    /* idle read timeout: repaint, do not touch the
                       walk — unless a signal asked the editor to
                       leave, which the walk must not outwait */
                    if (TerminalWantsExit())
                    {
                        stopped = 1;
                        answered = 1;
                    }

                    continue;
                }

                if (c == 27 || c == 'q' || c == 'Q')
                {
                    stopped = 1;
                    answered = 1;
                }
                else if (c == 'y' || c == 'Y')
                {
                    take = 1;
                    answered = 1;
                }
                else if (c == 'n' || c == 'N')
                {
                    answered = 1;
                }
                else if (c == 'a' || c == 'A')
                {
                    asking = 0;
                    instant = 1;
                    take = 1;
                    answered = 1;
                }
            }
        }
        else
        {
            take = 1;
        }

        if (stopped)
        {
            break;
        }

        if (take)
        {
            written = search_regex_mode
                ? BufferReplaceSpanRegex(m_row, m_col, m_len, replacement)
                : BufferReplaceSpan(m_row, m_col, m_len, replacement);

            if (written >= 0)
            {
                replaced++;

                /* the row the text went into repaints with it: the
                   walk paints it in the next frame, the instant pass
                   holds it for the one at the end */
                ScreenMarkRowDirty(m_row);

                /* the walk goes on past what went in, the way the
                   replace-all continues after the new text */
                pos_row = m_row;
                pos_col = m_col + (written > 0 ? written : m_len);
            }
        }

        if (!take || written < 0)
        {
            /* a match left behind is stepped past the way the counting
               counts it: the next one may share its bytes, and every
               place a match starts gets its own ask */
            vted_row_t *row = &vted_buffer.rows[m_row];

            pos_row = m_row;
            pos_col = Utf8NextBoundary(row->chars, row->size, m_col);
        }

        /* the walk never starts past the end of the row it is on */
        {
            int row_size = vted_buffer.rows[pos_row].size;

            if (pos_col > row_size)
            {
                pos_col = row_size;
            }
        }
    }

    /* the instant pass ends where the walk would have: the viewport
       around the last of them, the whole of it painted in the one
       frame the caller draws */
    if (instant)
    {
        ScreenCenterCursor();
    }

    /* leaving the walk clears the light */
    ClearSearchHighlight();

    free(query);
    free(replacement);

    SetStatusMessage("Replaced %d occurrence%s", replaced,
                     replaced == 1 ? "" : "s");
}

/* gives back everything the editor holds: the undo history, the
   colors of the syntax module, the scratch space of the screen
   module, the buffers waiting in their slots, and the live text
   buffer itself. It runs once, on the way out, whichever way the
   editor leaves */
static void FreeEditorState(void)
{
    UndoReset();
    SyntaxFree();
    ScreenFree();
    BuffersFreeOthers();
    RegexForget();
    EditFreeKeptCopy();
    FreeBuffer();
}

int main(int argc, char *argv[])
{
    int quit_confirmations = QUIT_CONFIRMATIONS;
    int close_confirmations = CLOSE_CONFIRMATIONS;
    vted_cli_options_t options;

    /* the command line is settled before the terminal is touched, so
       a bad argument is answered on stderr with a normal exit instead
       of the editor dying from inside raw mode */
    CliParse(argc, argv, &options);

    if (options.error)
    {
        return 1;
    }

    /* the usage text is printed on the standard output stream the
       normal way, before any terminal mode is entered */
    if (options.want_help)
    {
        CliPrintUsage(stdout);
        return 0;
    }

    /* so is the version; when both are asked for, the usage wins */
    if (options.want_version)
    {
        CliPrintVersion(stdout);
        return 0;
    }

    /* the editor draws its whole interface on the standard output; a
       pipe or a file there would only collect escape sequences. The
       usage and the version above stay printable into a pipe */
    if (!isatty(STDOUT_FILENO))
    {
        fprintf(stderr, "vted: standard output is not a terminal\n");
        fputs("Try 'vted --help' for more information.\n", stderr);
        return 1;
    }

    /* "-" as an operand reads the text from standard input; a terminal
       there has no text coming, so it is refused instead of hung on.
       The tty fallback in the terminal module keeps the keys flowing
       from /dev/tty while the pipe is drained */
    int read_stdin = 0;
    int i;

    for (i = 0; i < options.file_count; i++)
    {
        if (strcmp(options.files[i], "-") == 0)
        {
            read_stdin = 1;
        }
    }

    if (read_stdin && isatty(STDIN_FILENO))
    {
        fprintf(stderr, "vted: -: standard input is a terminal\n");
        fputs("Try 'vted --help' for more information.\n", stderr);
        return 1;
    }

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();
    SetupExitSignals();
    SetupResizeSignal();
    SetupSuspendSignal();
    EnterToAlternateScreenBuffer();
    EnableBracketedPaste();
    EnableMouseTracking();
    InitBuffer();

    /* registered right after the buffer exists, so the state is
       released even when a fatal error ends the editor early */
    atexit(FreeEditorState);

    /* every file named on the command line becomes a buffer of its own */
    for (i = 0; i < options.file_count; i++)
    {
        if (strcmp(options.files[i], "-") == 0)
        {
            /* the pipe is read before the terminal goes raw: nothing
               of the interface depends on it, and a huge pipe is
               drained in one go the way a file is */
            if (BuffersOpenStream(stdin) == -1)
            {
                KillApp("ReadStream");
            }
        }
        else if (BuffersOpen(options.files[i]) == -1)
        {
            KillApp("OpenFile");
        }
    }

    if (options.file_count == 0)
    {
        /* no file named: an unnamed empty buffer to type into, taking
           a slot like any other so opening and switching treat it the
           same */
        BuffersStartEmpty();
    }
    else if (BuffersCount() > 1)
    {
        /* with several files the editor starts in the first one, the
           way vim and less take a list; a +LINE lands on its line */
        BuffersSwitch(0);
    }

    InitVtedEditor();

    /* the settings from the rc file arrive after the defaults and
       before the first frame is drawn */
    EditorLoadSettings();

    /* a +LINE on the command line lands the cursor on that line the
       way less and vi take one; a number past the end lands on the
       last line, and an empty buffer ignores it */
    if (options.start_line > 0 && vted_buffer.num_rows > 0)
    {
        int line = options.start_line;

        if (line > vted_buffer.num_rows)
        {
            line = vted_buffer.num_rows;
        }

        vted_editor_config.cursor_y = line - 1;
        vted_editor_config.cursor_x = 0;
        ScreenCenterCursor();
    }

    /* the cursor the file starts on may already sit on a bracket */
    UpdateBracketMatch();

    while (1)
    {
        int c_in = ReadKeyPress();

        EditorSuspendIfAsked(c_in);

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
            /* every buffer is checked, not just the live one: leaving
               loses them all, so unsaved changes anywhere hold the
               quit back */
            int dirty_buffers = BuffersDirtyCount();

            if (dirty_buffers > 0 && quit_confirmations > 0)
            {
                if (dirty_buffers == 1)
                {
                    SetStatusMessage("WARNING! Unsaved changes. Press Ctrl-Q %d more times to quit.",
                                     quit_confirmations);
                }
                else
                {
                    SetStatusMessage("WARNING! Unsaved changes in %d buffers. Press Ctrl-Q %d more times to quit.",
                                     dirty_buffers, quit_confirmations);
                }
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
            /* any key but the two confirmation keys restarts both
               counts: a warning left half way through is not a promise */
            if (c_in != CTRL_KEY('e') && c_in != CTRL_KEY('E'))
            {
                quit_confirmations = QUIT_CONFIRMATIONS;
                close_confirmations = CLOSE_CONFIRMATIONS;
            }

            /* only a run of vertical moves keeps aiming for the same
               display column; every other key forgets it */
            if (c_in != ArrowUp && c_in != ArrowDown && c_in != PageUp && c_in != PageDown)
            {
                vted_editor_config.preferred_rx = -1;
            }

            /* the light a repeated search left on the matches goes out
               on the next key that is not one of the search's own: a
               step with Alt-n or Alt-p keeps it, opening the search
               again hands the light over to the prompt */
            if (search_highlight_on && c_in != SearchNext && c_in != SearchPrev &&
                c_in != CTRL_KEY('f') && c_in != CTRL_KEY('F'))
            {
                ClearSearchHighlight();
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
                     c_in == WordLeft || c_in == WordRight ||
                     c_in == ViewportTop || c_in == ViewportMiddle || c_in == ViewportBottom)
            {
                /* anything else forgets the selection */
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
                MoveCursor(c_in);
            }
            else if (c_in == ViewportScrollDown || c_in == ViewportScrollUp)
            {
                /* Alt-e and Alt-y scroll the viewport one screen line
                   at a time: the viewport moves and the cursor keeps
                   its place in the text, coming along only when the
                   scroll would leave it off screen */
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
                ScreenScroll(c_in == ViewportScrollDown ? 1 : -1);
            }
            else if (c_in == BracketJump)
            {
                /* Alt-% jumps to the partner of the byte under the
                   cursor — a bracket's own kind, a quote's far end —
                   the way % does in vi: a movement, so the selection
                   is gone on arrival */
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
                EditorJumpToBracket();
            }
            else if (c_in == SearchNext || c_in == SearchPrev)
            {
                /* Alt-n and Alt-p repeat the last search without the
                   prompt, the way n and N do in vi: a movement, so
                   the selection is gone on arrival */
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
                EditorSearchRepeat(c_in == SearchNext ? 1 : -1);
            }
            else if (c_in == ShowTabsToggle)
            {
                /* Alt-t shows or hides the tab characters: every row
                   with a tab in it repaints, the whole viewport is
                   where they all live */
                vted_editor_config.show_tabs = !vted_editor_config.show_tabs;
                ScreenMarkAllDirty();

                SetStatusMessage("Tabs %s",
                                 vted_editor_config.show_tabs ? "shown" : "hidden");
            }
            else if (c_in == WrapIndentToggle)
            {
                /* Alt-i keeps the indent on the wrapped lines, or
                   puts them back edge to edge: the whole viewport
                   lays out differently now, so every row repaints */
                vted_editor_config.wrap_indent = !vted_editor_config.wrap_indent;
                ScreenMarkAllDirty();

                SetStatusMessage("Wrap indent %s",
                                 vted_editor_config.wrap_indent ? "on" : "off");
            }
            else if (c_in == IndentGuidesToggle)
            {
                /* Alt-g shows or hides the indent guides: every row
                   with an indent repaints, the whole viewport is
                   where they all live */
                vted_editor_config.indent_guides = !vted_editor_config.indent_guides;
                ScreenMarkAllDirty();

                SetStatusMessage("Indent guides %s",
                                 vted_editor_config.indent_guides ? "shown" : "hidden");
            }
            else if (c_in == ElectricBracketsToggle)
            {
                /* Alt-( turns the electric bracket pairs on and off:
                   a plain ( is a character of the text and stays one */
                vted_editor_config.electric_brackets = !vted_editor_config.electric_brackets;

                SetStatusMessage("Electric brackets %s",
                                 vted_editor_config.electric_brackets ? "on" : "off");
            }
            else if (c_in == BracketColorPrompt)
            {
                /* Alt-c sets the color the matching bracket lights up
                   with: the setting the rc file holds, answered here
                   the way Ctrl-W answers the tab width */
                EditorSetBracketColor();
            }
            else if (c_in == ThemePrompt)
            {
                /* Alt-s picks the color scheme: the theme the rc file
                   names, answered here the way Alt-c answers the
                   bracket color */
                EditorSetTheme();
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
            else if (c_in == BufferPrev || c_in == BufferNext)
            {
                /* Alt-, and Alt-. step through the open buffers */
                EditorStepBuffer(c_in == BufferNext ? 1 : -1);
            }
            else if (c_in >= BufferSelect1 && c_in <= BufferSelect9)
            {
                /* Alt-1 through Alt-9 land on the buffer with that
                   number; a number past the end says there is none */
                int wanted = c_in - BufferSelect1 + 1;

                if (wanted > BuffersCount())
                {
                    SetStatusMessage("No buffer %d: %d open", wanted,
                                     BuffersCount());
                }
                else if (BuffersSwitch(wanted - 1))
                {
                    SetStatusMessage("%s (buffer %d of %d)",
                                     vted_buffer.filename != NULL ? vted_buffer.filename
                                                                  : "[No Name]",
                                     wanted, BuffersCount());
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
            else if (c_in == MousePress || c_in == MouseRelease || c_in == MouseDrag ||
                     c_in == MouseWheelUp || c_in == MouseWheelDown)
            {
                /* the mouse over the viewport: a click places the
                   cursor at the position the renderer drew under it,
                   a drag selects from where the click went down, and
                   a wheel notch scrolls that position into view */
                EditorHandleMouse(c_in);
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
                   left of the cursor — the empty pair the cursor sits
                   inside going as one when the electric pairs are on */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }
                else if (vted_editor_config.electric_brackets &&
                         EditorDeletePair())
                {
                    /* the pair went as one */
                }
                else
                {
                    EditorDelChar();
                }
            }
            else if (c_in == DeleteWordLeft)
            {
                /* Alt-Backspace removes the selection, or the word
                   left of the cursor the way the shells do */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }
                else
                {
                    EditorDeleteWordLeft();
                }
            }
            else if (c_in == DeleteWordRight)
            {
                /* Alt-d removes the selection, or the word right of
                   the cursor, the way Ctrl-Right would cross it */
                if (EditSelectionActive())
                {
                    EditDeleteSelection();
                }
                else
                {
                    EditorDeleteWordRight();
                }
            }
            else if (c_in == WordComplete)
            {
                /* Alt-/ completes the word before the cursor with the
                   words of the open buffers, dabbrev style: the menu
                   the buffer picker uses lists them */
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
                EditorCompleteWord();
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
                /* Tab indents the rows the selection spans; without
                   one it inserts up to the next tab stop */
                if (EditSelectionActive())
                {
                    EditorIndentSelection();
                }
                else if (vted_editor_config.insert_spaces != 0)
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
            else if (c_in == ShiftTab)
            {
                /* Shift-Tab takes an indent step off the rows the
                   selection spans — or off the row of the cursor
                   when nothing is selected — the way Tab puts one
                   in front of them */
                EditorOutdentSelection();
            }
            else if (c_in == CTRL_KEY('e') || c_in == CTRL_KEY('E'))
            {
                /* Ctrl-E closes the buffer being edited: unsaved
                   changes hold the close back, the way they hold a quit
                   back, until it is pressed again */
                quit_confirmations = QUIT_CONFIRMATIONS;

                if (vted_buffer.dirty != 0 && close_confirmations > 0)
                {
                    SetStatusMessage("WARNING! Unsaved changes. Press Ctrl-E %d more times to close.",
                                     close_confirmations);
                    close_confirmations--;
                }
                else
                {
                    EditorCloseBuffer();
                    close_confirmations = CLOSE_CONFIRMATIONS;
                }
            }
            else if (c_in == CTRL_KEY('b') || c_in == CTRL_KEY('B'))
            {
                /* Ctrl-B picks a buffer from a list of what is open */
                EditorPickBuffer();
            }
            else if (c_in == CTRL_KEY('s') || c_in == CTRL_KEY('S'))
            {
                /* Ctrl-S writes the buffer back to its file */
                EditorSave();
            }
            else if (c_in == CTRL_KEY('o') || c_in == CTRL_KEY('O'))
            {
                /* Ctrl-O opens another file as a buffer of its own */
                EditorOpenFile();
            }
            else if (c_in == OpenFileAtCursor)
            {
                /* Alt-o opens the file the cursor sits on, the way gf
                   does in vi: a movement, so the selection is gone on
                   arrival */
                EditClearSelection();
                ScreenSetSelection(-1, 0, -1, 0);
                EditorOpenFileAtCursor();
            }
            else if (c_in == CTRL_KEY('f') || c_in == CTRL_KEY('F'))
            {
                /* Ctrl-F searches the buffer incrementally */
                EditorFind();
            }
            else if (c_in == CTRL_KEY('r') || c_in == CTRL_KEY('R'))
            {
                /* Ctrl-R searches and replaces, one match at a time */
                EditorReplace();
            }
            else if (c_in == CTRL_KEY('g') || c_in == CTRL_KEY('G'))
            {
                /* Ctrl-G jumps to a line number */
                EditorGoToLine();
            }
            else if (c_in == CTRL_KEY('w') || c_in == CTRL_KEY('W'))
            {
                /* Ctrl-W sets the tab width in cells */
                EditorSetTabWidth();
            }
            else if (c_in == CTRL_KEY('n') || c_in == CTRL_KEY('N'))
            {
                /* Ctrl-N shows or hides the line number gutter */
                vted_editor_config.show_line_numbers = !vted_editor_config.show_line_numbers;
                SetStatusMessage("Line numbers %s",
                                 vted_editor_config.show_line_numbers ? "on" : "off");
            }
            else if (c_in == CTRL_KEY('l') || c_in == CTRL_KEY('L'))
            {
                /* Ctrl-L wraps long lines over several screen lines,
                   or puts them back on one line that scrolls sideways */
                vted_editor_config.wrap = !vted_editor_config.wrap;

                /* the whole viewport lays out differently now, and a
                   wrapped one never scrolls sideways. A vertical run
                   in progress forgets its column: the wrapped and the
                   plain layout count it differently */
                vted_editor_config.col_offset = 0;
                vted_editor_config.line_offset = 0;
                vted_editor_config.preferred_rx = -1;
                ScreenMarkAllDirty();

                SetStatusMessage("Wrap %s",
                                 vted_editor_config.wrap ? "on" : "off");
            }
            else if (c_in == CTRL_KEY('t') || c_in == CTRL_KEY('T'))
            {
                /* Ctrl-T toggles whether Tab inserts spaces up to the
                   next tab stop instead of a tab character */
                vted_editor_config.insert_spaces = !vted_editor_config.insert_spaces;
                SetStatusMessage("Tab inserts %s",
                                 vted_editor_config.insert_spaces ? "spaces" : "a tab");
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
            else if (c_in == CommentToggle)
            {
                /* Ctrl-/ comments the line under the cursor, or every
                   line the selection spans, and takes the comment off
                   again: the selection lives on, so a second press
                   takes it back off */
                EditorToggleComment();
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
                   replacing the selection first — an opening bracket
                   or a quote character excepted: with the electric
                   pairs on it wraps the selection instead of taking
                   it out */

                /* an opening bracket typed with the electric pairs on
                   closes itself right away, the cursor between the two;
                   a closing one already under the cursor, its opener
                   behind it, steps over instead */
                char closer = vted_editor_config.electric_brackets
                                  ? RowBracketCloser((char)c_in)
                                  : 0;

                /* the closer a wrap takes: a bracket's own, or a
                   quote answering itself as both halves — a quote
                   typed over a selection wraps it the way a bracket
                   does, and with nothing selected stays the plain
                   character it always was, laying no empty pair of
                   its own */
                char wrap_closer = vted_editor_config.electric_brackets
                                       ? RowWrapCloser((char)c_in)
                                       : 0;

                if (wrap_closer != 0 &&
                    EditorWrapSelection(c_in, wrap_closer))
                {
                    /* the selection went in wrapped */
                }
                else
                {
                    if (EditSelectionActive())
                    {
                        EditDeleteSelection();
                    }

                    if (closer != 0)
                    {
                        EditorInsertPair(c_in, closer);
                    }
                    else if (vted_editor_config.electric_brackets &&
                             CursorStepsOverCloser(c_in))
                    {
                        vted_editor_config.cursor_x++;
                    }
                    else
                    {
                        EditorInsertChar(c_in);
                    }
                }
            }
        }

        /* the cursor and the buffer are both settled here, after the
           key and before the frame */
        UpdateBracketMatch();

        RefreshScreen();
    }

    /* leaving for good: leave the alternate screen so the shell
       resumes on the main screen; the exit handlers restore the
       terminal settings and free the editor state */
    ExitAlternateScreenBuffer();
    return 0;
}
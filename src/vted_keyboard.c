/*===============================*/
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
/*===============================*/

#define ESC '\x1B'
/*===============
| Name:    ESC  |
| Decimal: 27   |
| Hex:     0x1B |
| Octal:   033  |
================*/

#define NO_KEY 0

int ReadRawByte(void)
{
    unsigned char user_input;
    /* (unsigned char) ensures bytes above 127 stay
    positive (0 to 255) and aren't misread as -1 */
    ssize_t read_return = read(TerminalFd(), &user_input, 1);

    if (read_return == 1)
    {
        return user_input;
    }

    /* read_return == 0 is the idle timeout (VMIN = 0, VTIME = 1): no
       key yet, not the end of the input. A terminal that is gone is
       detected through the failing frame writes instead. */
    else if (read_return == -1)
    {

        /* EINTR: a signal arrived while waiting, there is simply no
           key yet and the caller will poll again */
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            return NO_KEY;
        }
        KillApp("read");
    }

    return NO_KEY;
}
/* reads one more byte of an escape sequence; -1 when none arrives
   within the read timeout (the sequence was cut short) */
static int ReadSequenceByte(void)
{
    unsigned char byte;

    if (read(TerminalFd(), &byte, 1) != 1)
    {
        return -1;
    }

    return byte;
}

/* xterm style modifier bits, sent as 1 + the bits in the last
   parameter of a key sequence: ESC [ 1 ; 5 C is Ctrl-Right */
#define MODIFIER_SHIFT 1
#define MODIFIER_ALT 2
#define MODIFIER_CTRL 4

/* the position and the button of the last mouse report, kept for the
   key code it arrived with; -1 before the first one. The terminal
   counts its cells from one, the editor from zero, and the button is
   0 left, 1 middle, 2 right and 3 none */
static int mouse_last_x = -1;
static int mouse_last_y = -1;
static int mouse_last_button = -1;

int MouseLastX(void)
{
    return mouse_last_x;
}

int MouseLastY(void)
{
    return mouse_last_y;
}

int MouseLastButton(void)
{
    return mouse_last_button;
}

/* SGR mouse reports have the shape
     ESC [ < button ; x ; y M     (a press or a motion)
     ESC [ < button ; x ; y m     (a release)
   The button byte packs what happened: bits 0 and 1 the button itself
   (0 left, 1 middle, 2 right), bit 2 shift, bit 3 alt, bit 4 ctrl,
   bit 5 a motion with the button held, bit 6 the wheel (the button
   bits then saying which way, 0 up and 1 down). x and y count screen
   cells from one; they are clamped here to a screen no terminal is
   wider than, so a report garbled into a huge number cannot move the
   cursor somewhere strange */
#define MOUSE_MAX_COORDINATE 9999

static int ParseMouseReport(const char *parameters, int final_byte)
{
    /* the parameters are "button ; x ; y", digits and separators */
    long fields[3] = {0, 0, 0};
    int field = 0;
    int i;

    for (i = 0; parameters[i] != '\0' && field < 3; i++)
    {
        if (parameters[i] == ';')
        {
            field++;
        }
        else if (parameters[i] >= '0' && parameters[i] <= '9')
        {
            fields[field] = fields[field] * 10 + (parameters[i] - '0');

            if (fields[field] > MOUSE_MAX_COORDINATE)
            {
                fields[field] = MOUSE_MAX_COORDINATE;
            }
        }
        else
        {
            return NO_KEY; /* not a mouse report after all */
        }
    }

    mouse_last_x = (int)fields[1] - 1;
    mouse_last_y = (int)fields[2] - 1;
    mouse_last_button = (int)(fields[0] & 3);

    /* the wheel is its own two keys: up and down a notch */
    if (fields[0] & 64)
    {
        switch (fields[0] & 3)
        {
        case 0:
            return MouseWheelUp;
        case 1:
            return MouseWheelDown;
        default:
            return NO_KEY; /* the side wheels of exotic mice: not bound */
        }
    }

    /* a motion with a button held drags; a press and a release are
       told apart by the final byte alone */
    if (fields[0] & 32)
    {
        return MouseDrag;
    }

    return final_byte == 'M' ? MousePress : MouseRelease;
}

/* modifier bits of a CSI key, 0 for a plain key. Older terminals leave
   out the leading 1 (ESC [ 5 C), so the last number counts either way */
static int CsiModifierBits(const char *parameters)
{
    const char *last = strrchr(parameters, ';');
    int code = atoi(last != NULL ? last + 1 : parameters);

    return code >= 2 ? code - 1 : 0;
}

static int ParseCsiSequence(void)
{
    /*=================================================
    | CSI sequences have the shape:                   |
    |   ESC [ <parameters> <final byte>               |
    |                                                 |
    | parameters are digits and ';' separators, the   |
    | final byte is in the range 0x40-0x7E. Reading   |
    | up to the final byte keeps modified keys such   |
    | as Ctrl-Right (ESC [ 1 ; 5 C) from leaking      |
    | their tail into the text as typed characters.   |
    |                                                 |
    |   ESC [ A-D          -> arrows                  |
    |     C/D with Ctrl or Alt -> word moves          |
    |   ESC [ H / ESC [ F  -> Home / End              |
    |   ESC [ n ~          -> extended keys:          |
    |     1,7 = Home   3 = Delete   4,8 = End         |
    |     5 = PageUp   6 = PageDown                   |
    |   ESC [ < b;x;y M/m  -> an SGR mouse report     |
    =================================================*/

    char parameters[16];
    int parameters_len = 0;
    int final_byte;

    while ((final_byte = ReadSequenceByte()) != -1)
    {
        if (final_byte >= 0x40 && final_byte <= 0x7E)
        {
            break;
        }

        /* overlong parameter lists are read to the end but not kept */
        if (parameters_len < (int)sizeof(parameters) - 1)
        {
            parameters[parameters_len++] = (char)final_byte;
        }
    }

    if (final_byte == -1)
    {
        return '\x1b'; /* cut short: report the lone ESC */
    }

    parameters[parameters_len] = '\0';

    /* the Linux console sends F1-F5 as ESC [ [ A..E: eat the letter */
    if (final_byte == '[')
    {
        ReadSequenceByte();
        return NO_KEY;
    }

    /* a mouse report names itself with a leading < among the
       parameters: ESC [ < 0 ; 10 ; 5 M, say. It is read here so the
       numbers and the final byte, taken for key parameters on any
       other sequence, reach the mouse parser instead */
    if (parameters[0] == '<' && (final_byte == 'M' || final_byte == 'm'))
    {
        return ParseMouseReport(parameters + 1, final_byte);
    }

    switch (final_byte)
    {
    case 'A':
        if (CsiModifierBits(parameters) == MODIFIER_CTRL)
        {
            return MoveLineUp;
        }
        if (CsiModifierBits(parameters) & MODIFIER_SHIFT)
        {
            return ShiftArrowUp;
        }
        return ArrowUp;
    case 'B':
        if (CsiModifierBits(parameters) == MODIFIER_CTRL)
        {
            return MoveLineDown;
        }
        if (CsiModifierBits(parameters) & MODIFIER_SHIFT)
        {
            return ShiftArrowDown;
        }
        return ArrowDown;
    case 'C':
        if (CsiModifierBits(parameters) & MODIFIER_SHIFT)
        {
            return ShiftArrowRight;
        }
        /* Ctrl or Alt turns the arrow into a word move; Shift alone
           stays a plain arrow */
        return (CsiModifierBits(parameters) & (MODIFIER_CTRL | MODIFIER_ALT)) ? WordRight : ArrowRight;
    case 'D':
        if (CsiModifierBits(parameters) & MODIFIER_SHIFT)
        {
            return ShiftArrowLeft;
        }
        return (CsiModifierBits(parameters) & (MODIFIER_CTRL | MODIFIER_ALT)) ? WordLeft : ArrowLeft;
    case 'H':
        if (CsiModifierBits(parameters) & MODIFIER_SHIFT)
        {
            return ShiftHome;
        }
        return HomeKey;
    case 'F':
        if (CsiModifierBits(parameters) & MODIFIER_SHIFT)
        {
            return ShiftEnd;
        }
        return EndKey;
    case 'Z':
        /* terminals spell Shift-Tab ESC [ Z, with or without the
           modifier parameters ahead of the final byte */
        return ShiftTab;
    case '~':
        switch (atoi(parameters))
        {
        case 1:
        case 7:
            return HomeKey;
        case 3:
            return DeleteKey;
        case 200:
            return PasteBegin;
        case 4:
        case 8:
            return EndKey;
        case 5:
            return PageUp;
        case 6:
            return PageDown;
        }
        break;
    }

    return NO_KEY; /* well-formed but not bound: ignored */
}

static int ParseSs3Sequence(void)
{
    /* older terminals and application cursor mode send ESC O <letter>
       for the arrows and for Home / End */

    switch (ReadSequenceByte())
    {
    case 'A':
        return ArrowUp;
    case 'B':
        return ArrowDown;
    case 'C':
        return ArrowRight;
    case 'D':
        return ArrowLeft;
    case 'H':
        return HomeKey;
    case 'F':
        return EndKey;

    /* rxvt spells Ctrl-Right and Ctrl-Left with lower case letters */
    case 'c':
        return WordRight;
    case 'd':
        return WordLeft;
    }

    return NO_KEY;
}

int ParseArrowKey(void)
{
    int introducer = ReadSequenceByte();

    if (introducer == -1)
    {
        return '\x1b'; /* nothing followed: the ESC key itself */
    }

    if (introducer == '[')
    {
        return ParseCsiSequence();
    }

    if (introducer == 'O')
    {
        return ParseSs3Sequence();
    }

    /* ESC followed by a letter is Alt+letter. Alt-b and Alt-f are the
       readline spelling of "word left" and "word right", handy where
       the terminal keeps Alt-Arrow for itself */
    if (introducer == 'b')
    {
        return WordLeft;
    }

    if (introducer == 'f')
    {
        return WordRight;
    }

    /* Alt-, and Alt-. step through the open buffers, the way a comma
       and a dot step through arguments in readline */
    if (introducer == ',')
    {
        return BufferPrev;
    }

    if (introducer == '.')
    {
        return BufferNext;
    }

    /* Alt-h, Alt-m and Alt-l jump to the top, the middle and the
       bottom screen line of the viewport, the way H, M and L do
       in vi */
    if (introducer == 'h')
    {
        return ViewportTop;
    }

    if (introducer == 'm')
    {
        return ViewportMiddle;
    }

    if (introducer == 'l')
    {
        return ViewportBottom;
    }

    /* Alt-e and Alt-y scroll the viewport one screen line at a time,
       the way Ctrl-E and Ctrl-Y do in vi — the Ctrl spellings are
       taken by closing the buffer and redoing */
    if (introducer == 'e')
    {
        return ViewportScrollDown;
    }

    if (introducer == 'y')
    {
        return ViewportScrollUp;
    }

    /* Alt-t shows or hides the tab characters: Ctrl-T is taken by
       the choice between spaces and a real tab */
    if (introducer == 't')
    {
        return ShowTabsToggle;
    }

    /* Alt-i keeps the indent on the wrapped lines, or takes it back
       off: Ctrl-I is the Tab key, so the i goes with Alt */
    if (introducer == 'i')
    {
        return WrapIndentToggle;
    }

    /* Alt-g shows or hides the indent guides: Ctrl-G is taken by
       going to a line, so the g goes with Alt */
    if (introducer == 'g')
    {
        return IndentGuidesToggle;
    }

    /* Alt-c sets the color the bracket match lights up with: the
       same setting the rc file knows, answered while editing the way
       Ctrl-W answers the tab width */
    if (introducer == 'c')
    {
        return BracketColorPrompt;
    }

    /* Alt-s picks the color scheme, the theme the settings file names
       with set theme: s for scheme, since Alt-t is taken by the tabs */
    if (introducer == 's')
    {
        return ThemePrompt;
    }

    /* Alt-% jumps to the partner of the bracket the cursor sits on,
       the way % does in vi — the plain % is a character of the text,
       so the jump goes with Alt */
    if (introducer == '%')
    {
        return BracketJump;
    }

    /* Alt-n and Alt-p step to the match after and before the last
       search's cursor, the way n and N do in vi, without opening the
       prompt again */
    if (introducer == 'n')
    {
        return SearchNext;
    }

    if (introducer == 'p')
    {
        return SearchPrev;
    }

    /* Alt-Backspace deletes the word left of the cursor, the way the
       shells and readline spell it; Ctrl-W is taken by the tab width,
       so the backspace goes with Alt */
    if (introducer == 0x7F)
    {
        return DeleteWordLeft;
    }

    /* Alt-d deletes the word right of the cursor, readline's kill
       word; a plain d is a character of the text and stays one */
    if (introducer == 'd')
    {
        return DeleteWordRight;
    }

    /* Alt-/ completes the word left of the cursor with the words of
       the open buffers, the way dabbrev-expand in Emacs does; a
       plain / is a character of the text and stays one */
    if (introducer == '/')
    {
        return WordComplete;
    }

    /* Alt-1 through Alt-9 land on the buffer with that number; the
       digits above nine would need two keystrokes and are not bound */
    if (introducer >= '1' && introducer <= '9')
    {
        return BufferSelect1 + (introducer - '1');
    }

    /* any other Alt+key is not bound: swallowed */
    return NO_KEY;
}
int ReadKeyPress()
{
    int user_input = ReadRawByte();

    if (user_input == ESC)
    {
        user_input = ParseArrowKey();
    }

    /* Ctrl-/ arrives as 0x1F, the byte Ctrl-_ has always been: the
       terminals that spell it as a CSI u code are not spoken to, and
       a terminal that sends the byte for either key takes both */
    else if (user_input == 0x1F)
    {
        return CommentToggle;
    }

    return user_input;
}

/* how long a paste may stay silent before it is taken as over: the
   terminal's own read timeout is much shorter than the pauses a big
   paste can have between two chunks, so reading must outwait it */
#define PASTE_STALL_MS 2000

/* the end marker of a bracketed paste */
static const char paste_end_marker[] = {'\x1b', '[', '2', '0', '1', '~'};

/* appends bytes to the paste buffer, growing it as needed */
static void AppendPastedBytes(char **text, int *capacity, int *length,
                              const char *bytes, int count)
{
    if (*length + count > *capacity)
    {
        while (*length + count > *capacity)
        {
            *capacity *= 2;
        }

        char *grown = realloc(*text, (size_t)*capacity);

        if (grown == NULL)
        {
            free(*text);
            KillApp("realloc");
        }

        *text = grown;
    }

    memcpy(*text + *length, bytes, (size_t)count);
    *length += count;
}

char *ReadPastedText(int *out_length)
{
    /*=================================================
    | Inside a bracketed paste the terminal sends the |
    | pasted bytes verbatim, closed by ESC [ 2 0 1 ~. |
    | The marker is scanned out byte by byte so text  |
    | containing an ESC of its own survives intact.   |
    | Input that ends before the marker (a dead ssh   |
    | connection, say) pastes whatever arrived.       |
    =================================================*/

    int capacity = 256;
    int length = 0;
    int matched = 0; /* bytes of the end marker seen so far */
    char *text = malloc((size_t)capacity);

    if (text == NULL)
    {
        KillApp("malloc");
    }

    while (matched < (int)sizeof(paste_end_marker))
    {
        unsigned char byte;
        ssize_t got = read(TerminalFd(), &byte, 1);

        if (got != 1)
        {
            /* the read timeout ran out (or a signal came in): the paste
               may only be pausing, so wait for more. End-of-file and
               real silence both leave without POLLIN, and then the
               paste is over, marker or not */
            struct pollfd waiting = {.fd = TerminalFd(), .events = POLLIN, .revents = 0};
            int ready = poll(&waiting, 1, PASTE_STALL_MS);

            if (ready == -1 && errno == EINTR)
            {
                continue;
            }

            if (ready <= 0 || !(waiting.revents & POLLIN))
            {
                break;
            }

            continue; /* data is waiting: read it */
        }

        if (byte == (unsigned char)paste_end_marker[matched])
        {
            matched++;
        }
        else
        {
            /* a mismatch means the pending bytes were text after
               all; the mismatching byte may open the marker itself */
            AppendPastedBytes(&text, &capacity, &length,
                              paste_end_marker, matched);

            if (byte == (unsigned char)paste_end_marker[0])
            {
                matched = 1;
            }
            else
            {
                AppendPastedBytes(&text, &capacity, &length,
                                  (const char *)&byte, 1);
                matched = 0;
            }
        }
    }

    /* input that ended mid-marker leaves the pending bytes as text */
    if (matched > 0 && matched < (int)sizeof(paste_end_marker))
    {
        AppendPastedBytes(&text, &capacity, &length,
                          paste_end_marker, matched);
    }

    char *ended = realloc(text, (size_t)length + 1);

    if (ended == NULL)
    {
        free(text);
        KillApp("realloc");
    }

    ended[length] = '\0';
    *out_length = length;

    if (length == 0)
    {
        free(ended);
        return NULL;
    }

    return ended;
}

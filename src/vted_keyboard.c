/*===============================*/
#include <unistd.h>
#include <fcntl.h>
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

void SetNonBlockingInput(void)
{
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}
int ReadRawByte(void)
{
    unsigned char user_input;
    /* (unsigned char) ensures bytes above 127 stay
    positive (0 to 255) and aren't misread as -1 */
    ssize_t read_return = read(STDIN_FILENO, &user_input, 1);

    if (read_return == 1)
    {
        return user_input;
    }
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

    if (read(STDIN_FILENO, &byte, 1) != 1)
    {
        return -1;
    }

    return byte;
}

/* xterm style modifier bits, sent as 1 + the bits in the last
   parameter of a key sequence: ESC [ 1 ; 5 C is Ctrl-Right */
#define MODIFIER_ALT 2
#define MODIFIER_CTRL 4

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

    switch (final_byte)
    {
    case 'A':
        return ArrowUp;
    case 'B':
        return ArrowDown;
    case 'C':
        /* Ctrl or Alt turns the arrow into a word move; Shift alone
           stays a plain arrow */
        return (CsiModifierBits(parameters) & (MODIFIER_CTRL | MODIFIER_ALT)) ? WordRight : ArrowRight;
    case 'D':
        return (CsiModifierBits(parameters) & (MODIFIER_CTRL | MODIFIER_ALT)) ? WordLeft : ArrowLeft;
    case 'H':
        return HomeKey;
    case 'F':
        return EndKey;
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
        ssize_t got = read(STDIN_FILENO, &byte, 1);

        if (got != 1)
        {
            /* the read timeout ran out (or a signal came in): the paste
               may only be pausing, so wait for more. End-of-file and
               real silence both leave without POLLIN, and then the
               paste is over, marker or not */
            struct pollfd waiting = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
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

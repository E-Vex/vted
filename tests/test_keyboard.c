/* the pseudo-terminal calls below need POSIX 2008, which -std=c11
   alone hides */
#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_keyboard.h"
/*===============================*/

/* Puts bytes on a pipe and makes it the standard input, so the real
   parser reads them. The write end is closed right away: a sequence
   that is cut short ends in end-of-file instead of waiting for more
   bytes, the same way the read timeout cuts it short in the editor. */
static void FeedInput(const char *bytes)
{
    int fds[2];
    size_t length = strlen(bytes);

    if (pipe(fds) == -1)
    {
        fprintf(stderr, "  pipe failed\n");
        _exit(2);
    }

    if (write(fds[1], bytes, length) != (ssize_t)length)
    {
        fprintf(stderr, "  write failed\n");
        _exit(2);
    }

    close(fds[1]);
    dup2(fds[0], STDIN_FILENO);
    close(fds[0]);
}

static void TestPlainKeys(void)
{
    FeedInput("a");
    CHECK_INT(ReadKeyPress(), 'a');

    FeedInput("\x1b[A");
    CHECK_INT(ReadKeyPress(), ArrowUp);
    FeedInput("\x1b[B");
    CHECK_INT(ReadKeyPress(), ArrowDown);
    FeedInput("\x1b[C");
    CHECK_INT(ReadKeyPress(), ArrowRight);
    FeedInput("\x1b[D");
    CHECK_INT(ReadKeyPress(), ArrowLeft);

    FeedInput("\x1b[H");
    CHECK_INT(ReadKeyPress(), HomeKey);
    FeedInput("\x1b[F");
    CHECK_INT(ReadKeyPress(), EndKey);
    FeedInput("\x1b[3~");
    CHECK_INT(ReadKeyPress(), DeleteKey);
    FeedInput("\x1b[5~");
    CHECK_INT(ReadKeyPress(), PageUp);
    FeedInput("\x1b[6~");
    CHECK_INT(ReadKeyPress(), PageDown);
}

static void TestApplicationCursorKeys(void)
{
    FeedInput("\x1bOC");
    CHECK_INT(ReadKeyPress(), ArrowRight);
    FeedInput("\x1bOD");
    CHECK_INT(ReadKeyPress(), ArrowLeft);
    FeedInput("\x1bOH");
    CHECK_INT(ReadKeyPress(), HomeKey);
    FeedInput("\x1bOF");
    CHECK_INT(ReadKeyPress(), EndKey);
}

static void TestCtrlAndAltArrowsMoveByWord(void)
{
    /* rxvt: ESC O c and ESC O d */
    FeedInput("\x1bOc");
    CHECK_INT(ReadKeyPress(), WordRight);
    FeedInput("\x1bOd");
    CHECK_INT(ReadKeyPress(), WordLeft);

    /* xterm: ESC [ 1 ; <1 + modifier bits> <letter> */
    FeedInput("\x1b[1;5C"); /* Ctrl-Right */
    CHECK_INT(ReadKeyPress(), WordRight);
    FeedInput("\x1b[1;5D"); /* Ctrl-Left */
    CHECK_INT(ReadKeyPress(), WordLeft);
    FeedInput("\x1b[1;3C"); /* Alt-Right */
    CHECK_INT(ReadKeyPress(), WordRight);
    FeedInput("\x1b[1;3D"); /* Alt-Left */
    CHECK_INT(ReadKeyPress(), WordLeft);
    FeedInput("\x1b[1;7C"); /* Ctrl-Alt-Right */
    CHECK_INT(ReadKeyPress(), WordRight);

    /* older terminals drop the leading 1 */
    FeedInput("\x1b[5C");
    CHECK_INT(ReadKeyPress(), WordRight);
    FeedInput("\x1b[5D");
    CHECK_INT(ReadKeyPress(), WordLeft);
}

static void TestAltLettersMoveByWord(void)
{
    FeedInput("\x1b" "b"); /* Alt-b */
    CHECK_INT(ReadKeyPress(), WordLeft);
    FeedInput("\x1b" "f"); /* Alt-f */
    CHECK_INT(ReadKeyPress(), WordRight);

    /* other Alt combinations are swallowed whole, no text leaks */
    FeedInput("\x1b" "x");
    CHECK_INT(ReadKeyPress(), 0);
    CHECK_INT(ReadKeyPress(), 0);

    /* plain b and f are still text */
    FeedInput("bf");
    CHECK_INT(ReadKeyPress(), 'b');
    CHECK_INT(ReadKeyPress(), 'f');
}

static void TestShiftArrowsStayPlain(void)
{
    FeedInput("\x1b[1;2C"); /* Shift-Right */
    CHECK_INT(ReadKeyPress(), ArrowRight);
    FeedInput("\x1b[1;2D"); /* Shift-Left */
    CHECK_INT(ReadKeyPress(), ArrowLeft);
    FeedInput("\x1b[1C"); /* a leading 1 alone is no modifier */
    CHECK_INT(ReadKeyPress(), ArrowRight);
}

static void TestModifiedUpDownStayPlain(void)
{
    /* only Left and Right have a word meaning */
    FeedInput("\x1b[1;5A");
    CHECK_INT(ReadKeyPress(), ArrowUp);
    FeedInput("\x1b[1;5B");
    CHECK_INT(ReadKeyPress(), ArrowDown);
}

static void TestSequencesDoNotLeakText(void)
{
    /* the tail of a modified key must not come back as typed text:
       the next key read is the 'x' and nothing else */
    FeedInput("\x1b[1;5Cx");
    CHECK_INT(ReadKeyPress(), WordRight);
    CHECK_INT(ReadKeyPress(), 'x');
    CHECK_INT(ReadKeyPress(), 0);
}

static void TestCutShortSequences(void)
{
    FeedInput("\x1b");
    CHECK_INT(ReadKeyPress(), '\x1b');
    FeedInput("\x1b[");
    CHECK_INT(ReadKeyPress(), '\x1b');
    FeedInput("\x1b[1;5");
    CHECK_INT(ReadKeyPress(), '\x1b');
}

static void TestBracketedPasteMarkers(void)
{
    /* the begin marker reads as its own key code */
    FeedInput("\x1b[200~");
    CHECK_INT(ReadKeyPress(), PasteBegin);

    /* an end marker arriving on its own (another app left the mode
       on, say) is not bound: nothing leaks into the text */
    FeedInput("\x1b[201~");
    CHECK_INT(ReadKeyPress(), 0);
}

static void TestReadPastedText(void)
{
    int length;
    char *paste;

    /* plain content up to the end marker */
    FeedInput("one\r\ntwo\x1b[201~");
    paste = ReadPastedText(&length);
    CHECK_STR(paste, "one\r\ntwo");
    CHECK_INT(length, 8);
    free(paste);

    /* an ESC inside the content is not the end marker */
    FeedInput("a\x1bOb\x1b[201~");
    paste = ReadPastedText(&length);
    CHECK_STR(paste, "a\x1bOb");
    free(paste);

    /* input ending before the marker pastes what arrived */
    FeedInput("tail");
    paste = ReadPastedText(&length);
    CHECK_STR(paste, "tail");
    free(paste);

    /* an empty paste reads as nothing */
    FeedInput("\x1b[201~");
    paste = ReadPastedText(&length);
    CHECK_INT(paste == NULL, 1);
}

/* A real terminal in the editor's raw mode reads with a 100 ms timeout,
   and a big paste can pause for longer than that between two chunks.
   The paste must wait for the rest instead of ending at the pause. */
static void TestSlowPasteIsNotCut(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);

    if (master == -1 || grantpt(master) == -1 || unlockpt(master) == -1)
    {
        return; /* no pseudo-terminals here: nothing to check */
    }

    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);

    if (slave == -1)
    {
        close(master);
        return;
    }

    struct termios raw;
    tcgetattr(slave, &raw);
    raw.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; /* the editor's 100 ms read timeout */
    tcsetattr(slave, TCSANOW, &raw);

    pid_t writer = fork();

    if (writer == 0)
    {
        /* the paste, with a pause of 300 ms in the middle of it */
        write(master, "ab", 2);
        struct timespec pause = {0, 300000000L};
        nanosleep(&pause, NULL);
        write(master, "cd\x1b[201~", 8);
        _exit(0);
    }

    dup2(slave, STDIN_FILENO);
    close(slave);

    int length = 0;
    char *paste = ReadPastedText(&length);

    CHECK(paste != NULL);

    if (paste != NULL)
    {
        CHECK_INT(length, 4);
        CHECK_STR(paste, "abcd");
        free(paste);
    }

    waitpid(writer, NULL, 0);
    close(master);
}

int main(void)
{
    TestPlainKeys();
    TestApplicationCursorKeys();
    TestCtrlAndAltArrowsMoveByWord();
    TestAltLettersMoveByWord();
    TestShiftArrowsStayPlain();
    TestModifiedUpDownStayPlain();
    TestSequencesDoNotLeakText();
    TestCutShortSequences();
    TestBracketedPasteMarkers();
    TestReadPastedText();
    TestSlowPasteIsNotCut();

    return TestSummary("keyboard");
}

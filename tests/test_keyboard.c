#include <unistd.h>
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

    return TestSummary("keyboard");
}

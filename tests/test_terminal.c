#include <unistd.h>
#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_terminal.h"
/*===============================*/

/* Runs the action with the standard output turned into a pipe and
   gives back what it wrote. The pipe holds the bytes right away,
   so the action cannot block on it. */
static void CaptureOutput(void (*action)(void), char *out, size_t size)
{
    int fds[2];
    int saved_stdout;

    if (pipe(fds) == -1)
    {
        fprintf(stderr, "  pipe failed\n");
        _exit(2);
    }

    fflush(stdout);

    saved_stdout = dup(STDOUT_FILENO);

    if (saved_stdout == -1 || dup2(fds[1], STDOUT_FILENO) == -1)
    {
        fprintf(stderr, "  dup2 failed\n");
        _exit(2);
    }

    close(fds[1]);

    action();

    fflush(stdout);

    if (dup2(saved_stdout, STDOUT_FILENO) == -1)
    {
        fprintf(stderr, "  dup2 failed\n");
        _exit(2);
    }

    close(saved_stdout);

    ssize_t got = read(fds[0], out, size - 1);

    if (got < 0)
    {
        got = 0;
    }

    out[got] = '\0';

    close(fds[0]);
}

static void TestBracketedPasteSequences(void)
{
    char out[64];

    /* mode 2004 is what makes the terminal wrap a paste in markers */
    CaptureOutput(EnableBracketedPaste, out, sizeof(out));
    CHECK_STR(out, "\x1b[?2004h");

    /* the mode is switched off again on the way out */
    CaptureOutput(DisableBracketedPaste, out, sizeof(out));
    CHECK_STR(out, "\x1b[?2004l");

    /* a mode that is already off stays quiet: the shell must not be
        told to stop wrapping pastes it never started wrapping */
    CaptureOutput(DisableBracketedPaste, out, sizeof(out));
    CHECK_STR(out, "");
}

int main(void)
{
    TestBracketedPasteSequences();

    return TestSummary("terminal");
}

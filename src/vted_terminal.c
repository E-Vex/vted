/* sigaction() and struct sigaction are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
/*===============================*/
#include "vted_terminal.h"
/*===============================*/

struct termios old_terminal_settings;

/* set by a signal: the editor leaves on the next loop turn and
   restores the terminal the usual way (through the exit handlers) */
static volatile sig_atomic_t vted_wants_exit = 0;

/* set by SIGWINCH: the next frame repaints at the new terminal size */
static volatile sig_atomic_t vted_terminal_resized = 0;

/* set when read() reaches end of file: stdin went away, so the editor
   leaves instead of spinning on an empty input */
static volatile sig_atomic_t vted_stdin_eof = 0;

static void OnExitSignal(int signo)
{
    (void)signo;
    vted_wants_exit = 1;
}

static void OnResizeSignal(int signo)
{
    (void)signo;
    vted_terminal_resized = 1;
}

/* set once old_terminal_settings holds a real snapshot; KillApp must
   never apply the zeroed struct it would otherwise find there */
static int terminal_settings_saved = 0;

/* set while bracketed paste is on, so turning it off writes nothing
   when it was never turned on */
static int bracketed_paste_enabled = 0;

void KillApp(const char *syscall_name)
{
    int saved_errno = errno; /* the writes below must not clobber it */

    /* leave the alternate screen before reporting: anything printed
       inside it vanishes on exit and the shell would resume there.
       Harmless when the alternate screen was never entered */
    write(STDOUT_FILENO, "\x1b[?1049l", 8);

    /* back to cooked mode so the message ends in a proper CR LF; the
       call is made directly, RestoreTerminalSettings() reports its own
       failures through KillApp() and would recurse from here */
    if (terminal_settings_saved)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_terminal_settings);
    }

    errno = saved_errno;
    perror(syscall_name);
    exit(EXIT_FAILURE);
}

void SaveTerminalCurrentSettings(void)
{

    int tcgetattr_return;

    tcgetattr_return = tcgetattr(STDIN_FILENO, &old_terminal_settings); /* tcgetattr() returns value: 0 on success, -1 on error (with errno set). */

    if (tcgetattr_return == -1)
    {
        KillApp("tcgetattr");
    }

    terminal_settings_saved = 1;

    atexit(RestoreTerminalSettings);
}

void EnableTerminalRawMode(void)
{

    struct termios raw_terminal_settings = old_terminal_settings;

    /* enable the raw mode settings */

    raw_terminal_settings.c_iflag &= ~(tcflag_t)ICRNL;  // ==> disable carriage return
    raw_terminal_settings.c_iflag &= ~(tcflag_t)IXON;   // ==> disable software flow control
    raw_terminal_settings.c_iflag &= ~(tcflag_t)BRKINT; // ==> disable serial break condition
    raw_terminal_settings.c_iflag &= ~(tcflag_t)INPCK;  // ==> disable parity checking
    raw_terminal_settings.c_iflag &= ~(tcflag_t)ISTRIP; // ==> disable 7-bit ASCII received
    // raw_terminal_settings.c_iflag &= ~IGNBRK;
    // raw_terminal_settings.c_iflag &= ~PARMRK;
    // raw_terminal_settings.c_iflag &= ~INLCR;
    // raw_terminal_settings.c_iflag &= ~IGNCR;

    raw_terminal_settings.c_oflag &= ~(tcflag_t)OPOST; // ==> disable output post-processing

    raw_terminal_settings.c_cflag |= CS8; // ==> set character size to 8 bits
    // raw_terminal_settings.c_cflag &= ~CSIZE;
    // raw_terminal_settings.c_cflag &= ~PARENB;

    raw_terminal_settings.c_lflag &= ~(tcflag_t)ICANON; // ==> disable canonical mode
    raw_terminal_settings.c_lflag &= ~(tcflag_t)ECHO;   // ==> disable echoing characters
    raw_terminal_settings.c_lflag &= ~(tcflag_t)IEXTEN; // ==> disable extended input processing
    raw_terminal_settings.c_lflag &= ~(tcflag_t)ISIG;   // ==> disable signal generationse
    // raw_terminal_settings.c_lflag &= ~ECHONL;

    /* read with timeout */
    raw_terminal_settings.c_cc[VMIN] = 0;  // ==> read() does not require any character If input is available, it returns immediately Otherwise, it waits for VTIME and may return 0.
    raw_terminal_settings.c_cc[VTIME] = 1; // ==> 0.1 second timeout (100 ms)

    /* apply changes */
    int tcsetattr_return;
    tcsetattr_return = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_terminal_settings);

    if (tcsetattr_return == -1)
    {
        KillApp("tcsetattr");
    }

    /*========================================*
     *  READ  TERMIOS  MAN   TO  MORE   DEPTH *
     *  READ OR THE DOCS IN OF terminal-modes *
     *  IN   ->   vted/docs/terminal-modes.md *
     *========================================*/
}

void EnableBracketedPaste(void)
{
    write(STDOUT_FILENO, "\x1b[?2004h", 8);
    bracketed_paste_enabled = 1;
}

void DisableBracketedPaste(void)
{
    if (bracketed_paste_enabled)
    {
        write(STDOUT_FILENO, "\x1b[?2004l", 8);
        bracketed_paste_enabled = 0;
    }
}

void SetupExitSignals(void)
{
    struct sigaction action;

    /* SIGHUP, SIGINT and SIGTERM all ask the editor to leave; the
       actual exit happens in the main loop, so the terminal is
       restored the usual way and nothing is left half drawn */
    memset(&action, 0, sizeof(action));
    action.sa_handler = OnExitSignal;

    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    /* a terminal that hangs up must not kill the editor with SIGPIPE:
       a failed write is reported and the editor leaves instead */
    signal(SIGPIPE, SIG_IGN);
}

void SetupResizeSignal(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = OnResizeSignal;
    sigaction(SIGWINCH, &action, NULL);
}

int TerminalWantsExit(void)
{
    return vted_wants_exit || vted_stdin_eof;
}

int TerminalSizeChanged(void)
{
    int changed = vted_terminal_resized;

    vted_terminal_resized = 0;
    return changed;
}

void TerminalStdinEof(void)
{
    vted_stdin_eof = 1;
}

void RestoreTerminalSettings(void)
{
    int tcsetattr_return;

    /* this runs on every way out, KillApp included: the terminal must
       not keep wrapping pastes in markers once the shell has it back */
    DisableBracketedPaste();

    tcsetattr_return = tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_terminal_settings);

    if (tcsetattr_return == -1)
    {
        KillApp("tcsetattr");
    }
}

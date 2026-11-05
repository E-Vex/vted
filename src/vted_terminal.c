/* sigaction() and struct sigaction are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_screen.h"
/*===============================*/

struct termios old_terminal_settings;

/* the terminal the editor runs on: standard input when that is a
   terminal, the controlling terminal otherwise, so a pipe or a file
   on standard input does not stop the editor from reading keys */
static int tty_fd = -1;

/* set by a signal: the editor leaves on the next loop turn and
   restores the terminal the usual way (through the exit handlers) */
static volatile sig_atomic_t vted_wants_exit = 0;

/* set by SIGWINCH: the next frame repaints at the new terminal size */
static volatile sig_atomic_t vted_terminal_resized = 0;

/* set by SIGTSTP: the editor hands the terminal to the shell and
   stops on the next loop turn */
static volatile sig_atomic_t vted_suspend_requested = 0;

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

static void OnSuspendSignal(int signo)
{
    (void)signo;
    vted_suspend_requested = 1;
}

/* set once old_terminal_settings holds a real snapshot; KillApp must
   never apply the zeroed struct it would otherwise find there */
static int terminal_settings_saved = 0;

int TerminalFd(void)
{
    /* unresolved means nobody asked for a terminal yet: reads then go
       to standard input the way they always did, which is what the
       tests, with a pipe on standard input, count on */
    return tty_fd != -1 ? tty_fd : STDIN_FILENO;
}

/* picks the descriptor every terminal call goes through: standard
   input when it is a terminal, /dev/tty when it is not (a pipe on
   standard input, for instance, still leaves the editor a terminal
   to read keys from) */
static void ResolveTerminalFd(void)
{
    if (isatty(STDIN_FILENO))
    {
        tty_fd = STDIN_FILENO;
        return;
    }

    tty_fd = open("/dev/tty", O_RDWR);

    if (tty_fd == -1)
    {
        KillApp("open(\"/dev/tty\")");
    }
}

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
        tcsetattr(tty_fd, TCSAFLUSH, &old_terminal_settings);
    }

    errno = saved_errno;
    perror(syscall_name);
    exit(EXIT_FAILURE);
}

void SaveTerminalCurrentSettings(void)
{

    int tcgetattr_return;

    ResolveTerminalFd();

    tcgetattr_return = tcgetattr(tty_fd, &old_terminal_settings); /* tcgetattr() returns value: 0 on success, -1 on error (with errno set). */

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
    tcsetattr_return = tcsetattr(tty_fd, TCSAFLUSH, &raw_terminal_settings);

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

/* set while mouse reporting is on, so turning it off writes nothing
   when it was never turned on */
static int mouse_tracking_enabled = 0;

void EnableMouseTracking(void)
{
    /* mode 1000 reports the buttons going down and up, 1002 the
       motion of a held one, and 1006 asks for the SGR dialect: plain
       decimal numbers in the sequence, the one the keyboard module
       parses. A terminal that knows none of them ignores the request
       and simply never sends a report */
    write(STDOUT_FILENO, "\x1b[?1000h\x1b[?1002h\x1b[?1006h", 24);
    mouse_tracking_enabled = 1;
}

void DisableMouseTracking(void)
{
    if (mouse_tracking_enabled)
    {
        write(STDOUT_FILENO, "\x1b[?1000l\x1b[?1002l\x1b[?1006l", 24);
        mouse_tracking_enabled = 0;
    }
}

/* the editor draws on the alternate screen buffer: the screen the
   shell had keeps its scrollback and comes back whole when the
   editor leaves */
void EnterToAlternateScreenBuffer(void)
{
    write(STDOUT_FILENO, "\x1b[?1049h", 8);
}

void ExitAlternateScreenBuffer(void)
{
    write(STDOUT_FILENO, "\x1b[?1049l", 8);
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

void SetupSuspendSignal(void)
{
    struct sigaction action;

    /* a suspend that comes from outside (kill -TSTP) is only noted:
       carrying it out belongs to the main loop, which restores the
       terminal the usual way before the process stops. The stop
       itself happens with the default disposition, in
       TerminalSuspend() */
    memset(&action, 0, sizeof(action));
    action.sa_handler = OnSuspendSignal;
    sigaction(SIGTSTP, &action, NULL);
}

int TerminalWantsExit(void)
{
    return vted_wants_exit;
}

int TerminalSizeChanged(void)
{
    int changed = vted_terminal_resized;

    vted_terminal_resized = 0;
    return changed;
}

int TerminalSuspendRequested(void)
{
    int requested = vted_suspend_requested;

    vted_suspend_requested = 0;
    return requested;
}



void RestoreTerminalSettings(void)
{
    int tcsetattr_return;

    /* this runs on every way out, KillApp included: the terminal must
       not keep wrapping pastes in markers once the shell has it back */
    DisableBracketedPaste();

    /* nor keep the mouse to itself: the shell's own selections work
       again the moment the reporting is off */
    DisableMouseTracking();

    tcsetattr_return = tcsetattr(tty_fd, TCSAFLUSH, &old_terminal_settings);

    if (tcsetattr_return == -1)
    {
        KillApp("tcsetattr");
    }
}

/* hands the screen to the shell for a suspend: the screen the shell
   had comes back whole, pastes wrap and the mouse selects the
   shell's way again. The keys stay raw until TerminalSuspend() puts
   the cooked settings back — the two steps are separate so the
   writes alone can be tested */
void TerminalGiveToShell(void)
{
    ExitAlternateScreenBuffer();

    /* both stay quiet when the editor never had them */
    DisableBracketedPaste();
    DisableMouseTracking();
}

/* takes everything back when the editor continues: the alternate
   screen (empty, until the next frame fills it), paste and mouse
   reporting, and a whole-screen repaint. The raw keys are switched
   on by TerminalSuspend(), which alone knows the terminal settings */
void TerminalTakeFromShell(void)
{
    EnterToAlternateScreenBuffer();
    EnableBracketedPaste();
    EnableMouseTracking();

    /* every row repaints: nothing of what was on the alternate
       screen survived the time away */
    ScreenMarkAllDirty();
}

void TerminalSuspend(void)
{
    /* cooked keys for the shell, the way they were before the
       editor touched anything: the same path an exit takes, and
       running it after TerminalGiveToShell() is harmless — the two
       disables are already done and stay quiet */
    TerminalGiveToShell();
    RestoreTerminalSettings();

    /* stop the honest way: the default disposition is what tells
       the shell's job control the process stopped and the terminal
       is its own again. raise() only comes back when the editor is
       continued, and SIGCONT then runs the lines below */
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);

    /* back from the cold: the terminal holds whatever the shell
       left it, so every mode is asked for again */
    signal(SIGTSTP, OnSuspendSignal);
    EnableTerminalRawMode();
    TerminalTakeFromShell();
}

/* the Base64 alphabet, RFC 4648 */
static const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void TerminalCopy(const char *text, int length)
{
    /* OSC 52: copy the text to the terminal's clipboard; the terminal
       never answers, so this is best effort */
    int encoded = ((length + 2) / 3) * 4;
    char *buffer = malloc((size_t)encoded + 1);
    int at = 0;
    int i;

    if (buffer == NULL)
    {
        return;
    }

    for (i = 0; i + 3 <= length; i += 3)
    {
        int value = ((int)(unsigned char)text[i] << 16) |
                    ((int)(unsigned char)text[i + 1] << 8) |
                    (int)(unsigned char)text[i + 2];

        buffer[at++] = (char)base64_alphabet[(value >> 18) & 63];
        buffer[at++] = (char)base64_alphabet[(value >> 12) & 63];
        buffer[at++] = (char)base64_alphabet[(value >> 6) & 63];
        buffer[at++] = (char)base64_alphabet[value & 63];
    }

    if (i < length)
    {
        int value = (int)(unsigned char)text[i] << 16;
        int padding = 2;

        if (i + 1 < length)
        {
            value |= (int)(unsigned char)text[i + 1] << 8;
            padding = 1;
        }

        buffer[at++] = (char)base64_alphabet[(value >> 18) & 63];
        buffer[at++] = (char)base64_alphabet[(value >> 12) & 63];

        if (padding == 1)
        {
            buffer[at++] = (char)base64_alphabet[(value >> 6) & 63];
        }

        while (padding-- > 0)
        {
            buffer[at++] = '=';
        }
    }

    buffer[at] = '\0';

    write(STDOUT_FILENO, "\x1b]52;;", 6);
    write(STDOUT_FILENO, buffer, (size_t)at);
    write(STDOUT_FILENO, "\x1b\\", 2);

    free(buffer);
}

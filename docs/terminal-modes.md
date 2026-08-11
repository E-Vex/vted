terminal-modes
==============



vted is a terminal-based text editor. Unlike a shell,
which waits until you press Enter before processing
input, an editor has to react to every key the moment
it's pressed printable characters, arrow keys, `Ctrl`
combinations, escape sequences, all of it.
The Linux terminal is not just a static thing it
has some configuration settings which control how
it behaves so vted runs the terminal in **raw mode**.
In this mode, the terminal stops buffering input, echoing
typed characters, generating signals from special keys,
and performing most automatic input and output processing.
Every byte is delivered directly to the application, 
and vted becomes responsible for handling input and 
drawing the screen itself.

TERMINAL MODES AND THE TERMIOS API                               vted docs

THE TERMINAL DRIVER
-------------------

    Before anything else, you should understand what 
    the terminal driver is.
    When you type in a terminal, you are not sending keystrokes
    directly to your program. The kernel intercepts each key press
    and runs it through a layer called the terminal driver before
    your application ever sees it. It lives inside the kernel's TTY
    subsystem, not as a separate process -- each terminal window has
    an associated struct tty_struct in the kernel, which is where
    the termios configuration actually lives.

    Different programs want different things: a shell wants complete
    lines of text, an editor wants individual key presses. The
    terminal driver is what makes both possible, by:

        buffering    input until Enter is pressed (canonical mode)
        echoing      characters back to the screen
        processing   special characters like Ctrl+C and Ctrl+D
        translating  carriage returns, handling flow control

    A shell like Bash reads commands one line at a time. vted cannot
    wait that long. Terminal games run into the same problem, and
    usually turn off echoing too, since drawing every keystroke on
    screen would clash with the game's own rendering. termios is the
    API that lets a program ask the terminal for whichever of these
    behaviors it needs.

CANONICAL MODE
--------------

    By default, a terminal collects the characters you type and only
    hands them to the application once you press Enter, or send
    Ctrl+D, which also ends the input. This is canonical mode -- the
    terminal driver buffers the whole line and does basic line
    editing (backspace, etc.) before your program ever sees it. It
    is the default for Bash and most command-line tools, and it is
    controlled by the ICANON flag in c_lflag.

NON-CANONICAL MODE
------------------

    Some applications cannot wait for Enter -- an editor needs to
    react to every key the instant it is pressed. That is what
    non-canonical mode is for: input arrives character by character
    instead of line by line, enabled by clearing ICANON in c_lflag.

    Two control characters, VMIN and VTIME, govern how read()
    behaves once ICANON is off:

        VMIN > 0, VTIME = 0   block until VMIN characters arrive
        VMIN = 0, VTIME > 0   return immediately, or after VTIME
                               tenths of a second if nothing arrives
        VMIN > 0, VTIME > 0   return on VMIN characters or when the
                               inter-character timer expires
        VMIN = 0, VTIME = 0   polling mode -- return immediately
                               with whatever is available, maybe
                               nothing at all

    vted uses VMIN = 0, VTIME = 1: read() returns immediately if a
    key is waiting, or after a 100ms timeout if nothing arrives.
    That timeout is what lets vted come back around the loop and
    redraw or process pending state even when the user is not
    actively typing, instead of blocking forever on the next key.

RAW MODE
--------

    Raw mode is not just disabling ICANON. That alone gives
    character-by-character input, but the terminal still echoes,
    still generates signals, still runs other processing behind the
    scenes. Raw mode turns off nearly all of that, so the
    application decides how to interpret every byte itself. To get
    there, the following flags are disabled:

        ICANON  c_lflag   canonical (line-buffered) input
        ECHO    c_lflag   echoing typed characters back to the screen
        ISIG    c_lflag   signal generation (Ctrl+C, Ctrl+Z, Ctrl+\)
        IEXTEN  c_lflag   extended input processing (Ctrl+V, etc.)
        IXON    c_iflag   software flow control (Ctrl+S / Ctrl+Q)
        ICRNL   c_iflag   carriage return to newline translation
        OPOST   c_oflag   output post-processing

    Used by vim, nano, tty games, ssh, and vted. Compare against
    canonical mode (line-buffered, echo on, signals active) and
    plain non-canonical mode (character-oriented, but echo, signals
    and flow control still depend on their individual flags).

THE TERMIOS STRUCTURE
---------------------

    The POSIX termios API stores a terminal's configuration in a
    struct termios, defined in <termios.h>. A program reads the
    current settings, flips the flags it needs, and writes the
    configuration back to the terminal.

        struct termios
        {
            tcflag_t  c_iflag;      /* input mode flags */
            tcflag_t  c_oflag;      /* output mode flags */
            tcflag_t  c_cflag;      /* control mode flags */
            tcflag_t  c_lflag;      /* local mode flags */
            cc_t      c_line;       /* line discipline, usually N_TTY */
            cc_t      c_cc[NCCS];   /* control characters array */
            speed_t   c_ispeed;     /* input baud rate */
            speed_t   c_ospeed;     /* output baud rate */
        };

    c_iflag and c_lflag are the two that matter for an interactive
    app like vted. c_cflag and the baud rate fields are mostly
    leftovers from serial communication and rarely get touched by
    terminal programs. c_cc holds the special control characters --
    VINTR (Ctrl+C), VEOF (Ctrl+D), VERASE (Backspace), VMIN, VTIME,
    and others.

FLAGS
-----

    c_lflag (local mode)

        ICANON   canonical (line-buffered) input; on by default.
                 When off, input is non-canonical and governed by
                 VMIN/VTIME.
        ECHO     echo typed characters to the screen; on by default.
        ISIG     Ctrl+C -> SIGINT, Ctrl+Z -> SIGTSTP; on by default.
                 When off, these characters pass through as ordinary
                 input instead of generating signals.
        IEXTEN   enables implementation-defined extended input
                 functions such as Ctrl+V; on by default.

    c_iflag (input processing, before the application sees the data)

        IXON     Ctrl+S stops output, Ctrl+Q resumes it; on by
                 default. When off, both pass through as input.
        ICRNL    translates \r to \n; on by default.
        BRKINT   a break condition generates SIGINT; on by default.
        INPCK    input parity checking; off by default.
        ISTRIP   strips the eighth bit from input characters; off by
                 default. Needs to stay off for UTF-8 or binary data.

    c_oflag (output processing)

        OPOST    on by default; translates \n to \r\n on the way
                 out. Raw mode turns this off, which is why vted has
                 to print \r\n itself everywhere it draws to the
                 screen (see EXAMPLE below).

    c_cflag (hardware control -- mostly for serial ports)

        CS8      8-bit character size. The one c_cflag setting vted
                 actually sets, since 8 bits is the modern standard
                 and shouldn't be left to the default.

FUNCTIONS
---------

    tcgetattr(fd, termios_p)
        Retrieves the current termios configuration for file
        descriptor fd (usually STDIN_FILENO) into *termios_p.
        Returns 0 on success, -1 on error with errno set.

            struct termios old_settings;
            if (tcgetattr(STDIN_FILENO, &old_settings) == -1) {
                perror("tcgetattr");
                exit(EXIT_FAILURE);
            }

    tcsetattr(fd, optional_actions, termios_p)
        Applies the termios configuration in *termios_p to file
        descriptor fd. Returns 0 on success, -1 on error.

        optional_actions is one of:

            TCSANOW    apply immediately
            TCSADRAIN  wait for pending output to finish, then apply
            TCSAFLUSH  wait for pending output, discard pending
                       input, then apply

        TCSAFLUSH is usually right for enabling raw mode in an
        interactive program -- it stops old buffered input from
        leaking into the application right after the mode switch.
        That is why vted uses it: pending input gets discarded the
        moment raw mode kicks in.

EXAMPLE
-------

    A standalone raw-mode example, using the same flags and the same
    VMIN/VTIME values vted itself uses:

        #include <termios.h>
        #include <unistd.h>
        #include <stdlib.h>
        #include <stdio.h>

        struct termios orig_termios;

        void disableRawMode(void) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        }

        void enableRawMode(void) {
            struct termios raw;

            tcgetattr(STDIN_FILENO, &orig_termios);
            atexit(disableRawMode);
            raw = orig_termios;

            raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
            raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
            raw.c_oflag &= ~(OPOST);
            raw.c_cflag |= (CS8);

            raw.c_cc[VMIN]  = 0;
            raw.c_cc[VTIME] = 1;

            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        }

        int main(void) {
            char c;

            enableRawMode();
            printf("Raw mode enabled. Press 'q' to quit.\r\n");

            while (1) {
                if (read(STDIN_FILENO, &c, 1) == 1) {
                    if (c == 'q') break;
                    if (c < 32 || c == 127)
                        printf("\\x%02x (%d)\r\n", (unsigned char)c, c);
                    else
                        printf("Char: '%c' (%d)\r\n", c, c);
                }
            }

            printf("Goodbye!\r\n");
            return 0;
        }

    Note the \r\n instead of plain \n in every printf() call. Since
    OPOST is disabled, newlines are not automatically translated to
    carriage-return + newline, so \r\n has to be written explicitly
    to return the cursor to column 0. vted's own screen-drawing code
    follows the same rule.

NOTES
-----

    Quick reference for picking a mode:

        reading complete lines          canonical (default)
        reading single characters       non-canonical, set
                                         VMIN/VTIME as needed
        full-screen interactive app     raw mode (vted's case)
        password input                  non-canonical, ECHO off,
                                         ISIG left on

SEE ALSO
========
    termios(3), src/vted_terminal.c

    by E-Vex
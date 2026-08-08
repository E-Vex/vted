#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

struct termios old_terminal_settings;

void KillApp(const char *syscall_name)
{
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
}

void EnableTerminalRawMode(void)
{

    struct termios raw_terminal_settings = old_terminal_settings;

    /* enable the raw mode settings */

    raw_terminal_settings.c_iflag &= ~ICRNL;  // ==> disable carriage return
    raw_terminal_settings.c_iflag &= ~IXON;   // ==> disable software flow control
    raw_terminal_settings.c_iflag &= ~BRKINT; // ==> disable serial break condition
    raw_terminal_settings.c_iflag &= ~INPCK;  // ==> disable parity checking
    raw_terminal_settings.c_iflag &= ~ISTRIP; // ==> disable 7-bit ASCII received
    // raw_terminal_settings.c_iflag &= ~IGNBRK;
    // raw_terminal_settings.c_iflag &= ~PARMRK;
    // raw_terminal_settings.c_iflag &= ~INLCR;
    // raw_terminal_settings.c_iflag &= ~IGNCR;

    raw_terminal_settings.c_oflag &= ~OPOST; // ==> disable output post-processing

    raw_terminal_settings.c_cflag |= CS8; // ==> set character size to 8 bits
    // raw_terminal_settings.c_cflag &= ~CSIZE;
    // raw_terminal_settings.c_cflag &= ~PARENB;

    raw_terminal_settings.c_lflag &= ~ICANON; // ==> disable canonical mode
    raw_terminal_settings.c_lflag &= ~ECHO;   // ==> disable echoing characters
    raw_terminal_settings.c_lflag &= ~IEXTEN; // ==> disable extended input processing
    raw_terminal_settings.c_lflag &= ~ISIG;   // ==> disable signal generationse
    // raw_terminal_settings.c_lflag &= ~ECHONL;

    /*enable polling mode*/
    raw_terminal_settings.c_cc[VMIN] = 1;  // ==>  read() returns as soon as 1 char is available
    raw_terminal_settings.c_cc[VTIME] = 0; // ==> no timeout

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

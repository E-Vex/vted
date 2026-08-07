#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

void SaveTerminalCurrentSettings(struct termios *old_terminal_settings)
{

    int tcgetattr_return;

    tcgetattr_return = tcgetattr(STDIN_FILENO, old_terminal_settings); /* tcgetattr() returns value: 0 on success, -1 on error (with errno set). */

    if (tcgetattr_return == -1)
    {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
}

int main()
{
    struct termios old_terminal_settings;

    return 0;
}
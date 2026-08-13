#ifndef VTED_TERMINAL_H
#define VTED_TERMINAL_H

/*===============================*/
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
/*===============================*/

extern struct termios old_terminal_settings;

void KillApp(const char *syscall_name);

void SaveTerminalCurrentSettings(void);

void EnableTerminalRawMode(void);

void RestoreTerminalSettings(void);

#endif
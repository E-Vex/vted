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

/* asks the terminal to wrap pasted text in ESC [ 2 0 0 ~ and
   ESC [ 2 0 1 ~, so a paste can be told apart from typing. The mode
   is switched off again on every way out (RestoreTerminalSettings
   runs at exit), so the shell is never left receiving markers */
void EnableBracketedPaste(void);
void DisableBracketedPaste(void);

#endif

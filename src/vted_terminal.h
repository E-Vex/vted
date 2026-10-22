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

/* ask the editor to leave on the next loop turn: SIGHUP, SIGINT and
   SIGTERM all do this, and the terminal is then restored the usual
   way (through the exit handlers) */
void SetupExitSignals(void);

/* tell the editor that the terminal size changed: the next frame
   repaints at the new size */
void SetupResizeSignal(void);

/* queries */
int TerminalWantsExit(void);   /* 1 when the editor should leave */
int TerminalSizeChanged(void); /* 1 once after a resize, then 0 */

/* called by the keyboard module when stdin reaches end of file */
void TerminalStdinEof(void);

#endif

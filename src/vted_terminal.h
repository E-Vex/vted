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

/* asks the terminal to report the mouse: where a button goes down,
   where it comes up, where it is dragged and which way the wheel
   turns, as SGR sequences the keyboard module parses. The modes are
   switched off again on every way out (RestoreTerminalSettings runs
   at exit), so the shell gets the mouse back for its own selections;
   holding Shift bypasses the reporting in most terminals either way */
void EnableMouseTracking(void);
void DisableMouseTracking(void);

/* switches between the alternate screen buffer the editor draws on
   and the main screen the shell had: entering saves the shell's
   screen whole, leaving gives it back with its scrollback */
void EnterToAlternateScreenBuffer(void);
void ExitAlternateScreenBuffer(void);

/* ask the editor to leave on the next loop turn: SIGHUP, SIGINT and
   SIGTERM all do this, and the terminal is then restored the usual
   way (through the exit handlers) */
void SetupExitSignals(void);

/* tell the editor that the terminal size changed: the next frame
   repaints at the new size */
void SetupResizeSignal(void);

/* note a suspend that comes from outside (kill -TSTP): the request
   is carried out on the next loop turn, with the terminal restored
   the usual way before the process stops */
void SetupSuspendSignal(void);

/* queries */
int TerminalWantsExit(void);        /* 1 when the editor should leave */
int TerminalSizeChanged(void);      /* 1 once after a resize, then 0 */
int TerminalSuspendRequested(void); /* 1 once after a suspend ask, then 0 */

/* hands the screen to the shell for a suspend: the shell's screen
   comes back whole, pastes and the mouse work the shell's way
   again. The keys stay raw until TerminalSuspend() */
void TerminalGiveToShell(void);

/* takes the screen back when the editor continues: the alternate
   screen, paste and mouse reporting, and a whole-screen repaint */
void TerminalTakeFromShell(void);

/* the whole suspend: the terminal goes to the shell (screen,
   cooked keys), the process stops the ordinary way, and when it is
   continued everything is asked for again */
void TerminalSuspend(void);

/* the descriptor keys are read from and the terminal modes are
   managed on: standard input when it is a terminal, /dev/tty
   otherwise */
int TerminalFd(void);

/* copies text to the terminal's clipboard with an OSC 52 escape
   sequence; best effort, nothing is read back from the clipboard */
void TerminalCopy(const char *text, int length);

#endif

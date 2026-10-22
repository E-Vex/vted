#ifndef VTED_KEYBOARD_H
#define VTED_KEYBOARD_H

/*===============================*/
#include <unistd.h>
/*===============================*/
#include "vted_terminal.h"
/*===============================*/

#define CTRL_KEY(k) ((k) & 0x1f)

typedef enum
{
    ArrowLeft = 1000,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    HomeKey,
    EndKey,
    PageUp,
    PageDown,
    DeleteKey,
    WordLeft,   /* Ctrl or Alt with Left, Alt-b */
    WordRight,  /* Ctrl or Alt with Right, Alt-f */
    PasteBegin,   /* the terminal started a bracketed paste */
    MoveLineUp,   /* Ctrl-Up: move the current line up */
    MoveLineDown, /* Ctrl-Down: move the current line down */
    ShiftArrowLeft,   /* Shift-Arrow: start or extend a selection */
    ShiftArrowRight,
    ShiftArrowUp,
    ShiftArrowDown,
    ShiftHome,
    ShiftEnd,

} EditorKey;

int ReadKeyPress(void);

/* reads the bytes of a bracketed paste up to its end marker; returns
   them NUL-terminated in a heap buffer the caller frees (NULL when
   the paste held nothing) and reports their count through the
   out parameter */
char *ReadPastedText(int *out_length);

#endif

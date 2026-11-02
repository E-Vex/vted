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
    BufferPrev, /* Alt-, : the buffer before the current one */
    BufferNext, /* Alt-. : the buffer after the current one */
    BufferSelect1, /* Alt-1..Alt-9: the buffer with that number */
    BufferSelect2,
    BufferSelect3,
    BufferSelect4,
    BufferSelect5,
    BufferSelect6,
    BufferSelect7,
    BufferSelect8,
    BufferSelect9,
    ViewportTop,    /* Alt-h: the top screen line of the viewport */
    ViewportMiddle, /* Alt-m: the middle screen line of the viewport */
    ViewportBottom, /* Alt-l: the bottom screen line of the viewport */
    ViewportScrollDown, /* Alt-e: the viewport one screen line down */
    ViewportScrollUp,   /* Alt-y: the viewport one screen line up */
    ShowTabsToggle,     /* Alt-t: show or hide the tab characters */
    WrapIndentToggle,   /* Alt-i: the wrapped lines keep the row's indent */
    IndentGuidesToggle, /* Alt-g: show or hide the indent guides */
    BracketColorPrompt, /* Alt-c: the color the bracket match lights up with */
    CommentToggle,      /* Ctrl-/ (or Ctrl-_): comment the line or the
                            selection, or take the comment off again */
    BracketJump, /* Alt-%: jump to the partner of the bracket under
                     the cursor, the way % does in vi */

} EditorKey;

int ReadKeyPress(void);

/* reads the bytes of a bracketed paste up to its end marker; returns
   them NUL-terminated in a heap buffer the caller frees (NULL when
   the paste held nothing) and reports their count through the
   out parameter */
char *ReadPastedText(int *out_length);

#endif

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
    ShiftTab, /* Shift-Tab: the rows the selection spans take an
                   indent step back */
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
    ThemePrompt,        /* Alt-s: the color scheme the text is drawn in */
    CommentToggle,      /* Ctrl-/ (or Ctrl-_): comment the line or the
                            selection, or take the comment off again */
    BracketJump, /* Alt-%: jump to the partner of the byte under
                     the cursor — a bracket's own kind, a quote's
                     far end — the way % does in vi */
    SearchNext,  /* Alt-n: the match after the last search's cursor,
                     the way n does in vi */
    SearchPrev,  /* Alt-p: the match before it, the way N does */
    DeleteWordLeft,  /* Alt-Backspace: the word left of the cursor */
    DeleteWordRight, /* Alt-d: the word right of the cursor */
    WordComplete,    /* Alt-/: the word left of the cursor, completed
                        from the words of the buffer */
    OpenFileAtCursor, /* Alt-o: the file the cursor sits on, opened the
                         way gf does in vi */
    BrowseCreate, /* Alt-a: a name made in the directory the browse
                     lists, the listing standing again */
    BrowseRename, /* Alt-r: the name under the selection of the
                     browse moved to another name in the same
                     directory */
    BrowseCount,  /* Alt-w: the lines, the words and the bytes of
                     the name under the selection of the browse,
                     the way wc counts them */
    BrowseMark,   /* Alt-v: the name under the selection of the
                     browse marked or unmarked, the marked names
                     the copy and the move take together */
    ElectricBracketsToggle, /* Alt-(: the electric bracket pairs on
                               or off */
    SuspendEditor,   /* Alt-z: hand the terminal back to the shell and
                        stop, the way Ctrl-Z did before the terminal
                        went raw */
    MousePress,      /* a mouse button went down over the screen */
    MouseRelease,    /* the button came back up */
    MouseDrag,       /* the mouse moved with a button held down */
    MouseWheelUp,    /* the wheel scrolled a notch up */
    MouseWheelDown,  /* the wheel scrolled a notch down */

} EditorKey;

int ReadKeyPress(void);

/* the position and the button of the last mouse report: x and y are
   cells from the screen's top-left corner (the terminal counts from
   one, the editor from zero), the button 0 left, 1 middle, 2 right
   and 3 none. They hold the values of the report that came with the
   key and stay put while other keys arrive */
int MouseLastX(void);
int MouseLastY(void);
int MouseLastButton(void);

/* reads the bytes of a bracketed paste up to its end marker; returns
   them NUL-terminated in a heap buffer the caller frees (NULL when
   the paste held nothing) and reports their count through the
   out parameter */
char *ReadPastedText(int *out_length);

#endif

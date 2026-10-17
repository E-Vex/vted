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
    WordLeft,  /* Ctrl or Alt with Left, Alt-b */
    WordRight  /* Ctrl or Alt with Right, Alt-f */

} EditorKey;

int ReadKeyPress(void);

#endif
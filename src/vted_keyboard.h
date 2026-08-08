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
    ArrowDown

} EditorKey;

char ReadKeyPress(void);

#endif
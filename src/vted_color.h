#ifndef VTED_COLOR_H
#define VTED_COLOR_H

/*==========================================================
|                      Color Module                        |
|                                                          |
| The colors a person writes in a settings or theme file,   |
| and what a terminal needs to draw them. A color is kept   |
| as RGB whatever way it was spelled, and handed to the     |
| terminal in the richest form it understands: truecolor,   |
| the 256-color palette or the 16 colors every terminal     |
| has.                                                      |
==========================================================*/

#include <stddef.h>

typedef struct
{
    unsigned char red;
    unsigned char green;
    unsigned char blue;
} vted_rgb_t;

/* reads the len bytes at text as a color: "#rrggbb" or "#rgb" in
   hexadecimal, either case. Returns 0 and fills color, or -1 when the
   text is not a color, leaving color alone */
int ColorParse(const char *text, size_t len, vted_rgb_t *color);

#endif

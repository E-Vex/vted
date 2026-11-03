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
   hexadecimal, either case, or a name: black, red, green, yellow,
   blue, magenta, cyan, white, gray (or grey) and orange, or a number
   from 0 to 255 naming a color of the xterm 256-color palette. Returns 0 and fills color, or -1 when the
   text is not a color, leaving color alone */
int ColorParse(const char *text, size_t len, vted_rgb_t *color);

/* the RGB of a color of the xterm 256-color palette: 0 to 15 are the
   terminal's own colors in their usual shades, 16 to 231 a 6x6x6 cube
   and 232 to 255 a ramp of grays. An index out of range is clamped */
vted_rgb_t ColorFromPalette(int index);

/* the palette index, 16 to 255, whose color lies nearest to the given
   one: the cube or the ramp of grays, whichever comes closer. The 16
   terminal colors are left out of the match, since each terminal
   shades them as it likes */
int ColorNearest256(vted_rgb_t color);

#endif

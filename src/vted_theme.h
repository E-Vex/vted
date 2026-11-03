#ifndef VTED_THEME_H
#define VTED_THEME_H

/*==========================================================
|                      Theme Module                        |
|                                                          |
| A theme is a set of colors for the classes of text the   |
| syntax module tells apart. It is written the way the     |
| settings file is, a line per color:                       |
|                                                          |
|     color keyword #fb4934                                 |
|     color comment gray                                    |
|     color match 94                                        |
|                                                          |
| Blank lines and '#' comments in between are skipped. A    |
| theme starts from the built-in colors and names only what |
| it changes. The classes are comment, string, number,      |
| keyword, type, preproc, removed, context, function,       |
| match, tab, indent and marker; match and marker are       |
| backgrounds, the rest color the text.                     |
==========================================================*/

#include <stddef.h>

/* the syntax class a theme file calls the len bytes at name, or -1
   when the word names none */
int ThemeParseClass(const char *name, size_t len);

#endif

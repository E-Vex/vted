#ifndef VTED_CONFIG_H
#define VTED_CONFIG_H

/*==========================================================
|                   Settings Module                        |
|                                                          |
| The settings file: one "set key value" per line, blank     |
| lines and '#' comments in between. The file lives         |
| where the desktop conventions put it, and every           |
| setting it names is also a key the editor takes at        |
| runtime (Ctrl-W, Ctrl-T, Ctrl-N, Ctrl-L, Alt-t, Alt-i,    |
| Alt-g), so the rc file is just the remembered answer to   |
| those prompts.                                            |
==========================================================*/

#include <stdio.h>

typedef struct
{
    int tab_width;         /* cells between tab stops, 1 to 32 */
    int insert_spaces;     /* 1: the Tab key inserts spaces to the next stop */
    int show_line_numbers; /* 1: draw the line number gutter */
    int show_tabs;         /* 1: draw tab characters as dotted runs */
    int wrap;              /* 1: long lines flow over several screen lines */
    int wrap_column;       /* text cells that fit before the column marker,
                              0 to hide it */
    int wrap_indent;       /* 1: the lines a row wraps onto keep the row's
                              own indent at their start */
    int indent_guides;     /* 1: mark the last cell of every nesting
                              level the rows sit in with dots */
    int bracket_color;     /* the color the matching bracket lights up
                              with: 0 inverts video, the default, and
                              1 to 7 name a color */
    int ignore_case;       /* 1: the search matches letters without
                              regard to their case */
    char theme[33];        /* the name of the color theme: a theme file
                              in the themes directory or a built-in
                              one; "default" is the built-in colors */
} vted_settings_t;

/* the settings the editor starts from: the built-in defaults */
vted_settings_t SettingsDefaults(void);

/* reads the lines of the settings file into settings, which starts
   out holding the defaults; a later line wins over an earlier one.
   Returns the number of lines that could not be used (an unknown
   key or a bad value), which the caller may report */
int SettingsLoad(vted_settings_t *settings, FILE *file);

/* the path of the settings file: $XDG_CONFIG_HOME/vted/vtedrc or
   ~/.config/vted/vtedrc. NULL when no home can be found. The buffer
   is static and reused on every call */
const char *SettingsPath(void);

/* the color a bracket-color answer names: 0 to 7, or -1 when the
   answer names none. The rc file and the Alt-c prompt share the
   names: invert, red, green, yellow, blue, magenta, cyan, gray */
int SettingsParseBracketColor(const char *name);

/* the name of a bracket color, the word the rc file and the Alt-c
   prompt write for it; a color out of range answers with the first */
const char *SettingsBracketColorName(int color);

#endif

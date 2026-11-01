#ifndef VTED_CONFIG_H
#define VTED_CONFIG_H

/*==========================================================
|                   Settings Module                        |
|                                                          |
| The settings file: one "set key value" per line, blank     |
| lines and '#' comments in between. The file lives         |
| where the desktop conventions put it, and every           |
| setting it names is also a key the editor takes at        |
| runtime (Ctrl-W, Ctrl-T, Ctrl-N), so the rc file is       |
| just the remembered answer to those prompts.              |
==========================================================*/

#include <stdio.h>

typedef struct
{
    int tab_width;         /* cells between tab stops, 1 to 32 */
    int insert_spaces;     /* 1: the Tab key inserts spaces to the next stop */
    int show_line_numbers; /* 1: draw the line number gutter */
    int wrap;              /* 1: long lines flow over several screen lines */
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

#endif

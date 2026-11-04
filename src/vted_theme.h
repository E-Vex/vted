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

/* reads the lines of a theme, a NUL-terminated text, and gives the
   classes they name the colors they give. Nothing is taken back first,
   so a later line wins over an earlier one and the colors already
   chosen stay where a line leaves them alone. Returns the number of
   lines that could not be used (an unknown class, a word that is no
   color, a line that is none of these), which the caller may report;
   the other lines still apply */
int ThemeApply(const char *text);

/* is the word fit to be the name of a theme? 1 to 32 letters, digits,
   '-' and '_' and nothing else, so a name can never reach out of the
   themes directory: no '/', no dots, no spaces */
int ThemeNameValid(const char *name);

/* makes the named theme the one in use: the colors of whichever theme
   was before are taken back, and the named one's are given. The name is
   a file NAME.theme in the themes directory, or failing that one of
   the built-in themes, so a file of the person's own can stand in for a
   built-in of the same name. A file may be up to 16 KiB. Returns 0, or
   -1 when the name is neither (or not fit to be a name, or the file is
   too large), in which case the theme in use is left as it was */
int ThemeSelect(const char *name);

/* how many lines of the theme the last successful ThemeSelect chose
   could not be used (an unknown class, a word that is no color); the
   lines that could be used applied all the same. 0 for the built-in
   themes, which are all well formed */
int ThemeBadLines(void);

/* the name of the theme in use: "default" until another is selected */
const char *ThemeCurrent(void);

/* the built-in themes, by number from 0: their count, and the name of
   one (NULL when the number is out of range) */
int ThemeBuiltinCount(void);
const char *ThemeBuiltinName(int index);

/* the text of a built-in theme, written the way a theme file is, so it
   can serve as the start of one; NULL when the number is out of range */
const char *ThemeBuiltinText(int index);

/* the directory the themes of the person live in, beside the settings
   file: $XDG_CONFIG_HOME/vted/themes or ~/.config/vted/themes. NULL
   when no home can be found. The buffer is static and reused on every
   call */
const char *ThemesDir(void);

#endif

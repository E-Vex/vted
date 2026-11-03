/*===============================*/
#include <stdio.h>
#include <string.h>
/*===============================*/
#include "vted_theme.h"
#include "vted_color.h"
#include "vted_config.h"
#include "vted_syntax.h"
/*===============================*/

/* the words a theme file names the classes of text by */
static const struct
{
    const char *name;
    int hl_class;
} class_names[] = {
    {"comment", HlComment},
    {"string", HlString},
    {"number", HlNumber},
    {"keyword", HlKeyword},
    {"type", HlType},
    {"preproc", HlPreproc},
    {"removed", HlRemoved},
    {"context", HlContext},
    {"function", HlFunction},
    {"match", HlMatch},
    {"tab", HlTab},
    {"indent", HlIndent},
    {"marker", HlMarker}
};

int ThemeParseClass(const char *name, size_t len)
{
    size_t i;

    for (i = 0; i < sizeof(class_names) / sizeof(class_names[0]); i++)
    {
        if (strlen(class_names[i].name) == len &&
            memcmp(class_names[i].name, name, len) == 0)
        {
            return class_names[i].hl_class;
        }
    }

    return -1;
}

static int IsBlank(char c)
{
    return c == ' ' || c == '\t';
}

/* the end of the word that starts at at, within end */
static const char *WordEnd(const char *at, const char *end)
{
    while (at < end && !IsBlank(*at))
    {
        at++;
    }

    return at;
}

static const char *SkipBlanks(const char *at, const char *end)
{
    while (at < end && IsBlank(*at))
    {
        at++;
    }

    return at;
}

/* applies one line, the len bytes at line; 1 when it could not be used */
static int ApplyLine(const char *line, size_t len)
{
    const char *at = line;
    const char *end = line + len;

    /* a Windows line ending and trailing blanks are nothing to read */
    while (end > at && (IsBlank(end[-1]) || end[-1] == '\r'))
    {
        end--;
    }

    at = SkipBlanks(at, end);

    /* blank lines and comments */
    if (at == end || *at == '#')
    {
        return 0;
    }

    /* every other line gives a color: color <class> <color> */
    const char *word_end = WordEnd(at, end);

    if (word_end - at != 5 || memcmp(at, "color", 5) != 0)
    {
        return 1;
    }

    at = SkipBlanks(word_end, end);
    word_end = WordEnd(at, end);

    int hl_class = ThemeParseClass(at, (size_t)(word_end - at));

    if (hl_class < 0)
    {
        return 1;
    }

    /* the color is the rest of the line */
    at = SkipBlanks(word_end, end);

    vted_rgb_t color;

    if (ColorParse(at, (size_t)(end - at), &color) != 0)
    {
        return 1;
    }

    return SyntaxSetClassColor(hl_class, color) != 0;
}

int ThemeApply(const char *text)
{
    int bad = 0;

    while (*text != '\0')
    {
        const char *newline = strchr(text, '\n');
        size_t len = newline != NULL ? (size_t)(newline - text) : strlen(text);

        bad += ApplyLine(text, len);
        text += len;

        if (*text == '\n')
        {
            text++;
        }
    }

    return bad;
}

int ThemeNameValid(const char *name)
{
    size_t len = strlen(name);
    size_t i;

    if (len < 1 || len > 32)
    {
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_'))
        {
            return 0;
        }
    }

    return 1;
}

/* the themes that come with the editor, each a theme text of its own.
   The first, default, names no color: it is the built-in colors */
static const struct
{
    const char *name;
    const char *text;
} builtin_themes[] = {
    {"default", ""},
    {"gruvbox",
        "color comment  #928374\n"
        "color string   #b8bb26\n"
        "color number   #d3869b\n"
        "color keyword  #fb4934\n"
        "color type     #fabd2f\n"
        "color preproc  #8ec07c\n"
        "color removed  #fb4934\n"
        "color context  #7c6f64\n"
        "color function #83a598\n"
        "color match    #7c5e10\n"
        "color tab      #665c54\n"
        "color indent   #504945\n"
        "color marker   #3c3836\n"
    },
    {"nord",
        "color comment  #616e88\n"
        "color string   #a3be8c\n"
        "color number   #b48ead\n"
        "color keyword  #81a1c1\n"
        "color type     #8fbcbb\n"
        "color preproc  #5e81ac\n"
        "color removed  #bf616a\n"
        "color context  #4c566a\n"
        "color function #88c0d0\n"
        "color match    #6b5b2e\n"
        "color tab      #4c566a\n"
        "color indent   #434c5e\n"
        "color marker   #3b4252\n"
    },
    {"dracula",
        "color comment  #6272a4\n"
        "color string   #f1fa8c\n"
        "color number   #bd93f9\n"
        "color keyword  #ff79c6\n"
        "color type     #8be9fd\n"
        "color preproc  #ffb86c\n"
        "color removed  #ff5555\n"
        "color context  #6b7090\n"
        "color function #50fa7b\n"
        "color match    #6f5f1a\n"
        "color tab      #6272a4\n"
        "color indent   #44475a\n"
        "color marker   #343746\n"
    },
    {"solarized-dark",
        "color comment  #586e75\n"
        "color string   #2aa198\n"
        "color number   #d33682\n"
        "color keyword  #859900\n"
        "color type     #b58900\n"
        "color preproc  #cb4b16\n"
        "color removed  #dc322f\n"
        "color context  #586e75\n"
        "color function #268bd2\n"
        "color match    #5f5200\n"
        "color tab      #586e75\n"
        "color indent   #12404c\n"
        "color marker   #073642\n"
    },
    {"light",
        "color comment  #6a737d\n"
        "color string   #22863a\n"
        "color number   #005cc5\n"
        "color keyword  #d73a49\n"
        "color type     #6f42c1\n"
        "color preproc  #986801\n"
        "color removed  #cb2431\n"
        "color context  #959da5\n"
        "color function #e36209\n"
        "color match    #ffdf5d\n"
        "color tab      #959da5\n"
        "color indent   #d0d7de\n"
        "color marker   #eaeef2\n"
    }
};

static char current_theme[33] = "default";
static int bad_lines;

int ThemeBuiltinCount(void)
{
    return (int)(sizeof(builtin_themes) / sizeof(builtin_themes[0]));
}

const char *ThemeBuiltinName(int index)
{
    if (index < 0 || index >= ThemeBuiltinCount())
    {
        return NULL;
    }

    return builtin_themes[index].name;
}

const char *ThemeBuiltinText(int index)
{
    if (index < 0 || index >= ThemeBuiltinCount())
    {
        return NULL;
    }

    return builtin_themes[index].text;
}

int ThemeBadLines(void)
{
    return bad_lines;
}

const char *ThemeCurrent(void)
{
    return current_theme;
}

/* the most a theme file may hold: a theme names a dozen colors, so a
   file past this is not one */
#define THEME_FILE_MAX 16384

/* reads NAME.theme from the themes directory into text, which holds
   THEME_FILE_MAX + 1 bytes. 1 when the file was read, 0 when there is
   none to read, -1 when it is too large */
static int ReadThemeFile(const char *name, char *text)
{
    const char *dir = ThemesDir();
    char path[600];

    if (dir == NULL)
    {
        return 0;
    }

    int written = snprintf(path, sizeof(path), "%s/%s.theme", dir, name);

    if (written < 0 || (size_t)written >= sizeof(path))
    {
        return 0;
    }

    FILE *file = fopen(path, "r");

    if (file == NULL)
    {
        return 0;
    }

    size_t got = fread(text, 1, THEME_FILE_MAX + 1, file);

    fclose(file);

    if (got > THEME_FILE_MAX)
    {
        return -1;
    }

    text[got] = '\0';
    return 1;
}

int ThemeSelect(const char *name)
{
    static char file_text[THEME_FILE_MAX + 1];
    int i;

    if (!ThemeNameValid(name))
    {
        return -1;
    }

    /* the person's own file comes first; the colors of the theme in
       use are only taken back once the new one is known to be there */
    int found = ReadThemeFile(name, file_text);

    if (found < 0)
    {
        return -1;
    }

    if (found > 0)
    {
        SyntaxResetClassColors();
        bad_lines = ThemeApply(file_text);
        snprintf(current_theme, sizeof(current_theme), "%s", name);
        return 0;
    }

    for (i = 0; i < ThemeBuiltinCount(); i++)
    {
        if (strcmp(builtin_themes[i].name, name) == 0)
        {
            SyntaxResetClassColors();
            bad_lines = ThemeApply(builtin_themes[i].text);
            snprintf(current_theme, sizeof(current_theme), "%s", name);
            return 0;
        }
    }

    return -1;
}

const char *ThemesDir(void)
{
    static char dir[512];
    const char *settings = SettingsPath();

    if (settings == NULL)
    {
        return NULL;
    }

    /* the directory of the settings file, with themes inside it */
    const char *slash = strrchr(settings, '/');

    if (slash == NULL)
    {
        return NULL;
    }

    int written = snprintf(dir, sizeof(dir), "%.*s/themes",
                           (int)(slash - settings), settings);

    if (written < 0 || (size_t)written >= sizeof(dir))
    {
        return NULL;
    }

    return dir;
}

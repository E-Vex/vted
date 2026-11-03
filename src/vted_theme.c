/*===============================*/
#include <string.h>
/*===============================*/
#include "vted_theme.h"
#include "vted_color.h"
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

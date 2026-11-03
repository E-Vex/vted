/* setenv() is POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_theme.h"
#include "src/vted_syntax.h"
/*===============================*/

static int Class(const char *name)
{
    return ThemeParseClass(name, strlen(name));
}

static void TestClassNames(void)
{
    CHECK_INT(Class("comment"), HlComment);
    CHECK_INT(Class("string"), HlString);
    CHECK_INT(Class("number"), HlNumber);
    CHECK_INT(Class("keyword"), HlKeyword);
    CHECK_INT(Class("type"), HlType);
    CHECK_INT(Class("preproc"), HlPreproc);
    CHECK_INT(Class("removed"), HlRemoved);
    CHECK_INT(Class("context"), HlContext);
    CHECK_INT(Class("function"), HlFunction);
    CHECK_INT(Class("match"), HlMatch);
    CHECK_INT(Class("tab"), HlTab);
    CHECK_INT(Class("indent"), HlIndent);
    CHECK_INT(Class("marker"), HlMarker);

    /* normal text and the bracket match are not named: a theme cannot
       color them */
    CHECK_INT(Class("normal"), -1);
    CHECK_INT(Class("bracket"), -1);

    /* whole lower-case words only */
    CHECK_INT(Class("Keyword"), -1);
    CHECK_INT(Class("keywords"), -1);
    CHECK_INT(Class("keyw"), -1);
    CHECK_INT(Class(""), -1);

    /* the length bounds the word */
    CHECK_INT(ThemeParseClass("keywordx", 7), HlKeyword);
    CHECK_INT(ThemeParseClass("keyword", 3), -1);
}

static void TestApply(void)
{
    SyntaxSetColorMode(2);
    SyntaxResetClassColors();

    /* a line gives a class a color: hex, a name or a palette number */
    CHECK_INT(ThemeApply("color keyword #fb4934\n"
                         "color string green\n"
                         "color number 208\n"),
              0);
    CHECK_STR(SyntaxStyle(HlKeyword), "\x1b[38;2;251;73;52m");
    CHECK_STR(SyntaxStyle(HlString), "\x1b[38;2;0;170;0m");
    CHECK_STR(SyntaxStyle(HlNumber), "\x1b[38;2;255;135;0m");

    /* the classes drawn as a background stay backgrounds */
    CHECK_INT(ThemeApply("color match #444444\n"), 0);
    CHECK_STR(SyntaxStyle(HlMatch), "\x1b[48;2;68;68;68m");

    /* blank lines, comments, indentation, blanks around the words, a
       Windows line ending and no newline at the end are all fine */
    SyntaxResetClassColors();
    CHECK_INT(ThemeApply("\n"
                         "# a comment\n"
                         "   # an indented one\n"
                         "  color\tcomment   #112233  \r\n"
                         "color type #fff"),
              0);
    CHECK_STR(SyntaxStyle(HlComment), "\x1b[38;2;17;34;51m");
    CHECK_STR(SyntaxStyle(HlType), "\x1b[38;2;255;255;255m");

    /* a later line wins over an earlier one */
    CHECK_INT(ThemeApply("color comment #000000\ncolor comment #ffffff\n"), 0);
    CHECK_STR(SyntaxStyle(HlComment), "\x1b[38;2;255;255;255m");

    /* lines that cannot be used are counted, and the rest still apply */
    SyntaxResetClassColors();
    CHECK_INT(ThemeApply("color keyword #ff0000\n"
                         "color bogus #00ff00\n"
                         "color string notacolor\n"
                         "color number\n"
                         "color\n"
                         "keyword #00ff00\n"
                         "colour keyword red\n"
                         "color keyword #ff00\n"
                         "color type #0000ff\n"),
              7);
    CHECK_STR(SyntaxStyle(HlKeyword), "\x1b[38;2;255;0;0m");
    CHECK_STR(SyntaxStyle(HlType), "\x1b[38;2;0;0;255m");
    CHECK_STR(SyntaxStyle(HlString), "\x1b[38;2;0;170;0m"); /* untouched */

    /* normal text and the bracket match cannot be colored */
    CHECK_INT(ThemeApply("color normal red\ncolor bracket red\n"), 2);

    /* nothing at all is a theme that changes nothing */
    CHECK_INT(ThemeApply(""), 0);

    SyntaxResetClassColors();
    SyntaxSetColorMode(-1);
}

static void TestNameValid(void)
{
    CHECK_INT(ThemeNameValid("gruvbox"), 1);
    CHECK_INT(ThemeNameValid("solarized-dark"), 1);
    CHECK_INT(ThemeNameValid("my_theme_2"), 1);
    CHECK_INT(ThemeNameValid("A"), 1);

    /* the longest name there is */
    CHECK_INT(ThemeNameValid("abcdefghijklmnopqrstuvwxyz012345"), 1);
    CHECK_INT(ThemeNameValid("abcdefghijklmnopqrstuvwxyz0123456"), 0);

    /* nothing that could leave the themes directory, or confuse a
       file name */
    CHECK_INT(ThemeNameValid(""), 0);
    CHECK_INT(ThemeNameValid("."), 0);
    CHECK_INT(ThemeNameValid(".."), 0);
    CHECK_INT(ThemeNameValid("../etc/passwd"), 0);
    CHECK_INT(ThemeNameValid("/etc/passwd"), 0);
    CHECK_INT(ThemeNameValid("a/b"), 0);
    CHECK_INT(ThemeNameValid("a\\b"), 0);
    CHECK_INT(ThemeNameValid("a.theme"), 0);
    CHECK_INT(ThemeNameValid("a b"), 0);
    CHECK_INT(ThemeNameValid("a\n"), 0);
    CHECK_INT(ThemeNameValid("caf\xc3\xa9"), 0);
}

/* the color each built-in theme gives a keyword, in truecolor: the
   mark of the theme, which a theme that lost its lines would not carry */
static void TestBuiltinThemes(void)
{
    static const struct
    {
        const char *name;
        const char *keyword;
    } builtin_keywords[] = {
        {"gruvbox", "\x1b[38;2;251;73;52m"},
        {"nord", "\x1b[38;2;129;161;193m"},
        {"dracula", "\x1b[38;2;255;121;198m"},
        {"solarized-dark", "\x1b[38;2;133;153;0m"},
        {"light", "\x1b[38;2;215;58;73m"}
    };
    size_t i;

    SyntaxSetColorMode(2);

    for (i = 0; i < sizeof(builtin_keywords) / sizeof(builtin_keywords[0]); i++)
    {
        CHECK_INT(ThemeSelect(builtin_keywords[i].name), 0);
        CHECK_STR(ThemeCurrent(), builtin_keywords[i].name);
        CHECK_STR(SyntaxStyle(HlKeyword), builtin_keywords[i].keyword);

        /* a theme colors the text it names and leaves the rest of the
           built-in colors alone, and selecting default takes it back */
        CHECK_INT(ThemeSelect("default"), 0);
        CHECK_STR(SyntaxStyle(HlKeyword), "\x1b[38;2;170;130;0m");
    }

    SyntaxSetColorMode(-1);
}

static void TestThemesDir(void)
{
    /* beside the settings file, wherever that is */
    setenv("XDG_CONFIG_HOME", "/tmp/xdg-config", 1);
    CHECK_STR(ThemesDir(), "/tmp/xdg-config/vted/themes");

    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/home/someone", 1);
    CHECK_STR(ThemesDir(), "/home/someone/.config/vted/themes");

    /* an empty XDG_CONFIG_HOME is no config home */
    setenv("XDG_CONFIG_HOME", "", 1);
    CHECK_STR(ThemesDir(), "/home/someone/.config/vted/themes");

    /* no home at all, no directory */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("HOME");
    CHECK(ThemesDir() == NULL);
}

static void TestSelect(void)
{
    SyntaxSetColorMode(2);
    SyntaxResetClassColors();

    /* default is where it starts, and selecting it is the built-in
       colors */
    CHECK_STR(ThemeCurrent(), "default");
    CHECK_INT(ThemeSelect("default"), 0);
    CHECK_STR(ThemeCurrent(), "default");
    CHECK_STR(SyntaxStyle(HlKeyword), "\x1b[38;2;170;130;0m");

    /* selecting default takes a color chosen by hand back too */
    ThemeApply("color keyword #ff0000\n");
    CHECK_STR(SyntaxStyle(HlKeyword), "\x1b[38;2;255;0;0m");
    CHECK_INT(ThemeSelect("default"), 0);
    CHECK_STR(SyntaxStyle(HlKeyword), "\x1b[38;2;170;130;0m");

    /* a name that is no theme, or no name at all, fails and changes
       nothing */
    ThemeApply("color keyword #ff0000\n");
    CHECK_INT(ThemeSelect("nonesuch"), -1);
    CHECK_INT(ThemeSelect("../default"), -1);
    CHECK_INT(ThemeSelect(""), -1);
    CHECK_STR(ThemeCurrent(), "default");
    CHECK_STR(SyntaxStyle(HlKeyword), "\x1b[38;2;255;0;0m");

    /* the built-in themes can be listed, and each of them selected */
    int i;

    CHECK(ThemeBuiltinCount() >= 1);
    CHECK_STR(ThemeBuiltinName(0), "default");
    CHECK(ThemeBuiltinName(-1) == NULL);
    CHECK(ThemeBuiltinName(ThemeBuiltinCount()) == NULL);

    for (i = 0; i < ThemeBuiltinCount(); i++)
    {
        CHECK_INT(ThemeSelect(ThemeBuiltinName(i)), 0);
        CHECK_STR(ThemeCurrent(), ThemeBuiltinName(i));
    }

    /* every built-in theme is written correctly: not one of its lines
       is refused, and its text is not empty unless it is the default */
    for (i = 0; i < ThemeBuiltinCount(); i++)
    {
        SyntaxResetClassColors();
        CHECK(ThemeBuiltinText(i) != NULL);
        CHECK_INT(ThemeApply(ThemeBuiltinText(i)), 0);

        if (strcmp(ThemeBuiltinName(i), "default") != 0)
        {
            CHECK(strlen(ThemeBuiltinText(i)) > 0);
        }
    }

    CHECK(ThemeBuiltinText(-1) == NULL);
    CHECK(ThemeBuiltinText(ThemeBuiltinCount()) == NULL);

    SyntaxResetClassColors();
    SyntaxSetColorMode(-1);
    ThemeSelect("default");
}

int main(void)
{
    TestThemesDir();
    TestSelect();
    TestBuiltinThemes();
    TestNameValid();
    TestClassNames();
    TestApply();

    return TestSummary("theme");
}

/* setenv() and fmemopen() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_config.h"
#include "src/vted_row.h"
/*===============================*/

/* loads the lines into fresh defaults the way the editor does at
   startup, and gives back how many lines were not understood */
static int LoadLines(const char *text, vted_settings_t *settings)
{
    FILE *file = fmemopen((void *)text, strlen(text), "r");

    if (file == NULL)
    {
        fprintf(stderr, "  fmemopen failed\n");
        _exit(2);
    }

    *settings = SettingsDefaults();
    int bad = SettingsLoad(settings, file);

    fclose(file);
    return bad;
}

static void TestDefaults(void)
{
    vted_settings_t settings = SettingsDefaults();

    CHECK_INT(settings.tab_width, VTED_TAB_STOP);
    CHECK_INT(settings.insert_spaces, 0);
    CHECK_INT(settings.show_line_numbers, 1);
    CHECK_INT(settings.wrap, 0);
}

static void TestTabWidth(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set tab-width 4\n", &settings), 0);
    CHECK_INT(settings.tab_width, 4);

    CHECK_INT(LoadLines("set tab-width 1\n", &settings), 0);
    CHECK_INT(settings.tab_width, 1);

    CHECK_INT(LoadLines("set tab-width 32\n", &settings), 0);
    CHECK_INT(settings.tab_width, 32);

    /* the bounds the Ctrl-W prompt takes */
    CHECK_INT(LoadLines("set tab-width 0\n", &settings), 1);
    CHECK_INT(settings.tab_width, VTED_TAB_STOP);

    CHECK_INT(LoadLines("set tab-width 33\n", &settings), 1);
    CHECK_INT(settings.tab_width, VTED_TAB_STOP);

    CHECK_INT(LoadLines("set tab-width wide\n", &settings), 1);
    CHECK_INT(settings.tab_width, VTED_TAB_STOP);
}

static void TestInsertSpaces(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set insert-spaces on\n", &settings), 0);
    CHECK_INT(settings.insert_spaces, 1);

    CHECK_INT(LoadLines("set insert-spaces off\n", &settings), 0);
    CHECK_INT(settings.insert_spaces, 0);

    /* anything else is not a setting */
    CHECK_INT(LoadLines("set insert-spaces maybe\n", &settings), 1);
    CHECK_INT(settings.insert_spaces, 0);
}

static void TestLineNumbers(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set line-numbers off\n", &settings), 0);
    CHECK_INT(settings.show_line_numbers, 0);

    CHECK_INT(LoadLines("set line-numbers on\n", &settings), 0);
    CHECK_INT(settings.show_line_numbers, 1);

    CHECK_INT(LoadLines("set line-numbers\n", &settings), 1);
    CHECK_INT(settings.show_line_numbers, 1);
}

static void TestWrap(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set wrap on\n", &settings), 0);
    CHECK_INT(settings.wrap, 1);

    CHECK_INT(LoadLines("set wrap off\n", &settings), 0);
    CHECK_INT(settings.wrap, 0);

    /* anything else is not a setting */
    CHECK_INT(LoadLines("set wrap sometimes\n", &settings), 1);
    CHECK_INT(settings.wrap, 0);
}

static void TestCommentsAndBlankLines(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("# a comment\n\n   \nset tab-width 2\n# another\n",
                        &settings),
              0);
    CHECK_INT(settings.tab_width, 2);
}

static void TestUnknownKeyIsCountedBad(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set colorscheme dark\n", &settings), 1);
    CHECK_INT(settings.tab_width, VTED_TAB_STOP);

    CHECK_INT(LoadLines("colorscheme dark\n", &settings), 1);
    CHECK_INT(settings.insert_spaces, 0);
}

static void TestALaterLineWins(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set tab-width 4\nset tab-width 8\n", &settings), 0);
    CHECK_INT(settings.tab_width, 8);
}

static void TestSpacesAroundTheWords(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("  set\ttab-width\t6  \n", &settings), 0);
    CHECK_INT(settings.tab_width, 6);
}

static void TestEmptyFile(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("", &settings), 0);
    CHECK_INT(settings.tab_width, VTED_TAB_STOP);
    CHECK_INT(settings.insert_spaces, 0);
    CHECK_INT(settings.show_line_numbers, 1);
    CHECK_INT(settings.wrap, 0);
}

static void TestLastLineWithoutNewline(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set insert-spaces on", &settings), 0);
    CHECK_INT(settings.insert_spaces, 1);
}

static void TestCarriageReturnEndings(void)
{
    vted_settings_t settings;

    CHECK_INT(LoadLines("set tab-width 4\r\nset insert-spaces on\r\n", &settings), 0);
    CHECK_INT(settings.tab_width, 4);
    CHECK_INT(settings.insert_spaces, 1);
}

static void TestPathUsesTheConfigHome(void)
{
    setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
    CHECK_STR(SettingsPath(), "/tmp/xdg/vted/vtedrc");
    unsetenv("XDG_CONFIG_HOME");
}

static void TestPathFallsBackToTheHomeDirectory(void)
{
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/home/editor", 1);
    CHECK_STR(SettingsPath(), "/home/editor/.config/vted/vtedrc");
}

static void TestPathWithoutEitherIsUnknown(void)
{
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("HOME");
    CHECK(SettingsPath() == NULL);
}

int main(void)
{
    TestDefaults();
    TestTabWidth();
    TestInsertSpaces();
    TestLineNumbers();
    TestWrap();
    TestCommentsAndBlankLines();
    TestUnknownKeyIsCountedBad();
    TestALaterLineWins();
    TestSpacesAroundTheWords();
    TestEmptyFile();
    TestLastLineWithoutNewline();
    TestCarriageReturnEndings();
    TestPathUsesTheConfigHome();
    TestPathFallsBackToTheHomeDirectory();
    TestPathWithoutEitherIsUnknown();
    return TestSummary("config");
}

/* _exit() is POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_screen.h"
#include "src/vted_row.h"
#include "src/vted_keyboard.h"
/*===============================*/

/* each check starts from a fresh buffer whose one row wraps over
   lines of cols cells, the way the viewport lays it out */
static void BeginWrapped(const char *text, int cols)
{
    FreeBuffer();

    if (text != NULL && text[0] != '\0')
    {
        AppendRow(text, (int)strlen(text), VTED_EOL_LF);
    }

    vted_editor_config.screen_rows = 24;
    vted_editor_config.text_cols = cols;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;
}

static void CursorAt(int y, int x)
{
    vted_editor_config.cursor_y = y;
    vted_editor_config.cursor_x = x;
}

static void TestHomeStartsTheScreenLine(void)
{
    /* abcdefghijkl over 4 cells: abcd / efgh / ijkl */
    BeginWrapped("abcdefghijkl", 4);

    CursorAt(0, 8); /* on the third line, between i and j */
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 8);

    CursorAt(0, 10); /* between k and l */
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 8);

    CursorAt(0, 5); /* on the second line */
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    /* without wrapping, Home is the start of the whole row */
    vted_editor_config.wrap = 0;
    CursorAt(0, 10);
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestHomeFromPastTheEnd(void)
{
    /* the position past the row's last glyph rests on the last line,
       so Home starts that one */
    BeginWrapped("abcdefghijkl", 4);

    CursorAt(0, 12);
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 8);
}

static void TestHomeBeforeAWideGlyphMovedDown(void)
{
    /* abc plus a wide ideograph at 4 cells: the ideograph starts the
       second line whole, x follows it */
    BeginWrapped("abc\xE4\xB8\xAD" "x", 4);

    CursorAt(0, 6); /* x, on the second line */
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 3); /* the ideograph */

    CursorAt(0, 1); /* b, on the first line */
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestEndEndsTheScreenLine(void)
{
    BeginWrapped("abcdefghijkl", 4);

    CursorAt(0, 5); /* on the second line */
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 8); /* one past its last byte */

    CursorAt(0, 0);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    /* the last line ends at the end of the row */
    CursorAt(0, 9);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 12);

    /* from past the end, End stays put */
    CursorAt(0, 12);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 12);

    /* without wrapping, End is the end of the whole row */
    vted_editor_config.wrap = 0;
    CursorAt(0, 5);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 12);
}

static void TestEndBeforeAWideGlyphMovedDown(void)
{
    /* the glyph that does not fit a line starts the next one, so the
       line ends before it */
    BeginWrapped("abc\xE4\xB8\xAD" "x", 4);

    CursorAt(0, 0);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 3); /* the ideograph */

    CursorAt(0, 3); /* the ideograph itself, on the second line */
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 7); /* the end of the row */
}

static void TestEndBeforeATabThatCrossesTheEdge(void)
{
    /* a tab at cell 2 runs to the stop at cell 8: at 4 cells a line
       its blank space covers the rest of the first line, and the
       cursor cannot sit past it on that line, so End leaves it on
       the tab itself */
    BeginWrapped("ab\tc", 4);
    RowSetTabWidth(8);

    CursorAt(0, 0);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 2);

    RowSetTabWidth(VTED_TAB_STOP);
}

static void TestShiftHomeAndEndMoveTheSameWay(void)
{
    /* the shift variants extend a selection with the same move */
    BeginWrapped("abcdefghijkl", 4);

    CursorAt(0, 5);
    MoveCursor(ShiftHome);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    CursorAt(0, 5);
    MoveCursor(ShiftEnd);
    CHECK_INT(vted_editor_config.cursor_x, 8);
}

static void TestHomeAndEndOnAnEmptyRow(void)
{
    BeginWrapped("", 4);

    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

/* one row of the given cells, wrapping over lines of 4: cells/4
   screen lines */
static void BeginTallRow(int cells)
{
    char *text = malloc((size_t)cells + 1);

    if (text == NULL)
    {
        fprintf(stderr, "  malloc failed\n");
        _exit(2);
    }

    memset(text, 'x', (size_t)cells);
    text[cells] = '\0';

    FreeBuffer();
    AppendRow(text, cells, VTED_EOL_LF);
    free(text);

    vted_editor_config.screen_rows = 10;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;
}

static void TestDeepCursorScrollsIntoATallRow(void)
{
    /* 240 cells over lines of 4: 60 screen lines for a viewport of
       10, so the row is taller than the viewport */
    BeginTallRow(240);

    /* byte 160 is line 40: the row becomes the top, entered at the
       line that puts the cursor on the last viewport line */
    CursorAt(0, 160);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 31);

    /* byte 200 is line 50 */
    CursorAt(0, 200);
    UpdateScroll();

    CHECK_INT(vted_editor_config.line_offset, 41);

    /* a shallow cursor, line 2, scrolls the row back up to it */
    CursorAt(0, 8);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 2);

    /* the top of the row again */
    CursorAt(0, 0);
    UpdateScroll();

    CHECK_INT(vted_editor_config.line_offset, 0);
}

static void TestCursorOnTheTopLinesOfATallRow(void)
{
    /* a cursor within the first viewport of a tall row leaves the
       viewport at the row's own top */
    BeginTallRow(240);

    CursorAt(0, 20); /* line 5 */
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 0);
}

static void TestPastTheEndScrollsToTheLastLine(void)
{
    /* the position past the row's last glyph rests on the row's last
       line, and the viewport follows it there */
    BeginTallRow(240);

    CursorAt(0, 240);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 50); /* line 59 at the bottom */
}

static void TestTallRowFallsAwayForARowBelow(void)
{
    /* a row taller than the viewport cannot share it with another
       one: a cursor on the row below makes that row the top */
    BeginTallRow(240);
    AppendRow("short", 5, VTED_EOL_LF);

    CursorAt(1, 3);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 1);
    CHECK_INT(vted_editor_config.line_offset, 0);

    /* and back up: the tall row is the top again, from its own top */
    CursorAt(0, 160);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 31);
}

static void TestStaleLineOffsetResets(void)
{
    /* a row that no longer has the lines the offset skips gives the
       stale offset up */
    BeginTallRow(240);

    CursorAt(0, 160);
    UpdateScroll();
    CHECK_INT(vted_editor_config.line_offset, 31);

    /* the row loses all but one line under an edit */
    RowTruncate(&vted_buffer.rows[0], 4);

    CursorAt(0, 2);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 0);
}

static void TestNormalRowsKeepALineOffsetOfZero(void)
{
    /* rows of one line each: the offset stays at zero while the
       viewport scrolls over whole rows */
    FreeBuffer();
    InitBuffer();
    AppendRow("one", 3, VTED_EOL_LF);
    AppendRow("two", 3, VTED_EOL_LF);
    AppendRow("tre", 3, VTED_EOL_LF);
    AppendRow("fur", 3, VTED_EOL_LF);
    AppendRow("fiv", 3, VTED_EOL_LF);

    vted_editor_config.screen_rows = 3;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;

    CursorAt(4, 2);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 2);
    CHECK_INT(vted_editor_config.line_offset, 0);

    /* a cursor above the viewport puts its row on top */
    CursorAt(0, 1);
    UpdateScroll();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 0);
}

/* rows of one line each, no wrapping: the plain layout */
static void BeginPlain(int rows)
{
    int i;

    FreeBuffer();
    InitBuffer();

    for (i = 0; i < rows; i++)
    {
        AppendRow("row", 3, VTED_EOL_LF);
    }

    vted_editor_config.screen_rows = 4;
    vted_editor_config.text_cols = 40;
    vted_editor_config.wrap = 0;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;
}

static void TestCenteringPutsTheCursorMidViewport(void)
{
    BeginPlain(10);

    /* a cursor below the viewport: the top goes up until the cursor
       sits on the middle line */
    CursorAt(8, 1);
    ScreenCenterCursor();
    CHECK_INT(vted_editor_config.row_offset, 6);

    /* a cursor already on screen keeps the viewport where it is */
    CursorAt(7, 1);
    ScreenCenterCursor();
    CHECK_INT(vted_editor_config.row_offset, 6);

    /* one above puts its row on top */
    CursorAt(1, 1);
    ScreenCenterCursor();
    CHECK_INT(vted_editor_config.row_offset, 0);

    /* the screen never opens empty below the last row */
    CursorAt(9, 1);
    ScreenCenterCursor();
    CHECK_INT(vted_editor_config.row_offset, 6);
}

int main(void)
{
    TestHomeStartsTheScreenLine();
    TestHomeFromPastTheEnd();
    TestHomeBeforeAWideGlyphMovedDown();
    TestEndEndsTheScreenLine();
    TestEndBeforeAWideGlyphMovedDown();
    TestEndBeforeATabThatCrossesTheEdge();
    TestShiftHomeAndEndMoveTheSameWay();
    TestHomeAndEndOnAnEmptyRow();
    TestDeepCursorScrollsIntoATallRow();
    TestCursorOnTheTopLinesOfATallRow();
    TestPastTheEndScrollsToTheLastLine();
    TestTallRowFallsAwayForARowBelow();
    TestStaleLineOffsetResets();
    TestNormalRowsKeepALineOffsetOfZero();
    TestCenteringPutsTheCursorMidViewport();

    return TestSummary("screen");
}

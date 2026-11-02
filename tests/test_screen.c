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

static void TestHomeAndEndOnWordWrappedLines(void)
{
    /* "aaaa b cc" over lines of 4 cells: line 1 holds " b " and a
       cell of padding, line 2 "cc". The lines end where the words
       moved from, so their ends sit on the wrapped layout and not in
       the middle of the row's cell stream */
    BeginWrapped("aaaa b cc", 4);

    /* Home on the second line: its first byte is the blank */
    CursorAt(0, 5); /* b, on the second line */
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    /* End of the first line: the first byte of the second one */
    CursorAt(0, 0);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    /* End of the second line: the first byte of the third */
    CursorAt(0, 5);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 7);

    /* End of the last line: the end of the row */
    CursorAt(0, 7);
    MoveCursor(EndKey);
    CHECK_INT(vted_editor_config.cursor_x, 9);

    /* Home from the third line: its first byte */
    CursorAt(0, 8); /* the second c */
    MoveCursor(HomeKey);
    CHECK_INT(vted_editor_config.cursor_x, 7);
}

static void TestViewportJumpsOnWordWrappedLines(void)
{
    /* "ab cd ab cd ab cd" over lines of 4 cells: one word per screen
       line, word k starting at byte 3k. The viewport enters the row
       on its second line */
    FreeBuffer();
    InitBuffer();
    AppendRow("ab cd ab cd ab cd", 17, VTED_EOL_LF);

    vted_editor_config.screen_rows = 4;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 1;
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;

    /* the top: the row's second line, the first "cd" */
    MoveCursor(ViewportTop);
    CHECK_INT(vted_editor_config.cursor_x, 3);

    /* the middle of 4 is 2: the viewport's third line, the second "cd" */
    MoveCursor(ViewportMiddle);
    CHECK_INT(vted_editor_config.cursor_x, 9);

    /* the bottom: the viewport's fourth line, the third "ab" */
    MoveCursor(ViewportBottom);
    CHECK_INT(vted_editor_config.cursor_x, 12);
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

/* a tall first row (12 lines of 4 cells) over two short ones */
static void BeginTallAndShort(void)
{
    FreeBuffer();
    InitBuffer();
    AppendRow("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 48, VTED_EOL_LF);
    AppendRow("short", 5, VTED_EOL_LF);
    AppendRow("row", 3, VTED_EOL_LF);

    vted_editor_config.screen_rows = 10;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;
}

static void TestCenteringCountsTallRows(void)
{
    /* a wrapped viewport centers in screen lines: the tall row above
       the cursor takes its real height, so the cursor lands mid
       viewport instead of off screen below it */
    BeginTallAndShort();

    CursorAt(2, 1);
    ScreenCenterCursor();

    /* the middle of 10 is 5: the short row's two lines and three
       more from the tall row, entered at line 9 */
    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 9);
}

static void TestCenteringWhenTheHalfEndsAtARowEdge(void)
{
    /* half a viewport of lines ending exactly where a row ends
       leaves the rows whole: the top is the next one down, or the
       first row when the buffer runs out above */
    FreeBuffer();
    InitBuffer();
    AppendRow("xxxxxxxxxxxx", 12, VTED_EOL_LF); /* 3 lines */
    AppendRow("a", 1, VTED_EOL_LF);              /* 1 line */
    AppendRow("row", 3, VTED_EOL_LF);

    vted_editor_config.screen_rows = 8;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;

    /* the 3 lines plus the 'a' line are exactly the half of 8: both
       rows stay whole and the cursor sits on the middle line */
    CursorAt(2, 1);
    ScreenCenterCursor();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 0);

    /* one more row above than the half holds: the walk stops inside
       the tall row */
    {
        FreeBuffer();
        InitBuffer();
        AppendRow("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 48, VTED_EOL_LF); /* 12 lines */
        AppendRow("a", 1, VTED_EOL_LF);              /* 1 line */
        AppendRow("xxxxxxxxxxxxxxxx", 16, VTED_EOL_LF); /* 4 lines */
        AppendRow("row", 3, VTED_EOL_LF);

        vted_editor_config.screen_rows = 10;
        vted_editor_config.text_cols = 4;
        vted_editor_config.wrap = 1;
        vted_editor_config.preferred_rx = -1;
        vted_editor_config.row_offset = 0;
        vted_editor_config.line_offset = 0;

        /* the 'a' and the 4-line row are exactly the half of 10:
           they are the top and the tall row above falls away */
        CursorAt(3, 1);
        ScreenCenterCursor();

        CHECK_INT(vted_editor_config.row_offset, 1);
        CHECK_INT(vted_editor_config.line_offset, 0);
    }
}

static void TestCenteringSeesWhatATallRowHides(void)
{
    /* a row number inside the viewport is not on screen when a tall
       row above eats the lines: the goto still centers on it */
    FreeBuffer();
    InitBuffer();
    AppendRow("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 40, VTED_EOL_LF); /* 10 lines */
    AppendRow("a", 1, VTED_EOL_LF);
    AppendRow("b", 1, VTED_EOL_LF);
    AppendRow("c", 1, VTED_EOL_LF);
    AppendRow("d", 1, VTED_EOL_LF);
    AppendRow("e", 1, VTED_EOL_LF);

    vted_editor_config.screen_rows = 10;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;

    CursorAt(5, 0);
    ScreenCenterCursor();

    /* the four one-line rows and one line of the tall row are above
       the cursor, so it sits on the middle line */
    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 9);
}

static void TestAWrappedCursorOnScreenStays(void)
{
    /* a viewport already showing the cursor keeps its place, also
       when the cursor sits inside a tall row */
    BeginTallRow(240); /* 60 lines for a viewport of 10 */
    AppendRow("tail", 4, VTED_EOL_LF);

    /* the way UpdateScroll left it: the tall row on top, entered at
       line 31, the cursor on its line 32 */
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 31;

    CursorAt(0, 128);
    ScreenCenterCursor();

    CHECK_INT(vted_editor_config.row_offset, 0);
    CHECK_INT(vted_editor_config.line_offset, 31);
}

/* three rows over lines of 4 cells: 3, 1 and 2 screen lines */
static void BeginWrappedRows(void)
{
    FreeBuffer();
    InitBuffer();
    AppendRow("abcdefghijkl", 12, VTED_EOL_LF);
    AppendRow("xy", 2, VTED_EOL_LF);
    AppendRow("abcdefgh", 8, VTED_EOL_LF);

    vted_editor_config.screen_rows = 4;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;
}

static void TestPageDownMovesByScreenLines(void)
{
    BeginWrappedRows();

    /* a viewport of 4 lines down from the first: the rest of the
       tall row, the one-line row, and the start of the last */
    CursorAt(0, 0);
    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    /* another viewport: past the last row, onto the phantom row */
    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 3);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    /* and back up: the phantom line, the last row's two lines and
       the one-line row land the cursor on the tall row's last line */
    MoveCursor(PageUp);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 8);

    /* at the top, a page up stays on the first line */
    MoveCursor(PageUp);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestPagingKeepsTheColumnWithinTheLine(void)
{
    BeginWrappedRows();

    /* the column the cursor started from within its line is what
       the cursor aims for on every line it lands on */
    CursorAt(0, 5); /* line 1, column 1 */
    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 5);
}

static void TestPagingThroughATallRow(void)
{
    /* a row taller than the viewport pages inside it, line by
       line */
    BeginTallRow(240); /* 60 lines for a viewport of 10 */
    AppendRow("tail", 4, VTED_EOL_LF);

    CursorAt(0, 0);
    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 40); /* line 10 */

    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_x, 80); /* line 20 */

    /* from line 50 a page lands past the row's end, on the phantom
       row */
    CursorAt(0, 200);
    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestPlainPagingMovesByRows(void)
{
    /* without wrapping, a page is a viewport of rows like before */
    BeginPlain(10);

    CursorAt(0, 1);
    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 4);

    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 8);

    /* past the end: the phantom row past the last line */
    MoveCursor(PageDown);
    CHECK_INT(vted_editor_config.cursor_y, 10);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    MoveCursor(PageUp);
    CHECK_INT(vted_editor_config.cursor_y, 6);
}

static void TestViewportJumpsThroughTheWrappedLayout(void)
{
    /* a tall row of three lines, then four of one line each; the
       viewport starts inside the tall row, on its second line, so the
       lines of the viewport are: the tall row's second and third, the
       four rows, then two past the text */
    FreeBuffer();
    InitBuffer();
    AppendRow("xxxxxxxxxxxx", 12, VTED_EOL_LF); /* 3 lines of 4 cells */
    AppendRow("a", 1, VTED_EOL_LF);
    AppendRow("row", 3, VTED_EOL_LF);
    AppendRow("zz", 2, VTED_EOL_LF);
    AppendRow("q", 1, VTED_EOL_LF);

    vted_editor_config.screen_rows = 8;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 1;
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;

    /* the top: the second line of the tall row */
    MoveCursor(ViewportTop);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    /* the middle of 8 is 4: the row after 'row' */
    MoveCursor(ViewportMiddle);
    CHECK_INT(vted_editor_config.cursor_y, 3);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    /* the bottom opens below the text: the last row's only line */
    MoveCursor(ViewportBottom);
    CHECK_INT(vted_editor_config.cursor_y, 4);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestViewportJumpsLandInsideATallRow(void)
{
    /* a viewport of four lines over a row of three lines and one of
       one: the middle lands inside the tall row */
    FreeBuffer();
    InitBuffer();
    AppendRow("xxxxxxxxxxxx", 12, VTED_EOL_LF); /* 3 lines of 4 cells */
    AppendRow("a", 1, VTED_EOL_LF);

    vted_editor_config.screen_rows = 4;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 0;
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;

    /* the top: the first line of the tall row */
    MoveCursor(ViewportTop);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    /* the middle of 4 is 2: the tall row's third line */
    MoveCursor(ViewportMiddle);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 8);

    /* the bottom: the row below */
    MoveCursor(ViewportBottom);
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestViewportJumpsMoveByRows(void)
{
    /* without wrapping one row is one screen line: the jumps move by
       rows, and a target past the end lands on the last row */
    BeginPlain(10);
    vted_editor_config.row_offset = 2;

    MoveCursor(ViewportTop);
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    MoveCursor(ViewportMiddle); /* the middle of 4 is 2 */
    CHECK_INT(vted_editor_config.cursor_y, 4);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    MoveCursor(ViewportBottom);
    CHECK_INT(vted_editor_config.cursor_y, 5);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    /* a viewport that opens below the text: the last row */
    vted_editor_config.row_offset = 8;
    MoveCursor(ViewportBottom);
    CHECK_INT(vted_editor_config.cursor_y, 9);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestViewportJumpsOnAnEmptyBuffer(void)
{
    /* nothing to land on: the phantom row past the end is all there
       is, and every jump stays on it */
    FreeBuffer();
    InitBuffer();

    vted_editor_config.screen_rows = 4;
    vted_editor_config.wrap = 1;

    MoveCursor(ViewportTop);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    MoveCursor(ViewportBottom);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestViewportJumpsPastAStaleOffset(void)
{
    /* the row shrank under an edit, or the viewport arrived from
       another buffer: a line offset that no longer exists is walked
       from the row's start, the way UpdateScroll resets it */
    FreeBuffer();
    InitBuffer();
    AppendRow("xxxxxxxxxxxx", 12, VTED_EOL_LF); /* 3 lines of 4 cells */

    vted_editor_config.screen_rows = 4;
    vted_editor_config.text_cols = 4;
    vted_editor_config.wrap = 1;
    vted_editor_config.preferred_rx = -1;
    vted_editor_config.row_offset = 0;
    vted_editor_config.line_offset = 5; /* stale: the row has three */
    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;

    MoveCursor(ViewportTop);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 0);
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
    TestHomeAndEndOnWordWrappedLines();
    TestDeepCursorScrollsIntoATallRow();
    TestCursorOnTheTopLinesOfATallRow();
    TestPastTheEndScrollsToTheLastLine();
    TestTallRowFallsAwayForARowBelow();
    TestStaleLineOffsetResets();
    TestNormalRowsKeepALineOffsetOfZero();
    TestCenteringPutsTheCursorMidViewport();
    TestCenteringCountsTallRows();
    TestCenteringWhenTheHalfEndsAtARowEdge();
    TestCenteringSeesWhatATallRowHides();
    TestAWrappedCursorOnScreenStays();
    TestPageDownMovesByScreenLines();
    TestPagingKeepsTheColumnWithinTheLine();
    TestPagingThroughATallRow();
    TestPlainPagingMovesByRows();
    TestViewportJumpsThroughTheWrappedLayout();
    TestViewportJumpsLandInsideATallRow();
    TestViewportJumpsMoveByRows();
    TestViewportJumpsOnAnEmptyBuffer();
    TestViewportJumpsPastAStaleOffset();
    TestViewportJumpsOnWordWrappedLines();

    return TestSummary("screen");
}

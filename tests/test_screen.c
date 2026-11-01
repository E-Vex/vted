#include <string.h>
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

    return TestSummary("screen");
}

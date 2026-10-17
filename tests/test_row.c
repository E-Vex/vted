#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_row.h"
/*===============================*/

/* builds a row on a static buffer, enough for these checks */
static vted_row_t MakeRow(const char *text)
{
    static char storage[128];
    vted_row_t row;

    strcpy(storage, text);
    row.chars = storage;
    row.size = (int)strlen(text);
    return row;
}

static void TestCharWidth(void)
{
    vted_row_t row = MakeRow("a\t\x01\x7F\xC3\xA9\xE4\xB8\xAD");

    CHECK_INT(RowCharWidth(&row, 0, 0), 1);  /* a */
    CHECK_INT(RowCharWidth(&row, 1, 1), 7);  /* tab at cell 1 -> stop 8 */
    CHECK_INT(RowCharWidth(&row, 1, 8), 8);  /* tab on a stop: full width */
    CHECK_INT(RowCharWidth(&row, 1, 15), 1); /* tab one cell before a stop */
    CHECK_INT(RowCharWidth(&row, 2, 8), 2);  /* ^A */
    CHECK_INT(RowCharWidth(&row, 3, 10), 2); /* ^? */
    CHECK_INT(RowCharWidth(&row, 4, 12), 1); /* é */
    CHECK_INT(RowCharWidth(&row, 6, 13), 2); /* a wide ideograph */
}

static void TestTabs(void)
{
    vted_row_t row = MakeRow("a\tb");

    CHECK_INT(RowCxToRx(&row, 0), 0);
    CHECK_INT(RowCxToRx(&row, 1), 1);
    CHECK_INT(RowCxToRx(&row, 2), 8); /* b sits on the first tab stop */
    CHECK_INT(RowCxToRx(&row, 3), 9);

    CHECK_INT(RowRxToCx(&row, 0), 0);
    CHECK_INT(RowRxToCx(&row, 1), 1);
    CHECK_INT(RowRxToCx(&row, 5), 1); /* inside the tab: its start */
    CHECK_INT(RowRxToCx(&row, 8), 2);
    CHECK_INT(RowRxToCx(&row, 9), 3);
    CHECK_INT(RowRxToCx(&row, 40), 3); /* past the text: row end */
}

static void TestWideCharacters(void)
{
    vted_row_t row = MakeRow("\xE4\xB8\xAD" "x"); /* wide ideograph, x */

    CHECK_INT(RowCxToRx(&row, 0), 0);
    CHECK_INT(RowCxToRx(&row, 3), 2);
    CHECK_INT(RowCxToRx(&row, 4), 3);

    CHECK_INT(RowRxToCx(&row, 1), 0); /* second half of the ideograph */
    CHECK_INT(RowRxToCx(&row, 2), 3);
}

static void TestMultibyteAndControls(void)
{
    vted_row_t row = MakeRow("h\xC3\xA9y\x01z"); /* h é y ^A z */

    CHECK_INT(RowCxToRx(&row, 1), 1);
    CHECK_INT(RowCxToRx(&row, 3), 2); /* é is two bytes, one cell */
    CHECK_INT(RowCxToRx(&row, 5), 5); /* ^A takes two cells */
    CHECK_INT(RowRxToCx(&row, 4), 4); /* the first cell of ^A */
    CHECK_INT(RowRxToCx(&row, 5), 5);
}

static void TestCombiningMarks(void)
{
    vted_row_t row = MakeRow("e\xCC\x81x"); /* e + combining acute, x */

    CHECK_INT(RowCxToRx(&row, 3), 1); /* the mark adds no width */
    CHECK_INT(RowRxToCx(&row, 1), 3); /* lands on x, not the mark */
}

static void TestEmptyRow(void)
{
    vted_row_t row = MakeRow("");

    CHECK_INT(RowCxToRx(&row, 0), 0);
    CHECK_INT(RowRxToCx(&row, 0), 0);
    CHECK_INT(RowRxToCx(&row, 7), 0);
}

int main(void)
{
    TestCharWidth();
    TestTabs();
    TestWideCharacters();
    TestMultibyteAndControls();
    TestCombiningMarks();
    TestEmptyRow();

    return TestSummary("row");
}

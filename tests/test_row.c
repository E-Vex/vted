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

static void TestIndentLength(void)
{
    vted_row_t row = MakeRow("  \tx  y");

    CHECK_INT(RowIndentLength(&row, row.size), 3); /* two spaces and a tab */
    CHECK_INT(RowIndentLength(&row, 2), 2);        /* capped by the limit */
    CHECK_INT(RowIndentLength(&row, 0), 0);

    row = MakeRow("code");
    CHECK_INT(RowIndentLength(&row, row.size), 0);

    row = MakeRow("    "); /* only blanks: the whole row */
    CHECK_INT(RowIndentLength(&row, 99), 4);

    row = MakeRow("");
    CHECK_INT(RowIndentLength(&row, 99), 0);
}

static void TestWordRight(void)
{
    vted_row_t row = MakeRow("foo bar  baz");

    CHECK_INT(RowWordRight(&row, 0), 4);  /* to the start of bar */
    CHECK_INT(RowWordRight(&row, 1), 4);  /* from inside a word */
    CHECK_INT(RowWordRight(&row, 3), 4);  /* from the blank after it */
    CHECK_INT(RowWordRight(&row, 4), 9);  /* two blanks skipped */
    CHECK_INT(RowWordRight(&row, 9), 12); /* last word: the row end */
    CHECK_INT(RowWordRight(&row, 12), 12);

    row = MakeRow("a->b_c");
    CHECK_INT(RowWordRight(&row, 0), 1);  /* punctuation is its own word */
    CHECK_INT(RowWordRight(&row, 1), 3);
    CHECK_INT(RowWordRight(&row, 3), 6);  /* b_c is one word */

    row = MakeRow("  foo");
    CHECK_INT(RowWordRight(&row, 0), 2);  /* leading blanks skipped */
}

static void TestWordLeft(void)
{
    vted_row_t row = MakeRow("foo bar  baz");

    CHECK_INT(RowWordLeft(&row, 12), 9);  /* to the start of baz */
    CHECK_INT(RowWordLeft(&row, 10), 9);  /* from inside a word */
    CHECK_INT(RowWordLeft(&row, 9), 4);   /* over two blanks to bar */
    CHECK_INT(RowWordLeft(&row, 4), 0);
    CHECK_INT(RowWordLeft(&row, 0), 0);

    row = MakeRow("a->b");
    CHECK_INT(RowWordLeft(&row, 4), 3);
    CHECK_INT(RowWordLeft(&row, 3), 1);   /* the -> run */
    CHECK_INT(RowWordLeft(&row, 1), 0);

    row = MakeRow("  foo");
    CHECK_INT(RowWordLeft(&row, 2), 0);   /* only blanks before: the start */
}

static void TestWordMovementUtf8(void)
{
    /* h é l l o <blank> w ö r l d, é and ö are two bytes each */
    vted_row_t row = MakeRow("h\xC3\xA9llo w\xC3\xB6rld");

    CHECK_INT(RowWordRight(&row, 0), 7);   /* past the blank, on w */
    CHECK_INT(RowWordRight(&row, 1), 7);
    CHECK_INT(RowWordRight(&row, 7), 13);  /* the row end */
    CHECK_INT(RowWordLeft(&row, 13), 7);
    CHECK_INT(RowWordLeft(&row, 7), 0);    /* é never splits the word */

    /* a CJK word, a blank, a latin word */
    row = MakeRow("\xE4\xB8\xAD\xE6\x96\x87 ab");
    CHECK_INT(RowWordRight(&row, 0), 7);
    CHECK_INT(RowWordLeft(&row, 7), 0);

    /* a combining mark stays with its letter */
    row = MakeRow("e\xCC\x81 x");
    CHECK_INT(RowWordRight(&row, 0), 4);
    CHECK_INT(RowWordLeft(&row, 4), 0);

    /* an invalid byte is punctuation and does not hang the scan */
    row = MakeRow("a\xFF" "b");
    CHECK_INT(RowWordRight(&row, 0), 1);
    CHECK_INT(RowWordRight(&row, 1), 2);
    CHECK_INT(RowWordLeft(&row, 3), 2);
}

static void TestWordMovementEmptyRow(void)
{
    vted_row_t row = MakeRow("");

    CHECK_INT(RowWordRight(&row, 0), 0);
    CHECK_INT(RowWordLeft(&row, 0), 0);
}

int main(void)
{
    TestCharWidth();
    TestTabs();
    TestWideCharacters();
    TestMultibyteAndControls();
    TestCombiningMarks();
    TestEmptyRow();
    TestIndentLength();
    TestWordRight();
    TestWordLeft();
    TestWordMovementUtf8();
    TestWordMovementEmptyRow();

    return TestSummary("row");
}

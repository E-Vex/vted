#include <stdlib.h>
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

/* builds a row whose text lives on the heap, as RowSetText needs */
static vted_row_t HeapRow(const char *text)
{
    vted_row_t row;

    row.size = (int)strlen(text);
    row.chars = malloc((size_t)row.size + 1);
    memcpy(row.chars, text, (size_t)row.size + 1);
    row.eol = VTED_EOL_LF;
    return row;
}

static void TestTruncate(void)
{
    vted_row_t row = HeapRow("hello world");

    RowTruncate(&row, 5);
    CHECK_INT(row.size, 5);
    CHECK_STR(row.chars, "hello");

    /* at or past the end there is nothing to cut */
    RowTruncate(&row, 5);
    CHECK_STR(row.chars, "hello");
    RowTruncate(&row, 99);
    CHECK_STR(row.chars, "hello");

    /* a negative position is ignored too */
    RowTruncate(&row, -1);
    CHECK_STR(row.chars, "hello");

    RowTruncate(&row, 0);
    CHECK_INT(row.size, 0);
    CHECK_STR(row.chars, "");

    free(row.chars);
}

static void TestSetText(void)
{
    vted_row_t row = HeapRow("old text");

    CHECK_INT(RowSetText(&row, "new", 3), 0);
    CHECK_INT(row.size, 3);
    CHECK_STR(row.chars, "new");

    /* longer than before, and the row ending is left alone */
    row.eol = VTED_EOL_CRLF;
    CHECK_INT(RowSetText(&row, "a much longer replacement", 25), 0);
    CHECK_INT(row.size, 25);
    CHECK_STR(row.chars, "a much longer replacement");
    CHECK_INT(row.eol, VTED_EOL_CRLF);

    /* an empty text, with no bytes to copy */
    CHECK_INT(RowSetText(&row, NULL, 0), 0);
    CHECK_INT(row.size, 0);
    CHECK_STR(row.chars, "");

    free(row.chars);
}

static void TestInsertString(void)
{
    vted_row_t row = HeapRow("hello");

    /* in the middle, at the start and at the end */
    CHECK_INT(RowInsertString(&row, 2, "XY", 2), 0);
    CHECK_STR(row.chars, "heXYllo");
    CHECK_INT(row.size, 7);

    CHECK_INT(RowInsertString(&row, 0, ">>", 2), 0);
    CHECK_STR(row.chars, ">>heXYllo");

    CHECK_INT(RowInsertString(&row, row.size, "!", 1), 0);
    CHECK_STR(row.chars, ">>heXYllo!");
    CHECK_INT(row.size, 10);

    /* nothing to place leaves the row as it is */
    CHECK_INT(RowInsertString(&row, 3, "", 0), 0);
    CHECK_STR(row.chars, ">>heXYllo!");

    /* a position outside the row, or a negative length, is refused */
    CHECK_INT(RowInsertString(&row, 11, "x", 1), -1);
    CHECK_INT(RowInsertString(&row, -1, "x", 1), -1);
    CHECK_INT(RowInsertString(&row, 0, "x", -1), -1);
    CHECK_STR(row.chars, ">>heXYllo!");

    /* the bytes are copied as they are, a tab and UTF-8 included */
    CHECK_INT(RowInsertString(&row, 2, "\t\xc3\xa9", 3), 0);
    CHECK_INT(row.size, 13);
    CHECK_STR(row.chars, ">>\t\xc3\xa9heXYllo!");

    free(row.chars);
}

/* a buffer holding the given lines, every one ending with LF */
static void FillBuffer(int count, const char *lines[])
{
    int i;

    FreeBuffer();

    for (i = 0; i < count; i++)
    {
        AppendRow(lines[i], (int)strlen(lines[i]), VTED_EOL_LF);
    }
}

static void TestSplitRow(void)
{
    const char *lines[] = {"abcd", "end"};
    FillBuffer(2, lines);

    CHECK_INT(SplitRow(0, 2, VTED_EOL_CRLF), 0);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
    CHECK_STR(vted_buffer.rows[2].chars, "end");

    /* the new row takes the ending asked for, the old one keeps its own */
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);

    /* split at the very end, the new row is empty */
    CHECK_INT(SplitRow(2, 3, VTED_EOL_LF), 0);
    CHECK_INT(vted_buffer.num_rows, 4);
    CHECK_STR(vted_buffer.rows[2].chars, "end");
    CHECK_STR(vted_buffer.rows[3].chars, "");

    FreeBuffer();
}

static void TestInsertAndRemoveText(void)
{
    const char *lines[] = {"abcd"};
    FillBuffer(1, lines);

    /* one line goes in like typing */
    CHECK_INT(InsertText(0, 2, "XY", 2, VTED_EOL_LF), 0);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abXYcd");

    /* several lines: what stood right of the spot rides to the last one */
    CHECK_INT(InsertText(0, 3, "1\n22\n3", 6, VTED_EOL_CRLF), 0);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "abX1");
    CHECK_STR(vted_buffer.rows[1].chars, "22");
    CHECK_STR(vted_buffer.rows[2].chars, "3Ycd");
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[2].eol, VTED_EOL_CRLF);

    /* taking it out again leaves the row as it was */
    CHECK_INT(RemoveText(0, 3, "1\n22\n3", 6), 0);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abXYcd");

    CHECK_INT(RemoveText(0, 2, "XY", 2), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_buffer.rows[0].size, 4);

    /* a text that is only line breaks, and one that ends with one */
    CHECK_INT(InsertText(0, 1, "\n\n", 2, VTED_EOL_LF), 0);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_STR(vted_buffer.rows[1].chars, "");
    CHECK_STR(vted_buffer.rows[2].chars, "bcd");
    CHECK_INT(RemoveText(0, 1, "\n\n", 2), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_buffer.num_rows, 1);

    FreeBuffer();
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
    TestTruncate();
    TestSetText();
    TestInsertString();
    TestSplitRow();
    TestInsertAndRemoveText();

    return TestSummary("row");
}

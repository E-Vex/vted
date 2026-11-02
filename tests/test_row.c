/* _exit() is POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static void TestTabWidth(void)
{
    vted_row_t row = MakeRow("a\tb");

    RowSetTabWidth(4);
    CHECK_INT(RowCharWidth(&row, 1, 0), 4); /* tab at cell 0 -> stop 4 */
    CHECK_INT(RowCharWidth(&row, 1, 1), 3); /* tab at cell 1 -> stop 4 */
    CHECK_INT(RowCxToRx(&row, 2), 4);       /* b sits on cell 4 */
    CHECK_INT(RowRxToCx(&row, 4), 2);       /* cell 4 is where 'b' starts */

    RowSetTabWidth(VTED_TAB_STOP);
    CHECK_INT(RowCharWidth(&row, 1, 1), 7); /* back to the default stop */
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

/* builds a buffer of rows from a NULL-terminated list of texts */
static void MakeBuffer(const char *const *texts)
{
    int i;

    FreeBuffer();

    for (i = 0; texts[i] != NULL; i++)
    {
        InsertRow(i, texts[i], (int)strlen(texts[i]), VTED_EOL_LF);
    }
}

/* runs one match query and checks where it landed */
static int MatchAt(int cy, int cx, int *out_y, int *out_x)
{
    *out_y = -2;
    *out_x = -2;
    return BufferFindBracketMatch(cy, cx, out_y, out_x);
}

static void TestBracketForwardMatch(void)
{
    const char *const texts[] = {"f(a, b)", NULL};
    int y, x;

    MakeBuffer(texts);
    CHECK_INT(MatchAt(0, 1, &y, &x), 1); /* cursor on ( */
    CHECK_INT(y, 0);
    CHECK_INT(x, 6);
    CHECK_INT(MatchAt(0, 6, &y, &x), 1); /* cursor on ) */
    CHECK_INT(y, 0);
    CHECK_INT(x, 1);
}

static void TestBracketBeforeTheCursor(void)
{
    const char *const texts[] = {"f(a)", NULL};
    int y, x;

    MakeBuffer(texts);
    /* the cursor rests right after the closing bracket */
    CHECK_INT(MatchAt(0, 4, &y, &x), 1);
    CHECK_INT(y, 0);
    CHECK_INT(x, 1);
}

static void TestBracketNesting(void)
{
    const char *const texts[] = {"f(g(x))", NULL};
    int y, x;

    MakeBuffer(texts);
    /* the outer pair: its closer is the last one */
    CHECK_INT(MatchAt(0, 1, &y, &x), 1);
    CHECK_INT(x, 6);
    /* the inner pair */
    CHECK_INT(MatchAt(0, 3, &y, &x), 1);
    CHECK_INT(x, 5);
}

static void TestBracketOtherPairs(void)
{
    const char *const texts[] = {"a [b] {c} (d)", NULL};
    int y, x;

    MakeBuffer(texts);
    CHECK_INT(MatchAt(0, 2, &y, &x), 1);
    CHECK_INT(x, 4);
    CHECK_INT(MatchAt(0, 6, &y, &x), 1);
    CHECK_INT(x, 8);
    CHECK_INT(MatchAt(0, 10, &y, &x), 1);
    CHECK_INT(x, 12);
}

static void TestBracketAcrossRows(void)
{
    const char *const texts[] = {"f(", "  a,", "  b)", NULL};
    int y, x;

    MakeBuffer(texts);
    CHECK_INT(MatchAt(0, 1, &y, &x), 1);
    CHECK_INT(y, 2);
    CHECK_INT(x, 3);
    /* and backwards from the closer */
    CHECK_INT(MatchAt(2, 3, &y, &x), 1);
    CHECK_INT(y, 0);
    CHECK_INT(x, 1);
}

static void TestBracketMixedPairs(void)
{
    /* a ] between the parentheses does not close them, but a stray )
       does: only brackets of the pair are counted */
    const char *const texts[] = {"(a]", NULL};
    int y, x;

    MakeBuffer(texts);
    CHECK_INT(MatchAt(0, 0, &y, &x), 0);
}

static void TestBracketUnmatched(void)
{
    const char *const texts[] = {"abc(", NULL};
    int y, x;

    MakeBuffer(texts);
    CHECK_INT(MatchAt(0, 3, &y, &x), 0);
    CHECK_INT(MatchAt(0, 4, &y, &x), 0); /* resting after it neither */
}

static void TestBracketWithoutABracket(void)
{
    const char *const texts[] = {"plain words", NULL};
    int y, x;

    MakeBuffer(texts);
    CHECK_INT(MatchAt(0, 3, &y, &x), 0);
    CHECK_INT(MatchAt(0, 0, &y, &x), 0);
    CHECK_INT(MatchAt(0, 11, &y, &x), 0);
}

static void TestBracketOutOfTheBuffer(void)
{
    const char *const texts[] = {"()", NULL};
    int y, x;

    MakeBuffer(texts);
    CHECK_INT(MatchAt(-1, 0, &y, &x), 0);
    CHECK_INT(MatchAt(5, 0, &y, &x), 0);
    CHECK_INT(MatchAt(0, -1, &y, &x), 0);
    CHECK_INT(MatchAt(0, 9, &y, &x), 0);
}

/* runs one block query and checks where its rows land */
static int BlockAt(int cy, int cx, int *first, int *last)
{
    *first = -2;
    *last = -2;
    return BufferFindBracketBlock(cy, cx, first, last);
}

static void TestBracketBlockSpansItsRows(void)
{
    const char *const texts[] = {
        "int main(void)",
        "{",
        "    body();",
        "}",
        "tail",
        NULL
    };
    int first, last;

    MakeBuffer(texts);
    /* the cursor on the opener: the block is the opener's row through
       the closer's, everything between them included */
    CHECK_INT(BlockAt(1, 0, &first, &last), 1);
    CHECK_INT(first, 1);
    CHECK_INT(last, 3);

    /* right past the closer: the bracket behind the cursor counts,
       and the block is the same rows either way */
    CHECK_INT(BlockAt(3, 1, &first, &last), 1);
    CHECK_INT(first, 1);
    CHECK_INT(last, 3);
}

static void TestBracketBlockOnOneRowIsThatRow(void)
{
    const char *const texts[] = {"f(a, b)", NULL};
    int first, last;

    MakeBuffer(texts);
    CHECK_INT(BlockAt(0, 1, &first, &last), 1);
    CHECK_INT(first, 0);
    CHECK_INT(last, 0);
}

static void TestBracketBlockWithoutAMatch(void)
{
    const char *const texts[] = {"{", "body", NULL};
    int first, last;

    MakeBuffer(texts);
    /* an opener with no closer encloses nothing */
    CHECK_INT(BlockAt(0, 0, &first, &last), 0);

    /* and neither does a cursor without a bracket */
    CHECK_INT(BlockAt(1, 0, &first, &last), 0);
}

static void TestDuplicateRowsCopiesTheSpanInOrder(void)
{
    const char *const texts[] = {"a", "b", "c", NULL};

    MakeBuffer(texts);

    CHECK_INT(BufferDuplicateRows(0, 1), 0);
    CHECK_INT(vted_buffer.num_rows, 5);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_STR(vted_buffer.rows[1].chars, "b");
    CHECK_STR(vted_buffer.rows[2].chars, "a"); /* the copies, in order */
    CHECK_STR(vted_buffer.rows[3].chars, "b");
    CHECK_STR(vted_buffer.rows[4].chars, "c");
}

static void TestDuplicateRowsKeepTheirEndings(void)
{
    const char *const texts[] = {"a {", "b", "}", NULL};

    MakeBuffer(texts);
    vted_buffer.rows[0].eol = VTED_EOL_LF;
    vted_buffer.rows[1].eol = VTED_EOL_CRLF;
    vted_buffer.rows[2].eol = VTED_EOL_CR;

    CHECK_INT(BufferDuplicateRows(0, 2), 0);
    CHECK_INT(vted_buffer.rows[3].eol, VTED_EOL_LF);
    CHECK_INT(vted_buffer.rows[4].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[5].eol, VTED_EOL_CR);
}

static void TestDuplicateRowsOfOneRow(void)
{
    const char *const texts[] = {"only", NULL};

    MakeBuffer(texts);

    CHECK_INT(BufferDuplicateRows(0, 0), 0);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "only");
}

static void TestToggleCommentPutsItInFrontOfTheFirstNonBlank(void)
{
    const char *const texts[] = {"code", NULL};
    int changed;

    MakeBuffer(texts);

    CHECK_INT(BufferToggleComment(0, 0, "//", NULL, &changed), 0);
    CHECK_INT(changed, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "// code");

    /* the indent stays in front of the token */
    {
        const char *const indented[] = {"    code", NULL};

        MakeBuffer(indented);

        CHECK_INT(BufferToggleComment(0, 0, "//", NULL, &changed), 0);
        CHECK_INT(changed, 1);
        CHECK_STR(vted_buffer.rows[0].chars, "    // code");
    }
}

static void TestToggleCommentTakesItOffAgain(void)
{
    const char *const texts[] = {"// code", NULL};
    int changed;

    MakeBuffer(texts);

    CHECK_INT(BufferToggleComment(0, 0, "//", NULL, &changed), 0);
    CHECK_INT(changed, -1);
    CHECK_STR(vted_buffer.rows[0].chars, "code");

    /* a comment with no space behind it loses only the token */
    {
        const char *const tight[] = {"//code", NULL};

        MakeBuffer(tight);

        CHECK_INT(BufferToggleComment(0, 0, "//", NULL, &changed), 0);
        CHECK_STR(vted_buffer.rows[0].chars, "code");
    }

    /* a comment with the indent still in front */
    {
        const char *const indented[] = {"    // code", NULL};

        MakeBuffer(indented);

        CHECK_INT(BufferToggleComment(0, 0, "//", NULL, &changed), 0);
        CHECK_STR(vted_buffer.rows[0].chars, "    code");
    }

    /* a line that is only the comment and its space empties out */
    {
        const char *const bare[] = {"// ", NULL};

        MakeBuffer(bare);

        CHECK_INT(BufferToggleComment(0, 0, "//", NULL, &changed), 0);
        CHECK_STR(vted_buffer.rows[0].chars, "");
    }
}

static void TestToggleCommentRoundTrips(void)
{
    /* commenting and uncommenting brings back exactly what the
       toggle itself wrote, indent and all */
    const char *const texts[] = {"one", "    two", NULL};
    int changed;

    MakeBuffer(texts);

    CHECK_INT(BufferToggleComment(0, 1, "//", NULL, &changed), 0);
    CHECK_INT(changed, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "// one");
    CHECK_STR(vted_buffer.rows[1].chars, "    // two");

    CHECK_INT(BufferToggleComment(0, 1, "//", NULL, &changed), 0);
    CHECK_INT(changed, -2);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "    two");
}

static void TestToggleCommentSkipsBlankRows(void)
{
    /* a row of only blanks stays as it is, and a span of nothing but
       them changes nothing at all */
    const char *const texts[] = {"", "   ", "code", "", NULL};
    int changed;

    MakeBuffer(texts);

    CHECK_INT(BufferToggleComment(0, 4, "//", NULL, &changed), 0);
    CHECK_INT(changed, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "");
    CHECK_STR(vted_buffer.rows[1].chars, "   ");
    CHECK_STR(vted_buffer.rows[2].chars, "// code");
    CHECK_STR(vted_buffer.rows[3].chars, "");

    /* the direction comes from the first row that is not blanks,
       blank rows in between included */
    {
        const char *const blanks[] = {"", "  ", NULL};

        MakeBuffer(blanks);

        CHECK_INT(BufferToggleComment(0, 1, "//", NULL, &changed), 0);
        CHECK_INT(changed, 0);
        CHECK_STR(vted_buffer.rows[0].chars, "");
        CHECK_STR(vted_buffer.rows[1].chars, "  ");
    }
}

static void TestToggleCommentDirectionOfAMixedSpan(void)
{
    /* the first row that is not blanks decides for the whole span:
       an uncommented one comments them all, a commented one takes
       the tokens off, and rows already the other way stay put */
    const char *const texts[] = {"one", "// two", "three", NULL};
    int changed;

    MakeBuffer(texts);

    CHECK_INT(BufferToggleComment(0, 2, "//", NULL, &changed), 0);
    CHECK_INT(changed, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "// one");
    CHECK_STR(vted_buffer.rows[1].chars, "// two");
    CHECK_STR(vted_buffer.rows[2].chars, "// three");

    CHECK_INT(BufferToggleComment(0, 2, "//", NULL, &changed), 0);
    CHECK_INT(changed, -3);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_STR(vted_buffer.rows[2].chars, "three");
}

static void TestToggleCommentKnowsBothSpellings(void)
{
    /* INI comments with # and knows ; besides: the first is written
       in, and either one comes off */
    const char *const texts[] = {"key = 1", "; note", NULL};
    int changed;

    MakeBuffer(texts);

    CHECK_INT(BufferToggleComment(0, 1, "#", ";", &changed), 0);
    CHECK_INT(changed, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "# key = 1");
    CHECK_STR(vted_buffer.rows[1].chars, "; note"); /* the other spelling: kept */

    CHECK_INT(BufferToggleComment(1, 1, "#", ";", &changed), 0);
    CHECK_INT(changed, -1);
    CHECK_STR(vted_buffer.rows[1].chars, "note");
}

static void TestToggleCommentBounds(void)
{
    /* a span wider than the buffer is cut down to it, and a token
       that is not there at all leaves the buffer be */
    const char *const texts[] = {"a", "b", NULL};
    int changed;

    MakeBuffer(texts);

    CHECK_INT(BufferToggleComment(-2, 9, "//", NULL, &changed), 0);
    CHECK_INT(changed, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "// a");
    CHECK_STR(vted_buffer.rows[1].chars, "// b");

    CHECK_INT(BufferToggleComment(0, 1, NULL, NULL, &changed), 0);
    CHECK_INT(changed, 0);
    CHECK_STR(vted_buffer.rows[0].chars, "// a");
}

static void TestBracketScanStopsAtTheLimit(void)
{
    /* a row far past the scan limit: the match at its end counts as
       missing, and the query still answers quickly */
    int size = VTED_BRACKET_SCAN_BYTES + 64;
    char *text = malloc((size_t)size + 2);
    int y, x;

    if (text == NULL)
    {
        fprintf(stderr, "  malloc failed\n");
        _exit(2);
    }

    memset(text, 'x', (size_t)size);
    text[0] = '(';
    text[size] = ')';
    text[size + 1] = '\0';

    FreeBuffer();
    InsertRow(0, text, size + 1, VTED_EOL_LF);

    CHECK_INT(MatchAt(0, 0, &y, &x), 0);

    free(text);
    FreeBuffer();
}

static void TestWrapBreaksAtWordBoundaries(void)
{
    /* words move down whole: "aaa b ccc dddd e" over lines of 4
       cells never cuts a run of non-blank glyphs in half */
    vted_row_t row = MakeRow("aaa b ccc dddd e");
    int line;
    int col;

    /* line 0 "aaa", line 1 " b", line 2 "ccc", line 3 "dddd",
       line 4 " e" */
    CHECK_INT(RowWrapCount(&row, 4, 0), 5);

    /* "ccc" does not fit the cells line 1 has left after " b", so it
       starts line 2 */
    RowCxToWrapped(&row, 6, 4, 0, &line, &col); /* c */
    CHECK_INT(line, 2);
    CHECK_INT(col, 0);

    RowCxToWrapped(&row, 10, 4, 0, &line, &col); /* d */
    CHECK_INT(line, 3);
    CHECK_INT(col, 0);

    /* "e" fits the cell line 4 has left after the blank */
    RowCxToWrapped(&row, 15, 4, 0, &line, &col); /* e */
    CHECK_INT(line, 4);
    CHECK_INT(col, 1);

    /* the blank the moved run left behind ends its line */
    RowCxToWrapped(&row, 5, 4, 0, &line, &col); /* the blank before ccc */
    CHECK_INT(line, 1);
    CHECK_INT(col, 1);

    /* a run that fits the rest of its line stays on it */
    {
        vted_row_t fits = MakeRow("a bc");

        CHECK_INT(RowWrapCount(&fits, 4, 0), 1); /* "bc" fits from cell 2 */
    }

    /* a run exactly as long as a line moves down when it starts
       mid-line */
    {
        vted_row_t exact = MakeRow("a bbbb");

        CHECK_INT(RowWrapCount(&exact, 4, 0), 2);
    }
}

static void TestWrapBreaksLongRunsByCharacter(void)
{
    /* a run longer than a whole line wraps by character inside
       itself, while the runs around it still move down whole */
    vted_row_t row = MakeRow("aa aaaaaaaa bb");
    int line;
    int col;

    /* line 0 "aa a", line 1 "aaaa", line 2 "aaa ", line 3 "bb" */
    CHECK_INT(RowWrapCount(&row, 4, 0), 4);

    RowCxToWrapped(&row, 3, 4, 0, &line, &col); /* the long run's first a */
    CHECK_INT(line, 0);
    CHECK_INT(col, 3);

    RowCxToWrapped(&row, 7, 4, 0, &line, &col); /* inside the long run */
    CHECK_INT(line, 1);
    CHECK_INT(col, 3);

    RowCxToWrapped(&row, 12, 4, 0, &line, &col); /* b */
    CHECK_INT(line, 3);
    CHECK_INT(col, 0);
}

static void TestWrapMovesWideRunsDown(void)
{
    /* a run with a wide glyph in it moves down whole too */
    vted_row_t row = MakeRow("ab cd \xE4\xB8\xAD" "x");
    int line;
    int col;

    /* line 0 "ab", line 1 " cd", line 2 the wide glyph and x */
    CHECK_INT(RowWrapCount(&row, 4, 0), 3);

    RowCxToWrapped(&row, 6, 4, 0, &line, &col); /* the wide glyph */
    CHECK_INT(line, 2);
    CHECK_INT(col, 0);

    RowCxToWrapped(&row, 9, 4, 0, &line, &col); /* x */
    CHECK_INT(line, 2);
    CHECK_INT(col, 2);
}

static void TestWrapCount(void)
{
    vted_row_t row = MakeRow("abcdefghijkl"); /* 12 cells */

    CHECK_INT(RowWrapCount(&row, 12, 0), 1); /* exactly one line */
    CHECK_INT(RowWrapCount(&row, 11, 0), 2); /* one cell over */
    CHECK_INT(RowWrapCount(&row, 4, 0), 3);  /* 12 cells of 4 */
    CHECK_INT(RowWrapCount(&row, 5, 0), 3);  /* 5+5+2 */

    CHECK_INT(RowWrapCount(&row, 1, 0), 12); /* every cell its own line */

    /* a width of nothing is treated as one cell */
    CHECK_INT(RowWrapCount(&row, 0, 0), 12);

    /* an empty row takes the one line the cursor sits on */
    {
        vted_row_t empty = MakeRow("");

        CHECK_INT(RowWrapCount(&empty, 4, 0), 1);
    }
}

static void TestWrapMovesWideGlyphsDown(void)
{
    /* abc plus a wide ideograph: at 4 cells it cannot share a line
       with c, so it starts the next one whole */
    vted_row_t row = MakeRow("abc\xE4\xB8\xAD" "x");
    int line;
    int col;

    CHECK_INT(RowWrapCount(&row, 4, 0), 2);

    RowCxToWrapped(&row, 3, 4, 0, &line, &col); /* the wide one itself */
    CHECK_INT(line, 1);
    CHECK_INT(col, 0);

    RowCxToWrapped(&row, 6, 4, 0, &line, &col); /* x after it */
    CHECK_INT(line, 1);
    CHECK_INT(col, 2);

    /* without wrapping pressure the ideograph stays on the first line */
    CHECK_INT(RowWrapCount(&row, 5, 0), 2);
    RowCxToWrapped(&row, 3, 5, 0, &line, &col);
    CHECK_INT(line, 0);
    CHECK_INT(col, 3);

    /* a control character is a glyph too: drawn as ^X it does not
       split either */
    {
        vted_row_t control = MakeRow("abc\x01");

        CHECK_INT(RowWrapCount(&control, 4, 0), 2);
        RowCxToWrapped(&control, 3, 4, 0, &line, &col);
        CHECK_INT(line, 1);
        CHECK_INT(col, 0);
    }
}

static void TestWrapLetsTabsCrossTheEdge(void)
{
    /* a tab at cell 2 runs to the stop at cell 8: at 4 cells a line it
       blanks the rest of the first line and the whole second one */
    vted_row_t row = MakeRow("ab\tc");
    int line;
    int col;

    RowSetTabWidth(8);

    CHECK_INT(RowWrapCount(&row, 4, 0), 3); /* c lands on the third line */

    RowCxToWrapped(&row, 3, 4, 0, &line, &col); /* c */
    CHECK_INT(line, 2);
    CHECK_INT(col, 0);

    /* a trailing tab runs onto lines of its own */
    {
        vted_row_t trailing = MakeRow("ab\t");

        CHECK_INT(RowWrapCount(&trailing, 4, 0), 2);
    }

    RowSetTabWidth(VTED_TAB_STOP);
}

static void TestWrapCursorPosition(void)
{
    vted_row_t row = MakeRow("abcdefghijkl");
    int line;
    int col;

    RowCxToWrapped(&row, 0, 4, 0, &line, &col);
    CHECK_INT(line, 0);
    CHECK_INT(col, 0);

    RowCxToWrapped(&row, 4, 4, 0, &line, &col); /* e starts the second line */
    CHECK_INT(line, 1);
    CHECK_INT(col, 0);

    RowCxToWrapped(&row, 7, 4, 0, &line, &col); /* h, the line's last cell */
    CHECK_INT(line, 1);
    CHECK_INT(col, 3);

    /* the end of the row sits after its last glyph: the line after
       the row's own ones, since the text ends exactly at a line edge */
    RowCxToWrapped(&row, 12, 4, 0, &line, &col);
    CHECK_INT(line, 3);
    CHECK_INT(col, 0);

    RowCxToWrapped(&row, 8, 4, 0, &line, &col); /* i starts the third line */
    CHECK_INT(line, 2);
    CHECK_INT(col, 0);

    /* past the end asks for the same place */
    RowCxToWrapped(&row, 99, 4, 0, &line, &col);
    CHECK_INT(line, 3);
    CHECK_INT(col, 0);
}

static void TestWrappedToCx(void)
{
    /* "aaaa b cc" over lines of 4 cells: line 1 holds " b " and one
       cell of padding, line 2 "cc" */
    vted_row_t row = MakeRow("aaaa b cc");

    CHECK_INT(RowWrappedToCx(&row, 0, 0, 4, 0), 0); /* a */
    CHECK_INT(RowWrappedToCx(&row, 0, 3, 4, 0), 3); /* the last a */
    CHECK_INT(RowWrappedToCx(&row, 1, 0, 4, 0), 4); /* the blank */
    CHECK_INT(RowWrappedToCx(&row, 1, 1, 4, 0), 5); /* b */
    CHECK_INT(RowWrappedToCx(&row, 1, 2, 4, 0), 6); /* the blank after b */

    /* the padding a moved run leaves: the spot lands on the line's
       last glyph instead of dropping onto the next line */
    CHECK_INT(RowWrappedToCx(&row, 1, 3, 4, 0), 6);

    CHECK_INT(RowWrappedToCx(&row, 2, 0, 4, 0), 7); /* c */
    CHECK_INT(RowWrappedToCx(&row, 2, 1, 4, 0), 8); /* the second c */

    /* a spot past the line's text is the end of the row */
    CHECK_INT(RowWrappedToCx(&row, 2, 2, 4, 0), 9);

    /* a tab that runs past the edge covers the cells it blanks */
    {
        vted_row_t tabs = MakeRow("ab\tc");

        RowSetTabWidth(8);
        CHECK_INT(RowWrappedToCx(&tabs, 1, 0, 4, 0), 2); /* the tab */
        RowSetTabWidth(VTED_TAB_STOP);
    }

    /* without a run ever moving, the mapping agrees with the plain
       cell stream */
    {
        vted_row_t plain = MakeRow("abcdefghijkl");

        CHECK_INT(RowWrappedToCx(&plain, 1, 2, 4, 0), 6); /* g */
        CHECK_INT(RowWrappedToCx(&plain, 2, 0, 4, 0), 8); /* i */
    }
}

static void TestWrapWalkersKeepAnIndent(void)
{
    /* the walkers answer for a layout whose continuation lines keep
       their first cells blank: "aa bb cc" over lines of 4 cells with
       an indent of 2 lays out "aa", then "bb" past the indent, then
       the blank of " cc" past its own, then "cc" past it again */
    vted_row_t row = MakeRow("aa bb cc");
    int line;
    int col;

    CHECK_INT(RowWrapCount(&row, 4, 2), 4);

    RowCxToWrapped(&row, 0, 4, 2, &line, &col); /* a */
    CHECK_INT(line, 0);
    CHECK_INT(col, 0);

    RowCxToWrapped(&row, 3, 4, 2, &line, &col); /* b, past the indent */
    CHECK_INT(line, 1);
    CHECK_INT(col, 2);

    RowCxToWrapped(&row, 5, 4, 2, &line, &col); /* the blank of " cc" */
    CHECK_INT(line, 2);
    CHECK_INT(col, 2);

    RowCxToWrapped(&row, 6, 4, 2, &line, &col); /* c, past the indent */
    CHECK_INT(line, 3);
    CHECK_INT(col, 2);

    /* one past the last glyph: the walk stops in the padding of the
       line after the text and rests right after the indent */
    RowCxToWrapped(&row, 8, 4, 2, &line, &col);
    CHECK_INT(line, 4);
    CHECK_INT(col, 2);

    /* the inverse: a spot on a continuation line past its indent is
       the glyph that sits there */
    CHECK_INT(RowWrappedToCx(&row, 1, 2, 4, 2), 3); /* b */
    CHECK_INT(RowWrappedToCx(&row, 3, 2, 4, 2), 6); /* c */

    /* a spot inside the indent of a continuation line lands on the
       line's own first glyph, not on the line before it */
    CHECK_INT(RowWrappedToCx(&row, 1, 0, 4, 2), 3); /* b */
    CHECK_INT(RowWrappedToCx(&row, 1, 1, 4, 2), 3); /* b */

    /* the blank that stayed behind on the first line sits where it
       always did, and the padding a moved run leaves at a line's end
       keeps the line's last glyph */
    CHECK_INT(RowWrappedToCx(&row, 0, 2, 4, 2), 2); /* the blank */
    CHECK_INT(RowWrappedToCx(&row, 2, 3, 4, 2), 5); /* the blank */

    /* a line that filled up: one past its last glyph rests past the
       indent of the line after it (a wide indent leaves a line one
       content cell, so the rest of the row takes a line each) */
    {
        vted_row_t full = MakeRow("abcdefgh");

        RowCxToWrapped(&full, 8, 4, 3, &line, &col);
        CHECK_INT(line, 5);
        CHECK_INT(col, 3);
    }
}


static void TestWrapIndentCells(void)
{
    /* the indent a row's continuation keeps: its own leading
       whitespace laid out in cells, so the wrapped lines line up
       under the first one's text */
    vted_row_t row;

    row = MakeRow("  hi");
    CHECK_INT(RowWrapIndentCells(&row, 4), 2);

    row = MakeRow("hi");
    CHECK_INT(RowWrapIndentCells(&row, 4), 0);

    row = MakeRow("");
    CHECK_INT(RowWrapIndentCells(&row, 4), 0);

    /* a row of nothing but blanks keeps none either */
    row = MakeRow("    ");
    CHECK_INT(RowWrapIndentCells(&row, 4), 0);

    /* a tab is laid out to the stop it reaches */
    RowSetTabWidth(8);
    row = MakeRow("\tcode");
    CHECK_INT(RowWrapIndentCells(&row, 12), 8);
    CHECK_INT(RowWrapIndentCells(&row, 6), 5);
    RowSetTabWidth(VTED_TAB_STOP);

    /* the indent never takes a line's last cell */
    row = MakeRow("      hi");
    CHECK_INT(RowWrapIndentCells(&row, 4), 3);

    /* a width of nothing is treated as one cell */
    row = MakeRow("hi");
    CHECK_INT(RowWrapIndentCells(&row, 0), 0);
}

static void TestWrapRunStart(void)
{
    /* a run of non-blank glyphs that does not fit the rest of its
       line moves down to the next one whole; one that fits, one that
       starts a line, and one longer than a line stay where they are */
    vted_row_t row = MakeRow("ab cdef");

    /* "cdef" takes 4 cells: it does not fit the 2 cells line 0 has
       left from cell 2, so it starts line 1 */
    CHECK_INT(RowWrapRunStart(&row, 3, 2, 4, 0), 4);

    /* from the start of a line there is a whole line of room: no
       move, wherever the run would begin */
    CHECK_INT(RowWrapRunStart(&row, 3, 4, 4, 0), 4);
    CHECK_INT(RowWrapRunStart(&row, 3, 0, 4, 0), 0);

    /* "ab" takes 2 cells and fits the rest of line 0 from cell 2 */
    CHECK_INT(RowWrapRunStart(&row, 0, 2, 4, 0), 2);
    CHECK_INT(RowWrapRunStart(&row, 0, 3, 4, 0), 4);

    /* a run of only blanks takes no cells and never moves */
    {
        vted_row_t blanks = MakeRow("  ");

        CHECK_INT(RowWrapRunStart(&blanks, 0, 2, 4, 0), 2);
    }

    /* a run longer than a whole line breaks at the edge instead */
    {
        vted_row_t long_run = MakeRow("abcdef");

        CHECK_INT(RowWrapRunStart(&long_run, 0, 2, 4, 0), 2);
    }
}

static void TestWrapRunStartKeepsAnIndent(void)
{
    /* with an indent on the continuation lines, a run that moves
       down lands past it, and only a run that fits the cells left
       under the indent moves at all */
    vted_row_t row = MakeRow("ab cdef");

    /* "cdef" needs more than the 2 cells a line of 4 has left under
       an indent of 2: no line fits it whole past the indent, so it
       stays and breaks at the edge instead */
    CHECK_INT(RowWrapRunStart(&row, 3, 2, 4, 2), 2);

    /* a run of 2 cells fits a continuation line exactly: from cell
       3 it does not fit line 0's rest, so it starts line 1 past the
       indent, at cell 4 + 2 */
    {
        vted_row_t two = MakeRow("ab cd");

        CHECK_INT(RowWrapRunStart(&two, 3, 3, 4, 2), 6);
    }

    /* from the content start of a continuation line there is no
       moving left: the run breaks at the edge however long it is */
    CHECK_INT(RowWrapRunStart(&row, 3, 6, 4, 2), 6);

    /* from the first cell of line 0 the run starts as far left as
       it goes, indent or not */
    CHECK_INT(RowWrapRunStart(&row, 3, 0, 4, 2), 0);

    /* an indent wider than a line is clamped to leave one content
       cell: room for nothing whole, so the run stays */
    CHECK_INT(RowWrapRunStart(&row, 3, 2, 4, 9), 2);
}

static void TestWrapAdvanceKeepsAnIndent(void)
{
    /* the walk a renderer and the cursor math share: with an indent
       on the continuation lines, every glyph the layout puts past
       the first line starts at that line's first cell after it */
    vted_row_t row = MakeRow("aa bb cc");
    int at = 0;
    int cell = 0;
    int in_run = 0;

    /* line 0 holds "aa" in cells 0 and 1, cells 2 and 3 stay blank */
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 0);
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 1);

    /* "bb" does not fit the cells line 0 has left after "aa": it
       starts line 1 past its indent, at cell 4 + 2 */
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 2);  /* blank */
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 6);  /* b */
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 7);  /* b */

    /* the walk lands in the padding line 2 keeps for its indent and
       skips it: the blank of " cc" starts at cell 8 + 2 */
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 10); /* blank */

    /* "cc" does not fit the cells line 2 has left after the blank:
       it starts line 3 past its indent */
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 14); /* c */
    CHECK_INT(RowWrapAdvance(&row, 1, 4, 2, &at, &cell, &in_run), 15); /* c */

    /* a glyph that would cross the right edge lands on the next
       line's first cell after the indent */
    {
        vted_row_t wide = MakeRow("aa\xE4\xB8\xAD"); /* a wide glyph */
        int at2 = 0;
        int cell2 = 0;
        int run2 = 0;
        int i;

        for (i = 0; i < 2; i++)
        {
            RowWrapAdvance(&wide, 1, 3, 1, &at2, &cell2, &run2);
        }

        /* the wide glyph does not fit the one cell line 0 has left
           after "aa": it starts line 1 at cell 3 + 1 */
        CHECK_INT(RowWrapAdvance(&wide, 1, 3, 1, &at2, &cell2, &run2), 4);
    }

    /* without an indent the walk lays every line out edge to edge,
       the way it always did */
    {
        vted_row_t plain = MakeRow("aa bb");
        int at3 = 0;
        int cell3 = 0;
        int run3 = 0;

        CHECK_INT(RowWrapAdvance(&plain, 1, 4, 0, &at3, &cell3, &run3), 0);
        CHECK_INT(RowWrapAdvance(&plain, 1, 4, 0, &at3, &cell3, &run3), 1);
        CHECK_INT(RowWrapAdvance(&plain, 1, 4, 0, &at3, &cell3, &run3), 2);
        CHECK_INT(RowWrapAdvance(&plain, 1, 4, 0, &at3, &cell3, &run3), 4); /* b */
    }
}

static void TestFindNext(void)
{
    vted_row_t row = MakeRow("abcabdab");

    /* every match, one at a time: 0, 3 and 6 */
    CHECK_INT(RowFindNext(&row, 0, row.size, "ab", 2), 0);
    CHECK_INT(RowFindNext(&row, 1, row.size, "ab", 2), 3);
    CHECK_INT(RowFindNext(&row, 4, row.size, "ab", 2), 6);
    CHECK_INT(RowFindNext(&row, 7, row.size, "ab", 2), -1);

    /* the bound ends the search before the match at 3 */
    CHECK_INT(RowFindNext(&row, 1, 4, "ab", 2), 3);
    CHECK_INT(RowFindNext(&row, 1, 3, "ab", 2), -1);

    /* a match that would run past the bound does not count */
    CHECK_INT(RowFindNext(&row, 2, 4, "abc", 3), -1);
    CHECK_INT(RowFindNext(&row, 0, row.size, "abc", 3), 0);
    CHECK_INT(RowFindNext(&row, 1, row.size, "abc", 3), -1);

    /* an empty query never matches, and neither does one longer
       than the row */
    CHECK_INT(RowFindNext(&row, 0, row.size, "", 0), -1);
    CHECK_INT(RowFindNext(&row, 0, row.size, "abcdefgh", 8), -1);

    /* out-of-range bounds are clamped into the row */
    CHECK_INT(RowFindNext(&row, -5, row.size, "ab", 2), 0);
    CHECK_INT(RowFindNext(&row, 0, 100, "ab", 2), 0);
}

static void TestFindPrev(void)
{
    vted_row_t row = MakeRow("abcabdab");

    /* the last match before a bound, then the ones before it */
    CHECK_INT(RowFindPrev(&row, 0, row.size, "ab", 2), 6);
    CHECK_INT(RowFindPrev(&row, 0, 7, "ab", 2), 6);
    CHECK_INT(RowFindPrev(&row, 0, 6, "ab", 2), 3);
    CHECK_INT(RowFindPrev(&row, 0, 3, "ab", 2), 0);
    CHECK_INT(RowFindPrev(&row, 0, 1, "ab", 2), 0);
    CHECK_INT(RowFindPrev(&row, 0, 0, "ab", 2), -1);

    /* the bound holds the start of the match, not its end */
    CHECK_INT(RowFindPrev(&row, 0, row.size, "dab", 3), 5);
    CHECK_INT(RowFindPrev(&row, 0, 6, "dab", 3), 5);
    CHECK_INT(RowFindPrev(&row, 0, 5, "dab", 3), -1);

    /* an empty query never matches, and neither does one longer
       than the row */
    CHECK_INT(RowFindPrev(&row, 0, row.size, "", 0), -1);
    CHECK_INT(RowFindPrev(&row, 0, row.size, "abcabdabx", 9), -1);

    /* out-of-range bounds are clamped into the row */
    CHECK_INT(RowFindPrev(&row, -5, 100, "ab", 2), 6);
}

static void TestFindInsideAMultibyteCharacter(void)
{
    /* a match may start inside a multi-byte character: the search
       works on bytes, the way a plain byte search does */
    vted_row_t row = MakeRow("\xE4\xB8\xAD\xE6\x96\x87"); /* two ideographs */

    CHECK_INT(RowFindNext(&row, 0, row.size, "\xB8\xAD", 2), 1);
    CHECK_INT(RowFindPrev(&row, 0, row.size, "\xB8\xAD", 2), 1);
    CHECK_INT(RowFindNext(&row, 2, row.size, "\xB8\xAD", 2), -1);
}

static void TestCountMatches(void)
{
    const char *const texts[] = {"abcabdab", "", "ab", NULL};
    int total;
    int before;

    MakeBuffer(texts);

    /* four matches across the buffer */
    BufferCountMatches("ab", 2, 0, 0, &total, &before);
    CHECK_INT(total, 4);
    CHECK_INT(before, 0);

    /* the second match of the first row */
    BufferCountMatches("ab", 2, 0, 3, &total, &before);
    CHECK_INT(total, 4);
    CHECK_INT(before, 1);

    /* the one on the last row */
    BufferCountMatches("ab", 2, 2, 0, &total, &before);
    CHECK_INT(total, 4);
    CHECK_INT(before, 3);

    /* a place past every match still counts them all */
    BufferCountMatches("ab", 2, 2, 2, &total, &before);
    CHECK_INT(total, 4);
    CHECK_INT(before, 4);

    /* nothing matches */
    BufferCountMatches("zz", 2, 0, 0, &total, &before);
    CHECK_INT(total, 0);
    CHECK_INT(before, 0);

    /* neither does an empty query */
    BufferCountMatches("", 0, 0, 0, &total, &before);
    CHECK_INT(total, 0);
    CHECK_INT(before, 0);
}

static void TestCountMatchesThatShareBytes(void)
{
    /* "aa" matches twice inside "aaa", the way the search steps
       through them */
    const char *const texts[] = {"aaa", NULL};
    int total;
    int before;

    MakeBuffer(texts);

    BufferCountMatches("aa", 2, 0, 0, &total, &before);
    CHECK_INT(total, 2);
    CHECK_INT(before, 0);

    BufferCountMatches("aa", 2, 0, 1, &total, &before);
    CHECK_INT(total, 2);
    CHECK_INT(before, 1);
}

static void TestIndentEdges(void)
{
    /* the levels a row sits in: its own indent edge first, then every
       shallower one still open above it */
    const char *const texts[] = {
        "int main() {",
        "    if (x) {",
        "        foo();",
        "    }",
        "}",
        NULL,
    };
    int edges[8];

    MakeBuffer(texts);

    CHECK_INT(BufferIndentEdges(2, edges, 8), 2); /* foo: its own 8 and the if's 4 */
    CHECK_INT(edges[0], 8);
    CHECK_INT(edges[1], 4);

    CHECK_INT(BufferIndentEdges(1, edges, 8), 1); /* the if: its own 4 alone */
    CHECK_INT(edges[0], 4);

    CHECK_INT(BufferIndentEdges(0, edges, 8), 0); /* flush left: no level */

    CHECK_INT(BufferIndentEdges(3, edges, 8), 1); /* the closing brace: 4 again */
    CHECK_INT(edges[0], 4);

    CHECK_INT(BufferIndentEdges(4, edges, 8), 0); /* so has the last one */
}

static void TestIndentEdgesAcrossBlankRows(void)
{
    /* an empty row and a row of nothing but blanks hold no level of
       their own: the levels above them stay open, and a blank row
       keeps its whole width as its own — the level its blanks sit at */
    const char *const texts[] = {
        "int main() {",
        "  if (x) {",
        "",
        "    foo();",
        "      ",
        "    bar();",
        NULL,
    };
    int edges[8];

    MakeBuffer(texts);

    CHECK_INT(BufferIndentEdges(3, edges, 8), 2);
    CHECK_INT(edges[0], 4);
    CHECK_INT(edges[1], 2);

    /* the blank row: its own 6, then the foo's 4 and the if's 2 it
       sits inside */
    CHECK_INT(BufferIndentEdges(4, edges, 8), 3);
    CHECK_INT(edges[0], 6);
    CHECK_INT(edges[1], 4);
    CHECK_INT(edges[2], 2);

    CHECK_INT(BufferIndentEdges(5, edges, 8), 2);
    CHECK_INT(edges[0], 4);
    CHECK_INT(edges[1], 2);
}

static void TestIndentEdgesLayTabsOut(void)
{
    /* a tab's edge is where it ends, laid out the way the row is
       drawn, so tabs and spaces that indent to the same column mark
       the same level */
    const char *const texts[] = {
        "int main() {",
        "\tif (x) {",
        "\t\tfoo();",
        "                bar();",
        NULL,
    };
    int edges[8];

    MakeBuffer(texts);

    CHECK_INT(BufferIndentEdges(1, edges, 8), 1);
    CHECK_INT(edges[0], 8);

    CHECK_INT(BufferIndentEdges(2, edges, 8), 2);
    CHECK_INT(edges[0], 16);
    CHECK_INT(edges[1], 8);

    /* the sixteen spaces sit in the same levels the tabs indent to */
    CHECK_INT(BufferIndentEdges(3, edges, 8), 2);
    CHECK_INT(edges[0], 16);
    CHECK_INT(edges[1], 8);
}

static void TestIndentEdgesRunOut(void)
{
    /* at most max edges are kept, the deepest ones, and a row outside
       the buffer has none at all */
    const char *const texts[] = {
        "a",
        "  b",
        "    c",
        "      d",
        NULL,
    };
    int edges[2];

    MakeBuffer(texts);

    CHECK_INT(BufferIndentEdges(3, edges, 2), 2);
    CHECK_INT(edges[0], 6);
    CHECK_INT(edges[1], 4);

    CHECK_INT(BufferIndentEdges(3, edges, 0), 0);
    CHECK_INT(BufferIndentEdges(9, edges, 2), 0);
    CHECK_INT(BufferIndentEdges(-1, edges, 2), 0);
}

int main(void)
{
    TestCharWidth();
    TestTabWidth();
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
    TestBracketForwardMatch();
    TestBracketBeforeTheCursor();
    TestBracketNesting();
    TestBracketOtherPairs();
    TestBracketAcrossRows();
    TestBracketMixedPairs();
    TestBracketUnmatched();
    TestBracketWithoutABracket();
    TestBracketOutOfTheBuffer();
    TestBracketBlockSpansItsRows();
    TestBracketBlockOnOneRowIsThatRow();
    TestBracketBlockWithoutAMatch();
    TestDuplicateRowsCopiesTheSpanInOrder();
    TestDuplicateRowsKeepTheirEndings();
    TestDuplicateRowsOfOneRow();
    TestToggleCommentPutsItInFrontOfTheFirstNonBlank();
    TestToggleCommentTakesItOffAgain();
    TestToggleCommentRoundTrips();
    TestToggleCommentSkipsBlankRows();
    TestToggleCommentDirectionOfAMixedSpan();
    TestToggleCommentKnowsBothSpellings();
    TestToggleCommentBounds();
    TestBracketScanStopsAtTheLimit();
    TestWrapCount();
    TestWrapBreaksAtWordBoundaries();
    TestWrapBreaksLongRunsByCharacter();
    TestWrapMovesWideRunsDown();
    TestWrapMovesWideGlyphsDown();
    TestWrapLetsTabsCrossTheEdge();
    TestWrapCursorPosition();
    TestWrappedToCx();
    TestWrapWalkersKeepAnIndent();
    TestWrapIndentCells();
    TestWrapRunStart();
    TestWrapRunStartKeepsAnIndent();
    TestWrapAdvanceKeepsAnIndent();
    TestFindNext();
    TestFindPrev();
    TestFindInsideAMultibyteCharacter();
    TestCountMatches();
    TestCountMatchesThatShareBytes();
    TestIndentEdges();
    TestIndentEdgesAcrossBlankRows();
    TestIndentEdgesLayTabsOut();
    TestIndentEdgesRunOut();

    return TestSummary("row");
}

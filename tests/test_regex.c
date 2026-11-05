/*==========================================================
|        Regex Module: compile and forward search          |
==========================================================*/
#include "tests/vted_test.h"
#include "src/vted_regex.h"
#include "src/vted_utf8.h"

/* runs the forward find the way the search does and hands back the
   start, with the length through out_len when one is given */
static int Find(const char *row, int from, int to, int *out_len)
{
    int len = 0;
    int at = RegexFindNext(row, from, to, &len, NULL);

    if (out_len != NULL)
    {
        *out_len = len;
    }

    return at;
}

static void TestCompile(void)
{
    CHECK(RegexCompile("foo") == 0);
    CHECK(RegexHasPattern() == 1);

    /* a bad pattern leaves nothing behind to match with, and the
       reason it failed is there to show */
    CHECK(RegexCompile("(") == -1);
    CHECK(RegexHasPattern() == 0);
    CHECK(RegexError()[0] != '\0');

    /* a good one after a bad one clears the reason */
    CHECK(RegexCompile("bar") == 0);
    CHECK(RegexError()[0] == '\0');

    /* the same pattern compiled again stands as it is */
    CHECK(RegexCompile("bar") == 0);
    CHECK(RegexHasPattern() == 1);
}

static void TestLiteralFind(void)
{
    CHECK(RegexCompile("bc") == 0);

    int len = 0;

    CHECK(Find("abcd", 0, 4, &len) == 1);
    CHECK_INT(len, 2);

    /* the window keeps the matches it names: one starting at `to`
       does not count, one that starts before it but runs past it
       does, at its full length */
    CHECK(Find("abcd", 2, 4, &len) == -1);
    CHECK(Find("abcd", 0, 2, &len) == 1);
    CHECK_INT(len, 2);

    /* from walks past the matches before it */
    CHECK(Find("abcbcz", 2, 6, &len) == 3);
    CHECK_INT(len, 2);

    /* a row without the pattern holds none */
    CHECK(Find("xyz", 0, 3, &len) == -1);

    /* an empty window and a empty row hold none either */
    CHECK(Find("abc", 2, 2, &len) == -1);
    CHECK(Find("", 0, 0, &len) == -1);
}

static void TestAnchors(void)
{
    CHECK(RegexCompile("^ab") == 0);

    int len = 0;

    /* the head of the row is the only place a ^ match starts */
    CHECK(Find("abab", 0, 4, &len) == 0);
    CHECK_INT(len, 2);
    CHECK(Find("abab", 1, 4, &len) == -1);
    CHECK(Find("xabc", 0, 4, &len) == -1);

    CHECK(RegexCompile("bc$") == 0);

    /* $ pins the match to the end of the row */
    CHECK(Find("abbc", 0, 4, &len) == 2);
    CHECK_INT(len, 2);
    CHECK(Find("abcb", 0, 4, &len) == -1);

    CHECK(RegexCompile("^ab$") == 0);

    CHECK(Find("ab", 0, 2, &len) == 0);
    CHECK_INT(len, 2);
    CHECK(Find("abc", 0, 3, &len) == -1);
}

static void TestEreSyntax(void)
{
    int len = 0;

    CHECK(RegexCompile("cat|dog") == 0);
    CHECK(Find("a dog runs", 0, 10, &len) == 2);
    CHECK_INT(len, 3);

    CHECK(RegexCompile("a+b") == 0);
    CHECK(Find("caaabd", 0, 6, &len) == 1);
    CHECK_INT(len, 4);

    CHECK(RegexCompile("[0-9]+") == 0);
    CHECK(Find("ab12cd", 0, 6, &len) == 2);
    CHECK_INT(len, 2);

    CHECK(RegexCompile("c[[:alpha:]]t") == 0);
    CHECK(Find("the cat", 0, 7, &len) == 4);
    CHECK_INT(len, 3);
}

static void TestGroups(void)
{
    CHECK(RegexCompile("(a)(b)") == 0);

    int len = 0;
    vted_regex_group_t groups[VTED_REGEX_GROUPS];

    CHECK(RegexFindNext("ab", 0, 2, &len, groups) == 0);
    CHECK_INT(len, 2);
    CHECK_INT(groups[0].start, 0);
    CHECK_INT(groups[0].end, 2);
    CHECK_INT(groups[1].start, 0);
    CHECK_INT(groups[1].end, 1);
    CHECK_INT(groups[2].start, 1);
    CHECK_INT(groups[2].end, 2);

    /* a group that took no part in the match keeps -1 */
    CHECK(RegexCompile("(a)|b") == 0);
    CHECK(RegexFindNext("b", 0, 1, &len, groups) == 0);
    CHECK_INT(groups[1].start, -1);
    CHECK_INT(groups[1].end, -1);

    /* without a table the find still runs */
    CHECK(RegexCompile("(a)") == 0);
    CHECK(Find("bab", 0, 3, &len) == 1);
}

static void TestEmptyMatchesSkipped(void)
{
    int len = 0;

    /* a* matches no bytes almost everywhere: those are not matches */
    CHECK(RegexCompile("a*") == 0);
    CHECK(Find("bb", 0, 2, &len) == -1);

    CHECK(Find("aab", 0, 3, &len) == 0);
    CHECK_INT(len, 2);

    CHECK(RegexCompile("x?") == 0);
    CHECK(Find("abc", 0, 3, &len) == -1);
}

static void TestFoldCase(void)
{
    RegexSetIgnoreCase(0);
    CHECK(RegexCompile("foo") == 0);
    CHECK(Find("a FOO b", 0, 7, NULL) == -1);

    /* the fold goes in at the compile, so the same pattern asked
       again after the setting changed is built up afresh */
    RegexSetIgnoreCase(1);
    CHECK(RegexCompile("foo") == 0);
    CHECK(Find("a FOO b", 0, 7, NULL) == 2);

    RegexSetIgnoreCase(0);
    CHECK(RegexCompile("FOO") == 0);
    CHECK(Find("a foo b", 0, 7, NULL) == -1);
}

static void TestUtf8Boundaries(void)
{
    int len = 0;
    vted_regex_group_t groups[VTED_REGEX_GROUPS];

    /* the dot of the extended flavor takes one byte: the match grows
       back to the whole two-byte letter instead of cutting it */
    CHECK(RegexCompile(".") == 0);
    CHECK(Find("\xc3\xa9x", 0, 3, &len) == 0); /* é then x */
    CHECK_INT(len, 2);

    /* a group of the same match lands on the boundaries too */
    CHECK(RegexCompile("(.)") == 0);
    CHECK(RegexFindNext("\xc3\xa9x", 0, 3, &len, groups) == 0);
    CHECK_INT(groups[1].start, 0);
    CHECK_INT(groups[1].end, 2);

    /* a letter written into the pattern as its own bytes matches the
       letter in the row */
    CHECK(RegexCompile("\xc3\xa9") == 0);
    CHECK(Find("caf\xc3\xa9!", 0, 6, &len) == 3);
    CHECK_INT(len, 2);

    /* two of them in a row: from inside the first, the second one is
       still the next the search finds */
    CHECK(Find("\xc3\xa9\xc3\xa9!", 2, 5, &len) == 2);
    CHECK_INT(len, 2);

    /* a search that opens inside a letter is not pulled back onto
       the letter it sits in: the step past it is taken */
    CHECK(Find("\xc3\xa9\xc3\xa9!", 1, 5, &len) == 2);
    CHECK_INT(len, 2);
}

static void TestCache(void)
{
    CHECK(RegexCompile("one") == 0);
    CHECK(Find("one two", 0, 7, NULL) == 0);

    /* a second compile of the same pattern keeps it working */
    CHECK(RegexCompile("one") == 0);
    CHECK(Find("one two", 0, 7, NULL) == 0);

    /* a new pattern takes the old one down */
    CHECK(RegexCompile("two") == 0);
    CHECK(Find("one two", 0, 7, NULL) == 4);
    CHECK(Find("one two", 0, 3, NULL) == -1);

    RegexForget();
    CHECK(RegexHasPattern() == 0);
    CHECK(Find("one two", 0, 7, NULL) == -1);
}

static void TestBackwardFind(void)
{
    int len = 0;

    CHECK(RegexCompile("ab") == 0);

    /* the last match that starts inside the window is the one the
       way back lands on */
    CHECK(RegexFindPrev("abab", 0, 4, &len, NULL) == 2);
    CHECK_INT(len, 2);
    CHECK(RegexFindPrev("abab", 0, 2, &len, NULL) == 0);
    CHECK(RegexFindPrev("abab", 1, 4, &len, NULL) == 2);
    CHECK(RegexFindPrev("abab", 3, 4, &len, NULL) == -1);

    /* a match that starts inside the window but runs past its edge
       still counts, the way the literal search counts it */
    CHECK(RegexFindPrev("abab", 0, 1, &len, NULL) == 0);
    CHECK_INT(len, 2);

    /* the matches that share bytes each get their turn, the way the
       literal search steps through them */
    CHECK(RegexCompile("aa") == 0);
    CHECK(RegexFindPrev("aaa", 0, 3, &len, NULL) == 1);
    CHECK_INT(len, 2);
    CHECK(RegexFindPrev("aaa", 0, 2, &len, NULL) == 1);
    CHECK(RegexFindPrev("aaa", 2, 3, &len, NULL) == -1);

    /* $ holds at the end of the row on the way back too */
    CHECK(RegexCompile("b$") == 0);
    CHECK(RegexFindPrev("abab", 0, 4, &len, NULL) == 3);
    CHECK_INT(len, 1);
    CHECK(RegexFindPrev("abab", 0, 3, &len, NULL) == -1);

    /* a ^ pattern finds the head of the row from the other end the
       same way it does going forward */
    CHECK(RegexCompile("^ab") == 0);
    CHECK(RegexFindPrev("abab", 0, 4, &len, NULL) == 0);
    CHECK(RegexFindPrev("abab", 1, 4, &len, NULL) == -1);

    /* empty matches are not matches on the way back either */
    CHECK(RegexCompile("a*") == 0);
    CHECK(RegexFindPrev("bab", 0, 3, &len, NULL) == 1);
    CHECK_INT(len, 1);
    CHECK(RegexFindPrev("bb", 0, 2, &len, NULL) == -1);

    /* the groups come back with the match they belong to */
    CHECK(RegexCompile("(a+)b") == 0);
    vted_regex_group_t groups[VTED_REGEX_GROUPS];
    CHECK(RegexFindPrev("aab ab", 0, 6, &len, groups) == 4);
    CHECK_INT(len, 2);
    CHECK_INT(groups[1].start, 4);
    CHECK_INT(groups[1].end, 5);

    /* on the way back the walk may step inside an earlier match: the
       ab at 1 starts after the aab at 0, so it is the last one in a
       window that holds them both */
    CHECK(RegexFindPrev("aab ab", 0, 4, &len, groups) == 1);
    CHECK_INT(len, 2);
    CHECK_INT(groups[1].start, 1);
    CHECK_INT(groups[1].end, 2);
    CHECK(RegexFindPrev("aab ab", 0, 1, &len, groups) == 0);
    CHECK_INT(len, 3);
    CHECK_INT(groups[1].start, 0);
    CHECK_INT(groups[1].end, 2);
}

int main(void)
{
    TestCompile();
    TestLiteralFind();
    TestAnchors();
    TestEreSyntax();
    TestGroups();
    TestEmptyMatchesSkipped();
    TestFoldCase();
    TestUtf8Boundaries();
    TestCache();
    TestBackwardFind();

    return TestSummary("regex");
}

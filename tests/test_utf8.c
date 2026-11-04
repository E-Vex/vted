#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_utf8.h"
/*===============================*/

static void TestDecode(void)
{
    int cp;

    CHECK_INT(Utf8Decode("A", 1, &cp), 1);
    CHECK_INT(cp, 'A');

    CHECK_INT(Utf8Decode("\xC3\xA9", 2, &cp), 2); /* é */
    CHECK_INT(cp, 0xE9);

    CHECK_INT(Utf8Decode("\xE2\x82\xAC", 3, &cp), 3); /* € */
    CHECK_INT(cp, 0x20AC);

    CHECK_INT(Utf8Decode("\xF0\x9F\x98\x80", 4, &cp), 4); /* grinning face */
    CHECK_INT(cp, 0x1F600);
}

static void TestDecodeRejectsInvalid(void)
{
    int cp;

    /* every invalid form is one byte long with no code point */
    CHECK_INT(Utf8Decode("\x80", 1, &cp), 1); /* lone continuation */
    CHECK_INT(cp, -1);

    CHECK_INT(Utf8Decode("\xE2\x82", 2, &cp), 1); /* truncated */
    CHECK_INT(cp, -1);

    CHECK_INT(Utf8Decode("\xC0\x80", 2, &cp), 1); /* overlong NUL */
    CHECK_INT(cp, -1);

    CHECK_INT(Utf8Decode("\xE0\x80\x80", 3, &cp), 1); /* overlong */
    CHECK_INT(cp, -1);

    CHECK_INT(Utf8Decode("\xED\xA0\x80", 3, &cp), 1); /* surrogate */
    CHECK_INT(cp, -1);

    CHECK_INT(Utf8Decode("\xF4\x90\x80\x80", 4, &cp), 1); /* > U+10FFFF */
    CHECK_INT(cp, -1);

    CHECK_INT(Utf8Decode("\xFF", 1, &cp), 1);
    CHECK_INT(cp, -1);

    CHECK_INT(Utf8Decode("A\xA9", 1, &cp), 1); /* len cuts the sequence */
}

static void TestBoundaries(void)
{
    /* a(0) é(1,2) €(3,4,5) x(6) */
    const char *text = "a\xC3\xA9\xE2\x82\xAC" "x";
    int len = 7;

    CHECK_INT(Utf8NextBoundary(text, len, 0), 1);
    CHECK_INT(Utf8NextBoundary(text, len, 1), 3);
    CHECK_INT(Utf8NextBoundary(text, len, 3), 6);
    CHECK_INT(Utf8NextBoundary(text, len, 6), 7);
    CHECK_INT(Utf8NextBoundary(text, len, 7), 7); /* stays at the end */

    CHECK_INT(Utf8PrevBoundary(text, 7), 6);
    CHECK_INT(Utf8PrevBoundary(text, 6), 3);
    CHECK_INT(Utf8PrevBoundary(text, 3), 1);
    CHECK_INT(Utf8PrevBoundary(text, 1), 0);
    CHECK_INT(Utf8PrevBoundary(text, 0), 0); /* stays at the start */
}

static void TestBoundariesOnInvalidBytes(void)
{
    /* stray bytes are stepped over one at a time, never skipped */
    CHECK_INT(Utf8PrevBoundary("a\x80", 2), 1);
    CHECK_INT(Utf8PrevBoundary("\xA9\xA9", 2), 1);
    CHECK_INT(Utf8NextBoundary("a\xE2\x82", 3, 1), 2); /* truncated tail */
    CHECK_INT(Utf8NextBoundary("\xFF" "a", 2, 0), 1);
}

static void TestWidths(void)
{
    CHECK_INT(Utf8CharWidth('a'), 1);
    CHECK_INT(Utf8CharWidth(0xE9), 1);     /* é */
    CHECK_INT(Utf8CharWidth(0x20AC), 1);   /* € */
    CHECK_INT(Utf8CharWidth(0x0301), 0);   /* combining acute */
    CHECK_INT(Utf8CharWidth(0x200B), 0);   /* zero width space */
    CHECK_INT(Utf8CharWidth(0x4E2D), 2);   /* a CJK ideograph */
    CHECK_INT(Utf8CharWidth(0xFF21), 2);   /* fullwidth A */
    CHECK_INT(Utf8CharWidth(0x1F600), 2);  /* emoji */
    CHECK_INT(Utf8CharWidth(-1), 1);       /* invalid byte: one cell */
    CHECK_INT(Utf8CharWidth(0x7F), 0);     /* controls are the row's job */
}

static void TestCharClasses(void)
{
    CHECK_INT(Utf8CharClass(' '), Utf8ClassBlank);
    CHECK_INT(Utf8CharClass('\t'), Utf8ClassBlank);
    CHECK_INT(Utf8CharClass(0xA0), Utf8ClassBlank);     /* no-break space */
    CHECK_INT(Utf8CharClass(0x3000), Utf8ClassBlank);   /* ideographic space */

    CHECK_INT(Utf8CharClass('a'), Utf8ClassWord);
    CHECK_INT(Utf8CharClass('Z'), Utf8ClassWord);
    CHECK_INT(Utf8CharClass('7'), Utf8ClassWord);
    CHECK_INT(Utf8CharClass('_'), Utf8ClassWord);
    CHECK_INT(Utf8CharClass(0xE9), Utf8ClassWord);      /* é */
    CHECK_INT(Utf8CharClass(0xAA), Utf8ClassWord);      /* ª is a letter */
    CHECK_INT(Utf8CharClass(0x4E2D), Utf8ClassWord);    /* CJK ideograph */
    CHECK_INT(Utf8CharClass(0x1F600), Utf8ClassWord);   /* emoji */

    CHECK_INT(Utf8CharClass('-'), Utf8ClassPunctuation);
    CHECK_INT(Utf8CharClass('.'), Utf8ClassPunctuation);
    CHECK_INT(Utf8CharClass('\x01'), Utf8ClassPunctuation);
    CHECK_INT(Utf8CharClass(0xA1), Utf8ClassPunctuation);   /* ¡ */
    CHECK_INT(Utf8CharClass(0xD7), Utf8ClassPunctuation);   /* × */
    CHECK_INT(Utf8CharClass(0x2014), Utf8ClassPunctuation); /* em dash */
    CHECK_INT(Utf8CharClass(0x3002), Utf8ClassPunctuation); /* ideographic full stop */
    CHECK_INT(Utf8CharClass(-1), Utf8ClassPunctuation);     /* invalid byte */
}

int main(void)
{
    TestDecode();
    TestDecodeRejectsInvalid();
    TestBoundaries();
    TestBoundariesOnInvalidBytes();
    TestWidths();
    TestCharClasses();

    return TestSummary("utf8");
}

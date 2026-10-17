/*===============================*/
#include "vted_utf8.h"
/*===============================*/

typedef struct
{
    int first;
    int last;
} code_range_t;

/* characters that combine with the previous one or draw nothing */
static const code_range_t zero_width_ranges[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x1AB0, 0x1AFF},
    {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x2028, 0x202E}, {0x2060, 0x2064},
    {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF},
    {0xE0100, 0xE01EF}};

/* East Asian wide and fullwidth characters, and emoji, two cells each */
static const code_range_t wide_ranges[] = {
    {0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC},
    {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF},
    {0xA000, 0xA4CF}, {0xA960, 0xA97F}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
    {0xFE10, 0xFE19}, {0xFE30, 0xFE6F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6},
    {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF},
    {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD}};

#define RANGE_COUNT(table) ((int)(sizeof(table) / sizeof((table)[0])))

static int InRanges(const code_range_t *ranges, int count, int code_point)
{
    int index;

    for (index = 0; index < count; index++)
    {
        if (code_point >= ranges[index].first && code_point <= ranges[index].last)
        {
            return 1;
        }
    }

    return 0;
}

static int IsContinuation(char byte)
{
    return ((unsigned char)byte & 0xC0) == 0x80;
}

int Utf8Decode(const char *s, int len, int *code_point)
{
    const unsigned char *bytes = (const unsigned char *)s;
    int lead;
    int expected;
    int value;
    int index;

    *code_point = -1;

    if (len <= 0)
    {
        return 1;
    }

    lead = bytes[0];

    if (lead < 0x80)
    {
        *code_point = lead;
        return 1;
    }

    /* the lead byte says how long the sequence is; C0 and C1 would
       only encode overlong forms and F5..FF are out of range */
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        expected = 2;
        value = lead & 0x1F;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        expected = 3;
        value = lead & 0x0F;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        expected = 4;
        value = lead & 0x07;
    }
    else
    {
        return 1;
    }

    if (len < expected)
    {
        return 1; /* truncated sequence */
    }

    for (index = 1; index < expected; index++)
    {
        if (!IsContinuation(s[index]))
        {
            return 1;
        }

        value = (value << 6) | (bytes[index] & 0x3F);
    }

    /* reject overlong forms, surrogates and values past U+10FFFF */
    if ((expected == 3 && value < 0x800) ||
        (expected == 4 && value < 0x10000) ||
        (value >= 0xD800 && value <= 0xDFFF) ||
        value > 0x10FFFF)
    {
        return 1;
    }

    *code_point = value;
    return expected;
}

int Utf8PrevBoundary(const char *s, int at)
{
    int start;
    int code_point;

    if (at <= 0)
    {
        return 0;
    }

    /* walk back over continuation bytes, at most three of them */
    start = at - 1;

    while (start > 0 && at - start < 4 && IsContinuation(s[start]))
    {
        start--;
    }

    /* the candidate only counts when it decodes to exactly the
       bytes up to at, otherwise the byte before at stands alone */
    if (Utf8Decode(&s[start], at - start, &code_point) == at - start)
    {
        return start;
    }

    return at - 1;
}

int Utf8NextBoundary(const char *s, int len, int at)
{
    int code_point;

    if (at >= len)
    {
        return len;
    }

    return at + Utf8Decode(&s[at], len - at, &code_point);
}

int Utf8CharWidth(int code_point)
{
    if (code_point < 0)
    {
        return 1; /* invalid bytes are drawn as one replacement cell */
    }

    if (code_point < 0x300)
    {
        /* ASCII and Latin fast path; controls are laid out by the
           row module, here they simply take no width */
        return (code_point < 0x20 || (code_point >= 0x7F && code_point < 0xA0)) ? 0 : 1;
    }

    if (InRanges(zero_width_ranges, RANGE_COUNT(zero_width_ranges), code_point))
    {
        return 0;
    }

    if (InRanges(wide_ranges, RANGE_COUNT(wide_ranges), code_point))
    {
        return 2;
    }

    return 1;
}

/* regex.h is POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <regex.h>
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_regex.h"
#include "vted_terminal.h"
/*===============================*/

/* the pattern stands compiled for the search to run against */
static regex_t compiled;
static int has_pattern = 0;

/* the pattern the compile holds, kept so the same one asked again is
   left as it stands instead of taken down and built back up */
static char *compiled_pattern;
static int compiled_fold = 0;

/* whether the next compile takes the case in */
static int fold_case = 0;

/* the reason the last compile failed, for the status line */
static char error_text[128];

/* 1 when the byte sits inside a multi-byte character instead of
   starting one: the matches have to stand on the boundaries */
static int IsContinuation(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
}

/* walks the start of a match back to the byte its character begins
   at, the way a pattern that looked at one byte of a letter still
   names the whole letter */
static int AlignStart(const char *chars, int start)
{
    while (start > 0 && IsContinuation((unsigned char)chars[start]))
    {
        start--;
    }

    return start;
}

/* walks the end of a match up to the byte its last character ends
   at, so a match never stops inside one */
static int AlignEnd(const char *chars, int size, int end)
{
    while (end < size && IsContinuation((unsigned char)chars[end]))
    {
        end++;
    }

    return end;
}

/* copies the groups of a match into the caller's table when it asked
   for them: a group that took no part keeps -1, and every one of
   them lands on the boundaries of the characters like the match
   itself does */
static void FillGroups(const regmatch_t *m, int base, const char *chars,
                       int size, vted_regex_group_t *groups)
{
    int i;

    if (groups == NULL)
    {
        return;
    }

    for (i = 0; i < VTED_REGEX_GROUPS; i++)
    {
        if (m[i].rm_so < 0)
        {
            groups[i].start = -1;
            groups[i].end = -1;
            continue;
        }

        int start = AlignStart(chars, base + (int)m[i].rm_so);
        int end = AlignEnd(chars, size, base + (int)m[i].rm_eo);

        groups[i].start = start;
        groups[i].end = end;
    }
}

void RegexSetIgnoreCase(int fold)
{
    fold_case = fold != 0;
}

int RegexCompile(const char *pattern)
{
    if (pattern == NULL)
    {
        return -1;
    }

    /* the same pattern under the same case stands already: the
       compile keeps it instead of building it a second time */
    if (has_pattern && compiled_pattern != NULL &&
        compiled_fold == fold_case && strcmp(compiled_pattern, pattern) == 0)
    {
        return 0;
    }

    RegexForget();

    int flags = REG_EXTENDED | (fold_case ? REG_ICASE : 0);
    int failure = regcomp(&compiled, pattern, flags);

    if (failure != 0)
    {
        /* regerror turns the code the compile left behind into words
           the status line can show */
        regerror(failure, &compiled, error_text, sizeof(error_text));
        return -1;
    }

    compiled_pattern = strdup(pattern);

    if (compiled_pattern == NULL)
    {
        KillApp("strdup");
    }

    compiled_fold = fold_case;
    has_pattern = 1;
    error_text[0] = '\0';
    return 0;
}

const char *RegexError(void)
{
    return error_text;
}

void RegexForget(void)
{
    if (has_pattern)
    {
        regfree(&compiled);
        has_pattern = 0;
    }

    free(compiled_pattern);
    compiled_pattern = NULL;
}

int RegexHasPattern(void)
{
    return has_pattern;
}

int RegexFindNext(const char *chars, int from, int to, int *match_len,
                  vted_regex_group_t *groups)
{
    if (!has_pattern || chars == NULL || match_len == NULL)
    {
        return -1;
    }

    int size = (int)strlen(chars);

    if (from < 0)
    {
        from = 0;
    }

    if (to > size)
    {
        to = size;
    }

    regmatch_t m[VTED_REGEX_GROUPS];

    /* a pattern that opens with ^ pins every match of it to the head
       of the row: either the head itself matches or the row holds
       nothing for this window */
    if (compiled_pattern[0] == '^')
    {
        if (from > 0 || to <= 0 ||
            regexec(&compiled, chars, VTED_REGEX_GROUPS, m, 0) != 0)
        {
            return -1;
        }

        if (m[0].rm_so != 0 || m[0].rm_eo == 0)
        {
            return -1; /* an empty match at the head is not one */
        }

        int end = AlignEnd(chars, size, (int)m[0].rm_eo);

        *match_len = end;
        FillGroups(m, 0, chars, size, groups);
        return 0;
    }

    /* the engine reads the tail of the row that starts at from, so
       its leftmost match is the first one of the whole row that
       starts at or after from */
    int at = from;

    while (at <= size)
    {
        if (regexec(&compiled, chars + at, VTED_REGEX_GROUPS, m, 0) != 0)
        {
            return -1;
        }

        int start = at + (int)m[0].rm_so;
        int end = at + (int)m[0].rm_eo;

        if (start >= to)
        {
            return -1; /* the matches only start later and later */
        }

        if (end == start)
        {
            at = start + 1; /* a match of no bytes is not a match */
            continue;
        }

        start = AlignStart(chars, start);
        end = AlignEnd(chars, size, end);

        *match_len = end - start;
        FillGroups(m, at, chars, size, groups);
        return start;
    }

    return -1;
}

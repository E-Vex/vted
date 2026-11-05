#ifndef VTED_REGEX_H
#define VTED_REGEX_H

/*==========================================================
|                     Regex Module                         |
|                                                          |
| Compiles the pattern of a search once and finds its      |
| matches inside a row of bytes. The flavor is the POSIX   |
| extended one, the way grep -E speaks it. A match never   |
| leaves the row it starts in: ^ anchors the head of the   |
| row and $ its end, every match sits on the boundaries of |
| the UTF-8 characters, and a match of no bytes at all is  |
| never reported.                                          |
==========================================================*/

/* the matches the replace expands: the whole match is group 0 and
   the parentheses of the pattern are 1 through 9 */
#define VTED_REGEX_GROUPS 10

typedef struct
{
    int start; /* byte the group starts at in the row, -1 when the
                  group took no part in the match */
    int end;   /* byte just past the group's last one */
} vted_regex_group_t;

/* sets whether the search folds the case: 1 matches letters without
   their case, 0 takes them apart. The next compile takes it in */
void RegexSetIgnoreCase(int fold);

/* compiles the pattern for the search: 0 when it stands, -1 when the
   pattern is a bad one, which leaves nothing behind to match with and
   a reason RegexError() tells. The same pattern compiled again while
   it still stands is left as it is, so a search that runs on every
   keystroke compiles it once */
int RegexCompile(const char *pattern);

/* the reason the last compile failed, or an empty string when the
   last one went through */
const char *RegexError(void);

/* takes the compiled pattern down */
void RegexForget(void);

/* 1 while a pattern stands compiled for the search to run */
int RegexHasPattern(void);

/* the first match of the pattern in the row that starts at or after
   `from`, -1 when the window [from, to) holds none: a match starting
   at `to` or past it does not count, one that starts before it but
   runs past it does. The length in bytes goes through *match_len and
   the groups of the match through `groups` when the caller asks for
   them (VTED_REGEX_GROUPS of them, group 0 the whole match) */
int RegexFindNext(const char *chars, int from, int to, int *match_len,
                  vted_regex_group_t *groups);

#endif

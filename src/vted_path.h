#ifndef VTED_PATH_H
#define VTED_PATH_H
/*==========================================================
|                Path Name Completion                      |
|                                                          |
| The prompts that take a path complete what is typed     |
| with the names of the directory the path names: the     |
| module lists them and tells how far they agree, the    |
| way the shell's own Tab does. A directory listed for    |
| its own sake — the browse of it — is served too, with   |
| every name it holds.                                    |
==========================================================*/

/* the longest name a match carries, and how many matches one
   listing holds: a directory wider than the listing answers with
   the names the sort puts first */
#define VTED_PATH_NAME_MAX 256
#define VTED_PATH_MATCHES 64

/* the order a listing lays its names in: by name the alphabet's
   own, by size the largest first, by time the newest first — the
   way ls -S and ls -t lay their own — names that agree on the key
   falling back to the alphabet so the order never wavers between
   two rounds of the same listing */
typedef enum
{
    VTED_PATH_SORT_NAME,
    VTED_PATH_SORT_SIZE,
    VTED_PATH_SORT_MTIME
} vted_path_sort_t;

typedef struct
{
    char name[VTED_PATH_NAME_MAX]; /* a name the directory answers with */
    int is_dir; /* the name names a directory: it takes a slash behind
                   it, so the walk can keep going into it */
    long long size; /* the bytes the name holds, the way stat counts
                       them: a file its own bytes, a directory the
                       room its own listing takes on the disk */
    long long mtime; /* the second the name was last changed: what a
                        listing can sort by and say, the clock the
                        disk keeps for every name it holds */
} vted_path_match_t;

/* lists the names the directory the typed path names answers the
   prefix with: the bytes up to the last slash name the directory —
   the working one when there is no slash — and what follows the
   slash is the prefix every answering name starts with. A name that
   starts with a dot answers only a prefix that starts with one, the
   way the shell's completion holds the hidden names back until they
   are asked for. The answers come out sorted, at most max of them,
   each saying whether it names a directory, the bytes it holds and
   the second it was last changed. Returns their count, or -1 when
   the directory itself cannot be read */
int PathListMatches(const char *typed, vted_path_match_t *out, int max);

/* lists the names the directory the path names holds that carry the
   filter's bytes anywhere in them — not only at their heads, the way
   the answering listing's prefix asks, but in the middle too, the
   way a narrowing of a listing asks after the names worth taking.
   The hidden ones are held back or taken in as take_hidden says, the
   way the browse holds them, and . and .. stay back either way, the
   walk and its way up. The answers come out in the order sort asks,
   at most max of them, a directory wider than the listing answering
   with the names the sort puts first, each saying whether it names
   a directory, the bytes it holds and the second it was last
   changed. Returns their count, or -1 when the directory itself
   cannot be read */
int PathListFiltered(const char *dir, const char *filter, int take_hidden,
                     vted_path_sort_t sort, vted_path_match_t *out, int max);

/* lists every name the directory the path names holds — the ones
   that start with a dot among them, . and .. held back as the walk
   itself and its way up, which the browse names by its own keys —
   sorted, at most max of them, a directory wider than the listing
   answering with the names the sort puts first, each saying
   whether it names a directory, the bytes it holds and the second
   it was last changed — the answers coming out in the order sort
   asks. Returns the count, or -1 when the directory itself cannot
   be read */
int PathListAll(const char *dir, vted_path_sort_t sort,
                vted_path_match_t *out, int max);

/* the number of bytes the names of a listing agree on at their
   heads: what a path typed so far can take of them without choosing
   between them yet. A listing of one name agrees with all of it, an
   empty one with nothing */
int PathSharedStem(const vted_path_match_t *matches, int count);

#endif

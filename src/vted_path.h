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
| every name it holds, and with the names a hand walking  |
| it makes, moves or takes away.                           |
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
   are asked for. The answers come out in the order sort asks — the
   completion itself reading the alphabet's own — at most max of
   them, each saying whether it names a directory, the bytes it
   holds and the second it was last changed. Returns their count, or
   -1 when the directory itself cannot be read */
int PathListMatches(const char *typed, vted_path_sort_t sort,
                    vted_path_match_t *out, int max);

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

/* creates the name in the directory the path names as an empty
   file: a name already there is not made again, the way the
   shell's touch never writes over what stands. Returns 0 when
   the name came to be, -1 when it did not, the reason standing
   in errno for the caller */
int PathCreateFile(const char *dir, const char *name);

/* creates the name in the directory the path names as a
   directory of its own, empty and ready to walk into. Returns 0
   when the name came to be, -1 when it did not, the reason
   standing in errno for the caller */
int PathCreateDir(const char *dir, const char *name);

/* moves the name a directory holds to the other name it is
   given in the same directory: what the name holds comes with
   it, the way the shell's mv moves a name within one directory.
   Returns 0 when the name moved, -1 when it did not, the reason
   standing in errno for the caller */
int PathRenameName(const char *dir, const char *from, const char *to);

/* takes the name a directory holds away: a file outright, a
   directory only when it holds nothing of its own — the shell's
   rm without its -r, the round never reaching into a directory
   behind the walk's back. Returns 0 when the name is gone, -1
   when it stayed, the reason standing in errno for the caller */
int PathDeleteName(const char *dir, const char *name);

/* copies the name a directory holds into the other directory it is
   given: a file its bytes, a directory its whole tree — the names
   inside laid in as the copy walks them, a link its own pointing —
   the permissions every name holds coming with it. The copy never
   writes over a name already standing in the other directory, the
   exclusive way the create never makes a name again, and a
   directory is not copied into itself — the tree it would make
   holding itself forever. Returns 0 when the name came to stand in
   the other directory, -1 when it did not, the reason standing in
   errno for the caller */
int PathCopyName(const char *from_dir, const char *name,
                 const char *to_dir);

/* the counts the shell's wc makes of a file: the lines its
   newlines make, the words the runs of bytes between the blanks
   make, and the bytes themselves */
typedef struct
{
    long long lines; /* the newlines the file holds, each ending a
                        line the way wc counts them */
    long long words; /* the runs of non-blank bytes the file holds,
                        a word ending where a blank one comes */
    long long bytes; /* the bytes the file holds, all of them */
} vted_path_counts_t;

/* counts the name in the directory the path names the way the
   shell's wc counts a file: the lines its newlines make — a last
   line without its own newline behind it not making one, the way
   wc counts — the words the runs of bytes between the blanks make,
   and the bytes themselves. A directory is no file to count and
   says no, the way wc says nothing of one. Returns 0 with the
   counts laid in *out, or -1 when the name could not be read, the
   reason standing in errno for the caller */
int PathCountName(const char *dir, const char *name,
                  vted_path_counts_t *out);

#endif

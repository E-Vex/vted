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

typedef struct
{
    char name[VTED_PATH_NAME_MAX]; /* a name the directory answers with */
    int is_dir; /* the name names a directory: it takes a slash behind
                   it, so the walk can keep going into it */
} vted_path_match_t;

/* lists the names the directory the typed path names answers the
   prefix with: the bytes up to the last slash name the directory —
   the working one when there is no slash — and what follows the
   slash is the prefix every answering name starts with. A name that
   starts with a dot answers only a prefix that starts with one, the
   way the shell's completion holds the hidden names back until they
   are asked for. The answers come out sorted, at most max of them,
   each saying whether it names a directory. Returns their count, or
   -1 when the directory itself cannot be read */
int PathListMatches(const char *typed, vted_path_match_t *out, int max);

/* lists every name the directory the path names holds — the ones
   that start with a dot among them, . and .. held back as the walk
   itself and its way up, which the browse names by its own keys —
   sorted, at most max of them, a directory wider than the listing
   answering with the names the sort puts first. Each name says
   whether it names a directory. Returns the count, or -1 when the
   directory itself cannot be read */
int PathListAll(const char *dir, vted_path_match_t *out, int max);

/* the number of bytes the names of a listing agree on at their
   heads: what a path typed so far can take of them without choosing
   between them yet. A listing of one name agrees with all of it, an
   empty one with nothing */
int PathSharedStem(const vted_path_match_t *matches, int count);

#endif

/* mkdtemp() and mkdir() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_path.h"
/*===============================*/

/* the tree every check walks in: a fresh directory of its own, made
   by mkdtemp so no run steps on another one's names */
static char root[] = "/tmp/vted_test_path_XXXXXX";
static char sub[512];
static vted_path_match_t matches[VTED_PATH_MATCHES];

/* lists the names the typed path is answered with: the count, or -1
   when the directory itself did not answer */
static int List(const char *typed)
{
    return PathListMatches(typed, VTED_PATH_SORT_NAME,
                           matches, VTED_PATH_MATCHES);
}

static void MakeDir(const char *path)
{
    if (mkdir(path, 0755) != 0)
    {
        perror(path);
        exit(1);
    }
}

static void MakeFile(const char *path)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
    {
        perror(path);
        exit(1);
    }

    fclose(file);
}

/* writes the bytes a file holds, so a listing can be asked after
   sizes it knows */
static void FillFile(const char *path, const char *bytes)
{
    FILE *file = fopen(path, "wb");
    size_t len = strlen(bytes);

    if (file == NULL)
    {
        perror(path);
        exit(1);
    }

    if (fwrite(bytes, 1, len, file) != len)
    {
        perror(path);
        exit(1);
    }

    fclose(file);
}

/* sets the moment a name was last changed, so a listing can be
   asked after a time it knows the same way */
static void SetMtime(const char *path, long long seconds)
{
    struct timespec when[2];

    when[0].tv_sec = (time_t)seconds;
    when[0].tv_nsec = 0;
    when[1].tv_sec = (time_t)seconds;
    when[1].tv_nsec = 0;

    if (utimensat(AT_FDCWD, path, when, 0) != 0)
    {
        perror(path);
        exit(1);
    }
}

static void BuildTree(void)
{
    char path[1024];

    if (mkdtemp(root) == NULL)
    {
        perror(root);
        exit(1);
    }

    /* the working directory the checks without a slash walk in */
    if (chdir(root) != 0)
    {
        perror(root);
        exit(1);
    }

    snprintf(sub, sizeof(sub), "%s/sub", root);
    MakeDir(sub);
    snprintf(path, sizeof(path), "%s/inner.txt", sub);
    MakeFile(path);
    snprintf(path, sizeof(path), "%s/deeper", sub);
    MakeDir(path);

    MakeFile("alpha.txt");
    MakeFile("beta.txt");
    MakeFile("note");
    MakeFile(".hidden");
    MakeDir(".git");
    MakeFile("common1.c");
    MakeFile("common2.h");
}

static void TakeTreeDown(void)
{
    char path[1024];
    int i;

    if (chdir("/") != 0)
    {
        return; /* the names stay in /tmp for the system to sweep */
    }

    snprintf(path, sizeof(path), "%s/deeper", sub);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/inner.txt", sub);
    unlink(path);
    rmdir(sub);

    for (i = 0; i < VTED_PATH_MATCHES + 6; i++)
    {
        snprintf(path, sizeof(path), "cap%03d", i);
        unlink(path);
    }

    unlink("alpha.txt");
    unlink("beta.txt");
    unlink("note");
    unlink(".hidden");
    rmdir(".git");
    unlink("common1.c");
    unlink("common2.h");
    rmdir(root);
}

static void TestWorkingDirectoryAnswers(void)
{
    /* an empty path asks the working directory, which answers with
       its own names: the plain ones, sorted, the hidden ones held
       back until a dot asks for them */
    CHECK_INT(List(""), 6);

    CHECK_STR(matches[0].name, "alpha.txt");
    CHECK_STR(matches[1].name, "beta.txt");
    CHECK_STR(matches[2].name, "common1.c");
    CHECK_STR(matches[3].name, "common2.h");
    CHECK_STR(matches[4].name, "note");
    CHECK_STR(matches[5].name, "sub");
    CHECK_INT(matches[0].is_dir, 0);
    CHECK_INT(matches[5].is_dir, 1);
}

static void TestPrefixes(void)
{
    /* what follows the last slash is the prefix the names answer */
    CHECK_INT(List("al"), 1);
    CHECK_STR(matches[0].name, "alpha.txt");

    CHECK_INT(List("common"), 2);
    CHECK_STR(matches[0].name, "common1.c");
    CHECK_STR(matches[1].name, "common2.h");

    /* a name that starts no answer says so by count alone */
    CHECK_INT(List("zz"), 0);

    /* the hidden names answer a prefix that starts with a dot, and
       only then — the shell's own rule */
    CHECK_INT(List(".h"), 1);
    CHECK_STR(matches[0].name, ".hidden");

    CHECK_INT(List("."), 4);
    CHECK_STR(matches[0].name, ".");
    CHECK_STR(matches[1].name, "..");
    CHECK_STR(matches[2].name, ".git");
    CHECK_STR(matches[3].name, ".hidden");
    CHECK_INT(matches[0].is_dir, 1);
    CHECK_INT(matches[1].is_dir, 1);
    CHECK_INT(matches[2].is_dir, 1);
    CHECK_INT(matches[3].is_dir, 0);

    CHECK_INT(List(".g"), 1);
    CHECK_STR(matches[0].name, ".git");
    CHECK_INT(matches[0].is_dir, 1);
}

static void TestDirectoryWalk(void)
{
    char typed[512];

    /* a trailing slash walks into the directory it names: every
       plain name of its own answers */
    snprintf(typed, sizeof(typed), "sub/");
    CHECK_INT(List(typed), 2);
    CHECK_STR(matches[0].name, "deeper");
    CHECK_STR(matches[1].name, "inner.txt");
    CHECK_INT(matches[0].is_dir, 1);
    CHECK_INT(matches[1].is_dir, 0);

    /* a directory of the working one answers by name, and a prefix
       behind the slash narrows it */
    CHECK_INT(List("sub/inn"), 1);
    CHECK_STR(matches[0].name, "inner.txt");

    /* an absolute path names its directory the same way: the bytes
       up to the last slash, however many it holds */
    snprintf(typed, sizeof(typed), "%s/al", root);
    CHECK_INT(List(typed), 1);
    CHECK_STR(matches[0].name, "alpha.txt");

    snprintf(typed, sizeof(typed), "%s/sub/d", root);
    CHECK_INT(List(typed), 1);
    CHECK_STR(matches[0].name, "deeper");
    CHECK_INT(matches[0].is_dir, 1);

    /* a directory that is not there to answer says so apart from an
       empty answer, so the caller can tell the two apart */
    CHECK_INT(List("nodir/"), -1);
    CHECK_INT(List("nodir/x"), -1);
}

static void TestEveryNameListing(void)
{
    /* the every-name listing takes the hidden names in: every name
       the directory holds answers, . and .. staying back as the
       walk and its way up, and all of them come out sorted the way
       the answering listing sorts them */
    CHECK_INT(PathListAll(".", VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 8);

    CHECK_STR(matches[0].name, ".git");
    CHECK_STR(matches[1].name, ".hidden");
    CHECK_STR(matches[2].name, "alpha.txt");
    CHECK_STR(matches[3].name, "beta.txt");
    CHECK_STR(matches[4].name, "common1.c");
    CHECK_STR(matches[5].name, "common2.h");
    CHECK_STR(matches[6].name, "note");
    CHECK_STR(matches[7].name, "sub");
    CHECK_INT(matches[0].is_dir, 1);
    CHECK_INT(matches[1].is_dir, 0);
    CHECK_INT(matches[7].is_dir, 1);

    /* the room runs out the way it does for the answering listing:
       the names the sort puts first stay standing */
    CHECK_INT(PathListAll(".", VTED_PATH_SORT_NAME, matches, 3), 3);
    CHECK_STR(matches[0].name, ".git");
    CHECK_STR(matches[1].name, ".hidden");
    CHECK_STR(matches[2].name, "alpha.txt");

    /* a directory named with its trailing slash lists the same */
    CHECK_INT(PathListAll("sub/", VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 2);
    CHECK_STR(matches[0].name, "deeper");
    CHECK_STR(matches[1].name, "inner.txt");
    CHECK_INT(matches[0].is_dir, 1);
    CHECK_INT(matches[1].is_dir, 0);

    /* a directory that is not there to answer says so apart from an
       empty answer */
    CHECK_INT(PathListAll("nodir", VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), -1);
}

static void TestSharedStem(void)
{
    /* one name agrees with all of itself */
    CHECK_INT(List("alph"), 1);
    CHECK_INT(PathSharedStem(matches, 1), (int)strlen("alpha.txt"));

    /* several agree as far as they do and no further */
    CHECK_INT(List("common"), 2);
    CHECK_INT(PathSharedStem(matches, 2), (int)strlen("common"));

    /* names that share nothing agree on nothing */
    CHECK_INT(List(""), 6);
    CHECK_INT(PathSharedStem(matches, 6), 0);

    /* the agreement is over bytes, so names that part ways inside a
       character agree only up to the bytes before it: the caller
     walks the answer back to a boundary of its own */
    CHECK_INT(PathSharedStem(NULL, 0), 0);
    CHECK_INT(PathSharedStem(matches, -1), 0);
}

static void TestStemSplitsCharacters(void)
{
    vted_path_match_t split[2];

    /* é and è share their first byte and part on the second: the
       stem is the bytes they truly agree on, the boundary rule
       living with the caller that lays the answer in */
    strcpy(split[0].name, "caf\303\251");
    strcpy(split[1].name, "caf\303\250");

    CHECK_INT(PathSharedStem(split, 2), 4);
}

static void TestListingHoldsWhatItHolds(void)
{
    char name[512];
    int i;

    /* a directory wider than the listing answers with the names the
       sort puts first: the count stops at the room it has */
    for (i = 0; i < VTED_PATH_MATCHES + 6; i++)
    {
        snprintf(name, sizeof(name), "cap%03d", i);
        MakeFile(name);
    }

    CHECK_INT(List("cap"), VTED_PATH_MATCHES);
    CHECK_STR(matches[0].name, "cap000");
    CHECK_STR(matches[VTED_PATH_MATCHES - 1].name,
              "cap063");
}

static void TestFilteredListing(void)
{
    char wide[512];

    /* the filtered listing answers with the names that carry the
       filter's bytes anywhere in them, not only at their heads —
       the middle is where a filter asks, the head only one of the
       places a name can carry them — and all of them come out
       sorted the way every listing sorts them */
    CHECK_INT(PathListFiltered(".", "mmon", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES),
              2);
    CHECK_STR(matches[0].name, "common1.c");
    CHECK_STR(matches[1].name, "common2.h");

    CHECK_INT(PathListFiltered(".", "alph", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES),
              1);
    CHECK_STR(matches[0].name, "alpha.txt");

    /* the dot of a name is a byte like any other: a filter can ask
       after it in the middle of a name */
    CHECK_INT(PathListFiltered(".", ".txt", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES),
              2);
    CHECK_STR(matches[0].name, "alpha.txt");
    CHECK_STR(matches[1].name, "beta.txt");

    /* the hidden names stay back the way the browse holds them,
       until the round takes them in — the filter asking after a
       hidden name does not take it in by itself */
    CHECK_INT(PathListFiltered(".", "hid", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 0);
    CHECK_INT(PathListFiltered(".", "hid", 1, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 1);
    CHECK_STR(matches[0].name, ".hidden");
    CHECK_INT(matches[0].is_dir, 0);

    /* . and .. stay back even taken in, the walk and its way up */
    CHECK_INT(PathListFiltered(".", ".", 1, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 6);
    CHECK_STR(matches[0].name, ".git");
    CHECK_STR(matches[1].name, ".hidden");
    CHECK_STR(matches[2].name, "alpha.txt");
    CHECK_STR(matches[3].name, "beta.txt");
    CHECK_STR(matches[4].name, "common1.c");
    CHECK_STR(matches[5].name, "common2.h");

    /* a filter nothing carries answers with none, and one too long
       for any name to carry answers the same — no name the listing
       holds is that long */
    CHECK_INT(PathListFiltered(".", "zz", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 0);
    memset(wide, 'z', sizeof(wide) - 1);
    wide[sizeof(wide) - 1] = '\0';
    CHECK_INT(PathListFiltered(".", wide, 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 0);

    /* a directory named with its trailing slash answers the same
       way, its names saying whether they name directories */
    CHECK_INT(PathListFiltered("sub/", "e", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES),
              2);
    CHECK_STR(matches[0].name, "deeper");
    CHECK_STR(matches[1].name, "inner.txt");
    CHECK_INT(matches[0].is_dir, 1);
    CHECK_INT(matches[1].is_dir, 0);

    CHECK_INT(PathListFiltered("sub/", "de", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES),
              1);
    CHECK_STR(matches[0].name, "deeper");
    CHECK_INT(matches[0].is_dir, 1);

    /* the bytes of a name in another script are bytes like any
       other: a filter can ask after the ones a character carries */
    MakeFile("caf\303\251.txt");
    CHECK_INT(PathListFiltered(".", "\303\251", 0, VTED_PATH_SORT_NAME,
                               matches, VTED_PATH_MATCHES), 1);
    CHECK_STR(matches[0].name, "caf\303\251.txt");
    unlink("caf\303\251.txt");

    /* the room runs out the way it does for every listing: the
       names the sort puts first stay standing */
    CHECK_INT(PathListFiltered(".", "cap", 0, VTED_PATH_SORT_NAME, matches, 3), 3);
    CHECK_STR(matches[0].name, "cap000");
    CHECK_STR(matches[1].name, "cap001");
    CHECK_STR(matches[2].name, "cap002");

    /* a directory that is not there to answer says so apart from an
       empty answer */
    CHECK_INT(PathListFiltered("nodir", "x", 0, VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES),
              -1);
}

static void TestListingTellsTheMetadata(void)
{
    /* the bytes a name holds and the second it was last changed come
       with the name: a file of ten bytes saying ten, an empty one
       nothing, the moment the clock was set to saying itself — and a
       directory carrying its own the same way, the room its listing
       takes on the disk */
    FillFile("alpha.txt", "0123456789");
    FillFile("beta.txt", "vted");
    SetMtime("alpha.txt", 1700000000);
    SetMtime("beta.txt", 1600000000);
    SetMtime(sub, 1500000000);

    CHECK_INT(List(""), 6);
    CHECK_INT(matches[0].size, 10);
    CHECK_INT(matches[0].mtime, 1700000000);
    CHECK_INT(matches[1].size, 4);
    CHECK_INT(matches[1].mtime, 1600000000);
    CHECK_INT(matches[4].size, 0); /* note: an empty file holds nothing */
    CHECK_INT(matches[5].mtime, 1500000000);

    /* the filtered listing says the same word for a name */
    CHECK_INT(PathListFiltered(".", "beta", 0, VTED_PATH_SORT_NAME, matches,
                               VTED_PATH_MATCHES), 1);
    CHECK_INT(matches[0].size, 4);
    CHECK_INT(matches[0].mtime, 1600000000);

    /* and the every-name listing the same word again, a name among
       the hidden ones keeping its own */
    CHECK_INT(PathListAll(".", VTED_PATH_SORT_NAME, matches, VTED_PATH_MATCHES), 8);
    CHECK_INT(matches[2].size, 10); /* alpha.txt, third among them all */
    CHECK_INT(matches[2].mtime, 1700000000);
}

static void TestSortedListings(void)
{
    /* the order the sort asks: by size the largest first, by time
       the newest first, names that agree on the key falling back to
       the alphabet. The round asks by a filter that leaves the
       directories out, their room on the disk being the
       filesystem's own word and not one the round pinned */
    FillFile("alpha.txt", "0123456789012345678901234567890123456789");
    FillFile("beta.txt", "0123456789");
    FillFile("note", "0123456789012345678901234567890123456789"
                     "0123456789012345678901234567890123456789"
                     "01234567890123456789");
    SetMtime("alpha.txt", 1700000000);
    SetMtime("beta.txt", 1700000000);
    SetMtime("note", 1600000000);

    /* by name: the alphabet's own order */
    CHECK_INT(PathListFiltered(".", "t", 0, VTED_PATH_SORT_NAME,
                               matches, VTED_PATH_MATCHES), 3);
    CHECK_STR(matches[0].name, "alpha.txt");
    CHECK_STR(matches[1].name, "beta.txt");
    CHECK_STR(matches[2].name, "note");

    /* by size: the largest first — note holding a hundred bytes,
       alpha forty, beta ten */
    CHECK_INT(PathListFiltered(".", "t", 0, VTED_PATH_SORT_SIZE,
                               matches, VTED_PATH_MATCHES), 3);
    CHECK_STR(matches[0].name, "note");
    CHECK_STR(matches[1].name, "alpha.txt");
    CHECK_STR(matches[2].name, "beta.txt");

    /* by time: the newest first, alpha and beta agreeing on their
       moment and falling back to the alphabet */
    CHECK_INT(PathListFiltered(".", "t", 0, VTED_PATH_SORT_MTIME,
                               matches, VTED_PATH_MATCHES), 3);
    CHECK_STR(matches[0].name, "alpha.txt");
    CHECK_STR(matches[1].name, "beta.txt");
    CHECK_STR(matches[2].name, "note");

    /* the every-name listing answers with the same orders: every
       moment pinned, so the round knows where each name lands */
    SetMtime(".git", 1300000000);
    SetMtime(".hidden", 1400000000);
    SetMtime(sub, 1500000000);
    SetMtime("common1.c", 1900000000);
    SetMtime("common2.h", 1800000000);

    CHECK_INT(PathListAll(".", VTED_PATH_SORT_MTIME,
                          matches, VTED_PATH_MATCHES), 8);
    CHECK_STR(matches[0].name, "common1.c");
    CHECK_STR(matches[1].name, "common2.h");
    CHECK_STR(matches[2].name, "alpha.txt");
    CHECK_STR(matches[3].name, "beta.txt");
    CHECK_STR(matches[4].name, "note");
    CHECK_STR(matches[5].name, "sub");
    CHECK_STR(matches[6].name, ".hidden");
    CHECK_STR(matches[7].name, ".git");

    /* the room runs out the way it does for every listing: the
       names the sort puts first stay standing */
    FillFile("srt00", "01234567890123456789012345678901234567890123456789"
                      "012345");
    FillFile("srt02", "0123456789012345678901234567890123456789");
    FillFile("srt03", "012345678901234567890123");
    MakeFile("srt01");
    MakeFile("srt04");

    CHECK_INT(PathListFiltered(".", "srt", 0, VTED_PATH_SORT_SIZE,
                               matches, 3), 3);
    CHECK_STR(matches[0].name, "srt00");
    CHECK_STR(matches[1].name, "srt02");
    CHECK_STR(matches[2].name, "srt03");

    unlink("srt00");
    unlink("srt01");
    unlink("srt02");
    unlink("srt03");
    unlink("srt04");
}

static void TestNameOperations(void)
{
    /* a name is made — an empty file the exclusive way, a directory
       of its own — moved to another name with what it holds coming
       along, and taken away, a file outright and a directory only
       when it holds nothing of its own */
    CHECK_INT(PathCreateFile(".", "made.txt"), 0);
    CHECK_INT(List("made."), 1);
    CHECK_STR(matches[0].name, "made.txt");
    CHECK_INT(matches[0].size, 0);
    CHECK_INT(matches[0].is_dir, 0);

    /* a name already there is not made again */
    CHECK_INT(PathCreateFile(".", "made.txt"), -1);
    CHECK_INT(errno, EEXIST);

    /* a directory of its own, empty and ready to walk into */
    CHECK_INT(PathCreateDir(".", "made-dir"), 0);
    CHECK_INT(List("made"), 2);
    CHECK_STR(matches[0].name, "made-dir");
    CHECK_INT(matches[0].is_dir, 1);
    CHECK_STR(matches[1].name, "made.txt");

    CHECK_INT(PathCreateDir(".", "made-dir"), -1);

    /* the move: what the name holds comes with it, the old name
       gone */
    FillFile("made.txt", "what it holds");
    CHECK_INT(PathRenameName(".", "made.txt", "moved.txt"), 0);
    CHECK_INT(List("moved"), 1);
    CHECK_STR(matches[0].name, "moved.txt");
    CHECK_INT(matches[0].size, 13);
    CHECK_INT(List("made.txt"), 0);

    /* a move onto a name of the other kind says no, both names
       standing as they were */
    CHECK_INT(PathRenameName(".", "moved.txt", "made-dir"), -1);
    CHECK_INT(List("moved"), 1);
    CHECK_INT(List("made"), 1);

    /* a file goes outright */
    CHECK_INT(PathDeleteName(".", "moved.txt"), 0);
    CHECK_INT(List("moved"), 0);

    /* an empty directory goes too */
    CHECK_INT(PathDeleteName(".", "made-dir"), 0);
    CHECK_INT(List("made"), 0);

    /* a directory holding a name of its own stays: the round never
       reaches into it behind the walk's back */
    CHECK_INT(PathCreateDir(".", "kept"), 0);
    CHECK_INT(PathCreateFile("kept", "inside"), 0);
    CHECK_INT(PathDeleteName(".", "kept"), -1);
    CHECK_INT(errno, ENOTEMPTY);
    CHECK_INT(List("kept"), 1);
    CHECK_INT(matches[0].is_dir, 1);

    CHECK_INT(PathDeleteName("kept", "inside"), 0);
    CHECK_INT(PathDeleteName(".", "kept"), 0);

    /* a directory moves like any name, what it holds coming along */
    CHECK_INT(PathCreateDir(".", "hold-a"), 0);
    CHECK_INT(PathCreateFile("hold-a", "inside"), 0);
    CHECK_INT(PathRenameName(".", "hold-a", "hold-b"), 0);
    CHECK_INT(List("hold-"), 1);
    CHECK_STR(matches[0].name, "hold-b");
    CHECK_INT(matches[0].is_dir, 1);
    CHECK_INT(PathDeleteName("hold-b", "inside"), 0);
    CHECK_INT(PathDeleteName(".", "hold-b"), 0);

    /* a name that is not there says no, and a directory that is
       not there says no to a make */
    CHECK_INT(PathDeleteName(".", "gone"), -1);
    CHECK_INT(PathCreateFile("nodir", "x"), -1);
    CHECK_INT(PathCreateDir("nodir", "x"), -1);
    CHECK_INT(PathRenameName(".", "gone", "elsewhere"), -1);
}

/* takes a name and everything it holds away, the way the shell's rm
   -r does: the rounds below leave their trees standing, and the
   round after them starts from a clean one */
static void TakeNameAway(const char *path)
{
    char inside[512];
    DIR *handle = opendir(path);
    struct dirent *entry;

    if (handle == NULL)
    {
        unlink(path); /* a name of any other kind goes outright */
        return;
    }

    while ((entry = readdir(handle)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(inside, sizeof(inside), "%s/%s", path, entry->d_name);
        TakeNameAway(inside);
    }

    closedir(handle);
    rmdir(path);
}

/* the counting asked of the copy a round made, so the round knows
   the bytes came across: -1 says the name did not answer at all */
static long long CountedBytes(const char *dir, const char *name)
{
    vted_path_counts_t counts;

    return PathCountName(dir, name, &counts) == 0 ? counts.bytes : -1;
}

static void TestCopyMoveNames(void)
{
    /* a file copies into another directory, its bytes and its
       permissions coming with it and the name it left standing
       where it was */
    struct stat info;
    char aim[256];
    ssize_t aim_len;

    FillFile("carry.txt", "what it holds");
    MakeDir("away");
    chmod("carry.txt", 0741);
    CHECK_INT(PathCopyName(".", "carry.txt", "away"), 0);
    CHECK_INT(CountedBytes("away", "carry.txt"), 13);
    CHECK_INT(stat("away/carry.txt", &info), 0);
    CHECK_INT(info.st_mode & 07777, 0741);
    CHECK_INT(List("carry"), 1);

    /* a name already standing in the other directory is not
       written over */
    CHECK_INT(PathCopyName(".", "carry.txt", "away"), -1);
    CHECK_INT(errno, EEXIST);
    CHECK_INT(CountedBytes("away", "carry.txt"), 13);

    /* a link copies as the link it is, its own pointing coming
       across */
    CHECK_INT(symlink("aim", "pointed"), 0);
    CHECK_INT(PathCopyName(".", "pointed", "away"), 0);
    aim_len = readlink("away/pointed", aim, sizeof(aim) - 1);
    CHECK_INT(aim_len >= 0, 1);
    aim[aim_len > 0 ? aim_len : 0] = '\0';
    CHECK_STR(aim, "aim");

    /* a directory copies as the tree it holds: the names inside
       laid in with it, the nest inside walked the same way, the
       link inside kept its pointing, and the permissions the
       directory holds coming to it once it is full */
    MakeDir("tree");
    FillFile("tree/inside.txt", "the inside");
    MakeDir("tree/nest");
    FillFile("tree/nest/deep.txt", "the depth");
    CHECK_INT(symlink("inside.txt", "tree/point"), 0);
    chmod("tree", 0750);
    CHECK_INT(PathCopyName(".", "tree", "away"), 0);
    CHECK_INT(PathListAll("away/tree", VTED_PATH_SORT_NAME,
                          matches, VTED_PATH_MATCHES), 3);
    CHECK_STR(matches[0].name, "inside.txt");
    CHECK_STR(matches[1].name, "nest");
    CHECK_STR(matches[2].name, "point");
    CHECK_INT(CountedBytes("away/tree", "inside.txt"), 10);
    CHECK_INT(CountedBytes("away/tree/nest", "deep.txt"), 9);
    aim_len = readlink("away/tree/point", aim, sizeof(aim) - 1);
    CHECK_INT(aim_len >= 0, 1);
    aim[aim_len > 0 ? aim_len : 0] = '\0';
    CHECK_STR(aim, "inside.txt");
    CHECK_INT(stat("away/tree", &info), 0);
    CHECK_INT(info.st_mode & 07777, 0750);
    CHECK_INT(List("tree/"), 3);

    /* a directory is not copied onto the spot it stands in, nor
       into the tree it holds — the tree it would make holding
       itself forever */
    CHECK_INT(PathCopyName(".", "tree", "tree"), -1);
    CHECK_INT(errno, EEXIST);
    CHECK_INT(PathCopyName(".", "tree", "tree/nest"), -1);
    CHECK_INT(errno, EINVAL);
    CHECK_INT(List("tree/n"), 1);

    /* a directory that is not there says no to a copy */
    CHECK_INT(PathCopyName(".", "tree", "nodir"), -1);
    CHECK_INT(errno, ENOENT);

    /* a move takes the name into the other directory, what it
       holds coming with it and the old name gone */
    FillFile("shift.txt", "moving on");
    CHECK_INT(PathMoveName(".", "shift.txt", "away"), 0);
    CHECK_INT(List("shift"), 0);
    CHECK_INT(CountedBytes("away", "shift.txt"), 9);

    /* a move onto a name already standing says no, both names
       standing as they were */
    FillFile("shift.txt", "again");
    CHECK_INT(PathMoveName(".", "shift.txt", "away"), -1);
    CHECK_INT(errno, EEXIST);
    CHECK_INT(List("shift"), 1);
    CHECK_INT(CountedBytes("away", "shift.txt"), 9);

    /* a directory moves with its tree, the old name gone */
    MakeDir("walk");
    FillFile("walk/held.txt", "held");
    CHECK_INT(PathMoveName(".", "walk", "away"), 0);
    CHECK_INT(List("walk"), 0);
    CHECK_INT(CountedBytes("away/walk", "held.txt"), 4);

    /* a move into a directory that is not there says no, and a
       directory is not moved into itself */
    CHECK_INT(PathMoveName(".", "shift.txt", "nodir"), -1);
    CHECK_INT(errno, ENOENT);
    CHECK_INT(List("shift"), 1);
    CHECK_INT(PathMoveName(".", "tree", "tree/nest"), -1);
    CHECK_INT(errno, EINVAL);

    /* the names the round made go the way the rounds above take
       theirs down */
    unlink("carry.txt");
    unlink("pointed");
    unlink("shift.txt");
    TakeNameAway("tree");
    TakeNameAway("away");
}

/* the tree a name holds coming away with it, the way the shell's
   rm -r does: a file outright, an empty shell, a tree gone whole,
   a name that is not there, and a shell the round cannot reach
   into saying why it stayed */
static void TestDeleteTrees(void)
{
    /* a file goes outright, the way the plain delete takes it */
    FillFile("tree.txt", "what it holds");
    CHECK_INT(PathDeleteTree(".", "tree.txt"), 0);
    CHECK_INT(List("tree.txt"), 0);

    /* an empty shell goes too */
    MakeDir("shell");
    CHECK_INT(PathDeleteTree(".", "shell"), 0);
    CHECK_INT(List("shell"), 0);

    /* a tree goes whole: the names inside first, the shells they
       sat in behind them, a link taken as the link it is */
    MakeDir("forest");
    FillFile("forest/inside.txt", "the inside");
    MakeDir("forest/nest");
    FillFile("forest/nest/deep.txt", "the depth");
    CHECK_INT(symlink("inside.txt", "forest/point"), 0);
    CHECK_INT(PathDeleteTree(".", "forest"), 0);
    CHECK_INT(List("forest"), 0);

    /* a name that is not there says no */
    CHECK_INT(PathDeleteTree(".", "gone"), -1);
    CHECK_INT(errno, ENOENT);

    /* a shell the hand cannot write into keeps what it holds: the
       round stops where it cannot reach and says why, the names
       before the one it stopped at gone for good */
    MakeDir("sealed");
    FillFile("sealed/inside.txt", "the inside");
    chmod("sealed", 0555);
    CHECK_INT(PathDeleteTree(".", "sealed"), -1);
    CHECK_INT(errno, EACCES);
    CHECK_INT(PathListAll("sealed", VTED_PATH_SORT_NAME,
                          matches, VTED_PATH_MATCHES), 1);
    CHECK_STR(matches[0].name, "inside.txt");

    /* the room open again, the round takes the tree whole */
    chmod("sealed", 0755);
    CHECK_INT(PathDeleteTree(".", "sealed"), 0);
    CHECK_INT(List("sealed"), 0);
}

static void TestCountingName(void)
{
    /* the shell's wc asked of one name: the lines its newlines
       make, the words the runs of bytes between the blanks make,
       and the bytes themselves */
    vted_path_counts_t counts;

    FillFile("counted.txt", "one two\nthree\n");
    CHECK_INT(PathCountName(".", "counted.txt", &counts), 0);
    CHECK_INT(counts.lines, 2);
    CHECK_INT(counts.words, 3);
    CHECK_INT(counts.bytes, 14);

    /* a last line without its own newline behind it makes no line,
       the way wc counts */
    FillFile("tailed.txt", "hello");
    CHECK_INT(PathCountName(".", "tailed.txt", &counts), 0);
    CHECK_INT(counts.lines, 0);
    CHECK_INT(counts.words, 1);
    CHECK_INT(counts.bytes, 5);

    /* the blanks of every kind end a word, and a file of nothing
       but them holds no word at all, its newlines still making
       their lines */
    FillFile("blank.txt", " \t\n \n\n");
    CHECK_INT(PathCountName(".", "blank.txt", &counts), 0);
    CHECK_INT(counts.lines, 3);
    CHECK_INT(counts.words, 0);
    CHECK_INT(counts.bytes, 6);

    /* an empty file holds nothing at all */
    FillFile("hollow.txt", "");
    CHECK_INT(PathCountName(".", "hollow.txt", &counts), 0);
    CHECK_INT(counts.lines, 0);
    CHECK_INT(counts.words, 0);
    CHECK_INT(counts.bytes, 0);

    /* the bytes of a script beyond the alphabet count as they
       stand, a word a word whatever bytes it holds */
    FillFile("accent.txt", "h\303\251llo w\303\266rld\ncaf\303\251\n");
    CHECK_INT(PathCountName(".", "accent.txt", &counts), 0);
    CHECK_INT(counts.lines, 2);
    CHECK_INT(counts.words, 3);
    CHECK_INT(counts.bytes, 20);

    /* a file wider than one round of the counting keeps its words
       whole across the rounds' edges: the last word of a round
       goes on being counted as the one it is when the next round
       takes it up — a thousand rounds of "ab cd" making their two
       thousand words, the word that spans an edge still one */
    {
        char wide[6001];
        int i;

        for (i = 0; i < 1000; i++)
        {
            memcpy(wide + i * 6, "ab cd\n", 6);
        }
        wide[6000] = '\0';
        FillFile("wide.txt", wide);
    }

    CHECK_INT(PathCountName(".", "wide.txt", &counts), 0);
    CHECK_INT(counts.lines, 1000);
    CHECK_INT(counts.words, 2000);
    CHECK_INT(counts.bytes, 6000);

    /* a directory is no file to count, the way wc says nothing of
       one, and a name that is not there says no — the reason
       standing in errno where the round knows it */
    CHECK_INT(PathCountName(".", "sub", &counts), -1);
    CHECK_INT(errno, EISDIR);
    CHECK_INT(PathCountName(".", "gone", &counts), -1);
    CHECK_INT(errno, ENOENT);

    /* the names the round made go the way the rounds above take
       theirs down */
    unlink("counted.txt");
    unlink("tailed.txt");
    unlink("blank.txt");
    unlink("hollow.txt");
    unlink("accent.txt");
    unlink("wide.txt");
}

int main(void)
{
    BuildTree();

    TestWorkingDirectoryAnswers();
    TestPrefixes();
    TestDirectoryWalk();
    TestEveryNameListing();
    TestSharedStem();
    TestStemSplitsCharacters();
    TestListingTellsTheMetadata();
    TestSortedListings();
    TestListingHoldsWhatItHolds();
    TestFilteredListing();
    TestNameOperations();
    TestCopyMoveNames();
    TestDeleteTrees();
    TestCountingName();

    TakeTreeDown();

    return TestSummary("path");
}

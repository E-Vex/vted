/* mkdtemp() and mkdir() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
    return PathListMatches(typed, matches, VTED_PATH_MATCHES);
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

int main(void)
{
    BuildTree();

    TestWorkingDirectoryAnswers();
    TestPrefixes();
    TestDirectoryWalk();
    TestSharedStem();
    TestStemSplitsCharacters();
    TestListingHoldsWhatItHolds();

    TakeTreeDown();

    return TestSummary("path");
}

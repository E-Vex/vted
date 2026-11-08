/* opendir() and stat() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
/*===============================*/
#include "vted_path.h"
/*===============================*/

/* where a directory and one of its names join into the path that
   says whether the name names a directory: the room a typed path
   and a name both fit into comfortably */
#define PATH_JOIN_MAX 2048

/* the directory a typed path names and the prefix its answers start
   with: the bytes up to the last slash name the directory — the
   working one when there is none, the root when the slash leads —
   and what follows the slash is the prefix. The prefix points into
   the typed path itself, so it stands as long as the caller's does */
static void SplitTyped(const char *typed, char *dir, int dir_size,
                       const char **prefix)
{
    const char *last_slash = strrchr(typed, '/');

    if (last_slash == NULL)
    {
        /* no slash: the working directory answers */
        snprintf(dir, (size_t)dir_size, ".");
        *prefix = typed;
    }
    else if (last_slash == typed)
    {
        /* a leading slash: the root answers */
        snprintf(dir, (size_t)dir_size, "/");
        *prefix = typed + 1;
    }
    else
    {
        size_t dir_len = (size_t)(last_slash - typed);

        if (dir_len >= (size_t)dir_size)
        {
            dir_len = (size_t)dir_size - 1;
        }

        memcpy(dir, typed, dir_len);
        dir[dir_len] = '\0';
        *prefix = last_slash + 1;
    }
}

/* says whether the name of a directory names a directory itself: a
   name that cannot be looked at honestly names none, the way a
   broken link names no file */
static int NameIsDir(const char *dir, const char *name)
{
    char joined[PATH_JOIN_MAX];
    struct stat info;

    if (snprintf(joined, sizeof(joined), "%s/%s", dir, name)
        >= (int)sizeof(joined))
    {
        return 0; /* the pair cannot even be named: no directory */
    }

    if (stat(joined, &info) != 0)
    {
        return 0;
    }

    return S_ISDIR(info.st_mode) ? 1 : 0;
}

/* the round a listing of names runs: every entry of the open
   directory that answers goes into out sorted, at most max of them,
   a full listing keeping the names the sort puts first, the largest
   giving way to a smaller one come later. A name that starts with a
   dot answers only a prefix that starts with one, the way the
   shell's completion holds the hidden names back until they are
   asked for, and a name too long for a match to carry is passed
   over rather than cut. Returns the count of the names it laid */
static int CollectMatches(DIR *handle, const char *dir,
                          const char *prefix, vted_path_match_t *out,
                          int max)
{
    struct dirent *entry;
    size_t prefix_len = strlen(prefix);
    int count = 0;

    while ((entry = readdir(handle)) != NULL)
    {
        const char *name = entry->d_name;
        size_t name_len = strlen(name);
        int slot;

        /* a name that starts with a dot answers only a prefix that
           starts with one, the way the shell's completion holds the
           hidden names back until they are asked for */
        if (name[0] == '.' && (prefix_len == 0 || prefix[0] != '.'))
        {
            continue;
        }

        /* a name too long for a match to carry cannot be walked to
           anyway, and one that starts another way is not an answer */
        if (name_len >= sizeof(out->name) ||
            strncmp(name, prefix, prefix_len) != 0)
        {
            continue;
        }

        /* the listing stays sorted, so the menu and the stem read
           the order the shell's own completion would; a full listing
           keeps the names the sort puts first, the largest giving
           way to a smaller one come later */
        if (count == max)
        {
            if (strcmp(out[max - 1].name, name) <= 0)
            {
                continue;
            }
            count = max - 1;
        }

        slot = count;
        while (slot > 0 && strcmp(out[slot - 1].name, name) > 0)
        {
            out[slot] = out[slot - 1];
            slot--;
        }

        memcpy(out[slot].name, name, name_len + 1);
        out[slot].is_dir = NameIsDir(dir, name);
        count++;
    }

    return count;
}

int PathListMatches(const char *typed, vted_path_match_t *out, int max)
{
    char dir[PATH_JOIN_MAX];
    const char *prefix;
    DIR *handle;
    int count;

    SplitTyped(typed, dir, (int)sizeof(dir), &prefix);

    handle = opendir(dir);

    if (handle == NULL)
    {
        return -1; /* the reason stands in errno for the caller */
    }

    count = CollectMatches(handle, dir, prefix, out, max);
    closedir(handle);
    return count;
}

int PathSharedStem(const vted_path_match_t *matches, int count)
{
    size_t shared;
    int i;

    if (count <= 0)
    {
        return 0;
    }

    shared = strlen(matches[0].name);

    for (i = 1; i < count; i++)
    {
        size_t agree = 0;
        size_t bound = strlen(matches[i].name);

        while (agree < shared && agree < bound &&
               matches[0].name[agree] == matches[i].name[agree])
        {
            agree++;
        }

        shared = agree;
    }

    return (int)shared;
}

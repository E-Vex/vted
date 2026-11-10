/* opendir() and stat() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/*===============================*/
#include "vted_path.h"
/*===============================*/

/* where a directory and one of its names join into the path that
   says whether the name names a directory: the room a typed path
   and a name both fit into comfortably */
#define PATH_JOIN_MAX 2048

/* the path a name makes behind the directory it belongs to: the
   pair joined into out. Returns 0 when they fit, -1 when the room
   given cannot hold them */
static int NamePath(const char *dir, const char *name,
                    char *out, int size)
{
    if (snprintf(out, (size_t)size, "%s/%s", dir, name) >= size)
    {
        return -1;
    }

    return 0;
}

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

/* the one look a listing takes at a name: whether it names a
   directory, the bytes it holds and the second it was last changed
   — the same single stat the is_dir alone used to cost. A name that
   cannot be looked at honestly answers as no directory holding
   nothing, changed never, the way a broken link names no file */
static void NameLook(const char *dir, const char *name,
                     int *is_dir, long long *size, long long *mtime)
{
    char joined[PATH_JOIN_MAX];
    struct stat info;

    *is_dir = 0;
    *size = 0;
    *mtime = 0;

    if (NamePath(dir, name, joined, (int)sizeof(joined)) != 0)
    {
        return; /* the pair cannot even be named */
    }

    if (stat(joined, &info) != 0)
    {
        return;
    }

    *is_dir = S_ISDIR(info.st_mode) ? 1 : 0;
    *size = (long long)info.st_size;
    *mtime = (long long)info.st_mtime;
}

/* says whether one name goes before another in the order the sort
   asks: by name the alphabet's own order, by size the largest
   first the way ls -S lays its own, by time the newest first the
   way ls -t does — and names that agree on the key falling back
   to the alphabet, so the order never wavers between two rounds
   of the same listing */
static int MatchBefore(const vted_path_match_t *one,
                       const vted_path_match_t *two,
                       vted_path_sort_t sort)
{
    if (sort == VTED_PATH_SORT_SIZE && one->size != two->size)
    {
        return one->size > two->size;
    }

    if (sort == VTED_PATH_SORT_MTIME && one->mtime != two->mtime)
    {
        return one->mtime > two->mtime;
    }

    return strcmp(one->name, two->name) < 0;
}

/* the round a listing of names runs: every entry of the open
   directory that answers goes into out in the order the sort asks,
   at most max of them, a full listing keeping the names the sort
   puts first, the last of them giving way to a name come later
   that the sort puts before it. A name that starts with a
   dot answers only a prefix that starts with one, the way the
   shell's completion holds the hidden names back until they are
   asked for — unless the round takes the hidden names in, when
   every name the directory holds answers and . and .. stay back,
   the walk and its way up, which the browse names by its own keys —
   and a name too long for a match to carry is passed over rather
   than cut. A filter, when one stands, asks after the bytes a name
   carries anywhere in it, not only at its head, so the round answers
   with the names that carry them somewhere. Returns the count of the
   names it laid */
static int CollectMatches(DIR *handle, const char *dir,
                          const char *prefix, const char *filter,
                          int take_hidden, vted_path_sort_t sort,
                          vted_path_match_t *out, int max)
{
    struct dirent *entry;
    size_t prefix_len = strlen(prefix);
    int count = 0;

    while ((entry = readdir(handle)) != NULL)
    {
        const char *name = entry->d_name;
        size_t name_len = strlen(name);
        vted_path_match_t fresh; /* the name, looked at before it
                                    takes its place */
        int slot;

        if (name[0] == '.')
        {
            if (take_hidden)
            {
                /* the hidden names taken in: every name the
                   directory holds answers, but . and .. — the walk
                   and its way up, which the browse names by its own
                   keys */
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                {
                    continue;
                }
            }
            else if (prefix_len == 0 || prefix[0] != '.')
            {
                /* a name that starts with a dot answers only a
                   prefix that starts with one, the way the shell's
                   completion holds the hidden names back until they
                   are asked for */
                continue;
            }
        }

        /* a name too long for a match to carry cannot be walked to
           anyway, and one that starts another way is not an answer */
        if (name_len >= sizeof(out->name) ||
            strncmp(name, prefix, prefix_len) != 0)
        {
            continue;
        }

        /* the filter asks after the bytes a name carries anywhere in
           it, not only at its head: a name that carries them nowhere
           is not an answer either */
        if (filter != NULL && strstr(name, filter) == NULL)
        {
            continue;
        }

        memcpy(fresh.name, name, name_len + 1);
        NameLook(dir, name, &fresh.is_dir, &fresh.size, &fresh.mtime);

        /* the listing stays in the order the sort asks, so the menu
           and the stem read it the way the asking sort lays it; a
           full listing keeps the names the sort puts first, the
           last of them giving way to a name the sort puts before
           it */
        if (count == max)
        {
            if (!MatchBefore(&fresh, &out[max - 1], sort))
            {
                continue;
            }
            count = max - 1;
        }

        slot = count;
        while (slot > 0 && MatchBefore(&fresh, &out[slot - 1], sort))
        {
            out[slot] = out[slot - 1];
            slot--;
        }

        out[slot] = fresh;
        count++;
    }

    return count;
}

int PathListMatches(const char *typed, vted_path_sort_t sort,
                    vted_path_match_t *out, int max)
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

    count = CollectMatches(handle, dir, prefix, NULL, 0,
                           sort, out, max);
    closedir(handle);
    return count;
}

int PathListFiltered(const char *dir, const char *filter, int take_hidden,
                     vted_path_sort_t sort, vted_path_match_t *out, int max)
{
    DIR *handle;
    int count;

    handle = opendir(dir);

    if (handle == NULL)
    {
        return -1; /* the reason stands in errno for the caller */
    }

    count = CollectMatches(handle, dir, "", filter, take_hidden,
                           sort, out, max);
    closedir(handle);
    return count;
}

int PathListAll(const char *dir, vted_path_sort_t sort,
                vted_path_match_t *out, int max)
{
    DIR *handle;
    int count;

    handle = opendir(dir);

    if (handle == NULL)
    {
        return -1; /* the reason stands in errno for the caller */
    }

    count = CollectMatches(handle, dir, "", NULL, 1, sort, out, max);
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

int PathCreateFile(const char *dir, const char *name)
{
    char path[PATH_JOIN_MAX];
    FILE *file;

    if (NamePath(dir, name, path, (int)sizeof(path)) != 0)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* the x of the mode makes the create exclusive: a name already
       there is not made again */
    file = fopen(path, "wx");

    if (file == NULL)
    {
        return -1;
    }

    fclose(file);
    return 0;
}

int PathCreateDir(const char *dir, const char *name)
{
    char path[PATH_JOIN_MAX];

    if (NamePath(dir, name, path, (int)sizeof(path)) != 0)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (mkdir(path, 0755) != 0)
    {
        return -1;
    }

    return 0;
}

int PathRenameName(const char *dir, const char *from, const char *to)
{
    char old_path[PATH_JOIN_MAX];
    char new_path[PATH_JOIN_MAX];

    if (NamePath(dir, from, old_path, (int)sizeof(old_path)) != 0 ||
        NamePath(dir, to, new_path, (int)sizeof(new_path)) != 0)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (rename(old_path, new_path) != 0)
    {
        return -1;
    }

    return 0;
}

int PathDeleteName(const char *dir, const char *name)
{
    char path[PATH_JOIN_MAX];
    struct stat info;

    if (NamePath(dir, name, path, (int)sizeof(path)) != 0)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (stat(path, &info) != 0)
    {
        return -1; /* the reason stands in errno for the caller */
    }

    if (S_ISDIR(info.st_mode))
    {
        /* a directory goes only when it holds nothing of its own:
           the shell's rm without its -r, the round never reaching
           into a directory behind the walk's back */
        return rmdir(path) == 0 ? 0 : -1;
    }

    return unlink(path) == 0 ? 0 : -1;
}

/* the room one round of the copying reads and writes at a time: a
   file walks into its copy a room at a time, so a file of any size
   is copied without ever standing whole in the memory */
#define COPY_CHUNK 16384

/* copies the file at one path to the other, a room at a time: the
   name the destination gets is not made again — the exclusive way
   the create makes one — and the permissions the file holds come
   with its bytes. A copy that cannot finish leaves nothing of
   itself behind. Returns 0 when the bytes came across, -1 when
   they did not, the reason standing in errno for the caller */
static int CopyFileBytes(const char *from_path, const char *to_path,
                         const struct stat *info)
{
    FILE *from = fopen(from_path, "rb");
    FILE *to;
    unsigned char chunk[COPY_CHUNK];
    size_t got;
    int failed; /* the reason a copy that could not finish left
                   standing in errno */

    if (from == NULL)
    {
        return -1;
    }

    /* the x of the mode makes the copy exclusive: a name already
       standing in the other directory is not written over */
    to = fopen(to_path, "wx");

    if (to == NULL)
    {
        int saved = errno;

        fclose(from);
        errno = saved;
        return -1;
    }

    failed = 0;

    while ((got = fread(chunk, 1, sizeof(chunk), from)) > 0)
    {
        if (fwrite(chunk, 1, got, to) != got)
        {
            failed = errno != 0 ? errno : EIO;
            break;
        }
    }

    if (!failed && ferror(from))
    {
        failed = errno != 0 ? errno : EIO;
    }

    /* the permissions come with the bytes, set while the file is
       still open so no other hand ever sees them half way */
    if (!failed && fchmod(fileno(to),
                          (mode_t)(info->st_mode & 07777)) != 0)
    {
        failed = errno;
    }

    if (failed)
    {
        fclose(to); /* the stream only comes down, the reason
                       already standing */
    }
    else if (fclose(to) != 0)
    {
        failed = errno != 0 ? errno : EIO;
    }

    fclose(from);

    if (failed)
    {
        /* a copy that could not finish leaves nothing of itself
           behind, the disk standing as it stood */
        unlink(to_path);
        errno = failed;
        return -1;
    }

    return 0;
}

int PathCopyName(const char *from_dir, const char *name,
                 const char *to_dir)
{
    char from_path[PATH_JOIN_MAX];
    char to_path[PATH_JOIN_MAX];
    struct stat info;

    if (NamePath(from_dir, name, from_path,
                 (int)sizeof(from_path)) != 0 ||
        NamePath(to_dir, name, to_path, (int)sizeof(to_path)) != 0)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* the link itself is looked at, not what it points at: a link
       is a name of its own kind, and what it points at is no
       business of the copy's */
    if (lstat(from_path, &info) != 0)
    {
        return -1; /* the reason stands in errno for the caller */
    }

    if (S_ISREG(info.st_mode))
    {
        return CopyFileBytes(from_path, to_path, &info);
    }

    if (S_ISDIR(info.st_mode))
    {
        /* a directory is a tree of its own, walked by the copy of
           its own round */
        errno = EISDIR;
        return -1;
    }

    errno = EOPNOTSUPP;
    return -1;
}

/* the room one round of the counting reads at a time: a file walks
   into the counts a room at a time, so a file of any size is
   counted without ever standing whole in the memory */
#define COUNT_CHUNK 4096

int PathCountName(const char *dir, const char *name,
                  vted_path_counts_t *out)
{
    char path[PATH_JOIN_MAX];
    struct stat info;
    FILE *file;
    unsigned char chunk[COUNT_CHUNK];
    size_t got;
    int in_word = 0; /* the counting stands inside a word: the next
                        blank byte ends it */

    out->lines = 0;
    out->words = 0;
    out->bytes = 0;

    if (NamePath(dir, name, path, (int)sizeof(path)) != 0)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (stat(path, &info) != 0)
    {
        return -1; /* the reason stands in errno for the caller */
    }

    if (S_ISDIR(info.st_mode))
    {
        /* a directory is no file to count, the way wc says nothing
           of one */
        errno = EISDIR;
        return -1;
    }

    file = fopen(path, "rb");

    if (file == NULL)
    {
        return -1;
    }

    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        size_t i;

        for (i = 0; i < got; i++)
        {
            if (chunk[i] == '\n')
            {
                out->lines++;
            }

            /* a word is a run of bytes between the blanks, the way
               wc counts them: the first byte of one after a blank
               — or before any, at the file's own head — makes the
               word, the blanks inside a run keeping it going */
            if (isspace(chunk[i]))
            {
                in_word = 0;
            }
            else if (!in_word)
            {
                in_word = 1;
                out->words++;
            }
        }

        out->bytes += (long long)got;
    }

    if (ferror(file))
    {
        int saved = errno;

        fclose(file);
        errno = saved;
        return -1;
    }

    fclose(file);
    return 0;
}

/* getline() and strdup() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_row.h"
#include "vted_file.h"
/*===============================*/

int OpenFile(const char *filename)
{
    /* start from an empty buffer whatever it held before */
    FreeBuffer();

    FILE *file = fopen(filename, "r");

    if (file == NULL)
    {
        if (errno != ENOENT)
        {
            return -1;
        }

        /* a path that does not exist yet is a new file: start with an
           empty buffer that remembers the name, so Ctrl-S creates it */
        free(vted_buffer.filename);
        vted_buffer.filename = strdup(filename);

        return vted_buffer.filename == NULL ? -1 : 0;
    }

    /* remember which file this buffer belongs to */
    free(vted_buffer.filename);
    vted_buffer.filename = strdup(filename);

    if (vted_buffer.filename == NULL)
    {
        fclose(file);
        return -1;
    }

    /* only regular files can be loaded this way; anything else
       (a directory, a device, a pipe) is not a text file */
    struct stat info;

    if (fstat(fileno(file), &info) == -1 || !S_ISREG(info.st_mode))
    {
        fclose(file);
        return -1;
    }

    /* read the whole file at once: fewer allocations than a line by
       line loop, and NUL bytes or other binary content survive
       intact instead of being truncated at the first NUL */
    long file_size = (long)info.st_size;
    char *content = malloc((size_t)file_size + 1);

    if (content == NULL)
    {
        fclose(file);
        return -1;
    }

    size_t got = fread(content, 1, (size_t)file_size, file);
    int read_error = ferror(file);

    fclose(file);

    if (read_error != 0 || got != (size_t)file_size)
    {
        free(content);
        return -1;
    }

    content[file_size] = '\0';

    int terminated = 1; /* did the last line end with a terminator */
    long pos = 0;

    while (pos < file_size)
    {
        long end = pos;

        while (end < file_size && content[end] != '\n')
        {
            end++;
        }

        /* take exactly one terminator off the end: \n, \r\n, or a
           lone \r on the final line. A \r anywhere else stays in
           the row and shows as ^M instead of being silently dropped */
        int line_len = (int)(end - pos);
        int eol = VTED_EOL_LF;

        if (end < file_size)
        {
            terminated = 1;

            if (line_len > 0 && content[end - 1] == '\r')
            {
                line_len--;
                eol = VTED_EOL_CRLF;
            }

            end++; /* skip the \n itself */
        }
        else
        {
            terminated = 0;

            if (line_len > 0 && content[end - 1] == '\r')
            {
                line_len--;
                eol = VTED_EOL_CR;
                terminated = 1;
            }
        }

        if (AppendRow(content + pos, line_len, eol) == -1)
        {
            free(content);
            return -1;
        }

        pos = end;
    }

    free(content);

    vted_buffer.final_newline = terminated;

    /* the unterminated last line has no ending of its own: it takes
       the one above it, so splitting it later writes the file's own
       convention rather than Unix's */
    if (!terminated && vted_buffer.num_rows > 1)
    {
        vted_buffer.rows[vted_buffer.num_rows - 1].eol =
            vted_buffer.rows[vted_buffer.num_rows - 2].eol;
    }

    return 0;
}

/* the bytes that terminate a row on save */
static const char *RowEnding(const vted_row_t *row, int *len)
{
    switch (row->eol)
    {
    case VTED_EOL_CRLF:
        *len = 2;
        return "\r\n";
    case VTED_EOL_CR:
        *len = 1;
        return "\r";
    default:
        *len = 1;
        return "\n";
    }
}

/* writes every row and its terminator, nothing else */
static int WriteRows(FILE *file)
{
    int row_index;

    for (row_index = 0; row_index < vted_buffer.num_rows; row_index++)
    {
        vted_row_t *row = &vted_buffer.rows[row_index];

        if (fwrite(row->chars, 1, (size_t)row->size, file) != (size_t)row->size)
        {
            return -1;
        }

        /* the last row of a file without a trailing terminator keeps
           having none; every other row ends its own way */
        if (row_index == vted_buffer.num_rows - 1 && !vted_buffer.final_newline)
        {
            continue;
        }

        int ending_len;
        const char *ending = RowEnding(row, &ending_len);

        if (fwrite(ending, 1, (size_t)ending_len, file) != (size_t)ending_len)
        {
            return -1;
        }
    }

    return 0;
}

/* the path of a temporary file next to the target: rename is only
   atomic within one file system, so it must be a sibling */
static char *TempPathFor(const char *filename)
{
    size_t dir_len = 0;
    const char *slash = strrchr(filename, '/');

    if (slash != NULL)
    {
        dir_len = (size_t)(slash - filename) + 1; /* keep the slash */
    }

    char *path = malloc(dir_len + sizeof(".vtedXXXXXX"));

    if (path == NULL)
    {
        return NULL;
    }

    memcpy(path, filename, dir_len);
    memcpy(path + dir_len, ".vtedXXXXXX", sizeof(".vtedXXXXXX"));

    return path;
}

/* best effort: sync the directory so the rename is on disk too */
static void SyncParentDirectory(const char *filename)
{
    /* some file systems refuse to sync a directory, and that must
       not fail a save that already succeeded */
    const char *slash = strrchr(filename, '/');
    char stack_dir[64];
    char *dir;

    if (slash == NULL)
    {
        dir = stack_dir;
        strcpy(dir, ".");
    }
    else if (slash == filename)
    {
        dir = stack_dir;
        strcpy(dir, "/");
    }
    else
    {
        size_t dir_len = (size_t)(slash - filename);
        dir = malloc(dir_len + 1);

        if (dir == NULL)
        {
            return;
        }

        memcpy(dir, filename, dir_len);
        dir[dir_len] = '\0';
    }

    int fd = open(dir, O_RDONLY | O_DIRECTORY);

    if (fd != -1)
    {
        fsync(fd);
        close(fd);
    }

    if (dir != stack_dir)
    {
        free(dir);
    }
}

int SaveFile(void)
{
    if (vted_buffer.filename == NULL)
    {
        return -1; /* unnamed buffer has nowhere to go */
    }

    /* a save that fails halfway must not destroy the file it saves:
       the rows go to a sibling temporary file first and the finished
       result is renamed over the target, so the name always points
       at either the whole old file or the whole new one */
    char *temp_path = TempPathFor(vted_buffer.filename);

    if (temp_path == NULL)
    {
        return -1;
    }

    int fd = mkstemp(temp_path);

    if (fd == -1)
    {
        free(temp_path);
        return -1;
    }

    FILE *file = fdopen(fd, "w");

    if (file == NULL)
    {
        int saved_errno = errno;
        close(fd);
        unlink(temp_path);
        free(temp_path);
        errno = saved_errno;
        return -1;
    }

    int ok = WriteRows(file) == -1 ? -1 : 0;

    if (ok == 0 && fflush(file) != 0)
    {
        ok = -1;
    }

    /* the mode of the file being replaced, or the usual one for a
       file created by the save */
    if (ok == 0)
    {
        struct stat info;
        mode_t mode;

        if (stat(vted_buffer.filename, &info) == 0)
        {
            mode = info.st_mode & 07777;
        }
        else
        {
            mode_t mask = umask(0);
            umask(mask);
            mode = 0666 & ~mask;
        }

        if (fchmod(fd, mode) == -1)
        {
            ok = -1;
        }
    }

    /* the data reaches the disk before the name changes */
    if (ok == 0 && fsync(fd) == -1)
    {
        ok = -1;
    }

    if (fclose(file) != 0)
    {
        ok = -1;
    }

    if (ok == -1 || rename(temp_path, vted_buffer.filename) == -1)
    {
        int saved_errno = errno;
        unlink(temp_path);
        free(temp_path);
        errno = saved_errno;
        return -1;
    }

    free(temp_path);

    SyncParentDirectory(vted_buffer.filename);

    return 0;
}

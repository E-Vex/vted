/* getline() and strdup() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_row.h"
#include "vted_file.h"
/*===============================*/

int OpenFile(const char *filename)
{
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

    char *line = NULL;    /* getline() allocates and grows this buffer */
    size_t line_cap = 0;  /* current allocation size of line */
    ssize_t line_len;     /* bytes read by getline(), -1 at end of file */
    int terminated = 1;   /* did the last line end with a terminator */

    while ((line_len = getline(&line, &line_cap, file)) != -1)
    {
        /* take exactly one terminator off the end: \n, \r\n, or a
           lone \r (which can only end the final line of a file,
           as getline() splits on \n alone). A \r anywhere else
           stays in the row and shows as ^M instead of being
           silently dropped */
        int eol = VTED_EOL_LF;

        terminated = 0;

        if (line_len > 0 && line[line_len - 1] == '\n')
        {
            line_len--;
            terminated = 1;

            if (line_len > 0 && line[line_len - 1] == '\r')
            {
                line_len--;
                eol = VTED_EOL_CRLF;
            }
        }
        else if (line_len > 0 && line[line_len - 1] == '\r')
        {
            line_len--;
            terminated = 1;
            eol = VTED_EOL_CR;
        }

        if (AppendRow(line, (int)line_len, eol) == -1)
        {
            free(line);
            fclose(file);
            return -1;
        }
    }

    free(line);

    /* getline() also returns -1 on a read error (a directory fails
       with EISDIR): do not pass a half-read file off as the whole */
    if (ferror(file))
    {
        int saved_errno = errno;

        fclose(file);
        errno = saved_errno;
        return -1;
    }

    fclose(file);

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

int SaveFile(void)
{
    if (vted_buffer.filename == NULL)
    {
        return -1; /* unnamed buffer has nowhere to go */
    }

    FILE *file = fopen(vted_buffer.filename, "w");

    if (file == NULL)
    {
        return -1;
    }

    if (WriteRows(file) == -1)
    {
        fclose(file);
        return -1;
    }

    if (fclose(file) != 0)
    {
        return -1;
    }

    return 0;
}

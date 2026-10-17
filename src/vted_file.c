/* getline() and strdup() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        return -1;
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

    while ((line_len = getline(&line, &line_cap, file)) != -1)
    {
        /* strip the trailing newline; handle \r\n and lone \r
           so files from other operating systems load cleanly */
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
        {
            line_len--;
        }

        if (AppendRow(line, (int)line_len) == -1)
        {
            free(line);
            fclose(file);
            return -1;
        }
    }

    free(line);
    fclose(file);
    return 0;
}

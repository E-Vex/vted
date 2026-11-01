/*===============================*/
#include <stdio.h>
/*===============================*/
#include "vted_cli.h"
/*===============================*/

void CliParse(int argc, char *argv[], vted_cli_options_t *options)
{
    /* an argument that starts with '-' but is longer than that alone
       is an option; no option exists yet, so it is rejected right
       here on the standard error stream, with a usage hint, before
       any terminal mode is entered. "-" by itself is not an option:
       it is the file operand it has always been */
    int i;

    options->file = NULL;
    options->error = 0;

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] != '\0')
        {
            fprintf(stderr, "vted: unknown option: %s\n", argv[i]);
            fputs("usage: vted [file]\n", stderr);
            options->error = 1;
            return;
        }

        /* the first operand is the file to open; further ones keep
           being ignored the way they always were */
        if (options->file == NULL)
        {
            options->file = argv[i];
        }
    }
}

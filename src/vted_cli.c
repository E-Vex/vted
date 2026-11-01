/*===============================*/
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_cli.h"
/*===============================*/

/* the Makefile passes the real version; a hand compile without it
   says so instead of inventing a number */
#ifndef VTED_VERSION
#define VTED_VERSION "unknown"
#endif

static const char *const usage_text =
    "usage: vted [+LINE] [file]\n"
    "\n"
    "Open file in the vex terminal text editor, or start with an\n"
    "empty buffer when no file is given.\n"
    "\n"
    "  -h, --help     print this help and exit\n"
    "  -V, --version  print the version and exit\n"
    "  +LINE          start the cursor on that line\n";

static void HintUsage(void)
{
    fputs("Try 'vted --help' for more information.\n", stderr);
}

void CliPrintUsage(FILE *out)
{
    fputs(usage_text, out);
}

void CliPrintVersion(FILE *out)
{
    fprintf(out, "vted %s\n", VTED_VERSION);
}

/* reads a +LINE operand: digits only, at least 1, and given at most
   once; anything else is rejected with the argument itself */
static int ParseStartLine(const char *arg, int *line)
{
    char *end;
    long value;

    if (!isdigit((unsigned char)arg[1]))
    {
        return -1;
    }

    value = strtol(arg + 1, &end, 10);

    if (*end != '\0' || value < 1 || value > INT_MAX)
    {
        return -1;
    }

    *line = (int)value;
    return 0;
}

void CliParse(int argc, char *argv[], vted_cli_options_t *options)
{
    int i;

    options->file = NULL;
    options->start_line = 0;
    options->want_help = 0;
    options->want_version = 0;
    options->error = 0;

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '+')
        {
            int line = 0;

            if (ParseStartLine(argv[i], &line) == -1)
            {
                fprintf(stderr, "vted: not a line number: %s\n", argv[i]);
                HintUsage();
                options->error = 1;
                return;
            }

            if (options->start_line != 0)
            {
                fprintf(stderr, "vted: +LINE given twice: %s\n", argv[i]);
                HintUsage();
                options->error = 1;
                return;
            }

            options->start_line = line;
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0')
        {
            /* the two spellings of the help option answer with the
               usage text instead of an error, and win over everything
               else on the line */
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            {
                options->want_help = 1;
                continue;
            }

            /* so do the two spellings of the version option */
            if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0)
            {
                options->want_version = 1;
                continue;
            }

            fprintf(stderr, "vted: unknown option: %s\n", argv[i]);
            HintUsage();
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

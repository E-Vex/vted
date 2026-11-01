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
    "usage: vted [+LINE] [FILE | -]...\n"
    "\n"
    "Open every FILE in its own buffer, or start with an empty buffer\n"
    "when no file is given.\n"
    "\n"
    "  -h, --help     print this help and exit\n"
    "  -V, --version  print the version and exit\n"
    "  +LINE          start the cursor on that line of the first file\n"
    "  -              read the text from standard input\n";

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

/* the file operands of the current parse, in the order they were
   given. The options struct hands the array out and the next parse
   rebuilds it */
static const char **open_files;
static int open_files_count;
static int open_files_capacity;

/* remembers one more file operand, in order */
static void RememberFile(const char *file)
{
    if (open_files_count == open_files_capacity)
    {
        int capacity = open_files_capacity > 0 ? open_files_capacity * 2 : 4;
        const char **grown = realloc(open_files, (size_t)capacity * sizeof *grown);

        if (grown == NULL)
        {
            fputs("vted: out of memory\n", stderr);
            exit(1);
        }

        open_files = grown;
        open_files_capacity = capacity;
    }

    open_files[open_files_count++] = file;
}

void CliParse(int argc, char *argv[], vted_cli_options_t *options)
{
    int i;
    int read_stdin = 0;

    options->files = NULL;
    options->file_count = 0;
    options->start_line = 0;
    options->want_help = 0;
    options->want_version = 0;
    options->error = 0;

    open_files_count = 0;

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

        /* a lone "-" is the text on standard input; it can be named
           only once, for there is only one stream */
        if (strcmp(argv[i], "-") == 0)
        {
            if (read_stdin)
            {
                fprintf(stderr, "vted: '-' given twice: %s\n", argv[i]);
                HintUsage();
                options->error = 1;
                return;
            }

            read_stdin = 1;
        }

        RememberFile(argv[i]);
    }

    options->files = open_files;
    options->file_count = open_files_count;
}

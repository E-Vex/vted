#ifndef VTED_CLI_H
#define VTED_CLI_H

/*==========================================================
|                  Command Line Module                     |
|                                                          |
| Reads the arguments the editor was started with into a   |
| plain struct, before anything touches the terminal, so   |
| a bad argument is answered on the standard error         |
| stream with a normal exit instead of the editor dying    |
| from inside raw mode. vted.c asks for the options        |
| once at startup and hands the file on to the file        |
| module.                                                   |
==========================================================*/

#include <stdio.h>

typedef struct
{
    const char *file; /* the operand naming the file to open, NULL when absent */
    int read_stdin;   /* the operand "-": the text comes from standard input */
    int start_line;   /* the +LINE operand, 1-based; 0 when absent */
    int want_help;    /* -h or --help was given: print the usage and leave */
    int want_version; /* -V or --version was given: print the version and leave */
    int error;        /* 1 when the arguments were rejected on stderr */
} vted_cli_options_t;

/* scans argv and fills options; a rejected argument is reported on
   stderr with a hint toward --help */
void CliParse(int argc, char *argv[], vted_cli_options_t *options);

/* the usage text: what the editor takes on the command line */
void CliPrintUsage(FILE *out);

/* the version the editor was built as, from the Makefile */
void CliPrintVersion(FILE *out);

#endif

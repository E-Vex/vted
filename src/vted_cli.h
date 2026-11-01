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

typedef struct
{
    const char *file; /* the operand naming the file to open, NULL when absent */
    int error;        /* 1 when the arguments were rejected on stderr */
} vted_cli_options_t;

/* scans argv and fills options; a rejected argument is reported on
   stderr together with a usage hint */
void CliParse(int argc, char *argv[], vted_cli_options_t *options);

#endif

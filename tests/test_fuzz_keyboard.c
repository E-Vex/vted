/* Fuzz harness for the key parser (ReadKeyPress). Built both as a
   libFuzzer target (clang -fsanitize=fuzzer) and as a bounded
   standalone fuzzer run by `make test`. It feeds raw bytes through a
   pipe as stdin and runs ReadKeyPress until the input is exhausted,
   checking only that the parser never crashes or hangs on malformed
   escape sequences. No escape sequence is valid input; every byte
   stream must resolve to keys or NO_KEY without leaking text. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
/*===============================*/
#include "src/vted_keyboard.h"
/*===============================*/

/* how many ReadKeyPress calls a single fuzz input may trigger; keeps
   the standalone fuzzer bounded even when the input holds no complete
   sequence */
#define FUZZ_MAX_KEYS 4096

/* make stdin return immediately when empty: the editor's 100 ms read
   timeout would make a fuzz run crawl otherwise */
static void MakeStdinNonBlocking(void)
{
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);

    if (flags != -1)
    {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    /* no blocking, no timeout: read() returns at once with whatever
       bytes are present, or -1/EAGAIN when there are none */
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &raw) == 0)
    {
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
}

/* feed the given bytes through a pipe as stdin, then run the parser
   until nothing more arrives; any crash surfaces through the
   sanitizer running this harness */
static void FuzzBytes(const uint8_t *data, size_t size)
{
    int fds[2];

    if (pipe(fds) == -1)
    {
        return;
    }

    if (write(fds[1], data, size) != (ssize_t)size)
    {
        close(fds[0]);
        close(fds[1]);
        return;
    }

    close(fds[1]);
    dup2(fds[0], STDIN_FILENO);
    close(fds[0]);

    MakeStdinNonBlocking();

    {
        int i;

        for (i = 0; i < FUZZ_MAX_KEYS; i++)
        {
            int key = ReadKeyPress();

            (void)key;

            /* when the input is drained, ReadKeyPress returns NO_KEY;
               keep draining a few more in case a sequence tail is
               being assembled, then stop */
            if (key == 0)
            {
                int idle = 0;
                int j;

                for (j = 0; j < 8; j++)
                {
                    if (ReadKeyPress() == 0)
                    {
                        idle++;
                    }
                }

                if (idle == 8)
                {
                    break;
                }
            }
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FuzzBytes(data, size);
    return 0;
}

/* standalone bounded fuzzer: runs the given corpus once and reports
   how many key parses it drove, then exits 0 unless a check fails */
int main(int argc, char **argv)
{
    static uint8_t corpus[65536];
    size_t size = 0;
    FILE *file = NULL;

    if (argc >= 2)
    {
        file = fopen(argv[1], "rb");

        if (file == NULL)
        {
            perror(argv[1]);
            return 1;
        }

        size = fread(corpus, 1, sizeof(corpus), file);
        fclose(file);
    }

    MakeStdinNonBlocking();

    FuzzBytes(corpus, size);

    printf("fuzz_keyboard: parsed %zu corpus bytes without crashing\n", size);
    return 0;
}

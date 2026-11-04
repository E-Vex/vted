/*===============================*/
/* clock_gettime() is POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
/*===============================*/
#include "src/vted_row.h"
#include "src/vted_screen.h"
#include "src/vted_edit.h"
#include "src/vted_undo.h"
#include "src/vted_file.h"
/*===============================*/

/* about a hundred megabytes of text, line by line */
#define BENCH_TARGET_SIZE (100L * 1024 * 1024)
static const char bench_path[] = "/tmp/vted_bench_100mb.txt";

static double now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static long peak_rss_kb(void)
{
    FILE *status = fopen("/proc/self/status", "r");
    char line[128];
    long kb = -1;

    if (status != NULL)
    {
        while (fgets(line, sizeof(line), status) != NULL)
        {
            if (strncmp(line, "VmHWM:", 6) == 0)
            {
                kb = atol(line + 6);
                break;
            }
        }

        fclose(status);
    }

    return kb;
}

static int generate_file(void)
{
    /* only create the file when it is missing, so repeated runs are
       comparable */
    struct stat info;

    if (stat(bench_path, &info) == 0)
    {
        return 0;
    }

    FILE *file = fopen(bench_path, "wb");

    if (file == NULL)
    {
        return -1;
    }

    char line[128];
    long written = 0;
    long index = 0;

    while (written < BENCH_TARGET_SIZE)
    {
        int len = snprintf(line, sizeof(line),
                           "The quick brown fox jumps over the lazy dog %07ld\n",
                           index);

        if (len <= 0)
        {
            fclose(file);
            return -1;
        }

        if (fwrite(line, 1, (size_t)len, file) != (size_t)len)
        {
            fclose(file);
            return -1;
        }

        written += len;
        index++;
    }

    fclose(file);
    return 0;
}

int main(void)
{
    struct stat info;
    double t0, t1;
    long rows;
    long size;

    if (generate_file() == -1)
    {
        fprintf(stderr, "bench: cannot create %s\n", bench_path);
        return 1;
    }

    if (stat(bench_path, &info) == -1)
    {
        fprintf(stderr, "bench: stat failed\n");
        return 1;
    }

    size = (long)info.st_size;
    printf("file: %s (%ld bytes)\n", bench_path, size);

    /* ---------------- load ---------------- */
    t0 = now();

    if (OpenFile(bench_path) == -1)
    {
        fprintf(stderr, "bench: OpenFile failed\n");
        return 1;
    }

    t1 = now();
    rows = vted_buffer.num_rows;
    printf("load:  %.0f ms  (%.0f MB/s, %ld rows)\n",
           (t1 - t0) * 1e3, (double)size / (1024.0 * 1024.0) / (t1 - t0), rows);

    /* ---------------- save ---------------- */
    {
        char save_path[256];
        snprintf(save_path, sizeof(save_path), "%s.copy", bench_path);
        unlink(save_path);
        free(vted_buffer.filename);
        vted_buffer.filename = strdup(save_path);

        t0 = now();

        if (SaveFile() == -1)
        {
            fprintf(stderr, "bench: SaveFile failed\n");
            return 1;
        }

        t1 = now();
        printf("save:  %.0f ms  (%.0f MB/s)\n",
               (t1 - t0) * 1e3, (double)size / (1024.0 * 1024.0) / (t1 - t0));

        /* restore the original name so later saves go where they should */
        free(vted_buffer.filename);
        vted_buffer.filename = strdup(bench_path);
    }

    /* ---------------- editing ---------------- */
    {
        const char *text = "bench edit burst: 0123456789 "
                           "abcdefghij 0123456789 abcdefghij ";
        int text_len = (int)strlen(text);
        int repeats = 200;
        int total = text_len * repeats;

        /* insert 20 bursts at the end of the last row */
        vted_editor_config.cursor_y = (int)(rows - 1);
        vted_editor_config.cursor_x = vted_buffer.rows[rows - 1].size;

        t0 = now();

        for (int i = 0; i < repeats; i++)
        {
            EditorInsertText(text, text_len);
        }

        t1 = now();
        printf("insert: %.0f ms  (%d bytes, %.0f bytes/ms)\n",
               (t1 - t0) * 1e3, total, (double)total / ((t1 - t0) * 1e3));

        /* ---------------- undo/redo ---------------- */
        t0 = now();

        for (int i = 0; i < repeats; i++)
        {
            if (EditorUndo() == 0)
            {
                break;
            }
        }

        t1 = now();
        printf("undo:   %.0f ms (%d steps)\n", (t1 - t0) * 1e3, repeats);

        for (int i = 0; i < repeats; i++)
        {
            if (EditorRedo() == 0)
            {
                break;
            }
        }

        printf("peak RSS: %ld kB\n", peak_rss_kb());
    }

    return 0;
}

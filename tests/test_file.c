/* mkstemp() is POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_row.h"
#include "src/vted_file.h"
/*===============================*/

/* the file every check loads and saves; mkstemp makes it unique */
static char test_path[] = "/tmp/vted_test_file_XXXXXX";
static char read_back[512];

static void WriteFile(const char *content, int len)
{
    FILE *file = fopen(test_path, "wb");

    if (file == NULL)
    {
        perror(test_path);
        exit(1);
    }

    fwrite(content, 1, (size_t)len, file);
    fclose(file);
}

static const char *ReadBack(void)
{
    FILE *file = fopen(test_path, "rb");

    if (file == NULL)
    {
        perror(test_path);
        exit(1);
    }

    size_t got = fread(read_back, 1, sizeof(read_back) - 1, file);

    fclose(file);
    read_back[got] = '\0';
    return read_back;
}

/* writes the content, loads it, and leaves the buffer ready for checks */
static void Load(const char *content, int len)
{
    FreeBuffer();
    WriteFile(content, len);

    if (OpenFile(test_path) == -1)
    {
        perror(test_path);
        exit(1);
    }
}

/* loads content, saves it back, and returns the bytes on disk */
static const char *RoundTrip(const char *content, int len)
{
    Load(content, len);

    if (SaveFile() == -1)
    {
        perror(test_path);
        exit(1);
    }

    return ReadBack();
}

static void TestUnixEndings(void)
{
    CHECK_STR(RoundTrip("one\ntwo\n", 8), "one\ntwo\n");
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_LF);
    CHECK_INT(vted_buffer.final_newline, 1);
}

static void TestWindowsEndings(void)
{
    CHECK_STR(RoundTrip("one\r\ntwo\r\n", 10), "one\r\ntwo\r\n");
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.final_newline, 1);
}

static void TestMixedEndings(void)
{
    CHECK_STR(RoundTrip("one\r\ntwo\nthree\r\n", 16), "one\r\ntwo\nthree\r\n");
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_LF);
    CHECK_INT(vted_buffer.rows[2].eol, VTED_EOL_CRLF);
}

static void TestClassicMacEndings(void)
{
    /* getline() splits on \n alone, so a CR-only file is one row
       with the \r bytes kept in the text */
    CHECK_STR(RoundTrip("a\rb\rc\r", 6), "a\rb\rc\r");
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(vted_buffer.rows[0].size, 5);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CR);
    CHECK_INT(vted_buffer.final_newline, 1);
}

static void TestInteriorCarriageReturn(void)
{
    /* a \r in the middle of a line is text, not a terminator */
    CHECK_STR(RoundTrip("a\rb\n", 4), "a\rb\n");
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a\rb");
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);
}

static void TestNoTrailingNewline(void)
{
    CHECK_STR(RoundTrip("one\ntwo", 7), "one\ntwo");
    CHECK_INT(vted_buffer.final_newline, 0);
    /* the unterminated line takes the ending of the one above it */
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_LF);
}

static void TestNoTrailingNewlineWindows(void)
{
    CHECK_STR(RoundTrip("a\r\nb", 5), "a\r\nb");
    CHECK_INT(vted_buffer.final_newline, 0);
    /* splitting b later must write a CRLF, not an LF */
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
}

static void TestDegenerateFiles(void)
{
    CHECK_STR(RoundTrip("", 0), "");
    CHECK_INT(vted_buffer.num_rows, 0);

    CHECK_STR(RoundTrip("\n", 1), "\n");
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(vted_buffer.rows[0].size, 0);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);

    CHECK_STR(RoundTrip("\r\n", 2), "\r\n");
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);
}

static void TestSaveWritesRowEndings(void)
{
    FreeBuffer();

    AppendRow("a", 1, VTED_EOL_LF);
    AppendRow("b", 1, VTED_EOL_CRLF);

    /* the buffer owns no file yet */
    CHECK_INT(SaveFile(), -1);

    /* the buffer takes ownership of the name, like OpenFile does */
    vted_buffer.filename = strdup(test_path);

    vted_buffer.final_newline = 1;
    CHECK_INT(SaveFile(), 0);
    CHECK_STR(ReadBack(), "a\nb\r\n");

    vted_buffer.final_newline = 0;
    CHECK_INT(SaveFile(), 0);
    CHECK_STR(ReadBack(), "a\nb");

    /* a row that is not last always ends, whatever the buffer says */
    AppendRow("c", 1, VTED_EOL_LF);
    CHECK_INT(SaveFile(), 0);
    CHECK_STR(ReadBack(), "a\nb\r\nc");
}

static void TestMissingAndSpecialFiles(void)
{
    FreeBuffer();

    /* a path that does not exist opens as a new empty buffer */
    CHECK_INT(OpenFile("/tmp/vted_test_missing_file"), 0);
    CHECK_INT(vted_buffer.num_rows, 0);
    CHECK_INT(vted_buffer.final_newline, 1);

    /* a directory cannot be read: the loader reports it */
    CHECK_INT(OpenFile("/tmp"), -1);
}

int main(void)
{
    int fd = mkstemp(test_path);

    if (fd == -1)
    {
        perror("mkstemp");
        return 1;
    }
    close(fd);

    TestUnixEndings();
    TestWindowsEndings();
    TestMixedEndings();
    TestClassicMacEndings();
    TestInteriorCarriageReturn();
    TestNoTrailingNewline();
    TestNoTrailingNewlineWindows();
    TestDegenerateFiles();
    TestSaveWritesRowEndings();
    TestMissingAndSpecialFiles();

    unlink(test_path);

    return TestSummary("file");
}

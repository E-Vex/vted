/* mkstemp() and fmemopen() are POSIX, not ISO C11 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_screen.h"
#include "src/vted_row.h"
#include "src/vted_edit.h"
#include "src/vted_undo.h"
#include "src/vted_buffers.h"
/*===============================*/

/* two files to open as buffers; mkstemp makes them unique */
static char first_path[] = "/tmp/vted_test_buffers_1_XXXXXX";
static char second_path[] = "/tmp/vted_test_buffers_2_XXXXXX";

static void WriteFile(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
    {
        perror(path);
        exit(1);
    }

    fwrite(content, 1, strlen(content), file);
    fclose(file);
}

/* starts from a pristine editor: no buffers, empty globals, no
   history. The live buffer of the previous check is freed by hand
   the way the editor core does on its way out */
static void Begin(void)
{
    BuffersFreeOthers();
    FreeBuffer();
    UndoReset();

    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;
    vted_editor_config.row_offset = 0;
    vted_editor_config.col_offset = 0;
    vted_editor_config.tab_width = VTED_TAB_STOP;
    RowSetTabWidth(VTED_TAB_STOP);
}

static void TestStartEmpty(void)
{
    Begin();

    BuffersStartEmpty();

    CHECK_INT(BuffersCount(), 1);
    CHECK_INT(BuffersCurrent(), 0);
    CHECK(vted_buffer.filename == NULL);
    CHECK_INT(vted_buffer.num_rows, 0);
    CHECK_INT(EditorUndo(), 0); /* nothing was ever changed */
}

static void TestOpenKeepsTheFirstBufferWaiting(void)
{
    Begin();
    WriteFile(first_path, "");
    BuffersStartEmpty();

    CHECK_INT(BuffersOpen(first_path), 0);

    CHECK_INT(BuffersCount(), 2);
    CHECK_INT(BuffersCurrent(), 1);
    CHECK_STR(vted_buffer.filename, first_path);
    CHECK_INT(vted_buffer.num_rows, 0); /* the file was empty */

    /* the unnamed buffer the editor started in is still there */
    CHECK_INT(BuffersSwitch(0), 1);
    CHECK(vted_buffer.filename == NULL);

    /* and the file comes back with it */
    CHECK_INT(BuffersSwitch(1), 1);
    CHECK_STR(vted_buffer.filename, first_path);
}

static void TestOpenLoadsTheText(void)
{
    Begin();
    WriteFile(first_path, "one\ntwo\n");

    CHECK_INT(BuffersOpen(first_path), 0);

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(vted_editor_config.cursor_y, 0); /* starts at the top */
    CHECK_INT(EditorUndo(), 0); /* a loaded file has no history */
}

static void TestAMissingFileOpensEmptyAndNamed(void)
{
    Begin();

    CHECK_INT(BuffersOpen("/tmp/vted_test_buffers_absent"), 0);

    CHECK_INT(BuffersCount(), 1);
    CHECK_STR(vted_buffer.filename, "/tmp/vted_test_buffers_absent");
    CHECK_INT(vted_buffer.num_rows, 0);
}

static void TestAFailedOpenKeepsTheCurrentBuffer(void)
{
    Begin();
    WriteFile(first_path, "kept\n");
    CHECK_INT(BuffersOpen(first_path), 0);

    /* a directory is not a regular file: the open fails */
    CHECK_INT(BuffersOpen("/tmp"), -1);

    CHECK_INT(BuffersCount(), 1); /* nothing was added */
    CHECK_INT(BuffersCurrent(), 0);
    CHECK_STR(vted_buffer.filename, first_path);
    CHECK_STR(vted_buffer.rows[0].chars, "kept");
}

static void TestOpeningTheSameFileAgainSwitchesToIt(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    WriteFile(second_path, "two\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);
    CHECK_INT(BuffersCount(), 2);

    /* the same literal path: the first buffer comes back into view */
    CHECK_INT(BuffersOpen(first_path), 0);
    CHECK_INT(BuffersCount(), 2); /* no third buffer appeared */
    CHECK_INT(BuffersCurrent(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "one");

    /* a different spelling of the same file resolves to it too */
    const char *base = strrchr(first_path, '/');
    char indirection[300];

    snprintf(indirection, sizeof(indirection), "/tmp/../tmp%s", base);

    CHECK_INT(BuffersOpen(indirection), 0);
    CHECK_INT(BuffersCount(), 2);
    CHECK_INT(BuffersCurrent(), 0);

    /* the path of a new file matches itself the same way */
    CHECK_INT(BuffersOpen("/tmp/vted_test_buffers_absent"), 0);
    CHECK_INT(BuffersOpen("/tmp/vted_test_buffers_absent"), 0);
    CHECK_INT(BuffersCount(), 3);
    CHECK_INT(BuffersCurrent(), 2);
}

static void TestTheHistoryTravelsWithTheBuffer(void)
{
    Begin();
    WriteFile(first_path, "ab\n");
    BuffersOpen(first_path);

    /* type into the file's buffer */
    EditorInsertChar('X');
    CHECK_STR(vted_buffer.rows[0].chars, "Xab");
    CHECK_INT(vted_buffer.dirty, 1);

    /* another buffer is opened and worked on */
    WriteFile(second_path, "cd\n");
    BuffersOpen(second_path);
    CHECK_INT(BuffersCount(), 2);
    CHECK_INT(EditorUndo(), 0); /* this buffer has no changes yet */

    EditorInsertChar('Y');
    CHECK_STR(vted_buffer.rows[0].chars, "Ycd");

    /* back at the first buffer, its own change is still there to
       undo, and only its own */
    CHECK_INT(BuffersSwitch(0), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "Xab");
    CHECK_INT(vted_buffer.dirty, 1); /* it keeps its unsaved marker */

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "Xab");

    /* and the second buffer's change survives its wait untouched */
    CHECK_INT(BuffersSwitch(1), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "Ycd");
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "cd");
}

static void TestTheCursorAndViewportWaitWithTheBuffer(void)
{
    Begin();
    WriteFile(first_path, "one\ntwo\nthree\n");
    BuffersOpen(first_path);

    vted_editor_config.cursor_y = 2;
    vted_editor_config.cursor_x = 1;
    vted_editor_config.row_offset = 1;
    vted_editor_config.col_offset = 0;
    vted_editor_config.preferred_rx = 3;

    /* any second buffer to switch away with */
    FILE *stream = fmemopen((char *)"other\n", 6, "r");

    CHECK_INT(BuffersOpenStream(stream), 0);
    fclose(stream);

    CHECK_INT(BuffersCurrent(), 1);

    CHECK_INT(BuffersSwitch(0), 1);
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 1);
    CHECK_INT(vted_editor_config.row_offset, 1);
    CHECK_INT(vted_editor_config.preferred_rx, 3);
}

static void TestAStreamOpensAsAnUnnamedBuffer(void)
{
    Begin();

    FILE *stream = fmemopen((char *)"piped\nlines\n", 12, "r");

    CHECK_INT(BuffersOpenStream(stream), 0);
    fclose(stream);

    CHECK_INT(BuffersCount(), 1);
    CHECK_INT(BuffersCurrent(), 0);
    CHECK(vted_buffer.filename == NULL);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "lines");
}

static void TestSwitchBounds(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    WriteFile(second_path, "two\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    CHECK_INT(BuffersSwitch(-1), 0);
    CHECK_INT(BuffersSwitch(2), 0);
    CHECK_INT(BuffersSwitch(BuffersCurrent()), 0); /* the live one is a no-op */

    CHECK_INT(BuffersCurrent(), 1);
    CHECK_INT(BuffersCount(), 2);
}

static void TestDirtyCountsAcrossBuffers(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    WriteFile(second_path, "two\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    CHECK_INT(BuffersDirtyCount(), 0);

    EditorInsertChar('X'); /* the live buffer changes */
    CHECK_INT(BuffersDirtyCount(), 1);

    BuffersSwitch(0); /* a waiting buffer changes its mind... */

    EditorInsertChar('Y');
    CHECK_INT(BuffersDirtyCount(), 2); /* ...and both count now */

    BuffersSwitch(1);
    CHECK_INT(BuffersDirtyCount(), 2); /* switching does not forget */
}

static void TestNamesAndDirtyFlags(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    WriteFile(second_path, "two\n");

    /* out of range answers nothing, unsaved or not */
    CHECK(BuffersName(-1) == NULL);
    CHECK(BuffersName(0) == NULL);
    CHECK_INT(BuffersDirty(0), 0);

    BuffersOpen(first_path);
    BuffersOpen(second_path);

    /* the live buffer's name is the one in the globals, the waiting
       one's is in its slot */
    CHECK_STR(BuffersName(0), first_path);
    CHECK_STR(BuffersName(1), second_path);
    CHECK_INT(BuffersDirty(1), 0);

    EditorInsertChar('X'); /* the live buffer changes */

    CHECK_INT(BuffersDirty(1), 1);
    CHECK_INT(BuffersDirty(0), 0);

    BuffersSwitch(0); /* the changed one waits, the other is live */

    CHECK_INT(BuffersDirty(1), 1);
    CHECK_INT(BuffersDirty(0), 0);

    /* an unnamed buffer has no name, modified or not */
    {
        FILE *stream = fmemopen((char *)"piped\n", 6, "r");

        BuffersOpenStream(stream);
        fclose(stream);
    }
    CHECK(BuffersName(2) == NULL);
    CHECK_INT(BuffersDirty(2), 0);
    EditorInsertChar('Y');
    CHECK_INT(BuffersDirty(2), 1);
}

static void TestAWaitingBufferReadsWhileAnotherIsEdited(void)
{
    Begin();
    WriteFile(first_path, "one\ntwo\n");
    WriteFile(second_path, "three\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    /* out of range, or the buffer being edited: nothing to read */
    CHECK(BuffersText(-1) == NULL);
    CHECK(BuffersText(2) == NULL);
    CHECK(BuffersText(BuffersCurrent()) == NULL);

    /* the waiting one's rows read the same as its file */
    {
        const vted_buffer_t *text = BuffersText(0);

        CHECK(text != NULL);
        CHECK_INT(text->num_rows, 2);
        CHECK_STR(text->rows[0].chars, "one");
        CHECK_STR(text->rows[1].chars, "two");
    }
}

static void TestAWordCompletesFromTheOtherBuffersToo(void)
{
    char words[16][32];
    char prefix[32];
    int prefix_len = 0;
    int count;
    int i;

    Begin();
    WriteFile(first_path, "fool food\n");
    WriteFile(second_path, "foo\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    /* the cursor sits past foo, the whole word of the live buffer */
    vted_editor_config.cursor_y = 0;
    vted_editor_config.cursor_x = 3;

    count = BufferCollectCompletions(0, 3, &words[0][0], 16, 32, &prefix_len);
    CHECK_INT(count, 0); /* the live buffer has nothing but the word itself */
    CHECK_INT(prefix_len, 3);

    memcpy(prefix, &vted_buffer.rows[0].chars[0], (size_t)prefix_len);
    prefix[prefix_len] = '\0';

    /* the other buffers follow the live one around the ring, the way
       Alt-/'s menu reads them */
    for (i = 1; count < 16 && i < BuffersCount(); i++)
    {
        const vted_buffer_t *text =
            BuffersText((BuffersCurrent() + i) % BuffersCount());

        if (text == NULL)
        {
            continue; /* the live one, collected above already */
        }

        count = BufferCollectTextWords(text, prefix, prefix_len,
                                       &words[0][0], 16, 32, count);
    }

    CHECK_INT(count, 2);
    CHECK_STR(words[0], "fool");
    CHECK_STR(words[1], "food");
}

static void TestEachBufferKeepsItsOwnTabWidth(void)
{
    Begin();
    WriteFile(first_path, "a\tb\n");
    WriteFile(second_path, "c\td\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    /* a width set while a buffer is edited is its own */
    vted_editor_config.tab_width = 4;
    RowSetTabWidth(4);

    CHECK_INT(BuffersSwitch(0), 1);
    CHECK_INT(vted_editor_config.tab_width, 8); /* the first one kept the default */
    CHECK_INT(RowCharWidth(&vted_buffer.rows[0], 1, 1), 7); /* its tab stops at 8 */

    CHECK_INT(BuffersSwitch(1), 1);
    CHECK_INT(vted_editor_config.tab_width, 4);
    CHECK_INT(RowCharWidth(&vted_buffer.rows[0], 1, 1), 3); /* this one's at 4 */

    /* a buffer opened later starts with the width the live one uses */
    {
        FILE *stream = fmemopen((char *)"piped\n", 6, "r");

        BuffersOpenStream(stream);
        fclose(stream);
    }
    CHECK_INT(vted_editor_config.tab_width, 4);

    CHECK_INT(BuffersSwitch(0), 1);
    CHECK_INT(vted_editor_config.tab_width, 8);
}

static void TestTheSettingsWidthReachesEveryBuffer(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    WriteFile(second_path, "two\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    /* the way startup applies the settings file: every buffer open
       at that point takes the width */
    BuffersSetTabWidth(4);

    CHECK_INT(BuffersCurrent(), 1);
    CHECK_INT(vted_editor_config.tab_width, 4);

    CHECK_INT(BuffersSwitch(0), 1);
    CHECK_INT(vted_editor_config.tab_width, 4);

    CHECK_INT(BuffersSwitch(1), 1);
    CHECK_INT(vted_editor_config.tab_width, 4);
}

static void TestCloseBounds(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    BuffersOpen(first_path);

    CHECK_INT(BuffersClose(-1), 0);
    CHECK_INT(BuffersClose(1), 0);
    CHECK_INT(BuffersClose(BuffersCurrent()), 1); /* in range it works */
}

static void TestCloseRemovesAWaitingBuffer(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    WriteFile(second_path, "two\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    /* the live buffer is untouched when a waiting one closes */
    CHECK_INT(BuffersClose(0), 1);

    CHECK_INT(BuffersCount(), 1);
    CHECK_INT(BuffersCurrent(), 0); /* its index slid down */
    CHECK_STR(vted_buffer.filename, second_path);
    CHECK_STR(vted_buffer.rows[0].chars, "two");

    /* a waiting buffer with unsaved changes takes its warning with it */
    BuffersOpen(first_path);
    EditorInsertChar('X');
    BuffersSwitch(0); /* the changed one waits now */
    CHECK_INT(BuffersDirtyCount(), 1);

    CHECK_INT(BuffersClose(1), 1);
    CHECK_INT(BuffersDirtyCount(), 0); /* the unsaved change went with it */
    CHECK_STR(vted_buffer.filename, second_path);
}

static void TestCloseSwitchesToTheNeighbor(void)
{
    Begin();
    WriteFile(first_path, "one\ntwo\nthree\n");
    WriteFile(second_path, "other\n");
    BuffersOpen(first_path);
    BuffersOpen(second_path);

    /* the live one closes: the neighbor comes back into view */
    CHECK_INT(BuffersClose(1), 1);

    CHECK_INT(BuffersCount(), 1);
    CHECK_INT(BuffersCurrent(), 0);
    CHECK_STR(vted_buffer.filename, first_path);
    CHECK_STR(vted_buffer.rows[2].chars, "three");
    CHECK_INT(vted_buffer.dirty, 0);

    /* the first buffer closed instead: the one after it takes its
       place and becomes the live one */
    BuffersOpen(second_path);
    CHECK_INT(BuffersClose(0), 1);
    CHECK_INT(BuffersCurrent(), 0);
    CHECK_STR(vted_buffer.filename, second_path);
}

static void TestCloseTheLastBufferStartsAnEmptyOne(void)
{
    Begin();
    WriteFile(first_path, "one\n");
    BuffersOpen(first_path);

    CHECK_INT(BuffersClose(0), 1);

    CHECK_INT(BuffersCount(), 1);
    CHECK_INT(BuffersCurrent(), 0);
    CHECK(vted_buffer.filename == NULL);
    CHECK_INT(vted_buffer.num_rows, 0);
    CHECK_INT(vted_buffer.dirty, 0);
    CHECK_INT(EditorUndo(), 0); /* nothing to undo in a fresh buffer */
}

static void TestCloseForgetsTheHistory(void)
{
    Begin();
    WriteFile(first_path, "ab\n");
    BuffersOpen(first_path);

    EditorInsertChar('X');
    CHECK_INT(vted_buffer.dirty, 1);

    /* the buffer with its unsaved change closes, and with it the
       history of the change */
    CHECK_INT(BuffersClose(0), 1);
    CHECK_INT(BuffersCount(), 1); /* the fresh empty buffer */

    /* the file comes back the way it is on disk, with no history */
    CHECK_INT(BuffersOpen(first_path), 0);
    CHECK_INT(BuffersCount(), 2);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(EditorUndo(), 0);
}

int main(void)
{
    /* the files every check reuses, created once */
    int fd = mkstemp(first_path);

    if (fd == -1)
    {
        perror(first_path);
        return 2;
    }
    close(fd);

    fd = mkstemp(second_path);
    if (fd == -1)
    {
        perror(second_path);
        return 2;
    }
    close(fd);

    TestStartEmpty();
    TestOpenKeepsTheFirstBufferWaiting();
    TestOpenLoadsTheText();
    TestAMissingFileOpensEmptyAndNamed();
    TestAFailedOpenKeepsTheCurrentBuffer();
    TestOpeningTheSameFileAgainSwitchesToIt();
    TestTheHistoryTravelsWithTheBuffer();
    TestTheCursorAndViewportWaitWithTheBuffer();
    TestAStreamOpensAsAnUnnamedBuffer();
    TestSwitchBounds();
    TestDirtyCountsAcrossBuffers();
    TestNamesAndDirtyFlags();
    TestAWaitingBufferReadsWhileAnotherIsEdited();
    TestAWordCompletesFromTheOtherBuffersToo();
    TestEachBufferKeepsItsOwnTabWidth();
    TestTheSettingsWidthReachesEveryBuffer();
    TestCloseBounds();
    TestCloseRemovesAWaitingBuffer();
    TestCloseSwitchesToTheNeighbor();
    TestCloseTheLastBufferStartsAnEmptyOne();
    TestCloseForgetsTheHistory();

    /* put the editor back the way the next test binary expects it */
    Begin();

    unlink(first_path);
    unlink(second_path);

    return TestSummary("buffers");
}

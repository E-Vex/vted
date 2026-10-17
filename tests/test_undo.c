#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_screen.h"
#include "src/vted_row.h"
#include "src/vted_undo.h"
/*===============================*/

/* each check starts from a fresh buffer and fresh histories */
static void Begin(const char *text)
{
    FreeBuffer();
    UndoReset();

    if (text != NULL && text[0] != '\0')
    {
        AppendRow(text, (int)strlen(text), VTED_EOL_LF);
    }

    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;
}

static void TestEmptyHistories(void)
{
    Begin("ab");

    CHECK_INT(EditorUndo(), 0);
    CHECK_INT(EditorRedo(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "ab"); /* untouched */
}

static void TestInsertBytes(void)
{
    Begin("aXb"); /* as if X had just been typed at column 1 */

    UndoEntry entry = {
        .kind = UndoKindInsertBytes,
        .row = 0,
        .cx = 1,
        .bytes = "X",
        .length = 1,
        .undo_y = 0,
        .undo_x = 1,
        .redo_y = 0,
        .redo_x = 2,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_x, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aXb");
    CHECK_INT(vted_editor_config.cursor_x, 2);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestRemoveBytes(void)
{
    Begin("ab"); /* as if X at column 1 had just been deleted */

    UndoEntry entry = {
        .kind = UndoKindRemoveBytes,
        .row = 0,
        .cx = 1,
        .bytes = "X",
        .length = 1,
        .undo_y = 0,
        .undo_x = 2,
        .redo_y = 0,
        .redo_x = 1,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aXb");
    CHECK_INT(vted_editor_config.cursor_x, 2);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestSplitRow(void)
{
    Begin("ab");
    AppendRow("cd", 2, VTED_EOL_CRLF); /* as if the row had been split */
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;

    UndoEntry entry = {
        .kind = UndoKindSplitRow,
        .row = 0,
        .cx = 2,
        .eol = VTED_EOL_CRLF,
        .undo_y = 0,
        .undo_x = 2,
        .redo_y = 1,
        .redo_x = 0,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
    CHECK_INT(vted_editor_config.cursor_y, 1);
}

static void TestMergeRows(void)
{
    Begin("abcd"); /* as if ab and cd had just been joined */
    vted_buffer.rows[0].eol = VTED_EOL_LF;

    UndoEntry entry = {
        .kind = UndoKindMergeRows,
        .row = 0,
        .cx = 2,
        .eol = VTED_EOL_LF,      /* the lower line's ending */
        .prev_eol = VTED_EOL_CRLF, /* the upper line's before */
        .undo_y = 1,
        .undo_x = 0,
        .redo_y = 0,
        .redo_x = 2,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF); /* restored */
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_LF);
    CHECK_INT(vted_editor_config.cursor_y, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestInsertRow(void)
{
    Begin("a");
    AppendRow("b", 1, VTED_EOL_LF); /* as if the row had appeared */

    UndoEntry entry = {
        .kind = UndoKindInsertRow,
        .row = 1,
        .bytes = "b",
        .length = 1,
        .eol = VTED_EOL_LF,
        .undo_y = 0,
        .undo_x = 0,
        .redo_y = 1,
        .redo_x = 0,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a");

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "b");
}

static void TestRemoveRow(void)
{
    Begin("a"); /* as if the second row had just vanished */

    UndoEntry entry = {
        .kind = UndoKindRemoveRow,
        .row = 1,
        .bytes = "b",
        .length = 1,
        .eol = VTED_EOL_CRLF,
        .undo_y = 1,
        .undo_x = 1,
        .redo_y = 0,
        .redo_x = 0,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "b");
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
}

static void TestReplaceRow(void)
{
    Begin(NULL);
    AppendRow("", 0, VTED_EOL_LF); /* the cleared line still exists */

    UndoEntry entry = {
        .kind = UndoKindReplaceRow,
        .row = 0,
        .bytes = "abc",
        .length = 3,
        .undo_y = 0,
        .undo_x = 2,
        .redo_y = 0,
        .redo_x = 0,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(vted_editor_config.cursor_x, 2);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.rows[0].size, 0);
}

static void TestNewChangeDropsRedo(void)
{
    Begin("aXb"); /* as if X had just been typed at column 1 */

    UndoEntry entry = {
        .kind = UndoKindInsertBytes,
        .row = 0,
        .cx = 1,
        .bytes = "X",
        .length = 1,
        .undo_y = 0,
        .undo_x = 1,
        .redo_y = 0,
        .redo_x = 2,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    /* a different change arrives: the redone X is now unreachable.
       The change is applied too, so the buffer stays truthful. */
    entry.cx = 0;
    entry.bytes = "Z";
    UndoPush(&entry);
    RowInsertChar(&vted_buffer.rows[0], 0, 'Z');

    CHECK_INT(EditorRedo(), 0);
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab"); /* the Z is gone */
}

static void TestResetDropsEverything(void)
{
    Begin("ab");

    UndoEntry entry = {
        .kind = UndoKindInsertBytes,
        .row = 0,
        .cx = 1,
        .bytes = "X",
        .length = 1,
    };
    UndoPush(&entry);
    UndoReset();

    CHECK_INT(EditorUndo(), 0);
    CHECK_INT(EditorRedo(), 0);
}

static void TestLimitDropsTheOldest(void)
{
    Begin(NULL);

    UndoEntry entry = {
        .kind = UndoKindInsertRow,
        .row = 0,
        .bytes = "x",
        .length = 1,
        .eol = VTED_EOL_LF,
    };

    int i;
    for (i = 0; i < 210; i++)
    {
        AppendRow("x", 1, VTED_EOL_LF); /* the change really happens */
        entry.row = i;
        UndoPush(&entry);
    }

    int undone = 0;
    while (EditorUndo() == 1)
    {
        undone++;
    }

    CHECK_INT(undone, 200);             /* the limit, no further */
    CHECK_INT(vted_buffer.num_rows, 10); /* the first ten rows stay */
}

int main(void)
{
    TestEmptyHistories();
    TestInsertBytes();
    TestRemoveBytes();
    TestSplitRow();
    TestMergeRows();
    TestInsertRow();
    TestRemoveRow();
    TestReplaceRow();
    TestNewChangeDropsRedo();
    TestResetDropsEverything();
    TestLimitDropsTheOldest();

    return TestSummary("undo");
}

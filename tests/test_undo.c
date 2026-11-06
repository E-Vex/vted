#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_screen.h"
#include "src/vted_row.h"
#include "src/vted_edit.h"
#include "src/vted_syntax.h"
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
        .bytes = "cd", /* the line the split made */
        .length = 2,
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

/* the operations record entries as they work: each check below
   drives the real editing calls and walks the history back and
   forth through them */
static void TestTypingRoundTrips(void)
{
    Begin("ab");
    vted_editor_config.cursor_x = 1;

    EditorInsertChar('X');
    CHECK_STR(vted_buffer.rows[0].chars, "aXb");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_x, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aXb");
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestTypingOnThePhantomRowRoundTrips(void)
{
    Begin(NULL);

    EditorInsertChar('h');
    EditorInsertChar('i');
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "hi");

    /* the row and the typing that filled it are one change: one
       step takes the line away whole */
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 0);

    CHECK_INT(EditorRedo(), 1); /* and brings it back whole */
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "hi");
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestEnterRoundTrips(void)
{
    Begin("ab");
    vted_editor_config.cursor_x = 1; /* between the two characters */

    EditorInsertNewline();
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_STR(vted_buffer.rows[1].chars, "b");

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_x, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_STR(vted_buffer.rows[1].chars, "b");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestEnterKeepsIndentOnRedo(void)
{
    Begin("    code");
    vted_editor_config.cursor_x = 8; /* at the end of the line */

    EditorInsertNewline();
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "    "); /* the indent moved down */

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "    code");

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "    code");
    CHECK_STR(vted_buffer.rows[1].chars, "    "); /* survives the round trip */
    CHECK_INT(vted_editor_config.cursor_x, 4);
}

static void TestEnterAtColumnZeroRoundTrips(void)
{
    Begin("ab");

    EditorInsertNewline(); /* an empty line appears above */
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "ab");

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "");
    CHECK_STR(vted_buffer.rows[1].chars, "ab");
}

static void TestBackspaceMergeRoundTrips(void)
{
    Begin("ab");
    AppendRow("cd", 2, VTED_EOL_LF);
    vted_editor_config.cursor_y = 1;

    EditorDelChar(); /* at column zero: merge */
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
    CHECK_INT(vted_editor_config.cursor_y, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestMergeKeepsEndingsThroughUndo(void)
{
    Begin("ab");
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;
    AppendRow("cd", 2, VTED_EOL_LF);
    vted_editor_config.cursor_y = 1;

    EditorDelChar();
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF); /* the lower one won */

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF); /* restored */
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_LF);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);
}

static void TestDeleteLineRoundTrips(void)
{
    Begin("ab");
    AppendRow("cd", 2, VTED_EOL_CRLF);
    vted_editor_config.cursor_y = 1;

    EditorDeleteLine();
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_editor_config.cursor_y, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
}

static void TestDeleteOnlyLineRoundTrips(void)
{
    Begin("abc");
    vted_editor_config.cursor_x = 3;

    EditorDeleteLine(); /* the only line is emptied, not removed */
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(vted_buffer.rows[0].size, 0);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(vted_editor_config.cursor_x, 3);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.rows[0].size, 0);
}

static void TestDuplicateRoundTrips(void)
{
    Begin("ab");
    vted_editor_config.cursor_x = 2;

    EditorDuplicateLine();
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_y, 1);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_INT(vted_editor_config.cursor_y, 1);
}

static void TestDuplicateBlockRoundTrips(void)
{
    /* the cursor on an opener: the whole block copies, and one step
       takes it back and brings it again */
    Begin(NULL);

    AppendRow("int main(void)", 14, VTED_EOL_LF);
    AppendRow("{", 1, VTED_EOL_LF);
    AppendRow("}", 1, VTED_EOL_LF);
    vted_editor_config.cursor_y = 1;
    vted_editor_config.cursor_x = 0; /* on the { */

    EditorDuplicateLine();
    CHECK_INT(vted_buffer.num_rows, 5);
    CHECK_STR(vted_buffer.rows[3].chars, "{");
    CHECK_STR(vted_buffer.rows[4].chars, "}");
    CHECK_INT(vted_editor_config.cursor_y, 3);

    /* one Ctrl-Z takes the whole block of copies away */
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[2].chars, "}");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    /* one Ctrl-Y brings it back, byte for byte */
    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 5);
    CHECK_STR(vted_buffer.rows[3].chars, "{");
    CHECK_STR(vted_buffer.rows[4].chars, "}");
    CHECK_INT(vted_editor_config.cursor_y, 3);
}

static void TestMultibyteDeleteRoundTrips(void)
{
    Begin("a\xC3\xA9" "b"); /* a, a two-byte e-acute, b */
    vted_editor_config.cursor_x = 3; /* just past the e-acute */

    EditorDelChar();
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a\xC3\xA9" "b");
    CHECK_INT(vted_editor_config.cursor_x, 3);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestTypingBurstUndoesAsOne(void)
{
    Begin("ab");
    vted_editor_config.cursor_x = 1;

    EditorInsertChar('X');
    EditorInsertChar('Y');
    EditorInsertChar('Z');
    CHECK_STR(vted_buffer.rows[0].chars, "aXYZb");

    CHECK_INT(EditorUndo(), 1); /* the whole burst goes */
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_x, 1); /* where it began */

    CHECK_INT(EditorRedo(), 1); /* and comes back whole */
    CHECK_STR(vted_buffer.rows[0].chars, "aXYZb");
    CHECK_INT(vted_editor_config.cursor_x, 4); /* where it ended */
}

static void TestBackspaceBurstUndoesAsOne(void)
{
    Begin("hello");
    vted_editor_config.cursor_x = 5;

    EditorDelChar();
    EditorDelChar();
    CHECK_STR(vted_buffer.rows[0].chars, "hel");

    CHECK_INT(EditorUndo(), 1); /* both bites come back */
    CHECK_STR(vted_buffer.rows[0].chars, "hello");
    CHECK_INT(vted_editor_config.cursor_x, 5);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "hel");
    CHECK_INT(vted_editor_config.cursor_x, 3);
}

static void TestBurstBreaksWhenTheCursorMoves(void)
{
    Begin("ab");
    vted_editor_config.cursor_x = 1;

    EditorInsertChar('X');
    vted_editor_config.cursor_x = 3; /* moved to the end in between */
    EditorInsertChar('Y');
    CHECK_STR(vted_buffer.rows[0].chars, "aXbY");

    /* a gap between the keystrokes: two separate changes */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aXb");
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
}

static void TestBurstBreaksAcrossRows(void)
{
    Begin("ab");
    AppendRow("cd", 2, VTED_EOL_LF);

    vted_editor_config.cursor_y = 1;
    EditorInsertChar('X'); /* row 1 becomes Xcd, the column is 1 now */
    vted_editor_config.cursor_y = 0; /* typed on another row in between */
    EditorInsertChar('Y'); /* row 0 becomes aYb, the column is 2 now */
    vted_editor_config.cursor_y = 1;
    EditorInsertChar('Z');
    CHECK_STR(vted_buffer.rows[0].chars, "aYb");
    CHECK_STR(vted_buffer.rows[1].chars, "XcZd");

    CHECK_INT(EditorUndo(), 1); /* the Z goes alone */
    CHECK_STR(vted_buffer.rows[1].chars, "Xcd");
    CHECK_INT(EditorUndo(), 1); /* then the Y */
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(EditorUndo(), 1); /* then the X */
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
}

static void TestTypingAfterEnterAtZeroFoldsIntoTheLine(void)
{
    Begin("ab");

    EditorInsertNewline(); /* an empty line appears above */
    CHECK_STR(vted_buffer.rows[0].chars, "");
    CHECK_STR(vted_buffer.rows[1].chars, "ab");

    /* Enter leaves the cursor on the line below; step up onto the
       new empty one and fill it */
    vted_editor_config.cursor_y = 0;
    EditorInsertChar('h');
    EditorInsertChar('i');
    CHECK_STR(vted_buffer.rows[0].chars, "hi");

    /* the line and the typing that filled it are one change */
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "hi");
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestTypingAfterDuplicateStaysSeparate(void)
{
    Begin("ab");
    vted_editor_config.cursor_x = 2;

    EditorDuplicateLine();
    EditorInsertChar('!'); /* typed at the end of the copy */
    CHECK_STR(vted_buffer.rows[1].chars, "ab!");

    /* the copy was a change of its own: the ! must not fold into it */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[1].chars, "ab");
    CHECK_INT(vted_buffer.num_rows, 2);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
}

static void TestFoldingDropsRedo(void)
{
    Begin(NULL);

    EditorInsertChar('h');
    EditorInsertChar('i'); /* one folded entry */
    EditorInsertNewline(); /* the split at the end: an entry of its own */
    CHECK_INT(vted_buffer.num_rows, 2);

    CHECK_INT(EditorUndo(), 1); /* the split is undone: hi is whole */
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "hi");

    /* typing more folds into the hi entry, and the undone split
       must become unreachable with it */
    EditorInsertChar('!');
    CHECK_STR(vted_buffer.rows[0].chars, "hi!");

    CHECK_INT(EditorRedo(), 0);
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 0); /* the line went whole */
}

static void TestInsertTextOneLine(void)
{
    Begin("abXYcd"); /* as if XY had been pasted at column 2 */

    UndoEntry entry = {
        .kind = UndoKindInsertText,
        .row = 0,
        .cx = 2,
        .bytes = "XY",
        .length = 2,
        .undo_y = 0,
        .undo_x = 2,
        .redo_y = 0,
        .redo_x = 4,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_editor_config.cursor_x, 2);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abXYcd");
    CHECK_INT(vted_editor_config.cursor_x, 4);
}

static void TestInsertTextSeveralLines(void)
{
    Begin("abX"); /* as if "X\nYY\nZ" had been pasted at column 2 of "abcd" */
    AppendRow("YY", 2, VTED_EOL_LF);
    AppendRow("Zcd", 3, VTED_EOL_LF);

    UndoEntry entry = {
        .kind = UndoKindInsertText,
        .row = 0,
        .cx = 2,
        .bytes = "X\nYY\nZ",
        .length = 6,
        .eol = VTED_EOL_LF,
        .undo_y = 0,
        .undo_x = 2,
        .redo_y = 2,
        .redo_x = 1,
    };
    UndoPush(&entry);

    /* one undo takes back all three lines */
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2);
    CHECK_INT(EditorUndo(), 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "abX");
    CHECK_STR(vted_buffer.rows[1].chars, "YY");
    CHECK_STR(vted_buffer.rows[2].chars, "Zcd");
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestInsertTextKeepsEndings(void)
{
    Begin("abX"); /* a CRLF line, pasted into */
    AppendRow("Zcd", 3, VTED_EOL_CRLF);
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;

    UndoEntry entry = {
        .kind = UndoKindInsertText,
        .row = 0,
        .cx = 2,
        .bytes = "X\nZ",
        .length = 3,
        .eol = VTED_EOL_CRLF,
        .undo_y = 0,
        .undo_x = 2,
        .redo_y = 1,
        .redo_x = 1,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
}

static void TestInsertTextOnTheEmptyRowPastTheEnd(void)
{
    Begin("a"); /* "p\nq" pasted on the row past the end made two rows */
    AppendRow("p", 1, VTED_EOL_LF);
    AppendRow("q", 1, VTED_EOL_LF);

    UndoEntry entry = {
        .kind = UndoKindInsertText,
        .row = 1,
        .cx = 0,
        .bytes = "p\nq",
        .length = 3,
        .eol = VTED_EOL_LF,
        .made_row = 1,
        .undo_y = 1,
        .undo_x = 0,
        .redo_y = 2,
        .redo_x = 1,
    };
    UndoPush(&entry);

    /* the row the paste made is gone too, the cursor back past the end */
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[1].chars, "p");
    CHECK_STR(vted_buffer.rows[2].chars, "q");
    CHECK_INT(vted_editor_config.cursor_y, 2);
}

static void TestTypingAfterAPasteStaysSeparate(void)
{
    Begin("abXYZcd"); /* XY pasted at column 2, then Z typed right after it */

    UndoEntry pasted = {
        .kind = UndoKindInsertText,
        .row = 0,
        .cx = 2,
        .bytes = "XY",
        .length = 2,
        .undo_y = 0,
        .undo_x = 2,
        .redo_y = 0,
        .redo_x = 4,
    };
    UndoPush(&pasted);

    UndoEntry typed = {
        .kind = UndoKindInsertBytes,
        .row = 0,
        .cx = 4,
        .bytes = "Z",
        .length = 1,
        .undo_y = 0,
        .undo_x = 4,
        .redo_y = 0,
        .redo_x = 5,
    };
    UndoPushTyping(&typed);

    /* the typed letter goes first, the paste is still whole */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abXYcd");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(EditorUndo(), 0);
}

static void TestPasteRoundTrips(void)
{
    Begin("abcd");
    vted_editor_config.cursor_x = 2;

    EditorInsertText("X\nYY\nZ", 6);
    CHECK_INT(vted_buffer.num_rows, 3);

    /* the three lines come back out in one step */
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2);
    CHECK_INT(EditorUndo(), 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "abX");
    CHECK_STR(vted_buffer.rows[1].chars, "YY");
    CHECK_STR(vted_buffer.rows[2].chars, "Zcd");
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestPasteOnThePhantomRowRoundTrips(void)
{
    Begin("a");
    vted_editor_config.cursor_y = 1; /* the empty row past the end */

    EditorInsertText("p\nq", 3);
    CHECK_INT(vted_buffer.num_rows, 3);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[1].chars, "p");
    CHECK_STR(vted_buffer.rows[2].chars, "q");
}

static void TestPasteIntoAnEmptyBufferRoundTrips(void)
{
    Begin(NULL); /* a buffer with no rows at all */

    EditorInsertText("one\ntwo", 7);
    CHECK_INT(vted_buffer.num_rows, 2);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "two");
}

static void TestBigPasteIsOneStep(void)
{
    /* far more lines than the history holds entries: it must still
       come back out in one step, none of it left behind */
    char *text = malloc(2 * 500 + 1);
    int i;

    for (i = 0; i < 500; i++)
    {
        text[2 * i] = 'l';
        text[2 * i + 1] = '\n';
    }
    text[1000] = '\0';

    Begin("ab");
    vted_editor_config.cursor_x = 1;

    EditorInsertText(text, 1000);
    CHECK_INT(vted_buffer.num_rows, 501);
    CHECK_STR(vted_buffer.rows[0].chars, "al");
    CHECK_STR(vted_buffer.rows[500].chars, "b");

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(EditorUndo(), 0);

    free(text);
}

static void TestTypingAfterAPasteIsItsOwnStep(void)
{
    Begin("ab");
    vted_editor_config.cursor_x = 1;

    EditorInsertText("XY", 2);
    EditorInsertChar('Z'); /* right where the paste ended */
    CHECK_STR(vted_buffer.rows[0].chars, "aXYZb");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aXYb"); /* the paste is whole */

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(EditorUndo(), 0);
}

static void TestPasteKeepsEndingsThroughUndo(void)
{
    Begin("abcd");
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;
    vted_editor_config.cursor_x = 2;

    EditorInsertText("X\nZ", 3);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF); /* a CRLF file stays CRLF */

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
}

static void TestPasteEndingTheFileRoundTrips(void)
{
    Begin("a");
    vted_editor_config.cursor_y = 1; /* the empty row past the end */

    EditorInsertText("p\nq\n", 5);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_INT(vted_editor_config.cursor_y, 3);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[1].chars, "p");
    CHECK_STR(vted_buffer.rows[2].chars, "q");
    CHECK_INT(vted_editor_config.cursor_y, 3);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestPasteOfOneBreakEndingTheFileRoundTrips(void)
{
    /* the text left over after the break is empty: the entry holds no
       bytes at all, and a replay must cope with that */
    Begin("a");
    vted_editor_config.cursor_y = 1;

    EditorInsertText("\n", 1);
    CHECK_INT(vted_buffer.num_rows, 2);

    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(vted_editor_config.cursor_y, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "");
    CHECK_INT(vted_editor_config.cursor_y, 2);
}

static void TestWrapSelectionRoundTrips(void)
{
    /* as if "cd" had just been wrapped in a pair: one undo takes
       both halves back out, one redo lays them in again */
    Begin("ab(cd)ef");

    UndoEntry entry = {
        .kind = UndoKindWrapSelection,
        .row = 0,
        .cx = 2,
        .bytes = "()",
        .length = 2,
        .undo_y = 0,
        .undo_x = 4,
        .redo_y = 0,
        .redo_x = 5,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcdef");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab(cd)ef");
    CHECK_INT(vted_editor_config.cursor_x, 5);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestWrapSelectionAcrossRowsRoundTrips(void)
{
    /* a span over a line break: the opener and the closer went into
       different rows, and both round trips must find their own */
    Begin("ab(cd");
    AppendRow("ef)gh", 5, VTED_EOL_LF);

    UndoEntry entry = {
        .kind = UndoKindWrapSelection,
        .row = 0,
        .cx = 2,
        .bytes = "()",
        .length = 2,
        .undo_y = 1,
        .undo_x = 2,
        .redo_y = 1,
        .redo_x = 2,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_STR(vted_buffer.rows[1].chars, "efgh");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 2);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab(cd");
    CHECK_STR(vted_buffer.rows[1].chars, "ef)gh");
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

/* pushes an entry for one typed byte, the way the tests above do;
   the caller sets the buffer up around the byte */
static void PushTypedByte(char byte, int at)
{
    UndoEntry entry = {
        .kind = UndoKindInsertBytes,
        .row = 0,
        .cx = at,
        .bytes = &byte,
        .length = 1,
        .undo_y = 0,
        .undo_x = at,
        .redo_y = 0,
        .redo_x = at + 1,
    };

    UndoPush(&entry);
}

static void TestDetachLeavesAnEmptyModule(void)
{
    Begin("aXb"); /* as if X had just been typed at column 1 */
    PushTypedByte('X', 1);

    void *token = UndoDetach();

    /* the module holds nothing: the text is not touched either way */
    CHECK_INT(EditorUndo(), 0);
    CHECK_INT(EditorRedo(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "aXb");

    UndoAttach(token);

    /* put back, the change is reachable again */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
}

static void TestAnEmptyHistoryDetachesToo(void)
{
    Begin("ab");

    void *token = UndoDetach();

    UndoAttach(token);

    CHECK_INT(EditorUndo(), 0);
    CHECK_INT(EditorRedo(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
}

static void TestAttachReplacesTheCurrentHistory(void)
{
    /* one buffer's history goes out */
    Begin("aXb");
    PushTypedByte('X', 1);
    void *token = UndoDetach();

    /* another buffer is edited in the meantime */
    Begin("cYd");
    PushTypedByte('Y', 1);
    CHECK_INT(EditorUndo(), 1);
    CHECK_INT(EditorRedo(), 1);

    /* the first buffer and its history come back together */
    Begin("aXb");
    UndoAttach(token);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    /* the second buffer's change is unreachable now: only the first
       buffer's own redo is left */
    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aXb");
    CHECK_INT(EditorRedo(), 0);
}

static void TestANullHistoryIsEmpty(void)
{
    Begin("ab");
    PushTypedByte('X', 0); /* as if X had been typed in front */

    UndoAttach(NULL);

    CHECK_INT(EditorUndo(), 0);
    CHECK_INT(EditorRedo(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
}

static void TestDiscardLeavesTheModuleAlone(void)
{
    Begin("aXb");
    PushTypedByte('X', 1);

    void *token = UndoDetach();

    /* another buffer's change is already recorded when the first
       buffer is given up on */
    Begin("cYd");
    PushTypedByte('Y', 1);

    UndoDiscard(token);
    UndoDiscard(NULL); /* nothing is fine to discard too */

    /* the module still holds the live buffer's history */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "cd");
}

static void TestCommentRowsUndo(void)
{
    /* as if Ctrl-/ had commented the two rows of a C file, the row
       of the cursor and the one the selection reached */
    Begin("// one"); /* the rows as the toggle left them */
    AppendRow("// two", 6, VTED_EOL_LF);
    SyntaxSelectForFile("main.c");

    UndoEntry entry = {
        .kind = UndoKindCommentRows,
        .row = 0,
        .cx = 1,
        .bytes = "one\ntwo", /* the texts as they stood */
        .length = 7,
        .undo_y = 0,
        .undo_x = 0,
        .redo_y = 0,
        .redo_x = 3,
    };
    UndoPush(&entry);

    /* one undo takes the token off every row, whatever each of them
       carried */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 0);

    /* one redo puts it back the way the toggle first did */
    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "// one");
    CHECK_STR(vted_buffer.rows[1].chars, "// two");
    CHECK_INT(vted_editor_config.cursor_x, 3);
}

static void TestCommentRowsUndoBringsBackExactTexts(void)
{
    /* the old texts return exactly, double spaces, blank rows and
       other tokens included: the undo is not asked to retrace them.
       The press was an uncomment — the first row carried the token */
    Begin(" wide"); /* the rows as the toggle left them */
    AppendRow("", 0, VTED_EOL_LF); /* a blank row stays blank */
    AppendRow("#  hash", 7, VTED_EOL_LF); /* no // to take off */
    SyntaxSelectForFile("main.c");

    UndoEntry entry = {
        .kind = UndoKindCommentRows,
        .row = 0,
        .cx = 2,
        .bytes = "//  wide\n\n#  hash",
        .length = 17,
        .undo_y = 0,
        .undo_x = 0,
        .redo_y = 0,
        .redo_x = 1,
    };
    UndoPush(&entry);

    /* the texts stand exactly as they did, the double space behind
       the token and the blank row included */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "//  wide");
    CHECK_STR(vted_buffer.rows[1].chars, "");
    CHECK_STR(vted_buffer.rows[2].chars, "#  hash");

    /* the redo takes the token off again, the way the press did */
    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, " wide");
    CHECK_STR(vted_buffer.rows[1].chars, "");
    CHECK_STR(vted_buffer.rows[2].chars, "#  hash");
}

static void TestCommentRowsUndoWithoutOldTexts(void)
{
    /* an entry that holds no texts at all, as a defensive path for a
       span whose rows were all empty: the row empties out on undo,
       and a redo finds nothing to comment and leaves it be */
    Begin("// lone");
    SyntaxSelectForFile("main.c");

    UndoEntry entry = {
        .kind = UndoKindCommentRows,
        .row = 0,
        .cx = 0,
        .bytes = NULL,
        .length = 0,
        .undo_y = 0,
        .undo_x = 0,
        .redo_y = 0,
        .redo_x = 0,
    };
    UndoPush(&entry);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "");

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "");
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
    TestTypingRoundTrips();
    TestTypingOnThePhantomRowRoundTrips();
    TestEnterRoundTrips();
    TestEnterKeepsIndentOnRedo();
    TestEnterAtColumnZeroRoundTrips();
    TestBackspaceMergeRoundTrips();
    TestMergeKeepsEndingsThroughUndo();
    TestDeleteLineRoundTrips();
    TestDeleteOnlyLineRoundTrips();
    TestDuplicateRoundTrips();
    TestDuplicateBlockRoundTrips();
    TestMultibyteDeleteRoundTrips();
    TestTypingBurstUndoesAsOne();
    TestBackspaceBurstUndoesAsOne();
    TestBurstBreaksWhenTheCursorMoves();
    TestBurstBreaksAcrossRows();
    TestTypingAfterEnterAtZeroFoldsIntoTheLine();
    TestTypingAfterDuplicateStaysSeparate();
    TestFoldingDropsRedo();
    TestInsertTextOneLine();
    TestInsertTextSeveralLines();
    TestInsertTextKeepsEndings();
    TestInsertTextOnTheEmptyRowPastTheEnd();
    TestTypingAfterAPasteStaysSeparate();
    TestPasteRoundTrips();
    TestPasteOnThePhantomRowRoundTrips();
    TestPasteIntoAnEmptyBufferRoundTrips();
    TestBigPasteIsOneStep();
    TestTypingAfterAPasteIsItsOwnStep();
    TestPasteKeepsEndingsThroughUndo();
    TestPasteEndingTheFileRoundTrips();
    TestPasteOfOneBreakEndingTheFileRoundTrips();
    TestWrapSelectionRoundTrips();
    TestWrapSelectionAcrossRowsRoundTrips();
    TestDetachLeavesAnEmptyModule();
    TestAnEmptyHistoryDetachesToo();
    TestAttachReplacesTheCurrentHistory();
    TestANullHistoryIsEmpty();
    TestDiscardLeavesTheModuleAlone();
    TestCommentRowsUndo();
    TestCommentRowsUndoBringsBackExactTexts();
    TestCommentRowsUndoWithoutOldTexts();

    return TestSummary("undo");
}

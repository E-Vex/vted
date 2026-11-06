#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_screen.h"
#include "src/vted_row.h"
#include "src/vted_edit.h"
#include "src/vted_regex.h"
#include "src/vted_syntax.h"
#include "src/vted_undo.h"
/*===============================*/

/* each check starts from a fresh buffer with the given rows */
static void Begin(const char *text)
{
    FreeBuffer();

    /* no rows at all when there is no text */
    if (text != NULL && text[0] != '\0')
    {
        AppendRow(text, (int)strlen(text), VTED_EOL_LF);
    }

    vted_editor_config.cursor_x = 0;
    vted_editor_config.cursor_y = 0;
}

static void CursorAt(int y, int x)
{
    vted_editor_config.cursor_y = y;
    vted_editor_config.cursor_x = x;
}

static void TestInsertChar(void)
{
    Begin(NULL); /* an empty buffer has no rows yet */
    EditorInsertChar('a');
    EditorInsertChar('b');

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_x, 2);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestInsertCharOnPhantomRow(void)
{
    /* typing below the last line starts a new line that ends the
       way the file does */
    Begin("a");
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;
    CursorAt(1, 0);

    EditorInsertChar('x');

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "x");
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
}

static void TestInsertPair(void)
{
    /* the opener lands with its closer behind it, the cursor between
       the two */
    Begin("ab");
    CursorAt(0, 1);

    EditorInsertPair('(', ')');

    CHECK_STR(vted_buffer.rows[0].chars, "a()b");
    CHECK_INT(vted_editor_config.cursor_x, 2);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestInsertPairOnPhantomRow(void)
{
    /* the pair starts a new line past the end the way a single
       character does, ending the way the file does */
    Begin("a");
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;
    CursorAt(1, 0);

    EditorInsertPair('{', '}');

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "{}");
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestInsertPairUndoRedo(void)
{
    /* the pair is one undo step: one Ctrl-Z takes both halves back,
       one Ctrl-Y lays them in again */
    Begin("ab");
    CursorAt(0, 1);

    EditorInsertPair('[', ']');

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 1);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a[]b");
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestInsertPairFoldsIntoTheTypingAfterIt(void)
{
    /* a keystroke landing right past the pair folds into it, the way
       typing folds into typing; one inside it does not, the pair being
       its own step there */
    Begin("ab");
    CursorAt(0, 1);

    EditorInsertPair('(', ')');
    vted_editor_config.cursor_x = 3; /* past the closer */
    EditorInsertChar('x');

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    /* inside the pair the keystroke is a step of its own */
    Begin("ab");
    CursorAt(0, 1);

    EditorInsertPair('(', ')');
    EditorInsertChar('x'); /* between the two */

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a()b");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
}

static void TestDeletePair(void)
{
    /* backspace inside an empty pair takes both halves out as one
       step */
    Begin("f()x");
    CursorAt(0, 2);

    CHECK_INT(EditorDeletePair(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "fx");
    CHECK_INT(vted_editor_config.cursor_x, 1);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "f()x");
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestDeletePairOnlyInsideAnEmptyOne(void)
{
    /* a pair with something inside is not the cursor's to take whole,
       nor a mismatched half, nor an opener at the row's end */
    Begin("f(x)y");
    CursorAt(0, 2);
    CHECK_INT(EditorDeletePair(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "f(x)y");

    Begin("(]");
    CursorAt(0, 1);
    CHECK_INT(EditorDeletePair(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "(]");

    Begin("x(");
    CursorAt(0, 2);
    CHECK_INT(EditorDeletePair(), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "x(");

    /* nothing left of the cursor, and nothing on the phantom row */
    Begin("()");
    CursorAt(0, 0);
    CHECK_INT(EditorDeletePair(), 0);

    Begin("()");
    CursorAt(1, 0);
    CHECK_INT(EditorDeletePair(), 0);
}

static void TestWrapSelection(void)
{
    /* an opener typed over a selection wraps it: the halves go in
       around what is selected, which stays selected the way it was,
       the cursor keeping its own end of it */
    Begin("abcdef");
    CursorAt(0, 4);
    EditSetAnchor(0, 2);

    CHECK_INT(EditorWrapSelection('(', ')'), 1);

    CHECK_STR(vted_buffer.rows[0].chars, "ab(cd)ef");
    CHECK_INT(vted_editor_config.cursor_x, 5);
    CHECK_INT(vted_buffer.dirty, 1);

    int top, left, bottom, right;

    EditSelectionRange(&top, &left, &bottom, &right);
    CHECK_INT(top, 0);
    CHECK_INT(left, 3);
    CHECK_INT(bottom, 0);
    CHECK_INT(right, 5);

    EditClearSelection();
}

static void TestWrapSelectionKeepsTheCursorEnd(void)
{
    /* a selection made the other way keeps its ends too: the cursor
       stays on the end it was on, before the closer, the anchor
       behind the opener */
    Begin("abcdef");
    CursorAt(0, 2);
    EditSetAnchor(0, 4);

    CHECK_INT(EditorWrapSelection('[', ']'), 1);

    CHECK_STR(vted_buffer.rows[0].chars, "ab[cd]ef");
    CHECK_INT(vted_editor_config.cursor_x, 3);

    int top, left, bottom, right;

    EditSelectionRange(&top, &left, &bottom, &right);
    CHECK_INT(left, 3);
    CHECK_INT(right, 5);

    EditClearSelection();
}

static void TestWrapSelectionAcrossRows(void)
{
    /* a span over a line break: the opener goes in front of the top
       row's tail and the closer behind the bottom row's head */
    Begin("abcd");
    AppendRow("efgh", 4, VTED_EOL_LF);
    CursorAt(1, 2);
    EditSetAnchor(0, 2);

    CHECK_INT(EditorWrapSelection('{', '}'), 1);

    CHECK_STR(vted_buffer.rows[0].chars, "ab{cd");
    CHECK_STR(vted_buffer.rows[1].chars, "ef}gh");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 2);

    EditClearSelection();
}

static void TestWrapSelectionAtTheRowsEnd(void)
{
    /* a selection running to the end of a row: the closer lands
       behind its last byte, and the cursor keeps standing at the
       row's end */
    Begin("abcd");
    CursorAt(0, 4);
    EditSetAnchor(0, 2);

    CHECK_INT(EditorWrapSelection('(', ')'), 1);

    CHECK_STR(vted_buffer.rows[0].chars, "ab(cd)");
    CHECK_INT(vted_editor_config.cursor_x, 5);

    EditClearSelection();
}

static void TestWrapSelectionUndoRedo(void)
{
    /* the wrap is one undo step: both halves go as one, and one redo
       lays them both back in */
    Begin("abcdef");
    CursorAt(0, 4);
    EditSetAnchor(0, 2);

    CHECK_INT(EditorWrapSelection('(', ')'), 1);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcdef");
    CHECK_INT(vted_editor_config.cursor_x, 4);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab(cd)ef");
    CHECK_INT(vted_editor_config.cursor_x, 5);

    EditClearSelection();
}

static void TestWrapSelectionInAQuote(void)
{
    /* a quote wraps with itself as both halves — the same byte on
       each end — and one undo takes the pair back out, the way a
       bracket's does */
    Begin("abcdef");
    CursorAt(0, 4);
    EditSetAnchor(0, 2);

    CHECK_INT(EditorWrapSelection('"', '"'), 1);

    CHECK_STR(vted_buffer.rows[0].chars, "ab\"cd\"ef");
    CHECK_INT(vted_editor_config.cursor_x, 5);

    int top, left, bottom, right;

    EditSelectionRange(&top, &left, &bottom, &right);
    CHECK_INT(left, 3);
    CHECK_INT(right, 5);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcdef");
    CHECK_INT(vted_editor_config.cursor_x, 4);

    EditClearSelection();
}

static void TestWrapSelectionOnlyWithSomethingSelected(void)
{
    /* without a selection, or with an empty one, there is nothing to
       wrap: the caller lays the pair in at the cursor instead */
    Begin("ab");
    CursorAt(0, 1);

    CHECK_INT(EditorWrapSelection('(', ')'), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    EditSetAnchor(0, 1);
    CHECK_INT(EditorWrapSelection('(', ')'), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    /* a selection that is no longer active wraps nothing either */
    EditSetAnchor(0, 0);
    EditClearSelection();
    CHECK_INT(EditorWrapSelection('(', ')'), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");

    /* a buffer with no rows at all has no spot for the halves */
    Begin(NULL);
    CHECK_INT(EditorWrapSelection('(', ')'), 0);
    CHECK_INT(vted_buffer.num_rows, 0);

    EditClearSelection();
}

static void TestWrapSelectionRidesLaterKeys(void)
{
    /* the wrapped text answers the next key the way the bare one
       did: a second opener wraps it again, its closer going in behind
       the text and in front of the first closer — which is no part
       of the selection — and a letter typed over it replaces it
       between the halves */
    Begin("abcdef");
    CursorAt(0, 4);
    EditSetAnchor(0, 2);

    CHECK_INT(EditorWrapSelection('(', ')'), 1);

    /* the second wrap goes around the same span, inside the halves
       the first laid in */
    CHECK_INT(EditorWrapSelection('[', ']'), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "ab([cd])ef");

    /* a typed letter takes the wrapped text out between the halves */
    EditDeleteSelection();
    EditorInsertChar('x');
    CHECK_STR(vted_buffer.rows[0].chars, "ab([x])ef");
}

static void TestInsertNewlineSplits(void)
{
    Begin("abcd");
    CursorAt(0, 2);

    EditorInsertNewline();

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestInsertNewlineKeepsIndent(void)
{
    Begin("    ab");
    CursorAt(0, 6);

    EditorInsertNewline();

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "    ");
    CHECK_INT(vted_editor_config.cursor_x, 4);
}

static void TestInsertNewlineAtColumnZero(void)
{
    Begin("ab");
    CursorAt(0, 0);

    EditorInsertNewline();

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "");
    CHECK_STR(vted_buffer.rows[1].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_y, 1);
}

static void TestInsertNewlineKeepsEndings(void)
{
    Begin("abcd");
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;
    CursorAt(0, 2);

    EditorInsertNewline();

    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
}

static void TestDelChar(void)
{
    Begin("abc");
    CursorAt(0, 2);

    EditorDelChar();

    CHECK_STR(vted_buffer.rows[0].chars, "ac");
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestDelCharMultibyte(void)
{
    Begin("a\xC3\xA9"); /* a followed by a two-byte e-acute */
    CursorAt(0, 3);

    EditorDelChar();

    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestDelCharMerges(void)
{
    Begin("ab");
    AppendRow("cd", 2, VTED_EOL_LF);
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;
    CursorAt(1, 0);

    EditorDelChar();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2);
    /* the merged line ends the way the lower one did */
    CHECK_INT(vted_buffer.rows[0].eol, VTED_EOL_LF);
}

static void TestDelCharAtStartOfFile(void)
{
    Begin("ab");
    CursorAt(0, 0);

    EditorDelChar();

    CHECK_STR(vted_buffer.rows[0].chars, "ab"); /* nothing deleted */
    CHECK_INT(vted_buffer.dirty, 0);
}

static void TestDeleteWordLeft(void)
{
    /* the word before the cursor goes, the blanks left of it stay */
    Begin("foo bar baz");
    CursorAt(0, 11);

    EditorDeleteWordLeft();
    CHECK_STR(vted_buffer.rows[0].chars, "foo bar ");
    CHECK_INT(vted_editor_config.cursor_x, 8);
    CHECK_INT(vted_buffer.dirty, 1);

    /* one undo step puts the word and the cursor back */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo bar baz");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 11);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo bar ");
    CHECK_INT(vted_editor_config.cursor_x, 8);
}

static void TestDeleteWordLeftFromInsideAWord(void)
{
    /* mid-word, the deletion goes back to the word's own start */
    Begin("foo bar");
    CursorAt(0, 6); /* on the r */

    EditorDeleteWordLeft();
    CHECK_STR(vted_buffer.rows[0].chars, "foo r");
    CHECK_INT(vted_editor_config.cursor_x, 4);
}

static void TestDeleteWordLeftOverBlanks(void)
{
    /* the word right before the cursor goes, even when blanks sit
       further left; those stay */
    Begin("foo    bar");
    CursorAt(0, 10);

    EditorDeleteWordLeft();
    CHECK_STR(vted_buffer.rows[0].chars, "foo    ");
    CHECK_INT(vted_editor_config.cursor_x, 7);

    /* sitting right after the run of blanks, the run goes with the
       word before it, the way the shell erases a word */
    Begin("foo    bar");
    CursorAt(0, 7);

    EditorDeleteWordLeft();
    CHECK_STR(vted_buffer.rows[0].chars, "bar");
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestDeleteWordLeftAtColumnZero(void)
{
    /* the start of a line merges it into the line above, the way
       Backspace does */
    Begin("ab");
    AppendRow("cd", 2, VTED_EOL_LF);
    CursorAt(1, 0);

    EditorDeleteWordLeft();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestDeleteWordLeftAtStartOfFile(void)
{
    Begin("ab");
    CursorAt(0, 0);

    EditorDeleteWordLeft();

    CHECK_STR(vted_buffer.rows[0].chars, "ab"); /* nothing deleted */
    CHECK_INT(vted_buffer.dirty, 0);
}

static void TestDeleteWordLeftOfBlanks(void)
{
    /* a run of nothing but blanks goes in one step */
    Begin("   ");
    CursorAt(0, 3);

    EditorDeleteWordLeft();
    CHECK_STR(vted_buffer.rows[0].chars, "");
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestDeleteWordLeftUtf8(void)
{
    /* the deletion moves by whole characters, never into a sequence */
    Begin("h\xC3\xA9llo w\xC3\xB6rld"); /* héllo wörld */
    CursorAt(0, 13);

    EditorDeleteWordLeft();
    CHECK_STR(vted_buffer.rows[0].chars, "h\xC3\xA9llo ");
    CHECK_INT(vted_editor_config.cursor_x, 7);
}

static void TestDeleteWordLeftUndoesAsSteps(void)
{
    /* two presses are two steps, undone one at a time */
    Begin("foo bar");
    CursorAt(0, 7);

    EditorDeleteWordLeft();
    EditorDeleteWordLeft();
    CHECK_STR(vted_buffer.rows[0].chars, "");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo ");
    CHECK_INT(vted_editor_config.cursor_x, 4);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo bar");
    CHECK_INT(vted_editor_config.cursor_x, 7);
}

static void TestDeleteWordRight(void)
{
    /* the word and the blanks behind it go, to the next word's start */
    Begin("foo bar baz");
    CursorAt(0, 0);

    EditorDeleteWordRight();
    CHECK_STR(vted_buffer.rows[0].chars, "bar baz");
    CHECK_INT(vted_editor_config.cursor_x, 0);
    CHECK_INT(vted_buffer.dirty, 1);

    /* undo puts them back, the cursor where it stood */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo bar baz");
    CHECK_INT(vted_editor_config.cursor_x, 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "bar baz");
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestDeleteWordRightFromInsideAWord(void)
{
    /* mid-word, the rest of the word goes, the blanks behind it with
       it */
    Begin("foo bar baz");
    CursorAt(0, 1);

    EditorDeleteWordRight();
    CHECK_STR(vted_buffer.rows[0].chars, "fbar baz");
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestDeleteWordRightOfBlanks(void)
{
    /* sitting on a run of blanks, the run goes up to the next word's
       start, the way the word move right crosses it */
    Begin("foo    bar");
    CursorAt(0, 3);

    EditorDeleteWordRight();
    CHECK_STR(vted_buffer.rows[0].chars, "foobar");
    CHECK_INT(vted_editor_config.cursor_x, 3);
}

static void TestDeleteWordRightAtRowEnd(void)
{
    /* the end of a line joins the line below, the way Delete does */
    Begin("ab");
    AppendRow("cd", 2, VTED_EOL_LF);
    CursorAt(0, 2);

    EditorDeleteWordRight();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestDeleteWordRightAtEndOfBuffer(void)
{
    Begin("ab");
    CursorAt(0, 2);

    EditorDeleteWordRight();

    CHECK_STR(vted_buffer.rows[0].chars, "ab"); /* nothing deleted */
    CHECK_INT(vted_buffer.dirty, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestDeleteWordRightOnPhantomRow(void)
{
    Begin("ab");
    CursorAt(1, 0); /* the row past the end */

    EditorDeleteWordRight();

    CHECK_STR(vted_buffer.rows[0].chars, "ab");
    CHECK_INT(vted_buffer.dirty, 0);
}

static void TestDeleteWordRightUtf8(void)
{
    /* the deletion moves by whole characters, never into a sequence */
    Begin("h\xC3\xA9llo w\xC3\xB6rld"); /* héllo wörld */
    CursorAt(0, 0);

    EditorDeleteWordRight();
    CHECK_STR(vted_buffer.rows[0].chars, "w\xC3\xB6rld");
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestDeleteWordRightUndoesAsSteps(void)
{
    /* two presses are two steps, undone one at a time */
    Begin("foo bar baz");
    CursorAt(0, 0);

    EditorDeleteWordRight();
    EditorDeleteWordRight();
    CHECK_STR(vted_buffer.rows[0].chars, "baz");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "bar baz");
    CHECK_INT(vted_editor_config.cursor_x, 0);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo bar baz");
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestInsertCompletion(void)
{
    /* the bytes past the prefix land at the cursor, the tail of the
       row rides along */
    Begin("foo  bar");
    CursorAt(0, 3);

    EditorInsertCompletion("foobar", 6, 3);

    CHECK_STR(vted_buffer.rows[0].chars, "foobar  bar");
    CHECK_INT(vted_editor_config.cursor_x, 6);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestInsertCompletionUtf8(void)
{
    /* the bytes past the prefix may be half of a character */
    Begin("caf x");
    CursorAt(0, 3);

    EditorInsertCompletion("caf\xC3\xA9", 5, 3);

    CHECK_STR(vted_buffer.rows[0].chars, "caf\xC3\xA9 x");
    CHECK_INT(vted_editor_config.cursor_x, 5);
}

static void TestInsertCompletionUndoesAsSteps(void)
{
    /* the pick is one step, and two picks undo one at a time */
    Begin("foo");
    CursorAt(0, 3);

    EditorInsertCompletion("foobar", 6, 3);
    EditorInsertCompletion("foobarizer", 10, 6);

    CHECK_STR(vted_buffer.rows[0].chars, "foobarizer");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foobar");
    CHECK_INT(vted_editor_config.cursor_x, 6);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo");
    CHECK_INT(vted_editor_config.cursor_x, 3);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foobar");
    CHECK_INT(vted_editor_config.cursor_x, 6);
}

static void TestInsertCompletionOfNothing(void)
{
    /* a word that extends nothing places nothing */
    Begin("foo bar");
    CursorAt(0, 3);

    EditorInsertCompletion("foo", 3, 3);

    CHECK_STR(vted_buffer.rows[0].chars, "foo bar");
    CHECK_INT(vted_editor_config.cursor_x, 3);
    CHECK_INT(vted_buffer.dirty, 0);
}

static void TestInsertCompletionOnPhantomRow(void)
{
    /* the phantom row past the end holds no prefix to complete */
    Begin("foo");
    CursorAt(1, 0);

    EditorInsertCompletion("foobar", 6, 3);

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "foo");
}

static void TestDeleteLine(void)
{
    Begin("one");
    AppendRow("two", 3, VTED_EOL_LF);
    AppendRow("three", 5, VTED_EOL_LF);
    CursorAt(1, 2);

    EditorDeleteLine();

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "three");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 2); /* the column survives */
}

static void TestDeleteLineClampsColumn(void)
{
    Begin("hello");
    AppendRow("ab", 2, VTED_EOL_LF);
    CursorAt(0, 5);

    EditorDeleteLine();

    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 2); /* fits the new line */
}

static void TestDeleteOnlyLine(void)
{
    Begin("abc");
    CursorAt(0, 2);

    EditorDeleteLine();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(vted_buffer.rows[0].size, 0);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestDuplicateLine(void)
{
    Begin("ab");
    CursorAt(0, 1);

    EditorDuplicateLine();

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "ab");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestToggleCommentOnTheLine(void)
{
    /* the token goes in front of the first non-blank with a space
       behind it, and the cursor rides the shift */
    Begin("code");
    CursorAt(0, 2);
    SyntaxSelectForFile("main.c");

    EditorToggleComment();

    CHECK_STR(vted_buffer.rows[0].chars, "// code");
    CHECK_INT(vted_editor_config.cursor_x, 5);
    CHECK_INT(vted_buffer.dirty, 1);

    /* a column inside the indent stays where it is */
    Begin("  code");
    CursorAt(0, 1);
    SyntaxSelectForFile("main.c");

    EditorToggleComment();

    CHECK_STR(vted_buffer.rows[0].chars, "  // code");
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestToggleCommentUndoesAsOneStep(void)
{
    Begin("one");
    AppendRow("two", 3, VTED_EOL_LF);
    UndoReset();
    CursorAt(0, 0);
    SyntaxSelectForFile("main.c");

    EditSetAnchor(0, 0);
    CursorAt(1, 3);

    EditorToggleComment();

    CHECK_STR(vted_buffer.rows[0].chars, "// one");
    CHECK_STR(vted_buffer.rows[1].chars, "// two");

    /* the selection lives on, its ends riding the shift, so a second
       press takes the comment back off */
    CHECK_INT(EditSelectionActive(), 1);
    {
        int top;
        int left;
        int bottom;
        int right;

        EditSelectionRange(&top, &left, &bottom, &right);
        CHECK_INT(top, 0);
        CHECK_INT(left, 3); /* the anchor rode the shift of its row */
        CHECK_INT(bottom, 1);
        CHECK_INT(right, 6);
    }

    EditorToggleComment();

    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(EditSelectionActive(), 1);

    /* one undo takes the whole span back, cursor and all */
    EditorToggleComment();

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 3);

    /* and one redo comments it again */
    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "// one");
    CHECK_STR(vted_buffer.rows[1].chars, "// two");

    EditClearSelection();
}

static void TestToggleCommentOnLanguageWithoutOne(void)
{
    /* a language that comments only in blocks changes nothing and
       records nothing to undo */
    Begin("<p>text</p>");
    UndoReset();
    CursorAt(0, 1);
    SyntaxSelectForFile("page.html");

    EditorToggleComment();

    CHECK_STR(vted_buffer.rows[0].chars, "<p>text</p>");
    CHECK_INT(vted_buffer.dirty, 0);
    CHECK_INT(EditorUndo(), 0);

    /* and plain text is the same story */
    SyntaxSelectForFile(NULL);

    EditorToggleComment();

    CHECK_STR(vted_buffer.rows[0].chars, "<p>text</p>");
    CHECK_INT(EditorUndo(), 0);
}

static void TestToggleCommentOnAllBlankLines(void)
{
    /* blank rows are never given a token, and a span of nothing but
       them leaves nothing to undo */
    Begin(" ");
    AppendRow("   ", 3, VTED_EOL_LF);
    UndoReset();
    CursorAt(0, 0);
    SyntaxSelectForFile("main.c");

    EditorToggleComment();

    CHECK_STR(vted_buffer.rows[0].chars, " ");
    CHECK_STR(vted_buffer.rows[1].chars, "   ");
    CHECK_INT(EditorUndo(), 0);
}

/* a small C-shaped block, the cursor on its opener */
static void BeginBlock(void)
{
    FreeBuffer();
    UndoReset();

    AppendRow("int main(void)", 14, VTED_EOL_LF);
    AppendRow("{", 1, VTED_EOL_LF);
    AppendRow("    body();", 11, VTED_EOL_LF);
    AppendRow("}", 1, VTED_EOL_LF);
    CursorAt(1, 0);
}

static void TestDuplicateTheBlockABracketEncloses(void)
{
    /* the cursor on the opener: the whole block is copied below
       itself, and the cursor lands on the copy of its own row, the
       column kept */
    BeginBlock();

    EditorDuplicateLine();

    CHECK_INT(vted_buffer.num_rows, 7);
    CHECK_STR(vted_buffer.rows[0].chars, "int main(void)");
    CHECK_STR(vted_buffer.rows[3].chars, "}");
    CHECK_STR(vted_buffer.rows[4].chars, "{");
    CHECK_STR(vted_buffer.rows[5].chars, "    body();");
    CHECK_STR(vted_buffer.rows[6].chars, "}");
    CHECK_INT(vted_editor_config.cursor_y, 4);
    CHECK_INT(vted_editor_config.cursor_x, 0);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestDuplicateTheBlockFromTheCloser(void)
{
    /* the cursor right past the closer: the block copies the same
       way, and the cursor lands on the copy of the closer's row */
    BeginBlock();
    CursorAt(3, 1);

    EditorDuplicateLine();

    CHECK_INT(vted_buffer.num_rows, 7);
    CHECK_STR(vted_buffer.rows[4].chars, "{");
    CHECK_STR(vted_buffer.rows[6].chars, "}");
    CHECK_INT(vted_editor_config.cursor_y, 6);
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestDuplicateTheBlockKeepsTheEndings(void)
{
    /* every copied line keeps the ending its own original has */
    BeginBlock();
    vted_buffer.rows[1].eol = VTED_EOL_CRLF;
    vted_buffer.rows[3].eol = VTED_EOL_CR;

    EditorDuplicateLine();

    CHECK_INT(vted_buffer.rows[4].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[5].eol, VTED_EOL_LF);
    CHECK_INT(vted_buffer.rows[6].eol, VTED_EOL_CR);
}

static void TestDuplicateABlockOnOneRowCopiesTheRow(void)
{
    /* the bracket and its partner on the same row: the block is that
       row, copied once the way a line always is */
    Begin("f(a, b)");
    CursorAt(0, 2); /* on the ( */

    EditorDuplicateLine();

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "f(a, b)");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 2);
}

static void TestDuplicateWithoutAMatchCopiesTheLine(void)
{
    /* an opener with no closer encloses nothing: the line copies on
       its own */
    Begin("{");
    CursorAt(0, 0);

    EditorDuplicateLine();

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "{");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestMoveLineUpRoundTrip(void)
{
    FreeBuffer();
    UndoReset();

    AppendRow("first", 5, VTED_EOL_LF);
    AppendRow("second", 6, VTED_EOL_LF);
    AppendRow("third", 5, VTED_EOL_LF);
    CursorAt(2, 0); /* on "third" */

    EditorMoveLineUp();
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "first");
    CHECK_STR(vted_buffer.rows[1].chars, "third");
    CHECK_STR(vted_buffer.rows[2].chars, "second");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_buffer.dirty, 1);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[1].chars, "second");
    CHECK_STR(vted_buffer.rows[2].chars, "third");
    CHECK_INT(vted_editor_config.cursor_y, 2);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[1].chars, "third");
    CHECK_INT(vted_editor_config.cursor_y, 1);
}

static void TestMoveLineDownRoundTrip(void)
{
    FreeBuffer();
    UndoReset();

    AppendRow("first", 5, VTED_EOL_LF);
    AppendRow("second", 6, VTED_EOL_LF);
    AppendRow("third", 5, VTED_EOL_LF);
    CursorAt(0, 0); /* on "first" */

    EditorMoveLineDown();
    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "second");
    CHECK_STR(vted_buffer.rows[1].chars, "first");
    CHECK_STR(vted_buffer.rows[2].chars, "third");
    CHECK_INT(vted_editor_config.cursor_y, 1);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "first");
    CHECK_INT(vted_editor_config.cursor_y, 0);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "second");
    CHECK_INT(vted_editor_config.cursor_y, 1);
}

static void TestMoveLineAtTheEdgesDoesNothing(void)
{
    FreeBuffer();
    UndoReset();

    AppendRow("first", 5, VTED_EOL_LF);
    AppendRow("second", 6, VTED_EOL_LF);
    CursorAt(0, 0);

    EditorMoveLineUp();
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "first");

    CursorAt(1, 0);
    EditorMoveLineDown();
    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "second");

    CHECK_INT(EditorUndo(), 0);
    CHECK_INT(EditorRedo(), 0);
}


static void TestBufferReplaceRegexWholeMatches(void)
{
    Begin("one cat, two cats");

    CHECK_INT(RegexCompile("cat") , 0);
    CHECK_INT(BufferReplaceRegex("dog"), 2);
    CHECK_STR(vted_buffer.rows[0].chars, "one dog, two dogs");

    /* the walk that ran left the pattern behind: it is taken down so
       a later replace of nothing stands for nothing */
    RegexForget();
    Begin("abc");
    CHECK_INT(RegexCompile("b"), 0);
    CHECK_INT(BufferReplaceRegex("X"), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aXc");
    RegexForget();
}

static void TestBufferReplaceRegexGroups(void)
{
    Begin("2026-11-05");

    /* the groups of the match are written where their escapes stand */
    CHECK_INT(RegexCompile("([0-9]+)-([0-9]+)-([0-9]+)"), 0);
    CHECK_INT(BufferReplaceRegex("\\3.\\2.\\1"), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "05.11.2026");

    /* the whole match is group 0 */
    Begin("ab cd");
    CHECK_INT(RegexCompile("[a-z]+"), 0);
    CHECK_INT(BufferReplaceRegex("[\\0]"), 2);
    CHECK_STR(vted_buffer.rows[0].chars, "[ab] [cd]");

    /* a group that took no part writes nothing, and two slashes
       stand for one */
    Begin("b and c");
    CHECK_INT(RegexCompile("(a|b) and (a|c)"), 0);
    CHECK_INT(BufferReplaceRegex("\\1\\\\\\2"), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "b\\c");
    RegexForget();

    /* a group that took no part in the match writes nothing at all,
       the way an optional group left aside stays empty */
    Begin("b and c");
    CHECK_INT(RegexCompile("b (x)?and (a|c)"), 0);
    CHECK_INT(BufferReplaceRegex("[\\1][\\2]"), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "[][c]");
    RegexForget();
}

static void TestBufferReplaceRegexFoldsCase(void)
{
    Begin("Foo and FOO");

    RegexSetIgnoreCase(1);
    CHECK_INT(RegexCompile("foo"), 0);
    CHECK_INT(BufferReplaceRegex("bar"), 2);
    CHECK_STR(vted_buffer.rows[0].chars, "bar and bar");
    RegexSetIgnoreCase(0);
    RegexForget();
}

static void TestBufferReplaceRegexAcrossRows(void)
{
    FreeBuffer();
    UndoReset();

    AppendRow("a1 b2", 5, VTED_EOL_LF);
    AppendRow("c3 d4", 5, VTED_EOL_LF);

    CHECK_INT(RegexCompile("[a-z][0-9]"), 0);
    CHECK_INT(BufferReplaceRegex("<\\0>"), 4);
    CHECK_STR(vted_buffer.rows[0].chars, "<a1> <b2>");
    CHECK_STR(vted_buffer.rows[1].chars, "<c3> <d4>");

    /* the replacements undo in reverse order, the way the literal
       replace lays its steps down */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[1].chars, "<c3> d4");
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[1].chars, "c3 d4");
    RegexForget();
}

static void TestBufferReplaceRegexOfNothing(void)
{
    Begin("abc");

    /* no pattern standing, or one the row never matches, replaces
       nothing */
    RegexForget();
    CHECK_INT(BufferReplaceRegex("x"), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");

    CHECK_INT(RegexCompile("zzz"), 0);
    CHECK_INT(BufferReplaceRegex("x"), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(vted_buffer.dirty, 0);
    RegexForget();
}

static void TestBufferReplaceSpan(void)
{
    Begin("one foo, two foo");

    /* the span in the middle comes out and the new text goes in,
       one undoable step */
    CHECK_INT(BufferReplaceSpan(0, 4, 3, "bar"), 3);
    CHECK_STR(vted_buffer.rows[0].chars, "one bar, two foo");
    CHECK_INT(vted_buffer.dirty, 1);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "one foo, two foo");

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "one bar, two foo");
}

static void TestBufferReplaceSpanAtTheEdges(void)
{
    /* a span at the head of the row */
    Begin("abc");
    CHECK_INT(BufferReplaceSpan(0, 0, 1, "X"), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "Xbc");

    /* a span at its end */
    Begin("abc");
    CHECK_INT(BufferReplaceSpan(0, 2, 1, "X"), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abX");

    /* an empty new text deletes the span, nothing written */
    Begin("axb");
    CHECK_INT(BufferReplaceSpan(0, 1, 1, ""), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "ab");
}

static void TestBufferReplaceSpanOutsideTheRow(void)
{
    Begin("abc");

    /* a span that runs past the row, one that starts past it, a row
       that is not there, one before the start: nothing changes */
    CHECK_INT(BufferReplaceSpan(0, 2, 4, "X"), -1);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(BufferReplaceSpan(0, 7, 1, "X"), -1);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(BufferReplaceSpan(3, 0, 1, "X"), -1);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(BufferReplaceSpan(-1, 0, 1, "X"), -1);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(vted_buffer.dirty, 0);
}

static void TestBufferReplaceSpanRegex(void)
{
    Begin("2026-11-05");

    /* the groups of the match that starts at `at` fill the escapes,
       the way the replace-all fills them */
    CHECK_INT(RegexCompile("([0-9]+)-([0-9]+)-([0-9]+)"), 0);
    CHECK_INT(BufferReplaceSpanRegex(0, 0, 10, "\\3.\\2.\\1"), 10);
    CHECK_STR(vted_buffer.rows[0].chars, "05.11.2026");
    CHECK_INT(vted_buffer.dirty, 1);

    /* the undo puts the bytes that were there back, one step */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "2026-11-05");
    RegexForget();
}

static void TestBufferReplaceSpanRegexEachMatchWithItsOwn(void)
{
    /* two matches in a row: each is written with the groups of the
       match it is told to replace, not the other one's */
    Begin("one cat, two cats");

    CHECK_INT(RegexCompile("cat(s?)"), 0);

    /* the first, a cat of no s: its group wrote nothing */
    CHECK_INT(BufferReplaceSpanRegex(0, 4, 3, "dog\\1"), 3);
    CHECK_STR(vted_buffer.rows[0].chars, "one dog, two cats");

    /* the second, an s of its own: the group of the match that
       starts there carries it */
    CHECK_INT(BufferReplaceSpanRegex(0, 13, 4, "dog\\1"), 4);
    CHECK_STR(vted_buffer.rows[0].chars, "one dog, two dogs");

    /* no match starts there: nothing changes */
    CHECK_INT(BufferReplaceSpanRegex(0, 2, 3, "X"), -1);
    CHECK_STR(vted_buffer.rows[0].chars, "one dog, two dogs");
    RegexForget();
}

static void TestInsertTextInsideALine(void)
{
    Begin("abcd");
    CursorAt(0, 2);

    EditorInsertText("XY", 2);

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abXYcd");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 4);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestInsertTextSeveralLines(void)
{
    /* the rest of the line rides to the end of the pasted text */
    Begin("abcd");
    CursorAt(0, 2);

    EditorInsertText("X\nYY\nZ", 6);

    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "abX");
    CHECK_STR(vted_buffer.rows[1].chars, "YY");
    CHECK_STR(vted_buffer.rows[2].chars, "Zcd");
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestInsertTextAddsNoIndent(void)
{
    /* Enter would copy the four blanks; a paste brings its own */
    Begin("    abcd");
    CursorAt(0, 6);

    EditorInsertText("X\nY", 3);

    CHECK_STR(vted_buffer.rows[0].chars, "    abX");
    CHECK_STR(vted_buffer.rows[1].chars, "Ycd");
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestInsertTextEndingWithABreak(void)
{
    /* a copied whole line ends with its break: the cursor lands at
       the start of the line the old tail now sits on */
    Begin("abcd");
    CursorAt(0, 2);

    EditorInsertText("X\n", 2);

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "abX");
    CHECK_STR(vted_buffer.rows[1].chars, "cd");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestInsertTextLineBreakForms(void)
{
    /* CR LF, a lone CR and a lone LF are each one break */
    Begin("");
    EditorInsertText("a\r\nb\rc\nd", 9);

    CHECK_INT(vted_buffer.num_rows, 4);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_STR(vted_buffer.rows[1].chars, "b");
    CHECK_STR(vted_buffer.rows[2].chars, "c");
    CHECK_STR(vted_buffer.rows[3].chars, "d");
}

static void TestInsertTextDropsControlBytes(void)
{
    /* an ESC, a NUL and a DEL are dropped; a Tab and UTF-8 stay */
    Begin("");
    EditorInsertText("a\x1b" "b\0c\x7f" "d\te\xc3\xa9", 11);

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcd\te\xc3\xa9");
    CHECK_INT(vted_editor_config.cursor_x, 8); /* bytes: the é takes two */
}

static void TestInsertTextOfNothing(void)
{
    /* only control bytes, or no bytes: nothing changes, not even the
       dirty flag */
    Begin("abc");
    CursorAt(0, 1);

    EditorInsertText("\x1b\x01", 2);
    EditorInsertText("", 0);

    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(vted_editor_config.cursor_x, 1);
    CHECK_INT(vted_buffer.dirty, 0);
}

static void TestInsertTextIntoAnEmptyBuffer(void)
{
    Begin(NULL); /* no rows at all */

    EditorInsertText("one\ntwo", 7);

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 3);
}

static void TestInsertTextOnPhantomRow(void)
{
    /* below the last line the paste starts new lines, ending the way
       the file does */
    Begin("a");
    vted_buffer.rows[0].eol = VTED_EOL_CRLF;
    CursorAt(1, 0);

    EditorInsertText("p\nq", 3);

    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[0].chars, "a");
    CHECK_STR(vted_buffer.rows[1].chars, "p");
    CHECK_STR(vted_buffer.rows[2].chars, "q");
    CHECK_INT(vted_buffer.rows[1].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_buffer.rows[2].eol, VTED_EOL_CRLF);
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 1);
}

static void TestInsertTextEndingTheFile(void)
{
    /* pasted on the row past the end, the break at the end of the text
       only ends its last line: no empty line below, the cursor back
       on the row past the end, like after reading a file */
    Begin("a");
    CursorAt(1, 0);

    EditorInsertText("p\nq\n", 5);

    CHECK_INT(vted_buffer.num_rows, 3);
    CHECK_STR(vted_buffer.rows[1].chars, "p");
    CHECK_STR(vted_buffer.rows[2].chars, "q");
    CHECK_INT(vted_editor_config.cursor_y, 3);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestInsertTextOfOneBreakEndingTheFile(void)
{
    /* a lone line break there makes one empty line, like Enter does */
    Begin("a");
    CursorAt(1, 0);

    EditorInsertText("\n", 1);

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[1].chars, "");
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 0);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestInsertTextEndingWithABreakIntoAnEmptyBuffer(void)
{
    Begin(NULL);

    EditorInsertText("one\ntwo\n", 8);

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(vted_editor_config.cursor_y, 2);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestInsertTextBreakAtTheEndOfTheLastLine(void)
{
    /* with the cursor on the last real line, Enter-like behavior stays:
       the break makes an empty line for the cursor to sit on */
    Begin("x");
    CursorAt(0, 1);

    EditorInsertText("1\n", 2);

    CHECK_INT(vted_buffer.num_rows, 2);
    CHECK_STR(vted_buffer.rows[0].chars, "x1");
    CHECK_STR(vted_buffer.rows[1].chars, "");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 0);
}

static void TestIndentSelectionIndentsTheSpan(void)
{
    Begin("one");
    AppendRow("two", 3, VTED_EOL_LF);
    AppendRow("three", 5, VTED_EOL_LF);
    UndoReset();
    EditClearSelection();
    CursorAt(1, 1);

    EditSetAnchor(0, 0);
    CursorAt(1, 1);

    EditorIndentSelection();

    CHECK_STR(vted_buffer.rows[0].chars, "\tone");
    CHECK_STR(vted_buffer.rows[1].chars, "\ttwo");
    CHECK_STR(vted_buffer.rows[2].chars, "three"); /* past the span */

    /* the selection lives on, both its ends riding the shift */
    CHECK_INT(EditSelectionActive(), 1);
    {
        int top;
        int left;
        int bottom;
        int right;

        EditSelectionRange(&top, &left, &bottom, &right);
        CHECK_INT(top, 0);
        CHECK_INT(left, 1);  /* the anchor rode its row */
        CHECK_INT(bottom, 1);
        CHECK_INT(right, 2); /* the cursor rode its own */
    }

    /* one undo takes the whole span back, cursor and all */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "two");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 1);

    /* and one redo indents it again */
    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "\tone");
    CHECK_STR(vted_buffer.rows[1].chars, "\ttwo");

    EditClearSelection();
}

static void TestIndentSelectionSkipsBlankRows(void)
{
    /* a row of only blanks keeps none of the step, the rows around
       it take theirs */
    Begin("one");
    AppendRow("", 0, VTED_EOL_LF);
    AppendRow("  two", 5, VTED_EOL_LF);
    UndoReset();
    EditClearSelection();

    EditSetAnchor(0, 0);
    CursorAt(2, 2);

    EditorIndentSelection();

    CHECK_STR(vted_buffer.rows[0].chars, "\tone");
    CHECK_STR(vted_buffer.rows[1].chars, "");
    CHECK_STR(vted_buffer.rows[2].chars, "\t  two");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[2].chars, "  two");

    EditClearSelection();
}

static void TestIndentSelectionOfSpaces(void)
{
    /* with insert-spaces on, the step is a run of spaces a tab stop
       wide, the bytes the Tab key alone would put in */
    Begin("a");
    AppendRow("b", 1, VTED_EOL_LF);
    UndoReset();
    EditClearSelection();
    vted_editor_config.insert_spaces = 1;
    RowSetTabWidth(4);

    EditSetAnchor(0, 0);
    CursorAt(1, 1);

    EditorIndentSelection();

    CHECK_STR(vted_buffer.rows[0].chars, "    a");
    CHECK_STR(vted_buffer.rows[1].chars, "    b");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a");

    RowSetTabWidth(VTED_TAB_STOP);
    vted_editor_config.insert_spaces = 0;
    EditClearSelection();
}

static void TestIndentOnAllBlankRowsDoesNothing(void)
{
    /* nothing the step could sit in front of: nothing changes, and
       nothing enters the history */
    Begin("");
    AppendRow("   ", 3, VTED_EOL_LF);
    AppendRow("\t\t", 2, VTED_EOL_LF);
    UndoReset();
    EditClearSelection();

    EditSetAnchor(0, 0);
    CursorAt(1, 0);

    EditorIndentSelection();

    CHECK_STR(vted_buffer.rows[0].chars, "   ");
    CHECK_STR(vted_buffer.rows[1].chars, "\t\t");
    CHECK_INT(EditorUndo(), 0);

    EditClearSelection();
}

static void TestIndentOnPhantomRowDoesNothing(void)
{
    Begin("x");
    UndoReset();
    EditClearSelection();
    CursorAt(1, 0); /* the row past the end */

    EditorIndentSelection();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_INT(EditorUndo(), 0);

    EditClearSelection();
}

static void TestOutdentSelectionTakesTheStepBack(void)
{
    Begin("\tone");
    AppendRow("\t  two", 6, VTED_EOL_LF);
    UndoReset();
    EditClearSelection();
    vted_editor_config.tab_width = 4;

    EditSetAnchor(0, 1);
    CursorAt(1, 3);

    EditorOutdentSelection();

    CHECK_STR(vted_buffer.rows[0].chars, "one");
    CHECK_STR(vted_buffer.rows[1].chars, "  two"); /* the tab went */

    /* the selection lives on, its ends riding the shrink */
    CHECK_INT(EditSelectionActive(), 1);
    {
        int top;
        int left;
        int bottom;
        int right;

        EditSelectionRange(&top, &left, &bottom, &right);
        CHECK_INT(top, 0);
        CHECK_INT(left, 0);  /* the anchor rode back */
        CHECK_INT(bottom, 1);
        CHECK_INT(right, 2);
    }

    /* one undo puts the whole span back */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "\tone");
    CHECK_STR(vted_buffer.rows[1].chars, "\t  two");
    CHECK_INT(vted_editor_config.cursor_y, 1);
    CHECK_INT(vted_editor_config.cursor_x, 3);

    /* and one redo takes the step off again */
    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "one");

    vted_editor_config.tab_width = VTED_TAB_STOP;
    EditClearSelection();
}

static void TestOutdentOfTheLineUnderTheCursor(void)
{
    /* without a selection, the row of the cursor takes the step */
    Begin("    deep");
    UndoReset();
    EditClearSelection();
    vted_editor_config.tab_width = 4;
    CursorAt(0, 7); /* on the last p */

    EditorOutdentSelection();

    CHECK_STR(vted_buffer.rows[0].chars, "deep");
    CHECK_INT(vted_editor_config.cursor_x, 3);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "    deep");
    CHECK_INT(vted_editor_config.cursor_x, 7);

    vted_editor_config.tab_width = VTED_TAB_STOP;
    EditClearSelection();
}

static void TestOutdentWithNothingToTakeDoesNothing(void)
{
    Begin("plain");
    UndoReset();
    EditClearSelection();
    vted_editor_config.tab_width = 4;
    CursorAt(0, 2);

    EditorOutdentSelection();

    CHECK_STR(vted_buffer.rows[0].chars, "plain");
    CHECK_INT(vted_editor_config.cursor_x, 2);
    CHECK_INT(EditorUndo(), 0);

    vted_editor_config.tab_width = VTED_TAB_STOP;
    EditClearSelection();
}

static void TestDeleteSelectionSingleRowKeepsTail(void)
{
    Begin("abcdef");

    /* select "bcd", columns 1..4, leaving "a" before and "ef" after */
    EditSetAnchor(0, 1);
    vted_editor_config.cursor_x = 4;

    EditDeleteSelection();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aef");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 1);
    CHECK_INT(vted_buffer.dirty, 1);
}

static void TestDeleteSelectionMultiRowKeepsBottomTail(void)
{
    FreeBuffer();
    UndoReset();

    AppendRow("HEADtopTAIL", 12, VTED_EOL_LF);
    AppendRow("MID1", 4, VTED_EOL_LF);
    AppendRow("BOTbottom", 9, VTED_EOL_LF);

    /* select from column 4 of row 0 to column 3 of row 2: the top
       row's tail, the whole middle row and the bottom row's head
       vanish; the bottom row's tail rides onto the top row */
    EditSetAnchor(0, 4);
    vted_editor_config.cursor_y = 2;
    vted_editor_config.cursor_x = 3;

    EditDeleteSelection();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "HEADbottom");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 4);
}

static void TestSelectionEndingPastTheLastRow(void)
{
    /* a selection that reaches the phantom row past the last line —
       the way Shift-Down onto the tildes below the text makes — ends
       at the end of that line: the range says so, the copy carries
       the last row whole and the cut takes it out, and none of them
       looks at a row that is not there */
    FreeBuffer();
    UndoReset();

    AppendRow("abc", 3, VTED_EOL_LF);
    AppendRow("def", 3, VTED_EOL_LF);

    EditSetAnchor(0, 1);
    vted_editor_config.cursor_y = 2; /* the phantom row past the end */
    vted_editor_config.cursor_x = 0;

    /* the range ends at the end of the last row */
    {
        int top;
        int left;
        int bottom;
        int right;

        EditSelectionRange(&top, &left, &bottom, &right);

        CHECK_INT(top, 0);
        CHECK_INT(left, 1);
        CHECK_INT(bottom, 1);
        CHECK_INT(right, 3);
    }

    /* the copy carries the tail of the first row and the last row
       whole */
    {
        int length = 0;
        char *text = EditCopyText(&length);

        CHECK_STR(text, "bc\ndef");
        free(text);
    }

    /* the cut leaves the head of the first row alone */
    EditDeleteSelection();

    CHECK_INT(vted_buffer.num_rows, 1);
    CHECK_STR(vted_buffer.rows[0].chars, "a");

    /* the same selection the other way round: the anchor past the
       end, the cursor inside the text */
    FreeBuffer();
    UndoReset();

    AppendRow("abc", 3, VTED_EOL_LF);
    AppendRow("def", 3, VTED_EOL_LF);

    EditSetAnchor(2, 0); /* the phantom row past the end */
    vted_editor_config.cursor_y = 0;
    vted_editor_config.cursor_x = 1;

    {
        int top;
        int left;
        int bottom;
        int right;

        EditSelectionRange(&top, &left, &bottom, &right);

        CHECK_INT(top, 0);
        CHECK_INT(left, 1);
        CHECK_INT(bottom, 1);
        CHECK_INT(right, 3);
    }
}

static void TestDeleteSelectionRoundTrips(void)
{
    Begin("abcdef");

    EditSetAnchor(0, 1);
    vted_editor_config.cursor_x = 4;

    EditDeleteSelection();
    CHECK_STR(vted_buffer.rows[0].chars, "aef");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcdef");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 4);

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aef");
    CHECK_INT(vted_editor_config.cursor_y, 0);
    CHECK_INT(vted_editor_config.cursor_x, 1);

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcdef");
}

static void TestBufferReplaceAll(void)
{
    Begin("abcabc");

    CHECK_INT(BufferReplaceAll("b", "B"), 2);
    CHECK_STR(vted_buffer.rows[0].chars, "aBcaBc");
    CHECK_INT(vted_buffer.dirty, 1);

    /* each occurrence is its own step, undone in reverse order */
    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aBcabc");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "abcabc");

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aBcabc");

    CHECK_INT(EditorRedo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aBcaBc");
}

static void TestBufferReplaceAllDeletesWithEmptyReplacement(void)
{
    Begin("axbxc");

    CHECK_INT(BufferReplaceAll("x", ""), 2);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
}

static void TestBufferReplaceAllAcrossRows(void)
{
    FreeBuffer();
    UndoReset();

    AppendRow("foo bar foo", 11, VTED_EOL_LF);
    AppendRow("bar foo", 7, VTED_EOL_LF);

    CHECK_INT(BufferReplaceAll("foo", "FOO"), 3);
    CHECK_STR(vted_buffer.rows[0].chars, "FOO bar FOO");
    CHECK_STR(vted_buffer.rows[1].chars, "bar FOO");
    CHECK_INT(vted_buffer.num_rows, 2);
}

static void TestBufferReplaceAllEmptyQueryDoesNothing(void)
{
    Begin("abc");

    CHECK_INT(BufferReplaceAll("", "x"), 0);
    CHECK_STR(vted_buffer.rows[0].chars, "abc");
    CHECK_INT(vted_buffer.dirty, 0);
}

static void TestBufferReplaceAllFoldsCase(void)
{
    /* with the fold off, only the exact bytes answer */
    RowSetIgnoreCase(0);
    Begin("aBc abc AbC");

    CHECK_INT(BufferReplaceAll("abc", "X"), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aBc X AbC");

    /* folded, every spelling is replaced, each still its own step */
    RowSetIgnoreCase(1);
    Begin("aBc abc AbC");

    CHECK_INT(BufferReplaceAll("abc", "X"), 3);
    CHECK_STR(vted_buffer.rows[0].chars, "X X X");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "X X AbC");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "X abc AbC");

    CHECK_INT(EditorUndo(), 1);
    CHECK_STR(vted_buffer.rows[0].chars, "aBc abc AbC");

    RowSetIgnoreCase(0);
}

int main(void)
{
    TestInsertChar();
    TestInsertCharOnPhantomRow();
    TestInsertPair();
    TestInsertPairOnPhantomRow();
    TestInsertPairUndoRedo();
    TestInsertPairFoldsIntoTheTypingAfterIt();
    TestDeletePair();
    TestDeletePairOnlyInsideAnEmptyOne();
    TestWrapSelection();
    TestWrapSelectionKeepsTheCursorEnd();
    TestWrapSelectionAcrossRows();
    TestWrapSelectionAtTheRowsEnd();
    TestWrapSelectionUndoRedo();
    TestWrapSelectionInAQuote();
    TestWrapSelectionOnlyWithSomethingSelected();
    TestWrapSelectionRidesLaterKeys();
    TestInsertNewlineSplits();
    TestInsertNewlineKeepsIndent();
    TestInsertNewlineAtColumnZero();
    TestInsertNewlineKeepsEndings();
    TestDelChar();
    TestDelCharMultibyte();
    TestDelCharMerges();
    TestDelCharAtStartOfFile();
    TestDeleteWordLeft();
    TestDeleteWordLeftFromInsideAWord();
    TestDeleteWordLeftOverBlanks();
    TestDeleteWordLeftAtColumnZero();
    TestDeleteWordLeftAtStartOfFile();
    TestDeleteWordLeftOfBlanks();
    TestDeleteWordLeftUtf8();
    TestDeleteWordLeftUndoesAsSteps();
    TestDeleteWordRight();
    TestDeleteWordRightFromInsideAWord();
    TestDeleteWordRightOfBlanks();
    TestDeleteWordRightAtRowEnd();
    TestDeleteWordRightAtEndOfBuffer();
    TestDeleteWordRightOnPhantomRow();
    TestDeleteWordRightUtf8();
    TestDeleteWordRightUndoesAsSteps();
    TestInsertCompletion();
    TestInsertCompletionUtf8();
    TestInsertCompletionUndoesAsSteps();
    TestInsertCompletionOfNothing();
    TestInsertCompletionOnPhantomRow();
    TestDeleteLine();
    TestDeleteLineClampsColumn();
    TestDeleteOnlyLine();
    TestDuplicateLine();
    TestToggleCommentOnTheLine();
    TestToggleCommentUndoesAsOneStep();
    TestToggleCommentOnLanguageWithoutOne();
    TestToggleCommentOnAllBlankLines();
    TestDuplicateTheBlockABracketEncloses();
    TestDuplicateTheBlockFromTheCloser();
    TestDuplicateTheBlockKeepsTheEndings();
    TestDuplicateABlockOnOneRowCopiesTheRow();
    TestDuplicateWithoutAMatchCopiesTheLine();
    TestMoveLineUpRoundTrip();
    TestMoveLineDownRoundTrip();
    TestMoveLineAtTheEdgesDoesNothing();
    TestDeleteSelectionSingleRowKeepsTail();
    TestDeleteSelectionMultiRowKeepsBottomTail();
    TestSelectionEndingPastTheLastRow();
    TestDeleteSelectionRoundTrips();
    TestBufferReplaceAll();
    TestBufferReplaceAllDeletesWithEmptyReplacement();
    TestBufferReplaceAllAcrossRows();
    TestBufferReplaceAllEmptyQueryDoesNothing();
    TestBufferReplaceAllFoldsCase();
    TestBufferReplaceRegexWholeMatches();
    TestBufferReplaceRegexGroups();
    TestBufferReplaceRegexFoldsCase();
    TestBufferReplaceRegexAcrossRows();
    TestBufferReplaceRegexOfNothing();
    TestBufferReplaceSpan();
    TestBufferReplaceSpanAtTheEdges();
    TestBufferReplaceSpanOutsideTheRow();
    TestBufferReplaceSpanRegex();
    TestBufferReplaceSpanRegexEachMatchWithItsOwn();
    TestInsertTextInsideALine();
    TestInsertTextSeveralLines();
    TestInsertTextAddsNoIndent();
    TestInsertTextEndingWithABreak();
    TestInsertTextLineBreakForms();
    TestInsertTextDropsControlBytes();
    TestInsertTextOfNothing();
    TestInsertTextIntoAnEmptyBuffer();
    TestInsertTextOnPhantomRow();
    TestInsertTextEndingTheFile();
    TestInsertTextOfOneBreakEndingTheFile();
    TestInsertTextEndingWithABreakIntoAnEmptyBuffer();
    TestInsertTextBreakAtTheEndOfTheLastLine();
    TestIndentSelectionIndentsTheSpan();
    TestIndentSelectionSkipsBlankRows();
    TestIndentSelectionOfSpaces();
    TestIndentOnAllBlankRowsDoesNothing();
    TestIndentOnPhantomRowDoesNothing();
    TestOutdentSelectionTakesTheStepBack();
    TestOutdentOfTheLineUnderTheCursor();
    TestOutdentWithNothingToTakeDoesNothing();

    return TestSummary("edit");
}

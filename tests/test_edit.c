#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_screen.h"
#include "src/vted_row.h"
#include "src/vted_edit.h"
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

int main(void)
{
    TestInsertChar();
    TestInsertCharOnPhantomRow();
    TestInsertNewlineSplits();
    TestInsertNewlineKeepsIndent();
    TestInsertNewlineAtColumnZero();
    TestInsertNewlineKeepsEndings();
    TestDelChar();
    TestDelCharMultibyte();
    TestDelCharMerges();
    TestDelCharAtStartOfFile();
    TestDeleteLine();
    TestDeleteLineClampsColumn();
    TestDeleteOnlyLine();
    TestDuplicateLine();
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

    return TestSummary("edit");
}

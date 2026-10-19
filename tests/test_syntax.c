#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_row.h"
#include "src/vted_syntax.h"
/*===============================*/

/* the classes of a row as one letter per byte, so a check can be read at
   a glance: n normal, c comment, s string, d number, k keyword, t type,
   p preprocessor */
static const char *Classes(int at)
{
    static char text[256];
    unsigned char hl[256];
    int size = vted_buffer.rows[at].size;
    int i;

    SyntaxHighlightRow(at, hl);

    for (i = 0; i < size; i++)
    {
        text[i] = "ncsdktp"[hl[i]];
    }

    text[size] = '\0';
    return text;
}

/* replaces the buffer with the given lines and starts the highlighting
   over, as opening a file does */
static void Load(const char *const *lines, int count)
{
    int i;

    FreeBuffer();
    InitBuffer();

    for (i = 0; i < count; i++)
    {
        AppendRow(lines[i], (int)strlen(lines[i]), VTED_EOL_LF);
    }

    SyntaxInvalidate(0);
}

static void LoadOne(const char *line)
{
    const char *lines[1];

    lines[0] = line;
    Load(lines, 1);
}

static void TestSelectingTheLanguage(void)
{
    SyntaxSelectForFile("main.c");
    CHECK_STR(SyntaxLanguageName(), "C");

    SyntaxSelectForFile("include/vted_row.h");
    CHECK_STR(SyntaxLanguageName(), "C");

    /* only the extension counts, not a dot in a directory name */
    SyntaxSelectForFile("my.dir/notes");
    CHECK(SyntaxLanguageName() == NULL);

    SyntaxSelectForFile("notes.txt");
    CHECK(SyntaxLanguageName() == NULL);

    /* a hidden file is not an extension */
    SyntaxSelectForFile(".c");
    CHECK(SyntaxLanguageName() == NULL);

    SyntaxSelectForFile(NULL);
    CHECK(SyntaxLanguageName() == NULL);
}

static void TestPlainTextIsLeftAlone(void)
{
    SyntaxSelectForFile("notes.txt");
    LoadOne("int x = 42; // not code");
    CHECK_STR(Classes(0), "nnnnnnnnnnnnnnnnnnnnnnn");
}

static void TestKeywordsAndTypes(void)
{
    SyntaxSelectForFile("a.c");

    LoadOne("int x = 42;");
    CHECK_STR(Classes(0), "tttnnnnnddn");

    LoadOne("return 0;");
    CHECK_STR(Classes(0), "kkkkkkndn");

    /* a word has to match whole: these only contain keywords */
    LoadOne("intx int_t xint");
    CHECK_STR(Classes(0), "nnnnnnnnnnnnnnn");

    LoadOne("foo(int)");
    CHECK_STR(Classes(0), "nnnnttt" "n");

    LoadOne("p = NULL;");
    CHECK_STR(Classes(0), "nnnnddddn");
}

static void TestNumbers(void)
{
    SyntaxSelectForFile("a.c");

    LoadOne("0x1F + 1.5e3 + x1 + 10UL");
    CHECK_STR(Classes(0), "ddddnnnddddd" "nnnnnnnn" "dddd");
}

static void TestLineComment(void)
{
    SyntaxSelectForFile("a.c");

    LoadOne("x = 1; // note");
    CHECK_STR(Classes(0), "nnnndnnccccccc");

    /* a comment hides the keywords inside it */
    LoadOne("// return int");
    CHECK_STR(Classes(0), "ccccccccccccc");
}

static void TestStrings(void)
{
    SyntaxSelectForFile("a.c");

    /* the // inside the string is not a comment, the \n does not end it */
    LoadOne("printf(\"a // b\\n\");");
    CHECK_STR(Classes(0), "nnnnnnn" "ssssssssss" "nn");

    LoadOne("'a' + '\\''");
    CHECK_STR(Classes(0), "sssnnn" "ssss");

    /* UTF-8 bytes inside a string stay in the string */
    LoadOne("\"\xC3\xA9\" int");
    CHECK_STR(Classes(0), "ssss" "n" "ttt");

    /* a string that is never closed stops at the end of the row */
    {
        const char *lines[2] = {"\"abc", "int"};

        Load(lines, 2);
        CHECK_STR(Classes(0), "ssss");
        CHECK_STR(Classes(1), "ttt");
    }
}

static void TestPreprocessor(void)
{
    SyntaxSelectForFile("a.c");

    LoadOne("#include <stdio.h>");
    CHECK_STR(Classes(0), "pppppppp" "n" "sssssssss");

    LoadOne("#include \"a.h\"");
    CHECK_STR(Classes(0), "pppppppp" "n" "sssss");

    LoadOne("#define N 10");
    CHECK_STR(Classes(0), "ppppppp" "nnn" "dd");

    /* indented directives count, a # in the middle of a line does not */
    LoadOne("  #if X");
    CHECK_STR(Classes(0), "nn" "ppp" "nn");

    LoadOne("x # y");
    CHECK_STR(Classes(0), "nnnnn");
}

static void TestBlockComments(void)
{
    const char *lines[3] = {"int a; /* start", "still comment", "end */ int b;"};

    SyntaxSelectForFile("a.c");
    Load(lines, 3);

    CHECK_STR(Classes(0), "tttnnnn" "cccccccc");
    CHECK_STR(Classes(1), "ccccccccccccc");
    CHECK_STR(Classes(2), "cccccc" "n" "ttt" "nnn");

    /* a comment closed on its own row leaves the next one alone */
    {
        const char *closed[2] = {"/* a */ int", "int"};

        Load(closed, 2);
        CHECK_STR(Classes(0), "ccccccc" "n" "ttt");
        CHECK_STR(Classes(1), "ttt");
    }
}

static void TestRowsAskedOutOfOrder(void)
{
    const char *lines[3] = {"/* open", "inside", "*/ int"};

    SyntaxSelectForFile("a.c");
    Load(lines, 3);

    /* the last row first: its state has to be learned from the rows
       above it, which have not been drawn yet */
    CHECK_STR(Classes(2), "cc" "n" "ttt");
    CHECK_STR(Classes(1), "cccccc");
    CHECK_STR(Classes(0), "ccccccc");
}

static void TestInvalidation(void)
{
    const char *lines[2] = {"/* open", "int"};

    SyntaxSelectForFile("a.c");
    Load(lines, 2);

    CHECK_STR(Classes(1), "ccc");

    /* the comment is cut away after the fact: the row primitives report
       the change, so the next row is no longer inside a comment */
    RowRemoveChar(&vted_buffer.rows[0], 0);
    RowRemoveChar(&vted_buffer.rows[0], 0);
    CHECK_STR(Classes(1), "ttt");

    /* and a comment opened by typing swallows the rows after it */
    RowInsertChar(&vted_buffer.rows[0], 0, '/');
    RowInsertChar(&vted_buffer.rows[0], 1, '*');
    CHECK_STR(Classes(1), "ccc");
}

static void TestInsertingAndRemovingRows(void)
{
    const char *lines[2] = {"int a;", "int b;"};

    SyntaxSelectForFile("a.c");
    Load(lines, 2);
    CHECK_STR(Classes(1), "tttnnn");

    /* a row that opens a comment, put in between */
    InsertRow(1, "/*", 2, VTED_EOL_LF);
    CHECK_STR(Classes(1), "cc");
    CHECK_STR(Classes(2), "cccccc");

    /* and taken out again */
    RemoveRow(1);
    CHECK_STR(Classes(1), "tttnnn");

    /* cutting and replacing text count as edits as well */
    InsertRow(0, "/* x", 4, VTED_EOL_LF);
    CHECK_STR(Classes(1), "cccccc");
    RowTruncate(&vted_buffer.rows[0], 1);
    CHECK_STR(Classes(1), "tttnnn");
    RowSetText(&vted_buffer.rows[0], "/*", 2);
    CHECK_STR(Classes(1), "cccccc");
    RowAppendString(&vted_buffer.rows[0], "*/", 2);
    CHECK_STR(Classes(1), "tttnnn");
}

static void TestStyles(void)
{
    CHECK_STR(SyntaxStyle(HlNormal), "");
    CHECK(strlen(SyntaxStyle(HlComment)) > 0);
    CHECK(strlen(SyntaxStyle(HlString)) > 0);
    CHECK(strlen(SyntaxStyle(HlNumber)) > 0);
    CHECK(strlen(SyntaxStyle(HlKeyword)) > 0);
    CHECK(strlen(SyntaxStyle(HlType)) > 0);
    CHECK(strlen(SyntaxStyle(HlPreproc)) > 0);
}

int main(void)
{
    TestSelectingTheLanguage();
    TestPlainTextIsLeftAlone();
    TestKeywordsAndTypes();
    TestNumbers();
    TestLineComment();
    TestStrings();
    TestPreprocessor();
    TestBlockComments();
    TestRowsAskedOutOfOrder();
    TestInvalidation();
    TestInsertingAndRemovingRows();
    TestStyles();

    FreeBuffer();
    SyntaxFree();

    return TestSummary("syntax");
}

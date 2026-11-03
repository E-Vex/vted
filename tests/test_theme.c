/*===============================*/
#include <stdio.h>
#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_theme.h"
#include "src/vted_syntax.h"
/*===============================*/

static int Class(const char *name)
{
    return ThemeParseClass(name, strlen(name));
}

static void TestClassNames(void)
{
    CHECK_INT(Class("comment"), HlComment);
    CHECK_INT(Class("string"), HlString);
    CHECK_INT(Class("number"), HlNumber);
    CHECK_INT(Class("keyword"), HlKeyword);
    CHECK_INT(Class("type"), HlType);
    CHECK_INT(Class("preproc"), HlPreproc);
    CHECK_INT(Class("removed"), HlRemoved);
    CHECK_INT(Class("context"), HlContext);
    CHECK_INT(Class("function"), HlFunction);
    CHECK_INT(Class("match"), HlMatch);
    CHECK_INT(Class("tab"), HlTab);
    CHECK_INT(Class("indent"), HlIndent);
    CHECK_INT(Class("marker"), HlMarker);

    /* normal text and the bracket match are not named: a theme cannot
       color them */
    CHECK_INT(Class("normal"), -1);
    CHECK_INT(Class("bracket"), -1);

    /* whole lower-case words only */
    CHECK_INT(Class("Keyword"), -1);
    CHECK_INT(Class("keywords"), -1);
    CHECK_INT(Class("keyw"), -1);
    CHECK_INT(Class(""), -1);

    /* the length bounds the word */
    CHECK_INT(ThemeParseClass("keywordx", 7), HlKeyword);
    CHECK_INT(ThemeParseClass("keyword", 3), -1);
}

int main(void)
{
    TestClassNames();

    return TestSummary("theme");
}

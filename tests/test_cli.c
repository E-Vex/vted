/*===============================*/
#include <stdio.h>
#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_cli.h"
/*===============================*/

/* runs CliParse over an argument list the way main() receives it;
   argv[0] is the program name and the list ends with NULL */
static vted_cli_options_t ParseArgs(char *argv[])
{
    vted_cli_options_t options;
    int argc = 0;

    while (argv[argc] != NULL)
    {
        argc++;
    }

    CliParse(argc, argv, &options);
    return options;
}

static void TestNoArguments(void)
{
    char *argv[] = {"vted", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK(options.file == NULL);
    CHECK_INT(options.error, 0);
}

static void TestFileOperand(void)
{
    char *argv[] = {"vted", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_STR(options.file, "file.c");
    CHECK_INT(options.error, 0);
}

static void TestDashAloneIsAFile(void)
{
    char *argv[] = {"vted", "-", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_STR(options.file, "-");
    CHECK_INT(options.error, 0);
}

static void TestOnlyTheFirstOperandCounts(void)
{
    char *argv[] = {"vted", "first.c", "second.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_STR(options.file, "first.c");
    CHECK_INT(options.error, 0);
}

static void TestUnknownLongOptionIsRejected(void)
{
    char *argv[] = {"vted", "--help", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK(options.file == NULL);
    CHECK_INT(options.error, 1);
}

static void TestUnknownShortOptionIsRejected(void)
{
    char *argv[] = {"vted", "-x", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK(options.file == NULL);
    CHECK_INT(options.error, 1);
}

static void TestOptionAfterAFileIsRejected(void)
{
    char *argv[] = {"vted", "file.c", "--version", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

int main(void)
{
    TestNoArguments();
    TestFileOperand();
    TestDashAloneIsAFile();
    TestOnlyTheFirstOperandCounts();
    TestUnknownLongOptionIsRejected();
    TestUnknownShortOptionIsRejected();
    TestOptionAfterAFileIsRejected();
    return TestSummary("cli");
}

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

    CHECK_INT(options.file_count, 0);
    CHECK_INT(options.error, 0);
}

static void TestFileOperand(void)
{
    char *argv[] = {"vted", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 1);
    CHECK_STR(options.files[0], "file.c");
    CHECK_INT(options.error, 0);
}

static void TestDashAloneMeansStandardInput(void)
{
    char *argv[] = {"vted", "-", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 1);
    CHECK_STR(options.files[0], "-");
    CHECK_INT(options.error, 0);
}

static void TestAFileAndADashAreBothOperands(void)
{
    char *argv[] = {"vted", "file.c", "-", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 2);
    CHECK_STR(options.files[0], "file.c");
    CHECK_STR(options.files[1], "-");
    CHECK_INT(options.error, 0);
}

static void TestNoDashByDefault(void)
{
    char *argv[] = {"vted", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 1);
    CHECK_STR(options.files[0], "file.c");
}

static void TestEveryFileOperandCounts(void)
{
    char *argv[] = {"vted", "first.c", "second.c", "third.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 3);
    CHECK_STR(options.files[0], "first.c");
    CHECK_STR(options.files[1], "second.c");
    CHECK_STR(options.files[2], "third.c");
    CHECK_INT(options.error, 0);
}

static void TestTwoDashesAreRejected(void)
{
    char *argv[] = {"vted", "-", "-", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
    CHECK_INT(options.file_count, 0);
}

static void TestUnknownLongOptionIsRejected(void)
{
    char *argv[] = {"vted", "--no-such-option", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 0);
    CHECK_INT(options.error, 1);
}

static void TestUnknownShortOptionIsRejected(void)
{
    char *argv[] = {"vted", "-x", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 0);
    CHECK_INT(options.error, 1);
}

static void TestOptionAfterAFileIsRejected(void)
{
    char *argv[] = {"vted", "file.c", "-x", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

static void TestLongHelpAsksForTheUsage(void)
{
    char *argv[] = {"vted", "--help", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_help, 1);
    CHECK_INT(options.error, 0);
}

static void TestShortHelpAsksForTheUsage(void)
{
    char *argv[] = {"vted", "-h", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_help, 1);
    CHECK_INT(options.error, 0);
}

static void TestHelpWinsOverAFile(void)
{
    char *argv[] = {"vted", "-h", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_help, 1);
    CHECK_INT(options.file_count, 1);
    CHECK_STR(options.files[0], "file.c");
    CHECK_INT(options.error, 0);
}

static void TestNoArgumentsDoesNotAskForHelp(void)
{
    char *argv[] = {"vted", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_help, 0);
}

static void TestLongVersionAsksForTheVersion(void)
{
    char *argv[] = {"vted", "--version", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_version, 1);
    CHECK_INT(options.error, 0);
}

static void TestShortVersionAsksForTheVersion(void)
{
    char *argv[] = {"vted", "-V", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_version, 1);
    CHECK_INT(options.error, 0);
}

static void TestHelpAndVersionCanBeAskedTogether(void)
{
    char *argv[] = {"vted", "-h", "-V", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_help, 1);
    CHECK_INT(options.want_version, 1);
    CHECK_INT(options.error, 0);
}

static void TestNoArgumentsDoesNotAskForTheVersion(void)
{
    char *argv[] = {"vted", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.want_version, 0);
}

static void TestStartLineAlone(void)
{
    char *argv[] = {"vted", "+5", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.start_line, 5);
    CHECK_INT(options.error, 0);
}

static void TestStartLineAfterAFile(void)
{
    char *argv[] = {"vted", "file.c", "+12", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 1);
    CHECK_STR(options.files[0], "file.c");
    CHECK_INT(options.start_line, 12);
    CHECK_INT(options.error, 0);
}

static void TestStartLineBeforeAFile(void)
{
    char *argv[] = {"vted", "+12", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.file_count, 1);
    CHECK_STR(options.files[0], "file.c");
    CHECK_INT(options.start_line, 12);
    CHECK_INT(options.error, 0);
}

static void TestStartLineOfOneIsAllowed(void)
{
    char *argv[] = {"vted", "+1", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.start_line, 1);
    CHECK_INT(options.error, 0);
}

static void TestBarePlusIsRejected(void)
{
    char *argv[] = {"vted", "+", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

static void TestZeroStartLineIsRejected(void)
{
    char *argv[] = {"vted", "+0", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

static void TestWordAfterPlusIsRejected(void)
{
    char *argv[] = {"vted", "+abc", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

static void TestTrailingGarbageIsRejected(void)
{
    char *argv[] = {"vted", "+5x", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

static void TestHugeStartLineIsRejected(void)
{
    char *argv[] = {"vted", "+99999999999999999999", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

static void TestTwoStartLinesAreRejected(void)
{
    char *argv[] = {"vted", "+5", "+6", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.error, 1);
}

static void TestNoStartLineByDefault(void)
{
    char *argv[] = {"vted", "file.c", NULL};
    vted_cli_options_t options = ParseArgs(argv);

    CHECK_INT(options.start_line, 0);
}

int main(void)
{
    TestNoArguments();
    TestFileOperand();
    TestDashAloneMeansStandardInput();
    TestAFileAndADashAreBothOperands();
    TestNoDashByDefault();
    TestEveryFileOperandCounts();
    TestTwoDashesAreRejected();
    TestUnknownLongOptionIsRejected();
    TestUnknownShortOptionIsRejected();
    TestOptionAfterAFileIsRejected();
    TestLongHelpAsksForTheUsage();
    TestShortHelpAsksForTheUsage();
    TestHelpWinsOverAFile();
    TestNoArgumentsDoesNotAskForHelp();
    TestLongVersionAsksForTheVersion();
    TestShortVersionAsksForTheVersion();
    TestHelpAndVersionCanBeAskedTogether();
    TestNoArgumentsDoesNotAskForTheVersion();
    TestStartLineAlone();
    TestStartLineAfterAFile();
    TestStartLineBeforeAFile();
    TestStartLineOfOneIsAllowed();
    TestBarePlusIsRejected();
    TestZeroStartLineIsRejected();
    TestWordAfterPlusIsRejected();
    TestTrailingGarbageIsRejected();
    TestHugeStartLineIsRejected();
    TestTwoStartLinesAreRejected();
    TestNoStartLineByDefault();
    return TestSummary("cli");
}

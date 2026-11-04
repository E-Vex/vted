#ifndef VTED_TEST_H
#define VTED_TEST_H

/*==========================================================
|                    Unit Test Harness                     |
|                                                          |
| Just enough to write checks without a framework: every   |
| test file is its own program with a main() that runs     |
| its checks and returns TestSummary(). A failed check     |
| prints where it happened and the run exits non-zero.     |
==========================================================*/

#include <stdio.h>
#include <string.h>

static int vted_test_checks = 0;
static int vted_test_failures = 0;

#define CHECK(condition)                                                  \
    do                                                                    \
    {                                                                     \
        vted_test_checks++;                                               \
        if (!(condition))                                                 \
        {                                                                 \
            vted_test_failures++;                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #condition);                                          \
        }                                                                 \
    } while (0)

#define CHECK_INT(actual, expected)                                       \
    do                                                                    \
    {                                                                     \
        long actual_value = (long)(actual);                               \
        long expected_value = (long)(expected);                           \
        vted_test_checks++;                                               \
        if (actual_value != expected_value)                               \
        {                                                                 \
            vted_test_failures++;                                         \
            fprintf(stderr, "  FAIL %s:%d: %s is %ld, expected %ld\n",    \
                    __FILE__, __LINE__, #actual, actual_value,            \
                    expected_value);                                      \
        }                                                                 \
    } while (0)

#define CHECK_STR(actual, expected)                                       \
    do                                                                    \
    {                                                                     \
        const char *actual_text = (actual);                               \
        const char *expected_text = (expected);                           \
        vted_test_checks++;                                               \
        if (strcmp(actual_text, expected_text) != 0)                      \
        {                                                                 \
            vted_test_failures++;                                         \
            fprintf(stderr, "  FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", \
                    __FILE__, __LINE__, #actual, actual_text,             \
                    expected_text);                                       \
        }                                                                 \
    } while (0)

static int TestSummary(const char *suite)
{
    printf("  %s: %d checks, %d failed\n", suite, vted_test_checks,
           vted_test_failures);
    return vted_test_failures == 0 ? 0 : 1;
}

#endif

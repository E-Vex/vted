/*===============================*/
#include <stdio.h>
#include <string.h>
/*===============================*/
#include "tests/vted_test.h"
#include "src/vted_color.h"
/*===============================*/

/* parses a whole string, the way the config and theme readers hand
   over a value; gives back -1 when it is no color */
static int Parse(const char *text, vted_rgb_t *color)
{
    return ColorParse(text, strlen(text), color);
}

static void TestHex(void)
{
    vted_rgb_t color = {1, 2, 3};

    CHECK_INT(Parse("#ff8800", &color), 0);
    CHECK_INT(color.red, 255);
    CHECK_INT(color.green, 136);
    CHECK_INT(color.blue, 0);

    /* either case */
    CHECK_INT(Parse("#AbCdEf", &color), 0);
    CHECK_INT(color.red, 0xab);
    CHECK_INT(color.green, 0xcd);
    CHECK_INT(color.blue, 0xef);

    /* the short spelling doubles every digit */
    CHECK_INT(Parse("#f80", &color), 0);
    CHECK_INT(color.red, 255);
    CHECK_INT(color.green, 136);
    CHECK_INT(color.blue, 0);

    CHECK_INT(Parse("#000", &color), 0);
    CHECK_INT(color.red, 0);
    CHECK_INT(color.blue, 0);

    /* anything else is no color, and the answer is left alone */
    color.red = 9;
    color.green = 9;
    color.blue = 9;
    CHECK_INT(Parse("#", &color), -1);
    CHECK_INT(Parse("#ff", &color), -1);
    CHECK_INT(Parse("#ff88", &color), -1);
    CHECK_INT(Parse("#ff88000", &color), -1);
    CHECK_INT(Parse("#gg0000", &color), -1);
    CHECK_INT(Parse("ff8800", &color), -1);
    CHECK_INT(Parse("", &color), -1);
    CHECK_INT(color.red, 9);
    CHECK_INT(color.green, 9);
    CHECK_INT(color.blue, 9);

    /* the length bounds the read: the bytes past it do not count */
    CHECK_INT(ColorParse("#ff8800zzz", 7, &color), 0);
    CHECK_INT(color.red, 255);
    CHECK_INT(ColorParse("#ff8800", 5, &color), -1);
    /* ...and four bytes of it happen to be the short spelling */
    CHECK_INT(ColorParse("#ff8800", 4, &color), 0);
    CHECK_INT(color.green, 255);
}

int main(void)
{
    TestHex();

    return TestSummary("color");
}

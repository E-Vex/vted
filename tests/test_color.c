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

static void TestNames(void)
{
    vted_rgb_t color = {1, 2, 3};

    CHECK_INT(Parse("black", &color), 0);
    CHECK_INT(color.red + color.green + color.blue, 0);

    CHECK_INT(Parse("red", &color), 0);
    CHECK_INT(color.red, 204);
    CHECK_INT(color.green, 0);
    CHECK_INT(color.blue, 0);

    CHECK_INT(Parse("orange", &color), 0);
    CHECK_INT(color.red, 255);
    CHECK_INT(color.green, 140);

    /* the two spellings of gray are the same color */
    vted_rgb_t grey;

    CHECK_INT(Parse("gray", &color), 0);
    CHECK_INT(Parse("grey", &grey), 0);
    CHECK_INT(memcmp(&color, &grey, sizeof(color)), 0);

    /* every name answers, and a word that is none does not; names are
       lower case and whole, and the answer is left alone */
    static const char *const all[] = {
        "black", "red", "green", "yellow", "blue", "magenta", "cyan",
        "white", "gray", "grey", "orange"
    };
    size_t i;

    for (i = 0; i < sizeof(all) / sizeof(all[0]); i++)
    {
        CHECK_INT(Parse(all[i], &color), 0);
    }

    color.red = 7;
    CHECK_INT(Parse("Red", &color), -1);
    CHECK_INT(Parse("purple", &color), -1);
    CHECK_INT(Parse("redd", &color), -1);
    CHECK_INT(Parse("re", &color), -1);
    CHECK_INT(Parse(" red", &color), -1);
    CHECK_INT(color.red, 7);

    /* the length bounds the name as it bounds a hex color */
    CHECK_INT(ColorParse("redish", 3, &color), 0);
    CHECK_INT(color.red, 204);
}

static int Same(vted_rgb_t color, int red, int green, int blue)
{
    return color.red == red && color.green == green && color.blue == blue;
}

static void TestPalette(void)
{
    vted_rgb_t color = {1, 2, 3};

    /* the palette as xterm lays it out: its own sixteen, a cube, grays */
    CHECK(Same(ColorFromPalette(0), 0, 0, 0));
    CHECK(Same(ColorFromPalette(1), 205, 0, 0));
    CHECK(Same(ColorFromPalette(15), 255, 255, 255));

    CHECK(Same(ColorFromPalette(16), 0, 0, 0));
    CHECK(Same(ColorFromPalette(17), 0, 0, 95));
    CHECK(Same(ColorFromPalette(21), 0, 0, 255));
    CHECK(Same(ColorFromPalette(46), 0, 255, 0));
    CHECK(Same(ColorFromPalette(196), 255, 0, 0));
    CHECK(Same(ColorFromPalette(208), 255, 135, 0));
    CHECK(Same(ColorFromPalette(231), 255, 255, 255));

    CHECK(Same(ColorFromPalette(232), 8, 8, 8));
    CHECK(Same(ColorFromPalette(245), 138, 138, 138));
    CHECK(Same(ColorFromPalette(255), 238, 238, 238));

    /* an index out of range is the nearest end of the palette */
    CHECK(Same(ColorFromPalette(-5), 0, 0, 0));
    CHECK(Same(ColorFromPalette(999), 238, 238, 238));

    /* a number in a file names a palette color */
    CHECK_INT(Parse("208", &color), 0);
    CHECK(Same(color, 255, 135, 0));
    CHECK_INT(Parse("0", &color), 0);
    CHECK(Same(color, 0, 0, 0));
    CHECK_INT(Parse("255", &color), 0);
    CHECK(Same(color, 238, 238, 238));
    CHECK_INT(Parse("007", &color), 0);
    CHECK(Same(color, 229, 229, 229));

    /* past the palette, signed, or not only digits: no color, and the
       answer is left alone */
    CHECK_INT(Parse("256", &color), -1);
    CHECK_INT(Parse("1000", &color), -1);
    CHECK_INT(Parse("-1", &color), -1);
    CHECK_INT(Parse("+5", &color), -1);
    CHECK_INT(Parse("12x", &color), -1);
    CHECK_INT(Parse("1 2", &color), -1);
    CHECK(Same(color, 229, 229, 229));
}

static void TestNearest256(void)
{
    vted_rgb_t color;

    /* a color the cube holds exactly is found at its own index */
    color = ColorFromPalette(208);
    CHECK_INT(ColorNearest256(color), 208);
    color = ColorFromPalette(21);
    CHECK_INT(ColorNearest256(color), 21);

    color.red = 255;
    color.green = 0;
    color.blue = 0;
    CHECK_INT(ColorNearest256(color), 196);

    color.red = 0;
    color.green = 0;
    color.blue = 0;
    CHECK_INT(ColorNearest256(color), 16);

    color.red = 255;
    color.green = 255;
    color.blue = 255;
    CHECK_INT(ColorNearest256(color), 231);

    /* a gray the ramp holds is found there, where the cube is further
       off than the ramp is */
    color.red = 128;
    color.green = 128;
    color.blue = 128;
    CHECK_INT(ColorNearest256(color), 244);

    /* a color between two levels goes to the nearer one: 100 is
       closer to 95 than to 135 */
    color.red = 100;
    color.green = 0;
    color.blue = 0;
    CHECK_INT(ColorNearest256(color), 52);

    /* the answer is always a color of the cube or the ramp, whatever
       the input: never one of the terminal's own 16 */
    int red;
    int green;
    int blue;

    for (red = 0; red < 256; red += 51)
    {
        for (green = 0; green < 256; green += 51)
        {
            for (blue = 0; blue < 256; blue += 51)
            {
                color.red = (unsigned char)red;
                color.green = (unsigned char)green;
                color.blue = (unsigned char)blue;
                int index = ColorNearest256(color);

                CHECK(index >= 16 && index <= 255);
            }
        }
    }
}

static void TestNearest16(void)
{
    vted_rgb_t color;
    int index;

    /* each of the sixteen is its own nearest */
    for (index = 0; index < 16; index++)
    {
        color = ColorFromPalette(index);
        CHECK_INT(ColorNearest16(color), index);
    }

    /* a color between them goes to the nearer */
    color.red = 200;
    color.green = 10;
    color.blue = 10;
    CHECK_INT(ColorNearest16(color), 1);

    color.red = 250;
    color.green = 20;
    color.blue = 20;
    CHECK_INT(ColorNearest16(color), 9);

    color.red = 20;
    color.green = 20;
    color.blue = 20;
    CHECK_INT(ColorNearest16(color), 0);

    color.red = 130;
    color.green = 130;
    color.blue = 130;
    CHECK_INT(ColorNearest16(color), 8);

    /* a dull yellow is yellow, not red or gray */
    color.red = 170;
    color.green = 130;
    color.blue = 0;
    CHECK_INT(ColorNearest16(color), 3);
}

static void TestEscape(void)
{
    char escape[24];
    vted_rgb_t orange = {255, 135, 0};
    vted_rgb_t red = {205, 0, 0};
    vted_rgb_t bright_red = {255, 0, 0};

    /* truecolor spells the channels out, for the text and behind it */
    ColorEscape(escape, sizeof(escape), orange, 0, 2);
    CHECK_STR(escape, "\x1b[38;2;255;135;0m");
    ColorEscape(escape, sizeof(escape), orange, 1, 2);
    CHECK_STR(escape, "\x1b[48;2;255;135;0m");

    /* 256 colors name the nearest palette entry */
    ColorEscape(escape, sizeof(escape), orange, 0, 1);
    CHECK_STR(escape, "\x1b[38;5;208m");
    ColorEscape(escape, sizeof(escape), orange, 1, 1);
    CHECK_STR(escape, "\x1b[48;5;208m");

    /* 16 colors: the normal ones are 30 to 37, the bright 90 to 97 */
    ColorEscape(escape, sizeof(escape), red, 0, 0);
    CHECK_STR(escape, "\x1b[31m");
    ColorEscape(escape, sizeof(escape), bright_red, 0, 0);
    CHECK_STR(escape, "\x1b[91m");

    /* ...and behind the text 40 to 47, and 100 to 107 */
    ColorEscape(escape, sizeof(escape), red, 1, 0);
    CHECK_STR(escape, "\x1b[41m");
    ColorEscape(escape, sizeof(escape), bright_red, 1, 0);
    CHECK_STR(escape, "\x1b[101m");

    /* the widest escape fits the buffer the header promises */
    vted_rgb_t widest = {255, 255, 255};

    ColorEscape(escape, sizeof(escape), widest, 1, 2);
    CHECK_STR(escape, "\x1b[48;2;255;255;255m");

    /* a buffer too small gets nothing rather than half an escape */
    char small[8];

    memset(small, 'x', sizeof(small));
    ColorEscape(small, sizeof(small), widest, 1, 2);
    CHECK_STR(small, "");
}

int main(void)
{
    TestHex();
    TestNames();
    TestPalette();
    TestNearest256();
    TestNearest16();
    TestEscape();

    return TestSummary("color");
}

/*===============================*/
#include <string.h>
/*===============================*/
#include "vted_color.h"
/*===============================*/

/* the value of a hexadecimal digit, or -1 when the byte is none */
static int HexValue(char digit)
{
    if (digit >= '0' && digit <= '9')
    {
        return digit - '0';
    }

    if (digit >= 'a' && digit <= 'f')
    {
        return digit - 'a' + 10;
    }

    if (digit >= 'A' && digit <= 'F')
    {
        return digit - 'A' + 10;
    }

    return -1;
}

/* reads "#rrggbb" or "#rgb"; a short digit is doubled, so #f80 is
   #ff8800 */
static int ParseHex(const char *text, size_t len, vted_rgb_t *color)
{
    int digits[6];
    size_t count = len - 1; /* the digits after the # */
    size_t i;

    if (count != 3 && count != 6)
    {
        return -1;
    }

    for (i = 0; i < count; i++)
    {
        digits[i] = HexValue(text[i + 1]);

        if (digits[i] < 0)
        {
            return -1;
        }
    }

    if (count == 3)
    {
        color->red = (unsigned char)(digits[0] * 17);
        color->green = (unsigned char)(digits[1] * 17);
        color->blue = (unsigned char)(digits[2] * 17);
    }
    else
    {
        color->red = (unsigned char)(digits[0] * 16 + digits[1]);
        color->green = (unsigned char)(digits[2] * 16 + digits[3]);
        color->blue = (unsigned char)(digits[4] * 16 + digits[5]);
    }

    return 0;
}

/* the 16 colors every terminal has, in the shades xterm gives them;
   a terminal may draw them otherwise, which is why a color a person
   chose is only ever matched to the 256-color cube and its grays when
   the terminal has them */
static const vted_rgb_t standard_colors[16] = {
    {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
    {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
    {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255}
};

/* the level of one axis of the 6x6x6 cube: 0, then 95 up in steps of 40 */
static unsigned char CubeLevel(int step)
{
    return (unsigned char)(step == 0 ? 0 : 55 + 40 * step);
}

vted_rgb_t ColorFromPalette(int index)
{
    vted_rgb_t color;

    if (index < 0)
    {
        index = 0;
    }

    if (index > 255)
    {
        index = 255;
    }

    if (index < 16)
    {
        return standard_colors[index];
    }

    if (index < 232)
    {
        int cell = index - 16;

        color.red = CubeLevel(cell / 36);
        color.green = CubeLevel((cell / 6) % 6);
        color.blue = CubeLevel(cell % 6);
        return color;
    }

    /* the grays start at 8 and climb by 10, never reaching black or white */
    color.red = (unsigned char)(8 + 10 * (index - 232));
    color.green = color.red;
    color.blue = color.red;
    return color;
}

/* the squared distance between two colors, in RGB space */
static int Distance(vted_rgb_t a, vted_rgb_t b)
{
    int red = a.red - b.red;
    int green = a.green - b.green;
    int blue = a.blue - b.blue;

    return red * red + green * green + blue * blue;
}

/* the step of the cube axis whose level lies nearest to the value:
   the levels are 0, 95, 135, 175, 215 and 255, so the boundaries
   fall at 48, 115, 155, 195 and 235 */
static int NearestCubeStep(int value)
{
    if (value < 48)
    {
        return 0;
    }

    if (value < 115)
    {
        return 1;
    }

    return (value - 35) / 40;
}

int ColorNearest256(vted_rgb_t color)
{
    int cube = 16 + 36 * NearestCubeStep(color.red) +
               6 * NearestCubeStep(color.green) + NearestCubeStep(color.blue);

    /* the ramp of grays holds the color nearest to the average of the
       three channels */
    int average = (color.red + color.green + color.blue) / 3;
    int ramp = (average - 8 + 5) / 10;

    if (ramp < 0)
    {
        ramp = 0;
    }

    if (ramp > 23)
    {
        ramp = 23;
    }

    int gray = 232 + ramp;

    return Distance(color, ColorFromPalette(gray)) <
                   Distance(color, ColorFromPalette(cube))
               ? gray
               : cube;
}

int ColorNearest16(vted_rgb_t color)
{
    int best = 0;
    int best_distance = Distance(color, standard_colors[0]);
    int i;

    for (i = 1; i < 16; i++)
    {
        int distance = Distance(color, standard_colors[i]);

        if (distance < best_distance)
        {
            best = i;
            best_distance = distance;
        }
    }

    return best;
}

/* reads a decimal number of up to three digits as a palette index */
static int ParsePalette(const char *text, size_t len, vted_rgb_t *color)
{
    int index = 0;
    size_t i;

    if (len == 0 || len > 3)
    {
        return -1;
    }

    for (i = 0; i < len; i++)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return -1;
        }

        index = index * 10 + (text[i] - '0');
    }

    if (index > 255)
    {
        return -1;
    }

    *color = ColorFromPalette(index);
    return 0;
}

/* the names a color goes by: the words the bracket-color setting
   knows, and the black, white and orange a scheme cannot do without.
   Each is the RGB the syntax module already draws that name with */
static const struct
{
    const char *name;
    vted_rgb_t color;
} color_names[] = {
    {"black", {0, 0, 0}},
    {"red", {204, 0, 0}},
    {"green", {0, 170, 0}},
    {"yellow", {170, 130, 0}},
    {"blue", {70, 100, 200}},
    {"magenta", {170, 0, 170}},
    {"cyan", {0, 150, 150}},
    {"white", {229, 229, 229}},
    {"gray", {128, 128, 128}},
    {"grey", {128, 128, 128}},
    {"orange", {255, 140, 0}}
};

static int ParseName(const char *text, size_t len, vted_rgb_t *color)
{
    size_t i;

    for (i = 0; i < sizeof(color_names) / sizeof(color_names[0]); i++)
    {
        if (strlen(color_names[i].name) == len &&
            memcmp(color_names[i].name, text, len) == 0)
        {
            *color = color_names[i].color;
            return 0;
        }
    }

    return -1;
}

int ColorParse(const char *text, size_t len, vted_rgb_t *color)
{
    if (len > 0 && text[0] == '#')
    {
        return ParseHex(text, len, color);
    }

    if (len > 0 && text[0] >= '0' && text[0] <= '9')
    {
        return ParsePalette(text, len, color);
    }

    return ParseName(text, len, color);
}

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

    return ParseName(text, len, color);
}

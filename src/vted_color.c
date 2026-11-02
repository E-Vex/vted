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

int ColorParse(const char *text, size_t len, vted_rgb_t *color)
{
    if (len > 0 && text[0] == '#')
    {
        return ParseHex(text, len, color);
    }

    return -1;
}

/*===============================*/
#include <unistd.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
/*===============================*/

#define ESC '\x1B'
/*===============
| Name:    ESC  |
| Decimal: 27   |
| Hex:     0x1B |
| Octal:   033  |
================*/
char ReadRawByte()
{
    char user_input;
    char read_return;

    while ((read_return = read(STDIN_FILENO, &user_input, 1)) != 1)
    {
        if (read_return == -1)
        {
            KillApp("read");
        }
    }

    return user_input;
}
char ParseArrowKey()
{
    /*=================================================
    | An arrow key is not a single-byte character.    |
    | It is represented by a 3-byte escape sequence:  |
    |  [0x1B] [0x5B] [0x41-0x44]                      |
    |                                                 |
    | The first byte is always ESC (0x1B).            |
    | The second byte is always '[' (0x5B).           |
    | The third byte identifies the arrow:            |
    |   0x41 = Up                                     |
    |   0x42 = Down                                   |
    |   0x43 = Right                                  |
    |   0x44 = Left                                   |
    =================================================*/

    char sequence_input[2];

    if (read(STDIN_FILENO, &sequence_input[0], 1) != 1)
        return '\x1b';
    if (read(STDIN_FILENO, &sequence_input[1], 1) != 1)
        return '\x1b';

    if (sequence_input[0] == '[')
    {
        switch (sequence_input[1])
        {
        case 0x41:
            return ArrowUp;
        case 0x42:
            return ArrowDown;
        case 0x43:
            return ArrowRight;
        case 0x44:
            return ArrowLeft;
        }
    }
    return '\x1B';
}

char ReadKeyPress()
{
    char user_input = ReadRawByte();

    if (user_input == ESC)
    {
        user_input = ParseArrowKey(user_input);
    }

    return user_input;
}

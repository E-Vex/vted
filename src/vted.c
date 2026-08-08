/*===============================*/
#include <stdio.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
/*===============================*/

int main()
{

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();

    while (1)
    {
        char c_in = ReadKeyPress();
        if (c_in == CTRL_KEY('q') || c_in == CTRL_KEY('Q'))
        {
            return 0;
        }
    }

    return 0;
}

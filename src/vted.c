/*===============================*/
#include <stdio.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
/*===============================*/

int main()
{
    ClearScreen();

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();
    InitVtedEditor();
    EnterToAlternateScreenBuffer();

    while (1)
    {
        char c_in = ReadKeyPress();
        if (c_in == CTRL_KEY('q') || c_in == CTRL_KEY('Q'))
        {
            ExitAlternateScreenBuffer();
            return 0;
        }

        RefreshScreen();
    }

    return 0;
}

/*===============================*/
#include <stdio.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
/*===============================*/

int main()
{

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();
    InitVtedEditor();

    while (1)
    {
        char c_in = ReadKeyPress();
        if (c_in == CTRL_KEY('q') || c_in == CTRL_KEY('Q'))
        {
            RefreshScreen();
            return 0;
        }

        RefreshScreen();
    }

    return 0;
}

/*===============================*/
#include <stdio.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_keyboard.h"
#include "vted_screen.h"
#include "vted_row.h"
#include "vted_file.h"
/*===============================*/

int main(int argc, char *argv[])
{
    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();
    EnterToAlternateScreenBuffer();
    InitBuffer();

    if (argc >= 2)
    {
        if (OpenFile(argv[1]) == -1)
        {
            KillApp("OpenFile");
        }
    }

    InitVtedEditor();

    while (1)
    {
        int c_in = ReadKeyPress();
        if (c_in == CTRL_KEY('q') || c_in == CTRL_KEY('Q'))
        {
            ExitAlternateScreenBuffer();
            FreeBuffer();
            return 0;
        }

        if (c_in == ArrowUp || c_in == ArrowDown || c_in == ArrowLeft || c_in == ArrowRight ||
            c_in == PageUp || c_in == PageDown || c_in == HomeKey || c_in == EndKey)
        {
            MoveCursor(c_in);
        }
        RefreshScreen();
    }

    return 0;
}
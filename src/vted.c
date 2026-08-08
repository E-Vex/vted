/*===============================*/
#include <stdio.h>
/*===============================*/
#include "vted_terminal.h"
/*===============================*/

int main()
{

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();

    char c_in;
    while (1)
    {
        scanf("%c", &c_in);
        if (c_in == '0')
        {
            return 1;
        }
    }

    return 0;
}

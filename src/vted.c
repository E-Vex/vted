#include <stdio.h>
#include "vted_terminal.h"

int main()
{

    SaveTerminalCurrentSettings();
    EnableTerminalRawMode();

    return 0;
}

/*===============================*/
#include <string.h>
/*===============================*/
#include "vted_theme.h"
#include "vted_syntax.h"
/*===============================*/

/* the words a theme file names the classes of text by */
static const struct
{
    const char *name;
    int hl_class;
} class_names[] = {
    {"comment", HlComment},
    {"string", HlString},
    {"number", HlNumber},
    {"keyword", HlKeyword},
    {"type", HlType},
    {"preproc", HlPreproc},
    {"removed", HlRemoved},
    {"context", HlContext},
    {"function", HlFunction},
    {"match", HlMatch},
    {"tab", HlTab},
    {"indent", HlIndent},
    {"marker", HlMarker}
};

int ThemeParseClass(const char *name, size_t len)
{
    size_t i;

    for (i = 0; i < sizeof(class_names) / sizeof(class_names[0]); i++)
    {
        if (strlen(class_names[i].name) == len &&
            memcmp(class_names[i].name, name, len) == 0)
        {
            return class_names[i].hl_class;
        }
    }

    return -1;
}

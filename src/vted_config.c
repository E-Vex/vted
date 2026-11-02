/*===============================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_config.h"
#include "vted_row.h"
/*===============================*/

vted_settings_t SettingsDefaults(void)
{
    vted_settings_t settings;

    settings.tab_width = VTED_TAB_STOP;
    settings.insert_spaces = 0;
    settings.show_line_numbers = 1;
    settings.show_tabs = 0;
    settings.wrap = 0;
    settings.wrap_column = 0;
    settings.wrap_indent = 1;
    settings.indent_guides = 0;

    return settings;
}

/* is the len bytes at text exactly the given word? */
static int TextIs(const char *text, size_t len, const char *name)
{
    size_t name_len = strlen(name);

    return len == name_len && memcmp(text, name, len) == 0;
}

/* reads the value of a boolean key; 1 for on, 0 for off, -1 when the
   word is neither */
static int ParseOnOff(const char *value, size_t len)
{
    if (TextIs(value, len, "on"))
    {
        return 1;
    }

    if (TextIs(value, len, "off"))
    {
        return 0;
    }

    return -1;
}

int SettingsLoad(vted_settings_t *settings, FILE *file)
{
    char line[256];
    int bad = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        const char *at = line;

        while (*at == ' ' || *at == '\t')
        {
            at++;
        }

        /* blank lines and comments are nothing to read */
        if (*at == '\0' || *at == '\n' || *at == '#')
        {
            continue;
        }

        /* every other line names a setting: set key value */
        if (strncmp(at, "set", 3) != 0 || (at[3] != ' ' && at[3] != '\t'))
        {
            bad++;
            continue;
        }

        at += 3;

        while (*at == ' ' || *at == '\t')
        {
            at++;
        }

        const char *key = at;

        while (*at != '\0' && *at != ' ' && *at != '\t' && *at != '\n')
        {
            at++;
        }

        size_t key_len = (size_t)(at - key);

        while (*at == ' ' || *at == '\t')
        {
            at++;
        }

        /* the value is the rest of the line, without trailing blanks
           or the carriage return of a Windows line ending */
        const char *value = at;

        while (*at != '\0' && *at != '\n')
        {
            at++;
        }

        while (at > value && (at[-1] == ' ' || at[-1] == '\t' || at[-1] == '\r'))
        {
            at--;
        }

        size_t value_len = (size_t)(at - value);

        if (TextIs(key, key_len, "tab-width"))
        {
            /* the width is a number in the range the Ctrl-W prompt takes */
            if (value_len == 0 || value_len > 8)
            {
                bad++;
                continue;
            }

            char number[9];
            char *end;
            long width;

            memcpy(number, value, value_len);
            number[value_len] = '\0';

            width = strtol(number, &end, 10);

            if (*end != '\0' || width < 1 || width > 32)
            {
                bad++;
            }
            else
            {
                settings->tab_width = (int)width;
            }
        }
        else if (TextIs(key, key_len, "insert-spaces"))
        {
            int on_off = ParseOnOff(value, value_len);

            if (on_off == -1)
            {
                bad++;
            }
            else
            {
                settings->insert_spaces = on_off;
            }
        }
        else if (TextIs(key, key_len, "line-numbers"))
        {
            int on_off = ParseOnOff(value, value_len);

            if (on_off == -1)
            {
                bad++;
            }
            else
            {
                settings->show_line_numbers = on_off;
            }
        }
        else if (TextIs(key, key_len, "show-tabs"))
        {
            int on_off = ParseOnOff(value, value_len);

            if (on_off == -1)
            {
                bad++;
            }
            else
            {
                settings->show_tabs = on_off;
            }
        }
        else if (TextIs(key, key_len, "wrap-column"))
        {
            /* the column is a number in the range a terminal is wide
               enough to show; 0 hides the marker */
            if (value_len == 0 || value_len > 8)
            {
                bad++;
                continue;
            }

            char number[9];
            char *end;
            long column;

            memcpy(number, value, value_len);
            number[value_len] = '\0';

            column = strtol(number, &end, 10);

            if (*end != '\0' || column < 0 || column > 512)
            {
                bad++;
            }
            else
            {
                settings->wrap_column = (int)column;
            }
        }
        else if (TextIs(key, key_len, "wrap"))
        {
            int on_off = ParseOnOff(value, value_len);

            if (on_off == -1)
            {
                bad++;
            }
            else
            {
                settings->wrap = on_off;
            }
        }
        else if (TextIs(key, key_len, "wrap-indent"))
        {
            int on_off = ParseOnOff(value, value_len);

            if (on_off == -1)
            {
                bad++;
            }
            else
            {
                settings->wrap_indent = on_off;
            }
        }
        else if (TextIs(key, key_len, "indent-guides"))
        {
            int on_off = ParseOnOff(value, value_len);

            if (on_off == -1)
            {
                bad++;
            }
            else
            {
                settings->indent_guides = on_off;
            }
        }
        else
        {
            bad++; /* a key the editor does not know */
        }
    }

    return bad;
}

const char *SettingsPath(void)
{
    static char path[512];

    /* the desktop convention first: XDG_CONFIG_HOME/vted/vtedrc */
    const char *base = getenv("XDG_CONFIG_HOME");

    if (base != NULL && base[0] != '\0')
    {
        snprintf(path, sizeof(path), "%s/vted/vtedrc", base);
        return path;
    }

    /* without it, ~/.config stands in for the config home */
    const char *home = getenv("HOME");

    if (home != NULL && home[0] != '\0')
    {
        snprintf(path, sizeof(path), "%s/.config/vted/vtedrc", home);
        return path;
    }

    return NULL;
}

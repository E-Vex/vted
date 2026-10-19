/*===============================*/
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
/*===============================*/
#include "vted_terminal.h"
#include "vted_row.h"
#include "vted_syntax.h"
/*===============================*/

/* what a language needs to be highlighted: the lists are NULL-terminated */
typedef struct
{
    const char *name;
    const char *const *extensions;  /* ".c", or a whole file name like "Makefile" */
    const char *const *keywords;    /* control flow */
    const char *const *types;       /* types and declaration words */
    const char *const *constants;   /* NULL, true, ... drawn like numbers */
    const char *line_comment;       /* opens a comment up to the end of the row, or NULL */
    const char *block_start;        /* opens a comment that may span rows, or NULL */
    const char *block_end;          /* closes it */
    const char *quotes;             /* characters that open a string */
    int preprocessor;               /* 1: '#' directives at the start of a line */
} vted_language_t;

/*----------------------------------------------------------
| C
----------------------------------------------------------*/

static const char *const c_extensions[] = {".c", ".h", NULL};

static const char *const c_keywords[] = {
    "break", "case", "continue", "default", "do", "else", "for", "goto",
    "if", "return", "sizeof", "switch", "while", NULL};

static const char *const c_types[] = {
    "auto", "char", "const", "double", "enum", "extern", "float", "inline",
    "int", "long", "register", "restrict", "short", "signed", "static",
    "struct", "typedef", "union", "unsigned", "void", "volatile",
    "_Bool", "_Alignas", "_Alignof", "_Atomic", "_Noreturn",
    "bool", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "FILE", NULL};

static const char *const c_constants[] = {
    "NULL", "true", "false", "EOF", "stdin", "stdout", "stderr", NULL};

static const vted_language_t language_c = {
    "C", c_extensions, c_keywords, c_types, c_constants,
    "//", "/*", "*/", "\"'", 1};

/*----------------------------------------------------------
| the table
----------------------------------------------------------*/

static const vted_language_t *const languages[] = {
    &language_c,
    NULL};

static const vted_language_t *current_language;

/* a block comment is the only thing that carries over to the next row */
enum
{
    StateNormal = 0,
    StateBlockComment
};

/* the state each row ends in; rows [0, valid_rows) are up to date */
static int *state_after;
static int state_capacity;
static int valid_rows;

/* scratch space for scanning rows whose colors nobody asked for */
static unsigned char *scratch;
static int scratch_capacity;

/*----------------------------------------------------------
| choosing the language
----------------------------------------------------------*/

static int MatchesName(const vted_language_t *language, const char *base, const char *ext)
{
    int i;

    for (i = 0; language->extensions[i] != NULL; i++)
    {
        const char *candidate = language->extensions[i];

        if (strcmp(candidate, base) == 0)
        {
            return 1; /* a whole file name, like Makefile */
        }

        if (candidate[0] == '.' && ext != NULL && strcmp(candidate, ext) == 0)
        {
            return 1;
        }
    }

    return 0;
}

void SyntaxSelectForFile(const char *filename)
{
    const char *base;
    const char *ext;
    int i;

    current_language = NULL;
    valid_rows = 0;

    if (filename == NULL)
    {
        return;
    }

    base = strrchr(filename, '/');
    base = base != NULL ? base + 1 : filename;

    /* a leading dot alone (".bashrc") is a hidden file, not an extension */
    ext = strrchr(base, '.');
    if (ext == base)
    {
        ext = NULL;
    }

    for (i = 0; languages[i] != NULL; i++)
    {
        if (MatchesName(languages[i], base, ext))
        {
            current_language = languages[i];
            return;
        }
    }
}

const char *SyntaxLanguageName(void)
{
    return current_language != NULL ? current_language->name : NULL;
}

/*----------------------------------------------------------
| scanning one row
----------------------------------------------------------*/

static int IsSeparator(int c)
{
    return c == '\0' || isspace(c) || strchr(",.()+-/*=~%<>[];:{}&|!?^@#\\\"'`", c) != NULL;
}

static int IsWordByte(int c)
{
    return isalnum(c) || c == '_';
}

static int StartsWith(const vted_row_t *row, int at, const char *prefix)
{
    size_t length = strlen(prefix);

    return (size_t)(row->size - at) >= length && memcmp(&row->chars[at], prefix, length) == 0;
}

static int InList(const char *const *list, const char *word, int length)
{
    int i;

    if (list == NULL)
    {
        return 0;
    }

    for (i = 0; list[i] != NULL; i++)
    {
        if (strlen(list[i]) == (size_t)length && memcmp(list[i], word, (size_t)length) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static void Mark(unsigned char *hl, int from, int to, HlClass hl_class)
{
    while (from < to)
    {
        hl[from++] = (unsigned char)hl_class;
    }
}

/* a '#' directive: the '#' and the word after it are the directive, and
   the <header> of an include is a string. Returns the index after it */
static int ScanDirective(const vted_row_t *row, int at, unsigned char *hl)
{
    int word = at + 1;
    int end;

    while (word < row->size && (row->chars[word] == ' ' || row->chars[word] == '\t'))
    {
        word++;
    }

    end = word;
    while (end < row->size && isalpha((unsigned char)row->chars[end]))
    {
        end++;
    }

    Mark(hl, at, end, HlPreproc);

    if (end - word == 7 && memcmp(&row->chars[word], "include", 7) == 0)
    {
        int open = end;

        while (open < row->size && (row->chars[open] == ' ' || row->chars[open] == '\t'))
        {
            open++;
        }

        if (open < row->size && row->chars[open] == '<')
        {
            int close = open + 1;

            while (close < row->size && row->chars[close] != '>')
            {
                close++;
            }

            if (close < row->size)
            {
                Mark(hl, open, close + 1, HlString);
                return close + 1;
            }
        }
    }

    return end;
}

/* classifies every byte of the row, starting in the given state, and
   returns the state the row ends in */
static int HighlightRow(const vted_language_t *language, const vted_row_t *row,
                        int state, unsigned char *hl)
{
    int first_word = RowIndentLength(row, row->size);
    int prev_separator = 1;
    int quote = 0;
    int i = 0;

    memset(hl, HlNormal, (size_t)row->size);

    while (i < row->size)
    {
        int c = (unsigned char)row->chars[i];

        if (state == StateBlockComment)
        {
            hl[i] = HlComment;

            if (StartsWith(row, i, language->block_end))
            {
                int length = (int)strlen(language->block_end);

                Mark(hl, i, i + length, HlComment);
                i += length;
                state = StateNormal;
                prev_separator = 1;
            }
            else
            {
                i++;
            }
            continue;
        }

        if (quote != 0)
        {
            hl[i] = HlString;

            if (c == '\\' && i + 1 < row->size)
            {
                hl[i + 1] = HlString;
                i += 2;
                continue;
            }

            if (c == quote)
            {
                quote = 0;
                prev_separator = 1;
            }

            i++;
            continue;
        }

        if (language->line_comment != NULL && StartsWith(row, i, language->line_comment))
        {
            Mark(hl, i, row->size, HlComment);
            break;
        }

        if (language->block_start != NULL && StartsWith(row, i, language->block_start))
        {
            int length = (int)strlen(language->block_start);

            Mark(hl, i, i + length, HlComment);
            i += length;
            state = StateBlockComment;
            continue;
        }

        if (language->quotes != NULL && strchr(language->quotes, c) != NULL)
        {
            quote = c;
            hl[i++] = HlString;
            continue;
        }

        if (language->preprocessor && i == first_word && c == '#')
        {
            i = ScanDirective(row, i, hl);
            prev_separator = 1;
            continue;
        }

        if (prev_separator && isdigit(c))
        {
            int end = i;

            /* 0x1F, 1.5e3, 10UL: a number runs through letters, digits,
               dots and underscores */
            while (end < row->size && (IsWordByte((unsigned char)row->chars[end]) ||
                                       row->chars[end] == '.'))
            {
                end++;
            }

            Mark(hl, i, end, HlNumber);
            i = end;
            prev_separator = 0;
            continue;
        }

        if (prev_separator && (isalpha(c) || c == '_'))
        {
            int end = i;
            int length;
            HlClass found = HlNormal;

            while (end < row->size && IsWordByte((unsigned char)row->chars[end]))
            {
                end++;
            }

            length = end - i;

            if (InList(language->keywords, &row->chars[i], length))
            {
                found = HlKeyword;
            }
            else if (InList(language->types, &row->chars[i], length))
            {
                found = HlType;
            }
            else if (InList(language->constants, &row->chars[i], length))
            {
                found = HlNumber;
            }

            Mark(hl, i, end, found);
            i = end;
            prev_separator = 0;
            continue;
        }

        prev_separator = IsSeparator(c);
        i++;
    }

    return state;
}

/*----------------------------------------------------------
| remembering how rows end
----------------------------------------------------------*/

static void ReserveStates(int count)
{
    int *grown;
    int capacity = state_capacity > 0 ? state_capacity : 64;

    if (count <= state_capacity)
    {
        return;
    }

    while (capacity < count)
    {
        capacity *= 2;
    }

    grown = realloc(state_after, (size_t)capacity * sizeof(int));

    if (grown == NULL)
    {
        KillApp("realloc");
    }

    state_after = grown;
    state_capacity = capacity;
}

static unsigned char *Scratch(int size)
{
    if (size > scratch_capacity)
    {
        unsigned char *grown = realloc(scratch, (size_t)size);

        if (grown == NULL)
        {
            KillApp("realloc");
        }

        scratch = grown;
        scratch_capacity = size;
    }

    return scratch;
}

/* makes the end state of every row before upto known */
static void LearnStatesUpTo(int upto)
{
    if (upto > vted_buffer.num_rows)
    {
        upto = vted_buffer.num_rows;
    }

    ReserveStates(upto);

    while (valid_rows < upto)
    {
        const vted_row_t *row = &vted_buffer.rows[valid_rows];
        int start = valid_rows > 0 ? state_after[valid_rows - 1] : StateNormal;

        state_after[valid_rows] = HighlightRow(current_language, row, start, Scratch(row->size));
        valid_rows++;
    }
}

void SyntaxInvalidate(int row)
{
    if (row < 0)
    {
        row = 0;
    }

    if (row < valid_rows)
    {
        valid_rows = row;
    }
}

void SyntaxHighlightRow(int at, unsigned char *hl)
{
    const vted_row_t *row = &vted_buffer.rows[at];
    int start;
    int end;

    if (current_language == NULL)
    {
        memset(hl, HlNormal, (size_t)row->size);
        return;
    }

    LearnStatesUpTo(at);

    start = at > 0 ? state_after[at - 1] : StateNormal;
    end = HighlightRow(current_language, row, start, hl);

    if (valid_rows == at)
    {
        ReserveStates(at + 1);
        state_after[at] = end;
        valid_rows = at + 1;
    }
}

const char *SyntaxStyle(int hl_class)
{
    switch (hl_class)
    {
    case HlComment:
        return "\x1b[90m";
    case HlString:
        return "\x1b[32m";
    case HlNumber:
        return "\x1b[35m";
    case HlKeyword:
        return "\x1b[33m";
    case HlType:
        return "\x1b[36m";
    case HlPreproc:
        return "\x1b[94m";
    default:
        return "";
    }
}

void SyntaxFree(void)
{
    free(state_after);
    free(scratch);
    state_after = NULL;
    scratch = NULL;
    state_capacity = 0;
    scratch_capacity = 0;
    valid_rows = 0;
}

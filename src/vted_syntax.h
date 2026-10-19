#ifndef VTED_SYNTAX_H
#define VTED_SYNTAX_H

/*==========================================================
|                  Syntax Highlight Module                 |
|                                                          |
| Colors the text by what it means: comments, strings,     |
| numbers, keywords, types and preprocessor lines. The     |
| language is picked from the file name. A row is scanned  |
| when it is drawn; the only thing remembered between rows |
| is whether a block comment is still open at the end of   |
| each one, so a comment can span lines. The row module    |
| reports every change so that memory never goes stale.    |
==========================================================*/

/* what a byte of text is, one class per byte of a row */
typedef enum
{
    HlNormal = 0,
    HlComment,
    HlString,
    HlNumber,
    HlKeyword,
    HlType,
    HlPreproc
} HlClass;

/* picks the language for a file name ("main.c" is C); NULL, or a name
   no language claims, turns highlighting off */
void SyntaxSelectForFile(const char *filename);

/* name of the language in use, NULL when the text is plain */
const char *SyntaxLanguageName(void);

/* the rows from this index on changed: what is known about them is
   thrown away and found out again when they are next drawn */
void SyntaxInvalidate(int row);

/* classifies the bytes of rows[at] into hl, which must hold at least
   the row's size in bytes */
void SyntaxHighlightRow(int at, unsigned char *hl);

/* the terminal escape sequence that switches to the color of a class */
const char *SyntaxStyle(int hl_class);

/* gives back the memory the module holds */
void SyntaxFree(void);

#endif

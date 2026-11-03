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

#include "vted_color.h"

/* what a byte of text is, one class per byte of a row */
typedef enum
{
    HlNormal = 0,
    HlComment,
    HlString,
    HlNumber,
    HlKeyword,
    HlType,
    HlPreproc,
    HlRemoved, /* text that was taken out: the minus lines of a diff */
    HlContext, /* text that stands around a change: the context lines
                  of a diff, the lines a hunk carries unmodified */
    HlFunction, /* the name a function is defined by: the word between
                   a return type and the parameter list */
    HlMatch, /* a match of the text being searched for: never given out
                by the scanner, the renderer hands it to the matches of
                the search query so they light up with a background of
                their own */
    HlTab, /* a tab character drawn as a dotted run: never given out by
             the scanner either, the renderer hands it to tabs when
             they are asked to be shown */
    HlIndent, /* a blank the indent guides mark: never given out by the
                 scanner either, the renderer hands it to the whitespace
                 that ends a nesting level inside a row's indent when
                 the guides are asked to be shown */
    HlMarker, /* the wrap column marker: a background the renderer adds
                over the cells of that column on top of whatever else
                they carry, so the column shows even where text crosses
                it */
    HlBracket /* the partner of the bracket the cursor sits on: never
                 given out by the scanner either, the renderer hands it
                 to that one cell when the person chose a color for the
                 match; without one the cell inverts video as before */
} HlClass;

/* picks the language for a file name ("main.c" is C); NULL, or a name
   no language claims, turns highlighting off */
void SyntaxSelectForFile(const char *filename);

/* name of the language in use, NULL when the text is plain */
const char *SyntaxLanguageName(void);

/* the token that opens a line comment in the language in use — "//"
   in C, "#" in Python — or NULL when the language has none or no
   language is chosen; a second spelling follows the same rule (INI
   comments with ';' besides '#'), and a caller that writes comments
   in takes the first and one that takes them off looks for both */
const char *SyntaxLineComment(void);
const char *SyntaxLineComment2(void);

/* the rows from this index on changed: what is known about them is
   thrown away and found out again when they are next drawn */
void SyntaxInvalidate(int row);

/* classifies the bytes of rows[at] into hl, which must hold at least
   the row's size in bytes */
void SyntaxHighlightRow(int at, unsigned char *hl);

/* the terminal escape sequence that switches to the color of a class */
const char *SyntaxStyle(int hl_class);

/* gives a class a color of its own in place of the built-in one: the
   text of a foreground class, the cell behind it for the classes drawn
   as a background (a search match, the wrap column marker). The color
   reaches the terminal in the richest spelling the color mode allows.
   Returns 0, or -1 when the class is not one a color can be chosen
   for: normal text, and the bracket match, which has its own setting */
int SyntaxSetClassColor(int hl_class, vted_rgb_t color);

/* takes every color chosen with SyntaxSetClassColor back, so the
   built-in ones show again */
void SyntaxResetClassColors(void);

/* the color the matching bracket lights up with: 0 leaves the match
   in inverted video, and 1 to 7 are the colors the bracket-color
   setting names. The choice is held here the way the language is,
   and the renderer asks which it is before drawing the match */
void SyntaxSetBracketColor(int color);
int SyntaxBracketColor(void);

/* the colors the escapes of SyntaxStyle carry: 2 for truecolor RGB, 1
   for the 256-color palette, 0 for the terminal's 16 colors. The
   environment decides (COLORTERM, TERM) until a caller forces one with
   SyntaxSetColorMode; a mode out of 0 to 2 hands the choice back to
   the environment */
void SyntaxSetColorMode(int mode);
int SyntaxColorMode(void);

/* gives back the memory the module holds */
void SyntaxFree(void);

#endif

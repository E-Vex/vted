#ifndef VTED_ROW_H
#define VTED_ROW_H

/*==========================================================
|                     Text Buffer Module                   |
|                                                          |
| Holds the document being edited: a dynamic array of      |
| text rows. Each row owns a heap buffer of raw bytes,     |
| always null-terminated so string helpers can be used     |
| on it as well. The screen module renders these rows,     |
| the file module loads and stores them.                   |
==========================================================*/

/* a tab advances the text to the next multiple of this many cells */
#define VTED_TAB_STOP 8

/* how far the bracket match scan reaches, in bytes: a match beyond it
   counts as missing, so one keystroke on a huge file stays cheap */
#define VTED_BRACKET_SCAN_BYTES (1 << 20)

/* how a row is terminated when the buffer is written back out */
enum
{
    VTED_EOL_LF,  /* \n, the way Unix writes text */
    VTED_EOL_CRLF, /* \r\n, the way Windows writes it */
    VTED_EOL_CR   /* a lone \r, the way classic Mac OS did */
};

typedef struct
{
    char *chars; /* heap buffer with the raw text of the row */
    int size;    /* number of bytes stored in chars */
    int eol;     /* how this row ends (VTED_EOL_*), used on save */
} vted_row_t;

typedef struct
{
    vted_row_t *rows; /* dynamic array of text rows */
    int num_rows;     /* number of rows currently stored */

    char *filename; /* path of the loaded file, NULL when unnamed */
    int dirty;      /* 1 when the buffer has unsaved changes */

    /* 1 when the file ends with a line terminator; the last row's
       ending is written out only then, so a file with no trailing
       newline keeps having none */
    int final_newline;
} vted_buffer_t;

extern vted_buffer_t vted_buffer;

void InitBuffer(void);

int AppendRow(const char *chars, int size, int eol);
int InsertRow(int at, const char *chars, int size, int eol);
void RemoveRow(int at);

int RowInsertChar(vted_row_t *row, int at, int c);
/* places len bytes at byte index at in one step, which a whole run of
   text needs instead of a call per byte; the row is left alone and -1
   returned when at is outside it or memory runs out */
int RowInsertString(vted_row_t *row, int at, const char *s, int len);
int RowRemoveChar(vted_row_t *row, int at);
int RowAppendString(vted_row_t *row, const char *s, int len);

/* cuts the row down to its first at bytes; nothing happens when at lies
   at or past the end */
void RowTruncate(vted_row_t *row, int at);

/* replaces the whole text of the row with a copy of the given bytes;
   the row is left alone and -1 returned when memory runs out */
int RowSetText(vted_row_t *row, const char *chars, int size);

/* splits rows[row] at byte cx: the tail moves to a new row below, which
   ends the way eol says; -1 when memory runs out */
int SplitRow(int row, int cx, int eol);

/* places a text whose lines are separated by '\n' at byte cx of
   rows[row]. The lines after the first become rows of their own, ending
   as eol says, and whatever stood right of cx rides along to the end of
   the last line; -1 when memory runs out */
int InsertText(int row, int cx, const char *text, int length, int eol);

/* takes a text placed by InsertText back out: the rows it made vanish
   and what rode along to its last line returns to the first; -1 when
   memory runs out */
int RemoveText(int row, int cx, const char *text, int length);

/* Display layout. Rows hold raw bytes, but the terminal shows cells:
   a tab spans up to the next tab stop, a control character takes two
   cells (drawn as ^X) and UTF-8 characters take their own width. The
   cursor keeps byte indexes (cx), the screen needs cell columns (rx). */

/* cells taken by the character at byte index at, when it starts at
   cell column cell (a tab's width depends on where it starts) */
int RowCharWidth(const vted_row_t *row, int at, int cell);

/* cell column where the character at byte index cx starts */
int RowCxToRx(const vted_row_t *row, int cx);

/* byte index of the character that covers cell column rx; the end of
   the row when rx lies past the text */
int RowRxToCx(const vted_row_t *row, int rx);

/* how many screen lines the row takes when it flows over lines of
   cols cells: wrapping breaks at word boundaries, a run of
   non-blank glyphs that does not fit the rest of a line starting the
   next one whole — only a run longer than a line, or a wide glyph at
   the very edge, breaks mid-run — while a tab may run to the edge
   and on past it, because it is blank space. The indent is the cells
   every line after the first keeps blank at its start, which the
   row's content flows around */
int RowWrapCount(const vted_row_t *row, int cols, int indent);

/* advances one glyph through the row as the wrapped layout places
   it: cols cells per screen line, with wrapping breaking at word
   boundaries — a run of non-blank glyphs that does not fit the rest
   of its line starts the next one whole, and only a run longer than
   a line, like a wide glyph at the very edge, breaks mid-run (a tab,
   being blank space, may run to the edge and on past it). With wrap 0
   the row lays out on one endless line instead, which is what a
   viewport that scrolls sideways shows. The indent is the cells every
   line after the first keeps blank at its start, so a wrapped row
   continues under its own leading whitespace: a glyph the layout
   puts past the first line starts at that line's first cell after
   the indent, and the indent is clamped to leave the line at least
   one content cell. The walk's byte index and cell are kept in *at
   and *cell and moved past the glyph, and *in_run keeps whether it
   is inside a run of non-blank glyphs: it starts at 0 and is carried
   from glyph to glyph. The cell the glyph was placed at is returned,
   so renderer and cursor math can walk a row one glyph at a time and
   always agree on the layout */
int RowWrapAdvance(const vted_row_t *row, int wrap, int cols, int indent,
                   int *at, int *cell, int *in_run);

/* the cell a run of non-blank glyphs that starts at byte index at is
   placed at when it begins at start_cell on lines of cols cells: a
   run that does not fit the rest of its line moves down to the next
   one whole, as long as one line has room for it past the indent its
   continuation keeps — a run longer than that breaks at the line's
   edge instead, and one that already starts at its line's first cell
   after the indent is as far left as it goes */
int RowWrapRunStart(const vted_row_t *row, int at, int start_cell, int cols,
                    int indent);

/* where byte index cx sits when the row flows over lines of cols
   cells: the screen line it is on (one past the row's own lines when
   the text ends exactly at a line edge) and the cell column within
   that line. The indent is the cells every line after the first
   keeps blank at its start */
void RowCxToWrapped(const vted_row_t *row, int cx, int cols, int indent,
                    int *line, int *col);

/* the inverse: the byte index of the glyph at screen line line, cell
   column col of the wrapped layout, which keeps indent cells blank
   at the start of every line after the first. The glyph that covers
   the spot is the answer; the padding a moved run leaves at a line's
   end gives the line's last glyph, the padding a continuation line
   keeps for its indent gives the line's own first glyph, and a spot
   past the row's text its end, so a cursor aimed there stays on the
   line it was aimed at */
int RowWrappedToCx(const vted_row_t *row, int line, int col, int cols,
                   int indent);

/* number of leading blanks (spaces and tabs) of the row, looking at
   no more than the first limit bytes; the indent a new line copies */
int RowIndentLength(const vted_row_t *row, int limit);

/* the cells a row's wrapped lines keep for their indent when it
   flows over lines of cols cells: the cell column where the row's
   first non-blank glyph starts, its leading whitespace laid out, so
   the wrapped lines line up under it the way the first line does. A
   row of nothing but blanks keeps none, and the indent never takes
   a line's last cell */
int RowWrapIndentCells(const vted_row_t *row, int cols);

/* the nesting levels the row sits in, as the cell columns where each
   level's indent ends: its own edge first, then every shallower one
   still open above it. Empty rows and rows of nothing but blanks
   hold no level of their own — blank lines inside a block do not end
   the levels above them — and at most max edges are kept, the deepest
   ones. How many were written is returned */
int BufferIndentEdges(int row, int *edges, int max);

/* word movement: a word is a run of characters of one class (see
   Utf8CharClass), so "foo_bar" is one word and "->" is another.
   RowWordRight gives the byte index of the start of the next word,
   RowWordLeft the start of the word before cx. Both stay inside the
   row: they return its end or its start when there is no further
   word, and the caller decides whether to wrap to another line */
int RowWordRight(const vted_row_t *row, int cx);
int RowWordLeft(const vted_row_t *row, int cx);

/* word completion, the way dabbrev-expand in Emacs works: collects
   the distinct words of the buffer being edited that start with the
   word left of the cursor at (cy, cx), so a menu can offer them. A
   word is a run of word characters, the unit the word moves cross,
   and the word being completed is the run of them that ends at the
   cursor; the match folds the case — an ASCII letter of the prefix
   agrees with either of its cases in a candidate, the fold the
   search uses, every other byte agreeing with itself alone. The
   words are collected nearest first — the text before the cursor and
   the rows above it, read backwards, then the text after it and the
   rows below, read forwards — then ranked by how often the buffer
   says them, the ones it says most first, a saying counted under
   either spelling of a word and words said as often as one another
   keeping the nearness they were collected in. Each is copied
   NUL-terminated into
   word_size bytes of the caller's words array, at most max_words of
   them; a word too long for a line of the array is passed over, and
   so is the word the cursor sits inside, which would only double its
   own tail. How many words filled the array is returned, and the
   length of the one being completed in *out_prefix_len (0 when the
   cursor sits past no word at all) */
int BufferCollectCompletions(int cy, int cx,
                             char *words, int max_words, int word_size,
                             int *out_prefix_len);

/* the words of a text that is not the one being edited — another
   open buffer's, say: every distinct word of its rows, first row to
   last, that starts with prefix and is longer than it, the match
   folding the case the way BufferCollectCompletions does, appended
   NUL-terminated to the first count lines of the caller's words
   array, the same lines BufferCollectCompletions fills, so a word
   they hold already is passed over, until max_words lines are filled
   in all; a word too long for a line of the array is passed over the
   same way. How many lines the array holds afterwards is returned. A
   text of NULL, a prefix of NULL or no bytes, a count below zero or
   already at max_words, changes nothing */
int BufferCollectTextWords(const vted_buffer_t *text,
                           const char *prefix, int prefix_len,
                           char *words, int max_words, int word_size,
                           int count);

/* where the bytes of query sit in the row: the first match that
   starts inside [from, to), or the last one, or -1 when there is
   none. A match may start anywhere, even inside a multi-byte
   character, the way a plain byte search finds it; the bounds are
   clamped to the row, and an empty query never matches. With the
   case folded (RowSetIgnoreCase), letters agree with either of
   their cases and every other byte with itself alone */
int RowFindNext(const vted_row_t *row, int from, int to,
                const char *query, int query_len);
int RowFindPrev(const vted_row_t *row, int from, int to,
                const char *query, int query_len);

/* looks for the bracket under the cursor, or the one right before
   it, and scans the buffer for the bracket that closes it: the scan
   counts brackets of the same pair so nesting comes out right, and
   it stops after VTED_BRACKET_SCAN_BYTES bytes, so a match farther
   away than that counts as missing. Returns 1 with the byte index
   and row of the matching bracket in *match_cx and *match_cy, 0 when
   the cursor is not on a bracket or no match is there to be found */
int BufferFindBracketMatch(int cy, int cx, int *match_cy, int *match_cx);

/* the rows the bracket at row cy column cx (or the one right before
   it) and its partner enclose: whole rows, from the earlier of the
   two to the later one, everything in between included. Returns 1
   with the span in *first_row and *last_row, 0 when the cursor is
   not on a bracket or no match is there to be found */
int BufferFindBracketBlock(int cy, int cx, int *first_row, int *last_row);

/* places copies of the rows first through last below them, in order
   and each keeping its own text and ending. Returns 0, or -1 when a
   copy could not be made */
int BufferDuplicateRows(int first, int last);

/* puts the line comment in front of the first non-blank of every row
   first through last — or takes it off the rows that carry it, when
   the first row that is not all blanks already does. The token goes
   in with a space behind it and comes off with that space when it is
   there; a row of only blanks is never given one, and a row that
   already carries a token, either spelling, keeps the one it has, so
   a mixed span comes out of one press all one way. How many rows
   changed is reported through *out_changed, negative when they were
   uncommented. Returns 0, or -1 when memory ran out half way */
int BufferToggleComment(int first, int last, const char *token,
                        const char *token2, int *out_changed);

/* counts the matches of query in the whole buffer, and how many of
   them start before (row, col): the place among them all of the
   match the cursor sits on. Matches are counted the way the search
   steps through them, so ones that share bytes count each */
void BufferCountMatches(const char *query, int query_len,
                        int row, int col, int *total, int *before);

void FreeRow(vted_row_t *row);
void FreeBuffer(void);

/* sets the tab stop in cells used by RowCharWidth(); VTED_TAB_STOP by
   default, clamped to at least one */
void RowSetTabWidth(int width);

/* sets whether the search folds the case: 1 matches letters without
   regard to their case, 0 (the default) takes bytes as they stand */
void RowSetIgnoreCase(int fold);

#endif

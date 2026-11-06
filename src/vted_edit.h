#ifndef VTED_EDIT_H
#define VTED_EDIT_H

/*==========================================================
|                    Editing Module                        |
|                                                          |
| The operations that change the buffer: inserting and     |
| deleting text, splitting and merging lines, and the      |
| whole-line commands. They keep the cursor and the         |
| file's line endings consistent. vted.c dispatches keys   |
| to them; the screen module only reads the buffer they    |
| leave behind.                                            |
==========================================================*/

void EditorInsertChar(int c);
void EditorInsertNewline(void);

/* the electric pair: an opening bracket laid in with its closing half
   right behind it, the cursor between the two — one undoable step
   that folds into the typing around it the way a pair of keystrokes
   would. On the phantom row past the end the pair starts a new line
   the way a single character does */
void EditorInsertPair(int opener, int closer);

/* takes the empty bracket pair the cursor sits inside — the opener
   left of it, the closer under it — back out as one undoable step,
   the way the electric typing laid it in as one. Returns 1 when a
   pair went, 0 when the cursor sits inside none: the caller falls
   back on the plain backspace then */
int EditorDeletePair(void);

/* places a pasted text at the cursor and leaves the cursor after it.
   The text goes in as it is: no auto indent, and CR LF or a lone CR
   count as one line break; control bytes other than Tab are dropped.
   Pasted on the empty row past the end, a final line break only ends
   the last line and leaves the cursor on that row again. The whole
   paste is one undo step, however many lines it has */
void EditorInsertText(const char *text, int length);
void EditorDelChar(void);

/* deletes the run of blanks and the word before them on the left of
   the cursor, the way the shells and readline take Alt-Backspace: one
   undoable step, the cursor left where the deletion started. At the
   start of a line the word ends on the line above, so the lines merge
   the way Backspace does; a selection is the caller's to delete */
void EditorDeleteWordLeft(void);

/* deletes the text the word move right crosses (Ctrl-Right, Alt-f):
   the run the cursor sits in and the blanks behind it, up to the
   start of the next word — the rest of a word when the cursor sits
   inside one, the blanks when it sits on them. One undoable step, the
   cursor left where it stood. At the end of a line the next word
   starts on the line below, so the lines merge the way Delete does */
void EditorDeleteWordRight(void);

/* completes the word before the cursor with the given one: the bytes
   past the prefix — word[prefix_len..length) — land at the cursor and
   the cursor lands after them, the prefix itself staying in the text.
   The whole pick is one undoable step, the way the word deletions
   are a word at a time */
void EditorInsertCompletion(const char *word, int length, int prefix_len);

void EditorDeleteLine(void);
void EditorDuplicateLine(void);
void EditorMoveLineUp(void);
void EditorMoveLineDown(void);

/* comments the line under the cursor — or every line the selection
   spans — with the token the language comments lines with, and takes
   the token off again when the lines carry it: the whole span is one
   undo step, and the selection and the cursor ride the text as it
   shifts. A language without a line comment, or none at all, is
   only reported in the message bar */
void EditorToggleComment(void);

/* puts one indent step — the bytes the Tab key inserts — in front of
   every row the selection spans, as one undo step; the selection and
   the cursor ride the text as it shifts. A row of only blanks keeps
   none */
void EditorIndentSelection(void);

/* takes one indent step off every row the selection spans — the tab
   a row starts with, or else up to the tab width of its spaces — or
   off the row of the cursor, when nothing is selected, as one undo
   step; the selection and the cursor ride the text as it shrinks */
void EditorOutdentSelection(void);

/* selection: anchor/cursor model. The anchor is set with
   EditSetAnchor(); the cursor is the other end of the selection. */
void EditSetAnchor(int y, int x);
void EditClearSelection(void);
int EditSelectionActive(void);
void EditSelectionRange(int *top, int *left, int *bottom, int *right);

/* copies the selection into a heap buffer the caller frees, lines
   joined with line feeds; NULL when there is nothing to copy */
char *EditCopyText(int *out_length);

/* deletes the selection as one undoable change; the cursor lands at
   the top/left end of what was selected, and the light the frame
   drew it with goes out, so whatever lands in the span's place is
   not lit as if still selected */
void EditDeleteSelection(void);

/* copies or cuts the selection (or the current line) to the terminal
   clipboard with an OSC 52 escape sequence; the text of it is kept,
   so a paste of its own can come from it */
void EditorCopy(void);
void EditorCut(void);

/* pastes the copy the editor kept from its last copy or cut at the
   cursor, the way a paste from the terminal arrives: the text goes
   in as it is, the whole of it one undo step. Returns the number of
   bytes laid in — 0 when no copy has been kept yet */
int EditorPasteKeptCopy(void);

/* gives the kept copy back, the way the exit handlers give the rest
   of the editor state */
void EditFreeKeptCopy(void);

/* replaces every occurrence of query in the buffer with replacement,
   one undoable step per occurrence, and returns the number of
   replacements made. query must not contain a line break */
int BufferReplaceAll(const char *query, const char *replacement);

/* the same walk for the pattern that stands compiled in the regex
   module: the escapes of the replacement are filled with the groups
   of the match they name, \0 the whole match and \1 through \9 its
   parentheses, one undoable step per occurrence, and the number of
   replacements made comes back */
int BufferReplaceRegex(const char *replacement);

/* replaces the old_len bytes the row holds at `at` with new_text, one
   undoable step: the bytes that were there go into the step, taken
   from the row, so undo puts back exactly what the span held. Returns
   the number of bytes written — 0 when the new text is empty and the
   span is deleted — or -1 when the span sits outside the row */
int BufferReplaceSpan(int row_index, int at, int old_len,
                      const char *new_text);

/* the same splice for the pattern that stands compiled in the regex
   module: the groups of the match that starts at `at` fill the escapes
   of the replacement, so a walk that asks about one match at a time
   writes each one with the groups of its own. Returns the number of
   bytes written, or -1 when no match starts there or the span sits
   outside the row */
int BufferReplaceSpanRegex(int row_index, int at, int match_len,
                           const char *replacement);

#endif

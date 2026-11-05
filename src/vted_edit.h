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
   the top/left end of what was selected */
void EditDeleteSelection(void);

/* copies or cuts the selection (or the current line) to the terminal
   clipboard with an OSC 52 escape sequence */
void EditorCopy(void);
void EditorCut(void);

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

#endif

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
void EditorDeleteLine(void);
void EditorDuplicateLine(void);
void EditorMoveLineUp(void);
void EditorMoveLineDown(void);

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

#endif

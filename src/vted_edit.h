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
   The whole paste is one undo step, however many lines it has */
void EditorInsertText(const char *text, int length);
void EditorDelChar(void);
void EditorDeleteLine(void);
void EditorDuplicateLine(void);

#endif

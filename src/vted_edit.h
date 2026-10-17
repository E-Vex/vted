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
void EditorDelChar(void);
void EditorDeleteLine(void);
void EditorDuplicateLine(void);

#endif

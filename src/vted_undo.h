#ifndef VTED_UNDO_H
#define VTED_UNDO_H

/*==========================================================
|                     Undo Module                          |
|                                                          |
| Every change to the buffer is described by an entry:     |
| enough to replay it (redo) and to invert it (undo).      |
| The editing operations record entries as they work;      |
| this module keeps the two stacks and moves the buffer    |
| between the states they describe. A run of keystrokes    |
| in the same place folds into a single entry, so one     |
| step takes back a whole burst of typing, and a pasted    |
| text of any length is a single entry of its own.         |
==========================================================*/

typedef enum
{
    UndoKindInsertBytes, /* bytes were placed inside a row */
    UndoKindRemoveBytes, /* bytes were taken out of a row */
    UndoKindSplitRow,    /* one row became two */
    UndoKindMergeRows,   /* two rows became one */
    UndoKindInsertRow,   /* a whole row appeared */
    UndoKindRemoveRow,   /* a whole row vanished */
    UndoKindReplaceRow,  /* a row's whole text was replaced */
    UndoKindInsertText,  /* a text of several lines was placed (a paste) */
    UndoKindRemoveText,  /* a text of several lines was removed (a deletion) */
    UndoKindMoveRow      /* a whole row moved up or down */
} UndoKind;

typedef struct
{
    UndoKind kind;

    int row; /* the row the change touched */
    int cx;  /* the column inside it */

    char *bytes; /* the bytes involved (NULL when there are none); the
                    lines of an inserted text are separated by '\n' */
    int length;

    int eol;      /* the row ending involved */
    int prev_eol; /* the ending a merge replaced */
    int indent;   /* blanks auto-indent added to the new line */
    int fold;     /* a created row that may absorb the typing that follows it */
    int made_row; /* an inserted text went onto a row made just for it */

    /* for UndoKindMoveRow: +1 when the row moved down, -1 when up */
    int move_delta;

    int undo_y, undo_x; /* the cursor to restore on undo */
    int redo_y, redo_x; /* the cursor to restore on redo */
} UndoEntry;

/* stores a copy of the entry's bytes and drops the redo history */
void UndoPush(UndoEntry *entry);

/* the same, but a keystroke contiguous with the previous typing
   folds into it: one Ctrl-Z then takes back the whole burst */
void UndoPushTyping(UndoEntry *entry);

/* drops both histories (a freshly loaded file has none) */
void UndoReset(void);

int EditorUndo(void); /* returns 1 when something was undone */
int EditorRedo(void); /* returns 1 when something was redone */

#endif

#ifndef VTED_BUFFERS_H
#define VTED_BUFFERS_H

/*==========================================================
|                    Buffer List Module                    |
|                                                          |
| The files open in the editor: one slot per file, holding |
| its text buffer, its cursor and viewport, and its undo   |
| history while another buffer is being edited. The        |
| module swaps the live buffer in and out of the editor's  |
| globals, so every other module keeps working on exactly  |
| one buffer: the live one.                                |
==========================================================*/

#include <stdio.h>

/* opens filename as a new buffer and makes it the one being edited.
   A path that does not exist yet opens as an empty buffer that keeps
   the name, the way the command line does. Returns 0, or -1 with
   errno from the failed open — the current buffer is then untouched */
int BuffersOpen(const char *filename);

/* opens the text of a stream as a new unnamed buffer and makes it the
   one being edited; -1 on a read error, with the current buffer
   untouched */
int BuffersOpenStream(FILE *stream);

/* makes an empty unnamed buffer the one being edited: the buffer the
   editor starts in when no file is named */
void BuffersStartEmpty(void);

/* makes the buffer at index the one being edited, restoring its
   cursor and viewport; an index out of range does nothing. Returns 1
   when the buffer changed, 0 when it did not */
int BuffersSwitch(int index);

int BuffersCount(void);   /* buffers open, 0 before the first one */
int BuffersCurrent(void); /* index of the buffer being edited, -1 before the first */

/* the name of the buffer at index: its path, or NULL when it is
   unnamed or the index is out of range */
const char *BuffersName(int index);

/* whether the buffer at index has unsaved changes; 0 when the index
   is out of range */
int BuffersDirty(int index);

/* how many buffers have unsaved changes */
int BuffersDirtyCount(void);

/* closes the buffer at index: its text, undo history and name are
   given back and the slots above slide down. The neighbor takes its
   place and becomes the one being edited when the live one closed;
   closing the last buffer starts a fresh empty one. Returns 1 when a
   buffer was closed, 0 when the index is out of range */
int BuffersClose(int index);

/* frees every buffer but the live one, which the editor core frees
   the usual way; runs once, on the way out */
void BuffersFreeOthers(void);

#endif

#ifndef VTED_FILE_H
#define VTED_FILE_H

/*==========================================================
|                      File I/O Module                     |
|                                                          |
| Reads a file into the text buffer, one row per line,     |
| and writes the buffer back out on save. A stream can     |
| be read the same way, for text piped into the editor.    |
==========================================================*/

#include <stdio.h>

int OpenFile(const char *filename);

/* reads a whole stream into the buffer the way OpenFile reads a
   file; the buffer stays unnamed, so saving it asks for a name */
int ReadStream(FILE *stream);

int SaveFile(void);

/* did the buffer's file change on disk since it was last read or
   written: 0 says it stands as the buffer saw it, 1 says another
   hand changed it, and 2 says the file is gone or is no regular
   file any more. An unnamed buffer never changes underneath */
int FileCheckChanged(void);

/* reads the buffer's file into the buffer again, the old text given
   back only after the new one came in whole: a read that fails —
   the file gone, or not a file any more — leaves the buffer exactly
   as it stood, with errno saying why. The undo history is the
   caller's to walk, for the cursor and the history of a buffer
   whose whole text changed cannot be asked to follow it */
int FileReload(void);

#endif

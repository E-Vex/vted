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

#endif

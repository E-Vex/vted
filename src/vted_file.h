#ifndef VTED_FILE_H
#define VTED_FILE_H

/*==========================================================
|                      File I/O Module                     |
|                                                          |
| Reads a file into the text buffer, one row per line,     |
| and writes the buffer back out on save.                  |
==========================================================*/

int OpenFile(const char *filename);

#endif

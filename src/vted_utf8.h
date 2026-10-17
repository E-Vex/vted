#ifndef VTED_UTF8_H
#define VTED_UTF8_H

/*==========================================================
|                       UTF-8 Module                       |
|                                                          |
| Rows store raw bytes, but the cursor, deletion and the   |
| renderer must treat a multibyte sequence as one          |
| character. These helpers find character boundaries in a  |
| byte string and tell how many terminal cells a character |
| occupies. Invalid bytes never break anything: each one   |
| is treated as a character of its own, one byte long.     |
==========================================================*/

/* decodes the character at the start of s (len bytes available).
   Returns how many bytes it spans (1 to 4) and stores its code
   point in *code_point, or -1 there when the bytes are invalid */
int Utf8Decode(const char *s, int len, int *code_point);

/* index of the first byte of the character before byte index at */
int Utf8PrevBoundary(const char *s, int at);

/* index just past the character that starts at byte index at */
int Utf8NextBoundary(const char *s, int len, int at);

/* terminal cells taken by a code point: 0 (combining marks and
   other zero-width characters), 1 or 2 (East Asian wide, emoji) */
int Utf8CharWidth(int code_point);

#endif

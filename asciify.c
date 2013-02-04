/* File: asciify.c
   Time-stamp: <2012-09-25 17:29:11 gawen>

   Copyright (C) 2012 David Hauweele <david@hauweele.net>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>. */

#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <err.h>
#include <assert.h>

#include "iobuf_stdout.h"

#define IN_IOBUF_SIZE 4096

/* Converts a wide char to an ASCII char. */
const char * asciify_wchar(wchar_t wchar)
{
  if(isascii(wchar)) {
    static char cchar[2];
    cchar[0] = (char)wchar;

    return cchar;
  }

  /* We use a simple switch here though we could achieve a better result using a
     translation table. */
  switch(wchar) {
  case L'⍽':
    return "_";
  case L'–':
    return "-";
  case L'é':
  case L'è':
  case L'ë':
  case L'ê':
    return "e";
  case L'ï':
  case L'î':
  case L'í':
  case L'ì':
    return "i";
  case L'à':
  case L'á':
  case L'ä':
  case L'â':
    return "a";
  case L'ö':
  case L'ô':
  case L'ó':
  case L'ò':
    return "o";
  case L'ü':
  case L'û':
  case L'ú':
  case L'ù':
  case L'µ':
    return "u";
  case L'ç':
    return "c";
  case L'ŷ':
  case L'ÿ':
  case L'ý':
  case L'ỳ':
    return "y";
  case L'É':
  case L'È':
  case L'Ë':
  case L'Ê':
    return "e";
  case L'Ï':
  case L'Î':
  case L'Í':
  case L'Ì':
    return "I";
  case L'Á':
  case L'À':
  case L'Ä':
  case L'Â':
    return "A";
  case L'Ö':
  case L'Ô':
  case L'Ó':
  case L'Ò':
    return "O";
  case L'Ü':
  case L'Û':
  case L'Ú':
  case L'Ù':
    return "U";
  case L'Ç':
    return "C";
  case L'©':
    return "(C)";
  case L'Ŷ':
  case L'Ÿ':
  case L'Ý':
  case L'Ỳ':
    return "Y";
  default:
    return "?";
  }
}

void proceed(int fd)
{
  int offset;

  while(1) {
    int bsize;
    int index = 0;
    char buf[IN_IOBUF_SIZE];
    ssize_t n = read(fd, buf + offset, IN_IOBUF_SIZE - offset);

    if(n < 0)
      err(1, "read error");
    else if(!n)
      break;

    bsize  = n + offset;
    offset = 0;
    while(index < n + offset) {
      const char *cchar;
      wchar_t wchar;
      int     wres;

      wres = mbtowc(&wchar, buf + index, bsize - index);

      /* Special case when we leave the current buffer. It's possible that we
         lie on a open multibyte sequence. */
      if(wres == -1) {
        char cp_buf[sizeof(wchar_t)];

        offset = bsize - index;
        if(offset > sizeof(wchar_t))
          errx(2, "cannot decode multibyte string %s", buf);

        memcpy(cp_buf, buf + index, offset);
        memcpy(buf, cp_buf, sizeof(cp_buf));

        /* Don't forget to reset the erroneous state. */
        (void)mbtowc(NULL, NULL, 0);
        break;
      }

      index += wres;

      cchar = asciify_wchar(wchar);
      iobuf_printf(cchar);
    }
  }

  /* Check for the final multibyte parsing state */
  if(mbtowc(NULL, NULL, 0))
    errx(1, "cannot decode multibyte string");
}

void exit_cb(void)
{
  /* We need to release the stdout context to flush buffers. */
  iobuf_stdout_destroy();
}

int main(int argc, char *argv[])
{
  argv++;

  /* Configure locales according to environments variables and
   * initialisation of the stdout context. */
  setlocale(LC_ALL, "");
  iobuf_stdout_init();
  atexit(exit_cb);

  if(!*argv)
    proceed(STDIN_FILENO);

  for(; *argv ; argv++) {
    int fd = open(*argv, O_RDONLY, 0);

    if(fd < 0)
      errx(1, "cannot open %s", *argv);

    proceed(fd);

    close(fd);
  }

  return 0;
}

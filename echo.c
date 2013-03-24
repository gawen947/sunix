/* File: echo.c

   Copyright (C) 2011 David Hauweele <david@hauweele.net>

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

#include <stdbool.h>

#include "tlibc.h"
#include "tlibc.c"

/* default string size */
#define STRING_SIZE 256

#define isxalpha(c) (c >= 'a' && c <= 'f')
#define isXalpha(c) (c >= 'A' && c <= 'F')
#define isodigit(c) (c >= '0' && c <= '7')

/* TODO: support \0NNN \xHH */
inline static void esc_write(const char *s)
{
  int i;
  size_t length = STRING_SIZE;
  char *str = malloc(length);
  if(!str)
    error(1, "out of memory");

  for(i = 0 ; *s != '\0' ; i++, s++) {
    char val1, val2, val3;

    /* reallocation */
    if(i >= length) {
      if(length > 1048576)
        length += 1048576;
      else
        length <<= 1;
      str = realloc(str, length);
    }

    if(*s == '\\') {
      s++;

      switch(*s) {
      case('\\'):
        str[i] = '\\';
        break;
      case('a'):
        str[i] = '\a';
        break;
      case('b'):
        str[i] = '\b';
        break;
      case('e'):
        str[i] = '\e';
        break;
      case('f'):
        str[i] = '\f';
        break;
      case('n'):
        str[i] = '\n';
        break;
      case('r'):
        str[i] = '\r';
        break;
      case('t'):
        str[i] = '\t';
        break;
      case('v'):
        str[i] = '\v';
        break;
      case('x'):
        /* first digit */
        s++;
        if(isdigit(*s))
          val1 = *s - '0';
        else if(isxalpha(*s))
          val1 = *s - 'a' + 10;
        else if(isXalpha(*s))
          val1 = *s - 'A' + 10;
        else {
          s -= 2;
          str[i] = *s;
          break;
        }

        /* second digit */
        s++;
        if(isdigit(*s))
          val2 = *s - '0';
        else if(isxalpha(*s))
          val2 = *s - 'a' + 10;
        else if(isXalpha(*s))
          val2 = *s - 'A' + 10;
        else {
          s--;
          str[i] = val1;
          break;
        }

        str[i] = val1*16 + val2;
        break;
      case('0'):
        /* first digit */
        s++;
        if(isodigit(*s))
          val1 = *s - '0';
        else {
          s -= 2;
          str[i] = *s;
          break;
        }

        /* second digit */
        s++;
        if(isodigit(*s))
          val2 = *s - '0';
        else {
          s--;
          str[i] = val1;
          break;
        }

        /* third digit */
        s++;
        if(isodigit(*s))
          val3 = *s - '0';
        else {
          s--;
          str[i] = val1 * 8 + val2;
          break;
        }

        str[i] = val1 * 64 + val2 * 8 + val3;
        break;
      default:
        s--;
        str[i] = *s;
        break;
      }
    }
    else
      str[i] = *s;
  }

  write(STDOUT_FILENO, str, i);

  free(str);
}

int main(int argc, char **argv)
{
  int i;
  bool opt_bs  = false;
  bool opt_nnl = false;

  /* argument parsing */
  for(i = 1 ; i < argc && *argv[i] == '-' ; i++) {
    int j;
    for(j = 1 ; j != 0 ; j++) {
      switch(*(argv[1] + j)) {
      case('e'):
        opt_bs = true;
        break;
      case('n'):
        opt_nnl = true;
        break;
      case('E'):
        opt_bs = false;
        break;
      case('\0'):
        j = -1;
        break;
      default:
        goto END_ARGS;
      }
    }
  }

END_ARGS:

  if(opt_bs) {
    for(; i < argc - 1 ; i++) {
      esc_write(argv[i]);
      write(STDOUT_FILENO, " ", 1);
    }
    if(i == argc - 1)
      esc_write(argv[i]);
  }
  else {
    for(; i < argc - 1 ; i++) {
      write(STDOUT_FILENO, argv[i], strlen(argv[i]));
      write(STDOUT_FILENO, " ", 1);
    }
    if(i == argc - 1)
      write(STDOUT_FILENO, argv[i], strlen(argv[i]));
  }

  if(!opt_nnl)
    write(STDOUT_FILENO, "\n", 1);
}

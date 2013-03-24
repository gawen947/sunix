/* File: basename.c

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

#include "tlibc.c"
#include "tlibc.h"

static char * xstrndup(const char *s, size_t n)
{
  register void *str = strndup(s, n);
  if(!str)
    error(1, "out of memory");

  return str;
}

/* return address of the last file name component of NAME */
static char * last_component (char const *name)
{
  char const *base = name;
  char const *p;
  bool saw_slash = false;

  while (*base == '/')
    base++;

  for (p = base; *p; p++) {
    if (*p == '/')
      saw_slash = true;
    else if (saw_slash) {
      base = p;
      saw_slash = false;
    }
  }

  return (char *) base;
}

static size_t base_len (char const *name)
{
  size_t len;
  size_t prefix_len = 0;
  
  for (len = strlen(name);  1 < len && name[len - 1] == '/';  len--)
    continue;
  
  return len;
}

static char * base_name(char const *name)
{
  char const *base = last_component(name);
  size_t length;

  /* if there is no last component, then name is a file system root or
     the empty string */
  if(!*base)
    return xstrndup(name, base_len(name));

  length = base_len(base);
  if(base[length] == '/')
    length++;

  return xstrndup(base, length);
}

static bool strip_trailing_slashes(char *file)
{
  char *base = last_component(file);
  char *base_lim;
  bool had_slash;

  /* last component returns "" for file system roots, but we need to turn '///'
     into '/' */
  if(!*base)
    base = file;
  base_lim = base + base_len(base);
  had_slash = (*base_lim != '\0');
  *base_lim = '\0';

  return had_slash;
}

static void remove_suffix(char *name, const char *suffix)
{
  char *np;
  const char *sp;

  np = name + strlen(name);
  sp = suffix + strlen(suffix);

  while(np > name && sp > suffix)
    if(*--np != *--sp)
      return;
  if(np > name)
    *np = '\0';
}

int main(int argc, char **argv)
{
  char *name;

  if(argc < 2)
    error(1, "missing operand");

  name = base_name(argv[1]);
  strip_trailing_slashes(name);

  if(argc >= 3 && name[0] != '/')
    remove_suffix(name, argv[2]);

  write(STDOUT_FILENO, name, strlen(name));
  write(STDOUT_FILENO, "\n", 1);
  free(name);

  return 0;
}


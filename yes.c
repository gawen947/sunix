/* File: yes.c
   Time-stamp: <2011-06-12 02:34:50 gawen>

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

#include "tlibc.c"
#include "tlibc.h"

int main(int argc, char **argv)
{
  size_t length;
  const char *arg;
  char *str;
  int i, idx;

  if(argc <= 1)
    while(1)
      print("y");

  /* message length */
  for(i = 0, length = 1 ; i < argc ; i++) {
    length += strlen(argv[i]);
    length += 1;
  }

  /* allocate string for the message */
  str = malloc(length);
  if(!str)
    error(1, "out of memory");

  /* copy the string */
  for(i = 1, idx = 0 ; i < argc - 1 ; i++) {
    arg = argv[i];

    for(; *arg != '\0' ; arg++, idx++)
      str[idx] = *arg;
    str[idx] = ' ';
    idx++;
  }

  arg = argv[i];
  for(; *arg != '\0' ; arg++, idx++)
    str[idx] = *arg;
  str[idx++] = '\n';
  str[idx]   = '\0';

  while(1)
    write(STDOUT_FILENO, str, length);

  /* though will never go here */
  free(str);
  return 0;
}


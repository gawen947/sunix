/* File: scat.c
   Time-stamp: <2011-06-08 21:48:48 gawen>

   Copyright (C) 2010 David Hauweele <david.hauweele@gmail.com>

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

#include "tlibc.h"

/* read one disk block */
#define BUFFER_SIZE 4096

/* display a specific file */
static void show(char *filename)
{
  char buf[BUFFER_SIZE];
  register int n = BUFFER_SIZE;
  register int fd;

  fd = open(filename, O_RDONLY, 0);
  if(fd < 0)
    error(1, "Cannot open file");

  do {
    n = read(fd, buf, BUFFER_SIZE);
    write(STDOUT_FILENO, buf, n);
  } while(n == BUFFER_SIZE);
}

int main(int argc, char **argv)
{
  argc--;
  while(argc > 0)
    show(argv[argc--]);

  exit(0);
}

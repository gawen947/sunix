/* File: scat.c
   Time-stamp: <2011-06-11 02:39:16 gawen>

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
#include "tlibc.c"

/* same value as original cat command */
#define BUFFER_SIZE 32768

/* path for real cat */
#define REAL_CAT    "/bin/cat.real"

/* display a specific file */
static void show(char *filename)
{
  char buf[BUFFER_SIZE];
  register int n;
  register int fd;

  fd = open(filename, O_RDONLY, 0);
  if(fd < 0)
    error(1, "Cannot open file");

  do {
    n = read(fd, buf, BUFFER_SIZE);
    write(STDOUT_FILENO, buf, n);
  } while(n == BUFFER_SIZE);

  close(fd);
}

int main(int argc, char **argv)
{
  /* no arguments means we read from stdin */
  if(argc == 1) {
    char buf[BUFFER_SIZE];
    register int n;

    do {
      n = read(STDIN_FILENO, buf, BUFFER_SIZE);
      write(STDOUT_FILENO, buf, n);
    } while(n > 0);

    goto CLEAN;
  }

  argc--;
  while(argc > 0) {
    /* anything that begins with '-' is an argument for real cat */
    if(*argv[argc] == '-')
      execve("/bin/cat.real", argv, environ);

    /* show this file */
    show(argv[argc--]);
  }

CLEAN:
  /* flush the buffers */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);

  return 0;
}

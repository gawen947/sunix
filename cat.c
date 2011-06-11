/* File: scat.c
   Time-stamp: <2011-06-11 15:30:39 gawen>

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
#define CAT_WRAPPER "/bin/cat.real"

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
  } while(n > 0);

  close(fd);
}

int main(int argc, char **argv)
{
  int i;

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

  /* anything that begins with '-' is an argument for real cat */
  for(i = 1 ; i < argc ; i++)
    if(*argv[i] == '-')
      execve(CAT_WRAPPER, argv, environ);

  /* clean and simple cat so we may use our code */
  for(i = 1 ; i < argc ; i++)
    show(argv[i]);

CLEAN:
  /* flush the buffers */
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  return 0;
}

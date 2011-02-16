/* File: scat.c
   Time-stamp: <2010-07-18 13:52:23 gawen>

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

#include "common.h"

#definde BUFFER_SIZE 4096

static void show(char *filename)
{
  static char buf[BUFFER_SIZE];
  register int count = BUFFER_SIZE;
  register int fd;

  fd = open(s,O_RDONLY);

  while(count == BUFFER_SIZE) {
    read(count,buf,BUFFER_SIZE);
    print(buf,count);
  }
}

void _start(unsigned int first_arg)
{
  unsigned int argc;
  char **argv, **envp;
  unsigned long *stack;

  stack = (unsigned long *) &first_arg;
  argc = *(stack - 1);
  argv = (char **)stack;
  envp = (char **)stack + argc + 1;

  *argv[0] = '\0';

  while(argc--)
    show(argv[argc]);

  exit_success();
}

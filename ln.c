/* File: ln.c
   Time-stamp: <2011-09-06 20:17:20 gawen>

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

#include "tlibc.h"
#include "tlibc.c"

#define LN_WRAPPER "/bin/ln.real"

int main(int argc, char **argv)
{
  unsigned char use_symlink = 0;
  char *src;
  char *dst;

  /* anything that begins with '-' is an argument for real cat */
  for(i = 1 ; i < argc ; i++) {
    if(!strcmp(argv[i], "-s")) {
      use_symlink = 1;
      continue;
    }
    else if(*argv[i] == '-')
      execve(LN_WRAPPER, argv, environ);
  }

  if(use_symlink) {
    src = argv[2];
    dst = argv[3];
  }
  else {
    src = argv[1];
    dst = argv[2];
  }
}

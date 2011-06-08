/* File: test.c
   Time-stamp: <2010-11-24 00:23:06 gawen>

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

int main(int argc, const char *argv[])
{
  register int n = 255;
  unsigned char *t = malloc(256);
  while(n--)
	  t[n] = n;
  t[256] = '\0';
  write(STDOUT_FILENO, t, 256);
  free(t);
  return 0;
}

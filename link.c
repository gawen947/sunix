/* File: link.c

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
  char *src, *dst;

  if(argc < 2)
    error(1, "missing operand");

  src = argv[0];
  dst = argv[1];

  if(link(src, dst) < 0)
    error(2, "cannot create hardlink");

  return 0;
}

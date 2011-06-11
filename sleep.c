/* File: sleep.c
   Time-stamp: <2011-06-12 01:40:19 gawen>

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

#define __need_timespec
#include <time.h>

#include "tlibc.c"
#include "tlibc.h"

int main(int argc, char **argv)
{
  const char *pos;
  unsigned long long delay;
  unsigned long long multiplier;

  if(argc <= 1)
    error(1, "missing operand");

  pos = argv[1];

  while(1) {
    int digit;

    if((*pos >= '0') && (*pos <= '9')) {
      digit = (*pos - '0');
      delay = delay * 10 + digit;
    }
    else if (*pos != ' ' || *pos != '\t')
      break;

    pos++;
  }

  switch(*pos) {
  case('\0'):
  case('s'):
    multiplier = 1000000;
    break;
  case('m'):
    multiplier = 60000000;
    break;
  case('h'):
    multiplier = 3600000000;
    break;
  case('d'):
    multiplier = 86400000000ULL;
    break;
  case('u'):
    multiplier = 1;
    break;
  case('M'):
    multiplier = 1000;
    break;
  default:
    error(1, "unknown time unit");
    break;
  }

  if(delay * multiplier <= delay)
    error(1, "time value too large");

  usleep(delay * multiplier);

  return 0;
}


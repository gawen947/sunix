/* File: autorestart.c

   Copyright (C) 2010 David Hauweele <david@hauweele.net>

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

#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <err.h>

#define SLEEP_TIME 6000000

int main(int argc, char *argv[])
{
  pid_t pid;
  int n,i;

  /* check arguments */
  if(argc < 2)
    errx(EXIT_FAILURE,"usage [progname] [arg] ...");

  argv++;

  i = 0;

  while(1) {
    warnx("restart %d",i++);
    pid = fork();
    if(!pid) { /* child */
      n = execvp(argv[0],argv);
      warn("program stopped");
      usleep(SLEEP_TIME);
      exit(EXIT_FAILURE);
    }
    else if(pid < 0) /* error */
      err(EXIT_FAILURE,"fork error");
    /* parent */
    if(waitpid(pid,&n,0) == -1)
      warn("wait pid error");
  }
}


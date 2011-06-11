/* File: quickexec.c
   Time-stamp: <2011-06-11 15:22:47 gawen>

   Copyright (C) 2010 David Hauweele <david.hauweele@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <err.h>

/* MAX_ARGUMENTS means the maximum number of arguments
   where L_ARG_MAX and M_ARG_MAX means the maximum length
   of the argument string */
#define MAX_ARGUMENTS 1024
#define L_ARG_MAX 131072
#define M_ARG_MAX ((L_ARG_MAX < sysconf(_SC_ARG_MAX)) ? \
                   L_ARG_MAX : sysconf(_SC_ARG_MAX))

/* first line max length */
#define FL_MAX 1024

int main(int argc, char *argv[])
{
  char buf[M_ARG_MAX + FL_MAX];   /* effective size = M_ARG_MAX + FL_MAX */
  char *s_argv[MAX_ARGUMENTS+1];  /* effective size = MAX_ARGUMENTS + 1 */
  struct stat info;
  ssize_t n,i;
  size_t sofl; /* size of first line and file length */
  int fd, s_argc;

  /* check arguments */
  if(argc != 2)
    errx(EXIT_FAILURE,"path argument missing");

  /* open and read entire file */
  fd = open(argv[1],O_RDONLY);
  n = read(fd,buf,M_ARG_MAX + FL_MAX);
  if(n == -1)
    err(EXIT_FAILURE,"read failed");
  fstat(fd,&info);
  close(fd);

  /* check first string length and compare
     to real file size to determine effective
     argument string length */
  for(i = 0 ; buf[i] != '\n' && i != n; i++);
  if(M_ARG_MAX < info.st_size - i)
    errx(EXIT_FAILURE,"argument too long");

  /* replace newline by newstring
     and generate array of char [] */
  s_argc = 0;
  for(i = 0 ; i != n ; i++) {
    if(buf[i] == '\n') {
      /* replace newline character */
      buf[i] = '\0';

      /* index the current string into
         the array of arguments */
      s_argv[s_argc] = buf + i + 1;

      /* check that we are not out of bound */
      if(++s_argc > MAX_ARGUMENTS + 1)
        errx(EXIT_FAILURE,"too many arguments");
    }
  }

  /* ensure that the array of arguments is null terminated */
  s_argv[--s_argc] = NULL;

  /* execution */
  execvp(s_argv[0],s_argv);

  /* we should not get here
     it means an error occured */
  err(EXIT_FAILURE,"execution failed");

  /* avoid warning */
  return 0;
}

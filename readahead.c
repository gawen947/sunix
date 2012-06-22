1/* File: readahead.c
   Time-stamp: <2012-06-22 13:10:09 gawen>

   Copyright (c) 2011 David Hauweele <david@hauweele.net>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the University nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
   OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE. */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <err.h>
#include <ftw.h>

#define MAX_CHILDREN 10
#define MAX_FILESIZE 536870912
#define MIN_FORKSIZE 1048576

static long child;
static long max_child;

static void read_fork_regular(const char *fpath, int fd)
{
  pid_t pid = fork();

  switch(pid) {
  case(-1):
    warn("fork error");
  case(0):
    printf("read \"%s\"...\n", fpath);
    readahead(fd, 0, MAX_FILESIZE);
    exit(0);
    break;
  default:
    child++;
    break;
  }
}

static void read_regular(const char *fpath, int fd)
{
  printf("read \"%s\"...\n", fpath);
  readahead(fd, 0, MAX_FILESIZE);
}

static int read_path(const char *fpath, const struct stat *sb, int typeflag)
{
  int fd;

  switch(typeflag) {
  case(FTW_DNR):
    warnx("cannot read \"%s\"", fpath);
  case(FTW_D):
    break;
  case(FTW_NS):
    warn("stat failed on \"%s\"", fpath);
    break;
  case(FTW_F):
    /* open file */
    fd = open(fpath, O_RDONLY);
    if(fd < 0) {
      warn("cannot open \"%s\"", fpath);
      break;
    }

    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);

    /* check minimum fork size and children */
    if(sb->st_size > MIN_FORKSIZE && child < max_child)
      read_fork_regular(fpath, fd);
    else
      read_regular(fpath, fd);
    close(fd);
    break;
  default:
    break;
  }

  return 0;
}

static void sig_child(int signum)
{
  wait(NULL);
  child--;
}

int main(int argc, const char *argv[])
{
  struct sigaction act = { .sa_handler = sig_child };
  long max_open  = sysconf(_SC_OPEN_MAX);

  max_child = sysconf(_SC_CHILD_MAX);
  max_child = MAX_CHILDREN > max_child ? max_child : MAX_CHILDREN;

  /* setup the child catcher */
  sigemptyset(&act.sa_mask);
  sigaction(SIGCHLD, &act, NULL);

  /* two ways to run this program
     either using command line
     arguments either using stdin */
  if(argc > 1) {
    argv++;

    for(; *argv ; argv++)
      if(ftw(*argv, read_path, max_open) < 0)
        warn("cannot walk into \"%s\"", *argv);
  }
  else {
    while(!feof(stdin)) {
      char path[PATH_MAX];

      fgets(path, PATH_MAX, stdin);
      strtok(path, "\n");

      if(ftw(path, read_path, max_open) < 0)
        warn("cannot walk into \"%s\"", path);
    }
  }

  /* Wait for the remaining children. We have to remove the SIGCHLD signal. */
  signal(SIGCHLD, SIG_IGN);
  while(child--)
    wait(NULL);

  return 0;
}


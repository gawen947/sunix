/* File: rmdir.c
   Time-stamp: <2012-02-04 20:19:06 gawen>

   Copyright (c) 2012 David Hauweele <david@hauweele.net>
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

#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int pflag;
static int vflag;
static int eflag;

static void usage()
{
  (void)fprintf(stderr, "usage: rmdir [-pv] directory_name ...\n");
  exit(1);
}

static void do_rmdir(const char *path)
{
  if(vflag)
    printf("%s\n", path);

  if(rmdir(path) < 0)
    if(!eflag || errno != ENOTEMPTY)
      err(1, "cannot remove \"%s\"", path);
  
  if(vflag)
    printf("%s\n", path);
}

int main(int argc, char *argv[])
{
  enum opt { OPT_EMPTY };

  struct option opts[] = {
    { "parents", no_argument, NULL, 'p' },
    { "verbose", no_argument, NULL, 'v' },
    { "ignore-fail-on-non-empty", no_argument, NULL, OPT_EMPTY },
    { NULL, 0, NULL, 0 }
  };

  while(1) {
    int c = getopt_long(argc, argv, "pv", opts, NULL);

    if(c < 0)
      break;

    switch(c) {
    case 'p':
      pflag = 1;
      break;
    case 'v':
      vflag = 1;
      break;
    case OPT_EMPTY:
      eflag = 1;
      break;
    case '?':
    default:
      usage();
    }
  }

  argc -= optind;
  argv += optind;

  if(!*argv)
    usage();

  for(; *argv ; argv++) {
    do_rmdir(*argv);

    if(pflag) {
      size_t n = strlen(*argv);
      char *s;

      for(s = *argv + n ; s != *argv ; s--) {
        if(*s == '/') {
          *s = '\0';
          do_rmdir(*argv);
        }
      }
    }
  }

  exit(0);
}


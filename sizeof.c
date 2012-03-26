/* File: sizeof.c
   Time-stamp: <2012-03-26 18:25:27 gawen>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

#include "iobuf.h"

static int aflag;
static int Hflag;

static iofile_t out;

static void usage()
{
  (void)fprintf(stderr, "usage: [-daHh] files ...\n");
  exit(1);
}

static unsigned int human_size(char * buf, ssize_t size)
{
  const char *unit;
  unsigned int dec, fract;

  if(size < 1000) {
    unit  = "B";
    dec   = size;
    fract = 0;
  }
  else if(size < 1000000) {
    unit  = "kB";
    dec   = size / 1000;
    fract = (size % 1000) / 10;
  }
  else if(size < 1000000000L) {
    unit  = "MB";
    dec   = size / 1000000;
    fract = (size % 1000000) / 10000;
  }
  else if(size < 1000000000000LL) {
    unit  = "GB";
    dec   = size / 1000000000L;
    fract = (size % 1000000000L) / 10000000L;
  }
  else if(size < 1000000000000000LL) {
    unit  = "TB";
    dec   = size / 1000000000000LL;
    fract = (size % 1000000000000LL) / 10000000000LL;
  } else {
    unit  = "PB";
    dec   = size / 1000000000000000LL;
    fract = (size % 1000000000000000LL) / 10000000000000LL;
  }

  if(fract == 0)
    return sprintf(buf, "%d %s", dec, unit);
  else
    return sprintf(buf, "%d.%d %s", dec, fract, unit);
}

static void do_stat(const char *path)
{
  struct stat info;
  unsigned int len;
  ssize_t filesize;
  char buf[32];
  char nl = '\n', sp = ' ';

  if(stat(path, &info) < 0)
    err(1, "cannot stat \"%s\"", path);

  if(aflag)
    filesize = info.st_size;
  else
    filesize = 512 * info.st_blocks;

  if(Hflag)
    len = human_size(buf, filesize);
  else
    len = sprintf(buf, "%ld", filesize);

  iobuf_write(out, path, strlen(path));
  iobuf_write(out, &sp, 1);
  iobuf_write(out, buf, len);
  iobuf_write(out, &nl, 1);
}

int main(int argc, char *argv[])
{
  struct option opts[] = {
    { "disk-usage",    no_argument, NULL, 'd' },
    { "apparent-size", no_argument, NULL, 'a' },
    { "human",         no_argument, NULL, 'H' },
    { "help",          no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };

  while(1) {
    int c = getopt_long(argc, argv, "daHh", opts, NULL);

    if(c < 0)
      break;

    switch(c) {
    case('d'):
      aflag = 0;
      break;
    case('a'):
      aflag = 1;
      break;
    case('H'):
      Hflag = 1;
      break;
    case('h'):
    default:
      usage();
    }
  }

  argc -= optind;
  argv += optind;

  if(!argc)
    usage();

  out = iobuf_dopen(STDOUT_FILENO);
  for(; *argv ; argv++)
    do_stat(*argv);
  iobuf_close(out);

  exit(0);
}

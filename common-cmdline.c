/* File: fallback.c
   Time-stamp: <2012-10-16 15:47:25 gawen>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <getopt.h>

#include "fallback.h"

static void version(const char *name) 
{
#ifdef PARTIAL_COMMIT
  printf("%s - SUnix v%s (commit:%s)\n", name, VERSION, PARTIAL_COMMIT);
#else
  printf("%s - SUnix v%s\n", name, VERSION);
#endif
  exit(0);
}

static void commit(void)
{
  printf("%s\n", COMMIT);
  exit(0);
}

void common_main(int argc, char *argv[], const char *name, const char *real_path, void (*usage)(void), struct option opts[])
{
  int i = 0;
  for(; i < argc ; i++) {
    if(!strncmp(argv[i], "--", 2)) {
      /* Check for any optional long options. */
      if(opts != NULL) {
        for(; opts->name ; opts++)
          if(!strcmp(argv[i] + 2, opts->name))
            return;
      }

      if(!strcmp(argv[i], "--version"))
        version(name);
      else if(!strcmp(argv[i], "--help"))
        usage();
#ifdef COMMIT
      else if(!strcmp(argv[i], "--commit"))
        commit();
#endif
      else if(real_path != NULL)
        fallback_main(argc, argv, real_path);
    }
  }
}

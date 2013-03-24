/* File: record-invalid.c

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

#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>

#define INVALID_PATH "/var/log/invalid.sunix"

static const char * trimblank_nospace(const char *s)
{
  static char res[256];

  for(int i = 0 ; i < sizeof(res) ; i++) {
    if(isspace(s[i]) && s[i] != ' ')
      continue;
    res[i] = s[i];
  }

  return res;
}

void record_invalid(const char *prog_name, const char *option)
{
  const char *st;
  time_t t;
  FILE *f;

  time(&t);
  st = trimblank_nospace(ctime(&t));
  if(!(f = fopen(INVALID_PATH, "a")))
    return;
  fprintf(f, "%s (uid=%d) : %s : Invalid option '%s'\n",
          st, getuid(), prog_name, option);
  fclose(f);
}

void record_invalid_string(const char *prog_name, const char *op,
                           const char *msg)
{
  const char *st;
  time_t t;
  FILE *f;

  time(&t);
  st = trimblank_nospace(ctime(&t));
  if(!(f = fopen(INVALID_PATH, "a")))
    return;
  if(op)
    fprintf(f, "%s (uid=%d) : %s : (%s) %s\n",
            st, getuid(), prog_name, op, msg);
  else
    fprintf(f, "%s (uid=%d) : %s : %s\n", st, getuid(), prog_name, msg);
  fclose(f);
}

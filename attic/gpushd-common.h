/* File: gpushd-common.h

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

#ifndef _GPUSHD_COMMON_H_
#define _GPUSHD_COMMON_H_

#include <stdint.h>
#include <stdbool.h>

#include "gpushd.h"

#ifndef SUN_LEN
# define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path) \
                       + strlen ((ptr)->sun_path))
#endif /* SUN_LEN */

#define UNIX_PATH_MAX 108 /* we don't really need to define this one */
#define MAX_PATH      256
#define IOSIZE        256

enum state { ST_CMD,
             ST_STR,
             ST_INT,
             ST_PROCEED };

struct parse_state {
  enum state state;
  int p_idx;

  struct message {
    enum cmd command;
    char p_string[MAX_ENTRY];
    union {
      int     value;
      uint8_t bytes[sizeof(int)];
    } p_int;
  } msg;
};

void send_error(int remote, int code);
const char * str_error(int code);
bool parse(int remote, struct parse_state *state,
           bool (*proceed)(int, struct message *));

#endif /* _GPUSHD_COMMON_H_ */

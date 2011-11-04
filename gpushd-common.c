/* File: gpushd-common.c
   Time-stamp: <2011-11-04 16:29:54 gawen>

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

#define _POSIX_SOURCE 1

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sysexits.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <err.h>

#include "safe-call.h"
#include "gpushd.h"
#include "gpushd-common.h"

void send_error(int remote, int code)
{
  char cmd = CMD_ERROR;

  write(remote, &cmd, sizeof(char));
  write(remote, &code, sizeof(int));
}

const char * str_error(int code)
{
  const char *s;

  switch(code) {
  case(E_PERM):
    s = "Permission denied";
    break;
  case(E_INVAL):
    s = "Invalid command";
    break;
  case(E_EMPTY):
    s = "Empty stack";
    break;
  case(E_NFOUND):
    s = "Not found";
    break;
  case(E_LONG):
    s = "Too long";
    break;
  case(E_FULL):
    s = "Stack full";
    break;
  default:
    s = "Unknown error";
    break;
  }

  return s;
}

bool parse(int remote, struct parse_state *state,
           bool (*proceed)(int, struct message *))
{
  int i = 0;
  char buf[IOSIZE];

  ssize_t n = xrecv(remote, buf, IOSIZE, 0);

  if(!n) {
    warnx("remote disconnected");
    return false;
  }

  do {
    switch(state->state) {
      int j;
    case(ST_CMD):
      state->p_idx       = 0;
      state->msg.command = buf[i++];

      switch(state->msg.command) {
      case(CMD_PUSH):
      case(CMD_RESPS):
        state->state = ST_STR;
        break;
      case(CMD_POP):
      case(CMD_GET):
      case(CMD_RESPI):
      case(CMD_ERROR):
        state->state = ST_INT;
        break;
      case(CMD_END):
      case(CMD_QUIT):
      case(CMD_SIZE):
      case(CMD_NBCLI):
      case(CMD_NBSRV):
      case(CMD_NBRCV):
      case(CMD_NBSND):
      case(CMD_NBERR):
      case(CMD_MAXNSEC):
      case(CMD_MINNSEC):
      case(CMD_SUMNSEC):
      case(CMD_GETF):
      case(CMD_POPF):
      case(CMD_CLEAN):
      case(CMD_GETALL):
        state->state = ST_CMD;
        if(!proceed(remote, &state->msg))
          return false;
        break;
      default:
        send_error(remote, E_INVAL);
        return false;
      }

      break;
    case(ST_STR):
      for(j = state->p_idx ; i < n ; j++, i++) {
        state->msg.p_string[j] = buf[i];
        if(buf[i] == '\0') {
          state->state = ST_CMD;
          if(!proceed(remote, &state->msg))
            return false;
          i++;
          break;
        }
        else if(j == MAX_PATH) {
          send_error(remote, E_LONG);
          return false;
        }
      }

      state->p_idx = j;
      break;
    case(ST_INT):
      for(j = state->p_idx ; i < n ; j++, i++) {
        /* since we use unix domain sockets we stick
           to the same architecture and don't bother
           about endianess */
        state->msg.p_int.bytes[j] = buf[i];
        if(j == sizeof(int) - 1) {
          state->state = ST_CMD;
          if(!proceed(remote, &state->msg))
            return false;
          i++;
          break;
        }
      }

      state->p_idx = j;
      break;
    default:
      assert(false); /* unknown parsing state */
    }
  } while(i < n);

  return true;
}

/* File: gpushd-client.c
   Time-stamp: <2011-10-30 11:56:40 gawen>

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

#define UNIX_PATH_MAX 108

#ifndef SUN_LEN
# define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path) \
                       + strlen ((ptr)->sun_path))
#endif /* SUN_LEN */

static const char *prog_name;
static const char *sock_path;
static bool stay = false;

static struct request {
  struct request *next;

  enum cmd_cli command;
  const char *d_path;
  int p_int;
} *req_stack;

static opts_name {
  char name_short;
  const char *name_long;
  const char *help;
};

static void add_request(enum cmd_cli command, const char *path, int code)
{
  /* @FIXME: should FIFO instead */
  struct request *req = xmalloc(sizeof(struct request));

  req.next    = req_stack;
  req.command = command;
  req.d_path  = path;
  req.p_int   = code;

  req_stack = req;
}

static void help(const struct opts_name *names)
{
  const struct opts_name *opt;
  int size;
  int max = 0;

  fprintf(stderr, "Usage: %s [OPTIONS] [SOCKET-PATH]\n", pgn);

  /* maximum option name size for padding */
  for(opt = names ; opt->name_long ; opt++) {
    size = strlen(opt->name_long);
    if(size > max)
      max = size;
  }

  /* print options and help messages */
  for(opt = names ; opt->name_long ; opt++) {
    if(opt->name_short != 0)
      fprintf(stderr, "  -%c, --%s", opt->name_short, opt->name_long);
    else
      fprintf(stderr, "      --%s", opt->name_long);

    /* padding */
    size = strlen(opt->name_long);
    for(; size <= max ; size++)
      fputc(' ', stderr);
    fprintf(stderr, "%s\n", opt->help);
  }
}

static void cmdline(int argc, const char *argv[], const char *cwd)
{
  enum opt { OPT_PUSH   = 'p',
             OPT_POP    = 'P',
             OPT_CLEAN  = 'c',
             OPT_GET    = 'g',
             OPT_GETALL = 'G',
             OPT_SIZE   = 's'
             OPT_STAY   = 'S' };

  struct opts_name names[] = {
    { 'p', "push",    "Push the current directory" },
    { 'P', "pop",     "Pop a directory from the stack" },
    { 'c', "clean",   "Clean the stack" },
    { 'g', "get",     "Get a directory, without removing it" },
    { 'G', "get-all", "Get and print all directory from the stack" },
    { 's', "size",    "Get the stack size" },
    { 'S', "stay",    "Do not change directory on get or pop" },
    { 0, NULL, NULL }
  };

  struct option opts[] = {
    { "push",    optional_argument, NULL, OPT_PUSH },
    { "pop",     optional_argument, NULL, OPT_POP },
    { "clean",   no_argument, NULL, OPT_CLEAN },
    { "get",     optional_argument, NULL, OPT_GET },
    { "get-all", no_argument, NULL, OPT_GETALL },
    { "size",    no_argument, NULL, OPT_SIZE },
    { "stay",    no_argument, NULL, OPT_STAY }
    { NULL, 0, NULL, 0 }
  };

  while(1) {
    int c = getopt_long(argc, argv, "p:P:cg:GsS", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
    case(OPT_PUSH):
      if(optarg)
        add_request(CMD_PUSH, cwd, atoi(optarg));
      else
        add_request(CMD_PUSHF, cwd, 0);
      break;
    case(OPT_POP):
      if(optarg)
        add_request(CMD_POP, cwd, atoi(optarg));
      else
        add_request(CMD_POPF, cwd, 0);
      break;
    case(OPT_CLEAN):
      add_request(CMD_CLEAN, NULL, 0);
      break;
    case(OPT_GET):
      if(optarg)
        add_request(CMD_GET, NULL, atoi(optarg));
      else
        add_request(CMD_GETF, NULL, 0);
      break;
    case(OPT_GETALL):
      add_request(CMD_GETALL, NULL, 0);
      break;
    case(OPT_SIZE):
      add_request(CMD_SIZE, NULL, 0);
      break;
    case(OPT_STAY):
      stay = true;
      break;
    case(OPT_HELP):
      exit_status = EXIT_SUCCESS;
    default:
      help(names);
      exit(exit_status);
    }
  }

  /* consider remaining arguments */
  if(argc - optind != 1)
    errx(EXIT_FAILURE, "except socket path");
  sock_path = argv[optind];
}

static void proceed_request(const struct request *request, int srv)
{
  char cmd = request->command;

  write(srv, &cmd, sizeof(char));

  switch(request->command) {
  case(CMD_PUSH):
    write(srv, request->d_path, strlen(request->d_path));
    break;
  case(CMD_POP):
  case(CMD_INT):
    write(srv, request->p_int, sizeof(int));
    break;
  case(CMD_POPF):
  case(CMD_GETF):
  case(CMD_CLEAN):
  case(CMD_GETALL):
  case(CMD_QUIT):
    break;
  case(CMD_END):
  case(CMD_RESPS):
  case(CMD_RESPI):
  case(CMD_SIZE):
  case(CMD_ERROR):
  default:
    assert(false); /* not implemented */
  }
}

static bool proceed_server(int srv, struct cli_state *state)
{
  int i = 0;
  char buf[IOSIZE];

  ssize_t n = xread(srv, buf, IOSIZE);

  if(!n) {
    warnx("server disconnected");
    return false;
  }

  do {
    switch(state->state) {
      int j;

    case(ST_CMD):
      state->p_idx           = 0;
      state->request.command = buf[i++];

      switch(state->request.command) {
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
      case(CMD_GETF):
      case(CMD_POPF):
      case(CMD_CLEAN):
      case(CMD_GETALL):
        state->state = ST_PROCEED;
        break;
      default:
        send_error(cli, E_INVAL);
        return false;
      }

      break;
    case(ST_STR):
      for(j = state->p_idx ; i != n ; j++, i++) {
        state->request.p_string[j] = buf[i];
        if(buf[i] == '\0') {
          state->state = ST_PROCEED;
          break;
        }
        else if(j == MAX_PATH) {
          send_error(cli, E_LONG);
          return false;
        }
      }

      state->p_idx = j;
      break;
    case(ST_INT):
      for(j = state->p_idx ; i != n ; j++, i++) {
        /* since we use unix domain sockets we stick
           to the same architecture and don't bother
           about endianess */
        state->request.p_int.bytes[j] = buf[i];
        if(j == sizeof(int)) {
          state->state = ST_PROCEED;
          break;
        }
      }

      state->p_idx = j;
      break;
    case(ST_PROCEED):
      if(!proceed_request(cli, &state->request))
        return false;
      break;
    default:
      assert(false); /* unknown parsing state */
    }
  } while(i != n);

  return true;
}

static void proceed_request_stack(int srv)
{
  struct request *r;

  for(r = request_stack ; r != NULL ; r = r->next) {
    proceed_request(r, srv);
    if(!proceed_server(srv)) {
      warnx("disconnected due to error");
      break;
    }
  }
}

static client()
{
  int sd;
  struct request quit       = { .command    = CMD_QUIT };
  struct sockaddr_un s_addr = { .sun_family = AF_UNIX };

  /* socket creation */
  sd = xsocket(AF_UNIX, SOCK_STREAM, 0);

  /* bind socket to the specified sock_path */
  strncpy(s_addr.sun_path, sock_path, UNIX_PATH_MAX);

  /* connect to the server */
  xconnect(sd, (struct sockaddr *)&s_addr, SUN_LEN(s_addr));

  proceed_request_stack(sd);

  /* quit only after the request has been proceeded */
  proceed_request(&quit, sd);
}

int main(int argc, const char *argv[])
{
  char cwd[MAX_PATH];

  /* get current working directory */
  getcwd(cwd, MAX_PATH);

  /* get program name */
  pgn = (const char *)strrchr(argv[0], '/');
  pgn = pgn ? (pgn + 1) : argv[0];

  /* parse command line and build
     the request stack */
  cmdline(argc, argv, cwd);

  /* start client */
  client();

  return EXIT_SUCCESS;
}

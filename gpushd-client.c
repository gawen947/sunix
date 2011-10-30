/* File: gpushd-client.c
   Time-stamp: <2011-10-30 19:39:10 gawen>

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
#include <getopt.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <err.h>

#include "gpushd-common.h"
#include "safe-call.h"
#include "gpushd.h"

#ifndef SUN_LEN
# define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path) \
                       + strlen ((ptr)->sun_path))
#endif /* SUN_LEN */

static const char *prog_name;
static const char *sock_path;
static bool stay = false;
static enum cmd current_request;
static int getall_counter;

static struct request {
  struct request *next;

  enum cmd command;
  const char *d_path;
  int p_int;
} *req_stack;
static struct request *tail_request;

struct opts_name {
  char name_short;
  const char *name_long;
  const char *help;
};

static void add_request(enum cmd command, const char *path, int code)
{
  struct request *req = xmalloc(sizeof(struct request));

  req->next    = NULL;
  req->command = command;
  req->d_path  = path;
  req->p_int   = code;

  if(!req_stack)
    req_stack = req;
  if(!tail_request)
    tail_request = req;
  else
    tail_request->next = req;
}

static void help(const struct opts_name *names)
{
  const struct opts_name *opt;
  int size;
  int max = 0;

  fprintf(stderr, "Usage: %s [OPTIONS] [SOCKET-PATH]\n", prog_name);

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

static void cmdline(int argc, char * const argv[], const char *cwd)
{
  int exit_status = EXIT_FAILURE;

  enum opt { OPT_PUSH   = 'p',
             OPT_POP    = 'P',
             OPT_CLEAN  = 'c',
             OPT_GET    = 'g',
             OPT_GETALL = 'G',
             OPT_SIZE   = 's',
             OPT_STAY   = 'S',
             OPT_SOCKET = 'f',
             OPT_HELP   = 'h' };

  struct opts_name names[] = {
    { 'p', "push",    "Push the current directory" },
    { 'P', "pop",     "Pop a directory from the stack" },
    { 'c', "clean",   "Clean the stack" },
    { 'g', "get",     "Get a directory, without removing it" },
    { 'G', "get-all", "Get and print all directory from the stack" },
    { 's', "size",    "Get the stack size" },
    { 'S', "stay",    "Do not change directory on get or pop" },
    { 'f', "socket",  "Socket path" },
    { 'h', "help",    "Show this help message" },
    { 0, NULL, NULL }
  };

  struct option opts[] = {
    { "push",    optional_argument, NULL, OPT_PUSH },
    { "pop",     optional_argument, NULL, OPT_POP },
    { "clean",   no_argument, NULL, OPT_CLEAN },
    { "get",     optional_argument, NULL, OPT_GET },
    { "get-all", no_argument, NULL, OPT_GETALL },
    { "size",    no_argument, NULL, OPT_SIZE },
    { "stay",    no_argument, NULL, OPT_STAY },
    { "socket",  required_argument, NULL, OPT_SOCKET },
    { "help",    no_argument, NULL, OPT_HELP },
    { NULL, 0, NULL, 0 }
  };

  while(1) {
    int c = getopt_long(argc, argv, "p:P:cg:GsSf:h", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
    case(OPT_PUSH):
      if(optarg)
        add_request(CMD_PUSH, optarg, 0);
      else
        add_request(CMD_PUSH, cwd, 0);
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
    case(OPT_SOCKET):
      sock_path = optarg;
      break;
    case(OPT_HELP):
      exit_status = EXIT_SUCCESS;
    default:
      help(names);
      exit(exit_status);
    }
  }

  /* check for socket path */
  if(!sock_path)
    errx(EXIT_FAILURE, "except socket path");
}

static void proceed_request(int srv, const struct request *request)
{
  char cmd = request->command;

  current_request = cmd;

  write(srv, &cmd, sizeof(char));

  switch(request->command) {
  case(CMD_PUSH):
    write(srv, request->d_path, strlen(request->d_path) + 1);
    break;
  case(CMD_POP):
  case(CMD_GET):
    write(srv, &request->p_int, sizeof(int));
    break;
  case(CMD_GETALL):
    getall_counter = 0;
  case(CMD_POPF):
  case(CMD_GETF):
  case(CMD_CLEAN):
  case(CMD_SIZE):
  case(CMD_QUIT):
    break;
  case(CMD_END):
  case(CMD_RESPS):
  case(CMD_RESPI):
  case(CMD_ERROR):
  default:
    assert(false); /* not implemented */
  }
}

static bool cmd_resps(int srv, struct message *response)
{
  switch(current_request) {
  case(CMD_QUIT):
  case(CMD_END):
  case(CMD_PUSH):
  case(CMD_CLEAN):
  case(CMD_RESPS):
  case(CMD_RESPI):
  case(CMD_SIZE):
    warnx("unexpected string from server");
    send_error(srv, E_INVAL);
    break;
  case(CMD_POP):
  case(CMD_POPF):
  case(CMD_GET):
  case(CMD_GETF):
    printf("- %s\n", response->p_string);
    if(!stay)
      chdir(response->p_string);
    break;
  case(CMD_GETALL):
    printf("%d - %s\n", getall_counter++, response->p_string);
    break;
  default:
    break;
  }

  return true;
}

static bool cmd_respi(int srv, struct message *response)
{
  switch(current_request) {
  case(CMD_QUIT):
  case(CMD_END):
  case(CMD_PUSH):
  case(CMD_CLEAN):
  case(CMD_RESPS):
  case(CMD_POP):
  case(CMD_POPF):
  case(CMD_GET):
  case(CMD_RESPI):
  case(CMD_GETF):
  case(CMD_GETALL):
    warnx("unexpected integer from server");
    send_error(srv, E_INVAL);
    break;
  case(CMD_SIZE):
    printf("Stack size : %d\n", response->p_int.value);
    break;
  default:
    break;
  }

  return true;
}

static bool proceed_response(int srv, struct message *response)
{
  switch(response->command) {
  case(CMD_QUIT):
  case(CMD_END):
    return false;
  case(CMD_PUSH):
  case(CMD_POP):
  case(CMD_POPF):
  case(CMD_CLEAN):
  case(CMD_GET):
  case(CMD_GETF):
  case(CMD_GETALL):
  case(CMD_SIZE):
    warnx("received invalid command %d from server", response->command);
    send_error(srv, E_INVAL);
    return true;
  case(CMD_RESPS):
    return cmd_resps(srv, response);
  case(CMD_RESPI):
    return cmd_respi(srv, response);
  default:
    assert(false); /* unknown command */
  }

  return false;
}

static void proceed_request_stack(int srv)
{
  struct parse_state srv_state = { .state = ST_CMD,
                                   .p_idx = 0 };
  struct request *r;

  for(r = req_stack ; r != NULL ; r = r->next) {
    proceed_request(srv, r);
    while(parse(srv, &srv_state, proceed_response));
  }
}

static void client()
{
  int sd;
  struct request quit       = { .command    = CMD_QUIT };
  struct sockaddr_un s_addr = { .sun_family = AF_UNIX };

  /* socket creation */
  sd = xsocket(AF_UNIX, SOCK_STREAM, 0);

  /* bind socket to the specified sock_path */
  strncpy(s_addr.sun_path, sock_path, UNIX_PATH_MAX);

  /* connect to the server */
  xconnect(sd, (struct sockaddr *)&s_addr, SUN_LEN(&s_addr));

  proceed_request_stack(sd);

  /* quit only after the request has been proceeded */
  proceed_request(sd, &quit);
}

int main(int argc, char * const argv[])
{
  char cwd[MAX_PATH];

  /* get current working directory */
  getcwd(cwd, MAX_PATH);

  /* get program name */
  prog_name = (const char *)strrchr(argv[0], '/');
  prog_name = prog_name ? (prog_name + 1) : argv[0];

  /* parse command line and build
     the request stack */
  cmdline(argc, argv, cwd);

  /* start client */
  client();

  return EXIT_SUCCESS;
}

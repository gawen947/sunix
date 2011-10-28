/* File: gpushd-server.c
   Time-stamp: <2011-10-29 01:23:01 gawen>

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

#include "gpushd.h"

#define MAX_CONCURRENCY 16
#define MAX_PATH        256
#define MAX_STACK       65535
#define IOSIZE          256
#define REQUEST_TIMEOUT 1

#define CHECK_BIT(bit, flag) ((flag) & (1 << (bit)))
#define SET_BIT(bit, flag) ((flag) |= (1 << (bit)))
#define CLEAR_BIT(bit, flag) ((flag) |= ~(1 << (bit)))

enum st_cli { ST_CMD,
              ST_STR,
              ST_INT,
              ST_PROCEED };

static struct thread_pool {
  sem_t available;
  unsigned int idx;
  phtread_t threads[MAX_CONCURRENCY]; /* threads */
  int       fd_cli[MAX_CONCURRENCY];  /* client file descriptor */
  uint16_t  st_threads;               /* threads state */
} pool;

static struct dir_stack {
  sem_t mutex;
  size_t size;

  struct d_node {
    struct d_node *next;

    char d_path[MAX_PATH];
  } *dirs;
} stack;

struct cli_state {
  st_cli state;
  int p_idx;

  struct request {
    cmd_cli command;

    char p_string[MAX_PATH];
    union integer {
      int     value;
      uint8_t bytes[sizeof(int)];
    } p_int;
  } current_request;
};

static void cli_timeout(int signum)
{
  /* one problem with that,
     thread aborted but connection
     stall */
  warnx("client timeout");
  pthread_exit(NULL);
}

static void send_error(int cli, int code)
{
  struct {
    char cmd;
    int code;
  } message = { CMD_ERROR, code };

  write(cli, &message, sizeof(message));
}

static bool cmd_push(int cli, struct request *request)
{
  struct d_node *new = xmalloc(sizeof(struct d_node));

  strcpy(new.d_path, request->p_string);

  /* stack critical read-write section */
  sem_wait(&stack.mutex);
  {
    if(stack.size == MAX_STACK) {
      sem_post(&stack.mutex);  /* release early */
      send_error(cli, E_FULL);
      free(new);
      return true;
    }
    new.next   = stack.dirs;
    stack.dirs = new;
    stack.size++;
  }
  sem_post(&stack.mutex);

  return true;
}

static bool cmd_pop(int cli, struct request *request)
{
  struct d_node result;
  struct d_node *c = stack.dirs, *o = NULL;
  int i, j = request->p_int.value;
  char cmd = CMD_RESPS;
  size_t path_size;

  /* stack critical read-write section */
  sem_wait(&stack.mutex);
  {
    for(i = 0 ; i < j ; i++, c = c.next) {
      if(c == NULL) {
        sem_post(&stack.mutex); /* release early */
        send_error(cli, E_NFOUND);
        return true;
      }

      o = c;
    }

    /* save result */
    result = *c;

    /* free the node */
    if(!o)
      stack.dirs = c.next;
    else
      o.next = c.next;
    stack.size--;
    free(c);
  }
  sem_post(&stack.mutex);

  /* we don't send the response
     inside the critical section */
  path_size = strlen(result.d_path);
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, path_size);

  return true;
}

static bool cmd_popf(int cli, struct request *request)
{
  struct d_node *c = stack.dirs;
  struct d_node result;
  char cmd = CMD_RESPS;
  size_t path_size;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    stack.dirs = c.next;
    result = *c;
    free(c);
  }
  sem_post(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  path_size = strlen(result.d_path);
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, path_size);

  return true;
}

static bool cmd_clean(int cli, struct request *request)
{
  struct d_node *c = stack.dirs;

  /* stack critical write section */
  sem_wait(&stack.mutex);
  {
    while(c != NULL) {
      struct d_node *o = c;

      c = c.next;
      free(o);
    }

    stack.size = 0;
  }
  sem_post(&stack.mutex);

  return true;
}

static bool cmd_get(int cli, struct request *request)
{
  int i;
  struct d_node *c = stack.dirs;
  struct d_node result;
  char cmd = CMD_RESPS;
  size_t path_size;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    for(i = 0 ; i < j ; i++, c = c.next) {
      if(c == NULL) {
        sem_post(&stack.mutex); /* release early */
        send_error(cli, E_NFOUND);
        return true;
      }
    }

    result = *c;
  }
  sem_post(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  path_size = strlen(result.d_path);
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, path_size);

  return true;
}

static bool cmd_getf(int cli, struct request *request)
{
  struct d_node result;
  char cmd = CMD_RESPS;
  size_t path_size;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    result = *stack.dirs;
  }
  sem_post(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  path_size = strlen(result.d_path);
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, path_size);

  return true;
}

static bool cmd_size(int cli, struct request *request)
{
  size_t size;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    size = stack.size;
  }
  sem_post(&stack.mutex);

  return true;
}

static bool cmd_getall(int cli, struct request *request)
{
  struct d_node *c = stack.dirs;
  char cmd = CMD_RESPALLS;

  write(cli, &cmd, sizeof(char));

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    /* we got no choice here but to send
       the message inside the critical section */
    for(; c != NULL ; c = c.next)
      write(cli, c->d_path, strlen(c->d_path));
  }
  sem_post(&stack.mutex);

  return true;
}

static bool proceed_request(int cli, struct request *request)
{
  switch(request->command) {
  case(CMD_QUIT):
    return false;
  case(CMD_RESPI):
  case(CMD_RESPS):
  case(CMD_RESPALLS):
  case(CMD_ERROR):
    warnx("received invalid command %d from client", request->command);
    send_error(cli, E_PERM);
    return true;
  case(CMD_PUSH):
    return cmd_push(cli, request);
  case(CMD_POP):
    return cmd_pop(cli, request);
  case(CMD_POPF):
    return cmd_popf(cli, request);
  case(CMD_CLEAN):
    return cmd_clean(cli, request);
  case(CMD_GET):
    return cmd_get(cli, request);
  case(CMD_GETF):
    return cmd_getf(cli, request);
  case(CMD_GETALL):
    return cmd_getall(cli, request);
  case(CMD_SIZE):
    return cmd_size(cli, request);
  default:
    assert(false); /* unknown command */
  }

  return false;
}

static bool proceed_client(int cli, struct cli_state *state)
{
  int i = 0;
  char buf[IOSIZE];

  ssize_t n = xread(cli, buf, IOSIZE);

  if(!n) {
    warnx("client disconnected");
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

static void * new_cli(void *arg)
{
  int idx = (int)arg;
  struct cli_state cli = { .state = ST_CMD,
                           .p_idx = 0 };
  struct sigaction act = { .sa_handler = cli_timeout,
                           .sa_flags   = 0 };

  /* ensure this thread won't live more than REQUEST_TIMEOUT seconds */
  sigfillset(&act.mask);
  sigaction(SIGALRM, &act, NULL);

  alarm(REQUEST_TIMEOUT);

  while(proceed_client(pool.fd_cli[idx], &cli));

  /* free the thread slot */
  CLEAR_BIT(idx, pool.st_threads);
  sem_post(&pool.available);

  return NULL;
}

static void server(const char *sock_path)
{
  int sd;
  struct sockaddr_un s_addr = {0};

  /* socket creation */
  sd = xsocket(AF_UNIX, SOCK_STREAM, 0);

  /* bind socket to the specified sock_path */
  strncpy(s_addr.sun_path, sock_path, UNIX_PATH_MAX);
  xbind(sd, (struct sockaddr *)&s_addr, SUN_LEN(&s_addr));

  /* listen and backlog up to five connections */
  xlisten(sd, 8);
  while(1) {
    int i, fd = xaccept(sd, NULL, NULL);

    /* wait for the first available thread */
    sem_wait(&pool.available);

    for(i = pool.idx ; !CHECK_BIT(i, pool.st_threads) ;
        i = (i + 1) % MAX_CONCURRENCY);

    /* setup client thread */
    SET_BIT(i, pool.st_threads);
    pool.fd_cli[i] = fd;

    if(pthread_create(&pool.threads[i], NULL, new_cli, (void *)i))
      err(EXIT_FAILURE, "cannot create thread");

    pool.idx = (pool.idx + 1) % MAX_CONCURRENCY;
  }
}

int main(int argc, char *argv[])
{
  if(argc != 2)
    errx(EXIT_FAILURE, "usage <socket-path>");

  xsem_init(&pool.available, 0, MAX_CONCURRENCY);
  xsem_init(&stack.mutex, 0, 1);
}

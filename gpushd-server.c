/* File: gpushd-server.c
   Time-stamp: <2011-10-30 21:52:05 gawen>

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

#include "gpushd-common.h"
#include "safe-call.h"
#include "gpushd.h"

#define MAX_CONCURRENCY 16
#define MAX_STACK       65535
#define REQUEST_TIMEOUT 1

#define CHECK_BIT(bit, flag) ((flag) & (1 << (bit)))
#define SET_BIT(bit, flag) ((flag) |= (1 << (bit)))
#define CLEAR_BIT(bit, flag) ((flag) &= ~(1 << (bit)))

static const char *sock_path;

static struct thread_pool {
  sem_t available;
  unsigned int idx;
  pthread_t threads[MAX_CONCURRENCY]; /* threads */
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

static void exit_clean()
{
  unlink(sock_path);
}

static void signal_clean(int signum)
{
  exit_clean();
}

static void cli_timeout(int signum)
{
  /* one problem with that,
     thread aborted but connection
     stall : also note that this
     doesn't work at all */
  warnx("client timeout");
  pthread_exit(NULL);
}

static bool cmd_push(int cli, struct message *request)
{
  struct d_node *new = xmalloc(sizeof(struct d_node));

  strcpy(new->d_path, request->p_string);

  /* stack critical read-write section */
  sem_wait(&stack.mutex);
  {
    if(stack.size == MAX_STACK) {
      sem_post(&stack.mutex);  /* release early */
      send_error(cli, E_FULL);
      free(new);
      return true;
    }

    new->next  = stack.dirs;
    stack.dirs = new;
    stack.size++;
  }
  sem_post(&stack.mutex);

  return true;
}

static bool cmd_pop(int cli, struct message *request)
{
  struct d_node result;
  struct d_node *c = stack.dirs, *o = NULL;
  int i, j = request->p_int.value;
  char cmd = CMD_RESPS;

  /* stack critical read-write section */
  sem_wait(&stack.mutex);
  {
    for(i = 0 ; i < j ; i++, c = c->next) {
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
      stack.dirs = c->next;
    else
      o->next = c->next;
    stack.size--;
    free(c);
  }
  sem_post(&stack.mutex);

  /* we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, strlen(result.d_path) + 1);

  return true;
}

static bool cmd_popf(int cli, struct message *request)
{
  struct d_node *c = stack.dirs;
  struct d_node result;
  char cmd = CMD_RESPS;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    if(!stack.dirs) {
      sem_post(&stack.mutex);
      send_error(cli, E_NFOUND);
      return true;
    }

    stack.dirs = c->next;
    result = *c;
    free(c);
  }
  sem_post(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, strlen(result.d_path) + 1);

  return true;
}

static bool cmd_clean(int cli, struct message *request)
{
  struct d_node *c = stack.dirs;

  /* stack critical write section */
  sem_wait(&stack.mutex);
  {
    while(c != NULL) {
      struct d_node *o = c;

      c = c->next;
      free(o);
    }
    stack.dirs = NULL;
    stack.size = 0;
  }
  sem_post(&stack.mutex);

  return true;
}

static bool cmd_get(int cli, struct message *request)
{
  int i, j = request->p_int.value;
  struct d_node *c = stack.dirs;
  struct d_node result;
  char cmd = CMD_RESPS;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    for(i = 0 ; i < j ; i++, c = c->next) {
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
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, strlen(result.d_path) + 1);

  return true;
}

static bool cmd_getf(int cli, struct message *request)
{
  struct d_node result;
  char cmd = CMD_RESPS;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    if(!stack.dirs) {
      sem_post(&stack.mutex);
      send_error(cli, E_NFOUND);
      return true;
    }
    result = *stack.dirs;
  }
  sem_post(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, strlen(result.d_path) + 1);

  return true;
}

static bool cmd_size(int cli, struct message *request)
{
  int size;
  char cmd = CMD_RESPI;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    size = stack.size;
  }
  sem_post(&stack.mutex);

  write(cli, &cmd, sizeof(char));
  write(cli, &size, sizeof(int));

  return true;
}

static bool cmd_getall(int cli, struct message *request)
{
  struct d_node *c = stack.dirs;
  char cmd = CMD_RESPS;

  /* stack critical read section */
  sem_wait(&stack.mutex);
  {
    /* we got no choice here but to send
       the message inside the critical section */
    for(; c != NULL ; c = c->next) {
      write(cli, &cmd, sizeof(char));
      write(cli, c->d_path, strlen(c->d_path) + 1);
    }
  }
  sem_post(&stack.mutex);

  return true;
}

static bool cmd_error(int cli, struct message *request)
{
  warnx("received error from client %d with %s",
        str_error(request->p_int.value));

  return true;
}

static bool proceed_request(int cli, struct message *request)
{
  bool result = false;
  char end = CMD_END;

  switch(request->command) {
  case(CMD_QUIT):
    return false;
  case(CMD_END):
  case(CMD_RESPI):
  case(CMD_RESPS):
    warnx("received invalid command %d from client", request->command);
    send_error(cli, E_PERM);
    result = true;
    break;
  case(CMD_ERROR):
    result = cmd_error(cli, request);
    break;
  case(CMD_PUSH):
    result = cmd_push(cli, request);
    break;
  case(CMD_POP):
    result = cmd_pop(cli, request);
    break;
  case(CMD_POPF):
    result = cmd_popf(cli, request);
    break;
  case(CMD_CLEAN):
    result = cmd_clean(cli, request);
    break;
  case(CMD_GET):
    result = cmd_get(cli, request);
    break;
  case(CMD_GETF):
    result = cmd_getf(cli, request);
    break;
  case(CMD_GETALL):
    result = cmd_getall(cli, request);
    break;
  case(CMD_SIZE):
    result = cmd_size(cli, request);
    break;
  default:
    assert(false); /* unknown command */
  }

  write(cli, &end, sizeof(char));

  return result;
}

static void * new_cli(void *arg)
{
  int idx = (long)arg;
  struct parse_state cli = { .state = ST_CMD,
                             .p_idx = 0 };
  struct sigaction act   = { .sa_handler = cli_timeout,
                             .sa_flags   = 0 };

  /* ensure this thread won't live more than REQUEST_TIMEOUT seconds */
  sigfillset(&act.sa_mask);
  sigaction(SIGALRM, &act, NULL);

  alarm(REQUEST_TIMEOUT);

  while(parse(pool.fd_cli[idx], &cli, proceed_request));

  close(pool.fd_cli[idx]);

  /* free the thread slot */
  CLEAR_BIT(idx, pool.st_threads);
  sem_post(&pool.available);

  alarm(0);

  return NULL;
}

static void server(const char *sock_path)
{
  int sd;
  struct sockaddr_un s_addr = { .sun_family = AF_UNIX };

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

    for(i = pool.idx ; CHECK_BIT(i, pool.st_threads) ;
        i = (i + 1) % MAX_CONCURRENCY);

    /* setup client thread */
    SET_BIT(i, pool.st_threads);
    pool.fd_cli[i] = fd;

    if(pthread_create(&pool.threads[i], NULL, new_cli, (void *)(long)i))
      err(EXIT_FAILURE, "cannot create thread");

    pool.idx = (pool.idx + 1) % MAX_CONCURRENCY;
  }
}

int main(int argc, const char *argv[])
{
  struct sigaction act = { .sa_handler = signal_clean,
                           .sa_flags   = 0 };

  if(argc != 2)
    errx(EXIT_FAILURE, "usage <socket-path>");

  xsem_init(&pool.available, 0, MAX_CONCURRENCY);
  xsem_init(&stack.mutex, 0, 1);

  /* unlink socket on exit */
  sigfillset(&act.sa_mask);
  sigaction(SIGTERM, &act, NULL);
  sigaction(SIGSTOP, &act, NULL);
  sigaction(SIGINT, &act, NULL);

  atexit(exit_clean);
  sock_path = argv[1];
  server(argv[1]);

  return EXIT_SUCCESS;
}

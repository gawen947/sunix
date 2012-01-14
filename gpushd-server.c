/* File: gpushd-server.c
   Time-stamp: <2011-11-04 16:40:46 gawen>

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

#define _BSD_SOURCE
#define _POSIX_SOURCE
#define _POSIX_C_SOURCE 201111L

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sysexits.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <err.h>

#ifdef __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif /* __FreeBSD__ */

#include "gpushd-common.h"
#include "safe-call.h"
#include "gpushd.h"

enum s_magic {
  GPUSHD_SWAP_MAGIK1  = 0x48535047, /* GPSH */
  GPUSHD_SWAP_MAGIK2  = 0x50415753, /* SWAP */
  GPUSHD_SWAP_VERSION = 0x00000001 };

/*
 * TODO:
 *  - avoid duplicates
 */

#define MAX_CONCURRENCY 16
#define MAX_STACK       65535

#define CHECK_BIT(bit, flag) ((flag) & (1 << (bit)))
#define SET_BIT(bit, flag) ((flag) |= (1 << (bit)))
#define CLEAR_BIT(bit, flag) ((flag) &= ~(1 << (bit)))

static const char *sock_path;
static const char *swap_path;

static struct thread_pool {
  sem_t available;
  unsigned int idx;
  pthread_t threads[MAX_CONCURRENCY]; /* threads */
  int       fd_cli[MAX_CONCURRENCY];  /* client file descriptor */
  uint16_t  st_threads;               /* threads state */
  sem_t     st_mutex;                 /* mutex for threads */

  pthread_t cleaner;                  /* cleaner thread */
  sem_t clr_mutex;                    /* mutex for cleaner state */
  sem_t clr_bell;                     /* call the cleaner thread */
  sem_t clr_done[MAX_CONCURRENCY];    /* clean threads */
  uint16_t st_cleaner;                /* cleaner thread state */
} pool;

static struct stats {
  size_t nb_cli;     /* clients connected */
  size_t nb_srv;     /* server started */
  size_t nb_rcv;     /* commands received */
  size_t nb_snd;     /* commands sent */
  size_t nb_err;     /* error sent */

  uint32_t max_nsec; /* max request time */
  uint32_t min_nsec; /* min request time */
  uint64_t sum_nsec; /* total request time */
} stats;

static struct dir_stack {
  sem_t mutex;
  size_t size;

  struct d_node {
    struct d_node *next;

    char d_path[MAX_PATH];
  } *dirs;
} stack;

static void swap_write_stats(int fd)
{
  uint32_t ul;
  uint64_t ull;

  ul = htole32(stats.nb_cli);
  xwrite(fd, &ul, sizeof(ul));

  ul = htole32(stats.nb_srv);
  xwrite(fd, &ul, sizeof(ul));

  ul = htole32(stats.nb_rcv);
  xwrite(fd, &ul, sizeof(ul));

  ul = htole32(stats.nb_snd);
  xwrite(fd, &ul, sizeof(ul));

  ul = htole32(stats.nb_err);
  xwrite(fd, &ul, sizeof(ul));

  ul = htole32(stats.max_nsec);
  xwrite(fd, &ul, sizeof(ul));

  ul = htole32(stats.min_nsec);
  xwrite(fd, &ul, sizeof(ul));

  ull = htole64(stats.sum_nsec);
  xwrite(fd, &ull, sizeof(ull));
}

static void swap_read_stats(int fd)
{
  uint32_t ul;
  uint64_t ull;

  xread(fd, &ul, sizeof(ul));
  stats.nb_cli = le32toh(ul);

  xread(fd, &ul, sizeof(ul));
  stats.nb_srv = le32toh(ul);

  xread(fd, &ul, sizeof(ul));
  stats.nb_rcv = le32toh(ul);

  xread(fd, &ul, sizeof(ul));
  stats.nb_snd = le32toh(ul);

  xread(fd, &ul, sizeof(ul));
  stats.nb_err = le32toh(ul);

  xread(fd, &ul, sizeof(ul));
  stats.max_nsec = le32toh(ul);

  xread(fd, &ul, sizeof(ul));
  stats.min_nsec = le32toh(ul);

  xread(fd, &ull, sizeof(ull));
  stats.sum_nsec = le64toh(ull);
}

static void swap_save(const char *swap_file)
{
  struct d_node *c = stack.dirs;

  uint32_t magik1  = htole32(GPUSHD_SWAP_MAGIK1);
  uint32_t magik2  = htole32(GPUSHD_SWAP_MAGIK2);
  uint32_t version = htole32(GPUSHD_SWAP_VERSION);

  int fd = xopen(swap_file, O_CREAT | O_WRONLY | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  xwrite(fd, &magik1, sizeof(uint32_t));
  xwrite(fd, &magik2, sizeof(uint32_t));
  xwrite(fd, &version, sizeof(uint32_t));

  /* close stack until exit */
  sem_wait(&stack.mutex);

  swap_write_stats(fd);

  for(; c != NULL ; c = c->next) {
    uint8_t size = strlen(c->d_path);
    xwrite(fd, &size, sizeof(uint8_t));
    xwrite(fd, c->d_path, size);
  }

  close(fd);
}

static void swap_load(const char *swap_file)
{
  struct d_node *last = NULL;
  uint32_t magik1;
  uint32_t magik2;
  uint32_t version;

  int fd = open(swap_file, O_RDONLY, 0);

  if(fd < 0)
    return;

  xread(fd, &magik1, sizeof(uint32_t));
  xread(fd, &magik2, sizeof(uint32_t));
  xread(fd, &version, sizeof(uint32_t));
  magik1  = le32toh(magik1);
  magik2  = le32toh(magik2);
  version = le32toh(version);

  if(magik1 != GPUSHD_SWAP_MAGIK1 && magik2 != GPUSHD_SWAP_MAGIK2) {
    warnx("bad magik number in swap file");
    return;
  }

  if(version > GPUSHD_SWAP_VERSION) {
    warnx("version too high in swap file");
    return;
  }

  swap_read_stats(fd);

  while(1) {
    struct d_node *new = xmalloc(sizeof(struct d_node));
    uint8_t size;
    size_t n = xread(fd, &size, sizeof(uint8_t));

    if(!n)
      break;

    n = xread(fd, new->d_path, size);
    if(n != size)
      errx(EXIT_FAILURE, "invalid swap file");

    sem_wait(&stack.mutex);
    {
      if(stack.size == MAX_STACK) {
        sem_post(&stack.mutex);
        warnx("stack full");
        close(fd);
        free(new);
        return;
      }

      new->next = NULL;

      if(!last)
        stack.dirs = new;
      else
        last->next = new;
      last = new;
      stack.size++;
    }
    sem_post(&stack.mutex);
  }

  close(fd);
}

static uint64_t substract_nsec(const struct timespec *begin,
                               const struct timespec *end)
{
  uint64_t b = begin->tv_sec * 1000000000 + begin->tv_nsec;
  uint64_t e = end->tv_sec * 1000000000 + end->tv_nsec;

  assert(e > b);

  return (e - b);
}

static void exit_clean()
{
  unlink(sock_path);

  /* swap out */
  if(swap_path)
    swap_save(swap_path);

  exit(EXIT_SUCCESS);
}

static void signal_clean(int signum)
{
  exit(EXIT_SUCCESS);
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

static void s_send_error(int cli, int code)
{
  send_error(cli, code);
  stats.nb_snd++;
  stats.nb_err++;
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
      s_send_error(cli, E_FULL);
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
        if(stack.size)
          s_send_error(cli, E_NFOUND);
        else
          s_send_error(cli, E_EMPTY);
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
  stats.nb_snd++;

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
      if(stack.size)
        s_send_error(cli, E_NFOUND);
      else
        s_send_error(cli, E_EMPTY);
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
  stats.nb_snd++;

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
        if(stack.size)
          s_send_error(cli, E_NFOUND);
        else
          s_send_error(cli, E_EMPTY);
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
  stats.nb_snd++;

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
      if(stack.size)
        s_send_error(cli, E_NFOUND);
      else
        s_send_error(cli, E_EMPTY);
      return true;
    }
    result = *stack.dirs;
  }
  sem_post(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.d_path, strlen(result.d_path) + 1);
  stats.nb_snd++;

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
  stats.nb_snd++;

  return true;
}

static bool cmd_nbcli(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;

  v = stats.nb_cli;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  return true;
}

static bool cmd_nbsrv(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;

  v = stats.nb_srv;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  return true;
}

static bool cmd_nbrcv(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;

  v = stats.nb_rcv;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  return true;
}

static bool cmd_nbsnd(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;

  v = stats.nb_snd;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  return true;
}

static bool cmd_nberr(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;

  v = stats.nb_err;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  return true;
}

static bool cmd_maxnsec(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;

  v = stats.max_nsec;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  return true;
}

static bool cmd_minnsec(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;

  v = stats.min_nsec;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  return true;
}

static bool cmd_sumnsec(int cli, struct message *request)
{
  unsigned int v;
  char cmd = CMD_RESPI;


  v = (stats.sum_nsec & 0xffffff000000LL);

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

  v = (stats.sum_nsec & 0xffffffLL) >> 32;

  write(cli, &cmd, sizeof(char));
  write(cli, &v, sizeof(unsigned int));
  stats.nb_snd++;

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
      stats.nb_snd++;
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
    stats.nb_rcv++;
    return false;
  case(CMD_END):
  case(CMD_RESPI):
  case(CMD_RESPS):
    warnx("received invalid command %d from client", request->command);
    s_send_error(cli, E_PERM);
    stats.nb_rcv++;
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
  case(CMD_NBCLI):
    result = cmd_nbrcv(cli, request);
    break;
  case(CMD_NBSRV):
    result = cmd_nbsrv(cli, request);
    break;
  case(CMD_NBRCV):
    result = cmd_nbrcv(cli, request);
    break;
  case(CMD_NBSND):
    result = cmd_nbsnd(cli, request);
    break;
  case(CMD_NBERR):
    result = cmd_nberr(cli, request);
    break;
  case(CMD_MAXNSEC):
    result = cmd_maxnsec(cli, request);
    break;
  case(CMD_MINNSEC):
    result = cmd_minnsec(cli, request);
    break;
  case(CMD_SUMNSEC):
    result = cmd_sumnsec(cli, request);
    break;
  default:
    assert(false); /* unknown command */
  }

  write(cli, &end, sizeof(char));

  stats.nb_rcv++;

  return result;
}

static void * new_cli(void *arg)
{
  uint64_t req_nsec;
  struct timespec begin, end;
  int idx = (long)arg;
  struct parse_state cli = { .state = ST_CMD,
                             .p_idx = 0 };
  struct sigaction act   = { .sa_handler = cli_timeout,
                             .sa_flags   = 0 };

  clock_gettime(CLOCK_MONOTONIC, &begin);

  /* ensure this thread won't live more than REQUEST_TIMEOUT seconds */
  sigfillset(&act.sa_mask);
  sigaction(SIGALRM, &act, NULL);

  alarm(REQUEST_TIMEOUT);

  while(parse(pool.fd_cli[idx], &cli, proceed_request));

  close(pool.fd_cli[idx]);

  /* signal the cleaner thread */
  sem_wait(&pool.clr_mutex);
  SET_BIT(idx, pool.st_cleaner);
  sem_post(&pool.clr_mutex);
  sem_post(&pool.clr_bell);

  /* free the thread slot */
  sem_wait(&pool.st_mutex);
  CLEAR_BIT(idx, pool.st_threads);
  sem_post(&pool.st_mutex);
  sem_post(&pool.available);

  alarm(0);

  clock_gettime(CLOCK_MONOTONIC, &end);

  req_nsec = substract_nsec(&begin, &end);
  if(req_nsec > UINT32_MAX)
    warnx("slow request");
  if(req_nsec > stats.max_nsec)
    stats.max_nsec = req_nsec;
  if(req_nsec < stats.min_nsec)
    stats.min_nsec = req_nsec;
  stats.sum_nsec += req_nsec;

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

  /* now we may register the exit function */
  atexit(exit_clean);

  /* listen and backlog up to eight connections */
  xlisten(sd, 8);
  while(1) {
    int i, fd = xaccept(sd, NULL, NULL);

    /* wait for the first available thread */
    sem_wait(&pool.available);

    /* fix this, use T(16) at each turn */
    for(i = pool.idx ; CHECK_BIT(i, pool.st_threads) ;
        i = (i + 1) % MAX_CONCURRENCY);

    /* wait for it to be cleaned */
    sem_wait(&pool.clr_done[i]);

    /* setup client thread */
    sem_wait(&pool.st_mutex);
    SET_BIT(i, pool.st_threads);
    sem_post(&pool.st_mutex);
    pool.fd_cli[i] = fd;

    if(pthread_create(&pool.threads[i], NULL, new_cli, (void *)(long)i))
      err(EXIT_FAILURE, "cannot create thread");

    stats.nb_cli++;

    pool.idx = (pool.idx + 1) % MAX_CONCURRENCY;
  }
}

static void * cleaner_thread(void *null)
{
  while(1) {
    int i;

    sem_wait(&pool.clr_bell);
    for(i = 0 ; i < MAX_CONCURRENCY ; i++) {
      if(CHECK_BIT(i, pool.st_cleaner)) {
        sem_wait(&pool.clr_mutex);
        CLEAR_BIT(i, pool.st_cleaner);
        sem_post(&pool.clr_mutex);

        pthread_join(pool.threads[i], NULL);
        sem_post(&pool.clr_done[i]);
      }
    }
  }

  return NULL; /* we never get here */
}

int main(int argc, const char *argv[])
{
  int n = MAX_CONCURRENCY;
  struct sigaction act = { .sa_handler = signal_clean,
                           .sa_flags   = 0 };

  if(argc != 2 && argc != 3)
    errx(EXIT_FAILURE, "usage <socket-path> [swap-path]");

  while(n--)
    xsem_init(&pool.clr_done[n], 0, 1);
  xsem_init(&pool.available, 0, MAX_CONCURRENCY);
  xsem_init(&pool.st_mutex, 0, 1);
  xsem_init(&pool.clr_bell, 0, 0);
  xsem_init(&pool.clr_mutex, 0, 1);
  xsem_init(&stack.mutex, 0, 1);

  /* unlink socket on exit */
  sigfillset(&act.sa_mask);
  sigaction(SIGQUIT, &act, NULL);
  sigaction(SIGTSTP, &act, NULL);
  sigaction(SIGINT, &act, NULL);
  sigaction(SIGSTOP, &act, NULL);
  sigaction(SIGKILL, &act, NULL);
  sigaction(SIGTERM, &act, NULL);
  sigaction(SIGSTOP, &act, NULL);
  sigaction(SIGINT, &act, NULL);

  stats.min_nsec = UINT32_MAX;

  /* start cleaner thread */
  if(pthread_create(&pool.cleaner, NULL, cleaner_thread, NULL))
    err(EXIT_FAILURE, "cannot create thread");

  if(argc == 3)
    swap_path = argv[2];
  sock_path = argv[1];
  if(swap_path)
    swap_load(swap_path);
  stats.nb_srv++;
  server(argv[1]);

  return EXIT_SUCCESS;
}

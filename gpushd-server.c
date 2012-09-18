/* File: gpushd-server.c
   Time-stamp: <2012-07-02 14:08:39 gawen>

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

#include <sys/time.h>
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
#include <getopt.h>
#include <errno.h>
#include <err.h>

#ifdef __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif /* __FreeBSD__ */

#include "gpushd-common.h"
#include "safe-call.h"
#include "iobuf.h"
#include "gpushd.h"

enum s_magic {
  GPUSHD_SWAP_MAGIK1  = 0x48535047, /* GPSH */
  GPUSHD_SWAP_MAGIK2  = 0x50415753, /* SWAP */
  GPUSHD_SWAP_VERSION = 0x00000002
};

#define DEFAULT_TIMEOUT 1
#define MAX_CONCURRENCY 16
#define MAX_STACK       UINT16_MAX

#define CHECK_BIT(bit, flag) ((flag) & (1 << (bit)))
#define SET_BIT(bit, flag) ((flag) |= (1 << (bit)))
#define CLEAR_BIT(bit, flag) ((flag) &= ~(1 << (bit)))

static const char *prog_name;
static const char *sock_path;
static const char *swap_path;
static int synctime;
static int timeout = DEFAULT_TIMEOUT;

struct opts_name {
  char name_short;
  const char *name_long;
  const char *help;
};

static struct thread_pool {
  sem_t available;
  unsigned int idx;
  pthread_t threads[MAX_CONCURRENCY];        /* threads */
  int       fd_cli[MAX_CONCURRENCY];         /* client file descriptor */
  struct timespec start_ts[MAX_CONCURRENCY]; /* thread start time */
  uint16_t  st_threads;                      /* threads state */
  pthread_mutex_t st_mutex;                  /* mutex for threads */

  pthread_t cleaner;                         /* cleaner thread */
  pthread_mutex_t clr_mutex;                 /* mutex for cleaner state */
  pthread_mutex_t clr_bell;                  /* call the cleaner thread */
  pthread_mutex_t clr_done[MAX_CONCURRENCY]; /* clean threads */
  uint16_t st_cleaner;                       /* cleaner thread state */

  bool dirty;                                /* swap file dirty flag */
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
  pthread_mutex_t  mutex;
  size_t size;

  struct d_node {
    struct d_node *next;
    char *entry;
  } *dirs;
} stack;

static void free_node(struct d_node *node)
{
  free(node->entry);
  free(node);
}

static int xiobuf_close(iofile_t file)
{
  int ret = iobuf_close(file);
  if(ret < 0)
    err(EXIT_FAILURE, "iobuf_close");
  return ret;
}

static size_t xiobuf_write(iofile_t file, const void *buf, size_t count)
{
  size_t ret = iobuf_write(file, buf, count);
  if(ret != count) {
    iobuf_close(file);
    err(EXIT_FAILURE, "iobuf_write");
  }
  return ret;
}

static size_t xiobuf_read(iofile_t file, void *buf, size_t count)
{
  int ret = iobuf_read(file, buf, count);
  if(ret < 0) {
    iobuf_close(file);
    err(EXIT_FAILURE, "iobuf_read");
  }
  return ret;
}

static void swap_write_stats(iofile_t file)
{
  uint32_t ul;
  uint64_t ull;

  ul = htole32(stats.nb_cli);
  xiobuf_write(file, &ul, sizeof(ul));

  ul = htole32(stats.nb_srv);
  xiobuf_write(file, &ul, sizeof(ul));

  ul = htole32(stats.nb_rcv);
  xiobuf_write(file, &ul, sizeof(ul));

  ul = htole32(stats.nb_snd);
  xiobuf_write(file, &ul, sizeof(ul));

  ul = htole32(stats.nb_err);
  xiobuf_write(file, &ul, sizeof(ul));

  ul = htole32(stats.max_nsec);
  xiobuf_write(file, &ul, sizeof(ul));

  ul = htole32(stats.min_nsec);
  xiobuf_write(file, &ul, sizeof(ul));

  ull = htole64(stats.sum_nsec);
  xiobuf_write(file, &ul, sizeof(ull));
}

static void swap_read_stats(iofile_t file)
{
  uint32_t ul;
  uint64_t ull;

  xiobuf_read(file, &ul, sizeof(ul));
  stats.nb_cli = le32toh(ul);

  xiobuf_read(file, &ul, sizeof(ul));
  stats.nb_srv = le32toh(ul);

  xiobuf_read(file, &ul, sizeof(ul));
  stats.nb_rcv = le32toh(ul);

  xiobuf_read(file, &ul, sizeof(ul));
  stats.nb_snd = le32toh(ul);

  xiobuf_read(file, &ul, sizeof(ul));
  stats.nb_err = le32toh(ul);

  xiobuf_read(file, &ul, sizeof(ul));
  stats.max_nsec = le32toh(ul);

  xiobuf_read(file, &ul, sizeof(ul));
  stats.min_nsec = le32toh(ul);

  xiobuf_read(file, &ull, sizeof(ull));
  stats.sum_nsec = le64toh(ull);
}

static void swap_save(const char *swap_file)
{
  iofile_t file;
  struct d_node *c = stack.dirs;

  uint32_t magik1  = htole32(GPUSHD_SWAP_MAGIK1);
  uint32_t magik2  = htole32(GPUSHD_SWAP_MAGIK2);
  uint32_t version = htole32(GPUSHD_SWAP_VERSION);

  file = iobuf_open(swap_file, O_CREAT | O_WRONLY | O_TRUNC, 00666);
  if(!file)
    err(EXIT_FAILURE, "swap_save");

  xiobuf_write(file, &magik1,  sizeof(uint32_t));
  xiobuf_write(file, &magik2,  sizeof(uint32_t));
  xiobuf_write(file, &version, sizeof(uint32_t));

  /* close stack until exit */
  pthread_mutex_lock(&stack.mutex);

  swap_write_stats(file);

  for(; c != NULL ; c = c->next) {
    uint16_t size = strlen(c->entry);
    uint16_t us   = htole32(size);

    xiobuf_write(file, &us, sizeof(us));
    xiobuf_write(file, c->entry, size);
  }

  xiobuf_close(file);
}

/* Migration code */
static void swap_load_1(iofile_t file)
{
  struct d_node *last = NULL;

  swap_read_stats(file);

  while(1) {
    struct d_node *new = xmalloc(sizeof(struct d_node));
    uint8_t size;
    size_t n = xiobuf_read(file, &size, sizeof(uint8_t));

    if(!n)
      break;

    new->entry = xmalloc(size + 1);
    n = xiobuf_read(file, new->entry, size);
    new->entry[size] = '\0';

    if(n != size)
      errx(EXIT_FAILURE, "invalid swap file");

    pthread_mutex_lock(&stack.mutex);
    {
      if(stack.size == MAX_STACK) {
        pthread_mutex_unlock(&stack.mutex);
        warnx("Stack full");
        xiobuf_close(file);
        free_node(new);
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
    pthread_mutex_unlock(&stack.mutex);
  }
}

/* Current version swap file loader */
static void swap_load_2(iofile_t file)
{
  struct d_node *last = NULL;

  swap_read_stats(file);

  while(1) {
    struct d_node *new = xmalloc(sizeof(struct d_node));
    uint16_t size;
    size_t n = xiobuf_read(file, &size, sizeof(size));

    if(!n)
      break;

    size = le32toh(size);

    new->entry = xmalloc(size + 1);
    n = xiobuf_read(file, new->entry, size);
    new->entry[size] = '\0';

    if(n != size)
      errx(EXIT_FAILURE, "invalid swap file");

    pthread_mutex_lock(&stack.mutex);
    {
      if(stack.size == MAX_STACK) {
        pthread_mutex_unlock(&stack.mutex);
        warnx("Stack full");
        xiobuf_close(file);
        free_node(new);
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
    pthread_mutex_unlock(&stack.mutex);
  }
}

static void swap_load(const char *swap_file)
{
  iofile_t file;
  uint32_t magik1;
  uint32_t magik2;
  uint32_t version;

  file = iobuf_open(swap_file, O_RDONLY, 0);
  if(!file)
    return;

  xiobuf_read(file, &magik1, sizeof(uint32_t));
  xiobuf_read(file, &magik2, sizeof(uint32_t));
  xiobuf_read(file, &version, sizeof(uint32_t));
  magik1  = le32toh(magik1);
  magik2  = le32toh(magik2);
  version = le32toh(version);

  if(magik1 != GPUSHD_SWAP_MAGIK1 && magik2 != GPUSHD_SWAP_MAGIK2) {
    warnx("bad magik number in swap file");
    goto CLOSE;
  }

  if(version != GPUSHD_SWAP_VERSION)
    warnx("migrating swap file from version %d to version %d", version,
          GPUSHD_SWAP_VERSION);

  switch(version) {
  case(1):
    swap_load_1(file);
    break;
  case(2):
    swap_load_2(file);
    break;
  default:
    warnx("unknown swap file version %d ; current is %d", version,
          GPUSHD_SWAP_VERSION);
    break;
  }

CLOSE:
  xiobuf_close(file);
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

static void signal_timer(int signum)
{
  /* The dirty flag is set up when a new request comes up
     and reset when the stack and stats are swapped out
     to disk. */
  if(!pool.dirty)
    return;

  /* swap out */
  if(swap_path)
    swap_save(swap_path);
  pthread_mutex_unlock(&stack.mutex);
  pool.dirty = false;
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
  new->entry = xmalloc(strlen(request->p_string)+1);
  strcpy(new->entry, request->p_string);

  /* stack critical read-write section */
  pthread_mutex_lock(&stack.mutex);
  {
    if(stack.size == MAX_STACK) {
      pthread_mutex_unlock(&stack.mutex);  /* release early */
      s_send_error(cli, E_FULL);
      free_node(new);
      return true;
    }

    new->next  = stack.dirs;
    stack.dirs = new;
    stack.size++;
  }
  pthread_mutex_unlock(&stack.mutex);

  return true;
}

static bool cmd_pop(int cli, struct message *request)
{
  struct d_node result;
  struct d_node *c = stack.dirs, *o = NULL;
  int i, j = request->p_int.value;
  char cmd = CMD_RESPS;

  /* stack critical read-write section */
  pthread_mutex_lock(&stack.mutex);
  {
    for(i = 0 ; i < j && c != NULL; i++, c = c->next)
      o = c;

    if(c == NULL) {
      pthread_mutex_unlock(&stack.mutex); /* release early */
      if(stack.size)
        s_send_error(cli, E_NFOUND);
      else
        s_send_error(cli, E_EMPTY);
      return true;
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
  pthread_mutex_unlock(&stack.mutex);

  /* we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.entry, strlen(result.entry) + 1);
  free(result.entry);
  stats.nb_snd++;

  return true;
}

static bool cmd_popf(int cli, struct message *request)
{
  struct d_node *c = stack.dirs;
  struct d_node result;
  char cmd = CMD_RESPS;

  /* stack critical read section */
  pthread_mutex_lock(&stack.mutex);
  {
    if(!stack.dirs) {
      pthread_mutex_unlock(&stack.mutex);
      if(stack.size)
        s_send_error(cli, E_NFOUND);
      else
        s_send_error(cli, E_EMPTY);
      return true;
    }

    stack.size--;
    stack.dirs = c->next;
    result = *c;
    free(c);
  }
  pthread_mutex_unlock(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.entry, strlen(result.entry) + 1);
  free(result.entry);
  stats.nb_snd++;

  return true;
}

static bool cmd_clean(int cli, struct message *request)
{
  struct d_node *c = stack.dirs;

  /* stack critical write section */
  pthread_mutex_lock(&stack.mutex);
  {
    while(c != NULL) {
      struct d_node *o = c;

      c = c->next;
      free_node(o);
    }
    stack.dirs = NULL;
    stack.size = 0;
  }
  pthread_mutex_unlock(&stack.mutex);

  return true;
}

static bool cmd_get(int cli, struct message *request)
{
  int i, j = request->p_int.value;
  struct d_node *c = stack.dirs;
  struct d_node result;
  char cmd = CMD_RESPS;

  /* stack critical read section */
  pthread_mutex_lock(&stack.mutex);
  {
    for(i = 0 ; i < j && c != NULL ; i++, c = c->next);

    if(c == NULL) {
      pthread_mutex_unlock(&stack.mutex); /* release early */
      if(stack.size)
        s_send_error(cli, E_NFOUND);
      else
        s_send_error(cli, E_EMPTY);
      return false;
    }

    result = *c;
  }
  pthread_mutex_unlock(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.entry, strlen(result.entry) + 1);
  stats.nb_snd++;

  return true;
}

static bool cmd_getf(int cli, struct message *request)
{
  struct d_node result;
  char cmd = CMD_RESPS;

  /* stack critical read section */
  pthread_mutex_lock(&stack.mutex);
  {
    if(!stack.dirs) {
      pthread_mutex_unlock(&stack.mutex);
      if(stack.size)
        s_send_error(cli, E_NFOUND);
      else
        s_send_error(cli, E_EMPTY);
      return true;
    }
    result = *stack.dirs;
  }
  pthread_mutex_unlock(&stack.mutex);

  /* again, we don't send the response
     inside the critical section */
  write(cli, &cmd, sizeof(char));
  write(cli, result.entry, strlen(result.entry) + 1);
  stats.nb_snd++;

  return true;
}

static bool cmd_size(int cli, struct message *request)
{
  int size;
  char cmd = CMD_RESPI;

  /* stack critical read section */
  pthread_mutex_lock(&stack.mutex);
  {
    size = stack.size;
  }
  pthread_mutex_unlock(&stack.mutex);

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
  pthread_mutex_lock(&stack.mutex);
  {
    /* we got no choice here but to send
       the message inside the critical section */
    for(; c != NULL ; c = c->next) {
      write(cli, &cmd, sizeof(char));
      write(cli, c->entry, strlen(c->entry) + 1);
      stats.nb_snd++;
    }
  }
  pthread_mutex_unlock(&stack.mutex);

  return true;
}

static bool cmd_error(int cli, struct message *request)
{
  warnx("received error from client %d with %s",
        cli, str_error(request->p_int.value));

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
    result = cmd_nbcli(cli, request);
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

static void clean_thread(int idx)
{
  close(pool.fd_cli[idx]);

  /* signal the cleaner thread */
  pthread_mutex_lock(&pool.clr_mutex);
  SET_BIT(idx, pool.st_cleaner);
  pthread_mutex_unlock(&pool.clr_mutex);

  /* free the thread slot */
  pthread_mutex_lock(&pool.st_mutex);
  CLEAR_BIT(idx, pool.st_threads);
  pthread_mutex_unlock(&pool.st_mutex);
  sem_post(&pool.available);
}

/* When there are no more connections available
   and the last incoming connection timedout on
   semtimedwait() we check for any stalled
   connection and we destroy it. */
static void check_stalled(void)
{
  struct timespec now;
  int i;

  clock_gettime(CLOCK_MONOTONIC, &now);

  for(i = 0 ; i < MAX_CONCURRENCY ; i++) {
    struct timespec ts = pool.start_ts[i];
    if(now.tv_sec > ts.tv_sec + timeout) {
      pthread_cancel(pool.threads[i]);
      clean_thread(i);
    }
  }

  /* We signal the cleaner thread here.
     We wait until we checked each slot
     for a stalled connection so we
     wake up the cleaner thread only once. */
  pthread_mutex_unlock(&pool.clr_bell);
}

static void * new_cli(void *arg)
{
  uint64_t req_nsec;
  struct timespec begin, end;
  int idx = (long)arg;
  struct parse_state cli = { .state = ST_CMD,
                             .p_idx = 0 };

  clock_gettime(CLOCK_MONOTONIC, &begin);

  pool.dirty = true;

  while(parse(pool.fd_cli[idx], &cli, proceed_request));
  clean_thread(idx);

  clock_gettime(CLOCK_MONOTONIC, &end);

  req_nsec = substract_nsec(&begin, &end);
  if(req_nsec > UINT32_MAX)
    warnx("slow request");
  if(req_nsec > stats.max_nsec)
    stats.max_nsec = req_nsec;
  if(req_nsec < stats.min_nsec)
    stats.min_nsec = req_nsec;
  stats.sum_nsec += req_nsec;

  /* We signal the cleaner thread here */
  pthread_mutex_unlock(&pool.clr_bell);

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
    struct timespec ts = { .tv_sec  = timeout,
                           .tv_nsec = 0 };
    int i, fd = accept(sd, NULL, NULL);

    if(fd < 0) {
      if(errno == EINTR)
        continue;
      err(EXIT_FAILURE, "accept error");
    }

    /* wait for the first available thread */
    while(1) {
      if(sem_timedwait(&pool.available, &ts) < 0) {
        switch(errno) {
        case(ETIMEDOUT):
          check_stalled();
          break;
        case(EINTR):
          break;
        default:
          err(EXIT_FAILURE, "sem_timedwait");
        }
      }
      else
        break; /* success */
    }

    /* fix this, use T(16) at each turn */
    for(i = pool.idx ; CHECK_BIT(i, pool.st_threads) ;
        i = (i + 1) % MAX_CONCURRENCY);

    /* wait for it to be cleaned */
    pthread_mutex_lock(&pool.clr_done[i]);

    /* setup client thread */
    pthread_mutex_lock(&pool.st_mutex);
    SET_BIT(i, pool.st_threads);
    pool.fd_cli[i] = fd;
    pthread_mutex_unlock(&pool.st_mutex);

    if(pthread_create(&pool.threads[i], NULL, new_cli, (void *)(long)i))
      err(EXIT_FAILURE, "cannot create thread");

    stats.nb_cli++;

    pool.idx = (pool.idx + 1) % MAX_CONCURRENCY;
  }
}

/* We need to join threads to free associated
   resources. When a new thread is created it
   warns the cleaner thread through the clr_bell
   mutex. This thread will then join on each
   thread ready to be cleaned. This ensures that
   we never have more than MAX_CONCURRENCY still
   living in memory and consuming resources. */
static void * cleaner_thread(void *null)
{
  while(1) {
    int i;

    pthread_mutex_lock(&pool.clr_bell);
    for(i = 0 ; i < MAX_CONCURRENCY ; i++) {
      if(CHECK_BIT(i, pool.st_cleaner)) {
        pthread_mutex_lock(&pool.clr_mutex);
        CLEAR_BIT(i, pool.st_cleaner);
        pthread_mutex_unlock(&pool.clr_mutex);

        pthread_join(pool.threads[i], NULL);
        pthread_mutex_unlock(&pool.clr_done[i]);
      }
    }
  }

  return NULL; /* we never get here */
}

/* Auto-Indentation of the standard help message
   according to the options specified in names. */
static void help(const struct opts_name *names)
{
  const struct opts_name *opt;
  int size;
  int max = 0;

  /* basic usage */
  fprintf(stderr, "Usage: %s [OPTIONS] [SOCKET-PATH] [SWAP-PATH]\n", prog_name);

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

int main(int argc, char *argv[])
{
  int exit_status = EXIT_FAILURE;
  int n = MAX_CONCURRENCY;
  struct itimerval timer;
  struct sigaction ign = { .sa_handler = SIG_IGN };
  struct sigaction tim = { .sa_handler = signal_timer };
  struct sigaction act = { .sa_handler = signal_clean,
                           .sa_flags   = 0 };

  /* This structure is mainly used for the help(...) function and
     map options to their respective help message. */
  struct opts_name names[] = {
    { 's', "sync-time", "Sync the swap file every specified seconds" },
    { 't', "timeout",   "Time in seconds before a request timeout" },
    { 'h', "help",      "Show this help message" },
    { 0, NULL, NULL }
  };

  /* geopt long options declaration */
  struct option opts[] = {
    { "sync-time", required_argument, NULL, 's' },
    { "timeout",   required_argument, NULL, 't' },
    { "help",      no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };

  /* parse program name */
  prog_name = (const char *)strrchr(argv[0], '/');
  prog_name = prog_name ? (prog_name + 1) : argv[0];

  while(1) {
    int c = getopt_long(argc, argv, "s:t:h", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
    case('s'):
      synctime = atoi(optarg);
      if(synctime < 0)
        errx(EXIT_FAILURE, "invalid sync time");
      break;
    case('t'):
      timeout  = atoi(optarg);
      if(timeout < 1)
        errx(EXIT_FAILURE, "invalid timeout time");
      break;
    case('h'):
      exit_status = EXIT_SUCCESS;
    default:
      help(names);
      exit(exit_status);
    }
  }

  argc -= optind;
  argv += optind;

  if(argc != 1 && argc != 2) {
    help(names);
    exit(EXIT_FAILURE);
  }

  while(n--)
    pthread_mutex_init(&pool.clr_done[n], NULL);
  xsem_init(&pool.available, 0, MAX_CONCURRENCY);

  pthread_mutex_init(&pool.st_mutex, NULL);
  pthread_mutex_init(&pool.clr_bell, NULL);
  pthread_mutex_init(&pool.clr_mutex, NULL);
  pthread_mutex_init(&stack.mutex, NULL);
  pthread_mutex_lock(&pool.clr_bell);

  /* unlink socket on exit */
  sigfillset(&act.sa_mask);
  sigaction(SIGTERM, &act, NULL);
  sigaction(SIGINT, &act, NULL);
  sigaction(SIGALRM, &tim, NULL);
  sigaction(SIGUSR1, &tim, NULL);

  stats.min_nsec = UINT32_MAX;

  /* start cleaner thread */
  if(pthread_create(&pool.cleaner, NULL, cleaner_thread, NULL))
    err(EXIT_FAILURE, "cannot create thread");

  if(argc == 2)
    swap_path = argv[1];
  sock_path = argv[0];
  if(swap_path)
    swap_load(swap_path);
  stats.nb_srv++;

  /* itimer initialisation */
  if(synctime > 0) {
    timer.it_value.tv_sec  = timer.it_interval.tv_sec  = synctime;
    timer.it_value.tv_usec = timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
  }

  server(argv[0]);

  return EXIT_SUCCESS;
}

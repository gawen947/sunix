/* File: xte-bench.c
   Time-stamp: <2011-11-27 18:25:19 gawen>

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

/* TODO: review average time */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sysexits.h>
#include <fcntl.h>
#include <err.h>

enum mode { M_NEWLINE = 0,
            M_CARRIAGE,
            M_FILL };

enum t_mode { TM_PROF = 0,
              TM_USER,
              TM_REAL };

static struct result {
  volatile unsigned long long cur;
  float  interval;
  double sum;
  double sq_sum;
  unsigned long n;

  FILE *output;
} res = { .n = 1 };

struct context {
  float interval;
  unsigned int size;
  unsigned int limit;
  enum mode mode;
  enum t_mode t_mode;
  const char *message;
  const char *output;
};

struct opts_name {
  char name_short;
  const char *name_long;
  const char *help;
};

static void show_time(double speed)
{
  if(speed < 1000)
    fprintf(res.output, "%3.3g B/s", speed);
  else if(speed < 1E6)
    fprintf(res.output, "%3.3g KB/s", speed / 1000.);
  else if(speed < 1E9)
    fprintf(res.output, "%3.3g MB/s", speed / 1E6);
  else if(speed < 1E12)
    fprintf(res.output, "%3.3g GB/s", speed / 1E9);
  else
    fprintf(res.output, "%3.3g TB/s", speed / 1E12);
}

static void compute(int signum)
{
  double cur_speed = (double)res.cur / res.interval;
  double avg_speed;
  double mean_dev;

  /* update stats */
  res.cur     = 0;
  res.sum    += cur_speed;
  res.sq_sum += cur_speed * cur_speed;
  res.n++;

  /* compute avg and mean dev */
  avg_speed = res.sum / res.n;
  mean_dev  = sqrt(res.sq_sum / res.n - avg_speed * avg_speed);

  fprintf(res.output, "Current ");
  show_time(cur_speed);
  fprintf(res.output, " Average ");
  show_time(avg_speed);
  fprintf(res.output, " +/- ");
  show_time(mean_dev);
  fprintf(res.output, "\n");
  fflush(res.output);
}

static void output(const char *message, size_t len)
{
  size_t n = write(STDOUT_FILENO, message, len);
  res.cur += n;
}

static void put_last_character(char *message, size_t len,
                               const struct context *ctx)
{
  switch(ctx->mode) {
  case(M_NEWLINE):
    message[len++] = '\n';
    break;
  case(M_CARRIAGE):
    message[len++] = '\r';
    break;
  case(M_FILL):
  default:
    break;
  }

  message[len] = '\0';
}

static void start_bench(const struct context *ctx)
{
  int which_timer;
  size_t mlen;
  unsigned long sec;
  unsigned long usec;
  struct sigaction act = { .sa_handler = compute,
                           .sa_flags   = 0 };
  struct itimerval val;
  char *message;

  /* message structure */
  if(!ctx->message) {
    int i;

    message = malloc(ctx->size + 2);

    if(!message)
      errx(EXIT_FAILURE, "cannot allocate memory");

    for(i = 0 ; i < ctx->size ; i++)
      message[i] = 'a' + (i % 26);

    put_last_character(message, ctx->size, ctx);
  }
  else {
    mlen    = strlen(ctx->message);
    message = malloc(mlen + 2);

    if(!message)
      errx(EXIT_FAILURE, "cannot allocate memory");

    strcpy(message, ctx->message);

    put_last_character(message, mlen, ctx);
  }
  mlen = strlen(message);

  /* output */
  if(ctx->output) {
    res.output = fopen(ctx->output, "a+");
    if(!res.output)
      err(EXIT_FAILURE, "cannot open output");
  }
  else
    res.output = stderr;

  /* signal */
  switch(ctx->t_mode) {
  case(TM_PROF):
    which_timer = ITIMER_PROF;
    break;
  case(TM_USER):
    which_timer = ITIMER_VIRTUAL;
    break;
  case(TM_REAL):
  default:
    which_timer = ITIMER_REAL;
    break;
  }

  sigemptyset(&act.sa_mask);
  sigaction(SIGALRM, &act, NULL);
  sigaction(SIGVTALRM, &act, NULL);
  sigaction(SIGPROF, &act, NULL);

  /* timer */
  res.interval = ctx->interval;
  sec  = (int)ctx->interval;
  usec = (int)((ctx->interval - sec) * 1000000);

  val.it_value.tv_sec  = val.it_interval.tv_sec  = sec;
  val.it_value.tv_usec = val.it_interval.tv_usec = usec;
  setitimer(which_timer, &val, NULL);

  if(!ctx->limit) {
    while(1)
      output(message, mlen);
  }
  else {
    unsigned int n = ctx->limit;

    while(n--)
      output(message, mlen);
  }
}

static void help(const struct opts_name *names, const char *prog_name)
{
  const struct opts_name *opt;
  int size;
  int max = 0;

  fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name);

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

static void cmdline(int argc, char * const argv[], struct context *ctx)
{
  const char *prog_name;
  int exit_status = EXIT_FAILURE;

  prog_name = (const char *)strrchr(argv[0], '/');
  prog_name = prog_name ? (prog_name + 1) : argv[0];

  struct opts_name names[] = {
    { 'i', "interval",  "Interval in seconds between calculation" },
    { 's', "size",      "Message size" },
    { 'l', "limit",     "Number of messages to send" },
    { 'M', "message",   "Specify message" },
    { 'o', "output",    "Result output (default to stderr)" },
    { 'm', "mode",      "Benchmark mode (0=newline,1=carriage-return,2=fill)" },
    { 't', "time-mode", "Time mode (0=prof, 1=user, 2=real)" },
    { 'h', "help",      "Show this help message" },
    { 0, NULL, NULL }
  };

  struct option opts[] = {
    { "interval",  required_argument, NULL, 'i' },
    { "size",      required_argument, NULL, 's' },
    { "limit",     required_argument, NULL, 'l' },
    { "message",   required_argument, NULL, 'M' },
    { "output",    required_argument, NULL, 'o' },
    { "mode",      required_argument, NULL, 'm' },
    { "time-mode", required_argument, NULL, 't' },
    { "help",      no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };

  while(1) {
    int c = getopt_long(argc, argv, "i:s:l:M:o:m:t:h", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
    case('i'):
      ctx->interval = atof(optarg);
      break;
    case('s'):
      ctx->size     = atoi(optarg);
      break;
    case('l'):
      ctx->limit    = atoi(optarg);
      break;
    case('M'):
      ctx->message  = optarg;
      break;
    case('o'):
      ctx->output   = optarg;
      break;
    case('m'):
      ctx->mode     = atoi(optarg);
      break;
    case('t'):
      ctx->t_mode   = atoi(optarg);
      break;
    case('h'):
      exit_status = EXIT_SUCCESS;
    default:
      help(names, prog_name);
      exit(exit_status);
    }
  }
}

int main(int argc, char * const argv[])
{
  struct context ctx = { .interval = 1.,
                         .limit    = 0,
                         .size     = 64,
                         .mode     = M_NEWLINE,
                         .t_mode   = TM_REAL,
                         .message  = NULL,
                         .output   = NULL };

  /* parse command line */
  cmdline(argc, argv, &ctx);

  /* start benchmarking */
  start_bench(&ctx);

  return EXIT_FAILURE;
}

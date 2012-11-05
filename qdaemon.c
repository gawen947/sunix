/* File: qdaemon.c
   Time-stamp: <2012-11-05 20:54:34 gawen>

   Copyright (C) 2012 David Hauweele <david@hauweele.net>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>. */

#define _BSD_SOURCE
#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <err.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>

static const char * queue;
static const char * command;
static int          av_task;
static int          nb_task;

static ssize_t qread(int fd, char * restrict buf)
{
  ssize_t n;
  if((n = read(fd, buf, 4096)) < 0)
    err(1, "cannot read queue");
  return n;
}

static void qwrite(int fd, const char *buf, ssize_t n)
{
  static ssize_t size;

  if(!buf) {
    if(ftruncate(fd, size) < 0)
      err(1, "cannot truncate queue");
    size = 0;
  } else {
    size += n;
    if(write(fd, buf, n) != n)
      err(1, "cannot write queue");
  }
}

static void exec_command(const char *line)
{
  switch(fork()) {
  case -1:
    err(1, "cannot fork");
  case 0:
    execl(command, command, line, NULL);
    err(1, "cannot exec command");
  default:
    break;
  }
}

static int extract(void)
{
  char line[4096];
  char buf[4096];
  ssize_t n;
  int fd;
  int i;

  if(av_task == 0)
    return 0;

  if((fd = open(queue, O_RDWR)) < 0)
    err(1, "cannot open queue");

  n = qread(fd, buf);

  for(i = 0 ; i < n; i++) {
    if(buf[i] == '\n') {
      line[i] = '\0';
      break;
    }

    line[i] = buf[i];
  }

  if(i == n)
    return 0;

  if(lseek(fd, 0, SEEK_SET) < 0)
    err(1, "cannot seek");

  qwrite(fd, buf + i + 1, n - i - 1);
  while((n = qread(fd, buf)))
    qwrite(fd, buf + i, n - i - 1);
  qwrite(fd, NULL, 0);

  close(fd);

  exec_command(line);
  av_task--;

  return 1;
}

static void sig_child(int signum)
{
  while(waitpid(-1, NULL, WNOHANG) > 0) {
    av_task++;
    if(av_task > nb_task)
      av_task = nb_task;
  }

  kill(getpid(), SIGUSR1);
}

static void sig_alrm(int signum)
{
  while(extract());
}

static void sig_term(int signum)
{
  exit(EXIT_SUCCESS);
}

static void init(int argc, char * const argv[])
{
  int wakeup_time;
  struct sigaction tim = { .sa_handler = sig_alrm };
  struct sigaction chd = { .sa_handler = sig_child };
  struct sigaction clr = { .sa_handler = sig_term,
                           .sa_flags   = 0 };

  if(argc != 5)
    errx(1, "expect options [wake-up-time] [par-task] [queue] [command]");

  wakeup_time = atoi(argv[1]);
  nb_task     = atoi(argv[2]);
  queue       = argv[3];
  command     = argv[4];
  av_task     = nb_task;

  if(wakeup_time < 0)
    errx(1, "invalid wake-up time");

  if(nb_task < 1 || nb_task > 8092)
    errx(1, "invalid simultaneous tasks number");
  else if(nb_task > 16)
    warn("too many simultaneous tasks");

  sigfillset(&clr.sa_mask);
  sigaction(SIGTERM, &clr, NULL);
  sigaction(SIGINT,  &clr, NULL);
  sigaction(SIGALRM, &tim, NULL);
  sigaction(SIGUSR1, &tim, NULL);
  sigaction(SIGCHLD, &chd, NULL);

  if(wakeup_time > 0) {
    struct itimerval timer;
    timer.it_value.tv_sec  = timer.it_interval.tv_sec  = wakeup_time;
    timer.it_value.tv_usec = timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
  }
}

int main(int argc, char * const argv[])
{
  init(argc, argv);

  while(1)
    pause();

  return 1;
}

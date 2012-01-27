/* File: par.c
   Time-stamp: <2012-01-28 00:26:09 gawen>

   Copyright (c) 2012 David Hauweele <david@hauweele.net>
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

#define _XOPEN_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

static int semid;
static int shmid;

#define MAX_FORK   8
#define PROJ_ID    'P'
#define TOK_PREFIX "/tmp/par"

#define UP()   sem(+1)
#define DOWN() sem(-1)
static void sem(int n)
{
  struct sembuf sem = { 0 };

  sem.sem_op = n;
  semop(semid, &sem, 1);
}

/* Get IPC or create the file if necessary
   this function ensure that semaphore are
   initialised and shm attached. */
static int * ipc_creat(const char *path)
{
  static int ipcflg = 0600;
  static int created;
  int *shm;
  key_t key;

  if((key = ftok(path, PROJ_ID)) < 0) {
    int fd;

    /* If the file doesn't exist we create it
       this creation along with IPC creation
       should be atomic. But it is not the
       case for now. */
    if(errno != ENOENT)
      err(1, "ftok \"%s\"", path);

    fd = open(path, O_RDWR | O_CREAT, 0600);
    if(fd < 0)
      err(1, "open \"%s\"", path);
    close(fd);

    ipcflg |= IPC_CREAT | IPC_EXCL;
    created  = 1;

    return ipc_creat(path);
  }

  do {
    /* Get IPC or create them when necessary */
    semid = semget(key, 1, ipcflg);
    shmid = shmget(key, sizeof(int), ipcflg);

    /* Check ipcget results */
    if((semid | shmid) < 0) {
      if(errno != EEXIST)
        err(1, "ipcget");
      ipcflg = 0600;
      continue;
    }
  } while(0);

  /* Attach shared memory segment to process address space */
  if((shm = shmat(shmid, NULL, 0)) == NULL)
    err(1, "shmat");

  /* Initialise memory and semaphores if just created */
  if(created) {
    if(semctl(semid, 0, SETVAL, 1) < 0)
      err(1, "semctl");

    *shm = 0;
  }

  return shm;
}

static void usage(void)
{
  (void)fprintf(stderr, "par [COMMAND]\n");
  exit(1);
}

int main(int argc, char * const argv[])
{
  pid_t pid;
  bool do_fork = true;
  char *s_argv;
  char path[256];
  int *shm;

  /* Check arguments */
  if(argc < 2)
    usage();

  /* Create the path file and get IPC */
  sprintf(path, TOK_PREFIX "%x", getuid());
  shm = ipc_creat(path);

  /* Check if we need to fork then do it
     when necessary and execute arguments */
  DOWN();
  {
    if(*shm <= MAX_FORK)
      *shm++;
    else
      do_fork = false;
  }
  UP();

  if(!do_fork)
    goto EXEC;

  pid = fork();

  if(pid < 0)
    err(1, "fork");
  else if(pid > 0)
    exit(0);

  pid = fork();

  if(pid < 0)
    err(1, "fork");
  else if(pid > 0) {
    wait(NULL);

    DOWN();
    {
      *shm--;
    }
    UP();

    exit(0);
  }

EXEC:
  argv++;
  execvp(argv[0], argv);
  err(1, "exec");
}

/*-
 * Copyright (c) 1988, 1993, 1994
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "record-invalid.h"

static void nosig(const char *);
static void printsigdesc(FILE *);
static void printsignals(FILE *);
static int signame_to_signum(const char *);
static void usage(void);

#define SIG2STR_MAX (sizeof "SIGRTMAX" + INT_STRLEN_BOUND (int) - 1)

#if defined _sys_nsig
# define SIGNUM_BOUND (_sys_nsig - 1)
#elif defined SIGNUM_BOUND
# define SIGNUM_BOUND (SIGNUM_BOUND - 1)
#else
# define SIGNUM_BOUND 64
#endif

#ifndef SIGRTMIN
# define SIGRTMIN 0
# undef SIGRTMAX
#endif
#ifndef SIGRTMAX
# define SIGRTMAX (SIGRTMIN - 1)
#endif

#define NUMNAME(name) { SIG##name, #name }

/* Signal names and numbers.  Put the preferred name first.  */
static struct numname { int num; char const name[8]; } numname_table[] =
  {
    /* Signals required by POSIX 1003.1-2001 base, listed in
       traditional numeric order where possible.  */
#ifdef SIGHUP
    NUMNAME (HUP),
#endif
#ifdef SIGINT
    NUMNAME (INT),
#endif
#ifdef SIGQUIT
    NUMNAME (QUIT),
#endif
#ifdef SIGILL
    NUMNAME (ILL),
#endif
#ifdef SIGTRAP
    NUMNAME (TRAP),
#endif
#ifdef SIGABRT
    NUMNAME (ABRT),
#endif
#ifdef SIGFPE
    NUMNAME (FPE),
#endif
#ifdef SIGKILL
    NUMNAME (KILL),
#endif
#ifdef SIGSEGV
    NUMNAME (SEGV),
#endif
    /* On Haiku, SIGSEGV == SIGBUS, but we prefer SIGSEGV to match
       strsignal.c output, so SIGBUS must be listed second.  */
#ifdef SIGBUS
    NUMNAME (BUS),
#endif
#ifdef SIGPIPE
    NUMNAME (PIPE),
#endif
#ifdef SIGALRM
    NUMNAME (ALRM),
#endif
#ifdef SIGTERM
    NUMNAME (TERM),
#endif
#ifdef SIGUSR1
    NUMNAME (USR1),
#endif
#ifdef SIGUSR2
    NUMNAME (USR2),
#endif
#ifdef SIGCHLD
    NUMNAME (CHLD),
#endif
#ifdef SIGURG
    NUMNAME (URG),
#endif
#ifdef SIGSTOP
    NUMNAME (STOP),
#endif
#ifdef SIGTSTP
    NUMNAME (TSTP),
#endif
#ifdef SIGCONT
    NUMNAME (CONT),
#endif
#ifdef SIGTTIN
    NUMNAME (TTIN),
#endif
#ifdef SIGTTOU
    NUMNAME (TTOU),
#endif

    /* Signals required by POSIX 1003.1-2001 with the XSI extension.  */
#ifdef SIGSYS
    NUMNAME (SYS),
#endif
#ifdef SIGPOLL
    NUMNAME (POLL),
#endif
#ifdef SIGVTALRM
    NUMNAME (VTALRM),
#endif
#ifdef SIGPROF
    NUMNAME (PROF),
#endif
#ifdef SIGXCPU
    NUMNAME (XCPU),
#endif
#ifdef SIGXFSZ
    NUMNAME (XFSZ),
#endif

    /* Unix Version 7.  */
#ifdef SIGIOT
    NUMNAME (IOT),      /* Older name for ABRT.  */
#endif
#ifdef SIGEMT
    NUMNAME (EMT),
#endif

    /* USG Unix.  */
#ifdef SIGPHONE
    NUMNAME (PHONE),
#endif
#ifdef SIGWIND
    NUMNAME (WIND),
#endif

    /* Unix System V.  */
#ifdef SIGCLD
    NUMNAME (CLD),
#endif
#ifdef SIGPWR
    NUMNAME (PWR),
#endif

    /* GNU/Linux 2.2 and Solaris 8.  */
#ifdef SIGCANCEL
    NUMNAME (CANCEL),
#endif
#ifdef SIGLWP
    NUMNAME (LWP),
#endif
#ifdef SIGWAITING
    NUMNAME (WAITING),
#endif
#ifdef SIGFREEZE
    NUMNAME (FREEZE),
#endif
#ifdef SIGTHAW
    NUMNAME (THAW),
#endif
#ifdef SIGLOST
    NUMNAME (LOST),
#endif
#ifdef SIGWINCH
    NUMNAME (WINCH),
#endif

    /* GNU/Linux 2.2.  */
#ifdef SIGINFO
    NUMNAME (INFO),
#endif
#ifdef SIGIO
    NUMNAME (IO),
#endif
#ifdef SIGSTKFLT
    NUMNAME (STKFLT),
#endif

    /* AIX 5L.  */
#ifdef SIGDANGER
    NUMNAME (DANGER),
#endif
#ifdef SIGGRANT
    NUMNAME (GRANT),
#endif
#ifdef SIGMIGRATE
    NUMNAME (MIGRATE),
#endif
#ifdef SIGMSG
    NUMNAME (MSG),
#endif
#ifdef SIGPRE
    NUMNAME (PRE),
#endif
#ifdef SIGRETRACT
    NUMNAME (RETRACT),
#endif
#ifdef SIGSAK
    NUMNAME (SAK),
#endif
#ifdef SIGSOUND
    NUMNAME (SOUND),
#endif

    /* Older AIX versions.  */
#ifdef SIGALRM1
    NUMNAME (ALRM1),    /* unknown; taken from Bash 2.05 */
#endif
#ifdef SIGKAP
    NUMNAME (KAP),      /* Older name for SIGGRANT.  */
#endif
#ifdef SIGVIRT
    NUMNAME (VIRT),     /* unknown; taken from Bash 2.05 */
#endif
#ifdef SIGWINDOW
    NUMNAME (WINDOW),   /* Older name for SIGWINCH.  */
#endif

    /* BeOS */
#ifdef SIGKILLTHR
    NUMNAME (KILLTHR),
#endif

    /* Older HP-UX versions.  */
#ifdef SIGDIL
    NUMNAME (DIL),
#endif

    /* Korn shell and Bash, of uncertain vintage.  */
    { 0, "EXIT" }
  };

#define NUMNAME_ENTRIES (sizeof numname_table / sizeof numname_table[0])

/* ISDIGIT differs from isdigit, as follows:
   - Its arg may be any int or unsigned int; it need not be an unsigned char
     or EOF.
   - It's typically faster.
   POSIX says that only '0' through '9' are digits.  Prefer ISDIGIT to
   isdigit unless it's important to use the locale's definition
   of `digit' even when the host does not conform to POSIX.  */
#define ISDIGIT(c) ((unsigned int) (c) - '0' <= 9)

/* Convert the signal name SIGNAME to a signal number.  Return the
   signal number if successful, -1 otherwise.  */

static int str2signum (char const *signame)
{
  if (ISDIGIT (*signame))
    {
      char *endp;
      long int n = strtol (signame, &endp, 10);
      if (! *endp && n <= SIGNUM_BOUND)
        return n;
    }
  else
    {
      unsigned int i;
      for (i = 0; i < NUMNAME_ENTRIES; i++)
        if (strcmp (numname_table[i].name, signame) == 0)
          return numname_table[i].num;

      {
        char *endp;
        int rtmin = SIGRTMIN;
        int rtmax = SIGRTMAX;

        if (0 < rtmin && strncmp (signame, "RTMIN", 5) == 0)
          {
            long int n = strtol (signame + 5, &endp, 10);
            if (! *endp && 0 <= n && n <= rtmax - rtmin)
              return rtmin + n;
          }
        else if (0 < rtmax && strncmp (signame, "RTMAX", 5) == 0)
          {
            long int n = strtol (signame + 5, &endp, 10);
            if (! *endp && rtmin - rtmax <= n && n <= 0)
              return rtmax + n;
          }
      }
    }

  return -1;
}

/* Convert the signal name SIGNAME to the signal number *SIGNUM.
   Return 0 if successful, -1 otherwise.  */

static int str2sig (char const *signame, int *signum)
{
  *signum = str2signum (signame);
  return *signum < 0 ? -1 : 0;
}

/* Convert SIGNUM to a signal name in SIGNAME.  SIGNAME must point to
   a buffer of at least SIG2STR_MAX bytes.  Return 0 if successful, -1
   otherwise.  */

static int sig2str (int signum, char *signame)
{
  unsigned int i;
  for (i = 0; i < NUMNAME_ENTRIES; i++)
    if (numname_table[i].num == signum)
      {
        strcpy (signame, numname_table[i].name);
        return 0;
      }

  {
    int rtmin = SIGRTMIN;
    int rtmax = SIGRTMAX;

    if (! (rtmin <= signum && signum <= rtmax))
      return -1;

    if (signum <= rtmin + (rtmax - rtmin) / 2)
      {
        int delta = signum - rtmin;
        sprintf (signame, delta ? "RTMIN+%d" : "RTMIN", delta);
      }
    else
      {
        int delta = rtmax - signum;
        sprintf (signame, delta ? "RTMAX-%d" : "RTMAX", delta);
      }

    return 0;
  }
}

int main(int argc, char *argv[])
{
  int cmd = 0;
  int errors, numsig, pid;
  char *ep;

  if (argc < 2)
    usage();

  numsig = SIGTERM;

  argc--, argv++;

  if(!strcmp(*argv, "-l"))
    cmd = 1;
  else if(!strcmp(*argv, "-L"))
    cmd = 2;

  switch(cmd) {
  case(1):
  case(2):
    argc--, argv++;
    if (argc > 1)
      usage();
    if (argc == 1) {
      if (!isdigit(**argv))
        usage();
      numsig = strtol(*argv, &ep, 10);
      if (!**argv || *ep) {
        record_invalid_string(argv[0], NULL, *argv);
        errx(2, "illegal signal number: %s", *argv);
      }
      if (numsig >= 128)
        numsig -= 128;
      if (numsig <= 0 || numsig >= SIGNUM_BOUND)
        nosig(*argv);
      printf("%s\n", sys_siglist[numsig]);
      return (0);
    }

    switch(cmd) {
    case(1):
      printsignals(stdout);
      break;
    case(2):
      printsigdesc(stdout);
      break;
    }

    return (0);
  }

  if (!strcmp(*argv, "-s")) {
    argc--, argv++;
    if (argc < 1) {
      warnx("option requires an argument -- s");
      usage();
    }
    if (strcmp(*argv, "0")) {
      if ((numsig = signame_to_signum(*argv)) < 0)
        nosig(*argv);
    } else
      numsig = 0;
    argc--, argv++;
  } else if (**argv == '-' && *(*argv + 1) != '-') {
    ++*argv;
    if (isalpha(**argv)) {
      if ((numsig = signame_to_signum(*argv)) < 0)
        nosig(*argv);
    } else if (isdigit(**argv)) {
      numsig = strtol(*argv, &ep, 10);
      if (!**argv || *ep)
        errx(2, "illegal signal number: %s", *argv);
      if (numsig < 0)
        nosig(*argv);
    } else
      nosig(*argv);
    argc--, argv++;
  }

  if (argc > 0 && strncmp(*argv, "--", 2) == 0)
    argc--, argv++;

  if (argc == 0)
    usage();

  for (errors = 0; argc; argc--, argv++) {
    pid = strtol(*argv, &ep, 10);
    if (!**argv || *ep)
      errx(2, "illegal process id: %s", *argv);
    if (kill(pid, numsig) == -1) {
      warn("%s", *argv);
      errors = 1;
    }
  }

  return (errors);
}

static int signame_to_signum(const char *sig)
{
  int n;

  if (!strncasecmp(sig, "SIG", (size_t)3))
    sig += 3;

  str2sig(sig, &n);

  return n;
}

static void nosig(const char *name)
{
  warnx("unknown signal %s; valid signals:", name);
  printsignals(stderr);
  exit(2);
}

static void printsignals(FILE *fp)
{
  int n;

  for (n = 1; n < SIGNUM_BOUND / 2; n++) {
    char buf2[32];

    sig2str(n, buf2);

    (void)fprintf(fp, "%s ", buf2);
  }

  printf("\n");
}

static void printsigdesc(FILE *fp)
{
  int n;

  for (n = 1; n < SIGNUM_BOUND / 2; n++) {
    int len;
    char *buf;
    char buf2[32];

    sig2str(n, buf2);
    len = 10 - strlen(buf2);

    (void)fprintf(fp, "%s", buf2);

    while(len--)
      fputc(' ', fp);

    buf = strsignal(n);

    (void)fprintf(fp, "%s\n", buf);
  }
}

static void usage(void)
{

  (void)fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n",
                "usage: kill [-s signal_name] pid ...",
                "       kill -l [exit_status]",
                "       kill -L [exit_status]",
                "       kill -signal_name pid ...",
                "       kill -signal_number pid ...");
  exit(2);
}

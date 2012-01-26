/*-
 * Copyright (c) 1983, 1992, 1993
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

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sysexits.h>
#include <unistd.h>

static int  build(char *, mode_t);
static void usage(void);

static int  vflag;

#define SET_LEN 6   /* initial # of bitcmd struct to malloc */
#define SET_LEN_INCR 4    /* # of bitcmd structs to add as needed */

typedef struct bitcmd {
  char  cmd;
  char  cmd2;
  mode_t  bits;
} BITCMD;

#define CMD2_CLR  0x01
#define CMD2_SET  0x02
#define CMD2_GBITS  0x04
#define CMD2_OBITS  0x08
#define CMD2_UBITS  0x10

static BITCMD *addcmd(BITCMD *, int, int, int, u_int);
static void  compress_mode(BITCMD *);
#ifdef SETMODE_DEBUG
static void  dumpmode(BITCMD *);
#endif

/*
 * Given the old mode and an array of bitcmd structures, apply the operations
 * described in the bitcmd structures to the old mode, and return the new mode.
 * Note that there is no '=' command; a strict assignment is just a '-' (clear
 * bits) followed by a '+' (set bits).
 */
static mode_t getmode(const void *bbox, mode_t omode)
{
  const BITCMD *set;
  mode_t clrval, newmode, value;

  set = (const BITCMD *)bbox;
  newmode = omode;
  for (value = 0;; set++)
    switch(set->cmd) {
      /*
       * When copying the user, group or other bits around, we "know"
       * where the bits are in the mode so that we can do shifts to
       * copy them around.  If we don't use shifts, it gets real
       * grundgy with lots of single bit checks and bit sets.
       */
    case 'u':
      value = (newmode & S_IRWXU) >> 6;
      goto common;

    case 'g':
      value = (newmode & S_IRWXG) >> 3;
      goto common;

    case 'o':
      value = newmode & S_IRWXO;
    common:     if (set->cmd2 & CMD2_CLR) {
        clrval =
          (set->cmd2 & CMD2_SET) ?  S_IRWXO : value;
        if (set->cmd2 & CMD2_UBITS)
          newmode &= ~((clrval<<6) & set->bits);
        if (set->cmd2 & CMD2_GBITS)
          newmode &= ~((clrval<<3) & set->bits);
        if (set->cmd2 & CMD2_OBITS)
          newmode &= ~(clrval & set->bits);
      }
      if (set->cmd2 & CMD2_SET) {
        if (set->cmd2 & CMD2_UBITS)
          newmode |= (value<<6) & set->bits;
        if (set->cmd2 & CMD2_GBITS)
          newmode |= (value<<3) & set->bits;
        if (set->cmd2 & CMD2_OBITS)
          newmode |= value & set->bits;
      }
      break;

    case '+':
      newmode |= set->bits;
      break;

    case '-':
      newmode &= ~set->bits;
      break;

    case 'X':
      if (omode & (S_IFDIR|S_IXUSR|S_IXGRP|S_IXOTH))
        newmode |= set->bits;
      break;

    case '\0':
    default:
#ifdef SETMODE_DEBUG
      (void)printf("getmode:%04o -> %04o\n", omode, newmode);
#endif
      return (newmode);
    }
}

#define ADDCMD(a, b, c, d)                              \
  if (set >= endset) {                                  \
    BITCMD *newset;                                     \
    setlen += SET_LEN_INCR;                             \
    newset = realloc(saveset, sizeof(BITCMD) * setlen); \
    if (!newset) {                                      \
      if (saveset)                                      \
        free(saveset);                                  \
      saveset = NULL;                                   \
      return (NULL);                                    \
    }                                                   \
    set = newset + (set - saveset);                     \
    saveset = newset;                                   \
    endset = newset + (setlen - 2);                     \
  }                                                     \
  set = addcmd(set, (a), (b), (c), (d))

#define STANDARD_BITS (S_ISUID|S_ISGID|S_IRWXU|S_IRWXG|S_IRWXO)

static void * setmode(const char *p)
{
  int perm, who;
  char op, *ep;
  BITCMD *set, *saveset, *endset;
  sigset_t sigset, sigoset;
  mode_t mask;
  int equalopdone=0, permXbits, setlen;
  long perml;

  if (!*p)
    return (NULL);

  /*
   * Get a copy of the mask for the permissions that are mask relative.
   * Flip the bits, we want what's not set.  Since it's possible that
   * the caller is opening files inside a signal handler, protect them
   * as best we can.
   */
  sigfillset(&sigset);
  (void)sigprocmask(SIG_BLOCK, &sigset, &sigoset);
  (void)umask(mask = umask(0));
  mask = ~mask;
  (void)sigprocmask(SIG_SETMASK, &sigoset, NULL);

  setlen = SET_LEN + 2;

  if ((set = malloc((u_int)(sizeof(BITCMD) * setlen))) == NULL)
    return (NULL);
  saveset = set;
  endset = set + (setlen - 2);

  /*
   * If an absolute number, get it and return; disallow non-octal digits
   * or illegal bits.
   */
  if (isdigit((unsigned char)*p)) {
    perml = strtol(p, &ep, 8);
    if (*ep || perml < 0 || perml & ~(STANDARD_BITS)) {
      free(saveset);
      return (NULL);
    }
    perm = (mode_t)perml;
    ADDCMD('=', (STANDARD_BITS), perm, mask);
    set->cmd = 0;
    return (saveset);
  }

  /*
   * Build list of structures to set/clear/copy bits as described by
   * each clause of the symbolic mode.
   */
  for (;;) {
    /* First, find out which bits might be modified. */
    for (who = 0;; ++p) {
      switch (*p) {
      case 'a':
        who |= STANDARD_BITS;
        break;
      case 'u':
        who |= S_ISUID|S_IRWXU;
        break;
      case 'g':
        who |= S_ISGID|S_IRWXG;
        break;
      case 'o':
        who |= S_IRWXO;
        break;
      default:
        goto getop;
      }
    }

  getop:    if ((op = *p++) != '+' && op != '-' && op != '=') {
      free(saveset);
      return (NULL);
    }
    if (op == '=')
      equalopdone = 0;

    for (perm = 0, permXbits = 0;; ++p) {
      switch (*p) {
      case 'r':
        perm |= S_IRUSR|S_IRGRP|S_IROTH;
        break;
      case 's':
        /* If only "other" bits ignore set-id. */
        if (!who || who & ~S_IRWXO)
          perm |= S_ISUID|S_ISGID;
        break;
        /* If only "other" bits ignore sticky. */
        break;
      case 'w':
        perm |= S_IWUSR|S_IWGRP|S_IWOTH;
        break;
      case 'X':
        permXbits = S_IXUSR|S_IXGRP|S_IXOTH;
        break;
      case 'x':
        perm |= S_IXUSR|S_IXGRP|S_IXOTH;
        break;
      case 'u':
      case 'g':
      case 'o':
        /*
         * When ever we hit 'u', 'g', or 'o', we have
         * to flush out any partial mode that we have,
         * and then do the copying of the mode bits.
         */
        if (perm) {
          ADDCMD(op, who, perm, mask);
          perm = 0;
        }
        if (op == '=')
          equalopdone = 1;
        if (op == '+' && permXbits) {
          ADDCMD('X', who, permXbits, mask);
          permXbits = 0;
        }
        ADDCMD(*p, who, op, mask);
        break;

      default:
        /*
         * Add any permissions that we haven't already
         * done.
         */
        if (perm || (op == '=' && !equalopdone)) {
          if (op == '=')
            equalopdone = 1;
          ADDCMD(op, who, perm, mask);
          perm = 0;
        }
        if (permXbits) {
          ADDCMD('X', who, permXbits, mask);
          permXbits = 0;
        }
        goto apply;
      }
    }

  apply:    if (!*p)
      break;
    if (*p != ',')
      goto getop;
    ++p;
  }
  set->cmd = 0;
#ifdef SETMODE_DEBUG
  (void)printf("Before compress_mode()\n");
  dumpmode(saveset);
#endif
  compress_mode(saveset);
#ifdef SETMODE_DEBUG
  (void)printf("After compress_mode()\n");
  dumpmode(saveset);
#endif
  return (saveset);
}

static BITCMD * addcmd(BITCMD *set, int op, int who, int oparg, u_int mask)
{
  switch (op) {
  case '=':
    set->cmd = '-';
    set->bits = who ? who : STANDARD_BITS;
    set++;

    op = '+';
    /* FALLTHROUGH */
  case '+':
  case '-':
  case 'X':
    set->cmd = op;
    set->bits = (who ? who : mask) & oparg;
    break;

  case 'u':
  case 'g':
  case 'o':
    set->cmd = op;
    if (who) {
      set->cmd2 = ((who & S_IRUSR) ? CMD2_UBITS : 0) |
        ((who & S_IRGRP) ? CMD2_GBITS : 0) |
        ((who & S_IROTH) ? CMD2_OBITS : 0);
      set->bits = (mode_t)~0;
    } else {
      set->cmd2 = CMD2_UBITS | CMD2_GBITS | CMD2_OBITS;
      set->bits = mask;
    }

    if (oparg == '+')
      set->cmd2 |= CMD2_SET;
    else if (oparg == '-')
      set->cmd2 |= CMD2_CLR;
    else if (oparg == '=')
      set->cmd2 |= CMD2_SET|CMD2_CLR;
    break;
  }
  return (set + 1);
}

#ifdef SETMODE_DEBUG
static void dumpmode(BITCMD *set)
{
  for (; set->cmd; ++set)
    (void)printf("cmd: '%c' bits %04o%s%s%s%s%s%s\n",
                 set->cmd, set->bits, set->cmd2 ? " cmd2:" : "",
                 set->cmd2 & CMD2_CLR ? " CLR" : "",
                 set->cmd2 & CMD2_SET ? " SET" : "",
                 set->cmd2 & CMD2_UBITS ? " UBITS" : "",
                 set->cmd2 & CMD2_GBITS ? " GBITS" : "",
                 set->cmd2 & CMD2_OBITS ? " OBITS" : "");
}
#endif

/*
 * Given an array of bitcmd structures, compress by compacting consecutive
 * '+', '-' and 'X' commands into at most 3 commands, one of each.  The 'u',
 * 'g' and 'o' commands continue to be separate.  They could probably be
 * compacted, but it's not worth the effort.
 */
static void compress_mode(BITCMD *set)
{
  BITCMD *nset;
  int setbits, clrbits, Xbits, op;

  for (nset = set;;) {
    /* Copy over any 'u', 'g' and 'o' commands. */
    while ((op = nset->cmd) != '+' && op != '-' && op != 'X') {
      *set++ = *nset++;
      if (!op)
        return;
    }

    for (setbits = clrbits = Xbits = 0;; nset++) {
      if ((op = nset->cmd) == '-') {
        clrbits |= nset->bits;
        setbits &= ~nset->bits;
        Xbits &= ~nset->bits;
      } else if (op == '+') {
        setbits |= nset->bits;
        clrbits &= ~nset->bits;
        Xbits &= ~nset->bits;
      } else if (op == 'X')
        Xbits |= nset->bits & ~setbits;
      else
        break;
    }
    if (clrbits) {
      set->cmd = '-';
      set->cmd2 = 0;
      set->bits = clrbits;
      set++;
    }
    if (setbits) {
      set->cmd = '+';
      set->cmd2 = 0;
      set->bits = setbits;
      set++;
    }
    if (Xbits) {
      set->cmd = 'X';
      set->cmd2 = 0;
      set->bits = Xbits;
      set++;
    }
  }
}

int main(int argc, char *argv[])
{
  int ch, exitval, success, pflag;
  mode_t omode;
  void *set = NULL;
  char *mode;

  struct option opts[] = {
    { "mode", required_argument, NULL, 'm' },
    { "parents", no_argument, NULL, 'p' },
    { "verbose", no_argument, NULL, 'v' },
    { "context", required_argument, NULL, 'Z' },
    { NULL, 0, NULL, 0 }
  };

  omode = pflag = 0;
  mode = NULL;
  while ((ch = getopt_long(argc, argv, "m:pvZ:", opts, NULL)) != -1)
    switch(ch) {
    case 'm':
      mode = optarg;
      break;
    case 'p':
      pflag = 1;
      break;
    case 'v':
      vflag = 1;
      break;
    case 'Z':
      warnx("SELinux not implemented");
      break;
    default:
      usage();
    }

  argc -= optind;
  argv += optind;
  if (argv[0] == NULL)
    usage();

  if (mode == NULL) {
    omode = S_IRWXU | S_IRWXG | S_IRWXO;
  } else {
    if ((set = setmode(mode)) == NULL)
      errx(1, "invalid file mode: %s", mode);
    omode = getmode(set, S_IRWXU | S_IRWXG | S_IRWXO);
    free(set);
  }

  for (exitval = 0; *argv != NULL; ++argv) {
    if (pflag) {
      success = build(*argv, omode);
    } else if (mkdir(*argv, omode) < 0) {
      if (errno == ENOTDIR || errno == ENOENT)
        warn("%s", dirname(*argv));
      else
        warn("%s", *argv);
      success = 0;
    } else {
      success = 1;
      if (vflag)
        (void)printf("%s\n", *argv);
    }
    if (!success)
      exitval = 1;
    /*
     * The mkdir() and umask() calls both honor only the low
     * nine bits, so if you try to set a mode including the
     * sticky, setuid, setgid bits you lose them.  Don't do
     * this unless the user has specifically requested a mode,
     * as chmod will (obviously) ignore the umask.  Do this
     * on newly created directories only.
     */
    if (success == 1 && mode != NULL && chmod(*argv, omode) == -1) {
      warn("%s", *argv);
      exitval = 1;
    }
  }
  exit(exitval);
}

/*
 * Returns 1 if a directory has been created,
 * 2 if it already existed, and 0 on failure.
 */
int build(char *path, mode_t omode)
{
  struct stat sb;
  mode_t numask, oumask;
  int first, last, retval;
  char *p;

  p = path;
  oumask = 0;
  retval = 1;
  if (p[0] == '/')    /* Skip leading '/'. */
    ++p;
  for (first = 1, last = 0; !last ; ++p) {
    if (p[0] == '\0')
      last = 1;
    else if (p[0] != '/')
      continue;
    *p = '\0';
    if (!last && p[1] == '\0')
      last = 1;
    if (first) {
      /*
       * POSIX 1003.2:
       * For each dir operand that does not name an existing
       * directory, effects equivalent to those caused by the
       * following command shall occcur:
       *
       * mkdir -p -m $(umask -S),u+wx $(dirname dir) &&
       *    mkdir [-m mode] dir
       *
       * We change the user's umask and then restore it,
       * instead of doing chmod's.
       */
      oumask = umask(0);
      numask = oumask & ~(S_IWUSR | S_IXUSR);
      (void)umask(numask);
      first = 0;
    }
    if (last)
      (void)umask(oumask);
    if (mkdir(path, last ? omode : S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
      if (errno == EEXIST || errno == EISDIR) {
        if (stat(path, &sb) < 0) {
          warn("%s", path);
          retval = 0;
          break;
        } else if (!S_ISDIR(sb.st_mode)) {
          if (last)
            errno = EEXIST;
          else
            errno = ENOTDIR;
          warn("%s", path);
          retval = 0;
          break;
        }
        if (last)
          retval = 2;
      } else {
        warn("%s", path);
        retval = 0;
        break;
      }
    } else if (vflag)
      printf("%s\n", path);
    if (!last)
      *p = '/';
  }
  if (!first && !last)
    (void)umask(oumask);
  return (retval);
}

void usage(void)
{
  (void)fprintf(stderr, "usage: mkdir [-pv] [-m mode] directory_name ...\n");
  exit (EX_USAGE);
}

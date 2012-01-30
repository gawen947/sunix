/* File: bsd.c
   Time-stamp: <2012-01-30 21:37:29 gawen>

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

#define _GNU_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <err.h>

#include "bsd.h"

#ifndef NO_HTABLE

#include "htable.h"

/* Hashtable for UID and GID */
static htable_t uid_ht;
static htable_t gid_ht;

#endif /* NO_HTABLE */

#ifndef NO_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size)
{
  size_t i;

  for(i = 0 ; i < size && src[i] != '\0' ; i++)
    dst[i] = src[i];
  dst[i] = '\0';

  return i;
}
#endif /* NO_STRLCPY */

#ifndef NO_STRMODE
void strmode(mode_t mode, char *bp)
{
  memset(bp, '-', 11);

  /* file type */
  switch(mode & S_IFMT) {
  case(S_IFSOCK):
    *bp = 's';
    break;
  case(S_IFLNK):
    *bp = 'b';
    break;
  case(S_IFREG):
    *bp = '-';
    break;
  case(S_IFDIR):
    *bp = 'd';
    break;
  case(S_IFBLK):
    *bp = 'b';
    break;
  case(S_IFIFO):
    *bp = 'p';
    break;
  default:
    *bp = '?';
    break;
  }

  /* basic permissions */
  if(mode & S_IRUSR)
    bp[1] = 'r';
  if(mode & S_IWUSR)
    bp[2] = 'w';
  if(mode & S_IXUSR)
    bp[3] = 'x';
  if(mode & S_IRGRP)
    bp[4] = 'r';
  if(mode & S_IWGRP)
    bp[5] = 'w';
  if(mode & S_IXGRP)
    bp[6] = 'x';
  if(mode & S_IROTH)
    bp[7] = 'r';
  if(mode & S_IWOTH)
    bp[8] = 'w';
  if(mode & S_IXOTH)
    bp[9] = 'x';

  /* extended permissions */
  if(mode & S_ISUID) {
    if(bp[3] == 'x')
      bp[3] = 's';
    else
      bp[3] = 'S';
  }
  if(mode & S_ISGID) {
    if(bp[6] == 'x')
      bp[6] = 's';
    else
      bp[6] = 'S';
  }
  if(mode & S_ISVTX) {
    if(bp[9] == 'x')
      bp[9] = 't';
    else
      bp[9] = 'T';
  }

  /* last NUL character */
  bp[10] = '\0';
}
#endif /* NO_STRMODE */


#ifndef NO_HTABLE
/* We use Knuth's multiplicative hash */
static uint32_t knuth_hash(const void *key)
{
  register uint32_t hash = (unsigned long)key;

  hash *= 0x9e3779b1;

  return hash;
}

/* UID/GID comparison */
static bool id_cmp(const void *s1, const void *s2)
{
  return s1 == s2;
}

/* Destroy data from the hashtable which is only a string here */
static void str_destroy(void *data)
{
  free(data);
}

static void *retrieve_user(const void *key, void *optarg)
{
  uintptr_t uid = (uintptr_t)key;
  struct passwd *u_uid = getpwuid(uid);

  /* In case something goes wrong we use the uid as name */
  if(!u_uid) {
    char *str = malloc(32);
    if(!str)
      err(1, "malloc");
    (void)sprintf(str, "%d", uid);
    return str;
  }

  return strdup(u_uid->pw_name);
}

static void *retrieve_group(const void *key, void *optarg)
{
  uintptr_t gid = (uintptr_t)key;
  struct group *g_gid = getgrgid(gid);

  /* In case something goes wrong we use the gid as name */
  if(!g_gid) {
    char *str = malloc(32);
    if(!str)
      err(1, "malloc");
    (void)sprintf(str, "%d", gid);
    return str;
  }

  return strdup(g_gid->gr_name);
}

void init_uid_ht(void)
{
  uid_ht = ht_create(1024, knuth_hash, id_cmp, str_destroy);

  if(!uid_ht)
    err(1, "ht_create");
}

void init_gid_ht(void)
{
  gid_ht = ht_create(1024, knuth_hash, id_cmp, str_destroy);

  if(!gid_ht)
    err(1, "ht_create");
}

void free_uid_ht(void)
{
  ht_destroy(uid_ht);
}

void free_gid_ht(void)
{
  ht_destroy(gid_ht);
}

const char * user_from_uid(uid_t uid, int nouser)
{
  uintptr_t p_uid = uid;
  return ht_lookup(uid_ht, (const void *)p_uid, retrieve_user, NULL);
}

const char * group_from_gid(gid_t gid, int nogroup)
{
  uintptr_t p_gid = gid;
  return ht_lookup(gid_ht, (const void *)p_gid, retrieve_group, NULL);
}
#endif /* NO_HTABLE */

#ifndef NO_SETMODE
# define SET_LEN 6   /* initial # of bitcmd struct to malloc */
# define SET_LEN_INCR 4    /* # of bitcmd structs to add as needed */

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
mode_t getmode(const void *bbox, mode_t omode)
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

#define STANDARD_BITS (S_ISGID|S_ISVTX|S_ISUID|S_ISGID|S_IRWXU|S_IRWXG|S_IRWXO)

void * setmode(const char *p)
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
void dumpmode(BITCMD *set)
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

#endif /* NO_SETMODE */

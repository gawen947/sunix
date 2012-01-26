/*-
 * Copyright (c) 1989, 1993, 1994
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Michael Fischbein.
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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <grp.h>
#include <inttypes.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <getopt.h>
#include <pwd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef COLORLS
#include <termcap.h>
#include <signal.h>
#endif

#include "bsd.h"

#define NO_PRINT  1

#define HUMANVALSTR_LEN 10

typedef struct {
  FTSENT *list;
  u_long btotal;
  int entries;
  int maxlen;
  u_int s_block;
  u_int s_flags;
  u_int s_label;
  u_int s_group;
  u_int s_inode;
  u_int s_nlink;
  u_int s_size;
  u_int s_user;
} DISPLAY;

typedef struct {
  char *user;
  char *group;
  char *flags;
  char *label;
  char data[1];
} NAMES;

/*
 * Upward approximation of the maximum number of characters needed to
 * represent a value of integral type t as a string, excluding the
 * NUL terminator, with provision for a sign.
 */
#define STRBUF_SIZEOF(t)  (1 + CHAR_BIT * sizeof(t) / 3 + 1)

/*
 * MAKENINES(n) turns n into (10**n)-1.  This is useful for converting a width
 * into a number that wide in decimal.
 * XXX: Overflows are not considered.
 */
#define MAKENINES(n)                                \
  do {                                              \
    intmax_t i;                                     \
                                                    \
    /* Use a loop as all values of n are small. */  \
    for (i = 1; n > 0; i *= 10)                     \
      n--;                                          \
    n = i - 1;                                      \
  } while(0)

static void  display(const FTSENT *, FTSENT *, int);
static int   mastercmp(const FTSENT **, const FTSENT **);
static void  traverse(int, char **, int);

static void (*printfcn)(const DISPLAY *);
static int (*sortfcn)(const FTSENT *, const FTSENT *);

long blocksize;     /* block size units */
int termwidth = 80;   /* default terminal width */

/* flags */
static int f_accesstime;  /* use time of last access */
static int f_humanval;    /* show human-readable file sizes */
static int f_inode;   /* print inode */
static int f_kblocks;   /* print size in kilobytes */
static int f_listdir;   /* list actual directory, not contents */
static int f_listdot;   /* list files beginning with . */
static int f_noautodot;   /* do not automatically enable -A for root */
static int f_longform;    /* long listing format */
static int f_nofollow;    /* don't follow symbolic link arguments */
static int f_nonprint;    /* show unprintables as ? */
static int f_nosort;    /* don't sort output */
static int f_notabs;    /* don't use tab-separated multi-col output */
static int f_numericonly; /* don't convert uid/gid to name */
static int f_octal;   /* show unprintables as \xxx */
static int f_octal_escape;  /* like f_octal but use C escapes if possible */
static int f_recursive;   /* ls subdirectories also */
static int f_reversesort; /* reverse whatever sort is used */
static int f_sectime;   /* print the real time for all files */
static int f_singlecol;   /* use single column output */
static int f_size;    /* list size in short listing */
static int f_author; /* display author in long form */
static int f_slash;   /* similar to f_type, but only for dirs */
static int f_sortacross;  /* sort across rows, not down columns */
static int f_statustime;  /* use time of last mode change */
static int f_stream;    /* stream the output, separate with commas */
static int f_timesort;    /* sort by time vice name */
static char *f_timeformat;      /* user-specified time format */
static int f_sizesort;
static int f_type;    /* add type character for non-regular files */
static int f_whiteout;    /* show whiteout entries */
#ifdef COLORLS
static int f_color;   /* add type in color for non-regular files */

static char *ansi_bgcol;   /* ANSI sequence to set background colour */
static char *ansi_fgcol;   /* ANSI sequence to set foreground colour */
static char *ansi_coloff;    /* ANSI sequence to reset colours */
static char *attrs_off;    /* ANSI sequence to turn off attributes */
static char *enter_bold;   /* ANSI sequence to set color to bold mode */
#endif

static int rval;

static int prn_normal(const char *s)
{
  mbstate_t mbs;
  wchar_t wc;
  int i, n;
  size_t clen;

  memset(&mbs, 0, sizeof(mbs));
  n = 0;
  while ((clen = mbrtowc(&wc, s, MB_LEN_MAX, &mbs)) != 0) {
    if (clen == (size_t)-2) {
      n += printf("%s", s);
      break;
    }
    if (clen == (size_t)-1) {
      memset(&mbs, 0, sizeof(mbs));
      putchar((unsigned char)*s);
      s++;
      n++;
      continue;
    }
    for (i = 0; i < (int)clen; i++)
      putchar((unsigned char)s[i]);
    s += clen;
    if (iswprint(wc))
      n += wcwidth(wc);
  }
  return (n);
}

static int prn_printable(const char *s)
{
  mbstate_t mbs;
  wchar_t wc;
  int i, n;
  size_t clen;

  memset(&mbs, 0, sizeof(mbs));
  n = 0;
  while ((clen = mbrtowc(&wc, s, MB_LEN_MAX, &mbs)) != 0) {
    if (clen == (size_t)-1) {
      putchar('?');
      s++;
      n++;
      memset(&mbs, 0, sizeof(mbs));
      continue;
    }
    if (clen == (size_t)-2) {
      putchar('?');
      n++;
      break;
    }
    if (!iswprint(wc)) {
      putchar('?');
      s += clen;
      n++;
      continue;
    }
    for (i = 0; i < (int)clen; i++)
      putchar((unsigned char)s[i]);
    s += clen;
    n += wcwidth(wc);
  }
  return (n);
}

/*
 * The fts system makes it difficult to replace fts_name with a different-
 * sized string, so we just calculate the real length here and do the
 * conversion in prn_octal()
 *
 * XXX when using f_octal_escape (-b) rather than f_octal (-B), the
 * length computed by len_octal may be too big. I just can't be buggered
 * to fix this as an efficient fix would involve a lookup table. Same goes
 * for the rather inelegant code in prn_octal.
 *
 *                                              DES 1998/04/23
 */

static size_t len_octal(const char *s, int len)
{
  mbstate_t mbs;
  wchar_t wc;
  size_t clen, r;

  memset(&mbs, 0, sizeof(mbs));
  r = 0;
  while (len != 0 && (clen = mbrtowc(&wc, s, len, &mbs)) != 0) {
    if (clen == (size_t)-1) {
      r += 4;
      s++;
      len--;
      memset(&mbs, 0, sizeof(mbs));
      continue;
    }
    if (clen == (size_t)-2) {
      r += 4 * len;
      break;
    }
    if (iswprint(wc))
      r++;
    else
      r += 4 * clen;
    s += clen;
  }
  return (r);
}

static int prn_octal(const char *s)
{
  static const char esc[] = "\\\\\"\"\aa\bb\ff\nn\rr\tt\vv";
  const char *p;
  mbstate_t mbs;
  wchar_t wc;
  size_t clen;
  unsigned char ch;
  int goodchar, i, len, prtlen;

  memset(&mbs, 0, sizeof(mbs));
  len = 0;
  while ((clen = mbrtowc(&wc, s, MB_LEN_MAX, &mbs)) != 0) {
    goodchar = clen != (size_t)-1 && clen != (size_t)-2;
    if (goodchar && iswprint(wc) && wc != L'\"' && wc != L'\\') {
      for (i = 0; i < (int)clen; i++)
        putchar((unsigned char)s[i]);
      len += wcwidth(wc);
    } else if (goodchar && f_octal_escape && wc >= 0 &&
               wc <= (wchar_t)UCHAR_MAX &&
               (p = strchr(esc, (char)wc)) != NULL) {
      putchar('\\');
      putchar(p[1]);
      len += 2;
    } else {
      if (goodchar)
        prtlen = clen;
      else if (clen == (size_t)-1)
        prtlen = 1;
      else
        prtlen = strlen(s);
      for (i = 0; i < prtlen; i++) {
        ch = (unsigned char)s[i];
        putchar('\\');
        putchar('0' + (ch >> 6));
        putchar('0' + ((ch >> 3) & 7));
        putchar('0' + (ch & 7));
        len += 4;
      }
    }
    if (clen == (size_t)-2)
      break;
    if (clen == (size_t)-1) {
      memset(&mbs, 0, sizeof(mbs));
      s++;
    } else
      s += clen;
  }
  return (len);
}

static int  printaname(const FTSENT *, u_long, u_long);
static void printdev(size_t, dev_t);
static void printlink(const FTSENT *);
static void printtime(time_t);
static int  printtype(u_int);
static void printsize(size_t, off_t);
#ifdef COLORLS
static void endcolor(int);
static int  colortype(mode_t);
#endif

#define IS_NOPRINT(p) ((p)->fts_number == NO_PRINT)

#ifdef COLORLS
/* Most of these are taken from <sys/stat.h> */
typedef enum Colors {
  C_DIR,      /* directory */
  C_LNK,      /* symbolic link */
  C_SOCK,     /* socket */
  C_FIFO,     /* pipe */
  C_EXEC,     /* executable */
  C_BLK,      /* block special */
  C_CHR,      /* character special */
  C_SUID,     /* setuid executable */
  C_SGID,     /* setgid executable */
  C_WSDIR,    /* directory writeble to others, with sticky
               * bit */
  C_WDIR,     /* directory writeble to others, without
               * sticky bit */
  C_NUMCOLORS   /* just a place-holder */
} Colors;

static const char *defcolors = "exfxcxdxbxegedabagacad";

/* colors for file types */
static struct {
  int num[2];
  int bold;
} colors[C_NUMCOLORS];
#endif

static int namecmp(const FTSENT *a, const FTSENT *b)
{

  return (strcoll(a->fts_name, b->fts_name));
}

static int revnamecmp(const FTSENT *a, const FTSENT *b)
{

  return (strcoll(b->fts_name, a->fts_name));
}

static int modcmp(const FTSENT *a, const FTSENT *b)
{

  if (b->fts_statp->st_mtim.tv_sec >
      a->fts_statp->st_mtim.tv_sec)
    return (1);
  if (b->fts_statp->st_mtim.tv_sec <
      a->fts_statp->st_mtim.tv_sec)
    return (-1);
  if (b->fts_statp->st_mtim.tv_nsec >
      a->fts_statp->st_mtim.tv_nsec)
    return (1);
  if (b->fts_statp->st_mtim.tv_nsec <
      a->fts_statp->st_mtim.tv_nsec)
    return (-1);
  return (strcoll(a->fts_name, b->fts_name));
}

static int revmodcmp(const FTSENT *a, const FTSENT *b)
{

  return (modcmp(b, a));
}

static int acccmp(const FTSENT *a, const FTSENT *b)
{

  if (b->fts_statp->st_atim.tv_sec >
      a->fts_statp->st_atim.tv_sec)
    return (1);
  if (b->fts_statp->st_atim.tv_sec <
      a->fts_statp->st_atim.tv_sec)
    return (-1);
  if (b->fts_statp->st_atim.tv_nsec >
      a->fts_statp->st_atim.tv_nsec)
    return (1);
  if (b->fts_statp->st_atim.tv_nsec <
      a->fts_statp->st_atim.tv_nsec)
    return (-1);
  return (strcoll(a->fts_name, b->fts_name));
}

static int revacccmp(const FTSENT *a, const FTSENT *b)
{

  return (acccmp(b, a));
}

static int statcmp(const FTSENT *a, const FTSENT *b)
{

  if (b->fts_statp->st_ctim.tv_sec >
      a->fts_statp->st_ctim.tv_sec)
    return (1);
  if (b->fts_statp->st_ctim.tv_sec <
      a->fts_statp->st_ctim.tv_sec)
    return (-1);
  if (b->fts_statp->st_ctim.tv_nsec >
      a->fts_statp->st_ctim.tv_nsec)
    return (1);
  if (b->fts_statp->st_ctim.tv_nsec <
      a->fts_statp->st_ctim.tv_nsec)
    return (-1);
  return (strcoll(a->fts_name, b->fts_name));
}

static int revstatcmp(const FTSENT *a, const FTSENT *b)
{

  return (statcmp(b, a));
}

static int sizecmp(const FTSENT *a, const FTSENT *b)
{

  if (b->fts_statp->st_size > a->fts_statp->st_size)
    return (1);
  if (b->fts_statp->st_size < a->fts_statp->st_size)
    return (-1);
  return (strcoll(a->fts_name, b->fts_name));
}

static int revsizecmp(const FTSENT *a, const FTSENT *b)
{

  return (sizecmp(b, a));
}

static void printscol(const DISPLAY *dp)
{
  FTSENT *p;

  for (p = dp->list; p; p = p->fts_link) {
    if (IS_NOPRINT(p))
      continue;
    (void)printaname(p, dp->s_inode, dp->s_block);
    (void)putchar('\n');
  }
}

/*
 * print name in current style
 */
static int printname(const char *name)
{
  if (f_octal || f_octal_escape)
    return prn_octal(name);
  else if (f_nonprint)
    return prn_printable(name);
  else
    return prn_normal(name);
}

static void printlong(const DISPLAY *dp)
{
  struct stat *sp;
  FTSENT *p;
  NAMES *np;
  char buf[20];
#ifdef COLORLS
  int color_printed = 0;
#endif

  if ((dp->list == NULL || dp->list->fts_level != FTS_ROOTLEVEL) &&
      (f_longform || f_size)) {
    (void)printf("total %lu\n", howmany(dp->btotal, blocksize));
  }

  for (p = dp->list; p; p = p->fts_link) {
    if (IS_NOPRINT(p))
      continue;
    sp = p->fts_statp;
    if (f_inode)
      (void)printf("%*lu ", dp->s_inode, (u_long)sp->st_ino);
    if (f_size)
      (void)printf("%*jd ",
                   dp->s_block, howmany(sp->st_blocks, blocksize));
    strmode(sp->st_mode, buf);
    np = p->fts_pointer;
    if(f_author)
      (void)printf("%s %*u %s %-*s  %-*s  ", buf, dp->s_nlink,
                   sp->st_nlink, np->user, dp->s_user, np->user, dp->s_group,
                   np->group);
    else
      (void)printf("%s %*u %-*s  %-*s  ", buf, dp->s_nlink,
                   sp->st_nlink, dp->s_user, np->user, dp->s_group,
                   np->group);
    if (S_ISCHR(sp->st_mode) || S_ISBLK(sp->st_mode))
      printdev(dp->s_size, sp->st_rdev);
    else
      printsize(dp->s_size, sp->st_size);
    if (f_accesstime)
      printtime(sp->st_atime);
    else if (f_statustime)
      printtime(sp->st_ctime);
    else
      printtime(sp->st_mtime);
#ifdef COLORLS
    if (f_color)
      color_printed = colortype(sp->st_mode);
#endif
    (void)printname(p->fts_name);
#ifdef COLORLS
    if (f_color && color_printed)
      endcolor(0);
#endif
    if (f_type)
      (void)printtype(sp->st_mode);
    if (S_ISLNK(sp->st_mode))
      printlink(p);
    (void)putchar('\n');
  }
}

static void printstream(const DISPLAY *dp)
{
  FTSENT *p;
  int chcnt;

  for (p = dp->list, chcnt = 0; p; p = p->fts_link) {
    if (p->fts_number == NO_PRINT)
      continue;
    /* XXX strlen does not take octal escapes into account. */
    if (strlen(p->fts_name) + chcnt +
        (p->fts_link ? 2 : 0) >= (unsigned)termwidth) {
      putchar('\n');
      chcnt = 0;
    }
    chcnt += printaname(p, dp->s_inode, dp->s_block);
    if (p->fts_link) {
      printf(", ");
      chcnt += 2;
    }
  }
  if (chcnt)
    putchar('\n');
}

static void printcol(const DISPLAY *dp)
{
  static FTSENT **array;
  static int lastentries = -1;
  FTSENT *p;
  FTSENT **narray;
  int base;
  int chcnt;
  int cnt;
  int col;
  int colwidth;
  int endcol;
  int num;
  int numcols;
  int numrows;
  int row;
  int tabwidth;

  if (f_notabs)
    tabwidth = 1;
  else
    tabwidth = 8;

  /*
   * Have to do random access in the linked list -- build a table
   * of pointers.
   */
  if (dp->entries > lastentries) {
    if ((narray =
         realloc(array, dp->entries * sizeof(FTSENT *))) == NULL) {
      warn(NULL);
      printscol(dp);
      return;
    }
    lastentries = dp->entries;
    array = narray;
  }
  for (p = dp->list, num = 0; p; p = p->fts_link)
    if (p->fts_number != NO_PRINT)
      array[num++] = p;

  colwidth = dp->maxlen;
  if (f_inode)
    colwidth += dp->s_inode + 1;
  if (f_size)
    colwidth += dp->s_block + 1;
  if (f_type)
    colwidth += 1;

  colwidth = (colwidth + tabwidth) & ~(tabwidth - 1);
  if (termwidth < 2 * colwidth) {
    printscol(dp);
    return;
  }
  numcols = termwidth / colwidth;
  numrows = num / numcols;
  if (num % numcols)
    ++numrows;

  if ((dp->list == NULL || dp->list->fts_level != FTS_ROOTLEVEL) &&
      (f_longform || f_size)) {
    (void)printf("total %lu\n", howmany(dp->btotal, blocksize));
  }

  base = 0;
  for (row = 0; row < numrows; ++row) {
    endcol = colwidth;
    if (!f_sortacross)
      base = row;
    for (col = 0, chcnt = 0; col < numcols; ++col) {
      chcnt += printaname(array[base], dp->s_inode,
                          dp->s_block);
      if (f_sortacross)
        base++;
      else
        base += numrows;
      if (base >= num)
        break;
      while ((cnt = ((chcnt + tabwidth) & ~(tabwidth - 1)))
             <= endcol) {
        if (f_sortacross && col + 1 >= numcols)
          break;
        (void)putchar(f_notabs ? ' ' : '\t');
        chcnt = cnt;
      }
      endcol += colwidth;
    }
    (void)putchar('\n');
  }
}

/*
 * print [inode] [size] name
 * return # of characters printed, no trailing characters.
 */
static int printaname(const FTSENT *p, u_long inodefield, u_long sizefield)
{
  struct stat *sp;
  int chcnt;
#ifdef COLORLS
  int color_printed = 0;
#endif

  sp = p->fts_statp;
  chcnt = 0;
  if (f_inode)
    chcnt += printf("%*lu ", (int)inodefield, (u_long)sp->st_ino);
  if (f_size)
    chcnt += printf("%*jd ",
                    (int)sizefield, howmany(sp->st_blocks, blocksize));
#ifdef COLORLS
  if (f_color)
    color_printed = colortype(sp->st_mode);
#endif
  chcnt += printname(p->fts_name);
#ifdef COLORLS
  if (f_color && color_printed)
    endcolor(0);
#endif
  if (f_type)
    chcnt += printtype(sp->st_mode);
  return (chcnt);
}

/*
 * Print device special file major and minor numbers.
 */
static void printdev(size_t width, dev_t dev)
{

  (void)printf("%#*jx ", (u_int)width, (uintmax_t)dev);
}

static void printtime(time_t ftime)
{
  char longstring[80];
  static time_t now = 0;
  const char *format;
  static int d_first = -1;

  if (d_first < 0)
    d_first = (*nl_langinfo(D_FMT) == 'd');
  if (now == 0)
    now = time(NULL);

#define SIXMONTHS ((365 / 2) * 86400)
  if (f_timeformat)  /* user specified format */
    format = f_timeformat;
  else if (f_sectime)
    /* mmm dd hh:mm:ss yyyy || dd mmm hh:mm:ss yyyy */
    format = d_first ? "%e %b %T %Y" : "%b %e %T %Y";
  else if (ftime + SIXMONTHS > now && ftime < now + SIXMONTHS)
    /* mmm dd hh:mm || dd mmm hh:mm */
    format = d_first ? "%e %b %R" : "%b %e %R";
  else
    /* mmm dd  yyyy || dd mmm  yyyy */
    format = d_first ? "%e %b  %Y" : "%b %e  %Y";
  strftime(longstring, sizeof(longstring), format, localtime(&ftime));
  fputs(longstring, stdout);
  fputc(' ', stdout);
}

static int printtype(u_int mode)
{

  if (f_slash) {
    if ((mode & S_IFMT) == S_IFDIR) {
      (void)putchar('/');
      return (1);
    }
    return (0);
  }

  switch (mode & S_IFMT) {
  case S_IFDIR:
    (void)putchar('/');
    return (1);
  case S_IFIFO:
    (void)putchar('|');
    return (1);
  case S_IFLNK:
    (void)putchar('@');
    return (1);
  case S_IFSOCK:
    (void)putchar('=');
    return (1);
  default:
    break;
  }
  if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
    (void)putchar('*');
    return (1);
  }
  return (0);
}

#ifdef COLORLS
static int putch(int c)
{
  (void)putchar(c);
  return 0;
}

static int writech(int c)
{
  char tmp = (char)c;

  (void)write(STDOUT_FILENO, &tmp, 1);
  return 0;
}

static void printcolor(Colors c)
{
  char *ansiseq;

  if (colors[c].bold)
    tputs(enter_bold, 1, putch);

  if (colors[c].num[0] != -1) {
    ansiseq = tgoto(ansi_fgcol, 0, colors[c].num[0]);
    if (ansiseq)
      tputs(ansiseq, 1, putch);
  }
  if (colors[c].num[1] != -1) {
    ansiseq = tgoto(ansi_bgcol, 0, colors[c].num[1]);
    if (ansiseq)
      tputs(ansiseq, 1, putch);
  }
}

static void endcolor(int sig)
{
  tputs(ansi_coloff, 1, sig ? writech : putch);
  tputs(attrs_off, 1, sig ? writech : putch);
}

static int colortype(mode_t mode)
{
  switch (mode & S_IFMT) {
  case S_IFDIR:
    printcolor(C_DIR);
    return (1);
  case S_IFLNK:
    printcolor(C_LNK);
    return (1);
  case S_IFSOCK:
    printcolor(C_SOCK);
    return (1);
  case S_IFIFO:
    printcolor(C_FIFO);
    return (1);
  case S_IFBLK:
    printcolor(C_BLK);
    return (1);
  case S_IFCHR:
    printcolor(C_CHR);
    return (1);
  default:;
  }
  if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
    if (mode & S_ISUID)
      printcolor(C_SUID);
    else if (mode & S_ISGID)
      printcolor(C_SGID);
    else
      printcolor(C_EXEC);
    return (1);
  }
  return (0);
}

static void parsecolors(const char *cs)
{
  int i;
  int j;
  size_t len;
  char c[2];
  short legacy_warn = 0;

  if (cs == NULL)
    cs = "";  /* LSCOLORS not set */
  len = strlen(cs);
  for (i = 0; i < (int)C_NUMCOLORS; i++) {
    colors[i].bold = 0;

    if (len <= 2 * (size_t)i) {
      c[0] = defcolors[2 * i];
      c[1] = defcolors[2 * i + 1];
    } else {
      c[0] = cs[2 * i];
      c[1] = cs[2 * i + 1];
    }
    for (j = 0; j < 2; j++) {
      /* Legacy colours used 0-7 */
      if (c[j] >= '0' && c[j] <= '7') {
        colors[i].num[j] = c[j] - '0';
        if (!legacy_warn) {
          warnx("LSCOLORS should use "
                "characters a-h instead of 0-9 ("
                "see the manual page)");
        }
        legacy_warn = 1;
      } else if (c[j] >= 'a' && c[j] <= 'h')
        colors[i].num[j] = c[j] - 'a';
      else if (c[j] >= 'A' && c[j] <= 'H') {
        colors[i].num[j] = c[j] - 'A';
        colors[i].bold = 1;
      } else if (tolower((unsigned char)c[j]) == 'x')
        colors[i].num[j] = -1;
      else {
        warnx("invalid character '%c' in LSCOLORS"
              " env var", c[j]);
        colors[i].num[j] = -1;
      }
    }
  }
}

static void colorquit(int sig)
{
  endcolor(sig);

  (void)signal(sig, SIG_DFL);
  (void)kill(getpid(), sig);
}

#endif /* COLORLS */

static void printlink(const FTSENT *p)
{
  int lnklen;
  char name[MAXPATHLEN + 1];
  char path[MAXPATHLEN + 1];

  if (p->fts_level == FTS_ROOTLEVEL)
    (void)snprintf(name, sizeof(name), "%s", p->fts_name);
  else
    (void)snprintf(name, sizeof(name),
                   "%s/%s", p->fts_parent->fts_accpath, p->fts_name);
  if ((lnklen = readlink(name, path, sizeof(path) - 1)) == -1) {
    (void)fprintf(stderr, "\nls: %s: %s\n", name, strerror(errno));
    return;
  }
  path[lnklen] = '\0';
  (void)printf(" -> ");
  (void)printname(path);
}

static void humanize_number(char *buf, size_t len, int64_t number)
{
  double val = number;
  char unit = '\0';

  if(number <= 1024) {
    snprintf(buf, len, "%d   B", number);
    return;
  }
  else if(number <= 1000000L) {
    val /= 1024L;
    unit = 'K';
  }
  else if(number <= 1000000000L) {
    val /= 1048576L;
    unit = 'M';
  }
  else if(number <= 1000000000000L) {
    val /= 1073741824L;
    unit = 'G';
  }
  else if(number <= 1000000000000000L) {
    val /= 1099511627776LL;
    unit = 'T';
  }
  else if(number <= 1000000000000000000) {
    val /= 1125899906842624LL;
    unit = 'P';
  }
  else {
    val /= 1152921504606846976LL;
    unit = 'E';
  }

  snprintf(buf, len, "%3.1f %ciB", val, unit);
}

static void printsize(size_t width, off_t bytes)
{

  if (f_humanval) {
    /*
     * Reserve one space before the size and allocate room for
     * the trailing '\0'.
     */
    char buf[HUMANVALSTR_LEN - 1 + 1];

    humanize_number(buf, sizeof(buf), (int64_t)bytes);
    (void)printf("%*s ", (u_int)width, buf);
  } else
    (void)printf("%*jd ", (u_int)width, bytes);
}

static void usage(void)
{
  (void)fprintf(stderr,
#ifdef COLORLS
                "usage: ls [-ABCFGHILPRSTUWabcdfghiklmnpqrstuwx1] [-D format]"
#else
                "usage: ls [-ABCFHILPRSTUWZabcdfghiklmnpqrstuwx1] [-D format]"
#endif
                " [file ...]\n");
  exit(1);
}

int main(int argc, char *argv[])
{
  static char dot[] = ".", *dotav[] = {dot, NULL};
  struct winsize win;
  int ch, fts_options, notused;
  char *p;
#ifdef COLORLS
  char termcapbuf[1024];  /* termcap definition buffer */
  char tcapbuf[512];  /* capability buffer */
  char *bp = tcapbuf;
#endif

  /* options that does not have a short equivalent */
  enum opt {
    OPT_AUTHOR,
    OPT_BLOCKSIZE,
    OPT_COLOR,
    OPT_FILETYPE,
    OPT_FULLTIME,
    OPT_GRPDIRFIRST,
    OPT_SI,
    OPT_HIDE,
    OPT_INDICSTYLE,
    OPT_SHOWCTRLCHAR,
    OPT_QUOTINGSTYLE,
    OPT_SORT,
    OPT_TIME,
    OPT_TIMESTYLE,
    OPT_FORMAT
  };

  struct option opts[] = {
    { "all", no_argument, NULL, 'a' },
    { "almost-all", no_argument, NULL, 'A' },
    { "author", no_argument, NULL, OPT_AUTHOR },
    { "escape", no_argument, NULL, 'b' },
    { "block-size", required_argument, NULL, OPT_BLOCKSIZE },
    { "ignore-backups", no_argument, NULL, 'B' },
    { "color", required_argument, NULL, OPT_COLOR },
    { "directory", no_argument, NULL, 'd' },
    { "dired", no_argument, NULL, 'D' },
    { "classify", no_argument, NULL, 'F' },
    { "file-type", no_argument, NULL, OPT_FILETYPE },
    { "format", required_argument, NULL, OPT_FORMAT },
    { "full-time", no_argument, NULL, OPT_FULLTIME },
    { "group-directories-first", no_argument, NULL, OPT_GRPDIRFIRST },
    { "no-group", no_argument, NULL, 'G' },
    { "human-readable", no_argument, NULL, 'h' },
    { "si", no_argument, NULL, OPT_SI },
    { "dereference-command-line", no_argument, NULL, 'H' },
    { "hide", required_argument, NULL, OPT_HIDE },
    { "indicator-style", required_argument, NULL, OPT_INDICSTYLE },
    { "inode", no_argument, NULL, 'i' },
    { "ignore", required_argument, NULL, 'I' },
    { "dereference", no_argument, NULL, 'L' },
    { "numeric-uid-gid", no_argument, NULL, 'n' },
    { "literal", no_argument, NULL, 'N' },
    { "hide-control-chars", no_argument, NULL, 'q' },
    { "show-control-chars", no_argument, NULL, OPT_SHOWCTRLCHAR },
    { "quote-name", no_argument, NULL, 'Q' },
    { "quoting-style", no_argument, NULL, OPT_QUOTINGSTYLE },
    { "reverse", no_argument, NULL, 'r' },
    { "recursive", no_argument, NULL, 'R' },
    { "size", no_argument, NULL, 's' },
    { "sort", required_argument, NULL, OPT_SORT },
    { "time", required_argument, NULL, OPT_TIME },
    { "time-style", required_argument, NULL, OPT_TIMESTYLE },
    { "tabsize", required_argument, NULL, 'T' },
    { "width", required_argument, NULL, 'w' },
    { NULL, 0, NULL, 0 }
  };

  /* we really need locales here */
  (void)setlocale(LC_ALL, "");

  /* Terminal defaults to -Cq, non-terminal defaults to -1. */
  if (isatty(STDOUT_FILENO)) {
    termwidth = 80;
    if ((p = getenv("COLUMNS")) != NULL && *p != '\0')
      termwidth = atoi(p);
    else if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) != -1 &&
             win.ws_col > 0)
      termwidth = win.ws_col;
    f_nonprint = 1;
  } else {
    f_singlecol = 1;
    /* retrieve environment variable, in case of explicit -C */
    p = getenv("COLUMNS");
    if (p)
      termwidth = atoi(p);
  }

  fts_options = FTS_PHYSICAL;
  while ((ch = getopt_long(argc, argv,
                           "aAbBcCdDfFgGhHiI:klLmnNop:qQrRsStT:uUvw:xXZ",
                           opts, NULL)) != -1) {
    switch (ch) {
      /*
       * The -1, -C, -x and -l options all override each other so
       * shell aliasing works right.
       */
    case OPT_AUTHOR:
      f_author = 1;
      break;
    case '1':
      f_singlecol = 1;
      f_longform = 0;
      f_stream = 0;
      break;
    case 'B':
      f_nonprint = 0;
      f_octal = 1;
      f_octal_escape = 0;
      break;
    case 'C':
      f_sortacross = f_longform = f_singlecol = 0;
      break;
    case 'D':
      f_timeformat = optarg;
      break;
    case 'l':
      f_longform = 1;
      f_singlecol = 0;
      f_stream = 0;
      break;
    case 'x':
      f_sortacross = 1;
      f_longform = 0;
      f_singlecol = 0;
      break;
      /* The -c, -u, and -U options override each other. */
    case 'c':
      f_statustime = 1;
      f_accesstime = 0;
      break;
    case 'u':
      f_accesstime = 1;
      f_statustime = 0;
      break;
    case 'U':
      f_accesstime = 0;
      f_statustime = 0;
      break;
    case 'F':
      f_type = 1;
      f_slash = 0;
      break;
    case 'H':
      fts_options |= FTS_COMFOLLOW;
      f_nofollow = 0;
      break;
    case 'G':
      setenv("CLICOLOR", "", 1);
      break;
    case 'L':
      fts_options &= ~FTS_PHYSICAL;
      fts_options |= FTS_LOGICAL;
      f_nofollow = 0;
      break;
    case 'P':
      fts_options &= ~FTS_COMFOLLOW;
      fts_options &= ~FTS_LOGICAL;
      fts_options |= FTS_PHYSICAL;
      f_nofollow = 1;
      break;
    case 'R':
      f_recursive = 1;
      break;
    case 'a':
      fts_options |= FTS_SEEDOT;
      /* FALLTHROUGH */
    case 'A':
      f_listdot = 1;
      break;
    case 'I':
      f_noautodot = 1;
      break;
      /* The -d option turns off the -R option. */
    case 'd':
      f_listdir = 1;
      f_recursive = 0;
      break;
    case 'f':
      f_nosort = 1;
      break;
    case 'g': /* Compatibility with 4.3BSD. */
      break;
    case 'h':
      f_humanval = 1;
      break;
    case 'i':
      f_inode = 1;
      break;
    case 'k':
      f_humanval = 0;
      f_kblocks = 1;
      break;
    case 'm':
      f_stream = 1;
      f_singlecol = 0;
      f_longform = 0;
      break;
    case 'n':
      f_numericonly = 1;
      break;
    case 'p':
      f_slash = 1;
      f_type = 1;
      break;
    case 'q':
      f_nonprint = 1;
      f_octal = 0;
      f_octal_escape = 0;
      break;
    case 'r':
      f_reversesort = 1;
      break;
    case 's':
      f_size = 1;
      break;
    case 'T':
      f_sectime = 1;
      break;
      /* The -t and -S options override each other. */
    case 't':
      f_timesort = 1;
      f_sizesort = 0;
      break;
    case 'S':
      f_sizesort = 1;
      f_timesort = 0;
      break;
    case 'W':
      f_whiteout = 1;
      break;
    case 'b':
      f_nonprint = 0;
      f_octal = 0;
      f_octal_escape = 1;
      break;
    case 'w':
      f_nonprint = 0;
      f_octal = 0;
      f_octal_escape = 0;
      break;
    default:
      usage();
    }
  }
  argc -= optind;
  argv += optind;

  if(f_longform && !f_numericonly) {
    init_uid_ht();
    init_gid_ht();
  }

  /* Root is -A automatically unless -I. */
  if (!f_listdot && getuid() == (uid_t)0 && !f_noautodot)
    f_listdot = 1;

  /* Enabling of colours is conditional on the environment. */
  if (getenv("CLICOLOR") &&
      (isatty(STDOUT_FILENO) || getenv("CLICOLOR_FORCE")))
#ifdef COLORLS
    if (tgetent(termcapbuf, getenv("TERM")) == 1) {
      ansi_fgcol = tgetstr("AF", &bp);
      ansi_bgcol = tgetstr("AB", &bp);
      attrs_off = tgetstr("me", &bp);
      enter_bold = tgetstr("md", &bp);

      /* To switch colours off use 'op' if
       * available, otherwise use 'oc', or
       * don't do colours at all. */
      ansi_coloff = tgetstr("op", &bp);
      if (!ansi_coloff)
        ansi_coloff = tgetstr("oc", &bp);
      if (ansi_fgcol && ansi_bgcol && ansi_coloff)
        f_color = 1;
    }
#else
  warnx("color support not compiled in");
#endif /*COLORLS*/

#ifdef COLORLS
  if (f_color) {
    /*
     * We can't put tabs and color sequences together:
     * column number will be incremented incorrectly
     * for "stty oxtabs" mode.
     */
    f_notabs = 1;
    (void)signal(SIGINT, colorquit);
    (void)signal(SIGQUIT, colorquit);
    parsecolors(getenv("LSCOLORS"));
  }
#endif

  /*
   * If not -F, -i, -l, -s, -S or -t options, don't require stat
   * information, unless in color mode in which case we do
   * need this to determine which colors to display.
   */
  if (!f_inode && !f_longform && !f_size && !f_timesort &&
      !f_sizesort && !f_type
#ifdef COLORLS
      && !f_color
#endif
    )
    fts_options |= FTS_NOSTAT;

  /*
   * If not -F, -P, -d or -l options, follow any symbolic links listed on
   * the command line.
   */
  if (!f_nofollow && !f_longform && !f_listdir && (!f_type || f_slash))
    fts_options |= FTS_COMFOLLOW;

  /*
   * If -W, show whiteout entries
   */
#ifdef FTS_WHITEOUT
  if (f_whiteout)
    fts_options |= FTS_WHITEOUT;
#endif

  /* If -i, -l or -s, figure out block size. */
  if (f_inode || f_longform || f_size) {
    if (f_kblocks)
      blocksize = 2;
    else {
      /*(void)getbsize(&notused, &blocksize);*/
      blocksize = 1024; /* 1K-bsize */
      blocksize /= 512;
    }
  }
  /* Select a sort function. */
  if (f_reversesort) {
    if (!f_timesort && !f_sizesort)
      sortfcn = revnamecmp;
    else if (f_sizesort)
      sortfcn = revsizecmp;
    else if (f_accesstime)
      sortfcn = revacccmp;
    else if (f_statustime)
      sortfcn = revstatcmp;
    else    /* Use modification time. */
      sortfcn = revmodcmp;
  } else {
    if (!f_timesort && !f_sizesort)
      sortfcn = namecmp;
    else if (f_sizesort)
      sortfcn = sizecmp;
    else if (f_accesstime)
      sortfcn = acccmp;
    else if (f_statustime)
      sortfcn = statcmp;
    else    /* Use modification time. */
      sortfcn = modcmp;
  }

  /* Select a print function. */
  if (f_singlecol)
    printfcn = printscol;
  else if (f_longform)
    printfcn = printlong;
  else if (f_stream)
    printfcn = printstream;
  else
    printfcn = printcol;

  if (argc)
    traverse(argc, argv, fts_options);
  else
    traverse(1, dotav, fts_options);
  exit(rval);
}

static int output;    /* If anything output. */

/*
 * Traverse() walks the logical directory structure specified by the argv list
 * in the order specified by the mastercmp() comparison function.  During the
 * traversal it passes linked lists of structures to display() which represent
 * a superset (may be exact set) of the files to be displayed.
 */
static void traverse(int argc, char *argv[], int options)
{
  FTS *ftsp;
  FTSENT *p, *chp;
  int ch_options;

  if ((ftsp =
       fts_open(argv, options, f_nosort ? NULL : mastercmp)) == NULL)
    err(1, "fts_open");

  /*
   * We ignore errors from fts_children here since they will be
   * replicated and signalled on the next call to fts_read() below.
   */
  chp = fts_children(ftsp, 0);
  if (chp != NULL)
    display(NULL, chp, options);
  if (f_listdir)
    return;

  /*
   * If not recursing down this tree and don't need stat info, just get
   * the names.
   */
  ch_options = !f_recursive &&
    options & FTS_NOSTAT ? FTS_NAMEONLY : 0;

  while ((p = fts_read(ftsp)) != NULL)
    switch (p->fts_info) {
    case FTS_DC:
      warnx("%s: directory causes a cycle", p->fts_name);
      break;
    case FTS_DNR:
    case FTS_ERR:
      warnx("%s: %s", p->fts_path, strerror(p->fts_errno));
      rval = 1;
      break;
    case FTS_D:
      if (p->fts_level != FTS_ROOTLEVEL &&
          p->fts_name[0] == '.' && !f_listdot)
        break;

      /*
       * If already output something, put out a newline as
       * a separator.  If multiple arguments, precede each
       * directory with its name.
       */
      if (output) {
        putchar('\n');
        (void)printname(p->fts_path);
        puts(":");
      } else if (argc > 1) {
        (void)printname(p->fts_path);
        puts(":");
        output = 1;
      }
      chp = fts_children(ftsp, ch_options);
      display(p, chp, options);

      if (!f_recursive && chp != NULL)
        (void)fts_set(ftsp, p, FTS_SKIP);
      break;
    default:
      break;
    }
  if (errno)
    err(1, "fts_read");
}

/*
 * Display() takes a linked list of FTSENT structures and passes the list
 * along with any other necessary information to the print function.  P
 * points to the parent directory of the display list.
 */
static void display(const FTSENT *p, FTSENT *list, int options)
{
  struct stat *sp;
  DISPLAY d;
  FTSENT *cur;
  NAMES *np;
  off_t maxsize;
  long maxblock;
  u_long btotal, labelstrlen, maxinode, maxlen, maxnlink;
  u_long maxlabelstr;
  u_int sizelen;
  int maxflags;
  gid_t maxgroup;
  uid_t maxuser;
  size_t flen, ulen, glen;
  char *initmax;
  int entries, needstats;
  const char *user, *group;
  char *flags, *labelstr = NULL;
  char ngroup[STRBUF_SIZEOF(uid_t) + 1];
  char nuser[STRBUF_SIZEOF(gid_t) + 1];

  needstats = f_inode || f_longform || f_size;
  flen = 0;
  btotal = 0;
  initmax = getenv("LS_COLWIDTHS");
  /* Fields match -lios order.  New ones should be added at the end. */
  maxlabelstr = maxblock = maxinode = maxlen = maxnlink =
    maxuser = maxgroup = maxflags = maxsize = 0;
  if (initmax != NULL && *initmax != '\0') {
    char *initmax2, *jinitmax;
    int ninitmax;

    /* Fill-in "::" as "0:0:0" for the sake of scanf. */
    jinitmax = malloc(strlen(initmax) * 2 + 2);
    if (jinitmax == NULL)
      err(1, "malloc");
    initmax2 = jinitmax;
    if (*initmax == ':')
      strcpy(initmax2, "0:"), initmax2 += 2;
    else
      *initmax2++ = *initmax, *initmax2 = '\0';
    for (initmax++; *initmax != '\0'; initmax++) {
      if (initmax[-1] == ':' && initmax[0] == ':') {
        *initmax2++ = '0';
        *initmax2++ = initmax[0];
        initmax2[1] = '\0';
      } else {
        *initmax2++ = initmax[0];
        initmax2[1] = '\0';
      }
    }
    if (initmax2[-1] == ':')
      strcpy(initmax2, "0");

    ninitmax = sscanf(jinitmax,
                      " %lu : %ld : %lu : %u : %u : %i : %jd : %lu : %lu ",
                      &maxinode, &maxblock, &maxnlink, &maxuser,
                      &maxgroup, &maxflags, &maxsize, &maxlen, &maxlabelstr);
    f_notabs = 1;
    switch (ninitmax) {
    case 0:
      maxinode = 0;
      /* FALLTHROUGH */
    case 1:
      maxblock = 0;
      /* FALLTHROUGH */
    case 2:
      maxnlink = 0;
      /* FALLTHROUGH */
    case 3:
      maxuser = 0;
      /* FALLTHROUGH */
    case 4:
      maxgroup = 0;
      /* FALLTHROUGH */
    case 5:
      maxflags = 0;
      /* FALLTHROUGH */
    case 6:
      maxsize = 0;
      /* FALLTHROUGH */
    case 7:
      maxlen = 0;
      /* FALLTHROUGH */
    case 8:
      maxlabelstr = 0;
      /* FALLTHROUGH */
#ifdef COLORLS
      if (!f_color)
#endif
        f_notabs = 0;
      /* FALLTHROUGH */
    default:
      break;
    }
    MAKENINES(maxinode);
    MAKENINES(maxblock);
    MAKENINES(maxnlink);
    MAKENINES(maxsize);
    free(jinitmax);
  }
  d.s_size = 0;
  sizelen = 0;
  flags = NULL;
  for (cur = list, entries = 0; cur; cur = cur->fts_link) {
    if (cur->fts_info == FTS_ERR || cur->fts_info == FTS_NS) {
      warnx("%s: %s",
            cur->fts_name, strerror(cur->fts_errno));
      cur->fts_number = NO_PRINT;
      rval = 1;
      continue;
    }
    /*
     * P is NULL if list is the argv list, to which different rules
     * apply.
     */
    if (p == NULL) {
      /* Directories will be displayed later. */
      if (cur->fts_info == FTS_D && !f_listdir) {
        cur->fts_number = NO_PRINT;
        continue;
      }
    } else {
      /* Only display dot file if -a/-A set. */
      if (cur->fts_name[0] == '.' && !f_listdot) {
        cur->fts_number = NO_PRINT;
        continue;
      }
    }
    if (cur->fts_namelen > maxlen)
      maxlen = cur->fts_namelen;
    if (f_octal || f_octal_escape) {
      u_long t = len_octal(cur->fts_name, cur->fts_namelen);

      if (t > maxlen)
        maxlen = t;
    }
    if (needstats) {
      sp = cur->fts_statp;
      if (sp->st_blocks > maxblock)
        maxblock = sp->st_blocks;
      if (sp->st_ino > maxinode)
        maxinode = sp->st_ino;
      if (sp->st_nlink > maxnlink)
        maxnlink = sp->st_nlink;
      if (sp->st_size > maxsize)
        maxsize = sp->st_size;

      btotal += sp->st_blocks;
      if (f_longform) {
        if (f_numericonly) {
          (void)snprintf(nuser, sizeof(nuser),
                         "%u", sp->st_uid);
          (void)snprintf(ngroup, sizeof(ngroup),
                         "%u", sp->st_gid);
          user = nuser;
          group = ngroup;
        } else {
          user = user_from_uid(sp->st_uid, 0);
          group = group_from_gid(sp->st_gid, 0);
        }
        if ((ulen = strlen(user)) > maxuser)
          maxuser = ulen;
        if ((glen = strlen(group)) > maxgroup)
          maxgroup = glen;
        else
          flen = 0;
        labelstr = NULL;

        if((np = malloc(sizeof(NAMES) + ulen + glen + 4)) == NULL)
          err(1, "malloc");

        np->user = &np->data[0];
        (void)strcpy(np->user, user);
        np->group = &np->data[ulen + 1];
        (void)strcpy(np->group, group);

        cur->fts_pointer = np;
      }
    }
    ++entries;
  }

  /*
   * If there are no entries to display, we normally stop right
   * here.  However, we must continue if we have to display the
   * total block count.  In this case, we display the total only
   * on the second (p != NULL) pass.
   */
  if (!entries && (!(f_longform || f_size) || p == NULL))
    return;

  d.list = list;
  d.entries = entries;
  d.maxlen = maxlen;
  if (needstats) {
    d.btotal = btotal;
    d.s_block = snprintf(NULL, 0, "%lu", howmany(maxblock, blocksize));
    d.s_flags = maxflags;
    d.s_label = maxlabelstr;
    d.s_group = maxgroup;
    d.s_inode = snprintf(NULL, 0, "%lu", maxinode);
    d.s_nlink = snprintf(NULL, 0, "%lu", maxnlink);
    sizelen = f_humanval ? HUMANVALSTR_LEN :
      snprintf(NULL, 0, "%ju", maxsize);
    if (d.s_size < sizelen)
      d.s_size = sizelen;
    d.s_user = maxuser;
  }
  printfcn(&d);
  output = 1;

  if (f_longform)
    for (cur = list; cur; cur = cur->fts_link)
      free(cur->fts_pointer);
}

/*
 * Ordering for mastercmp:
 * If ordering the argv (fts_level = FTS_ROOTLEVEL) return non-directories
 * as larger than directories.  Within either group, use the sort function.
 * All other levels use the sort function.  Error entries remain unsorted.
 */
static int mastercmp(const FTSENT **a, const FTSENT **b)
{
  int a_info, b_info;

  a_info = (*a)->fts_info;
  if (a_info == FTS_ERR)
    return (0);
  b_info = (*b)->fts_info;
  if (b_info == FTS_ERR)
    return (0);

  if (a_info == FTS_NS || b_info == FTS_NS)
    return (namecmp(*a, *b));

  if (a_info != b_info &&
      (*a)->fts_level == FTS_ROOTLEVEL && !f_listdir) {
    if (a_info == FTS_D)
      return (1);
    if (b_info == FTS_D)
      return (-1);
  }
  return (sortfcn(*a, *b));
}

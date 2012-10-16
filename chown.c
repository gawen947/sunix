/*
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

#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <grp.h>
#include <libgen.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "record-invalid.h"
#include "common-cmdline.h"

static gid_t a_gid(const char *);
static uid_t a_uid(const char *);
static void chownerr(const char *);
static uid_t id(const char *, const char *);
static void usage(void);

static uid_t uid;
static gid_t gid;
static uid_t from_uid = -1;
static gid_t from_gid = -1;
static const char *gname;

static void verbose(uid_t uid, gid_t gid, FTSENT *p)
{
  printf("%s: %ju:%ju -> %ju:%ju\n",
         p->fts_path,
         (uintmax_t)
         p->fts_statp->st_uid,
         (uintmax_t)
         p->fts_statp->st_gid,
         (uid == (uid_t)-1) ?
         (uintmax_t)
         p->fts_statp->st_uid :
         (uintmax_t)uid,
           (gid == (gid_t)-1) ?
         (uintmax_t)
         p->fts_statp->st_gid :
         (uintmax_t)gid);
}

static void parse_from(char *from)
{
  char *cp;

  if(*from == '\0')
    return;

  if ((cp = strchr(from, ':')) != NULL) {
      *cp++ = '\0';
      from_gid = a_gid(cp);
  }
#ifdef SUPPORT_DOT
  else if ((cp = strchr(from, '.')) != NULL) {
    warnx("separation of user and group with a period is deprecated");
    *cp++ = '\0';
    from_gid = a_gid(cp);
  }
#endif
  from_uid = a_uid(from);
}

int main(int argc, char **argv)
{
  FTS *ftsp;
  FTSENT *p;
  int Hflag, Lflag, Rflag, fflag, hflag, vflag, xflag, cflag, rootflg;
  int ch, fts_options, rval;
  char *from = NULL;
  char *ref  = NULL;
  char *cp;

  enum opt { OPT_DEREFERENCE,
             OPT_REFERENCE,
             OPT_FROM,
             OPT_NO_ROOT,
             OPT_ROOT };

  struct option opts[] = {
    { "changes", no_argument, NULL, 'c' },
    { "dereference", no_argument, NULL, OPT_DEREFERENCE },
    { "no-dereference", no_argument, NULL, 'h' },
    { "from", required_argument, NULL, OPT_FROM },
    { "no-preserve-root", no_argument, NULL, OPT_NO_ROOT },
    { "preserve-root", no_argument, NULL, OPT_ROOT },
    { "silent", no_argument, NULL, 'f' },
    { "quiet", no_argument, NULL, 'f' },
    { "reference", required_argument, NULL, OPT_REFERENCE },
    { "recursive", no_argument, NULL, 'R' },
    { "verbose", no_argument, NULL, 'v' },
    { NULL, 0, NULL, 0 }
  };

  common_main(argc, argv, "chown", "/bin/chown.real", usage, opts);

  Hflag = Lflag = Rflag = fflag = hflag = vflag = xflag = cflag = rootflg =  0;
  while ((ch = getopt_long(argc, argv, "HLPRfhcvx", opts, NULL)) != -1)
    switch (ch) {
    case OPT_FROM:
      from = optarg;
      break;
    case OPT_REFERENCE:
      ref  = optarg;
      break;
    case OPT_ROOT:
      rootflg = 1;
      break;
    case OPT_NO_ROOT:
    case OPT_DEREFERENCE:
      break;
    case 'H':
      Hflag = 1;
      Lflag = 0;
      break;
    case 'L':
      Lflag = 1;
      Hflag = 0;
      break;
    case 'P':
      Hflag = Lflag = 0;
      break;
    case 'R':
      Rflag = 1;
      break;
    case 'f':
      fflag = 1;
      break;
    case 'h':
      hflag = 1;
      break;
    case 'c':
      vflag = 2;
      cflag = 1;
      break;
    case 'v':
      vflag = 2;
      break;
    case 'x':
      xflag = 1;
      break;
    default:
    case '?':
      record_invalid(argv[0], argv[optind - 1]);
      usage();
    }
  argv += optind;
  argc -= optind;

  if ((argc < 2 && !ref) || (argc < 1 && ref))
    usage();

  if (Rflag) {
    fts_options = FTS_PHYSICAL;
    if (hflag && (Hflag || Lflag))
      errx(1, "the -R%c and -h options may not be "
           "specified together", Hflag ? 'H' : 'L');
    if (Hflag)
      fts_options |= FTS_COMFOLLOW;
    else if (Lflag) {
      fts_options &= ~FTS_PHYSICAL;
      fts_options |= FTS_LOGICAL;
    }
  } else
    fts_options = hflag ? FTS_PHYSICAL : FTS_LOGICAL;
  if (xflag)
    fts_options |= FTS_XDEV;

  if(!ref) {
    if ((cp = strchr(*argv, ':')) != NULL) {
      *cp++ = '\0';
      gid = a_gid(cp);
    }
#ifdef SUPPORT_DOT
    else if ((cp = strchr(*argv, '.')) != NULL) {
      warnx("separation of user and group with a period is deprecated");
      *cp++ = '\0';
      gid = a_gid(cp);
    }
#endif
    uid = a_uid(*argv);

    ++argv;
  }
  else {
    struct stat st;
    if(stat(ref, &st) < 0)
      err(1, "stat \"%s\"", ref);
    uid = st.st_uid;
    gid = st.st_gid;
  }

  if(from)
    parse_from(from);

  if ((ftsp = fts_open(argv, fts_options, 0)) == NULL)
    err(1, NULL);

  if(rootflg && Rflag && !strcmp(*argv, "/"))
    errx(1, "will not recurse indide root");

  for (rval = 0; (p = fts_read(ftsp)) != NULL;) {
    switch (p->fts_info) {
    case FTS_D:     /* Change it at FTS_DP. */
      if (!Rflag)
        fts_set(ftsp, p, FTS_SKIP);
      continue;
    case FTS_DNR:     /* Warn, chown. */
      warnx("%s: %s", p->fts_path, strerror(p->fts_errno));
      rval = 1;
      break;
    case FTS_ERR:     /* Warn, continue. */
    case FTS_NS:
      warnx("%s: %s", p->fts_path, strerror(p->fts_errno));
      rval = 1;
      continue;
    case FTS_SL:
    case FTS_SLNONE:
      /*
       * The only symlinks that end up here are ones that
       * don't point to anything and ones that we found
       * doing a physical walk.
       */
      if (hflag)
        break;
      else
        continue;
    default:
      break;
    }

    if((from_uid != -1 || from_gid != -1) &&
       (p->fts_statp->st_uid != from_uid && p->fts_statp->st_gid != from_gid))
      continue;

    if ((uid == (uid_t)-1 || uid == p->fts_statp->st_uid) &&
        (gid == (gid_t)-1 || gid == p->fts_statp->st_gid)) {
      if(vflag && !cflag)
        verbose(uid, gid, p);
      continue;
    }
    if ((hflag ? lchown : chown)(p->fts_accpath, uid, gid) == -1) {
      if (!fflag) {
        chownerr(p->fts_path);
        rval = 1;
      }
    } else {
      if (vflag)
        verbose(uid, gid, p);
    }
  }
  if (errno)
    err(1, "fts_read");
  exit(rval);
}

static gid_t a_gid(const char *s)
{
  struct group *gr;

  if (*s == '\0')     /* Argument was "uid[:.]". */
    return -1;
  gname = s;
  return ((gr = getgrnam(s)) != NULL) ? gr->gr_gid : id(s, "group");
}

static uid_t a_uid(const char *s)
{
  struct passwd *pw;

  if (*s == '\0')     /* Argument was "[:.]gid". */
    return -1;
  return ((pw = getpwnam(s)) != NULL) ? pw->pw_uid : id(s, "user");
}

static uid_t id(const char *name, const char *type)
{
  uid_t val;
  char *ep;

  /*
   * XXX
   * We know that uid_t's and gid_t's are unsigned longs.
   */
  errno = 0;
  val = strtoul(name, &ep, 10);
  if (errno || *ep != '\0')
    errx(1, "%s: illegal %s name", name, type);
  return (val);
}

static void chownerr(const char *file)
{
  static uid_t euid = -1;
  static int ngroups = -1;
  static long ngroups_max;
  gid_t *groups;

  /* Check for chown without being root. */
  if (errno != EPERM || (uid != (uid_t)-1 &&
                         euid == (uid_t)-1 && (euid = geteuid()) != 0)) {
    warn("%s", file);
    return;
  }

  /* Check group membership; kernel just returns EPERM. */
  if (gid != (gid_t)-1 && ngroups == -1 &&
      euid == (uid_t)-1 && (euid = geteuid()) != 0) {
    ngroups_max = sysconf(_SC_NGROUPS_MAX) + 1;
    if ((groups = malloc(sizeof(gid_t) * ngroups_max)) == NULL)
      err(1, "malloc");
    ngroups = getgroups(ngroups_max, groups);
    while (--ngroups >= 0 && gid != groups[ngroups]);
    free(groups);
    if (ngroups < 0) {
      warnx("you are not a member of group %s", gname);
      return;
    }
  }
  warn("%s", file);
}

static void usage(void)
{

  (void)fprintf(stderr, "%s\n%s\n",
                "usage: chown [-fhvx] [-R [-H | -L | -P]] owner[:group]"
                " file ...",
                "       chown [-fhvx] [-R [-H | -L | -P]] :group file ...");
  exit(1);
}

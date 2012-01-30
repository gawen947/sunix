/*-
 * Copyright (c) 1989, 1993, 1994
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ken Smith of The State University of New York at Buffalo.
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
#include <sys/param.h>
#include <sys/vfs.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <err.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <stdbool.h>
#include <unistd.h>

#include "bsd.h"

#define _PATH_CP "/bin/cp"
#define _PATH_RM "/bin/rm"

/* Exit code for a failed exec. */
#define EXEC_FAILED 127

static int  fflg, iflg, nflg, vflg;

static int copy(const char *, const char *);
static int do_move(const char *, const char *);
static int fastcopy(const char *, const char *, struct stat *);
static void usage(void);

static bool is_mountpoint(const char *path)
{
  struct stat st, st2;
  char buf[256];

  memset(buf, 0, sizeof(buf));
  strncpy(buf, path, sizeof(buf) - 4);
  strcat(buf, "/..");

  stat(path, &st);
  stat(buf, &st2);

  return (st.st_dev != st2.st_dev) ||                     \
         (st.st_dev == st2.st_dev && st.st_ino == st2.st_ino);
}

int main(int argc, char *argv[])
{
  size_t baselen, len;
  int rval;
  char *p, *endp;
  struct stat sb;
  int ch;
  char path[PATH_MAX];

  while ((ch = getopt_long(argc, argv, "finv", NULL, NULL)) != -1)
    switch (ch) {
    case 'i':
      iflg = 1;
      fflg = nflg = 0;
      break;
    case 'f':
      fflg = 1;
      iflg = nflg = 0;
      break;
    case 'n':
      nflg = 1;
      fflg = iflg = 0;
      break;
    case 'v':
      vflg = 1;
      break;
    default:
      usage();
    }

  if(!iflg && !nflg) {
    init_uid_ht();
    init_gid_ht();
  }

  argc -= optind;
  argv += optind;

  if (argc < 2)
    usage();

  /*
   * If the stat on the target fails or the target isn't a directory,
   * try the move.  More than 2 arguments is an error in this case.
   */
  if (stat(argv[argc - 1], &sb) || !S_ISDIR(sb.st_mode)) {
    if (argc > 2)
      usage();
    exit(do_move(argv[0], argv[1]));
  }

  /* It's a directory, move each file into it. */
  if (strlen(argv[argc - 1]) > sizeof(path) - 1)
    errx(1, "%s: destination pathname too long", *argv);
  (void)strcpy(path, argv[argc - 1]);
  baselen = strlen(path);
  endp = &path[baselen];
  if (!baselen || *(endp - 1) != '/') {
    *endp++ = '/';
    ++baselen;
  }
  for (rval = 0; --argc; ++argv) {
    /*
     * Find the last component of the source pathname.  It
     * may have trailing slashes.
     */
    p = *argv + strlen(*argv);
    while (p != *argv && p[-1] == '/')
      --p;
    while (p != *argv && p[-1] != '/')
      --p;

    if ((baselen + (len = strlen(p))) >= PATH_MAX) {
      warnx("%s: destination pathname too long", *argv);
      rval = 1;
    } else {
      memmove(endp, p, (size_t)len + 1);
      if (do_move(*argv, path))
        rval = 1;
    }
  }
  exit(rval);
}

static int do_move(const char *from, const char *to)
{
  struct stat sb;
  int ask, ch, first;
  char modep[15];

  /*
   * Check access.  If interactive and file exists, ask user if it
   * should be replaced.  Otherwise if file exists but isn't writable
   * make sure the user wants to clobber it.
   */
  if (!fflg && !access(to, F_OK)) {

    /* prompt only if source exist */
    if (lstat(from, &sb) == -1) {
      warn("%s", from);
      return (1);
    }

#define YESNO "(y/n [n]) "
    ask = 0;
    if (nflg) {
      if (vflg)
        printf("%s not overwritten\n", to);
      return (0);
    } else if (iflg) {
      (void)fprintf(stderr, "overwrite %s? %s", to, YESNO);
      ask = 1;
    } else if (access(to, W_OK) && !stat(to, &sb)) {
      strmode(sb.st_mode, modep);
      (void)fprintf(stderr, "override %s%s%s/%s for %s? %s",
                    modep + 1, modep[9] == ' ' ? "" : " ",
                    user_from_uid((unsigned long)sb.st_uid, 0),
                    group_from_gid((unsigned long)sb.st_gid, 0), to, YESNO);
      ask = 1;
    }
    if (ask) {
      first = ch = getchar();
      while (ch != '\n' && ch != EOF)
        ch = getchar();
      if (first != 'y' && first != 'Y') {
        (void)fprintf(stderr, "not overwritten\n");
        return (0);
      }
    }
  }
  /*
   * Rename on FreeBSD will fail with EISDIR and ENOTDIR, before failing
   * with EXDEV.  Therefore, copy() doesn't have to perform the checks
   * specified in the Step 3 of the POSIX mv specification.
   */
  if (!rename(from, to)) {
    if (vflg)
      printf("%s -> %s\n", from, to);
    return (0);
  }

  if (errno == EXDEV) {
    char path[PATH_MAX];

    /*
     * If the source is a symbolic link and is on another
     * filesystem, it can be recreated at the destination.
     */
    if (lstat(from, &sb) == -1) {
      warn("%s", from);
      return (1);
    }
    if (!S_ISLNK(sb.st_mode)) {
      /* Can't mv(1) a mount point. */
      if (realpath(from, path) == NULL) {
        warn("cannot resolve %s: %s", from, path);
        return (1);
      }
      if (is_mountpoint(from)) {
        warnx("cannot rename a mount point");
        return (1);
      }
    }
  } else {
    warn("rename %s to %s", from, to);
    return (1);
  }

  /*
   * If rename fails because we're trying to cross devices, and
   * it's a regular file, do the copy internally; otherwise, use
   * cp and rm.
   */
  if (lstat(from, &sb)) {
    warn("%s", from);
    return (1);
  }
  return (S_ISREG(sb.st_mode) ?
          fastcopy(from, to, &sb) : copy(from, to));
}

static int fastcopy(const char *from, const char *to, struct stat *sbp)
{
  struct timeval tval[2];
  static u_int blen = MAXPHYS;
  static char *bp = NULL;
  mode_t oldmode;
  int nread, from_fd, to_fd;

  if ((from_fd = open(from, O_RDONLY, 0)) < 0) {
    warn("fastcopy: open() failed (from): %s", from);
    return (1);
  }
  if (bp == NULL && (bp = malloc((size_t)blen)) == NULL) {
    warnx("malloc(%u) failed", blen);
    return (1);
  }
  while ((to_fd =
          open(to, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0)) < 0) {
    if (errno == EEXIST && unlink(to) == 0)
      continue;
    warn("fastcopy: open() failed (to): %s", to);
    (void)close(from_fd);
    return (1);
  }
  while ((nread = read(from_fd, bp, (size_t)blen)) > 0)
    if (write(to_fd, bp, (size_t)nread) != nread) {
      warn("fastcopy: write() failed: %s", to);
      goto err;
    }
  if (nread < 0) {
    warn("fastcopy: read() failed: %s", from);
  err:    if (unlink(to))
      warn("%s: remove", to);
    (void)close(from_fd);
    (void)close(to_fd);
    return (1);
  }

  oldmode = sbp->st_mode & ALLPERMS;
  if (fchown(to_fd, sbp->st_uid, sbp->st_gid)) {
    warn("%s: set owner/group (was: %lu/%lu)", to,
         (u_long)sbp->st_uid, (u_long)sbp->st_gid);
    if (oldmode & (S_ISUID | S_ISGID)) {
      warnx(
        "%s: owner/group changed; clearing suid/sgid (mode was 0%03o)",
        to, oldmode);
      sbp->st_mode &= ~(S_ISUID | S_ISGID);
    }
  }
  if (fchmod(to_fd, sbp->st_mode))
    warn("%s: set mode (was: 0%03o)", to, oldmode);

  (void)close(from_fd);
  /*
   * XXX
   * NFS doesn't support chflags; ignore errors unless there's reason
   * to believe we're losing bits.  (Note, this still won't be right
   * if the server supports flags and we were trying to *remove* flags
   * on a file that we copied, i.e., that we didn't create.)
   */
  errno = 0;

  tval[0].tv_sec = sbp->st_atime;
  tval[1].tv_sec = sbp->st_mtime;
  tval[0].tv_usec = tval[1].tv_usec = 0;
  if (utimes(to, tval))
    warn("%s: set times", to);

  if (close(to_fd)) {
    warn("%s", to);
    return (1);
  }

  if (unlink(from)) {
    warn("%s: remove", from);
    return (1);
  }
  if (vflg)
    printf("%s -> %s\n", from, to);
  return (0);
}

static int copy(const char *from, const char *to)
{
  struct stat sb;
  int pid, status;

  if (lstat(to, &sb) == 0) {
    /* Destination path exists. */
    if (S_ISDIR(sb.st_mode)) {
      if (rmdir(to) != 0) {
        warn("rmdir %s", to);
        return (1);
      }
    } else {
      if (unlink(to) != 0) {
        warn("unlink %s", to);
        return (1);
      }
    }
  } else if (errno != ENOENT) {
    warn("%s", to);
    return (1);
  }

  /* Copy source to destination. */
  if (!(pid = vfork())) {
    execl(_PATH_CP, "mv", vflg ? "-PRpv" : "-PRp", "--", from, to,
          (char *)NULL);
    _exit(EXEC_FAILED);
  }
  if (waitpid(pid, &status, 0) == -1) {
    warn("%s %s %s: waitpid", _PATH_CP, from, to);
    return (1);
  }
  if (!WIFEXITED(status)) {
    warnx("%s %s %s: did not terminate normally",
          _PATH_CP, from, to);
    return (1);
  }
  switch (WEXITSTATUS(status)) {
  case 0:
    break;
  case EXEC_FAILED:
    warnx("%s %s %s: exec failed", _PATH_CP, from, to);
    return (1);
  default:
    warnx("%s %s %s: terminated with %d (non-zero) status",
          _PATH_CP, from, to, WEXITSTATUS(status));
    return (1);
  }

  /* Delete the source. */
  if (!(pid = vfork())) {
    execl(_PATH_RM, "mv", "-rf", "--", from, (char *)NULL);
    _exit(EXEC_FAILED);
  }
  if (waitpid(pid, &status, 0) == -1) {
    warn("%s %s: waitpid", _PATH_RM, from);
    return (1);
  }
  if (!WIFEXITED(status)) {
    warnx("%s %s: did not terminate normally", _PATH_RM, from);
    return (1);
  }
  switch (WEXITSTATUS(status)) {
  case 0:
    break;
  case EXEC_FAILED:
    warnx("%s %s: exec failed", _PATH_RM, from);
    return (1);
  default:
    warnx("%s %s: terminated with %d (non-zero) status",
          _PATH_RM, from, WEXITSTATUS(status));
    return (1);
  }
  return (0);
}

static void usage(void)
{

  (void)fprintf(stderr, "%s\n%s\n",
                "usage: mv [-f | -i | -n] [-v] source target",
                "       mv [-f | -i | -n] [-v] source ... directory");
  exit(EX_USAGE);
}

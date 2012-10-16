/*-
 * Copyright (c) 1988, 1993, 1994
 *   The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * David Hitz of Auspex Systems Inc.
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

/*
 * Cp copies source files to target files.
 *
 * The global PATH_T structure "to" always contains the path to the
 * current target file.  Since fts(3) does not change directories,
 * this path can be either absolute or dot-relative.
 *
 * The basic algorithm is to initialize "to" and use fts(3) to traverse
 * the file hierarchy rooted in the argument list.  A trivial case is the
 * case of 'cp file1 file2'.  The more interesting case is the case of
 * 'cp file1 file2 ... fileN dir' where the hierarchy is traversed and the
 * path (relative to the root of the traversal) is appended to dir (stored
 * in "to") to form the final target path.
 */

#define _GNU_SOURCE

#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <fts.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "bsd.h"
#include "record-invalid.h"
#include "common-cmdline.h"

typedef struct {
  char *p_end;            /* pointer to NULL */
  char *target_end;       /* pointer to end of target base */
  char p_path[PATH_MAX];  /* pointer to the start of a path */
} PATH_T;

#define STRIP_TRAILING_SLASH(p) {                               \
    while ((p).p_end > (p).p_path + 1 && (p).p_end[-1] == '/')  \
      *--(p).p_end = 0;                                         \
  }

static char emptystring[] = "";

PATH_T to = { to.p_path, emptystring, "" };

static int fflag, iflag, lflag, nflag, pflag, vflag;
static int Rflag;
volatile sig_atomic_t info;

enum op { FILE_TO_FILE, FILE_TO_DIR, DIR_TO_DNE };

static int copy(char *[], enum op, int);
static int mastercmp(const FTSENT **, const FTSENT **);
static int copy_fifo(struct stat *, int);
static int copy_file(const FTSENT *, int);
static int copy_link(const FTSENT *, int);
static int copy_special(struct stat *, int);
static int setfile(struct stat *, int);
static void usage(void);

#define cp_pct(x, y)  ((y == 0) ? 0 : (int)(100.0 * (x) / (y)))

/* Memory strategy threshold, in pages: if physmem is larger then this, use a
 * large buffer */
#define PHYSPAGES_THRESHOLD (32*1024)

/* Maximum buffer size in bytes - do not allow it to grow larger than this */
#define BUFSIZE_MAX (2*1024*1024)

/* Small (default) buffer size in bytes. It's inefficient for this to be
 * smaller than MAXPHYS */
#define BUFSIZE_SMALL (MAXPHYS)

static int copy_file(const FTSENT *entp, int dne)
{
  static char *buf = NULL;
  static size_t bufsize;
  struct stat *fs;
  ssize_t wcount;
  size_t wresid;
  off_t wtotal;
  int ch, checkch, from_fd = 0, rcount, rval, to_fd = 0;
  char *bufp;
#ifdef VM_AND_BUFFER_CACHE_SYNCHRONIZED
  char *p;
#endif

  if ((from_fd = open(entp->fts_path, O_RDONLY, 0)) == -1) {
    warn("%s", entp->fts_path);
    return (1);
  }

  fs = entp->fts_statp;

  /*
   * If the file exists and we're interactive, verify with the user.
   * If the file DNE, set the mode to be the from file, minus setuid
   * bits, modified by the umask; arguably wrong, but it makes copying
   * executables work right and it's been that way forever.  (The
   * other choice is 666 or'ed with the execute bits on the from file
   * modified by the umask.)
   */
  if (!dne) {
#define YESNO "(y/n [n]) "
    if (nflag) {
      if (vflag)
        printf("%s not overwritten\n", to.p_path);
      (void)close(from_fd);
      return (0);
    } else if (iflag) {
      (void)fprintf(stderr, "overwrite %s? %s",
                    to.p_path, YESNO);
      checkch = ch = getchar();
      while (ch != '\n' && ch != EOF)
        ch = getchar();
      if (checkch != 'y' && checkch != 'Y') {
        (void)close(from_fd);
        (void)fprintf(stderr, "not overwritten\n");
        return (1);
      }
    }

    if (fflag) {
      /* remove existing destination file name,
       * create a new file  */
      (void)unlink(to.p_path);
      if (!lflag)
        to_fd = open(to.p_path, O_WRONLY | O_TRUNC | O_CREAT,
                     fs->st_mode & ~(S_ISUID | S_ISGID));
    } else {
      if (!lflag)
        /* overwrite existing destination file name */
        to_fd = open(to.p_path, O_WRONLY | O_TRUNC, 0);
    }
  } else {
    if (!lflag)
      to_fd = open(to.p_path, O_WRONLY | O_TRUNC | O_CREAT,
                   fs->st_mode & ~(S_ISUID | S_ISGID));
  }

  if (to_fd == -1) {
    warn("%s", to.p_path);
    (void)close(from_fd);
    return (1);
  }

  rval = 0;

  if (!lflag) {
    /*
     * Mmap and write if less than 8M (the limit is so we don't totally
     * trash memory on big files.  This is really a minor hack, but it
     * wins some CPU back.
     * Some filesystems, such as smbnetfs, don't support mmap,
     * so this is a best-effort attempt.
     */
#ifdef VM_AND_BUFFER_CACHE_SYNCHRONIZED
    if (S_ISREG(fs->st_mode) && fs->st_size > 0 &&
        fs->st_size <= 8 * 1024 * 1024 &&
        (p = mmap(NULL, (size_t)fs->st_size, PROT_READ,
                  MAP_SHARED, from_fd, (off_t)0)) != MAP_FAILED) {
      wtotal = 0;
      for (bufp = p, wresid = fs->st_size; ;
           bufp += wcount, wresid -= (size_t)wcount) {
        wcount = write(to_fd, bufp, wresid);
        if (wcount <= 0)
          break;
        wtotal += wcount;
        if (info) {
          info = 0;
          (void)fprintf(stderr,
                        "%s -> %s %3d%%\n",
                        entp->fts_path, to.p_path,
                        cp_pct(wtotal, fs->st_size));
        }
        if (wcount >= (ssize_t)wresid)
          break;
      }
      if (wcount != (ssize_t)wresid) {
        warn("%s", to.p_path);
        rval = 1;
      }
      /* Some systems don't unmap on close(2). */
      if (munmap(p, fs->st_size) < 0) {
        warn("%s", entp->fts_path);
        rval = 1;
      }
    } else
#endif
    {
      if (buf == NULL) {
        /*
         * Note that buf and bufsize are static. If
         * malloc() fails, it will fail at the start
         * and not copy only some files.
         */
        if (sysconf(_SC_PHYS_PAGES) >
            PHYSPAGES_THRESHOLD)
          bufsize = MIN(BUFSIZE_MAX, MAXPHYS * 8);
        else
          bufsize = BUFSIZE_SMALL;
        buf = malloc(bufsize);
        if (buf == NULL)
          err(1, "Not enough memory");
      }
      wtotal = 0;
      while ((rcount = read(from_fd, buf, bufsize)) > 0) {
        for (bufp = buf, wresid = rcount; ;
             bufp += wcount, wresid -= wcount) {
          wcount = write(to_fd, bufp, wresid);
          if (wcount <= 0)
            break;
          wtotal += wcount;
          if (info) {
            info = 0;
            (void)fprintf(stderr,
                          "%s -> %s %3d%%\n",
                          entp->fts_path, to.p_path,
                          cp_pct(wtotal, fs->st_size));
          }
          if (wcount >= (ssize_t)wresid)
            break;
        }
        if (wcount != (ssize_t)wresid) {
          warn("%s", to.p_path);
          rval = 1;
          break;
        }
      }
      if (rcount < 0) {
        warn("%s", entp->fts_path);
        rval = 1;
      }
    }
  } else {
    if (link(entp->fts_path, to.p_path)) {
      warn("%s", to.p_path);
      rval = 1;
    }
  }

  /*
   * Don't remove the target even after an error.  The target might
   * not be a regular file, or its attributes might be important,
   * or its contents might be irreplaceable.  It would only be safe
   * to remove it if we created it and its length is 0.
   */

  if (!lflag) {
    if (pflag && setfile(fs, to_fd))
      rval = 1;
    if (close(to_fd)) {
      warn("%s", to.p_path);
      rval = 1;
    }
  }

  (void)close(from_fd);

  return (rval);
}

static int copy_link(const FTSENT *p, int exists)
{
  int len;
  char llink[PATH_MAX];

  if ((len = readlink(p->fts_path, llink, sizeof(llink) - 1)) == -1) {
    warn("readlink: %s", p->fts_path);
    return (1);
  }
  llink[len] = '\0';
  if (exists && unlink(to.p_path)) {
    warn("unlink: %s", to.p_path);
    return (1);
  }
  if (symlink(llink, to.p_path)) {
    warn("symlink: %s", llink);
    return (1);
  }
  return (pflag ? setfile(p->fts_statp, -1) : 0);
}

static int copy_fifo(struct stat *from_stat, int exists)
{
  if (exists && unlink(to.p_path)) {
    warn("unlink: %s", to.p_path);
    return (1);
  }
  if (mkfifo(to.p_path, from_stat->st_mode)) {
    warn("mkfifo: %s", to.p_path);
    return (1);
  }
  return (pflag ? setfile(from_stat, -1) : 0);
}

static int copy_special(struct stat *from_stat, int exists)
{
  if (exists && unlink(to.p_path)) {
    warn("unlink: %s", to.p_path);
    return (1);
  }
  if (mknod(to.p_path, from_stat->st_mode, from_stat->st_rdev)) {
    warn("mknod: %s", to.p_path);
    return (1);
  }
  return (pflag ? setfile(from_stat, -1) : 0);
}

static int setfile(struct stat *fs, int fd)
{
  static struct timeval tv[2];
  struct stat ts;
  int rval, gotstat, islink, fdval;

  rval = 0;
  fdval = fd != -1;
  islink = !fdval && S_ISLNK(fs->st_mode);
  fs->st_mode &= S_ISUID | S_ISGID | S_ISVTX |
    S_IRWXU | S_IRWXG | S_IRWXO;

  TIMESPEC_TO_TIMEVAL(&tv[0], &fs->st_atim);
  TIMESPEC_TO_TIMEVAL(&tv[1], &fs->st_mtim);
  if (islink ? lutimes(to.p_path, tv) : utimes(to.p_path, tv)) {
    warn("%sutimes: %s", islink ? "l" : "", to.p_path);
    rval = 1;
  }
  if (fdval ? fstat(fd, &ts) :
      (islink ? lstat(to.p_path, &ts) : stat(to.p_path, &ts)))
    gotstat = 0;
  else {
    gotstat = 1;
    ts.st_mode &= S_ISUID | S_ISGID | S_ISVTX |
      S_IRWXU | S_IRWXG | S_IRWXO;
  }
  /*
   * Changing the ownership probably won't succeed, unless we're root
   * or POSIX_CHOWN_RESTRICTED is not set.  Set uid/gid before setting
   * the mode; current BSD behavior is to remove all setuid bits on
   * chown.  If chown fails, lose setuid/setgid bits.
   */
  if (!gotstat || fs->st_uid != ts.st_uid || fs->st_gid != ts.st_gid)
    if (fdval ? fchown(fd, fs->st_uid, fs->st_gid) :
        (islink ? lchown(to.p_path, fs->st_uid, fs->st_gid) :
         chown(to.p_path, fs->st_uid, fs->st_gid))) {
      if (errno != EPERM) {
        warn("chown: %s", to.p_path);
        rval = 1;
      }
      fs->st_mode &= ~(S_ISUID | S_ISGID);
    }

  if (!gotstat || fs->st_mode != ts.st_mode)
    if (fdval ? fchmod(fd, fs->st_mode) :
        (islink ? lchmod(to.p_path, fs->st_mode) :
         chmod(to.p_path, fs->st_mode))) {
      warn("chmod: %s", to.p_path);
      rval = 1;
    }

  if (!gotstat)
    rval = 1;

  return (rval);
}

static void usage(void)
{
  (void)fprintf(stderr, "%s\n%s\n",
                "usage: cp [-R [-H | -L | -P]] [-f | -i | -n] [-alpvx] source_file target_file",
                "       cp [-R [-H | -L | -P]] [-f | -i | -n] [-alpvx] source_file ... "
                "target_directory");
  exit(EX_USAGE);
}

int main(int argc, char *argv[])
{
  common_main(argc, argv, "cp", "/bin/cp.real", usage, NULL);

  struct stat to_stat, tmp_stat;
  enum op type;
  int Hflag, Lflag, Pflag, ch, fts_options, r, have_trailing_slash;
  char *target;

  fts_options = FTS_NOCHDIR | FTS_PHYSICAL;
  Hflag = Lflag = Pflag = 0;
  while ((ch = getopt_long(argc, argv, "HLPRafilnprvx", NULL, NULL)) != -1)
    switch (ch) {
    case 'H':
      Hflag = 1;
      Lflag = Pflag = 0;
      break;
    case 'L':
      Lflag = 1;
      Hflag = Pflag = 0;
      break;
    case 'P':
      Pflag = 1;
      Hflag = Lflag = 0;
      break;
    case 'R':
    case 'r':
      Rflag = 1;
      break;
    case 'a':
      Pflag = 1;
      pflag = 1;
      Rflag = 1;
      Hflag = Lflag = 0;
      break;
    case 'f':
      fflag = 1;
      iflag = nflag = 0;
      break;
    case 'i':
      iflag = 1;
      fflag = nflag = 0;
      break;
    case 'l':
      lflag = 1;
      break;
    case 'n':
      nflag = 1;
      fflag = iflag = 0;
      break;
    case 'p':
      pflag = 1;
      break;
    case 'v':
      vflag = 1;
      break;
    case 'x':
      fts_options |= FTS_XDEV;
      break;
    default:
    case '?':
      record_invalid(argv[0], argv[optind]);
      usage();
    }
  argc -= optind;
  argv += optind;

  if (argc < 2)
    usage();

  if (Rflag) {
    if (Hflag)
      fts_options |= FTS_COMFOLLOW;
    if (Lflag) {
      fts_options &= ~FTS_PHYSICAL;
      fts_options |= FTS_LOGICAL;
    }
  } else {
    fts_options &= ~FTS_PHYSICAL;
    fts_options |= FTS_LOGICAL | FTS_COMFOLLOW;
  }

  /* Save the target base in "to". */
  target = argv[--argc];
  if (strlcpy(to.p_path, target, sizeof(to.p_path)) >= sizeof(to.p_path))
    errx(1, "%s: name too long", target);
  to.p_end = to.p_path + strlen(to.p_path);
  if (to.p_path == to.p_end) {
    *to.p_end++ = '.';
    *to.p_end = 0;
  }
  have_trailing_slash = (to.p_end[-1] == '/');
  if (have_trailing_slash)
    STRIP_TRAILING_SLASH(to);
  to.target_end = to.p_end;

  /* Set end of argument list for fts(3). */
  argv[argc] = NULL;

  /*
   * Cp has two distinct cases:
   *
   * cp [-R] source target
   * cp [-R] source1 ... sourceN directory
   *
   * In both cases, source can be either a file or a directory.
   *
   * In (1), the target becomes a copy of the source. That is, if the
   * source is a file, the target will be a file, and likewise for
   * directories.
   *
   * In (2), the real target is not directory, but "directory/source".
   */
  r = stat(to.p_path, &to_stat);
  if (r == -1 && errno != ENOENT)
    err(1, "%s", to.p_path);
  if (r == -1 || !S_ISDIR(to_stat.st_mode)) {
    /*
     * Case (1).  Target is not a directory.
     */
    if (argc > 1)
      errx(1, "%s is not a directory", to.p_path);

    /*
     * Need to detect the case:
     *  cp -R dir foo
     * Where dir is a directory and foo does not exist, where
     * we want pathname concatenations turned on but not for
     * the initial mkdir().
     */
    if (r == -1) {
      if (Rflag && (Lflag || Hflag))
        stat(*argv, &tmp_stat);
      else
        lstat(*argv, &tmp_stat);

      if (S_ISDIR(tmp_stat.st_mode) && Rflag)
        type = DIR_TO_DNE;
      else
        type = FILE_TO_FILE;
    } else
      type = FILE_TO_FILE;

    if (have_trailing_slash && type == FILE_TO_FILE) {
      if (r == -1)
        errx(1, "directory %s does not exist",
             to.p_path);
      else
        errx(1, "%s is not a directory", to.p_path);
    }
  } else
    /*
     * Case (2).  Target is a directory.
     */
    type = FILE_TO_DIR;

  exit (copy(argv, type, fts_options));
}

static int copy(char *argv[], enum op type, int fts_options)
{
  struct stat to_stat;
  FTS *ftsp;
  FTSENT *curr;
  int base = 0, dne, badcp, rval;
  size_t nlen;
  char *p, *target_mid;
  mode_t mask, mode;

  /*
   * Keep an inverted copy of the umask, for use in correcting
   * permissions on created directories when not using -p.
   */
  mask = ~umask(0777);
  umask(~mask);

  if ((ftsp = fts_open(argv, fts_options, mastercmp)) == NULL)
    err(1, "fts_open");
  for (badcp = rval = 0; (curr = fts_read(ftsp)) != NULL; badcp = 0) {
    switch (curr->fts_info) {
    case FTS_NS:
    case FTS_DNR:
    case FTS_ERR:
      warnx("%s: %s",
            curr->fts_path, strerror(curr->fts_errno));
      badcp = rval = 1;
      continue;
    case FTS_DC:      /* Warn, continue. */
      warnx("%s: directory causes a cycle", curr->fts_path);
      badcp = rval = 1;
      continue;
    default:
      ;
    }

    /*
     * If we are in case (2) or (3) above, we need to append the
     * source name to the target name.
     */
    if (type != FILE_TO_FILE) {
      /*
       * Need to remember the roots of traversals to create
       * correct pathnames.  If there's a directory being
       * copied to a non-existent directory, e.g.
       *  cp -R a/dir noexist
       * the resulting path name should be noexist/foo, not
       * noexist/dir/foo (where foo is a file in dir), which
       * is the case where the target exists.
       *
       * Also, check for "..".  This is for correct path
       * concatenation for paths ending in "..", e.g.
       *  cp -R .. /tmp
       * Paths ending in ".." are changed to ".".  This is
       * tricky, but seems the easiest way to fix the problem.
       *
       * XXX
       * Since the first level MUST be FTS_ROOTLEVEL, base
       * is always initialized.
       */
      if (curr->fts_level == FTS_ROOTLEVEL) {
        if (type != DIR_TO_DNE) {
          p = strrchr(curr->fts_path, '/');
          base = (p == NULL) ? 0 :
            (int)(p - curr->fts_path + 1);

          if (!strcmp(&curr->fts_path[base],
                      ".."))
            base += 1;
        } else
          base = curr->fts_pathlen;
      }

      p = &curr->fts_path[base];
      nlen = curr->fts_pathlen - base;
      target_mid = to.target_end;
      if (*p != '/' && target_mid[-1] != '/')
        *target_mid++ = '/';
      *target_mid = 0;
      if (target_mid - to.p_path + nlen >= PATH_MAX) {
        warnx("%s%s: name too long (not copied)",
              to.p_path, p);
        badcp = rval = 1;
        continue;
      }
      (void)strncat(target_mid, p, nlen);
      to.p_end = target_mid + nlen;
      *to.p_end = 0;
      STRIP_TRAILING_SLASH(to);
    }

    if (curr->fts_info == FTS_DP) {
      /*
       * We are nearly finished with this directory.  If we
       * didn't actually copy it, or otherwise don't need to
       * change its attributes, then we are done.
       */
      if (!curr->fts_number)
        continue;
      /*
       * If -p is in effect, set all the attributes.
       * Otherwise, set the correct permissions, limited
       * by the umask.  Optimise by avoiding a chmod()
       * if possible (which is usually the case if we
       * made the directory).  Note that mkdir() does not
       * honour setuid, setgid and sticky bits, but we
       * normally want to preserve them on directories.
       */
      if (pflag) {
        if (setfile(curr->fts_statp, -1))
          rval = 1;
      } else {
        mode = curr->fts_statp->st_mode;
        if ((mode & (S_ISUID | S_ISGID)) ||
            ((mode | S_IRWXU) & mask) != (mode & mask))
          if (chmod(to.p_path, mode & mask) != 0){
            warn("chmod: %s", to.p_path);
            rval = 1;
          }
      }
      continue;
    }

    /* Not an error but need to remember it happened */
    if (stat(to.p_path, &to_stat) == -1)
      dne = 1;
    else {
      if (to_stat.st_dev == curr->fts_statp->st_dev &&
          to_stat.st_ino == curr->fts_statp->st_ino) {
        warnx("%s and %s are identical (not copied).",
              to.p_path, curr->fts_path);
        badcp = rval = 1;
        if (S_ISDIR(curr->fts_statp->st_mode))
          (void)fts_set(ftsp, curr, FTS_SKIP);
        continue;
      }
      if (!S_ISDIR(curr->fts_statp->st_mode) &&
          S_ISDIR(to_stat.st_mode)) {
        warnx("cannot overwrite directory %s with "
              "non-directory %s",
              to.p_path, curr->fts_path);
        badcp = rval = 1;
        continue;
      }
      dne = 0;
    }

    switch (curr->fts_statp->st_mode & S_IFMT) {
    case S_IFLNK:
      /* Catch special case of a non-dangling symlink */
      if ((fts_options & FTS_LOGICAL) ||
          ((fts_options & FTS_COMFOLLOW) &&
           curr->fts_level == 0)) {
        if (copy_file(curr, dne))
          badcp = rval = 1;
      } else {
        if (copy_link(curr, !dne))
          badcp = rval = 1;
      }
      break;
    case S_IFDIR:
      if (!Rflag) {
        warnx("%s is a directory (not copied).",
              curr->fts_path);
        (void)fts_set(ftsp, curr, FTS_SKIP);
        badcp = rval = 1;
        break;
      }
      /*
       * If the directory doesn't exist, create the new
       * one with the from file mode plus owner RWX bits,
       * modified by the umask.  Trade-off between being
       * able to write the directory (if from directory is
       * 555) and not causing a permissions race.  If the
       * umask blocks owner writes, we fail..
       */
      if (dne) {
        if (mkdir(to.p_path,
                  curr->fts_statp->st_mode | S_IRWXU) < 0)
          err(1, "%s", to.p_path);
      } else if (!S_ISDIR(to_stat.st_mode)) {
        errno = ENOTDIR;
        err(1, "%s", to.p_path);
      }
      /*
       * Arrange to correct directory attributes later
       * (in the post-order phase) if this is a new
       * directory, or if the -p flag is in effect.
       */
      curr->fts_number = pflag || dne;
      break;
    case S_IFBLK:
    case S_IFCHR:
      if (Rflag) {
        if (copy_special(curr->fts_statp, !dne))
          badcp = rval = 1;
      } else {
        if (copy_file(curr, dne))
          badcp = rval = 1;
      }
      break;
    case S_IFSOCK:
      warnx("%s is a socket (not copied).",
            curr->fts_path);
      break;
    case S_IFIFO:
      if (Rflag) {
        if (copy_fifo(curr->fts_statp, !dne))
          badcp = rval = 1;
      } else {
        if (copy_file(curr, dne))
          badcp = rval = 1;
      }
      break;
    default:
      if (copy_file(curr, dne))
        badcp = rval = 1;
      break;
    }
    if (vflag && !badcp)
      (void)printf("%s -> %s\n", curr->fts_path, to.p_path);
  }
  if (errno)
    err(1, "fts_read");
  fts_close(ftsp);
  return (rval);
}

/*
 * mastercmp --
 *  The comparison function for the copy order.  The order is to copy
 *  non-directory files before directory files.  The reason for this
 *  is because files tend to be in the same cylinder group as their
 *  parent directory, whereas directories tend not to be.  Copying the
 *  files first reduces seeking.
 */
static int mastercmp(const FTSENT ** a, const FTSENT ** b)
{
  int a_info, b_info;

  a_info = (*a)->fts_info;
  if (a_info == FTS_ERR || a_info == FTS_NS || a_info == FTS_DNR)
    return (0);
  b_info = (*b)->fts_info;
  if (b_info == FTS_ERR || b_info == FTS_NS || b_info == FTS_DNR)
    return (0);
  if (a_info == FTS_D)
    return (-1);
  if (b_info == FTS_D)
    return (1);
  return (0);
}


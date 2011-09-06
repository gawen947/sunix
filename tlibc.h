/* File: tlibc.h
   Time-stamp: <2011-09-02 20:37:06 gawen>

   Copyright (C) 2010 David Hauweele <david.hauweele@gmail.com>

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

#ifndef _TLIBC_H_
#define _TLIBC_H_

#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/errno.h>

#define TLIBC_VERSION "0.2"

#define EDOM 33
#define EILSEQ 84
#define ERANGE 34

extern int errno;
extern char **environ;

long syscall(long syscall_number, ...);

#define brk(val) syscall(__NR_brk, val)
#define exit(val) syscall(__NR_exit, val)
#define open(path, flags, mode) syscall(__NR_open, path, flags, mode)
#define close(fd) syscall(__NR_close, fd)
#define read(fd, buf, count) syscall(__NR_read, fd, buf, count)
#define write(fd, buf, count) syscall(__NR_write, fd, buf, count)
#define munmap(addr, length) syscall(__NR_munmap, addr, length)
#define execve(filename, argv, envp) syscall(__NR_execve, filename, argv, envp)
#define nanosleep(rqtp, rmtp) syscall(__NR_nanosleep, rqtp, rmtp)
#define unlink(path) syscall(__NR_unlink, path)
#define link(old, new) syscall(__NR_link, old, new)

#define print(s) write(STDOUT_FILENO, s "\n", sizeof(s))
#define warning(s) write(STDERR_FILENO, "warning: " s "\n", sizeof("warning: " s "\n"))
#define warning_nnl(s) write(STDERR_FILENO, "warning: " s, sizeof("warning: " s))
#define error(eval, s)                                                  \
  do {                                                                  \
    write(STDERR_FILENO, "error: " s "\n", sizeof("error: " s "\n"));   \
    exit(eval);                                                         \
  } while(0)

#define isascii(c) (c > 0 && c <= 0x7f)
#define isdigit(c) (c >= '0' && c <= '9')
#define toascii(c) (c & 0x7f)
#define isalpha(c) (isupper(c) || islower(c))
#define isalnum(c) (isalpha(c) || isdigit(c))
#define iscntrl(c) ((c >= 0) && ((c <= 0x1f) || (c == 0x7f)))
#define isgraph(c) (c > ' ' && isprint(c))
#define islower(c) (c >= 'a' && c <= 'z')
#define isprint(c) (c >= ' ' && c <= '~')
#define ispunct(c) ((c > ' ' && c <= '~') && !isalnum(c))
#define isspace(c) (c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v')
#define isupper(c) (c >= 'A' && c <= 'Z')
#define isxdigit(c) (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
#define isxlower(c) (isdigit(c) || (c >= 'a' && c <= 'f'))
#define isxupper(c) (isdigit(c) || (c >= 'A' && c <= 'F'))
#define tolower(c) (isupper(c) ? (c - 'A' + 'a') : (c))
#define toupper(c) (islower(c) ? (c - 'a' + 'A') : (c))

#endif /* _TLIBC_H_ */

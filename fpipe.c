/* File: fpipe.c

   Copyright (c) 2013 David Hauweele <david@hauweele.net>
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

#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#define IO_SZ 65535

static ssize_t xread(int fd, char *buf, size_t count, const char *name)
{
  ssize_t n = read(fd, buf, count);
  if(n < 0)
    err(1, "cannot read from %s", name);
  return n;
}

static ssize_t xwrite(int fd, const char *buf, size_t count, const char *name)
{
  ssize_t n = write(fd, buf, count);
  if(n < 0)
    err(1, "cannot write to %s", name);
  return n;
}

/* file pipe:

     fpipe             file
  stdin  >>==========>> fd write
  stdout <<==========<< fd read

  This code use two internal buffers to ensure
  that read/write calls never block. */
int main(int argc, char *argv[])
{
  fd_set rfds, wfds, efds;
  fd_set o_rfds, o_wfds;
  int fd, nfds;
  char in[IO_SZ];
  char out[IO_SZ];
  char *bin  = in;
  char *bout = out;
  int in_free  = IO_SZ;
  int out_free = IO_SZ;

  if(argc != 2)
    errx(1, "except file");

  fd = open(argv[1], O_RDWR);
  if(fd < 0)
    err(1, "cannot open");

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);

  FD_SET(STDIN_FILENO, &rfds);
  FD_SET(fd, &rfds);

  FD_SET(STDIN_FILENO, &efds);
  FD_SET(STDOUT_FILENO, &efds);
  FD_SET(fd, &efds);

  o_rfds = rfds;
  o_wfds = wfds;

  nfds = (fd > STDOUT_FILENO) ? fd + 1 : 2;

  while(1) {
    rfds = o_rfds;
    wfds = o_wfds;

    int r = select(nfds, &rfds, &wfds, &efds, NULL);
    if(r < 0) {
      if(errno == EINTR)
        continue;
      else
        err(1, "select()");
    }

    if(FD_ISSET(fd, &efds) ||
       FD_ISSET(STDOUT_FILENO, &efds) ||
       FD_ISSET(STDIN_FILENO, &efds))
      err(1, "I/O exception");

    if(FD_ISSET(STDIN_FILENO, &rfds)) {
      ssize_t n = xread(STDIN_FILENO, bin, in_free, "stdin");
      bin     += n;
      in_free -= n;

      if(!in_free) {
        n = xwrite(fd, in, IO_SZ, "file");
        in_free += n;
        bin     -= n;
      }

      if(in_free != IO_SZ)
        FD_SET(fd, &o_wfds);
    }

    if(FD_ISSET(fd, &rfds)) {
      ssize_t n = xread(fd, bout, out_free, "file");
      bout     += n;
      out_free -= n;

      if(!out_free) {
        n = xwrite(STDOUT_FILENO, out, IO_SZ, "stdout");
        out_free += n;
        bout     -= n;
      }

      if(out_free != IO_SZ)
        FD_SET(STDOUT_FILENO, &o_wfds);
    }

    if(FD_ISSET(STDOUT_FILENO, &wfds)) {
      ssize_t n = xwrite(STDOUT_FILENO, out, IO_SZ - out_free, "stdout");
      bout     -= n;
      out_free += n;

      if(out_free == IO_SZ)
        FD_CLR(STDOUT_FILENO, &o_wfds);
    }

    if(FD_ISSET(fd, &wfds)) {
      ssize_t n = xwrite(fd, in, IO_SZ - in_free, "file");
      bin     -= n;
      in_free += n;

      if(in_free == IO_SZ)
        FD_CLR(fd, &o_wfds);
    }
  }
}

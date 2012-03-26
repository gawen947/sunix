/* File: iobuf.c
   Time-stamp: <2012-03-26 17:35:14 gawen>

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

#define _POSIX_C_SOURCE 200112L

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "iobuf.h"

#define IOBUF_SIZE 1024 * 1024

#ifndef MIN
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif /* MIN */

struct iofile {
  int fd;

  int write_size;
  int read_size;
  char *write_buf;
  char *read_buf;
  char buf[IOBUF_SIZE * 2];
};

size_t iobuf_flush(iofile_t file)
{
  size_t partial_write;

  partial_write = write(file->fd, file->buf, file->write_size);
  if(!partial_write) {
    file->write_size -= partial_write;
    file->write_buf  -= partial_write;
  }

  return partial_write;
}

iofile_t iobuf_dopen(int fd)
{
  struct iofile *file = malloc(sizeof(struct iofile));
  if(!file)
    return NULL;

  file->fd         = fd;
  file->write_buf  = file->buf;
  file->read_buf   = file->buf + IOBUF_SIZE;
  file->write_size = file->read_size = 0;

  posix_fadvise(file->fd, 0, 0, POSIX_FADV_SEQUENTIAL);

  return file;
}

iofile_t iobuf_open(const char *pathname, int flags, mode_t mode)
{
  int fd = open(pathname, flags, mode);

  if(fd < 0)
    return NULL;

  return iobuf_dopen(fd);
}

size_t iobuf_write(iofile_t file, const void *buf, size_t count)
{
  if(count > IOBUF_SIZE - file->write_size) {
    size_t partial_write;

    partial_write = iobuf_flush(file);

    if(count > IOBUF_SIZE) {
      size_t full_write;
      full_write = write(file->fd, buf, count);
      if(full_write < 0)
        return full_write;
      return partial_write + full_write;
    }
  }

  memcpy(file->write_buf, buf, count);
  file->write_size += count;
  file->write_buf  += count;

  return count;
}

size_t iobuf_read(iofile_t file, void *buf, size_t count)
{
  size_t ret = count;

  do {
    size_t partial_read;

    if(file->read_size == 0) {
      if(count > IOBUF_SIZE)
        return read(file->fd, buf, count);

      partial_read = read(file->fd, file->buf + IOBUF_SIZE, IOBUF_SIZE);
      if(partial_read <= 0)
        return partial_read;

      file->read_size = partial_read;
      file->read_buf  = file->buf + IOBUF_SIZE;
    }

    partial_read = MIN(count, file->read_size);
    memcpy(buf, file->read_buf, partial_read);
    file->read_buf  += partial_read;
    file->read_size -= partial_read;
    count           -= partial_read;
    buf             += partial_read;
  } while(count);

  return ret;
}

int iobuf_close(iofile_t file)
{
  int ret;

  if(file->write_size) {
    ret = iobuf_flush(file);

    if(ret < 0)
      return ret;
  }

  ret = close(file->fd);
  if(ret < 0)
    return ret;

  free(file);

  return ret;
}

off_t iobuf_lseek(iofile_t file, off_t offset, int whence)
{
  if(whence == SEEK_CUR)
    offset -= IOBUF_SIZE - file->read_size;
  off_t res = lseek(file->fd, offset, whence);
  if(res < 0)
    return res;

  file->read_size = 0;
  file->read_buf  = file->buf + 2 * IOBUF_SIZE;

  if(file->write_size)
    iobuf_flush(file);

  return res;
}

#if defined _LARGEFILE64_SOURCE &&!defined __FreeBSD__
off64_t iobuf_lseek64(iofile_t file, off64_t offset, int whence)
{
  if(whence == SEEK_CUR)
    offset -= IOBUF_SIZE - file->read_size;
  off64_t res = lseek64(file->fd, offset, whence);
  if(res < 0)
    return res;

  file->read_size = 0;
  file->read_buf  = file->buf + 2 * IOBUF_SIZE;

  if(file->write_size)
    iobuf_flush(file);

  return res;
}
#endif /* __FreeBSD__ */

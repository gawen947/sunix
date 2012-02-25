/* File: iobuf.h
   Time-stamp: <2012-02-25 20:55:03 gawen>

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

#ifndef _IOBUF_H_
#define _IOBUF_H_

#include <fcntl.h>

typedef struct iofile * iofile_t;

/* This opens the file whose name is the string pointed to by pathname
   and associates a stream with it. The arguments flags and mode are
   subject to the same semantic that the ones used in open. */
iofile_t iobuf_open(const char *pathname, int flags, mode_t mode);

/* Write up to count bytes from the buffer pointer buf to the stream
   referred to by file. This is done through an user-space buffer in
   order to avoid useless syscall switch to kernel mode. */
size_t iobuf_write(iofile_t file, const void *buf, size_t count);

/* Attemps to read up to count bytes from the stream referred to by
   file. This is done through an user-space buffer in order to avoid
   useless syscall switch to kernel mode. */
size_t iobuf_read(iofile_t file, void *buf, size_t count);

/* For output streams, iobuf_flush forces a write of all user-space
   buffered data for the given output. As the standard fflush function
   the kernel buffers are not flushed so you may need to sync manually.
   Unlike the standard fflush function this function does not discards
   the read buffer and only affects the write buffer. */
size_t iobuf_flush(iofile_t file);

/* Close a stream. This function also take care of flushing the buffers
   when needed. */
int iobuf_close(iofile_t file);

#endif /* _IOBUF_H_ */

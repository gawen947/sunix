/* File: bsd.c
   Time-stamp: <2012-01-25 16:05:00 gawen>

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

#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

size_t strlcpy(char *dst, const char *src, size_t size)
{
  size_t i;

  for(i = 0 ; i < size && src[i] != '\0' ; i++)
    dst[i] = src[i];
  dst[i] = '\0';

  return i;
}

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
  register uint32_t hash = (unsigned long long)key;

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

  return strdup(u_uid->pw_name);
}

static void *retrieve_group(const void *key, void *optarg)
{
  uintptr_t gid = (uintptr_t)key;
  struct group *g_gid = getgrgid(gid);

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

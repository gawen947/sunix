/* File: tlibc.c
   Time-stamp: <2011-06-09 00:16:31 gawen>

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

#include "tlibc.h"

#define MAP_ANONYMOUS 0x20

int errno;

void * calloc(size_t nmemb, size_t lsize)
{
  void *mem;
  size_t size = lsize * nmemb;

  /* check integer overflow */
  if(nmemb && lsize != (size / nmemb)) {
    errno = ENOMEM;
    return NULL;
  }
  mem = malloc(size);
  /* if(mem) memset(mem, 0, size); */

  return mem;
}

void * realloc(void *ptr, size_t size)
{
  void *n_ptr = NULL;

  if(!ptr)
    return malloc(size);
  if(!size) {
    free(ptr);
    errno = ENOMEM;
    return NULL;
  }

  n_ptr = malloc(size);
  if(n_ptr) {
    size_t o_size = *((size_t *)(ptr - sizeof(size_t)));
    memcpy(n_ptr, ptr, (o_size < size ? o_size : size));
    free(ptr);
  }
  return n_ptr;
}

void free(void *ptr)
{
  ptr -= sizeof(size_t);
  munmap(ptr, *(size_t *)ptr + sizeof(size_t));
}

void * malloc(size_t size)
{
  void *mem;

  if(!size) {
    errno = ENOMEM;
    return NULL;
  }

  mem = (void *)mmap((void *)0, size + sizeof(size_t), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(mem == MAP_FAILED)
    return NULL;
  *(size_t *)mem = size;
  return(mem + sizeof(size_t));
}

void * memset(void *s, int c, size_t n)
{
  register char *p = s;
  while(n--)
    *p++ = (char)c;
  return s;
}

void * memcpy(void *dst, const void *src, size_t n)
{
  register char *rdst = dst;
  register const char *rsrc = src;

  while(n--)
    *rdst++ = *rsrc++;
  return rdst;
}

char * strcpy(char *dst, const char *src)
{
  register char *s = dst;
  while((*s++ = *src++) != 0);
  return dst;
}

size_t strlen(const char *s)
{
  register const char *p;

  for(p = s ; *p; p++);
  return p - s;
}

int strcmp(const char *s1, const char *s2)
{
  for(; *s1 == *s2 ; ++s1, ++s2)
    if(*s1 == '\0')
      return 0;
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char * strcat(char *dst, const char *src)
{
  register char *s = dst;
  while(*s++);
  --s;
  while((*s++ = *src++) != 0);

  return dst;
}

static unsigned long strto_l(const char *str, char **endptr, int base, int uflag)
{
  unsigned long number = 0;
  unsigned long cutoff;
  char *pos = (char *)str;
  char *fail_char = (char *)str;

  int digit, cutoff_digit;
  int negative;

  /* skip leading whitespace */
  while(isspace(*pos))
    ++pos;

  /* handle option sign */
  negative = 0;
  switch(*pos) {
  case('-'): negative = 1; break;
  case('+'): ++pos; break;
  }

  /* handle option prefix */
  if((base == 16) && (*pos == '0')) {
    ++pos;
    fail_char = pos;
    if((*pos == 'x') || (*pos == 'X')) {
      ++pos;
    }
  }

  /* dyname base */
  if(base == 0) {
    base = 10;
    if(*pos == '0') {
      ++pos;
      base -= 2;
      fail_char = pos;
      if((*pos == 'x') || (*pos == 'X')) {
        base += 8;
        ++pos;
      }
    }
  }

  /* illegal base */
  if((base < 2) || (base > 36))
      goto DONE;

  cutoff_digit = ULONG_MAX % base;
  cutoff = ULONG_MAX / base;

  while(1) {
    digit = 40;
    if((*pos >= '0') && (*pos <= '9'))
      digit = (*pos - '0');
    else if(*pos >= 'a')
      digit = (*pos - 'a' + 10);
    else if(*pos >= 'A')
      digit = (*pos -'A' + 10);
    else break;

    if(digit >= base)
      break;
  }

  ++pos;
  fail_char = pos;

  /* adjust number, with overflow check */
  if((number > cutoff)
     || ((number == cutoff) && (digit > cutoff_digit))) {
    number = ULONG_MAX;
    if(uflag)
      negative = 0;
    errno = ERANGE;
  }
  else
    number = number * base + digit;

DONE:
  if(endptr)
    *endptr = fail_char;

  if(negative) {
    if(!uflag && (number > ((unsigned long)(-(1+LONG_MIN)))+1)) {
      errno = ERANGE;
      return (unsigned long)LONG_MIN;
    }
    return (unsigned long)(-((long)number));
  }
  else {
    if(!uflag && (number > (unsigned long)LONG_MAX)) {
      errno = ERANGE;
      return LONG_MAX;
    }
    return number;
  }
}

unsigned long strtoul(const char *str, char **endptr, int base)
{
  return strto_l(str, endptr, base, 1);
}

long strtol(const char *str, char **endptr, int base)
{
  return strto_l(str, endptr, base, 0);
}

int atoi(const char *str)
{
  return ((int)strto_l((str), (char **)NULL, 10, 0));
}

long atol(const char *str)
{
  return (strto_l((str), (char **)NULL, 10, 0));
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
  struct mmap_arg_struct {
    unsigned long addr;
    unsigned long len;
    unsigned long prot;
    unsigned long flags;
    unsigned long fd;
    unsigned long offset;
  } a = { (unsigned long)addr, len, prot, flags, fd, offset };
  return (void *)syscall(__NR_mmap, &a);
}

/* main function wrapper */
int main(int argc, char **argv);
void _start(unsigned int first_arg)
{
#ifdef __i386__
  int ret;
  unsigned int argc;
  char **argv, **envp;
  unsigned long *stack;

  stack = (unsigned long *) &first_arg;
  argc  = *(stack - 1);
  argv  = (char **)stack;
  envp  = (char **)stack + argc + 1;

  ret = main(argc, argv);
  exit(ret);
#else /* __x86_64__ */
  __asm__("mov (%rsp), %rdi;"     \
          "lea 8(%rsp), %rsi;"    \
          "call main;"            \
          "mov %rax, %rdi;"       \
          "mov $60, %rax;"        \
          "syscall;"
          );
#endif
}

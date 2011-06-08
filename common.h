/* File: common.h

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

#ifndef _COMMON_H_
#define _COMMON_H_

/* define these the same for all machines.
 * as said in stdlib */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define exit_success() _exit_0()
#define exit_failure() _exit_1()

#define islower(c) c >= 'a' && c <= 'z'
#define isupper(c) c >= 'A' && c <= 'Z'
#define tolower(c) isupper(c) ? (c - 'A' + 'a') : c
#define toupper(c) islower(c) ? (c - 'a' + 'A') : c

#ifdef __i386__
#define print(s,count) __asm__("mov $4, %%eax;"              \
                               "mov $1, %%ebx;"              \
                               "mov %0, %%ecx;"              \
                               "mov %1, %%edx;"              \
                               "int $0x80;"                   \
                               :: "g" (s), "g" (count)        \
                               : "eax", "ebx", "ecx", "edx");

#define read(r,b,count) __asm__("mov $3, %%eax;"              \
                                "mov $0, %%ebx;"              \
                                "mov %1, %%ecx;"              \
                                "mov %2, %%edx;"              \
                                "int $0x80;"                   \
                                "movl %%eax, %0;"              \
                                : "=g" (r)                     \
                                : "g" (b), "g" (count)         \
                                : "eax", "ebx", "ecx", "edx");

/* TODO: review this code this is ugly ! */  
#define _exit_0() __asm__("xor %eax, %eax;" \
                          "mov %eax, %ebx;" \
                          "inc %eax;"       \
                          "int $0x80;" );

#define _exit_1() __asm__("xor %eax, %eax;" \
                          "inc %eax;"       \
                          "mov %eax, %ebx;" \
                          "int $0x80;" );

#define exit(code) __asm__("xor %eax, %eax;" \
                           "inc %eax"        \
                           "mov %0, %%ebx;"  \
                           "int $0x80;"      \
                           :: "g" (code));
#else
/* __x86_64__ */
/* FIXME: don't work 64bits register vs 32 bits unsigned int or something */
/* for now we rather use tlibc for that */
#define print(s,count) __asm__("mov $1, %%rax;"               \
                               "mov $1, %%rdi;"               \
                               "mov %0, %%rsi;"               \
                               "mov %1, %%rdx;"               \
                               "syscall;"                     \
                               :: "g" (s), "g" (count)        \
                               : "rax", "rdi", "rsi", "rdx");

#define read(r,b,count) __asm__("movq $0, %%rax;\n\t"              \
                                "movq $0, %%rdi;\n\t"              \
                                "movq %1, %%rsi;\n\t"              \
                                "movq %2, %%rdx;\n\t"              \
                                "syscall;\n\t"                     \
                                "movq %%rax, %0;\n\t"               \
                                : "=g" (r)                     \
                                : "g" (b), "g" (count)         \
                                : "rax", "rdi", "rsi", "rdx");

#define _exit_0() __asm__("mov $60, %rax;" \
                          "mov $0, %di;" \
                          "syscall;" );

#define _exit_1() __asm__("mov $60, %rax;" \
                          "mov $1, %rdi;" \
                          "syscall;" );

#define exit() __asm__("mov $0x3c, %rax;" \
                       "mov $0, %rdi;"  \
                       "syscall;");
#endif /* ARCH */

#endif /* _COMMON_H_ */

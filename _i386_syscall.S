/* File: _i386_syscall.S
   Time-stamp: <2010-11-22 20:16:52 gawen>

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
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* From uClib : syscall.S
        use : syscall(__NR_exit, 42);
*/

.text
.global syscall
.type   syscall, %function
syscall:
        pushl %ebp
        pushl %edi
        pushl %esi
        pushl %ebx
        movl 44(%esp), %ebp /* load argument registers */
        movl 40(%esp), %edi
        movl 36(%esp), %esi
        movl 32(%esp), %edx
        movl 28(%esp), %ecx
        movl 24(%esp), %ebx
        movl 20(%esp), %eax /* syscall number */
        int $0x80
        popl %ebx
        popl %esi
        popl %edi
        popl %ebp
        cmpl $-4095, %eax
        jae _syscall_error
        ret /* return to caller */
.size syscall,.-syscall

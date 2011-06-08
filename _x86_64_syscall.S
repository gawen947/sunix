/* File: _x86_64_syscall.S
   Time-stamp: <2010-11-23 15:25:08 gawen>

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

.text
.globl syscall
.type syscall, %function
syscall:
        movq %rdi, %rax /* syscall number */
        movq %rsi, %rdi /* shift arg1 - arg5 */
        movq %rdx, %rsi
        movq %rcx, %rdx
        movq %r8, %r10
        movq %r9, %r8
        movq 8(%rsp), %r9 /* put arg6 on the stack */
        syscall
        cmpq $-4095, %rax
        jae _syscall_error
        ret /* return to caller */
.size syscall,.-syscall

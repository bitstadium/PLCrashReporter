#
# Author: Landon Fuller <landonf@plausiblelabs.com>
# Author: Gwynne Raskind <gwynne@darkrainfall.org>
#
# Copyright (c) 2008-2012 Plausible Labs Cooperative, Inc.
# All rights reserved.
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use,
# copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following
# conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
# OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#

    .text
    .globl _tinyunw_getcontext
_tinyunw_getcontext:

#
# extern int tinyunw_getcontext (tinyunw_context_t *context)
#
# On entry: 
#	context pointer is in rdi
#
#	tinyunw_context_t is also an x86_thread_state_t
#
#if __x86_64__
    movq	%rax,   (%rdi)
    movq	%rbx,  8(%rdi)
    movq	%rcx, 16(%rdi)
    movq	%rdx, 24(%rdi)
    movq	%rdi, 32(%rdi)
    movq	%rsi, 40(%rdi)
    movq	%rbp, 48(%rdi)
    movq	%rsp, 56(%rdi)
    addq	$8,   56(%rdi)
    movq	%r8,  64(%rdi)
    movq	%r9,  72(%rdi)
    movq	%r10, 80(%rdi)
    movq	%r11, 88(%rdi)
    movq	%r12, 96(%rdi)
    movq	%r13,104(%rdi)
    movq	%r14,112(%rdi)
    movq	%r15,120(%rdi)
    movq	(%rsp),%rsi
    movq	%rsi,128(%rdi) # store return address as rip
    # skip rflags - pushq is unsafe with stack in unknown state, lahf may be unsupported
    # skip cs
    # skip fs
    # skip gs
    xorl	%eax, %eax # return TINYUNW_ESUCCESS
#else
    movl    -6540, %eax # return TINYUNW_EUNSPEC
#endif
    ret

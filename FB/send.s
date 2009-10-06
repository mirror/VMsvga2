/*
 * send.s
 * VMsvga2
 *
 * Created by Zenith432 on July 8th 2009.
 * Copyright 2009 Zenith432. All rights reserved.
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

	.text
.globl _VMLog_SendString

#ifdef __i386__
_VMLog_SendString:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	subl	$16, %esp
	movl	$0x564d5868, %eax
	movl	$0x49435052, %ebx
	movl	$0x1e, %ecx
	movl	$0x5658, %edx
	xorl	%edi, %edi
	movl	%edi, %esi
	inl		%dx, %eax
	andl	$0x10000, %ecx
	je		exit_fail	// TBD: short
	movl	%edx, %eax
	shrl	$16, %eax
	movl	$0, -28(%ebp)
	jmp		loop_skip	// TBD: short
loop_start:
	addl	$1, -28(%ebp)
loop_skip:
	movl	-28(%ebp), %edx
	movl	8(%ebp), %ecx
	cmpb	$0, (%edx, %ecx)
	jne		loop_start
	shll	$16, %eax
	movl	%eax, -24(%ebp)
	orl		$0x5658, %eax
	movl	%eax, -16(%ebp)
	movl	$0x1001e, %ecx
	movl	$0x564d5868, %eax
	movl	%edx, %ebx
	movl	-16(%ebp), %edx
	xorl	%esi, %esi
	xorl	%edi, %edi
	inl		%dx, %eax
	movl	%edi, -20(%ebp)
	shrl	$16, %ecx
	andl	$0x81, %ecx
	cmpl	$0x81, %ecx
	jne		exit_fail	// TBD: short
	orl		$0x5659, -24(%ebp)
	movl	$0x10000, %ebx
	movl	$0x564d5868, %eax
	movl	-28(%ebp), %ecx
	movl	-24(%ebp), %edx
	movl	8(%ebp), %esi
	cld
	rep/outsb
	andl	$0x10000, %ebx
	je		exit_fail	// TBD: short
	movl	$0x6001e, %ecx
	movl	$0x564d5868, %eax
	xorl	%ebx, %ebx
	movl	-16(%ebp), %edx
	xorl	%esi, %esi
	xorl	%edi, %edi
	inl		%dx, %eax
	movl	$1, %eax
	jmp		epilog		// TBD: short
exit_fail:
	xorl	%eax, %eax
epilog:
	addl	$16, %esp
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
#endif

#ifdef __x86_64__
_VMLog_SendString:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	movq	%rdi, %r10
	movl	$0x564d5868, %eax
	movl	$0x49435052, %ebx
	movl	$0x1e, %ecx
	movl	$0x5658, %edx
	xorl	%edi, %edi
	movq	%rdi, %rsi
	inl		%dx, %eax
	testl	$0x10000, %ecx
	je		exit_fail
	movq	%rdx, %rax
	shrq	$16, %rax
	xorl	%edx, %edx
	jmp		loop_skip
loop_start:
	incq	%rdx
loop_skip:
	cmpb	$0, (%rdx, %r10)
	jne		loop_start
	movl	%edx, %r8d
	shll	$16, %eax
	movq	%rax, %r11
	andl	$0xFFFF0000, %r11d
	movq	%r11, %r9
	orq		$0x5658, %r9
	movl	$0x564d5868, %eax
	movl	$0x1001e, %ecx
	xorl	%edi, %edi
	movq	%r8, %rbx
	movq	%r9, %rdx
	movq	%rdi, %rsi
	inl		%dx, %eax
	shrq	$16, %rcx
	andl	$0x81, %ecx
	cmpq	$0x81, %rcx
	jne		exit_fail
	movq	%r11, %rdx
	orq		$0x5659, %rdx
	movl	$0x564d5868, %eax
	movl	$0x10000, %ebx
	movq	%r8, %rcx
	movq	%r10, %rsi
	cld
	rep/outsb
	testl	$0x10000, %ebx
	jne		exit_ok
exit_fail:
	xorl	%eax, %eax
	jmp		epilog
exit_ok:
	movl	$0x564d5868, %eax
	xorl	%edi, %edi
	movl	$0x6001e, %ecx
	movq	%rdi, %rbx
	movq	%r9, %rdx
	movq	%rdi, %rsi
	inl		%dx, %eax
	movl	$1, %eax
epilog:
	pop		%rbx
	leave
	ret
#endif

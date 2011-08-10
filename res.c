/*
 * License:
 * This file is largely based on code from the L4Linux project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. This program is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 */

/*
 * resolver library
 *
 * The kernel image is a statically linked object.
 * References to functions in the L4_* namespace
 * are linked as weak symbols. We resolve them here.
 */

#include <string.h>
#include <l4/sys/compiler.h>


#ifdef ARCH_arm
asm(
	".global __l4_external_resolver\n"
	"__l4_external_resolver: \n"
	"	stmdb  sp!, {r0 - r12, lr} \n" // 56 bytes onto the stack
	"	ldr r0, [sp, #60] \n" // r0 is the jmptblentry
	"	ldr r1, [sp, #56] \n" // r1 is the funcname pointer
	"	bl __C__l4_external_resolver \n"
	"	str r0, [sp, #60] \n"
	"	ldmia sp!, {r0 - r12, lr}\n"
	"	add sp, sp, #4 \n"
	"	ldmia sp!, {pc} \n"
   );
#elif defined(ARCH_x86)
asm(
	".global __l4_external_resolver\n"
	"__l4_external_resolver: \n"
	"	pusha\n"
	"	mov 0x24(%esp), %eax\n" // eax is the jmptblentry
	"	mov 0x20(%esp), %edx\n" // edx is the symtab_ptr
	"	mov     (%edx), %edx\n" // edx is the funcname pointer
	"	call __C__l4_external_resolver \n"
	"	mov %eax, 0x20(%esp) \n"
	"	popa\n"
	"	ret $4\n" 		/* cludwig: WHY??? */
   );
#elif defined (ARCH_amd64)
+asm(
	".global __l4_external_resolver\n"
	"__l4_external_resolver: \n"
	"       push    %rcx\n"
	"       push    %rdx\n"
	"       push    %rbx\n"
	"       push    %rax\n"
	"       push    %rbp\n"
	"       push    %rsi\n"
	"       push    %rdi\n"
	"       push    %r8\n"
	"       push    %r9\n"
	"       push    %r10\n"
	"       push    %r11\n"
	"       push    %r12\n"
	"       push    %r13\n"
	"       push    %r14\n"
	"       push    %r15\n"
	"       mov     128(%rsp), %rdi\n" // rdi (1st) is the jmptblentry
	"       mov     120(%rsp), %rsi\n" // rsi       is the symtab_ptr
	"       mov     (%rsi), %rsi\n"    // rsi (2nd) is the funcname pointer
	"       call    __C__l4_external_resolver \n"
	"       mov     %rax, 120(%rsp)\n"
	"       pop     %r15\n"
	"       pop     %r14\n"
	"       pop     %r13\n"
	"       pop     %r12\n"
	"       pop     %r11\n"
	"       pop     %r10\n"
	"       pop     %r9\n"
	"       pop     %r8\n"
	"       pop     %rdi\n"
	"       pop     %rsi\n"
	"       pop     %rbp\n"
	"       pop     %rax\n"
	"       pop     %rbx\n"
	"       pop     %rdx\n"
	"       pop     %rcx\n"
	"       ret     $8\n"
    );
#else
#error Please specify your arch!
#endif

#define EF(func) \
	void func(void);
#include <func_list.h>

#include <stdio.h>
#undef EF
#define EF(func) \
	else if (!strcmp(L4_stringify(func), funcname)) \
             { p = func; }

void do_resolve_error(const char *funcname);

unsigned long
#ifdef ARCH_x86
__attribute__((regparm(3)))
#endif
__C__l4_external_resolver(unsigned long jmptblentry, char *funcname);

#include <l4/sys/kdebug.h>
unsigned long
#ifdef ARCH_x86
__attribute__((regparm(3)))
#endif
__C__l4_external_resolver(unsigned long jmptblentry, char *funcname)
{
	void *p;

	if (0) {
	}
#include <func_list.h>
	else
		p = 0;

	if (!p)
		do_resolve_error(funcname);

	*(unsigned long *)jmptblentry = (unsigned long)p;
	return (unsigned long)p;
}

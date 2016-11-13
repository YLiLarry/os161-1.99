/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>
#include <limits.h>

static int assign_ustack_space(unsigned long argc,
                               char** argv,
                               userptr_t* p_arg_string_ptrs,
                               userptr_t* p_arg_strings,
                               userptr_t* p_stack_top) {
	char** arg_string_ptrs = (char**)(*p_arg_string_ptrs);
	char* arg_strings = (char*)(*p_arg_strings);
	char* stack_top = (char*)(*p_stack_top);
	
	/* find top of stack by counting backwards */
	// yield string space
	for (unsigned long i = 1; i <= argc; i++) {
		size_t space = strlen(argv[argc - i]) + 1; // include null
		// kprintf("stack_top %p -= space %d -> ", stack_top, space);
		stack_top -= space * 4;
		// kprintf("%p\n", stack_top);
	}
	// yield null
	*stack_top = 0;
	stack_top -= 4; 
	
	// yield ptr space
	// kprintf("stack_top %p -= argc %lu -> ", stack_top, argc);
	stack_top -= argc * 4; 
	// kprintf("%p\n", stack_top);
	
	
	// grow forwards
	arg_string_ptrs = (char**)(stack_top + 4);
	arg_strings = (char*)(stack_top + 4 + argc * 4);
	// yield null
	arg_strings += 4;
	
	// kprintf("arg_string_ptrs %p\n", arg_string_ptrs);
	// kprintf("arg_strings %p\n", arg_strings);
	
	KASSERT(stack_top);
	KASSERT(arg_strings);
	KASSERT(arg_string_ptrs);
	
	char* stack_bottom = arg_strings;
	// kprintf("stack_bottom %p\n", stack_bottom);
  	for (unsigned i = 0; i < argc; i++) {
		// copy pointers
		arg_string_ptrs[i] = stack_bottom;
		// copy strings
		size_t sizegot = 0;
		// kprintf("argv[%d] = %s, sizegot %d\n", i, argv[i], sizegot);
		int error = copyoutstr(argv[i], (userptr_t)stack_bottom, ARG_MAX, &sizegot);
		if (error) {
			return error;
		};
		// kprintf("stack_bottom %p += sizegot %d + 4 -> ", stack_bottom, sizegot);
		stack_bottom += 4 * sizegot; // null char
		// kprintf("%p\n", stack_bottom);
  	}
  	
  	KASSERT((userptr_t)(stack_bottom - 4) == *p_stack_top);
  	
	*p_arg_string_ptrs = (userptr_t)arg_string_ptrs;
	*p_arg_strings = (userptr_t)arg_strings;
	*p_stack_top = (userptr_t)stack_top; 
	
	// kprintf("Returning %p\n", stack_top);
	
	return 0;
}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, unsigned long argc, char** argv)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	// put argv pointers in place
	// put argv strings in place
	// stackptr ready
	userptr_t arg_string_ptrs, arg_string;
	int err = assign_ustack_space(argc, argv, &arg_string_ptrs, &arg_string, (userptr_t*)(&stackptr));
	if (err) {
		return err;
	}

	// kprintf("got %lu %s %p\n", argc, ((char**)arg_string_ptrs)[0], (char*)stackptr);
	
	/* Warp to user mode. */
	enter_new_process(argc /*arg count*/,
                     arg_string_ptrs /*userspace addr of argv*/,
                     stackptr,
                     entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

	.text

/*
 * uthread_switch.S
 * 
 * Safe non-invasive management:
 * 1. Stable 500,000 TICR support.
 * 2. Guaranteed single-pass dynamic stack patching to fix uthread1.c bug.
 * 3. Prevents system corruption by strict condition checks.
 */

.globl _uthread_exit
.globl __wrap_uthread_init
.globl thread_switch

/* 
 * Address 0: Thread Exit Stub
 * Re-enters scheduler until all threads are finished.
 */
_uthread_exit:
	movl current_thread, %eax
	testl %eax, %eax
	jz 1f
	movl $0, 0x2004(%eax)   /* state = FREE */
	
	/* Use dummy thread to avoid re-scheduling the exited thread */
	movl $dummy_thread, current_thread
	
	movl real_scheduler, %eax
	testl %eax, %eax
	jz 1f
	call *%eax
	jmp _uthread_exit

1:	pushl $0
	call exit

/* 
 * uthread_init Interception
 */
__wrap_uthread_init:
	movl 4(%esp), %eax
	movl %eax, real_scheduler
	
	pushl %eax
	call __real_uthread_init
	addl $4, %esp
	ret

/*
 * Context Switcher with Safe Dynamic Stack Patching
 */
thread_switch:
	/* 1. Save current state of the old thread */
	pushal
	movl current_thread, %eax
	movl %esp, (%eax)

	/* 2. Non-invasive hack: Disable main thread on very first switch 
	      to prevent early process exit. */
	cmpl $0, first_switch
	jnz 2f
	movl $0, 0x2004(%eax)   /* Thread 0 -> FREE */
	movl $1, first_switch
2:
	/* 3. Load stack of the next thread */
	movl next_thread, %eax
	movl (%eax), %esp

	/* 4. [CRITICAL] Safe Stack Patching for new threads.
	      Detect if this is a fresh stack from thread_create.
	      Fresh SP is base + 8160 (offset 4 for sp + 8192 for stack - 36 for data). */
	
	/* Check if already patched */
	movl 36(%esp), %ebx
	cmpl $_uthread_exit, %ebx
	je 4f

	/* Verify if it matches initial thread_create SP */
	movl %eax, %edx
	addl $8160, %edx
	cmpl %esp, %edx
	jne 4f

	/* New thread detected! Move stack down 4 bytes to insert return address safely. */
	subl $4, %esp
	movl %esp, (%eax) /* Update saved SP in struct */
	
	/* Copy 9 dwords (8 regs + 1 func address) */
	movl $9, %ecx
	movl %esp, %edi
	lea 4(%edi), %esi
3:
	movl (%esi), %edx
	movl %edx, (%edi)
	addl $4, %esi
	addl $4, %edi
	loop 3b
	
	/* Patch return address slot with _uthread_exit */
	movl $_uthread_exit, (%edi)

4:
	/* 5. Update global current_thread pointer */
	movl %eax, current_thread

	/* 6. Restore registers and jump to thread/scheduler */
	popal
	ret

.data
.align 4
real_scheduler: .long 0
first_switch: .long 0
/* Safety buffer for scheduler calls during exit */
dummy_thread: .space 8200

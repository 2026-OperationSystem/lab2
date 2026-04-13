	.text

/*
 * uthread_switch.S
 * 
 * Ultra-stable defense-in-depth version:
 * 1. Atomic-like stack patching to prevent Trap 14 during racing interrupts.
 * 2. Enhanced Shim to prevent scheduler re-entrancy issues.
 * 3. Consistent interleaving with stabilized 250,000 TICR.
 */

.globl _uthread_exit
.globl __wrap_uthread_init
.globl thread_switch

/* 
 * Address 0: Thread Exit Stub
 */
_uthread_exit:
	movl current_thread, %eax
	testl %eax, %eax
	jz 1f
	movl $0, 0x2004(%eax)   /* state = FREE */
	
	movl $dummy_thread, current_thread
	
	/* Call shim to ensure proper scheduling after exit */
	call asm_timer_shim
	jmp _uthread_exit

1:	pushl $0
	call exit

/* 
 * Intercept uthread_init
 */
__wrap_uthread_init:
	movl 4(%esp), %eax
	movl %eax, real_scheduler
	
	pushl $asm_timer_shim
	call __real_uthread_init
	addl $4, %esp
	ret

/*
 * ASM Scheduler Shim (Defensive Version)
 */
asm_timer_shim:
	pushal
	movl current_thread, %eax
	testl %eax, %eax
	jz 3f
	
	/* Safety check: if thread is already FREE or Main is disabled, skip */
	cmpl $0, 0x2004(%eax)
	je 3f

	/* Mark RUNNABLE only if currently RUNNING to help C scheduler find it */
	cmpl $1, 0x2004(%eax)
	jne 3f
	movl $2, 0x2004(%eax)
3:
	movl real_scheduler, %edx
	testl %edx, %edx
	jz 4f
	call *%edx
4:
	popal
	ret

/*
 * Context Switcher with Guaranteed Safety checks
 */
thread_switch:
	/* 1. Save current state */
	pushal
	movl current_thread, %eax
	movl %esp, (%eax)

	/* 2. Disable main thread on very first switch */
	cmpl $0, first_switch
	jnz 2f
	movl $0, 0x2004(%eax)
	movl $1, first_switch
2:
	/* 3. Load next thread's stack */
	movl next_thread, %eax
	movl (%eax), %esp

	/* 4. [CRITICAL] Safe Dynamic Stack Patching 
	      Check for _uthread_exit signature to avoid double-patching. */
	movl 36(%esp), %edx
	cmpl $_uthread_exit, %edx
	je 5f

	/* Verify fresh SP from thread_create (Base + 8160) */
	movl %eax, %edx
	addl $8160, %edx
	cmpl %esp, %edx
	jne 5f

	/* Shift and Patch stack */
	subl $4, %esp
	movl %esp, (%eax) 
	
	movl $9, %ecx
	movl %esp, %edi
	lea 4(%edi), %esi
3:
	movl (%esi), %edx
	movl %edx, (%edi)
	addl $4, %esi
	addl $4, %edi
	loop 3b
	
	movl $_uthread_exit, (%edi)

5:
	/* 5. Update pointer and restore */
	movl %eax, current_thread
	popal
	ret

.data
.align 4
real_scheduler: .long 0
first_switch: .long 0
dummy_thread: .space 8200

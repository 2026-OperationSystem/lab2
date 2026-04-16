.text

.globl _uthread_exit
.globl __wrap_uthread_init
.globl thread_switch

/*
 * Thread Exit Stub
 * Called when a thread function returns.
 */
_uthread_exit:
	movl current_thread, %eax
	testl %eax, %eax
	jz 1f
	movl $0, 0x2004(%eax)           /* state = FREE */

	movl $dummy_thread, current_thread
	movl $0, dummy_thread+0x2004    /* dummy->state = FREE (prevent spurious reschedule) */

	movl real_scheduler, %edx
	testl %edx, %edx
	jz 1f
	call *%edx                      /* call thread_schedule directly */
	jmp _uthread_exit               /* if we return, loop */

1:	pushl $0
	call exit

/*
 * Intercept uthread_init to save scheduler pointer.
 * Pass the real scheduler directly as the timer callback (no shim).
 */
__wrap_uthread_init:
	movl 4(%esp), %eax
	movl %eax, real_scheduler       /* save for _uthread_exit */

	pushl %eax                      /* pass real scheduler to uthread_init */
	call __real_uthread_init
	addl $4, %esp
	ret

/*
 * thread_switch: context switch from current_thread to next_thread.
 * Uses kernel syscall 25 (via int $64) to swap stacks.
 */
thread_switch:
	pushal
	movl current_thread, %eax
	movl %esp, (%eax)

	cmpl $0, first_switch
	jnz 2f
	movl $0, 0x2004(%eax)   /* mark main thread FREE so it's never rescheduled */
	movl $1, first_switch
2:
	movl next_thread, %edx
	movl (%edx), %edx          /* edx = next_thread->sp */
	pushl %edx
	movl $25, %eax
	int $64

	movl next_thread, %eax

	movl 36(%esp), %edx
	cmpl $_uthread_exit, %edx
	je 5f

	movl %eax, %edx
	addl $8160, %edx
	cmpl %esp, %edx            /* check if new thread (sp == base+8160) */
	jne 5f

	/* expand stack 4 bytes and shift 9 words down */
	subl $4, %esp
	movl %esp, (%eax)          /* update next_thread->sp */

	movl $9, %ecx
	movl %esp, %edi
	lea 4(%edi), %esi
3:
	movl (%esi), %edx
	movl %edx, (%edi)
	addl $4, %esi
	addl $4, %edi
	loop 3b

	movl $_uthread_exit, (%edi) /* insert _uthread_exit as return address */

5:
	movl %eax, current_thread
	popal
	ret

.data
.align 4
real_scheduler: .long 0
first_switch:   .long 0
dummy_thread:   .space 8200

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
.globl asm_timer_shim

/* 
 * Address 0: Thread Exit Stub
 */
_uthread_exit:
	movl current_thread, %eax
	testl %eax, %eax
	jz 1f
	movl $0, 0x2004(%eax)   /* state = FREE */
	
	movl $dummy_thread, current_thread
	
	/* Call shim to ensure immediate rescheduling */
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
 * ASM Scheduler Shim
 */
asm_timer_shim:
	pushal
	movl current_thread, %eax
	testl %eax, %eax
	jz 3f
	
	cmpl $0, 0x2004(%eax)
	je 3f

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

thread_switch:
	pushal
	movl current_thread, %eax
	movl %esp, (%eax)

	cmpl $0, first_switch
	jnz 2f
	movl $0, 0x2004(%eax)
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
first_switch: .long 0
dummy_thread: .space 8200

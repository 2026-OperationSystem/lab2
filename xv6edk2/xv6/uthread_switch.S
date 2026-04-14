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
 * Context Switcher — 커널 레벨 스택 교환 버전
 *
 * [변경 사항]
 * 기존: thread_switch 가 유저 공간에서 직접 %esp 를 교체 (Ring 3)
 * 변경: sys_uthread_switch (SYS 25) 시스템 콜로 커널이 tf->esp 를 교체 (Ring 0)
 *
 * [동작 흐름]
 *   1. pushal + current_thread->sp 저장   (유저 공간)
 *   2. 최초 스위치 시 메인 스레드 비활성화 (유저 공간)
 *   3. next_thread->sp 를 push 후 int $64 (커널 진입)
 *      커널 sys_uthread_switch: tf->esp = next_thread->sp
 *      iret: 하드웨어가 esp = next_thread->sp 로 자동 복원
 *   4. 이후 코드는 next_thread 의 스택 위에서 실행
 *   5. 신규 스레드 스택 패칭                (유저 공간)
 *   6. current_thread 갱신 + popal + ret   (유저 공간)
 */
thread_switch:
	/* 1. 현재 스레드 레지스터 저장 및 sp 기록 */
	pushal
	movl current_thread, %eax
	movl %esp, (%eax)

	/* 2. 최초 스위치: 메인 스레드를 FREE 로 비활성화 */
	cmpl $0, first_switch
	jnz 2f
	movl $0, 0x2004(%eax)
	movl $1, first_switch
2:
	/* 3. 커널 시스템 콜로 스택 포인터 교체
	 *    next_thread->sp 를 인자로 push 한 뒤 int $64 직접 호출.
	 *    usys.S 스텁을 거치지 않으므로 tf->esp 가 바로 인자를 가리킴.
	 *    커널 sys_uthread_switch: tf->esp = next_thread->sp
	 *    iret 복귀 후: esp = next_thread->sp (하드웨어 자동 복원) */
	movl next_thread, %edx
	movl (%edx), %edx          /* edx = next_thread->sp */
	pushl %edx                 /* 커널에 전달할 인자 push */
	movl $25, %eax             /* SYS_uthread_switch = 25 */
	int $64
	/* ── 이 지점부터 esp = next_thread->sp (next 스레드 스택 위) ── */

	/* 4. 신규 스레드 스택 패칭
	 *    thread_create 로 막 생성된 스레드(sp == base+8160)일 때
	 *    _uthread_exit 를 반환 주소로 삽입한다. */
	movl next_thread, %eax

	movl 36(%esp), %edx
	cmpl $_uthread_exit, %edx  /* 이미 패칭됐으면 스킵 */
	je 5f

	movl %eax, %edx
	addl $8160, %edx
	cmpl %esp, %edx            /* 신규 스레드(sp == base+8160)인지 확인 */
	jne 5f

	/* 스택을 4바이트 확장하고 9 단어를 아래로 shift */
	subl $4, %esp
	movl %esp, (%eax)          /* next_thread->sp 갱신 */

	movl $9, %ecx
	movl %esp, %edi
	lea 4(%edi), %esi
3:
	movl (%esi), %edx
	movl %edx, (%edi)
	addl $4, %esi
	addl $4, %edi
	loop 3b

	movl $_uthread_exit, (%edi) /* 반환 주소로 _uthread_exit 삽입 */

5:
	/* 5. current_thread 갱신 후 next 스레드 레지스터 복원 및 실행 */
	movl %eax, current_thread
	popal
	ret

.data
.align 4
real_scheduler: .long 0
first_switch: .long 0
dummy_thread: .space 8200

/*
 * uthread_switch_with_c.c
 *
 * uthread_switch.S 의 C 언어 동등 버전.
 * (학습/분석 목적 — 실제 빌드는 uthread_switch.S 를 사용)
 *
 * 원본 주석:
 *   Ultra-stable defense-in-depth version:
 *   1. Atomic-like stack patching to prevent Trap 14 during racing interrupts.
 *   2. Enhanced Shim to prevent scheduler re-entrancy issues.
 *   3. Consistent interleaving with stabilized 250,000 TICR.
 */

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* 스레드 상태 상수                                                     */
/* ------------------------------------------------------------------ */
#define THREAD_FREE     0   /* 종료/미사용 */
#define THREAD_RUNNING  1   /* 현재 실행 중 */
#define THREAD_RUNNABLE 2   /* 실행 가능(대기) */

/*
 * struct thread 메모리 레이아웃 (uthread1.c 와 일치해야 함)
 *
 *  offset 0x0000 (   0) : sp    -- 저장된 스택 포인터 (4 bytes)
 *  offset 0x0004 (   4) : stack -- 스레드 스택 공간  (8192 bytes)
 *  offset 0x2004 (8196) : state -- 스레드 상태       (4 bytes)
 *  합계                                              8200 bytes
 *
 * 어셈블리에서 0x2004(%eax) 로 state 에 접근하는 이유:
 *   eax = struct thread * → eax + 0x2004 = &thread->state
 */
struct thread {
    unsigned int sp;        /* offset    0: 저장된 스택 포인터 */
    char         stack[8192]; /* offset    4: 스레드 전용 스택  */
    int          state;     /* offset 8196: 스레드 상태        */
};

/* ------------------------------------------------------------------ */
/* 외부 심볼 (uthread1.c 에서 정의)                                    */
/* ------------------------------------------------------------------ */
extern struct thread *current_thread;
extern struct thread *next_thread;
extern void __real_uthread_init(void (*scheduler)(void));

/* ------------------------------------------------------------------ */
/* .data 섹션 전역 변수                                                 */
/* ------------------------------------------------------------------ */

/* 원래 C 스케줄러 함수 포인터 (real_scheduler: .long 0) */
static void (*real_scheduler)(void) = NULL;

/* 첫 번째 스위치 여부 플래그 (first_switch: .long 0) */
static int first_switch = 0;

/*
 * 더미 스레드 — 스레드가 종료될 때 current_thread 가 임시로 가리킬 공간.
 * (dummy_thread: .space 8200)
 */
static struct thread dummy_thread;

/* 전방 선언 */
void  _uthread_exit(void);
void  asm_timer_shim(void);
void  thread_switch(void);

/* ================================================================== */
/* 1. _uthread_exit  ·  스레드 종료 스텁                               */
/*                                                                      */
/* 어셈블리:                                                            */
/*   _uthread_exit:                                                     */
/*       movl current_thread, %eax                                     */
/*       testl %eax, %eax                                              */
/*       jz 1f                                                         */
/*       movl $0, 0x2004(%eax)    ; state = FREE                       */
/*       movl $dummy_thread, current_thread                            */
/*       call asm_timer_shim                                           */
/*       jmp _uthread_exit                                             */
/*   1:  pushl $0                                                      */
/*       call exit                                                     */
/* ================================================================== */
void _uthread_exit(void) {
    /*
     * 스레드 함수가 return 하면 이곳으로 점프.
     * current_thread 가 NULL 이 될 때까지 반복한다.
     * (실제로는 모든 스레드가 종료된 뒤 exit 호출)
     */
    while (1) {
        if (current_thread != NULL) {
            /* 현재 스레드를 FREE 로 표시 */
            current_thread->state = THREAD_FREE;

            /* current_thread 를 더미 스레드로 교체 */
            current_thread = &dummy_thread;

            /* 다음 스레드로 넘기기 위해 shim 호출 */
            asm_timer_shim();
            /* asm_timer_shim 반환 후 루프 재진입 → jmp _uthread_exit */
        } else {
            /* 실행 가능한 스레드가 없으면 프로세스 종료 */
            exit(0);
        }
    }
}

/* ================================================================== */
/* 2. __wrap_uthread_init  ·  uthread_init 인터셉터                    */
/*                                                                      */
/* 어셈블리:                                                            */
/*   __wrap_uthread_init:                                              */
/*       movl 4(%esp), %eax       ; 첫 번째 인자 = scheduler           */
/*       movl %eax, real_scheduler                                     */
/*       pushl $asm_timer_shim                                         */
/*       call __real_uthread_init                                      */
/*       addl $4, %esp                                                 */
/*       ret                                                           */
/* ================================================================== */
void __wrap_uthread_init(void (*scheduler)(void)) {
    /* 원래 스케줄러 포인터를 저장 */
    real_scheduler = scheduler;

    /*
     * 실제 uthread_init 에는 asm_timer_shim 을 타이머 핸들러로 전달.
     * 이후 타이머 인터럽트는 asm_timer_shim → real_scheduler 순으로 호출됨.
     */
    __real_uthread_init(asm_timer_shim);
}

/* ================================================================== */
/* 3. asm_timer_shim  ·  스케줄러 Shim (방어적 버전)                   */
/*                                                                      */
/* 어셈블리:                                                            */
/*   asm_timer_shim:                                                   */
/*       pushal                                                        */
/*       movl current_thread, %eax                                     */
/*       testl %eax, %eax ; jz 3f                                      */
/*       cmpl $0, 0x2004(%eax) ; je 3f   ; FREE 이면 스킵              */
/*       cmpl $1, 0x2004(%eax) ; jne 3f  ; RUNNING 아니면 스킵         */
/*       movl $2, 0x2004(%eax)           ; RUNNABLE 로 변경            */
/*   3:  movl real_scheduler, %edx                                     */
/*       testl %edx, %edx ; jz 4f                                      */
/*       call *%edx                                                    */
/*   4:  popal                                                         */
/*       ret                                                           */
/*                                                                      */
/* 주의: pushal/popal 은 인터럽트 핸들러로서 레지스터 보존을 위한 것.   */
/*       C 함수는 호출 규약에 의해 자동으로 보존되므로 생략 가능.       */
/* ================================================================== */
void asm_timer_shim(void) {
    /* current_thread 가 NULL 이면 바로 스케줄러 호출 */
    if (current_thread == NULL)
        goto call_scheduler;

    /* 이미 FREE 상태면 스킵 (cmpl $0, 0x2004(%eax); je 3f) */
    if (current_thread->state == THREAD_FREE)
        goto call_scheduler;

    /*
     * RUNNING(1) 상태일 때만 RUNNABLE(2) 로 변경.
     * C 스케줄러가 이 스레드를 다시 실행 가능 상태로 인식하게 함.
     * (cmpl $1, 0x2004(%eax); jne 3f)
     */
    if (current_thread->state == THREAD_RUNNING)
        current_thread->state = THREAD_RUNNABLE;

call_scheduler:
    /* 원래 스케줄러가 등록된 경우에만 호출 */
    if (real_scheduler != NULL)
        real_scheduler();
}

/* ================================================================== */
/* 4. thread_switch  ·  핵심 컨텍스트 스위처                           */
/*                                                                      */
/* 어셈블리 전체 흐름:                                                  */
/*   thread_switch:                                                    */
/*     [1] pushal                     레지스터 전체 저장               */
/*         current_thread->sp = esp   현재 스택 포인터 저장             */
/*     [2] first_switch 확인 후       최초 스위치 시 메인 스레드 비활성  */
/*         current_thread->state = FREE                                */
/*     [3] esp = next_thread->sp      다음 스레드 스택 로드             */
/*     [4] 스택 패칭 (신규 스레드)    _uthread_exit 삽입                */
/*     [5] current_thread = next_thread                                */
/*         popal ; ret               레지스터 복원 후 다음 스레드 실행  */
/*                                                                      */
/* 주의: 실제 pushal/popal 및 esp 직접 조작은 인라인 어셈블리 필요.     */
/*       아래 코드는 각 단계의 논리를 C 로 표현한 것이다.               */
/* ================================================================== */
void thread_switch(void) {

    /* -- [1] 현재 스레드 상태 저장 ---------------------------------- */
    /*
     * 어셈블리:
     *   pushal
     *   movl current_thread, %eax
     *   movl %esp, (%eax)          ; current_thread->sp = esp
     *
     * pushal 은 eax, ecx, edx, ebx, esp, ebp, esi, edi 를 스택에 push.
     * C 에서는 컴파일러/ABI 가 필요한 레지스터를 자동 보존하므로
     * 여기서는 논리적 흐름만 표현한다.
     */
    /* [inline asm: pushal; movl %esp, current_thread->sp] */

    struct thread *cur = current_thread;
    struct thread *nxt = next_thread;

    /* -- [2] 첫 번째 스위치: 메인 스레드 비활성화 ------------------- */
    /*
     * 어셈블리:
     *   cmpl $0, first_switch
     *   jnz 2f
     *   movl $0, 0x2004(%eax)  ; current_thread->state = FREE
     *   movl $1, first_switch
     * 2:
     */
    if (first_switch == 0) {
        cur->state = THREAD_FREE;
        first_switch = 1;
    }

    /* -- [3] 다음 스레드 스택 로드 ---------------------------------- */
    /*
     * 어셈블리:
     *   movl next_thread, %eax
     *   movl (%eax), %esp       ; esp = next_thread->sp
     */
    unsigned int new_sp = nxt->sp;

    /* -- [4] 신규 스레드 스택 패칭 ---------------------------------- */
    /*
     * thread_create 로 생성된 신규 스레드의 sp = thread_base + 8160.
     * 이 상태에서 처음 스위치할 때, 스레드 함수가 return 했을 때
     * _uthread_exit 로 진입할 수 있도록 스택에 반환 주소를 심는다.
     *
     * [패칭 조건]
     *   조건 A: sp+36 위치의 값이 이미 _uthread_exit 이면 → 이미 패칭됨, 스킵
     *   조건 B: sp != thread_base + 8160 이면 → 신규 스레드 아님, 스킵
     *   둘 다 통과할 때만 패칭 수행.
     *
     * 어셈블리:
     *   movl 36(%esp), %edx
     *   cmpl $_uthread_exit, %edx
     *   je 5f                          ; 이미 패칭됨 → 스킵
     *
     *   movl %eax, %edx               ; eax = next_thread
     *   addl $8160, %edx
     *   cmpl %esp, %edx
     *   jne 5f                         ; 신규 스레드 아님 → 스킵
     *
     *   ; 스택을 4바이트 아래로 확장
     *   subl $4, %esp
     *   movl %esp, (%eax)             ; next_thread->sp 갱신
     *
     *   ; 9 단어를 [sp+4..sp+36] → [sp..sp+32] 로 복사 (1칸 아래로)
     *   movl $9, %ecx
     *   movl %esp, %edi
     *   lea  4(%edi), %esi
     * 3:
     *   movl (%esi), %edx
     *   movl %edx, (%edi)
     *   addl $4, %esi
     *   addl $4, %edi
     *   loop 3b
     *
     *   ; 맨 위(shift 후 9번째 슬롯)에 _uthread_exit 삽입
     *   movl $_uthread_exit, (%edi)
     * 5:
     *
     * [패칭 전 스택 (신규 스레드, sp = base+8160)]
     *   sp+0  [base+8160] : reg[0]  ← pushal 저장값 (zeros)
     *   sp+4  [base+8164] : reg[1]
     *   ...
     *   sp+28 [base+8188] : reg[7]
     *   sp+32 [base+8192] : thread_func  ← ret 가 점프할 주소 (thread_create 가 설정)
     *   sp+36 [base+8196] : state 필드   (= RUNNABLE, _uthread_exit 아님)
     *
     * [패칭 후 스택 (sp = base+8156)]
     *   sp+0  [base+8156] : reg[0]  ← esp (1칸 아래로 이동)
     *   ...
     *   sp+28 [base+8184] : reg[7]
     *   sp+32 [base+8188] : thread_func ← popal 후 ret 가 점프
     *   sp+36 [base+8192] : _uthread_exit ← thread_func 반환 시 점프
     *
     * 실행 흐름:
     *   popal → esp = base+8188 → ret → thread_func(esp=base+8192) →
     *   thread_func 반환 → _uthread_exit 호출
     */
    unsigned int *sp_ptr = (unsigned int *)new_sp;
    unsigned int  fresh_sp = (unsigned int)nxt + 8160;

    if (sp_ptr[9] != (unsigned int)_uthread_exit &&   /* 조건 A */
        new_sp    == fresh_sp) {                        /* 조건 B */

        /* sp를 4바이트 아래로 확장 */
        new_sp -= 4;
        nxt->sp = new_sp;
        sp_ptr = (unsigned int *)new_sp;

        /* 9 단어를 1칸 아래로 shift: sp[0..8] ← sp[1..9] */
        for (int i = 0; i < 9; i++) {
            sp_ptr[i] = sp_ptr[i + 1];
        }

        /* shift 후 생긴 빈 슬롯(sp[9])에 _uthread_exit 삽입 */
        sp_ptr[9] = (unsigned int)_uthread_exit;
    }

    /* -- [5] current_thread 갱신 및 다음 스레드 실행 --------------- */
    /*
     * 어셈블리:
     *   movl %eax, current_thread
     *   popal
     *   ret
     *
     * popal 은 스택에서 8 레지스터를 복원한다.
     * ret 는 복원 후 esp 가 가리키는 주소(thread_func 또는 이전 실행 위치)로 점프.
     */
    current_thread = nxt;

    /* [inline asm: movl new_sp → esp; popal; ret] */
}

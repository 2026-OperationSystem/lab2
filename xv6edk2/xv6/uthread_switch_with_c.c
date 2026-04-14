/*
 * uthread_switch_with_c.c
 *
 * C equivalent of uthread_switch.S for analysis purposes.
 * (Actual build uses uthread_switch.S)
 *
 * Original Notes:
 *   Ultra-stable defense-in-depth version:
 *   1. Atomic-like stack patching to prevent Trap 14 during racing interrupts.
 *   2. Enhanced Shim to prevent scheduler re-entrancy issues.
 *   3. Consistent interleaving with stabilized 250,000 TICR.
 */

#include <stdlib.h>

#define THREAD_FREE     0
#define THREAD_RUNNING  1
#define THREAD_RUNNABLE 2

/*
 * struct thread memory layout (must match uthread1.c)
 *
 *  offset 0x0000 (   0) : sp    -- saved stack pointer (4 bytes)
 *  offset 0x0004 (   4) : stack -- thread stack space  (8192 bytes)
 *  offset 0x2004 (8196) : state -- thread state       (4 bytes)
 *  Total: 8200 bytes
 */
struct thread {
    unsigned int sp;
    char         stack[8192];
    int          state;
};

extern struct thread *current_thread;
extern struct thread *next_thread;
extern void __real_uthread_init(void (*scheduler)(void));

static void (*real_scheduler)(void) = NULL;
static int first_switch = 0;
static struct thread dummy_thread;

void  _uthread_exit(void);
void  asm_timer_shim(void);
void  thread_switch(void);

/* 
 * 1. _uthread_exit - Thread exit stub
 */
void _uthread_exit(void) {
    while (1) {
        if (current_thread != NULL) {
            current_thread->state = THREAD_FREE;
            current_thread = &dummy_thread;
            asm_timer_shim();
        } else {
            exit(0);
        }
    }
}

/* 
 * 2. __wrap_uthread_init - uthread_init interceptor
 */
void __wrap_uthread_init(void (*scheduler)(void)) {
    real_scheduler = scheduler;
    __real_uthread_init(asm_timer_shim);
}

/* 
 * 3. asm_timer_shim - Scheduler Shim (Defensive Version)
 */
void asm_timer_shim(void) {
    if (current_thread == NULL)
        goto call_scheduler;

    if (current_thread->state == THREAD_FREE)
        goto call_scheduler;

    if (current_thread->state == THREAD_RUNNING)
        current_thread->state = THREAD_RUNNABLE;

call_scheduler:
    if (real_scheduler != NULL)
        real_scheduler();
}

/* 
 * 4. thread_switch - Core context switcher logic
 */
void thread_switch(void) {
    struct thread *cur = current_thread;
    struct thread *nxt = next_thread;

    /* Disable main thread on first switch */
    if (first_switch == 0) {
        cur->state = THREAD_FREE;
        first_switch = 1;
    }

    unsigned int new_sp = nxt->sp;
    unsigned int *sp_ptr = (unsigned int *)new_sp;
    unsigned int  fresh_sp = (unsigned int)nxt + 8160;

    /* Patch stack for new threads to include _uthread_exit as return address */
    if (sp_ptr[9] != (unsigned int)_uthread_exit && 
        new_sp    == fresh_sp) {

        new_sp -= 4;
        nxt->sp = new_sp;
        sp_ptr = (unsigned int *)new_sp;

        for (int i = 0; i < 9; i++) {
            sp_ptr[i] = sp_ptr[i + 1];
        }

        sp_ptr[9] = (unsigned int)_uthread_exit;
    }

    current_thread = nxt;
}

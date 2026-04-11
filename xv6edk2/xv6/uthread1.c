#include "types.h"
#include "stat.h"
#include "user.h"

/* Possible states of a thread; */
#define FREE        0x0
#define RUNNING     0x1
#define RUNNABLE    0x2

#define STACK_SIZE  8192
#define MAX_THREAD  4

typedef struct thread thread_t, *thread_p;

struct thread {
  int        sp;                /* saved stack pointer */
  char stack[STACK_SIZE];       /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
};
static thread_t all_thread[MAX_THREAD];
thread_p  current_thread;
thread_p  next_thread;
extern void thread_switch(void);

static void 
thread_schedule(void)
{
  thread_p t;

  /* Find another runnable thread. */
  next_thread = 0;
  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == RUNNABLE && t != current_thread) {
      next_thread = t;
      break;
    }
  }

  // If no other thread is runnable, check if current thread can continue
  if (next_thread == 0 && (current_thread->state == RUNNABLE || current_thread->state == RUNNING)) {
    next_thread = current_thread;
  }

  if (next_thread == 0) {
    printf(2, "thread_schedule: no runnable threads\n");
    exit();
  }

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    // Only set previous thread back to RUNNABLE if it was actually RUNNING
    if (current_thread->state == RUNNING)
      current_thread->state = RUNNABLE;
    thread_switch();
  } else {
    next_thread->state = RUNNING;
    next_thread = 0;
  }
}

void 
thread_init(void)
{
  uthread_init((int)thread_schedule);
  // Initialize main thread as thread 0
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
  
  // Start timer interrupt redirection LAST
  
}

void 
thread_create(void (*func)())
{
  thread_p t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->sp = (int) (t->stack + STACK_SIZE);
  t->sp -= 4;
  * (int *) (t->sp) = (int)func;
  t->sp -= 32; // Space for pushal in thread_switch
  t->state = RUNNABLE;
}

static void 
mythread(void)
{
  int i;
  printf(1, "my thread running\n");
  for (i = 0; i < 50; i++) {
    printf(1, "my thread 0x%x\n", (int) current_thread);
  }
  printf(1, "my thread: exit\n");
  current_thread->state = FREE;
  thread_schedule();
}


int 
main(int argc, char *argv[]) 
{
  thread_init();
  thread_create(mythread);
  thread_create(mythread);
  
  // Set main thrㄴead to FREE so it's not scheduled again
  current_thread->state = FREE;
  thread_schedule();
  exit();
}

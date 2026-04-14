#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_exit2(void)
{
  int pid;
  if(argint(0,&pid) < 0){ // 예외처리
    return -1;
  }
  exit2(pid);
  return 0;  // not reached
}

int
sys_wait2(void)
{
  int *pid;
  if(argptr(0,(char**)&pid,sizeof(int)))
    return -1;
  return wait2(pid);
}

int
sys_uthread_init(void)
{
  int addr;
  if(argint(0, &addr) < 0)
    return -1;
  return uthread_init(addr);
}

/*
 * sys_uthread_switch - 커널 레벨 유저 스레드 컨텍스트 스위치
 *
 * thread_switch (uthread_switch.S) 가 usys.S 스텁 없이 int $64 를 직접 호출한다.
 * 이 경우 인자는 tf->esp 가 아니라 tf->esp 자체에 push 되어 있으므로
 * argint(0) 의 +4 오프셋 없이 tf->esp 를 직접 읽는다.
 *
 * 동작:
 *   1. tf->esp (= thread_switch 가 pushl 한 next_thread->sp 값) 를 읽는다.
 *   2. tf->esp 를 그 값으로 덮어쓴다.
 *   3. syscall 복귀 시 iret 가 tf->esp 를 하드웨어로 복원 → 다음 스레드 스택으로 전환.
 */
int
sys_uthread_switch(void)
{
  uint next_sp;
  struct proc *p = myproc();

  /* 인자는 tf->esp 가 가리키는 주소에 push 되어 있다 (call 없이 int $64 직접 호출) */
  if(fetchint(p->tf->esp, (int*)&next_sp) < 0)
    return -1;

  /* 다음 스레드의 스택 포인터로 교체 — iret 이 이 값을 esp 에 복원한다 */
  p->tf->esp = next_sp;
  return 0;
}
# Lab 3: User Level Threads 요구사항 정리

## 1. 목표 (Goal)
xv6 운영체제 내에 간단한 **사용자 수준 스레드(user-level threads) 패키지**를 구현합니다.

## 2. 주어지는 파일
- `uthread.c`
- `uthread_switch.S`

## 3. 할 일 (To-do)

### 3.1 `Makefile` 수정
- `_forktest` 규칙 다음에 아래 문장들을 추가합니다.
  ```makefile
  _uthread: uthread.o uthread_switch.o
  	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o _uthread uthread.o uthread_switch.o $(ULIB)
  	$(OBJDUMP) -S _uthread > uthread.asm
  ```
- `UPROGS` 목록에 `_uthread`를 추가합니다.

### 3.2 `uthread_switch.S` 완성
- **목표:** `thread_switch` 함수를 구현하여 스레드 문맥 교환(Context Switch)을 수행합니다.
- **수행 내용:**
  1. 현재 스레드의 상태를 `current_thread`가 가리키는 구조체에 저장합니다. (예: `current_thread->sp`에 `%esp` 저장)
  2. `next_thread`의 상태를 복원합니다.
  3. `current_thread`가 `next_thread`가 가리키던 곳을 가리키도록 만듭니다.
  4. `uthread_switch`가 반환될 때 `next_thread`가 실행 상태가 되고, 현재 스레드(`current_thread`)가 됩니다.
- **힌트 (레지스터 저장):** `current_thread->sp`에 `%esp`를 저장하려면 `sp`가 구조체의 오프셋 0에 있으므로 다음처럼 할 수 있습니다.
  ```assembly
  movl current_thread, %eax
  movl %esp, (%eax)
  ```

### 3.3 사용자 수준 스레드 간 타임 쉐어링(Time sharing) 구현
타이머 인터럽트가 발생할 때 사용자 수준 스케줄러가 스레드들을 스케줄링하도록 커널 코드를 수정합니다. (프로세스 스위칭과 크게 다르지 않음)
수정해야 할 파일: `proc.h`, `proc.c`, `trap.c`

1. **사용자 수준 스케줄러 등록 (`proc.h`, `proc.c`)**
   - PCB(`struct proc`)가 사용자 수준 스케줄러 주소를 포함하도록 수정합니다.
     ```c
     struct proc {
         // ... 기존 코드 ...
         uint scheduler; // address of the user-level scheduler
     };
     ```
   - 새로운 시스템 콜 `uthread_init`을 추가합니다.
     ```c
     int uthread_init(int address); // address of the user-level scheduler
     ```

2. **사용자 수준 스레드 스케줄링 (`trap.c`)**
   - 타이머 인터럽트 발생 시, 현재 사용자 프로세스를 위해 스케줄러가 동작하도록 구현합니다.
   - **확인할 사항:**
     - 사용자 프로세스가 여러 스레드를 가지고 있는가?
     - 타이머 인터럽트가 커널 모드에서 발생했는가?
   - `trap.c`의 `case T_IRQ0 + IRQ_TIMER:` 부분에 코드를 추가합니다.

## 4. 테스트 (Test)
- QEMU를 반드시 `CPUS=1`로 설정하여 실행합니다. (예: `make qemu CPUS=1`)
- 구현이 올바르게 되었다면 `$ uthread` 실행 시, 페이지 폴트 에러 없이 두 스레드가 교대로 동작하며 `my thread ...` 메시지를 출력하고 마지막에 아래와 같이 정상 종료되어야 합니다.
  ```
  my thread: exit
  my thread: exit
  thread_schedule: no runnable threads
  ```
- **디버깅 팁:** `gdb`를 사용하여 `thread_switch`에 브레이크포인트를 걸고 레지스터 값(`next_thread->sp` 등)을 확인해보세요.

---

## 5. 제출물 및 보고서 작성 (중요)

### 5.1 구현 소스코드
- **제출 형식:** XV6 소스코드만 `zip`으로 압축하여 제출
- **주의사항:**
  - 바이너리 파일(이미지, `.o` 파일 등)은 제외할 것
  - `Makefile`은 반드시 포함할 것
  - 제출되지 않은 파일은 기본 소스코드에서 변경이 없는 것으로 간주함
  - **컴파일/빌드 오류 및 프로그램이 수행되지 않을 경우 0점 처리**

### 5.2 구현 보고서
반드시 다음 내용을 포함해야 합니다.
1. 과제 수행을 위한 xv6 분석
2. 설계 및 구현 내용
3. **수행 결과 캡쳐 (팀원 당 1개씩 필수):**
   - 팀원의 학번이 확인될 수 있도록 **ID 출력 프로그램**을 먼저 수행하고, 이어서 **테스트 프로그램**을 수행하여 두 출력이 모두 화면에 나오도록 캡쳐하여 제출.
   - **수행 결과 캡쳐 제출이 없는 경우 해당 팀원은 0점 처리됨.**

#### ID 출력 프로그램 예시
```c
#include "types.h" 
#include "user.h" 

int main(int argc, char *argv[]) {
    int id=2026; // 자신의 학번으로 수정
    printf(1, "My ID is %d\n", id);
    exit();
}
```
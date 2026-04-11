# Lab 3: User Level Threads - 질의응답 및 컨텍스트 정리

본 문서는 xv6 운영체제 환경에서 사용자 수준 스레드(User-level threads) 패키지를 구현하는 과정에서 발생한 주요 개념과 질의응답을 정리한 것입니다.

## 1. Makefile 수정 위치
`Makefile`을 수정할 때, 새로 추가할 타겟(`_uthread`)은 반드시 명확한 위치에 작성되어야 합니다.

*   **`_uthread:` 타겟 추가 위치:** `_forktest:` 타겟의 빌드 명령어가 끝나는 **바로 다음 줄**에 추가합니다.
    ```makefile
    _forktest: forktest.o $(ULIB)
    	# forktest has less library code linked in - needs to be small
    	# in order to be able to max out the proc table.
    	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o _forktest forktest.o ulib.o usys.o
    	$(OBJDUMP) -S _forktest > forktest.asm

    _uthread: uthread.o uthread_switch.o
    	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o _uthread uthread.o uthread_switch.o $(ULIB)
    	$(OBJDUMP) -S _uthread > uthread.asm
    ```
    *(주의: 각 명령어 행의 시작은 스페이스(공백)가 아닌 반드시 **Tab(탭) 키**여야 합니다.)*
*   **`UPROGS` 수정:** `Makefile` 상단의 `UPROGS` 변수 목록에 `_uthread\`를 추가하여 시스템 빌드 시 사용자 프로그램으로 포함되도록 합니다.

## 2. uthread_switch.S 의 핵심 역할과 포인터 변경
스레드 문맥 교환(Context Switch)의 핵심은 현재 실행 중인 스레드의 상태를 저장하고, 다음 실행할 스레드의 상태를 복원하는 것입니다. 이 과정에서 포인터 변수의 갱신이 필수적입니다.

*   **역할:**
    1.  `current_thread`의 상태(레지스터)를 스택에 저장합니다.
    2.  `next_thread`의 상태(레지스터)를 스택에서 복원합니다.
    3.  **포인터 주소 변경:** `current_thread` 포인터가 `next_thread`가 가리키는 구조체를 가리키도록 갱신합니다. (즉, 현재 스레드를 다음 스레드로 완전히 교체)
*   어셈블리어 구현 시 메모리 간 직접 복사가 안 되므로 레지스터를 경유해야 합니다.
    ```assembly
    movl next_thread, %eax
    movl %eax, current_thread
    ```

## 3. 스레드 상태 데이터의 저장과 참조 방식
스레드의 상태(레지스터 값들)는 구조체 안에 직접 저장되는 것이 아니라, 각 스레드의 **스택(Stack) 메모리** 영역에 저장됩니다. 포인터 변수는 이 스택의 위치(주소)만을 기억합니다.

1.  **저장 위치:** 문맥 교환이 시작되면 범용 레지스터(`%ebp`, `%ebx` 등)의 값들을 현재 스레드의 스택에 `push` 합니다.
2.  **참조 방식 (저장):** 레지스터를 모두 `push`한 직후의 스택 포인터(`%esp`) 값을 `current_thread->sp`에 저장하여 "내 스택의 최상단 위치"를 기억합니다.
    ```assembly
    movl current_thread, %eax
    movl %esp, (%eax)
    ```
3.  **참조 방식 (복원):** 나중에 이 스레드가 `next_thread`로 선택되면, `next_thread->sp`에 저장된 주소를 다시 CPU의 `%esp`에 덮어씁니다. 그 후 스택에서 `pop` 연산을 통해 레지스터 상태를 고스란히 복원하게 됩니다.

## 4. struct thread 의 구조적 특징
`uthread_switch.S`에서 어셈블리 코드로 스택 포인터(`sp`)에 쉽게 접근할 수 있는 이유는 구조체의 설계 방식 때문입니다.

*   강의 자료 힌트: **"sp is at offset 0 in the struct"**
*   C언어 표현 (예상 구조):
    ```c
    struct thread {
        int sp;                  // 스택 포인터 (offset 0)
        char stack[STACK_SIZE];  // 스레드별 개별 스택 공간
        int state;               // 스레드 상태 (RUNNABLE, RUNNING 등)
    };
    ```
*   **특징 및 이유:** `sp` 변수가 구조체의 맨 첫 번째 멤버(오프셋 0)로 배치되어 있습니다. 이 덕분에 복잡한 주소 계산(오프셋 덧셈 등) 없이, 구조체의 시작 주소(예: `%eax`에 담긴 `current_thread`의 주소)가 곧 `sp` 변수의 주소가 되어 간결한 어셈블리 명령어(`movl %esp, (%eax)`)로 값을 저장하고 읽을 수 있습니다.
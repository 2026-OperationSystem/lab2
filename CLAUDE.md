# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 개요

이 저장소는 xv6edk2 기반의 운영체제 실습 과제입니다. 주요 구현 내용:
- `exit2(int status)` / `wait2(int *status)` 시스템 콜 추가 (부모-자식 프로세스 간 종료 상태 전달)
- 사용자 프로그램 `team1id` 추가
- Lab2: 유저 레벨 스레드 (`uthread1.c`, `uthread_switch.S`) 구현 예정

## 빌드 및 실행

xv6 커널 빌드 및 QEMU 실행은 `xv6edk2/xv6/` 디렉토리에서 수행합니다:

```bash
# xv6 빌드
cd xv6edk2/xv6
make

# QEMU로 실행 (xv6edk2/ 루트에서)
cd xv6edk2
./run.sh
# 또는: qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -drive if=ide,file=fat:rw:image,index=0,media=disk -m 2048 -smp 4 -serial mon:stdio -vga std

# GDB 디버그 실행
cd xv6edk2
./dbgrun.sh
```

툴체인: `i386-jos-elf-gcc` (크로스 컴파일러) 또는 네이티브 `gcc` (elf32-i386 지원 필요)

## 아키텍처

### 시스템 콜 추가 흐름

새 시스템 콜을 추가할 때는 아래 파일들을 순서대로 수정해야 합니다:

1. **`syscall.h`** — 시스템 콜 번호 상수 정의 (`#define SYS_xxx N`)
2. **`syscall.c`** — `syscalls[]` 함수 포인터 배열에 래퍼 함수 등록
3. **`proc.h`** — `struct proc`에 필요한 필드 추가 (예: `int xstate;`)
4. **`proc.c`** — 실제 커널 로직 구현 (`exit2`, `wait2` 등)
5. **`defs.h`** — 다른 커널 파일에서 호출할 수 있도록 함수 원형 선언
6. **`sysproc.c`** — 유저 공간 인자를 `argint`/`argptr`로 추출 후 커널 함수 호출
7. **`user.h`** — 유저 프로그램용 함수 원형 선언
8. **`usys.S`** — `SYSCALL(xxx)` 매크로로 어셈블리 스텁 생성
9. **`Makefile`** — 새 사용자 프로그램은 `UPROGS`에 `_progname` 형태로 추가

### 시스템 콜 실행 흐름 요약

```
유저 프로그램 → usys.S (int $64 트랩) → trap.c → syscall.c (syscalls[] 디스패치)
→ sysproc.c (인자 추출/검증) → proc.c (실제 로직) → struct proc (PCB)
```

### 유저 레벨 스레드 (Lab2)

- **`uthread1.c`** — 스레드 구조체(`struct thread`), 스케줄러(`thread_schedule`), `thread_create` 구현
- **`uthread_switch.S`** — `thread_switch`: 현재 스레드 레지스터 저장 → 다음 스레드 레지스터 복원
- 스레드 스택 레이아웃: 스택 상단에 반환 주소, 그 아래 32바이트 레지스터 저장 공간
- `thread_switch`는 `current_thread->sp` ↔ `next_thread->sp` 간의 컨텍스트 스위치를 수행해야 함

> **[중요] `uthread1.c` 는 절대 수정하지 말 것.**
> 커널 레벨 컨텍스트 스위칭 구현 시에도 `uthread1.c` 의 `struct thread`, `thread_schedule`, `thread_create` 등은 그대로 유지해야 한다.
> 모든 변경은 `uthread_switch.S`, 커널 파일(`syscall.h`, `syscall.c`, `sysproc.c`, `defs.h`)에만 적용한다.

### 주요 커널 자료구조

- **`struct proc`** (`proc.h`): PCB — `sz`, `pgdir`, `tf`(트랩프레임), `context`, `state`, `xstate`(추가됨)
- **`struct context`** (`proc.h`): 커널 컨텍스트 스위치용 레지스터 (`edi`, `esi`, `ebx`, `ebp`, `eip`)
- **`swtch.S`**: 커널 레벨 컨텍스트 스위치 (유저 레벨 `uthread_switch.S`와 구분)

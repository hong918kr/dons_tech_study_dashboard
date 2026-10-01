# Ch.06 제한된 직접 실행 — trap, 시스템 콜, 타이머 인터럽트, 컨텍스트 스위치

> 📖 원문: [06. Mechanism: Limited Direct Execution](../book-md/C06_mechanism_limited_direct_execution.md) · [PDF p.69](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=69) · ⏱️ 읽기 약 60분 (+ 실습 1.5시간) · 🔗 선행: [Ch.04](2026-09-30_C04_process.md), [Ch.05](2026-09-30_C05_process_api.md)

## 0. 한눈에 보기

- CPU 가상화의 기본 기법은 **제한된 직접 실행(Limited Direct Execution, LDE)**: 프로그램을 **CPU 위에서 그대로(직접) 실행**해서 빠르게 하되, 하드웨어를 미리 세팅해서 **할 수 있는 일을 제한**한다.
- 문제 #1 **제한된 연산**: 유저 프로그램이 I/O 같은 위험한 일을 직접 못 하게 하려면? → **유저 모드 / 커널 모드** + **trap**(시스템 콜) + 부팅 때 등록하는 **trap table**. 들어갈 때 하드웨어가 레지스터를 **커널 스택**에 저장하고, 나올 때 **return-from-trap** 으로 복원한다.
- 문제 #2 **프로세스 전환**: 프로세스가 CPU를 쓰는 동안 OS는 돌고 있지 않다. 어떻게 제어권을 되찾나? → 협력형(시스템 콜/yield 기다리기)은 무한 루프에 무력하다. → **타이머 인터럽트**로 강제로 되찾고, **컨텍스트 스위치**로 다른 프로세스의 커널 스택과 레지스터로 갈아탄다.
- 이 Mac(M2)에서 실제로 잰 값: 함수 호출 **2 ns**, 시스템 콜(`getppid`) **약 93 ns**, 파이프 핑퐁 기반 컨텍스트 스위치 **약 1.1 µs**, 유저 공간 `swtch()` **약 10 ns**.
- 원문 CRUX: *"The OS must virtualize the CPU in an efficient manner while retaining control over the system."* — OS는 CPU를 **효율적으로** 가상화하면서도 시스템에 대한 **제어권을 유지**해야 한다. 하위 CRUX: *"How can the OS and hardware work together to perform restricted operations?"*, *"How can the operating system regain control of the CPU so that it can switch between processes?"*, *"How can the OS gain control of the CPU even if processes are not being cooperative?"*

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 직접 실행 (direct execution) | 프로그램을 에뮬레이션 없이 CPU에서 바로 실행 | 네이티브 속도 |
| 제한된 직접 실행 (LDE) | 직접 실행 + 하드웨어로 할 수 있는 일 제한 | 아기 방 안전장치(baby proofing) |
| 유저 모드 (user mode) | 권한이 제한된 CPU 모드 | arm64 EL0, x86 ring 3 |
| 커널 모드 (kernel mode) | 모든 하드웨어에 접근 가능한 CPU 모드 | arm64 EL1, x86 ring 0 |
| 특권 명령 (privileged instruction) | 커널 모드에서만 실행 가능한 명령 | `msr daifset`, `mrs VBAR_EL1`, x86 `cli`, `lidt` |
| trap | 권한을 올리며 커널로 진입하는 명령/사건 | arm64 `svc #0x80`, x86 `syscall` |
| return-from-trap | 권한을 내리며 유저로 복귀하는 명령 | arm64 `eret`, x86 `sysret`/`iret` |
| trap table | 사건별로 실행할 커널 핸들러 주소 표 (부팅 때 등록) | arm64 벡터 테이블 + `VBAR_EL1`, x86 IDT |
| 시스템 콜 번호 (syscall number) | 어떤 시스템 콜인지 나타내는 정수 | macOS arm64: `x16`에 넣음, write = 4 |
| 커널 스택 (kernel stack) | 프로세스마다 하나, trap 때 레지스터를 저장하는 곳 | xv6 `p->kstack` |
| 협력형 스케줄링 (cooperative) | 프로세스가 양보해야만 OS가 제어권을 얻음 | Mac OS 9, `yield()` |
| 타이머 인터럽트 (timer interrupt) | 일정 주기로 하드웨어가 강제로 커널 진입 | arm64 Generic Timer, x86 APIC timer |
| 컨텍스트 스위치 (context switch) | 지금 프로세스의 레지스터를 저장, 다른 것 복원 | xv6 `swtch()` |
| 스케줄러 (scheduler) | 계속 돌릴지, 바꿀지 결정하는 OS 부분 | [Ch.07](2026-09-30_C07_scheduling_intro.md) |
| 인터럽트 비활성화 (disable interrupts) | 인터럽트 처리 중 다른 인터럽트 막기 | arm64 `DAIF.I`, x86 `cli` |
| lmbench | 시스템 콜 · 컨텍스트 스위치 비용 측정 도구 | `lat_syscall`, `lat_ctx` |

## 2. 기본 기법: 직접 실행 (6.1)

CPU를 가상화하는 아이디어는 단순하다. 프로세스 하나를 잠깐 돌리고, 다른 걸 잠깐 돌리고… (시분할). 그런데 만들어 보면 두 가지 도전이 있다.

1. **성능**: 가상화 때문에 오버헤드가 너무 커지면 안 된다.
2. **제어**: 프로세스를 효율적으로 돌리면서도 OS가 **CPU에 대한 제어권**을 쥐고 있어야 한다. 제어권이 없으면 프로세스가 영원히 돌며 기계를 장악하거나, 봐서는 안 될 정보에 접근할 수 있다.

가장 빠른 방법은 당연히 **그냥 CPU에서 직접 실행**하는 것이다. OS가 프로그램을 시작할 때의 절차(원문 Figure 6.1):

```text
OS                                  Program
--------------------------------    -----------------------
Create entry for process list
Allocate memory for program
Load program into memory
Set up stack with argc/argv
Clear registers
Execute call main()
                                    Run main()
                                    Execute return from main
Free memory of process
Remove from process list
```

간단하다. 하지만 이렇게 "제한 없이" 돌리면 두 문제가 생긴다.

- 프로그램이 우리가 원치 않는 일(디스크 아무 데나 쓰기)을 하는 걸 어떻게 막나? 그러면서 빠르게?
- 프로그램이 돌고 있을 때 OS는 어떻게 그것을 멈추고 다른 프로세스로 전환하나?

이 두 질문의 답이 "제한된(limited)" 부분이다. 제한이 없다면 OS는 아무것도 통제하지 못하는 "그냥 라이브러리" 가 된다. 운영체제 지망생에게는 아주 슬픈 일이다.

## 3. 문제 #1: 제한된 연산 (6.2)

직접 실행은 빠르다. 그런데 프로세스가 **제한된 연산**, 예를 들어 디스크 I/O나 CPU · 메모리 같은 자원을 더 얻는 일을 하고 싶다면?

> **CRUX — 제한된 연산을 어떻게 수행하나 (HOW TO PERFORM RESTRICTED OPERATIONS)**
> 프로세스는 I/O 등 제한된 연산을 할 수 있어야 한다. 하지만 시스템 전체에 대한 통제권을 주지 않으면서. OS와 하드웨어는 어떻게 협력하나?

### 3.1 가장 단순한 해법과 그 문제

"아무 프로세스나 I/O를 마음대로 하게 둔다." → 권한 검사를 하는 파일 시스템을 만들 수 없다. 아무 프로세스나 디스크 전체를 읽고 쓸 수 있으면 모든 보호가 사라진다.

### 3.2 해법: 두 개의 모드 + trap

- **유저 모드(user mode)**: 여기서 도는 코드는 할 수 있는 일이 제한된다. 예를 들어 I/O 요청을 직접 내면 프로세서가 **예외(exception)** 를 일으키고, OS가 보통 그 프로세스를 죽인다.
- **커널 모드(kernel mode)**: OS(커널)가 도는 모드. I/O 요청, 모든 종류의 제한된 명령을 실행할 수 있다.

그럼 유저 프로세스가 디스크를 읽고 싶으면? 하드웨어가 **시스템 콜** 기능을 준다. Atlas 같은 옛 기계에서 시작된 이 아이디어로 커널은 파일 시스템 접근, 프로세스 생성/삭제, 프로세스 간 통신, 메모리 할당 같은 핵심 기능을 **신중하게** 노출한다. 대부분의 OS는 수백 개의 시스템 콜을 제공한다(POSIX 표준 참고). 초기 UNIX는 약 20개였다.

시스템 콜을 실행하려면 프로그램이 특별한 **trap 명령어**를 실행한다. 이 명령어는 **동시에** 두 가지를 한다.

1. 커널로 점프한다.
2. 권한 수준을 커널 모드로 올린다.

커널은 필요한 특권 작업을 (허용된다면) 하고, 끝나면 **return-from-trap** 명령어로 호출한 유저 프로그램으로 돌아가며 **동시에** 권한을 유저 모드로 내린다.

> **TIP — 보호된 제어 이전을 써라 (USE PROTECTED CONTROL TRANSFER)**
> 하드웨어는 실행 모드를 여러 개 제공한다. 유저 모드에서 앱은 하드웨어 자원에 완전히 접근할 수 없다. 커널 모드에서 OS는 기계의 모든 자원에 접근한다. 커널로 trap하는 명령, 유저로 return-from-trap하는 명령, 그리고 trap table 위치를 하드웨어에 알려 주는 명령이 제공된다.

### 3.3 trap 때 하드웨어는 무엇을 저장하나

return-from-trap 후 정확히 원래 자리로 돌아가려면 하드웨어가 호출자의 레지스터를 **충분히** 저장해야 한다.

- **x86**: 프로세서가 PC, 플래그, 몇몇 레지스터를 **프로세스별 커널 스택**에 push한다. return-from-trap(`iret`)이 이를 pop해서 유저 프로그램을 재개한다.
- **arm64** (이 노트의 코드를 돌린 M2): `svc` 를 실행하면 하드웨어가 복귀 주소를 **`ELR_EL1`**, 그때의 PSTATE(플래그, 모드)를 **`SPSR_EL1`** 에 저장하고, 스택 포인터를 커널용 **`SP_EL1`** 로 바꾸고, **`VBAR_EL1` + 오프셋**의 벡터로 점프한다. 범용 레지스터(x0~x30)는 하드웨어가 아니라 **커널 진입 코드가 직접** 커널 스택에 저장한다(trap frame). 복귀는 `eret`: `ELR_EL1` 로 점프하며 `SPSR_EL1` 로 모드를 되돌린다.

플랫폼마다 세부는 달라도 개념은 같다: **"어디로 돌아갈지 + 그때의 상태" 를 안전한 곳(커널 쪽)에 저장하고, 나올 때 원자적으로 복원한다.**

### 3.4 trap은 어디로 가나: trap table

중요한 빠진 부분: trap했을 때 OS 안의 **어느 코드**를 실행하나? 호출자가 점프할 주소를 고르게 하면 안 된다. 그러면 프로그램이 커널 아무 곳으로나 점프할 수 있다. 예를 들어 "파일 접근 코드인데 **권한 검사 바로 다음**" 으로 점프하면? 영리한 프로그래머는 이런 능력으로 커널이 임의의 코드 시퀀스를 실행하게 만들 수 있다(return-oriented programming, 원문 [S07]).

그래서 커널은 **부팅 때 trap table**을 세팅한다.

- 부팅 시 기계는 커널 모드에서 시작하므로 하드웨어를 마음대로 설정할 수 있다.
- OS가 가장 먼저 하는 일 중 하나: "이런 예외 사건이 생기면 이 코드를 실행하라" 를 하드웨어에 알려 주기. 하드 디스크 인터럽트, 키보드 인터럽트, 시스템 콜 각각의 **trap handler** 위치.
- 하드웨어는 다음 재부팅까지 이 위치를 기억한다.
- trap table 위치를 알려 주는 명령 자체도 **특권 명령**이다. 유저 모드에서 실행하려 하면? "adios, offending program". 생각해 볼 점: 내 trap table을 설치할 수 있다면 기계를 장악할 수 있을까? (당연히 그렇다. 모든 시스템 콜과 인터럽트가 내 코드로 온다.)

시스템 콜은 주소 대신 **시스템 콜 번호(system-call number)** 로 지정한다. 유저 코드가 원하는 번호를 레지스터나 스택의 정해진 위치에 넣으면, 커널의 trap handler가 번호를 **검사**하고 유효하면 해당 코드를 실행한다. 이 간접 참조가 **보호**의 한 형태다. 유저는 커널 안으로 점프할 곳을 지정할 수 없고, 번호로 서비스를 **요청**할 수만 있다. (원문 v0.91 본문에는 명시가 약하지만 이후 판과 실제 OS의 핵심 개념이다.) §8.1에서 macOS arm64의 규약(`x16` = 번호, `svc #0x80`)으로 직접 trap하고, 없는 번호 9999를 넣으면 커널이 `SIGSYS` 로 프로세스를 죽이는 것까지 본다.

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C06-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="660" height="110" rx="8" fill="none" stroke="currentColor"/>
  <text x="35" y="40" fill="currentColor" font-weight="bold">유저 모드 (EL0)</text>
  <rect x="40" y="55" width="180" height="60" rx="6" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="130" y="78" text-anchor="middle" fill="currentColor">앱: read(fd, buf, n)</text>
  <text x="130" y="98" text-anchor="middle" fill="currentColor" font-size="12">일반 함수 호출 (bl)</text>
  <rect x="260" y="55" width="210" height="60" rx="6" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="365" y="76" text-anchor="middle" fill="currentColor">libc 스텁: mov x16, #3</text>
  <text x="365" y="96" text-anchor="middle" style="fill:var(--accent)" font-weight="bold">svc #0x80  ← trap</text>
  <line x1="220" y1="85" x2="258" y2="85" stroke="currentColor" marker-end="url(#C06-arrow)"/>
  <rect x="510" y="55" width="150" height="60" rx="6" fill="none" stroke="currentColor"/>
  <text x="585" y="78" text-anchor="middle" fill="currentColor">b.lo 성공 → ret</text>
  <text x="585" y="98" text-anchor="middle" fill="currentColor" font-size="12">실패 → cerror(errno)</text>
  <line x1="20" y1="150" x2="680" y2="150" style="stroke:var(--accent)" stroke-width="2" stroke-dasharray="8 4"/>
  <text x="350" y="145" text-anchor="middle" style="fill:var(--accent)" font-size="12">권한 경계: 하드웨어만 넘게 해 준다</text>
  <rect x="20" y="165" width="660" height="150" rx="8" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="35" y="185" fill="currentColor" font-weight="bold">커널 모드 (EL1)</text>
  <path d="M365,115 L365,205" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C06-arrow)"/>
  <text x="372" y="168" fill="currentColor" font-size="11">HW: ELR←PC, SPSR←PSTATE,</text>
  <text x="372" y="182" fill="currentColor" font-size="11">SP←SP_EL1, PC←VBAR+off</text>
  <rect x="40" y="200" width="200" height="100" rx="6" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="140" y="220" text-anchor="middle" fill="currentColor" font-weight="bold">trap table (벡터)</text>
  <text x="140" y="240" text-anchor="middle" fill="currentColor" font-size="12">부팅 때 VBAR_EL1에 등록</text>
  <text x="140" y="258" text-anchor="middle" fill="currentColor" font-size="12">sync(svc/폴트) · IRQ · FIQ</text>
  <text x="140" y="276" text-anchor="middle" fill="#d9534f" font-size="12">등록 명령은 특권 명령</text>
  <rect x="280" y="207" width="200" height="90" rx="6" fill="none" stroke="currentColor"/>
  <text x="380" y="228" text-anchor="middle" fill="currentColor">trap handler</text>
  <text x="380" y="248" text-anchor="middle" fill="currentColor" font-size="12">x0..x30 → 커널 스택(trap frame)</text>
  <text x="380" y="266" text-anchor="middle" fill="currentColor" font-size="12">x16 번호 검사 → sys_read()</text>
  <text x="380" y="284" text-anchor="middle" fill="currentColor" font-size="12">인자(포인터) 검증</text>
  <rect x="510" y="207" width="150" height="90" rx="6" fill="none" stroke="currentColor"/>
  <text x="585" y="235" text-anchor="middle" fill="currentColor">trap frame 복원</text>
  <text x="585" y="260" text-anchor="middle" style="fill:var(--accent)" font-weight="bold">eret</text>
  <text x="585" y="280" text-anchor="middle" fill="currentColor" font-size="11">PC←ELR, PSTATE←SPSR</text>
  <line x1="480" y1="252" x2="508" y2="252" stroke="currentColor" marker-end="url(#C06-arrow)"/>
  <path d="M585,207 L585,117" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C06-arrow)"/>
</svg>
```

> **ASIDE — 시스템 콜은 왜 함수 호출처럼 생겼나**
> `open()`, `read()` 는 C 함수 호출과 똑같이 생겼다. 그럼 시스템은 이게 시스템 콜인 걸 어떻게 아나? 답: **진짜로 함수 호출이다.** 다만 그 함수(C 라이브러리) 안에 trap 명령어가 숨어 있다. 라이브러리는 커널과 약속한 호출 규약대로 인자를 정해진 위치(스택 또는 레지스터)에 두고, 시스템 콜 번호도 정해진 위치에 두고, trap 명령어를 실행한다. trap 뒤의 코드는 리턴값을 풀어서 프로그램에 돌려준다. 그래서 라이브러리의 시스템 콜 부분은 어셈블리로 손으로 작성되어 있다. 누군가 이미 그 어셈블리를 짜 줬으니 우리는 안 짜도 된다. §8.2에서 이 Mac의 libc(`libsystem_kernel.dylib`)에 있는 `read` 와 `getppid` 스텁을 실제로 디스어셈블해 본다: `mov x16, #3` / `svc #0x80` / `b.lo` — 정확히 이 설명대로다.

### 3.5 전체 프로토콜 (원문 Figure 6.2)

시간은 아래로 흐른다. 각 프로세스에는 **커널 스택**이 있어 커널 진입/탈출 때 하드웨어가 레지스터(범용 레지스터, PC)를 여기에 저장하고 복원한다고 가정한다. 원문 그림은 핵심 명령/사건을 굵게 표시하는데(본문은 "특권 명령은 굵게" 라고 하지만 실제로는 유저가 실행하는 `trap` 도 굵게 되어 있다), 여기서는 그 항목들을 `★` 로 표시했다. 진짜 특권 명령은 trap table 초기화, 타이머 시작, return-from-trap이다.

```text
OS @ boot (kernel mode)              Hardware
-----------------------------------  ---------------------------------
★ initialize trap table
                                     remember address of
                                       syscall handler

OS @ run (kernel mode)               Hardware                          Program (user mode)
-----------------------------------  --------------------------------  --------------------
Create entry for process list
Allocate memory for program
Load program into memory
Setup user stack with argv
Fill kernel stack with reg/PC
★ return-from-trap
                                     restore regs from kernel stack
                                     move to user mode
                                     jump to main
                                                                       Run main()
                                                                       ...
                                                                       Call system call
                                                                       ★ trap into OS
                                     save regs to kernel stack
                                     move to kernel mode
                                     jump to trap handler
Handle trap
  Do work of syscall
★ return-from-trap
                                     restore regs from kernel stack
                                     move to user mode
                                     jump to PC after trap
                                                                       ...
                                                                       return from main
                                                                       ★ trap (via exit())
Free memory of process
Remove from process list
```

포인트:

- **두 단계**: (1) 부팅 때 커널이 특권 명령으로 trap table을 초기화하고, CPU가 그 위치를 기억한다. (2) 프로세스를 실행할 때, 커널은 프로세스 리스트 항목과 메모리를 준비한 뒤 **return-from-trap** 으로 프로세스를 시작한다.
- 왜 처음 시작할 때도 "return-from-trap" 인가? 새 프로세스의 커널 스택에 **마치 방금 trap해서 들어온 것처럼** 가짜 레지스터/PC(= `main` 의 시작, 정확히는 C 런타임 진입점)를 채워 두고 return-from-trap하면, 하드웨어가 알아서 유저 모드로 내려가며 거기로 점프한다. 유저 모드로 내려가는 **유일한 문**을 재사용하는 깔끔한 트릭이다. xv6의 `forkret` → `trapret`, Linux의 `ret_from_fork` 가 이것이다.
- `main()` 에서 리턴하면 보통 스텁 코드로 돌아가고, 그 스텁이 `exit()` 시스템 콜로 trap해서 OS가 정리한다.

### 3.6 커널은 인자를 믿지 않는다 (보충)

trap table만으로 보호가 끝나지 않는다. `write(fd, buf, n)` 의 `buf` 가 **커널 메모리 주소**라면? 커널이 아무 검사 없이 거기서 읽으면 유저가 커널 메모리를 파일로 빼낼 수 있다. 그래서 커널은 유저 포인터를 **검증**한다(주소 범위가 유저 영역인지, Linux `copy_from_user` / `access_ok`, XNU `copyin`). 시스템 콜 경계는 **신뢰 경계(trust boundary)** 다.

## 4. 문제 #2: 프로세스 간 전환 (6.3)

프로세스 전환은 간단해 보인다. OS가 하나를 멈추고 다른 걸 시작하면 된다. 그런데 미묘한 문제가 있다. **프로세스가 CPU 위에서 돌고 있다는 건, 정의상 OS는 돌고 있지 않다는 뜻이다.** OS가 돌고 있지 않으면 어떻게 무슨 행동을 하나? (힌트: 못 한다.) 철학적으로 들리지만 진짜 문제다.

> **CRUX — CPU 제어권을 어떻게 되찾나 (HOW TO REGAIN CONTROL OF THE CPU)**
> 프로세스 간 전환을 하려면 OS가 CPU 제어권을 어떻게 되찾아야 하나?

### 4.1 협력형 접근: 시스템 콜을 기다린다

일부 옛 시스템(초기 Macintosh OS, Xerox Alto)은 **협력형(cooperative)** 방식을 썼다. OS는 프로세스가 합리적으로 행동한다고 **믿는다**. 너무 오래 도는 프로세스는 주기적으로 CPU를 양보할 거라고 가정한다.

- 대부분의 프로세스는 생각보다 자주 시스템 콜을 한다(파일 열기, 읽기, 메시지 보내기, 프로세스 생성). 그때마다 OS가 제어권을 얻는다.
- 명시적인 **`yield` 시스템 콜**도 있다. 아무 일도 안 하고 OS에 제어권만 넘긴다.
- 불법 행동을 해도 OS로 trap한다. 0으로 나누기, 접근하면 안 되는 메모리 접근 → trap → OS가 제어권을 얻고 (아마) 그 프로세스를 죽인다.

> **TIP — 앱의 비행에 대처하기**
> 악의든 버그든 하면 안 될 일을 하려는 프로세스를 현대 OS는 그냥 **종료**시킨다. 원 스트라이크 아웃. 가혹해 보이지만, 불법 메모리 접근이나 불법 명령을 실행하면 달리 뭘 하겠나? §8.3에서 유저 모드로 특권 명령을 실행해 `SIGILL` 로 죽는 걸 직접 본다.

수동적인 이 방식의 문제: 프로세스가 (악의든 버그든) **무한 루프에 빠져 시스템 콜을 한 번도 안 하면?** OS는 아무것도 못 한다. 협력형에서 유일한 해결책은 컴퓨터 시스템의 모든 문제에 대한 유서 깊은 해결책, **재부팅**이다.

### 4.2 비협력형 접근: OS가 제어권을 뺏는다 — 타이머 인터럽트

> **CRUX — 협력 없이 제어권을 얻는 법 (HOW TO GAIN CONTROL WITHOUT COOPERATION)**
> 프로세스가 협력하지 않아도 OS가 CPU 제어권을 얻으려면? 악성 프로세스가 기계를 장악하지 못하게 하려면?

답은 단순하고, 오래전에 여러 사람이 발견했다: **타이머 인터럽트(timer interrupt)**.

- 타이머 장치를 몇 밀리초마다 인터럽트를 일으키도록 프로그램한다.
- 인터럽트가 오면 지금 도는 프로세스가 멈추고, OS에 미리 설정된 **인터럽트 핸들러**가 실행된다.
- 이 순간 OS는 CPU 제어권을 되찾았다. 지금 프로세스를 멈추고 다른 걸 시작하는 등 원하는 걸 할 수 있다.

> **TIP — 제어권을 되찾으려면 타이머 인터럽트를 써라**
> 타이머 인터럽트가 있으면 프로세스가 비협력적이어도 OS가 CPU에서 다시 돌 수 있다. 이 하드웨어 기능은 OS가 기계의 제어권을 유지하는 데 **필수**다.

시스템 콜처럼, OS는 부팅 때 두 가지를 해야 한다.

1. 타이머 인터럽트가 오면 실행할 코드를 하드웨어에 알려 준다(trap table에 등록).
2. 타이머를 **시작**한다. 이것도 당연히 특권 명령이다. 타이머가 돌기 시작하면 OS는 "언젠가 반드시 제어권이 돌아온다" 고 안심하고 유저 프로그램을 돌릴 수 있다. 타이머는 끌 수도 있다(역시 특권 명령, 동시성에서 다시 다룬다).

인터럽트가 올 때 하드웨어의 책임: 시스템 콜 trap과 거의 같다. 지금 돌던 프로그램의 상태를 충분히 저장해서(예: 커널 스택에), 나중에 return-from-trap이 프로그램을 정확히 재개할 수 있게 한다.

이 Mac의 커널 설정을 보면 `sysctl kern.clockrate` 가 `hz = 100, tick = 10000` (10 ms) 이다. 다만 현대 XNU/Linux는 고정 주기 틱 대신 필요할 때만 다음 타이머를 거는 **tickless(dynamic tick)** 방식을 쓴다. 이 노트에서 쓴 타이머 카운터 주파수는 `sysctl hw.tbfrequency` = **24,000,000 Hz** (24 MHz, 한 틱 41.67 ns) 이다.

### 4.3 문맥 저장과 복원 (Saving and Restoring Context)

OS가 (시스템 콜이든 타이머 인터럽트든) 제어권을 되찾았다. 이제 결정: **지금 프로세스를 계속 돌릴까, 다른 걸로 바꿀까?** 이 결정은 **스케줄러**의 몫이다([Ch.07](2026-09-30_C07_scheduling_intro.md) ~). 바꾸기로 했다면 OS는 저수준 코드인 **컨텍스트 스위치(context switch)** 를 실행한다.

개념은 단순하다.

- 지금 프로세스의 레지스터 몇 개를 저장한다(예: 그 프로세스의 커널 스택에).
- 곧 실행할 프로세스의 레지스터를 복원한다(그 프로세스의 커널 스택에서).
- 그러면 return-from-trap을 실행할 때, 원래 프로세스로 돌아가는 대신 **다른 프로세스**가 재개된다.

조금 더 정확히: OS는 저수준 어셈블리로 지금 프로세스의 범용 레지스터, PC, **커널 스택 포인터**를 저장하고, 다음 프로세스의 레지스터, PC를 복원하고, 그 프로세스의 **커널 스택으로 전환**한다. 스택을 바꿨기 때문에, 커널은 switch 코드를 **한 프로세스(인터럽트된 것)의 문맥에서 호출**하고 **다른 프로세스(곧 실행될 것)의 문맥에서 리턴**한다. 그 뒤 return-from-trap을 하면 곧 실행될 프로세스가 지금 실행 중인 프로세스가 된다. 컨텍스트 스위치 완료.

원문 Figure 6.3 — 타이머 인터럽트로 A에서 B로 (★ = 원문에서 굵게 표시된 항목):

```text
OS @ boot (kernel mode)              Hardware
-----------------------------------  ---------------------------------
★ initialize trap table
                                     remember addresses of
                                       syscall handler
                                       timer handler
★ start interrupt timer
                                     start timer
                                     interrupt CPU in X ms

OS @ run (kernel mode)               Hardware                          Program (user mode)
-----------------------------------  --------------------------------  --------------------
                                                                       Process A
                                                                       ...
                                     ★ timer interrupt
                                     save regs(A) to k-stack(A)
                                     move to kernel mode
                                     jump to trap handler
Handle the trap
Call switch() routine
  save regs(A) to proc-struct(A)
  restore regs(B) from proc-struct(B)
  switch to k-stack(B)
★ return-from-trap (into B)
                                     restore regs(B) from k-stack(B)
                                     move to user mode
                                     jump to B's PC
                                                                       Process B
                                                                       ...
```

```svg
<svg viewBox="0 0 700 400" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C06-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">두 종류의 저장: 하드웨어(암묵적) vs OS(명시적)</text>
  <rect x="20" y="35" width="140" height="44" rx="6" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="90" y="62" text-anchor="middle" fill="currentColor">Process A (유저)</text>
  <rect x="540" y="35" width="140" height="44" rx="6" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="610" y="62" text-anchor="middle" fill="currentColor">Process B (유저)</text>
  <line x1="20" y1="95" x2="680" y2="95" style="stroke:var(--accent)" stroke-dasharray="8 4"/>
  <text x="350" y="90" text-anchor="middle" style="fill:var(--accent)" font-size="12">유저 / 커널 경계</text>
  <path d="M90,79 L90,128" fill="none" stroke="currentColor" stroke-width="2" marker-end="url(#C06-arrow2)"/>
  <text x="98" y="112" fill="currentColor" font-size="12">① 타이머 인터럽트</text>
  <rect x="20" y="130" width="160" height="110" rx="6" fill="none" stroke="currentColor"/>
  <text x="100" y="150" text-anchor="middle" fill="currentColor" font-weight="bold">k-stack(A)</text>
  <rect x="35" y="160" width="130" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="100" y="176" text-anchor="middle" fill="currentColor" font-size="11">trap frame: A의</text>
  <text x="100" y="189" text-anchor="middle" fill="currentColor" font-size="11">유저 regs + PC</text>
  <text x="100" y="215" text-anchor="middle" fill="currentColor" font-size="11">← 하드웨어/진입 코드가</text>
  <text x="100" y="230" text-anchor="middle" fill="currentColor" font-size="11">암묵적으로 저장</text>
  <rect x="520" y="130" width="160" height="110" rx="6" fill="none" stroke="currentColor"/>
  <text x="600" y="150" text-anchor="middle" fill="currentColor" font-weight="bold">k-stack(B)</text>
  <rect x="535" y="160" width="130" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="600" y="176" text-anchor="middle" fill="currentColor" font-size="11">trap frame: B의</text>
  <text x="600" y="189" text-anchor="middle" fill="currentColor" font-size="11">유저 regs + PC</text>
  <text x="600" y="215" text-anchor="middle" fill="currentColor" font-size="11">(B가 예전에 멈출 때</text>
  <text x="600" y="230" text-anchor="middle" fill="currentColor" font-size="11">저장된 것)</text>
  <rect x="215" y="140" width="270" height="140" rx="8" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="350" y="162" text-anchor="middle" fill="currentColor" font-weight="bold">② switch(): OS가 명시적으로</text>
  <rect x="230" y="175" width="110" height="60" rx="4" fill="none" stroke="currentColor"/>
  <text x="285" y="195" text-anchor="middle" fill="currentColor" font-size="12">proc(A).context</text>
  <text x="285" y="213" text-anchor="middle" fill="currentColor" font-size="11">커널 regs, SP,</text>
  <text x="285" y="227" text-anchor="middle" fill="currentColor" font-size="11">리턴 주소 저장</text>
  <rect x="360" y="175" width="110" height="60" rx="4" fill="none" stroke="currentColor"/>
  <text x="415" y="195" text-anchor="middle" fill="currentColor" font-size="12">proc(B).context</text>
  <text x="415" y="213" text-anchor="middle" fill="currentColor" font-size="11">복원 → SP가</text>
  <text x="415" y="227" text-anchor="middle" fill="currentColor" font-size="11">k-stack(B)로</text>
  <line x1="340" y1="205" x2="358" y2="205" stroke="currentColor" marker-end="url(#C06-arrow2)"/>
  <text x="350" y="265" text-anchor="middle" fill="currentColor" font-size="12">A의 문맥에서 호출 → B의 문맥에서 리턴</text>
  <path d="M180,185 L213,200" fill="none" stroke="currentColor" marker-end="url(#C06-arrow2)"/>
  <path d="M487,200 L518,185" fill="none" stroke="currentColor" marker-end="url(#C06-arrow2)"/>
  <path d="M610,130 L610,81" fill="none" stroke="currentColor" stroke-width="2" marker-end="url(#C06-arrow2)"/>
  <text x="618" y="112" fill="currentColor" font-size="12">③ return-from-trap</text>
  <rect x="20" y="300" width="660" height="85" rx="8" fill="none" stroke="currentColor"/>
  <text x="35" y="322" fill="currentColor" font-weight="bold">정리</text>
  <text x="35" y="344" fill="currentColor" font-size="12">① 인터럽트 시: A의 유저 레지스터 → A의 커널 스택 (하드웨어, 암묵적)</text>
  <text x="35" y="362" fill="currentColor" font-size="12">② 스위치 시: A의 커널 레지스터 → A의 프로세스 구조체 (소프트웨어, 명시적), B 것 복원</text>
  <text x="35" y="380" fill="currentColor" font-size="12">③ 결과: "A에서 막 trap한 상태" → "B에서 막 trap한 상태" 로 바뀌고, B로 돌아간다</text>
</svg>
```

### 4.4 레지스터 저장이 두 번 일어난다

이 프로토콜에서 레지스터 저장/복원은 **두 종류**다.

1. **타이머 인터럽트가 올 때**: 돌던 프로세스의 **유저 레지스터**를 하드웨어가 **암묵적으로** 그 프로세스의 **커널 스택**에 저장한다.
2. **OS가 A에서 B로 바꾸기로 했을 때**: **커널 레지스터**를 소프트웨어(OS)가 **명시적으로** 그 프로세스의 **프로세스 구조체**(PCB) 메모리에 저장한다.

두 번째 동작이 시스템을 "A에서 막 커널로 trap한 것처럼" 에서 "B에서 막 커널로 trap한 것처럼" 으로 바꾼다. 왜 두 번이 필요한가? 첫 번째는 "유저 프로그램이 어디서 멈췄나" 를 저장하고, 두 번째는 "커널이 이 프로세스를 처리하던 중 어디까지 왔나(커널 콜 스택)" 를 저장한다. B를 재개하면 B는 **B가 예전에 멈췄던 커널 코드 지점**에서 이어서 진행해 결국 B의 trap frame으로 return-from-trap한다.

### 4.5 xv6 `swtch()` 한 줄씩 읽기 (원문 Figure 6.4)

```c
 1     # void swtch(struct context **old, struct context *new);
 2     #
 3     # Save current register context in old
 4     # and then load register context from new.
 5     .globl swtch
 6     swtch:
 7       # Save old registers
 8       movl 4(%esp), %eax # put old ptr into eax
 9       popl 0(%eax)        # save the old IP
10       movl %esp, 4(%eax) # and stack
11       movl %ebx, 8(%eax) # and other registers
12       movl %ecx, 12(%eax)
13       movl %edx, 16(%eax)
14       movl %esi, 20(%eax)
15       movl %edi, 24(%eax)
16       movl %ebp, 28(%eax)
18       # Load new registers
19       movl 4(%esp), %eax # put new ptr into eax
20       movl 28(%eax), %ebp # restore other registers
21       movl 24(%eax), %edi
22       movl 20(%eax), %esi
23       movl 16(%eax), %edx
24       movl 12(%eax), %ecx
25       movl 8(%eax), %ebx
26       movl 4(%eax), %esp # stack is switched here
27       pushl 0(%eax)       # return addr put in place
28       ret                 # finally return into new ctxt
```

- 8행: 스택의 첫 인자(old 포인터)를 `eax` 로. (원문 시그니처 주석은 `struct context **old` 지만 이 코드는 `old` 를 `struct context *` 처럼 쓴다. xv6 실제 구현과 책 버전이 조금 다르다. 개념만 보자.)
- 9행: **`popl 0(%eax)`** — 스택 꼭대기의 **리턴 주소**(swtch를 부른 곳 다음)를 pop해서 `context.eip` 에 저장. "이 프로세스를 나중에 재개할 때 돌아올 곳" = 저장된 IP.
- 10~16행: 스택 포인터와 나머지 레지스터를 [Ch.04](2026-09-30_C04_process.md) 의 `struct context` 순서(eip, esp, ebx, ecx, edx, esi, edi, ebp)대로 저장. pop 후의 `esp` 이므로 "swtch를 부르기 전" 스택 상태다.
- 19행: 이제 `4(%esp)` 는 (리턴 주소를 pop했으니 한 칸 당겨져서) 두 번째 인자 **new** 포인터.
- 20~25행: new의 레지스터 복원.
- **26행: `movl 4(%eax), %esp` — 여기서 스택이 바뀐다.** 이 줄 이후로 우리는 B의 커널 스택 위에 있다.
- 27~28행: B의 저장된 IP를 push하고 `ret`. CPU는 **B가 예전에 swtch를 불렀던 곳 바로 다음**으로 "리턴" 한다. A에서 호출한 함수가 B에서 리턴하는 순간이다.

실제로 저장해야 하는 건 **callee-saved 레지스터**뿐이다. `swtch()` 는 보통 C 함수 호출로 불리므로, caller-saved 레지스터는 호출 규약상 호출자가 이미 책임진다. x86-32 cdecl에서 callee-saved는 `ebx, esi, edi, ebp`(+`esp`, `eip`)다. 실제 xv6(x86 버전)의 `swtch.S` 는 그래서 `ecx`, `edx` 를 저장하지 않는다. arm64(AAPCS64)에서는 `x19~x28`, `fp(x29)`, `lr(x30)`, `sp`, 그리고 `d8~d15` 의 하위 64비트다. §8.6에서 이 규칙대로 arm64 `swtch` 를 직접 작성해 돌린다.

## 5. 동시성이 걱정된다면? (6.4)

주의 깊은 독자라면 이렇게 물을 것이다. "시스템 콜 처리 중에 타이머 인터럽트가 오면?" "인터럽트 하나를 처리하는데 또 인터럽트가 오면? 커널에서 처리하기 어렵지 않나?"

맞다. OS는 인터럽트나 trap 처리 중 또 인터럽트가 오는 경우를 신경 써야 한다. 이것이 책의 두 번째 조각 전체(동시성)의 주제다. 맛보기만 하면:

- **인터럽트 처리 중 인터럽트 비활성화**: 인터럽트 하나를 처리하는 동안 다른 인터럽트가 CPU에 전달되지 않게 한다. 단, 너무 오래 끄면 **인터럽트를 잃을 수 있다**(기술 용어로 "나쁘다"). arm64는 예외 진입 시 하드웨어가 자동으로 `DAIF` 마스크를 세팅한다.
- **정교한 락킹**: 커널 내부 자료구조에 대한 동시 접근을 락으로 보호한다. 그래야 커널 안에서 여러 활동이 동시에 진행될 수 있다(멀티프로세서에서 특히). 하지만 락은 복잡하고 찾기 어려운 버그를 낳는다([Ch.28](2026-09-30_C28_locks.md), [Ch.32](2026-09-30_C32_concurrency_bugs.md)).

## 6. 컨텍스트 스위치는 얼마나 걸리나 (ASIDE + 측정)

> **ASIDE — 컨텍스트 스위치에 걸리는 시간**
> lmbench라는 도구가 이런 것을 잰다. 1996년 200 MHz P6 위의 Linux 1.3.37에서 시스템 콜은 약 **4 µs**, 컨텍스트 스위치는 약 **6 µs** 였다. 현대 시스템(2~3 GHz)은 거의 한 자릿수 더 빨라 **1 µs 미만**이다. 단, 모든 OS 동작이 CPU 성능을 따라가지는 않는다. Ousterhout가 지적했듯 많은 OS 동작은 **메모리 집약적**인데, 메모리 대역폭은 프로세서 속도만큼 빨라지지 않았다.

이 Mac(Apple M2, P코어 4 + E코어 4, macOS)에서 원문 숙제대로 직접 잰 결과(§8.4, §8.5, §8.6). 시스템이 한가할 때 세 번 반복해 거의 같은 값이 나왔다.

| 항목 | 측정값 | 비고 |
|---|---|---|
| 일반 함수 호출 | 2.1 ns | 기준선 |
| `gettimeofday()` | 9.7 ns | 커널 진입 없음 (commpage) |
| `getppid()` | 93 ns | 가장 가벼운 시스템 콜 ≈ 순수 trap 왕복 비용 |
| `read(fd, buf, 0)` | 280 ns | fd 조회 + 파일 계층 통과 |
| `write(/dev/null, 1)` | 440 ns | |
| 파이프 write+read (스위치 없음) | 450 ns | 컨텍스트 스위치 측정의 기준선 |
| 파이프 핑퐁 왕복 (프로세스 2개) | 3.05 µs | 스위치 2번 + 파이프 연산 2쌍 |
| → 컨텍스트 스위치 1회 추정 | **약 1.07 µs** | (3045 − 2×450) / 2 |
| 유저 공간 `swtch()` | 9~13 ns | 레지스터 저장/복원만 |

다른 에이전트들이 동시에 컴파일하던 **부하가 높은 시점**(load average 약 12)에 처음 쟀을 때는 `getppid()` 315 ns, 컨텍스트 스위치 추정 2.4~3.0 µs 가 나왔다. 측정값은 시스템 상태에 크게 흔들린다. 그래서 반복 측정 후 **최솟값**을 쓰고, 측정 조건을 함께 적어야 한다.

### 숫자로 읽는 법 (계산)

- **시스템 콜 ÷ 함수 호출**: 93 / 2.1 ≈ **44배**. M2 P코어(약 3.5 GHz)에서 93 ns ≈ 93 × 3.5 ≈ **325 사이클**. trap 진입/탈출, 레지스터 저장/복원, 번호 디스패치, 보안 완화 조치(Spectre류 대응) 등이 포함된다.
- **1996년 대비**: 시스템 콜 4 µs → 0.093 µs 로 약 **43배**, 컨텍스트 스위치 6 µs → 약 1.07 µs 로 약 **5.6배** 빨라졌다. 클럭은 200 MHz → 3.5 GHz 로 약 17배. 시스템 콜은 클럭 이상으로 빨라졌는데 컨텍스트 스위치는 클럭만큼도 못 따라갔다. 원문이 말한 "모든 OS 동작이 CPU 속도를 따라가지는 않는다" 의 실례다. 이 측정에서 스위치 비용에는 다른 코어를 깨우는 비용(아래 주의)과 스케줄러 경로, 캐시/TLB 영향이 섞여 있다.
- **타임 슬라이스 오버헤드** (새 예제): 스위치 1회 1.07 µs, 타임 슬라이스 10 ms라면 오버헤드는 1.07 / 10000 ≈ **0.011%**. 슬라이스를 100 µs로 줄이면 1.07 / 100 ≈ **1.1%**, 10 µs면 **약 11%**. 게다가 직접 비용 외에 **간접 비용**(캐시와 TLB가 다른 프로세스 데이터로 채워져 있어 스위치 직후 느려짐)이 보통 더 크다. 그래서 타임 슬라이스를 마냥 줄일 수 없다([Ch.07](2026-09-30_C07_scheduling_intro.md) 라운드 로빈의 트레이드오프).
- **커널 스위치 ÷ 유저 swtch**: 1070 / 10 ≈ **100배**. 레지스터 저장/복원 자체는 몇 ns다. 비싼 건 trap 왕복, 파이프 · 스케줄러 경로, 주소 공간 전환(페이지 테이블 레지스터 교체), 다른 코어 깨우기다. 이게 유저 공간 스레드(green thread, 코루틴, Go goroutine)가 존재하는 이유다.

> **측정 주의 (원문 숙제의 경고 그대로)**
> - **타이머 정밀도**: `gettimeofday()` 를 연속 두 번 불렀을 때 98% 이상이 차이 **0 µs** 였다. 마이크로초 해상도로는 93 ns짜리 사건을 한 번에 잴 수 없다. 그래서 100만 번 반복해 평균을 낸다. `mach_absolute_time()` 의 틱은 41.67 ns(24 MHz)라 이것도 연속 호출 차이가 0이다. x86의 `rdtsc` 에 해당하는 arm64 명령은 `mrs CNTVCT_EL0` 이고, 유저 모드에서 허용된다(§8.3에서 확인).
> - **CPU 고정(affinity)**: lmbench는 두 프로세스를 **같은 CPU**에 묶어야 순수한 스위치 비용을 잰다. Linux는 `sched_setaffinity()` 가 있지만 **macOS에는 이에 해당하는 공개 API가 없다**(`thread_policy_set` 의 affinity tag는 힌트일 뿐이고 Apple Silicon에서는 지원되지 않는다). 그래서 이 측정은 두 프로세스가 **다른 코어**에 있을 수도 있고, 그 경우 "스위치" 비용에 코어 간 깨우기(IPI)와 캐시 라인 이동이 섞인다. P코어/E코어 중 어디에 놓이는지도 통제하지 못했다.

## 7. 정리 (6.5)

CPU 가상화를 구현하는 핵심 저수준 메커니즘을 봤다. 묶어서 **제한된 직접 실행**이라 부른다. 기본 아이디어: 돌리고 싶은 프로그램을 CPU에서 그냥 돌린다. 단, 먼저 하드웨어를 세팅해서 OS의 도움 없이는 할 수 있는 일을 제한한다.

현실의 비유: **아기 방 안전장치(baby proofing)**. 위험한 물건이 든 캐비닛은 잠그고, 콘센트는 덮는다. 방이 준비되면 아기를 자유롭게 돌아다니게 둔다. 가장 위험한 것들은 이미 막혀 있으니 안심이다.

OS도 CPU를 baby proof한다.

1. 부팅 때 **trap handler를 세팅**하고 **인터럽트 타이머를 시작**한다.
2. 프로세스는 **제한된 모드(유저 모드)** 에서만 돌린다.

그러면 프로세스는 효율적으로 돌고, OS는 프로세스가 특권 연산을 할 때나 CPU를 너무 오래 독점했을 때만 개입한다.

> **TIP — 재부팅은 유용하다**
> 협력형에서 무한 루프의 유일한 해결책이 재부팅이라고 했다. 비웃을 수 있지만, 연구에 따르면 재부팅(또는 소프트웨어 일부를 다시 시작하는 것)은 견고한 시스템을 만드는 데 아주 유용하다(Microreboot, [C+04]). 소프트웨어를 알려진(더 많이 테스트된) 상태로 되돌리고, 새어 나간 자원(메모리 등)을 회수하며, 자동화하기 쉽다. 대규모 인터넷 서비스에서 주기적으로 기계를 재부팅하는 것도 이 이유다.

남은 큰 질문: **지금 어떤 프로세스를 돌려야 하나?** 이것이 스케줄러의 질문이고, 다음 주제다.

## 8. 직접 해보기

모든 C 코드는 macOS 26(Darwin 25.4, Apple M2, Apple clang 21)에서 `cc -Wall -Wextra -O0 -pthread code/<파일>.c -o .work/bin/<이름>` 으로 **경고 0개**로 컴파일했고, 프로젝트 루트에서 `.work/bin/<이름>` 으로 실행했다. 아래 출력은 전부 실제 실행 결과다. arm64 어셈블리를 쓰는 예제(§8.1, 8.3, 8.6, 8.7)는 `#if defined(__APPLE__) && defined(__aarch64__)` 로 감쌌다. 다른 플랫폼에서는 안내 문구만 출력한다.

이 장에는 OSTEP 숙제 시뮬레이터가 없다(측정 숙제다). 대신 숙제가 요구한 **시스템 콜 비용**과 **컨텍스트 스위치 비용**을 §8.4, §8.5에서 잰다.

### 8.1 libc 없이 trap 명령어로 시스템 콜 하기 — [code/C06_raw_svc.c](../code/C06_raw_svc.c)

macOS arm64의 시스템 콜 규약: **`x16` = 시스템 콜 번호**, `x0..x5` = 인자, **`svc #0x80`** 으로 trap. 리턴 시 `x0` = 결과, 에러면 **carry 플래그**가 1이고 `x0` = errno.

```c
// C06_raw_svc.c — libc 없이 "trap 명령어"를 직접 실행해 시스템 콜을 한다 (macOS arm64).
// Darwin arm64 규약: x16 = 시스템 콜 번호, x0..x5 = 인자, 'svc #0x80' 으로 커널 진입.
// 리턴: x0 = 결과, 에러면 carry 플래그가 1이고 x0 = errno.
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(__aarch64__)
static long raw_syscall3(long num, long a0, long a1, long a2, int *err) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x16 __asm__("x16") = num;
    long carry;
    __asm__ volatile(
        "svc #0x80\n"          // ← 이것이 trap 명령어. 여기서 EL0 → EL1(커널)로 넘어간다
        "cset %1, cs\n"        // carry set 이면 에러
        : "+r"(x0), "=r"(carry)
        : "r"(x1), "r"(x2), "r"(x16)
        : "memory", "cc");
    *err = (int)carry;
    return x0;
}

int main(void) {
    int err;
    setvbuf(stdout, NULL, _IONBF, 0);   // 마지막에 SIGSYS로 죽어도 출력이 남도록 버퍼링 끔
    const char *msg = "hello from svc #0x80 (SYS_write = 4)\n";
    long n = raw_syscall3(4, 1, (long)msg, (long)strlen(msg), &err);   // write(1, msg, len)
    printf("write returned %ld, error flag %d\n", n, err);

    long pid = raw_syscall3(20, 0, 0, 0, &err);                        // getpid()
    printf("raw getpid() = %ld, libc getpid() = %d\n", pid, (int)getpid());

    n = raw_syscall3(4, 99, (long)msg, 5, &err);                       // write(99, ...) 잘못된 fd
    printf("write(99, ...) returned %ld, error flag %d (EBADF = 9)\n", n, err);

    printf("now trapping with nonexistent syscall #9999 ...\n");
    n = raw_syscall3(9999, 0, 0, 0, &err);                             // 없는 시스템 콜 번호
    printf("syscall #9999 returned (%ld, %d)\n", n, err);              // 보통 여기 안 옴: SIGSYS
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
```

실행 결과(`.work/bin/C06_raw_svc; echo "exit status = $?"`):

```text
hello from svc #0x80 (SYS_write = 4)
write returned 37, error flag 0
raw getpid() = 65630, libc getpid() = 65630
write(99, ...) returned 9, error flag 1 (EBADF = 9)
now trapping with nonexistent syscall #9999 ...
exit status = 140 (128 + 12 = SIGSYS)
```

- `svc #0x80` 한 줄로 커널에 들어가 `write` 를 했다. libc를 거치지 않아도 trap 명령어 + 약속된 레지스터만 있으면 된다.
- 잘못된 fd에 쓰면 커널은 프로세스를 죽이지 않고 **에러(carry=1, x0=9=EBADF)** 를 돌려준다. libc 스텁은 이걸 보고 `errno = 9`, 리턴 −1 로 바꿔 준다.
- **없는 번호(9999)** 로 trap하면 커널의 trap handler가 번호 검사에서 걸러 `SIGSYS` 를 보내고, 프로세스는 죽는다(종료 코드 128 + 12 = 140). "번호로 요청만 할 수 있고, 커널이 검사한다" 의 실제 모습이다. 마지막 `printf` 는 실행되지 않았다.
- 처음 버전은 stdout 버퍼링 때문에 SIGSYS로 죽을 때 앞의 출력이 전부 사라졌다. 그래서 `setvbuf(stdout, NULL, _IONBF, 0)` 를 넣었다.

### 8.2 libc의 시스템 콜 스텁을 디스어셈블하기 (ASIDE 확인)

lldb로 아무 프로그램이나 `main` 에서 멈춘 뒤 libc 함수를 디스어셈블했다(명령은 아래 첫 블록, 실제 출력은 둘째 블록의 관련 부분):

```text
lldb --batch -o 'b main' -o run \
  -o 'dis -c 8 -s `(void(*)(void))getppid`' \
  -o 'dis -c 10 -s `(void(*)(void))read`' -o kill .work/bin/C04_layout
```


```text
libsystem_kernel.dylib`getppid:
    0x18c75a49c <+0>:  mov    x16, #0x27                ; =39 
    0x18c75a4a0 <+4>:  svc    #0x80
    0x18c75a4a4 <+8>:  b.lo   0x18c75a4c4               ; <+40>
    0x18c75a4a8 <+12>: pacibsp 
    0x18c75a4ac <+16>: stp    x29, x30, [sp, #-0x10]!
    0x18c75a4b0 <+20>: mov    x29, sp
    0x18c75a4b4 <+24>: bl     0x18c7545f4               ; cerror_nocancel
    0x18c75a4b8 <+28>: mov    sp, x29
libsystem_kernel.dylib`read:
    0x18c754914 <+0>:  mov    x16, #0x3                 ; =3 
    0x18c754918 <+4>:  svc    #0x80
    0x18c75491c <+8>:  b.lo   0x18c75493c               ; <+40>
    0x18c754920 <+12>: pacibsp 
    0x18c754924 <+16>: stp    x29, x30, [sp, #-0x10]!
    0x18c754928 <+20>: mov    x29, sp
    0x18c75492c <+24>: bl     0x18c7555d4               ; cerror
    0x18c754930 <+28>: mov    sp, x29
    0x18c754934 <+32>: ldp    x29, x30, [sp], #0x10
    0x18c754938 <+36>: retab  
```

읽는 법:

- `mov x16, #3` → 시스템 콜 번호 3(read). `getppid` 는 39.
- `svc #0x80` → **trap**. 인자(fd, buf, n)는 C 호출 규약상 이미 `x0~x2` 에 있으므로 따로 옮길 필요도 없다.
- `b.lo <+40>` → carry가 0("lower")이면 성공: `<+40>` 의 `ret` 로 바로 리턴.
- 실패면 `cerror` 를 불러 `errno` 를 세팅하고 −1을 리턴. `pacibsp`/`retab` 은 Apple의 포인터 인증(PAC) 명령으로, 리턴 주소 변조를 막는다.

원문 ASIDE의 말("라이브러리 안에 trap 명령이 숨어 있다, 어셈블리로 손으로 작성되어 있다")이 정확히 이 10줄이다.

### 8.3 유저 모드에서 특권 명령 실행하기 — [code/C06_privileged.c](../code/C06_privileged.c)

```c
// C06_privileged.c — 유저 모드(EL0)에서 "제한된 연산"을 시도하면? → CPU 예외 → 커널이 프로세스를 죽인다.
// 각 시도는 fork()한 자식에서 하고, 부모가 wait()로 사인을 확인한다 (OSTEP: "adios, offending program").
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(__aarch64__)
static void read_current_el(void) {      // 현재 예외 레벨 읽기: EL1 이상에서만 허용
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    printf("CurrentEL = %llu\n", (unsigned long long)(el >> 2));
}
static void mask_irqs(void) {            // 인터럽트 끄기 (DAIF.I = 1): 커널 전용
    __asm__ volatile("msr daifset, #2");
}
static void read_vbar(void) {            // 예외 벡터 테이블 주소 = OSTEP의 "trap table" 위치
    uint64_t v;
    __asm__ volatile("mrs %0, VBAR_EL1" : "=r"(v));
    printf("VBAR_EL1 = %#llx\n", (unsigned long long)v);
}
static void touch_kernel_memory(void) {  // 커널 주소 공간(상위 절반) 읽기
    volatile uint64_t *k = (uint64_t *)0xfffffe0000000000ULL;
    printf("%llu\n", (unsigned long long)*k);
}
static void read_cntvct(void) {          // 비교용: 유저에게 허용된 시스템 레지스터 (가상 카운터)
    uint64_t v;
    __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(v));
    printf("    (child) CNTVCT_EL0 = %llu  <- allowed in user mode\n", (unsigned long long)v);
    fflush(stdout);                       // 자식은 _exit()로 끝나므로 직접 flush
}

static void attempt(const char *what, void (*fn)(void)) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        fn();
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st))
        printf("%-34s -> killed by signal %d (%s)\n", what, WTERMSIG(st), strsignal(WTERMSIG(st)));
    else
        printf("%-34s -> exited normally (%d)\n", what, WEXITSTATUS(st));
}

int main(void) {
    attempt("mrs x, CNTVCT_EL0 (user timer)", read_cntvct);
    attempt("mrs x, CurrentEL", read_current_el);
    attempt("msr daifset, #2 (disable IRQ)", mask_irqs);
    attempt("mrs x, VBAR_EL1 (trap table)", read_vbar);
    attempt("load from kernel address", touch_kernel_memory);
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
```

실행 결과:

```text
    (child) CNTVCT_EL0 = 8656379336597  <- allowed in user mode
mrs x, CNTVCT_EL0 (user timer)     -> exited normally (0)
mrs x, CurrentEL                   -> killed by signal 4 (Illegal instruction: 4)
msr daifset, #2 (disable IRQ)      -> killed by signal 4 (Illegal instruction: 4)
mrs x, VBAR_EL1 (trap table)       -> killed by signal 4 (Illegal instruction: 4)
load from kernel address           -> killed by signal 11 (Segmentation fault: 11)
```

- 유저에게 허용된 시스템 레지스터(`CNTVCT_EL0`, 가상 카운터)는 읽힌다. 이름 끝의 `_EL0` 가 "EL0에서 접근 가능" 이라는 뜻이다.
- **인터럽트 끄기**(`msr daifset, #2`), **trap table 위치 읽기**(`VBAR_EL1`), **현재 권한 레벨 읽기**(`CurrentEL`)는 전부 EL0에서 **불법 명령 예외** → 커널로 trap → 커널이 `SIGILL` 을 보내 죽였다. 원문의 "유저 모드에서 trap table 설치 명령을 실행하면? adios, offending program" 을 그대로 재현했다.
- 커널 주소(상위 절반)를 읽으면 MMU가 막아 **페이지 폴트** → `SIGSEGV`.
- 모든 시도를 `fork()` 한 자식에서 했기 때문에 부모는 살아서 결과를 보고할 수 있었다([Ch.05](2026-09-30_C05_process_api.md) 의 `WIFSIGNALED`).

### 8.4 시스템 콜 비용 측정 (원문 숙제) — [code/C06_syscall_cost.c](../code/C06_syscall_cost.c)

```c
// C06_syscall_cost.c — OSTEP 6장 측정 숙제 (1): 타이머 정밀도와 시스템 콜 비용.
// macOS / Apple Silicon 용. 각 측정은 N회 반복을 5번 하고 최솟값(가장 방해 적은 run)을 쓴다.
#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define N 1000000
#define RUNS 5

static uint64_t ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

__attribute__((noinline)) static int plain_function(int x) {
    __asm__ volatile("" ::: "memory");   // 컴파일러가 호출을 없애지 못하게
    return x + 1;
}

typedef void (*bench_fn)(int fd);
static void b_func(int fd)    { (void)plain_function(fd); }
static void b_getppid(int fd) { (void)fd; (void)getppid(); }
static void b_read0(int fd)   { char c; (void)read(fd, &c, 0); }       // 0바이트 read
static void b_write1(int fd)  { (void)write(fd, "x", 1); }             // /dev/null 에 1바이트
static void b_gtod(int fd)    { struct timeval tv; (void)fd; gettimeofday(&tv, NULL); }

static double bench(bench_fn f, int fd) {
    double best = 1e30;
    for (int r = 0; r < RUNS; r++) {
        uint64_t t0 = ns();
        for (int i = 0; i < N; i++) f(fd);
        double per = (double)(ns() - t0) / N;
        if (per < best) best = per;
    }
    return best;
}

int main(void) {
    // 1) 타이머 해상도: 연속 두 번 호출했을 때 0이 아닌 최소 차이
    struct timeval a, b;
    long min_us = 1000000;
    int zero = 0;
    for (int i = 0; i < 100000; i++) {
        gettimeofday(&a, NULL);
        gettimeofday(&b, NULL);
        long d = (b.tv_sec - a.tv_sec) * 1000000L + (b.tv_usec - a.tv_usec);
        if (d == 0) zero++;
        else if (d < min_us) min_us = d;
    }
    printf("gettimeofday: back-to-back diff==0 in %d/100000 pairs, min nonzero = %ld us\n", zero, min_us);

    uint64_t m0 = mach_absolute_time(), m1 = mach_absolute_time();
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    printf("mach_absolute_time: tick = %u/%u ns (%.2f ns), back-to-back diff = %llu ticks\n",
           tb.numer, tb.denom, (double)tb.numer / tb.denom, (unsigned long long)(m1 - m0));
    uint64_t c0 = ns(), c1 = ns();
    printf("clock_gettime_nsec_np(UPTIME_RAW): back-to-back diff = %llu ns\n\n",
           (unsigned long long)(c1 - c0));

    int fd0 = open("/dev/zero", O_RDONLY);
    int fdn = open("/dev/null", O_WRONLY);
    printf("per-call cost (min of %d runs x %d calls):\n", RUNS, N);
    printf("  plain function call      : %7.1f ns\n", bench(b_func, 0));
    printf("  gettimeofday() (commpage): %7.1f ns\n", bench(b_gtod, 0));
    printf("  getppid()      (syscall) : %7.1f ns\n", bench(b_getppid, 0));
    printf("  read(fd, buf, 0)         : %7.1f ns\n", bench(b_read0, fd0));
    printf("  write(/dev/null, 1 byte) : %7.1f ns\n", bench(b_write1, fdn));
    return 0;
}
```

실행 결과(시스템이 한가할 때):

```text
gettimeofday: back-to-back diff==0 in 98295/100000 pairs, min nonzero = 1 us
mach_absolute_time: tick = 125/3 ns (41.67 ns), back-to-back diff = 0 ticks
clock_gettime_nsec_np(UPTIME_RAW): back-to-back diff = 0 ns

per-call cost (min of 5 runs x 1000000 calls):
  plain function call      :     2.1 ns
  gettimeofday() (commpage):     9.7 ns
  getppid()      (syscall) :    93.6 ns
  read(fd, buf, 0)         :   280.6 ns
  write(/dev/null, 1 byte) :   439.8 ns
```

같은 바이너리를 부하가 높을 때(다른 빌드들이 동시에 돌던 시점) 처음 돌린 결과(마지막 5줄):

```text
  plain function call      :     2.2 ns
  gettimeofday() (commpage):    10.3 ns
  getppid()      (syscall) :   315.5 ns
  read(fd, buf, 0)         :   884.8 ns
  write(/dev/null, 1 byte) :  1253.7 ns
```

해석:

- **타이머 정밀도** (숙제의 첫 질문): `gettimeofday()` 는 µs 단위라 연속 호출의 98%가 0 µs 차이. 그래서 100만 번 반복 × 5회, 최솟값. 한 번의 측정 구간이 수십~수백 ms가 되어 µs 해상도로도 충분히 정확하다.
- **`gettimeofday()` 9.7 ns**: 시스템 콜이 아니다. 커널이 모든 프로세스에 읽기 전용으로 매핑해 둔 **commpage** 의 시간 값을 읽어 계산할 뿐이다(Linux의 vDSO와 같은 아이디어). 시간 조회는 너무 자주 불리니까 trap 비용을 아예 없앤 것이다.
- **`getppid()` 93 ns**: 거의 일을 안 하는 시스템 콜 → **trap 왕복 자체의 비용**에 가깝다. 함수 호출의 약 44배.
- `read(…, 0)` 와 `write(/dev/null)` 은 그 위에 fd 테이블 조회, 파일 객체 참조, VFS · 장치 계층, (macOS의) MAC 정책 훅 등이 더해져 3~5배 비싸다.
- 부하가 높을 때는 2~3배 느려졌다. 공유 기계에서의 측정은 반드시 반복 · 최솟값 · 조건 기록.

### 8.5 컨텍스트 스위치 비용 측정 (원문 숙제, lmbench 방식) — [code/C06_ctxsw_cost.c](../code/C06_ctxsw_cost.c)

```c
// C06_ctxsw_cost.c — OSTEP 6장 측정 숙제 (2): lmbench lat_ctx 방식 컨텍스트 스위치 비용.
// 두 프로세스가 파이프 두 개로 1바이트를 핑퐁한다. 한 번 왕복 = 스위치 2번 (+ 파이프 read/write 2쌍).
// 기준선: 한 프로세스가 자기 파이프에 write→read (스위치 없이 파이프 비용만).
// 주의: macOS에는 sched_setaffinity가 없다 → 두 프로세스가 같은 코어에 있다고 보장 못 함.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define N 200000
#define RUNS 5

static uint64_t ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static double baseline(void) {
    int p[2];
    char c = 'x';
    pipe(p);
    double best = 1e30;
    for (int r = 0; r < RUNS; r++) {
        uint64_t t0 = ns();
        for (int i = 0; i < N; i++) {
            if (write(p[1], &c, 1) != 1 || read(p[0], &c, 1) != 1) exit(1);
        }
        double per = (double)(ns() - t0) / N;
        if (per < best) best = per;
    }
    close(p[0]); close(p[1]);
    return best;   // write 1번 + read 1번 (블록 없음)
}

static double pingpong(void) {
    int ab[2], ba[2];   // a→b, b→a
    char c = 'x';
    pipe(ab); pipe(ba);
    pid_t pid = fork();
    if (pid == 0) {     // B: 받으면 돌려준다
        close(ab[1]); close(ba[0]);
        while (read(ab[0], &c, 1) == 1)
            if (write(ba[1], &c, 1) != 1) break;
        _exit(0);
    }
    close(ab[0]); close(ba[1]);
    double best = 1e30;
    for (int r = 0; r < RUNS; r++) {
        uint64_t t0 = ns();
        for (int i = 0; i < N; i++) {
            if (write(ab[1], &c, 1) != 1 || read(ba[0], &c, 1) != 1) exit(1);
        }
        double per = (double)(ns() - t0) / N;
        if (per < best) best = per;
    }
    close(ab[1]);       // B의 read가 EOF를 보고 끝남
    waitpid(pid, NULL, 0);
    close(ba[0]);
    return best;        // 왕복 1회
}

int main(void) {
    double base = baseline();
    double rt = pingpong();
    printf("pipe write+read in one process (no switch): %7.1f ns\n", base);
    printf("ping-pong round trip between 2 processes  : %7.1f ns\n", rt);
    printf("=> per switch estimate (rt - 2*base) / 2  : %7.1f ns\n", (rt - 2 * base) / 2);
    printf("=> naive per switch (rt / 2)              : %7.1f ns\n", rt / 2);
    return 0;
}
```

실행 결과(한가할 때, 세 번 연속 실행 중 첫 번째. 나머지 두 번도 1071 ns, 1073 ns로 거의 같았다):

```text
pipe write+read in one process (no switch):   447.6 ns
ping-pong round trip between 2 processes  :  3056.8 ns
=> per switch estimate (rt - 2*base) / 2  :  1080.8 ns
=> naive per switch (rt / 2)              :  1528.4 ns
```

부하가 높을 때 처음 돌린 결과:

```text
pipe write+read in one process (no switch):   520.1 ns
ping-pong round trip between 2 processes  :  6943.7 ns
=> per switch estimate (rt - 2*base) / 2  :  2951.8 ns
=> naive per switch (rt / 2)              :  3471.9 ns
```

계산 과정(한가할 때 값):

1. 왕복 1회 = A가 write → A가 read에서 Blocked → **스위치** → B가 read 완료, write → B가 read에서 Blocked → **스위치** → A가 read 완료. 즉 스위치 2번 + (write+read) 2쌍.
2. (write+read) 1쌍의 비용은 한 프로세스 안에서 블록 없이 잰 기준선 447.6 ns.
3. 스위치 1회 ≈ (3056.8 − 2 × 447.6) / 2 = (3056.8 − 895.2) / 2 = 2161.6 / 2 ≈ **1081 ns**.

기준선을 빼지 않은 "naive" 값(1528 ns)과 약 450 ns 차이가 난다. lmbench `lat_ctx` 도 이렇게 통신 비용을 빼서 보고한다. 다만 기준선의 read는 **블록되지 않는** read라서, 실제 핑퐁의 "블록 → 깨우기" 경로 비용은 스위치 쪽에 들어간다. 그리고 §6의 주의대로 CPU를 고정하지 못했으니, 이 1.1 µs에는 **다른 코어를 깨우는 비용**도 섞여 있을 수 있다. Linux에서 `taskset -c 0` 으로 같은 코어에 묶어 재 보면 비교가 된다.

### 8.6 xv6 `swtch()` 를 arm64로 옮겨 돌려 보기 — [code/C06_swtch.c](../code/C06_swtch.c)

원문 Figure 6.4의 아이디어를 그대로 arm64로 옮겼다. "커널" 스케줄러 하나와 "프로세스" 둘이 각자 스택을 갖고, `swtch(old, new)` 가 callee-saved 레지스터와 `sp`, `lr` 을 저장/복원해서 다른 흐름으로 "리턴" 한다. 유저 공간에서 도는 **협력형** 버전이다(태스크가 `yield()` 해야 전환).

```c
// C06_swtch.c — xv6 swtch()(OSTEP Figure 6.4)를 arm64로 옮겨 유저 공간에서 돌려 본다.
// "커널"(scheduler) 하나 + "프로세스"(task) 둘. 각 task는 자기 스택을 갖고,
// yield() 하면 swtch()가 레지스터를 저장/복원하고 스택 포인터를 바꿔 다른 흐름으로 '리턴'한다.
// (협력형: task가 스스로 yield해야만 전환된다 — 타이머 인터럽트 버전은 C06_timer_preempt.c)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(__APPLE__) && defined(__aarch64__)
// AAPCS64에서 함수 호출을 건너 살아남아야 하는(callee-saved) 레지스터만 저장하면 된다.
// caller-saved(x0~x18)는 swtch()를 "부른 쪽"이 이미 책임진다 — xv6가 eip/esp/ebx/esi/edi/ebp만 저장하는 이유와 같다.
struct context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp;   // x29
    uint64_t lr;   // x30: swtch가 ret 할 주소 (xv6의 eip 역할)
    uint64_t sp;   // 스택 포인터 (xv6의 esp)
    double d8, d9, d10, d11, d12, d13, d14, d15;
};

void swtch(struct context *old, struct context *new);
__asm__(
    ".text\n.globl _swtch\n.p2align 2\n"
    "_swtch:\n"
    // ---- old 에 현재 레지스터 저장 ----
    "  stp x19, x20, [x0, #0]\n"
    "  stp x21, x22, [x0, #16]\n"
    "  stp x23, x24, [x0, #32]\n"
    "  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n"
    "  stp x29, x30, [x0, #80]\n"
    "  mov x9, sp\n"
    "  str x9,       [x0, #96]\n"
    "  stp d8,  d9,  [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    // ---- new 에서 레지스터 복원 ----
    "  ldp x19, x20, [x1, #0]\n"
    "  ldp x21, x22, [x1, #16]\n"
    "  ldp x23, x24, [x1, #32]\n"
    "  ldp x25, x26, [x1, #48]\n"
    "  ldp x27, x28, [x1, #64]\n"
    "  ldp x29, x30, [x1, #80]\n"
    "  ldr x9,       [x1, #96]\n"
    "  mov sp, x9\n"                 // ← 스택이 여기서 바뀐다 (xv6: movl 4(%eax), %esp)
    "  ldp d8,  d9,  [x1, #104]\n"
    "  ldp d10, d11, [x1, #120]\n"
    "  ldp d12, d13, [x1, #136]\n"
    "  ldp d14, d15, [x1, #152]\n"
    "  ret\n");                      // ← new->lr 로 '리턴' = 다른 흐름으로 점프

enum state { RUNNABLE, ZOMBIE };
struct proc {
    const char *name;
    enum state state;
    struct context ctx;
    void (*fn)(void);
    char *stack;
};

#define STACK_SIZE (64 * 1024)
static struct context sched_ctx;     // "커널" 스케줄러의 문맥
static struct proc procs[2];
static struct proc *current;
static int verbose = 1;
static long switches;

static void yield(void) {            // 프로세스 → 스케줄러
    swtch(&current->ctx, &sched_ctx);
}

static void proc_entry(void) {       // 새 프로세스가 처음 '리턴'해 들어오는 곳
    current->fn();
    current->state = ZOMBIE;
    swtch(&current->ctx, &sched_ctx);    // 다시는 돌아오지 않는다
}

static void taskA(void) {
    for (int i = 0; i < 3; i++) {
        int local = i * 10;              // 스택 지역 변수가 전환 후에도 살아있는지 확인
        if (verbose) printf("  A: step %d (local=%d, &local=%p)\n", i, local, (void *)&local);
        yield();
    }
}
static void taskB(void) {
    for (int i = 0; i < 3; i++) {
        double f = 1.5 * i;              // d8~d15 저장도 확인
        if (verbose) printf("  B: step %d (f=%.1f, &f=%p)\n", i, f, (void *)&f);
        yield();
    }
}
static long spin_count;
static void spinner(void) {
    for (;;) { spin_count++; yield(); }
}

static void proc_init(struct proc *p, const char *name, void (*fn)(void)) {
    p->name = name;
    p->fn = fn;
    p->state = RUNNABLE;
    p->stack = malloc(STACK_SIZE);
    uintptr_t top = ((uintptr_t)p->stack + STACK_SIZE) & ~(uintptr_t)15;  // 16바이트 정렬
    p->ctx = (struct context){0};
    p->ctx.sp = top;
    p->ctx.lr = (uint64_t)proc_entry;    // 첫 swtch의 ret이 여기로 간다 (xv6의 forkret 역할)
}

static void scheduler(long max_switches) {  // xv6 scheduler()와 같은 모양
    for (switches = 0; switches < max_switches;) {
        int ran = 0;
        for (int i = 0; i < 2; i++) {
            if (procs[i].state != RUNNABLE) continue;
            current = &procs[i];
            swtch(&sched_ctx, &current->ctx);   // 커널 → 프로세스
            switches += 2;                      // 갔다 오기 = 스위치 2번
            ran = 1;
        }
        if (!ran) break;
    }
}

int main(void) {
    printf("[scheduler] sched_ctx (global) at %p\n", (void *)&sched_ctx);
    proc_init(&procs[0], "A", taskA);
    proc_init(&procs[1], "B", taskB);
    printf("[scheduler] A stack top %p, B stack top %p\n",
           (void *)procs[0].ctx.sp, (void *)procs[1].ctx.sp);
    scheduler(1000);
    printf("[scheduler] all tasks ZOMBIE after %ld switches\n\n", switches);

    // 유저 공간 swtch 비용 측정
    verbose = 0;
    proc_init(&procs[0], "S1", spinner);
    proc_init(&procs[1], "S2", spinner);
    long n = 10 * 1000 * 1000;
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    scheduler(n);
    uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    printf("user-level swtch: %ld switches in %.3f ms -> %.2f ns per switch\n",
           switches, (t1 - t0) / 1e6, (double)(t1 - t0) / switches);
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
```

실행 결과:

```text
[scheduler] sched_ctx (global) at 0x100b64008
[scheduler] A stack top 0xc16810000, B stack top 0xc16820000
  A: step 0 (local=0, &local=0xc1680ffc8)
  B: step 0 (f=0.0, &f=0xc1681ffc0)
  A: step 1 (local=10, &local=0xc1680ffc8)
  B: step 1 (f=1.5, &f=0xc1681ffc0)
  A: step 2 (local=20, &local=0xc1680ffc8)
  B: step 2 (f=3.0, &f=0xc1681ffc0)
[scheduler] all tasks ZOMBIE after 16 switches

user-level swtch: 10000000 switches in 126.094 ms -> 12.61 ns per switch
```

xv6와의 대응:

| 이 코드 | xv6 (원문) | 의미 |
|---|---|---|
| `struct context` 의 `lr` | `eip` | 재개할 때 돌아갈 곳 |
| `struct context` 의 `sp` | `esp` | 그 프로세스의 스택 |
| `x19~x28`, `fp`, `d8~d15` | `ebx, esi, edi, ebp` (+ 원문은 ecx, edx도) | callee-saved 레지스터 |
| `mov sp, x9` (복원 중) | `movl 4(%eax), %esp` | **여기서 스택이 바뀐다** |
| `ret` (x30으로 점프) | `pushl 0(%eax); ret` | 새 문맥으로 "리턴" |
| `proc_init` 에서 `ctx.lr = proc_entry` | `forkret` | 새 프로세스의 첫 swtch가 리턴할 곳 |
| `scheduler()` 루프 | xv6 `scheduler()` | RUNNABLE 하나 골라 swtch |

관찰:

- A의 `&local` 은 매번 `0xc1680ffc8`, B의 `&f` 는 `0xc1681ffc0`. **각자 자기 스택**에서 돌고, 전환 후에도 지역 변수 값(0, 10, 20 / 0.0, 1.5, 3.0)이 정확히 살아 있다. 레지스터 + 스택 포인터만 저장/복원하면 "실행 흐름" 전체가 보존된다는 증거다.
- 16번 = 태스크마다 4번 방문(3번 yield + 마지막 ZOMBIE 전환) × 2 태스크 × 왕복 2.
- 유저 공간 swtch는 **약 10 ns** (여러 번 실행해 9.2~12.6 ns). 커널 컨텍스트 스위치(약 1.1 µs)의 1% 수준이다. 즉 커널 스위치 비용의 대부분은 레지스터 저장이 아니라 **trap, 스케줄러, 주소 공간 전환, 캐시 효과**다.

### 8.7 타이머 인터럽트로 제어권 되찾기 (선점형) — [code/C06_timer_preempt.c](../code/C06_timer_preempt.c)

§8.6은 협력형이었다. 이번엔 두 "프로세스" 가 **절대 yield 하지 않는 무한 루프**다. 협력형이었다면 A가 CPU를 영원히 차지한다. 원문 Figure 6.3의 흐름을 유저 공간에서 흉내 낸다.

- 하드웨어 타이머 인터럽트 ≈ `setitimer()` 가 10 ms마다 보내는 `SIGALRM`
- 부팅 때 trap table 등록 ≈ `sigaction()` 으로 핸들러 등록
- 인터럽트 핸들러 → `switch()` ≈ 시그널 핸들러 안에서 `swtch()` 로 스케줄러에 복귀
- return-from-trap into B ≈ 스케줄러가 B의 문맥으로 `swtch()`, B는 자기가 예전에 멈췄던 핸들러에서 리턴해 루프를 이어감

```c
// C06_timer_preempt.c — "타이머 인터럽트로 제어권 되찾기"를 유저 공간에서 흉내 낸다.
//   하드웨어 타이머 인터럽트  ≈ setitimer() 가 주기적으로 보내는 SIGALRM
//   OS의 trap handler         ≈ 시그널 핸들러 on_tick()
//   switch() 루틴             ≈ swtch() (arm64 어셈블리, C06_swtch.c 와 동일)
// 두 '프로세스'는 절대 yield 하지 않는 무한 루프다. 협력형이었다면 A가 CPU를 영원히 독점한다.
// 교육용 데모: 시그널 핸들러 안에서 스택을 바꾸는 것은 POSIX가 보장하지 않는다(macOS arm64에서 동작 확인).
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(__aarch64__)
struct context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, fp, lr, sp;
    double d8, d9, d10, d11, d12, d13, d14, d15;
};
void swtch(struct context *old, struct context *new);
__asm__(".text\n.globl _swtch\n.p2align 2\n_swtch:\n"
        "  stp x19, x20, [x0, #0]\n  stp x21, x22, [x0, #16]\n  stp x23, x24, [x0, #32]\n"
        "  stp x25, x26, [x0, #48]\n  stp x27, x28, [x0, #64]\n  stp x29, x30, [x0, #80]\n"
        "  mov x9, sp\n  str x9, [x0, #96]\n"
        "  stp d8, d9, [x0, #104]\n  stp d10, d11, [x0, #120]\n"
        "  stp d12, d13, [x0, #136]\n  stp d14, d15, [x0, #152]\n"
        "  ldp x19, x20, [x1, #0]\n  ldp x21, x22, [x1, #16]\n  ldp x23, x24, [x1, #32]\n"
        "  ldp x25, x26, [x1, #48]\n  ldp x27, x28, [x1, #64]\n  ldp x29, x30, [x1, #80]\n"
        "  ldr x9, [x1, #96]\n  mov sp, x9\n"
        "  ldp d8, d9, [x1, #104]\n  ldp d10, d11, [x1, #120]\n"
        "  ldp d12, d13, [x1, #136]\n  ldp d14, d15, [x1, #152]\n"
        "  ret\n");

#define NPROC 2
#define STACK_SIZE (256 * 1024)
#define TICK_MS 10
#define MAX_TICKS 12

struct proc { char name; volatile uint64_t work; struct context ctx; char *stack; };
static struct proc procs[NPROC];
static struct context sched_ctx;
static int cur = -1;
static volatile int ticks;
static char trace[MAX_TICKS + 1];

// "타이머 인터럽트 핸들러": 하드웨어가 레지스터를 저장해 주는 대신 커널이 시그널 프레임을 이미 만들어 줬다.
static void on_tick(int sig) {
    (void)sig;
    ticks++;
    if (cur >= 0)
        swtch(&procs[cur].ctx, &sched_ctx);   // A의 문맥 저장 → 스케줄러로
    // 나중에 이 프로세스가 다시 선택되면 여기서부터 이어서 핸들러를 리턴 = "return-from-trap"
}

static void spin_forever(void) {             // 절대 yield 하지 않는 악성(?) 프로세스
    for (;;) procs[cur].work++;
}
static void entry(void) { spin_forever(); }

int main(void) {
    for (int i = 0; i < NPROC; i++) {
        procs[i].name = 'A' + i;
        procs[i].stack = malloc(STACK_SIZE);
        memset(&procs[i].ctx, 0, sizeof procs[i].ctx);
        procs[i].ctx.sp = ((uintptr_t)procs[i].stack + STACK_SIZE) & ~(uintptr_t)15;
        procs[i].ctx.lr = (uint64_t)entry;
    }
    // "부팅 시": trap table(=시그널 핸들러) 등록, 타이머 시작
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tick;
    sa.sa_flags = SA_NODEFER;                // 새로 시작하는 프로세스가 SIGALRM 막힌 채로 돌지 않게
    sigaction(SIGALRM, &sa, NULL);
    struct itimerval it = { {0, TICK_MS * 1000}, {0, TICK_MS * 1000} };
    setitimer(ITIMER_REAL, &it, NULL);

    // 스케줄러: 라운드 로빈. 타이머가 프로세스를 끊을 때마다 여기로 돌아온다.
    int next = 0;
    while (ticks < MAX_TICKS) {
        cur = next;
        swtch(&sched_ctx, &procs[cur].ctx);  // "return-from-trap into B"
        trace[ticks - 1] = procs[cur].name;  // 이 tick 동안 누가 돌았나
        cur = -1;
        next = (next + 1) % NPROC;
    }
    struct itimerval off = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &off, NULL);

    printf("timer tick = %d ms, %d ticks\n", TICK_MS, ticks);
    printf("who ran in each tick: %s\n", trace);
    for (int i = 0; i < NPROC; i++)
        printf("proc %c: work = %llu loop iterations (never called yield)\n",
               procs[i].name, (unsigned long long)procs[i].work);
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
```

실행 결과(세 번 실행, 모두 같은 패턴):

```text
timer tick = 10 ms, 12 ticks
who ran in each tick: ABABABABABAB
proc A: work = 23882057 loop iterations (never called yield)
proc B: work = 21278421 loop iterations (never called yield)
```

```text
timer tick = 10 ms, 12 ticks
who ran in each tick: ABABABABABAB
proc A: work = 22240591 loop iterations (never called yield)
proc B: work = 23019024 loop iterations (never called yield)
```

```text
timer tick = 10 ms, 12 ticks
who ran in each tick: ABABABABABAB
proc A: work = 24524754 loop iterations (never called yield)
proc B: work = 20993786 loop iterations (never called yield)
```

- A와 B는 `for (;;) work++` 만 하고 한 번도 양보하지 않았는데도 10 ms마다 번갈아 돌았다(`ABAB…`). **제어권은 타이머가 되찾아 준다.**
- 각자 약 2,100만~2,400만 번 반복 = 6틱(60 ms) 동안. 거의 공평하게 나눠 가졌다(라운드 로빈 정책).
- 핸들러 안에서 `swtch()` 를 부르면, A의 "인터럽트된 상태"(시그널 프레임)는 **A의 스택**에 그대로 남는다. 나중에 A가 다시 선택되면 그 핸들러에서 리턴하고, 커널이 시그널 프레임으로 레지스터를 복원해 루프 한가운데로 돌아간다. 원문의 "A의 레지스터는 A의 커널 스택에(①), 커널 문맥은 proc 구조체에(②)" 와 같은 두 단계 저장이다.
- 주의: 시그널 핸들러 안에서 스택을 바꾸는 것은 POSIX가 보장하는 동작이 아니다. 교육용 데모이고 macOS arm64에서 동작을 확인했다. 진짜 커널은 이걸 하드웨어 예외 진입 + 커널 스택으로 한다.

## 9. 펌웨어 엔지니어의 눈으로

- **Cortex-M RTOS의 컨텍스트 스위치가 이 장의 축소판이다.** SysTick(타이머 인터럽트) → 스케줄러가 다음 태스크 결정 → **PendSV** 예외에서 스위치. Cortex-M은 예외 진입 시 하드웨어가 `r0-r3, r12, lr, pc, xPSR` 를 **자동으로 스택에 push**(원문 ①의 "하드웨어가 암묵적으로 저장")하고, PendSV 핸들러가 `r4-r11` 을 **소프트웨어로 저장**한 뒤 PSP를 다음 태스크 것으로 바꾼다(원문 ②, `swtch` 의 `movl ..., %esp`). FreeRTOS `xPortPendSVHandler` 를 읽어 보면 xv6 `swtch` 와 줄 단위로 대응된다. 면접에서 "x86 xv6 / arm64 Linux / Cortex-M FreeRTOS 의 스위치 비교" 를 말할 수 있으면 강하다.
- **SVC = 시스템 콜, 그리고 특권 분리.** Cortex-M도 `SVC` 명령과 Thread/Handler 모드, privileged/unprivileged가 있다. MPU를 켜고 태스크를 unprivileged로 돌리면 이 장의 "유저 모드" 와 같은 보호가 생긴다(FreeRTOS-MPU). SSD 컨트롤러 FW는 보통 전부 privileged로 돌아서, 버그 하나가 FTL 메타데이터를 덮어쓸 수 있었다. LDE의 "제한" 이 없는 상태다.
- **인터럽트 vs 폴링, 그리고 "끄는 시간"**. 원문 6.4의 "인터럽트를 너무 오래 끄면 인터럽트를 잃는다" 는 FW에서 매일 겪는 문제다: 크리티컬 섹션에서 IRQ를 끈 시간이 NVMe doorbell이나 NAND ready 신호 처리를 늦춘다. 고성능 NVMe 드라이버(SPDK)나 컨트롤러 FW가 **폴링**을 쓰는 이유도 이 장의 숫자로 설명된다: trap/인터럽트 진입이 수백 ns~µs인데 NVMe 명령 완료가 수 µs 단위면 오버헤드 비율이 크다.
- **trap table = 벡터 테이블, 그리고 그걸 지키는 일.** 부트 ROM/부트로더가 `VBAR_EL1`(Cortex-M은 `VTOR`)을 세팅하는 순간이 "OS @ boot: initialize trap table" 이다. 보안 부팅에서 벡터 테이블이 있는 메모리를 쓰기 금지로 잠그는 이유가 원문의 질문("내 trap table을 설치할 수 있다면?")에 대한 답이다. Apple SoC는 커널 텍스트와 페이지 테이블 영역을 하드웨어로 잠그는 장치(KTRR/CTRR 계열)까지 둔다.
- **AI 가속기 = 직접 실행 + 제한, 그리고 선점의 어려움.** GPU/NPU도 유저 프로세스가 커맨드 버퍼를 **직접** 장치에 넣는다(유저 모드 submission, doorbell을 유저 공간에 매핑). 제한은 IOMMU(Apple DART)와 장치 페이지 테이블이 건다: 이 프로세스의 버퍼만 DMA 가능. 그리고 "타이머로 제어권 되찾기" 의 가속기 버전이 **compute preemption**: 큰 커널(GPU kernel)이 끝날 때까지 기다리지 않고 중간에 상태(레지스터 파일, 공유 메모리 수백 KB)를 저장하고 다른 컨텍스트로 바꾸는 것. 저장할 상태가 CPU보다 훨씬 커서 비싸고, 그래서 타임 슬라이스도 길다. 이 장의 오버헤드 계산(§6)을 그대로 적용해 볼 수 있다.

## 10. 면접 질문

### Q1. 시스템 콜이 실행될 때 하드웨어와 OS가 하는 일을 순서대로 설명하라 (arm64 또는 x86).
<details>
<summary>답 보기</summary>

(arm64, macOS 기준) 1) libc 스텁이 인자를 `x0..x5`, 번호를 `x16` 에 두고 **`svc #0x80`** 실행. 2) **하드웨어**: 복귀 주소 → `ELR_EL1`, PSTATE → `SPSR_EL1`, 예외 레벨 EL0 → **EL1**, `SP_EL1` 사용, 인터럽트 마스크, **`VBAR_EL1` + 오프셋**(sync 예외 벡터)으로 점프. 3) **커널 진입 코드**: 범용 레지스터를 **커널 스택의 trap frame**에 저장, 예외 원인(`ESR_EL1`)이 SVC인지 확인, **번호 범위 검사** 후 시스템 콜 테이블로 디스패치, **유저 포인터 검증/복사**. 4) 결과를 trap frame의 x0에(에러면 carry 세팅). 5) trap frame 복원 후 **`eret`**: `ELR_EL1` 로 점프하며 `SPSR_EL1` 로 EL0 복귀. x86-64는 `syscall`(MSR_LSTAR에 등록된 진입점, RCX/R11에 RIP/RFLAGS 저장) → `sysretq`.

</details>

### Q2. 협력형 스케줄링과 선점형 스케줄링의 차이는? 선점형에 필요한 하드웨어는?
<details>
<summary>답 보기</summary>

**협력형**: OS는 프로세스가 시스템 콜, `yield`, 또는 예외(불법 동작)를 일으킬 때만 제어권을 얻는다. 무한 루프 하나가 시스템을 멈출 수 있다(Mac OS 9, 초기 Windows 3.x). **선점형**: **타이머 인터럽트**가 주기적으로 강제로 커널에 진입시켜, OS가 언제든 현재 프로세스를 멈추고 다른 것으로 바꿀 수 있다. 필요한 하드웨어: (1) **프로그래머블 타이머 + 인터럽트**, (2) 인터럽트 시 상태를 저장하고 정해진 핸들러로 가는 **예외/trap 메커니즘**, (3) 타이머 설정과 인터럽트 마스킹을 **특권 명령**으로 만들어 유저가 끄지 못하게 하는 **모드 분리**. RTOS에서도 SysTick 기반 선점이 일반적이지만, 협력형 태스크(run-to-completion)를 의도적으로 쓰기도 한다(지연 예측성).

</details>

### Q3. 컨텍스트 스위치 때 무엇을 저장하나? 왜 callee-saved 레지스터만 저장해도 되나?
<details>
<summary>답 보기</summary>

두 단계: (1) 인터럽트/trap 진입 시 **유저 레지스터 전체 + PC + 상태 레지스터**를 그 프로세스의 **커널 스택(trap frame)** 에 저장. (2) `switch()` 에서 **커널 문맥**(callee-saved 레지스터, 커널 SP, 리턴 주소)을 PCB의 context에 저장하고 다음 프로세스 것을 복원, **커널 스택 전환**. 주소 공간이 다르면 **페이지 테이블 베이스**(arm64 `TTBR0_EL1`, x86 `CR3`)도 바꾸고 ASID/PCID로 TLB 플러시를 피한다. FPU/SIMD 레지스터는 lazy하게 저장하기도 한다. `switch()` 는 **C 함수로 호출되므로** 호출 규약상 caller-saved 레지스터는 호출자가 이미 필요하면 저장해 둔다. 그래서 swtch는 callee-saved(arm64 x19~x29, x30, sp, d8~d15)만 지키면 된다. 유저 레지스터 전체는 이미 ①에서 trap frame에 있다.

</details>

### Q4. trap table(벡터 테이블)을 유저가 바꿀 수 있다면 무슨 일이 생기나? 시스템 콜 번호 방식은 왜 안전한가?
<details>
<summary>답 보기</summary>

모든 시스템 콜, 인터럽트, 폴트가 **공격자 코드로, 커널 권한으로** 실행된다. 즉 기계 장악. 그래서 trap table 설치(`msr VBAR_EL1`, x86 `lidt`)는 **특권 명령**이고, 보안 부팅 시스템은 벡터 테이블과 커널 텍스트를 쓰기 금지로 잠근다. 시스템 콜 번호 방식은 유저가 **커널 안의 점프 대상 주소를 지정할 수 없고** 번호로 서비스를 **요청**만 하게 한다. 커널은 번호를 범위 검사하고 고정된 테이블로만 디스패치하므로, "권한 검사 바로 다음" 같은 곳으로 뛰어드는 공격이 불가능하다. 추가로 인자(특히 포인터)를 **신뢰하지 않고 검증**해야 한다(`copyin`, `copy_from_user`).

</details>

### Q5. 컨텍스트 스위치 비용을 어떻게 측정하겠나? 측정 시 함정은?
<details>
<summary>답 보기</summary>

lmbench 방식: 두 프로세스를 **파이프 두 개로 핑퐁**시켜 N회 왕복 시간을 잰다. 왕복 1회 = 스위치 2회 + 통신 비용 2회. **통신 비용 기준선**(한 프로세스 안에서 write+read)을 따로 재서 빼고 2로 나눈다. 함정: (1) **타이머 해상도** — gettimeofday는 µs라 반복 횟수를 충분히, 가능하면 고해상도 카운터(`rdtsc`, `CNTVCT_EL0`, `clock_gettime`). (2) **CPU 고정** — 두 프로세스가 다른 코어에 있으면 IPI와 캐시 이동이 섞인다. Linux `sched_setaffinity`/`taskset` (macOS는 공개 API 없음). (3) **간접 비용** — 캐시/TLB 오염은 작업 집합 크기에 따라 다르다(lmbench는 작업 집합 크기를 바꿔 가며 잰다). (4) **노이즈** — 반복 후 최솟값/중앙값, 부하 · 주파수 스케일링 · P/E 코어 조건 기록. 이 Mac에서 직접 재니 약 1.1 µs, 부하 시 약 3 µs였다.

</details>

### Q6. 시스템 콜 처리 중에 타이머 인터럽트가 오면? 인터럽트 핸들러 중 또 인터럽트가 오면?
<details>
<summary>답 보기</summary>

**시스템 콜 중 타이머**: 커널 모드에서도 인터럽트는 보통 켜져 있으므로 들어온다. 선점 가능 커널(Linux `CONFIG_PREEMPT`)이면 커널 코드 중간에도 스위치할 수 있고, 아니면 "need_resched" 플래그만 세우고 시스템 콜이 유저로 돌아가기 직전에 스위치한다. 커널이 공유 자료구조를 만지는 구간은 **락**이나 **선점 비활성화**로 보호한다. **인터럽트 중 인터럽트**: 간단한 방법은 핸들러 동안 **인터럽트 마스킹**(arm64는 예외 진입 시 자동으로 DAIF 세팅). 우선순위 기반 중첩을 허용하기도 한다(GIC/NVIC 우선순위). 너무 오래 끄면 인터럽트를 잃거나 지연되므로, 핸들러는 **top half(최소 처리) / bottom half(softirq, 워크큐, RTOS의 deferred task)** 로 나눈다.

</details>

## 11. 자가 점검 & 숙제

### 퀴즈

1. LDE의 "Limited" 를 실현하는 하드웨어 기능 세 가지는?
2. 새 프로세스를 처음 시작할 때도 return-from-trap을 쓰는 이유는?
3. Figure 6.3에서 레지스터 저장이 두 번 일어난다. 각각 무엇을, 누가, 어디에 저장하나?
4. 컨텍스트 스위치가 2 µs, 타임 슬라이스가 1 ms라면 직접 오버헤드는 몇 %인가? 슬라이스를 50 µs로 줄이면?
5. 이 Mac에서 `gettimeofday()` 가 `getppid()` 보다 10배 가까이 빠른 이유는?

<details>
<summary>정답</summary>

1. (1) **유저/커널 모드**와 특권 명령, (2) **trap / return-from-trap** 명령과 부팅 때 등록하는 **trap table**, (3) **타이머 인터럽트**. (+ 메모리 보호용 MMU, 이건 메모리 가상화 장에서.)
2. 유저 모드로 **내려가는 유일한 공식 경로**가 return-from-trap이기 때문. 커널 스택에 "막 trap한 것처럼" 가짜 레지스터(PC = 진입점, SP = 유저 스택)를 채워 두고 return-from-trap하면 하드웨어가 권한을 내리며 그곳으로 점프한다.
3. ① 타이머 인터럽트 때: A의 **유저 레지스터**를 **하드웨어**(+진입 코드)가 **A의 커널 스택**에. ② switch() 때: A의 **커널 레지스터**를 **OS 소프트웨어**가 **A의 프로세스 구조체(PCB)** 에.
4. 2 µs / 1000 µs = **0.2%**. 50 µs면 2 / 50 = **4%** (간접 비용 제외).
5. `gettimeofday()` 는 커널 진입(trap) 없이 commpage(커널이 매핑해 둔 읽기 전용 페이지)의 값을 읽어 계산하기 때문. `getppid()` 는 실제로 `svc` 로 trap한다.

</details>

### 원문 숙제 (측정) — 이 노트에서 한 것과 더 해볼 것

- **시스템 콜 비용 측정** (0바이트 read 반복): §8.4에서 실행. **타이머 정밀도를 먼저 재고, 그에 맞춰 반복 횟수를 정하는 측정 방법론**을 확인하는 문제. 더 해 볼 것: `CNTVCT_EL0` 를 직접 읽어 41.67 ns 해상도로 단일 호출 분포를 그려 보기.
- **컨텍스트 스위치 비용 측정** (lmbench식 파이프 핑퐁): §8.5에서 실행. **통신 비용을 분리하고 CPU 고정의 필요성**을 이해하는 문제. 더 해 볼 것: Linux 머신(또는 VM)에서 `taskset -c 0` 으로 같은 코어 / 다른 코어를 비교. macOS에서는 `taskpolicy -b` 로 백그라운드(E코어 위주)로 돌려 P/E 코어 차이를 보기.
- (추가) §8.7의 `TICK_MS` 를 1, 10, 50으로 바꿔 보고, 같은 시간 동안 A+B의 총 `work` 가 어떻게 변하는지 보라. 타임 슬라이스가 짧을수록 스위치 오버헤드로 총 작업량이 줄어드는지 확인하는 문제.

## 12. 다음으로

- 다음 장: [Ch.07 스케줄링 입문](2026-09-30_C07_scheduling_intro.md) — 메커니즘(이 장)은 갖췄다. 이제 정책: **지금 어떤 프로세스를 돌리나?** FIFO, SJF, STCF, 라운드 로빈, 그리고 타임 슬라이스 길이의 트레이드오프.
- 인터럽트 중 인터럽트, 커널 락: [Ch.26 동시성 입문](2026-09-30_C26_concurrency_intro.md), [Ch.28 락](2026-09-30_C28_locks.md).
- 주소 공간 전환(페이지 테이블 레지스터)과 TLB: [Ch.19 TLB](2026-09-30_C19_tlb.md).
- 하드웨어 가상화(trap-and-emulate는 LDE의 확장): [부록 B VMM](2026-09-30_C0B_virtual_machine_monitors.md).
- 장치 인터럽트 vs 폴링: [Ch.36 I/O 장치](2026-09-30_C36_io_devices.md).

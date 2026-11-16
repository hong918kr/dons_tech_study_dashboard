---
id: ARM-A01
time: 120분
updated: 2026-11-15
---
# ARM-A01 PendSV 컨텍스트 스위치 어셈블리 (Cortex-M)

## 학습 목표

- `PendSV_Handler` 를 **순수 `.S`** 로 쓴다: PSP 에 r4–r11 저장 → `cur->sp` 기록 → 다음 TCB 복원 → `msr psp` → `bx lr`(EXC_RETURN). 인라인 asm 판(RTOS-A01)과 달리 컴파일러가 한 줄도 끼어들지 않는다.
- 하드웨어가 쌓는 8워드와 소프트웨어가 쌓는 8워드가 합쳐진 **16워드 저장 문맥**을 주소까지 그리고, `init_stack()` 이 같은 모양을 "가짜로" 만들어야 하는 이유(xPSR=0x01000000, PC bit0=0, LR=exit trap, 8바이트 정렬)를 설명한다.
- **레지스터 보존 검사기**(r0–r12, lr 에 매 라운드 다른 패턴 → 오래 돈 뒤 전부 대조)로 스위처를 검증하고, "창(window) 안에서 진짜 스위치가 일어났다" 까지 센다.
- 결함을 실제로 넣어 본다: r11 누락, PendSV 우선순위가 바쁜 IRQ 보다 높음, `isb` 누락, 작은 스택 오버플로, (M4F) s16–s31 미저장 — 각각 QEMU 에서 무슨 일이 났는지 숫자로 말한다.
- Cortex-M4F 에서 EXC_RETURN bit4 로 s16–s31 을 조건부 저장하고, **lazy stacking 때문에 `vstmdb` 한 줄이 s0–s15 까지 지킨다**는 것을 실측으로 보인다.

## 1. 문제

"PendSV 핸들러를 화이트보드에 써 봐. FPU 켜져 있으면 뭐가 달라져? 왜 PendSV 가 제일 낮은 우선순위야?" — RTOS 를 쓰는 펌웨어 팀(드론·로봇·SSD 컨트롤러 보조 코어)이 시니어에게 거의 반드시 묻는 질문이다. 실제 버그(태스크 하나의 변수만 가끔 깨짐, FPU 켠 뒤부터 PID 출력이 튐)도 대부분 이 20줄 안에서 나온다.

RTOS-A01 에서는 C 파일 안의 인라인 asm 으로 미니 RTOS 전체(비트맵 스케줄러, PI 뮤텍스)를 만들었다. 이번에는 범위를 **스위처 하나**로 좁히고, 대신 (1) 순수 asm, (2) 모든 레지스터를 검사하는 테스트, (3) 일부러 틀린 스위처 다섯 가지, (4) FPU 판까지 간다. ARM-M09 의 예외 프레임·EXC_RETURN, ARM-M10 의 BASEPRI 를 그대로 쓴다.

```svg
<svg viewBox="0 0 740 370" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">스위치아웃된 태스크 A 의 스택 (gdb 실측, an385, 주소는 위가 높음)</text>
  <rect x="20" y="34" width="250" height="22" class="f-mut" stroke="currentColor"/>
  <text x="28" y="49" fill="currentColor" font-family="monospace">0x20000bc8 = 스택 꼭대기 (8 정렬)</text>
  <rect x="20" y="56" width="250" height="176" class="f-acc" stroke="currentColor"/>
  <text x="28" y="74" fill="currentColor" font-family="monospace">+60 xPSR 0x01000000</text>
  <text x="28" y="92" fill="currentColor" font-family="monospace">+56 PC   0x0000158c (bit0=0)</text>
  <text x="28" y="110" fill="currentColor" font-family="monospace">+52 LR   0x000015f1 (exit trap)</text>
  <text x="28" y="128" fill="currentColor" font-family="monospace">+48 r12  0x0c0c0c0c</text>
  <text x="28" y="146" fill="currentColor" font-family="monospace">+44 r3   0x03030303</text>
  <text x="28" y="164" fill="currentColor" font-family="monospace">+40 r2   0x02020202</text>
  <text x="28" y="182" fill="currentColor" font-family="monospace">+36 r1   0x01010101</text>
  <text x="28" y="200" fill="currentColor" font-family="monospace">+32 r0   0x20000074 (&amp;RC[0])</text>
  <text x="28" y="222" fill="currentColor">↑ 하드웨어가 예외 진입 때 push</text>
  <rect x="20" y="232" width="250" height="100" class="f-ok" stroke="currentColor"/>
  <text x="28" y="252" fill="currentColor" font-family="monospace">+28 r11 0x0b0b0b0b</text>
  <text x="28" y="272" fill="currentColor" font-family="monospace">  …  r5..r10</text>
  <text x="28" y="292" fill="currentColor" font-family="monospace">+0  r4  0x04040404</text>
  <text x="28" y="320" fill="currentColor">↑ PendSV 의 stmdb r0!, {r4-r11}</text>
  <text x="20" y="352" fill="currentColor" font-family="monospace">0x20000b88 = A.sp (TCB 첫 칸)</text>
  <path d="M 200 346 L 270 334" stroke="currentColor"/>
  <text x="300" y="60" fill="currentColor" font-weight="bold">M4F 이고 A 가 FP 를 썼으면 (EXC_RETURN 0xFFFFFFED)</text>
  <rect x="300" y="70" width="200" height="26" class="f-warn" stroke="currentColor"/>
  <text x="308" y="88" fill="currentColor" font-family="monospace">s0–s15, FPSCR, 예약 (18)</text>
  <rect x="300" y="96" width="200" height="26" class="f-acc" stroke="currentColor"/>
  <text x="308" y="114" fill="currentColor" font-family="monospace">r0..xPSR (8)</text>
  <rect x="300" y="122" width="200" height="26" class="f-warn" stroke="currentColor"/>
  <text x="308" y="140" fill="currentColor" font-family="monospace">s16–s31 (16) ← vstmdbeq</text>
  <rect x="300" y="148" width="200" height="26" class="f-ok" stroke="currentColor"/>
  <text x="308" y="166" fill="currentColor" font-family="monospace">r4–r11 (8) ← stmdb</text>
  <text x="510" y="88" fill="currentColor">하드웨어 (lazy: 자리만)</text>
  <text x="510" y="114" fill="currentColor">하드웨어</text>
  <text x="510" y="140" fill="currentColor">소프트웨어</text>
  <text x="510" y="166" fill="currentColor">소프트웨어</text>
  <text x="300" y="200" fill="currentColor">합계 26 + 24 = 50 워드 = 200 B / 태스크</text>
  <text x="300" y="218" fill="currentColor">FP 안 쓴 태스크: 16 워드 = 64 B</text>
  <text x="300" y="250" fill="currentColor" font-weight="bold">복원 순서 = 저장의 정확한 역순</text>
  <text x="300" y="270" fill="currentColor">ldmia r1!, {r4-r11}</text>
  <text x="300" y="288" fill="currentColor">(bit4==0) vldmia r1!, {s16-s31}</text>
  <text x="300" y="306" fill="currentColor">msr psp, r1 → bx lr</text>
  <text x="300" y="324" fill="currentColor">→ 하드웨어가 나머지를 pop</text>
</svg>
```

### Part A — 스위처 본체 (asm) + 초기 프레임 (C)

- `starter.S` TODO 1 `SVC_Handler` (svc #0 = 스케줄러 시작): main 의 r4–r11 + EXC_RETURN 을 MSP 에 push, `g_main_msp` 기록, `g_cur` 의 저장 문맥을 풀어 PSP 로 첫 태스크에 들어간다. `os_run` 이 시작 경쟁을 막으려 올려 둔 BASEPRI(0x40) 도 여기서 0 으로.
- TODO 2 `PendSV_Handler`: r4–r11 저장 → `os_schedule(EXC_RETURN)` (C, 주어짐: 스택 검사 + 라운드로빈) → 복원. `os_schedule` 이 NULL 이면 **main 으로 복귀**: `g_main_msp` 에서 SVC 가 쌓은 것을 pop 하고 EXC_RETURN 0xFFFFFFF9 로 `os_start` 의 svc 다음 명령에 돌아간다.
- `starter.c` TODO 3 `init_stack(tcb, entry, arg)`, TODO 4 `os_tick()` (SysTick 이 부름: 틱 증가, 한계면 stop, `ICSR = PENDSVSET`).
- 검사(주어짐, `shared_tasks.S`): `task_regcheck` 는 매 라운드 xorshift 패턴 14개를 r0–r12, lr 에 `ldmia` 로 넣고 2000~6095번 돈 뒤 `push {r0-r12, lr}` 로 꺼내 C 에서 대조한다. 창 밖의 C 호출은 `cpsid i` 로 감싸서 스위치는 asm 창 안(또는 `cpsie` 경계)에만 떨어진다.

### Part B — 결함 주입 (모두 실제로 넣고 결과를 assert)

- solution 안: B4 바쁜 IRQ6(0x80) 가 SysTick(0x40)에 계속 끊겨도 PendSV(0xFF)는 IRQ6 이 끝난 뒤에만 → 손상 0. B5 96워드 스택 태스크가 재귀로 넘침 → `os_schedule` 의 카나리/범위 검사가 잡고 그 태스크만 죽인다.
- `run_qemu.sh bugs` (`demo_bugs.S` 를 VTOR 로 끼움): BUG1a r11 누락(일관), BUG1b r11 누락(초기 프레임은 8칸), BUG2 PendSV 우선순위 0x40, BUG3 `isb` 누락, BUG4 (M4F) s16–s31 미저장 × LSPEN 0/1, BUG5 시작 경쟁(BASEPRI 가드 없음).

### Part C — Cortex-M4F (mps2-an386): FPU 인식 스위처

- EXC_RETURN bit4 로 `vstmdbeq/vldmiaeq {s16-s31}`, EXC_RETURN 을 TCB 에 태스크별로 저장. FP 태스크 F, G 는 s0–s31 패턴까지 검사, I 는 정수 전용. FPCCR 리셋값 관찰.

| 파일 | 역할 |
|---|---|
| `starter.S` / `solution.S` | TODO: `SVC_Handler`, `PendSV_Handler` (`#ifdef HAS_FPU` 로 M4F 판 포함) |
| `starter.c` / `solution.c` | TODO: `init_stack`, `os_tick` + `#include "harness.c"` (테스트) |
| `a01.h` | TCB/검사 구조체 + `_Static_assert` 오프셋, 레지스터 주소 |
| `shared_os.c` | `os_schedule`, `os_run`, SysTick(워치독), `rc_prepare/rc_verify`, exit trap C 부분 |
| `shared_tasks.S` | `os_start`(main 센티널 검사), `task_regcheck`, `task_fpcheck`, `IRQ6_Handler`, 헬퍼 |
| `shared_startup.c` / `arm_a01.ld` | 벡터 테이블(16+32), Reset, 세미호스팅 (ARM-M09 축약판) |
| `demo_bugs.S` / `bugs_out.txt` | Part B 의 잘못된 핸들러들과 실제 출력 |
| `demo_switch.gdb` / `demo_switch_out.txt` | 첫 스위치 A→B 를 한 명령씩 본 gdb 세션 |
| `practice.c` / `practice.S` | 추가 연습 답 |

## 2. Problem (English)

> **Minimal preemptive context switcher for Cortex-M3/M4F, in assembly.** Bare metal on QEMU `mps2-an385` (Cortex-M3) and `mps2-an386` (Cortex-M4F).
>
> 1. `uint32_t *init_stack(struct tcb *t, void (*entry)(void *), void *arg)` — build the initial 16-word frame at the 8-byte-aligned top of `t`'s stack so that the first switch-in "returns" into `entry(arg)` in Thread mode on PSP; if `entry` returns it must land in `task_exit_trap`. Write two canary words at the stack bottom.
> 2. `void os_tick(void)` — called from SysTick: count, request stop at the limit, pend PendSV.
> 3. `SVC_Handler` — `svc #0` from `os_start()` (privileged Thread, MSP): save the caller's callee-saved context on MSP, start `g_cur` on PSP.
> 4. `PendSV_Handler` — save r4–r11 (and s16–s31 if EXC_RETURN[4] == 0) on the outgoing PSP, store `sp` (and EXC_RETURN) in the TCB, call `struct tcb *os_schedule(uint32_t exc_return)`, restore the next task; if it returns NULL, resume the caller of `os_start()`.
>
> Constraints: AAPCS (r4–r11 callee-saved, 8-byte stack at calls); PendSV is the lowest-priority exception; no task may observe any register change across preemption. Example (real run, an385): two tasks, 300 ticks → `switches=299`, task A 237 rounds, 149 of them preempted mid-window, 0 mismatches; `os_start` returns 0 (main's r4–r11 intact).
>
> Follow-ups: Why lowest priority? What changes with an FPU and lazy stacking? How do you start the first task without SVC? How do you detect stack overflow?

## 3. 시작 코드

asm TODO (스위처 두 개):

<!-- code: starter.S -->

C TODO (`init_stack`, `os_tick`) — 맨 위에서 테스트 하네스를 include 한다:

<!-- code: starter.c -->

공용 선언 (asm 오프셋과 맞추는 `_Static_assert` 포함):

<!-- code: a01.h -->

주어진 커널 C 부분 (`os_schedule`, `os_run`, 워치독, 검사 태스크의 C 쪽):

<!-- code: shared_os.c -->

주어진 asm (검사 태스크, 바쁜 IRQ, `os_start`):

<!-- code: shared_tasks.S -->

테스트 하네스 (`-DBUGS` 부분은 Part B 실험):

<!-- code: harness.c -->

빌드·실행은 Lima VM `elinux` 안에서 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). 맥에서 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-A01
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"   # solution / practice / fpu / bugs / gdb / objdump
```

`starter`/`solution` 은 같은 펌웨어를 두 번 돈다: `-mcpu=cortex-m3` 로 `mps2-an385`, `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DHAS_FPU` 로 `mps2-an386` (Part C 추가). QEMU 는 `-icount shift=0,align=off,sleep=off` (가상 시간 = 명령 수) 로 돌려 SysTick 이 매번 같은 명령에 떨어진다 — 아래 숫자가 매번 같게 나오는 이유다.

<!-- code: run_qemu.sh -->

처음 실행 결과 (실제 출력, 발췌):

```text
A0 init_stack
FAIL line 92: sp[15] == XPSR_INIT
...
   top(misaligned)=0x20000fc4 -> sp=0x20000f84
FAIL line 104: ((uint32_t)(uintptr_t)sp & 7u) == 0u && sp == &g_stk[1][254 - 16]
A1 two regcheck tasks, SysTick -> PENDSVSET -> PendSV
   output: 
A1 ticks=0 switches=0 pendsv=0 from_handler=0 main_mask=0x00000000
FAIL line 119: g_os.ticks == 300u && g_os.switches >= 290u
...
13/35 passed
[mps2-an385] qemu exit=1
...
17/42 passed
[mps2-an386] qemu exit=1
qemu exit=1
```

스텁 `SVC_Handler` 는 `bx lr` 만 해서 `os_start` 가 곧장 돌아오고(태스크 0개 실행), `os_run` 이 SysTick 을 끈다 — 그래서 starter 는 멈추지 않는다. 반쯤 푼 답(예: SVC 는 되는데 `os_tick` 이 빈 함수, 또는 SVC 에서 BASEPRI 를 안 풂)은 두 워치독이 잡아 `FAIL watchdog` 으로 끝난다 (SysTick 워치독, "틱 0 인 채 2000 라운드" 워치독).

VM 없이 맥에서 "어셈블은 되나?" 만:

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=thumbv7m-none-eabi -c solution.S -o /tmp/ARM-A01_s.o && /usr/bin/objdump -d /tmp/ARM-A01_s.o | head -20
clang --target=thumbv7em-none-eabihf -mfpu=fpv4-sp-d16 -DHAS_FPU -c solution.S -o /tmp/ARM-A01_f.o   # FPU 판도 OK
```

## 4. 힌트

<details>
<summary>힌트 1 — PendSV 진입 순간 이미 끝나 있는 일</summary>

태스크가 PSP 를 쓰고 있었으니 하드웨어는 r0–r3, r12, lr, pc, xPSR 를 **PSP** 에 쌓았고, 핸들러는 MSP 위에서 돈다. 그래서 `push` 가 아니라 `mrs r0, psp` → `stmdb r0!, {r4-r11}` 로 **태스크 스택**에 쌓는다. lr 은 복귀 주소가 아니라 EXC_RETURN(0xFFFFFFFD) 이다.

</details>

<details>
<summary>힌트 2 — C 를 부르기 전후</summary>

`bl os_schedule` 은 lr 을 덮어쓴다. EXC_RETURN 을 잃으면 예외 복귀가 안 되니 `push {r3, lr}` (짝을 맞춰 8 정렬) → `bl` → `pop {r3, lr}`. 인자로 넘길 EXC_RETURN 은 `mov r0, lr` 로 먼저.

</details>

<details>
<summary>힌트 3 — 첫 태스크와 main 복귀는 같은 모양</summary>

`init_stack` 이 만든 16워드는 PendSV 가 저장한 모양과 같다. 그래서 SVC 는 "PendSV 의 복원 절반" 만 하면 된다: `ldmia r1!, {r4-r11}` → `msr psp, r1` → lr = 0xFFFFFFFD → `bx lr`. main 복귀는 그 반대: SVC 가 MSP 에 `push {r3-r11, lr}` 한 것을 PendSV 가 `pop {r3-r11, lr}` → `bx lr`(0xFFFFFFF9) 하면 하드웨어가 svc 때 쌓은 main 프레임을 꺼낸다.

</details>

<details>
<summary>힌트 4 — M4F: 무엇을 보고 s16–s31 을 저장하나</summary>

EXC_RETURN bit4 = 0 이면 이 태스크는 FP 문맥이 있다(확장 프레임). `tst lr, #0x10` → `it eq` → `vstmdbeq r0!, {s16-s31}`. 복원 때는 **들어오는 태스크의** EXC_RETURN 을 봐야 하므로 TCB 에 저장해 둔 값을 lr 로 읽어 와서 같은 검사를 한다.

</details>

## 5. 풀이

### Part A — 저장 문맥 16워드와 `init_stack`

| 칸 (sp+) | 0..28 | 32 | 36..44 | 48 | 52 | 56 | 60 |
|---|---|---|---|---|---|---|---|
| 내용 | r4..r11 | r0 | r1..r3 | r12 | LR | PC | xPSR |
| 초기값 | 0x04040404..0x0B0B0B0B | `arg` | 0x01010101.. | 0x0C0C0C0C | `task_exit_trap` | `entry & ~1` | 0x01000000 |
| 누가 쌓나 | PendSV | 하드웨어 | 하드웨어 | 하드웨어 | 하드웨어 | 하드웨어 | 하드웨어 |

- **xPSR = 0x01000000**: T 비트(24)만. 0 이면 예외 복귀 직후 INVSTATE UsageFault (RTOS-A01 F1 에서 실측). IPSR 칸(하위 9비트)이 0 이어야 "Thread 로 복귀" 가 성립한다 — 이 조건이 깨지는 모습은 BUG1b 에서 본다.
- **PC bit0 = 0**: 함수 포인터에는 Thumb 비트가 있다 (`task_regcheck & 1 == 1` 을 A0 이 확인). 스택된 PC 는 bit0 = 0 이어야 한다. 빼먹으면? 변이 m4 로 실제로 넣어 봤다: 테스트 A0 만 FAIL 하고 나머지 런타임은 **전부 통과**했다. `-d guest_errors` 로 보니 QEMU 가 `M profile return from interrupt with misaligned PC is UNPREDICTABLE on v7M` 를 찍고 그냥 진행한다. 실칩에서는 UNPREDICTABLE 이니 "QEMU 에서 됐다" 는 근거가 못 된다.
- **8바이트 정렬**: `top = (lo + words) & ~7`. 스택 길이가 홀수 워드인 B 의 경우 실측: `top(misaligned)=0x20000fc4 -> sp=0x20000f80` (0xfc4 → 0xfc0 로 내림, −64 B). 하드웨어 프레임 시작(sp+32)이 8 정렬이면 xPSR bit9 패딩 표시가 필요 없다 (ARM-M09).
- **LR = exit trap**: A3 에서 `task_ret_fn` 이 `0xBEEF ^ 0x5A5A0000` 을 return → `exited_id=0x00000002 exited_r0=0x5a5abeef`. 태스크 함수의 반환이 곧 `bx lr` 이고, 그 lr 이 초기 프레임 LR 칸이다.

### Part A — SVC 시작, PendSV 스위치, main 복귀

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">main ↔ 태스크 왕복 (MSP 는 gdb 실측 값)</text>
  <rect x="10" y="34" width="150" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">main: os_start()</text>
  <text x="18" y="68" fill="currentColor" font-family="monospace">r4..r11=0x44..0xBB</text>
  <rect x="190" y="34" width="190" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="198" y="52" fill="currentColor">SVC_Handler (lr=0xFFFFFFF9)</text>
  <text x="198" y="68" fill="currentColor" font-family="monospace">push {r3-r11,lr}; g_main_msp</text>
  <rect x="410" y="34" width="150" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="418" y="52" fill="currentColor">태스크 A, B (PSP)</text>
  <text x="418" y="68" fill="currentColor">SysTick→PendSV 반복</text>
  <rect x="590" y="34" width="140" height="44" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="598" y="52" fill="currentColor">PendSV: NULL</text>
  <text x="598" y="68" fill="currentColor" font-family="monospace">pop; bx 0xFFFFFFF9</text>
  <path d="M160 56 L186 56" stroke="currentColor"/><path d="M186 56 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M380 56 L406 56" stroke="currentColor"/><path d="M406 56 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M560 56 L586 56" stroke="currentColor"/><path d="M586 56 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M660 78 L660 100 L85 100 L85 82" stroke="currentColor" fill="none"/><path d="M85 82 l-4 7 l8 0 z" fill="currentColor"/>
  <text x="250" y="96" fill="currentColor">하드웨어가 MSP 의 svc 프레임을 꺼냄 → os_start_ret 에서 계속</text>
  <text x="10" y="132" fill="currentColor" font-weight="bold">태스크가 도는 동안의 MSP (main 스택 맨 위)</text>
  <rect x="40" y="144" width="300" height="26" class="f-mut" stroke="currentColor"/>
  <text x="48" y="162" fill="currentColor" font-family="monospace">0x2000ff70  svc 직전 main 의 sp</text>
  <rect x="40" y="170" width="300" height="50" class="f-acc" stroke="currentColor"/>
  <text x="48" y="188" fill="currentColor" font-family="monospace">0x2000ff50  svc 하드웨어 프레임 (8)</text>
  <text x="48" y="206" fill="currentColor" font-family="monospace">  PC=0x1526 os_start+38, xPSR=0x21000000</text>
  <rect x="40" y="220" width="300" height="50" class="f-ok" stroke="currentColor"/>
  <text x="48" y="238" fill="currentColor" font-family="monospace">0x2000ff28  r3, r4..r11, lr=0xfffffff9</text>
  <text x="48" y="256" fill="currentColor" font-family="monospace">  (= g_main_msp)</text>
  <rect x="40" y="270" width="300" height="40" class="f-warn" stroke="currentColor"/>
  <text x="48" y="294" fill="currentColor">그 아래: SysTick / PendSV / IRQ 가 쓰는 공간</text>
  <text x="370" y="162" fill="currentColor">• 이 40+32 B 는 태스크가 도는 내내 그대로 있다</text>
  <text x="370" y="182" fill="currentColor">  (Thread 가 PSP 를 쓰니 MSP 는 핸들러 전용)</text>
  <text x="370" y="206" fill="currentColor">• PendSV 진입 시 msp == g_main_msp (gdb 확인)</text>
  <text x="370" y="230" fill="currentColor">• FreeRTOS 는 반대로 시작 때 MSP 를 VTOR[0]</text>
  <text x="370" y="248" fill="currentColor">  값으로 되감고 main 으로 안 돌아온다</text>
  <text x="370" y="272" fill="currentColor">• 이 레슨은 테스트를 위해 "돌아오는" 스케줄러:</text>
  <text x="370" y="290" fill="currentColor">  os_run() 이 여러 번 불린다 (A1, A2, …)</text>
</svg>
```

`run_qemu.sh objdump` 의 실제 기계어 (an385, `-O2` 와 무관 — asm 이라 그대로):

```text
00000d28 <PendSV_Handler>:
     d28:	f3ef 8009 	mrs	r0, PSP
     d2c:	e920 0ff0 	stmdb	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
     d30:	4b0b      	ldr	r3, [pc, #44]	@ (d60 <PendSV_Handler+0x38>)
     d32:	681a      	ldr	r2, [r3, #0]
     d34:	6010      	str	r0, [r2, #0]
     d36:	4670      	mov	r0, lr
     d38:	b508      	push	{r3, lr}
     d3a:	f000 f82d 	bl	d98 <os_schedule>
     d3e:	e8bd 4008 	ldmia.w	sp!, {r3, lr}
     d42:	b138      	cbz	r0, d54 <PendSV_Handler+0x2c>
     d44:	6801      	ldr	r1, [r0, #0]
     d46:	e8b1 0ff0 	ldmia.w	r1!, {r4, r5, r6, r7, r8, r9, sl, fp}
     d4a:	f381 8809 	msr	PSP, r1
     d4e:	f3bf 8f6f 	isb	sy
     d52:	4770      	bx	lr
     d54:	4803      	ldr	r0, [pc, #12]	@ (d64 <PendSV_Handler+0x3c>)
     d56:	6800      	ldr	r0, [r0, #0]
     d58:	4685      	mov	sp, r0
     d5a:	e8bd 4ff8 	ldmia.w	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
     d5e:	4770      	bx	lr
```

스위치 경로는 0xd28–0xd52 의 16명령 + `os_schedule`. SVC 의 `ldr lr, =0xFFFFFFFD` 는 어셈블러가 리터럴 풀 대신 `mvn.w lr, #2` (f06f 0e02) 로 바꿨다 (~2 = 0xFFFFFFFD). `arm-none-eabi-size`: an385 text 8192 B / an386 9280 B (테스트 포함 전체).

gdb 로 본 첫 스위치 (`demo_switch_out.txt`, `run_qemu.sh gdb`, 발췌). 이 레인은 icount 없이 돌린다 — gdb 가 멈춘 동안에도 호스트 시간이 흘러 틱이 이미 대기 중이었고, 그래서 A 는 **첫 명령에서** 끊겼다 (stacked pc = `task_regcheck`, r4–r11 = 초기 채움값):

```text
Breakpoint 1, PendSV_Handler () at solution.S:59
  lr(EXC_RETURN)=0xfffffffd msp=0x2000ff28 psp=0x20000ba8 control=0 g_cur=A
  r4=04040404 r5=05050505 r6=06060606 r7=07070707 r8=08080808 r9=09090909 r10=0a0a0a0a r11=0b0b0b0b
0x20000ba8 <g_stk+992>:	0x20000074	0x01010101	0x02020202	0x03030303
0x20000bb8 <g_stk+1008>:	0x0c0c0c0c	0x000015f1	0x0000158c	0x01000000
=== (2) stmdb + str 뒤 ===
  r0(new A sp)=0x20000b88  A.sp in TCB=0x20000b88
=== (3) ... B 복원 직전 (bx lr) ===
  os_schedule -> r0=0x20000020 (&T[1]=0x20000020) g_cur=B
  psp=0x20000fa8  lr=0xfffffffd
=== (4) bx lr 한 명령 → B 의 첫 명령 ===
SysTick_Handler () at shared_os.c:62
=== (5) 첫 main 복귀 ===
  r0=0x00000000 (NULL → main 으로)  g_main_msp=0x2000ff28 msp=0x2000ff20
=> 0xd5e <PendSV_Handler+54>:	bx	lr
  sp=0x2000ff50 lr=0xfffffff9
  r4=44444444 r5=55555555 r6=66666666 r7=77777777 r8=88888888 r9=99999999 r10=aaaaaaaa r11=bbbbbbbb
  frame pc -> os_start + 38 in section .text
  bx lr 뒤: pc=0x00001526 sp=0x2000ff70 control=0 ipsr=0 r4=0x44444444 r11=0xbbbbbbbb
```

(4) 가 재미있다: `bx lr` 한 번에 B 가 아니라 `SysTick_Handler` 로 갔다. 그 사이 SysTick 이 대기 중이었으므로 예외 복귀 대신 **tail-chaining** 으로 바로 SysTick 에 들어간 것 — lr 은 여전히 0xFFFFFFFD, PSP 는 이미 B 의 프레임(0x20000fa8). B 는 아직 한 명령도 안 돌았다. (5) 는 main 복귀: pop 직후 r4–r11 이 os_start 의 센티널로 돌아와 있고, 예외 복귀 뒤 sp=0x2000ff70 (svc 직전 값), IPSR=0, CONTROL=0.

### Part A — 레지스터 보존 검사 결과 (solution, an385)

```text
A1 two regcheck tasks, SysTick -> PENDSVSET -> PendSV
   output: ABABABABABABABABABABABABABABABABABABABABABABABAB
A1 ticks=300 switches=299 pendsv=300 from_handler=0 main_mask=0x00000000
   task A: rounds=237 windows_switched=149 mismatches=0 mask=0x00000000
   task B: rounds=241 windows_switched=149 mismatches=0 mask=0x00000000
   trace: BABABABABABABABABABABABABABABABABABABABA
A2 three tasks, C yields every round
A2 ticks=300 switches=448 pendsv=449 from_handler=0 main_mask=0x00000000
   task A: rounds=97 windows_switched=88 mismatches=0 mask=0x00000000
   task B: rounds=243 windows_switched=149 mismatches=0 mask=0x00000000
   task C: rounds=149 windows_switched=0 mismatches=0 mask=0x00000000
   trace: BCABCABCABCABCABCABCABCABCABCABCABCABCAB
   C yields=149
```

- `windows_switched=149`: A 의 237 라운드 중 149번은 **패턴을 레지스터에 든 채로 끊겼다가** 돌아와서 14개를 다 맞췄다. 0이면 테스트가 아무것도 검사 안 한 것이다 — 그래서 `>= 50` 을 assert 한다.
- `output` 의 A/B 교대: 태스크가 "창 안에서 스위치당했다" 를 알아챈 라운드마다 세미호스팅으로 자기 이름 한 글자를 찍는다 (24글자씩만).
- `switches=299`, `pendsv=300`: 300번째 PendSV 는 stop 을 보고 main 으로 돌아간 것.
- A2 의 C 는 매 라운드 끝에 `os_yield()` 하므로 **창 안에서는 한 번도 안 끊긴다** (`windows_switched=0`): C 는 틱이 A→B→C 로 넘겨준 직후 한 라운드(1틱 미만)를 돌고 스스로 양보한다. 스위치 수가 틱보다 많은 것(448 > 300)이 yield 의 증거. 그래서 A 는 C 가 양보한 직후에 남은 틱 조각만 받아 라운드가 적다(97).

### Part B — 바쁜 IRQ 와 PendSV 최저 우선순위 (B4) — 그리고 틀렸을 때 (BUG2)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">IRQ6(0x80) 안에서 SysTick(0x40) 이 PendSV 를 pend 하면</text>
  <text x="10" y="44" fill="currentColor" font-weight="bold">B4: PendSV = 0xFF (정답)</text>
  <rect x="10" y="54" width="70" height="26" class="f-ok" stroke="currentColor"/><text x="18" y="72" fill="currentColor">task A</text>
  <rect x="80" y="54" width="300" height="26" class="f-mut" stroke="currentColor"/><text x="88" y="72" fill="currentColor">IRQ6 (r4–r11 패턴 들고 6만 번)</text>
  <rect x="140" y="80" width="30" height="20" class="f-warn" stroke="currentColor"/><text x="142" y="94" fill="currentColor" font-size="11">Tick</text>
  <rect x="230" y="80" width="30" height="20" class="f-warn" stroke="currentColor"/><text x="232" y="94" fill="currentColor" font-size="11">Tick</text>
  <rect x="380" y="54" width="90" height="26" class="f-acc" stroke="currentColor"/><text x="386" y="72" fill="currentColor">PendSV 1번</text>
  <rect x="470" y="54" width="70" height="26" class="f-ok" stroke="currentColor"/><text x="478" y="72" fill="currentColor">task B</text>
  <text x="550" y="64" fill="currentColor">irq runs=30, 모두 틱 겪음</text>
  <text x="550" y="80" fill="currentColor">mism 0, from_handler 0</text>
  <text x="10" y="134" fill="currentColor" font-weight="bold">BUG2: PendSV = 0x40 (IRQ6 보다 높음)</text>
  <rect x="10" y="144" width="70" height="26" class="f-ok" stroke="currentColor"/><text x="18" y="162" fill="currentColor">task A</text>
  <rect x="80" y="144" width="60" height="26" class="f-mut" stroke="currentColor"/><text x="86" y="162" fill="currentColor">IRQ6</text>
  <rect x="140" y="170" width="30" height="20" class="f-warn" stroke="currentColor"/><text x="142" y="184" fill="currentColor" font-size="11">Tick</text>
  <rect x="170" y="170" width="80" height="20" class="f-acc" stroke="currentColor"/><text x="174" y="184" fill="currentColor" font-size="11">PendSV lr=F1</text>
  <rect x="250" y="144" width="130" height="26" class="f-mut" stroke="currentColor"/><text x="256" y="162" fill="currentColor">IRQ6 (B 의 r4–r11!)</text>
  <rect x="380" y="144" width="70" height="26" class="f-ok" stroke="currentColor"/><text x="386" y="162" fill="currentColor">task B</text>
  <text x="460" y="154" fill="currentColor">from_handler=183,</text>
  <text x="460" y="170" fill="currentColor">IRQ·B 의 r4–r11 손상</text>
  <text x="10" y="220" fill="currentColor">① PendSV 가 IRQ6 를 끊으면 지금 r4–r11 은 IRQ6 것 → A 의 스택에 IRQ6 값이 저장된다.</text>
  <text x="10" y="238" fill="currentColor">② B 의 r4–r11 을 꺼내 IRQ6 로 복귀 → IRQ6 은 남의 값으로 계속 (C 부분이면 포인터가 바뀐다).</text>
  <text x="10" y="256" fill="currentColor">③ 한 창에서 스위치가 짝수 번이면 IRQ6 값이 되돌아온다 → 손상이 적게 보인다 (실측 1).</text>
  <text x="10" y="274" fill="currentColor">④ stop 이 IRQ6 안에서 오면 main 복귀(F9)가 IRQ6 active 인 채 Thread 로 → INVPC.</text>
</svg>
```

solution B4 (an385) 실측: `irq runs=30 windows_with_tick=30 ticks_in_windows=181 irq mismatches=0`, `from_handler=0`. IRQ6 창 30개 모두에서 SysTick 이 평균 6틱씩 끼어들었지만, PendSV 는 IRQ6 이 끝난 뒤 한 번만 돌았다 (B4 의 `switches=147` 은 틱 300의 절반 — IRQ6 이 돌던 틱들은 PENDSVSET 이 한 비트로 합쳐졌다). M4F 판은 `ticks=305`: stop 틱이 IRQ6 안에서 와서, PendSV 가 IRQ6 이 끝나길 기다리는 동안 틱이 5개 더 지나갔다.

BUG2 (`bugs_out.txt`, 실제):

```text
BUG2 PendSV priority 0x40 (= SysTick, above busy IRQ6 0x80)
   HardFault: EXC_RETURN=0xfffffff9 CFSR=0x00040000 HFSR=0x40000000 g_cur=1
   MSP 0x2000ff68: 0x2000ffb0 0x00000002 0x000003e7 0x00000007 0x2000144c 0x00000959 0x00000eee 0x21000000
   BUG2 at fault: ticks=300 switches=299 pendsv=300 from_handler=183 main_mask=0x00000000
   task B: rounds=95 windows_switched=58 mismatches=1 mask=0x00000ff0
   irq runs=30 irq mismatches=1 irq mask=0xbcd097a6 last PendSV lr=0xfffffff1
```

- PendSV 300번 중 183번이 Handler 모드(IRQ6)를 끊고 들어왔다 (lr = 0xFFFFFFF1).
- 태스크 B 는 r4–r11 이 **전부** 남의 값(mask 0xff0)인 라운드를 겪었다.
- `irq mask=0xbcd097a6` 는 `irq_verify` 가 만들 수 없는 값(비트 4–11 만 가능)이다 → 메모리가 다른 경로로 덮였다. 가장 그럴듯한 설명은 IRQ6 의 C 부분(`irq_prepare`)이 r4–r11 에 들고 있던 포인터가 스위치로 바뀌어 xorshift 값을 엉뚱한 주소에 썼다는 것 (끝까지 추적하지는 않았다 — 같은 버그의 이전 빌드에선 mask 0xff0 로 나왔다).
- 마지막에 HardFault: CFSR 0x00040000 = UFSR.INVPC, HFSR.FORCED. 스택된 PC 0xeee = `os_start_ret` — **새 프레임 없이** main 의 svc 프레임을 그대로 HardFault 프레임으로 쓰고 있다. QEMU 10.2 `target/arm/tcg/m_helper.c` `do_v7m_exception_exit` 의 해당 분기 주석: *"Bad exception return: instead of popping the exception stack, directly take a usage fault on the current stack."* — Thread 로 돌아가려는데 다른 예외(IRQ6)가 active 이고 CCR.NONBASETHRDENA = 0 이라 `ufault` (같은 파일, `case 9`/`case 13` 의 `!rettobase` 검사).

### Part B — r11 누락: 조용한 손상(1a) vs 즉시 크래시(1b)

```text
BUG1a ticks=200 switches=199 pendsv=200 from_handler=0 main_mask=0x00000800
   task A: rounds=156 windows_switched=98 mismatches=98 mask=0x00000800
   task B: rounds=161 windows_switched=99 mismatches=98 mask=0x00000800
BUG1b PendSV {r4-r10} but SVC + init frame still 8 slots (expect HardFault)
   HardFault: EXC_RETURN=0xfffffffd CFSR=0x00040000 HFSR=0x40000000 g_cur=1
   MSP 0x2000ff28: 0x0b0b0b0b 0x20000168 0x01010101 0x02020202 0x03030303 0x0c0c0c0c 0x00000f10 0x00000cac
   PSP 0x20000dc4: 0x01000000 0x00000000 ...
   B init frame at 0x20000d88
```

- **1a** (SVC·PendSV·초기 프레임 모두 7칸으로 일관): 프레임 모양은 맞으니 아무것도 안 죽는다. 그런데 r11 은 아무도 안 바꿔 주므로 태스크끼리 **공유**된다 → 창 안에서 끊긴 98번 거의 전부 mask = 0x800 (정확히 r11 하나), main 의 r11 도 (`main_mask=0x800`). 현장에서 가장 무서운 형태: "가끔 한 태스크의 지역 변수 하나가 다른 값".
- **1b** (PendSV 만 7칸, `init_stack`·SVC 는 8칸): A 는 SVC 로 정상 시작, **B 는 처음 들어가는 길이 PendSV** 라 7워드만 꺼내 r11 칸부터를 하드웨어 프레임으로 읽는다. 그러면 스택된 xPSR 자리에 entry 주소(0xcac → IPSR 칸 = 0x0ac ≠ 0)가 온다 → "Thread 로 복귀하는데 IPSR ≠ 0" → INVPC. QEMU 는 이 경우 프레임을 **MSP 에 다시 쌓는다** (`m_helper.c` 주석: *"Take an INVPC UsageFault by pushing the stack again"*) — MSP 덤프가 정확히 한 칸 밀린 B 의 초기 프레임(r0=0x0b0b0b0b=r11 칸, pc=0xf10=exit trap, xpsr=entry)이다. EXC_RETURN 은 0xFFFFFFFD 인데 프레임은 MSP 에 있다는 점도 주의 — `tst lr, #4` 로 PSP 를 고르는 일반 HardFault 분석기(ARM-M12)는 여기서 엉뚱한 프레임을 읽는다.

### Part B — `isb` 누락 (BUG3), 스택 오버플로 (B5), 시작 경쟁 (BUG5)

- BUG3 `msr psp, r1` 뒤 `isb` 를 뺐다: `mismatches=0`, A 98 windows — **QEMU 에서는 관찰 불가**. 이유(문서 기준, 확인 권장): 바로 다음 명령이 예외 복귀(`bx lr`)이고 예외 복귀는 문맥 동기화 이벤트로 취급된다; PSP 를 Handler 모드에서 바꾸면 Handler 는 MSP 를 쓰므로 "다음 명령이 옛 sp 를 쓴다" 는 위험 자체가 없다. FreeRTOS CM3/CM4 포트가 `isb` 를 두는 건 ARM 권고를 따르는 방어용이다. **Thread 모드에서** `msr control` 로 SPSEL 을 바꿀 때(연습 1)는 `isb` 가 실제로 필요한 자리다.
- B5: 96워드(384 B) 스택 태스크가 깊이 12 재귀 → `overflow_sp=0x20000578 stack_lo=0x20000648` (208 B 아래), 칠해 둔 가드 128워드 중 52워드가 지워졌다. `os_schedule` 이 나가는 태스크의 `sp < lo+2` 또는 카나리 손상을 보고 그 태스크만 `alive=0` — A 는 `A.rounds=101` 로 계속 정상. 깊이 1 은 가드를 하나도 안 건드림. 한계: 검사는 **스위치 때만**, 그리고 이미 남의 메모리를 쓴 뒤다 (여기선 희생 가드가 있어 무사). 실제로는 MPU 가드 영역(BM-A08) 이나 ARMv8-M 의 PSPLIM 이 즉시 잡는다 (문서 확인 권장).
- BUG5 (gdb 가 찾아 준 버그): 처음 `os_run` 은 SysTick 을 켜고 바로 `os_start`(svc) 를 불렀다. gdb 로 멈춰 있는 동안 틱이 밀려 **svc 전에 PendSV 가 main 을 끊었고**(`from_main`), main 의 r9–r11 이 깨졌다 (`main_mask=0x00000e00`, gdb 레인에서 실제 관찰). 같은 상황을 `ICSR.PENDSTSET` 로 강제 재현한 BUG5: `from_main=1` → 뒤이어 INVPC HardFault. 고친 방법은 FreeRTOS 와 같은 생각: `os_run` 이 SysTick 을 켜기 **전에** BASEPRI=0x40 (SysTick·PendSV 차단, SVC 0 은 통과) → `SVC_Handler` 가 0 으로 푼다. 같은 디버깅에서 두 번째 구멍도 나왔다: stop 처리 중(`os_schedule` 의 to_main) SysTick 이 PendSV 를 **또** pend 하면 main 으로 돌아가자마자 PendSV 가 main 을 끊는다 → to_main 에서 SysTick 을 끈 뒤 `ICSR = PENDSTCLR | PENDSVCLR`.

### Part C — FPU 인식 스위처 (an386)

```text
C1 FPU-aware switch: F, G use s0-s31, I is integer-only
   FPCCR at reset = 0xc0000000
C1 ticks=300 switches=299 pendsv=300 from_handler=0 main_mask=0x00000000
   task F: rounds=160 windows_switched=98 mismatches=0 mask=0x00000000 fmask=0x00000000
   task G: rounds=156 windows_switched=97 mismatches=0 mask=0x00000000 fmask=0x00000000
   task I: rounds=163 windows_switched=99 mismatches=0 mask=0x00000000 fmask=0x00000000
   EXC_RETURN saved: F=0xffffffed G=0xffffffed I=0xfffffffd
```

- F, G 는 첫 `vldmia` 이후 CONTROL.FPCA = 1 → 이후 진입마다 EXC_RETURN 0xFFFFFFED (bit4 = 0). I 는 FP 를 안 써서 끝까지 0xFFFFFFFD. 스위처는 태스크마다 다른 값을 TCB 에 저장해 두었다가 **들어오는 태스크의 값**으로 복귀한다.
- FPCCR 리셋값 0xC0000000 = ASPEN(31) | LSPEN(30): FP 문맥 자동 저장 + lazy stacking 켜짐 (QEMU 실측, ARM-M09 과 같음).

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">BUG4: s16–s31 을 안 저장한 스위처 — LSPEN 에 따라 손상 범위가 다르다</text>
  <rect x="10" y="34" width="350" height="96" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-weight="bold">LSPEN = 0 (진입 때 즉시 저장)</text>
  <text x="20" y="74" fill="currentColor">진입: 하드웨어가 s0–s15, FPSCR 를 F 프레임에 씀</text>
  <text x="20" y="92" fill="currentColor">복귀(G): G 프레임에서 s0–s15 꺼냄 → 안전</text>
  <text x="20" y="110" fill="currentColor">s16–s31 은 아무도 안 바꿔 줌 → 공유</text>
  <text x="20" y="126" fill="currentColor" font-family="monospace">fmask = 0xffff0000</text>
  <rect x="380" y="34" width="350" height="96" class="f-warn" stroke="currentColor"/>
  <text x="390" y="54" fill="currentColor" font-weight="bold">LSPEN = 1 (lazy, 리셋 기본값)</text>
  <text x="390" y="74" fill="currentColor">진입: 자리만 잡고 LSPACT=1, FPCAR=그 자리</text>
  <text x="390" y="92" fill="currentColor">핸들러가 FP 명령을 안 씀 → 끝내 안 저장됨</text>
  <text x="390" y="110" fill="currentColor">복귀(G): LSPACT=1 → 프레임에서 안 꺼냄</text>
  <text x="390" y="126" fill="currentColor" font-family="monospace">fmask = 0xffffffff (s0–s15 도!)</text>
  <rect x="10" y="146" width="720" height="100" class="f-ok" stroke="currentColor"/>
  <text x="20" y="166" fill="currentColor" font-weight="bold">정답 스위처: vstmdbeq r0!, {s16-s31} 가 FP 명령이라 lazy 저장을 먼저 실행시킨다</text>
  <text x="20" y="186" fill="currentColor">→ s0–s15/FPSCR 가 나가는 태스크 프레임(FPCAR)에 써지고 LSPACT=0 → 복귀 때 정상 복원</text>
  <text x="20" y="206" fill="currentColor">BUG4 lazy 실행 뒤 main 에서: FPCCR=0xc0000019 (LSPACT=1 그대로), FPCAR=0x20000cb0 = G 의 옛 스택 안</text>
  <text x="20" y="226" fill="currentColor">→ 다음 FP 명령이 그 주소에 17 워드를 쏟는다 (첫 시도: 다음 실행의 태스크 스택이 깨져 BusFault)</text>
  <text x="20" y="242" fill="currentColor">정답 스위처로 돌린 뒤: FPCCR=0xc0000018 (LSPACT=0)</text>
</svg>
```

BUG4 의 실측 줄:

```text
BUG4 PendSV ignores EXC_RETURN bit4 (no s16-s31 save), FPCCR.LSPEN=0
   task F: rounds=160 windows_switched=97 mismatches=97 mask=0x00000000 fmask=0xffff0000
BUG4 PendSV ignores EXC_RETURN bit4 (no s16-s31 save), FPCCR.LSPEN=1
   task F: rounds=160 windows_switched=97 mismatches=97 mask=0x00000000 fmask=0xffffffff
   after lazy run, back in main: FPCCR=0xc0000019 FPCAR=0x20000cb0 (F stack 0x20000508 G stack 0x20000908)
BUG4 control: correct PendSV, LSPEN=1
   task F: ... mismatches=0 mask=0x00000000 fmask=0x00000000
   FPCCR back in main=0xc0000018
```

QEMU 소스(`m_helper.c`, 예외 복귀의 FP 처리)도 같은 동작이다: *"State in FPU is still valid, just clear LSPACT"* — LSPACT 가 1 이면 프레임에서 S 레지스터를 읽지 않는다. 처음 이 실험을 LSPEN=1 → 0 순서로 돌렸을 때는 남아 있던 LSPACT 때문에 두 번째 실행 첫 `vldmia` 가 옛 FPCAR 로 s0–s15 를 쏟아 태스크 스택의 `st` 포인터를 덮었고, `rc_prepare` 안에서 BusFault(CFSR 0x00008200, PRECISERR+BFARVALID) 가 났다 — 그래서 지금은 0 → 1 순서 + LSPACT 정리 후 대조군을 돈다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

<!-- code: solution.c diff=starter.c -->

실행 (VM): `limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"` → `35/35 passed` (an385) + `42/42 passed` (an386), 마지막 줄 `qemu exit=0`. 전체 0.8 초 안팎 (icount 모드). `fpu` 는 an386 판만, `bugs` 는 위 결함 5종 (`4/4, 6/6, 6/6, 3/3, 10/10 expected bug outcomes observed`, exit 0).

결함 주입용 핸들러 (Part B, `run_qemu.sh bugs`):

<!-- code: demo_bugs.S -->

## 7. 자주 하는 실수

| 실수 | 실제로 일어난 일 | 고친 코드 |
|---|---|---|
| 저장 목록에서 r11 누락 (초기 프레임은 8칸 그대로) | PendSV 로 처음 들어가는 태스크에서 INVPC HardFault (BUG1b) | `stmdb/ldmia {r4-r11}` — 초기 프레임·SVC·PendSV 세 곳의 칸 수가 같아야 한다 |
| 세 곳 모두 r11 누락 (일관) | 크래시 없이 r11 만 태스크 간 공유, mask 0x800 (BUG1a) | 같음. 이걸 잡으려면 r4–r11 각각에 패턴을 넣는 테스트가 필요하다 |
| PendSV 를 SysTick 과 같은 0x40 으로 | IRQ6 안에서 183번 스위치, IRQ·태스크 r4–r11 손상, 메모리 오염, 끝에 INVPC (BUG2) | `SHPR3[23:16] = 0xFF` (최저) |
| SysTick 을 켠 뒤 svc 전까지 무방비 | PendSV 가 main 을 태스크로 착각 (BUG5, gdb 레인에서 main r9–r11 손상) | `os_run`: BASEPRI 0x40 → SysTick on → svc, `SVC_Handler`: `msr basepri, 0` |
| stop 때 PENDSVCLR 안 함 | main 복귀 직후 대기 중이던 PendSV 가 main 을 끊음 (gdb 레인 `from_main`) | to_main: `SYST_CSR=0; ICSR = PENDSTCLR \| PENDSVCLR` |
| M4F 에서 EXC_RETURN bit4 무시 | s16–s31 공유, lazy 면 s0–s15 까지 + LSPACT 잔류 (BUG4) | `tst lr,#0x10; it eq; vstmdbeq r0!,{s16-s31}` + 태스크별 EXC_RETURN |
| 복원 때 **나가는** 태스크의 lr 로 bit4 검사 | F→I 스위치에서 I 프레임에 없는 s16–s31 을 pop → 프레임 어긋남 (분석; 직접 주입은 안 함) | `ldr lr, [next, #TCB_EXCRET]` 을 `vldmiaeq` **앞에** |
| `init_stack` 에서 `& ~7` 누락 | 홀수 워드 스택에서 sp 가 4 mod 8 (변이 m2: A0 2개 FAIL) | `top = (lo + words) & ~7u` |
| PC 칸에 Thumb 비트 그대로 | QEMU 는 경고(`misaligned PC is UNPREDICTABLE`)만 찍고 진행 — 테스트만 잡음 (변이 m4) | `entry & ~1u` |
| `bl os_schedule` 전에 lr 저장 안 함 | 복귀 주소가 `PendSV_Handler` 안쪽이 되어 예외 복귀가 아닌 일반 분기 → 엉망 (원리상) | `push {r3, lr}` … `pop {r3, lr}` |
| `ICSR \|= PENDSVSET` | 이 레슨에선 차이 없음 (변이 m6 통과). ICSR 의 set/clear 비트는 쓰기 1 에만 반응하고 읽기값은 상태라, RMW 는 "읽은 1 을 다시 써서 의도치 않은 pend" 위험이 있다 (문서 확인 권장) | `SCB_ICSR = ICSR_PENDSVSET;` (쓰기만) |

## 8. 면접 Q&A

<details>
<summary>Q1. 컨텍스트 스위치를 왜 SysTick 안에서 바로 안 하고 PendSV 로 미루나? 왜 최저 우선순위인가?</summary>

SysTick 이나 다른 ISR 은 **다른 ISR 을 선점한 상태**일 수 있다. 그 안에서 스위치하면 "지금 레지스터" 가 태스크 것이 아니라 끊긴 ISR 것이고, 복귀도 Thread 가 아니라 그 ISR 로 가야 한다. 최저 우선순위 PendSV 는 모든 ISR 이 끝나 **Thread 로 돌아가기 직전에만** 돈다 (tail-chain) — 그래서 lr 은 항상 0xFFFFFFFD/ED 이고 r4–r11 은 항상 태스크 것. 실측: 0xFF 일 때 `from_handler=0`, 0x40 일 때 300번 중 183번이 IRQ 안에서 스위치, 레지스터 손상과 INVPC (BUG2). 덤: 여러 소스가 PENDSVSET 을 여러 번 써도 펜딩 비트는 하나라 스위치는 한 번 (연습 2: 요청 600 → 진입 200).

</details>

<details>
<summary>Q2. 왜 r4–r11 만 소프트웨어로 저장하나? lr 은?</summary>

r0–r3, r12, lr, pc, xPSR 는 예외 진입 때 하드웨어가 이미 태스크 스택(PSP)에 쌓았다 — AAPCS 의 caller-saved 집합과 같아서 C 함수를 그대로 핸들러로 쓸 수 있게 한 설계. 남은 callee-saved r4–r11 은 핸들러 C 코드가 "자기가 쓰면 복구" 할 뿐이라, 다른 태스크의 값으로 **바꾸려면** 직접 저장/복원해야 한다. 핸들러의 lr 은 EXC_RETURN 이라 태스크 상태가 아니다 (태스크의 lr 은 하드웨어 프레임 안). M4F 에서는 EXC_RETURN 이 태스크마다 달라지므로(FD/ED) TCB(또는 FreeRTOS 처럼 태스크 스택)에 같이 저장한다. sp 자체는 TCB 의 `sp` 칸이 저장소다.

</details>

<details>
<summary>Q3. 첫 태스크는 어떻게 시작하나? SVC 방식과 직접 방식의 차이는?</summary>

SVC 방식(이 레슨, FreeRTOS CM3/CM4): `init_stack` 이 "쫓겨난 적 있는" 16워드를 만들고, SVC 핸들러가 PendSV 복원 절반만 해서 예외 복귀로 들어간다 — 첫 진입이 이후 모든 재개와 **같은 경로**라 초기 프레임 버그가 첫 순간에 드러난다. 직접 방식(연습 1): Thread 모드에서 PSP 설정 → CONTROL.SPSEL=1 → `isb` → r4–r11, r0, lr, pc 를 손으로 꺼내고 xPSR 칸은 버린 뒤 `bx`. 예외를 안 쓰니 PRIMASK 상태와 무관하게 동작하지만, 프레임을 두 가지 방식으로 해석하는 코드가 생긴다. 이 레슨의 연습 1 은 main 복귀를 위해 MSP 에 **가짜 예외 프레임**(PC=복귀 라벨, xPSR=0x01000000)까지 만들어 같은 PendSV 복귀 경로를 재사용한다 — 실측 `P1 direct start: ... A.mism=0 B.mism=0`.

</details>

<details>
<summary>Q4. FPU 가 켜져 있으면 스위처에 뭐가 달라지나? lazy stacking 은 왜 문제를 키우나?</summary>

FP 를 쓴 태스크는 진입 때 26워드 확장 프레임(s0–s15, FPSCR, 예약) + EXC_RETURN bit4=0. 스위처는 bit4 를 보고 s16–s31 을 추가로 저장/복원하고, 태스크별 EXC_RETURN 으로 복귀해야 한다. lazy stacking(LSPEN=1, 리셋 기본)에서는 하드웨어가 s0–s15 자리만 잡고 LSPACT=1, FPCAR=그 주소로 미뤄 둔다. 핸들러가 FP 명령을 하나라도 실행해야 그때 저장된다 — 정답 스위처의 `vstmdbeq` 가 그 역할까지 한다. 그 명령을 안 쓰는 스위처는 s16–s31 뿐 아니라 **s0–s15 까지** 태스크 간에 새고(fmask 0xffffffff vs LSPEN=0 일 때 0xffff0000), LSPACT 가 1 로 남아 다음 FP 명령이 옛 FPCAR(이미 다른 용도인 스택)에 17워드를 쏟는다 — 실측으로 BusFault 까지 봤다.

</details>

<details>
<summary>Q5. `msr psp, r1` 뒤의 `isb` 는 꼭 필요한가?</summary>

이 자리에서는 QEMU 에서 차이를 만들 수 없었다 (BUG3: 200틱, 98 windows, mismatch 0). 근거: Handler 모드는 MSP 를 쓰므로 PSP 변경이 다음 명령의 sp 에 영향이 없고, 바로 뒤 예외 복귀가 문맥 동기화로 동작한다 (Arm ARM 기준, 확인 권장). FreeRTOS 등이 넣어 두는 건 ARM 권고와 코어별 파이프라인 차이에 대한 방어. 반대로 **Thread 모드에서 CONTROL(SPSEL/nPRIV)을 바꿀 때**는 `isb` 가 필수다 — 다음 명령이 어느 sp/권한으로 실행될지가 바뀌기 때문. "관찰 안 된다" 와 "필요 없다" 는 다르다고 말할 수 있어야 한다.

</details>

<details>
<summary>Q6. 태스크 스택 오버플로를 어떻게 잡나? 이 레슨 방식의 한계는?</summary>

이 레슨: 스위치 때 나가는 태스크의 `sp >= lo+2` 와 바닥 카나리 2워드 검사 (FreeRTOS `configCHECK_FOR_STACK_OVERFLOW` 1/2 와 같은 발상). 실측: 96워드 스택이 208 B 넘쳤고 가드 52워드가 지워진 뒤에야 잡았다 — **사후 감지**다. 넘친 동안 남의 메모리를 썼을 수 있고, 스위치 사이에 넘쳤다 돌아오면 sp 검사는 못 보고 카나리만 본다. 즉시 잡으려면 MPU 로 스택 바닥에 접근 금지 영역(MemManage fault, BM-A08)이나 ARMv8-M 의 PSPLIM/MSPLIM (문서 확인 권장). 크기 결정은 high-water mark (연습 4: 검사 태스크 88 B, 루프 태스크 64 B) + 여유 + **ISR 은 MSP 를 쓴다**는 점(태스크 스택엔 프레임 8~26워드만 추가).

</details>

<details>
<summary>Q7. (꼬리질문) 이 설계가 틀리는 경우는? 실제로 찾은 버그를 말해 봐.</summary>

두 개를 gdb 가 찾아 줬다. (1) SysTick 을 켠 뒤 svc 전까지 틱이 오면 PendSV 가 main(Thread+MSP) 을 태스크로 착각한다 — EXC_RETURN 0xFFFFFFF9 로 PendSV 에 들어오는 순간이 증거 (`from_main` 카운터). 평소엔 1000사이클 틱 주기가 길어서 안 보이다가, 디버거로 멈춰 시간이 밀리자 드러났다 → BASEPRI 로 막고 SVC 에서 푼다. (2) stop 처리 중 SysTick 이 PendSV 를 다시 pend → main 복귀 직후 PendSV. 공통 교훈: **"PendSV 는 Thread+PSP 에서만 들어온다" 를 가정했다면 그걸 강제하거나 검사해야 한다.** 그 밖의 한계: 단일 코어 가정(SMP 에서는 `g_cur` 가 코어별), 태스크 간 MPU 영역 교체 없음, PendSV 진입~복귀 지연을 QEMU 로는 사이클 단위로 말할 수 없음.

</details>

<details>
<summary>Q8. (꼬리질문) ARMv8-M (Cortex-M33/M55, TrustZone) 으로 옮긴다면 뭘 바꾸겠나?</summary>

(문서 확인 권장) EXC_RETURN 에 보안 상태·스택 프레임 종류 비트가 늘어 0xFFFFFFBC 같은 값이 나오고 고정 패턴 검사가 달라진다; PSPLIM 을 태스크마다 저장/복원(스위처에 `mrs/msr psplim` 추가)하면 오버플로를 하드웨어가 즉시 잡는다; Secure 쪽 문맥을 쓰는 태스크는 Secure 스택 포인터도 관리해야 한다 (FreeRTOS 의 ARMv8-M 포트가 TCB 에 xSecureContext 를 둔다). M55/M85 의 MVE(Helium) 는 FP 확장 프레임에 VPR 이 추가된다 — QEMU 소스의 예외 복귀 FP 경로가 "S-registers, FPSCR, and VPR" 을 읽는 것도 그 때문이다.

</details>

<details>
<summary>Q9. 스위치 비용은 몇 사이클이야? 어떻게 재겠나?</summary>

이 레슨은 QEMU(TCG)라 사이클을 말할 수 없다 — icount 는 "명령 수 = 시간" 인 모델일 뿐이다. 명령 수로는 셀 수 있다: M3 스위치 경로 = PendSV 16명령(+`os_schedule`), 저장/복원 메모리 접근은 r4–r11 각 8워드. 실칩에서는 예외 진입/복귀 각 ~12사이클(+ wait state, FPU 확장 프레임이면 더) 이 ARM 문서의 출발점이고 (확인 권장), DWT CYCCNT 를 SysTick 진입과 새 태스크 첫 명령에서 읽어 잰다 (BM-A10). 줄이는 방법: `os_schedule` 을 asm 으로(테이블 한 번 로드), FP 문맥 없는 태스크는 bit4 덕분에 s16–s31 을 건너뜀, lazy stacking 유지.

</details>

## 9. 추가 연습

### 연습 1 — SVC 없이 첫 태스크 시작 (`os_start_direct`)

Thread 모드에서 PSP 로 전환해 첫 태스크로 "점프" 하라. main 복귀는 기존 PendSV 의 to-main 경로를 그대로 쓰도록 MSP 를 꾸며라.

<details>
<summary>답</summary>

`cpsid i` → MSP 에 가짜 하드웨어 프레임 8워드(PC = 복귀 라벨 `2:`, xPSR = 0x01000000) → 그 아래 `push {r3-r11, lr=0xFFFFFFF9}` (SVC 가 쌓는 모양과 동일) → `g_main_msp` → PSP = 첫 TCB sp, `CONTROL = 2`, `isb` → `ldmia sp!, {r4-r11}`, r0/lr/pc 를 프레임에서 읽고 `add sp, #32` 로 xPSR 칸까지 버림 → BASEPRI 0, `cpsie i`, `bx` (pc|1). 실측 `P1 direct start: ticks=200 switches=199 pendsv=200 A.mism=0 B.mism=0 A.win=99`, main 의 r4/r11 센티널 그대로, MSP·CONTROL 원래대로.

</details>

### 연습 2 — PENDSVSET 을 여러 번 쓰면?

SysTick 한 번에 `ICSR = PENDSVSET` 을 3번 쓰고 PendSV 진입 횟수를 세라.

<details>
<summary>답</summary>

`requests=600 pendsv entries=200` — pend 는 상태 비트 하나라서 몇 번 써도 한 번 실행된다. 그래서 RTOS 는 "깨운 태스크마다 PendSV" 같은 걱정 없이 마음껏 pend 한다. 반대로 "스위치 요청 횟수" 가 필요하면 별도 카운터가 있어야 한다.

</details>

### 연습 3 — 비특권 태스크

SVC 복귀 직전에 CONTROL.nPRIV = 1 로 태스크를 비특권으로 돌리고, main 복귀 경로에서 특권을 되돌려라. 비특권 태스크에서 `cpsid i` 는?

<details>
<summary>답</summary>

`probe CONTROL=0x00000003 PRIMASK after cpsid=0x00000000 main CONTROL after=0x00000000`. Handler 는 항상 특권이라 핸들러 안에서 `msr control` 로 Thread 권한을 바꿀 수 있고, 스위처 자체는 아무것도 안 바뀐다 (A, B mismatch 0). 비특권 `cpsid i` 는 무시된다(PRIMASK 0 그대로) — 그래서 검사 태스크의 `cpsid` 보호가 사라져도 정답 스위처는 여전히 0 이었다. 태스크마다 권한이 다르면 CONTROL 을 TCB 에 저장/복원하면 된다.

</details>

### 연습 4 — 스택 최고 수위

`tcb_setup` 이 칠한 0xA5A5A5A5 를 아래에서부터 훑어 처음 지워진 곳까지로 사용량을 구하라.

<details>
<summary>답</summary>

`P4 high-water: A=88 B=88 P=64 bytes`. 루프만 도는 P 는 저장 문맥 16워드(64 B)가 전부. 검사 태스크 최저점은 **검사 단계**(st·카운터 8 B + got 56 B + `rc_verify` 프레임)이지, "패턴 창 + 저장 문맥(8+4+64)" 이 아니다 — 검사 단계는 인터럽트를 막고 돌기 때문에 두 최대치가 겹치지 않는다. 갓 만든 초기 프레임만 있는 스택은 64 B.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # → 11/11 passed, qemu exit=0
```

## 10. 핵심 정리

- PendSV = 최저 우선순위 → 모든 ISR 이 끝난 뒤 Thread+PSP 에서만 진입 (lr 0xFFFFFFFD/ED). 높이면 ISR 의 레지스터를 태스크에 저장한다 (BUG2 실측).
- 하드웨어 8워드(r0–r3, r12, LR, PC, xPSR) + 소프트웨어 8워드(r4–r11) = 64 B/태스크. 초기 프레임·SVC·PendSV 세 곳의 모양이 같아야 한다.
- `init_stack`: 8 정렬, xPSR=0x01000000, PC=entry&~1, LR=exit trap, r0=arg, 바닥 카나리.
- `bl` 전 lr(EXC_RETURN) 보관, 스택 8 정렬. main 복귀 = SVC 가 쌓은 `{r3-r11, lr}` pop + `bx 0xFFFFFFF9`.
- 시작 경쟁: SysTick 켜기 전에 BASEPRI 로 막고 SVC 에서 풀기. stop 때 PENDSVCLR.
- M4F: EXC_RETURN bit4 로 `vstmdbeq/vldmiaeq {s16-s31}`, EXC_RETURN 은 태스크별. 이 FP 명령이 lazy 저장(s0–s15)을 실행시킨다 — 없으면 s0–s31 전부 새고 LSPACT 가 남는다.
- 검증은 "창 안에서 끊겼는가" 까지 세는 레지스터 패턴 검사로. `isb` 누락처럼 관찰 안 되는 것은 그렇다고 말하고 문서 근거로 판단.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5 예외 진입/복귀, EXC_RETURN, 무결성 검사(INVPC), FP lazy stacking(FPCCR/FPCAR). https://developer.arm.com/documentation/ddi0403/latest (문서 확인 권장: 이 레슨의 비트 위치·값은 기억 + QEMU 실측 기준)
- Arm, *Cortex-M4 Devices Generic User Guide* — SCB ICSR/SHPR3, FPCCR. https://developer.arm.com/documentation/dui0553/latest
- Arm Application Note 298, *Cortex-M4(F) Lazy Stacking and Context Switching* — vstmdb 로 lazy 저장을 촉발하는 스위처 패턴.
- QEMU 소스 `target/arm/tcg/m_helper.c` (`do_v7m_exception_exit`: INVPC 분기들, LSPACT 처리) — https://gitlab.com/qemu-project/qemu/-/blob/master/target/arm/tcg/m_helper.c (본문 인용은 v10.2.0 태그 기준으로 읽음)
- FreeRTOS-Kernel `portable/GCC/ARM_CM3/port.c`, `ARM_CM4F/port.c` (`xPortPendSVHandler`, `vPortSVCHandler`, `prvPortStartFirstTask`) — https://github.com/FreeRTOS/FreeRTOS-Kernel
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — OS 지원(PendSV/SVC) 장과 FPU(lazy stacking) 장.
- 관련 레슨: RTOS-A01 (C/인라인 asm 미니 RTOS, 스케줄러·PI), ARM-M09 (예외 프레임·EXC_RETURN·FP 프레임), ARM-M10 (PRIMASK/BASEPRI), ARM-M08 (VTOR), ARM-M12 (HardFault 분석), BM-A08 (MPU), BM-A10 (예외 지연 모델·DWT).

---
id: OS-B04
time: 70분
updated: 2026-11-08
---
# OS-B04 스레드 기초: pthread 생성과 공유 카운터 레이스

## 학습 목표

- `pthread_create` / `pthread_join` 으로 스레드를 만들고, 인자를 넘기고, 결과를 돌려받는다. 스레드마다 **따로 있는 것**(레지스터·PC·스택)과 **같이 쓰는 것**(코드·전역·힙·열린 파일, 그리고 남의 스택까지)을 주소를 찍어 확인한다.
- `counter++` 가 arm64 에서 `ldr / add / str`, Cortex-M4 에서 `ldr / adds / str` 세 명령이라는 것을 **실제 asm** 으로 보이고, 두 스레드가 그 사이에 끼어들 때 증가 하나가 사라지는 과정(lost update)을 표로 그린다.
- 실제 4 스레드 × 100만 번 실행에서 75% 가 사라지는 것과, `-O2` 에서는 **손실 0 으로 보이지만 고쳐진 게 아니라는 것** 을 asm 으로 설명한다.
- 결정적 인터리빙 시뮬레이터로 가능한 **모든** 실행 순서를 열거해, 2 스레드 × k 번 증가의 결과가 **2 까지** 내려갈 수 있음을 보인다.
- ThreadSanitizer 보고서를 처음 보고 "어디서·누가·무엇을" 세 줄을 읽는다. 단일 코어 MCU 에서는 같은 레이스가 **ISR 이 끼어드는 지점** 문제로 바뀌고, LDREX/STREX 와 인터럽트 끄기가 각각 어떻게 막는지 안다.

## 1. 문제

"`counter++` 는 원자적인가요?" — 펌웨어·시스템 면접의 첫 동시성 질문이다. 답은 "아니요, load–add–store 세 단계입니다" 한 줄이지만, 면접관은 곧바로 "그걸 어떻게 보여 줄 수 있죠? 왜 테스트에서는 잘 되죠? MCU 에서는요?" 로 넘어간다. 펌웨어에서 같은 버그는 **ISR 과 메인 루프**(C-M14), **FreeRTOS 태스크끼리**(RTOS-B05 의 tick 선점), **듀얼 코어 MCU**(RTOS-A08) 에서 나온다. Cortex-M0+ 처럼 LDREX 가 없는 코어에서는 인터럽트를 끄는 수밖에 없고, M3/M4 이상은 LDREX/STREX 로 막는다 (ARM-M11).

**스레드(thread)** 는 "같은 프로세스 안의 또 하나의 실행 흐름" 이다. 운영체제는 스레드마다 레지스터 세트(PC·SP 포함)와 스택만 따로 주고, 나머지 주소 공간은 통째로 같이 쓰게 한다 (OSTEP 26장). 그래서 전역 변수 하나를 여러 스레드가 아무 준비 없이 고칠 수 있다 — 편하고, 그래서 위험하다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 프로세스 = 주소 공간 하나 + 스레드 여러 개</text>
  <rect x="300" y="35" width="420" height="250" rx="6" fill="none" stroke="currentColor"/>
  <text x="310" y="54" fill="currentColor">주소 공간 (모든 스레드가 같은 주소로 본다)</text>
  <rect x="315" y="65" width="390" height="30" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="325" y="85" fill="currentColor">코드 (__TEXT) — worker() 기계어 하나를 셋이 같이 실행</text>
  <rect x="315" y="102" width="390" height="30" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="325" y="122" fill="currentColor">전역 (__DATA) — long counter;  ← 레이스의 무대</text>
  <rect x="315" y="139" width="390" height="30" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="325" y="159" fill="currentColor">힙 (malloc) — 포인터만 있으면 누구나 읽고 쓴다</text>
  <rect x="315" y="180" width="120" height="60" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="325" y="200" fill="currentColor">T0 스택</text>
  <text x="325" y="218" fill="currentColor" font-size="12">512 KiB</text>
  <rect x="450" y="180" width="120" height="60" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="460" y="200" fill="currentColor">T1 스택</text>
  <text x="460" y="218" fill="currentColor" font-size="12">512 KiB</text>
  <rect x="585" y="180" width="120" height="60" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="595" y="200" fill="currentColor">main 스택</text>
  <text x="595" y="218" fill="currentColor" font-size="12">8 MiB</text>
  <text x="315" y="268" fill="currentColor" font-size="12">주소를 넘기면 남의 스택도 고칠 수 있다 (demo_share.c)</text>
  <rect x="20" y="70" width="200" height="44" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="30" y="88" fill="currentColor">T0: PC, SP, x0..x30</text>
  <text x="30" y="105" fill="currentColor" font-size="12">자기 레지스터 세트</text>
  <rect x="20" y="130" width="200" height="44" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="30" y="148" fill="currentColor">T1: PC, SP, x0..x30</text>
  <text x="30" y="165" fill="currentColor" font-size="12">자기 레지스터 세트</text>
  <path d="M220 92 L313 117" stroke="currentColor" fill="none"/>
  <path d="M220 152 L313 117" stroke="currentColor" fill="none"/>
  <path d="M220 100 L373 180" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M220 160 L508 180" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="20" y="210" fill="currentColor" font-size="12">실선: 같은 counter 를 load/store</text>
  <text x="20" y="228" fill="currentColor" font-size="12">점선: SP 는 자기 스택을 가리킨다</text>
</svg>
```

| 스레드마다 따로 | 모든 스레드가 공유 |
|---|---|
| 레지스터(PC, SP, 범용), 스택(지역 변수·리턴 주소), `errno`(스레드 로컬), 시그널 마스크 | 코드, 전역/static 변수, 힙, 열린 파일 디스크립터, 시그널 핸들러, 현재 디렉터리, PID |

### Part A — pthread 생성과 공유 주소 공간

- `worker_main(arg)` — 출발선(`gate_wait`, 하네스 제공) 통과 → 자기 지역 변수 주소, `&g_magic` 과 그 값, 맡은 구간의 합, 힙 칸(`*heap_slot = id + 1`)을 자기 `worker_t` 에 기록 → `arg` 를 반환.
- `run_workers(w, n)` — n 개 스레드 생성 → 전부 join → join 이 돌려준 값이 `&w[i]` 인지 확인. 에러 코드 규칙은 2. Problem 참고.
- `sum_parallel(a, n, nthreads)` — 구간을 나눠(나머지는 앞 구간에 1 개씩) 병렬로 합한다. **공유 변수에 더하지 않고** 각자 자기 칸에 쓴 뒤 join 후 main 이 합친다.
- 확인: 모든 스레드가 같은 `&g_magic` 을 보고, 지역 변수 주소는 전부 다르며, 스레드가 힙에 쓴 값이 join 뒤 main 에 보인다.

### Part B — `counter++` 레이스: asm, 실제 실행, 모든 인터리빙

- `demo_asm.c` 로 `counter++` 의 arm64 `-O0`/`-O2` asm 과 Cortex-M4/M0+ asm 을 본다. `demo_race.c` 로 4 스레드 × 100만 번을 실제로 돌린다 (`-O0`, `-O2`, `-O2 -DVOL`).
- 시뮬레이터: 스레드 하나의 `counter++` 를 `LOAD → ADD → STORE` 세 단계로 쪼갠다. `vm_step(vm, t)` 를 구현하고, 스케줄 문자열(`"ABABAB"` = A, B 가 한 단계씩 번갈아)로 lost update 를 **결정적으로** 재현한다. 이건 하네스가 고른 **강제 스케줄** 이다 — 실제 OS 는 이런 순서를 "언젠가" 만든다.
- `explore()` 로 가능한 모든 인터리빙을 열거해 최종값 분포를 구한다. 원자 RMW(1 단계, arm64 `ldadd`) 모드에서는 모든 인터리빙이 정답인지 확인.
- `worst_schedule(k)` — 2 스레드 × k 번 증가인데 결과가 **2** 가 되는 스케줄을 만든다. 마지막으로 `demo_race.c` 를 TSan 으로 빌드해 보고서를 한 번 본다 (깊이 읽기는 TOOL-M09).

### Part C — 단일 코어 MCU: UART RX ISR vs main 루프

UART 수신 ISR 은 바이트가 올 때마다 `pending++`, main 루프는 바이트를 하나 처리할 때마다 `pending--`. 단일 코어라 둘이 "동시에" 돌지는 않지만, ISR 은 main 의 **명령과 명령 사이 아무 데나** 끼어든다.

- `mcu_main_take(kind, inject_at)` — main 의 `pending -= 1` 을 세 가지 방식으로 단계 실행하고, IRQ 를 `inject_at` 번째 단계 직전에 한 번 넣는다.
  - `RMW_PLAIN`: `LDR / SUBS / STR`
  - `RMW_LDREX`: `LDREX / SUBS / STREX` — ISR(예외 진입·복귀)이 로컬 모니터를 지우면 STREX 실패 → 다시
  - `RMW_IRQOFF`: `CPSID i / LDR / SUBS / STR / CPSIE i` — 막힌 동안의 IRQ 는 래치했다가 CPSIE 직후 실행
- 확인: PLAIN 은 LDR 과 STR 사이에 온 IRQ 의 증가를 잃는다. LDREX 는 어디서 와도 정답(재시도 1 회), IRQOFF 도 정답이지만 ISR 이 최대 4 단계 늦어진다.

## 2. Problem (English)

> **Threads share an address space — show it, break a shared counter, and explain the break at instruction level.**
>
> 1. `void *worker_main(void *arg)` and `int run_workers(worker_t *w, int n)`: start `n` threads (1 ≤ n ≤ 16, else return `EINVAL`), each running `worker_main(&w[i])`, join them all, and verify that each thread's return value is `&w[i]` (else return -1). If `pthread_create` fails, join the threads already created and return its error code. Each worker records the address of one of its locals, `&g_magic` and its value, the sum of its slice, and writes `id + 1` to its heap slot. `uint64_t sum_parallel(const uint32_t *a, size_t n, int nthreads)` must equal the serial sum for any `n` and 1–8 threads, without any shared accumulator.
> 2. Model `counter++` as three steps LOAD/ADD/STORE. Implement `bool vm_step(vm_t *vm, int t)` (also a one-step atomic mode), enumerate every interleaving with `explore()`, and build `worst_schedule(k, out)` so that two threads doing `k ≥ 2` increments each end with `counter == 2`.
> 3. Single-core MCU: main does `pending -= 1` while a UART RX ISR does `pending += 1`. Simulate an interrupt arriving before any given step for (a) plain LDR/SUBS/STR, (b) LDREX/STREX retry loop where exception entry/return clears the local monitor, (c) PRIMASK critical section with a latched IRQ. Report the final value, retries and ISR delay.
>
> Example: 2 threads × 1 increment have 20 interleavings; only the 2 serial ones give 2. 2 threads × 2 increments: 924 interleavings, min 2, max 4.
>
> Follow-ups: Why did `-O2` show zero lost updates? Is `volatile` a fix? What exactly is a data race in C11? Why does TSan report a race between a thread that already *finished* and a new one? On a Cortex-M0+, how do you make `pending--` safe?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 멈추지 않고 끝난다):

```text
FAIL starter.c:220  same_global
FAIL starter.c:221  seen
… (Part A·B 의 나머지 FAIL 18 줄과 info 2 줄 생략)
FAIL starter.c:290  worst_ok
FAIL starter.c:298  plain[1] == 4u && plain[2] == 4u
FAIL starter.c:305  ldrex_ok
FAIL starter.c:307  m1.retries == 1 && m1.main_steps == 6
FAIL starter.c:308  m0.retries == 0 && m0.main_steps == 3
FAIL starter.c:315  off_ok
FAIL starter.c:316  mcu_main_take(RMW_IRQOFF, 1).isr_delay == 4
6/33 passed
```

하네스 설계: 실제 스레드를 쓰는 Part A 는 **각 스레드가 자기 칸에만 쓰므로** 매 실행 결과가 같다. 레이스(실행마다 다른 값)는 CHECK 하지 않고 `demo_race.c` 에서 눈으로 본다. 출발선 `gate_wait` 은 2초 타임아웃이 있어서 스레드 수를 잘못 세도 멈추지 않고 FAIL 한다 (`pthread_barrier_t` 는 macOS 에 없어서 mutex + cond 로 만들었다 — 조건 변수 자체는 OS-B06).

관찰용 파일 (하네스와 별개, 빌드 명령은 각 파일 첫 주석에도 있다):

```bash
cc -std=c11 -O2 -pthread demo_share.c -o share && ./share                       # 주소 공간 공유
cc -std=c11 -O2 -S demo_asm.c -o -                                              # arm64 (-O0 도)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -   # M4 (thumbv6m/cortex-m0plus 도)
cc -std=c11 -O0 -pthread demo_race.c -o race_o0 && ./race_o0 [nthreads] [iters]  # -O2, -O2 -DVOL 도
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_race.c -o race_t && ./race_t   # TSan
cc -std=c11 -O2 -pthread demo_argbug.c -o argbug && ./argbug                     # 연습 1 의 버그
```

## 4. 힌트

<details>
<summary>힌트 1 — pthread_create 의 반환값과 인자</summary>

`pthread_create(&tid, NULL, fn, arg)` 는 성공하면 0, 실패하면 **에러 번호를 반환** 한다 (`errno` 를 쓰지 않는다 — `perror` 로 찍으면 엉뚱한 메시지). `arg` 는 스레드가 살아 있는 동안 유효해야 하고, 스레드마다 **다른** 메모리를 가리켜야 한다: `&w[i]` 는 되고 루프 변수 `&i` 는 안 된다 (연습 1).

</details>

<details>
<summary>힌트 2 — 결과 돌려받기와 "join 뒤에는 보인다"</summary>

스레드 함수가 `return p;` 하면 `pthread_join(tid, &ret)` 의 `ret` 으로 나온다. join 은 동기화 지점이다: 스레드가 끝나기 전에 쓴 모든 것이 join 이 돌아온 뒤 main 에 보인다 (POSIX 메모리 동기화 목록에 `pthread_create`, `pthread_join` 이 있다). 그래서 Part A 는 원자 변수도 락도 필요 없다. 실패 경로에서는 이미 만든 스레드를 join 하지 않으면 그 스레드가 아직 `w[]` 를 쓰는 중에 함수가 반환한다.

</details>

<details>
<summary>힌트 3 — vm_step 은 pc 로 세 갈래</summary>

`pc == ST_LOAD` 면 `reg = mem`, `ST_ADD` 면 `reg += 1` (메모리는 안 건드린다), `ST_STORE` 면 `mem = reg` 하고 `left--`, `pc` 를 처음으로. 레이스는 "LOAD 와 STORE 사이에 다른 스레드의 STORE 가 끼는 것" 이고, ADD 가 레지스터에서만 일어난다는 게 핵심이다.

</details>

<details>
<summary>힌트 4 — 모든 인터리빙 = 매 단계 "누구를 돌릴까" 로 갈라지는 트리</summary>

상태(`vm_t`)를 **값으로 복사** 해서 아직 안 끝난 스레드마다 한 단계 진행한 복사본으로 재귀한다. 아무도 진행할 수 없으면 잎(leaf) — `hist[mem]++`. 2 스레드 × 3 단계면 잎은 6!/(3!·3!) = 20 개.

</details>

## 5. 풀이

### Part A — pthread_create / pthread_join, 그리고 공유 주소 공간

`run_workers` 의 뼈대 (전체는 6. 정답 코드): `for` 로 `pthread_create(&tid[made], NULL, worker_main, &w[made])` — 0 이 아니면 그 값을 에러로 들고 멈춤 → 만든 `made` 개를 **전부** join 하며 `ret == &w[i]` 확인.

| 포인트 | 설명 |
|---|---|
| 인자는 `&w[i]` | 스레드마다 다른 칸 → 인자 읽기에 레이스 없음. `&i` 를 넘기면 main 의 `i++` 와 레이스 (연습 1, 실제 출력 있음) |
| 결과는 자기 칸에 | `w->sum`, `*w->heap_slot` 은 그 스레드만 쓴다. 공유 누산기 `total += s` 를 스레드 안에서 하면 그게 바로 Part B 의 레이스 |
| join = 동기화 | POSIX.1-2017 XBD 4.12 (Memory Synchronization): `pthread_create`, `pthread_join`, 뮤텍스 lock/unlock, cond wait/signal 등은 메모리를 동기화한다. create 전 main 의 `g_magic = 0xC0FFEE` 는 새 스레드에 보이고, 스레드의 쓰기는 join 뒤 main 에 보인다. C11 로 말하면 이 함수들이 happens-before 를 만든다 |
| 출발선 | 앞 스레드가 끝난 뒤 다음 스레드가 만들어지면 **스택이 재사용** 되어 지역 변수 주소가 같게 나올 수 있다. gate 로 n 개가 동시에 살아 있게 한 뒤 주소를 찍는다 |

`sum_parallel` 은 이 레슨의 "올바른 병렬화" 다: 공유 쓰기를 **아예 없애고**(분할, partition), 합치기는 join 뒤 한 스레드가 한다. 락도 원자 연산도 없이 1~8 스레드 모두 정확하다. (매번 합치지 않고 스레드별 누적을 가끔 전역에 보내는 sloppy counter 는 OS-B05.)

`demo_share.c` 실제 출력 (실행마다 주소는 다르다 — ASLR, OS-B08):

```text
main: &g_global=0x1043e0000  g_heap=0x104c55150  &main_local=0x16ba26b2c  stack=8176 KiB
T3: &g_global=0x1043e0000  g_heap=0x104c55150  &local=0x16bbc6f8c  stack=524 KiB
T1: &g_global=0x1043e0000  g_heap=0x104c55150  &local=0x16baaef8c  stack=524 KiB
T2: &g_global=0x1043e0000  g_heap=0x104c55150  &local=0x16bb3af8c  stack=524 KiB
main: main_local=7777 (T1 이 고침)  g_heap = {0, 100, 200, 300}
```

- 전역·힙 주소는 네 줄 모두 같다. 지역 변수는 `0x...aef8c`, `0x...b3af8c`, `0x...bc6f8c` — 간격 `0x8c000` = 560 KiB ≥ 기본 스택 512 KiB(`pthread_attr_getstacksize` 기본값 524288, 실측) + 가드 16 KiB(`pthread_attr_getguardsize`). main 스레드는 8 MiB.
- **T1 이 main 의 지역 변수를 고쳤다.** 스택도 같은 주소 공간이라 포인터만 넘기면 된다. 보호는 없다 — 펌웨어에서 태스크 스택 오버플로가 남의 스택을 조용히 망가뜨리는 이유와 같다 (RTOS-M09, BM-A08 MPU 스택 가드). `T3` 가 먼저 찍힌 것처럼 실행 순서는 생성 순서와 무관하다.

### Part B — `counter++` 는 세 명령이다

`demo_asm.c` 의 `void inc(void) { counter++; }` (`unsigned counter;`). 실제 출력에서 지시어(`.cfi_*`, `.p2align` 등)만 지웠다:

```text
arm64 -O2 (-O0 도 같은 세 명령)              Cortex-M4 -O2                  Cortex-M0+ -O2
_inc:                                         inc:                           inc:
  adrp x8, _counter@GOTPAGE                     movw r0, :lower16:counter      ldr  r0, .LCPI0_0
  ldr  x8, [x8, _counter@GOTPAGEOFF]            movt r0, :upper16:counter      ldr  r1, [r0]
  ldr  w9, [x8]          ; ① LOAD               ldr  r1, [r0]    ; ①           adds r1, r1, #1
  add  w9, w9, #1        ; ② ADD (레지스터)      adds r1, #1      ; ②           str  r1, [r0]
  str  w9, [x8]          ; ③ STORE              str  r1, [r0]    ; ③           bx   lr
  ret                                           bx   lr
```

(`-O0` 은 레지스터 번호만 `w8/x9` 로 다르고 같은 세 명령이다.) 어느 ISA 든 ① 메모리 → 레지스터, ② 레지스터에서 더하기, ③ 레지스터 → 메모리. ①과 ③ 사이에 다른 스레드(또는 ISR)가 같은 주소에 쓰면, ③ 이 그 값을 **옛 값 + 1** 로 덮는다.

**lost update 인터리빙** (counter = 0 에서 시작, 시간은 위 → 아래):

| 시각 | 스레드 A (코어 0) | 스레드 B (코어 1) | counter (메모리) | A 의 w9 | B 의 w9 |
|---|---|---|---|---|---|
| t1 | `ldr w9, [x8]` | | 0 | 0 | — |
| t2 | | `ldr w9, [x8]` | 0 | 0 | 0 |
| t3 | `add w9, w9, #1` | | 0 | 1 | 0 |
| t4 | | `add w9, w9, #1` | 0 | 1 | 1 |
| t5 | `str w9, [x8]` | | **1** | 1 | 1 |
| t6 | | `str w9, [x8]` | **1** ← B 가 A 의 결과를 덮음 | 1 | 1 |

두 번 증가했는데 1. 레지스터는 스레드마다 따로라서(1. 문제 표) 각자 "0 을 읽었다" 는 사실을 들고 있다가 쓴다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">LOAD 와 STORE 사이 = 위험 구간. 두 구간이 겹치면 증가 하나가 사라진다</text>
  <text x="10" y="62" fill="currentColor">A (코어 0)</text>
  <text x="10" y="122" fill="currentColor">B (코어 1)</text>
  <rect x="100" y="45" width="90" height="28" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="112" y="64" fill="currentColor" font-family="monospace">ldr 0</text>
  <rect x="250" y="45" width="90" height="28" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="262" y="64" fill="currentColor" font-family="monospace">add →1</text>
  <rect x="400" y="45" width="90" height="28" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="412" y="64" fill="currentColor" font-family="monospace">str 1</text>
  <rect x="175" y="105" width="90" height="28" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="187" y="124" fill="currentColor" font-family="monospace">ldr 0</text>
  <rect x="325" y="105" width="90" height="28" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="337" y="124" fill="currentColor" font-family="monospace">add →1</text>
  <rect x="475" y="105" width="90" height="28" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="487" y="124" fill="currentColor" font-family="monospace">str 1</text>
  <rect x="100" y="80" width="390" height="10" class="f-warn"/>
  <rect x="175" y="140" width="390" height="10" class="f-warn"/>
  <text x="590" y="64" fill="currentColor">A 의 위험 구간</text>
  <text x="590" y="124" fill="currentColor">B 의 위험 구간</text>
  <path d="M100 175 L700 175" stroke="currentColor"/>
  <path d="M690 170 L700 175 L690 180" stroke="currentColor" fill="none"/>
  <text x="650" y="195" fill="currentColor" font-size="12">시간</text>
  <text x="10" y="200" fill="currentColor">counter: 0 ─────────────── 1 (A) ─── 1 (B: 덮어씀)   기대 2, 실제 1</text>
  <text x="10" y="220" fill="currentColor" font-size="12">원자 RMW(arm64 ldadd, M4 ldrex/strex 루프)는 이 구간을 "한 덩어리" 로 만든다 → OS-B05</text>
</svg>
```

**실제 실행** — `demo_race.c`, 4 스레드 × 100만 번. 머신: Apple M6 (`sysctl hw.perflevel0/1/2.physicalcpu` = Super 2 / Performance 4 / Efficiency 6, 12 코어), `Apple clang version 21.0.0 (clang-2100.0.123.102)`.

```text
== -O0 (5회)
threads=4 iters=1000000 expect=4000000 got=1006186 lost=2993814 (74.8%)
threads=4 iters=1000000 expect=4000000 got=1046559 lost=2953441 (73.8%)
threads=4 iters=1000000 expect=4000000 got=1010147 lost=2989853 (74.7%)
threads=4 iters=1000000 expect=4000000 got=1000751 lost=2999249 (75.0%)
threads=4 iters=1000000 expect=4000000 got=1005604 lost=2994396 (74.9%)
== -O2 (5회)
threads=4 iters=1000000 expect=4000000 got=4000000 lost=0 (0.0%)      (5회 모두 같음)
== -O2 -DVOL (5회)
threads=4 iters=1000000 expect=4000000 got=1022000 lost=2978000 (74.5%)
threads=4 iters=1000000 expect=4000000 got=1016020 lost=2983980 (74.6%)
threads=4 iters=1000000 expect=4000000 got=1000000 lost=3000000 (75.0%)
threads=4 iters=1000000 expect=4000000 got=1013991 lost=2986009 (74.7%)
threads=4 iters=1000000 expect=4000000 got=1599131 lost=2400869 (60.0%)
== -O0, 4 스레드 × 1000 번 (5회)
threads=4 iters=1000 expect=4000 got=4000 lost=0 (0.0%)              (5회 모두 같음)
```

`-O0` 에서 스레드 수만 바꾼 실행 (각 100만 번, 실제 값):

| 스레드 | 기대 | got (실행별) | 손실 |
|---|---|---|---|
| 1 | 1000000 | 1000000 | 0% |
| 2 | 2000000 | 1026560 / 1021781 | 48.7% / 48.9% |
| 8 | 8000000 | 1287771 / 1541124 | 83.9% / 80.7% |

읽는 법:

1. **`-O0` 은 거의 "한 스레드 분량" 만 남는다** (4 스레드면 ~25%, 2 스레드면 ~51%). 각 코어가 촘촘한 루프에서 `ldr/add/str` 를 반복하면 대부분의 STORE 가 다른 코어의 옛 값 위에 덮인다. 정확한 비율은 캐시 라인이 코어 사이를 오가는 타이밍에 달려 있어서 아무 보장이 없다 (캐시 일관성 비용은 MEM-M04, ARCH-M08).
2. **`-O2` 는 손실 0 — 그런데 고쳐진 게 아니다.** `worker` 의 `-O2` asm (실제 출력):

    ```text
    _worker:
        adrp x8, _iters@PAGE
        ldr  x8, [x8, _iters@PAGEOFF]
        cmp  x8, #1
        b.lt LBB1_2
        adrp x9, _counter@PAGE
        ldr  x10, [x9, _counter@PAGEOFF]   ; counter 를 한 번 읽고
        add  x8, x8, x10                   ; + iters (100만) 을 한 번에
        str  x8, [x9, _counter@PAGEOFF]    ; 한 번 쓴다
    ```

    clang 이 100만 번의 `counter++` 를 `counter += iters` 하나로 합쳤다. 다른 스레드가 볼 수 없다고 가정해도 되기 때문이다 — 레이스가 있는 프로그램은 UB 이므로 컴파일러는 "다른 스레드가 끼어들지 않는다" 고 가정할 자격이 있다 (C11 5.1.2.4p25, 아래). 위험 구간이 스레드당 ldr~str 세 명령으로 줄었고, 다음 스레드가 생성되는 간격보다 훨씬 짧아서 우연히 안 겹친 것뿐이다. 반복 횟수가 런타임 값이 아니거나 루프 안에 함수 호출이 있으면 이 합치기가 사라지고 버그가 다시 보인다.
3. **`volatile` 은 고치지 못한다.** `-DVOL` 의 `-O2` 루프는 `LBB1_2: ldr x10,[x9,_counter@PAGEOFF] / add x10,x10,#1 / str x10,[x9,...] / subs x8,x8,#1 / b.ne LBB1_2` (실제 출력) — 매 반복 메모리에 접근하도록 되돌렸을 뿐, 세 명령 사이의 틈은 그대로다 (C-B09). 결과도 `-O0` 과 같이 ~75% 손실.
4. **1000 번이면 손실 0.** 스레드 하나가 1000 번을 끝내는 시간이 다음 스레드 생성 시간보다 짧아서 실행이 겹치지 않았다. "테스트에서 한 번도 안 깨졌다" 가 증거가 못 되는 이유다.

**C11 의 정의** — 두 식의 평가가 같은 메모리 위치에 접근하고 그중 하나 이상이 쓰기이면 **충돌(conflict)** 한다 (5.1.2.4p4). 서로 다른 스레드의 충돌하는 두 동작 중 하나 이상이 원자 연산이 아니고 어느 쪽도 다른 쪽보다 먼저 일어나지(happens before) 않으면 **데이터 레이스(data race)** 이고, 동작은 **정의되지 않는다**(UB) (5.1.2.4p25). 그래서 "가끔 하나 덜 센다" 가 아니라 "어떤 결과도 가능하다" 가 정확한 답이다 — `-O2` 의 합치기도 그 자유의 한 예다.

**모든 인터리빙 열거** (solution 실행의 info 줄 — 2×2: `final 2:702 3:216 4:6`, 3×1: `final 1:1404 2:270 3:6`, 6. 정답 코드의 실제 출력):

| 경우 | 인터리빙 수 | 정답이 나오는 수 | 최소 결과 |
|---|---|---|---|
| 2 스레드 × 1 번 | 6!/(3!·3!) = 20 | 2 (직렬 AAABBB, BBBAAA) | 1 |
| 2 스레드 × 2 번 | 12!/(6!·6!) = 924 | 6 | **2** |
| 3 스레드 × 1 번 | 9!/(3!)³ = 1680 | 6 (= 3! 직렬 순서) | 1 |
| 3 스레드 × 2 번, 원자 RMW 1 단계 | 6!/(2!)³ = 90 | **90 (전부)** | 6 |

- 정답은 "증가 하나하나가 통째로 직렬화된" 순서에서만 나온다. 단계 단위로 고르게 섞으면 대부분이 틀린다. 실제 실행의 손실률이 이 비율과 다른 건 스레드가 한 번 CPU 를 잡으면 긴 구간을 연달아 실행하기 때문이다 — 단계 단위로 고르게 섞이지 않는다.
- **최소값은 2** — 면접 퍼즐 "두 스레드가 각각 n 번 `count++` 하면 가능한 최종값의 범위는?" 의 답 [2, 2n]. `worst_schedule` 이 그 증거다:

```c
*p++ = 'A';                                         /* A: LOAD 0 하고 멈춤 */
for (int i = 0; i < 3 * (k - 1); i++) *p++ = 'B';   /* B: k-1 번 온전히 증가 → mem = k-1 */
*p++ = 'A'; *p++ = 'A';                             /* A: ADD, STORE 1 → B 의 일을 덮음 */
*p++ = 'B';                                         /* B: LOAD 1 하고 멈춤 */
for (int i = 0; i < 3 * (k - 1); i++) *p++ = 'A';   /* A: 나머지 k-1 번 → mem = k */
*p++ = 'B'; *p++ = 'B';                             /* B: ADD, STORE 2 → A 의 일을 덮음 */
```

  1 이 안 되는 이유: 모든 STORE 는 (읽은 값 + 1) ≥ 1 을 쓰므로 첫 STORE 이후 메모리는 항상 ≥ 1 이다. 마지막 STORE 를 하는 스레드는 k ≥ 2 라서 자기 첫 STORE **뒤에** 마지막 LOAD 를 한다 → ≥ 1 을 읽고 ≥ 2 를 쓴다. (k = 1 이면 1 이 가능 — 2 스레드 × 1 번 표.)

### Part B — ThreadSanitizer 첫 만남

```bash
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_race.c -o race_t && ./race_t ; echo "exit=$?"
```

```text
==================
WARNING: ThreadSanitizer: data race (pid=15262)
  Write of size 8 at 0x0001020a8008 by thread T4:
    #0 worker demo_race.c:23 (OS-B04_race_t:arm64+0x1000008bc)

  Previous write of size 8 at 0x0001020a8008 by thread T3:
    #0 worker demo_race.c:23 (OS-B04_race_t:arm64+0x1000008bc)

  Location is global 'counter' at 0x0001020a8008 (OS-B04_race_t+0x100008008)

  Thread T4 (tid=5895553, running) created by main thread at:
    #0 pthread_create <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64e+0x335b8)
    #1 main demo_race.c:33 (OS-B04_race_t:arm64+0x10000076c)

  Thread T3 (tid=5895552, finished) created by main thread at:
    …
SUMMARY: ThreadSanitizer: data race demo_race.c:23 in worker
==================
threads=4 iters=1000000 expect=4000000 got=3000000 lost=1000000 (25.0%)
ThreadSanitizer: reported 1 warnings
exit=134
```

지금은 세 줄만 읽는다:

| 줄 | 뜻 |
|---|---|
| `Write of size 8 ... by thread T4` / `#0 worker demo_race.c:23` | **지금** 접근: T4 가 23행(`counter++`)에서 8 바이트(`long`) 쓰기 |
| `Previous write ... by thread T3` | **앞선** 충돌 접근: T3 의 같은 줄. 둘 다 쓰기 → 충돌 |
| `Location is global 'counter'` | 어떤 변수인가 — 전역 `counter` |

- **T3 는 `finished` 인데도 보고된다.** TSan 은 "실제로 동시에 실행됐나" 가 아니라 **happens-before 가 있었나** 를 본다. main 이 T3 를 join 하기 전에 T4 를 만들었으므로 T3 의 쓰기와 T4 의 쓰기 사이에 순서를 정하는 동기화가 없다 → C11 데이터 레이스. `./race_t 2 1000` 처럼 결과가 맞게 나온 실행(`got=2000`)도 같은 보고를 내고 exit 134 였다 (실측). 결과가 맞아도 레이스는 레이스다.
- 이 맥의 TSan 은 보고가 있으면 끝에서 abort 해 **exit 134** — CI 에서 실패로 잡힌다. `-g -O0` 빌드도 같은 `Write ... Previous write` 보고를 냈다.
- 스택 전부 읽기, `Read` vs `Write` 조합, 뮤텍스 생성 위치, `TSAN_OPTIONS`, 억제(suppression), 비용(5~15배)과 못 잡는 것은 **TOOL-M09** 에서 한다.

### Part C — 단일 코어 MCU: ISR 은 "어디서" 끼어드나

단일 코어에서는 진짜 동시 실행이 없다. 대신 ISR 이 main 의 **명령 경계 아무 데나** 끼어들고, 끝날 때까지 main 은 멈춘다. 그래서 질문이 "겹치나?" 에서 "**어느 지점** 에서 끼어들면 깨지나?" 로 바뀐다.

| 방식 | 주입 0 | 주입 1 | 주입 2 | 주입 3 | 비용 |
|---|---|---|---|---|---|
| PLAIN `LDR/SUBS/STR` | 5 | **4** | **4** | 5 | 없음 — 대신 틀림 |
| `LDREX/SUBS/STREX` 루프 | 5 | 5 (재시도 1, 6 단계) | 5 (재시도 1) | 5 | 끼어들었을 때만 재실행 |
| `CPSID/…/CPSIE` | 5 | 5 (ISR 지연 4) | 5 (지연 3) | 5 (지연 2) | 모든 IRQ 의 지연이 임계 구역 길이만큼 늘어남 |

- **PLAIN** 의 1·2 지점이 Part B 의 표와 같은 구조다: main 이 5 를 읽음 → ISR 이 6 으로 → main 이 4 를 씀. ISR 의 바이트 하나가 "도착하지 않은 것" 이 된다 — UART 링 버퍼라면 데이터는 있는데 카운트가 모자라서 마지막 바이트를 영영 처리하지 않는 버그.
- **LDREX/STREX**: ARMv7-M 은 예외 진입/복귀 때 로컬 배타 모니터를 지운다(C-M14 에서 다룬 동작). 그래서 ISR 이 끼어든 뒤의 STREX 는 반드시 실패하고 다시 읽는다. 인터럽트를 막지 않으므로 ISR 지연이 0 이다. `_Atomic` 변수에 `atomic_fetch_add` 를 M4 로 컴파일하면 실제로 이 루프가 나온다 (`demo_asm.c` 의 `inc_atomic`, 실제 출력: `ldrex r1,[r0] / adds r1,#1 / strex r2,r1,[r0] / cmp r2,#0 / bne .LBB3_1`). 모니터·리저베이션 그래뉼 상세는 ARM-M11, AArch64 판 LDXR/STXR 은 ARM-M04.
- **M0+ 에는 LDREX 가 없다.** 같은 `inc_atomic` 이 M0+ 에서는 `bl __atomic_fetch_add_4` 라이브러리 호출이 되고 clang 이 `-Watomic-alignment` 경고("max lock-free size (0 bytes)")를 냈다 (실측). M0+ 에서는 IRQOFF(PRIMASK 저장·복원, ARM-M10) 가 기본 해법이다.
- **IRQOFF 의 비용**: 막힌 동안 들어온 IRQ 는 NVIC 에 pending 으로 남았다가 CPSIE 직후 실행된다. 정확하지만 시스템 전체의 인터럽트 지연이 늘어난다 — 그래서 임계 구역은 몇 명령으로 짧게. 그리고 **다른 코어는 못 막는다** (RTOS-A08, C-A03).
- **FreeRTOS 태스크끼리** 도 같은 그림이다. 태스크 A 의 LDR 과 STR 사이에 SysTick 이 와서 태스크 B 로 전환되면(RTOS-B05) B 의 증가가 덮인다. `taskENTER_CRITICAL()` 은 Cortex-M3+ 포트에서 BASEPRI 를 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 로 올려, 그보다 급하지 않은(우선순위 숫자가 같거나 큰) 인터럽트만 막는다. 더 급한 인터럽트는 계속 들어오므로 그런 ISR 은 FreeRTOS API 를 부르면 안 된다.

### 고치는 법 — 미리보기 (OS-B05 에서 측정)

| 방법 | 핵심 | 어디서 |
|---|---|---|
| 분할(partition) | 공유 쓰기를 없앤다. 스레드별 칸 → join 뒤 합산 | 이 레슨 Part A `sum_parallel` |
| 뮤텍스 | LOAD~STORE 를 임계 구역으로 | OS-B05 |
| 원자 RMW | `atomic_fetch_add` → arm64 `ldadd` 한 명령, M4 `ldrex/strex` 루프 | OS-B05, C-A03 (memory_order 는 C-A03 이 담당) |
| sloppy counter | 스레드별 누적 + 임계값마다 전역에 반영 | OS-B05 (OSTEP 29장) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
```

실제 출력 (info 의 주소는 실행마다 다르다. 8 번 연속 실행 모두 `33/33 passed`):

```text
info: &g_magic=0x1004a8000  T0 local=0x16f9e6f90  T1 local=0x16fa72f90  (stack gap 560 KiB)
info: 2 threads x 2 incs: 924 interleavings -> final 2:702 3:216 4:6
info: 3 threads x 1 inc : 1680 interleavings -> final 1:1404 2:270 3:6
33/33 passed
```

새니타이저는 둘 다 깨끗하다. TSan 이 조용한 이유: Part A 의 공유 쓰기는 전부 "create 전 main 이 씀 → 스레드가 읽음" 또는 "스레드가 자기 칸에 씀 → join 뒤 main 이 읽음" 이고, gate 의 카운터는 뮤텍스 안에서만 바뀐다.

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan   # 33/33, exit 0
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan                                     # 33/33, 보고 없음, exit 0
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `pthread_create(&t[i], NULL, f, &i)` | 모든 스레드가 같은 `i` 를 가리키고, main 의 `i++` 와 레이스. 루프가 끝나면 `i` 의 수명도 끝난다 (실제 출력: 연습 1) | 스레드별 인자 배열 `&arg[i]`, 또는 `(void *)(intptr_t)i` |
| `if (pthread_create(...) == -1) perror(...)` | 실패는 0 이 아닌 **에러 번호 반환**, errno 를 설정하지 않는다 | `int e = pthread_create(...); if (e) fprintf(stderr, "%s\n", strerror(e));` |
| 스레드 안에서 `g_total += my_sum;` | 공유 누산기 = Part B 의 레이스 | 자기 칸에 쓰고 join 뒤 합산 (또는 OS-B05 의 락/원자) |
| `return &result;` (지역 변수) | 스레드가 끝나면 그 스택은 사라진다 → join 한 쪽이 매달린 포인터를 읽음 | 힙에 담아 반환하고 받은 쪽이 `free` (연습 4), 또는 인자 구조체에 쓰기 |
| "1000 번 돌려 봤는데 맞았다" | 스레드 생성 시간이 일보다 길면 실행이 안 겹친다 (실측: 4×1000 은 5회 모두 정답) | 반복을 키우고, **TSan** 으로 확인 — TSan 은 결과가 맞아도 레이스를 보고한다 |
| `-O2` 에서 잘 되니 OK | 컴파일러가 루프를 `+= iters` 로 합쳐 틈을 줄였을 뿐. UB 위에서 우연 | 동기화를 넣는다. 최적화 수준으로 판단하지 않는다 |
| `volatile long counter;` 로 해결 | volatile 은 접근을 지우지 않게 할 뿐, ldr/add/str 사이 틈은 그대로 (실측 75% 손실) | 원자 RMW 또는 락 (C-B09, OS-B05) |
| 단일 코어니까 안전 | ISR·태스크 전환이 LDR 과 STR 사이에 온다 (Part C) | LDREX/STREX(`_Atomic`), 짧은 임계 구역 |

## 8. 면접 Q&A

<details>
<summary>Q1. counter++ 는 왜 원자적이지 않나요? 한 줄 C 인데요.</summary>

한 줄 C 가 한 명령이 아니다. 메모리에 직접 더하는 명령이 없는 load/store 아키텍처(ARM, RISC-V)에서는 `ldr` → `add` → `str` 세 명령이다 (실측: arm64 `ldr w9,[x8] / add w9,w9,#1 / str w9,[x8]`, Cortex-M4 `ldr / adds / str`). 두 스레드가 둘 다 `ldr` 로 같은 값을 읽은 뒤 각자 +1 해서 쓰면 증가 하나가 사라진다. 꼬리: "x86 의 `incl (mem)` 은 한 명령인데요?" → 한 명령이어도 내부적으로는 읽기-수정-쓰기라 멀티코어에서는 `lock` 접두사가 있어야 원자적이다. 그리고 C 의 `counter++` 가 그 명령으로 컴파일된다는 보장도 없다 — C11 기준으로는 원자 타입이 아닌 이상 데이터 레이스 = UB.

</details>

<details>
<summary>Q2. 4 스레드 × 100만 번이면 결과가 얼마나 나오나요? 최소값은?</summary>

실측 `-O0` 에서 약 100만 (75% 손실), `-O2` 에서는 400만 (합쳐진 루프가 우연히 안 겹침). 이론적 범위는 정답 400만부터 **2** 까지다: 한 스레드가 0 을 읽고 멈춘 사이 다른 스레드들이 일을 하고, 멈췄던 스레드가 1 을 써서 전부 지우는 식의 스케줄을 끝까지 이어 가면 된다. 2 스레드 × 2 번 924 개 인터리빙을 전부 열거하면 최소 2, 2 스레드 × k 번에서 2 를 만드는 스케줄도 직접 만들었다 (`worst_schedule`). 꼬리: "왜 1 은 안 되죠?" → STORE 는 항상 ≥ 1 을 쓰므로 첫 STORE 이후 메모리는 ≥ 1. 마지막에 쓰는 스레드는 자기 첫 STORE 뒤에 마지막 LOAD 를 하므로(k ≥ 2) ≥ 1 을 읽고 ≥ 2 를 쓴다.

</details>

<details>
<summary>Q3. volatile 을 붙이면 해결되나요?</summary>

아니다. volatile 은 "이 접근을 지우거나 합치지 마라" 만 보장한다. `-O2 -DVOL` asm 은 매 반복 `ldr/add/str` 로 돌아왔고, 결과는 75% 손실로 `-O0` 과 같았다. volatile 은 원자성도, 스레드 간 순서도 주지 않는다 (C-B09). C11 에서 volatile 접근끼리의 경쟁도 데이터 레이스다. 고치는 것은 `_Atomic` + `atomic_fetch_add`, 또는 뮤텍스 (OS-B05). 꼬리: "그럼 volatile 은 언제?" → MMIO 레지스터, 시그널 핸들러와 공유하는 `volatile sig_atomic_t` 플래그 (C-M14).

</details>

<details>
<summary>Q4. 스레드끼리 공유하는 것과 따로 가지는 것은?</summary>

따로: 레지스터(PC, SP 포함), 스택, 스레드 로컬 저장소(`errno`, `_Thread_local`), 시그널 마스크. 공유: 코드, 전역/static, 힙, 파일 디스크립터, 시그널 핸들러, 작업 디렉터리. 주의할 점은 **스택도 같은 주소 공간** 이라는 것 — 주소를 넘기면 다른 스레드가 쓸 수 있다(실측: T1 이 main 의 지역 변수를 7777 로 바꿈). 그래서 스택 지역 변수의 주소를 다른 스레드에 넘길 때는 그 변수의 수명(함수가 끝나면 끝)을 지켜야 한다. 꼬리: "프로세스와 스레드 차이?" → 프로세스는 주소 공간 자체가 따로라 공유하려면 공유 메모리·파이프 같은 IPC 가 필요하다(OS-B14). 생성·전환 비용도 다르다 (OS-M02 에서 측정).

</details>

<details>
<summary>Q5. C11 이 말하는 데이터 레이스의 정확한 정의는? 레이스 컨디션과 같은 말인가요?</summary>

같은 메모리 위치에 대한 두 접근이 ① 서로 다른 스레드에서 ② 하나 이상 쓰기이고(충돌, 5.1.2.4p4) ③ 하나 이상 원자 연산이 아니며 ④ 어느 쪽도 다른 쪽보다 먼저 일어나지 않으면(happens-before 없음) 데이터 레이스, 동작은 UB (5.1.2.4p25). 레이스 컨디션(race condition)은 더 넓은 말로 "결과가 실행 순서에 따라 달라지는 논리 버그" 다. 예: 원자 load 와 원자 store 로 증가를 하면 데이터 레이스는 아니지만(전부 원자) 증가를 잃는다 — 레이스 컨디션. TSan 은 앞쪽(데이터 레이스)만 잡는다 (C-A03 `demo_lost_update.c`). 꼬리: "메모리 위치란?" → 스칼라 객체 하나, 또는 인접한 비트필드의 최대 묶음 (3.14) — 그래서 서로 다른 비트필드를 두 스레드가 써도 레이스다 (연습 3).

</details>

<details>
<summary>Q6. TSan 보고서에서 "Previous write" 스레드가 이미 finished 인데 왜 레이스죠?</summary>

TSan 은 실제로 겹쳤는지가 아니라 두 접근 사이에 **happens-before** 가 있었는지를 본다 (벡터 클럭으로 추적). T3 가 끝났더라도 main 이 T3 를 join 하기 전에 T4 를 만들었으면 T3 의 쓰기와 T4 의 쓰기를 순서 짓는 동기화가 없다. 그래서 결과가 맞은 실행(`got=2000`)도 보고했다. 이게 TSan 의 장점이다: 운 좋게 안 겹친 실행에서도 잠재 레이스를 찾는다. 반대로 실행되지 않은 경로의 레이스는 못 찾는다. 꼬리: "MCU 코드는 어떻게?" → TSan 이 없으니 공유 로직을 호스트에서 pthread 로 돌려 TSan 에 넣는다 — 이 레슨의 구조. 상세는 TOOL-M09.

</details>

<details>
<summary>Q7. 단일 코어 Cortex-M 의 ISR 과 main 이 카운터를 공유합니다. 어떻게 막죠? M0+ 라면?</summary>

단일 코어라도 ISR 이 LDR 과 STR 사이에 오면 증가를 잃는다 (Part C: 주입 1, 2 에서 4). M3/M4/M7: `_Atomic` + `atomic_fetch_add/sub` → `LDREX/STREX` 루프. 예외 진입/복귀가 로컬 모니터를 지우므로 끼어든 뒤의 STREX 는 실패해 다시 읽는다. 인터럽트를 안 막으니 지연 0. M0/M0+: 배타 명령이 없어서(실측 `bl __atomic_fetch_add_4`) PRIMASK 저장 → `cpsid i` → RMW → 복원(ARM-M10). 또는 "ISR 만 쓰고 main 은 읽기만" 하도록 설계를 바꾼다(단일 writer, 32 비트 이하 정렬 접근은 찢어지지 않음 — C-M14). 꼬리: "듀얼 코어(RP2040)라면?" → 인터럽트 끄기는 자기 코어만 막는다. 하드웨어 스핀락(SIO)이나 다른 코어용 락이 필요하다 (RTOS-A08).

</details>

## 9. 추가 연습

### 연습 1 — 루프 변수 주소를 넘기는 고전 버그

`for (int i = 0; i < 8; i++) pthread_create(&t[i], NULL, worker, &i);` 의 결과를 예측하고(`demo_argbug.c`), 올바른 두 가지 방식 — 스레드별 인자 칸, `(void *)(intptr_t)i` — 으로 `spawn_ids` 를 작성하라. 각 스레드가 자기 id 를 정확히 한 번씩 봐야 한다.

<details>
<summary>답 보기</summary>

`demo_argbug.c` 실제 출력 (`-O2`, 5회):

```text
id seen count: [0]=0 [1]=0 [2]=1 [3]=2 [4]=0 [5]=1 [6]=1 [7]=2 [8]=1
id seen count: [0]=0 [1]=1 [2]=0 [3]=2 [4]=1 [5]=1 [6]=0 [7]=1 [8]=2
id seen count: [0]=0 [1]=1 [2]=1 [3]=1 [4]=1 [5]=1 [6]=1 [7]=1 [8]=1
id seen count: [0]=0 [1]=1 [2]=1 [3]=1 [4]=1 [5]=1 [6]=1 [7]=1 [8]=1
id seen count: [0]=0 [1]=1 [2]=0 [3]=2 [4]=1 [5]=1 [6]=1 [7]=1 [8]=1
```

0 은 한 번도 안 보였다(스레드가 읽기 전에 main 이 이미 `i++`), 중복이 있고, **8** 까지 보였다 — 루프가 끝난 뒤의 `i`, 즉 수명이 끝난 객체를 읽은 것(UB). TSan 은 `Read of size 4 ... by thread T1` / `Previous write ... by main thread` / `Location is stack of main thread` 로 보고했다. 고친 코드:

```c
slots[i] = (slot_t){ .id = i, .seen = -1 };
pthread_create(&t[i], NULL, id_worker, &slots[i]);            /* 스레드마다 자기 칸 */
pthread_create(&u[i], NULL, id_worker_val, (void *)(intptr_t)i);  /* 값 자체를 실어 보냄 */
```

`intptr_t` ↔ `void *` 변환은 구현 정의(C11 6.3.2.3p5–6)지만 POSIX 시스템에서 관용적으로 쓰인다. 이식성이 걱정되면 인자 칸 방식.

</details>

### 연습 2 — 최종값 분포 더 보기

시뮬레이터로 2 스레드 × 3 번, 4 스레드 × 1 번의 모든 인터리빙을 열거하라. 인터리빙 수, 최소값, 정답 수를 예측한 뒤 확인.

<details>
<summary>답 보기</summary>

실제 출력:

```text
info: 2x3 -> 48620 interleavings, final 2:648 3:30780 4:15714 5:1458 6:20
info: 4x1 -> 369600 interleavings, final 1:291600 2:74952 3:3024 4:24
```

2×3: 18!/(9!·9!) = 48620, 최소 2, 정답은 증가 6 개를 통째로 섞는 순서 수 6!/(3!·3!) = 20. 4×1: 12!/(3!)⁴ = 369600, 정답 4! = 24 (0.0065%). 스레드가 늘수록 "완전히 직렬인 순서" 의 비율이 급격히 줄어든다. 실제 하드웨어에서 손실률이 이보다 훨씬 낮은 이유는 스케줄러가 스레드를 긴 덩어리로 돌리기 때문이고, `-O0` 4 스레드 실측(75%)처럼 촘촘한 루프에서 겹치면 크게 잃는다.

</details>

### 연습 3 — 서로 다른 비트필드도 레이스다

`struct { unsigned a:4, b:4; } s;` 에서 스레드 A 는 `s.a = 5`, 스레드 B 는 `s.b = 9` 만 한다. 서로 다른 필드인데 왜 하나가 사라질 수 있나? 시뮬레이터로 20 개 인터리빙 중 몇 개가 잃는지 세고, `uint8_t a, b;` 로 바꾸면 어떻게 되는지 보여라.

<details>
<summary>답 보기</summary>

비트필드 쓰기는 바이트(또는 워드) 단위 RMW 다. 실제 asm (`s.a = 5`):

```text
arm64 -O2:  ldrb w9,[x8] / and w9,w9,#0xf0 / mov w10,#5 / orr w9,w9,w10 / strb w9,[x8]
M4 -O2:     ldrb r1,[r0] / and r1,r1,#240 / adds r1,#5 / strb r1,[r0]
```

B 도 같은 바이트를 읽고 쓰므로 `counter++` 와 똑같이 18/20 인터리빙에서 한쪽이 사라진다(시뮬레이터 결과). C11 3.14: 인접한 (0 이 아닌 폭의) 비트필드 묶음은 **하나의 메모리 위치** 이고, 레이스는 메모리 위치 단위로 정의되므로 이건 데이터 레이스다. `uint8_t a, b;` 는 서로 다른 메모리 위치 — 각 STORE 가 자기 바이트만 써서 0/20. C11 은 이 경우 서로 간섭하지 않는다고 보장한다(3.14 NOTE 1). NOTE 2 는 반대로 "사이에 비트필드만 있는 두 비아토믹 비트필드를 동시에 갱신하는 것은 안전하지 않다" 고 못 박는다. 펌웨어 함정: 상태 플래그를 비트필드 구조체로 모아 두고 ISR 과 main 이 각자 다른 비트를 쓰는 코드. 레지스터 비트필드의 RMW 문제는 C-M02/C-M03.

</details>

### 연습 4 — 스레드 결과를 안전하게 돌려받기

스레드 8 개가 각자 `(i+1000)²` 을 계산해 돌려준다. 지역 변수 주소를 반환하는 버전이 왜 틀렸는지 설명하고, 힙으로 돌려받는 `squares()` 를 작성하라.

<details>
<summary>답 보기</summary>

```c
static void *square_worker(void *arg) {
    uint64_t x = (uint64_t)(uintptr_t)arg;
    uint64_t *res = malloc(sizeof *res);     /* return &local; 은 스택이 사라진 뒤의 포인터 */
    if (res) *res = x * x;
    return res;
}
/* main: pthread_join(t[i], &r); out[i] = *(uint64_t *)r; free(r); */
```

스레드가 끝나면 그 스택은 해제(또는 다음 스레드에 재사용 — Part A 의 gate 가 필요했던 이유)되므로 지역 변수 주소는 매달린 포인터다. 소유권 규칙: 스레드가 malloc → join 한 쪽이 free. 값이 작으면 `worker_t` 같은 인자 구조체의 out 필드에 쓰는 편이 할당이 없어 더 낫다 (Part A 방식). 결과 대신 `pthread_exit(p)` 를 불러도 같다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice    # 9/9 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pa && ./pa   # 9/9, 깨끗
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pt && ./pt                    # 9/9, 보고 없음
```

## 10. 핵심 정리

- 스레드 = 레지스터 + 스택만 따로, 나머지 주소 공간(코드·전역·힙·남의 스택)은 공유. `pthread_create` 는 에러를 **반환** 하고, 인자는 스레드별 칸으로.
- `pthread_create` / `pthread_join` 은 메모리 동기화 지점(POSIX XBD 4.12) — create 전에 쓴 것은 스레드가 보고, 스레드가 쓴 것은 join 뒤 보인다. **공유 쓰기를 없애는 분할** 이 가장 싼 해법.
- `counter++` = `ldr / add / str` (arm64), `ldr / adds / str` (M4). LOAD 와 STORE 사이에 남의 STORE 가 끼면 lost update. 2 스레드 × k 번의 최종값 범위는 [2, 2k].
- 서로 다른 스레드의 충돌 접근 + 비원자 + happens-before 없음 = 데이터 레이스 = UB (C11 5.1.2.4p25). 비트필드 묶음은 하나의 메모리 위치 (3.14).
- `-O2` 에서 손실 0 은 루프가 `+= n` 으로 합쳐진 우연, `volatile` 은 해결이 아니다 (실측 75% 손실). "몇 번 돌려 봤다" 는 증거가 아니다.
- TSan 은 happens-before 로 판단 → 결과가 맞은 실행에서도 보고, 이 맥에선 exit 134. 상세는 TOOL-M09.
- 단일 코어 MCU 에선 ISR 이 LDR~STR 사이에 끼면 깨진다. M3+ 는 LDREX/STREX(예외가 모니터를 지움 → 재시도), M0+ 는 PRIMASK 임계 구역, 멀티코어는 그것만으론 부족.

## 참고자료

- OSTEP (Arpaci-Dusseau) — 26장 *Concurrency: An Introduction* (공유 카운터 레이스, "uncontrolled scheduling"), 27장 *Interlude: Thread API*, 28장 *Locks*, 29장 *Lock-based Concurrent Data Structures*: <https://pages.cs.wisc.edu/~remzi/OSTEP/>
- POSIX.1-2017 — `pthread_create`, `pthread_join`, XBD 4.12 *Memory Synchronization*: <https://pubs.opengroup.org/onlinepubs/9699919799/>
- C11 (N1570) — 3.14 memory location, 5.1.2.4 Multi-threaded executions and data races (p4 conflict, p25 data race), 6.3.2.3 포인터↔정수 변환: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Clang ThreadSanitizer: <https://clang.llvm.org/docs/ThreadSanitizer.html>
- ARMv7-M Architecture Reference Manual (DDI0403) — A3.4 배타적 접근(LDREX/STREX, local monitor): <https://developer.arm.com/documentation/ddi0403/latest/>
- Arm A64 ISA — LDR/STR, LSE `LDADD`: <https://developer.arm.com/documentation/ddi0602/latest/>
- FreeRTOS — Cortex-M 포트의 인터럽트 우선순위·BASEPRI (`configMAX_SYSCALL_INTERRUPT_PRIORITY`), `taskENTER_CRITICAL()`: <https://www.freertos.org/RTOS-Cortex-M3-M4.html>
- 관련 레슨: OS-B05 (뮤텍스·원자·sloppy counter 처리량), OS-B06 (조건 변수), TOOL-M09 (TSan 보고서 읽기), OS-M02 (문맥 교환 비용), OS-B08 (주소 공간·ASLR), C-B09 (volatile 한계), C-M14 (ISR ↔ main 공유 데이터), C-A03 (C11 atomics·memory_order), ARM-M04 (LDXR/STXR), ARM-M10 (PRIMASK/BASEPRI), ARM-M11 (LDREX/STREX), RTOS-B05 (tick 선점), RTOS-A08 (SMP RTOS), MEM-M04 / ARCH-M08 (캐시 라인 경합)

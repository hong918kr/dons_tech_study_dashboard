---
id: RTOS-B03
time: 60분
updated: 2026-11-11
---
# RTOS-B03 ucontext로 컨텍스트 스위치 체험

## 학습 목표

- 컨텍스트 스위치(context switch)가 **"지금 CPU 레지스터 일부 + 스택 포인터(sp)를 TCB 에 저장하고, 다른 태스크 것을 불러온다"** 임을 `getcontext / makecontext / swapcontext` 로 직접 만들어 보고 설명할 수 있다.
- 두 태스크가 **각자 자기 스택 배열** 위에서 돈다는 것을 주소로 증명한다: 지역 변수 주소와 저장된 sp 가 그 태스크의 스택 범위 안에 있고, 양보 뒤에도 지역 변수 값이 그대로다.
- `makecontext` 가 요구하는 것(먼저 `getcontext`, `uc_stack`, `uc_link`, int 인자)과 태스크 함수가 **return 했을 때** 일어나는 일(`uc_link` 가 있으면 그리로, `NULL` 이면 프로세스 종료)을 이 맥에서 관찰한 사실로 말할 수 있다.
- 이 맥(arm64)의 `mcontext` 에서 실제로 저장되는 칸(x19–x28, fp, lr, sp, d8–d15)을 디스어셈블로 확인하고, Cortex-M 의 PendSV(하드웨어가 r0–r3, r12, lr, pc, xPSR 저장 + 소프트웨어가 r4–r11 저장)와 비교한다.
- 스택을 0xA5 로 칠해 **high-water mark** 를 재고, 스택 끝 카나리로 오버플로를 잡는 FreeRTOS 방식을 구현한다.

## 1. 문제

"컨텍스트 스위치 때 정확히 뭘 저장하죠? 태스크마다 스택이 왜 따로 있어야 하나요? PendSV 는 왜 쓰죠?" — RTOS 면접에서 거의 항상 나오고, 말로만 외운 사람과 직접 만들어 본 사람이 바로 갈리는 질문이다. 이 레슨은 호스트(macOS)에서 ucontext 로 컨텍스트 스위치를 **직접 일으키고 주소로 확인** 한다. 실제 MCU(STM32, nRF52 의 Cortex-M4)에서는 같은 일을 PendSV 핸들러의 어셈블리 30줄 남짓이 한다.

핵심 그림은 하나다. 태스크 = **자기 스택 + 저장된 레지스터(TCB 안)**. 스위치 = 지금 레지스터를 나가는 태스크의 TCB 에 넣고, 들어오는 태스크의 TCB 에서 꺼내 CPU 에 싣는 것. sp 가 바뀌는 순간 "어느 스택 위의 어느 함수 호출 사슬" 이 바뀐다 — 그게 태스크가 바뀐다는 뜻이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">swapcontext(&amp;A-&gt;ctx, &amp;B-&gt;ctx) = ① CPU → A.ctx 저장  ② B.ctx → CPU 복원</text>
  <rect x="270" y="40" width="200" height="120" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="330" y="62" fill="currentColor" font-weight="bold">CPU (arm64)</text>
  <text x="282" y="86" fill="currentColor" font-family="monospace">x19–x28, fp, lr</text>
  <text x="282" y="108" fill="currentColor" font-family="monospace">sp  ← 어느 스택인가</text>
  <text x="282" y="130" fill="currentColor" font-family="monospace">d8–d15, 시그널 마스크</text>
  <text x="282" y="150" fill="currentColor">x0–x18 은 저장 안 함</text>
  <rect x="20" y="40" width="200" height="90" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor" font-weight="bold">TCB A (나감)</text>
  <text x="32" y="86" fill="currentColor" font-family="monospace">ctx: 레지스터 사본</text>
  <text x="32" y="108" fill="currentColor" font-family="monospace">ctx.sp → 스택 A 안</text>
  <rect x="520" y="40" width="200" height="90" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="532" y="62" fill="currentColor" font-weight="bold">TCB B (들어옴)</text>
  <text x="532" y="86" fill="currentColor" font-family="monospace">ctx: 레지스터 사본</text>
  <text x="532" y="108" fill="currentColor" font-family="monospace">ctx.sp → 스택 B 안</text>
  <path d="M270 70 L222 70" stroke="currentColor" stroke-width="2"/>
  <path d="M222 70 L232 64 M222 70 L232 76" stroke="currentColor" stroke-width="2"/>
  <text x="236" y="58" fill="currentColor">①</text>
  <path d="M520 100 L472 100" stroke="currentColor" stroke-width="2"/>
  <path d="M472 100 L482 94 M472 100 L482 106" stroke="currentColor" stroke-width="2"/>
  <text x="490" y="92" fill="currentColor">②</text>
  <rect x="20" y="180" width="200" height="100" rx="4" fill="none" stroke="currentColor"/>
  <text x="32" y="200" fill="currentColor">g_stackA[64 KB] (높은 주소 ↑)</text>
  <rect x="30" y="208" width="180" height="22" class="f-mut" stroke="currentColor"/>
  <text x="38" y="224" fill="currentColor" font-family="monospace">worker: sum, note, i</text>
  <rect x="30" y="232" width="180" height="22" class="f-mut" stroke="currentColor"/>
  <text x="38" y="248" fill="currentColor" font-family="monospace">task_yield → swap</text>
  <text x="32" y="272" fill="currentColor">↓ 남은 공간 (0xA5 칠)</text>
  <rect x="520" y="180" width="200" height="100" rx="4" fill="none" stroke="currentColor"/>
  <text x="532" y="200" fill="currentColor">g_stackB[64 KB]</text>
  <rect x="530" y="208" width="180" height="22" class="f-ok" stroke="currentColor"/>
  <text x="538" y="224" fill="currentColor" font-family="monospace">worker: sum, note, i</text>
  <rect x="530" y="232" width="180" height="22" class="f-ok" stroke="currentColor"/>
  <text x="538" y="248" fill="currentColor" font-family="monospace">task_yield 에서 재개</text>
  <path d="M120 130 L120 176" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M620 130 L620 176" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="250" y="200" fill="currentColor">A 의 호출 사슬은 스택 A 에 얼어 있다.</text>
  <text x="250" y="222" fill="currentColor">B 의 sp 를 실으면 B 사슬이 이어진다.</text>
  <text x="250" y="244" fill="currentColor">지역 변수는 복사되지 않는다 —</text>
  <text x="250" y="266" fill="currentColor">스택이 그대로 있을 뿐.</text>
</svg>
```

**미니 커널 (구조체·하네스 제공, 함수 7개는 학생).** RTOS-B02 의 TCB 어휘(`tcb_t`, `TASK_READY/RUNNING/...`)를 그대로 쓰고, `ucontext_t ctx` 와 스택 포인터·크기를 더한다. 선점은 없다(RTOS-B05). 태스크는 `task_yield()` 로 스스로 양보하고, 스케줄러는 라운드 로빈이다(우선순위 비트맵은 RTOS-B04).

| 이 모델 | 정체 | 실제 RTOS 와 다른 점 |
|---|---|---|
| `tcb_t.ctx` | `ucontext_t` (880 B, 그 안 `mcontext` 816 B) | FreeRTOS TCB 는 `pxTopOfStack` 하나만 들고 레지스터는 **태스크 스택에** 쌓는다 (5장) |
| `task_yield()` | 직접 `swapcontext(&prev->ctx, &next->ctx)` | Cortex-M: `taskYIELD()` → PendSV 를 걸고, PendSV 가 저장/복원 |
| `kernel_run()` | main 이 "커널 문맥" `g_main_ctx` 를 저장하고 첫 태스크로 | `vTaskStartScheduler()` 는 돌아오지 않는다 |
| 태스크 return | `uc_link = &g_main_ctx` → 커널이 DELETED 로 회수 | FreeRTOS 태스크는 return 금지 (`prvTaskExitError`) |
| 타임라인 | step(=`log_step` 1회)마다 `R` 실행 · `r` READY · `.` 끝남 | RTOS-B09/M03 의 tick 타임라인과 같은 글자 |

### Part A — 두 태스크, 두 스택, yield 로 핑퐁

- `task_create(t, name, fn, arg, stack, size, link)` — 인자 검사 → `getcontext` → `uc_stack`/`uc_link` 설정 → `makecontext(&t->ctx, task_entry, 0)` → READY 로 `g_tasks[]` 등록. `fn/arg` 는 TCB 에 둔다 (`task_entry` 는 하네스 제공: `g_cur->fn(g_cur->arg)`).
- `sched_pick_next()` — `g_cur` 다음 번호부터 한 바퀴, 첫 READY (자기 자신 제외). 없으면 NULL.
- `task_yield()` — 다음 태스크가 있으면 상태 갱신 + `swapcontext`. 혼자면 그냥 return.
- `kernel_run()` — READY 를 골라 `swapcontext(&g_main_ctx, ...)`; 돌아오면(태스크가 return) DELETED 로 회수, 반복.
- 검사: 순서 `A0 B0 A1 B1 ... B4`, 스위치 10회, 지역 변수(`volatile int sum`, `char note[8]`, `double acc`) 보존, `&sum` 이 **자기 스택 배열 안 + 상대 스택 밖**, 다섯 step 내내 같은 주소, 상대 태스크의 **저장된 sp** (`ctx.uc_mcontext->__ss.__sp`) 가 상대 스택 안.

### Part B — makecontext 의 요건과 return 동작

1. **B1** 길이가 다른 태스크 (A 2 step, B 4 step): A 가 먼저 return → `uc_link` 로 커널에 돌아가 회수 → B 는 혼자 남아 `task_yield` 가 아무것도 안 한다. 순서 `A0 B0 A1 B1 B2 B3`, 스위치 4.
2. **B2** 끝난 A 의 문맥으로 다시 `swapcontext` → 이 맥에서는 `-1`, `errno = EINVAL`.
3. **B3** 스택 `NULL`, 1 KB(< 16 KB), 16 바이트 정렬 아님 → `task_create` 가 `-1`.
4. **B4** 만든 뒤 `ctx.uc_link == link`, `uc_stack.ss_sp == stack` 인가 (getcontext 를 나중에 부르면 덮어써진다).
5. **B5** 상위 32비트가 0 이 아닌 포인터 인자가 그대로 도착.
6. **B6** `uc_link = NULL` 인 태스크가 return → **자식 프로세스**(fork)에서 실행해 "태스크는 돌았고(`R`), 커널로는 안 돌아왔고(`K` 없음), 종료 코드 0" 을 확인.

### Part C — 스택 칠하기, high-water mark, 카나리 (IMU + UART 태스크)

STM32 에서 IMU 필터 태스크(256 샘플 `float` 창 = 1 KB)와 UART 명령 태스크(32 B 줄 버퍼)를 돌린다고 하자. 스택을 얼마 줘야 하나? 측정한다.

- `stack_paint(t)` — 스택 전체를 `0xA5` 로. **makecontext 뒤에** (이 맥의 makecontext 는 스택을 0 으로 지운다 — 5장).
- `stack_free_bytes(t)` — 낮은 주소부터 `0xA5` 가 이어지는 길이 (= 한 번도 안 쓴 바이트).
- `stack_canary_ok(t)` — 가장 낮은 16 바이트가 아직 `0xA5` 인가. `task_yield` 는 **나가는 태스크** 를, `kernel_run` 은 return 한 태스크를 검사해 깨졌으면 `stack_overflow_hook`.
- 검사: imu 사용량 ≥ 1 KB, uart < imu, 차이 ≥ 768 B, 훅 0회. 그다음 uart 스택 맨 아래 바이트를 일부러 망가뜨리면(넘친 배열·잘못된 DMA 주소 흉내) 훅이 uart 이름으로 불려야 한다.

## 2. Problem (English)

> **Cooperative context switching with ucontext.**
>
> ```c
> int    task_create(tcb_t *t, const char *name, void (*fn)(void *), void *arg,
>                    uint8_t *stack, size_t size, ucontext_t *link);   // 0 or -1
> tcb_t *sched_pick_next(void);     // round robin, next READY task other than current, or NULL
> void   task_yield(void);          // switch to the next READY task; no-op if none
> int    kernel_run(void);          // run until every task has returned; returns #tasks reaped, -1 on error
> void   stack_paint(tcb_t *t);     size_t stack_free_bytes(const tcb_t *t);   bool stack_canary_ok(const tcb_t *t);
> ```
>
> Each task runs on its own statically allocated, 16-byte-aligned stack (≥ 16 KB on the host). Two tasks that loop 5 times and yield after each step must interleave as `A0 B0 A1 B1 … B4`. Prove (with addresses, not just prints) that each task's locals live inside its own stack array and survive every yield, and that a suspended task's saved stack pointer points into its own stack. When a task function returns, control must go back to the kernel (`uc_link`); show what happens when `uc_link` is NULL. Finally, paint stacks with `0xA5`, report per-task stack usage, and detect a corrupted end-of-stack canary at switch-out.
>
> Follow-ups: What exactly is saved on a context switch and why not all registers? Why does Cortex-M use PendSV? Why does every task need its own stack? What happens if a FreeRTOS task returns? How do you size a task stack? Why is `swapcontext` slow?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

`_XOPEN_SOURCE 600` 은 파일 첫 줄에 있다 — macOS `<ucontext.h>` 는 이것 없이 include 하면 `#error The deprecated ucontext routines require _XOPEN_SOURCE to be defined` 다. 그리고 네 함수 모두 `__API_DEPRECATED("No longer supported", macos(10.5, 10.6))` 라서 `-Werror` 에서 deprecation 경고가 에러가 된다. 그래서 **그 경고 하나만** `#pragma clang diagnostic ignored "-Wdeprecated-declarations"` 로 끈다(`-Wall -Wextra -Werror` 는 그대로). 학습용 호스트 모델이라 괜찮지만, 제품 코드에서 macOS 코루틴이 필요하면 손으로 쓴 어셈블리 스위치나 스레드를 쓴다.

처음 실행 결과 (실제 출력 일부, 멈추지 않는다):

```text
== Part A: two tasks, own stacks, yield ping-pong ==
FAIL starter.c:257  task_create(&A, "A", worker, &ja, g_stackA, sizeof g_stackA, &g_main_ctx) == 0
...
B6 uc_link=NULL child: pipe="" exited=1 status=42
FAIL starter.c:352  strcmp(buf, "R") == 0
...
task  stack   used   free   used%
imu   65536  65536      0  100.0
12/43 passed
```

하네스 설계:

- **멈추지 않는다.** 스텁 `kernel_run` 은 `-1` 을 바로 돌려주고, B2 는 A 가 정말 끝났을 때만 swap 을 시도한다(쓰레기 문맥으로 점프 방지). B6 의 자식은 실패하면 `_exit(42)`.
- **주소로 검사한다.** `in_stack(t, addr)` 는 `[t->stack, t->stack + size)` 범위 비교. 저장된 sp 는 arm64 macOS 의 `uc_mcontext->__ss.__sp` 로 읽는다 (다른 플랫폼은 그 검사만 건너뛴다).
- `(void)task_entry; (void)stack_overflow_hook; (void)sched_pick_next;` 줄은 starter 에서 아직 안 쓰는 함수의 unused 경고를 막는다.

## 4. 힌트

<details>
<summary>힌트 1 — getcontext 는 "초기화" 다: 맨 먼저 부른다</summary>

`makecontext` 는 빈 `ucontext_t` 를 받지 않는다. `getcontext` 로 시그널 마스크와 `uc_mcontext` 포인터(구조체 안 `__mcontext_data` 를 가리킴)를 채운 **다음에** `uc_stack`, `uc_link` 를 쓴다. 이 맥에서 getcontext 는 `uc_link` 를 `NULL` 로, `uc_stack` 을 "지금 sp, 8 MB" 로 덮어쓴다 (demo 의 P1).

</details>

<details>
<summary>힌트 2 — makecontext 의 인자는 int: 포인터는 TCB 로</summary>

`makecontext(&ctx, f, argc, ...)` 의 가변 인자는 POSIX 상 `int` 다. 이 맥의 구현은 정말로 32비트씩 꺼내서 64비트 포인터의 위쪽이 잘린다 (P3: `0x10235c000 → 0x235c000`). `fn/arg` 를 TCB 에 넣고 인자 0 개로 `task_entry` 를 시작한 뒤, `task_entry` 가 `g_cur` 로 찾게 한다.

</details>

<details>
<summary>힌트 3 — task_yield: 상태를 먼저 바꾸고 swap</summary>

`swapcontext` 를 부른 순간 다음 태스크가 돈다. 그러니 `g_cur = next`, `prev->state = READY`, `next->state = RUNNING`, `g_switches++` 는 **swap 전에**. swap 이 return 하는 것은 "누군가 나를 다시 골랐을 때" 이고, 그때 `g_cur` 는 이미 나다(나를 고른 쪽이 설정했다).

</details>

<details>
<summary>힌트 4 — kernel_run: swapcontext 가 "두 번째로 return" 하는 곳</summary>

`swapcontext(&g_main_ctx, &n->ctx)` 는 태스크로 떠난다. 태스크끼리 yield 하는 동안 커널은 그 줄에 얼어 있다. 어떤 태스크가 return 하면 libc 의 `_ctx_done` 이 `setcontext(uc_link)` = `g_main_ctx` 를 불러, swapcontext 가 0 으로 return 한 것처럼 이어진다. 이때 `g_cur` 가 방금 끝난 태스크다.

</details>

<details>
<summary>힌트 5 — 스택은 아래로 자란다: 낮은 주소가 "끝"</summary>

arm64 와 Cortex-M 모두 full-descending 스택이다. 쓰이지 않은 칠은 **배열 앞쪽(낮은 주소)** 에 남는다. `stack_free_bytes` 는 `stack[0]` 부터 0xA5 를 세고, 카나리는 `stack[0..15]` 다.

</details>

## 5. 풀이

### Part A — 네 함수

```c
static int task_create(tcb_t *t, ..., uint8_t *stack, size_t size, ucontext_t *link) {
    if (!t || !fn || !stack || size < MIN_STACK || ((uintptr_t)stack & 15u) || g_ntask >= MAX_TASKS) return -1;
    memset(t, 0, sizeof *t); /* name, fn, arg, stack, stack_size 기록 */
    if (getcontext(&t->ctx) != 0) return -1;           /* ① 먼저 */
    t->ctx.uc_stack.ss_sp = stack; t->ctx.uc_stack.ss_size = size; t->ctx.uc_stack.ss_flags = 0;
    t->ctx.uc_link = link;                             /* ② return 하면 갈 곳 */
    makecontext(&t->ctx, task_entry, 0);               /* ③ 시작점 + 스택 꼭대기 */
    t->id = g_ntask; t->state = TASK_READY; g_tasks[g_ntask++] = t; return 0;
}
static void task_yield(void) {
    tcb_t *prev = g_cur; if (!prev) return;
    if (!stack_canary_ok(prev)) stack_overflow_hook(prev);
    tcb_t *next = sched_pick_next(); if (!next) return;
    prev->state = TASK_READY; next->state = TASK_RUNNING; g_cur = next; g_switches++;
    swapcontext(&prev->ctx, &next->ctx);
}
```

실제 출력 (solution.c, -O2):

```text
order    : A0 B0 A1 B1 A2 B2 A3 B3 A4 B4
step : 0 1 2 3 4 5 6 7 8 9
A    : R r R r R r R r R r
B    : r R r R r R r R r R
switches : 10, reaped 2
stack A  : [0x10442c020, 0x10443c020)   &sum in A = 0x10443bf94
stack B  : [0x10443c3a0, 0x10444c3a0)   &sum in B = 0x10444c314
main     : &main_local = 0x16b9dab24
saved sp : A checked 5 bad 0, B checked 5 bad 0
example  : A.ctx sp = 0x102613f40 (top-224), B.ctx sp = 0x1026242c0 (top-224)
```

(주소는 ASLR 때문에 실행마다 다르다. `example` 줄은 다른 실행에서 찍은 것.) 읽는 법:

- `&sum` 은 스택 A 꼭대기에서 0x8c = **140 바이트 아래** 다. 64 KB 배열의 맨 위쪽 — 스택은 꼭대기에서 아래로 자란다. main 의 지역 변수는 `0x16b...` (진짜 프로세스 스택) 에 있어서 두 배열과 전혀 다른 영역이다.
- 다섯 step 내내 `&sum` 이 같은 주소다(CHECK `same`). 양보는 프레임을 옮기지 않는다. 지역 변수를 "저장/복원" 하는 코드는 어디에도 없다 — **스택 A 를 아무도 안 건드리니 그대로 있는 것** 이다.
- 상대의 저장된 sp 는 꼭대기에서 224 바이트 아래: `task_entry` → `worker` → `task_yield` → `swapcontext` 프레임까지 쌓인 지점. getcontext 가 그 sp 를 `__ss.__sp` 에 적었다.
- 스위치 10번 = yield 10번 모두 상대가 READY 였다. 마지막 B4 의 yield 가 A 로 넘어가면 A 는 루프를 끝내고 return → 커널 → B 를 다시 들여보내고 → B return.

```svg
<svg viewBox="0 0 740 196" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">실제 타임라인 (solution.c 출력 그대로) — 위: Part A, 아래: B1</text>
  <text x="10" y="40" fill="currentColor">step</text>
  <text x="84" y="40" fill="currentColor">0</text><text x="144" y="40" fill="currentColor">1</text><text x="204" y="40" fill="currentColor">2</text><text x="264" y="40" fill="currentColor">3</text><text x="324" y="40" fill="currentColor">4</text><text x="384" y="40" fill="currentColor">5</text><text x="444" y="40" fill="currentColor">6</text><text x="504" y="40" fill="currentColor">7</text><text x="564" y="40" fill="currentColor">8</text><text x="624" y="40" fill="currentColor">9</text>
  <text x="10" y="63" fill="currentColor" font-family="monospace">A</text>
  <rect x="70" y="50" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="130" y="50" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="190" y="50" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="250" y="50" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="310" y="50" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="370" y="50" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="430" y="50" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="490" y="50" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="550" y="50" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="610" y="50" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="87" fill="currentColor" font-family="monospace">B</text>
  <rect x="70" y="74" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="130" y="74" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="190" y="74" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="250" y="74" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="310" y="74" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="370" y="74" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="430" y="74" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="490" y="74" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="550" y="74" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="610" y="74" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="680" y="75" fill="currentColor">10 swaps</text>
  <text x="10" y="127" fill="currentColor" font-family="monospace">A</text>
  <rect x="70" y="114" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="130" y="114" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="190" y="114" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="250" y="114" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="320" y="127" fill="currentColor">. . (return → uc_link → 커널이 DELETED 로 회수)</text>
  <text x="10" y="151" fill="currentColor" font-family="monospace">B</text>
  <rect x="70" y="138" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="130" y="138" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="190" y="138" width="60" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="250" y="138" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="310" y="138" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="370" y="138" width="60" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="440" y="151" fill="currentColor">4 swaps (혼자면 yield = no-op)</text>
  <rect x="10" y="172" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="30" y="183" fill="currentColor">R 실행</text>
  <rect x="100" y="172" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="120" y="183" fill="currentColor">r READY (자기 yield 안에서 얼어 있음)</text>
</svg>
```

### "컨텍스트" 의 정체 — 이 맥에서 직접 본 것

`ucontext_t` 는 880 B 이고 그 안의 `uc_mcontext` 가 816 B 짜리 `struct __darwin_mcontext64 { __es(16 B), __ss(272 B), __ns(528 B) }` 를 가리킨다. `__ss` 가 `_STRUCT_ARM_THREAD_STATE64` = `__x[29]`, `__fp`, `__lr`, `__sp`, `__pc`, `__cpsr`, `__pad`(mach/arm/_structs.h). 칸은 다 있지만 **getcontext 가 실제로 채우는 칸은 일부** 다. lldb 로 `libsystem_platform.dylib` 의 getcontext 를 디스어셈블했다 (x0 = mcontext 포인터, 핵심만):

```text
getcontext:  ... bl populate_signal_stack_context    ; __sigaltstack, getrlimit, sigprocmask 호출
  stp x10, x12, [x0, #0xf8]     ; fp, lr  (fp 는 pacda 로 서명 — arm64 프로세스에선 값 그대로였다)
  str x11, [x0, #0x108]         ; sp
  str w13, [x0, #0x11c]         ; flags = 2
  stp x19, x20, [x0, #0xa8] ... stp x27, x28, [x0, #0xe8]
  str xzr, [x0, #0x10]          ; x0 = 0 → 재개되면 getcontext 가 0 을 return
  str d8, [x0, #0x1a8] ... str d15, [x0, #0x218]
_setcontext: ldp x19, x20, [x0, #0xa8] ... ldr d15 ... ldr xzr, [x11]  ; 새 스택을 한 번 건드려 본다
  mov sp, x11 ; mov x29, x10 ; mov x30, x12 ; ... retab              ; lr 로 "return" = 재개
```

`demo_mcontext_layout.c` 로 확인 (구조체를 0xEE 로 채운 뒤 getcontext, 바뀐 칸만 출력, x19/d8 에는 asm 으로 표식 값):

<!-- code: demo_mcontext_layout.c -->

```text
sizeof(ucontext_t)=880  uc_mcsize=816  uc_mcontext==&__mcontext_data: yes
mcontext: __es @0 (16 B), __ss @16 (272 B), __ns @288 (528 B)
written __ss slots: x0 x19 x20 x21 x22 x23 x24 x25 x26 x27 x28 fp lr sp cpsr/flags
written __ns slots: v8.hi v9.hi v10.hi v11.hi v12.hi v13.hi v14.hi v15.hi
written __es: no
x19 = 0x1919191919191919
v8  = lo 0xeeeeeeeeeeeeeeee  hi 0x0808080808080808
lr  = 0x0000000100df8860   grab = 0x100df8838 (재개 지점 = grab 안, getcontext 호출 다음)
flags(__pad) = 0x2
```

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">getcontext 가 실제로 쓴 mcontext 칸 (demo_mcontext_layout.c 관찰)</text>
  <rect x="10" y="34" width="50" height="26" class="f-mut" stroke="currentColor"/><text x="16" y="51" fill="currentColor">__es</text>
  <rect x="60" y="34" width="40" height="26" class="f-warn" stroke="currentColor"/><text x="68" y="51" fill="currentColor">x0=0</text>
  <rect x="100" y="34" width="170" height="26" class="f-mut" stroke="currentColor"/><text x="120" y="51" fill="currentColor">x1–x18 (안 씀)</text>
  <rect x="270" y="34" width="200" height="26" class="f-acc" stroke="currentColor"/><text x="300" y="51" fill="currentColor">x19–x28 (callee-saved)</text>
  <rect x="470" y="34" width="120" height="26" class="f-acc" stroke="currentColor"/><text x="480" y="51" fill="currentColor">fp · lr · sp</text>
  <rect x="590" y="34" width="50" height="26" class="f-mut" stroke="currentColor"/><text x="600" y="51" fill="currentColor">pc ✗</text>
  <rect x="640" y="34" width="90" height="26" class="f-ok" stroke="currentColor"/><text x="650" y="51" fill="currentColor">flags=2</text>
  <text x="10" y="80" fill="currentColor">__ns (v0–v31, 각 16 B):</text>
  <rect x="10" y="88" width="200" height="26" class="f-mut" stroke="currentColor"/><text x="40" y="105" fill="currentColor">v0–v7 (안 씀)</text>
  <rect x="210" y="88" width="230" height="26" class="f-acc" stroke="currentColor"/><text x="222" y="105" fill="currentColor">v8–v15 의 hi 칸에 d8–d15</text>
  <rect x="440" y="88" width="290" height="26" class="f-mut" stroke="currentColor"/><text x="500" y="105" fill="currentColor">v16–v31 · fpsr · fpcr (안 씀)</text>
  <text x="10" y="140" fill="currentColor">AAPCS64: x0–x18, v0–v7, v16–v31, 그리고 v8–v15 의 상위 64비트는 함수가 망가뜨려도 된다.</text>
  <text x="10" y="160" fill="currentColor">swapcontext 는 "함수 호출" 이므로 호출한 쪽 컴파일러가 이미 그것들을 버린 것으로 여긴다 → 저장할 필요 없음.</text>
  <text x="10" y="180" fill="currentColor">pc 칸도 안 쓴다: 재개 지점은 lr (getcontext 의 복귀 주소). 컨텍스트 = callee-saved + sp + 복귀 주소.</text>
</svg>
```

정리하면 **협력형(cooperative) 스위치의 컨텍스트 = callee-saved 레지스터 + sp + 복귀 주소(lr)** 다. 나머지 레지스터는 호출 규약(AAPCS64) 상 "함수를 부르면 깨질 수 있는" 레지스터라, 호출한 쪽이 이미 필요한 값을 스택에 대피시켰다. 그리고 ucontext 는 여기에 **시그널 마스크** 를 더한다 — 그래서 느리다 (Q5).

`getcontext` 가 "두 번 return" 하는 원리는 `setjmp` 와 같다: 처음엔 그냥 0 으로 return, 나중에 누가 그 문맥을 `setcontext` 하면 저장된 lr 로 다시 한 번 0 으로 return. swapcontext 는 이 둘을 구별하려고 `uc_onstack` 의 bit 31 을 표식으로 쓴다 (디스어셈블: `orr w8, w8, #0x80000000` 후 `setcontext(ucp)`, 다시 돌아왔을 때 `tbnz w8, #0x1f` 로 "이미 갔다 왔음" → return 0).

### Part B — makecontext 의 요건과 return 동작 (실제 출력)

```text
B1 order : A0 B0 A1 B1 B2 B3  (switches 4, reaped 2)
B2 swap to finished A: ret=-1 errno=22, A.ctx.uc_mcsize=0
B5 &ja2 = 0x16dd0de30 (upper 32 bits non zero)
B6 uc_link=NULL child: pipe="R" exited=1 status=0
```

makecontext 디스어셈블에서 본 동작 (이 맥 libsystem_platform):

| 요건 / 동작 | 관찰 | 놓치면 |
|---|---|---|
| `getcontext` 먼저 | getcontext 가 `uc_link` 와 `uc_stack` 을 덮어쓴다 (P1: `uc_link=0x0`, `ss_sp`=현재 sp, `ss_size=8372224`) | uc_link 가 NULL 이 돼 태스크 return 때 **프로세스 종료** |
| `uc_stack.ss_sp/ss_size` | `ss_sp == NULL` 이면 makecontext 가 `uc_mcsize = 0` → 이후 swap 은 `-1/EINVAL` (P4) | 조용히 실패: 반환값을 안 보면 태스크가 안 돈다 |
| 스택 꼭대기 | `top = (ss_sp + ss_size) & ~15`, `sp = top - max(argc,8)*4` (16 정렬), **스택 전체를 `__bzero`** | 칠하기를 먼저 하면 지워진다 (P2: 0xA5 0 / 65536) |
| 시작점 | lr 칸 = `_ctx_start`, x19 칸 = ucp, x20 칸 = 함수 주소(TLS 쿠키로 XOR) | — |
| 인자 | `_ctx_start` 가 `ldp w0, w1, [sp]` … 로 **32비트씩** 꺼내 `blraaz x20` | 포인터 상위 32비트 손실 (P3) |
| return | `_ctx_start` → `_ctx_done(ucp)`: `uc_link == NULL` 이면 `__exit(0)`, 아니면 `ucp->uc_mcsize = 0` 후 `setcontext(uc_link)` | 끝난 문맥 재사용 → `-1/EINVAL` (B2) |

B6 의 의미: `uc_link = NULL` 은 POSIX 상 "이 문맥이 return 하면 스레드가 끝난다" 이고, 이 맥 구현은 `__exit(0)` — **atexit 도 stdio flush 도 없는** `_exit` 다. `demo_ucontext_pitfalls.c` P7 에서 `printf` 한 줄이 stdout 이 파이프일 때 사라지는 것을 봤다 (터미널이면 줄 버퍼라 보인다). glibc 는 `exit(0)` 을 부른다고 알려져 있다 (미검증). 어느 쪽이든 RTOS 태스크로서는 "return 하면 시스템이 끝난다" 이다.

<!-- code: demo_ucontext_pitfalls.c -->

```text
P1 set uc_link=0x10235c000, uc_stack.ss_sp=0x10235c6e0 BEFORE getcontext -> after: uc_link=0x0 ss_sp=0x16daaa390 ss_size=8372224
P2 painted 0xA5 BEFORE makecontext -> 0xA5 bytes left: 0 / 65536 (zero bytes: 65536)
P3 passed pointer 0x10235c000 -> task saw 0x235c000
P4 no stack: swapcontext=-1 errno=22 (Invalid argument), uc_mcsize=0
P5 first swap=0, swap to finished ctx=-1 errno=22, uc_mcsize=0
P6 copy.uc_mcontext=0x16daaa7f0  &copy.__mcontext_data=0x16daaa480  &u.__mcontext_data=0x16daaa7f0 -> points to ORIGINAL
P7 uc_link=NULL: child exited=1 status=0 signaled=0
```

또 하나: **swapcontext 도 나가는 문맥의 `uc_stack` 을 덮어쓴다** (내부에서 getcontext 를 부르므로). yield 한 태스크의 `ctx.uc_stack.ss_sp` 는 "스택 시작" 이 아니라 그 순간의 sp(`0x100cdc330`, 배열 `0x100ccc370..0x100cdc370` 안), `ss_size` 는 8 MB 였다. `uc_link` 만은 swapcontext 가 따로 보존한다(디스어셈블: `ldr x21, [x0, #0x20]` → getcontext → `str x21, [x20, #0x20]`). 그래서 TCB 에 `stack`, `stack_size` 를 **따로** 둔다 — FreeRTOS TCB 의 `pxStack` 과 같은 이유.

### Part C — high-water mark 와 카나리 (실제 출력)

```text
task  stack   used   free   used%
imu   65536   1240  64296    1.9
uart  65536    248  65288    0.4
C2 corrupted canary: hooks=4 (task uart)
```

- uart 248 B 중 대부분은 커널 몫이다: `task_entry` + `task_yield` + `swapcontext` + getcontext 의 `populate_signal_stack_context` 프레임. imu 는 그 위에 `float window[256]` 1 KB 를 더 쓴다 → 1240 B. 차이 992 B ≈ 1024 − 32. (ASan 빌드에선 프레임이 커져 1392/512 — 그래서 CHECK 는 "≥ 768" 로 느슨하게.)
- 비교: `snprintf("%d %f") + printf` 를 부르는 태스크는 **2048 B** 를 썼다 (스크래치 측정). 호스트 libc 는 스택을 많이 쓴다. Cortex-M 의 newlib `printf("%f")` 도 수백 B~1 KB 를 쓰는 것으로 알려져 있어(미검증), 로그 태스크 스택이 가장 먼저 터진다.
- C2: uart 의 `stack[0]` 과 `stack[3]` 을 망가뜨렸다. uart 가 yield 할 때마다(3번) + return 회수 때(1번) 검사 → 훅 4번, 이름 `uart`. imu 는 깨끗하니 훅이 없다. 실제 펌웨어에서 훅은 보통 로그 후 리셋이다 — 스택이 넘친 뒤의 상태는 믿을 수 없다.

FreeRTOS V11.3.2 가 같은 일을 하는 곳 (소스 확인):

| 이 레슨 | FreeRTOS |
|---|---|
| `FILL_BYTE 0xA5` | `#define tskSTACK_FILL_BYTE ( 0xa5U )` (tasks.c:121) |
| `stack_paint` | `prvInitialiseNewTask` 의 `memset( pxNewTCB->pxStack, tskSTACK_FILL_BYTE, ... )` (tasks.c:1835), `configCHECK_FOR_STACK_OVERFLOW > 1` 이나 HWM API 를 켜면 (tasks.c:129–135) |
| `stack_free_bytes` | `prvTaskCheckFreeStackSpace`: `while( *pucStackByte == tskSTACK_FILL_BYTE )` 세고 `/= sizeof( StackType_t )` (tasks.c:6470–6479) → `uxTaskGetStackHighWaterMark` 는 **word 단위** |
| `stack_canary_ok` (16 B) | 방법 2: `pulStack[0..3] != 0xa5a5a5a5` 또는 `pxTopOfStack <= pxStack + portSTACK_LIMIT_PADDING` 이면 `vApplicationStackOverflowHook` (stack_macros.h:101–118) |
| `task_yield` 에서 검사 | `vTaskSwitchContext` 안 `taskCHECK_FOR_STACK_OVERFLOW()` (tasks.c:5259) — 나가는 태스크 |

### Cortex-M PendSV 와 비교

이 레슨의 스위치는 **함수 호출 안에서** 일어난다(협력형). Cortex-M 의 선점 스위치는 **아무 명령 사이에서** 일어나므로 모든 레지스터가 살아 있다. 그래서 둘로 나눈다: 예외 진입 때 **하드웨어** 가 AAPCS 의 caller-saved 절반(r0–r3, r12, lr, pc, xPSR = 8 word)을 PSP 에 쌓고, PendSV 핸들러가 **소프트웨어로** 나머지 callee-saved 절반(r4–r11)을 쌓는다. 하드웨어 절반 덕분에 ISR 을 그냥 C 함수로 쓸 수 있는 것이고, 소프트웨어 절반이 정확히 이 레슨의 getcontext 가 저장한 종류다. 어셈블리를 직접 쓰는 건 ARM-A01 · RTOS-A01 에서.

```svg
<svg viewBox="0 0 740 268" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">FreeRTOS ARM_CM4F: 태스크 스택에 쌓이는 컨텍스트 (위 = 높은 주소)</text>
  <rect x="20" y="32" width="220" height="22" class="f-ok" stroke="currentColor"/><text x="30" y="47" fill="currentColor">xPSR · PC · LR · R12</text>
  <rect x="20" y="54" width="220" height="22" class="f-ok" stroke="currentColor"/><text x="30" y="69" fill="currentColor">R3 · R2 · R1 · R0</text>
  <rect x="20" y="76" width="220" height="22" class="f-mut" stroke="currentColor"/><text x="30" y="91" fill="currentColor">(FPU 썼으면) S0–S15, FPSCR</text>
  <rect x="20" y="98" width="220" height="22" class="f-mut" stroke="currentColor"/><text x="30" y="113" fill="currentColor">(FPU 썼으면) S16–S31</text>
  <rect x="20" y="120" width="220" height="22" class="f-acc" stroke="currentColor"/><text x="30" y="135" fill="currentColor">R14 (EXC_RETURN) · R11–R8</text>
  <rect x="20" y="142" width="220" height="22" class="f-acc" stroke="currentColor"/><text x="30" y="157" fill="currentColor">R7 · R6 · R5 · R4</text>
  <text x="248" y="57" fill="currentColor">← 하드웨어가 예외 진입 때 PSP 에 push</text>
  <text x="248" y="113" fill="currentColor">← PendSV: vstmdbeq r0!, {s16-s31}</text>
  <text x="248" y="146" fill="currentColor">← PendSV: stmdb r0!, {r4-r11, r14}</text>
  <path d="M20 170 L240 170" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="20" y="188" fill="currentColor" font-family="monospace">TCB.pxTopOfStack (첫 멤버) = 여기 주소</text>
  <rect x="500" y="32" width="230" height="132" rx="6" fill="none" stroke="currentColor"/>
  <text x="512" y="52" fill="currentColor" font-weight="bold">이 레슨 (macOS ucontext)</text>
  <text x="512" y="76" fill="currentColor">스택에는 평범한 함수 프레임만</text>
  <text x="512" y="98" fill="currentColor">레지스터 사본은 TCB.ctx 안:</text>
  <text x="512" y="120" fill="currentColor" font-family="monospace">x19–x28 fp lr sp d8–d15</text>
  <text x="512" y="142" fill="currentColor">+ 시그널 마스크 (syscall)</text>
  <text x="20" y="216" fill="currentColor">FreeRTOS 는 레지스터를 태스크 스택에 쌓고 TCB 에는 그 끝 주소(pxTopOfStack)만 둔다 → 스위치 = sp 바꿔치기.</text>
  <text x="20" y="238" fill="currentColor">새 태스크는 pxPortInitialiseStack 이 "이미 한 번 선점당한 것처럼" 가짜 프레임을 만든다: PC=태스크 함수,</text>
  <text x="20" y="258" fill="currentColor">LR=prvTaskExitError, R0=인자, EXC_RETURN=0xfffffffd ← makecontext 의 lr=_ctx_start 역할.</text>
</svg>
```

출처 (FreeRTOS V11.3.2 `portable/GCC/ARM_CM4F/port.c`): `pxPortInitialiseStack` 202–231 (xPSR `0x01000000`, PC, LR = `portTASK_RETURN_ADDRESS` = `prvTaskExitError`, R0 = `pvParameters`, `portINITIAL_EXC_RETURN 0xfffffffd`, `pxTopOfStack -= 8` 으로 R4–R11 자리), `xPortPendSVHandler` 504–557 (`mrs r0, psp` → `tst r14, #0x10` / `vstmdbeq r0!, {s16-s31}` → `stmdb r0!, {r4-r11, r14}` → `str r0, [r2]` 로 TCB 첫 멤버에 저장 → `bl vTaskSwitchContext` → 반대로 pop → `msr psp, r0` → `bx r14`). TCB 첫 멤버 규칙은 tasks.c:379 (`THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT`). 어셈블리가 `[r2]` 한 번으로 저장할 수 있게 하려는 것이다.

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | FreeRTOS (V11.3.2 소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `task_create(t, ..., stack, size, link)` | `xTaskCreateStatic(fn, name, depth, arg, prio, stackbuf, &tcb)` — depth 는 **word** 수 | `k_thread_create(&t, stack, size, entry, p1, p2, p3, prio, 0, K_NO_WAIT)` | `osThreadNew(fn, arg, &attr)` (`attr.stack_mem/stack_size`) |
| 16 B 정렬 검사 | `pxTopOfStack &= ~portBYTE_ALIGNMENT_MASK` + `configASSERT` (tasks.c:1845–1849), CM4F 는 8 B (portmacro.h:83) | `K_THREAD_STACK_DEFINE` 가 정렬 처리 | 구현 의존 |
| `task_yield()` | `taskYIELD()` = `portYIELD()` (task.h:227) → `ICSR = PENDSVSET` (portmacro.h:88–91) | `k_yield()` | `osThreadYield()` |
| `kernel_run()` | `vTaskStartScheduler()` (tasks.c:3742) — 돌아오지 않음 | `main()` 전에 커널이 이미 시작 | `osKernelStart()` |
| 태스크 return → `uc_link` | 금지: `prvTaskExitError` = `configASSERT` + 인터럽트 끄고 무한 루프 (port.c:234–257). 끝내려면 `vTaskDelete(NULL)` | entry 함수 return = 스레드 종료 | `osThreadExit()` |
| `swapcontext` 저장/복원 | `xPortPendSVHandler` (port.c:504–557), 선택은 `vTaskSwitchContext` (tasks.c:5215) | arch 별 `z_arm_pendsv` 등 | 커널 구현 |
| `stack_free_bytes` | `uxTaskGetStackHighWaterMark(h)` (tasks.c:6535) — **word** | `k_thread_stack_space_get()` | `osThreadGetStackSpace()` |
| `stack_canary_ok` + 훅 | `configCHECK_FOR_STACK_OVERFLOW 2` + `vApplicationStackOverflowHook` | `CONFIG_STACK_SENTINEL`, MPU 스택 가드 | 구현(RTX5 의 스택 워터마크 옵션 등) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 43/43 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o so_asan && \
  ASAN_OPTIONS=detect_stack_use_after_return=0 ./so_asan                          # 43/43, 경고 0
cc -std=c11 -g -O1 -fsanitize=undefined solution.c -o so_ubsan && ./so_ubsan      # 43/43, 경고 0
```

6번 반복 실행 모두 `43/43 passed`, 실행 시간 0.004 s. 주소는 ASLR 로 바뀌지만 순서·카운트·사용량 줄은 매번 같다.

**ASan + swapcontext — 실제로 일어난 일.** 기본 옵션에서 ASan 은 스택 전환에 대해 아무 경고도 내지 않았고 43/43 이었다. 하지만 `ASAN_OPTIONS=detect_stack_use_after_return=1` 로 돌리면 `FAIL ... own` 이 하나 나온다: `&sum in A = 0x1053b9020` — 스택 배열 밖이다. ASan 의 use-after-return 탐지는 주소가 노출된 지역 변수를 힙의 **가짜 스택(fake stack)** 으로 옮기기 때문이다. 버그가 아니라 "지역 변수는 스택 배열 안" 이라는 이 레슨의 가정을 ASan 이 바꾼 것이라, 빌드 명령에 `detect_stack_use_after_return=0` 을 명시했다. 스레드가 없어 TSan 은 해당 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `uc_link`/`uc_stack` 을 설정한 **뒤에** `getcontext` | getcontext 가 둘 다 덮어쓴다 (P1). uc_link 가 NULL 이 돼 태스크 return 때 `_exit(0)` — 테스트 출력이 통째로 사라진다 | `getcontext(&c); c.uc_stack... ; c.uc_link = ...; makecontext(...)` |
| makecontext **전에** 스택 칠하기 | macOS makecontext 는 스택을 `__bzero` 한다 → HWM 이 "100% 사용" 으로 나온다 (P2) | makecontext 뒤에 `stack_paint` (인자 0 개라 makecontext 가 스택에 쓴 것이 없다) |
| `makecontext(&c, f, 1, ptr)` | 인자는 int. 이 맥에선 상위 32비트가 잘린다 (P3) | TCB/전역으로 넘기거나 hi/lo 두 int (연습 2) |
| `ucontext_t b = a;` 로 복사 | `uc_mcontext` 는 **원본의** `__mcontext_data` 를 가리킨다 (P6). 복사본으로 swap 하면 원본 레지스터가 덮인다 | 복사하지 말고 포인터로. 새로 만들려면 getcontext + makecontext |
| swap 반환값 무시 | 스택 없음/끝난 문맥은 `-1` 인데 그냥 다음 줄로 간다 → "태스크가 안 돌았는데 조용함" | `if (swapcontext(...) != 0) return -1;` |
| 끝난 태스크 문맥을 다시 resume | 이 맥은 `uc_mcsize = 0` 으로 막아 `EINVAL` 이지만 libc 마다 다르다 (미정의) | 상태를 DELETED 로 두고 스케줄러가 고르지 않게. 재시작은 새로 makecontext (연습 5) |
| yield 한 태스크의 `ctx.uc_stack` 으로 스택 범위를 계산 | swapcontext 가 그 칸을 현재 sp / 8 MB 로 덮는다 | TCB 에 `stack`, `stack_size` 를 따로 |
| 상태·`g_cur` 를 swap **뒤에** 갱신 | swap 뒤의 코드는 "다음에 내가 다시 뽑힐 때" 실행된다. 그 사이 상대는 틀린 `g_cur` 로 돈다 | 모든 갱신은 swap 전에 |
| 호스트에서 스택 4 KB | printf 가 든 태스크가 2 KB 를 쓴다. 넘치면 옆 정적 배열을 조용히 망가뜨릴 수 있다 | 호스트 모델은 ≥ 64 KB, MCU 에선 HWM 측정 후 여유 20~30% (경험칙) |
| 시그널 핸들러에서 swapcontext | POSIX 상 async-signal-safe 가 아니다 | RTOS-B05 에서 "학습용" 이라 명시하고 다룬다 |

## 8. 면접 Q&A

<details>
<summary>Q1. 컨텍스트 스위치 때 정확히 무엇을 저장하나요? 레지스터를 전부 저장하나요?</summary>

"다시 이어서 실행하는 데 필요한 것" 만: 레지스터, 스택 포인터, 재개 주소(pc 또는 lr), 상태 레지스터(xPSR/FPU 상태). **협력형** 스위치(함수 호출 안)는 호출 규약 덕에 callee-saved 만 저장하면 된다 — 이 맥 getcontext 는 x19–x28, fp, lr, sp, d8–d15 만 쓰고 x1–x18 과 pc 칸은 손대지 않았다 (demo_mcontext_layout). **선점형**(인터럽트가 아무 명령 사이에 끼어듦)은 모든 레지스터가 살아 있으니 전부 저장해야 하고, Cortex-M 은 그걸 하드웨어 반(r0–r3, r12, lr, pc, xPSR) + 소프트웨어 반(r4–r11)으로 나눈다. 꼬리: "TCB 에 저장하나 스택에 저장하나?" → FreeRTOS 는 **태스크 스택** 에 push 하고 TCB 첫 멤버 `pxTopOfStack` 에 그 주소만 둔다 (tasks.c:379). ucontext 는 TCB(ucontext_t) 안에 둔다. 둘 다 결국 "sp 바꿔치기" 다.

</details>

<details>
<summary>Q2. Cortex-M 에서 왜 PendSV 로 스위치하나요? SysTick 핸들러에서 바로 하면?</summary>

PendSV 는 **가장 낮은 우선순위** 로 설정된다 (FreeRTOS: `portNVIC_PENDSV_PRI = 255 << 16`, port.c:66, 433). 그래서 다른 ISR 이 다 끝난 뒤, 스레드 모드로 돌아가기 직전에만 돈다 → 중첩된 ISR 한가운데서 PSP 를 바꿔 치우는 일이 없다. SysTick 은 tick 만 세고 스위치가 필요하면 `portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT` 로 PendSV 를 "걸어" 둔다 (port.c:576). `taskYIELD()` 도 같은 비트를 쓴다 (portmacro.h:88–91). 여러 ISR 이 동시에 스위치를 요청해도 PendSV 는 한 번만 돈다. 꼬리: "ISR 에서 깨운 태스크로 바로 가려면?" → `portYIELD_FROM_ISR(woken)` 이 PendSV 를 걸고, 테일 체이닝으로 ISR 끝나자마자 실행 (RTOS-B08).

</details>

<details>
<summary>Q3. 태스크마다 스택이 따로 필요한 이유는? 스택 하나를 같이 쓰면?</summary>

양보한 태스크의 **호출 사슬(프레임들)이 그 자리에 얼어 있어야** 하기 때문이다. Part A 에서 A 가 `task_yield` 안에 멈춰 있는 동안 A 의 `worker` 프레임(`sum`, `note`, `i`)과 `swapcontext` 프레임이 스택 A 에 그대로 있고, 다시 뽑히면 그 위에서 이어진다. 스택 하나를 공유하면 B 의 호출이 A 의 프레임을 덮어쓴다. 스택 하나로 하는 방법이 **stackless 코루틴/protothreads** 다 — 대신 양보는 최상위 함수에서만 되고, 지역 변수는 양보를 넘어 살아남지 못한다(상태를 static/구조체에 직접 둬야 한다). 연습 3 의 파서는 스택이 따로 있어서 `next_byte()` 를 루프 깊숙이에서 불러도 된다. 꼬리: "RAM 이 20 KB 인 MCU 에 태스크 10개면?" → 태스크당 스택이 가장 큰 RAM 소비자다. HWM 측정으로 줄이고, 깊은 호출(printf, 큰 지역 배열)을 특정 태스크에 몰아 둔다.

</details>

<details>
<summary>Q4. 태스크 스택 크기는 어떻게 정하고, 오버플로는 어떻게 잡나요?</summary>

① 정적 분석(컴파일러 `-fstack-usage` + 호출 그래프, RTOS-M08) ② **측정**: 스택을 0xA5 로 칠하고 최악 시나리오를 돌린 뒤 HWM 확인 (`uxTaskGetStackHighWaterMark` — word 단위, tasks.c:6470–6479) ③ 여유를 더한다 (ISR 이 같은 스택을 쓰는지 확인: Cortex-M 은 ISR 이 MSP 라 태스크 스택에는 예외 프레임만). 탐지는 FreeRTOS 방법 1(스위치 때 sp 가 범위 밖인지)과 방법 2(스택 끝 16 B 패턴, stack_macros.h:101–118) — 둘 다 **스위치 순간에만** 보므로 넘쳤다 돌아온 경우나 이미 망가진 뒤를 잡는다. 확실한 건 하드웨어: MPU 가드 영역, ARMv8-M(Cortex-M33)의 `PSPLIM` 스택 한계 레지스터 (stack_macros.h 머리 주석도 Armv8-M 하드웨어 검사를 언급). 꼬리: "HWM 이 넉넉하면 안전한가?" → 측정한 경로만 안다. 드문 에러 경로·최대 ISR 중첩은 따로 따져야 한다.

</details>

<details>
<summary>Q5. swapcontext 는 왜 느린가요? 실제 코루틴 라이브러리는 어떻게 하죠?</summary>

이 맥에서 측정 (`demo_switch_cost.c`, 7회 중앙값): swapcontext **약 670–710 ns**, `sigprocmask` 1회 약 140–160 ns, 빈 함수 호출 0.7 ns. 비율 swap/sigprocmask ≈ 4.4–4.9. 디스어셈블한 대로 swapcontext 한 번에 시스템 콜이 4개다: getcontext 쪽 `__sigaltstack`, `getrlimit`, `sigprocmask` + setcontext 쪽 `sigprocmask`. 시그널 마스크를 문맥의 일부로 보존하는 POSIX 의미 때문이다. Boost.Context 같은 라이브러리나 RTOS 는 시그널 마스크를 다루지 않고 callee-saved 레지스터만 어셈블리로 저장해 수십 명령으로 끝낸다. Cortex-M 의 PendSV 도 push/pop 몇 개 + `vTaskSwitchContext` 다. 꼬리: "그럼 이 레슨의 측정이 MCU 에 의미가 있나?" → 절대 시간은 없다. "스위치 = 레지스터 저장 + sp 교체" 라는 구조와, 스위치 비용이 0 이 아니라는 감각만 가져간다.

</details>

<details>
<summary>Q6. FreeRTOS 태스크 함수가 return 하면 어떻게 되나요?</summary>

`pxPortInitialiseStack` 이 태스크의 초기 LR 을 `prvTaskExitError` 로 넣어 둔다 (port.c:217, 125). return 하면 거기로 가서 `configASSERT( uxCriticalNesting == ~0UL )` 로 assert 를 일부러 터뜨리고, 인터럽트를 끄고 무한 루프 (port.c:234–257). 태스크는 `for(;;)` 이거나 끝낼 때 `vTaskDelete(NULL)` 이어야 한다. ucontext 의 `uc_link` 가 정확히 그 "return 하면 갈 곳" 이고, 이 레슨은 그걸 커널 문맥으로 잡아 회수했다. `NULL` 이면 이 맥에서 `_exit(0)` (B6). 꼬리: "Zephyr 는?" → 스레드 entry 가 return 하면 그 스레드가 종료된다고 문서에 있다 (미검증).

</details>

<details>
<summary>Q7. FPU 레지스터도 매번 저장하나요?</summary>

다 저장하면 비싸다(Cortex-M4F 는 S0–S31 + FPSCR 로 33 word). 그래서 ① 호출 규약상 callee-saved 인 것만: AAPCS64 에서 v8–v15 의 **하위 64비트(d8–d15)** 만 callee-saved 라서 이 맥 getcontext 도 d8–d15 만 저장했다. ② Cortex-M4F: 하드웨어는 FPU 를 쓴 태스크일 때만 확장 프레임(S0–S15, FPSCR)을 쌓고(지연 스태킹, lazy stacking — ARM 문서 기준), PendSV 는 `tst r14, #0x10` 으로 EXC_RETURN 의 bit 4 를 보고 FPU 를 쓴 태스크일 때만 `vstmdbeq r0!, {s16-s31}` 한다 (port.c 의 PendSV). 꼬리: "ISR 에서 float 를 쓰면?" → 그 ISR 진입 때도 확장 프레임 공간이 잡히므로 스택·지연이 늘어난다. 펌웨어는 ISR 에서 FPU 를 피하거나 그 비용을 예산에 넣는다.

</details>

## 9. 추가 연습

### 연습 1 — 스케줄러 문맥을 거치는 양보

태스크가 다음 태스크로 **직접** 가지 않고, 자기 스택을 가진 스케줄러 문맥으로 갔다가(`task → sched`) 스케줄러가 다음을 고르는(`sched → task`) 구조로 바꿔라. 두 태스크 × 4 step 에서 swapcontext 횟수는?

<details>
<summary>답 보기</summary>

실제 출력: `ex1 via-scheduler: order=A0B0A1B1A2B2A3B3 swaps=19` = main→sched 1 + step 8개 × (sched→task, task→sched) 2 + 마지막 진입 2 (그 뒤 return 은 uc_link 라 swap 아님). 직접 전환이면 같은 일이 yield 8 + 커널 진입 2 = 10. 스케줄러 문맥 방식은 구조가 단순하고(스케줄 결정이 한 곳) 스택 위에서 디버깅하기 쉽지만 스위치가 두 배다. FreeRTOS 의 PendSV 는 별도 스케줄러 "스레드" 없이 핸들러 안에서 `vTaskSwitchContext` 를 부르고 바로 새 태스크를 복원한다 — 직접 전환 쪽이다 (핸들러는 MSP 를 쓰므로 태스크 스택을 소모하지 않는다).

</details>

### 연습 2 — 64비트 포인터를 makecontext 인자로

`makecontext` 의 int 인자 두 개로 포인터를 넘겨라.

<details>
<summary>답 보기</summary>

`makecontext(&c, entry_hilo, 2, (int)(uint32_t)(u >> 32), (int)(uint32_t)u)`, 받는 쪽은 `((uintptr_t)(uint32_t)hi << 32) | (uint32_t)lo`. `entry_hilo(int, int)` 는 `void (*)()` 와 호환되므로 캐스트가 필요 없다. 실제 출력: `ex2 pointer 0x1001cc000 -> received 0x1001cc000`. glibc 는 64비트 인자를 넘겨준다고 알려져 있지만(미검증) POSIX 가 보장하는 것은 int 뿐이다. 이식성이 필요하면 이 방식이나 TCB 경유.

</details>

### 연습 3 — 코루틴 UART 프레임 파서

UART RX ISR 이 바이트를 하나씩 넘긴다. 파서를 별도 스택의 코루틴으로 만들어, `next_byte()` 가 바이트가 필요할 때 feeder 에게 양보하게 하라. 프레임은 `$...\n`, 최대 15 글자, 넘치면 버린다. 입력 `"xx$IMU,12\n$BAT,87\n$THIS_LINE_IS_TOO_LONG\nnoise$GPS,OK\n"`.

<details>
<summary>답 보기</summary>

실제 출력: `ex3 frames=3 [IMU,12] [BAT,87] [GPS,OK], dropped=1`. 파서 코드는 "while 루프로 한 줄 읽기" 를 **그대로** 쓴다 — `buf`, `n`, `overflow` 가 지역 변수로 yield 를 넘어 살아남기 때문이다. 같은 걸 ISR 콜백 방식으로 쓰면 `enum { WAIT_DOLLAR, IN_FRAME }` 상태 기계 + 구조체에 `buf/n/overflow` 를 꺼내 둬야 한다. 코루틴의 "상태" = 멈춘 자리(lr) + 그 스택. 대가는 스택 하나(여기선 64 KB, MCU 라면 수백 B). 실제 RTOS 에서는 이 파서가 태스크이고 `next_byte()` 가 큐/스트림 버퍼 수신(RTOS-B11) 이다.

</details>

### 연습 4 — 재귀 깊이별 스택 사용량

`dive(d)` 가 64 B `volatile` 배열을 채우고 재귀한다. d = 8, 16, 32 에서 HWM 으로 사용량을 재고 프레임당 바이트를 구하라.

<details>
<summary>답 보기</summary>

실제 출력 (-O2): `ex4 used: depth8=872 depth16=1640 depth32=3176 bytes -> 96 bytes/frame`. 1640 − 872 = 768 = 8 × 96, 3176 − 1640 = 1536 = 16 × 96 → 정확히 선형. 96 B = 배열 64 + fp/lr 16 + 정렬·기타 16. ASan 빌드에선 `256 bytes/frame` (레드존 때문) — 측정은 **제품과 같은 빌드 옵션** 으로 해야 한다는 교훈. 상수 부분(≈ 872 − 8×96 = 104 B)은 진입 트램펄린과 libc 의 몫이다. RTOS-M08 에서 `-fstack-usage` 의 정적 값과 비교한다.

</details>

### 연습 5 — 끝난 태스크 재시작

끝난 문맥을 다시 resume 하면? 재시작하려면?

<details>
<summary>답 보기</summary>

실제 출력: `ex5 resume finished: ret=-1 errno=22; re-made: ret=0 runs=2`. 끝난 문맥은 (이 맥에서) 쓸 수 없고, getcontext + makecontext 를 **다시** 해야 처음부터 돈다. FreeRTOS 에는 "재시작" API 가 없다 — `vTaskDelete` 후 `xTaskCreate(Static)` 로 새로 만들거나, 태스크 본체를 `for(;;)` 로 두고 알림으로 "처음 상태로" 돌아가게 짠다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice      # 10/10 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_asan && \
  ASAN_OPTIONS=detect_stack_use_after_return=0 ./pr_asan                        # 10/10, 경고 0
```

데모 (각각 빌드해 실행, 위 출력은 이 맥에서 실제로 찍은 것):

<!-- code: demo_switch_cost.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_mcontext_layout.c  -o demo_layout   && ./demo_layout
cc -std=c11 -Wall -Wextra -Werror -O2 demo_ucontext_pitfalls.c -o demo_pitfalls && ./demo_pitfalls | cat
cc -std=c11 -Wall -Wextra -Werror -O2 demo_switch_cost.c      -o demo_cost     && ./demo_cost
```

```text
median of 7 reps, N=200000 each (this Mac, -O2):
  swapcontext       :   671.8 ns/switch
  sigprocmask       :   138.3 ns/call
  empty fn call     :    0.67 ns/call
  ratio swap/sigprocmask = 4.9, swap/call = 1003   (task ran 1401000 times)
```

(Apple M-series, 다른 작업이 함께 돌던 상태. 다시 돌리면 709 ns / 160 ns / 비율 4.4 — 절대값보다 "syscall 4개 분량" 이라는 비율을 본다.)

## 10. 핵심 정리

- 컨텍스트 스위치 = **나가는 태스크의 레지스터·sp 를 저장하고 들어오는 태스크 것을 싣는 것**. sp 가 바뀌면 "어느 스택 위의 호출 사슬" 이 바뀐다. 지역 변수는 복사되지 않는다 — 각자의 스택에 그대로 있을 뿐.
- 협력형(함수 호출 안) 스위치는 **callee-saved + sp + 복귀 주소** 면 충분하다: arm64 = x19–x28, fp, lr, sp, d8–d15 (이 맥 getcontext 관찰, pc 칸은 안 씀).
- Cortex-M 선점 스위치: 하드웨어가 r0–r3, r12, lr, pc, xPSR 를 PSP 에, PendSV 가 r4–r11(+r14, FPU 면 s16–s31)을 push, sp 는 TCB 첫 멤버 `pxTopOfStack` 에. PendSV 는 최저 우선순위.
- makecontext 순서: **getcontext → uc_stack/uc_link → makecontext**. 인자는 int 만(이 맥은 포인터를 자른다). return 하면 `uc_link`, NULL 이면 `_exit(0)`. 끝난 문맥은 재사용 불가(`EINVAL`).
- 이 맥 makecontext 는 스택을 0 으로 지운다 → **칠하기는 그 뒤**. HWM = 낮은 주소부터 남은 0xA5, 카나리 = 맨 아래 16 B, 검사는 switch-out 때 (FreeRTOS tasks.c:5259).
- FreeRTOS 태스크는 return 금지 (`prvTaskExitError`) → `for(;;)` 또는 `vTaskDelete(NULL)`.
- ucontext 는 시그널 마스크 때문에 스위치마다 syscall 4개(≈ 0.7 µs) — 학습용. 실제 RTOS/코루틴은 레지스터만 asm 으로.

## 참고자료

- POSIX (Issue 6, `_XOPEN_SOURCE 600`) `makecontext` / `swapcontext`, `getcontext` / `setcontext` — uc_link 의미, int 인자: <https://pubs.opengroup.org/onlinepubs/009695399/functions/makecontext.html>, <https://pubs.opengroup.org/onlinepubs/009695399/functions/getcontext.html> (Issue 7 에서 삭제됨)
- macOS SDK 헤더 (MacOSX26.4.sdk): `usr/include/ucontext.h` (deprecated 선언), `sys/_types/_ucontext.h`, `arm/_mcontext.h`, `mach/arm/_structs.h` (`_STRUCT_ARM_THREAD_STATE64`, `_STRUCT_ARM_NEON_STATE64`). 디스어셈블: lldb `disassemble -n getcontext / swapcontext / makecontext / _setcontext / _ctx_start / _ctx_done` (libsystem_platform.dylib)
- Arm, *Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)* — callee-saved x19–x29, v8–v15 하위 64비트: <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
- Arm, *ARMv7-M Architecture Reference Manual* — 예외 진입 스태킹, EXC_RETURN, 지연 FP 스태킹: <https://developer.arm.com/documentation/ddi0403/latest>
- FreeRTOS-Kernel V11.3.2 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `portable/GCC/ARM_CM4F/port.c` (66, 92–100, 125, 202–231, 234–257, 433, 504–557, 576), `portmacro.h` (83, 88–100), `tasks.c` (121, 129–135, 379, 1835, 1845–1849, 3742, 5215, 5259, 6470–6479, 6535), `include/stack_macros.h` (101–118), `include/task.h:227`, POSIX 포트 `portable/ThirdParty/GCC/Posix/port.c` 28–50 (태스크 = pthread, 조건 변수로 전환): <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS 문서 — Stack usage and stack overflow checking: <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/02-Stack-usage-and-stack-overflow-checking>
- Zephyr — Threads (`k_thread_create`, 스레드 종료), Stack sentinel / MPU stack guard: <https://docs.zephyrproject.org/latest/kernel/services/threads/index.html>
- CMSIS-RTOS2 — Thread Management (`osThreadNew`, `osThreadYield`, `osThreadGetStackSpace`): <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__ThreadMgmt.html>
- Adam Dunkels, *Protothreads* (stackless 코루틴, Q3 비교): <https://dunkels.com/adam/pt/>
- 관련 레슨: RTOS-B02 (TCB·상태 전이), RTOS-B04 (비트맵 ready list, 이 커널 확장), RTOS-B05 (SIGALRM 선점), RTOS-B08 (`portYIELD_FROM_ISR`), RTOS-B11 (큐), RTOS-M08 (스택 예산), RTOS-M09 (스택 오버플로 탐지), RTOS-M13 (FreeRTOS 내부 읽기), RTOS-A01 (Cortex-M 미니 RTOS — 진짜 PendSV), ARM-A01 (PendSV 컨텍스트 스위치 어셈블리), ARM-M01 (AAPCS64 callee-saved), C-M14 (ISR 공유 데이터)

---
id: RTOS-B05
time: 65분
updated: 2026-11-11
---
# RTOS-B05 선점과 라운드 로빈: SIGALRM tick 시뮬레이션

## 학습 목표

- tick 인터럽트 한 번에 커널이 하는 세 가지 — **지연 끝난 태스크 깨우기 → 더 급한 태스크면 선점 → 같은 우선순위면 time slice 회계** — 를 `kernel_tick()` 으로 직접 짜고, FreeRTOS `xTaskIncrementTick()` 의 해당 줄과 1:1 로 대응시킬 수 있다.
- 같은 우선순위 **라운드 로빈**(밀려난 태스크는 줄 맨 뒤)을 `sched_pick_next()` 로 구현하고, `configUSE_PREEMPTION` / `configUSE_TIME_SLICING` 조합 3가지의 타임라인 차이를 말할 수 있다 — 특히 "slicing 을 꺼도 같은 우선순위끼리 바뀌는 경우".
- `setitimer` + SIGALRM 핸들러 + `swapcontext` 로 **진짜 무한 루프 태스크를 선점** 하고, slice 길이(1/2/5/10 tick)별 문맥 교환 횟수를 측정해 `≈ N/s` 를 확인한다.
- 핸들러 ↔ 태스크 공유 데이터를 `volatile sig_atomic_t` 와 `sigprocmask` 임계 구역(= `taskENTER_CRITICAL`)으로 지키고, 임계 구역 중첩 카운트를 **태스크별로** 저장해야 하는 이유를 안다.
- 이 모델이 왜 "학습용" 인지(POSIX 가 보장하지 않음) 설명하고, 실제 Cortex-M 의 SysTick → PendSV 구조와 비교할 수 있다.

## 1. 문제

"선점형과 협력형 스케줄러의 차이는? tick 인터럽트에서 커널은 뭘 하죠? 같은 우선순위 태스크 둘이 무한 루프를 돌면?" — RTOS 면접에서 우선순위 역전(RTOS-M03) 바로 앞에 나오는 질문들이다. 답을 "선점은 OS 가 뺏는 것" 수준으로 하면 꼬리질문(slice 길이는? 누가 줄 맨 앞에 서나? 임계 구역을 길게 잡으면?)에서 막힌다. 이 레슨은 그 답을 **직접 만든 커널의 실측 타임라인** 으로 갖게 한다.

RTOS-B01 의 협력형 스케줄러는 "가장 긴 작업이 지연을 결정한다" 로 끝났다. 해결책이 선점(preemption)이다: 주기적인 **tick 인터럽트**(Cortex-M 의 SysTick, 보통 1 kHz)가 지금 도는 태스크를 멈추고, 커널이 "다른 태스크로 바꿔야 하나?" 를 판단한다. 맥에는 SysTick 이 없으므로 `setitimer(ITIMER_REAL)` 가 주는 SIGALRM 을 tick 으로 쓴다 — 사실 FreeRTOS 의 POSIX 포트도 SIGALRM 을 tick 으로 쓴다 (`portable/ThirdParty/GCC/Posix/port.c:40`).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">tick 하나의 일생 — 이 레슨(맥 호스트) vs Cortex-M + FreeRTOS</text>
  <text x="20" y="46" fill="currentColor" font-weight="bold">이 레슨</text> <text x="400" y="46" fill="currentColor" font-weight="bold">Cortex-M4 + FreeRTOS V11.3.2</text>
  <rect x="20" y="58" width="330" height="36" rx="5" class="f-mut" stroke="currentColor"/> <text x="32" y="81" fill="currentColor" font-family="monospace">setitimer(ITIMER_REAL, 1 ms) → SIGALRM</text>
  <rect x="400" y="58" width="320" height="36" rx="5" class="f-mut" stroke="currentColor"/> <text x="412" y="81" fill="currentColor" font-family="monospace">SysTick 1 kHz → SysTick 예외</text>
  <rect x="20" y="108" width="330" height="56" rx="5" class="f-warn" stroke="currentColor"/> <text x="32" y="130" fill="currentColor" font-family="monospace">on_tick(): g_tick++</text> <text x="32" y="152" fill="currentColor" font-family="monospace">need = kernel_tick()   ← TODO 1</text>
  <rect x="400" y="108" width="320" height="56" rx="5" class="f-warn" stroke="currentColor"/> <text x="412" y="130" fill="currentColor" font-family="monospace">xPortSysTickHandler (port.c:560)</text> <text x="412" y="152" fill="currentColor" font-family="monospace">xTaskIncrementTick() → pdTRUE?</text>
  <rect x="20" y="178" width="330" height="56" rx="5" class="f-acc" stroke="currentColor"/> <text x="32" y="200" fill="currentColor" font-family="monospace">need → port_switch()   ← TODO 4</text> <text x="32" y="222" fill="currentColor" font-family="monospace">  sched_pick_next()     ← TODO 2</text>
  <rect x="400" y="178" width="320" height="56" rx="5" class="f-acc" stroke="currentColor"/> <text x="412" y="200" fill="currentColor" font-family="monospace">ICSR.PENDSVSET = 1 (port.c:576)</text> <text x="412" y="222" fill="currentColor" font-family="monospace">→ PendSV: vTaskSwitchContext()</text>
  <rect x="20" y="248" width="330" height="36" rx="5" class="f-ok" stroke="currentColor"/> <text x="32" y="271" fill="currentColor" font-family="monospace">swapcontext(&amp;prev-&gt;ctx, &amp;next-&gt;ctx)</text>
  <rect x="400" y="248" width="320" height="36" rx="5" class="f-ok" stroke="currentColor"/> <text x="412" y="271" fill="currentColor" font-family="monospace">PSP 에 r4–r11 저장/복원 (port.c:520, 536)</text>
  <path d="M185 94 V106 M185 164 V176 M185 234 V246 M560 94 V106 M560 164 V176 M560 234 V246" stroke="currentColor" stroke-width="2"/> <text x="20" y="306" fill="currentColor">임계 구역: sigprocmask(SIG_BLOCK, {SIGALRM})  ↔  taskENTER_CRITICAL = BASEPRI 올리기 (TODO 3)</text> <text x="20" y="324" fill="currentColor">핸들러 안에서 swapcontext 는 POSIX 보장 밖 → 학습용. 실제 커널은 교환을 PendSV 로 미룬다.</text>
</svg>
```

**미니 커널 두 가지 모드 (하네스 제공).** 같은 커널 함수(TODO 4개)를 두 방식으로 돌린다.

| | sim 모드 (Part A, practice) | real 모드 (Part B, C) |
|---|---|---|
| tick | 루프가 `g_tick++; if (kernel_tick()) port_switch();` 를 부른다 | SIGALRM 핸들러 `on_tick()` 이 같은 일을 한다 |
| 태스크 | 각본: `SCRIPT_CPU`(CPU 를 안 놓음) / `SCRIPT_PERIODIC`(burst 후 지연) | 진짜 C 함수, 64 KB 스택, `makecontext` |
| 문맥 교환 | 없음 (`g_cur` 만 바뀜) | `swapcontext` |
| 결과 | 매번 같다 → 타임라인 문자열까지 정확히 CHECK | 실제 타이머라 흔들린다 → 넉넉한 범위 CHECK |
| 타임라인 | `R` 실행 · `r` READY · `B` 지연(BLOCKED) | `R` = tick 이 끝난 순간 CPU 를 쥔 태스크 |

어휘는 RTOS-B02~B04 와 같다 (`tcb_t`, `TASK_READY/RUNNING/BLOCKED/SUSPENDED`, 우선순위는 **클수록 급함** — FreeRTOS 방향, Zephyr/µC/OS 는 반대). ready list 는 우선순위별 이중 연결 FIFO 를 선형 탐색한다 (O(1) 비트맵은 RTOS-B04, 정렬된 지연 리스트는 RTOS-B06 — 여기서는 모든 태스크를 훑는다).

### Part A — 결정적 tick 시뮬레이터에서 커널 함수 4개

- `kernel_tick()` — (g_tick 은 이미 +1) ① `BLOCKED` 이고 `wake_tick` 에 도달한 태스크를 READY 줄 맨 뒤로 (**wrap 안전**: `(int32_t)(g_tick - wake) >= 0`), ② 선점이 켜져 있고 깨어난 태스크가 `g_cur` 보다 **급하면** 전환 필요, ③ 선점 + slicing 이면 `g_cur->slice_left--`, 0 이 되면 같은 우선순위 READY 가 있을 때만 전환 필요 (혼자면 slice 다시 채움). 반환 = 전환 필요?
- `sched_pick_next()` — `g_cur` 가 아직 `RUNNING` 이면(선점당함·slice 끝·yield) READY 로 바꿔 **자기 줄 맨 뒤** 에. 그다음 가장 높은 우선순위 줄의 맨 앞을 꺼내 `RUNNING`, slice 를 채워 돌려준다.
- `crit_enter/exit()` — SIGALRM 차단/해제, 중첩 가능 (바깥쪽에서만 실제로 막고 푼다).
- `port_switch()` — pick 한 태스크가 다르면 `g_ctx_switches++`, `g_cur = next`, real 모드면 `swapcontext`. **중첩 카운트는 지역 변수에 저장했다가 돌아오면 복원.**
- 검사: 단위 테스트 15개 + 시나리오 (H p2 4-tick 주기, HOG/B/C p1 무한 CPU) 를 협력형 / 선점만 / 선점+slicing(slice 1, 2) 로 → 타임라인 문자열 비교, slice 1/2/5/10 에서 교환 횟수 = `⌊119/s⌋` 정확히.

### Part B — 실제 SIGALRM: 무한 루프가 있어도 모두 돈다

`hog_task` 는 `for (;;) self->work++;` — 절대 양보하지 않는다. 1 ms tick, 200 tick 동안 같은 시나리오를 세 모드로 돌려 표를 만든다: 협력형이면 B·C 가 **0 tick**, 선점이면 H 가 4 tick 마다 **지연 0** 으로 돈다. 그다음 무한 루프 3개만 두고 slice 1/2/5/10 에서 문맥 교환 횟수를 잰다. 실제 타이머라 CHECK 는 "모두 ≥1 tick", "교환 횟수 ∈ [0.5, 1.5] × N/s" 처럼 넉넉하게 — 5번 이상 돌려 모두 PASS.

### Part C — IMU 샘플링 + 텔레메트리 + BLE (선점과 공유 데이터)

nRF52 BLE 센서 노드 같은 구성: `IMU`(p3) 가 4 ms 마다 `{seq, ax, ay, sum}` 레코드를 쓰고, `TELEM`(p1) 이 그것을 계속 복사해 검사하고, `BLE`(p1) 는 무한 루프로 스택을 돌린다. TELEM 이 4개 필드를 복사하는 도중 IMU 에게 **선점** 당하면 옛 seq + 새 ax 같은 "찢어진" 레코드를 읽는다. TELEM 의 복사를 `crit_enter/exit` 로 감싸서 `torn == 0`, IMU 는 최대 지연 ≤ 1 tick, 끝난 뒤 `g_crit_nest == 0` 이고 SIGALRM 이 막혀 있지 않음을 확인한다.

## 2. Problem (English)

> **Add tick-driven preemption and round-robin time slicing to a tiny kernel.**
>
> ```c
> bool   kernel_tick(void);       // called from the tick ISR after g_tick++; returns true if a switch is required
> tcb_t *sched_pick_next(void);   // highest-priority READY task; equal priorities rotate (preempted/expired task -> tail)
> void   crit_enter(void);        // mask the tick interrupt (nestable)
> void   crit_exit(void);
> void   port_switch(void);       // pick next; if different, context-switch to it
> ```
>
> Config: `preempt` (on/off), `slicing` (on/off), `slice` (ticks). A task whose delay expires is made READY; if it has a strictly higher priority than the running task, request a switch. With slicing, the running task's slice is decremented every tick; when it reaches 0 and another task of the same priority is READY, request a switch.
>
> 1. Show with a deterministic tick simulation the timelines of: cooperative, preemptive without slicing, preemptive with slicing.
> 2. On the host, use `setitimer(ITIMER_REAL)` / SIGALRM as the tick and `swapcontext` as the context switch. Show that a task that never yields (`for(;;) x++;`) no longer starves others, and measure context switches vs slice length (1, 2, 5, 10 ticks).
> 3. A high-priority IMU task writes a 4-field record that a low-priority task copies. Make the copy safe.
>
> Follow-ups: What does the tick ISR do in FreeRTOS? Why switch in PendSV instead of SysTick? What is FreeRTOS's time-slice length? Where does a preempted task go in the ready list? Is calling `swapcontext` from a signal handler legal? What happens if you hold a critical section longer than a tick?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부 — 2 초 안에 끝난다, 멈추지 않는다):

```text
FAIL starter.c:336  !kernel_tick() && g_cur->slice_left == 1
FAIL starter.c:337  kernel_tick()
...
     1 |        0 |                 119 | 120/0/0
...
 coop (preempt=0)     |   200.2 | 95121 | 4294967292 | 0/0/0 | 0
...
9/54 passed
```

스텁 `port_switch` 는 아무것도 안 하므로, real 모드에서 처음 디스패치된 H 가 `task_delay()` 를 불러도 비키지 못하고 200 ms 동안 혼자 돈다 (H 활성화 수만 번, HOG/B/C 0 tick). 하네스 설계:

- **멈추지 않는다.** 정지는 하네스 몫이다: 핸들러가 `g_tick >= g_stop_tick` 이면 TODO 와 상관없이 `swapcontext(&g_cur->ctx, &g_main_ctx)` 로 main 에 돌아온다. sim 은 tick 수만큼만 돈다.
- **`#define _XOPEN_SOURCE 600`** 이 첫 줄에 있다. 맥의 `<ucontext.h>` 는 이것 없이 include 하면 `#error` 를 낸다. 이 매크로가 있어도 `-std=c11` 로 `sigaction`, `setitimer`, `clock_gettime` 이 모두 보였다 (확인함).
- **`#pragma clang diagnostic ignored "-Wdeprecated-declarations"`** — 맥은 `getcontext/makecontext/swapcontext` 를 deprecated 로 표시한다 (POSIX.1-2008 에서 삭제된 함수들이다). 그 경고 **하나만** 끄고 `-Werror` 는 유지한다. 다른 경고는 여전히 빌드를 멈춘다.
- `(void)sched_pick_next;` 는 스텁 단계에서 unused-function 경고를 막는 줄이다.

## 4. 힌트

<details>
<summary>힌트 1 — kernel_tick 은 세 덩어리, 순서는 "깨우기 → 선점 → slice"</summary>

깨우기 루프 안에서 `t->prio > g_cur->prio` 이면 `need = true`. **`>`** 다 — 같은 우선순위가 깨어났다고 바로 뺏으면 slice 의 의미가 없어진다 (FreeRTOS 도 `>`: tasks.c:4944). slice 회계는 `g_cfg.preempt && g_cfg.slicing && g_cur->state == TASK_RUNNING` 일 때만.

</details>

<details>
<summary>힌트 2 — "같은 우선순위 READY 가 있나?" = 그 줄의 head 가 NULL 이 아닌가</summary>

이 커널에서 RUNNING 태스크는 ready list 에서 **빠져 있다**. 그래서 `g_rq_head[g_cur->prio] != NULL` 이면 "나 말고 또 있다". FreeRTOS 는 RUNNING 태스크도 리스트에 남겨 두므로 같은 질문을 `listCURRENT_LIST_LENGTH(...) > 1U` 로 한다 (tasks.c:4971).

</details>

<details>
<summary>힌트 3 — 라운드 로빈은 sched_pick_next 의 첫 4줄이 전부다</summary>

`g_cur` 가 아직 RUNNING 이면 READY 로 바꿔 `rq_push_tail`. 그다음 높은 우선순위부터 head 를 꺼낸다. 혼자 있는 태스크는 꼬리에 넣었다가 바로 다시 꺼내지므로 따로 처리할 필요가 없다 (`port_switch` 가 `next == prev` 를 걸러 교환 횟수에 안 센다).

</details>

<details>
<summary>힌트 4 — swapcontext 앞뒤로 중첩 카운트를 지역 변수에</summary>

A 가 `task_delay()` 안(crit 깊이 1)에서 B 로 바뀌었다. B 는 핸들러 안(깊이 1)에서 멈췄던 태스크라 깨어나 핸들러를 마저 끝내고 `g_crit_nest--` 한다. 전역 하나를 그대로 쓰면 A 의 깊이 1 이 B 로 새어 나간다. `sig_atomic_t nest = g_crit_nest; swapcontext(...); g_crit_nest = nest;` — 지역 변수는 **각 태스크 자기 스택** 에 있으므로 자동으로 태스크별이 된다. FreeRTOS POSIX 포트가 정확히 이렇게 한다 (port.c:583–599).

</details>

<details>
<summary>힌트 5 — crit 는 "깊이 0 일 때만" 막고 푼다</summary>

`if (g_crit_nest == 0) sigprocmask(SIG_BLOCK, ...); g_crit_nest++;` / `g_crit_nest--; if (g_crit_nest == 0) sigprocmask(SIG_UNBLOCK, ...);`. 핸들러도 들어올 때 `g_crit_nest++`, 나갈 때 `--` 하므로 핸들러 안에서 부른 커널 함수가 SIGALRM 을 풀어 버리는 일이 없다 (POSIX 포트 port.c:497 과 같은 생각).

</details>

## 5. 풀이

### Part A — 네 함수와 tick 순서

코드는 6. 정답 코드 (바뀐 줄 강조). `kernel_tick` 은 "깨우기 루프(안에서 선점 판단) → slice 블록" 두 덩어리, `sched_pick_next` 는 "RUNNING 이면 맨 뒤로 → 높은 줄부터 head 꺼내기" 두 덩어리다. 중요한 것은 **한 tick 의 순서** 다 (real 모드도 같은 순서가 되도록 맞췄다):

| 단계 | sim (`sim_run`) | real (`on_tick`) |
|---|---|---|
| ① tick 경계 | `g_tick++` → `kernel_tick()` → 필요하면 `port_switch()` | 같음 (핸들러 안) |
| ② 스스로 비킴 | burst 끝난 주기 태스크가 `task_delay(period - burst)` | 태스크가 아무 때나 `task_delay(4)` |
| ③ 실행 | `g_cur` 가 이 tick 을 쓴다 → 타임라인 `R` | 태스크 코드가 돈다 |

단위 테스트 (Part A1, 전부 PASS) 가 확인하는 것:

| 상황 | 기대 | 이유 |
|---|---|---|
| A,B,C p1, slice 2: tick 1 / tick 2 | false (남은 1) / true → B, A 는 줄 맨 뒤 `[C, A]` | slice 회계 + 라운드 로빈 |
| 거기서 `task_yield()` × 4 | `CABC` | yield = 차례 넘기기 |
| p1 에 A 혼자 (+idle), 10 tick | 전환 0, `yield` 해도 0 | 갈 곳이 없으면 slice 를 다시 채운다 |
| 협력형, H 가 tick 3 에 깨어남 | H 는 READY, 전환 없음 | `configUSE_PREEMPTION = 0` |
| 선점 O slicing X, H 깨어남 | tick 3 에 true, A 는 줄 맨 뒤 → H 가 비키면 **B** | 선점당한 태스크는 차례를 잃는다 |
| `g_tick = 0xFFFFFFFE`, `task_delay(3)` | tick 1 에 깨어남 | `g_tick >= wake` 였다면 즉시 깨어났다 (wrap, RTOS-B06) |
| `crit_enter` × 2, `crit_exit` × 2 | 깊이 2 → 1 → 0, 마지막에만 해제 | 중첩 |

### Part A2 — 세 가지 설정의 타임라인 (실제 출력)

```text
A2 coop (preempt=0)                 A2 preempt=1 slicing=0              A2 preempt=1 slicing=1 slice=1
tick : 012345678901234567890123     tick : 012345678901234567890123     tick : 012345678901234567890123
H    : RBBBrrrrrrrrrrrrrrrrrrrr     H    : RBBBRBBBRBBBRBBBRBBBRBBB     H    : RBBBRBBBRBBBRBBBRBBBRBBB
HOG  : rRRRRRRRRRRRRRRRRRRRRRRR     HOG  : rRRRrrrrrrrrrRRRrrrrrrrr     HOG  : rRrrrRrrrRrrrRrrrRrrrRrr
B    : rrrrrrrrrrrrrrrrrrrrrrrr     B    : rrrrrRRRrrrrrrrrrRRRrrrr     B    : rrRrrrRrrrRrrrRrrrRrrrRr
C    : rrrrrrrrrrrrrrrrrrrrrrrr     C    : rrrrrrrrrRRRrrrrrrrrrRRR     C    : rrrRrrrRrrrRrrrRrrrRrrrR
```

```svg
<svg viewBox="0 0 704 352" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">Part A2 실제 출력 (sim, 결정적) — H(p2, 4 tick 주기) + HOG/B/C(p1, CPU 를 안 놓음)</text>
  <text x="72" y="40" fill="currentColor">0</text> <text x="176" y="40" fill="currentColor">4</text> <text x="280" y="40" fill="currentColor">8</text> <text x="384" y="40" fill="currentColor">12</text> <text x="488" y="40" fill="currentColor">16</text> <text x="592" y="40" fill="currentColor">20</text>
  <text x="10" y="58" fill="currentColor" font-weight="bold">① 협력형 (configUSE_PREEMPTION=0): H 는 tick 4 에 깨어나지만 영원히 못 돈다, B·C 는 0 tick</text>
  <text x="10" y="76" fill="currentColor" font-family="monospace">H</text>
  <rect x="70" y="64" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="96" y="64" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="174" y="64" width="520" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="93" fill="currentColor" font-family="monospace">HOG</text>
  <rect x="70" y="81" width="26" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="96" y="81" width="598" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="110" fill="currentColor" font-family="monospace">B</text>
  <rect x="70" y="98" width="624" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="127" fill="currentColor" font-family="monospace">C</text>
  <rect x="70" y="115" width="624" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="152" fill="currentColor" font-weight="bold">② 선점 O, slicing X: H 는 제때. p1 은 H 가 끼어들 때만 차례가 바뀐다</text>
  <text x="10" y="170" fill="currentColor" font-family="monospace">H</text>
  <rect x="70" y="158" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="96" y="158" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="174" y="158" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="200" y="158" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="278" y="158" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="304" y="158" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="382" y="158" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="408" y="158" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="486" y="158" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="512" y="158" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="590" y="158" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="616" y="158" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="187" fill="currentColor" font-family="monospace">HOG</text>
  <rect x="70" y="175" width="26" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="96" y="175" width="78" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="174" y="175" width="234" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="408" y="175" width="78" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="486" y="175" width="208" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="204" fill="currentColor" font-family="monospace">B</text>
  <rect x="70" y="192" width="130" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="200" y="192" width="78" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="278" y="192" width="234" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="512" y="192" width="78" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="590" y="192" width="104" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="221" fill="currentColor" font-family="monospace">C</text>
  <rect x="70" y="209" width="234" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="304" y="209" width="78" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="382" y="209" width="234" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="616" y="209" width="78" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="246" fill="currentColor" font-weight="bold">③ 선점 O, slicing O, slice 1 (FreeRTOS 기본): 매 tick 교대</text>
  <text x="10" y="264" fill="currentColor" font-family="monospace">H</text>
  <rect x="70" y="252" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="96" y="252" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="174" y="252" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="200" y="252" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="278" y="252" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="304" y="252" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="382" y="252" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="408" y="252" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="486" y="252" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="512" y="252" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="590" y="252" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="616" y="252" width="78" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="281" fill="currentColor" font-family="monospace">HOG</text>
  <rect x="70" y="269" width="26" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="96" y="269" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="122" y="269" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="200" y="269" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="226" y="269" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="304" y="269" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="330" y="269" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="408" y="269" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="434" y="269" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="512" y="269" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="538" y="269" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="616" y="269" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="642" y="269" width="52" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="298" fill="currentColor" font-family="monospace">B</text>
  <rect x="70" y="286" width="52" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="122" y="286" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="148" y="286" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="226" y="286" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="252" y="286" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="330" y="286" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="356" y="286" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="434" y="286" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="460" y="286" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="538" y="286" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="564" y="286" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="642" y="286" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="668" y="286" width="26" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="315" fill="currentColor" font-family="monospace">C</text>
  <rect x="70" y="303" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="148" y="303" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="174" y="303" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="252" y="303" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="278" y="303" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="356" y="303" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="382" y="303" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="460" y="303" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="486" y="303" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="564" y="303" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="590" y="303" width="78" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="668" y="303" width="26" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="10" y="328" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="339" fill="currentColor">R 실행</text>
  <rect x="180" y="328" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="200" y="339" fill="currentColor">r READY (CPU 대기)</text>
  <rect x="350" y="328" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="370" y="339" fill="currentColor">B 지연 (task_delay)</text>
</svg>
```

읽는 법:

- **① 협력형**: H 는 tick 0 에 돌고 지연, tick 4 에 깨어나 `r` 이 되지만 HOG 가 CPU 를 놓지 않으니 **영원히** `r`. B·C 는 0 tick. 우선순위가 높아도 소용없다 — 협력형에서 우선순위는 "다음에 누구" 만 정한다.
- **② 선점만**: H 는 4 tick 마다 정확히 (지연 0). 그런데 HOG 만 도는 게 아니라 HOG → B → C 가 3 tick 씩 돈다. **slicing 을 껐는데도 같은 우선순위가 돌아간다** — H 가 HOG 를 선점할 때 HOG 가 줄 맨 뒤로 가고, H 가 비키면 줄 맨 앞(B)이 뽑히기 때문이다. FreeRTOS 도 같은 결과가 나오는 구조다: RUNNING 태스크를 리스트에 남겨 두고 `listGET_OWNER_OF_NEXT_ENTRY` 가 `pxIndex` 를 한 칸씩 **전진** 시키며 고르므로 (tasks.c:208–210, list.h:269–300), 선점에서 돌아올 때 다음 태스크로 넘어간다 (소스 읽기 기준, FreeRTOS 로 실행 확인은 안 함).
- **③ slice 1**: 매 tick 교대. H 가 깨어나는 tick 4 에는 깨우기(H 를 p2 줄에)와 slice 만료(C 를 p1 줄 맨 뒤)가 같은 ISR 에서 일어나고, 전환은 한 번.
- slice 2 (출력 생략, `B: rrrRrrrrrRRrrrrRrrrrrRRr`): tick 3 에 차례를 받은 B 는 tick 4 에 H 에게 선점당해 **slice 1 을 남긴 채** 줄 맨 뒤로 간다. 그래서 1 tick 짜리 차례가 생긴다 — POSIX 식(맨 앞, 남은 slice 유지)과의 비교는 연습 5.

### Part A3 — slice 길이 vs 문맥 교환 횟수 (결정적)

```text
A3 sim sweep: 3 CPU tasks, 120 ticks
 slice | switches | expect floor(119/s) | ticks A/B/C
     1 |      119 |                 119 | 40/40/40
     2 |       59 |                  59 | 40/40/40
     5 |       23 |                  23 | 40/40/40
    10 |       11 |                  11 | 40/40/40
```

120 tick 을 돌리면 tick ISR 은 경계 1..119 에서 119 번이다. slice s 는 ISR s, 2s, … 번째에 만료되므로 교환 = `⌊119/s⌋`. 교환 횟수는 **slice 에 반비례**, CPU 몫은 slice 와 무관하게 40/40/40.

### Part B — 핸들러 안에서 무슨 일이 일어나나

```c
static void on_tick(int sig) {                      /* SIGALRM = SysTick_Handler */
    int saved_errno = errno;
    if (g_stopped || g_cur == NULL) { errno = saved_errno; return; }
    g_crit_nest++;                                  /* 핸들러 안 = SIGALRM 차단 (sa_mask) */
    g_cur->ticks_run++; g_tick++;
    if (g_tick >= g_stop_tick) { g_stopped = 1; swapcontext(&g_cur->ctx, &g_main_ctx); }
    else if (kernel_tick()) port_switch();          /* 여기서 다른 태스크로 간다 */
    g_crit_nest--;                                  /* ... 언젠가 이 태스크가 다시 뽑히면 여기서 계속 */
    errno = saved_errno;
}
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">선점당한 태스크의 스택 — 호스트(SIGALRM) vs Cortex-M(PendSV)</text>
  <text x="20" y="44" fill="currentColor">HOG 의 64 KB 스택 (아래로 자람)</text>
  <rect x="20" y="52" width="320" height="30" class="f-mut" stroke="currentColor"/> <text x="30" y="72" fill="currentColor" font-family="monospace">task_entry → hog_task: work++ …</text>
  <rect x="20" y="82" width="320" height="44" class="f-warn" stroke="currentColor"/> <text x="30" y="100" fill="currentColor">커널(macOS)이 쌓은 시그널 프레임</text> <text x="30" y="119" fill="currentColor" font-family="monospace">끊긴 pc, sp, x0–x28, fp, lr, 마스크</text>
  <rect x="20" y="126" width="320" height="30" class="f-mut" stroke="currentColor"/> <text x="30" y="146" fill="currentColor" font-family="monospace">on_tick → port_switch (nest 지역변수)</text>
  <rect x="20" y="156" width="320" height="30" class="f-acc" stroke="currentColor"/> <text x="30" y="176" fill="currentColor" font-family="monospace">swapcontext → tcb.ctx 에 레지스터 저장</text> <text x="20" y="208" fill="currentColor">재개: swapcontext 복귀 → 핸들러 끝</text>
  <text x="20" y="226" fill="currentColor">→ sigreturn 이 시그널 프레임으로 work++ 루프 복원</text> <text x="400" y="44" fill="currentColor">태스크 스택 (PSP)</text>
  <rect x="400" y="52" width="320" height="30" class="f-mut" stroke="currentColor"/> <text x="410" y="72" fill="currentColor" font-family="monospace">task 함수 프레임</text>
  <rect x="400" y="82" width="320" height="44" class="f-warn" stroke="currentColor"/> <text x="410" y="100" fill="currentColor">하드웨어가 쌓는 예외 프레임</text> <text x="410" y="119" fill="currentColor" font-family="monospace">r0–r3, r12, lr, pc, xPSR (+FPU)</text>
  <rect x="400" y="126" width="320" height="30" class="f-acc" stroke="currentColor"/> <text x="410" y="146" fill="currentColor" font-family="monospace">PendSV 가 r4–r11, r14 (+s16–s31)</text> <text x="400" y="178" fill="currentColor">pxTopOfStack 을 TCB 첫 칸에 저장</text>
  <text x="400" y="208" fill="currentColor">핸들러 프레임은 MSP 에 → 태스크 스택에 안 남음</text> <text x="400" y="226" fill="currentColor">재개: r4–r11 pop → 예외 복귀가 나머지 복원</text>
  <text x="10" y="262" fill="currentColor">호스트는 "저장"이 두 군데(시그널 프레임 + swapcontext). 핸들러 프레임도 태스크 스택에 남는다 →</text>
  <text x="10" y="282" fill="currentColor">태스크 스택 예산에 시그널 프레임 + 핸들러 깊이를 더해야 한다 (Cortex-M 의 예외 프레임 예산 = RTOS-M08).</text>
</svg>
```

꼭 알아야 할 네 가지:

1. **핸들러 안에서 다른 태스크로 간다.** HOG 의 스택에는 "시그널 프레임 → `on_tick` → `port_switch` → `swapcontext`" 가 쌓인 채 멈춘다. 나중에 HOG 가 다시 뽑히면 `swapcontext` 가 돌아오고, 핸들러가 끝나며, 시그널 복귀(sigreturn)가 `work++` 루프를 끊긴 곳에서 되살린다. 처음 뽑히는 태스크는 `makecontext` 로 만든 `task_entry` 에서 시작한다.
2. **시그널 마스크.** 맥의 `swapcontext` 는 마스크도 저장·복원한다 — 확인함: SIGALRM 을 막은 문맥에서 안 막은 문맥으로 swap 하면 풀린 상태로 돌아왔다. 그래도 `task_entry` 는 처음에 `g_crit_nest = 0` + `SIG_UNBLOCK` 을 한다. 핸들러 안에서 처음 디스패치된 태스크가 "인터럽트 금지" 를 물려받아 다시는 선점되지 않는 사고를 막는 안전장치다 (7. 자주 하는 실수).
3. **왜 동작하나 / 왜 학습용인가.** POSIX 의 async-signal-safe 함수 목록 (XSH 2.4.3; 맥 `man sigaction` 의 목록도 같다 — 확인함) 에 `swapcontext` 는 없다. 게다가 C11 7.14.1.1p5 는 비동기 시그널 핸들러가 `volatile sig_atomic_t` (또는 lock-free atomic) 가 아닌 정적 객체를 건드리면 UB 라고 한다 — 이 핸들러는 ready list 를 고친다. 이 모델이 실제로 동작하는 이유는 **커널 자료구조를 태스크가 만질 때는 항상 SIGALRM 을 막은 상태(crit)** 이기 때문이다. 핸들러가 끼어드는 순간 커널 자료구조는 "아무도 안 쓰는 중" 이 보장된다 — 이것이 RTOS 의 "인터럽트 금지 임계 구역" 규칙 그 자체다. 남는 위험은 태스크가 **libc 안**(printf, malloc 의 내부 락)에서 끊겼을 때 다른 태스크가 같은 libc 를 부르는 경우 — FreeRTOS POSIX 포트도 같은 경고를 단다: "stdio should be called from a single task only or serialized" (port.c:43–49). 그래서 이 레슨의 태스크는 출력하지 않고 main 이 끝난 뒤 출력한다.
4. **`volatile`.** `work` 는 `volatile uint64_t` 다. 무한 루프가 언제 끊길지 컴파일러는 모르므로, volatile 이 아니면 레지스터에만 두고 메모리에 안 쓸 수도 있다. `g_crit_nest`, `g_stopped` 는 핸들러와 공유되므로 `volatile sig_atomic_t`. `g_tick` 은 태스크가 crit 안에서만 읽는다 (`task_get_tick` = `xTaskGetTickCount`, 이것도 tick 타입이 원자적이지 않은 포트에서는 critical section 으로 읽는다: tasks.c:4219–4224, FreeRTOS.h:2925–2942).

### Part B1 — 세 모드, 진짜 무한 루프 (실제 출력)

```text
B1 real SIGALRM (1 ms tick, 200 ticks)
 mode                 | wall ms | H act | H maxlat | ticks HOG/B/C | switches
 coop (preempt=0)     |   200.2 |     1 |        0 | 200/0/0 | 1
 preempt, slicing=0   |   200.2 |    50 |        0 | 68/68/64 | 99
 preempt, slicing=1   |   200.2 |    50 |        0 | 67/67/66 | 249
```

```svg
<svg viewBox="0 0 680 162" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">Part B1 실제 SIGALRM 실행 (preempt, slicing=1, 1 ms tick) — solution.c 출력 그대로</text>
  <text x="71" y="40" fill="currentColor">0</text> <text x="146" y="40" fill="currentColor">5</text> <text x="221" y="40" fill="currentColor">10</text> <text x="296" y="40" fill="currentColor">15</text> <text x="371" y="40" fill="currentColor">20</text> <text x="446" y="40" fill="currentColor">25</text> <text x="521" y="40" fill="currentColor">30</text> <text x="596" y="40" fill="currentColor">35</text>
  <text x="10" y="58" fill="currentColor" font-family="monospace">H</text>
  <rect x="70" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="130" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="190" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="250" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="310" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="370" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="430" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="490" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="550" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="610" y="46" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="75" fill="currentColor" font-family="monospace">HOG</text>
  <rect x="70" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="115" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="160" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="205" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="250" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="295" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="340" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="385" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="430" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="475" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="520" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="565" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="610" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="655" y="63" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="92" fill="currentColor" font-family="monospace">B</text>
  <rect x="85" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="130" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="175" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="220" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="265" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="310" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="355" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="400" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="445" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="490" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="535" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="580" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="625" y="80" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="109" fill="currentColor" font-family="monospace">C</text>
  <rect x="100" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="145" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="190" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="235" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="280" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="325" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="370" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="415" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="460" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="505" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="550" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="595" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="640" y="97" width="15" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="132" fill="currentColor">칸 = SIGALRM 이 온 순간 CPU 를 쥔 태스크. H 칸 = H 가 일을 시작한 tick (수 µs 후 지연).</text>
  <text x="10" y="150" fill="currentColor">무한 루프 3개가 1 tick 씩 돌아가고, H 는 0, 4, 8 … 에 정확히 깨어난다 (최대 지연 0 tick).</text>
</svg>
```

- 협력형: H 는 첫 1번뿐, B·C 0 tick, 교환 1번 (H → HOG). `hog_task` 하나가 시스템을 멈췄다.
- 선점만: H 50번 모두 지연 0 tick. p1 은 sim 과 같은 이유로 H 가 끼어들 때마다 차례가 넘어간다 (68/68/64). 교환 99 ≈ H 활성화당 2번 (들어가기 + 나오기).
- 선점 + slicing: 67/67/66 로 고르게, 교환 249 = 매 tick.
- 6번 실행에서 tick 단위 숫자(활성화, tick 몫, 교환 횟수)는 **매번 같았다** — 모든 결정이 tick 에서만 일어나기 때문이다. 흔들린 것은 wall ms (200.2–260.2) 와 루프 횟수뿐. 그래서 CHECK 를 "tick 기준은 정확, 시간 기준은 넉넉" 으로 나눴다.

### Part B2 — slice 길이별 실제 문맥 교환 횟수

```text
B2 real sweep: 3 hogs, 1 ms tick, 250 ticks
 slice | wall ms | switches | expect N/s | ratio | work share A/B/C
     1 |   250.2 |      249 |      250.0 |  1.00 | 0.33/0.34/0.33
     2 |   250.3 |      124 |      125.0 |  0.99 | 0.35/0.33/0.32
     5 |   250.2 |       49 |       50.0 |  0.98 | 0.32/0.35/0.33
    10 |   250.2 |       24 |       25.0 |  0.96 | 0.36/0.33/0.31
```

1 kHz tick 에서 slice 1/2/5/10 = 초당 교환 ≈ 1000/500/200/100 번, 같은 우선순위 3개가 다툴 때 새로 깨어난 태스크의 최악 대기 = 2/5/14/29 ms (연습 3: n·s − 1).

교환 횟수는 정확히 `N/s − 1` (마지막 tick 은 정지 처리). 일의 몫(work share)은 slice 와 상관없이 ≈ 1/3. slice 를 줄여도 **처리량 손실이 안 보인다**: demo E3 에서 잰 `swapcontext` 한 번이 약 0.63 µs, 시그널 한 번이 약 0.75 µs → 1 ms 마다 한 번이면 0.1 % 대라서 실행마다의 잡음(±3 %)에 묻힌다. slice 의 진짜 비용은 처리량보다 **응답 시간** 쪽이다 (위 문장의 최악 대기). Cortex-M 의 PendSV 교환은 수십~백여 사이클이라 비율은 더 작지만, tick 을 10 kHz 로 올리면 tick ISR 자체가 부담이 된다 (tickless 는 RTOS-A02).

### Part C — 찢어진 레코드와 임계 구역

`telem_task` 는 무한 루프에서 `crit_enter(); r = g_rec (4필드 복사); crit_exit();` 후 `r.sum` 을 다시 계산해 맞지 않으면 `g_torn++`. `IMU` 는 4 tick 마다 `seq → ax → ay → sum` 순서로 쓴다.

solution 실제 출력: `C IMU/TELEM/BLE: wall 305.2 ms, IMU act=75 maxlat=0, TELEM reads=411793 torn=0, ticks TELEM/BLE=150/150, nest=0`.

누가 임계 구역을 가져야 하나? **선점당할 수 있는 쪽** 이다. IMU 는 가장 높은 우선순위라 쓰는 도중 어떤 태스크도 끼어들 수 없다 (tick ISR 은 끼어들지만 레코드를 안 만진다) → IMU 쪽은 보호가 필요 없다. TELEM 은 읽는 도중 IMU 가 끼어들 수 있으므로 읽는 쪽을 막는다. 같은 우선순위끼리였다면 slice 만료로도 끊기므로 양쪽 모두 필요하다. 싱글 코어 가정이다 (SMP 는 RTOS-A08).

`demo_realtime.c` 로 crit 를 빼 보면 (실제 출력, 300 tick × 5회):

```text
E1 torn reads (IMU p3 writes every 4 ticks, TELEM p1 copies 4 fields), 300 ticks x 5 runs
  run 0: no crit torn=  1 / reads=203321248   |  crit torn=0 / reads=420865
  run 1: no crit torn=  8 / reads=227474781   |  crit torn=0 / reads=420902
  run 2: no crit torn=  5 / reads=206112659   |  crit torn=0 / reads=442619
  run 3: no crit torn=  4 / reads=206854446   |  crit torn=0 / reads=421366
  run 4: no crit torn=  4 / reads=193717129   |  crit torn=0 / reads=415264
```

- crit 없이 2억 번 읽는 동안 찢어진 레코드 1–8개 — **드물고, 매번 다르다**. 실험실 테스트에서 안 보이고 현장에서 가끔 나는 버그의 전형이다. 그래서 solution 의 CHECK 는 "crit 이면 0" 만 본다.
- crit 를 넣으면 읽기 횟수가 2억 → 42만으로 약 500배 준다. 호스트의 crit 는 `sigprocmask` **시스템 콜 2번**(E3: 약 347 ns)이기 때문이다. Cortex-M 의 `taskENTER_CRITICAL` 은 BASEPRI 쓰기 몇 사이클이다 — 호스트 숫자로 펌웨어 비용을 판단하면 안 된다.

E2 — 임계 구역을 tick 보다 길게 쥐면 tick 을 **잃는다** (실제 출력):

```text
E2 lost ticks: one task holds crit (SIGALRM blocked) for X us, loops; 1 ms tick, run until tick 100
  hold us | wall ms | ticks | ms per tick
      200 |   100.4 |   100 |  1.00
      900 |   101.0 |   100 |  1.01
     2500 |   250.2 |   100 |  2.50
     5500 |   550.2 |   100 |  5.50
```

SIGALRM 이 막혀 있는 동안 타이머는 계속 울리지만 보류(pending) 시그널은 **1개로 합쳐진다**. 2.5 ms 동안 막으면 2.5번 울린 tick 이 1번만 처리되어 "tick 100" 에 250 ms 가 걸렸다 — 커널 시계가 실제보다 2.5배 느려졌다. Cortex-M 의 SysTick 도 pending 비트가 하나라 같은 일이 생긴다. 그래서 오래 걸리는 일은 인터럽트를 막지 말고 **스케줄러만 잠근다** (`vTaskSuspendAll`: tick 은 ISR 에서 계속 세고 `xPendedTicks` 에 쌓아 두었다가 `xTaskResumeAll` 이 몰아서 처리 — tasks.c:5064, 4150–4171 → 연습 4).

### FreeRTOS V11.3.2 와 나란히 (소스 직접 확인)

| 이 레슨 | FreeRTOS | 위치 |
|---|---|---|
| `kernel_tick()` ① 깨우기 | 지연 리스트 head 부터 `xConstTickCount < xItemValue` 일 때까지 꺼내 `prvAddTaskToReadyList`. wrap 은 비교식이 아니라 지연 리스트 2개를 바꿔치기해서 처리 (`taskSWITCH_DELAYED_LISTS`, 4860 — RTOS-B06) | tasks.c:4873–4928 |
| ② `t->prio > g_cur->prio` | `if( pxTCB->uxPriority > pxCurrentTCB->uxPriority ) xSwitchRequired = pdTRUE;` | tasks.c:4944 |
| ③ slice | `#if ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 )` → `listCURRENT_LIST_LENGTH( &pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) > 1U` 이면 전환 | tasks.c:4967–4971 |
| slice 길이 `g_cfg.slice` | **설정 없음 — 매 tick 검사하므로 slice = 1 tick 고정**. 길게 하려면 tick 을 느리게 | 위 블록 |
| slicing 기본값 | `configUSE_TIME_SLICING` 미정의 → 1 | FreeRTOS.h:2771–2773 |
| `rq_push_tail` | `listINSERT_END` — "pxIndex 바로 앞 = 다음 순회에서 가장 나중" | tasks.c:287–293, list.h:340–370 |
| `sched_pick_next` | `taskSELECT_HIGHEST_PRIORITY_TASK`: 비어 있지 않은 최고 우선순위 → `listGET_OWNER_OF_NEXT_ENTRY` 로 한 칸 전진 | tasks.c:197–212 |
| `on_tick` → `port_switch` | `xPortSysTickHandler`: `xTaskIncrementTick()` 이 pdTRUE 면 `portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT` | ARM_CM4F/port.c:560–584 |
| `swapcontext` | PendSV: `stmdb r0!, {r4-r11, r14}` → `bl vTaskSwitchContext` → `ldmia r0!, {r4-r11, r14}` | ARM_CM4F/port.c:520–536 |
| SysTick·PendSV 우선순위 | 둘 다 최저 (`portMIN_INTERRUPT_PRIORITY` 255) | ARM_CM4F/port.c:65–67, 433–434 |
| POSIX 포트의 tick | 타이머 스레드가 `usleep` + `pthread_kill(SIGALRM)` (setitimer 아님), 핸들러가 `xTaskIncrementTick` → `vTaskSwitchContext` → 스레드 전환 | Posix/port.c:452–470, 488–512 |
| `crit_enter` | POSIX 포트: `pthread_sigmask(SIG_BLOCK, &xAllSignals)` + `uxCriticalNesting` | Posix/port.c:354–372, 406–412 |

왜 SysTick 안에서 바로 교환하지 않고 PendSV 를 거는가: PendSV 는 최저 우선순위라 **다른 ISR 이 모두 끝난 뒤** 에 실행된다. 중첩된 ISR 들이 각자 "전환 필요" 를 말해도 교환은 마지막에 한 번, 그리고 ISR 스택 프레임 한가운데서 태스크를 바꾸는 일이 없다. 이 레슨의 핸들러 안 `swapcontext` 는 바로 그 "한가운데서 바꾸기" 라서 시그널 프레임이 태스크 스택에 남는다 (위 그림).

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | FreeRTOS (소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 / RTX5 (문서 기준, 미검증) |
|---|---|---|---|
| `g_cfg.preempt` | `configUSE_PREEMPTION` (필수 정의, FreeRTOS.h:190–192) | 스레드 우선순위로: 음수 = cooperative, 0 이상 = preemptible | 항상 선점형 |
| `g_cfg.slicing` / `slice` | `configUSE_TIME_SLICING`, slice = 1 tick | `CONFIG_TIMESLICING`, `CONFIG_TIMESLICE_SIZE`(ms), `k_sched_time_slice_set()` | RTX_Config.h `OS_ROBIN_ENABLE`, `OS_ROBIN_TIMEOUT` |
| `on_tick` + `kernel_tick` | `SysTick_Handler` → `xPortSysTickHandler` → `xTaskIncrementTick` | 시스템 클럭 드라이버 → `sys_clock_announce()` | RTX 의 SysTick 처리 |
| `port_switch` (swapcontext) | PendSV → `vTaskSwitchContext` | 아키텍처 swap (Cortex-M 도 PendSV) | PendSV |
| `crit_enter/exit` | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` | `irq_lock()` / `irq_unlock()` | `__disable_irq()` + PRIMASK 복원 (C-M14) |
| `task_yield` | `taskYIELD()` | `k_yield()` | `osThreadYield()` |
| `task_delay(n)` | `vTaskDelay(n)` | `k_sleep(K_TICKS(n))` | `osDelay(n)` |
| `task_get_tick` | `xTaskGetTickCount()` (tasks.c:4213) | `k_uptime_ticks()` | `osKernelGetTickCount()` |
| `sched_suspend/resume` (연습 4) | `vTaskSuspendAll()` / `xTaskResumeAll()` (tasks.c:3890, 4064) | `k_sched_lock()` / `k_sched_unlock()` | `osKernelLock()` / `osKernelUnlock()` |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 54/54 passed, 약 2.0 초
cc -std=c11 -g -O1 -fsanitize=undefined solution.c -o so_ub && ./so_ub           # 54/54, 경고 0
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o so_asan && ./so_asan   # 54/54, 경고 0
```

6번 연속 실행 모두 `54/54 passed`, exit 0 (실제 타이머 부분 포함). ASan 은 시그널 핸들러 안의 `swapcontext` 와 직접 할당한 스택에 대해 **아무 경고도 내지 않았다** — `ASAN_OPTIONS=detect_stack_use_after_return=0` 같은 옵션도 필요 없었다 (이 맥, Apple clang 에서 확인. 다른 플랫폼에서 ASan 이 스택 전환을 오탐할 수 있다는 점은 일반적으로 알려져 있으니, 그때는 그 옵션부터 시도). pthread 를 쓰지 않으므로 TSan 대상이 아니다 — 이 레슨의 동시성은 "스레드" 가 아니라 "인터럽트(시그널)" 이고, 그 경쟁은 TSan 이 아니라 임계 구역 규칙과 Part C 의 `torn` 검사로 잡는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 깨우기 비교를 `g_tick >= t->wake_tick` | tick 이 wrap 하면 (`0xFFFFFFFE` 에서 `delay(3)` → wake 1) **즉시** 깨어난다 (Part A1 테스트) | `(int32_t)(g_tick - t->wake_tick) >= 0` |
| 선점 조건을 `>=` | 같은 우선순위가 깨어날 때마다 현재 태스크를 뺏는다 → slice 무의미, 교환 폭증 | `t->prio > g_cur->prio` (tasks.c:4944 도 `>`) |
| `sched_pick_next` 에서 RUNNING 이던 `g_cur` 를 줄에 다시 안 넣음 | 선점당한 태스크가 어느 리스트에도 없다 → 영원히 사라짐 | `if (g_cur->state == TASK_RUNNING) { state = READY; rq_push_tail(g_cur); }` |
| 선점당한 태스크를 항상 줄 **맨 앞** 에 | 그것도 한 설계(POSIX)지만, slice 만료/yield 까지 맨 앞이면 같은 태스크가 계속 뽑힌다 → 라운드 로빈 아님 | slice 만료·yield 는 맨 뒤. 선점만 맨 앞으로 하려면 연습 5 |
| slice 만료인데 같은 우선순위가 없을 때 slice 를 안 채움 | `slice_left` 가 0 에 머물러 다음 tick 마다 "만료" → 혼자인데 의미 없는 검사 반복, 누군가 오면 즉시 뺏김 | `else g_cur->slice_left = slice_of(g_cur);` |
| 중첩 카운트를 전역 하나로만 (swap 전후 저장 안 함) | crit 안에서 비킨 태스크의 깊이가 다음 태스크로 샌다 → 그 태스크의 `crit_enter` 가 "이미 막혀 있음" 으로 착각하고 **안 막는다** | swap 전에 지역 변수에 저장, 돌아와서 복원 (Posix/port.c:583–599) |
| 새 태스크를 핸들러 안에서 처음 띄우는데 마스크를 안 풂 | (getcontext 를 SIGALRM 이 막힌 뒤 부른 경우) 그 태스크는 영원히 "인터럽트 금지" → 다시는 선점되지 않는다 | `task_entry` 첫 줄에서 `g_crit_nest = 0; sigprocmask(SIG_UNBLOCK, ...)` |
| 핸들러에서 `errno` 를 안 지킴 | 끊긴 태스크가 막 읽으려던 errno 를 `sigprocmask`/`sigsuspend` 가 덮는다 | 들어올 때 저장, 나갈 때 복원 |
| 태스크마다 `printf` | libc 내부 락을 쥔 채 선점되면, 같은 락을 원하는 다른 태스크가 영원히 기다린다 | 출력은 한 태스크(또는 main)에서만, 아니면 crit/mutex 로 직렬화 (Posix/port.c:43–49) |
| 긴 일을 crit 안에서 | tick 이 합쳐져 **커널 시계가 느려진다** (E2: 2.5 ms 잡으면 2.5배) | 짧은 RMW 만 crit, 긴 일은 스케줄러 잠금(연습 4)이나 뮤텍스 |
| 무한 루프 카운터를 일반 변수로 | 루프가 레지스터에서만 돌아 main 이 0 을 읽을 수 있다 | `volatile` (또는 C11 atomic) |

## 8. 면접 Q&A

<details>
<summary>Q1. 선점형과 협력형 스케줄러의 차이는? 무한 루프 태스크가 하나 있으면 각각 어떻게 되나요?</summary>

협력형은 태스크가 스스로 비킬 때(yield, 지연, 블록)만 바뀐다. 선점형은 **인터럽트**(tick 또는 이벤트 ISR) 뒤에 커널이 "더 급한 태스크가 READY 인가" 를 보고 뺏는다. 무한 루프 하나가 있으면 협력형은 그 태스크만 돈다 — B1 실측: B·C 0 tick, 우선순위 높은 H 도 첫 1번 이후 0번. 선점형은 H 가 50번 모두 지연 0 tick 으로 돌았다. 꼬리: "협력형의 장점은?" → 공유 데이터 보호가 쉽다 (끊기는 지점이 yield 뿐), 문맥 교환이 함수 호출 경계에서만 일어나 스택·레지스터 저장이 적고 분석이 쉽다. 그래서 작은 MCU 의 superloop/run-to-completion 스케줄러(RTOS-B01)나 Zephyr 의 cooperative 스레드(음수 우선순위, 문서 기준)가 쓰인다. 단, 최악 지연 = 가장 긴 작업.

</details>

<details>
<summary>Q2. tick 인터럽트에서 FreeRTOS 커널은 정확히 무엇을 하나요? 왜 SysTick 에서 바로 문맥 교환을 안 하죠?</summary>

`xPortSysTickHandler` 가 `xTaskIncrementTick()` 을 부른다 (ARM_CM4F/port.c:560–584). 그 안에서 ① `xTickCount++`, ② 지연 리스트 head 부터 깨어날 시각이 된 태스크를 ready list 로 (tasks.c:4873–4928), 깨운 태스크가 현재보다 높으면 `xSwitchRequired` (4944), ③ 선점 + slicing 이면 현재 우선순위 ready list 에 2개 이상일 때 `xSwitchRequired` (4967–4971), ④ tick hook. pdTRUE 면 PendSV 를 pend 할 뿐이다 (576). 교환은 최저 우선순위 PendSV 가 다른 ISR 이 다 끝난 뒤 한 번 한다 — 중첩 ISR 중간에서 바꾸지 않고, 여러 ISR 의 요청을 한 번으로 합친다. 꼬리: "스케줄러가 잠겨 있으면(vTaskSuspendAll)?" → tick 을 세지 않고 `xPendedTicks += 1U` (tasks.c:5064), 풀 때 몰아서 처리 (4150–4171) — 연습 4.

</details>

<details>
<summary>Q3. FreeRTOS 의 time slice 길이는? time slicing 을 끄면 같은 우선순위 태스크는 절대 안 바뀌나요?</summary>

길이를 정하는 설정이 없다. 매 tick "현재 우선순위 ready list 에 2개 이상인가" 를 보므로 **slice = 1 tick** (tasks.c:4967–4971). `configUSE_TIME_SLICING` 기본값 1 (FreeRTOS.h:2771). Zephyr 는 `CONFIG_TIMESLICE_SIZE`, RTX5 는 `OS_ROBIN_TIMEOUT` 로 길이를 정한다 (문서 기준). slicing 을 꺼도 바뀌는 경우가 있다: **높은 태스크가 선점했다가 비키면** 다음 태스크로 넘어간다. 이 레슨의 A2 ② (HOG → B → C 가 H 주기마다 교대) 와 B1 (68/68/64) 가 그것이다. FreeRTOS 소스에서는 `listGET_OWNER_OF_NEXT_ENTRY` 가 `pxIndex` 를 전진시키며 고르기 때문이다 (tasks.c:208–210). 꼬리: "선점당한 태스크는 줄 어디에?" → FreeRTOS 식으로는 사실상 맨 뒤(차례를 잃음). POSIX SCHED_FIFO/RR 은 "선점당한 스레드는 자기 우선순위 줄의 맨 앞" 이라고 규정한다 (XSH 2.8.4). 연습 5 에서 두 방식을 비교한다.

</details>

<details>
<summary>Q4. slice 를 짧게 하면 무엇이 좋아지고 무엇이 나빠지나요?</summary>

교환 횟수 ∝ 1/s (A3 정확히 ⌊119/s⌋, B2 실측 249/124/49/24). 같은 우선순위 n 개가 CPU 를 다툴 때 새로 깨어난 태스크의 최악 대기 = **n·s − 1 tick** (연습 3: 위상 전수 조사로 확인) — slice 가 짧을수록 응답이 좋다. 대가는 교환 오버헤드와 캐시/파이프라인 손실. 이 맥에서는 swapcontext 0.63 µs + 시그널 0.75 µs 라 1 ms tick 에서 측정 불가 수준이었다. 실무 요점: 실시간 응답은 slice 가 아니라 **우선순위** 로 보장한다. 같은 우선순위 라운드 로빈은 "배경 일 공평하게 나누기" 용이고, 마감이 있는 일은 높은 우선순위 + 블록/지연 구조로 만든다 (RTOS-M11 RMA). 꼬리: "tick 을 10 kHz 로 올리면?" → 모든 tick 이 ISR 비용 + (전환 시) PendSV. 전력도 나빠진다 → tickless idle (RTOS-A02).

</details>

<details>
<summary>Q5. 시그널 핸들러에서 swapcontext 를 불러도 되나요? 이 모델은 왜 동작하죠?</summary>

POSIX 는 보장하지 않는다. `swapcontext` 는 async-signal-safe 목록(XSH 2.4.3)에 없고, ucontext 함수들은 POSIX.1-2008 에서 삭제됐다 (맥은 deprecated 표시). C11 7.14.1.1p5 로 보면 핸들러가 `volatile sig_atomic_t` 아닌 전역(ready list)을 고치는 것 자체가 UB 다. 실제로 동작하는 이유: 태스크는 커널 자료구조를 **SIGALRM 을 막은 상태에서만** 만진다 → 핸들러가 끼어드는 순간 그 자료구조는 일관된 상태다. 남는 구멍은 libc 내부(printf/malloc 락) — 그래서 태스크에서 출력하지 않는다. FreeRTOS POSIX 포트도 같은 구조와 같은 경고를 갖고 있다 (port.c:43–49, 488–512). 꼬리: "실제 MCU 에서는?" → 하드웨어 예외 진입/복귀가 정의된 동작이고, PendSV 가 r4–r11 만 저장하면 된다 (나머지는 하드웨어가 이미 쌓았다).

</details>

<details>
<summary>Q6. 임계 구역(인터럽트 금지)과 스케줄러 잠금의 차이는? 임계 구역을 길게 잡으면?</summary>

임계 구역은 tick 을 포함한 인터럽트를 막는다 → ISR 과 공유하는 데이터 보호용, **짧게** (수 µs). 스케줄러 잠금(`vTaskSuspendAll`)은 인터럽트는 받되 태스크 전환만 미룬다 → 긴 태스크 간 작업용, ISR 지연은 늘지 않는다. 길게 잡은 임계 구역의 함정: tick 이 합쳐져 **잃어버린다**. E2 실측: SIGALRM 을 2.5 ms 씩 막으면 커널 시계가 2.5배 느려졌다 (100 tick 에 250 ms). Cortex-M 의 SysTick pending 도 한 비트라 같다. 스케줄러 잠금은 ISR 이 `xPendedTicks` 로 세어 두므로 tick 을 잃지 않는다 (연습 4: 잠긴 7 tick 을 풀 때 몰아서 처리, `g_tick == 15`). 꼬리: "FreeRTOS 의 taskENTER_CRITICAL 은 모든 인터럽트를 막나?" → Cortex-M3/M4 포트는 BASEPRI 로 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하만 막는다. 그보다 높은 인터럽트는 계속 들어오지만 FreeRTOS API 를 부를 수 없다 (FreeRTOS 문서 기준).

</details>

<details>
<summary>Q7. 실제 타이머로 도는 스케줄러를 어떻게 테스트했나요?</summary>

두 층으로 나눴다. ① 결정 로직(`kernel_tick`, `sched_pick_next`)은 **문맥 없는 tick 시뮬레이터** 에서 돌려 타임라인 문자열·교환 횟수를 정확히 비교한다 (A1 15개, A2/A3). ② 실제 SIGALRM + swapcontext 는 **tick 기준 숫자** (활성화 횟수, tick 몫, 교환 횟수 — 모든 결정이 tick 에서 일어나므로 6번 모두 같았다) 와 **시간 기준 숫자** (wall ms, 루프 횟수 — 흔들림) 를 구분해, 앞의 것은 좁게, 뒤의 것은 `[0.5, 1.5]`·`[0.5, 3.0]` 처럼 넓게 검사했다. 그리고 5번 이상 반복. 찢어진 읽기처럼 확률적인 버그는 "있음" 을 CHECK 하지 않고 (E1: 0–8개로 들쭉날쭉) "고친 뒤 0" 만 CHECK 한다. 꼬리: "실제 보드에서는?" → GPIO 토글 + 로직 분석기로 tick·PendSV·태스크 진입을 보거나, SEGGER SystemView / Tracealyzer 트레이스로 같은 타임라인을 얻는다 (RTOS-A06).

</details>

## 9. 추가 연습

모두 결정적 sim 모드 (`practice.c`, 실제 시그널 없음).

### 연습 1 — slicing 끔 + `task_yield()`

`preempt=1, slicing=0` 에서 A·B·C(p1) 가 3 tick 일하고 `task_yield()`. 혼자일 때 yield 는? 양보하지 않는 태스크가 끼면?

<details>
<summary>답 보기</summary>

```text
ex1 slicing=0, each task yields after 3 ticks
tick : 012345678901234567
A    : RRRrrrrrrRRRrrrrrr
B    : rrrRRRrrrrrrRRRrrr
C    : rrrrrrRRRrrrrrrRRR
```

교환 5번. 같은 우선순위에 혼자면 yield 는 교환 0 으로 바로 돌아온다 (idle 은 우선순위 0 이라 못 뺏는다). A 가 `SCRIPT_CPU`(양보 안 함)면 18 tick 모두 A, B·C 0 — slicing 이 없으면 **같은 우선순위 공평성은 태스크의 예의에 달렸다**. FreeRTOS 의 `taskYIELD()` 가 같은 일을 한다 (`vTaskDelay(0)` 도 yield 효과 — RTOS-B06).

</details>

### 연습 2 — 태스크별 slice (가중 라운드 로빈)

`tcb_t.slice` 를 A=3, B=2, C=1 로 주면 60 tick 동안 CPU 몫은?

<details>
<summary>답 보기</summary>

`slice_of(t)` 가 이미 `t->slice ? t->slice : g_cfg.slice` 라서 코드 변경 없이 된다. 실제 출력: `ex2 weighted RR (slice 3/2/1, 60 ticks): A=30 B=20 C=10 switches=29` — 6 tick 주기 `AAABBC` 가 10번. 교환은 경계 59개 중 6 tick 에 3번 → 29. FreeRTOS 에는 태스크별 slice 가 없다 (slice = 1 tick 고정). ThreadX 는 `tx_thread_create` 의 time_slice 인자로 태스크별 slice 를 준다 (문서 기준, 미검증).

</details>

### 연습 3 — 새로 깨어난 같은 우선순위 태스크의 최악 대기

CPU 를 놓지 않는 p1 태스크 n 개 + slice s 에서, 지연이 끝난 p1 태스크 W 가 처음 CPU 를 받기까지 최악 몇 tick? 깨어나는 위상을 전부 돌려 확인하라.

<details>
<summary>답 보기</summary>

W 는 줄 맨 뒤에 선다. 앞에 n−1 개가 각 s tick, 그 전에 지금 도는 태스크의 남은 slice r 이 있다. ISR 에서 slice 를 먼저 깎으므로 r ≤ s−1 (r = 0 이면 그 ISR 에서 만료 → 그 태스크는 W **뒤** 로 간다). 최악 = (s−1) + (n−1)s = **n·s − 1**, 최선 = (n−1)s. 실제 출력 (위상 1..2ns+2 전수):

```text
  n s | min max | n*s-1
  2 2 |   2   3 | 3
  3 2 |   4   5 | 5
  3 5 |  10  14 | 14
  4 5 |  15  19 | 19
```

(n,s) 9쌍 모두 일치. FreeRTOS(s = 1)라면 최악 n − 1 tick. 이 상한이 마음에 안 들면 slice 를 줄이는 게 아니라 W 의 우선순위를 올린다.

</details>

### 연습 4 — 스케줄러 잠금과 보류된 tick (`vTaskSuspendAll` / `xTaskResumeAll`)

`sched_suspend()` 동안 tick ISR 은 `g_pended++` 만 하고, `sched_resume()` 은 보류된 tick 마다 `g_tick++; kernel_tick()` 을 돌린 뒤 필요하면 전환한다. L(p1) 이 tick 2~8 동안 잠그면 H(p2, 주기 4) 는?

<details>
<summary>답 보기</summary>

```text
ex4 L locks the scheduler at tick 2, unlocks at tick 9
tick : 0123456789012345
H    : RBBBBBBBBRBBBRBB
L    : rRRRRRRRRrRRRrRR
ex4 H activations=3 at: 0 9 13, max latency 5, g_tick=15 pended=0
```

H 는 tick 4 에 깨어나야 했지만 잠금 동안 tick 처리가 미뤄져 타임라인에서도 `B` 로 남는다. tick 9 에 풀리자 보류된 7 tick 을 처리하며 H 가 깨어나 즉시 선점 (지연 5). 그래도 `g_tick == 15` — **tick 을 하나도 잃지 않았다** (E2 의 임계 구역과 대조). FreeRTOS 소스: 잠김이면 `xPendedTicks += 1U` (tasks.c:5064), `xTaskResumeAll` 이 `xPendedCounts` 만큼 `xTaskIncrementTick()` 을 반복하고 yield 가 필요하면 전환 (tasks.c:4150–4185).

</details>

### 연습 5 — 선점당한 태스크: 줄 맨 앞(POSIX 식) vs 맨 뒤(FreeRTOS 식)

`g_cfg.head_on_preempt` 를 켜면 **더 높은 태스크 때문에** 밀려난 태스크는 줄 맨 앞에 서고 남은 slice 를 유지한다 (`keep_slice`). slice 2, A2 시나리오에서 비교하라.

<details>
<summary>답 보기</summary>

```text
ex5 preempted -> TAIL (FreeRTOS style), slice 2      ex5 preempted -> HEAD (POSIX style), slice 2
B    : rrrRrrrrrRRrrrrRrrrrrRRr                       B    : rrrRrRrrrrrRrRrrrrrRrRrr
ex5 preempted task NOT resumed right after H: tail=5 head=0
```

맨 앞 방식에서 B 는 tick 3 → (H, tick 4) → tick 5 에 남은 1 tick 을 마저 쓴다. 맨 뒤 방식에서는 slice 를 남긴 채 밀려나 다음 차례를 기다린 경우가 5번. 맨 앞 방식은 "선점이 같은 우선순위 사이의 순서를 바꾸지 않는다" 는 성질을 지킨다 (POSIX XSH 2.8.4 SCHED_FIFO 규칙; SCHED_RR 의 남은 quantum 처리는 이 레슨의 선택). FreeRTOS 식은 구현이 단순하다 (리스트 하나 + 인덱스 전진).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_asan && ./pr_asan   # 22/22, 경고 0
```

관찰용 데모 (CHECK 없음, 숫자는 실행마다 조금씩 다르다):

<!-- code: demo_realtime.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_realtime.c -o demo_rt && ./demo_rt   # 약 6.5 초
```

```text
E3 cost of the host "kernel" primitives (median of 5 x 200000, ns per op)
  swapcontext (one direction)      :  632.9 ns
  crit_enter+crit_exit (2 syscalls):  346.9 ns
  raise + empty handler (tick ISR) :  745.6 ns   (hits=1000000)
```

(E1, E2 출력은 5. 풀이 Part C. 이 맥 = Apple Silicon, `-O2`, 비율만 의미 있다.)

## 10. 핵심 정리

- tick ISR 의 일 = **깨우기 → (더 급하면) 선점 → (같은 우선순위면) slice 회계**. 결과는 "전환 필요?" 한 비트 (`xTaskIncrementTick` 의 `xSwitchRequired`).
- 선점은 `>` (엄격히 높을 때만). 같은 우선순위 공유는 time slicing 의 일. FreeRTOS slice = **1 tick 고정** (설정 없음).
- 라운드 로빈 = 밀려난 태스크를 자기 줄 **맨 뒤** 에. slicing 을 꺼도 높은 태스크가 끼어들었다 나가면 다음 차례로 넘어간다 (FreeRTOS 식). POSIX 는 선점당한 스레드를 맨 앞에.
- 협력형 + 무한 루프 = 전원 정지 (B1: B·C 0 tick). 선점형 = H 50/50 지연 0.
- 교환 횟수 ∝ 1/slice, 같은 우선순위 최악 대기 = n·s − 1. 실시간 보장은 slice 가 아니라 우선순위로.
- 임계 구역 = tick 차단: **짧게**, 중첩 카운트는 **태스크별**. 길면 tick 을 잃는다 → 긴 일은 스케줄러 잠금 (pended ticks).
- 핸들러 안 swapcontext 는 POSIX 보장 밖 — 실제 Cortex-M 은 SysTick 이 PendSV 를 pend, PendSV 가 r4–r11 만 저장/복원.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` (`taskSELECT_HIGHEST_PRIORITY_TASK` 197–212, `prvAddTaskToReadyList` 287–293, `vTaskSuspendAll` 3890, `xTaskResumeAll` 4064 / 4150–4185, `xTaskGetTickCount` 4213–4229, `xTaskIncrementTick` 4831–5078 — 4944, 4967–4971, 5064), `include/list.h` (269–300, 340–370), `include/FreeRTOS.h` (190–192, 2771–2773, 2925–2942), `portable/GCC/ARM_CM4F/port.c` (65–67, 433–434, 505–545, 560–584), `portmacro.h` (88–114), `portable/ThirdParty/GCC/Posix/port.c` (40–49, 354–412, 452–512, 575–600): <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS 문서 — Customisation (`configUSE_PREEMPTION`, `configUSE_TIME_SLICING`): <https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/02-Customization>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Task Management 장 (scheduling algorithms: prioritized pre-emptive with/without time slicing, co-operative): <https://www.freertos.org/Documentation/02-Kernel/07-Books-and-manual/01-RTOS_book>
- POSIX.1-2017 XSH 2.4.3 Signal Actions (async-signal-safe 목록), 2.8.4 Process Scheduling (SCHED_FIFO / SCHED_RR): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html>
- ISO/IEC 9899:2011 7.14.1.1 The signal function (p5: 핸들러가 접근해도 되는 객체)
- macOS `man sigaction` (async-signal safe 함수 목록), `man makecontext`, `man getcontext` (`uc_link`, 시그널 핸들러 문맥)
- Zephyr — Scheduling (cooperative / preemptive, time slicing `CONFIG_TIMESLICE_SIZE`): <https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html>
- CMSIS-RTOS2 / Keil RTX5 문서 — `RTX_Config.h` 의 round-robin 설정 (`OS_ROBIN_ENABLE`, `OS_ROBIN_TIMEOUT`): <https://arm-software.github.io/CMSIS-RTX/>
- ARMv7-M Architecture Reference Manual — SysTick, PendSV, exception entry stacking (B1.5)
- 관련 레슨: RTOS-B01 (협력형 스케줄러), RTOS-B02 (TCB·상태 전이), RTOS-B03 (ucontext 문맥 교환), RTOS-B04 (우선순위 비트맵), RTOS-B06 (지연·tick wrap), RTOS-M03 (우선순위 역전), RTOS-M08 (스택 예산), RTOS-M11 (RMA), RTOS-A01 (Cortex-M PendSV), RTOS-A02 (tickless), RTOS-A06 (트레이싱), RTOS-A08 (SMP), C-M14 (ISR 공유 데이터, PRIMASK)

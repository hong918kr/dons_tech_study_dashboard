---
id: RTOS-B02
time: 65분
updated: 2026-11-11
---
# RTOS-B02 TCB 설계와 태스크 상태 전이

## 학습 목표

- TCB(Task Control Block, 태스크 제어 블록)에 들어가야 하는 필드 — 저장된 스택 포인터, 우선순위 2개(`prio`/`base_prio`), 상태, 깨어날 tick, 기다리는 객체, 리스트 노드 — 를 **왜 필요한지와 함께** 말할 수 있다.
- READY / RUNNING / BLOCKED / SUSPENDED (+ DELETED) 의 **합법 전이 12개와 불법 전이 13개**를 표로 그리고, "RUNNING 의 입구는 READY 하나뿐" 같은 규칙을 설명한다.
- 상태를 **리스트 소속**으로 정의한다 (READY ⇔ ready[prio] 에 있음 …). FreeRTOS 가 리스트 노드를 2개(`xStateListItem` + `xEventListItem`) 두는 이유 — "타임아웃이 있는 대기는 두 리스트에 동시에 있다" — 를 코드로 보인다.
- 불법 전이를 `KERNEL_ASSERT` 로 막고, 훅을 바꿔 끼워 **위반을 세는 테스트** 와 fork() 로 **진짜 abort 를 확인하는 death test** 를 둘 다 쓸 수 있다.
- 실제 FreeRTOS(POSIX 포트)에서 `eTaskGetState`·suspend/resume·`xTaskAbortDelay` 가 내 모델과 어디서 같고 어디서 다른지 확인한다.

## 1. 문제

"태스크 상태를 그려 보세요. Blocked 에서 바로 Running 으로 갈 수 있나요? TCB 에는 뭐가 들어 있죠?" — RTOS 면접의 첫 질문이다. 펌웨어에서는 디버깅 그 자체다: HardFault 덤프에서 `pxCurrentTCB` 를 따라가 스택 포인터를 찾고, 태스크가 "사라졌다" 는 버그의 대부분은 **상태와 리스트 소속이 어긋난 것**(깨어났는데 대기 리스트에 남은 유령 노드)이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b02arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 Z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">태스크 상태 기계 — 이 레슨의 합법 전이 (클수록 높은 우선순위, 단일 코어)</text>
  <rect x="300" y="36" width="140" height="36" rx="6" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="318" y="59" fill="currentColor">DELETED (끝)</text>
  <text x="452" y="52" fill="currentColor">← 모든 상태에서 delete. 나가는 길 없음</text>
  <rect x="60" y="120" width="140" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="102" y="150" fill="currentColor" font-weight="bold">READY</text>
  <rect x="500" y="120" width="140" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="532" y="150" fill="currentColor" font-weight="bold">RUNNING</text>
  <rect x="500" y="250" width="140" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="535" y="280" fill="currentColor" font-weight="bold">BLOCKED</text>
  <rect x="60" y="250" width="140" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="85" y="280" fill="currentColor" font-weight="bold">SUSPENDED</text>
  <path d="M200 132 L498 132" stroke="currentColor" stroke-width="1.6" marker-end="url(#b02arr)"/>
  <text x="270" y="125" fill="currentColor">sched_run (dispatch)</text>
  <path d="M500 160 L202 160" stroke="currentColor" stroke-width="1.6" marker-end="url(#b02arr)"/>
  <text x="280" y="152" fill="currentColor">선점 / yield (make_ready)</text>
  <path d="M560 170 L560 248" stroke="currentColor" stroke-width="1.6" marker-end="url(#b02arr)"/>
  <text x="440" y="214" fill="currentColor">block_on / delay</text>
  <path d="M500 262 L202 168" stroke="currentColor" stroke-width="1.6" marker-end="url(#b02arr)"/>
  <text x="215" y="250" fill="currentColor">이벤트 · 타임아웃 · abort</text>
  <path d="M620 250 L620 172" stroke="currentColor" stroke-width="1.4" stroke-dasharray="5 4"/>
  <text x="628" y="208" fill="currentColor">✗ 직행</text>
  <text x="628" y="224" fill="currentColor">금지</text>
  <path d="M150 170 L150 248" stroke="currentColor" stroke-width="1.6" marker-end="url(#b02arr)"/>
  <text x="158" y="214" fill="currentColor">suspend</text>
  <path d="M100 250 L100 172" stroke="currentColor" stroke-width="1.6" marker-end="url(#b02arr)"/>
  <text x="34" y="214" fill="currentColor">resume</text>
  <path d="M500 288 L202 288" stroke="currentColor" stroke-width="1.6" marker-end="url(#b02arr)"/>
  <text x="250" y="306" fill="currentColor">suspend (기다리던 것을 잃는다)</text>
  <text x="10" y="324" fill="currentColor">RUNNING→SUSPENDED (자기 suspend) 도 합법. ✗ BLOCKED/SUSPENDED→RUNNING: RUNNING 의 입구는 READY 하나뿐.</text>
</svg>
```

**핵심 생각: 상태 = 리스트 소속.** 커널은 "누가 READY 인가" 를 매번 모든 TCB 를 훑어서 찾지 않는다. 상태마다 리스트가 있고, 태스크의 노드가 **어느 리스트에 들어 있는지가 곧 상태** 다. 그래서 전이 = "노드를 한 리스트에서 빼서 다른 리스트에 넣기 + 상태 필드 갱신" 이고, 둘이 어긋나면 버그다.

| 상태 | `state_node` 위치 | `event_node` 위치 | 기타 |
|---|---|---|---|
| READY | `ready[prio]` | 없음 | |
| RUNNING | 없음 (이 모델) | 없음 | `K.current == t`, 동시에 하나 |
| BLOCKED | 유한 타임아웃이면 `delayed`, 무한이면 없음 | 객체를 기다리면 `obj->waiters` | 둘 중 하나는 반드시 있다 |
| SUSPENDED | `suspended` | 없음 | `wait_obj == NULL` |
| DELETED | 없음 | 없음 | 다시 쓰면 use-after-delete |

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="b02arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 Z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">TCB 하나, 리스트 노드 둘 — BLOCKED + 타임아웃이면 두 리스트에 동시에 있다</text>
  <rect x="20" y="32" width="250" height="248" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="52" fill="currentColor" font-weight="bold">tcb_t X (prio 2, BLOCKED)</text>
  <text x="32" y="74" fill="currentColor" font-family="monospace">sp          ← 첫 멤버 [TCB+0]</text>
  <text x="32" y="94" fill="currentColor" font-family="monospace">stack_base, name</text>
  <text x="32" y="114" fill="currentColor" font-family="monospace">prio=2  base_prio=2</text>
  <text x="32" y="134" fill="currentColor" font-family="monospace">state=BLOCKED</text>
  <text x="32" y="154" fill="currentColor" font-family="monospace">wake_tick=3  wake_reason</text>
  <text x="32" y="174" fill="currentColor" font-family="monospace">wait_obj=&amp;q</text>
  <rect x="30" y="190" width="230" height="36" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="40" y="213" fill="currentColor" font-family="monospace">state_node (value=3)</text>
  <rect x="30" y="234" width="230" height="36" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="40" y="257" fill="currentColor" font-family="monospace">event_node (value=8-2=6)</text>
  <text x="330" y="60" fill="currentColor" font-weight="bold">K.delayed — wake_tick 오름차순</text>
  <rect x="330" y="72" width="80" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="342" y="94" fill="currentColor" font-family="monospace">X : 3</text>
  <rect x="440" y="72" width="80" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="452" y="94" fill="currentColor" font-family="monospace">D : 4</text>
  <rect x="550" y="72" width="80" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="562" y="94" fill="currentColor" font-family="monospace">Y : 5</text>
  <text x="330" y="160" fill="currentColor" font-weight="bold">q.waiters — 우선순위 높은 순, 같으면 FIFO</text>
  <rect x="330" y="172" width="80" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="342" y="194" fill="currentColor" font-family="monospace">Y p3</text>
  <rect x="440" y="172" width="80" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="452" y="194" fill="currentColor" font-family="monospace">X p2</text>
  <path d="M260 206 L328 98" stroke="currentColor" stroke-width="1.5" marker-end="url(#b02arr2)"/>
  <path d="M260 252 L438 198" stroke="currentColor" stroke-width="1.5" marker-end="url(#b02arr2)"/>
  <text x="290" y="236" fill="currentColor">타임아웃 때 state_node 만 빼면 event_node 가 q 에 남는다 (유령)</text>
  <text x="290" y="256" fill="currentColor">q 에 give → 이미 READY 인 X 를 또 깨움 → ready 리스트 이중 삽입</text>
  <text x="290" y="276" fill="currentColor">그래서 깨어날 땐 이유와 상관없이 detach() 로 둘 다 뺀다</text>
</svg>
```

**모델 규칙 (하네스 제공).** 컨텍스트 스위치는 없다 — 그건 RTOS-B03(ucontext)이 한다. 이 레슨은 스레드도 타이머도 없는 결정적 장부 시뮬레이터다.

| 규칙 | 이 모델 | 실제 RTOS 와 다른 점 |
|---|---|---|
| 리스트 | 침투형(intrusive) 원형 이중 연결 리스트, 센티넬 `end`, 노드에 `container`·`owner` 역참조. 정렬 삽입은 `value` 오름차순, 같은 값은 뒤로 | FreeRTOS `ListItem_t` 와 같은 모양 (list.h:144-153), `<=` 규칙도 같음 (list.c:192). tick wrap 은 RTOS-B06 |
| RUNNING | `K.current`, **ready 리스트에서 뺀다** | FreeRTOS 는 RUNNING 도 ready 리스트에 **남겨 둔다** (5. 풀이) |
| 우선순위 선택 | `sched_pick_next()` = 7→0 선형 탐색 | O(1) 비트맵은 RTOS-B04 |
| `KERNEL_ASSERT` | 실패 → 훅 호출. 기본 훅은 `abort()`, 테스트는 카운터 훅. 훅이 돌아오면 함수는 **아무것도 안 바꾸고 false** | FreeRTOS `configASSERT` 는 기본이 빈 매크로 (FreeRTOS.h:97-101) |
| Part C 의 tick | ① `kernel_tick()` (타임아웃) ② DRDY ISR ③ 선점 판단 + dispatch ④ RUNNING 의 `step()` 한 번 | 실제 선점은 ISR 출구에서 즉시 (PendSV, RTOS-B05) |

### Part A — TCB 와 전이 함수

`tcb_t` 와 리스트, 커널 `K`(ready[8], delayed, suspended, current, tick) 는 주어진다. 채울 것:

- **A1** `transition_ok(from, to)` — 5×5 합법 전이 표.
- **A2** `detach(t)` — `state_node`·`event_node` 를 **둘 다** 빼고 `wait_obj`, `K.current` 정리.
- **A3** `task_make_ready(t)` — X→READY 공통 (선점·yield·깨어남·resume). ready[prio] **끝** 에 넣는다.
- **A4** `sched_pick_next()` / `sched_run(t)` — 최고 우선순위 READY 의 맨 앞, READY→RUNNING (current 가 비어 있어야).
- **A5** `task_block_on(t, obj, ticks)` / `task_delay(t, ticks)` — RUNNING→BLOCKED. obj 가 있으면 `event_node` 를 우선순위 순으로, 유한 타임아웃이면 `state_node` 를 `delayed` 에 wake_tick 순으로.
- **A6** `task_unblock(t, why)` / `obj_signal(o)` / `kernel_tick()` — BLOCKED→READY 는 이벤트든 타임아웃이든 두 리스트 모두에서 빠진다.
- **A7** `task_suspend` / `task_resume` / `task_delete`.

모든 전이 함수는 **먼저 검사, 그다음 변경**: `if (!TRANSITION_CHECK(t, TO)) return false;` 가 첫 줄이다.

### Part B — 불법 전이는 KERNEL_ASSERT 로 거부

1. **B1 5×5 전수**: 각 from 상태의 태스크를 합법 경로로 만들고 각 to 로 보내 본다. 합법 12개는 성공 + 위반 0, 불법 13개는 `false` + 위반 정확히 1 + **상태·리스트·current 가 그대로**.
2. **B2 대표 사례와 메시지**: BLOCKED→RUNNING, SUSPENDED→RUNNING, RUNNING→RUNNING, 다른 태스크가 RUNNING 인데 dispatch, READY 를 또 READY 로(이중 삽입), SUSPENDED 아닌 태스크 resume.
3. **B3 검사기 자체 시험**: 버그 있는 unblock 을 손으로 흉내 내(state_node 만 빼고 READY) 불변식 검사기가 유령 노드를 잡는지.
4. **B4 death test** (`demo_death.c`): 기본 훅(abort)으로 불법 전이 → 자식 프로세스가 SIGABRT 로 죽는지 `fork()` + `waitpid()` 로 확인.

### Part C — BLE 센서 노드 (nRF52 류 MCU 를 흉내 낸 30 tick)

| 태스크 | 우선순위 | 하는 일 | 쓰는 전이 |
|---|---|---|---|
| PWR | 4 | tick 8 에 배터리 낮음 → LOG suspend, tick 16 에 resume 후 **스스로 delete** | delay, suspend(남), resume(남), delete(자기) |
| IMU | 3 | DRDY 인터럽트를 타임아웃 3 tick 으로 기다림. 샘플 1 tick. 타임아웃이면 I2C 복구 2 tick | block_on, 이벤트/타임아웃 깨어남 |
| BLE | 2 | 연결 이벤트 처리 2 tick → 6 tick delay | delay, 선점당함 |
| LOG | 1 | 블록하지 않는 백그라운드 로그 | 선점당함, suspend 당함 |

DRDY 는 tick 2, 4, …, 12 와 20, 22, …, 28 에 온다 — **13–19 는 센서가 멈춘 구간**(I2C 버스 hang 흉내). 매 tick 불변식 검사, 타임라인과 전이 횟수 행렬 출력. 검사: 위반 0, 불법 전이 사용 0, LOG 는 suspend 구간(tick 8–15)에 한 번도 못 돈다, DRDY 는 샘플이 되거나 "놓침" 으로 세어진다, 타임아웃은 센서가 멈춘 구간에서만, 상태별 흐름 보존.

## 2. Problem (English)

> **Design a task control block and its state machine for a small single-core RTOS.**
>
> ```c
> typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED, TASK_DELETED } task_state_t;
> bool    transition_ok(task_state_t from, task_state_t to);
> bool    task_make_ready(tcb_t *t);                          // any legal X -> READY (preempt, yield, wake, resume)
> tcb_t  *sched_pick_next(void);                              // highest-priority READY task, FIFO within a priority
> bool    sched_run(tcb_t *t);                                // READY -> RUNNING; only one RUNNING at a time
> bool    task_block_on(tcb_t *t, kobj_t *obj, uint32_t ticks); // RUNNING -> BLOCKED on obj and/or timeout
> bool    task_unblock(tcb_t *t, wake_t why);                 // BLOCKED -> READY (EVENT / TIMEOUT / ABORT)
> tcb_t  *obj_signal(kobj_t *o);  void kernel_tick(void);
> bool    task_suspend(tcb_t *t); bool task_resume(tcb_t *t); bool task_delete(tcb_t *t);
> ```
>
> A task waiting on an object with a timeout must sit in the object's wait list (priority order) **and** in the delayed list (wake-tick order) at the same time; whichever wakes it first must remove it from both. Illegal transitions (e.g. BLOCKED→RUNNING, SUSPENDED→RUNNING, RUNNING→RUNNING, anything out of DELETED) must trip `KERNEL_ASSERT`. Make the assert hook replaceable so a unit test can count violations instead of aborting, and leave the kernel unchanged when an assert returns.
>
> Example: X (prio 2) blocks on `q` with timeout 3 at tick 0. After 3 ticks X is READY with reason TIMEOUT, `q` has no waiters, and a later `obj_signal(&q)` returns NULL.
>
> Follow-ups: What must be the first member of the TCB and why? Why does FreeRTOS keep two list items per task? Is the running task in a ready list? What happens to a blocked task that gets suspended and then resumed? How do you test that an assert really halts?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부 — 멈추지 않고 바로 끝난다):

```text
FAIL starter.c:403  mism == 0 && nlegal == 12
...
  READY      BUG       BUG       BUG       BUG       BUG
Part C BLE sensor node: 30 ticks, IMU samples=0 timeouts=0 (first -1, last -1), DRDY pulses=11 missed=11, BLE=0, LOG ticks=0
PWR  : r r r r r r r r r r r r r r r r r r r r r r r r r r r r r r
5/40 passed
```

하네스 설계:

- **무한 루프가 없다.** Part C 는 30 tick 고정, `kernel_tick` 의 깨우기 루프는 unblock 이 실패하면 `break`. 스텁 `sched_pick_next` 는 NULL 이라 모두 `r` 로 끝난다.
- **테스트 중 assert 는 멈추지 않는다.** `main` 첫 줄이 `g_kassert_hook = kassert_count`. 그래서 스텁 상태에서도 FAIL 로 끝난다 (abort 로 죽지 않음).
- **불변식 검사기 `k_check()` 는 학생 코드와 독립.** 태스크 표 전체를 돌며 "상태 ↔ 리스트 소속" 표(위)와 리스트 모양(링크, count, 정렬)을 본다. 첫 위반 메시지를 `g_inv_msg` 에 남긴다.

## 4. 힌트

<details>
<summary>힌트 1 — 표부터: RUNNING 열과 DELETED 행</summary>

RUNNING 으로 들어가는 칸은 READY 행 하나만 1 이다 (BLOCKED·SUSPENDED 는 READY 를 거쳐야 하고, RUNNING→RUNNING 은 이중 dispatch). DELETED 행은 전부 0. 대각선(자기 자신으로)도 전부 0 — "READY 를 또 READY 로" 는 ready 리스트 이중 삽입이다. BLOCKED 열은 RUNNING 행만 1: 블록은 **자기 자신만** 할 수 있다 (READY 태스크는 CPU 가 없으니 블로킹 API 를 부를 수 없다). 합계 12 가 나와야 한다.

</details>

<details>
<summary>힌트 2 — detach 한 함수가 버그 절반을 막는다</summary>

`list_remove()` 는 노드가 어느 리스트에도 없으면 아무 일도 안 한다. 그러니 "지금 어디 있는지" 따지지 말고 두 노드를 **무조건** 뺀다. RUNNING 태스크는 리스트에 없으니 `K.current == t` 이면 NULL 로. 이러면 make_ready / suspend / delete / block_on 이 모두 `detach` 한 줄로 시작할 수 있다.

</details>

<details>
<summary>힌트 3 — 정렬 키 두 개</summary>

`state_node.value = wake_tick` (작을수록 먼저 깸), `event_node.value = NPRIO - prio` (우선순위 높을수록 작은 값 → 앞). 둘 다 `list_insert_sorted` 하나로 된다 — FreeRTOS 가 `xItemValue = configMAX_PRIORITIES - uxPriority` 로 하는 것과 같다 (tasks.c:5416-5419). 무한 대기(`WAIT_FOREVER`)는 delayed 에 넣지 않는다: tick 이 영원히 건드리지 않게.

</details>

<details>
<summary>힌트 4 — 검사 먼저, 변경 나중</summary>

훅이 돌아오는 테스트 모드에서는 assert 뒤 코드가 계속 실행된다. 리스트를 이미 건드린 뒤 assert 하면 "거부했는데 상태는 반쯤 바뀐" 커널이 된다. 그래서 `TRANSITION_CHECK` 와 인자 검사를 **맨 위** 에 두고, 실패하면 바로 `return false`. B1 이 거부 후 `state`, 두 노드의 `container`, `K.current` 를 전후 비교한다.

</details>

<details>
<summary>힌트 5 — kernel_tick 은 "맨 앞만" 본다</summary>

delayed 는 wake_tick 순이므로 맨 앞이 아직이면 뒤는 볼 필요가 없다: `while (head && head->wake_tick <= K.tick) task_unblock(head, WAKE_TIMEOUT);`. 같은 tick 에 깨는 태스크가 여럿이면 while 이 다 처리한다. unblock 이 실패하면 같은 노드에서 영원히 돌지 않게 `break`.

</details>

## 5. 풀이

### Part A — 전이 표 12 + 13

| from \ to | READY | RUNNING | BLOCKED | SUSPENDED | DELETED |
|---|---|---|---|---|---|
| **READY** | ✗ 이중 삽입 | ✓ dispatch | ✗ CPU 가 없다 | ✓ 남이 suspend | ✓ 남이 delete |
| **RUNNING** | ✓ 선점·yield | ✗ 이중 dispatch | ✓ block_on·delay | ✓ 자기 suspend | ✓ 자기 delete |
| **BLOCKED** | ✓ 이벤트·타임아웃·abort | ✗ **직행 금지** | ✗ 이미 대기 중 | ✓ 대기를 잃음 | ✓ |
| **SUSPENDED** | ✓ resume | ✗ resume 없이 실행 | ✗ 실행 안 하고 대기? | ✗ 중복 suspend | ✓ |
| **DELETED** | ✗ | ✗ | ✗ | ✗ | ✗ use-after-delete |

불법 13개의 이유는 세 가지로 묶인다.

1. **RUNNING 의 입구는 READY 하나** (BLOCKED/SUSPENDED/RUNNING→RUNNING). 스케줄러만 RUNNING 을 만들고, 스케줄러는 ready 리스트에서만 고른다. BLOCKED 에서 바로 RUNNING 이 되면 "깨어남" 단계를 건너뛰어 노드가 delayed/대기 리스트에 남는다 — 바로 유령 노드.
2. **블록은 자기 자신만** (READY/BLOCKED/SUSPENDED→BLOCKED). 블로킹 API 는 "지금 도는 태스크가 자기를 재운다". READY 태스크는 그 코드를 실행 중이 아니다.
3. **자기 자신으로 가는 전이와 DELETED 출발은 모두 버그** — 리스트 이중 삽입, 해제된 TCB 재사용.

**SUSPENDED→SUSPENDED 를 불법으로 둔 것은 이 모델의 선택** 이다 (엄격한 커널). FreeRTOS 는 같은 태스크에 `vTaskSuspend` 를 두 번 해도 리스트에서 뺐다 다시 넣을 뿐 assert 하지 않고, SUSPENDED 가 아닌 태스크에 `vTaskResume` 하면 조용히 아무것도 안 한다 (`prvTaskIsTaskSuspended` 가 거름, tasks.c:3456) — 데모에서 확인. 사용자 API 는 관대하게, 커널 내부 전이는 엄격하게 — 둘 다 말할 수 있으면 된다.

**TCB 필드 — 각각 왜 있나, FreeRTOS 에선 무엇인가** (V11.3.2 `tskTCB`, tasks.c:377-455):

| 이 모델 | 왜 필요한가 | FreeRTOS `tskTCB` |
|---|---|---|
| `sp` (첫 멤버) | 컨텍스트 스위치가 레지스터를 태스크 스택에 밀고 그 꼭대기를 여기 저장. 포트 asm 이 `[TCB+0]` 으로 읽는다 | `pxTopOfStack` — "THIS MUST BE THE FIRST MEMBER" (tasks.c:379). CM4F PendSV: `str r0, [r2]` "Save the new top of stack into the first member of the TCB" (portable/GCC/ARM_CM4F/port.c:521, 533-534) |
| `stack_base` | high-water mark, 오버플로 검사 (RTOS-M09) | `pxStack` (392) |
| `prio` / `base_prio` | 상속 중 유효 우선순위와 돌아갈 원래 값 (RTOS-M04) | `uxPriority` (391), `uxBasePriority` + `uxMutexesHeld` (417-418, configUSE_MUTEXES) |
| `state` | 빠른 판정·디버깅 | **없다.** `eTaskGetState()` 가 리스트 소속으로 계산 (2556-2660) |
| `wake_tick` | 타임아웃 시각 | 별도 필드 없음 — `xStateListItem` 의 `xItemValue` 에 저장 (8729-8732) |
| `wait_obj` | 무엇을 기다리나 (디버거, 상속 대상 찾기) | 없음 — `xEventListItem.pxContainer` 가 그 객체의 리스트 |
| `wake_reason` | 블로킹 API 의 반환값 (EVENT/TIMEOUT/ABORT) | `ucDelayAborted` (448-450) + API 의 재검사 루프 (queue.c:1686) |
| `state_node` / `event_node` | 아래 | `xStateListItem` / `xEventListItem` (389-390) |
| `name` | 디버거·트레이스 | `pcTaskName[configMAX_TASK_NAME_LEN]` (397) |

**리스트 노드가 왜 둘인가.** `xStateListItem` 의 주석은 "The list that the state list item of a task is reference from denotes the state of that task (Ready, Blocked, Suspended)", `xEventListItem` 은 "Used to reference a task from an event list" (tasks.c:389-390). 노드 하나는 리스트 하나에만 들어갈 수 있는데, `xSemaphoreTake(s, 10)` 하는 태스크는 **세마포어 대기 리스트(누가 먼저 받나, 우선순위 순)와 지연 리스트(언제 포기하나, 시각 순)에 동시에** 있어야 한다. 정렬 기준도 다르다. 그래서 둘. 깨울 때는 어느 쪽 사건이든 둘 다 뺀다 — FreeRTOS 의 tick 처리도 delayed 에서 꺼낸 태스크의 `xEventListItem` 이 어딘가 있으면 함께 뺀다 (tasks.c:4913-4919), 이벤트 쪽 `xTaskRemoveFromEventList` 도 event 를 뺀 뒤 state 를 빼서 ready 로 (5525-5530).

핵심은 `detach` 한 함수 — make_ready·suspend·delete·block_on 이 모두 이것으로 시작한다 (나머지는 6. 정답 코드):

```c
static void detach(tcb_t *t) {                 /* 어디 있든 둘 다 뺀다 (리스트에 없으면 list_remove 는 no-op) */
    list_remove(&t->state_node);
    list_remove(&t->event_node);
    t->wait_obj = NULL;
    if (K.current == t) K.current = NULL;
}
```

Part A 테스트가 확인한 것 (전부 PASS):

| 시나리오 | 기대 |
|---|---|
| A(1) B(2) C(3) 생성 → pick → run(C) | C RUNNING, `K.current == &C`, C 는 ready 리스트 밖, 다음 pick 은 B |
| X 가 q 를 타임아웃 3 으로 기다림 → tick ×3 | tick 2 까지 BLOCKED, tick 3 에 READY(TIMEOUT), **q 대기자 0**, 이후 `obj_signal(&q) == NULL` (두 번 깨지 않음) |
| Y 가 q 를 타임아웃 5 로 기다림 → signal → tick ×10 | READY(EVENT), **delayed 0**, BLOCKED→READY 전이는 딱 1번 |
| L(1) H1(3) H2(3) 이 무한 대기 | delayed 비어 있음, 깨는 순서 H1 → H2 → L (우선순위 → FIFO) |
| Z 가 q 대기 중 suspend → tick ×10 → resume | SUSPENDED 동안 q·delayed 어디에도 없음, `wake_reason = ABORT`, resume 후 READY |

**FreeRTOS 의 장부는 이 모델과 두 곳이 다르다** (소스 확인):

- **RUNNING 태스크도 ready 리스트에 있다.** `prvAddCurrentTaskToDelayedList` 주석: "Remove the task from the ready list before adding it to the blocked list as the same list item is used for both lists" (tasks.c:8702-8704). 스케줄러는 `listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) )` 로 같은 우선순위 안에서 다음 것을 고른다 (tasks.c:210) — 리스트 인덱스를 돌리는 것만으로 라운드 로빈이 된다. 그래서 FreeRTOS 의 선점은 리스트를 건드리지 않는다. 이 모델은 RUNNING 을 빼서 "상태 ↔ 리스트" 를 1:1 로 만들었다 (가르치기 쉬움, 선점 때 맨 뒤로 감).
- **무한 대기는 suspended 리스트에 들어간다.** `xTicksToWait == portMAX_DELAY` 이고 `xCanBlockIndefinitely` 이면 `listINSERT_END( &xSuspendedTaskList, ... )` (8717-8722) — tick 이 절대 깨우지 않게. 그래서 `eTaskGetState` 는 suspended 리스트에 있어도 `xEventListItem` 이 어떤 리스트에 있거나 알림을 기다리면 `eBlocked` 로 돌려준다 (2602-2638). `vTaskDelay(portMAX_DELAY)` 는 `pdFALSE` 를 넘기므로 (2528) 이 경로가 아니다.

### Part B — 불법 전이 거부 (실제 출력)

```text
Part B1 transition matrix (ok = done, ASSERT = rejected, BUG = wrong)
  from \ to  READY     RUNNING   BLOCKED   SUSPENDED DELETED
  READY      ASSERT    ok        ASSERT    ok        ok
  RUNNING    ok        ASSERT    ok        ok        ok
  BLOCKED    ok        ASSERT    ASSERT    ok        ok
  SUSPENDED  ok        ASSERT    ASSERT    ASSERT    ok
  DELETED    ASSERT    ASSERT    ASSERT    ASSERT    ASSERT
  legal ok 12/12, illegal rejected 13/13
```

**assert 를 테스트 가능하게 만드는 세 가지 장치.**

```c
typedef void (*kassert_hook_t)(const char *msg, const char *file, int line);
static kassert_hook_t g_kassert_hook = kassert_abort;          /* 제품: 멈춘다 */
#define KERNEL_ASSERT(cond, ...) ((cond) ? true : kassert_failf(__FILE__, __LINE__, __VA_ARGS__))
/* 사용: if (!KERNEL_ASSERT(legal, "illegal %s -> %s (%s)", ...)) return false; */
```

1. **훅이 함수 포인터** — `configASSERT` 가 사용자가 정의하는 매크로인 것과 같은 생각 (FreeRTOS.h:97-101 은 정의가 없으면 빈 매크로). 제품은 `abort()`/브레이크포인트/워치독 리셋, 테스트는 카운터.
2. **식(expression)으로 값을 돌려준다** — `KERNEL_ASSERT` 가 bool 이므로 `if (!KERNEL_ASSERT(...)) return false;` 한 줄로 "검사 실패 시 아무것도 안 바꾸고 나감" 을 쓴다. 제품에서는 훅이 안 돌아오니 `return` 은 죽은 코드.
3. **검사 먼저, 변경 나중** — 거부된 전이 뒤 `state`, `state_node.container`, `event_node.container`, `K.current` 가 전과 같은지 B1 이 13칸 모두 확인했다.

B2 의 메시지 (훅이 남긴 `g_kassert_msg`): `illegal BLOCKED -> RUNNING (T)`, `illegal SUSPENDED -> RUNNING (T)`, `illegal RUNNING -> RUNNING (T)`, `T still RUNNING, cannot dispatch U` (표로는 READY→RUNNING 이 합법이지만 **단일 코어** 조건 위반), READY 를 또 make_ready → 거부되고 `ready[3].count == 1` 유지, `resume U: not SUSPENDED`.

B3 — 검사기 자체를 시험: BLOCKED 태스크의 `state_node` 만 빼서 READY 로 옮기자 `k_check()` 가 `T: state READY but state_node in ready, event_node in obj` 를 잡았다. **테스트를 테스트** 하지 않으면 검사기가 항상 0 을 돌려줘도 모른다.

**B4 death test** (`demo_death.c`, 실제 출력 — `[child]` 줄은 자식의 stderr):

```text
SIGABRT = 6 -> expected status for illegal = 134
  [child] KERNEL_ASSERT: illegal BLOCKED -> RUNNING (demo_death.c:34)
BLOCKED   -> RUNNING   : child status 134  (killed by signal, expect SIGABRT) PASS
  [child] KERNEL_ASSERT: illegal SUSPENDED -> RUNNING (demo_death.c:34)
SUSPENDED -> RUNNING   : child status 134  (killed by signal, expect SIGABRT) PASS
  [child] KERNEL_ASSERT: illegal RUNNING -> RUNNING (demo_death.c:34)
RUNNING   -> RUNNING   : child status 134  (killed by signal, expect SIGABRT) PASS
  [child] KERNEL_ASSERT: illegal DELETED -> READY (demo_death.c:34)
DELETED   -> READY     : child status 134  (killed by signal, expect SIGABRT) PASS
READY     -> RUNNING   : child status   0  (exited, expect exit 0) PASS
BLOCKED   -> READY     : child status   0  (exited, expect exit 0) PASS
6/6 passed
```

<!-- code: demo_death.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_death.c -o demo_death && ./demo_death
```

`fork()` 전에 `fflush(NULL)` (안 하면 부모 stdout 버퍼가 자식에서 한 번 더 나온다), 자식은 `_exit()`, 부모는 `WIFSIGNALED`/`WTERMSIG` 로 판정. 합법 전이 2개를 **대조군** 으로 넣어 "아무 이유로나 죽는" 거짓 PASS 를 막았다. 별도 파일로 뺀 이유: 처음엔 solution.c 안에 넣었는데, macOS 가 abort 로 죽은 자식마다 `~/Library/Logs/DiagnosticReports/` 에 `.ips` 크래시 리포트를 남겼다 (`RTOS-B02_so-2026-11-11-090305.ips` 생성 확인). 검증 때마다 돌리는 파일에서는 뺐다. 보드에서는 "죽는다" 를 HIL 테스트가 리셋 원인 레지스터(STM32 `RCC_CSR` 의 IWDG/SFT 리셋 플래그 등)나 브레이크포인트로 확인한다.

### Part C — BLE 센서 노드 (실제 출력)

```text
Part C BLE sensor node: 30 ticks, IMU samples=10 timeouts=2 (first 15, last 20), DRDY pulses=11 missed=1, BLE=3, LOG ticks=4
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
PWR  : R B B B B B B B R B B B B B B B R . . . . . . . . . . . . .
IMU  : r R R B R B R B r R R B R B B R r R B B R R R B R B R B R B
BLE  : r r r R r R B B B B B R r R B B B B B R r r r R B B B B B R
LOG  : r r r r r r r R S S S S S S S S r r R r r r r r r R r R r r
DRDY :     ^   ^   ^   ^   ^   ^               ^   ^   ^   ^   ^
transitions        READY   RUNNING   BLOCKED SUSPENDED   DELETED
READY                  0        28         0         1         0
RUNNING                8         0        18         0         1
BLOCKED               17         0         0         0         0
SUSPENDED              1         0         0         0         0
DELETED                0         0         0         0         0
```

읽는 법 (tick 번호는 맨 위 줄):

- **tick 4**: BLE 가 RUNNING(tick 3 부터 일하던 중)인데 DRDY 로 IMU(p3) 가 깨어남 → BLE RUNNING→READY (선점) → IMU dispatch. BLE 는 tick 5 에 남은 1 tick 을 한다.
- **tick 8**: PWR 의 delay 가 끝남 + DRDY. PWR(p4) 가 LOG 를 선점하고, **READY 상태의 LOG 를 suspend** (READY→SUSPENDED 1회). IMU 는 깨어났지만 PWR 때문에 `r`, tick 9 에 샘플.
- **tick 14**: 모두 B/S — idle tick. 센서가 멈췄고(13–19) BLE 는 delay 중.
- **tick 15**: IMU 의 타임아웃(12 에 블록, 3 tick) → `WAKE_TIMEOUT` → I2C 복구 2 tick. 하지만 **tick 16** 에 PWR 이 깨어나 IMU 를 선점, LOG resume(SUSPENDED→READY) 후 **스스로 delete** (RUNNING→DELETED). 그 뒤 PWR 줄은 `.`.
- **tick 20 — 경주(race)**: IMU 의 두 번째 타임아웃 시각(17 + 3)과 DRDY 가 같은 tick. 이 모델은 ① tick(타임아웃) → ② ISR 순서라 **타임아웃이 이기고** DRDY 는 대기자가 없어 `missed=1`. 실제 MCU 에서도 SysTick 과 DRDY EXTI 중 누가 먼저냐로 갈린다. 대기 객체가 **세마포어** 였다면 give 가 count 에 남아 다음 take 가 바로 성공했을 것이다 (RTOS-B08/B09) — 맨 대기 리스트(조건 변수처럼 술어가 없는 것)는 신호를 잃는다.
- 전이 행렬: READY→RUNNING 28 = 바쁜 29 tick − IMU 가 tick 20→21 연속 실행 1. RUNNING→READY 8 = 선점 8번 (tick 4, 8, 12, 16, 19, 20, 26, 28). RUNNING→BLOCKED 18 = IMU 13 + BLE 3 + PWR 2, BLOCKED→READY 17 = 18 − 마지막에 블록된 IMU 1. **불법 칸은 전부 0**.
- 흐름 보존 검사: 상태마다 `시작 개수 + 들어온 수 − 나간 수 == 지금 개수` (READY: 4 + 26 − 29 = 1 = LOG. 끝에는 BLE RUNNING, IMU BLOCKED, LOG READY, PWR DELETED).

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part C 실제 타임라인 — BLE 센서 노드 (solution.c 출력 그대로)</text>
  <text x="63" y="36" fill="currentColor">0</text> <text x="85" y="36" fill="currentColor">1</text> <text x="107" y="36" fill="currentColor">2</text> <text x="129" y="36" fill="currentColor">3</text> <text x="151" y="36" fill="currentColor">4</text> <text x="173" y="36" fill="currentColor">5</text> <text x="195" y="36" fill="currentColor">6</text> <text x="217" y="36" fill="currentColor">7</text> <text x="239" y="36" fill="currentColor">8</text> <text x="261" y="36" fill="currentColor">9</text> <text x="283" y="36" fill="currentColor">0</text> <text x="305" y="36" fill="currentColor">1</text> <text x="327" y="36" fill="currentColor">2</text> <text x="349" y="36" fill="currentColor">3</text> <text x="371" y="36" fill="currentColor">4</text> <text x="393" y="36" fill="currentColor">5</text> <text x="415" y="36" fill="currentColor">6</text> <text x="437" y="36" fill="currentColor">7</text> <text x="459" y="36" fill="currentColor">8</text> <text x="481" y="36" fill="currentColor">9</text> <text x="503" y="36" fill="currentColor">0</text> <text x="525" y="36" fill="currentColor">1</text> <text x="547" y="36" fill="currentColor">2</text> <text x="569" y="36" fill="currentColor">3</text> <text x="591" y="36" fill="currentColor">4</text> <text x="613" y="36" fill="currentColor">5</text> <text x="635" y="36" fill="currentColor">6</text> <text x="657" y="36" fill="currentColor">7</text> <text x="679" y="36" fill="currentColor">8</text> <text x="701" y="36" fill="currentColor">9</text>
  <text x="10" y="59" fill="currentColor" font-family="monospace">PWR</text> <rect x="56" y="44" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="78" y="44" width="154" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="85" y="59" fill="currentColor">B</text> <rect x="232" y="44" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="254" y="44" width="154" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="261" y="59" fill="currentColor">B</text> <rect x="408" y="44" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="83" fill="currentColor" font-family="monospace">IMU</text> <rect x="56" y="68" width="22" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="78" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="100" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="122" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="129" y="83" fill="currentColor">B</text> <rect x="144" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="166" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="173" y="83" fill="currentColor">B</text> <rect x="188" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="210" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="217" y="83" fill="currentColor">B</text> <rect x="232" y="68" width="22" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="254" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="276" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="298" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="305" y="83" fill="currentColor">B</text> <rect x="320" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="342" y="68" width="44" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="349" y="83" fill="currentColor">B</text> <rect x="386" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="408" y="68" width="22" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="430" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="452" y="68" width="44" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="459" y="83" fill="currentColor">B</text> <rect x="496" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="518" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="540" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="562" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="569" y="83" fill="currentColor">B</text> <rect x="584" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="606" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="613" y="83" fill="currentColor">B</text> <rect x="628" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="650" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="657" y="83" fill="currentColor">B</text> <rect x="672" y="68" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="694" y="68" width="22" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="701" y="83" fill="currentColor">B</text>
  <text x="10" y="107" fill="currentColor" font-family="monospace">BLE</text> <rect x="56" y="92" width="66" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="122" y="92" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="144" y="92" width="22" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="166" y="92" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="188" y="92" width="110" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="195" y="107" fill="currentColor">B</text> <rect x="298" y="92" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="92" width="22" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="342" y="92" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="364" y="92" width="110" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="371" y="107" fill="currentColor">B</text> <rect x="474" y="92" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="496" y="92" width="66" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="562" y="92" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="584" y="92" width="110" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="591" y="107" fill="currentColor">B</text> <rect x="694" y="92" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="131" fill="currentColor" font-family="monospace">LOG</text> <rect x="56" y="116" width="154" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="210" y="116" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="232" y="116" width="176" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="239" y="131" fill="currentColor">S</text> <rect x="408" y="116" width="44" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="452" y="116" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="474" y="116" width="132" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="606" y="116" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="628" y="116" width="22" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="650" y="116" width="22" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="672" y="116" width="44" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="156" fill="currentColor" font-family="monospace">DRDY</text> <path d="M111 156 L106 146 L116 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M155 156 L150 146 L160 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M199 156 L194 146 L204 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M243 156 L238 146 L248 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M287 156 L282 146 L292 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M331 156 L326 146 L336 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M507 156 L502 146 L512 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M551 156 L546 146 L556 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M595 156 L590 146 L600 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M639 156 L634 146 L644 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <path d="M683 156 L678 146 L688 146 Z" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="186" fill="currentColor">tick 8: PWR 가 LOG suspend (READY→SUSPENDED) · tick 16: LOG resume, PWR 스스로 delete</text>
  <text x="10" y="202" fill="currentColor">tick 15·20: IMU DRDY 타임아웃 → I2C 복구 2 tick · tick 20: 같은 tick 의 DRDY 는 대기자 없음 (missed)</text>
  <rect x="10" y="207" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="218" fill="currentColor">R 실행</text> <rect x="90" y="207" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="110" y="218" fill="currentColor">r READY</text> <rect x="180" y="207" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="200" y="218" fill="currentColor">B BLOCKED</text> <rect x="285" y="207" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="305" y="218" fill="currentColor">S SUSPENDED</text> <text x="400" y="218" fill="currentColor">빈칸 = DELETED · ▼ DRDY 인터럽트</text>
</svg>
```

### 실제 FreeRTOS 로 확인 — `demo_freertos_states.c`

같은 질문을 진짜 커널(V11.3.2, POSIX 포트)에 던졌다. ctrl(p6) 이 지휘하고 busy(p1, 무한 루프), wto(p3, `xSemaphoreTake(semA, 50)`), wfor(p3, `portMAX_DELAY`), dly(p2, `vTaskDelay(1000)`), sus(p2, 자기 suspend) 가 각자 대기 지점에 선다. 실제 출력 (5번 실행, md5 모두 같음. Q1 의 PASS 3줄은 생략):

```text
Q1 states at t=6
  ctrl=eRunning busy=eReady wto=eBlocked wfor=eBlocked dly=eBlocked sus=eSuspended
Q2 vTaskResume(blocked wfor)
  still eBlocked, take not returned (no-op)                      PASS
Q3 suspend blocked wfor, give semB, resume
  after suspend: wfor=eSuspended
  give=1, semB count=1, rWfor=99
  suspended task is not woken by give (count stays 1)            PASS
  after resume: rWfor=1 at t=10, semB count=0, wfor=eBlocked
  resumed task re-checks and takes the token (pdTRUE)            PASS
Q4 suspend + immediate resume of wto (50-tick timeout, started at t~0)
  t=14 wto=eBlocked rWto=99
  re-blocked (eBlocked), take not returned yet                   PASS
  take returned 0 at t=51
  times out with pdFALSE at the ORIGINAL deadline (t<=52)        PASS
Q5 xTaskAbortDelay / double suspend
  abort(dly)=1 -> dly returned at t=74 (asked 1000); abort(sus)=0 abort(busy)=0
  abort delayed -> pdPASS, early return                          PASS
  abort suspended / ready -> pdFAIL                              PASS
  suspend twice: no assert, still eSuspended                     PASS
ALL PASS (0 fail)
```

| 질문 | 이 모델 | FreeRTOS V11.3.2 (데모 + 소스) |
|---|---|---|
| 무한 대기 태스크의 상태 | BLOCKED (event_node 만 있음) | `eBlocked` — suspended 리스트에 있지만 event 리스트 검사로 구분 (2602-2638) |
| BLOCKED 에 resume | KERNEL_ASSERT (엄격) | **조용히 무시** (3456) |
| suspend 두 번 | KERNEL_ASSERT (엄격) | assert 없음, `eSuspended` 유지 |
| BLOCKED 를 suspend | 두 리스트에서 빠짐, `wake_reason = ABORT` | 같다: state·event 둘 다 뺌 (3214, 3223-3226). 그 사이 give 는 대기자 없음 → count 1 로 남음 |
| 그다음 resume | READY, 대기는 취소된 채 | READY → 블로킹 API 의 `for( ; ; )` 재시도 루프 (queue.c:1686) 가 **다시 확인**: 토큰이 있으면 가져가고(Q3, pdTRUE — 그 뒤 `eBlocked` 는 자기 루프의 `vTaskDelay`), 없으면 남은 시간으로 다시 블록(Q4) |
| 타임아웃 마감 | — | suspend/resume 해도 **처음 진입 시각 기준** (Q4: t=51 에 pdFALSE). `xTaskCheckForTimeOut` 이 `xTimeOnEntering` 으로 경과 시간 계산 (tasks.c:5707) |
| abort delay | 연습 2 | BLOCKED 만 pdPASS, 아니면 pdFAIL (4750) |

결론: **TCB/리스트 수준의 전이는 같다**. "resume 후 다시 블록" 은 커널 상태 기계가 아니라 블로킹 API 의 재시도 루프가 하는 일이다 — 모델의 `wake_reason = ABORT` 를 받은 API 가 다시 `task_block_on` 하면 FreeRTOS 와 같아진다.

빌드 (FreeRTOSConfig.h 는 RTOS-B08 것에서 `configUSE_TICK_HOOK 0`, `INCLUDE_xTaskAbortDelay 1` 만 바꿈, hooks.c 는 같음):

<!-- code: demo_freertos_states.c -->

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_states.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o demo_states && ./demo_states
```

**Zephyr 는 (문서 기준, 미검증)**: 상태가 하나의 enum 이 아니라 **비트 플래그** 다 (`_THREAD_PENDING`, `_THREAD_SUSPENDED`, `_THREAD_DEAD`, `_THREAD_PRESTART` 등을 `thread_base.thread_state` 에 OR). 그래서 "대기 중이면서 suspend 됨" 이 한 스레드에 동시에 표현되고, 어떤 비트도 없으면 ready. 우선순위는 **숫자가 작을수록 높고**, 음수는 협력형(cooperative) 스레드다. µC/OS-II/III 도 작은 숫자가 높은 우선순위 (문서 기준).

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | FreeRTOS (V11.3.2 소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `tcb_t` | `tskTCB` / `TCB_t` (tasks.c:377-455), 핸들 = `TaskHandle_t` | `struct k_thread` | `osThreadId_t` (내부 구조는 구현 의존, RTX5 등) |
| `task_state_t` | `eTaskState` eRunning…eInvalid (task.h:109-117), `eTaskGetState()` | `thread_state` 비트 | `osThreadState_t`, `osThreadGetState()` |
| `tcb_init` (→ READY) | `xTaskCreate` / `xTaskCreateStatic` → `prvAddTaskToReadyList` (tasks.c:287-293) | `k_thread_create` (delay `K_NO_WAIT`) | `osThreadNew` |
| `task_delay` | `vTaskDelay` (2506) | `k_sleep` / `k_msleep` | `osDelay` |
| `task_block_on` | 내부: `vTaskPlaceOnEventList` (5402-5425) | 내부: `z_pend_curr` | (각 객체 Acquire/Get 안에서) |
| `obj_signal` | 내부: `xTaskRemoveFromEventList` (5500-) | 내부: `z_unpend_first_thread` | — |
| `task_suspend` / `task_resume` | `vTaskSuspend` (3197) / `vTaskResume` (3430), ISR: `xTaskResumeFromISR` (3491) | `k_thread_suspend` / `k_thread_resume` | `osThreadSuspend` / `osThreadResume` |
| `task_delete` | `vTaskDelete` (2212) — 자기 삭제는 idle 이 회수 (2269-2274) | `k_thread_abort` | `osThreadTerminate` / `osThreadExit` |
| 연습 2 abort | `xTaskAbortDelay` (4737) | `k_wakeup` (sleep 중인 스레드) | — |
| `KERNEL_ASSERT` | `configASSERT` (FreeRTOSConfig.h) | `__ASSERT` (CONFIG_ASSERT) | — |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

출력은 5. 풀이에 실은 B1 행렬 + Part C 타임라인·전이 행렬 + 마지막 `40/40 passed` (0.01 초). 5번 실행 모두 `40/40 passed`, 출력 md5 5번 모두 같음(결정적). 새니타이저 — 경고 0, 종료 코드 0:

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
```

스레드가 없는 단일 스레드 시뮬레이터라 TSan 은 의미가 없다. 이 레슨의 버그(유령 노드, 이중 삽입, 반쯤 바뀐 상태)는 전부 **장부 버그** 라서 새니타이저가 아니라 불변식 검사기(`k_check`)와 전후 비교로 잡는다 — 리스트 링크가 꼬여도 메모리 접근은 합법이기 때문이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 타임아웃 처리에서 `state_node` 만 빼기 | `event_node` 가 객체 대기 리스트에 남는다(유령). 나중 give 가 이미 READY 인 태스크를 또 깨워 ready 리스트에 **이중 삽입** → 원형 리스트가 꼬이고 순회가 끝나지 않는다. B3 가 이 상태를 만들어 검사기로 잡는다 | `detach(t)`: 두 노드를 무조건 `list_remove` |
| 이벤트로 깨울 때 delayed 에서 안 빼기 | 몇 tick 뒤 tick 처리가 "타임아웃" 으로 **또 깨운다**. 깨어난 이유가 뒤바뀌고(EVENT→TIMEOUT), RUNNING 중인 태스크를 READY 로 만들 수도 있다 | 같은 `detach` (A4 테스트: BLOCKED→READY 딱 1번) |
| 리스트를 바꾼 뒤 assert | 테스트 모드(훅이 돌아옴)에서 "거부했는데 반쯤 바뀐" 커널. 제품에서는 안 보이고 테스트에서만 이상하게 보인다 | `if (!TRANSITION_CHECK(t, TO)) return false;` 를 첫 줄에 |
| `sp` 를 TCB 중간에 둠 | 포트 asm(PendSV)은 `ldr r0, [r1]` 처럼 `[TCB+0]` 을 스택 포인터로 읽는다. 필드 순서를 바꾸면 엉뚱한 값이 SP 가 되어 첫 스위치에서 HardFault | `sp` 를 첫 멤버로 고정, `_Static_assert(offsetof(tcb_t, sp) == 0, "")` |
| 블록하면서 `K.current` 를 안 비움 | RUNNING 이 아닌데 current 가 남아 다음 dispatch 가 "still RUNNING" 으로 거부되거나, 두 태스크가 RUNNING | `detach` 안에서 `if (K.current == t) K.current = NULL;` |
| 무한 대기를 delayed 에 `wake_tick = tick + 0xFFFFFFFF` 로 넣기 | 32비트 tick 이 돌면 **언젠가 깨어난다** (~49.7일 @1 kHz). "영원히" 가 아니다 | delayed 에 넣지 않는다 (FreeRTOS: suspended 리스트, tasks.c:8717-8722) |
| 같은 우선순위 정렬 삽입에 `<` | 같은 값 **앞** 에 끼어들어 LIFO — 나중 온 태스크가 먼저 깸 | `while (next->value <= n->value)` (FreeRTOS list.c:192) |
| 깨어난 태스크를 ready 리스트 **앞** 에 넣기 | 같은 우선순위에서 방금 깬 태스크가 오래 기다린 태스크를 추월 → 굶주림 | `list_insert_end` (FreeRTOS `prvAddTaskToReadyList` 도 `listINSERT_END`, tasks.c:291) |
| DELETED TCB 를 바로 free (자기 삭제) | 지금 그 스택 위에서 달리는 중 — 해제한 메모리 위에서 다음 명령을 실행한다 | 좀비 목록 → idle 이 회수 (연습 4, tasks.c:2269-2274) |

## 8. 면접 Q&A

<details>
<summary>Q1. TCB 에는 최소 무엇이 들어가나요? 왜 스택 포인터가 첫 멤버죠?</summary>

저장된 스택 포인터(컨텍스트는 태스크 스택에 밀어 두고 그 꼭대기만 TCB 에), 스택 영역(검사·HWM), 우선순위(유효값 + 상속 전 원래값), 상태(또는 리스트 소속), 깨어날 시각, 기다리는 객체, 리스트 노드(상태 리스트용 + 이벤트 리스트용), 이름. FreeRTOS 는 여기에 알림 값·TLS·런타임 통계 등을 `#if` 로 붙인다 (tasks.c:377-455). SP 가 첫 멤버인 이유: 포트 asm 이 `pxCurrentTCB` 를 읽고 그 주소 그대로를 SP 저장/복원 위치로 쓴다 — 오프셋 0 이면 `ldr r0, [r1]` 한 명령 (CM4F port.c:533-534 주석 "The first item in pxCurrentTCB is the task top of stack"). 꼬리: "MPU 포트에서 두 번째 멤버는?" → `xMPUSettings` 가 두 번째여야 한다고 주석에 있다 (tasks.c:381-382). 꼬리2: "TCB 와 스택을 같은 블록에 할당하면?" → `xTaskCreateStatic` 은 둘 다 사용자 버퍼, 동적 생성은 "If the stack grows down then allocate the stack then the TCB so the stack does not grow into the TCB" (tasks.c:1640-1642) — 오버플로가 TCB 를 덮지 않게 할당 순서를 고른다 (상세는 RTOS-M13).

</details>

<details>
<summary>Q2. Blocked 에서 바로 Running 으로 갈 수 있나요? Suspended 와 Blocked 의 차이는?</summary>

못 간다. RUNNING 은 스케줄러만 만들고 스케줄러는 ready 리스트에서만 고른다. 깨어나는 사건(이벤트·타임아웃)은 READY 로 옮길 뿐이고, 그 태스크가 가장 높으면 **바로 다음 스케줄링 지점**에 RUNNING 이 된다(선점이면 ISR 출구). 이 경로를 건너뛰면 대기·지연 리스트에서 빠지는 단계가 사라진다. Blocked = **사건이나 시간을 기다림** — 그 사건이 오면 스스로 풀린다, 보통 타임아웃이 있다. Suspended = **다른 누가 resume 할 때까지 스케줄링 대상에서 제외** — 시간이 지나도, 이벤트가 와도 안 풀린다 (데모 Q3: suspend 중 give 해도 count 만 1). 꼬리: "Blocked 태스크를 suspend 하면?" → FreeRTOS 는 이벤트 리스트에서도 빼서(tasks.c:3223-3226) 그 사이 이벤트를 못 받는다. resume 후 블로킹 API 가 재검사해서 토큰이 있으면 받고 없으면 남은 시간으로 다시 블록 (데모 Q3/Q4).

</details>

<details>
<summary>Q3. FreeRTOS 는 왜 리스트 아이템을 두 개 갖나요?</summary>

`xSemaphoreTake(s, 10)` 하는 태스크는 세마포어의 대기 리스트(우선순위 순 — 누가 먼저 받나)와 지연 리스트(시각 순 — 언제 포기하나)에 **동시에** 있어야 한다. 아이템 하나는 리스트 하나에만 들어간다. 그래서 `xStateListItem`(ready/delayed/suspended — 상태) + `xEventListItem`(객체 대기 리스트) (tasks.c:389-390). 어느 쪽 사건으로 깨든 둘 다 뺀다: tick 처리 4913-4919, 이벤트 처리 5525-5530. 꼬리: "리스트 아이템의 `pxContainer` 는 왜 있죠?" → O(1) 제거(이중 연결이라 이웃만 고치면 됨)와 "지금 어느 상태인가" 판정 — `eTaskGetState` 가 이걸로 상태를 계산한다 (2580-2594). 꼬리2: "Zephyr 는?" → 스레드에 대기용 노드와 타임아웃용 노드를 따로 둔다고 알고 있다 (`base.qnode_dlist`, `base.timeout`) — 미검증.

</details>

<details>
<summary>Q4. 지금 실행 중인 태스크는 ready 리스트에 있나요?</summary>

설계 선택이다. FreeRTOS 는 **있다**: `pxCurrentTCB` 가 가리킬 뿐 ready 리스트에서 빼지 않는다 (블록할 때 비로소 뺀다는 주석, tasks.c:8702-8704). 장점: 선점·time slice 때 리스트 조작이 없다 — `listGET_OWNER_OF_NEXT_ENTRY` 가 리스트 인덱스를 한 칸 돌리면 같은 우선순위 라운드 로빈 (tasks.c:210). 이 레슨의 모델은 **뺀다**: 상태 ↔ 리스트가 1:1 이라 불변식이 단순하고, 선점 때 맨 뒤로 넣는다. 꼬리: "빼는 방식에서 선점당한 태스크는 앞에 넣어야 하나 뒤에 넣어야 하나?" → time slice 가 끝나서면 뒤(공정), 더 높은 태스크에 선점당한 것이면 앞에 넣어 "남은 slice 를 이어 받게" 하는 커널도 있다 (POSIX `SCHED_FIFO` 는 선점된 스레드를 자기 우선순위 리스트 **맨 앞**에 둔다고 규정). 이 모델은 단순히 뒤.

</details>

<details>
<summary>Q5. assert 는 어떻게 단위 테스트하나요? 그리고 제품에서는 어떻게 하나요?</summary>

두 층. ① **훅 교체**: `KERNEL_ASSERT` 가 함수 포인터 훅을 부르게 하고, 테스트에서는 카운터 훅으로 바꿔 "정확히 1번 걸렸나 + 상태가 그대로인가" 를 본다 (B1: 13칸). 이를 위해 assert 가 bool 을 돌려주고 함수는 검사 → 변경 순서여야 한다. ② **death test**: 기본 훅이 진짜로 멈추는지는 다른 프로세스에서 — `fork()` 후 자식에서 불법 전이, 부모가 `waitpid` → `WTERMSIG == SIGABRT` (demo_death.c, 상태 134). 합법 대조군을 함께 둔다. 제품에서는 `configASSERT` 를 `taskDISABLE_INTERRUPTS(); for(;;);` 또는 브레이크포인트(`__BKPT`)로 정의하는 예가 흔하고, 필드에서는 파일/라인을 no-init RAM 에 남기고 워치독 리셋 → 부팅 후 리포트 (RTOS-A09 류). 꼬리: "assert 를 릴리스에서 끄면?" → 불법 전이가 조용히 리스트를 망가뜨린다. 커널 상태 기계 assert 는 비용이 작으니 켜 두는 쪽을 권한다 (팀 정책 문제).

</details>

<details>
<summary>Q6. 같은 tick 에 타임아웃과 이벤트가 겹치면 누가 이기나요?</summary>

먼저 처리된 쪽. Part C tick 20: 이 모델은 tick(타임아웃) → ISR 순서라 IMU 가 TIMEOUT 으로 깨고, 같은 tick 의 DRDY 는 대기자가 없어 `missed`. 중요한 건 **둘 다 처리돼도 깨는 건 한 번** 이라는 것 — 첫 사건이 두 노드를 모두 빼므로 두 번째 사건은 그 태스크를 못 찾는다. 이벤트를 잃는지는 객체에 달렸다: 세마포어는 give 가 count 로 남아 다음 take 가 즉시 성공 (FreeRTOS 는 깨어난 쪽이 루프 맨 위에서 count 를 **먼저** 보므로 마감 직전에 들어온 토큰도 받는다 — RTOS-B09 연습 1). 꼬리: "IMU 드라이버라면?" → 타임아웃 후 DRDY 핀 상태/STATUS 레지스터를 한 번 더 읽어 "늦게 온 데이터" 를 확인하고, 연속 타임아웃 N번이면 I2C 버스 복구(SCL 9 클럭 토글)·센서 리셋.

</details>

<details>
<summary>Q7. 태스크가 자기 자신을 삭제하면 TCB 와 스택은 언제 해제되나요?</summary>

바로는 못 한다 — 지금 그 스택 위에서 `vTaskDelete` 를 실행 중이다. FreeRTOS 는 자기 자신(또는 실행 중) 삭제를 `xTasksWaitingTermination` 리스트에 넣고 `uxDeletedTasksWaitingCleanUp` 을 올린 뒤 (tasks.c:2269-2274), idle 태스크가 `prvCheckTasksWaitingTermination` 에서 해제한다. 그래서 **idle 태스크를 굶기면(높은 태스크가 쉬지 않음) 메모리가 안 돌아온다** — 흔한 누수 원인. 다른 태스크를 삭제하면 바로 해제된다. 연습 4 가 이걸 좀비 목록으로 재현한다. 꼬리: "정적 할당 태스크는?" → `ucStaticallyAllocated` 로 구분해 free 하지 않는다 (tasks.c:444-446).

</details>

## 9. 추가 연습

### 연습 1 — 리스트만 보고 상태 알아내기 (`eTaskGetState` 방식)

`state_from_lists(t)` 를 `t->state` 를 보지 않고 쓰라: current 면 RUNNING, suspended 리스트면 SUSPENDED, delayed 에 있거나 event_node 가 어딘가 있으면 BLOCKED, ready 중 하나면 READY, 어디에도 없으면 DELETED. Part C 를 돌리며 매 tick 모든 태스크에서 `t->state` 와 비교하라.

<details>
<summary>답 보기</summary>

실제 출력: `ex1 state_from_lists: 120 checks over 30 ticks, mismatches=0, invariant errors=0`. 무한 대기 태스크(state_node 없음, event_node 만)도 BLOCKED 로 판정. 이게 가능하다는 건 **`state` 필드가 중복 정보** 라는 뜻 — FreeRTOS TCB 에 state 가 없는 이유다 (RAM 절약 + 두 진실이 어긋날 일이 없음). 대신 판정이 리스트 비교 몇 번이라 디버거/트레이스가 조금 일한다. FreeRTOS 는 무한 대기를 suspended 리스트에 넣으므로 "suspended 리스트 + event 아이템 있음 → eBlocked" 분기가 더 필요하다 (tasks.c:2602-2638).

</details>

### 연습 2 — `task_abort_delay` (xTaskAbortDelay)

BLOCKED 태스크를 강제로 READY(`WAKE_ABORT`)로. BLOCKED 가 아니면 **assert 가 아니라 false** — 사용자 API 의 정상 실패.

<details>
<summary>답 보기</summary>

`if (t->state != TASK_BLOCKED) return false; return task_unblock(t, WAKE_ABORT);` — 실제 출력: `ex2 abort_delay: delayed task -> READY (ABORT), READY task -> false, violations=0`. 객체 대기 + 타임아웃 중인 태스크도 두 리스트에서 모두 빠진다. FreeRTOS 도 `eTaskGetState(xTask) == eBlocked` 일 때만 pdPASS (tasks.c:4750) 이고, 깨어난 태스크는 `ucDelayAborted` 로 "타임아웃과 같은 결과" 를 받는다 (5710-5715). 데모 Q5: `abort(dly)=1`, 1000 tick delay 가 t=74 에 끝남, suspended·ready 태스크는 0(pdFAIL). 쓰임: 종료 요청 시 대기 중인 통신 태스크를 깨워 정리시키기.

</details>

### 연습 3 — 우선순위 변경 `task_set_prio(t, p, resort)`

`base_prio = p`. 상속 중(`prio > base_prio`)이면 p 가 더 클 때만 `prio` 도 바꾼다. READY 면 ready 리스트를 옮기고, 객체를 기다리는 중이면 정렬 키(`NPRIO - prio`)를 바꾼다 — `resort=true` 면 대기 리스트에서 다시 정렬 삽입, `false` 면 키만 (FreeRTOS 방식). MID(2), LO(1) 순으로 대기 중 LO 를 3 으로 올리고 signal 하면?

<details>
<summary>답 보기</summary>

실제 출력:

```text
ex3 resort=1: LO prio 1->3 while waiting, first woken=LO, invariant errors=0
ex3 resort=0: LO prio 1->3 while waiting, first woken=MID, invariant errors=2 — q: not sorted at LO
```

FreeRTOS `vTaskPrioritySet` 은 이벤트 아이템의 **값만** 바꾸고 (tasks.c:2974-2976) 리스트 이동은 ready 리스트일 때만 한다 — 주석: "If the task is in the blocked or suspended list we need do nothing more than change its priority variable" (2983-2986). 그래서 이미 대기 중인 태스크의 우선순위를 올려도 **그 대기에서는** 예전 자리에서 깬다 (다음 대기부터 반영). 상속 규칙도 같다: 상속 중이면 새 값이 상속값보다 클 때만 `uxPriority` 변경, `uxBasePriority` 는 항상 (2954-2964). 테스트: base 2·prio 5(상속) 에 set 3 → prio 5 유지·base 3, set 6 → 둘 다 6.

</details>

### 연습 4 — 자기 삭제와 지연 회수

`task_delete_deferred(t)`: 남을 지우면 즉시 `reclaim`(스택을 `0xDEADBEEF` 로 덮고 `sp = NULL`), 자기 자신이면 좀비 목록에 넣고 `idle_reclaim()` 이 나중에. 지운 TCB 를 다시 `make_ready` 하면?

<details>
<summary>답 보기</summary>

실제 출력: `ex4 delete: self -> zombie then idle reclaim, other -> immediate; reclaimed=2, use-after-delete violations=1`. 자기 삭제 직후 `reclaimed=0`, `sp != NULL`(아직 그 스택 위) → idle 후 2. 지운 TCB 재사용은 `illegal DELETED -> READY` 로 거부. 실제 MCU 에서 회수 전 스택을 패턴으로 채워 두면 use-after-free 를 덤프에서 알아보기 쉽다 (`0xA5` 채우기는 FreeRTOS 의 스택 HWM 방식, RTOS-M09).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 14/14, 경고 0
```

## 10. 핵심 정리

- TCB = **SP(첫 멤버)** + 스택 영역 + `prio`/`base_prio` + 상태 + 깨어날 tick + 기다리는 객체 + 리스트 노드 **2개**.
- 상태 = 리스트 소속. READY ⇔ ready[prio], BLOCKED ⇔ delayed 및/또는 객체 대기 리스트, SUSPENDED ⇔ suspended, RUNNING ⇔ current.
- 합법 12 / 불법 13. **RUNNING 의 입구는 READY 하나**, 블록은 자기 자신만, DELETED 에서 나가는 길 없음, 자기 자신으로의 전이는 버그.
- 타임아웃 있는 대기는 **두 리스트에 동시에** → 어떤 사건으로 깨든 `detach` 로 둘 다 뺀다 (유령 노드 = 이중 깨움 = 리스트 손상).
- `KERNEL_ASSERT` = 교체 가능한 훅 + bool 반환 + **검사 먼저, 변경 나중**. 테스트는 카운터 훅, 진짜 멈춤은 fork death test.
- FreeRTOS: TCB 에 state 없음(`eTaskGetState` 가 계산), RUNNING 도 ready 리스트에, 무한 대기는 suspended 리스트에, resume/suspend 는 관대, 블록 중 suspend→resume 은 API 재시도 루프가 처리.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` — `tskTCB` 377-455, ready/delayed/suspended 리스트 478-495, `prvAddTaskToReadyList` 287-293, `taskSELECT_HIGHEST_PRIORITY_TASK` 210, `vTaskDelete` 2212-2274, `vTaskDelay` 2506-2528, `eTaskGetState` 2556-2660, `vTaskPrioritySet` 2844-3004, `vTaskSuspend` 3197-3350, `prvTaskIsTaskSuspended` 3360-3420, `vTaskResume` 3430-3470, `xTaskAbortDelay` 4737-4760, `xTaskIncrementTick` 4894-4928, `vTaskPlaceOnEventList` 5402-5425, `xTaskRemoveFromEventList` 5500-5570, `xTaskCheckForTimeOut` 5693-5715, `prvAddCurrentTaskToDelayedList` 8685-8760; `include/task.h` `eTaskState` 109-117; `include/list.h` `ListItem_t` 144-153; `include/FreeRTOS.h` `configASSERT` 97-101; `queue.c` 재시도 루프 1686; `portable/GCC/ARM_CM4F/port.c` PendSV 513-534: <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS 문서 — Task states (Running/Ready/Blocked/Suspended 그림), `eTaskGetState`, `vTaskSuspend`, `xTaskAbortDelay`, `configASSERT`: <https://www.freertos.org> → Documentation → Kernel → Tasks / API Reference
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Chapter "Task Management" (상태 전이도, Blocked vs Suspended): <https://www.freertos.org/Documentation/02-Kernel/07-Books-and-manual/01-RTOS_book>
- Zephyr — Threads (Thread States, Thread Priorities, cooperative vs preemptible): <https://docs.zephyrproject.org/latest/kernel/services/threads/index.html>
- CMSIS-RTOS2 — Thread Management (`osThreadState_t`, `osThreadSuspend`/`osThreadResume`): <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__ThreadMgmt.html>
- Jean J. Labrosse, *µC/OS-III: The Real-Time Kernel* — Task states, TCB (`OS_TCB`) 장
- GoogleTest — Death tests (`EXPECT_DEATH`, fork 로 죽는 코드 격리): <https://google.github.io/googletest/advanced.html#death-tests>
- POSIX `waitpid` / `WIFSIGNALED` / `WTERMSIG`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/waitpid.html>
- 관련 레슨: RTOS-B03 (컨텍스트 스위치 — `sp` 가 채워지는 곳), RTOS-B04 (O(1) 비트맵 ready list), RTOS-B05 (선점·PendSV), RTOS-B06 (delayed list 와 tick wrap), RTOS-B08/B09 (세마포어 — 이벤트를 count 로 기억), RTOS-M04 (`base_prio` 와 상속), RTOS-M09 (스택 검사), RTOS-M13 (FreeRTOS 내부 따라가기), C-M14 (ISR 공유 데이터)

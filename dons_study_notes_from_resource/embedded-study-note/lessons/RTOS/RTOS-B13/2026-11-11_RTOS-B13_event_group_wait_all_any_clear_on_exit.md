---
id: RTOS-B13
time: 65분
updated: 2026-11-11
---
# RTOS-B13 이벤트 그룹: wait-all/any와 clear-on-exit

## 학습 목표

- 이벤트 그룹(event group, event flags)을 **비트 묶음 + 순서 없는 대기 리스트** 로 직접 구현하고, `wait(ANY|ALL, clear-on-exit, timeout)` · `set` · `clear` 의 규칙을 FreeRTOS `event_groups.c` 줄 번호로 설명할 수 있다.
- `set` 한 번이 조건이 맞는 대기자를 **모두** 깨우고, clear-on-exit 는 **모든 대기자를 평가한 뒤** 한 번에 적용된다는 것(그래서 W1 이 bit0 을 지워도 W2 의 wait-all 이 깨어난다)을 테스트로 증명한다.
- 타임아웃의 세 갈래 — 마감 전에 맞음 / 마감 tick 에 맞음 / 타임아웃 뒤 돌기 전에 맞음(재검사) — 를 구분한다.
- "확인(get) → 처리 → clear" 와 "남의 비트까지 clear" 가 이벤트를 잃는 경쟁 조건을 결정적으로 재현하고, **원자적 wait-with-clear** 와 "비트마다 주인 하나" 규칙으로 고친다.
- 부팅 동기화(init 3개 → main), 워치독 건강 감시, 랑데부(`xEventGroupSync`)에 이벤트 그룹을 쓰고, 세마포어·큐·태스크 알림과 언제 바꿔 써야 하는지 말한다.

## 1. 문제

"이벤트 그룹은 세마포어와 뭐가 다르죠? 여러 조건을 **모두** 기다려야 하면 어떻게 하죠? 확인하고 지우는 사이에 이벤트가 또 오면?" — RTOS 면접에서 세마포어(RTOS-B08/B09) 다음에 꼭 나오는 질문이다. 펌웨어에서는 **부팅 동기화**(센서·무선·저장소 초기화가 다 끝나야 앱 시작), **여러 원인 중 하나를 기다리기**(버튼 / UART 프레임 / 배터리 경고), **워치독 건강 감시**(모든 태스크가 이번 주기에 살아 있었나)에 쓴다.

이벤트 그룹 = 비트 하나하나가 "그 일이 일어났다" 를 뜻하는 정수(`bits`) + 그 비트들을 기다리는 태스크 목록. 세마포어와 결정적으로 다른 점 세 가지:

- **조합을 기다린다**: `want = 0x7` 을 **ALL**(전부 1) 또는 **ANY**(하나라도 1) 로.
- **방송(broadcast)이다**: `set` 한 번에 조건이 맞는 대기자가 **몇 명이든 모두** 깨어난다. 세마포어 give 는 한 명.
- **개수를 세지 않는다**: 같은 비트를 두 번 set 해도 1 이다(RTOS-B08 바이너리 세마포어처럼 합쳐짐). 비트는 누군가 **지울 때까지 남는다** — 지우는 방법이 `clear` 와 wait 의 **clear-on-exit**(조건이 맞아 나갈 때 `want` 비트를 지움)다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">이벤트 그룹 = 비트 + 순서 없는 대기 리스트 — set(0x1) 한 번의 처리</text>
  <rect x="20" y="40" width="170" height="64" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="32" y="64" fill="currentColor" font-family="monospace">bits = 0b010</text>
  <text x="32" y="88" fill="currentColor">set 전: bit1 만 1</text>
  <text x="205" y="64" fill="currentColor">head →</text>
  <rect x="262" y="40" width="140" height="64" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="274" y="64" fill="currentColor" font-family="monospace">W1 p1</text>
  <text x="274" y="88" fill="currentColor" font-family="monospace">ANY 0x1 +CLR</text>
  <rect x="422" y="40" width="140" height="64" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="434" y="64" fill="currentColor" font-family="monospace">W2 p2</text>
  <text x="434" y="88" fill="currentColor" font-family="monospace">ALL 0x3 +CLR</text>
  <rect x="582" y="40" width="140" height="64" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="594" y="64" fill="currentColor" font-family="monospace">W4 p2</text>
  <text x="594" y="88" fill="currentColor" font-family="monospace">ALL 0x7</text>
  <path d="M402 72 L420 72 M562 72 L580 72" stroke="currentColor" stroke-width="1.5"/>
  <rect x="20" y="122" width="702" height="132" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="34" y="145" fill="currentColor" font-family="monospace">① bits |= 0x1           → 0b011  (아직 아무것도 지우지 않는다)</text>
  <text x="34" y="167" fill="currentColor" font-family="monospace">② W1: ANY 0x1 ✓ → 깨움, value=0x3, clr |= 0x1</text>
  <text x="34" y="189" fill="currentColor" font-family="monospace">③ W2: ALL 0x3 ✓ → 깨움, value=0x3, clr |= 0x3   (bits 는 여전히 0b011)</text>
  <text x="34" y="211" fill="currentColor" font-family="monospace">④ W4: ALL 0x7 ✗ → 계속 대기</text>
  <text x="34" y="233" fill="currentColor" font-family="monospace">⑤ 루프가 끝난 뒤: bits &amp;= ~clr → 0b000, set 의 반환값 0x0</text>
  <rect x="20" y="266" width="702" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="34" y="288" fill="currentColor">틀린 판(깨우자마자 지움): ② 에서 bits → 0b010 → ③ W2 는 ✗ → W2 는 이벤트를 놓친다.</text>
  <text x="34" y="310" fill="currentColor">리스트 순서가 W2, W1 이면 반대로 W1 이 놓친다 — 순서에 따라 달라지는 버그 (연습 4).</text>
</svg>
```

**미니 커널 (하네스 제공).** RTOS-B09 와 같은 tick 시뮬레이터에 B02 어휘(`tcb_t`, `TASK_READY/RUNNING/BLOCKED`, `kernel_tick()`, `sched_pick_next()`, `task_delay()`)를 쓴다. OS 스레드 1개라 모든 실행이 매번 같다.

| 규칙 | 이 모델 | 실제 RTOS 와 다른 점 |
|---|---|---|
| tick 하나 | ① tick hook (ISR 자리) → ② 출발·지연·**타임아웃 마감** 처리 → ③ 최고 우선순위 READY 태스크의 `step()` 한 번 | 실제는 set 안에서 바로 선점. 여기선 다음 tick 의 ③ 에서 |
| ISR 의 set | hook 이 `eg_set` 을 직접 부른다 = "타이머 데몬이 tick 맨 앞에서 처리" | FreeRTOS 는 ISR 에서 직접 못 한다 → 데몬 태스크로 미룸 (연습 2, RTOS-M01) |
| 같은 우선순위 | 먼저 READY 된 순, time slicing 없음 | FreeRTOS 기본은 time slicing 있음 |
| 대기 리스트 | 정렬 없이 **끝에 붙인다**, `next/prev` 로 O(1) 제거 | FreeRTOS 도 `listINSERT_END` (tasks.c:5453) |
| 타임라인 | `R` 실행 · `r` READY · `B` 이벤트 그룹 대기(또는 영원히 멈춤) · `d` `task_delay` · `.` 출발 전/끝 | RTOS-B09/M03 형식 + `d` |

### Part A — 이벤트 그룹 (set / clear / wait all|any, clear-on-exit, 타임아웃)

`evgroup_t { bits, head, tail, nwait }` 와 TCB 의 `ev_want / ev_flags / ev_value / ev_by_set` 으로 일곱 함수를 채운다.

- `wl_push_back / wl_remove` — 대기 리스트: 끝에 붙이기, 아무 위치에서나 O(1) 제거.
- `eg_cond_met(bits, want, all)` — ALL 이면 `(bits & want) == want`, ANY 면 `!= 0`.
- `eg_wait(g, self, want, flags, timeout, &out)` — `want == 0` 이거나 상위 8비트(커널 제어용)를 건드리면 `KASSERT` + `EG_EINVAL`. 이미 맞으면 `EG_OK`(값은 **지우기 전**, `EG_CLEAR` 면 지움). `timeout == 0` 이면 `EG_TIMEOUT`(폴링). 아니면 리스트 끝에 붙이고 블록 → `EG_BLOCKED`.
- `eg_set(g, set)` — OR 하고, 리스트를 **끝까지** 돌며 맞는 대기자를 모두 깨운다(`ev_value` = 지금 비트, `ev_by_set = true`). clear-on-exit 비트는 **모아 두었다가 루프 뒤에** 지운다. 반환 = 최종 비트.
- `eg_clear(g, clr)` — 지우기 **전** 값을 돌려준다.
- `eg_on_timeout(g, t)` — 커널이 마감 tick 에 부른다: 리스트에서 빼고 `ev_by_set = false`.
- `eg_wait_result(g, self, &out)` — 다시 돌 때: set 으로 깼으면 `ev_value`, 타임아웃이면 **지금 비트로 다시 검사**(맞으면 clear 하고 `EG_OK`).

### Part B — 여러 대기자, 타임아웃, 경쟁 조건 두 개

1. **B1**: W1(p1, ANY 0x1 +CLR), W2(p2, ALL 0x3 +CLR), W3(p3, ANY 0x2), W4(p2, ALL 0x7, 10 tick). tick 5 에 `set(0x2)`, tick 7 에 `set(0x1)`. 깨운 순서 `W3 W1 W2`, 실행 순서 `W3 W2 W1 W4`, W1·W2 둘 다 `0x3` 을 받고, W4 는 타임아웃.
2. **B2**: ALL 0x7 을 5 tick 기다리는데 0x2 가 안 옴 → `EG_TIMEOUT`, 반환 비트로 **무엇이 빠졌는지** 안다 / 마감 tick 에 0x2 가 옴 → 성공 / 타임아웃으로 READY 가 된 뒤 돌기 전에 더 높은 태스크가 0x2 를 세움 → 재검사로 성공 + clear.
3. **B3 경쟁 1 (이벤트 분실)**: RX 이벤트를 "get 으로 확인 → (없으면 clear 없이 wait) → 4 tick 처리 → **그제서야** clear" 하는 버그판은 처리 중 온 이벤트를 지운다(3개 중 2개). 고친판: `wait(RX, ANY|CLEAR)` 한 번으로 확인과 지우기를 원자적으로(3개 모두).
4. **B4 경쟁 2 (남의 비트 지우기)**: CTRL 이 자기 IMU 이벤트를 처리하고 "남은 플래그 싹 정리" 로 `clear(0xFFFFFF)` → 아직 기다리기 전인 LOG 의 `LOGREQ` 가 사라져 LOG 가 타임아웃. 고친판: 자기 비트만 clear-on-exit.

### Part C — BLE 센서 노드 (nRF52 같은 MCU) 의 부팅과 워치독

1. **C1 부팅**: SENS(p3, IMU SPI 초기화 3 tick) · RADIO(p2, BLE 스택 6 tick) · STORE(p2, 플래시 FS 마운트 4 tick) 가 끝날 때마다 자기 비트. MAIN(p4) 은 `wait(0x7, ALL, 30)` — **clear 하지 않는다**(상태 플래그). 늦게 출발한 TELEM 은 폴링(`timeout 0`)으로 바로 통과. RADIO 가 실패하면 MAIN 은 tick 30 에 타임아웃 + "무엇이 빠졌나(0x2)" 로 degraded 모드. MAIN 이 늦게 출발해도 결과가 같다(순서 무관).
2. **C2 워치독**: IMU·CTRL·COMM 이 주기마다 alive 비트, WDG(p4) 가 `wait(0x7, ALL|CLEAR, 8)` 로 성공할 때만 IWDG 를 kick. COMM 이 5번 일한 뒤 멈추면(영원히 블록) WDG 가 **빠진 비트 0x4** 를 보고 kick 을 멈추고 → 하드웨어 IWDG 리셋.

## 2. Problem (English)

> **Implement an event group (event flags) in a tiny tick-driven kernel.**
>
> ```c
> bool    eg_cond_met(ebits_t bits, ebits_t want, bool all);
> int     eg_wait(evgroup_t *g, tcb_t *self, ebits_t want, int flags /* EG_ALL | EG_CLEAR */,
>                 int timeout /* ticks, 0 = poll, EG_FOREVER */, ebits_t *out);  // EG_OK / EG_TIMEOUT / EG_BLOCKED / EG_EINVAL
> int     eg_wait_result(evgroup_t *g, tcb_t *self, ebits_t *out);               // after a blocked wait resumes
> ebits_t eg_set(evgroup_t *g, ebits_t bits);    // returns the group value after auto-clears
> ebits_t eg_clear(evgroup_t *g, ebits_t bits);  // returns the value BEFORE clearing
> ```
>
> The top 8 bits are reserved for the kernel. `*out` is the group value at the moment the condition was met (before any clear-on-exit), or the current value on timeout. One `eg_set` must release **every** waiter whose condition becomes true; clear-on-exit bits are cleared only after all waiters have been evaluated. Waiters are kept in an unordered (FIFO-append) list.
>
> 1. Three init tasks each set one bit; `main` must not start until all three are set. Show what happens when one init fails.
> 2. Reproduce, deterministically, an event that is lost because a consumer checks the bit and clears it later; fix it.
> 3. Build a task-health watchdog: kick the hardware watchdog only if every worker checked in during the window.
>
> Follow-ups: event group vs counting semaphore vs task notification? Why can't FreeRTOS set bits directly from an ISR? Who should clear a bit? Implement `xEventGroupSync` — why can't it be `set()` followed by `wait()`?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.1 초에 끝나고 exit 1):

```text
FAIL starter.c:438  eg_cond_met(0x5, 0x5, true) && !eg_cond_met(0x4, 0x5, true) && ...
FAIL starter.c:453  eg_wait(&g_eg, w1, 0x1, EG_ANY | EG_CLEAR, EG_FOREVER, &v) == EG_BLOCKED && ...
...
Part B1 multi-waiter: wakes=[] run=[W1 W2 W3 W4] W1=0 W2=0 W3=0 W4 rc=0 got=0 bits=0 (done 4)
Part B3 lost event: buggy (get, wait, handle, clear) handled 6 of 3
Part B4 wrong-task clear: buggy CTRL -> ctrl=30 log ok=0 lost=0 (LOG rc=-1 at -1)
Part C1 boot: last init bit at tick 13, MAIN rc=0 got=0 starts tick 0 (ran 1 steps), TELEM rc=0 got=0x7, bits=0x7
Part C2 watchdog hang   : COMM hung at -1, first miss at -1 mask=0, kicks=64 misses=0, IWDG reset at -1
5/38 passed
```

스텁 `eg_wait` 는 "항상 이미 맞음" 이라 아무도 기다리지 않는다 — MAIN 이 tick 0 에 초기화도 안 된 시스템으로 출발하고, 처리하지도 않은 RX 를 6번 처리했다고 센다. 하네스 설계:

- **무한 루프가 없다.** `k_run(limit)` / `k_run_until(limit)` 이 tick 상한에서 멈춘다. 스텁 WDG 는 매 tick kick 하며 CPU 를 독점하지만 64 tick 에서 끝난다.
- **오라클은 학생 코드와 독립.** `H_set()` 은 set 전에 태스크 표 전체를 훑어 "누가 깨어나야 하고, 각자 어떤 값을 받고, 최종 비트가 얼마인지" 를 따로 계산한 뒤 `eg_set` 결과와 비교한다(`g_set_err`). `list_check()` 는 매 tick 대기 리스트의 prev 링크·tail·`nwait`·"이 그룹에서 BLOCKED 인 태스크 수" 를 대조한다(`g_list_err`).
- **체크는 상태·순서·반환값만.** 시간 측정 없음. 버그판 태스크(`rx_buggy`, `ctrl_buggy`)는 하네스가 준다 — 학생 코드가 맞으면 버그가 **재현되는지**도 체크한다.
- `(void)k_block; ...` 줄은 starter 에서 아직 안 쓰이는 함수의 unused 경고를 막는다(solution 에도 같은 줄 — diff 를 깨끗하게).

## 4. 힌트

<details>
<summary>힌트 1 — 리스트 순회 중에 지우려면 next 를 먼저 잡는다</summary>

`for (t = g->head, nx; t; t = nx) { nx = t->next; ... wl_remove(g, t); }` — `wl_remove` 가 `t->next` 를 NULL 로 만들기 때문에 순회 전에 꺼내 둔다. FreeRTOS 도 같은 이유로 `pxNext = listGET_NEXT( pxListItem )` 를 먼저 읽는다(event_groups.c:579, 주석 629–631).

</details>

<details>
<summary>힌트 2 — clear-on-exit 는 "지금" 이 아니라 "다 본 다음"</summary>

깨울 때마다 `g->bits &= ~t->ev_want` 를 하면 뒤 대기자는 이미 지워진 비트를 본다. `clr |= t->ev_want` 로 모았다가 루프가 끝난 뒤 `g->bits &= ~clr`. 그래야 같은 set 으로 조건이 맞은 대기자들이 **같은 값**(`ev_value`)을 받는다.

</details>

<details>
<summary>힌트 3 — 반환값은 지우기 전 값</summary>

`eg_wait` 가 즉시 성공할 때 `*out = cur` 를 먼저 적고 그다음 지운다. 호출자는 이 값으로 "무엇 때문에 깼나"(ANY) 를 판단한다. `eg_clear` 도 지우기 전 값을 돌려준다 — 원자적 fetch-and-clear 로 쓸 수 있다.

</details>

<details>
<summary>힌트 4 — 타임아웃은 커널이 리스트에서 빼고, 판단은 태스크가</summary>

`eg_on_timeout` 은 `wl_remove` + `ev_by_set = false` 뿐. 태스크가 다시 돌 때 `eg_wait_result` 가 `ev_by_set` 을 보고, false 면 **지금** 비트로 조건을 다시 본다. 타임아웃으로 READY 가 된 뒤 실제로 돌기 전에 비트가 채워질 수 있기 때문이다 (B2c).

</details>

<details>
<summary>힌트 5 — 버그판이 왜 잃는지 타임라인으로 확인</summary>

B3: tick 6 에 RX 가 오는데 버그판은 그 tick 에 처리를 끝내며 `eg_clear(RX)` 를 한다 — 방금 온 이벤트와 처리한 이벤트를 구분할 수 없다. B4: CTRL 의 `clear(0xFFFFFF)` 가 tick 4 에 LOGREQ 를 지우고, LOG 는 tick 15 에야 기다리기 시작한다.

</details>

## 5. 풀이

### Part A — 일곱 함수

```c
static ebits_t eg_set(evgroup_t *g, ebits_t set) {
    KASSERT((set & EG_CTRL_MASK) == 0);
    set &= ~EG_CTRL_MASK;
    ebits_t clr = 0;
    g->bits |= set;
    for (tcb_t *t = g->head, *nx; t; t = nx) {
        nx = t->next;
        if (!eg_cond_met(g->bits, t->ev_want, t->ev_flags & EG_ALL)) continue;
        if (t->ev_flags & EG_CLEAR) clr |= t->ev_want;   /* 모아 둔다 */
        t->ev_value = g->bits; t->ev_by_set = true;
        wl_remove(g, t);
        k_unblock(t);
    }
    g->bits &= ~clr;                                     /* 다 본 다음 한 번에 */
    return g->bits;
}
static int eg_wait_result(evgroup_t *g, tcb_t *self, ebits_t *out) {
    if (self->ev_by_set) { *out = self->ev_value; return EG_OK; }
    ebits_t cur = g->bits;                               /* 타임아웃: 지금 값으로 재검사 */
    *out = cur;
    if (eg_cond_met(cur, self->ev_want, self->ev_flags & EG_ALL)) {
        if (self->ev_flags & EG_CLEAR) g->bits &= ~self->ev_want;
        return EG_OK;
    }
    return EG_TIMEOUT;
}
```

Part A 단위 테스트 (전부 PASS):

| 테스트 | 기대 | 왜 |
|---|---|---|
| bits 0x5, `wait(0x1, ANY\|CLR, 0)` | `EG_OK`, out 0x5, bits 0x4 | 반환은 지우기 전 값 |
| bits 0x4, `wait(0x3, ALL, 0)` | `EG_TIMEOUT`, out 0x4, 블록 안 함 | timeout 0 = 폴링 |
| `want == 0`, `want = 0x01000000` | `EG_EINVAL`, assert 2번 | 제어 비트 보호 (FreeRTOS `configASSERT`, event_groups.c:328–329) |
| `clear(0x4)` | 0x4 반환, bits 0 | fetch-and-clear |
| W1..W4 블록 → `set(0x2)` → `set(0x1)` | wakes `W3 W1 W2`, W1·W2 `ev_value` 0x3, 최종 0, W4 남음 | 방송 + 늦은 clear |
| 리스트 W4 X Y 에서 X 타임아웃 | W4 ⇄ Y 로 이어짐 | 가운데 O(1) 제거 |
| X 타임아웃 뒤 `set(0x30)` → `eg_wait_result(X)` | `EG_OK`, 0x30, bits 0x20 | 재검사 + X 의 clear(0x10) |

**FreeRTOS V11.3.2 는 실제로 이렇게 한다** (`~/.esn_scratch/FreeRTOS-Kernel`, 직접 읽음):

- 구조: `EventGroup_t { EventBits_t uxEventBits; List_t xTasksWaitingForBits; ... }` (event_groups.c:54–66). 이게 전부다 — 세마포어(큐)보다 훨씬 작다.
- **set** (`xEventGroupSetBits`, 547–647): `vTaskSuspendAll()`(567) → `uxEventBits |= uxBitsToSet`(574) → `while( pxListItem != pxListEnd )` 로 **리스트 전체** 순회, `pxNext` 를 먼저 저장(579). ANY/ALL 판정(587–607), 맞으면 `uxBitsToClear |= uxBitsWaitedFor`(612–615) 하고 `vTaskRemoveFromUnorderedEventList( pxListItem, pxEventBits->uxEventBits | eventUNBLOCKED_DUE_TO_BIT_SET )`(626) — 깨는 태스크의 이벤트 리스트 항목 값에 **그 순간의 비트** 를 넣어 준다. 루프가 끝난 뒤 `uxEventBits &= ~uxBitsToClear`(635–637, 주석 "Clear any bits that matched when the eventCLEAR_EVENTS_ON_EXIT_BIT bit was set"). 반환은 지운 뒤의 스냅샷(640). 헤더도 "Returned value might have the bits specified by the uxBitsToSet parameter cleared" 라고 경고한다(event_groups.h:482–487).
- **wait** (`xEventGroupWaitBits`, 312–469): 이미 맞으면 지우기 전 값을 반환하고 `xClearOnExit` 면 지움(343–359), `xTicksToWait == 0` 이면 현재 값 반환(360–366), 아니면 ALL/CLEAR 를 **제어 비트** 로 바꿔(373–389) `vTaskPlaceOnUnorderedEventList( ..., uxBitsToWaitFor | uxControlBits, xTicksToWait )`(394). 이 함수는 항목 값을 저장하고(tasks.c:5446) 리스트 **끝에** 붙인다(`listINSERT_END`, tasks.c:5453) — 우선순위 정렬이 없는 이유: 어차피 맞는 대기자는 모두 깨우니까.
- **24비트의 비밀**: 대기 조건(want + ALL/CLEAR)과 결과(값 + "set 으로 깼음")를 TCB 의 `xEventListItem` 값 **하나**(TickType_t)에 넣는다. 그래서 상위 바이트가 제어용이고 — 32비트 tick 이면 `eventCLEAR_EVENTS_ON_EXIT_BIT 0x01000000`, `eventUNBLOCKED_DUE_TO_BIT_SET 0x02000000`, `eventWAIT_FOR_ALL_BITS 0x04000000`, `eventEVENT_BITS_CONTROL_BYTES 0xff000000` (event_groups.h:47–51) — 사용자 비트는 **24개**. 이 레슨의 TCB 필드 `ev_want/ev_flags/ev_value/ev_by_set` 가 그 값 하나를 풀어 쓴 것이다. 항목이 하나뿐인 이유는 RTOS-B02 의 `xStateListItem`(ready/delayed) + `xEventListItem`(대기 리스트) 두 개 중 후자를 쓰기 때문.
- **타임아웃 뒤 재검사**: 깨어나면 `uxTaskResetEventItemValue()`(421, tasks.c:7752)로 항목 값을 꺼낸다. `eventUNBLOCKED_DUE_TO_BIT_SET` 이 없으면 타임아웃 — 임계 구역에서 현재 값을 읽고 "It is possible that the event bits were updated between this task leaving the Blocked state and running again" 이면 조건을 다시 보고 clear(423–451). 마지막에 제어 비트를 떼고 반환(458). 이 레슨의 `eg_wait_result` 와 같다. 마감에 태스크를 이벤트 리스트에서 빼는 건 tick 처리다: `xTaskIncrementTick` 이 지연 리스트에서 꺼내며 `listREMOVE_ITEM( &( pxTCB->xEventListItem ) )` (tasks.c:4912–4920) = `eg_on_timeout`.
- **clear**: 임계 구역 안에서 지우기 전 값을 저장하고 지운다(472–501, 491·494). `xEventGroupGetBits()` 는 `xEventGroupClearBits( x, 0 )` 매크로다(event_groups.h:765).
- 선점: 깨운 태스크가 지금 태스크보다 높으면 `xYieldPendings[0] = pdTRUE`(tasks.c:5638) → `xTaskResumeAll()` 에서 전환. 스케줄러 잠금(suspend) 동안 리스트를 도는 이유는 대기자 수만큼 걸리는 **비결정적** 작업을 인터럽트 금지 구간에서 하지 않기 위해서다 (헤더 event_groups.h:543–550 — ISR 판이 데몬으로 미루는 이유와 같은 원리).

### Part B1 — 여러 대기자: 깨우는 순서 vs 실행 순서

```text
Part B1 multi-waiter: wakes=[W3 W1 W2] run=[W3 W2 W1 W4] W1=0x3 W2=0x3 W3=0x2 W4 rc=1 got=0 bits=0 (done 14)
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3
W1   : R B B B B B B r R . . . . .
W2   : . R B B B B B R . . . . . .
W3   : . . R B B R . . . . . . . .
W4   : . . . R B B B B B B B B B R
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part B1 실제 타임라인 — set(0x2)@5 → W3, set(0x1)@7 → W1·W2 둘 다</text>
  <text x="51" y="38" fill="currentColor">0</text> <text x="251" y="38" fill="currentColor">5</text> <text x="451" y="38" fill="currentColor">10</text>
  <text x="10" y="61" fill="currentColor" font-family="monospace">W1</text> <rect x="50" y="48" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="90" y="48" width="240" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="93" y="61" fill="currentColor">B</text> <rect x="330" y="48" width="40" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="370" y="48" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="85" fill="currentColor" font-family="monospace">W2</text> <rect x="90" y="72" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="130" y="72" width="200" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="133" y="85" fill="currentColor">B</text> <rect x="330" y="72" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="109" fill="currentColor" font-family="monospace">W3</text> <rect x="130" y="96" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="170" y="96" width="80" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="173" y="109" fill="currentColor">B</text> <rect x="250" y="96" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="133" fill="currentColor" font-family="monospace">W4</text> <rect x="170" y="120" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="210" y="120" width="360" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="213" y="133" fill="currentColor">B</text> <rect x="570" y="120" width="40" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="157" fill="currentColor" font-family="monospace">set</text> <path d="M270.0 44 L270.0 146" stroke="currentColor" stroke-dasharray="3 3"/> <text x="266.0" y="157" fill="currentColor" font-family="monospace">0x2</text> <path d="M350.0 44 L350.0 146" stroke="currentColor" stroke-dasharray="3 3"/> <text x="346.0" y="157" fill="currentColor" font-family="monospace">0x1</text>
  <rect x="10" y="172" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="183" fill="currentColor">R 실행</text> <rect x="86" y="172" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="106" y="183" fill="currentColor">r READY</text> <rect x="180" y="172" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="200" y="183" fill="currentColor">B 이벤트 그룹 대기</text> <rect x="330" y="172" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="350" y="183" fill="currentColor">d 지연(task_delay)</text>
  <text x="10" y="206" fill="currentColor">W4(ALL 0x7)는 0x4 가 안 와서 tick 13 에 타임아웃 (rc=EG_TIMEOUT, got=0).</text>
  <text x="10" y="226" fill="currentColor">깨운 순서 W1→W2 (대기 리스트 순), 실행 순서 W2→W1 (우선순위 순).</text>
</svg>
```

- tick 5 `set(0x2)`: W3(ANY 0x2) 만 맞는다. W3 은 clear 가 없으므로 bit1 은 남는다(반환 0x2).
- tick 7 `set(0x1)`: bits = 0x3 → W1(ANY 0x1) ✓, W2(ALL 0x3) ✓. W1 의 clear 가 바로 적용됐다면 W2 는 0x2 만 보고 실패했을 것이다. 둘 다 `ev_value = 0x3`, 루프 뒤 `clr = 0x1 | 0x3` → bits 0.
- **깨운 순서 ≠ 실행 순서**: 리스트에는 W1 이 먼저 있어 먼저 READY 가 되지만, CPU 는 우선순위대로 W2(p2, tick 7) → W1(p1, tick 8). 이벤트 그룹은 "누가 먼저 기다렸나" 로 줄 세우지 않는다 — **조건이 맞으면 전부** 이고, 그다음은 스케줄러 몫이다.
- W4 는 0x4 가 끝내 안 와서 tick 3 + 10 = 13 에 `EG_TIMEOUT`, `got = 0` (그 시점 bits). FreeRTOS 데모에서도 같은 순서·같은 값이 나왔다(아래).

### Part B2 — 타임아웃의 세 갈래

```text
Part B2a timeout      : rc=1 got=0x5 at tick 5 -> missing 0x2
Part B2b set@deadline : rc=0 got=0x7 at tick 5 (hook runs before timeouts)
Part B2c late set     : rc=0 got=0x3 at tick 5, by_set=0, bits after=0
tick : 0 1 2 3 4 5
M    : R B B B r R
H    : . . . . R .
```

| 경우 | 무슨 일 | 결과 |
|---|---|---|
| a. 0x2 가 끝내 안 옴 | tick 5 마감 → 리스트에서 빠짐 → 재검사 실패 | `EG_TIMEOUT`, got 0x5 → **빠진 것 = 0x7 & ~0x5 = 0x2** |
| b. 마감 tick 에 0x2 | 이 모델의 tick 순서는 "ISR(hook) → 마감 처리" | set 이 먼저 깨움 → `EG_OK`, `ev_by_set` |
| c. 마감으로 READY → 돌기 전에 H(p3) 가 0x2 | M 은 이미 리스트 밖이라 set 이 못 깨움. M 이 돌 때 재검사 | `EG_OK`, 0x3, CLEAR 적용 → bits 0 |

FreeRTOS 에서 반환값만으로 성공/실패를 판단하는 관용구가 여기서 나온다: `b = xEventGroupWaitBits(g, ALL, pdTRUE, pdTRUE, to); if ((b & ALL) == ALL) ok; else missing = ALL & ~b;`. 반환 코드가 따로 없다 — **값을 검사하라** 가 API 계약이다(event_groups.h 의 xEventGroupWaitBits 예제). 경우 c 를 FreeRTOS 데모에서도 재현했다: 마감 tick 에 더 높은 ctrl 이 `SetBits(0x2)` → 반환 0x3(아무도 안 깨움 = M 은 리스트 밖) → M 이 0x3 을 받고 지움.

### Part B3 — 경쟁 1: 확인과 지우기를 나누면 이벤트를 잃는다

```text
Part B3 lost event: buggy (get, wait, handle, clear) handled 2 of 3
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
RXbug: R B R R R R R R B B B B B B R R R R R R B B B B
Part B3 lost event: fixed (wait with clear-on-exit)  handled 3 of 3
RXfix: R B R R R R R R R R R R R B R R R R R R B B B B
Part B3 merge     : fixed, events at 2,6,7 -> handled 2 of 3 (bits do not count)
```

```svg
<svg viewBox="0 0 740 202" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part B3 실제 타임라인 — RX 이벤트 tick 2·6·14 (위: 버그판 2개 처리, 아래: 고친판 3개)</text>
  <text x="51" y="38" fill="currentColor">0</text> <text x="191" y="38" fill="currentColor">5</text> <text x="331" y="38" fill="currentColor">10</text> <text x="471" y="38" fill="currentColor">15</text> <text x="611" y="38" fill="currentColor">20</text>
  <text x="10" y="61" fill="currentColor" font-family="monospace">RXbug</text> <rect x="50" y="48" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="78" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="81" y="61" fill="currentColor">B</text> <rect x="106" y="48" width="168" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="274" y="48" width="168" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="277" y="61" fill="currentColor">B</text> <rect x="442" y="48" width="168" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="610" y="48" width="112" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="613" y="61" fill="currentColor">B</text>
  <text x="10" y="85" fill="currentColor" font-family="monospace">RXfix</text> <rect x="50" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="78" y="72" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="81" y="85" fill="currentColor">B</text> <rect x="106" y="72" width="308" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="414" y="72" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="417" y="85" fill="currentColor">B</text> <rect x="442" y="72" width="168" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="610" y="72" width="112" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="613" y="85" fill="currentColor">B</text>
  <text x="10" y="109" fill="currentColor" font-family="monospace">set</text> <path d="M120.0 44 L120.0 98" stroke="currentColor" stroke-dasharray="3 3"/> <text x="116.0" y="109" fill="currentColor" font-family="monospace">RX</text> <path d="M232.0 44 L232.0 98" stroke="currentColor" stroke-dasharray="3 3"/> <text x="228.0" y="109" fill="currentColor" font-family="monospace">RX</text> <path d="M456.0 44 L456.0 98" stroke="currentColor" stroke-dasharray="3 3"/> <text x="452.0" y="109" fill="currentColor" font-family="monospace">RX</text>
  <rect x="10" y="124" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="135" fill="currentColor">R 실행</text> <rect x="86" y="124" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="106" y="135" fill="currentColor">r READY</text> <rect x="180" y="124" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="200" y="135" fill="currentColor">B 이벤트 그룹 대기</text> <rect x="330" y="124" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="350" y="135" fill="currentColor">d 지연(task_delay)</text>
  <text x="10" y="158" fill="currentColor">버그판: tick 6 에 온 RX 를 같은 tick 의 "처리 끝 clear" 가 지운다 → tick 7–13 헛대기.</text>
  <text x="10" y="178" fill="currentColor">고친판: wait 가 깨어날 때 이미 지웠으므로 tick 6 의 RX 는 남아 있고 tick 7 에 바로 처리.</text>
</svg>
```

버그판 코드 (하네스 제공, `rx_buggy`): `if (eg_get(&g) & RX) 처리; else wait(RX, ANY, 지우지 않음); ... 처리 4 tick ...; eg_clear(&g, RX);`. 비트가 처리 내내 1 로 남아 있으니 **처리 중에 온 새 이벤트(tick 6)와 지금 처리 중인 이벤트를 구분할 수 없다** — 마지막 `clear` 가 둘 다 지운다. tick 7 의 get 은 0 을 보고 tick 14 까지 잔다.

고친판: `wait(RX, ANY | EG_CLEAR)` — 조건 확인과 지우기가 **한 번의 원자적 연산**(FreeRTOS 에서는 스케줄러 잠금 안, 343–359)이다. 깨어나는 순간 비트가 0 이 되므로 처리 중 온 tick 6 의 RX 는 **새 1** 로 남고, tick 7 의 wait 가 바로 가져간다. ISR 쪽 대응 관용구: `prev = xEventGroupClearBits(g, RX); if (prev & RX) 처리;` (지우기 전 값 = fetch-and-clear).

주의할 두 가지:

- "확인 후 기다리기(check-then-wait)" **자체** 는 이벤트 그룹에서 안전하다 — get 과 wait 사이에 온 비트는 남아 있으니 wait 가 즉시 성공한다(조건 변수에서 술어 없이 기다릴 때 생기는 lost wakeup 과 다르다, OS-B06). 위험한 것은 **소비(clear)를 확인과 분리** 하는 것이다.
- 고쳐도 `merge` 줄처럼 처리 중 **두 번** 오면 하나로 합쳐진다(2 of 3). 비트는 개수를 세지 않는다. 개수가 중요하면 카운팅 세마포어(RTOS-B09)나 큐(RTOS-B11), 또는 알림의 `eIncrement`(RTOS-M01).

### Part B4 — 경쟁 2: 남의 비트를 지우는 태스크

```text
Part B4 wrong-task clear: buggy CTRL -> ctrl=4 log ok=0 lost=1 (LOG rc=1 at 25)
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
CTRL : R R R B R R B B R R B B R R B B B B B B B B B B
LOG  : r r r R r r R R r r R R r r R R B B B B B B B B
Part B4 wrong-task clear: fixed CTRL -> ctrl=4 log ok=1 lost=0 (LOG rc=0 at 15)
```

LOGREQ 는 tick 3 에 왔지만 LOG(p1) 는 플래시 쓰기 중이라 tick 15 에야 기다리기 시작한다. 그 사이 tick 4 에 CTRL(p3) 이 IMU 이벤트를 처리하고 `eg_clear(0x00FFFFFF)` — "남은 플래그 정리" 가 **남의 이벤트** 를 지웠다. LOG 는 tick 15–24 를 기다리다 타임아웃. 고친판 CTRL 은 `wait(IMU, ANY | EG_CLEAR)` 로 **자기 비트만** 지우므로 LOG 는 tick 15 에 즉시 성공.

규칙: **비트마다 지우는 주인은 하나**. 이벤트 비트는 그 비트를 소비하는 태스크가 clear-on-exit 로, 상태 비트(C1 의 "초기화 끝남")는 아무도 지우지 않거나 상태를 바꾸는 쪽만. `clear(전부)` 로 "새로 시작" 하는 코드는 리뷰에서 막는다.

### Part C1 — BLE 센서 노드 부팅

```text
Part C1 boot: last init bit at tick 13, MAIN rc=0 got=0x7 starts tick 14 (ran 2 steps), TELEM rc=0 got=0x7, bits=0x7
Part C1 boot (radio fails): MAIN rc=1 got=0x5 at tick 30 -> missing 0x2 -> degraded mode
Part C1 boot (MAIN starts late): rc=0 at tick 20, ran 1 step(s)
```

```svg
<svg viewBox="0 0 740 274" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part C1 실제 타임라인 — init 3개 → MAIN(p4) wait-all, TELEM 은 tick 20 에 폴링</text>
  <text x="57" y="38" fill="currentColor">0</text> <text x="217" y="38" fill="currentColor">5</text> <text x="377" y="38" fill="currentColor">10</text> <text x="537" y="38" fill="currentColor">15</text> <text x="697" y="38" fill="currentColor">20</text>
  <text x="10" y="61" fill="currentColor" font-family="monospace">MAIN</text> <rect x="56" y="48" width="32" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="88" y="48" width="384" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="91" y="61" fill="currentColor">B</text> <rect x="472" y="48" width="32" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="504" y="48" width="32" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="85" fill="currentColor" font-family="monospace">SENS</text> <rect x="56" y="72" width="32" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="88" y="72" width="96" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="109" fill="currentColor" font-family="monospace">RADIO</text> <rect x="56" y="96" width="128" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="184" y="96" width="192" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="133" fill="currentColor" font-family="monospace">STORE</text> <rect x="56" y="120" width="320" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="376" y="120" width="128" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="157" fill="currentColor" font-family="monospace">TELEM</text> <rect x="696" y="144" width="32" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="181" fill="currentColor" font-family="monospace">set</text> <path d="M168.0 44 L168.0 170" stroke="currentColor" stroke-dasharray="3 3"/> <text x="164.0" y="181" fill="currentColor" font-family="monospace">0x1</text> <path d="M360.0 44 L360.0 170" stroke="currentColor" stroke-dasharray="3 3"/> <text x="356.0" y="181" fill="currentColor" font-family="monospace">0x2</text> <path d="M488.0 44 L488.0 170" stroke="currentColor" stroke-dasharray="3 3"/> <text x="484.0" y="181" fill="currentColor" font-family="monospace">0x4</text>
  <rect x="10" y="196" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="207" fill="currentColor">R 실행</text> <rect x="86" y="196" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="106" y="207" fill="currentColor">r READY</text> <rect x="180" y="196" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="200" y="207" fill="currentColor">B 이벤트 그룹 대기</text> <rect x="330" y="196" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="350" y="207" fill="currentColor">d 지연(task_delay)</text>
  <text x="10" y="230" fill="currentColor">MAIN 은 가장 높은 우선순위인데도 tick 1–12 동안 CPU 를 쓰지 않는다 (B).</text>
  <text x="10" y="250" fill="currentColor">마지막 비트(STORE, tick 13) 다음 tick 에 시작. 비트를 지우지 않으므로 TELEM 은 블록 없이 통과.</text>
</svg>
```

- MAIN(p4) 은 가장 높은 우선순위지만 tick 0 에 한 번 wait 하고 **블록** 하므로 init 태스크들이 CPU 를 쓴다(`ran 2 steps` = wait 1번 + 시작 1번). `while (!(get() == ALL)) {}` 로 폴링했다면 p4 가 CPU 를 독점해 init 이 **영원히** 못 끝난다 — 우선순위 기반 RTOS 에서 바쁜 대기는 지연이 아니라 교착이다.
- 부팅 비트는 **상태** 다: MAIN 이 clear 하면 tick 20 의 TELEM 은 영원히 기다린다. 그래서 MAIN 은 `EG_ALL` 만, clear 없이. 이벤트(B3)와 상태(C1)는 같은 API 로 정반대 규칙을 쓴다.
- 실패 경로: RADIO 가 비트를 안 세우면 MAIN 은 tick 30 에 `got = 0x5` → 빠진 비트 0x2 를 로그로 남기고 BLE 없이(degraded) 시작하거나 리셋 정책을 고른다. 타임아웃이 없으면 현장에서 "켜졌는데 아무것도 안 함" 장애가 된다.
- MAIN 이 init 보다 늦게 출발해도(tick 20) 즉시 통과 — 비트가 남아 있으므로 **순서 무관**. 세마포어나 알림으로 했다면 "누가 먼저 give 했나" 를 따져야 한다.

### Part C2 — 워치독 건강 감시

```text
Part C2 watchdog healthy: kicks=12 misses=0 reset_at=-1
Part C2 watchdog hang   : COMM hung at 28, first miss at 33 mask=0x4, kicks=5 misses=4, IWDG reset at 36
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
WDG  : R B B r R R B B r R R B B r R R B B r R R B B r R R B B B B B B B R R B B B B B
IMU  : r R d d d r R d d d r R d d d r R d d d r R d d d r R d d d R d d d r R d d d R
CTRL : r r R d d d r R d d d r R d d d r R d d d r R d d d r R d d d R d d d r R d d d
COMM : r r r R d d d r R d d d r R d d d r R d d d r R d d d r R B B B B B B B B B B B
```

- WDG 는 `wait(0x7, ALL | EG_CLEAR, 8)` — **CLEAR 가 필수**: 이번 창(window)의 alive 만 세야 한다. 안 지우면 COMM 이 한 번 set 한 비트가 영원히 남아 멈춘 COMM 을 못 잡는다.
- COMM 은 tick 23 에 마지막으로 set 하고 tick 28 에 멈춘다(`B`, 오지 않는 자원을 기다림). WDG 는 tick 25 에 시작한 대기가 tick 33 에 타임아웃 → 반환 0x3 → **빠진 비트 0x4 = COMM**. kick 을 멈추고, 마지막 kick(tick 24) + 12 = tick 36 에 IWDG 리셋. 로그에 "COMM" 을 남기고 리셋하면 현장 디버깅이 된다(리셋 원인 레지스터 + 백업 RAM 에 마스크 저장).
- 건강한 경우 kick 이 64 tick 동안 12번 — 일꾼 주기가 4 가 아니라 5 인 것은 `task_delay(4)` 가 실행 시점 기준이라 밀리기 때문(delay vs delay_until, RTOS-B06).
- 실제 STM32: kick = `IWDG->KR = 0xAAAA;`. 이 패턴의 핵심은 **하드웨어 워치독을 한 곳(WDG 태스크)에서만** 차는 것 — 아무 태스크나 차면 다른 태스크가 멈춘 걸 못 잡는다.

### FreeRTOS 데모 — 모델과 진짜 커널 비교

<!-- code: demo_freertos_eventgroup.c -->

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_eventgroup.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o /tmp/RTOS-B13_dfr && /tmp/RTOS-B13_dfr
```

실제 출력 (발췌):

```text
[ OK ] WaitBits(0x1, clear, any, 0) -> value BEFORE clear           got=0x5      model=0x5
[ OK ] WaitBits(0xB, clear, any, 0) with 0x3 set -> returns         got=0x3      model=0x3
[ OK ]   clear-on-exit cleared the whole mask 0xB                   got=0        model=0
[ OK ] SetBits(0x2) -> returns (W3 has no clear)                    got=0x2      model=0x2
[ OK ] SetBits(0x1) -> returns (W1|W2 clear after the loop)         got=0        model=0
[ OK ]   W2 got (woken although W1 clears bit0)                     got=0x3      model=0x3
[ OK ]   W4 timed out -> current bits                               got=0        model=0
       return order: W3 W2 W1 W4 (model: W3 W2 W1 W4)
[ OK ]   both waiters woke (clear-on-exit is NOT one-consumer)      got=0x2      model=0x2
[ OK ]   M timed out (Ready) but has not run yet                    got=0x1      model=0x1
[ OK ] SetBits(0x2) at deadline -> returns (M no longer waiting)    got=0x3      model=0x3
[ OK ]   M returns 0x3 via the timeout re-check path                got=0x3      model=0x3
[ OK ]   Sync() returned all rendezvous bits                        got=0x70     model=0x70
       arrive (rel) 0 1 3  leave (rel) 3 3 3
26/26 match the model
```

POSIX 포트는 한 번에 FreeRTOS 스레드 하나만 돌리므로 **순서와 반환값** 은 결정적이지만 **시간** 은 아니다. 첫 판은 "`vTaskDelay(2)` 면 대기자들이 다 돌았겠지" 를 가정했는데, 8개를 병렬로 120번 돌리자 1번 `W1 got = 0`(W1 이 아직 못 돎) → `25/26` 이 나왔다. 그래서 "Blocked 가 될 때까지 / 돌아올 때까지 1 tick 씩 확인"(`settle`, `eTaskGetState`)으로 바꿨다(B2c 도 "매 tick 깨어나 M 이 Ready 가 된 tick 에 확인" 으로). 고친 판: 8개 병렬 × 15 = 120번 + 순차 30번 모두 26/26, 순차 출력 md5 30번 동일. 병렬 부하에서는 `leave (rel)` 줄의 tick 이 1 늘 수 있고(시간), 종료 순간 stderr 에 `vPortSystemTickHandler called from non-FreeRTOS thread` 가 찍히기도 했다(exit 뒤 tick 신호, 종료 코드 0). ASan+UBSan 빌드도 26/26, 경고 0. 호스트 설정은 64비트 tick 이라 `EventBits_t` 가 64비트(사용자 비트 56개) — Cortex-M 32비트 tick 이면 24개다.

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | FreeRTOS (V11.3.2 소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `eg_init` | `xEventGroupCreate()` / `xEventGroupCreateStatic(&buf)` (86) | `k_event_init(&e)` / `K_EVENT_DEFINE` | `osEventFlagsNew(NULL)` |
| `eg_set` | `xEventGroupSetBits(g, b)` → 지운 뒤 값 (547) | `k_event_post(&e, b)` (OR) / `k_event_set` (덮어쓰기) | `osEventFlagsSet(id, b)` |
| ISR 에서 set | `xEventGroupSetBitsFromISR(g, b, &woken)` → 데몬 경유 (826) | `k_event_post` 를 ISR 에서 | `osEventFlagsSet` (ISR 가능) |
| `eg_clear` | `xEventGroupClearBits(g, b)` → 지우기 전 값 (472) | `k_event_clear(&e, b)` | `osEventFlagsClear(id, b)` → 지우기 전 값 |
| `eg_get` | `xEventGroupGetBits(g)` = ClearBits(g, 0) (h:765) | — | `osEventFlagsGet(id)` |
| `eg_wait(ANY/ALL, CLEAR, to)` | `xEventGroupWaitBits(g, b, xClearOnExit, xWaitForAllBits, to)` (312) | `k_event_wait(&e, b, reset, to)` / `k_event_wait_all` — `reset` 은 **기다리기 전** 에 지우는 옵션 | `osEventFlagsWait(id, b, osFlagsWaitAny/All [\| osFlagsNoClear], to)` — 기본이 clear-on-exit |
| `eg_sync` (연습 1) | `xEventGroupSync(g, set, wait, to)` (185) | 해당 API 없음 (조합해서) | 없음 |
| 제어 비트 상위 8 | 32비트 tick: 24 비트 (h:47–51) | 32비트 | 31 비트 (최상위 = 오류 코드) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

출력은 5. 풀이에 실은 줄 + 마지막 `38/38 passed`. 5번 실행 모두 `38/38` 이고 출력 md5 가 5번 모두 같았다(결정적). 새니타이저 (경고 0, 종료 코드 0):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 38/38
```

pthread 없는 단일 스레드 시뮬레이터라 TSan 대상이 아니다. 이 레슨의 버그(늦은 clear, 남의 비트 clear, 분리된 확인/소비)는 전부 **로직 순서** 버그라서 오라클(`H_set`)과 리스트 불변식(`list_check`)으로 잡는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| set 루프 안에서 즉시 `bits &= ~want` | 뒤 대기자가 지워진 비트를 본다 → 리스트 순서에 따라 한 명씩 놓침 (연습 4: `[W1]` / `[W2]`) | `clr \|= want;` 모아서 루프 뒤 `bits &= ~clr` |
| `wl_remove` 후 `t->next` 로 진행 | 제거가 링크를 지웠다 → 순회가 끊기거나 엉뚱한 곳으로 | `nx = t->next;` 를 먼저 |
| 이벤트를 `get` → 처리 → `clear` | 처리 중 온 같은 이벤트를 지운다 (B3: 2 of 3) | `wait(bit, ANY \| CLEAR)` 또는 `prev = clear(bit); if (prev & bit)` |
| `clear(0xFFFFFF)` 로 "새로 시작" | 남의 이벤트·상태 비트까지 지운다 (B4: LOG 타임아웃) | 자기 비트만, clear-on-exit 로 |
| 부팅/상태 비트를 clear-on-exit 로 기다림 | 두 번째 대기자(TELEM)가 영원히 못 본다 | 상태 비트는 `EG_ALL` 만, 지우지 않는다 |
| 워치독 alive 비트를 clear 없이 기다림 | 한 번 세운 비트가 남아 멈춘 태스크를 못 잡는다 | `wait(ALL_ALIVE, ALL \| CLEAR, window)` |
| `wait` 반환값을 확인 안 하고 "성공" 가정 | FreeRTOS 는 타임아웃이어도 비트 값을 돌려준다 | `if ((b & ALL) != ALL) missing = ALL & ~b;` |
| ANY + CLEAR 의 반환값을 `else if` 로 하나만 처리 | clear 는 `want` 전체를 지웠다 → 같이 온 다른 이벤트 분실 (연습 3) | 돌려받은 비트를 **전부** 처리 |
| 이벤트 그룹을 작업 분배(한 명만 처리)에 사용 | 방송이라 CLEAR 대기자 둘이 **둘 다** 깬다 (데모: 2) | 작업 분배는 큐/세마포어 (RTOS-B09/B11) |
| ISR 에서 `xEventGroupSetBits` 호출 | 스케줄러 잠금을 쓰는 태스크 전용 API | `xEventGroupSetBitsFromISR` + `portYIELD_FROM_ISR(woken)`, 데몬 큐 실패(pdFAIL) 처리 |
| 상위 8비트 사용 (`1u << 24`) | 커널 제어 비트 — `configASSERT` (event_groups.c:328, 563) | 32비트 tick 에서는 bit 0–23 만 |

## 8. 면접 Q&A

<details>
<summary>Q1. 이벤트 그룹, 카운팅 세마포어, 큐, 태스크 알림 — 각각 언제 쓰나요?</summary>

이벤트 그룹: **여러 조건의 조합**(ALL/ANY), **여러 태스크가 같은 사실을 봄**(방송), **상태 플래그**(부팅 완료), 랑데부. 개수는 안 센다. 카운팅 세마포어: 개수(이벤트 수, 자원 수), 한 명만 깨움(RTOS-B09). 큐: 데이터 + 순서(RTOS-B11). 태스크 알림: 수신자가 **한 명** 이고 wait-all 이 필요 없을 때 가장 가볍다 — 이벤트 그룹 대신 `eSetBits` + `xTaskNotifyWait` 가 되지만 wait-all·방송·랑데부는 안 된다(RTOS-M01, 한 줄 요약: "알림 = 1:1 경량 이벤트 비트, 이벤트 그룹 = 1:N 조합 조건"). 꼬리: "RAM 은?" → `EventGroup_t` 는 비트 + 리스트 하나라 작다 (Cortex-M4F 에서 28 B, RTOS-M01 실측).

</details>

<details>
<summary>Q2. set 한 번에 대기자 여럿이 깨나요? clear-on-exit 는 언제 적용되죠?</summary>

조건이 맞는 대기자는 **모두** 깬다. FreeRTOS 는 스케줄러를 잠그고 리스트 전체를 돌며(event_groups.c:577–633) 맞는 대기자를 ready 로 옮기고, 각자 그 순간의 비트 값을 받는다(626). clear-on-exit 로 지울 비트는 `uxBitsToClear` 에 모았다가 **루프가 끝난 뒤** 한 번에 지운다(637). 그래서 같은 set 에서 깬 대기자는 모두 같은 값을 받고, 어떤 대기자의 clear 가 다른 대기자의 조건을 망치지 않는다(B1: W1 ANY 0x1+CLR 와 W2 ALL 0x3 가 둘 다 깸). 즉시 지우면 리스트 순서에 따라 한 명이 놓친다(연습 4). 꼬리: "그럼 set 의 반환값은?" → 지운 **뒤** 값이라 방금 세운 비트가 0 일 수 있다(헤더 event_groups.h:482–487). 꼬리2: "set 비용은?" → 대기자 수에 비례 O(n) — 그래서 ISR 판은 데몬으로 미룬다(Q3).

</details>

<details>
<summary>Q3. ISR 에서 이벤트 비트를 세우면 무슨 일이 일어나나요?</summary>

`xEventGroupSetBitsFromISR` 는 비트를 직접 세우지 않고 `xTimerPendFunctionCallFromISR` 로 **타이머 데몬 태스크** 에 "set 해 줘" 를 보낸다(event_groups.c:826). 대기자 수만큼 걸리는 비결정적 작업을 ISR·임계 구역에서 하지 않기 위해서다(event_groups.h:543–550). 결과 ① `configUSE_TIMERS`, `INCLUDE_xTimerPendFunctionCall` 필요 ② 실제 set 은 데몬 우선순위(`configTIMER_TASK_PRIORITY`)에 따라 늦어진다 — 연습 2: 데몬 p4 면 지연 1 tick, 데몬이 p1 이면 p2 태스크 뒤에 밀려 **8 tick**(p3 처리 태스크가 p2 에 막히는 일종의 역전) ③ 타이머 커맨드 큐가 차면 pdFAIL(연습 2: 3번 중 1번) ④ `woken` 은 "데몬이 지금 태스크보다 높은가" 다. 꼬리: "지연이 싫으면?" → 수신자가 하나면 `xTaskNotifyFromISR(..., eSetBits, &woken)` 은 ISR 에서 바로 처리된다(RTOS-M01). `xEventGroupClearBitsFromISR` 도 같은 이유로 미뤄진다(514).

</details>

<details>
<summary>Q4. 이벤트를 확인하고 지우는 사이에 새 이벤트가 오면요? 어떻게 막죠?</summary>

"get → 처리 → clear" 는 처리 중에 온 같은 비트를 함께 지운다(B3: 3개 중 2개). 확인과 소비를 **하나의 원자적 연산** 으로: `xEventGroupWaitBits(g, BIT, pdTRUE, pdFALSE, to)`(조건 확인 + clear 가 스케줄러 잠금 안에서, 343–359) 또는 `prev = xEventGroupClearBits(g, BIT); if (prev & BIT) handle();`(임계 구역 안 fetch-and-clear, 485–496). 이렇게 하면 처리 중 온 이벤트는 새 1 로 남아 다음 루프에서 처리된다. 꼬리: "그래도 처리 중 두 번 오면?" → 합쳐진다(B3 merge: 2 of 3). 비트는 개수를 세지 않으니 개수가 필요하면 카운팅 세마포어/큐. 꼬리2: "get 하고 wait 하는 사이에 오면 놓치나?" → 아니다. 비트가 남아 있으니 wait 가 즉시 성공한다. 문제는 확인이 아니라 **분리된 소비** 다.

</details>

<details>
<summary>Q5. 비트는 누가 지워야 하나요? clear-on-exit 를 언제 켜고 끄죠?</summary>

비트마다 **주인(소비자) 하나**. 이벤트 비트(RX, 버튼, DRDY) → 소비하는 태스크가 clear-on-exit 로. 상태 비트(초기화 완료, 링크 연결됨) → 지우지 않는다(여러 태스크가, 늦게 와도 봐야 함 — C1 TELEM). 상태가 바뀌면(링크 끊김) 상태를 바꾸는 쪽만 clear. 워치독 alive 비트 → 감시자가 매 창마다 clear(C2). 다른 태스크의 비트를 `clear(전부)` 하는 코드는 버그다(B4). 꼬리: "ANY(A|B) + clear 로 기다리면 A 만 와도 B 도 지워지나?" → `want` 전체를 지운다(데모: 0xB 마스크 전체 clear). 같이 와 있던 B 는 반환값에 있으니 **반환값의 모든 비트를 처리** 하면 잃지 않는다(연습 3).

</details>

<details>
<summary>Q6. 랑데부(xEventGroupSync)는 왜 set 다음 wait 로 만들 수 없나요?</summary>

세 태스크가 각자 비트를 세우고 셋 다 모일 때까지 기다리는 장벽(barrier)이다. `set(me); wait(ALL, CLEAR)` 로 나누면 마지막 태스크의 set 이 먼저 와 있던 대기자들을 깨우고 **그들의 clear-on-exit 가 마지막 태스크의 비트까지 지운다** → 마지막 태스크의 wait 는 영원히 못 모인다(연습 1 naive: C 가 타임아웃, 다음 라운드엔 A·B 도). `xEventGroupSync` 는 스케줄러 잠금 안에서 set 전 값(`uxOriginalBitValue`)을 기억하고 `(orig | set) & wait == wait` 면 블록 없이 통과하고 비트를 지운다(event_groups.c:205–221, 주석 "Rendezvous always clear the bits"). 아니면 ALL|CLEAR 로 블록(231). 꼬리: "반환값은?" → 모두 모였을 때의 값(지우기 전). 타임아웃이면 일부만 — 검사해야 한다(event_groups.h:653–659).

</details>

<details>
<summary>Q7. 대기자가 블록해 있다가 타임아웃 났는데, 실제로 돌기 전에 비트가 다 세워지면요?</summary>

FreeRTOS 는 태스크가 다시 돌 때 `eventUNBLOCKED_DUE_TO_BIT_SET` 이 없으면(타임아웃) 임계 구역에서 **현재 비트로 조건을 다시 검사** 하고, 맞으면 clear-on-exit 도 적용한다(event_groups.c:423–451). API 는 성공/실패 코드 없이 값만 돌려주므로 호출자 입장에선 성공이다. B2c 와 데모가 이 경로를 확인했다(마감 tick 에 더 높은 태스크가 set → `SetBits` 반환 0x3 = 아무도 안 깸 → M 은 0x3 을 받고 지움). 꼬리: "마감 tick 에 ISR 이 set 하면?" → tick 처리와 set 의 순서에 달렸다. 이 모델은 hook 이 먼저(B2b 성공). FreeRTOS 는 ISR 판이 데몬 경유라 데몬이 언제 도느냐에 따른다 — 어느 쪽이든 재검사 덕에 "비트는 왔는데 실패" 는 태스크가 돌기 전까지 온 비트에 대해선 생기지 않는다.

</details>

## 9. 추가 연습

### 연습 1 — 랑데부 `eg_sync` (xEventGroupSync)

A(p3, 일 2 tick) · B(p2, 3 tick) · C(p1, 5 tick) 이 매 라운드 일하고 장벽에서 만난다, 2라운드. `eg_sync(g, self, set, wait, timeout, &out)` 을 구현하고, "set 따로 → 다음 step 에 wait(ALL|CLEAR)" 인 naive 판과 비교하라.

<details>
<summary>답 보기</summary>

`orig = bits; eg_set(set); if (((orig | set) & wait) == wait) { *out = orig | set; bits &= ~wait; return EG_OK; }` 아니면 `ev_flags = EG_ALL | EG_CLEAR` 로 블록. 실제 출력:

```text
ex1 eg_sync : round1 arrive A=1 B=4 C=9 leave A=10 B=13 C=9 | round2 arrive 12 16 21 leave 22 23 21 | fail=0 bits=0
ex1 naive   : set() then wait(ALL|CLEAR) -> timeouts=3, round1 leave A=12 B=16 C=-1
```

아무도 마지막 도착(9, 21) 전에 나가지 않고, 마지막 도착자 C 는 블록 없이 통과(leave = arrive). naive 판: C 의 set(tick 9)이 A·B 를 깨우며 그들의 clear 가 C 의 비트까지 지운다 → C 의 wait 는 영원히 0x4 를 못 본다(타임아웃) → 2라운드에서 A·B 도 C 를 기다리다 타임아웃(합 3). 원자성의 이유가 "선점" 이 아니라 **자기 set 이 남의 clear 를 촉발** 하는 데 있다는 점이 핵심.

</details>

### 연습 2 — ISR 의 set 은 데몬으로 미뤄진다

`eg_set_from_isr(bits)` 는 길이 2 인 커맨드 큐에 넣기만 하고(가득 차면 실패), 데몬 태스크 TMR 이 한 step 에 하나씩 `eg_set` 한다. PROC(p3) 가 DRDY 를 기다리고 BUSY(p2) 가 tick 1–8 CPU 를 쓴다. ISR 은 tick 2. 데몬 우선순위 4 vs 1 에서 PROC 의 지연은? ISR 이 tick 2·3·4 에 세 번 오면?

<details>
<summary>답 보기</summary>

```text
ex2 ISR set, daemon prio 4: PROC(p3) runs at tick 3 (ISR at 2) -> latency 1
ex2 ISR set, daemon prio 1: PROC(p3) runs at tick 10 (ISR at 2) -> latency 8
ex2 3 ISRs (ticks 2,3,4), queue len 2, daemon prio 1: fails=1, PROC woke 2 time(s)
```

데몬이 낮으면 **p3 인 PROC 가 p2 인 BUSY 에 막힌다** — 이벤트 경로에 낀 데몬의 우선순위가 사실상 PROC 의 우선순위가 된다. 그래서 `configTIMER_TASK_PRIORITY` 는 보통 높게 둔다(이 레슨 설정: `configMAX_PRIORITIES - 1`). 큐 길이 2 에 3번 → 세 번째는 pdFAIL(실제 API 도 반환값을 확인해야 한다). 큐에 들어간 둘은 PROC 가 사이에 소비해서 2번 깼다. 데몬·커맨드 큐는 RTOS-B12, 직접 처리되는 대안은 RTOS-M01 알림.

</details>

### 연습 3 — 반환값의 비트를 전부 처리하라

APP 가 `wait(BTN | UART | BATT, ANY | CLEAR)` 로 기다린다. tick 3 에 BTN 과 UART 가 같이 오고(APP 는 tick 5 에야 기다리기 시작), tick 6 에 BATT. `if/else if` 로 하나만 처리하는 핸들러와 비트 전부를 처리하는 핸들러를 비교하라.

<details>
<summary>답 보기</summary>

```text
ex3 else-if : btn=1 uart=0 batt=1 (sent 1 each)
ex3 all-bits: btn=1 uart=1 batt=1 (sent 1 each)
```

tick 5 의 wait 는 0x3 을 돌려주며 `want` 전체(0x7)를 지웠다. `else if` 는 BTN 만 처리 → UART 프레임은 **이미 지워져서** 다시 오지 않는다. 고친 판: `for (i...) if (b & (1u << i)) handle(i);`. 우선순위가 필요하면 순서만 정하고 전부 처리한다.

</details>

### 연습 4 — 틀린 set: 깨우자마자 지우기

`eg_set_immediate` 는 대기자를 깨우는 즉시 그 대기자의 `want` 를 지운다. bits 0x2 에서 W1(ANY 0x1 +CLR), W2(ALL 0x3 +CLR) 가 기다릴 때 `set(0x1)` — 리스트 순서 W1,W2 와 W2,W1 에서 각각 누가 깨나?

<details>
<summary>답 보기</summary>

```text
ex4 immediate clear: list W1,W2 -> woke [W1]; list W2,W1 -> woke [W2]
ex4 deferred clear : list W1,W2 -> woke [W1 W2]; list W2,W1 -> woke [W2 W1]
```

즉시 지우면 **먼저 평가된 쪽만** 깬다 — 대기 리스트 순서(=누가 먼저 블록했나 = 타이밍)에 따라 피해자가 바뀌는 재현 어려운 버그다. 미루면 순서와 무관하게 둘 다. FreeRTOS 가 `uxBitsToClear` 를 루프 뒤에 적용하는 이유(event_groups.c:635–637).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 8/8 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 8/8, 경고 0
```

## 10. 핵심 정리

- 이벤트 그룹 = **비트 + 순서 없는 대기 리스트**. wait 는 ANY/ALL + clear-on-exit + timeout, set 은 **맞는 대기자 전부** 를 깨운다(방송). 개수는 세지 않는다.
- clear-on-exit 는 **모든 대기자를 평가한 뒤** 한 번에 (event_groups.c:612–637). 즉시 지우면 리스트 순서에 따라 한 명이 놓친다.
- 반환값 = 조건이 맞은 순간의 값(지우기 **전**). 타임아웃도 값을 돌려주므로 `(b & ALL) == ALL` 로 검사하고, 빠진 비트 `ALL & ~b` 로 원인을 찾는다. 타임아웃 뒤 돌기 전에 맞으면 재검사로 성공.
- 이벤트는 **확인과 소비를 원자적으로**: `wait(bit, CLEAR)` 또는 `prev = clear(bit)`. get → 처리 → clear 는 처리 중 온 이벤트를 지운다.
- 비트마다 주인 하나. 이벤트 비트는 소비자가 지우고, 상태 비트(부팅 완료)는 안 지우고, 워치독 alive 는 감시자가 창마다 지운다.
- 랑데부는 `xEventGroupSync` — set 과 wait 를 나누면 자기 set 이 남의 clear 를 촉발해 막힌다.
- FreeRTOS: 32비트 tick 에서 24비트(상위 8 = 제어), ISR set 은 타이머 데몬 경유(우선순위·큐 길이 주의). 수신자 하나면 태스크 알림(RTOS-M01).

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `event_groups.c` (`EventGroup_t` 54–66, `xEventGroupSync` 185–309, `xEventGroupWaitBits` 312–469, `xEventGroupClearBits` 472–501, `xEventGroupClearBitsFromISR` 506–519, `xEventGroupSetBits` 547–647, `vEventGroupDelete` 650–697, `prvTestWaitCondition` 778–812, `xEventGroupSetBitsFromISR` 817–831), `include/event_groups.h` (제어 비트 41–57, SetBits 반환 482–487, FromISR 543–562, Sync 653–659, `xEventGroupGetBits` 765), `tasks.c` (`vTaskPlaceOnUnorderedEventList` 5431–5458, `vTaskRemoveFromUnorderedEventList` 5593–5662, `xTaskIncrementTick` 이벤트 리스트 제거 4912–4920, `uxTaskResetEventItemValue` 7752): <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS API Reference — Event Groups (`xEventGroupWaitBits`, `xEventGroupSetBitsFromISR`, `xEventGroupSync`): <https://www.freertos.org> → Documentation → API Reference → Event Groups
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Chapter "Event Groups" (task synchronization, `xEventGroupSync`): <https://www.freertos.org/Documentation/02-Kernel/07-Books-and-manual/01-RTOS_book>
- Zephyr — Events (`k_event_post`, `k_event_set`, `k_event_wait`, `k_event_wait_all`): <https://docs.zephyrproject.org/latest/kernel/services/synchronization/events.html>
- CMSIS-RTOS2 — Event Flags (`osEventFlagsNew/Set/Clear/Wait`, `osFlagsWaitAll`, `osFlagsNoClear`): <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__EventFlags.html>
- STM32 reference manual — Independent watchdog (IWDG, `IWDG_KR = 0xAAAA` reload) — 예: RM0090 (STM32F4) IWDG 장
- 관련 레슨: RTOS-B02 (TCB, xStateListItem/xEventListItem), RTOS-B06 (delay vs delay_until), RTOS-B08 (바이너리 세마포어·이벤트 합쳐짐), RTOS-B09 (카운팅 세마포어), RTOS-B11 (큐), RTOS-B12 (타이머 데몬), RTOS-M01 (태스크 알림 — 경량 이벤트 비트), OS-B06 (조건 변수와 lost wakeup), C-M14 (ISR 공유 데이터)

---
id: RTOS-M07
time: 90분
updated: 2026-11-11
---
# RTOS-M07 Tick 처리와 지연 리스트: 정렬 리스트 vs 타이머 휠

## 학습 목표

- FreeRTOS 방식 **정렬 지연 리스트**(current / overflow 두 리스트 + `xNextTaskUnblockTime` 캐시)를 직접 짜고, 왜 tick 이 대부분 **비교 1번**으로 끝나는지 연산 수(ops)로 보인다.
- **hashed timing wheel** (Varghese & Lauck 1987) 을 짜고, 슬롯 수가 2의 거듭제곱이면 tick wrap 이 **아무 처리 없이** 사라지는 이유를 설명할 수 있다.
- 두 구조가 무작위 start / cancel / tick 작업(wrap 포함)에서 64비트 brute-force 참조와 **(tick, id) 만료 순서가 완전히 같음**을 테스트로 보인다.
- 타이머 1만 개에서 tick 당 비용, 삽입·취소 비용을 결정적 ops 와 호스트 ns 로 비교하고, **언제 어느 쪽이 이기는지** (reset 폭주 vs tickless 절전) 숫자로 말할 수 있다.
- tickless idle 이 묻는 "다음 만료까지 몇 tick?" 이 정렬 리스트에선 O(1), 휠에선 O(N + n) 인 이유와 보완책(비트맵, 계층 휠)을 안다.

## 1. 문제

"tick 인터럽트에서 지연된 태스크·타이머를 어떻게 찾나요? 1만 개면요? FreeRTOS 는 왜 지연 리스트가 두 개죠? Linux 는요?" — tick ISR 의 최악 실행 시간은 모든 인터럽트의 지연(latency)에 더해지기 때문에, RTOS 면접에서 스케줄러 다음으로 자주 파고드는 자료구조 질문이다. 펌웨어 현장에서는 BLE·CAN·TCP 같은 프로토콜 스택이 연결·메시지마다 타임아웃을 걸고, 대부분은 만료 전에 **취소·재시작(reset)** 된다. 이 패턴에서 정렬 리스트는 삽입이 O(n) 이라 무너지고, 반대로 배터리 노드의 tickless 절전에서는 휠이 손해를 본다.

RTOS-B06 에서 wrap 안전 비교와 FreeRTOS 두 리스트 트릭의 **정확성**을 다뤘다 (재설명 안 함). 이 레슨은 **비용**이다: 같은 API 를 두 자료구조로 구현하고, 결과는 같고 비용만 다르다는 것을 결정적 연산 수로 보인다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 다섯 타이머 (now = fffffff0) — solution.c A1 / B1 의 실제 배치</text>
  <line x1="372" y1="34" x2="372" y2="322" stroke="currentColor" stroke-dasharray="4 4"/>
  <text x="10" y="46" fill="currentColor" font-weight="bold">정렬 리스트 (FreeRTOS tasks.c)</text>
  <text x="10" y="72" fill="currentColor" font-size="12">cur</text>
  <rect x="44" y="56" width="92" height="26" rx="4" class="f-acc" stroke="currentColor"/><text x="52" y="74" fill="currentColor" font-family="monospace" font-size="12">B fffffff5</text>
  <rect x="152" y="56" width="92" height="26" rx="4" class="f-acc" stroke="currentColor"/><text x="160" y="74" fill="currentColor" font-family="monospace" font-size="12">D fffffff5</text>
  <path d="M136 69 L152 69" stroke="currentColor"/>
  <text x="10" y="122" fill="currentColor" font-size="12">ovf</text>
  <rect x="44" y="106" width="92" height="26" rx="4" class="f-mut" stroke="currentColor"/><text x="52" y="124" fill="currentColor" font-family="monospace" font-size="12">E 00000000</text>
  <rect x="152" y="106" width="92" height="26" rx="4" class="f-mut" stroke="currentColor"/><text x="160" y="124" fill="currentColor" font-family="monospace" font-size="12">A 00000004</text>
  <rect x="260" y="106" width="96" height="26" rx="4" class="f-mut" stroke="currentColor"/><text x="268" y="124" fill="currentColor" font-family="monospace" font-size="12">C ffffffef</text>
  <path d="M136 119 L152 119 M244 119 L260 119" stroke="currentColor"/>
  <rect x="44" y="152" width="200" height="26" rx="4" class="f-warn" stroke="currentColor"/><text x="52" y="170" fill="currentColor" font-family="monospace" font-size="12">next_unblock = fffffff5</text>
  <text x="10" y="206" fill="currentColor" font-size="12">tick : now ≥ 캐시 일 때만 머리 확인 → 보통 1 op</text>
  <text x="10" y="226" fill="currentColor" font-size="12">start: 정렬 위치까지 걷기 → O(n)</text>
  <text x="10" y="246" fill="currentColor" font-size="12">cancel: 이중 연결이라 O(1)</text>
  <text x="10" y="266" fill="currentColor" font-size="12">next(): 머리 값 → O(1) (tickless 에 유리)</text>
  <text x="10" y="286" fill="currentColor" font-size="12">wrap : now 가 0 이 되면 cur ↔ ovf 포인터 교환</text>
  <text x="390" y="46" fill="currentColor" font-weight="bold">hashed wheel N=8 (slot = expiry &amp; 7)</text>
  <g font-family="monospace" font-size="12">
    <rect x="390" y="58" width="40" height="26" class="f-ok" stroke="currentColor"/><text x="405" y="76" fill="currentColor">0</text>
    <rect x="430" y="58" width="40" height="26" stroke="currentColor" fill="none"/><text x="445" y="76" fill="currentColor">1</text>
    <rect x="470" y="58" width="40" height="26" stroke="currentColor" fill="none"/><text x="485" y="76" fill="currentColor">2</text>
    <rect x="510" y="58" width="40" height="26" stroke="currentColor" fill="none"/><text x="525" y="76" fill="currentColor">3</text>
    <rect x="550" y="58" width="40" height="26" stroke="currentColor" fill="none"/><text x="565" y="76" fill="currentColor">4</text>
    <rect x="590" y="58" width="40" height="26" stroke="currentColor" fill="none"/><text x="605" y="76" fill="currentColor">5</text>
    <rect x="630" y="58" width="40" height="26" stroke="currentColor" fill="none"/><text x="645" y="76" fill="currentColor">6</text>
    <rect x="670" y="58" width="40" height="26" stroke="currentColor" fill="none"/><text x="685" y="76" fill="currentColor">7</text>
    <rect x="390" y="100" width="40" height="22" rx="3" class="f-mut" stroke="currentColor"/><text x="404" y="116" fill="currentColor">E</text>
    <rect x="550" y="100" width="40" height="22" rx="3" class="f-mut" stroke="currentColor"/><text x="564" y="116" fill="currentColor">A</text>
    <rect x="590" y="100" width="40" height="22" rx="3" class="f-acc" stroke="currentColor"/><text x="604" y="116" fill="currentColor">B</text>
    <rect x="590" y="130" width="40" height="22" rx="3" class="f-acc" stroke="currentColor"/><text x="604" y="146" fill="currentColor">D</text>
    <rect x="670" y="100" width="40" height="22" rx="3" class="f-mut" stroke="currentColor"/><text x="684" y="116" fill="currentColor">C</text>
  </g>
  <path d="M410 84 L410 100 M570 84 L570 100 M610 84 L610 100 M610 122 L610 130 M690 84 L690 100" stroke="currentColor"/>
  <text x="390" y="176" fill="currentColor" font-size="12">now &amp; 7 = 0 → 다음 tick 은 슬롯 1, 2, … 순서로 하나씩</text>
  <text x="390" y="206" fill="currentColor" font-size="12">start : 슬롯 꼬리에 붙이기 → O(1)</text>
  <text x="390" y="226" fill="currentColor" font-size="12">cancel: O(1)</text>
  <text x="390" y="246" fill="currentColor" font-size="12">tick  : 슬롯 하나를 끝까지 훑기 → O(슬롯 길이)</text>
  <text x="390" y="266" fill="currentColor" font-size="12">next(): 빈 슬롯을 세며 걷기 → O(N + n)</text>
  <text x="390" y="286" fill="currentColor" font-size="12">wrap : 8 | 2^32 이라 할 일 없음. C 는 매 바퀴 방문,</text>
  <text x="390" y="304" fill="currentColor" font-size="12">        expiry ≠ now 라 지나간다 (거의 2^32 tick 뒤 만료)</text>
</svg>
```

**모델 규칙** (solution.c 머리 주석과 같다 — 공유 라이브러리 없이 레슨 안에 다 있다):

| 항목 | 규칙 |
|---|---|
| 시간 | `uint32_t now`, `tick()` 한 번 = `now++` (= 미니 커널의 `kernel_tick()`). 시작점을 wrap 직전에 둔다 |
| `start(id, d)` | 만료 = `now + d` (mod 2^32), `d ∈ [1, 2^32−1]`. 지연 태스크라면 `wake_tick`, 소프트웨어 타이머라면 만료 시각 |
| 같은 만료 시각 | **start 한 순서(FIFO)** 로 만료 — FreeRTOS `vListInsert` 의 `<=` 규칙 (list.c:192) |
| 노드 | 침입형(intrusive) 이중 연결 노드가 자기 리스트(`owner`)를 안다 → `cancel` 은 두 구조 모두 O(1), 주어진 코드 |
| 비용 | 연산 수(ops): 상수 작업 1, 노드 방문 1, 슬롯 들여다보기 1. 결정적이라 CHECK 에 넣는다. ns 는 demo_bench.c 만 |
| 정답 | 64비트 시계를 쓰는 brute-force 참조(매 tick 전부 훑기)와 만료 열 `(tick, id)` 가 **완전히 같아야** 한다 |

### Part A — FreeRTOS 방식 정렬 리스트

- `sl_start` — **TODO 1.** `wake = now + d`. `wake < now`(넘침) 이면 overflow 리스트, 아니면 current 리스트에 정렬 삽입. current 머리가 바뀌면 캐시 `next_unblock` 갱신.
- `sl_tick` — **TODO 2.** `now++`, 0 이 되면 리스트 교환(그 순간 current 는 비어 있어야 — `KERNEL_ASSERT`). `now >= next_unblock` 일 때만 머리부터 만료 처리.
- `sl_next` — **TODO 3.** tickless 용 다음 만료까지 남은 tick, O(1).
- 테스트: 손 계산 시나리오(A1), 만료 없는 tick 500번 = **정확히 500 ops** (A2), 머리 cancel 후 stale 캐시 헛걸음 1번 (A3), 삽입 비용 오름차순 500500 / 내림차순 1000 (A4), 무작위 24 시드 × 4000 tick vs 참조 (A5).

### Part B — hashed timing wheel

- `hw_start` / `hw_tick` / `hw_next` — **TODO 4–6.** 슬롯 = `expiry & (N−1)`, 슬롯 안은 정렬하지 않고 꼬리에 붙인다. tick 은 슬롯 하나를 끝까지 훑어 `expiry == now` 인 것만 뗀다. 노드에는 **절대 만료 시각**을 저장한다 (원문 Scheme 6 의 rounds 저장 방식은 연습 1).
- 테스트: 같은 다섯 타이머가 같은 순서로 만료 (B1), 한 바퀴 tick 비용 = `N + n` (B2), `next()` 비용 (B3), N = 8 / 64 / 1024 무작위 vs 참조 (B4).

### Part C — 1만 개: 게이트웨이 수신 감시 vs 배터리 노드

- **C1 게이트웨이** (예: STM32H7 / S32G 급 CAN·Ethernet 게이트웨이): 수신 신호 10,000 개마다 "주기 × 3" 타임아웃. 신호가 올 때마다 `cancel + start` (reset), 끊긴 신호만 만료된다. wrap 을 사이에 둔 200 tick.
- **C2 배터리 BLE 센서 노드** (예: nRF52): 주기 타이머 5개 (100 / 250 / 1000 / 5000 / 60000 tick). tickless: 깰 때마다 `next()` 로 잘 시간을 묻고 `step(d−1)` + `tick()`. 600,000 tick (1 kHz 면 10분).
- 하네스가 두 구조로 같은 시나리오를 돌려 **만료 열이 같은지** 확인하고 ops 를 표로 찍는다. 그리고 `demo_bench.c` 로 1만 개 ns 측정.

## 2. Problem (English)

> **Delayed-list data structures for an RTOS tick: sorted list vs hashed timing wheel.**
>
> Implement a timer queue with `start(id, delay)` (expires at `now + delay` mod 2^32, `1 ≤ delay ≤ 2^32−1`), `cancel(id)` (given, O(1) unlink), `tick()` (advance `now` by one and return the ids that expire at the new `now`, FIFO among equal expiry times), and `next(&delta)` (ticks until the earliest expiry, for tickless idle).
>
> 1. **Sorted list, FreeRTOS style**: two lists (current / overflow). Insert into the overflow list iff `now + delay` wrapped. When `now` becomes 0, swap the lists (current must be empty — assert). Cache the earliest expiry of the current list in `next_unblock`; a tick whose `now < next_unblock` must cost exactly one comparison. `next()` must be O(1).
> 2. **Hashed timing wheel** with `N = 2^k` slots, slot = `expiry & (N−1)`, unsorted per-slot lists (append at tail). `tick()` scans one slot. `next()` may scan the wheel; it must be exact.
> 3. Both must produce exactly the same `(tick, id)` expiry sequence as a 64-bit brute-force reference over random start/cancel/tick workloads that cross the 32-bit wrap. Count operations (node visits) deterministically.
>
> Example: at `now = 0xFFFFFFF0`, start A=20, B=5, C=0xFFFFFFFF, D=5, E=16 → expiries `B@fffffff5 D@fffffff5 E@0 A@4`; C stays pending (expires at 0xFFFFFFEF of the next lap).
>
> Follow-ups: Why does FreeRTOS keep two delayed lists instead of a wrap-safe comparison? Why is a stale `xNextTaskUnblockTime` safe? Cost per tick with 10,000 timers in each structure? How do you choose N? Why is the wheel bad for tickless idle, and how do Linux / hierarchical wheels deal with long timeouts? How do you bound the worst-case tick ISR time?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 3/33 passed, exit 1
```

스텁은 "아무것도 넣지 않고 now 만 올리는" 큐다. 그래서 리스트 도우미 자가 검사(L0)와 cancel 관련 몇 개만 통과하고, 무작위 비교는 `seq mismatches=69136` 같은 숫자로 실패한다. 스텁 상태에서도 C2 루프는 `next()` 가 0 을 돌려주면 끝나므로 멈추지 않는다. 스레드가 없는 단일 스레드 시뮬레이터라 `-pthread` / TSan 은 필요 없다.

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: 어느 리스트에 넣을지는 "넘쳤나" 하나로 정한다</summary>

`uint32_t wake = now + d;` 는 넘쳐도 정의된 동작(부호 없는 산술은 mod 2^32)이다. `wake < now` 면 넘친 것 → overflow 리스트. 리스트 안에서는 평범한 `<=` 로 걷는다: "앞 노드의 expiry 가 wake 이하인 동안 지나간다" → 같은 값은 뒤로 = FIFO. 캐시는 **current 에 넣었을 때만** `min` 으로 줄인다. overflow 에 넣은 값은 다음 바퀴 값이라 지금 캐시와 비교하면 안 된다.

</details>

<details>
<summary>힌트 2 — TODO 2: 캐시는 "이 값보다 일찍은 아무것도 안 깬다" 는 하한(lower bound)</summary>

캐시는 정확할 필요가 없다. **진짜 최솟값 이하**이기만 하면 안전하다 — 작으면 헛걸음 한 번(머리 보고 캐시 고침), 크면 **만료를 놓친다**. 그래서 cancel 은 캐시를 안 고쳐도 되고, insert 는 줄이기만 하면 된다. 루프: 비었으면 캐시 = `TICK_MAX` 후 끝, 머리 expiry > now 면 캐시 = 그 값 후 끝, 아니면 떼어낸다.

</details>

<details>
<summary>힌트 3 — TODO 4–5: 휠은 비교를 "같다" 로만 한다</summary>

tick 은 모든 now 값을 하나도 빼먹지 않고 지나간다. 그러니 슬롯 `now & mask` 의 노드 중 `expiry == now` 인 것만 만료시키면 된다 — 크기 비교가 없으니 wrap 이 문제가 안 된다. 다음 바퀴 이후 노드는 그대로 둔다. 순회 중 떼어낼 거면 `next` 를 먼저 저장하라.

</details>

<details>
<summary>힌트 4 — TODO 6: 휠의 next() 는 "첫 번째 비어 있지 않은 슬롯" 이 아니다</summary>

그 슬롯의 노드가 몇 바퀴 뒤 타이머일 수 있다. k = 1, 2, … 순서로 슬롯 `(now + k) & mask` 를 보며 `expiry − now == k` 인 노드를 찾으면 그게 답이다. 한 바퀴를 다 봐도 없으면, 그동안 **모든 노드를 정확히 한 번씩** 봤으니 그중 `expiry − now` 의 최솟값이 답이다.

</details>

<details>
<summary>힌트 5 — ops 를 맞히는 법</summary>

`sl_start`: 1 + 지나간 노드 수. `sl_tick`: 1 (캐시 비교) + 머리를 볼 때마다 1. `hw_tick`: 1 (슬롯) + 방문 노드마다 1. `hw_next`: 1 + 본 슬롯마다 1 + 방문 노드마다 1. A2 는 500, B2 는 256 + 1000, B3 는 1 대 202 가 나와야 한다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 이 레슨 | FreeRTOS V11.3.2 (소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준) | Linux (문서 기준, 미검증) |
|---|---|---|---|---|
| `sl_start` | 태스크: `prvAddCurrentTaskToDelayedList` (tasks.c:8685) ← `vTaskDelay`, 큐·세마포어 타임아웃. 타이머: `prvInsertTimerInActiveList` (timers.c:892) | `z_add_timeout()` (kernel/timeout.c) | `osDelay`, `osTimerStart` | `add_timer` / `mod_timer` |
| `sl_cancel` | `listREMOVE_ITEM` (list.h:322), `uxListRemove` (list.c:217) | `z_abort_timeout()` | `osTimerStop` | `del_timer` 계열 |
| `sl_tick` | `xTaskIncrementTick` (tasks.c:4831) | `sys_clock_announce()` | (커널 내부) | timer softirq `__run_timers` |
| `next_unblock` | `xNextTaskUnblockTime` (tasks.c:513) | 타임아웃 리스트 머리 | — | `timer_base` 의 다음 만료 |
| `sl_next` | `prvGetExpectedIdleTime` (tasks.c:4002–4059) | 다음 타임아웃 조회 | — | NO_HZ `get_next_timer_interrupt` |
| `step(k)` | `vTaskStepTick` (tasks.c:4665) | `sys_clock_announce(ticks)` 여러 tick 한 번에 | — | — |
| 리스트 교환 | `taskSWITCH_DELAYED_LISTS` (tasks.c:267–279), 타이머 `prvSwitchTimerLists` (timers.c:1091) | 64비트 tick 이라 해당 없음 | — | jiffies wrap 비교 매크로 |

FreeRTOS 의 정렬 리스트는 두 곳에 있다: **태스크 지연/타임아웃** (tick ISR 이 처리) 과 **소프트웨어 타이머** (timer daemon 태스크가 처리, RTOS-B12). 둘 다 같은 `vListInsert` 정렬 삽입과 current/overflow 교환을 쓴다.

### Part A — 정렬 리스트: tick 은 싸고 삽입이 비싸다

```c
static int sl_tick(slist_t *s, uint32_t *out, int max) {
    int n = 0;
    s->now++;
    if (s->now == 0) {                                       /* taskSWITCH_DELAYED_LISTS */
        KERNEL_ASSERT(list_empty(s->cur));
        tlist_t *t = s->cur; s->cur = s->ovf; s->ovf = t;
        s->overflows++;
        s->next_unblock = list_empty(s->cur) ? TICK_MAX : list_head(s->cur)->expiry;
    }
    s->ops.tick++;
    if (s->now >= s->next_unblock) {                         /* 조기 종료: 대부분 여기서 끝 */
        for (;;) {
            if (list_empty(s->cur)) { s->next_unblock = TICK_MAX; break; }
            tnode_t *h = list_head(s->cur);
            s->ops.tick++;
            if (s->now < h->expiry) { s->next_unblock = h->expiry; break; }
            list_remove(h);
            if (n < max) out[n] = h->id;
            n++;
        }
    }
    return n;
}
```

FreeRTOS 원본과 한 줄씩 대응한다 (V11.3.2 tasks.c):

| 단계 | 소스 |
|---|---|
| `const TickType_t xConstTickCount = xTickCount + 1;` 후 `xTickCount = xConstTickCount;` | 4852–4856 |
| `if( xConstTickCount == ( TickType_t ) 0U ) { taskSWITCH_DELAYED_LISTS(); }` | 4858–4861 |
| 매크로: `configASSERT( listLIST_IS_EMPTY( pxDelayedTaskList ) )`, 포인터 교환, `xNumOfOverflows` 증가, `prvResetNextTaskUnblockTime()` | 267–279, 6621–6638 |
| 주석 "once one task has been found whose block time has not expired there is no need to look any further down the list" + `if( xConstTickCount >= xNextTaskUnblockTime )` | 4867–4871 |
| 비었으면 `xNextTaskUnblockTime = portMAX_DELAY;` | 4875–4883 |
| `if( xConstTickCount < xItemValue ) { xNextTaskUnblockTime = xItemValue; break; }` | 4897–4905 |
| `listREMOVE_ITEM( &( pxTCB->xStateListItem ) )`, 이벤트 리스트에도 있으면 거기서도 뗀다 (RTOS-M06 의 두 리스트) | 4913, 4917–4920 |

삽입 쪽은 `prvAddCurrentTaskToDelayedList`: `xTimeToWake = xConstTickCount + xTicksToWait;` (8729, 주석 "This may overflow but this doesn't matter"), `if( xTimeToWake < xConstTickCount )` 면 overflow 리스트에 `vListInsert` (8734–8739), 아니면 current 에 넣고 `if( xTimeToWake < xNextTaskUnblockTime ) xNextTaskUnblockTime = xTimeToWake;` (8746–8753). `vListInsert` 는 `pxIterator->pxNext->xItemValue <= xValueOfInsertion` 동안 걷는다 (list.c:192) — 이것이 O(n) 이고, 같은 값 뒤에 서는 FIFO 규칙이다. `portMAX_DELAY` 값만 예외로 바로 끝에 붙인다 (list.c:161).

**A1 실제 출력** — 다섯 타이머가 wrap 을 사이에 두고 어떻게 나뉘고 교환되는지:

```text
A1 now=fffffff0  cur=[ B:fffffff5 D:fffffff5 ]  ovf=[ E:00000000 A:00000004 C:ffffffef ]  next_unblock=fffffff5
A1 after tick 00000000 (swap):  cur=[ A:00000004 C:ffffffef ]  ovf=[ ]  next_unblock=00000004 overflows=1
A1 expired: B@fffffff5 D@fffffff5 E@0 A@4
```

C (`d = 0xFFFFFFFF`) 는 `fffffff0 + ffffffff = ffffffef` 로 넘쳐 overflow 에 들어갔다가, 교환 뒤 current 에서 거의 한 바퀴를 기다린다 — 두 리스트 방식은 2^32 − 1 tick 까지 표현한다 (wrap 안전 비교는 2^31 − 1, RTOS-B06 연습 3).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">sl_tick 의 길 — 회색 길이 tick 대부분 (1 op)</text>
  <rect x="10" y="44" width="90" height="34" rx="5" stroke="currentColor" fill="none"/><text x="30" y="66" fill="currentColor">now++</text>
  <path d="M100 61 L130 61" stroke="currentColor"/><path d="M124 56 L130 61 L124 66" stroke="currentColor" fill="none"/>
  <rect x="130" y="44" width="110" height="34" rx="5" stroke="currentColor" fill="none"/><text x="142" y="66" fill="currentColor">now == 0 ?</text>
  <path d="M185 78 L185 110" stroke="currentColor"/><text x="192" y="98" fill="currentColor" font-size="12">예 (2^32 tick 에 1번)</text>
  <rect x="110" y="110" width="200" height="34" rx="5" class="f-mut" stroke="currentColor"/><text x="118" y="132" fill="currentColor" font-size="12">cur ↔ ovf 교환, 캐시 = 새 머리</text>
  <path d="M240 61 L270 61" stroke="currentColor"/><path d="M264 56 L270 61 L264 66" stroke="currentColor" fill="none"/>
  <rect x="270" y="44" width="130" height="34" rx="5" class="f-acc" stroke="currentColor"/><text x="280" y="66" fill="currentColor">now ≥ 캐시 ?</text>
  <path d="M335 78 L335 186" stroke="currentColor" stroke-width="3" stroke-dasharray="6 3"/><text x="342" y="176" fill="currentColor" font-size="12">아니오</text>
  <rect x="270" y="186" width="160" height="34" rx="5" class="f-ok" stroke="currentColor"/><text x="282" y="208" fill="currentColor">끝 — 비교 1번</text>
  <path d="M400 61 L430 61" stroke="currentColor"/><path d="M424 56 L430 61 L424 66" stroke="currentColor" fill="none"/><text x="404" y="54" fill="currentColor" font-size="12">예</text>
  <rect x="430" y="44" width="130" height="34" rx="5" stroke="currentColor" fill="none"/><text x="440" y="66" fill="currentColor">머리 ≤ now ?</text>
  <path d="M560 61 L590 61" stroke="currentColor"/><path d="M584 56 L590 61 L584 66" stroke="currentColor" fill="none"/><text x="564" y="54" fill="currentColor" font-size="12">예</text>
  <rect x="590" y="44" width="140" height="34" rx="5" class="f-warn" stroke="currentColor"/><text x="600" y="66" fill="currentColor" font-size="12">떼어내 READY 로</text>
  <path d="M660 44 L660 30 L495 30 L495 44" stroke="currentColor" fill="none"/><path d="M490 38 L495 44 L500 38" stroke="currentColor" fill="none"/>
  <path d="M495 78 L495 120" stroke="currentColor"/><text x="502" y="104" fill="currentColor" font-size="12">아니오 / 비었음</text>
  <rect x="440" y="120" width="290" height="34" rx="5" stroke="currentColor" fill="none"/><text x="450" y="142" fill="currentColor" font-size="12">캐시 = 머리 expiry (비었으면 MAX), 끝</text>
  <text x="10" y="240" fill="currentColor" font-size="12">비용 = 1 + (만료된 개수 + 1). 만료가 없는 tick 은 타이머가 몇 개든 1 op — xNextTaskUnblockTime 의 존재 이유.</text>
</svg>
```

**A2 / A3 — 캐시의 의미.** 실제 출력:

```text
A2 500 idle ticks with 3 delayed: tick ops=500
A3 cancel head: stale next_unblock=10, ops at tick 10=2, total tick ops for 50 ticks=52
```

캐시는 "current 의 가장 이른 만료 **이하**" 라는 하한이다. 머리(10)를 cancel 해도 캐시는 10 그대로(stale) — tick 10 에 머리(50)를 한 번 보고 캐시를 50 으로 고친다(헛걸음 1 op). FreeRTOS 도 똑같이 한다: 이벤트로 깨어난 태스크를 지연 리스트에서 뺄 때 캐시는 "normally left unchanged, because it is automatically reset to a new value when the tick count equals xNextTaskUnblockTime" 이고, **tickless 일 때만** 바로 `prvResetNextTaskUnblockTime()` 을 부른다 — "it might be more important to enter sleep mode at the earliest possible time" (tasks.c:5532–5542). stale 캐시는 tickless 에서 **너무 일찍 깨는** 손해만 있다. 반대 방향(캐시 > 진짜 최솟값)은 만료를 놓치는 버그다 — 돌연변이 시험에서 insert 의 캐시 갱신을 지우면 A1 부터 실패했다.

이 레슨의 `sl_next` 는 캐시 대신 **두 리스트의 머리**를 본다 (둘 다 O(1), 항상 정확). FreeRTOS `prvGetExpectedIdleTime` 은 `xReturn = xNextTaskUnblockTime; xReturn -= xTickCount;` (tasks.c:4054–4055) 로 캐시를 쓰고, current 가 비면 캐시가 `portMAX_DELAY` 라 wrap 직전까지만 잔다 — 둘 다 "늦지 않게" 는 보장한다.

**A4 — 삽입 비용은 순서에 달렸다.** `ascending delays=500500  descending=1000`. 오름차순(새 값이 항상 맨 뒤)은 1000 + (0 + 1 + … + 999), 내림차순(항상 맨 앞)은 개당 1. 실제 시스템의 타임아웃은 "지금 + 고정값" 이라 **거의 항상 뒤쪽** 이다 → 정렬 리스트 최악에 가깝다 (C1 에서 reset 당 3749 노드).

**A5 — 무작위 비교.** `96000 ticks, 48941 expirations, seq mismatches=0, next() mismatches=0, invariant bad=0, asserts=0`. 지연 분포에 1–3 tick 몰림, 2^31 이상, 정확히 tick 0 / 0xFFFFFFFF 에 떨어지는 값을 섞었고, 시드 대부분이 wrap 을 지난다. 불변식: 두 리스트 정렬, current 는 전부 `> now`, overflow 는 전부 `< now`, 캐시 ≤ current 머리. 교환 시 current 가 비어 있지 않은 경우(assert) 0번.

### Part B — hashed timing wheel: 삽입은 O(1), 대신 tick 이 슬롯을 훑는다

```c
static void hw_start(wheel_t *w, uint32_t id, uint32_t delay) {
    tnode_t *x = &w->node[id];
    x->expiry = w->now + delay;
    tlist_t *l = &w->slot[x->expiry & w->mask];
    list_insert_before(l, &l->end, x);                       /* 꼬리 = FIFO */
    w->count++; w->ops.start++;
}
/* hw_tick: now++, 슬롯 now & mask 를 끝까지 훑어 expiry == now 인 것만 뗀다 */
```

**설계 선택 (문서화).** Varghese & Lauck (1987) 의 분류로 보면:

| 방식 | 슬롯 안 | 노드에 저장 | 삽입 | tick | 이 레슨 |
|---|---|---|---|---|---|
| Scheme 5 | 정렬 리스트 | 만료 시각 | O(슬롯 길이) | 머리만 보면 됨 | — |
| Scheme 6 (원문) | 정렬 안 함 | **rounds** (남은 바퀴 수) | O(1) | 슬롯 전체, 방문마다 rounds−− (쓰기) | 연습 1 |
| Scheme 6 변형 | 정렬 안 함 | **절대 만료 시각** | O(1) | 슬롯 전체, `expiry == now` 비교만 (쓰기 없음) | **Part B** |

절대 시각을 고른 이유: ① 방문할 때 쓰기가 없다 (연습 1 에서 rounds 방식은 방문의 95% 가 쓰기였다 — 캐시 라인을 더럽힌다), ② `next()` 계산에 `expiry − now` 를 바로 쓴다, ③ cancel 후 재삽입 때 rounds 를 다시 계산할 필요가 없다. 대가는 노드당 4바이트(만료 시각) — rounds 와 같은 크기다.

**왜 overflow 리스트가 필요 없나.** 슬롯 = `expiry mod N` 이고 N = 2^k 는 2^32 의 약수다. tick 은 `now` 를 하나도 빠짐없이 지나가며 슬롯 `now mod N` 을 본다. 타이머는 `now == expiry` 인 바로 그 tick 에 자기 슬롯이 방문되므로, 그 사이에 wrap 이 몇 번 끼든 상관없다. 비교도 크기(`<`)가 아니라 **같음(`==`)** 뿐이라 wrap 이 보일 자리가 없다. (N 이 2^32 의 약수가 아니어도 `now % N` 으로 슬롯을 계산하면 맞다. 틀리는 것은 슬롯 커서를 따로 `cur = (cur + 1) % N` 으로 돌리는 구현 — 7. 자주 하는 실수.)

**B1–B3 실제 출력:**

```text
B1 wheel N=8 slots: [0]E [4]A [5]BD [7]C
B1 expired: B@fffffff5 D@fffffff5 E@0 A@4
B2 N=256, 1000 far timers: insert ops=1000, one lap (256 ticks) tick ops=1256
B3 next() for one timer 200 ticks away: sorted ops=1  wheel(N=256) ops=202
```

- B1: 정렬 리스트와 **같은 열**. 슬롯 7 의 C 는 B1 의 24 tick 동안 세 번 방문되지만 `expiry ≠ now` 라 통과한다.
- B2: 한 바퀴(N tick) 동안 모든 노드가 정확히 한 번 방문된다 → `N + n`. tick 당 평균 `1 + n/N` — 이것이 휠의 tick 비용이고, **멀리 있는 타이머(긴 타임아웃)도 매 바퀴 비용을 낸다**.
- B3: `next()` 는 200 tick 뒤 하나를 찾으려고 빈 슬롯 200개를 센다. 한 바퀴보다 멀면(5000) 전체 256 슬롯 + 모든 노드를 본다 (`1 + 256 + 1`, CHECK).

**B4 — 무작위 비교** (N 이 작을수록 한 슬롯에 여러 바퀴가 섞여 어렵다):

```text
B4 wheel N=8    vs brute force: 32000 ticks, 16246 expirations, seq mismatches=0, next() mismatches=0, invariant bad=0
B4 wheel N=64   vs brute force: 32000 ticks, 16246 expirations, seq mismatches=0, next() mismatches=0, invariant bad=0
B4 wheel N=1024 vs brute force: 32000 ticks, 16246 expirations, seq mismatches=0, next() mismatches=0, invariant bad=0
```

이 테스트가 버그를 실제로 잡는지 돌연변이로 확인했다 (`/tmp` 사본): 정렬 삽입 `<=` → `<` (LIFO) 는 A1·A5·C 실패, 리스트 선택을 current 고정은 9개 실패, 휠을 머리 삽입으로 바꾸면 B4·C 실패, 휠 tick 에서 `expiry == now` 대신 "슬롯이 같으면 만료" 는 B1·B2 실패. `hw_next` 의 멈춤 조건을 `best <= N` 으로 바꾼 것은 **수학적으로 같은 조건**이라 33/33 그대로였다 (슬롯 k 에서 d ≤ N 이면 d = k 뿐).

### Part C — 1만 개: 누가 이기나 (실제 출력)

```text
C1 gateway 10000 RX-timeout timers, 200 ticks across wrap: resets=25544 expired=22
   structure      | start ops   per reset | cancel ops | tick ops  per tick | total ops
   sorted list    |    95771111    3749.3 |      25544 |      243       1.2 | 95796898
   wheel N=1024   |       25544       1.0 |      25544 |      222       1.1 | 51310
C2 battery node 5 periodic timers, tickless over 600000 ticks: wakeups=7200 fired=9130
   structure      | next ops  per wake | tick ops | start ops | total ops
   sorted list    |      7200      1.0 |    23520 |     14330 | 45050
   wheel N=256    |    622750     86.5 |    16420 |      9130 | 648300
```

두 시나리오 모두 두 구조의 만료 열(해시)이 같다 (CHECK). 차이는 비용뿐이다:

- **C1**: reset 한 번에 정렬 리스트는 평균 **3749 노드**를 걷는다 (새 만료 = now + 3P 는 리스트 뒤쪽). 휠은 1. 합계 1870 배. 흥미롭게도 **tick 비용은 둘 다 tick 당 ≈ 1** 이다 — 정렬 리스트는 캐시 덕분에, 휠은 N = 1024 > 최대 타임아웃 600 이라 슬롯마다 이번 바퀴 것만 있어서.
- **C2**: 깰 때마다 `next()` — 정렬 리스트 1 op, 휠 평균 86.5 op (다음 만료까지 빈 슬롯을 센다). 합계 14 배 휠이 손해. tick·start 는 휠이 조금 싸다 (정렬 삽입이 5개짜리 리스트를 걷는 비용 14330 − 9130 = 5200).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">총 연산 수 (로그 눈금) — solution.c Part C 실제 출력</text>
  <g stroke="currentColor" stroke-opacity="0.2"><path d="M160 34 L160 200 M260 34 L260 200 M360 34 L360 200 M460 34 L460 200 M560 34 L560 200 M660 34 L660 200"/></g>
  <g fill="currentColor" font-size="12"><text x="150" y="218">10³</text><text x="250" y="218">10⁴</text><text x="350" y="218">10⁵</text><text x="450" y="218">10⁶</text><text x="550" y="218">10⁷</text><text x="650" y="218">10⁸</text></g>
  <text x="10" y="58" fill="currentColor">C1 정렬 리스트</text>
  <rect x="160" y="44" width="498" height="22" class="f-warn" stroke="currentColor"/><text x="566" y="60" fill="currentColor" font-size="12">95,796,898</text>
  <text x="10" y="88" fill="currentColor">C1 휠 N=1024</text>
  <rect x="160" y="74" width="171" height="22" class="f-ok" stroke="currentColor"/><text x="338" y="90" fill="currentColor" font-size="12">51,310</text>
  <text x="10" y="138" fill="currentColor">C2 정렬 리스트</text>
  <rect x="160" y="124" width="165" height="22" class="f-ok" stroke="currentColor"/><text x="332" y="140" fill="currentColor" font-size="12">45,050</text>
  <text x="10" y="168" fill="currentColor">C2 휠 N=256</text>
  <rect x="160" y="154" width="281" height="22" class="f-warn" stroke="currentColor"/><text x="448" y="170" fill="currentColor" font-size="12">648,300</text>
  <text x="10" y="240" fill="currentColor" font-size="12">C1 = reset 폭주(1만 개, 대부분 만료 전 취소) → 휠이 1870배 적다.  C2 = 타이머 5개 + tickless → 정렬 리스트가 14배 적다.</text>
</svg>
```

**ns 측정 — `demo_bench.c`** (10,000 개, 각 항목 5회 중앙값):

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -O2 demo_bench.c -o demo_bench && ./demo_bench    # 약 15 초 (정렬 리스트 reset 이 대부분)
```

실제 출력 (Apple M6, Apple clang 21.0.0, `-O2`, 두 번 연속 실행 중 두 번째):

```text
clock_gettime pair overhead: 21.0 ns (loop 4.2 ms)
10,000 timers, median of 5 runs, ns per operation (host ratios only)
structure     |     insert |     cancel |  steady tick     + re-arm |    RX reset | next(), 5 tmr
sorted list   |     6908.4 |        5.2 |         67.0      19498.1 |     49448.7 |           0.4
wheel N=256   |        1.4 |        2.1 |        253.2          7.3 |         3.5 |          50.5
wheel N=1024  |        1.8 |        2.4 |         79.5          7.5 |         4.2 |          61.0
wheel N=4096  |        2.0 |        2.4 |         39.5          7.4 |         3.3 |          66.2
(checksum 201746620)
```

| 열 | 무엇을 쟀나 | 읽는 법 |
|---|---|---|
| insert | 빈 구조에 1만 개 (지연 1..10000) | 정렬 리스트 6.9 µs/개 ≈ 평균 5000 노드 × 1.4 ns. 휠은 상수 |
| cancel | 1만 개를 무작위 순서로 | 둘 다 O(1) unlink — 같다 |
| steady tick | 주기 타이머 1만 개(100..10000 tick)의 tick 처리만, 20,000 tick (wrap 포함) | 시계 읽기 ≈ 17–21 ns 포함. 휠은 tick 당 `n/N` 노드: N=256 → 39개 → 253 ns, N=4096 → 2.4개 → 40 ns. 정렬 리스트는 캐시 + 만료분만 |
| + re-arm | 만료된 타이머 재장전 1번 | 정렬 리스트 19.5 µs — 재장전 위치가 거의 맨 뒤 |
| RX reset | 1만 개 걸린 상태에서 무작위 cancel + start(3000) | 정렬 리스트 49 µs. 삽입(6.9 µs)보다 7배 느린 건 reset 을 거듭하면 리스트 순서와 메모리 순서가 어긋나 포인터 추적이 캐시 미스를 내기 때문으로 보인다 (추정, 하드웨어 카운터로 확인 안 함) |
| next(), 5 tmr | 타이머 5개 노드에서 tickless 질의 | 정렬 리스트 0.4 ns (머리 읽기), 휠 50–66 ns (빈 슬롯 100개 세기) |

첫 번째 실행에서는 같은 칸이 6139 / 18186 / 46537 ns, 휠 next 74–116 ns 였다 — 절대값은 흔들리고 **자릿수 차이**만 믿을 수 있다. Cortex-M4 (캐시 없음, 168 MHz 급) 에서는 노드 방문 하나가 수 사이클~십수 사이클이라 "reset 한 번에 5000 노드" 는 수십 µs 로, 1 kHz tick 주기(1 ms)를 갉아먹는 수준이다 (계산값, 보드에서 재지 않음).

**언제 어느 쪽인가.**

| 조건 | 정렬 리스트 (FreeRTOS) | hashed wheel |
|---|---|---|
| 대기 항목 n 이 작다 (태스크 수십 개) | ✓ 단순, RAM 작음 | 슬롯 배열 RAM 이 아깝다 |
| start / reset / cancel 이 많다 (프로토콜 타임아웃, 워치독 감시) | ✗ O(n), 그것도 대개 뒤쪽 | ✓ O(1) |
| tick 비용 | ✓ 만료 없으면 1 op (캐시) | 1 + 슬롯 길이 ≈ 1 + n/N (긴 타임아웃도 매 바퀴) |
| tickless 의 "다음 만료" | ✓ O(1) 머리 | ✗ O(N + n) — 비트맵(연습 3)이나 최솟값 별도 유지로 보완 |
| 최악 tick 시간 | 같은 tick 만료 수 k 에 비례 | k + 슬롯에 몰린 다른 바퀴 노드 (해시 몰림) |
| wrap | 리스트 교환 + assert | N \| 2^32 면 할 일 없음 |
| 긴 지연 (n/N 이 큼) | 상관없음 | 계층 휠(연습 4)로 슬롯 수를 억제 |

**tickless 와의 연결 (RTOS-A02 예고).** FreeRTOS idle 태스크는 `xExpectedIdleTime = prvGetExpectedIdleTime();` 가 `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` 이상이면 `portSUPPRESS_TICKS_AND_SLEEP( xExpectedIdleTime )` 를 부르고 (tasks.c:5989–6009), 깨면 `vTaskStepTick` 이 건너뛴 tick 을 한 번에 더한다 (4665~, `configASSERT( xUpdatedTickCount <= xNextTaskUnblockTime )` 4675). 정렬 리스트에서는 그 "잘 시간" 이 캐시 빼기 now 한 번이다. 휠을 쓰는 커널이 tickless 를 하려면 다음 만료를 따로 유지하거나 스캔 비용을 감수해야 한다.

**계층 휠 한 줄 (문서 기준, 미검증).** Varghese & Lauck 의 Scheme 7 은 시·분·초 시계처럼 여러 단의 휠을 두고, 윗단 슬롯이 돌아오면 아랫단으로 내린다(cascade) — 연습 4 에서 64×64 로 직접 짠다. Linux 커널 타이머 휠은 v4.8 (2016, Thomas Gleixner) 에서 **cascade 를 없앤** 다단 휠로 바뀌었다고 알려져 있다: 단마다 64 슬롯, 윗단으로 갈수록 슬롯 하나가 덮는 시간이 8배씩 커지고, 먼 타이머는 그 단의 해상도만큼 **늦게** 만료될 수 있다 (대부분 만료 전에 취소되는 네트워크 타임아웃에 맞춘 절충). 정밀 타이머는 별도의 hrtimer (레드블랙 트리) 가 맡는다. Zephyr 의 타임아웃 리스트는 각 노드가 앞 노드와의 차이(`dticks`)를 들고 있는 **delta list** 로 알려져 있다 (연습 2).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 33/33 passed, 약 0.7 초 (5회 출력 md5 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 33/33, 보고 없음
```

5. 풀이의 A1–C2 출력이 전부 이 실행에서 나온 것이다. 단일 스레드라 TSan 은 의미가 없다. 하네스 보호 장치: 참조는 "비활성" 인데 구조 안에 노드가 아직 남아 있으면(= 만료를 놓친 구현) 다시 넣기 전에 떼어내고 `g_double_start` 로 센다 — 틀린 구현이 리스트를 망가뜨려 하네스가 무한 루프에 빠지는 것을 막는다 (돌연변이 시험 중 실제로 겪었다).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 리스트 하나에 만료 날값으로 정렬 + `now >= wake` | wrap 너머 값이 맨 앞에 와서 즉시 깬다 (RTOS-B06) | 두 리스트 (`wake < now` → overflow) 또는 wrap 안전 비교 |
| 같은 만료 시각을 `<` 로 지나가 앞에 삽입 | 같은 tick 에 깨는 태스크 순서가 LIFO — 같은 우선순위 태스크의 실행 순서가 바뀐다 (돌연변이: A1·A5 실패) | `while (it->expiry <= wake)` (list.c:192) |
| overflow 에 넣을 때도 캐시를 `min` 으로 갱신 | 다음 바퀴 값(작은 수)이 캐시가 되어 매 tick 헛걸음 | current 에 넣을 때만 캐시 갱신 |
| current 에 넣을 때 캐시 갱신을 빠뜨림 | 캐시 > 진짜 최솟값 → 만료를 놓친다 (A1 부터 실패) | `if (wake < next_unblock) next_unblock = wake;` |
| cancel 할 때마다 캐시를 정확히 고치려고 리스트를 다시 훑음 | 불필요한 비용. stale(작은) 캐시는 안전하다 | 그대로 두기 — tickless 일 때만 머리로 재설정 (tasks.c:5532–5542) |
| 휠 tick 에서 슬롯의 노드를 전부 만료 | 다음 바퀴 이후 타이머가 N tick 일찍 깬다 (B1·B2 실패) | `if (it->expiry == now)` |
| 휠 `next()` = 첫 번째 비어 있지 않은 슬롯 | 그 슬롯 노드가 몇 바퀴 뒤일 수 있다 → 엉뚱한 값 | `expiry − now == k` 확인 + 한 바퀴 후 전체 최솟값 |
| 슬롯 커서를 `cur = (cur + 1) % N` 로 따로 돌리고 N 이 2의 거듭제곱이 아님 | wrap 에서 `now % N` 과 커서가 어긋난다 (2^32 mod N ≠ 0) | 슬롯을 항상 `now & (N−1)` 로, N = 2^k |
| 순회 중 `list_remove(it)` 후 `it = it->next` | 떼어낸 노드의 next 를 따라간다 (이 모델은 NULL 로 지워서 바로 터진다) | `nx = it->next` 를 먼저 저장 |
| tick ISR 에서 타이머 콜백까지 실행 | 콜백 길이만큼 ISR 이 길어지고 모든 IRQ 지연에 더해진다 | FreeRTOS 처럼 tick 은 태스크만 READY 로, 콜백은 timer daemon 태스크에서 (RTOS-B12) |

## 8. 면접 Q&A

<details>
<summary>Q1. FreeRTOS 는 왜 지연 리스트가 두 개인가요? 교환할 때 current 가 비어 있어야 하는 이유는?</summary>

삽입할 때 `xTimeToWake < xConstTickCount` (넘침) 이면 overflow 리스트, 아니면 current 리스트에 넣는다 (tasks.c:8729–8746). 그러면 각 리스트 안에서는 평범한 `<=` 정렬과 `>=` 비교로 충분하고, 지연 범위가 2^32 − 1 tick 까지 된다 (wrap 안전 비교는 2^31 − 1). tick 이 0 이 되면 `taskSWITCH_DELAYED_LISTS()` 로 포인터만 바꾼다 (4858–4861, 267–279). current 에는 "이번 바퀴(≤ 0xFFFFFFFF)" 만 있으니 tick 이 0xFFFFFFFF 를 지나는 순간 모두 깼어야 한다 → `configASSERT( listLIST_IS_EMPTY( pxDelayedTaskList ) )`. 비어 있지 않으면 tick 을 빼먹었거나(스케줄러 정지 중 pended tick 처리 버그) 리스트가 깨진 것이다. 소프트웨어 타이머도 같은 트릭인데, timer daemon 은 tick 마다 돌지 않으므로 `prvSampleTimeNow` 가 `xTimeNow < xLastTime` 로 wrap 을 감지하고 (timers.c:876), `prvSwitchTimerLists` 가 **남은 current 타이머를 먼저 처리한 뒤** 교환한다 (1100–1112).

</details>

<details>
<summary>Q2. xNextTaskUnblockTime 은 뭐고, 왜 stale 해도 괜찮나요?</summary>

current 지연 리스트 머리의 wake 시각 캐시다. tick ISR 은 `if( xConstTickCount >= xNextTaskUnblockTime )` 일 때만 리스트를 연다 (tasks.c:4871) → 만료가 없는 tick 은 대기 태스크가 몇 개든 비교 1번 (A2: 500 tick = 500 ops). 캐시는 "진짜 최솟값 **이하**" 이기만 하면 안전한 하한이다. 머리 태스크가 이벤트로 먼저 깨서 빠지면 캐시는 더 작은 옛 값으로 남고, 그 tick 에 헛걸음 한 번으로 고쳐진다 (A3: tick 10 에 2 ops). FreeRTOS 는 tickless 가 켜져 있을 때만 바로 재설정한다 — 옛 값이면 너무 일찍 깨서 전력을 버리기 때문 (5532–5542). 꼬리: "캐시가 진짜보다 크면?" → 만료를 놓친다. 그래서 삽입은 `min` 으로만 줄이고, 키우는 건 tick 루프가 머리를 직접 보고 할 때뿐이다.

</details>

<details>
<summary>Q3. 타이머 1만 개면 tick 당 비용은? 정렬 리스트와 타이머 휠을 비교해 보세요.</summary>

정렬 리스트: tick O(1) + 만료 수 (캐시), 삽입 O(n), 취소 O(1), 다음 만료 O(1). 휠(N 슬롯): tick O(1 + n/N) + 만료, 삽입 O(1), 취소 O(1), 다음 만료 O(N + n). 이 레슨 C1 (1만 개, reset 위주): 정렬 리스트 reset 당 3749 노드, 총 9580만 ops vs 휠 5만 ops. 호스트 실측 reset 49 µs vs 3.5 ns. 반대로 C2 (타이머 5개 + tickless): 정렬 리스트 4.5만 vs 휠 65만 ops. 결론: **대기 항목이 적고 tickless 가 중요하면 정렬 리스트, 타임아웃이 많고 대부분 만료 전에 취소되면 휠**. RTOS 커널의 태스크 지연은 태스크 수가 작아 정렬 리스트가 맞고, TCP/BLE 연결 타임아웃 같은 대량 타이머는 별도 휠을 두는 게 흔한 설계다.

</details>

<details>
<summary>Q4. 휠의 슬롯 수 N 은 어떻게 정하나요?</summary>

① 2의 거듭제곱 — 슬롯 = `expiry & (N−1)`, 나눗셈 없음, wrap 자동. ② tick 비용 ≈ 1 + n/N 이므로 "tick 당 몇 노드까지 허용?" 에서 역산한다 (bench: n = 1만, N=256 → 39 노드 253 ns, N=4096 → 2.4 노드 40 ns). ③ 흔한 타임아웃이 N 보다 짧으면 노드가 한 바퀴 안에 끝나 낭비 방문이 없다 (C1: 최대 600 < 1024 → tick 당 1.1 op). ④ RAM: 슬롯마다 리스트 헤더. 단일 연결 머리 포인터면 Cortex-M 에서 4 B × N — N=1024 면 4 KB (계산값). ⑤ 긴 타임아웃이 섞이면 N 을 키우는 대신 계층 휠이나 "먼 것은 별도 정렬 리스트" 로 나눈다. 꼬리: "해시 몰림?" → 같은 주기·같은 시작 tick 의 타이머들이 한 슬롯에 몰려 그 tick 만 길어진다 — 최악 tick 시간은 평균이 아니라 최대 슬롯 길이로 본다.

</details>

<details>
<summary>Q5. 휠은 왜 tickless idle 과 궁합이 나쁜가요? 어떻게 보완하나요?</summary>

tickless 는 잠들기 전 "다음 만료까지 몇 tick?" 을 묻는다. 정렬 리스트는 머리(또는 캐시) 하나라 O(1). 휠은 슬롯에 정렬이 없어서 빈 슬롯을 세며 걸어야 하고, 한 바퀴 안에 없으면 모든 노드를 봐야 한다 (B3: 200 tick 뒤 하나 찾는 데 202 ops, C2: 깰 때마다 평균 86.5). 보완: ① 점유 비트맵 + CTZ 로 빈 슬롯 건너뛰기 (연습 3: 16290 → 2000 ops), ② 계층 휠로 슬롯 수 자체를 줄이기, ③ 최솟값을 따로 유지 (삽입 때 min, 만료·취소 때 재계산 필요 — 취소가 많으면 결국 다시 스캔), ④ 정밀도를 포기하고 "가장 가까운 비어 있지 않은 슬롯" 까지만 잔다 (몇 바퀴 뒤 노드면 한 번 깨서 다시 잔다 — 안전하지만 덜 잔다).

</details>

<details>
<summary>Q6. Linux 커널 타이머는 어떻게 되어 있나요? (문서 기준)</summary>

일반 타이머(`timer_list`, jiffies 해상도)는 다단 timing wheel 이다. v4.8 (2016) 의 재작성 이후 단마다 64 슬롯, 윗단일수록 슬롯 하나가 8배 긴 시간을 덮고, **cascade 를 하지 않는다** — 대신 먼 타이머는 그 단의 해상도만큼 늦게 만료될 수 있다. 대부분의 커널 타이머(네트워크 재전송·타임아웃)는 만료 전에 취소되므로 삽입·취소 O(1) 이 정밀도보다 중요하다는 판단으로 알려져 있다. 나노초 정밀도가 필요한 것은 hrtimer 가 레드블랙 트리로 관리하고 다음 만료를 하드웨어 one-shot 타이머에 건다. 이 레슨에서 직접 확인한 것은 아니므로 세부 숫자는 커널 소스(kernel/time/timer.c 머리 주석)로 확인하라. 꼬리: "RTOS 에서 jiffies wrap 은?" → `time_after()` 매크로 = wrap 안전 비교 (RTOS-B06).

</details>

<details>
<summary>Q7. tick ISR 의 최악 실행 시간을 어떻게 묶나요?</summary>

tick 이 하는 일 = "만료된 것들을 READY 로" 이므로 최악은 **같은 tick 에 만료되는 수 k** 에 비례한다 (정렬 리스트: 1 + k + 1, 휠: 1 + 슬롯 길이). ① 콜백을 ISR 에서 돌리지 않는다 — FreeRTOS tick 은 태스크를 ready 리스트로 옮기기만 하고, 소프트웨어 타이머 콜백은 timer daemon 태스크가 돈다 (timers.c, RTOS-B12). ② 주기 태스크들의 위상을 엇갈리게(offset) 해서 같은 tick 몰림을 줄인다. ③ 휠은 해시 몰림·긴 타임아웃 방문까지 포함해 최대 슬롯 길이를 측정한다. ④ 삽입 O(n) 은 tick ISR 이 아니라 호출한 태스크(또는 daemon)에서 일어나게 한다 — `xTimerStartFromISR` 도 `xQueueSendToBackFromISR( xTimerQueue, ... )` 로 명령을 넣을 뿐 (timers.c:525) 정렬 삽입은 daemon 이 한다. ⑤ 측정: tick ISR 진입/종료에 GPIO 토글 + 로직 분석기, 또는 DWT->CYCCNT (RTOS-A03).

</details>

<details>
<summary>Q8. 최소 힙(min-heap)은요? 정렬 리스트와 휠 사이 어디쯤인가요?</summary>

배열 이진 힙: 삽입 O(log n), 최솟값 보기 O(1), 최솟값 빼기 O(log n), 임의 취소는 노드가 자기 힙 인덱스를 들고 있으면 O(log n). 1만 개면 log₂ ≈ 14 단계라 정렬 리스트의 수천 노드보다 훨씬 낫고, 다음 만료가 O(1) 이라 tickless 에도 좋다. 대가: 같은 만료 시각의 FIFO 가 자동으로 안 지켜진다 (시퀀스 번호를 키에 더해야 함), 배열 크기 상한(정적 할당), 최악 비교 수가 O(log n) 로 휠의 O(1) 보다 크다. 펌웨어에서는 "타이머 수 상한이 정해진 대량 타임아웃 + tickless" 일 때 고려할 만하다. 꼬리: "레드블랙 트리는?" → 순서 순회·범위 삭제가 필요할 때 (hrtimer 가 그 예로 알려져 있다).

</details>

## 9. 추가 연습

### 연습 1 — Varghese–Lauck Scheme 6 원문: rounds 저장

노드에 절대 만료 시각 대신 "첫 방문 이후 더 돌아야 할 바퀴 수" `rounds = (d − 1) >> log2(N)` 을 저장하고, 방문 때 0 이면 만료, 아니면 감소시키는 휠을 짜라 (N = 8). 참조와 같은 열인가? 방문 중 쓰기는 얼마나 되나?

<details>
<summary>답 보기</summary>

```text
ex1 rounds wheel N=8: 16118 expirations, mismatches=0, slot visits=294595, rounds writes=278477 (95% of visits)
```

첫 방문은 `((d − 1) mod N) + 1` tick 뒤, 그 뒤로 N tick 마다 — 그래서 `d − 1` 을 N 으로 나눈 몫이다 (d = 8 → 0, d = 9 → 1, d = 17 → 2, CHECK). 같은 열이 나오지만 방문의 95% 가 쓰기다 (N = 8 에 긴 지연이 섞여서). 쓰기는 캐시 라인을 더럽히고, 여러 코어가 휠을 공유하면 캐시 일관성 트래픽이 된다. 절대 시각 저장은 읽기만 한다. rounds 의 장점은 tick 카운터 폭과 무관(상대값)하다는 것 — 16비트 tick 에서도 노드 필드는 작게 유지된다.

</details>

### 연습 2 — delta list (Zephyr 방식으로 알려진 구조)

각 노드가 "앞 노드보다 몇 tick 뒤" 만 저장하는 정렬 리스트를 짜라. tick 은 머리의 delta 만 1 줄이고 0 이 된 것들을 뗀다. 삽입은 delta 를 빼며 걷고, 취소는 자기 delta 를 뒤 노드에 돌려줘야 한다.

<details>
<summary>답 보기</summary>

```text
ex2 delta list: 14980 expirations, mismatches (order + next) =0
ex2 deltas after start(10,4,10,15): 1:4 0:6 2:0 3:5
ex2 after cancel(id0)        : 1:4 2:6 3:5
```

절대 시각이 없으니 **wrap 자체가 없다** — 리스트 교환도, wrap 안전 비교도 필요 없다 (지연 범위는 delta 합이 32비트에 들어가는 한). 같은 시각은 `d >= it->delta` 로 지나가 delta 0 으로 뒤에 선다 (FIFO). 다음 만료 = 머리 delta, O(1) — tickless 에 좋다. 삽입은 여전히 O(n). 취소에서 `next->delta += my_delta` 를 빠뜨리면 뒤 노드가 전부 당겨진다 (돌연변이로 확인: 9/11). tickless 로 k tick 을 건너뛰면 머리부터 k 를 빼 나가면 된다.

</details>

### 연습 3 — 점유 비트맵으로 휠의 next() 빠르게

64 슬롯 휠에 `uint64_t occ` (비어 있지 않은 슬롯 = 1) 를 두고, `next()` 가 비트맵을 `now + 1` 기준으로 회전한 뒤 CTZ 로 비어 있지 않은 슬롯만 보게 하라 (RTOS-B04 의 우선순위 비트맵과 같은 생각, CTZ 는 내장 함수 없이 de Bruijn 곱셈). 주기 50 / 60 / 1000 타이머 3개 tickless 500 회 깨기에서 ops 를 비교하라.

<details>
<summary>답 보기</summary>

```text
ex3 64-slot wheel +/- bitmap: 20250 expirations, mismatches=0
ex3 tickless 500 wakeups: next() ops linear scan=16290  bitmap=2000
```

8배 적다. 비트 하나 = 슬롯 하나이고 비어 있는 슬롯은 건너뛰므로 비용은 "비어 있지 않은 슬롯 수 + 그 노드들" 이다. 함정 둘: cancel·tick 에서 슬롯이 비면 비트를 꺼야 하고, 회전 양이 0 일 때 `x << 64` 는 정의되지 않은 동작(C11 6.5.7)이라 따로 처리한다. 슬롯이 64개를 넘으면 2단 비트맵(64 × 64)으로 확장한다 — 우선순위 비트맵을 32개 넘게 쓰는 커널과 같은 방법.

</details>

### 연습 4 — 2단 계층 휠 64 × 64 (cascade)

1단: 지금 64-tick 블록 안의 만료 (슬롯 = `expiry & 63`). 2단: 다른 블록 (슬롯 = `(expiry >> 6) & 63`). 새 블록이 시작되는 tick(`(now & 63) == 0`)에 2단 슬롯 하나를 1단으로 내린다. 지연 ≤ 4032 를 128 슬롯으로 처리하라. 같은 만료 시각의 FIFO 가 지켜지나?

<details>
<summary>답 보기</summary>

```text
ex4 2-level wheel 64x64: 31841 expirations, mismatches=0, cascaded nodes=13134, tick ops/tick=2.25
```

4032 tick 범위를 4032 슬롯 대신 128 슬롯으로, 그리고 1단 슬롯의 노드는 **전부 지금 만료**라 다른 바퀴 노드를 지나칠 일이 없다 (tick ops = 1 + 만료 + cascade 이동). 4032 인 이유: 2단은 앞으로 63 블록까지만 구분한다 — now 가 블록 끝(63)에 있으면 최대 `64 × 64 − 64 = 4032`. FIFO: 2단에 들어간 노드는 그 블록이 시작되기 **전에** start 됐고, 1단에 바로 들어가는 노드는 블록이 시작된 **뒤에** start 되므로, cascade 할 때 1단 슬롯 **꼬리** 에 붙이면 순서가 유지된다. 머리에 붙이면 깨진다 (돌연변이로 확인: 10/11). Linux 의 v4.8 이후 휠은 이 cascade 이동 비용을 없애는 대신 정밀도를 포기한 쪽으로 알려져 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 11/11 passed, 0.05 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 11/11, 보고 없음
```

## 10. 핵심 정리

- tick 처리 비용은 "지연 항목을 어떤 자료구조에 두나" 로 정해진다. 결과(만료 열)는 같고 비용만 다르다 — 64비트 brute-force 참조와 열을 비교해 검증한다.
- FreeRTOS 정렬 리스트: 삽입 O(n) (`vListInsert` `<=` = FIFO), tick 은 `xNextTaskUnblockTime` 캐시로 만료 없으면 **비교 1번** (tasks.c:4871), 다음 만료 O(1) → tickless 에 유리.
- 캐시는 하한: stale(작은) 값은 헛걸음 1번이라 안전, 큰 값은 만료 누락. cancel 은 캐시를 안 고친다 (tickless 일 때만 재설정, tasks.c:5532).
- current/overflow 두 리스트: `wake < now` 면 overflow, tick 0 에 교환 + current 비었나 assert. 지연 범위 2^32 − 1.
- hashed wheel: 슬롯 = `expiry & (N−1)`, 꼬리 삽입 O(1), tick = 슬롯 하나 훑기 ≈ 1 + n/N, `expiry == now` 비교뿐이라 N | 2^32 면 wrap 처리가 없다. 다음 만료는 O(N + n).
- 1만 개 reset 폭주: 정렬 리스트 reset 당 3749 노드 vs 휠 1 (총 1870 배). 타이머 5개 + tickless: 정렬 리스트가 14 배 적다. → 태스크 지연은 정렬 리스트, 대량 프로토콜 타임아웃은 휠.
- 휠 보완: 점유 비트맵 + CTZ (next 8배↓), 계층 휠 (cascade, Varghese–Lauck Scheme 7), Linux 는 cascade 없는 다단 휠 + hrtimer 트리 (문서 기준).
- tick ISR 최악 시간 = 같은 tick 만료 수 + (휠) 최대 슬롯 길이. 콜백은 ISR 밖 (timer daemon).

## 참고자료

- G. Varghese, A. Lauck, "Hashed and Hierarchical Timing Wheels: Data Structures for the Efficient Implementation of a Timer Facility", Proc. 11th ACM Symposium on Operating Systems Principles (SOSP), 1987 — Scheme 1–7 (정렬 리스트, 기본 휠, 해시 휠 정렬/비정렬, 계층 휠). 확장판: IEEE/ACM Transactions on Networking 5(6), 1997
- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`) — tasks.c: `taskSWITCH_DELAYED_LISTS` 265–279, 리스트 포인터 481–482, `xNumOfOverflows` 511, `xNextTaskUnblockTime` 513, `prvGetExpectedIdleTime` 4002–4059, `vTaskStepTick` 4665, `xTaskIncrementTick` 4831–4920, 이벤트 해제 시 tickless 재설정 5532–5542, idle 의 `portSUPPRESS_TICKS_AND_SLEEP` 5989–6009, `prvResetNextTaskUnblockTime` 6621–6638, `prvAddCurrentTaskToDelayedList` 8685–8753; list.c: `vListInsert` 139–212 (`portMAX_DELAY` 161, `<=` 루프 192), `uxListRemove` 217; include/list.h: `listREMOVE_ITEM` 322; timers.c: 타이머 리스트 145–146, `prvGetNextExpireTime` 842, `prvSampleTimeNow` 869–888, `prvInsertTimerInActiveList` 892–932, `prvSwitchTimerLists` 1091–1113 — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS 문서 — Low Power Support / Tickless Idle Mode: <https://www.freertos.org/low-power-tickless-rtos.html>
- Linux kernel/time/timer.c 머리 주석 (다단 휠, 단별 해상도) 과 Thomas Gleixner, "timer: Switch to a non-cascading wheel" (2016, v4.8) — <https://github.com/torvalds/linux/blob/master/kernel/time/timer.c>; hrtimers 문서 <https://docs.kernel.org/timers/hrtimers.html> (이 레슨에서 소스로 확인하지 않음)
- Zephyr — Kernel Timing / timeout 구현 (kernel/timeout.c, delta list): <https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html> (문서 기준, 미검증)
- CMSIS-RTOS2 — Timer Management / Generic Wait Functions: <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__TimerMgmt.html>
- 관련 레슨: RTOS-B06 (wrap 안전 비교, 두 리스트 트릭의 정확성), RTOS-B12 (소프트웨어 타이머, timer daemon), RTOS-B04 (비트맵 + CLZ), RTOS-M06 (상태 리스트 + 이벤트 리스트 두 노드), RTOS-A02 (tickless idle), RTOS-A03 (WCET 측정), DS 트랙 (연결 리스트, 힙)

---
id: RTOS-M06
time: 90분
updated: 2026-11-11
---
# RTOS-M06 블로킹 큐 구현: 대기 리스트와 타임아웃

## 학습 목표

- 고정 크기 메시지 큐를 **대기 리스트 2개**(보내기 대기 = `xTasksWaitingToSend`, 받기 대기 = `xTasksWaitingToReceive`)와 **지연 리스트 1개**로 직접 만들고, 각 대기 리스트를 "prio 높은 순, 같으면 먼저 온 순" 으로 유지하는 방법(`MAX_PRIO - prio` 오름차순 + 같은 값 뒤에 삽입)을 설명할 수 있다.
- TCB 에 리스트 노드가 **왜 2개**인지(`xStateListItem` + `xEventListItem`) 말하고, 깨울 때·타임아웃 때·정지/삭제 때 **두 리스트 모두에서** 빼야 하는 이유를 버그 재현으로 보인다.
- FreeRTOS `xQueueReceive` 의 `for( ; ; )` **재시도 루프**를 그대로 구현한다: 깨어났다고 데이터가 내 것이 아니다 → 다시 확인 → 없으면 **남은** 타임아웃(`xTaskCheckForTimeOut`)으로 다시 잔다. 마감 시각은 절대 뒤로 밀리지 않는다.
- "타임아웃과 give 가 같은 tick" 의 규칙을 정의하고(깨어난 태스크가 실제로 돌 때 데이터가 있으면 데이터가 이긴다) 모델과 진짜 FreeRTOS 에서 같은 결과를 확인한다.
- 매 tick 불변식(대기 리스트의 모든 태스크는 BLOCKED, 정렬·FIFO, `count ∈ [0, len]`, 잃어버린 깨움 없음, 마감 불변)을 무작위 300 세트로 검사하고, 돌연변이(mutation)로 그 검사가 진짜 버그를 잡는지 확인한다.

## 1. 문제

"큐가 비어 있을 때 `xQueueReceive(q, &v, 100)` 를 부르면 커널 안에서 무슨 일이 일어나죠? 깨어났는데 데이터가 없으면요? 타임아웃이 100 tick 인데 중간에 한 번 깨어났다 다시 자면 총 몇 tick 기다리나요?" — 큐는 RTOS 면접에서 세마포어·뮤텍스보다 자주 나오고, 꼬리질문은 거의 항상 **대기 리스트와 타임아웃의 내부** 로 내려간다. 펌웨어 현장에서 "가끔 타임아웃이 두 배로 걸린다", "데이터가 큐에 있는데 소비자가 안 깬다", "삭제한 태스크가 큐에서 깨어나 HardFault" 같은 버그가 전부 이 내부를 잘못 이해해서 생긴다.

RTOS-B09 에서 대기 리스트가 있는 세마포어를, RTOS-B11 에서 복사 큐(방향별 대기자 1명)를 만들었다. 이 레슨은 그 둘을 합쳐 **진짜 RTOS 큐의 커널 쪽** 을 만든다: 대기자가 여러 명, 우선순위 순서, 타임아웃이 있는 블록, 그리고 FreeRTOS 가 쓰는 "넘겨주지 않고 깨우기(barging, B09 연습 3) + 재시도 루프".

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">TCB 하나에 리스트 노드 둘 — 큐를 기다리는 태스크는 두 리스트에 동시에 들어 있다</text>
  <rect x="20" y="40" width="200" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="34" y="62" fill="currentColor" font-weight="bold">TCB  L (prio 1)</text>
  <text x="34" y="82" fill="currentColor" font-size="12">state = BLOCKED, wait_obj = q</text>
  <rect x="34" y="94" width="172" height="38" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="42" y="110" fill="currentColor" font-size="12">state_node (xStateListItem)</text>
  <text x="42" y="126" fill="currentColor" font-size="12">value = 깰 tick = 10</text>
  <rect x="34" y="140" width="172" height="38" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="42" y="156" fill="currentColor" font-size="12">event_node (xEventListItem)</text>
  <text x="42" y="172" fill="currentColor" font-size="12">value = MAX_PRIO - prio = 7</text>
  <rect x="270" y="40" width="450" height="118" rx="6" stroke="currentColor" fill="none"/>
  <text x="284" y="60" fill="currentColor" font-weight="bold">큐 q (len 2, count 0)</text>
  <text x="284" y="86" fill="currentColor" font-size="12">rx_wait (오름차순):</text>
  <rect x="410" y="72" width="62" height="24" class="f-warn" stroke="currentColor"/><text x="416" y="89" fill="currentColor" font-size="12">R2 · 5</text>
  <rect x="478" y="72" width="62" height="24" class="f-warn" stroke="currentColor"/><text x="484" y="89" fill="currentColor" font-size="12">R4 · 5</text>
  <rect x="546" y="72" width="62" height="24" class="f-warn" stroke="currentColor"/><text x="552" y="89" fill="currentColor" font-size="12">R3 · 6</text>
  <rect x="614" y="72" width="62" height="24" class="f-warn" stroke="currentColor"/><text x="622" y="89" fill="currentColor" font-size="12">L · 7</text>
  <text x="284" y="124" fill="currentColor" font-size="12">tx_wait:</text>
  <text x="410" y="124" fill="currentColor" font-size="12">(비어 있음 — 꽉 찼을 때 보내는 쪽이 여기 선다)</text>
  <text x="284" y="146" fill="currentColor" font-size="12">send → 맨 앞(R2) 1명만 깨운다. 아이템을 쥐여 주지는 않는다</text>
  <rect x="270" y="190" width="450" height="64" rx="6" stroke="currentColor" fill="none"/>
  <text x="284" y="210" fill="currentColor" font-weight="bold">지연 리스트 (pxDelayedTaskList, 깰 tick 오름차순)</text>
  <rect x="284" y="220" width="70" height="24" class="f-ok" stroke="currentColor"/><text x="292" y="237" fill="currentColor" font-size="12">A · 7</text>
  <rect x="360" y="220" width="70" height="24" class="f-ok" stroke="currentColor"/><text x="368" y="237" fill="currentColor" font-size="12">L · 10</text>
  <rect x="436" y="220" width="70" height="24" class="f-ok" stroke="currentColor"/><text x="444" y="237" fill="currentColor" font-size="12">R3 · 12</text>
  <text x="520" y="237" fill="currentColor" font-size="12">(FOREVER 대기자는 없음)</text>
  <path d="M206 160 L640 100" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M206 114 L392 220" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="20" y="282" fill="currentColor">데이터로 깸 (send): event_node 를 빼고 state_node 도 뺀다 → READY  (tasks.c:5525, 5529)</text>
  <text x="20" y="302" fill="currentColor">시간으로 깸 (tick): state_node 를 빼고 event_node 도 뺀다 → READY  (tasks.c:4913, 4917-4919)</text>
  <text x="20" y="322" fill="currentColor">한쪽만 빼면: 유령 타임아웃 / 유령 깨움 — Part A 의 TODO 2·3 이 지키는 것</text>
</svg>
```

**미니 커널 규칙** (starter.c 머리 주석과 같다 — 레슨마다 커널을 따로 둔다):

| 항목 | 규칙 |
|---|---|
| 실행 | OS 스레드 1개, tick 단위, 완전히 결정적. 태스크 = 스크립트 `SEND(q,v,tmo)` `RECV(q,tmo)` `WORK(n)` `DELAY(n)` `LOOP` `END` |
| 비용 | SEND/RECV 시도 1번 = 1 tick (블록으로 끝나도), WORK n = n tick. DELAY/LOOP/END = 0 tick |
| tick 순서 | ① tick ISR: 지연 리스트 만료 ② 태스크 출발 ③ 장치 ISR 훅 ④ READY 중 최고 prio 1개가 op 1개 ⑤ 기록·불변식 |
| 동점 | 같은 prio 는 먼저 READY 된 쪽. time slicing 없음 |
| 선점 | API 가 더 높은 태스크를 깨우면 전환은 **다음 tick** 에 반영 (1 tick 해상도). `g_yield_req` 로 "전환 요청" 만 센다 |
| 타임라인 | `R` 실행 · `r` READY 인데 못 돎 · `B` 큐 대기 · `d` DELAY · `S` 정지 · `.` 아직/끝 |
| 깨움 기록 | `wakes:` 줄에 깬 순서. `이름(t)` = 큐를 기다리다 타임아웃으로 깸 |

### Part A — 리스트 두 개, 함수 다섯 개 중 셋

- `list_insert_sorted(l, x)` — **TODO 1.** FreeRTOS `vListInsert` 와 같은 규칙: 값 오름차순, 같은 값이면 **뒤에**. 지연 리스트(값 = 깰 tick)와 대기 리스트(값 = `MAX_PRIO - prio`) 둘 다 이걸 쓴다.
- `event_list_wake_head(wait)` — **TODO 2.** `xTaskRemoveFromEventList`: 대기 리스트 맨 앞(최고 prio)을 빼고, **지연 리스트에서도** 빼고, READY. 깬 태스크가 지금 태스크보다 **엄격히** 높으면 `true`.
- `tick_expire()` — **TODO 3.** `xTaskIncrementTick` 의 만료 부분: 지연 리스트 맨 앞부터 `wake_tick <= now` 인 태스크를 빼고, 큐를 기다리던 중이면 **대기 리스트에서도** 뺀다.
- 테스트: 정렬 삽입 단위 테스트, 빈 큐 블록 → send 가 깨움, receive/send 타임아웃이 정확히 tmo tick, 받기 대기자 4명(R1 p1 · R2 p3 · R3 p2 · R4 p3)의 깨는 순서 = `R2 R4 R3 R1`.

### Part B — 재시도 루프와 경쟁 시나리오

- `q_receive(q, &out, tmo)` / `q_send(q, v, tmo)` — **TODO 4, 5.** FreeRTOS 의 `for( ; ; )` 루프 그대로. 블록하면 `Q_PENDING` 을 돌려주고, 태스크가 다시 뽑히면 같은 API 가 다시 불려 루프를 이어 간다 (`in_api = 1` = `xEntryTimeSet`).
- 경쟁 시나리오 (전부 결정적):
  1. **뺏긴 아이템** — L(p1) 이 tmo 10 으로 받기 대기, S(p2) 가 보내 L 을 깨우지만 H(p3) 가 먼저 돌아 가져간다 → L 은 **남은 6 tick** 으로 재블록, 정확히 t10 에 EMPTY.
  2. **타임아웃과 give 가 같은 tick** — ISR / 높은 태스크 / 낮은 태스크가 마감 tick 에 보낼 때 각각 결과를 정의하고 테스트.
  3. **꽉 찬 큐의 보내기 대기자 4명** — prio 순서(같으면 FIFO)로 깨어나 그 순서대로 들어간다.
  4. 무작위 300 세트 × 64 tick, **매 tick 불변식**.
- 태스크 정지·삭제 중 대기(선택 항목)는 추가 연습 2 에서.

### Part C — 로그 큐: 생산자 셋, 정책 셋

모터 제어 MCU 의 로그 경로. 제어 루프 CTRL(p4, 4 tick 주기)은 **절대 기다리면 안 되므로** tmo 0, IMU(p3, 8 tick)는 tmo 2, 통신 COMM(p2, 16 tick)은 `FOREVER`. 로거 LOG(p1)는 메시지 하나를 받아 UART 로 내보낸다(1 tick). 길이 4 큐로 240 tick 돌려 생산자별 성공/버림/최대 대기를 표로 만들고, 큐 깊이·UART 속도(DMA)를 바꿔 본다. 질문: **가장 높은 우선순위 CTRL 이 왜 가장 많이 버리나?**

## 2. Problem (English)

> **Blocking message queue with priority-ordered wait lists and timeouts (tick-driven mini kernel).**
>
> Each TCB has two list nodes: `state_node` (delayed list, value = wake tick) and `event_node` (a queue's wait list, value = `MAX_PRIO - prio`). Implement:
>
> 1. `void list_insert_sorted(list_t *l, node_t *x)` — ascending by value; equal values go **after** existing ones (FIFO).
> 2. `bool event_list_wake_head(list_t *wait)` — remove the head waiter from the wait list **and** from the delayed list, make it READY; return true iff its priority is strictly higher than the current task's.
> 3. `void tick_expire(void)` — move every task with `wake_tick <= now` out of the delayed list **and** out of any wait list, make it READY.
> 4. `q_rc_t q_receive(kqueue_t *q, int32_t *out, uint32_t timeout)` and `q_send(...)` — FreeRTOS semantics: waking does **not** hand the item over. A woken task re-checks the queue; if it lost the race it blocks again with the **remaining** time (`timeout_check`), so the deadline never moves. Return `Q_OK`, `Q_EMPTY`/`Q_FULL`, or `Q_PENDING` when the caller blocked (it will be called again after it wakes).
>
> Example: `L(p1) RECV(tmo 10)` at t0; `S(p2)` sends 7 at t2 (wakes L); `H(p3)` receives with tmo 0 at t3 and gets 7. L runs at t4, finds the queue empty, re-blocks for 6 ticks, and returns `Q_EMPTY` at t10 — elapsed exactly 10, blocked twice. Timeline `L RBrrRBBBBBR`.
>
> Follow-ups: Why does FreeRTOS not hand the item to the woken task? What is the tie rule when the timeout and a send land in the same tick? Why two list items per TCB? What breaks if a task is deleted while waiting? What does `xTaskAbortDelay` return to a `portMAX_DELAY` waiter? Does changing a waiter's priority re-sort the wait list?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 9/41 passed, exit 1
./starter -v                                                                 # 타임라인 더 보기 (A2, B2a~d)
```

처음 상태: 정렬 삽입은 "맨 뒤에 붙이기", 깨우기·만료는 아무것도 안 함, receive 는 항상 EMPTY, send 는 항상 FULL. 그래서 "바로 실패" 하는 테스트 몇 개만 우연히 통과한다. 스케줄러·실행기·불변식 검사기·테스트는 주어진 코드다. pthread 를 쓰지 않는 단일 스레드 시뮬레이터라 `-pthread`·TSan 은 필요 없다.

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: 반복자는 "넣을 자리의 앞 노드" 에서 멈춘다</summary>

끝 표지(`l->end`)에서 출발해 `it->next` 가 끝 표지가 아니고 `it->next->value <= x->value` 인 동안 전진. `<=` 라서 같은 값들 **뒤** 에 선다 → FIFO. `<` 로 쓰면 같은 prio 끼리 LIFO 가 된다(돌연변이 M4: 31/41). 넣은 뒤 `container` 와 `n` 을 잊지 말 것.

</details>

<details>
<summary>힌트 2 — TODO 2·3: "두 리스트 모두" 가 핵심</summary>

깨우는 이유가 무엇이든 태스크는 BLOCKED 를 떠난다. 그 순간 그 태스크를 가리키는 노드는 **어떤 리스트에도** 남아 있으면 안 된다. 데이터로 깼는데 지연 리스트에 남으면 나중에 엉뚱한 tick 에 "타임아웃" 으로 다시 깨고, 시간으로 깼는데 대기 리스트에 남으면 다음 send 가 이미 깬 태스크를 또 깨운다. `node->container != NULL` 이면 들어 있는 것이다.

</details>

<details>
<summary>힌트 3 — TODO 4: 블록 = 함수가 끝나는 게 아니라 "루프 중간에 멈춤"</summary>

FreeRTOS 의 `xQueueReceive` 는 블록할 때 함수를 빠져나가지 않는다. `taskYIELD_WITHIN_API()` 에서 멈췄다가 깨면 **루프 처음** 부터 다시 돈다. 모델에서는 그 "멈춤" 을 `return Q_PENDING` 으로, "다시 돈다" 를 같은 함수 재호출로 흉내 낸다. 그래서 지역 변수 `xTicksToWait`, `xTimeOut`, `xEntryTimeSet` 은 TCB 의 `ticks_to_wait`, `tmo`, `in_api` 에 산다. 첫 진입(`in_api == 0`)일 때만 `ticks_to_wait = timeout`.

</details>

<details>
<summary>힌트 4 — 남은 시간은 주어진 timeout_check 가 계산한다</summary>

`timeout_check(&self->tmo, &self->ticks_to_wait)` 는 `xTaskCheckForTimeOut` 과 같다: 경과 < 남은 시간이면 남은 시간을 줄이고 기준 시각을 지금으로 바꾼 뒤 false, 아니면 0 으로 만들고 true. 그러니 재시도 때 `timeout_set` 을 **다시 부르면 안 된다** (그건 처음 블록할 때 한 번). 다시 부르고 `ticks_to_wait = timeout` 으로 되돌리면 마감이 계속 밀린다 — 하네스의 `deadline-moved` 가 잡는다.

</details>

<details>
<summary>힌트 5 — 루프 순서가 곧 tie 규칙</summary>

루프 첫 줄이 "데이터 있나?" 다. 타임아웃으로 깨어난 태스크도 돌 때 데이터가 있으면 가져간다. 그래서 FreeRTOS 의 규칙은 "타임아웃은 tick ISR 이 아니라 **그 태스크가 돌 때** 결정된다" 이다. Part B2 의 네 테스트는 이 한 줄에서 나온다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 미니 커널 | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 |
|---|---|---|---|
| `kqueue_t` (len, count, rd, wr) | `Queue_t` — `uxLength`, `uxMessagesWaiting`, `pcReadFrom`, `pcWriteTo` | `struct k_msgq` | `osMessageQueueId_t` |
| `tx_wait` / `rx_wait` | `xTasksWaitingToSend` / `xTasksWaitingToReceive` (queue.c) | 대기 큐 `wait_q` 하나 (가득/빔 중 한 상태에서만 대기자가 생김) | — |
| `state_node` / `event_node` | `xStateListItem` / `xEventListItem` (tasks.c:390 근처 TCB) | `thread->base.qnode_dlist` + timeout 노드 | — |
| `list_insert_sorted` | `vListInsert` (list.c:139, `<=` 루프 192) | 우선순위 순 대기 큐 | — |
| `block_on` | `vTaskPlaceOnEventList` (tasks.c:5402) → `prvAddCurrentTaskToDelayedList` (8685) | `z_pend_curr()` | — |
| `event_list_wake_head` | `xTaskRemoveFromEventList` (tasks.c:5500) | `z_unpend_first_thread()` | — |
| `tick_expire` | `xTaskIncrementTick` 만료 루프 (tasks.c:4871–4960) | timeout 만료 콜백 | — |
| `timeout_set` / `timeout_check` | `vTaskInternalSetTimeOutState` (5681) / `xTaskCheckForTimeOut` (5693) | `sys_timepoint` 계열 | — |
| `q_receive(q,&v,tmo)` | `xQueueReceive(q, &v, tmo)` (queue.c:1509) | `k_msgq_get(&q, &v, K_TICKS(tmo))` | `osMessageQueueGet(mq, &v, NULL, tmo)` |
| `q_send(q,v,tmo)` | `xQueueSend` = `xQueueGenericSend(..., queueSEND_TO_BACK)` (queue.c:949) | `k_msgq_put(&q, &v, K_TICKS(tmo))` | `osMessageQueuePut(mq, &v, 0, tmo)` |
| `Q_EMPTY` / `Q_FULL` | `errQUEUE_EMPTY` / `errQUEUE_FULL` | `-ENOMSG` (K_NO_WAIT) / `-EAGAIN` (타임아웃) | `osErrorResource` (0 대기) / `osErrorTimeout` |
| `FOREVER` | `portMAX_DELAY` → `xSuspendedTaskList` (tasks.c:8717–8722) | `K_FOREVER` | `osWaitForever` |
| `q_send_from_isr` | `xQueueSendFromISR` (queue.c:1167, RTOS-M02) | `k_msgq_put(..., K_NO_WAIT)` | `osMessageQueuePut(..., 0)` (ISR 에서 timeout 0) |

Zephyr 의 `k_msgq_put` 은 기다리는 받는 쪽이 있으면 메시지를 그 스레드의 버퍼로 **직접 복사(handoff)** 하는 것으로 알고 있다 — FreeRTOS 의 "깨우기만 하고 다시 확인" 과 다른 설계다 (소스를 이 레슨에서 직접 확인하진 않았다: 미검증). CMSIS-RTOS2 는 API 층이라 대기 순서는 밑의 커널(RTX5, FreeRTOS 래퍼)이 정한다.

### Part A — 정렬 삽입 하나로 두 리스트

```c
static void list_insert_sorted(list_t *l, node_t *x) {
    node_t *it = &l->end;
    while (it->next != &l->end && it->next->value <= x->value) it = it->next;   /* list.c:192 의 <= */
    x->next = it->next; x->prev = it;
    it->next->prev = x; it->next = x;
    x->container = l; l->n++;
}
```

FreeRTOS 의 원본 (list.c:192):

```c
for( pxIterator = ( ListItem_t * ) &( pxList->xListEnd ); pxIterator->pxNext->xItemValue <= xValueOfInsertion; pxIterator = pxIterator->pxNext )
```

FreeRTOS 는 끝 표지의 값을 `portMAX_DELAY` 로 두어 루프의 끝 검사를 생략하고, 대신 삽입 값이 `portMAX_DELAY` 이면 따로 처리한다 (list.c:158–161 "if the xItemValue is the same as the back marker the iteration loop below will not end"). 모델은 끝 검사를 명시했다.

대기 리스트의 값은 TCB 를 만들 때 정해진다 (tasks.c:1929):

```c
listSET_LIST_ITEM_VALUE( &( pxNewTCB->xEventListItem ), ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) uxPriority );
```

그리고 `vTaskPlaceOnEventList` 의 주석이 그대로 정답이다 (tasks.c:5412–5423): "This is placed in the list in priority order so the highest priority task is the first to be woken by the event. ... Lists are sorted in ascending order by ListItem_t.xItemValue. Normally ... xItemValue = ( configMAX_PRIORITIES - uxPriority ). Therefore, the event list is sorted in descending priority order." → `vListInsert( pxEventList, &( pxCurrentTCB->xEventListItem ) );`

**깨우기와 만료 — 두 리스트 모두.**

```c
static bool event_list_wake_head(list_t *wait) {           /* xTaskRemoveFromEventList */
    tcb_t *t = list_head_owner(wait);
    list_remove(&t->event_node);                               /* tasks.c:5525 */
    if (t->state_node.container) list_remove(&t->state_node);  /* tasks.c:5529 */
    make_ready(t, false);
    return g_cur != NULL && t->prio > g_cur->prio;             /* tasks.c:5555 */
}
static void tick_expire(void) {                            /* xTaskIncrementTick */
    while (g_delayed.n > 0) {
        node_t *h = g_delayed.end.next;
        if (h->value > g_now) break;                           /* tasks.c:4897 */
        tcb_t *t = h->owner;
        list_remove(&t->state_node);                           /* tasks.c:4913 */
        if (t->event_node.container) list_remove(&t->event_node);   /* tasks.c:4917-4919 */
        make_ready(t, t->wait_obj != NULL);
    }
}
```

FreeRTOS 원본에서 대응하는 줄: `listREMOVE_ITEM( &( pxUnblockedTCB->xEventListItem ) );` (5525) → `listREMOVE_ITEM( &( pxUnblockedTCB->xStateListItem ) ); prvAddTaskToReadyList( pxUnblockedTCB );` (5529–5530), 그리고 tick 쪽 `/* Is the task waiting on an event also?  If so remove it from the event list. */ if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL ) { listREMOVE_ITEM( ... ) }` (4915–4919). 반환값 `>` 는 "같은 prio 를 깨워도 전환 요청 없음" — 테스트 A2 의 두 번째 경우(같은 p1 끼리)가 `g_yield_req == 0` 을 확인한다.

받기 대기자 4명 (A4, `./solution` 실제 출력):

```text
A4 four receivers (R1 p1, R2 p3, R3 p2, R4 p3), sender S p4
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 R1   : R B B B B B B B r r r r R .
 R2   : . R B B B r r r r R . . . .
 R3   : . . R B B B B r r r r R . .
 R4   : . . . R B B r r r r R . . .
 S    : . . . . . R R R R . . . . .
 wakes: R2 R4 R3 R1
```

블록한 순서는 R1, R2, R3, R4 지만 깨는 순서는 값 순(5, 5, 6, 7): R2 와 R4 는 같은 prio 3 이라 먼저 온 R2 가 앞. S 가 가장 높아 4번 보내는 동안 아무도 못 돌고(t5–t8), 그 뒤 prio 순으로 돈다 → R2←1, R4←2, R3←3, R1←4.

**진짜 FreeRTOS** — `demo_freertos_queue_wait.c` [1] (빌드 명령은 Part B 의 데모 부분). 받기 대기자 RL(p2) · RH(p5) · RM1(p3) · RM2(p3) 가 5 tick 간격으로 이 순서대로 블록한다(trace 로 확인). (a) 보내는 쪽이 p1: 보낼 때마다 깬 태스크가 더 높으니 즉시 전환 → 받은 순서 = 깨운 순서. (b) 보내는 쪽이 p6: 4개를 다 보낼 때까지 아무도 못 돌고(`received during sends=0`), 그 뒤 prio 순.

```text
[1] block order (trace): RL RH RM1 RM2
[1a] sender p1 : RH<-1 RM1<-2 RM2<-3 RL<-4
[1] block order (trace): RL RH RM1 RM2
[1b] sender p6 : received during sends=0, then RH<-1 RM1<-2 RM2<-3 RL<-4
```

모델과 같다: prio 순, 같은 p3 은 먼저 온 RM1. 25회 실행 모두 같았다. (처음 버전은 블록 간격을 1 tick 으로 두었는데 12회 중 1–2회 순서가 흔들렸다 — 호스트 타이머 tick 이 printf·전환 사이에 끼어든 것으로 보고 간격을 5 tick 으로 늘렸다. POSIX 포트는 순서 확인용이지 시간 정밀도용이 아니다.)

### Part B — 재시도 루프

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">q_receive = xQueueReceive 의 for( ; ; ) — 깨면 맨 위로 돌아온다</text>
  <rect x="20" y="36" width="220" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="30" y="58" fill="currentColor">첫 진입: ticks = timeout</text>
  <rect x="20" y="92" width="220" height="34" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="30" y="114" fill="currentColor">① count &gt; 0 ?  (queue.c:1541)</text>
  <rect x="290" y="92" width="430" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="300" y="114" fill="currentColor">꺼냄 → tx_wait 맨 앞 1명 깨움 → in_api = 0 → Q_OK</text>
  <rect x="20" y="148" width="220" height="34" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="30" y="170" fill="currentColor">② ticks == 0 ?  (1575)</text>
  <rect x="290" y="148" width="200" height="34" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="300" y="170" fill="currentColor">Q_EMPTY (기다리지 않음)</text>
  <rect x="20" y="204" width="220" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="30" y="226" fill="currentColor">③ 처음이면 timeout_set (1590)</text>
  <rect x="20" y="260" width="220" height="34" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="30" y="282" fill="currentColor">④ 시간 남았나? (1609)</text>
  <rect x="290" y="260" width="430" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="300" y="282" fill="currentColor">남음: 비었으면 block_on(rx_wait, 남은 ticks) → PENDING</text>
  <rect x="290" y="312" width="430" height="34" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="300" y="334" fill="currentColor">다 됨: 여전히 비면 Q_EMPTY (1643–1648), 데이터 있으면 ①로</text>
  <path d="M130 70 L130 92" stroke="currentColor" fill="none"/>
  <path d="M130 126 L130 148" stroke="currentColor" fill="none"/><text x="136" y="141" fill="currentColor" font-size="12">없음</text>
  <path d="M130 182 L130 204" stroke="currentColor" fill="none"/><text x="136" y="197" fill="currentColor" font-size="12">아니오</text>
  <path d="M130 238 L130 260" stroke="currentColor" fill="none"/>
  <path d="M240 109 L290 109" stroke="currentColor" fill="none"/><text x="250" y="104" fill="currentColor" font-size="12">있음</text>
  <path d="M240 165 L290 165" stroke="currentColor" fill="none"/><text x="250" y="160" fill="currentColor" font-size="12">예</text>
  <path d="M240 277 L290 277" stroke="currentColor" fill="none"/>
  <path d="M240 290 L270 329 L290 329" stroke="currentColor" fill="none"/>
  <path d="M720 277 L732 277 L732 84 L10 84 L10 109 L20 109" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="480" y="78" fill="currentColor" font-size="12">깨면 (데이터든 시간이든) ①로</text>
</svg>
```

```c
static q_rc_t q_receive(kqueue_t *q, int32_t *out, uint32_t timeout) {
    tcb_t *self = g_cur;
    if (!self->in_api) self->ticks_to_wait = timeout;
    for (;;) {
        if (q->count > 0) {
            *out = q_pop(q);
            if (q->tx_wait.n > 0 && event_list_wake_head(&q->tx_wait)) g_yield_req++;
            self->in_api = 0;
            return Q_OK;
        }
        if (self->ticks_to_wait == 0) { self->in_api = 0; return Q_EMPTY; }
        if (!self->in_api) { timeout_set(&self->tmo); self->in_api = 1; }
        if (!timeout_check(&self->tmo, &self->ticks_to_wait)) {
            if (q->count == 0) { block_on(&q->rx_wait, self->ticks_to_wait, q); return Q_PENDING; }
        } else if (q->count == 0) {
            self->in_api = 0;
            return Q_EMPTY;
        }
    }
}
```

FreeRTOS 가 이 루프를 쓰는 이유는 첫 줄의 주석에 있다 (queue.c:1539–1540): "Is there data in the queue now?  To be running the calling task must be the highest priority task wanting to access the queue." — 커널은 아이템을 깨운 태스크에게 **예약해 두지 않는다**. 깨어난 태스크가 실제로 CPU 를 받을 때까지 더 높은 태스크(큐를 기다리고 있지 않던)가 먼저 가져갈 수 있다. 그래서 다시 확인하고, 잃었으면 다시 잔다 (1628–1634 "The queue contains data again.  Loop back to try and read the data.").

남은 시간 계산은 `xTaskCheckForTimeOut` (tasks.c:5741–5746):

```c
else if( xElapsedTime < *pxTicksToWait )
{
    /* Not a genuine timeout. Adjust parameters for time remaining. */
    *pxTicksToWait -= xElapsedTime;
    vTaskInternalSetTimeOutState( pxTimeOut );
    xReturn = pdFALSE;
}
```

**B1 — 뺏긴 아이템** (`./solution` 실제 출력):

```text
B1 L(p1) RECV tmo 10 @t0, S(p2) SEND @t2 wakes L, H(p3) RECV tmo 0 @t3 steals it
 tick : 0 1 2 3 4 5 6 7 8 9 0 1
 L    : R B r r R B B B B B R .
 S    : . . R . . . . . . . . .
 H    : . . . R . . . . . . . .
 wakes: L L(t)
 L: rc=EMPTY at t10, elapsed 10, blocked 2 times
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">B1 — solution.c 실제 타임라인: 깨어난 L 이 아이템을 H 에게 뺏기고 남은 시간으로 다시 잔다</text>
  <text x="79" y="48" fill="currentColor" font-size="12">0</text>
  <text x="125" y="48" fill="currentColor" font-size="12">1</text>
  <text x="171" y="48" fill="currentColor" font-size="12">2</text>
  <text x="217" y="48" fill="currentColor" font-size="12">3</text>
  <text x="263" y="48" fill="currentColor" font-size="12">4</text>
  <text x="309" y="48" fill="currentColor" font-size="12">5</text>
  <text x="355" y="48" fill="currentColor" font-size="12">6</text>
  <text x="401" y="48" fill="currentColor" font-size="12">7</text>
  <text x="447" y="48" fill="currentColor" font-size="12">8</text>
  <text x="493" y="48" fill="currentColor" font-size="12">9</text>
  <text x="539" y="48" fill="currentColor" font-size="12">10</text>
  <text x="585" y="48" fill="currentColor" font-size="12">11</text>
  <text x="16" y="77" fill="currentColor">L</text>
  <rect x="60" y="58" width="44" height="28" class="f-acc" stroke="currentColor"/><text x="78" y="77" fill="currentColor" font-size="12">R</text>
  <rect x="106" y="58" width="44" height="28" class="f-warn" stroke="currentColor"/><text x="124" y="77" fill="currentColor" font-size="12">B</text>
  <rect x="152" y="58" width="44" height="28" class="f-mut" stroke="currentColor"/><text x="170" y="77" fill="currentColor" font-size="12">r</text>
  <rect x="198" y="58" width="44" height="28" class="f-mut" stroke="currentColor"/><text x="216" y="77" fill="currentColor" font-size="12">r</text>
  <rect x="244" y="58" width="44" height="28" class="f-acc" stroke="currentColor"/><text x="262" y="77" fill="currentColor" font-size="12">R</text>
  <rect x="290" y="58" width="44" height="28" class="f-warn" stroke="currentColor"/><text x="308" y="77" fill="currentColor" font-size="12">B</text>
  <rect x="336" y="58" width="44" height="28" class="f-warn" stroke="currentColor"/><text x="354" y="77" fill="currentColor" font-size="12">B</text>
  <rect x="382" y="58" width="44" height="28" class="f-warn" stroke="currentColor"/><text x="400" y="77" fill="currentColor" font-size="12">B</text>
  <rect x="428" y="58" width="44" height="28" class="f-warn" stroke="currentColor"/><text x="446" y="77" fill="currentColor" font-size="12">B</text>
  <rect x="474" y="58" width="44" height="28" class="f-warn" stroke="currentColor"/><text x="492" y="77" fill="currentColor" font-size="12">B</text>
  <rect x="520" y="58" width="44" height="28" class="f-acc" stroke="currentColor"/><text x="538" y="77" fill="currentColor" font-size="12">R</text>
  <text x="16" y="113" fill="currentColor">S</text>
  <rect x="152" y="94" width="44" height="28" class="f-acc" stroke="currentColor"/><text x="170" y="113" fill="currentColor" font-size="12">R</text>
  <text x="16" y="149" fill="currentColor">H</text>
  <rect x="198" y="130" width="44" height="28" class="f-acc" stroke="currentColor"/><text x="216" y="149" fill="currentColor" font-size="12">R</text>
  <line x1="60" y1="172" x2="612" y2="172" stroke="currentColor"/>
  <line x1="83" y1="186" x2="543" y2="186" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="198" y="202" fill="currentColor" font-size="12">마감 = 첫 호출 t0 + 10 = t10 (재블록해도 그대로)</text>
  <text x="16" y="228" fill="currentColor" font-size="12">t0  L: RECV(tmo 10) → 큐가 비어 블록 (대기 리스트 + 지연 리스트, 깰 tick 10)</text>
  <text x="16" y="248" fill="currentColor" font-size="12">t2  S: SEND 7 → 대기 리스트 맨 앞 L 을 깨움. L(1) &lt; S(2) 이라 전환 요청 없음 → L 은 r</text>
  <text x="16" y="268" fill="currentColor" font-size="12">t3  H(3) 출발: RECV(tmo 0) → 데이터가 있으니 바로 가져감. L 은 아직 못 돌았다</text>
  <text x="16" y="288" fill="currentColor" font-size="12">t4  L: 루프 처음으로 → 비었음 → 경과 4 &lt; 10 → 남은 6 tick 으로 다시 블록 (깰 tick 4+6 = 10)</text>
  <text x="16" y="308" fill="currentColor" font-size="12">t10 tick ISR 이 L 을 두 리스트에서 빼고 READY → L: 비었음 + 시간 다 됨 → EMPTY, 총 10 tick</text>
</svg>
```

같은 시나리오에 t6 에 S2 가 하나 더 보내면, 재블록한 L 도 정상적으로 깨서 t7 에 8 을 받는다 (테스트). H 가 "큐를 기다리지 않던" 태스크라는 점이 중요하다: H 가 대기 리스트에 있었다면 더 높은 H 가 맨 앞이라 S 의 send 는 H 를 깨웠을 것이다.

**진짜 FreeRTOS 도 같은가** — `demo_freertos_queue_wait.c` [2]: L(p1) 이 100 tick 으로 받기 대기, S(p2) 가 +20 에 보내고 H(p3) 에게 알림, H 가 tmo 0 으로 가져간다. `FreeRTOSConfig.h` 에 `traceBLOCKING_ON_QUEUE_RECEIVE` 를 정의해 L 이 블록할 때마다 `xQueueReceive` 의 지역 변수 `xTicksToWait` 를 기록했다 (queue.c:1615, `vTaskPlaceOnEventList` 바로 앞).

<!-- code: demo_freertos_queue_wait.c -->

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_queue_wait.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o demo_wait && ./demo_wait          # 약 0.7 초 (Apple M6, Apple clang 21.0.0)
```

```text
[2] S sent at +20, H got 7 (pdPASS) at +20, L: errQUEUE_EMPTY after 100 ticks
[2] trace: L blocks at +0 with xTicksToWait=100
[2] trace: L blocks at +20 with xTicksToWait=80
```

모델의 B1 과 같다: 두 번 블록, 두 번째는 **남은 80**, 총 100 tick 에 `errQUEUE_EMPTY`. 20회 실행 중 19회가 위와 같았고, 1회는 H 가 +21 에 돌아 L 이 "+22 에 78" 로 재블록했다 — 그래도 총 100 tick. POSIX 포트의 tick 은 호스트 타이머라 ±1 tick 은 흔들린다 (실시간성 없음, 순서·합계를 본다).

**B2 — 타임아웃과 give 가 같은 tick.** 규칙: **깨어난 태스크가 실제로 돌 때 큐에 데이터가 있으면 데이터가 이긴다.** 타임아웃은 tick ISR 이 정하는 게 아니라, 깨어난 태스크가 루프를 돌며 정한다. R(p2) 이 `RECV(tmo 5)` 로 t0 에 블록했을 때 (`./solution -v` 실제 출력 요약):

| 경우 | 누가 언제 보내나 | 실제 타임라인 (R) | 결과 |
|---|---|---|---|
| B2a | ISR 이 마감 tick t5 에 (tick ISR ① 다음, ③) | `RBBBBR` · wakes `R(t)` | **OK 42** @t5. ①에서 이미 타임아웃으로 깼고, ISR 은 대기자 없는 큐에 넣기만 → R 이 돌며 데이터 발견 |
| B2b | 더 높은 Hs(p3) 가 t5 에 | `RBBBBrR` · wakes `Hs R(t)` | **OK 43** @t6, 경과 6 — 마감이 지났어도 데이터가 있으면 성공 |
| B2c | 더 낮은 Ls(p1) 가 t5 에 깸 | `RBBBBR` · Ls `.ddddrR` | **EMPTY** @t5, Ls 가 t6 에 넣은 44 는 큐에 남음 (`count == 1`) |
| B2d | ISR 이 t4(마감 1 tick 전), t9 | `RBBBRRBBBR` · wakes `R R` | t4 OK 45 (데이터로 깸 → 지연 리스트에서도 빠짐), 두 번째 `RECV(FOREVER)` 는 t9 에 46 — 유령 타임아웃 없음 |

B2d 가 TODO 2 의 "지연 리스트에서도 빼기" 를 검사한다: 안 빼면 t5 에 tick ISR 이 R 의 낡은 노드를 꺼내 FOREVER 대기 중인 R 을 엉뚱하게 깨운다 (돌연변이 M1: 36/41, 불변식 위반 1125회).

진짜 FreeRTOS `demo_freertos_queue_wait.c` [3]: T(p2) 가 50 tick 으로 받기 대기, 틱 훅(`vApplicationTickHook`, 틱 ISR 안)이 xQueueSendFromISR. 틱 훅은 `xTaskIncrementTick` 에서 지연 리스트 처리 **뒤** 에 불린다 (만료 루프 tasks.c:4871–4960, 훅 5006) — 모델의 ① → ③ 순서와 같다.

```text
[3] ISR send at deadline-1: isr=pdPASS, T got pdPASS val=42 after 49 ticks, left in queue=0
[3] ISR send at deadline+0: isr=pdPASS, T got pdPASS val=42 after 50 ticks, left in queue=0
[3] ISR send at deadline+1: isr=pdPASS, T got errQUEUE_EMPTY val=0 after 50 ticks, left in queue=1
```

마감 tick 에 보낸 경우가 B2a 와 같이 **pdPASS, 50 tick**. 20회 반복 모두 같았다 (앞선 12회 시험 중 1회는 `start` 를 읽은 뒤 tick 이 넘어가 49/50 대신 50/51 로 찍혔다 — 측정 쪽의 ±1).

참고: 받기 쪽은 타임아웃 분기에서 **한 번 더** 비었는지 본다 (queue.c:1636–1643 "Timed out.  If there is no data in the queue exit, otherwise loop back"). 보내기 쪽은 바로 `errQUEUE_FULL` 이다 (1152–1161). FreeRTOS 는 루프 꼭대기의 임계 구역을 나온 뒤 스케줄러만 멈추고(`vTaskSuspendAll` + `prvLockQueue`) 시간을 확인하므로, 그 틈에 ISR 이 데이터를 넣을 수 있어서다. 모델은 API 전체가 원자적이라 이 재확인이 없어도 결과가 같다 (돌연변이 M5·M7: 41/41 — "동치 돌연변이").

**B3 — 꽉 찬 큐의 보내기 대기자** (`./solution` 실제 출력, 길이 2 큐에 100·101 이 미리 들어 있음):

```text
B3 full queue (len 2), senders S1 p1, S2 p3, S3 p2, S4 p3; consumer C p4
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 S1   : R B B B B B B B B B B B r r r r r R . . . . . .
 S2   : . R B B B r r r R . . . . . . . . . . . . . . .
 S3   : . . R B B B B B B r r r r r R . . . . . . . . .
 S4   : . . . R B B r r r r r R . . . . . . . . . . . .
 C    : . . . . . R R R r R R r R R r R R r R . . . . .
 wakes: S2 S4 C S3 C S1 C C
```

C 가 하나 꺼낼 때마다 보내기 대기자 맨 앞 1명이 깬다: S2(t5), S4(t6), S3(t9), S1(t12) — prio 순, 같은 prio 3 은 먼저 온 S2. C 가 받은 순서 `100 101 2 4 3 1` 도 그 순서다. C 가 t7·t10·t13·t16 에 `R` 인데 아무것도 못 받는 것은 큐가 비어 블록하는 시도다 — 깨운 보내기 대기자들이 아직 못 돌았기 때문 (C 가 가장 높다).

**B4 — 무작위 300 세트, 매 tick 불변식.** xorshift32(고정 시드)로 큐 2개(길이 1–3, 1–2), 태스크 4–7개(prio 1–5), op 3–6개(SEND/RECV 의 tmo ∈ {0, 1, 2, 3, 5, FOREVER}, WORK, DELAY)를 만들어 64 tick 씩 돌린다. 하네스가 매 tick 끝에 검사하는 것:

| 검사 | 뜻 |
|---|---|
| 대기·지연 리스트의 모든 노드 주인이 BLOCKED, `container` 일치, prev/next 일관, 개수 일치 | "리스트에 BLOCKED 아닌 태스크 없음" |
| 대기 리스트는 값 오름차순 + 같은 값은 `block_seq` 순 / 지연 리스트에 `wake_tick <= now` 없음 | 정렬·FIFO, 만료 누락 없음 |
| BLOCKED 태스크는 적어도 한 리스트에, 큐 대기면 그 큐의 대기 리스트에, 유한 대기면 지연 리스트에 | 아무도 못 깨우는 태스크 없음 |
| `0 <= count <= len` | 이중 꺼내기·넘침 없음 |
| 대기자가 있는데 데이터(자리) 수 > "깨웠지만 아직 안 돈" 태스크 수 | **잃어버린 깨움(lost wakeup)** |
| 재블록 때 깰 tick == 첫 호출 tick + 원래 tmo / 실패 반환 때 경과 >= tmo | 마감 불변, 이른 타임아웃 없음 |
| 받은 값 순서 == 성공한 send 순서, 끝에 보낸 수 − 받은 수 == count | FIFO, 보존 |

```text
B4 random: 300 sets x 64 ticks, ops ok=4015 err=1504 | invariant=0 lost-wakeup=0 deadline-moved=0 early-timeout=0 fifo/conservation=0
```

검사가 진짜 버그를 잡는지 돌연변이로 확인했다 (`/tmp` 의 임시 사본, 레슨 파일 아님):

| 돌연변이 | 결과 | 잡은 검사 |
|---|---|---|
| M1 TODO 2 에서 지연 리스트 제거 빠뜨림 | 36/41 | 불변식 1125 (유령 노드) |
| M2 TODO 3 에서 대기 리스트 제거 빠뜨림 | 35/41 | 불변식 40638, lost-wakeup 1711 |
| M3 재시도마다 `timeout_set` + 원래 tmo 로 | 33/41 | deadline-moved 9304 |
| M4 정렬 삽입 `<=` → `<` (같은 prio LIFO) | 31/41 | 정렬·FIFO 912 |
| M6 yield 판정 `>` → `>=` | 40/41 | A2 의 같은 prio 테스트 |
| M5·M7 타임아웃/블록 직전 재확인 제거 | 41/41 | 동치 (모델은 원자적) |

### Part C — 로그 큐: 가장 높은 태스크가 가장 많이 버린다

`./solution` 실제 출력 (앞 48 tick 타임라인과 240 tick 통계):

```text
C log queue len 4, LOG p1 + UART 1 tick (first 48 ticks)
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7
 CTRL : R d d d R d d d R d d d R d d d R d d d R d d d R d d d R d d d R d d d R d d d R d d d R d d d
 IMU  : . R d d d d d d d R d d d d d d d R d d d d d d d R r R d d d d d d d R d d d d d d d R d d d d
 COMM : . . R d d d d d d d d d d d d d d d R B B r R d d d d d d d d d d d d d d d R d d d d d d d d d
 LOG  : r r r R r R R R r r R R r R R R r r r R r R r R r r R r r R R R r R R r r R r R r R R r r R R R
C  UART work qlen | CTRL ok/full  IMU ok/full maxblk  COMM ok maxblk | logged
           1    4 |      25/35        29/1         2       14      9 |     65
           1    8 |      30/30        27/2         2       15      9 |     64
           0    4 |      60/0         30/0         0       15      0 |    105
           0    1 |      30/30        29/1         2       15      4 |     74
```

| 관찰 | 이유 (큐 내부로 설명) |
|---|---|
| CTRL(p4) 이 60 중 35 를 버린다 — 가장 높은데 가장 많이 | tmo 0 은 **대기 리스트에 서지 않는다**. LOG 가 자리를 하나 비우면 `tx_wait` 맨 앞(기다리던 IMU 나 COMM)이 깨어 그 자리를 가져간다. 우선순위는 "대기 리스트 안에서의 순서" 일 뿐, 줄에 안 선 태스크는 경쟁에 못 낀다 |
| IMU 최대 대기 2, 버림 1 | tmo 2 → 2 tick 안에 자리가 안 나면 포기. 마감이 밀리지 않으니 최악 지연이 정확히 보장된다 |
| COMM 버림 0, 최대 대기 9 tick | FOREVER — 데이터는 지키지만 그 시간 동안 통신 태스크가 멈춘다 (워치독 킥을 이 태스크가 하면 위험) |
| 큐 4 → 8: 버림 35 → 30 | 지속 과부하(생산 > 소비)에는 깊이가 소용없다. 깊이는 **버스트** 흡수용 |
| DMA UART (메시지당 1 tick): 버림 0 | 소비 속도를 올리는 것이 근본 해결 |
| DMA 라도 깊이 1: CTRL 30 버림 | 세 생산자가 같은 tick 근처에 몰리는 버스트를 깊이 1 로는 못 받는다 |

실제 MCU 로 옮기면(예: STM32G4 모터 컨트롤러): 20 kHz PWM ISR 이 1 kHz 제어 태스크를 깨우고, 제어 태스크는 로그를 `xQueueSend(q, &rec, 0)` 로만 넣고 버린 개수를 카운터로 센다. 로거는 가장 낮은 우선순위에서 DMA UART 로 내보낸다. "로그를 하나도 잃으면 안 된다" 는 요구가 있으면 로그용 큐를 생산자별로 나누거나(제어 전용 큐), 링 버퍼 + 덮어쓰기 정책을 쓴다 — 높은 우선순위 태스크를 대기시키는 쪽으로 풀면 안 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 41/41 passed (5회 반복 동일, 0.01 초)
./solution -v                                                                 # A2, B2a~d 타임라인까지
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san -v   # 41/41, 보고 없음
```

(작성 중 ASan 이 실제 버그를 하나 잡았다: `LOOP` 로 끝나는 스크립트를 `END` 까지 복사하던 `k_task` 가 복합 리터럴 배열 끝을 넘어 읽었다 — stack-buffer-overflow. `END` 또는 `LOOP` 에서 멈추도록 고친 것이 지금 코드다.)

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 깨어났으면 데이터가 있다고 믿고 바로 꺼냄 | FreeRTOS 는 넘겨주지 않는다. 더 높은 태스크가 먼저 가져가면 빈 큐에서 꺼냄 → 같은 값 두 번, `count = -1` (연습 1b) | 루프 처음으로 돌아가 `count > 0` 재확인 |
| 재시도 때 원래 timeout 으로 다시 블록 | 깰 때마다 마감이 뒤로 밀린다. 계속 뺏기면 **영원히** 안 끝난다 (연습 1a: 30 tick 동안 PENDING, 마감 10 → 14 → 24 → 34) | `timeout_check` 로 남은 시간, `timeout_set` 은 처음 한 번 |
| 데이터로 깨울 때 대기 리스트에서만 뺌 | 지연 리스트에 남은 노드가 나중에 유령 타임아웃을 일으킨다 (B2d, M1) | 두 리스트 모두 제거 (tasks.c:5525, 5529) |
| 타임아웃 때 지연 리스트에서만 뺌 | 다음 send 가 이미 깬(또는 끝난) 태스크를 "깨운다" → 진짜 대기자는 못 깸 = lost wakeup (M2: 1711회) | tasks.c:4917–4919 처럼 대기 리스트도 |
| 대기 리스트를 FIFO 로만 (`list_insert_end`) | 낮은 prio 가 먼저 줄 섰으면 높은 prio 가 기다린다 = 큐에서 생기는 우선순위 역전 | `MAX_PRIO - prio` 로 정렬 삽입 |
| 같은 값 앞에 삽입 (`<`) | 같은 prio 끼리 LIFO — 먼저 온 태스크가 굶을 수 있다 (M4) | 같은 값 **뒤** (`<=`, list.c:192) |
| send 가 대기자를 **모두** 깨움 | 1개 넣었는데 N 명이 깨어 N−1 명은 헛돌고 다시 잠든다 (thundering herd) | 맨 앞 1명만 (`xTaskRemoveFromEventList` 는 head 하나) |
| `xQueuePeek` 성공 후 아무도 안 깨움 | 데이터가 남았는데 다른 받기 대기자가 잠든 채 — lost wakeup (연습 3) | peek 도 다음 대기자를 깨운다 (queue.c:1927–1935) |
| 대기 중인 태스크를 삭제할 때 대기 리스트를 안 건드림 | 나중의 send 가 해제된 TCB 를 깨운다 → HardFault | `vTaskDelete` 처럼 두 리스트 모두 (tasks.c:2228, 2238–2240) |
| 제어 루프 같은 높은 태스크가 FOREVER 로 send | 소비자가 느리면 제어 주기가 무너진다 (Part C 의 COMM 최대 9 tick) | tmo 0 + 버림 카운터, 또는 상한 있는 tmo |

## 8. 면접 Q&A

<details>
<summary>Q1. 빈 큐에서 xQueueReceive(q, &v, 100) 를 부르면 커널 안에서 무슨 일이 일어나나요?</summary>

임계 구역에서 `uxMessagesWaiting` 를 본다 → 0 이고 대기 시간이 0 이 아니니 지금 tick 을 `xTimeOut` 에 기록 → 임계 구역을 나와 스케줄러를 멈추고 큐를 잠근 뒤(`vTaskSuspendAll`, `prvLockQueue`) `xTaskCheckForTimeOut` 으로 시간을 확인 → 아직 비었으면 `vTaskPlaceOnEventList`: TCB 의 `xEventListItem` 을 `xTasksWaitingToReceive` 에 prio 순으로 넣고, `xStateListItem` 을 ready 리스트에서 빼 지연 리스트(깰 tick = 지금 + 100)에 넣는다 → 큐 잠금 해제, 스케줄러 재개, yield. 깨면(데이터든 시간이든) 루프 처음으로 돌아가 다시 확인한다 (queue.c:1509–1656). 꼬리: "왜 큐를 잠그나?" — 스케줄러를 멈춘 동안 ISR 이 대기 리스트를 건드리지 못하게. 잠긴 동안 ISR 의 send 는 `cTxLock` 만 올리고, `prvUnlockQueue` 가 나중에 대기자를 깨운다 (queue.c:176–182 주석).

</details>

<details>
<summary>Q2. TCB 에 리스트 아이템이 왜 두 개인가요?</summary>

태스크는 동시에 두 가지를 기다릴 수 있기 때문이다: "이벤트(큐·세마포어)" 와 "시간(타임아웃)". `xStateListItem` 은 상태 리스트(ready / delayed / suspended) 중 정확히 하나에, `xEventListItem` 은 이벤트 대기 리스트에 들어간다. 타임아웃 있는 큐 대기는 둘 다 쓴다. 그래서 어느 쪽으로 깨든 **반대쪽도** 빼야 한다: send 쪽 `xTaskRemoveFromEventList` 는 이벤트 → 상태 순(tasks.c:5525, 5529), tick 쪽은 상태 → 이벤트 순(4913, 4917–4919). 정지(`vTaskSuspend` 3214, 3224–3226), 삭제(2228, 2238–2240), `xTaskAbortDelay`(4757, 4765–4767)도 전부 두 개를 같이 다룬다. 노드 값의 의미도 다르다: 상태 리스트는 깰 tick, 이벤트 리스트는 `configMAX_PRIORITIES - prio`.

</details>

<details>
<summary>Q3. 왜 FreeRTOS 는 아이템을 깨운 태스크에게 바로 넘겨주지(handoff) 않나요? 단점은?</summary>

넘겨주려면 send 가 받는 태스크의 버퍼에 직접 복사해야 하는데, FreeRTOS 의 블록된 태스크는 `xQueueReceive` 의 루프 안에서 멈춰 있을 뿐 "어디로 복사해 달라" 는 정보가 커널에 없다. 대신 루프가 깨어난 뒤 스스로 꺼낸다 — 구현이 단순하고, 높은 prio 태스크가 대기 없이 바로 가져갈 수 있어 처리량·응답성이 좋다 (B09 연습 3 의 barging). 단점: ① 깨어난 낮은 태스크가 헛걸음(재블록)할 수 있고, 계속 뺏기면 굶는다. ② 그래서 재시도 루프와 "남은 시간" 계산이 필수다. Zephyr `k_msgq` 는 대기자가 있으면 그 스레드의 버퍼로 바로 복사하는 쪽으로 알고 있다 (문서 기준, 미검증). 꼬리: "뺏긴 태스크의 총 대기 시간은?" — 마감 불변이므로 원래 tmo 를 넘지 않는다 (B1: 정확히 10, FreeRTOS 데모: 정확히 100).

</details>

<details>
<summary>Q4. 타임아웃이 만료되는 tick 에 ISR 이 데이터를 넣으면 receive 는 무엇을 돌려주나요?</summary>

FreeRTOS 에서는 데이터가 이긴다 — 단, 받는 태스크가 **실제로 돌기 전에** 데이터가 들어왔다면. 틱 ISR 안에서 지연 리스트 처리(tasks.c:4871–4960)가 먼저 태스크를 READY 로 옮기고, 그 뒤 틱 훅(5006)이나 다른 ISR 이 send 하면 대기자가 없으니 큐에 들어가기만 한다. 태스크가 돌면 루프 첫 줄 `if( uxMessagesWaiting > 0 )` (queue.c:1541) 에서 가져간다 → `pdPASS`. 데모 [3]: 마감 tick 에 보내면 `pdPASS after 50 ticks`, 마감+1 이면 `errQUEUE_EMPTY` + 큐에 1개 남음. 더 높은 태스크가 마감 tick 에 보내도 같다(B2b: 경과 6 인데 OK). 반대로 받는 쪽이 먼저 돌면 EMPTY (B2c). 즉 "타임아웃 = tick ISR 이 아니라 태스크가 루프에서 내리는 결정".

</details>

<details>
<summary>Q5. 대기 리스트가 우선순위 순인데, 기다리는 도중 vTaskPrioritySet 으로 우선순위를 바꾸면?</summary>

FreeRTOS V11.3.2 는 `xEventListItem` 의 **값만** 바꾸고(값이 다른 용도로 쓰이지 않을 때, tasks.c:2972–2976) 리스트에서 위치를 옮기지 않는다. 주석: "If the task is in the blocked or suspended list we need do nothing more than change its priority variable" (2983–2984). 그래서 이미 줄 선 위치는 그대로다. 연습 5 에서 재현: R1 을 1 → 5 로 올려도 먼저 깨는 건 R2(p3). 정렬을 다시 하는 커널이면 R1. 실무 결론: 대기 중 우선순위 변경에 의존하지 말 것. (뮤텍스 상속은 별개 경로: `xTaskPriorityInherit` 이 주인의 이벤트 아이템 값을 고친다, tasks.c:6764–6766 — RTOS-M04.)

</details>

<details>
<summary>Q6. 큐에서 기다리던 태스크를 다른 태스크가 vTaskDelete / vTaskSuspend 하면?</summary>

두 리스트 모두에서 빠진다: `vTaskSuspend` 는 `uxListRemove( &xStateListItem )` 후 "Is the task waiting on an event also?" 를 보고 `xEventListItem` 도 뺀다 (tasks.c:3214–3226). 삭제도 같다 (2228–2240). 빼지 않으면 나중의 send 가 사라진 TCB 를 깨운다. 정지됐다가 `vTaskResume` 되면 태스크는 `xQueueReceive` 루프로 돌아가 다시 확인한다: 그새 데이터가 있으면 성공, 없으면 남은 시간으로 재블록, 마감이 지났으면 즉시 EMPTY (연습 2 의 세 경우). 정지된 동안 온 데이터는 그 태스크를 깨우지 못하고 큐에 남는다.

</details>

<details>
<summary>Q7. portMAX_DELAY 로 기다리는 태스크를 xTaskAbortDelay 하면 무엇이 반환되나요?</summary>

`errQUEUE_EMPTY` (send 면 `errQUEUE_FULL`). `xTaskAbortDelay` 는 두 리스트에서 빼고, 이벤트 리스트에 있었으면 `ucDelayAborted = pdTRUE` 로 표시한다 (tasks.c:4757–4772, 주석 "so it should not re-evaluate its block time and then block again"). 깨어난 태스크의 `xTaskCheckForTimeOut` 은 이 표시를 **`portMAX_DELAY` 검사보다 먼저** 본다 (5710–5716 vs 5721) → 타임아웃으로 처리 → 큐가 비었으니 EMPTY. 연습 4: FOREVER 대기가 t3 abort 에 `EMPTY, 경과 3`. 단순 지연(vTaskDelay) 중이면 표시 없이 그냥 깨운다. `INCLUDE_xTaskAbortDelay` 가 1 이어야 한다 (기본 0, FreeRTOS.h:268–269).

</details>

<details>
<summary>Q8. 이 구조의 시간 복잡도는? 대기자가 많으면 무엇이 느려지나요?</summary>

블록(정렬 삽입)은 대기자 수 n 에 O(n) — `vListInsert` 가 선형 탐색이고 스케줄러를 멈춘 상태에서 돈다. 깨우기(head 제거)는 O(1), 지연 리스트 삽입은 지연 태스크 수에 O(m). tick ISR 의 만료 처리는 `xNextTaskUnblockTime` 으로 대부분 O(1) 에 끝난다 (tasks.c:4871). 펌웨어에서 큐 하나에 대기자가 수십 명인 일은 드물어 문제되지 않지만, 대기자가 많은 설계라면 이벤트 그룹이나 태스크별 알림(RTOS-M01, B13)이 맞다. tick 처리 자료구조 비교는 RTOS-M07.

</details>

## 9. 추가 연습

### 연습 1 — 흔한 오답 두 가지를 재현하라

(a) 재시도할 때마다 `timeout_set` + `ticks_to_wait = timeout` 으로 되돌리는 구현, (b) "깼으면 내 것" 이라 믿고 확인 없이 꺼내는 구현을 B1 시나리오로 돌려라. 무엇이 관찰되나?

<details>
<summary>답 보기</summary>

실제 출력 (`./practice -v`):

```text
ex1a restart timeout on every retry
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
 L    : R B r r R B B B B B B B B B R B B B B B B B B B R B B B B B
 wakes: L L(t) L(t)
ex1a VAR_RESTART: after 30 ticks L rc=PENDING, blocked 4 times, wake_tick now 34, deadline-moved=3
ex1b VAR_TRUST: H got 7, L rc=OK got 7 at t4, count=-1, invariant=26 fifo=1
```

(a) 생각보다 나쁘다: 마감이 t14 로 밀리는 데서 끝나지 않는다. 시간이 다 돼서 깨어나도 재시도 경로가 타임아웃을 다시 시작하므로 **영원히 EMPTY 를 못 돌려준다** (t14, t24, t34 …). (b) H 가 가져간 7 을 L 도 "받는다" — 빈 링 버퍼의 낡은 칸을 읽은 것. `count = -1` 이 되고 FIFO 오라클도 틀린다. 실제 칩에서는 다음 send 가 count 0 으로 돌아온 큐에 덮어쓰며 데이터가 한 칸 어긋난다.

</details>

### 연습 2 — 기다리는 도중 정지·재개·삭제

`task_suspend`, `task_resume`, `task_delete` 를 작성하라. R(p2) 이 `RECV(tmo 10)` 로 t0 에 블록. (i) t3 정지, t4 데이터 도착, t6 재개 (ii) t3 정지, t6 재개, 데이터 없음 (iii) t3 정지, t12 재개 (iv) t3 삭제, t6 데이터 도착 — 각각 결과는?

<details>
<summary>답 보기</summary>

정지·삭제 모두 `list_remove(&state_node); list_remove(&event_node);` (tasks.c:3214–3226, 2228–2240). 재개는 READY 로만 — 나머지는 재시도 루프가 알아서 한다.

```text
ex2 suspend t3, data t4, resume t6
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
 R    : R B B S S S R . . . . . . . . .
 wakes: R
```

(i) t4 의 데이터는 정지된 R 을 깨우지 못하고(대기 리스트에 없음) 큐에 남는다 → t6 재개 후 루프 첫 줄에서 가져감, OK @t6. (ii) 데이터 없음 → 경과 6 < 10 → 남은 4 로 재블록 → EMPTY @t10, 블록 2회. (iii) 마감이 지났다 → 재개 즉시 EMPTY @t12, 블록 1회. (iv) 삭제 후 대기 리스트·지연 리스트 모두 비어 있고, t6 의 데이터는 큐에 남는다 (불변식 0).

</details>

### 연습 3 — peek 는 왜 다음 대기자를 깨우나

`q_peek` (꺼내지 않고 복사만)를 만들어라. P(p3) 가 peek, R(p2) 가 receive 로 빈 큐에서 기다리고 S(p1) 가 1개 보낸다. peek 성공 후 아무도 안 깨우면?

<details>
<summary>답 보기</summary>

send 는 맨 앞 1명(P)만 깨운다. P 는 데이터를 **남기므로** 그 데이터를 기다리는 다른 대기자를 깨워야 한다 — FreeRTOS 도 그렇게 한다 (queue.c:1927–1931 "The data is being left in the queue, so see if there are any other tasks waiting for the data.").

```text
ex3 peek_wakes_next=1: P got 77, R rc=OK, count=0, lost-wakeup ticks=0
ex3 peek_wakes_next=0: P got 77, R rc=PENDING, count=1, lost-wakeup ticks=5
```

깨우지 않으면 데이터가 큐에 있는데 R 이 영원히 잔다 — 하네스의 lost-wakeup 검사가 5 tick 연속 잡는다.

</details>

### 연습 4 — xTaskAbortDelay

`task_abort_delay(t)` 를 만들어라: BLOCKED 이면 두 리스트에서 빼고, 이벤트 대기였으면 `aborted = 1`. `timeout_check` 는 `aborted` 를 FOREVER 검사보다 먼저 본다. R 이 `RECV(FOREVER)` 중 t3 에 abort 되면? `DELAY(10)` 중이면?

<details>
<summary>답 보기</summary>

`RECV(FOREVER)`: t3 에 EMPTY, 경과 3. 순서가 핵심이다 — `aborted` 를 FOREVER 보다 늦게 보면 FOREVER 분기가 "시간 안 됨" 을 돌려줘 R 은 다시 잠든다. FreeRTOS 도 abort 검사가 먼저다 (tasks.c:5710–5716, FOREVER 는 5721). DELAY 중 abort: 이벤트 리스트에 없으니 표시 없이 READY → 다음 op `RECV(0)` 가 t2 에 EMPTY, `aborted` 는 0 그대로 (tasks.c:4765–4772 는 이벤트 아이템이 있을 때만 표시).

</details>

### 연습 5 — 기다리는 중 우선순위 변경

R1(p1) 이 t0, R2(p3) 이 t1 에 빈 큐에서 기다린다. t3 에 R1 을 p5 로 올리고 t4 에 ISR 이 1개 보낸다. FreeRTOS 처럼 값만 바꿀 때와, 다시 정렬할 때 누가 먼저 깨나?

<details>
<summary>답 보기</summary>

```text
ex5 resort=0: first woken = R2
ex5 resort=1: first woken = R1
```

값만 바꾸면 R1 의 노드는 여전히 R2 뒤에 있어 R2 가 깬다 (FreeRTOS V11.3.2, tasks.c:2972–2986). 이때 리스트의 "값 오름차순" 불변식이 깨지므로 이 연습만 불변식 검사를 껐다 — 실제 FreeRTOS 리스트도 이 순간 값과 위치가 어긋나 있다. 다시 정렬하는 커널(대안)이면 R1(p5) 이 먼저. 면접 답: "대기 순서는 줄 선 순간의 우선순위로 정해지고, FreeRTOS 는 그 뒤 변경을 반영하지 않는다."

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 14/14 passed
./practice -v                                                                # 위 타임라인들
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 14/14, 보고 없음
```

## 10. 핵심 정리

- 큐 = 링 버퍼 + **대기 리스트 2개**(보내기/받기). 대기 리스트 값 = `MAX_PRIO - prio`, 오름차순 정렬 삽입 + 같은 값 뒤 → "prio 높은 순, 같으면 FIFO" (tasks.c:1929, list.c:192).
- TCB 노드 2개: `xStateListItem`(지연 리스트, 깰 tick) + `xEventListItem`(대기 리스트). 데이터로 깨든 시간으로 깨든, 정지·삭제·abort 든 **두 리스트 모두에서** 뺀다.
- send/receive 는 대기자 맨 앞 **1명만** 깨우고 아이템을 넘겨주지 않는다. 깨어난 태스크는 `for(;;)` 루프 처음으로 돌아가 다시 확인한다 — 뺏겼으면 다시 잔다.
- 재블록은 `xTaskCheckForTimeOut` 의 **남은 시간**으로. 마감 = 첫 호출 + tmo, 절대 안 밀린다 (B1: 10, FreeRTOS 데모: 100 → 80, 총 100).
- tie 규칙: 타임아웃은 tick ISR 이 아니라 태스크가 돌 때 결정 → 그때 데이터가 있으면 데이터가 이긴다 (마감 tick 의 ISR send = pdPASS).
- tmo 0 생산자는 줄에 서지 않으므로 우선순위가 높아도 기다리는 생산자에게 자리를 뺏긴다 (Part C: CTRL 35/60 버림). 지속 과부하는 깊이가 아니라 소비 속도로 푼다.
- 검증: 매 tick 불변식(BLOCKED·정렬·count·lost wakeup·마감) + 무작위 300 세트 + 돌연변이로 검사 자체를 검사.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 — queue.c (`xQueueGenericSend` 949–1164, `xQueueReceive` 1509–1656: 데이터 확인 1541, 블록 1616, 재시도 1628–1634, 타임아웃 재확인 1636–1648, `xQueuePeek` 1884 · 다음 대기자 깨움 1927–1935, 큐 잠금 주석 176–182), tasks.c (`vTaskPlaceOnEventList` 5402, `xTaskRemoveFromEventList` 5500, `xTaskIncrementTick` 만료 4871–4960 · 틱 훅 5006, `xTaskCheckForTimeOut` 5693, `prvAddCurrentTaskToDelayedList` 8685, `vTaskSuspend` 3197, `vTaskDelete` 2212, `xTaskAbortDelay` 4737, `vTaskPrioritySet` 이벤트 값 2972–2986), list.c (`vListInsert` 139) — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS 문서 (freertos.org) — Queue Management, `xQueueReceive` / `xQueueSend` / `xTaskAbortDelay` API, Trace Hook Macros — <https://www.freertos.org/>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — 4장 Queue Management (대기 리스트 순서, 블록 시간)
- Zephyr Project 문서 — Message Queues (`k_msgq_put` / `k_msgq_get`, 반환값) — <https://docs.zephyrproject.org/latest/kernel/services/data_passing/message_queues.html>
- ARM CMSIS-RTOS2 — Message Queue (`osMessageQueuePut` / `osMessageQueueGet`) — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/index.html>
- 관련 레슨: RTOS-B09 (대기 리스트 세마포어, barging vs handoff), RTOS-B11 (복사 큐, 가득 참 정책), RTOS-B06·M07 (지연 리스트·tick wrap), RTOS-M01 (태스크 알림), RTOS-M02 (FromISR, 큐 잠금과 ISR), RTOS-M04 (상속과 이벤트 리스트 값), RTOS-M13 (list.c·ready 리스트 내부)

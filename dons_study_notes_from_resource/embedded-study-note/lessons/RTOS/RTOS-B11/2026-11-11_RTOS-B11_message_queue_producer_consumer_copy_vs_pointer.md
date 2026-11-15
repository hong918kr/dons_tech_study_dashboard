---
id: RTOS-B11
time: 70분
updated: 2026-11-11
---
# RTOS-B11 메시지 큐: 생산자-소비자, 타임아웃, 복사 vs 포인터

## 학습 목표

- FreeRTOS 큐가 **복사 큐**(send 때 `uxItemSize` 바이트를 큐 저장소로 `memcpy`, receive 때 다시 밖으로 `memcpy`)라는 것을 직접 구현해 보이고, 그래서 "보낸 뒤 원본을 바로 재사용해도 안전하다" 를 설명한다.
- 가득 참 정책 네 가지(**막기 / 최신 버리기 = timeout 0 / 타임아웃 / 길이 1 덮어쓰기**)를 같은 센서 → 처리 → 로깅 파이프라인에 걸어 보고, 잃는 개수·센서 지연·남는 seq 를 숫자로 비교한다. 모델과 진짜 FreeRTOS(V11.3.2 POSIX 포트)가 같은 숫자를 낸다.
- 큐 깊이가 "가끔 오는 긴 지연(stall)" 을 얼마나 흡수하는지 재고(깊이 1 → 5개 유실, 깊이 6 → 0), 버스트 흡수 최소 깊이 공식 `B − ⌈(B−1)/C⌉` 을 증명·검증한다.
- 큰 버퍼는 **포인터 + 고정 블록 풀** 로 넘기고, "포인터와 함께 소유권이 넘어간다 / send 가 실패하면 소유권은 그대로" 규칙과 그 규칙을 어길 때의 버그(누수, use-after-send)를 재현한다.
- `xQueueSendFromISR` 는 절대 기다리지 않는다(꽉 차면 즉시 `errQUEUE_FULL`) — ISR 쪽 생산자의 정책은 버리기·덮어쓰기뿐임을 안다.

## 1. 문제

"태스크끼리 데이터를 어떻게 넘기나요? 큐가 꽉 차면? 1 KB 프레임도 큐로 넘기나요?" — RTOS 면접에서 세마포어 다음으로 반드시 나오는 질문이고, 답에서 **복사 의미론·가득 참 정책·소유권** 세 가지를 다 말하는지가 경력의 차이로 보인다.

실제 펌웨어 모양은 거의 같다. nRF52/STM32 의 IMU 태스크가 SPI 로 샘플을 읽어 큐에 넣고, 필터 태스크가 받아 자세를 계산해 다음 큐에 넣고, 가장 낮은 로깅 태스크가 UART 나 플래시에 쓴다. 로깅이 가끔 오래 걸리면(플래시 페이지 지우기 수십 ms) 그 동안 쌓이는 샘플을 **어디에 두고, 넘치면 무엇을 버릴지** 가 설계다. 그리고 큰 데이터(DMA 로 받은 IMU FIFO 256 B, 오디오 프레임)는 큐에 복사하지 않고 포인터만 넘긴다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part B: 복사 큐 파이프라인 (작은 항목) — Part C: 포인터 + 풀 (큰 버퍼)</text>
  <rect x="10" y="36" width="120" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor">센서 S (prio 3)</text><text x="20" y="78" fill="currentColor">3 틱마다 16 B</text>
  <rect x="160" y="44" width="110" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="170" y="69" fill="currentColor" font-family="monospace">q_raw [16B×d]</text>
  <rect x="300" y="36" width="120" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="310" y="58" fill="currentColor">처리 P (prio 2)</text><text x="310" y="78" fill="currentColor">1 틱 계산</text>
  <rect x="450" y="44" width="110" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="460" y="69" fill="currentColor" font-family="monospace">q_log [12B×d]</text>
  <rect x="590" y="36" width="140" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="600" y="58" fill="currentColor">로깅 L (prio 1)</text><text x="600" y="78" fill="currentColor">1 틱, 가끔 12 틱</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M130 64 L158 64"/><path d="M270 64 L298 64"/><path d="M420 64 L448 64"/><path d="M560 64 L588 64"/>
  </g>
  <text x="10" y="116" fill="currentColor">send = memcpy 로 큐 안에 사본 → 보낸 직후 원본 재사용 OK. 꽉 차면? → 막기/버리기/덮어쓰기</text>
  <rect x="10" y="140" width="150" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="162" fill="currentColor">풀: 블록 4개</text><text x="20" y="182" fill="currentColor">(정적 배열 + free list)</text>
  <rect x="200" y="140" width="130" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="210" y="162" fill="currentColor">생산자 (prio 3)</text><text x="210" y="182" fill="currentColor">256 B 채우기</text>
  <rect x="370" y="148" width="130" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="380" y="173" fill="currentColor" font-family="monospace">q_ptr [8B×d]</text>
  <rect x="540" y="140" width="190" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="550" y="162" fill="currentColor">소비자 (prio 1)</text><text x="550" y="182" fill="currentColor">7 틱 처리 → 풀에 반납</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M160 160 L198 160"/><path d="M330 168 L368 168"/><path d="M500 168 L538 168"/>
    <path d="M635 196 L635 230 L85 230 L85 198"/>
  </g>
  <text x="166" y="152" fill="currentColor" font-size="12">alloc</text><text x="336" y="160" fill="currentColor" font-size="12">&amp;b</text>
  <text x="300" y="248" fill="currentColor" font-size="12">free (다 쓴 쪽이 반납)</text>
  <rect x="10" y="258" width="720" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="280" fill="currentColor">큐는 포인터 8 B 만 복사, 블록 256 B 는 제자리 — 대신 "지금 누가 주인인가" 를 코드가 지킨다.</text>
</svg>
```

호스트(맥)에는 진짜 RTOS 타이밍이 없으니 RTOS-B08 과 같은 두 층을 쓴다.

1. **체크되는 코드(starter/solution/practice)** — 단일 OS 스레드의 **틱 단위 결정적 미니 커널**. 태스크는 한 틱에 한 걸음 도는 상태 기계, 항상 가장 높은 READY 태스크가 돈다. 한 틱의 순서: ① `kernel_tick()` (타임아웃·지연 만료) → ② 스케줄러 → ③ (있으면) 장치 ISR → ④ 태스크 한 걸음. 걸음 도중 막히거나 **더 높은 태스크를 깨우면** 이 틱에 아직 안 돈 다음 태스크가 이어 받는다(= `queueYIELD_IF_USING_PREEMPTION`). "일" 은 CPU 틱으로 센다 — 선점당한 틱은 일로 치지 않는다. 타임라인: `R` 실행, `r` READY 인데 CPU 없음, `B` 막힘, `qRaw/qLog` 줄은 틱 끝의 큐 항목 수.
2. **진짜 FreeRTOS** — `demo_freertos_queue.c`, `demo_freertos_pool.c`. 같은 시나리오를 시간만 K=10 배로 늘려 돌린다. CPU 시간은 틱 훅(틱 ISR 안)이 "지금 돌던 태스크" 에 틱을 매겨서 센다. 실시간성은 없으므로 **반환값·순서·개수** 를 비교한다.

### Part A — 복사 큐 (`kq_send`, `kq_receive`, `kq_send_from_isr`)

- `kq_send(q, item, self, timeout)` — 자리가 있으면 `item` 의 `item_size` 바이트를 `wr` 칸에 `memcpy`, `wr` 전진(끝이면 0), `count++`, 받으려고 막혀 있던 태스크가 있으면 깨우고(`wake_waiter`) 그게 더 높으면 `g_resched = true` → `Q_OK`. 꽉 찼고 `timeout == 0` → `Q_FULL`(아무것도 안 바꿈). 아니면 `task_block(…, DIR_SEND, timeout)` → `Q_BLOCKED`.
- `kq_receive(q, out, self, timeout)` — 대칭. 비었고 timeout 0 이면 `Q_EMPTY` 이고 `out` 은 건드리지 않는다. 꺼냈으면 보내려고 막혀 있던 태스크를 깨운다.
- `kq_send_from_isr(q, item, &woken)` — 절대 막히지 않는다. 깨운 태스크가 끼어든 태스크(`g_cur`)보다 높을 때만 `*woken = true`, false 로는 안 되돌린다 (RTOS-B08 의 give 와 같은 규칙).

### Part B — 깊이·타임아웃·가득 참 정책 (`kq_overwrite` + 실험)

- `kq_overwrite(q, item)` — 길이 1 큐 전용. 비었으면 넣고, 차 있으면 덮어쓰고 `overwritten++`. 절대 막히지 않는다. 길이 ≠ 1 이면 거부(`Q_FULL`, FreeRTOS 는 `configASSERT`).
- 파이프라인(위 그림): 12 샘플, 주기 3, 처리 1, 로깅 1 — 단 seq 2 의 로깅만 12 틱(플래시 지우기). 정책 `BLOCK`(portMAX_DELAY) / `DROP`(timeout 0) / `TIMEOUT`(2 틱) / `OVERWRITE`(q_log 길이 1) 과 깊이 4/2, 1/1, 그리고 log 깊이 1..7 스윕. 잃은 수·센서 최대 지연·로그된 seq 를 확인한다.

### Part C — 큰 버퍼: 포인터 + 고정 블록 풀 (`pool_alloc`, `pool_free`, `submit_frame`)

- `pool_alloc` — free list 머리에서 하나 떼어 `in_use` 표시. 비었으면 `NULL` (기다리지 않음). `min_free` 로 최악 사용량을 기록.
- `pool_free` — 이 풀의 블록이 아니거나(`NULL` 포함) 이미 free 면 `false`(이중 해제 탐지), 아니면 free list 에 붙인다. 블록 찾기는 `==` 비교 (다른 객체를 가리킬 수도 있는 포인터에 `<`/`>` 는 정의되지 않은 동작, C11 6.5.8p5).
- `submit_frame` — alloc → 256 B 채우기 → 포인터를 send(timeout 0). 성공하면 **소유권이 소비자에게** 넘어간다. 실패(`Q_FULL`)면 소유권은 **그대로 생산자** — 스스로 `pool_free` 해야 새지 않는다.
- 테스트: 소비자가 느릴 때(처리 7 > 주기 3) 큐 깊이 4 면 **풀이 먼저 마르고**(alloc 실패 3), 깊이 1 이면 **큐가 먼저 찬다**(send 실패 5) — 두 경우 모두 끝에 블록 4개가 다 돌아와야 한다.

## 2. Problem (English)

> **Inter-task message queue: copy semantics, full-queue policies, and zero-copy hand-off.**
>
> You get a deterministic, tick-driven single-CPU mini kernel (highest-priority READY task runs; a queue operation that wakes a higher-priority task lets it run in the same tick). Implement a fixed-size **copy** queue `{buf, item_size, len, count, rd, wr, tx_waiter, rx_waiter}` with one waiter slot per direction.
>
> 1. `q_rc_t kq_send(kqueue_t *q, const void *item, int self, int timeout)` and `q_rc_t kq_receive(kqueue_t *q, void *out, int self, int timeout)` — copy `item_size` bytes in/out of a ring buffer; wake the waiter on the other side; `timeout == 0` returns `Q_FULL` / `Q_EMPTY` immediately without side effects; otherwise block and return `Q_BLOCKED` (the caller retries after waking — like FreeRTOS's `for(;;)` loop in `xQueueGenericSend`).
> 2. `q_rc_t kq_send_from_isr(kqueue_t *q, const void *item, bool *woken)` — never blocks; sets `*woken` only if the woken task outranks the interrupted one.
> 3. `q_rc_t kq_overwrite(kqueue_t *q, const void *item)` — length-1 mailbox; reject other lengths.
> 4. `blk_t *pool_alloc(pool_t *)`, `bool pool_free(pool_t *, blk_t *)` (reject foreign pointers and double frees), and `bool submit_frame(int self, uint32_t seq)` — pass a 256-byte frame by pointer; on a failed send the producer still owns the block and must free it.
>
> Example (sensor every 3 ticks → process → logger; logging of seq 2 stalls 12 ticks; depths 1/1): `BLOCK` loses nothing but the sensor is 3 ticks late; `DROP` logs `0 1 2 3 9 10 11` (5 dropped); `OVERWRITE` logs `0 1 2 8 9 10 11` (5 overwritten).
>
> Follow-ups: why does FreeRTOS copy instead of passing references? What can an ISR do when the queue is full? How do you size the queue? Who frees the buffer if `xQueueSend` fails?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "10/57 passed", exit 1 — TODO 7곳을 채워 57/57 을 만든다
```

스텁은 늘 `Q_FULL`/`Q_EMPTY`/`NULL` 을 돌려주므로 태스크가 막히지 않고 READY 로 돌지만, 시뮬레이션 틱 수가 정해져 있어 멈추지 않는다. 미니 커널(`task_block`, `kernel_tick`, `wake_waiter`)은 RTOS-B08 에서 직접 짠 것과 같아서 주어진 코드다.

진짜 FreeRTOS 데모 (커널: `~/.esn_scratch/FreeRTOS-Kernel` V11.3.2. 이 폴더의 `FreeRTOSConfig.h` 는 템플릿에서 `configUSE_TICK_HOOK` 만 1):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_queue.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o demo_queue && ./demo_queue          # 약 9 초
# demo_freertos_pool.c 도 같은 줄에서 파일 이름만 바꾼다 (-o demo_pool, 약 5 초)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_copy_cost.c -o demo_copy_cost && ./demo_copy_cost   # 복사 비용 표
```

## 4. 힌트

<details>
<summary>힌트 1 — 링의 칸 주소</summary>

칸 `i` 의 주소는 `q->buf + (size_t)i * q->item_size`. `item_size` 가 실행 시간 값이라 `memcpy` 크기도 변수다 — FreeRTOS 도 `( size_t ) pxQueue->uxItemSize` 로 같은 일을 한다. 인덱스 전진은 `% len` 대신 `(i + 1 == len) ? 0 : i + 1` (나눗셈 없는 MCU 에서도 싸다).

</details>

<details>
<summary>힌트 2 — 실패 경로는 "아무것도 안 바꾼다"</summary>

`Q_FULL`/`Q_EMPTY` 를 돌려줄 때는 `count`, 인덱스, `out` 버퍼 어느 것도 바뀌면 안 된다. 테스트는 빈 큐에서 `receive(…, 0)` 뒤에 `out` 이 그대로인지(777) 본다. 막을 때는 직접 상태를 바꾸지 말고 주어진 `task_block()` 을 부른다 — 대기자 칸 등록과 절대 마감 틱 계산을 해 준다.

</details>

<details>
<summary>힌트 3 — 누구를 깨우나</summary>

send 는 "받으려고 기다리던 쪽"(`DIR_RECV`)을, receive 는 "보내려고 기다리던 쪽"(`DIR_SEND`)을 깨운다. `wake_waiter()` 가 true 면 깨운 태스크가 지금 태스크보다 높다는 뜻. 태스크 문맥이면 `g_resched = true`(즉시 전환), ISR 문맥이면 `*woken = true`(전환은 ISR 끝의 `port_yield_from_isr` 가 결정).

</details>

<details>
<summary>힌트 4 — submit_frame 의 소유권</summary>

`kq_send(&g_qptr, &b, …)` — 큐에 들어가는 건 `b`(포인터 값 8 B)이지 블록이 아니다. 그래서 두 번째 인자가 `&b`. send 가 `Q_OK` 면 이 함수는 그 블록을 더 만지면 안 된다. `Q_FULL` 이면 아무도 그 블록을 모르므로, 여기서 `pool_free` 하지 않으면 영원히 샌다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨의 모델 | FreeRTOS (V11.3.2) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준) |
|---|---|---|---|
| `kq_init(q, buf, len, size)` | `xQueueCreateStatic(len, size, buf, &ctrl)` / `xQueueCreate(len, size)` | `k_msgq_init(&q, buf, size, len)` / `K_MSGQ_DEFINE` | `osMessageQueueNew(len, size, &attr)` |
| `kq_send(q, &x, me, t)` → `Q_OK` / `Q_FULL` | `xQueueSend(q, &x, t)` → `pdPASS` / `errQUEUE_FULL` | `k_msgq_put(&q, &x, K_MSEC(t))` → `0` / `-ENOMSG`(no wait) / `-EAGAIN`(timeout) | `osMessageQueuePut(q, &x, 0, t)` → `osOK` / `osErrorResource` / `osErrorTimeout` |
| `kq_receive(q, &x, me, t)` | `xQueueReceive(q, &x, t)` → `pdPASS` / `errQUEUE_EMPTY` | `k_msgq_get(&q, &x, t)` | `osMessageQueueGet(q, &x, NULL, t)` |
| `kq_overwrite(q, &x)` (길이 1) | `xQueueOverwrite(q, &x)` | 같은 API 없음 (`k_msgq_purge` + put 으로 흉내) | 없음 |
| `kq_send_from_isr(q, &x, &w)` | `xQueueSendFromISR(q, &x, &w)` + `portYIELD_FROM_ISR(w)` | ISR 에서 `k_msgq_put(…, K_NO_WAIT)` | ISR 에서 `osMessageQueuePut(…, 0)` |
| `kq_peek` (연습 3) | `xQueuePeek(q, &x, t)` | `k_msgq_peek` | — |
| `pool_alloc` / `pool_free` | 직접 구현 (정적 배열 + free list, 또는 연습 5 의 free-queue) | `k_mem_slab_alloc` / `k_mem_slab_free` | `osMemoryPoolAlloc` / `osMemoryPoolFree` |
| `FOREVER` | `portMAX_DELAY` | `K_FOREVER` | `osWaitForever` |

### Part A — send/receive: 자리 확인 → 복사 → 상대편 깨우기

```c
static q_rc_t kq_send(kqueue_t *q, const void *item, int self, int timeout) {
    if (q->count < q->len) {
        memcpy(q->buf + (size_t)q->wr * q->item_size, item, q->item_size);
        q->wr = (q->wr + 1 == q->len) ? 0 : q->wr + 1;
        q->count++;
        if (wake_waiter(q, DIR_RECV)) g_resched = true;
        return Q_OK;
    }
    if (timeout == 0) return Q_FULL;
    task_block(self, q, DIR_SEND, timeout);
    return Q_BLOCKED;
}
```

`kq_receive` 는 `rd` 칸에서 `out` 으로 복사하고 `DIR_SEND` 대기자를 깨우는 거울상이다. 진짜 FreeRTOS 도 같은 뼈대다 (V11.3.2 `queue.c`, 줄 번호는 실제 파일):

| 단계 | `xQueueGenericSend` (949~) | `xQueueReceive` (1509~) |
|---|---|---|
| 자리/항목 확인 (임계 구역 안) | 977 `if( ( uxMessagesWaiting < uxLength ) \|\| ( xCopyPosition == queueOVERWRITE ) )` | 1541 `if( uxMessagesWaiting > 0 )` |
| 복사 | 985/1043 `prvCopyDataToQueue` → 2425 `memcpy( pcWriteTo, pvItemToQueue, uxItemSize )` | 1544 `prvCopyDataFromQueue` → 2494 `memcpy( pvBuffer, pcReadFrom, uxItemSize )` |
| 상대편 깨우기 | 1047-1055 (queue set 켠 빌드는 1012-1016) `xTasksWaitingToReceive` 에서 하나 → 더 높으면 `queueYIELD_IF_USING_PREEMPTION()` | 1551-1555 `xTasksWaitingToSend` 에서 하나 → 같은 yield |
| 실패 (timeout 0) | 1085-1096 `return errQUEUE_FULL` | 1575-1584 `return errQUEUE_EMPTY` |
| 막기 | 1126 `vTaskPlaceOnEventList( &xTasksWaitingToSend, xTicksToWait )` | 1616 `…( &xTasksWaitingToReceive, … )` |
| 타임아웃 만료 | 1154-1161 루프 위에서 자리를 한 번 더 본 뒤 `errQUEUE_FULL` | 1643-1648 `errQUEUE_EMPTY` |

복사가 FreeRTOS 의 공식 의미다: `queue.h:107` "Items are queued by copy, not by reference". 큐는 한 번의 할당으로 `sizeof(Queue_t) + len × itemSize` 를 잡는다 (`queue.c:529`, 곱셈·덧셈 오버플로 검사 512-518). 막힌 태스크는 이벤트 리스트에 **우선순위 순서로** 들어가고(`tasks.c:5413-5423`, `vListInsert`), 같은 우선순위면 먼저 온 쪽이 앞이다(`list.c:155-157`). 이 모델은 방향별 대기자 1칸만 두었다 — 여러 생산자·소비자의 대기 리스트는 RTOS-M06 의 몫.

테스트가 확인하는 핵심 (`[Part A]`): 보낸 뒤 원본 `a` 를 `seq=99, raw0=-1` 로 바꿔도 큐 안 사본과 받은 값은 `seq=1, raw0=100`; 4번째 send(timeout 0)는 `Q_FULL` 이고 `count` 3 그대로; 링 끝을 20 라운드 넘나들어도 FIFO; `receive(…, 5)` 는 10 틱에 막혀 마감 15, 15 틱 `kernel_tick` 에서 `WAKE_TIMEOUT`; 낮은 L 의 send 가 높은 H 를 깨우면 `g_resched`, 높은 H 의 receive 가 낮은 L 을 깨우면 `g_resched` 아님.

**진짜 FreeRTOS 도 같은가** — `demo_freertos_queue.c` [1] 실제 출력 (Apple M6, Apple clang 21.0.0):

<!-- code: demo_freertos_queue.c -->

```text
[1] API semantics
send x4 (depth 3, timeout 0) -> 1 1 1 0 (errQUEUE_FULL=0); waiting=3 spaces=0 itemsize=16
send (full, timeout 5)       -> 0 after 5 ticks blocked
receive #1                   -> seq=1 raw0=100   (copy taken at send time)
receive #2, #3               -> seq=2 seq=3
receive (empty, timeout 0)   -> 0 (errQUEUE_EMPTY=0), buffer seq still 777
receive (empty, timeout 5)   -> 0 after 5 ticks blocked
overwrite x3 on length-1     -> 1 1 1; waiting=1
peek                         -> seq=3, waiting=1 (peek does not remove)
receive                      -> seq=3, waiting=0
```

`errQUEUE_FULL` 과 `errQUEUE_EMPTY` 는 둘 다 0 (`projdefs.h:61-62`) — `pdPASS` 가 아니면 실패로 세면 된다.

**ISR 판** — 타이머 ISR 이 2..13 틱에 1개씩, 받는 태스크는 항목당 3 틱, 깊이 4:

```text
  -- ISR sends every tick 2..13, task needs 3 ticks/item, depth 4
  tick : 01234567890123456789012345
  rx   : RBRRRRRRRRRRRRRRRRRRRRRRRR
  bg   : RRrrrrrrrrrrrrrrrrrrrrrrrr
  q    : 00012234344344333222111000
  isr ok 8 full 4 woken 1, task got 8
```

ISR 은 기다릴 곳이 없으니 큐가 4 로 꽉 차 있는 동안 온 4개는 즉시 `Q_FULL` 로 버려진다. `woken` 은 rx 가 빈 큐에서 막혀 있던 2 틱에 딱 1번 — 그 뒤로 rx 는 늘 일하는 중이라 깨울 대기자가 없다. FreeRTOS 실측도 `isr ok 8 full 4 woken 1, task got 8` 로 같다 (틱 훅 안의 `xQueueSendFromISR`). 소스: `xQueueGenericSendFromISR` (`queue.c:1167`) 는 자리 확인(1207) 후 실패면 1327 `errQUEUE_FULL` — 블록 경로 자체가 없다. "더 높은 태스크를 깨웠다" 는 `*pxHigherPriorityTaskWoken = pdTRUE` 로만 알리고, 실제 전환은 `portYIELD_FROM_ISR` (Cortex-M: PendSV, RTOS-B08/M02). 8 = 12 − 4 인 이유는 연습 2 의 공식으로 나온다 (B=12, C=3 → 필요 깊이 8, 가진 깊이 4 → 4개 부족).

### Part B — 정책 실험: 같은 stall, 다른 결과

`kq_overwrite` 는 짧다:

```c
static q_rc_t kq_overwrite(kqueue_t *q, const void *item) {
    if (q->len != 1) return Q_FULL;
    if (q->count == 1) q->overwritten++;
    memcpy(q->buf, item, q->item_size);
    q->count = 1; q->rd = q->wr = 0;
    if (wake_waiter(q, DIR_RECV)) g_resched = true;
    return Q_OK;
}
```

FreeRTOS: `xQueueOverwrite` = `xQueueGenericSend(q, p, 0, queueOVERWRITE)` (`queue.h:597-598`) — block time 0 고정. 길이 ≠ 1 은 `configASSERT` (`queue.c:962`). 덮어쓸 때 개수를 하나 빼고 다시 더해 1 을 유지한다 (`queue.c:2451-2466`).

**여유 있는 기준** (stall 없음): 12개 전부, 센서 지연 0, 큐는 늘 0 — `P` 는 받는 틱 + 끝내는 틱, `L` 은 그 다음 틱에 기록한다.

**stall + 깊이 4/2, BLOCK** — 안 잃고 센서도 안 늦다. 큐가 부풀었다 빠지는 것이 보인다:

```text
  -- BLOCK, log stall 12 ticks on seq 2 (raw depth 4, log depth 2)
  tick : 01234567890123456789012345678901234567890123
  S    : RBBRBBRBBRBBRBBRBBRBBRBBRBBRBBRBBRBBBBBBBBBB
  P    : RRBRRBRRBRRBRRBRRBBBBBRRRRRRRRRRRRRBBBBBBBBB
  L    : rRRBRRBRRrRRrRRrRRRRRRRRrRrRrRrRRrRRRBBBBBBB
  qRaw : 00000000000000000011121111010000000000000000
  qLog : 00000000001112222222222121212121111000000000
```

**같은 stall + 깊이 1/1, BLOCK** — 여전히 안 잃지만 **역압(backpressure)** 이 센서까지 올라온다:

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">solution 이 찍은 타임라인: BLOCK, 깊이 1/1, seq 2 로깅 12 틱 (틱 6~33)</text>
  <text x="74" y="42" fill="currentColor" font-size="12" font-family="monospace">6</text><text x="140" y="42" fill="currentColor" font-size="12" font-family="monospace">9</text><text x="206" y="42" fill="currentColor" font-size="12" font-family="monospace">12</text><text x="272" y="42" fill="currentColor" font-size="12" font-family="monospace">15</text><text x="338" y="42" fill="currentColor" font-size="12" font-family="monospace">18</text><text x="404" y="42" fill="currentColor" font-size="12" font-family="monospace">21</text><text x="470" y="42" fill="currentColor" font-size="12" font-family="monospace">24</text><text x="536" y="42" fill="currentColor" font-size="12" font-family="monospace">27</text><text x="602" y="42" fill="currentColor" font-size="12" font-family="monospace">30</text><text x="668" y="42" fill="currentColor" font-size="12" font-family="monospace">33</text>
  <text x="10" y="65" fill="currentColor" font-family="monospace">S</text><rect x="70" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="92" y1="58" x2="114" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><line x1="114" y1="58" x2="136" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="136" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="158" y1="58" x2="180" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><line x1="180" y1="58" x2="202" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="202" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="224" y1="58" x2="246" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><line x1="246" y1="58" x2="268" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="268" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="290" y1="58" x2="312" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><line x1="312" y1="58" x2="334" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="334" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="356" y1="58" x2="378" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><line x1="378" y1="58" x2="400" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="400" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="422" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="444" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="466" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="488" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="510" y1="58" x2="532" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="532" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="554" y1="58" x2="576" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><line x1="576" y1="58" x2="598" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="598" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="620" y1="58" x2="642" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><line x1="642" y1="58" x2="664" y2="58" stroke="currentColor" stroke-dasharray="2 2"/><rect x="664" y="48" width="22" height="20" class="f-acc" stroke="currentColor"/>
  <text x="10" y="91" fill="currentColor" font-family="monospace">P</text><rect x="70" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="92" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="114" y1="84" x2="136" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><rect x="136" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="158" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="180" y1="84" x2="202" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><rect x="202" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="224" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="246" y1="84" x2="268" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><line x1="268" y1="84" x2="290" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><line x1="290" y1="84" x2="312" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><line x1="312" y1="84" x2="334" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><line x1="334" y1="84" x2="356" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><line x1="356" y1="84" x2="378" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><line x1="378" y1="84" x2="400" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><rect x="400" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="422" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="444" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="466" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="488" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="510" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="532" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="554" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="576" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="598" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="620" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="642" y1="84" x2="664" y2="84" stroke="currentColor" stroke-dasharray="2 2"/><rect x="664" y="74" width="22" height="20" class="f-acc" stroke="currentColor"/>
  <text x="10" y="117" fill="currentColor" font-family="monospace">L</text><line x1="70" y1="110" x2="92" y2="110" stroke="currentColor" stroke-dasharray="2 2"/><rect x="92" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="114" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="136" y="100" width="22" height="20" class="f-mut" stroke="currentColor"/><rect x="158" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="180" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="202" y="100" width="22" height="20" class="f-mut" stroke="currentColor"/><rect x="224" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="246" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="268" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="290" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="312" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="334" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="356" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="378" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="400" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="422" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="444" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="466" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="488" y="100" width="22" height="20" class="f-mut" stroke="currentColor"/><rect x="510" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="532" y="100" width="22" height="20" class="f-mut" stroke="currentColor"/><rect x="554" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="576" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="598" y="100" width="22" height="20" class="f-mut" stroke="currentColor"/><rect x="620" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><rect x="642" y="100" width="22" height="20" class="f-acc" stroke="currentColor"/><line x1="664" y1="110" x2="686" y2="110" stroke="currentColor" stroke-dasharray="2 2"/>
  <text x="10" y="143" fill="currentColor" font-family="monospace">qRaw</text><text x="77" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="99" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="121" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="143" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="165" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="187" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="209" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="231" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="253" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><rect x="268" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="275" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="290" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="297" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="312" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="319" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="334" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="341" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="356" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="363" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="378" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="385" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="400" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="407" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="422" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="429" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="444" y="126" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="451" y="141" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="473" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="495" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="517" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="539" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="561" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="583" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="605" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="627" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="649" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="671" y="141" fill="currentColor" font-size="12" font-family="monospace">0</text>
  <text x="10" y="169" fill="currentColor" font-family="monospace">qLog</text><text x="77" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="99" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="121" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="143" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><rect x="158" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="165" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="180" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="187" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="202" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="209" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="224" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="231" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="246" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="253" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="268" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="275" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="290" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="297" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="312" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="319" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="334" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="341" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="356" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="363" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="378" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="385" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><rect x="400" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="407" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="429" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="451" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="473" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><rect x="488" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="495" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="517" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><rect x="532" y="152" width="22" height="20" class="f-warn" stroke="currentColor"/><text x="539" y="167" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="561" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="583" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="605" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="627" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="649" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="671" y="167" fill="currentColor" font-size="12" font-family="monospace">0</text>
  <path d="M334 46 L334 182" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M400 46 L400 182" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="304" y="198" fill="currentColor" font-size="12">18: S 릴리즈, q_raw 꽉 참 → 막힘</text>
  <text x="404" y="214" fill="currentColor" font-size="12">21: 겨우 넣음 (3 틱 = 한 주기 늦음)</text>
  <rect x="10" y="224" width="16" height="12" class="f-acc" stroke="currentColor"/><text x="32" y="235" fill="currentColor" font-size="12">실행(R)</text>
  <rect x="100" y="224" width="16" height="12" class="f-mut" stroke="currentColor"/><text x="122" y="235" fill="currentColor" font-size="12">READY 인데 CPU 없음(r)</text>
  <line x1="290" y1="230" x2="306" y2="230" stroke="currentColor" stroke-dasharray="2 2"/><text x="312" y="235" fill="currentColor" font-size="12">막힘(B)</text>
  <rect x="380" y="224" width="16" height="12" class="f-warn" stroke="currentColor"/><text x="402" y="235" fill="currentColor" font-size="12">큐 안 항목 수 (틱 끝)</text>
</svg>
```

로거가 seq 2 에 묶인 동안 q_log(1) 가 차고 → P 가 seq 4 를 q_log 에 send 하다 막히고(14~20 틱 `B`) → q_raw(1) 가 차고 → 18 틱에 릴리즈된 센서가 q_raw 에 막힌다. 샘플 6 은 21 틱에야 들어가고, 7·8 도 2, 1 틱 늦다(따라잡기). 데이터는 안 잃었지만 센서가 큐에 넣은 시각이 15 → 21 → 23 → 25 로, **간격이 3 이 아니라 6, 2, 2** 가 됐다 — 제어 루프나 디지털 필터에는 이게 유실보다 나쁠 수 있다.

**네 정책 한 표** (stall 동일, 깊이 1/1 — 모델 출력과 FreeRTOS `demo_freertos_queue.c` [2] 출력):

| 정책 | 잃음 | 센서 최대 지연 | 로그된 seq | FreeRTOS (K=10) |
|---|---|---|---|---|
| BLOCK, 깊이 4/2 | 0 | 0 | 0..11 | 같음, 지연 0 |
| BLOCK, 깊이 1/1 | 0 | **3 틱** (한 주기) | 0..11 | 같음, 지연 30 틱 = 3.0 K (5회 중 1회 32) |
| DROP (timeout 0) | 5 (P 에서) | 0 | `0 1 2 3 9 10 11` | 같음 |
| TIMEOUT 2 틱 | 4 | 0 | `0 1 2 3 8 9 10 11` | 같음 |
| OVERWRITE (q_log 길이 1) | 5 덮어씀 | 0 | `0 1 2 8 9 10 11` | 같음 |

`demo_freertos_queue.c` [2] 실제 출력:

```text
policy    depth produced logged sdrop pdrop ow maxlate(/K) ord data  logged seqs
BLOCK     4/2       12    12     0   0      0      0 (0.0)  ok  ok 
-- stall: logging of seq 2 takes 120 ticks
BLOCK     4/2       12    12     0   0      0      0 (0.0)  ok  ok  0 1 2 3 4 5 6 7 8 9 10 11
BLOCK     1/1       12    12     0   0      0     30 (3.0)  ok  ok  0 1 2 3 4 5 6 7 8 9 10 11
DROP      1/1       12     7     0   5      0      0 (0.0)  ok  ok  0 1 2 3 9 10 11
TIMEOUT2  1/1       12     8     0   4      0      0 (0.0)  ok  ok  0 1 2 3 8 9 10 11
OVERWRITE 1/1       12     7     0   0      5      0 (0.0)  ok  ok  0 1 2 8 9 10 11
```

5회 실행에서 개수·seq·반환값은 매번 같았고, `maxlate` 만 틱 단위로 흔들렸다 (BLOCK 1/1 은 30 또는 32, DROP 1/5 는 0 또는 1) — POSIX 포트는 순서를 보는 도구지 타이밍 도구가 아니다. 데모는 태스크·큐를 한 번만 만들어 재사용한다: 처음 판은 실행마다 다른 태스크에서 `vTaskDelete` 했는데 5회 중 1회 멈췄다. `sample` 로 본 스택은 `controller → vTaskDelete → vPortCancelThread → event_signal → pthread_mutex_lock` 대기 (`port.c:531-541`) — 취소된 스레드가 쥐고 있던 mutex 를 되살리는 코드가 `#ifndef __APPLE__` 로 빠져 있다 (`wait_for_event.c:126-133`). 실제 MCU 포트와는 상관없는 호스트 포트의 한계다.

읽는 법:
- **DROP 은 "새 것" 을 버린다** (drop-newest): 큐 안의 3 이 남고 그 뒤 4..8 이 버려졌다. 로그·이벤트 기록처럼 "처음 생긴 일" 이 중요할 때 맞다.
- **OVERWRITE 는 "옛 것" 을 버린다**: 3..7 이 덮이고 8 이 남았다. "지금 자세/지금 온도" 처럼 **최신 상태만 의미 있는** 소비자(디스플레이, 제어 설정값)에 맞다. 깊이 1 의 drop-oldest 와 같다 (연습 1).
- **TIMEOUT** 은 그 사이: 2 틱 기다려서 seq 8 하나를 더 살렸고, 센서 지연은 타임아웃으로 묶인다 (생산자가 기다리는 최대 시간 = timeout).
- **BLOCK** 은 데이터를 지키는 대신 지연을 위로 미룬다. 맨 위 생산자가 ISR 이면 BLOCK 은 선택지조차 아니다.

**깊이 스윕** (DROP, q_raw 1, q_log d): 실측 `drops : 5 4 3 2 1 0 0` — FreeRTOS 도 `5 4 3 2 1 0 0`.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">q_log 깊이 d 에 따른 유실 수 (DROP, 같은 stall) — 실측, 모델 = FreeRTOS</text>
  <line x1="80" y1="220" x2="660" y2="220" stroke="currentColor"/><line x1="80" y1="60" x2="80" y2="220" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="62" y="74">5</text><text x="62" y="164">2</text><text x="62" y="224">0</text></g>
  <g stroke="currentColor">
    <rect x="100" y="70" width="50" height="150" class="f-warn"/><rect x="180" y="100" width="50" height="120" class="f-warn"/>
    <rect x="260" y="130" width="50" height="90" class="f-warn"/><rect x="340" y="160" width="50" height="60" class="f-warn"/>
    <rect x="420" y="190" width="50" height="30" class="f-warn"/>
    <rect x="500" y="216" width="50" height="4" class="f-ok"/><rect x="580" y="216" width="50" height="4" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="120" y="64">5</text><text x="200" y="94">4</text><text x="280" y="124">3</text><text x="360" y="154">2</text><text x="440" y="184">1</text>
    <text x="520" y="210">0</text><text x="600" y="210">0</text>
    <text x="114" y="238">d=1</text><text x="196" y="238">2</text><text x="276" y="238">3</text><text x="356" y="238">4</text>
    <text x="436" y="238">5</text><text x="516" y="238">6</text><text x="596" y="238">7</text>
  </g>
  <text x="10" y="266" fill="currentColor">stall 동안 도착하는 샘플 = seq 3..8 = 6개. 큐에 d 개가 남고 나머지는 버려진다 → 유실 = max(0, 6 − d).</text>
</svg>
```

왜 6개인가: 로거는 seq 2 를 7 틱에 받아 12 **CPU** 틱을 써야 하는데, 3 틱 중 1 틱은 처리 태스크가 쓰므로 실제로는 25 틱에야 끝난다. 그 사이 9, 12, …, 24 틱에 seq 3..8 이 도착한다. DROP 에서는 P 가 막히지 않으니 q_raw 는 늘 비고(타임라인 `qRaw` 0), q_log 의 d 칸만 버퍼다. BLOCK 4/2 가 지연 0 이었던 것도 같은 계산: 버퍼 = q_raw 4 + q_log 2 + P 가 손에 쥔 1 = 7 ≥ 6. 1/1 이면 3 < 6 → 센서까지 밀린다. 깊이 설계 규칙: **최악 stall 동안의 도착 수 ≤ 버퍼 칸 수의 합** (연습 2 에서 버스트에 대해 정확한 공식).

### Part C — 포인터 + 풀: 소유권을 주고받는다

```c
static blk_t *pool_alloc(pool_t *p) {
    blk_t *b = p->free_head;
    if (b == NULL) return NULL;
    p->free_head = b->next;
    b->next = NULL;
    p->in_use[b - p->blocks] = true;
    p->nfree--;
    if (p->nfree < p->min_free) p->min_free = p->nfree;
    return b;
}
static bool submit_frame(int self, uint32_t seq) {
    blk_t *b = pool_alloc(&g_pool);
    if (b == NULL) { CR.drop_nomem++; return false; }
    b->seq = seq;
    for (unsigned i = 0; i < BLK_SIZE; i++) b->data[i] = pat(seq, i);
    if (kq_send(&g_qptr, &b, self, g_ctimeout) == Q_OK) { CR.sent++; return true; }
    if (!pool_free(&g_pool, b)) CR.leaked_free_fail++;
    CR.drop_full++;
    return false;
}
```

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">블록 하나의 일생 — 어느 순간에도 주인은 정확히 하나</text>
  <rect x="10" y="40" width="130" height="50" rx="6" class="f-ok" stroke="currentColor"/><text x="22" y="70" fill="currentColor">풀 (free list)</text>
  <rect x="200" y="40" width="150" height="50" rx="6" class="f-acc" stroke="currentColor"/><text x="212" y="62" fill="currentColor">생산자 소유</text><text x="212" y="80" fill="currentColor" font-size="12">채운다 (쓰기 OK)</text>
  <rect x="410" y="40" width="130" height="50" rx="6" class="f-warn" stroke="currentColor"/><text x="422" y="62" fill="currentColor">큐 안 (포인터)</text><text x="422" y="80" fill="currentColor" font-size="12">아무도 만지지 않음</text>
  <rect x="600" y="40" width="130" height="50" rx="6" class="f-acc" stroke="currentColor"/><text x="612" y="62" fill="currentColor">소비자 소유</text><text x="612" y="80" fill="currentColor" font-size="12">읽고 처리</text>
  <g stroke="currentColor" stroke-width="2" fill="none"><path d="M140 65 L198 65"/><path d="M350 65 L408 65"/><path d="M540 65 L598 65"/><path d="M665 90 L665 130 L75 130 L75 92"/><path d="M275 90 L275 112 L110 112 L110 92"/></g>
  <g fill="currentColor" font-size="12"><text x="150" y="58">alloc</text><text x="356" y="58">send OK</text><text x="546" y="58">receive</text><text x="380" y="146">다 쓴 소비자가 free</text><text x="150" y="108">send 실패 → 생산자가 free</text></g>
  <rect x="10" y="160" width="720" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="178" fill="currentColor">버그 1: send 실패 경로에서 free 안 함 → 누수 (풀이 하나씩 마른다)</text>
  <text x="22" y="194" fill="currentColor">버그 2: send OK 뒤에 블록에 씀 (use-after-send) → 소비자 우선순위에 따라 보였다 안 보였다 (연습 4)</text>
</svg>
```

실제 출력 (소비자 느림: 주기 3, 처리 7, 풀 4):

```text
  -- pointer queue, slow consumer (period 3, cost 7, pool 4, qdepth 4)
  tick : 0123456789012345678901234567890123456789
  prod : RBBRBBRBBRBBRBBRBBRBBRBBRBBRBBBBBBBBBBBB
  cons : RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
  qPtr : 0001112112223323333332223333222222211111
  qdepth 4: produced 10 sent 7 drop_nomem 3 drop_full 0 consumed 7 min_free 0
  qdepth 1: produced 10 sent 5 drop_nomem 0 drop_full 5 consumed 5 min_free 1
```

깊이 4 에서 큐는 최대 3 — 소비자 손에 1개 + 큐에 3개 = 풀 4개 전부라서 **풀이 먼저 마른다** (`drop_nomem 3`). 풀 크기 ≤ 큐 깊이 + 1 이면 큐 깊이는 장식이다. 깊이 1 이면 큐가 먼저 차서 send 가 5번 실패하고, 그때마다 생산자가 반납해서 최대 사용 블록은 3, 끝에 4개 전부 복귀. FreeRTOS `demo_freertos_pool.c` (풀 = 빈 블록 포인터의 큐, 연습 5) 실제 출력이 숫자 그대로 같다:

<!-- code: demo_freertos_pool.c -->

```text
item size: copy queue would move 260 B per item; pointer queue moves 8 B
[1] pool of 4 blocks, producer every 30 ticks, N=10
cost  20 qdepth 2 prio p3/c1 bug 0 | produced 10 sent 10 nomem 0 dropfull 0 consumed 10 corrupt 0 | max in use 1, free at end 4/4
cost  70 qdepth 4 prio p3/c1 bug 0 | produced 10 sent  7 nomem 3 dropfull 0 consumed  7 corrupt 0 | max in use 4, free at end 4/4
cost  70 qdepth 1 prio p3/c1 bug 0 | produced 10 sent  5 nomem 0 dropfull 5 consumed  5 corrupt 0 | max in use 3, free at end 4/4
[2] use-after-send bug (producer writes data[0] after xQueueSend)
cost  20 qdepth 2 prio p3/c1 bug 1 | produced  6 sent  6 nomem 0 dropfull 0 consumed  6 corrupt 6 | max in use 1, free at end 4/4
cost  20 qdepth 2 prio p1/c3 bug 1 | produced  6 sent  6 nomem 0 dropfull 0 consumed  6 corrupt 0 | max in use 1, free at end 4/4
```

[2] 가 무서운 부분이다: **같은 버그 코드** 가 소비자가 낮으면 6/6 깨지고, 소비자가 높으면 0 — `xQueueSend` 안에서 높은 소비자로 즉시 전환되어(`queueYIELD_IF_USING_PREEMPTION`) 다 읽고 반납한 **뒤에** 생산자의 다음 줄이 돌기 때문이다. 그 쓰기는 이미 풀로 돌아간 블록을 망가뜨린다 — 다른 생산자(예: ISR)가 그 블록을 막 빌렸다면 그쪽 데이터가 깨진다. 우선순위 하나 바꾸면 나타나는 하이젠버그. 연습 4 는 블록에 주인 태그를 달아 두 경우 모두 6번 잡는다. (5회 실행 모두 같은 출력.)

**복사 비용** — `demo_copy_cost.c` (이 맥, -O2, send+receive 1쌍, 7회 중앙값; 절대값 말고 비율):

<!-- code: demo_copy_cost.c -->

| 항목 크기 | ns / (send+receive) | 8 B(포인터) 대비 | 1쌍이 복사하는 바이트 |
|---|---|---|---|
| 4 B | 4.50 | — | 8 |
| 8 B | 3.29 | 1.0 | 16 |
| 16 B | 4.14 | 1.3 | 32 |
| 64 B | 3.98 | 1.2 | 128 |
| 256 B | 7.93 | 2.4 | 512 |
| 1024 B | 25.85 | 7.9 | 2048 |
| 4096 B | 103.69 | 31.5 | 8192 |

(두 번째 실행: 1024 B 6.9×, 4096 B 25.9× — 비율의 모양은 같다.) 64 B 까지는 고정 비용(인덱스·분기)이 지배해 거의 평평하고, 256 B 부터 크기에 비례한다. 펌웨어에서 더 중요한 점: FreeRTOS 의 복사는 **임계 구역 안에서** 일어난다 — `prvCopyDataToQueue` 첫 주석 "This function is called from a critical section" (`queue.c:2402`). Cortex-M 포트에서 임계 구역은 BASEPRI 로 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하 인터럽트를 막으므로, 1 KB 복사 시간만큼 그 인터럽트들의 지연이 늘어난다(MCU 에서의 절대 시간은 미측정 — 칩·클럭·버스에 따라 직접 재야 한다). 그래서 `queue.h:984-986` 도 "small items … preferable to store a pointer" 라고 쓴다. 대략의 선: 수십 바이트 이하는 복사(단순·안전), 수백 바이트 이상이거나 ISR 에서 보내면 포인터 + 풀.

### Part C — 실제 MCU 로 옮기면 (nRF52840 IMU 노드 예)

| 이 레슨 | 펌웨어 |
|---|---|
| 센서 S, 16 B 샘플, 복사 큐 | IMU 데이터 준비 GPIO 인터럽트 → `xQueueSendFromISR` 로 타임스탬프(8 B)만 → IMU 태스크가 SPI 로 읽어 샘플 16 B 를 `xQueueSend(q_raw, &s, 0)` (유실 카운터 증가) |
| 처리 P | 자세 필터(Madgwick 등) 태스크 → 결과를 BLE 알림용 **길이 1 우편함** 에 `xQueueOverwrite` (BLE 는 최신 자세만 원함) + 로그 큐에 `xQueueSend(…, 0)` |
| 로깅 L + stall | QSPI 플래시 로거. 섹터 지우기 동안 막힘 → q_log 깊이 = 최악 지우기 시간 × 샘플률 + 여유, 그리고 `uxQueueSpacesAvailable` 최솟값을 기록해 실측으로 검증 |
| 포인터 + 풀 | SPI DMA 로 IMU FIFO 256 B 를 정적 블록에 받음 → DMA 완료 ISR 이 `xQueueSendFromISR(q_ptr, &blk, &w)` → 처리 태스크가 끝나면 free-queue 로 반납. 블록은 DMA 가 닿는 RAM 영역 + 캐시 있는 칩(Cortex-M7)은 정렬·캐시 관리 (ARCH-M12) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 57/57 passed (5회 같은 출력, 0.003 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 57/57, 보고 없음
```

스레드를 쓰지 않으므로 TSan 은 의미가 없다 (`-pthread` 를 붙여도 57/57).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `xQueueCreate(8, sizeof(p))` 인데 `p` 가 포인터가 아니라 구조체를 넣을 생각 (또는 반대) | `uxItemSize` 가 복사 바이트 수다. 포인터 큐에 구조체를 넣으면 앞 8 B 만 간다 | 포인터 큐: `xQueueCreate(n, sizeof(blk_t *))` + `xQueueSend(q, &b, 0)` |
| 포인터 큐에 `xQueueSend(q, b, 0)` (`&` 빠짐) | 블록의 첫 8 B 를 "포인터" 로 복사한다 → 소비자가 쓰레기 주소를 받는다 | `xQueueSend(q, &b, 0)` — 넣을 값(포인터)의 **주소** 를 넘긴다 |
| 지역 배열의 포인터를 큐로 보냄 | 함수가 돌아오면 스택 프레임이 사라진다 → 소비자가 dangling 포인터 | 정적 풀 블록, 또는 작으면 복사 큐 |
| send 실패 경로에서 블록을 안 돌려줌 | 소유권이 안 넘어갔는데 아무도 free 안 함 → 풀이 하나씩 마른다 | `if (xQueueSend(...) != pdPASS) pool_free(b);` |
| send 성공 뒤 블록에 씀 | 이미 소비자 것. 소비자 우선순위에 따라 깨짐이 보였다 안 보였다 (corrupt 6 vs 0) | 보내기 전에 다 쓴다. 디버그 빌드는 주인 태그 확인 (연습 4) |
| ISR 에서 `xQueueSend(q, &x, portMAX_DELAY)` | ISR 은 막힐 수 없다. 태스크 API 는 ISR 문맥에서 금지 | `xQueueSendFromISR(q, &x, &woken); portYIELD_FROM_ISR(woken);` + 실패 카운터 |
| `xQueueOverwrite` 를 길이 > 1 큐에 | `configASSERT` 로 멈춘다 (`queue.c:962`) | 길이 1 우편함 전용. 깊이가 필요하면 drop-oldest (연습 1) |
| 로그 큐에 `portMAX_DELAY` + 센서 태스크도 `portMAX_DELAY` | 느린 로거가 역압으로 센서 주기를 흔든다 (3 틱 지연) | 위에서 아래로: 센서는 timeout 0 + 카운터, 로거 쪽 깊이를 키운다 |
| send 반환값 무시 | 유실이 조용히 일어난다 | `if (xQueueSend(...) != pdPASS) g_drops++;` 를 텔레메트리로 |
| 큐 깊이를 "적당히 10" | stall·버스트가 깊이를 넘으면 잃는다 (깊이 1 → 5개 유실) | 최악 stall × 도착률로 계산 + `uxQueueSpacesAvailable` 최솟값 실측 |

## 8. 면접 Q&A

<details>
<summary>Q1. 태스크 간에 데이터를 넘길 때 전역 버퍼 + 세마포어 대신 큐를 쓰는 이유는?</summary>

큐는 **데이터와 동기화를 한 번에** 준다: 항목이 있으면 가져가고, 없으면 그 자리에서 막혀 기다린다. 전역 버퍼 + 세마포어는 "버퍼를 누가 언제 쓰나" 를 따로 지켜야 하고(쓰는 중에 읽는 경쟁, 덮어쓰기), 신호가 합쳐지는 문제(RTOS-B08)도 있다. 그리고 FreeRTOS 큐는 **복사** 라서 보낸 쪽과 받은 쪽이 같은 메모리를 공유하지 않는다 — 보낸 직후 원본을 재사용해도 안전하다 (Part A 테스트: 보낸 뒤 원본을 바꿔도 받은 값은 보낸 시점 값). 비용은 복사 시간과 RAM(`len × itemSize`). 단순 "깨워" 신호면 태스크 알림(RTOS-M01)이 더 가볍고, 바이트 스트림이면 스트림 버퍼가 맞다.

</details>

<details>
<summary>Q2. 큐가 꽉 찼을 때 어떤 정책을 쓰나요? 기준은?</summary>

"무엇을 잃으면 안 되나" 와 "누가 기다려도 되나" 로 고른다. ① **막기**(portMAX_DELAY): 데이터를 잃으면 안 되고 생산자가 늦어져도 될 때 — 대신 역압이 위로 전파된다 (실측: 센서 3 틱 지연). ② **최신 버리기**(timeout 0): 생산자 주기가 신성할 때(센서·ISR) — 오래된 것이 남는다 (`0 1 2 3 9 10 11`). ③ **타임아웃**: 생산자 지연에 상한을 두고 그 안에서 최대한 살린다. ④ **덮어쓰기**(길이 1 `xQueueOverwrite`): 최신 상태만 의미 있을 때 (`0 1 2 8 …`). ISR 생산자는 ②나 ④뿐이다. 어떤 경우든 실패 수를 센다. 꼬리: "drop-oldest 는?" → FreeRTOS 에 없다. receive(0) 로 하나 버리고 send — 생산자가 하나일 때만 항상 성공 (연습 1).

</details>

<details>
<summary>Q3. 1 KB 프레임을 큐로 넘기면 안 되나요? 포인터로 넘길 때 주의점은?</summary>

넘길 수는 있다. 다만 send/receive 마다 1 KB 를 두 번 복사하고(호스트 실측 8 B 대비 약 7~8 배), FreeRTOS 는 그 복사를 **임계 구역 안에서** 하므로(`queue.c:2402`) 그동안 RTOS 관리 인터럽트가 막힌다 — 인터럽트 지연이 늘어난다. RAM 도 `깊이 × 1 KB`. 그래서 큰 데이터는 정적 풀의 블록 포인터(8 B)만 보낸다. 규칙: ① 포인터와 함께 **소유권** 이 넘어간다 — 보낸 뒤 만지지 않는다. ② send 가 실패하면 소유권은 그대로 — 보낸 쪽이 반납한다. ③ 다 쓴 쪽이 반납한다. ④ 스택 버퍼의 포인터는 절대 보내지 않는다. ⑤ 풀은 고정 크기 블록(단편화 없음, O(1)) — 일반 malloc 은 RTOS-M10. 꼬리: "풀 크기는?" → 동시에 살아 있을 수 있는 최대 수 = 큐 깊이 + 소비자가 쥔 수 + 생산자가 채우는 중인 수. 풀이 그보다 작으면 큐가 아니라 풀이 병목이다 (Part C: 풀 4 = 큐 3 + 소비자 1 → alloc 실패).

</details>

<details>
<summary>Q4. 큐 깊이는 어떻게 정하나요?</summary>

평균이 아니라 **최악 구간** 으로 정한다. 평균 처리율이 도착률보다 낮으면 어떤 깊이도 소용없다(언젠가 넘친다) — 먼저 ρ = 도착률 × 처리시간 < 1 을 확인. 그 다음 일시적 초과분: 소비자가 S 동안 멈추면 그 동안의 도착 수만큼(Part B: stall 동안 6개 → 깊이 6 에서 유실 0, 깊이 d 면 6 − d 유실). 1 틱 간격 버스트 B 개를 항목당 C 틱 소비자가 받으면 최소 깊이 = `B − ⌈(B−1)/C⌉` (연습 2 에서 B ≤ 20, C ≤ 6 전수 검증). 평균 대기열 길이는 Little 의 법칙 L = λW 로 감을 잡는다. 마지막으로 실측: 런타임에 `uxQueueSpacesAvailable()` 의 최솟값(최고 수위)을 기록해 여유를 확인한다.

</details>

<details>
<summary>Q5. 같은 큐에서 태스크 셋이 받으려고 기다리면 항목이 오면 누가 받나요?</summary>

가장 높은 우선순위의 대기자. FreeRTOS 는 막히는 태스크를 이벤트 리스트에 우선순위 순서로 넣고(`tasks.c:5413-5423`, `vListInsert`), send 는 그 리스트의 첫 태스크를 깨운다. 같은 우선순위끼리는 먼저 기다린 쪽이 앞이다 — 같은 값이면 뒤에 넣는다(`list.c:155-157`). 꼬리: "깨어난 태스크가 꼭 받나?" → 깨운 것과 가져가는 것은 별개다. 깨어난 태스크가 실제로 돌기 전에 더 높은 태스크가 먼저 receive 하면 비어 있을 수 있고, 그래서 FreeRTOS 의 send/receive 는 `for( ; ; )` 루프로 다시 확인한다. 이 모델은 방향별 대기자 1칸이라 이 경쟁이 없다 — 대기 리스트 구현은 RTOS-M06.

</details>

<details>
<summary>Q6. ISR 에서 큐에 넣을 때 무엇이 다른가요?</summary>

`xQueueSendFromISR(q, &x, &woken)` 만 쓴다. ① 절대 막히지 않는다 — 블록 경로가 없고 꽉 차면 즉시 `errQUEUE_FULL` (`queue.c:1327`). 그래서 ISR 생산자의 정책은 버리기(+카운터) 또는 `xQueueOverwriteFromISR` 뿐. ② 높은 태스크를 깨웠는지는 `woken` 으로만 알리고, 호출자가 `pdFALSE` 로 초기화한 뒤 ISR 끝에서 `portYIELD_FROM_ISR(woken)` 한 번 (실측: woken 은 rx 가 막혀 있던 첫 ISR 에서만 1). ③ Cortex-M 에서는 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 보다 급한 ISR 에서 부르면 안 된다(`portASSERT_IF_INTERRUPT_PRIORITY_INVALID`, `queue.c:1195`). ④ 항목은 작게 — 복사가 ISR 안·임계 구역 안에서 일어난다. UART 바이트 하나씩 큐에 넣는 것은 바이트당 커널 호출이라 고속이면 비싸다 → DMA + 스트림 버퍼나 링 + 알림 (RTOS-B08, DS-A01).

</details>

<details>
<summary>Q7. 보낸 뒤 같은 구조체 변수를 바로 다음 샘플로 덮어써도 되나요? 포인터 큐라면?</summary>

복사 큐면 된다 — `xQueueSend` 가 돌아온 시점에 사본이 이미 큐 저장소에 있다 (Part B 의 센서는 보낸 직후 자기 버퍼에 0xEE 를 낙서하는데도 `data_ok`). 포인터 큐면 안 된다 — 큐에는 주소만 있고 데이터는 원래 자리에 있다. 이게 "use-after-send": 소비자가 낮으면 깨진 데이터가 보이고(6/6), 높으면 안 보인다(0/6) — 테스트에서 안 보였다고 버그가 없는 게 아니다. 이중 버퍼(ping-pong) 나 풀로 블록을 매번 새로 받는다.

</details>

## 9. 추가 연습

### 연습 1 — drop-oldest 를 만들어라

FreeRTOS 에는 "꽉 차면 가장 오래된 것을 버리고 넣기" 가 없다. `kq_send_drop_oldest(q, item, scratch, &dropped, self)` 를 만들고, 깊이 3 에 1..6 을 보냈을 때 drop-newest(timeout 0)와 남는 값을 비교하라. 깊이 1 이면?

<details>
<summary>답 보기</summary>

꽉 차 있으면 `kq_receive(q, scratch, self, 0)` 로 맨 앞을 버리고(`dropped++`) `kq_send(…, 0)`. drop-newest 는 `1 2 3` 이 남고(실패 3), drop-oldest 는 `4 5 6` 이 남는다(버림 3). 깊이 1 이면 `xQueueOverwrite` 와 결과가 같다 (둘 다 5, 버림/덮음 4). FreeRTOS 에서 receive(0) + send(0) 는 **두 번의 임계 구역** 이라, 다른 생산자가 사이에 끼면 send 가 실패할 수 있다 — 생산자가 하나일 때만 "항상 성공". ISR 과 태스크가 같이 넣는 큐라면 사이를 `taskENTER_CRITICAL` 로 묶거나 설계를 바꾼다.

</details>

### 연습 2 — 버스트 흡수 최소 깊이 공식

0..B−1 틱에 1개씩 도착, 소비자는 한가하면 그 틱에 꺼내고 C 틱 바쁘다. 유실 0 인 최소 깊이를 공식으로 쓰고 시뮬레이션과 비교하라 (B = 1..20, C = 1..6 전부).

<details>
<summary>답 보기</summary>

틱 t 의 send 직후 개수 = 지금까지 도착 (t+1) − 그 전에 꺼낸 수. 꺼내는 틱은 0, C, 2C, … 이므로 t 전에 꺼낸 수 = ⌈t/C⌉. f(t) = t + 1 − ⌈t/C⌉ 은 t 가 1 늘 때 0 또는 1 늘어나므로(⌈⌉ 가 많아야 1 증가) 최대는 t = B−1: **d_min = B − ⌈(B−1)/C⌉**. C = 1 이면 1 (들어오는 족족 나감), C ≥ B−1 이면 B − 1. 예: B=10, C=3 → 7 (깊이 6 이면 1개 유실). Part A 의 ISR 실험(B=12, C=3)은 d_min = 8 인데 깊이 4 → 정확히 4개 유실. practice 는 120 조합 전부 공식 = 시뮬레이션을 확인한다.

</details>

### 연습 3 — 최신 값 우편함 + peek

자세(attitude)를 길이 1 큐에 `xQueueOverwrite` 로 넣고, BLE 태스크와 디스플레이 태스크가 **둘 다** 읽게 하라. `kq_peek` 를 만들어라.

<details>
<summary>답 보기</summary>

`kq_peek` = 비었으면 `Q_EMPTY`, 아니면 `rd` 칸을 복사만 하고 `count` 는 그대로. 읽는 쪽은 receive 가 아니라 peek 을 쓴다 → 둘 다 같은 최신 값(5), 덮어쓰면 둘 다 6. receive 를 쓰면 먼저 읽은 쪽이 지워서 다른 쪽은 빈 큐를 본다. FreeRTOS `xQueuePeek` 도 막힐 수 있다(timeout) — 새 값이 오기를 기다리는 용도. 이 패턴은 "공유 전역 변수 + 뮤텍스" 를 대체하는 FreeRTOS 의 우편함(mailbox) 관용구다.

</details>

### 연습 4 — use-after-send 를 항상 잡기

Part C 생산자가 send 성공 뒤 `data[0]` 을 바꾸는 버그를 넣고, 소비자 우선순위를 낮게/높게 바꿔 깨진 데이터 수를 세라. 그 다음 블록에 `owner` 필드를 달아 위반을 세라.

<details>
<summary>답 보기</summary>

소비자 낮음: corrupt 6/6, 소비자 높음: corrupt 0/6 (FreeRTOS 실측과 같음). 모델의 한 걸음은 쪼개지지 않으므로 버그 줄을 send 다음 걸음으로 미뤄 "send 안에서 즉시 선점" 을 흉내 냈다. 주인 태그: alloc 때 `owner = me`, send 직전 `owner = consumer`, 블록을 만질 때마다 `owner == me` 확인(`blk_touch`). 위반은 두 경우 모두 **6번** — 타이밍과 무관하게 잡힌다. 실제 펌웨어에서는 디버그 빌드의 `configASSERT(b->owner == xTaskGetCurrentTaskHandle())` 같은 형태로 두고, 릴리스에서는 빼거나 MPU 영역으로 막는다.

</details>

### 연습 5 — 풀을 큐로

빈 블록 포인터를 담은 큐(`g_freeq`, 길이 4, 항목 8 B)로 풀을 만들어라. alloc = receive(0), free = send(0). 연결 리스트 풀과 결과가 같은지 보라.

<details>
<summary>답 보기</summary>

같다: 깊이 4 → sent 7 / nomem 3, 깊이 1 → dropfull 5, 끝에 4개 복귀. 장점: alloc/free 가 이미 ISR-safe(`xQueueReceiveFromISR` / `xQueueSendFromISR`)이고, `xQueueReceive(free_q, &b, timeout)` 로 **블록이 생길 때까지 기다리는** alloc 도 공짜로 생긴다 (풀 고갈 = 역압). 단점: alloc/free 마다 커널 호출 + 임계 구역 (연결 리스트는 짧은 임계 구역 하나면 된다). `demo_freertos_pool.c` 가 이 방식이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed
```

## 10. 핵심 정리

- FreeRTOS 큐 = **복사 큐**: send 는 `uxItemSize` 바이트를 큐 저장소로 `memcpy`, receive 는 밖으로 `memcpy` (`queue.c:2425`, `2494`). 보낸 직후 원본 재사용 OK.
- 꽉 참/빔 + timeout 0 → 즉시 `errQUEUE_FULL`/`errQUEUE_EMPTY`(둘 다 0), timeout > 0 → 막혔다가 상대편이 깨우거나 타임아웃. 대기자는 우선순위 순.
- 정책: BLOCK(안 잃음, 역압 → 센서 3 틱 지연) / DROP(새 것 버림) / TIMEOUT(지연 상한) / OVERWRITE(길이 1, 옛 것 버림 = 최신 상태). ISR 생산자는 버리기·덮어쓰기뿐.
- 깊이 = 최악 stall·버스트 동안의 도착 수. 버스트 공식 `B − ⌈(B−1)/C⌉`. 평균 처리율이 모자라면 깊이로 못 고친다. 실패 수와 최고 수위는 반드시 기록.
- 큰 버퍼는 포인터 + 고정 블록 풀: 큐는 8 B 만 복사. **포인터와 함께 소유권이 넘어가고, send 실패면 보낸 쪽이 반납**. 풀 ≤ 큐 깊이 + 1 이면 풀이 병목.
- use-after-send 는 우선순위에 따라 보였다 안 보였다 한다 → 주인 태그로 잡는다.
- 복사는 FreeRTOS 임계 구역 안에서 일어난다 → 큰 항목은 인터럽트 지연을 늘린다. 작은 건 복사, 큰 건 포인터.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (`~/.esn_scratch/FreeRTOS-Kernel`, <https://github.com/FreeRTOS/FreeRTOS-Kernel>): `queue.c` (`xQueueGenericCreate` 502-529, `xQueueGenericSend` 949-1164 — 962, 977, 1043-1055, 1085-1096, 1126, 1154-1161; `xQueueGenericSendFromISR` 1167-1335 — 1195, 1207, 1327; `xQueueReceive` 1509-1656 — 1541-1584, 1616; `prvCopyDataToQueue` 2395-2475 — 2402, 2425, 2451-2466; `prvCopyDataFromQueue` 2478-2496), `include/queue.h` (107, 513-514, 597-598, 984-986, 1286-1287), `tasks.c` (`vTaskPlaceOnEventList` 5402-5428), `list.c` 155-157, `include/projdefs.h` 61-62, POSIX 포트 `port.c` 531-541 + `utils/wait_for_event.c` 126-133 (vTaskDelete 교착)
- FreeRTOS 문서 — Queues / `xQueueSend` / `xQueueOverwrite` / `xQueueSendFromISR` — <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Queue Management 장 (복사 vs 참조, 큰 데이터는 포인터) — <https://www.freertos.org/Documentation/RTOS_book.html>
- Zephyr — Message Queues (`k_msgq_put` 반환값) — <https://docs.zephyrproject.org/latest/kernel/services/data_passing/message_queues.html>, Memory Slabs — <https://docs.zephyrproject.org/latest/kernel/memory_management/slabs.html>
- CMSIS-RTOS2 — Message Queue / Memory Pool — <https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS__Message.html>
- ISO/IEC 9899:2011 (C11) 6.5.8p5 — 관계 연산자는 같은 배열(객체) 안의 포인터끼리만 정의
- 관련 레슨: RTOS-B08 (ISR → 태스크 신호, woken), RTOS-B09 (카운팅 세마포어 풀), RTOS-B12 (타이머 명령 큐), RTOS-M01 (태스크 알림), RTOS-M02 (FromISR·yield), RTOS-M06 (대기 리스트가 있는 블로킹 큐 구현), RTOS-M10 (힙 전략), RTOS-A07 (Zephyr `k_msgq`), DS-A01 (SPSC 링), ARCH-M12 (DMA 버퍼와 캐시)

---
id: RTOS-B09
time: 65분
updated: 2026-11-08
---
# RTOS-B09 카운팅 세마포어: 리소스 풀과 이벤트 카운트

## 학습 목표

- 카운팅 세마포어(counting semaphore)를 **count + 대기 리스트(wait list)** 로 직접 구현하고, take/give/ISR 용 give 의 규칙(블록하는 쪽, 절대 블록하지 않는 쪽)을 말할 수 있다.
- 대기 리스트의 깨우는 순서를 **우선순위 → 같은 우선순위는 FIFO** 로 정하고, 그것을 결정적 테스트(깨어난 순서 문자열 + 독립 오라클)로 증명한다.
- 같은 세마포어가 두 가지 일을 한다는 것을 안다: ① **자원 풀** — DMA 채널 3개를 태스크 5개가 나눠 쓸 때 동시 보유 ≤ 3, ② **이벤트 카운트** — ISR 이 take 전에 N번 give 하면 take N번이 블록 없이 성공한다. 그리고 max 에 닿으면 give 가 **버려진다**(포화).
- give 의 두 가지 설계 — **직접 넘겨주기(handoff)** vs **값 올리고 깨우기(barging, FreeRTOS 방식)** — 의 공정성/처리량 차이를 설명한다.
- "세마포어는 *몇 개* 남았는지만 안다, *어느* 채널인지는 모른다" → DMA 드라이버에서 비트맵 + 임계 구역이 따로 필요한 이유를 안다.

## 1. 문제

"카운팅 세마포어는 언제 쓰나요? 바이너리 세마포어와 뭐가 다르죠? 기다리는 태스크가 여럿이면 누가 먼저 깨어나죠?" — RTOS 면접의 단골이다. 펌웨어에서는 매일 쓴다: STM32 의 DMA 스트림처럼 **개수가 정해진 하드웨어 자원**(DMA 채널, 버퍼 풀, I2C 트랜잭션 슬롯)을 여러 태스크가 나눠 쓸 때, 그리고 **UART RX ISR 이 프레임 끝마다 신호를 주고 파서 태스크가 그만큼 처리**해야 할 때다. 바이너리 세마포어는 신호가 합쳐져 사라진다(RTOS-B08). 카운팅 세마포어는 **개수를 기억한다**.

카운팅 세마포어 = 정수 하나(`count`) + 기다리는 태스크 목록. 연산은 둘뿐이다.

- **take** (P, wait, `xSemaphoreTake`): `count > 0` 이면 하나 가져가고 진행. 0 이면 **블록** — 대기 리스트에 들어가 잠든다. 그래서 ISR 에서는 블록하는 take 를 부를 수 없다.
- **give** (V, signal, `xSemaphoreGive`): 기다리는 태스크가 있으면 하나 깨우고, 없으면 `count++`. `count == max` 이면 give 는 실패(포화). give 는 **절대 블록하지 않는다** → ISR 에서도 부를 수 있다 (`...FromISR`).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">카운팅 세마포어 = count + 대기 리스트 (이 레슨의 설계)</text>
  <rect x="20" y="45" width="150" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="34" y="72" fill="currentColor" font-family="monospace">count = 0</text>
  <text x="34" y="96" fill="currentColor" font-family="monospace">max   = 3</text>
  <text x="200" y="62" fill="currentColor">wait_head →</text>
  <rect x="290" y="45" width="80" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="300" y="67" fill="currentColor" font-family="monospace">W3 p3</text>
  <rect x="390" y="45" width="80" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="400" y="67" fill="currentColor" font-family="monospace">W2 p2</text>
  <rect x="490" y="45" width="80" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="500" y="67" fill="currentColor" font-family="monospace">W5 p2</text>
  <rect x="590" y="45" width="80" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="600" y="67" fill="currentColor" font-family="monospace">W1 p1</text>
  <path d="M370 62 L388 62 M470 62 L488 62 M570 62 L588 62" stroke="currentColor" stroke-width="1.5"/>
  <text x="290" y="100" fill="currentColor">우선순위 높은 순 · 같으면 먼저 온 순 (W2 → W5)</text>
  <rect x="20" y="140" width="220" height="130" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="162" fill="currentColor" font-weight="bold">take(s)</text>
  <text x="32" y="186" fill="currentColor" font-family="monospace">count &gt; 0 → count--</text>
  <text x="32" y="208" fill="currentColor" font-family="monospace">count == 0 → 리스트에</text>
  <text x="32" y="228" fill="currentColor" font-family="monospace">  넣고 블록 (B)</text>
  <text x="32" y="254" fill="currentColor">ISR 에서는 금지</text>
  <rect x="260" y="140" width="460" height="130" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="272" y="162" fill="currentColor" font-weight="bold">give(s) — 절대 블록하지 않는다</text>
  <text x="272" y="186" fill="currentColor" font-family="monospace">대기자 있음 → 맨 앞(W3)에게 직접 넘김, count 그대로</text>
  <text x="272" y="208" fill="currentColor" font-family="monospace">대기자 없음 → count++</text>
  <text x="272" y="230" fill="currentColor" font-family="monospace">count == max → SEM_FULL (이벤트 버려짐)</text>
  <text x="272" y="254" fill="currentColor">ISR 용: 깨운 태스크가 더 높으면 woken = true</text>
  <text x="10" y="292" fill="currentColor">불변식: count &gt; 0 이면 대기자는 0명 — 토큰이 남는데 누가 기다리는 상태는 없다.</text>
</svg>
```

**미니 커널 (하네스 제공).** RTOS 트랙의 앞 레슨(B01–B07, 커널 만들기)은 아직 없으므로 이 레슨은 아주 작은 커널을 자기 안에 갖고 있다. 실제 스레드는 쓰지 않는다 — OS 스레드 1개가 tick 을 하나씩 돌리는 **시뮬레이터** 라서 모든 실행이 매번 똑같다(결정적).

| 규칙 | 이 모델 | 실제 RTOS 와 다른 점 |
|---|---|---|
| tick 하나 | ① ISR 대용 함수(tick hook) → ② release / sleep 끝난 태스크 READY → ③ READY 중 최고 우선순위 태스크의 `step()` 한 번 | 실제는 give 안에서 즉시 선점이 일어난다. 여기선 다음 tick 의 ③ 에서 |
| 태스크 | 상태 기계 함수 `step(t)` + `pc`. 한 step = CPU 1 tick | 실제는 C 함수가 계속 돌다가 선점됨 |
| 같은 우선순위 | 먼저 READY 된 순 (FIFO), time slicing 없음 | FreeRTOS 기본은 time slicing 있음 (`configUSE_TIME_SLICING`) |
| 블록 | `k_block()`: 상태 BLOCKED, 대기 순번 기록 | 대기 리스트 조작은 **세마포어 코드(학생) 몫** |
| ISR | tick 시작에 하네스가 부른다. `g_cur` = 끼어든 태스크 (NULL = idle, 우선순위 0) | 실제 ISR 은 아무 명령 사이에나 |
| 타임라인 | tick 끝 상태: `R` 실행 · `r` READY(CPU 대기) · `B` 세마포어 대기 · `s` sleep · `.` 미출발/끝 | (RTOS-M03/M04/M05 와 같은 형식, `s` 만 추가) |

### Part A — 카운팅 세마포어 (대기 리스트 포함)

`ksem_t { count, max, wait_head, nwait, full_drops }` 에 다섯 함수를 채운다.

- `ksem_init(s, initial, max)` — `max ≥ 1`, `0 ≤ initial ≤ max` 아니면 false.
- `wait_insert / wait_pop` — 단일 연결 리스트. **우선순위 높은 순, 같으면 먼저 온 순**.
- `ksem_take(s, self)` — 있으면 `count--` 후 true. 없으면 `wait_insert` + `k_block`, false. ISR 안이면 거부(false, `g_isr_take_err++`).
- `ksem_give(s)` — 대기자가 있으면 **맨 앞 태스크에게 직접 넘긴다** (`token = 1`, `k_unblock`, count 그대로). 없으면 `count++`, max 면 `SEM_FULL` + `full_drops++`.
- `ksem_give_from_isr(s, &woken)` — give 와 같고, 깨운 태스크 우선순위 > 끼어든 태스크(`g_cur`, idle=0) 이면 `woken = true` (false 로 덮어쓰지 않는다).

### Part B — 자원 풀 (DMA 채널 3 × 태스크 5) 과 이벤트 카운트

1. **B1 풀**: 채널 토큰 3개, 태스크 5개(우선순위 3,2,2,1,1)가 "take → hold tick 동안 사용 → give → think" 를 3번씩. 매 tick 하네스가 검사: 보유자 ≤ 3, **보유자 + count == 3** (토큰 보존), count > 0 인데 대기자 있음 = 0, 모두 3번 서비스.
2. **B2 깨우는 순서**: 채널 3개를 H1–H3 이 오래 쥔 동안 W1(p1) W2(p2) W3(p3) W4(p1) W5(p2) 순서로 블록 → 깨어나는 순서는 `W3 W2 W5 W1 W4` 여야 한다.
3. **B3 무작위 300개**: xorshift32 고정 seed 로 우선순위·출발·hold·think 를 뽑아 300 시나리오, 모든 불변식 + 오라클.
4. **B4 이벤트 카운트**: UART 프레임 ISR 이 tick 1..5 에 give, 파서 태스크는 더 높은 MOTOR 태스크 때문에 tick 7 부터 돈다 → take 5번은 블록 없이, 6번째만 블록.
5. **B5 포화**: max 4 인데 give 6번 → 4번 OK, 2번 `SEM_FULL`. max 1(바이너리)이면 5번 버려진다.

### Part C — DMA 채널 드라이버 (세마포어 + 비트맵 + 완료 ISR)

세마포어는 "채널이 **몇 개** 남았나" 만 안다. 어느 채널인지는 비트맵 `used` 가 안다. 태스크는 bit 을 set, **DMA 완료 ISR 이 clear** 하므로 공유 RMW 다 → 태스크 쪽은 임계 구역 안에서 (C-M14).

- `dma_claim(d)` — (세마포어 토큰을 가진 상태에서) 가장 낮은 번호의 빈 채널을 `crit_enter()` … `dma_mark_used()` … `crit_exit()` 로 잡는다.
- `dma_complete_isr(d, ch, &woken)` — 비트를 지우고 `ksem_give_from_isr`.
- 시나리오: ADC(p3), I2S 오디오(p3), SPI FLASH(p2), UART TX(p2), LOG(p1) 가 4번씩 전송. 하네스의 "DMA 하드웨어 모델" 이 전송 길이만큼 tick 을 센 뒤 완료 ISR 을 부른다. 검사: 20번 모두 완료, 채널 중복 0, 임계 구역 밖 비트 조작 0, `woken` 이 오라클 값과 매번 같음.

## 2. Problem (English)

> **Counting semaphore with a priority-ordered wait list, in a tiny tick-driven kernel.**
>
> ```c
> bool ksem_init(ksem_t *s, int initial, int max);      // false unless 1 <= max, 0 <= initial <= max
> bool ksem_take(ksem_t *s, task_t *self);              // true = got it now; false = blocked (token handed over later)
> int  ksem_give(ksem_t *s);                            // SEM_OK or SEM_FULL (count already == max)
> int  ksem_give_from_isr(ksem_t *s, bool *higher_woken); // never blocks; sets *higher_woken, never clears it
> ```
>
> Waiters are woken highest-priority first, FIFO among equal priority. A give with waiters hands the token directly to the head waiter (count unchanged). Calling `ksem_take` from ISR context must be rejected.
>
> 1. Three DMA channels shared by five tasks: show that at most 3 tasks ever hold a channel, every task is eventually served, and wake order is priority-then-FIFO.
> 2. An ISR gives N times before the handler task first runs: the next N takes must succeed without blocking. Show saturation at `max`.
> 3. Write a DMA channel allocator: the semaphore counts free channels; a bitmap (set by tasks inside a critical section, cleared by the DMA-complete ISR) says which one.
>
> Follow-ups: binary vs counting semaphore? Why can't an ISR take? What does `xHigherPriorityTaskWoken` do? Handoff vs "increment and wake" — which does FreeRTOS do and what can go wrong? Can a low-priority waiter starve? How is this different from a mutex?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.1 초에 끝남 — 멈추지 않는다):

```text
FAIL starter.c:371  !ksem_init(&s, 4, 3) && !ksem_init(&s, 0, 0) && !ksem_init(&s, -1, 3)
FAIL starter.c:380  strcmp(order, "bdcae") == 0
...
Part B1 pool: 3 channels, 5 tasks, 3 rounds each -> all done at tick 35
hold : 1 2 3 4 5 5 4 5 4 3 4 5 4 3 4 3 2 1 2 3 2 3 4 4 3 2 1 0 1 2 2 2 2 1 0
FAIL starter.c:420  g_max_hold == 3
...
Part B4 event count (max 8): fast takes=6, blocked takes=0, processed=6, done tick 19
Part B5 saturation (max 4, 6 gives): ok=6 full=0 drops=0 fast=5 processed=5
...
Part C DMA: transfers=0 (ch0=0 ch1=0 ch2=0), woken=true 0 times, tasks done at tick 20
7/33 passed
```

스텁 `ksem_take` 는 무조건 true 라서 `hold` 줄이 **5** 까지 올라간다 — 채널 3개를 5명이 동시에 쓴다는 뜻이다. 하네스 설계:

- **무한 루프가 없다.** `k_run(limit)` 은 tick 상한에서 멈춘다. 틀린 세마포어로 모두 블록돼도 FAIL 로 끝난다.
- **오라클은 학생 코드와 독립.** `k_unblock()` 은 깨우려는 태스크가 "같은 세마포어에서 블록된 태스크 중 우선순위 최고, 그중 가장 먼저 블록"인지 **태스크 표 전체를 훑어** 확인한다(`g_order_err`). 대기 리스트를 어떻게 짰든 상관없이 결과만 본다.
- **체크는 상태만.** 보유자 수, 토큰 보존, 깨어난 순서 문자열, 반환값 카운트. 시간 측정은 없다.
- `(void)k_block; ...` 줄은 starter 에서 아직 안 부르는 커널 서비스의 unused 경고를 막는다 (clang 은 `static inline` 도 경고한다 — 확인함).

## 4. 힌트

<details>
<summary>힌트 1 — 정렬 삽입: <code>&gt;=</code> 하나로 FIFO 가 된다</summary>

포인터의 포인터 `task_t **pp = &s->wait_head` 로 걸어가며 "앞 원소의 우선순위가 나보다 **크거나 같으면** 지나간다". 같은 우선순위를 지나가야 내가 그 뒤(=나중에 온 순)에 붙는다. `>` 로 쓰면 같은 우선순위 중 맨 앞에 끼어들어 LIFO 가 된다. 머리 삽입 특별 처리가 필요 없다.

</details>

<details>
<summary>힌트 2 — 블록된 태스크는 깨어날 때 이미 토큰을 가진다</summary>

give 에서 대기자가 있으면 `count++` 하지 말고 그 태스크에게 바로 넘긴다: `w->token = 1; k_unblock(w);`. 태스크 프로그램은 `pc == 1`(깨어남) 에서 `token` 을 확인하고 바로 자원을 쓴다. 그래서 불변식 "count > 0 이면 대기자 없음" 이 항상 성립한다.

</details>

<details>
<summary>힌트 3 — 포화: give 는 실패를 돌려주지 블록하지 않는다</summary>

대기자도 없고 `count == max` 이면 `full_drops++` 후 `SEM_FULL`. "give 가 자리가 날 때까지 기다린다" 는 선택지는 없다 — ISR 에서 불리기 때문이다. 호출자가 반환값으로 오버런을 센다.

</details>

<details>
<summary>힌트 4 — woken 은 "누구를 깨웠나" 를 give 전에 본다</summary>

give 가 `wait_pop` 하면 맨 앞이 사라지므로, 먼저 `s->wait_head` 를 기억해 둔다. 비교 대상은 `g_cur`(끼어든 태스크, NULL 이면 idle = 0). 같은 우선순위면 false — 선점할 이유가 없다 (FreeRTOS 도 `>`).

</details>

<details>
<summary>힌트 5 — DMA ISR: 비트를 먼저 지우고 give</summary>

토큰을 받은 태스크가 `dma_claim` 할 때 빈 비트가 **반드시** 있어야 한다. 순서를 거꾸로 하면 (task 문맥 give 처럼 즉시 선점되는 곳에서는) 깨어난 태스크가 빈 비트를 못 찾는다. 태스크 쪽 `dma_claim` 은 ISR 이 같은 `used` 를 RMW 하므로 `crit_enter/exit` 안에서.

</details>

## 5. 풀이

### Part A — 다섯 함수

```c
static void wait_insert(ksem_t *s, task_t *t) {
    task_t **pp = &s->wait_head;
    while (*pp && (*pp)->prio >= t->prio)   /* 같은 우선순위는 지나간다 → FIFO */
        pp = &(*pp)->wnext;
    t->wnext = *pp; *pp = t; s->nwait++;
}
static int ksem_give(ksem_t *s) {
    task_t *w = wait_pop(s);
    if (w) { w->token = 1; k_unblock(w); return SEM_OK; }  /* handoff: count 그대로 */
    if (s->count < s->max) { s->count++; return SEM_OK; }
    s->full_drops++; return SEM_FULL;                       /* 포화 */
}
```

Part A 단위 테스트가 확인하는 것 (전부 PASS):

| 테스트 | 기대 | 왜 |
|---|---|---|
| `ksem_init(4,3)`, `(0,0)`, `(-1,3)` | false | FreeRTOS `xQueueCreateCountingSemaphore` 도 `uxMaxCount != 0 && uxInitialCount <= uxMaxCount` 가 아니면 NULL (queue.c:919–920) |
| 우선순위 1,3,2,3,1 순서로 삽입 → pop | `bdcae` | 3,3 은 넣은 순(b, d), 그다음 2, 1,1 은 넣은 순(a, e) |
| count 0 에서 Y 가 take → give | Y 가 `token=1`, READY, **count 0 유지** | handoff |
| give 3번 → count 3, 한 번 더 | `SEM_FULL`, `full_drops=1` | 포화 |
| ISR give, 끼어든 X(p2), 대기 Y(p3) / Z(p1) | woken true / false / (이미 true 면 유지) | 비교는 `>` |
| ISR 안에서 take | false, `g_isr_take_err=1`, 블록 안 됨 | ISR 은 잠들 수 없다 |

**설계 선택 두 개 — 말로 설명할 수 있어야 한다.**

| 선택 | 이 레슨 | 다른 선택지 | 결과 |
|---|---|---|---|
| 깨우는 순서 | **우선순위 → FIFO** | 순수 FIFO (µC/OS 등 일부 옵션, 연습 2) | 우선순위 순서는 응답성이 좋지만 낮은 대기자가 굶을 수 있다. 연습 2: L 이 tick 36 vs FIFO tick 9 |
| give 와 대기자 | **handoff** (토큰을 직접 넘김) | **barging**: count++ 하고 깨우기만, 깨어난 태스크가 다시 take (FreeRTOS) | handoff 는 가로채기가 없어 공정하지만, 넘겨받은 태스크가 CPU 를 못 받는 동안 자원이 논다. barging 은 처리량이 좋지만 헛깨움(futile wakeup)이 생긴다 (연습 3: 4번) |

FreeRTOS V11.3.2 가 실제로 어떻게 하는지 (소스 직접 확인):

- **대기 리스트 정렬**: `vTaskPlaceOnEventList` 가 `xItemValue = configMAX_PRIORITIES - uxPriority` 로 오름차순 삽입한다 (tasks.c:5412–5423 주석, tasks.c:1929). `vListInsert` 는 `pxIterator->pxNext->xItemValue <= xValueOfInsertion` 동안 지나가므로 (list.c:192) 같은 우선순위는 뒤에 붙는다 = **FIFO**. 깨울 때는 맨 앞 (`listGET_OWNER_OF_HEAD_ENTRY`, tasks.c:5523). 내 `>=` 와 같은 생각이다.
- **give = barging**: 세마포어는 "길이 max, 항목 크기 0 인 큐" 다. give 는 `prvCopyDataToQueue` 에서 `uxMessagesWaiting + 1` (queue.c:2472) 한 뒤 `xTaskRemoveFromEventList` 로 수신 대기자를 깨운다 (queue.c:985–1014). 깨어난 태스크는 `xQueueSemaphoreTake` 의 `for( ; ; )` (queue.c:1686) 맨 위에서 `uxSemaphoreCount > 0` 를 **다시 확인** (queue.c:1696) — 그 사이 더 높은 태스크가 가져갔으면 남은 시간으로 다시 블록 (queue.c:1779 `xTaskCheckForTimeOut`, 1806).
- **포화**: `xSemaphoreGive` 는 블록 시간 0 (`semGIVE_BLOCK_TIME`, semphr.h:42, 460) 으로 send 하므로 꽉 차면 즉시 `errQUEUE_FULL` (= 0 = pdFAIL, projdefs.h:62; queue.c:1085–1096). ISR 판도 `uxMessagesWaiting < uxLength` 아니면 실패 (queue.c:1388, 1498).
- **woken**: `xTaskRemoveFromEventList` 는 `pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority` 일 때만 pdTRUE (tasks.c:5555) — 같은 우선순위면 false. 내 모델과 같다.

### Part B1 — 채널 3개 × 태스크 5개 (실제 출력)

```text
Part B1 pool: 3 channels, 5 tasks, 3 rounds each -> all done at tick 42
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
A    : R s s s s s R R r R s s s s s R R B r R s s s s s R . . . . . . . . . . . . . . . .
B    : r R s s s s s s r r R r R B B B B B B B r R s s s s s s R R B r R s s s s s s R . .
C    : . r R s s s s r R s r R B B B r r R s s s s R s R r R s s s s R . . . . . . . . . .
D    : . r r R B B r r r r r r r R s s s s s r R r r R s s s s s r r r r R R s s s s s R .
E    : . . r r R B B B B B r r r r R s s s R r r r r r r r r R r r R s s s r R R s s s r R
sem  : 2 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 1 0 1 0 0 0 1 2 3
hold : 1 2 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 2 3 3 3 3 3 3 3 3 3 3 2 3 2 3 3 3 2 1 0
wake order: D A E C A B C E B
blocked ticks: A=1 B=8 C=3 D=2 E=5
```

읽는 법:

- `hold` 는 tick 2 부터 거의 내내 3 이고 **한 번도 4 가 되지 않는다** (시작 코드에서는 5 까지 갔다). `sem + hold == 3` 이 모든 칸에서 성립한다 (토큰 보존 — 하네스가 매 tick 검사).
- tick 3: D 가 take → count 0 이라 블록 (tick 3 은 R, 4–5 는 B). tick 4: E 도 블록. 둘 다 우선순위 1 이니 **먼저 온 D** 가 tick 6 에 A 의 give 로 넘겨받는다 → wake order 첫 글자 `D`.
- tick 6–12: D 는 토큰을 쥔 채 `r` 이다. 더 높은 A·B·C 가 CPU 를 쓰기 때문이다. `hold` 에는 D 가 포함돼 있다 — **handoff 의 비용**: 채널이 예약된 채 7 tick 놀았다. barging 이었다면 그 사이 A 가 채널을 썼을 수도 있다(연습 3).
- B(p2) 는 tick 12 에 줄을 섰고 A(p3) 는 tick 16 에 섰는데, tick 18 의 토큰은 **나중에 온 A** 가 받는다 (A 는 tick 18 에 `r`, B 는 tick 20 까지 `B`) — 우선순위가 도착 순서를 이긴다. B 가 7 tick 이나 기다린 이유.

```svg
<svg viewBox="0 0 740 274" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part B1 실제 타임라인 — DMA 채널 3개 × 태스크 5개 (solution.c 출력 그대로)</text>
  <text x="71" y="38" fill="currentColor">0</text> <text x="146" y="38" fill="currentColor">5</text> <text x="221" y="38" fill="currentColor">10</text> <text x="296" y="38" fill="currentColor">15</text> <text x="371" y="38" fill="currentColor">20</text> <text x="446" y="38" fill="currentColor">25</text> <text x="521" y="38" fill="currentColor">30</text> <text x="596" y="38" fill="currentColor">35</text> <text x="671" y="38" fill="currentColor">40</text> <rect x="70.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="85.0" y="48" width="75.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="160.0" y="48" width="30.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="190.0" y="48" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="205.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="220.0" y="48" width="75.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="295.0" y="48" width="30.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="325.0" y="48" width="15.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="340.0" y="48" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="355.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="370.0" y="48" width="75.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="445.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="61" fill="currentColor" font-family="monospace">A</text> <text x="328.0" y="61" fill="currentColor">B</text> <rect x="70.0" y="72" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="85.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="100.0" y="72" width="90.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="190.0" y="72" width="30.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="220.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="235.0" y="72" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="250.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="265.0" y="72" width="105.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="370.0" y="72" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="385.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="400.0" y="72" width="90.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="490.0" y="72" width="30.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="520.0" y="72" width="15.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="535.0" y="72" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="550.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="565.0" y="72" width="90.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="655.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="85" fill="currentColor" font-family="monospace">B</text> <text x="268.0" y="85" fill="currentColor">B</text> <text x="523.0" y="85" fill="currentColor">B</text> <rect x="85.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="100.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="115.0" y="96" width="60.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="175.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="190.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="205.0" y="96" width="15.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="220.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="235.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="250.0" y="96" width="45.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="295.0" y="96" width="30.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="325.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="340.0" y="96" width="60.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="400.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="415.0" y="96" width="15.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="430.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="445.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="460.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="96" width="60.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="535.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="109" fill="currentColor" font-family="monospace">C</text> <text x="253.0" y="109" fill="currentColor">B</text> <rect x="85.0" y="120" width="30.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="115.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="130.0" y="120" width="30.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="160.0" y="120" width="105.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="265.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="280.0" y="120" width="75.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="355.0" y="120" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="370.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="385.0" y="120" width="30.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="415.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="430.0" y="120" width="75.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="505.0" y="120" width="60.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="565.0" y="120" width="30.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="595.0" y="120" width="75.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="670.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="133" fill="currentColor" font-family="monospace">D</text> <text x="133.0" y="133" fill="currentColor">B</text> <rect x="100.0" y="144" width="30.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="130.0" y="144" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="145.0" y="144" width="75.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="220.0" y="144" width="60.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="280.0" y="144" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="295.0" y="144" width="45.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="340.0" y="144" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="355.0" y="144" width="120.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="144" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="490.0" y="144" width="30.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="520.0" y="144" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="535.0" y="144" width="45.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="580.0" y="144" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="595.0" y="144" width="30.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="625.0" y="144" width="45.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="670.0" y="144" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="685.0" y="144" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="157" fill="currentColor" font-family="monospace">E</text> <text x="148.0" y="157" fill="currentColor">B</text>
  <text x="10" y="181" fill="currentColor" font-family="monospace">sem</text> <text x="74.5" y="181" fill="currentColor" font-family="monospace">2</text> <text x="89.5" y="181" fill="currentColor" font-family="monospace">1</text> <text x="104.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="119.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="134.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="149.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="164.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="179.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="194.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="209.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="224.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="239.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="254.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="269.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="284.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="299.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="314.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="329.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="344.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="359.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="374.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="389.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="404.5" y="181" fill="currentColor" font-family="monospace">1</text> <text x="419.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="434.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="449.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="464.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="479.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="494.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="509.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="524.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="539.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="554.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="569.5" y="181" fill="currentColor" font-family="monospace">1</text> <text x="584.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="599.5" y="181" fill="currentColor" font-family="monospace">1</text> <text x="614.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="629.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="644.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="659.5" y="181" fill="currentColor" font-family="monospace">1</text> <text x="674.5" y="181" fill="currentColor" font-family="monospace">2</text> <text x="689.5" y="181" fill="currentColor" font-family="monospace">3</text>
  <text x="10" y="205" fill="currentColor" font-family="monospace">hold</text> <text x="74.5" y="205" fill="currentColor" font-family="monospace">1</text> <text x="89.5" y="205" fill="currentColor" font-family="monospace">2</text> <text x="104.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="119.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="134.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="149.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="164.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="179.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="194.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="209.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="224.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="239.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="254.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="269.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="284.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="299.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="314.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="329.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="344.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="359.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="374.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="389.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="404.5" y="205" fill="currentColor" font-family="monospace">2</text> <text x="419.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="434.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="449.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="464.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="479.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="494.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="509.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="524.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="539.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="554.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="569.5" y="205" fill="currentColor" font-family="monospace">2</text> <text x="584.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="599.5" y="205" fill="currentColor" font-family="monospace">2</text> <text x="614.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="629.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="644.5" y="205" fill="currentColor" font-family="monospace">3</text> <text x="659.5" y="205" fill="currentColor" font-family="monospace">2</text> <text x="674.5" y="205" fill="currentColor" font-family="monospace">1</text> <text x="689.5" y="205" fill="currentColor" font-family="monospace">0</text>
  <rect x="10" y="228" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="239" fill="currentColor">R 실행</text> <rect x="86" y="228" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="106" y="239" fill="currentColor">r READY(CPU 대기)</text> <rect x="261" y="228" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="281" y="239" fill="currentColor">B 세마포어 대기</text> <rect x="382" y="228" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="402" y="239" fill="currentColor">s sleep (보유 중 또는 쉬는 중)</text>
  <text x="10" y="262" fill="currentColor">hold 줄은 3 을 넘지 않는다. sem 이 0 인 동안 새로 take 한 태스크는 B (대기 리스트)로 간다.</text>
</svg>
```

### Part B2 — 깨우는 순서 테스트

H1–H3 (p4) 이 tick 0–2 에 채널을 모두 잡고 각각 8·9·10 tick 쥔다. 그 사이 W 다섯이 한 tick 씩 차례로 블록한다.

| tick | 사건 | 대기 리스트 (앞 → 뒤) |
|---|---|---|
| 3 | W1(p1) 블록 | W1 |
| 4 | W2(p2) 블록 | W2 W1 |
| 5 | W3(p3) 블록 | W3 W2 W1 |
| 6 | W4(p1) 블록 | W3 W2 W1 W4 |
| 7 | W5(p2) 블록 | W3 W2 **W5** W1 W4 (W2 뒤, W1 앞) |
| 8 | H1 give → **W3** | W2 W5 W1 W4 |
| 10 | H2 give → **W2** | W5 W1 W4 |
| 11 | W3 give → **W5** | W1 W4 |
| 12 | H3 give → **W1** | W4 |
| 15 | W2 give → **W4** | — |

실제 출력: `Part B2 wake order: W3 W2 W5 W1 W4 (done at tick 21)`, `g_order_err == 0`. 같은 우선순위 쌍 (W2, W5) 과 (W1, W4) 가 둘 다 들어 있어 "우선순위" 와 "FIFO" 를 한 번에 검증한다. 타임라인에서 W1 은 tick 12 에 넘겨받고도 tick 17 에야 처음 돈다 (`r r r r r`) — 넘겨받음 ≠ 실행.

### Part B3 — 무작위 300 시나리오

`Part B3 random: 300 scenarios, fails=0, worst finish tick=78, handoffs=2366` — 우선순위 1–3, 출발 0–3, hold 1–8, think 0–3, 4라운드. 매 tick 불변식 + 깨울 때마다 오라클. 이 시나리오들에서 "모두 서비스됨" 이 성립하는 이유는 **라운드가 유한**해서다: 높은 태스크가 할 일을 마치면 낮은 태스크 차례가 온다. 높은 태스크가 끝없이 자원을 원하면 우선순위 순서에서는 낮은 대기자가 **굶는다** — 연습 2 에서 일부러 만든다. "모든 태스크가 결국 서비스된다" 는 우선순위 순서의 일반 성질이 아니라 **부하에 대한 조건** 이다.

### Part B4 — 이벤트 카운트: give 5번 → take 5번은 블록 없이

UART RX ISR 이 프레임 끝(예: `\n` 수신, IDLE line 인터럽트)마다 give, 파서 태스크는 take 한 번에 프레임 하나를 처리한다. MOTOR(p3) 가 tick 0–6 CPU 를 독점해서 파서(p2) 는 tick 7 에 처음 돈다.

```text
Part B4 event count (max 8): fast takes=5, blocked takes=1, processed=6, done tick 21
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0
MOTOR: R R R R R R R . . . . . . . . . . . . . .
PARSE: r r r r r r r R R R R R R R R R R R B B R
sem  : 0 1 2 3 4 5 5 4 4 3 3 2 2 1 1 0 0 0 0 0 0
```

```svg
<svg viewBox="0 0 740 178" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part B4 실제 타임라인 — ISR give ×5 (tick 1..5), 파서는 tick 7 부터</text>
  <text x="71" y="38" fill="currentColor">0</text> <text x="191" y="38" fill="currentColor">5</text> <text x="311" y="38" fill="currentColor">10</text> <text x="431" y="38" fill="currentColor">15</text> <text x="551" y="38" fill="currentColor">20</text> <rect x="70.0" y="48" width="168.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="61" fill="currentColor" font-family="monospace">MOTOR</text> <rect x="70.0" y="72" width="168.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="238.0" y="72" width="264.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="502.0" y="72" width="48.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="550.0" y="72" width="24.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="85" fill="currentColor" font-family="monospace">PARSE</text> <text x="505.0" y="85" fill="currentColor">B</text>
  <text x="10" y="109" fill="currentColor" font-family="monospace">sem</text> <text x="79.0" y="109" fill="currentColor" font-family="monospace">0</text> <text x="103.0" y="109" fill="currentColor" font-family="monospace">1</text> <text x="127.0" y="109" fill="currentColor" font-family="monospace">2</text> <text x="151.0" y="109" fill="currentColor" font-family="monospace">3</text> <text x="175.0" y="109" fill="currentColor" font-family="monospace">4</text> <text x="199.0" y="109" fill="currentColor" font-family="monospace">5</text> <text x="223.0" y="109" fill="currentColor" font-family="monospace">5</text> <text x="247.0" y="109" fill="currentColor" font-family="monospace">4</text> <text x="271.0" y="109" fill="currentColor" font-family="monospace">4</text> <text x="295.0" y="109" fill="currentColor" font-family="monospace">3</text> <text x="319.0" y="109" fill="currentColor" font-family="monospace">3</text> <text x="343.0" y="109" fill="currentColor" font-family="monospace">2</text> <text x="367.0" y="109" fill="currentColor" font-family="monospace">2</text> <text x="391.0" y="109" fill="currentColor" font-family="monospace">1</text> <text x="415.0" y="109" fill="currentColor" font-family="monospace">1</text> <text x="439.0" y="109" fill="currentColor" font-family="monospace">0</text> <text x="463.0" y="109" fill="currentColor" font-family="monospace">0</text> <text x="487.0" y="109" fill="currentColor" font-family="monospace">0</text> <text x="511.0" y="109" fill="currentColor" font-family="monospace">0</text> <text x="535.0" y="109" fill="currentColor" font-family="monospace">0</text> <text x="559.0" y="109" fill="currentColor" font-family="monospace">0</text>
  <rect x="10" y="132" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="143" fill="currentColor">R 실행</text> <rect x="86" y="132" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="106" y="143" fill="currentColor">r READY(CPU 대기)</text> <rect x="261" y="132" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="281" y="143" fill="currentColor">B 세마포어 대기</text> <rect x="382" y="132" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="402" y="143" fill="currentColor">s sleep (보유 중 또는 쉬는 중)</text>
  <text x="10" y="166" fill="currentColor">take 5번은 블록 없음. 6번째 take(tick 17)만 B → tick 20 의 ISR give 가 직접 넘겨준다.</text>
</svg>
```

- `sem` 이 1, 2, 3, 4, 5 로 쌓인다 — **이벤트를 하나도 잃지 않았다**. 바이너리였다면 1 에서 멈추고 4개를 잃는다 (RTOS-B08).
- tick 7, 9, 11, 13, 15 의 take 는 모두 즉시 성공 (`fast takes=5`), 6번째 (tick 17) 에서 처음 블록 → tick 20 ISR give 가 직접 넘겨준다. 그 ISR 은 idle 에 끼어들었으므로 `woken = true` (체크 `g_late_woken`).
- 카운팅 세마포어의 count = "처리 안 된 이벤트 수" = 백로그. 실전에서는 `uxSemaphoreGetCount()` 로 이 값을 텔레메트리로 내보내면 파서가 밀리는지 보인다.

### Part B5 — 포화: max 에 닿은 give 는 버려진다

```text
Part B5 saturation (max 4, 6 gives): ok=4 full=2 drops=2 fast=4 processed=5
Part B5 binary   (max 1, 6 gives): ok=1 full=5 fast=1 processed=2
```

| max | give 6번 | 블록 없는 take | 잃은 이벤트 |
|---|---|---|---|
| 8 (B4) | 5번 모두 OK | 5 | 0 |
| 4 | OK 4, `SEM_FULL` 2 | 4 | **2** |
| 1 (바이너리) | OK 1, `SEM_FULL` 5 | 1 | **5** |

max 는 "얼마나 밀려도 되나" 의 설계값이다. 포화는 버그가 아니라 **오버런** 이고, ISR 은 `SEM_FULL` 을 받으면 오버런 카운터를 올려야 한다 (펌웨어에서 조용히 잃는 것이 진짜 버그). 이벤트에 데이터가 붙어 있으면(바이트, 길이) 세마포어가 아니라 큐/링 버퍼가 맞다 — 연습 4, RTOS-B11.

### Part C — DMA 채널 드라이버

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">세마포어 = 몇 개, 비트맵 = 어느 것 (DMA 채널 3개)</text>
  <rect x="20" y="45" width="200" height="90" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="68" fill="currentColor" font-weight="bold">태스크 (ADC, FLASH ...)</text>
  <text x="32" y="92" fill="currentColor" font-family="monospace">① take(free)</text>
  <text x="32" y="112" fill="currentColor" font-family="monospace">② claim: crit{used|=bit}</text>
  <text x="32" y="130" fill="currentColor" font-family="monospace">③ start DMA(ch, buf)</text>
  <rect x="270" y="45" width="200" height="90" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="282" y="68" fill="currentColor" font-family="monospace">free.count = 1</text>
  <text x="282" y="96" fill="currentColor" font-family="monospace">used = 0b011</text>
  <text x="282" y="120" fill="currentColor">ch0, ch1 사용 · ch2 빔</text>
  <rect x="520" y="45" width="200" height="90" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="532" y="68" fill="currentColor" font-weight="bold">DMA 완료 ISR (TCIF)</text>
  <text x="532" y="92" fill="currentColor" font-family="monospace">① used &amp;= ~bit</text>
  <text x="532" y="112" fill="currentColor" font-family="monospace">② give_from_isr(free)</text>
  <text x="532" y="130" fill="currentColor" font-family="monospace">③ yield if woken</text>
  <path d="M220 90 L268 90" stroke="currentColor" stroke-width="2"/>
  <path d="M258 84 L268 90 L258 96" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M520 90 L472 90" stroke="currentColor" stroke-width="2"/>
  <path d="M482 84 L472 90 L482 96" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="20" y="170" fill="currentColor">used 는 태스크(set)와 ISR(clear)이 함께 RMW 한다 → 태스크 쪽은 임계 구역 필수 (C-M14).</text>
  <text x="20" y="194" fill="currentColor">토큰을 가진 태스크에게는 빈 비트가 반드시 있다: 빈 비트 수 ≥ count + 넘겨받고 아직 claim 안 한 수.</text>
  <text x="20" y="218" fill="currentColor">세마포어에는 소유자가 없다 — 누가 어느 채널을 쥐었는지는 드라이버(owner[])가 기록한다.</text>
  <text x="20" y="242" fill="currentColor">채널 하나를 정해서 써야 하면(특정 스트림) 채널마다 세마포어/뮤텍스 (RTOS-B10).</text>
</svg>
```

`dma_claim` = `crit_enter()` → 가장 낮은 빈 비트를 `dma_mark_used` → `crit_exit()`. `dma_complete_isr` = `dma_mark_free` (비트 먼저) → `ksem_give_from_isr` (그다음 토큰). 코드는 6. 정답 코드. 실제 출력:

```text
Part C DMA: transfers=20 (ch0=7 ch1=7 ch2=6), woken=true 12 times, tasks done at tick 39
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part C 실제 타임라인 — DMA 완료 ISR 이 채널을 반납 (solution.c 출력)</text>
  <text x="71" y="38" fill="currentColor">0</text> <text x="146" y="38" fill="currentColor">5</text> <text x="221" y="38" fill="currentColor">10</text> <text x="296" y="38" fill="currentColor">15</text> <text x="371" y="38" fill="currentColor">20</text> <text x="446" y="38" fill="currentColor">25</text> <text x="521" y="38" fill="currentColor">30</text> <text x="596" y="38" fill="currentColor">35</text> <rect x="70.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="85.0" y="48" width="30.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="115.0" y="48" width="30.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="145.0" y="48" width="30.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="175.0" y="48" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="190.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="205.0" y="48" width="15.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="220.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="235.0" y="48" width="30.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="265.0" y="48" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="280.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="295.0" y="48" width="30.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="325.0" y="48" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="61" fill="currentColor" font-family="monospace">ADC</text> <text x="208.0" y="61" fill="currentColor">B</text> <text x="298.0" y="61" fill="currentColor">B</text> <rect x="70.0" y="72" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="85.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="100.0" y="72" width="30.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="130.0" y="72" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="145.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="160.0" y="72" width="15.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="175.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="190.0" y="72" width="30.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="220.0" y="72" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="235.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="250.0" y="72" width="15.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="265.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="280.0" y="72" width="30.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="310.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="325.0" y="72" width="30.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="355.0" y="72" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="85" fill="currentColor" font-family="monospace">I2S</text> <text x="163.0" y="85" fill="currentColor">B</text> <text x="253.0" y="85" fill="currentColor">B</text> <text x="328.0" y="85" fill="currentColor">B</text> <rect x="85.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="100.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="115.0" y="96" width="90.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="205.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="220.0" y="96" width="60.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="280.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="295.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="310.0" y="96" width="60.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="370.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="385.0" y="96" width="30.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="415.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="430.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="445.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="460.0" y="96" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="96" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="109" fill="currentColor" font-family="monospace">FLASH</text> <text x="223.0" y="109" fill="currentColor">B</text> <text x="388.0" y="109" fill="currentColor">B</text> <rect x="85.0" y="120" width="75.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="160.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="175.0" y="120" width="15.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="190.0" y="120" width="60.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="250.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="265.0" y="120" width="15.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="280.0" y="120" width="60.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="340.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="355.0" y="120" width="30.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="385.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="400.0" y="120" width="15.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="415.0" y="120" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="430.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="445.0" y="120" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="460.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="120" width="15.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="490.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="505.0" y="120" width="30.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="535.0" y="120" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="133" fill="currentColor" font-family="monospace">UART</text> <text x="178.0" y="133" fill="currentColor">B</text> <text x="358.0" y="133" fill="currentColor">B</text> <text x="508.0" y="133" fill="currentColor">B</text> <rect x="100.0" y="144" width="300.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="400.0" y="144" width="15.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="415.0" y="144" width="120.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="535.0" y="144" width="15.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="550.0" y="144" width="30.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="580.0" y="144" width="15.0" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="595.0" y="144" width="60.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="157" fill="currentColor" font-family="monospace">LOG</text> <text x="418.0" y="157" fill="currentColor">B</text> <text x="583.0" y="157" fill="currentColor">B</text>
  <text x="10" y="181" fill="currentColor" font-family="monospace">sem</text> <text x="74.5" y="181" fill="currentColor" font-family="monospace">2</text> <text x="89.5" y="181" fill="currentColor" font-family="monospace">1</text> <text x="104.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="119.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="134.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="149.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="164.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="179.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="194.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="209.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="224.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="239.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="254.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="269.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="284.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="299.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="314.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="329.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="344.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="359.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="374.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="389.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="404.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="419.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="434.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="449.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="464.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="479.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="494.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="509.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="524.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="539.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="554.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="569.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="584.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="599.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="614.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="629.5" y="181" fill="currentColor" font-family="monospace">0</text> <text x="644.5" y="181" fill="currentColor" font-family="monospace">0</text>
  <rect x="10" y="204" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="215" fill="currentColor">R 실행</text> <rect x="86" y="204" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="106" y="215" fill="currentColor">r READY(CPU 대기)</text> <rect x="261" y="204" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="281" y="215" fill="currentColor">B 세마포어 대기</text> <rect x="382" y="204" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="402" y="215" fill="currentColor">s sleep (보유 중 또는 쉬는 중)</text>
  <text x="10" y="238" fill="currentColor">LOG(우선순위 1)는 sem=0 인 동안 뒤로 밀린다 — 대기 리스트가 우선순위 순.</text>
</svg>
```

- 20번 완료, `g_claim_fail = g_start_err = g_crit_err = 0`. 끝나면 `used == 0`, `free.count == 3`.
- 완료 ISR 20번 중 12번 `woken = true`: 끼어든 태스크보다 높은 대기자를 깨운 경우. 나머지 8번은 대기자가 없거나 (count++) 깨운 태스크가 끼어든 태스크보다 낮거나 같다. 하네스가 ISR 직전 태스크 표로 기대값을 계산해 20번 모두 일치.
- LOG(p1) 는 tick 2 에 준비됐지만 tick 22 에야 처음 돈다. 채널을 기다린 것이 아니라 **CPU** 를 기다렸다 (`r`). 그다음 tick 23–30 은 `B` — 채널 대기 리스트에서도 높은 태스크들에게 계속 밀린다.

실제 DMA 완료 ISR 은 이런 모양이다 (FreeRTOS, STM32 HAL 스타일 — 이 맥에서 빌드하지 않은 예시):

```c
void DMA2_Stream0_IRQHandler(void) {
    BaseType_t woken = pdFALSE;
    if (DMA2->LISR & DMA_LISR_TCIF0) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF0;          /* W1C 플래그 지우기 (C-M02) */
        dma_used &= ~(1u << 0);                  /* ISR 안: 같은/낮은 IRQ 가 못 끼어듦 */
        xSemaphoreGiveFromISR(dma_free, &woken);
    }
    portYIELD_FROM_ISR(woken);                   /* woken 이면 PendSV → ISR 끝나자마자 그 태스크로 */
}
```

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | FreeRTOS (V11.3.2 소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `ksem_init(&s, init, max)` | `xSemaphoreCreateCounting(max, init)` — **인자 순서 반대** (semphr.h:1025) | `k_sem_init(&s, init, limit)` / `K_SEM_DEFINE` | `osSemaphoreNew(max, init, NULL)` |
| `ksem_take` (영원히) | `xSemaphoreTake(h, portMAX_DELAY)` (semphr.h:298) | `k_sem_take(&s, K_FOREVER)` | `osSemaphoreAcquire(id, osWaitForever)` |
| try / 타임아웃 (연습 1) | `xSemaphoreTake(h, 0)` / `(h, pdMS_TO_TICKS(10))` | `K_NO_WAIT` / `K_MSEC(10)` | `osSemaphoreAcquire(id, 0)` |
| ISR 에서 take (연습 4) | `xSemaphoreTakeFromISR(h, &woken)` (semphr.h:675) | `k_sem_take(&s, K_NO_WAIT)` 만 | timeout 0 만 |
| `ksem_give` | `xSemaphoreGive(h)` → pdPASS / errQUEUE_FULL | `k_sem_give(&s)` — void, limit 이면 count 그대로 | `osSemaphoreRelease(id)` → osOK / osErrorResource |
| `ksem_give_from_isr(&s, &woken)` + 다음 tick 재스케줄 | `xSemaphoreGiveFromISR(h, &woken)` (semphr.h:640) + `portYIELD_FROM_ISR(woken)` | ISR 에서도 `k_sem_give`, 재스케줄은 커널이 ISR 출구에서 | `osSemaphoreRelease` (ISR 가능) |
| `s.count` | `uxSemaphoreGetCount(h)` (semphr.h:1177) | `k_sem_count_get(&s)` | `osSemaphoreGetCount(id)` |
| `crit_enter/exit` | `taskENTER_CRITICAL()` / ISR 에서는 `taskENTER_CRITICAL_FROM_ISR()` | `irq_lock()` / `k_spinlock` | CMSIS-Core `__disable_irq()` + PRIMASK 복원 (C-M14) |
| 대기 순서 | 우선순위 → FIFO (list.c:192) | 문서: "highest priority thread that has waited longest" | 구현(RTX5 등) 의존 |
| give 와 대기자 | barging (count++ 후 깨움, 재확인 루프) | 소스상 대기자에게 직접 넘기는 것으로 알고 있음 — 미검증 | 구현 의존 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

출력은 5. 풀이에 실은 타임라인·요약 줄 + 마지막 `33/33 passed`. 6번 반복 실행 모두 `33/33 passed` 이고 출력 md5 가 6번 모두 같았다(결정적). 새니타이저 (경고 0, 종료 코드 0):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o so_tsan && ./so_tsan    # 스레드가 없어 형식상 확인만
```

pthread 를 쓰지 않는 단일 스레드 시뮬레이터라서 데이터 레이스가 생길 수 없다. 대신 이 레슨의 버그(깨우는 순서, 토큰 분실, 포화 무시)는 전부 **로직 버그** 라서 상태 불변식과 오라클로 잡는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 정렬 삽입에서 `>` | 같은 우선순위가 LIFO 가 된다. B2 가 `W3 W5 W2 W4 W1` 이 됨 | `while (*pp && (*pp)->prio >= t->prio)` |
| 대기자가 있는데 give 에서 `count++` 하고 깨우기만 (재확인 없이) | 깨어난 태스크가 "가졌다" 고 믿는데 count 도 1 → 토큰 2개가 생김 (보존 위반). barging 을 하려면 깨어난 쪽이 반드시 다시 take | handoff (`token = 1`, count 그대로) 또는 연습 3 의 재시도 루프 |
| 포화를 무시 (`count++` 무조건) | max 를 넘어 count 가 커지고, 자원 풀이면 **없는 채널** 을 나눠 준다 | `if (count < max) count++; else return SEM_FULL;` |
| ISR 에서 블록하는 take | ISR 은 태스크가 아니라 잠들 수 없다. FreeRTOS 는 `configASSERT` 에 걸리거나 망가진다 | ISR 에서는 `...FromISR` (give, 또는 non-blocking take) |
| `woken = (w->prio > cur)` 로 대입 | 한 ISR 에서 give 를 여러 번 하면 앞의 true 를 false 로 덮는다 → 선점 누락 | `if (...) *woken = true;` (초기값 false 는 호출자가) |
| `portYIELD_FROM_ISR` 생략 | 높은 태스크를 깨웠는데 다음 tick 인터럽트까지 낮은 태스크가 계속 돈다 (최대 1 tick 지연) | ISR 끝에서 `portYIELD_FROM_ISR(woken)` |
| DMA ISR 에서 give 먼저, 비트 clear 나중 | task 문맥 give 라면 즉시 선점된 태스크가 빈 비트를 못 찾는다. ISR 안에서는 운 좋게 동작해도 순서가 의미를 갖도록 쓴다 | 상태(비트) 먼저, 신호(give) 나중 |
| 비트맵 RMW 를 임계 구역 없이 | ISR 의 clear 와 태스크의 set 이 엇갈리면 비트 하나를 잃는다 (C-M14 lost update) | `crit_enter(); used \|= bit; crit_exit();` |
| 세마포어로 "어느 채널" 까지 해결하려 함 | 세마포어엔 소유자도 이름도 없다 | 세마포어(개수) + 비트맵/owner(어느 것) |
| `xSemaphoreCreateCounting(init, max)` | 인자 순서는 `(uxMaxCount, uxInitialCount)` | `xSemaphoreCreateCounting(3, 3)` |

## 8. 면접 Q&A

<details>
<summary>Q1. 바이너리 세마포어와 카운팅 세마포어의 차이는? 각각 언제 쓰나요?</summary>

바이너리 = max 1 인 카운팅 세마포어다. 차이는 **기억하는 개수**. 바이너리는 "1번 이상 일어났다" 만 기억해서, 처리보다 빠르게 온 신호는 합쳐진다 (B5: give 6번 → 1번만 남고 5번 `SEM_FULL`). 카운팅은 max 까지 개수를 쌓는다 → ① 이벤트 카운트(UART 프레임 수, 처리 안 된 백로그) ② N개 동일 자원 풀(DMA 채널, 버퍼). 꼬리: "카운팅 세마포어로 이벤트 데이터도 넘길 수 있나?" → 아니다. 개수만 센다. 데이터는 큐/링 (연습 4: 링 + 세마포어 2개, RTOS-B11 큐). 꼬리2: "FreeRTOS 에서 더 가벼운 대안?" → 수신자가 1명이면 태스크 알림 `xTaskNotifyGive` / `ulTaskNotifyTake` 가 카운팅 세마포어를 대신한다 (RTOS-M01).

</details>

<details>
<summary>Q2. 기다리는 태스크가 여럿일 때 give 하면 누가 깨어나나요?</summary>

FreeRTOS: **가장 높은 우선순위, 같으면 가장 오래 기다린 태스크**. 이벤트 리스트가 `configMAX_PRIORITIES - priority` 로 정렬되고 (tasks.c:5412–5423), 삽입이 `<=` 로 같은 값 뒤에 붙으며 (list.c:192), 깨울 땐 맨 앞을 꺼낸다 (tasks.c:5523). Zephyr 문서도 "highest priority thread that has waited longest" 다. 이 레슨은 같은 규칙을 구현하고 B2 (`W3 W2 W5 W1 W4`) + 독립 오라클로 검증했다. 꼬리: "POSIX `sem_post` 는?" → 우선순위 스케줄링(SCHED_FIFO/RR) 아래서는 우선순위 순이라고 규정하지만, 그 밖은 미지정이다. 꼬리2: "낮은 우선순위 대기자가 굶을 수 있나?" → 있다. 높은 태스크들이 쉬지 않고 원하면 영원히 밀린다 (연습 2: L 이 tick 36 까지, FIFO 면 tick 9).

</details>

<details>
<summary>Q3. ISR 에서 take 는 왜 안 되고 give 는 되나요? <code>xHigherPriorityTaskWoken</code> 은 뭐죠?</summary>

take 는 count 가 0 이면 **잠들어야** 하는데 ISR 은 태스크가 아니라 잠들 문맥(TCB, 스택, 대기 리스트 항목)이 없다. 잠들면 인터럽트된 태스크까지 멈춘다. give 는 count++ 또는 대기자 하나를 READY 로 옮기는 유한한 일이라 블록이 없다. 단, ISR 판(`xSemaphoreGiveFromISR`)은 그 자리에서 문맥 교환을 하지 않고, 깨운 태스크가 끼어든 태스크보다 높으면 플래그를 세운다. ISR 끝의 `portYIELD_FROM_ISR(woken)` 가 Cortex-M 에서 PendSV 를 걸어, ISR 이 끝나자마자 그 태스크로 바뀐다. 안 부르면 다음 SysTick 까지 늦어진다. 꼬리: "ISR 에서 non-blocking take 는?" → `xSemaphoreTakeFromISR` 이 있다 (semphr.h:675, 연습 4 의 빈 칸 확보). 꼬리2: "ISR 에서 FreeRTOS API 를 아무 우선순위에서나 불러도 되나?" → 아니다. Cortex-M 포트에서는 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 보다 논리적으로 낮은(숫자는 크거나 같은) 인터럽트에서만 (FreeRTOS 문서 기준).

</details>

<details>
<summary>Q4. 리소스 풀에 세마포어를 쓰면 "어느" 자원을 받았는지는 어떻게 알죠?</summary>

모른다. 세마포어는 개수만 센다. 그래서 드라이버가 비트맵이나 free list 로 "어느 것" 을 관리하고, 토큰을 받은 태스크가 그 안에서 하나를 고른다 (Part C `dma_claim`). 그 비트맵은 태스크와 ISR 이 함께 RMW 하므로 임계 구역이 필요하다. 세마포어의 count 와 비트맵의 빈 칸 수가 어긋나지 않도록 순서가 중요하다: 반납은 "비트 clear → give", 획득은 "take → 비트 set". 그러면 토큰을 가진 태스크에게 빈 비트가 항상 있다 (`g_claim_fail = 0`). 꼬리: "그냥 mutex + 비트맵이면?" → 다 찼을 때 **기다리는** 기능이 없다. 세마포어가 기다림을, 비트맵이 선택을 맡는다.

</details>

<details>
<summary>Q5. give 할 때 대기자에게 직접 넘기는 것과 count 를 올리고 깨우는 것의 차이는?</summary>

직접 넘기기(handoff): 깨어난 태스크는 이미 토큰을 가진다. 가로채기가 없어 공정하고 대기 시간이 예측 가능하지만, 넘겨받은 태스크가 CPU 를 못 받는 동안 자원이 논다 (B1 의 D: tick 6–12). 값 올리고 깨우기(barging): FreeRTOS 방식 (queue.c:2472 → 985–1014 → 깨어난 쪽이 queue.c:1686/1696 루프에서 재확인). 그 사이 더 높은 태스크나 give 한 태스크 자신이 가져갈 수 있다 — 처리량은 좋지만 깨어난 태스크는 헛걸음 후 다시 잠든다. 연습 3: 같은 시나리오에서 handoff 는 W 가 tick 3 에 받고 헛깨움 0, barging 은 G 가 4번 다 쓸 때까지 W 가 tick 9 에야 받고 토큰 없이 깨어난 횟수 4. 꼬리: "barging 이 위험한 곳은?" → 뮤텍스. 놓았다 바로 다시 잡는 루프에서 다른 태스크를 굶긴다(lock convoy/starvation). 리눅스 mutex 같은 구현은 오래 기다린 대기자에게 넘겨주는 장치(handoff)를 따로 둔다 (상세 미검증).

</details>

<details>
<summary>Q6. 세마포어로 상호 배제(뮤텍스 대신)를 해도 되나요?</summary>

count 1 로 만들면 동작은 한다. 하지만 세마포어에는 **소유자가 없다**: ① 아무 태스크나(ISR 도) give 할 수 있어 잘못된 반납을 못 잡는다 ② 소유자를 모르니 **우선순위 상속**을 할 수 없다 → 우선순위 역전 (RTOS-M03, Mars Pathfinder) ③ 재귀 take 를 하면 자기 자신과 교착. 그래서 상호 배제는 뮤텍스, 신호와 개수 세기는 세마포어. 자세한 비교는 RTOS-B10. 꼬리: "카운팅 세마포어 풀(N개)에서도 역전이 생기나?" → 생긴다. 낮은 태스크가 채널 3개를 다 쥐고 중간 태스크에 선점되면 높은 태스크가 무한정 기다릴 수 있다. 세마포어는 상속을 못 하므로 보유 구간을 짧게, 또는 타임아웃(연습 1)으로 막는다.

</details>

<details>
<summary>Q7. 이 코드를 어떻게 테스트했나요? 실제 RTOS 에서는?</summary>

단일 스레드 tick 시뮬레이터라서 모든 스케줄이 결정적이다. ① 매 tick 불변식: 보유자 ≤ N, `보유자 + 넘겨받은 수 + count == N`, `count > 0 이면 대기자 0`, 대기 리스트 길이 == BLOCKED 태스크 수 ② 독립 오라클: 깨울 때마다 태스크 표 전체에서 "최고 우선순위·최고참" 인지 ③ 각본 시나리오(B2 깨우는 순서, B4/B5 이벤트 카운트) ④ 무작위 300 시나리오(고정 seed). 실제 RTOS 에서는 같은 불변식을 `configASSERT` 로 넣고, 트레이스(SystemView, Tracealyzer)로 take/give 순서를 보고, `uxSemaphoreGetCount` 를 텔레메트리로 내보낸다. 꼬리: "모델이 실제와 다른 점은?" → 선점이 다음 tick 에 일어나는 것, 그리고 give 가 handoff 라는 것 (FreeRTOS 는 barging) — 5. 풀이 대응표.

</details>

## 9. 추가 연습

### 연습 1 — take 에 타임아웃

`ksem_take_timeout(s, self, ticks)` — `ticks` 안에 못 받으면 실패. `0` 이면 기다리지 않는다(try). 커널은 tick 마다 마감이 지난 BLOCKED 태스크를 `wait_remove()` 로 리스트에서 빼고 `timed_out = 1` 로 READY 로 만든다. 마감 tick 에 ISR 이 give 하면? 세 명 중 **가운데** 가 타임아웃되면 리스트가 맞게 이어지나?

<details>
<summary>답 보기</summary>

`wait_remove` 는 포인터의 포인터로 리스트를 걸으며 `*pp == t` 이면 `*pp = t->wnext` (머리·중간·꼬리 모두 같은 코드). 이 모델의 tick 순서는 "ISR → 타임아웃 처리" 라서 마감 tick 의 give 가 이긴다. 실제 출력:

```text
ex1 no give      : timed_out=1 got_at=-1 nwait=0
ex1 give@deadline: timed_out=0 got_at=5
```

a(p3) b(p2) c(p1) 중 b 가 tick 3 에 타임아웃 → 리스트 `a → c`, 이어서 give 두 번이 a, c 에게 간다. FreeRTOS 는 깨어난 태스크가 루프 맨 위에서 **count 를 먼저** 보고 (queue.c:1696) 그다음 `xTaskCheckForTimeOut` (queue.c:1779) 을 보므로, 마감 직전에 들어온 토큰은 받는다 — 같은 방향이다. 펌웨어에서 타임아웃은 "자원이 영원히 안 돌아오는" 버그(ISR 이 give 를 빼먹음)를 감지하는 안전장치다.

</details>

### 연습 2 — 우선순위 순서의 굶주림 vs FIFO

토큰 1개, H1·H2(p3) 가 쉬지 않고 6번씩 쓰고, L(p1) 이 한 번 쓰려 한다. 우선순위 순서와 FIFO 순서(`s->fifo = true`)에서 L 이 처음 받는 tick 을 비교하라.

<details>
<summary>답 보기</summary>

`wait_insert` 의 조건을 `(s->fifo || (*pp)->prio >= t->prio)` 로 — FIFO 면 무조건 맨 뒤. 실제 출력:

```text
ex2 priority: L first got token at tick 36 (H1/H2 6 rounds each, all done at 38)
ex2 FIFO    : L first got token at tick 9 (H1/H2 6 rounds each, all done at 38)
```

우선순위 순서에서는 H1 이 give 하고 바로 다시 take 하면 L(p1) **앞에** 끼어든다 → H 들이 일을 거의 다 끝낼 때까지 L 은 못 받는다. 일이 끝없다면 영원히. FIFO 는 대기 시간이 "앞에 선 사람 수 × 보유 시간" 으로 묶인다(bounded). 실시간 시스템은 대개 우선순위 순서를 고르고, 굶주림은 **설계(부하 분석, 보유 시간 제한)** 로 막는다. 끝나는 시각(38)은 같다 — 총 작업량은 같고 순서만 바뀐다.

</details>

### 연습 3 — barging give (FreeRTOS 방식) vs handoff

`ksem_give_barging`: count++ 후 맨 앞 대기자를 **토큰 없이** READY 로. 깨어난 태스크는 다시 take 한다. G(p3) 가 "take → 2 tick → give → 바로 take" 를 4번, W(p2) 는 tick 1 에 줄을 선다.

<details>
<summary>답 보기</summary>

실제 출력:

```text
ex3 handoff : W got token at tick 3, futile wakeups=0, done at 12
ex3 barging : W got token at tick 9, futile wakeups=4, done at 11
```

handoff: G 의 첫 give(tick 2) 가 W 에게 넘어가고, G 의 바로 다음 take 는 블록 → 번갈아 쓴다. barging: G 가 give 해서 count 1 → 바로 자기가 다시 take (W 는 READY 일 뿐 아직 못 돌았다) → W 가 돌면 count 0 → 다시 잠듦. 토큰 없이 깨어난 횟수 4 (3번은 재시도도 실패, 마지막 1번은 G 가 끝난 뒤라 재시도 성공). 대신 barging 이 1 tick 빨리 끝났다 — 자원이 "넘겨받고 아직 못 돈" 태스크를 기다리며 놀지 않기 때문. 공정성 vs 처리량. FreeRTOS 세마포어는 barging 쪽이고 (queue.c:2472, 1686–1696), 그래서 "give 직후 같은 태스크가 다시 take" 루프는 같은/낮은 우선순위 대기자를 굶길 수 있다. 고치려면 give 와 take 사이에 `taskYIELD()` 나 지연을 둔다(같은 우선순위일 때만 효과).

</details>

### 연습 4 — UART RX ISR → 4칸 링: 세마포어 2개

`slots`(빈 칸, 4/4) 과 `filled`(찬 칸, 0/4). ISR 은 `ksem_try_take_isr(&slots)` (non-blocking) 로 칸을 확보, 실패하면 오버런, 성공하면 바이트를 쓰고 `give_from_isr(&filled)`. 파서는 `take(filled)` → 읽기 → `give(slots)`. tick 1..12 에 바이트가 오고 MOTOR(p3) 가 tick 0–7 CPU 를 쥔다.

<details>
<summary>답 보기</summary>

실제 출력: `ex4 uart ring: sent=12 recv=8 overrun=4 slots=4 filled=0`. 바이트 1–4 는 칸에 들어가고, 5–8 은 파서가 못 돌아 칸이 없으므로 버려진다(오버런 4). tick 8 부터 파서가 칸을 비우며 9–12 는 모두 받는다. 체크: `recv + overrun == sent`, 받은 순서 증가, 끝에 `slots == 4 && filled == 0`. OSTEP 31장의 세마포어 bounded buffer 와 같은 구조인데, 생산자가 **ISR** 이라 빈 칸 기다리기 대신 버린다 (OS-B06 연습 2 의 `try_put`). 단일 생산자·단일 소비자이고 인덱스를 각자 하나씩만 쓰므로 링 자체에는 락이 없다 (실제 Cortex-M 에서는 인덱스를 `volatile`/원자적 32비트로 — DS-A01, C-M14). ISR 의 non-blocking take = `xSemaphoreTakeFromISR`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 15/15 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 15/15, 경고 0
```

## 10. 핵심 정리

- 카운팅 세마포어 = **count + 대기 리스트**. take 는 0 이면 블록(ISR 금지), give 는 절대 블록하지 않는다(ISR 가능, `...FromISR` + `portYIELD_FROM_ISR`).
- 두 용도: **자원 풀** (N개 동일 자원, 동시 보유 ≤ N — 불변식 `보유 + count == N`) 과 **이벤트 카운트** (give N번 → take N번 즉시 성공, count = 백로그).
- max 에 닿은 give 는 **버려진다** (FreeRTOS `errQUEUE_FULL`) → ISR 은 오버런을 센다. 바이너리 = max 1 → 합쳐짐 (RTOS-B08).
- 깨우는 순서: FreeRTOS 는 **우선순위 → 같으면 FIFO** (list.c:192 `<=`). 낮은 대기자는 굶을 수 있다 — FIFO 는 대기 시간을 묶는다.
- handoff(직접 넘김: 공정, 자원이 놀 수 있음) vs barging(count++ 후 깨움: FreeRTOS, 처리량↑, 헛깨움·가로채기).
- 세마포어는 "몇 개" 만 안다. "어느 것" 은 비트맵 + 임계 구역. 반납은 상태 먼저(비트 clear) → 신호(give).
- 소유자가 없으니 상호 배제엔 뮤텍스 (우선순위 상속 — RTOS-B10, M03, M04).

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `include/semphr.h` (42, 298, 460, 640, 675, 1025, 1177), `queue.c` (`xQueueCreateCountingSemaphore` 912–920, `xQueueGenericSend` 977–1096, `xQueueGiveFromISR` 1338–1498, `xQueueSemaphoreTake` 1659–1806, `prvCopyDataToQueue` 2472), `tasks.c` (`vTaskPlaceOnEventList` 5402–5423, `xTaskRemoveFromEventList` 5500–5560), `list.c` (`vListInsert` 139–192), `include/projdefs.h:62`: <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS API Reference — Semaphore / Mutexes (`xSemaphoreCreateCounting`, `xSemaphoreGiveFromISR`, `xSemaphoreTakeFromISR`, `uxSemaphoreGetCount`): <https://www.freertos.org> → Documentation → API Reference
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Chapter "Interrupt Management" (deferred interrupt processing, counting semaphores): <https://www.freertos.org/Documentation/02-Kernel/07-Books-and-manual/01-RTOS_book>
- Zephyr — Semaphores (`k_sem_init`, `k_sem_give`, `k_sem_take`, limit): <https://docs.zephyrproject.org/latest/kernel/services/synchronization/semaphores.html>
- CMSIS-RTOS2 — Semaphores (`osSemaphoreNew`, `osSemaphoreAcquire`, `osSemaphoreRelease`): <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__SemaphoreMgmt.html>
- E. W. Dijkstra, *Cooperating Sequential Processes* (EWD123, 1965) — P/V 세마포어의 원전: <https://www.cs.utexas.edu/~EWD/transcriptions/EWD01xx/EWD123.html>
- OSTEP 31장 *Semaphores* — 세마포어 bounded buffer (연습 4): <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-sema.pdf>
- POSIX `sem_post` — 우선순위 스케줄링 아래 깨우는 순서: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_post.html>
- 관련 레슨: C-M14 (ISR 공유 데이터·임계 구역), C-M02 (W1C 레지스터), OS-B06 (조건 변수 bounded buffer, try_put), RTOS-B08 (바이너리 세마포어·이벤트 합쳐짐), RTOS-B10 (뮤텍스 vs 세마포어), RTOS-B11 (큐), RTOS-M01 (태스크 알림), RTOS-M03/M04/M05 (우선순위 역전·상속·천장), DS-A01 (lock-free SPSC 링)

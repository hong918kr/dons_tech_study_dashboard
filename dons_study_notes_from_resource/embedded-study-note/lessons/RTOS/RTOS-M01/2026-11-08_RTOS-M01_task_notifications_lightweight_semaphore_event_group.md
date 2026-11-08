---
id: RTOS-M01
time: 85분
updated: 2026-11-08
---
# RTOS-M01 태스크 알림: 세마포어·이벤트 그룹의 경량 대체

## 학습 목표

- FreeRTOS 의 태스크 알림(direct-to-task notification)이 **TCB 안의 `uint32_t` 값 + `uint8_t` 상태 한 쌍** 이라는 것을 알고, 다섯 가지 동작(`eSetBits` / `eIncrement` / `eSetValueWithOverwrite` / `eSetValueWithoutOverwrite` / `eNoAction`)과 받는 쪽 두 API(`ulTaskNotifyTake` 의 clear-on-exit vs decrement, `xTaskNotifyWait` 의 entry/exit 마스크)를 소스 줄 번호로 설명할 수 있다.
- 바이너리 세마포어 → `vTaskNotifyGiveFromISR` + `ulTaskNotifyTake`, 카운팅 세마포어 → decrement 모드, 이벤트 그룹 → `eSetBits` + `xTaskNotifyWait`, 길이 1 큐 → overwrite 메일박스로 **바꿔 쓰는 법** 과, 바꾸면 안 되는 경우(수신자 둘 이상, wait-all, 뮤텍스)를 구분한다.
- RAM 을 실측으로 비교한다: Cortex-M4F 에서 세마포어 72 B(heap_4 로 80 B), 이벤트 그룹 28 B(40 B) vs 알림 0 B 추가 — 단 **인덱스를 늘리면 모든 TCB** 가 4–8 B 씩 커진다.
- 호출당 시간을 호스트에서 재고, 그 숫자가 무엇을 재는지(POSIX 포트의 임계 구역 = 시스템 콜) 정직하게 해석한다.
- 알림 배열 인덱스를 나눠 쓰는 UART 드라이버를 짜고, 인덱스 하나에 섞을 때 생기는 "TX 완료 착각 + RX 이벤트 증발" 버그와 타임아웃 뒤 늦게 온 알림(stale notification) 버그를 막는다.

## 1. 문제

"세마포어 대신 태스크 알림을 쓰면 왜 빠르고 RAM 이 적나요? 그럼 항상 알림을 쓰면 되나요?" — FreeRTOS 를 쓰는 펌웨어 팀 면접에서 RTOS-B08(ISR → 태스크 바이너리 세마포어) 다음에 바로 나오는 질문이다. 정답은 "중간 객체가 없어서" 와 "**받는 태스크가 정확히 하나일 때만**" 두 가지를 같이 말하는 것이다.

UART RX ISR 이 파서 태스크를 깨우는 흔한 코드를 보자. 세마포어를 쓰면 ISR → **세마포어 객체(Queue_t)** → 그 객체의 대기 리스트 → 태스크 순서로 간다. 알림은 ISR 이 **태스크의 TCB 를 직접** 고친다. 객체가 없으니 객체 RAM 이 없고, 대기 리스트를 거치지 않으니 경로도 짧다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ISR → 태스크 신호: 중간 객체를 거치느냐, TCB 를 직접 고치느냐</text>
  <text x="10" y="60" fill="currentColor">세마포어</text>
  <rect x="90" y="40" width="110" height="36" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="102" y="63" fill="currentColor" font-family="monospace">UART_IRQ</text>
  <rect x="235" y="34" width="210" height="96" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="245" y="54" fill="currentColor" font-weight="bold">Queue_t (72 B, M4F)</text>
  <text x="245" y="74" fill="currentColor" font-family="monospace">uxMessagesWaiting++</text>
  <text x="245" y="94" fill="currentColor" font-family="monospace">cTxLock 검사</text>
  <text x="245" y="114" fill="currentColor" font-family="monospace">xTasksWaitingToReceive</text>
  <rect x="480" y="40" width="120" height="36" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="492" y="63" fill="currentColor">parser TCB</text>
  <rect x="630" y="40" width="100" height="36" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="645" y="63" fill="currentColor">ready list</text>
  <path d="M200 58 L235 58" stroke="currentColor" fill="none"/>
  <path d="M445 58 L480 58" stroke="currentColor" fill="none"/>
  <path d="M600 58 L630 58" stroke="currentColor" fill="none"/>
  <text x="452" y="96" fill="currentColor">이벤트 리스트 + 상태 리스트</text>
  <text x="452" y="114" fill="currentColor">둘 다에서 빼기</text>
  <line x1="10" y1="150" x2="730" y2="150" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="190" fill="currentColor">알림</text>
  <rect x="90" y="170" width="110" height="36" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="102" y="193" fill="currentColor" font-family="monospace">UART_IRQ</text>
  <rect x="235" y="164" width="365" height="70" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="245" y="184" fill="currentColor" font-weight="bold">parser TCB 안 (추가 객체 0 B)</text>
  <text x="245" y="204" fill="currentColor" font-family="monospace">ulNotifiedValue[0]++</text>
  <text x="245" y="224" fill="currentColor" font-family="monospace">ucNotifyState[0] = RECEIVED</text>
  <rect x="630" y="170" width="100" height="36" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="645" y="193" fill="currentColor">ready list</text>
  <path d="M200 188 L235 188" stroke="currentColor" fill="none"/>
  <path d="M600 188 L630 188" stroke="currentColor" fill="none"/>
  <rect x="10" y="248" width="720" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="268" fill="currentColor">대가: 보내는 쪽이 받는 태스크의 핸들을 알아야 하고, 받는 태스크는 하나뿐이다.</text>
  <text x="22" y="286" fill="currentColor">객체가 없으니 "여러 태스크가 같은 것을 기다린다" 는 표현 자체가 불가능하다.</text>
</svg>
```

호스트(맥)에서는 진짜 커널 대신 **알림 의미론의 작은 모델** 을 직접 만든다. `ntask_t` 가 TCB 의 알림 부분(`value[2]`, `state[2]`, `prio`, `blocked_on`)만 가진다. 블로킹 API 는 두 함수로 나눈다:

| FreeRTOS (tasks.c V11.3.2) | 모델 | 하는 일 |
|---|---|---|
| `ulTaskGenericNotifyTake` 앞부분 (7810–7826) | `nt_take_begin(t, idx, ticks)` | 값이 0 이고 ticks &gt; 0 이면 WAITING → 1(블록) |
| 같은 함수 뒷부분 (7864–7886) | `nt_take_end(t, idx, clear)` | 값 반환, clear ? 0 : −1, 상태 NOT_WAITING |
| `xTaskGenericNotifyWait` 앞부분 (7913–7934) | `nt_wait_begin(t, idx, clr_entry, ticks)` | 읽지 않은 알림이 없고 ticks &gt; 0 일 때만 entry 마스크 + WAITING |
| 같은 함수 뒷부분 (7975–7999) | `nt_wait_end(t, idx, clr_exit, &out)` | 값 출력, 받았으면 exit 마스크 + PASS, 아니면 FAIL |
| `xTaskGenericNotify` (8013–8126) | `nt_notify(t, idx, v, action, &prev)` | 5가지 동작 + WAITING 이면 깨우기 |
| `xTaskGenericNotifyFromISR` (8133–) | `nt_notify_from_isr(..., running_prio, &hptw)` | + 깨운 태스크가 더 높으면 `*hptw = 1` |

테스트는 "begin → ISR 함수 호출 → end" 순서를 직접 정하므로 스레드도, 경쟁도, 시간도 없다. **같은 시나리오를 진짜 FreeRTOS(POSIX 포트)로 돌린 `demo_freertos_notify.c` 가 39개 항목 모두 모델과 같은 값을 냈다** (5. 풀이). RAM 과 시간은 진짜 커널로만 잰다(`demo_freertos_bench.c`, `demo_sizes_cm4.c`).

### Part A — 알림 커널 모델

`nt_notify`, `nt_notify_from_isr`, `nt_give_from_isr`, `nt_take_begin/end`, `nt_wait_begin/end` 를 채운다. 테스트가 확인하는 미묘한 점:

- `eSetValueWithoutOverwrite` 는 "값이 0 이 아닌가" 가 아니라 **상태가 RECEIVED 인가** 로 실패를 판단한다.
- `xTaskNotifyWait` 의 entry 마스크는 **실제로 블록할 때만** 적용된다 (이미 알림이 와 있거나 ticks == 0 이면 무시).
- `*pulNotificationValue` 는 exit 마스크 적용 **전** 값이다.
- `ulTaskNotifyTake` 는 상태가 아니라 **값** 을 본다 — `eNoAction` 으로 깨우면 깨어나서 0 을 돌려준다. 비트 값 `0x8` 도 "8개" 로 센다.
- FromISR 의 `*pxHigherPriorityTaskWoken` 은 1 로만 쓰고 0 으로 되돌리지 않는다.

### Part B — 대체 패턴과 한계

- `adc_post(t, sample, keep_unread, &dropped)` — ADC 완료 ISR 의 메일박스. keep_unread 면 NoOverwrite(실패 시 드롭 카운트), 아니면 Overwrite(최신 값).
- `fusion_on_wake(got, need, &acc)` — 이벤트 그룹의 `xWaitForAllBits` 를 흉내: 깨어날 때마다 비트를 모은다. 테스트가 **깨어나는 횟수** 를 이벤트 그룹 모델과 비교한다 (3 vs 1).
- `notify_fanout_from_isr(ts, n, idx, bits, running_prio, &hptw)` — 수신자가 둘일 때의 우회. 이벤트 그룹 모델은 `set` 한 번에 둘 다 깨운다.

### Part C — UART 드라이버: 인덱스 0 = 앱 이벤트, 인덱스 1 = 드라이버 전용

STM32 USART 처럼 `ISR` 상태 레지스터(IDLE=bit4, ORE=bit3, TC=bit6)와 1 을 쓰면 지워지는 `ICR`(W1C) 이 있다. comm 태스크는 메인 루프에서 `xTaskNotifyWaitIndexed(0, ...)` 로 RX 이벤트를 기다리고, 응답을 보낼 때는 드라이버 함수가 `ulTaskNotifyTakeIndexed(1, pdTRUE, timeout)` 으로 TX 완료를 기다린다.

- `usart_irq(u, comm, running_prio)` — IDLE/ORE → 인덱스 0 에 비트 OR **한 번**, TC → 인덱스 1 에 give, 처리한 플래그만 ICR 로 지우고, `portYIELD_FROM_ISR` 에 넘길 값을 반환.
- 주어진 `usart_irq_one_index()` 는 인덱스를 하나만 쓰는 버그 버전이다. 테스트가 "TX 를 기다리던 중 RX 프레임이 오면 TX 완료로 착각하고, 그 RX 이벤트는 사라진다" 를 재현한다.

## 2. Problem (English)

> **Task notifications as lightweight semaphores and event groups.**
>
> Model FreeRTOS direct-to-task notifications. A task `ntask_t` has `uint32_t value[2]`, `uint8_t state[2]` (`NOT_WAITING`=0, `WAITING`=1, `RECEIVED`=2), a priority, and `blocked_on` (index it is blocked on, or −1). Blocking calls are split into `*_begin` (decide whether to block) and `*_end` (after wake-up, timeout, or no block).
>
> 1. `int nt_notify(t, idx, v, action, uint32_t *prev)` for `eNoAction`, `eSetBits`, `eIncrement`, `eSetValueWithOverwrite`, `eSetValueWithoutOverwrite` (fails iff an unread notification is pending). Always marks `RECEIVED`; wakes the task if it was `WAITING`. `nt_notify_from_isr(..., running_prio, int *hptw)` also sets `*hptw = 1` (never 0) when the woken task outranks the running one.
> 2. `nt_take_begin/end` = `ulTaskNotifyTake(xClearCountOnExit, ticks)`: block only if `value == 0 && ticks > 0`; on exit return the value and either clear it or decrement it.
> 3. `nt_wait_begin/end` = `xTaskNotifyWait(clrEntry, clrExit, &out, ticks)`: block (and apply `clrEntry`) only if nothing is pending and `ticks > 0`; on exit output the value before `clrExit`, apply `clrExit` and return pass only if a notification was received.
> 4. Use them to replace a binary/counting semaphore, a length-1 mailbox, and an event group (`wait-all` emulation), and write a USART ISR that keeps application events (index 0) and driver TX-complete (index 1) apart.
>
> Example: five `vTaskNotifyGiveFromISR` while the handler is busy → `ulTaskNotifyTake(pdTRUE, 0)` returns 5 (a binary semaphore would have kept only 1). `eSetBits 0x1`, `0x4`, then `xTaskNotifyWait(0, 0x1, &v, 0)` → `pdTRUE`, `v == 0x5`, value left `0x4`.
>
> Follow-ups: why can't two tasks wait on one notification? Can an ISR receive one? Why is `xEventGroupSetBitsFromISR` slower than `xTaskNotifyFromISR`? What does raising `configTASK_NOTIFICATION_ARRAY_ENTRIES` cost?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/RTOS-M01_st && /tmp/RTOS-M01_st
# 처음엔 "26/84 passed", exit 1 — TODO 를 채워 84/84 를 만든다
```

비교용 세마포어(`csem_t`)와 이벤트 그룹(`egroup_t`, `eg_set_bits`) 모델, USART 레지스터 모델, 버그 버전 ISR 은 주어진 코드다. 고치지 않는다.

진짜 FreeRTOS 로 확인하는 데모 세 개 (체크 대상 아님, 커널 소스 `~/.esn_scratch/FreeRTOS-Kernel` V11.3.2 필요):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
# ① 의미론: 모델과 같은 시나리오를 진짜 커널로 (heap_3)
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_notify.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o /tmp/RTOS-M01_dn && /tmp/RTOS-M01_dn
# ② RAM(heap_4 사용량) + 호출당 시간 (heap_4)
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_bench.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_4.c $P/port.c $P/utils/wait_for_event.c \
   -o /tmp/RTOS-M01_db && /tmp/RTOS-M01_db
# ③ Cortex-M4F 객체 크기 · 코드 크기 (컴파일만, clang --target=thumbv7em-none-eabi)
bash cm4/measure_cm4.sh
```

레슨 폴더의 `FreeRTOSConfig.h` 는 `~/.esn_scratch/freertos-posix-mac` 템플릿에 `configTASK_NOTIFICATION_ARRAY_ENTRIES 2` 만 더한 것이다. `cm4/` 에는 Cortex-M4F 용 최소 설정과, 프리스탠딩 타깃에 없는 `<stdlib.h>`/`<string.h>` 를 대신하는 두 줄짜리 스텁이 있다. 커널 소스 파일은 경고 0개로 빌드됐다.

## 4. 힌트

<details>
<summary>힌트 1 — nt_notify: 원래 상태를 먼저 저장한다</summary>

깨울지(원래 WAITING 이었나), NoOverwrite 가 실패할지(원래 RECEIVED 였나) 둘 다 **바꾸기 전 상태** 로 판단한다. 그래서 `orig = state[idx]` → `state[idx] = RECEIVED` → `switch(action)` → `if (orig == WAITING) 깨우기` 순서다. FreeRTOS 도 이 순서다 (tasks.c 8036–8038, 8054–8066, 8088).

</details>

<details>
<summary>힌트 2 — Take 는 값을, Wait 는 상태를 본다</summary>

`ulTaskNotifyTake` 는 "세마포어 대용" 이라 **카운트 == 값** 이다: 값이 0 일 때만 블록하고, 끝나면 값을 돌려준 뒤 0 이나 −1 로 만든다. `xTaskNotifyWait` 는 "이벤트 대용" 이라 값이 0 이어도 "알림이 왔다" 가 의미 있다(`eSetValueWithOverwrite(0)`): 그래서 상태 RECEIVED 를 본다.

</details>

<details>
<summary>힌트 3 — entry 마스크는 블록하는 분기 안에만 있다</summary>

FreeRTOS 소스에서 `ulNotifiedValue &= ~ulBitsToClearOnEntry` 는 `if (state != RECEIVED && xTicksToWait > 0)` 블록 **안** 에 있다 (7913, 7930). 즉 대기 전에 와 있던 비트는 지워지지 않는다 — 그래야 "Wait 를 부르기 직전에 온 이벤트" 를 잃지 않는다.

</details>

<details>
<summary>힌트 4 — hptw 는 OR 로 모은다</summary>

ISR 하나가 FromISR API 를 여러 번 부르고 마지막에 `portYIELD_FROM_ISR(hptw)` 를 한 번 부른다. 그러니 각 API 는 "필요하면 1" 만 쓴다. `usart_irq` 도 지역 `hptw = 0` 하나를 두 알림에 넘기고 그 값을 반환한다.

</details>

<details>
<summary>힌트 5 — Part C: 인덱스 0 알림은 인덱스 1 대기자를 깨우지 않는다</summary>

상태가 인덱스마다 따로 있다. comm 이 인덱스 1 을 기다리는 중(`state[1] == WAITING`)에 인덱스 0 에 비트가 오면 `state[0]` 만 RECEIVED 가 되고 깨우지 않는다. 그 비트는 다음 `xTaskNotifyWaitIndexed(0, ...)` 가 블록 없이 가져간다.

</details>

## 5. 풀이

### Part A — 상태 하나가 모든 규칙을 만든다

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ucNotifyState[idx] 상태 전이 (tasks.c V11.3.2)</text>
  <rect x="40" y="110" width="160" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="58" y="133" fill="currentColor" font-weight="bold">NOT_WAITING (0)</text>
  <text x="58" y="151" fill="currentColor">초기값 / 받아 감</text>
  <rect x="300" y="40" width="160" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="330" y="63" fill="currentColor" font-weight="bold">WAITING (1)</text>
  <text x="318" y="81" fill="currentColor">이 인덱스로 Blocked</text>
  <rect x="540" y="110" width="170" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="560" y="133" fill="currentColor" font-weight="bold">RECEIVED (2)</text>
  <text x="560" y="151" fill="currentColor">읽지 않은 알림 있음</text>
  <path d="M180 110 L310 90" stroke="currentColor" fill="none"/>
  <text x="20" y="42" fill="currentColor">begin 에서 블록할 때:</text>
  <text x="20" y="58" fill="currentColor" font-family="monospace">Take: value==0 &amp;&amp; ticks&gt;0</text>
  <text x="20" y="74" fill="currentColor" font-family="monospace">Wait: ticks&gt;0 (+entry 마스크)</text>
  <path d="M460 70 L600 110" stroke="currentColor" fill="none"/>
  <text x="540" y="78" fill="currentColor">notify → 깨움</text>
  <path d="M200 135 L540 135" stroke="currentColor" fill="none"/>
  <text x="300" y="130" fill="currentColor">notify (깨울 대상 없음)</text>
  <path d="M540 150 L200 150" stroke="currentColor" fill="none"/>
  <text x="250" y="168" fill="currentColor">Take_end / Wait_end (받음, exit 처리)</text>
  <path d="M360 90 L205 124" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="312" y="116" fill="currentColor">타임아웃 → end</text>
  <rect x="10" y="190" width="720" height="92" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="210" fill="currentColor">RECEIVED 에서 다시 notify: 상태 그대로, 값만 갱신. NoOverwrite 만 pdFAIL (값 보존).</text>
  <text x="22" y="230" fill="currentColor">RECEIVED 에서 Wait_begin: 블록 안 함, entry 마스크도 안 함 → 대기 전 이벤트를 지킨다.</text>
  <text x="22" y="250" fill="currentColor">Take_end 는 상태와 무관하게 값을 본다: eNoAction 으로 깨우면 0 을 돌려준다.</text>
  <text x="22" y="270" fill="currentColor">상태·값이 인덱스마다 따로 → 인덱스 0 알림은 인덱스 1 대기자를 깨우지 않는다.</text>
</svg>
```

다섯 동작을 한 표로 (값 `v`, 원래 값 `x`):

| eAction | 새 값 | 반환 | 대표 용도 | 바꿔 쓰는 객체 |
|---|---|---|---|---|
| `eIncrement` (`xTaskNotifyGive`) | `x + 1` (상한 없음, wrap) | 항상 pdPASS | ISR → 처리 태스크 | 바이너리/카운팅 세마포어 |
| `eSetBits` | `x \| v` | 항상 pdPASS | 여러 이벤트 원인 | 이벤트 그룹 (수신자 1명) |
| `eSetValueWithOverwrite` | `v` | 항상 pdPASS | 최신 센서 값 | 길이 1 큐 + `xQueueOverwrite` |
| `eSetValueWithoutOverwrite` | 안 읽었으면 그대로, 아니면 `v` | 안 읽었으면 **pdFAIL** | 값 유실을 알아야 할 때 | 길이 1 큐 + `xQueueSend(…, 0)` |
| `eNoAction` | `x` | 항상 pdPASS | 그냥 깨우기 | — |

받는 쪽 두 API:

| | `ulTaskNotifyTake(xClear, ticks)` | `xTaskNotifyWait(clrEntry, clrExit, &v, ticks)` |
|---|---|---|
| 블록 조건 | **값** == 0 && ticks &gt; 0 | **상태** != RECEIVED && ticks &gt; 0 |
| 들어갈 때 | — | 블록할 때만 `value &= ~clrEntry` |
| 나올 때 | 값 반환. 0 아니면 `xClear ? 0 : 값−1` | `*v = 값`, 받았으면 `value &= ~clrExit` |
| 반환 | 나오기 전 값 (0 = 타임아웃 또는 가짜 깨움) | pdTRUE(받음) / pdFALSE(타임아웃) |
| 상태 | 항상 NOT_WAITING 으로 | 항상 NOT_WAITING 으로 |

`ulTaskNotifyTake(pdTRUE)` 가 바이너리 세마포어보다 **나은** 점이 하나 있다: 처리 중에 ISR 이 5번 오면 세마포어는 1개로 합쳐지지만(RTOS-B08), Take 는 5 를 돌려준다. 태스크는 "몇 개 왔는지" 알고 한 번에 처리한다.

### 진짜 커널과 비교 — `demo_freertos_notify.c`

모델 테스트와 같은 순서·같은 값으로 진짜 FreeRTOS V11.3.2(POSIX 포트)를 돌렸다. POSIX 포트는 FreeRTOS 스레드를 한 번에 하나만 돌리므로 이 순서는 매번 같다 (5회 실행 모두 39/39). 실제 출력 (발췌, 줄은 그대로):

```text
-- A1 eIncrement + ulTaskNotifyTake (decrement vs clear)
[ OK ] Take(pdFALSE) after 5 gives returns                        got=5          model=5
[ OK ] Take(pdFALSE) again returns                                got=4          model=4
[ OK ] Take(pdTRUE) returns                                       got=3          model=3
-- A2 eSetValueWithoutOverwrite / eSetValueWithOverwrite
[ OK ] NoOverwrite(22) while unread -> pdFAIL                     got=0          model=0
[ OK ] NoOverwrite(33) after read -> pdPASS                       got=1          model=1
-- A3 eSetBits + xTaskNotifyWait masks
[ OK ]   *pulNotificationValue = value BEFORE exit clear          got=5          model=5
[ OK ] Wait(~0,~0,ticks=0), nothing pending -> pdFALSE            got=0          model=0
[ OK ]   entry clear NOT applied when it does not block           got=4          model=4
[ OK ]   pending bits kept (no entry clear)                       got=6          model=6
[ OK ]   entry clear applied because it blocked                   got=0          model=0
-- B  mixing Take with bits, eIncrement wrap
[ OK ] Take(pdTRUE) on bit value 0x8 returns                      got=8          model=8
[ OK ] 0xFFFFFFFF + eIncrement wraps to                           got=0          model=0
-- B  eNoAction wakes a Take waiter, Take returns 0
[ OK ]   ulTaskNotifyTake returned                                got=0          model=0
-- B  FromISR: *pxHigherPriorityTaskWoken only set when woken prio > running prio
[ OK ] give to blocked W(prio 3) from ctrl(prio 2): hptw          got=1          model=1
[ OK ] give to blocked L(prio 1): hptw stays                      got=0          model=0
-- B  single receiver: notification vs event group broadcast
[ OK ] notify R1: R1 woke                                         got=1          model=1
[ OK ]   R2 woke                                                  got=0          model=0
[ OK ]   R2 still eBlocked                                        got=2          model=2
[ OK ] one xEventGroupSetBits: E1 woke                            got=1          model=1
[ OK ]   E2 woke                                                  got=1          model=1
-- C  two indexes: index-0 bits do not wake a task blocked on index 1
[ OK ] X after index-0 notify: stage (1 = still in Take idx 1)    got=1          model=1
[ OK ]   Take(idx 1) returned                                     got=1          model=1
[ OK ]   index-0 bits                                             got=1          model=1
39/39 agree with the model
```

(`eBlocked` = 2. 호스트에서 `vTaskNotifyGiveFromISR` 를 태스크에서 불렀다 — POSIX 포트의 ISR 마스크는 아무것도 안 하므로 가능하다. 실제 MCU 에서 FromISR 은 ISR 안에서만 부른다.)

<!-- code: demo_freertos_notify.c -->

### Part B — 무엇으로, 무엇을 바꿀 수 있나

**B1. 세마포어 대용.** 바이너리 세마포어는 이벤트 5개 중 1개만 남긴다(테스트 `ok == 1 && taken == 1`). 알림 clear 모드는 5 를 한 번에, decrement 모드는 카운팅 세마포어처럼 하나씩 5번 준다. 단 카운팅 세마포어의 `uxMaxCount` 같은 **상한이 없다** — `0xFFFFFFFF` 다음은 0 (모델·커널 모두 확인). 실제로 2^32 개가 밀릴 일은 없지만 "포화" 를 기대하는 설계는 안 된다.

**B2. 메일박스.** `eSetValueWithOverwrite` = 길이 1 큐 + `xQueueOverwrite`: ADC 최신 값만 의미 있을 때. `eSetValueWithoutOverwrite` = 안 읽은 값을 지키고 pdFAIL 로 유실을 알린다. 큐와 다른 점: **보내는 쪽이 블록해서 기다릴 수 없다** (큐는 `xQueueSend(q, &x, timeout)` 으로 자리 날 때까지 기다린다). 값도 32비트 하나 — 구조체는 포인터로(수명 관리 필요) 또는 큐로.

**B3. 이벤트 그룹 대용.** `eSetBits` + `xTaskNotifyWait(0, ~0, &bits, …)` 는 wait-**any** 와 같다. wait-**all** 은 없다 — 태스크가 깨어날 때마다 비트를 모아야 한다(`fusion_on_wake`). 테스트 결과: 센서 3개의 DRDY 가 따로 오면 알림 쪽은 **3번 깨어나고**(`fu.wakes == 3`), 이벤트 그룹 모델은 **1번**(`eg_t.wakes == 1`). 깨어남 = 컨텍스트 스위치이므로 "호출당 비용이 싸다" 가 "전체가 싸다" 는 아니다. 또 이벤트 그룹은 비트를 여러 태스크가 같이 보고(브로드캐스트), `xEventGroupSync` 로 랑데부도 된다 — 알림으로는 안 된다.

대신 ISR 에서는 알림이 훨씬 직접적이다. `xEventGroupSetBitsFromISR` 는 비트를 바로 세우지 않고 **타이머 데몬 태스크에 작업을 보낸다** (event_groups.c:826 → `xTimerPendFunctionCallFromISR`). 이유는 헤더 주석 그대로 "기다리는 태스크 수를 모르는 비결정적 작업을 ISR 에서 하지 않기 위해" (event_groups.h:543–550). 그래서 `configUSE_TIMERS`·타이머 큐가 필요하고, 실제 비트 설정은 타이머 태스크 우선순위에 따라 늦어지며, 타이머 큐가 차면 pdFAIL 이다. 32비트 tick 에서 이벤트 그룹의 사용 가능 비트는 24개(상위 8비트는 커널 제어용, event_groups.h:47–51), 알림은 32개.

**B4. 수신자 1명.** 알림은 TCB 하나에 있다. R1·R2 둘이 `ulTaskNotifyTake` 로 기다릴 때 R1 에게 보내면 R2 는 계속 Blocked (커널에서도 `eTaskGetState(R2) == eBlocked`). "같은 이벤트를 두 태스크가 기다린다" 를 알림으로 하려면 보내는 쪽이 **핸들을 모두 알고 각각 보내야** 한다(`notify_fanout_from_isr` — ISR 에서 n 번 호출, n 이 커질수록 ISR 이 길어짐). 이벤트 그룹은 `xEventGroupSetBits` 한 번에 E1·E2 둘 다 깨운다(커널 확인). 그리고 **ISR 은 알림을 받을 수 없다** (TCB 가 없다).

FreeRTOS 문서(Mastering the FreeRTOS Kernel, 태스크 알림 장)가 꼽는 한계도 같다: ① ISR 로 보낼 수 없음 ② 수신 태스크 하나 ③ 여러 데이터를 버퍼링 못 함 ④ 브로드캐스트 불가 ⑤ 보내는 쪽이 블록해서 기다릴 수 없음.

### RAM — 실측

Cortex-M4F(ARM_CM4F 포트, 32비트 `TickType_t`, trace/queue set 끔). `cm4/measure_cm4.sh` 실제 출력:

```text
== object sizes (bytes), Cortex-M4F, 32-bit TickType_t
-DconfigUSE_TASK_NOTIFICATIONS=0                        StaticEventGroup_t=28 StaticSemaphore_t=72 StaticTask_t=80 StaticTimer_t=40
-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=1               StaticEventGroup_t=28 StaticSemaphore_t=72 StaticTask_t=84 StaticTimer_t=40
-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=2               StaticEventGroup_t=28 StaticSemaphore_t=72 StaticTask_t=88 StaticTimer_t=40
-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=3               StaticEventGroup_t=28 StaticSemaphore_t=72 StaticTask_t=92 StaticTimer_t=40
-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=4               StaticEventGroup_t=28 StaticSemaphore_t=72 StaticTask_t=100 StaticTimer_t=40
-DconfigUSE_TRACE_FACILITY=1 -DconfigUSE_QUEUE_SETS=1   StaticEventGroup_t=32 StaticSemaphore_t=84 StaticTask_t=92 StaticTimer_t=44
```

`Static*_t` 는 커널 내부 구조체의 공개 "거울" 이다. 크기가 같다는 건 커널이 직접 assert 한다: `Queue_t` (queue.c:406), `EventGroup_t` (event_groups.c:101), `TCB_t` (tasks.c:1285). 크기 읽는 법: `char sz_X[sizeof(X)];` 를 전역으로 두고 컴파일만 한 뒤 `xcrun llvm-nm -S` 로 심볼 크기를 본다 — 링크도 실행도 필요 없다.

<!-- code: demo_sizes_cm4.c -->

동적 생성(heap_4)은 블록 헤더 `BlockLink_t`(Cortex-M 에서 8 B)를 붙이고 8 바이트로 올림한다 (heap_4.c:158, 188–196). 호스트에서 이 규칙을 실측으로 확인했다: 64비트라 헤더 16 B, 세마포어 168 + 16 = **184**, 이벤트 그룹 64 + 16 = **80** — `demo_freertos_bench.c` 가 `xPortGetFreeHeapSize()` 차이로 잰 값과 같다. 같은 규칙을 Cortex-M4F 에 적용하면 (계산값):

| 무엇 | 정적 (`...CreateStatic`) | heap_4 동적 | 알림으로 바꾸면 |
|---|---|---|---|
| 바이너리/카운팅 세마포어 | 72 B | 80 B | 0 B |
| 이벤트 그룹 | 28 B | 40 B | 0 B (+ ISR 에서 쓰면 타이머 태스크·큐 불필요해질 수 있음) |
| 알림 필드 (인덱스 1개) | 모든 TCB 에 이미 있음: 80 → 84 B | | 추가 0 B |
| 인덱스 1 → 2 | TCB **마다** +4 B (84 → 88) | | 태스크 12개면 48 B |
| 인덱스 1 → 4 | TCB 마다 +16 B (84 → 100) | | 태스크 20개면 320 B |

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Cortex-M4F RAM (바이트, 막대 2.5 px = 1 B) — 실측 크기 + heap_4 규칙</text>
  <g fill="currentColor">
    <text x="10" y="56">바이너리 세마포어 (heap_4)</text>
    <text x="10" y="86">바이너리 세마포어 (정적)</text>
    <text x="10" y="116">이벤트 그룹 (heap_4)</text>
    <text x="10" y="146">이벤트 그룹 (정적)</text>
    <text x="10" y="176">알림 (인덱스 그대로)</text>
    <text x="10" y="206">인덱스 1→2, TCB 12개</text>
    <text x="10" y="236">인덱스 1→4, TCB 20개</text>
  </g>
  <rect x="200" y="42" width="200" height="20" class="f-warn" stroke="currentColor"/>
  <text x="408" y="57" fill="currentColor">80</text>
  <rect x="200" y="72" width="180" height="20" class="f-warn" stroke="currentColor"/>
  <text x="388" y="87" fill="currentColor">72</text>
  <rect x="200" y="102" width="100" height="20" class="f-warn" stroke="currentColor"/>
  <text x="308" y="117" fill="currentColor">40</text>
  <rect x="200" y="132" width="70" height="20" class="f-warn" stroke="currentColor"/>
  <text x="278" y="147" fill="currentColor">28</text>
  <rect x="200" y="162" width="2" height="20" class="f-ok" stroke="currentColor"/>
  <text x="210" y="177" fill="currentColor">0 (필드는 이미 모든 TCB 에 있음)</text>
  <rect x="200" y="192" width="120" height="20" class="f-acc" stroke="currentColor"/>
  <text x="328" y="207" fill="currentColor">48 = 12 × 4</text>
  <rect x="200" y="222" width="500" height="20" class="f-acc" stroke="currentColor"/>
  <text x="560" y="237" fill="currentColor">320 = 20 × 16</text>
  <line x1="200" y1="36" x2="200" y2="248" stroke="currentColor"/>
  <text x="10" y="262" fill="currentColor">교훈: 객체 몇 개를 없애려고 인덱스를 늘리면 모든 태스크(idle·timer 포함)가 값을 치른다 (연습 2).</text>
</svg>
```

TCB 증가가 `5 × N` 이 아니라 4–8 B 계단인 이유는 패딩이다: 알림 배열 뒤에 `ucStaticallyAllocated`(1 B)가 있고 구조체가 4 B 정렬이라, N=0 일 때 남던 3 B 패딩에 `ucNotifyState` 가 들어간다 (tasks.c:437–445 의 필드 순서).

### 호출당 시간 — 호스트에서 무엇을 쟀나

`demo_freertos_bench.c` 실제 출력 (Apple M6, perflevel0 2 + perflevel1 4 코어, Apple clang 21.0.0, `-O2`):

```text
== time per call pair, host-relative only (N=200000, 7 reps)
  ref: pthread_sigmask BLOCK+UNBLOCK (= 1 critical section) min   247.1  median   249.9 ns
  binary sem: Give + Take(0)                         min   504.5  median   505.8 ns
  notify:     NotifyGive + NotifyTake(pdTRUE,0)      min   501.1  median   502.7 ns
  event grp:  SetBits + WaitBits(clear,0)            min   505.2  median   506.8 ns
  notify:     Notify(eSetBits) + NotifyWait(0)       min   501.9  median   506.7 ns
  FromISR give only: xSemaphoreGiveFromISR (counting) min     1.7  median     1.8 ns
  FromISR give only: vTaskNotifyGiveFromISR          min     1.1  median     1.1 ns
== ping-pong: give from ctrl(prio 2) -> wake hi(prio 3) -> hi blocks again (N=20000)
  binary sem round trip                              min  4997.6  median  5487.7 ns
  notify round trip                                  min  4383.7  median  5426.9 ns
  wakeups counted: sem 141000, notify 141000 (expected 141000 each)
```

읽는 법:

1. **태스크 수준 쌍 4개가 모두 ≈ 500 ns** 인 건 커널 코드가 같아서가 아니다. POSIX 포트의 `taskENTER_CRITICAL` 은 `pthread_sigmask` 시스템 콜 두 번(port.c:406–421)이고, 한 쌍 ≈ 250 ns 를 따로 쟀다. 네 쌍 모두 API 두 개 × 임계 구역 1번(소스상 블록 없는 경로에서 각 API 가 한 번 — 이벤트 그룹은 `xTaskResumeAll` 안에서) = 시스템 콜 4번 ≈ 500 ns 로, 커널 자체 코드(수십 ns 미만)는 이 잡음에 묻힌다.
2. **FromISR give 는 시스템 콜이 없다** (POSIX 포트의 ISR 마스크는 0 을 돌려줄 뿐, port.c:424–436). 그래서 커널 코드 경로가 보인다: 대기자가 없을 때 세마포어 1.8 ns vs 알림 1.1 ns. 세 번 더 돌린 run 에서도 1.7–1.8 vs 1.0–1.1 로 같았다.
3. **블록·스위치가 끼면** 스위치 비용(POSIX 포트는 pthread 깨우기)이 지배한다. 일곱 번 run 에서 min 은 세마포어 4965–5410 ns, 알림 4020–4453 ns 로 알림이 매번 짧았지만 median 은 run 마다 흔들렸다(이번 run 5488 vs 5427). 그러니 이 호스트 숫자로 "몇 % 빠르다" 를 주장하지 않는다.
4. MCU 에서의 숫자: FreeRTOS 문서는 "바이너리 세마포어 같은 중간 객체로 깨우는 것보다 알림이 45% 빠르고 RAM 도 덜 쓴다" 고 한다 (문서 기준, 미검증 — 정확한 값은 DWT->CYCCNT 로 보드에서 재야 한다).

구조적인 이유는 소스에 있다. 세마포어로 기다리는 태스크는 세마포어의 `xTasksWaitingToReceive`(우선순위 정렬 삽입)와 지연 리스트 **두 곳** 에 걸리고, give 는 `xTaskRemoveFromEventList` 로 둘 다에서 뺀다. 알림 대기자는 지연 리스트에만 있고(`configASSERT(listLIST_ITEM_CONTAINER(&xEventListItem) == NULL)`, tasks.c:8094), notify 는 `listREMOVE_ITEM` 한 번 + `prvAddTaskToReadyList` 다. 큐 잠금(`cTxLock`) 검사도 없다.

코드 크기(`-Os`, Cortex-M4F, 같은 스크립트)는 **속도가 아니라 플래시** 지표다:

| 함수 | 바이트 | | 함수 | 바이트 |
|---|---|---|---|---|
| `xTaskGenericNotify` | 252 | | `xQueueGenericSend` (give) | 354 |
| `ulTaskGenericNotifyTake` | 146 | | `xQueueSemaphoreTake` | 366 |
| `xTaskGenericNotifyWait` | 192 | | `xEventGroupWaitBits` | 228 |
| `vTaskGenericNotifyGiveFromISR` | 258 | | `xQueueGiveFromISR` | 136 |
| `xTaskGenericNotifyFromISR` | 308 | | `xEventGroupSetBitsFromISR` | 18 (+ 타이머 태스크로 미룸) |

`xQueueGiveFromISR` 가 알림 FromISR 보다 작다 — 알림 FromISR 은 스케줄러가 멈췄을 때 `xPendingReadyList` 처리와 우선순위 비교를 함수 안에 다 가지고 있고, 큐는 그 일을 `xTaskRemoveFromEventList`(230 B, 별도 함수)에 맡긴다. 함수 크기만 보고 빠르기를 판단하면 안 된다. 그리고 큐/세마포어를 이미 쓰는 프로젝트(대부분)에서는 queue.c 가 어차피 링크되므로, 알림으로 바꿔도 플래시는 거의 안 준다.

### Part C — 인덱스를 나눈 UART 드라이버

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part C 테스트 순서: 응답 송신 중에 새 프레임이 와도 잃지 않는다</text>
  <g fill="currentColor">
    <text x="10" y="60">USART IRQ</text>
    <text x="10" y="110">comm 태스크</text>
    <text x="10" y="160">value[0] 앱</text>
    <text x="10" y="200">value[1] TX</text>
  </g>
  <rect x="130" y="44" width="70" height="24" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="140" y="61" fill="currentColor">IDLE#1</text>
  <rect x="360" y="44" width="70" height="24" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="370" y="61" fill="currentColor">IDLE#2</text>
  <rect x="500" y="44" width="50" height="24" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="512" y="61" fill="currentColor">TC</text>
  <rect x="110" y="94" width="90" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="116" y="112" fill="currentColor">Wait idx0</text>
  <rect x="200" y="94" width="90" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="206" y="112" fill="currentColor">파싱·TX시작</text>
  <rect x="290" y="94" width="260" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="300" y="112" fill="currentColor">Take idx1 (TC 대기) — IDLE#2 에 안 깸</text>
  <rect x="550" y="94" width="180" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="556" y="112" fill="currentColor">Wait idx0 → 블록 없이 #2</text>
  <text x="140" y="164" fill="currentColor" font-family="monospace">0→RX→0</text>
  <text x="370" y="164" fill="currentColor" font-family="monospace">RX (RECEIVED, 보존)</text>
  <text x="600" y="164" fill="currentColor" font-family="monospace">→ 0</text>
  <text x="300" y="204" fill="currentColor" font-family="monospace">0 (WAITING)</text>
  <text x="505" y="204" fill="currentColor" font-family="monospace">1 → Take 가 1 반환 → 0</text>
  <rect x="10" y="218" width="720" height="26" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="22" y="236" fill="currentColor">인덱스 1개 버그: TX 대기 중 IDLE#2 → Take 가 RX 비트(=1)를 TX 완료로 먹음, RX 이벤트 증발</text>
</svg>
```

정답 `usart_irq` 의 핵심:

```c
uint32_t sr = u->ISR, ev = 0, handled = sr & (USART_ISR_IDLE | USART_ISR_ORE | USART_ISR_TC);
int hptw = 0;
if (sr & USART_ISR_IDLE) ev |= EV_RX_FRAME;
if (sr & USART_ISR_ORE)  ev |= EV_RX_OVERRUN;
if (ev) (void)nt_notify_from_isr(comm, IDX_APP, ev, eSetBits, NULL, running_prio, &hptw);
if (sr & USART_ISR_TC)   nt_give_from_isr(comm, IDX_TX, running_prio, &hptw);
if (handled) usart_write_icr(u, handled);
return hptw;                                   /* 실제: portYIELD_FROM_ISR(hptw); */
```

- 상태 레지스터를 **한 번** 읽고(`sr`), 이 ISR 이 처리한 플래그만 ICR 에 쓴다. 테스트의 `bit7(TXE)` 는 이 ISR 몫이 아니라 남아야 한다 (`u.ISR == 1u << 7`). W1C 의 장점: RMW 가 없어서 다른 비트를 실수로 지우지 않는다 (C-M02 참고).
- 원인이 여러 개면 비트를 모아 **알림 한 번** — ISR 이 짧아지고, 태스크는 한 번 깨어 두 원인을 다 본다 (`v == EV_RX_FRAME | EV_RX_OVERRUN`).
- comm(prio 3)이 실행 중인 태스크(prio 3)를 깨우면 "더 높지 않음" → hptw 0. 스위치는 다음 기회에 일어난다 (같은 우선순위는 선점하지 않는다).
- 왜 인덱스를 나누나: 드라이버의 `ulTaskNotifyTake` 가 앱의 이벤트 비트를 "카운트" 로 먹어 버리기 때문이다 (버그 테스트). FreeRTOS 의 스트림/메시지 버퍼도 같은 이유로 알림을 쓰며, 기본 인덱스는 0 이다 (stream_buffer.c:1654, V11.3.2 에는 `vStreamBufferSetStreamBufferNotificationIndex` 로 바꿀 수 있음, 1690). 앱이 인덱스 0 을 비트 이벤트로 쓰는 태스크에서 스트림 버퍼로도 블록하면 서로 간섭한다 — 라이브러리/드라이버용 인덱스를 따로 정해 두는 것이 알림 배열(문서 기준 V10.4.0 도입)의 목적이다.

### API 매핑

| 모델 (이 레슨) | FreeRTOS | CMSIS-RTOS2 (문서 기준) | Zephyr (문서 기준) |
|---|---|---|---|
| `nt_give_from_isr` | `vTaskNotifyGiveFromISR(h, &w); portYIELD_FROM_ISR(w);` | `osThreadFlagsSet(id, F)` | `k_sem_give(&s)` (ISR 가능) |
| `nt_take_begin/end(…, clear=1)` | `ulTaskNotifyTake(pdTRUE, t)` | `osThreadFlagsWait(F, osFlagsWaitAny, t)` | `k_sem_take(&s, K_MSEC(t))` |
| `nt_take_*(…, clear=0)` | `ulTaskNotifyTake(pdFALSE, t)` | — (플래그라 개수 없음) | `k_sem` (limit &gt; 1) |
| `nt_notify(…, eSetBits)` | `xTaskNotify(h, b, eSetBits)` / `...FromISR` | `osThreadFlagsSet` | `k_event_post(&e, b)` (여러 스레드가 대기 가능) |
| `nt_wait_begin/end` | `xTaskNotifyWait(in, out, &v, t)` | `osThreadFlagsWait` (wait-all 지원) | `k_event_wait` / `k_event_wait_all` |
| `adc_post(…, 0)` | `xTaskNotify(h, v, eSetValueWithOverwrite)` | — | `k_msgq` 길이 1 + purge |
| `IDX_TX` 인덱스 | `ulTaskNotifyTakeIndexed(1, …)` | — | — |
| `usart_write_icr` | `USART1->ICR = handled;` | | |

CMSIS-RTOS2 의 thread flags 가 개념상 가장 가깝다(스레드마다 플래그, ISR 에서 set, 대기는 그 스레드만). FreeRTOS 위의 CMSIS-RTOS2 래퍼는 thread flags 를 태스크 알림으로 구현한다고 알려져 있다 (미검증 — 이 레슨에서 래퍼 소스를 읽지 않았다). Zephyr 에는 "TCB 에 직접 쓰는" 같은 경량 객체가 따로 없고 `k_sem`/`k_event` 가 커널 객체다.

### 언제 무엇을

| 상황 | 쓸 것 | 이유 |
|---|---|---|
| ISR → **정해진 한** 태스크, 이벤트 수 중요 | 알림 `eIncrement` + `ulTaskNotifyTake` | 객체 0 B, 짧은 경로, 개수 보존 |
| 원인 여러 개 → 한 태스크 | 알림 `eSetBits` + `xTaskNotifyWait` | 비트 OR 한 번, 32비트 |
| 최신 값 하나 | 알림 Overwrite | 길이 1 큐보다 가벼움 |
| 여러 태스크가 같은 이벤트 / wait-all / 랑데부 | 이벤트 그룹 | 브로드캐스트, `xWaitForAllBits`, `xEventGroupSync` |
| 데이터를 여러 개 쌓아야 | 큐 / 스트림 버퍼 | 알림은 값 1개 |
| 보내는 쪽이 자리 날 때까지 기다려야 | 큐 | 알림 send 는 블록 불가 |
| 공유 자원 보호 | **뮤텍스** | 소유권·우선순위 상속 (RTOS-B10, RTOS-M04) — 알림은 상호 배제 도구가 아니다 |
| 받는 쪽이 ISR | 없음 (ISR 은 블록 불가) | 플래그/링 버퍼 (C-M14) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/RTOS-M01_so && /tmp/RTOS-M01_so      # 84/84 passed (6회 반복 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/RTOS-M01_san && /tmp/RTOS-M01_san   # 84/84, 보고 없음
```

체크 대상 코드는 스레드를 쓰지 않는다(단일 스레드 결정적 모델) — TSan 대상이 없다.

<!-- code: demo_freertos_bench.c -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 이벤트를 두 태스크가 받게 하려고 둘 다 `ulTaskNotifyTake` | 알림은 받는 TCB 를 지정해 보낸다. 다른 태스크는 영원히 안 깬다 (B4) | 이벤트 그룹, 또는 보내는 쪽이 각각 notify |
| `ulTaskNotifyTake` 반환값을 안 보고 "깨어났으니 이벤트" | 타임아웃·`eNoAction`·`xTaskAbortDelay` 로도 깨어 0 을 돌려준다 (A5) | `if (n == 0) { 타임아웃 처리 } else for (i<n) 처리` |
| 같은 인덱스에 Take(카운트) 와 Wait(비트)를 섞음 | Take 는 비트 값을 개수로 보고 지운다 (`0x8` → 8) | 용도별로 인덱스를 나눈다 (Part C) |
| 드라이버가 앱과 같은 인덱스 0 으로 TX 완료 대기 | RX 비트가 TX 대기를 깨움 + 그 비트가 지워짐 | `ulTaskNotifyTakeIndexed(IDX_TX, …)` |
| 타임아웃 뒤 다음 전송을 그냥 시작 | 늦게 온 이전 TC 알림이 다음 Take 를 즉시 통과시킴 | 하드웨어 중단 후 `xTaskNotifyStateClearIndexed` + `ulTaskNotifyValueClearIndexed` (연습 1) |
| `xTaskNotifyWait(ULONG_MAX, …)` 의 entry 마스크로 "이전 이벤트를 다 지운다" 고 믿음 | 이미 RECEIVED 면 entry 마스크는 적용되지 않는다 (A3, 커널 확인) | 진짜로 버리려면 `ulTaskNotifyValueClear` 로 명시적으로 |
| `eSetValueWithoutOverwrite` 가 실패하면 "나중에 다시 보내지겠지" | 알림 send 는 블록·재시도가 없다. 값은 버려진다 | 반환값 pdFAIL 로 드롭 카운트 (B2), 데이터면 큐 |
| FromISR 에서 `hptw = pdFALSE` 를 매 호출 전에 다시 대입 | 앞선 호출이 요청한 스위치를 지워 버린다 | ISR 첫 줄에서 한 번만 0, 끝에서 `portYIELD_FROM_ISR(hptw)` |
| 세마포어 몇 개 없애려 `configTASK_NOTIFICATION_ARRAY_ENTRIES` 를 크게 | 모든 TCB(idle·timer 포함)가 커진다 (연습 2: −240 B 손해 사례) | 인덱스는 용도가 확실할 때만 하나씩 |
| 알림으로 공유 자원 상호 배제 | 소유권·우선순위 상속 없음 → 우선순위 역전 (RTOS-M03) | 뮤텍스 |

## 8. 면접 Q&A

<details>
<summary>Q1. 태스크 알림이 바이너리 세마포어보다 빠르고 RAM 을 덜 쓰는 이유는?</summary>

세마포어는 별도 커널 객체(`Queue_t`, Cortex-M4F 실측 72 B, heap_4 로 80 B)를 거친다: give 는 그 객체의 카운트를 올리고, 기다리는 태스크를 객체의 이벤트 리스트와 지연 리스트에서 빼서 ready 로 옮긴다. 알림은 받는 태스크의 TCB 안 `ulNotifiedValue[]`/`ucNotifyState[]` 를 직접 고치고 지연 리스트에서만 빼면 된다. 객체가 없으니 RAM 0 B 추가(필드는 이미 모든 TCB 에 있다), 경로는 짧다. 호스트 실측에서 시스템 콜이 없는 FromISR give 는 1.1 ns vs 1.8 ns. 꼬리: "몇 % 빠른가?" → 문서는 45% 라 하지만 (미검증) 보드에서 DWT 사이클 카운터로 재야 한다. 호스트 POSIX 포트에서는 임계 구역이 시스템 콜이라 태스크 수준 API 차이는 묻힌다.

</details>

<details>
<summary>Q2. 그럼 왜 항상 알림을 쓰지 않나요? 수신자가 둘 이상이면 왜 안 되죠?</summary>

알림은 "누구에게" 를 보내는 쪽이 정한다 — 객체가 없어서 "이 이벤트를 기다리는 태스크 목록" 이 존재하지 않는다. 둘이 같은 이벤트를 기다리려면 보내는 쪽이 둘의 핸들을 다 알고 두 번 보내야 하고(ISR 이 길어지고 결합도가 올라감), 기다리는 쪽 하나가 늘 때마다 보내는 코드를 고쳐야 한다. 그 밖의 한계: ISR 은 받을 수 없음(TCB 없음), 값 하나만(버퍼링 없음), 보내는 쪽이 블록해서 기다릴 수 없음, wait-all 없음. 이벤트 그룹은 `xEventGroupSetBits` 한 번에 조건이 맞는 대기자를 모두 깨운다(커널 데모: E1, E2 둘 다).

</details>

<details>
<summary>Q3. ulTaskNotifyTake(pdTRUE) 와 ulTaskNotifyTake(pdFALSE) 는 각각 무엇을 흉내 내나요?</summary>

`pdTRUE` = clear-on-exit: 쌓인 개수를 한 번에 돌려주고 0 으로. 바이너리 세마포어 자리에 쓰지만 **더 낫다** — 처리 중 ISR 이 5번 오면 바이너리 세마포어는 1개만 남기는데 Take 는 5 를 알려 줘서 일괄 처리할 수 있다. `pdFALSE` = decrement: 하나씩 −1, 카운팅 세마포어처럼 이벤트마다 한 번 처리. 차이 하나: 카운팅 세마포어의 최대 카운트(포화)가 없고 `0xFFFFFFFF + 1 = 0` 으로 wrap 한다 (모델·커널 모두 확인). 꼬리: "반환값 0 은?" → 타임아웃, 또는 `eNoAction`/`eSetValueWithOverwrite(0)` 같은 가짜 깨움. Take 는 상태가 아니라 값을 보기 때문이다 (tasks.c 7867).

</details>

<details>
<summary>Q4. xTaskNotifyWait 의 ulBitsToClearOnEntry 는 언제 적용되나요?</summary>

"블록할 때만" 이다. 소스상 entry 지우기는 `state != RECEIVED && xTicksToWait > 0` 분기 안에 있다 (tasks.c 7913, 7930). 그래서 ① 이미 알림이 와 있으면 entry 마스크를 무시하고 바로 그 값을 돌려준다 — Wait 를 부르기 직전에 ISR 이 세운 비트를 잃지 않게 하려는 설계다. ② ticks == 0 으로 폴링하면 아무것도 지우지 않는다 (커널 데모: 값 0x4 유지). exit 마스크는 "알림을 받았을 때만", 그리고 `*pulNotificationValue` 는 exit 지우기 **전** 값이다. 꼬리: "이전 이벤트를 확실히 버리려면?" → `ulTaskNotifyValueClear(NULL, bits)` 를 명시적으로.

</details>

<details>
<summary>Q5. ISR 에서 이벤트 그룹 비트를 세우는 것과 알림 비트를 세우는 것의 차이는?</summary>

`xEventGroupSetBitsFromISR` 는 비트를 바로 세우지 않고 `xTimerPendFunctionCallFromISR` 로 타이머 데몬 태스크에 "비트 세우기" 를 보낸다 (event_groups.c:826). 대기자가 몇 명인지 모르는 리스트 순회를 ISR·임계 구역에서 하지 않기 위해서다. 결과: 타이머 기능(`configUSE_TIMERS`, `INCLUDE_xTimerPendFunctionCall`)과 타이머 큐가 필요하고, 실제 깨움은 타이머 태스크가 돌 때까지 늦어지며(타이머 태스크 우선순위가 낮으면 더), 큐가 차면 pdFAIL. `xTaskNotifyFromISR(..., eSetBits, &w)` 는 대상이 하나라 결정적이므로 ISR 안에서 바로 TCB 를 고치고 깨운다. 꼬리: "`pxHigherPriorityTaskWoken` 은 이벤트 그룹에서 무슨 뜻?" → 타이머 **데몬 태스크** 가 지금 태스크보다 높으면 pdTRUE (event_groups.h 주석).

</details>

<details>
<summary>Q6. configTASK_NOTIFICATION_ARRAY_ENTRIES 를 늘리면 무엇을 얻고 무엇을 잃나요?</summary>

얻는 것: 용도별 독립 채널. 드라이버가 인덱스 1 로 TX 완료를 기다리는 동안 앱 이벤트(인덱스 0)는 깨우지도 지우지도 않고 쌓인다 (Part C, 커널 데모 C). 스트림/메시지 버퍼도 알림을 쓰고 기본 인덱스는 0 이다 (stream_buffer.c:1654) — 이런 라이브러리와 충돌을 피한다. 잃는 것: **모든 TCB** 가 커진다. Cortex-M4F 실측 `StaticTask_t` 84 → 88(2) → 92(3) → 100(4) B. 태스크 20개에 4칸이면 320 B — 세마포어 4개를 없앤 이득(320 B)이 그대로 사라진다. 꼬리: "인덱스가 다르면 한 태스크가 두 인덱스를 동시에 기다릴 수 있나?" → 없다. 한 번에 하나의 인덱스로만 블록한다. 둘 다 기다려야 하면 같은 인덱스의 다른 비트로 합친다.

</details>

<details>
<summary>Q7. 전송을 타임아웃으로 포기한 뒤 다음 전송의 완료 대기가 바로 통과됐습니다. 원인과 해결은?</summary>

늦게 온 이전 전송의 TC 인터럽트가 `vTaskNotifyGiveIndexedFromISR` 로 값 1, 상태 RECEIVED 를 남겼다. 다음 `ulTaskNotifyTakeIndexed` 는 값이 0 이 아니므로 블록하지 않고 1 을 돌려준다 → "전송 2 완료" 로 착각 (연습 1 재현). 해결 순서가 중요하다: ① 먼저 하드웨어에서 이전 전송을 확실히 멈춘다(TCIE 끄기, DMA abort, 펜딩 IRQ 클리어) — 안 그러면 정리 직후에 또 올 수 있다 ② `xTaskNotifyStateClearIndexed(NULL, IDX_TX)` + `ulTaskNotifyValueClearIndexed(NULL, IDX_TX, ~0u)` ③ 새 전송 시작 ④ Take. 세마포어도 같은 문제가 있다(`xSemaphoreTake(s, 0)` 로 비우기) — 알림만의 문제는 아니지만 "알림은 값이 남는다" 를 기억해야 한다.

</details>

<details>
<summary>Q8. FromISR 함수 여러 개를 부르는 ISR 에서 xHigherPriorityTaskWoken 은 어떻게 다루나요?</summary>

ISR 시작에 `BaseType_t w = pdFALSE;` 한 번, 모든 FromISR 호출에 `&w` 를 넘기고, 끝에 `portYIELD_FROM_ISR(w);` 한 번. FromISR API 는 깨운 태스크가 **지금 실행 중이던** 태스크보다 높을 때만 pdTRUE 를 쓰고 pdFALSE 로 되돌리지 않는다 (tasks.c 8265–8271) — 그래서 여러 호출을 OR 처럼 모을 수 있다. Cortex-M 에서 `portYIELD_FROM_ISR` 는 PendSV 를 펜딩만 하고 실제 스위치는 모든 ISR 이 끝난 뒤(테일 체이닝)에 일어난다. 또 V11 은 `xYieldPendings` 도 세워 두므로(tasks.c 8277) 인자를 NULL 로 넘겨도 다음 tick 처리에서 스위치되지만(tasks.c 5020), 그만큼 늦어진다 (RTOS-M02 가 이 부분을 미니 커널로 다룬다).

</details>

## 9. 추가 연습

### 연습 1 — 타임아웃 뒤 늦게 온 알림 (stale notification)

전송 1 이 타임아웃(`Take` 가 0)된 뒤 그 전송의 TC 가 늦게 와서 `IDX_TX` 에 1 이 남았다. 정리 없이 전송 2 를 시작하면 무슨 일이 생기는지 재현하고, `nt_state_clear` (`xTaskNotifyStateClearIndexed`), `nt_value_clear` (`ulTaskNotifyValueClearIndexed`) 와 `uart_tx_prepare()` 를 작성하라.

<details>
<summary>답 보기</summary>

정리 없이: `nt_take_begin` 이 0(값 1 이라 블록 안 함), `nt_take_end` 가 1 → 전송 2 완료로 착각. `nt_state_clear` 는 RECEIVED 일 때만 NOT_WAITING 으로 바꾸고 pdPASS, 아니면 pdFAIL. `nt_value_clear` 는 이전 값을 돌려주고 `value &= ~bits` (상태는 안 건드림 — 그래서 둘 다 필요하다: 값만 지우면 Wait 는 여전히 "받음" 으로 통과, 상태만 지우면 Take 는 값 1 로 통과). 실제 드라이버는 그 전에 이전 전송을 하드웨어에서 멈춰야 한다 (Q7).

</details>

### 연습 2 — RAM 손익 계산기

Cortex-M4F 실측(세마포어 72 B, 이벤트 그룹 28 B, `StaticTask_t` 80/84/88/92/100 B for entries 0..4)과 heap_4 규칙(`(obj + 8 + 7) & ~7`)으로 `ram_saved(design)` 을 작성하라. 세마포어 6 + 이벤트 그룹 2 를 알림으로 바꾸는 TCB 12개 시스템에서 인덱스를 그대로 둘 때 / 1→2 로 늘릴 때 / 정적 할당일 때, 그리고 세마포어 1개를 없애려 TCB 20개 시스템의 인덱스를 1→4 로 늘릴 때 각각 몇 바이트인가?

<details>
<summary>답 보기</summary>

`ram_saved = n_sem × 블록(72) + n_eg × 블록(28) − n_tcb × (TCB[after] − TCB[before])`. 블록(72) = 80, 블록(28) = 40. 결과: 560 B / 512 B (−12×4) / 440 B (정적: 6×72 + 2×28 − 48) / **−240 B** (80 − 20×16, 손해). idle 태스크와 타이머 태스크도 TCB 라는 걸 잊기 쉽다. 숫자는 이 설정(trace 끔, queue set 끔) 기준 — 설정을 바꾸면 `cm4/measure_cm4.sh` 로 다시 잰다.

</details>

### 연습 3 — "최신 값 유지 + 버린 개수 세기" 메일박스

B2 는 "오래된 값 지키기(NoOverwrite)" 와 "최신 값(Overwrite)" 중 하나만 했다. 최신 값을 남기면서 **몇 개를 덮어썼는지** 도 아는 `adc_post_newest()` 를 작성하라.

<details>
<summary>답 보기</summary>

먼저 `eSetValueWithoutOverwrite` 로 시도 → pdFAIL 이면 안 읽은 값이 있었다는 뜻이므로 `(*dropped)++` 후 `eSetValueWithOverwrite`. 100, 101, 102 를 연달아 보내면 dropped == 2, 태스크는 102 를 읽는다. ISR 안에서 두 호출 사이에 태스크가 끼어들 수는 없지만(태스크는 ISR 을 선점 못 함) 더 높은 우선순위 ISR 이 같은 태스크에 알림을 보내면 순서가 섞일 수 있다 — 그런 설계라면 `xTaskNotifyAndQueryFromISR` 로 이전 값을 함께 받아 판단한다.

</details>

### 연습 4 — wait-all + 전체 타임아웃 (tick wrap 안전)

알림으로 wait-all 을 흉내 내면 깨어날 때마다 `xTaskNotifyWait` 를 다시 부른다. 매번 같은 timeout 을 넘기면 전체 대기 시간이 늘어난다. 시작 tick 기준 남은 시간을 계산하는 `ticks_left(start, now, timeout)` 과 `wait_all_deadline()` 을 작성하라. 시작 tick 이 `0xFFFFFFFC` (곧 wrap) 일 때도 맞아야 한다.

<details>
<summary>답 보기</summary>

`elapsed = now - start` (부호 없는 뺄셈은 wrap 을 자동으로 처리, RTOS-B06 참고) → `elapsed >= timeout ? 0 : timeout - elapsed`. 이벤트가 +3, +7, +9 tick 에 오면 timeout 10 안에 완료(done tick = 5, wrap 후), MAG 가 +12 에 오면 2번 깨어난 뒤 만료. FreeRTOS 에서는 `vTaskSetTimeOutState(&to)` 로 시작을 기록하고 매번 `xTaskCheckForTimeOut(&to, &remaining)` 로 남은 tick 을 갱신하는 패턴이 같은 일을 한다 (이벤트 그룹의 `xEventGroupWaitBits` 는 이 루프가 필요 없다 — wait-all 을 커널이 한다).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/RTOS-M01_pr && /tmp/RTOS-M01_pr   # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/RTOS-M01_ps && /tmp/RTOS-M01_ps   # 보고 없음
```

## 10. 핵심 정리

- 태스크 알림 = TCB 안의 `uint32_t ulNotifiedValue[N]` + `uint8_t ucNotifyState[N]` (NOT_WAITING / WAITING / RECEIVED). 중간 객체가 없어 추가 RAM 0 B, 깨우기 경로가 짧다.
- 보내기 5종: `eIncrement`(세마포어) · `eSetBits`(이벤트) · `Overwrite`(최신 값) · `WithoutOverwrite`(안 읽었으면 pdFAIL) · `eNoAction`. 받기 2종: `ulTaskNotifyTake` 는 **값** 을 보고(clear = 개수 한 번에 / decrement = 하나씩), `xTaskNotifyWait` 는 **상태** 를 본다(entry 마스크는 블록할 때만, `*v` 는 exit 지우기 전 값).
- 바이너리 세마포어보다 낫다: 5번 give → Take 가 5. 단 상한 없음(wrap), 반환 0 = 타임아웃/가짜 깨움.
- 한계: 수신자 1명, ISR 은 못 받음, 값 1개, 보내는 쪽 블록 불가, wait-all 없음(깨어남 3번 vs 1번). 브로드캐스트·wait-all·랑데부는 이벤트 그룹, 데이터는 큐, 상호 배제는 뮤텍스.
- ISR 에서 이벤트 그룹 set 은 타이머 태스크로 미뤄진다(event_groups.c:826) — 알림은 바로.
- Cortex-M4F 실측: 세마포어 72 B(heap_4 80), 이벤트 그룹 28 B(40), TCB 84 B — 인덱스 하나 늘릴 때마다 **모든 TCB** +4~8 B.
- 인덱스는 용도별로: 앱 비트(0) vs 드라이버 완료(1). 섞으면 Take 가 비트를 개수로 먹는다. 새 전송 전 이전 알림 정리(StateClear + ValueClear).
- 호스트 시간은 POSIX 포트의 시스템 콜을 잰다: 태스크 수준 API 차이는 안 보이고, FromISR(1.1 vs 1.8 ns)와 경로 구조로 설명한다. MCU 숫자는 DWT 로.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 — `tasks.c` 113–115(상태 값), 438–439(TCB 필드), 1285(`TCB_t` 크기 assert), 7797–7892 `ulTaskGenericNotifyTake`, 7899–8006 `xTaskGenericNotifyWait`, 8013–8126 `xTaskGenericNotify`, 8133– `xTaskGenericNotifyFromISR`(8265–8271 hptw), 8315– `vTaskGenericNotifyGiveFromISR`; `queue.c` 406, 1338 `xQueueGiveFromISR`; `event_groups.c` 101, 547 `xEventGroupSetBits`, 817–833 `xEventGroupSetBitsFromISR`; `include/event_groups.h` 43–57(제어 비트), 536–600; `stream_buffer.c` 1654, 1690; `portable/MemMang/heap_4.c` 158, 188–196; `portable/ThirdParty/GCC/Posix/port.c` 354–436 — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS — RTOS Task Notifications (사용법, 성능 주장, 배열 인덱스) — <https://www.freertos.org/RTOS-task-notifications.html>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Task Notifications 장 (장점과 한계 목록), Event Groups 장 — <https://www.freertos.org/Documentation/RTOS_book.html>
- CMSIS-RTOS2 Thread Flags (`osThreadFlagsSet` / `osThreadFlagsWait`) — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__ThreadFlagsMgmt.html>
- Zephyr — Semaphores, Events (`k_event_post`, `k_event_wait_all`) — <https://docs.zephyrproject.org/latest/kernel/services/synchronization/events.html>
- STM32 USART `ISR`/`ICR` 레지스터 (IDLE·ORE·TC 와 W1C 클리어) — 예: RM0351 (STM32L4) USART 장
- 관련 레슨: RTOS-B08 (바이너리 세마포어 ISR 신호, 이벤트 합쳐짐), RTOS-B09 (카운팅 세마포어), RTOS-B10 (뮤텍스 vs 세마포어), RTOS-B13 (이벤트 그룹), RTOS-M02 (FromISR·yield 요청), RTOS-M03/M04 (우선순위 역전·상속), RTOS-B06 (tick wrap 안전 비교), C-M14 (ISR 공유 데이터), C-M02 (W1C)

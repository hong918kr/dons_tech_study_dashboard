---
id: RTOS-B12
time: 65분
updated: 2026-11-11
---
# RTOS-B12 소프트웨어 타이머: one-shot·auto-reload·타이머 태스크

## 학습 목표

- 소프트웨어 타이머(software timer)를 **만료 순 리스트 + 명령 큐 + 타이머 서비스 태스크(daemon)** 로 직접 구현하고, one-shot 과 auto-reload 의 차이(다음 만료 = **이전 만료 + 주기**, 늦으면 지난 만료마다 콜백을 몰아서 부르는 "따라잡기")를 코드로 설명한다.
- `xTimerStart/Reset/Stop/ChangePeriod` 는 **명령을 큐에 넣을 뿐** 이고 실제 일은 데몬이 할 때 일어난다는 것 — 그래서 `xTimerIsTimerActive` 가 방금 start 한 타이머에 pdFALSE 를 줄 수 있고, reset 의 기준은 "처리한 순간" 이 아니라 "**부른 순간**" 이라는 것을 안다.
- 콜백은 **데몬 태스크 문맥에서** 돈다 → 콜백 하나가 막히면(vTaskDelay, 뮤텍스 대기) **다른 모든 타이머가 밀린다**. 이것을 틱 타임라인으로 재현·측정하고(LED 최대 7 틱 지연, 통신 타임아웃 1번이 사라짐) worker 태스크로 넘겨 고친다.
- `configTIMER_TASK_PRIORITY` 가 앱 태스크보다 낮으면 생기는 일(LED 콜백 4번이 한 틱에 몰림, 최대 13 틱 지연)과 `configTIMER_QUEUE_LENGTH` 가 넘치는 경우(ISR 에서 바이트마다 reset → pdFAIL)를 숫자로 말할 수 있다.
- LED 블링크(auto-reload) + 통신 타임아웃(one-shot, 수신마다 reset) + `xTimerPendFunctionCallFromISR` 로 짠 "링크 상태 표시" 를 모델과 진짜 FreeRTOS(V11.3.2 POSIX 포트)에서 같은 틱 번호로 확인한다.

## 1. 문제

"소프트웨어 타이머 콜백은 어디서 실행되나요? 콜백 안에서 `vTaskDelay` 를 부르면?" — FreeRTOS 를 써 본 사람을 가르는 면접 질문이다. "타이머니까 ISR 에서 돌겠죠" 라고 답하면 바로 탈락이다. FreeRTOS 의 콜백은 **"Tmr Svc" 라는 평범한 태스크** 안에서 하나씩 차례로 불리므로, 콜백 하나가 느리면 그 뒤의 모든 타이머가 늦는다. 그리고 그 태스크의 우선순위와 명령 큐 길이는 `FreeRTOSConfig.h` 의 세 줄이 정한다.

펌웨어에서 소프트웨어 타이머는 "하드웨어 타이머를 쓸 만큼 정밀할 필요는 없는 ms 단위 일" 을 맡는다. STM32 Nucleo 의 LD2(PA5) 를 500 ms 마다 토글하는 상태 LED, BLE 센서 노드가 5 초 동안 패킷을 못 받으면 "링크 끊김" 으로 판단하는 **통신 타임아웃**, 버튼 채터링을 거르는 디바운스, 워치독 체크. 하드웨어 TIMx 는 몇 개 없고 µs 단위 PWM/ADC 트리거에 써야 하니, 이런 일은 RTOS 틱 하나로 수십 개를 돌리는 소프트웨어 타이머가 한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">FreeRTOS 소프트웨어 타이머: API 는 명령을 보내고, 일은 데몬 태스크가 한다</text>
  <rect x="10" y="44" width="200" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="66" fill="currentColor">앱 태스크</text>
  <text x="20" y="86" fill="currentColor" font-family="monospace" font-size="12">xTimerReset(t, 0)</text>
  <rect x="10" y="110" width="200" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="132" fill="currentColor">UART RX ISR</text>
  <text x="20" y="152" fill="currentColor" font-family="monospace" font-size="12">xTimerResetFromISR</text>
  <rect x="10" y="176" width="200" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="198" fill="currentColor">ISR (지연 처리)</text>
  <text x="20" y="218" fill="currentColor" font-family="monospace" font-size="12">PendFunctionCallFromISR</text>
  <path d="M210 71 L238 120 M210 137 L238 137 M210 203 L238 154" stroke="currentColor" fill="none"/>
  <rect x="240" y="104" width="160" height="66" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="250" y="126" fill="currentColor" font-weight="bold">타이머 명령 큐</text>
  <text x="250" y="146" fill="currentColor" font-family="monospace" font-size="12">길이 = configTIMER_</text>
  <text x="250" y="162" fill="currentColor" font-family="monospace" font-size="12">QUEUE_LENGTH (10)</text>
  <path d="M400 137 L448 137" stroke="currentColor"/>
  <path d="M440 131 L448 137 L440 143" stroke="currentColor" fill="none"/>
  <rect x="450" y="44" width="280" height="190" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="462" y="66" fill="currentColor" font-weight="bold">"Tmr Svc" 데몬 태스크</text>
  <text x="462" y="86" fill="currentColor" font-family="monospace" font-size="12">prio = configTIMER_TASK_PRIORITY</text>
  <text x="462" y="110" fill="currentColor">for (;;) {</text>
  <text x="474" y="130" fill="currentColor">① 명령 큐를 비운다 (start/reset/...)</text>
  <text x="474" y="150" fill="currentColor">② 만료된 타이머 1개 → 콜백()</text>
  <text x="474" y="170" fill="currentColor">③ 할 일 없으면 Blocked:</text>
  <text x="490" y="188" fill="currentColor">새 명령 또는 다음 만료까지</text>
  <text x="462" y="208" fill="currentColor">}  ← 콜백도 이 태스크 스택에서 돈다</text>
  <rect x="240" y="252" width="490" height="66" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="252" y="274" fill="currentColor" font-weight="bold">활성 타이머 리스트 (만료 틱 오름차순)</text>
  <text x="252" y="296" fill="currentColor" font-family="monospace" font-size="12">LED@12 → COMMS@14 → LED... (같은 틱은 먼저 넣은 순)</text>
  <text x="252" y="312" fill="currentColor" font-size="12">auto-reload 는 만료 후 "이전 만료 + 주기" 로 다시 들어간다</text>
  <path d="M590 234 L590 250" stroke="currentColor"/>
  <text x="10" y="262" fill="currentColor" font-size="12">콜백이 막히면 ①②가</text>
  <text x="10" y="280" fill="currentColor" font-size="12">전부 멈춘다 = 모든</text>
  <text x="10" y="298" fill="currentColor" font-size="12">타이머가 함께 늦는다</text>
</svg>
```

호스트(맥)에는 SysTick 도 RTOS 도 없으니 두 가지를 쓴다.

1. **체크되는 코드(starter/solution/practice)** — OS 스레드 1개가 틱을 하나씩 돌리는 **결정적 모델**. FreeRTOS `timers.c` 의 구조를 그대로 옮겼다: 타이머 API 는 명령(`tmr_cmd_t`)을 큐에 넣고, 데몬(`daemon_run`)이 ① 큐 비우기 ② 만료 1개 처리를 반복한다. 한 틱 안의 순서:

   | 순서 | 하는 일 | 실제 RTOS |
   |---|---|---|
   | ⓪ | 콜백 안에서 막혔던 데몬 / worker 를 깨운다 | SysTick 의 지연 리스트 만료 |
   | ① | ISR 각본 (`isr(now)`): `timer_reset_from_isr`, `pend_function_call_from_isr` | USART IDLE/EXTI ISR |
   | ② | 데몬이 할 일이 있고 READY 중 **가장 높으면** `daemon_run(now)` — 콜백은 µs 라 **시간 0** 으로 친다 | PendSV 로 Tmr Svc 로 전환 |
   | ③ | 가장 높은 앱 태스크가 그 틱을 쓴다. 태스크가 명령을 보냈는데 데몬이 더 높으면 **그 자리에서 선점** | 태스크 코드 |

   타임라인 표기: `Tmr` 줄 `R` = 그 틱에 데몬이 돌았다, `r` = 할 일이 있는데 더 높은 태스크 때문에 못 돈다, `B` = **콜백 안에서 막혀 있다**, `.` = 정상 대기. 타이머 줄의 숫자 = 그 틱에 불린 콜백 수. 데몬은 시간 0 이므로 `Tmr R` 과 `ctrl R` 이 같은 열에 나올 수 있다.
2. **진짜 FreeRTOS** — `demo_freertos_timers.c`. "ISR" 은 틱 ISR 안에서 불리는 `vApplicationTickHook` (RTOS-B08 과 같은 방법), 데몬 우선순위는 `vApplicationDaemonTaskStartupHook` 에서 모드별로 바꾼다. 실시간성은 없으므로 **틱 번호·반환값·순서만** 비교한다.

### Part A — 타이머 리스트와 데몬 (`tmr_insert`, `tmr_expire`, `tmr_process_command`)

- `tmr_insert(t)` — 활성 리스트에 `expiry` 오름차순으로 넣는다. 같은 값은 기존 것 **뒤**(FIFO). 비교는 랩 안전한 `before(a, b) = (int32_t)(a - b) < 0`.
- `tmr_expire(t, now)` — 이미 리스트에서 뺀 만료 타이머 처리. one-shot 은 `active = false` 후 콜백 1번. auto-reload 는 다음 만료 = **이전 만료 + period**, 그것도 이미 지났으면 지난 만료마다 콜백을 부르며 따라잡은 뒤 다시 넣는다.
- `tmr_process_command(c, now)` — START/RESET: `expiry = 보낸 틱 + period` (처리할 때 이미 지났으면 즉시 `tmr_expire`), STOP, CHANGE_PERIOD: `expiry = now + 새 주기`, PEND_CALL: 함수 호출.

### Part B — 콜백이 막히면, 그리고 데몬 우선순위 (`log_flush_cb_deferred`)

- 시나리오: LED(auto-reload 4), FLUSH(one-shot 9 — 로그를 플래시에 쓰는데 10 틱 걸린다), COMMS(one-shot 14 통신 타임아웃), 15 틱에 RX ISR 이 COMMS 를 reset.
- 나쁜 예(주어짐): FLUSH 콜백이 `cb_block(10)` (= `vTaskDelay(10)`). 측정: LED 지연, 몰림, COMMS 가 언제 불리나.
- 고침(TODO): 콜백은 worker 세마포어를 give 만(`g_wrk.sem++`), worker(prio 2)가 10 틱 기다린다.
- 우선순위 실험(하네스): ctrl(prio 3)이 10..24 틱 CPU 를 쓰고 11 틱에 `timer_start(X)` 직후 `timer_is_active(X)` 를 본다. 데몬 prio 1 vs 6.

### Part C — 통신 타임아웃 + LED 링크 표시 (`comms_timeout_cb`, `link_up_pended`, `uart_frame_isr`)

- LED: 링크 정상이면 주기 8, 끊기면 2 (빠른 깜빡임). COMMS: one-shot 20, **UART 프레임마다 reset**. 프레임은 5, 15, 25, 35 틱에 오다 끊기고 80, 90, 100 틱에 돌아온다.
- `comms_timeout_cb` (데몬 문맥): 링크 끊김 표시 + `timer_change_period(&g_led, LED_FAST)`.
- `uart_frame_isr` (ISR): `timer_reset_from_isr(&g_comms)`, 끊겨 있으면 상태를 직접 건드리지 말고 `pend_function_call_from_isr(link_up_pended, …)` 로 데몬에게 넘긴다. 링크 상태는 **데몬만** 쓴다 → 잠금이 필요 없다.
- 덤: ISR 이 바이트마다 reset 하면 12바이트 버스트에서 명령 큐(10칸)가 넘친다 — 반환값으로 센다.

## 2. Problem (English)

> **Software timers with a timer-service (daemon) task.**
>
> Implement the core of a FreeRTOS-style software-timer service in a deterministic, tick-driven model. Timer API calls (`timer_start/reset/stop/change_period`, `pend_function_call`) only enqueue a command (block time 0, fail if the queue is full). START/RESET carry the tick at which they were *called*. A daemon task repeatedly (1) drains the command queue and (2) processes one expired timer; callbacks run in the daemon's context.
>
> 1. `void tmr_insert(swtimer_t *t)` — insert into the active list ordered by `expiry` (wrap-safe), ties after existing entries.
> 2. `void tmr_expire(swtimer_t *t, uint32_t now)` — `t` was removed from the list and `t->expiry <= now`. One-shot: mark inactive, call back once. Auto-reload: keep the phase (`next = previous expiry + period`); if the daemon is late, invoke the callback once per missed expiry, then re-insert with the first expiry after `now`.
> 3. `void tmr_process_command(const tmr_cmd_t *c, uint32_t now)` — START/RESET: active, `expiry = sent_tick + period`, expire immediately if already due; STOP; CHANGE_PERIOD: `expiry = now + period`; PEND_CALL: call the function.
> 4. Part B/C: a non-blocking flush callback that hands the slow work to a worker task; a comms timeout (one-shot 20, reset by the UART frame ISR) that switches an auto-reload LED between periods 8 and 2, with the link-up path deferred from the ISR to the daemon.
>
> Example: LED auto-reload 4, a one-shot callback at tick 9 blocks the daemon for 10 ticks → LED callbacks at `4 8 19 19 20 24 …` (max lateness 7). Daemon priority 1 below a CPU-bound task running ticks 10..24 → LED callbacks `4 8 25 25 25 25 28 …`.
>
> Follow-ups: Where do timer callbacks execute? Why must a callback never block? What does `xTimerIsTimerActive()` return right after `xTimerStart()`? How do you size `configTIMER_QUEUE_LENGTH`?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "5/35 passed", exit 1 — TODO 7곳(A 3 + B 1 + C 3)을 채워 35/35 를 만든다
```

스텁 상태에서도 멈추지 않는다: 데몬 루프는 큐를 다 꺼내고(처리는 스텁이 무시) 리스트가 비어 있으면 끝나며, 루프에 1000회 상한도 있다. `(void)tmr_fire;` 같은 줄은 스텁에서 "안 쓰는 함수" 경고만 끄는 것이니 채울 때 지운다.

진짜 FreeRTOS 데모 (커널 소스 `~/.esn_scratch/FreeRTOS-Kernel`, V11.3.2 태그. 이 폴더의 `FreeRTOSConfig.h` 는 템플릿에 틱 훅·데몬 시작 훅을 켜고 타이머 설정에 주석을 단 것):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_timers.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o demo_timers
for m in block defer prio_low prio_high link burst; do ./demo_timers $m; done
```
커널 파일에는 `-Werror` 를 붙이지 않았다. 이 레슨의 `demo_freertos_timers.c`·`hooks.c` 는 따로 `-c -Wall -Wextra -Werror` 로 컴파일해도 경고 0 이다.

## 4. 힌트

<details>
<summary>힌트 1 — 리스트 삽입: "나보다 늦은 첫 노드" 앞</summary>

포인터의 포인터(`swtimer_t **pp = &g_head`)로 걸어가면 머리 삽입을 따로 처리할 필요가 없다. 멈추는 조건은 `before(t->expiry, (*pp)->expiry)` — **엄격히** 이른 경우에만 앞에 넣어야 같은 만료 틱이 FIFO 가 된다 (FreeRTOS `vListInsert` 도 `<=` 인 동안 지나간다, `list.c:192`).

</details>

<details>
<summary>힌트 2 — auto-reload: now + period 가 아니다</summary>

데몬이 12 틱 만료를 19 틱에 처리한다고 하자. `now + period = 23` 으로 다시 넣으면 LED 가 영원히 3 틱 밀린다(위상 이동). 맞는 답은 `12 + 4 = 16` — 그런데 16 도 이미 지났다. 그러면 16 몫의 콜백도 지금 부르고 20 으로 넣는다. `while (is_due(due + period, now)) { fire(due); due += period; }` 다음에 넣고, 마지막 콜백.

</details>

<details>
<summary>힌트 3 — 명령은 "보낸 틱" 을 싣고 온다</summary>

`xTimerReset` 매크로는 `xTaskGetTickCount()` 를 명령에 담는다 (`timers.h:803-804`). 데몬이 25 틱에야 11 틱에 보낸 START(주기 10)를 처리하면 만료는 21 — 이미 지났으니 리스트에 넣지 말고 그 자리에서 `tmr_expire`. CHANGE_PERIOD 만 예외로 "처리하는 지금" 기준이다 (`timers.c:1048`). 그리고 어떤 명령이든 먼저 리스트에서 뺀다.

</details>

<details>
<summary>힌트 4 — Part C: 상태는 한 문맥만 쓴다</summary>

`g_link_up` 를 ISR 과 데몬이 둘 다 쓰면 임계 구역이 필요하다. ISR 은 "끊겨 있네?" 만 보고 실제 변경은 `pend_function_call_from_isr` 로 데몬에게 넘기면, 쓰는 쪽은 데몬 하나뿐이다. pend 가 두 번 들어와도 괜찮게 `link_up_pended` 첫 줄에서 이미 up 이면 돌아간다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨의 모델 | FreeRTOS (V11.3.2) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준) |
|---|---|---|---|
| `timer_init(t, name, p, auto, cb)` | `xTimerCreate(name, p, pdTRUE/pdFALSE, id, cb)` | `k_timer_init(&t, expiry_fn, stop_fn)` | `osTimerNew(cb, osTimerPeriodic/osTimerOnce, arg, NULL)` |
| `timer_start(t)` | `xTimerStart(t, 0)` | `k_timer_start(&t, dur, period)` (period 가 `K_NO_WAIT` 이면 한 번) | `osTimerStart(id, ticks)` |
| `timer_reset(t)` | `xTimerReset(t, 0)` | `k_timer_start` 다시 호출 | `osTimerStart` 다시 호출 |
| `timer_stop` / `timer_change_period` | `xTimerStop` / `xTimerChangePeriod` | `k_timer_stop` / 새 값으로 start | `osTimerStop` / 새 값으로 start |
| `timer_reset_from_isr` | `xTimerResetFromISR(t, &woken)` | ISR 에서 `k_timer_start` 가능 | ISR 에서 호출 불가로 문서화 |
| `pend_function_call(_from_isr)` | `xTimerPendFunctionCall(FromISR)` | `k_work_submit` (system workqueue) | 해당 없음 |
| 데몬 `daemon_run`, `g_dmn_prio` | "Tmr Svc" 태스크, `configTIMER_TASK_PRIORITY` | `k_timer` 만료 함수는 **ISR 문맥**. 스레드 문맥이 필요하면 `k_work_delayable` | RTX5 는 타이머 스레드(osRtxTimerThread)에서 콜백 |
| `g_qlen` | `configTIMER_QUEUE_LENGTH` | 명령 큐 없음 | RTX5 설정의 타이머 큐 |
| `cb_block(n)` | 콜백 안 `vTaskDelay(n)` / `xSemaphoreTake(…, n)` | (ISR 문맥이라 블록 자체가 불가) | |

Zephyr/CMSIS 칸은 공식 문서만 보고 쓴 것이고 이 레슨에서 실행하지 않았다(RTOS-A07). 핵심 차이 하나만 기억하자: **FreeRTOS 콜백 = 태스크 문맥, Zephyr `k_timer` 만료 함수 = 인터럽트 문맥**.

### Part A-1 — 리스트 삽입

```c
static void tmr_insert(swtimer_t *t) {
    swtimer_t **pp = &g_head;
    while (*pp && !before(t->expiry, (*pp)->expiry)) pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
    t->in_list = true;
}
```

테스트: expiry 30, 10, 20, 10 을 넣으면 `b(10) d(10) c(20) a(30)` — 같은 10 은 넣은 순. O(n) 삽입이지만 데몬이 매번 보는 것은 머리 하나(O(1))다. FreeRTOS 도 같은 구조(`vListInsert` 정렬 삽입, `listGET_ITEM_VALUE_OF_HEAD_ENTRY` 로 다음 만료, `prvGetNextExpireTime`, `timers.c:842-866`). 틱이 넘칠 때는 FreeRTOS 는 리스트를 두 개(current/overflow) 두고 넘치는 순간 바꾼다(`prvSwitchTimerLists`, `timers.c:1091`). 이 모델은 부호 있는 차(`before`)로 대신했다 — 활성 타이머들이 서로 2^31 틱 안에 있으면 맞다 (연습 1, 랩 상세는 RTOS-B06).

### Part A-2 — 만료: one-shot vs auto-reload (따라잡기)

```c
static void tmr_expire(swtimer_t *t, uint32_t now) {
    uint32_t due = t->expiry;
    if (t->auto_reload) {
        while (is_due(due + t->period, now)) {      /* 다음 만료도 이미 지났다 → 이번 것 콜백, 한 칸 전진 */
            tmr_fire(t, due, now);
            due += t->period;
        }
        t->expiry = due + t->period;
        tmr_insert(t);
    } else {
        t->active = false;
    }
    tmr_fire(t, due, now);
}
```

FreeRTOS 원본 (발췌, 줄 번호는 실제 파일):

```c
/* timers.c V11.3.2 — prvReloadTimer 697-713, prvProcessExpiredTimer 716-743 */
704  while( prvInsertTimerInActiveList( pxTimer, ( xExpiredTime + pxTimer->xTimerPeriodInTicks ), xTimeNow, xExpiredTime ) != pdFALSE )
707      xExpiredTime += pxTimer->xTimerPeriodInTicks;          /* 다음 만료도 지났으면 전진 */
711      pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer ); /* 지난 몫의 콜백 */
731  if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U ) prvReloadTimer( pxTimer, xNextExpireTime, xTimeNow );
737  else pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE ); /* one-shot: 비활성 */
742  pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );     /* 마지막 콜백 */
```

`prvInsertTimerInActiveList` 가 pdTRUE("넣지 말고 지금 처리") 를 돌려주는 조건은 `xNextExpiryTime <= xTimeNow` 이면서 `xTimeNow - xCommandTime >= period` (`timers.c:902-906`) — 재로드에서는 `xCommandTime = 이전 만료` 이므로 결국 "다음 만료도 이미 지났나" 다. 모델의 `while` 과 같다.

| 상황 (주기 4, 만료 12) | now | 콜백 (due) | 다음 만료 |
|---|---|---|---|
| 제때 | 12 | 12 | 16 |
| 7 틱 늦음 | 19 | **12, 16** (한 틱에 2번) | 20 |
| 13 틱 늦음 | 25 | **12, 16, 20, 24** (4번) | 28 |
| (틀린 구현) now + period | 19 | 12 | 23 → 이후 영원히 3 틱 밀림 |

따라잡기는 **위상을 지킨다**(20, 24, 28… 원래 격자). 대신 콜백이 한 틱에 몰린다. LED 토글이면 몰린 2번이 서로 상쇄되어 깜빡임 위상이 맞게 남고, "건너뛰기"(1번만 부르기) 구현이면 LED 가 반대 위상으로 굳는다 (연습 2 에서 확인).

### Part A-3 — 명령 처리

```c
static void tmr_process_command(const tmr_cmd_t *c, uint32_t now) {
    if (c->id == CMD_PEND_CALL) { c->fn(c->arg1, c->arg2); return; }
    swtimer_t *t = c->t;
    if (t->in_list) tmr_remove(t);
    switch (c->id) {
    case CMD_START:
    case CMD_RESET:
        t->active = true;
        t->expiry = c->value + t->period;              /* "보낸 틱" 기준 */
        if (is_due(t->expiry, now)) tmr_expire(t, now);
        else tmr_insert(t);
        break;
    case CMD_STOP:  t->active = false; break;
    case CMD_CHANGE_PERIOD:
        t->period = c->value; t->active = true;
        t->expiry = now + t->period;                   /* "처리한 틱" 기준 */
        tmr_insert(t);
        break;
    default: break;
    }
}
```

FreeRTOS `prvProcessReceivedCommands` (`timers.c:936-1080`): 큐에서 하나씩 꺼내(943) 음수 ID 면 pend 된 함수 호출(950-958), 아니면 리스트에서 빼고(977-979) — START/RESET 은 `ACTIVE` 를 켜고 `xMessageValue(보낸 틱) + period` 로 넣되 이미 지났으면 바로 재로드/비활성 + 콜백(999-1028), STOP 은 `ACTIVE` 만 끄고(1030-1034), CHANGE_PERIOD 는 `ACTIVE` 를 켜고 `xTimeNow + period`(1036-1048). 두 가지 함의:

- **잠든 타이머에 `xTimerChangePeriod` 를 부르면 타이머가 시작된다** (1038 에서 ACTIVE 를 켠다). "주기만 바꿔 두자" 했다가 원치 않는 콜백이 온다 — 연습 3b 에서 재현.
- `xTimerIsTimerActive()` 는 `ACTIVE` 비트만 본다(`timers.c:1179`). 비트를 켜는 건 **데몬** 이므로 데몬이 아직 처리 안 했으면 방금 start 한 타이머도 pdFALSE 다 (Part B 실측).

데몬 루프(주어진 코드)가 ① 큐 비우기 → ② 만료 1개 순서인 이유: FreeRTOS `prvTimerTask` 는 `prvProcessTimerOrBlockTask`(만료 1개 처리 **또는** 대기) 다음에 `prvProcessReceivedCommands` 를 부른다(`timers.c:766-775`). 데몬이 멈추는 곳은 언제나 대기(818, `vQueueWaitForMessageRestricted`) 아니면 콜백 안이고, 둘 다 그 다음 줄이 "큐 비우기" 다. 그래서 **깨어난 데몬은 큐부터 본다** — Part B 의 "사라진 타임아웃" 이 여기서 나온다.

### Part B1 — 콜백이 10 틱 막히면 (실제 출력)

```text
  -- blocking callback (FLUSH 콜백이 10 틱 대기), Tmr prio 6
  tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
  Tmr  : R . . . R . . . R B B B B B B B B B B R R . . . R . . . R R . . R . . . R . . .
  LED  : . . . . 1 . . . 1 . . . . . . . . . . 2 1 . . . 1 . . . 1 . . . 1 . . . 1 . . .
  FLUSH: . . . . . . . . . 1 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
  COMMS: . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 1 . . . . . . . . . .
  order: LED@4 LED@8 FLUSH@9 LED@19 LED@19 LED@20 LED@24 LED@28 COMMS@29 LED@32 LED@36
  -- deferred (FLUSH 콜백은 give 만, worker 가 10 틱 대기)
  Tmr  : R . . . R . . . R R . . R . R R R . . . R . . . R . . . R R . . R . . . R . . .
  wrk  : . . . . . . . . . R B B B B B B B B B . . . . . . . . . . . . . . . . . . . . .
  LED  : . . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . .
  COMMS: . . . . . . . . . . . . . . 1 . . . . . . . . . . . . . . 1 . . . . . . . . . .
  order: LED@4 LED@8 FLUSH@9 LED@12 COMMS@14 LED@16 LED@20 LED@24 LED@28 COMMS@29 LED@32 LED@36
```

틱 19 에 무슨 일이 있었나 (막힌 버전):

| 순서 | 데몬이 하는 일 | 결과 |
|---|---|---|
| 9 | FLUSH 만료 → 콜백 → `vTaskDelay(10)` | 데몬 Blocked 9..18. 그동안 LED 12, 16 과 COMMS 14 가 리스트에서 기다린다 |
| 15 | (ISR) COMMS reset 명령이 **큐에** 들어감 — 보낸 틱 15 | 데몬이 막혀 처리 못 함 |
| 19 ① | 깨어나 큐부터: RESET COMMS → 리스트에서 빼고 15+14 = 29 로 | **14 의 타임아웃은 한 번도 불리지 않고 사라졌다** |
| 19 ② | LED(12) 만료 → 16 도 지남 → 콜백 2번, 20 으로 | LED 최대 7 틱 늦음, 한 틱에 2번 |

통신 쪽에서 보면 0 틱 이후 15 틱 동안 아무것도 안 왔다 — 타임아웃 14 를 넘겼다. 그런데 콜백 하나가 데몬을 막은 사이 늦은 RX 의 reset 이 먼저 처리되어 **장애가 보고조차 되지 않았다**. 고친 버전에서는 COMMS 가 14 에 제대로 불리고 15 의 reset 이 잠든 타이머를 다시 시작한다(→ 29).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">solution 이 찍은 타임라인 (틱 0~39). 위: 콜백이 막힘 / 아래: worker 로 넘김</text>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="107" y="40">0</text><text x="182" y="40">5</text><text x="254" y="40">10</text><text x="329" y="40">15</text>
    <text x="404" y="40">20</text><text x="479" y="40">25</text><text x="554" y="40">30</text><text x="629" y="40">35</text>
  </g>
  <line x1="110" y1="46" x2="710" y2="46" stroke="currentColor"/>
  <text x="10" y="70" fill="currentColor">Tmr (막힘)</text>
  <g class="f-ok" stroke="currentColor">
    <rect x="110" y="58" width="15" height="16"/><rect x="170" y="58" width="15" height="16"/><rect x="230" y="58" width="15" height="16"/>
    <rect x="395" y="58" width="15" height="16"/><rect x="410" y="58" width="15" height="16"/><rect x="470" y="58" width="15" height="16"/>
    <rect x="530" y="58" width="15" height="16"/><rect x="545" y="58" width="15" height="16"/><rect x="590" y="58" width="15" height="16"/><rect x="650" y="58" width="15" height="16"/>
  </g>
  <rect x="245" y="58" width="150" height="16" class="f-warn" stroke="currentColor"/>
  <text x="262" y="71" fill="currentColor" font-size="12">B: vTaskDelay(10)</text>
  <text x="10" y="100" fill="currentColor">LED</text>
  <g fill="currentColor" font-family="monospace">
    <text x="173" y="100">|</text><text x="233" y="100">|</text><text x="396" y="100">||</text><text x="413" y="100">|</text>
    <text x="473" y="100">|</text><text x="533" y="100">|</text><text x="593" y="100">|</text><text x="653" y="100">|</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 2" fill="none"><rect x="290" y="86" width="15" height="18"/><rect x="350" y="86" width="15" height="18"/></g>
  <text x="270" y="120" fill="currentColor" font-size="12">12, 16 이 19 로 밀려 2번 몰림 (최대 7 틱 늦음)</text>
  <text x="10" y="144" fill="currentColor">COMMS</text>
  <text x="320" y="144" fill="currentColor" font-family="monospace">×</text>
  <text x="336" y="144" fill="currentColor" font-size="12">14 의 타임아웃 사라짐</text>
  <text x="548" y="144" fill="currentColor" font-family="monospace">|</text><text x="560" y="144" fill="currentColor" font-size="12">29</text>
  <line x1="110" y1="160" x2="710" y2="160" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="10" y="186" fill="currentColor">Tmr (고침)</text>
  <g class="f-ok" stroke="currentColor">
    <rect x="110" y="174" width="15" height="16"/><rect x="170" y="174" width="15" height="16"/><rect x="230" y="174" width="15" height="16"/><rect x="245" y="174" width="15" height="16"/>
    <rect x="290" y="174" width="15" height="16"/><rect x="320" y="174" width="15" height="16"/><rect x="335" y="174" width="15" height="16"/><rect x="350" y="174" width="15" height="16"/>
    <rect x="410" y="174" width="15" height="16"/><rect x="470" y="174" width="15" height="16"/><rect x="530" y="174" width="15" height="16"/><rect x="545" y="174" width="15" height="16"/>
    <rect x="590" y="174" width="15" height="16"/><rect x="650" y="174" width="15" height="16"/>
  </g>
  <text x="10" y="214" fill="currentColor">worker</text>
  <rect x="245" y="202" width="15" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="260" y="202" width="135" height="16" class="f-warn" stroke="currentColor"/>
  <text x="270" y="215" fill="currentColor" font-size="12">B: 플래시 쓰기 대기</text>
  <text x="10" y="242" fill="currentColor">LED</text>
  <g fill="currentColor" font-family="monospace">
    <text x="173" y="242">|</text><text x="233" y="242">|</text><text x="293" y="242">|</text><text x="353" y="242">|</text><text x="413" y="242">|</text>
    <text x="473" y="242">|</text><text x="533" y="242">|</text><text x="593" y="242">|</text><text x="653" y="242">|</text>
  </g>
  <text x="10" y="266" fill="currentColor">COMMS</text>
  <text x="323" y="266" fill="currentColor" font-family="monospace">|</text><text x="548" y="266" fill="currentColor" font-family="monospace">|</text>
  <text x="10" y="292" fill="currentColor" font-size="12">초록 = 데몬이 돈 틱 (콜백은 µs 라 시간 0 으로 침), 노랑 = 막힘. 막대 = 콜백 1번. 고친 쪽: LED 늦음 0, COMMS 14·29.</text>
</svg>
```

**진짜 FreeRTOS 도 같은가** — `demo_freertos_timers.c`:

<!-- code: demo_freertos_timers.c -->

실제 출력 (Apple M6, Apple clang 21.0.0; 모드마다 30회 실행):

```text
mode=block  timer task prio=6  (ticks < 40)
  LED   : 4 8 19 19 20 24 28 32 36
  FLUSH : 9
  COMMS : 29
  order : LED@4 LED@8 FLUSH@9 LED@19 LED@19 LED@20 LED@24 LED@28 COMMS@29 LED@32 LED@36
mode=defer  timer task prio=6  (ticks < 40)
  LED   : 4 8 12 16 20 24 28 32 36
  FLUSH : 9
  COMMS : 14 29
  order : LED@4 LED@8 FLUSH@9 LED@12 COMMS@14 LED@16 LED@20 LED@24 LED@28 COMMS@29 LED@32 LED@36
  worker flush done at tick 19
```

모델의 `order` 줄과 **글자 하나까지 같다**: 19 틱에 LED 2번, COMMS 14 사라짐, 고치면 늦음 0. 틱 번호는 BASE(50) 기준 상대값이다 — 스케줄러 시작 직후엔 pthread 들이 처음 뜨면서 1 틱씩 늦게 기록되는 일이 있어서 50 틱 뒤에 setup 태스크가 타이머를 start 한다. 그래도 POSIX 포트는 진짜 OS 스레드라 가끔 흔들린다: 6 모드 × 30회 = 180회 중 **178회가 위와 같았고, 2회는 LED 콜백 하나가 +1 틱으로 기록** 됐다(prio_low 의 `9`, link 의 `33`). 그 바로 다음 LED 는 다시 제자리(12, 40)였다 — 기록이 늦었을 뿐 만료 격자(위상)는 그대로라는 것도 보여 준다. 실제 MCU 에서는 이런 호스트 지터가 없다. (180회 중 1회는 종료 순간 포트가 stderr 에 `vPortSystemTickHandler called from non-FreeRTOS thread` 를 한 줄 찍었다 — `exit()` 도중 틱 신호가 FreeRTOS 스레드가 아닌 곳에 간 것, `port.c:515`. 결과 출력에는 영향 없음.)

### Part B2 — 데몬 우선순위 vs 앱 태스크

ctrl(prio 3)이 10..24 틱 CPU 를 쓰고, 11 틱에 `xTimerStart(X, 0)` (X = one-shot 10) 직후 `xTimerIsTimerActive(X)` 를 본다. 15 틱에 RX reset 은 같다. FLUSH 는 없다.

```text
  -- Tmr prio 1 < ctrl prio 3 (ctrl 이 10..24 CPU 사용)
  Tmr  : R . . . R . . . R . . r r r r r r r r r r r r r r R . . R R . . R . . . R . . .
  ctrl : . . . . . . . . . . R R R R R R R R R R R R R R R . . . . . . . . . . . . . . .
  LED  : . . . . 1 . . . 1 . . . . . . . . . . . . . . . . 4 . . 1 . . . 1 . . . 1 . . .
  X    : . . . . . . . . . . . . . . . . . . . . . . . . . 1 . . . . . . . . . . . . . .
  COMMS: . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 1 . . . . . . . . . .
  order: LED@4 LED@8 X@25 LED@25 LED@25 LED@25 LED@25 LED@28 COMMS@29 LED@32 LED@36
  -- Tmr prio 6 > ctrl prio 3
  Tmr  : R . . . R . . . R . . R R . R R R . . . R R . . R . . . R R . . R . . . R . . .
  ctrl : . . . . . . . . . . R R R R R R R R R R R R R R R . . . . . . . . . . . . . . .
  LED  : . . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . . 1 . . .
  order: LED@4 LED@8 LED@12 COMMS@14 LED@16 LED@20 X@21 LED@24 LED@28 COMMS@29 LED@32 LED@36
```

진짜 FreeRTOS (`./demo_timers prio_low`, `prio_high`):

```text
mode=prio_low  timer task prio=1  (ticks < 40)
  LED   : 4 8 25 25 25 25 28 32 36
  COMMS : 29
  X     : 25
  order : LED@4 LED@8 X@25 LED@25 LED@25 LED@25 LED@25 LED@28 COMMS@29 LED@32 LED@36
  ctrl: xTimerStart(X,0) at tick 11 -> 1, xTimerIsTimerActive right after -> 0
mode=prio_high  timer task prio=6  (ticks < 40)
  LED   : 4 8 12 16 20 24 28 32 36
  COMMS : 14 29
  X     : 21
  order : LED@4 LED@8 LED@12 COMMS@14 LED@16 LED@20 X@21 LED@24 LED@28 COMMS@29 LED@32 LED@36
  ctrl: xTimerStart(X,0) at tick 11 -> 1, xTimerIsTimerActive right after -> 1
```

| 측정 | 데몬 prio 1 (< ctrl 3) | 데몬 prio 6 (> ctrl 3) |
|---|---|---|
| LED 콜백 틱 | 4 8 **25 25 25 25** 28 … | 4 8 12 16 20 24 28 … |
| LED 최대 늦음 | **13 틱** (12 → 25) | 0 |
| `xTimerStart(X)` 직후 `IsTimerActive` | **pdFALSE** (명령이 큐에만 있다) | pdTRUE (보내는 순간 데몬이 선점해 처리) |
| X (11 틱에 start, 주기 10) | 25 — 처리 순간 이미 21 이 지나 **즉시** 콜백 | 21 |
| COMMS 14 타임아웃 | 사라짐 (25 에 큐부터: X, 그 다음 15 의 RESET) | 14 에 불림 |
| ctrl 이 끝난 틱 | 25 | 25 (콜백이 µs 라 거의 안 뺏김) |

`order` 의 `X@25` 가 `LED@25` 들보다 **먼저** 인 것이 "깨어난 데몬은 큐부터" 의 증거다 — 모델과 FreeRTOS 가 같다. 실무 결론: 타이머가 "언제 불릴지" 를 약속하려면 데몬이 그 약속을 깰 수 있는 태스크들보다 높아야 한다. 그래서 흔히 데몬을 높게 두고, 대신 **콜백을 짧게** 유지한다(높은 데몬의 긴 콜백은 그대로 앱 태스크의 지터가 된다). 반대로 데몬을 낮게 두면 "앱이 바쁠 때는 타이머가 늦어도 된다" 를 받아들인 것이다. 그리고 데몬 우선순위와 상관없이 **보낸 틱 기준**(`timers.h:718-721`: "the timers expiry time is relative to when xTimerStart() is actually called")이라 X 는 21 을 기준으로 "늦게" 불린다 — 영원히 미뤄지지는 않는다.

### Part C — 통신 타임아웃 + LED 링크 표시

```c
static void comms_timeout_cb(swtimer_t *t) {          /* 데몬 문맥 */
    (void)t;
    g_link_up = false; g_link_downs++; g_link_down_tick = g_tick;
    if (!timer_change_period(&g_led, LED_FAST)) g_api_fail++;   /* 콜백 안: block time 0 */
}
static void link_up_pended(void *arg1, uint32_t rx_tick) {      /* pend 된 함수 = 데몬 문맥 */
    (void)arg1; (void)rx_tick;
    if (g_ctx != CTX_DAEMON) g_ctx_err++;
    if (g_link_up) return;                                      /* 두 번 와도 안전 */
    g_link_up = true; g_link_ups++; g_link_up_tick = g_tick;
    if (!timer_change_period(&g_led, LED_SLOW)) g_api_fail++;
}
static void uart_frame_isr(uint32_t now) {                      /* UART IDLE(프레임 끝) ISR */
    if (!timer_reset_from_isr(&g_comms)) g_api_fail++;
    if (!g_link_up && !pend_function_call_from_isr(link_up_pended, NULL, now)) g_api_fail++;
}
```

모델 출력과 FreeRTOS 출력 (`./demo_timers link`):

```text
  LED   : 8 16 24 32 40 48 57 59 61 63 65 67 69 71 73 75 77 79 88 96 104   (max late 0)
  COMMS : 55   (max late 0)
mode=link  timer task prio=6  (ticks < 112)
  LED   : 8 16 24 32 40 48 57 59 61 63 65 67 69 71 73 75 77 79 88 96 104
  COMMS : 55
  UP    : 80
  pended fn ran in task "Tmr Svc", pend ok=1, timer API fails=0
```

| 틱 | 사건 | 이유 |
|---|---|---|
| 5,15,25,35 | RX → COMMS reset (25, 35, 45, **55**) | one-shot 을 계속 뒤로 미는 "살아 있다" 신호 |
| 55 | COMMS 콜백: 링크 끊김, LED 주기 2 명령 → 같은 루프에서 처리 → **57** | CHANGE_PERIOD 는 처리한 순간 + 2 (56 의 원래 LED 는 취소) |
| 57..79 | LED 2 틱마다 (12번) | 빠른 깜빡임 = 사용자에게 "끊김" |
| 80 | RX: reset(→100) + `PendFunctionCallFromISR` → 데몬이 `link_up_pended` 실행 → LED 8 → **88** | 상태 변경은 데몬 한 곳에서 |
| 90, 100 | reset → 110, 120 | 112 전에 타임아웃 없음 |

`pended fn ran in task "Tmr Svc"` — `xTimerPendFunctionCallFromISR` 로 넘긴 함수가 정말 데몬 태스크에서 돌았다(`pcTaskGetName(NULL)`; 이름은 `configTIMER_SERVICE_TASK_NAME` 기본값, `timers.c:64-65`). 이 함수는 `tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR(-2)` 라는 **음수 ID 명령** 으로 같은 타이머 큐에 들어가(`timers.c:1250`, `timers.h:55`) 다른 명령과 FIFO 로 처리된다(모델 테스트 A5 의 `"ab"`). 덕분에 링크 상태를 ISR 과 데몬이 같이 쓰지 않으므로 임계 구역이 필요 없고, 태스크를 하나 더 만들지 않아도 된다. 대신 데몬과 같은 규칙(짧게, 막지 말 것)을 따른다.

**명령 큐 넘침** — RX ISR 이 바이트마다 `xTimerResetFromISR` 를 부르고 12바이트가 한 번에(FIFO 인터럽트 하나에) 온다:

```text
  per-byte reset x12 in one ISR (queue 10): ok=10 fail=2                                   (모델)
  12 x xTimerResetFromISR in one ISR: pdPASS=10  pdFAIL=2 (configTIMER_QUEUE_LENGTH=10)    (FreeRTOS burst)
```

ISR 이 도는 동안 데몬은 못 도니 큐 10칸이 차고 나머지 2번은 즉시 pdFAIL 이다(FromISR 는 기다리지 않는다: 매크로의 마지막 인자 `0U`, `timers.h:1115-1116`). 이 경우는 같은 틱의 reset 이라 버려져도 결과(COMMS 19)가 같지만, 그 사이 **다른 태스크의 STOP 이나 CHANGE_PERIOD 가 실패** 할 수 있다는 게 진짜 문제다. 고침: 프레임 끝(UART IDLE line) 에 한 번만 reset(`isr_burst_per_frame` → 실패 0), 반환값 확인, 큐 길이 = "데몬이 못 도는 최악 구간 동안 들어올 수 있는 명령 수" + 여유. 큐 메모리는 `configTIMER_QUEUE_LENGTH × sizeof(DaemonTaskMessage_t)` 바이트다(`timers.c:1135`).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 35/35 passed (5회 같은 출력)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 35/35, 보고 없음
```

스레드를 쓰지 않으므로 TSan 은 해당 없다. 실행 시간은 수 ms.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 콜백 안에서 `vTaskDelay`, `xSemaphoreTake(m, 100)`, 느린 I2C 폴링 | 데몬이 막혀 **모든** 타이머가 밀린다 (LED 7 틱, COMMS 타임아웃 소실) | 콜백은 `xSemaphoreGive`/`xTaskNotifyGive` 만, 일은 worker 태스크 |
| 콜백 안에서 `xTimerStop(t, portMAX_DELAY)` | 큐가 꽉 차면 데몬이 자기 큐가 비기를 기다린다 = 교착 (`timers.h:178-179` 경고) | 콜백 안 타이머 API 는 block time **0**, 반환값 확인 |
| auto-reload 를 직접 구현하며 `expiry = now + period` | 늦을 때마다 위상이 밀린다 | `expiry = 이전 expiry + period` (+ 따라잡기 또는 건너뛰기를 의식적으로 선택) |
| `xTimerStart` 직후 `xTimerIsTimerActive` 로 확인 | 데몬이 낮으면 아직 pdFALSE (실측) | 상태가 필요하면 자기 플래그, 아니면 데몬을 높게 |
| `xTimerReset` 반환값 무시 | 큐 넘침(pdFAIL)이 조용히 일어난다 (12바이트에 2번 실패) | `if (xTimerResetFromISR(t, &w) != pdPASS) g_tmr_fail++;` |
| UART 바이트마다 `xTimerResetFromISR` | 명령 큐를 버스트로 채운다 | 프레임 끝(IDLE) 또는 DMA 반쪽/완료에서 1번 |
| 데몬 우선순위를 "제일 낮게, 덜 중요하니까" | 바쁜 태스크가 있으면 타임아웃·LED 가 그만큼 늦고 콜백이 몰린다 (13 틱) | 데몬을 높게 + 콜백 짧게, 또는 늦음을 명시적으로 허용 |
| 잠든 타이머에 "주기만 바꾸려고" `xTimerChangePeriod` | 타이머가 시작된다 (`timers.c:1038`) → 원치 않는 콜백 (연습 3b) | 바꾼 직후 `xTimerStop`, 또는 시작할 때 주기 설정 |
| ISR 과 콜백이 같은 상태 변수를 쓴다 | 경쟁 조건 — 콜백 도중 ISR 이 끼어든다 | `xTimerPendFunctionCallFromISR` 로 변경을 데몬에 모은다 |
| 워치독을 타이머 콜백에서 무조건 킥 | 데몬만 살아 있으면 킥된다 — 앱 태스크가 데드락이어도 리셋 안 됨 (연습 4) | 태스크 체크인 비트가 모두 모였을 때만 킥 |

## 8. 면접 Q&A

<details>
<summary>Q1. FreeRTOS 소프트웨어 타이머 콜백은 어디서 실행되나요? 그래서 무엇을 조심하나요?</summary>

ISR 이 아니라 **타이머 서비스 태스크("Tmr Svc", daemon)** 안에서 순서대로 불린다. 스케줄러가 시작될 때 `xTimerCreateTimerTask()` 가 만든다(`tasks.c:3758-3769`, `timers.c:237-330`). 그래서 ① FromISR 아닌 API 를 쓸 수 있지만 ② **막히면 안 되고**(다른 타이머가 전부 밀린다), ③ 스택은 `configTIMER_TASK_STACK_DEPTH`(콜백에서 큰 지역 배열·printf 금지), ④ 실행 시점은 데몬 우선순위에 달렸다. 꼬리: "Zephyr 는?" → `k_timer` 의 만료 함수는 시스템 클럭 인터럽트 문맥이라 아예 블록할 수 없고, 스레드 문맥이 필요하면 `k_work_delayable` 을 쓴다고 문서에 있다 (미검증).

</details>

<details>
<summary>Q2. 콜백 안에서 플래시에 로그를 쓰느라 10 ms 기다리면 정확히 무슨 일이 생기나요?</summary>

그 10 틱 동안 데몬이 Blocked 라 리스트의 만료도, 큐의 명령도 처리되지 않는다. 실측(모델 = FreeRTOS): 주기 4 LED 의 12, 16 만료가 19 틱에 **한꺼번에 2번** 불렸고(최대 7 틱 늦음), 14 틱 통신 타임아웃은 15 틱의 늦은 RX reset 이 먼저 처리되는 바람에 **아예 불리지 않았다** — 장애 보고가 사라진 것. 고침: 콜백은 세마포어/알림만 주고 worker 태스크가 쓰기를 한다 → LED 늦음 0, 타임아웃 14 에 정상 보고. 콜백 안에서 타이머 API 를 block time 을 주고 부르는 것도 같은 이유로 금지(자기 큐를 기다리는 교착, `timers.h:178-179`).

</details>

<details>
<summary>Q3. configTIMER_TASK_PRIORITY 는 어떻게 정하나요?</summary>

"타이머가 늦어도 되는 태스크" 보다는 높게, "타이머 콜백에 방해받으면 안 되는 태스크" 보다는 낮게. 실측: CPU 를 15 틱 쓰는 prio 3 태스크 아래에 데몬(prio 1)을 두면 LED 콜백 4번이 25 틱에 몰리고(최대 13 틱 늦음) 14 틱 타임아웃이 사라졌다. 데몬을 6 으로 올리면 전부 제때. 그래서 흔히 높게(템플릿은 `configMAX_PRIORITIES - 1`) 두고 콜백을 µs 단위로 유지한다 — 높은 데몬의 콜백 시간은 곧 앱 태스크의 지터다. 꼬리: "모터 제어 루프(1 kHz)보다 높게 둬도 되나?" → 콜백이 수 µs 이고 개수가 적으면 괜찮지만, 제어 루프 지터 예산을 넘는 콜백이 하나라도 있으면 데몬을 루프 아래로 내리거나 그 일을 다른 태스크로 뺀다.

</details>

<details>
<summary>Q4. 태스크가 xTimerStart(t, 0) 를 부른 직후 xTimerIsTimerActive(t) 는 무엇을 돌려주나요?</summary>

데몬 우선순위에 따라 다르다. start 는 명령을 큐에 넣을 뿐이고 `ACTIVE` 비트는 데몬이 명령을 처리할 때 켠다(`timers.c:1004`, 검사는 1179). 부른 태스크보다 데몬이 높으면 큐 send 가 데몬을 깨워 그 자리에서 선점하므로 돌아왔을 때 pdTRUE, 낮으면 pdFALSE — 실측 `-> 0` / `-> 1`. 반대로 만료 시각은 처리 시점이 아니라 **부른 시점** + 주기다(명령에 `xTaskGetTickCount()` 가 실림, `timers.h:507-508`). 그래서 11 틱에 start 한 주기 10 타이머는 데몬이 25 틱에 처리해도 21 기준으로 "즉시" 불린다.

</details>

<details>
<summary>Q5. 통신 타임아웃을 소프트웨어 타이머로 어떻게 만드나요? RX 마다 reset 하면 문제는 없나요?</summary>

one-shot(예: 5 s) 을 만들어 두고 **유효 프레임을 받을 때마다 `xTimerResetFromISR`**. 프레임이 끊기면 마지막 reset + 주기 뒤에 콜백 → 링크 끊김 처리(LED 빠르게, 재연결 시도). 실측: 마지막 프레임 35 + 20 = 55 에 정확히 불렸다. 주의: ① 바이트마다 reset 하면 버스트에 명령 큐가 넘친다(12바이트 → pdFAIL 2) — 프레임 끝(UART IDLE line, DMA 완료)에 1번, ② 반환값 확인, ③ 데몬이 늦으면 타임아웃 자체가 늦거나 사라질 수 있다(Q2) — 안전 관련 감시라면 데몬을 높게 두거나 하드웨어 타이머/워치독으로 이중화. 꼬리: "reset 대신 마지막 RX 틱을 저장하고 주기적으로 비교하면?" → 그것도 맞는 설계다(명령 큐를 안 씀, ISR 은 변수 하나만 쓴다). 대신 검사 주기만큼 감지가 늦는다.

</details>

<details>
<summary>Q6. auto-reload 타이머가 늦게 처리되면 다음 만료는 언제인가요? vTaskDelayUntil 과 비교하면?</summary>

FreeRTOS 는 위상을 지킨다: 다음 만료 = 이전 만료 + 주기. 이미 지난 만료가 여럿이면 `prvReloadTimer`(`timers.c:697-713`) 가 지난 만료마다 콜백을 한 번씩 **연달아** 부른다(실측: 13 틱 늦으면 한 틱에 4번). 장점: 횟수와 위상이 맞다(LED 토글 위상 유지, "초 카운터" 같은 누적값이 정확). 단점: 몰림 — 하트비트 송신이라면 패킷 4개가 붙어서 나간다. 필요하면 콜백이 `xTaskGetTickCount()` 를 보고 늦은 것은 버리면 된다. `vTaskDelayUntil` 도 기준 시각을 주기만큼 더해 위상을 지키는 점은 같지만, 그건 **태스크 하나** 가 자기 루프를 돌리는 것이고(늦으면 다음 반복이 바로 이어 실행됨, RTOS-B06), 타이머는 데몬 하나가 여러 주기 일을 나눠 맡는 것이다.

</details>

<details>
<summary>Q7. 하드웨어 타이머가 있는데 왜 소프트웨어 타이머를 쓰나요? xTimerPendFunctionCall 은 언제 씁니까?</summary>

하드웨어 TIMx 는 수가 적고 µs 해상도·지터 없는 일(PWM, ADC 트리거, 입력 캡처)에 써야 한다. 소프트웨어 타이머는 해상도가 틱(보통 1 ms)이고 지터가 있지만 RAM 몇십 바이트로 수십 개를 만들 수 있어 LED, 타임아웃, 디바운스, 주기 로그 같은 ms~s 단위 관리 작업에 맞다. 기준: "1 틱 늦어도 괜찮나?" 아니면 하드웨어. `xTimerPendFunctionCallFromISR` 는 ISR 의 뒷일을 **데몬에게** 미루는 방법이다 — 전용 지연 처리 태스크를 만들지 않아도 되고, 같은 큐에서 타이머 명령과 순서가 지켜진다(Part C: 링크 복구 함수가 "Tmr Svc" 에서 실행). 단 데몬 하나를 공유하므로 무거운 일엔 전용 태스크 + 태스크 알림(RTOS-M01, RTOS-M02)이 낫다.

</details>

## 9. 추가 연습

### 연습 1 — 틱 랩어라운드

틱이 `0xFFFFFFF0` 에서 시작한다. LED(auto 4), A(one-shot 10 → `0xFFFFFFFA`), B(one-shot 30 → `0x0000000E`) 를 B, A, LED 순으로 start 하면 리스트 순서는? 콜백 틱은?

<details>
<summary>답 보기</summary>

순서는 LED(`…F4`) → A(`…FA`) → B(`0x0E`). 숫자로는 B 가 가장 작지만 `before()` 는 `(int32_t)(a - b) < 0` 이라 `0x0E - 0xFFFFFFFA = 0x14 > 0` → B 가 더 늦다. LED 는 `F4 F8 FC 0 4 8 12 16 20` 에 늦음 0, A 는 `FA`, B 는 `14`. `expiry < now` 같은 부호 없는 비교였다면 0 을 넘는 순간 순서가 뒤집힌다. FreeRTOS 는 대신 리스트 두 개를 두고 틱이 넘칠 때 바꾼다(`prvSampleTimeNow` 가 `xTimeNow < xLastTime` 을 보고 `prvSwitchTimerLists`, `timers.c:869-890`) — 같은 문제의 다른 답이다(RTOS-B06).

</details>

### 연습 2 — 따라잡기 vs 건너뛰기: LED 토글 위상

Part B 의 막힘(9..18) 상황에서 LED 콜백이 GPIO 를 토글한다. 20 틱까지 토글 횟수와 LED 상태를, FreeRTOS 식 따라잡기와 "지난 만료는 콜백 1번으로 합치는" 건너뛰기로 비교하라.

<details>
<summary>답 보기</summary>

막힘이 없었다면 4, 8, 12, 16, 20 → 5번(상태 1). 따라잡기: 4, 8, 19, 19, 20 → **5번, 상태 1** (위상 유지). 건너뛰기: 4, 8, 19, 20 → **4번, 상태 0** — 이후 LED 가 원래와 반대 위상으로 계속 깜빡인다. practice 출력 `toggles by tick 20: catch-up 5, skip 4, ideal 5`. 건너뛰기 구현은 "다음 만료 = now 보다 뒤인 첫 격자점, 콜백 1번" (`g_skip_mode`). 어느 쪽이 맞는지는 콜백의 의미에 달렸다: 횟수/위상이 중요하면 따라잡기, "최신 상태로 한 번만" 이면 건너뛰기.

</details>

### 연습 3 — 버튼 디바운스 (one-shot + 에지마다 reset)

EXTI ISR 이 에지마다 `timer_reset_from_isr(&deb)` (one-shot 5). 에지가 10, 11, 13, 14 (채터링), 40 에 온다. 콜백(= "눌림 확정")은 언제? 그리고 3b: 60 틱에 잠든 `deb` 에 `timer_change_period(&deb, 3)` 을 부르면?

<details>
<summary>답 보기</summary>

마지막 에지 + 5 → **19** 와 **45**, 눌림 2번. 채터링 동안은 reset 이 만료를 계속 미루므로 한 번만 확정된다. 3b: CHANGE_PERIOD 는 ACTIVE 를 켜므로 타이머가 **시작** 되어 63 에 "눌림" 이 1번 더 생긴다 (`g_presses == 3`). FreeRTOS 도 같다(`timers.c:1038`). 주기만 바꾸고 싶었다면 바로 `xTimerStop`. 실무 메모: 에지마다 ISR → 명령 1개이므로 채터링이 심하면 연습 C 의 큐 넘침과 같은 문제가 생긴다. EXTI 를 첫 에지에서 끄고 타이머 콜백에서 다시 켜는 방법도 흔하다.

</details>

### 연습 4 — 워치독 킥을 타이머 콜백에서?

KICK 타이머(auto 10)가 IWDG 를 킥한다(타임아웃 25). 태스크 A 는 5 틱마다 체크인, 태스크 B 는 25 틱 뒤 데드락으로 멈춘다. ① 콜백이 무조건 킥, ② 체크인 비트가 모두 모였을 때만 킥 — 리셋은 언제?

<details>
<summary>답 보기</summary>

① 킥 7번, 리셋 **안 됨** — 데몬만 살아 있으면 킥되므로 B 의 데드락을 워치독이 못 잡는다. ② 10, 20, 30 에 킥(30 틱 콜백 시점엔 B 의 25 틱 체크인이 남아 있음), 40 에는 B 비트가 없어 킥 안 함 → 30 + 25 = **55 에 리셋**. practice 출력 `checkin: kicks=3 last_kick=30 reset_tick=55`. 이것이 "태스크 체크인 워치독" 패턴이고, 데몬 자체가 멈추면 킥도 멈추므로 데몬도 함께 감시된다. 하드웨어 쪽: STM32 IWDG 는 `IWDG->KR = 0xAAAA` 로 킥, 한번 켜면(0xCCCC) 소프트웨어로 끌 수 없다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 13/13 passed
```

## 10. 핵심 정리

- FreeRTOS 소프트웨어 타이머 콜백은 ISR 이 아니라 **"Tmr Svc" 데몬 태스크** 에서 차례로 돈다. 우선순위 `configTIMER_TASK_PRIORITY`, 명령 큐 `configTIMER_QUEUE_LENGTH`, 스택 `configTIMER_TASK_STACK_DEPTH`.
- 타이머 API 는 **명령을 큐에 넣을 뿐**. `ACTIVE` 는 데몬이 처리할 때 켜진다(직후 `IsTimerActive` 가 pdFALSE 일 수 있음). START/RESET 기준은 **부른 틱**, CHANGE_PERIOD 기준은 **처리한 틱**, 잠든 타이머도 change_period 하면 시작된다.
- auto-reload 다음 만료 = **이전 만료 + 주기**(위상 유지). 늦으면 지난 만료마다 콜백을 **몰아서** 부른다(실측 한 틱 4번). one-shot 은 1번 후 비활성.
- 콜백이 막히면 **모든 타이머가 밀린다**: 실측 LED 7 틱 늦음, 통신 타임아웃 1번 소실. 콜백은 give/notify 만, 일은 worker. 콜백 안 타이머 API 는 block time 0.
- 데몬이 앱 태스크보다 낮으면 그 태스크가 바쁜 동안 타이머가 멈춘다(실측 13 틱). 보통 높게 + 콜백 짧게.
- 통신 타임아웃 = one-shot + 프레임마다 `xTimerResetFromISR`(바이트마다 X — 큐 넘침 pdFAIL), 상태 변경은 `xTimerPendFunctionCallFromISR` 로 데몬 한 곳에서.
- 모델과 FreeRTOS V11.3.2 POSIX 포트가 모든 시나리오에서 같은 콜백 틱·순서를 냈다(180회 중 178회 동일, 2회는 호스트 지터로 기록 +1 틱).

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (`~/.esn_scratch/FreeRTOS-Kernel`, <https://github.com/FreeRTOS/FreeRTOS-Kernel>): `timers.c` (서비스 태스크 이름 64-65, `xTimerCreateTimerTask` 237-330, `xTimerGenericCommandFromTask` 448-495, `prvReloadTimer` 697-713, `prvProcessExpiredTimer` 716-743, `prvTimerTask` 746-777, `prvProcessTimerOrBlockTask` 780-835, `prvSampleTimeNow` 869-890, `prvInsertTimerInActiveList` 892-934, `prvProcessReceivedCommands` 936-1080, `prvSwitchTimerLists` 1091, 큐 생성 1135-1141, `xTimerIsTimerActive` 1167-1190, `xTimerPendFunctionCall(FromISR)` 1236-1298), `include/timers.h` (명령 ID 55-68, 콜백 안 block time 경고 178-179, `xTimerStart` 507-508, `xTimerReset` 설명 686-722·매크로 803-804, `xTimerResetFromISR` 1115-1116, `xTimerPendFunctionCall` 1215-1245), `tasks.c` (데몬 생성 3758-3769, 틱 훅 5000-5013), `list.c` 192, `include/FreeRTOS.h` 532-542
- FreeRTOS 문서 — Software Timers / Timer service daemon task / `xTimerPendFunctionCall` — <https://www.freertos.org/RTOS-software-timer.html>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Software Timer Management 장 — <https://www.freertos.org/Documentation/RTOS_book.html>
- Zephyr — Timers (`k_timer`, 만료 함수 문맥) / Workqueue (`k_work_delayable`) — <https://docs.zephyrproject.org/latest/kernel/services/timing/timers.html>
- CMSIS-RTOS2 — Timer Management (`osTimerNew`, `osTimerStart`) — <https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS__TimerMgmt.html>
- STM32F4 Reference Manual (RM0090) — IWDG (키 레지스터 0xAAAA/0xCCCC), USART IDLE line 검출 — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armcortexm4-based-32bit-mcus-stmicroelectronics.pdf>
- 관련 레슨: RTOS-B06 (Delay vs DelayUntil, 틱 랩), RTOS-B08 (ISR → 태스크, 틱 훅을 ISR 로 쓰는 법), RTOS-B11 (큐: 명령 큐도 복사 큐), RTOS-M01 (태스크 알림 — worker 를 깨우는 가벼운 방법), RTOS-M02 (ISR 지연 처리), C-M14 (ISR 공유 데이터)

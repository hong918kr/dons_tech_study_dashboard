---
id: RTOS-M03
time: 85분
updated: 2026-11-08
---
# RTOS-M03 우선순위 역전 재현: Mars Pathfinder 시나리오

## 학습 목표

- tick 구동 미니 커널(우선순위 선점 스케줄러 + 소유자 없는 일반 세마포어)을 직접 만들고, H/M/L 세 태스크로 **unbounded priority inversion(무한정 우선순위 역전)** 을 tick 단위 타임라인으로 재현한다.
- H 의 대기를 "자원 보유자 L 때문(피할 수 없음)" 과 "R 과 무관한 M 때문(역전)" 으로 **분해** 하고, 대기 = L 의 남은 임계 구역 + C_M 으로 M 의 길이에 **정비례** 함을 표와 그래프로 보인다.
- Mars Pathfinder(1997) 사고를 30초 안에 설명한다: bc_dist / ASI/MET / 중간 우선순위 태스크, select() 의 뮤텍스, 주기 마감 검사 → 리셋, 우선순위 상속 플래그를 켜는 패치.
- "일반 세마포어로는 왜 커널이 도와줄 수 없나 (소유자가 없다)" 를 말하고, 해결책(우선순위 상속 RTOS-M04, 천장 프로토콜 RTOS-M05, 스케줄러 잠금)을 **미리보기** 수준에서 비교한다.
- 모델의 각 호출을 FreeRTOS / Zephyr / VxWorks / CMSIS-RTOS2 의 실제 API 로 옮길 수 있다.

## 1. 문제

"우선순위 역전(priority inversion)이 뭔지, 실제 사례와 해결책을 말해 보세요" 는 RTOS 면접의 단골 질문이고, 정답의 핵심은 **"높은 우선순위 태스크가 기다리는 시간에 상한이 없어진다"** 는 한 문장이다. 펌웨어에서 그대로 생긴다: STM32F4 비행 제어기에서 1 kHz 자세 제어 태스크(H)와 저우선 로거 태스크(L)가 IMU 와 SPI 플래시가 붙은 SPI 버스를 세마포어 하나로 나눠 쓴다. L 이 플래시 페이지를 쓰려고 버스를 잡은 순간 무선 수신 ISR 이 텔레메트리 태스크(M)를 깨우고, M 이 압축·암호화로 수 ms 를 쓰면 — H 는 M 보다 우선순위가 높은데도 **M 이 끝날 때까지** 자세 제어를 못 한다.

이 레슨은 이것을 **결정적인(deterministic) 시뮬레이션 커널** 에서 tick 단위로 재현한다. 진짜 스레드가 아니라 OS 스레드 하나에서 도는 모델이라 매번 같은 타임라인이 나오고, 테스트는 벽시계 시간이 아니라 **타임라인 상태** 를 검사한다. (RTOS-B01~B07 은 아직 없으므로 커널은 이 레슨 안에 전부 들어 있다.)

```svg
<svg viewBox="0 0 740 276" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">정준 시나리오, 일반 세마포어, C_M=4 — solution.c 가 찍은 타임라인 그대로</text>
  <text x="121" y="42" fill="currentColor" font-size="12">0</text> <text x="171" y="42" fill="currentColor" font-size="12">1</text> <text x="221" y="42" fill="currentColor" font-size="12">2</text> <text x="271" y="42" fill="currentColor" font-size="12">3</text>
  <text x="321" y="42" fill="currentColor" font-size="12">4</text> <text x="371" y="42" fill="currentColor" font-size="12">5</text> <text x="421" y="42" fill="currentColor" font-size="12">6</text> <text x="471" y="42" fill="currentColor" font-size="12">7</text>
  <text x="521" y="42" fill="currentColor" font-size="12">8</text> <text x="571" y="42" fill="currentColor" font-size="12">9</text> <text x="621" y="42" fill="currentColor" font-size="12">10</text> <text x="671" y="42" fill="currentColor" font-size="12">11</text>
  <text x="10" y="74" fill="currentColor">H (3)</text>
  <rect x="201" y="56" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="221" y="74" fill="currentColor">B</text> <rect x="251" y="56" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="271" y="74" fill="currentColor">B</text>
  <rect x="301" y="56" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="321" y="74" fill="currentColor">B</text> <rect x="351" y="56" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="371" y="74" fill="currentColor">B</text>
  <rect x="401" y="56" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="421" y="74" fill="currentColor">B</text> <rect x="451" y="56" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="471" y="74" fill="currentColor">B</text>
  <rect x="501" y="56" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="521" y="74" fill="currentColor">B</text> <rect x="551" y="56" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="601" y="56" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <text x="10" y="114" fill="currentColor">M (2)</text>
  <rect x="251" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="301" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="351" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="401" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <text x="10" y="154" fill="currentColor">L (1)</text>
  <rect x="101" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="151" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="201" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="251" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="272" y="154" fill="currentColor">r</text> <rect x="301" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="322" y="154" fill="currentColor">r</text> <rect x="351" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="372" y="154" fill="currentColor">r</text> <rect x="401" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="422" y="154" fill="currentColor">r</text> <rect x="451" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="501" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="551" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="572" y="154" fill="currentColor">r</text> <rect x="601" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="622" y="154" fill="currentColor">r</text> <rect x="651" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <line x1="150" y1="46" x2="150" y2="170" stroke="currentColor" stroke-dasharray="2 3"/> <line x1="550" y1="46" x2="550" y2="170" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="10" y="190" fill="currentColor">채움 = 실행(R), B = R 대기, 점선 r = 준비됐지만 못 돎. 세로 점선: t=1 L 이 R 획득, t=9 H 가 R 받음.</text>
  <text x="10" y="212" fill="currentColor">t=2 H 가 R 요청 → B.  t=3~6 M 이 L 을 선점 → R 을 쥔 L 이 못 도니 H 도 못 돈다.</text>
  <text x="10" y="234" fill="currentColor">H 대기 7 = L 의 남은 CS 3 + C_M 4.  M 이 길수록 H 도 길게 기다린다 = unbounded 역전.</text>
</svg>
```

**커널 모델 (모든 Part 공통, RTOS-M04/M05 와 같은 규칙)**

| 규칙 | 이 모델 | 실제 RTOS 와의 관계 |
|---|---|---|
| 시간 | 1 tick = `WORK` 1단위. `TAKE`/`GIVE`/`PRIO` 커널 호출은 시간 0 | 실제로 커널 호출은 µs, tick 은 1 ms 정도 |
| 스케줄링 | 매 tick, READY 중 **우선순위가 가장 높은** 태스크를 돌린다 (숫자가 클수록 높음) | FreeRTOS 의 고정 우선순위 선점 스케줄러 |
| 같은 우선순위 | READY 가 된 순서(FIFO). 시분할 없음 | `configUSE_TIME_SLICING 0` 상당 |
| 막힘 | `TAKE` 실패 → BLOCKED, 같은 tick 에 다음 태스크를 다시 고른다 | 블록 즉시 컨텍스트 스위치 |
| 세마포어 | 카운트 + 우선순위순 대기 리스트, **소유자 없음**. `GIVE` 는 누구나(ISR 포함), 절대 안 막힘 | `xSemaphoreCreateBinary`, `k_sem`, `semBCreate` |
| 깨우기 | 대기자가 있으면 가장 높은 대기자에게 **직접 넘김** (TAKE 완료 처리) | FreeRTOS 는 깨운 뒤 재시도 — 이 시나리오에선 결과 같음 (5. 풀이) |
| ISR | 하네스가 매 tick 시작에 부르는 함수 `g_isr(tick)` | `xSemaphoreGiveFromISR` + `portYIELD_FROM_ISR` |
| 타임라인 | `R` 실행 · `B` 세마포어 대기 · `r` 준비됐지만 못 돎 · `.` 없음/끝남 | 트레이서(Tracealyzer, SystemView)의 태스크 상태 줄 |

**정준 역전 시나리오** (이 레슨이 정의하고 RTOS-M04/M05 가 같은 숫자로 재사용한다)

| 태스크 | 우선순위 | 릴리스 | 스크립트 |
|---|---|---|---|
| H | 3 | t=2 | 첫 단계에서 `TAKE R` → `WORK 2` → `GIVE R` |
| M | 2 | t=3 | `WORK C_M` (R 을 전혀 안 씀) |
| L | 1 | t=0 | `WORK 1` → (t=1 시각) `TAKE R` → `WORK 4` → `GIVE R` → `WORK 1` |

C_M ∈ {0, 2, 4, 8, 16} 에 대해 H 의 대기 시간을 잰다. 이 모델의 결과: **H 대기 = 3 + C_M** — L 의 임계 구역(CS, critical section) 4 tick 중 H 가 도착한 t=2 에 이미 1 tick 이 끝나 남은 것이 3 이기 때문이다. (커널 호출을 1 tick 으로 세는 모델이면 상수가 4 로 한 칸 밀린다. 비례 관계 "기울기 1" 은 모델과 무관하다.)

### Part A — 미니 커널: 스케줄러 · 일반 세마포어 · 디스패치

- `int pick_next(void)` — READY 중 최고 우선순위, 같으면 먼저 READY 된 태스크. 없으면 -1 (idle).
- `bool ksem_take(int i, int s)` / `int ksem_give(int s)` — 카운트가 있으면 줄이고 성공, 없으면 BLOCKED + 대기 리스트. give 는 대기자 중 (우선순위, 대기 순서) 가 가장 앞선 태스크에게 직접 넘기고 그 번호를 반환, 없으면 `count++` (max 에서 포화).
- `run_t run_ops(int i, bool may_preempt, int now)` — 시간 0 명령을 `WORK` 를 만날 때까지 실행. `GIVE`/`PRIO` 직후 더 높은 태스크가 생기면 선점.
- `int dispatch(void)` — 한 tick: 고르고, 막히면 다시 고르고, `WORK` 1단위를 소비.
- 테스트: 정준 시나리오 C_M=0, 4 의 타임라인이 **글자 하나까지** 위 그림과 같아야 한다.

### Part B — 역전의 크기를 재고, 분해한다

- `hstats_t h_stats(int h, int holder)` — 타임라인에서 H 가 기다린 tick(`B`/`r`)을 세고, 그 tick 에 돈 태스크로 분해: `by_holder`(L), `by_lower`(H 보다 낮은 다른 태스크 = **역전**), `by_higher`(정상 선점), `idle`.
- C_M ∈ {0,2,4,8,16} 에 대해 `wait == 3 + C_M`, `by_holder == 3`, `by_lower == C_M` 를 확인하고 기울기 1 을 검사한다.

### Part C — Pathfinder-lite: 워치독 리셋과 해결 미리보기

- H=bc_dist (t=2 에 버스 주기 시작, 다음 주기 t=10 전에 끝나야 함), M=통신 태스크 (이벤트 세마포어 EV 를 기다리다 **무선 ISR** 이 t=3 에 깨움), L=ASI/MET.
- `void radio_isr(int tick)` — t=3 에 `EV` 를 give (ISR 은 절대 막히지 않는다, RTOS-B08).
- `int watchdog_check(int h, int deadline)` — bc_sched 의 검사: 마감까지 못 끝났으면 리셋 시각을 반환.
- `int max_safe_cm(int deadline, bool boost)` — 리셋이 안 나는 최대 C_M 을 커널을 돌려서 찾는다.
- 미리보기: L 이 R 을 쥐는 동안 **손으로** 우선순위를 3 으로 올리면(`vTaskPrioritySet` 상당, 커널 프로토콜 아님) 모든 C_M 에서 H 대기 3, 리셋 없음. 상속/천장 프로토콜 자체의 구현은 RTOS-M04/M05 의 몫이다.

## 2. Problem (English)

> **Reproduce unbounded priority inversion in a tick-driven mini RTOS.**
>
> Implement a deterministic single-threaded kernel. Tasks run scripts of `WORK n` (n ticks of CPU), `TAKE s`, `GIVE s`, `PRIO p`, `END`; kernel calls take zero time. Every tick the highest-priority READY task runs one unit of work (FIFO among equal priorities, no time slicing). A plain semaphore has a count and a priority-ordered wait list but **no owner**; `GIVE` never blocks and may be called from an ISR hook.
>
> 1. `int pick_next(void)`, `bool ksem_take(int task, int sem)`, `int ksem_give(int sem)`, `run_t run_ops(int task, bool may_preempt, int now)`, `int dispatch(void)`.
> 2. `hstats_t h_stats(int h, int holder)` — split H's waiting ticks into "holder ran", "lower-priority non-holder ran" (inversion), "higher ran", "idle".
> 3. `void radio_isr(int tick)`, `int watchdog_check(int h, int deadline)`, `int max_safe_cm(int deadline, bool boost)`.
>
> Scenario: H=3 released t=2, takes R first, works 2; M=2 released t=3, works C_M without R; L=1 released t=0, takes R at t=1, works 4 ticks holding R. Expected (C_M=4): `H ..BBBBBBBRR.`, `M ...RRRR.....`, `L RRRrrrrRRrrR`; H waits 3 + C_M ticks for C_M ∈ {0,2,4,8,16}. With a deadline at t=10, the largest safe C_M is 3.
>
> Follow-ups: Why can't the kernel apply priority inheritance to a binary semaphore? What exactly happened on Mars Pathfinder? Is the inversion of 3 ticks caused by L itself avoidable? Compare inheritance, ceiling and scheduler locking.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/49 passed" (모두 idle 인 64-tick 타임라인이 찍힌다) — TODO 9개를 채워 49/49 를 만든다
```

스레드도 시그널도 없는 순수 C11 프로그램이다. 하네스(`run_tick`, `print_timeline`, 시나리오 빌더)는 주어지고, 커널 핵심 5개(Part A), 통계 1개(Part B), Pathfinder 3개(Part C)가 TODO 다. 스텁이 -1/0 을 돌려도 `run_until_done()` 은 `MAX_TICKS`(64)에서 멈추므로 starter 는 멈추지(hang) 않고 FAIL 로 끝난다.

관찰용 파일 하나 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_posix_protocol.c -o dpp && ./dpp   # 맥 pthread 뮤텍스의 우선순위 프로토콜 지원 확인
```

## 4. 힌트

<details>
<summary>힌트 1 — pick_next / 대기 리스트: "더 높거나, 같으면 더 먼저"</summary>

비교 하나로 끝난다: `t->prio > best->prio || (t->prio == best->prio && t->ready_seq < best->ready_seq)`. 대기 리스트도 같은 모양인데 `ready_seq` 대신 `wait_seq` 를 쓴다. 우선순위는 **지금** 값(`prio`)으로 비교한다 — Part C 에서 L 이 스스로 우선순위를 바꾸기 때문이다.

</details>

<details>
<summary>힌트 2 — ksem_give 는 "넘겨주기" 다</summary>

대기자가 있으면 카운트를 올리지 말고 그 태스크의 `pc++` (기다리던 TAKE 가 성공한 것으로) + `make_ready()`. 대기자가 없을 때만 `count++`, 단 `max` 를 넘지 않게. 소유자 검사는 하지 않는다 — 일반 세마포어는 누가 잡았는지 **모른다**. 이게 이 레슨 전체의 복선이다.

</details>

<details>
<summary>힌트 3 — run_ops 와 dispatch 의 분업</summary>

`run_ops` 는 `for` 루프 + `switch`. `WORK` 이면 바로 `RUN_WORK` (arg 0 이면 건너뛰기), `TAKE` 실패면 `RUN_BLOCKED`, `END` 면 `finish = now` 후 `RUN_DONE`. `GIVE`/`PRIO` 뒤에는 `may_preempt && pick_next() != i` 로 선점 검사. `dispatch` 는 `pick_next → run_ops` 를 `RUN_WORK` 가 나올 때까지 반복(상한 있는 루프)하고, `left` 가 0 이면 `ops[pc].arg` 로 시작해 하나 줄인다. 0 이 되면 `pc++` — 그 뒤의 `GIVE` 는 주어진 `run_tick` 이 tick 끝에 실행해 준다.

</details>

<details>
<summary>힌트 4 — h_stats: 문자 하나와 g_run 하나</summary>

`g_row[h][t]` 가 `'B'` 또는 `'r'` 이면 대기 tick. 그 tick 에 돈 태스크는 `g_run[t]`. holder 인지, `base_prio` 가 H 이상인지로 나눈다 (부스트된 지금 우선순위가 아니라 **설계** 우선순위로 분류해야 Part C 미리보기에서도 의미가 같다).

</details>

<details>
<summary>힌트 5 — Part C: 마감 검사는 "끝난 시각 ≤ 마감"</summary>

`finish` 는 "마지막 WORK tick + 1" 이므로 `finish <= deadline` 이면 다음 주기 시작 전에 끝난 것이다. `max_safe_cm` 은 C_M 을 0 부터 올리며 처음 리셋이 나는 값 - 1 을 돌려준다 (대기가 C_M 에 단조 증가하므로 첫 실패만 찾으면 된다).

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨 | FreeRTOS (V11.3.2) | Zephyr | VxWorks (문서 기준) | CMSIS-RTOS2 |
|---|---|---|---|---|
| 일반 세마포어 `ksem_t` (소유자 없음) | `xSemaphoreCreateBinary()` / `…Counting()` | `k_sem` | `semBCreate` / `semCCreate` | `osSemaphoreNew` |
| `ksem_take(i, s)` | `xSemaphoreTake(h, portMAX_DELAY)` | `k_sem_take(&s, K_FOREVER)` | `semTake(s, WAIT_FOREVER)` | `osSemaphoreAcquire(s, osWaitForever)` |
| `ksem_give(s)` (태스크) | `xSemaphoreGive(h)` | `k_sem_give(&s)` | `semGive(s)` | `osSemaphoreRelease(s)` |
| `radio_isr` 의 give | `xSemaphoreGiveFromISR(h, &woken); portYIELD_FROM_ISR(woken);` | `k_sem_give` (ISR 에서 호출 가능) | `semGive` (바이너리/카운팅은 ISR 가능) | `osSemaphoreRelease` (ISR 가능) |
| `OP_PRIO` (Part C 수동 부스트) | `vTaskPrioritySet(NULL, p)` | `k_thread_priority_set` | `taskPrioritySet` | `osThreadSetPriority` |
| 상속 뮤텍스 (M04 미리보기) | `xSemaphoreCreateMutex()` | `k_mutex` | `semMCreate(SEM_Q_PRIORITY \| SEM_INVERSION_SAFE)` | `osMutexNew` + `osMutexPrioInherit` |
| 스케줄러 잠금 (연습 3) | `vTaskSuspendAll()` / `xTaskResumeAll()` | `k_sched_lock()` / `k_sched_unlock()` | `taskLock()` / `taskUnlock()` | `osKernelLock()` / `osKernelUnlock()` |
| `run_tick` | SysTick → `xTaskIncrementTick()` | `sys_clock_announce()` | `tickAnnounce()` | — |
| `pick_next` | `taskSELECT_HIGHEST_PRIORITY_TASK()` | 런큐의 `next_up()` | 커널 내부 | — |

주의: 우선순위 숫자의 방향이 RTOS 마다 다르다. FreeRTOS·CMSIS-RTOS2 는 **클수록 높고**, Zephyr·VxWorks 는 **작을수록 높다** (VxWorks 0 이 최고). 면접에서 "priority 5 가 높은가?" 는 RTOS 이름을 먼저 물어봐야 하는 질문이다.

### Part A — 커널 핵심

```c
static int pick_next(void) {
    int best = -1;
    for (int i = 0; i < g_ntask; i++) {
        const task_t *t = &g_task[i];
        if (t->st != ST_READY) continue;
        if (best < 0 || t->prio > g_task[best].prio ||
            (t->prio == g_task[best].prio && t->ready_seq < g_task[best].ready_seq))
            best = i;
    }
    return best;
}
```

O(n) 선형 탐색이다. 실제 커널은 우선순위별 리스트 + 비트맵으로 O(1) 이다 — FreeRTOS 의 `configUSE_PORT_OPTIMISED_TASK_SELECTION` 은 Cortex-M 의 `CLZ` 로 가장 높은 비트를 찾는다. 같은 우선순위 FIFO 는 FreeRTOS 리스트 삽입 규칙과 같다: `list.c:156` 주석 — 같은 값의 항목이 이미 있으면 새 항목은 **그 뒤에** 놓인다.

세마포어 두 함수의 핵심은 **give 가 넘겨주기** 라는 점과 **owner 필드가 없다** 는 점이다.

```c
static bool ksem_take(int i, int s) {
    if (g_sem[s].count > 0) { g_sem[s].count--; return true; }
    g_task[i].st = ST_BLOCKED;
    g_task[i].wait_sem = s;
    g_task[i].wait_seq = ++g_seq;
    return false;
}
static int ksem_give(int s) {
    int w = /* BLOCKED 이고 wait_sem == s 인 것 중 (prio 높은, 먼저 기다린) */;
    if (w >= 0) { g_task[w].pc++; g_task[w].wait_sem = -1; make_ready(w); return w; }
    if (g_sem[s].count < g_sem[s].max) g_sem[s].count++;
    return -1;
}
```

FreeRTOS 와 비교하면 (`queue.c`, V11.3.2):

| 동작 | FreeRTOS | 이 모델 | 이 시나리오에서 차이 |
|---|---|---|---|
| 대기 리스트 순서 | `xTasksWaitingToReceive` 는 "Stored in priority order" (`queue.c:115`), `vTaskPlaceOnEventList` 가 우선순위순 삽입 (`tasks.c:5413` 주석) | 우선순위 → 대기 순서 | 없음 |
| 깨어난 뒤 | `xQueueSemaphoreTake` 의 `for( ; ; )` (`queue.c:1686`) 로 돌아가 카운트를 **다시 확인** | 직접 넘김 | 깨어난 H 가 최고 우선순위라 즉시 돌아 재확인에 성공 → 같음 |
| 소유자 기록 | take 성공 시 `uxQueueType == queueQUEUE_IS_MUTEX` 일 때만 `xMutexHolder` 기록 (`queue.c:1706`) | 없음 | 일반 세마포어는 FreeRTOS 도 기록 안 함 |
| 막힐 때 상속 | 뮤텍스일 때만 `xTaskPriorityInherit(xMutexHolder)` (`queue.c:1791–1795`) | 없음 | **이것이 역전의 원인** |

마지막 줄이 핵심이다. `xSemaphoreCreateBinary()` 는 `queueQUEUE_TYPE_BINARY_SEMAPHORE` 로 만들어지고(`semphr.h:167`), 상속 코드 경로는 `queueQUEUE_IS_MUTEX` 일 때만 탄다. 바이너리 세마포어로 상호 배제를 하면 커널은 **올려 줄 대상(소유자)을 모른다.** (뮤텍스 vs 세마포어의 소유권 자체는 RTOS-B10.)

`run_ops` / `dispatch` 는 6. 정답 코드에 있다. 정준 시나리오 C_M=4 를 tick 별로 따라가면:

| tick | 사건 | 실행 | 설명 |
|---|---|---|---|
| 0 | L 릴리스 | L | 유일한 READY. `WORK 1` 끝 → tick 끝(t=1 시각)에 `TAKE R` 성공, count 1→0 |
| 1 | | L | CS 1/4 |
| 2 | H 릴리스 | L | H 가 먼저 뽑혀 `TAKE R` → count 0 → **B**. 같은 tick 에 다시 골라 L (CS 2/4) |
| 3 | M 릴리스 | **M** | M(2) > L(1) → L 선점. L 은 R 을 쥔 채 `r` |
| 4–6 | | **M** | H(3) 는 M(2) 보다 높지만 B 라서 후보가 아니다. **역전 구간** |
| 7 | (M 은 t=7 시각에 끝) | L | CS 3/4 |
| 8 | | L | CS 4/4 → tick 끝에 `GIVE R` → 대기자 H 에게 직접 넘김 |
| 9–10 | | H | H 작업 2 tick → `GIVE R`, `END` (finish 11) |
| 11 | | L | 꼬리 `WORK 1` → finish 12 |

`B` 인 tick 은 2~8 의 7개 = 3 + C_M. 테스트가 그대로 확인한다: `row_is(TH, "..BBBBBBBRR.")`, `row_is(TL, "RRRrrrrRRrrR")`.

### Part B — 대기 시간은 M 의 길이에 비례한다

`solution.c` 실제 출력:

```text
C_M | H wait | blocked | by_holder(L) | by_lower(M) | H response
  0 |      3 |       3 |            3 |           0 |          5
  2 |      5 |       5 |            3 |           2 |          7
  4 |      7 |       7 |            3 |           4 |          9
  8 |     11 |      11 |            3 |           8 |         13
 16 |     19 |      19 |            3 |          16 |         21

canonical, plain semaphore, C_M=16
                           1 1 1 1 1 1 1 1 1 1 2 2 2 2
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
H    : . . B B B B B B B B B B B B B B B B B B B R R .
M    : . . . R R R R R R R R R R R R R R R R . . . . .
L    : R R R r r r r r r r r r r r r r r r r R R r r R
```

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">H 의 대기 tick vs M 의 실행 길이 C_M (solution.c 실측 표)</text>
  <line x1="80" y1="290" x2="676" y2="290" stroke="currentColor"/> <line x1="80" y1="290" x2="80" y2="60" stroke="currentColor"/> <text x="76" y="308" fill="currentColor" font-size="12">0</text> <text x="148" y="308" fill="currentColor" font-size="12">2</text>
  <text x="220" y="308" fill="currentColor" font-size="12">4</text> <text x="364" y="308" fill="currentColor" font-size="12">8</text> <text x="652" y="308" fill="currentColor" font-size="12">16</text> <text x="52" y="294" fill="currentColor" font-size="12">0</text>
  <text x="52" y="239" fill="currentColor" font-size="12">5</text> <text x="52" y="184" fill="currentColor" font-size="12">10</text> <text x="52" y="129" fill="currentColor" font-size="12">15</text> <text x="52" y="74" fill="currentColor" font-size="12">20</text>
  <text x="596" y="326" fill="currentColor">C_M (tick)</text> <text x="14" y="56" fill="currentColor">H 대기 (tick)</text> <line x1="80" y1="224" x2="676" y2="224" stroke="currentColor" stroke-dasharray="6 4"/> <text x="404" y="218" fill="currentColor" font-size="12">Part C 워치독 한계: 대기 ≤ 6 (그 위 = 리셋)</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="80,257 152,235 224,213 368,169 656,81"/>
  <circle cx="80" cy="257" r="5" class="f-warn" stroke="currentColor"/> <text x="88" y="249" fill="currentColor" font-size="12">3</text> <circle cx="152" cy="235" r="5" class="f-warn" stroke="currentColor"/> <text x="132" y="227" fill="currentColor" font-size="12">5</text>
  <circle cx="224" cy="213" r="5" class="f-warn" stroke="currentColor"/> <text x="204" y="205" fill="currentColor" font-size="12">7</text> <circle cx="368" cy="169" r="5" class="f-warn" stroke="currentColor"/> <text x="376" y="161" fill="currentColor" font-size="12">11</text>
  <circle cx="656" cy="81" r="5" class="f-warn" stroke="currentColor"/> <text x="664" y="73" fill="currentColor" font-size="12">19</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3" points="80,257 152,257 224,257 368,257 656,257"/>
  <rect x="76" y="253" width="8" height="8" class="f-ok" stroke="currentColor"/> <rect x="148" y="253" width="8" height="8" class="f-ok" stroke="currentColor"/> <rect x="220" y="253" width="8" height="8" class="f-ok" stroke="currentColor"/> <rect x="364" y="253" width="8" height="8" class="f-ok" stroke="currentColor"/>
  <rect x="652" y="253" width="8" height="8" class="f-ok" stroke="currentColor"/> <text x="296" y="277" fill="currentColor" font-size="12">수동 부스트(미리보기): 항상 3</text> <text x="116" y="125" fill="currentColor" font-size="12">일반 세마포어: 3 + C_M (기울기 1)</text>
</svg>
```

분해가 핵심이다:

| 성분 | 값 | 성격 |
|---|---|---|
| `by_holder` | 항상 3 | **bounded(유한) 역전**: H 가 L 의 남은 CS 를 기다리는 것. 자원을 공유하는 한 피할 수 없고, CS 길이로 상한이 정해진다 |
| `by_lower` | = C_M | **unbounded 역전**: R 과 아무 관계없는 M 이 H 를 늦춘다. M 이 길어지거나, 중간 우선순위 태스크가 **여러 개** 연달아 오면(연습 1) 상한이 없다 |
| `by_higher` | 0 | H 보다 높은 태스크의 선점은 역전이 아니라 정상 동작 (연습 2) |

일반식: H 의 대기 = (H 가 막힌 순간 L 의 남은 CS) + (그동안 READY 가 된, L 보다 높고 H 보다 낮은 모든 태스크의 실행 시간 합). 두 번째 항이 문제다. Liu & Layland(1973)의 고정 우선순위 분석은 태스크들이 서로 독립(공유 자원 없음)이라고 가정한다. 공유 자원을 넣으려면 응답 시간 분석(RTA)에서 태스크 i 의 응답 시간에 **블로킹 항 B_i** 를 더하는데, 일반 세마포어로는 B_i 에 M 들의 실행 시간이 섞여 들어가 유한한 값을 줄 수가 없다 — 스케줄 가능성 분석 자체가 무너진다. Sha·Rajkumar·Lehoczky(1990)가 우선순위 상속/천장 프로토콜을 제안한 이유가 바로 "B_i 를 임계 구역 길이로 묶는 것" 이다.

"unbounded" 의 정확한 뜻: M 이 무한히 돈다는 뜻이 아니라, **H 의 대기 상한을 H·L·R 만 보고는 정할 수 없다** 는 뜻이다. 시스템 어딘가의 중간 우선순위 태스크 하나가 길어지면 H 의 최악 응답 시간이 같이 늘어난다. 그리고 그 중간 태스크를 작성한 사람은 R 의 존재조차 모를 수 있다.

### Part C — Mars Pathfinder, 그리고 리셋

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Mars Pathfinder (1997) — Glenn Reeves 의 설명을 이 레슨의 모델로 옮기면</text>
  <rect x="10" y="40" width="200" height="40" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor">bc_sched (최고 우선순위)</text>
  <text x="20" y="74" fill="currentColor" font-size="12">8 Hz 주기마다 bc_dist 완료 확인</text>
  <rect x="10" y="100" width="200" height="40" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="118" fill="currentColor">bc_dist (높음) = H</text>
  <text x="20" y="134" fill="currentColor" font-size="12">1553 버스 데이터 분배</text>
  <rect x="10" y="160" width="200" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="178" fill="currentColor">여러 중간 우선순위 태스크 = M</text>
  <text x="20" y="194" fill="currentColor" font-size="12">통신 등, R 과 무관</text>
  <rect x="10" y="220" width="200" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="20" y="238" fill="currentColor">ASI/MET (낮음) = L</text>
  <text x="20" y="254" fill="currentColor" font-size="12">기상 데이터, 파이프로 IPC</text>
  <rect x="300" y="150" width="190" height="60" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="312" y="172" fill="currentColor">select() 의 뮤텍스 = R</text>
  <text x="312" y="190" fill="currentColor" font-size="12">파이프 fd "wait list" 보호</text>
  <text x="312" y="204" fill="currentColor" font-size="12">PI 옵션 꺼진 채 생성됨</text>
  <path d="M210 240 L300 200" stroke="currentColor" fill="none"/>
  <text x="218" y="276" fill="currentColor" font-size="12">① R 을 잡은 채 선점됨</text>
  <path d="M210 120 L300 162" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="222" y="126" fill="currentColor" font-size="12">③ 파이프로 보내려다 B</text>
  <path d="M210 180 L250 180 L250 232" stroke="currentColor" fill="none"/>
  <text x="256" y="196" fill="currentColor" font-size="12">② L 선점</text>
  <rect x="520" y="40" width="210" height="220" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="532" y="62">④ 다음 주기 시작에 bc_sched 가</text>
    <text x="532" y="80">   "bc_dist 미완료" 를 발견</text>
    <text x="532" y="98">   (Reeves: hard deadline)</text>
    <text x="532" y="122">⑤ 오류 → 컴퓨터 전체 리셋</text>
    <text x="532" y="140">   (Jones 요약: watchdog)</text>
    <text x="532" y="164">⑥ 지상 복제기에서 같은 작업을</text>
    <text x="532" y="182">   반복해 18시간 안에 재현,</text>
    <text x="532" y="200">   비행 SW 에 남겨 둔 trace 로 분석</text>
    <text x="532" y="224">⑦ selectLib 전역 변수로 PI 를 켜는</text>
    <text x="532" y="242">   패치를 업링크 → 리셋 멈춤</text>
  </g>
  <text x="10" y="292" fill="currentColor">이 레슨의 Part C: H=bc_dist, M=comm (무선 ISR 이 깨움), L=ASI/MET, R=select 뮤텍스, DEADLINE=10 = 다음 버스 주기.</text>
  <text x="10" y="314" fill="currentColor">C_M ≥ 4 이면 bc_dist 가 마감을 넘겨 "리셋". 숫자는 축소 모델 — 실제 tick·주기 길이와 1:1 이 아니다.</text>
</svg>
```

**무슨 일이 있었나** — 착륙(1997-07-04) 며칠 뒤부터 Pathfinder 착륙선 컴퓨터가 반복해서 **전체 리셋** 되었고 그때마다 데이터를 잃었다 (Jones 요약). 비행 소프트웨어 책임 엔지니어 Glenn Reeves 가 1997-12-15 에 쓴 설명(아래 참고자료)의 요지:

- VxWorks 위에서 1553 버스 활동을 **8 Hz** (주기 0.125 s)로 스케줄했다. `bc_sched` 는 (VxWorks `tExec` 를 빼면) 시스템 최고 우선순위, `bc_dist` 는 세 번째로 높고, 기상 계측 `ASI/MET` 태스크는 그보다 훨씬 낮았다.
- `bc_sched` 와 `bc_dist` 는 매 주기 상대가 실행을 끝냈는지 확인한다. 다음 주기가 시작될 때 `bc_dist` 가 끝나지 않았으면 오류로 보고 **컴퓨터를 리셋** 했다 — Reeves 표현으로 "a hard deadline in the system".
- ASI/MET 은 데이터를 VxWorks **파이프** 기반 IPC 로 받는데, 파이프를 기다리는 `select()` 구현이 파일 기술자 "wait list" 를 보호하려고 **상호 배제 세마포어** 를 만든다. ASI/MET 이 `select()` 안에서 이 세마포어를 쥔 채 선점되고, 여러 중간 우선순위 태스크가 도는 동안 `bc_dist` 가 ASI/MET 쪽 파이프로 데이터를 보내려다 같은 세마포어에서 막혔다. 다음 주기에 `bc_sched` 가 깨어나 미완료를 발견 → 리셋.
- 비행 소프트웨어에 남겨 둔 **trace/log 기능** 과, 지상 복제기에서 같은 작업을 반복해 **18시간 안에** 재현한 것이 원인 분석을 가능하게 했다. 착륙 전 시험에서도 본 적이 있지만 재현이 안 됐다고 한다.
- 수정: `selectLib` 의 **전역 변수** 로 select 세마포어의 생성 플래그에 우선순위 상속을 켰다 (그 뒤 만들어지는 모든 select 세마포어에 적용). Wind River 는 성능 때문에 기본 select 에서 상속을 빼 두었고, JPL 은 일부러 끈 게 아니라 "We just missed it" 이라고 했다. 변경은 탑재 소프트웨어에 차이분을 보내는 **패치** 로 올렸다.
- 이 이야기가 널리 퍼진 계기는 Mike Jones 가 Wind River 의 David Wilner 의 IEEE RTSS(1997-12) 기조연설을 요약한 글이다. 그 요약은 리셋을 "watchdog timer" 로, 수정을 "VxWorks C 인터프리터로 실행되는 짧은 C 프로그램을 올려 그 뮤텍스와 다른 두 개의 초기화 플래그를 FALSE→TRUE 로 바꿨다" 로 설명한다. 세부는 두 글이 조금 다르니 면접에서는 "주기 마감 검사가 리셋을 일으켰고, 상속 플래그를 켜는 패치를 업링크했다" 수준으로 말하면 안전하다.

이 레슨의 Part C 는 이 구조를 축소한 모델이다. 실제 출력:

```text
pathfinder-lite, plain semaphore, C_M=4 (deadline t=10)
                           1 1
tick : 0 1 2 3 4 5 6 7 8 9 0 1
H    : . . B B B B B B B R R .
M    : B B B R R R R . . . . .
L    : R R R r r r r R R r r R

C_M | plain: H wait  reset | boost: H wait  blocked  reset
  0 |            3     no |            3        0     no
  2 |            5     no |            3        0     no
  4 |            7    YES |            3        0     no
  8 |           11    YES |            3        0     no
 16 |           19    YES |            3        0     no
```

- M 의 t=0~2 `B` 는 `EV` 대기이고, t=3 에 `radio_isr` 가 give 해서 깨운다. Reeves 의 설명에서 중간 우선순위 태스크들이 "하필 그 짧은 창에" 돌게 된 것처럼, 이 모델에서도 역전은 **ISR 이 언제 오느냐** 에 달렸다 — 그래서 시험에서는 드물게만 보인다.
- H 의 끝난 시각 = 2 + 대기 + 2 = 7 + C_M. 마감 10 → **C_M ≤ 3 이면 안전, 4 부터 리셋**. `max_safe_cm(10, false) == 3`, 마감 8 이면 1, 마감 6 이면 C_M=0 도 실패(-1). 닫힌 식 `D − 7` 은 연습 4 에서 D=3..30 전부 커널로 확인한다.

```c
static int watchdog_check(int h, int deadline) {
    if (g_task[h].finish >= 0 && g_task[h].finish <= deadline) return -1;
    return deadline;
}
```

### Part C — 해결 미리보기 (구현은 M04 / M05)

```svg
<svg viewBox="0 0 740 298" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 Pathfinder-lite 시나리오, L 을 R 을 쥐는 동안만 손으로 prio 3 으로 올렸을 때 (C_M=4)</text>
  <text x="121" y="42" fill="currentColor" font-size="12">0</text> <text x="171" y="42" fill="currentColor" font-size="12">1</text> <text x="221" y="42" fill="currentColor" font-size="12">2</text> <text x="271" y="42" fill="currentColor" font-size="12">3</text>
  <text x="321" y="42" fill="currentColor" font-size="12">4</text> <text x="371" y="42" fill="currentColor" font-size="12">5</text> <text x="421" y="42" fill="currentColor" font-size="12">6</text> <text x="471" y="42" fill="currentColor" font-size="12">7</text>
  <text x="521" y="42" fill="currentColor" font-size="12">8</text> <text x="571" y="42" fill="currentColor" font-size="12">9</text> <text x="621" y="42" fill="currentColor" font-size="12">10</text> <text x="671" y="42" fill="currentColor" font-size="12">11</text>
  <text x="10" y="74" fill="currentColor">H (3)</text>
  <rect x="201" y="56" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="222" y="74" fill="currentColor">r</text> <rect x="251" y="56" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="272" y="74" fill="currentColor">r</text>
  <rect x="301" y="56" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="322" y="74" fill="currentColor">r</text> <rect x="351" y="56" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="401" y="56" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <text x="10" y="114" fill="currentColor">M (2)</text>
  <rect x="101" y="96" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="121" y="114" fill="currentColor">B</text> <rect x="151" y="96" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="171" y="114" fill="currentColor">B</text>
  <rect x="201" y="96" width="48" height="26" class="f-warn" stroke="currentColor"/> <text x="221" y="114" fill="currentColor">B</text> <rect x="251" y="96" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="272" y="114" fill="currentColor">r</text>
  <rect x="301" y="96" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="322" y="114" fill="currentColor">r</text> <rect x="351" y="96" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="372" y="114" fill="currentColor">r</text>
  <rect x="401" y="96" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="422" y="114" fill="currentColor">r</text> <rect x="451" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="501" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="551" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="601" y="96" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <text x="10" y="154" fill="currentColor">L (1→3→1)</text>
  <rect x="101" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="151" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="201" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="251" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="301" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <rect x="351" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="372" y="154" fill="currentColor">r</text> <rect x="401" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="422" y="154" fill="currentColor">r</text> <rect x="451" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="472" y="154" fill="currentColor">r</text> <rect x="501" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="522" y="154" fill="currentColor">r</text> <rect x="551" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/> <text x="572" y="154" fill="currentColor">r</text> <rect x="601" y="136" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="622" y="154" fill="currentColor">r</text> <rect x="651" y="136" width="48" height="26" class="f-acc" stroke="currentColor"/> <line x1="150" y1="46" x2="150" y2="170" stroke="currentColor" stroke-dasharray="2 3"/> <line x1="350" y1="46" x2="350" y2="170" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="10" y="190" fill="currentColor">H 는 t=2~4 r (L 과 같은 prio 3, FIFO 로 L 먼저) → 대기 3 = L 의 남은 CS. C_M=16 이어도 3.</text>
  <text x="10" y="212" fill="currentColor">대가는 M 이 치른다 (t=3~6 r): R 을 안 쓰는 M 도 L 의 CS 한 번만큼 늦는다.</text>
  <text x="10" y="234" fill="currentColor">(M 의 t=0~2 B 는 R 이 아니라 무선 이벤트 EV 대기 — t=3 의 radio_isr 가 깨운다.)</text>
  <text x="10" y="256" fill="currentColor">커널이 "대기자가 생기면" 자동으로 올리면 PI (RTOS-M04), "잡는 순간 항상" 이면 IPCP (RTOS-M05).</text>
</svg>
```

```text
pathfinder-lite, L boosted to 3 by hand, C_M=4
                           1 1
tick : 0 1 2 3 4 5 6 7 8 9 0 1
H    : . . r r r R R . . . . .
M    : B B B r r r r R R R R .
L    : R R R R R r r r r r r R
```

L 의 스크립트에 `PRIO 3` (R 잡기 전) / `PRIO 1` (R 놓은 뒤)을 넣었을 뿐인데, 모든 C_M 에서 H 대기가 3 으로 고정되고 리셋이 사라진다. 이것은 **커널의 해결책이 아니라 사람이 손으로 한 것** 이고, 그 자동화 방식이 두 가지다:

| 방식 | 언제 올리나 | 얼마나 | 이 시나리오의 H 대기 | 레슨 |
|---|---|---|---|---|
| 일반 세마포어 (지금) | 안 올림 | — | 3 + C_M (무한정) | M03 |
| 우선순위 상속 (PI) | 더 높은 태스크가 **막히는 순간** | 그 대기자의 우선순위까지 | 3 (L 의 남은 CS) | RTOS-M04 |
| 즉시 천장 (IPCP) | 자원을 **잡는 순간** 항상 | 그 자원을 쓰는 최고 우선순위(천장) | 3 | RTOS-M05 |
| 스케줄러 잠금 | 잡는 순간 | 사실상 무한대 (아무도 선점 못 함) | 3, 대신 무관한 급한 태스크도 막힘 | 연습 3 |
| 설계로 회피 | — | — | 0 (공유 안 함) | 아래 |

우리 손 부스트는 "잡기 전에 천장(3)으로" 올렸으므로 모양으로는 IPCP 에 가깝다. 그래서 H 는 `B` 가 아니라 `r` 로 기다린다 (같은 우선순위 3 이고 L 이 먼저 READY 였으니 FIFO). PI 였다면 t=2 에 H 가 `B` 가 되는 순간 L 이 3 으로 올라가고, 대기 tick 수는 똑같이 3 이다. 어느 쪽이든 **대가는 M 이 치른다** — R 을 쓰지 않는 M 도 L 의 CS 한 번만큼(t=3~6) 늦어진다. 이것이 "블로킹은 CS 하나로 제한된다" 의 다른 얼굴이다.

설계로 피하는 법도 면접에서 점수가 된다: ① H 와 L 이 같은 락을 공유하지 않게 — L 은 **메시지 큐** 로 게이트키퍼(gatekeeper) 태스크에 요청만 보내고, 버스는 그 태스크 하나만 만진다 (FreeRTOS 책이 권하는 패턴). ② CS 를 짧게 — 플래시 쓰기 전체가 아니라 DMA 설정 몇 줄만. ③ 우선순위 설계 — 공유하는 태스크끼리 우선순위를 붙여 두면 사이에 끼는 M 이 줄어든다. ④ 시간 감시 — Pathfinder 처럼 마감 검사·trace 를 남겨 둬야 현장에서 원인을 찾는다.

**POSIX 쪽 확인** — `demo_posix_protocol.c` 실제 출력 (macOS, Apple clang 21):

<!-- code: demo_posix_protocol.c -->

```text
default protocol            : PTHREAD_PRIO_NONE
setprotocol(PTHREAD_PRIO_NONE   ): rc=0 (ok) get=PTHREAD_PRIO_NONE init=0 lock=0
setprotocol(PTHREAD_PRIO_INHERIT): rc=0 (ok) get=PTHREAD_PRIO_INHERIT init=0 lock=0
setprotocol(PTHREAD_PRIO_PROTECT): rc=0 (ok) get=PTHREAD_PRIO_PROTECT init=0 lock=0
prioceiling                 : rc=0 ceiling=0
```

POSIX 뮤텍스의 기본값은 `PTHREAD_PRIO_NONE` — 이 레슨의 일반 세마포어와 같은 "대책 없음" 이다. `PTHREAD_PRIO_INHERIT` = 상속(M04), `PTHREAD_PRIO_PROTECT` = 즉시형 천장(M05). 맥 libc 는 세 값을 모두 **받아 준다**. 다만 이 데모는 속성 설정이 성공하는지만 본 것이고, 맥 커널이 실제로 우선순위를 올려 주는지는 확인하지 않았다(미검증). 맥은 실시간 스케줄러가 아니고 코어도 여러 개라 이 레슨의 역전을 진짜 스레드로 재현하기 어렵다 — 그래서 모델을 쓴다. Linux 에서는 `SCHED_FIFO` + 한 코어에 고정(`taskset`)하면 재현할 수 있다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 타임라인·표 출력 후 49/49 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 49/49, 보고 없음
```

5번 연속 실행해도 출력이 바이트 단위로 같다 — 커널이 결정적이기 때문이다 (스레드·시간·난수 없음).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 상호 배제에 바이너리 세마포어 사용 (`xSemaphoreCreateBinary`) | 소유자가 없어 커널이 상속할 대상을 모른다 — FreeRTOS 도 `queueQUEUE_IS_MUTEX` 일 때만 `xTaskPriorityInherit` | `xSemaphoreCreateMutex()` (PI), Zephyr `k_mutex` |
| "H 가 최고 우선순위니까 응답 시간 = H 의 실행 시간" 으로 분석 | 공유 자원이 있으면 블로킹 항 B_H 가 붙는다. 일반 세마포어면 B_H 에 상한이 없다 | RTA 에 B_i = (상속/천장 하에서) 최장 CS 를 넣는다 |
| 역전을 "L 이 H 를 막는 것" 으로만 설명 | 그건 bounded 부분(`by_holder`)이다. 위험한 건 **R 과 무관한 M** 이 끼는 unbounded 부분 | 대기를 holder / lower / higher 로 분해 (Part B) |
| `pick_next` 에서 base 우선순위로 비교 | `vTaskPrioritySet`·상속으로 바뀐 **지금** 우선순위가 스케줄을 정한다 → 부스트가 안 먹음 | `t->prio` 로 비교, 통계 분류만 `base_prio` |
| give 에서 대기자가 있는데도 `count++` 도 함 | 넘겨준 뒤 카운트까지 올리면 자원이 두 번 나간다 (이진 세마포어에서 두 태스크가 CS 안) | 대기자가 있으면 넘기기만, 없을 때만 `count++` |
| give 직후 선점 검사 안 함 | 깨어난 H 가 L 의 다음 WORK 가 끝날 때까지 못 돈다 (불필요한 1 tick 역전) | `may_preempt && pick_next() != i` → `RUN_PREEMPTED` |
| 디스패치 루프에 상한 없음 | 잘못된 스텁/우선순위 핑퐁에서 무한 루프 → 테스트가 FAIL 대신 멈춤 | `for (guard < 4*MAX_TASKS)` |
| CS 안에서 긴 작업 (플래시 페이지 쓰기, `printf`) | CS 가 길수록 bounded 부분도, 그 사이 끼는 M 의 기회도 커진다 | CS 는 버스 설정·복사만, 긴 일은 밖에서 |
| PI 를 켜면 끝났다고 생각 | 상속은 연쇄·다중 뮤텍스에서 복잡하고 교착은 못 막는다 (M04/M05) | 락 순서 규칙 + 짧은 CS + 필요시 IPCP |

## 8. 면접 Q&A

<details>
<summary>Q1. Mars Pathfinder 우선순위 역전을 30초 안에 설명해 보세요.</summary>

1997년 착륙 후 Pathfinder 가 반복 리셋됐다. 높은 우선순위 버스 분배 태스크 bc_dist 가, 낮은 우선순위 기상 태스크 ASI/MET 이 select() 안에서 쥐고 있던 뮤텍스를 기다리는 동안 중간 우선순위 태스크들이 ASI/MET 을 선점해 뮤텍스가 안 풀렸고, 다음 8 Hz 버스 주기에 bc_sched 가 bc_dist 미완료를 발견해 시스템을 리셋했다. 그 뮤텍스는 VxWorks 에서 우선순위 상속 옵션 없이 만들어져 있었다. JPL 은 남겨 둔 trace 로 지상에서 재현했고, 상속을 켜는 전역 플래그를 바꾸는 패치를 업링크해 해결했다. 꼬리: "왜 시험에서 못 잡았나?" → 중간 태스크가 정확히 그 짧은 창에 깨어나야 생기는 타이밍 버그라 드물었고, 착륙 전에 한두 번 봤지만 재현이 안 됐다.

</details>

<details>
<summary>Q2. bounded 역전과 unbounded 역전의 차이는? bounded 는 없앨 수 있나요?</summary>

bounded = H 가 자원 보유자 L 의 남은 임계 구역만큼 기다리는 것. 자원을 공유하는 한 **없앨 수 없고**, CS 길이로 상한이 정해진다 (이 레슨에서 항상 3). unbounded = 그 사이 R 과 무관한 중간 우선순위 태스크가 L 을 선점해서 대기가 그 태스크들의 실행 시간만큼 늘어나는 것 (3 + C_M). 상속/천장 프로토콜은 unbounded 를 없애고 bounded 를 "CS 하나" 로 묶는다. bounded 를 0 으로 만들려면 공유 자체를 없애야 한다 (게이트키퍼 태스크 + 큐, lock-free).

</details>

<details>
<summary>Q3. 바이너리 세마포어로 상호 배제하면 왜 커널이 우선순위 상속을 못 하나요?</summary>

상속은 "막힌 H 의 우선순위를 **자원을 쥔 태스크** 에 빌려 주는 것" 인데, 세마포어는 누가 take 했는지 기록하지 않는다 (아무나 give 할 수 있도록 설계됐으니까 — ISR → 태스크 신호용). FreeRTOS V11.3.2 에서도 소유자 기록(`xMutexHolder`, `queue.c:1706`)과 `xTaskPriorityInherit` 호출(`queue.c:1795`)은 `uxQueueType == queueQUEUE_IS_MUTEX` 일 때만 실행된다. 꼬리: "그럼 ISR → 태스크 신호에 뮤텍스를 쓰면?" → 뮤텍스는 take 한 태스크가 give 해야 하고 ISR 에는 태스크 정체성이 없다 (RTOS-B10).

</details>

<details>
<summary>Q4. 우선순위 상속과 우선순위 천장 프로토콜을 비교해 주세요.</summary>

상속(PI): 높은 태스크가 막힐 때만 보유자를 대기자 우선순위로 올린다. 자원별 정보가 필요 없고 평소 비용이 없다. 하지만 블로킹이 연쇄될 수 있고(A→B→C 전이 상속 필요), **교착은 못 막는다**. 천장(IPCP): 자원마다 "그것을 쓰는 태스크의 최고 우선순위" 를 미리 정해 두고, 잡는 순간 항상 그 천장으로 올린다. 블로킹이 CS 한 번으로 제한되고 교착(반대 순서 잠금)도 생기지 않지만, 천장을 정적으로 알아야 하고 경합이 없을 때도 우선순위를 바꾸는 비용이 든다. POSIX 로는 `PTHREAD_PRIO_INHERIT` vs `PTHREAD_PRIO_PROTECT`. 구현과 증명은 RTOS-M04/M05, 원 논문은 Sha·Rajkumar·Lehoczky 1990.

</details>

<details>
<summary>Q5. 현장 장비에서 "가끔 제어 루프가 늦는다" 는 보고가 왔습니다. 역전인지 어떻게 확인하나요?</summary>

① 트레이스: FreeRTOS trace 매크로 + Tracealyzer/SystemView 로 태스크 상태 줄을 보고, 이 레슨의 Part B 처럼 H 가 `B` 인 구간에 **누가 돌았는지** 를 본다 — H 보다 낮고 락 보유자가 아닌 태스크가 돌면 역전. ② 계측: H 의 릴리스→완료 시간 최댓값, 락 대기 시간 최댓값을 카운터로 남긴다 (DWT CYCCNT). ③ 마감 감시: Pathfinder 의 bc_sched 처럼 주기 마감을 검사하고 리셋 전에 trace 버퍼를 비휘발 영역에 저장. ④ 코드 검색: 상호 배제에 `xSemaphoreCreateBinary` 를 쓰는 곳. 재현은 의심되는 중간 태스크를 인위적으로 길게 만들어(C_M↑) 대기가 비례해 느는지 본다 — 이 레슨의 실험 그대로.

</details>

<details>
<summary>Q6. 그냥 임계 구역에서 인터럽트를 끄거나 스케줄러를 잠그면 되지 않나요?</summary>

역전은 막힌다: 아무도 L 을 선점하지 못하니 H 대기 = L 의 남은 CS (연습 3 에서 3). 하지만 대가가 크다 — R 과 **무관한** 더 급한 태스크(연습 3 의 U, prio 4)까지 CS 동안 막힌다 (U 대기 3 vs 부스트 0). 인터럽트를 끄면 ISR 지연까지 늘어난다. 그래서 짧은 CS(수 µs, 레지스터 몇 개)에만 쓰고 긴 CS 는 PI/천장 뮤텍스. 그리고 FreeRTOS 는 `vTaskSuspendAll()` 상태에서 블로킹 API 호출을 허용하지 않는다(문서 기준) — "잠근 채 세마포어 대기" 는 안 된다. 멀티코어에서는 내 코어만 잠그므로 상호 배제도 안 된다 (RTOS-A08).

</details>

<details>
<summary>Q7. M 이 없으면 문제가 없는 건가요? 그리고 H 보다 높은 태스크가 끼는 건요?</summary>

M 이 없으면 대기는 L 의 CS 로 제한된다(C_M=0 → 3) — 정상적인 공유 비용. 하지만 "M 이 없다" 는 시스템 전체를 봐야 알 수 있는 성질이라 나중에 누가 중간 우선순위 태스크를 추가하는 순간 깨진다. H 보다 높은 태스크 X 가 끼는 건 역전이 아니라 정상 선점이다(연습 2: `by_higher == 2`). RTA 에서 X 는 간섭(interference) 항, L 은 블로킹 항으로 따로 센다.

</details>

<details>
<summary>Q8. 이 시뮬레이터의 숫자는 실제 시스템과 얼마나 같나요?</summary>

순서와 비례 관계는 같고, 상수는 모델 규칙에 달렸다. 이 모델은 커널 호출을 시간 0 으로, CS 를 tick 단위로 센다. 커널 호출 자체를 1 tick 으로 세면 H 대기가 4 + C_M 으로 한 칸 밀린다 — 하지만 기울기 1 과 "M 의 실행 시간이 그대로 H 의 대기로 옮겨 온다" 는 결론은 같다. 실제 칩에서는 tick 사이에도 선점이 일어나고(ISR 이 높은 태스크를 깨우면 `portYIELD_FROM_ISR` 로 즉시), 컨텍스트 스위치 비용·캐시 효과가 더해진다. FreeRTOS POSIX 포트로 순서를 검증할 수는 있지만 실시간성은 없다.

</details>

## 9. 추가 연습

### 연습 1 — 중간 우선순위 태스크가 여럿

정준 시나리오에서 M 대신 M1(prio 2, t=3, 3 tick)과 M2(prio 2, t=4, 5 tick)를 넣어라. H 의 대기와 분해는?

<details>
<summary>답 보기</summary>

```text
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
H    : . . B B B B B B B B B B B R R .
1    : . . . R R R . . . . . . . . . .
L    : R R R r r r r r r r r R R r r R
2    : . . . . r r R R R R R . . . . .
```

대기 11 = 3 + 3 + 5, `by_lower == 8`. 중간 태스크들의 실행 시간이 **더해진다**. 하나하나는 짧아도 합에는 상한이 없다 — "unbounded" 가 "M 하나가 무한 루프" 라는 뜻이 아닌 이유.

</details>

### 연습 2 — H 보다 높은 태스크는 역전이 아니다

M 을 C_M=0 으로 두고 X(prio 4, t=3, 2 tick, R 안 씀)를 추가하라. `h_stats` 의 분해는?

<details>
<summary>답 보기</summary>

`wait == 5`, `by_holder == 3`, `by_higher == 2`, `by_lower == 0`. X 는 H 가 막혀 있든 아니든 H 를 선점했을 것이므로 역전이 아니라 간섭이다. (타임라인에서 M 이 t=3,4 에 `r` 인 것은 일이 0 인 M 이 X 에 밀려 아직 디스패치되지 않았기 때문이다.) 분석에서 이 둘을 섞으면 "역전이 없는 시스템" 에서도 역전을 봤다고 착각한다.

</details>

### 연습 3 — 스케줄러 잠금 vs 수동 부스트: 무관한 태스크의 대가

커널에 `OP_LOCK`/`OP_UNLOCK`(`vTaskSuspendAll`/`xTaskResumeAll` 상당: 잠근 태스크가 READY 인 동안 그것만 돈다)을 추가하고, L 의 CS 를 잠금으로 감싸라. R 과 무관한 급한 태스크 U(prio 4, t=2, 1 tick)를 넣고 C_M=4 에서 U 와 H 의 대기를 부스트 방식과 비교하라.

<details>
<summary>답 보기</summary>

```text
ex3: scheduler lock around CS (C_M=4)          ex3: manual boost to 3 (C_M=4)
H    : . . r r r r R R . . . . .               H    : . . r r r r R R . . . . .
L    : R R R R R r r r r r r r R               L    : R R r R R R r r r r r r R
U    : . . r r r R . . . . . . .               U    : . . R . . . . . . . . . .
```

(practice.c 실제 출력 두 개를 나란히 놓고 tick 줄·M 줄은 생략했다.) H 는 둘 다 대기 4 (L 의 CS 3 + U 1). 하지만 U 는 잠금에서 **3 tick** 기다리고 부스트에서는 0. 잠금은 "모두를 막아서" 역전을 막는다. C_M = 0..16 모두 잠금 하에서 H 대기 3 (U 없을 때) — 역전 자체는 확실히 막는다.

</details>

### 연습 4 — 마감과 안전한 C_M 의 닫힌 식

Pathfinder-lite 에서 마감 D 에 대해 리셋이 안 나는 최대 C_M 을 식으로 쓰고, D=3..30 전부에서 커널로 검증하라.

<details>
<summary>답 보기</summary>

H 끝난 시각 = 릴리스 2 + 대기 (3 + C_M) + 작업 2 = 7 + C_M. `7 + C_M ≤ D` → **C_M ≤ D − 7**, D < 7 이면 C_M=0 도 실패(-1). 28개 D 모두 `max_safe_cm(D) == (D ≥ 7 ? D − 7 : −1)`. 설계 관점: 마감 여유(slack)가 정확히 "참을 수 있는 역전 길이" 가 된다. 시스템 어딘가의 중간 태스크가 slack 보다 길어지는 순간 리셋 — 그 태스크를 쓴 사람은 그 사실을 알 길이 없다.

</details>

### 연습 5 — H 가 언제 도착하느냐

C_M=4 로 두고 H 의 릴리스를 r_H = 1..9 로 바꿔 대기를 표로 만들어라. 무엇이 일정한가?

<details>
<summary>답 보기</summary>

```text
ex5: r_H | H blocked
       1 | 8
       2 | 7
       3 | 6
       4 | 5
       5 | 4
       6 | 3
       7 | 2
       8 | 1
       9 | 0
```

대기 = 9 − r_H. **R 이 풀리는 시각(9 = 1 + 4 + C_M)이 고정** 이고 H 는 그때까지 기다릴 뿐이다. r_H = 4, 5 는 H 가 도착했을 때 이미 M 이 돌고 있는 경우인데 결과 식은 같다. 즉 일반 세마포어에서는 "L 의 CS 가 끝나는 시각" 자체가 M 에 의해 미뤄진다 — 상속은 바로 이 시각을 C_M 과 무관하게 만든다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
```

## 10. 핵심 정리

- 우선순위 역전 = 높은 H 가 낮은 L 이 쥔 자원을 기다리는 동안, R 과 **무관한** 중간 M 이 L 을 선점해 H 까지 늦추는 것.
- H 대기 = L 의 남은 CS (bounded, 피할 수 없음) + 끼어든 중간 태스크들의 실행 시간 합 (unbounded). 이 모델: **3 + C_M**, 기울기 1.
- 일반(바이너리/카운팅) 세마포어는 **소유자가 없어** 커널이 상속할 수 없다. FreeRTOS 도 상속 경로는 `queueQUEUE_IS_MUTEX` 일 때만.
- Pathfinder: bc_dist(H) ↔ select() 뮤텍스 ↔ ASI/MET(L), 중간 태스크들, 8 Hz 주기 마감 검사 → 리셋. trace + 지상 재현, selectLib 전역 플래그로 PI 를 켜는 패치.
- 해결: PI(막힐 때 올림, M04), IPCP(잡을 때 천장, M05), 스케줄러 잠금(무관한 태스크까지 막음), 설계로 공유 회피(게이트키퍼 + 큐), CS 짧게.
- 분석: RTA 의 블로킹 항 B_i 는 상속/천장이 있어야 "CS 하나" 로 유한해진다.
- 우선순위 숫자 방향은 RTOS 마다 다르다 (FreeRTOS 클수록 높음, Zephyr/VxWorks 작을수록 높음).

## 참고자료

- Glenn Reeves, "What really happened on Mars?" (Mars Pathfinder Flight Software Cognizant Engineer, 1997-12-15 이메일) — <https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html>
- Mike Jones, "What really happened on Mars Rover Pathfinder" (David Wilner 의 RTSS 1997 기조연설 요약, 1997-12-07) — <https://www.cs.cornell.edu/courses/cs614/1999sp/papers/pathfinder.html>
- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization," IEEE Transactions on Computers 39(9), 1990.
- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment," JACM 20(1), 1973.
- FreeRTOS-Kernel V11.3.2 소스 — `queue.c` (115 대기 리스트 우선순위순, 1686 재시도 루프, 1706 소유자 기록, 1791–1795 `xTaskPriorityInherit`), `include/semphr.h` (167 `xSemaphoreCreateBinary`, 735 `xSemaphoreCreateMutex`), `tasks.c` (2844 `vTaskPrioritySet`, 5413 이벤트 리스트 우선순위순), `list.c` (156 같은 값은 뒤에 삽입) — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS 문서 — Mutexes (우선순위 상속), Binary Semaphores — <https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html>
- Zephyr 문서 — Mutexes (priority inheritance), Semaphores — <https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html>
- POSIX.1-2017 `pthread_mutexattr_setprotocol` (PTHREAD_PRIO_NONE / INHERIT / PROTECT) — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutexattr_setprotocol.html>
- Giorgio Buttazzo, *Hard Real-Time Computing Systems* (Springer) — 자원 접근 프로토콜 장 (NPP, PIP, PCP, SRP).
- 관련 레슨: RTOS-B08 (ISR → 태스크 신호, give 는 안 막힘), RTOS-B09 (카운팅 세마포어 대기 리스트), RTOS-B10 (뮤텍스 vs 세마포어, 소유권), RTOS-M04 (우선순위 상속 구현 — 같은 시나리오), RTOS-M05 (IPCP 와 교착 — 같은 시나리오), RTOS-A08 (멀티코어에서 잠금), C-M14 (PRIMASK 임계 구역), OS-B06 (조건 변수)

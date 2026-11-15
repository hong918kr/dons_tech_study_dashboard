---
id: RTOS-A09
time: 110분
updated: 2026-11-11
---
# RTOS-A09 데드라인 미스 사후 분석: 트레이스 케이스 스터디

## 학습 목표

- 트레이스 텍스트만 보고 **"누가 언제 CPU 를 썼나 / 누가 무엇 때문에 막혔나 / 누가 뮤텍스를 쥐었나"** 를 구간(interval)으로 재구성할 수 있다 (ISR 시간은 태스크 몫에서 뺀다, wrap·유실 구간 처리).
- 데드라인 미스 하나를 **창(release → deadline)** 으로 잘라, 우선순위 역전 · ISR 과다 · 큐 포화 블로킹 · RM 위반 우선순위를 **숫자 근거**와 함께 구분할 수 있다 — 그리고 비슷해 보이는 정상 패턴(유한 역전, 적당한 ISR)은 오탐하지 않는다.
- 원인마다 수정(우선순위 상속 / ISR 일을 태스크로 / 큐 깊이·소비자 우선순위 / RM 재할당)을 고르고, **다시 돌려서** 미스 0 을 보이고, RTA 로 왜 고쳐졌는지 설명할 수 있다.
- 진짜 FreeRTOS(POSIX 포트)에 trace 매크로를 꽂아 같은 포맷의 트레이스를 만들고, 같은 분석기로 진단할 수 있다.
- 면접에서 "이 트레이스에서 미스 원인은?" 에 **증상 → 근거 → 원인 → 수정 → 재검증** 순서로 답할 수 있다.

## 1. 문제

시니어/스태프 펌웨어 면접의 단골은 "SystemView 캡처 한 장(또는 로그 몇 줄)을 줄 테니 제어 루프가 왜 가끔 마감을 놓치는지 말해 보라" 다 — 스케줄링 이론을 외웠는지가 아니라 **증거로 원인을 좁히는 디버깅 사고**를 보는 질문이다.

이번 레슨은 RTOS-A06 이 정한 **공유 트레이스 포맷**(`# rtos-trace v1 unit=us`, 한 줄 = `<t_us> <EVENT> <이름> [인자]`)을 입력으로 받는 사후 분석기(post-mortem analyzer)를 만든다. 재료는 두 가지다. ① 결정적 미니 커널 시뮬레이터가 일부러 버그를 심은 시스템 4개 + 정상 대조군 1개를 돌려 트레이스를 쓴다. ② 진짜 FreeRTOS V11.3.2(POSIX 포트)에 trace 매크로를 꽂아 같은 버그 3개를 재현하고 같은 포맷으로 덤프한다. 분석기는 **텍스트만** 본다 — 시뮬레이터 내부 변수는 못 본다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이 레슨의 흐름: 버그 → 트레이스 → 진단 → 수정 → 다시 돌리기</text>
  <rect x="20" y="40" width="150" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor">시스템 S1–S5</text>
  <text x="32" y="82" fill="currentColor" font-size="12">태스크·ISR·뮤텍스·큐 표</text>
  <rect x="200" y="40" width="150" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="212" y="62" fill="currentColor">미니 커널 시뮬레이터</text>
  <text x="212" y="82" fill="currentColor" font-size="12">10 µs 슬롯, seed 흔들림</text>
  <rect x="380" y="40" width="170" height="54" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="392" y="62" fill="currentColor">트레이스 텍스트</text>
  <text x="392" y="82" fill="currentColor" font-size="12">RTOS-A06 포맷 (분석기 입력)</text>
  <rect x="580" y="40" width="150" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="592" y="62" fill="currentColor">FreeRTOS POSIX 포트</text>
  <text x="592" y="82" fill="currentColor" font-size="12">trace 매크로 → 파일</text>
  <line x1="170" y1="67" x2="196" y2="67" stroke="currentColor"/><polygon points="196,62 204,67 196,72" fill="currentColor"/>
  <line x1="350" y1="67" x2="376" y2="67" stroke="currentColor"/><polygon points="376,62 384,67 376,72" fill="currentColor"/>
  <line x1="580" y1="67" x2="554" y2="67" stroke="currentColor"/><polygon points="556,62 548,67 556,72" fill="currentColor"/>
  <rect x="380" y="140" width="170" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="392" y="162" fill="currentColor">타임라인 재구성</text>
  <text x="392" y="182" fill="currentColor" font-size="12">실행·블록·보유 구간</text>
  <rect x="200" y="140" width="150" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="212" y="162" fill="currentColor">규칙 4개</text>
  <text x="212" y="182" fill="currentColor" font-size="12">역전·ISR·큐·RM</text>
  <rect x="20" y="140" width="150" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="32" y="162" fill="currentColor">진단 + 숫자 근거</text>
  <text x="32" y="182" fill="currentColor" font-size="12">→ apply_fix()</text>
  <line x1="465" y1="94" x2="465" y2="136" stroke="currentColor"/><polygon points="460,134 465,142 470,134" fill="currentColor"/>
  <line x1="380" y1="167" x2="354" y2="167" stroke="currentColor"/><polygon points="356,162 348,167 356,172" fill="currentColor"/>
  <line x1="200" y1="167" x2="174" y2="167" stroke="currentColor"/><polygon points="176,162 168,167 176,172" fill="currentColor"/>
  <line x1="95" y1="140" x2="95" y2="98" stroke="currentColor" stroke-dasharray="5 4"/><polygon points="90,100 95,92 100,100" fill="currentColor"/>
  <text x="104" y="124" fill="currentColor" font-size="12">고친 설정으로 다시 → 미스 0?</text>
  <text x="10" y="228" fill="currentColor">분석기는 텍스트만 본다. 같은 코드가 시뮬레이터 트레이스와 진짜 FreeRTOS 트레이스를 둘 다 읽는다.</text>
  <text x="10" y="250" fill="currentColor">정상 대조군 S5 에도 뮤텍스 대기·ISR·큐가 있다 — 오탐(false positive) 0 이 규칙의 품질 기준이다.</text>
</svg>
```

포맷 (RTOS-A06 소유, 여기서는 축약 사본 파서를 쓴다):

| 이벤트 | 인자 | 이 레슨에서의 뜻 |
|---|---|---|
| `RELEASE T d` | 절대 마감 µs | 주기 작업 출시. 창 = [이 시각, d] |
| `SWITCH_OUT A` / `SWITCH_IN B` | — | 문맥 교환. 같은 시각 연속 두 줄 |
| `ISR_ENTER I` / `ISR_EXIT I` | — | 그 사이 CPU 는 ISR 몫 (태스크는 "현재" 지만 못 돈다) |
| `BLOCK T obj` | `delay`/`delay_until` 또는 락 이름 | 작업 끝(다음 주기 대기) 또는 락 대기 |
| `Q_SEND_BLOCK T q` / `Q_RECV_BLOCK T q` | 큐 이름 | 꽉 찬 큐에 보내다 / 빈 큐에서 받다 막힘 |
| `UNBLOCK T [obj]` | — | 블록 해제 (블록 중이 아니면 무시) |
| `MUTEX_TAKE T m` / `MUTEX_GIVE T m` | 락 이름 | 보유 구간 |
| `DEADLINE_MISS T d` | 마감 µs | 이 작업이 d 까지 안 끝남 |
| `DROP TRACE n` | 잃은 수 | 유실. 다음 `SWITCH_IN` 까지 시간을 아무에게도 주지 않는다 |

이 레슨의 확장: `# task NAME prio=P period=US` 주석 줄(A06 파서는 무시한다). SystemView/Tracealyzer 도 태스크 생성 시 우선순위를 기록한다 (문서 기준, 미검증) — 우선순위 없이 선점 관계만으로 순서를 복원하는 건 추가 연습 1.

| 시스템 | 태스크 (prio, 주기) | 심은 버그 | 기대 진단 |
|---|---|---|---|
| S1 | CTRL(3, 1 ms) · COMM(2, 5 ms) · LOG(1, 10 ms), `spi` 락 = 상속 없는 이진 세마포어 | LOG 가 spi 쥔 채 COMM 에 밀림 | `PRIORITY_INVERSION` |
| S2 | CTRL(3, 1 ms) · TELEM(1, 10 ms), ISR `UART_RX` 40회 버스트 (50 µs 간격, 40 µs 씩) | 파싱·CRC 를 ISR 안에서 | `ISR_OVERLOAD` |
| S3 | CTRL(4, 1 ms, 매 주기 logq 에 1개) · COMM(2, 30 ms, 7 ms 버스트) · LOGGER(1, 이벤트), logq 깊이 2 | 소비자가 굶어 큐가 참 | `QUEUE_FULL_BLOCKING` |
| S4 | IMU(2, 0.5 ms) · BMS(3, 4 ms) · TELEM(1, 10 ms), 시작 t0 = 0xFFFFF000 (wrap) | "안전하니까" BMS 를 위로 | `WRONG_PRIORITY_RM` |
| S5 | CTRL(5) · LOGGER(4) · PARSE(3) · COMM(2) · LOG(1), PI 뮤텍스, ISR→큐→태스크 | 없음 (대조군) | 미스 0, 진단 0 |

### Part A — 타임라인 재구성과 규칙 4개

1. `unwrap32()` 는 Part B. 먼저 `tl_build()`: 이벤트를 훑으며 **실행 구간** `seg`(ISR 이 우선, 유실 구간은 아무에게도 안 줌), **블록 구간** `blk`(종류: 뮤텍스/보내기/받기/delay), **보유 구간** `hold`, **작업** `job`(출시·마감), **미스** `miss`(마감 값으로 작업과 짝)를 만든다.
2. 미스마다 규칙 4개: `rule_inversion`, `rule_isr`, `rule_queue`, `rule_rm`. 각 규칙은 "걸렸다/안 걸렸다" 만이 아니라 **근거 숫자**(누가, 몇 µs, 몇 %)를 `diag_t` 에 채운다. 하나도 안 걸리면 `UNKNOWN` (주어진 `analyze()`).
3. 테스트: S1–S4 는 원인이 **정확히 하나**, S5 는 0. S1 의 재구성 숫자(CTRL 이 300 µs 에 막혀 1900 µs 대기, 창 안 COMM 900 µs)까지 맞아야 한다.

### Part B — 함정: wrap, seed, 오탐, 유실, 잘린 트레이스

- 32비트 µs 는 71.6분마다 wrap 한다. `unwrap32()` 로 64비트로 펴고, **같은 시스템을 t0 = 0 과 0xFFFFFF00 에서 돌려 진단 숫자가 같아야** 한다.
- seed 1..8 (실행 시간 0–10 % 흔들림) 에서도 원인이 같고 대조군은 미스 0.
- 대조군의 **모든 작업 창**에 규칙을 강제로 돌려도 하나도 안 걸려야 한다 — 거기엔 진짜 뮤텍스 대기(유한 역전)와 ISR 실행이 있다.
- 창 안에 `DROP` 이 있으면 진단은 하되 "신뢰도 낮음". 트레이스가 중간부터 시작해 짝 없는 `DEADLINE_MISS` 는 세고 버린다.

### Part C — 수정 → 재시뮬레이션 → 진짜 FreeRTOS 트레이스

- `rm_assign()`, `queue_depth_for()`, `apply_fix()` 를 채운다. 진단 → 수정 → 같은 seed 로 다시 → **미스 0, ISR 유실 0, 진단 0** (seed 0..8 × 버그 4개).
- 대안 수정도 검사: 큐는 소비자 우선순위 올리기, ISR 은 **핸들러를 CTRL 위에 두면 여전히 미스** (그리고 4개 규칙 어느 것도 안 걸려 `UNKNOWN` — 추가 연습 4 가 이걸 잡는 규칙).
- 데모: FreeRTOS POSIX 포트로 S1·S3·S4 를 재현 (버그/수정 각각), 트레이스 파일을 `./solution 파일` 로 분석.

## 2. Problem (English)

> **Deadline-miss post-mortem from an RTOS trace.** You get a text trace (`# rtos-trace v1 unit=us`, one event per line: `<t_us> <EVENT> <name> [arg]`, 32-bit wrapping microsecond timestamps; task metadata lines `# task NAME prio=P period=US`). Events: RELEASE (arg = absolute deadline), SWITCH_IN/OUT, ISR_ENTER/EXIT, BLOCK (arg = lock name or `delay`), Q_SEND_BLOCK/Q_RECV_BLOCK, UNBLOCK, MUTEX_TAKE/GIVE, DEADLINE_MISS (arg = deadline), DROP.
>
> 1. `uint64_t unwrap32(uint64_t prev, uint32_t t)` — monotonic 64-bit time across one wrap.
> 2. `int tl_build(const trace_t *, tl_t *)` — rebuild CPU segments (ISR time is not task time; after DROP, time is unattributed until the next SWITCH_IN), block intervals, mutex-hold intervals, jobs, and misses (match each DEADLINE_MISS to its RELEASE by deadline value).
> 3. For each miss with window [release, deadline], implement four rules, each returning evidence:
>    - *Priority inversion*: the task was blocked on a lock owned by a lower-priority task while a task of intermediate priority ran ≥ 10 % of the window.
>    - *ISR overload*: ISR time ≥ 40 % of the window.
>    - *Queue-full blocking*: the task blocked sending to a full queue while the queue's consumer (a task seen blocking on receive from it) ran < 20 % of that interval; report who ran instead and how long the consumer starved.
>    - *Wrong priority*: a task with higher priority **and** longer period ran inside the window.
> 4. `int apply_fix(sys_t *, const tl_t *, const diag_t *, int variant)` — enable priority inheritance / move ISR work into a handler task below the victim / size the queue as ⌈starve / producer period⌉ + 1 (or raise the consumer above the starving task) / reassign priorities rate-monotonically. Re-simulate: zero misses.
>
> Example: S1, seed 0 → `MISS CTRL window [200, 1200] us -> PRIORITY_INVERSION: CTRL(p3) blocked on spi for 1900 us; owner LOG(p1); meanwhile COMM(p2) ran 900 us`. With inheritance: 0 misses.
>
> Follow-ups: how do you tell bounded from unbounded inversion in a trace? Why can deferring ISR work still miss? How deep should the queue be, and when is depth the wrong fix? What does your analyzer get wrong?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/RTOS-A09_st && /tmp/RTOS-A09_st   # 21/56 passed, exit 1
/tmp/RTOS-A09_st --emit 1          # S1 트레이스를 그대로 보기 (시뮬레이터는 주어진 코드라 starter 에서도 동작)
```

주어진 것: 시스템 표(`sys_make`), 시뮬레이터(`sim_run` — 결정적, 10 µs 슬롯, 고정 우선순위 선점, FreeRTOS 처럼 "깨우기만 하고 깨어난 쪽이 다시 take"), 파서(`trace_parse`, A06 축약 사본), 도우미(`run_in`, `hold_owner_at`, `count_ev`), 미스마다 규칙을 돌리는 `analyze()`, 리포트·타임라인 출력, 하네스. TODO 는 8개: 1 `unwrap32`, 2 `tl_build`, 3–6 규칙 4개, 7 `rm_assign`/`queue_depth_for`, 8 `apply_fix`. starter 에서 PASS 하는 21개는 "시뮬레이터가 버그를 실제로 만든다 / 포맷이 맞다 / 대조군은 미스 0" 같은 주어진 부분이다.

## 4. 힌트

<details>
<summary>힌트 1 — tl_build: "직전 이벤트부터 지금까지" 의 시간을 먼저 나눠 주고 이벤트를 처리</summary>

이벤트 k 를 처리하기 **전에** `[last, e->t)` 를 누구에게 줄지 정한다: ISR 깊이 > 0 이면 맨 위 ISR, 아니면 유실 중이면 아무도, 아니면 `cur`. 그다음 이벤트로 상태를 바꾼다 (`SWITCH_IN` → cur, `ISR_ENTER` → push …). 같은 who 의 이어지는 구간은 합친다(`seg_add`). 블록은 태스크당 열린 것 하나(`open_blk[task]`), `UNBLOCK` 또는 그 태스크의 `SWITCH_IN`/`RELEASE` 에서 닫는다 — `UNBLOCK` 이 빠진 트레이스에서도 "돌고 있으면 막힌 게 아니다".

</details>

<details>
<summary>힌트 2 — 미스와 출시의 짝은 "마감 값"으로</summary>

`DEADLINE_MISS CTRL 1200` 의 1200 은 그 작업의 `RELEASE CTRL 1200` 인자와 같다. "그 태스크의 마지막 RELEASE" 로 짝지으면 FreeRTOS 데모처럼 미스를 늦게 적는 트레이스나, 작업이 다음 주기를 넘긴 경우에 틀린 창을 고른다. 인자는 32비트라 이벤트 시각 기준으로 부호 있는 차이를 더해 64비트로 편다 (`deadline64`).

</details>

<details>
<summary>힌트 3 — 역전 규칙: "막혔다" 만으로는 버그가 아니다</summary>

높은 X 가 낮은 L 이 쥔 락을 기다리는 건 **유한 역전**(bounded inversion) — L 의 임계 구역 길이만큼은 설계상 감수한다 (RTOS-M03, RTA 의 B 항). 버그는 그 사이에 **L 보다 높고 X 보다 낮은 M** 이 도는 것 (무한 역전). 그래서 조건은 "블록 구간 ∩ 창 안에서 prio(L) < prio(M) < prio(X) 인 M 의 실행 시간 ≥ 창의 10 %". 대조군 S5 의 CTRL 은 실제로 spi 에서 200 µs 넘게 막히지만 LOG 가 상속받아 돌므로 M 이 0 µs 다.

</details>

<details>
<summary>힌트 4 — 큐 규칙: 소비자는 트레이스에서 어떻게 아나</summary>

포맷엔 "받기 성공" 이벤트가 없다. 대신 소비자는 큐가 비었을 때 `Q_RECV_BLOCK LOGGER logq` 를 남긴다 — 그 큐에서 받으려다 막힌 적 있는 태스크가 소비자다. 굶주림 길이(`starve_us`)는 "소비자가 마지막으로 돈 끝 → 생산자 블록이 풀린 시각" 으로 잰다. 생산자 블록 구간만 재면 큐를 채우는 데 걸린 앞부분(깊이 × 주기)을 빼먹어 깊이를 작게 잡는다.

</details>

<details>
<summary>힌트 5 — wrap: 차이를 32비트에서 빼라</summary>

`prev64 + (uint32_t)(t32 - (uint32_t)prev64)`. 32비트 뺄셈은 모듈로 2^32 라 wrap 을 한 번 넘어도 양수 차이가 나온다 (RTOS-B06 의 `(int32_t)(now - wake)` 와 같은 원리). 전제: 이웃한 두 이벤트 간격 < 2^32 µs.

</details>

<details>
<summary>힌트 6 — ISR 수정의 우선순위: 비율이 아니라 마감</summary>

파싱 태스크를 "ISR 에서 온 일이니까 제일 높게" 두면 CTRL 은 여전히 4/5 를 빼앗긴다 (ISR 10 µs + 파싱 30 µs / 50 µs). 핸들러 우선순위는 그 일의 **마감**(명령 처리는 수 ms 안이면 된다)으로 정한다 → CTRL 아래. `variant 1` 이 일부러 틀린 쪽이다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨 (모델) | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 | 트레이스 이벤트 |
|---|---|---|---|---|
| `LOCK/UNLOCK`, `pi = 0` | `xSemaphoreCreateBinary()` + Take/Give (상속 없음) | `k_sem_take/give` | `osSemaphoreAcquire/Release` | `MUTEX_TAKE/GIVE`, `BLOCK T spi` |
| `LOCK/UNLOCK`, `pi = 1` | `xSemaphoreCreateMutex()` (semphr.h 735) → `xTaskPriorityInherit` (tasks.c 6745) | `k_mutex_lock/unlock` (상속) | `osMutexNew` + `osMutexPrioInherit` | 같음 + 주인의 `UNBLOCK` (재삽입) |
| `SEND/RECV` (블로킹) | `xQueueSend/Receive(…, portMAX_DELAY)` | `k_msgq_put/get(K_FOREVER)` | `osMessageQueuePut/Get(osWaitForever)` | `Q_SEND_BLOCK`, `Q_RECV_BLOCK` |
| ISR → 큐 (`isr.q`) | `xQueueSendFromISR` + `portYIELD_FROM_ISR` (RTOS-M02) | ISR 에서 `k_msgq_put(K_NO_WAIT)` | `osMessageQueuePut(…, 0)` | `ISR_ENTER/EXIT`, 소비자 `UNBLOCK` |
| 주기 작업 끝 → 다음 출시 | `xTaskDelayUntil` (RTOS-B06) | `k_timer` / `k_sleep` 절대 시각 | `osDelayUntil` | `BLOCK T delay`, `RELEASE` |
| 마감 검사 | 틱 훅 `vApplicationTickHook` (데모) | (앱 구현) | (앱 구현) | `DEADLINE_MISS` |
| 트레이스 훅 | `traceTASK_SWITCHED_IN` 등 (FreeRTOS.h 635–878) | tracing 서브시스템 (`CONFIG_TRACING`) | Event Recorder (Keil, 미검증) | 전부 |

### Part A — 타임라인: "누가 CPU 를 썼나" 를 구간으로

규칙은 전부 세 개의 질문으로 만들어진다: **창 안에서 누가 얼마나 돌았나**(`run_in`), **X 는 무엇 때문에 언제부터 언제까지 막혔나**(`blk`), **그때 락의 주인은 누구였나**(`hold_owner_at`). `tl_build` 는 그 재료를 한 번에 만든다.

| 이벤트 | 시간 귀속 (직전 구간) 다음에 하는 일 |
|---|---|
| `SWITCH_IN X` | `cur = X`, 유실 해제, X 의 열린 블록 닫기 |
| `SWITCH_OUT X` | `cur == X` 면 `cur = -1` |
| `ISR_ENTER I` / `ISR_EXIT` | ISR 스택 push / pop — 그동안 시간은 ISR 몫 |
| `RELEASE X d` | 작업 추가 (d 를 64비트로), 열린 `delay` 블록 닫기, 출시 간격 최솟값 = 주기 추정 |
| `DEADLINE_MISS X d` | 같은 d 의 작업을 찾아 미스 추가 (없으면 `unmatched++`) |
| `BLOCK` / `Q_*_BLOCK` | 블록 열기 (종류: `delay`·`delay_until` → DELAY, 그 외 이름 → MUTEX) |
| `UNBLOCK X` | X 의 열린 블록 닫기 (없으면 무시 — FreeRTOS 는 상속 때도 이걸 남긴다) |
| `MUTEX_TAKE/GIVE` | 보유 구간 열기/닫기 (주인 확인) |
| `DROP` | 유실 시작: `cur = -1`, ISR 스택 비움, 다음 `SWITCH_IN` 까지 미귀속 |

S1 (seed 0) 의 진짜 트레이스 앞부분과 분석기가 재구성한 타임라인 (`./solution -v`, 한 칸 = 50 µs, R 실행 / B 락·큐 대기 / r 출시됐는데 대기):

```text
0 RELEASE LOG 10000          200 RELEASE CTRL 1200       300 BLOCK CTRL spi         1800 BLOCK COMM delay
0 SWITCH_IN LOG              200 SWITCH_OUT LOG          300 SWITCH_OUT CTRL        1800 SWITCH_OUT COMM
0 MUTEX_TAKE LOG spi         200 SWITCH_IN CTRL          300 SWITCH_IN COMM         1800 SWITCH_IN LOG
                             250 RELEASE COMM 5250       1200 DEADLINE_MISS CTRL 1200   2200 MUTEX_GIVE LOG spi
                                                                                    2200 UNBLOCK CTRL spi
-- S1 timeline (seed 0)
  us/50  :|         |         |         |         |          (from t=0 us)
  CTRL    :....RRBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBRR..
  COMM    :.....rRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR............
  LOG     :RRRRrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrRRRRRRRRrrRR
```

같은 시스템에 상속을 켠 트레이스(`./solution --emit 1 0 fixed`)와 나란히 그리면 원인이 그림 하나로 보인다 (두 트레이스의 실제 시각으로 그림):

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">S1 버그: spi = 이진 세마포어 (상속 없음)</text>
  <text x="10" y="49" fill="currentColor">CTRL p3</text>
  <text x="10" y="74" fill="currentColor">COMM p2</text>
  <text x="10" y="99" fill="currentColor">LOG p1</text>
  <rect x="135" y="36" width="27.5" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="162.5" y="36" width="522.5" height="18" class="f-warn" stroke="currentColor"/>
  <text x="300" y="49" fill="currentColor">spi 대기 1900 µs (주인 LOG)</text>
  <rect x="685" y="36" width="27.5" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="162.5" y="61" width="412.5" height="18" class="f-acc" stroke="currentColor"/>
  <text x="300" y="74" fill="currentColor">COMM 1500 µs — 무한 역전의 정체</text>
  <rect x="80" y="86" width="55" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="575" y="86" width="110" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="712.5" y="86" width="27.5" height="18" class="f-acc" stroke="currentColor"/>
  <line x1="410" y1="30" x2="410" y2="110" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="414" y="122" fill="currentColor">CTRL 마감 1200 → 미스</text>
  <line x1="80" y1="128" x2="740" y2="128" stroke="currentColor"/>
  <text x="76" y="142" fill="currentColor">0</text><text x="238" y="142" fill="currentColor">600</text>
  <text x="400" y="142" fill="currentColor">1200</text><text x="563" y="142" fill="currentColor">1800</text>
  <text x="710" y="142" fill="currentColor">2400 µs</text>
  <text x="10" y="168" fill="currentColor" font-weight="bold" font-size="13">S1 수정: spi = 뮤텍스 (우선순위 상속)</text>
  <text x="10" y="199" fill="currentColor">CTRL p3</text>
  <text x="10" y="224" fill="currentColor">COMM p2</text>
  <text x="10" y="249" fill="currentColor">LOG p1</text>
  <rect x="135" y="186" width="27.5" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="162.5" y="186" width="110" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="272.5" y="186" width="27.5" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="410" y="186" width="55" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="685" y="186" width="55" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="300" y="211" width="110" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="465" y="211" width="220" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="80" y="236" width="55" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="162.5" y="236" width="110" height="18" class="f-ok" stroke="currentColor"/>
  <text x="280" y="249" fill="currentColor">LOG 이 p3 을 물려받아 임계 구역을 끝냄 (300→700)</text>
  <line x1="410" y1="180" x2="410" y2="260" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="304" y="200" fill="currentColor">끝 800</text>
  <line x1="80" y1="266" x2="740" y2="266" stroke="currentColor"/>
  <text x="10" y="290" fill="currentColor">색: 실행 = 강조, spi 대기 = 경고, 상속받은 우선순위로 실행 = 초록. 점선 = CTRL 첫 작업 마감 1200 µs.</text>
</svg>
```

### Part A — 규칙 4개: 조건, 문턱값, 근거

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <rect x="200" y="8" width="340" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="214" y="26" fill="currentColor" font-size="13">DEADLINE_MISS X → 창 W = [release, deadline]</text>
  <text x="214" y="42" fill="currentColor">규칙 4개를 모두 돌린다 (여러 개 걸리면 다 적는다)</text>
  <rect x="20" y="66" width="340" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="84" fill="currentColor" font-weight="bold">① PRIORITY_INVERSION</text>
  <text x="32" y="102" fill="currentColor">X 가 락 대기 ∩ W, 그때 주인 L: prio(L) &lt; prio(X)</text>
  <text x="32" y="118" fill="currentColor">prio(L) &lt; prio(M) &lt; prio(X) 인 M 이 그 사이 실행</text>
  <text x="32" y="134" fill="currentColor">M 실행 ≥ |W| × 10 %  (유한 역전은 통과)</text>
  <rect x="380" y="66" width="340" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="392" y="84" fill="currentColor" font-weight="bold">② ISR_OVERLOAD</text>
  <text x="392" y="102" fill="currentColor">W 안 ISR 실행 시간 합 ≥ |W| × 40 %</text>
  <text x="392" y="118" fill="currentColor">근거: 가장 많이 돈 ISR, 진입 횟수, µs, %</text>
  <text x="392" y="134" fill="currentColor">(ISR 시간은 태스크 몫에서 이미 빠져 있다)</text>
  <rect x="20" y="150" width="340" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="168" fill="currentColor" font-weight="bold">③ QUEUE_FULL_BLOCKING</text>
  <text x="32" y="186" fill="currentColor">X 가 Q_SEND_BLOCK q ∩ W, 소비자 C = q 에서</text>
  <text x="32" y="202" fill="currentColor">받으려다 막힌 적 있는 태스크, C 실행 &lt; 20 %</text>
  <text x="32" y="218" fill="currentColor">근거: 대신 돈 Y, 막힌 시간, 굶주림 길이</text>
  <rect x="380" y="150" width="340" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="392" y="168" fill="currentColor" font-weight="bold">④ WRONG_PRIORITY_RM</text>
  <text x="392" y="186" fill="currentColor">prio(Y) &gt; prio(X) 이고 period(Y) &gt; period(X)</text>
  <text x="392" y="202" fill="currentColor">인 Y 가 W 안에서 실행 (주기: 메타 또는</text>
  <text x="392" y="218" fill="currentColor">RELEASE 간격 최솟값)</text>
  <text x="20" y="250" fill="currentColor">아무것도 안 걸리면 UNKNOWN → 타임라인을 손으로 (추가 연습 3 실행 시간 초과, 4 이벤트 구동 상위 태스크)</text>
</svg>
```

| 규칙 | 왜 이 문턱값인가 | 일부러 빼먹은 것 |
|---|---|---|
| 역전 10 % | 유한 역전(L 의 임계 구역)은 정상. M 이 창의 의미 있는 몫을 먹어야 "무한" — S1 은 100 % | 연쇄 역전(L 이 또 다른 락 대기), 여러 M 의 합 |
| ISR 40 % | 정상 시스템 S5 의 ISR 은 버스트 중에도 창의 10 % 이하. S2 는 80 % | 짧은 창의 ISR 지연(지터) 자체 — A04 의 ISR 간섭 항으로 |
| 큐 20 % | 소비자가 조금이라도 돌았다면 "느린 소비자" 문제(용량)일 수 있다 → 다른 처방 | 큐가 여러 생산자를 가질 때 누가 채웠나 |
| RM | RM 은 D = T 고정 우선순위에서 최적 (Liu & Layland 1973) — 주기 긴 태스크가 위에 있으면 의심 1순위 | D < T(DM), 중요도 기반 설계의 의도된 예외 (Q5) |

실제 출력 (`./solution -v`):

```text
== S1 SPI shared by CTRL and LOG (binary-semaphore lock): 3 miss(es), 3 diagnosis line(s)
  MISS CTRL   window [200, 1200] us  -> PRIORITY_INVERSION
       CTRL(p3) blocked on spi for 1900 us; owner LOG(p1); meanwhile COMM(p2) ran 900 us (100% of the wait in window)
== S2 UART RX burst parsed inside the ISR: 3 miss(es), 3 diagnosis line(s)
  MISS CTRL   window [2000, 3000] us  -> ISR_OVERLOAD
       ISR time 800 us = 80% of window; top ISR UART_RX entered 20 times
== S3 log queue fills while LOGGER is starved by COMM: 1 miss(es), 1 diagnosis line(s)
  MISS CTRL   window [7000, 8000] us  -> QUEUE_FULL_BLOCKING
       CTRL blocked sending to logq for 5400 us; consumer LOGGER(p1) got 0% of CPU in window; COMM(p2) ran 850 us; consumer starved 8270 us
== S4 BMS (4 ms) above IMU (0.5 ms): RM order violated: 5 miss(es), 5 diagnosis line(s)
  MISS IMU    window [4294963200, 4294963700] us  -> WRONG_PRIORITY_RM
       BMS(p3, period 4000 us) outranks IMU(p2, period 500 us) and ran 400 us (80% of window)
  MISS IMU    window [4294967200, 404] us  -> WRONG_PRIORITY_RM
       BMS(p3, period 4000 us) outranks IMU(p2, period 500 us) and ran 400 us (80% of window)
== S5 healthy control (PI mutex, deferred ISR, RM order, deep queue): 0 miss(es), 0 diagnosis line(s)
```

S4 의 두 번째 창 `[4294967200, 404]` 가 wrap 을 지나는 창이다 — 리포트는 원본에서 grep 할 수 있게 32비트 값 그대로 찍고, 계산은 64비트로 한다.

### Part B — 함정

**wrap.** `unwrap32(0xFFFFFF00, 0x10) = 0x1_0000_0010` 처럼 차이를 32비트로 뺀다. 검사는 두 겹이다: 단위 테스트 3개, 그리고 S1–S4 를 t0 = 0 과 t0 = 0xFFFFFF00 에서 각각 돌려 **원인·µs·블록 시간·창 오프셋이 전부 같은지**. 파서는 unwrap 이 틀려도 시각을 단조로 고정해(크래시 없이) starter 가 멈추지 않게 했다.

**seed.** seed ≥ 1 은 RUN 길이를 0–10 % 늘린다 (xorshift32). 원인이 하나로 고정되는지 9 × 5 = 45 회 확인한다. 규칙 문턱값이 S1 100 % / S2 80 % / S3 0 % / S4 80 % 처럼 경계에서 멀어서 흔들림에 강하다 — 실제 현장 트레이스에서 문턱값 근처 값이 나오면 "판정 보류" 가 정직한 답이다.

**오탐.** 미스가 없으면 규칙이 안 도니 대조군 0 은 당연하다. 그래서 대조군(seed 3)의 **39개 작업 창 전부**를 가짜 미스로 넘겨 규칙을 강제로 돌린다. 그 트레이스엔 CTRL 의 spi 대기가 3번(예: 300→530 µs), ISR 실행도 있다 — 그래도 0 개가 걸려야 한다. 역전 규칙이 "막혔다" 가 아니라 "중간 M 이 돌았다" 를 보는 이유가 여기서 드러난다: 상속 덕분에 대기 동안 돈 것은 LOG(p5 상속)뿐이다.

**유실과 잘린 트레이스.** S1 트레이스의 500 µs 앞에 `500 DROP TRACE 7` 을 끼우면 `ngap = 1`, 첫 진단에 "trace gap in window: low confidence", 둘째 진단은 깨끗하다. 1100 µs 부터 잘라 내면 첫 작업의 `RELEASE` 가 없어 그 미스는 `unmatched = 1` 로 세고 버리고, 남은 2개만 역전으로 진단한다. 없는 증거로 원인을 지어내지 않는 것이 사후 분석기의 첫째 덕목이다.

### Part C — 수정과 재검증

| 원인 | `apply_fix` | 다시 돌린 결과 (seed 0..8) | 왜 고쳐졌나 (RTA, RTOS-M11/A04 식) |
|---|---|---|---|
| 역전 (S1) | `spi.pi = 1` | 미스 3 → 0 | CTRL 블로킹 B = LOG 임계 구역 ≤ 660 µs (60 슬롯 + 10 %), R = C 200 + B 660 = 860 ≤ 1000 µs |
| ISR (S2) | ISR 비용 40 → 10 µs + `rxq`(64) + PARSE(p2, 30 µs/바이트) | 미스 3 → 0, 유실 0 | CTRL: R = 300 + ⌈R/50⌉·10 → 380 µs ≤ 1000 |
| 큐 (S3) | 깊이 = ⌈8270 / 1000⌉ + 1 = **10** (대안: LOGGER prio = COMM + 1 = 3) | 미스 1 → 0 (두 방법 모두) | 버스트 동안 쌓이는 9개를 담음 / 소비자가 COMM 을 선점 |
| RM (S4) | `rm_assign`: IMU 3, BMS 2, TELEM 1 | 미스 5 → 0 | R_IMU = 120, R_BMS = 1360, R_TELEM = 2720 µs (각 마감 안) |

```text
   fix S1 SPI shared by CTRL and LOG (binary-semaphore lock) misses 3 -> 0
   fix S2 UART RX burst parsed inside the ISR             misses 3 -> 0
   fix S3 log queue fills while LOGGER is starved by COMM misses 1 -> 0
   fix S4 BMS (4 ms) above IMU (0.5 ms): RM order violated misses 5 -> 0
== S2 with handler ABOVE CTRL (wrong fix): 3 miss(es), 3 diagnosis line(s)
  MISS CTRL   window [2000, 3000] us  -> UNKNOWN
       no rule matched — look at the timeline by hand
```

마지막 두 줄이 이 레슨에서 가장 중요한 실패다. ISR 일을 태스크로 옮기고 그 태스크를 CTRL **위**(p4)에 두면 ISR 비율은 80 % → 20 % 로 내려가 ISR 규칙이 안 걸린다. 그런데 CTRL 의 RTA 는 R = 300 + ⌈R/50⌉·(10 + 30) — 300 → 540 → 740 → 900 → 1020 µs 로 마감 1000 을 넘고, 실제로 미스가 그대로다. "인터럽트 처리 = 제일 높게" 는 비율(rate)로 우선순위를 준 것이고, 맞는 기준은 **마감**(DM)이다. 그리고 분석기의 4개 규칙이 이걸 못 잡는다 — 규칙 기반 진단의 한계를 정직하게 `UNKNOWN` 으로 낸다 (추가 연습 4 가 다섯째 규칙).

큐 깊이의 근거: 굶주림 8270 µs 를 생산 주기 1000 µs 로 나눠 올림 = 9개가 쌓이고, +1 은 반올림·측정 경계 여유다. 블록 시간 5400 µs 만 쓰면 ⌈5.4⌉ + 1 = 7 — 블록 전에 큐를 채운 2주기를 빼먹어 seed 에 따라 다시 막힌다. 현장에서는 측정 최댓값에 ×1.5–2 여유를 더 둔다. 그리고 깊이는 **버스트가 유한할 때만** 처방이다: COMM 이 영원히 돌면 어떤 깊이도 결국 찬다 — 그땐 소비자 우선순위(대안)나 COMM 의 예산 제한(RTOS-A05 서버)이 답이다.

### Part C — 진짜 FreeRTOS 에서 같은 진단

<!-- code: FreeRTOSConfig.h -->

<!-- code: demo_freertos_deadline.c -->

`FreeRTOSConfig.h` 의 trace 매크로가 커널을 한 줄도 안 고치고 같은 포맷을 쓴다. 핵심 대응:

| 트레이스 이벤트 | FreeRTOS 훅 (V11.3.2) | 비고 |
|---|---|---|
| `SWITCH_OUT/IN` | `traceTASK_SWITCHED_OUT/IN` (tasks.c 5228/5274, 첫 태스크 3809) | 같은 태스크로 돌아오는 쌍(틱마다 생김)은 버린다 |
| `BLOCK T spi` / `Q_RECV_BLOCK` | `traceBLOCKING_ON_QUEUE_RECEIVE` (queue.c 1787 세마포어, 1615 큐) | 락/데이터 큐 구분은 데모의 객체 표로 |
| `Q_SEND_BLOCK` | `traceBLOCKING_ON_QUEUE_SEND` (queue.c 1125) | |
| `MUTEX_TAKE` / `MUTEX_GIVE` | `traceQUEUE_RECEIVE` (1698) / `traceQUEUE_SEND` (979) on 락 객체 | 비재귀 뮤텍스 전용 훅은 없다 (재귀만 `traceTAKE_MUTEX_RECURSIVE`) |
| `UNBLOCK T` | `traceMOVED_TASK_TO_READY_STATE` (tasks.c 289, `prvAddTaskToReadyList`) | 상속(6791)·해제(6895) 때도 불린다 → 분석기는 무시 |
| `BLOCK T delay` | `traceTASK_DELAY_UNTIL` (tasks.c 2472) | |
| `RELEASE` / `DEADLINE_MISS` | `vApplicationTickHook` (tasks.c 5006) 안에서 데모가 판정 | 마감 먼저, 출시 다음 (모델과 같은 순서) |

블로킹 경로에서 `traceBLOCKING_ON_QUEUE_RECEIVE` 가 상속보다 먼저 불린다 (queue.c 1787 → 1795 `xTaskPriorityInherit`). POSIX 포트의 "실행 시간" 은 틱 회계(틱이 왔을 때 돌던 태스크의 카운터 +1)로 흉내 냈고, 1 tick = 10 ms, 타임스탬프 = tick × 10000 + 틱 이후 경과(0–9999 µs 로 자름) 다 — 실시간이 아니라 **순서와 틱 단위 시간**만 믿을 수 있다. S2(ISR 과다)는 재현하지 않았다: POSIX 포트의 ISR 은 SIGALRM 핸들러라, 그 안에서 쓴 CPU 를 틱 회계가 태스크 몫으로 센다.

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_deadline.c \
   $K/{tasks,list,queue}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-A09_demo
for s in inv inv_fix rm rm_fix queue queue_fix; do /tmp/RTOS-A09_demo $s /tmp/RTOS-A09_work/final_$s.txt; done
/tmp/RTOS-A09_so /tmp/RTOS-A09_work/final_*.txt
```

실제 출력 (데모 쪽 요약은 bug 3개만 옮김, 수정 3개는 모두 `0 deadline misses`):

```text
/tmp/RTOS-A09_work/final_inv.txt: 77 events, 1 deadline misses (tick 0..60)
  CTRL   prio 3  cpu ticks 15
  COMM   prio 2  cpu ticks 11
  LOG    prio 1  cpu ticks 7
/tmp/RTOS-A09_work/final_rm.txt: 79 events, 2 deadline misses (tick 0..60)
/tmp/RTOS-A09_work/final_queue.txt: 126 events, 1 deadline misses (tick 0..70)

final_inv.txt: 77 events, 0 bad lines, 1 misses (0 unmatched), causes: PRIORITY_INVERSION
  MISS CTRL   window [70001, 170000] us  -> PRIORITY_INVERSION
       CTRL(p3) blocked on spi for 139998 us; owner LOG(p1); meanwhile COMM(p2) ran 89993 us (99% of the wait in window)
final_inv_fix.txt: 87 events, 0 bad lines, 0 misses (0 unmatched), causes: none
final_rm.txt: 79 events, 0 bad lines, 2 misses (0 unmatched), causes: WRONG_PRIORITY_RM
  MISS IMU    window [50001, 100000] us  -> WRONG_PRIORITY_RM
       BMS(p3, period 400000 us) outranks IMU(p2, period 50000 us) and ran 49999 us (100% of window)
  MISS IMU    window [450000, 500000] us  -> WRONG_PRIORITY_RM
       BMS(p3, period 400000 us) outranks IMU(p2, period 50000 us) and ran 49999 us (99% of window)
final_rm_fix.txt: 102 events, 0 bad lines, 0 misses (0 unmatched), causes: none
final_queue.txt: 126 events, 0 bad lines, 1 misses (0 unmatched), causes: QUEUE_FULL_BLOCKING
  MISS CTRL   window [210001, 260000] us  -> QUEUE_FULL_BLOCKING
       CTRL blocked sending to logq for 170009 us; consumer LOGGER(p1) got 0% of CPU in window; COMM(p2) ran 39996 us; consumer starved 319990 us
final_queue_fix.txt: 160 events, 0 bad lines, 0 misses (0 unmatched), causes: none
```

6개 시나리오 × 5회 반복(부하 평균 약 16)에서 진단 결과가 30번 모두 같았다. 상속이 켜진 `final_inv_fix.txt` 에는 커널이 남긴 상속의 흔적이 보인다:

```text
80004 BLOCK CTRL spi
80004 UNBLOCK LOG          <- xTaskPriorityInherit 가 LOG 를 p3 ready 리스트로 옮김 (tasks.c 6791)
80006 SWITCH_OUT CTRL
80006 SWITCH_IN LOG
120002 MUTEX_GIVE LOG spi
120002 UNBLOCK LOG         <- xTaskPriorityDisinherit 가 p1 로 되돌림 (tasks.c 6895, queue.c 2413 에서 호출)
120002 UNBLOCK CTRL
```

호환성 확인: 작성 시점의 RTOS-A06 `solution.c --dump` 트레이스(528 이벤트, `delay_until` 사용)를 이 파서로 읽으면 `0 bad lines`, 미스 0 → 진단 없음.

### 사고 보고서 1 — 제어 루프가 10 ms 마다 마감을 놓친다 (S1)

- **증상**: CTRL(1 kHz) 이 10 ms 에 한 번씩 정확히 미스 (30 ms 에 3회). 미스 때마다 CTRL 응답 2100 µs.
- **트레이스 근거**: `300 BLOCK CTRL spi`, spi 주인은 `0 MUTEX_TAKE LOG spi` 의 LOG(p1). 대기 1900 µs 동안 COMM(p2) 이 300→1800 µs 실행(창 안 900 µs = 대기의 100 %). `2200 MUTEX_GIVE LOG spi` 직후 CTRL 재개.
- **원인**: spi 락이 이진 세마포어라 상속이 없다 → 중간 우선순위 COMM 이 LOG 를 밀어 **무한 역전** (RTOS-M03 의 Pathfinder 와 같은 구조).
- **수정**: `xSemaphoreCreateMutex()` (상속). 근본적으로는 LOG 의 SPI 임계 구역을 256 B 조각으로 줄여 B 항 자체를 작게 (RTOS-M11 Part C).
- **재검증**: 모델 seed 0..8 미스 0, FreeRTOS `inv_fix` 미스 0, 트레이스에 상속 흔적 `UNBLOCK LOG` 2회. RTA: R = 200 + B(≤ 660) ≤ 1000 µs.

### 사고 보고서 2 — 호스트 명령이 몰리면 제어 루프가 늦는다 (S2)

- **증상**: UART 명령 버스트(10 ms 마다 2 ms) 때만 CTRL 미스, 버스트당 1회.
- **트레이스 근거**: 창 [2000, 3000] µs 에서 ISR 800 µs = 80 %, `UART_RX` 진입 20회 (50 µs 간격, 각 40 µs). 같은 창 다른 태스크 실행 없음.
- **원인**: 파싱·CRC 를 ISR 안에서 → 모든 태스크 위에서 CPU 의 4/5 를 가져감.
- **수정**: ISR 은 바이트를 큐에 넣고 끝(10 µs), 파싱은 PARSE 태스크를 **CTRL 아래**(p2)에 (RTOS-M02 지연 처리). 위에 두면 그대로 미스 — 위 출력.
- **재검증**: 미스 3 → 0, rxq 유실 0. RTA(ISR 을 최소 간격 50 µs 의 최상위 sporadic 으로, RTOS-A04 축약): R_CTRL = 380 µs.

### 사고 보고서 3 — OTA 중 로그 큐가 차서 제어 루프가 멈춘다 (S3)

- **증상**: COMM 의 OTA 조각 검증(7 ms) 때 CTRL 이 한 번 미스하고, 막힌 작업이 12.6 ms 에야 끝나 8–12 ms 의 출시 5번이 건너뛰어짐(오버런).
- **트레이스 근거**: `7150 Q_SEND_BLOCK CTRL logq`, 소비자 LOGGER(처음에 `Q_RECV_BLOCK LOGGER logq`) 는 창 안 실행 0 %, 대신 COMM(p2) 850 µs. 블록 5400 µs, LOGGER 마지막 실행 끝 → 블록 해제 = 8270 µs 굶주림.
- **원인**: 깊이 2 인 큐 + 소비자 우선순위가 버스트 태스크보다 낮음 → 생산자(최상위 CTRL)가 `portMAX_DELAY` 로 보내다 막힘. 제어 루프가 로깅에 **블로킹 의존**한 설계 결함이기도 하다.
- **수정**: 깊이 10 (= ⌈8270/1000⌉ + 1) 또는 LOGGER 를 p3. 더 근본: CTRL 은 `xQueueSend(…, 0)` 로 보내고 실패하면 드롭 카운터만 올린다 — 로그 때문에 제어가 멈추면 안 된다.
- **재검증**: 두 방법 모두 seed 0..8 미스 0, FreeRTOS `queue_fix`(LOGGER p3) 미스 0.

### 사고 보고서 4 — IMU 샘플이 4 ms 마다 빠진다 (S4)

- **증상**: IMU(2 kHz) 가 4 ms 주기로 미스 (20 ms 에 5회), 미스 시각이 BMS 출시와 일치.
- **트레이스 근거**: 창 [0, 500] µs 마다 BMS(p3, 주기 4000 µs) 가 400 µs = 80 % 실행. IMU(p2) 주기 500 µs. 창이 32비트 wrap 을 지나도 같은 숫자.
- **원인**: 우선순위를 중요도("배터리 안전")로 줬다 → RM 순서 위반. 위반 상태 RTA: R_IMU = 120 + 1000 µs > 500.
- **수정**: RM 재할당 (IMU 3, BMS 2, TELEM 1). BMS 의 안전 요구는 우선순위가 아니라 **마감과 감시**(워치독, 마감 모니터)로 보장한다.
- **재검증**: 미스 5 → 0, RTA R = 120 / 1360 / 2720 µs, FreeRTOS `rm_fix` 미스 0.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/RTOS-A09_so && /tmp/RTOS-A09_so   # 56/56 passed (5회 동일)
/tmp/RTOS-A09_so -v                                                                           # 리포트 + 타임라인
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/RTOS-A09_san && /tmp/RTOS-A09_san   # 56/56, 보고 없음
/tmp/RTOS-A09_so FILE...                                                                      # 트레이스 파일 분석
```

실행 시간은 0.02 s 안쪽이다 (시뮬레이션 수십 번 + 분석).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ISR 시간을 "현재 태스크" 몫으로 셈 | S2 에서 CTRL 이 창 내내 돈 것처럼 보여 "CTRL 이 느리다(WCET)" 로 오진 | 귀속 순서: ISR 깊이 > 유실 > `cur` |
| 미스를 "그 태스크의 마지막 RELEASE" 와 짝지음 | 미스를 늦게 적는 트레이스(FreeRTOS 데모)나 오버런에서 엉뚱한 창 | `RELEASE` 인자(마감) == `DEADLINE_MISS` 인자로 짝 |
| "높은 태스크가 락을 기다림" = 역전 버그 | 유한 역전은 정상 (상속이 있어도 생긴다) → 대조군 오탐 | 중간 M 의 실행 ≥ 10 % 조건 |
| 락 주인을 "블록 해제 때 give 한 태스크" 로 | 해제 시각엔 이미 주인이 바뀌었을 수 있다 | 블록 **시작** 시각의 보유 구간 (`hold_owner_at(obj, b->a)`) |
| 큐 깊이를 생산자 블록 시간만으로 | 블록 전에 큐를 채운 시간을 빼먹어 다시 찬다 | 굶주림 = 소비자 마지막 실행 끝 → 블록 해제 |
| wrap 을 `if (t < prev) t += 1<<32` 로 | 같은 시각 재정렬·보정과 섞이면 2번 더한다 | `prev + (uint32_t)(t32 - (uint32_t)prev)` |
| `DROP` 뒤 시간을 마지막 태스크에 줌 | 모르는 시간을 아는 척 → 틀린 근거 | `gap = 1` 동안 미귀속, 창에 있으면 신뢰도 낮음 |
| ISR 핸들러 태스크를 최상위로 | 비율은 줄어도 CTRL 을 같은 만큼 굶김 (S2 variant 1) | 마감 기준 우선순위 (CTRL 아래) |
| `UNBLOCK` 이 오면 무조건 "깨어남" 처리 | FreeRTOS 는 상속/해제 때도 남긴다 (6791, 6895) | 열린 블록이 있을 때만 닫기 |
| 미스 1건에 규칙 하나만 보고 멈춤 | 원인이 겹친 시스템에서 둘째 원인을 놓침 | 규칙 4개 전부, 마스크로 합침 |

## 8. 면접 Q&A

<details>
<summary>Q1. 트레이스 한 장으로 데드라인 미스 원인을 찾는 순서를 말해 보세요.</summary>

① 미스한 작업의 **창**(출시 → 마감)을 정한다. ② 창 안 CPU 를 나눈다: 미스한 태스크 자신 / 더 높은 태스크 / ISR / 더 낮은 태스크. ③ 자신이 충분히 돌았는데 늦었으면 **실행 시간 초과**(WCET, RTOS-A03), 남이 먹었으면 **누가, 왜 그 우선순위인지**, 아예 못 돌았으면 **무엇에 막혔나**(락이면 주인과 그 사이 실행된 것, 큐면 소비자). ④ 가설을 하나로 좁히고, 그 가설이 맞다면 트레이스에 **있어야 할 다른 흔적**을 확인한다 (역전이면 주인이 그동안 거의 못 돌았어야 한다). ⑤ 고친 뒤 같은 조건으로 다시 돌려 미스 0 + RTA 로 왜 고쳐졌는지. 꼬리: "트레이스가 없으면?" → GPIO 토글 + 로직 분석기로 같은 창 정보를, DWT CYCCNT 로 실행 시간을 (RTOS-A03).

</details>

<details>
<summary>Q2. 트레이스에서 "유한 역전" 과 "무한 역전" 을 어떻게 구분하나요?</summary>

둘 다 "높은 X 가 낮은 L 의 락을 기다림" 으로 시작한다. 유한 역전은 그 대기 동안 L(또는 상속받은 L)만 돌고 대기 길이가 L 의 임계 구역 길이로 묶인다 — RTA 의 B 항이고 설계상 감수한다. 무한 역전은 대기 동안 **L 보다 높고 X 보다 낮은 M** 이 돈다 — 대기 길이가 M 의 실행 시간에 비례해 커진다 (RTOS-M03). 이 레슨의 규칙은 그래서 "M 이 창의 10 % 이상" 을 본다. 대조군 S5 의 CTRL 은 spi 에서 230 µs 를 기다리지만 그동안 돈 건 상속받은 LOG 뿐이라 걸리지 않는다. FreeRTOS 트레이스에서는 상속이 일어나면 주인 태스크의 ready 리스트 이동(`traceMOVED_TASK_TO_READY_STATE`, tasks.c 6791)이 바로 찍힌다 — 상속이 켜져 있는지 트레이스로 확인할 수 있다.

</details>

<details>
<summary>Q3. ISR 이 너무 오래 걸려서 일을 태스크로 옮겼는데도 제어 루프가 여전히 미스합니다. 왜일까요?</summary>

옮긴 태스크를 제어 루프보다 높은 우선순위에 뒀을 가능성이 크다. 그러면 CPU 를 먹는 주체가 ISR → 태스크로 이름만 바뀐다. S2 에서 핸들러를 p4 에 두면 ISR 비율은 80 % → 20 % 로 내려가지만 CTRL 은 50 µs 마다 40 µs 를 그대로 빼앗겨 미스가 3회 그대로다 (RTA: R = 300 + ⌈R/50⌉·40 → 1020 µs > 1000). 핸들러 우선순위는 그 일의 **마감**으로 정한다 — 명령 파싱은 수 ms 안이면 되니 1 ms 제어 루프 아래. 꼬리: "그러면 명령 응답이 늦지 않나?" → 버스트 동안 쌓이는 깊이(여기선 rxq 64)와 처리량(CTRL 이 쉬는 동안 PARSE 가 돔)을 계산해 마감 안인지 확인한다. 정말 빨라야 하는 한 바이트 처리(흐름 제어 등)만 ISR 에 남긴다. FreeRTOS 라면 그 ISR 은 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 아래여야 FromISR API 를 쓸 수 있다 (RTOS-M02).

</details>

<details>
<summary>Q4. 큐가 차서 생산자가 막혔습니다. 큐 깊이를 얼마로 늘리겠나요? 그 답이 틀리는 경우는?</summary>

소비자가 못 도는 최장 구간(굶주림) 동안 쌓이는 개수 + 여유: ⌈굶주림 / 생산 주기⌉ + 1. S3 은 8270 µs / 1000 µs → 10. 측정 기반이므로 현장에선 ×1.5–2 여유를 더 둔다. 틀리는 경우: ① 버스트가 유한하지 않으면(소비자가 평균적으로도 느리면, ρ = 도착률/처리율 ≥ 1) 어떤 깊이도 결국 찬다 — 그땐 소비자 우선순위나 처리량을 고친다. ② 측정한 버스트가 최악이 아니면 깊이도 최악이 아니다 (RTOS-A03: 측정 최댓값 ≠ WCET). ③ 메모리: 깊이 × 항목 크기만큼 RAM — 큰 항목은 포인터 + 풀로 (RTOS-B11). 그리고 근본 질문: **최상위 제어 태스크가 로깅에 블로킹 의존하면 안 된다** — `xQueueSend(q, &rec, 0)` 로 보내고 실패하면 드롭 카운터. "로그 때문에 모터가 멈췄다" 는 사고 보고서는 면접관이 가장 좋아하는 결론이다.

</details>

<details>
<summary>Q5. "BMS 는 안전에 중요하니 최고 우선순위" 가 왜 틀렸나요? 중요도로 우선순위를 주는 게 맞을 때는 없나요?</summary>

고정 우선순위에서 우선순위는 **"누가 먼저 CPU 를 받아야 모두가 마감을 지키나"** 를 정하는 도구다. D = T 면 주기가 짧을수록 높게(RM)가 최적이고 (Liu & Layland 1973), D < T 면 마감이 짧을수록(DM). 중요도는 "과부하일 때 누구를 살릴까" 의 문제라 다른 축이다. S4 에서 BMS 를 위로 두면 U = 0.59 로 넉넉한데도 IMU 가 미스한다. 맞을 때: 과부하가 실제로 올 수 있는 혼합 중요도(mixed-criticality) 시스템에서 중요한 태스크를 지키려고 의도적으로 RM 을 깨는 경우 — 그때는 **주기 변환**(period transformation: 긴 주기 작업을 잘게 쪼개 짧은 주기로 만들어 RM 순서와 중요도 순서를 맞춤, Sha et al. 계열 기법 — 미검증 인용) 이나 예산 서버(RTOS-A05)로 낮은 쪽을 보호한다. BMS 의 안전은 우선순위가 아니라 마감 모니터와 워치독으로 보장한다.

</details>

<details>
<summary>Q6. 트레이싱 자체가 타이밍을 바꾸지 않나요? 현장 장비에서는 어떻게 기록하나요?</summary>

바꾼다 (관찰자 효과). 그래서 ① 훅은 배열/링 버퍼에 고정 크기 레코드만 쓰고(이 데모의 `rec()` 도 printf 없이 배열에만), ② 타임스탬프는 싼 카운터(DWT CYCCNT, 타이머)에서, ③ 전송은 나중에 또는 낮은 우선순위로 한다 (RTOS-A06 의 overwrite 링 = flight recorder). 오버헤드는 이벤트당 수십~수백 사이클 수준이라는 게 상용 도구의 설명이다 (SEGGER SystemView / Percepio Tracealyzer 문서 기준, 미검증). 현장에서는 RAM 링 버퍼를 항상 켜 두고 **`DEADLINE_MISS` 나 워치독 직전에 고정(freeze)** 해서 플래시나 진단 포트로 빼낸다 — 미스 직전 수백 ms 가 바로 이 레슨의 창이다. 꼬리: "버퍼가 넘치면?" → overwrite 면 오래된 것이 사라지고(미스 직전은 남음), stop 이면 `DROP` 마커 — 분석기는 그 구간을 미귀속으로 둬야 한다 (Part B).

</details>

<details>
<summary>Q7. (스태프) 당신 분석기가 틀리는 경우는 어떤 것들이 있나요?</summary>

① **원인 겹침**: 역전 + ISR 이 동시에 있으면 두 규칙 다 걸리고 어느 쪽이 "결정적" 이었는지는 말해 주지 않는다 — 반사실(counterfactual) 질문이므로 하나씩 고쳐 다시 돌려야 안다 (그래서 Part C 의 재시뮬레이션이 진단의 일부다). ② **연쇄 역전**: L 이 또 다른 락에서 막힌 경우, 주인의 주인을 따라가야 한다 (RTOS-M04 연쇄 상속). ③ **메타가 틀리거나 없음**: 우선순위가 런타임에 바뀌거나(vTaskPrioritySet, 상속) 주기를 출시 간격으로 추정하면 sporadic 태스크에서 틀린다. ④ **D < T**: RM 규칙이 DM 이 맞는 시스템을 오진한다. ⑤ **멀티코어**: "누가 CPU 를 먹었나" 가 코어별이 아니면 전부 틀린다 (RTOS-A08). ⑥ **시간 해상도**: FreeRTOS POSIX 데모처럼 틱 단위면 1 틱보다 짧은 원인(짧은 ISR 폭주)은 안 보인다. ⑦ **4개 밖의 원인**: 실행 시간 초과(WCET), 이벤트 구동 상위 태스크, 캐시/플래시 대기, 스케줄러 잠금(`vTaskSuspendAll`) 구간 — 모두 `UNKNOWN` 이 정직한 출력이다 (추가 연습 3·4).

</details>

<details>
<summary>Q8. (스태프) 현장 장비에서 한 달에 한 번 마감 미스가 납니다. 재현이 안 되면 어떻게 바꾸겠나?</summary>

재현 대신 **증거를 남기게** 바꾼다. ① 마감 모니터: 주기 태스크마다 출시/완료 시각을 기록하고 틱 훅이나 하드웨어 타이머가 마감 초과를 감지 (이 데모의 `vApplicationTickHook` 방식) — 감지 시 카운터 + 트레이스 링 freeze. ② 항상 켜진 RAM 트레이스 링 (수 KB, overwrite) + 마지막 N ms 를 비휘발 영역에 저장. ③ 태스크별 최대 응답 시간·최대 실행 시간·최대 블록 시간 같은 **고수위(high-water) 통계**를 텔레메트리로 — 미스가 나기 전에 여유가 줄어드는 걸 본다 (추가 연습 2 의 near-miss). ④ 받은 트레이스를 이 분석기 같은 도구로 자동 분류해 원인별로 모은다. 그리고 분석 쪽: RTA 에 빠진 항(B, J, ISR — RTOS-A04)을 다시 확인하고, 측정한 최악값의 출처(입력 경로, 캐시, RTOS-A03)를 의심한다. 꼬리: "비용은?" → RAM 몇 KB, 훅당 수십 사이클, 플래시 쓰기는 미스 때만.

</details>

<details>
<summary>Q9. "고쳤다" 를 어떻게 증명하나요? 미스가 안 났다는 것만으로 충분한가요?</summary>

부족하다 — 미스가 안 난 건 그 실행에서 최악이 안 왔다는 뜻일 수도 있다. 세 겹으로 본다. ① 같은 조건 재실행: 이 레슨은 seed 0..8 × 버그 4개, 수정 후 미스 0 **그리고 분석기 진단 0** (규칙이 근거를 못 찾음). ② 분석: 수정이 RTA 의 어느 항을 바꿨는지 숫자로 — S1 은 B 가 "COMM 전체" 에서 "LOG 임계 구역" 으로, S4 는 간섭 항에서 BMS 가 빠짐. RTA 가 통과해야 "모든 위상에서" 안전이다 (시뮬레이션은 본 위상만). ③ 여유: 응답 시간 분포와 최소 slack 을 수정 전후로 비교 (추가 연습 2). 꼬리: "대안 수정이 둘이면 뭘 고르나?" → S3 처럼 깊이(RAM 비용, 버스트가 유한할 때만)와 우선순위(다른 태스크의 RTA 에 영향)의 비용을 비교해 고르고, 보통은 "제어 루프가 로그에 블로킹하지 않게" 가 둘 다보다 먼저다.

</details>

## 9. 추가 연습

### 연습 1 — 우선순위 메타 없이 순서 복원: `infer_prio_order()`

`# task … prio=` 줄이 없는 트레이스에서, 같은 시각 `SWITCH_OUT A` → `SWITCH_IN B` 인데 A 가 그 시각에 블록하지 않았다면 A 는 선점당했다 → B > A. 이 간선으로 위상 정렬해 우선순위 순서를 복원하라 (IDLE 제외). 같은 우선순위 time slicing 처럼 사이클이 생기면 −1.

<details>
<summary>답 보기</summary>

`blocked_at[A] == t` 로 "이 SWITCH_OUT 은 블록 때문" 을 거른다 (우리 포맷은 BLOCK 이 SWITCH_OUT 보다 먼저 온다). Kahn 알고리즘: 진입 차수 0 인 것을 차례로 꺼내고, 후보가 둘 이상인 단계가 있으면 순서가 유일하지 않다(`unique = 0`). 손 트레이스에서 LOG→CTRL(200), LOG→COMM(400), COMM→CTRL(1200) 간선 → CTRL > COMM > LOG, 유일. A↔B 를 서로 바꾸는 트레이스는 사이클 → −1. 한계: 상속 중에는 "물려받은 우선순위" 의 선점이 찍혀 간선이 틀릴 수 있고, 한 번도 경쟁하지 않은 두 태스크의 순서는 알 수 없다.

</details>

### 연습 2 — 미스 "전에" 경고: `job_responses()`

각 `RELEASE` 에서 다음 `BLOCK X delay` 까지를 응답 시간으로, 마감까지 남은 시간을 여유(slack)로 계산하라. 여유가 주기의 10 % 미만인 작업 수를 세어 near-miss 경고로 쓴다.

<details>
<summary>답 보기</summary>

CTRL(주기 1000 µs) 응답 100 / 950 / 300 → 최대 950, 여유 50 < 100 → 경고 1건. 이게 Q8 의 "고수위 통계" 다: 미스는 0 이어도 최소 여유가 줄어드는 추세가 먼저 보인다. A06 의 응답 시간 측정(RELEASE → BLOCK delay)과 같은 정의라 A06 데모 트레이스(`delay_until`)도 같은 함수로 읽는다.

</details>

### 연습 3 — 실행 시간 초과 규칙: `rule_overrun()`

미스한 작업의 창 안 자기 CPU 시간이, 같은 태스크의 다른 작업 CPU 시간 중앙값의 1.5배를 넘으면 `EXEC_OVERRUN` 으로 진단하라. 아무도 끼어들지 않았는데 미스한 경우를 잡는다.

<details>
<summary>답 보기</summary>

손 트레이스: CTRL 작업 CPU 100 / 90 / 110 µs, 넷째 작업은 창 안에서 1000 µs (끝은 1200 µs 뒤) → 중앙값 100, 1000 > 150 → 걸림. 평범한 작업(90 µs)은 안 걸림. 같은 창에서 LOG 실행 0 µs — 남 탓이 아니라 자기 실행 시간이 원인이다. 현장 원인 후보: 입력 의존 경로, 캐시/플래시 대기, 디버그 printf (RTOS-A03). 1.5배 문턱은 임의값이다 — 실제로는 측정 WCET 예산 대비로 판정한다.

</details>

### 연습 4 — 이벤트 구동 상위 태스크 규칙: `rule_event_hog()`

본문 S2 의 "틀린 수정"(PARSE 를 CTRL 위에)은 4개 규칙 어디에도 안 걸려 `UNKNOWN` 이었다. 주기 메타가 0(이벤트 구동)이고 X 보다 높은 태스크가 창의 50 % 이상 돌았으면 그 태스크를 반환하는 규칙을 만들어라.

<details>
<summary>답 보기</summary>

손 트레이스(S2 모양): 창 [2000, 3000] 에서 PARSE(p4, period 0) 630 µs = 63 %, ISR 은 20 µs = 2 % → ISR 규칙으론 안 잡히고 이 규칙으로 잡힌다. RM 규칙이 못 잡는 이유: 이벤트 구동 태스크는 주기가 없어 "주기가 더 길다" 를 판정할 수 없다. 처방은 Q3: 마감 기준으로 CTRL 아래로. 실제로는 이벤트 태스크의 최소 도착 간격(sporadic)을 알면 RM/DM 규칙에 넣을 수 있다 — A04 가 ISR 을 그렇게 다룬다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/RTOS-A09_pr && /tmp/RTOS-A09_pr   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/RTOS-A09_ps && /tmp/RTOS-A09_ps   # 11/11, 보고 없음
```

## 10. 핵심 정리

- 사후 분석 = **창(출시→마감) 안의 CPU 분배** 를 묻는 일: 자신 / 높은 태스크 / ISR / 막힘(무엇에, 주인은 누구, 그사이 누가).
- 타임라인 재구성 규칙: ISR 시간은 ISR 몫, `DROP` 뒤는 미귀속, 미스는 **마감 값**으로 출시와 짝, wrap 은 `prev + (uint32_t)(t - (uint32_t)prev)`.
- 역전 버그의 증거는 "막혔다" 가 아니라 **"막힌 동안 L 과 X 사이의 M 이 돌았다"**. 유한 역전은 정상(B 항).
- ISR 과다 → 일을 태스크로, 단 우선순위는 **마감**으로 (위에 두면 그대로 미스). 큐 포화 → 깊이 = ⌈굶주림/주기⌉+1 은 유한 버스트일 때만, 근본은 "제어 루프는 로그에 블로킹하지 않는다".
- RM 위반은 "주기 긴 태스크가 위에서 돌았다" 로 보인다. 안전 중요도는 우선순위가 아니라 마감 감시로.
- 고쳤다 = 같은 조건 재실행 미스 0 + 진단 0 + RTA 로 바뀐 항을 숫자로. 오탐 0(대조군 전 창)이 규칙의 품질 기준.
- 규칙이 모르는 원인은 `UNKNOWN` 으로 — 근거 없는 진단을 지어내지 않는다.

## 참고자료

- FreeRTOS-Kernel V11.3.2 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `include/FreeRTOS.h` 635 `traceTASK_SWITCHED_IN`, 666 `traceTASK_SWITCHED_OUT`, 676/685 `traceTASK_PRIORITY_(DIS)INHERIT`, 694 `traceBLOCKING_ON_QUEUE_RECEIVE`, 712 `traceBLOCKING_ON_QUEUE_SEND`, 730 `traceMOVED_TASK_TO_READY_STATE`, 790 `traceQUEUE_SEND`, 798 `traceQUEUE_RECEIVE`, 854 `traceTASK_DELAY_UNTIL`, 878 `traceTASK_INCREMENT_TICK`; `tasks.c` 287–292 `prvAddTaskToReadyList`, 2472, 3809, 4842, 5006, 5228/5274, 6745 `xTaskPriorityInherit` (6791), 6848 `xTaskPriorityDisinherit` (6895); `queue.c` 979, 1125, 1545, 1615, 1698, 1787–1795, 2413; `include/semphr.h` 167 `xSemaphoreCreateBinary`, 735 `xSemaphoreCreateMutex` — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", IEEE Transactions on Computers, 1990.
- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", Journal of the ACM, 1973.
- N. Audsley, A. Burns, M. Richardson, K. Tindell, A. Wellings, "Applying New Scheduling Theory to Static Priority Pre-emptive Scheduling", Software Engineering Journal, 1993 (블로킹·지터를 넣은 RTA — 제목은 기억 기준).
- G. Reeves, "What really happened on Mars?" (Mars Pathfinder 우선순위 역전 사후 분석, 1997 — 트레이스로 원인을 찾은 고전 사례).
- SEGGER SystemView, Percepio Tracealyzer 사용자 문서 — 이벤트 기록·타임라인·응답 시간 뷰 (원리 비교용, 이 레슨에서 미검증).
- Zephyr Project 문서 — Tracing, Mutexes (priority inheritance) — <https://docs.zephyrproject.org/latest/services/tracing/index.html>; CMSIS-RTOS2 `osMutexPrioInherit`, `osDelayUntil` — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/>
- 관련 레슨: RTOS-A06 (트레이스 포맷·레코더·디코더), RTOS-M03/M04/M05 (역전·상속·천장), RTOS-M02 (ISR 지연 처리), RTOS-B11 (큐), RTOS-B06 (delay_until), RTOS-M11 (RM·RTA), RTOS-A04 (B·J·ISR 를 넣은 RTA), RTOS-A03 (WCET 측정), RTOS-A05 (비주기 서버), RTOS-M13 (trace 매크로), RTOS-A08 (SMP), RTOS-A10 (시스템 설계)

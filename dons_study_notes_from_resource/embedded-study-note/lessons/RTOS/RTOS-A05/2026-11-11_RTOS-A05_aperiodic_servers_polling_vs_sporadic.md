---
id: RTOS-A05
time: 110분
updated: 2026-11-11
---
# RTOS-A05 비주기 작업 서버: polling server vs sporadic server

## 학습 목표

- 비주기(aperiodic) 요청을 **background · polling server(PS) · deferrable server(DS) · sporadic server(SS)** 로 처리하는 결정적 tick 시뮬레이터를 직접 만들고, 같은 요청열에서 응답 시간과 주기 태스크 데드라인을 비교할 수 있다.
- PS 가 "활성화 순간 큐가 비면 예산을 잃는" 이유, SS 가 "쓴 만큼, 활성화 시각 + Ts 에" 돌려받는 이유를 타임라인으로 설명하고, 둘 다 **주기 태스크 (Cs, Ts) 로 RTA 를 해도 되는** 근거를 말할 수 있다.
- DS 가 주기 경계를 넘어 back-to-back 으로 2·Cs 를 쓰는 반례를 만들고, **지터 J = Ts − Cs** 를 넣은 RTA 로 그 위험을 미리 잡을 수 있다.
- SS 보충 규칙의 함정(chunk 도중에 보충이 도착)을 재현하고, 틀린 규칙이 실제로 데드라인을 놓치게 하는 워크로드를 보일 수 있다.
- 펌웨어 시스템에서 서버 (Cs, Ts) 를 RTA 로 정하고, FreeRTOS 처럼 서버가 없는 RTOS 에서 무엇으로 흉내 내는지(그리고 무엇이 안 되는지) 말할 수 있다.

## 1. 문제

"주기 제어 루프는 RM 으로 다 검증했어요. 그런데 UART/CAN 으로 설정 명령이 **언제 올지 모르게** 들어옵니다. 응답은 빨리 하고 싶고, 제어 루프 보장은 깨면 안 됩니다. 어떻게 하죠?" — RTA(RTOS-M11) 다음에 시니어 펌웨어 면접이 반드시 가는 질문이다. "가장 높은 우선순위 태스크에서 바로 처리" 는 빠르지만 폭주 한 번에 루프가 놓치고(연습 3: burst 부하 20 seed 에서 1206번), "가장 낮은 우선순위" 는 안전하지만 느리다. 정답은 **예산(budget, capacity)을 가진 서버** — 비주기 일을 "주기 Ts 마다 최대 Cs 만큼만 쓰는 주기 태스크" 처럼 보이게 만들어 RM 분석 안에 넣는 것이다.

서버 4종을 한 줄로:

| 정책 | 예산 규칙 | 응답 | RM 분석에 넣는 법 |
|---|---|---|---|
| BG (background) | 없음. 주기 태스크가 하나도 준비되지 않은 tick 에만 | 느림 (가장 낮은 태스크 뒤) | 영향 0 |
| PS (polling server) | k·Ts 에 큐가 비어 있지 않으면 Cs, 비어 있으면 **0**. 처리 중 큐가 비면 남은 예산도 버림 | 보통 (다음 활성화까지 기다림) | 주기 태스크 (Cs, Ts) |
| DS (deferrable server) | k·Ts 마다 Cs 로 채우고 주기 안에서는 **아껴 둠** | 빠름 | 주기 태스크로 보면 **틀림** → 지터 J = Ts − Cs |
| SS (sporadic server) | 쓴 만큼만, **활성화 시각 + Ts** 에 돌려받음 (Sprunt, Sha, Lehoczky 1989) | DS 에 가까움 | 주기 태스크 (Cs, Ts) |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 요청 3개, 정책 4개 — 서버 줄만 (solution.c 실제 출력, hi(1,5) lo(2,10), 서버 (2,4))</text>
  <text x="107" y="46" fill="currentColor" font-size="12">0</text><text x="147" y="46" fill="currentColor" font-size="12">2</text><text x="187" y="46" fill="currentColor" font-size="12">4</text><text x="227" y="46" fill="currentColor" font-size="12">6</text><text x="267" y="46" fill="currentColor" font-size="12">8</text><text x="307" y="46" fill="currentColor" font-size="12">10</text><text x="347" y="46" fill="currentColor" font-size="12">12</text><text x="387" y="46" fill="currentColor" font-size="12">14</text><text x="427" y="46" fill="currentColor" font-size="12">16</text><text x="467" y="46" fill="currentColor" font-size="12">18</text><text x="507" y="46" fill="currentColor" font-size="12">20</text><text x="547" y="46" fill="currentColor" font-size="12">22</text><text x="587" y="46" fill="currentColor" font-size="12">24</text>
  <line x1="130" y1="52" x2="130" y2="226" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="133" y="64" fill="currentColor" font-size="12">도착 C=2</text>
  <line x1="290" y1="52" x2="290" y2="226" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="293" y="64" fill="currentColor" font-size="12">도착 C=1</text>
  <line x1="370" y1="52" x2="370" y2="226" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="373" y="64" fill="currentColor" font-size="12">도착 C=3</text>
  <text x="10" y="93" fill="currentColor" font-size="12">BG</text>
  <line x1="110" y1="98" x2="590" y2="98" stroke="currentColor" stroke-width="0.5"/>
  <rect x="130" y="72" width="40" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="170" y="72" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="290" y="72" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="370" y="72" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="410" y="72" width="20" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="430" y="72" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="598" y="90" fill="currentColor" font-size="12">4, 1, 4 → 평균 3.00</text>
  <text x="10" y="131" fill="currentColor" font-size="12">PS</text>
  <line x1="110" y1="136" x2="590" y2="136" stroke="currentColor" stroke-width="0.5"/>
  <rect x="130" y="110" width="60" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="190" y="110" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="290" y="110" width="60" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="350" y="110" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="370" y="110" width="60" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="430" y="110" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="470" y="110" width="40" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="510" y="110" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="598" y="128" fill="currentColor" font-size="12">5, 4, 8 → 평균 5.67</text>
  <text x="10" y="169" fill="currentColor" font-size="12">DS</text>
  <line x1="110" y1="174" x2="590" y2="174" stroke="currentColor" stroke-width="0.5"/>
  <rect x="130" y="148" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="290" y="148" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="370" y="148" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="410" y="148" width="20" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="430" y="148" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="598" y="166" fill="currentColor" font-size="12">2, 1, 4 → 평균 2.33</text>
  <text x="10" y="207" fill="currentColor" font-size="12">SS</text>
  <line x1="110" y1="212" x2="590" y2="212" stroke="currentColor" stroke-width="0.5"/>
  <rect x="130" y="186" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="290" y="186" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="370" y="186" width="40" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="410" y="186" width="40" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="450" y="186" width="20" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="598" y="204" fill="currentColor" font-size="12">2, 1, 5 → 평균 2.67</text>
  <rect x="110" y="234" width="16" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="132" y="246" fill="currentColor" font-size="12">비주기 처리</text>
  <rect x="230" y="234" width="16" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/><text x="252" y="246" fill="currentColor" font-size="12">일은 있는데 못 돎 (예산 0 / 주기 태스크 실행 중)</text>
  <text x="10" y="272" fill="currentColor" font-size="12">PS: t=0 활성화 때 큐가 비어 예산 0 → t=1 요청은 t=4 까지 기다린다. SS/DS: 예산을 들고 있다가 바로 처리.</text>
</svg>
```

**시뮬레이터 규칙** (starter.c 머리 주석과 같다, 모든 파일 자급식):

| 항목 | 규칙 |
|---|---|
| 시간 | 정수 tick, 단일 코어, 고정 우선순위 선점. 1 tick 에 1 단위 실행 |
| 주기 태스크 | `(C, T, O)`: job k 는 `O + kT` 에 릴리즈, 데드라인 = 릴리즈 + T. 우선순위는 RM (주기 짧을수록 높음) |
| 서버 우선순위 | 주기 Ts 로 같은 RM 줄에 선다. 주기가 같으면 **서버가 위**. BG 는 맨 아래 |
| 비주기 요청 | `(arr, C)`. 서버 하나, FIFO 큐 하나 |
| tick t 순서 | ① 데드라인 검사 ② 주기 릴리즈 ③ 비주기 도착 ④ `srv_tick_begin` (활성화·보충) ⑤ `sched_pick` ⑥ 1 tick 실행 + `srv_consume` ⑦ `srv_tick_end` (폐기·chunk 닫기) |
| 미스 | 데드라인 == t 인데 남은 일이 있으면 1 회. job 은 계속 실행 (RTOS-M12 의 `MISS_CONTINUE`) |
| 타임라인 | 주기 줄 `R` 실행 · `r` 준비인데 못 돎 · `.` 없음 / 서버 줄 `R` 실행 · `r` 예산·일 있는데 못 돎 · `w` 일은 있는데 예산 없음 / `budget` 줄 = 그 tick 의 예산 |

### Part A — 선택 규칙, background, polling server

- `int sched_pick(const sim_t *s)` — **TODO 1.** 준비된 주기 태스크 중 rank 가 가장 작은 것, 서버는 `srv_ready()`(예산 > 0 이고 큐에 일) 일 때만 `rank[n]` 으로 경쟁. BG 는 주기 태스크가 하나도 없고 큐에 일이 있을 때만. 반환 −1 = idle, n = 서버.
- PS 규칙 — **TODO 2.** `srv_tick_begin`: `t % Ts == 0` 이면 `budget = 큐 비었나 ? 0 : Cs`. `srv_tick_end`: 큐가 비면 `budget = 0`.
- 테스트: 선택 규칙 단위 검사 7개, BG/PS 의 응답(4,1,4 / 5,4,8)·타임라인·예산 줄.

### Part B — sporadic server 보충 규칙, deferrable server 반례

- SS 규칙 — **TODO 3.** chunk(= 한 번의 활성화) 열기: 닫혀 있고 예산 > 0 이고 일이 있으면 `chunk_start = t`. 실행하면 `chunk_used++`. 닫기: 예산 소진 · 큐 비움 · **chunk 도중 보충 도착** — 쓴 양이 있으면 `(chunk_start + Ts, chunk_used)` 를 보충 대기열에 넣는다. 보충 도착: `due == t` 인 항목을 예산에 더한다.
- DS 규칙 — **TODO 4.** `t % Ts == 0` 이면 `budget = Cs` (쌓이지 않음).
- 테스트: SS 보충 기록 `(1→5, 2) (9→13, 1) (13→17, 2) (17→21, 1)`, 도중 보충 예제 `(8→16, 2)`, 함정 워크로드에서 미스 0, 불변식 "예산 + 대기 보충 + 열린 chunk 사용량 == Cs" 매 tick, DS 반례에서 lo 응답 10 (= 지터 RTA 값) 과 미스 1, 같은 워크로드에서 PS/SS 미스 0.

### Part C — 모터 컨트롤러: 서버 크기 정하기와 seed 비교

STM32G4 급 모터 컨트롤러, 1 tick = 100 µs. speed 루프 1 kHz (200 µs), IMU 융합 500 Hz (400 µs), 플래시 로그 100 Hz (3 ms, 부팅 후 0.9 ms 에 시작 → O = 9). 비주기: UART/CAN 설정·진단 명령 파싱 100–400 µs.

- `uint32_t server_max_budget(ts, n, Ts, ds_jitter)` — **TODO 5.** Cs = Ts … 1 중 주기 태스크가 모두 RTA 를 통과하는 가장 큰 값. DS 는 `ds_jitter = 1` 로 서버를 지터 `Ts − Cs` 인 태스크로 본다. RTA 자체(`rta_with_server`)는 주어진 코드 (RTOS-M11 식 + 지터).
- 같은 seed 40개 × 20 000 tick 을 **normal**(평균 간격 20, U_ap ≈ 0.125) 과 **burst**(간격 10, U_ap ≈ 0.25) 로 돌려 5개 정책(BG, PS, DS, SS, 지터로 줄인 DS-safe)을 비교. 검사: BG/PS/SS/DS-safe 미스 0 이고 모든 주기 태스크의 관측 최대 응답 ≤ RTA, 평균 응답 DS < SS < PS < BG, burst 에서 DS 는 미스 > 0.

## 2. Problem (English)

> **Aperiodic servers on a fixed-priority uniprocessor.** Write a tick-driven simulator (integer time, preemptive, Rate-Monotonic periodic tasks with `D = T`) that serves a FIFO queue of aperiodic requests `(arrival, exec)` with one of: background (idle-priority), polling server, deferrable server, sporadic server. The server has budget `Cs` and period `Ts` and takes its RM priority from `Ts` (ties: server above).
>
> 1. `int sched_pick(const sim_t *s)` — highest-priority ready periodic task, or the server if it has budget and pending work and outranks it; background runs only when no periodic task is ready.
> 2. Polling server: at `t % Ts == 0`, `budget = queue_empty ? 0 : Cs`; if the queue empties, the remaining budget is discarded.
> 3. Sporadic server: the server *activates* when it becomes ready (budget > 0 and work pending) at time `tA`; consumption in that activation is replenished at `tA + Ts`. An activation ends when the budget is exhausted, the queue empties, or a replenishment arrives (then a new activation starts at that instant).
> 4. Deferrable server: `budget = Cs` at every `t % Ts == 0`, preserved within the period.
> 5. `uint32_t server_max_budget(const ptask_t *ts, int n, uint32_t Ts, int ds_jitter)` — the largest `Cs` for which every periodic task passes response-time analysis with the server modelled as a periodic task `(Cs, Ts)` (with release jitter `Ts − Cs` if `ds_jitter`).
>
> Example: tasks `hi(1,5)`, `lo(2,10)`, server `(2,4)`, requests `(1,2) (9,1) (13,3)`. Responses: background 4, 1, 4; polling 5, 4, 8; deferrable 2, 1, 4; sporadic 2, 1, 5 with replenishments `(5,+2) (13,+1) (17,+2) (21,+1)`. Counterexample: `lo(4,8)` released at 3, DS `(2,5)`, request `(3,6)` → `lo` responds in 10 > 8 although RTA with the DS as a periodic task says 8.
>
> Follow-ups: why is a sporadic server analysable as a periodic task but a deferrable server is not? What goes wrong if a replenishment arriving mid-activation is charged to the old activation time? How would you size `(Cs, Ts)`? What do you do on FreeRTOS, which has no server?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter          # 처음엔 "11/58 passed", exit 1
./starter -v      # 타임라인 출력 (Part A/B 예제)
./starter sweep   # 비주기 부하·log 오프셋 스윕 표 (검사 없음, 관찰용)
```

TODO 는 다섯 덩어리다: TODO 1 `sched_pick`, TODO 2a/2b PS, TODO 3a–3e SS (`srv_tick_begin` / `srv_consume` / `srv_tick_end` / `ss_close_chunk`), TODO 4 DS, TODO 5 `server_max_budget`. 시뮬레이터 본체(`sim_step`), RM 순위(`assign_rm`), RTA(`rta_resp`, `rta_with_server`), 요청 생성기(xorshift32, 고정 seed)는 주어진 코드다. starter 는 아무것도 실행하지 않으므로 주기 태스크가 놓치기만 하고 정해진 tick 수 뒤에 끝난다 (멈추지 않는다).

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: BG 와 서버는 "언제 후보가 되나" 만 다르다</summary>

먼저 주기 태스크 중 `jn[i] > 0` 이고 rank 가 가장 작은 것을 고른다. 그다음 BG 면 "아무도 없을 때만" 서버(n), 아니면 `srv_ready(s)` 이고 `rank[n] < rank[best]` 일 때 서버. 예산이 0 인데 일만 있는 서버를 고르면 예산이 음수(= uint32 감싸기)가 된다.

</details>

<details>
<summary>힌트 2 — TODO 2: PS 의 "잃는다" 는 두 군데</summary>

활성화 순간(`t % Ts == 0`)에 큐가 비어 있으면 0 — 그 주기 동안 요청이 와도 못 쓴다. 그리고 처리하다 큐가 비면(`srv_tick_end`) 남은 예산을 0 으로. 두 번째를 빼먹으면 "아껴 둔 예산" 이 생겨 DS 처럼 동작하고, DS 의 반례가 PS 에도 생긴다.

</details>

<details>
<summary>힌트 3 — TODO 3: SS 는 "chunk" 단위로 장부를 쓴다</summary>

chunk = 서버가 준비 상태(예산 > 0, 일 있음)인 연속 구간. 여는 순간의 시각이 보충 기준이고, 닫힐 때 그 chunk 에서 쓴 양을 `(chunk_start + Ts)` 에 돌려받는다. 닫는 이유는 세 가지: 예산 소진, 큐 비움, **도중에 보충이 도착**. 세 번째는 `srv_tick_begin` 에서 보충을 더한 뒤 확인한다 — 닫고, 바로 같은 tick 에 새 chunk 를 연다.

</details>

<details>
<summary>힌트 4 — TODO 3: 불변식으로 스스로 검사</summary>

`budget + Σ 대기 보충 + (열린 chunk 의 chunk_used) == Cs` 가 매 tick 성립해야 한다 (시뮬레이터가 `inv_bad` 로 센다). 보충을 `Cs` 전체로 하거나, chunk 를 닫으면서 `chunk_used` 를 0 으로 안 돌리면 바로 깨진다.

</details>

<details>
<summary>힌트 5 — TODO 5: 위에서부터 내려오며 RTA</summary>

`for (Cs = Ts; Cs >= 1; Cs--) if (rta_with_server(ts, n, Cs, Ts, ds_jitter ? Ts - Cs : 0, NULL)) return Cs;` — 위에서부터 하나씩 내려오므로 처음 통과하는 값이 곧 최대다 (단조성 가정이 필요 없다). 이분 탐색으로 줄이고 싶다면 단조성이 필요한데, DS 는 Cs 가 줄면 지터 `Ts − Cs` 가 **늘어난다** — 그래도 단조일지 생각해 보라 (Q5).

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨 (호스트 모델) | FreeRTOS V11.3.2 | POSIX / Linux | Zephyr |
|---|---|---|---|
| 주기 태스크 + RM 우선순위 | `xTaskCreate` + 우선순위, 루프에 `xTaskDelayUntil` (`tasks.c:2414`) | `SCHED_FIFO` + `clock_nanosleep(TIMER_ABSTIME)` | `k_thread_create` + `k_sleep`/`k_timer` (문서 기준) |
| BG | `tskIDLE_PRIORITY` (`task.h:210`, = 0) 태스크, 또는 idle hook (블로킹 금지 `task.h:2035`) | 가장 낮은 `SCHED_FIFO` 또는 `SCHED_OTHER` | 가장 낮은 preemptible 우선순위 |
| PS | 우선순위 Ps 태스크가 `xTaskDelayUntil(…, Ts)` 로 깨어 큐를 `xQueueReceive(q, …, 0)` 로 Cs 만큼만 비움 | 같은 패턴 | 같은 패턴 |
| SS | **없음** (커널 트리에 sporadic/deferrable 문자열 0개 — grep 확인). 태스크 + 예산 장부 + 보충 타이머로 직접 | POSIX `SCHED_SPORADIC` (선택 기능). 이 Mac 은 `_POSIX_SPORADIC_SERVER (-1)` = 미지원 (`unistd.h:138`). Linux 메인라인에도 없다 (문서 기준, 미검증) | 커널 서버 없음 (문서 기준, 미검증) |
| 예산 소비 측정 | `configGENERATE_RUN_TIME_STATS` → `ulRunTimeCounter +=` (`tasks.c:5247`), `ulTaskGetRunTimeCounter` (`tasks.c:8521`) | `CLOCK_THREAD_CPUTIME_ID` | thread runtime stats (문서 기준) |
| 보충 이벤트 | one-shot 소프트웨어 타이머 `xTimerCreate` (`timers.c:336`, 콜백은 타이머 태스크 — RTOS-B12) | `timer_create` | `k_timer` |
| 예산 소진 시 강등 | `vTaskPrioritySet` (`tasks.c:2844`) — FromISR 버전 없음 (`task.h` 에는 `uxTaskPriorityGetFromISR` 만, `task.h:1103`) | `sched_ss_low_priority` 로 자동 강등 (POSIX, 문서 기준) | `k_thread_priority_set` |
| EDF 쪽 대응 (연습 4) | 없음 (고정 우선순위 — RTOS-M12) | `SCHED_DEADLINE` = EDF + CBS (문서 기준, 미검증) | `CONFIG_SCHED_DEADLINE` (문서 기준) |

### Part A — 선택 규칙과 background

```c
int best = -1;
for (int i = 0; i < s->n; i++)
    if (s->jn[i] > 0 && (best < 0 || s->rank[i] < s->rank[best])) best = i;
if (s->sc.kind == SRV_BG) {
    if (best < 0 && q_nonempty(s)) best = s->n;
} else if (srv_ready(s) && (best < 0 || s->rank[s->n] < s->rank[best])) {
    best = s->n;
}
```

서버를 "우선순위 Ps 의 태스크" 로 다룬다는 게 이 레슨 전체의 뼈대다. 서버가 다른 태스크와 다른 점은 **준비 조건** 하나뿐이다 — 일이 있어야 하고 예산이 있어야 한다. 그래서 `sched_pick` 은 RTOS-B04 의 ready list 선택 그대로이고, 정책 차이는 모두 예산 규칙(TODO 2–4)에 들어간다.

실제 출력 (`./solution -v`, hi(1,5) lo(2,10), 요청 (1,2) (9,1) (13,3)):

```text
-- A background  (Cs=0 Ts=4)
tick   : 012345678901234567890123
hi     : R....R....R....R....R...
lo     : rRR.......rRR.......rRR.
BG     : .wwRR....R...RRwR.......
budget : ------------------------
A  BG: aperiodic done=3 avg=3.00 max=4 | periodic misses=0 maxresp hi=1 lo=3
```

이 예제는 주기 부하가 U = 0.4 로 가벼워서 BG 가 나쁘지 않다 (응답 4, 1, 4). BG 의 약점은 **주기 부하가 크고, 특히 긴 저우선순위 태스크가 있을 때** 드러난다 — Part C 의 3 ms 로그 태스크 뒤에 줄 서면 평균 21–31 tick 이다.

### Part A — polling server: 예산을 잃는다

```text
-- A polling server  (Cs=2 Ts=4)
tick   : 012345678901234567890123
hi     : R....rR...R....R....rR..
lo     : rRR.......rRrR......rrRR
PS     : .wwwRR...wwwRwwwRRwwR...
budget : 000021000000200021002000
A  PS: aperiodic done=3 avg=5.67 max=8 | periodic misses=0 maxresp hi=2 lo=4
```

| 시각 | 사건 | 예산 |
|---|---|---|
| 0 | 활성화. 큐가 비어 있음 → **0** | 0 |
| 1 | 요청 1 (C=2) 도착. 예산 0 → `w` | 0 |
| 4 | 활성화. 큐에 일 → 2. t=4, 5 처리 → 응답 5 | 2 → 0 |
| 9 | 요청 2 (C=1) 도착 → t=12 활성화까지 대기, t=12 처리 → 응답 4. **큐가 비어 남은 1 폐기** | 2 → 1 → 0 |
| 13 | 요청 3 (C=3) 도착. 직전에 폐기했으므로 t=16 까지 대기 | 0 |
| 16, 20 | 2 + 1 로 나눠 처리 → 응답 8 | |

PS 가 이렇게 정의된 이유: 서버를 "k·Ts 에 릴리즈되어 최대 Cs 를 쓰는 주기 태스크" 와 **정확히 같게** 만들기 위해서다. 활성화 순간 일이 없으면 그 job 은 실행 0 으로 끝난 것이고, 처리 중 큐가 비면 job 이 일찍 끝난 것이다. 주기 태스크가 C 보다 덜 써도 RTA 는 안전하므로, PS 는 RTA 에서 그냥 주기 태스크 (Cs, Ts) 다 (Lehoczky, Sha, Strosnider 1987). 대가는 응답: "방금 활성화가 지나간" 요청은 최대 Ts 를 기다린다.

### Part B — sporadic server: 쓴 만큼, 활성화 + Ts 에

```c
case SRV_SS: {
    int got = 0;
    while (s->rn > 0 && s->repl[s->rh].due == t) { s->budget += s->repl[s->rh].amt; /* pop */ got = 1; }
    if (got && s->in_chunk) ss_close_chunk(s);              /* 도중 보충 → 닫고 */
    if (!s->in_chunk && s->budget > 0 && q_nonempty(s)) {   /* 준비가 되는 순간 = 활성화 */
        s->in_chunk = 1; s->chunk_start = t; s->chunk_used = 0;
    }
}
/* srv_consume:  chunk_used++      srv_tick_end: 소진 또는 큐 비움 → ss_close_chunk
   ss_close_chunk: chunk_used > 0 이면 (chunk_start + Ts, chunk_used) 를 대기열 끝에 */
```

같은 예제의 실제 출력:

```text
-- B sporadic server  (Cs=2 Ts=4)
tick   : 012345678901234567890123
hi     : R....R....R....R....R...
lo     : rrrRR.....rRR.......rRR.
SS     : .RR......R...RRwwR......
budget : 221002222211121002111222
B  SS: aperiodic done=3 avg=2.67 max=5 | periodic misses=0 maxresp hi=1 lo=5
```

| chunk 시작 | 닫힌 이유 | 쓴 양 | 보충 (시각, 양) |
|---|---|---|---|
| 1 | 큐 비움 (t=2 끝) | 2 | (5, +2) |
| 9 | 큐 비움 | 1 | (13, +1) |
| 13 | 예산 소진 (t=14 끝) | 2 | (17, +2) |
| 17 | 큐 비움 | 1 | (21, +1) |

SS 는 PS 와 달리 예산을 **들고 있다가** 요청이 오면 바로 쓴다 (응답 2, 1, 5 — PS 의 5, 4, 8). 그런데도 RTA 에서 주기 태스크 (Cs, Ts) 로 다룰 수 있는 이유는 **각 예산 단위(토큰)가 Ts 안에 두 번 "릴리즈" 되지 않기** 때문이다:

1. chunk 는 서버가 준비 상태인 연속 구간이다. 그 안에서 서버가 못 도는 건 더 높은 우선순위가 돌 때뿐 — 즉 chunk 는 "chunk_start 에 릴리즈되어 높은 우선순위에게 밀리는 job" 과 같다.
2. chunk 가 쓴 토큰은 `chunk_start + Ts` 에야 돌아오므로, 같은 토큰을 쓰는 다음 chunk 의 시작은 앞 chunk 시작보다 Ts 이상 뒤다.
3. 그러면 길이 L 인 어떤 구간에서 시작한 chunk 들이 쓸 수 있는 총량 ≤ ⌈L/Ts⌉·Cs — 주기 태스크 (Cs, Ts) 의 간섭 상한과 같다. 낮은 태스크 i 의 바쁜 구간(level-i busy period) 시작 시점에 서버가 준비 상태일 수는 없으므로(준비면 이미 바쁜 구간이다), 그 구간에서 서버가 쓰는 건 모두 구간 안에서 시작한 chunk 의 몫이다 → RTA 식이 그대로 성립.

Sprunt, Sha, Lehoczky (1989) 의 원래 정의는 활성화를 "우선순위 Ps 이상이 실행 중인 구간의 시작" 으로 잡는다. 이 레슨은 "서버가 준비가 되는 순간" 으로 잡았는데, 이 시각은 원래 정의의 시각보다 같거나 늦다 → 보충이 같거나 늦게 온다 → 같은 논증으로 안전하고, 응답은 약간 손해다 (정의 차이는 기억 기준, 원문 대조는 미검증).

시뮬레이터는 매 tick **불변식** `budget + Σ 대기 보충 + 열린 chunk 사용량 == Cs` 를 센다 (`inv_bad`). 모든 테스트·seed 에서 0 이다.

### Part B — 함정: chunk 도중에 보충이 오면

3번 논증의 전제는 "chunk 의 모든 토큰이 chunk_start 에 이미 있었다" 이다. 서버가 더 높은 태스크에게 밀려 기다리는 동안 보충이 도착하면, 그 새 토큰은 chunk_start 보다 **늦게** 생겼다. 그 토큰을 쓴 양까지 `chunk_start + Ts` 에 돌려받으면 토큰이 Ts 보다 빨리 돌아온다. 그래서 TODO 3a 는 보충이 도착하면 chunk 를 닫고 그 시각에 다시 연다.

작은 예 (mid(3,6, O=1) 가 서버 (2,8) 보다 높다, 요청 (0,1) (7,3)) — 실제 출력:

```text
-- B SS replenish mid-chunk  (Cs=2 Ts=8)
tick   : 012345678901234567890123
mid    : .RRR...RRR...RRR...RRR..
low    : rrrrR...................
SS     : R......rrrRRwwwwR.......
budget : 211111112221000021111111
```

t=7 에 요청이 와 chunk 가 열리지만(예산 1) mid 가 7–9 를 쓴다. t=8 에 첫 보충(+1)이 도착 → 닫고(쓴 양 0) t=8 에 다시 연다. t=10, 11 을 쓰고 소진 → 보충은 **(16, +2)**. 틀린 규칙이면 (15, +2) 다. 이 1 tick 차이가 정말 문제가 되나? 연습 1 의 함정 워크로드 (hi(2,7,O5) lo(2,20,O6), SS(6,10), RTA 로 lo 의 R = 20 정확히) 에서 실제로 데드라인을 놓친다:

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SS(6,10) 함정 워크로드, t=24..55 (practice.c -v 실제 출력)</text>
  <text x="107" y="44" fill="currentColor" font-size="12">24</text><text x="179" y="44" fill="currentColor" font-size="12">28</text><text x="251" y="44" fill="currentColor" font-size="12">32</text><text x="323" y="44" fill="currentColor" font-size="12">36</text><text x="395" y="44" fill="currentColor" font-size="12">40</text><text x="467" y="44" fill="currentColor" font-size="12">44</text><text x="539" y="44" fill="currentColor" font-size="12">48</text><text x="611" y="44" fill="currentColor" font-size="12">52</text><text x="683" y="44" fill="currentColor" font-size="12">56</text>
  <text x="10" y="66" fill="currentColor" font-size="12" font-weight="bold">틀린 규칙: 도중 보충이 와도 chunk 시작 = 32 유지</text>
  <text x="10" y="89" fill="currentColor" font-size="12">hi</text>
  <line x1="110" y1="94" x2="686" y2="94" stroke="currentColor" stroke-width="0.5"/>
  <rect x="146" y="74" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="272" y="74" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="398" y="74" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="524" y="74" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="650" y="74" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="115" fill="currentColor" font-size="12">lo</text>
  <line x1="110" y1="120" x2="686" y2="120" stroke="currentColor" stroke-width="0.5"/>
  <rect x="146" y="100" width="90" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="236" y="100" width="18" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="254" y="100" width="324" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="578" y="100" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="614" y="100" width="18" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="632" y="100" width="18" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="141" fill="currentColor" font-size="12">SS</text>
  <line x1="110" y1="146" x2="686" y2="146" stroke="currentColor" stroke-width="0.5"/>
  <rect x="128" y="126" width="18" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="146" y="126" width="36" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="182" y="126" width="54" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="236" y="126" width="18" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="254" y="126" width="18" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="272" y="126" width="36" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="308" y="126" width="90" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="398" y="126" width="36" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="434" y="126" width="90" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="524" y="126" width="36" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="560" y="126" width="18" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="578" y="126" width="36" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="614" y="126" width="18" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="164" fill="currentColor" font-size="12">예산</text>
  <text x="116" y="164" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="134" y="164" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="152" y="164" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="170" y="164" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="188" y="164" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="206" y="164" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="224" y="164" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="242" y="164" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="260" y="164" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="278" y="164" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="296" y="164" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="314" y="164" fill="currentColor" font-size="12" font-family="monospace">5</text><text x="332" y="164" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="350" y="164" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="368" y="164" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="386" y="164" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="404" y="164" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="422" y="164" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="440" y="164" fill="currentColor" font-size="12" font-family="monospace">6</text><text x="458" y="164" fill="currentColor" font-size="12" font-family="monospace">5</text><text x="476" y="164" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="494" y="164" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="512" y="164" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="530" y="164" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="548" y="164" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="566" y="164" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="584" y="164" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="602" y="164" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="620" y="164" fill="currentColor" font-size="12" font-family="monospace">6</text><text x="638" y="164" fill="currentColor" font-size="12" font-family="monospace">5</text><text x="656" y="164" fill="currentColor" font-size="12" font-family="monospace">5</text><text x="674" y="164" fill="currentColor" font-size="12" font-family="monospace">5</text>
  <text x="10" y="216" fill="currentColor" font-size="12" font-weight="bold">맞는 규칙: t=35 보충 도착 → chunk 닫고 35 에 새로 연다</text>
  <text x="10" y="239" fill="currentColor" font-size="12">hi</text>
  <line x1="110" y1="244" x2="686" y2="244" stroke="currentColor" stroke-width="0.5"/>
  <rect x="146" y="224" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="272" y="224" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="398" y="224" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="524" y="224" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="650" y="224" width="36" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="265" fill="currentColor" font-size="12">lo</text>
  <line x1="110" y1="270" x2="686" y2="270" stroke="currentColor" stroke-width="0.5"/>
  <rect x="146" y="250" width="90" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="236" y="250" width="18" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="254" y="250" width="198" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="452" y="250" width="18" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="506" y="250" width="126" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="632" y="250" width="18" height="20" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="650" y="250" width="36" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="291" fill="currentColor" font-size="12">SS</text>
  <line x1="110" y1="296" x2="686" y2="296" stroke="currentColor" stroke-width="0.5"/>
  <rect x="128" y="276" width="18" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="146" y="276" width="36" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="182" y="276" width="54" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="236" y="276" width="18" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="254" y="276" width="18" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="272" y="276" width="36" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="308" y="276" width="90" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="398" y="276" width="36" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="434" y="276" width="18" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="452" y="276" width="36" height="20" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="488" y="276" width="36" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="524" y="276" width="36" height="20" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="560" y="276" width="72" height="20" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="314" fill="currentColor" font-size="12">예산</text>
  <text x="116" y="314" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="134" y="314" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="152" y="314" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="170" y="314" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="188" y="314" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="206" y="314" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="224" y="314" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="242" y="314" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="260" y="314" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="278" y="314" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="296" y="314" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="314" y="314" fill="currentColor" font-size="12" font-family="monospace">5</text><text x="332" y="314" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="350" y="314" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="368" y="314" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="386" y="314" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="404" y="314" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="422" y="314" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="440" y="314" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="458" y="314" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="476" y="314" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="494" y="314" fill="currentColor" font-size="12" font-family="monospace">5</text><text x="512" y="314" fill="currentColor" font-size="12" font-family="monospace">4</text><text x="530" y="314" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="548" y="314" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="566" y="314" fill="currentColor" font-size="12" font-family="monospace">3</text><text x="584" y="314" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="602" y="314" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="620" y="314" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="638" y="314" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="656" y="314" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="674" y="314" fill="currentColor" font-size="12" font-family="monospace">5</text>
  <line x1="506" y1="50" x2="506" y2="330" stroke="currentColor" stroke-width="1.5"/>
  <text x="510" y="345" fill="currentColor" font-size="12">lo 데드라인 46</text>
  <text x="10" y="365" fill="currentColor" font-size="12">t=42: 틀린 규칙은 6 이 한꺼번에 (35 에 받은 4 가 7 tick 만에 돌아옴)</text>
  <text x="10" y="388" fill="currentColor" font-size="12">틀린 규칙 → lo 응답 25 (미스), 맞는 규칙 → 18. RTA 경계 20.</text>
</svg>
```

t=32 에 2 가 돌아와 chunk 가 열리고, hi 에 밀린 사이 t=35 에 4 가 더 돌아온다. 틀린 규칙은 이 4 를 쓴 것까지 "32 의 몫" 으로 장부에 적어 t=42 에 6 을 한꺼번에 돌려받는다 — 35 에 받은 토큰이 7 tick 만에 다시 쓰인다. 그 결과 lo 는 26 에 릴리즈된 job 을 51 에야 끝낸다 (응답 25, 데드라인 46 미스). 맞는 규칙은 (42, +1), (45, +5) 로 나눠 적어 lo 가 t=43 에 남은 1 tick 을 돌고 44 에 끝난다 (응답 18 ≤ RTA 20). practice.c 의 무작위 탐색(소형 시스템 19 573개, 서버는 RTA 최대 Cs)에서도 틀린 규칙은 RTA 초과 3개·미스 1개, 맞는 규칙은 0개였다. 예산 보존 불변식은 **틀린 규칙에서도 깨지지 않는다** — 양이 아니라 시각이 틀렸기 때문에 불변식 검사로는 못 잡는다.

POSIX `SCHED_SPORADIC` 의 정의에도 이런 류의 보충 시각·예산 결함이 있다는 지적이 있다 — Stanovich, Baker, Wang, González Harbour (2010, RTAS) "Defects of the POSIX Sporadic Server and How to Correct Them" (제목·세부는 기억 기준). 면접에서는 "SS 는 논문 정의를 그대로 구현해도 경계 사례에서 틀리기 쉽고, 보충을 늦추는 쪽으로 보수적으로 고친다" 정도로 말하면 충분하다.

### Part B — deferrable server 반례: back-to-back

DS 는 PS 의 단점(예산을 잃음)을 "주기 안에서 아껴 둔다" 로 고친 것이다 (Strosnider, Lehoczky, Sha 1995). 응답은 가장 빠르지만, 주기 끝에 남은 예산을 쓰고 바로 다음 주기 예산을 이어 쓰면 **짧은 구간에 2·Cs** 가 몰린다. 주기 태스크는 이런 패턴을 만들 수 없으므로 "DS = 주기 태스크 (Cs, Ts)" 라는 RTA 는 틀린다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">lo(4,8, 첫 릴리즈 3) + 서버 (2,5) + t=3 에 6 짜리 요청 (solution.c -v 실제 출력)</text>
  <text x="107" y="44" fill="currentColor" font-size="12">0</text><text x="155" y="44" fill="currentColor" font-size="12">2</text><text x="203" y="44" fill="currentColor" font-size="12">4</text><text x="251" y="44" fill="currentColor" font-size="12">6</text><text x="299" y="44" fill="currentColor" font-size="12">8</text><text x="347" y="44" fill="currentColor" font-size="12">10</text><text x="395" y="44" fill="currentColor" font-size="12">12</text><text x="443" y="44" fill="currentColor" font-size="12">14</text><text x="491" y="44" fill="currentColor" font-size="12">16</text><text x="539" y="44" fill="currentColor" font-size="12">18</text><text x="587" y="44" fill="currentColor" font-size="12">20</text><text x="635" y="44" fill="currentColor" font-size="12">22</text><text x="683" y="44" fill="currentColor" font-size="12">24</text>
  <text x="10" y="66" fill="currentColor" font-size="12" font-weight="bold">DS: 예산을 아껴 둔 채 주기 경계를 넘는다</text>
  <text x="10" y="91" fill="currentColor" font-size="12">lo</text>
  <line x1="110" y1="96" x2="686" y2="96" stroke="currentColor" stroke-width="0.5"/>
  <rect x="182" y="74" width="96" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="278" y="74" width="72" height="22" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="350" y="74" width="48" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="398" y="74" width="120" height="22" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="566" y="74" width="96" height="22" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="119" fill="currentColor" font-size="12">DS</text>
  <line x1="110" y1="124" x2="686" y2="124" stroke="currentColor" stroke-width="0.5"/>
  <rect x="182" y="102" width="96" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="278" y="102" width="72" height="22" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="350" y="102" width="48" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="142" fill="currentColor" font-size="12">예산</text>
  <text x="119" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="143" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="167" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="191" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="215" y="142" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="239" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="263" y="142" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="287" y="142" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="311" y="142" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="335" y="142" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="359" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="383" y="142" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="407" y="142" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="431" y="142" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="455" y="142" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="479" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="503" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="527" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="551" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="575" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="599" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="623" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="647" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="671" y="142" fill="currentColor" font-size="12" font-family="monospace">2</text>
  <text x="10" y="186" fill="currentColor" font-size="12" font-weight="bold">SS: 쓴 만큼 "활성화 + 5" 에만 돌려받는다</text>
  <text x="10" y="211" fill="currentColor" font-size="12">lo</text>
  <line x1="110" y1="216" x2="686" y2="216" stroke="currentColor" stroke-width="0.5"/>
  <rect x="182" y="194" width="48" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="230" y="194" width="72" height="22" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="302" y="194" width="48" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="350" y="194" width="72" height="22" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="422" y="194" width="48" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="470" y="194" width="48" height="22" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="566" y="194" width="96" height="22" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="239" fill="currentColor" font-size="12">SS</text>
  <line x1="110" y1="244" x2="686" y2="244" stroke="currentColor" stroke-width="0.5"/>
  <rect x="182" y="222" width="48" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="230" y="222" width="72" height="22" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="302" y="222" width="48" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="350" y="222" width="72" height="22" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="422" y="222" width="48" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="262" fill="currentColor" font-size="12">예산</text>
  <text x="119" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="143" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="167" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="191" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="215" y="262" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="239" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="263" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="287" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="311" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="335" y="262" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="359" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="383" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="407" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="431" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="455" y="262" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="479" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="503" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="527" y="262" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="551" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="575" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="599" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="623" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="647" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text><text x="671" y="262" fill="currentColor" font-size="12" font-family="monospace">2</text>
  <line x1="230" y1="70" x2="230" y2="300" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.8"/>
  <line x1="350" y1="70" x2="350" y2="300" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.8"/>
  <line x1="470" y1="70" x2="470" y2="300" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.8"/>
  <line x1="374" y1="50" x2="374" y2="300" stroke="currentColor" stroke-width="1.5"/>
  <text x="378" y="314" fill="currentColor" font-size="12">lo 데드라인 11</text>
  <text x="10" y="170" fill="currentColor" font-size="12">t=3..6 에 4 tick 연속 (주기 5 안에 2 + 다음 주기 2) → lo 는 13 에 끝, 응답 10 → 미스</text>
  <text x="10" y="290" fill="currentColor" font-size="12">t=3..4 쓰고 t=8 (=3+5) 에 2 보충 → lo 는 11 에 끝, 응답 8 → OK</text>
</svg>
```

```text
B  DS(2,5) adversarial: aperiodic done=1 avg=9.00 max=9 | periodic misses=1 maxresp lo=10
B  PS(2,5) adversarial: aperiodic done=1 avg=14.00 max=14 | periodic misses=0 maxresp lo=7
B  SS(2,5) adversarial: aperiodic done=1 avg=12.00 max=12 | periodic misses=0 maxresp lo=8
```

손 계산 (lo = (4, 8), 서버 (2, 5) 가 위):

| 분석 | 반복 | R | 판정 |
|---|---|---|---|
| 순진: 서버 = 주기 태스크 | w = 4 + ⌈4/5⌉·2 = 6 → 4 + ⌈6/5⌉·2 = 8 → 8 | 8 | 통과 (틀림) |
| 지터 J = Ts − Cs = 3 | w = 4 + ⌈(4+3)/5⌉·2 = 8 → 4 + ⌈11/5⌉·2 = 10 → 4 + ⌈13/5⌉·2 = 10 | 10 | 실패 (관측 10 과 같다) |

왜 지터 `Ts − Cs` 인가: DS 의 한 주기 예산은 그 주기의 어느 시점에서든 쓸 수 있으므로, 가장 늦게는 주기 끝 `Ts − Cs` 지점에서 시작한다. 즉 "주기 Ts 로 릴리즈되지만 실제 실행 시작이 최대 `Ts − Cs` 늦어질 수 있는 태스크" 이고, 이것이 바로 RTOS-M11 Part C 의 릴리즈 지터 항 `⌈(w + J_j)/T_j⌉` 이다 (DS 를 이렇게 모델링하는 것은 RTA 문헌의 통상적 처리 — 출처 세부는 미검증).

Part C 스윕에서 이 지터가 정확히 보인다. 로그 태스크 시작 오프셋 O 를 0–9 로 바꾸면 DS(3,10) 의 미스가 **O ≥ 7 = Ts − Cs** 에서만 나온다 (`./solution sweep` 실제 출력):

```text
sweep: log offset O vs DS(3,10) periodic misses (burst gap~10)
   O: 0:0 1:0 2:0 3:0 4:0 5:0 6:0 7:11 8:20 9:25
```

O < 7 이면 로그가 릴리즈되기 전에 DS 의 "앞 주기 남은 예산" 이 다 쓰이거나 로그의 바쁜 구간에 들어오지 못한다. 위상이 맞아떨어지는 시스템에서만 터지는 버그 — 벤치 테스트는 통과하고 현장에서 부팅 순서가 바뀌면 터지는 종류다.

### Part C — 서버 크기: RTA 로 Cs 의 최대값

```c
for (uint32_t Cs = Ts; Cs >= 1; Cs--)
    if (rta_with_server(ts, n, Cs, Ts, ds_jitter ? Ts - Cs : 0, NULL)) return Cs;
return 0;
```

```text
C  server Ts=10: max RM-feasible Cs = 3 (PS/SS), DS with jitter = 2
```

순서는 서버(3,10) → speed(2,10) → imu(4,20) → log(30,100) (주기 10 동률은 서버가 위). log 의 RTA 손 계산:

| w | ⌈w/10⌉·3 (서버) | ⌈w/10⌉·2 (speed) | ⌈w/20⌉·4 (imu) | 새 w = 30 + 합 |
|---|---|---|---|---|
| 30 | 9 | 6 | 8 | 53 |
| 53 | 18 | 12 | 12 | 72 |
| 72 | 24 | 16 | 16 | 86 |
| 86 | 27 | 18 | 20 | 95 |
| 95 | 30 | 20 | 20 | 100 |
| 100 | 30 | 20 | 20 | **100** ≤ 100 |

정확히 경계다. 주기가 10/10/20/100 으로 **조화(harmonic)** 라서 U = 0.3 + 0.2 + 0.2 + 0.3 = 1.0 인데도 통과한다 (RTOS-M11 연습 3). Cs = 4 면 w 가 56 → 78 → 94 → 110 으로 넘친다. DS 를 Cs = 3 으로 두면 지터 7 때문에 log 의 w 가 56 → 75 → 89 → 98 → 103 으로 실패하고, Cs = 2 (지터 8) 면 52 → 66 → 76 → 80 으로 통과한다 — 그래서 "DS-safe" 는 2 다.

**seed 비교** (`./solution` 실제 출력, 40 seed × 20 000 tick, 요청 실행량 1–4 tick):

```text
C  normal (gap~20, U_ap~0.125): 40 seeds x 20000 ticks, 39175 requests
   policy   Cs  ap_done  ap_avg  ap_max  periodic_miss  over_RTA
   BG        0    39175   21.27      71              0         0
   PS        3    39175   10.22      47              0         0
   DS        3    39175    4.16      41              0         0
   SS        3    39175    5.64      41              0         0
   DS-safe   2    39175    8.81      86              0         0
C  burst  (gap~10, U_ap~0.25): 40 seeds x 20000 ticks, 78409 requests
   policy   Cs  ap_done  ap_avg  ap_max  periodic_miss  over_RTA
   BG        0    78409   30.99     139              0         0
   PS        3    78409   18.27     112              0         0
   DS        3    78409   10.91     102             78        26
   SS        3    78409   13.46     108              0         0
   DS-safe   2    64158  1980.19    4500              0         0
```

(`over_RTA` = 관측 최대 응답이 RTA 값을 넘은 (실행, 태스크) 쌍 수. 단위는 tick = 100 µs.)

| 읽을 거리 | 숫자 | 의미 |
|---|---|---|
| SS vs PS | 5.64 vs 10.22 (normal), 13.46 vs 18.27 (burst) | 같은 Cs, 같은 RM 보장인데 SS 가 normal 1.8배, burst 1.36배 빠르다 — 예산을 들고 기다리기 때문 |
| SS vs DS | 5.64 vs 4.16 | DS 가 더 빠르지만 burst 에서 log 를 78번 놓친다. SS 는 그 차이 정도만 양보하고 보장을 지킨다 |
| BG | 21.27 / 30.99 | 3 ms 로그 뒤에 줄 서기 때문. 주기 부하 U_p = 0.7 |
| DS-safe | 8.81 → 1980 | 지터 분석으로 Cs 를 2 로 줄이면 대역폭 0.2. burst 부하 0.25 를 못 따라가 큐가 끝없이 자란다 (64158/78409 만 끝남) |
| over_RTA | BG/PS/SS/DS-safe 모두 0 | 관측이 분석을 넘지 않는다 = 분석이 안전 |

부하 스윕 (`./solution sweep`, seed 10개):

```text
sweep: aperiodic load vs avg response (seeds 1..10, Ts=10, Cs=3)
   gap  U_ap     BG      PS      DS      SS   | periodic misses BG/PS/DS/SS
    80  0.031    18.3     9.1     3.4     4.5  | 0/0/0/0
    40  0.062    18.8     9.3     3.5     4.7  | 0/0/0/0
    20  0.125    21.0    10.1     4.1     5.5  | 0/0/0/0
    15  0.167    23.1    11.3     4.8     6.6  | 0/0/0/0
    12  0.208    25.4    13.2     6.4     8.5  | 0/0/4/0
    10  0.250    31.6    18.6    11.3    13.8  | 0/0/25/0
```

부하가 서버 대역폭(0.3)에 가까워질수록 모든 서버의 응답이 급격히 나빠지고(대기열 이론의 1/(1−ρ) 모양), DS 는 그 구간에서 미스가 나기 시작한다.

**서버 크기에 대한 실무 감각** (연습 5 스윕): Ts 를 키우면 RTA 가 허락하는 Cs 도 계단식으로 커지는데, 대역폭 Cs/Ts 는 단조가 아니다 (Ts=10 → 0.300, Ts=13 → 0.231). 요청 하나(1–4 tick)가 Cs 안에 들어가지 못하면 여러 주기에 쪼개져 응답이 튄다 (Ts=6, Cs=1 → SS 평균 20.97). 그래서 Ts 는 "**대역폭 Cs/Ts 가 큰 지점** + **Cs ≥ 전형적인 요청 길이**" 로 고른다. 이 시스템에서는 Ts = 20 (Cs = 6) 이 SS 평균 4.32 로 가장 좋았다. 단, Ts = 20 이면 서버가 speed 루프(10)보다 아래로 내려간다 — 응답 요구가 speed 루프보다 급하지 않다면 오히려 바람직하다.

### FreeRTOS 에서 sporadic server 흉내 내기 — 설계 스케치 (컴파일·실행 안 함)

FreeRTOS 에는 서버도, 태스크별 CPU 예산 강제도 없다. 흔한 근사는 **요청 단위 예산 장부** 다 — 요청마다 WCET(RTOS-A03 방식으로 측정한 상한)를 알고, 실행 전에 예산이 충분한지 보고, 보충은 절대 시각으로 관리한다.

```c
/* 우선순위 Ps 의 서버 태스크. 요청은 ISR 이 xQueueSendFromISR 로 넣는다 (RTOS-M02). */
for (;;) {
    xQueuePeek(q, &req, portMAX_DELAY);              /* 일이 생길 때까지 블록 (예산과 무관) */
    TickType_t now = xTaskGetTickCount();
    apply_due_replenishments(now);                   /* due <= now 인 보충을 예산에 */
    if (budget < req.wcet) {                         /* 부족하면 다음 보충 시각까지 잔다 */
        vTaskDelay(next_due() - now);                /* 다음 보충 시각까지 */
        continue;
    }
    TickType_t start = now;                          /* = 활성화 시각 */
    xQueueReceive(q, &req, 0);
    handle(&req);                                    /* 실행 중엔 더 높은 태스크만 끼어든다 */
    budget -= req.wcet;
    push_replenishment(start + Ts, req.wcet);        /* 쓴 만큼, 활성화 + Ts */
}
```

한계를 같이 말해야 한다: (1) 예산 강제가 **요청 경계에서만** 된다 — 요청이 WCET 를 넘기면 아무도 멈추지 않는다 (진짜 강제는 tick hook 에서 실행 시간을 세고 서버를 강등해야 하는데, tick hook 은 ISR 문맥이고 `vTaskPrioritySet` 은 FromISR 버전이 없어 알림으로 감독 태스크에 넘겨야 한다). (2) WCET 로 장부를 쓰므로 실제로 덜 쓴 시간은 버려진다 (보수적). (3) 요청 하나가 Cs 보다 크면 영원히 못 돈다 → 쪼개거나 Cs 를 키운다. 측정이 필요하면 `ulTaskGetRunTimeCounter` (`tasks.c:8521`) 로 실제 사용량을 볼 수 있지만, 그 값은 `vTaskSwitchContext` 에서 전환될 때만 갱신된다 (`tasks.c:5247`).

### 정리: 다섯 가지 처리 방식

| | 응답 (normal 평균) | 주기 보장 | 분석 | 구현 비용 |
|---|---|---|---|---|
| 바로 처리 (최고 우선순위, 연습 3, 20 seed) | 2.56 | 깨짐 (normal 34, burst 1206 미스 / 20 seed) | 불가 | 0 |
| BG | 21.27 | 안전 | 필요 없음 | 0 |
| PS | 10.22 | 안전 | 주기 태스크 (Cs, Ts) | 낮음 (주기 태스크 하나) |
| DS | 4.16 | **위상에 따라 깨짐** | 지터 J = Ts − Cs 필요 → Cs 가 줄어듦 | 낮음 |
| SS | 5.64 | 안전 | 주기 태스크 (Cs, Ts) | 보충 대기열 + 장부, 정의가 까다로움 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 58/58 passed (5회 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 깨끗
```

## 7. 자주 하는 실수

| 실수 | 왜 틀리나 | 고친 코드 |
|---|---|---|
| PS 에서 큐가 비어도 예산을 남겨 둠 | 남은 예산을 주기 안 아무 때나 쓰면 그게 DS 다 → back-to-back 반례가 PS 에도 생긴다 | `srv_tick_end`: `if (!q_nonempty(s)) s->budget = 0;` |
| PS 예산을 다음 주기로 누적 (`budget += Cs`) | 쉬었다가 한꺼번에 k·Cs 를 쓴다 — 어떤 주기 태스크 모델로도 안 묶인다 | `budget = q_nonempty ? Cs : 0` (대입, 누적 아님) |
| SS 보충을 Cs 전체로 | 쓴 양보다 많이 돌아와 예산이 부풀어 오른다. 불변식 `inv_bad` 가 바로 잡는다 | `amt = chunk_used` |
| SS 보충 시각을 "chunk 처음 시각" 으로 고정 (도중 보충 무시) | 도중에 온 토큰이 Ts 보다 빨리 돌아온다 → 함정 워크로드에서 lo 미스 (연습 1). 불변식은 못 잡는다 | 보충 도착 시 `ss_close_chunk` 후 같은 tick 에 새 chunk |
| SS 보충 시각을 "요청 도착 + Ts" 로 | 요청이 예산 0 일 때 도착해 나중에 처리되면 너무 이른 시각이 된다 | 기준은 도착이 아니라 **준비가 된 순간** (예산 > 0 이고 일 있음) |
| DS 를 주기 태스크 (Cs, Ts) 로 RTA | 반례: 순진 R = 8, 관측 10. 위상(오프셋)에 따라서만 터져 테스트로 놓치기 쉽다 | `rta_with_server(…, Js = Ts − Cs, …)` |
| BG 처리를 idle hook 에서 블로킹 큐 대기로 | idle hook 은 블로킹 금지 (`task.h:2035` "MUST NOT, UNDER ANY CIRCUMSTANCES, CALL A FUNCTION THAT MIGHT BLOCK") | `tskIDLE_PRIORITY` 의 별도 태스크 (같은 우선순위면 `configIDLE_SHOULD_YIELD` 로 idle 이 양보, `tasks.c:5856`) |

## 8. 면접 Q&A

<details>
<summary>Q1. 비주기 이벤트(UART 명령, 버튼, 진단 요청)를 그냥 가장 높은 우선순위 태스크에서 바로 처리하면 안 되나요?</summary>

평균 응답은 가장 좋다 (연습 3: 2.56 tick). 하지만 그 태스크의 부하를 **누가 묶어 주지 않으면** RM 분석에 넣을 C 와 T 가 없다 — 비주기는 최소 도착 간격이 없기 때문이다. 실제로 같은 시스템에서 normal 부하에도 20 seed 에 34번, burst 에 1206번 주기 태스크가 놓쳤다. 고칠 방법은 둘: (1) 이벤트에 최소 간격을 강제해(디바운스, 프로토콜 rate limit) **산발(sporadic) 태스크** 로 만들거나, (2) 예산을 가진 서버로 감싸 "최대 Cs / Ts" 를 강제한다. ISR 상반부는 짧게 유지하고(RTOS-M02) 무거운 처리를 서버로 넘기는 것이 전형이다.

</details>

<details>
<summary>Q2. Polling server 와 sporadic server 의 차이는? 둘 다 RTA 에서 주기 태스크 (Cs, Ts) 로 다뤄도 되는 이유는?</summary>

PS 는 k·Ts 순간에만 "일이 있나" 를 보고, 없거나 다 처리하면 예산을 버린다 → 정확히 "k·Ts 에 릴리즈되는 주기 태스크" 와 같은 실행 패턴이라 분석이 자명하다. SS 는 예산을 들고 기다리다 요청이 오면 바로 쓰고, **쓴 양을 그 활성화 시각 + Ts 에** 돌려받는다. 각 예산 단위가 Ts 안에 두 번 활성화될 수 없으므로 길이 L 구간의 서버 수요 ≤ ⌈L/Ts⌉·Cs — 주기 태스크와 같은 간섭 상한이다 (Sprunt, Sha, Lehoczky 1989). 결과: 같은 Cs 로 SS 가 PS 보다 평균 1.8배 빠르면서(5.64 vs 10.22) 보장은 같다.

</details>

<details>
<summary>Q3. Deferrable server 는 왜 RM 보장을 깰 수 있나요? 그럼 어떻게 분석하나요?</summary>

DS 는 주기 시작에 Cs 로 채우고 주기 안에서 아껴 둔다. 주기 끝에 남은 예산을 쓰고 경계 직후 새 예산을 이어 쓰면 짧은 구간에 2·Cs 가 몰린다 — 주기 태스크는 이런 패턴을 못 만든다. 반례: lo(4,8,O3), DS(2,5) → 순진한 RTA 8, 관측 10 (미스). 분석은 DS 를 **릴리즈 지터 J = Ts − Cs** 인 주기 태스크로 보면 된다 (지터 RTA R = 10 = 관측). 대가로 허용 Cs 가 줄어 (Part C: 3 → 2) 대역폭이 0.3 → 0.2 가 되고, burst 부하 0.25 를 못 따라간다. 꼬리: 미스가 로그 오프셋 O ≥ 7 = Ts − Cs 에서만 나왔다 — 위상 의존 버그라 테스트로는 놓치기 쉽다.

</details>

<details>
<summary>Q4. Sporadic server 의 보충 규칙을 정확히 말해 보세요. 서버가 밀려 있는 동안 보충이 도착하면요?</summary>

(이 레슨의 정의) 활성화 = 예산 > 0 이고 일이 있어 서버가 준비가 되는 순간 tA. 활성화가 끝나는 순간(예산 소진 또는 큐 비움)에 그동안 쓴 양 RA 를 tA + Ts 에 돌려받도록 예약한다. 서버가 더 높은 태스크에게 밀려 준비 상태로 기다리는 사이 보충이 도착하면, 새 토큰은 tA 이후에 생긴 것이므로 **그 시각에 활성화를 끊고 새로 시작** 해야 한다. 그러지 않으면 토큰이 Ts 보다 빨리 돌아와 함정 워크로드에서 lo 가 놓친다 (응답 25 > 20). 원래 논문 정의(우선순위 레벨이 활성인 구간 기준)와 POSIX 정의에도 이런 경계 사례 결함이 보고됐다 (Stanovich et al. 2010 — 세부는 기억 기준). 의심스러우면 보충을 **늦추는** 쪽이 항상 안전하다.

</details>

<details>
<summary>Q5. 서버의 (Cs, Ts) 는 어떻게 정하나요?</summary>

① 응답 요구로 Ts 의 대략을 정한다 (Ts 가 서버 우선순위를 정한다 — RM 이면 짧을수록 위). ② 그 Ts 에서 RTA 로 주기 태스크를 깨지 않는 최대 Cs 를 구한다 (`server_max_budget`: 위에서부터 내려오는 선형 탐색이라 첫 통과값이 최대). PS/SS 는 간섭 ⌈w/Ts⌉·Cs 가 Cs 에 단조라 이분 탐색도 된다. DS 는 Cs 를 1 줄이면 지터가 1 늘어 ⌈(w + Ts − Cs)/Ts⌉ 가 k → k+1 이 될 수 있고, (k+1)(Cs−1) > k·Cs ⇔ Cs > k+1 이므로 **Cs 를 줄였는데 간섭이 늘 수 있다** — 단조가 아니니 선형 탐색이 맞다. ③ 확인: 대역폭 Cs/Ts > 비주기 평균 부하 (아니면 큐가 무한히 자란다 — DS-safe 1980 tick), Cs ≥ 전형적 요청 길이 (아니면 요청이 여러 주기로 쪼개진다). ④ 시뮬레이션·트레이스로 평균/최대 응답 확인 (연습 5: Ts=20, Cs=6 이 SS 평균 4.32 로 최적, Cs/Ts 는 Ts 에 단조가 아니다).

</details>

<details>
<summary>Q6. FreeRTOS 에는 서버가 없는데 실무에서는 어떻게 하나요?</summary>

커널 트리 V11.3.2 에 sporadic/deferrable 구현은 없다 (grep 결과 0). 실무 선택지: (1) **PS** — 우선순위 Ps 태스크가 `xTaskDelayUntil` 로 Ts 마다 깨어 큐를 `xQueueReceive(…, 0)` 로 최대 Cs 만큼 비운다. 가장 단순하고 분석이 자명하다. (2) **요청 단위 SS 근사** — 요청별 WCET 로 장부를 쓰고 보충을 절대 시각으로 관리 (5장 스케치). (3) 비주기 이벤트에 최소 간격을 강제해 산발 태스크로 분석. 진짜 예산 강제(초과 시 강등)는 tick hook 이 ISR 문맥이고 `vTaskPrioritySet` 에 FromISR 판이 없어서 감독 태스크로 넘겨야 하며, 정밀도는 tick 이다. 예산 강제가 꼭 필요하면 OS 를 바꾼다 — Linux `SCHED_DEADLINE`(CBS, 문서 기준), 상용 RTOS 의 시간 분할(ARINC 653 류 파티션) 등.

</details>

<details>
<summary>Q7. (staff) 비주기 부하가 서버 대역폭을 넘으면 어떻게 되나요? 어떻게 설계를 바꾸겠습니까?</summary>

서버는 주기 태스크를 지켜 주지만 비주기 쪽은 **대기열이 끝없이 자란다** — DS-safe(대역폭 0.2) 에 부하 0.25 를 넣었더니 평균 응답 1980 tick, 최대 4500 tick, 78 409개 중 64 158개만 끝났다. 바꿀 것: (1) 부하 상한을 계약으로 정하고 입구에서 거절/드롭 (큐 깊이 상한 + 오래된 요청 폐기, 프로토콜 NACK/busy), (2) 요청을 중요도별로 나눠 서버 두 개 (진단은 BG, 설정은 SS), (3) 대역폭을 늘릴 여지가 있는지 RTA 로 다시 — 주기 태스크의 C 를 줄이거나 (예: 로그 플래시 쓰기를 DMA 로), 주기를 조화로 맞춰 경계를 1.0 까지 쓰기, (4) EDF 쪽이면 CBS 로 비주기마다 대역폭을 나눠 격리. 면접에서 중요한 말: "서버는 **누가** 손해를 볼지 정하는 장치다 — 과부하에서 손해는 비주기 쪽이 본다."

</details>

<details>
<summary>Q8. (staff) 이 분석이 틀리는 경우는? 실제 보드에서 무엇을 더 넣어야 하나요?</summary>

(1) **오버헤드**: 서버 활성화·보충마다 문맥 전환과 타이머 비용 — SS 는 보충 이벤트가 많아 PS 보다 비싸다. RTA 의 C 에 전환 비용을 넣거나 Cs 에서 뺀다 (RTOS-M11 연습 2). (2) **예산 강제 정밀도**: tick 단위 강제면 최대 1 tick 초과 실행 → 분석에 Cs + 1 tick. (3) **서버 안의 블로킹**: 요청 처리 중 뮤텍스를 잡으면 블로킹 항 B 가 생기고, 예산이 소진된 채 락을 쥐고 있으면 다른 태스크가 오래 막힌다 (RTOS-M04/M05, A04). (4) **ISR 상반부**는 서버 밖이다 — ISR 부하는 별도 최고 우선순위 항으로 (RTOS-A04). (5) **요청 길이 > Cs** 는 여러 주기로 쪼개져 응답 분석이 달라진다. (6) **보충 대기열 크기 제한**: 꽉 차면 합쳐야 하는데 시각을 앞당기면 깨지고, 늦추면 안전하다 (연습 2, POSIX `sched_ss_max_repl` 류). (7) **멀티코어**: 서버를 코어에 고정하지 않으면 분석 자체가 달라진다 (RTOS-A08).

</details>

<details>
<summary>Q9. 시스템이 EDF 라면 서버는 어떻게 되나요?</summary>

EDF 에서는 서버에 **데드라인** 을 주면 된다. Total Bandwidth Server (Spuri & Buttazzo) 는 k 번째 요청에 `d_k = max(r_k, d_{k−1}) + C_k / U_s` 를 주고 EDF 큐에 넣는다 — 주기 태스크 U_p 와 U_p + U_s ≤ 1 이면 모두 지킨다 (연습 4: U_s = 1/4 일 때 4, 12, 24). C_k 를 미리 알아야 한다는 게 약점이고, 모를 때는 CBS (Abeni & Buttazzo 1998) 가 예산을 쓰면 데드라인을 미뤄 대역폭을 강제한다. Linux `SCHED_DEADLINE` 이 EDF + CBS 다 (문서 기준, 미검증 — RTOS-M12 Q8).

</details>

## 9. 추가 연습

### 연습 1 — 보충 시각의 함정을 직접 재현

`sim_t.naive = 1` 이면 chunk 도중에 보충이 와도 chunk 를 닫지 않는다(틀린 규칙). 함정 워크로드 hi(2,7,O5) lo(2,20,O6), SS(6,10), 요청 (2,4) (5,10) (25,4) (27,13) 에서 틀린 규칙은 lo 가 놓치고 맞는 규칙은 안 놓친다는 것을 보이고, 무작위 소형 시스템 수천 개에서 "관측 > RTA" 인 시스템 수를 두 규칙으로 세라.

<details>
<summary>답 보기</summary>

`srv_tick_begin` 의 한 줄 차이다: `if (got && s->in_chunk && !s->naive) ss_close_chunk(s);`. 실제 출력:

```text
ex1 naive: lo misses=1 maxresp=25 (RTA 20) inv_bad=0
ex1 fixed: lo misses=0 maxresp=18
ex1 random: 19573 systems  naive: over-RTA 3, with misses 1 | fixed: over-RTA 0, with misses 0
```

무작위 탐색에서 틀린 규칙의 위반은 드물다 (19 573개 중 3개) — 서버가 더 높은 태스크에게 밀린 채 예산 일부를 들고 있고, 그 사이 보충이 오고, 낮은 태스크의 RTA 가 빡빡해야 한다. **드물다 = 테스트로는 못 잡는다** 가 이 연습의 요점이다. 그래서 규칙 자체를 논증(토큰이 Ts 안에 두 번 안 쓰인다)으로 확인하고, 불변식(양)과 별도로 시각을 검사하는 테스트(보충 기록 비교)를 둔다.

</details>

### 연습 2 — 보충 대기열 크기 제한

임베디드에서는 보충 대기열을 고정 크기 K 로 잡는다 (POSIX 의 `sched_ss_max_repl` 이 같은 문제를 다룬다 — 정확한 POSIX 동작은 문서 기준, 미검증). 꽉 찼을 때 새 보충을 **마지막 항목에 합치되 시각은 더 늦은 새 시각으로** 하라. K = 1, 2, 4, 무제한으로 burst 워크로드에서 미스·RTA 초과·불변식과 평균 응답을 비교하라.

<details>
<summary>답 보기</summary>

```c
if (s->chunk_used > 0 && s->max_repl > 0 && s->rn >= s->max_repl) {
    repl_t *last = &s->repl[(s->rh + s->rn - 1) % MAXREPL];
    last->amt += s->chunk_used;
    last->due = s->chunk_start + s->sc.Ts;     /* 앞 항목보다 늦다: chunk 시작은 증가 */
}
```

```text
ex2 burst workload, SS(3,10), max_repl K:
    K=1   avg= 15.90 max= 110 done=39175 misses=0 over=0 inv_bad=0
    K=2   avg= 13.72 max= 104 done=39175 misses=0 over=0 inv_bad=0
    K=4   avg= 13.51 max= 104 done=39175 misses=0 over=0 inv_bad=0
    K=inf avg= 13.51 max= 104 done=39175 misses=0 over=0 inv_bad=0
```

합치면서 시각을 늦추면 토큰이 **더 늦게** 돌아올 뿐이라 안전 논증이 그대로다 (미스 0, 초과 0). 반대로 "앞 항목 시각으로 합치기" 는 새 토큰을 너무 일찍 돌려줘 연습 1 과 같은 결함이 된다. 이 시스템에서는 K = 4 면 무제한과 같았다 (Cs = 3 이라 동시에 대기하는 보충이 많지 않다). 작은 예 (요청 (1,1) (2,1), SS(2,4)) 에서 무제한은 t=5, 6 에 1씩, K = 1 은 t=6 에 2 를 한꺼번에 돌려받는다 (예산 줄 `2210012` vs `2210002`).

</details>

### 연습 3 — 서버 없이 바로 처리

비주기 요청을 예산 없이 가장 높은 우선순위로 처리하는 `SRV_DI` 를 추가하고, Part C 시스템에서 SS(3,10) 과 응답·미스를 비교하라.

<details>
<summary>답 보기</summary>

`sched_pick`: `if (kind == SRV_DI && q_nonempty(s)) best = s->n;` (순위도 맨 위), `srv_consume` 에서 예산을 깎지 않는다.

```text
ex3 direct: normal avg=2.56 misses=34 | burst avg=2.65 misses=1206
ex3 SS    : normal avg=5.66 misses=0 | burst avg=13.51 misses=0
```

normal 부하(평균 U_ap 0.125)에서도 짧은 구간에 요청이 몰리면 speed·imu·log 가 놓친다. "평균 부하가 낮으니 괜찮다" 는 RM 에서 통하지 않는다 — 보장은 최악 구간의 수요로 결정된다. 이것이 ISR 에서 모든 처리를 하면 안 되는 이유(RTOS-M02 Part B)의 태스크 버전이다.

</details>

### 연습 4 — EDF 쪽: Total Bandwidth Server 데드라인

`uint32_t tbs_deadline(prev_d, r, C, num, den)` — 대역폭 U_s = num/den 일 때 `max(r, prev_d) + ⌈C·den/num⌉` 를 정수로 계산하라. U_s = 1/4 로 요청 (0,1) (2,2) (20,1), 이어서 U_s = 3/10 으로 (21,3) 의 데드라인은?

<details>
<summary>답 보기</summary>

```c
uint32_t base = r > prev_d ? r : prev_d;
return base + (uint32_t)(((uint64_t)C * den + num - 1) / num);
```

```text
ex4 TBS deadlines: 4 12 24 34
```

(0,1) → 0 + 4 = 4. (2,2) → max(2, 4) + 8 = 12 — 앞 요청의 데드라인에서 이어 붙어 대역폭을 넘지 않는다. (20,1) → 20 + 4 = 24. (21,3) 에 U_s = 0.3 → max(21, 24) + ⌈10⌉ = 34. ⌈C/U_s⌉ 를 double 로 하면 1/0.3 = 3.333… 의 반올림 문제가 생기므로 정수 올림으로 한다 (RTOS-M11 Part B 의 float 함정과 같은 이유). 이 데드라인으로 EDF 큐에 넣으면 U_p + U_s ≤ 1 일 때 모두 지킨다.

</details>

### 연습 5 — 서버 설계 공간 탐색

Ts = 5…25 각각에 대해 RTA 최대 Cs, 대역폭, SS/PS 평균 응답(normal 워크로드, seed 10개)을 표로 만들고 SS 평균이 가장 작은 Ts 를 고르라. 각 설정에서 미스 0, RTA 초과 0 을 확인하라.

<details>
<summary>답 보기</summary>

```text
ex5 design sweep (normal workload, 10 seeds):  Ts  Cs  Cs/Ts  SS_avg  SS_max  PS_avg
     5   1  0.200   13.40      84   15.39
     6   1  0.167   20.97     192   23.46
     7   2  0.286    6.63      44    9.68
    10   3  0.300    5.62      41   10.24
    13   3  0.231    8.45      60   14.56
    14   4  0.286    4.91      41   11.43
    17   5  0.294    4.64      40   12.57
    20   6  0.300    4.32      41   14.41
    21   6  0.286    5.77      44   15.36
    25   7  0.280    5.58      42   17.05
ex5 best Ts for SS avg = 20 (avg 4.32)
```

(일부 줄만 옮김, 전체는 `./practice`.) 세 가지가 보인다. ① Cs 는 Ts 에 계단식이라 Cs/Ts 가 톱니 모양이다 — 계단 바로 위(Ts = 7, 10, 14, 17, 20)가 좋다. ② Cs < 요청 최대 길이(4) 면 요청이 쪼개져 나빠진다 (Ts = 5, 6). ③ PS 는 Ts 가 커질수록 나빠진다 — "다음 활성화까지 기다림" 이 Ts 에 비례하기 때문. SS 는 예산을 들고 있으므로 그 손해가 없다. 두 서버의 차이가 Ts 가 클수록 벌어지는 것이 SS 의 존재 이유다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 43/43 passed
./practice -v                                                                   # 연습 1 타임라인 (틀린/맞는 규칙)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 깨끗
```

## 10. 핵심 정리

- 비주기 처리의 선택지: 바로 처리(빠름·보장 없음) / BG(안전·느림) / **서버** = 예산 Cs 를 가진 우선순위 Ts 의 태스크.
- **PS**: k·Ts 에 일이 없으면 예산 0, 비면 폐기 → 정확히 주기 태스크 (Cs, Ts). 응답은 최대 Ts 대기.
- **DS**: 예산을 주기 안에서 아낌 → 빠르지만 경계에서 2·Cs back-to-back. 분석은 **지터 J = Ts − Cs** 로 (반례: 순진 8, 실제 10).
- **SS**: 쓴 만큼, **활성화 시각 + Ts** 에 보충. 토큰이 Ts 안에 두 번 활성화되지 않으므로 주기 태스크 (Cs, Ts) 로 분석 가능, 응답은 DS 에 근접 (5.64 vs 4.16, PS 10.22).
- SS 함정: chunk 도중 보충이 오면 닫고 다시 연다. 시각 결함은 예산 보존 불변식으로 못 잡는다 — 보충 기록을 검사.
- 서버 크기: RTA 로 최대 Cs → 대역폭 > 비주기 부하, Cs ≥ 전형적 요청 길이. 과부하의 손해는 비주기 쪽이 본다 (대기열 폭증).
- FreeRTOS 에는 서버가 없다: PS 패턴(`xTaskDelayUntil` + 큐 비우기)이나 요청 단위 예산 장부로 근사. EDF 쪽은 TBS/CBS (`SCHED_DEADLINE`).

## 참고자료

- B. Sprunt, L. Sha, J. Lehoczky, "Aperiodic Task Scheduling for Hard-Real-Time Systems", *Real-Time Systems* 1(1), 1989 — sporadic server 원 논문.
- J. Lehoczky, L. Sha, J. Strosnider, "Enhanced Aperiodic Responsiveness in Hard Real-Time Environments", IEEE RTSS 1987 — polling / deferrable server.
- J. Strosnider, J. Lehoczky, L. Sha, "The Deferrable Server Algorithm for Enhanced Aperiodic Responsiveness in Hard Real-Time Environments", *IEEE Trans. Computers* 44(1), 1995.
- M. Stanovich, T. Baker, A.-I. Wang, M. González Harbour, "Defects of the POSIX Sporadic Server and How to Correct Them", IEEE RTAS 2010 (제목은 기억 기준).
- M. Spuri, G. Buttazzo, "Scheduling Aperiodic Tasks in Dynamic Priority Systems", *Real-Time Systems* 10(2), 1996 — TBS. L. Abeni, G. Buttazzo, "Integrating Multimedia Applications in Hard Real-Time Systems", IEEE RTSS 1998 — CBS.
- J. Lehoczky, S. Ramos-Thuel, "An Optimal Algorithm for Scheduling Soft-Aperiodic Tasks in Fixed-Priority Preemptive Systems", IEEE RTSS 1992 — slack stealing (다음 단계).
- G. Buttazzo, *Hard Real-Time Computing Systems* (Springer) — 5장 고정 우선순위 서버, 6장 동적 우선순위 서버. J. W. S. Liu, *Real-Time Systems* (2000) 7장.
- N. Audsley, A. Burns, M. Richardson, K. Tindell, A. Wellings, "Applying New Scheduling Theory to Static Priority Pre-emptive Scheduling", *Software Engineering Journal* 1993 — 지터 RTA (RTOS-M11).
- FreeRTOS-Kernel V11.3.2 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `include/task.h` 210 (`tskIDLE_PRIORITY`), 1103 (`uxTaskPriorityGetFromISR` — Set 의 FromISR 판 없음), 2035 (idle hook 블로킹 금지); `tasks.c` 2414 (`xTaskDelayUntil`), 2844 (`vTaskPrioritySet`), 4831 (`xTaskIncrementTick`), 5000–5007 (tick hook 호출), 5247 (`ulRunTimeCounter` 누적), 5856 (`configIDLE_SHOULD_YIELD`), 8521 (`ulTaskGetRunTimeCounter`); `timers.c` 336 (`xTimerCreate`); `include/FreeRTOS.h` 376–377. 커널 트리 전체에 "sporadic"/"deferrable" 0건 (grep) — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- macOS SDK 26.4 `usr/include/unistd.h` 138, 148 (`_POSIX_SPORADIC_SERVER`, `_POSIX_THREAD_SPORADIC_SERVER` = −1), `pthread/pthread_impl.h` 54–56 (SCHED_OTHER/FIFO/RR 만).
- POSIX.1-2017 `sched_setparam` / `<sched.h>` — `SCHED_SPORADIC`, `sched_ss_low_priority`, `sched_ss_repl_period`, `sched_ss_init_budget`, `sched_ss_max_repl` — <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sched.h.html>
- Linux kernel docs — Deadline Task Scheduling (`SCHED_DEADLINE`, CBS) — <https://docs.kernel.org/scheduler/sched-deadline.html>
- 관련 레슨: RTOS-M11 (RM/RTA, 지터·블로킹 항), RTOS-M12 (EDF, SCHED_DEADLINE), RTOS-M02 (ISR 지연 처리), RTOS-B04 (우선순위 선택), RTOS-B06 (`delay_until`), RTOS-B12 (소프트웨어 타이머), RTOS-M04/M05 (블로킹 항), RTOS-A03 (WCET 측정), RTOS-A04 (확장 RTA — ISR 부하), RTOS-A08 (SMP).

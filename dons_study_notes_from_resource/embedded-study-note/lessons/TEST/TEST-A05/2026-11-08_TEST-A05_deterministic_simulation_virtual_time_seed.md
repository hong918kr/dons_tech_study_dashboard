---
id: TEST-A05
time: 110분
updated: 2026-11-08
---
# TEST-A05 결정론적 시뮬레이션 테스트: 가상 시간과 재현 가능한 seed

## 학습 목표

- 펌웨어 로직을 HAL 뒤로 숨기고, **가상 시계 + 이벤트 큐 + 시드 PRNG**로 시간·타이머·UART·인터럽트를 흉내 낸 시뮬레이터를 만든다 (벽시계 없음).
- 우선순위 인터럽트 선점을 **단일 스레드 중첩 호출**로 모델링하고, 타임아웃 vs RX 완료 경쟁을 seed sweep으로 찾아낸다.
- 실패한 seed를 **비트 단위로 동일하게** 재현하고(event-trace hash·trace 전체 비교), 결정 테이프를 줄여서 **13줄짜리 최소 실패 trace**로 만든다.
- 고침을 하나씩 켜면서 버그 클래스가 하나씩 사라지는 것을 같은 sweep으로 보이고, 전부 고친 펌웨어가 20000 seed를 통과함을 확인한다.
- 시뮬레이션의 **커버리지 경계**를 말할 수 있다: 시뮬이 찾는 것과 실제 스레드 스트레스(SIV-M09)가 찾는 것은 서로 다르다.

## 1. 문제

펌웨어 현장에서 가장 비싼 버그는 "HIL 랙에서 사흘에 한 번 죽는데 로그를 켜면 안 나오는" 타이밍 버그다 — **타이밍 의존 버그를 재현 가능하게 만드는 방법**은 안전 관련 펌웨어와 실리콘 검증 면접에서 그대로 나오는 질문이고, 답은 "시간과 순서를 코드가 소유하게 만든다"이다.

이 레슨의 대상은 UART로 장치에 요청을 보내고 응답을 기다리는 **요청/응답 드라이버**다. 응답이 오면 RX-complete ISR이 요청을 완료하고, 안 오면 타임아웃 타이머 ISR이 완료한다. 두 ISR이 **같은 요청 하나를 놓고 경쟁**한다. 실제 보드에서는 응답이 타임아웃 경계(±수 µs)에 걸릴 때만 터지는 버그인데, 이걸 시드 하나로 재현 가능하게 만드는 것이 목표다.

구조는 네 층이다. 펌웨어는 **HAL 밖을 절대 보지 않는다** — 그래서 같은 코드가 시뮬레이터에도, 실제 Cortex-M에도 그대로 올라간다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">시뮬레이터 구조: 펌웨어는 HAL만 본다</text>
  <rect x="20" y="40" width="720" height="62" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="34" y="62" fill="currentColor" font-weight="bold">시험 (Test)</text>
  <text x="34" y="86" fill="currentColor" font-size="12">앱 워크로드 · 장치 모델(지연/유실) · 불변식 checker · seed sweep · shrink</text>
  <rect x="20" y="116" width="340" height="62" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="34" y="138" fill="currentColor" font-weight="bold">FW (시험 대상)</text>
  <text x="34" y="162" fill="currentColor" font-size="12">fw_submit · fw_on_uart_rx · fw_on_timer</text>
  <rect x="400" y="116" width="340" height="62" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="414" y="138" fill="currentColor" font-weight="bold">실제 보드라면</text>
  <text x="414" y="162" fill="currentColor" font-size="12">TIMx/USARTx 레지스터, NVIC, PRIMASK</text>
  <rect x="20" y="192" width="720" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="34" y="222" fill="currentColor" font-weight="bold">HAL 경계: hal_now_us · hal_timer_start/stop · hal_uart_send · hal_irq_save/restore · hal_window</text>
  <rect x="20" y="256" width="720" height="62" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="34" y="278" fill="currentColor" font-weight="bold">코어 (Core): 벽시계·스레드 없음</text>
  <text x="34" y="302" fill="currentColor" font-size="12">가상 시계 now · 이벤트 큐(t, tie, seq) · 결정 테이프(모든 무작위의 관문) · trace hash</text>
</svg>
```

시뮬레이터가 지켜야 할 규칙은 세 가지다. ① **시간은 코어가 정한다**: 다음 이벤트의 시각으로 시계를 점프한다. ② **무작위는 전부 `sim_choice()` 하나를 지난다**: 그래서 seed 하나 또는 그 결정 열(테이프)만 있으면 실행 전체가 복원된다. ③ **벽시계·스레드·`rand()`·포인터 값을 쓰지 않는다**. 어기면 "거의 재현되는" 실패가 되고, 이는 재현이 안 되는 것과 같다.

버그는 시각이 아주 좁게 맞을 때만 난다. 아래가 이 레슨의 sweep이 실제로 찾아낸 시나리오(최소화된 trace 그대로)다.

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">타임아웃 경계의 선점: RX ISR의 check와 commit 사이에 타이머 ISR이 끼어든다</text>
  <line x1="70" y1="225" x2="740" y2="225" stroke="currentColor"/>
  <text x="70" y="248" fill="currentColor" font-size="12">t=998</text>
  <text x="430" y="248" fill="currentColor" font-size="12">t=1000 (타임아웃)</text>
  <text x="10" y="70" fill="currentColor" font-weight="bold">RX ISR</text>
  <rect x="70" y="52" width="170" height="30" class="f-acc" stroke="currentColor"/>
  <text x="80" y="72" fill="currentColor" font-size="12">check: active == 1</text>
  <rect x="600" y="52" width="140" height="30" class="f-warn" stroke="currentColor"/>
  <text x="610" y="72" fill="currentColor" font-size="12">commit → DONE ok</text>
  <text x="10" y="128" fill="currentColor" font-weight="bold">타이머 ISR</text>
  <rect x="290" y="108" width="280" height="30" class="f-warn" stroke="currentColor"/>
  <text x="300" y="128" fill="currentColor" font-size="12">check → commit → DONE timeout (끝까지 달림)</text>
  <text x="290" y="168" fill="currentColor" font-size="12">prio 2 &gt; prio 1 이라 RX를 선점 (NVIC 방식 = 중첩 호출)</text>
  <text x="70" y="200" fill="currentColor" font-size="12">같은 요청이 두 번 완료 → 불변식 "요청당 완료 정확히 1번" 위반</text>
  <path d="M170 82 L170 225 M430 138 L430 225 M670 82 L670 225" stroke="currentColor" stroke-dasharray="3 3" fill="none"/>
</svg>
```

### Part A — 코어: PRNG, 이벤트 큐, 해시

`starter.c`의 TODO를 채운다.

| TODO | 함수 | 요구 |
|---|---|---|
| A1 | `rng_next` | xorshift64 (13, 7, 17) 한 스텝 |
| A2 | `ev_before` | `(t, tie, seq)` 사전식 **전순서** 비교 |
| A3 | `eq_pick` | `t <= now`이고 `prio > above_prio`인 것 중 prio 최대, 동률이면 `ev_before` |
| A4 | `hash_u64` | FNV-1a 64를 8바이트에 적용 (구조체 통째로 해시 금지 — padding) |
| A5 | `sim_choice` | 기록/재생을 겸하는 **유일한** 무작위 관문 |

같은 시각 이벤트 세 개의 순서는 seed가 바꾸되, 같은 seed면 항상 같아야 한다 (테스트로 확인).

### Part B — 경쟁을 찾고, 재현하고, 고친다

`dispatch_due` (B1), `sweep` (B5), 그리고 펌웨어 고침 세 개(B2·B3·B4)를 구현한다. 펌웨어의 버그는 일부러 넣어 둔 세 개다.

| 고침 플래그 | 버그 | 증상 (checker가 보는 위반) |
|---|---|---|
| `FW_F_CLAIM` | check와 commit 사이가 ISR에 열려 있음 | `DOUBLE_COMPLETE` (요청 하나가 두 번 완료) |
| `FW_F_MATCH_ID` | RX가 응답 ID를 안 봄 | `STALE_RESP` (늦은 응답이 다음 요청을 완료) |
| `FW_F_STOP_FIRST` | RX 경로가 콜백 **뒤에** 타이머를 끔 | `HANG` (콜백이 켠 다음 요청의 타이머를 꺼 버림, 응답이 유실되면 영원히 대기) |

sweep으로 버그를 찾고(FW_F_NONE에서 위반 발견), 실패 seed를 다시 돌려 hash와 trace가 비트까지 같음을 보이고, seed 없이 **결정 테이프만으로도** 같은 실행이 나오는지 확인한다. 고침을 하나씩 켜면서 위반 종류가 하나씩 사라져야 한다.

### Part C — 실패 trace 최소화

`shrink_tape` (C1)을 구현한다. 실패를 일으킨 결정 테이프에서 덩어리를 지우고 값을 낮춰 보다가, **같은 종류로 계속 실패하는 가장 짧은 테이프**를 남긴다. Part B의 51줄 trace가 13줄이 된다. 이 13줄이 곧 버그 리포트다.

## 2. Problem (English)

> You are given firmware for a request/response UART driver. A request completes when either the RX-complete ISR or the timeout-timer ISR fires, and the two race. Build a **deterministic simulator** that finds the race and makes it reproducible.
>
> 1. Core: implement `uint64_t rng_next(rng_t *)` (xorshift64, shifts 13/7/17); `bool ev_before(const event_t *, const event_t *)`, a total order on `(t, tie, seq)`; `int eq_pick(const evq_t *, uint64_t now, int above_prio)`, returning the highest-priority due event with `prio > above_prio`; `uint64_t hash_u64(uint64_t h, uint64_t v)` (FNV-1a over 8 bytes, low byte first); and `uint32_t sim_choice(uint32_t bound)`, the single decision point: in record mode it draws from the PRNG and appends to a tape, in replay mode it reads the tape (`0` past the end).
> 2. Firmware fixes, behind flags: claim the request under an interrupt lock (`FW_F_CLAIM`), check the response id (`FW_F_MATCH_ID`), and stop the timer before invoking the completion callback (`FW_F_STOP_FIRST`).
> 3. `void sweep(uint64_t first, uint32_t n, uint32_t fixes, sweep_t *out)`: run consecutive seeds, classify the first violation of each run.
> 4. `int shrink_tape(uint32_t *tape, int len, uint32_t fixes, int want, int *runs_out)`: return a shorter tape that still fails with the same violation kind.
>
> Constraints: no wall clock, no threads, no `rand()` in the core; every wait is bounded; the firmware calls only the HAL so the same code can be compiled for a Cortex-M target; the checker is independent of the firmware's own state.
>
> Example: with no fixes, some seed yields `DOUBLE_COMPLETE`; re-running that seed gives an identical event-trace hash; `FW_F_ALL` passes a 4000-seed sweep with zero violations.

## 3. 시작 코드

`starter.c`: 시뮬레이터 코어·HAL·펌웨어·테스트가 모두 들어 있고 `TODO(A*)`, `TODO(B*)`, `TODO(C1)` 자리만 비어 있다. 스텁 상태로 빌드되고 실행도 끝난다(멈추지 않음).

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o /tmp/TEST-A05_st && /tmp/TEST-A05_st
```

실제 실행 결과 앞부분:

```text
--- Part A: PRNG, event queue, hash
FAIL starter.c:484  a == b && a != 0
FAIL starter.c:486  rng_next(&r) == 0x0000000040822041ull
FAIL starter.c:494  i >= 0 && q.a[i].arg == 101
...
--- Part B: seed sweep finds the race, replay, fix
[B] FW_F_NONE: runs=0 fails=0 double=0 stale=0 hang=0, first_seed=0 (none)
...
6/27 passed
```

## 4. 힌트

<details><summary>힌트 1 — 왜 tie와 seq가 둘 다 필요한가</summary>

`tie`는 같은 시각 이벤트의 순서를 **seed가 흔들게** 해 주는 값이다. `seq`는 tie까지 같을 때도 순서가 항상 하나로 정해지게 하는 값이다. 전순서가 아니면 큐 내부 구현(swap-remove, 힙의 불안정성)이 순서를 정해 버리고, 그 구현을 바꾸는 순간 재현이 깨진다.
</details>

<details><summary>힌트 2 — ISR 선점을 스레드 없이 어떻게 만드나</summary>

Cortex-M의 인터럽트 선점은 **같은 스택 위의 중첩 호출**이다. 그러니 시뮬레이터도 중첩 호출로 만들면 된다: `hal_window()`가 시간을 조금 진행시키고, 기한이 지난 더 높은 우선순위 이벤트가 있으면 그 자리에서 핸들러를 호출한다. `dispatch` 안에서 `dispatch_due`가 다시 돌면서 우선순위가 엄격히 올라가므로 깊이는 레벨 수로 제한된다.
</details>

<details><summary>힌트 3 — sim_choice의 기록/재생</summary>

record 모드: `rng_next % bound`를 계산해 `g_tape[g.tape_len++]`에 저장하고 돌려준다. replay 모드: `g.tape_pos < g.tape_len ? tape[g.tape_pos] : 0`을 읽고 `tape_pos`를 증가시킨 뒤 `v % bound`를 돌려준다. shrink가 값을 줄이므로 replay에서 `v % bound`로 범위를 지켜 주는 것이 중요하다.
</details>

<details><summary>힌트 4 — claim의 핵심</summary>

두 경로가 동시에 들어와도 "이 요청은 내가 끝낸다"는 판정이 정확히 한 번만 참이어야 한다. check와 commit을 `hal_irq_save`/`hal_irq_restore`로 묶는다. 그 안에서 `hal_window()`로 시간이 흘러도 인터럽트는 pending으로 남고, `hal_irq_restore`에서 처리된다 — 그러면 늦게 들어온 경로는 이미 끝난 요청을 보고 물러난다.
</details>

<details><summary>힌트 5 — shrink가 재현을 깨지 않게 하려면</summary>

후보 테이프는 `sim_run_tape`로 돌리고, 위반 종류가 `want`와 같을 때만 받아들인다. 받아들이면 `r.tape_used`로 길이를 줄여도 된다 (`tape_used` 뒤의 값은 어차피 읽히지 않았다). 끝낼 때는 줄어든 테이프를 다시 돌려서 실패가 재현되는지 확인한다.
</details>

## 5. 풀이

### Part A — 코어의 설계 이유

**가상 시계.** 시간은 다음 이벤트의 시각으로 점프한다(`sim_body`의 `g.now = g.q.a[i].t`). 타임아웃 1000 µs를 기다리는 데 실제로는 나노초가 든다. 측정값은 아래 "시뮬레이션의 경제학"에 있다.

**이벤트 큐.** 크기 24의 배열 + 선형 스캔이다. 큐가 작고, 면접에서 설명하기 쉽고, 힙으로 바꿔도 전순서 덕분에 결과가 같다.

| 선택 규칙 | 함수 | 비고 |
|---|---|---|
| 다음 시계 점프 대상 | `eq_earliest` | 가장 이른 `(t, tie, seq)` |
| 지금 실행할 인터럽트 | `eq_pick` | 기한이 됐고 현재 prio보다 높은 것 중 **prio 최대** — NVIC가 pending 중 최고 우선순위를 고르는 규칙 |
| 제거 | `eq_take` | swap-remove: 배열 순서가 바뀌어도 전순서 비교라 결과 무관 |

**결정 테이프.** 시뮬레이터의 모든 무작위 결정은 `sim_choice` 하나를 지난다. 응답 지연, 유실, 같은 시각 tie, window 길이, 콜백 체이닝, 요청 개수까지 전부. 그 결과 실행은 **테이프의 순수 함수**가 된다. seed는 테이프를 만드는 압축 표현일 뿐이고, 테이프를 직접 고칠 수 있다는 점이 Part C의 기반이다.

**해시를 구조체로 하지 않는 이유.** `trace_rec`는 `int`와 `uint64_t`가 섞여 padding이 있고, padding 바이트 값은 정해져 있지 않다(C11 6.2.6.1). 필드별로 값을 `hash_u64`에 넣어야 빌드가 달라도 hash가 같다.

### Part B — 경쟁을 시간으로 설명하기

`hal_window()`가 이 시뮬레이터의 핵심이다. 실제 CPU에서는 명령 실행에 시간이 걸리고, 그 사이에 인터럽트가 끼어들 수 있다. 시뮬에서는 "여기서 시간이 흐를 수 있다"를 코드로 표시한다. 펌웨어가 `active`를 읽고(`check`) 쓰는(`commit`) 사이에 window가 있고, 그 사이에 타이머가 만료되면 타이머 ISR이 선점한다.

버그 세 개의 메커니즘을 표로 정리한다.

| 버그 | 필요한 조건 | 왜 일반 테스트에 안 잡히나 |
|---|---|---|
| ① DOUBLE_COMPLETE | 응답이 타임아웃 ±수 µs에 도착 + RX window 안에 타이머 만료 | 평소 지연은 경계에서 수백 µs 떨어져 있다 |
| ② STALE_RESP | 응답이 타임아웃 뒤에 도착 + 다음 요청이 이미 시작됨 | ID 비교가 없어도 평소엔 응답이 요청과 1:1이다 |
| ③ HANG | 콜백 안에서 다음 요청 시작(chain) + 그 요청의 응답 유실 | 응답이 유실되어야 타이머 필요성이 드러난다 |

**실행 결과 (solution.c).** seed 246이 가장 먼저 발견된 `DOUBLE_COMPLETE`이다. 같은 seed로 두 번 돌린 hash가 같고, trace 전체 `memcmp`도 같고, seed 없이 테이프만 넣어도 같다.

```text
[B] FW_F_NONE: runs=4000 fails=1548 double=34 stale=976 hang=538, first_seed=3 (STALE_RESP)
[B] seed 246: viol=DOUBLE_COMPLETE id=5 events=51 hash=fda6ec606e40ce54 / replay hash=fda6ec606e40ce54
[B] CLAIM          : fails=1523 double=0 stale=980 hang=543
[B] CLAIM+MATCH    : fails=647 double=0 stale=0 hang=647
[B] CLAIM+MATCH+STOP: fails=0
```

고침 하나마다 **그 클래스만** 사라진다. `CLAIM`을 켠 직후에 stale과 hang이 그대로인 것을 보면 "한 번에 다 고쳤다"고 믿는 것이 왜 위험한지 알 수 있다. 첫 위반만 세므로 앞 버그를 고치면 뒤 버그의 숫자가 약간 움직인다 (예: hang 538→543).

**각 고침의 코드.**

- **CLAIM**: `check`와 `commit`을 `hal_irq_save`/`hal_irq_restore`로 묶는다. 잠근 구간에서 `hal_window()`로 시간이 흘러도 인터럽트는 pending으로 남는다(`g.masked`).
- **MATCH_ID**: RX가 `resp == id`일 때만 claim을 성공시킨다. 늦은 응답은 버려진다.
- **STOP_FIRST**: `done()` 콜백 **전에** 타이머를 끈다. 콜백이 다음 요청을 시작하면 새 타이머가 켜지는데, 끄는 순서가 뒤면 방금 켠 타이머를 꺼 버린다.

타이머 취소의 현실: `hal_timer_stop`은 **아직 만료 전인 타이머만** 지운다. `t <= now`로 이미 만료된 이벤트는 "pending 비트가 선 것"처럼 그대로 남아서 ISR이 돌지만, `fw_claim`이 `active`를 보고 물러나므로 안전하다. 실제 MCU에서 타이머를 멈추어도 이미 선 플래그(예: STM32의 UIF)나 NVIC pending은 저절로 지워지지 않는 것과 같다 (부품별 레퍼런스 매뉴얼에서 확인할 것).

### Part C — 줄이기

줄이기는 테이프에 대한 탐색이다. 덩어리를 지워 보고, 값을 0/절반/1 작게 만들어 보고, 위반 종류가 같으면 받아들인다. 테이프 값이 작아지고 짧아지면 시나리오가 단순해진다(아래 결과는 요청 0번 하나에서 바로 터진다).

```text
[C] tape 51 -> 9 decisions, events 51 -> 13, 75 sim runs
[C] minimal failing trace (DOUBLE_COMPLETE):
  t=     0 prio= 0  EV app       0
  t=     0 prio= 0  timer_start  1000
  t=     0 prio= 0  uart_tx      0
  t=   998 prio= 1  EV rx        0
  t=   998 prio= 1  rx.check     0
  t=  1000 prio= 2  EV timer     0
  t=  1000 prio= 2  tmr.check    0
  t=  1000 prio= 2  tmr.commit   0
  t=  1000 prio= 2  DONE timeout 0
  t=  1000 prio= 1  rx.commit    0
  t=  1000 prio= 1  DONE ok      0
  t=  1000 prio= 1  VIOLATION    65536
  t=  1000 prio= 1  timer_stop   0
27/27 passed
```

`rx.check`와 `rx.commit` 사이에 `EV timer`가 끼어든 것이 한눈에 보인다 — 인터럽트 선점의 정확한 위치까지. `VIOLATION 65536`은 `(V_DOUBLE<<16)|id`이다. 위반 뒤 `timer_stop`이 찍히는 것은 RX ISR이 남은 줄을 끝까지 실행해서다.

자주 묻는 함정: **줄어든 테이프는 "그 스케줄"이 아니다.** 펌웨어가 바뀌면 `sim_choice` 호출 순서도 바뀌어서 같은 테이프가 다른 스케줄이 된다. 그래서 고친 펌웨어(FW_F_ALL)에 같은 테이프를 돌리는 테스트는 "우연히 통과"가 아니라 "이 입력에서도 안전"을 보이는 데에만 쓸 수 있다. 회귀 방지용 코퍼스는 트레이스(혹은 결정 테이프 + 그때의 펌웨어 버전)로 남기는 게 원칙이다.

### 시뮬레이션의 경제학 (demo_sweep.c)

<!-- code: demo_sweep.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_sweep.c -o /tmp/TEST-A05_ds && /tmp/TEST-A05_ds
```

머신: Apple M6 (Super 2 + Performance 4 + Efficiency 6), Apple clang version 21.0.0 (clang-2100.0.123.102), 측정 시 `uptime` load average 23.5 (다른 작업과 CPU를 공유 중, 비율만 믿을 것).

```text
digest(none, 2000 seeds) = ee30fc2cc35b8088
digest(all,  2000 seeds) = feb06fc1715ed578

[1] seed sweep by fix set
fixes                 runs   fails  double   stale    hang  first_seed  first_viol
none                  20000    7725     160    4867    2698           3  STALE_RESP
CLAIM                 20000    7608       0    4889    2719           3  STALE_RESP
CLAIM+MATCH           20000    3216       0       0    3216           6  HANG
CLAIM+MATCH+STOP      20000       0       0       0       0           0  -

[2] virtual vs wall
20000 seeds, virtual 89.698 s, wall 0.033 s, speedup 2757x, 1.63 us/seed
```

- **결정성 확인**: `digest` (2000개 seed의 hash를 다시 해시한 값)는 `-O0`, `-O2`, ASan+UBSan 빌드에서 **세 번 다 위와 똑같았다** — 컴파일러 최적화·메모리 배치가 달라도 실행이 같다는 뜻이다.
- **속도**: 가상 89.7초를 벽시계 33 ms에 돌렸다. 실제 보드라면 이 시나리오에 90초가 걸리고, 그 90초 안에 경계 타이밍이 몇 번 맞는지도 운이다.
- **희귀 버그의 비용**: `DOUBLE_COMPLETE`는 seed의 0.8%(160/20000)에서만 나왔다. 어느 seed인지 시드만 적어 두면 그 실행을 언제든 다시 볼 수 있다.

벽시계는 이 demo에만 있다. 코어·HAL·펌웨어·테스트(solution.c)에 벽시계와 스레드가 없다는 것은 아래 grep으로 직접 확인했다.

```bash
grep -nE 'clock_gettime|gettimeofday|[^_a-z]time\(|[^_a-z]rand\(|mach_absolute|pthread_create|usleep|nanosleep' solution.c starter.c practice.c
# (출력 없음, grep exit 1)
```

### 같은 코드가 Cortex-M에서도 — HAL이 진짜 경계인지 확인

FW 구간을 잘라 내서 `thumbv7em-none-eabi`로 컴파일했다. `demo_m4_hal.h`는 HAL 선언과 `PRIMASK` 기반 `hal_irq_save/restore`만 준다.

<!-- code: demo_m4_hal.h -->

```bash
sed -n '/FW_BEGIN/,/FW_END/p' solution.c > /tmp/TEST-A05_work/fw_only.c
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -std=c11 \
      -Wall -Wextra -Werror -include demo_m4_hal.h -c /tmp/TEST-A05_work/fw_only.c -o /tmp/TEST-A05_work/fw_m4.o
xcrun llvm-nm /tmp/TEST-A05_work/fw_m4.o; xcrun llvm-size /tmp/TEST-A05_work/fw_m4.o
```

```text
00000000 T fw_init
000000b4 T fw_on_timer
00000040 T fw_on_uart_rx
00000014 T fw_submit
00000000 b g_fw
         U hal_timer_start
         U hal_timer_stop
         U hal_uart_send
   text	   data	    bss	    dec	    hex	filename
    288	      0	     16	    304	    130	/tmp/TEST-A05_work/fw_m4.o
```

경고 없이 컴파일되고, 미해결 심볼이 **HAL 세 개뿐**이다(`hal_window`, `hal_mark`, `hal_irq_*`는 타깃에서 인라인으로 사라진다). 288바이트 text가 시뮬 sweep을 통과한 바로 그 소스다. 실제 보드로 올리는 작업은 `TIMx`/`USARTx` 레지스터로 세 함수를 구현하는 것뿐이고, 여기서 실제로 돌려 본 것은 아니다.

### 커버리지의 경계: 시뮬 vs 실제 스레드 (SIV-M09와 짝)

`demo_realthreads.c`는 같은 check-then-commit을 **실제 스레드 2개**로 경쟁시켜 어떤 순서가 나오는지 센다. `R`/`T`는 check, `r`/`t`는 commit이다. active 비트와 티켓을 한 워드에 넣어서, 순서 기록과 상태 관찰이 원자적 RMW 하나로 일어나게 했다.

<!-- code: demo_realthreads.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_realthreads.c -o /tmp/TEST-A05_rt && /tmp/TEST-A05_rt
```

load average 23.5인 상태에서 300000 라운드를 3회 실행한 실제 출력의 첫 번째:

```text
real threads: rounds=300000 wall=0.094 s, double-commit=4783 (1.59%), distinct orderings=6
   RrT    130232
   TtR    164985
   RTtr   1523
   RTrt   119
   TRrt   2876
   TRtr   265
```

나머지 두 번의 double-commit은 4.70%, 5.60%로 흔들렸다. 시뮬은 같은 코드의 같은 질문에 다음과 같이 답했다 (`demo_sweep.c`의 [4], 20000 seed, 경쟁이 있었던 요청만):

```text
[3] blind spot: window_max=0 (no execution-time model, buggy fw)
window_max=0: runs=20000 fails=7758 double=0 stale=4986 hang=2772

[4] orderings the single-core model can reach (buggy fw)
TMR > RX  : contested=4052, distinct orderings=2
   TtR    3892
   RTtr   160
RX  > TMR : contested=4068, distinct orderings=3
   TtR    3733
   TRrt   197
   RrT    138
```

| 순서 | double? | 실제 스레드 | 시뮬 (TMR &gt; RX) | 시뮬 (RX &gt; TMR) | 의미 |
|---|---|---|---|---|---|
| `RrT` | 아님 | 있음 | 없음 (타이머는 stop으로 취소됨) | 138 | RX가 먼저 끝냄 |
| `TtR` | 아님 | 있음 | 3892 | 3733 | 타임아웃이 먼저 끝내고 응답은 늦게 도착 |
| `RTtr` | **double** | 있음 | **160** | 없음 | 타이머 ISR이 RX를 선점 |
| `TRrt` | **double** | 있음 | 없음 | **197** | RX ISR이 타이머를 선점 |
| `RTrt` | double | 있음 | 없음 | 없음 | check가 겹치고 commit이 교차 — 단일 코어 우선순위 모델에선 불가 |
| `TRtr` | double | 있음 | 없음 | 없음 | 같은 이유로 불가 |

여기서 세 가지를 말할 수 있어야 한다.

1. **시뮬은 "그 플랫폼에서 일어날 수 있는 순서"만 만든다.** TMR &gt; RX에서 `TRrt`는 시뮬에 없고, 실제로도 단일 코어 MCU에서는 일어날 수 없다. 코어 2개 이상인 칩이면 `RTrt`, `TRtr`까지 일어나므로 멀티코어 모델이 필요하다 (실제 스레드 + yield point, SIV-M09의 토큰 스케줄러가 그 영역이다).
2. **시뮬은 모델에 없는 것을 못 본다.** `window_max=0`으로 시간 모델을 끄면 같은 버그 펌웨어가 `double=0`이다 (20000 seed 전부). 위 [3]의 그 줄이 시뮬의 맹점이다. `hal_window()`를 안 넣은 줄의 race는 영원히 안 보인다.
3. **실제 스레드는 시뮬이 전제하는 메모리 모델 바깥을 본다.** plain `bool` 버전을 TSan으로 돌리면 data race로 잡힌다 (시뮬은 인터리빙만 탐색하고 메모리 순서는 다루지 않는다). 반대로 `_Atomic` 버전은 TSan이 조용한데 double-commit은 그대로 난다 — data race가 없어도 **check-then-act 원자성 위반**은 남는다는 뜻이다 (SIV-M09 Part와 같은 교훈).

```text
$ cc -std=c11 -g -O1 -fsanitize=thread -pthread -DPLAIN_RACE -DROUNDS=2000 demo_realthreads.c -o /tmp/TEST-A05_work/rt_tsan
$ /tmp/TEST-A05_work/rt_tsan ; echo exit=$?
==================
WARNING: ThreadSanitizer: data race (pid=44104)
  Read of size 1 at 0x000100664000 by thread T1:
    #0 racer demo_realthreads.c:75 (rt_tsan:arm64+0x100000d18)

  Previous write of size 1 at 0x000100664000 by thread T2:
    #0 racer demo_realthreads.c:76 (rt_tsan:arm64+0x100000c24)

  Location is global 'g_active' at 0x000100664000 (rt_tsan+0x100008000)
…
exit=134
```

`_Atomic` 버전을 TSan으로 돌린 결과 (같은 2000 라운드, 경고 없음):

```text
real threads: rounds=2000 wall=3.933 s, double-commit=1268 (63.40%), distinct orderings=6
```

**측정 실수 하나 (실제로 겪었다).** 처음 만든 `demo_realthreads.c`는 티켓을 check와 **따로** 뽑았다(`load` 다음에 `fetch_add`). 그랬더니 이 불가능한 순서가 나왔다:

```text
real threads: rounds=300000 wall=0.117 s, double-commit=222145 (74.05%), distinct orderings=7
   RrT    40460
   TtRr   112488
   RrTt   106237
   …
```

`RrTt`(T가 r 뒤에 check했는데 commit함)는 로직상 불가능하다. 원인: T가 `active`를 읽고 나서 티켓을 뽑기 전에 틈이 있어서, 기록된 순서와 실제 상태 관찰 순서가 달랐다. 위의 표는 active와 티켓을 **같은 원자적 RMW 하나**로 얻도록 고친 뒤의 수치다. 측정 도구가 경쟁 자체를 바꾸는 경우(관찰자 효과)는 실전에서도 흔하다.

### 시뮬이 찾는 것 / 스트레스가 찾는 것 (SIV-M09 경계)

| 관점 | 결정론적 시뮬레이션 (이 레슨) | 실제 스트레스 (SIV-M09) |
|---|---|---|
| 재현 | seed/테이프로 항상 | 어렵다 (시드 고정 토큰 스케줄러로 일부 가능) |
| 시간 | 가상, 수천 배 빠름 | 실제 시간 |
| 경계 타이밍 | 분포를 직접 설계 (±100 µs 밴드) | 우연 또는 race window 넓히기 |
| 잡는 것 | 인터리빙·프로토콜·타이머 로직·장애 시나리오 | 실제 코어 수, 캐시/메모리 순서, 버스 경합, 전력·발열 |
| 못 잡는 것 | 모델에 없는 하드웨어, 메모리 재배치 | 희귀한 시나리오, 재현 불가 |

펌웨어 조직이라면 둘 다 쓴다: SIL에서 시드 sweep으로 로직을 밟아 없애고, HIL/랩에서 스트레스(CPU·DMA·메모리 동시 부하)로 시뮬이 모델링하지 않은 영역을 때린다.

### 같은 아이디어가 쓰이는 곳 (확인된 사실이 아니라 기억 기준의 포인터)

FoundationDB 팀은 단일 스레드 결정론적 시뮬레이션으로 분산 데이터베이스를 시험했다고 알려져 있고(Will Wilson의 Strange Loop 2014 발표로 기억한다), TigerBeetle의 시뮬레이터 VOPR도 같은 계열로 소개되어 있다. **PCT**(Burckhardt et al., ASPLOS 2010로 기억)는 무작위 우선순위와 change point로 "깊이 d" 버그의 검출 확률 하한을 준다고 알려져 있다 — 이 레슨의 practice에서 토이로 확인했다. 임베디드 쪽은 Zephyr `native_sim`, Renode처럼 펌웨어를 호스트에서 시뮬레이션하는 도구들이 있는데, 결정성 보장은 도구별로 다르니 문서를 볼 것(여기서는 실행하지 않았다). 수치·세부 사항은 각 출처를 확인해야 한다.

## 6. 정답 코드

바뀐 부분은 TODO 자리뿐이다. 펌웨어 고침은 `fw_claim` (B2·B3)과 `fw_on_uart_rx` (B4)에 있고, 나머지는 코어 구현이다.

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o /tmp/TEST-A05_so && /tmp/TEST-A05_so
# 27/27 passed, exit 0 (6회 실행, stdout 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o /tmp/TEST-A05_san && /tmp/TEST-A05_san
# 27/27 passed, ASan/UBSan clean
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o /tmp/TEST-A05_tsan && /tmp/TEST-A05_tsan
# 27/27 passed, TSan clean (스레드가 없으니 당연하지만, 코어에 숨은 스레드가 없다는 확인)
```

## 7. 자주 하는 실수

**1) 이벤트 순서를 전순서로 안 만든다.**

```c
/* 틀림: t만 비교 — 같은 시각이면 큐 구현이 순서를 정한다 */
static bool ev_before(const event_t *x, const event_t *y) { return x->t < y->t; }
```

같은 시각 이벤트는 흔하다. 순서가 큐 내부 구현(스왑, 힙)에 달리면 그 구현을 고치는 순간 모든 기록된 재현이 깨진다. `(t, tie, seq)` 전순서로 비교한다.

**2) 결정 관문을 두 개 이상 만든다.**

```c
/* 틀림: 응답 지연은 rand(), 나머지는 sim_choice() */
uint32_t lat = 100 + (uint32_t)(rand() % 500);
```

`rand()`나 포인터 값, 해시맵 순회 순서, 초기화 안 된 메모리가 한 곳이라도 섞이면 "거의 재현"이 된다. 모든 비결정 입력을 `sim_choice` 하나로 모은다.

**3) 구조체를 통째로 memcmp/해시한다.**

```c
/* 틀림: padding 바이트가 들어간다 (값은 불특정, C11 6.2.6.1) */
memcmp(&a, &b, sizeof a);
```

필드별로 비교·해시한다. 이 레슨의 `trace_log`처럼 값을 하나씩 `hash_u64`에 넣는다. 테스트의 trace 비교는 한 프로세스에서 같은 코드가 채운 배열을 비교하므로 허용되지만, 교차 빌드·교차 플랫폼 비교는 hash로 한다.

**4) 콜백 뒤에 타이머를 끈다.**

```c
g_fw.done(id, FW_OK, resp);      /* 콜백이 다음 요청을 시작하고 타이머를 켠다 */
hal_timer_stop();                /* 방금 켠 타이머를 꺼 버린다 */
```

고침: `hal_timer_stop()`을 `done()` **앞**으로. 체이닝이 없는 시험에서는 절대 안 나타나는 버그다 — 이 버그가 `HANG`으로 나오려면 체이닝 + 응답 유실이 같이 일어나야 한다.

**5) "타이머를 껐으니 이제 안 온다"고 믿는다.**

취소는 **이미 만료된 건을 지우지 못한다** (`hal_timer_stop`이 `t > now`만 지우는 이유). 실제 칩에서도 인터럽트 마스크 안에서 플래그가 이미 서 있으면 ISR은 온다. 고침: ISR 쪽에서 상태(`active`)를 다시 보고 물러난다. 이 레슨의 `fw_claim`이 그 역할이다.

**6) 시뮬 통과를 곧 안전이라 부른다.**

시뮬은 모델이 포함한 것만 본다. `window_max=0`인 시뮬에서 double이 0이었던 것을 기억하자. 면접에서는 항상 "이 시뮬레이터가 모델링하지 않는 것"까지 같이 말한다.

## 8. 면접 Q&A

<details><summary>Q1. 타이밍에 의존하는 간헐 버그를 어떻게 재현 가능하게 만듭니까?</summary>

시간과 순서를 코드 밖에서 안으로 가져온다. HAL 뒤에 펌웨어를 두고, 타이머·UART·인터럽트를 가상 시계와 이벤트 큐로 시뮬레이션하고, 무작위 결정을 시드 PRNG 하나로 모은다. 그러면 seed만 있으면 실행이 재현된다. 이 레슨에서는 seed 246이 `DOUBLE_COMPLETE`를 만들고 hash까지 동일하게 재현됐다.

꼬리질문: "seed를 알아도 실행이 달라질 수 있는 원인은?" → 벽시계, `rand()`, 초기화 안 된 메모리, 해시 순서, 포인터 주소, 전순서가 아닌 정렬, 스레드. 그래서 digest를 `-O0`/`-O2`/ASan 빌드에서 비교한다 (이 레슨의 값은 셋이 같았다).
</details>

<details><summary>Q2. 가상 시간을 쓰면 왜 빠르고, 무엇을 잃습니까?</summary>

다음 이벤트 시각으로 시계를 점프하니까 대기 시간이 0이다. 측정: 가상 89.7초를 33 ms에 실행(load 23에서 약 2757배, 비율만 신뢰). 잃는 것은 **코드 실행 시간**이다: 그래서 `hal_window()`로 "여기서 시간이 흐를 수 있다"를 명시해야 한다. window를 끄면 double-complete가 0건이 되는 것을 실측했다. 이 값의 분포(몇 µs를 쓸지)는 측정된 최악 실행 시간(WCET)을 참고해서 잡는다.

꼬리질문: "window를 코드 어디에 넣나?" → shared state를 읽고 쓰는 줄 사이. 사람이 빠뜨리면 그 race는 안 보인다. Loom/Shuttle 같은 도구는 동기화 지점마다 자동으로 넣어 준다고 알려져 있다 (사용해 보지는 않았다).
</details>

<details><summary>Q3. 인터럽트 선점을 스레드 없이 어떻게 시뮬레이션합니까? SMP면요?</summary>

Cortex-M의 선점은 같은 스택 위의 중첩 호출이므로 그대로 중첩 호출로 만든다. `hal_window`/`hal_irq_restore`에서 `eq_pick(now, cur_prio)`로 더 높은 prio의 기한 도달 이벤트를 고르면 그 자리에서 핸들러를 호출한다. 우선순위가 엄격히 올라가서 깊이는 레벨 수로 제한된다.

꼬리질문: "SMP에선?" → 코어 간 진짜 병렬은 중첩으로 표현 안 된다. 스레드/코루틴을 쓰고 공유 접근마다 yield point를 두는 스케줄러(SIV-M09의 토큰 스케줄러)로 가야 한다. 그때의 대가: 실행 속도와 yield point 누락. 그리고 시뮬이 sequential consistency만 표현하면 메모리 재배치 race는 여전히 안 보인다.
</details>

<details><summary>Q4. timeout 타이머와 RX-complete가 같은 요청을 완료하는 race의 정석 해법은?</summary>

"완료 권리(ownership)를 정확히 한 번만 넘긴다." `check`와 `commit`을 인터럽트 마스크로 묶어 원자적으로 만들고(이 레슨의 `FW_F_CLAIM`), 패배한 경로는 조용히 물러난다. 추가로 ① 응답 ID 비교(늦은 응답 차단) ② 콜백 전에 타이머 취소 ③ 타이머 취소 실패는 정상이라 가정하고 핸들러에서 상태 재확인.

꼬리질문: "같은 우선순위로 만들면 해결되지 않나?" → 단일 코어라면 같은 prio ISR끼리는 선점이 없어 check-commit 사이는 안전해진다. 하지만 ID 불일치(STALE)와 콜백 순서(HANG)는 선점과 무관하게 남고, 코어가 둘이면 병렬 실행이라 원래 문제가 돌아온다. 락프리로 하려면 `atomic_compare_exchange`로 `PENDING→DONE`을 단일 주체만 성공하게 한다 (C-A03 원자 연산 참고).

꼬리질문 2: "mutex로 해결?" → ISR에서 mutex는 잠들 수 있어 금지다 (ELX-M04에서 실제 커널 splat으로 확인).
</details>

<details><summary>Q5. 시뮬레이션에서 통과하면 출시해도 됩니까?</summary>

아니다. 시뮬은 **모델이 포함한 순서·시간·장애만** 탐색한다. 이 레슨의 `demo_realthreads`는 시뮬(단일 코어 우선순위 모델)이 만들 수 없는 순서 `RTrt`, `TRtr`을 실제 스레드에서 보여 준다 — 단일 코어라면 괜찮지만 멀티코어라면 시뮬이 틀린 안심을 준다. 반대로 시뮬은 실제 보드에서 드물게만 일어나는 경계 타이밍을 수천 배 빠르게, 재현 가능하게 밟는다. 그래서 둘을 같이 쓴다: 로직은 시뮬로, 하드웨어·코어 수·버스 경합·캐시는 SIV-M09 같은 동시 부하 스트레스로.

꼬리질문: "스트레스에서 무엇을 동시에 거나?" → CPU memtest, DMA 트래픽, 인터럽트 폭주를 동시에 걸고 트래픽별 독립 checker를 붙인다 (SIV-M09).
</details>

<details><summary>Q6. 실패 trace를 어떻게 줄이고, 왜 seed만으로는 부족합니까?</summary>

실패한 실행의 **결정 테이프**를 대상으로 탐색한다. 덩어리를 지우고 값을 낮추면서 같은 종류로 계속 실패하는 것만 받아들인다. 51 결정 → 9 결정, 51 이벤트 → 13 이벤트, 시뮬 실행 75번이면 끝났다.

seed만으로 부족한 이유: seed는 "PRNG에게 처음 넣는 값"이라 코드가 조금만 달라져도(`sim_choice` 호출이 하나 늘면) 다른 실행이 된다. 그래서 회귀용으로는 테이프(또는 스케줄 trace)와 펌웨어 빌드 ID를 같이 저장한다.

꼬리질문: "코드를 고치면 예전 seed는 무의미해지나?" → 재현 도구로서는 그렇다. 코드가 변한 뒤에도 의미 있는 기록은 "실패한 이벤트 순서 trace"다. 시뮬레이터가 trace-driven 모드로 그 순서를 강제 재생하면 된다 (이 레슨의 테이프는 결정 수준이라 코드 변경에 그만큼 약하다).
</details>

<details><summary>Q7. 왜 seed 범위 sweep이 "무작위 순회"보다 나은가요? PCT는 뭘 더해 줍니까?</summary>

sweep은 seed 하나가 실행 전체를 결정하므로 병렬로 쪼개기 쉽고, 어디까지 봤는지 말할 수 있다 (seed 1..N 커버). 그런데 균등 무작위 스케줄러는 "특정 순서가 연속으로 이어져야 하는" 깊은 버그를 거의 못 찾는다. 연습 4에서 균등 스케줄러는 20000회에 12번(0.060%), PCT는 873번(4.365%) 찾았다 — 이론 하한 1/(n·K^(d-1)) = 4.17%(n=2, K=12, d=2)를 만족했다. PCT 이론 자체는 원 논문을 확인할 것.

꼬리질문: "그럼 언제 PCT, 언제 균등?" → 깊이가 작은 대량의 스케줄 탐색은 PCT가 유리하고, 시간 값 자체(타임아웃 경계)가 중요한 버그에는 이 레슨처럼 **지연 분포를 경계에 몰아주는 쪽**이 더 잘 듣는다. 둘은 경쟁이 아니라 다른 축이다.
</details>

<details><summary>Q8. 이 시뮬에서 실패 seed를 찾았을 때 디버깅 절차는요?</summary>

1. 실패 seed와 hash, 위반 종류를 그대로 CI 리포트에 남긴다. 2. 같은 seed로 재실행해 재현을 확인한다(hash 동일). 3. shrink로 최소 trace를 얻는다 — 이 레슨은 13줄이 나왔고 그 안에 `rx.check`/`rx.commit` 사이의 `EV timer`가 그대로 보였다. 4. 고침 후 같은 시드 범위를 다시 돌려 그 클래스가 0이 되고 다른 클래스가 안 늘었는지 본다. 5. 최소 시나리오를 scripted test(회귀)로 고정한다.

꼬리질문: "두 번 돌렸는데 hash가 다르면?" → prefix hash를 배열로 남겨 **이진 탐색으로 첫 분기 지점**을 찾는다(연습 3). 그 지점의 이벤트가 비결정 입력이 들어온 곳이다.
</details>

## 9. 추가 연습

연습 답은 `<details>` 안에 있고, 전체를 `practice.c`로 한 번에 검증한다.

<details><summary>연습 1 — check/commit 인터리빙을 전수 열거하라</summary>

`R<r`, `T<t` 조건에서 순서는 C(4,2)=6개다. 실제 코드 기준으로 double이 몇 개인가? 타이머가 RX보다 높은 우선순위(T와 t가 붙어 있어야 함)일 때 도달 가능한 것은? 답: 6개 중 double 4개, 도달 가능 3개, 그중 double은 `RTtr` 하나. 위 시뮬의 표와 일치한다. 실제 실행: `[1] orders=6 double=4 reachable(TMR>RX)=3 reachable&double=1`.
</details>

<details><summary>연습 2 — 32비트 tick wrap을 가상 시계로 시험하라</summary>

벽시계로 wrap 버그를 보려면 71.6분(2^32 µs) 기다려야 한다. 가상 시계는 `0xFFFFFF00`에서 시작하면 된다. `deadline_passed(now, d) = (int32_t)(now - d) >= 0`로 비교한다. `now >= d`는 시작하자마자 true라는 것을 CHECK로 확인한다 (마감까지 2^31 µs 미만이어야 하는 전제도 같이 말할 것).
</details>

<details><summary>연습 3 — 두 trace가 처음 갈라진 곳을 O(log n)에 찾아라</summary>

prefix hash 배열을 저장하면 한번 달라진 hash는 계속 다르다(충돌이 없다고 가정). 이진 탐색으로 첫 분기 인덱스를 구한다. 디버깅에서 "왜 재현이 깨졌나"를 찾을 때 그대로 쓰는 도구다. CHECK: 같으면 -1, 300번 인덱스를 바꾸면 300, 0번을 바꾸면 0.
</details>

<details><summary>연습 4 — PCT와 균등 스케줄러를 비교하라</summary>

스레드 A(2스텝), B(10스텝), 버그는 "a0 다음, a1 전에 B가 전부 실행"될 때만 난다(깊이 2). 균등 스케줄러는 B 10스텝을 연달아 받아야 해서 약 2^-11이고, PCT는 A가 먼저 우선순위를 받고(1/2) change point가 정확히 a0 직후면(약 1/12) 성공한다. 실제 실행: `[4] trials=20000 uniform=12 (0.060%) pct=873 (4.365%) bound=4.167%`. 하한 1/(n·K^(d-1)) 식 자체는 원 논문을 참조.
</details>

<details><summary>연습 5 — ddmin으로 실패 입력을 줄여라</summary>

입력 목록에서 조각을 제거하는 Delta Debugging(ddmin)을 구현한다. 실패 조건이 "3과 11이 둘 다 있을 때"면 16개가 2개로 줄어든다. 실제 실행: `[5] ddmin: 16 -> 2 items, 19 tests`. 본문의 `shrink_tape`는 같은 착상을 **결정 테이프**에 적용한 것이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o /tmp/TEST-A05_pr && /tmp/TEST-A05_pr
# 17/17 passed, exit 0. ASan/UBSan, TSan 모두 clean
```

## 10. 핵심 정리

- 타이밍 버그를 재현하려면 시간과 순서를 코드가 소유하게 만든다: **가상 시계 + 이벤트 큐 + 시드 PRNG**, 벽시계·스레드·`rand()` 금지.
- 무작위 결정의 **관문은 하나**(`sim_choice`). 그러면 실행은 seed(또는 결정 테이프)의 순수 함수이고 hash로 비트 동일성을 증명한다 (`-O0`/`-O2`/ASan digest 일치).
- 이벤트 순서는 `(t, tie, seq)` **전순서**: tie는 seed로 순서를 흔들고, seq는 큐 구현과 무관하게 순서를 고정한다.
- ISR 선점은 **중첩 호출**로 정확히 모델링된다. `hal_window`가 "여기서 시간이 흐른다"를 표시하고, 그 사이에 더 높은 우선순위 이벤트가 끼어든다.
- 타임아웃 vs RX 완료의 해법: **완료 소유권을 정확히 한 번만**(마스크로 묶은 claim), 응답 ID 비교, 콜백 전에 타이머 취소, 취소 실패는 정상이라 가정.
- 측정: 버그 펌웨어는 20000 seed 중 double 160, stale 4867, hang 2698로 잡히고, 전부 고치면 0건. 가상 89.7초 = 벽시계 33 ms. 실패 trace는 51→13 이벤트로 줄어든다.
- 시뮬은 모델 안의 것만 본다: `window_max=0`이면 double 0건, 단일 코어 모델은 `RTrt`·`TRtr`을 못 만든다. 실제 동시 스트레스(SIV-M09)와 TSan이 나머지를 맡는다.

## 참고자료

- George Marsaglia, *Xorshift RNGs*, Journal of Statistical Software 8(14), 2003.
- C11 표준 6.2.6.1 (padding 바이트 값), 5.1.2.4 (data race 정의).
- Sebastian Burckhardt et al., *A Randomized Scheduler with Probabilistic Guarantees of Finding Bugs*, ASPLOS 2010 (PCT) — 기억 기준, 본문 수치는 토이 측정값.
- Andreas Zeller, Ralf Hildebrandt, *Simplifying and Isolating Failure-Inducing Input*, IEEE TSE 2002 (ddmin).
- FoundationDB 시뮬레이션 테스트 (Will Wilson, Strange Loop 2014) — 발표로 기억, 미확인.
- TigerBeetle VOPR 문서 — 기억 기준, 미확인.
- ARM Cortex-M Generic User Guide: NVIC 우선순위 선점, PRIMASK (`cpsid i`).
- 이 저장소의 관련 레슨: SIV-M09 (동시성 스트레스, 시드 토큰 스케줄러), C-M14 (ISR 공유 변수), C-A03 (원자 연산), RTOS-B10 (deferred ISR), TOOL-M09 (TSan 읽기), QFIN-M07 (비동기 메시지의 같은 "두 완료" 패턴).

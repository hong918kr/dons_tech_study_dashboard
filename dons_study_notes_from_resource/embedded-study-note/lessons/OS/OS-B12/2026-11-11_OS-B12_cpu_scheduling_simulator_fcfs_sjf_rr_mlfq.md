---
id: OS-B12
time: 70분
updated: 2026-11-11
---
# OS-B12 CPU 스케줄링 시뮬레이터: FCFS/SJF/RR/MLFQ

## 학습 목표

- 반환 시간(turnaround)·응답 시간(response)·대기 시간(waiting)을 정의하고, 같은 작업 묶음에서 FCFS·SJF·STCF·RR 이 왜 다른 숫자를 내는지 Gantt 차트로 설명할 수 있다.
- tick 단위 시뮬레이터를 만들고 **동률 규칙(tie-breaking)** 까지 명시해서, OSTEP 공식 시뮬레이터(`scheduler.py`, `mlfq.py`)와 **tick 단위로 같은 결과** 를 낸다 (98 케이스 실측 일치).
- MLFQ 규칙 1~5(우선순위, 같은 레벨 RR, 새 작업 맨 위, 할당량 소진 시 강등, 주기적 부스트)를 구현하고, 각 규칙이 무엇을 막는지 숫자로 보인다.
- 옛 규칙(4a/4b)의 두 약점 — **기아(starvation)** 와 **게이밍(gaming)** — 을 재현하고 부스트·할당량 회계가 각각 무엇을 고치는지(그리고 무엇은 못 고치는지) 말할 수 있다.
- 이 감각을 임베디드 리눅스 보드(UI·센서·OTA·워치독 데몬)와 RTOS 고정 우선순위 스케줄러에 연결한다.

## 1. 문제

"FCFS, SJF, RR 의 장단점은? MLFQ 규칙을 설명하고, 기아는 어떻게 막죠?" — OS 면접의 단골 질문이고, 펌웨어 쪽에서도 그대로 나온다: 임베디드 리눅스 보드에서 UI 이벤트·센서 읽기 같은 **I/O 위주 작업** 과 로그 압축·OTA 이미지 검증 같은 **CPU 위주 작업** 이 한 CPU 를 나눠 쓸 때, 스케줄러 정책이 UI 반응성과 워치독 데몬의 생존을 결정한다. 이 레슨은 그 정책들을 시뮬레이터로 직접 만들어 숫자로 비교한다. (실시간 스케줄링 — 마감 시간 보장, RM/EDF — 은 RTOS 트랙: RTOS-M11, RTOS-M12.)

세 가지 지표 (OSTEP 7.2, 7.6장). 시간은 정수 tick (1 tick = 1 ms 라고 생각해도 된다):

| 지표 | 정의 | 누가 좋아하나 |
|---|---|---|
| 반환 시간 T<sub>turnaround</sub> | 끝난 시각 − 도착 시각 | 배치 작업 (빨리 끝나면 된다) |
| 응답 시간 T<sub>response</sub> | **처음** 실행된 시각 − 도착 시각 | 대화형 작업 (키를 누르면 바로 반응) |
| 대기 시간 T<sub>wait</sub> | 레디 큐에서 기다린 tick 합 | = 반환 − CPU 시간 − I/O 시간 (시간 보존식) |

반환과 응답은 서로 당긴다. SJF/STCF 는 평균 반환을 최소로 만들지만 긴 작업의 응답이 나쁘고, RR 은 응답이 좋지만 반환이 거의 최악이다. MLFQ 는 **작업의 길이를 모르는 채로** 둘 다 잡으려는 시도다 — 과거 행동(CPU 를 slice 끝까지 쓰나, 금방 I/O 로 양보하나)을 보고 우선순위를 바꾼다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">MLFQ = 우선순위 큐 여러 개 + 행동을 보고 우선순위를 바꾸는 규칙 5개</text>
  <text x="150" y="44" fill="currentColor">↓ 새 작업은 Q0 꼬리로 (규칙 3)</text>
  <text x="10" y="67" fill="currentColor">Q0 높음</text><text x="10" y="82" fill="currentColor" font-size="12">slice 10</text>
  <rect x="150" y="52" width="300" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="370" y="57" width="34" height="24" class="f-ok" stroke="currentColor"/><text x="382" y="74" fill="currentColor">C</text>
  <rect x="410" y="57" width="34" height="24" class="f-ok" stroke="currentColor"/><text x="422" y="74" fill="currentColor">D</text>
  <text x="10" y="127" fill="currentColor">Q1</text><text x="10" y="142" fill="currentColor" font-size="12">slice 10</text>
  <rect x="150" y="112" width="300" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="410" y="117" width="34" height="24" class="f-warn" stroke="currentColor"/><text x="422" y="134" fill="currentColor">B</text>
  <text x="10" y="187" fill="currentColor">Q2 낮음</text><text x="10" y="202" fill="currentColor" font-size="12">slice 10</text>
  <rect x="150" y="172" width="300" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="410" y="177" width="34" height="24" class="f-acc" stroke="currentColor"/><text x="422" y="194" fill="currentColor">A</text>
  <path d="M175 88 L175 108" stroke="currentColor" stroke-width="2"/><path d="M170 100 L175 109 L180 100" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="185" y="104" fill="currentColor" font-size="12">규칙 4: 이 레벨 할당량을 다 쓰면 한 칸 아래</text>
  <path d="M175 148 L175 168" stroke="currentColor" stroke-width="2"/><path d="M170 160 L175 169 L180 160" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="185" y="164" fill="currentColor" font-size="12">규칙 4 (맨 아래는 그 자리에서 리필)</text>
  <path d="M150 196 L128 196 L128 64 L144 64" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="5 4"/>
  <path d="M137 59 L146 64 L137 69" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="590" y="98" width="140" height="60" rx="6" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="643" y="124" fill="currentColor" font-weight="bold">CPU</text><text x="616" y="145" fill="currentColor" font-size="12">1 tick 씩 실행</text>
  <path d="M450 69 L588 116" stroke="currentColor"/><path d="M450 129 L588 128" stroke="currentColor"/><path d="M450 189 L588 140" stroke="currentColor"/>
  <text x="560" y="182" fill="currentColor" font-size="12">규칙 1: 높은 큐 먼저 (큐 맨 앞)</text>
  <text x="560" y="198" fill="currentColor" font-size="12">규칙 2: 같은 큐 안은 RR</text>
  <text x="10" y="232" fill="currentColor">점선 = 규칙 5 부스트: S tick 마다 모든 작업을 Q0 로 (기아 방지 + 행동이 바뀐 작업 구제)</text>
  <text x="10" y="254" fill="currentColor">I/O 를 시작하면 큐에서 빠지고, 끝나면 자기 레벨 큐 꼬리로 (레벨·남은 slice·할당량 유지)</text>
  <text x="10" y="276" fill="currentColor">옛 규칙 4a/4b: slice 를 다 쓰면 강등, 그 전에 양보(I/O)하면 그대로 + 리필 → 게이밍 가능 (Part C)</text>
</svg>
```

**작업 모델.** 작업 하나 = `{arrival, length, io_every, io_len}`. `io_every = k > 0` 이면 CPU 를 k tick 쓸 때마다 `io_len` tick 동안 I/O 로 막힌다 (마지막 tick 뒤에는 I/O 없음). OSTEP `mlfq.py` 의 `ioFreq`/`-i` 와 같은 모델이다.

**한 tick 의 순서 (하네스가 제공, 고정).** 동률 규칙이 결과를 바꾸므로 순서를 못 박는다 — `mlfq.py` 와 같은 순서다:

1. (MLFQ) `t > 0 && t % S == 0` 이면 부스트
2. 이번 tick 에 **도착** 한 작업 → 큐 꼬리 (id 순)
3. 이번 tick 에 **I/O 가 끝난** 작업 → 큐 꼬리 (id 순)
4. `pick_next()` 가 고른 작업이 1 tick 실행 (아무도 없으면 idle `.`)
5. 끝났으면 DONE, I/O 차례면 BLOCKED (큐에서 빠짐), 아니면 정책 훅(`rr_tick_end` / `mlfq_tick_end`)

핵심 설계: **실행 중인 작업도 레디 큐 안(보통 맨 앞)에 그대로 있다.** 빼는 건 끝나거나 I/O 로 막힐 때뿐이다. 그래서 FCFS 는 "맨 앞", RR 은 "맨 앞 + slice 끝나면 꼬리로", MLFQ 는 "가장 높은 큐의 맨 앞" 한 줄이 되고, 선점당한 작업은 자기 큐 맨 앞에서 남은 slice 를 이어서 쓴다.

### Part A — FCFS · SJF · STCF · RR 와 지표

- `int pick_next(sim_t *s)` — 이번 tick 에 돌 작업 (없으면 -1).
  - FCFS: 큐 맨 앞. 비선점.
  - SJF: 비선점. 돌던 작업(`s->cur`)이 있으면 계속, 없으면 **다음 CPU 버스트**(`next_burst()`: 다음 I/O 나 종료까지 남은 CPU tick)가 가장 짧은 작업. 동률은 큐 앞쪽.
  - STCF (= PSJF, 선점형 SJF): 매 tick `next_burst` 최소. 돌던 작업이 최소와 **같으면 바꾸지 않는다**.
  - RR: 큐 맨 앞. `rr_tick_end()` 에서 slice 를 다 쓰면 꼬리로 보내고 `cur = -1`.
- `void compute_metrics(sim_t *s)` — 작업별 반환·응답, 세 평균.
- 테스트: OSTEP 그림 7.1~7.9 의 손 계산 값, RR 동률 규칙, I/O 겹치기(overlap), 고정 시드 랜덤 300 케이스 (시간 보존식, 모든 정책의 makespan 동일, **SJF = 순열 전수 최솟값**, **STCF 가 평균 반환 최소**).

### Part B — MLFQ (규칙 1~5)

- `mlfq_pick()` (규칙 1·2), `mlfq_tick_end()` (규칙 4, 옵션 `stay_on_io` 면 옛 규칙 4b), `mlfq_boost()` (규칙 5).
- 레벨별 slice `qlen[]` 와 할당량 `allot[]` (slice 몇 개 — `mlfq.py` 의 `-Q`/`-A` 와 같은 뜻), 부스트 주기 `boost`.
- 테스트: OSTEP 그림 8.2(긴 작업 하나), 8.3(짧은 작업 도착), 규칙 4 vs 옛 4b, 부스트, `mlfq.py` 출력에서 옮겨 적은 값, 그리고 참조 구현 비교 — **레벨 1개 MLFQ = RR**, **slice 무한 MLFQ = FCFS** (tick 단위 gantt 일치, 랜덤 200 케이스).
- 교차 검증 스크립트 `demo_ostep_check.sh`: 내 시뮬레이터와 OSTEP `scheduler.py`/`mlfq.py` 를 같은 입력으로 돌려 tick 단위로 diff.

### Part C — MLFQ 공격과 방어: 기아 · 게이밍

- `cpu_ticks_in(s, j, t0, t1)` — 구간 안에서 j 가 CPU 를 쓴 tick 수 (몫).
- `longest_gap(s, j)` — 처음 실행 ~ 끝 사이에 j 가 연속으로 CPU 를 못 받은 최장 tick (기아 지표. 워치독 데몬이라면 이게 타임아웃보다 길면 보드가 리셋된다).
- 시나리오 1 (기아): 배치 A + t=20 에 도착하는 대화형 B·C (1 tick 실행 → 1 tick I/O). 옛 규칙 / 옛 규칙 + 부스트 50 / 새 규칙 4.
- 시나리오 2 (게이밍): 배치 A vs 게이머 G (9 tick 실행 → 1 tick I/O, slice 10). 옛 규칙 / 새 규칙 / 옛 규칙 + 부스트.
- 결과 숫자를 CHECK 하고, 같은 설정을 `mlfq.py` 로도 돌려 tick 단위 일치를 확인한다.

## 2. Problem (English)

> **CPU scheduling simulator.**
>
> Jobs have an arrival tick, a total CPU length, and an optional I/O pattern (every `io_every` CPU ticks the job blocks for `io_len` ticks). Time advances in integer ticks; within a tick: boost (MLFQ) → arrivals (by id) → I/O completions (by id) → pick one READY job → run it for one tick → finish / block / policy hook.
>
> ```c
> int  simulate(const job_t *jobs, int n, const cfg_t *cfg, sim_t *s);   /* given: the tick engine */
> int  pick_next(sim_t *s);                       /* FCFS, SJF (non-preemptive), STCF (preemptive), RR */
> void rr_tick_end(sim_t *s, int j, bool blocked);
> void compute_metrics(sim_t *s);                  /* turnaround, response, waiting averages */
> int  mlfq_pick(const sim_t *s);                  /* MLFQ rules 1-2 */
> void mlfq_tick_end(sim_t *s, int j, bool blocked);/* rule 4 (allotment), or old rules 4a/4b if stay_on_io */
> void mlfq_boost(sim_t *s);                       /* rule 5 */
> int  cpu_ticks_in(const sim_t *s, int j, int t0, int t1);
> int  longest_gap(const sim_t *s, int j);
> ```
>
> Example: jobs A(0,100), B(10,10), C(10,10). SJF → `A100 B10 C10`, avg turnaround 103.33; STCF → `A10 B10 C10 A90`, avg turnaround 50.
>
> Constraints: deterministic; state every tie-breaking rule; your FCFS/SJF/RR must match OSTEP `scheduler.py -c` and your MLFQ must match `mlfq.py -c` tick for tick.
>
> Follow-ups: Why is SJF optimal for average turnaround when all jobs arrive together? Why does RR minimize response but nearly maximize turnaround? How do you pick the quantum? How can a process game MLFQ and how does the allotment rule fix it? What does the boost period trade off? How does this differ from an RTOS fixed-priority scheduler?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.01 초 안에 끝남):

```text
FAIL starter.c:266  run(f1, 3, C(POL_FCFS, 0)) == 0 && near(S.avg_tat, 20.0)
FAIL starter.c:267  strcmp(rle(&S), "A10 B10 C10") == 0
...
gaming: gamer CPU ticks in [0,300)  old=0  new=0  old+boost100=0
FAIL starter.c:407  g_old == 261 && g_new == 108 && g_old_b == 243
0/45 passed
```

하네스 설계:

- **엔진 `simulate()` 는 하네스가 준다.** 도착·I/O·tick 진행·gantt 기록·대기/I/O tick 계수를 한다. 여러분은 "누구를 고르나" 와 "slice 가 끝나면 어떻게 하나" 만 쓴다. 엔진은 `pick_next()` 가 READY 가 아닌 작업이나 큐에 없는 작업을 고르면 -1 을 돌려준다.
- **멈추지 않는다.** tick 상한 `MAXT = 20000` — 스텁이 계속 -1(idle)을 돌려도 시뮬레이션이 상한에서 -1 로 끝나고 테스트가 FAIL 한다.
- `gantt[t]` 는 tick 별 실행 작업 (`'A'+id`, idle 은 `.`), `glevel[t]` 는 그 tick 의 MLFQ 레벨. 테스트는 `rle()` 로 `"A10 B10 A90"` 처럼 압축해 비교한다.
- `#ifndef SCHED_NO_MAIN` — 데모 파일이 `solution.c` 를 `#include` 해서 같은 엔진을 쓴다.

관찰용 파일 (하네스와 별개, 결정적 — 시간 측정 없음):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep   # 정책 비교표 · RR q 스윕 · 부스트 스윕 · 게이밍 표
cc -std=c11 -Wall -Wextra -Werror -O2 demo_ostep.c -o demo_ostep                    # OSTEP 시뮬레이터와 같은 입력/출력 형식
sh demo_ostep_check.sh                                                               # scheduler.py · mlfq.py 와 tick 단위 비교 (어느 cwd 에서든)
```

`demo_*.c` 는 `#include "solution.c"` 를 하므로 레슨 디렉터리 안의 `solution.c` 가 필요하다 (컴파일 위치는 상관없다 — 따옴표 include 는 데모 파일의 디렉터리부터 찾는다).

## 4. 힌트

<details>
<summary>힌트 1 — "실행 중인 작업도 큐 안에 있다" 를 믿어라</summary>

엔진은 작업이 끝나거나 I/O 로 막힐 때만 큐에서 뺀다. 그러면 FCFS 는 `rq[0][0]` 한 줄이다 — 새 도착은 꼬리로 가니 돌던 작업이 계속 맨 앞이다. RR 은 같은 한 줄 + slice 가 끝나면 `rq_move_to_tail()`. 따로 "running" 변수를 관리하려 들면 동률 규칙이 꼬인다.

</details>

<details>
<summary>힌트 2 — SJF 와 STCF 의 차이는 cur 한 줄</summary>

둘 다 `next_burst()` 최소를 고른다. SJF 는 비선점이라 `s->cur >= 0` (직전 tick 에 돌았고 아직 READY) 이면 그대로 돌려준다. STCF 는 매 tick 다시 고르되, 돌던 작업이 최소와 **동률이면 유지** — 쓸데없는 컨텍스트 스위치를 피하는 흔한 선택이고, 이 레슨의 기대 gantt 가 그 규칙을 따른다.

</details>

<details>
<summary>힌트 3 — MLFQ tick_end: 먼저 slice, 그다음 할당량</summary>

매 tick `ticks_left--`. 0 이 되면(slice 소진) `allot_left--`. 할당량이 남았으면 slice 를 리필하고 같은 레벨 꼬리로(규칙 2), 0 이면 한 칸 아래로(규칙 4) — 맨 아래면 그 자리 리필 + 꼬리. 막힌(`blocked`) 작업은 이미 큐에 없으니 **레벨만** 바꾸고 큐는 건드리지 않는다. `stay_on_io` 면 I/O 순간 slice·할당량을 리필하는데, `mlfq.py -S` 는 "리필 → (slice 가 방금 0 이 됐으면) 차감" 순서다.

</details>

<details>
<summary>힌트 4 — 부스트는 큐 이동 + 상태 리셋 두 가지</summary>

낮은 레벨 큐들의 작업을 Q0 꼬리에 붙이고(어느 레벨부터 붙이느냐가 동률 규칙이다 — `mlfq.py` 는 가장 낮은 레벨부터), **끝나지 않은 모든 작업** (I/O 중인 작업, 아직 도착 안 한 작업 포함) 의 레벨·slice·할당량을 Q0 기준으로 리셋한다. 이미 Q0 에 있던 작업의 slice 도 리필된다.

</details>

<details>
<summary>힌트 5 — 지표: 시간 보존식으로 자기 검사</summary>

반환 = 끝 − 도착, 응답 = 첫 실행 − 도착. 대기는 엔진이 센다. 모든 작업에 대해 `반환 == length + waiting + io_ticks` 가 성립해야 한다 — 하네스의 `conserved()` 가 랜덤 케이스마다 이걸 본다.

</details>

## 5. 풀이

### Part A — 네 정책의 pick

```c
static int pick_next(sim_t *s) {
    if (s->cfg.pol == POL_MLFQ) return mlfq_pick(s);
    if (s->rqn[0] == 0) return -1;
    switch (s->cfg.pol) {
    case POL_FCFS:
    case POL_RR:
        return s->rq[0][0];                                   /* 큐 순서 = 도착/복귀 순서 */
    case POL_SJF:
    case POL_STCF: {
        if (s->cfg.pol == POL_SJF && s->cur >= 0) return s->cur;   /* 비선점 */
        int best = s->rq[0][0];
        for (int i = 1; i < s->rqn[0]; i++)
            if (next_burst(s, s->rq[0][i]) < next_burst(s, best)) best = s->rq[0][i];
        if (s->cur >= 0 && next_burst(s, s->cur) == next_burst(s, best)) best = s->cur;
        return best;
    } ...
```

| 정책 | 선점? | 고르는 기준 | 동률 | 필요한 정보 |
|---|---|---|---|---|
| FCFS | 아니오 | 먼저 큐에 선 작업 | — | 없음 |
| SJF | 아니오 | 다음 버스트 최소 | 큐 앞쪽 | **미래** (오라클) |
| STCF | 매 tick | 다음 버스트 최소 | 돌던 작업 유지, 그다음 큐 앞쪽 | **미래** |
| RR | slice 끝 | 큐 맨 앞, slice 끝나면 꼬리 | slice 끝난 작업이 같은 tick 도착보다 **먼저** 줄 선다 | 없음 |

마지막 줄이 미묘하다. tick t−1 에 slice 가 끝난 작업은 그 tick 끝에 꼬리로 가고, tick t 에 도착한 작업은 t 의 시작에 꼬리로 간다 → 기존 작업이 앞. `mlfq.py` 도 같은 순서라 이렇게 정했다 (테스트: `A(0,4) B(2,2) q=2 → AAAABB`; 반대 규칙이면 `AABBAA`). 교과서마다 이 규칙이 다르므로 면접에서 **"동률 규칙을 이렇게 가정합니다"** 라고 먼저 말하면 좋다.

`next_burst` 를 "남은 전체 CPU" 가 아니라 "다음 I/O 까지" 로 쓴 이유: OSTEP 7.8장처럼 I/O 하는 작업의 각 CPU 버스트를 독립된 작업으로 보기 위해서다. I/O 가 없으면 둘은 같다.

### Part A — 손 계산과 일치 (OSTEP 7장 그림)

| 그림 | 작업 (도착, 길이) | 정책 | gantt (실제) | 평균 반환 | 평균 응답 |
|---|---|---|---|---|---|
| 7.1 | A,B,C (0,10) | FCFS | `A10 B10 C10` | 20 | 10 |
| 7.2 | A(0,100) B,C(0,10) | FCFS | `A100 B10 C10` | 110 | 70 |
| 7.3 | 같음 | SJF | `B10 C10 A100` | 50 | — |
| 7.4 | A(0,100) B,C(10,10) | SJF | `A100 B10 C10` | 103.33 | — |
| 7.5 | 같음 | STCF | `A10 B10 C10 A90` | 50 | 3.33 (0,0,10) |
| 7.6 | A,B,C (0,5) | SJF | `A5 B5 C5` | 10 | 5 |
| 7.7 | 같음 | RR q=1 | `ABCABC…` | 14 | 1 |

7.2 의 FCFS 가 **convoy effect** 다: 100 짜리 하나 뒤에 10 짜리 둘이 줄 서서 평균이 110. 7.6 vs 7.7 이 반환↔응답 트레이드오프: RR 은 응답 1 이지만 반환 14 (SJF 10). RR q=1 의 디스패치 15 번 = 매 tick 스위치.

### Part A — I/O 겹치기 (그림 7.8 / 7.9)

A 는 10 tick 돌고 10 tick I/O (총 CPU 50), B 는 CPU 50. 실제 출력:

```text
FCFS   A10 B50 A10 .10 A10 .10 A10 .10 A10      makespan 130, idle 30
STCF   A10 B10 A10 B10 A10 B10 A10 B20 A10      makespan 100, idle 0
```

STCF 는 A 의 10 tick 버스트가 B 의 남은 시간보다 짧으니 A 가 돌아오자마자 선점 → A 의 I/O 와 B 의 계산이 **겹친다**. t=80 에 A(버스트 10) 와 B(남은 10) 가 동률 → 돌던 B 유지 (`B20`). OSTEP 그림 7.8 의 140 은 "A 의 I/O 동안에도 B 를 안 돌리는" 스케줄이다. 내 FCFS 는 프로세스 단위라 A 가 막히면 B 가 돌지만, 비선점이라 B 가 50 을 다 쓰는 동안 돌아온 A 가 기다리고, 그 뒤 A 의 I/O 동안 CPU 가 논다 — 130. 펌웨어 감각: DMA·I²C 전송을 기다리는 동안 다른 일을 하게 하는 것이 처리량의 핵심이고, 그건 **짧은 버스트 작업을 먼저** 돌려야 생긴다.

### Part A — 임베디드 보드 작업 묶음 W1 비교 (`demo_sweep.c`)

| 작업 | 도착 | CPU | I/O | 역할 |
|---|---|---|---|---|
| A | 0 | 60 | — | 로그 압축 |
| B | 2 | 12 | 2 계산마다 6 | 센서 읽기 (I²C) |
| C | 5 | 8 | 1 계산마다 4 | UI 이벤트 |
| D | 10 | 30 | — | OTA 이미지 검증 |
| E | 12 | 4 | — | CLI 명령 |

실제 출력:

```text
policy             avgTAT  avgResp  avgWait   disp    end  idle
FCFS                96.60    49.80    62.20     17    132    18   resp: A=0 B=58 C=57 D=53 E=81
SJF                 91.00    44.60    56.60     17    133    19   resp: A=0 B=59 C=55 D=58 E=51
STCF                50.40     1.80    16.00     26    114     0   resp: A=0 B=0 C=0 D=8 E=1
RR q=2              70.20     1.80    35.80     61    114     0   resp: A=0 B=2 C=3 D=1 E=3
RR q=8              83.00     7.00    48.60     27    114     0   resp: A=0 B=6 C=5 D=9 E=15
MLFQ 2/4/8 B100     69.60     0.20    35.20     31    114     0   resp: A=0 B=0 C=0 D=0 E=1
```

```svg
<svg viewBox="0 0 760 236" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="8" y="16" fill="currentColor" font-weight="bold">W1 Gantt (실제 시뮬레이션 결과, 1칸 = 1 tick, 빈칸 = CPU 놀음)</text>
  <text x="8" y="47" fill="currentColor">FCFS</text>
  <rect x="86.0" y="30" width="288.0" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="226" y="47" fill="currentColor">A</text><rect x="374.0" y="30" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="383.6" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="388.4" y="30" width="144.0" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="456" y="47" fill="currentColor">D</text><rect x="532.4" y="30" width="19.2" height="26" fill="none" stroke="currentColor" stroke-width="1"/><text x="538" y="47" fill="currentColor">E</text><rect x="551.6" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="556.4" y="30" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="575.6" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="594.8" y="30" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="604.4" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="628.4" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="633.2" y="30" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="652.4" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="671.6" y="30" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="681.2" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="705.2" y="30" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="710.0" y="30" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="722.6" y="47" fill="currentColor" font-size="12">132</text><text x="8" y="85" fill="currentColor">STCF</text>
  <rect x="86.0" y="68" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="95.6" y="68" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="105.2" y="68" width="4.8" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="110.0" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="114.8" y="68" width="19.2" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="120" y="85" fill="currentColor">A</text><rect x="134.0" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="138.8" y="68" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="148.4" y="68" width="9.6" height="26" fill="none" stroke="currentColor" stroke-width="1"/><rect x="158.0" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="162.8" y="68" width="9.6" height="26" fill="none" stroke="currentColor" stroke-width="1"/><rect x="172.4" y="68" width="4.8" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="177.2" y="68" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="186.8" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="191.6" y="68" width="19.2" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="197" y="85" fill="currentColor">D</text><rect x="210.8" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="215.6" y="68" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="225.2" y="68" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="234.8" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="239.6" y="68" width="14.4" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="243" y="85" fill="currentColor">D</text>
  <rect x="254.0" y="68" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="263.6" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="268.4" y="68" width="19.2" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="274" y="85" fill="currentColor">D</text><rect x="287.6" y="68" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="292.4" y="68" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="302.0" y="68" width="76.8" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="336" y="85" fill="currentColor">D</text><rect x="378.8" y="68" width="254.4" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="502" y="85" fill="currentColor">A</text>
  <text x="636.2" y="85" fill="currentColor" font-size="12">114</text><text x="8" y="123" fill="currentColor">RR q=2</text>
  <rect x="86.0" y="106" width="19.2" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="92" y="123" fill="currentColor">A</text><rect x="105.2" y="106" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="114.8" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="124.4" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="129.2" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="138.8" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="148.4" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="158.0" y="106" width="9.6" height="26" fill="none" stroke="currentColor" stroke-width="1"/><rect x="167.6" y="106" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="177.2" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="186.8" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="191.6" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="201.2" y="106" width="9.6" height="26" fill="none" stroke="currentColor" stroke-width="1"/><rect x="210.8" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="220.4" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="230.0" y="106" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="239.6" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="244.4" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="254.0" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="263.6" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="273.2" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="282.8" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="287.6" y="106" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="297.2" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="306.8" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="316.4" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="326.0" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="330.8" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="340.4" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="350.0" y="106" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="359.6" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="369.2" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="378.8" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="383.6" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="393.2" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="402.8" y="106" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="412.4" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="422.0" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="431.6" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <rect x="436.4" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="446.0" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="455.6" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="465.2" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="474.8" y="106" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="479.6" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="489.2" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="498.8" y="106" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="508.4" y="106" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="518.0" y="106" width="115.2" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="572" y="123" fill="currentColor">A</text>
  <text x="636.2" y="123" fill="currentColor" font-size="12">114</text><text x="8" y="161" fill="currentColor">MLFQ 2/4/8</text>
  <rect x="86.0" y="144" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="95.6" y="144" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="105.2" y="144" width="4.8" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="110.0" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="114.8" y="144" width="19.2" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="120" y="161" fill="currentColor">A</text><rect x="134.0" y="144" width="9.6" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="143.6" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="148.4" y="144" width="9.6" height="26" fill="none" stroke="currentColor" stroke-width="1"/><rect x="158.0" y="144" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="167.6" y="144" width="19.2" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="173" y="161" fill="currentColor">D</text><rect x="186.8" y="144" width="9.6" height="26" fill="none" stroke="currentColor" stroke-width="1"/><rect x="196.4" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="201.2" y="144" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="210.8" y="144" width="9.6" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="220.4" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="225.2" y="144" width="19.2" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="231" y="161" fill="currentColor">A</text><rect x="244.4" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="249.2" y="144" width="4.8" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="254.0" y="144" width="14.4" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="257" y="161" fill="currentColor">D</text><rect x="268.4" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="273.2" y="144" width="24.0" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="281" y="161" fill="currentColor">D</text><rect x="297.2" y="144" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="306.8" y="144" width="38.4" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="322" y="161" fill="currentColor">A</text><rect x="345.2" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="350.0" y="144" width="38.4" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="365" y="161" fill="currentColor">D</text><rect x="388.4" y="144" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="398.0" y="144" width="38.4" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="413" y="161" fill="currentColor">A</text><rect x="436.4" y="144" width="4.8" height="26" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="441.2" y="144" width="38.4" height="26" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="456" y="161" fill="currentColor">D</text><rect x="479.6" y="144" width="9.6" height="26" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="489.2" y="144" width="144.0" height="26" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="557" y="161" fill="currentColor">A</text>
  <text x="636.2" y="161" fill="currentColor" font-size="12">114</text><line x1="86" y1="176" x2="86" y2="182" stroke="currentColor"/><text x="80" y="194" fill="currentColor">0</text><line x1="182" y1="176" x2="182" y2="182" stroke="currentColor"/><text x="176" y="194" fill="currentColor">20</text><line x1="278" y1="176" x2="278" y2="182" stroke="currentColor"/><text x="272" y="194" fill="currentColor">40</text>
  <line x1="374" y1="176" x2="374" y2="182" stroke="currentColor"/><text x="368" y="194" fill="currentColor">60</text><line x1="470" y1="176" x2="470" y2="182" stroke="currentColor"/><text x="464" y="194" fill="currentColor">80</text><line x1="566" y1="176" x2="566" y2="182" stroke="currentColor"/><text x="560" y="194" fill="currentColor">100</text><line x1="662" y1="176" x2="662" y2="182" stroke="currentColor"/>
  <text x="656" y="194" fill="currentColor">120</text><rect x="8" y="201" width="14" height="14" class="f-mut" stroke="currentColor" stroke-width="0.5"/><text x="26" y="212" fill="currentColor">A 로그 압축 60</text><rect x="156" y="201" width="14" height="14" class="f-ok" stroke="currentColor" stroke-width="0.5"/><text x="174" y="212" fill="currentColor">B 센서 2+I/O6</text>
  <rect x="304" y="201" width="14" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/><text x="322" y="212" fill="currentColor">C UI 1+I/O4</text><rect x="452" y="201" width="14" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="470" y="212" fill="currentColor">D OTA 검증 30</text><rect x="600" y="201" width="14" height="14" fill="none" stroke="currentColor"/>
  <text x="618" y="212" fill="currentColor">E CLI 4</text>
</svg>
```

읽는 법:

- **FCFS/SJF**: A(60) 가 먼저 와서 비선점으로 다 돈다 — UI(C) 첫 응답이 57 tick. 게다가 끝부분에 B·C 의 I/O 만 남아 CPU 가 18~19 tick 논다. SJF 도 A 가 이미 돌고 있으니 소용없다(비선점).
- **STCF**: 평균 반환 최소(50.4), idle 0. 하지만 미래(버스트 길이)를 알아야 한다. 그리고 긴 A 는 맨 끝(`A53`) — 짧은 작업이 계속 오면 A 는 굶는다 (SJF/STCF 의 기아, 연습 2).
- **RR**: 응답은 STCF 수준(1.8)인데 반환이 나쁘고 디스패치가 61 번.
- **MLFQ** (slice 2/4/8, 부스트 100): 길이를 모르고도 응답 0.2 — 새 작업은 맨 위로 오니 거의 바로 돈다. 반환은 RR 과 비슷.

RR quantum 스윕 (같은 W1, 실제 값):

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="8" y="16" fill="currentColor" font-weight="bold">RR quantum 스윕 (W1, 실제 값): q 를 키우면 응답↑ 반환↑ 스위치↓ → q=64 는 FCFS 와 같다</text>
  <line x1="70" y1="200" x2="520" y2="200" stroke="currentColor"/><line x1="70" y1="200" x2="70" y2="40" stroke="currentColor"/><text x="42" y="204" fill="currentColor">0</text><text x="42" y="164" fill="currentColor">25</text><text x="42" y="124" fill="currentColor">50</text><text x="42" y="84" fill="currentColor">75</text><text x="42" y="44" fill="currentColor">100</text>
  <text x="64" y="216" fill="currentColor">1</text><text x="139" y="216" fill="currentColor">2</text><text x="214" y="216" fill="currentColor">4</text><text x="289" y="216" fill="currentColor">8</text><text x="364" y="216" fill="currentColor">16</text><text x="439" y="216" fill="currentColor">32</text><text x="514" y="216" fill="currentColor">64</text>
  <text x="265" y="234" fill="currentColor">quantum (tick, log2)</text><polyline points="70,92 145,88 220,75 295,67 370,62 445,52 520,45" fill="none" stroke="currentColor" stroke-width="2"/><polyline points="70,198 145,197 220,194 295,189 370,181 445,156 520,120" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="5 4"/><circle cx="70" cy="92" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="145" cy="88" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="220" cy="75" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="295" cy="67" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="370" cy="62" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="445" cy="52" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="520" cy="45" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="70" cy="198" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="145" cy="197" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="220" cy="194" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="295" cy="189" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="370" cy="181" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="445" cy="156" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="520" cy="120" r="3.5" class="f-ok" stroke="currentColor"/><line x1="545" y1="60" x2="575" y2="60" stroke="currentColor" stroke-width="2"/><text x="581" y="64" fill="currentColor">평균 반환 (67.8 → 96.6)</text><line x1="545" y1="84" x2="575" y2="84" stroke="currentColor" stroke-width="2" stroke-dasharray="5 4"/><text x="581" y="88" fill="currentColor">평균 응답 (1.2 → 49.8)</text>
  <text x="545" y="120" fill="currentColor">디스패치 수</text><text x="545" y="138" fill="currentColor" font-family="monospace">q=1: 114 · q=2: 61</text><text x="545" y="156" fill="currentColor" font-family="monospace">q=8: 27 · q=64: 17</text><text x="545" y="184" fill="currentColor">STCF: 반환 50.4 / 응답 1.8</text><text x="545" y="202" fill="currentColor">MLFQ: 반환 69.6 / 응답 0.2</text>
</svg>
```

q 가 커지면 디스패치(≈ 컨텍스트 스위치)가 줄지만 응답이 나빠지고, 모든 작업보다 길어지면(q=64) FCFS 와 **완전히 같다** (평균 반환 96.6, 응답 49.8, 디스패치 17). q 가 너무 작으면 스위치 비용이 CPU 를 먹는다 — 이 시뮬레이터는 스위치 비용을 0 으로 뒀다. 비용을 넣은 버전이 연습 1 (q=1, 비용 1 tick 이면 효율 50%). 실제 스위치 비용 측정은 OS-M02.

### Part A — 브루트포스·랜덤 검증

고정 시드 xorshift32 로 300 케이스 (작업 2~6 개, 길이 1~15, 절반은 모두 0 에 도착):

- **시간 보존**: 모든 정책·모든 작업에서 반환 = CPU + 대기 + I/O (I/O 섞인 케이스 포함).
- **작업 보존(work-conserving)**: I/O 가 없으면 네 정책의 makespan 이 같다 — 누구도 일이 있는데 놀지 않는다.
- **SJF 최적성**: 모두 0 에 도착하면 SJF 평균 반환 = 실행 순서 n! 개 전수 중 최솟값. (교환 논증: 인접한 긴 것-짧은 것을 바꾸면 합이 줄어든다.)
- **STCF(SRPT) 최적성**: 도착이 제각각이어도 I/O 가 없으면 STCF 평균 반환 ≤ FCFS/SJF/RR. 선점 단일 CPU 에서 남은 시간 최소 우선(SRPT)이 평균 반환을 최소화한다는 고전 결과(Schrage 1968)와 일치한다 — 300/300.

### Part B — MLFQ 구현

```c
static void mlfq_tick_end(sim_t *s, int j, bool blocked) {
    const cfg_t *c = &s->cfg;
    int lv = s->level[j];
    bool expired = (--s->ticks_left[j] == 0);
    if (blocked && c->stay_on_io) {                 /* 옛 규칙 4b: I/O 하면 그 레벨 예산 리필 */
        s->ticks_left[j] = c->qlen[lv];
        s->allot_left[j] = c->allot[lv];
    }
    if (!expired) return;
    if (--s->allot_left[j] > 0) {                   /* 할당량 남음: 같은 레벨 RR (규칙 2) */
        s->ticks_left[j] = c->qlen[lv];
        if (!blocked) rq_move_to_tail(s, lv, j);
        return;
    }
    int nlv = lv < c->nlevels - 1 ? lv + 1 : lv;    /* 규칙 4: 강등 (맨 아래는 리필) */
    s->level[j] = nlv; s->ticks_left[j] = c->qlen[nlv]; s->allot_left[j] = c->allot[nlv];
    if (!blocked) { rq_remove(s, lv, j); rq_push(s, nlv, j); }
}
```

| 규칙 (OSTEP 8.6) | 코드 위치 | 테스트 |
|---|---|---|
| 1 높은 우선순위 먼저 | `mlfq_pick`: 가장 높은 비어 있지 않은 레벨 | 그림 8.3: t=100 도착한 B 가 바로 선점, 응답 0 |
| 2 같은 우선순위는 RR | 할당량 남으면 꼬리로 | 레벨 1개 MLFQ = RR (랜덤 200 케이스 gantt 일치) |
| 3 새 작업은 맨 위 | 엔진: `level = 0` 으로 도착 | 그림 8.3 |
| 4 할당량 소진 → 강등 | `ticks_left`/`allot_left` 는 I/O 해도 유지 | 1 tick 실행 + I/O 작업도 11 번째 실행은 레벨 1 |
| 5 S 마다 부스트 | `mlfq_boost` | 긴 작업이 t=49 레벨 2 → t=50 레벨 0 → t=60 레벨 1 |

**선점된 작업의 slice 는 유지된다.** 레벨 1 의 작업이 slice 를 4 tick 쓰다가 레벨 0 작업에 선점되면, 자기 큐 맨 앞에서 기다렸다가 남은 6 tick 을 이어서 쓴다. 이게 "실행 중인 작업도 큐에 있다" 설계의 덕이다.

### Part B — OSTEP `mlfq.py` / `scheduler.py` 와 tick 단위 교차 검증

`demo_ostep.c` 는 OSTEP 시뮬레이터와 같은 인자를 받아 같은 형식으로 출력한다 (`mlfq.py` 는 우선순위 번호가 거꾸로 — 가장 높은 큐가 N−1 — 라서 출력 때 `N-1-level` 로 바꾼다). 예: `scheduler.py -p RR -q 3 -s 4 -j 5 -c` (작업 길이 3,2,4,2,1):

```text
$ python3 -I .../cpu-sched/scheduler.py -p RR -q 3 -s 4 -j 5 -c     (Final statistics 부분)
  Job   0 -- Response: 0.00  Turnaround 3.00  Wait 0.00
  Job   1 -- Response: 3.00  Turnaround 5.00  Wait 3.00
  Job   2 -- Response: 5.00  Turnaround 12.00  Wait 8.00
  Job   3 -- Response: 8.00  Turnaround 10.00  Wait 8.00
  Job   4 -- Response: 10.00  Turnaround 11.00  Wait 10.00
  Average -- Response: 5.20  Turnaround 8.20  Wait 5.80
$ ./demo_ostep sched RR 3 3,2,4,2,1
(위와 같은 다섯 줄 + Average -- Response: 5.20  Turnaround 8.20  Wait 5.80)
```

MLFQ 예 (Part B 테스트의 작업 목록: `mlfq.py -n 3 -q 10 -i 5 -B 100 -l 0,120,0:30,40,7:50,25,3 -c`), 부스트 직전·직후:

```text
mlfq.py                                                      demo_ostep mlfq (같은 인자)
[ time 98 ] IO_DONE by JOB 2
[ time 98 ] Run JOB 2 at PRIORITY 1 [ TICKS 1 ALLOT 1 ...]   [ time 98 ] Run JOB 2 at PRIORITY 1
[ time 99 ] Run JOB 2 at PRIORITY 1 [ TICKS 0 ALLOT 1 ...]   [ time 99 ] Run JOB 2 at PRIORITY 1
[ time 100 ] BOOST ( every 100 )
[ time 100 ] Run JOB 0 at PRIORITY 2 [ TICKS 9 ALLOT 1 ...]  [ time 100 ] Run JOB 0 at PRIORITY 2
Final statistics:
  Job  0: startTime   0 - response   0 - turnaround 185      (세 줄 모두 같음)
  Job  1: startTime  30 - response   0 - turnaround 107
  Job  2: startTime  50 - response   0 - turnaround  77
```

t=99 에 JOB 2 가 레벨 1 의 slice 를 다 써서 맨 아래(JOB 0 뒤)로 강등되고, t=100 부스트에서 **가장 낮은 큐부터** Q0 로 옮기므로 [JOB 0, JOB 2] 순서 → JOB 0 이 먼저 돈다. 이 순서를 반대로(높은 레벨부터) 하면 어떻게 되는지는 아래 변이 테스트.

전체 비교 (`sh demo_ostep_check.sh`, 실제 출력):

```text
scheduler.py: 40 match, 0 differ
mlfq.py:      58 match, 0 differ
ALL MATCH (98 cases)
```

- `scheduler.py`: FIFO / SJF / RR q=1 / RR q=3 × seed 0~9, 작업 5개 — 작업별 Response/Turnaround/Wait 와 평균 줄.
- `mlfq.py`: 랜덤 작업 seed 0~9 × 설정 5개 (기본, 부스트 50, `-Q 10,20,40 -A 2,2,1 -B 100`, `-S -a 2`, `-n 2 -q 5 -a 3 -i 3 -j 4`) + 레슨의 고정 목록 8개 — **매 tick 의 (작업, 우선순위)** 와 Final statistics.

**변이 테스트 (교차 검증이 정말 무언가를 잡나?)** — `solution.c` 를 한 줄씩 바꿔 스크립트를 다시 돌렸다 (실제 결과):

| 변이 | 결과 |
|---|---|
| 부스트 때 높은 레벨부터 옮기기 | mlfq.py 3 케이스 differ |
| I/O 하면 slice 만 리필 (할당량은 유지) | mlfq.py 43 케이스 differ |
| `-S` 에서 "slice 가 방금 0 이면 리필 안 함" | 처음엔 57/57 일치 — **못 잡았다**. 그 모서리(slice 끝 tick 에 정확히 I/O, 할당량 2)를 노리는 고정 케이스를 추가한 뒤 1 differ |

마지막 줄이 교훈이다: 랜덤 교차 검증도 드문 모서리는 놓친다. 의심 가는 분기는 그 분기를 반드시 지나는 케이스를 손으로 만들어 넣는다.

차이가 나는 곳 (정직하게): `mlfq.py` 의 `-I` (iobump: I/O 끝난 작업을 큐 **맨 앞** 에) 는 구현하지 않았다. `scheduler.py` 는 도착 시각·I/O 를 지원하지 않아 STCF·도착·I/O 는 `mlfq.py`(한 레벨=RR, slice 무한=FCFS)와 손 계산으로만 검증했다. `mlfq.py` 의 I/O 길이는 전 작업 공통(`-i`)이라 작업별 `io_len` 이 다른 경우는 교차 검증 범위 밖이다.

### Part C — 기아: 부스트가 고치는 것과 못 고치는 것

배치 A(0, 200) + 대화형 B·C(20, 100, 1 tick 실행 → 1 tick I/O), 3 레벨 slice 10 할당량 1:

```svg
<svg viewBox="0 0 760 190" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="8" y="16" fill="currentColor" font-weight="bold">기아 시나리오 0~250 tick: 배치 A(강조색) vs 대화형 B·C(회색) — 실제 결과</text>
  <text x="8" y="45" fill="currentColor">옛 4a/4b</text><rect x="96" y="30" width="650" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="96.0" y="30" width="52.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="668.0" y="30" width="78.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="8" y="81" fill="currentColor">옛 + 부스트50</text>
  <rect x="96" y="66" width="650" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="96.0" y="66" width="52.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="226.0" y="66" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="356.0" y="66" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="486.0" y="66" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="616.0" y="66" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="8" y="117" fill="currentColor">새 규칙 4</text>
  <rect x="96" y="102" width="650" height="22" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="96.0" y="102" width="52.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="252.0" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="283.2" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="314.4" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="345.6" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="376.8" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="408.0" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="439.2" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="470.4" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="501.6" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="532.8" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="564.0" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="595.2" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="626.4" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="657.6" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="688.8" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="720.0" y="102" width="26.0" height="22" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <line x1="96" y1="130" x2="96" y2="136" stroke="currentColor"/><text x="88" y="148" fill="currentColor">0</text><line x1="226" y1="130" x2="226" y2="136" stroke="currentColor"/><text x="218" y="148" fill="currentColor">50</text><line x1="356" y1="130" x2="356" y2="136" stroke="currentColor"/><text x="348" y="148" fill="currentColor">100</text><line x1="486" y1="130" x2="486" y2="136" stroke="currentColor"/>
  <text x="478" y="148" fill="currentColor">150</text><line x1="616" y1="130" x2="616" y2="136" stroke="currentColor"/><text x="608" y="148" fill="currentColor">200</text><line x1="746" y1="130" x2="746" y2="136" stroke="currentColor"/><text x="738" y="148" fill="currentColor">250</text><text x="8" y="170" fill="currentColor">A 가 [20,220) 에서 받은 tick: 옛 0 · 옛+부스트50 40 · 새 규칙 134 (최장 공백 200 · 40 · 40)</text>
</svg>
```

부스트 주기 S 스윕 (옛 규칙, `demo_sweep.c` 실제 출력):

```text
 boost S A ticks[20,220)    A gap    A TAT   B,C resp
       0              0      200      400      0,1
     200             10      180      400      0,1
     100             20       90      400      0,1
      50             40       40      400      0,1
      25             80       15      400      0,1
      10            180        2      220     10,11
     new            134       40      274      0,1   <- new rule 4, no boost
```

- **옛 규칙 + 부스트 없음**: B·C 는 1 tick 쓰고 I/O 로 양보하니 영원히 Q0. 둘이 번갈아 CPU 를 100% 채워서 A 는 200 tick 동안 0 tick. **최장 공백 200**.
- **부스트 S**: S 마다 A 가 Q0 꼬리로 올라와 slice 10 을 받는다 → A 의 몫 ≈ 10/S, 최장 공백 ≈ S − 10. S=50 이면 40.
- **A 의 반환은 400 그대로**(S ≥ 25). CPU 는 늘 바쁘고 A 가 마지막에 끝나므로 총량은 같다. 부스트가 바꾸는 건 **진척의 분포**(공백 길이)지 끝나는 시각이 아니다. 워치독 데몬이라면 바로 그 공백이 중요하다.
- **S=10 (= slice)** 은 병적: 매 10 tick 모두 리셋되니 사실상 전부 같은 레벨의 RR — A 가 [20,220) 에서 180 tick 을 먹고 B·C 의 응답이 10, 11 로 나빠진다. OSTEP 의 말대로 S 는 "voo-doo constant": 너무 길면 기아, 너무 짧으면 대화형 작업이 손해.
- **새 규칙 4 (부스트 없음)**: B·C 도 Q0 에서 10 tick 을 쓰면 강등된다 → 결국 A 와 같은 맨 아래 레벨에서 RR (`A10 B1 C1 A10 …`). A 는 134 tick 을 받지만, 이번엔 **B·C 가 매번 A 의 slice 10 을 기다린다** — 대화형 지연이 나빠진다. 행동이 다시 대화형으로 바뀐 작업을 위로 올려 주는 것이 부스트의 두 번째 역할이다.

### Part C — 게이밍: 할당량 회계가 고치는 것

배치 A(0, 300) vs 게이머 G(0, 300, 9 tick 실행 → 1 tick I/O). slice 10 바로 직전에 I/O 를 내서 옛 규칙 4b("slice 전에 양보하면 그대로")를 악용한다. 구간 [0, 300) 의 몫 (실제 출력):

```text
old 4a/4b        G=261 (87.0%)  A= 39 (13.0%)
old + boost 100  G=243 (81.0%)  A= 57 (19.0%)
new rule 4       G=108 (36.0%)  A=192 (64.0%)
new + boost 100  G=120 (40.0%)  A=180 (60.0%)
```

- 옛 규칙: G 가 87% 를 가져간다 (`A10 B9 A1 B9 A1 …` — 이 출력에서 G 는 B 열). **부스트는 게이밍을 못 고친다** (81%) — 부스트 직후 잠깐 A 가 slice 를 받을 뿐 G 는 다시 Q0 에 눌러앉는다.
- 새 규칙 4: I/O 를 몇 번 하든 레벨에서 쓴 CPU 총량이 할당량(10)을 넘으면 강등 → G 도 맨 아래에서 A 와 RR. G 가 50% 가 아니라 36% 인 것은 G 가 I/O 로 빠질 때마다 남은 slice 조각(1 tick)만 들고 돌아오기 때문이다 (`A10 B1 A9 …` 같은 조각). 공정성의 기준은 "G 가 부당하게 많이 못 가져간다" 이고, 그건 달성됐다.

### Part C — 펌웨어/임베디드 리눅스로 옮기면

| 상황 | 이 레슨의 어느 부분 |
|---|---|
| 리눅스 기본 스케줄러 (CFS, 6.6 부터 EEVDF) | MLFQ 가 아니라 가중 공정 분배 + 지연 목표. 개념 구현은 OS-A04. 실제 지연 측정은 LPERF-M05 (runqlat) |
| `SCHED_FIFO` / `SCHED_RR` 실시간 정책 | 고정 우선순위 + 같은 우선순위 안 FIFO/RR = 이 레슨의 FCFS·RR 을 우선순위 레벨마다 둔 것. `sched_rr_get_interval()` 로 slice 를 읽는다. PREEMPT_RT 지연은 ELX-A04 |
| RTOS (FreeRTOS 등) 고정 우선순위 선점 | MLFQ 의 규칙 1·2 만 있고 3~5 는 없다: 우선순위는 **설계자가 정적으로** 준다. 그래서 높은 우선순위 태스크가 바쁘게 돌면 낮은 태스크는 **설계상** 굶는다 (부스트 없음). 대신 RM 분석으로 마감을 증명한다 (RTOS-B04 ready list, RTOS-M11, RTOS-M12) |
| 워치독 kick 을 낮은 우선순위 태스크/데몬에 둠 | Part C 의 `longest_gap` 이 워치독 타임아웃을 넘으면 리셋. 그래서 kick 은 "모든 태스크가 살아 있음" 을 모아서 확인하는 태스크가 하고, 타임아웃은 최장 공백 분석으로 정한다 (BM-M10, ELX-B13 systemd `WatchdogSec=`) |
| 센서 I/O 와 계산 겹치기 | Part A I/O 겹치기: 짧은 버스트 작업을 먼저 깨워야 DMA/I²C 대기와 계산이 겹친다 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

```text
starvation: batch ticks in [20,220) / longest gap  old=0/200  old+boost50=40/40  new=134/40
gaming: gamer CPU ticks in [0,300)  old=261  new=108  old+boost100=243
45/45 passed
```

시뮬레이션이 결정적이라 매 실행 같은 출력이다 (6 회 반복 모두 `45/45 passed`, 0.01 초). 새니타이저도 깨끗하다 (경고 0, 종료 코드 0):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 동률 규칙을 정하지 않음 | RR 에서 "slice 끝난 작업 vs 같은 tick 도착" 순서만 바꿔도 gantt 가 `AAAABB` ↔ `AABBAA`. 참조 시뮬레이터와 안 맞는 대부분의 원인 | tick 안의 이벤트 순서를 먼저 고정하고 문서화 |
| 응답 시간을 "마지막으로 기다린 시간" 으로 | 응답은 **처음** 실행까지. 여러 번 선점돼도 한 번만 잰다 | `if (first_run[j] < 0) first_run[j] = t;` |
| 평균을 정수 나눗셈으로 | 310/3 = 103 이 된다 | `(double)sum / n` |
| SJF 를 매 tick 다시 고름 | 그건 STCF(선점). SJF 는 막히거나 끝날 때까지 계속 | `if (pol == SJF && cur >= 0) return cur;` |
| 선점된 작업을 큐 꼬리로 보냄 | MLFQ 에서 높은 레벨 작업에 선점당했을 뿐인데 같은 레벨 순서를 잃는다 (`mlfq.py` 와 불일치) | 작업은 끝나거나 막힐 때만 큐에서 뺀다 |
| I/O 할 때 slice·할당량을 리필 | 옛 규칙 4b 그대로 → 게이머가 87% 독점 | 새 규칙 4: I/O 와 무관하게 누적 (`stay_on_io = false`) |
| 막힌 작업을 강등하며 큐에도 넣음 | 같은 작업이 큐에 둘 — 엔진이 I/O 끝날 때 또 넣는다 | `if (!blocked) { rq_remove; rq_push; }` 레벨만 변경 |
| 부스트에서 I/O 중·도착 전 작업은 리셋 안 함 | 돌아왔을 때 낮은 레벨로 복귀 → 부스트 효과 반감 (`mlfq.py` 는 끝나지 않은 모든 작업을 리셋) | `for all j: if (state != DONE) reset` |
| 부스트 주기를 slice 와 비슷하게 | 매번 리셋 → 사실상 전체 RR, 대화형 응답 악화 (S=10: 10, 11 tick) | S 는 slice 의 수~수십 배 (OSTEP: Solaris 기본 약 1 초) |
| 시뮬레이터 루프에 상한 없음 | 정책 버그(아무도 안 고름)면 무한 루프 | tick 상한 `MAXT` → -1 |

## 8. 면접 Q&A

<details>
<summary>Q1. 반환 시간과 응답 시간은 왜 서로 당기나요? 한 정책으로 둘 다 최적화할 수 있나요?</summary>

반환 최소화는 "짧은 것부터 끝까지" (SJF/STCF) 이고 응답 최소화는 "모두를 조금씩 빨리" (RR) 다. RR 은 모든 작업을 잘게 섞으니 모든 작업이 늦게 끝나고, SJF 는 긴 작업을 맨 뒤로 미루니 긴 작업의 응답이 끝없이 길어질 수 있다. W1 실측: STCF 반환 50.4/응답 1.8, RR q=2 반환 70.2/응답 1.8, FCFS 96.6/49.8. STCF 가 둘 다 좋아 보이지만 **미래를 알아야** 한다 — 현실의 스케줄러는 모른다. MLFQ 는 과거 행동으로 미래를 추정해서 "짧은/대화형 작업 우선" 을 근사한다 (W1 응답 0.2, 반환 69.6). 꼬리: "공정성은?" → 세 번째 축. 반환·응답 최적 정책은 대개 불공정하고(SJF 기아), 공정 정책(stride, CFS)은 반환이 최적이 아니다.

</details>

<details>
<summary>Q2. SJF 가 평균 반환 시간 최적이라는 걸 증명해 보세요. 언제 깨지나요?</summary>

모두 0 에 도착하면 반환 합 = Σ (i 번째 작업의 끝 시각) = Σ<sub>i</sub> (n−i+1)·len<sub>i</sub> — 앞에 놓인 작업의 길이가 뒤의 모든 작업에 더해진다. 그러니 긴 작업이 짧은 작업 앞에 있으면 둘을 바꿔 합을 (긴−짧은) 만큼 줄일 수 있다(교환 논증) → 오름차순이 최적. 이 레슨은 n ≤ 6 에서 n! 순열 전수와 비교해 150 케이스(모두 0 도착인 절반) 모두 일치했다. 깨지는 경우: ① 도착 시각이 다르면 비선점 SJF 는 최적이 아니다 (그림 7.4: 103.33 vs STCF 50). I/O 가 없으면 선점을 허용한 STCF(SRPT)가 최적이 된다 — 랜덤 300 케이스 모두 STCF ≤ 나머지. ② I/O 가 섞이면 버스트 단위로 봐야 하고, ③ 길이를 모르면 아예 구현할 수 없다 (예측: 연습 4).

</details>

<details>
<summary>Q3. RR 의 quantum 은 어떻게 정하나요?</summary>

트레이드오프 두 개. ① 응답 vs 스위치 비용: q 가 작으면 응답이 좋지만 스위치 비율이 커진다 — 스위치 비용 c, slice q 면 CPU 효율 ≈ q/(q+c) (연습 1: q=1, c=1 → 50%). 비용에는 레지스터 저장뿐 아니라 캐시·TLB·분기 예측기 상태를 잃는 것이 포함된다 (OSTEP 7.7, 측정은 OS-M02). ② q 가 모든 버스트보다 길면 RR = FCFS (W1 q=64 실측: 숫자가 FCFS 와 완전히 같다). 경험칙: 대부분의 CPU 버스트가 한 slice 안에 끝날 정도(대화형 작업은 I/O 로 먼저 양보) + 스위치 비용보다 충분히 길게. 리눅스 `SCHED_RR` 기본 slice 는 100 ms (`/proc/sys/kernel/sched_rr_timeslice_ms`). 꼬리: "MLFQ 는?" → 높은 레벨은 짧게(대화형), 낮은 레벨은 길게(배치, 스위치 줄이기) — OSTEP 그림 8.7 의 10/20/40.

</details>

<details>
<summary>Q4. MLFQ 규칙 1~5 를 말하고, 각 규칙이 없으면 무슨 일이 생기는지 설명해 보세요.</summary>

1 높은 우선순위 먼저, 2 같으면 RR, 3 새 작업은 맨 위, 4 레벨 할당량을 다 쓰면 강등(I/O 횟수 무관), 5 S 마다 전부 맨 위로. 없으면: 3 이 없으면 새 짧은 작업이 긴 작업 뒤에 줄 선다(응답 악화 — 그림 8.3 은 B 응답 0). 4 가 "옛 4a/4b" 면 게이밍(실측 87% 독점). 5 가 없으면 대화형 작업이 많을 때 배치 기아(실측 200 tick 동안 0) + 행동이 바뀐 작업(배치였다가 대화형이 됨)이 아래에 갇힌다. 규칙 3 의 의도는 "모르면 일단 짧은 작업이라고 가정" — SJF 의 근사다. 꼬리: "부스트와 새 규칙 4 는 서로 대체되나?" → 아니다. 실측: 부스트는 게이밍을 못 막고(81%), 새 규칙 4 는 기아는 줄이지만 대화형 작업을 맨 아래로 끌어내려 응답이 나빠진다. 둘 다 필요하다.

</details>

<details>
<summary>Q5. 시뮬레이터가 맞다는 건 어떻게 보장했나요?</summary>

네 겹. ① 손 계산 값(OSTEP 7·8장 그림)을 gantt 문자열까지 비교. ② 불변식: 시간 보존(반환 = CPU + 대기 + I/O), 작업 보존(I/O 없으면 makespan 동일). ③ 참조 구현: SJF = 순열 전수 최솟값, 레벨 1개 MLFQ = RR, slice 무한 MLFQ = FCFS, STCF ≤ 모든 정책. ④ 독립 구현과 차분 테스트: OSTEP `scheduler.py`/`mlfq.py` 와 98 케이스 tick 단위 일치. 그리고 변이 테스트로 ④ 가 실제로 버그를 잡는지 확인했는데, 한 변이(`-S` 모서리)는 랜덤으로 못 잡아 표적 케이스를 추가했다. 면접 포인트: "참조와 일치" 는 동률 규칙까지 맞췄을 때만 의미가 있고, 차분 테스트도 커버리지를 따로 확인해야 한다.

</details>

<details>
<summary>Q6. RTOS 의 스케줄러와 MLFQ 는 뭐가 다른가요? 펌웨어에서 기아는 어떻게 다루나요?</summary>

RTOS(FreeRTOS, Zephyr 등)는 **고정 우선순위 선점** 이 기본이다 — MLFQ 의 규칙 1·2(같은 우선순위는 tick 마다 RR, FreeRTOS `configUSE_TIME_SLICING`)만 있고, 우선순위를 행동으로 바꾸는 3~5 가 없다. 목표가 평균 반환·응답이 아니라 **마감 보장** 이기 때문이다: 우선순위는 주기에 따라 설계자가 정하고(Rate Monotonic), 응답 시간 분석으로 최악의 경우를 증명한다 (RTOS-M11). 그래서 기아는 버그가 아니라 설계 결과다 — 높은 태스크가 예산을 넘게 돌면 낮은 태스크는 굶는다. 다루는 법: ① 각 태스크의 최악 실행 시간·주기 예산을 두고 넘으면 감지, ② 워치독은 모든 태스크의 "살아 있음" 플래그를 모아 확인한 뒤 kick (가장 낮은 태스크가 굶어도 리셋되게), ③ 우선순위 역전은 상속/천장 프로토콜 (RTOS-M03~M05). 꼬리: "리눅스에서 SCHED_FIFO 무한 루프면?" → 같은 CPU 의 일반 태스크가 굶는다. 리눅스는 RT throttling(`sched_rt_runtime_us`, 기본 1 초 중 0.95 초)으로 안전장치를 둔다.

</details>

<details>
<summary>Q7. 게이밍을 막는 다른 방법은? 실제 OS 들은?</summary>

OSTEP 8.5 가 소개하는 것: Solaris TS 클래스는 우선순위가 어떻게 바뀌는지·레벨별 slice·부스트 주기를 테이블로 관리하고(관리자가 튜닝, 기본 60 레벨, slice 20 ms~수백 ms, 약 1 초마다 부스트), FreeBSD 4.3 계열은 최근 CPU 사용량을 지수적으로 감쇠(decay-usage)시켜 우선순위를 계산한다 — "할당량 회계" 와 "부스트" 를 연속적으로 한 것이다. 리눅스 CFS/EEVDF 는 우선순위 큐 대신 가중 가상 실행 시간(vruntime)이 가장 작은 태스크를 고른다 — I/O 로 자주 자는 태스크는 vruntime 이 덜 쌓여 깨어나면 빨리 돈다(대화형 우대)인데, 잠든 동안 무한히 크레딧을 쌓지 못하게 깨어날 때 vruntime 을 최소값 근처로 끌어올린다 (게이밍 방지). 개념 구현은 OS-A04. 공통 원리: "얼마나 자주 양보했나" 가 아니라 **"실제로 CPU 를 얼마나 썼나"** 를 센다.

</details>

## 9. 추가 연습

### 연습 1 — RR + 컨텍스트 스위치 비용

모두 0 에 도착, I/O 없음. **다른** 작업으로 바꿀 때마다(첫 디스패치 포함) cs tick 을 쓴다. 같은 작업이 연달아 slice 를 받으면 비용 없음. `rr_cs(len, n, q, cs)` 가 makespan·유효 tick·스위치 수를 돌려주게 하라. {5,5,5}, q=1, cs=1 → makespan 30. {5,1}, q=1 → 9 (B 가 끝난 뒤 A 는 연속).

<details>
<summary>답 보기</summary>

큐 맨 앞을 꺼내 `if (j != last) { t += cs; switches++; }` 후 `min(rem, q)` 만큼 돈다. 끝났으면 버리고 아니면 꼬리로. {5,5,5} q=1 은 15 번 스위치 + 15 tick 일 = 30 (효율 50%), q=5 는 3 번 → 18. 길이 100 짜리 4 개로 q=1..64 를 돌리면 효율이 q 에 따라 단조 증가하고 q=1 이면 800 (효율 1/2). 효율 ≈ q/(q+cs) 를 숫자로 확인하는 연습 — W1 스윕에서 디스패치 114 번(q=1) vs 17 번(q=64) 의 의미다.

</details>

### 연습 2 — HRRN: SJF 의 기아를 에이징으로 막기

비선점. 고를 때마다 응답 비율 (대기 + 버스트) / 버스트 가 가장 큰 작업. 긴 작업 L(1, 10) 뒤로 길이 2 짜리 작업이 2 tick 마다 계속 오는 상황에서 SJF 와 HRRN 의 L 시작 시각을 비교하라.

<details>
<summary>답 보기</summary>

나눗셈 대신 교차 곱으로 비교: `(w1+b1)*b2 > (w2+b2)*b1`. SJF: 짧은 작업 10 개가 다 끝나야 L 이 돈다 → 시작 23. HRRN: t=7 에 L 의 비율 (6+10)/10 = 1.6 > 새 짧은 작업 (1+2)/2 = 1.5 → 시작 7. 대가로 평균 대기는 SJF 가 더 작다 (CHECK). 기다릴수록 우선순위가 오르는 것이 **에이징(aging)** — MLFQ 의 부스트, RTOS 에는 없는 것.

</details>

### 연습 3 — stride 스케줄링 (OSTEP 9.6)

티켓 100/50/250 → stride = 10000/티켓 = 100/200/40. 매번 pass 가 가장 작은 작업(동률은 낮은 id)을 돌리고 pass += stride. 처음 8 번의 순서와 80 번 뒤의 횟수를 확인하라.

<details>
<summary>답 보기</summary>

순서 `ABCCCACC` — OSTEP 그림 9.3 과 같다. 8 번마다 모든 pass 가 200 의 배수로 같아지므로 80 번이면 정확히 20 : 10 : 50 (2 : 1 : 5). 복권(lottery) 스케줄링은 같은 비율을 **확률적으로** 맞추고 전역 상태가 없다. stride 는 정확하지만 새 작업의 pass 를 0 으로 두면 독점하므로 현재 최소 pass 로 넣어야 한다 (OSTEP 9.6). 같은 아이디어가 패킷 스케줄러의 WFQ/DRR (NETHW-M10) 에도 있다.

</details>

### 연습 4 — 다음 버스트 예측 (지수 평균)

τ<sub>n+1</sub> = α·t<sub>n</sub> + (1−α)·τ<sub>n</sub>. α = 1/2, τ<sub>0</sub> = 10, 실제 버스트 6 4 6 4 13 13 13 일 때 예측값을 정수 연산으로 구하라.

<details>
<summary>답 보기</summary>

`(a_num*burst + (a_den-a_num)*tau) / a_den` → 8 6 6 5 9 11 12. α = 0 이면 과거만(10 고정), α = 1 이면 직전 버스트만. "No More Oracle" (OSTEP 7.9) 의 한 답이다: SJF/STCF 의 `next_burst` 를 이 예측값으로 바꾸면 구현 가능한 정책이 된다. 버스트가 4 → 13 으로 바뀐 뒤 예측이 따라가는 데 몇 번 걸리는지(5 → 9 → 11 → 12)가 α 의 트레이드오프다. MLFQ 는 이것을 "레벨" 로 이산화한 것으로 볼 수 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 13/13, 경고 0
```

## 10. 핵심 정리

- 지표 셋: 반환(끝 − 도착), 응답(첫 실행 − 도착), 대기(= 반환 − CPU − I/O). 반환 최적(SJF/STCF)과 응답 최적(RR)은 서로 당긴다.
- FCFS 는 convoy effect (그림 7.2: 평균 110), SJF 는 모두 동시 도착일 때 최적(순열 전수로 확인), 도착이 다르면 선점형 STCF(SRPT)가 최적 — 둘 다 미래를 알아야 한다.
- RR: q 가 작으면 응답↑·스위치↑, q 가 모든 버스트보다 길면 FCFS 와 같다 (W1: q=64 = FCFS). 효율 ≈ q/(q+c).
- I/O 와 계산을 겹치려면 짧은 버스트 작업을 먼저 — STCF 100 tick vs FCFS 130 tick.
- MLFQ 규칙 1~5: 높은 큐 먼저, 같은 큐 RR, 새 작업 맨 위, **할당량 누적** 소진 시 강등, S 마다 부스트. 길이를 모르고도 응답 0.2 (W1).
- 옛 규칙 4b → 게이밍 87% 독점, 새 규칙 4 로 36%. 부스트 없음 → 기아(200 tick 동안 0), S=50 → 공백 40. 부스트는 게이밍을, 할당량은 대화형 구제를 못 한다 — 둘 다 필요.
- 시뮬레이터는 동률 규칙을 고정·문서화해야 참조와 비교할 수 있다. OSTEP `scheduler.py`/`mlfq.py` 와 98 케이스 tick 단위 일치, 변이 테스트로 커버리지 확인.
- 펌웨어: RTOS 는 고정 우선순위(규칙 1·2만) + 정적 설계, 기아는 설계 결과 → 예산 감시·워치독 집계로 다룬다.

## 참고자료

- OSTEP 7장 *Scheduling: Introduction* — 지표, FIFO/SJF/STCF/RR, 그림 7.1~7.9, I/O 겹치기: <https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched.pdf> (로컬: `operating-system-three-easy-pieces/book-md/C07_scheduling_introduction.md`, Don 노트 `notes/2026-09-30_C07_scheduling_intro.md`)
- OSTEP 8장 *The Multi-Level Feedback Queue* — 규칙 1~5, 그림 8.2~8.7, Solaris/FreeBSD 튜닝: <https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-mlfq.pdf> (로컬: `book-md/C08_...md`, `notes/2026-09-30_C08_mlfq.md`)
- OSTEP 9장 *Proportional Share* — lottery, stride (그림 9.3): <https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-lottery.pdf>
- OSTEP 숙제 시뮬레이터 `cpu-sched/scheduler.py`, `cpu-sched-mlfq/mlfq.py` (로컬: `operating-system-three-easy-pieces/.tools/ostep-homework/`, 원본 <https://github.com/remzi-arpacidusseau/ostep-homework>)
- L. Schrage, "A Proof of the Optimality of the Shortest Remaining Processing Time Discipline", *Operations Research* 16(3), 1968
- F. J. Corbató et al., "An Experimental Time-Sharing System" (CTSS, 1962) — MLFQ 의 원조 (OSTEP 8장 참고문헌)
- POSIX `sched_setscheduler` / `sched_rr_get_interval` — SCHED_FIFO, SCHED_RR: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sched_rr_get_interval.html>
- Linux `sched(7)` — SCHED_RR slice, RT throttling: <https://man7.org/linux/man-pages/man7/sched.7.html>
- FreeRTOS — 스케줄링 (고정 우선순위 선점, time slicing): <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/04-Task-scheduling>
- 관련 레슨: OS-M02 (컨텍스트 스위치 비용), OS-A04 (CFS 개념 구현), LPERF-B10/M05 (런큐 대기·runqlat), ELX-A04 (PREEMPT_RT), ELX-B13 (systemd watchdog), RTOS-B01 (협력형 스케줄러), RTOS-B04 (우선순위 ready list), RTOS-M03~M05 (우선순위 역전), RTOS-M11 (RM 분석), RTOS-M12 (EDF vs RM), BM-M10 (워치독 하드웨어), NETHW-M10 (DRR 패킷 스케줄러), ALGO-M09 (구간 스케줄링 그리디)

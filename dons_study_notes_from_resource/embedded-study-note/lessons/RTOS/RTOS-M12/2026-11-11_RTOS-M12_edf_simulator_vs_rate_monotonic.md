---
id: RTOS-M12
time: 90분
updated: 2026-11-11
---
# RTOS-M12 EDF 스케줄링 시뮬레이터와 RM 비교

## 학습 목표

- 주기 태스크 세트를 **하이퍼피리어드(hyperperiod, lcm(T_i))** 동안 tick 단위로 돌리는 결정적(deterministic) 시뮬레이터를 직접 만들고, RM 과 EDF 를 **명시적인 동점 규칙**으로 비교할 수 있다.
- U = 34/35 ≈ 0.971 두 태스크 세트로 "U ≤ 1 인데 RM 은 놓치고 EDF 는 지킨다" 를 타임라인으로 보이고, Liu & Layland(1973) 의 EDF 조건 **U ≤ 1 ⇔ 스케줄 가능** 을 무작위 400 세트로 확인한다.
- 데드라인 미스 정책 두 가지(계속 실행 / 버림)를 구현하고, 과부하(U > 1)에서 **EDF 는 미스가 모든 태스크로 번지고(domino effect) RM 은 가장 낮은 우선순위에 가둔다** 는 것을 숫자로 설명할 수 있다.
- 선점(preemption)·컨텍스트 스위치·최대 응답 시간을 세는 기준을 스스로 정의하고, "EDF 가 선점이 더 적다" 같은 주장을 측정값으로 말할 수 있다.
- FreeRTOS 에 EDF 가 없는 이유(고정 우선순위 스케줄러, 소스 근거)와 Linux `SCHED_DEADLINE`(EDF + CBS) · Zephyr `CONFIG_SCHED_DEADLINE` 이 무엇을 다르게 하는지 말할 수 있다.

## 1. 문제

"EDF 가 이론상 최적인데 왜 FreeRTOS 는 고정 우선순위만 주나요? 둘을 시뮬레이션으로 비교해 보세요." — 스케줄 가능성 분석(RTOS-M11) 다음에 시니어 펌웨어·로보틱스 면접에서 나오는 질문이다. 정답은 "EDF 는 U ≤ 1 까지 다 받지만, **과부하가 오면 누가 놓칠지 고를 수 없다**" 이고, 이걸 숫자로 보여 줄 수 있으면 끝난다. 실무에서는 1 kHz 전류 루프·500 Hz IMU·CAN·로깅이 한 Cortex-M 코어를 나눠 쓸 때, 통신 재전송 폭주 같은 **일시 과부하** 에서 모터 루프를 지키느냐가 정책 선택을 가른다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">이용률 U = Σ C/T 축 위의 RM 과 EDF (D = T, 단일 코어, 독립 태스크)</text>
  <line x1="80" y1="190" x2="720" y2="190" stroke="currentColor"/> <line x1="80" y1="185" x2="80" y2="195" stroke="currentColor"/> <text x="76" y="210" fill="currentColor" font-size="12">0</text>
  <line x1="440" y1="40" x2="440" y2="152" stroke="currentColor" stroke-dasharray="3 3"/> <line x1="440" y1="185" x2="440" y2="195" stroke="currentColor"/> <text x="420" y="210" fill="currentColor" font-size="12">0.693</text>
  <line x1="510" y1="40" x2="510" y2="152" stroke="currentColor" stroke-dasharray="3 3"/> <line x1="510" y1="185" x2="510" y2="195" stroke="currentColor"/> <text x="494" y="210" fill="currentColor" font-size="12">0.828</text>
  <line x1="600" y1="40" x2="600" y2="195" stroke="currentColor"/> <text x="592" y="210" fill="currentColor" font-size="12">1.0</text>
  <text x="372" y="228" fill="currentColor" font-size="12">ln 2 (n→∞)</text> <text x="470" y="228" fill="currentColor" font-size="12">n = 2 경계</text> <text x="620" y="228" fill="currentColor" font-size="12">과부하 U &gt; 1</text>
  <text x="14" y="78" fill="currentColor" font-weight="bold">RM</text>
  <rect x="80" y="58" width="360" height="30" class="f-ok" stroke="currentColor"/> <text x="90" y="78" fill="currentColor" font-size="12">Liu–Layland 경계 이하: 항상 OK</text>
  <rect x="440" y="58" width="160" height="30" class="f-mut" stroke="currentColor"/> <text x="448" y="78" fill="currentColor" font-size="12">RTA 로 확인 (M11)</text>
  <rect x="600" y="58" width="120" height="30" class="f-warn" stroke="currentColor"/> <text x="606" y="78" fill="currentColor" font-size="12">낮은 prio 만 미스</text>
  <text x="14" y="138" fill="currentColor" font-weight="bold">EDF</text>
  <rect x="80" y="118" width="520" height="30" class="f-ok" stroke="currentColor"/> <text x="90" y="138" fill="currentColor" font-size="12">U ≤ 1 ⇔ 스케줄 가능 (Liu &amp; Layland 1973) — 최적 (Dertouzos 1974)</text>
  <rect x="600" y="118" width="120" height="30" class="f-warn" stroke="currentColor"/> <text x="606" y="138" fill="currentColor" font-size="12">도미노: 누구나</text>
  <circle cx="585" cy="170" r="5" class="f-acc" stroke="currentColor"/> <text x="436" y="175" fill="currentColor" font-size="12">0.96 · 0.971 (C, A)</text>
  <circle cx="704" cy="170" r="5" class="f-acc" stroke="currentColor"/> <text x="640" y="175" fill="currentColor" font-size="12">1.2 (B)</text>
</svg>
```

**시뮬레이터 규칙** (starter.c 머리 주석과 같다 — 이 레슨의 파일은 모두 자급식, 공유 라이브러리 없음):

| 항목 | 규칙 |
|---|---|
| 태스크 | `(C, T, D, O)` — 실행 시간, 주기, 상대 데드라인(0 이면 D = T), 첫 릴리즈. job k 는 `O + kT` 에 릴리즈, 절대 데드라인 = 릴리즈 + D |
| tick t 의 순서 | ① 절대 데드라인 == t 인데 남은 일이 있으면 **미스** ② 릴리즈 ③ job 고르기 ④ 선점·스위치 집계 ⑤ 1 tick 실행 |
| RM | 주기가 짧을수록 높은 고정 우선순위. 주기가 같으면 **태스크 번호가 작은 쪽** |
| EDF | 절대 데드라인이 가장 이른 job. 같으면 **태스크 번호가 작은 쪽**, 그래도 같으면 먼저 릴리즈된 job |
| 미스 정책 | `MISS_CONTINUE` 늦어도 끝까지 (데드라인은 그대로) / `MISS_DROP` 데드라인 순간에 남은 일을 버림 |
| 선점 | 직전 tick 에 돌던 job 이 **아직 남은 일이 있는데** 이번 tick 에 다른 job 이 돈다 |
| 컨텍스트 스위치 | 이번 tick 에 도는 **태스크**(idle = -1 도 태스크)가 직전과 다르다. 같은 태스크의 다음 job 은 스위치가 아니다 |
| 타임라인 | `R` 실행 · `r` 끝나지 않은 job 이 있는데 못 돎 · `.` 할 일 없음 · `miss` 줄 = 미스가 확인된 tick 의 태스크 이니셜 (`*` = 2개 이상) |

EDF 동점을 "작은 번호" 로 고정한 이유: 규칙이 없으면 같은 입력에 다른 타임라인이 나와 테스트가 불가능하다. FreeRTOS 도 같은 우선순위에서는 "깨운 태스크가 **엄격히 높을 때만** 전환" 이라는 동점 규칙을 소스로 갖고 있다(tasks.c:4944 `if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )`). 어떤 규칙이든 좋지만 **문서화하고 테스트로 고정** 해야 한다.

### Part A — 시뮬레이터 핵심: 하이퍼피리어드, 선택 규칙, 미스, 집계

- `uint32_t hyperperiod(const ptask_t *ts, int n)` — **TODO 1.** `lcm(T_i)` 를 64비트로 누적, `HYPER_MAX`(100 000) 를 넘거나 T = 0 이면 0.
- `int before(const sim_t *s, const job_t *a, const job_t *b)` — **TODO 2.** RM = `(rank, seq)`, EDF = `(절대 데드라인, 태스크 번호, seq)` 사전식 비교.
- `void on_deadlines(sim_t *s)` — **TODO 3.** 데드라인 == now 인 job 을 미스로 세고, `MISS_DROP` 이면 버린다.
- `void account(sim_t *s, int j)` — **TODO 4.** 선점 · 컨텍스트 스위치 집계.
- 테스트: t1 = (2, 5), t2 = (4, 7), U = 34/35 (RM vs EDF 비교에 널리 쓰이는 교재 예, Buttazzo 교재의 주기 태스크 장 참고). RM 은 t2 첫 job 이 **t = 7 에 미스**(응답 8), EDF 는 미스 0. 타임라인 문자열 4줄이 정확히 일치. 그리고 무작위 400 세트에서 **EDF 미스 0 ⇔ U ≤ 1**, **RM 미스 0 ⇔ RTA 통과** (RTOS-M11 의 식을 테스트 쪽 오라클로).

### Part B — 과부하 U > 1: 도미노 vs 격리, 미스 정책의 차이

A(2, 8) · B(3, 10) · C(3, 12) · D(6, 15), U = 1.2. 같은 세트를 RM/EDF × 계속/버림 4가지로 돌린다.

- RM 은 위 세 태스크(U = 0.8)가 **한 번도** 놓치지 않고 D 만 8/8 미스. 계속 실행이면 D 의 밀린 job 이 5개까지 쌓이고, 버림이면 1개로 묶인다.
- EDF + 계속 실행은 **A·B·C·D 모두** 미스 (총 35회, 첫 미스 D@30 → A@32). 버림으로 바꾸면 10회로 준다.
- 같은 tick 에 두 job 이 동시에 미스하고 둘 다 버려지는 경우(지우면서 도는 루프의 함정)도 테스트한다.

### Part C — 모터 제어 펌웨어: 평소엔 EDF, 오버런이 오면?

1 tick = 100 µs. 모터 FOC 전류 루프 (2, 10) = 1 kHz · 200 µs, IMU (4, 20) = 500 Hz, CAN 통신 (12, 40) = 250 Hz, 로깅 (26, 100) = 100 Hz. U = 0.96.

- 평소: RM 은 로깅이 t = 100 에 미스 (최대 응답 108 = RTA 고정점), EDF 는 미스 0 → "EDF 면 같은 MCU 에 더 넣을 수 있다".
- CAN job 1 이 버스 오류 재전송으로 **+3 ms(30 tick)** 오버런. RM 은 모터·IMU 미스 0. EDF + 계속 실행은 **모터가 6번** 놓친다. EDF + 버림은 모터·IMU 0 — 대신 CAN job 을 버린다.

## 2. Problem (English)

> **Discrete-time RM vs EDF simulator with deadline-miss policies.**
>
> Each periodic task has `(C, T, D, O)`; job k is released at `O + k*T` with absolute deadline `release + D` (D = 0 means D = T). Simulate one processor tick by tick over the hyperperiod. At each tick: (1) any unfinished job whose absolute deadline equals now is a deadline miss — with `MISS_DROP` it is aborted, with `MISS_CONTINUE` it keeps running with its old deadline; (2) release jobs; (3) pick a job; (4) count preemptions and context switches; (5) run it for one tick. Implement:
>
> 1. `uint32_t hyperperiod(const ptask_t *ts, int n)` — lcm of periods computed without overflow; return 0 if it exceeds `HYPER_MAX` or a period is 0.
> 2. `int before(const sim_t *s, const job_t *a, const job_t *b)` — RM: shorter period first (tie → lower task index), FIFO within a task. EDF: earlier absolute deadline (tie → lower task index, then earlier release).
> 3. `void on_deadlines(sim_t *s)` — detect misses, apply the policy (careful: you remove elements while iterating).
> 4. `void account(sim_t *s, int j)` — preemption = the previously running job is still unfinished and a different job runs now; context switch = the running task (idle included) changed.
>
> Example: t1 = (C2, T5), t2 = (C4, T7), U = 34/35. RM: t2 misses at t = 7 (response 8), 5 preemptions. EDF: no miss, max responses 4 and 6, 2 preemptions. Both: 34 busy ticks, 1 idle tick, 15 context switches.
>
> Follow-ups: Prove EDF is optimal on one processor. Why does EDF behave worse than RM under overload? How long must you simulate (offsets, D < T)? How would you get EDF on FreeRTOS? What do Linux `SCHED_DEADLINE`'s runtime/deadline/period mean and how do they protect other tasks from an overrunning one?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 11/44 passed, exit 1
./starter -v                                                                 # 타임라인 + 태스크별 통계까지
```

처음 상태(stub)는 하이퍼피리어드가 0 이라 시뮬레이션을 거부하고(`sim_run` → -1), 선택 규칙은 릴리즈 순서(FIFO), 미스·선점은 세지 않는다. 그래서 "0 을 돌려줘야 하는" 오버플로 테스트와, 아무것도 돌지 않아 우연히 참이 되는 무작위 검사 몇 개(예: "U ≤ 1 ⇔ EDF 미스 0" 이 0 = 0 으로 성립)만 통과한다. 스레드 없는 단일 스레드 시뮬레이터라 `-pthread`·TSan 은 필요 없다. 루프는 모두 horizon 으로 끝이 정해져 있어 stub 상태에서도 멈추지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: 곱하기 전에 나눈다</summary>

`lcm(h, T) = h / gcd(h, T) * T`. `h * T / gcd` 순서면 곱에서 먼저 넘친다. 32비트로 하면 `65536u * 65537u == 65536u` — 넘침이 **그럴듯한 작은 값** 으로 보여 시뮬레이터가 아무 경고 없이 잘못된 구간만 돈다. `uint64_t` 로 누적하고 매 단계 `HYPER_MAX` 와 비교하면, 상한 이하의 h 와 32비트 T 의 곱은 2^64 를 넘지 않는다.

</details>

<details>
<summary>힌트 2 — TODO 2: 비교 함수 하나로 두 정책</summary>

`pick_job()` 은 주어져 있다: `before(a, b)` 가 참인 job 을 최소값처럼 고른다. 그러니 키만 정하면 된다. RM 은 태스크의 고정 순위 `s->rank[task]` (주기 오름차순, 이미 계산됨), 같은 태스크면 `seq`(릴리즈 순서). EDF 는 `dl` → `task` → `seq`. 키가 **전순서(total order)** 여야 `act[]` 배열 순서와 상관없이 결과가 같다 — `seq` 가 유일하므로 마지막 비교가 동점을 없앤다.

</details>

<details>
<summary>힌트 3 — TODO 3: 지우면서 도는 루프</summary>

`remove_job(s, j)` 은 마지막 job 을 j 자리로 옮긴다. 지웠으면 **j 를 올리지 말고** 같은 j 를 다시 봐야 한다. 그렇지 않으면 같은 tick 에 데드라인이 겹친 두 번째 job 을 건너뛴다 — Part B 의 p/q/s 세트(U = 2.25)가 정확히 이 경우를 잡는다. `missed` 플래그로 계속 실행 중인 늦은 job 을 두 번 세지 않게 하자.

</details>

<details>
<summary>힌트 4 — TODO 4: "선점" 과 "끝남" 을 구분</summary>

직전 job 의 `seq` 가 아직 `act[]` 에 있으면 끝나지 않은 것이다 — 그때만 선점. 직전 job 이 끝났으면 `sim_tick` 이 `prev_seq` 를 `NONE` 으로 지운다(주어진 코드). 버려진(drop) job 도 `act[]` 에서 빠지므로 선점으로 세지 않는다. 컨텍스트 스위치는 job 이 아니라 **태스크** 비교 — 실제 RTOS 에서 태스크는 `for(;;) { 일; xTaskDelayUntil(); }` 루프 하나라서, 다음 job 으로 넘어가는 건 스위치가 아니다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 시뮬레이터 | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | Linux (문서 기준, 미검증) | CMSIS-RTOS2 |
|---|---|---|---|---|
| 주기 릴리즈 `O + kT` | `xTaskDelayUntil(&last, T)` (tasks.c:2414) | `k_sleep()` / `k_timer` | `clock_nanosleep(TIMER_ABSTIME)` | `osDelayUntil()` |
| `POL_RM` 고정 순위 | `xTaskCreate(..., uxPriority, ...)`, `vTaskPrioritySet` (tasks.c:2844). 클수록 높음 (tasks.c:391 `uxPriority`, 0 = idle) | `k_thread_priority_set()` (작을수록 높음) | `SCHED_FIFO` + `sched_priority` | `osThreadSetPriority()` |
| `POL_EDF` | **없음** — tasks.c 에 "deadline" 이 0번 나온다(grep). 선택은 `taskSELECT_HIGHEST_PRIORITY_TASK()` (tasks.c:197–212): 우선순위 리스트만 본다 | `CONFIG_SCHED_DEADLINE` + `k_thread_deadline_set()` — **같은 우선순위 안에서** 데드라인 순 | `SCHED_DEADLINE` (EDF + CBS), `sched_setattr()` 의 runtime/deadline/period | 없음 |
| 미스 감지 | `xTaskDelayUntil` 의 반환값 `xShouldDelay` (tasks.c:2498) — 주석 "can be used to determine whether a deadline was missed" (include/task.h:991–992, RTOS-B06) | 앱이 직접 | 런타임 초과 시 throttling, `SCHED_FLAG_DL_OVERRUN` → `SIGXCPU` | 앱이 직접 |
| `MISS_DROP` | 앱 수준: 일을 시작하기 전에 "이미 늦었나" 확인하고 건너뛰기 | 앱 수준 | CBS 예산 소진 → 그 주기의 남은 실행이 다음 주기로 밀림 (버림과 다름) | 앱 수준 |
| idle = 태스크 -1 | idle 태스크 (`tskIDLE_PRIORITY` = 0, include/task.h:210) | idle 스레드 | idle | idle 스레드 |

FreeRTOS 의 tick 선점 판단도 우선순위 비교뿐이다 — `xTaskIncrementTick()` 이 깨운 태스크에 대해 `if( pxTCB->uxPriority > pxCurrentTCB->uxPriority ) xSwitchRequired = pdTRUE;` (tasks.c:4944–4946). 데드라인이라는 개념이 커널에 없다.

### Part A — 시뮬레이터 네 조각

**TODO 1 — 하이퍼피리어드.** 동기 릴리즈(O = 0)·D ≤ T 이면 스케줄은 하이퍼피리어드 H 마다 반복된다(H 시점에 모든 태스크가 다시 동시에 릴리즈되고, U ≤ 1 이면 그때 밀린 일이 없다). 그래서 **H 만큼만** 돌리면 충분하다.

```c
uint64_t h = 1;
for (int i = 0; i < n; i++) {
    if (ts[i].T == 0) return 0;
    h = h / gcd_u64(h, ts[i].T) * ts[i].T;      /* 먼저 나누고 곱한다 */
    if (h > HYPER_MAX) return 0;
}
return (uint32_t)h;
```

테스트 값: {5, 7} → 35, {4, 6, 10} → 60, {10, 20, 40, 100} → 200, {9973, 9967} → 0 (99 400 891), {65536, 65537} → 0. 마지막은 32비트로 곱하면 65536 이 나오는 함정이다. 실무 교훈: 서로소 주기(7 ms, 11 ms, 13 ms …)는 H 를 폭발시킨다 — 주기를 **조화(harmonic)** 로 맞추면 H 가 작아지고 RM 의 경계도 100 % 까지 올라간다.

**TODO 2 — 선택 규칙.**

```c
if (s->pol == POL_RM) {
    if (s->rank[a->task] != s->rank[b->task]) return s->rank[a->task] < s->rank[b->task];
    return a->seq < b->seq;
}
if (a->dl != b->dl) return a->dl < b->dl;         /* EDF: 이른 절대 데드라인 */
if (a->task != b->task) return a->task < b->task; /* 동점 → 작은 태스크 번호 */
return a->seq < b->seq;
```

**TODO 3 — 미스와 정책.** 데드라인 == now 인 job 만 본다(늦은 job 은 `missed` 플래그로 한 번만). 버리면 `remove_job` 후 `continue` — j 를 올리지 않는다.

```c
for (int j = 0; j < s->nact; ) {
    job_t *jb = &s->act[j];
    if (jb->dl == s->now && !jb->missed) {
        tstat_t *st = &s->st[jb->task];
        jb->missed = 1; st->missed++; s->misses++;
        if (st->first_miss == NONE) st->first_miss = s->now;
        note_miss(s, jb->task);
        if (s->mp == MISS_DROP) { st->dropped++; remove_job(s, j); continue; }
    }
    j++;
}
```

**TODO 4 — 집계.** 직전 job 이 `act[]` 에 살아 있고 이번에 다른 job 이 돌면 선점. 태스크가 바뀌면 스위치.

```c
int task = (j >= 0) ? s->act[j].task : -1;
uint32_t seq = (j >= 0) ? s->act[j].seq : NONE;
if (s->prev_seq != NONE && seq != s->prev_seq)
    for (int k = 0; k < s->nact; k++)
        if (s->act[k].seq == s->prev_seq) { s->preemptions++; s->st[s->act[k].task].preempted++; break; }
if (task != s->prev_task) s->ctx_switches++;
s->prev_task = task; s->prev_seq = seq;
```

**한 tick 씩 손으로 (t1 = (2, 5), t2 = (4, 7)).** 괄호 = 절대 데드라인.

| t | 미스 검사 / 릴리즈 | RM 이 고른 job | EDF 가 고른 job |
|---|---|---|---|
| 0 | t1(5), t2(7) 릴리즈 | t1 (주기 5 < 7) | t1 (5 < 7) |
| 1 | — | t1 → 끝 (응답 2) | t1 → 끝 |
| 2–4 | — | t2 (남은 4 → 1) | t2 (남은 4 → 1) |
| 5 | t1(10) 릴리즈 | **t1** — t2 선점 | **t2** (7 &lt; 10) → 끝 (응답 6) |
| 6 | — | t1 → 끝 | t1 |
| 7 | RM: t2(7) 남은 1 → **미스**. t2(14) 릴리즈 | t2 의 늦은 job → 끝 (응답 8) | t1 (10 &lt; 14) → 끝 (응답 3) |

갈림길은 t = 5 하나다: RM 은 "주기가 짧은 태스크" 를, EDF 는 "지금 가장 급한 job" 을 고른다. 이게 고정 우선순위와 동적 우선순위의 차이의 전부다.

```svg
<svg viewBox="0 0 740 284" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">t1 = (C2, T5), t2 = (C4, T7), U = 34/35 ≈ 0.971 — solution -v 의 실제 타임라인</text> <text x="10" y="50" fill="currentColor" font-weight="bold">RM (주기 짧은 t1 이 항상 우선) — t2 첫 job 이 t=7 에 미스, 응답 8</text> <text x="57" y="70" fill="currentColor" font-size="12">0</text> <text x="147" y="70" fill="currentColor" font-size="12">5</text> <text x="237" y="70" fill="currentColor" font-size="12">10</text> <text x="327" y="70" fill="currentColor" font-size="12">15</text> <text x="417" y="70" fill="currentColor" font-size="12">20</text> <text x="507" y="70" fill="currentColor" font-size="12">25</text>
  <text x="597" y="70" fill="currentColor" font-size="12">30</text> <text x="10" y="88" fill="currentColor" font-size="12">t1</text> <rect x="56" y="76" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="146" y="76" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="236" y="76" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="326" y="76" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="416" y="76" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="506" y="76" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="596" y="76" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="108" fill="currentColor" font-size="12">t2</text> <rect x="56" y="100" width="36" height="8" class="f-mut"/> <rect x="92" y="96" width="54" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="146" y="100" width="36" height="8" class="f-mut"/> <rect x="182" y="96" width="54" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="236" y="100" width="36" height="8" class="f-mut"/>
  <rect x="272" y="96" width="54" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="326" y="100" width="36" height="8" class="f-mut"/> <rect x="362" y="96" width="54" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="434" y="100" width="18" height="8" class="f-mut"/> <rect x="452" y="96" width="54" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="506" y="100" width="36" height="8" class="f-mut"/> <rect x="542" y="96" width="54" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="596" y="100" width="36" height="8" class="f-mut"/>
  <rect x="632" y="96" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="127" fill="currentColor" font-size="12">miss</text> <rect x="181" y="115" width="18" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="183" y="127" fill="currentColor" font-size="12">t</text> <text x="10" y="152" fill="currentColor" font-weight="bold">EDF (데드라인 이른 job 우선) — 미스 0, t2 최대 응답 6</text> <text x="57" y="172" fill="currentColor" font-size="12">0</text> <text x="147" y="172" fill="currentColor" font-size="12">5</text> <text x="237" y="172" fill="currentColor" font-size="12">10</text>
  <text x="327" y="172" fill="currentColor" font-size="12">15</text> <text x="417" y="172" fill="currentColor" font-size="12">20</text> <text x="507" y="172" fill="currentColor" font-size="12">25</text> <text x="597" y="172" fill="currentColor" font-size="12">30</text> <text x="10" y="190" fill="currentColor" font-size="12">t1</text> <rect x="56" y="178" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="146" y="182" width="18" height="8" class="f-mut"/> <rect x="164" y="178" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="236" y="182" width="36" height="8" class="f-mut"/>
  <rect x="272" y="178" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="326" y="178" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="416" y="178" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="506" y="182" width="18" height="8" class="f-mut"/> <rect x="524" y="178" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="596" y="178" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="210" fill="currentColor" font-size="12">t2</text>
  <rect x="56" y="202" width="36" height="8" class="f-mut"/> <rect x="92" y="198" width="72" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="182" y="202" width="18" height="8" class="f-mut"/> <rect x="200" y="198" width="72" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="308" y="198" width="18" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="326" y="202" width="36" height="8" class="f-mut"/> <rect x="362" y="198" width="54" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="434" y="202" width="18" height="8" class="f-mut"/>
  <rect x="452" y="198" width="72" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="560" y="198" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="596" y="202" width="36" height="8" class="f-mut"/> <rect x="632" y="198" width="36" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="229" fill="currentColor" font-size="12">miss</text> <text x="10" y="256" fill="currentColor" font-size="12">진한 칸 = 실행(R), 얇은 막대 = 끝나지 않은 job 이 기다림(r), miss = 미스 확인 tick.</text> <text x="10" y="274" fill="currentColor" font-size="12">t=5: RM 은 새 t1(데드라인 10)을, EDF 는 데드라인 7 인 t2 의 남은 1 tick 을 먼저.</text>
</svg>
```

실제 출력 (`./solution -v` 일부와 첫 표):

```text
Part A  U=34/35   misses  preempt  ctx  max_resp(t1,t2)  busy idle
        RM            1        5   15        2,8          34    1
        EDF           0        2   15        4,6          34    1
two-task RM (continue)
  task  rel done miss drop preempted max_resp backlog
  t1      7    7    0    0         0        2       1
  t2      5    5    1    0         5        8       2
```

- 둘 다 busy 34, idle 1 (t = 34) — 일의 총량은 정책과 무관하다(작업 보존, work-conserving). 다른 것은 **순서** 뿐.
- RM 의 미스는 1 번(하이퍼피리어드 안). t2 의 둘째 job 이 첫 job 의 늦은 1 tick 때문에 밀려 backlog 2 가 된다.
- 응답 시간은 정책이 "나눠 갖는다": EDF 는 t1 을 4 까지 늦추는 대신 t2 를 6 에 맞춘다. EDF 가 "공짜" 가 아니라, 여유 있는 태스크의 응답을 **데드라인 직전까지** 미뤄 쓰는 것이다 — 지터(jitter)에 민감한 제어 루프에는 단점이 될 수 있다.
- 선점 RM 5 vs EDF 2, 컨텍스트 스위치는 15 로 같다(이 세트에서는).

**정리(theorem)를 오라클로 쓰는 무작위 테스트.** 고정 시드 xorshift32 로 2–5 태스크, 주기 {4, 5, 6, 8, 10, 12, 15, 20, 24, 30, 40, 60} 중에서(H ≤ 120), C ∈ [1, T/2] 인 세트 400개:

```text
random: 400 sets, 223 with U<=1 (RM misses in 18 of them), EDF<->U<=1 agree 400/400, RM<->RTA agree 400/400
        U<=1 sets: preemptions RM=1008 EDF=895, ctx switches RM=6167 EDF=6077
```

| 검사 | 근거 | 결과 |
|---|---|---|
| EDF 미스 0 ⇔ Σ C_i·(H/T_i) ≤ H | Liu & Layland (1973): D = T 이면 deadline driven(EDF) 은 U ≤ 1 일 때 그리고 그때만 스케줄 가능 | 400/400 |
| RM 미스 0 ⇔ 모든 i 에서 RTA R_i ≤ D_i | 임계 순간(critical instant) — 동기 릴리즈의 첫 job 이 최악 (RTOS-M11) | 400/400 |
| RM 미스 0 ⇒ EDF 미스 0 | EDF 최적성 (Dertouzos 1974) | 위반 0 |
| U ≤ 1 이면 busy == Σ C_i·H/T_i | 작업 보존 | 223/223 |
| U ≤ 1 인데 RM 이 놓친 세트 | — | 18개 |

U ≤ 1 판정은 **정수** 로 한다: `Σ C_i·(H/T_i) ≤ H`. `0.1 + 0.2 + …` 같은 float 합은 경계(U 가 정확히 1)에서 틀릴 수 있다.

**왜 EDF 가 최적인가 (교환 논증, exchange argument).** 어떤 스케줄 S 가 모든 데드라인을 지킨다고 하자. S 에서 시각 t 에 데드라인이 더 늦은 job J_b 가 돌고, 데드라인이 더 이른 J_a 는 나중에 돈다면, J_b 의 그 1 tick 과 J_a 의 나중 1 tick 을 **맞바꾼다**. J_a 는 더 일찍 끝나니 괜찮고, J_b 는 J_a 가 원래 돌던 시각(≤ J_a 의 데드라인 ≤ J_b 의 데드라인)에 끝나니 여전히 괜찮다. 이 교환을 반복하면 S 가 EDF 스케줄이 된다 → "누구라도 지킬 수 있으면 EDF 도 지킨다" (Dertouzos 1974, 단일 코어·선점 가능·독립 job). 이 논증은 다중 코어에서는 깨진다 (RTOS-A08).

**선점 횟수.** U ≤ 1 인 223 세트 합계에서 RM 1008 vs EDF 895 (−11 %). Buttazzo (2005, "Rate Monotonic vs. EDF: Judgment Day") 가 "EDF 는 동적 우선순위라 오버헤드가 크다" 는 통념에 반해 **EDF 가 선점을 덜 일으킨다** 고 보고한 경향과 같은 방향이다. 직관: RM 은 짧은 주기 태스크가 오면 데드라인이 한참 남은 job 도 무조건 끊지만, EDF 는 새 job 의 데드라인이 더 늦으면 끊지 않는다.

### Part B — 과부하: 도미노와 격리

```text
Part B  U=1.2   H=120   misses A/B/C/D            total  preempt
        RM  continue     0    0    0    8             8       15
        RM  drop         0    0    0    7             7       15
        EDF continue    11    9    8    7            35        0
        EDF drop         0    2    1    7            10        0
```

```svg
<svg viewBox="0 0 740 364" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">과부하 U = 1.2: A(2,8) B(3,10) C(3,12) D(6,15) — 실제 출력, 처음 60 tick</text> <text x="10" y="50" fill="currentColor" font-weight="bold">EDF + 미스 후 계속 실행 — 4 태스크 모두 미스 (도미노)</text> <text x="57" y="70" fill="currentColor" font-size="12">0</text> <text x="112" y="70" fill="currentColor" font-size="12">5</text> <text x="167" y="70" fill="currentColor" font-size="12">10</text> <text x="222" y="70" fill="currentColor" font-size="12">15</text> <text x="277" y="70" fill="currentColor" font-size="12">20</text> <text x="332" y="70" fill="currentColor" font-size="12">25</text>
  <text x="387" y="70" fill="currentColor" font-size="12">30</text> <text x="442" y="70" fill="currentColor" font-size="12">35</text> <text x="497" y="70" fill="currentColor" font-size="12">40</text> <text x="552" y="70" fill="currentColor" font-size="12">45</text> <text x="607" y="70" fill="currentColor" font-size="12">50</text> <text x="662" y="70" fill="currentColor" font-size="12">55</text> <text x="10" y="88" fill="currentColor" font-size="12">A</text> <rect x="56" y="76" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="144" y="80" width="66" height="8" class="f-mut"/>
  <rect x="210" y="76" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="232" y="80" width="33" height="8" class="f-mut"/> <rect x="265" y="76" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="80" width="99" height="8" class="f-mut"/> <rect x="419" y="76" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="441" y="80" width="33" height="8" class="f-mut"/> <rect x="474" y="76" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="496" y="80" width="99" height="8" class="f-mut"/>
  <rect x="595" y="76" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="617" y="80" width="66" height="8" class="f-mut"/> <rect x="683" y="76" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="705" y="80" width="11" height="8" class="f-mut"/> <text x="10" y="108" fill="currentColor" font-size="12">B</text> <rect x="56" y="100" width="22" height="8" class="f-mut"/> <rect x="78" y="96" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="166" y="100" width="66" height="8" class="f-mut"/>
  <rect x="232" y="96" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="276" y="100" width="44" height="8" class="f-mut"/> <rect x="320" y="96" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="386" y="100" width="110" height="8" class="f-mut"/> <rect x="496" y="96" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="529" y="100" width="121" height="8" class="f-mut"/> <rect x="650" y="96" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="683" y="100" width="22" height="8" class="f-mut"/>
  <rect x="705" y="96" width="11" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="128" fill="currentColor" font-size="12">C</text> <rect x="56" y="120" width="55" height="8" class="f-mut"/> <rect x="111" y="116" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="188" y="120" width="99" height="8" class="f-mut"/> <rect x="287" y="116" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="120" width="121" height="8" class="f-mut"/> <rect x="441" y="116" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="474" y="120" width="143" height="8" class="f-mut"/> <rect x="617" y="116" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="650" y="120" width="66" height="8" class="f-mut"/> <text x="10" y="148" fill="currentColor" font-size="12">D</text> <rect x="56" y="140" width="88" height="8" class="f-mut"/> <rect x="144" y="136" width="66" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="221" y="140" width="132" height="8" class="f-mut"/> <rect x="353" y="136" width="66" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="419" y="140" width="110" height="8" class="f-mut"/> <rect x="529" y="136" width="66" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="595" y="140" width="121" height="8" class="f-mut"/> <text x="10" y="167" fill="currentColor" font-size="12">miss</text> <rect x="385" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="387" y="167" fill="currentColor" font-size="12">D</text> <rect x="407" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="409" y="167" fill="currentColor" font-size="12">A</text>
  <rect x="451" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="453" y="167" fill="currentColor" font-size="12">C</text> <rect x="495" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="497" y="167" fill="currentColor" font-size="12">B</text> <rect x="550" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="552" y="167" fill="currentColor" font-size="12">D</text> <rect x="583" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="585" y="167" fill="currentColor" font-size="12">*</text> <rect x="605" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="607" y="167" fill="currentColor" font-size="12">B</text> <rect x="671" y="155" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="673" y="167" fill="currentColor" font-size="12">A</text> <text x="10" y="192" fill="currentColor" font-weight="bold">RM + 미스 후 계속 실행 — 미스는 가장 낮은 D 에만</text> <text x="57" y="212" fill="currentColor" font-size="12">0</text> <text x="112" y="212" fill="currentColor" font-size="12">5</text>
  <text x="167" y="212" fill="currentColor" font-size="12">10</text> <text x="222" y="212" fill="currentColor" font-size="12">15</text> <text x="277" y="212" fill="currentColor" font-size="12">20</text> <text x="332" y="212" fill="currentColor" font-size="12">25</text> <text x="387" y="212" fill="currentColor" font-size="12">30</text> <text x="442" y="212" fill="currentColor" font-size="12">35</text> <text x="497" y="212" fill="currentColor" font-size="12">40</text> <text x="552" y="212" fill="currentColor" font-size="12">45</text> <text x="607" y="212" fill="currentColor" font-size="12">50</text>
  <text x="662" y="212" fill="currentColor" font-size="12">55</text> <text x="10" y="230" fill="currentColor" font-size="12">A</text> <rect x="56" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="144" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="232" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="408" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="496" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="584" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="672" y="218" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="250" fill="currentColor" font-size="12">B</text> <rect x="56" y="242" width="22" height="8" class="f-mut"/> <rect x="78" y="238" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="166" y="238" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="276" y="238" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="386" y="238" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="408" y="242" width="22" height="8" class="f-mut"/> <rect x="430" y="238" width="11" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="496" y="242" width="22" height="8" class="f-mut"/> <rect x="518" y="238" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="606" y="238" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="270" fill="currentColor" font-size="12">C</text> <rect x="56" y="262" width="55" height="8" class="f-mut"/> <rect x="111" y="258" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="188" y="262" width="11" height="8" class="f-mut"/> <rect x="199" y="258" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="262" width="22" height="8" class="f-mut"/> <rect x="342" y="258" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="452" y="258" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="584" y="262" width="55" height="8" class="f-mut"/> <rect x="639" y="258" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="290" fill="currentColor" font-size="12">D</text> <rect x="56" y="282" width="198" height="8" class="f-mut"/> <rect x="254" y="278" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="276" y="282" width="33" height="8" class="f-mut"/> <rect x="309" y="278" width="11" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="282" width="55" height="8" class="f-mut"/>
  <rect x="375" y="278" width="11" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="386" y="282" width="55" height="8" class="f-mut"/> <rect x="441" y="278" width="11" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="452" y="282" width="33" height="8" class="f-mut"/> <rect x="485" y="278" width="11" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="496" y="282" width="55" height="8" class="f-mut"/> <rect x="551" y="278" width="33" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="584" y="282" width="110" height="8" class="f-mut"/>
  <rect x="694" y="278" width="22" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="309" fill="currentColor" font-size="12">miss</text> <rect x="220" y="297" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="222" y="309" fill="currentColor" font-size="12">D</text> <rect x="385" y="297" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="387" y="309" fill="currentColor" font-size="12">D</text> <rect x="550" y="297" width="12" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="552" y="309" fill="currentColor" font-size="12">D</text> <text x="10" y="336" fill="currentColor" font-size="12">EDF: 늦은 job 은 옛 데드라인을 그대로 가져 새 job 을 모두 이긴다 → 늦음이 번진다.</text> <text x="10" y="354" fill="currentColor" font-size="12">RM: A·B·C (U = 0.8) 는 D 가 얼마나 밀렸든 제때 돈다. * = 같은 tick 에 2개 이상.</text>
</svg>
```

**RM 의 격리.** 고정 우선순위에서 태스크 i 가 받는 간섭은 **자기보다 높은 태스크들** 에게서만 온다. A·B·C 의 U 는 0.25 + 0.3 + 0.25 = 0.8 이고 RTA 가 통과하므로, D 가 얼마나 밀려 있든 A·B·C 는 매번 제때 끝난다. 남는 CPU 0.2 를 D(필요 0.4)가 받으니 8/8 미스. 계속 실행이면 D 의 job 이 쌓여 최대 응답 75, backlog 5 — 정확히 **"가장 덜 중요한 태스크가 혼자 굶는다"**.

```text
overload RM continue                                    overload EDF continue
  task  rel done miss drop preempted max_resp backlog     task  rel done miss drop preempted max_resp backlog
  A      15   15    0    0         0        2       1     A      15   13   11    0         0       20       3
  B      12   12    0    0         3        5       1     B      12   10    9    0         0       24       3
  C      10   10    0    0         3        8       1     C      10    8    8    0         0       27       3
  D       8    4    8    0         9       75       5     D       8    6    7    0         0       31       3
```

(`./solution -v` 의 두 통계 블록을 옆으로 붙인 것.)

**EDF 의 도미노.** t = 30 에 D 의 job(데드라인 30)이 늦는다. 계속 실행이면 이 job 은 **데드라인 30 을 그대로** 들고 있으므로, 데드라인 32 · 36 · 40 … 인 모든 새 job 보다 급하다. 그래서 먼저 돌고, 그 사이 A(데드라인 32)가 늦고, A 의 늦은 job 이 또 모든 것을 이기고 … 늦음이 한 태스크에서 다음 태스크로 넘어간다. 결과: 4 태스크 전부 미스, 총 35회(RM 의 4배 이상). EDF 에서 "가장 급한 것" 은 "가장 늦은 것" 이 되기 때문이다 — 과부하에서는 **이미 실패한 job 에 CPU 를 쏟는다**.

- EDF 선점 0 회 (이 세트): 새로 릴리즈되는 job 의 데드라인이 늘 밀린 job 보다 늦어서 아무도 끊지 못한다. 밀린 job 이 쌓인 과부하 EDF 는 "가장 오래 밀린 것부터" 의 FIFO 에 가깝게 돈다.
- **버림(drop)** 은 늦은 job 을 데드라인 순간에 지워서 사슬을 끊는다: 35 → 10. A 는 0 으로 돌아오지만 B·C 는 여전히 놓친다 — EDF 는 여전히 "누가 놓칠지" 를 고르지 않는다.
- RM 은 drop 이든 continue 든 미스가 D 에만 있다. 버림은 D 의 backlog 를 1 로 묶는다(`max_backlog` 5 → 1, 테스트로 확인).

이것이 Buttazzo 교재의 과부하 장에서 다루는 EDF 의 domino effect 다. 해결책은 정책 쪽에서 나온다: (1) 수락 제어(admission control) — 애초에 U ≤ 1 만 받기, (2) 예산 강제(budget enforcement) — Linux `SCHED_DEADLINE` 의 CBS 처럼 각 태스크가 선언한 runtime 을 넘으면 그 주기에서 멈추게, (3) 중요도와 긴급도를 분리 — 안전 필수 태스크는 높은 고정 우선순위, 나머지만 EDF.

**같은 tick 에 두 job 이 동시에 미스.** p/q/s = (3, 4) 세 개, U = 2.25, EDF + drop. t = 4 에 q(남은 2)와 s(남은 3)가 함께 미스 → `missrow[4] == '*'`, 둘 다 dropped 1. 힌트 3 의 "j 를 올리면 하나를 건너뛴다" 버그가 바로 여기서 잡힌다 (아래 돌연변이 표).

### Part C — 모터 제어 펌웨어: 평소 vs 오버런

```text
Part C  U=0.96 H=200  RM: misses=1 (log max_resp=108)  EDF: misses=0 (log max_resp=90)  preempt RM=13 EDF=14
        can overrun +30: misses motor/imu/can/log  RM-cont 0/0/2/2  EDF-cont 6/3/3/2  EDF-drop 0/0/1/2
```

**평소 (U = 0.96).** RM 에서 로깅의 첫 job 이 t = 100 에 미스하고 t = 108 에 끝난다. RTA 로 검산: `R = 26 + 2⌈R/10⌉ + 4⌈R/20⌉ + 12⌈R/40⌉` 를 26 + 18 = 44 에서 시작하면 44 → 72 → 82 → 100 → 102 에서 D = 100 을 처음 넘는다(RTOS-M11 처럼 R > D 에서 멈추면 여기서 "불가" 판정 — 100 은 D 이하지만 고정점이 아니다). 멈추지 않고 고정점까지 가면 102 → 108 → 108. 시뮬레이터의 최대 응답 **108 = RTA 고정점** 이 정확히 맞는다. EDF 는 미스 0, 로깅 최대 응답 90.

**오버런.** CAN job 1 (t = 40 릴리즈, 데드라인 80) 이 42 tick 을 쓴다.

```svg
<svg viewBox="0 0 740 506" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part C: can job 1 이 +3 ms 오버런 (1 tick = 100 µs), t70~t119 확대 — 실제 출력</text> <text x="10" y="50" fill="currentColor" font-weight="bold">EDF + 계속 실행 — 데드라인 80 을 넘긴 can job 이 t97 까지 계속 돈다 → 모터 미스</text> <text x="57" y="70" fill="currentColor" font-size="12">70</text> <text x="122" y="70" fill="currentColor" font-size="12">75</text> <text x="187" y="70" fill="currentColor" font-size="12">80</text> <text x="252" y="70" fill="currentColor" font-size="12">85</text> <text x="317" y="70" fill="currentColor" font-size="12">90</text> <text x="382" y="70" fill="currentColor" font-size="12">95</text>
  <text x="447" y="70" fill="currentColor" font-size="12">100</text> <text x="512" y="70" fill="currentColor" font-size="12">105</text> <text x="577" y="70" fill="currentColor" font-size="12">110</text> <text x="642" y="70" fill="currentColor" font-size="12">115</text> <text x="10" y="88" fill="currentColor" font-size="12">motor</text> <rect x="56" y="76" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="186" y="80" width="234" height="8" class="f-mut"/> <rect x="420" y="76" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="472" y="80" width="234" height="8" class="f-mut"/>
  <text x="10" y="108" fill="currentColor" font-size="12">imu</text> <rect x="186" y="100" width="286" height="8" class="f-mut"/> <rect x="472" y="96" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="524" y="100" width="182" height="8" class="f-mut"/> <text x="10" y="128" fill="currentColor" font-size="12">can</text> <rect x="56" y="120" width="26" height="8" class="f-mut"/> <rect x="82" y="116" width="338" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="420" y="120" width="286" height="8" class="f-mut"/> <text x="10" y="148" fill="currentColor" font-size="12">log</text>
  <rect x="56" y="140" width="468" height="8" class="f-mut"/> <rect x="524" y="136" width="182" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="167" fill="currentColor" font-size="12">miss</text> <rect x="185" y="155" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="187" y="167" fill="currentColor" font-size="12">c</text> <rect x="315" y="155" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="317" y="167" fill="currentColor" font-size="12">m</text> <rect x="445" y="155" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="447" y="167" fill="currentColor" font-size="12">*</text> <rect x="575" y="155" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="577" y="167" fill="currentColor" font-size="12">m</text> <text x="10" y="192" fill="currentColor" font-weight="bold">EDF + drop — t80 에 늦은 can job 을 버린다 → 모터·IMU 미스 0</text> <text x="57" y="212" fill="currentColor" font-size="12">70</text> <text x="122" y="212" fill="currentColor" font-size="12">75</text> <text x="187" y="212" fill="currentColor" font-size="12">80</text> <text x="252" y="212" fill="currentColor" font-size="12">85</text>
  <text x="317" y="212" fill="currentColor" font-size="12">90</text> <text x="382" y="212" fill="currentColor" font-size="12">95</text> <text x="447" y="212" fill="currentColor" font-size="12">100</text> <text x="512" y="212" fill="currentColor" font-size="12">105</text> <text x="577" y="212" fill="currentColor" font-size="12">110</text> <text x="642" y="212" fill="currentColor" font-size="12">115</text> <text x="10" y="230" fill="currentColor" font-size="12">motor</text> <rect x="56" y="218" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="186" y="218" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="316" y="218" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="446" y="218" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="576" y="218" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="250" fill="currentColor" font-size="12">imu</text> <rect x="186" y="242" width="26" height="8" class="f-mut"/> <rect x="212" y="238" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="446" y="242" width="26" height="8" class="f-mut"/> <rect x="472" y="238" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="270" fill="currentColor" font-size="12">can</text> <rect x="56" y="262" width="26" height="8" class="f-mut"/> <rect x="82" y="258" width="104" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="186" y="262" width="338" height="8" class="f-mut"/> <rect x="524" y="258" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="576" y="262" width="26" height="8" class="f-mut"/> <rect x="602" y="258" width="104" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="290" fill="currentColor" font-size="12">log</text>
  <rect x="56" y="282" width="208" height="8" class="f-mut"/> <rect x="264" y="278" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="316" y="282" width="26" height="8" class="f-mut"/> <rect x="342" y="278" width="104" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="446" y="282" width="260" height="8" class="f-mut"/> <text x="10" y="309" fill="currentColor" font-size="12">miss</text> <rect x="185" y="297" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="187" y="309" fill="currentColor" font-size="12">c</text>
  <rect x="445" y="297" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="447" y="309" fill="currentColor" font-size="12">l</text> <text x="10" y="334" fill="currentColor" font-weight="bold">RM + 계속 실행 — can 은 3순위라 모터·IMU 를 못 막는다</text> <text x="57" y="354" fill="currentColor" font-size="12">70</text> <text x="122" y="354" fill="currentColor" font-size="12">75</text> <text x="187" y="354" fill="currentColor" font-size="12">80</text> <text x="252" y="354" fill="currentColor" font-size="12">85</text> <text x="317" y="354" fill="currentColor" font-size="12">90</text>
  <text x="382" y="354" fill="currentColor" font-size="12">95</text> <text x="447" y="354" fill="currentColor" font-size="12">100</text> <text x="512" y="354" fill="currentColor" font-size="12">105</text> <text x="577" y="354" fill="currentColor" font-size="12">110</text> <text x="642" y="354" fill="currentColor" font-size="12">115</text> <text x="10" y="372" fill="currentColor" font-size="12">motor</text> <rect x="56" y="360" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="186" y="360" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="316" y="360" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="446" y="360" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="576" y="360" width="26" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="392" fill="currentColor" font-size="12">imu</text> <rect x="186" y="384" width="26" height="8" class="f-mut"/> <rect x="212" y="380" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="446" y="384" width="26" height="8" class="f-mut"/> <rect x="472" y="380" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="412" fill="currentColor" font-size="12">can</text> <rect x="56" y="404" width="26" height="8" class="f-mut"/> <rect x="82" y="400" width="104" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="186" y="404" width="78" height="8" class="f-mut"/> <rect x="264" y="400" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="316" y="404" width="26" height="8" class="f-mut"/> <rect x="342" y="400" width="104" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="446" y="404" width="78" height="8" class="f-mut"/>
  <rect x="524" y="400" width="52" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="576" y="404" width="26" height="8" class="f-mut"/> <rect x="602" y="400" width="104" height="16" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="10" y="432" fill="currentColor" font-size="12">log</text> <rect x="56" y="424" width="650" height="8" class="f-mut"/> <text x="10" y="451" fill="currentColor" font-size="12">miss</text> <rect x="185" y="439" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="187" y="451" fill="currentColor" font-size="12">c</text>
  <rect x="445" y="439" width="13" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="447" y="451" fill="currentColor" font-size="12">l</text> <text x="10" y="478" fill="currentColor" font-size="12">m = motor 미스, c = can, * = 2개 이상 (t100: motor·imu·log). 모터 주기 10 tick(1 kHz).</text> <text x="10" y="496" fill="currentColor" font-size="12">EDF 계속 실행: t106~t119 는 데드라인 100 을 이미 넘긴 log 가 차지 → t110 에 모터 또 미스.</text>
</svg>
```

- EDF + 계속 실행: t = 80 에 CAN 이 미스했지만 데드라인 80 을 들고 계속 돈다 → 데드라인 90 인 모터 job 보다 급하다 → CAN 이 t97 까지 계속 돌고 모터가 **t90 · t100 에 연속 미스** (IMU 도 t100 에 미스). 그 뒤 t106–t119 에는 데드라인 100 을 이미 넘긴 로깅 job 이 같은 이유로 CPU 를 잡아 t110 에 모터가 또 미스. 모터 최대 응답 22 tick = 2.2 ms (주기 1 ms). 전류 루프가 연속으로 주기를 놓치면 실제 모터에서는 토크 리플이나 과전류 트립으로 이어질 수 있다.
- EDF + 버림: t = 80 에 늦은 CAN job 을 버린다 → 모터·IMU 미스 0. 대가는 CAN 메시지 하나와 로깅.
- RM + 계속 실행: CAN 은 3순위라 모터(1순위)·IMU(2순위)를 끊을 수 없다 → 미스 0. 대가는 CAN 과 로깅.

| 정책 | 평소 U = 0.96 | CAN 오버런 시 모터 미스 | 무엇을 잃나 |
|---|---|---|---|
| RM 배치 (FreeRTOS 고정 우선순위) | 로깅 1회 미스 | 0 | 낮은 순위(CAN, 로깅) |
| EDF + 계속 실행 | 미스 0 | **6** | 아무나 — 이번엔 모터 |
| EDF + 버림 | 미스 0 | 0 | 늦은 job (여기선 CAN, 로깅) |

실무 결론: EDF 의 이득(U 를 1 까지)은 **오버런이 없다는 가정** 위에 있다. WCET 측정(RTOS-A03)에 자신이 없거나, 통신·로깅처럼 실행 시간이 입력에 따라 크게 변하는 태스크가 섞여 있으면, 펌웨어는 보통 RM(고정 우선순위) + 우선순위를 "중요도" 로 배치하는 쪽을 택한다. EDF 를 쓰려면 예산 강제(CBS)나 버림 같은 과부하 정책을 **반드시** 같이 설계해야 한다.

### 돌연변이(mutation) 확인

테스트가 진짜 버그를 잡는지 solution.c 의 임시 사본(/tmp, 레슨 파일 아님)을 한 줄씩 망가뜨려 돌렸다:

| 돌연변이 | 결과 |
|---|---|
| EDF 동점을 큰 번호 우선으로 | 36/44 |
| EDF 동점 규칙 삭제 (dl → seq) | 38/44 |
| 미스 검사를 1 tick 늦게 (`dl + 1 == now`) | 33/44 |
| drop 후 `continue` 대신 j++ | 42/44 |
| 컨텍스트 스위치를 job 단위로 | 43/44 |
| lcm 을 32비트 곱으로 | 43/44 |
| RM 순위 대신 태스크 번호 | 42/44 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 44/44 passed (5회 반복 동일)
./solution -v                                                                 # 위 풀이의 타임라인·통계 전부
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san -v   # 44/44, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 하이퍼피리어드 = 주기들의 곱, 또는 32비트 lcm | {4, 6, 10} 이면 240 (진짜 60) — 4배 더 돈다. 32비트면 `65536 * 65537` 이 65536 으로 둔갑 | `h = h / gcd(h, T) * T` 를 `uint64_t` 로, 매 단계 상한 검사 |
| 미스를 "job 이 끝날 때" 만 검사 | 끝나지 않는 job(과부하, drop)은 미스로 안 세진다 | 매 tick 시작에 `dl == now` 이고 남은 일이 있으면 미스 |
| 릴리즈를 미스 검사보다 먼저 | D = T 이면 같은 tick 에 새 job 이 들어와 순서가 꼬이고 통계가 1 tick 어긋난다 | 순서를 문서화: 미스 → 릴리즈 → 선택 |
| 배열에서 지우면서 `j++` | 같은 tick 의 두 번째 미스를 건너뜀 (p/q/s 테스트) | 지웠으면 같은 j 를 다시 본다 |
| EDF 동점 규칙 없음 | 같은 입력에 다른 타임라인 — 테스트·디버깅 불가. 구현에 따라 같은 데드라인끼리 번갈아 도는 thrash | `(dl, task, seq)` 전순서 |
| 끝난 job 을 "선점됨" 으로 셈 | 선점 횟수가 job 수만큼 부풀려진다 | 직전 job 이 `act[]` 에 살아 있을 때만 |
| U ≤ 1 을 float 로 비교 | U 가 정확히 1 인 경계에서 반올림으로 판정이 뒤집힐 수 있다 | `Σ C_i·(H/T_i) ≤ H` 정수 비교 |
| "U ≤ 1 이니 RM 도 된다" | RM 은 U = 0.971 에서도 놓친다 (Part A) | RM 은 RTA (RTOS-M11), EDF 만 U ≤ 1 |
| 과부하 대책 없이 EDF | 늦은 job 이 옛 데드라인으로 모두를 이긴다 → 모터 미스 (Part C) | 수락 제어 · 예산 강제(CBS) · 버림 · 중요 태스크는 고정 우선순위 |

## 8. 면접 Q&A

<details>
<summary>Q1. RM 과 EDF 를 비교해 보세요. 어느 쪽이 더 좋나요?</summary>

스케줄 가능성: D = T 독립 주기 태스크, 단일 코어에서 EDF 는 **U ≤ 1 ⇔ 가능** (Liu & Layland 1973) — 이보다 좋은 알고리즘은 없다(최적). RM 은 고정 우선순위 중 최적이지만 U ≤ n(2^{1/n} − 1) (n → ∞ 이면 ln 2 ≈ 0.693) 만 보장되고, 그 위는 RTA 로 따져야 한다 (Part A: U = 0.971 에서 RM 미스, EDF 통과). 구현: RM 은 우선순위 비트맵으로 O(1) 선택(RTOS-B04), EDF 는 데드라인 정렬 큐(힙이면 O(log n))와 job 마다 데드라인 갱신. 과부하: RM 은 낮은 우선순위만 미스(예측 가능), EDF 는 도미노 (Part B). 결론: "효율은 EDF, 예측 가능성과 생태계는 RM" — 그래서 FreeRTOS · Zephyr 기본 · CMSIS-RTOS2 는 고정 우선순위다. 꼬리: "U 가 0.7 이면?" → 둘 다 OK, 지터 특성(RM 은 높은 우선순위 지터가 작음)과 툴링으로 고른다.

</details>

<details>
<summary>Q2. EDF 가 단일 코어에서 최적임을 증명해 보세요.</summary>

교환 논증. 데드라인을 다 지키는 스케줄 S 가 있다고 하자. S 가 EDF 와 처음 다른 시각 t 에 데드라인이 늦은 J_b 를 돌리고, 더 이른 J_a 는 나중 시각 t' 에 돈다. t 와 t' 의 실행을 맞바꾸면 J_a 는 더 일찍 끝나고, J_b 는 t' ≤ d_a ≤ d_b 에 끝나므로 둘 다 데드라인을 지킨다. 이 교환을 앞에서부터 반복하면 S 는 EDF 스케줄이 되고 여전히 다 지킨다 (Dertouzos 1974). 전제: 단일 코어, 선점 가능, 독립 job(자원 공유·선행 관계 없음), 컨텍스트 스위치 비용 0. 다중 코어에서는 Dhall 효과로 전역 EDF 가 낮은 U 에서도 실패할 수 있다 (RTOS-A08 참고).

</details>

<details>
<summary>Q3. 과부하에서 EDF 가 왜 RM 보다 나쁜가요? 어떻게 막나요?</summary>

계속 실행 정책이면 늦은 job 은 옛 데드라인을 그대로 가지므로 모든 새 job 보다 "급하다". 그래서 이미 실패한 job 에 CPU 를 쏟는 동안 다음 job 들이 줄줄이 늦는다 — domino effect (Part B: RM 8회·D 에만, EDF 35회·전 태스크). RM 에서는 간섭이 위에서 아래로만 흐르므로 미스가 낮은 우선순위에 갇힌다. 대책: ① 수락 제어(U ≤ 1 만 받음), ② 데드라인 순간 버림(abort)이나 skip (Part B: 35 → 10), ③ 예산 강제 — Linux `SCHED_DEADLINE` 은 CBS 로 태스크가 선언한 runtime 을 넘으면 그 주기 동안 멈춰 다른 태스크를 보호한다(문서 기준), ④ 중요도와 긴급도 분리: 안전 필수 루프는 높은 고정 우선순위, 그 아래에서만 EDF.

</details>

<details>
<summary>Q4. FreeRTOS 에서 EDF 를 쓰려면 어떻게 하나요?</summary>

커널에는 없다: 선택은 `taskSELECT_HIGHEST_PRIORITY_TASK()` (tasks.c:197–212) 가 우선순위 리스트만 보고, tasks.c 에 "deadline" 이라는 단어가 한 번도 없다. 방법은 셋. ① 매 릴리즈 시점(예: `xTaskDelayUntil` 직후)에 모든 태스크의 우선순위를 "남은 데드라인 순위" 로 `vTaskPrioritySet()` (tasks.c:2844) — 릴리즈마다 O(n) 재배치, 우선순위 상속(RTOS-M04)과 섞이면 복잡. ② 디스패처 태스크 하나가 job 큐를 데드라인 힙으로 관리하고 작업 함수를 run-to-completion 으로 실행(선점 없음 → 비선점 EDF). ③ 커널 수정. 실무에서는 대개 "RM + 주기 조화 + RTA" 로 충분한지를 먼저 본다. 꼬리: "Zephyr 는?" → `CONFIG_SCHED_DEADLINE` 과 `k_thread_deadline_set()` 이 있지만 문서상 **같은 정적 우선순위 안에서만** 데드라인 순 (문서 기준, 미검증).

</details>

<details>
<summary>Q5. 시뮬레이션은 얼마나 길게 돌려야 하나요? 오프셋이 있으면?</summary>

동기 릴리즈(모든 O = 0)·D ≤ T 이면 하이퍼피리어드 H 하나로 충분하다: H 에 모두 다시 동시에 릴리즈되고, 데드라인 ≤ H 인 job 만 [0, H) 에 릴리즈되므로 U ≤ 1 이면 H 에 밀린 일이 0 이다. 고정 우선순위는 임계 순간 정리 덕에 첫 job 만 보면 된다(RTA). 오프셋이 있으면 첫 H 는 과도 구간일 수 있어 더 길게 봐야 한다 — 고전적인 상한은 max O + 2H (Leung & Merrill 1980 — 정확한 형태는 원문 확인 권장). EDF 는 동기 바쁜 구간(busy period) L = Σ⌈L/T_i⌉C_i 까지만 검사해도 된다 (연습 4). 그리고 H 자체가 서로소 주기에서 폭발하므로 64비트 + 상한 검사가 필수.

</details>

<details>
<summary>Q6. 선점·컨텍스트 스위치 횟수는 RM 과 EDF 중 어느 쪽이 많나요?</summary>

측정해 보면 EDF 가 선점이 적다: 무작위 223 세트(U ≤ 1) 합계 RM 1008 vs EDF 895, 두 태스크 예 5 vs 2. RM 은 짧은 주기 태스크가 릴리즈되면 무조건 끊지만, EDF 는 새 job 의 데드라인이 더 늦으면 끊지 않기 때문이다. Buttazzo (2005, "Judgment Day") 도 같은 결론. 다만 **정의에 따라 숫자가 바뀐다** — 이 레슨은 선점 = "끝나지 않은 job 이 밀려남", 스위치 = "도는 태스크가 바뀜(idle 포함)" 으로 정했다. 스위치 수는 거의 같다(6167 vs 6077) — 대부분이 "job 끝 → 다음 태스크/idle" 이라 정책과 무관하기 때문. LLF(최소 여유 우선)는 여유가 같은 job 끼리 번갈아 돌아 스위치가 크게 는다 (연습 3: 15 → 23).

</details>

<details>
<summary>Q7. D &lt; T 이면 무엇이 달라지나요?</summary>

고정 우선순위: 주기 대신 **상대 데드라인 순서**(Deadline Monotonic)가 최적이다 (Leung & Whitehead 1982) — 연습 1 에서 ADC 판독(D = 2, T = 10)을 RM 은 놓치고 DM 은 지킨다. EDF: U ≤ 1 은 필요조건일 뿐이고, 처리 요구량(processor demand) 검사 dbf(L) = Σ (⌊(L − D_i)/T_i⌋ + 1)·C_i ≤ L 를 모든 L 에서 확인해야 한다 (Baruah, Rosier & Howell 1990). 밀도 Σ C_i/D_i ≤ 1 은 충분조건이라 너무 비관적이다 (연습 2: 50 세트에서 거부했지만 실제로는 가능).

</details>

<details>
<summary>Q8. Linux SCHED_DEADLINE 은 무엇이고 Part C 의 오버런 문제를 어떻게 다루나요?</summary>

(커널 문서 Documentation/scheduler/sched-deadline.rst 기준, 이 Mac 에서는 실행 불가라 미검증.) 태스크가 `sched_setattr()` 로 runtime · deadline · period 를 선언하면 커널은 EDF 로 고르고, CBS(Constant Bandwidth Server, Abeni & Buttazzo 1998)로 각 태스크가 주기당 runtime 이상 못 쓰게 한다 — 넘으면 다음 주기까지 throttle. 수락 제어로 Σ runtime/period 가 한도를 넘는 설정을 거부한다. 그래서 Part C 의 CAN 오버런은 CAN 자신의 다음 주기로 밀릴 뿐 모터의 대역폭을 빼앗지 못한다(시간적 격리, temporal isolation). 즉 "EDF 의 효율 + RM 의 격리" 를 예산으로 얻는다. 꼬리: SCHED_DEADLINE 태스크는 SCHED_FIFO/RR 보다 먼저 선택된다.

</details>

## 9. 추가 연습

### 연습 1 — D &lt; T: Deadline Monotonic

제어 루프 ctrl = (C2, T4) 와 ADC 판독 adc = (C1, T10, **D2**) — 변환 완료 후 200 µs 안에 결과 레지스터를 읽어야 다음 변환이 덮어쓰지 않는다. RM 과 DM 으로 돌려 비교하고, 무작위 D ≤ T 세트 300개에서 "RM 은 되는데 DM 은 안 되는" 세트가 있는지 확인하라.

<details>
<summary>답 보기</summary>

`sim_init` 의 순위 키를 `pol == POL_DM ? D : T` 로 바꾸면 끝.

```text
ex1 RM                                   ex1 DM
 ctrl : RR..RR..RR..RR..RR..              ctrl : rRR.RR..RR..RR..RR..
 adc  : rrR.......R.........              adc  : R.........R.........
 miss :   a                               miss :
ex1 RM: misses=1 (adc first miss t=2)   DM: misses=0, adc max_resp=1 ctrl max_resp=3
ex1 random D<=T: RM ok but DM misses=0, RM misses but DM ok=7
```

RM 은 주기가 짧은 ctrl 을 먼저 돌려 adc 가 t = 3 에 끝난다(D = 2 초과). DM 은 adc 를 먼저 → 응답 1, ctrl 은 3 ≤ 4. 무작위 결과는 DM 의 최적성(Leung & Whitehead 1982: 고정 우선순위 중 D ≤ T 에서 최적)과 맞다 — RM 이 되면 DM 도 된다(위반 0), 그 반대는 7 세트. 펌웨어 교훈: "주기가 길어도 응답이 급한" 일(ADC 판독, 통신 ACK)은 주기 대신 데드라인으로 우선순위를 매긴다.

</details>

### 연습 2 — EDF 처리 요구량 검사 (D ≤ T)

`uint64_t dbf(ts, n, L)` 와 `int dbf_ok(ts, n)` (0 &lt; L ≤ H 의 모든 L 에서 dbf(L) ≤ L) 를 작성하고, 무작위 D ≤ T 세트 300개에서 EDF 시뮬레이션과 일치하는지 보라. 밀도 검사 Σ C_i/D_i ≤ 1 과도 비교하라.

<details>
<summary>답 보기</summary>

dbf(L) = 구간 [0, L] 안에 릴리즈되고 **데드라인도 L 이하인** 일의 합 = Σ max(0, ⌊(L − D_i)/T_i⌋ + 1)·C_i. 그 일은 L 까지 반드시 끝나야 하므로 dbf(L) > L 이면 누구도 못 지킨다(필요). EDF 최적성으로 충분하기도 하다.

```text
ex2 dbf vs EDF sim agree 300/300 (infeasible 191), density test too pessimistic in 50, unsafe 0
ex2 a(2,6,3) b(3,8,5): dbf(3)=2 dbf(5)=5 dbf(9)=7 dbf(13)=10  density_ok=0 dbf_ok=1 EDF misses=0
```

a(2, 6, 3) · b(3, 8, 5) 는 밀도 2/3 + 3/5 ≈ 1.27 > 1 이라 밀도 검사는 거부하지만, dbf 는 L = 5 에서 딱 5 로 빠듯하게 통과하고 시뮬레이션도 미스 0. 동기 릴리즈·D ≤ T 면 L ≤ H 만 보면 충분하다(L = H 가 U ≤ 1 을 포함). 실제 도구는 검사점을 절대 데드라인 값들로만 줄인다.

</details>

### 연습 3 — LLF: 같은 결과, 더 많은 스위치

Least Laxity First: 여유(laxity) = 절대 데드라인 − 현재 − 남은 일 이 가장 작은 job 을 고른다 (동점 → 작은 번호). 두 태스크 예와 무작위 U ≤ 1 세트에서 EDF 와 컨텍스트 스위치를 비교하라.

<details>
<summary>답 보기</summary>

```text
ex3 LLF
 t1   : RrR..rRR..rRrR.RrR..RR...rRR..RrR..
 t2   : rRrRRR.rRRRrR.RrRrRR.rRRRR..RRrRrR.
ex3 two-task  EDF: misses=0 ctx=15 preempt=2   LLF: misses=0 ctx=23 preempt=10
ex3 random U<=1 sets=146  ctx switches EDF=4078 LLF=4865  LLF sets with a miss=0
```

LLF 도 단일 코어에서 최적으로 알려져 있고(Mok 1983) 이 표본에서도 미스 0 이었지만, 기다리는 job 의 여유는 매 tick 1 씩 줄고 도는 job 의 여유는 그대로라 **여유가 같아지는 순간마다 순서가 뒤집힌다** — t0–t2 의 `RrR` / `rRr` 가 그 모습. 선점 2 → 10, 스위치 15 → 23. 여유를 매 tick 다시 계산해야 하는 비용까지 있어 RTOS 커널에서는 거의 쓰지 않는다.

</details>

### 연습 4 — 동기 바쁜 구간 (busy period)

`busy_period()` 로 L = Σ⌈L/T_i⌉·C_i 의 최소 고정점(시작값 Σ C_i)을 구하고, 시뮬레이터가 "t > 0 에서 처음으로 밀린 job 이 0 개가 된 시각" 과 같은지 EDF·RM 모두에서 확인하라. 두 태스크 예에서 L 은 34(첫 idle tick)인가?

<details>
<summary>답 보기</summary>

```text
ex4 busy period L == first empty instant (EDF and RM) in 150/150 sets; two-task L=14 first_empty=14 first idle tick=34
```

L = 14 다: 6 → 8 → 12 → 14 → 14. t = 14 에 [0, 14) 에 릴리즈된 일(t1 × 3 = 6, t2 × 2 = 8)이 모두 끝나 밀린 일이 0 이 되지만, **바로 그 순간 t2 가 다시 릴리즈** 되어 CPU 는 쉬지 않는다 — 그래서 첫 idle tick(34)과 다르다. 고정점은 "밀린 일이 0 이 되는 순간" 이지 "CPU 가 노는 순간" 이 아니다(테스트는 릴리즈 **전에** 본다). 작업 보존 스케줄러라면 정책과 무관하게 같다(RM 도 150/150). EDF 검사(연습 2)는 L 까지만 확인해도 충분하므로 H 가 큰 세트에서 유용하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 12/12 passed
./practice -v                                                                # 위 타임라인들
```

## 10. 핵심 정리

- EDF = 절대 데드라인이 가장 이른 job (동적 우선순위). D = T 단일 코어에서 **U ≤ 1 ⇔ 스케줄 가능** (Liu & Layland 1973), 최적 (Dertouzos 1974, 교환 논증).
- RM = 주기 순 고정 우선순위. U ≤ n(2^{1/n} − 1) 만 보장, 그 위는 RTA (RTOS-M11). U = 34/35 두 태스크: RM 은 t = 7 미스, EDF 는 통과.
- 시뮬레이터는 동기 릴리즈면 하이퍼피리어드 하나로 충분. lcm 은 나누고 곱하고 64비트 + 상한. 동점 규칙은 문서화하고 테스트로 고정.
- 과부하: RM 은 미스가 가장 낮은 우선순위에 갇히고(격리), EDF 는 늦은 job 이 옛 데드라인으로 모두를 이겨 미스가 번진다 (도미노: 8 vs 35). 버림·수락 제어·예산 강제(CBS)로 끊는다.
- 측정: EDF 가 선점이 적다 (1008 vs 895), 스위치는 거의 같다. LLF 는 최적이지만 스위치가 많다.
- 펌웨어: FreeRTOS 에는 EDF 가 없다 (tasks.c 는 우선순위만 본다). WCET 가 불확실하면 RM + 중요도 순 우선순위, EDF 를 쓰면 과부하 정책을 같이 설계.

## 참고자료

- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", Journal of the ACM 20(1), 1973 — RM 경계, EDF(deadline driven) U ≤ 1
- M. L. Dertouzos, "Control Robotics: The Procedural Control of Physical Processes", Proc. IFIP Congress, 1974 — EDF 최적성
- J. Y.-T. Leung, J. Whitehead, "On the Complexity of Fixed-Priority Scheduling of Periodic, Real-Time Tasks", Performance Evaluation 2(4), 1982 — Deadline Monotonic
- J. Y.-T. Leung, M. L. Merrill, "A Note on Preemptive Scheduling of Periodic, Real-Time Tasks", Information Processing Letters 11(3), 1980 — 오프셋이 있을 때의 검사 구간
- S. K. Baruah, L. E. Rosier, R. R. Howell, "Algorithms and Complexity Concerning the Preemptive Scheduling of Periodic, Real-Time Tasks on One Processor", Real-Time Systems 2(4), 1990 — 처리 요구량(dbf) 검사
- A. K. Mok, "Fundamental Design Problems of Distributed Systems for the Hard-Real-Time Environment", PhD thesis, MIT, 1983 — LLF
- G. C. Buttazzo, "Rate Monotonic vs. EDF: Judgment Day", Real-Time Systems 29(1), 2005 — 선점 횟수, 과부하 동작, 구현 비용 비교
- G. C. Buttazzo, *Hard Real-Time Computing Systems* (Springer) — 주기 태스크 스케줄링 장(RM vs EDF 비교 예), 과부하 장(domino effect)
- L. Abeni, G. Buttazzo, "Integrating Multimedia Applications in Hard Real-Time Systems", RTSS 1998 — CBS
- FreeRTOS-Kernel V11.3.2 — tasks.c (`taskSELECT_HIGHEST_PRIORITY_TASK` 197–212, TCB `uxPriority` 391, `xTaskDelayUntil` 2414/2498, `vTaskPrioritySet` 2844, tick 선점 비교 4944), include/task.h:991–992, 210 — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- Linux Documentation/scheduler/sched-deadline.rst — <https://docs.kernel.org/scheduler/sched-deadline.html>
- Zephyr 문서 — Scheduling, `CONFIG_SCHED_DEADLINE`, `k_thread_deadline_set()` — <https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html>
- 관련 레슨: RTOS-M11 (RM 분석: Liu–Layland 경계, RTA), RTOS-B04 (우선순위 비트맵 O(1) 선택), RTOS-B06 (`xTaskDelayUntil`, 미스 감지), RTOS-M04/M05 (블로킹 항, 상속·천장), RTOS-A03 (WCET), RTOS-A04 (확장 RTA), RTOS-A08 (SMP)

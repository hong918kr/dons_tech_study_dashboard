---
id: OS-A04
time: 110분
updated: 2026-11-15
---
# OS-A04 CFS 개념 구현: vruntime과 nice 가중치

## 학습 목표

- Linux 6.18.55 `kernel/sched/core.c` 의 `sched_prio_to_weight[40]`·`sched_prio_to_wmult[40]` 를 그대로 가져와 **vruntime += Δ × 1024 / w** 를 커널과 같은 **역가중치 곱 + 시프트** (`__calc_delta`) 로 구현하고, nice {0,0} = 50/50, {0,5} = 1024:335, {−5,0,5} 몫을 시뮬레이터로 맞춘다.
- 런큐를 min-heap 으로 짜서 **단독 테스트**(2 만 번 무작위 연산 vs 전수 최솟값, 불변식) 하고, CFS 시절 `sched_latency`/`min_granularity` 로 period·slice 를 계산해 tick 선점을 돌린다. "항상 runnable 인 태스크의 **lag 는 period + tick 이하**" 를 상한으로 검사한다.
- **min_vruntime 추적**과 깨어난 태스크 **배치(place_entity)** 를 구현하고, 1 초 잔 태스크가 배치 없이는 **501 ms 를 독점**(다른 태스크 505 ms 기아)하고 배치가 있으면 3 ms 만에 돌아가는 것을 숫자로 보인다.
- Linux 6.6 부터 pick 이 **EEVDF** 로 바뀐 이유를 lag·eligibility·가상 마감으로 설명하고, 6.18 `pick_eevdf`/`avg_vruntime`/`place_entity` 를 읽어 연습에서 미니 EEVDF 와 **같은 몫, 짧은 slice = 짧은 대기(6 → 2 ms)** 를 재현한다.
- 임베디드 리눅스에서 nice 는 **몫**이지 지연 보장이 아님을, SCHED_FIFO 폭주 + RT throttling(VM 실제값 950000/1000000 µs) 시나리오로 보이고 워치독 설계로 연결한다.

## 1. 문제

"CFS 는 어떻게 공정한가요? nice 5 는 CPU 를 얼마나 받죠? 오래 잔 태스크가 깨어나면 왜 독점하지 않나요?" — 리눅스 커널·임베디드 리눅스·성능 면접의 단골이다. 보드 위에서 센서 수집·로깅·OTA 압축·UI 가 한 CPU 를 나눌 때 "nice 를 낮추면 해결되나, SCHED_FIFO 로 가야 하나" 를 판단하려면 스케줄러가 시간을 **가중치로 나눈 가상 시간**으로 센다는 것을 손으로 구현해 봐야 한다. OS-B12 는 반환/응답 시간 중심 정책(FCFS~MLFQ)을, RTOS-M12 는 마감 중심(EDF)을 다뤘다. 이 레슨은 세 번째 축 **비례 공정(proportional share)** 이다 (OS-B12 연습 3 의 stride 스케줄링이 같은 아이디어의 결정적 버전).

핵심 세 줄:

1. 태스크마다 **vruntime**(가상 실행 시간) 을 둔다. 실제로 Δ 만큼 돌면 `vruntime += Δ × 1024 / weight`. nice 0(weight 1024) 은 실제 시간과 같은 속도, nice 5(335) 는 3.06 배 빨리 오른다.
2. 항상 **vruntime 이 가장 작은** 태스크를 고른다 → 모두의 vruntime 이 나란히 올라가고, 실제 시간은 weight 비율로 나뉜다.
3. 막 깨어난 태스크는 vruntime 이 한참 뒤처져 있다 → 그대로 두면 따라잡을 때까지 독점한다. 그래서 **min_vruntime 근처로 끌어올린다** (CFS 시절) / **잠들기 전 lag 를 보존한다** (6.6+ EEVDF).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">vruntime 은 weight 로 나눈 시간: nice 0(A) 기울기 1, nice 5(B) 기울기 1024/335 ≈ 3.06</text>
  <line x1="60" y1="260" x2="700" y2="260" stroke="currentColor"/>
  <line x1="60" y1="260" x2="60" y2="40" stroke="currentColor"/>
  <text x="600" y="282" fill="currentColor">실제 시간 (ms) →</text>
  <text x="66" y="48" fill="currentColor">vruntime ↑</text>
  <polyline points="60,260 240,188 300,188 480,116 540,116 660,68" fill="none" stroke="currentColor" stroke-width="3"/>
  <polyline points="60,260 240,260 300,186 480,186 540,113 660,113" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4"/>
  <rect x="60" y="232" width="180" height="14" class="f-ok" stroke="currentColor"/>
  <rect x="240" y="232" width="60" height="14" class="f-warn" stroke="currentColor"/>
  <rect x="300" y="232" width="180" height="14" class="f-ok" stroke="currentColor"/>
  <rect x="480" y="232" width="60" height="14" class="f-warn" stroke="currentColor"/>
  <rect x="540" y="232" width="120" height="14" class="f-ok" stroke="currentColor"/>
  <text x="130" y="243" fill="currentColor">A</text><text x="262" y="243" fill="currentColor">B</text>
  <text x="380" y="243" fill="currentColor">A</text><text x="502" y="243" fill="currentColor">B</text>
  <text x="590" y="243" fill="currentColor">A</text>
  <text x="250" y="180" fill="currentColor">A 4.5 ms 실행 → vr +4.5</text>
  <text x="305" y="206" fill="currentColor">B 1.5 ms 실행 → vr +4.6</text>
  <text x="560" y="60" fill="currentColor">A (실선)</text>
  <text x="560" y="105" fill="currentColor">B (점선)</text>
  <rect x="470" y="140" width="250" height="56" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="480" y="160" fill="currentColor">규칙: 항상 vr 가 작은 쪽을 돌린다</text>
  <text x="480" y="178" fill="currentColor">→ vr 은 나란히, 실제 시간은 3.06 : 1</text>
  <text x="480" y="192" fill="currentColor">→ A 75.4 %, B 24.6 % (1024:335)</text>
</svg>
```

| 개념 | 이 레슨 코드 | 커널 (6.18.55) | 한 줄 |
|---|---|---|---|
| nice → weight | `set_load_weight` | `core.c:1448 set_load_weight`, 표 `core.c:10331` | nice 1 차이 ≈ 1.25 배 |
| Δ×1024/w | `calc_delta_fair` | `fair.c:260 __calc_delta`, `:290 calc_delta_fair` | 나눗셈 대신 2^32/w 곱 |
| 런큐 | min-heap (`heap_push/pop_min`) | RB 트리 (augmented, deadline 순) | 최소 vr 꺼내기 |
| 몫 계산 | `sched_period_us`, `sched_slice_us` | 6.5 까지 `sched_slice`; 6.6+ 는 `base_slice` | latency 를 weight 로 나눔 |
| 최소 기준점 | `update_min_vruntime` | 6.18 은 `zero_vruntime` + `avg_vruntime()` | 새/깨어난 태스크의 기준 |
| 깨어남 배치 | `place_entity` | `fair.c:5313 place_entity` (lag 보존) | 독점 방지 |

### Part A — 가중치, 고정소수점, 런큐, 몫

- `set_load_weight(lw, nice)` — 표에서 weight 와 inv_weight(2^32/w). 범위 밖 nice 는 자른다. 인접 nice 비가 1.18~1.32 (≈1.25) 인지, inv × w ≈ 2^32 인지 표 전체를 검사.
- `calc_delta_fair(Δns, lw)` — weight 1024 면 그대로, 아니면 `(Δ × (1024·inv >> fs)) >> (32 − fs)`. 40 개 nice × 6 개 Δ 에서 정확한 값과 1 ns + 2 ppm 이내.
- `heap_push / heap_pop_min` — vruntime(부호 있는 차이로 비교) → 같으면 번호. **단독 테스트**: 무작위 20 000 연산마다 전수 최솟값과 비교 + 불변식 검사.
- `sysctl_factor(ncpu)` = 1 + ilog2(min(ncpu, 8)), `sched_period_us(nr)`, `sched_slice_us(i)`.
- `update_curr` / `check_preempt_tick` — tick(1 ms) 마다 vruntime 반영, "slice 다 씀" 또는 "leftmost 보다 slice 이상 앞섬" 이면 교체.
- 결과: {0,0} 50/50, {0,5} 비 3.05 (이상 3.06), {−5,0,5} 0.697/0.229/0.075 — 각 ±1 %p, **max |lag| ≤ period + tick**, vruntime 퍼짐 상한, min_vruntime 역행 0.

### Part B — min_vruntime 과 깨어난 태스크 배치

- `update_min_vruntime` — `max(min_vruntime, min(curr.vr, leftmost.vr))`. 절대 뒤로 가지 않는다.
- `place_entity` — CFS 시절 규칙: `vr = max(vr, min_vruntime − latency/2)` (GENTLE_FAIR_SLEEPERS). `PLACE_NONE` 이면 그대로.
- `wakeup_preempt` — `curr.vr − woken.vr > calc_delta_fair(wakeup_gran, woken)` 이면 선점.
- 시나리오: A, B 는 처음부터 hog, C 는 1 초 자다가 hog. 배치 有: C 최장 연속 실행 3 ms, A 최장 대기 7 ms, 1~2 초 몫 1/3. 배치 無: C 501 ms 독점, A 505 ms 기아.

### Part C — 임베디드 리눅스: SCHED_FIFO, RT throttling, nice 의 한계

- `pick_next_task` — 클래스 순서: throttle 이 아니면 SCHED_FIFO(rt_prio 최대), 아니면 CFS.
- `rt_account` — `sched_rt_runtime_us / sched_rt_period_us` 회계. 폭주한 FIFO 태스크 옆에서 CFS 워치독(100 ms 마다 200 µs)이 무제한이면 0 번, 950/1000 ms 면 처리량은 되지만 **최대 950 ms 늦고**, 95/100 ms 면 95 ms 이하.
- 센서 태스크(10 ms 마다 1.5 ms) 를 nice 0 → nice 19 로 내리면: 몫 0.7 % 로 떨어져 **마감 초과(overrun) 14 번 / 14 주기** — nice 는 우선순위가 아니라 몫이다.

## 2. Problem (English)

> **Weighted fair scheduler simulator.** Single CPU, time in microseconds, deterministic. Implement:
>
> A. `void set_load_weight(load_weight_t*, int nice)` from Linux's `sched_prio_to_weight[40]` / `sched_prio_to_wmult[40]`; `uint64_t calc_delta_fair(uint64_t delta_ns, const load_weight_t*)` computing `delta * 1024 / weight` **without division** (multiply by the precomputed inverse, shift); a binary min-heap run queue keyed by vruntime (wrap-safe signed comparison, tie → lower id) tested on its own; `sched_period_us(nr)` = `sched_latency` if `nr <= sched_latency / min_granularity` else `nr * min_granularity`; `sched_slice_us(i)` = period × w_i / Σw; `update_curr()` and `check_preempt_tick()`. Two CPU hogs at nice {0,0} must get 50/50, {0,5} must get 1024:335 (±3 %), {−5,0,5} their weight fractions (±1 %p). For always-runnable tasks, the lag `|service_i(t) − t·w_i/W|` must stay ≤ period + tick.
>
> B. `update_min_vruntime()` (monotonic), `place_entity()` (CFS-era: `vruntime = max(vruntime, min_vruntime − latency/2)`), `wakeup_preempt()`. A task that slept 1 s must not run longer than latency/2 + period + 2 ticks after waking with placement, and must monopolize the CPU for > 400 ms without it.
>
> C. Scheduling classes: `pick_next_task()` (SCHED_FIFO before CFS unless RT is throttled), `rt_account()` implementing `sched_rt_runtime_us / sched_rt_period_us`. Show a CFS watchdog starving next to a runaway FIFO task, then surviving under 950/1000 ms throttling (but late by up to 950 ms), and the latency with a 95/100 ms period.
>
> Follow-ups: Why the inverse-weight table? Why `latency/2` and not 0 or `latency`? What did EEVDF (Linux 6.6) change — lag, eligibility, virtual deadline — and why does a short requested slice reduce latency but not increase share? Would you run a 1 kHz control loop at nice −20?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.25 초 만에 끝나고 exit 1 — 스텁 `pick_next_task` 가 −1 이라 아무것도 돌지 않을 뿐, 시뮬레이션 시간이 정해져 있어 멈추지 않는다):

```text
FAIL starter.c:411  lw.weight == 88761
FAIL starter.c:412  lw.weight == 15 && lw.inv_weight == 286331153u
...
FAIL starter.c:471  ops == 20000 && mism == 0
...
  nice {0,0}: share 0.0000(ideal 0.5000) 0.0000(ideal 0.5000)  max|lag| 1499500 us, spread 0.00 ms, switches 0
FAIL starter.c:497  run_hogs(n00, 2, c) && absd(share(&S, 0) - 0.5) <= 0.005
...
  sensor nice 19: bursts 0 overruns 0 max wake lat 0 us share 0.000
FAIL starter.c:610  S.t[0].overruns > 0 && share(&S, 0) < 0.15
14/50 passed
```

하네스 구조 (starter·solution 공통, TODO 가 아닌 부분):

| 하네스 함수 | 하는 일 |
|---|---|
| `sim_init` | 태스크 생성. `phase_us > 0` 이면 그 시각까지 잔다 |
| `simulate` | µs 루프: ① 끝난 버스트 → `dequeue_sleep` ② `release == now` → `enqueue(wakeup)` ③ tick 이면 `update_curr` + `check_preempt_tick` ④ `schedule()` ⑤ curr 1 µs 실행 + `rt_account` |
| `enqueue` | 깨어남이면 `update_curr` → `place_entity` → heap 에 넣기 → `wakeup_preempt` |
| `schedule` | prev(CFS) 를 `update_curr` 후 heap 에 되돌리고 `pick_next_task`. 연속 실행·대기·깨어남 지연 통계 |
| `observe_tick` | lag(이상적 GPS 대비), vruntime 퍼짐, min_vruntime 역행 횟수 |

curr 는 **heap 밖**에 있다 — 커널도 돌고 있는 엔티티를 트리에서 빼 두고(`set_next_entity`) 내려올 때 다시 넣는다(`put_prev_entity`). 그래서 `check_preempt_tick` 의 "leftmost" 는 heap 루트다.

관찰용 파일:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace && ./demo_trace     # 타임라인 · nice/tick 스윕
# Lima VM (실제 커널 교차 확인):
limactl shell elinux -- bash -lc 'mkdir -p /tmp/OS-A04_work && cp <lesson>/demo_linux_nice.c /tmp/OS-A04_work/ &&
  cd /tmp/OS-A04_work && gcc -std=c11 -Wall -Wextra -Werror -O2 demo_linux_nice.c -o d && ./d'
```

## 4. 힌트

<details><summary>힌트 1 — 표 인덱스와 inv_weight</summary>

`sched_prio_to_weight[nice + 20]`. 커널은 `static_prio − MAX_RT_PRIO` 로 인덱스를 만든다 (`NICE_TO_PRIO(n) = n + 120`, `MAX_RT_PRIO = 100` → nice + 20). inv_weight 는 같은 인덱스의 `sched_prio_to_wmult`. 검산: 1024 × 4194304 = 2^32.
</details>

<details><summary>힌트 2 — 나눗셈 없는 Δ×1024/w</summary>

`Δ × 1024 / w = Δ × (1024 × 2^32/w) / 2^32 = (Δ × 1024·inv) >> 32`. 그런데 1024·inv 는 32 비트를 넘는다 (nice 19 면 2^38 근처). 커널은 넘친 비트 수 `fs = fls(hi)` 만큼 곱하는 수를 오른쪽으로 밀고 시프트를 `32 − fs` 로 줄인다 — 정밀도를 조금 버리고 32×64 곱 한 번으로 끝낸다. 128 비트 곱 결과를 시프트하면 `mul_u64_u32_shr` 와 같다.
</details>

<details><summary>힌트 3 — heap 비교와 래핑</summary>

`(int64_t)(a − b) < 0` 로 비교한다. u64 vruntime 이 넘쳐도(연습 1) 두 값의 차가 2^63 보다 작으면 순서가 맞다. 동률은 번호로 깨야 결과가 결정적이다. pop 은 루트를 빼고 마지막 원소를 루트에 올린 뒤 더 작은 자식과 바꾸며 내려간다.
</details>

<details><summary>힌트 4 — check_preempt_tick 의 세 갈래</summary>

`ran = sum_exec − prev_sum_exec` (이번에 고른 뒤 실행한 시간). ① `ran > ideal` → 교체. ② `ran < min_gran` → 아직 너무 짧다, 유지. ③ leftmost 의 vr 보다 `ideal`(ns) 이상 앞섰으면 교체. tick 이 1 ms 라서 slice 3 ms 인 태스크는 실제로 4 ms 를 돈다 ("넘었는지" 를 tick 에서만 보니까) — 그게 lag 상한에 tick 이 들어가는 이유다.
</details>

<details><summary>힌트 5 — place_entity 와 wakeup_preempt</summary>

배치 기준은 `min_vruntime − latency/2`, 그리고 **max** — 이미 그보다 앞(큰 vr)에 있는 태스크(짧게 잔 태스크)를 뒤로 당겨 주지는 않는다. 빼기는 u64 래핑이 나도 비교를 부호 있게 하면 된다. wakeup 선점의 gran 은 **깨어난 태스크의** weight 로 환산한다 (nice 5 면 1 ms → 3.06 ms).
</details>

<details><summary>힌트 6 — RT 회계</summary>

FIFO 가 돈 µs 만 `rt_time++`. `rt_time ≥ runtime` 이면 throttled 로 바꾸고 `need_resched` — 다음 `schedule()` 에서 `pick_next_task` 가 FIFO 를 건너뛴다. `(now+1) % period == 0` 이면 `rt_time = 0` 이고 throttled 를 풀며 다시 `need_resched`. throttled 동안 FIFO 는 runnable 이지만 뽑히지 않는다.
</details>

## 5. 풀이

### Part A — 가중치 표: "nice 1 = 약 10 %"

Linux 6.18.55 `kernel/sched/core.c:10318-10340` (주석 + 표, 그대로):

```c
/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 * ... (to achieve that we use a multiplier of 1.25. ...)
 */
const int sched_prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 ...
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 ...
 /*  15 */        36,        29,        23,        18,        15,
};
```

두 nice 0 태스크 중 하나를 nice 1 로 바꾸면 1024/(1024+820) = 55.5 % vs 44.5 % — 약 10 %p 차이. **상대적**이라 어디서 시작해도 한 단계가 1.25 배다. `demo_trace` ② 의 실측 (hog 2 개, 3 초):

| k (0 vs k) | w(k) | 실측 몫 비 | w(0)/w(k) | 1.25^k |
|---|---|---|---|---|
| 1 | 820 | 1.25 | 1.25 | 1.25 |
| 5 | 335 | 3.05 | 3.06 | 3.05 |
| 10 | 110 | 9.31 | 9.31 | 9.31 |
| 19 | 15 | 67.18 | 68.27 | 69.39 |

nice 19 에서 1 %p 벌어지는 건 tick 단위 양자화다 (nice 19 는 한 번 뽑히면 slice 0.09 ms 지만 tick 이 1 ms 라 1 ms 를 돈다). nice −20 vs 19 는 88761/15 ≈ **5917 배**. SCHED_IDLE 은 weight 3 (`sched.h:2351 WEIGHT_IDLEPRIO`) 으로 nice 19 보다도 약하다.

64-bit 커널은 `se->load.weight` 를 `scale_load()` 로 2^10 배 키워 저장한다 (`sched.h:148` `NICE_0_LOAD_SHIFT = 20`, `:173 NICE_0_LOAD = 1L << 20`) — 그룹 가중치의 해상도를 위해서다. 계산 직전에 `scale_load_down` 으로 되돌리므로 이 레슨의 1024 기준과 결과가 같다.

### Part A — 나눗셈 없는 `calc_delta_fair`

`fair.c:251-288` (6.18.55, 일부):

```c
 * delta_exec * weight / lw.weight
 *   OR
 * (delta_exec * (weight * lw->inv_weight)) >> WMULT_SHIFT
static u64 __calc_delta(u64 delta_exec, unsigned long weight, struct load_weight *lw)
{
	u64 fact = scale_load_down(weight);
	u32 fact_hi = (u32)(fact >> 32);
	int shift = WMULT_SHIFT;
	...
	fact = mul_u32_u32(fact, lw->inv_weight);
	fact_hi = (u32)(fact >> 32);
	if (fact_hi) {
		fs = fls(fact_hi);
		shift -= fs;
		fact >>= fs;
	}
	return mul_u64_u32_shr(delta_exec, fact, shift);
}
static inline u64 calc_delta_fair(u64 delta, struct sched_entity *se)
{
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &se->load);
	return delta;
}
```

손 계산 (nice 5, Δ = 1 ms): `fact = 1024 × 12820798 = 13 128 497 152` → 상위 32 비트 = 3 → `fs = fls(3) = 2`, shift 30, `fact >>= 2 = 3 282 124 288` → `1 000 000 × 3 282 124 288 >> 30 = 3 056 716` (정확한 값 3 056 716.4). 테스트 `calc_delta_fair(1000000, nice 5) == 3056716` 이 이 숫자다. 이유: `update_curr` 는 문맥 전환·tick 마다 불리는 **가장 뜨거운 경로**고, 64 비트 나눗셈은 곱셈보다 수십 배 느리다 (32 비트 ARM 에는 64 비트 나눗셈 명령 자체가 없어 라이브러리 호출이다). nice 0 은 분기 하나로 끝난다.

`update_curr` 의 핵심 한 줄은 `fair.c:1314` `curr->vruntime += calc_delta_fair(delta_exec, curr);` 다. 커널은 ns 단위 `rq_clock_task` 차이를 쓰고, 이 시뮬레이터는 µs 로 쌓아 둔 `pending_us × 1000` 을 쓴다.

### Part A — 런큐: min-heap vs 커널의 RB 트리

| | min-heap (이 레슨) | RB 트리 (커널) |
|---|---|---|
| 최소 꺼내기 | O(log n) | O(1) — leftmost 캐시 (`rb_root_cached`) |
| 넣기 | O(log n) | O(log n) |
| 임의 엔티티 빼기 (잠듦·이주·nice 변경) | 위치 색인이 따로 필요 | 노드 포인터로 O(log n) |
| 정렬 순회 / 부분 트리 정보 | 불가 | 가능 — 6.18 은 **deadline 순 정렬 + 부분 트리 min_vruntime 증강** (`fair.c:1007-1024` 주석) |

단일 CPU 시뮬레이터에선 curr 만 잠들므로 heap 의 임의 삭제가 필요 없었다. 커널에선 다른 CPU 로 이주·cgroup 이동·nice 변경으로 트리 한가운데 엔티티를 빼야 하고, EEVDF 는 "eligible 중 가장 이른 deadline" 을 찾아야 해서 heap 으로는 안 된다 (Q10). 단독 테스트는 **동률을 일부러 많이** (vruntime 0~49) 만들어 tie-break 까지 전수 최솟값과 비교했다: 20 000 연산 불일치 0, 불변식 위반 0.

### Part A — period · slice · tick 선점 (CFS 시절 규칙)

6.5 까지의 CFS (아래 이름·기본값은 6.5 이전 `fair.c` 에 대한 기억이다 — 이 VM 에는 6.18 트리만 있어 원문을 대조하지 못했다): `sysctl_sched_latency` = 6 ms, `sysctl_sched_min_granularity` = 0.75 ms, `sysctl_sched_wakeup_granularity` = 1 ms, 모두 CPU 수 배율을 곱한다. `sched_nr_latency = 6/0.75 = 8`.

```text
period(nr) = nr ≤ 8 ? latency : nr × min_gran           ← 너무 잘게 쪼개지지 않게
slice_i    = period(nr) × w_i / Σw                        ← 한 period 안에서 weight 만큼
tick:  ran > slice_i            → 교체
       ran < min_gran            → 유지
       curr.vr − leftmost.vr > slice_i → 교체
```

`demo_trace` ④ (factor 1): nr 1~8 은 period 6 ms 고정이고 nice 0 slice 가 6 → 0.75 ms 로 줄다가, 9 개부터는 slice 0.75 ms 고정에 period 가 6.75, 7.5, … 로 늘어난다 — **지연 목표(latency)와 문맥 전환 비용의 타협**이다.

배율은 6.18 에도 그대로 남아 있다 (`fair.c:192`):

```c
static unsigned int get_update_sysctl_factor(void)
{
	unsigned int cpus = min_t(unsigned int, num_online_cpus(), 8);
	...
	case SCHED_TUNABLESCALING_LOG:
	default:
		factor = 1 + ilog2(cpus);
```

그리고 6.18 에 남은 튜너블은 하나다 (`fair.c:75-80`): `"Minimal preemption granularity for CPU-bound tasks: (default: 0.70 msec * (1 + ilog(ncpus)))"`, `sysctl_sched_base_slice = 700000ULL`. **sched_latency 와 min_granularity 는 6.18 소스에 없다** — `grep sysctl_sched_latency kernel/sched/fair.c` 결과 0 줄. Lima VM (6 CPU, 커널 7.0.0-38-generic) 에서 실제로 읽은 값:

```text
$ sudo cat /sys/kernel/debug/sched/base_slice_ns      → 2100000      (0.7 ms × (1 + ilog2(6)) = 2.1 ms)
$ sudo cat /sys/kernel/debug/sched/tunable_scaling    → 1            (LOG)
$ grep CONFIG_HZ= /boot/config-$(uname -r)            → CONFIG_HZ=1000
```

### Part A — 실측: 몫과 lag

`solution` 출력 (3 초, tick 1 ms):

| nice | 실측 몫 | 이상 (w/Σw) | max \|lag\| | 상한 period+tick | 문맥 전환 |
|---|---|---|---|---|---|
| {0,0} | 0.5000 / 0.5000 | 0.5 / 0.5 | 2000 µs | 7000 µs | 750 |
| {0,5} | 0.7533 / 0.2467 | 0.7535 / 0.2465 | 1233 µs | 7000 µs | 904 |
| {−5,0,5} | 0.6967 / 0.2287 / 0.0747 | 0.6967 / 0.2286 / 0.0748 | 2035 µs | 7000 µs | 952 |
| {0,0,0,0} | 0.25 × 4 | 0.25 × 4 | 1500 µs | 7000 µs | 1500 |

lag = "실제로 받은 CPU − 이상적 GPS(모두가 동시에 w/W 속도로 도는 유체 모델)가 줬을 CPU". CFS 는 한 번에 하나만 돌리니 0 일 수 없고, **한 period 안에 모두가 한 번씩 slice 를 받으며 tick 만큼 넘칠 수 있다**는 것이 상한의 근거다. {0,0} 은 3 ms slice 를 tick 때문에 4 ms 씩 돌아서 (750 번 전환 = 3 s / 4 ms) lag 이 정확히 2 ms 다. `demo_trace` ③ tick 스윕이 이걸 확인한다:

| tick | max \|lag\| | vr 퍼짐 | 전환 | nice −5 몫 |
|---|---|---|---|---|
| 4000 µs (HZ 250) | 5278 µs | 12.19 ms | 418 | 0.6960 |
| 1000 µs (HZ 1000) | 2035 µs | 3.05 ms | 952 | 0.6967 |
| 250 µs | 1343 µs | 1.53 ms | 1397 | 0.6968 |
| 100 µs | 1307 µs | 1.52 ms | 1436 | 0.6965 |

**장기 몫은 tick 과 무관**하고(vruntime 이 기억하므로), **단기 공정성(lag)과 전환 수가 tick 에 좌우**된다. 커널이 HZ 와 별개로 `hrtick` (고해상도 타이머로 slice 끝에 정확히 선점) 을 둘 수 있는 이유다 (기본 꺼짐 — `features.h:66 SCHED_FEAT(HRTICK, false)`).

### Part B — min_vruntime: 단조 증가하는 "현재 시각"

CFS 시절 `update_min_vruntime` 의 의미 (6.18 `Documentation/scheduler/sched-design-CFS.rst:65` 에 아직 설명이 남아 있다: *"rq->cfs.min_vruntime value, which is a monotonic increasing value tracking the smallest vruntime among all tasks in the runqueue"*):

```text
v = min(curr.vr (curr 가 runnable CFS 면), leftmost.vr)
min_vruntime = max(min_vruntime, v)          ← 절대 뒤로 안 감
```

왜 max 인가: 작은 vr 를 가진 태스크가 들어올 때마다 기준점이 뒤로 가면, 그 기준으로 배치되는 다음 태스크들도 줄줄이 뒤로 가서 기존 태스크를 굶긴다. 손 테스트 4 개: curr 없음 → leftmost(40), curr 45 / left 50 → 45, curr 70 / left 50 → 50, 강제로 60 을 넣으면 60 유지. 시뮬레이션 전체에서 역행 0 회.

### Part B — 깨어난 태스크 배치: 실측 501 ms 독점

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">t = 1 s: A, B 는 vr ≈ 500 ms. 1 초 잔 C 는 vr = 0 그대로</text>
  <line x1="40" y1="110" x2="720" y2="110" stroke="currentColor"/>
  <text x="600" y="132" fill="currentColor">vruntime (ms) →</text>
  <line x1="60" y1="102" x2="60" y2="118" stroke="currentColor"/><text x="54" y="96" fill="currentColor">0</text>
  <line x1="640" y1="102" x2="640" y2="118" stroke="currentColor"/><text x="626" y="96" fill="currentColor">500</text>
  <circle cx="60" cy="110" r="9" class="f-warn" stroke="currentColor"/><text x="56" y="114" fill="currentColor">C</text>
  <circle cx="644" cy="110" r="9" class="f-ok" stroke="currentColor"/><text x="640" y="114" fill="currentColor">A</text>
  <circle cx="668" cy="110" r="9" class="f-ok" stroke="currentColor"/><text x="664" y="114" fill="currentColor">B</text>
  <circle cx="620" cy="110" r="9" class="f-acc" stroke="currentColor"/><text x="615" y="114" fill="currentColor">C'</text>
  <path d="M60 140 L636 140" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" fill="none"/>
  <path d="M628 135 L638 140 L628 145" stroke="currentColor" fill="none"/>
  <text x="120" y="160" fill="currentColor">PLACE_NONE: C 가 vr 500 에 닿을 때까지 혼자 돈다 → 실측 501 ms 연속 실행, A 505 ms 기아</text>
  <path d="M620 70 L620 98" stroke="currentColor"/>
  <text x="430" y="62" fill="currentColor">PLACE_CFS: C' = max(0, min_vr − 3 ms)</text>
  <rect x="40" y="180" width="680" height="58" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="52" y="200" fill="currentColor">왜 latency/2 를 빼 주나: 깨어난 대화형 태스크가 "조금 먼저" → 즉시 선점 (실측 지연 0).</text>
  <text x="52" y="220" fill="currentColor">왜 그 이상은 안 주나: 잔 시간만큼 다 돌려주면 위의 독점이 된다. 공정성 = "같이 runnable 일 때" 만 따진다.</text>
</svg>
```

CFS 시절 `place_entity` (6.5 이전 기억, 원문 미대조 — 요지): 새 태스크(`initial`)는 `START_DEBIT` 으로 `min_vruntime + vslice` (한 slice 빚지고 시작 — fork 폭탄이 이득을 못 보게), 깨어난 태스크는 `thresh = sysctl_sched_latency`, `GENTLE_FAIR_SLEEPERS` 면 `thresh >>= 1`, `vruntime = min_vruntime − thresh`, 마지막에 `se->vruntime = max_vruntime(se->vruntime, vruntime)`. 이 레슨의 `place_entity` 가 깨어남 갈래를 그대로 옮긴 것이다 (새 태스크는 t = 0 에 모두 vr0 로 시작하므로 START_DEBIT 은 생략).

`demo_trace` ① 실측 (1 글자 = 1 ms, t = 995~1065 ms):

```text
  CFS   |ABBBBCCCAAABBBCCCAAABBBCCCAAABBBCCCAAABBBCCCAAABBBCCCAAABBBCCCAAABBBCC|  C max run 3.0 ms, A max wait 7.0 ms
  NONE  |ABBBBCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC|  C max run 501.0 ms, A max wait 505.0 ms
   1 글자 = 10 ms, t = 0.95 .. 1.65 s   (칸의 90% 이상 돈 태스크, 섞이면 *)
  CFS   |**********************************************************************|
  NONE  |*****CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC***************|
```

배치 有: C 는 깨어나자마자 선점(`wakeup_preempt`: curr.vr − C'.vr = 3 ms > gran 1 ms)하고, 3 ms 돈 뒤 A·B 와 3 ms 씩(slice 2 ms + tick) 돌아가며 1~2 초 몫 0.334. 배치 無: 1.0~1.5 초 C 독점, 1~2 초 몫 0.666. 테스트 상한 = `latency/2 + period(3) + 2·tick = 11 ms` 를 C 최장 실행과 A 최장 대기가 모두 지킨다.

`wakeup_preempt` 의 gran 은 **깨어난 쪽의 weight 로** 환산한다 — 손 테스트: nice 5 가 깨어나면 gran = 1 ms × 1024/335 = 3.057 ms, vdiff 3.0 ms 는 선점 안 함, 3.1 ms 는 선점. nice 0 은 vdiff == gran(1 ms) 이면 선점 안 함 (`>` 이지 `≥` 가 아니다).

### Part B — 6.18 은 어떻게 다른가: lag 보존 배치

6.18.55 `fair.c:5313 place_entity` 에는 `min_vruntime` 도 `latency/2` 도 없다:

```c
	u64 vslice, vruntime = avg_vruntime(cfs_rq);          /* 가중 평균 V */
	...
	if (sched_feat(PLACE_LAG) && cfs_rq->nr_queued && se->vlag) {
		lag = se->vlag;                                    /* 잠들 때 저장한 V − v */
		...
		lag *= load + scale_load_down(se->load.weight);    /* (W + w_i) / W 배로 부풀림 */
		...
		lag = div_s64(lag, load);
	}
	se->vruntime = vruntime - lag;                         /* fair.c:5398 */
	...
	if (sched_feat(PLACE_DEADLINE_INITIAL) && (flags & ENQUEUE_INITIAL))
		vslice /= 2;                                       /* 새 태스크는 반 slice 로 시작 */
	se->deadline = se->vruntime + vslice;                  /* fair.c:5417 */
```

즉 "잠들기 전에 받을 게 남았던 만큼(양의 lag) 은 돌려주고, 너무 많이 받았던 만큼(음의 lag) 은 빚으로 들고 온다". 저장되는 lag 는 `entity_lag` (`fair.c:767`) 에서 `±calc_delta_fair(max_slice + TICK_NSEC)` 로 잘린다 — 오래 자도 독점은 한 slice 남짓이 상한이다. 잠깐 자서 빚을 털어 버리는 게이밍을 막는 것이 CFS 의 `max(vr, min_vr − latency/2)` 와 다른 점이다 (CFS 도 max 라서 빚은 남지만, 오래 잔 태스크의 크레딧이 "latency/2" 고정 vs "실제 lag, 상한 있음" 으로 다르다). 부풀림 식이 왜 필요한지는 연습 5 에서 2000 케이스로 확인한다. 6.18 `features.h` 의 기본값과 VM 의 실제 `features` (sudo 로 debugfs 읽음):

```text
SCHED_FEAT(PLACE_LAG, true)               features.h:7
SCHED_FEAT(PLACE_DEADLINE_INITIAL, true)  features.h:11
SCHED_FEAT(RUN_TO_PARITY, true)           features.h:20
SCHED_FEAT(PREEMPT_SHORT, true)           features.h:25
VM /sys/kernel/debug/sched/features → PLACE_LAG PLACE_DEADLINE_INITIAL PLACE_REL_DEADLINE RUN_TO_PARITY
                                      PREEMPT_SHORT NO_NEXT_BUDDY PICK_BUDDY DELAY_DEQUEUE DELAY_ZERO ...
```

### EEVDF — 6.6 부터 바뀐 pick

`Documentation/scheduler/sched-eevdf.rst` (6.18): *"The Linux kernel began transitioning to EEVDF in version 6.6 ... EEVDF picks tasks with lag greater or equal to zero and calculates a virtual deadline (VD) for each, selecting the task with the earliest VD to execute next."* 원 알고리즘은 Stoica & Abdel-Wahab 의 1995 기술 보고서 (제목 "Earliest Eligible Virtual Deadline First: A Flexible and Accurate Mechanism for Proportional Share Resource Allocation" 으로 기억 — 정확한 보고서 번호 미확인).

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">EEVDF pick: ① eligible(v ≤ V) 만 후보 ② 그중 가상 마감 d = v + slice·1024/w 가 가장 이른 것</text>
  <line x1="40" y1="200" x2="720" y2="200" stroke="currentColor"/>
  <text x="620" y="222" fill="currentColor">vruntime →</text>
  <rect x="40" y="40" width="340" height="150" class="f-ok" stroke="none"/>
  <text x="50" y="58" fill="currentColor">eligible: lag = w(V − v) ≥ 0 (받을 게 남음)</text>
  <line x1="380" y1="34" x2="380" y2="208" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="352" y="248" fill="currentColor">V = Σwᵢvᵢ / Σwᵢ (avg_vruntime)</text>
  <circle cx="120" cy="90" r="7" class="f-acc" stroke="currentColor"/>
  <line x1="120" y1="90" x2="470" y2="90" stroke="currentColor" stroke-width="2"/>
  <text x="128" y="82" fill="currentColor">hog A: v 작음, slice 2.8 ms → d 멀다</text>
  <line x1="470" y1="82" x2="470" y2="98" stroke="currentColor"/>
  <circle cx="250" cy="135" r="7" class="f-acc" stroke="currentColor"/>
  <line x1="250" y1="135" x2="310" y2="135" stroke="currentColor" stroke-width="3"/>
  <line x1="310" y1="127" x2="310" y2="143" stroke="currentColor"/>
  <text x="258" y="127" fill="currentColor">S: slice 0.5 ms → d 가깝다 ✔ pick</text>
  <circle cx="470" cy="170" r="7" class="f-warn" stroke="currentColor"/>
  <line x1="470" y1="170" x2="560" y2="170" stroke="currentColor" stroke-width="2"/>
  <line x1="560" y1="162" x2="560" y2="178" stroke="currentColor"/>
  <text x="480" y="160" fill="currentColor">B: 이미 많이 받음 → 마감이 일러도 제외</text>
  <rect x="400" y="40" width="320" height="56" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="410" y="60" fill="currentColor">몫(장기)은 weight 가 정하고,</text>
  <text x="410" y="80" fill="currentColor">지연(얼마나 자주 뽑히나)은 slice 가 정한다</text>
</svg>
```

6.18.55 `fair.c:1025 pick_eevdf` 요지:

```c
	if (cfs_rq->nr_queued == 1)
		return curr && curr->on_rq ? curr : se;
	...
	if (curr && (!curr->on_rq || !entity_eligible(cfs_rq, curr)))
		curr = NULL;
	if (curr && protect && protect_slice(curr))
		return curr;                                   /* RUN_TO_PARITY 보호 구간 */
	/* Pick the leftmost entity if it's eligible */
	if (se && entity_eligible(cfs_rq, se)) { best = se; goto found; }
	/* Heap search for the EEVD entity */
	while (node) {
		struct rb_node *left = node->rb_left;
		if (left && vruntime_eligible(cfs_rq, __node_2_se(left)->min_vruntime)) {
			node = left; continue;                     /* 왼쪽(더 이른 deadline)에 eligible 이 있다 */
		}
		se = __node_2_se(node);
		if (entity_eligible(cfs_rq, se)) { best = se; break; }
		node = node->rb_right;
	}
```

트리는 **deadline 순** 정렬이고 각 노드가 부분 트리의 최소 vruntime 을 들고 있어서(`se->min_vruntime`, 증강 RB 트리) "왼쪽 부분 트리에 eligible 이 하나라도 있나" 를 O(1) 에 판정한다 → O(log n) pick. 나머지 조각:

| 6.18 코드 | 의미 | 연습 |
|---|---|---|
| `avg_vruntime()` `fair.c:715` | V = 가중 평균. `zero_vruntime` 기준 상대값 `sum_w_vruntime = Σ(vᵢ − v0)·wᵢ` 로 넘침 방지, 음수 floor 보정 | 연습 4 |
| `vruntime_eligible()` `fair.c:802` | `avg ≥ (v − v0)·load` — 나눗셈 없는 정수 판정 (주석: "avg_vruntime() > se->vruntime is inaccurate") | 연습 4 |
| `update_deadline()` `fair.c:1127` | v ≥ d 면 요청을 다 받음 → `d = v + calc_delta_fair(slice)`, 재스케줄 | 연습 3 |
| `set_protect_slice()` `fair.c:973` | RUN_TO_PARITY: 큐의 **최소 slice** 만큼은 curr 를 보호 | 연습 3 |
| `__setparam_fair()` `fair.c:5296` | `sched_setattr(sched_runtime)` 로 slice 를 0.1~100 ms 로 요청 (`custom_slice`) | 연습 3 |

연습 3 실측: hog 2 개 + 태스크 S. S 의 최대 대기 — CFS 6.0 ms, EEVDF(S slice 0.5 ms) **2.0 ms**, EEVDF(S 기본 2.8 ms) 6.0 ms. 몫은 셋 다 0.333. 대가: S 가 큐에 있으면 RUN_TO_PARITY 보호가 0.5 ms 로 줄어 **모든 태스크가 매 tick 교체** — 전환 1001 → 3001.

### Part C — SCHED_FIFO 폭주와 RT throttling

클래스 순서는 stop > deadline > rt(FIFO/RR) > fair(CFS/EEVDF) > idle 이고, 높은 클래스가 runnable 이면 낮은 클래스는 몫과 상관없이 못 돈다. 폭주한 FIFO 태스크(무한 루프 버그) 하나면 CFS 의 워치독 데몬·sshd 가 전부 굶는다. 안전판이 RT throttling 이다 — `kernel/sched/rt.c:18` `int sysctl_sched_rt_period = 1000000;`, `:24` `int sysctl_sched_rt_runtime = 950000;` ("part of the period that we allow rt tasks to run in us. default: 0.95s"). VM 실제값:

```text
$ cat /proc/sys/kernel/sched_rt_runtime_us /proc/sys/kernel/sched_rt_period_us
950000
1000000
$ sudo cat /sys/kernel/debug/sched/fair_server/cpu0/runtime /sys/kernel/debug/sched/fair_server/cpu0/period
50000000
1000000000
```

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">폭주 FIFO + RT throttling: 몫은 5 % 지키지만 워치독은 최대 950 ms 늦는다</text>
  <text x="10" y="56" fill="currentColor">950/1000</text>
  <rect x="80" y="40" width="190" height="24" class="f-warn" stroke="currentColor"/>
  <rect x="270" y="40" width="10" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="280" y="40" width="190" height="24" class="f-warn" stroke="currentColor"/>
  <rect x="470" y="40" width="10" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="480" y="40" width="190" height="24" class="f-warn" stroke="currentColor"/>
  <rect x="670" y="40" width="10" height="24" class="f-ok" stroke="currentColor"/>
  <text x="140" y="57" fill="currentColor">FIFO 950 ms</text>
  <g stroke="currentColor"><line x1="80" y1="70" x2="80" y2="80"/><line x1="100" y1="70" x2="100" y2="80"/><line x1="120" y1="70" x2="120" y2="80"/><line x1="140" y1="70" x2="140" y2="80"/><line x1="160" y1="70" x2="160" y2="80"/><line x1="180" y1="70" x2="180" y2="80"/><line x1="200" y1="70" x2="200" y2="80"/><line x1="220" y1="70" x2="220" y2="80"/><line x1="240" y1="70" x2="240" y2="80"/><line x1="260" y1="70" x2="260" y2="80"/></g>
  <text x="80" y="96" fill="currentColor">워치독 깨어남 100 ms 마다 → 전부 950~1000 ms 창(초록)에서 몰아서 실행: 30 버스트, overrun 27</text>
  <text x="10" y="136" fill="currentColor">95/100</text>
  <g class="f-warn" stroke="currentColor">
    <rect x="80" y="120" width="19" height="24"/><rect x="100" y="120" width="19" height="24"/><rect x="120" y="120" width="19" height="24"/>
    <rect x="140" y="120" width="19" height="24"/><rect x="160" y="120" width="19" height="24"/><rect x="180" y="120" width="19" height="24"/>
    <rect x="200" y="120" width="19" height="24"/><rect x="220" y="120" width="19" height="24"/><rect x="240" y="120" width="19" height="24"/>
    <rect x="260" y="120" width="19" height="24"/>
  </g>
  <text x="290" y="137" fill="currentColor">… 같은 95 %, 창이 100 ms 마다 → 최대 지연 95 ms</text>
  <text x="10" y="180" fill="currentColor">교훈: "평균 몫" ≠ "최악 지연". 워치독 마감보다 RT 주기를 짧게, 또는 RT 태스크에 예산을.</text>
</svg>
```

실측 (3 초):

| 설정 | FIFO 몫 | 워치독 버스트 (필요 30) | overrun | 워치독 최대 깨어남 지연 |
|---|---|---|---|---|
| 무제한 (`sched_rt_runtime_us = -1`) | 1.000 | **0** | — | ∞ (로거도 0 µs) |
| 950 / 1000 ms (기본) | 0.950 | 30 | 27 | **950 000 µs** |
| 95 / 100 ms | 0.950 | 30 | 0 | 95 000 µs |

임베디드 보드 설계로 옮기면: 하드웨어 워치독 타임아웃이 1 초인데 펫(pet)을 CFS 데몬이 한다면 기본 throttling 으로는 **간신히 아슬아슬**하다 (950 ms + 데몬 자체 지연). 선택지: (1) 펫 데몬을 FIFO 의 더 높은 우선순위로, (2) rt_period 를 줄이기, (3) 폭주 가능 RT 태스크를 SCHED_DEADLINE(예산 강제)로, (4) 펫을 "모든 핵심 태스크가 살아 있음" 을 확인한 뒤에만 (systemd `WatchdogSec` + `sd_notify` 연쇄는 ELX-B13). 6.18 에는 CFS 쪽 보장으로 **fair_server**(deadline 서버, `deadline.c:1825` `runtime = 50 * NSEC_PER_MSEC`, `period = 1000 * NSEC_PER_MSEC`) 도 있다 — RT 가 CPU 를 쥐고 있어도 CFS 에 1 초당 50 ms 를 deadline 클래스로 준다. throttling 과 fair_server 의 정확한 상호작용(어느 쪽이 먼저 걸리는지)은 소스만 읽고 실험하지 않았다.

### Part C — nice 는 몫이다: 센서 태스크를 nice 19 로

| 센서 (10 ms 마다 1.5 ms = 15 %) | 몫 | 버스트 | overrun | 해석 |
|---|---|---|---|---|
| nice 0 + hog nice 0,0,10 | 0.150 | 300 / 300 | 0 | 공정 몫 1024/3182 = 32 % ≥ 15 % → 여유 |
| nice 19 + 같은 hog | 0.007 | 14 | 14 | 공정 몫 15/2173 = 0.7 % < 15 % → 계속 밀림 |

nice 를 **낮춰도**(−20) 지연 보장은 안 생긴다 — 몫이 커질 뿐 깨어났을 때 바로 돈다는 보장은 wakeup 선점·배치 휴리스틱에 달려 있다. 1 kHz 제어 루프처럼 마감이 있으면 SCHED_FIFO/SCHED_DEADLINE + `mlockall` + CPU 격리(`isolcpus`/cpuset) + PREEMPT_RT 커널 (측정은 ELX-A04 cyclictest — 다음 단계) 이고, RTOS 쪽 분석은 RTOS-M11(RM 응답 시간), RTOS-M12(EDF) 다. Cortex-M 의 FreeRTOS 는 고정 우선순위 + 같은 우선순위 time slicing 이라 "nice" 개념 자체가 없다.

### 실제 커널 교차 확인 (`demo_linux_nice.c`, Lima VM)

<!-- code: demo_linux_nice.c -->

Lima VM `elinux` (aarch64, 6 vCPU, `uname -r` = 7.0.0-38-generic, gcc 15.2.0, VM load 3.3, 호스트 load 8.9) — 프로세스들을 CPU 5 하나에 묶고 nice 를 올린 뒤 3 초 동안 돌린 실제 출력 (두 번째 실행, 첫 실행도 몫은 같은 값):

```text
pinned to CPU 5
nice {0,0}  cpu ms: 1500 1500  share: 0.500(ideal 0.500) 0.500(ideal 0.500)  (sum 3000 ms of 3000 wall)
nice {0,5}  cpu ms: 2260 740  share: 0.753(ideal 0.753) 0.247(ideal 0.247)  (sum 3000 ms of 3000 wall)
nice {0,5,10}  cpu ms: 2091 684 225  share: 0.697(ideal 0.697) 0.228(ideal 0.228) 0.075(ideal 0.075)  (sum 3000 ms of 3000 wall)
baseline 3 hogs (1..3 s): max gap 10.06 / 10.32 / 18.32 ms, gaps>10ms 1 / 2 / 1 of 244 / 242 / 241
sleeper: after C wakes (2..4 s): C cpu 669 ms, A cpu 666 ms, B cpu 664 ms
         C max continuous run 3.88 ms, A max gap 25.47 ms, B max gap 31.16 ms, gaps>10ms A 9 B 12 C 14
```

- 몫은 시뮬레이터와 소수 셋째 자리까지 같다 (EEVDF 도 장기 몫은 weight 가 정한다).
- 2 초 잔 C 는 깨어난 뒤 **최장 연속 실행 3~3.9 ms** (base_slice 2.1 ms + tick 반올림) 이고 2 초 동안 세 태스크가 666~669 ms 씩 — 독점 없음.
- A·B 의 최장 끊김 10~90 ms 는 같은 측정을 잠자는 태스크 없이 돌린 기준선에서도 10~28 ms 가 나와서 **VM 잡음**(호스트가 vCPU 를 못 돌려 준 시간, 같은 CPU 의 다른 프로세스)으로 본다. 이 숫자로 스케줄러 지연을 주장하지 않는다 — 제대로 재려면 `sched_switch` 트레이스 (LPERF-M05 runqlat — 다음 단계).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san
```

실제 출력 (Apple M6, Apple clang 21.0.0, load 12.9 — 시뮬레이터라 부하와 무관하게 같은 값):

```text
  nice {0,0}: share 0.5000(ideal 0.5000) 0.5000(ideal 0.5000)  max|lag| 2000 us, spread 4.00 ms, switches 750
  nice {0,5}: share 0.7533(ideal 0.7535) 0.2467(ideal 0.2465)  max|lag| 1233 us, spread 5.00 ms, switches 904
  nice {-5,0,5}: share 0.6967(ideal 0.6967) 0.2287(ideal 0.2286) 0.0747(ideal 0.0748)  max|lag| 2035 us, spread 3.05 ms, switches 952
  nice {0,0,0,0}: share 0.2500(ideal 0.2500) 0.2500(ideal 0.2500) 0.2500(ideal 0.2500) 0.2500(ideal 0.2500)  max|lag| 1500 us, spread 2.00 ms, switches 1500
  place=CFS : C max run 3000 us, A max wait 7000 us, C wake lat 0 us, C share(1-2s) 0.334
  place=NONE: C max run 501000 us, A max wait 505000 us, C share(1-2s) 0.666
  RT unlimited : fifo share 1.000, watchdog bursts 0
  RT 950/1000ms: fifo share 0.950, watchdog bursts 30 overruns 27 max wake lat 950000 us
  RT 95/100ms  : fifo share 0.950, watchdog bursts 30 max wake lat 95000 us
  sensor nice 0 : bursts 300 overruns 0 max wake lat 0 us share 0.150
  sensor nice 19: bursts 14 overruns 14 max wake lat 0 us share 0.007
50/50 passed
```

5 회 연속 `50/50 passed` (결정적이라 매번 같은 출력), 다른 cwd(`/tmp`)에서도 exit 0, 0.29 초. ASan+UBSan: `50/50 passed`, 보고 없음. 스레드를 쓰지 않아 TSan 은 해당 없음.

## 7. 자주 하는 실수

1. **vruntime 을 `a < b` 로 비교** — u64 가 넘치면 넘친 쪽이 "가장 작은 값" 이 되어 독점한다 (연습 1: 넘친 태스크 몫 1.000). → `(int64_t)(a − b) < 0`. 커널도 `vruntime_cmp`/`entity_before` 에서 부호 있는 차이를 쓴다.
2. **weight 를 곱하기** — `vr += Δ × w / 1024` 로 쓰면 nice −5 가 오히려 빨리 늘어 몫이 뒤집힌다. 무거운 태스크일수록 vruntime 이 **천천히** 올라야 한다. → `Δ × 1024 / w`.
3. **매 µs 마다 `calc_delta_fair(1000 ns)`** — 고정소수점 내림이 매번 쌓인다 (nice 5: 3056.7 → 3056, 0.02 % 체계 오차). → 실행 시간을 모아 두었다가 결정 시점(tick, 전환, 깨어남)에 한 번에 반영 (`pending_us`). 커널도 `update_curr` 를 이벤트마다만 부른다.
4. **min_vruntime 을 그냥 min 으로** — 작은 vr 가 들어올 때마다 기준이 뒤로 가 다음 배치까지 끌려간다. → `max(min_vruntime, …)`, 단조.
5. **깨어난 태스크의 vr 를 그냥 min_vruntime 으로 덮어쓰기** — 짧게 잔 태스크의 빚(큰 vr)까지 탕감된다 → 1 µs 자고 깨는 게이밍. → `max(자기 vr, min_vr − thresh)`.
6. **wakeup gran 을 curr 의 weight 로 환산** — 커널은 깨어난 엔티티 기준(`wakeup_gran(se)`)이다. nice 19 가 깨어날 때 gran 이 68 ms 처럼 커져서 사실상 선점 못 하는 게 의도다.
7. **"nice −20 이면 실시간"** — 몫이 커질 뿐 지연 보장이 아니다 (Part C 표). 마감이 있으면 FIFO/DEADLINE + throttling 이해 + PREEMPT_RT.
8. **RT throttling 을 "끄면 성능↑"** — `sched_rt_runtime_us = -1` 은 폭주 RT 하나로 시스템 전체(sshd, 워치독 데몬)를 잃는 설정이다 (실측 CFS 0 µs). 끄려면 RT 태스크 예산을 다른 방법으로 보장해야 한다.
9. **lag 상한에 tick 을 빼먹기** — 커널(HZ 기반 선점)은 slice 를 tick 만큼 넘긴다. {0,0} 을 3 ms 씩 정확히 번갈아 돌리면 lag 최대는 slice/2 = 1.5 ms 지만, 실측은 2.0 ms 다 — slice 3 ms 가 tick(1 ms) 에서야 "넘었다" 로 잡혀 4 ms 씩 돌기 때문. 상한을 `slice/2` 로 잡은 테스트는 바로 실패한다. → `period + tick`.

## 8. 면접 Q&A

<details>
<summary>Q1. vruntime 이 뭐고, nice 가 어떻게 CPU 비율로 바뀌나요? nice 0 과 5 는 몇 대 몇?</summary>

weight 로 나눈 실행 시간이다: `vr += Δ × 1024 / w(nice)`. 스케줄러는 항상 vr 최소를 고르므로 장기적으로 모든 runnable 태스크의 vr 가 같은 속도로 오르고, 실제 시간은 weight 비율로 나뉜다. weight 표는 nice 한 단계마다 ×1.25 (주석: "gentle 10% change") — nice 0 = 1024, 5 = 335 → 1024:335 = 75.4 % : 24.6 %. 시뮬레이터 0.7533/0.2467, VM 실제 커널 0.753/0.247. 꼬리: "nice −20 vs 19 는?" → 88761/15 ≈ 5917 배. "SCHED_IDLE 은?" → weight 3. "왜 곱셈 규칙인가?" → 덧셈 규칙(옛 O(1) 스케줄러의 nice 별 timeslice)은 nice 차이의 효과가 절대 위치에 따라 달랐다; 곱셈이면 "한 단계 = 항상 10 %" 다.
</details>

<details>
<summary>Q2. calc_delta_fair 는 왜 나눗셈 대신 역가중치 표를 쓰죠? 정밀도는요?</summary>

`update_curr` 는 매 tick·전환·깨어남마다 불리는 뜨거운 경로라 64 비트 나눗셈을 피한다 (32 비트 ARM 은 64 비트 나눗셈이 라이브러리 호출). `2^32/w` 를 미리 계산해 두고 `Δ × 1024 × inv >> 32` 로 바꾸는데, `1024 × inv` 가 32 비트를 넘으면 넘친 비트 수 `fs` 만큼 곱수를 밀고 시프트를 줄여 32×64 곱 하나로 끝낸다 (`fair.c:260-288`). 정밀도: 40 개 nice × 6 개 Δ 에서 정확한 값과 1 ns + 2 ppm 이내, nice 5 1 ms → 3 056 716 (정확 3 056 716.4). 꼬리: "nice 0 은?" → `weight == NICE_0_LOAD` 분기로 곱셈조차 안 한다. "64-bit 커널의 1<<20 은?" → 그룹 가중치(예: nice 19 autogroup 안의 태스크) 해상도를 위해 저장만 2^10 배, 계산 직전 `scale_load_down`.
</details>

<details>
<summary>Q3. 깨어난 태스크의 vruntime 을 왜 보정하나요? 안 하면? 너무 많이 하면?</summary>

잔 동안 다른 태스크의 vr 는 올랐는데 잔 태스크는 그대로라, 그대로 넣으면 **차이를 따라잡을 때까지 독점**한다 — 실측 1 초 잔 C 가 501 ms 연속 실행, A 505 ms 기아. CFS 는 `max(자기 vr, min_vruntime − latency/2)` 로 끌어올린다: 오래 잔 태스크는 "조금 먼저(3 ms)" 만 받아 깨어나자마자 선점하고(대화형 반응성) 독점은 못 한다 (실측 3 ms). `max` 라서 짧게 잔 태스크의 빚은 그대로 남는다. 너무 많이 주면(latency 전체 이상) 주기적으로 잠드는 태스크가 공정 몫 이상을 가져갈 수 있고, 0 이면 대화형 태스크가 깨어나도 curr 의 slice 가 끝날 때까지 기다린다. 6.18 은 이 휴리스틱을 버리고 **잠들기 전 lag 를 보존** (`v = V − vlag`, ±(max_slice + tick) 로 clamp) 한다 — 원칙적이고, 게이밍에 강하다.
</details>

<details>
<summary>Q4. min_vruntime 은 왜 단조 증가여야 하나요? 6.18 에선 무엇이 대신하나요?</summary>

새 태스크와 깨어난 태스크를 놓는 **기준 시각**이라서다. 작은 vr 하나가 들어올 때마다 기준이 뒤로 가면 다음 배치도 뒤로 가서 기존 태스크를 굶긴다; 단조면 "런큐의 현재 가상 시각" 이 앞으로만 간다. 구현은 `max(min_vruntime, min(curr, leftmost))`. 6.18.55 에는 `cfs_rq->min_vruntime` 이 없고 (`se->min_vruntime` 은 이제 RB 트리 증강 필드), 기준은 **가중 평균 V = avg_vruntime()** 이며 넘침을 피하려고 `zero_vruntime` 기준 상대값 `sum_w_vruntime` 을 들고 다닌다 (`fair.c:615-748`). 평균은 단조가 아니다 — lag 를 가진 태스크가 들어오고 나갈 때 V 가 앞뒤로 움직이고, 그래서 place_entity 가 lag 를 부풀려 넣는다 (연습 5). 꼬리: "평균이면 나눗셈 오차는?" → eligibility 는 나누지 않고 `avg ≥ key × load` 정수 비교 (연습 4 전수 일치).
</details>

<details>
<summary>Q5. sched_latency 와 min_granularity 는 무엇을 맞바꾸나요? 왜 CPU 수의 로그로 키우죠?</summary>

latency = "runnable 태스크가 모두 한 번씩 도는 목표 주기" (짧을수록 반응성), min_gran = "한 번 뽑히면 최소 이만큼은 돈다" (길수록 캐시·TLB 손실과 전환 비용↓). 태스크가 latency/min_gran(= 8) 개를 넘으면 period 를 `nr × min_gran` 으로 늘려 slice 가 0.75 ms 아래로 내려가지 않게 한다 — 반응성보다 처리량을 지킨다. 로그 배율(`1 + ilog2(min(ncpu, 8))`, `fair.c:192`)의 근거는 소스 주석 그대로 "CPU 가 많으면 사용자가 체감하는 지연이 줄지만 선형은 아니다" 라는 경험칙이다. 6.18 에는 이 둘이 없고 `base_slice` (0.7 ms × 배율, VM 6 CPU 실측 2.1 ms) 하나가 "요청 크기" 로 남았다. 꼬리: "HZ 는?" → tick 선점이면 slice 를 tick 까지 넘긴다: HZ 250 에서 lag 5.3 ms, HZ 1000 에서 2.0 ms (demo_trace ③).
</details>

<details>
<summary>Q6. EEVDF 는 CFS 의 무엇을 고쳤나요? 짧은 slice 를 요청하면 CPU 를 더 받나요?</summary>

CFS 는 "vr 최소" 하나로 몫과 지연을 같이 정해서, 지연에 민감한 태스크를 위해 sleeper 보너스·wakeup gran·buddy 같은 휴리스틱이 쌓였다. EEVDF 는 둘을 분리한다: **eligible**(lag ≥ 0, 즉 v ≤ V — 받을 게 남은 태스크) 만 후보로 하고 그중 **가상 마감 d = v + slice·1024/w** 가 가장 이른 것을 고른다. 몫(장기)은 여전히 weight 가 정하고, slice(요청 크기)는 "얼마나 자주, 얼마나 짧게 뽑히나" 만 바꾼다. 연습 3 실측: 짧은 slice(0.5 ms) 태스크의 최대 대기 6 → 2 ms, 몫은 0.333 그대로. 대가는 전환 수 1001 → 3001 (RUN_TO_PARITY 보호가 큐의 최소 slice 로 줄어 모두가 매 tick 교체). 요청은 `sched_setattr()` 의 `sched_runtime` 으로 0.1~100 ms (`fair.c:5296`). 원 논문: Stoica & Abdel-Wahab 1995 (기술 보고서, 제목 기억 기준).
</details>

<details>
<summary>Q7. 임베디드 리눅스 보드에서 1 kHz 모터 제어 루프를 nice −20 으로 돌리면 되나요? 어떻게 바꾸겠나요?</summary>

안 된다. nice 는 몫이고, 1 ms 마감에 필요한 건 **최악 지연** 보장이다. CFS/EEVDF 에서는 다른 태스크의 slice(2~3 ms), 커널 비선점 구간, 인터럽트 스레드가 지연에 들어간다. 바꾸는 순서: ① SCHED_FIFO (높은 rt_prio) 또는 주기가 명확하면 SCHED_DEADLINE (runtime/deadline/period, CBS 로 예산 강제), ② `mlockall(MCL_CURRENT|MCL_FUTURE)` 로 페이지 폴트 제거, ③ 제어 스레드를 `isolcpus`/cpuset 로 격리하고 IRQ affinity 를 다른 CPU 로, ④ PREEMPT_RT 커널로 커널 안 비선점 구간 축소, ⑤ cyclictest 로 최악 지연을 **측정** (ELX-A04, 다음 단계). 그리고 RT throttling 기본 950/1000 ms 를 이해해야 한다 — 폭주 시 안전판이지만 throttle 되면 **제어 루프가 50 ms 멈출 수 있다**. 1 kHz 루프가 그 정도 마감이면 MCU (Cortex-M + RTOS, RTOS-M11/M12 분석) 로 내리고 리눅스는 상위 계획만 맡기는 분할 설계가 흔하다.
</details>

<details>
<summary>Q8. 이 설계(가중 공정 분배)가 틀리는 경우는? "공정" 한데 사용자가 불공정하다고 느끼는 경우.</summary>

① **그룹**: 공정은 엔티티 단위다. 터미널 A 에서 `make -j16`, 터미널 B 에서 프로그램 1 개면 평평한 CFS 는 1/17 을 준다. autogroup/cgroup `cpu.weight` 는 2 단계 vruntime 으로 그룹끼리 먼저 나눈다 (연습 2: 1 개짜리 그룹의 태스크 0.500, 4 개짜리 그룹 0.125 씩). ② **멀티코어**: 공정은 CPU 별 런큐 안에서만이고, CPU 사이는 로드 밸런서가 주기적으로 맞춘다 — 순간적으로 한 CPU 에 몰리면 같은 nice 라도 몫이 다르다 (이 시뮬레이터 범위 밖). ③ **I/O 위주 태스크**는 원래 몫을 다 안 쓰므로 "공정" 이 반응성을 보장하지 않는다 — 깨어남 처리(배치/선점)가 실제 체감을 정한다. ④ **RT 클래스**는 공정의 바깥 — FIFO 하나가 CFS 전체를 굶긴다 (Part C). ⑤ **짧은 측정 창**: tick 반올림 때문에 nice 19 같은 아주 가벼운 태스크는 1 % 이상 어긋난다 (demo ②). 꼬리: "어떻게 알아챌까?" → `/proc/<pid>/sched` 의 `se.sum_exec_runtime`, `perf sched latency`, `schedstat` 으로 몫과 대기를 따로 본다.
</details>

<details>
<summary>Q9. 시뮬레이터가 맞다는 건 어떻게 확인했나요?</summary>

네 겹. ① **단위**: 가중치 표 전체 성질(비 1.18~1.32, inv × w ≈ 2^32), `calc_delta_fair` 를 정확한 값과 240 케이스 비교, heap 을 전수 최솟값과 20 000 연산 비교, 손 계산(1 000 000 → 3 056 716, slice 4520/1479 µs). ② **불변식**: lag ≤ period + tick, vr 퍼짐 ≤ 가장 가벼운 태스크의 (slice + 2 tick) 환산, min_vruntime 역행 0. ③ **대조 실험**: 같은 시나리오를 배치 有/無, 스로틀 有/無 로 돌려 차이가 예측한 방향과 크기(501 ms, 950 ms)로 나오는지. ④ **실제 커널**: 같은 nice 조합을 VM 의 실제 스케줄러에서 CPU 하나에 묶어 돌려 몫이 소수 셋째 자리까지 일치(0.753/0.247, 0.697/0.228/0.075), 잔 태스크가 깨어나도 최장 연속 실행 3~3.9 ms. 한계도 말한다: VM 의 끊김 측정은 호스트 잡음이 섞여 지연 주장에 쓰지 않았고, CFS 시절 기본값·`check_preempt_tick` 은 6.5 원문과 대조하지 못했다.
</details>

<details>
<summary>Q10. 커널은 왜 min-heap 이 아니라 RB 트리를 쓰나요?</summary>

① 임의 엔티티 삭제가 잦다 — 잠듦은 curr 만이지만 이주(load balance), nice/cgroup 변경, `DELAY_DEQUEUE` 처리로 트리 한가운데를 빼야 하고, heap 은 위치 색인을 따로 유지해야 한다. ② leftmost 캐시로 최소 조회 O(1). ③ 결정적: EEVDF 는 "eligible(v ≤ V) 중 deadline 최소" 라는 **2 차원 질의**라서 heap 하나로는 안 된다 — 트리를 deadline 으로 정렬하고 각 노드에 부분 트리 최소 vruntime 을 증강하면 왼쪽 부분 트리에 eligible 이 있는지 O(1) 로 알고 O(log n) 에 찾는다 (`pick_eevdf`). heap 은 포인터 없는 배열이라 캐시 친화적이고 단순해서, 태스크가 고정된 MCU 스케줄러(우선순위 비트맵, ready list)나 이 레슨처럼 최소만 필요한 경우엔 오히려 낫다.
</details>

## 9. 추가 연습

### 연습 1 — vruntime 래핑

u64 vruntime 을 넘치기 2 ms 전(`UINT64_MAX − 2 000 000`)에서 시작하는 hog 2 개를 순진한 `a < b` 비교와 부호 있는 차이로 각각 돌려 몫을 비교하라.

<details><summary>답 보기</summary>

순진한 비교: 먼저 넘친 태스크의 vr 가 작은 수가 되어 계속 "최소" — 몫 **1.000** 대 0. 부호 있는 차이: 0.500. 두 vr 의 실제 차가 2^63 ns(≈ 292 년)보다 작으면 `(int64_t)(a − b)` 가 순서를 맞게 준다. 펌웨어에서 32 비트 tick 카운터 비교(`(int32_t)(now − deadline) >= 0`)와 같은 기법이고, 커널 `vruntime_cmp`/`entity_before` 와 jiffies 의 `time_after()` 도 같다.
</details>

### 연습 2 — 그룹 스케줄링 (cgroup cpu.weight / autogroup)

그룹 G1(태스크 1 개), G2(태스크 4 개), 그룹 가중치 같음. 2 단계로 고른다: 그룹 엔티티 중 vr 최소 → 그 그룹 안에서 vr 최소. 그룹 엔티티의 vr 는 **그룹 가중치로** 오른다. 몫을 구하고, G1 가중치를 2 배로 하면?

<details><summary>답 보기</summary>

1:1 → G1 태스크 0.500, G2 태스크 0.125 × 4 (평평하면 0.2 씩). 2:1 → G1 태스크 0.667. 커널은 그룹마다 CPU 별 `cfs_rq` 를 두고 그룹 엔티티(`se->my_q`)를 부모 cfs_rq 에 넣는다; 그룹 가중치(cgroup v2 `cpu.weight` 기본 100 ↔ shares 1024)는 CPU 별 부하 비율로 나뉘어 각 CPU 의 그룹 엔티티 weight 가 된다 (`calc_group_shares` — 세부는 문서 기준, 미검증). autogroup 은 세션(setsid) 마다 그룹을 자동으로 만드는 것 (`kernel/sched/autogroup.c:258` 에서 nice → `sched_prio_to_weight` 로 그룹 shares 를 정한다). 이 연습은 1 ms tick 마다 다시 고르는 단순화 모델이다.
</details>

### 연습 3 — 미니 EEVDF: 같은 몫, 짧은 대기

hog 2 개 + 태스크 S (slice 0.5 ms 요청), 모두 nice 0, base slice 2.8 ms (8 CPU 이상 기본값), tick 1 ms. eligible 판정은 `Σ w_j (v_j − v_i) ≥ 0`, 마감 `d = v + slice·1024/w`, v ≥ d 면 마감 갱신 + 재스케줄, RUN_TO_PARITY 보호는 큐의 최소 slice. CFS 식 pick 과 S 의 최대 대기·몫·전환 수를 비교하라. 손 계산 상태 하나(`v = {0, 3, 1} ms`, `d = {5, 4, 1.5} ms`)에서 pick 도 확인하라.

<details><summary>답 보기</summary>

실측: S 최대 대기 CFS 6.0 ms (전환 1001) | EEVDF slice 0.5 ms **2.0 ms** (hog 도 2.0 ms, 전환 3001) | EEVDF 기본 slice 6.0 ms (전환 1001). 몫은 CFS 0.333, EEVDF 0.333. 손 계산: V = 4/3 ms → eligible {0, 2}, 마감 {5, 1.5} → **2**. S 의 마감을 6 ms 로 바꾸면 0. 해석: 짧은 slice 는 "자주, 짧게" 를 사는 것이지 더 많이 사는 것이 아니다 — 마감이 가까워 eligible 이 되는 즉시 뽑히지만, 0.5 ms 를 쓰면 v 가 V 를 넘어 eligible 에서 빠진다. 그리고 보호 구간이 큐의 최소 slice 로 줄어 **다른 태스크까지 전환이 늘어난다** — 지연과 처리량의 거래가 slice 라는 손잡이 하나로 드러난 것.
</details>

### 연습 4 — avg_vruntime 증분 추적 (6.18 방식)

엔티티 추가/제거/실행(v 증가)을 무작위로 2 만 번 하면서 `zero_vruntime`, `sum_w_vruntime = Σ(vᵢ − v0)·wᵢ`, `sum_weight` 만 갱신하라. `avg()` 는 floor(Σwv/Σw) 를 돌려주고 기준점을 그 값으로 옮긴다. 전수 계산(128 비트)과 평균·eligibility 가 매번 같은지, `avg()` 결과 자체가 항상 eligible 인지 검사하라.

<details><summary>답 보기</summary>

실측: 20 000 상태, 평균 불일치 0, eligibility 불일치 0, `eligible(avg())` 실패 0. 핵심 두 줄: 기준점 이동 `sum_w −= sum_weight × Δ; zero += Δ` (`fair.c:694 update_zero_vruntime`, 항등식이라 오차가 안 쌓인다), 음수 floor `if (rt < 0) rt −= W − 1` (`fair.c:734` "sign flips effective floor / ceiling") — 왼쪽으로 치우친 평균이라 평균 자체가 eligible 이다 (`fair.c:704` 주석 "avg_vruntime() + 0 must result in entity_eligible() := true"). 상대값을 쓰는 이유: vᵢ·wᵢ 를 그대로 더하면 u64 가 금방 넘친다; vᵢ − v0 는 lag 크기(수 ms) 수준이라 key·weight 가 ~44 비트 (`fair.c:593-605` 주석).
</details>

### 연습 5 — lag 보존 배치의 부풀림

가중치 합 Wq, 평균 V 인 큐에 저장된 lag vl 을 가진 엔티티(w)를 `v = V − vl` 로 그냥 넣으면, 넣은 뒤 새 평균 V' 기준 실제 lag 는 얼마인가? `vl·(Wq + w)/Wq` 로 부풀려 넣으면? 2000 개 무작위 케이스로 확인하고 `entity_lag` 의 clamp 도 검사하라.

<details><summary>답 보기</summary>

그냥 넣으면 V' = V − w·vl/(W + w) 로 평균이 끌려가 실제 lag = vl·W/(W + w) — **매번 줄어든다** (2000/2000). 부풀리면 넣은 뒤 lag 가 저장값과 같다 (정수 반올림 2 ns 이내, 2000/2000). 6.18 `place_entity` 주석의 유도(`fair.c:5337-5385`)를 그대로 따라간 것: `vl'ᵢ = vlᵢ − wᵢ·vlᵢ/(W + wᵢ)` 을 거꾸로 풀어 `vlᵢ = (W + wᵢ)·vl'ᵢ / W`. clamp: `±calc_delta_fair(max_slice + TICK_NSEC)` — nice 0, slice 2.8 ms 면 ±3.8 ms, nice 5 면 ×1024/335. 이게 "오래 자도 한 slice 남짓 이상은 앞서지 못한다" 의 근거다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san
```

실제 출력 (exit 0, ASan+UBSan 도 `17/17 passed` 보고 없음):

```text
  ex1 wrap: naive share0 1.000  signed share0 0.500
  ex2 groups 1:1  -> G1 task 0.500, G2 tasks 0.125 .. 0.125
  ex2 groups 2:1  -> G1 task 0.667
  ex3 task2 max wait: CFS 6.0 ms (switches 1001) | EEVDF slice 0.5 ms 2.0 ms (hog 2.0 ms, switches 3001) | EEVDF default 6.0 ms (switches 1001); share CFS 0.333 EEVDF 0.333
  ex4 avg_vruntime: 20000 states, avg mismatch 0, eligible mismatch 0
  ex5 place: 2000 cases, inflate off-by>2ns 0, no-inflate shrank 2000
17/17 passed
```

관찰용 `demo_trace.c` (solution.c 를 `main` 이름만 바꿔 포함):

<!-- code: demo_trace.c -->

## 10. 핵심 정리

- **vr += Δ × 1024 / w**, 항상 vr 최소를 고른다 → 실제 시간은 weight 비율. nice 한 단계 ×1.25 (≈10 %p), 0:5 = 1024:335 = 75.4:24.6 (시뮬레이터·VM 실측 일치).
- 나눗셈 대신 `sched_prio_to_wmult` (2^32/w) 곱 + 시프트 (`__calc_delta`). nice 0 은 분기 하나. 비교는 항상 `(int64_t)(a − b)` (래핑).
- CFS 시절: period = max(latency 6 ms, nr × 0.75 ms), slice = period·w/Σw, tick 에서 선점 → lag ≤ period + tick. 배율 1 + ilog2(min(ncpu, 8)).
- min_vruntime 은 단조. 깨어난 태스크는 `max(vr, min_vr − latency/2)` — 없으면 1 초 잔 태스크가 501 ms 독점.
- 6.6+ **EEVDF**: eligible(v ≤ V, V = 가중 평균) 중 가상 마감 `v + slice/w` 최소. 몫은 weight, 지연은 slice. 6.18 엔 latency/min_gran 없고 `base_slice` 0.7 ms × 배율 (VM 2.1 ms 실측), 배치는 lag 보존 + clamp.
- 클래스 순서가 몫보다 먼저: FIFO 하나면 CFS 0. RT throttling 950/1000 ms 는 몫 5 % 는 지키지만 **최대 950 ms 지연** — 워치독 설계에 반영. 6.18 은 fair_server 50/1000 ms 도 있다.
- nice 는 지연 보장이 아니다. 마감이 있으면 FIFO/DEADLINE + mlock + 격리 + PREEMPT_RT, 또는 MCU 로 분할.

## 참고자료

- Linux 6.18.55 소스 (Lima VM `/var/lab/src/linux-6.18.55`): `kernel/sched/core.c:10318-10360` (가중치 표·역수 표), `:1448 set_load_weight`; `kernel/sched/sched.h:135-173` (scale_load, NICE_0_LOAD), `:2351` WEIGHT_IDLEPRIO; `kernel/sched/fair.c:72-80` (튜너블), `:192` 배율, `:229-296` `__calc_delta`/`calc_delta_fair`, `:615-748` avg_vruntime/zero_vruntime, `:767` entity_lag, `:802` vruntime_eligible, `:973` set_protect_slice, `:1025` pick_eevdf, `:1127` update_deadline, `:1294` update_curr, `:5313` place_entity; `kernel/sched/features.h`; `kernel/sched/rt.c:18,24`; `kernel/sched/deadline.c:1825` (fair_server)
- `Documentation/scheduler/sched-design-CFS.rst`, `Documentation/scheduler/sched-eevdf.rst` (6.18 트리) — <https://docs.kernel.org/scheduler/sched-eevdf.html>
- Ion Stoica, Hussein Abdel-Wahab, "Earliest Eligible Virtual Deadline First: A Flexible and Accurate Mechanism for Proportional Share Resource Allocation", 기술 보고서, Old Dominion University, 1995 (제목·번호는 기억 기준)
- Jonathan Corbet, "An EEVDF CPU scheduler for Linux", LWN, 2023 — <https://lwn.net/Articles/925371/>; "Completing the EEVDF scheduler", LWN, 2024 — <https://lwn.net/Articles/969062/>
- man7.org: sched(7) (정책·nice·RT throttling `sched_rt_runtime_us`), sched_setattr(2) (`sched_runtime`), setpriority(2), sched_setaffinity(2) — <https://man7.org/linux/man-pages/man7/sched.7.html>
- OSTEP 9장 Proportional Share (§9.6 stride; 이 디스크의 v0.91 원문에는 CFS 절이 없고 이후 판에 추가 — Don 노트 `notes/2026-09-30_C09_proportional_share.md` 8.1 보충)
- Carl A. Waldspurger, William E. Weihl, "Stride Scheduling: Deterministic Proportional-Share Resource Management", MIT TM-528, 1995
- 관련 레슨: OS-B12 (FCFS~MLFQ, 연습 3 stride), RTOS-M11 (RM 응답 시간 분석), RTOS-M12 (EDF vs RM), RTOS-M03 (우선순위 역전), ELX-B13 (systemd watchdog), OS-A11 (서비스 데몬). 다음 단계: ELX-A04 (PREEMPT_RT + cyclictest), LPERF-M05 (runqlat 로 스케줄러 지연 측정)

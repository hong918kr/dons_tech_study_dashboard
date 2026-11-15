---
id: RTOS-A03
time: 110분
updated: 2026-11-11
---
# RTOS-A03 WCET 측정과 지터 분석

## 학습 목표

- 실행 시간 표본에서 **nearest-rank 백분위**(p50/p99/p99.9), 고정 bin 히스토그램, 최고 수위(HWM, high-water mark)와 그 위치, 지터(max − min), 표준편차(Welford)를 정확히 계산하고 — "표본 1000개의 p99.9 는 max 가 아니다", "큰 기준값에서 교과서 분산 공식은 0 이 나온다" 를 직접 확인한다.
- **측정 기반 WCET(MBTA, measurement-based timing analysis)** 이 왜 "하한" 인지 보인다: 유효 입력 범위에서 2만 번 무작위로 돌려도 오류 처리 경로를 한 번도 안 밟아 진짜 WCET 의 57% 만 본다. 표적 입력 3개로 최악 경로를 밟는다.
- 제어 흐름 그래프(CFG) 최장 경로로 **정적 WCET 상한** 을 구하고, 실행 불가능한 경로(infeasible path) 때문에 상한이 비관적이 되는 것과 흐름 정보(flow fact) 하나로 정확해지는 것을 숫자로 본다.
- 캐시 모델로 **cold vs warm**, "희귀 경로가 다음 호출을 느리게 만든다"(캐시 오염)를 보이고, 출력 래치(타이머 preload)로 **I/O 지터를 0** 으로 만드는 설계를 설명한다.
- 호스트(`mach_absolute_time`)와 보드(DWT `CYCCNT`)에서 실제로 재는 법과 그 한계(시계 해상도, OS 잡음, QEMU 는 CYCCNT 를 안 돌린다 — 실측)를 말할 수 있다.

## 1. 문제

RTA(RTOS-M11)의 `C_i` 는 "최악 실행 시간" 인데, 면접관은 곧바로 **"그 C 는 어디서 났나요? 측정 max 를 믿어도 되나요?"** 를 묻는다. 측정은 본 것까지만 말해 준다 — 안 밟은 경로, 비어 있던 캐시, 끼어든 인터럽트는 숫자에 없다. 이 레슨은 그 간극을 결정적인(deterministic) 모델로 재현하고, 진짜 시계로도 재 본다.

측정 대상은 모터 속도 루프 한 스텝 `ctrl_step(sp, meas)` 다. 기본 블록마다 "비용 모델 사이클" 을 붙여 두었다 (실제 MCU 사이클이 아니라 **비율을 보기 위한 모델**):

| 블록 | 하는 일 | 비용 | 언제 |
|---|---|---|---|
| ENTRY | 진입 | 10 | 항상 |
| OK / FAULT | ADC 값 범위 검사 (0..4095) | 2 / 6 | 범위 밖이면 FAULT, 마지막 정상값 유지 |
| RECAL | 3연속 FAULT → 보정표 32개 평균, 적분기 리셋 | 8 + 32×(3+mem) | **희귀** |
| SPIKE / PLAIN | 직전 값과 1500 넘게 차이 → 최근 5개 중앙값(삽입 정렬, 이동 1회 3) | 6+3×이동 / 2 | 입력 의존 |
| HOLD | 오차 ≤ 8 → 출력 유지, 끝 | 3 | 데드밴드 |
| LUT | 피드포워드 표 16칸 선형 탐색 | 4 + 2×mem + 탐색 칸×(2+mem) | sp 가 클수록 길다 |
| EXACT / INTERP | 구간 끝점 정확히 / 보간(나눗셈) | 2 / 20+2×mem | |
| PI → SAT / INTEG | PI, 포화면 적분 멈춤(anti-windup) | 15 → 8 / 4 | |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 함수, 여섯 개의 "WCET" (solution 실제 출력, 비용 모델 사이클)</text>
  <line x1="40" y1="120" x2="720" y2="120" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="40" y1="114" x2="40" y2="126"/><line x1="200" y1="114" x2="200" y2="126"/>
    <line x1="360" y1="114" x2="360" y2="126"/><line x1="520" y1="114" x2="520" y2="126"/><line x1="680" y1="114" x2="680" y2="126"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="36" y="142">0</text><text x="190" y="142">200</text><text x="350" y="142">400</text>
    <text x="510" y="142">600</text><text x="670" y="142">800</text>
  </g>
  <rect x="54" y="60" width="99" height="24" class="f-mut" stroke="currentColor"/>
  <text x="58" y="77" fill="currentColor" font-size="12">무작위 min..max</text>
  <line x1="153" y1="84" x2="153" y2="120" stroke="currentColor"/>
  <text x="120" y="54" fill="currentColor" font-size="12">141 측정 max</text>
  <line x1="175" y1="96" x2="175" y2="120" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="150" y="168" fill="currentColor" font-size="12">169 = max+20%</text>
  <rect x="236" y="96" width="8" height="24" class="f-ok" stroke="currentColor"/>
  <text x="214" y="90" fill="currentColor" font-size="12">247 진짜 WCET</text>
  <rect x="261" y="96" width="8" height="24" class="f-warn" stroke="currentColor"/>
  <text x="262" y="188" fill="currentColor" font-size="12">281 정적 상한(흐름 정보 없음)</text>
  <line x1="265" y1="120" x2="265" y2="176" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="338" y="96" width="8" height="24" class="f-acc" stroke="currentColor"/>
  <text x="318" y="90" fill="currentColor" font-size="12">377 cold 캐시</text>
  <rect x="634" y="96" width="8" height="24" class="f-warn" stroke="currentColor"/>
  <text x="560" y="90" fill="currentColor" font-size="12">747 "항상 miss" 상한</text>
  <text x="10" y="216" fill="currentColor">측정은 아래에서 다가가고(하한), 정적 분석은 위에서 내려온다(상한). 진짜 WCET 은 그 사이 어딘가 —</text>
  <text x="10" y="236" fill="currentColor">이 레슨은 모델이 작아서 둘 다 247 에 정확히 닿게 만들 수 있다. 실제 칩에서는 보통 둘 사이에 틈이 남는다.</text>
</svg>
```

### Part A — 통계 엔진 (합성 표본으로 검사)

- `pct_rank(n, q)` / `pct_nr(sorted, n, q)` — nearest-rank: `rank = ⌈q·n/10000⌉` (q 는 만분율, p99.9 = 9990), 1 ≤ rank ≤ n, 64비트로 곱한다.
- `hist_add(h, x)` — 고정 bin `[lo + i·w, lo + (i+1)·w)`, 아래는 `under`, 위는 `over`, `maxseen` 기록. **`lo + nbins·w` 를 32비트로 계산하지 말 것.** `hist_pct_upper(h, q)` — 표본 없이 히스토그램만으로 백분위의 **상한**(bin 위쪽 끝, over 면 `maxseen`). MCU 는 5만 개 표본을 저장할 RAM 이 없다.
- `summarize(x, n, scratch, s)` — min, max, **HWM 위치**(max 가 처음 나온 인덱스), p50/p99/p99.9, 지터 = max − min, 평균과 모표준편차(Welford).

### Part B — 경로가 WCET 를 정한다

- (주어진) `campaign()` — sp·meas 를 0..4095 에서 균일하게 2만 번. 결과 max 와 블록 커버리지 비트맵.
- `cfg_bound(g, n, mc, a, b, path, plen)` — 주어진 CFG 표(위상 순서)에서 진입→출구 **최장 경로** = 정적 WCET 상한. 노드 비용 = `base_ops + base_mem·mc + loop·(body_ops + body_mem·mc)` (루프 상한 × 몸통). `a, b ≥ 0` 이면 "두 노드는 한 경로에 같이 못 온다" (흐름 정보).
- `craft_worst(sp, meas, cap)` — `ctrl_init` 직후부터 마지막 스텝이 최악 경로를 밟는 입력 시퀀스.
- 검사: 무작위 max < 진짜 WCET, max × 1.2 도 모자람, 1% 고장 주입도 RECAL 은 못 봄, 표적 입력 = 흐름 정보를 넣은 상한(정확), 흐름 정보 없는 상한 > 표적.

### Part C — STM32 식 캐시와 출력 지터

STM32F4 의 ART 가속기(플래시용 작은 명령·데이터 캐시)나 Cortex-M7 L1 캐시처럼, 데이터 접근을 **직접 사상 8줄 × 16 B** 캐시에 통과시킨다. LUT(128 B)는 8줄을 정확히 채우고, 보정표(128 B)는 **같은 8줄에 전부 충돌** 한다.

- `cache_access(c, addr)` — hit 1, miss 1 + 10. hits/misses 집계.
- `io_times(exec, n, P, mode, lat)` — 주기 P ISR 형 잡의 출력 시각. 즉시 쓰기(끝나는 순간 PWM 레지스터) vs 래치(shadow 레지스터에 쓰고 다음 주기 경계의 갱신 이벤트에서 반영). 시작 = max(출시, 이전 잡 끝), 오버런 수 반환.
- 검사: cold(매 호출 전 flush) 무작위 p50/max > warm, 표적 cold = 377, "항상 miss" 상한 747, RECAL 다음 호출에 miss 가 남음, 래치하면 I/O 지터 0.

## 2. Problem (English)

> **WCET measurement and jitter analysis for a control-loop step.** You are given `ctrl_step()` (sensor validation with 3-strike recalibration, spike filter, deadband, piecewise-linear feed-forward LUT, PI with saturation and conditional-integration anti-windup) instrumented with a per-basic-block cost model, plus its CFG as a table.
>
> 1. `uint32_t pct_rank(uint32_t n, uint32_t q10k)`, `uint32_t pct_nr(const uint32_t *sorted, uint32_t n, uint32_t q10k)` — nearest-rank percentile, `q10k` in 1/10000 (p99.9 = 9990).
> 2. `void hist_add(hist_t *h, uint32_t x)` (fixed bins + under/over, overflow-safe) and `uint32_t hist_pct_upper(const hist_t *h, uint32_t q10k)` — a conservative percentile from the histogram alone.
> 3. `int summarize(const uint32_t *x, uint32_t n, uint32_t *scratch, summary_t *s)` — min, max, first index of max, p50/p99/p99.9, jitter = max − min, mean and population stddev (numerically stable).
> 4. `uint32_t cfg_bound(...)` — longest entry→exit path in the DAG with loop bounds; optional mutual-exclusion pair (a, b).
> 5. `int craft_worst(int32_t *sp, int32_t *meas, int cap)` — an input sequence whose last step hits the worst feasible path.
> 6. `uint32_t cache_access(cache_t *c, uint32_t addr)` — direct-mapped, 8 lines × 16 B, hit 1 / miss 11.
> 7. `uint32_t io_times(const uint32_t *exec, uint32_t n, uint32_t P, int mode, uint32_t *lat)` — output latency for immediate write vs. latch-at-next-period-boundary; return overruns.
>
> Example: 20,000 uniform random valid inputs → max 141, coverage misses FAULT and RECAL. Static bound without flow facts 281; with "FAULT and SPIKE are exclusive" 247; crafted input reaches exactly 247. Cold cache: 377. Latched output: latency 250 for every job, jitter 0.
>
> Follow-ups: Would you ship with "measured max + 20 %"? How do you get WCET on a cached Cortex-M7? Where does the analysis break (multicore interference, timing anomalies)? How would you cut the I/O jitter of a 20 kHz current loop?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "3/42 passed", exit 1 (스텁은 0 을 돌려주고 멈추지 않는다)
```

TODO 는 A1 `pct_rank`/`pct_nr`, A2 `hist_add`/`hist_pct_upper`, A3 `summarize`, B1 `cfg_bound`(+ 도우미 `longest_skip`), B2 `craft_worst`, C1 `cache_access`, C2 `io_times`. `ctrl_step`, CFG 표 `g_cfg`, `campaign`, 정렬·제곱근 도우미, 하네스는 주어진 코드다.

관찰용 파일 (하네스와 별개, 결과가 매번 다르므로 CHECK 없음):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_wcet_host.c -o demo_wcet_host && uptime && ./demo_wcet_host   # 약 5 s
./demo_wcet_host noqos                                                     # QoS 지정 없이 비교
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_dwt_cyccnt.c -o -   # 보드 코드 컴파일 확인
```

## 4. 힌트

<details>
<summary>힌트 1 — A1: "p99.9 = 정렬 후 0.999·n 번째" 는 반올림 규칙이 핵심</summary>

nearest-rank 는 "그 백분위 이하에 표본의 q% 이상이 들어가는 **가장 작은 관측값**" 이다. 순위는 올림 `⌈q·n/10000⌉`, 0 이면 1 로. `q·n` 은 n 이 40억이면 32비트를 넘으니 `uint64_t` 로 곱한다. 보간(interpolation)하지 않으므로 답은 항상 실제로 관측된 값이다 — 사이클 수에 "123.4 사이클" 같은 값을 보고하지 않는 이유.

</details>

<details>
<summary>힌트 2 — A2: 넘침 없이 bin 번호 구하기, 그리고 상한 백분위</summary>

`x < lo` 를 먼저 거르고, `i = (x − lo) / w` 를 64비트로 계산해 `i ≥ nbins` 면 over. `x >= lo + nbins*w` 로 비교하면 `lo` 가 크거나 `w` 가 클 때 32비트 덧셈이 넘쳐 엉뚱한 값과 비교한다 (테스트: `lo = 0xFFFFFF00`). 상한 백분위는 under → bin 0.. → over 순으로 누적하다가 누적 ≥ rank 가 되는 칸의 **위쪽 끝 − 1** (그 칸 안 어떤 값보다도 크거나 같다). 마지막 칸이면 `maxseen` 으로 조여도 된다.

</details>

<details>
<summary>힌트 3 — A3: Welford 한 줄씩</summary>

`d = x − mean; mean += d / k; m2 += d · (x − mean)` (k = 지금까지 개수, 두 번째 곱에는 **갱신된** mean). 분산 = m2 / n. `Σx²/n − (Σx/n)²` 은 x 가 10⁹ 근처면 두 큰 수(10¹⁸)의 차이라 double 53비트로는 4 같은 작은 분산이 다 날아간다. HWM 위치는 `>` 로 비교해야 "처음" 나온 위치가 남는다.

</details>

<details>
<summary>힌트 4 — B1/B2: DAG 최장 경로 + 경우 나누기, 그리고 "안 밟은 블록부터"</summary>

노드가 위상 순서이므로 앞에서부터 `dist[s] = max(dist[s], dist[v] + cost(s))`, 부모를 기록해 경로를 복원한다. "a 와 b 는 같이 못 온다" 는 a 를 뺀 그래프와 b 를 뺀 그래프 각각의 최장 경로 중 큰 것 (이 두 경우가 모든 실행 가능한 경로를 덮는다). 표적 입력은 커버리지에서 빠진 블록(FAULT, RECAL)을 보고 만든다: 범위 밖 값 3연속 + sp 를 LUT 마지막 구간의 끝점이 아닌 곳(예: 4000)에 두면 보간과 포화까지 같이 밟는다.

</details>

<details>
<summary>힌트 5 — C1/C2: tag/index, 그리고 "끝난 시각을 올림"</summary>

`line = addr / 16`, `idx = line % 8`, `tag = line / 8`. valid 이고 tag 가 같으면 hit. 래치 모드의 출력 시각은 `⌈end / P⌉ · P` — 끝이 경계에 딱 맞으면 그 경계다. 이전 잡이 늦게 끝나면 다음 잡의 시작이 밀린다 (`start = max(k·P, prev_end)`), 그래서 오버런 한 번이 다음 잡의 지연까지 바꾼다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨 | Cortex-M 보드 | FreeRTOS (V11.3.2) | Zephyr | CMSIS-RTOS2 | macOS 호스트 |
|---|---|---|---|---|---|
| 시각 읽기 `cyc_now()` | `DWT->CYCCNT` (M3/M4/M7), M0/M0+ 는 SysTick `VAL`·타이머 | `portGET_RUN_TIME_COUNTER_VALUE()` (사용자가 정의) | `k_cycle_get_32()` | `osKernelGetSysTimerCount()` | `mach_absolute_time()` (24 MHz, 41.67 ns) |
| 태스크별 누적 시간 | — | `ulTaskGetRunTimeCounter()`, `vTaskGetRunTimeStatistics()` | `k_thread_runtime_stats_get()` (문서 기준, 미검증) | — | `getrusage` 류 |
| 잡 하나의 실행 시간 | 측정 구간 앞뒤 CYCCNT 차 | `traceTASK_SWITCHED_IN/OUT` 훅에서 CYCCNT (RTOS-A06) | 트레이싱 | — | 앞뒤 차 |
| `summarize` / `hist_*` | 보드에선 히스토그램 + max + "max 를 만든 입력" 만 저장 | — | — | — | 표본 저장 후 정렬 |
| `cache_access` | M7 L1 D-cache, STM32F4 ART 데이터 캐시 | — | — | — | 실제 캐시 (L1D 128 KB, L2 20 MB) |
| `IO_LATCHED` | TIM CCR preload(OCxPE) + 갱신 이벤트 | — | — | — | — |

FreeRTOS 의 run-time stats 는 **누적** 이다. `vTaskSwitchContext` 가 나가는 태스크에 "들어온 뒤 흐른 시간" 을 더할 뿐이다 (`tasks.c:5230-5254`):

```c
ulTotalRunTime[ 0 ] = portGET_RUN_TIME_COUNTER_VALUE();
/* ... Note that there is no overflow
 * protection here so count values are only valid until the timer
 * overflows. ... */
if( ulTotalRunTime[ 0 ] > ulTaskSwitchedInTime[ 0 ] )
    pxCurrentTCB->ulRunTimeCounter += ( ulTotalRunTime[ 0 ] - ulTaskSwitchedInTime[ 0 ] );
ulTaskSwitchedInTime[ 0 ] = ulTotalRunTime[ 0 ];
```

그래서 "CPU 점유율" 은 알지만 **잡 하나의 최악** 은 모른다. 카운터 형은 기본 `uint32_t` (`FreeRTOS.h:2876-2881`)이고, CYCCNT 를 그대로 쓰면 168 MHz 에서 25.6 s 마다 넘쳐 위 주석대로 값이 무의미해진다 (연습 2). `configGENERATE_RUN_TIME_STATS == 1` 이면 `portCONFIGURE_TIMER_FOR_RUN_TIME_STATS` 와 카운터 매크로가 필수다 (`FreeRTOS.h:2697-2713`). 잡 단위 최악이 필요하면 측정 구간을 코드에 직접 넣거나 `traceTASK_SWITCHED_IN()`(`FreeRTOS.h:631-635`, `tasks.c:5274` 에서 호출)/`traceTASK_SWITCHED_OUT()`(`FreeRTOS.h:662-666`) 훅에서 CYCCNT 를 찍는다 — 그 방식은 RTOS-A06.

### Part A — 백분위: nearest-rank 와 "p99.9 의 함정"

```c
static uint32_t pct_rank(uint32_t n, uint32_t q10k) {
    if (n == 0) return 0;
    uint64_t r = ((uint64_t)q10k * n + 9999u) / 10000u;   /* 올림 */
    if (r < 1) r = 1;
    if (r > n) r = n;
    return (uint32_t)r;
}
```

| 표본 수 n | p50 순위 | p99 순위 | p99.9 순위 | 뜻 |
|---|---|---|---|---|
| 10 | 5 | 10 | 10 | 표본 10개면 p99 = max. "p99 가 좋다" 는 말이 의미 없다 |
| 1,000 | 500 | 990 | **999** | p99.9 = **두 번째로 큰 값**. max 하나는 빠진다 |
| 1,001 | 501 | 991 | 1,000 | |
| 100,000 | 50,000 | 99,000 | 99,900 | 위쪽 100개가 p99.9 위에 있다 |

짝수 n 의 p50 은 아래 중앙값(관측값)이다 — 통계 패키지의 "중앙값 = 두 값 평균" 과 다르다 (Hyndman & Fan 1996 의 정의 9가지 중 하나를 고른 것). 사이클·µs 보고에는 실제로 나온 값이 낫다. 핵심 교훈: **p99.9 는 꼬리의 "대표값" 이지 최악이 아니다.** 하드 실시간 RTA 의 C 에는 max(그것도 상한)를 쓰고, 백분위는 소프트 실시간 SLA·추세 감시용이다.

### Part A — 히스토그램과 넘침, 그리고 상한 백분위

```c
static void hist_add(hist_t *h, uint32_t x) {
    h->n++;
    if (x > h->maxseen) h->maxseen = x;
    if (x < h->lo) { h->under++; return; }
    uint64_t i = ((uint64_t)x - h->lo) / h->width;      /* lo + nbins*width 를 안 만든다 */
    if (i >= h->nbins) { h->over++; return; }
    h->bins[i]++;
}
```

테스트 `lo = 0xFFFFFF00, width 16, nbins 64` 에서 `x >= lo + nbins*width` 로 쓰면 오른쪽이 `0x300` 으로 넘쳐 `0xFFFFFFFF` 가 over 로 간다. 차이로 계산하면 bin 15 에 정확히 들어간다. 보드에서 CYCCNT 원시값이나 µs 타임스탬프를 그대로 넣을 때 생기는 버그다.

`hist_pct_upper` 는 표본 없이 백분위를 낸다. 순위가 떨어지는 칸의 위쪽 끝을 돌려주므로 진짜 nearest-rank 값 이상, 차이는 bin 폭 미만이다 (테스트: 5000 표본, bin 25, 6개 백분위 전부). over 칸에 떨어지면 `maxseen` — 그래서 **max 는 히스토그램과 따로 반드시 기록한다.** 보드에서 표본 5만 개(200 KB)는 못 담아도 64칸 히스토그램(256 B) + max 는 담는다.

### Part A — 지터, HWM, 그리고 분산 공식

```c
for (uint32_t i = 0; i < n; i++) {
    if (x[i] < s->min) s->min = x[i];
    if (x[i] > s->max) { s->max = x[i]; s->hwm_idx = i; }    /* '>' → 처음 나온 위치 */
    double d = (double)x[i] - mean;
    mean += d / (double)(i + 1);
    m2 += d * ((double)x[i] - mean);                          /* Welford 1962 */
}
```

실제 출력 (기준값 10⁹ + {2,4,4,4,5,5,7,9}, 진짜 모표준편차 2):

```text
info: sd welford=2.000000 naive=0.000000 (baseline 1e9)
```

교과서 공식 `E[x²] − E[x]²` 는 8×10¹⁸ 근처 두 수의 차이를 구하는데, 그 크기에서 double 의 한 눈금(ulp)이 1024 라 분산 4 는 흔적도 없다 (음수가 나오면 0 으로 잘랐다). 표본이 "부팅 후 µs" 나 64비트 사이클 타임스탬프면 그대로 당한다.

용어 정리 — 이 레슨에서 "지터" 는 셋이다:

| 이름 | 정의 | 어디서 |
|---|---|---|
| 실행 시간 지터 | 실행 시간 max − min | Part A/B (`summary.jitter`) |
| 릴리즈 지터 | 출시 시각이 이상적 시각에서 벗어난 폭 | 연습 2, RTA 의 J (RTOS-M11 Part C, RTOS-A04) |
| I/O(출력) 지터 | 출시 → 출력 반영 지연의 max − min | Part C (`io_times`) — 제어 성능에 직접 영향 |

### Part B — 무작위 측정이 본 것

solution 실제 출력:

```text
info: random (valid ADC only) n=20000: min=17 p50=95 p99=132 p99.9=138 max=141 (run #3390) jitter=124 mean=95.1 sd=15.6 cover=0x1ff3
  [ 60, 70)    466 |####
  [ 70, 80)   2668 |#######################
  [ 80, 90)   4270 |####################################
  [ 90,100)   4794 |########################################
  [100,110)   4464 |######################################
  [110,120)   1673 |##############
  [120,130)   1355 |############
  [130,140)    220 |##
  [140,150)      1 |#
info: random (1% invalid)    max=141 cover=0x1ff7  missing=0x0008
```

(아래쪽 칸 [10,60) 은 HOLD 경로 등 89개, 생략.) 커버리지 `0x1ff3` 에는 비트 2(FAULT)와 3(RECAL)이 없다. 무작위 입력을 **"유효한 ADC 값"** 에서 뽑았기 때문이다 — 자연스러운 선택이 바로 오류 경로를 지운다. 1% 고장을 주입하면 FAULT 는 보지만 RECAL(3연속)은 못 본다: 스텝당 확률이 약 p³ = 10⁻⁶ 이라 2만 번에 볼 확률은 1.96% (연습 4, 정확한 DP), 99% 확신으로 보려면 **465만 번** 이 필요하다.

흥미로운 점: 141 은 "FAULT 를 뺀 그래프의 최장 경로" 와 정확히 같다 (`10+2+36+48+22+15+8`). 무작위 테스트는 **흔한 경로들 사이의 최악은 잘 찾았다** — 못 찾은 건 다른 경로 자체다. 그래서 커버리지가 100% 가 아닌 측정 max 는 WCET 로서 근거가 없다. 측정 max × 1.2 = 169 도 진짜 247 아래다 (CHECK).

### Part B — 정적 상한: CFG 최장 경로

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">ctrl_step 의 CFG (노드 비용, 평평한 메모리 mc=1)</text>
  <rect x="10" y="120" width="70" height="30" class="f-acc" stroke="currentColor"/><text x="18" y="140" fill="currentColor">ENTRY 10</text>
  <rect x="110" y="60" width="60" height="30" class="f-mut" stroke="currentColor"/><text x="120" y="80" fill="currentColor">OK 2</text>
  <rect x="110" y="180" width="70" height="30" class="f-acc" stroke="currentColor"/><text x="116" y="200" fill="currentColor">FAULT 6</text>
  <rect x="200" y="230" width="80" height="30" class="f-acc" stroke="currentColor"/><text x="206" y="250" fill="currentColor">RECAL 136</text>
  <rect x="300" y="60" width="74" height="30" class="f-warn" stroke="currentColor"/><text x="306" y="80" fill="currentColor">SPIKE 36</text>
  <rect x="300" y="180" width="74" height="30" class="f-acc" stroke="currentColor"/><text x="306" y="200" fill="currentColor">PLAIN 2</text>
  <rect x="400" y="20" width="62" height="30" class="f-mut" stroke="currentColor"/><text x="406" y="40" fill="currentColor">HOLD 3</text>
  <rect x="400" y="120" width="62" height="30" class="f-acc" stroke="currentColor"/><text x="406" y="140" fill="currentColor">LUT 48</text>
  <rect x="490" y="80" width="70" height="30" class="f-mut" stroke="currentColor"/><text x="496" y="100" fill="currentColor">EXACT 2</text>
  <rect x="490" y="160" width="76" height="30" class="f-acc" stroke="currentColor"/><text x="496" y="180" fill="currentColor">INTERP 22</text>
  <rect x="590" y="120" width="56" height="30" class="f-acc" stroke="currentColor"/><text x="598" y="140" fill="currentColor">PI 15</text>
  <rect x="670" y="80" width="60" height="30" class="f-acc" stroke="currentColor"/><text x="678" y="100" fill="currentColor">SAT 8</text>
  <rect x="670" y="160" width="60" height="30" class="f-mut" stroke="currentColor"/><text x="674" y="180" fill="currentColor">INTEG 4</text>
  <g stroke="currentColor" fill="none">
    <path d="M80 130 L110 75"/><path d="M80 140 L110 195"/><path d="M145 210 L200 245"/>
    <path d="M170 75 L300 75"/><path d="M170 80 L300 190"/><path d="M180 195 L300 195"/><path d="M280 245 L300 75" stroke-dasharray="5 4" stroke-width="2"/>
    <path d="M280 240 L300 200"/>
    <path d="M374 75 L400 35"/><path d="M374 80 L400 130"/><path d="M374 195 L400 140"/><path d="M374 190 L400 40"/>
    <path d="M462 130 L490 95"/><path d="M462 140 L490 175"/><path d="M560 95 L590 130"/><path d="M566 175 L590 140"/>
    <path d="M646 130 L670 95"/><path d="M646 140 L670 175"/>
  </g>
  <text x="10" y="290" fill="currentColor">점선 RECAL→SPIKE: 그래프로는 이어지지만 실행 불가능 (고장이면 m = prev → 차이 0 → 스파이크 아님)</text>
  <text x="400" y="230" fill="currentColor">강조 경로 = 진짜 최악 247</text>
  <text x="400" y="250" fill="currentColor">점선을 타면 281 (비관적)</text>
</svg>
```

(J1/J2 합류 노드와 EXIT 은 그림에서 생략.) 최장 경로 DP 는 위상 순서로 한 번 훑으면 된다:

```c
dist[0] = node_cost(&g[0], mc);
for (int v = 0; v < n; v++) {
    if (dist[v] < 0 || v == skip) continue;
    for (int k = 0; k < 2; k++) {
        int s = g[v].succ[k];
        if (s < 0 || s == skip) continue;
        int64_t d = dist[v] + node_cost(&g[s], mc);
        if (d > dist[s]) { dist[s] = d; par[s] = v; }
    }
}
```

실제 출력:

```text
info: static bound (no flow facts) = 281 : ENTRY>FAULT>RECAL>J1>SPIKE>J2>LUT>INTERP>PI>SAT>EXIT
info: static bound (FAULT x SPIKE)  = 247 : ENTRY>FAULT>RECAL>J1>PLAIN>J2>LUT>INTERP>PI>SAT>EXIT
info: crafted 3 steps -> worst ops=247 blocks=0x0ead
```

- **281 은 안전하지만 비관적이다.** 코드는 "센서 검사 → 스파이크 필터" 로 순서대로 짜여 있어 그래프상으로는 FAULT 와 SPIKE 를 둘 다 지나는 경로가 있다. 실제로는 고장이면 `m = c->prev` 라 `|m − prev| = 0` → SPIKE 불가. 이건 **값(value)에 숨은 상관관계** 라서 구조만 보는 분석은 모른다.
- 흐름 정보 "FAULT 와 SPIKE 는 배타" 를 주면 247 — 표적 입력이 정확히 닿는다. 상업 도구도 이런 정보를 사용자 주석(annotation)으로 받는다. IPET(implicit path enumeration, Li & Malik 1995)는 블록 실행 횟수를 정수 변수로 두고 `x_FAULT + x_SPIKE ≤ 1` 같은 제약을 넣어 ILP 로 푼다 — 이 레슨의 "경우 나누기" 는 제약 1개짜리 IPET 다.
- **루프 상한도 흐름 정보다.** SPIKE 의 "이동 최대 10" 은 원소 5개 삽입 정렬의 총 이동 수 상한(5·4/2)이고, 안쪽 루프 상한(4) × 바깥(4) = 16 으로 잡으면 18 사이클 더 비관적이 된다. RECAL 32, LUT 14 는 상수라 도구가 스스로 찾는다.
- **코드를 WCET 친화적으로 바꾸면 분석도 쉬워진다:** `if (!valid) { ... } else { 스파이크 필터 }` 처럼 구조로 배타를 드러내면 흐름 정보 없이 247 이 나온다.

표적 입력 (TODO B2) — 커버리지에서 빠진 블록부터 생각한다:

```c
for (int k = 0; k < FAULT_N; k++) { sp[k] = 4000; meas[k] = 4096 + k; }   /* 범위 밖 3연속 */
return FAULT_N;
```

세 번째 스텝: FAULT + RECAL(136), `m = prev = 2048` 이라 PLAIN, `err = 4000 − 2048 + cal_off` 가 커서 HOLD 아님, sp = 4000 ≥ bp[14] = 3940 이라 LUT 14칸 다 탐색, 4000 ≠ 3940 이라 INTERP, `u = 1.5·err + ff` 가 1000 을 넘어 SAT. 블록 비트 `0x0ead` = ENTRY·FAULT·RECAL·PLAIN·LUT·INTERP·PI·SAT.

### Part C — 캐시: cold, warm, 그리고 다음 호출

```c
uint32_t line = addr / LINE_B, idx = line % NLINES, tag = line / NLINES;
if (c->valid[idx] && c->tag[idx] == tag) { c->hits++; return 1; }
c->valid[idx] = 1; c->tag[idx] = tag; c->misses++;
return 1 + MISS_PEN;
```

실제 출력:

```text
info: crafted worst: flat=247 warm-cache=377 cold-cache=377 all-miss bound=747; misses next call after RECAL=1, steady=0
info: random warm cache: p50=95 p99=132 max=149 | flush-each (cold): p50=135 p99=185 max=195 max_misses=6
```

| 상황 | 값 | 왜 |
|---|---|---|
| 평평한 메모리 (TCM, 0-wait SRAM) | 247 | 모든 접근 1 |
| 표적, 매 스텝 전 flush (cold) | 377 | 보정표 8줄 miss + 24 hit, LUT 는 보정표에 밀려 bp 4줄 + ff 1줄 miss |
| 표적, LUT 를 다 데운 뒤 (warm) | **377** | RECAL 이 LUT 8줄을 전부 밀어내므로 데워 둔 게 소용없다 (충돌 miss) |
| "항상 miss" 정적 상한 | 747 | 모든 접근 11 — 공간 지역성(한 줄에 4워드)을 전혀 안 믿은 값 |
| 무작위, warm | p50 95 / max 149 | max 149 > 141: 맨 처음 호출이 cold 라서 |
| 무작위, 매 호출 전 flush | p50 135 / max 195 | 선점당한 뒤를 가정. 그래도 표적 377 은 못 찾는다 |

세 가지 교훈.

1. **WCET 는 함수만의 성질이 아니다.** 같은 코드가 앞에 무엇이 돌았느냐(선점한 태스크가 캐시를 얼마나 갈아엎었나)에 따라 247 ~ 377. 선점이 만드는 이 추가 비용을 CRPD(cache-related preemption delay)라 부르고, RTA 의 C 에 더하거나 별도 항으로 넣는다.
2. **희귀 경로는 다음 호출까지 느리게 만든다.** RECAL 다음 호출(sp 1000)은 ff[4..5] 줄을 다시 읽어야 해 miss 1 — 흔한 경로의 측정 max 가 "RECAL 직후" 에서 갑자기 커진다. 트레이스에서 이런 2연속 이상치를 보면 앞 호출을 의심한다.
3. **"항상 miss" 는 안전하지만 2배 비관적.** 정적 도구(aiT 등)는 캐시 상태를 추상 해석(abstract interpretation)으로 추적해 "반드시 hit(must)" 인 접근을 골라내 이 틈을 줄인다 (Wilhelm et al. 2008 서베이 기준). 실무 대안은 캐시를 없애는 것: 제어 루프 코드·데이터를 ITCM/DTCM 에 두거나 캐시 라인 잠금(lockdown) — 그러면 평평한 247 쪽이 맞는 모델이 된다.

모델이 안 다루는 것: 명령어 fetch(플래시 대기 상태, 프리페치 — STM32F4 에서는 ART 의 명령 캐시가 더 중요할 수 있다), 파이프라인·분기 예측, 버스 경합(DMA, 다른 코어). 이 레슨의 캐시는 STM32F4 ART 의 데이터 캐시(8줄 × 128비트로 알려져 있음, RM0090 기준 — 이 레슨에서 미확인) 크기를 흉내 낸 것이다.

### Part C — I/O 지터: 즉시 쓰기 vs 래치

```c
uint64_t start = prev_end > rel ? prev_end : rel;
uint64_t end = start + exec[k];
uint64_t out = mode == IO_LATCHED ? (end + P - 1) / P * P : end;
if (out > rel + P) over++;
lat[k] = (uint32_t)(out - rel);
prev_end = end;
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">테스트 그대로: exec = {100, 377, 100}, 주기 P = 300 (한 칸 = 50)</text>
  <g stroke="currentColor"><line x1="80" y1="40" x2="80" y2="210"/><line x1="230" y1="40" x2="230" y2="210"/>
    <line x1="380" y1="40" x2="380" y2="210"/><line x1="530" y1="40" x2="530" y2="210"/><line x1="680" y1="40" x2="680" y2="210"/></g>
  <g fill="currentColor"><text x="74" y="230">0</text><text x="218" y="230">300</text><text x="368" y="230">600</text>
    <text x="518" y="230">900</text><text x="666" y="230">1200</text></g>
  <text x="6" y="70" fill="currentColor">실행</text>
  <rect x="80" y="55" width="50" height="22" class="f-mut" stroke="currentColor"/><text x="86" y="70" fill="currentColor">j0</text>
  <rect x="230" y="55" width="188" height="22" class="f-warn" stroke="currentColor"/><text x="236" y="70" fill="currentColor">j1 = 377 (주기 초과)</text>
  <rect x="418" y="55" width="50" height="22" class="f-mut" stroke="currentColor"/><text x="424" y="70" fill="currentColor">j2</text>
  <text x="6" y="120" fill="currentColor">즉시</text>
  <g fill="currentColor"><circle cx="130" cy="115" r="5"/><circle cx="418" cy="115" r="5"/><circle cx="468" cy="115" r="5"/></g>
  <text x="96" y="140" fill="currentColor">100</text><text x="400" y="140" fill="currentColor">377</text><text x="460" y="100" fill="currentColor">177</text>
  <text x="6" y="180" fill="currentColor">래치</text>
  <g fill="currentColor"><circle cx="230" cy="175" r="5"/><circle cx="530" cy="175" r="5"/></g>
  <text x="206" y="200" fill="currentColor">300</text><text x="440" y="200" fill="currentColor">900 경계: j1(600, 오버런)·j2(300) 둘 다</text>
  <text x="560" y="120" fill="currentColor">점 = 출력 반영 시각</text>
  <text x="560" y="140" fill="currentColor">숫자 = 출시부터 지연</text>
  <text x="440" y="160" fill="currentColor">→ j2 가 shadow 를 덮어써 j1 값은 안 나감</text>
</svg>
```

래치는 지연을 **항상 정확히 한 주기** 로 만든다 — 실행 시간이 17이든 149든 PWM 은 다음 경계에서 바뀐다. 실제 출력 (warm 무작위 2만 개를 P = 250 에):

```text
info: I/O latency P=250: immediate min=17 max=149 jitter=132 sd=15.6 | latched min=250 max=250 jitter=0 overruns=0
```

대가는 둘이다. ① 평균 지연이 늘어난다(95 → 250) — 제어 루프에서는 위상 지연이 고정되어 **보상할 수 있는** 지연이 되고, 흔들리는 지연은 보상할 수 없다. ② **WCET 가 주기를 넘으면 지연이 한 주기 통째로 뛴다** (오버런 → 600). 게다가 그림처럼 j1(끝 677)과 j2(끝 777)가 **같은 경계 900** 에 래치되므로 실제 레지스터에서는 j2 가 shadow 값을 덮어써 **j1 의 출력은 한 번도 PWM 에 안 나간다** — 모델의 `lat[1] = 600` 은 "가장 이른 반영 가능 시각" 이고, 진짜로는 값 하나가 사라진다. 그래서 래치 설계에서 WCET 상한은 "가끔 넘어도 되는 숫자" 가 아니라 **경계 조건** 이다. STM32 타이머에서는 CCR preload(OCxPE)를 켜고 ISR 에서 CCR 에 쓰면 다음 갱신 이벤트(UEV)에서 반영된다 (레퍼런스 매뉴얼 기준 일반적 구조). ADC 샘플 시점도 타이머 트리거로 고정하면 입력 쪽 지터도 같이 사라진다.

### 호스트에서 진짜로 재 보기 — `demo_wcet_host.c`

<!-- code: demo_wcet_host.c -->

같은 구조의 함수를 키워서(LUT 256칸, 보정표 4096개 = 16 KB) 6개 입력 클래스로 잰다. warm = 연속 5만 회, cold = 매 호출 전 64 MB 를 128 B 줄마다 읽어 캐시를 밀어낸 뒤 400회. 기계: Apple M6 (P 2 + E 4), Apple clang 21, `mach_timebase_info` = 125/3 → **한 칸 41.67 ns**. `uptime` load average 9.9–11.4 (다른 세션의 빌드·벤치가 동시에 돌았다). 세 번 실행: A·B = `QOS_CLASS_USER_INTERACTIVE`, C = 기본 QoS. 단위는 시계 칸(tick).

| warm, n = 50,000 | p50 | p99 | p99.9 | max A | max B | max C |
|---|---|---|---|---|---|---|
| 빈 구간 (측정 오버헤드) | 0 | 1 | 1 | 3 | 2 | 2 |
| hold (데드밴드) | 0 | 1 | 1 | 2 | 4 | 187 |
| nominal | 1 | 1 | 2 | 6 | 69 | 344 |
| high_sp (LUT 끝까지) | 2 | 3 | 3–4 | **7,664** (319 µs) | 5 | 69 |
| spike | 0 | 1 | 1–2 | 4 | 3 | 5 |
| recal (16 KB 합) | 5–6 | 9–12 | 10–14 | **8,702** (363 µs) | 5,487 | 79 |
| random | 1 | 3–4 | 4–6 | 1,412 | 176 | 6 |

| cold, n = 400 | p50 (A/B/C) | p99 (A/B/C) | max (A/B/C) |
|---|---|---|---|
| hold | 4 / 3 / 3 | 10 / 10 / 9 | 12 / 12 / 11 |
| nominal | 6 / 5 / 5 | 15 / 15 / 14 | 18 / 27 / 74 |
| high_sp | 9 / 6 / 6 | 20 / 17 / 16 | 28 / 24 / 146 |
| spike | 5 / 6 / 3 | 15 / 18 / 14 | 17 / 20 / 17 |
| recal | 20 / 20 / 13 | 43 / 45 / 35 | 110 / 118 / 303 |
| random | 7 / 7 / 6 | 18 / 18 / 17 | 26 / 22 / 19 |

64회를 한 구간으로 묶은 최솟값(평균 해상도 0.65 ns): hold 2.6–5.2 ns, nominal 26.7–35.2 ns, high_sp 74.2–115.2 ns, random 46.2–63.8 ns/호출.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">실행 B 실제 히스토그램 (막대 길이 = 개수, 1 칸 = 41.67 ns)</text>
  <text x="10" y="44" fill="currentColor">warm random (n=50,000, bin = 1 tick)</text>
  <g fill="currentColor">
    <text x="10" y="66">0</text><text x="10" y="84">1</text><text x="10" y="102">2</text><text x="10" y="120">3</text>
    <text x="10" y="138">4</text><text x="10" y="156">5..8</text><text x="10" y="174">≥11</text>
  </g>
  <rect x="50" y="56" width="85" height="13" class="f-acc" stroke="currentColor"/><text x="140" y="66" fill="currentColor">6,664</text>
  <rect x="50" y="74" width="300" height="13" class="f-acc" stroke="currentColor"/><text x="355" y="84" fill="currentColor">23,621</text>
  <rect x="50" y="92" width="191" height="13" class="f-acc" stroke="currentColor"/><text x="246" y="102" fill="currentColor">15,005</text>
  <rect x="50" y="110" width="52" height="13" class="f-acc" stroke="currentColor"/><text x="107" y="120" fill="currentColor">4,063</text>
  <rect x="50" y="128" width="7" height="13" class="f-acc" stroke="currentColor"/><text x="62" y="138" fill="currentColor">537</text>
  <rect x="50" y="146" width="2" height="13" class="f-acc" stroke="currentColor"/><text x="58" y="156" fill="currentColor">99</text>
  <rect x="50" y="164" width="2" height="13" class="f-warn" stroke="currentColor"/><text x="58" y="174" fill="currentColor">11 (최대 176 tick = 7.3 µs: OS 가 끼어듦)</text>
  <text x="400" y="44" fill="currentColor">cold recal (n=400, bin = 10 tick)</text>
  <g fill="currentColor">
    <text x="400" y="66">0..9</text><text x="400" y="84">10..19</text><text x="400" y="102">20..29</text><text x="400" y="120">30..39</text>
    <text x="400" y="138">40..59</text><text x="400" y="156">80..89</text><text x="400" y="174">≥110</text>
  </g>
  <rect x="460" y="56" width="27" height="13" class="f-mut" stroke="currentColor"/><text x="492" y="66" fill="currentColor">28</text>
  <rect x="460" y="74" width="160" height="13" class="f-mut" stroke="currentColor"/><text x="625" y="84" fill="currentColor">171</text>
  <rect x="460" y="92" width="119" height="13" class="f-mut" stroke="currentColor"/><text x="584" y="102" fill="currentColor">127</text>
  <rect x="460" y="110" width="52" height="13" class="f-mut" stroke="currentColor"/><text x="517" y="120" fill="currentColor">56</text>
  <rect x="460" y="128" width="15" height="13" class="f-mut" stroke="currentColor"/><text x="480" y="138" fill="currentColor">16</text>
  <rect x="460" y="146" width="2" height="13" class="f-warn" stroke="currentColor"/><text x="468" y="156" fill="currentColor">1</text>
  <rect x="460" y="164" width="2" height="13" class="f-warn" stroke="currentColor"/><text x="468" y="174" fill="currentColor">1 (118 tick = 4.9 µs)</text>
  <text x="10" y="210" fill="currentColor">왼쪽: 몸통은 1~3 칸에 몰리고 꼬리는 OS 잡음. 오른쪽: 같은 RECAL 이 warm p50 6 칸 → cold p50 20 칸 (3.3배).</text>
  <text x="10" y="230" fill="currentColor">max 는 실행마다 2 tick ~ 8,702 tick 으로 뛴다 — 호스트에서 max 는 함수의 성질이 아니라 그 순간 OS 의 성질이다.</text>
</svg>
```

읽는 법:

- **p50·p99 는 세 번 다 거의 같고, max 는 수천 배 흔들린다.** max 가 319 µs·363 µs 인 표본은 함수가 아니라 스레드가 선점·이동된 것이다 (load 10 근처). QoS 를 높여도 이상치가 없어지지 않았다 (A 가 가장 컸다). 호스트에서 "WCET" 를 말하려면 이 잡음을 걸러야 하는데, 거르는 순간 그건 WCET 가 아니라 "잡음 뺀 꼬리" 다.
- **해상도가 모자라다.** hold 는 실제 2.6–5.2 ns 인데 한 칸이 41.67 ns 라 0 칸 또는 1 칸으로만 보인다 (연습 3: 100 ns 구간은 2 칸 또는 3 칸, 10 ns 구간은 76% 확률로 0 칸). 빈 구간 오버헤드도 0~1 칸이다. 짧은 함수는 묶어서 평균을 재거나 사이클 카운터가 필요하다 — 호스트의 `cntvct_el0` 도 같은 24 MHz 라 도움이 안 되고, CPU 사이클 PMU 는 사용자 공간에서 바로 못 쓴다 (macOS, 이 레슨에서 시도 안 함).
- **cold 는 클래스에 따라 2~6배.** 64 MB 를 읽은 뒤 16 KB 보정표 합이 p50 208–250 ns → 542–833 ns. 모델의 247 → 377(1.5배)보다 큰 것은 진짜 DRAM miss 가 모델의 10 사이클보다 훨씬 비싸서다. 반면 LUT 탐색은 순차 접근이라 하드웨어 프리페처가 상당 부분 숨긴다 (high_sp cold p50 6–9 칸).
- **입력 클래스가 분포를 가른다.** random 하나만 재면 recal 클래스는 영원히 안 나온다 (Part B 와 같다). 측정 계획은 "입력 클래스 × 캐시 상태" 표로 짠다.

### 보드에서는 — DWT CYCCNT

<!-- code: demo_dwt_cyccnt.c -->

```c
DEMCR |= DEMCR_TRCENA;                      /* 0xE000EDFC bit 24: DWT 켜기 */
if (DWT_CTRL & DWT_CTRL_NOCYCCNT) return -1; /* bit 25: 카운터 없음 */
DWT_CYCCNT = 0;
DWT_CTRL |= DWT_CTRL_CYCCNTENA;             /* bit 0 */
```

Cortex-M4 로 컴파일한 측정 구간 (clang `--target=thumbv7em-none-eabi -O2`, 실제 asm 발췌):

```text
	movw	r6, #4100          @ 0xE0001004 = DWT_CYCCNT
	movt	r6, #57344
	ldr	r4, [r6]           @ t0
	bl	ctrl_step_board
	ldr	r0, [r6]           @ t1
	ldr.w	r1, [r5, #284]     @ s->overhead
	add	r1, r4
	sub.w	r0, r0, r1         @ d = t1 - t0 - overhead (모듈로 2^32)
```

- 측정 구간 = 레지스터 읽기 두 번 사이. 빈 구간 최솟값(`cyc_calibrate`)을 빼 준다. `uint32_t` 뺄셈이라 한 번 랩해도 맞다 (연습 2).
- `mask_irq = 1` 이면 PRIMASK 로 인터럽트를 막고 잰다 = **순수 실행 시간**(WCET 의 정의). 막지 않고 재면 **응답 시간**(선점 포함)이다. 둘 다 필요하고, 섞어서 보고하면 안 된다.
- 보드에서는 표본을 저장하지 않고 히스토그램 + max + **max 를 만든 입력(sp, meas)** 을 저장한다 — 새 최대가 나오면 그 입력을 호스트 모델로 재현해 경로를 확인한다.
- M0/M0+ (ARMv6-M) 에는 CYCCNT 가 없다 (ARMv6-M ARM 기준). SysTick `VAL`(24비트, 아래로 셈)이나 32비트 범용 타이머를 쓰고, 가장 믿을 만한 외부 방법은 **GPIO 토글 + 로직 분석기/오실로스코프** 다 (측정 대상 밖에서 재므로 OS·디버거 영향이 없다).

**QEMU 는 CYCCNT 를 안 돌린다 (실측).** Lima VM `elinux` 에서 같은 파일을 `-DQEMU_PROBE` 로 mps2-an385 펌웨어로 빌드했다 (arm-none-eabi-gcc 14.2.1, QEMU 10.2.1):

```bash
W=/tmp/RTOS-A03_work; mkdir -p $W && cp demo_dwt_cyccnt.c $W/ && cd $W
cat > probe.ld <<EOF
MEMORY { FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K }
_estack = ORIGIN(RAM) + LENGTH(RAM);
ENTRY(Reset_Handler)
SECTIONS { .isr_vector : { KEEP(*(.isr_vector)) } > FLASH  .text : { *(.text*) *(.rodata*) } > FLASH  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM }
EOF
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -std=c11 -Wall -Wextra -Werror -ffreestanding -nostdlib -DQEMU_PROBE \
    -T probe.ld demo_dwt_cyccnt.c -o probe.elf -lgcc
timeout 10 qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none \
    -semihosting-config enable=on,target=native -kernel probe.elf; echo "qemu exit=$?"
```

```text
DEMCR before   = 0x00000000
DEMCR after    = 0x00000000
DWT_CTRL       = 0x00000000
cyc_init: counter does not move
CYCCNT t0      = 0x00000000
CYCCNT t1      = 0x00000000
delta (100k)   = 0x00000000
overhead       = 0x00000000
qemu exit=2
```

DEMCR 쓰기가 남지 않고 DWT 는 전부 0 으로 읽힌다 (`-icount shift=0` 을 줘도 같았다). 함정: `NOCYCCNT` 비트도 0 이라 "카운터 있음" 으로 보인다 — 그래서 `cyc_init` 이 **켠 뒤 실제로 움직이는지** 확인하도록 고쳤다 (첫 실행에선 "init ok" 를 찍고 delta 0 이 나왔다). QEMU 는 명령어 수준 에뮬레이터라 사이클 타이밍 자체가 없다; WCET 는 진짜 보드나 사이클 정확 시뮬레이터에서 잰다.

### MBTA vs 정적 분석, 그리고 다음 단계

| | 측정 기반 (MBTA) | 정적 분석 (aiT, OTAWA 등) |
|---|---|---|
| 결과 | 관측 max = **하한** | 상한 (모델이 맞다면 안전) |
| 틀리는 곳 | 안 밟은 경로·캐시 상태·입력 (Part B/C) | 실행 불가능 경로·"항상 miss" 같은 비관 (281, 747) |
| 필요한 것 | 보드, 입력 클래스 설계, 커버리지 | 정확한 프로세서 모델, 루프 상한·흐름 주석 |
| 잘 맞는 하드웨어 | 무엇이든 | 단순 파이프라인·캐시 (M0~M7 급); 비순차 코어·공유 버스는 어렵다 |

aiT(AbsInt 상용)와 OTAWA(IRIT 오픈소스)의 지원 코어·정확도는 각 문서 기준이며 이 레슨에서 돌려 보지 않았다. 둘을 섞는 하이브리드(블록 단위로 재고 경로는 구조로 합치는 방식)도 있다 — 이 레슨의 "블록 비용 × CFG 최장 경로" 가 그 모양이다. 측정 분포의 꼬리에 극값 이론(EVT)을 맞춰 "10⁻⁹ 확률로 넘는 값" 을 내는 확률적 WCET(pWCET)는 **다음 단계** 로 둔다 (가정: 표본이 독립·동일 분포 — 캐시·상태가 있는 코드에선 그 가정부터 검증해야 한다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 42/42 passed (약 0.1 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_a && ./sol_a   # 42/42, 보고 없음
```

5회 연속 42/42 (결정적 — 출력이 바이트 단위로 같다). 시작 코드의 `(void)node_cost;` 한 줄은 스텁 상태에서 쓰이지 않는 함수 경고(`-Werror`)를 막기 위한 것이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 측정 max 를 그대로 RTA 의 C 로 | 하한일 뿐. 유효 입력만 넣으면 오류 경로를 안 밟는다 (141 vs 247) | 경로 커버리지 확인 + 표적 입력 + 정적 상한과 대조 |
| "max + 20% 여유" 로 끝 | 여유는 근거가 아니다 — 여기선 169 < 247 | 경로·캐시 상태별 최악을 따로 재고, 여유는 남은 미지수(측정 오차 등)에만 |
| p99.9 = max 라고 생각 | n = 1000 이면 두 번째로 큰 값 | `rank = ⌈q·n/10000⌉`, 하드 RT 에는 max |
| 순위 반올림/0 처리 실수 (`q*n/10000` 내림) | n=10 p50 이 rank 5 가 아니라 다른 값, q=0 이면 인덱스 −1 | 올림 + `max(1, ·)` + `min(n, ·)` |
| `q*n` 을 32비트로 | n 이 크면 넘친다 | `(uint64_t)q * n` |
| `x >= lo + nbins*width` | 32비트 덧셈 넘침 → 정상 값이 over 로 | `(uint64_t)(x - lo) / width >= nbins` |
| 히스토그램만 저장하고 max 버림 | over 칸 안의 값은 모른다 | `maxseen` 별도 기록 |
| `E[x²] − E[x]²` | 큰 기준값에서 상쇄 → 0 (실측) | Welford, 또는 기준값을 빼고 계산 |
| HWM 위치를 `>=` 로 갱신 | 마지막 위치가 남아 첫 발생 시점(트레이스와 맞출 지점)을 잃음 | `>` |
| 정적 상한 281 을 "진짜 WCET" 로 | 실행 불가능 경로 포함 — 비관적 (안전하긴 하다) | 흐름 정보(배타, 루프 상한) 추가 |
| 루프 안팎 상한을 곱해 SPIKE = 6+16·3 | 삽입 정렬 총 이동은 10 이 상한 | 총 반복 수 흐름 정보 |
| 캐시를 데운 루프에서만 측정 | 첫 호출·선점 후 호출은 cold (95 → 135, 247 → 377) | 매 측정 전 캐시 무효화/밀어내기 클래스 추가 |
| 측정 중 인터럽트를 안 막고 "실행 시간" 이라고 보고 | 응답 시간(선점 포함)과 섞인다 | PRIMASK 로 막은 측정과 안 막은 측정을 분리 |
| 호스트 측정 max 를 보고 | OS 잡음 (실측 2 ~ 8,702 tick) | 호스트는 원리·비율만; 수치는 보드 DWT |
| QEMU 에서 CYCCNT 로 잰다 | 전부 0 (실측) — `NOCYCCNT=0` 이라 속는다 | 켠 뒤 움직이는지 확인, 보드에서 측정 |
| 래치 출력인데 WCET > 주기 | 지연이 한 주기 통째로 뛴다 (600) | WCET 상한을 경계 조건으로 검증 |

## 8. 면접 Q&A

<details>
<summary>Q1. 측정한 최대 실행 시간을 WCET 로 믿어도 되나요?</summary>

아니다, **하한** 이다. 측정은 실제로 밟은 경로·캐시 상태·입력에 대해서만 말한다. 이 레슨에서 유효 ADC 값 2만 개 무작위는 141 을 봤지만 진짜 최악은 3연속 센서 고장 → 재보정 경로의 247, cold 캐시면 377 이었다. 믿을 수 있게 하려면 ① 블록·분기 커버리지로 "안 밟은 경로가 없는지" 보이고, ② 오류·복구 경로는 표적 입력(고장 주입)으로 밟고, ③ 캐시 상태를 클래스로 나눠(cold/warm, 선점 직후) 재고, ④ 가능하면 정적 상한과 대조한다. 꼬리: "여유 20% 면 되지 않나?" → 여유는 근거가 아니다. 여기서 1.2 × 141 = 169 < 247. 여유는 측정 오차·온도·클럭 같은 남은 불확실성에 쓰고, 경로 문제를 덮는 데 쓰면 안 된다.

</details>

<details>
<summary>Q2. p99, p99.9, max 중 무엇을 보고해야 하나요?</summary>

용도가 다르다. 하드 실시간(데드라인 한 번 놓치면 고장)에 RTA 로 넣는 C 는 max — 그것도 상한이어야 한다. p99.9 는 소프트 실시간 SLA, 회귀 감시(빌드마다 꼬리가 늘었나)에 좋다. 함정: nearest-rank 로 n = 1000 이면 p99.9 는 두 번째로 큰 값, n = 10 이면 p99 = max — 표본 수 없이 백분위만 말하면 의미가 없다. 그리고 보간하지 않는 nearest-rank 는 항상 실제 관측값이라 "그 입력을 재현" 할 수 있다. 꼬리: "보드에 RAM 이 없으면?" → 고정 bin 히스토그램 + max 를 따로. 히스토그램 백분위는 bin 위쪽 끝으로 보수적으로 낸다.

</details>

<details>
<summary>Q3. Cortex-M 에서 사이클 단위로 어떻게 재나요? 주의할 점은?</summary>

M3/M4/M7: `DEMCR.TRCENA`(0xE000EDFC bit 24) 켜고, `DWT_CTRL.NOCYCCNT`(bit 25) 확인, `DWT_CYCCNT = 0`, `CYCCNTENA`(bit 0). 구간 앞뒤로 읽어 `uint32_t` 뺄셈(한 번 랩까지 안전, 168 MHz 면 25.6 s 마다 랩), 빈 구간 최솟값을 오버헤드로 뺀다. 순수 실행 시간이면 PRIMASK 로 막고, 응답 시간이면 안 막는다. 함정: M7 일부에서 CoreSight 잠금(LAR 0xC5ACCE55) 문제가 보고된다(문서 기준, 미검증), 디버거가 DWT 를 같이 쓰면 설정이 바뀐다, **QEMU 는 DWT 를 0 으로 읽는다** (이 레슨에서 mps2-an385 로 실측 — NOCYCCNT 도 0 이라 "있음" 으로 속는다). M0/M0+ 는 CYCCNT 가 없어 SysTick·타이머나 GPIO 토글 + 로직 분석기를 쓴다. 꼬리: "가장 믿을 만한 방법?" → GPIO 토글을 스코프로. 대상 밖에서 재고, ISR 진입까지 포함한 실제 핀 시각이 보인다.

</details>

<details>
<summary>Q4. 캐시가 있는 Cortex-M7 에서 WCET 를 어떻게 다루나요?</summary>

같은 함수라도 앞에 무엇이 돌았는지에 따라 달라진다 (모델: 247 → 377, 호스트 실측 RECAL p50 6 → 20 칸). 선택지: ① 측정은 매번 캐시 무효화/밀어내기한 cold 클래스를 포함해 그 max 를 쓴다, ② 선점이 만드는 추가 miss(CRPD)를 RTA 에 별도 항으로 넣는다, ③ 아예 결정성을 사서 해결한다 — 제어 루프 코드·데이터를 ITCM/DTCM 에 두거나 캐시 라인 잠금, 그러면 "평평한 메모리" 모델이 맞는다. 이 레슨에서 본 함정 두 개: 희귀 경로(RECAL)가 같은 인덱스를 쓰는 LUT 를 밀어내 **warm 으로 데워 둔 게 소용없고**, **다음 호출까지** miss 가 남는다. 꼬리: "정적 분석 도구는 캐시를 어떻게 보나?" → 추상 해석으로 반드시 hit/반드시 miss 를 분류한다; "항상 miss" 로 두면 747 — 2배 비관적.

</details>

<details>
<summary>Q5. 정적 WCET 분석은 어떻게 동작하고, 왜 비관적인가요?</summary>

바이너리에서 CFG 를 만들고 ① 루프 상한(자동 또는 주석), ② 프로세서 모델(파이프라인·캐시)로 블록별 최악 비용, ③ 경로 분석(IPET: 블록 실행 횟수를 변수로 둔 ILP, Li & Malik 1995)으로 최장 경로를 구한다. 비관이 생기는 곳: 실행 불가능 경로 — 여기서 "고장이면 m = prev → 스파이크 불가" 같은 **값 상관관계** 를 모르면 281, 흐름 정보 하나로 247; 캐시·파이프라인 상태를 모르면 "항상 miss" 쪽으로 기운다; 루프 상한이 느슨하면(삽입 정렬 16 vs 10) 커진다. 꼬리: "코드로 도와줄 수 있나?" → 있다. 배타를 구조로 드러내기(`if (!valid) … else 필터`), 고정 상한 루프, 재귀·함수 포인터·데이터 의존 루프 피하기, 단일 출구. "WCET 친화 코딩" 이다.

</details>

<details>
<summary>Q6. 10 kHz 전류 루프 ISR 의 측정 max 가 예산의 60% 입니다. 출시하겠나요? 무엇을 바꾸겠나요?</summary>

그 숫자만으로는 못 한다. 먼저 묻는다: 측정에 오류 경로(센서 고장·재보정)가 들어갔나, cold 캐시/플래시 대기 상태 클래스가 있나, 더 높은 우선순위 ISR 이 끼어든 응답 시간인가 순수 실행 시간인가. 바꿀 것: ① 재보정 같은 무거운 복구를 ISR 밖으로 — ISR 은 "고장 플래그 + 안전 출력" 만 하고 재보정은 낮은 우선순위 태스크로 미룬다 (최악 경로 자체를 없앤다, RTOS-M02 의 지연 처리), ② 가변 비용 루프를 상수 시간으로 (LUT 선형 탐색 → 균일 간격 인덱스 계산), ③ 코드·데이터를 TCM 에, ④ 출력은 타이머 preload 로 래치해 I/O 지터 0. 그다음 정적 상한 또는 표적 입력 max 로 예산과 비교. 꼬리: "60% 면 여유가 크지 않나?" → 여유의 크기가 아니라 측정이 무엇을 덮었는지가 문제다. 이 레슨의 모델에서도 흔한 경로 max 대비 진짜 cold 최악은 2.7배였다.

</details>

<details>
<summary>Q7. 제어 루프의 출력 지터를 줄이려면?</summary>

지터의 원인은 실행 시간 변동(경로·캐시)과 시작 시각 변동(다른 ISR, 크리티컬 섹션)이다. 가장 강한 방법은 **출력 시각을 하드웨어에 맡기기**: 계산 결과를 타이머 CCR 의 shadow(preload)에 쓰고 다음 갱신 이벤트에서 반영 → 지연이 정확히 한 주기, 지터 0 (모델: 17..149 → 항상 250). 입력도 ADC 를 타이머로 트리거하면 샘플 시점이 고정된다. 대가: 평균 지연이 한 주기로 늘어나고(제어기 설계에서 보상 가능한 고정 위상 지연), WCET 가 주기를 넘으면 한 주기 통째로 밀린다 (오버런 → 600) — WCET 상한이 경계 조건이 된다. 소프트웨어 쪽: 가변 경로를 상수 시간화, 우선순위·선점 임계값 조정(BASEPRI 로 더 높은 ISR 만 허용), 크리티컬 섹션 단축.

</details>

<details>
<summary>Q8. 이 분석(측정이든 정적이든)이 틀리는 경우는요?</summary>

① 멀티코어·DMA 의 공유 버스·공유 캐시 간섭 — 단일 코어에서 잰 WCET 가 그대로 안 맞는다 (RTOS-A08). ② 비순차·추측 실행 코어의 **타이밍 이상(timing anomaly)**: 한 번의 cache miss 가 오히려 전체를 빠르게 할 수 있어 "부분 최악의 합 = 최악" 이 깨진다 (Lundqvist & Stenström 1999). Cortex-M 급 순차 파이프라인에서는 드물다고 알려져 있다. ③ 클럭·전압 변경(DVFS), 플래시 대기 상태 설정 변경, 온도. ④ 컴파일러·최적화 옵션 변경 — 바이너리가 바뀌면 다시 재야 한다 (CI 에서 회귀 측정). ⑤ 입력 모델이 틀림 — 이 레슨의 "유효 ADC 값만" 처럼. ⑥ 측정 도구 자체: 호스트 시계 해상도(41.67 ns), OS 잡음, QEMU 의 가짜 CYCCNT. 꼬리: "어떻게 알아채나?" → 현장에서 HWM 과 "max 를 만든 입력" 을 계속 기록해 설계 상한과 비교하는 런타임 감시(예산 초과 시 로그·안전 모드).

</details>

<details>
<summary>Q9. 호스트(맥/리눅스)에서 잰 숫자는 어디에 쓸 수 있나요?</summary>

알고리즘 비교·회귀 추세·입력 클래스별 상대 비용(이 레슨: high_sp 가 nominal 의 약 2.8배, cold recal 이 warm 의 2~4배)에는 쓸 수 있다. MCU 의 WCET 로는 못 쓴다: 다른 ISA·캐시·클럭, OS 선점(실측 max 가 2 ~ 8,702 칸으로 흔들림), 시계 해상도(41.67 ns 라 5 ns 함수는 0 칸). p50·p99 는 실행 간에 안정적이었고 max 는 아니었다 — 그래서 호스트에서는 백분위로 말하고, max 를 말할 땐 "OS 잡음 포함" 을 붙인다. 리눅스라면 `isolcpus`·`SCHED_FIFO`·`mlockall` 로 잡음을 줄일 수 있다 (LLAT 트랙). 꼬리: "그럼 결정적 모델은 왜?" → 비용 모델은 매번 같은 숫자를 내므로 CI 에서 "최악 경로 비용이 늘었나" 를 CHECK 로 지킬 수 있다.

</details>

## 9. 추가 연습

### 연습 1 — MCU 용 스트리밍 통계와 창 합치기

표본을 저장하지 않고 `st_push(s, x)` 로 min/max/HWM 위치/평균/m2 를 갱신하라. 두 측정 창(예: 1초 창 둘)의 통계를 `st_merge(a, b)` 로 합쳐 전체를 한 번에 넣은 것과 같은지 확인하라 (b 의 HWM 위치는 a.n 만큼 밀린다).

<details>
<summary>답 보기</summary>

push 는 Part A 의 Welford 그대로. 합치기는 Chan · Golub · LeVeque 의 병렬 공식: `δ = mean_b − mean_a`, `mean = mean_a + δ·n_b/n`, `m2 = m2_a + m2_b + δ²·n_a·n_b/n`. 빈 창은 그대로 반대쪽을 돌려준다. 실제 출력 `info: stream n=30000 max=941317 at #17321 mean=902506.342 sd^2=2132295.2 (two-pass 2132295.2)` — 기준값 90만에서도 두 번 훑은 값과 같다. ISR 마다 창 통계를 쌓고 태스크가 주기적으로 합쳐 보고하는 구조에 쓴다.

</details>

### 연습 2 — CYCCNT 랩과 릴리즈 지터

32비트 사이클 타임스탬프로 실행 시간 `dur_u32(t0, t1)` 과, 주기 P 로 출시돼야 하는 잡 시작 시각들의 릴리즈 지터(편차 max − min, 최대 지각/조기)를 구하라. 타임스탬프가 도중에 0 을 지나도 맞아야 한다.

<details>
<summary>답 보기</summary>

`t1 − t0` 를 `uint32_t` 로 하면 모듈로 2³² 라 한 번 랩까지 맞다. 편차는 `(int32_t)(start[k] − (start[0] + k·P))` — 이상적 시각도 같은 모듈로 산술로 만들고 차이만 부호 있게 해석한다 (RTOS-B06 의 tick 랩 비교와 같은 원리). 테스트: 168 MHz·10 kHz(P = 16800), 시작 0xFFFF0000, 편차 {0, 35, −12, 80, 3, −20} → 지터 100, 지각 80, 조기 −20. 한 바퀴 25.6 s 보다 긴 구간은 32비트로 구분이 안 된다 → 64비트로 넓히거나(오버플로 카운트) 더 느린 시계.

</details>

### 연습 3 — 시계 양자화

길이 D 인 구간을 눈금 q 인 시계로 재면 시작 위상에 따라 `⌊(φ+D)/q⌋ − ⌊φ/q⌋` 칸이 나온다. 41.67 ns 시계로 100 ns, 10 ns 구간을 재면 무엇이 나오나?

<details>
<summary>답 보기</summary>

단위를 10 ps 로 하면 q = 4167, D = 10000: 모든 위상에서 2 칸 또는 3 칸이고, 모든 위상에 대한 합이 정확히 D (`Σ = D`, 위상 평균 × q = 진짜 길이). 10 ns 는 4167 위상 중 3167 (76%)에서 0 칸. 그래서 한 번 잰 값은 ±1 칸 오차, **max 는 최대 1 칸 부풀고 min 은 1 칸 줄어든다** — demo 의 hold p50 0 칸, 빈 구간 max 2~3 칸이 이것이다. 짧은 함수는 묶어서 평균(batch)을 재거나 더 빠른 시계(CYCCNT)를 쓴다.

</details>

### 연습 4 — 무작위 테스트가 희귀 경로를 볼 확률

스텝마다 독립적으로 확률 p 로 고장이 날 때 n 스텝 안에 "3연속 고장" 이 한 번이라도 나올 확률을 정확히 구하고(상태 0/1/2 연속의 마르코프 DP), p = 1% 에서 99% 확신에 필요한 n 을 구하라.

<details>
<summary>답 보기</summary>

상태 확률 a0, a1, a2 를 한 스텝씩: `a0' = (a0+a1+a2)(1−p)`, `a1' = a0·p`, `a2' = a1·p`, 흡수 += `a2·p`. 실제 출력 `P(3-in-a-row within 20000 | p=1%) = 0.0196 ; runs for 99% = 4651673`. Part B 의 1% 주입 캠페인이 RECAL 을 못 본 것은 98% 확률로 예상된 일이다. 근사 `ln(100)/((1−p)p³) ≈ 4.65×10⁶` 과 맞는다. 교훈: 희귀 경로는 무작위로 "기다리지" 말고 표적 입력으로 밟는다. 라이브러리 `log` 없이 DP 만으로 풀어 libm 의존도 없다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed (약 0.1 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_a && ./pr_a   # 16/16, 보고 없음
```

## 10. 핵심 정리

- 측정 max 는 **하한**, 정적 분석은 **상한**. 이 모델에서 무작위 max 141, 진짜 247, 흐름 정보 없는 상한 281, cold 377, "항상 miss" 747.
- 무작위 테스트는 흔한 경로들 사이의 최악은 찾지만 **안 밟은 경로** 는 못 찾는다 — 커버리지를 보고, 오류·복구 경로는 표적 입력으로 밟는다 (3연속 고장 = 2만 번에 2%, 99% 엔 465만 번).
- nearest-rank: `rank = ⌈q·n/10000⌉` (64비트). n = 1000 의 p99.9 = 두 번째로 큰 값. 하드 RT 의 C 에는 max/상한.
- 보드에선 표본 대신 고정 bin 히스토그램(넘침 없는 bin 계산) + max + max 를 만든 입력. 분산은 Welford (교과서 공식은 큰 기준값에서 0).
- 실행 불가능 경로·느슨한 루프 상한이 정적 분석을 비관적으로 만든다 → 흐름 정보 또는 WCET 친화 코드 구조.
- 캐시: WCET 는 함수만의 성질이 아니다 (선점 후 cold, 희귀 경로가 다음 호출까지 오염). 결정성이 필요하면 TCM·잠금.
- I/O 지터는 출력을 타이머 갱신 이벤트에 래치해 0 으로 — 대신 지연 한 주기, WCET > 주기면 한 주기 통째로 밀린다.
- 측정 도구: 보드 = DWT CYCCNT (켜고 **움직이는지** 확인, M0 엔 없음, QEMU 는 0), 호스트 = `mach_absolute_time` 41.67 ns + OS 잡음 → 백분위·비율만.

## 참고자료

- R. Wilhelm et al., "The worst-case execution-time problem — overview of methods and survey of tools", ACM TECS 7(3), 2008 — MBTA vs 정적 분석, 캐시 분석, 도구 서베이
- Y.-T. S. Li, S. Malik, "Performance analysis of embedded software using implicit path enumeration", DAC 1995 — IPET
- T. Lundqvist, P. Stenström, "Timing anomalies in dynamically scheduled microprocessors", RTSS 1999
- B. P. Welford, "Note on a method for calculating corrected sums of squares and products", Technometrics 4(3), 1962; T. F. Chan, G. H. Golub, R. J. LeVeque — 병렬 분산 갱신 공식 (1979 Stanford 보고서 / 1983 The American Statistician)
- R. J. Hyndman, Y. Fan, "Sample quantiles in statistical packages", The American Statistician 50(4), 1996 — 백분위 정의들
- ARMv7-M Architecture Reference Manual (DDI0403) — C1.6 DWT (`DWT_CTRL` NOCYCCNT/CYCCNTENA, `DWT_CYCCNT`), C1.6 DEMCR TRCENA — <https://developer.arm.com/documentation/ddi0403/latest/>; ARMv6-M ARM (DDI0419) — DWT 에 사이클 카운터 없음
- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` 5230-5254 (run-time 카운터 누적, 넘침 보호 없음 주석), 5274 (`traceTASK_SWITCHED_IN` 호출), 8519-8550 (`ulTaskGetRunTimeCounter`); `include/FreeRTOS.h` 631-635 / 662-666 (trace 훅 기본 정의), 2697-2713 (`configGENERATE_RUN_TIME_STATS` 요구 매크로), 2876-2881 (`configRUN_TIME_COUNTER_TYPE` 기본 `uint32_t`) — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- AbsInt aiT — <https://www.absint.com/ait/>, OTAWA — <https://www.otawa.fr/> (문서 기준, 이 레슨에서 미사용)
- STMicroelectronics RM0090 (STM32F405/407…) — ART 가속기, TIM CCR preload — <https://www.st.com/resource/en/reference_manual/rm0090.pdf> (이 레슨에서 수치 미확인)
- Apple, `mach_absolute_time` / `mach_timebase_info` — <https://developer.apple.com/documentation/kernel/1462446-mach_absolute_time>
- 관련 레슨: RTOS-M11 (RTA 의 C), RTOS-A04 (확장 RTA: 지터 J·블로킹 B), RTOS-A06 (트레이스 훅으로 잡 단위 시간), RTOS-A08 (멀티코어 간섭), RTOS-M02 (무거운 일 ISR 밖으로), RTOS-B06 (랩 안전 비교), BM-B08 (QEMU mps2-an385 첫 부팅), LLAT-M05 (호스트 시계 해상도·핑퐁 지연), DS-A11 (캐시 친화 배치), TEST-A05 (결정적 시뮬레이션)

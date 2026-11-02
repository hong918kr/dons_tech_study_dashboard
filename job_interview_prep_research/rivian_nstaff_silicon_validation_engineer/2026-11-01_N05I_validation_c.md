# N05I · 검증형 C — 측정 · 통계 · 마진
<!-- theme: pastel -->

> **묶음**: I 검증형 C (타임스탬프 · 통계 · shmoo · 고정소수점) · **문제 수**: 8 (★★★ I1) · **목표**: 측정값을 **wrap·overflow·FP 없이** 숫자로 만드는 함수를 종이 20분에 쓰고 말로 설명 · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · **이전/다음**: [N05H C 함정·코드 읽기](2026-11-01_N05H_c_pitfalls_read_code.html) / 다음 없음 → [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · [온사이트 플랜](2026-10-31_rivian_onsite_plan.html) · **코드**: onsitePrep/n05/i/ (`./run.sh` → I1–I8 PASS)

## 0. TL;DR · 사용법

- 이 페이지는 **validation 팀이 매일 쓰는 작은 측정 함수**를 훈련합니다: frame 주기 통계, 온라인 평균/분산, latency percentile, golden 비교, shmoo window, 처리량, Q15, fmax 탐색. Rivian 자체 SoC(RAP1) 검증에서 카메라 frame 주기 · PCIe/DDR 처리량 · V/F shmoo가 나올 거라는 건 [추정].
- **외울 한 줄 ①**: `(uint32_t)(b - a)` — 같은 폭의 unsigned 빼기는 wrap을 한 번까지 공짜로 처리한다 (16-bit면 **cast 필수**).
- **외울 한 줄 ②**: "**Multiply before divide, but split so the multiply can't overflow**" — `q = x / d, r = x % d` → `q*k + r*k/d`.
- **외울 한 줄 ③**: percentile은 `rank = ceil(p·N)` 번째, 즉 `(N*pm + 999) / 1000` (per-mille 정수).
- ★★★ = **I1** (가장 나올 확률 높음). ★★ = I2 · I3 · I5 · I7 · I8. ★ = I4 · I6 (짧지만 overflow 함정 설명이 채점 포인트).
- 더 긴 30분 버전은 이미 있음 — 여기서는 **20분 종이 크기**만: [V4 벤치 통계](hmScreenPrep/problems/V4_perf_bench_stats/index.html) · [V7 shmoo](hmScreenPrep/problems/V7_shmoo_margin/index.html) · [L3 RFC 2544 탐색](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) · [V12 카메라 timestamp](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) · [V11 NPU golden](hmScreenPrep/problems/V11_npu_int8_golden/index.html) · 개념은 [성능 메트릭 노트](2026-10-30_perf_metrics_ssd_vs_soc.html) · [BV12 shmoo/PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html).
- FP 원칙: 펌웨어/ISR에서는 FP를 피한다 (FPU 없음, ISR에서 FP context 저장 비용, `printf("%f")` 코드 크기). 그래서 I1 · I3 · I4 · I5 · I6 · I8은 **정수만**, I2는 double 버전 + 정수 대안, I7은 FP 대신 쓰는 고정소수점 자체.

## 1. 문제지 (인쇄용 — 답 없음)

### I1 · Frame period stats from a wrapping 32-bit timestamp

Camera start-of-frame timestamps come from a free-running 32-bit counter (1 MHz tick) that wraps. Compute min / max / mean (floor) period and peak-to-peak jitter. Return -1 for NULL or fewer than 2 stamps.

```c
typedef struct { uint32_t min, max, mean, jitter_pp; } period_stats_t;
int period_stats(const uint32_t *ts, size_t n, period_stats_t *out);
```

Example: `{4294940000, 6037, 39372, 72700}` → min 33328, max 33335, mean 33332, jitter_pp 7.

### I2 · Online mean / variance (Welford) without storing samples

Keep a running mean and sample variance of up to 2^32 samples using O(1) memory.

```c
typedef struct { uint32_t n; double mean; double m2; } welford_t;
void   welford_init(welford_t *w);
void   welford_add(welford_t *w, double x);
double welford_var(const welford_t *w);   /* sample variance, n-1; 0 if n < 2 */
```

Example: add `2,4,4,4,5,5,7,9` → mean 5.0, var 32/7 ≈ 4.571.

### I3 · Latency histogram → p50 / p99

Fixed-width buckets (bucket i = `[i*width, (i+1)*width)`, last bucket = overflow). Write `hist_add` and a nearest-rank percentile that returns the bucket index (`pm` = per-mille, 1..1000), or -1.

```c
void hist_add(uint32_t *hist, size_t nb, uint32_t width, uint32_t sample);
int  hist_percentile(const uint32_t *hist, size_t nb, uint32_t pm);
```

Example: `hist = {0, 50, 40, 9, 1}` → p50 (500) = bucket 1, p99 (990) = bucket 3.

### I4 · Golden compare with abs/rel tolerance, report first N

Compare `got[]` vs `exp[]` (int32). Pass if `|got-exp| <= abs_tol + |exp| * rel_ppm / 1e6`. Store the first `max_out` mismatches, return the total count. No overflow for any input.

```c
typedef struct { size_t idx; int32_t got, exp; } mismatch_t;
size_t golden_compare(const int32_t *got, const int32_t *exp, size_t n,
                      uint32_t abs_tol, uint32_t rel_ppm,
                      mismatch_t *out, size_t max_out);
```

Example: exp `{100,-50,1000,INT32_MIN}`, got `{101,-50,1012,INT32_MAX}`, abs 1, rel 10000 ppm → returns 2 (idx 2, 3).

### I5 · Shmoo grid → per-voltage pass window and margin center

`grid[r*ncols + c]` is 1 (pass) / 0 (fail); rows = voltage steps, cols = delay taps. Find the longest consecutive pass run in a row, its center, and the row with the widest window.

```c
typedef struct { int start; int len; } window_t;
window_t longest_pass(const uint8_t *row, int n);
int      window_center(window_t w);
int      best_row(const uint8_t *grid, int nrows, int ncols, window_t *w);
```

Example: row `..#######.` → start 2, len 7, center 5.

### I6 · Throughput in MB/s, integer only, no overflow

`bytes` moved in `cycles` ticks of a `clk_hz` counter → MB/s × 100 (1 MB = 10^6 B), exact floor. Return `UINT64_MAX` on overflow, 0 if `cycles == 0`.

```c
uint64_t mbps_x100(uint64_t bytes, uint32_t cycles, uint32_t clk_hz);
```

Example: `(4194304, 1000000, 1000000000)` → 419430 (= 4194.30 MB/s).

### I7 · Q15 multiply with rounding and saturation

Multiply two Q15 numbers (`int16_t`, value = x / 32768), round half up, saturate to `[-32768, 32767]`.

```c
int16_t q15_mul(int16_t a, int16_t b);
```

Example: `q15_mul(0x4000, 0x4000)` → `0x2000` (0.5 × 0.5 = 0.25); `q15_mul(-32768, -32768)` → 32767.

### I8 · Binary-search the max passing frequency (flaky-aware)

Frequency grid `f_lo + i*step`, `i in [0, n)`. Assume monotone (pass below fmax, fail above). A point passes only if `k` runs in a row pass. Return the highest passing frequency, 0 if `f_lo` fails.

```c
typedef bool (*freq_test_fn)(void *ctx, uint32_t mhz);
uint32_t find_fmax(freq_test_fn t, void *ctx, uint32_t f_lo, uint32_t step,
                   uint32_t n, uint32_t k);
```

Example: grid 800..2000 step 10 (n = 121), true fmax 1430 → returns 1430 with ≤ 8 test calls (k = 1).

## I1 · 32-bit 타임스탬프 delta (wrap 안전) → 주기 min/max/mean/jitter (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 계속 증가하다 2^32에서 0으로 돌아가는 타임스탬프 배열을 받아, 연속 두 값의 간격(주기)의 최소·최대·평균·peak-to-peak jitter를 구한다.
- **상황**: 카메라가 frame을 보낼 때마다 SoC의 capture 블록이 start-of-frame 순간의 free-running counter 값을 기록한다. 30 fps면 주기 ≈ 33,333 µs. validation에서는 "frame 주기가 spec 안에 있나, 흔들림(jitter)이 얼마인가"를 매일 본다. counter는 32-bit라 측정 도중 wrap할 수 있다. (Rivian 카메라 파이프라인 구체 구성은 [추정])
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `period_stats(ts, n, out)` | 연속 간격 n-1개로 통계 채움 | min, max, mean(내림), jitter_pp = max − min · 성공 0 |
| (에러) | 잘못된 입력 | `ts`/`out` NULL 또는 n < 2 → -1 |

- **예시를 말로 풀면**: 첫 값 4294940000은 wrap 직전, 두 번째 6037은 wrap 직후. 실제 간격은 33333이다. 이후 간격 33335, 33328. 최소 33328, 최대 33335, 평균 99996/3 = 33332, jitter 7.
- **지켜야 할 조건**: 실제 간격은 항상 2^32 tick보다 작다고 가정. 합계 overflow 없어야 함. 라이브러리 없이.

### 1. Problem (English)

You get an array of 32-bit start-of-frame timestamps from a free-running 1 MHz counter that wraps at 2^32. Compute the min, max and mean (floor) frame period and the peak-to-peak jitter. Return 0 on success, -1 if a pointer is NULL or there are fewer than 2 timestamps.

```c
typedef struct { uint32_t min, max, mean, jitter_pp; } period_stats_t;
int period_stats(const uint32_t *ts, size_t n, period_stats_t *out);
```

- `{4294940000, 6037, 39372, 72700}` → min 33328, max 33335, mean 33332, jitter_pp 7
- `{0xFFFFFFFF, 0, 1}` → min = max = mean = 1, jitter_pp 0
- `{100, 100, 99}` → min 0, max 4294967295 (the "99" is read as almost one full wrap later)

### 2. 먼저 물어볼 것 (clarify)

- "Can I assume consecutive stamps are less than one full wrap apart (2^32 ticks ≈ 71 minutes at 1 MHz)?" → Yes.
- "Is jitter peak-to-peak (max − min), or deviation from a nominal period?" → Peak-to-peak; nominal is a follow-up.
- "Integer mean rounded down is OK?" → Yes.
- "Do I need to detect dropped frames?" → Not now.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: wrap 횟수 가정, jitter 정의 |
| 2–5 | 예시 첫 간격을 손으로: 6037 − 4294940000 mod 2^32 = 33333 |
| 5–15 | `tick_delta` 헬퍼 → 루프(min/max/sum) → 결과 |
| 15–18 | trace 3스텝 |
| 18–20 | 테스트 5개 + 확장(nominal 편차, 16-bit counter) |

- 소리 내서: "**Unsigned subtraction in the same width is modulo 2^32, so `(uint32_t)(b - a)` gives the right delta across one wrap — no if-statement needed.**"
- 소리 내서: "**I accumulate the sum in 64 bits, because n − 1 deltas of up to 2^32 each overflow a 32-bit sum.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

wrap을 if문(`if (b < a) d = b + (UINT32_MAX - a) + 1`)으로 처리하려 하지 말고, unsigned 산술이 원래 mod 2^32라는 걸 이용하세요.

</details>

<details><summary>힌트 2</summary>

`min`은 `UINT32_MAX`, `max`는 0에서 시작. 합은 `uint64_t`. 간격 개수는 `n - 1`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t min, max;   /* shortest / longest period (ticks) */
    uint32_t mean;       /* floor(sum of deltas / count)      */
    uint32_t jitter_pp;  /* max - min (peak-to-peak)           */
} period_stats_t;

static inline uint32_t tick_delta(uint32_t a, uint32_t b)
{
    return (uint32_t)(b - a);   /* modulo 2^32: wrap-safe if real gap < 2^32 */
}

/* Returns 0 on success, -1 on bad args (NULL or fewer than 2 stamps). */
int period_stats(const uint32_t *ts, size_t n, period_stats_t *out)
{
    if (ts == NULL || out == NULL || n < 2)
        return -1;
    uint32_t mn = UINT32_MAX, mx = 0;
    uint64_t sum = 0;           /* n-1 deltas, each < 2^32: no overflow */
    for (size_t i = 1; i < n; i++) {
        uint32_t d = tick_delta(ts[i - 1], ts[i]);
        if (d < mn) mn = d;
        if (d > mx) mx = d;
        sum += d;
    }
    out->min = mn;
    out->max = mx;
    out->mean = (uint32_t)(sum / (n - 1));
    out->jitter_pp = mx - mn;
    return 0;
}
```

시간 O(n), 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| i | ts[i-1] | ts[i] | d = (uint32_t)(b − a) | mn | mx | sum |
|---|---|---|---|---|---|---|
| 1 | 4294940000 | 6037 | 33333 | 33333 | 33333 | 33333 |
| 2 | 6037 | 39372 | 33335 | 33333 | 33335 | 66668 |
| 3 | 39372 | 72700 | 33328 | 33328 | 33335 | 99996 |
| 끝 | | | | | | mean = 99996/3 = 33332, jitter = 7 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 + wrap: 예시 → 33328/33335/33332/7.
- 정확히 `0xFFFFFFFF → 0` 경계: 간격 1.
- 같은 타임스탬프 두 번(간격 0) → min 0. 그리고 "뒤로 간" 값 `100 → 99`는 2^32−1로 읽힘 → 이건 버그가 아니라 가정 위반이라고 말하기 (V12처럼 out-of-order 검출은 따로).
- n = 0, n = 1, NULL → -1.
- 일정 주기(전부 같은 간격) → jitter 0.

</details>

### 4. 꼬리 질문

Q1. "Why not compute the mean as `(ts[n-1] - ts[0]) / (n-1)`?"

<details><summary>답</summary>

That only works if the whole capture spans less than one wrap — 2^32 ticks is about 71.6 minutes at 1 MHz but only 4.3 seconds at 1 GHz. Summing per-interval deltas in 64 bits works for any capture length, as long as each single interval is under one wrap.
한국어 핵심: 전체 구간이 한 번 이상 wrap하면 끝−처음 방식은 틀린다 — 간격별 합이 안전.

</details>

Q2. "How would you report jitter against the nominal 33,333 µs period?"

<details><summary>답</summary>

Track the worst absolute deviation from nominal; compute it unsigned with a branch so there's no signed overflow. A dropped frame would show up as a delta near 2× nominal, so in practice I'd also count deltas above 1.5× nominal.
한국어 핵심: `|d − P|`는 unsigned 두 갈래로; 2P 근처 간격 = frame drop.

```c
/* Follow-up: worst deviation from the nominal period, |d - nominal|. */
uint32_t max_dev_from_nominal(const uint32_t *ts, size_t n, uint32_t nominal)
{
    uint32_t worst = 0;
    for (size_t i = 1; i < n; i++) {
        uint32_t d = tick_delta(ts[i - 1], ts[i]);
        uint32_t dev = (d > nominal) ? d - nominal : nominal - d;
        if (dev > worst) worst = dev;
    }
    return worst;
}
```

</details>

Q3. "The capture block only gives a 16-bit counter. What changes?"

<details><summary>답</summary>

`b - a` on `uint16_t` is done in `int` after integer promotion, so 0 − 65535 is −65535, not 1. I must cast the result back to `uint16_t`. And now each interval must be under 65536 ticks, so the tick rate has to be low enough.
한국어 핵심: 16-bit는 int 승격 때문에 **cast가 정답의 일부** (검증: 캐스트 없이 −65535).

```c
/* Follow-up trap: 16-bit counter. b - a is done in int (promotion) and can be
 * negative, so the cast back to uint16_t is REQUIRED. */
static inline uint16_t tick_delta16(uint16_t a, uint16_t b)
{
    return (uint16_t)(b - a);
}
```

</details>

Q4. "Can this run in the frame interrupt instead of post-processing?"

<details><summary>답</summary>

Yes — keep last timestamp, min, max, 64-bit sum and count in a struct; each ISR does O(1) work. The reader in task context copies the struct with the interrupt masked (or uses a sequence counter), because the 64-bit sum is not read atomically on a 32-bit core.
한국어 핵심: ISR에서 O(1) 갱신, 읽는 쪽은 IRQ 마스크 후 복사 (64-bit는 32-bit 코어에서 원자적이지 않음).

</details>

### 5. 채점 · 자주 틀리는 것

- wrap을 if문으로 처리하다 +1 빠뜨림 → `(uint32_t)(b - a)` 한 줄.
- 합을 `uint32_t`에 누적 → overflow.
- `min`을 0으로 초기화 → 항상 0.
- 간격 개수를 n으로 나눔 (n − 1이 맞음).
- n < 2 체크 빠뜨려 `n - 1 == 0`으로 나누기 (또는 size_t underflow).

## I2 · 온라인 평균/분산 (Welford) — 샘플 저장 없이 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 샘플이 하나씩 들어올 때마다 평균과 분산을 갱신하는 누산기를 만든다. 배열에 저장하지 않는다.
- **상황**: DDR read latency나 PLL lock 시간을 10^6번 재는데, 타깃 RAM은 몇 KB뿐이다. 다 저장할 수 없으니 들어오는 대로 통계를 갱신해야 한다. 또 큰 값(예: 10^9 tick)에 작은 흔들림이 얹힌 데이터라 "합과 제곱합" 공식은 정밀도가 무너진다.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `welford_init(w)` | 상태 0으로 | n = 0 |
| `welford_add(w, x)` | 샘플 1개 반영 | O(1), 저장 없음 |
| `welford_var(w)` | 표본 분산 | n − 1로 나눔, n < 2면 0 |

- **예시를 말로 풀면**: 2,4,4,4,5,5,7,9를 넣으면 평균 5, 편차 제곱합 32, 표본 분산 32/7 ≈ 4.571 (모분산은 4).
- **지켜야 할 조건**: 샘플 저장 금지. 값이 크고 분산이 작아도 정확해야 함.

### 1. Problem (English)

Implement a streaming accumulator for mean and sample variance. You cannot store the samples. It must stay accurate when samples are large (about 1e9) but vary by only a few units.

```c
typedef struct { uint32_t n; double mean; double m2; } welford_t;
void   welford_init(welford_t *w);
void   welford_add(welford_t *w, double x);
double welford_var(const welford_t *w);
```

- add `2,4,4,4,5,5,7,9` → mean 5.0, var 4.5714…
- add `1e9+4, 1e9+7, 1e9+13, 1e9+16` → mean 1e9+10, var 30
- no samples or one sample → var 0

### 2. 먼저 물어볼 것 (clarify)

- "Sample variance (n − 1) or population (n)?" → Sample.
- "Is double available on this target, or do you want integer-only?" → Double for now; integer is a follow-up.
- "Do you need standard deviation too?" → No, variance is enough (sqrt needs libm).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: n−1, FP 허용 여부 |
| 2–5 | 왜 sum/sumsq가 위험한지 한 문장, Welford 갱신식 말하기 |
| 5–15 | struct → init → add → var |
| 15–18 | 2,4,4,4로 trace |
| 18–20 | 테스트 + 확장(정수 버전, 병합) |

- 소리 내서: "**The naive formula sumsq − sum²/n subtracts two huge, nearly equal numbers, so all the significant digits cancel. Welford updates the mean incrementally and only accumulates small squared deviations.**"
- 소리 내서: "**The trick is multiplying the deviation from the old mean by the deviation from the new mean.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

평균은 `mean_new = mean_old + (x − mean_old) / n` 로 갱신됩니다. 분산 쪽에 필요한 건 "편차 제곱합" `m2` 하나.

</details>

<details><summary>힌트 2</summary>

`d = x − mean_old`를 먼저 저장하고 mean을 갱신한 뒤, `m2 += d * (x − mean_new)`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

typedef struct {
    uint32_t n;
    double mean;
    double m2;   /* sum of squared deviations from the running mean */
} welford_t;

void welford_init(welford_t *w)
{
    w->n = 0;
    w->mean = 0.0;
    w->m2 = 0.0;
}

void welford_add(welford_t *w, double x)
{
    w->n++;
    double d = x - w->mean;      /* deviation from the OLD mean */
    w->mean += d / w->n;
    w->m2 += d * (x - w->mean);  /* times deviation from the NEW mean */
}

/* Sample variance (divide by n-1); 0 when fewer than 2 samples. */
double welford_var(const welford_t *w)
{
    return (w->n < 2) ? 0.0 : w->m2 / (w->n - 1);
}
```

시간 O(1)/샘플, 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| n | x | d = x − mean_old | mean | m2 += d·(x − mean) |
|---|---|---|---|---|
| 1 | 2 | 2 | 2 | 0 |
| 2 | 4 | 2 | 3 | 2 |
| 3 | 4 | 1 | 3.333 | 2.667 |
| 4 | 4 | 0.667 | 3.5 | 3.0 → var = 3/3 = 1 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 2,4,4,4,5,5,7,9 → 5, 32/7.
- 0개 / 1개 → var 0 (0으로 나누기 없음).
- 큰 offset: 1e9 + {4,7,13,16} → var 30. 같은 데이터를 float sum/sumsq로 하면 **0이 나옴** (이 머신에서 실행 확인: `naive float var = 0`).
- 전부 같은 값 → var 0.
- 두 묶음으로 나눠 병합한 결과 = 한 번에 넣은 결과 (꼬리 Q3).

</details>

### 4. 꼬리 질문

Q1. "What's actually wrong with keeping sum and sum of squares?"

<details><summary>답</summary>

With samples near 1e9 the squares are near 1e18, and their difference is only around 100. A float has 24 bits of mantissa, so the difference is completely lost — I ran it and got a variance of 0 instead of 30. Even double loses digits for larger offsets; Welford avoids the subtraction.
한국어 핵심: 큰 수 − 큰 수 = catastrophic cancellation (float로 돌리면 30 대신 0).

</details>

Q2. "No FPU, or FP is not allowed in the ISR. How would you do it with integers?"

<details><summary>답</summary>

Shift every sample by the first one so the numbers stay small, then keep an int64 sum and uint64 sum of squares of the shifted values. With deviations under 2^20 and fewer than 2^20 samples nothing overflows, and integer arithmetic has no cancellation problem — it's exact until the final divide.
한국어 핵심: 첫 샘플을 빼서(shift) 작게 만든 뒤 정수 합/제곱합 — 범위 가정을 반드시 말하기.

```c
/* Follow-up: integer-only (no FPU / no FP in ISR). Shift every sample by the
 * first one (k) so the sums stay small. Assumes |x - k| < 2^20 and n < 2^20. */
typedef struct { uint32_t n, k; int64_t s; uint64_t ss; } istat_t;

void istat_add(istat_t *st, uint32_t x)
{
    if (st->n == 0) st->k = x;
    int64_t d = (int64_t)x - st->k;
    st->n++;
    st->s += d;
    st->ss += (uint64_t)(d * d);
}

/* floor-ish: mean = k + s/n ; var = (ss - (s/n)*s) / (n-1) */
uint32_t istat_mean(const istat_t *st) { return st->k + (uint32_t)(st->s / st->n); }
uint64_t istat_var(const istat_t *st)
{
    if (st->n < 2) return 0;
    int64_t md = st->s / (int64_t)st->n;
    return (uint64_t)((int64_t)st->ss - md * st->s) / (st->n - 1);
}
```

</details>

Q3. "Each of 4 cores collected its own stats. How do you combine them?"

<details><summary>답</summary>

Welford states merge exactly with Chan's formula: the combined m2 is m2a + m2b + d² · na·nb/n, where d is the difference of the means. So each core runs its own accumulator with no locking and I merge at the end.
한국어 핵심: 코어별 누산 → 마지막에 병합 (lock 없음).

```c
/* Follow-up: merge two accumulators (e.g. per-core stats) - Chan et al. */
void welford_merge(welford_t *a, const welford_t *b)
{
    if (b->n == 0) return;
    uint32_t n = a->n + b->n;
    double d = b->mean - a->mean;
    a->m2 += b->m2 + d * d * ((double)a->n * b->n / n);
    a->mean += d * b->n / n;
    a->n = n;
}
```

</details>

### 5. 채점 · 자주 틀리는 것

- mean을 먼저 갱신하고 `d`를 새 mean으로 계산 → 공식이 틀어짐 (old mean으로 d 저장이 먼저).
- `d / w->n`에서 n을 증가시키기 전에 나눔 → 첫 샘플에 0으로 나누기.
- n < 2에서 0으로 나누기.
- 모분산/표본분산 구분 말 안 함.
- 정수 버전에서 범위 가정을 말하지 않음.

## I3 · latency 히스토그램 버킷 → p50/p99 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: latency 샘플을 고정 폭 버킷 히스토그램에 넣고, 히스토그램만으로 p50·p99가 어느 버킷인지 찾는다.
- **상황**: PCIe MMIO read 왕복 시간이나 frame-done 인터럽트 latency를 수백만 번 잰다. 평균은 꼬리(tail)를 숨기니 p99가 필요한데, 샘플을 정렬하려면 메모리가 없다. 그래서 펌웨어는 카운터 배열(히스토그램)만 유지한다.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `hist_add(hist, nb, width, sample)` | 해당 버킷 +1 | 버킷 i = `[i*width, (i+1)*width)`, 범위 밖은 마지막(overflow) 버킷 · nb 0 또는 width 0이면 무시 |
| `hist_percentile(hist, nb, pm)` | percentile이 든 버킷 번호 | pm = 천분율(500 = p50, 990 = p99) 1..1000 · nearest rank · 비었거나 잘못된 pm → -1 |

- **예시를 말로 풀면**: 버킷 `{0, 50, 40, 9, 1}`, 총 100개. p50 = 50번째로 작은 값 → 누적이 처음 50 이상이 되는 버킷 1. p99 = 99번째 → 누적 0, 50, 90, 99 → 버킷 3.
- **지켜야 할 조건**: 정수만. 정렬 금지. 총합 overflow 주의.

### 1. Problem (English)

Latency samples go into a fixed-width histogram; the last bucket also collects everything that's too large. Write `hist_add` and a nearest-rank percentile on the histogram that returns the bucket index.

```c
void hist_add(uint32_t *hist, size_t nb, uint32_t width, uint32_t sample);
int  hist_percentile(const uint32_t *hist, size_t nb, uint32_t pm);  /* pm 1..1000 */
```

- `{0, 50, 40, 9, 1}`, pm 500 → 1; pm 990 → 3; pm 1000 → 4
- `{0, 0, 0}` → -1
- width 100, add 5000 into 4 buckets → goes to bucket 3

### 2. 먼저 물어볼 것 (clarify)

- "Which percentile definition — nearest rank, or interpolated?" → Nearest rank: the ceil(p·N)-th smallest.
- "Return the bucket index or a latency value?" → Index; caller converts.
- "Percentile as float 0.99 or integer?" → Integer per-mille so no FP.
- "What if the percentile lands in the overflow bucket?" → Return it; caller flags "≥ limit".

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: nearest rank, 단위(per-mille), overflow 버킷 |
| 2–5 | 예시 p99를 누적합으로 손으로 |
| 5–15 | hist_add(클램프) → total → rank → 누적 스캔 |
| 15–18 | trace p99 |
| 18–20 | 테스트 + 확장(log2 버킷, ISR) |

- 소리 내서: "**The p-th percentile is the sample with rank ceil(p·N); I scan the cumulative count until it reaches that rank.**"
- 소리 내서: "**I keep p as per-mille and do the ceiling in integers: (N·pm + 999) / 1000.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

먼저 총 샘플 수 N을 구하세요. "목표 순위" rank를 정수 올림으로 만든 뒤, 앞에서부터 누적.

</details>

<details><summary>힌트 2</summary>

`rank = (total * pm + 999) / 1000`, `total`은 `uint64_t`. `cum >= rank`가 되는 첫 버킷이 답.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

/* bucket i holds samples in [i*width, (i+1)*width); last bucket = overflow */
void hist_add(uint32_t *hist, size_t nb, uint32_t width, uint32_t sample)
{
    if (hist == NULL || nb == 0 || width == 0)
        return;
    size_t i = sample / width;
    if (i >= nb)
        i = nb - 1;                        /* clamp into overflow bucket */
    hist[i]++;
}

/* pm = per-mille: 500 = p50, 990 = p99, 999 = p99.9 (valid 1..1000).
 * Returns the bucket index holding the nearest-rank percentile, or -1. */
int hist_percentile(const uint32_t *hist, size_t nb, uint32_t pm)
{
    if (hist == NULL || pm == 0 || pm > 1000)
        return -1;
    uint64_t total = 0;
    for (size_t i = 0; i < nb; i++)
        total += hist[i];
    if (total == 0)
        return -1;
    uint64_t rank = (total * pm + 999) / 1000;   /* ceil(p * N), >= 1 */
    uint64_t cum = 0;
    for (size_t i = 0; i < nb; i++) {
        cum += hist[i];
        if (cum >= rank)
            return (int)i;
    }
    return -1;                             /* unreachable */
}
```

시간 O(nb), 공간 O(1). 샘플 추가는 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`hist = {0, 50, 40, 9, 1}`, pm = 990 → total 100, rank = (99000 + 999) / 1000 = 99

| i | hist[i] | cum | cum ≥ 99? |
|---|---|---|---|
| 0 | 0 | 0 | no |
| 1 | 50 | 50 | no |
| 2 | 40 | 90 | no |
| 3 | 9 | 99 | **yes → return 3** |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: p50 → 1, p99 → 3, p100 → 4, p0.1(pm 1) → 첫 비지 않은 버킷 1.
- 올림 확인: pm 501 → rank 51 → 버킷 2.
- 빈 히스토그램, nb 0, pm 0, pm 1001, NULL → -1.
- 경계: 샘플 99 → 버킷 0, 100 → 버킷 1, 5000 → overflow 버킷 3; width 0 → 무시.
- 샘플 1개: 모든 percentile이 같은 버킷.

</details>

### 4. 꼬리 질문

Q1. "What latency number do you put in the report?"

<details><summary>답</summary>

The bucket's upper edge, (i+1)·width − 1, because it's a guaranteed upper bound — the error is at most one bucket width, so I'd state the resolution with the number. If the answer is the overflow bucket I report "≥ (nb−1)·width" and flag it, never a fake number.
한국어 핵심: 상한값으로 보고(보수적) + 해상도 함께, overflow면 "이상"으로.

</details>

Q2. "Latency ranges from 100 ns to 10 ms. Fixed buckets don't work. What then?"

<details><summary>답</summary>

Use log2 buckets: the bucket is the bit length of the sample, so 33 buckets cover the whole 32-bit range. Relative error is up to 2×, so in practice you split each power of two into a few linear sub-buckets (the HdrHistogram idea).
한국어 핵심: 비트 길이 = 버킷 (33칸), 정밀도가 필요하면 2의 거듭제곱마다 선형 하위 버킷.

```c
/* Follow-up: log2 buckets (0, 1, 2-3, 4-7, ...) cover ns..s in 33 buckets. */
size_t log2_bucket(uint32_t x)
{
    size_t b = 0;
    while (x) { b++; x >>= 1; }   /* b = bit length: 0 -> 0, 1 -> 1, 2..3 -> 2 */
    return b;
}
```

</details>

Q3. "hist_add is called from an ISR. Any issue?"

<details><summary>답</summary>

With a single writer (one ISR) the increment is fine. The reader in task context sees counts from different moments while it scans, so for a consistent snapshot I'd mask the interrupt while copying, or keep two histograms and swap a pointer. If two ISRs on different priorities both add, `hist[i]++` is a read-modify-write and needs an atomic or IRQ masking.
한국어 핵심: 단일 writer면 OK, 일관된 스냅샷은 마스크/더블 버퍼, writer 둘이면 원자 연산.

</details>

### 5. 채점 · 자주 틀리는 것

- `p * N`을 내림으로 → N이 100의 배수가 아니면 p99를 낮게 보고.
- `total * pm`을 32-bit로 → overflow.
- overflow 버킷 클램프 빠뜨림 → 배열 밖 쓰기.
- width 0 나눗셈.
- `cum > rank` (엄격) → 한 버킷 밀림.

## I4 · golden 비교: 허용오차(abs/rel) mismatch 리포트, 첫 N개만 (★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 실리콘 출력과 기준(golden) 출력을 원소별로 비교해, 허용오차를 넘는 것만 앞에서 N개 기록하고 전체 개수를 돌려준다.
- **상황**: NPU/ISP 블록의 int32 출력을 C 기준 모델 결과와 비교한다. 기준이 float 모델이면 약간의 오차는 정상이라 절대 오차(abs)와 상대 오차(rel) 허용 폭을 둔다. 수백만 개가 틀릴 수 있어 로그는 앞 N개만. ([V11 NPU int8 golden](hmScreenPrep/problems/V11_npu_int8_golden/index.html)의 짧은 버전)
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `golden_compare(got, exp, n, abs_tol, rel_ppm, out, max_out)` | 원소별 비교 | 통과: abs(got−exp) ≤ abs_tol + abs(exp)·rel_ppm/10^6 · 실패는 앞 `max_out`개만 `out`에 · 반환 = 실패 총수 |

- **예시를 말로 풀면**: 허용 abs 1, rel 1%. 100 vs 101 → 차이 1 ≤ 1 + 1 통과. 1000 vs 1012 → 차이 12 > 1 + 10 실패. INT32_MIN vs INT32_MAX → 차이 약 43억 실패. 총 2개.
- **지켜야 할 조건**: 어떤 입력에서도 overflow/UB 없음. 나눗셈·FP 없이. `max_out`이 0이면 `out`은 NULL이어도 됨.

### 1. Problem (English)

Compare a DUT output buffer against a golden buffer. An element passes if |got − exp| ≤ abs_tol + |exp| · rel_ppm / 1e6. Record the first `max_out` failures and return the total number of failures. It must be correct for any int32 values and any tolerance.

```c
typedef struct { size_t idx; int32_t got, exp; } mismatch_t;
size_t golden_compare(const int32_t *got, const int32_t *exp, size_t n,
                      uint32_t abs_tol, uint32_t rel_ppm,
                      mismatch_t *out, size_t max_out);
```

- exp `{100,-50,1000,INT32_MIN}`, got `{101,-50,1012,INT32_MAX}`, (1, 10000) → 2, out = idx 2, idx 3
- got `{1011}` vs exp `{1000}`, (1, 10000) → 0 (exactly on the limit passes)
- 5 failures with `max_out = 2` → returns 5, stores 2

### 2. 먼저 물어볼 것 (clarify)

- "Should the limit be inclusive?" → Yes, ≤ passes.
- "Relative to the expected value or to the larger of the two?" → Expected (golden).
- "Combine abs and rel by OR or by sum?" → Sum, numpy-isclose style.
- "Do you want the first N or the worst N?" → First N, plus the total.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: 포함 경계, rel 기준 |
| 2–5 | 예시 4개를 말로, "int32 빼기는 overflow" 지적 |
| 5–15 | `abs_diff` (int64) → 스케일 비교 → 기록/카운트 |
| 15–18 | trace 4원소 |
| 18–20 | 테스트 + 확장(float, 로그 전략) |

- 소리 내서: "**`got - exp` in int32 can overflow, and `abs(INT_MIN)` is undefined, so I widen to int64 before subtracting.**"
- 소리 내서: "**Instead of dividing by a million I multiply the other side, and I check every term fits in 64 bits.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

차이의 최대 크기는 2^32 − 1, |exp|의 최대는 2^31. 10^6 ≈ 2^20. 각 항의 비트 수를 더해 보세요.

</details>

<details><summary>힌트 2</summary>

`diff * 1000000 <= abs_tol * 1000000 + mag * rel_ppm` — 좌변 < 2^52, 우변 < 2^52 + 2^63. 기록은 `if (bad < max_out)`, 카운트는 항상.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

typedef struct { size_t idx; int32_t got, exp; } mismatch_t;

static uint64_t abs_diff(int32_t a, int32_t b)
{
    int64_t d = (int64_t)a - (int64_t)b;      /* never overflows in 64-bit */
    return (uint64_t)(d < 0 ? -d : d);
}

/* pass if |got-exp| <= abs_tol + |exp| * rel_ppm / 1e6  (isclose style).
 * Stores the first max_out mismatches in out[], returns the TOTAL count. */
size_t golden_compare(const int32_t *got, const int32_t *exp, size_t n,
                      uint32_t abs_tol, uint32_t rel_ppm,
                      mismatch_t *out, size_t max_out)
{
    size_t bad = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t diff = abs_diff(got[i], exp[i]);     /* <= 2^32 - 1 */
        uint64_t mag = abs_diff(exp[i], 0);           /* <= 2^31     */
        /* multiply instead of divide: every term < 2^63, no overflow */
        if (diff * 1000000u <= (uint64_t)abs_tol * 1000000u + mag * rel_ppm)
            continue;
        if (bad < max_out) {
            out[bad].idx = i;
            out[bad].got = got[i];
            out[bad].exp = exp[i];
        }
        bad++;
    }
    return bad;
}
```

시간 O(n), 공간 O(1) (+ 호출자 `out`).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

abs_tol 1, rel_ppm 10000 → 우변 = 1,000,000 + mag × 10,000

| i | diff | mag | 좌변 diff×1e6 | 우변 | 결과 |
|---|---|---|---|---|---|
| 0 | 1 | 100 | 1,000,000 | 2,000,000 | pass |
| 1 | 0 | 50 | 0 | 1,500,000 | pass |
| 2 | 12 | 1000 | 12,000,000 | 11,000,000 | **fail → out[0]** |
| 3 | 4,294,967,295 | 2,147,483,648 | ≈ 4.29e15 | ≈ 2.15e13 | **fail → out[1]** |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 예시 → 2개, idx 2, 3.
- 경계: 차이가 정확히 한계(11)면 통과.
- 정확 비교: tol 0, rel 0 → 1만 달라도 실패.
- `max_out` 캡: 실패 5개, 저장 2개, `out[2]`는 안 건드림. `max_out 0` + `out NULL` OK, n = 0 → 0.
- 극단: INT32_MIN/INT32_MAX, `abs_tol = rel_ppm = UINT32_MAX` → overflow 없이 전부 통과 (UBSan 깨끗).

</details>

### 4. 꼬리 질문

Q1. "Why not just `abs(got[i] - exp[i]) > tol`?"

<details><summary>답</summary>

`INT32_MAX - INT32_MIN` overflows int, which is undefined behavior, and `abs(INT_MIN)` is also undefined. A compiler is allowed to assume it doesn't happen, so the check can silently pass a completely wrong output — exactly the case a validation tool must catch.
한국어 핵심: 검증 툴이 overflow로 틀린 결과를 "통과"시키면 최악 — 64-bit로 넓혀서 빼기.

</details>

Q2. "The NPU outputs FP16/FP32 instead. How do you compare?"

<details><summary>답</summary>

Relative tolerance or ULP distance: reinterpret the bits with memcpy into an integer, map negatives so the integer order matches float order, then the difference is the ULP count. NaN must always be a mismatch (NaN != NaN), and ±0 should compare equal. I'd also log the max error, not just pass/fail.
한국어 핵심: ULP 비교(memcpy로 비트 해석), NaN은 무조건 실패, +0/−0 같게. (개념 설명, 이 페이지에서 코드 미작성 [미검증])

</details>

Q3. "A million mismatches come back. What do you log so the designer can debug?"

<details><summary>답</summary>

Total count, first and last index, the first N entries, the max diff and where it was. I'd also OR together `got ^ exp` across all mismatches: if it's always the same bit, that points to a stuck data line or one lane, not a math bug. And the mismatch index pattern — every 64th element suggests a tiling or DMA boundary issue.
한국어 핵심: 총수 + 처음/끝 + 최대 오차 + **XOR 비트 패턴**(같은 비트 = 하드웨어 라인 문제).

</details>

### 5. 채점 · 자주 틀리는 것

- int32 빼기 / `abs(INT_MIN)` UB.
- 상대 오차를 `diff / exp`로 → 0으로 나누기 + 정수 내림.
- `out`을 `max_out` 넘어 쓰기, 또는 총수 대신 저장 수 반환.
- 경계 `<` vs `<=` 말 안 함.
- 곱셈 overflow 범위를 설명 못 함 (2^52, 2^63).

## I5 · shmoo 2D pass/fail 배열 → 전압별 pass window · 마진 중심 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: pass/fail 격자의 각 행에서 가장 긴 연속 pass 구간(window)과 그 중심을 찾고, window가 가장 넓은 행을 고른다.
- **상황**: DDR read eye 학습이나 SerDes margining처럼, 한 축은 전압(Vref 또는 VDD 단계), 다른 축은 delay tap을 쓸어서 pass/fail을 칠한다. 검증 결과물은 "된다"가 아니라 **"window 폭과 중심"** — 중심에 설정해야 양쪽 마진이 최대. (개념: [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html), [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html). V×F 전체 sweep은 [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html))
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `longest_pass(row, n)` | 행 하나의 최장 연속 pass | 동률이면 앞쪽 · 없으면 `{-1, 0}` |
| `window_center(w)` | 중심 tap | `start + (len−1)/2` (짝수 길이면 아래쪽 중간) |
| `best_row(grid, nrows, ncols, w)` | 가장 넓은 행 | 동률이면 낮은 행 · 없으면 -1 · `w`는 NULL 허용 |

- **예시를 말로 풀면**: 행 `.######.#.`는 tap 1–6이 연속 pass(길이 6) → 중심 3. tap 8은 혼자라 무시. 행 `..#######.`는 길이 7, 중심 5 → 이 행이 best.
- **지켜야 할 조건**: 오른쪽 끝에 닿는 run도 셀 것. 구멍(hole) 하나가 window를 둘로 쪼갬.

### 1. Problem (English)

A shmoo sweep produced a pass/fail grid: rows are voltage steps, columns are delay taps. For each row find the longest run of consecutive passes and its center tap, and return the row with the widest window.

```c
typedef struct { int start; int len; } window_t;
window_t longest_pass(const uint8_t *row, int n);
int      window_center(window_t w);
int      best_row(const uint8_t *grid, int nrows, int ncols, window_t *w);
```

- `..#######.` → start 2, len 7, center 5
- `###.####` → start 4, len 4 (the hole splits the window)
- grid rows `..##......`, `.######.#.`, `..#######.`, `##.#.##.##` → best row 2

### 2. 먼저 물어볼 것 (clarify)

- "Is a single failing tap inside a window a real fail, or should I tolerate it?" → Real fail for now.
- "Tie-break between equal windows?" → First run, lowest row.
- "Are taps circular (phase wraps around)?" → No; follow-up.
- "Center for even length?" → Lower middle.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: hole 정책, 동률, 원형 여부 |
| 2–5 | 행 하나를 손으로 run 길이 표시 |
| 5–15 | longest_pass (run_start/run_len) → center → best_row |
| 15–18 | `.######.#.` trace |
| 18–20 | 테스트 + 확장(원형, hole 재시도) |

- 소리 내서: "**One pass: track the current run's start and length, and update the best whenever the current run beats it — updating inside the loop means a run that touches the end is never lost.**"
- 소리 내서: "**I set the training value to the center so the left and right margins are equal.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

fail을 만나면 현재 run 길이를 0으로. pass면 run이 0일 때 시작 위치를 기억.

</details>

<details><summary>힌트 2</summary>

best 갱신을 fail을 만날 때 하면 끝까지 이어진 run을 놓칩니다 — pass일 때마다 `if (run_len > best.len)`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

typedef struct { int start; int len; } window_t;   /* len 0 -> no pass */

/* Longest run of consecutive pass (nonzero) cells; ties -> first run. */
window_t longest_pass(const uint8_t *row, int n)
{
    window_t best = { -1, 0 };
    int run_start = 0, run_len = 0;
    for (int i = 0; i < n; i++) {
        if (row[i]) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len > best.len) { best.start = run_start; best.len = run_len; }
        } else {
            run_len = 0;
        }
    }
    return best;
}

int window_center(window_t w) { return w.start + (w.len - 1) / 2; }

/* grid[r * ncols + c]. Returns the row with the widest window
 * (ties -> lower row) and its window in *w, or -1 if nothing passes. */
int best_row(const uint8_t *grid, int nrows, int ncols, window_t *w)
{
    int best = -1;
    window_t bw = { -1, 0 };
    for (int r = 0; r < nrows; r++) {
        window_t cur = longest_pass(&grid[(size_t)r * ncols], ncols);
        if (cur.len > bw.len) { bw = cur; best = r; }
    }
    if (w) *w = bw;
    return best;
}
```

시간 O(nrows × ncols), 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

행 `.######.#.`

| i | cell | run_start | run_len | best (start, len) |
|---|---|---|---|---|
| 0 | . | 0 | 0 | (-1, 0) |
| 1 | # | 1 | 1 | (1, 1) |
| 2–6 | # | 1 | 2…6 | (1, 6) |
| 7 | . | 1 | 0 | (1, 6) |
| 8 | # | 8 | 1 | (1, 6) |
| 9 | . | 8 | 0 | (1, 6) → center 1 + 5/2 = 3 |

best_row: r0 len 2 → r1 len 6 → r2 len 7 (best) → r3 len 2 → **2**

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 예시 grid → 행 2, start 2, len 7, center 5.
- 오른쪽 끝에 닿는 run: `#..###` → start 3, len 3.
- hole: `###.####` → start 4, len 4.
- 전부 pass → start 0, len n; 전부 fail → len 0, best_row −1; nrows 0 → −1.
- 동률: run 동률 → 앞쪽, 행 동률 → 낮은 행.

</details>

### 4. 꼬리 질문

Q1. "The taps are phase steps 0–359°, so the window can wrap around. Fix it."

<details><summary>답</summary>

Scan the row twice (index j mod n) so a run that crosses the end continues into the start, and cap the run length at n so an all-pass row doesn't count double. The start is reported mod n, and so is the center.
한국어 핵심: 2n 스캔 + 길이 n 캡 (선형 스캔은 `##....###`에서 3, 원형은 5).

```c
/* Follow-up: circular taps (phase wraps n-1 -> 0). Scan 2n, cap run at n. */
window_t longest_pass_circular(const uint8_t *row, int n)
{
    window_t best = { -1, 0 };
    int run_start = 0, run_len = 0;
    for (int j = 0; j < 2 * n; j++) {
        if (row[j % n]) {
            if (run_len == 0) run_start = j;
            if (run_len < n) run_len++;
            if (run_len > best.len) { best.start = run_start % n; best.len = run_len; }
        } else {
            run_len = 0;
        }
    }
    return best;   /* center = (start + (len-1)/2) % n */
}
```

</details>

Q2. "There's one failing tap in the middle of a wide eye. What do you do?"

<details><summary>답</summary>

First rerun that point several times. If it fails every time, it's a real conditional fail — maybe a data-pattern or crosstalk issue — and I must not center across it. If it's intermittent, I mark it marginal and report it, because random fails inside the eye usually mean noise, a power issue or an equipment problem. Either way the hole goes in the report.
한국어 핵심: 재현부터 — 반복 실패면 진짜(가로질러 중심 잡지 말 것), 간헐이면 marginal 표시.

</details>

Q3. "What margin number do you report?"

<details><summary>답</summary>

Left margin = center − start and right margin = end − center, converted from taps to picoseconds (or millivolts for the voltage axis), and I report the smaller one. I'd also report it across PVT corners, because the worst corner sets the real margin.
한국어 핵심: 양쪽 중 작은 마진을 ps/mV 단위로, PVT 최악 코너 기준.

</details>

### 5. 채점 · 자주 틀리는 것

- 끝에 닿은 run을 놓침 (fail 때만 best 갱신).
- `run_start`를 매 pass마다 덮어씀.
- 중심 계산 `(start + len) / 2` 같은 off-by-one.
- `grid[r * ncols]` 인덱스 계산을 행/열 반대로.
- hole 정책을 묻지 않음.

## I6 · 처리량 계산: bytes·cycles·clock Hz → MB/s (오버플로 없이, 정수만) (★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 옮긴 바이트 수, 걸린 cycle 수, 타이머 클럭 Hz로 MB/s(×100)를 정수만으로, 정확하게(내림), overflow 없이 계산한다.
- **상황**: PCIe DMA나 DDR copy를 32-bit cycle counter 차이로 잰다. 결과를 "6710.88 MB/s"처럼 찍어야 하는데 타깃에서 float을 쓰고 싶지 않다. 순진한 `bytes / cycles * clk`는 정밀도가 0이 되고, `bytes * clk / cycles`는 큰 전송에서 64-bit overflow. (측정 방법론은 [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html), [성능 메트릭 노트](2026-10-30_perf_metrics_ssd_vs_soc.html))
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `mbps_x100(bytes, cycles, clk_hz)` | (MB/s) × 100 | 1 MB = 10^6 B · 정확한 내림 · overflow면 `UINT64_MAX` · cycles 0 → 0 |

- **예시를 말로 풀면**: 4 MiB를 1 GHz에서 10^6 cycle(1 ms)에 옮김 → 4,194,304,000 B/s → 4194.30 MB/s → 419430.
- **지켜야 할 조건**: `cycles`는 32-bit counter 차이라 `uint32_t`. FP·128-bit 정수 없이.

### 1. Problem (English)

You measured a DMA transfer: `bytes` moved in `cycles` ticks of a timer running at `clk_hz`. Return the throughput in MB/s × 100 (1 MB = 10^6 bytes), rounded down exactly, using only 64-bit integers. Never overflow; return `UINT64_MAX` if the result doesn't fit.

```c
uint64_t mbps_x100(uint64_t bytes, uint32_t cycles, uint32_t clk_hz);
```

- `(4194304, 1000000, 1000000000)` → 419430 (4194.30 MB/s)
- `(268435456, 4000000, 100000000)` → 671088 (256 MiB in 40 ms = 6710.88 MB/s)
- `(100, 1000, 1000000000)` → 10000 (100.00 MB/s; naive `bytes/cycles*clk` gives 0)

### 2. 먼저 물어볼 것 (clarify)

- "MB as 10^6 or MiB as 2^20?" → 10^6 (decimal, like link specs).
- "Round down or to nearest?" → Down.
- "Range of bytes?" → Anything up to 2^64 − 1.
- "Is cycles already a wrap-safe delta?" → Yes (I1).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: MB 정의, 반올림 |
| 2–5 | 순진한 두 공식이 각각 왜 틀리는지 말하기 |
| 5–15 | q/r 분해 → overflow 체크 → /10000 |
| 15–18 | 256 MiB 예시 trace |
| 18–20 | 테스트 + 확장(효율 %, 긴 측정) |

- 소리 내서: "**Divide-first loses everything when bytes < cycles; multiply-first overflows above about 4 GB at a 4 GHz clock. So I split bytes into quotient and remainder by cycles: bytes·clk/cycles = q·clk + r·clk/cycles, and r·clk fits because r < 2^32.**"
- 소리 내서: "**Floor of a floor divided by an integer is still the exact floor, so dividing by 10,000 at the end is exact.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

`bytes = q × cycles + r` (0 ≤ r < cycles). 그러면 `bytes × clk / cycles`를 두 항으로 나눌 수 있습니다.

</details>

<details><summary>힌트 2</summary>

`q × clk`는 결과 자체보다 작거나 같으니 overflow면 진짜 결과도 안 들어감 → `q > UINT64_MAX / clk` 체크. `r × clk < 2^32 × 2^32 = 2^64`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* MB/s x100 (1 MB = 1e6 B) for `bytes` moved in `cycles` ticks of clk_hz.
 * Exact floor; returns UINT64_MAX on overflow; cycles == 0 -> 0. */
uint64_t mbps_x100(uint64_t bytes, uint32_t cycles, uint32_t clk_hz)
{
    if (cycles == 0)
        return 0;
    uint64_t q = bytes / cycles, r = bytes % cycles;
    /* bytes*clk/cycles == q*clk + r*clk/cycles ; r < 2^32 so r*clk < 2^64 */
    if (clk_hz != 0 && q > UINT64_MAX / clk_hz)
        return UINT64_MAX;
    uint64_t hi = q * clk_hz;
    uint64_t lo = r * clk_hz / cycles;
    if (hi > UINT64_MAX - lo)
        return UINT64_MAX;
    return (hi + lo) / 10000u;          /* B/s -> (MB/s) * 100 */
}
```

시간 O(1), 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`bytes = 268,435,456`, `cycles = 4,000,000`, `clk = 100,000,000`

| 단계 | 값 |
|---|---|
| q = bytes / cycles | 67 (67 × 4e6 = 268,000,000) |
| r = bytes % cycles | 435,456 |
| hi = q × clk | 6,700,000,000 |
| lo = r × clk / cycles | 43,545,600,000,000 / 4,000,000 = 10,886,400 |
| (hi + lo) / 10000 | 6,710,886,400 / 10000 = **671088** → 6710.88 MB/s |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 4 MiB / 1 ms → 419430; 256 MiB / 40 ms → 671088.
- bytes < cycles: 100 B / 1000 cycle @ 1 GHz → 10000 (순진한 나눗셈 먼저 → 0).
- 큰 값: 1 TiB, cycles = clk = UINT32_MAX → 정확히 2^40 / 10000 (순진한 곱셈 먼저는 overflow).
- 포화: `UINT64_MAX` bytes in 1 cycle @ max clock → `UINT64_MAX`.
- cycles 0, bytes 0, clk 0 → 0.

</details>

### 4. 꼬리 질문

Q1. "Why not just use double?"

<details><summary>답</summary>

On a host tool, double is fine. On the target I avoid it: some cores have no FPU, FP in an interrupt means saving FP registers, and `printf("%f")` pulls in a big library. The integer version is also exact and deterministic, so two runs give bit-identical logs.
한국어 핵심: 타깃에서는 FPU/ISR 비용/코드 크기 때문에 정수, 결과도 정확·재현 가능.

</details>

Q2. "This was a PCIe Gen4 x4 DMA. How efficient is 6710 MB/s?"

<details><summary>답</summary>

Gen4 is 16 GT/s per lane with 128b/130b encoding, so x4 is 4 × 16e9 × 128/130 / 8 ≈ 7877 MB/s on the wire. 6710 / 7877 is about 85%, which is in the normal range once TLP headers, DLLPs and the max payload size are counted [추정: 실제 상한은 MPS·read request size에 따라 다름]. I'd report both raw MB/s and percent of theoretical, and integer percent is just `measured * 100 / theoretical`.
한국어 핵심: 이론 7877 MB/s 대비 ≈ 85% — 원값과 효율 %를 같이 보고 (PCIe 상세: [LP17](2026-10-31_LP17_gen5_gen6_retimer_automotive.html)).

</details>

Q3. "The transfer takes 10 seconds and the 1 GHz counter wraps every 4.3 s."

<details><summary>답</summary>

A 32-bit delta can't represent more than one wrap. Options: use a 64-bit counter (most SoCs have one, like the Arm generic timer), use a slower reference clock for long runs, or measure in chunks and sum the per-chunk deltas in 64 bits — each chunk shorter than one wrap.
한국어 핵심: 한 번 이상 wrap하면 32-bit 차이 불가 → 64-bit 타이머 / 느린 클럭 / 구간 합.

</details>

### 5. 채점 · 자주 틀리는 것

- 나눗셈 먼저 → 0.
- 곱셈 먼저 → overflow를 말 안 함.
- `UINT64_MAX / clk_hz`에서 clk 0 나눗셈.
- MB vs MiB 혼동 (2^20 vs 10^6, 약 4.9% 차이).
- `hi + lo` 덧셈 overflow 체크 누락.

## I7 · Q15 고정소수점 곱셈 (반올림 + 포화) (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: −1 이상 1 미만 값을 `int16_t`로 표현하는 Q15 형식 두 개를 곱해, 반올림하고 범위를 넘으면 잘라(포화)서 Q15로 돌려준다.
- **상황**: 카메라 ISP의 gain·white balance 계수, IMU 보정 계수처럼 소수가 필요하지만 FP를 안 쓰는 경로. NPU의 int8 requantization도 같은 원리(Q31 곱). FP 대신 쓰는 것이 바로 고정소수점.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `q15_mul(a, b)` | a × b (Q15) | 32-bit 곱(Q30) → 반올림(+0.5 LSB) → >>15 → 포화 [−32768, 32767] |

- **예시를 말로 풀면**: 0x4000은 0.5. 0.5 × 0.5 = 0.25 = 0x2000. −1 × −1 = +1인데 Q15는 1을 표현 못 함 → 최대값 32767로 포화.
- **지켜야 할 조건**: int16 × int16을 int16에서 하지 않기, 포화 경우를 정확히 알기, 음수 시프트의 성질 말하기.

### 1. Problem (English)

Q15 stores a value in [−1, 1) as `int16_t` x / 32768. Multiply two Q15 numbers, round half up, and saturate to the int16 range.

```c
int16_t q15_mul(int16_t a, int16_t b);
```

- `q15_mul(0x4000, 0x4000)` → `0x2000` (0.5 × 0.5 = 0.25)
- `q15_mul(-32768, -32768)` → 32767 (+1.0 saturates)
- `q15_mul(1, 0x4000)` → 1 (exactly half an LSB rounds up); `q15_mul(-1, 0x4000)` → 0

### 2. 먼저 물어볼 것 (clarify)

- "Rounding mode: half up, half away from zero, or truncate?" → Half up (add 0.5 LSB, floor).
- "Saturate or wrap on overflow?" → Saturate.
- "Can I assume the compiler does arithmetic right shift?" → Yes, gcc/clang; mention portability.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: 반올림 모드, 포화 |
| 2–5 | Q15 × Q15 = Q30 설명, 0.5 × 0.5 손으로 |
| 5–15 | sat16 → q15_mul |
| 15–18 | trace 3개 (0.5², −1², −1 LSB × 0.5) |
| 18–20 | 테스트 + 확장(Q31, 이식성) |

- 소리 내서: "**Q15 times Q15 is Q30, so I multiply in 32 bits, add half of the bit I'm about to drop, then shift right by 15.**"
- 소리 내서: "**The only input that overflows is −1 × −1, because +1.0 isn't representable in Q15.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

`|a × b| ≤ 2^30` 이니 `int32_t`에 들어갑니다. 반올림 상수는 버릴 15비트의 절반 = `1 << 14`.

</details>

<details><summary>힌트 2</summary>

`(2^30 + 2^14) >> 15 = 32768` → int16 최대를 1 넘음 → 포화 함수로 clamp.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

#define Q15_MAX   32767
#define Q15_MIN (-32768)

static int16_t sat16(int32_t x)
{
    if (x > Q15_MAX) return Q15_MAX;
    if (x < Q15_MIN) return Q15_MIN;
    return (int16_t)x;
}

/* a * b in Q15, round half up, saturate.
 * >> of a negative int is implementation-defined in C11; gcc/clang do an
 * arithmetic shift (floor), which this rounding relies on. */
int16_t q15_mul(int16_t a, int16_t b)
{
    int32_t p = (int32_t)a * b;   /* Q30, |p| <= 2^30: fits int32 */
    p += 1 << 14;                 /* + 0.5 LSB of the Q15 result  */
    return sat16(p >> 15);        /* only -1 * -1 needs the clamp */
}
```

시간 O(1), 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| a | b | p = a·b | p + 16384 | >> 15 | sat |
|---|---|---|---|---|---|
| 16384 | 16384 | 268,435,456 | 268,451,840 | 8192 | 8192 = 0x2000 |
| −32768 | −32768 | 1,073,741,824 | 1,073,758,208 | 32768 | **32767** |
| −1 | 16384 | −16384 | 0 | 0 | 0 |
| 1 | 16384 | 16384 | 32768 | 1 | 1 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 0.5 × 0.5 = 0x2000, −1 × 0.5 = −16384, max × max = 32766.
- 포화: −1 × −1 → 32767. min × max → −32767.
- 0 곱하기 → 0.
- 반올림 경계: 1 × 1 → 0, 정확히 +0.5 LSB → 1, 정확히 −0.5 LSB → 0, 그보다 조금 더 음수 → −1.
- 교환법칙: `q15_mul(a,b) == q15_mul(b,a)`.

</details>

### 4. 꼬리 질문

Q1. "Why bother rounding? Truncation is cheaper."

<details><summary>답</summary>

Truncation with an arithmetic shift always rounds toward minus infinity, so every multiply has an average bias of −0.5 LSB. In a filter or an accumulating gain loop that bias adds up into a DC offset. Rounding costs one add and makes the error zero-mean (round-half-up still has a tiny bias on exact halves; convergent rounding removes it).
한국어 핵심: 내림은 매번 −0.5 LSB 편향 → 필터에서 DC 오프셋으로 누적.

</details>

Q2. "The NPU requantizes with a Q31 multiplier. Same thing?"

<details><summary>답</summary>

Same idea one size up: multiply in int64 (Q62), add 1 << 30, shift right 31, and only INT32_MIN × INT32_MIN saturates. This is the "saturating rounding doubling high multiply" used in integer-only inference libraries like gemmlowp/TFLite.
한국어 핵심: Q31 = 64-bit 곱 + `1<<30` + `>>31`, 포화는 MIN×MIN 하나뿐.

```c
/* Follow-up: Q31 (e.g. NPU requant multiplier) - same idea in 64-bit. */
int32_t q31_mul(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b;      /* Q62 */
    p += (int64_t)1 << 30;           /* + 0.5 LSB */
    p >>= 31;
    if (p > INT32_MAX) return INT32_MAX;   /* only INT32_MIN * INT32_MIN */
    return (int32_t)p;
}
```

</details>

Q3. "Right shift of a negative number — is that portable?"

<details><summary>답</summary>

No: in C11 the result of right-shifting a negative signed value is implementation-defined. gcc and clang (and this Mac) do an arithmetic shift, which is floor division by 2^15 — the rounding depends on that. For strict portability I can compute the floor explicitly with unsigned math; I verified it matches `>>` over the whole range of p.
한국어 핵심: 음수 `>>`는 구현 정의 — 이식성이 필요하면 unsigned로 floor 직접 계산 (전 범위 일치 확인).

```c
/* Follow-up: floor(p / 2^15) without relying on signed >> behaviour. */
static int32_t asr15_portable(int32_t p)
{
    if (p >= 0) return p >> 15;
    return -(int32_t)(((0u - (uint32_t)p) + 32767u) >> 15);   /* -ceil(-p/2^15) */
}
```

</details>

Q4. "Saturating add, and how do you make the Q15 coefficient table?"

<details><summary>답</summary>

Add in int32 and clamp. Coefficients are generated on the host with FP: multiply by 32768, round, clamp — note 1.0 can't be represented and becomes 32767. On Cortex-M with the DSP extension the clamp is a single `SSAT` instruction, and there are saturating add instructions like `QADD`.
한국어 핵심: 계수 표는 호스트에서 FP로 만들고 타깃은 정수만, 1.0 → 32767.

```c
/* Follow-up: saturating add and float -> Q15 (host side / table generation). */
int16_t q15_add(int16_t a, int16_t b) { return sat16((int32_t)a + b); }

int16_t q15_from_double(double x)
{
    double s = x * 32768.0;
    s += (s >= 0) ? 0.5 : -0.5;           /* round half away from zero */
    if (s >= 32767.0) return Q15_MAX;
    if (s <= -32768.0) return Q15_MIN;
    return (int16_t)s;                    /* truncation toward zero */
}
```

</details>

### 5. 채점 · 자주 틀리는 것

- `int16_t p = a * b;` → 잘림 (int 승격 후 다시 16-bit로).
- 반올림 상수를 `1 << 15`로 (그건 1 LSB 전체).
- 포화 경우를 모름, 또는 시프트 전에 clamp.
- 음수 시프트가 구현 정의라는 걸 말 안 함.
- `(int32_t)a * b`에서 cast 위치 — 여기선 int가 32-bit라 결과는 같지만 의도를 보여주기.

## I8 · 최대 통과 주파수 이진 탐색 (단조 가정, flaky 재시도) (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 주파수 격자에서 테스트가 통과하는 가장 높은 주파수(fmax)를 이진 탐색으로 찾되, 가끔 통과하는(flaky) 점은 실패로 친다.
- **상황**: NPU나 CPU 클러스터의 클럭을 올려 가며 stress 패턴을 돌린다. 한 번 돌리는 데 몇 분씩 걸려 121개 점을 다 돌리기엔 비싸다. 낮은 주파수는 통과, 높은 주파수는 실패한다는 단조 가정이 있으면 log 횟수로 찾을 수 있다. 경계 근처 점은 통과/실패가 섞이는데, 그걸 통과로 치면 마진 없는 칩을 출하하게 된다. (전체 sweep은 [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html), 확인 trial이 있는 탐색은 [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html))
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `find_fmax(t, ctx, f_lo, step, n, k)` | 격자 `f_lo + i·step` (i < n)에서 fmax | 점 통과 = k번 연속 통과 · f_lo 실패면 0 · NULL/n 0/k 0 → 0 |

- **예시를 말로 풀면**: 800–2000 MHz, 10 MHz 간격(121점), 진짜 fmax 1430. k = 1이면 테스트 8번 만에 1430. 1440이 가끔 통과하는 칩이면 k = 1은 1440이라고 속을 수 있고, k = 3은 1430을 찾는다.
- **지켜야 할 조건**: 불변식(lo는 통과, hi는 실패)을 말로 설명. `mid` 계산 overflow 없이. 무한 루프 없이.

### 1. Problem (English)

Find the highest passing frequency on a grid `f_lo + i*step`, `i in [0, n)`, using as few test runs as possible. Assume monotone behavior. Because points near the edge can be flaky, a point only counts as passing if it passes `k` times in a row. Return 0 if even `f_lo` fails.

```c
typedef bool (*freq_test_fn)(void *ctx, uint32_t mhz);
uint32_t find_fmax(freq_test_fn t, void *ctx, uint32_t f_lo, uint32_t step,
                   uint32_t n, uint32_t k);
```

- 800..2000 step 10 (n 121), fmax 1430, k 1 → 1430 in 8 calls
- same with k 3 → 1430 in 14 calls
- 1440 is flaky (passes every other call): k 1 → 1440 (fooled), k 3 → 1430

### 2. 먼저 물어볼 것 (clarify)

- "Can I assume monotonic — once it fails, every higher frequency fails?" → Yes.
- "For a flaky point, is a single pass enough, or must every retry pass?" → All k must pass (conservative).
- "What if everything passes?" → Return the top of the grid.
- "Is each test expensive?" → Yes, minutes — minimize calls.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: 단조, flaky 정책, 경계 |
| 2–5 | 불변식 말하고 121점 → 약 7번이라고 계산 |
| 5–15 | stable_pass → f_lo 체크 → lo/hi 루프 |
| 15–18 | trace 3–4스텝 |
| 18–20 | 테스트 + 확장(guard band, 비단조) |

- 소리 내서: "**Invariant: index lo is known to pass and index hi is known to fail, where hi = n is a virtual fail past the grid. I stop when they're adjacent, and lo is the answer.**"
- 소리 내서: "**For fmax a false pass is worse than a false fail, so a flaky point must pass k times in a row to count.**"

<details><summary>힌트 1 (막히면 5분 후)</summary>

인덱스로 탐색하고 주파수는 마지막에 변환. 먼저 `f_lo`가 통과하는지 확인하면 "lo는 통과" 불변식이 시작부터 성립.

</details>

<details><summary>힌트 2</summary>

`while (hi − lo > 1) { mid = lo + (hi − lo) / 2; pass → lo = mid; fail → hi = mid; }` — `mid`는 항상 lo와 hi 사이라 이미 아는 점을 다시 돌리지 않음.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef bool (*freq_test_fn)(void *ctx, uint32_t mhz);

/* A point counts as pass only if k runs in a row pass (flaky -> fail). */
static bool stable_pass(freq_test_fn t, void *ctx, uint32_t mhz, uint32_t k)
{
    for (uint32_t i = 0; i < k; i++)
        if (!t(ctx, mhz))
            return false;
    return true;
}

/* Grid f = f_lo + i*step, i in [0, n). Assumes pass below fmax, fail above.
 * Returns the highest stable-pass frequency, or 0 if f_lo itself fails. */
uint32_t find_fmax(freq_test_fn t, void *ctx, uint32_t f_lo, uint32_t step,
                   uint32_t n, uint32_t k)
{
    if (t == NULL || n == 0 || k == 0)
        return 0;
    if (!stable_pass(t, ctx, f_lo, k))
        return 0;
    uint32_t lo = 0, hi = n;     /* invariant: lo passes, hi fails (n = virtual) */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (stable_pass(t, ctx, f_lo + mid * step, k))
            lo = mid;
        else
            hi = mid;
    }
    return f_lo + lo * step;
}
```

시간 O(k · log n) 테스트 호출, 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

800..2000 step 10, n = 121, fmax 1430 (= index 63), k = 1. 먼저 f_lo 통과(호출 1).

| lo | hi | mid | f | 결과 |
|---|---|---|---|---|
| 0 | 121 | 60 | 1400 | pass → lo = 60 |
| 60 | 121 | 90 | 1700 | fail → hi = 90 |
| 60 | 90 | 75 | 1550 | fail → hi = 75 |
| 60 | 75 | 67 | 1470 | fail → hi = 67 |
| 60 | 67 | 63 | 1430 | pass → lo = 63 |
| 63 | 67 | 65 | 1450 | fail → hi = 65 |
| 63 | 65 | 64 | 1440 | fail → hi = 64 → 끝, **1430**, 호출 8번 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: fmax 1430 → 1430, 호출 ≤ 1 + 7 (실행: 8번), k = 3 → 14번.
- flaky: 1440이 번갈아 통과 → k 1은 1440 (실행 확인), k 3은 1430.
- 전부 통과 → 2000; f_lo부터 실패 → 0.
- n = 1 → f_lo; fmax가 두 번째 점(810) → 810.
- NULL 콜백, n 0, k 0 → 0.

</details>

### 4. 꼬리 질문

Q1. "You found 1430 MHz. Is that the number you'd give the product team?"

<details><summary>답</summary>

No — that's the edge, not an operating point. I'd confirm it with a long soak test at 1430, check it across temperature and voltage corners, and then apply a guard band, say a couple of grid steps or a few percent, to cover aging and parts I didn't test. The reported fmax is the worst corner minus the guard band.
한국어 핵심: 찾은 값은 경계 — soak 확인 + PVT 최악 코너 + guard band 후에 보고.

</details>

Q2. "What if the behavior isn't monotonic — there's a hole at 1200 MHz?"

<details><summary>답</summary>

Binary search silently gives a wrong answer: if it probes 1200 first it concludes fmax is below 1200. So after the search I'd spot-check some points below the result, and if any fail I fall back to a full linear sweep (the shmoo in V7). Holes are interesting bugs anyway — often a PLL or a specific clock ratio.
한국어 핵심: 비단조면 이진 탐색은 조용히 틀림 → 아래쪽 샘플 확인, 실패하면 전체 sweep.

</details>

Q3. "How do you also find Vmin at a target frequency?"

<details><summary>답</summary>

Same search on the voltage axis, mirrored: pass above Vmin, fail below, so the invariant is hi passes and lo fails. For a full V/F curve I walk the edge: at each next voltage, fmax is at least the previous one, so I start the search there — a staircase instead of a full grid.
한국어 핵심: 전압축은 방향만 반대인 같은 탐색, V/F 곡선은 이전 fmax에서 시작하는 계단식.

</details>

### 5. 채점 · 자주 틀리는 것

- `mid = (lo + hi) / 2` — 여기선 작아서 괜찮지만 습관적으로 `lo + (hi − lo)/2`.
- 불변식 없이 `lo <= hi` 루프 → off-by-one 또는 무한 루프.
- f_lo 확인 없이 시작 → "lo 통과" 가정이 거짓.
- flaky를 "재시도해서 한 번이라도 통과하면 통과"로 → 마진 없는 칩 통과.
- `f_lo + mid * step` overflow 범위를 말 안 함 (uint32, MHz라 안전).

## 출처

1. N1570 (C11 draft) — 6.2.5p9 unsigned 산술은 mod 2^N, 6.3.1.1 integer promotion, 6.5.7p5 음수 right shift는 implementation-defined, 7.22.6.1 `abs` 결과를 표현 못 하면 UB: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
2. cppreference — arithmetic operators / shift: https://en.cppreference.com/w/c/language/operator_arithmetic
3. Welford·Chan 병합 공식: https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
4. Nearest-rank percentile: https://en.wikipedia.org/wiki/Percentile
5. numpy `isclose` (abs + rel 합 방식): https://numpy.org/doc/stable/reference/generated/numpy.isclose.html
6. Q format: https://en.wikipedia.org/wiki/Q_(number_format) · gemmlowp fixedpoint (SaturatingRoundingDoublingHighMul): https://github.com/google/gemmlowp/blob/master/fixedpoint/fixedpoint.h
7. 이 job 폴더 내부 자료: hmScreenPrep V4 · V7 · V11 · V12 · L3, 2026-10-30_perf_metrics_ssd_vs_soc, BV09 · BV12, LP17 (PCIe Gen4 16 GT/s, 128b/130b).
8. 모든 코드: onsitePrep/n05/i/I1–I8 — `cc -isysroot …/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined`로 컴파일·실행, 2026-11-01 macOS arm64 clang에서 전부 PASS.

---
id: ALGO-A08
time: 110분
updated: 2026-11-11
---
# ALGO-A08 스트리밍 통계: Welford 분산과 저수지 샘플링

## 학습 목표

- 교과서 공식 Σx² − (Σx)²/n 이 **큰 평균 · 작은 분산** 데이터에서 무너지는 것(파국적 상쇄, catastrophic cancellation)을 float·double 로 실측하고, 오차가 κ²(κ ≈ |평균|/σ)에 비례해 자라는 반면 **Welford** 는 κ 에 비례한다는 것을 숫자로 보인다. Welford 갱신식과 **Chan 병합식**을 직접 유도한다.
- 정수 ADC 데이터라면 "첫 샘플 K 를 빼고 정확한 정수 합" 이 Cortex-M4F(단정도 FPU)에서 Welford float 보다 낫다는 것, 그리고 그 누산기의 넘침 한계(d² 는 uint64 로 곱해야 함)를 말한다.
- 정수 EMA `y += (x − y) >> k` 의 **정체 구간(dead band)** 을 증명한다: 위로 가는 계단은 정확히 2^k − 1 아래에서 멈추고 아래로 가는 계단은 끝까지 간다(바닥 나눗셈일 때). 음수 `>>` 가 C11 에서 구현 정의라는 점을 피하는 `asr_floor`, 소수부를 누산기에 넣는 **고정소수점 EMA** 로 고치고, 실수 EMA 대비 오차 한계 `|y − Y| < 1` 을 증명·실측한다.
- 나머지 편향(modulo bias) 없는 범위 난수(거부 샘플링)와 **저수지 샘플링 Algorithm R** 을 구현하고, 포함 확률 k/n 과 "모든 k-부분집합이 같은 확률" 을 귀납법으로 증명한다. **카이제곱 균일성 검정** 두 가지(부분집합 다항 검정, 포함 횟수 검정 + 상관 보정 (n−1)/(n−k))를 유도하고 p-값을 직접 계산한다.
- 펌웨어 텔레메트리 요약기(지연 5만 샘플)에서 "전부 저장(200 000 B)" 과 "요약 564 B" 의 결과를 나란히 놓고 무엇이 정확히 같고 무엇이 근사인지 말한다.

## 1. 문제

센서·통신 펌웨어 면접에서 "RAM 이 4 KB 인데 하루치 샘플의 평균·분산·대표 표본을 보고하라" 는 질문은 자주 나온다. 정답은 짧은 상태 몇 개지만, 꼬리질문은 **수치 안정성, 정수 시프트의 함정, 난수의 편향, 통계 검정**으로 깊어진다.

세 가지 요약기(summary)를 만든다. 모두 샘플을 저장하지 않고 한 번 훑으며(one pass), 샘플당 O(1) 일을 한다.

| 요약기 | 상태 | 이 레슨에서 쓰는 크기 | 전부 저장과 비교하면 |
|---|---|---|---|
| Welford 평균/분산 | n, mean, M2 | 24 B (double) / 12 B (float) / 24 B (정수 이동 합) | 정수 합은 **정확히 같다**, Welford 는 오차 ∝ κ |
| 지수 이동 평균(EMA) | acc 하나 | 4 B | 실수 EMA 와 **1 LSB 미만** 차이 (증명) |
| 저수지 표본 k 개 | k 칸 + 위치 + 난수 상태 | 524 B (k = 64) | 분위수는 **확률적 근사** — 그래서 검정이 필요 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a08a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">스트림 하나, 요약기 셋 — 샘플은 지나가고 상태만 남는다</text>
  <g stroke="currentColor">
    <rect x="20" y="120" width="34" height="30" class="f-mut"/><rect x="58" y="120" width="34" height="30" class="f-mut"/>
    <rect x="96" y="120" width="34" height="30" class="f-mut"/><rect x="134" y="120" width="34" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="26" y="140">128</text><text x="64" y="140">131</text><text x="102" y="140">524</text><text x="140" y="140">126</text>
  </g>
  <text x="20" y="172" fill="currentColor">x_i (µs) 하나씩, N = 50 000</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M172 135 L250 70" marker-end="url(#a08a)"/>
    <path d="M172 135 L250 140" marker-end="url(#a08a)"/>
    <path d="M172 135 L250 210" marker-end="url(#a08a)"/>
  </g>
  <rect x="252" y="45" width="230" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="262" y="65" fill="currentColor" font-weight="bold">Welford / 정수 이동 합</text>
  <text x="262" y="84" fill="currentColor" font-family="monospace" font-size="12">n, mean, M2  (24 B)</text>
  <rect x="252" y="115" width="230" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="262" y="135" fill="currentColor" font-weight="bold">고정소수점 EMA</text>
  <text x="262" y="154" fill="currentColor" font-family="monospace" font-size="12">acc = y·2^k  (4 B)</text>
  <rect x="252" y="185" width="230" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="262" y="205" fill="currentColor" font-weight="bold">저수지 표본 k = 64</text>
  <text x="262" y="224" fill="currentColor" font-family="monospace" font-size="12">item[64], pos[64]  (524 B)</text>
  <text x="500" y="65" fill="currentColor">평균·분산: 정확값과 일치</text>
  <text x="500" y="135" fill="currentColor">수준(level) 추적: 지연 짧음</text>
  <text x="500" y="205" fill="currentColor">분위수 p50/p90/p99: 추정치</text>
  <rect x="20" y="255" width="700" height="34" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="32" y="277" fill="currentColor">비교 기준 "전부 저장": int32 × 50 000 = 200 000 B → 정렬 분위수, 정확한 분산</text>
</svg>
```

데이터 생성기는 모두 결정적(xorshift32, 고정 시드)이다. "전부 저장" 쪽 기준값은 정수 데이터에 대해 n·Σx² − (Σx)² 를 `__int128`(GCC/Clang 확장)로 **정확히** 계산한 뒤 나눗셈 한 번으로 만든다 — 이 맥(arm64)에서는 `long double` 이 `double` 과 같은 64비트라 기준값으로 못 쓴다.

### Part A — Welford 평균/분산과 수치 안정성

- `welford_push(w, x)` (double), `welford_f_push(w, x)` (float) — 한 샘플 갱신.
- `welford_merge(a, b)` — 두 누산기 합치기(Chan 등). 빈 누산기는 항등원.
- `istats_push(s, x)` — 정수 ADC 용: K = 첫 샘플, d = x − K, Σd(int64)·Σd²(uint64) 정확 누산, 넘칠 샘플은 거부.
- 비교: naive float/double, two-pass(전부 저장), 정확값. 평균 0 / 1e8 / 1e9 데이터, 300개 스트림 × 무작위 조각 병합, 24-bit ADC(영점 8 000 000 count) float.

### Part B — 정수 EMA 의 정체 구간과 고정소수점 해법

- `asr_floor(v, k)` = ⌊v / 2^k⌋ — 음수 `>>` 에 기대지 않는다.
- 주어진 `ema_shift_step` (`y += ⌊(x−y)/2^k⌋`), 비교용 `ema_trunc_step` (C 의 `/`), `ema_round_step` (반올림).
- `ema_fx_step(e, x)` — `acc += x − ⌊acc/2^k⌋`, 출력 ⌊acc/2^k⌋.
- 테스트: 출발점 3개 × 목표 X = −600..600 × k = 1..8 계단에서 최종 오차가 증명한 식과 같은지(전수), 12-bit ADC 랜덤 워크 2만 샘플에서 매 샘플 `0 ≤ Y − y < 2^k`, `|y_fx − Y| < 1`, int16 전 범위 · k = 15 에서 넘침 없음(UBSan).

### Part C — 편향 없는 저수지 샘플링과 카이제곱 검정, 텔레메트리 요약

- `rand_below(next, ctx, bound, bits)` — [0, bound) 균일, 거부 샘플링. 8-bit 단어로 **전수** 검사(bound 1..256), 32-bit 에서 bound = 3·2^30 편향 실측.
- `reservoir_push(r, x)` — Algorithm R, k ≤ 64, 각 표본의 스트림 위치도 저장.
- 검정 ① n = 7, k = 3: 35개 부분집합 균일(다항 χ², df 34) ② n = 101, k = 10: 포함 횟수(보정 χ², df 100).
- 지연 텔레메트리 N = 50 000: 평균·분산·최소·최대·EMA·k = 64 표본 → 전부 저장한 정확한 분위수와 비교.

## 2. Problem (English)

You are writing the statistics block of a sensor node with no heap and a few hundred bytes of RAM per channel.

```c
void     welford_push(welford_t *w, double x);            /* running mean / M2 */
void     welford_merge(welford_t *a, const welford_t *b); /* a <- a U b (Chan et al.) */
bool     istats_push(istats_t *s, int32_t x);             /* exact shifted integer sums; false on overflow */
int32_t  asr_floor(int32_t v, unsigned k);                /* floor(v / 2^k) without relying on signed >> */
int32_t  ema_fx_step(ema_fx_t *e, int32_t x);             /* integer EMA, alpha = 2^-k, no dead band */
uint32_t rand_below(uint32_t (*next)(void *), void *ctx, uint32_t bound, unsigned bits); /* unbiased */
bool     reservoir_push(reservoir_t *r, int32_t x);       /* Algorithm R, k <= 64 */
```

Constraints: O(1) work per sample (reservoir: O(1) expected), no malloc, no storing the stream. Variance must stay accurate when |mean| / stddev is 10^9. The EMA must settle to exactly x for a constant input from either direction, and must stay within 1 LSB of the real-valued EMA. Every k-subset of the stream must be equally likely to be the final reservoir.

Example: `{2, 4, 4, 4, 5, 5, 7, 9}` → mean 5, M2 32, sample variance 32/7. Integer EMA with k = 4, input stepping 0 → 100: the naive version stops at 85; the fixed-point version reaches 100.

Follow-ups: Why does Σx² − (Σx)²/n fail and how badly? How do you merge statistics from two cores? What is the steady-state error of `y += (x − y) >> k` and is it symmetric? Is `rand() % n` good enough? How do you *test* that a sampler is uniform?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter -lm && ./starter
```

TODO 는 여덟 군데: `welford_push`, `welford_f_push`, `welford_merge`, `istats_push`, `asr_floor`, `ema_fx_step`, `rand_below`, `reservoir_push`. naive 공식, 분산 읽기 함수, 비교용 EMA 세 가지, 카이제곱 생존함수, 하네스는 주어진다. `rand_below` 스텁은 흔한 오답 `next() % bound` 라 실행은 되지만 편향 검사에서 걸린다. 처음 실행 결과 (실제 출력 일부):

```text
  off=1000000000  ±1    exact s2=0.664726     naive=-558183.558156(rel 8.4e+05) welford rel 1.0e+00 two-pass rel 2.8e-12 ...
  asr_floor: 13 shifts x 10001 values + 32 x 6 edges, mismatches=120112  (this compiler: -5>>1 = -3)
  rand_below exhaustive 8-bit: bounds 1..256, bad=11706
  bound=3*2^30: P(r<2^30) rejection=0.4994 (want 1/3)  plain modulo=0.4992
  subsets n=7 k=3: T=70000, 35 cells, chi2=70000.00 df=34 p=0.000 (bad trials 70000)
FAIL starter.c:491  p > 0.001
15/64 passed
```

(0.1 초, exit 1, FAIL 49줄. 통과한 15개는 naive·two-pass 쪽 관찰, 주어진 함수(`ema_trunc_step`, `chi2_sf_even`) 검사, n = 0 · 카운터 끝 같은 경계처럼 TODO 와 무관한 것들.)

## 4. 힌트

<details><summary>힌트 1 — Welford 는 "평균을 먼저, M2 는 옛 δ 와 새 편차의 곱"</summary>

δ = x − mean_old 를 먼저 구하고, n 을 늘린 뒤 mean += δ/n. 그다음 M2 += δ·(x − mean_new). 두 인수가 하나는 옛 평균, 하나는 새 평균 기준이라는 게 핵심이다(유도는 5. 풀이). 순서를 바꿔 δ² 를 더하면 M2 가 n/(n−1) 배쯤 커진다.

</details>

<details><summary>힌트 2 — 병합은 "평균 차이가 만드는 몫" 을 더한다</summary>

두 묶음의 평균이 δ 만큼 다르면, 합친 묶음의 제곱 편차 합에는 각 묶음 안쪽(M2_a + M2_b) 말고도 "각 묶음 평균이 전체 평균에서 떨어진 만큼" 이 들어간다: δ²·n_a·n_b/n. n_b = 1, M2_b = 0 으로 놓으면 Welford 한 걸음이 나와야 한다 — 검산용.

</details>

<details><summary>힌트 3 — 정수 이동 합: 무엇이 넘치나</summary>

d = x − K 는 int32 둘의 차라 |d| ≤ 2^32 − 1 → int64 로. d² 는 2^64 에 가까울 수 있으니 **int64 로 곱하면 UB**, |d| 를 uint64 로 바꿔 곱한다. 더하기 전에 `d2 > UINT64_MAX − s2` 면 거부. 분산 = (Σd² − (Σd)²/n)/(n−1) — K 가 평균 근처라 상쇄가 작다.

</details>

<details><summary>힌트 4 — ⌊v/2^k⌋ 를 음수 시프트 없이</summary>

v ≥ 0 이면 `v >> k`. v < 0 이면 ~v = −v−1 ≥ 0 이고, ⌊v/2^k⌋ = −1 − ⌊(−v−1)/2^k⌋ 이다. 고정소수점 EMA 는 출력이 아니라 acc = y·2^k 를 상태로 들고 다니면 버림 오차가 acc 의 아래 k 비트에 남는다.

</details>

<details><summary>힌트 5 — 거부 샘플링 문턱과 Algorithm R 의 j</summary>

[0, 2^bits) 단어 중 맨 아래 t = 2^bits mod bound 개를 버리면 남은 2^bits − t 개는 bound 의 배수다. i 번째(0부터) 항목은 j = U[0, i] — 상한이 **i + 1 개**. j < k 면 칸 j 를 바꾼다.

</details>

## 5. 풀이

**면접에서 말하는 순서**

| 단계 | 말할 것 |
|---|---|
| 1. 확인 질문 | 데이터가 정수(ADC)인가 실수인가? 평균과 σ 의 대략적 크기? 표본분산(n−1) 인가 모분산(n) 인가? FPU 가 단정도뿐인가? ISR 에서 갱신하나? 표본은 "균일" 이 목표인가 "최근 위주" 인가? |
| 2. 무식한 방법 | 전부 저장 → two-pass 분산, 정렬 → 분위수, 무작위 섞기로 표본. 메모리 O(N), 스트림이 끝나야 답. |
| 3. 핵심 아이디어 + 불변식 | Welford: 매 순간 (n, mean, M2) = 지금까지 본 샘플의 정확한 평균·제곱 편차 합 (실수 연산으로). EMA: acc − 2^k·Y ∈ [0, 2^k). 저수지: i 개를 본 뒤 모든 k-부분집합의 확률 = 1/C(i, k). |
| 4. 코드 | 8개 함수, 전부 10줄 이하. |
| 5. 소리 내 말할 테스트 | n = 0/1, 상수 스트림(분산 정확히 0), 큰 평균 작은 분산, 빈 조각 병합, 계단 위·아래, int16 극값·k = 15, bound 가 2의 거듭제곱 아닌 경우, n ≤ k, 카운터 끝, 카이제곱. |
| 6. 복잡도 | 시간 O(1)/샘플 (저수지: 난수 1개, 거부 기대 횟수 < 2). 메모리: 24 B / 4 B / k·8 + 12 B. 스택 몇십 바이트. |

### Part A — naive 공식은 왜 무너지나

S = Σx² − (Σx)²/n 은 수학적으로 맞다. 문제는 **크기가 거의 같은 두 큰 수의 뺄셈**이다. 평균 μ, 표준편차 σ 이면 Σx² ≈ n(μ² + σ²), (Σx)²/n ≈ nμ². 각각을 상대오차 u(double 은 2^-53 ≈ 1.1e−16) 로 들고 있어도 차이 nσ² 에 남는 상대오차는 대략 u·μ²/σ² 배로 커진다 — 즉 **κ² 에 비례**(κ ≈ 1 + |μ|/σ). 기초는 NUM-B02 (파국적 상쇄) 참고. 실제로 결과가 **음수**가 되기도 한다 (평균 1e9, σ ≈ 0.8: −558 183).

`demo_cancel.c` 실측 (n = 10^5, 정확값 대비 상대오차, 실제 출력):

```text
double, n=100000, x = offset + U{-100..100} (sigma ~ 58)
offset             u*k     u*k^2 |      naive    welford   two-pass   merge x8  int-shift
0              1.1e-16   1.1e-16 |    0.0e+00    1.4e-14    3.0e-14    1.4e-16    0.0e+00
1000           2.0e-15   3.7e-14 |    1.5e-14    1.6e-14    3.0e-14    1.6e-15    0.0e+00
100000         1.9e-13   3.3e-10 |    2.4e-10    9.6e-14    2.3e-15    9.9e-14    0.0e+00
1000000        1.9e-12   3.3e-08 |    8.7e-05    4.4e-13    2.3e-15    3.8e-14    0.0e+00
10000000       1.9e-11   3.3e-06 |    3.4e-03    1.0e-11    4.2e-13    4.3e-12    0.0e+00
100000000      1.9e-10   3.3e-04 |    6.9e-02    8.6e-11    1.8e-12    5.2e-11    0.0e+00
1000000000     1.9e-09   3.3e-02 |    1.1e+01    8.0e-10    8.9e-14    3.1e-10    0.0e+00
10000000000    1.9e-08   3.3e+00 |    2.2e+02    1.0e-08    1.8e-12    6.6e-10   (>int32)
```

읽는 법: naive 열은 u·κ² 열을 따라 **두 자리씩** 자라고(오프셋 10배 → 오차 100배), Welford 열은 u·κ 열을 따라 **한 자리씩** 자란다 (naive/(u·κ²) ≈ 200, Welford/(u·κ) ≈ 0.4~0.5 로 거의 일정). two-pass 는 평균을 먼저 빼고 제곱하니 상쇄가 없다 — 하지만 **두 번 훑어야**(= 저장해야) 한다. 정수 이동 합은 이 데이터에서 정확히 0 오차. Chan·Golub·LeVeque(1983)가 이 알고리즘들의 반올림 오차 한계를 비교한 논문이다 — 한계식의 정확한 꼴은 원문 확인 권장, 여기서는 **실측 기울기**만 주장한다.

### Part A — Welford 갱신식 유도

m_n = 평균, M_n = Σ_{i≤n}(x_i − m_n)². δ = x_n − m_{n−1} 이라 하자.

1. 평균: m_n = ((n−1)m_{n−1} + x_n)/n = m_{n−1} + δ/n.
2. M_n − M_{n−1} = (x_n − m_n)² + Σ_{i<n}[(x_i − m_n)² − (x_i − m_{n−1})²]. 괄호 안은 (m_{n−1} − m_n)(2x_i − m_n − m_{n−1}) 이고, i<n 에 대해 더하면 Σx_i = (n−1)m_{n−1} 이므로 (n−1)(m_{n−1} − m_n)² 이 된다.
3. m_n − m_{n−1} = δ/n, x_n − m_n = δ(n−1)/n 를 넣으면 M_n − M_{n−1} = δ²(n−1)²/n² + (n−1)δ²/n² = δ²(n−1)/n = **δ·(x_n − m_n)**.

그래서 코드는 `d = x − mean; mean += d/n; m2 += d*(x − mean)` — 곱의 한 인수는 옛 평균, 하나는 새 평균 기준이다. M2 는 항상 "편차의 제곱" 을 더하므로 상쇄가 없고, 실수 연산에서도 한 걸음 증가량 δ²(n−1)/n ≥ 0 이 큰 수끼리의 뺄셈 없이 계산된다. 남는 오차원은 mean 자체의 반올림(|μ|·u 크기)이 δ 에 들어가는 것 — 그래서 κ 에 비례한다.

손 계산 (`{2,4,4,4,5,5,7,9}`, solution 출력의 마지막 줄 `mean=5 m2=32` 과 일치):

| n | x | δ = x − m_old | m_new | δ·(x − m_new) | M2 |
|---|---|---|---|---|---|
| 1 | 2 | 2 | 2 | 0 | 0 |
| 2 | 4 | 2 | 3 | 2 | 2 |
| 3 | 4 | 1 | 3.3333 | 0.6667 | 2.6667 |
| 4 | 4 | 0.6667 | 3.5 | 0.3333 | 3 |
| 5 | 5 | 1.5 | 3.8 | 1.8 | 4.8 |
| 6 | 5 | 1.2 | 4.0 | 1.2 | 6.0 |
| 7 | 7 | 3 | 4.4286 | 7.7143 | 13.7143 |
| 8 | 9 | 4.5714 | 5 | 18.2857 | **32** |

### Part A — Chan 병합: 코어 두 개, DMA 블록 여러 개

묶음 A(n_a, m_a, M_a), B(n_b, m_b, M_b), δ = m_b − m_a, n = n_a + n_b. 합친 평균 m = m_a + δ·n_b/n. 합친 M = Σ_A(x − m)² + Σ_B(x − m)². Σ_A(x − m)² = M_a + n_a(m_a − m)² (평행 이동 공식, 교차항은 Σ_A(x − m_a) = 0 이라 사라짐). m_a − m = −δ n_b/n, m_b − m = δ n_a/n 이므로

M = M_a + M_b + n_a(δ n_b/n)² + n_b(δ n_a/n)² = M_a + M_b + **δ²·n_a·n_b/n**.

n_b = 1, M_b = 0 이면 δ²(n−1)/n — Welford 한 걸음과 같다(검산). 이 식은 Chan·Golub·LeVeque 의 pairwise 알고리즘이 쓰는 결합식이다 (1979 보고서 / 1983 논문 — 원문 식 번호는 확인 권장). 쓰임새: 코어별·ISR별 누산기를 태스크에서 합치기, DMA 블록마다 지역 통계 → 전역, 장비 여러 대의 통계를 서버에서 합치기. 빈 쪽은 항등원으로 먼저 처리해야 0 으로 나누지 않는다.

검사: 300개 스트림(n 2..3001, 오프셋 ±1e9, 잡음 폭 무작위)을 0~8 군데 무작위로 잘라(빈 조각 포함) 조각마다 Welford → 차례로 병합. 정확값 대비 상대오차 / (u·κ) 의 최댓값 **1.15** (실측). 위 표의 "merge x8" 열은 한 줄로 8 조각 병합인데 단일 Welford 보다 오히려 작다 — 조각마다 순차 갱신 횟수가 1/8 로 줄기 때문(pairwise 합산과 같은 효과).

### Part A — 펌웨어: 단정도 FPU 와 정수 이동 합

Cortex-M4F 의 FPU(FPv4-SP)는 **단정도만** 있다. 같은 Welford 를 double 로 쓰면 소프트웨어 에뮬레이션이 된다 — `demo_m4.c` 를 Cortex-M4 로 컴파일한 실제 결과: float 판은 `vsub.f32 / vcvt.f32.u32 / vdiv.f32 / vadd.f32 / vmul.f32` 몇 개, double 판은 `bl __aeabi_dsub`, `__aeabi_ui2d`, `__aeabi_ddiv`, `__aeabi_dadd`, `__aeabi_dmul` 호출 7번. (VDIV.F32 는 14 사이클 정도로 알고 있다 — 확인 권장.) Cortex-M0 에서는 float 판도 전부 `__aeabi_f*` 호출이다(실제 출력 확인).

그런데 float 의 u = 2^-24 ≈ 6e−8 이라 κ 가 조금만 커도 무너진다. 24-bit 로드셀 ADC(영점 8 000 000 count, 잡음 ±8, 값은 모두 2^24 미만이라 float 로 정확히 표현됨) 실측:

```text
float (Cortex-M4F style), n=100000, x = offset + U{-8..8} (sigma ~ 4.9), all x exact in float
offset            uf*k    uf*k^2 |  naive f32 welford f32  int-shift
0              6.0e-08   6.0e-08 |    4.3e-08    1.0e-06    0.0e+00
1000           1.2e-05   2.5e-03 |    6.4e+00    6.6e-06    0.0e+00
100000         1.2e-03   2.5e+01 |    1.2e+04    1.1e-03    0.0e+00
8000000        9.7e-02   1.6e+05 |    6.8e+08    9.3e-02    0.0e+00
```

naive float 는 오프셋 1000 에서 이미 쓰레기(640% 오차), Welford float 는 9.3% — "κ·u_f ≈ 0.1" 이 그대로 나온다. **정수 데이터면 정수로 세라**: K = 첫 샘플, d = x − K 의 Σd·Σd² 는 정확하고(덧셈·곱셈만), 분산은 읽을 때 한 번만 실수로 계산한다. 읽기 오차는 κ' = 1 + |평균 − K|/σ 의 제곱에 비례하는데 K 가 첫 샘플이면 |평균 − K| 는 보통 σ 몇 배라 무시할 만하다 (Chan 등이 "shifted data" 로 논의하는 방법; 첫 샘플이 튀는 값이면 κ' 가 커진다 — 부팅 직후 몇 샘플을 버리고 K 를 잡는 게 실무 요령).

넘침 한계(코드의 가드가 지키는 것):

| 양 | 최악 크기 | 타입 | 몇 샘플까지 안전 |
|---|---|---|---|
| d = x − K | 2^32 − 1 | int64 | — |
| d² | (2^32 − 1)² < 2^64 | **uint64 로 곱함** (int64 곱은 UB) | — |
| Σd² | n·d²_max | uint64 | 24-bit ADC 전 범위(|d| < 2^24): n < 2^16, 실제 잡음 |d| < 2^8: n < 2^48 |
| Σd | n·|d|_max | int64 | n < 2^31 (|d| < 2^32) |

### Part B — 정수 EMA 의 정체 구간: 정리와 증명

EMA(지수 이동 평균, exponential moving average) Y' = Y + α(x − Y), α = 2^-k 는 1차 IIR 저역통과 필터다 (시정수·−3 dB 주파수는 DSP-B07, 이동 평균은 ALGO-B10). 정수로는 흔히 `y += (x − y) >> k` 라고 쓴다. 오차 e = x − y 로 보면 상수 입력에서 e' = e − ⌊e/2^k⌋ (>> 가 바닥 나눗셈일 때).

**정리 1 (바닥 시프트).** 상수 x, 처음 오차 e_0 이면 최종 오차는 e_0 > 0 일 때 min(e_0, 2^k − 1), e_0 ≤ 0 일 때 0.

증명. (i) 0 ≤ e < 2^k 이면 ⌊e/2^k⌋ = 0 → 멈춤. (ii) e ≥ 2^k 일 때 g(e) = e − ⌊e/2^k⌋ 는 e 에 대해 단조 비감소(e 가 1 늘면 ⌊·⌋ 는 0 또는 1 늘어남)이고 g(2^k) = 2^k − 1. 그러므로 e ≥ 2^k 이면 e' ≥ 2^k − 1, 그리고 e' < e. 엄격히 줄면서 2^k − 1 밑으로는 못 내려가니 정확히 2^k − 1 에서 멈춘다. (iii) e < 0 이면 ⌊e/2^k⌋ ≤ −1 이라 e' ≥ e + 1, 그리고 e 가 정수이므로 ⌊e/2^k⌋ ≥ e 라 e' ≤ 0. 0 에 닿을 때까지 엄격히 증가. ∎

즉 **위로 가는 계단에서만** 최대 2^k − 1 LSB 모자란 채로 영원히 멈추고, 아래로는 끝까지 간다 — 비대칭 편향. 같은 논리로 변형 셋을 정리하면 (전수 검사로 확인한 식):

| 변형 | 한 걸음 | 정체 구간 (오차 e = x − y) | 0 → 100, k = 4 |
|---|---|---|---|
| 바닥 시프트 `>>` (ASR) | ⌊e/2^k⌋ | [0, 2^k − 1] — 한쪽 | y = 85 에서 멈춤 |
| C 의 `/` (0 쪽 버림) | trunc(e/2^k) | [−(2^k − 1), 2^k − 1] — 양쪽 | y = 85 |
| 반올림 `+2^(k−1)` | ⌊(e + 2^(k−1))/2^k⌋ | [−2^(k−1), 2^(k−1) − 1] — 폭 절반 | y = 93 |
| **고정소수점 acc** | 출력 기준 | **없음** — 항상 x 로 | **100** |

**C 표준 주의.** 음수의 오른쪽 시프트 결과는 C11 6.5.7p5 에서 **구현 정의(implementation-defined)** 다(UB 는 아님). GCC 문서는 "Signed `>>` acts on negative numbers by sign extension" 이라고 명시하고, 이 맥의 clang 도 `-5 >> 1 == -3` 이었다(하네스 출력). C23 은 2의 보수를 강제하지만 음수 `>>` 는 여전히 구현 정의 목록에 있는 것으로 안다 — 확인 권장. 그래서 `asr_floor` 를 쓴다:

```c
static int32_t asr_floor(int32_t v, unsigned k) { return v >= 0 ? v >> k : ~(~v >> k); }
```

int32_t 는 2의 보수가 보장되므로(C11 7.20.1.1) v < 0 이면 ~v = −v − 1 ≥ 0, 음이 아닌 수의 시프트만 쓰고, ⌊v/2^k⌋ = −1 − ⌊(−v−1)/2^k⌋ 이다. 비용은 0 — Cortex-M4 로 컴파일하면 함수 전체가 `asrs r0, r1` 한 줄이다(실제 출력, M0 도 같음). 컴파일러가 "v ≥ 0 이면 >>, 아니면 ~(~v >> k)" 가 산술 시프트와 같다는 걸 안다.

### Part B — 고정소수점 누산기: 왜 정체가 사라지나

상태를 출력 y 가 아니라 acc = y·2^k 로 들고, `acc += x − ⌊acc/2^k⌋`, 출력 ⌊acc/2^k⌋. 상수 x 에서 a = acc − x·2^k 로 놓으면 a' = a − ⌊a/2^k⌋ — 정리 1 과 **같은 동역학**이다. 다른 점은 단위다: 이제 멈추는 구간 a ∈ [0, 2^k − 1] 이 acc 의 **아래 k 비트(소수부)** 안에 있어서 출력 ⌊acc/2^k⌋ = x + ⌊a/2^k⌋ = x. 아래에서 오면 a 는 정확히 0 까지 간다 → 역시 출력 x. 정체 구간이 출력 해상도 밑으로 내려간 것이다.

**정리 2 (실수 EMA 와의 거리).** 같은 입력 열, Y_0 = y_0 = x_0 (acc_0 = 2^k x_0). 실수 EMA Y 에 대해 매 샘플

- 바닥 시프트: 0 ≤ Y − y < 2^k,
- 고정소수점: Y ≤ acc/2^k < Y + 1, 따라서 |⌊acc/2^k⌋ − Y| < 1.

증명(고정소수점). A = 2^k Y 라 하면 A' = A(1 − 2^-k) + x. acc' = acc − ⌊acc/2^k⌋ + x = acc(1 − 2^-k) + x + r, r = acc/2^k − ⌊acc/2^k⌋ ∈ [0, 1 − 2^-k]. D = acc − A 는 D' = D(1 − 2^-k) + r. D ∈ [0, 2^k) 이면 D' ≥ 0 이고 D' < 2^k(1 − 2^-k) + (1 − 2^-k) = 2^k − 2^-k < 2^k. D_0 = 0 이니 귀납. 2^k 로 나누면 결론. ∎ 바닥 시프트도 같은 방식: D = y − Y, D' = D(1 − 2^-k) − r, r ∈ [0, 1) → D ∈ (−2^k, 0] 유지. ∎

12-bit ADC 랜덤 워크 2만 샘플(가끔 큰 계단) 실측 (solution 출력):

| k | 바닥 시프트 최대 지연 Y − y | 한계 2^k − 1 | 고정소수점 최대 |y − Y| | 한계 |
|---|---|---|---|---|
| 1 | 1.000 | 1 | 0.500 | 1 |
| 2 | 2.826 | 3 | 0.699 | 1 |
| 4 | 11.839 | 15 | 0.680 | 1 |
| 6 | 54.117 | 63 | 0.879 | 1 |
| 8 | 215.239 | 255 | 0.858 | 1 |
| 10 | 729.469 | 1023 | 0.988 | 1 |

k = 10 에서 정수 EMA 는 실수 EMA 보다 **730 LSB** 나 낮게 있었다 — 12-bit ADC 의 18% 다. 온도 보상·배터리 SoC 처럼 "느린 필터(큰 k)" 를 쓰는 곳일수록 크게 틀린다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">계단 0 → 100, k = 4 (실제 출력값으로 그림: t = 1,2,3,4,8,16,32,48,64,80,96,199)</text>
  <g stroke="currentColor" fill="none">
    <path d="M60 260 L700 260"/><path d="M60 260 L60 34"/>
    <path d="M60 40 L700 40" stroke-dasharray="2 4"/>
    <path d="M60 73 L700 73" stroke-dasharray="2 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="264">0</text><text x="30" y="154">50</text><text x="24" y="44">100</text><text x="32" y="77">85</text>
    <text x="56" y="278">0</text><text x="214" y="278">50</text><text x="374" y="278">100</text><text x="534" y="278">150</text><text x="690" y="278">200</text>
    <text x="330" y="296">샘플 t</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="3" points="60,260 63.2,246.8 66.4,235.8 69.6,224.8 72.8,213.8 85.6,178.6 111.2,130.2 162.4,84 213.6,73 696.8,73"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1.5" points="60,260 63.2,246.8 66.4,233.6 69.6,222.6 72.8,211.6 85.6,172 111.2,119.2 162.4,68.6 213.6,51 264.8,44.4 316,42.2 367.2,40 696.8,40"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="5 3" points="60,260 85.6,171.3 111.2,118.3 162.4,67.9 213.6,49.9 264.8,43.5 367.2,40.4 696.8,40"/>
  <rect x="400" y="96" width="330" height="104" rx="6" fill="none" stroke="currentColor"/>
  <text x="412" y="116" fill="currentColor">굵은 선: y += (100 − y) &gt;&gt; 4</text>
  <text x="412" y="134" fill="currentColor">→ t = 48 부터 85 고정 (오차 15)</text>
  <text x="412" y="156" fill="currentColor">가는 선: acc += x − (acc &gt;&gt; 4)</text>
  <text x="412" y="174" fill="currentColor">→ t = 96 에 100 (acc = 1600)</text>
  <text x="412" y="192" fill="currentColor" font-size="12">점선: 실수 EMA Y (가는 선과 1 미만 차이)</text>
  <text x="10" y="320" fill="currentColor">정체는 "느린 수렴" 이 아니라 영구 오차다: t = 199 에도 85.</text>
</svg>
```

트레이스 (k = 4, 실제 계산값): t = 1, 2, 3, 4, 8, 16, 32, 48 에서 바닥 시프트 y = 6, 11, 16, 21, 37, 59, 80, **85**(이후 고정) / 고정소수점 acc = 100, 194, 282, 365, 648, 1034, 1404, 1535 → 출력 6, 12, 17, 22, 40, 64, 87, 95 → t = 96 에 acc = 1600, 출력 100 / 실수 Y = 6.25, 12.11, 17.60, 22.75, 40.33, 64.39, 87.32, 95.49.

**비트 예산.** acc = x·2^k 이므로 입력 b 비트 + k 비트 + 부호. int16 입력이면 k ≤ 15 까지 int32 에 들어간다 (하네스가 INT16_MIN ↔ INT16_MAX 계단을 k = 15 로 2백만 걸음씩 돌려 UBSan 에서 깨끗한 것을 확인). 중간값 `x − ⌊acc/2^k⌋` 는 17비트, acc 에 더해도 ±2^30 + 2^16 이라 안전. 24-bit ADC 에 k = 10 이면 35비트 → int64 acc 가 필요하고, 그러면 ISR 이 쓰고 태스크가 읽을 때 **찢어진 읽기(torn read)** 를 조심해야 한다(8. Q&A).

**코드 크기.** k 가 상수인 `ema_fx_step_k4` 는 Cortex-M4 에서 `ldr / sub.w r1, r1, r0, asr #4 / add / asrs r0, r1, #4 / str` — 시프트가 뺄셈에 붙은 5개 명령어다(실제 출력). 고정소수점이 "더 비싸다" 는 건 오해다.

### Part C — 편향 없는 범위 난수

`r % bound` 는 r 이 [0, 2^w) 균일이어도 bound 가 2^w 를 나누지 않으면 편향된다: 앞쪽 (2^w mod bound) 개의 나머지가 한 번씩 더 나온다. bound = 3·2^30 이면 [0, 2^30) 이 두 번씩 → 확률 1/2(원래 1/3). 실측: 나머지 0.4992, 거부 샘플링 0.3325.

**거부 샘플링.** t = 2^w mod bound. r < t 면 버리고 다시 뽑는다. 증명: 받아들이는 단어 [t, 2^w) 는 2^w − t = bound·⌊2^w/bound⌋ 개, 연속한 정수 bound·q 개의 나머지는 각 값이 정확히 q 번 → 받아들인 조건부로 r % bound 는 균일. 받아들일 확률 = 1 − t/2^w > 1/2 (t < bound ≤ 2^w 이고 t ≤ 2^w − bound 이므로 t < 2^w/2), 그래서 기대 시도 < 2, m 번 넘게 실패할 확률 < 2^-m. t 는 32-bit 에서 `(0u − bound) % bound` 로 구한다(부호 없는 뺄셈은 mod 2^32 로 정의됨).

검사는 **전수**: 8-bit 카운터 단어원(0, 1, …, 255)을 한 주기 정확히 소비하면 각 나머지가 정확히 ⌊256/bound⌋ 번 나와야 하고 호출 수는 256 − (256 mod bound). bound = 1..256 전부 bad = 0. 스텁(`% bound`)은 bad = 11706.

**정직한 각주 둘.** (1) xorshift32 는 0 을 내지 않아서 단어가 [1, 2^32) 의 2^32 − 1 개 값 위에서 균일하다 — 나머지 0 쪽 칸이 2^32/bound 개 중 하나 모자라는 2^-32 급 편향이 남는다. 이 레슨의 검정 크기로는 안 보이지만 "완벽히 균일" 이라 말하면 틀린다. (2) Lemire 의 곱셈-시프트 방법(ACM TOMACS 29(1), 2019)은 나눗셈을 대부분 피하면서 편향이 없다 — Cortex-M0 처럼 나눗셈기가 없어 `%` 가 `__aeabi_uidivmod` 호출이 되는 곳(이 함수를 M0 로 컴파일한 실제 출력에서 2번)에서 특히 유리하다. M4 는 `udiv` + `mls` 로 끝난다(실제 출력).

### Part C — Algorithm R 과 두 가지 증명

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Algorithm R, k = 3: i 번째 항목(0부터, i ≥ k)이 도착했을 때</text>
  <g stroke="currentColor">
    <rect x="40" y="50" width="80" height="40" class="f-ok"/><rect x="125" y="50" width="80" height="40" class="f-ok"/><rect x="210" y="50" width="80" height="40" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="80" y="75">칸 0</text><text x="165" y="75">칸 1</text><text x="250" y="75">칸 2</text></g>
  <rect x="420" y="50" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="460" y="75" fill="currentColor" text-anchor="middle">x_i</text>
  <text x="520" y="65" fill="currentColor">j = U[0, i]  (i + 1 가지)</text>
  <text x="520" y="85" fill="currentColor">j &lt; 3 이면 칸 j ← x_i</text>
  <path d="M420 70 Q 330 120 250 92" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="300" y="132" fill="currentColor">j = 2 인 경우</text>
  <rect x="40" y="150" width="660" height="88" rx="6" fill="none" stroke="currentColor"/>
  <text x="54" y="172" fill="currentColor">새 항목이 들어갈 확률 = k/(i+1)</text>
  <text x="54" y="192" fill="currentColor">이미 있던 항목이 쫓겨날 확률 = k/(i+1) · 1/k = 1/(i+1) → 살아남을 확률 i/(i+1)</text>
  <text x="54" y="212" fill="currentColor">i 개를 본 뒤 포함 확률이 k/i 였다면 → k/i · i/(i+1) = k/(i+1)  (모두 같아진다)</text>
  <text x="54" y="230" fill="currentColor" font-weight="bold">j 를 U[0, i−1] 로 뽑으면(off-by-one) 새 항목이 k/i 로 들어온다 → 뒤쪽 편향</text>
</svg>
```

`reservoir_push`: 처음 k 개는 그대로 칸에 넣는다. 그다음 i 번째(0부터)는 j = U[0, i] 를 뽑아 j < k 면 칸 j 를 교체. 샘플당 난수 1개, 메모리 k 칸 — 스트림 길이를 미리 몰라도 된다. Vitter(1985)는 이 알고리즘을 Waterman 의 것으로 소개하고, 건너뛰기로 난수 호출을 줄인 Algorithm Z 를 제시한다(Li 1994 의 Algorithm L 은 9. 연습 1).

**증명 1 (포함 확률).** m = 지금까지 본 개수 ≥ k. 귀납 가정: 각 항목이 저수지에 있을 확률 k/m. 다음 항목은 k/(m+1) 로 들어오고, 있던 항목은 "새 항목이 들어오고 그 칸이 자기 칸" 일 때만 나가므로 남을 확률 1 − (k/(m+1))(1/k) = m/(m+1), 곱하면 k/(m+1). 기저 m = k 에서 1. ∎

**증명 2 (모든 k-부분집합 균일).** 포함 확률이 k/n 이라고 해서 부분집합이 균일한 건 아니다(예: "연속한 k 개 블록 중 하나를 고르기" 도 포함 확률은 맞출 수 있다). 귀납 가정 P(저수지 = S) = 1/C(m, k). m+1 번째를 본 뒤 크기 k 집합 S 에 대해: (a) S 가 새 항목을 포함하지 않으면 이전 저수지가 S 이고 새 항목이 안 들어와야 함: (1/C(m,k))·(m+1−k)/(m+1). (b) S 가 새 항목을 포함하면 이전 저수지는 S 에서 새 항목을 빼고 S 밖의 y 하나를 더한 것(y 의 선택지 m − (k−1) 개), 새 항목이 들어오고 그 칸이 y 의 칸: (m−k+1)·(1/C(m,k))·(k/(m+1))(1/k). 둘 다 (m+1−k)/((m+1)C(m,k)) = 1/C(m+1, k) (C(m+1,k) = C(m,k)(m+1)/(m+1−k) 이므로). ∎

### Part C — 카이제곱으로 균일성을 "검사" 하는 법

증명이 있어도 **구현**은 틀릴 수 있다(off-by-one, 편향된 난수, 칸 고르기 실수). 결정적 시드로 많이 돌려 분포를 검정한다.

**검정 ① 부분집합 다항 검정.** n = 7, k = 3 이면 부분집합이 C(7,3) = 35 개, 각각 1/35. T = 70 000 번 돌려 칸마다 횟수 O_c, 기대 E = 2000. 시행끼리 독립이라 (O_1..O_35) 는 정확히 다항분포 → Pearson X = Σ(O−E)²/E 는 근사적으로 χ²(34). 이것은 증명 2 를 직접 검사한다.

**검정 ② 포함 횟수 검정과 보정.** n 이 크면 부분집합이 너무 많으니 항목별 포함 횟수 O_i 를 센다. 그런데 한 시행에서 정확히 k 개가 포함되므로 칸끼리 **음의 상관**이 있어 Pearson X 는 χ²(n−1) 을 따르지 않는다. 한 시행의 포함 지시 벡터 Y 에 대해 p = k/n,

- Var(Y_i) = p(1−p),
- Cov(Y_i, Y_j) = k(k−1)/(n(n−1)) − p² = −p(1−p)/(n−1),

즉 공분산 행렬 = p(1−p)·n/(n−1)·(I − J/n), (I − J/n) 은 합-0 부분공간으로의 사영(계수 n−1). 중심극한정리로 (O − E)/√T → N(0, Σ) 이고 ‖O − E‖²/(T·p(1−p)·n/(n−1)) → χ²(n−1). Pearson X 는 ‖O − E‖²/(Tp) 이므로

**X_c = X · (n−1)/(n−k) ~ χ²(n−1)**.

보정 없이 χ²(n−1) 로 보면 E[X] = n − k 라 너무 너그럽다(n = 101, k = 10 이면 기대 91 을 100 짜리 분포로 봄).

**p-값을 직접 계산.** df 가 짝수 2m 이면 χ² 생존함수가 닫힌 꼴이다: P(χ²_{2m} > x) = e^{−x/2} Σ_{j<m} (x/2)^j / j! (포아송 누적과 감마의 관계). 그래서 df 를 34, 100, 200 처럼 짝수가 되게 n 을 골랐다(35 칸, n = 101, n = 201). 하네스는 이 식이 표의 임계값 χ²_{100, 0.05} = 124.342 에서 0.0500 을 주는지도 확인한다 (Python 으로 같은 식을 계산해 χ²_{34, 0.05} = 48.602 → 0.0500 도 확인).

실측 (solution):

```text
  subsets n=7 k=3: T=70000, 35 cells, chi2=37.07 df=34 p=0.329 (bad trials 0)
  inclusion n=101 k=10: T=20000, E=1980.2/item, Pearson X=101.37 (p as-if-chi2 0.443), corrected X*(n-1)/(n-k)=111.40 df=100 p=0.205
```

판정 기준은 p > 0.001 (결정적 시드라 통과/실패가 고정; 맞는 구현이 이 시드에서 우연히 떨어질 확률은 검정마다 0.1%).

**어떤 검정이 어떤 버그를 잡나** — `demo_reservoir.c` 실측 (n = 7/k = 3 부분집합, n = 201/k = 10 포함, 실제 출력):

```text
sampler                            | subsets n=7 k=3 (df 34) p | inclusion n=201 k=10: corrected X (df 200), p
A  Algorithm R, rejection 32-bit   |                     0.329 |      196.4  0.559
B  off-by-one j=U[0,i-1]           |                         0 |      247.8  0.0121
C  8-bit word % (i+1)              |                     0.112 |    10667.4  0
D  8-bit word + rejection          |                     0.235 |      208.1  0.333
E  right prob, round-robin slot    |                         0 |    73725.5  0
```

- B(off-by-one)는 작은 n 의 부분집합 검정이 바로 잡지만 n = 201 포함 검정은 p = 0.012 로 **못 잡는다** — 편향이 1/i 로 줄어 긴 스트림에선 희미하다.
- C(8-bit 나머지)는 반대: bound ≤ 7 이면 256 mod 7 = 4 짜리 작은 편향이라 부분집합 검정은 통과(p = 0.112), bound 가 200 근처가 되는 포함 검정이 잡는다(앞 56개 값이 2배).
- D: 단어가 8비트여도 **거부하면** 균일. 문제는 폭이 아니라 나머지다.
- E: 들어올 확률은 맞게(k/(i+1)) 했지만 칸을 차례로 고르면 남을 확률이 기억을 갖게 돼 둘 다 실패.

→ 검정은 **두 종류**를 다 둔다.

### Part C — 텔레메트리: 요약 vs 전부 저장

CAN 요청→응답 지연(µs) 5만 샘플: 기본 120..135, 5% 꼬리 +50..449, 0.1% 스파이크 +5000 이상. 펌웨어 쪽은 한 번 훑으며 정수 이동 합 + Welford + 고정소수점 EMA(k = 4) + 저수지 k = 64 + 최소/최대. 실제 출력:

```text
  telemetry N=50000: mean 147.9992 (exact 147.9992)  sd 231.3761 (exact 231.3761)  welford sd 231.3761  min 120 max 7979  ema(k=4) 145
  quantiles  full-store p50/p90/p99 = 128/135/513   reservoir k=64 p50/p90/p99 = 129/135/500   samples below true median: 28/64
  RAM: full store 200000 B  vs  summary 564 B (istats 24 + ema 8 + reservoir 524 + min/max 8)
```

| 항목 | 요약(564 B) | 전부 저장(200 000 B) | 관계 |
|---|---|---|---|
| 평균·분산 | 147.9992 / sd 231.3761 | 같음 | 정수 합: 정확 (상대오차 < 1e−12 CHECK) |
| 최소·최대 | 120 / 7979 | 같음 | 정확 |
| p50 / p90 | 129 / 135 | 128 / 135 | 표본 추정 |
| p99 | 500 | 513 | k = 64 로는 p99 를 **못 믿는다** (위 1% 에 기대 0.64 개) |

검사 가능한 통계적 주장 하나: 저수지의 64개 중 "진짜 중앙값보다 아래(순위 기준, 동률은 위치로 깸)" 인 개수 c 는 무작위 표본이면 초기하분포이고, Bin(64, 1/2) 보다 더 집중된다. Bin 에서 [22, 42] 밖일 확률 0.8% (Python 으로 계산) → CHECK(22 ≤ c ≤ 42), 실측 28. 실무 해석: **64개 표본이면 정렬 후 22번째~43번째 값 사이에 진짜 중앙값이 99% 이상 확률로 있다.** p99 를 원하면 k 를 수백 이상으로 늘리거나, 상위 꼬리만 따로 추적(ALGO-M11 의 top-K 힙)하거나, 히스토그램/스케치(t-digest 등 — 범위 밖)를 쓴다.

샘플당 비용 (`demo_reservoir.c`, 이 맥 Apple M6, Apple clang 21, -O2, N = 10^7, 1회 예열 후 5회 중앙값, 측정 직전 `uptime` load average **60.31** — 다른 작성 작업이 동시에 도는 중이라 절대값보다 비율):

```text
  welford_push (double)        4.50 ns  (min 3.91, max 5.25)
  welford_f_push (float)       3.82 ns  (min 3.44, max 4.16)
  istats_push (int64 sums)     1.04 ns  (min 0.77, max 1.25)
  ema_fx_step (k=4)            0.73 ns  (min 0.68, max 0.80)
  reservoir_push (k=64)        1.89 ns  (min 1.74, max 3.69)
```

Welford 가 가장 비싼 이유는 샘플마다의 **나눗셈**이 다음 샘플의 mean 에 의존하는 직렬 체인이기 때문(나눗셈 지연이 그대로 처리량). 정수 이동 합은 나눗셈이 읽을 때 한 번뿐이라 4배 빠르다. MCU 에서는 이 차이가 더 크다(M4F: VDIV, M0: 소프트웨어 나눗셈).

**복잡도 요약.** 모두 샘플당 O(1) 시간. 저수지는 난수 1개(거부 기대 < 2회) + 비교 1번. 메모리: Welford 24 B, istats 24 B, EMA 4~8 B, 저수지 k·(4 + 4) + 12 B. 재귀 없음, 스택은 지역 변수 몇 개.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san -lm && ./solution_san
```

실제 출력 (앞부분; 나머지 줄은 5. 풀이에 인용):

```text
== Part A: Welford, Chan merge, shifted integer sums ==
  small: n=8 mean=5 m2=32 s2=4.5714285714285712 (want 5, 32, 32/7)
  off=0           ±100  exact s2=3355.372824  naive=3355.372824  (rel 0.0e+00) welford rel 1.4e-14 two-pass rel 3.0e-14 mean abs err 3.2e-15
  off=100000000   ±100  exact s2=3353.939885  naive=3734.278623  (rel 1.1e-01) welford rel 9.1e-13 two-pass rel 1.7e-14 mean abs err 2.5e-07
  off=1000000000  ±1    exact s2=0.664726     naive=-558183.558156(rel 8.4e+05) welford rel 1.5e-07 two-pass rel 2.8e-12 mean abs err 9.5e-06
  merge: 300 streams, 0-8 random cuts (empty chunks allowed): worst rel err / (u*kappa) = 1.15
  float ADC 8e6±8, n=100000: exact s2=24.050160 | naive float 1.63829e+10 (rel 6.8e+08) | welford float 26.290410 (rel 9.3e-02) | int shift 24.050160 (rel 0.0e+00)
== Part B: integer EMA dead band and fixed-point fix ==
  asr_floor: 13 shifts x 10001 values + 32 x 6 edges, mismatches=0  (this compiler: -5>>1 = -3)
  step y0 -> X (y0 = 0/-700/700, X = -600..600, k = 1..8): shift-floor bad=0 trunc bad=0 round bad=0 fixed-point bad=0 (max iters 587)
  ...
== Part C: unbiased bounded random, reservoir sampling, chi-square ==
  rand_below exhaustive 8-bit: bounds 1..256, bad=0
  bound=3*2^30: P(r<2^30) rejection=0.3325 (want 1/3)  plain modulo=0.4992
  ...
64/64 passed
```

-O2 0.05 초, ASan/UBSan 0.34 초, 보고 없음 — 그리고 두 빌드의 출력이 **글자 하나까지 같았다**(`diff` 로 확인; 부동소수점 결과가 최적화 수준에 따라 달라지지 않음). 평균 1e9 의 Welford 오차 1.5e−7 은 버그가 아니라 u·κ ≈ 1.4e−7 그대로다 — 그래서 CHECK 는 고정 문턱이 아니라 `16·u·κ + n·u` 로 건다.

데모 빌드:

```bash
cc -std=c11 -O2 demo_cancel.c -o demo_cancel -lm && ./demo_cancel          # 5. 풀이 Part A 표
cc -std=c11 -O2 demo_reservoir.c -o demo_reservoir -lm && ./demo_reservoir # 샘플러 5종 검정 + ns/sample
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -S demo_m4.c -o -
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_m4.c -o -   # 나눗셈 · float 가 라이브러리 호출로
```

## 7. 자주 하는 실수

아래 "실측" 은 solution.c 사본에 한 줄씩 버그를 넣어 돌린 결과다(작업용 사본, 레슨 폴더에는 없음).

**1. Welford 순서를 바꾼다 — M2 를 옛 평균으로만.**

```c
double d = x - w->mean;
w->m2 += d * (x - w->mean);      /* 틀림: 아직 옛 평균 → δ² */
w->mean += d / w->n;
```

δ² 를 더하면 정답 δ²(n−1)/n 보다 크다. 실측 54/64: 손 계산(M2 = 32), 상수 스트림(M2 = 0 이어야 함), κ 오차 3건, 병합 비교 전부 FAIL. → mean 을 먼저 갱신하고 `d * (x − mean_new)`.

**2. 저수지 j 의 상한을 i 로 (off-by-one).** `rand_below(..., seen, 32)` 면 새 항목이 k/i 로 들어온다. 실측 62/64: 부분집합 χ² = 11623 (p = 0), n = 101 포함 X_c = 302.9 (p ≈ 0). 하지만 n = 201 포함 검정만 있었다면 p = 0.012 로 통과했을 것(위 표 B) — 검정 두 종류가 필요한 이유.

**3. 범위 난수를 `r % bound` 로.** 실측 62/64: 8-bit 전수 검사 bad = 11706, bound = 3·2^30 에서 0.4994. → 거부 샘플링(또는 Lemire). 32-bit 단어에 작은 bound 면 편향이 bound/2^32 수준이라 검정으로는 안 보인다 — "테스트를 통과했으니 괜찮다" 가 아니라 **증명으로** 없애는 버그다.

**4. Chan 병합의 교차항을 n² 로 나눈다** (`δ²·n_a·n_b/n²`, 평균의 분산과 혼동). 실측 62/64: 무작위 병합 오차 / (u·κ) = 4.4e9, 단일 샘플 병합(M2 = 2 여야 함) FAIL.

**5. ⌊·⌋ 대신 0 쪽 버림(`/`)을 쓴다.** `asr_floor` 를 나눗셈으로 바꾸면 음수에서 다르다. 실측 60/64: asr 전수 불일치, 바닥 시프트·반올림 정체 식 불일치, 추적 한계 위반. 고정소수점 EMA 안에서 `acc / (1 << k)` 를 쓰면(실측 62/64) 음수 acc 를 아래에서 접근할 때 출력이 x − 1 에 멈춘다 — y0 = −700 출발 계단 검사와 int16 극값 검사가 잡는다. 0 에서만 출발하는 계단 테스트로는 **못 잡았다**(처음 하네스가 그랬고, 출발점 3개로 늘려서 잡았다).

**6. d² 를 int64 로 곱한다.** `uint64_t d2 = (uint64_t)(d * d);` — |d| = 2^32 − 1 이면 int64 곱이 넘쳐 UB. 실측: -O2 에서는 **64/64 통과**(넘친 비트가 우연히 맞는 값과 같다), UBSan 빌드에서 `runtime error: signed integer overflow: 4294967295 * 4294967295 cannot be represented in type 'int64_t'`. 테스트가 아니라 새니타이저가 잡는 버그다.

**7. n 을 늘리기 전에 나눈다.** `mean += d / n; n++;` — 첫 샘플에서 0 으로 나눈다(float 이면 inf/NaN, 정수면 HardFault 가능). "0 이면 1 로" 가드를 붙인 변형도 실측 63/64 로 float 손 계산이 FAIL.

**8. 카이제곱 포함 검정을 보정 없이 χ²(n−1) 로 본다.** 너그러운 검정이 된다(기대값 n − k 를 n − 1 짜리로 봄). 실측: 맞는 구현에서 보정 전 p = 0.443, 보정 후 0.205 — 버그가 있으면 보정 전 검정이 더 늦게 잡는다.

## 8. 면접 Q&A

<details><summary>Q1. 분산을 한 번에 계산하라고 하면 Σx² − (Σx)²/n 을 쓰면 안 되나요? 언제 괜찮나요?</summary>

평균이 σ 에 비해 크면(κ ≫ 1) 같은 크기의 두 수를 빼서 유효 숫자가 사라진다. 상대오차가 대략 u·κ² 로 자란다(실측: 평균 1e8·σ 58 에서 double 7%, 평균 1e9·σ 0.8 에서 음수 분산). 괜찮은 경우: (1) **정수 데이터를 정확한 정수 합**으로 들 때 — Σx, Σx² 가 넘치지 않는 한 정확하고, 마지막 한 번만 실수로 바꾼다(Chan 등도 "넘침 없는 정수 데이터면 교과서 공식도 괜찮다" 는 취지). (2) 평균을 대략 아는 값 K 로 먼저 빼면(shifted) κ 가 작아진다. 꼬리: "two-pass 는?" → 가장 정확하지만 두 번 훑어야 하니 저장이 필요하다. 꼬리 2: "float 로 Welford 면 충분?" → κ·u_f ≈ 0.1 이면 10% 오차(24-bit ADC 실측 9.3%).

</details>

<details><summary>Q2. Welford 식을 유도해 보세요. 왜 수치적으로 안정한가요?</summary>

m_n = m_{n−1} + δ/n (δ = x_n − m_{n−1}), M_n = M_{n−1} + δ(x_n − m_n). 유도는 M_n − M_{n−1} = (x_n − m_n)² + (n−1)(m_n − m_{n−1})² = δ²(n−1)/n (5. 풀이). 안정성: 큰 수 둘을 빼는 단계가 없고 M 에는 항상 음이 아닌 양을 더한다. 남는 오차는 m 의 반올림(크기 |μ|u)이 δ 에 들어가는 것이라 κ 에 비례한다 — 실측 기울기가 정확히 그렇다(오프셋 10배 → 오차 10배, naive 는 100배). 꼬리: "더 정확하게?" → 조각으로 나눠 Chan 병합(pairwise) — 실측 merge x8 이 단일 Welford 보다 작다. 꼬리 2: Kahan 보상 합산을 mean 에 쓰는 방법도 있다(NUM-B03).

</details>

<details><summary>Q3. 듀얼코어 MCU 에서 코어마다 통계를 모으고 합치려면? ISR 에서 갱신하고 태스크에서 읽으면?</summary>

코어마다 (n, mean, M2) 를 들고 Chan 식 m = m_a + δn_b/n, M = M_a + M_b + δ²n_a n_b/n 으로 합친다. 결합법칙이 성립해서 순서·트리 모양과 무관(반올림 차이만). ISR/태스크: 상태가 여러 워드라 태스크가 읽는 도중 ISR 이 갱신하면 **찢어진 스냅샷**(n 은 새 값, M2 는 옛 값)이 된다. 해법: (1) 읽을 때만 짧게 인터럽트 금지(PRIMASK)하고 구조체 복사, (2) ISR 은 지역 누산기에 모으고 태스크가 "교환" 할 때 병합 — 그러면 ISR 쪽 임계 구역이 거의 없다, (3) 시퀀스 카운터(seqlock)로 다시 읽기. 32비트 정렬 단일 워드(EMA 의 int32 acc)는 Cortex-M 에서 한 번의 LDR/STR 이라 찢어지지 않지만, int64 acc 는 컴파일러가 LDRD 를 쓸지 LDR 두 번을 쓸지 보장이 없으니 원자적이라고 가정하지 말 것 (C-M14 참고).

</details>

<details><summary>Q4. `y += (x − y) >> k` 의 정상 상태 오차는? 위아래 대칭인가요?</summary>

`>>` 가 산술 시프트(바닥)이면 비대칭: 위로 가는 계단은 정확히 2^k − 1 LSB 아래에서 영원히 멈추고, 아래로 가는 계단은 x 까지 간다(정리 1, 전수 확인). k = 4, 0 → 100 이면 85. 실수 EMA 대비로는 항상 0 ≤ Y − y < 2^k — 아래쪽으로만 치우친다. C 의 `/` 를 쓰면 양쪽 대칭 ±(2^k − 1), 반올림을 더하면 [−2^(k−1), 2^(k−1) − 1]. 고치는 법: acc = y·2^k 를 상태로(`acc += x − (acc >> k)`) → 출력이 정확히 x 로 가고, 매 샘플 |y − Y| < 1 이 증명된다. 꼬리: "음수 `>>` 는 표준상?" → C11 6.5.7p5 구현 정의. GCC 는 부호 확장이라고 문서화했고 clang 도 같다. 이식성이 필요하면 `asr_floor` — Cortex-M 에서 `asrs` 한 줄로 컴파일된다.

</details>

<details><summary>Q5. 저수지 샘플링이 균일하다는 걸 증명하고, 코드가 맞는지 어떻게 테스트하나요?</summary>

증명: m 개 본 뒤 포함 확률 k/m 이면 다음 단계에서 남을 확률 m/(m+1) → k/(m+1). 더 강하게, 모든 k-부분집합이 1/C(m,k) 임을 같은 귀납으로(5. 풀이). 테스트: 고정 시드로 작은 n 을 많이 돌려 (1) 부분집합 빈도 다항 χ²(df = C(n,k) − 1), (2) 큰 n 의 항목별 포함 횟수 χ², 단 칸끼리 음의 상관이라 X·(n−1)/(n−k) 로 보정. 실측으로 off-by-one 은 (1)만, 8-bit 나머지 편향은 (2)만 잡았다 — 둘 다 둬야 한다. 꼬리: "결정적 시드로 통계 검정을 하면 의미가 있나?" → 시드가 고정이라 회귀 테스트로는 결정적이고, 맞는 구현이 우연히 떨어질 확률은 α(여기선 0.1%) — 떨어지면 시드를 바꾸는 게 아니라 원인을 찾는다.

</details>

<details><summary>Q6. `rand() % n` 은 왜 안 되나요? 나눗셈기가 없는 M0 에서는?</summary>

2^w 가 n 으로 나누어떨어지지 않으면 앞쪽 (2^w mod n) 개 값이 한 번씩 더 나온다. n = 3·2^30 이면 [0, 2^30) 이 1/3 이 아니라 1/2 (실측 0.4992). 해법: 2^w mod n 보다 작은 단어를 버리는 거부 샘플링 — 기대 시도 < 2, 증명은 "남은 단어 수가 n 의 배수". M0 에는 하드웨어 나눗셈이 없어 `%` 가 `__aeabi_uidivmod` 호출이 된다(이 함수에서 실측 2회). Lemire(2019)의 곱셈-시프트 방법은 64비트 곱의 상위 워드로 범위를 만들고 드문 경우에만 나머지를 계산해 나눗셈을 대부분 없앤다. 꼬리: "실시간 경로에서 거부 루프의 최악은?" → 이론상 무한. m 회 넘게 실패할 확률 < 2^-m 이니 시도 횟수 상한(예: 8회)을 두고 넘으면 편향 허용 값으로 폴백하거나, 난수를 미리 채워 둔 버퍼를 쓴다.

</details>

<details><summary>Q7. 스트림이 2^32 개를 넘으면? 1 kHz 센서라면 언제 문제가 되나요?</summary>

`seen` 이 uint32 이면 2^32 − 1 에서 멈춰야 한다(이 구현은 false 반환 — 테스트 있음). 1 kHz 면 2^32 ms ≈ 49.7일 — 필드 장비는 충분히 넘긴다(Windows 95/98 의 "49.7일 뒤 멈춤" 문제가 같은 숫자의 ms 카운터 넘침이었다). 늘리려면 seen 을 uint64 로, 범위 난수도 64비트(두 단어로 64비트 거부 샘플링)로. Welford 의 n(uint32)도 같은 문제이고 n 이 커지면 δ/n 이 반올림에 묻혀 mean 이 더는 안 움직인다 — 장기 통계는 "기간별 누산기 + Chan 병합" 으로 끊어 두는 게 낫다. 정수 이동 합은 Σd² 넘침이 먼저 온다(5. 풀이 표).

</details>

<details><summary>Q8. 최근 데이터에 가중치를 두려면? EMA 로 분산도 추적할 수 있나요?</summary>

균일 저수지는 "지금까지 전체" 의 표본이다. 최근 위주가 필요하면 (1) 슬라이딩 윈도우(링 버퍼 + Welford 교체 갱신, 9. 연습 2 — 대신 윈도우 크기만큼 저장), (2) 지수 가중 평균/분산: mean += α·δ, var = (1 − α)(var + α·δ²) 꼴의 지수 가중 분산(Finch 2009 의 정리로 알고 있음 — 확인 권장), (3) 가중 저수지(Efraimidis–Spirakis 의 키 u^(1/w)) 로 최근 항목 가중치를 키우는 방법. 꼬리: "EMA 의 유효 윈도우는?" → 대략 2^k 샘플(시정수 τ ≈ 2^k 샘플, 정확한 식·−3 dB 는 DSP-B07).

</details>

<details><summary>Q9. 64개 표본으로 p99 지연을 보고해도 되나요? 몇 개가 필요하죠?</summary>

안 된다. p99 위쪽 1% 에 기대 0.64 개라 표본 최댓값 근처를 보는 셈이다(실측 500 vs 진짜 513 — 이번엔 가까웠지만 우연). 분위수 q 를 추정할 때 확률 단위의 퍼짐은 √(q(1−q)/k) 정도라, p99 를 ±0.5%p 로 잡으려면 1σ 기준 k ≈ 400, 95% 기준 k ≈ 1600 이다. 중앙값은 64개로도 "정렬한 22~43번째 사이" 가 99% 신뢰구간이다(Bin(64, 1/2) 계산, 실측 c = 28). 꼬리 측정이 목적이면 상위 K 개 힙(ALGO-M11), 로그 버킷 히스토그램, 또는 분위수 스케치를 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — Algorithm L: 난수 호출을 O(k log(N/k)) 로

Algorithm R 은 항목마다 난수를 하나 뽑는다(N = 10^6 이면 약 10^6 번). Li(1994)의 Algorithm L 은 "다음에 교체될 항목까지 몇 개를 건너뛸지" 를 기하분포로 한 번에 뽑는다: W = exp(ln(U)/k), 건너뛰기 s = ⌊ln U / ln(1 − W)⌋, 교체할 때마다 W ← W·exp(ln(U)/k). 구현하고, 균일성(두 검정)과 난수 호출 수를 확인하라.

<details><summary>답 보기</summary>

`resL_t` 는 W(double)와 다음 교체 위치 `next`(uint64)를 든다. 저수지가 찬 순간 W 와 첫 건너뛰기를 뽑고, `seen == next` 일 때만 무작위 칸 교체 → W 갱신 → 다음 건너뛰기. U 는 (0, 1) 개구간이어야 log 가 유한하다: xorshift32 출력이 1..2^32−1 이라 (r + 0.5)/2^32 는 0 도 1 도 아니다. 실측:

```text
ex1 Algorithm L: subsets chi2=39.61 p=0.234 | inclusion corrected X=207.2 p=0.349 | N=1e6 k=64: 1814 random calls (R needs 999936; k(1+ln(N/k))=682)
```

교체가 대략 k·ln(N/k) ≈ 618 번, 교체당 난수 3개(칸, W, 건너뛰기) → 1814. 대가: `log`/`exp` 가 필요하다(M4F 에 단정도 libm 이 있어도 수백 사이클 — 측정은 안 함). 항목 도착 자체가 이미 비싼 경우(플래시에서 읽기)에는 건너뛴 항목을 아예 안 읽어도 되는 게 진짜 이득이다.

</details>

### 연습 2 — 슬라이딩 윈도우 분산 (Welford 교체 갱신)

최근 W = 64 샘플의 분산을 매 샘플 O(1) 로. 링 버퍼에서 빠지는 값 old, 들어오는 값 x 에 대해 mean' = mean + (x − old)/W, M2' = M2 + (x − old)(x − mean' + old − mean) 임을 보이고, 평균 1e6 근처 랜덤 워크 20만 샘플에서 매 윈도우 정확값(__int128)과 비교하라. (ALGO-B10 의 이동 평균에 분산을 붙인 것.)

<details><summary>답 보기</summary>

유도: 새 윈도우와 옛 윈도우는 old 대신 x 만 다르다. Σ(y − m')² − Σ(y − m)² 를 "x 를 넣고 old 를 빼는" 두 Welford 걸음으로 쓰거나 직접 전개하면 (x − old)(x + old − m − m') 가 남는다. 반올림으로 아주 작은 음수가 될 수 있어 0 으로 자른다. 실측:

```text
ex2 sliding-window variance W=64 over 200000 samples (mean ~1e6): worst rel err 5.43e-11, windows > 1e-6: 0
```

20만 번 교체해도 표류(drift)가 작았지만, 빼기가 들어가는 갱신은 원리상 오차가 누적된다. 실무에서는 N 샘플마다(또는 윈도우가 한 바퀴 돌 때마다) 버퍼에서 다시 계산(two-pass)해 재동기화하거나, 정수 데이터면 Σ, Σ² 를 정수로 들고 더하고 빼면 **표류가 0** 이다.

</details>

### 연습 3 — 병합 가능한 저수지: bottom-k

코어 0 이 항목 0..3, 코어 1 이 항목 4..6 을 각자 본다. 두 코어의 표본을 합쳐 전체 7개 중 균일한 3개를 만들고 싶다. Algorithm R 의 결과 둘은 바로 못 합친다(각 저수지가 몇 개를 대표하는지에 따라 가중해야 함). 각 항목에 무작위 키를 붙이고 "키가 가장 작은 k 개" 를 유지하는 방식으로 풀고, 합친 결과의 부분집합 χ² 를 계산하라.

<details><summary>답 보기</summary>

모든 항목에 독립 균일 키를 주면 "키 최솟값 k 개" 는 키 순서가 균일 무작위 순열이므로 균일한 k-부분집합이다. 그리고 합집합의 최소 k 개 = 각 부분의 최소 k 개를 합친 것의 최소 k 개 → **병합이 그냥 합치기**다(Chan 병합의 저수지판). 키는 (32비트 난수 << 32) | 전역 위치 로 만들어 동률을 없앴다(동률 확률 2^-32 급의 미세 편향을 위치로 깨는 것). k 가 작으면 최댓값 찾기를 선형으로, 크면 최대 힙(DS-B12). 실측:

```text
ex3 bottom-k merged from 2 cores (4 + 3 items, k=3): chi2=22.57 df=34 p=0.933
```

대가: 항목마다 키 비교, 저수지 칸마다 키 8 B 추가.

</details>

### 연습 4 — 온라인 공분산과 온도 보상 기울기

자이로 바이어스 y(raw LSB, 오프셋 3e6)와 온도 x(0.01°C 단위 정수)를 동시에 받는다. Welford 를 공분산으로 확장해 C_xy 와 기울기 C_xy/C_xx(= 온도 1 단위당 바이어스 변화, 온도 보상 계수)를 한 번 훑기로 구하고 정확값과 비교하라.

<details><summary>답 보기</summary>

`sxy += dx * (y − my_new)` — dx 는 옛 x 평균 기준, y 쪽은 **새** 평균 기준(Welford 와 같은 "하나는 옛, 하나는 새" 구조. 대칭으로 dy·(x − mx_new) 를 써도 된다). 유도는 Welford 의 2변수판이고 병합식은 C = C_a + C_b + δx·δy·n_a n_b/n. 실측:

```text
ex4 online covariance: cov 1001728.907921 (exact 1001728.907921, rel 1.1e-13)  slope 0.749993 LSB per 0.01C (exact 0.749993)
```

펌웨어 쓰임: 공장 온도 챔버에서 바이어스-온도 직선을 메모리 없이 맞추기(최소제곱 기울기 = C_xy/C_xx). naive Σxy − ΣxΣy/n 은 y 오프셋 3e6 때문에 Part A 와 같은 상쇄를 겪는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 7/7 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san   # 7/7, 보고 없음
```

## 10. 핵심 정리

- Σx² − (Σx)²/n: 오차 ∝ u·κ² (κ ≈ |평균|/σ). 실측 평균 1e9·σ 0.8 → 음수 분산. Welford: 오차 ∝ u·κ, 식은 `δ = x − m; m += δ/n; M2 += δ·(x − m_new)`.
- Chan 병합: M = M_a + M_b + δ²·n_a·n_b/n. n_b = 1 이면 Welford 한 걸음. 코어·ISR·DMA 블록별 누산기를 합친다.
- 정수 ADC 면 정수로: K = 첫 샘플, Σd(int64)·Σd²(uint64, **uint64 로 곱하기**) 정확 누산 — M4F 에서 Welford float(9.3% 오차)보다 낫고 4배 싸다(호스트 실측).
- `y += (x − y) >> k`: 위로 가는 계단은 정확히 2^k − 1 모자란 채 멈춤, 아래로는 끝까지(비대칭). 실수 EMA 대비 0 ≤ Y − y < 2^k. 음수 `>>` 는 C11 구현 정의 → `asr_floor`(M4 에서 `asrs` 한 줄).
- 고침: `acc += x − (acc >> k)`, 출력 `acc >> k` → 정체 구간이 소수부로 내려가 출력이 정확히 x, 매 샘플 |y − Y| < 1 (D ∈ [0, 2^k) 귀납). 비트 예산: 입력 + k + 1.
- 범위 난수: 2^w mod n 개를 버리는 거부 샘플링 — 기대 시도 < 2. `% n` 은 n = 3·2^30 에서 1/3 → 1/2.
- Algorithm R: i 번째(0부터)는 j = U[0, i], j < k 면 칸 j 교체. 포함 k/n, 모든 k-부분집합 1/C(n,k) (귀납).
- 검정 두 개: 작은 n 부분집합 다항 χ², 큰 n 포함 χ² × (n−1)/(n−k). 짝수 df 면 p = e^{−x/2}Σ_{j<m}(x/2)^j/j!. off-by-one 과 나머지 편향은 서로 다른 검정이 잡는다.

## 참고자료

- B. P. Welford, "Note on a method for calculating corrected sums of squares and products", *Technometrics* 4(3), 419–420, 1962. doi:10.1080/00401706.1962.10490022
- T. F. Chan, G. H. Golub, R. J. LeVeque, "Algorithms for computing the sample variance: analysis and recommendations", *The American Statistician* 37(3), 242–247, 1983. doi:10.1080/00031305.1983.10483115 — 갱신·pairwise 알고리즘과 오차 한계 비교. 같은 저자들의 1979 Stanford 보고서 "Updating formulae and a pairwise algorithm for computing sample variances" 가 병합식의 출처로 흔히 인용된다(보고서 세부는 확인 권장).
- J. S. Vitter, "Random sampling with a reservoir", *ACM Transactions on Mathematical Software* 11(1), 37–57, 1985. doi:10.1145/3147.3165 — Algorithm R(Waterman 에게 돌림)과 Algorithm Z.
- K.-H. Li, "Reservoir-sampling algorithms of time complexity O(n(1 + log(N/n)))", *ACM Transactions on Mathematical Software*, Dec 1994. doi:10.1145/198429.198435 — Algorithm L (권·호 번호는 확인 권장).
- D. Lemire, "Fast random integer generation in an interval", *ACM Transactions on Modeling and Computer Simulation* 29(1), 2019 (arXiv:1805.10941).
- ISO/IEC 9899:2011 (C11) 6.5.7p5 — 음수의 오른쪽 시프트는 구현 정의; 7.20.1.1 — intN_t 는 2의 보수. GCC 매뉴얼 "Integers implementation": <https://gcc.gnu.org/onlinedocs/gcc/Integers-implementation.html> ("Signed `>>` acts on negative numbers by sign extension").
- N. J. Higham, *Accuracy and Stability of Numerical Algorithms*, 2nd ed., SIAM 2002 — 표본 분산 계산의 오차 분석 절(절 번호는 확인 권장).
- P. Efraimidis, P. Spirakis, "Weighted random sampling with a reservoir", *Information Processing Letters* 97(5), 2006 — 가중 저수지(서지 기억에 의존 — 확인 권장).
- T. Finch, "Incremental calculation of weighted mean and variance", 2009 — 지수 가중 분산(기억에 의존 — 확인 권장).
- 관련 레슨: NUM-B02 (파국적 상쇄 기초), NUM-B03 (보상 합산), ALGO-B10 (이동 평균·링 버퍼), ALGO-M11 (힙 중앙값·top-K), DSP-B07 (EMA 시정수·−3 dB), C-A09 (고정소수점 Q 형식), C-M14 (ISR 공유 데이터·원자성), DS-B12 (힙), ALGO-B12/B13 (비트 연산)

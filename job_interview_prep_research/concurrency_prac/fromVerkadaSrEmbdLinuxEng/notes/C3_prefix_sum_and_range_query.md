# C3. 누적합 — 구간 합·거리·에너지를 O(1)에

> **이 노트를 읽고 나면**
> - `sum(i..j) = P[j+1] - P[i]`를 인덱스 헷갈림 없이 유도해 쓸 수 있다
> - 링버퍼에서 **절대 누적값**을 저장해 eviction과 무관하게 만드는 법을 설명할 수 있다
> - 불규칙 간격 시계열을 사다리꼴로 적분하고, 양 끝의 부분 조각을 보간으로 처리할 수 있다
>
> **선행**: [C1. 링버퍼](C1_ring_buffer.md), [C2. 이진 탐색](C2_sorted_series_and_binary_search.md)
>
> **이 개념을 쓰는 문제**: [01_gps_fix_cache](../01_gps_fix_cache/question_note) Part 2의
> `gps_distance_travelled(t0, t1)`, [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note)
> Part 2의 `energy_joules_between(t0, t1)`, [02_modem_rssi_window](../02_modem_rssi_window/question_note)의 창 평균.

---

## 1. 왜 이게 필요한가

[C2](C2_sorted_series_and_binary_search.md)로 "그 시각의 값"은 O(log n)에 찾게 됐다. Part 2가 한
단계 더 나가면 이렇게 묻는다.

> `double gps_distance_travelled(uint64_t t0, uint64_t t1);` — t0부터 t1까지 **몇 미터** 움직였나
> `double energy_joules_between(uint64_t t0, uint64_t t1);` — t0부터 t1까지 **몇 줄** 내놨나

값 하나가 아니라 **구간 전체의 합**이다. 순진한 답은 구간을 훑는 것이고, 01번 모범답안이 왜 안
되는지 한 줄로 적었다.

```
 * Distance needs more than the ring.  Summing segments over the range is O(n)
 * -- 6000 fixes per dashboard poll per trailer.
```

트레일러 하나당 폴링마다 6000번, 게다가 `segment_m()`은 `cos`과 `sqrt`를 부른다. **그 동안 mutex를
잡고 있어** 샘플러가 그만큼 GPS를 놓친다.

핵심 관찰: **구간 합은 매번 새로 더할 필요가 없다.** 각 칸이 "처음부터 여기까지의 합"을 들고
있으면 임의 구간의 합은 **뺄셈 한 번**이다. 이것이 prefix sum(누적합)이다.

## 2. 그림으로 먼저

```svg
<svg viewBox="0 0 620 250" role="img" aria-label="막대와 누적선">
  <text class="lbl" x="8" y="16">a = [3, 1, 4, 1, 5, 9, 2, 6]  ·  막대 = 각 값,  꺾은선 = 누적합 P</text><line class="muted" x1="44" y1="200" x2="600" y2="200"/><rect class="box" x="56" y="176" width="42" height="24"/><rect class="box" x="122" y="192" width="42" height="8"/><rect class="fill-soft" x="188" y="168" width="42" height="32"/><rect class="fill-soft" x="254" y="192" width="42" height="8"/><rect class="fill-soft" x="320" y="160" width="42" height="40"/><rect class="fill-soft" x="386" y="128" width="42" height="72"/><rect class="box" x="452" y="184" width="42" height="16"/><rect class="box" x="518" y="152" width="42" height="48"/>
  <text class="lbl" x="77" y="216" text-anchor="middle">a0=3</text><text class="lbl" x="143" y="216" text-anchor="middle">a1=1</text><text class="lbl" x="209" y="216" text-anchor="middle">a2=4</text><text class="lbl" x="275" y="216" text-anchor="middle">a3=1</text><text class="lbl" x="341" y="216" text-anchor="middle">a4=5</text><text class="lbl" x="407" y="216" text-anchor="middle">a5=9</text><text class="lbl" x="473" y="216" text-anchor="middle">a6=2</text><text class="lbl" x="539" y="216" text-anchor="middle">a7=6</text>
  <polyline class="accent" fill="none" points="44,120 77,117 143,116 209,112 275,111 341,106 407,97 473,95 539,89"/><circle class="accent" cx="209" cy="112" r="5"/><circle class="accent" cx="473" cy="95" r="5"/><text class="lbl" x="209" y="102" text-anchor="middle">P[2]=4</text><text class="lbl" x="473" y="85" text-anchor="middle">P[6]=23</text><line class="dash" x1="209" y1="112" x2="473" y2="112"/><line class="dash" x1="473" y1="95" x2="473" y2="112"/><text x="250" y="60">sum(a2..a5) = P[6] - P[2] = 23 - 4 = 19</text><text class="lbl" x="250" y="40">= 누적선의 높이 차이. 막대를 다시 더하지 않는다.</text><text class="lbl" x="250" y="132">이 차이가 답</text>
</svg>
```

간격이 일정하지 않으면 값의 합이 아니라 **면적**을 구해야 한다 — 08번 문제다.

```svg
<svg viewBox="0 0 640 250" role="img" aria-label="사다리꼴 적분과 양 끝의 부분 조각">
  <line class="muted" x1="40" y1="200" x2="620" y2="200"/><polygon class="muted" points="628,200 616,195 616,205"/><text class="lbl" x="606" y="220">t</text><line class="muted" x1="40" y1="200" x2="40" y2="34"/><text class="lbl" x="24" y="44" text-anchor="middle">p</text><polygon class="fill-soft" points="150,200 150,150 280,90 280,200"/><polygon class="fill-soft" points="280,200 280,90 400,90 400,200"/><polygon class="fill-soft" points="400,200 400,90 540,170 540,200"/><line class="accent" x1="150" y1="150" x2="280" y2="90"/><line class="accent" x1="280" y1="90" x2="400" y2="90"/><line class="accent" x1="400" y1="90" x2="540" y2="170"/>
  <circle class="accent" cx="150" cy="150" r="5"/><circle class="accent" cx="280" cy="90" r="5"/><circle class="accent" cx="400" cy="90" r="5"/><circle class="accent" cx="540" cy="170" r="5"/><text class="lbl" x="150" y="216" text-anchor="middle">t_i</text><text class="lbl" x="280" y="216" text-anchor="middle">t_i+1</text><text class="lbl" x="400" y="216" text-anchor="middle">t_i+2</text><text class="lbl" x="540" y="216" text-anchor="middle">t_i+3</text>
  <text class="lbl" x="215" y="176" text-anchor="middle">E_i</text><text class="lbl" x="340" y="176" text-anchor="middle">cum 차이로 한 번에</text><line class="dash" x1="210" y1="34" x2="210" y2="210"/><text class="lbl" x="210" y="28" text-anchor="middle">t0</text><line class="dash" x1="480" y1="34" x2="480" y2="210"/><text class="lbl" x="480" y="28" text-anchor="middle">t1</text><circle class="box" cx="210" cy="122" r="6"/><circle class="box" cx="480" cy="136" r="6"/><text x="250" y="60">질의는 샘플 사이에서 시작하고 끝난다.</text><text class="lbl" x="250" y="80">양 끝의 부분 조각은 전력을 보간해서 따로 계산한다.</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### prefix sum (누적합)과 인덱스 함정

`P[k]` = "처음부터 k개를 더한 값". 정의가 이것 하나다.

```
P[0] = 0 (아무것도 더하지 않음),  P[k] = P[k-1] + a[k-1]  (k = 1 .. n)
   ->  sum(a[i..j]) = P[j+1] - P[i]      (양쪽 포함)
```

**인덱스 함정이 여기다.** 관례가 두 가지 돌아다닌다.

```
관례 A (이 노트):  길이 n+1,  P[0] = 0,    sum(i..j) = P[j+1] - P[i]
관례 B:            길이 n,    P[0] = a[0], sum(i..j) = P[j] - P[i-1]   ← i==0 이면 배열 밖!
```

**관례 A를 쓴다.** `P[0] = 0`이라는 **보초값(sentinel)** 덕에 `i == 0` 특수 처리가 사라진다. 관례 B는
`P[-1]`을 읽어야 해서 `if (i == 0)`이 붙고, 그 `if`를 빼먹는 것이 대표 버그다. 기억법:
**"P[k]는 앞에서 k개"** — 인덱스가 아니라 **개수**로 생각하면 헷갈리지 않는다.

### 절대 누적값 (absolute, never rebased)

링버퍼에서는 오래된 항목이 계속 빠져나간다. 누적값도 다시 계산해야 할까? **아니다.** 각 칸의 누적값을
"**맨 처음 샘플부터 여기까지**"로 두면 eviction은 누적값을 건드리지 않는다 — 읽는 것은 언제나 **두
누적값의 차이**이기 때문이다. 01번 주석: *"`cum` is absolute (never rebased), so evicting the tail
costs nothing -- only differences are ever read."* [C1](C1_ring_buffer.md)의 자유 증가 인덱스와 같은 발상이다.

### trapezoid (사다리꼴)

간격이 일정하지 않은 샘플에서 면적을 구하는 방법. 두 샘플 사이의 변화를 **직선으로 가정**하고
그 아래 면적을 사다리꼴로 계산한다. 08번 모범답안의 유도:

```
 * Between two reports we have no information, so we assume power moves linearly
 * from p_i to p_{i+1}. The integral of a straight line over [t_i, t_{i+1}] is a
 * trapezoid with parallel sides p_i and p_{i+1} and width dt:
 *     E_i = (p_i + p_{i+1}) / 2 * dt          [ watts * seconds = joules ]
```

대안은 **zero-order hold**(계단): `E_i = p_i × dt`. 어느 쪽이 맞는지는 **센서가 무엇을
보고하는지**에 달렸다.

| 모델 | 공식 | 가정 | 언제 맞나 |
| --- | --- | --- | --- |
| zero-order hold | `p_i × dt` | 값이 다음 보고까지 유지 | 센서가 **변할 때만** 보고 (ALS lux, 01번 위치) |
| trapezoid | `(p_i + p_i+1)/2 × dt` | 값이 선형으로 변한다 | 센서가 **연속 신호를 샘플링** (08번 전력) |

08번 주석: "the BMS here reports samples of a continuously varying load, so trapezoid is the
better model and it is second-order accurate." **어느 쪽을 왜 골랐는지 한 문장으로 말하면 된다.**

### 가산성 (additivity)

누적합이 성립하는 유일한 근거다: `E(a, m) + E(m, b) = E(a, b)` (단 `a <= m <= b`).
합·개수·거리·에너지·XOR은 이 성질이 있다. **최댓값·최솟값은 없다**(§5 마지막).

## 4. 코드로 보기

### 가장 작은 예제

```c
static void build_prefix(const int *a, unsigned n, long long *P)
{
    P[0] = 0;                                  /* P[0] 은 "아무것도 더하지 않음" */
    for (unsigned i = 0; i < n; i++)
        P[i + 1] = P[i] + a[i];                /* P[i+1] = a[0..i] 의 합 */
}
static long long range_sum(const long long *P, unsigned i, unsigned j)
{ return P[j + 1] - P[i]; }                    /* a[i..j] 포함-포함 */
```

- `long long P[]` — **합은 원소보다 타입이 커야 한다.** 값 하나가 작아도 6000개를 더하면 다르다.
  그리고 `P[0] = 0` — **이 줄이 없으면?** `i == 0`인 모든 질의가 틀린다. `P`의 길이는 `n+1`이다.

```
a  =  3  1  4  1  5  9  2  6
P  =  0  3  4  8  9 14 23 25 31
sum(2..5) = P[6] - P[2] = 23 - 4 = 19  (brute force 19)
모든 (i,j) 36쌍 검사: 불일치 0 -> PASS
```

### 링버퍼에서 — 절대 누적값

각 칸에 `cum`을 저장하고 append할 때 **직전 칸의 `cum`에 이번 구간 몫만 더한다.** O(1)이다.

```c
typedef struct { uint64_t ts; double seg; double cum; } entry_t;

static void ring_append(ring_t *r, uint64_t ts, double step)
{
    double cum = 0.0, seg = 0.0;
    if (Rn(r) > 0) { seg = step; cum = R(r, Rn(r) - 1u)->cum + step; }
    if (Rn(r) == CAP) r->tail++;               /* 가장 오래된 것을 버린다 */
    entry_t *e = &r->e[r->head & MASK];
    e->ts = ts; e->seg = seg; e->cum = cum; r->head++;
}
/* off 구간 [k0, k1] 의 합 = cum[k1] - cum[k0].  뺄셈 한 번. */
static double ring_range(ring_t *r, uint32_t k0, uint32_t k1)
{ return R(r, k1)->cum - R(r, k0)->cum; }
```

`R(r, off)`는 [C2](C2_sorted_series_and_binary_search.md)의 offset 헬퍼다. **남은 첫 칸의 `cum`은 0이 아니다** — 버려진 몫을 품고 있다. 차이만 읽으니 문제없다. `CAP=8`에 12개를 넣은 뒤:

```
count=8  (ts, seg, cum):
  off=0  ts= 500  seg=  5.0  cum=  14.0
  off=1  ts= 600  seg=  6.0  cum=  20.0
  off=5  ts=1000  seg= 10.0  cum=  54.0        (off=2..4, 6, 7 생략)
  off=7  ts=1200  seg= 12.0  cum=  77.0
range(off 1..5) = 54.0 - 20.0 = 34.0   (brute force 34.0)
```

01번이 이것을 거리로 쓴다 — 각 칸의 `cum`은 "첫 fix부터 여기까지 걸은 미터"다:
*"distance(t0, t1) = cum[last fix <= t1] - cum[first fix >= t0] ... two binary searches and one
subtraction: O(log n)."* **이진 탐색 두 번 + 뺄셈 한 번**이 Part 2의 정답 요약이다.

```c
uint32_t k0 = hist_lower_bound(n, t0);          /* first fix at or after t0 */
uint32_t k1 = hist_upper_bound(n, t1);          /* one past the last fix <= t1 */
if (k1 > k0 + 1u)
    d = hist_at(k1 - 1u)->cum - hist_at(k0)->cum;
```

`k1 > k0 + 1u` 검사가 **없으면?** 구간에 fix가 0개나 1개일 때 `k1 - 1 < k0`이 되어 **음수 거리**가
나온다. 점 하나로는 아무 데도 못 갔으니 0이어야 한다.

### 08번 — 사다리꼴 append

```c
static void hist_push(uint64_t ts, double p)
{
    double cum = 0.0;
    if (H_n > 0) {
        double dt_s = (double)(ts - H_ts[H_n - 1]) / 1e6;         /* 초 단위! */
        cum = H_cum[H_n - 1] + 0.5 * (H_p[H_n - 1] + p) * dt_s;   /* 사다리꼴 */
    }
    H_ts[H_n] = ts; H_p[H_n] = p; H_cum[H_n] = cum; H_n++;
}
```

`/ 1e6`이 **없으면?** 단위가 W·µs가 되어 결과가 **100만 배** 커진다. 제품에서는 "왜 우리
트레일러가 1 GJ를 쓰지?"가 된다. **단위를 주석에 쓴다.**

## 5. 단계별로 만들어 보기

### v0 (느림) → v1 (누적합) → v2 (링 + 절대 누적값)

v0은 §1의 O(n) 루프다. 맞긴 하다. **면접에서는 이걸 먼저 말하고 복잡도를 말한다**: "this is O(n)
per query, 6000 fixes per poll, and I'm holding the mutex the whole time." 그다음 v1(§4의
`build_prefix`) → v2(§4의 `ring_append`). v2에서 초보자가 하는 실수가 **rebase**다 — eviction할 때
남은 `cum`을 전부 다시 계산하면 append가 O(n)이 되어 누적합을 쓴 이유가 없어진다.

### v3: 불규칙 간격 + 양 끝 조각

08번의 진짜 문제다. 질의 `[t0, t1]`은 샘플 위에 떨어지지 않는다. 답은 세 조각이다.

```
    t0                                  t1        head = t0 부터 i+1 까지
     |                                   |               (앞 조각, t0 에서 보간)
  ---+------+---------+---------+--------+---      mid  = cum[j] - cum[i+1]
     i     i+1        …         j       j+1               (통째 구간들, 뺄셈 한 번)
                                                  tail = j 부터 t1 까지
                                                         (뒤 조각, t1 에서 보간)
```

경계에서의 전력은 **선형 보간**으로 구한다.

```c
static double p_at(unsigned i, uint64_t t)     /* 구간 [i, i+1] 안의 t 에서의 전력 */
{
    if (t <= H_ts[i])     return H_p[i];
    if (t >= H_ts[i + 1]) return H_p[i + 1];
    return H_p[i] + (H_p[i+1] - H_p[i]) * ((double)(t - H_ts[i]) / (double)(H_ts[i+1] - H_ts[i]));
}
```

그리고 본체(08번 모범답안과 같은 모양):

```c
unsigned i = floor_index(t0), j = floor_index(t1);
if (i == j)                                  /* 양 끝이 한 구간 안 */
    return 0.5 * (p_at(i, t0) + p_at(i, t1)) * ((double)(t1 - t0) / 1e6);

double head = 0.5 * (p_at(i,t0) + H_p[i+1]) * ((double)(H_ts[i+1] - t0) / 1e6);  /* 앞 조각 */
double mid  = H_cum[j] - H_cum[i + 1];                    /* 통째 구간들 */
double tail = 0.0;                                        /* 뒤 조각 */
if (t1 > H_ts[j] && j + 1u < H_n)
    tail = 0.5 * (H_p[j] + p_at(j, t1)) * ((double)(t1 - H_ts[j]) / 1e6);
return head + mid + tail;
```

**`i == j` 분기가 없으면?** 08번 주석이 "the classic bug"라고 부르는 것이다.

> BOTH ENDS IN ONE SEGMENT... Getting this case wrong (e.g. returning
> cum_j - cum_i = 0) is the classic bug: short queries would silently return zero.

`cum[j] - cum[i+1]`이 음수가 되거나 `cum[j] - cum[i] = 0`이 되어 **짧은 질의가 조용히 0을 반환**한다.
하네스가 짧은 구간을 안 물어보면 통과한다. 그래서 무섭다. `tail`의 `j + 1u < H_n` 검사가 없으면
`t1`이 마지막 샘플과 같을 때 `[j, j+1]`이 없어 배열 밖을 읽는다.

그리고 clamp: `if (t0 < first) t0 = first; if (t1 > last) t1 = last; if (t1 <= t0) return 0.0;`
08번 주석이 이유다: "Energy before the oldest retained sample or after the newest one is unknown,
and inventing it would quietly turn a gap into a number."
**모르는 것을 0으로 채우는 게 아니라 범위를 아는 만큼으로 줄인다.** 돌려 보면 경계가 전부 맞는다.

```
샘플: (t=1.00s, p=100W) (t=1.25s, p=200W) (t=1.30s, p=200W) (t=2.00s, p=50W) (t=2.40s, p=50W)
cum :      0.0     37.5     47.5    135.0    155.0  [J]
  [1.000, 2.400] =   155.000 J   brute   155.000 J   차이 0.00e+00   전체
  [1.100, 1.200] =    16.000 J   brute    16.000 J   차이 0.00e+00   한 구간 안 (양쪽 보간)
  [1.125, 2.000] =   119.375 J   brute   119.375 J   차이 0.00e+00   앞 조각 + 통째 + 정확히 일치
  [1.300, 2.200] =    97.500 J   brute    97.500 J   차이 0.00e+00   샘플에서 시작, 구간 중간에서 끝
  [0.500, 3.000] =   155.000 J   brute   155.000 J   차이 0.00e+00   양쪽 다 창 밖 -> clamp
  [2.400, 2.400] =     0.000 J   brute     0.000 J   차이 0.00e+00   폭 0
  [2.000, 1.000] =     0.000 J   brute     0.000 J   차이 0.00e+00   거꾸로
최대 차이 0.00e+00 J -> PASS
```

손으로 한 칸만 확인한다. 첫 구간 `1.00 ~ 1.25 s`, 전력 `100 → 200 W` → `(100+200)/2 × 0.25 = 37.5 J`
= `cum[1]`. 맞다.

### v4: 부동소수점 누적 오차

`cum`은 **영원히 커지는데** 원하는 차이는 작다. 큰 수에서 큰 수를 빼서 작은 수를 얻는 것 —
**catastrophic cancellation**, 부동소수점이 가장 약한 곳이다. 08번 주석이 숫자로 말한다.

> float has a 24-bit mantissa: once cum passes ~1.7e7 J (about 5 kWh, i.e. a few
> hours on this trailer) the spacing between representable values is larger than
> 1 J and short windows return garbage. Never accumulate energy in a float.
> double has a 53-bit mantissa: at 1e12 J the spacing is still ~2e-4 J.

실제로 재 봤다. 13.7 J씩 300만 번(총 4.1e7 J) 누적하고 마지막 10스텝(= 137 J)을 묻는다.

```
정답(창 10스텝)        = 137.000000 J
double cum 차이        = 137.000000 J   (오차 2.98e-08)
float  cum 차이        = 120.000000 J   (오차 1.70e+01)
float  cum 총합        = 40591040.0  (double 41100000.0, 차이 508960.0)
```

**float은 137 J을 120 J이라고 답한다. 12% 틀렸다.** 총합에서도 50만 J(1.2%)이 사라졌다. `volts`/`amps`가
`float`으로 와도 **`cum`은 반드시 `double`**이다. 부족하면 세 가지 길이 있다.

**(1) Kahan 합.** 매 덧셈에서 잘려나간 양을 두 번째 변수에 담아 다음 덧셈에 되돌린다.

```c
float s = 0.0f, c = 0.0f;                 /* c = 지금까지 잃어버린 몫 */
for (unsigned i = 0; i < n; i++) {
    float y = a[i] - c;                   /* 잃어버린 몫을 먼저 되돌린다 */
    float t = s + y;
    c = (t - s) - y;                      /* 이번 덧셈에서 잘려나간 양 */
    s = t;
}
```

`c = (t - s) - y`가 핵심이다. `t`는 `s + y`를 **반올림한** 값이므로 `(t - s)`가 실제로 더해진 양이고, 거기서 `y`를 빼면 **버려진 양**이 나온다.

`0.1f 를 100만 번:  naive float = 100958.3438   Kahan float = 100000.0000   double = 100000.0015`

naive float은 958이나(약 1%) 틀렸는데 Kahan은 float만으로 맞췄다. **`-ffast-math`를 켜면 컴파일러가
`(t - s) - y`를 0으로 "최적화"해 Kahan이 무력화된다.**

**(2) rebase.** 링이 감길 때 모든 `cum`에서 `cum[head]`를 빼 누적값이 무한정 커지지 않게 한다 —
eviction이 O(n)이 되는 대가를 낸다.

**(3) 정수로 다룬다.** 밀리 단위 정수로 바꾸면 **오차가 정확히 0**이다.
`long long mj = 0; ... mj += 13700;`(13.7 J = 13700 mJ) → 실측 `41100000000 mJ = 41100000.000 J`.
`int64_t`는 9.2e18까지 담으니 밀리줄이면 안 넘친다. **대신 사다리꼴의 `/2`와 `dt` 곱셈에서 반올림
정책을 직접 정해야 한다.** FPU 없는 MCU면 이쪽이 정답, 리눅스 유저 공간이면 `double`로 충분하다.

### v5: 검증 — brute force와 가산성

**(1) brute force 비교.** 참조는 "각 구간을 `[t0, t1]`로 자르고 하나씩 적분"하는 O(n) 루프 — 누적합도
조각 분기도 쓰지 않으니 **독립 구현**이다.

```c
for (unsigned k = 0; k + 1u < H_n; k++) {
    uint64_t s = H_ts[k] > t0 ? H_ts[k] : t0;          /* 구간을 질의로 자른다 */
    uint64_t f = H_ts[k+1] < t1 ? H_ts[k+1] : t1;
    if (f > s) e += 0.5 * (p_at(k, s) + p_at(k, f)) * ((double)(f - s) / 1e6);
}
```

**(2) 가산성 검사.** 참조 구현조차 없을 때 쓰는 자기 검사다. `E(a,m) + E(m,b) = E(a,b)`가 모든 분할점
`m`에서 성립해야 한다. **양 끝 조각 처리가 틀리면 바로 깨진다** — `m`이 샘플 사이에 떨어질 때 두
조각의 보간이 서로 안 맞기 때문이다. 실측 `21개 분할점 검사: 불일치 0 -> PASS`.

면접에서 "how would you test this?"의 답: "a brute-force O(n) reference over random inputs, and an
additivity check E(a,m) + E(m,b) == E(a,b) for many split points — that one catches the
edge-sliver bugs specifically."

### 언제 누적합이 안 되는가

전제는 **가산성** — 역연산이 있어야 뺄 수 있다.

| 집계 | 누적합이 되나 | 왜 |
| --- | --- | --- |
| 합, 개수, 평균(합/개수) | 된다 | 덧셈에 역연산(뺄셈)이 있다 |
| 거리, 에너지, 적분 | 된다 | 구간별 몫의 합이다 |
| **최댓값 / 최솟값** | **안 된다** | `max`에는 역연산이 없다 |
| 중간값, 상위 k개 | 안 된다 | 순서 통계량은 분해되지 않는다 |

`a = [5, 9, 2]`에서 `M[2] = max(5,9) = 9`, `M[3] = 9`. 둘이 같으니 그 차이에서 `max(a[2..2])`를
복원할 방법이 없다 — **9가 지나갔다는 사실이 정보를 덮어썼다.** 그래서 "창 안의 최대
RSSI"([02_modem_rssi_window](../02_modem_rssi_window/question_note))는 전혀 다른 자료구조가 필요하다:
[C4. monotonic deque](C4_sliding_window_and_monotonic_deque.md),
[C5. 시간 버킷](C5_time_buckets_and_histogram.md). **"합이니까 prefix sum, 최대니까 deque"를 즉시
갈라 말하는 것이 이 표의 용도다.**

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `P[0] = a[0]`으로 시작 (관례 B) | `i == 0`인 질의만 틀린다. 또는 `P[-1]`로 배열 밖 읽기 | 보초값이 없어서 "앞에서 0개"를 표현할 수 없다 | `P[0] = 0`, 길이 `n+1`, `sum(i..j) = P[j+1] - P[i]` |
| eviction할 때 `cum`을 rebase | 정답은 맞는데 append가 O(n)이 되어 링버퍼를 쓴 이유가 사라진다 | 누적값을 상대값으로 오해한 것 | 절대 누적값으로 두고 건드리지 않는다. 차이만 읽는다 |
| `i == j`(양 끝이 한 구간 안) 분기 누락 | **짧은 질의가 조용히 0을 반환.** 긴 질의는 다 맞는다 | 통째로 들어간 구간이 0개라 뺄 누적합이 없다 | 보간한 두 전력으로 사다리꼴 하나를 직접 계산 |
| `cum`을 `float`으로 누적 | 몇 시간 뒤부터 짧은 창이 12%씩 틀린다 | 24비트 mantissa로는 1.7e7을 넘으면 간격이 1 J보다 커진다 | `double`. 모자라면 Kahan 또는 정수 밀리 단위 |
| 구간에 샘플이 0~1개일 때 검사 누락 | **음수 거리**나 음수 에너지가 나온다 | `k1 - 1 < k0`이면 뺄셈 순서가 뒤집힌다 | `if (k1 > k0 + 1u)` 같은 하한 검사 |
| max/min에 누적합을 시도 | 값이 말이 안 되는데 이유를 못 찾는다 | `max`에는 역연산이 없다 | monotonic deque 또는 버킷 집계 (C4/C5) |

## 7. 손으로 확인하기

01번의 거리 계산부터 본다. 하네스가 ground truth를 따로 계산해 비교한다.

```sh
cd 01_gps_fix_cache && ./main.sh sol
grep -n -A 12 "gps_distance_travelled" gps_cache_solution.c
```

`if (k1 > k0 + 1u)`를 `if (k1 > k0)`로 바꾸면 **음수 거리**가 나온다. 08번은 양 끝 조각이 핵심이니
`cd 08_battery_energy_pipeline && ./main.sh sol` 로 돌려 보고 세 가지를 순서대로 깨 본다.

- `i == j` 분기를 지운다 → 짧은 구간 질의만 FAIL (0을 반환)
- `/ 1e6`을 지운다 → 전부 FAIL, 값이 100만 배
- `cum`을 `float`으로 바꾼다 → 30분 창에서는 **통과할 수도 있다.** 통과한다는 사실이 무섭다

stub(`energy_store.c`)을 채울 때는 본체보다 **brute force를 먼저 쓴다.** 느린 버전으로 하네스를
통과시키고 나서 누적합으로 바꾸면 두 구현을 비교할 수 있어 디버깅이 훨씬 빠르다.

이 노트의 모든 조각은 한 파일로 붙여 실제로 돌린 것이다: `cc -std=c11 -O2 -Wall -Wextra c3.c -lm && ./a.out`.

## 8. 자가 점검

```check
Q: `sum(i..j) = P[j+1] - P[i]`를 유도하고, P[0] = 0을 두는 이유를 말해 보라.
A: P[k]를 "앞에서 k개의 합"으로 정의하면 a[i..j]는 앞에서 j+1개에서 앞에서 i개를 뺀 것이다 —
   인덱스가 아니라 개수로 생각하면 헷갈리지 않는다. P[0] = 0은 보초값이라 i == 0도 그냥 된다.
   P[0] = a[0] 관례는 i == 0에서 P[-1]을 읽어야 해서 if가 붙고, 그걸 빼먹는 것이 대표 버그다.

Q: 링버퍼에서 eviction이 일어나는데도 누적합이 망가지지 않는 이유는?
A: cum을 "맨 처음부터 여기까지"의 절대값으로 저장하고 rebase하지 않기 때문이다. 질의는 두 cum의
   차이만 읽으므로 무엇이 빠져나갔든 상관없다. rebase하면 append가 O(n)이 되어 링을 쓴 이유가 없다.

Q: 사다리꼴 공식을 유도하고, zero-order hold와 언제 갈라 써야 하는지 말해 보라.
A: 두 보고 사이에는 정보가 없으므로 전력이 선형으로 변한다고 가정한다. 직선 아래 면적은 평행변이
   p_i, p_i+1이고 폭이 dt인 사다리꼴이므로 E = (p_i + p_i+1)/2 × dt, dt는 초 단위. 센서가
   "변할 때만" 보고하면(ALS lux, GPS 위치) ZOH(p_i × dt)가 맞고, 연속 신호를 샘플링하면
   사다리꼴이 맞다(2차 vs 1차 정확도).

Q: 질의 양 끝이 같은 구간에 떨어질 때(i == j) 왜 따로 처리해야 하는가? 안 하면?
A: 통째로 들어간 구간이 0개라서 뺄 누적합이 없다. cum[j] - cum[i]는 0이고 cum[j] - cum[i+1]은 음수다.
   그래서 짧은 질의가 조용히 0을 반환한다 — 긴 질의는 다 맞으니 하네스가 짧은 구간을 안 물어보면
   통과한다. 답은 t0과 t1에서 보간한 두 전력으로 만드는 사다리꼴 하나다("the classic bug").

Q: 왜 cum을 double로 잡아야 하는가? 숫자로 설명해 보라. 대안 두 가지는?
A: cum은 영원히 커지는데 원하는 차이는 작다 — catastrophic cancellation이다. float은 24비트
   mantissa라서 cum이 ~1.7e7 J을 넘으면 표현 간격이 1 J보다 커진다. 실측으로 137 J이 답인 질의에
   float 누적은 120 J(12% 오차)을 반환했다. 대안은 Kahan 보상 합과 정수 밀리 단위(오차 0).

Q: 최댓값·최솟값에는 누적합을 쓸 수 없는 이유는? 그럼 무엇을 쓰나?
A: 전제는 가산성 — 역연산이 있어서 뺄 수 있다는 것이다. max에는 역연산이 없다. a = [5, 9, 2]에서
   M[2] = 9, M[3] = 9로 같아지므로 그 차이에서 a[2]를 복원할 방법이 없다. 창 안의 max/min은
   monotonic deque(C4)나 버킷 집계(C5)로 푼다 — 02번의 RSSI 창이 그 문제다.

Q: 직접 쓴 구간 적분을 어떻게 검증하나? 두 가지 방법과 각각이 잡는 버그는?
A: (1) brute force 비교 — 각 구간을 질의로 자르고 하나씩 적분하는 O(n) 루프를 참조로 쓴다. 누적합도
   조각 분기도 쓰지 않으니 독립 구현이고 인덱스·누적합 실수를 잡는다. (2) 가산성 검사 — 모든
   분할점 m에서 E(a,m) + E(m,b) == E(a,b)인지 본다. 양 끝 조각의 보간 버그를 특히 잘 잡는다.
```

## 9. 요약 카드

- 누적합 = **각 칸이 "처음부터 여기까지의 합"을 든다.** 임의 구간은 **뺄셈 한 번**. 관례는 하나만:
  **`P[0] = 0`, 길이 `n+1`, `sum(i..j) = P[j+1] - P[i]`.** 합 타입은 원소보다 크게(`int` → `long long`).
- 링버퍼에서는 **절대 누적값**을 저장하고 **rebase하지 않는다.** 차이만 읽으니 eviction과 무관.
- 01번 Part 2 = **이진 탐색 두 번 + 뺄셈 한 번.** 구간에 점이 2개 미만이면 0.
- 불규칙 간격은 **사다리꼴** `(p_i + p_i+1)/2 × dt`, **`dt`는 초**. 변할 때만 보고하는 센서면 ZOH.
- 양 끝 조각 = **앞 조각(보간) + 통째(cum 차이) + 뒤 조각(보간)**. **`i == j` 분기를 빼먹으면 짧은
  질의가 조용히 0.** 모르는 구간은 **clamp** — 0으로 채우면 구멍이 숫자로 변한다.
- `cum`은 **`double`.** float은 1.7e7 J을 넘으면 12%씩 틀린다. 더 필요하면 Kahan 또는 정수 밀리 단위.
- 검증은 **brute force + 가산성 `E(a,m)+E(m,b)=E(a,b)`.** **max/min은 누적이 안 된다**(역연산이
  없다) → [C4](C4_sliding_window_and_monotonic_deque.md) / [C5](C5_time_buckets_and_histogram.md).

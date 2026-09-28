# C2. 정렬된 시계열과 이진 탐색 — "그 시각의 값"을 O(log n)에 찾기

> **이 노트를 읽고 나면**
> - `lower_bound`와 `upper_bound`를 외우지 않고 **불변식으로 유도**해 쓸 수 있다
> - "t 이하 중 마지막" = `upper_bound(t) - 1`이라는 관용구를 왜 그렇게 쓰는지 설명할 수 있다
> - 링버퍼 위에서 wrap을 신경 쓰지 않고 이진 탐색하고, 경계 조건 5가지를 빠짐없이 처리할 수 있다
>
> **선행**: [C1. 링버퍼](C1_ring_buffer.md)
>
> **이 개념을 쓰는 문제**: 원본 ALS 문제의 `get_lux_at(t)`,
> [01_gps_fix_cache](../01_gps_fix_cache/question_note) Part 2의 `gps_fix_at()`과
> `gps_distance_travelled()`, [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note)
> Part 2의 replay 질의, [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note)
> Part 2의 `floor_index()`.

---

## 1. 왜 이게 필요한가

원본 문제의 Part 2를 다시 읽는다.

> `float get_lux_at(uint64_t timestamp);`
> 시각 t의 값 = `timestamp <= t`인 가장 최근 VALID 샘플의 lux.

[C1](C1_ring_buffer.md)에서 저장소는 정했다. 링버퍼다. 질의는? 처음 떠오르는 답은 **뒤에서부터 훑기**다.

```c
/* O(n) — 이것도 답이다. 면접에서는 먼저 이걸 말하고 나서 개선한다 */
for (uint32_t i = n; i-- > 0; )
    if (at(i)->ts <= t) return at(i)->lux;
return -1;
```

틀린 건 아니다. 문제는 숫자다. 01번 지문이 그 숫자를 준다.

> Should be O(log n), not O(n): the dashboard polls it once a second per
> trailer and there can be 6000 fixes in the window.

트레일러 하나에 6000개, 초당 한 번, 수백 대. O(n)이면 게이트웨이 CPU가 링버퍼 훑는 데만 쓰이고,
**그 동안 mutex를 잡고 있어** 샘플러가 그만큼 기다린다. O(log n)이면 13번만 비교한다 — 462배다.
가능한 **유일한 이유**는 배열이 정렬돼 있기 때문이다(§3). 이 노트는 그 이진 탐색을 **외우지 않고
유도하는 법**이다.

## 2. 그림으로 먼저

센서는 **값이 변할 때만** 보고하므로 두 샘플 사이에서 값은 **유지된다**. 그래서 그래프가 계단이다
(step function). "시각 t의 값"의 정의가 이 그림이다.

```svg
<svg viewBox="0 0 620 220" role="img" aria-label="시계열은 계단 함수다">
  <line class="muted" x1="50" y1="185" x2="600" y2="185"/><polygon class="muted" points="608,185 596,180 596,190"/><text class="lbl" x="588" y="205">시각 t</text><line class="muted" x1="50" y1="185" x2="50" y2="25"/><polygon class="muted" points="50,17 45,29 55,29"/><text class="lbl" x="34" y="34" text-anchor="middle">값</text><line class="accent" x1="90" y1="140" x2="200" y2="140"/><line class="dash" x1="200" y1="140" x2="200" y2="70"/><line class="accent" x1="200" y1="70" x2="330" y2="70"/><line class="dash" x1="330" y1="70" x2="330" y2="115"/><line class="accent" x1="330" y1="115" x2="470" y2="115"/><line class="dash" x1="470" y1="115" x2="470" y2="155"/>
  <line class="accent" x1="470" y1="155" x2="570" y2="155"/><circle class="accent" cx="90" cy="140" r="5"/><circle class="accent" cx="200" cy="70" r="5"/><circle class="accent" cx="330" cy="115" r="5"/><circle class="accent" cx="470" cy="155" r="5"/><text class="lbl" x="90" y="203" text-anchor="middle">t0</text><text class="lbl" x="200" y="203" text-anchor="middle">t1</text><text class="lbl" x="330" y="203" text-anchor="middle">t2</text><text class="lbl" x="470" y="203" text-anchor="middle">t3</text><line class="dash" x1="390" y1="185" x2="390" y2="105"/><circle class="box" cx="390" cy="115" r="7"/><text class="lbl" x="390" y="203" text-anchor="middle">t</text>
  <text x="410" y="96">여기서 물으면 답은 t2 의 값.</text><text class="lbl" x="410" y="76">"t 이하 중 마지막 샘플" 하나만 찾으면 된다.</text>
</svg>
```

이진 탐색은 **답이 들어 있는 구간 `[lo, hi)`를 절반씩 줄이는** 일이다. 구간이 비면 끝난다.

```svg
<svg viewBox="0 0 640 250" role="img" aria-label="이진 탐색이 구간을 절반씩 줄인다">
  <text class="lbl" x="8" y="16">a = [100, 200, 200, 350, 500],  upper_bound(300) 을 찾는다</text><rect class="fill-soft" x="60" y="28" width="500" height="34" rx="5"/><text class="lbl" x="310" y="50" text-anchor="middle">lo=0, hi=5  ·  mid=2, a[2]=200 &lt;= 300 → 답은 오른쪽</text><line class="accent" x1="310" y1="64" x2="310" y2="82"/><polygon class="accent" points="310,88 305,76 315,76"/><rect class="box" x="60" y="92" width="240" height="34" rx="5"/><rect class="fill-soft" x="308" y="92" width="252" height="34" rx="5"/><text class="lbl" x="180" y="114" text-anchor="middle">버린다</text><text class="lbl" x="434" y="114" text-anchor="middle">lo=3, hi=5 · mid=4, a[4]=500 &gt; 300</text>
  <line class="accent" x1="380" y1="128" x2="380" y2="146"/><polygon class="accent" points="380,152 375,140 385,140"/><rect class="fill-soft" x="308" y="156" width="130" height="34" rx="5"/><rect class="box" x="446" y="156" width="114" height="34" rx="5"/><text class="lbl" x="373" y="178" text-anchor="middle">lo=3, hi=4 · mid=3, a[3]=350 &gt; 300</text><text class="lbl" x="503" y="178" text-anchor="middle">버린다</text><line class="accent" x1="340" y1="192" x2="340" y2="210"/><polygon class="accent" points="340,216 335,204 345,204"/><rect class="fill-soft" x="308" y="218" width="64" height="26" rx="5"/><text class="lbl" x="340" y="236" text-anchor="middle">lo=hi=3</text>
  <text x="392" y="238">구간이 비었다 → 답은 3</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### 시계열은 왜 공짜로 정렬돼 있나

배열에 넣는 것은 **도착 순서대로의 샘플**이다. 그러니 `ts[0] <= ts[1] <= ... <= ts[n-1]`이 자동으로
성립한다. **정렬 함수를 부를 일이 없다** — 이게 이진 탐색을 쓸 수 있는 근거 전부다.

"비감소"(non-decreasing)라고 쓴 것에 주의한다. 같은 타임스탬프가 연속으로 올 수 있다(시계
해상도가 거칠 때). 그래서 **중복을 허용하는 탐색**이 필요하고, 그게 두 함수를 구분하는 이유다.

### 정렬이 깨지는 경우 — 시계 역행

`gps.h`가 경고한다: `gps_now_us()`는 `gettimeofday`이고 **"Not monotonic -- NTP on the trailer's
LTE modem can step it."** 모뎀이 NTP로 시각을 맞추는 순간 시계가 **뒤로 점프**할 수 있다. 그러면 새 샘플의 타임스탬프가
직전보다 작아져 배열이 정렬을 잃고, 이진 탐색은 그 뒤 **모든 질의에서** 엉뚱한 답을 준다.
크래시도 안 나고 하네스도 종종 통과한다. 최악의 종류의 버그다. 대처는 두 가지다.

**(1) clamp — 직전 값으로 끌어올린다** (01번, `gps_cache_solution.c`):

```c
/* Clamp so the array stays non-decreasing rather than corrupting every
 * future lookup. */
uint64_t last = g_hist.buf[(g_hist.head - 1u) & HIST_MASK].fix.timestamp;
if (f.timestamp < last) f.timestamp = last;
```

위치 데이터는 하나도 아까우니 **시각을 조금 왜곡하고 샘플을 살린다.**
**(2) drop — 그 샘플을 버린다** (08번, `energy_store_solution.c`):
`if (hist_count > 0 && s->timestamp <= H(hist_count - 1)->ts) return;`

에너지 적분은 `dt`로 나누므로 **시각이 왜곡되면 적분값이 틀어진다.** 그래서 버리는 쪽이 맞다.
같은 문제에 다른 답 — **어느 쪽을 골랐는지 이유를 말할 수 있으면 된다.** 면접에서 한 줄:
"With a monotonic clock this branch never fires."

### 불변식 (invariant) — 이진 탐색을 외우지 않는 법

매번 헷갈리는 이유는 코드를 외우기 때문이다. 대신 **구간의 뜻**을 정한다 — `[lo, hi)`(lo 포함, hi 제외).

```
불변식:  인덱스 < lo 인 원소는 전부 "조건 불만족"이 확정됐다
        인덱스 >= hi 인 원소는 전부 "조건 만족"이 확정됐다
        답은 항상 [lo, hi] 안에 있다
```

"조건"은 우리가 찾는 경계다(`lower_bound`면 `a[i] >= t`, `upper_bound`면 `a[i] > t`). 그러면
루프 본문이 기계적으로 나온다.

- `mid`가 조건을 **만족** → `mid`도 답일 수 있다 → `hi = mid`
- `mid`가 조건을 **불만족** → `mid`는 답이 아니다 → `lo = mid + 1`

`lo == hi`면 구간이 비었고 그 값이 답이다. **종료 조건은 `lo < hi`이고, 이 형태에서는 무한
루프가 구조적으로 불가능하다** — `hi = mid`는 `hi`를, `lo = mid + 1`은 `lo`를 최소 1씩 움직인다.

### lower_bound / upper_bound

| 이름 | 정의 | 다른 말로 | 없을 때 |
| --- | --- | --- | --- |
| `lower_bound(t)` | `a[i] >= t`인 **첫** i | `t` **미만**인 원소의 개수 | `n` |
| `upper_bound(t)` | `a[i] > t`인 **첫** i | `t` **이하**인 원소의 개수 | `n` |

세 번째 열이 실전 해석이다. `upper_bound(t)`가 **`t` 이하인 개수**라는 사실에서 관용구가 나온다.

```
"t 이하 중 마지막 원소의 인덱스" = (t 이하인 개수) - 1 = upper_bound(t) - 1
   ... 단 개수가 0이면 그런 원소가 없다 (첫 샘플 이전)
```

`lower_bound(t) - 1`이 아니다. `t`와 같은 값이 있으면 `lower_bound`는 그 값을, `upper_bound`는
그 **다음**을 가리킨다. 중복이 있으면 차이가 더 벌어진다.

```
a = [100, 200, 200, 350, 500],  t = 200   ->   lower_bound = 1,  upper_bound = 3
   upper_bound-1 = 2 -> a[2]=200 (맞다)      lower_bound-1 = 0 -> a[0]=100 (틀렸다)
```

**외울 것 하나**: "이하 중 마지막"에는 `upper_bound`, "이상 중 처음"에는 `lower_bound`. 01번
모범답안의 주석이 정확히 그 문장이다 — `"number of offsets with timestamp <= t; minus one is
the last such"`.

## 4. 코드로 보기

### 한 글자만 다른 두 함수

```c
/* a[i] >= t 인 첫 i. 없으면 n. */
static unsigned lower_bound(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n;                 /* 답은 항상 [lo, hi] 안에 있다 */
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;  /* lo <= mid < hi */
        if (a[mid] < t) lo = mid + 1u;       /* mid 는 답이 아니다 */
        else            hi = mid;            /* mid 는 답일 수 있다 */
    }
    return lo;
}

/* a[i] > t 인 첫 i. 곧 "t 이하인 원소의 개수". 없으면 n. */
static unsigned upper_bound(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        if (a[mid] <= t) lo = mid + 1u;      /* 위와 완전히 같고, `<` 가 `<=` 로 바뀐 것뿐 */
        else             hi = mid;
    }
    return lo;
}
```

한 줄씩 왜 그런지.

- `hi = n` — `n - 1`이 아니다. `hi = n`이 "아직 아무것도 배제하지 않았다"는 뜻이다. `n - 1`로 쓰면
  **마지막 원소가 답인 경우를 못 찾고**, `n = 0`이 `lo = hi = 0`으로 자동 처리되지도 않는다.
- `mid = lo + (hi - lo) / 2u` — `(lo + hi) / 2`는 큰 배열에서 **덧셈이 오버플로**한다(고전적 JDK 버그).
- `while (lo < hi)` — `lo <= hi`로 쓰면 `hi = mid`와 조합될 때 `lo == hi == mid`에서 **무한 루프**다.
- `if (a[mid] < t)` vs `<=` — **이 한 글자가 lower와 upper를 가른다.** lower의 조건은 `>= t`이므로
  불만족이 `< t`, upper의 조건은 `> t`이므로 불만족이 `<= t`.

### 관용구 — "t 이하 중 마지막"

```c
static int last_le(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned k = upper_bound(a, n, t);
    return (k == 0u) ? -1 : (int)(k - 1u);   /* k == 0 이면 t 이하가 하나도 없다 */
}
```

`k == 0` 검사가 **없으면?** `k - 1u`가 unsigned라 `0 - 1 = 4294967295`가 되어 배열 밖을 읽는다.
크래시가 나면 운이 좋은 것이고, 보통은 쓰레기 값을 반환한다. 01번도 같은 검사를 한다:
`if (k > 0) { *out = hist_at(k - 1u)->fix; rc = 0; }  /* k == 0: t precedes every fix */`

### 링버퍼 위에서 — offset으로 다루는 트릭

링버퍼의 데이터는 배열 안에서 **감겨 있다**(`buf[5], buf[6], buf[7], buf[0], buf[1]`). 그냥 이진
탐색을 돌리면 틀린다. 순진한 해법("감긴 지점을 찾고 두 구간을 따로 탐색")은 코드와 off-by-one이
둘 다 두 배가 된다. **훨씬 쉬운 길**: 인덱스를 **`tail` 기준 offset**으로만 다루고 실제 배열
접근에서만 마스크를 씌운다. 그러면 탐색 코드는 **wrap의 존재를 모른다.**

```c
/* off 는 tail 기준 오프셋. head/tail 이 uint32 를 넘겨 감싸도 탐색은 영향이 없다. */
static uint64_t ring_at(const ring_t *r, uint32_t off) { return r->ts[(r->tail + off) & MASK]; }

static uint32_t ring_upper_bound(const ring_t *r, uint32_t n, uint64_t t)
{                                   /* 본문은 위의 upper_bound 와 글자 그대로 같다. */
    uint32_t lo = 0, hi = n;        /* a[mid] 대신 ring_at(r, mid) 를 쓸 뿐이다. */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (ring_at(r, mid) <= t) lo = mid + 1u; else hi = mid;
    }
    return lo;
}
```

01번 모범답안이 그 트릭에 주석을 붙여 놓았다.

```c
/* Offsets from tail, not raw indices, so the uint32 wrap of head/tail is
 * invisible to the search. */
static const hist_entry_t *hist_at(uint32_t off)
{ return &g_hist.buf[(g_hist.tail + off) & HIST_MASK]; }
```

**이 한 줄짜리 함수가 가장 실전적인 부분이다.** `hist_at(off)` 하나를 정의하면 그 위의 모든
코드(`lower_bound`, `upper_bound`, [C3](C3_prefix_sum_and_range_query.md)의 누적합 뺄셈)가
평범한 배열 코드가 된다. 실제로 감긴 상태에서 돌려 보면:

```
head=12 tail=4 count=8  내용: 500 600 700 800 900 1000 1100 1200
t= 400 -> 창보다 이전 (답 없음)
t= 650 -> off=1, 값 600
t= 900 -> off=4, 값 900
t=1150 -> off=6, 값 1100
```

### 08번의 변형 — 닫힌 구간 floor_index

08번은 `[lo, hi]` **닫힌 구간**에 `lo`가 답 후보를 들고 있는 형태를 쓴다:
`mid = lo + (hi - lo + 1u) / 2u;  if (H(mid)->ts <= t) lo = mid; else hi = mid - 1u;`
여기서 `mid` 계산의 `+ 1u`("bias up")가 **반드시** 있어야 한다. 없으면 `hi = lo + 1`일 때
`mid == lo`가 되고 `lo = mid`가 아무것도 바꾸지 않아 **무한 루프**다. **추천**: 면접에서는
`upper_bound(t) - 1`을 쓴다. 반열린 구간은 무한 루프가 구조적으로 불가능하다.

## 5. 단계별로 만들어 보기

### v0 (틀림): "정확히 일치"만 찾는다

코딩 테스트에서 처음 배우는 이진 탐색은 값이 **있는지** 찾는 것이다.

```c
static int find_exact(const uint64_t *a, unsigned n, uint64_t t)
{
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        if (a[mid] == t) return (int)mid;
        if (a[mid] < t)  lo = mid + 1u; else hi = mid;
    }
    return -1;                 /* 없음 */
}
```

시계열에서 이건 **거의 항상 -1을 반환한다.** 묻는 시각은 샘플이 찍힌 정확한 마이크로초가 아니다.

```
find_exact(200) = 2   (있으니 찾는다)
find_exact(300) = -1   (없다고 답한다 — 시계열에서는 이게 버그다)
```

문제 정의가 "정확히 t"가 아니라 "**t 이하 중 마지막**"이라는 것을 놓친 것이다. `a[mid] == t` 분기는
**불필요하기도 하다** — 지우면 그대로 `lower_bound`가 된다.

### v1: upper_bound - 1

```
t      lower_bound   upper_bound   last_le   그 시각의 값
50     0             0             -1        없음 (첫 샘플 이전)
100    0             1             0         a[0] = 100
150    1             1             0         a[0] = 100
200    1             3             2         a[2] = 200
300    3             3             2         a[2] = 200
500    4             5             4         a[4] = 500
900    5             5             4         a[4] = 500
```

이 표를 한 줄씩 확인하는 것이 핵심 연습이다. `t = 200`에서 두 함수가 갈리고, 샘플이 없는 시각
(`150`, `300`)에서는 같다. 그리고 **`last_le`는 언제나 맞다.**

### v2: off-by-one을 안 내는 법 — 손으로 추적하기

새 이진 탐색을 믿기 전에 **원소 5개짜리 배열로 손으로 한 번 돌린다.** 열은 항상 여섯 개다:
`lo` / `hi` / `mid` / `a[mid]` / 조건 / 다음 동작. `a = [100, 200, 200, 350, 500]`:

| step | lo | hi | mid | a[mid] | upper: a[mid] <= 300 ? | 다음 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0 | 5 | 2 | 200 | yes | lo = mid+1 → 3 |
| 2 | 3 | 5 | 4 | 500 | no | hi = mid → 4 |
| 3 | 3 | 4 | 3 | 350 | no | hi = mid → 3 |
| 끝 | 3 | 3 | — | — | — | upper_bound(300) = 3 |

`upper_bound(300) = 3` → `last_le = 2` → 값은 `a[2] = 200`. §2의 계단 그림과 일치한다.
같은 표를 `t = 200`으로 두 함수에 대해 그려 보면(추적 출력으로 확인했다) lower는
`hi=2 → hi=1 → lo=1`로 **1**에서 끝나고 upper는 `lo=3 → hi=4 → hi=3`으로 **3**에서 끝난다 —
중복 `200`이 두 개 있으니 그 사이가 벌어진다. 면접 중에도 이걸 한다: "let me trace this with
five elements"라고 말하고 표를 그린다. 면접관이 보고 싶은 게 정확히 그 습관이다.

### v3: 경계 조건 표 — 다섯 가지를 빼먹지 않는다

이진 탐색이 맞아도 **그 위의 정책**에서 떨어진다. 하네스가 보는 것이 이 다섯 개다.

| 경계 | 판정 위치 | 01번의 처리 | 왜 |
| --- | --- | --- | --- |
| t가 **미래** | 탐색 전 | `if (t > now) return -1;` | 우리는 미래를 모른다. 탐색하면 마지막 샘플을 잘못 반환한다 |
| t가 **창보다 오래됨** | 탐색 전 | `if (now - t > GPS_WINDOW_US) return -1;` | 그 구간은 보관을 약속하지 않았다. 남아 있어도 계약을 지킨다 |
| t가 **첫 샘플 이전** | 탐색 후 | `if (k > 0)`, `k == 0`이면 -1 | `upper_bound`가 0이면 t 이하가 없다. `k-1`은 unsigned 언더플로 |
| t가 **정확히 일치** | 탐색이 처리 | `upper_bound`가 그 샘플을 포함해 센다 | `lower_bound`였다면 하나 앞을 가리켜 틀린다 |
| **빈 상태** (0개) | 자동 | `n=0` → `lo=hi=0` → `k=0` → -1 | `hi = n`으로 시작한 덕에 특수 처리가 필요 없다 |

**순서에 주의한다.** `t > now`를 먼저 걸렀기 때문에 `now - t`가 언더플로하지 않는다. 두 줄의
순서를 바꾸면 미래 시각에서 `now - t`가 1800경이 되어 두 번째 조건이 항상 참이 된다.

### v4: brute force와 비교하는 테스트

손 추적은 한두 케이스뿐이다. 진짜 확신은 **참조 구현과의 비교**에서 온다. 참조는 느려도 되니
**정의를 그대로 옮긴 O(n) 루프**로 쓴다 — 틀릴 여지가 없다.

```c
static unsigned bf_lower(const uint64_t *a, unsigned n, uint64_t t)
{
    for (unsigned i = 0; i < n; i++) if (a[i] >= t) return i;
    return n;
}
/* bf_upper 는 `>=` 를 `>` 로만 바꾼 쌍둥이다. */
```

그리고 무작위 입력을 던진다. **입력 생성 방식이 핵심이다.**

```c
unsigned m = xr() % 17u;                       /* 0..16, 빈 배열과 1개도 포함 */
uint64_t cur = xr() % 5u;
for (unsigned i = 0; i < m; i++) { cur += xr() % 4u; arr[i] = cur; }  /* 증가량 0 허용 */
uint64_t t = xr() % 64u;                       /* 값 범위보다 넓게 */
```

- `m`에 **0과 1을 반드시 포함**시킨다. off-by-one은 거기서 나온다.
- 증가량이 0일 수 있게 해 **중복 값을 만든다.** 중복 없는 데이터로는 lower/upper 혼동을 못 잡는다.
- `t`를 값 범위보다 **넓게** 뽑아 모든 원소보다 작은/큰 t가 나오게 한다.
- `rand()`가 아니라 **고정 시드 xorshift**. 실패했을 때 같은 수열로 재현된다.

실제로 돌린 결과: `400000 회 비교, 불일치 0 건 -> PASS`. **15줄이다.** 이 방법으로 한 번 검증해
본 사람은 그 뒤로 이진 탐색을 무서워하지 않는다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `hi = n - 1`로 시작 | **마지막 원소가 답인 질의만** 틀린다. `n == 0`에서는 `hi`가 언더플로해 폭발 | `[lo, hi)`에서 `hi`는 "제외되는 끝"이다 | `hi = n`. 빈 배열도 자동 처리된다 |
| `while (lo <= hi)` + `hi = mid` | 무한 루프. 프로그램이 응답을 멈춘다 | `lo == hi == mid`에서 `hi = mid`가 아무것도 줄이지 않는다 | `while (lo < hi)`. 반열린 구간과 짝을 맞춘다 |
| `lower_bound - 1`로 "이하 중 마지막" | t와 **정확히 같은 샘플이 있을 때만** 하나 앞을 반환. 중복이면 더 벌어진다 | `lower_bound`는 t를, `upper_bound`는 t 다음을 가리킨다 | `upper_bound(t) - 1` |
| `k - 1`을 `k == 0` 검사 없이 | 배열 밖 읽기. 쓰레기 값 또는 크래시 | unsigned에서 `0 - 1`은 4294967295 | `if (k > 0)`로 감싸고 없으면 -1 |
| 미래 검사보다 `now - t > window`를 먼저 | 미래 시각이 전부 거절되거나 반대로 통과해 버린다 | `t > now`면 `now - t`가 언더플로로 거대해진다 | `t > now`를 **먼저** 거른 다음 `now - t` |
| 벽시계 타임스탬프를 그대로 append | 한 번 시계가 뒤로 점프하면 **그 뒤 모든 질의**가 조용히 틀린다 | 정렬이 깨지면 이진 탐색의 전제가 무너진다 | clamp(01번) 또는 drop(08번). 가능하면 monotonic |
| 감긴 링버퍼에 raw 인덱스로 탐색 | 대부분 맞다가 감긴 뒤부터 틀린다. 닫힌 구간에서 `mid`를 위로 안 치우치면 `hi = lo+1`에서 무한 루프 | `buf[]`는 감긴 순서라 정렬돼 있지 않다 | `at(off)` 헬퍼로 offset만 다룬다. 닫힌 구간은 `mid = lo + (hi-lo+1)/2` |

## 7. 손으로 확인하기

01번의 경계 검사를 직접 깨 보는 것이 제일 빠른 학습이다.

```sh
cd 01_gps_fix_cache && ./main.sh sol
grep -n "hist_upper_bound\|hist_lower_bound\|k > 0" 01_gps_fix_cache/gps_cache_solution.c
```

`gps_cache.c`(stub)에 §5의 v1을 구현하고 `./main.sh`를 돌린 다음, 이런 변형을 하나씩 넣어 보고
어떤 검사가 FAIL이 되는지 본다.

- `upper_bound`를 `lower_bound`로 바꾼다 → 정확히 일치하는 시각 질의만 FAIL
- `if (k > 0)`을 없앤다 → "첫 샘플 이전" 검사에서 쓰레기 값 또는 크래시
- `t > now` 검사를 뒤로 옮긴다 → 미래 시각 검사가 FAIL

08번의 닫힌 구간 형태도 읽어 본다. `+ 1u`를 지우고 돌리면 **행(hang)**한다 — 무한 루프가 어떤
모습인지 한 번 보는 것도 가치가 있다(Ctrl-C로 끊는다).

```sh
cd 08_battery_energy_pipeline && grep -n -A 10 "floor_index" energy_store_solution.c && ./main.sh sol
```

이 노트의 조각과 비교 테스트는 한 파일로 붙여 실제로 돌린 것이다. §4·§5를 모아
`cc -std=c11 -O2 -Wall -Wextra -o c2 c2.c && ./c2`.

## 8. 자가 점검

```check
Q: 시계열 배열이 별도 정렬 없이도 정렬돼 있는 이유는? 그 전제가 깨지는 경우와 대처는?
A: 샘플러가 도착 순서대로 append하므로 타임스탬프가 비감소다. 깨지는 경우는 벽시계가 NTP로 뒤로
   점프할 때다. 01번은 직전 값으로 clamp해 샘플을 살리고, 08번은 drop한다 — 에너지 적분은 dt로
   나누므로 시각을 왜곡하면 값이 틀어진다. 근본 해법은 CLOCK_MONOTONIC이다.

Q: 이진 탐색의 불변식을 말로 설명하고, 그것으로 루프 본문 두 줄을 유도해 보라.
A: [lo, hi) 반열린 구간에서 lo 왼쪽은 전부 조건 불만족 확정, hi 오른쪽은 전부 만족 확정, 답은
   [lo, hi] 안이다. mid가 만족하면 mid도 답일 수 있으니 hi = mid, 불만족이면 lo = mid + 1.
   lo == hi면 그 값이 답이다. 조건은 lower면 a[i] >= t, upper면 a[i] > t.

Q: lower_bound와 upper_bound의 차이를 "개수"로 설명하고, "t 이하 중 마지막"을 유도해 보라.
A: lower_bound(t)는 t 미만인 개수, upper_bound(t)는 t 이하인 개수다. 따라서 "t 이하 중 마지막"
   = (t 이하인 개수) - 1 = upper_bound(t) - 1이고, 개수가 0이면 그런 원소가 없다. lower_bound - 1은
   t와 같은 샘플이 있을 때 하나 앞을 가리켜 틀리고, 중복이 있으면 더 벌어진다.

Q: 감긴 링버퍼 위에서 wrap을 신경 쓰지 않고 이진 탐색하는 방법은?
A: 인덱스를 raw 배열 인덱스가 아니라 tail 기준 offset(0..n-1)으로만 다룬다.
   at(off) = &buf[(tail + off) & MASK] 헬퍼 하나를 만들면 그 위의 탐색 코드는 wrap을 모른 채
   평범한 배열 코드가 된다. head/tail이 감겨도 영향이 없다. 01번의 hist_at()이 이것이다.

Q: 하네스가 검사하는 경계 조건 다섯 가지를 대고, 각각 어디서 판정하는지 말해 보라.
A: (1) 미래 t — 탐색 전 t > now. (2) 창보다 오래된 t — 탐색 전 now - t > window. 순서가 중요하다:
   t > now를 먼저 걸러야 now - t가 언더플로하지 않는다. (3) 첫 샘플 이전 — 탐색 후 upper_bound가
   0인지. (4) 정확히 일치 — upper_bound여야 맞다. (5) 빈 상태 — hi = n 덕에 자동 처리된다.

Q: 직접 쓴 이진 탐색을 어떻게 검증하나? 무작위 입력 생성에서 반드시 챙길 세 가지는?
A: 정의를 그대로 옮긴 O(n) 루프를 참조로 쓰고 무작위 입력으로 비교한다. (1) 배열 크기 0과 1을
   포함시킨다 — off-by-one은 거기서 나온다. (2) 증가량 0을 허용해 중복을 만든다 — 중복 없으면
   lower/upper 혼동을 못 잡는다. (3) t를 값 범위보다 넓게 뽑는다. 시드는 고정 xorshift로.
```

## 9. 요약 카드

- 시계열은 **도착 순서 append라서 공짜로 정렬**돼 있다. 그게 O(log n)의 근거 전부다.
  벽시계는 뒤로 점프하므로 **clamp**(01번) 또는 **drop**(08번)으로 비감소를 지킨다.
- 구간은 **`[lo, hi)`, `hi = n`으로 시작.** 불변식: lo 왼쪽은 불만족 확정, hi 오른쪽은 만족 확정.
  본문 두 줄은 만족이면 `hi = mid`, 불만족이면 `lo = mid + 1`, 종료는 `while (lo < hi)`.
- `lower_bound(t)` = `t` 미만 개수. `upper_bound(t)` = `t` **이하 개수**. 차이는 **`<` vs `<=` 한 글자**.
- **"t 이하 중 마지막" = `upper_bound(t) - 1`**, 단 `k == 0`이면 없음. `lower_bound - 1`은 틀렸다.
- 링버퍼에서는 **`at(off) = buf[(tail + off) & MASK]`** 헬퍼로 offset만 다룬다. wrap을 잊는다.
- 경계 5종: **미래 / 창 밖 / 첫 샘플 이전 / 정확히 일치 / 빈 상태.** 미래를 먼저 거른 뒤 `now - t`.
- 검증: 손 추적 표 + **brute force 비교**(크기 0·1, 중복, 넓은 t). 다음은 [C3. 누적합](C3_prefix_sum_and_range_query.md).

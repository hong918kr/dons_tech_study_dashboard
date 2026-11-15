---
id: ALGO-B02
time: 50분
updated: 2026-11-11
---
# ALGO-B02 이진 탐색과 경계: lower_bound / upper_bound

## 학습 목표

- **반개구간(half-open interval) `[lo, hi)` 불변식** 하나로 `lower_bound` / `upper_bound` / 정확 일치(첫 등장 또는 -1)를 off-by-one 없이 쓴다. 답이 0~n 중 하나라는 것, 루프가 `lo == hi` 에서 끝난다는 것을 불변식에서 바로 말할 수 있다.
- 회전 정렬 배열(rotated sorted array)에서 **회전점**(최솟값 위치)과 **값 탐색**을 O(log n) 에 한다. 값이 서로 다르다는 가정이 왜 필요한지, 중복이 있으면 왜 최악 O(n) 인지 설명한다.
- 고전 버그 4개 — `(lo+hi)/2` 오버플로, `hi = n-1` 의 size_t underflow, `lo = mid` 무한 루프, 반개구간/닫힌구간 섞기 — 를 **전수 테스트(exhaustive test)** 가 실제로 잡는 것을 보고, 중복 없는 테스트로는 못 잡는 버그도 있다는 것을 안다.
- 펌웨어 상황(플래시 로그에서 `ts ≥ T` 인 첫 레코드, 순차 기록 영역의 첫 지워진 페이지)을 "단조 술어(monotonic predicate)가 처음 참이 되는 곳" 으로 바꿔 풀고, 플래시 읽기 횟수가 ⌈log₂(n+1)⌉ 이하임을 테스트로 확인한다.

## 1. 문제

이진 탐색은 면접에서 "5분이면 짜죠" 라고 생각하는 순간 떨어지는 문제다. 경계(첫 위치 / 마지막 위치 / 없으면 어디)를 묻는 꼬리질문과 off-by-one, 오버플로, 빈 배열이 바로 따라온다 — 펌웨어에서는 그 버그가 부팅 때 플래시 쓰기 포인터를 잘못 찾는 사고로 이어진다.

핵심은 코드를 외우는 게 아니라 **불변식(invariant)** 을 외우는 것이다. 정렬된 배열 `a[0..n)` 과 값 `x` 에 대해 `lower_bound(x)` = "`a[i] >= x` 인 첫 i (없으면 n)". 탐색 중 항상 다음이 성립하도록 `lo`, `hi` 를 움직인다.

- `[0, lo)` 의 모든 원소는 `< x` (이미 "왼쪽" 으로 확정)
- `[hi, n)` 의 모든 원소는 `>= x` (이미 "오른쪽" 으로 확정)
- `[lo, hi)` 는 아직 모름. 시작은 `lo = 0, hi = n` (전부 모름) — 두 확정 구역이 비어 있으니 불변식이 공짜로 성립한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b02a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">lower_bound 의 반개구간 불변식 — 세 구역</text>
  <rect x="40" y="50" width="200" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="240" y="50" width="260" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="500" y="50" width="200" height="40" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="140" y="75">[0, lo) : 모두 &lt; x</text>
    <text x="370" y="75">[lo, hi) : 아직 모름</text>
    <text x="600" y="75">[hi, n) : 모두 ≥ x</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="40" y="110">0</text><text x="240" y="110">lo</text><text x="500" y="110">hi</text><text x="700" y="110">n</text>
    <text x="355" y="140">mid = lo + (hi - lo) / 2</text>
  </g>
  <line x1="355" y1="125" x2="355" y2="95" stroke="currentColor" marker-end="url(#b02a)"/>
  <rect x="40" y="165" width="320" height="58" class="f-ok" stroke="currentColor"/>
  <rect x="380" y="165" width="320" height="58" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace">
    <text x="52" y="188">a[mid] &lt; x  →  lo = mid + 1</text>
    <text x="392" y="188">a[mid] ≥ x  →  hi = mid</text>
  </g>
  <g fill="currentColor">
    <text x="52" y="210">mid 까지 "&lt; x" 구역에 넣는다 (mid 포함)</text>
    <text x="392" y="210">mid 부터 "≥ x" 구역에 넣는다 (mid 포함)</text>
    <text x="40" y="250">lo ≤ mid &lt; hi 이므로 a[mid] 는 항상 유효하고, 어느 쪽이든 [lo, hi) 가 최소 1 줄어든다 → 반드시 끝난다.</text>
    <text x="40" y="275">끝: lo == hi. "모름" 구역이 비었으니 lo 왼쪽은 전부 &lt; x, 오른쪽은 전부 ≥ x → 답 = lo (0..n).</text>
  </g>
</svg>
```

`upper_bound(x)` = "`a[i] > x` 인 첫 i" 는 비교 하나(`<` → `<=`)만 바뀐다. 둘의 차 `upper - lower` 가 x 의 개수다. 둘 다 사실 **같은 문제**의 사례다: 앞쪽은 전부 거짓, 뒤쪽은 전부 참인 술어 `P(i)` 에서 "처음 참이 되는 i" 를 찾는 것 (`P(i) = a[i] >= x` 또는 `a[i] > x`). 이 시각이 Part B·C 를 전부 푼다. (답 자체를 이분 탐색하는 일반형은 ALGO-B04, 정렬 테이블 보간은 ALGO-B03.)

### Part A — 경계 탐색 세 가지

`int32_t` 정렬(비내림차순, 중복 가능) 배열에 대해:

- `size_t lower_bound_i32(const int32_t *a, size_t n, int32_t x)` — `a[i] >= x` 인 첫 i, 없으면 n.
- `size_t upper_bound_i32(...)` — `a[i] > x` 인 첫 i, 없으면 n.
- `ptrdiff_t find_exact(...)` — x 의 **첫 등장** 인덱스, 없으면 -1.

`n == 0` (그때 `a` 는 NULL 일 수 있다), 원소 1개, 전부 같은 값, `INT32_MIN` / `INT32_MAX` 가 들어 있는 배열도 맞아야 한다. 검증: 길이 0~50 마다 패턴 5개(무작위 중복, 전부 같음, 3 간격, `INT32_MAX` 근처, `INT32_MIN` 근처)를 결정적으로 만들고, `min-1 ~ max+1` 의 **모든 타깃**(int32 범위 밖은 제외)을 선형 탐색과 대조한다.

### Part B — 회전 정렬 배열 (값은 모두 다름)

오름차순 배열을 왼쪽으로 r 칸 돌린 배열(예: `[4,5,6,7,0,1,2]`)에서:

- `size_t rot_min_index(const int32_t *a, size_t n)` — 최솟값 위치(= 회전량). n == 0 이면 0.
- `ptrdiff_t rot_search(const int32_t *a, size_t n, int32_t x)` — x 의 위치, 없으면 -1. **이진 탐색 한 번**으로.

중복 정책: 이 레슨의 핵심 버전은 **값이 모두 다르다**고 가정한다. 중복이 있으면 어떻게 되는지는 5. 풀이와 연습 3 에서 다룬다. 검증: 길이 0~50 의 **모든 회전** × 모든 타깃을 선형 탐색과 대조.

### Part C — 플래시 상주 로그와 순차 기록 영역

1. 이벤트 로그가 외부 SPI NOR 플래시에 8바이트 레코드(`ts_ms`, `event`, `arg`)로 시간순(같은 ms 중복 가능)으로 쌓여 있다. 고장 분석 명령 "T 이후 이벤트를 보여 줘" 에 답하려면 `ts_ms >= T` 인 첫 레코드가 필요하다. `log_first_at_or_after(f, T)` 와 `log_count_in(f, t0, t1)` (`t0 <= ts < t1` 개수)을 구현한다. **플래시는 `log_read_ts()` 로만 읽고**, 테스트는 읽기 횟수가 쿼리당 ⌈log₂(n+1)⌉ 이하인지 센다 (n = 1000 → 10회).
2. 로그 영역은 페이지 0 부터 순서대로 쓰고 지운 페이지는 헤더 워드가 `0xFFFFFFFF` 다 (NOR 지움 상태 = 전부 1). 부팅 때 **첫 지워진 페이지**(= 다음 쓰기 위치)를 `first_erased_page()` 로 찾는다. 전원 차단으로 반쯤 쓰인 헤더(`0xFFFF0000`, 일부 비트만 0)는 "기록됨" 으로 본다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part C-2: 16 페이지 영역, 0..10 기록됨 → 첫 지워진 페이지 k = 11 (탐침 4회)</text>
  <g stroke="currentColor">
    <rect x="40" y="50" width="40" height="40" class="f-ok"/><rect x="80" y="50" width="40" height="40" class="f-ok"/>
    <rect x="120" y="50" width="40" height="40" class="f-ok"/><rect x="160" y="50" width="40" height="40" class="f-ok"/>
    <rect x="200" y="50" width="40" height="40" class="f-ok"/><rect x="240" y="50" width="40" height="40" class="f-ok"/>
    <rect x="280" y="50" width="40" height="40" class="f-ok"/><rect x="320" y="50" width="40" height="40" class="f-ok"/>
    <rect x="360" y="50" width="40" height="40" class="f-ok"/><rect x="400" y="50" width="40" height="40" class="f-ok"/>
    <rect x="440" y="50" width="40" height="40" class="f-acc"/><rect x="480" y="50" width="40" height="40" class="f-mut"/>
    <rect x="520" y="50" width="40" height="40" class="f-mut"/><rect x="560" y="50" width="40" height="40" class="f-mut"/>
    <rect x="600" y="50" width="40" height="40" class="f-mut"/><rect x="640" y="50" width="40" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="60" y="105">0</text><text x="220" y="105">4</text><text x="380" y="105">8</text><text x="460" y="105">10</text>
    <text x="500" y="105">11</text><text x="540" y="105">12</text><text x="660" y="105">15</text>
    <text x="380" y="42">①</text><text x="540" y="42">②</text><text x="460" y="42">③</text><text x="500" y="42">④</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="132">① [0,16) mid=8  기록됨 → lo=9    ② [9,16) mid=12 지워짐 → hi=12</text>
    <text x="40" y="152">③ [9,12) mid=10 기록됨 → lo=11   ④ [11,12) mid=11 지워짐 → hi=11  ⇒ k = 11</text>
  </g>
  <text x="40" y="182" fill="currentColor">페이지 10 은 전원 차단으로 헤더가 0xFFFF0000 (일부 비트만 0) — 0 비트가 하나라도 있으면 "기록됨".</text>
</svg>
```

## 2. Problem (English)

> **Boundary binary search.** Implement in C11, no recursion, no allocation, no library search functions:
>
> 1. `size_t lower_bound_i32(const int32_t *a, size_t n, int32_t x)` — first index `i` with `a[i] >= x`, or `n`. `upper_bound_i32` — first index with `a[i] > x`, or `n`. `ptrdiff_t find_exact(...)` — index of the **first** occurrence of `x`, or `-1`. `a` is sorted non-decreasing and may contain duplicates; `n` may be 0 (then `a` may be `NULL`).
> 2. `a` is a strictly increasing array rotated by an unknown amount. `size_t rot_min_index(const int32_t *a, size_t n)` returns the index of the minimum (0 if `n == 0`). `ptrdiff_t rot_search(const int32_t *a, size_t n, int32_t x)` returns the index of `x` or `-1` in O(log n).
> 3. A flash-resident log of `n` records sorted by `uint32_t ts_ms` (ties allowed) can only be read through `log_read_ts(f, i)`. Return the first record with `ts_ms >= T` using at most ⌈log₂(n+1)⌉ reads. A region of pages is written sequentially from page 0; an erased page has header `0xFFFFFFFF`. Return the index of the first erased page (`npages` if full).
>
> Examples: `a = [1,2,2,2,5,5,9,9]`: `lower_bound(2) = 1`, `upper_bound(2) = 4`, `lower_bound(3) = upper_bound(3) = 4`, `find_exact(9) = 6`, `find_exact(10) = -1`. `a = [4,5,6,7,0,1,2]`: `rot_min_index = 4`, `rot_search(0) = 4`, `rot_search(3) = -1`.
>
> Constraints: `0 <= n <= SIZE_MAX / sizeof(int32_t)`. Your midpoint computation must not overflow for any valid `n`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 8개 함수 본문. 스텁은 0 / -1 같은 "틀렸지만 안전한" 값을 돌려준다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:137  lower_bound_i32(one, 1, 4) == 0 && lower_bound_i32(one, 1, 5) == 0 && lower_bound_i32(one, 1, 6) == 1
…
Part A exhaustive: 255 arrays, 7463 queries, mismatches lower=6998 upper=7248 find=3565
…
Part B exhaustive: 1276 rotated arrays, 128778 queries, mismatches min=1225 search=42925
…
Part C log: 3002 queries, mismatches=2987 count_mismatches=2999, max flash reads=0 (bound 10)
…
Part C region: 1181 cases, mismatches=1172, over-budget=0
FAIL starter.c:261  bad == 0
4/25 passed
```

(exit 1. 통과한 4개는 빈 배열 사례 3개 — 스텁의 0 / -1 이 우연히 정답 — 와, 스텁이 플래시를 한 번도 안 읽어 "읽기 예산 초과 없음" 이 된 영역 검사 1개.)

## 4. 힌트

<details><summary>힌트 1 — 코드보다 불변식을 먼저 적는다</summary>

종이에 `[0,lo): < x`, `[hi,n): >= x` 를 적고 시작한다. `a[mid] < x` 면 mid 도 왼쪽 구역 → `lo = mid + 1`. 아니면 mid 가 오른쪽 구역 → `hi = mid`. `hi = mid - 1` 이 나오면 불변식이 깨진 것이다 (mid 를 "모름" 에서 빼면서 아무 구역에도 안 넣었다).

</details>

<details><summary>힌트 2 — find_exact 는 새 루프가 필요 없다</summary>

첫 등장 위치는 정의상 `lower_bound(x)` 다. 그 자리가 배열 안이고 값이 x 면 그게 답, 아니면 -1. "같으면 바로 return" 하는 교과서 탐색은 **아무** 등장이나 돌려준다 (7. 실수 4).

</details>

<details><summary>힌트 3 — 회전점도 "처음 참" 문제다</summary>

`[4,5,6,7,0,1,2]` 에서 `a[i] <= a[n-1]` (= 2) 를 칠하면 F F F F T T T. 앞 블록은 전부 마지막 값보다 크고 뒤 블록은 전부 작거나 같다 (값이 다르니까). 이 술어가 처음 참이 되는 곳이 최솟값.

</details>

<details><summary>힌트 4 — rot_search: mid 에서 반드시 한쪽은 정렬돼 있다</summary>

`a[lo] <= a[mid]` 면 `[lo, mid]` 가 정렬된 구간이다 (끊기는 곳이 그 안에 없다). 정렬된 쪽은 양 끝 값으로 x 가 안에 있는지 O(1) 에 안다 → 안에 있으면 그쪽으로, 아니면 반대쪽으로. 반개구간이므로 오른쪽 끝 값은 `a[hi - 1]`.

</details>

<details><summary>힌트 5 — Part C 는 Part A 를 그대로 옮긴다</summary>

`a[mid]` 대신 `log_read_ts(f, mid)`, `a[mid] >= x` 대신 `page_is_erased(r, mid)`. 루프가 mid 하나당 정확히 한 번 읽으면 읽기 횟수 = 반복 횟수 ≤ ⌈log₂(n+1)⌉ 이 자동으로 나온다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 정렬 방향? 중복 있음? 없으면 무엇을 반환(-1 / n / 삽입 위치)? n = 0 가능? 인덱스 타입(size_t)? 회전 배열은 값이 다른가? |
| 2. 무식한 방법 | 선형 탐색 O(n) 시간, O(1) 메모리. 테스트 기준(oracle)으로 남겨 둔다 |
| 3. 더 나은 생각 + 불변식 | 정렬 → 술어가 F…F T…T 로 단조 → 처음 T 를 반개구간 `[lo, hi)` 로 찾는다. 불변식을 먼저 말한다 |
| 4. 코드 | `while (lo < hi)`, `mid = lo + (hi - lo) / 2`, `lo = mid + 1` / `hi = mid` 두 줄 |
| 5. 소리 내어 말할 테스트 | n = 0, n = 1, x 가 전부보다 작음/큼, 전부 같은 값, 중복의 첫/끝, `INT32_MIN`/`INT32_MAX`, 회전 0칸과 n-1칸 |
| 6. 복잡도 | 시간 O(log n) — 비교 ⌈log₂(n+1)⌉ 회 이하. 추가 메모리 O(1), 스택 O(1) (재귀 없음) |

### Part A — 왜 `lo = mid + 1` / `hi = mid` 인가

불변식을 지키면 두 갱신식은 **선택이 아니라 결과**다. `a[mid] < x` 면 mid 를 포함한 `[lo, mid]` 가 전부 `< x` (정렬되어 있으니) → 왼쪽 구역의 새 끝은 `mid + 1`. `a[mid] >= x` 면 `[mid, hi)` 가 전부 `>= x` → 오른쪽 구역의 새 시작은 `mid`.

종료와 반복 횟수: 모름 구역의 크기 s = hi - lo 가 왼쪽으로 가면 `⌊s/2⌋`, 오른쪽으로 가면 `s - ⌊s/2⌋ - 1 ≤ ⌊s/2⌋` 가 된다. s ≥ 1 이면 매번 `s → ≤ ⌊s/2⌋` 이므로 반복 횟수 ≤ ⌊log₂ n⌋ + 1 = ⌈log₂(n+1)⌉. 이건 비교 기반 탐색의 하한과 같다 — 답이 0..n 의 n+1 가지이고 비교 한 번은 2갈래이므로 최악 ⌈log₂(n+1)⌉ 번은 물어야 한다 (정보 이론적 하한, information-theoretic lower bound). 그래서 Part C 의 "읽기 ≤ ⌈log₂(n+1)⌉" 은 그냥 테스트가 아니라 **최적성** 확인이다.

손 계산: `a = [1,2,2,2,5,5,9,9]`, n = 8.

| 단계 | lower_bound(2): lo, hi, mid, a[mid] → 갱신 | upper_bound(2): lo, hi, mid, a[mid] → 갱신 |
|---|---|---|
| 1 | 0, 8, 4, 5 ≥ 2 → hi = 4 | 0, 8, 4, 5 > 2 → hi = 4 |
| 2 | 0, 4, 2, 2 ≥ 2 → hi = 2 | 0, 4, 2, 2 ≤ 2 → lo = 3 |
| 3 | 0, 2, 1, 2 ≥ 2 → hi = 1 | 3, 4, 3, 2 ≤ 2 → lo = 4 |
| 4 | 0, 1, 0, 1 &lt; 2 → lo = 1 | (lo == hi = 4, 끝) |
| 답 | **1** (첫 2) | **4** (2 다음) → 개수 = 4 - 1 = 3 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">lower_bound(2) 추적 — 회색 = 아직 모름, 녹색 = "&lt; 2" 확정, 주황 = "≥ 2" 확정, 강조 = mid</text>
  <g fill="currentColor" font-family="monospace" text-anchor="middle" font-size="12">
    <text x="115" y="48">1</text><text x="165" y="48">2</text><text x="215" y="48">2</text><text x="265" y="48">2</text>
    <text x="315" y="48">5</text><text x="365" y="48">5</text><text x="415" y="48">9</text><text x="465" y="48">9</text>
  </g>
  <g fill="currentColor" font-size="12"><text x="20" y="48">a[i] =</text><text x="20" y="80">step 1</text><text x="20" y="120">step 2</text><text x="20" y="160">step 3</text><text x="20" y="200">step 4</text><text x="20" y="235">끝</text></g>
  <g stroke="currentColor">
    <rect x="90" y="62" width="50" height="28" class="f-mut"/><rect x="140" y="62" width="50" height="28" class="f-mut"/><rect x="190" y="62" width="50" height="28" class="f-mut"/><rect x="240" y="62" width="50" height="28" class="f-mut"/>
    <rect x="290" y="62" width="50" height="28" class="f-acc"/><rect x="340" y="62" width="50" height="28" class="f-mut"/><rect x="390" y="62" width="50" height="28" class="f-mut"/><rect x="440" y="62" width="50" height="28" class="f-mut"/>
    <rect x="90" y="102" width="50" height="28" class="f-mut"/><rect x="140" y="102" width="50" height="28" class="f-mut"/><rect x="190" y="102" width="50" height="28" class="f-acc"/><rect x="240" y="102" width="50" height="28" class="f-mut"/>
    <rect x="290" y="102" width="200" height="28" class="f-warn"/>
    <rect x="90" y="142" width="50" height="28" class="f-mut"/><rect x="140" y="142" width="50" height="28" class="f-acc"/>
    <rect x="190" y="142" width="300" height="28" class="f-warn"/>
    <rect x="90" y="182" width="50" height="28" class="f-acc"/>
    <rect x="140" y="182" width="350" height="28" class="f-warn"/>
    <rect x="90" y="218" width="50" height="26" class="f-ok"/>
    <rect x="140" y="218" width="350" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="505" y="81">lo=0 hi=8 mid=4: 5≥2 → hi=4</text>
    <text x="505" y="121">lo=0 hi=4 mid=2: 2≥2 → hi=2</text>
    <text x="505" y="161">lo=0 hi=2 mid=1: 2≥2 → hi=1</text>
    <text x="505" y="201">lo=0 hi=1 mid=0: 1&lt;2 → lo=1</text>
    <text x="505" y="236">lo = hi = 1 → 답 1</text>
  </g>
</svg>
```

`mid` 계산은 `lo + (hi - lo) / 2`. `hi - lo` 는 항상 0..n 이라 넘치지 않고, 결과는 `lo ≤ mid < hi` (s ≥ 1 이면 `s/2 < s`). `(lo + hi) / 2` 는 `lo + hi` 가 타입 범위를 넘을 수 있다 — 7. 실수 1 에서 실제로 무한 루프를 본다. 부호 없는 `/ 2` 는 컴파일러가 시프트 하나로 바꾼다 (아래 Q5 의 Cortex-M4 asm).

`find_exact` 는 `lower_bound` 를 재사용한다: `i < n && a[i] == x` 면 i, 아니면 -1. 반환형을 `ptrdiff_t` 로 둔 이유: -1 을 표현해야 하는데 `size_t` 에 -1 을 넣으면 `SIZE_MAX` 가 된다. "없으면 n" 규약(C++ `std::lower_bound` 스타일)을 쓰면 `size_t` 하나로 끝나서 펌웨어 API 에서는 오히려 그쪽이 깔끔하다 — 면접에서는 어느 규약인지 먼저 묻는다.

### Part B — 회전점: 술어 `a[i] <= a[n-1]`

값이 서로 다른 오름차순 배열을 r 칸 돌리면 두 오름차순 블록 `[r..n)` 가 앞으로, `[0..r)` 이 뒤로 간다: `A = [큰 블록 | 작은 블록]`. 마지막 원소 `L = a[n-1]` 은 작은 블록의 최댓값이다 (r = 0 이면 전체 최댓값). 그러면

- 큰 블록의 원소는 모두 `> L` (원래 배열에서 L 보다 뒤에 있던 값들),
- 작은 블록의 원소는 모두 `<= L`.

즉 `P(i) = a[i] <= L` 은 F…F T…T 이고, 처음 T 가 최솟값이다. Part A 와 같은 루프에 비교만 바꾸면 된다. **값이 다르다는 가정이 여기서 쓰인다**: 중복이 있으면 큰 블록에도 L 과 같은 값이 있을 수 있어(`[2,2,2,0,1,2]`) 술어가 단조가 아니게 된다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">회전 배열 [4,5,6,7,0,1,2]: 두 블록과 술어 a[i] ≤ a[n-1]</text>
  <g stroke="currentColor">
    <rect x="60" y="100" width="60" height="30" class="f-warn"/><rect x="130" y="80" width="60" height="50" class="f-warn"/>
    <rect x="200" y="60" width="60" height="70" class="f-warn"/><rect x="270" y="40" width="60" height="90" class="f-warn"/>
    <rect x="340" y="125" width="60" height="5" class="f-ok"/><rect x="410" y="115" width="60" height="15" class="f-ok"/>
    <rect x="480" y="105" width="60" height="25" class="f-ok"/>
  </g>
  <line x1="40" y1="105" x2="560" y2="105" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="565" y="109" fill="currentColor" font-size="12">L = a[n-1] = 2</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="90" y="150">4</text><text x="160" y="150">5</text><text x="230" y="150">6</text><text x="300" y="150">7</text>
    <text x="370" y="150">0</text><text x="440" y="150">1</text><text x="510" y="150">2</text>
    <text x="90" y="172">F</text><text x="160" y="172">F</text><text x="230" y="172">F</text><text x="300" y="172">F</text>
    <text x="370" y="172">T</text><text x="440" y="172">T</text><text x="510" y="172">T</text>
  </g>
  <text x="20" y="172" fill="currentColor" font-size="12">P(i)</text>
  <text x="40" y="200" fill="currentColor">큰 블록(주황)은 전부 &gt; L, 작은 블록(녹색)은 전부 ≤ L → 처음 T = 인덱스 4 = 최솟값.</text>
  <text x="40" y="220" fill="currentColor">rot_search 는 mid 가 어느 블록에 있든 [lo, mid] 또는 [mid, hi) 중 하나가 통째로 정렬돼 있다는 것을 쓴다.</text>
</svg>
```

`rot_search` 는 한 번의 루프로 한다. 불변식은 "x 가 있다면 `[lo, hi)` 안에 있다". mid 에서:

1. `a[mid] == x` → 찾음.
2. `a[lo] <= a[mid]` → `[lo, mid]` 안에 끊김이 없다 (값이 다르고 끊김은 "큰 값 → 작은 값" 하나뿐이니, 끊김이 그 안에 있었다면 `a[lo] > a[mid]`). x 가 `[a[lo], a[mid])` 안이면 `hi = mid`, 아니면 `lo = mid + 1`.
3. 아니면 `[mid, hi-1]` 이 정렬돼 있다. x 가 `(a[mid], a[hi-1]]` 안이면 `lo = mid + 1`, 아니면 `hi = mid`.

손 계산 `[4,5,6,7,0,1,2]`, x = 0:

| 단계 | lo | hi | mid | a[mid] | 정렬된 쪽 | 판단 |
|---|---|---|---|---|---|---|
| 1 | 0 | 7 | 3 | 7 | 왼쪽 (4 ≤ 7) | 0 ∉ [4, 7) → lo = 4 |
| 2 | 4 | 7 | 5 | 1 | 왼쪽 (0 ≤ 1) | 0 ∈ [0, 1) → hi = 5 |
| 3 | 4 | 5 | 4 | 0 | — | 일치 → **4** |

비교 중 `x < a[mid]`, `x <= a[hi-1]` 처럼 **값끼리만** 비교하고 빼기(`x - a[lo]`)를 하지 않는 것도 중요하다 — `INT32_MAX - INT32_MIN` 은 int32 에서 넘친다. 테스트의 `[INT32_MAX, INT32_MIN]` 사례가 그 확인이다.

두 단계 버전(회전점 k 를 찾고, x 가 `a[n-1]` 이하면 `[k, n)`, 아니면 `[0, k)` 에서 lower_bound)도 똑같이 O(log n) 이다 — 연습 4. 한 번 루프판은 면접에서 자주 나오는 형태이고(LeetCode 33), 두 단계판은 이미 검증된 부품을 재사용해서 실수할 곳이 적다. 실무 코드라면 두 단계판을 고르겠다.

**중복이 있으면 최악 O(n)**: `[1,1,…,1,0,1,…,1]` 에서 `a[lo] == a[mid] == a[hi-1]` 이면 0 이 왼쪽에 있는지 오른쪽에 있는지 알 방법이 없다. 이건 알고리즘의 한계가 아니라 **문제의 한계**다: 모두 1 인 배열(0 없음)과 한 칸만 0 인 배열들은 그 0 칸을 직접 읽기 전까지 똑같이 보이므로, 어떤 알고리즘이든 적대자(adversary)가 "아직 안 읽은 칸" 에 0 을 숨기면 최악 n 칸을 다 읽어야 한다. 연습 3 이 n = 50 에서 실측한다: 값이 다를 때 상한 6회 → 중복판은 최악 **26회** (양 끝을 하나씩 버리니 약 n/2).

### Part C — 플래시 로그: 같은 루프, 비싼 읽기

`log_first_at_or_after` 는 Part A 의 `lower_bound` 에서 `a[mid]` 를 `log_read_ts(f, mid)` 로 바꾼 것뿐이다. 실제 결과 (solution.c 출력):

```text
Part C log: 3002 queries, mismatches=0 count_mismatches=0, max flash reads=10 (bound 10)
Part C region: 1181 cases, mismatches=0, over-budget=0
```

1000 레코드에서 최대 10회 = ⌈log₂ 1001⌉. 선형 탐색이면 최악 1000회의 플래시 읽기다. SPI NOR 에서 랜덤 읽기 하나는 명령 + 주소 + 데이터를 버스로 보내는 트랜잭션이라 (클럭·모드에 따라 수 µs~수십 µs) 이 차이가 그대로 응답 시간이다. 메모리 맵(XIP) 플래시라도 대기 상태(wait state)·캐시 미스가 있다.

설계 포인트:

- `ts_ms` 는 `uint32_t` — `t - ts` 같은 뺄셈 비교가 아니라 `ts < t` 직접 비교. `log_count_in` 의 `t1 = t + 50` 이 `UINT32_MAX` 근처에서 감싸지면(wrap) `t1 <= t0` → 0 으로 처리한다 (테스트가 ts 를 `0xFFFF0000` 부터 시작해서 그 경계를 지난다).
- `[t0, t1)` 개수 = `lower(t1) - lower(t0)` — 반개구간 둘의 차라 경계 중복/누락이 없다. "t0 이상 t1 이하" 를 원하면 `upper(t1) - lower(t0)`.
- 2^32 ms ≈ 49.7 일이면 ms 틱은 한 바퀴 돈다. 로그가 그보다 길게 남는 장치라면 ts 가 더 이상 정렬이 아니다 → 시퀀스 번호나 64비트 시간을 키로 쓴다 (Q7, 연습 5).

`first_erased_page` 는 술어 `page_is_erased(p)` 가 F…F T…T 라는 것만 쓴다 (위 그림). Don 의 SSD 경험으로 말하면 NAND 블록 안 페이지는 **순서대로만 프로그램**할 수 있어서 "열린 블록의 다음 쓰기 페이지" 찾기가 정확히 이 문제다. 단, 두 가지를 면접에서 꼭 덧붙인다.

1. **단조성이 깨지면 이진 탐색은 조용히 틀린다.** 쓰다 만 페이지(헤더는 0 비트가 생겼지만 데이터는 불완전)는 "기록됨" 쪽에 들어가므로 단조성은 유지되지만, 그 페이지 내용은 CRC 로 다시 확인해야 한다. 경계 확인(`k-1` 은 기록, `k` 는 지움)을 마지막에 한 번 더 읽어 검증하는 것도 싸다.
2. NAND 의 "지워짐" 은 `== 0xFF…` 가 아니라 **0 비트 수가 임계값 이하** 로 판정한다 (지운 페이지에도 비트 반전이 생긴다). 술어만 바뀌고 탐색은 그대로다.

### 복잡도 요약

| 함수 | 시간 (최악) | 추가 메모리 | 스택 |
|---|---|---|---|
| lower/upper_bound, find_exact | ⌈log₂(n+1)⌉ 비교 | O(1) | O(1), 재귀 없음 |
| rot_min_index, rot_search (값 다름) | O(log n) | O(1) | O(1) |
| 중복 허용 회전 탐색 (연습 3) | **O(n)** — 피할 수 없음 | O(1) | O(1) |
| Part C 로그·영역 | ⌈log₂(n+1)⌉ 플래시 읽기 | O(1) | O(1) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 25/25 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 25/25, 보고 없음
```

실제 출력:

```text
Part A exhaustive: 255 arrays, 7463 queries, mismatches lower=0 upper=0 find=0
Part B exhaustive: 1276 rotated arrays, 128778 queries, mismatches min=0 search=0
Part C log: 3002 queries, mismatches=0 count_mismatches=0, max flash reads=10 (bound 10)
Part C region: 1181 cases, mismatches=0, over-budget=0
25/25 passed
```

(전체 0.1초 남짓. 255 = 길이 51개 × 패턴 5개, 1276 = 1 + 1 + 2 + … + 50, 즉 길이별 모든 회전.)

## 7. 자주 하는 실수

`demo_bugs.c` 가 고전 버그 변형을 lower_bound 전수 테스트(길이 0~50, min-1..max+1 모든 타깃)에 넣는다. 배열 읽기는 범위 검사 함수 `rd()` 를 거쳐 범위 밖이면 **읽지 않고** OOB 로 세고, 반복 200회를 넘으면 HANG 으로 센다 — 그래서 버그판도 UB 없이 끝까지 돈다. "distinct-only" 는 3 간격 배열만, "with dups" 는 거기에 무작위 중복·전부 같음 배열을 더한 것.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
```

```text
variant                       | distinct-only: q / wrong / OOB / hang | with dups: q / wrong / OOB / hang
V0 half-open (correct)        |  3828 /     0 /    0 /    0          |  5312 /     0 /    0 /    0
V1 size_t hi=n-1 inclusive    |  3828 /     0 /  103 /    0          |  5312 /     0 /  309 /    0
V2 lo = mid                   |  3828 /     0 /    0 / 3725          |  5312 /     0 /    0 / 5003
V3 hi=n + while(lo<=hi)       |  3828 /     0 /   53 / 3775          |  5312 /     0 /  159 / 5153
V4 any-match as first         |  3828 /     0 /    0 /    0          |  5312 /   248 /    0 /    0
```

**1. `mid = (lo + hi) / 2`.** n ≤ 50 테스트로는 절대 안 보인다 (위 표에 넣지도 않았다). 같은 demo 의 두 번째 부분: 인덱스가 `uint32_t` 인 가상 배열 `a(i) = i`, n = 4,000,000,000 (32비트 MCU 에서 바이트 주소나 ms 시간 축을 이분할 때의 크기), x = 3,000,000,000.

```text
  lo + (hi-lo)/2 : result=3000000000 steps=31
    step 1: lo=         0 hi=4000000000 mid=2000000000
    step 2: lo=2000000001 hi=4000000000 mid= 852516352  <-- mid < lo !
    step 3: lo= 852516353 hi=4000000000 mid= 278774528  <-- mid < lo !
    step 4: lo= 278774529 hi=4000000000 mid=2139387264
  (lo + hi)/2    : result=4294967295 steps=201 (HANG: step cap hit)
```

`2000000001 + 4000000000` 이 2^32 를 넘어 감싸지고(부호 없는 연산은 mod 2^N 으로 정의됨, C11 6.2.5p9) mid 가 lo 보다 **작아져** lo 가 뒤로 간다 → 무한 루프. `int` 인덱스면 더 나쁘다: 부호 있는 오버플로는 정의되지 않은 동작(UB, C11 6.5p5)이다. UBSan 빌드로만 돌린 실제 출력:

```text
demo_bugs.c:124:23: runtime error: signed integer overflow: 1073741824 + 2147483647 cannot be represented in type 'int'
    step 2: lo= 1073741824 hi= 2147483647 mid= -536870912
lb_int_buggy(INT_MAX, 2000000000) = -1, steps=201 (step cap hit)
```

(UBSan 은 보고 후 계속 실행하므로 이 실행에서는 음수 mid 가 나왔다. UB 이므로 -O2 일반 빌드에서 무엇이 일어날지는 약속되지 않는다 — 그래서 일반 빌드로는 돌리지 않았다.) 고침: `mid = lo + (hi - lo) / 2`. 부호 없는 두 값의 평균을 넘침 없이 구하는 다른 방법은 `(lo & hi) + ((lo ^ hi) >> 1)`.

**2. `size_t hi = n - 1` 닫힌구간 (V1).** n == 0 이면 `hi = SIZE_MAX`, 그리고 `a[0] >= x` 일 때 `hi = mid - 1 = 0 - 1` 도 `SIZE_MAX` → 루프가 계속되며 `a[SIZE_MAX/2]` 를 읽는다. 표의 OOB 103 = n = 0 의 타깃 3개 + n ≥ 1 마다 `x ≤ a[0]` 인 타깃 2개 × 50. **틀린 답은 0** 이라는 점이 무섭다: 범위 검사 없는 실제 코드에서는 크래시나 쓰레기 값 읽기로 나타난다. 고침: 반개구간으로 바꾸거나, 닫힌구간을 고집한다면 인덱스를 부호 있는 `ptrdiff_t` 로 + n == 0 먼저 처리.

**3. `lo = mid` (V2).** 모름 구역이 1칸(`hi = lo + 1`)이면 `mid = lo` 이고, `a[mid] < x` 일 때 `lo = mid` 는 아무것도 안 바꾼다 → 영원히 반복. 거의 모든 쿼리(3725/3828)가 HANG. 불변식으로 보면 "mid 가 `< x` 인데 모름 구역에 남겨 둔" 것이다. 고침: `lo = mid + 1`. (닫힌구간 + `lo = mid` 를 쓰는 변형은 mid 를 올림 `lo + (hi - lo + 1) / 2` 으로 해야 한다 — 그 짝을 외우기보다 반개구간 하나만 쓰는 게 안전하다.)

**4. 반개구간 초기값 + 닫힌구간 조건 (V3).** `hi = n` 인데 `while (lo <= hi)` → `lo == hi == n` 일 때 `a[n]` 을 읽는다 (OOB 53 = x 가 최댓값보다 큰 경우), `lo == hi < n` 이면 `hi = mid` 가 안 줄어서 HANG. 고침: 한 가지 규약만. `[lo, hi)` ↔ `while (lo < hi)` ↔ `hi = mid` 가 한 세트.

**5. "같으면 바로 return" 을 첫 등장으로 씀 (V4).** 값이 다른 배열에서는 **0 오류** — 중복 없는 테스트만 돌렸다면 통과했을 버그다. 중복을 넣자 248개 오답. LeetCode 34 류 문제에서 가장 흔한 실수이고, 이 레슨의 전수 테스트에 중복 패턴을 넣은 이유다. 고침: `lower_bound` 결과를 확인한다.

**6. 회전 배열에서 `a[hi]` 를 씀.** 닫힌구간 코드(LeetCode 풀이에 흔함)를 반개구간 루프에 옮기면서 `x <= a[hi]` 로 쓰면 `a[n]` 을 읽는다. 반개구간의 오른쪽 끝 원소는 `a[hi - 1]`.

## 8. 면접 Q&A

<details><summary>Q1. `mid = (lo + hi) / 2` 가 왜 문제죠? 64비트에서도요?</summary>

`lo + hi` 가 인덱스 타입 범위를 넘을 수 있다. `int` 면 UB(C11 6.5p5), `uint32_t`/32비트 `size_t` 면 mod 2^32 로 감싸져 mid < lo 가 되고 무한 루프(7. 실수 1 실측). Java `Arrays.binarySearch` 가 이 버그를 오래 갖고 있었다는 게 Joshua Bloch 의 2006년 글로 유명해졌다. 64비트 `size_t` 에서 메모리 배열 인덱스로는 사실상 안 넘치지만, Cortex-M 의 `size_t` 는 32비트이고, 배열이 아닌 **값 범위**(주소 공간, 시간, ADC 코드)를 이분할 때는 범위가 타입 끝까지 간다. 그래서 습관적으로 `lo + (hi - lo) / 2`. 꼬리: "`>> 1` 로 쓰면 빨라지나?" → 부호 없는 `/ 2` 는 이미 시프트로 컴파일된다 (Q5 asm). 부호 있는 값의 `/ 2` 는 0 쪽 반올림(C11 6.5.5p6)이라 음수 보정 명령이 하나 더 붙는다 — 인덱스를 부호 없게 두는 이유 하나.

</details>

<details><summary>Q2. 닫힌구간 [lo, hi] 로 짜면 안 되나요? 왜 반개구간을 고집하죠?</summary>

닫힌구간도 맞게 짤 수 있다. 다만 (1) 빈 구간이 `hi = lo - 1` 이라 `size_t` 에서 underflow 위험(실수 2), (2) 답의 범위 0..n 중 "n(없음)" 을 따로 변수 `ans` 로 들고 다녀야 하고, (3) `lo = mid` / `hi = mid - 1` / 올림 mid 의 짝을 상황마다 맞춰야 한다. 반개구간은 빈 구간이 `lo == hi`, 답이 곧 `lo`, 갱신식이 항상 `mid + 1` / `mid` 두 개다. C++ STL 의 반복자 범위 `[first, last)`, Dijkstra 의 "왜 번호는 0부터" 메모(EWD831)가 같은 이유로 반개구간을 쓴다. 꼬리: "반개구간에서 마지막 등장은?" → `upper_bound(x) - 1` (단, `upper == lower` 면 없음).

</details>

<details><summary>Q3. C 표준 라이브러리 `bsearch` 를 쓰면 되지 않나요?</summary>

`bsearch` 는 (1) 같은 키가 여럿이면 **어느 것**을 돌려줄지 정해져 있지 않고(C11 7.22.5.1), (2) 없으면 NULL 뿐이라 삽입 위치·개수·"T 이후 첫 레코드" 를 못 준다, (3) 비교를 함수 포인터로 매번 호출한다(인라인 안 됨). 즉 경계 문제에는 맞지 않는다. 펌웨어에서는 newlib/newlib-nano 의 구현 크기·스택 사용도 확인해야 하고, 결국 10줄짜리 `lower_bound` 를 직접 두는 경우가 많다. 꼬리: "C++ 라면?" → `std::lower_bound` / `std::upper_bound` / `std::equal_range` (반개구간, 정확히 이 레슨의 규약).

</details>

<details><summary>Q4. 회전 배열에 중복이 있으면요? O(log n) 으로 할 수 있나요?</summary>

최악에는 못 한다. `a[lo] == a[mid] == a[hi-1]` 이면 끊김(그리고 x)이 어느 쪽에 있는지 정보가 없다. 적대자 논증: 모두 1 인 배열과 한 칸만 0 인 배열은 그 칸을 읽기 전엔 구별이 안 되므로, 0 을 찾는 어떤 알고리즘도 최악 Ω(n) 칸을 읽는다. 표준 대처는 그 경우 양 끝을 하나씩 버리는 것(LeetCode 81 "Search in Rotated Sorted Array II", 154 "Find Minimum in Rotated Sorted Array II") — 중복이 적으면 거의 로그, 최악은 선형. 연습 3 실측: n = 50 적대적 입력에서 26회 탐침 (값 다를 때 상한 6회). 꼬리: "회전점에서 `a[mid] == a[hi-1]` 이면?" → `hi--` 로 하나만 버린다 (마지막 값과 같은 값이 남아 있으니 최솟값 자체는 잃지 않는다).

</details>

<details><summary>Q5. 나눗셈기 없는 MCU, 분기 예측, 캐시 — 하드웨어 쪽에서 뭐가 달라지죠?</summary>

나눗셈은 없다: `(hi - lo) / 2` (부호 없음)는 시프트다. 이 맥에서 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` 로 본 lower_bound 루프 본문 (컴파일러가 4번 펼침, 한 덩어리):

```text
subs   r3, r1, r0              @ hi - lo
add.w  lr, r0, r3, lsr #1      @ mid = lo + (hi-lo)>>1
ldr.w  r3, [r12, lr, lsl #2]   @ a[mid]
cmp    r3, r2
ite    ge
movge  r1, lr                  @ hi = mid
addlt.w r0, lr, #1             @ lo = mid + 1
```

갱신은 IT 블록 조건 실행이라 본문에 분기가 없다 (arm64 도 `csel`/`csinc`). 부호 있는 `int` 판 `lo + (hi-lo)/2` 는 `add.w r1, r1, r1, lsr #31` 으로 음수 보정 명령이 하나 더 붙었다. 남는 분기는 루프 탈출 하나이고, 반복 횟수는 데이터에 따라 1회 차이 난다 (n = 50 에서 5~6회, n = 2^k - 1 이면 고정 — 이 레슨 작업 중 측정). 반복 횟수까지 고정하고 싶으면(타이밍 결정성) 연습 2 의 길이 반감형이 ⌈log₂ n⌉ 회 고정. 캐시: 큰 배열에서 이진 탐색의 처음 몇 단계는 멀리 떨어진 줄을 건드려 캐시 미스가 난다 — 대안으로 Eytzinger(BFS 순서) 배치가 알려져 있다(측정은 안 함). Cortex-M 의 작은 SRAM 테이블에서는 대개 무관하고, 플래시 대기 상태가 더 중요하다.

</details>

<details><summary>Q6. 부팅할 때 첫 지워진 페이지를 이진 탐색으로 찾는 게 위험하진 않나요?</summary>

전제는 단조성(기록 페이지가 앞에 연속)이다. 깨지는 경우: (1) 전원 차단으로 쓰다 만 페이지 — 헤더에 0 비트가 생겼으면 "기록됨" 이라 단조성은 유지, 내용은 CRC/시퀀스로 검증, (2) NAND 배드 블록·건너뛴 페이지 — 블록 단위로 배드 블록 테이블을 먼저 적용, (3) 지운 페이지의 비트 반전 — "== 0xFF" 대신 0 비트 수 임계값으로 판정(ECC 엔진이 erased-page 검출을 따로 지원하는 컨트롤러도 있다). 방어: 찾은 k 에 대해 `k-1` 기록·`k` 지움을 다시 확인하고, 불일치면 선형 스캔으로 떨어지는 경로를 둔다. 꼬리: "헤더를 언제 쓰나?" → 데이터를 다 쓴 뒤 마지막에 커밋 마커(헤더/상태 워드)를 쓰면 "헤더 있음 = 완전히 기록됨" 이 되어 판정이 단순해진다. LeetCode 278 "First Bad Version" 이 같은 모양의 문제다.

</details>

<details><summary>Q7. 로그 타임스탬프가 32비트 ms 틱이면 무슨 문제가 있죠?</summary>

2^32 ms ≈ 49.7 일마다 0 으로 돌아간다. 로그에 한 바퀴를 넘는 구간이 있으면 ts 열은 정렬이 아니라 **회전된 정렬**이 된다 (Part B 모양). 대처: (1) 단조 증가 시퀀스 번호를 키로 쓰고 ts 는 표시용, (2) 64비트 시간, (3) 한 바퀴 안이라는 보장이 있으면 기준점 대비 차이 `(uint32_t)(ts - base)` 로 비교(직렬 번호 산술, RFC 1982 와 같은 생각). 원형(ring) 로그 영역은 물리 배열 자체가 회전이라 가장 오래된 레코드를 회전점으로 찾고 논리 인덱스로 이분한다 — 연습 5.

</details>

<details><summary>Q8. 재귀로 짜면 안 되나요? 스택은요?</summary>

깊이가 ⌈log₂(n+1)⌉ 이라 n = 10^9 도 30 프레임 — 터지진 않는다. 그래도 반복판을 쓴다: 스택 사용이 0 이고, MISRA 류 규칙(재귀 금지, C-A10)과 정적 스택 분석 도구가 재귀를 싫어한다. 이진 탐색의 재귀는 꼬리 호출이라 컴파일러가 루프로 바꿔 줄 수도 있지만 그것은 보장이 아니다. 꼬리: "ISR 에서 불러도 되나?" → 순수 함수(읽기만, 전역 상태 없음)라 재진입 가능. 단 배열을 다른 문맥이 동시에 고치고 있으면 정렬 불변식이 깨진 상태를 볼 수 있다 — 테이블 교체는 포인터 원자적 교체(더블 버퍼)로.

</details>

## 9. 추가 연습

### 연습 1 — 첫 위치와 끝 위치 (LeetCode 34)

`search_range(a, n, x, &first, &last)`: x 의 첫/끝 인덱스, 없으면 둘 다 -1. 예: `[5,7,7,8,8,10]`, x = 8 → (3, 4). 길이 0~50 무작위 중복 배열 전수 대조. (LeetCode 35 "Search Insert Position" 은 그냥 `lower_bound` 다.)

<details><summary>답 보기</summary>

`l = lower_bound(x)`, `u = upper_bound(x)`. `l == u` 면 없음, 아니면 `(l, u - 1)`. 이진 탐색 두 번, O(log n). "첫 위치를 찾고 오른쪽으로 선형 확장" 은 x 가 n 개면 O(n) 이 된다 — 면접관이 "전부 같은 값이면?" 으로 바로 찌르는 지점.

</details>

### 연습 2 — 반복 횟수가 고정된 lower_bound

데이터와 무관하게 ⌈log₂ n⌉ 회 반복하는 lower_bound 를 짜라 (타이밍이 입력에 따라 흔들리면 안 되는 제어 루프·보안 코드용). 모든 x 에 대해 반복 횟수가 같은지 테스트한다.

<details><summary>답 보기</summary>

`base` 포인터와 남은 길이 `len` 을 둔다. 불변식: 답은 `[base, base + len]` 안. `half = len / 2` 에서 `base[half] < x` 면 `base += half`, 그리고 항상 `len -= half` (len 이 ⌈len/2⌉ 로). `len == 1` 이 되면 답은 `base + (*base < x)`. 반복 횟수는 len 만으로 정해진다. 이 맥 arm64 `-O2` 에서는 갱신이 `csel` 한 줄로 나왔고, Cortex-M4 에서는 `it lt; addlt.w` 로 나왔다 (직접 asm 확인). 다만 Q5 처럼 일반판도 본문에는 분기가 없었다 — 이 버전의 실익은 **반복 횟수 고정**이다. 읽기는 ⌈log₂ n⌉ + 1 회 (마지막 확인 한 번).

</details>

### 연습 3 — 중복이 있는 회전 배열 (LeetCode 81)

중복을 허용하는 회전 배열에서 x 가 있는지(0/1) 판단하고 탐침 수를 세라. 길이 0~30 무작위 중복 배열의 모든 회전으로 전수 대조, 그리고 n = 50 적대적 입력(전부 1, 한 칸만 0)의 최악 탐침 수를 출력.

<details><summary>답 보기</summary>

`a[lo] == a[mid] == a[hi-1]` (그리고 `a[mid] != x`) 면 `lo++, hi--` — 버리는 두 값은 `a[mid]` 와 같으니 x 가 아니다. 그 외에는 본문 `rot_search` 와 같다 (세 값이 다 같지 않으면 `a[lo] <= a[mid]` 일 때 왼쪽이 정렬돼 있음이 보장된다: 왼쪽에 끊김이 있다면 오른쪽 전체가 `a[lo]` 이하이면서 `a[mid]` 이상이어야 해서 `a[hi-1] == a[mid]` 가 되어 모순). 실제 출력:

```text
연습 3: 3629 queries, mismatches=0; n=50 adversarial worst probes=26, all-equal absent probes=25
```

값이 다를 때 상한 6회 → 26회. 한 번에 두 칸씩 버리니 약 n/2 — 최악 O(n) 이 실측으로 보인다.

</details>

### 연습 4 — 두 단계 회전 탐색 (LeetCode 153 + lower_bound)

`rot_min_index` 로 회전점 k 를 찾은 뒤 `lower_bound` 를 한 번만 불러 x 를 찾아라. 본문 Part B 와 같은 전수 대조.

<details><summary>답 보기</summary>

`[k, n)` 과 `[0, k)` 는 각각 정렬. 작은 블록 `[k, n)` 의 최댓값이 `a[n-1]` 이므로 `x <= a[n-1]` 이면 `[k, n)`, 아니면 `[0, k)` 에서 찾는다 (k = 0 이면 두 번째 구간이 비어 자연히 -1). 부분 배열 탐색은 `lo + lower_bound(a + lo, hi - lo, x)`. 총 2 × O(log n). 검증된 부품 두 개를 이어 붙여서 off-by-one 을 낼 곳이 거의 없다.

</details>

### 연습 5 — 원형 플래시 로그

가득 찬 원형 로그 `ring[n]` 의 레코드는 `{seq, ts_ms}`. seq 는 논리 순서로 증가(서로 다름), 가장 오래된 레코드의 물리 위치는 모른다. (a) 가장 오래된 위치, (b) ts ≥ T 인 첫 레코드의 **논리** 인덱스(0 = 가장 오래된 것)를 각각 O(log n) 에. 길이 0~40, 모든 시작 위치로 대조.

<details><summary>답 보기</summary>

(a) seq 의 물리 배열은 회전 정렬(값 다름) → `rot_min_index` 그대로 (`seq[i] <= seq[n-1]` 이 처음 참). (b) 논리 인덱스 i 의 물리 위치는 `old + i` 에서 n 이상이면 n 을 뺀 값 — `%` 대신 비교+뺄셈 (Cortex-M0 처럼 나눗셈기가 없는 코어에서 `%` 는 라이브러리 호출이 된다). 이 위에서 Part A 의 lower_bound. seq 가 32비트로 한 바퀴 돌 수 있다면 `seq[i] <= seq[n-1]` 비교가 깨지므로 직렬 번호 산술(`(int32_t)(a - b) < 0`, 구현 정의 변환 — C11 6.3.1.3p3)이나 64비트 seq 를 쓴다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 13/13, 보고 없음
```

## 10. 핵심 정리

- 불변식부터: `[0,lo)` 는 "아니다", `[hi,n)` 은 "그렇다", `[lo,hi)` 는 모름. 시작 `lo=0, hi=n`, 끝 `lo==hi` = 답(0..n).
- 세트로 외운다: `while (lo < hi)` · `mid = lo + (hi - lo) / 2` · `lo = mid + 1` / `hi = mid`. 섞으면 OOB 나 무한 루프(실측 V1~V3).
- lower = 첫 `>= x`, upper = 첫 `> x`, 개수 = upper - lower, 첫 등장 = lower 확인, 마지막 = upper - 1.
- 모든 문제를 "F…F T…T 술어의 첫 T" 로 바꾼다: 회전점(`a[i] <= a[n-1]`), 첫 지워진 페이지, ts ≥ T.
- 비교 ⌈log₂(n+1)⌉ 회 = 정보 이론적 하한. 플래시 1000 레코드 → 읽기 10회.
- 회전 탐색은 값이 다를 때만 O(log n). 중복이면 최악 Ω(n) — 적대자 논증, 실측 n=50 에서 26 탐침.
- 테스트는 중복을 꼭 넣는다: "같으면 return" 버그는 중복 없는 전수 테스트에서 0 오류였다.
- 펌웨어: 인덱스는 부호 없게, `(lo+hi)/2` 금지(32비트 `size_t`), 단조성 전제(쓰다 만 페이지·비트 반전·틱 wrap)를 말로 확인.

## 참고자료

- AlgoMonster 노트 M02 §1~§5 (Binary Search Intro, First True, Monotonic Function, First Element Not Smaller Than Target, First Occurrence), §7 (Minimum in Rotated Sorted Array) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M02_binary_search.md`
- J. Bloch, "Extra, Extra — Read All About It: Nearly All Binary Searches and Mergesorts are Broken", Google Research Blog, 2006-06 — `(low + high) / 2` 오버플로 (URL 이 블로그 이전으로 바뀌었으니 제목으로 검색).
- J. Bentley, *Programming Pearls* (2nd ed.), Column 4 "Writing Correct Programs" — 이진 탐색을 불변식으로 증명하며 짜기.
- D. E. Knuth, *The Art of Computer Programming* Vol. 3, §6.2.1 "Searching an Ordered Table" — 이진 탐색과 비교 횟수 분석.
- E. W. Dijkstra, "Why numbering should start at zero" (EWD831), 1982 — 반개구간 `a ≤ i < b` 의 근거: <https://www.cs.utexas.edu/users/EWD/transcriptions/EWD08xx/EWD831.html>
- ISO/IEC 9899:2011 (C11) — 6.2.5p9 (부호 없는 산술 wrap), 6.5p5 (부호 있는 오버플로 UB), 6.5.5p6 (정수 나눗셈 0 쪽 버림), 7.22.5.1 (`bsearch`: 같은 키 중 어느 것인지 미지정).
- cppreference — `std::lower_bound`, `std::upper_bound`, `std::equal_range`: <https://en.cppreference.com/w/cpp/algorithm/lower_bound>
- LeetCode 34 Find First and Last Position of Element in Sorted Array · 35 Search Insert Position · 33 Search in Rotated Sorted Array · 81 Search in Rotated Sorted Array II · 153 Find Minimum in Rotated Sorted Array · 154 Find Minimum in Rotated Sorted Array II · 278 First Bad Version.
- 관련 레슨: ALGO-B03 (보정 테이블: 이진 탐색 + 보간), ALGO-B04 (답을 이분 탐색), C-B02 (고정 폭 정수·정수 승격), C-M12 (UB 카탈로그 — 부호 있는 오버플로), C-A10 (MISRA 스타일: 재귀 금지), DS-B13 (이진 탐색 트리 — 같은 '절반 버리기' 를 포인터 구조로).

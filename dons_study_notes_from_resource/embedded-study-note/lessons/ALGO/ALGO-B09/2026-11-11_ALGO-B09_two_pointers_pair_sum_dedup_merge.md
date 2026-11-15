---
id: ALGO-B09
time: 60분
updated: 2026-11-11
---
# ALGO-B09 투 포인터: 쌍 합·중복 제거·병합

## 학습 목표

- 투 포인터(two pointers)의 세 모양 — **마주 보기**(쌍 합), **같은 방향 읽기/쓰기**(중복 제거), **두 배열 병합** — 을 보고 바로 고를 수 있다.
- 각 모양의 **불변식(invariant)** 을 한 줄로 말하고, "작은 쪽 포인터를 옮겨도 답을 놓치지 않는 이유"를 증명할 수 있다.
- `int32` 값의 합을 `int64` 로 계산해야 하는 이유(부호 있는 overflow = UB)를 실제 실패 사례로 설명한다.
- "최대 2개 남기기", "뒤에서 채우는 병합", 3-sum 중복 건너뛰기 같은 **경계 변형**을 off-by-one 없이 짠다.
- 두 CAN 버스 타임스탬프 스트림 병합, I2C 스캔 목록 합집합·교집합처럼 펌웨어 상황에 같은 패턴을 쓴다.

## 1. 문제

투 포인터는 코딩 면접에서 "O(n²) 브루트포스 → O(n)" 으로 가는 가장 흔한 첫 단계이고, 추가 메모리 O(1) 이라 malloc 없는 펌웨어에서도 그대로 쓸 수 있다 — 그래서 임베디드 면접관이 특히 좋아한다.

정렬된 배열에서는 인덱스 두 개만 있으면 많은 문제가 한 번 훑기로 끝난다. 핵심은 매 걸음마다 **"이 원소는 더 볼 필요가 없다"** 를 증명하고 버리는 것이다. 한 걸음에 원소 하나를 버리니 전체가 O(n) 이다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">투 포인터의 세 가지 모양</text>
  <text x="10" y="48" fill="currentColor">① 마주 보기 (opposite direction) — 정렬 배열에서 합이 t 인 쌍</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="58" width="40" height="30"/><rect x="64" y="58" width="40" height="30"/><rect x="108" y="58" width="40" height="30"/>
    <rect x="152" y="58" width="40" height="30"/><rect x="196" y="58" width="40" height="30"/><rect x="240" y="58" width="40" height="30"/>
    <rect x="284" y="58" width="40" height="30"/>
  </g>
  <rect x="20" y="58" width="40" height="30" class="f-acc"/><rect x="284" y="58" width="40" height="30" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="40" y="78">-3</text><text x="84" y="78">1</text><text x="128" y="78">2</text><text x="172" y="78">4</text>
    <text x="216" y="78">5</text><text x="260" y="78">7</text><text x="304" y="78">10</text>
    <text x="40" y="106">lo→</text><text x="304" y="106">←hi</text>
  </g>
  <g fill="currentColor">
    <text x="350" y="70">합 &lt; t → lo++ (a[lo] 는 남은 누구와도 너무 작다)</text>
    <text x="350" y="90">합 &gt; t → hi-- (a[hi] 는 남은 누구와도 너무 크다)</text>
    <text x="350" y="110">한 걸음에 원소 하나를 버린다 → 최대 n-1 걸음</text>
  </g>
  <text x="10" y="138" fill="currentColor">② 같은 방향 (read / write) — 정렬 배열 중복 제거, 처리 도중 모습</text>
  <rect x="20" y="148" width="128" height="30" class="f-ok"/>
  <rect x="152" y="148" width="128" height="30" class="f-mut"/>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="148" width="40" height="30"/><rect x="64" y="148" width="40" height="30"/><rect x="108" y="148" width="40" height="30"/>
    <rect x="152" y="148" width="40" height="30"/><rect x="196" y="148" width="40" height="30"/><rect x="240" y="148" width="40" height="30"/>
    <rect x="284" y="148" width="40" height="30"/><rect x="328" y="148" width="40" height="30"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="40" y="168">0</text><text x="84" y="168">1</text><text x="128" y="168">2</text><text x="172" y="168">1</text>
    <text x="216" y="168">1</text><text x="260" y="168">2</text><text x="304" y="168">3</text><text x="348" y="168">3</text>
    <text x="172" y="196">↑w</text><text x="304" y="196">↑r</text>
  </g>
  <g fill="currentColor">
    <text x="390" y="160">[0, w) 확정: 서로 다른 값, 오름차순</text>
    <text x="390" y="180">[w, r) 이미 읽음 → 덮어써도 된다</text>
    <text x="390" y="200">[r, n) 아직 안 읽음 (원래 값 그대로)</text>
  </g>
  <text x="10" y="228" fill="currentColor">③ 두 배열 병합 (merge) — 두 머리 중 작은 쪽을 꺼낸다, 같으면 A 먼저</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="240" width="40" height="30"/><rect x="64" y="240" width="40" height="30"/><rect x="108" y="240" width="40" height="30"/>
    <rect x="180" y="240" width="40" height="30"/><rect x="224" y="240" width="40" height="30"/><rect x="268" y="240" width="40" height="30"/><rect x="312" y="240" width="40" height="30"/>
    <rect x="400" y="240" width="40" height="30" class="f-ok"/><rect x="444" y="240" width="40" height="30" class="f-ok"/><rect x="488" y="240" width="40" height="30"/>
    <rect x="532" y="240" width="40" height="30"/><rect x="576" y="240" width="40" height="30"/><rect x="620" y="240" width="40" height="30"/><rect x="664" y="240" width="40" height="30"/>
  </g>
  <rect x="20" y="240" width="40" height="30" class="f-mut"/><rect x="180" y="240" width="40" height="30" class="f-mut"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="40" y="260">1</text><text x="84" y="260">3</text><text x="128" y="260">5</text>
    <text x="200" y="260">2</text><text x="244" y="260">3</text><text x="288" y="260">4</text><text x="332" y="260">9</text>
    <text x="420" y="260">1</text><text x="464" y="260">2</text>
    <text x="84" y="288">↑i</text><text x="244" y="288">↑j</text><text x="508" y="288">↑k</text>
    <text x="84" y="310">A</text><text x="266" y="310">B</text><text x="552" y="310">out</text>
  </g>
</svg>
```

모든 값은 `int32_t`, 합은 **반드시 `int64_t`** 로 계산한다. `INT32_MAX + 1` 처럼 `int` 범위를 넘는 부호 있는 덧셈은 정의되지 않은 동작(UB, C11 6.5p5)이고, 실제로 "없는 쌍을 찾았다"는 거짓 답을 낸다 (7. 실수 1 에서 실측).

### Part A — 핵심 세 가지

- `pair_sum_sorted(a, n, target, &i, &j)` — 정렬 배열에서 `a[i] + a[j] == target`, `i < j` 인 쌍. 여러 개면 **i 가 가장 작은 쌍, 그중 j 가 가장 큰 쌍** 을 돌려준다 (마주 보기 투 포인터가 자연스럽게 찾는 쌍이 바로 이것임을 5. 풀이에서 증명한다). 있으면 1, 없으면 0.
- `dedup_sorted(a, n)` — 제자리(in-place) 중복 제거. 새 길이 k 를 돌려주고 `a[0..k)` 는 서로 다른 값 오름차순.
- `merge_sorted(a, na, b, nb, out)` — 두 정렬 배열을 별도 버퍼 `out` 으로 병합. 같은 값이면 a 쪽이 먼저 (안정, stable).

### Part B — 까다로운 변형과 경계

- `dedup_keep_k(a, n, k)` — 각 값을 **최대 k 개** 까지 남긴다 (k=2 가 유명한 변형). 원래 위치를 읽으면 이미 덮어쓴 칸을 읽게 되는 함정이 있다.
- `merge_into_a(a, na, b, nb)` — `a` 는 길이 `na + nb` 이고 앞 `na` 칸만 유효. 별도 버퍼 없이 **뒤에서부터** 채워 병합한다. `size_t` 인덱스가 0 아래로 내려가지 않게.
- `three_sum_zero(a, n, out, max_out)` — 합이 0 인 **서로 다른 값-세쌍** 을 모두 (x ≤ y ≤ z, 사전순). 정렬 + 투 포인터, 중복은 정확히 한 번만. 개수가 `max_out` 을 넘으면 앞부분만 쓰고 전체 개수를 돌려준다 (`snprintf` 식).

### Part C — 펌웨어: 두 CAN 버스 타임라인과 I2C 스캔

- `merge_events(a, na, b, nb, out, cap)` — CAN1(IMU)과 CAN2(휠 엔코더)에서 각각 시간순으로 받은 프레임 `{ts_us, can_id, seq, bus}` 를 하나의 타임라인으로. 같은 ts 면 CAN1 먼저, 버스 안 순서 유지. 출력 버퍼는 `cap` 칸 고정 → **가장 이른 cap 개만** 쓴다.
- `scan_union` / `scan_intersect` — I2C 버스를 두 번 스캔한 응답 주소 목록(각각 정렬, 재시도로 중복 기록 있음)에서 "한 번이라도 보인 장치"와 "두 번 다 보인 장치"를 중복 없이. 차이가 **불안정한 장치**(접촉 불량, 전원 순서 문제)다.

## 2. Problem (English)

> **Two pointers on sorted arrays.** All values are `int32_t`; any sum must be computed without signed overflow. Use O(1) extra memory unless an output buffer is given. No dynamic allocation.
>
> 1. `int pair_sum_sorted(const int32_t *a, size_t n, int32_t target, size_t *i, size_t *j);`
>    `a` is sorted non-decreasing. Find `i < j` with `a[i] + a[j] == target`. If several pairs exist, return the one with the smallest `i`, and among those the largest `j`. Return 1 if found, else 0. O(n) time.
>    Example: `a = [2,3,4,5,8,11,18], target = 8` → `(1, 3)`. `a = [1,1,5,5], target = 6` → `(0, 3)`. `a = [2147483646, 2147483647], target = -3` → not found.
> 2. `size_t dedup_sorted(int32_t *a, size_t n);` — remove duplicates in place; return the new length `k`; `a[0..k)` holds the distinct values in order.
> 3. `size_t dedup_keep_k(int32_t *a, size_t n, size_t k);` — keep each value at most `k` times (`k >= 1`). Example: `[1,1,1,2,2,3], k = 2` → `5`, `[1,1,2,2,3]`.
> 4. `void merge_sorted(const int32_t *a, size_t na, const int32_t *b, size_t nb, int32_t *out);` — stable merge (ties: `a` first).
>    `void merge_into_a(int32_t *a, size_t na, const int32_t *b, size_t nb);` — `a` has capacity `na + nb`; merge `b` into it in place. Example: `a = [1,2,3,_,_,_], b = [2,5,6]` → `[1,2,2,3,5,6]`.
> 5. `size_t three_sum_zero(int32_t *a, size_t n, int32_t (*out)[3], size_t max_out);` — return all **unique** triplets of values `x <= y <= z` with `x + y + z == 0`, in lexicographic order. You may sort `a` in place. Example: `[-1,0,1,2,-1,-4]` → `[[-1,-1,2], [-1,0,1]]`. O(n²) time.
> 6. Two CAN controllers deliver frames, each stream sorted by timestamp. Merge them into one timeline into a fixed buffer of `cap` entries (keep the earliest `cap`). Ties: CAN1 first, per-bus order preserved. Then, given two sorted (possibly duplicated) I2C scan results, return their union and intersection without duplicates.
>
> Constraints: `0 <= n <= 10^5`. Follow-ups: what if the input is not sorted? what if the streams arrive live from two ISRs?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데(Part A 3 · B 3 · C 3). 비교 함수 `cmp_i32`, `sort_i32`, 이벤트 구조체, 브루트포스 참조 구현(O(n²) 쌍 합, run 길이 기반 keep-k, O(n³) + 정규화한 3-sum, `(ts, bus, seq)` 정렬, 256칸 비트맵 합·교집합)과 고정 시드 xorshift32 랜덤 테스트는 주어진다. 스텁은 "못 찾음 / 그대로 / 0개"를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:187  pair_sum_sorted(x, 7, 8, &i, &j) == 1 && i == 1 && j == 3
…
pair_sum   : 25000 random cases vs O(n^2), mismatches=13291
…
timeline   :
FAIL starter.c:365  k == 8 && eo[0].ts_us == 0 && eo[2].ts_us == 2000 && eo[2].bus == 0 && eo[3].bus == 1 && eo[7].ts_us == 5000
…
14/46 passed
```

(exit 1. 통과한 14개는 "없음"이 정답인 경계 사례 — 빈 배열, 원소 하나, overflow 로 생기는 가짜 쌍 등.)

## 4. 힌트

<details>
<summary>힌트 1 — 쌍 합: 양 끝에서 시작</summary>

`lo = 0, hi = n - 1`. 합이 작으면 키워야 하는데 `a[hi]` 는 이미 남은 것 중 최대 → `a[lo]` 를 버린다. 합이 크면 반대. `while (lo < hi)` — 같은 원소를 두 번 쓰면 안 된다. 합은 `(int64_t)a[lo] + a[hi]`.

</details>

<details>
<summary>힌트 2 — 중복 제거: 쓰기 포인터는 "마지막으로 쓴 값"과 비교</summary>

`w` = 지금까지 확정한 개수. 새 원소 `a[r]` 가 `a[w-1]` 과 다르면 `a[w++] = a[r]`. 최대 k 개 버전은 `a[w-k]` 와 비교한다 — **쓴 쪽**이다. 원래 위치 `a[r-k]` 는 이미 덮어써졌을 수 있다.

</details>

<details>
<summary>힌트 3 — 뒤에서 채우는 병합: "남은 개수"로 센다</summary>

`i = na, j = nb, k = na + nb` 를 "남은 개수"로 두고 `a[i-1]` 과 `b[j-1]` 중 큰 것을 `a[--k]` 에. `size_t i = na - 1; while (i >= 0)` 은 항상 참이라 무한 루프 + 범위 밖 접근이다. b 가 바닥나면 a 의 나머지는 이미 제자리.

</details>

<details>
<summary>힌트 4 — 3-sum: x 를 고정하면 나머지는 쌍 합</summary>

정렬 후 `x = a[i]` 를 고정하고 `(i, n)` 구간에서 합 `-x` 인 쌍을 투 포인터로 모두 찾는다. 같은 x 는 **앞의 것과** 비교해 건너뛰고(`a[i] == a[i-1]`), 쌍을 찾은 뒤에는 lo, hi 를 같은 값이 끝날 때까지 민다.

</details>

<details>
<summary>힌트 5 — CAN 병합: 비교 연산자 하나가 안정성을 정한다</summary>

`a[i].ts_us <= b[j].ts_us` 이면 a 를 꺼낸다 (`<` 면 같은 ts 에서 CAN2 가 먼저 나온다). 루프 조건에 `k < cap` 을 넣으면 "가장 이른 cap 개"가 저절로 된다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 내용 (쌍 합 기준) |
|---|---|
| ① 확인 질문 | 정렬돼 있나? 중복 있나? 답이 여러 개면 어느 것? 인덱스 vs 값? 값 범위(합 overflow)? 빈 배열은? |
| ② 브루트포스 | 모든 쌍 O(n²) 시간, O(1) 메모리. "정렬 조건을 아직 안 썼다" 고 말한다 |
| ③ 더 나은 아이디어 + 불변식 | 양 끝 포인터. 불변식 "답이 있으면 [lo, hi] 안에 있다" + 버리는 이유 한 줄 |
| ④ 코드 | `lo < hi`, int64 합, 세 갈래 비교 |
| ⑤ 소리 내어 말할 테스트 | n=0, n=1, 답 없음, 같은 값 쌍 `[3,3]`, 여러 답, `INT32_MIN/MAX` 섞인 합 |
| ⑥ 복잡도 | 시간 O(n) (걸음마다 구간이 1 줄어든다), 추가 메모리 O(1), 재귀 없음 → 스택 상수 |

다른 대안도 한 줄: 정렬 안 된 입력이면 해시 집합 O(n) 시간 + O(n) 메모리(DS-B10), 또는 정렬 O(n log n) + 투 포인터(원래 인덱스가 필요하면 인덱스 배열을 정렬).

### Part A — 쌍 합: 왜 버려도 답을 놓치지 않나

예: `a = [-3, 1, 2, 4, 5, 7, 10]`, `t = 9`. 답이 두 개다: (2,5) = 2+7, (3,4) = 4+5.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">pair_sum_sorted(a, 7, t = 9) — 회색 = 버린 원소, 강조 = lo / hi</text>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="80" y="44">0</text><text x="124" y="44">1</text><text x="168" y="44">2</text><text x="212" y="44">3</text>
    <text x="256" y="44">4</text><text x="300" y="44">5</text><text x="344" y="44">6</text>
  </g>
  <rect x="60" y="52" width="40" height="28" class="f-acc"/><rect x="324" y="52" width="40" height="28" class="f-acc"/>
  <rect x="60" y="97" width="40" height="28" class="f-mut"/><rect x="104" y="97" width="40" height="28" class="f-acc"/><rect x="324" y="97" width="40" height="28" class="f-acc"/>
  <rect x="60" y="142" width="40" height="28" class="f-mut"/><rect x="104" y="142" width="40" height="28" class="f-acc"/><rect x="280" y="142" width="40" height="28" class="f-acc"/><rect x="324" y="142" width="40" height="28" class="f-mut"/>
  <rect x="60" y="187" width="40" height="28" class="f-mut"/><rect x="104" y="187" width="40" height="28" class="f-mut"/><rect x="148" y="187" width="40" height="28" class="f-ok"/><rect x="280" y="187" width="40" height="28" class="f-ok"/><rect x="324" y="187" width="40" height="28" class="f-mut"/>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="52" width="304" height="28"/><rect x="60" y="97" width="304" height="28"/>
    <rect x="60" y="142" width="304" height="28"/><rect x="60" y="187" width="304" height="28"/>
    <path d="M104 52 V80 M148 52 V80 M192 52 V80 M236 52 V80 M280 52 V80 M324 52 V80"/>
    <path d="M104 97 V125 M148 97 V125 M192 97 V125 M236 97 V125 M280 97 V125 M324 97 V125"/>
    <path d="M104 142 V170 M148 142 V170 M192 142 V170 M236 142 V170 M280 142 V170 M324 142 V170"/>
    <path d="M104 187 V215 M148 187 V215 M192 187 V215 M236 187 V215 M280 187 V215 M324 187 V215"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="80" y="71">-3</text><text x="124" y="71">1</text><text x="168" y="71">2</text><text x="212" y="71">4</text><text x="256" y="71">5</text><text x="300" y="71">7</text><text x="344" y="71">10</text>
    <text x="80" y="116">-3</text><text x="124" y="116">1</text><text x="168" y="116">2</text><text x="212" y="116">4</text><text x="256" y="116">5</text><text x="300" y="116">7</text><text x="344" y="116">10</text>
    <text x="80" y="161">-3</text><text x="124" y="161">1</text><text x="168" y="161">2</text><text x="212" y="161">4</text><text x="256" y="161">5</text><text x="300" y="161">7</text><text x="344" y="161">10</text>
    <text x="80" y="206">-3</text><text x="124" y="206">1</text><text x="168" y="206">2</text><text x="212" y="206">4</text><text x="256" y="206">5</text><text x="300" y="206">7</text><text x="344" y="206">10</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="71">1</text><text x="10" y="116">2</text><text x="10" y="161">3</text><text x="10" y="206">4</text>
    <text x="385" y="71">-3 + 10 = 7 &lt; 9 → lo++  (-3 은 누구와도 부족)</text>
    <text x="385" y="116">1 + 10 = 11 &gt; 9 → hi--  (10 은 누구와도 초과)</text>
    <text x="385" y="161">1 + 7 = 8 &lt; 9 → lo++</text>
    <text x="385" y="206">2 + 7 = 9 → (2, 5) 반환</text>
    <text x="10" y="240">(3, 4) = 4 + 5 도 답이지만, i 가 더 작은 (2, 5) 를 먼저 만난다 — 아래 증명.</text>
  </g>
</svg>
```

**버리는 이유 (한 걸음짜리 논증).** 지금 남은 구간이 `[lo, hi]` 라 하자.

- `a[lo] + a[hi] < t` 이면, 구간 안 어떤 `j ≤ hi` 에 대해서도 `a[lo] + a[j] ≤ a[lo] + a[hi] < t` (정렬). 즉 `a[lo]` 는 남은 누구와도 짝이 될 수 없다 → 버려도 된다.
- `a[lo] + a[hi] > t` 이면 대칭으로 `a[hi]` 는 누구와도 짝이 될 수 없다.

그래서 불변식 **"답이 있으면 그중 하나는 [lo, hi] 안에 있다"** 가 유지되고, 구간이 매번 1 줄어드니 최대 n−1 걸음에 끝난다.

**어느 쌍을 찾나 (더 강한 불변식).** 답 중 i 가 가장 작은 것을 i\*, 그 i\* 와 짝이 되는 가장 큰 j 를 J 라 하자. 주장: 반환 전까지 항상 `lo ≤ i*` 이고 `J ≤ hi`.

- `lo++` 는 `a[lo] + a[hi] < t` 일 때만 일어난다. 만약 `lo == i*` 였다면 `a[i*] + a[hi] ≥ a[i*] + a[J] = t` (hi ≥ J, 정렬) — 모순. 그러니 `lo < i*` 였고, 늘어난 뒤에도 `lo ≤ i*`.
- `hi--` 는 합 > t 일 때만. 만약 `hi == J` 였다면 `a[lo] + a[J] ≤ a[i*] + a[J] = t` (lo ≤ i*) — 모순.
- 반환은 `a[lo] + a[hi] = t` 일 때. `lo < i*` 면 i\* 의 최소성에 모순, `hi > J` 면 J 의 최대성에 모순 → 반환값은 정확히 `(i*, J)`.

그래서 "i 최소, 그중 j 최대" 라는 규약은 알고리즘이 **원래** 찾는 쌍이다. 하네스의 브루트포스도 이 규약으로 쌍을 고르고(`for i 오름차순, for j 내림차순`), 25000개 랜덤에서 인덱스까지 일치한다 (그중 13291개가 답이 있는 경우).

**overflow.** `a[lo] + a[hi]` 를 `int32_t` 로 계산하면 `2147483646 + 2147483647` 이 UB 이고, 이 맥에서는 −3 으로 감겨 "target −3 인 쌍 있음" 을 돌려준다 (7. 실수 1). `(int64_t)a[lo] + a[hi]` — 한쪽만 캐스트하면 다른 쪽은 보통 산술 변환으로 int64 가 된다. Cortex-M4 에서 이 64비트 덧셈+비교는 `adds` / `adc.w` / `subs` / `sbcs.w` 몇 개로 끝난다 (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` 로 확인) — 32비트 MCU 에서도 비용은 미미하다.

### Part A — 중복 제거: 읽기 포인터 r, 쓰기 포인터 w

불변식: **`a[0..w)` 는 원래 `a[0..r)` 의 서로 다른 값 목록(오름차순)이고, `w ≤ r`.** `w ≤ r` 이므로 `a[r]` 은 아직 덮어쓰이지 않은 원래 값이다. 정렬돼 있으니 `a[r]` 이 새 값인지는 **마지막으로 쓴 값** `a[w-1]` 하나만 보면 안다.

| r | a[r] | a[w-1] | 동작 | w 후 | a[0..w) |
|---|---|---|---|---|---|
| — | — | — | 시작 (a[0] 확정) | 1 | 0 |
| 1 | 0 | 0 | 같음 → 건너뜀 | 1 | 0 |
| 2 | 1 | 0 | 다름 → a[1]=1 | 2 | 0 1 |
| 3 | 1 | 1 | 건너뜀 | 2 | 0 1 |
| 4 | 1 | 1 | 건너뜀 | 2 | 0 1 |
| 5 | 2 | 1 | a[2]=2 | 3 | 0 1 2 |
| 6 | 3 | 2 | a[3]=3 | 4 | 0 1 2 3 |
| 7 | 3 | 3 | 건너뜀 | 4 | 0 1 2 3 |

(입력 `[0,0,1,1,1,2,3,3]`, 위 그림 ②가 r=6 직전 모습.) 시간 O(n), 추가 메모리 O(1). `a[w..n)` 에는 쓰레기가 남는다 — 호출자는 반환값 k 까지만 본다. `n == 0` 은 `w = 1` 로 시작하면 안 되므로 먼저 돌려준다.

### Part A — 두 배열 병합

`while (i < na && j < nb) out[k++] = (b[j] < a[i]) ? b[j++] : a[i++];` 다음, 남은 쪽을 통째로 복사. 불변식: `out[0..k)` 는 `a[0..i) ∪ b[0..j)` 를 정렬한 것이고, 그 모든 값 ≤ `min(a[i], b[j])`. 조건을 `b[j] < a[i]` (엄격) 로 써서 같은 값이면 a 를 먼저 꺼낸다 = **안정**. int 배열에선 보이지 않지만 Part C 의 레코드에서는 결과가 달라진다. 시간 O(na+nb), 추가 메모리는 out 버퍼 O(na+nb). 머지소트의 merge 단계 그대로다 (ALGO-B07).

### Part B — 최대 k 개 남기기

불변식은 중복 제거와 같고 "각 값 최대 k 개"만 붙는다. 새 원소 x = `a[r]` 은 지금까지 쓴 모든 값 이상이다(정렬). 그러니 x 의 사본은 쓴 영역의 **꼬리**에 모여 있고, "x 가 이미 k 개 있나" ⟺ `w ≥ k && a[w-k] == x`.

```c
if (w < k || a[r] != a[w - k]) a[w++] = a[r];
```

`[1,1,1,2,2,3], k=2`: r=0,1 → w<k 라 그냥 씀(w=2). r=2: a[0]=1 == 1 → 건너뜀. r=3: a[1]=1 ≠ 2 → a[2]=2 (w=3). r=4: a[1]=1 ≠ 2 → a[3]=2 (w=4). r=5: a[2]=2 ≠ 3 → a[4]=3 (w=5). 결과 `[1,1,2,2,3]`.

함정: "원래 배열에서 k 칸 앞과 같으면 k+1 번째" 라는 논리 자체는 맞다. 그런데 `a[r-k]` 를 읽으면 그 칸은 **이미 덮어쓴** 칸일 수 있다 (r=4 에서 a[2] 는 원래 1 이었는데 r=3 에서 2 로 바뀜 → 두 번째 2 를 잘못 버림). 실측 6000개 중 1635개 불일치 (7. 실수 3).

### Part B — 뒤에서 채우는 병합

`a` 뒤쪽 nb 칸이 비어 있다. 앞에서 채우면 아직 안 읽은 a 원소를 덮어쓴다. **뒤에서**, 큰 것부터 채우면 덮어쓸 일이 없다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">merge_into_a(a = [1,2,3,·,·,·], na = 3, b = [2,5,6]) — i, j, k = "남은 개수"</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="40" width="264" height="28"/><rect x="60" y="85" width="264" height="28"/><rect x="60" y="130" width="264" height="28"/>
    <rect x="60" y="175" width="264" height="28"/><rect x="60" y="220" width="264" height="28"/>
    <path d="M104 40 V68 M148 40 V68 M192 40 V68 M236 40 V68 M280 40 V68"/>
    <path d="M104 85 V113 M148 85 V113 M192 85 V113 M236 85 V113 M280 85 V113"/>
    <path d="M104 130 V158 M148 130 V158 M192 130 V158 M236 130 V158 M280 130 V158"/>
    <path d="M104 175 V203 M148 175 V203 M192 175 V203 M236 175 V203 M280 175 V203"/>
    <path d="M104 220 V248 M148 220 V248 M192 220 V248 M236 220 V248 M280 220 V248"/>
  </g>
  <rect x="280" y="85" width="44" height="28" class="f-acc"/>
  <rect x="236" y="130" width="44" height="28" class="f-acc"/><rect x="280" y="130" width="44" height="28" class="f-ok"/>
  <rect x="148" y="175" width="44" height="28" class="f-mut"/><rect x="192" y="175" width="44" height="28" class="f-acc"/><rect x="236" y="175" width="88" height="28" class="f-ok"/>
  <rect x="148" y="220" width="44" height="28" class="f-acc"/><rect x="192" y="220" width="132" height="28" class="f-ok"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="82" y="59">1</text><text x="126" y="59">2</text><text x="170" y="59">3</text><text x="214" y="59">·</text><text x="258" y="59">·</text><text x="302" y="59">·</text>
    <text x="82" y="104">1</text><text x="126" y="104">2</text><text x="170" y="104">3</text><text x="214" y="104">·</text><text x="258" y="104">·</text><text x="302" y="104">6</text>
    <text x="82" y="149">1</text><text x="126" y="149">2</text><text x="170" y="149">3</text><text x="214" y="149">·</text><text x="258" y="149">5</text><text x="302" y="149">6</text>
    <text x="82" y="194">1</text><text x="126" y="194">2</text><text x="170" y="194">3</text><text x="214" y="194">3</text><text x="258" y="194">5</text><text x="302" y="194">6</text>
    <text x="82" y="239">1</text><text x="126" y="239">2</text><text x="170" y="239">2</text><text x="214" y="239">3</text><text x="258" y="239">5</text><text x="302" y="239">6</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="59">시작</text><text x="10" y="104">1</text><text x="10" y="149">2</text><text x="10" y="194">3</text><text x="10" y="239">4</text>
    <text x="345" y="59">i=3 j=3 k=6</text>
    <text x="345" y="104">a[2]=3 vs b[2]=6 → b. a[5]=6 (j=2, k=5)</text>
    <text x="345" y="149">3 vs b[1]=5 → b. a[4]=5 (j=1, k=4)</text>
    <text x="345" y="194">3 vs b[0]=2 → a. a[3]=3 (i=2, k=3) 회색=옛 값</text>
    <text x="345" y="239">a[1]=2 vs 2 → 같으면 b. a[2]=2 (j=0) → 끝</text>
    <text x="10" y="275">불변식 k = i + j → 쓰는 칸 k−1 = i + j − 1 ≥ i (j ≥ 1 동안) → 안 읽은 a[0..i) 를 절대 덮지 않는다.</text>
  </g>
</svg>
```

- **불변식:** `k = i + j` (남은 칸 수 = 남은 원소 수), 그리고 `a[k..na+nb)` 는 이미 확정된 가장 큰 값들, 정렬. 쓰는 칸 `k-1` 은 `j ≥ 1` 인 동안 `≥ i` 이므로 아직 안 읽은 `a[0..i)` 와 겹치지 않는다.
- **끝 조건은 `j > 0` 하나.** b 가 바닥나면 `k == i` 이고 `a[0..i)` 는 처음부터 제자리에 있다. a 가 먼저 바닥나면(`i == 0`) b 를 계속 복사.
- **같은 값이면 b 를 뒤로.** 조건이 `a[i-1] > b[j-1]` (엄격) 이라 같으면 b 가 뒤 칸에 들어가고, a 의 같은 값은 앞에 남는다 → 원래 a 원소가 앞 = 안정. (`>=` 로 바꿔도 int 테스트는 46/46 통과 — 실측. 값만 있는 배열에선 안정성이 안 보인다. 레코드면 달라진다.)
- 인덱스를 "남은 개수"로 둔 이유: `size_t` 로 `i = na - 1` 에서 시작하면 na=0 에서 이미 `SIZE_MAX`, 그리고 `i >= 0` 은 항상 참이다 (7. 실수 4).

추가 메모리 O(1) — 펌웨어에서 큰 로그 버퍼 두 개를 하나로 합칠 때 두 번째 버퍼를 따로 잡지 않는 방법이다. FTL 에서 정렬된 매핑 변경 목록을 기존 정렬 테이블 끝에 붙인 뒤 합치는 것과 같은 모양이다.

### Part B — 3-sum: x 를 고정하고 쌍 합

정렬 후 `x = a[i]` 를 고정하면 남은 문제는 "`(i, n)` 구간에서 합이 `-x` 인 쌍을 **모두**" 찾기 = Part A 쌍 합의 "모두 찾기" 판이다. 중복 규칙 세 개:

1. **같은 x 는 한 번만.** `i > 0 && a[i] == a[i-1]` 이면 건너뛴다. 같은 값의 **첫 번째** 사본에서 처리해야 그 뒤 사본들을 y, z 로 쓸 수 있다 (`[-1,-1,2]`). 다음 칸과 비교하면(`a[i] == a[i+1]`) 첫 사본을 버리게 되어 `[-1,-1,2]` 를 놓친다 — 실측 4500개 중 915개 불일치.
2. **쌍을 찾으면 lo, hi 를 같은 값이 끝날 때까지 민다.** x 가 고정이면 y 가 같을 때 z 도 같다 (z = −x−y). 다음 서로 다른 세쌍은 y 가 더 커야 한다.
3. **찾지 못한 걸음에서는 건너뛰기 불필요.** 같은 값을 하나씩 지나가도 결과는 같다 (O(n) 걸음 안에 들어간다).

`[-1, 0, 1, 2, -1, -4]` → 정렬 `[-4, -1, -1, 0, 1, 2]`:

| i | x | need = −x | (lo, hi) → 합 | 동작 |
|---|---|---|---|---|
| 0 | −4 | 4 | (1,5) 1, (2,5) 1, (3,5) 2, (4,5) 3 | 모두 < 4 → lo++ … lo == hi 종료 |
| 1 | −1 | 1 | (2,5) −1+2 = 1 | **기록 [−1,−1,2]**, lo=3, hi=4 |
| | | | (3,4) 0+1 = 1 | **기록 [−1,0,1]**, lo=4, hi=3 종료 |
| 2 | −1 | — | — | a[2] == a[1] → 건너뜀 |
| 3 | 0 | 0 | (4,5) 1+2 = 3 | > 0 → hi-- → 종료 |

`i + 2 < n` 까지만 돌고, `x > 0` 이면 나머지도 전부 양수라 `break`. `need = -(int64_t)a[i]` — `-INT32_MIN` 은 int32 로는 표현이 안 되므로 반드시 넓힌 뒤 부호를 바꾼다.

복잡도: 정렬 O(n log n) + 바깥 n × 안쪽 O(n) = **O(n²)** 시간, 추가 메모리 O(1) (qsort 내부 스택 제외), 출력 제외. 브루트포스 O(n³). 검증은 O(n³) 로 모든 세쌍을 모아 **각 세쌍을 정렬(정규화) → 세쌍 목록 정렬 → 중복 제거** 한 집합과 비교한다. 작은 값 [−12, 12] 2500개(세쌍 20078개) + 극단값(`INT32_MIN`, `INT32_MAX`, ±2^30 …) 2000개(세쌍 1815개) 전부 일치.

### Part C — 두 CAN 버스를 하나의 타임라인으로

장면: 로봇의 데이터 기록기(flight/black-box recorder). CAN1 에 IMU(ID 0x101, 1 ms 주기), CAN2 에 휠 엔코더(0x201/0x202, 2.5 ms 주기). 두 컨트롤러가 같은 타이머로 수신 타임스탬프를 찍어 각 FIFO 를 비우면 버스별로는 시간순이다. 분석 도구는 하나의 시간순 로그를 원한다 → merge.

```c
while (k < cap && (i < na || j < nb)) {
    if (j >= nb || (i < na && a[i].ts_us <= b[j].ts_us)) out[k++] = a[i++];
    else                                                 out[k++] = b[j++];
}
```

- `<=` 하나가 규칙 "같은 ts 면 CAN1 먼저"를 만든다. 버스 안 순서는 원래부터 유지된다 (한 스트림에서 앞 원소가 항상 먼저 나오니까). 즉 결과 = `(ts, bus, seq)` 로 정렬한 것과 같다 — 하네스가 바로 그걸로 대조한다(4000쌍, 스트림 안에 같은 ts 도 섞음, 불일치 0).
- `k < cap` 이 루프 조건에 있으니 버퍼가 차면 **가장 이른 cap 개** 에서 멈춘다. 남은 것은 `i`, `j` 부터 다음 배치에서 이어 가면 된다 (실무에선 i, j 를 돌려줘 소비한 개수를 알린다).
- 실제 출력: 2000 µs 에 두 버스가 동시에 받은 프레임이 CAN1 → CAN2 순서다.

```text
timeline   : 0/CAN2:201 1000/CAN1:101 2000/CAN1:101 2000/CAN2:202 2500/CAN2:201 3000/CAN1:101 4000/CAN1:101 5000/CAN2:202
```

`<` 로 쓰면 같은 ts 에서 CAN2 가 먼저 나온다. 실측: 위 장면 검사 2개 FAIL + 랜덤 4000쌍 중 2488개 불일치 (43/46). 로그 재생기가 "IMU 를 먼저 적용한 뒤 엔코더" 같은 순서 가정을 하면 이런 차이가 재현 안 되는 버그가 된다.

### Part C — I2C 스캔: 합집합과 교집합

7비트 주소 0x08~0x77 을 훑어 ACK 한 주소를 기록한다(0x00~0x07, 0x78~0x7F 는 예약). 두 번 스캔한 목록은 각각 정렬돼 있고, 재시도가 로그에 두 번 남으면 중복도 있다.

- **합집합(union):** 병합하면서 `out[k-1]` 과 같으면 안 쓴다 = merge + dedup 을 한 번에.
- **교집합(intersect):** 작은 쪽을 전진(그 값은 다른 목록에 없다 — 정렬이니까), 같으면 기록하고 둘 다 전진. 중복은 마찬가지로 `out[k-1]` 비교.

```text
i2c scan   : seen=0x1E,0x48,0x50,0x68,0x76  stable=0x1E,0x48,0x68  flaky=0x50,0x76
```

(예: 0x1E 지자기, 0x48 ADC, 0x50 EEPROM, 0x68 IMU, 0x76 기압 센서 — 흔한 기본 주소.) `flaky = seen − stable` 도 같은 두 포인터 차집합으로 하네스가 계산한다. 1회차에만 있는 0x50 은 "부팅 중 전원이 늦게 올라온 EEPROM" 을, 2회차에만 있는 0x76 은 "늦게 깨어난 센서" 를 의심한다. 주소가 0~127 뿐이면 128비트 비트맵(DS-B08)이 더 단순하다 — 투 포인터가 이기는 곳은 키 공간이 큰 경우(CAN 29비트 ID, 32비트 시리얼 번호)다. 하네스의 참조 구현이 바로 그 비트맵(256칸)이다.

### 복잡도 정리

| 함수 | 시간 | 추가 메모리 | 비고 |
|---|---|---|---|
| `pair_sum_sorted` | O(n) | O(1) | 걸음마다 구간 −1 |
| `dedup_sorted`, `dedup_keep_k` | O(n) | O(1) | 읽기 n번, 쓰기 ≤ n번 |
| `merge_sorted`, `merge_events`, `scan_*` | O(na+nb) | 출력 버퍼 | 비교 ≤ na+nb−1 |
| `merge_into_a` | O(na+nb) | O(1) | b 가 끝나면 즉시 종료 |
| `three_sum_zero` | O(n²) | O(1) + 출력 | 정렬 O(n log n) 포함 |

모두 반복문만 쓰므로 스택 깊이는 상수다 (`three_sum_zero` 의 `qsort` 내부 제외 — 펌웨어에선 ALGO-B05 삽입 정렬이나 ALGO-B06 의 깊이 제한 퀵소트로 바꾼다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 46/46 passed, 0.15 s
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 46/46, 보고 없음
```

실제 출력:

```text
pair_sum   : 25000 random cases vs O(n^2), mismatches=0
dedup      : 5000 random cases vs reference, mismatches=0
merge      : 5000 random cases vs sort(concat), mismatches=0
keep_k     : 6000 random cases (k=1..3) vs run-length reference, mismatches=0
merge_into : 5000 random cases vs sort(concat), mismatches=0
three_sum  : 4500 random cases vs O(n^3) canonical set, mismatches=0
timeline   : 0/CAN2:201 1000/CAN1:101 2000/CAN1:101 2000/CAN2:202 2500/CAN2:201 3000/CAN1:101 4000/CAN1:101 5000/CAN2:202
merge_ev   : 4000 random stream pairs vs sort by (ts, bus, seq), mismatches=0
i2c scan   : seen=0x1E,0x48,0x50,0x68,0x76  stable=0x1E,0x48,0x68  flaky=0x50,0x76
scan       : 5000 random list pairs x {union, intersect} vs bitmap, mismatches=0
46/46 passed
```

## 7. 자주 하는 실수

아래 "실측"은 정답 코드에서 그 줄만 바꿔 같은 하네스로 돌린 결과다 (이 맥, Apple clang 21).

**1. 합을 int32 로 계산한다.** `int32_t s = a[lo] + a[hi];` → `-O2` 에서 42/46: `[2147483646, 2147483647]` 에서 target −3 인 쌍을 "찾고"(감긴 값 −3), `[1, 2, INT32_MAX]` 에서 `2 + INT32_MAX` 가 음수로 보여 포인터가 반대로 움직이는 바람에 진짜 답 (0,1) 까지 놓친다. 랜덤 전 범위 5000개 중 129개 불일치. UBSan 은 바로 잡는다:

```text
runtime error: signed integer overflow: 2147483646 + 2147483647 cannot be represented in type 'int32_t' (aka 'int')
```

더 고약한 것: 3-sum 안쪽 합을 같은 식으로 int32 로 바꾸면 **`-O2` 에선 46/46 통과**, `-O0` 에선 44/46 (극단값 407개 불일치). `-O2` 어셈블리를 보면 루프 안에서 `ldrsw x1 / ldrsw x2 / add x1, x2, x1` — 두 값을 64비트로 부호 확장해 더하고 있었다. overflow 가 없다고 가정해도 되니(UB) 컴파일러가 넓게 계산한 것이고, 그래서 우연히 맞았다. 테스트가 통과해도 UB 는 UB — 고친 코드: `int64_t s = (int64_t)a[lo] + a[hi];`

**2. `while (lo <= hi)`.** 같은 원소를 두 번 쓴다 (`[1,3,9]`, t=6 → (1,1)). 게다가 `lo == hi == 0` 에서 `hi--` 하면 `size_t` 가 `SIZE_MAX` 로 감겨 범위 밖을 읽는다. 실측: `-O2` 실행이 SIGSEGV (exit 139), ASan 은 `stack-buffer-overflow`. → `lo < hi`.

**3. keep-k 에서 원래 위치 `a[r-k]` 와 비교.**

```c
if (r < k || a[r] != a[r - k]) a[w++] = a[r];   /* 틀림: a[r-k] 는 이미 덮어썼을 수 있다 */
```

논리("k 칸 앞 원본과 같으면 k+1 번째")는 맞지만 그 칸이 이미 다른 값으로 바뀌었다. 실측 44/46, 6000개 중 1635개 불일치, `[1,1,1,2,2,3]` → `[1,1,2,3]`. → 쓴 쪽 `a[w-k]` 와 비교.

**4. 뒤에서 채우는 병합을 `size_t` 로 `i = na - 1 … while (i >= 0)`.** `i >= 0` 은 항상 참. 실측: `-O2` 실행 SIGSEGV (exit 139), ASan `global-buffer-overflow`. 그리고 **`-Wall -Wextra` 로는 경고가 안 나온다** (이 맥 clang 에서 확인). `-Wtype-limits` 를 켜야 `result of comparison of unsigned expression >= 0 is always true [-Wtautological-unsigned-zero-compare]` 가 나온다. → "남은 개수" `i = na` 로 두고 `a[i-1]`, 또는 부호 있는 `ptrdiff_t`.

**5. 3-sum 에서 x 중복을 다음 칸과 비교 / 아예 안 건너뜀 / 안쪽을 안 건너뜀.**

| 변형 | 결과 (실측) | 증상 |
|---|---|---|
| `if (a[i] == a[i+1]) continue;` | 42/46, 915/4500 불일치 | `[-1,-1,2]`, `[0,0,0]` 누락 |
| x 건너뛰기 없음 | 42/46, 1710/4500 | `[-1,0,1]` 이 두 번 |
| 찾은 뒤 lo/hi 건너뛰기 없음 | 44/46, 994/4500 | `[0,0,0,0,0]` → `[0,0,0]` 여러 번 |

**6. 병합에서 `<` 와 `<=` 를 아무렇게나.** int 배열에선 안 보이지만 레코드에선 안정성이 깨진다 (Part C 실측 2488/4000 불일치). 규칙을 먼저 정하고("같으면 A 먼저") 그에 맞는 연산자를 고른다: 앞에서 채우면 `a <= b` 면 a, 뒤에서 채우면 `a > b` 일 때만 a.

**7. 출력 개수 카운터를 32비트로.** 연습 1 처럼 "쌍의 개수"는 n=10^5 에서 C(10^5, 2) = 4,999,950,000 > 2^32. Cortex-M 의 `size_t` 는 32비트다 → `uint64_t`.

## 8. 면접 Q&A

<details><summary>Q1. 합이 작을 때 lo 를 옮기면 답을 놓치지 않는다는 걸 증명해 보세요.</summary>

남은 구간 `[lo, hi]` 에서 `a[lo] + a[hi] < t` 이면, 구간의 모든 j 에 대해 `a[lo] + a[j] ≤ a[lo] + a[hi] < t` (정렬). `a[lo]` 가 들어간 답은 구간 안에 없으므로 버려도 "답이 있으면 [lo, hi] 안에 있다" 가 유지된다. 대칭으로 hi. 구간이 매번 1 줄고, 길이 1 이 되면 쌍이 없다 → 답이 있었다면 그 전에 반환했다. 꼬리: "어느 답을 돌려주나?" → i 최소, 그중 j 최대 — 5. 풀이의 강한 불변식(`lo ≤ i*`, `J ≤ hi`). 꼬리 2: "모든 쌍을 원하면?" → 찾은 뒤 멈추지 않고 같은 값 묶음을 건너뛰며 계속(3-sum 안쪽 루프, 연습 1).

</details>

<details><summary>Q2. 입력이 정렬돼 있지 않으면? 메모리가 4 KB 뿐이라면?</summary>

선택지 둘. (a) 해시 집합: 한 번 훑으며 `t - x` 를 찾음, O(n) 기대 시간, O(n) 메모리, 최악은 해시 충돌에 달림(DS-B10/B11). (b) 정렬 + 투 포인터: O(n log n) 시간, 제자리 정렬이면 O(1)~O(log n) 추가 메모리 — 단 원래 인덱스가 필요하면 인덱스 배열(또는 (값, 인덱스) 쌍)을 정렬해야 하므로 O(n). RAM 이 빠듯하고 최악 시간이 결정적이어야 하는 펌웨어면 (b) 에 힙소트/깊이 제한 퀵소트(ALGO-B06)가 안전하다. 값 범위가 작으면(12비트 ADC) 4096비트 = 512 B 비트맵으로 O(n) 도 가능하다 (DS-B08).

</details>

<details><summary>Q3. 왜 int64 로 더하나요? 64비트가 비싼 MCU 라면?</summary>

`int32 + int32` 는 `int`(32비트)로 계산되고, 범위를 넘으면 부호 있는 overflow = UB (C11 6.5p5). 실제로 거짓 매치(−3)와 포인터 역주행이 나온다 (7. 실수 1). Cortex-M4 에서 64비트 덧셈은 `adds` + `adc.w`, 비교는 `subs` + `sbcs.w` 로 몇 명령이다 (이 레슨에서 `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2` 어셈블리로 확인). 피하고 싶다면 값 범위를 문서화해 합이 안 넘음을 보장하거나(예: 값이 ±2^30 이내), `a[lo] < t - a[hi]` 처럼 바꿔 쓸 수도 있지만 `t - a[hi]` 도 넘칠 수 있어 경우를 나눠야 한다 — 64비트가 가장 단순하고 맞다. 8/16비트 MCU(AVR 등)는 `int` 가 16비트라 int16 두 개 합에서 같은 문제가 더 일찍 온다 (C-B02 정수 승격).

</details>

<details><summary>Q4. 3-sum 보다 빠르게 할 수 있나요? 4-sum 은?</summary>

정렬 + 투 포인터 O(n²) 이 면접 정답이다. 해시로도 O(n²) (메모리 O(n)). 3SUM 은 오랫동안 "본질적으로 O(n²) 근처"로 여겨진 문제(3SUM 가설)이고, n² 을 로그 계열 인자만큼 줄인 이론적 알고리즘은 알려져 있지만 실용적이지 않다. k-sum 은 바깥 k−2 겹 고정 + 투 포인터로 O(n^{k−1}), 각 층에서 같은 값 건너뛰기를 똑같이 한다 (4-sum: O(n³)). 꼬리: "개수만 세면?" → 같은 값 묶음 곱셈으로 O(n²) (연습 1 의 방법).

</details>

<details><summary>Q5. CAN 스트림이 두 ISR 에서 실시간으로 들어오면 어떻게 병합하나요?</summary>

배열 병합은 두 스트림이 끝까지 있다는 가정이다. 실시간에선 각 ISR 이 자기 링 버퍼(DS-B03, SPSC 면 DS-A01)에 넣고, 기록 태스크가 두 머리를 비교한다. 함정: CAN2 큐가 **비어 있을 때** CAN1 머리를 내보내면, 나중에 도착한 더 이른 CAN2 프레임이 순서를 깬다. 그래서 "다른 쪽 큐가 비었으면 그 버스의 마지막 관측 시각(워터마크)이 내 ts 이상일 때만 내보낸다", 혹은 "최대 지연 D 가 지나면 내보낸다" 같은 규칙이 필요하다 — 지연과 정확성의 거래. 같은 ts 규칙(CAN1 먼저)은 그대로. ISR 안에서 병합하지 않는 이유: ISR 은 짧게, 결정적으로.

</details>

<details><summary>Q6. 타임스탬프가 32비트 µs 카운터라 감기면(wrap-around) 비교가 어떻게 되나요?</summary>

2^32 µs ≈ 4295 s ≈ 71.6 분마다 0 으로 돌아온다. `a.ts <= b.ts` 는 감긴 직후 틀린다. 해법: 차이를 부호 있게 본다 — `(int32_t)(a.ts - b.ts) <= 0`. 부호 없는 뺄셈은 mod 2^32 로 정의되고, 그 결과를 int32 로 바꾸는 것은 범위를 넘으면 구현 정의(C11 6.3.1.3p3)지만 GCC/Clang 은 2의 보수로 잘라낸다. 조건: 비교하는 두 시각의 실제 차이가 2^31 µs(약 35.8 분) 미만. Linux 의 `time_after()` 매크로가 같은 방식이다. 또는 소프트웨어로 상위 32비트를 붙여 64비트 타임스탬프로 확장한다.

</details>

<details><summary>Q7. 병합의 안정성은 언제 중요한가요? 뒤에서 채울 때는?</summary>

키가 같은 레코드의 상대 순서가 의미를 가질 때: 같은 tick 의 이벤트 순서, 다중 키 정렬(먼저 2차 키로 정렬 후 1차 키로 안정 정렬 — ALGO-B07, ALGO-B08 의 LSD 기수 정렬이 의존하는 성질). 앞에서 채우면 "같으면 A" = `a <= b` 면 A. 뒤에서 채우면 큰 것부터 놓으니 반대로 "같으면 B 를 뒤로" = `a > b` 일 때만 A. 두 연산자를 바꿔 쓰는 실수가 흔하다 — int 테스트로는 안 보이니 (값, 원래 위치) 레코드로 테스트한다.

</details>

<details><summary>Q8. 중복 제거를 플래시나 DMA 버퍼에 직접 해도 되나요?</summary>

알고리즘은 쓰기 포인터가 읽기 포인터를 절대 앞지르지 않으니(`w ≤ r`) 같은 버퍼 제자리에서 안전하다. 그런데 NOR/NAND 플래시는 "덮어쓰기"가 안 된다(지운 뒤 1→0 만) — 제자리 압축은 RAM 사본에서 하고 결과를 새 페이지에 쓴다. FTL 의 GC 가 유효 페이지만 새 블록으로 옮기는 것이 바로 "읽기 포인터 / 쓰기 포인터" 압축이다. DMA 가 아직 쓰고 있는 버퍼라면 r 이 DMA 쓰기 위치를 넘지 않게 해야 하고, D-cache 가 있는 코어(Cortex-M7)면 읽기 전 invalidate 가 필요하다 (ARCH-M12).

</details>

## 9. 추가 연습

### 연습 1 — 합이 t 인 쌍의 **개수** (중복 많은 배열)

정렬 배열에서 `a[i] + a[j] == t`, `i < j` 인 인덱스 쌍의 개수를 O(n) 에. `[1,1,2,3,3,3,4,5,5]`, t=6 → 8.

<details><summary>답 보기</summary>

찾았을 때 lo, hi 를 하나씩 옮기면 같은 값 묶음 사이의 곱을 놓친다. 두 경우로 나눈다:

- `a[lo] == a[hi]`: `[lo, hi]` 전부 같은 값 → `C(m, 2)`, m = hi−lo+1, 그리고 끝.
- 다르면: 왼쪽 묶음 길이 cl, 오른쪽 묶음 길이 ch → `cl × ch` 를 더하고 두 묶음을 건너뛴다.

예: (1,5) 2×2 = 4, (2,4) 1×1 = 1, (3,3) C(3,2) = 3 → 8. 카운터는 `uint64_t` — n = 10^5 전부 0 이면 4,999,950,000 (하네스가 검사).

```c
else if (a[lo] == a[hi]) { uint64_t m = hi - lo + 1; cnt += m * (m - 1) / 2; break; }
else { /* cl, ch 세기 */ cnt += (uint64_t)cl * ch; lo += cl; hi -= ch; }
```

</details>

### 연습 2 — 합이 t 이하인 쌍의 개수 (두 패킷을 한 프레임에)

크기 정렬된 패킷 목록에서 두 개를 MTU 1500 바이트 한 프레임에 묶을 수 있는 쌍의 수. `[100,200,600,900,1400]` → 7.

<details><summary>답 보기</summary>

`a[lo] + a[hi] ≤ t` 이면 `a[lo]` 는 `(lo, hi]` 의 **모든** 원소와 OK (그 원소들은 a[hi] 이하) → `hi − lo` 개를 한꺼번에 더하고 lo++. 아니면 a[hi] 는 남은 가장 작은 a[lo] 와도 초과 → hi--. O(n). 2Sum 의 "같음"이 "이하"로 바뀌면 개수를 묶어서 셀 수 있다는 점이 핵심. 랜덤 20000개(절반은 int32 전 범위, t 는 int64) 대조.

</details>

### 연습 3 — 무효 샘플 제거 (LeetCode 27 Remove Element)

ADC 드라이버가 읽기 실패 칸에 센티넬 −32768 을 넣는다. 순서를 유지하며 제자리에서 지우고 새 길이를 돌려줘라.

<details><summary>답 보기</summary>

중복 제거와 같은 읽기/쓰기 포인터, 조건만 `a[r] != v`. 불변식 `a[0..w)` = `a[0..r)` 중 v 가 아닌 것, 원래 순서. 정렬이 필요 없다. 순서가 상관없다면 지울 칸에 맨 뒤 원소를 가져와 채우는 방식이 쓰기 횟수가 적다 (지울 것이 적을 때).

</details>

### 연습 4 — 정렬된 오프셋의 제곱 정렬 (LeetCode 977 Squares of a Sorted Array)

보정 오프셋 `[-4,-1,0,3,10]` (정렬, 부호 있음) → 제곱(에너지) `[0,1,9,16,100]` 를 O(n) 에. 출력은 `int64_t`.

<details><summary>답 보기</summary>

제곱이 가장 큰 원소는 항상 **양 끝 중 하나**다(절댓값 최대). 마주 보기 포인터로 큰 쪽을 out 의 **맨 뒤부터** 채운다 — `merge_into_a` 와 같은 "뒤에서 채우기". `(int64_t)a[i] * a[i]` — `INT32_MIN` 의 제곱은 2^62 로 int64 에 들어간다 (하네스 검사). int32 곱이면 UB.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
```

## 10. 핵심 정리

- 투 포인터 = 매 걸음 "이 원소는 더 볼 필요 없다"를 증명하고 버리기 → O(n), 추가 메모리 O(1).
- 마주 보기 불변식: 답이 있으면 [lo, hi] 안. 합 < t 면 a[lo] 는 누구와도 부족 → lo++. 결과는 "i 최소, 그중 j 최대".
- 읽기/쓰기 불변식: `a[0..w)` 확정, `w ≤ r`. 비교는 **쓴 쪽**(`a[w-1]`, keep-k 는 `a[w-k]`).
- 뒤에서 채우는 병합: `k = i + j` 라 안 읽은 칸을 안 덮는다. 끝 조건 `j > 0`. 인덱스는 "남은 개수"로 (`size_t >= 0` 은 항상 참, `-Wall -Wextra` 도 경고 안 함).
- 3-sum: 정렬 → x 고정 + 쌍 합. x 는 `a[i-1]` 과 비교해 건너뛰고, 찾은 뒤 lo/hi 같은 값 건너뛰기. O(n²).
- int32 합은 int64 로. int32 overflow 는 UB — `-O2` 에선 우연히 맞고 `-O0` 에선 틀리는 식으로 숨는다. UBSan 으로 확인.
- 안정 병합: 앞에서 `<=` 면 A, 뒤에서 `>` 일 때만 A. 레코드로 테스트해야 보인다.
- 펌웨어: 두 CAN/센서 스트림 타임라인 병합(같은 ts 규칙, 고정 버퍼 cap), 스캔 목록 합·교·차집합, 무효 샘플 제거.

## 참고자료

- LeetCode 167 Two Sum II - Input Array Is Sorted: <https://leetcode.com/problems/two-sum-ii-input-array-is-sorted/> (1-기반 인덱스, 답이 정확히 하나라고 보장 — 이 레슨은 여러 답 규약을 정했다)
- LeetCode 26 Remove Duplicates from Sorted Array: <https://leetcode.com/problems/remove-duplicates-from-sorted-array/>
- LeetCode 80 Remove Duplicates from Sorted Array II (최대 2개): <https://leetcode.com/problems/remove-duplicates-from-sorted-array-ii/>
- LeetCode 88 Merge Sorted Array (뒤에서 채우기): <https://leetcode.com/problems/merge-sorted-array/>
- LeetCode 15 3Sum: <https://leetcode.com/problems/3sum/>
- LeetCode 27 Remove Element, LeetCode 977 Squares of a Sorted Array (연습 3, 4)
- ISO/IEC 9899:2011 (C11), N1570 초안: 6.5p5 (예외 조건 = UB), 6.3.1.3p3 (부호 있는 정수로의 범위 밖 변환 = 구현 정의), 6.3.1.8 (보통 산술 변환): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Clang UndefinedBehaviorSanitizer: <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 2장 MERGE 절차 (병합의 루프 불변식 증명 형식)
- AlgoMonster 노트 M03 §1–5 (투 포인터 개요·불변식·같은 방향, Remove Duplicates), §9–10 (마주 보기, Two Sum Sorted) (Don 정리본)
- 관련 레슨: ALGO-B07 (머지소트·안정성), ALGO-B10 (슬라이딩 윈도우 — 같은 방향 투 포인터의 가변 구간판), ALGO-B05/B06 (펌웨어용 정렬), ALGO-M13 (누적합), DS-B03 (링 버퍼), DS-B08 (비트맵), DS-B10 (해시 테이블), C-B02 (정수 승격), C-M12 (UB 카탈로그)

---
id: ALGO-B01
time: 60분
updated: 2026-11-11
---
# ALGO-B01 복잡도 분석: Big-O를 실측으로 확인

## 학습 목표

- 같은 문제(중복 원소 찾기)를 **O(n²) 쌍 비교 / O(n log n) 정렬 후 이웃 비교 / O(n) 해시 집합** 으로 malloc 없이 짜고, 세 답을 서로·정의와 대조해 검증한다.
- n = 10³ ~ 10⁷ 에서 시간·메모리를 재고, **log(시간) 대 log(n) 의 최소제곱 기울기** 로 복잡도를 추정한다. 쌍 비교는 10⁵ 에서 끊고 기울기로 10⁷ 을 외삽한다.
- 측정 기울기가 이론과 왜 다른지(n log n 은 ≈1.1, 해시 테이블이 L2 밖으로 나가면 원소당 비용 상승, 작은 n 의 타이머 해상도·고정 오버헤드) 숫자로 설명한다.
- 면접에서 "입력 크기 → 허용 복잡도" 를 바로 말하고, O(n) 해시 답의 **RAM 비용**(8n~16n 바이트)을 64 KB RAM MCU 기준으로 따져 다른 답(정렬·비트맵·쌍 비교)을 고를 수 있다.

## 1. 문제

임베디드 코딩 면접에서 "더 빠른 답" 은 언제나 "메모리를 더 쓰는 답" 이다 — 시간 복잡도와 함께 **바이트 수** 를 말할 수 있어야 MCU 팀에서 통과한다.

Big-O(빅오, 점근 상한)는 "n 이 커질 때 일의 양이 어떤 꼴로 자라나" 만 말한다. 상수, 캐시, 분기 예측, 타이머는 빠져 있다. 그래서 이 레슨은 같은 질문 — **"배열에 같은 값이 두 번 나오나?"** — 을 세 가지 복잡도로 풀고, 이 맥에서 실제로 재서 로그-로그 그래프의 기울기로 지수를 거꾸로 읽는다. t ≈ c·nᵖ 이면 log t = log c + p·log n 이므로, **로그-로그 그래프의 기울기 = 지수 p** 다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b01a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">A1 쌍 비교: i &lt; j 칸 전부</text>
  <g font-size="12" fill="currentColor" text-anchor="middle"><text x="62" y="52">7</text><text x="86" y="52">3</text><text x="110" y="52">41</text><text x="134" y="52">0</text><text x="158" y="52">20</text><text x="182" y="52">3</text></g>
  <g font-size="12" fill="currentColor" text-anchor="end"><text x="44" y="74">7</text><text x="44" y="98">3</text><text x="44" y="122">41</text><text x="44" y="146">0</text><text x="44" y="170">20</text><text x="44" y="194">3</text></g>
  <g stroke="currentColor"><rect x="74" y="58" width="24" height="24" class="f-mut"/><rect x="98" y="58" width="24" height="24" class="f-mut"/><rect x="122" y="58" width="24" height="24" class="f-mut"/><rect x="146" y="58" width="24" height="24" class="f-mut"/><rect x="170" y="58" width="24" height="24" class="f-mut"/>
    <rect x="98" y="82" width="24" height="24" class="f-mut"/><rect x="122" y="82" width="24" height="24" class="f-mut"/><rect x="146" y="82" width="24" height="24" class="f-mut"/><rect x="170" y="82" width="24" height="24" class="f-warn"/>
    <rect x="122" y="106" width="24" height="24" fill="none"/><rect x="146" y="106" width="24" height="24" fill="none"/><rect x="170" y="106" width="24" height="24" fill="none"/>
    <rect x="146" y="130" width="24" height="24" fill="none"/><rect x="170" y="130" width="24" height="24" fill="none"/><rect x="170" y="154" width="24" height="24" fill="none"/></g>
  <g font-size="12" fill="currentColor" text-anchor="middle"><text x="86" y="74">1</text><text x="110" y="74">2</text><text x="134" y="74">3</text><text x="158" y="74">4</text><text x="182" y="74">5</text><text x="110" y="98">6</text><text x="134" y="98">7</text><text x="158" y="98">8</text><text x="182" y="98" font-weight="bold">9</text></g>
  <text x="10" y="232" fill="currentColor" font-size="12">중복이 없으면 15칸 = n(n−1)/2 전부</text>
  <text x="10" y="252" fill="currentColor" font-size="12">이 입력: 9번째 비교에서 (3, 3)</text>
  <text x="250" y="22" fill="currentColor" font-weight="bold">A2 정렬 후 이웃만 비교</text>
  <text x="250" y="73" fill="currentColor">입력</text>
  <g stroke="currentColor" fill="none"><rect x="290" y="56" width="30" height="24"/><rect x="322" y="56" width="30" height="24"/><rect x="354" y="56" width="30" height="24"/><rect x="386" y="56" width="30" height="24"/><rect x="418" y="56" width="30" height="24"/><rect x="450" y="56" width="30" height="24"/></g>
  <g fill="currentColor" text-anchor="middle"><text x="305" y="73">7</text><text x="337" y="73">3</text><text x="369" y="73">41</text><text x="401" y="73">0</text><text x="433" y="73">20</text><text x="465" y="73">3</text></g>
  <path d="M385 86 L385 118" stroke="currentColor" marker-end="url(#b01a)"/>
  <text x="395" y="106" fill="currentColor" font-size="12">qsort (복사본에)</text>
  <text x="250" y="143" fill="currentColor">정렬</text>
  <g stroke="currentColor"><rect x="290" y="126" width="30" height="24" fill="none"/><rect x="322" y="126" width="30" height="24" class="f-warn"/><rect x="354" y="126" width="30" height="24" class="f-warn"/><rect x="386" y="126" width="30" height="24" fill="none"/><rect x="418" y="126" width="30" height="24" fill="none"/><rect x="450" y="126" width="30" height="24" fill="none"/></g>
  <g fill="currentColor" text-anchor="middle"><text x="305" y="143">0</text><text x="337" y="143">3</text><text x="369" y="143">3</text><text x="401" y="143">7</text><text x="433" y="143">20</text><text x="465" y="143">41</text></g>
  <g stroke="currentColor" fill="none"><path d="M305 152 Q321 170 337 152"/><path d="M337 152 Q353 170 369 152" stroke-width="2.5"/><path d="M369 152 Q385 170 401 152" stroke-dasharray="3 3"/><path d="M401 152 Q417 170 433 152" stroke-dasharray="3 3"/><path d="M433 152 Q449 170 465 152" stroke-dasharray="3 3"/></g>
  <text x="250" y="196" fill="currentColor" font-size="12">같은 값은 정렬하면 이웃이 된다</text>
  <text x="250" y="216" fill="currentColor" font-size="12">→ 이웃 n−1 쌍만 보면 끝 (여기선 2번째)</text>
  <text x="250" y="252" fill="currentColor" font-size="12">비용 = 정렬 O(n log n) + 복사본 4n B</text>
  <text x="500" y="22" fill="currentColor" font-weight="bold">A3 해시 집합 (cap 16)</text>
  <g font-size="12" fill="currentColor"><text x="500" y="58">7 → h=5, 넣음</text><text x="500" y="80">3 → h=13, 넣음</text><text x="500" y="102">41 → 5 찼음 → 6</text><text x="500" y="124">0 → has_zero = 1</text><text x="500" y="146">20 → 5, 6 찼음 → 7</text><text x="500" y="168" font-weight="bold">3 → 13 에 3: 중복!</text></g>
  <g font-size="12" fill="currentColor" text-anchor="end"><text x="644" y="55">4</text><text x="644" y="77">5</text><text x="644" y="99">6</text><text x="644" y="121">7</text><text x="644" y="143">8</text><text x="644" y="191">13</text></g>
  <g stroke="currentColor"><rect x="650" y="40" width="70" height="22" fill="none"/><rect x="650" y="62" width="70" height="22" class="f-mut"/><rect x="650" y="84" width="70" height="22" class="f-mut"/><rect x="650" y="106" width="70" height="22" class="f-mut"/><rect x="650" y="128" width="70" height="22" fill="none"/><rect x="650" y="176" width="70" height="22" class="f-warn"/></g>
  <g fill="currentColor" text-anchor="middle"><text x="685" y="78">7</text><text x="685" y="100">41</text><text x="685" y="122">20</text><text x="685" y="166">⋮</text><text x="685" y="192">3</text></g>
  <text x="500" y="228" fill="currentColor" font-size="12" font-family="monospace">h(x) = (x·2654435769) &gt;&gt; 28</text>
  <text x="500" y="250" fill="currentColor" font-size="12">칸 5·6·7 = 한 덩어리(cluster)</text>
  <text x="10" y="288" fill="currentColor" font-size="13">세 방법 모두 "있다". 다른 것은 일의 양: 비교 9번 / 정렬 + 이웃 비교 2번 / 삽입 6번에 칸 8번 확인</text>
</svg>
```

입력 예 `a = {7, 3, 41, 0, 20, 3}` 에서 세 방법이 하는 일. 해시 칸 번호는 solution.c 의 `hs_hash` 공식(k = 4)으로 계산한 실제 값이다.

### Part A — 세 가지 풀이 (malloc 금지)

- `has_dup_pairwise(a, n)` — 모든 쌍 `i < j` 비교. 추가 메모리 0.
- `cmp_u32` + `has_dup_sort(a, n, scratch)` — 입력을 호출자가 준 `scratch`(4n 바이트)에 복사 → **C 라이브러리 `qsort`** 로 정렬 → 이웃 비교. 입력은 바꾸지 않는다. (정렬을 직접 짜는 건 ALGO-B05~B08 몫이라 여기선 qsort 를 쓴다.)
- `hs_insert` + `has_dup_hash(a, n, buf, k)` — 호출자가 준 `buf` 에 칸 `cap = 2^k` 개(cap ≥ 2n)짜리 **오픈 어드레싱(open addressing), 선형 탐사(linear probing)** 해시 집합. 칸 값 0 = 빈 칸, 키 0 은 `has_zero` 플래그로. 해시는 곱셈 하나 + 시프트(피보나치 해싱).
- 테스트: 이름 붙은 경계(빈 배열, 원소 1개, 0 과 0xFFFFFFFF, 0 두 번, 처음·끝 중복, 전부 같음), 구성으로 답을 아는 20000개 입력, xorshift32 무작위 3000개를 정의(이중 루프)와 대조, `has_dup_sort` 가 입력을 안 바꾸는지.

### Part B — 경계 버그와 스트리밍 변형

- `cmp_u32` 를 빼기로 짜면 틀리는 입력(0 vs 0xFFFFFFFF, 1 vs 0x80000001), 그리고 **이 맥의 qsort 에서 실제로 중복을 놓친 6원소 입력**.
- 해시 집합: 키 0 두 번, cap 8 에 7개까지 넣고 8번째는 `-1`(무한 루프 금지), 가득 차도 조회는 됨. cap < 2n 이면 `has_dup_hash` 가 `-1`.
- `first_repeat_pairwise` / `first_repeat_hash` — "처음으로 반복이 보이는 위치 j"(UART 로 샘플이 하나씩 올 때 몇 번째에서 알아채나). 무작위 2000개를 정의와 대조.

### Part C — RAM 예산으로 방법 고르기

- `has_dup_bitmap(a, n, limit, bits)` — 값 범위가 `[0, limit)` 로 알려져 있으면 limit 비트짜리 비트맵. 범위 밖 값 = 손상된 로그 → `-1`.
- `dup_check_budget(a, n, limit, ram, ram_bytes, &method)` — **비트맵 → 해시 → 정렬 → 쌍 비교** 순서로 예산에 들어가는 첫 방법을 골라 실행.
- 시나리오: CAN 버스 관절 보드 200개의 32비트 UID(4 KB → 해시), UID 1000개(4 KB → 정렬), UID 3000개(4 KB → 쌍 비교), 전원 손실 후 FTL 복구에서 NAND OOB 로 읽은 LBA 20000개(LBA < 65536, 8 KB → 비트맵). 무작위 예산 500개 대조.

시간·메모리 측정은 `demo_bench.c` 가 한다 (5. 풀이 끝의 "측정" 절).

## 2. Problem (English)

Given an array of `n` unsigned 32-bit values, return whether any value appears at least twice.

```c
bool has_dup_pairwise(const uint32_t *a, size_t n);                       /* O(n^2) time, O(1) space   */
bool has_dup_sort(const uint32_t *a, size_t n, uint32_t *scratch);        /* O(n log n), 4n bytes      */
int  has_dup_hash(const uint32_t *a, size_t n, uint32_t *buf, unsigned k);/* O(n) expected, 4*2^k B   */
ptrdiff_t first_repeat_hash(const uint32_t *a, size_t n, uint32_t *buf, unsigned k);
int  dup_check_budget(const uint32_t *a, size_t n, uint32_t limit,
                      uint32_t *ram, size_t ram_bytes, int *method);
```

Constraints: `0 ≤ n ≤ 10^7`; values span the full `uint32_t` range (0 and `0xFFFFFFFF` included); **no dynamic allocation** — all working memory is passed in by the caller; the input must not be modified (it may live in flash). `has_dup_hash` returns `-1` instead of looping forever when the table is too small. Then **measure** all three for `n = 10^3 … 10^7`, fit the slope of `log t` vs `log n`, and explain any deviation from 2, ~1, 1.

```text
Input:  a = [7, 3, 41, 0, 20, 3]      Output: true   (first repeat at index 5)
Input:  a = [0, 0xFFFFFFFF]           Output: false
Input:  a = []                        Output: false
```

Follow-ups: Your hash answer needs 8–16 bytes per element — the MCU has 64 KB of RAM. What now? What if values are known to be < 65536? What if you may not write anything at all? (LeetCode 217 *Contains Duplicate* is the plain version.)

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데: `has_dup_pairwise`, `cmp_u32`, `has_dup_sort`, `hs_insert`, `has_dup_hash`, `first_repeat_pairwise`, `first_repeat_hash`, `has_dup_bitmap`, `dup_check_budget`. `hs_init`, `hs_hash`, `log2_cap_for`, `bytes_*` 는 주어진다. 스텁은 "중복 없음" 을 돌려주므로 멈추거나 터지지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:180  all3(e2, 2, true)
FAIL starter.c:182  all3(e4, 3, true)
…
random: 3000 arrays, 2055 with dup, 945 without, mismatches=2055
…
FAIL starter.c:337  bad == 0
work RAM n=1000: bitmap(limit 65536) 8192 B, hash 8192 B, sort 4000 B, pairwise 0 B -> 4 KB budget picks sort
15/53 passed
```

(exit 1. 통과한 15개는 스텁의 "없음 / 0 / -1" 이 우연히 맞는 사례들이다. 그래서 "있음" 사례를 반드시 섞는다.)

## 4. 힌트

<details><summary>힌트 1 — 쌍 비교: i &lt; j 만, 그리고 n = 0</summary>

`j` 를 `i + 1` 부터 시작하면 자기 자신과 비교하지 않고 같은 쌍을 두 번 보지도 않는다. 바깥 조건을 `i < n - 1` 로 쓰면 `size_t n = 0` 일 때 `n - 1` 이 `SIZE_MAX` 가 된다 → `i + 1 < n` 으로 쓴다.

</details>

<details><summary>힌트 2 — 정렬: 같은 값은 이웃이 된다</summary>

정렬된 배열에서 `x` 가 두 번 있으면 그 둘 사이의 모든 값은 `x` 이상 `x` 이하, 즉 `x` 다 → 이웃 쌍 `(i−1, i)` 만 보면 된다. 비교 함수는 `x - y` 를 돌려주면 안 된다(Part B). 비교 두 번으로 `-1/0/1`.

</details>

<details><summary>힌트 3 — 해시: 빈 칸 표시와 키 0</summary>

칸이 0 이면 빈 칸이라고 정했으니 키 0 은 칸에 넣을 수 없다 → `bool has_zero` 하나로 따로 기억한다. 탐사는 `i = (i + 1) & mask` (cap 이 2의 거듭제곱이라 `%` 없이 감김). 빈 칸을 하나도 남기지 않으면 없는 키를 찾는 탐사가 영원히 돈다 → `count >= cap − 1` 이면 거절.

</details>

<details><summary>힌트 4 — 처음 반복 위치 = 넣다가 처음으로 "이미 있음"</summary>

해시판은 `hs_insert` 가 1 을 돌려준 순간의 `j` 가 답이다. 쌍 비교판은 `j` 를 바깥 루프로 두고 `i < j` 를 안쪽에서 본다 — 바깥이 `i` 면 "짝이 있는 가장 작은 i" 의 짝을 돌려주게 되어 답이 달라진다 (`{1, 2, 2, 1}` 의 답은 2 인데 i-바깥은 3).

</details>

<details><summary>힌트 5 — 예산 고르기</summary>

`bytes_bitmap(limit)`, `bytes_hash(n)`, `bytes_sort(n)` 이 이미 있다. `limit != 0` 일 때만 비트맵 후보. 해시는 `log2_cap_for(n)` 을 `k` 로 넘긴다. `ram` 은 `uint32_t *` 라 정렬(alignment) 걱정이 없다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문 | 값의 범위는? (16비트? 전 범위?) n 의 최대는? 입력을 바꿔도 되나(정렬 제자리)? 추가 메모리 몇 바이트까지? 답은 bool 인가 위치인가? |
| 2. 무식한 답 | 모든 쌍 비교 — Θ(n²) 시간, O(1) 메모리. "n = 10⁵ 이면 5·10⁹ 비교라 수 초" 까지 말한다 |
| 3. 더 나은 생각 + 불변식 | 정렬하면 같은 값은 이웃 → O(n log n). 해시 집합은 "지금까지 본 값의 집합" 을 유지 → 기대 O(n) |
| 4. 코드 | 해시판을 호출자 버퍼로 (malloc 없이), 키 0 처리 포함 |
| 5. 소리 내어 말할 테스트 | `[]`, `[x]`, `[x, x]`, 전부 같음, 0 이 두 번, `0xFFFFFFFF`, 끝에서만 중복, 큰 무중복 입력 |
| 6. 복잡도 | 시간(평균·최악), **추가 메모리 바이트**(해시 8n~16n B), 스택(qsort 는 구현 따라 다름) |

### Part A — 쌍 비교: 일의 양을 정확히 센다

```c
for (size_t i = 0; i + 1 < n; i++)
    for (size_t j = i + 1; j < n; j++)
        if (a[i] == a[j]) return true;
```

중복이 없으면 비교 횟수는 정확히 C(n, 2) = n(n−1)/2 (연습 2 가 3000개에서 4498500 을 센다). 이게 Θ(n²) 의 실체다. 이 맥에서 `clang -O2 -S` 로 본 안쪽 루프는 `ldr / cmp / b.eq / add / cmp / b.ne` 6개짜리 스칼라 루프(벡터화 안 됨)이고, 비교 1번에 약 0.23 ns 였다(1.158 s ÷ 5·10⁹). 빠른 코어라 상수는 작지만 n² 는 이길 수 없다.

### Part A — 정렬 후 이웃 비교: 불변식

**불변식**: 오름차순 정렬된 `s[0..n)` 에서 `s[i] == s[j]` (i < j) 이면 `s[i] == s[i+1]`. 증명: `s[i] ≤ s[i+1] ≤ s[j] = s[i]`. 그래서 이웃 n−1 쌍만 보면 된다. 일의 대부분은 정렬이다.

- **qsort 를 고른 이유**: 이 레슨의 주제는 측정이지 정렬 구현이 아니다. 단, C 표준(C11 7.22.5.2)은 qsort 의 **알고리즘도 복잡도도 안정성도 정하지 않는다**. "O(n log n)" 은 구현을 믿는 것이다 — 최악을 보장하려면 힙 정렬(O(1) 스택)을 직접 쓴다(ALGO-B06/B07 참고).
- **복사본에 정렬**: 입력은 센서 DMA 버퍼거나 flash 의 `const` 표일 수 있다. 제자리 정렬로 바꾸면 7. 실수 3 처럼 이 맥에서 SIGBUS 로 죽는다. 대가는 4n 바이트.
- 연습 2 의 실측: 이 맥 qsort 의 비교 함수 호출 수 = 1.01~1.05 · n log₂ n (n = 10³ ~ 10⁶). 호출 수는 n log n 을 그대로 따른다.

### Part A — 해시 집합: 선형 탐사와 키 0

```c
if (x == 0) { if (h->has_zero) return 1; h->has_zero = true; return 0; }
uint32_t i = hs_hash(h, x);                 /* (x * 2654435769u) >> (32 - k) */
while (h->slot[i] != 0) {
    if (h->slot[i] == x) return 1;          /* 이미 있음 */
    i = (i + 1) & h->mask;                  /* 다음 칸, 끝에서 0 으로 감김 */
}
if (h->count >= h->mask) return -1;         /* 넣으면 빈 칸 0개 → 거절 */
h->slot[i] = x; h->count++; return 0;
```

**불변식**: 키 x 는 자기 집 칸 h(x) 에서 x 가 있는 칸까지 **빈 칸 없이 이어진 덩어리(cluster)** 안에 있다. 삭제를 안 하니 이 불변식이 깨지지 않고, 그래서 "빈 칸을 만나면 없다" 가 맞다 (삭제가 있으면 tombstone 이 필요하다 — DS-B11).

- **피보나치(곱셈) 해싱**: 2654435769 = ⌊2³²/φ⌋ = 0x9E3779B9. 곱한 결과의 **상위** k 비트를 쓴다(하위 비트는 입력 하위 비트에만 좌우된다). 나눗셈·나머지가 없어서 하드웨어 나눗셈이 없는 Cortex-M0 에서도 곱셈 1번 + 시프트 1번 (M0 의 MULS 는 구현 옵션에 따라 1 또는 32 사이클).
- **cap ≥ 2n (적재율 α ≤ 0.5)**: 선형 탐사의 기대 탐사 수(Knuth)는 성공 ≈ ½(1 + 1/(1−α)), 실패(=새 키 삽입) ≈ ½(1 + 1/(1−α)²). α = 0.5 이면 1.5 / 2.5 칸, α = 0.9 이면 5.5 / 50.5 칸. "중복 없음" 입력은 전부 실패 탐색이라 α 를 낮게 잡는다.
- **키 0**: 0 을 칸에 넣으면 그 칸은 여전히 "빈 칸" 으로 보여 두 번째 0 을 못 찾는다 (7. 실수 2, 실제로 FAIL 8개).

**추적 표** (`a = {7, 3, 41, 0, 20, 3}`, k = 4, cap 16; 그림의 오른쪽):

| j | a[j] | h(a[j]) | 확인한 칸 | 결과 | 칸 상태 |
|---|---|---|---|---|---|
| 0 | 7 | 5 | 5(빈) | 넣음 | 5:7 |
| 1 | 3 | 13 | 13(빈) | 넣음 | 5:7, 13:3 |
| 2 | 41 | 5 | 5(7), 6(빈) | 넣음 | + 6:41 |
| 3 | 0 | — | — | `has_zero = 1` | (칸 안 씀) |
| 4 | 20 | 5 | 5(7), 6(41), 7(빈) | 넣음 | + 7:20 (덩어리 5~7) |
| 5 | 3 | 13 | 13(3) | **이미 있음** → 1 | `first_repeat_hash` = 5 |

같은 입력에서 쌍 비교는 9번째 비교(i=1, j=5), 정렬판은 `{0,3,3,7,20,41}` 의 두 번째 이웃 쌍에서 찾는다. `first_repeat_pairwise` 는 j = 1..5 에서 1+2+3+4+2 = 12번 비교 후 5.

### Part A — 복잡도 정리

| 방법 | 시간 (최악) | 시간 (기대/보통) | 추가 메모리 | 스택 | 전제 |
|---|---|---|---|---|---|
| 쌍 비교 | n(n−1)/2 비교 = Θ(n²) | 같음 (중복 없을 때) | 0 B | O(1) | 없음 |
| 정렬 + 이웃 | 표준은 보장 안 함 | ≈1.04 n log₂ n 비교 (실측) | 4n B | qsort 구현 따라 | 입력 보존 위해 복사 |
| 해시 (선형 탐사) | Θ(n²) — 모든 키가 한 칸으로 (연습 3) | O(n), α ≤ 0.5 | 4·cap ∈ [8n, 16n) B | O(1) | 입력이 해시와 무관 |
| 비트맵 | O(n + L/32) | 같음 | ⌈L/32⌉·4 B | O(1) | 값 ∈ [0, L) |

cap = 2n 이상인 가장 작은 2의 거듭제곱이라 cap ∈ [2n, 4n), 바이트로 [8n, 16n). n = 10⁷ 이면 cap = 2²⁵, 128 MiB 다.

### Part B — 비교 함수의 빼기, 그리고 스트리밍

```c
return (int)(x - y);          /* 틀림: 0 - 0xFFFFFFFF = 1 (mod 2^32) → "0 > 0xFFFFFFFF" */
return (x > y) - (x < y);     /* 맞음: -1 / 0 / 1 */
```

`uint32_t` 빼기는 2³² 로 감긴 값이고, INT_MAX 보다 큰 값을 `int` 로 바꾸는 결과는 **구현 정의**(C11 6.3.1.3p3, UB 는 아님; clang 은 2의 보수로 자름)다. 입력이 `int` 였다면 `a - b` 의 오버플로는 **UB** 다. 결과적으로 비교가 추이성(transitivity)을 잃고, qsort 는 "정렬" 을 약속하지 않는다. 이 맥에서 `{1, 0x80000001, 0xFFFFFFFF, 0x80000000, 0x40000000, 0x80000001}` 을 빼기 비교로 정렬하면 `{1, 0x80000000, 0x80000001, 0xFFFFFFFF, 0x40000000, 0x80000001}` 이 나와 두 0x80000001 이 떨어진다 → 중복을 놓친다 (무작위로 찾은 입력, 하네스의 `e9`). 값 범위가 작은 무작위 테스트 3000개는 이 버그를 **전부 통과**했다 — 경계값 테스트가 따로 필요한 이유.

**스트리밍 변형**: 값이 UART/CAN 으로 하나씩 들어오면 "언제 처음 알아채나" 가 질문이 된다. 해시판은 들어오는 즉시 O(1) 기대 시간으로 답하고, 정렬판은 **전부 받은 뒤에야** 정렬할 수 있다 — 온라인(online) 알고리즘이냐 아니냐의 차이. 쌍 비교판은 j 번째 샘플에 j 번 비교라 ISR 안에서 쓰면 최악 시간이 샘플 수에 비례해 자란다.

### Part C — 예산이 답을 정한다

`dup_check_budget` 의 우선순위는 "빠른 것부터, 들어가면 쓴다". 64 KB RAM MCU 에서 이 작업에 줄 수 있는 RAM 이 4~16 KB 라면:

| n (32비트 ID) | 해시 4·cap | 정렬 복사본 4n | 비트맵 (값 < 65536) | 쌍 비교 |
|---|---|---|---|---|
| 256 | 2 KB | 1 KB | 8 KB (n 과 무관) | 0 B, 32640 비교 |
| 1,000 | 8 KB | 4 KB | 8 KB | 0 B, 499500 비교 |
| 4,000 | 32 KB | 16 KB | 8 KB | 0 B, 약 8·10⁶ 비교 |
| 8,192 | **64 KB = RAM 전부** | 32 KB | 8 KB | 0 B, 약 3.4·10⁷ 비교 |

- 하네스 시나리오의 실제 선택: UID 200개/4 KB → hash, 1000개/4 KB → sort, 3000개/4 KB → pairwise, LBA 20000개(< 65536)/8 KB → bitmap.
- **FTL 복구**(Don 의 SSD 경험): 전원 손실 뒤 NAND 페이지 OOB 를 훑어 LBA → 물리 페이지 표를 다시 만들 때, 같은 LBA 를 주장하는 페이지가 둘이면 시퀀스 번호로 최신을 고른다. LBA 범위가 정해져 있으니 비트맵(LBA 당 1비트)이 정답에 가깝다. 실제 FTL 에선 다시 만드는 L2P 표 자체가 "이 LBA 를 이미 봤나" 표 역할을 한다.
- 입력을 바꿔도 되면 정렬은 제자리 힙 정렬로 **추가 0 B + 최악 O(n log n)** — MCU 에서 가장 흔한 타협.
- 아무것도 못 쓰고 값이 `[1, n]` 이면 Floyd 사이클 찾기(연습 4)로 O(n) 시간 O(1) 메모리.

### 측정 — `demo_bench.c`

**방법.** 입력은 서로 다른 n 개(`fmix32(i·0x9E3779B1 + seed)` — 둘 다 2³² 위의 전단사라 겹침이 없다) → 세 방법 모두 끝까지 일하는 최악 경우. 각 (방법, n) 마다 워밍업 1번 → "20 ms 넘을 때까지 반복한 평균" 을 5번 → 중앙값. 결과는 `volatile` sink 에 더해 컴파일러가 일을 지우지 못하게 한다. 시계는 `CLOCK_MONOTONIC_RAW`, 스레드 QoS 는 `USER_INTERACTIVE`(P 코어 선호, 보장은 아님). 해시의 시간에는 테이블 memset 이 포함된다(그것도 O(cap) 일). 쌍 비교는 n = 10⁵ (1회 약 1.2 s)에서 멈춘다. 이 맥(Apple M6, P 코어 L1d 128 KiB, L2 20 MiB, `-O2`)의 실제 출력 (실행 4, 약 1분):

```text
[time] median ns per call (no duplicates = worst case)
        n |       pairwise    sort(qsort)           hash |  pair/n^2    sort/n    hash/n
     1000 |         120624          12688            757 |     0.121     12.69      0.76
     2000 |         462443          29421           1931 |     0.116     14.71      0.97
     5000 |        2876756          96739           3522 |     0.115     19.35      0.70
    10000 |       11171812         304008           7255 |     0.112     30.40      0.73
    20000 |       44390792         798082          15898 |     0.111     39.90      0.79
    50000 |      287407959        2340625          42460 |     0.115     46.81      0.85
   100000 |     1157727375        5678760         186580 |     0.116     56.79      1.87
   200000 |            nan       12307229         490138 |       nan     61.54      2.45
   500000 |            nan       31954958        1872686 |       nan     63.91      3.75
  1000000 |            nan       65926375        3972938 |       nan     65.93      3.97
  2000000 |            nan      140533500        8713569 |       nan     70.27      4.36
  5000000 |            nan      372991166       34512917 |       nan     74.60      6.90
 10000000 |            nan      798965542       72524250 |       nan     79.90      7.25

[slope] least squares of log10(t) on log10(n)
pairwise: all n ( 7 pts) 1.992   n>=1e4 ( 4 pts) 2.019
sort    : all n (13 pts) 1.199   n>=1e4 (10 pts) 1.123
hash    : all n (13 pts) 1.285   n>=1e4 (10 pts) 1.368
local slopes (between neighbouring n):   … 12줄 생략 (해시 0.66~2.14, 정렬 1.04~1.65, 쌍 비교 1.94~2.04)

[extrapolate] pairwise slope 1.992, measured 1.158 s at n=100000 -> n=1e7 ~ 11175 s (3.1 h)
              (exact n^2 scaling from the last point: 11577 s)
```

```svg
<svg viewBox="0 0 740 440" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="14">로그-로그 그래프: 중복 없는 n 개, 호출 1번의 시간 (중앙값)</text>
  <line x1="90" y1="380.0" x2="700" y2="380.0" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="349.9" x2="700" y2="349.9" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="319.8" x2="700" y2="319.8" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="289.7" x2="700" y2="289.7" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="259.6" x2="700" y2="259.6" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="229.6" x2="700" y2="229.6" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="199.5" x2="700" y2="199.5" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="90" y1="169.4" x2="700" y2="169.4" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="139.3" x2="700" y2="139.3" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="109.2" x2="700" y2="109.2" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="79.1" x2="700" y2="79.1" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90" y1="49.0" x2="700" y2="49.0" stroke="currentColor" stroke-opacity="0.15"/> <line x1="90.0" y1="40" x2="90.0" y2="380" stroke="currentColor" stroke-opacity="0.15"/> <line x1="242.5" y1="40" x2="242.5" y2="380" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="395.0" y1="40" x2="395.0" y2="380" stroke="currentColor" stroke-opacity="0.15"/> <line x1="547.5" y1="40" x2="547.5" y2="380" stroke="currentColor" stroke-opacity="0.15"/> <line x1="700.0" y1="40" x2="700.0" y2="380" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="384.0" fill="currentColor" text-anchor="end">100 ns</text> <text x="84" y="353.9" fill="currentColor" text-anchor="end">1 µs</text> <text x="84" y="323.8" fill="currentColor" text-anchor="end">10 µs</text> <text x="84" y="293.7" fill="currentColor" text-anchor="end">100 µs</text> <text x="84" y="263.6" fill="currentColor" text-anchor="end">1 ms</text> <text x="84" y="233.6" fill="currentColor" text-anchor="end">10 ms</text> <text x="84" y="203.5" fill="currentColor" text-anchor="end">100 ms</text> <text x="84" y="173.4" fill="currentColor" text-anchor="end">1 s</text> <text x="84" y="143.3" fill="currentColor" text-anchor="end">10 s</text>
  <text x="84" y="113.2" fill="currentColor" text-anchor="end">100 s</text> <text x="84" y="83.1" fill="currentColor" text-anchor="end">1000 s</text> <text x="84" y="53.0" fill="currentColor" text-anchor="end">10⁴ s</text>
  <text x="90.0" y="398" fill="currentColor" text-anchor="middle">10³</text> <text x="242.5" y="398" fill="currentColor" text-anchor="middle">10⁴</text> <text x="395.0" y="398" fill="currentColor" text-anchor="middle">10⁵</text> <text x="547.5" y="398" fill="currentColor" text-anchor="middle">10⁶</text> <text x="700.0" y="398" fill="currentColor" text-anchor="middle">10⁷</text>
  <rect x="90" y="40" width="610" height="340" fill="none" stroke="currentColor"/>
  <text x="395.0" y="416" fill="currentColor" text-anchor="middle">n (로그 눈금)</text>
  <polyline points="90.0,287.3 135.9,269.7 196.6,245.8 242.5,228.1 288.4,210.1 349.1,185.7 395.0,167.5" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <circle cx="90.0" cy="287.3" r="4" class="f-warn" stroke="currentColor"/> <circle cx="135.9" cy="269.7" r="4" class="f-warn" stroke="currentColor"/> <circle cx="196.6" cy="245.8" r="4" class="f-warn" stroke="currentColor"/> <circle cx="242.5" cy="228.1" r="4" class="f-warn" stroke="currentColor"/> <circle cx="288.4" cy="210.1" r="4" class="f-warn" stroke="currentColor"/> <circle cx="349.1" cy="185.7" r="4" class="f-warn" stroke="currentColor"/> <circle cx="395.0" cy="167.5" r="4" class="f-warn" stroke="currentColor"/>
  <polyline points="90.0,316.7 135.9,305.7 196.6,290.2 242.5,275.2 288.4,262.6 349.1,248.5 395.0,237.0 440.9,226.8 501.6,214.4 547.5,204.9 593.4,195.0 654.1,182.3 700.0,172.3" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <rect x="86.5" y="313.2" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="132.4" y="302.2" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="193.1" y="286.7" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="239.0" y="271.7" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="284.9" y="259.1" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="345.6" y="245.0" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="391.5" y="233.5" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="437.4" y="223.3" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="498.1" y="210.9" width="7" height="7" class="f-acc" stroke="currentColor"/>
  <rect x="544.0" y="201.4" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="589.9" y="191.5" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="650.6" y="178.8" width="7" height="7" class="f-acc" stroke="currentColor"/> <rect x="696.5" y="168.8" width="7" height="7" class="f-acc" stroke="currentColor"/>
  <polyline points="90.0,353.5 135.9,341.3 196.6,333.5 242.5,324.0 288.4,313.8 349.1,300.9 395.0,281.6 440.9,269.0 501.6,251.4 547.5,241.6 593.4,231.4 654.1,213.4 700.0,203.7" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
  <path d="M90.0 349.0 L94.5 357.0 L85.5 357.0 Z" class="f-ok" stroke="currentColor"/> <path d="M135.9 336.8 L140.4 344.8 L131.4 344.8 Z" class="f-ok" stroke="currentColor"/> <path d="M196.6 329.0 L201.1 337.0 L192.1 337.0 Z" class="f-ok" stroke="currentColor"/> <path d="M242.5 319.5 L247.0 327.5 L238.0 327.5 Z" class="f-ok" stroke="currentColor"/> <path d="M288.4 309.3 L292.9 317.3 L283.9 317.3 Z" class="f-ok" stroke="currentColor"/> <path d="M349.1 296.4 L353.6 304.4 L344.6 304.4 Z" class="f-ok" stroke="currentColor"/> <path d="M395.0 277.1 L399.5 285.1 L390.5 285.1 Z" class="f-ok" stroke="currentColor"/> <path d="M440.9 264.5 L445.4 272.5 L436.4 272.5 Z" class="f-ok" stroke="currentColor"/>
  <path d="M501.6 246.9 L506.1 254.9 L497.1 254.9 Z" class="f-ok" stroke="currentColor"/> <path d="M547.5 237.1 L552.0 245.1 L543.0 245.1 Z" class="f-ok" stroke="currentColor"/> <path d="M593.4 226.9 L597.9 234.9 L588.9 234.9 Z" class="f-ok" stroke="currentColor"/> <path d="M654.1 208.9 L658.6 216.9 L649.6 216.9 Z" class="f-ok" stroke="currentColor"/> <path d="M700.0 199.2 L704.5 207.2 L695.5 207.2 Z" class="f-ok" stroke="currentColor"/>
  <line x1="395.0" y1="167.5" x2="700.0" y2="47.6" stroke="currentColor" stroke-width="1.2" stroke-dasharray="3 4"/>
  <circle cx="700.0" cy="47.6" r="4" fill="none" stroke="currentColor"/>
  <text x="692.0" y="51.6" fill="currentColor" text-anchor="end">외삽 ≈ 3.1 시간</text> <text x="403.0" y="185.5" fill="currentColor">측정 상한 n=10⁵</text>
  <line x1="104" y1="58" x2="134" y2="58" stroke="currentColor" stroke-width="1.5"/>
  <circle cx="119" cy="58" r="4" class="f-warn" stroke="currentColor"/>
  <text x="142" y="62" fill="currentColor">pairwise O(n²)</text>
  <line x1="104" y1="78" x2="134" y2="78" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <rect x="115.5" y="74.5" width="7" height="7" class="f-acc" stroke="currentColor"/>
  <text x="142" y="82" fill="currentColor">sort O(n log n)</text>
  <line x1="104" y1="98" x2="134" y2="98" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
  <path d="M119 93.5 L123.5 101.5 L114.5 101.5 Z" class="f-ok" stroke="currentColor"/>
  <text x="142" y="102" fill="currentColor">hash O(n)</text> <text x="274" y="62" fill="currentColor">기울기 2.019  (n ≥ 10⁴ 맞춤)</text> <text x="274" y="82" fill="currentColor">기울기 1.123  (n ≥ 10⁴ 맞춤)</text> <text x="274" y="102" fill="currentColor">기울기 1.368  (n ≥ 10⁴ 맞춤)</text>
</svg>
```

그림: 위 표(실행 4)의 실측값을 그대로 찍었다. 쌍 비교의 점선은 맞춘 기울기로 10⁷ 까지 늘린 외삽이다.

**기울기는 실행마다 조금씩 다르다.** 같은 코드를 네 번 돌린 n ≥ 10⁴ 맞춤 기울기 (실행 1·2 는 QoS 설정과 캐시 스윕을 넣기 전 판):

| 방법 | 실행 1 | 실행 2 | 실행 3 | 실행 4 | 이론 |
|---|---|---|---|---|---|
| 쌍 비교 | 1.999 | 2.057 | 1.979 | 2.019 | 2 |
| 정렬 (qsort) | 1.090 | 1.101 | 1.084 | 1.123 | 1 + 1/ln n → 이 격자에서 1.081 (연습 1) |
| 해시 | 1.323 | 1.359 | 1.312 | 1.368 | 1 (메모리 비용이 일정하다면) |

절대 시간은 실행끼리 꽤 흔들렸지만(정렬 10⁷ 은 757~917 ms, 해시 10⁵ 은 101~268 µs 로 2.6배) 같은 방법의 기울기 차이는 0.08 이하다 — **비율과 기울기를 보고 ns 는 믿지 않는다.**

**왜 n log n 은 1.1 로 보이나.** log t = log c + log n + log log n 이므로 국소 기울기는 d ln(n ln n)/d ln n = **1 + 1/ln n**: 10³ 에서 1.145, 10⁵ 에서 1.087, 10⁷ 에서 1.062. 로그의 밑은 상수배라 기울기에 안 나온다. 최소제곱으로 10⁴~10⁷ 격자에 맞추면 1.081 (연습 1 이 정확한 n log₂ n 데이터로 계산) — 측정 1.08~1.12 와 맞는다. 즉 **기울기 1.1 은 "n log n 이거나, n 인데 무언가 느려지거나" 둘 다 가능** 하다. 가르려면 연산 수를 센다(연습 2). 덧붙여 비교 1번의 시간은 10³ 에서 약 1.2 ns, 10⁵ 이상에서 약 3.2 ns 로 커졌다(표의 sort/n ÷ 1.04 log₂ n) — 원인(작은 배열 처리 비중, 분기 예측, 캐시)은 이 실험으로 가르지 않았다.

**왜 O(n) 해시가 1.3 으로 보이나 — 캐시.** 알고리즘은 원소당 기대 탐사 수가 일정하지만, **탐사 한 번의 값** 이 테이블 크기에 따라 다르다. 적재율을 0.375 로 고정하고 테이블 크기만 바꾼 스윕 (실행 4, 괄호는 실행 3):

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="14">해시 삽입 1회 비용 vs 테이블 크기 (적재율 0.375 고정)</text>
  <line x1="70" y1="250.0" x2="700" y2="250.0" stroke="currentColor" stroke-opacity="0.15"/> <line x1="70" y1="197.5" x2="700" y2="197.5" stroke="currentColor" stroke-opacity="0.15"/> <line x1="70" y1="145.0" x2="700" y2="145.0" stroke="currentColor" stroke-opacity="0.15"/> <line x1="70" y1="92.5" x2="700" y2="92.5" stroke="currentColor" stroke-opacity="0.15"/> <line x1="70" y1="40.0" x2="700" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="64" y="254.0" fill="currentColor" text-anchor="end">0 ns</text> <text x="64" y="201.5" fill="currentColor" text-anchor="end">2 ns</text> <text x="64" y="149.0" fill="currentColor" text-anchor="end">4 ns</text> <text x="64" y="96.5" fill="currentColor" text-anchor="end">6 ns</text> <text x="64" y="44.0" fill="currentColor" text-anchor="end">8 ns</text>
  <text x="70.0" y="268" fill="currentColor" text-anchor="middle">8K</text> <text x="160.0" y="268" fill="currentColor" text-anchor="middle">32K</text> <text x="250.0" y="268" fill="currentColor" text-anchor="middle">128K</text> <text x="340.0" y="268" fill="currentColor" text-anchor="middle">512K</text> <text x="430.0" y="268" fill="currentColor" text-anchor="middle">2M</text> <text x="520.0" y="268" fill="currentColor" text-anchor="middle">8M</text> <text x="610.0" y="268" fill="currentColor" text-anchor="middle">32M</text> <text x="700.0" y="268" fill="currentColor" text-anchor="middle">128M</text>
  <line x1="250.0" y1="40" x2="250.0" y2="250" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="254.0" y="54" fill="currentColor">L1d 128 KiB</text>
  <line x1="579.5" y1="40" x2="579.5" y2="250" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="575.5" y="54" fill="currentColor" text-anchor="end">L2 20 MiB</text>
  <rect x="70" y="40" width="630" height="210" fill="none" stroke="currentColor"/>
  <polyline points="70.0,228.7 115.0,228.7 160.0,227.4 205.0,223.5 250.0,219.0 295.0,218.2 340.0,213.5 385.0,204.1 430.0,186.5 475.0,167.6 520.0,164.2 565.0,156.6 610.0,74.9 655.0,54.7 700.0,49.2" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <circle cx="70.0" cy="228.7" r="4" class="f-ok" stroke="currentColor"/> <circle cx="115.0" cy="228.7" r="4" class="f-ok" stroke="currentColor"/> <circle cx="160.0" cy="227.4" r="4" class="f-ok" stroke="currentColor"/> <circle cx="205.0" cy="223.5" r="4" class="f-ok" stroke="currentColor"/> <circle cx="250.0" cy="219.0" r="4" class="f-ok" stroke="currentColor"/> <circle cx="295.0" cy="218.2" r="4" class="f-ok" stroke="currentColor"/> <circle cx="340.0" cy="213.5" r="4" class="f-ok" stroke="currentColor"/> <circle cx="385.0" cy="204.1" r="4" class="f-ok" stroke="currentColor"/> <circle cx="430.0" cy="186.5" r="4" class="f-ok" stroke="currentColor"/> <circle cx="475.0" cy="167.6" r="4" class="f-ok" stroke="currentColor"/>
  <circle cx="520.0" cy="164.2" r="4" class="f-ok" stroke="currentColor"/> <circle cx="565.0" cy="156.6" r="4" class="f-ok" stroke="currentColor"/> <circle cx="610.0" cy="74.9" r="4" class="f-ok" stroke="currentColor"/> <circle cx="655.0" cy="54.7" r="4" class="f-ok" stroke="currentColor"/> <circle cx="700.0" cy="49.2" r="4" class="f-ok" stroke="currentColor"/>
  <text x="385.0" y="288" fill="currentColor" text-anchor="middle">테이블 크기 4·cap 바이트 (로그 눈금, 8 KiB → 128 MiB)</text>
</svg>
```

| 테이블 | 8~64 KiB | 1 MiB | 2 MiB | 16 MiB | 32 MiB | 128 MiB |
|---|---|---|---|---|---|---|
| ns/원소 (실행 4) | 0.81~1.01 | 1.75 | 2.42 | 3.56 | 6.67 | 7.65 |
| ns/원소 (실행 3) | 0.70~0.74 | 0.88 | 1.38 | 2.71 | 3.93 | 6.22 |

두 실행 모두 16 → 32 MiB 사이(L2 20 MiB 를 넘는 곳)에서 크게 뛰고, 1 → 2 MiB 에서도 한 번 뛴다. 두 번째 계단은 L1d(128 KiB)보다 훨씬 위라 다른 원인(TLB 도달 범위 등)이 의심되지만 **이 실험으로는 확인하지 않았다**. 무작위 칸 접근은 캐시 줄 하나를 쓰려고 줄 전체를 가져오므로 테이블이 캐시 밖이면 원소마다 메모리 지연을 낸다. 본 표의 1-2-5 격자는 적재율도 0.30~0.48 로 오르내려(cap 이 2의 거듭제곱이라) 국소 기울기를 더 흔든다. 그래도 10⁷ 에서 해시(72.5 ms)는 정렬(799 ms)보다 11배 빠르다 — 기울기가 커 보여도 상수와 지수가 둘 다 작다.

**작은 n — 타이머 해상도와 고정 비용.** 이 맥의 `clock_getres` 는 `CLOCK_MONOTONIC` 1000 ns, `CLOCK_MONOTONIC_RAW` 42 ns 를 보고한다. n = 1000 해시 1회는 약 0.75 µs 라 `CLOCK_MONOTONIC` 한 번으로 재면 0 이나 1000 ns 로 읽힌다. 한 번씩 잰 실제 값 (실행 4):

```text
[single shot, n=1000, 8 runs each, ns]
pairwise: 119125 117792 117667 117709 117667 117792 117667 117625
sort    :  21084  15208  14000  13792  13750  13708  13708  13625
hash    :   2042   1334   1084    834    792    750    750    750
```

해시 값은 41.67 ns(24 MHz 틱)의 배수(750 = 18틱, 792 = 19틱)로 계단져 있고, 첫 실행은 2.7배 느리다(캐시·분기 예측기가 차가운 탓으로 보인다). 그래서 (1) 반복해서 총 시간을 키우고 (2) 워밍업을 버리고 (3) 중앙값을 쓴다. 호출마다 붙는 고정 비용(함수 호출, memset 시작, 타이머 읽기)도 작은 n 의 기울기를 깎는다: t = 20n + 50000 ns 를 10³~10⁴ 에 맞추면 기울기 0.55 (연습 1).

**외삽.** 쌍 비교 기울기 1.992, n = 10⁵ 에서 1.158 s → n = 10⁷ 에서 약 11175 s ≈ **3.1 시간** (정확히 n² 로 늘리면 11577 s). 같은 n 에서 해시는 0.07 s. 실행 1~4 의 외삽은 3.0~3.9 시간.

**메모리.** 코드가 잡는 바이트와, 방법 하나만 돌리는 자식 프로세스(fork → `wait4` 의 `ru_maxrss`)의 최대 RSS 증가분. `ru_maxrss` 단위는 **macOS 는 바이트, Linux 는 킬로바이트** 라 코드에서 `#ifdef __APPLE__` 로 나눈다. 실제 출력 (실행 4; 네 실행이 0.1 MiB 안에서 같았다):

```text
        n |       pair B       sort B       hash B |  rss input      +pair      +sort      +hash
     1000 |            0         4000         8192 |      1.2 M     +0.0 M     +0.0 M     +0.0 M
   100000 |            0       400000      1048576 |      1.5 M     +0.0 M     +0.4 M     +1.0 M
  1000000 |            0      4000000      8388608 |      5.0 M     +0.0 M     +3.8 M     +8.0 M
 10000000 |            0     40000000    134217728 |     39.3 M     +0.0 M    +38.2 M   +128.0 M
```

(M = MiB.) 계산한 바이트와 RSS 증가분이 맞는다(40000000 B = 38.1 MiB, 2²⁷ B = 128 MiB). n = 1000 의 몇 KB 는 이미 잡혀 있던 힙 페이지 안에서 나오는 것으로 보이며 RSS 가 안 변한다. **시간 11배를 사려고 메모리 3.4배(128 MiB vs 38 MiB)를 낸다** — 이게 이 레슨의 교환 비율이다.

### 면접용 규칙: 입력 크기 → 허용 복잡도

채점기·면접관이 쓰는 경험칙은 "C 로 초당 단순 연산 약 10⁸" 이다(이 맥의 쌍 비교는 초당 4·10⁹ 비교였으니 여유가 있다). 1초 기준:

| n 의 크기 | 허용 복잡도 | 대표 |
|---|---|---|
| ≤ 10~12 | O(n!) | 순열 백트래킹 |
| ≤ 20~25 | O(2ⁿ), O(2ⁿ·n) | 부분집합, 비트마스크 DP (ALGO-A03) |
| ≤ 500 | O(n³) | 플로이드-워셜 (ALGO-A02) |
| ≤ 5·10³ ~ 10⁴ | O(n²) | 쌍 비교, 2D DP |
| ≤ 10⁶ ~ 10⁷ | O(n log n) | 정렬, 힙, 이진 탐색 n번 |
| ≤ 10⁸ | O(n) | 해시, 투 포인터, 한 번 훑기 |
| 그 이상 | O(log n), O(1) | 이진 탐색, 공식 |

MCU 는 클럭이 수십~수백 MHz 에 단일 발행이라 위 표에서 n 을 10~100 배 줄여 읽고, 그보다 먼저 **RAM 열** 을 하나 더 붙인다: "O(n) 해시는 8n~16n B" 처럼.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 53/53 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 53/53, 보고 없음
cc -std=c11 -Wall -Wextra -O2 demo_bench.c -o demo_bench -lm && ./demo_bench   # 약 1분
```

## 7. 자주 하는 실수

**1. 비교 함수를 빼기로.** `return (int)(x - y);` — solution.c 에 넣고 돌린 실제 결과: 무작위 3000개는 `mismatches=0` 으로 통과하고 경계 검사 4개만 FAIL.

```text
FAIL m1.c:267  cmp_u32(&x, &y) < 0
FAIL m1.c:268  cmp_u32(&y, &x) > 0
FAIL m1.c:273  v[0] == 0u && v[1] == 1u && v[2] == 0x7FFFFFFFu && v[3] == 0x80000000u && v[4] == 0xFFFFFFFFu
FAIL m1.c:276  all3(e9, 6, true)
49/53 passed
```

고침: `return (x > y) - (x < y);`.

**2. 키 0 을 보통 키처럼 칸에 넣기.** `has_zero` 분기를 지우면 0 이 들어간 칸이 계속 "빈 칸" 이다 → `{0, 1, 0}` 에서 중복을 놓치고 `count` 도 틀린다. 실제 결과: `FAIL … all3(e5, 3, true)`, 무작위 `mismatches=6` 포함 8개 FAIL, `45/53 passed`. 고침: 빈 칸 표시와 겹치는 키는 플래그로 따로 (또는 별도 점유 비트맵, cap/8 B).

**3. 입력을 제자리 정렬.** `qsort((uint32_t *)a, …)` 로 const 를 벗겨 입력을 정렬하면, 하네스의 `static const` 배열(읽기 전용 영역)에 쓰다가 이 맥에서 **SIGBUS, exit 138** 로 죽는다. MCU 에서 입력이 flash 의 `const` 표면 버스 오류 → HardFault, RAM 이면 호출자 데이터가 조용히 바뀐다. 고침: 복사본에 정렬하거나, 바꿔도 되는지 면접관에게 먼저 묻는다.

**4. "가득 참" 검사 빼기.** `if (h->count >= h->mask) return -1;` 을 지우면 cap 8 에 8번째 키를 넣는 순간 빈 칸이 0개가 되고, 다음 "없는 키" 탐사가 원을 영원히 돈다. 실제로 하네스가 끝나지 않아 5초 알람으로 끊었다(exit 142 = SIGALRM). 펌웨어라면 워치독 리셋. 고침: 빈 칸 1개 이상 유지, 아니면 -1.

**5. `i < n - 1` 과 `int` 루프 변수.** `size_t n = 0` 이면 `n - 1 = SIZE_MAX` → 범위 밖 읽기. `int i` 로 n ≥ 2³¹ 을 돌면 부호 있는 오버플로(UB). 비교 횟수 n(n−1)/2 를 32비트에 세면 n = 10⁵ 에서 이미 넘친다(5·10⁹ > 2³²) — 카운터는 `uint64_t`.

**6. 측정 실수.** (a) 결과를 안 쓰면 `-O2` 가 호출을 통째로 지울 수 있다 → `volatile` sink. (b) n = 1000 을 `CLOCK_MONOTONIC` 한 번으로 재면 0 이나 1 µs. (c) 첫 실행 포함 → 해시 n = 1000 에서 2.7배 부풀린 값. (d) n 이 2배씩만 바뀌는 두 점으로 기울기를 읽으면 국소 기울기가 0.66~2.14 까지 흔들린다(실행 4 의 해시) → 넓은 범위 + 최소제곱.

## 8. 면접 Q&A

<details><summary>Q1. n 이 10⁵ 이라고 하면 어떤 복잡도까지 괜찮나요? 왜요?</summary>

1초에 단순 연산 10⁸ 정도라고 보면 n² = 10¹⁰ 은 100초 — 안 된다. n log n ≈ 1.7·10⁶ 은 넉넉하다. 이 맥에서 실제로 쌍 비교 n = 10⁵ 는 1.16 s(빠른 코어라 덜 느림), 해시는 0.19 ms 였다. 반대로 n ≤ 2000 이면 O(n²) 도 수 ms 라 코드가 가장 짧은 답이 이기는 경우도 있다 — 그 판단을 소리 내어 말한다.

</details>

<details><summary>Q2. 해시 답이 O(n) 이니 끝 아닌가요? RAM 이 64 KB 인 MCU 라면요?</summary>

cap ≥ 2n 의 4바이트 칸이면 8n~16n B. ID 1000개면 8 KB(전체 RAM 의 1/8), 8192개면 64 KB 전부라 불가능. 선택지: (1) 값 범위가 작으면 비트맵 — 16비트 값은 n 과 무관하게 8 KB, (2) 입력을 바꿔도 되면 제자리 힙 정렬 — 추가 0 B, 최악 O(n log n), (3) 바꿀 수 없고 RAM 이 없으면 쌍 비교 — 1000개면 5·10⁵ 비교로 Cortex-M4 에서 수십 ms(비교 1번 ~8 사이클 가정, 측정 안 함), (4) 적재율을 0.75 로 올려 메모리를 반으로 — 대신 실패 탐색이 8.5칸으로 늘어난다. 정답은 예산표(Part C)를 보고 고르는 것.

</details>

<details><summary>Q3. 해시의 최악은요? 공격자가 입력을 고를 수 있다면?</summary>

최악은 모든 키가 한 칸 덩어리로 모일 때 Θ(n²). 고정된 해시 함수에는 항상 그런 입력이 있다(비둘기집: 2³² 개 키를 cap 칸에 넣으면 어느 칸엔 2³²/cap 개 이상). 피보나치 해시라도 키 = i·A⁻¹ (mod 2³²) 이면 x·A = i 라 상위 k 비트가 전부 0 이다 — 연습 3 에서 탐사 수가 정확히 n(n−1)/2 = 1999000. 네트워크 패킷 ID 처럼 외부가 고르는 입력이면 부팅 때 무작위 시드를 섞거나(universal hashing), 최악이 보장되는 정렬판을 쓴다.

</details>

<details><summary>Q4. 측정했더니 O(n) 해시의 기울기가 1.3 이에요. 분석이 틀린 건가요?</summary>

아니다. Big-O 는 연산 수를 세는데 시간은 "연산 수 × 연산 하나의 값" 이고, 무작위 접근 하나의 값은 테이블이 L1 → L2 → DRAM 으로 커지며 0.8 → 7.7 ns 로 10배쯤 오른다(이 맥, 적재율 고정 스윕). 확인하는 법: 연산 수(탐사 수)를 따로 세어 그 기울기가 1 인지 보고, 시간 기울기와의 차이를 메모리 계층 탓으로 돌린다. 캐시가 없는 Cortex-M4 의 SRAM 이면 접근 비용이 일정해 이 효과는 거의 사라지고, D-캐시가 있는 Cortex-M7 에선 다시 나타난다.

</details>

<details><summary>Q5. qsort 는 O(n log n) 이 보장되나요? 안정적인가요? 스택은요?</summary>

C 표준은 qsort 의 알고리즘·복잡도·안정성 어느 것도 정하지 않는다(C11 7.22.5.2). 구현마다 다르고, 어떤 구현(glibc 의 일부 버전)은 내부에서 임시 버퍼를 malloc 하기도 한다고 알려져 있다(버전마다 다름 — 확인 필요). 펌웨어에서 최악 시간과 스택을 보장해야 하면 직접 쓴 힙 정렬(제자리, O(1) 스택, 불안정) 또는 bottom-up 머지 정렬(안정, n 원소 버퍼)을 쓴다. 중복 찾기엔 안정성이 필요 없다.

</details>

<details><summary>Q6. 비교 함수를 a - b 로 쓰면 왜 안 되죠? 값이 int 면요?</summary>

`uint32_t` 면 빼기가 2³² 로 감기고 int 변환이 구현 정의라 0 vs 0xFFFFFFFF 가 거꾸로 나온다. `int` 면 `INT_MIN - 1` 같은 오버플로가 **UB** 라 더 나쁘다. 어느 쪽이든 비교가 추이성을 잃으면 qsort 결과는 정렬이 아니고, 이 맥에서 실제로 6원소 입력의 중복을 놓쳤다. 표준 관용구는 `(a > b) - (a < b)`.

</details>

<details><summary>Q7. 이 검사를 ISR 이나 실시간 루프 안에서 해야 한다면요?</summary>

ISR 에선 "평균" 이 아니라 **최악 실행 시간** 이 중요하다. 해시는 기대 O(1) 이지만 덩어리가 길어지면 한 번의 삽입이 길어진다 → 적재율 상한을 낮게 고정하고, 탐사 길이 상한을 두어 넘으면 "판정 보류" 로 태스크에 넘긴다. 정렬판은 전체를 모은 뒤에만 쓸 수 있어 ISR 용이 아니다. 비트맵은 삽입 1번이 상수 시간(워드 읽기·OR·쓰기)이라 ISR 에 가장 잘 맞는다. 공유 테이블이면 ISR 과 태스크 사이 접근을 임계 구역으로 막는다(C-M14 참고).

</details>

<details><summary>Q8. 나눗셈이 없는 Cortex-M0 에서 해시 인덱스는 어떻게 계산하나요?</summary>

cap 을 2의 거듭제곱으로 두고 `% cap` 대신 `& (cap − 1)` 또는 곱셈 해시의 상위 비트(`>> (32 − k)`)를 쓴다. ARMv6-M 엔 나눗셈 명령이 없어 `%` 는 라이브러리 함수 호출(`__aeabi_uidivmod`)이 된다. 곱셈 MULS 는 M0 구현 옵션에 따라 1 또는 32 사이클 — 32 사이클 곱셈기라면 시프트-XOR 해시가 더 싸다. 2의 거듭제곱 판별·올림은 ALGO-B12.

</details>

## 9. 추가 연습

### 연습 1 — 최소제곱 기울기와 "1.08" 의 정체

`fit_slope(x, y, m)` 을 짜고, 본문의 1-2-5 격자(10³~10⁷)에서 정확한 데이터 3n², n log₂ n, 20n + 50000(고정 비용 50 µs)의 기울기를 구하라. n log n 은 모든 점과 n ≥ 10⁴ 점 둘 다.

<details><summary>답 보기</summary>

기울기 p = (mΣxy − ΣxΣy) / (mΣx² − (Σx)²), x = ln n, y = ln t. 실제 출력:

```text
ex1 slopes: 3n^2 2.0000 | n log n all 1.0904, n>=1e4 1.0805 | 20n+50000 all 0.8991, n<=1e4 0.5535
```

상수 3 은 절편으로 가서 기울기에 안 나온다. n log n 은 국소 기울기 1 + 1/ln n 의 평균이라 1.08~1.09 — 측정한 qsort(1.08~1.12)와 같은 자리다. 고정 비용은 작은 n 의 기울기를 0.55 까지 깎는다: 작은 n 만으로 복잡도를 읽으면 안 된다.

</details>

### 연습 2 — 시간 대신 연산을 센다

쌍 비교의 비교 횟수와, qsort 비교 함수가 불린 횟수를 세라. 쌍 비교는 정확히 n(n−1)/2 인지(중복이 마지막 쌍일 때도), 첫 쌍이면 1 인지, qsort 는 호출 수 / (n log₂ n) 이 n = 10³~10⁶ 에서 거의 일정한지 확인하라.

<details><summary>답 보기</summary>

전역 카운터를 비교 함수 안에서 올린다. 실제 출력 (`-O2`):

```text
ex2 qsort n=   1000: comparator calls      10163 = 1.020 * n log2 n
ex2 qsort n=  10000: comparator calls     134451 = 1.012 * n log2 n
ex2 qsort n= 100000: comparator calls    1740106 = 1.048 * n log2 n
ex2 qsort n=1000000: comparator calls   20689858 = 1.038 * n log2 n
```

연산 수는 잡음이 없다 — 시간의 기울기 1.1 이 "n log n 이라서" 인지 확인하는 가장 깨끗한 방법. ASan 빌드에서는 호출 수가 정확히 n − 1 번씩 더 많았다(sanitizer 의 qsort 가로채기(interceptor)가 끝난 뒤 이웃 쌍을 한 번 더 보는 것으로 보임 — 소스는 확인 안 함).

</details>

### 연습 3 — 어떤 고정 해시에도 적이 있다

cap 4096(k = 12)에 키 2000개를 선형 탐사로 넣으며 "찬 칸을 본 횟수" 를 세라. (a) `h(x) = x & mask` 에 키 (i+1)·2¹², (b) 피보나치 해시에 키 (i+1)·A⁻¹ mod 2³² — A⁻¹ 은 Newton 반복 `x ← x(2 − A·x)` 로. 서로 바꿔 넣고 무작위 키와도 비교하라.

<details><summary>답 보기</summary>

(b) 는 x·A ≡ i+1 이라 상위 12비트가 모두 0 → 전부 칸 0. 실제 출력:

```text
ex3 probes (n=2000, cap=4096): mask-hash on mask-adversary 1999000, fib-hash on fib-adversary 1999000 (= n(n-1)/2 = 1999000); fib on mask-adv 14, mask on fib-adv 0, fib on random 975
```

각자의 적에게는 정확히 n(n−1)/2, 남의 적에게는 거의 0. Newton 반복은 홀수 a 에 대해 a·a ≡ 1 (mod 8) 에서 시작해 정확한 비트 수가 매번 2배(3 → 6 → 12 → 24 → 48)라 4번이면 32비트 역원이다.

</details>

### 연습 4 — 쓸 메모리가 0 일 때: n+1 개 값이 [1, n] (LeetCode 287 *Find the Duplicate Number*)

배열을 읽기만 하고 추가 메모리 O(1) 로 반복되는 값(여러 번 나와도 됨)을 찾아라. 무작위 2000개를 표시 배열 기준과 대조하라.

<details><summary>답 보기</summary>

i → a[i] 를 간선으로 보면 0 에서 출발한 경로는 반드시 사이클에 들어가고(값이 [1, n] 이라 0 으로는 안 돌아온다), 사이클 입구가 들어오는 간선이 둘인 값 = 중복이다. Floyd: 느린 포인터 1칸, 빠른 포인터 2칸으로 만나게 한 뒤, 하나를 0 으로 되돌려 둘 다 1칸씩 → 만나는 곳이 입구. O(n) 시간, 변수 2개.

```c
uint32_t slow = a[0], fast = a[a[0]];
while (slow != fast) { slow = a[slow]; fast = a[a[fast]]; }
slow = 0;
while (slow != fast) { slow = a[slow]; fast = a[fast]; }
return slow;
```

전제(값 범위 [1, n], 원소 n+1 개)가 깨지면 범위 밖 읽기다 — 면접에서 전제를 먼저 확인한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 14/14, 보고 없음
```

## 10. 핵심 정리

- 로그-로그 그래프의 기울기 = 지수. 넓은 n 범위(10³~10⁷) + 최소제곱으로 읽는다. 이웃 두 점의 국소 기울기는 0.66~2.14 까지 흔들린다 (실행 4 해시).
- 이 맥 실측(n ≥ 10⁴): 쌍 비교 1.98~2.06, qsort 1.08~1.12 (이론 1 + 1/ln n ≈ 1.08), 해시 1.31~1.37 (캐시 탓; 원소당 0.8 → 7.7 ns).
- 쌍 비교 n = 10⁵ = 1.16 s → 10⁷ 외삽 약 3.1 시간. 해시 10⁷ = 72.5 ms, qsort 10⁷ = 799 ms.
- 메모리: 쌍 비교 0, 정렬 복사본 4n, 해시 4·cap ∈ [8n, 16n) (10⁷ → 128 MiB, RSS 로도 확인). 비트맵 L/8 B (값 범위가 알려지면 최강).
- 해시 구현 함정: 키 0 = 빈 칸 표시 → 플래그, 빈 칸 1개 이상 유지(무한 루프), cap 2의 거듭제곱 + 곱셈 해시(나눗셈 없음), 고정 해시엔 항상 적이 있다(최악 Θ(n²)).
- qsort 비교 함수는 `(a > b) - (a < b)`. 표준은 qsort 의 복잡도·안정성·스택을 보장하지 않는다.
- 측정 규칙: 워밍업 버리기, 20 ms 이상 반복, 중앙값, volatile sink, 해상도 확인(이 맥 CLOCK_MONOTONIC 1 µs), ns 보다 비율.
- 면접: n → 허용 복잡도 표 + **RAM 열**. 64 KB MCU 면 해시는 ID 수천 개에서 끝 → 비트맵 / 제자리 힙 정렬 / 쌍 비교.

## 참고자료

- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms* (3판), 3장 Growth of Functions; 11.3.2 곱셈 방법(Knuth 의 A ≈ (√5 − 1)/2 제안); 11.4 오픈 어드레싱.
- D. E. Knuth, *The Art of Computer Programming* Vol. 3, §6.4 Hashing — 곱셈(피보나치) 해싱, 선형 탐사 기대 탐사 수 ½(1 + 1/(1−α)), ½(1 + 1/(1−α)²).
- ISO/IEC 9899:2011 (C11) 7.22.5.2 `qsort` (알고리즘·복잡도 규정 없음), 6.3.1.3 정수 변환(범위 밖 → 구현 정의).
- macOS `man clock_gettime` (`CLOCK_MONOTONIC_RAW`), `man getrusage` (macOS `ru_maxrss` = 바이트), Linux `man 2 getrusage` (`ru_maxrss` = 킬로바이트).
- Arm, *Cortex-M0+ Technical Reference Manual* — MULS 1 또는 32 사이클(구현 옵션), ARMv6-M 나눗셈 명령 없음.
- LeetCode 217 *Contains Duplicate*, 219 *Contains Duplicate II* (거리 k 이내 중복 — 슬라이딩 윈도 + 해시 삭제, ALGO-B10 과 DS-B11 의 조합), 287 *Find the Duplicate Number* (연습 4).
- AlgoMonster 노트 M01 §6 Math Basics, §7 Runtime to Algo Cheat Sheet (Don 정리본) — 입력 크기로 알고리즘 역추론.
- 관련 레슨: DS-B09 (해시 함수 분포), DS-B11 (오픈 어드레싱·tombstone), DS-M08 (리사이즈·Robin Hood), DS-A11 (캐시 친화 레이아웃), ALGO-B05~B08 (정렬 직접 구현), ALGO-B06 (퀵소트 최악), ALGO-B10 (슬라이딩 윈도), ALGO-B12 (2의 거듭제곱 비트 트릭), ALGO-A12 (4 KB RAM 외부 정렬), C-M14 (ISR 공유 데이터).

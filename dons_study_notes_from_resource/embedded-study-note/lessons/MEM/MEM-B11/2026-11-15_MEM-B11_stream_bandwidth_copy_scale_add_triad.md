---
id: MEM-B11
time: 70분
updated: 2026-11-15
---
# MEM-B11 메모리 대역폭 측정: STREAM Copy/Scale/Add/Triad

## 학습 목표

- STREAM 의 네 커널(Copy · Scale · Add · Triad)을 정의대로 직접 짜고, STREAM 과 같은 방식(스칼라로 같은 순서를 재생해 평균 상대 오차 ≤ 1e-13)으로 결과를 검증한다.
- STREAM 의 **바이트 계산 규약**(Copy/Scale 16 B, Add/Triad 24 B per 원소, write-allocate 미포함)과 쓰기 할당(write-allocate, RFO)을 넣은 "실제 버스 트래픽"(×3/2, ×4/3)을 구분해 말한다.
- 배열 크기 규칙(배열 하나 ≥ 4 × LLC, ≥ 1 M 원소)을 이 맥(M6)의 캐시 가정으로 계산하고, 규칙을 어기면 "캐시 대역폭"을 재게 됨을 실측 곡선으로 보인다.
- 스레드 1 ~ 12 개로 best-of-N GB/s 를 재고, 피크(Apple 공표 170 GB/s) 대비 효율, 포화 스레드 수, 측정 노이즈(load avg)를 숫자로 보고한다.
- write-allocate 가설을 "보고값 × 2 가 피크를 넘는가" 같은 상한 논증과 커널 간 비율로 판별한다.

## 1. 문제

새 보드 bring-up 에서 DDR 트레이닝이 끝나면 가장 먼저 묻는 숫자가 "스펙 대비 대역폭이 몇 % 나오냐" 이고, 면접에서도 "대역폭을 어떻게 재고, 왜 피크보다 낮은가" 는 단골 질문이다.

STREAM (John McCalpin) 은 이 질문의 사실상 표준 벤치마크다. 단순한 루프 네 개지만, 제대로 재려면 함정이 여럿 있다.

1. **배열이 캐시에 들어가면** 메모리가 아니라 캐시 대역폭을 잰다 → 배열 크기 규칙.
2. **컴파일러가 루프를 바꾸거나 지우면** 엉뚱한 것을 잰다 → 결과 검증 + `clang -S` 확인. (이 레슨에서 실제로 Copy 루프가 `memcpy` 호출로 바뀌었다.)
3. **바이트를 어떻게 세는가** — STREAM 은 프로그램이 읽고 쓰는 바이트만 센다. 쓰기 할당 캐시는 쓰기 전에 라인을 읽어 오므로(RFO, read for ownership) 버스 트래픽은 더 많을 수 있다.
4. **스레드 하나로는 포화가 안 될 수 있다** — 날아다니는 요청 수(Little 의 법칙)가 코어당 제한되기 때문이다.

Don 의 SSD 경험과 같은 구조다: NVMe 순차 읽기 대역폭을 잴 때도 (1) 호스트 페이지 캐시에 맞으면 가짜 숫자, (2) 큐 깊이(QD)가 낮으면 포화 실패, (3) "호스트가 본 바이트"와 "NAND 에 실제로 쓴 바이트"(WAF)가 다르다. DRAM 에서는 그 셋이 캐시 · Little · RFO 다.

**이 레슨의 정의 (STREAM 규약 / Apple 공표값 — 실제 부품은 데이터시트 확인)**

| 항목 | 값 | 출처 · 상태 |
|---|---|---|
| 커널 | Copy `c=a`, Scale `b=q·c`, Add `c=a+b`, Triad `a=b+q·c` (이 순서로 한 반복) | stream.c 와 같은 정의, q = 3.0 |
| 초기값 | a = 1, b = 2, c = 0, 그다음 a = 2·a (stream.c 의 타이머 보정 단계) | stream.c |
| 바이트 / 원소 | Copy · Scale 16 B, Add · Triad 24 B — **RFO 미포함** | stream.c |
| 반복 · 보고 | ntimes 번, 첫 반복 버림, best rate = 바이트 / 최소 시간, GB/s = 10^9 B/s | stream.c 와 같음 |
| 배열 크기 규칙 | 배열 하나 ≥ 4 × (LLC 총합) 바이트, 그리고 ≥ 1,000,000 원소 | stream.c 주석 (1 M 하한은 이 레슨의 정의) |
| 이 맥 | Apple M6, 12 코어 = Super 2 + Performance 4 + Efficiency 6, 32 GB, 라인 128 B, 페이지 16 KB | `sysctl`, `system_profiler` |
| LLC 가정 | L2 20 + 20 + 8 MiB (공유 여부 미확인 → 합) + SLC 32 MiB (**sysctl 에 없음, 가정**) = **80 MiB** | 가정 — 아래 그림 4 로 점검 |
| 피크 | **170 GB/s** ("up to 170GB/s of unified memory bandwidth") | Apple Newsroom 2026-08 (직접 확인) |
| 메모리 종류 · 폭 | 공표 안 됨. 170 GB/s ≈ 10667 MT/s × 128-bit = 170,672 MB/s 로 **추정** (LPDDR5X-10667 가정) | 추정 — 공식 수치 확인 권장 |
| 아이템 원문의 M2 | LPDDR5-6400 × 128-bit = 102,400 MB/s (Apple 표기 "100GB/s") | 참고용 |

이 맥의 캐시 크기 (`sysctl -a | grep -E 'perflevel[0-9].*(l1d|l2)cache|cachelinesize|pagesize'` 실제 출력):

```text
vm.native_hw_pagesize: 16384
vm.pagesize: 16384
debug.vm_mixed_pagesize_supported: 1
hw.perflevel0.l1dcachesize: 131072
hw.perflevel0.l2cachesize: 20971520
hw.perflevel1.l1dcachesize: 65536
hw.perflevel1.l2cachesize: 20971520
hw.perflevel2.l1dcachesize: 98304
hw.perflevel2.l2cachesize: 8388608
hw.pagesize: 16384
hw.pagesize32: 16384
hw.cachelinesize: 128
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">그림 1 — 한 원소당 메모리 트래픽: STREAM 이 세는 것 vs write-allocate 캐시의 실제 버스</text>
  <rect x="300" y="32" width="14" height="12" class="f-ok" stroke="currentColor"/><text x="318" y="43" fill="currentColor" font-size="12">읽기 8 B</text>
  <rect x="390" y="32" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="408" y="43" fill="currentColor" font-size="12">쓰기 8 B</text>
  <rect x="480" y="32" width="14" height="12" class="f-warn" stroke="currentColor" stroke-dasharray="3 2"/><text x="498" y="43" fill="currentColor" font-size="12">RFO 8 B (쓰기 전 라인 읽기)</text>
  <text x="200" y="72" fill="currentColor" font-size="12">a[i]</text><text x="270" y="72" fill="currentColor" font-size="12">b[i]</text><text x="340" y="72" fill="currentColor" font-size="12">c[i]</text>
  <text x="440" y="72" fill="currentColor" font-size="12">STREAM</text><text x="520" y="72" fill="currentColor" font-size="12">+RFO</text><text x="600" y="72" fill="currentColor" font-size="12">배율</text>
  <text x="10" y="100" fill="currentColor">Copy  c = a</text>
  <rect x="190" y="84" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="330" y="84" width="50" height="24" class="f-acc" stroke="currentColor"/><rect x="384" y="84" width="24" height="24" class="f-warn" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="446" y="101" fill="currentColor" font-family="monospace">16</text><text x="524" y="101" fill="currentColor" font-family="monospace">24</text><text x="600" y="101" fill="currentColor" font-family="monospace">3/2</text>
  <text x="10" y="150" fill="currentColor">Scale b = q·c</text>
  <rect x="260" y="134" width="50" height="24" class="f-acc" stroke="currentColor"/><rect x="290" y="162" width="24" height="10" class="f-warn" stroke="currentColor" stroke-dasharray="3 2"/>
  <rect x="330" y="134" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <text x="446" y="151" fill="currentColor" font-family="monospace">16</text><text x="524" y="151" fill="currentColor" font-family="monospace">24</text><text x="600" y="151" fill="currentColor" font-family="monospace">3/2</text>
  <text x="10" y="200" fill="currentColor">Add   c = a + b</text>
  <rect x="190" y="184" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="260" y="184" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="330" y="184" width="50" height="24" class="f-acc" stroke="currentColor"/><rect x="384" y="184" width="24" height="24" class="f-warn" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="446" y="201" fill="currentColor" font-family="monospace">24</text><text x="524" y="201" fill="currentColor" font-family="monospace">32</text><text x="600" y="201" fill="currentColor" font-family="monospace">4/3</text>
  <text x="10" y="250" fill="currentColor">Triad a = b + q·c</text>
  <rect x="190" y="234" width="50" height="24" class="f-acc" stroke="currentColor"/><rect x="220" y="262" width="24" height="10" class="f-warn" stroke="currentColor" stroke-dasharray="3 2"/>
  <rect x="260" y="234" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="330" y="234" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <text x="446" y="251" fill="currentColor" font-family="monospace">24</text><text x="524" y="251" fill="currentColor" font-family="monospace">32</text><text x="600" y="251" fill="currentColor" font-family="monospace">4/3</text>
  <text x="10" y="292" fill="currentColor" font-size="12">flop/원소: Copy 0, Scale 1, Add 1, Triad 2 (FMA 1 개) → Triad 산술 강도 2/24 = 1/12 flop/B (연습 4)</text>
</svg>
```

### Part A — 커널 · 검증 · 바이트 규약

- `stream_kernel(k, a, b, c, q, lo, hi)`: 커널 k 를 원소 구간 [lo, hi) 에 한 번. 스레드가 자기 구간만 돌리게 하려고 구간을 받는다.
- `stream_expected(ntimes, q, &aj, &bj, &cj)`: STREAM 의 `checkSTREAMresults` 처럼 스칼라 하나로 같은 순서를 재생한 기대값.
- `stream_check(a, b, c, n, ntimes, q, eps)`: 배열마다 평균 |x − 기대값| / 기대값 > eps 이면 그 배열 비트를 켠 마스크 (a = 1, b = 2, c = 4). 결함 주입: 한 원소 오염, "지워진" Add, 1 ulp 오차(통과해야 함).
- `stream_bytes(k, n)` 과 `actual_bytes(k, n, write_allocate)`.

### Part B — 배열 크기 규칙과 스레드 분할

- `stream_min_elems(llc_bytes, elem)` = max(⌈4·LLC / elem⌉, 1,000,000).
- `chunk_range(n, t, k, align, &lo, &hi)`: [0, n) 을 t 개의 **연속** 구간으로 정확히 한 번씩 덮고, 내부 경계를 align 원소(128 B 라인 = 16 double)의 배수로 맞춘다. t = 1..12, 어색한 n (0, 1, 소수, 2^20 + 3 ...) 과 무작위 2000 경우를 전부 검사한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">그림 2 — n = 1000, t = 3 분할: 경계를 128 B 라인(16 double)에 맞추면 공유 라인이 사라진다</text>
  <text x="10" y="64" fill="currentColor">정렬 안 함</text>
  <rect x="100" y="48" width="206.5" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="306.5" y="48" width="206.5" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="513" y="48" width="207" height="26" class="f-mut" stroke="currentColor"/>
  <text x="170" y="66" fill="currentColor" font-size="12">T0 [0,333)</text><text x="370" y="66" fill="currentColor" font-size="12">T1 [333,666)</text><text x="580" y="66" fill="currentColor" font-size="12">T2 [666,1000)</text>
  <rect x="298.4" y="78" width="9.9" height="12" class="f-warn" stroke="currentColor"/><rect x="506.7" y="78" width="9.9" height="12" class="f-warn" stroke="currentColor"/>
  <text x="250" y="106" fill="currentColor" font-size="12">라인 20 (원소 320–335)</text><text x="470" y="106" fill="currentColor" font-size="12">라인 41 (656–671)</text>
  <text x="10" y="146" fill="currentColor">16 배수로 내림</text>
  <rect x="100" y="130" width="198.4" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="298.4" y="130" width="208.3" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="506.7" y="130" width="213.3" height="26" class="f-mut" stroke="currentColor"/>
  <text x="160" y="148" fill="currentColor" font-size="12">T0 [0,320)</text><text x="360" y="148" fill="currentColor" font-size="12">T1 [320,656)</text><text x="575" y="148" fill="currentColor" font-size="12">T2 [656,1000)</text>
  <text x="10" y="196" fill="currentColor" font-size="12">라인 20 확대 (정렬 안 함):</text>
  <g stroke="currentColor">
    <rect x="200" y="182" width="26" height="22" class="f-ok"/><rect x="226" y="182" width="26" height="22" class="f-ok"/><rect x="252" y="182" width="26" height="22" class="f-ok"/><rect x="278" y="182" width="26" height="22" class="f-ok"/><rect x="304" y="182" width="26" height="22" class="f-ok"/><rect x="330" y="182" width="26" height="22" class="f-ok"/><rect x="356" y="182" width="26" height="22" class="f-ok"/><rect x="382" y="182" width="26" height="22" class="f-ok"/><rect x="408" y="182" width="26" height="22" class="f-ok"/><rect x="434" y="182" width="26" height="22" class="f-ok"/><rect x="460" y="182" width="26" height="22" class="f-ok"/><rect x="486" y="182" width="26" height="22" class="f-ok"/><rect x="512" y="182" width="26" height="22" class="f-ok"/>
    <rect x="538" y="182" width="26" height="22" class="f-acc"/><rect x="564" y="182" width="26" height="22" class="f-acc"/><rect x="590" y="182" width="26" height="22" class="f-acc"/>
  </g>
  <text x="200" y="222" fill="currentColor" font-size="12" font-family="monospace">320 ............ 332 | 333 334 335</text>
  <text x="10" y="252" fill="currentColor" font-size="12">T0 가 13 개, T1 이 3 개를 쓴다 → 한 128 B 라인을 두 코어가 번갈아 소유 (false sharing, ARCH-M08)</text>
  <text x="10" y="276" fill="currentColor" font-size="12">경계 b_j = floor(n·j / t) 를 align 배수로 내림, b_0 = 0, b_t = n → 단조 증가라 빈틈 · 겹침이 없다.</text>
  <text x="10" y="296" fill="currentColor" font-size="12">대가: 구간 크기가 이상값 n/t 에서 최대 align−1 원소 벗어난다 (2^26 원소에서는 무시할 수준).</text>
  <text x="10" y="316" fill="currentColor" font-size="12">경계 라인은 공유되어도 결과는 맞다 — 다만 그 라인의 쓰기가 코어 사이를 오가 느려질 수 있다.</text>
</svg>
```

### Part C — 측정 요약 · 피크 대비 효율 · 실측

- `stream_summarize(secs, ntimes, bytes, &st)`: 첫 반복을 버리고 min/avg/max 시간, best GB/s = bytes / min. 잘못된 입력은 −1.
- `peak_mbs(mts, bus_bits)` (정수 MB/s), `efficiency_pct(gbs, peak)`, `saturation_threads(gbs[], nt, frac)` (최대값의 frac 이상에 처음 도달하는 스레드 수).
- `demo_stream.c` 로 이 맥에서 실제로: 스레드 1 ~ 12 스윕, 배열 크기 스윕, write-allocate / non-temporal 실험. 숫자는 5절에 실제 출력 그대로.

## 2. Problem (English)

You are bringing up a new SoC and need a trustworthy sustained-memory-bandwidth number. Implement the STREAM kernels and the deterministic pieces of a STREAM-style harness.

```c
enum { K_COPY, K_SCALE, K_ADD, K_TRIAD };
void     stream_kernel(int k, double *restrict a, double *restrict b, double *restrict c,
                       double q, size_t lo, size_t hi);       /* one pass over [lo,hi) */
void     stream_expected(int ntimes, double q, double *aj, double *bj, double *cj);
unsigned stream_check(const double *a, const double *b, const double *c, size_t n,
                      int ntimes, double q, double eps);      /* bit0 a, bit1 b, bit2 c; 0 = valid */
uint64_t stream_bytes(int k, uint64_t n);                     /* STREAM convention, no RFO */
uint64_t actual_bytes(int k, uint64_t n, bool write_allocate);
uint64_t stream_min_elems(uint64_t llc_bytes, size_t elem);   /* max(ceil(4*LLC/elem), 1e6) */
void     chunk_range(size_t n, unsigned t, unsigned k, size_t align, size_t *lo, size_t *hi);
int      stream_summarize(const double *secs, int ntimes, uint64_t bytes, stream_stat *out);
uint64_t peak_mbs(uint32_t mts, uint32_t bus_bits);
unsigned saturation_threads(const double *gbs, unsigned nt, double frac);
```

Constraints: kernels are exactly `c=a; b=q*c; c=a+b; a=b+q*c` in that order. Arrays start at a=1, b=2, c=0, then a=2a. `chunk_range` must cover [0,n) exactly once with contiguous chunks; interior boundaries must be multiples of `align`; `t == 0` or `k >= t` yields an empty range. No dynamic allocation in the tested code.

Examples:

```text
stream_expected(1, 3.0)            -> a=30 b=6 c=8
stream_expected(10, 3.0)           -> a=1153300781250 (= 2*15^10, exact in double)
stream_bytes(K_TRIAD, 1e7)         -> 240000000;  actual_bytes(K_TRIAD, 1e7, true) -> 320000000
stream_min_elems(8 MiB, 8)         -> 4194304 (stream.c example: 8 MB L3 -> >= 4 million)
chunk_range(1000, 3, 1, 16)        -> [320, 656)
stream_summarize({.050,.020,.016,.018}, 4, 24*2^26) -> min .016 s, avg .018 s, best 100.66 GB/s
peak_mbs(6400, 128)                -> 102400   (LPDDR5-6400, 128-bit: "100 GB/s")
saturation_threads({40,75,100,115,118,119,117,118}, 8, 0.90) -> 4
```

Follow-ups: Why must arrays be ≥ 4× LLC? Why report the best and not the mean? Why can one core not saturate DRAM? Your Copy reports 129 GB/s on a 170 GB/s part — is the write-allocate hypothesis consistent with that?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 맥: 27.2 SDK 링크 깨짐
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 13/44 passed, exit 1
```

TODO 는 11 개 함수 본문이다. 스텁은 안전한 오답(0, 빈 구간, −1)을 돌려주므로 루프가 끝나고 충돌하지 않는다.

## 4. 힌트

<details><summary>힌트 1 — 기대값은 "스칼라 하나"로 충분하다</summary>

배열의 모든 원소가 같은 값에서 시작하고 같은 연산을 받으므로, 원소 하나를 스칼라로 따라가면 된다. 순서가 중요하다: Copy 가 c 를 덮고, Scale 이 그 c 를 읽고, Add 가 c 를 다시 덮고, Triad 가 새 c 를 읽는다. 한 반복에서 a 는 정확히 15 배가 된다 (q = 3).

</details>

<details><summary>힌트 2 — 검증은 "평균 상대 오차"</summary>

stream.c 는 배열마다 Σ|x − 기대값| / n 을 구하고, 그것을 기대값으로 나눈 값이 epsilon (double 은 1e-13) 보다 크면 실패로 본다. n == 0 이면 검증할 수 없으므로 전부 실패(7)로 둔다.

</details>

<details><summary>힌트 3 — 바이트는 "프로그램이 만지는 배열 수 × 8"</summary>

Copy: a 읽기 + c 쓰기 = 16 B. Triad: b, c 읽기 + a 쓰기 = 24 B. write-allocate 면 쓰기 대상 라인을 먼저 읽으므로 쓰기 하나당 8 B 가 더해진다.

</details>

<details><summary>힌트 4 — 분할 경계 공식</summary>

b_j = ⌊n·j / t⌋ (j = 1..t−1) 를 `x -= x % align` 로 내리고, b_0 = 0, b_t = n 로 고정한다. 스레드 k 의 구간은 [b_k, b_{k+1}). n·j 는 64 비트로 곱해 넘침을 막는다.

</details>

<details><summary>힌트 5 — 요약과 포화</summary>

`stream_summarize`: k = 1 부터만 본다 (첫 반복 = 페이지 폴트 · 캐시 워밍업). 0 이하의 시간이 있으면 −1. `saturation_threads`: 먼저 최대값을 구하고, 그 frac 배 이상인 첫 인덱스 + 1.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **무엇을 재는지 정의**: "지속(sustained) 대역폭 = 캐시에 안 들어가는 배열을 순차로 훑을 때 초당 바이트. STREAM 규약으로 세고, RFO 는 따로 말한다."
2. **크기 규칙**: 배열 하나 ≥ 4 × LLC. 이 칩의 LLC 를 모르면 가정을 밝히고 크기 스윕으로 점검한다.
3. **정확성**: 루프가 지워지거나 바뀌지 않았는지 — 결과 검증 + 어셈블리.
4. **스레드 · 반복**: best-of-N, 첫 반복 제외, 스레드 1..코어 수, 고정(pinning) 가능 여부.
5. **해석**: 피크 대비 %, 포화 스레드 수(Little), RFO 가설, 측정 당시 부하.

### Part A — 커널 순서와 기대값

q = 3, 초기 a = 2 (1 × 2), b = 2, c = 0 에서 한 반복을 따라가면:

| 반복 | Copy c = a | Scale b = 3c | Add c = a + b | Triad a = b + 3c |
|---|---|---|---|---|
| 1 | c = 2 | b = 6 | c = 8 | a = 6 + 24 = **30** |
| 2 | c = 30 | b = 90 | c = 120 | a = 90 + 360 = **450** |
| k | c = a | b = 3a | c = 4a | a = 3a + 12a = **15a** |

그래서 10 회 뒤 a = 2·15^10 = 1,153,300,781,250, b = 3·(2·15^9) = 230,660,156,250, c = 4·(2·15^9) = 307,546,875,000 (solution.c 출력과 같다). 이 값들은 모두 2^53 보다 작은 정수이고 중간 연산도 정수라 **반올림이 전혀 없다** → 테스트에서 `==` 로 비교해도 된다. (stream.c 가 epsilon 을 두는 이유는 일반적인 q 나 많은 반복에서 반올림이 생길 수 있어서다.)

결함 주입과 기대 결과:

| 주입 | 무엇을 흉내내나 | stream_check |
|---|---|---|
| A[999] = 0 | 분할 off-by-one 으로 마지막 원소를 아무도 안 씀 | 1 (a 만 실패) |
| 마지막 반복의 Add 생략 | 컴파일러/코드가 루프 하나를 건너뜀 | 1 \| 4 (c 가 틀리고, 그 c 를 읽은 a 도 틀림) |
| B[3] 를 1 ulp 올림 | 정상적인 반올림 차이 | 0 (평균 상대 오차 ≈ 2e-16/1000 ≪ 1e-13) |
| n = 0 | 검증 불가 | 7 |

**t = 1..8 × align 1/16 의 모든 분할**로 돌려도 결과가 같다 (16 경우 모두 통과). 분할이 원소를 빠뜨리거나 두 번 계산하면 이 검사가 잡는다.

### Part A — 바이트 규약과 write-allocate

STREAM 은 "프로그램이 요청한 바이트"를 센다. 하지만 write-back · write-allocate 캐시(ARCH-M06)에서 **캐시에 없는 라인에 쓰면**, 라인 전체를 먼저 메모리에서 읽어 와(RFO) 일부를 고친 뒤 나중에 써 돌려보낸다. 그러면 버스에는 쓰기 하나당 읽기 하나가 더 지난다.

| 커널 | 읽기 | 쓰기 | STREAM B | +RFO B | 실제/보고 |
|---|---|---|---|---|---|
| Copy, Scale | 1 | 1 | 16 | 24 | 3/2 |
| Add, Triad | 2 | 1 | 24 | 32 | 4/3 |
| Sum (읽기만) | 1 | 0 | 8 | 8 | 1 |
| Fill (쓰기만) | 0 | 1 | 8 | 16 | 2 |

그래서 RFO 가 있는 기계에서 STREAM 숫자는 버스가 실제로 옮긴 양보다 **작게** 나온다 (Copy 는 2/3, Triad 는 3/4). 반대로 이것을 이용하면 RFO 가 있는지 **판별**할 수 있다 (아래 실측).

### Part B — 왜 4 × LLC 인가, 이 맥에서는 얼마인가

배열이 캐시에 남아 있으면 다음 커널이 DRAM 대신 캐시에서 읽는다. 3 배열의 합이 LLC 보다 몇 배 크면, 한 커널이 지나간 뒤 다음 커널이 읽을 데이터의 대부분이 이미 쫓겨나 있다. stream.c 주석의 예: "One Xeon E3 with 8 MB L3 cache — STREAM_ARRAY_SIZE should be >= 4 million, giving an array size of 30.5 MB and a total memory requirement of 91.5 MB". 같은 주석이 10^6 과 2^20 차이는 신경 쓰지 않는다고 하므로 실제 하한은 약 3.8 × 캐시다.

| LLC 가정 | ⌈4·LLC / 8⌉ | 1 M 하한 적용 | 배열 하나 |
|---|---|---|---|
| 1 MiB | 524,288 | **1,000,000** | 7.6 MiB |
| 8 MiB (stream.c 예) | **4,194,304** | 4,194,304 | 32 MiB |
| 80 MiB (이 맥 가정) | **41,943,040** | 41,943,040 | 320 MiB |

demo 기본 N = 2^26 = 67,108,864 원소 = 배열 512 MiB (총 1.5 GiB) 로 가정치보다 여유 있게 크다. SLC 크기는 공개되지 않아 가정이므로, 크기 스윕(그림 4)에서 "곡선이 바닥에 닿는 크기"가 가정보다 작은지 확인한다.

### Part B — 분할이 정확히 한 번 덮는 이유 (trace)

b_j = ⌊n·j/t⌋ 는 j 에 대해 단조 증가이고, align 배수로 내림해도 단조성은 유지된다 (x ≤ y ⇒ x − x mod a ≤ y − y mod a). b_0 = 0, b_t = n 이므로 [b_0, b_1), …, [b_{t−1}, b_t) 은 [0, n) 을 빈틈 · 겹침 없이 덮는다. 균형: 내림은 경계를 최대 align−1 만큼 옮기므로 구간 크기는 ⌊n/t⌋ − (align−1) 이상, ⌈n/t⌉ + (align−1) 이하.

| n = 1000, t = 3 | j = 1: 1000/3 = 333.3 | j = 2: 666.7 | 구간 |
|---|---|---|---|
| align = 1 | 333 | 666 | [0,333) [333,666) [666,1000) — 라인 20, 41 공유 |
| align = 16 | 333 − 13 = **320** | 666 − 10 = **656** | [0,320) [320,656) [656,1000) — 공유 없음 |

테스트: 16 개 n × t = 1..12 × align {1, 16} = 384 경우 + 무작위 2000 경우, 실패 0. 원소 단위로 세어 n = 1025, t = 7, align = 16 이 모든 원소를 정확히 한 번 덮는지도 본다. t > n 이면 앞쪽 스레드는 빈 구간을 받는다 (n = 1, t = 8 → 스레드 0..6 은 빈 구간, 7 이 [0,1)).

### Part C — 요약 규칙과 피크 계산

STREAM 은 반복마다 시간을 재고, 첫 반복을 버리고, **최소 시간**으로 best rate 를 낸다. 최소를 쓰는 이유: 측정을 방해하는 것(인터럽트, 다른 프로세스, 스케줄러가 스레드를 E 코어로 옮김)은 시간을 늘리기만 하고 줄이지는 못한다. 그래서 최소값이 "기계가 낼 수 있는 값"에 가장 가깝다. avg 도 같이 보고해 노이즈 크기를 보여 준다.

피크 = MT/s × 버스 바이트 (MEM-B04 계산기와 같은 식):

| 칩 | 가정 | peak_mbs | Apple 표기 |
|---|---|---|---|
| DDR4-3200 DIMM | 3200 × 8 B | 25,600 | — |
| M2 | LPDDR5-6400 × 128-bit | 102,400 | 100 GB/s |
| M5 | 9600 MT/s × 128-bit (추정) | 153,600 | 153 GB/s |
| M6 (이 맥) | 10667 MT/s × 128-bit (추정) | 170,672 | "up to 170 GB/s" |

효율은 Apple 이 공표한 170 GB/s 를 분모로 쓴다 (추정 170,672 와 0.4 % 차이).

### 실측 — 컴파일러가 만든 것부터 확인

```bash
clang -std=c11 -O2 -S demo_stream.c -o demo_stream.s      # 커널을 noinline 으로 둬서 이름으로 찾는다
```

| 커널 | -O2 결과 (실제 asm 에서) | -O2 -fno-builtin |
|---|---|---|
| k_copy | **`b _memcpy`** — 루프 전체가 memcpy 꼬리 호출로 바뀜 (loop idiom recognition) | `ldp q0,q1` / `stp q0,q1` 루프 (64 B/반복) |
| k_scale | `ldp q` + `fmul.2d` ×4 + `stp q` | 같음 |
| k_add | `ldp q` ×4 + `fadd.2d` + `stp q` | 같음 |
| k_triad | `ldp q` ×4 + `fmla.2d` ×4 + `stp q` | 같음 |
| k_fill | **`bl _memset_pattern16`** | `stp q0, q0` 루프 |
| k_fill_nt / k_copy_nt | `stnp d0, d1` (`__builtin_nontemporal_store` → 비시간적 store pair) | 같음 |
| k_sum (처음 판) | `ldp q` 로 읽고 **스칼라 `fadd d` 사슬 4 개** — FP 덧셈은 재결합 불가 | — |

두 가지를 배웠다. (1) "내가 짠 Copy 루프"는 실제로는 **libc memcpy** 를 잰다. STREAM 공식 코드도 같은 컴파일러면 같은 일이 생길 수 있다. 그래서 demo 를 `-fno-builtin` 으로 한 번 더 빌드해 비교했다. (2) 처음 짠 Sum 은 1 스레드 33 ~ 40 GB/s 에서 막혔다 — 메모리가 아니라 **FP add 지연**이 한계였다. 벡터 누산기 8 개(16 사슬)로 바꾼 뒤 `fadd.2d` 로 컴파일되고 1 스레드 67 GB/s 로 올랐다. 측정 커널이 정말 메모리 바운드인지 먼저 확인해야 한다.

루프가 지워지지 않았다는 증거: 각 스레드 수마다 `stream_check` 가 통과(`valid ok`)했고, Sum 결과는 sink 로 출력한다.

### 실측 — 스레드 스윕 (demo_stream [1])

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_stream.c -o demo_stream
./demo_stream 67108864 20 12      # 약 28 초 (실측 27.6 s)
```

실제 출력 (2026-11-15 17:44, 시작 시 `uptime` load averages: 19.70 21.54 18.05 — 다른 빌드 에이전트가 같이 돌고 있었다):

```text
MEM-B11 demo_stream: N = 67108864 doubles = 512 MiB/array (1.50 GiB total), ntimes = 20, hw.ncpu = 12
timer: CLOCK_MONOTONIC_RAW min nonzero delta = 41 ns
[load avg start: 19.70 21.54 18.05]
first-touch init (page faults incl.): 313.1 ms

[1] thread sweep — best GB/s over 19 timed iters (first dropped), STREAM byte counting
  t     Copy    Scale      Add    Triad | Triad avg | valid
  1     94.9     89.2     96.4     87.9 |      68.0 | ok
  2    105.7    109.2    103.2    109.4 |      77.9 | ok
  3    122.4    121.5    113.8    113.7 |      92.7 | ok
  4    128.9    112.8    111.9    112.7 |      94.7 | ok
  5    122.6    114.0    112.0    114.6 |      92.0 | ok
  6    120.6    108.7    113.1    113.2 |      90.8 | ok
  7    126.4    113.5    118.1    106.2 |      91.1 | ok
  8    123.3    111.0    110.8    111.6 |      97.2 | ok
  9    116.5     99.2    106.4    108.5 |      91.0 | ok
 10    122.3    107.4    111.2    109.8 |      91.5 | ok
 11    119.3    101.9    108.7    113.0 |      90.3 | ok
 12    120.7     93.6    102.6    108.1 |      82.2 | ok
[load avg after [1]: 19.44 21.37 18.05]
```

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">그림 3 — 스레드 수별 best GB/s (M6 이 맥 실측, load avg 19.7, 512 MiB × 3 배열)</text>
  <line x1="60" y1="270" x2="720" y2="270" stroke="currentColor"/><line x1="60" y1="270" x2="60" y2="40" stroke="currentColor"/><text x="22" y="274" fill="currentColor">0</text><line x1="56" y1="270.0" x2="60" y2="270.0" stroke="currentColor"/>
  <text x="22" y="213" fill="currentColor">50</text><line x1="56" y1="208.9" x2="60" y2="208.9" stroke="currentColor"/><text x="22" y="152" fill="currentColor">100</text><line x1="56" y1="147.8" x2="60" y2="147.8" stroke="currentColor"/>
  <text x="22" y="91" fill="currentColor">150</text><line x1="56" y1="86.7" x2="60" y2="86.7" stroke="currentColor"/><line x1="60" y1="62.2" x2="720" y2="62.2" stroke="currentColor" stroke-dasharray="6 4"/><text x="470" y="56.2" fill="currentColor">Apple 공표 피크 170 GB/s (up to)</text>
  <rect x="68" y="154.0" width="20" height="116.0" class="f-acc" stroke="currentColor"/><rect x="90" y="162.6" width="20" height="107.4" class="f-ok" stroke="currentColor"/><text x="82" y="286" fill="currentColor">1</text><rect x="123" y="140.8" width="20" height="129.2" class="f-acc" stroke="currentColor"/>
  <rect x="145" y="136.3" width="20" height="133.7" class="f-ok" stroke="currentColor"/><text x="137" y="286" fill="currentColor">2</text><rect x="178" y="120.4" width="20" height="149.6" class="f-acc" stroke="currentColor"/><rect x="200" y="131.0" width="20" height="139.0" class="f-ok" stroke="currentColor"/>
  <text x="192" y="286" fill="currentColor">3</text><rect x="233" y="112.5" width="20" height="157.5" class="f-acc" stroke="currentColor"/><rect x="255" y="132.3" width="20" height="137.7" class="f-ok" stroke="currentColor"/><text x="247" y="286" fill="currentColor">4</text>
  <rect x="288" y="120.2" width="20" height="149.8" class="f-acc" stroke="currentColor"/><rect x="310" y="129.9" width="20" height="140.1" class="f-ok" stroke="currentColor"/><text x="302" y="286" fill="currentColor">5</text><rect x="343" y="122.6" width="20" height="147.4" class="f-acc" stroke="currentColor"/>
  <rect x="365" y="131.6" width="20" height="138.4" class="f-ok" stroke="currentColor"/><text x="357" y="286" fill="currentColor">6</text><rect x="398" y="115.5" width="20" height="154.5" class="f-acc" stroke="currentColor"/><rect x="420" y="140.2" width="20" height="129.8" class="f-ok" stroke="currentColor"/>
  <text x="412" y="286" fill="currentColor">7</text><rect x="453" y="119.3" width="20" height="150.7" class="f-acc" stroke="currentColor"/><rect x="475" y="133.6" width="20" height="136.4" class="f-ok" stroke="currentColor"/><text x="467" y="286" fill="currentColor">8</text>
  <rect x="508" y="127.6" width="20" height="142.4" class="f-acc" stroke="currentColor"/><rect x="530" y="137.4" width="20" height="132.6" class="f-ok" stroke="currentColor"/><text x="522" y="286" fill="currentColor">9</text><rect x="563" y="120.5" width="20" height="149.5" class="f-acc" stroke="currentColor"/>
  <rect x="585" y="135.8" width="20" height="134.2" class="f-ok" stroke="currentColor"/><text x="577" y="286" fill="currentColor">10</text><rect x="618" y="124.2" width="20" height="145.8" class="f-acc" stroke="currentColor"/><rect x="640" y="131.9" width="20" height="138.1" class="f-ok" stroke="currentColor"/>
  <text x="632" y="286" fill="currentColor">11</text><rect x="673" y="122.5" width="20" height="147.5" class="f-acc" stroke="currentColor"/><rect x="695" y="137.9" width="20" height="132.1" class="f-ok" stroke="currentColor"/><text x="687" y="286" fill="currentColor">12</text>
  <text x="330" y="304" fill="currentColor">스레드 수 t (QoS USER_INTERACTIVE, 코어 고정 불가)</text><text x="12" y="34" fill="currentColor">GB/s</text><rect x="80" y="316" width="14" height="10" class="f-acc" stroke="currentColor"/><text x="98" y="326" fill="currentColor">Copy (memcpy 로 바뀜) — 최고 128.9 @ t=4</text><rect x="430" y="316" width="14" height="10" class="f-ok" stroke="currentColor"/><text x="448" y="326" fill="currentColor">Triad — 최고 114.6 @ t=5 (피크의 67 %)</text>
</svg>
```

읽는 법 (부하 중 측정 — 한가할 때 `demo_stream` 재실행 권장):

| 지표 | 값 | 피크 170 대비 |
|---|---|---|
| Copy best (memcpy) | 128.9 GB/s @ t = 4 | 75.8 % |
| Scale best | 121.5 @ t = 3 | 71.5 % |
| Add best | 118.1 @ t = 7 | 69.5 % |
| Triad best | 114.6 @ t = 5 | **67.4 %** |
| Triad, 스레드 1 개 | 87.9 | 51.7 % — 코어 **하나**가 피크의 절반 |
| 포화 스레드 수 (Triad ≥ 90 % of best) | t = 2 (109.4 ≥ 103.1) | — |

- **스레드 하나가 이미 88 GB/s.** 대형 OoO 코어 + 하드웨어 프리페처가 미스를 많이 겹쳐 띄운다. Little 의 법칙(연습 3): 88 GB/s × (DRAM 지연 약 100 ns 가정, MEM-B10 이 실측) / 128 B ≈ 69 라인이 동시에 날아다녀야 한다. 170 GB/s 를 채우려면 ≈ 133 라인 — 한 코어의 미스 처리 자원으로는 모자라 2 ~ 3 스레드에서 포화한다.
- **t ≥ 3 부터 평평하다**: 병목이 코어가 아니라 메모리 쪽으로 넘어갔다. t = 9 ~ 12 에서 살짝 떨어지는 것은 (a) 고정 분할 + 이기종 코어 — E 코어에 걸린 스레드가 늦게 끝나면 전체가 그 스레드를 기다린다, (b) load avg ~20 인 머신에서 12 스레드 중 하나라도 밀려나면 그 반복이 늦어진다. avg 열(82 ~ 97)과 best 의 차이가 그 노이즈다.
- **Copy 가 Triad 보다 높다**: Copy 는 memcpy 였다. `-fno-builtin` 빌드(직접 짠 ldp/stp 루프, 아래 표)는 Copy best 108.8 로 Triad 와 비슷했다.

같은 날 다른 두 번의 실행 (부하가 더 컸을 때 / `-fno-builtin`) 의 커널별 best (모든 t 중 최대):

| 실행 | load avg (시작) | Copy | Scale | Add | Triad |
|---|---|---|---|---|---|
| run 1, -O2 (17:39) | 27.72 | 112.7 | 107.2 | 103.7 | 102.5 |
| run 1, -O2 -fno-builtin | 29.42 | 99.9 | 101.7 | 103.9 | 103.3 |
| **run 2, -O2 (위 표)** | **19.70** | **128.9** | **121.5** | **118.1** | **114.6** |
| run 2, -O2 -fno-builtin | 19.66 | 108.8 | 108.1 | 113.8 | 115.5 |

Scale/Add/Triad 는 두 빌드에서 **같은 기계어**인데 실행마다 10 % 넘게 다르다 → 이 머신의 측정 오차는 ±10 % 수준이라고 보고해야 한다. 부하가 낮을수록 숫자가 올라가므로 위 효율(67 ~ 76 %)은 **하한**에 가깝다.

### 실측 — 크기 스윕 (demo_stream [2])

```text
[2] size sweep — Triad best GB/s; per-array size; reps chosen so one timing moves >= ~256 MB
  per-array     3 arrays   t=1     t=12
      16 KiB      48 KiB    94.9   263.3
      32 KiB      96 KiB   147.8   324.5
      64 KiB     192 KiB   139.9   232.6
     128 KiB     384 KiB   132.0   441.3
     256 KiB     768 KiB   137.6   394.4
     512 KiB    1536 KiB   134.1   327.7
       1 MiB       3 MiB   130.9   257.5
       2 MiB       6 MiB   150.5   191.2
       4 MiB      12 MiB   131.3   196.6
       8 MiB      24 MiB   106.6   344.6
      16 MiB      48 MiB    98.7   193.8
      32 MiB      96 MiB    92.3   117.4
      64 MiB     192 MiB    90.0   106.5
     128 MiB     384 MiB    90.8   120.4
     256 MiB     768 MiB    89.6   105.1
     512 MiB    1536 MiB    61.0    99.5
[load avg after [2]: 19.80 21.41 18.08]
```

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">그림 4 — Triad GB/s vs 배열 크기 (M6 실측, best of 19, load avg 19.8)</text>
  <line x1="70" y1="280" x2="720" y2="280" stroke="currentColor"/><line x1="70" y1="280" x2="70" y2="40" stroke="currentColor"/><text x="36" y="284" fill="currentColor">0</text><text x="36" y="234" fill="currentColor">100</text>
  <text x="36" y="184" fill="currentColor">200</text><text x="36" y="134" fill="currentColor">300</text><text x="36" y="84" fill="currentColor">400</text><rect x="420" y="40" width="80" height="240" class="f-mut" opacity="0.35"/>
  <text x="424" y="66" fill="currentColor">L2 20 MiB · SLC(?)</text><text x="424" y="80" fill="currentColor">를 넘는 구간</text><line x1="654" y1="40" x2="654" y2="280" stroke="currentColor" stroke-dasharray="5 4"/><text x="650" y="100" fill="currentColor" text-anchor="end">4×LLC 가정 (배열 320 MiB) →</text>
  <polyline points="80,148.3 120,117.8 160,163.7 200,59.3 240,82.8 280,116.2 320,151.2 360,184.4 400,181.7 440,107.7 480,183.1 520,221.3 560,226.8 600,219.8 640,227.4 680,230.2" fill="none" stroke="currentColor" stroke-width="2"/><polyline points="80,232.6 120,206.1 160,210.1 200,214.0 240,211.2 280,212.9 320,214.6 360,204.8 400,214.3 440,226.7 480,230.7 520,233.8 560,235.0 600,234.6 640,235.2 680,249.5" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/><circle cx="80" cy="148.3" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="120" cy="117.8" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="160" cy="163.7" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="200" cy="59.3" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="240" cy="82.8" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="280" cy="116.2" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="320" cy="151.2" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="360" cy="184.4" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="400" cy="181.7" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="440" cy="107.7" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="480" cy="183.1" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="520" cy="221.3" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="560" cy="226.8" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="600" cy="219.8" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="640" cy="227.4" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="680" cy="230.2" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="80" cy="232.6" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="120" cy="206.1" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="160" cy="210.1" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="200" cy="214.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="240" cy="211.2" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="280" cy="212.9" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="320" cy="214.6" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="360" cy="204.8" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="400" cy="214.3" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="440" cy="226.7" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="480" cy="230.7" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="520" cy="233.8" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="560" cy="235.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="600" cy="234.6" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="640" cy="235.2" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="680" cy="249.5" r="3.5" class="f-ok" stroke="currentColor"/><text x="68" y="296" fill="currentColor" font-size="12">16K</text><text x="108" y="296" fill="currentColor" font-size="12">32K</text>
  <text x="148" y="296" fill="currentColor" font-size="12">64K</text><text x="188" y="296" fill="currentColor" font-size="12">128K</text><text x="228" y="296" fill="currentColor" font-size="12">256K</text><text x="268" y="296" fill="currentColor" font-size="12">512K</text>
  <text x="308" y="296" fill="currentColor" font-size="12">1M</text><text x="348" y="296" fill="currentColor" font-size="12">2M</text><text x="388" y="296" fill="currentColor" font-size="12">4M</text><text x="428" y="296" fill="currentColor" font-size="12">8M</text>
  <text x="468" y="296" fill="currentColor" font-size="12">16M</text><text x="508" y="296" fill="currentColor" font-size="12">32M</text><text x="548" y="296" fill="currentColor" font-size="12">64M</text><text x="588" y="296" fill="currentColor" font-size="12">128M</text>
  <text x="628" y="296" fill="currentColor" font-size="12">256M</text><text x="668" y="296" fill="currentColor" font-size="12">512M</text><text x="250" y="316" fill="currentColor">배열 하나 크기 (B, 3 배열 총합은 ×3)</text><circle cx="90" cy="238" r="4" class="f-acc" stroke="currentColor"/><text x="100" y="242" fill="currentColor">t = 12 (실선) 512 MiB: 99.5</text>
  <circle cx="90" cy="258" r="4" class="f-ok" stroke="currentColor"/><text x="100" y="262" fill="currentColor">t = 1 (점선) 512 MiB: 61.0</text><text x="12" y="34" fill="currentColor">GB/s</text>
</svg>
```

- 총 3 배열이 L2(20 MiB) 안(≤ 12 MiB 총합)이면 1 스레드 130 ~ 150 GB/s, 12 스레드 190 ~ 440 GB/s — 여러 코어의 L1/L2 를 합친 "캐시 대역폭"이다. 이 영역의 숫자를 "메모리 대역폭"이라 부르면 2 ~ 4 배 부풀린 것이다.
- 총합 24 ~ 48 MiB 에서 내려가기 시작해 **96 MiB 이상에서 평평**(1 스레드 ≈ 90, 12 스레드 ≈ 100 ~ 120) → 이 맥에서 "캐시 효과가 끝나는 지점"은 총 ~100 MiB 근처다. LLC 80 MiB 가정과 모순되지 않고, 4 × LLC 규칙(배열 하나 320 MiB)은 넉넉한 쪽이다.
- 작은 크기의 12 스레드 값은 들쭉날쭉하다 (128 KiB 441, 2 MiB 191): 구간이 작아 스레드 동기화 비용과 E 코어 배치가 지배한다. 해석하지 않는다.
- **설명 못 한 점**: 512 MiB 1 스레드 61.0 은 [1] 의 1 스레드 Triad(87.9) 와 [3] 의 83.4 보다 낮고, run 1 에서도 58.5 로 재현됐다. 차이는 [2] 가 512 MiB 를 재기 직전에 배열 후반부(256 ~ 512 MiB)를 몇 초간 건드리지 않았다는 점뿐이다. 메모리 압박으로 그 페이지가 압축/회수되었을 가능성을 의심하지만, `vm_stat` · minflt 로 확인하지 않았다 — 추측으로 남긴다.

### 실측 — write-allocate 실험 (demo_stream [3])

```text
[3] write-allocate experiment — reported GB/s (STREAM counting: read 8 B + write 8 B per elem, no RFO)
    saturation threads (>= 90% of best Triad) = 2
  op          B/elem   t=1 best   t=2 best  t=12 best
  Sum(read)        8       66.8      116.0     112.7
  Fill             8       71.4      117.1     114.4
  Fill-NT          8       88.5       87.0     123.1
  Copy            16       85.6      100.2     117.7
  Copy-NT         16       76.5       85.1     113.3
  Triad           24       83.4       83.3     100.1
[load avg end: 19.66 21.35 18.08]
```

`-fno-builtin` 빌드 (Fill = `stp q0,q0` 루프, Copy = `ldp/stp` 루프):

```text
[3] write-allocate experiment — reported GB/s (STREAM counting: read 8 B + write 8 B per elem, no RFO)
    saturation threads (>= 90% of best Triad) = 2
  op          B/elem   t=1 best   t=2 best  t=12 best
  Sum(read)        8       68.6       86.0     119.6
  Fill             8       94.2      103.4      89.9
  Fill-NT          8      117.7       96.6     104.8
  Copy            16       66.5       96.8     110.6
  Copy-NT         16       86.4       87.2     107.7
  Triad           24       79.9       89.7      99.1
[load avg end: 23.73 22.10 18.46]
```

**상한 논증 (가장 강한 증거).** RFO 가 있다면 Fill 의 실제 버스 트래픽은 보고값의 2 배, Copy 는 1.5 배다.

| 측정 | 보고 | RFO 가설의 실제 트래픽 | 피크 170 과 비교 |
|---|---|---|---|
| Fill (memset_pattern16), t = 2 | 117.1 | 234.2 | **초과 → 불가능** |
| Fill (stp 루프, -fno-builtin), t = 2 | 103.4 | 206.8 | **초과 → 불가능** |
| Copy (memcpy), [1] t = 4 | 128.9 | 193.4 | **초과 → 불가능** |
| Triad, [1] t = 5 | 114.6 | 152.8 | 넘지 않음 (판별 못 함) |

best-of-N 은 노이즈로 **낮아질** 수는 있어도 높아지기는 어렵다 (512 MiB 쓰기 중 캐시에 남아 측정 뒤로 미뤄지는 dirty 라인은 많아야 수십 MiB, 몇 % 이내). 그러므로 "큰 순차 쓰기마다 라인을 먼저 읽는다" 는 가설은 이 맥에서 **기각**된다. 라인 전체를 덮어쓰는 연속 store 를 하드웨어가 알아보고 읽기를 생략하는 것으로 보인다 — Apple 은 공개 문서가 없으므로 **측정으로 한 추론**이고, 부분 라인 쓰기나 흩어진 쓰기에서는 RFO 가 있을 수 있다 (MEM-M06 에서 카운터 없이 더 파고든다).

**비율 논증 (연습 1).** 버스가 포화일 때 Fill/Sum 은 RFO 면 1/2, 아니면 1. 측정: t = 2 117.1 / 116.0 = **1.01**, t = 12 114.4 / 112.7 = **1.02** → no-RFO 쪽. `-fno-builtin` 의 t = 12 (89.9 / 119.6 = 0.75) 는 중간값이라 노이즈로 본다 — 비율 논증은 노이즈에 약하고, 상한 논증이 더 튼튼하다.

**Non-temporal store (`stnp`).** Fill-NT vs Fill: 88.5 vs 71.4 (t = 1), 87.0 vs 117.1 (t = 2), 123.1 vs 114.4 (t = 12) — 방향이 일정하지 않고 ±10 % 노이즈 안이다. RFO 가 이미 없으니 NT store 가 줄일 트래픽도 없다는 위 결론과 맞는다. "이 맥에서 stnp 의 뚜렷한 이득은 관찰되지 않음" 이 정직한 보고다. (x86 에서는 `movntpd` 가 RFO 를 없애 STREAM 숫자를 올리는 것이 잘 알려진 효과다 — 그래서 STREAM 결과에는 컴파일 옵션을 같이 적는다.)

### 복잡도와 자원

| 부품 | 시간 | 메모리 |
|---|---|---|
| 커널 1 패스 | O(n), 원소당 16 ~ 24 B | 배열 3 개 (demo: 3 × 512 MiB) |
| stream_check | O(n) 한 번 | O(1) |
| chunk_range | O(1) | O(1) |
| demo_stream 전체 | 약 28 초 (12 스레드 × 4 커널 × 20 회 + 스윕) | 1.5 GiB |

`solution.c` 는 정적 배열 3 × 1000 원소만 쓰고 0.1 초 안에 끝난다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 44/44 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_stream.c -o demo_stream && ./demo_stream            # 측정 (약 30 초)
cc -std=c11 -Wall -Wextra -Werror -O2 -fno-builtin -pthread demo_stream.c -o demo_nb && ./demo_nb 67108864 20 12 13
cc -std=c11 -g -O1 -fsanitize=thread demo_stream.c -o demo_tsan && ./demo_tsan 65536 3 4 13             # TSan 경고 0
```

실제 출력 (`solution`):

```text
expected after 10 iters: a=1153300781250 b=230660156250 c=307546875000
  Copy  n=1e7: STREAM 160 MB, with write-allocate 240 MB
  Scale n=1e7: STREAM 160 MB, with write-allocate 240 MB
  Add   n=1e7: STREAM 240 MB, with write-allocate 320 MB
  Triad n=1e7: STREAM 240 MB, with write-allocate 320 MB
min elems for LLC 80 MiB: 41943040 (320 MiB per array)
partition cases: 2384, failures: 0
44/44 passed
```

## 7. 자주 하는 실수

**1. 배열이 캐시에 들어가는 크기로 잰다.** 그림 4 에서 총 3 MiB 배열은 12 스레드 257 GB/s — 피크 170 을 넘는 "메모리 대역폭"이 나온다. 피크를 넘는 숫자가 나오면 먼저 크기를 의심한다.

```c
size_t n = stream_min_elems(LLC_ASSUMED, sizeof(double));   /* 그리고 크기 스윕으로 가정 점검 */
```

**2. 결과를 검증하지 않는다.** 결과를 쓰지 않는 루프는 컴파일러가 지울 수 있고, 바꿀 수도 있다 (이 레슨: Copy → memcpy, Fill → memset_pattern16). 측정 뒤에 `stream_check` 를 돌리고, `clang -S` 로 커널이 무엇이 되었는지 본다.

**3. 첫 반복을 포함한다.** 첫 반복은 페이지 폴트(이 맥: 1.5 GiB first touch 313 ms — 한 커널 패스(약 14 ms)의 20 배 이상)와 TLB 워밍업을 포함한다. 첫 반복을 버리거나, 측정 전에 배열을 모두 써서 페이지를 만든다.

**4. 평균만 보고한다.** 부하가 있는 머신에서 평균은 "다른 프로세스의 양"을 잰다 (Triad t = 12: best 108.1, avg 82.2). best 와 avg 를 같이, 측정 당시 load avg 와 함께 적는다.

**5. 분할 경계가 캐시라인을 가른다.** 결과는 맞지만 경계 라인을 두 코어가 같이 써서 느려질 수 있다. 내부 경계를 라인(16 double) 배수로 맞춘다.

```c
if (align > 1) x -= x % align;   /* b_j 를 128 B 경계로 */
```

**6. 워커 스레드의 "본 세대" 를 0 으로 초기화한다.** 이 레슨의 demo 첫 판 버그: 스핀 풀을 다시 만들 때 새 작업자가 `seen = 0` 으로 시작해, 이미 증가해 있던 전역 세대 번호를 "새 일" 로 착각하고 옛 작업을 한 번 더 돌렸다 → t = 4 에서 `valid FAIL`. 풀을 만들 때의 세대를 넘겨준다.

```c
g_start_gen = atomic_load(&g_gen);           /* pool_start: pthread_create 전에 */
unsigned seen = g_start_gen;                 /* worker */
```

**7. STREAM 숫자를 "버스 트래픽" 으로 말한다.** RFO 가 있는 기계면 실제 트래픽은 3/2, 4/3 배다. 어느 쪽인지 모르면 "STREAM 규약 기준" 이라고 단위를 붙인다.

## 8. 면접 Q&A

<details><summary>Q1. STREAM 배열은 왜 LLC 의 4 배 이상이어야 하나요? 이 칩의 LLC 를 모르면요?</summary>

배열이 캐시에 남으면 다음 커널이 DRAM 대신 캐시에서 읽어 숫자가 부풀려진다. 3 배열 합이 LLC 보다 충분히 크면 다음 커널이 읽을 라인은 이미 쫓겨나 있다. stream.c 는 배열 하나 ≥ 4 × 캐시를 요구한다. 모르면 (1) 보이는 캐시(sysctl L2) + 공개 안 된 SLC 를 넉넉히 가정해 크기를 정하고, (2) 크기 스윕으로 곡선이 평평해지는 지점이 그보다 작은지 확인한다. 이 맥에서는 총 ~100 MiB 부터 평평했고 demo 는 배열 하나 512 MiB 를 썼다.

</details>

<details><summary>Q2. 왜 평균이 아니라 최소 시간(best rate)을 보고하나요?</summary>

방해 요인(인터럽트, 다른 프로세스, 스케줄러, E 코어 이동)은 시간을 늘리기만 한다. 최소값은 "기계가 해낸 것" 의 하한 증거다. 하지만 best 만 쓰면 재현성이 숨겨지므로 avg · max 와 측정 조건(load avg, 스레드 수, 컴파일 옵션)을 같이 적는다. 이 레슨에서 같은 기계어의 best 가 실행마다 10 % 넘게 달랐다.

</details>

<details><summary>Q3. 왜 스레드 하나로는 DRAM 대역폭을 다 못 쓰나요?</summary>

Little 의 법칙: 대역폭 = 날아다니는 바이트 / 지연. 170 GB/s × 100 ns(가정) = 17 KB ≈ 128 B 라인 133 개가 항상 떠 있어야 한다. 한 코어가 동시에 추적할 수 있는 미스(L1/L2 의 미스 처리 자원 — MSHR/fill buffer)와 프리페처 깊이가 이보다 작으면 코어 하나가 한계가 된다. 이 맥은 한 코어가 88 GB/s (약 69 라인 상당) 를 내고 2 ~ 3 코어에서 포화했다. 서버 CPU 는 코어당 대역폭이 훨씬 작아 소켓을 채우려면 많은 코어가 필요하다.

</details>

<details><summary>Q4. write-allocate 가 STREAM 숫자에 어떤 영향을 주나요? 이 기계에 있는지 어떻게 알죠?</summary>

RFO 가 있으면 쓰기마다 라인을 먼저 읽으므로 Copy 는 실제 24 B/원소, Triad 32 B 가 지나간다. STREAM 은 16, 24 로 세므로 보고값 = 버스 × 2/3, × 3/4. 판별: (1) 상한 — 보고값 × 실제/보고 배율이 피크를 넘으면 그 가설은 불가능. 이 맥: Fill 117 × 2 = 234 > 170, Copy 129 × 1.5 = 193 > 170 → 순차 전체 라인 쓰기에는 RFO 가 없다고 추론. (2) 비율 — 포화 상태의 Fill/Sum 이 0.5 근처인지 1 근처인지 (측정 1.01). 확실히 하려면 PMU 의 DRAM 읽기/쓰기 바이트 카운터를 본다.

</details>

<details><summary>Q5. 피크 170 GB/s 인데 왜 115 ~ 129 GB/s 밖에 안 나오죠?</summary>

피크는 핀 속도 × 폭이고, 실제로는 (1) 리프레시(tRFC/tREFI, MEM-B05), (2) 읽기↔쓰기 전환(tWTR 등 버스 턴어라운드 — Triad 는 읽기 2 : 쓰기 1 이 섞임), (3) 행 충돌 · 뱅크 충돌(MEM-B03), (4) 컨트롤러 스케줄링 효율(MEM-M01), (5) GPU · 디스플레이 등 다른 마스터와의 공유, (6) 이 측정에서는 부하 20 의 공용 머신 이라는 손실이 있다. 순차 STREAM 에서 피크의 70 ~ 85 % 가 흔히 "좋은" 값으로 언급된다(대표값). 이 맥은 부하 중 67 ~ 76 % 였다.

</details>

<details><summary>Q6. 새 보드 bring-up 에서 OS 없이 대역폭을 잰다면?</summary>

같은 원리로 베어메탈 루프: 캐시와 MMU 를 켜고(꺼져 있으면 버스트가 안 나와 엉뚱한 숫자), 배열 ≥ 4 × LLC, 사이클 카운터(DWT CYCCNT / CNTVCT)로 시간, 결과 검증. 코어 하나로 포화가 안 되면 DMA 엔진으로 메모리→메모리 복사를 걸어 컨트롤러 쪽 숫자를 보고, 메모리 컨트롤러의 성능 카운터(읽기/쓰기 바이트, 행 히트율)가 있으면 같이 읽는다. 먼저 정확성(memtest — MEM-B08/B09)을 통과한 뒤에 성능을 잰다. 트레이닝 결과가 나쁘면 대역폭보다 ECC CE 증가로 먼저 보인다 (SIV-M14).

</details>

<details><summary>Q7. 12 스레드가 4 스레드보다 느린 이유는?</summary>

세 가지. (1) 이기종 코어: 고정 분할이면 E 코어에 걸린 스레드가 늦게 끝나고 전체가 그것을 기다린다 — 동적 분할(작은 청크를 큐로, OpenMP `schedule(dynamic)`)이 해법. (2) 포화 뒤에는 스레드를 늘려도 대역폭이 안 늘고 컨트롤러 큐 경쟁 · 행 충돌만 는다. (3) 공용 머신에서 스레드가 많을수록 하나가 밀려날 확률이 커진다. macOS 는 코어 고정이 안 되므로(affinity → KERN_NOT_SUPPORTED) QoS 로 빠른 코어를 "선호" 만 할 수 있다.

</details>

<details><summary>Q8. Copy 루프가 memcpy 로 바뀌었습니다. 문제인가요?</summary>

측정 대상이 바뀐 것이다. libc memcpy 는 크기에 따라 다른 경로(큰 블록 전용 store, 프리페치 힌트 등)를 쓸 수 있어 "직접 짠 루프" 와 다른 숫자가 나온다 (이 레슨: memcpy 128.9 vs 루프 108.8 — 같은 부하의 다른 실행이라 ±10 % 노이즈 감안, run 1 에서도 112.7 vs 99.9 로 같은 방향). 어느 쪽이 맞다기보다 **무엇을 쟀는지 밝히는 것**이 중요하다. 루프를 재려면 `-fno-builtin` 을 쓰거나 커널을 다른 번역 단위에 둔다. 보고서에는 컴파일러 · 옵션 · asm 확인 결과를 같이 적는다.

</details>

## 9. 추가 연습

### 연습 1 — write-allocate 가설을 비율로 판별

버스가 같은 총 트래픽 X 로 포화라면 커널 k 의 보고값 = X × (STREAM B / 실제 B). 두 커널의 보고 비율을 RFO 있음/없음 두 가설로 예측하는 `predicted_ratio(k1, k2, wa)` 와, 측정 비율이 어느 쪽에 가까운지(로그 거리) 고르는 `wa_verdict` 를 짜라. Copy/Triad, Fill/Sum, Add/Triad 의 예측값은?

<details><summary>답 보기</summary>

Copy/Triad: RFO 면 (16/24)/(24/32) = **8/9**, 아니면 1. Fill/Sum: RFO 면 (8/16)/(8/8) = **1/2**, 아니면 1. Add/Triad: 둘 다 1 — 같은 모양의 커널끼리는 구별할 수 없다. 이 맥 실측 Fill/Sum = 1.01 (t = 2) → `wa_verdict` = 0 (no-RFO). 단, 비율은 노이즈에 약하므로 본문의 상한 논증과 같이 쓴다.

</details>

### 연습 2 — 보고값 → 버스 트래픽 → 효율

`bus_gbs(k, reported, wa)` = reported × 실제/보고. Triad 보고 120 GB/s 를 RFO 가정으로 환산하고 피크 170 대비 효율을 두 방식으로 구하라.

<details><summary>답 보기</summary>

120 × 4/3 = **160 GB/s** → 94.1 %. STREAM 규약 그대로면 120 / 170 = **70.6 %**. 같은 측정이 해석에 따라 70 % 도 94 % 도 된다 — 그래서 보고서에 "어느 규약인가" 를 반드시 적는다. 이 맥은 RFO 가 없다고 추론했으므로 70 % 쪽이 맞는 해석이다.

</details>

### 연습 3 — Little 의 법칙: 몇 라인이 날아다녀야 하나

`lines_in_flight(gbs, lat_ns, line)` = gbs × lat_ns / line (GB/s × ns = B). 170 GB/s, 100 ns, 128 B 라인이면? 한 코어가 40 라인을 띄울 수 있다면 몇 스레드가 필요한가?

<details><summary>답 보기</summary>

170 × 100 / 128 = **132.8 라인** (17,000 B). 40 라인/코어면 ⌈132.8 / 40⌉ = **4 스레드**. 64 B 라인 기계에서 100 GB/s · 100 ns 면 156.25 라인. 지연 100 ns 는 가정이다 — MEM-B10 의 포인터 체이싱으로 이 맥의 DRAM 지연을 잰 뒤 다시 계산하라. 실측 1 스레드 88 GB/s 는 같은 가정에서 약 69 라인 상당이다.

</details>

### 연습 4 — 산술 강도와 roofline 미리보기

`intensity(k, wa)` = flop / byte (기약분수), `attainable_gflops(peak, bw, ai)` = min(peak, bw × ai). Triad 의 산술 강도를 RFO 없음/있음으로 구하고, 대역폭 120 GB/s 에서 Triad 가 낼 수 있는 GFLOP/s 는?

<details><summary>답 보기</summary>

Triad: 2 flop / 24 B = **1/12**, RFO 포함 2/32 = **1/16**. Scale 1/16, Copy 0. 120 GB/s × 1/12 = **10 GFLOP/s** — 코어 하나의 FMA 능력(수십 GFLOP/s)에 한참 못 미친다 → STREAM 은 완전히 메모리 바운드다. 연산 천장이 5 GFLOP/s 인 작은 코어라면 min(5, 10) = 5 로 연산이 천장. 계층별 천장과 타일링은 MEM-A08, AI 가속기 관점은 AIPERF-B04.

</details>

### 연습 5 — 경계 라인 공유 개수

`shared_lines(n, t, align, line_elems)`: 내부 경계 b 가 라인 시작이 아니면 원소 b−1 과 b 가 같은 라인에 있다 → 서로 다른 라인 수를 센다. n = 1000 (t = 3, 8), n = 1024 (t = 8), n = 2^20 + 3 (t = 8), n = 20 (t = 8) 을 align 1 / 16 으로.

<details><summary>답 보기</summary>

| n, t | align 1 | align 16 |
|---|---|---|
| 1000, 3 | **2** (333, 666) | 0 |
| 1000, 8 | **7** (125·j 는 16 의 배수가 없음) | — |
| 1024, 8 | **0** (128·j) | — |
| 2^20 + 3, 8 | **5** (131072·j + ⌊3j/8⌋: j = 3,4,5,6,7 에서 +1, +1, +1, +2, +2) | 0 |
| 20, 8 | **2** (경계 2,5,7,10,12,15 는 모두 라인 0 — 한 라인을 6 스레드가 씀, 17 은 라인 1) | — |

마지막 경우가 극단: 작은 n 에서 정렬 안 한 분할은 한 라인을 여러 코어가 쓴다. 정렬하면 구간 크기가 최대 align−1 만큼 어긋나는 대가가 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 27/27 passed
```

## 10. 핵심 정리

- STREAM 4 커널: Copy c=a (16 B), Scale b=q·c (16 B), Add c=a+b (24 B), Triad a=b+q·c (24 B) — RFO 는 안 센다. RFO 가 있으면 실제 트래픽은 3/2, 4/3 배.
- 배열 하나 ≥ 4 × LLC (≥ 1 M 원소). 모르는 SLC 는 가정을 밝히고 크기 스윕으로 점검 (이 맥: 총 ~100 MiB 부터 평평).
- 측정은 첫 반복 버림 + best(최소 시간) + avg + load avg + 컴파일 옵션. 결과 검증과 `clang -S` 로 루프가 지워지거나 바뀌지 않았는지 확인 (Copy → memcpy 였다).
- 이 맥(M6, 공표 170 GB/s, 부하 ~20): Triad best 114.6 GB/s (67 %), Copy(memcpy) 128.9 (76 %), 1 스레드 Triad 87.9 (52 %), 2 ~ 3 스레드에서 포화. 실행 간 ±10 %.
- 1 스레드가 포화 못 하는 이유 = Little: 170 GB/s × 100 ns / 128 B ≈ 133 라인이 떠 있어야 한다.
- RFO 판별: 보고 × 배율 > 피크면 그 가설 기각 (Fill 117 × 2 = 234 > 170 → 순차 전체 라인 쓰기에 RFO 없음, 추론). NT store(stnp) 이득은 노이즈 안.
- 분할은 연속 구간 + 라인 정렬 경계. 이기종 코어 + 고정 분할 = 가장 느린 코어가 전체 시간.

## 참고자료

- John D. McCalpin, STREAM: Sustainable Memory Bandwidth in High Performance Computers — 커널 정의, 실행 규칙, stream.c (배열 크기 규칙 · 검증 · best rate 보고): <https://www.cs.virginia.edu/stream/>
- J. D. McCalpin, "Memory Bandwidth and Machine Balance in Current High Performance Computers", IEEE TCCA Newsletter, 1995 — STREAM 원 논문.
- Apple Newsroom (2026-08), "Apple introduces M6 and M5 Ultra..." — M6: "up to 170GB/s of unified memory bandwidth", 12 코어 (Super 2 + Performance 4 + Efficiency 6): <https://www.apple.com/newsroom/2026/08/apple-introduces-m6-and-m5-ultra-for-a-big-leap-in-performance-and-ai-compute/> (메모리 종류 · 폭은 공표 안 됨 — 이 레슨의 LPDDR5X-10667 × 128-bit 는 추정)
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007) — 3 장 캐시, 6.1 절 non-temporal store 와 RFO.
- S. Williams, A. Waterman, D. Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", CACM 2009 — 연습 4 의 바탕 (MEM-A08).
- Clang 문서 `__builtin_nontemporal_store`, LLVM LoopIdiomRecognize 패스 (루프 → memcpy/memset 변환).
- 관련 레슨: ARCH-M06 (쓰기 정책 · write-allocate), ARCH-M08 (128 B 라인 · false sharing), ARCH-M07 (루프 순서 · 대역폭), ARCH-A03 (페이지 폴트 비용), MEM-B03 (행 충돌), MEM-B04 (피크 계산기), MEM-B05 (리프레시 손실), MEM-B10 (지연 측정), MEM-M01 (스케줄링), MEM-M04 (일관성 트래픽 실측), MEM-M06 (RFO 대역폭), MEM-A08 / AIPERF-B04 (roofline).

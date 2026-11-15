---
id: ARCH-M05
time: 90분
updated: 2026-11-11
---
# ARCH-M05 집합 연관 캐시와 교체 정책

## 학습 목표

- 용량 C, 라인 B, 연관도 W 로 집합 수 S = C / (B·W) 와 tag / index / offset 비트를 구하고, N-way 집합 연관(set-associative) 캐시 시뮬레이터를 짤 수 있다.
- 교체 정책(replacement policy) 4종 — LRU, FIFO, Random(고정 시드 xorshift), tree-PLRU — 을 "히트 때 무엇을 바꾸나 / 채울 때 무엇을 바꾸나 / 희생자는 누구나" 세 함수로 구현하고, 4-way·8-way PLRU 비트를 손으로 따라갈 수 있다.
- 미스를 3C(compulsory / capacity / conflict, Hill 정의)로 나누고, 집계 conflict 가 **음수** 가 되는 경우(집합 연관이 같은 용량 FA-LRU 보다 잘하는 경우)를 설명할 수 있다.
- "연관도를 올리면 항상 좋아진다", "LRU 가 항상 최선이다" 가 틀린 트레이스를 직접 보여 주고, LRU 포함 성질(stack property)과 FIFO 의 Belady 이상을 구분할 수 있다.
- 펌웨어 시나리오(핫 셋 + DMA 스트림)에서 정책을 바꾸는 것보다 MPU non-cacheable, TCM, way lockdown 이 왜 확실한 답인지 숫자로 말할 수 있다.

## 1. 문제

"4-way LRU 로 바꾸면 어떤 미스가 줄어드나?", "PLRU 비트는 몇 개고 어떻게 움직이나?" — CPU·SoC 아키텍처, 실리콘 검증, 성능 엔지니어 면접에서 캐시 질문의 두 번째 단계가 이것이다. 직접 사상(ARCH-M04)의 충돌을 연관도로 줄이면 이번엔 "집합 안에서 누구를 쫓아내나" 가 성능을 가르고, 실제 칩(Cortex-M7 L1 D-cache 4-way 32 B 라인, Cortex-A 의 L1/L2, 데스크톱·모바일 코어)은 대개 정확한 LRU 대신 근사(PLRU, pseudo-random, 적응형 삽입 정책 등)를 쓴다고 알려져 있다.

이미 아는 것(다시 가르치지 않는다): AMAT 와 로컬/전역 미스율(ARCH-B10), 직접 사상 캐시의 tag/index/offset 분해와 conflict 사례(ARCH-M04 — 이 레슨 코드에 **ARCH-M04 모델을 축소해 넣었다**), false sharing(ARCH-M08), DMA 와 clean/invalidate(ARCH-M12). 쓰기 정책과 DRAM 바이트는 ARCH-M06, 실측 matmul 은 ARCH-M07, 다단 캐시는 MEM-M03 이 맡는다. 여기서 쓰기는 write-allocate 로 보고 읽기와 똑같이 hit/miss 만 센다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">4 KB, 32 B 라인, 4-way → S = 4096 / (32·4) = 32 집합</text>
  <rect x="120" y="34" width="330" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="450" y="34" width="110" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="560" y="34" width="110" height="30" class="f-ok" stroke="currentColor"/>
  <text x="220" y="54" fill="currentColor">tag [31:10] 22 bit</text>
  <text x="462" y="54" fill="currentColor">index [9:5]</text>
  <text x="570" y="54" fill="currentColor">offset [4:0]</text>
  <text x="10" y="54" fill="currentColor">주소 32 bit</text>
  <line x1="505" y1="64" x2="505" y2="96" stroke="currentColor"/>
  <text x="512" y="86" fill="currentColor" font-size="12">집합 하나 고르기</text>
  <text x="10" y="116" fill="currentColor">set 0</text>
  <text x="10" y="150" fill="currentColor">set 19</text>
  <text x="10" y="184" fill="currentColor">set 31</text>
  <text x="62" y="133" fill="currentColor" font-size="12">…</text>
  <g stroke="currentColor">
    <rect x="70" y="100" width="120" height="24" class="f-mut"/><rect x="200" y="100" width="120" height="24" class="f-mut"/>
    <rect x="330" y="100" width="120" height="24" class="f-mut"/><rect x="460" y="100" width="120" height="24" class="f-mut"/>
    <rect x="70" y="134" width="120" height="24" class="f-acc"/><rect x="200" y="134" width="120" height="24" class="f-acc"/>
    <rect x="330" y="134" width="120" height="24" class="f-acc"/><rect x="460" y="134" width="120" height="24" class="f-acc"/>
    <rect x="70" y="168" width="120" height="24" class="f-mut"/><rect x="200" y="168" width="120" height="24" class="f-mut"/>
    <rect x="330" y="168" width="120" height="24" class="f-mut"/><rect x="460" y="168" width="120" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="82" y="151">way0 V tag data</text><text x="212" y="151">way1 V tag data</text>
    <text x="342" y="151">way2 V tag data</text><text x="472" y="151">way3 V tag data</text>
  </g>
  <rect x="592" y="134" width="140" height="58" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="600" y="152" fill="currentColor" font-size="12">집합별 정책 상태</text>
  <text x="600" y="168" fill="currentColor" font-size="12">LRU/FIFO: way별 도장</text>
  <text x="600" y="184" fill="currentColor" font-size="12">PLRU: 트리 3 bit</text>
  <g stroke="currentColor" fill="none">
    <circle cx="130" cy="226" r="14"/><circle cx="260" cy="226" r="14"/><circle cx="390" cy="226" r="14"/><circle cx="520" cy="226" r="14"/>
    <line x1="130" y1="158" x2="130" y2="212"/><line x1="260" y1="158" x2="260" y2="212"/>
    <line x1="390" y1="158" x2="390" y2="212"/><line x1="520" y1="158" x2="520" y2="212"/>
    <line x1="130" y1="240" x2="320" y2="276"/><line x1="260" y1="240" x2="320" y2="276"/>
    <line x1="390" y1="240" x2="320" y2="276"/><line x1="520" y1="240" x2="320" y2="276"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="125" y="230">=</text><text x="255" y="230">=</text><text x="385" y="230">=</text><text x="515" y="230">=</text>
    <text x="560" y="230">tag 비교기 W 개 (병렬)</text>
  </g>
  <rect x="280" y="276" width="80" height="26" class="f-ok" stroke="currentColor"/>
  <text x="292" y="294" fill="currentColor">OR → hit</text>
  <text x="380" y="294" fill="currentColor" font-size="12">미스: 빈 way 먼저, 없으면 정책이 희생자 지정</text>
  <text x="10" y="322" fill="currentColor" font-size="12">W = 1 → 직접 사상(ARCH-M04), S = 1 → 완전 연관. 비교기 W 개, 정책 상태도 W 와 함께 커진다.</text>
</svg>
```

체크 테스트는 모두 결정적이다(Random 정책도 고정 시드 `0x2545F491` xorshift32). 비교 표와 탐색은 `demo_table.c` 가 찍는다.

### Part A — N-way 캐시와 LRU / FIFO / Random

- 주어짐: `cache_init(C, B, W, pol)`(2의 거듭제곱 검사, S·비트 계산), `addr_set / addr_tag`, `cache_access`(조회 → 히트면 `policy_on_hit`, 미스면 **빈 way 중 번호가 가장 작은 것**, 없으면 `policy_victim` → 채우고 `policy_on_fill`).
- 할 일: `policy_on_hit`, `policy_on_fill`, `policy_victim` 세 함수를 LRU / FIFO / Random 에 대해. `c->clock` 은 접근마다 1씩 오르는 시각.
- 테스트: 4-way 한 집합에 A B C D A E A — LRU 는 E 가 B 를, FIFO 는 A 를 쫓는다. 5 라인 순환 루프 x 4 회는 LRU/FIFO 가 20/20 미스(LRU 절벽), Random 은 일부 히트. W = 1 이면 네 정책이 같은 미스. LRU 포함 성질: S 고정에서 W → 2W 로 늘리면 **매 접근** hit(W) ⊆ hit(2W).

### Part B — tree-PLRU 와 3C 분류

- `plru_touch(bits, W, way)`, `plru_victim(bits, W)` — 비트 규칙은 아래 풀이에 정확히 적는다 (W = 4 → 3 bit, W = 8 → 7 bit, W = 1 → 0 bit).
- `sim_run(C, B, W, pol, trace, n, &st)` — 같은 트레이스를 대상 캐시와 **같은 용량 FA-LRU 그림자 캐시** 에 동시에 흘리고, 본 라인 집합(주어짐)으로 첫 접촉을 판정해 `comp / cap / conf / anti / fa_misses` 를 센다.
- 테스트: PLRU 비트 값(4-way, 8-way), A B C D A E C 에서 LRU 5 미스 vs PLRU 6 미스, 2-way 에서는 PLRU = LRU, 손계산 3C 두 개(직접 사상 핑퐁, 9 라인 순환 → 집계 conflict −14), 모든 정책·연관도에서 항등식, "같은 용량에서 연관도를 올렸더니 미스가 늘었다" 예.

### Part C — 펌웨어: 핫 셋 + DMA 스트림, 필요한 연관도

제어 루프 tick 마다 핫 영역(필터 계수표 + 상태, 64 라인 = 2 KB)을 읽고, 이어서 DMA RX 버퍼 96 라인(3 KB, tick 마다 새 주소)을 훑는다. 캐시는 4 KB 4-way 32 B (Cortex-M7 D-cache 의 작은 구성과 같은 모양 — 크기는 구현마다 다르고, 정책은 모델 파라미터).

- `region_misses(&scen, trace, n, lo, hi)` — `[lo, hi)` 주소의 미스만 센다. `scen.nc_lo..nc_hi` 는 MPU non-cacheable 창: 그 접근은 캐시를 조회도 채움도 안 하고 미스로 세지 않는다.
- `min_ways_for(C, B, pol, trace, n, tol)` — W = 1, 2, 4, …, C/B 중 미스가 같은 용량 FA-LRU 의 (1 + tol) 배 이내가 되는 가장 작은 W ("4-way 면 충분한가?" 를 숫자로). 없으면 0.
- 테스트: 4-way LRU/FIFO 는 핫 미스 1024/1024, Random·PLRU 는 더 적다, FA-LRU 여도 1024, 스트림을 non-cacheable 로 두면 64(첫 tick 만). `colwalk` → 32-way, `loop160` → 1, `rand_hot` → 8.

## 2. Problem (English)

Extend a direct-mapped cache simulator (ARCH-M04) to a W-way set-associative cache with four replacement policies and classify misses with the 3C model.

**Part A.** Given `cache_t` (capacity C, line B, ways W, sets S = C/(B·W), all powers of two), `cache_access()` looks up the set; on a hit it calls `policy_on_hit(c, set, way)`; on a miss it fills the lowest-numbered invalid way, or, if the set is full, the way returned by `policy_victim(c, set)`, then calls `policy_on_fill(c, set, way)`. Implement the three hooks for LRU (evict least recently used), FIFO (evict oldest fill; hits do not reorder) and Random (`xorshift32(&c->rng) & (W-1)`, state seeded with `0x2545F491` at init, so runs are reproducible).

**Part B.** Implement tree-PLRU for W ∈ {1, 2, 4, …}: W−1 node bits in heap order (root 0, children 2i+1, 2i+2); bit 0 means "next victim is in the left subtree". `plru_touch` (on hit and on fill) sets every node on the path to the touched way to point *away* from it; `plru_victim` follows the bits from the root. Implement `sim_run()` that runs a trace through the cache and a same-capacity fully-associative LRU shadow, and counts: compulsory (first touch of that line address ever), capacity (non-compulsory miss that the FA-LRU also misses), conflict (miss that FA-LRU hits), and `anti` (hit here but FA-LRU missed). Invariants: `misses = comp + cap + conf`, `fa_misses = comp + cap + anti`, so the aggregate (Hill) conflict count `misses − fa_misses = conf − anti` may be negative.

**Part C.** `region_misses()` counts misses within an address window, with an optional non-cacheable window that bypasses the cache entirely. `min_ways_for()` returns the smallest power-of-two W whose misses are within `(1 + tol)` of the FA-LRU misses (0 if none).

Example: FA 4-way, line sequence `A B C D A E C` → LRU 5 misses (E evicts B), PLRU 6 misses (E evicts C). 8-line cache, lines `0..8` repeated 3 times: FA-LRU 27 misses (9 comp, 18 cap); direct-mapped 13 misses (9 comp, 4 cap, 0 conf, anti 14).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M05
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
mkdir -p /tmp/esn_ARCH-M05
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-M05/st && /tmp/esn_ARCH-M05/st; echo "exit $?"
```

실제 출력 (발췌):

```text
-- Part A: N-way + LRU / FIFO / Random --
PASS  cache_init(&c, 4096, 32, 4, POL_LRU) == 0 && c.S == 32 && c.off_bits == 5 && c.idx_bits == 5
PASS  addr_set(&c, 0x12345678u) == 0x13 && addr_tag(&c, 0x12345678u) == 0x48D15u
...
FAIL  misses_of(128, 32, 4, POL_LRU, s, 7) == 5  (line 264)
FAIL  misses_of(128, 32, 4, POL_FIFO, s, 7) == 6  (line 265)
FAIL  l.evicted && l.evicted_line == 1  (line 269)
...
-- Part B: tree-PLRU, 3C --
FAIL  b4[0] == 1 && b4[1] == 1 && b4[2] == 0 && plru_victim(b4, 4) == 2  (line 311)
      8-way after fill 0..7, touch 3, 5: bits = 0000000, victim = 0
...
-- Part C: firmware hot set + DMA stream --
      4 KB 4-way LRU   : hot misses    0 / 1024
...
FAIL  w == 8  (line 401)
PASS  min_ways_for(4096, 32, POL_LRU, g_t, n, -2.0) == 0
14/42 passed
exit 1
```

주소 분해와 `cache_init` 검사는 주어진 코드라서 처음부터 PASS 다. 정책 훅이 비어 있으면 희생자가 항상 way 0 이 되고(`f.evicted_line == 0` 이 우연히 PASS), `sim_run` 의 세는 부분이 비어 있어 미스가 0 으로 나온다.

## 4. 힌트

<details><summary>힌트 1 — LRU 와 FIFO 는 같은 도장, 찍는 때만 다르다</summary>

way 마다 `stamp` 하나. LRU 는 **히트와 채움** 때 `stamp = clock`, FIFO 는 **채움** 때만. 희생자는 둘 다 "도장이 가장 작은 way". 하드웨어는 시각 대신 순위 비트나 라운드로빈 포인터로 같은 일을 한다(연습 3).
</details>

<details><summary>힌트 2 — Random 은 상태를 캐시 안에</summary>

전역 `rand()` 를 쓰면 다른 테스트가 부른 횟수에 따라 결과가 바뀐다. `c->rng` 를 init 에서 시드로 두고 `xorshift32(&c->rng) & (c->W - 1)`. W 가 2의 거듭제곱이라 `%` 대신 마스크로 치우침이 없다.
</details>

<details><summary>힌트 3 — PLRU 는 "범위 [lo, lo+size)" 를 반씩 줄이며 내려간다</summary>

`node = 0, lo = 0, size = W` 에서 시작해 `half = size/2`. touch: way 가 왼쪽 반이면 그 노드 비트를 1(희생자는 오른쪽), 오른쪽 반이면 0 으로 두고 그쪽 자식(2n+1 / 2n+2)으로. victim: 비트 0 이면 왼쪽, 1 이면 오른쪽(`lo += half`). `size == 1` 에서 멈추면 `lo` 가 way.
</details>

<details><summary>힌트 4 — 3C 는 접근 하나마다 세 질문</summary>

(1) 이 라인 주소를 처음 보나? (2) 대상 캐시 hit? (3) 같은 용량 FA-LRU hit? 대상이 미스면 (1) → comp, 아니고 FA 도 미스 → cap, FA 는 히트 → conf. 대상이 히트인데 FA 가 미스면 anti. FA 미스는 따로 `fa_misses`.
</details>

<details><summary>힌트 5 — non-cacheable 은 "캐시를 안 건드린다"</summary>

창 안의 접근은 `cache_access` 를 부르지 않는다 — 그래야 스트림이 핫 라인을 못 쫓아낸다. 미스 수에도 넣지 않는다(캐시 미스가 아니라 매번 버스 접근이다).
</details>

## 5. 풀이

### Part A — 세 훅과 정책 상태

| 정책 | on_hit | on_fill | victim (집합이 꽉 찼을 때) | 집합당 상태 (하드웨어) |
|---|---|---|---|---|
| LRU | `stamp = clock` | `stamp = clock` | stamp 최소 | 정확히는 ⌈log2 W!⌉ bit, 보통 나이 카운터 W·log2 W 또는 행렬 W(W−1)/2 |
| FIFO | 없음 | `stamp = clock` | stamp 최소 (= 가장 먼저 들어온 것) | 라운드로빈 포인터 log2 W bit |
| Random | 없음 | 없음 | `xorshift32 & (W−1)` | 0 bit (캐시 전체에 LFSR 하나) |
| PLRU | `plru_touch` | `plru_touch` | `plru_victim` | W−1 bit |

빈 way 를 먼저 쓰는 규칙은 `cache_access` 가 공통으로 처리한다. 그래서 정책 함수는 "꽉 찬 집합" 만 생각하면 된다. FIFO 의 도장은 들어온 순서라서, 빈 way 를 번호 순으로 채우는 이 모델에서는 라운드로빈 포인터와 같은 결과다.

A B C D A E A (FA 4-way) 손 추적:

| 접근 | LRU 내용 (오래된 → 최근) | FIFO 내용 (먼저 → 나중) | 결과 |
|---|---|---|---|
| A B C D | A B C D | A B C D | 4 미스 (compulsory) |
| A | B C D **A** | A B C D (그대로) | 둘 다 히트 |
| E | C D A E (B 쫓김) | B C D E (A 쫓김) | 둘 다 미스 |
| A | 히트 | **미스** | LRU 5, FIFO 6 |

5 라인 순환 x 4 회(FA 4-way)는 LRU 와 FIFO 모두 **20/20** 미스다. 다음에 쓸 라인이 언제나 "가장 오래된" 라인이라서다 — 워킹셋이 용량을 한 줄만 넘어도 LRU 히트율이 0 으로 떨어지는 **LRU 절벽**. Random 은 같은 트레이스에서 11/20(테스트 출력 `loop5 on FA-4: Random misses = 11 / 20`).

**LRU 포함 성질(inclusion / stack property, Mattson 1970).** 집합 수 S 를 고정하고 W → 2W 로 늘리면, W-way LRU 의 내용은 언제나 2W-way LRU 내용의 부분집합이다(각 집합의 "최근 사용 순서 상위 W 개" ⊆ "상위 2W 개"). 그래서 접근 하나하나에서 hit(W) → hit(2W). 테스트는 2만 개 랜덤 접근에서 W = 1→2→4→8→16 을 접근 단위로 확인한다. FIFO 와 Random 은 이 성질이 없다 → Part B 의 Belady 이야기.

### Part B — tree-PLRU 정확한 규칙

비트 배치(힙 순서)와 규칙:

- 노드 0 = 루트, 노드 i 의 자식 = 2i+1(왼쪽), 2i+2(오른쪽). 4-way: 노드 0(way 0–1 vs 2–3), 노드 1(0 vs 1), 노드 2(2 vs 3). 8-way: 노드 0(0–3 vs 4–7), 1(0–1 vs 2–3), 2(4–5 vs 6–7), 3(0 vs 1), 4(2 vs 3), 5(4 vs 5), 6(6 vs 7).
- 비트 0 = "다음 희생자는 왼쪽", 1 = "오른쪽".
- touch(way): 경로 위 노드를 모두 way 의 **반대쪽** 으로 (way 가 왼쪽 반이면 1, 오른쪽 반이면 0). 히트와 채움 모두.
- victim: 루트에서 비트를 따라 내려간다. 다른 비트는 바꾸지 않는다(채운 뒤 touch 가 바꾼다).

`demo_table plru` 로 A B C D A E C 를 한 단계씩 찍은 실제 출력 (LRU 칸의 괄호 = 도장, PLRU 비트 = 노드 0 1 2):

```text
step acc | LRU: hit way0..3          | PLRU: hit bits way0..3  victim-next
   1  A   |      m   A(1) .(0) .(0) .(0)  |       m  110  A . . .    way2
   2  B   |      m   A(1) B(2) .(0) .(0)  |       m  100  A B . .    way2
   3  C   |      m   A(1) B(2) C(3) .(0)  |       m  001  A B C .    way0
   4  D   |      m   A(1) B(2) C(3) D(4)  |       m  000  A B C D    way0
   5  A   |      H   A(5) B(2) C(3) D(4)  |       H  110  A B C D    way2
   6  E   |      m   A(5) E(6) C(3) D(4)  |       m  011  A B E D    way1
   7  C   |      H   A(5) E(6) C(7) D(4)  |       m  101  A C E D    way3
misses: LRU 5, PLRU 6
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">4-way tree-PLRU, A B C D 를 채우고 A 히트한 직후 (5단계)</text>
  <rect x="160" y="40" width="90" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="172" y="62" fill="currentColor">node0 = 1</text>
  <rect x="50" y="110" width="90" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="62" y="132" fill="currentColor">node1 = 1</text>
  <rect x="270" y="110" width="90" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="282" y="132" fill="currentColor">node2 = 0</text>
  <g stroke="currentColor" fill="none">
    <line x1="190" y1="74" x2="100" y2="110"/>
    <line x1="220" y1="74" x2="310" y2="110" stroke-width="3"/>
    <line x1="80" y1="144" x2="40" y2="190"/><line x1="110" y1="144" x2="140" y2="190"/>
    <line x1="300" y1="144" x2="260" y2="190" stroke-width="3"/><line x1="330" y1="144" x2="360" y2="190"/>
  </g>
  <rect x="10" y="190" width="70" height="30" class="f-ok" stroke="currentColor"/><text x="20" y="210" fill="currentColor">w0 A</text>
  <rect x="110" y="190" width="70" height="30" class="f-mut" stroke="currentColor"/><text x="120" y="210" fill="currentColor">w1 B</text>
  <rect x="225" y="190" width="70" height="30" class="f-warn" stroke="currentColor"/><text x="235" y="210" fill="currentColor">w2 C</text>
  <rect x="325" y="190" width="70" height="30" class="f-mut" stroke="currentColor"/><text x="335" y="210" fill="currentColor">w3 D</text>
  <text x="10" y="246" fill="currentColor" font-size="12">굵은 선 = 희생자 경로: node0=1 → 오른쪽, node2=0 → 왼쪽 → way2 (C)</text>
  <text x="10" y="266" fill="currentColor" font-size="12">A 를 touch: node0 ← 1, node1 ← 1 (A 반대쪽). node2 는 D 를 채울 때의 0 그대로.</text>
  <rect x="440" y="40" width="290" height="180" rx="6" fill="none" stroke="currentColor"/>
  <text x="452" y="62" fill="currentColor" font-weight="bold">정확한 LRU 라면</text>
  <text x="452" y="86" fill="currentColor">사용 순서: B(2) C(3) D(4) A(5)</text>
  <text x="452" y="108" fill="currentColor">희생자 = B (가장 오래됨)</text>
  <text x="452" y="138" fill="currentColor">PLRU 는 "B 와 A 가 같은 반쪽,</text>
  <text x="452" y="158" fill="currentColor">A 가 방금 쓰였다" 만 기억한다 →</text>
  <text x="452" y="178" fill="currentColor">반대쪽 반(C, D) 중 하나를 고른다</text>
  <text x="452" y="206" fill="currentColor" font-size="12">다음 C 접근: LRU 히트, PLRU 미스</text>
</svg>
```

8-way 확인: 0..7 을 차례로 채우면 모든 비트가 0 이다 — 각 노드를 마지막으로 지나간 touch 가 그 노드의 오른쪽 서브트리 way 였으므로 모두 "왼쪽" 을 가리킨다. 이어서 way 3, way 5 를 touch 하면 테스트 출력 그대로 `bits = 0010000, victim = 0` — way 5 가 루트를 0(왼쪽)으로 되돌렸고 노드 1·3 은 여전히 0 이라 way 0. 여기서 way 0 을 touch 하면 희생자는 way 6 인데, 정확한 LRU 순서(1 2 4 6 7 3 5 0)로는 way 1 이다. **PLRU 는 "최근 것은 안 쫓는다" 는 보장만 있고 "가장 오래된 것을 쫓는다" 는 보장이 없다.** 2-way 에서는 비트 1개가 곧 정확한 LRU 라서 테스트 `a.misses == b.misses` 가 성립한다.

### Part B — 3C: 정의, 손계산, 음수 conflict

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">접근 하나의 분류 (대상 캐시 + 같은 용량 FA-LRU 그림자 + 본 라인 집합)</text>
  <rect x="10" y="40" width="150" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor">대상 캐시 hit?</text>
  <rect x="220" y="40" width="160" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="232" y="62" fill="currentColor">처음 본 라인?</text>
  <rect x="440" y="40" width="150" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="452" y="62" fill="currentColor">FA-LRU hit?</text>
  <rect x="220" y="120" width="160" height="34" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="240" y="142" fill="currentColor">compulsory</text>
  <rect x="440" y="120" width="130" height="34" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="458" y="142" fill="currentColor">conflict</text>
  <rect x="600" y="40" width="130" height="34" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="618" y="62" fill="currentColor">capacity</text>
  <rect x="10" y="120" width="150" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="22" y="142" fill="currentColor">hit (FA 도 hit)</text>
  <rect x="10" y="180" width="150" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="22" y="202" fill="currentColor">anti (FA 는 miss)</text>
  <g stroke="currentColor" fill="none">
    <line x1="160" y1="57" x2="220" y2="57"/><line x1="380" y1="57" x2="440" y2="57"/>
    <line x1="590" y1="57" x2="600" y2="57"/><line x1="300" y1="74" x2="300" y2="120"/>
    <line x1="510" y1="74" x2="510" y2="120"/><line x1="85" y1="74" x2="85" y2="120"/>
    <line x1="160" y1="137" x2="200" y2="137"/><line x1="200" y1="137" x2="200" y2="197"/><line x1="200" y1="197" x2="160" y2="197"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="168" y="52">miss</text><text x="388" y="52">아니오</text><text x="592" y="34">miss</text>
    <text x="306" y="102">예</text><text x="516" y="102">hit</text><text x="91" y="102">hit</text>
    <text x="206" y="172">FA miss</text>
  </g>
  <text x="240" y="200" fill="currentColor" font-size="12">misses = comp + cap + conf,  FA misses = comp + cap + anti</text>
  <text x="240" y="218" fill="currentColor" font-size="12">Hill 집계 conflict = misses − FA misses = conf − anti</text>
</svg>
```

정의는 Hill & Smith(1989)의 표준 정의다: compulsory = 무한 캐시에서도 나는 미스(그 라인을 처음 만짐), capacity = 같은 용량 **완전 연관 LRU** 도 나는 미스, conflict = 나머지. 교과서는 이것을 "집계" 로 쓴다 — conflict 개수 = (이 캐시 미스) − (FA-LRU 미스). 이 레슨은 접근마다 분류해서 `conf`(이 캐시만 미스)와 `anti`(FA-LRU 만 미스)를 따로 센다. 둘을 빼면 집계 정의와 같다.

손계산 (8 라인 캐시 = 256 B / 32 B):

| 트레이스 | 캐시 | misses | comp | cap | conf | anti | Hill conflict |
|---|---|---|---|---|---|---|---|
| 라인 0 8 0 8 | 직접 사상 | 4 | 2 | 0 | 2 | 0 | +2 |
| 라인 0 8 0 8 | 2-way LRU | 2 | 2 | 0 | 0 | 0 | 0 |
| 라인 0..8 x 3 | FA-LRU | 27 | 9 | 18 | 0 | 0 | 0 |
| 라인 0..8 x 3 | 직접 사상 | 13 | 9 | 4 | 0 | 14 | **−14** |

마지막 줄: 9 라인 순환은 8 라인 FA-LRU 에게 최악(LRU 절벽, 27/27)이지만, 직접 사상에서는 라인 0 과 8 만 집합 0 에서 싸우고 1..7 은 자기 집합에 눌러앉아 2·3 회차에 히트한다(14 번). 집합 매핑이 캐시를 **분할** 해서 LRU 절벽을 피한 것이다. "conflict 미스 = 집합 연관이라서 생긴 손해" 라는 해석은 이때 깨진다 — 집합 연관은 이득도 볼 수 있다.

같은 용량에서 연관도를 올렸는데 미스가 늘어나는 예(라인 0 2 4 0 2 4, 4 라인 캐시): 직접 사상(S = 4)은 0/4 만 충돌해 5 미스, 2-way(S = 2)는 세 라인이 모두 집합 0 으로 가서 2-way LRU 순환 → 6 미스. 연관도를 올리면 집합 수가 줄어 **매핑이 바뀌므로** 포함 성질이 적용되지 않는다(포함 성질은 S 고정일 때만).

**FIFO 의 Belady 이상.** FIFO 는 "칸을 늘렸는데 미스가 는다" 가 가능하다. `demo_table belady` 실제 출력:

```text
FIFO FA 2 lines vs 4 lines: no anomaly found
FIFO FA 4 lines vs 8 lines: no anomaly found
classic Belady string, FIFO 3 frames: 9 misses, 4 frames: 10 misses
```

결정적 랜덤 탐색(최대 200만 개 문자열, 길이 8–40 / 8–60)으로는 **2의 거듭제곱 라인 수를 두 배로** 늘리는 경우(2→4, 4→8)의 예를 찾지 못했다. 그래서 이 캐시 모델(용량 2의 거듭제곱) 안에서 Belady 이상을 보였다고는 말하지 않는다. 대신 고전 예(Belady, Nelson, Shedler 1969 의 페이지 문자열 1 2 3 4 1 2 5 1 2 3 4 5)를 2의 거듭제곱 제약 없는 "k 칸 FIFO" 로 돌리면 3 칸 9 미스, 4 칸 10 미스로 이상이 실제로 나온다(연습 2, 같은 문자열을 LRU 로 돌리면 10 → 8). 원인: FIFO 는 히트가 순서를 안 바꿔서, 칸이 많은 쪽이 "곧 쓸 라인" 을 더 오래된 위치에 들고 있다가 먼저 쫓는 일이 생긴다. LRU 는 포함 성질 때문에 이것이 불가능하다.

### Part C — 비교 표와 펌웨어 시나리오

`demo_table table` 실제 출력 (4 KB / 32 B = 128 라인, W = 1 은 정책 무관이라 한 줄):

```text
[colwalk] 1280 accesses, 4 KB / 32 B lines (128 lines)
  W    policy  misses  miss%   comp    cap   conf   anti  Hill-conf
  1    -          608   47.5    160      0    448      0       +448
  2    LRU        832   65.0    160      0    672      0       +672
  2    Random     604   47.2    160      0    444      0       +444
  4    LRU       1280  100.0    160      0   1120      0      +1120
  4    Random     598   46.7    160      0    438      0       +438
  4    PLRU      1248   97.5    160      0   1088      0      +1088
  8    LRU       1280  100.0    160      0   1120      0      +1120
  8    Random     575   44.9    160      0    415      0       +415
  FA   LRU        160   12.5    160      0      0      0         +0
[loop160] 1280 accesses, 4 KB / 32 B lines (128 lines)
  W    policy  misses  miss%   comp    cap   conf   anti  Hill-conf
  1    -          608   47.5    160    448      0    672       -672
  2    LRU        832   65.0    160    672      0    448       -448
  4    LRU       1280  100.0    160   1120      0      0         +0
  4    PLRU      1248   97.5    160   1088      0     32        -32
  FA   LRU       1280  100.0    160   1120      0      0         +0
  FA   Random     560   43.8    160    400      0    720       -720
  FA   PLRU      1189   92.9    160   1029      0     91        -91
[rand_hot] 30000 accesses, 4 KB / 32 B lines (128 lines)
  W    policy  misses  miss%   comp    cap   conf   anti  Hill-conf
  1    -         5100   17.0   2205   1146   1749    689      +1060
  2    LRU       5122   17.1   2205   1265   1652    570      +1082
  2    FIFO      6111   20.4   2205   1310   2596    525      +2071
  4    LRU       4342   14.5   2205   1271    866    564       +302
  4    FIFO      6594   22.0   2205   1384   3005    451      +2554
  4    Random    6497   21.7   2205   1309   2983    526      +2457
  4    PLRU      4737   15.8   2205   1288   1244    547       +697
  8    LRU       4239   14.1   2205   1374    660    461       +199
  8    FIFO      7185   23.9   2205   1489   3491    346      +3145
  FA   LRU       4040   13.5   2205   1835      0      0         +0
  FA   FIFO      7785   25.9   2205   1622   3958    213      +3745
  FA   Random    7714   25.7   2205   1434   4075    401      +3674
  FA   PLRU      5150   17.2   2205   1522   1423    313      +1110
```

(발췌 — 생략한 줄은 FIFO 가 LRU 와 같은 값인 colwalk/loop160 행 등. 전체는 직접 돌려 볼 것.) 세 트레이스가 서로 다른 교훈을 준다.

| 트레이스 | 무엇 | 교훈 |
|---|---|---|
| `colwalk` | 20 x 64 int32 행렬(행 256 B)을 열 우선으로 | 행 간격 256 B = 8 라인 → 집합의 1/8 만 쓴다. W 와 상관없이 **슬롯 16 개**(연습 4). 행 20 개 > 16 이라 LRU 는 W 를 올릴수록 나빠진다(DM 47.5% → 4-way 100%). 고치는 법은 연관도가 아니라 **행 패딩**(ARCH-M04). `min_ways_for` = 32 |
| `loop160` | 160 라인 순환 x 8 | 워킹셋 > 용량. FA-LRU 100% 미스, Random FA 43.8%. 모든 줄이 capacity — "capacity 미스" 도 정책에 따라 크게 달라진다 |
| `rand_hot` | 90% 핫 96 라인 + 10% 콜드 4096 라인 | 여기서만 LRU 가 W 와 함께 좋아진다(5100 → 4040). FIFO/Random 은 W 를 올릴수록 **나빠진다**: 직접 사상에서는 핫 라인이 0..95 집합을 차지하고 96..127 집합에는 콜드만 들어가서 콜드 미스의 1/4 이 핫을 못 건드리는데, FA Random 은 아무 줄이나 쫓는다 |

`rand_hot` 의 FA FIFO/Random 은 `conf` 가 4000 가까이다. 완전 연관이라 매핑 충돌은 없는데도 그렇다 — Hill 의 3C 는 LRU 를 기준으로 정의되어 있어서, LRU 가 아닌 정책에서는 "conflict" 칸에 **정책 탓 미스(replacement miss)** 가 섞인다. 반대로 `loop160` 처럼 정책이 LRU 보다 잘하면 `anti` 가 커져 집계 conflict 가 음수가 된다. 면접에서 "3C 는 LRU 기준 분류" 라고 한 마디 붙이면 된다.

**펌웨어 시나리오 (`hot_stream`, 핫 영역 미스만).** 실제 출력:

```text
hot_stream: hot-region misses / 1024 (16 ticks x 64 hot lines)
  W     LRU  FIFO Random  PLRU | LRU + stream non-cacheable
  1      800   800   800   800 |    64
  2      544   544   621   544 |    64
  4     1024  1024   683   992 |    64
  8     1024  1024   694   992 |    64
  128   1024  1024   656   972 |    64
```

4-way 에서 집합 하나를 보면: 핫 2 라인 + tick 마다 새 스트림 3 라인 = 5 > 4 way. LRU 손추적 — tick 1 끝에 [h2 s1 s2 s3] 순서로 남고(s3 가 h1 을 쫓음), tick 2 에서 h1 미스가 LRU 인 h2 를, h2 미스가 s1 을 쫓고 … 매 tick 핫 두 줄이 서로를 쫓아낸다 → 1024/1024. FA 로 가도 160 > 128 라인이라 그대로. 정책을 Random 으로 바꾸면 683 으로 줄지만 여전히 2/3 가 미스다. 스트림 버퍼를 **MPU 로 non-cacheable** 로 두면 64(첫 tick 의 compulsory 만). 정답은 정책 싸움이 아니라 "다시 안 쓸 데이터는 캐시에 넣지 않는다" 다:

1. DMA 버퍼 영역을 MPU 로 non-cacheable — 이 오염 문제와 ARCH-M12 의 일관성 문제(clean/invalidate)가 한꺼번에 사라진다. 대가: CPU 가 그 버퍼를 읽을 때마다 버스 접근.
2. Cortex-M7 이라면 핫 계수표를 **DTCM** 에 둔다 — 캐시가 아니라 항상 1 cycle 인 SRAM. 평균 = 최악(ARCH-B10 Part C).
3. way lockdown 이 있는 캐시(예: L2C-310 류 외부 L2 컨트롤러의 lockdown-by-way)라면 핫 셋을 way 몇 개에 미리 넣고 할당을 막는다(연습 1: 1024 → 0, 대가는 나머지 코드가 쓸 수 있는 way 가 절반).

`min_ways_for`(5% 이내) 실제 출력:

```text
min ways within 5% of FA-LRU (LRU / Random / PLRU):
  colwalk      32  128   64
  loop160       1    1    1
  hot_stream    1    1    1
  rand_hot      8    0    0
```

`loop160`·`hot_stream` 은 FA-LRU 자체가 최악이라 직접 사상으로도 "5% 이내" 를 만족한다 — 기준이 FA-LRU 라는 걸 잊으면 "1-way 면 충분" 이라는 엉뚱한 결론이 나온다. `rand_hot` 의 Random/PLRU 는 FA 에서도 FA-LRU 와 5% 이내가 안 돼서 0. 아키텍처 결정에는 이 표를 **여러 트레이스** 로 봐야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-M05/so && /tmp/esn_ARCH-M05/so; echo "exit $?"
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-M05/san && /tmp/esn_ARCH-M05/san
```

실제 출력 (끝부분; ASan/UBSan 빌드도 같은 `42/42 passed`, 경고 없음):

```text
-- Part C: firmware hot set + DMA stream --
PASS  n == 16u * 160u
      4 KB 4-way LRU   : hot misses 1024 / 1024
      4 KB 4-way FIFO  : hot misses 1024 / 1024
      4 KB 4-way Random: hot misses  683 / 1024
      4 KB 4-way PLRU  : hot misses  992 / 1024
PASS  hm[POL_LRU] == 16u * 64u
...
PASS  min_ways_for(4096, 32, POL_LRU, g_t, n, 0.05) == 32
PASS  min_ways_for(4096, 32, POL_LRU, g_t, n, 0.0) == 1
      rand_hot: min ways within 5% of FA-LRU = 8
PASS  w == 8
PASS  min_ways_for(4096, 32, POL_LRU, g_t, n, -2.0) == 0
42/42 passed
exit 0
```

관찰용 파일 (CHECK 없음, `solution.c` 를 `ARCH_M05_NO_MAIN` 으로 포함):

<!-- code: demo_table.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_table.c -o /tmp/esn_ARCH-M05/table
/tmp/esn_ARCH-M05/table table ; /tmp/esn_ARCH-M05/table belady ; /tmp/esn_ARCH-M05/table plru
```

`belady` 는 이 맥(Apple M6, `Apple clang version 21.0.0 (clang-2100.0.123.102)`)에서 wall 11.0 s, user 5.2 s 걸렸다(직후 `uptime` load average 52 — 다른 세션이 여럿 돌던 중. 시간은 참고용, 결과는 결정적).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고침 |
|---|---|---|
| index 비트를 `log2(C/B)` 로 | 그건 직접 사상의 라인 수. 집합 수는 C/(B·W) | `S = C / (B * W)`, `idx_bits = log2 S` |
| LRU 를 채울 때만 갱신 | 그건 FIFO 다. A B C D A E A 에서 5 가 아니라 6 미스 | 히트 때도 `stamp = clock` |
| 빈 way 가 있는데 정책 희생자를 고름 | 유효 라인을 쓸데없이 쫓는다. 직접 사상이 아닌데 compulsory 직후 미스가 늘어남 | 빈 way 먼저 (`cache_access` 공통) |
| Random 에 전역 `rand()` / `% W` | 테스트 순서에 따라 결과가 바뀌어 재현 불가. W 가 2의 거듭제곱이 아니면 치우침 | 캐시마다 시드 고정 상태, `& (W-1)` |
| PLRU touch 에서 비트를 "접근한 쪽" 으로 | 방금 쓴 way 를 바로 다음 희생자로 만든다 → LRU 의 반대(MRU 교체) | touch 는 **반대쪽**, victim 은 비트를 따라 |
| compulsory 를 "그 집합에 처음 들어올 때" 로 | 쫓겨났다 다시 오는 라인도 compulsory 로 셈 → 3C 합이 FA 와 안 맞음 | 전체 트레이스에서 라인 주소 첫 접촉 |
| capacity 기준을 "같은 W, 큰 용량" 이나 FA-FIFO 로 | Hill 정의는 같은 용량 **FA-LRU** | 그림자 캐시 = `cache_init(C, B, C/B, POL_LRU)` |
| Hill conflict 음수를 버그로 봄 | 집합 매핑이 FA-LRU 의 절벽을 피하면 정상 (`loop160` DM −672) | `anti` 를 따로 세서 설명 |
| "연관도 올리면 미스는 줄거나 같다" | S 가 같이 줄어 매핑이 바뀐다 (0 2 4 반복: DM 5, 2-way 6) | 포함 성질은 S 고정 + LRU 일 때만 |
| 2의 거듭제곱 stride 문제를 연관도로 풀려 함 | `colwalk` 는 W 와 상관없이 슬롯 16 개, 32-way 가 되어서야 들어감 | 행 패딩 (ARCH-M04), 또는 루프 순서 교환 |

PLRU 비트 방향 실수를 코드로:

```c
/* 틀림: 접근한 쪽을 가리킨다 → 방금 쓴 way 가 희생자 */
if (way < lo + half) { bits[node] = 0; node = 2 * node + 1; }
else                 { bits[node] = 1; node = 2 * node + 2; lo += half; }

/* 고침: 반대쪽을 가리킨다 */
if (way < lo + half) { bits[node] = 1; node = 2 * node + 1; }
else                 { bits[node] = 0; node = 2 * node + 2; lo += half; }
```

## 8. 면접 Q&A

<details><summary>Q1. 직접 사상, 집합 연관, 완전 연관의 트레이드오프는?</summary>

미스율은 보통 연관도가 오를수록 줄지만(conflict 감소), 하드웨어 비용이 는다: tag 비교기가 W 개, 데이터 way 를 고르는 W:1 mux 가 히트 경로에 들어가 **히트 시간과 전력** 이 는다(모든 way 의 tag 와 데이터를 병렬로 읽으면 전력이 W 배). 직접 사상은 tag 비교와 데이터 읽기를 겹칠 수 있어 가장 빠르다. 그래서 L1 은 4–8 way, 큰 L2/L3 는 8–16 way 이상, TLB 처럼 작은 구조는 완전 연관이 흔하다.

꼬리: "전력을 줄이는 기법은?" → way prediction(예측한 way 만 먼저 읽기), 직렬 tag→data 접근(L2/L3 에서 흔함, 지연 대신 전력). "연관도를 올렸는데 미스가 늘 수 있나?" → 같은 용량이면 집합 수가 줄어 매핑이 바뀌므로 가능 (라인 0 2 4 반복: DM 5, 2-way 6; `colwalk` 의 LRU).
</details>

<details><summary>Q2. tree-PLRU 를 설명하고, LRU 와 다른 결정을 내리는 예를 들어 보세요.</summary>

W−1 개 비트의 이진 트리. 각 노드 비트는 "다음 희생자가 왼쪽/오른쪽 중 어디 있나". 접근하면 경로의 비트를 접근한 way 의 반대쪽으로 돌리고, 희생자는 루트에서 비트를 따라간다. 4-way 에서 A B C D 를 채우고 A 를 다시 쓰면 비트 = 1 1 0 → 희생자 C(way 2). 정확한 LRU 는 B. 이어서 C 를 쓰면 LRU 는 히트, PLRU 는 미스. PLRU 가 보장하는 것은 "방금 쓴 way 는 안 쫓는다" 뿐이다.

꼬리: "언제 PLRU = LRU?" → 2-way(비트 1개 = 정확한 LRU). "비트 수는?" → 8-way 에서 PLRU 7 bit vs 정확한 LRU 최소 16 bit(⌈log2 8!⌉), 실제 구현은 나이 카운터 24 bit 나 행렬 28 bit (연습 3). 집합 수만큼 곱해지고 매 히트마다 갱신해야 해서 PLRU 를 쓴다. 이 레슨의 `rand_hot` 에서 4-way PLRU 는 LRU 보다 9% 더 미스(4737 vs 4342).
</details>

<details><summary>Q3. 3C 를 정의하고, conflict 미스가 음수가 될 수 있는 이유는?</summary>

compulsory = 처음 만지는 라인(무한 캐시도 미스), capacity = 같은 용량 완전 연관 LRU 도 미스, conflict = 나머지 (Hill & Smith 1989). 교과서식 집계로는 conflict = (이 캐시 미스) − (FA-LRU 미스). 그런데 FA-LRU 가 최적이 아니다: 용량보다 조금 큰 순환 루프에서 FA-LRU 는 100% 미스지만, 직접 사상은 집합으로 나뉘어 일부가 눌러앉는다(9 라인 루프 x 3: DM 13 vs FA-LRU 27 → 집계 conflict −14). 정책이 LRU 가 아니면 "conflict" 에 교체 정책 탓 미스까지 섞인다(`rand_hot` FA-Random 의 conf 4075).

꼬리: "4번째 C 는?" → coherence 미스(다른 코어가 무효화해서 생기는 미스 — ARCH-A04, ARCH-M08). "compulsory 를 줄이는 방법?" → 큰 라인, prefetch (라인이 커지면 같은 용량에서 라인 수가 줄어 다른 미스가 늘 수 있다).
</details>

<details><summary>Q4. LRU 가 최악이 되는 접근 패턴과, 실제 CPU 가 그것을 어떻게 피하나요?</summary>

워킹셋이 용량(또는 집합의 way 수)보다 조금 큰 순환 루프와, 한 번만 쓰는 스트리밍 데이터(스캔)다. 순환 루프에서는 다음에 쓸 라인이 항상 가장 오래된 라인이라 LRU 히트율이 0 — `loop160` FA-LRU 100% vs FA-Random 43.8%. 스캔은 한 번 쓰고 버릴 라인이 핫 라인을 밀어낸다(`hot_stream`).

꼬리: "실제 칩은?" → 공개 자료와 역공학 논문에 따르면 L1 은 PLRU 류나 pseudo-random, 큰 LLC 는 삽입 위치를 바꾸는 정책(새 라인을 MRU 가 아니라 LRU 근처에 넣는 LIP/BIP, RRIP 계열)이나 집합 dueling 같은 적응형 정책을 쓴다고 알려져 있다 — 코어별 세부는 비공개가 많아 단정하지 않는다. 소프트웨어 쪽 회피책: non-temporal load/store, 스트림 버퍼를 non-cacheable 로, 루프 타일링(ARCH-M07).
</details>

<details><summary>Q5. Belady 이상이 뭐고, LRU 에는 왜 없나요?</summary>

FIFO 에서 캐시(프레임) 크기를 늘렸는데 미스가 느는 현상. 고전 문자열 1 2 3 4 1 2 5 1 2 3 4 5 는 FIFO 3 칸 9 미스, 4 칸 10 미스 (연습 2 에서 확인, LRU 는 10 → 8). LRU 는 **스택 알고리즘**(Mattson 1970)이다: 크기 k 캐시의 내용이 크기 k+1 캐시 내용의 부분집합으로 항상 유지되므로 큰 쪽이 더 많이 미스할 수 없다. 이 레슨 테스트는 S 고정에서 W → 2W 일 때 접근 하나하나가 hit(W) ⊆ hit(2W) 인지 확인한다.

꼬리: "그럼 두 배로 늘려도 FIFO 이상이 나나?" → 이 레슨의 탐색(2→4, 4→8 라인, 무작위 200만 개)에서는 못 찾았다 — 못 찾았다는 것이지 불가능하다는 증명은 아니다. "최적 정책은?" → Belady 의 MIN/OPT(가장 먼 미래에 쓸 라인을 쫓기). 미래를 알아야 해서 오프라인 기준선으로만 쓴다.
</details>

<details><summary>Q6. Cortex-M7 펌웨어에서 제어 루프의 계수표가 DMA 버퍼 복사 때문에 계속 캐시에서 밀려납니다. 어떻게 하나요?</summary>

먼저 숫자: 이 레슨 모델(4 KB 4-way)에서 핫 64 라인 + tick 마다 새 스트림 96 라인이면 LRU/FIFO 는 핫 미스 100%, Random 으로 바꿔도 67%. 정책으로는 못 이긴다. 해법은 (1) 계수표를 **DTCM** 으로 — 캐시를 거치지 않는 1 cycle SRAM, 최악 지연이 결정적; (2) DMA 버퍼 영역을 MPU 로 **non-cacheable** — 오염이 사라지고(핫 미스 64 = 첫 tick 뿐) DMA 일관성 유지보수(ARCH-M12)도 필요 없어진다; (3) 버퍼를 꼭 캐시해야 하면 처리 후 invalidate 해서 빨리 비우거나, 지원하는 캐시라면 way lockdown.

꼬리: "non-cacheable 의 비용은?" → CPU 가 그 버퍼를 읽을 때마다 버스(AXI) 접근, 바이트 단위 파싱이면 느리다 — 한 번 memcpy 로 캐시 가능한 작업 버퍼로 옮기는 설계도 흔하다. "MPU 영역 설정은?" → ARCH-M15(MPU 모델), 실제 코어 설정은 BM-A08.
</details>

<details><summary>Q7. (실리콘 검증) 모르는 칩의 L1 연관도와 교체 정책을 측정으로 알아내려면?</summary>

연관도: 같은 집합에 매핑되는 주소(간격 = S·B, 즉 "way 크기")를 k 개 만들어 포인터 체이싱으로 돌리며 접근당 지연을 잰다. k ≤ W 이면 L1 히트 지연, k = W+1 에서 (LRU/FIFO 라면) 급격히 오른다. 이 레슨의 `resident_lines` 계산(연습 4)이 그 원리다. 간격을 모르면 간격을 스윕하며 지연이 뛰는 지점을 찾는다(MEM-B10 포인터 체이싱). 정책: W 개를 채우고 그중 하나를 다시 쓴 뒤 새 라인 하나를 넣고, 어느 라인이 미스가 나는지(타이밍 또는 PMU refill 카운터)로 희생자를 본다 — A B C D A E 후 B 가 미스면 LRU, A 면 FIFO, C 면 이 레슨의 PLRU, 반복마다 다르면 random 계열.

꼬리: "잡음은?" → 하드웨어 prefetcher 를 끄거나 무작위 순서 체이싱으로 무력화, TLB 미스 분리(큰 페이지), 여러 번 반복해 중앙값. 시뮬레이터(이 레슨)는 측정 결과를 대조할 "기대값 생성기" 로 쓴다.
</details>

## 9. 추가 연습

**연습 1 — way lockdown.** 본문 `hot_stream` 과 같은 패턴(4 KB 4-way 32 B, LRU)에서 할당 마스크를 way 0–1 만 열고 핫 64 라인을 한 번 읽은 뒤, 마스크를 way 2–3 으로 바꿔 잠근다(히트는 어느 way 에서나 가능). 16 tick 동안 핫 미스는?

<details><summary>답</summary>

**0** (잠그지 않으면 1024). 핫 라인은 집합마다 2 개라 way 0–1 에 딱 들어가고, 이후 스트림은 way 2–3 사이에서만 교체된다. 실제 L2 컨트롤러의 lockdown-by-way 도 "그 way 로의 할당을 막는다" 는 식이라, 미리 넣을 때는 넣을 way 만 열어 두고 데이터를 만진 뒤 잠근다 (레지스터 이름·비트는 해당 TRM 확인). 대가: 나머지 모든 코드와 데이터가 쓸 수 있는 연관도가 4 → 2 로 준다. DTCM 이 있는 MCU 라면 TCM 이 더 단순하다.
</details>

**연습 2 — Belady.** 2의 거듭제곱 제약이 없는 k 칸 FIFO 와 k 칸 LRU 로 1 2 3 4 1 2 5 1 2 3 4 5 를 돌려 3 칸 / 4 칸 미스를 구하라.

<details><summary>답</summary>

FIFO 3 칸 **9**, 4 칸 **10** (이상). LRU 3 칸 **10**, 4 칸 **8**. FIFO 4 칸은 1 2 3 4 를 채운 뒤 1, 2 히트(순서 안 바뀜) → 5 가 1 을 쫓고 → 1 이 2 를, 2 가 3 을 … 차례로 "곧 쓸 라인" 을 쫓는다. 3 칸 FIFO 는 5 가 들어올 때 이미 순서가 어긋나 1, 2 가 남는다. LRU 는 스택 알고리즘이라 칸이 늘면 미스가 늘 수 없다.
</details>

**연습 3 — 교체 상태 비트.** 집합당 상태 비트를 W = 4, 8, 16 에 대해: 정확한 LRU 최소(⌈log2 W!⌉), 행렬 W(W−1)/2, 나이 카운터 W·⌈log2 W⌉, PLRU W−1, FIFO ⌈log2 W⌉. 32 KB 8-way 64 B 캐시 전체로는?

<details><summary>답</summary>

| W | LRU 최소 | 행렬 | 나이 | PLRU | FIFO |
|---|---|---|---|---|---|
| 4 | 5 | 6 | 8 | 3 | 2 |
| 8 | 16 | 28 | 24 | 7 | 3 |
| 16 | 45 | 120 | 64 | 15 | 4 |

32 KB / (8 · 64) = 64 집합 → 나이 카운터 1536 bit vs PLRU 448 bit. 비트 수보다 큰 문제는 **갱신**: 정확한 LRU 는 히트마다 여러 way 의 나이를 읽고-고쳐-써야 하고, PLRU 는 경로의 log2 W 비트를 덮어쓰기만 한다. Random 은 집합당 0 bit (전체에 LFSR 하나).
</details>

**연습 4 — stride 와 상주 라인.** 32 KB 8-way 64 B(S = 64)에서 간격 4096 B, 4160 B, 2048 B 로 놓인 배열 원소는 각각 최대 몇 개까지 동시에 캐시에 머물 수 있나? 본문 `colwalk` 캐시(4 KB, 32 B, 간격 256 B)는 W = 1 과 4 에서?

<details><summary>답</summary>

간격이 닿는 집합 수 = S / gcd(S, (stride/B) mod S), 상주 가능 = W × 그 수. 4096 B: stride/B = 64 ≡ 0 → 집합 1 개 → **8**. 4160 B: 65 ≡ 1 → 64 집합 → **512**(캐시 전체). 2048 B: 32 → 2 집합 → **16**. `colwalk`: 256/32 = 8 → DM(S = 128) 16 집합 × 1 = **16**, 4-way(S = 32) 4 집합 × 4 = **16** — 연관도를 올려도 슬롯이 같다. 그래서 2의 거듭제곱 간격은 패딩 한 줄(4096 → 4160)로 고친다. 실리콘 검증에서 연관도를 잴 때는 반대로 간격 S·B 를 일부러 쓴다(Q7).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-M05/pr && /tmp/esn_ARCH-M05/pr
```

```text
-- 연습 1 --
      hot misses: unlocked 1024 / 1024, ways 0-1 locked 0 / 1024
PASS  unl == 1024
PASS  lk == 0
-- 연습 2 --
PASS  frames_misses(0, 3, b, 12) == 9 && frames_misses(0, 4, b, 12) == 10
PASS  frames_misses(1, 3, b, 12) == 10 && frames_misses(1, 4, b, 12) == 8
-- 연습 3 --
PASS  lru_min_bits(4) == 5 && lru_matrix_bits(4) == 6 && lru_age_bits(4) == 8 && plru_bits(4) == 3 && fifo_bits(4) == 2
PASS  lru_min_bits(8) == 16 && lru_matrix_bits(8) == 28 && lru_age_bits(8) == 24 && plru_bits(8) == 7
PASS  lru_min_bits(16) == 45 && plru_bits(16) == 15
PASS  64 * lru_age_bits(8) == 1536 && 64 * plru_bits(8) == 448
-- 연습 4 --
PASS  resident_lines(32768, 64, 8, 4096) == 8
PASS  resident_lines(32768, 64, 8, 4160) == 512
PASS  resident_lines(32768, 64, 8, 2048) == 16
PASS  resident_lines(4096, 32, 1, 256) == 16
PASS  resident_lines(4096, 32, 4, 256) == 16
13/13 passed
```

## 10. 핵심 정리

- S = C / (B·W), index = log2 S 비트, tag = 나머지. W = 1 직접 사상, S = 1 완전 연관. 미스 때 빈 way 먼저, 꽉 차면 정책이 희생자.
- LRU = 히트·채움 때 도장, FIFO = 채움 때만, Random = 시드 고정 LFSR, PLRU = W−1 비트 트리(touch 는 반대쪽, victim 은 비트를 따라). 2-way PLRU = LRU, 4-way 부터 갈린다(A B C D A E C: 5 vs 6).
- 3C (Hill): compulsory = 첫 접촉, capacity = 같은 용량 FA-LRU 도 미스, conflict = 나머지. 집계 conflict = conf − anti 는 음수 가능(9 라인 루프: DM −14). LRU 가 아닌 정책에선 conflict 에 정책 탓 미스가 섞인다.
- LRU 는 스택 알고리즘(S 고정 W↑ → 미스↓). FIFO 는 Belady 이상(1 2 3 4 1 2 5 1 2 3 4 5: 9 → 10). 같은 용량에서 W 를 올리면 매핑이 바뀌어 미스가 늘 수도 있다.
- LRU 절벽: 워킹셋이 용량을 조금만 넘어도 순환 루프는 100% 미스, Random 이 이긴다(`loop160` 100% vs 43.8%).
- 2의 거듭제곱 stride 는 연관도로 못 고친다(`colwalk` 슬롯 16 개) → 패딩. 다시 안 쓸 스트림은 캐시에 넣지 않는다 → MPU non-cacheable / DTCM / lockdown (`hot_stream` 1024 → 64 → 0).
- 아키텍처 결정은 여러 트레이스 × {W} × {정책} 표로. "FA-LRU 의 5% 이내" 같은 기준은 FA-LRU 자체가 나쁜 트레이스에서 엉뚱한 답을 낸다.

## 참고자료

- Mark D. Hill, Alan Jay Smith, "Evaluating Associativity in CPU Caches", *IEEE Transactions on Computers* 38(12), 1989 — 3C 모델의 표준 정의.
- R. L. Mattson, J. Gecsei, D. R. Slutz, I. L. Traiger, "Evaluation Techniques for Storage Hierarchies", *IBM Systems Journal* 9(2), 1970 — 스택 알고리즘, 포함 성질.
- L. A. Belady, "A Study of Replacement Algorithms for a Virtual-Storage Computer", *IBM Systems Journal* 5(2), 1966 — MIN(OPT). L. A. Belady, R. A. Nelson, G. S. Shedler, "An Anomaly in Space-Time Characteristics of Certain Programs Running in a Paging Machine", *CACM* 12(6), 1969 — FIFO 이상.
- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach* (6th ed.), Appendix B (캐시 기초, 3C) · Chapter 2 (way prediction 등 고급 최적화).
- Aamer Jaleel et al., "High Performance Cache Replacement Using Re-Reference Interval Prediction (RRIP)", ISCA 2010 — 스캔 저항 LLC 정책.
- Arm, *Cortex-M7 Processor Technical Reference Manual* (DDI 0489) — L1 캐시 구성(D-cache 4-way, 32 B 라인), TCM. 교체 정책 세부는 TRM 원문 확인.
- Arm, *CoreLink Level 2 Cache Controller L2C-310 Technical Reference Manual* — lockdown by way / by line (레지스터 세부는 원문 확인).
- 관련 레슨: ARCH-M04 (직접 사상, 패딩), ARCH-B10 (AMAT), ARCH-M06 (쓰기 정책), ARCH-M07 (matmul 실측 + LRU 시뮬), ARCH-M08 (false sharing), ARCH-M12 (DMA clean/invalidate), ARCH-A04 (MESI, coherence 미스), ARCH-M15 (MPU 모델), MEM-B10 (포인터 체이싱 지연 측정), MEM-M03 (다단 캐시, 다음 단계), BM-A08 (실제 코어 MPU, 다음 단계).

---
id: ARCH-M07
time: 100분
updated: 2026-11-11
---
# ARCH-M07 캐시 친화 코드: 루프 순서와 타일링

## 학습 목표

- 행렬 곱의 루프 순서 ijk / ikj / jki 가 **안쪽 루프에서 어느 배열을 행으로, 어느 배열을 열로 걷는지** 로 세 무리로 갈린다는 것을 말하고, 안쪽 반복당 미스를 `1 + e/L`, `2e/L`, `2` 로 어림할 수 있다.
- Apple M6 에서 n = 512 / 1024 / 2048 GFLOP/s 를 직접 재고, **SIMD 효과와 캐시 효과를 나눠서** 말할 수 있다 (`-Rpass=loop-vectorize`, `-S`, `-fno-vectorize` 빌드 비교).
- ARCH-M04/M05 의 캐시 시뮬레이터를 축소해 넣고 같은 루프의 주소 트레이스를 흘려, 측정 순서를 미스 수로 설명할 수 있다. 시뮬레이터가 **설명하지 못하는 것** (L2, 프리페처, 루프 오버헤드) 도 구분한다.
- 2의 거듭제곱 행 간격이 열 걷기와 타일을 몇 개 set 에 몰아넣는 이유를 `128 / gcd(m, 128)` 로 계산하고, ld 패딩 / 타일 패킹으로 고칠 수 있다.
- Cortex-M7 류 16 KB D-cache 에서 타일 크기를 "3 타일이 캐시에 들어가게" 로 어림한 뒤 시뮬레이터로 확인한다.

## 1. 문제

"같은 O(n³) 인데 루프 순서만 바꿨더니 30배 빨라졌다 — 왜?" 는 성능 엔지니어링·AI 가속기·DSP 펌웨어 면접의 단골이고, 답은 **캐시는 라인 단위로 가져오므로 연속 접근(공간 지역성)과 재사용 거리(시간 지역성)가 FLOP 수보다 중요하다** 는 것이다. Cortex-M7 위의 CMSIS-NN 커널, NPU 의 타일 스케줄러, BLAS 의 블로킹이 모두 같은 생각이다.

이미 아는 것(다시 가르치지 않는다): AMAT 와 미스율이 CPI 에 미치는 영향(ARCH-B10), 직접 사상·N-way·LRU 시뮬레이터(ARCH-M04/M05 — 여기서는 축소판을 그대로 넣었다), 쓰기 정책(ARCH-M06 — 여기서는 쓰기를 write-allocate 읽기처럼 센다), 이 맥의 `hw.cachelinesize` = 128 B 와 측정 방식(ARCH-M08). 다음 단계: 다단 캐시 모델(MEM-M03), 루프라인(roofline, MEM-A08), 쓰기 할당 대역폭(RFO, MEM-M06).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m07a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">안쪽 루프가 걷는 방향 (행 우선 배열: 행 = 연속 바이트, 열 = ld·e 바이트 간격)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="70">ijk</text><text x="10" y="86">안쪽 k</text>
    <text x="10" y="160">ikj</text><text x="10" y="176">안쪽 j</text>
    <text x="10" y="250">jki</text><text x="10" y="266">안쪽 i</text>
    <text x="95" y="40">C[i][j] +=</text><text x="235" y="40">A[i][k]</text><text x="375" y="40">× B[k][j]</text>
    <text x="520" y="40">안쪽 반복당 미스 (e=8, L=128)</text>
  </g>
  <rect x="90" y="48" width="70" height="56" class="f-mut" stroke="currentColor"/>
  <rect x="230" y="48" width="70" height="56" class="f-mut" stroke="currentColor"/>
  <line x1="236" y1="70" x2="294" y2="70" stroke="currentColor" stroke-width="2" marker-end="url(#m07a)"/>
  <rect x="370" y="48" width="70" height="56" class="f-warn" stroke="currentColor"/>
  <line x1="400" y1="52" x2="400" y2="100" stroke="currentColor" stroke-width="2" marker-end="url(#m07a)"/>
  <text x="520" y="80" fill="currentColor" font-size="12">A 행 1/16 + B 열 1 = 1.06</text>
  <rect x="90" y="138" width="70" height="56" class="f-ok" stroke="currentColor"/>
  <line x1="96" y1="160" x2="154" y2="160" stroke="currentColor" stroke-width="2" marker-end="url(#m07a)"/>
  <rect x="230" y="138" width="70" height="56" class="f-mut" stroke="currentColor"/>
  <text x="250" y="170" fill="currentColor" font-size="12">r 고정</text>
  <rect x="370" y="138" width="70" height="56" class="f-ok" stroke="currentColor"/>
  <line x1="376" y1="160" x2="434" y2="160" stroke="currentColor" stroke-width="2" marker-end="url(#m07a)"/>
  <text x="520" y="170" fill="currentColor" font-size="12">B 행 1/16 + C 행 1/16 = 0.125</text>
  <rect x="90" y="228" width="70" height="56" class="f-warn" stroke="currentColor"/>
  <line x1="120" y1="232" x2="120" y2="280" stroke="currentColor" stroke-width="2" marker-end="url(#m07a)"/>
  <rect x="230" y="228" width="70" height="56" class="f-warn" stroke="currentColor"/>
  <line x1="260" y1="232" x2="260" y2="280" stroke="currentColor" stroke-width="2" marker-end="url(#m07a)"/>
  <rect x="370" y="228" width="70" height="56" class="f-mut" stroke="currentColor"/>
  <text x="390" y="260" fill="currentColor" font-size="12">r 고정</text>
  <text x="520" y="260" fill="currentColor" font-size="12">A 열 1 + C 열 1 = 2</text>
</svg>
```

체크 테스트(`starter.c` → `solution.c`)는 **결정적인 것만** 본다: 모든 변형이 ijk 와 비트 단위로 같은 결과, 시뮬레이터의 LRU 동작과 접근 수, 미스 수의 순서, ld 패딩 규칙과 타일 선택. **시간은 `demo_bench.c` 에서만**, 큰 시뮬레이션 표는 `demo_sim.c` 에서 만든다.

### Part A — 루프 순서와 타일링 구현

- `mm_ijk` (주어짐), `mm_ikj`, `mm_jki`, `mm_tiled(T)` — `C = A·B`, n×n double, 행 우선, 원소 (r, c) = `M[r*ld + c]`, `ld ≥ n`. 패딩 열(c ≥ n)은 건드리지 않는다.
- 입력은 −4…4 정수값 double 이라 곱·합이 전부 정확 → 결과가 **`==` 로 같아야** 한다. n ∈ {1, 7, 16, 33, 64, 100}, ld = n 과 패딩 (33 → 40, 100 → 104), T ∈ {1, 3, 4, 8, 16, 32, 50, 128} (n 의 약수가 아니어도, n 보다 커도).
- 관찰: `demo_bench.c` — M6 에서 n = 512 / 1024 / 2048 GFLOP/s, 벡터화 끈 빌드, 타일 스윕, ld 패딩, Accelerate 천장.

### Part B — 축소 LRU 시뮬레이터로 미스 설명하기

- `cache_access` — N-way LRU 접근 하나 (빈 way 먼저, 아니면 가장 오래된 way 교체). 2-way 손 계산 순서가 `MMHMMHMH`.
- `trace_matmul` — 각 `mm_*` 가 내는 메모리 접근을 같은 순서로 (ijk 는 주어짐). 스칼라 `s`, `r` 는 레지스터, `C += x` 는 읽기+쓰기, 0 으로 지우기는 쓰기.
- 접근 수 공식 (ijk `2n³+n²`, ikj·jki `3n³+2n²`, tiled `3n³+n²+n²⌈n/T⌉`), 다 들어가는 경우엔 compulsory 만 (n=16 → 48), M6 L1D (128 KB, 128 B, 8-way 가정) n=128 에서 **jki > ijk > ikj > tiled**.

### Part C — 펌웨어: Cortex-M7 류 D-cache 에서 ld 패딩과 타일 고르기

- `pad_ld(n, elem, line)` — n 이상, 행 간격이 라인의 배수이면서 **행 간격의 라인 수가 홀수** 인 가장 작은 ld.
- `best_tile(...)` — 후보 T 마다 시뮬레이터로 미스를 재서 가장 적은 T (같으면 큰 T).
- 16 KB · 4-way · 32 B 라인 (Cortex-M7 D-cache 는 4-way 32 B 라인, 크기는 칩마다 다르다 — 예: 16 KB), float 128×128: 패딩으로 ijk 미스 1/8, 패딩하면 T=32, 안 하면 T=16 이 이긴다.

## 2. Problem (English)

You are optimizing `C = A·B` (n×n, row-major, leading dimension `ld ≥ n`) on Apple M6 and on a Cortex-M7-class MCU, and you must justify every speed-up with cache behaviour.

**Part A.** Implement `mm_ikj`, `mm_jki` and `mm_tiled(n, ld, T, A, B, C)` (blocked `ii → jj → kk`, `ikj` inside a block, `T` need not divide `n`). Each overwrites `C[0..n)×[0..n)` and leaves padding columns untouched. With small-integer inputs every variant must equal `mm_ijk` exactly.

**Part B.** Implement `bool cache_access(cache_t *c, const trace_rec_t *r)` for an N-way set-associative LRU cache (byte address → offset/index/tag; fill an invalid way first, else evict the least-recently-used way; write-allocate, writes counted like reads). Implement the `ikj`, `jki` and `tiled` cases of `trace_matmul`, which replays exactly the loads/stores the corresponding `mm_*` performs (scalars live in registers; `C[i][j] += x` is a read then a write; zeroing is a write). Matrices are laid out back-to-back from `0x10000000`.

**Part C.** Implement `int pad_ld(int n, uint32_t elem, uint32_t line)`: the smallest `ld ≥ n` such that `ld·elem` is a multiple of `line` and `ld·elem/line` is odd. Implement `int best_tile(cap, line, ways, n, ld, elem, cand, nc, misses_out)`: simulate the tiled trace for each candidate, return the one with fewest misses (ties → larger T; `nc < 1` → 0).

Example: `pad_ld(64, 4, 32) == 72`, `pad_ld(1024, 8, 128) == 1040`. On a 16 KB / 32 B / 4-way cache with float n = 128: ijk misses 2 133 136 at `ld = 128` vs 266 240 at `ld = 136`; best tile is 32 with padding, 16 without.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M07
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
mkdir -p /tmp/esn_ARCH-M07
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-M07/st && /tmp/esn_ARCH-M07/st; echo "exit $?"
```

실제 출력 (발췌):

```text
-- Part A: loop orders and tiling (exact vs ijk) --
FAIL  C2[0] == 19 && C2[1] == 22 && C2[2] == 43 && C2[3] == 50  (line 221)
...
FAIL  variants_match(100, 104)  (line 240)
-- Part B: compact LRU cache sim + matmul address traces --
PASS  cache_init(&c, 128 * 1024, 128, 8)
PASS  c.sets == 128 && c.off_bits == 7 && c.idx_bits == 7
...
      LRU 2-way pattern: MMMMMMMM
FAIL  strcmp(pat, "MMHMMHMH") == 0  (line 260)
...
      M6-L1 n=128 ijk   accesses         0 misses        0  miss rate  0.000%  misses/n^3 0.0000
...
FAIL  pad_ld(64, 4, 32) == 72  (line 296)
...
7/43 passed
exit 1
```

## 4. 힌트

<details><summary>힌트 1 — ikj 는 "C 행 하나를 B 행들의 선형 결합으로"</summary>

`C[i][·] = Σ_k A[i][k] · B[k][·]`. i 를 고정하고 C 의 i 행을 0 으로 지운 다음, k 마다 스칼라 `r = A[i][k]` 를 B 의 k 행 전체에 곱해 더한다. 안쪽 j 루프는 B 행과 C 행을 **둘 다 연속으로** 걷는다 — 벡터화도 여기서 된다. jki 는 같은 생각을 열로: C 의 j 열 = Σ_k B[k][j] · A[·][k].
</details>

<details><summary>힌트 2 — 타일 경계와 0 으로 지우기</summary>

C 전체를 먼저 0 으로 지운 다음 블록 루프를 돈다 (블록 안에서 지우면 kk 마다 지워진다). 끝은 `imin(ii + T, n)`. 같은 (i, j) 에 대해 k 가 0, 1, 2 … 순서로 더해지는지 확인하면, 정수 입력이 아니어도 ijk 와 같은 순서다 (ii → jj → kk 순서라서).
</details>

<details><summary>힌트 3 — LRU 는 "마지막 사용 시각" 하나로</summary>

way 마다 `last` 시각을 두고, 접근마다 `tick++`. hit 이면 `last = tick`. miss 이면 빈 way 를 먼저 쓰고, 다 차 있으면 `last` 가 가장 작은 way 를 내쫓는다. 빈 way 의 키를 0 으로 보면 "빈 way 먼저" 와 "가장 오래된 것" 이 한 비교로 된다 (tick 은 1 부터).
</details>

<details><summary>힌트 4 — 트레이스는 코드를 그대로 베낀다</summary>

`mm_ikj` 의 루프를 복사하고 각 배열 접근을 `emit(...)` 으로 바꾼다. `C[i*ld+j] += r * B[k*ld+j]` → B 읽기, C 읽기, C 쓰기 (3번). `r = A[i*ld+k]` 는 안쪽 루프 밖에서 1번. 접근 수 공식이 맞는지부터 확인하면 순서 실수를 잡는다.
</details>

<details><summary>힌트 5 — 홀수 라인 간격이 왜 좋은가</summary>

행 간격이 m 라인이면 열을 걸을 때 k 번째 행의 set 은 `(시작 + k·m) mod S`. 이 수열은 `S / gcd(m, S)` 개 set 만 돈다. S 는 2의 거듭제곱이므로 m 이 홀수면 gcd = 1 → 모든 set. `pad_ld` 는 ld 를 하나씩 올리며 두 조건(라인 배수, 라인 수 홀수)을 검사하면 된다.
</details>

## 5. 풀이

### Part A — 세 무리와 안쪽 반복당 미스

여섯 가지 순서는 **안쪽 루프 변수** 로 세 무리가 된다 (CS:APP 6.6.2 와 같은 분석, 여기선 e = 8 B double, L = 128 B 라인 → 라인당 16 원소):

| 안쪽 변수 | 순서 | 안쪽 문장 | 걷는 방향 | 안쪽 반복당 미스 (라인이 못 버틸 때) |
|---|---|---|---|---|
| k | ijk, jik | `s += A[i][k]*B[k][j]` | A 행 · B **열** | 1/16 + 1 = **1.06** |
| j | ikj, kij | `C[i][j] += r*B[k][j]` | B 행 · C 행 | 1/16 + 1/16 = **0.125** |
| i | jki, kji | `C[i][j] += A[i][k]*r` | A **열** · C **열** | 1 + 1 = **2** |

연습 1 에서 작은 캐시(1 KB)로 여섯 순서 모두 이 값이 ±1% 로 나오는 것을 확인한다 (e/L = 1/4 로 바꿔서: 1.25 / 0.5 / 2.0 예측, 1.258 / 0.508 / 2.008 시뮬).

구현 포인트:

| 포인트 | 이유 |
|---|---|
| ikj 는 i 행을 지우고 시작 | 호출자가 C 를 지워 준다고 가정하면 두 번째 호출에서 누적된다 (테스트 `C3 = 99`) |
| `restrict` | B 와 C 가 겹칠 수 있으면 컴파일러가 실행 시간 별칭 검사를 넣거나 벡터화를 포기한다 |
| 정수값 입력 | 덧셈 순서가 다른 구현(예: 타일 kk 바깥 순서)을 써도 `==` 비교가 가능. 실수 입력이면 상대 오차로 비교해야 한다 |
| 타일 순서 ii → jj → kk | C 타일 하나가 kk 루프 내내 캐시에 머문다. (i, j) 마다 k 가 오름차순으로 더해져 ijk 와 같은 순서 → 실측에서도 maxdiff 0 |

#### 벡터화 확인 — SIMD 이득을 캐시 이득으로 착각하지 않기

```bash
cc -std=c11 -O2 -Rpass=loop-vectorize -c demo_bench.c -o /dev/null 2>&1 | grep remark
cc -std=c11 -O2 -S -o vec.s demo_bench.c     # 함수별로 fmla.2d / fmadd / ccmp w1,#1 개수 세기
```

```text
demo_bench.c:34:13: remark: vectorized loop (vectorization width: 2, interleaved count: 4) [-Rpass=loop-vectorize]   ← ijk 안쪽 k
demo_bench.c:43:13: remark: vectorized loop (vectorization width: 2, interleaved count: 4) [-Rpass=loop-vectorize]   ← ikj 안쪽 j
demo_bench.c:49:9: remark: vectorized loop (vectorization width: 2, interleaved count: 4) [-Rpass=loop-vectorize]    ← jki 열 지우기
demo_bench.c:52:13: remark: vectorized loop (vectorization width: 2, interleaved count: 4) [-Rpass=loop-vectorize]   ← jki 안쪽 i
demo_bench.c:66:25: remark: vectorized loop (vectorization width: 2, interleaved count: 4) [-Rpass=loop-vectorize]   ← tiled 안쪽 j

# -S 출력을 함수별로 잘라 grep -c 로 센 결과 (기본 빌드). novec 빌드는 넷 다 fmla.2d=0, fmadd=1
mm_ijk    fmla.2d=0 fmadd(scalar)=1 fmul.2d=4 ld==1-version=1
mm_ikj    fmla.2d=4 fmadd(scalar)=1 fmul.2d=0 ld==1-version=0
mm_jki    fmla.2d=4 fmadd(scalar)=1 fmul.2d=0 ld==1-version=1
mm_tiled  fmla.2d=4 fmadd(scalar)=1 fmul.2d=0 ld==1-version=0
```

remark 만 보면 ijk 와 jki 도 "벡터화됐다" 고 나온다. 함정이다. asm 을 보면 두 함수의 벡터 경로는 `cmp w0, #7; ccmp w1, #1, #0, hi` — 즉 **ld == 1 일 때만** (열이 연속일 때만) 들어가는 버전이고, 실제 ld = n 에서는 스칼라 루프가 돈다:

```text
; mm_ijk — 실제로 도는 안쪽 k 루프 (ld != 1 경로)
LBB5_11:
	ldr	d1, [x5], #8          ; A[i][k]  (연속, +8)
	ldr	d2, [x6]              ; B[k][j]
	fmadd	d0, d1, d2, d0    ; s += A*B  (스칼라)
	add	x6, x6, x14           ; x14 = ld*8 → 다음 행 (열 걷기)
	subs	x7, x7, #1
	b.ne	LBB5_11

; mm_ikj — 안쪽 j 루프 (반복당 double 8개)
LBB3_8:
	ldp	q1, q2, [x13, #-32]   ; B[k][j..j+3]
	ldp	q3, q4, [x13], #64    ; B[k][j+4..j+7]
	ldp	q5, q6, [x12, #-32]   ; C[i][j..j+3]
	ldp	q7, q16, [x12]        ; C[i][j+4..j+7]
	fmla.2d	v5, v1, v0[0]     ; C += B * r   (2개씩 × 4)
	fmla.2d	v6, v2, v0[0]
	fmla.2d	v7, v3, v0[0]
	fmla.2d	v16, v4, v0[0]
	stp	q5, q6, [x12, #-32]
	stp	q7, q16, [x12], #64
	subs	x14, x14, #8
	b.ne	LBB3_8
```
(`;` 뒤 설명은 덧붙인 것, 명령은 `cc -std=c11 -O2 -S` 출력 그대로.) ikj 도 FMA 하나마다 C 를 읽고 쓴다 — load 2 + store 1. 이 점이 Q8 의 열쇠다.

그래서 공정한 비교는 두 가지다: (1) 기본 빌드 — ikj/tiled 는 NEON `fmla.2d`, ijk/jki 는 스칼라 `fmadd`; (2) `-fno-vectorize -fno-slp-vectorize` 빌드 — 넷 다 스칼라 `fmadd` (확인: novec 빌드에서 `fmla.2d` 0개). 두 빌드의 차이가 SIMD 몫이고, novec 빌드 안의 차이가 **캐시(메모리 접근 순서) 몫** 이다. 덤: LLVM 의 loop-interchange 는 이 빌드에서 돌지 않았다 (`-Rpass=loop-interchange` remark 없음) — 컴파일러가 ijk 를 ikj 로 바꿔 주지 않는다.

#### 측정: M6 GFLOP/s (`demo_bench.c main`)

```bash
cc -std=c11 -O2 demo_bench.c -o /tmp/esn_ARCH-M07/bench                                      # 기본 (NEON 자동 벡터화)
cc -std=c11 -O2 -fno-vectorize -fno-slp-vectorize demo_bench.c -o /tmp/esn_ARCH-M07/bench_novec
/tmp/esn_ARCH-M07/bench main 64 ; /tmp/esn_ARCH-M07/bench_novec main 64
```

기계: `Apple M6` (Super 2 + Performance 4 + Efficiency 6), `Apple clang version 21.0.0 (clang-2100.0.123.102)`, L1D Super 128 KB / P 64 KB / E 96 KB, L2 Super·P 20 MB / E 8 MB, `hw.cachelinesize` 128, 페이지 16 KB. QoS USER_INTERACTIVE 요청, 코어 고정 불가 → 실행 직후 `pthread_cpu_number_np` 값을 적었다 (0–5 E, 6–7 Super, 8–11 P — ARCH-M08 의 추정 매핑). **load average 9.5–15.7** (다른 세션들이 동시에 빌드·벤치 중) — 절대값보다 배수를 믿는다. 5회 중앙값, 한 번이 4 초를 넘으면 1회 (runs 열). maxdiff 는 전부 0 (모든 변형이 같은 k 순서로 fma 누적).

| n | 변형 | 기본 빌드 GFLOP/s (min–max, runs) | 코어 | `-fno-vectorize` GFLOP/s | SIMD 배수 |
|---|---|---|---|---|---|
| 512 | ijk | **1.640** (1.501–1.857, 5) | 10 6 9 11 11 | 1.728 (5) | 0.95 |
| 512 | ikj | **14.689** (13.545–16.583, 5) | 7 7 10 11 6 | 6.266 (5) | 2.34 |
| 512 | jki | **0.405** (0.385–0.426, 5) | 10 10 11 9 9 | 0.406 (5) | 1.00 |
| 512 | tiled T=64 | **10.829** (9.381–11.632, 5) | 6 5 7 9 11 | 6.799 (5) | 1.59 |
| 1024 | ijk | **1.212** (1.195–1.270, 5) | 6 9 8 7 11 | 1.283 (5) | 0.94 |
| 1024 | ikj | **13.632** (11.610–13.913, 5) | 6 8 11 9 8 | 6.700 (5) | 2.03 |
| 1024 | jki | **0.331** (0.331–0.331, 1) | 6 | 0.357 (1) | 0.93 |
| 1024 | tiled T=64 | **8.643** (8.311–9.343, 5) | 9 6 9 9 8 | 5.107 (5) | 1.69 |
| 2048 | ijk | **0.643** (0.643–0.643, 1) | 9 | 0.627 (1) | 1.03 |
| 2048 | ikj | **13.058** (8.948–13.619, 5) | 10 11 10 6 11 | 6.778 (5) | 1.93 |
| 2048 | jki | **0.322** (0.322–0.322, 1) | 8 | 0.323 (1) | 1.00 |
| 2048 | tiled T=64 | **8.605** (8.285–8.960, 5) | 11 9 7 11 9 | 5.173 (5) | 1.66 |

- **순서 효과 (스칼라끼리, `-fno-vectorize` 열)**: n = 2048 에서 ikj 6.78 / ijk 0.627 = **10.8배**, jki 대비 21배. 이것이 캐시 몫이다.
- **SIMD 효과**: ikj 만 1.9–2.3배 (NEON `fmla.2d`, double 2개). ijk·jki 는 0.93–1.03 — 실제로 벡터화되지 않았다는 asm 결론과 일치.
- 그래서 기본 빌드의 ikj / ijk = 20.3배 (n = 2048) 를 "캐시 덕에 20배" 라고 말하면 틀리다 — 약 11배 × 약 2배.
- ijk 는 n 이 커질수록 나빠진다 (1.64 → 1.21 → 0.64). n = 2048 은 열 걷기가 L1 의 set 하나에 몰리고 (Part B 그림), B 열 하나가 2048 페이지에 걸쳐 TLB 도 매 접근 새 페이지다 (TLB 영향은 측정하지 않음 — 추정).
- **T = 64 타일이 ikj 보다 느리다** (8.6 vs 13.1). 아래 타일 스윕과 Q8 에서 이유를 본다.

#### 측정: 타일 크기 스윕, ld 패딩, Accelerate 천장

`bench tile 1024`, `bench tile 2048` (기본 빌드, 같은 조건):

| T | 8 | 16 | 32 | 48 | 64 | 96 | 128 | 256 | 512 |
|---|---|---|---|---|---|---|---|---|---|
| n = 1024 GFLOP/s | 2.73 | 4.08 | 6.99 | 7.80 | 8.64 | **10.66** | 9.08 | 10.58 | 10.64 |
| n = 2048 GFLOP/s | 2.07 (1회) | 3.56 (1회) | 6.88 | 8.06 | 8.48 | 8.57 | 9.27 | 12.05 | **13.42** |

"L1 에 3 타일" 어림 (Super 128 KB → 3·T²·8 ≤ 96 KB → T ≈ 64) 보다 **큰 타일이 이긴다**. 이 구현의 타일 안쪽 루프 길이가 T 라서 T 가 작으면 `fmla.2d` 루프가 몇 바퀴 못 돌고 루프 진입·주소 계산 비용이 커진다 (T = 8 이면 안쪽 루프 한 번에 double 8개). 그리고 B 가 L2 (20 MB) 에 들어가는 n = 1024 에서는 L1 미스를 줄여 봐야 얻을 게 적다. n = 2048 (B 32 MB > L2) 에서 T = 512 가 ikj 와 비슷해진 것은 "L2 를 위한 타일" 효과로 읽을 수 있다 (추정).

`bench pad 512`, `bench pad 1024` — ld 를 한 라인(16 double) 늘렸을 때:

| n | ld | ijk | jki | tiled T=64 |
|---|---|---|---|---|
| 512 | 512 | 1.41 | 0.41 | 11.59 |
| 512 | **528** (행 = 33 라인) | **2.93** (2.1배) | **4.64** (11배) | **20.08** (1.7배) |
| 1024 | 1024 | 1.30 | 0.38 (1회) | 8.86 |
| 1024 | **1040** (행 = 65 라인) | **2.56** (2.0배) | **1.65** (4.4배) | **10.12** (1.1배) |

패딩 한 줄로 jki 가 n = 512 에서 11배 빨라졌다 — jki 의 느림 대부분이 "열 걷기" 자체보다 **2의 거듭제곱 간격 충돌** 이었다는 뜻이다. n = 512 패딩 타일(20.1)은 ikj(14.7)도 넘는다. 순서·타일 실험은 2의 거듭제곱 크기 하나로만 하면 두 효과가 섞인다.

천장 참고 — Accelerate `cblas_dgemm` (`cc -std=c11 -O2 -DUSE_ACCELERATE -DACCELERATE_NEW_LAPACK demo_bench.c -framework Accelerate -o bench_blas; bench_blas blas`):

| n | 512 | 1024 | 2048 |
|---|---|---|---|
| dgemm GFLOP/s (중앙값, min–max) | 458 (127–463) | 489 (483–523) | 427 (196–490) |

손으로 짠 최고(n = 512 패딩 타일 20.1)의 20배 이상. 레지스터 블로킹·패킹·멀티코어 여부·AMX/SME 같은 행렬 유닛 사용 여부를 Apple 이 밝히지 않으므로 **"이 맥에서 도달 가능한 천장" 으로만** 쓴다 (스레드 수도 확인하지 않았다). 이 입력에서 dgemm 의 maxdiff 도 0 이었다 — 같은 k 순서로 누적하는 것으로 보이지만 확인하지 않았다.

### Part B — 시뮬레이터가 말해 주는 것

`trace_matmul` 은 "컴파일러 모델" 이다: 스칼라는 레지스터, 배열 접근만 메모리. 이 모델은 벡터화(16 B 한 번 = 원소 2개)를 무시하지만, 같은 라인을 두 번 읽는 것은 hit 이 되므로 **미스 수는 거의 같다** — 미스 수는 접근 순서가 정하고, 접근 폭은 hit 수만 바꾼다.

M6 L1D 파라미터: `hw.perflevel0.l1dcachesize: 131072` (Super), `hw.perflevel1.l1dcachesize: 65536` (Performance), `hw.perflevel2.l1dcachesize: 98304` (Efficiency), `hw.cachelinesize: 128`. **연관도는 Apple 이 공개하지 않는다** → 8-way 로 가정 (128 KB / 128 B / 8 = 128 set). 교체 정책도 진짜 LRU 라는 보장이 없다. 그래서 절대 미스 수가 아니라 **순서와 배수** 만 믿는다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sim.c -o /tmp/esn_ARCH-M07/sim && /tmp/esn_ARCH-M07/sim   # 약 15초 (표 3 의 완전 연관 시뮬이 대부분)
```

`demo_sim.c` 표 1 · 2 (Super 128 KB / Performance 64 KB, 128 B, 8-way 가정, double, 실제 출력):

```text
== table 1: M6 Super L1D 128 KB / 128 B / 8-way (assoc assumed), double ==
n      ld    variant       accesses     misses    rate%   miss/n^3
128    128   ijk            4210688     173064    4.110     0.0825
128    128   ikj            6324224      19328    0.306     0.0092
128    128   jki            6324224     671616   10.620     0.3203
128    128   tiled16        6438912      11768    0.183     0.0056
128    128   tiled32        6373376       8188    0.128     0.0039
128    128   tiled64        6340608       7160    0.113     0.0034
128    144   ijk            4210688      18433    0.438     0.0088
128    144   ikj            6324224      19300    0.305     0.0092
128    144   jki            6324224     148480    2.348     0.0708
128    144   tiled16        6438912      11214    0.174     0.0053
128    144   tiled32        6373376       7182    0.113     0.0034
128    144   tiled64        6340608       6365    0.100     0.0030
256    256   ijk           33619968   16914464   50.311     1.0082
256    256   ikj           50462720    1056768    2.094     0.0630
256    256   jki           50462720   33685504   66.753     2.0078
256    256   tiled16       51445760      84976    0.165     0.0051
256    256   tiled32       50921472      80112    0.157     0.0048
256    256   tiled64       50659328     303472    0.599     0.0181
256    272   ijk           33619968    1056768    3.143     0.0630
256    272   ikj           50462720    1056768    2.094     0.0630
256    272   jki           50462720    1118208    2.216     0.0667
256    272   tiled16       51445760      77824    0.151     0.0046
256    272   tiled32       50921472      66560    0.131     0.0040
256    272   tiled64       50659328      47438    0.094     0.0028
== table 2: M6 Performance L1D 64 KB / 128 B / 8-way (assoc assumed), double ==
n      ld    variant       accesses     misses    rate%   miss/n^3
256    256   ijk           33619968   16915456   50.314     1.0082
256    256   ikj           50462720    1056768    2.094     0.0630
256    256   jki           50462720   33685504   66.753     2.0078
256    256   tiled16       51445760     143344    0.279     0.0085
256    256   tiled32       50921472     582080    1.143     0.0347
256    256   tiled64       50659328    1085440    2.143     0.0647
256    272   ijk           33619968    1056768    3.143     0.0630
256    272   ikj           50462720    1056768    2.094     0.0630
256    272   jki           50462720    1794512    3.556     0.1070
256    272   tiled16       51445760     115720    0.225     0.0069
256    272   tiled32       50921472      73728    0.145     0.0044
256    272   tiled64       50659328      53248    0.105     0.0032
```

읽는 법:

- **n = 256, ld = 256**: ijk 1.008 미스/반복, jki 2.008, ikj 0.063 — 위 표의 1.06 / 2 / 0.125 와 거의 같다. ikj 가 0.125 가 아니라 0.063 인 것은 C 의 i 행(2 KB)이 k 루프 내내 L1 에 남아 B 행만 흘러 들어오기 때문이다.
- **같은 n = 256 에 ld = 272 (패딩 16 원소 = 1 라인)**: ijk 가 1.008 → **0.063** (16배). 이유는 아래 그림. 열 256 라인이 L1 1024 라인 안에 퍼져 들어가 다음 j (같은 라인의 이웃 열) 때까지 살아남는다. 반면 n = 2048 이면 열 하나가 2048 라인 = 256 KB > L1 이라 패딩해도 못 버틴다 — 그때는 순서를 바꾸거나 타일링해야 한다.
- **타일 크기**: ld = 256 에서 T = 64 (0.0181) 가 T = 32 (0.0048) 보다 나쁘다 — 64×64 double 타일의 64 행이 행 간격 16 라인 때문에 8 set 에 몰려 서로 쫓아낸다 (타일 자기 충돌, self-interference). 패딩하면 T = 64 가 가장 좋다 (0.0028). Performance 코어 64 KB 가정(표 2)에서는 ld = 256 일 때 T = 16 이 가장 좋다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">B 의 한 열을 걸을 때 쓰이는 set (M6 L1D 가정: 128 set × 8 way × 128 B)</text>
  <text x="10" y="44" fill="currentColor" font-size="12">ld = 256 (행 간격 2048 B = 16 라인)</text>
  <rect x="40" y="52" width="640" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="40" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="120" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="200" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="280" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="360" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="440" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="520" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="600" y="52" width="5" height="22" class="f-warn" stroke="currentColor"/>
  <text x="40" y="90" fill="currentColor" font-size="12">set 0</text><text x="660" y="90" fill="currentColor" font-size="12">127</text>
  <text x="120" y="90" fill="currentColor" font-size="12">set 0, 16, 32 … 112 의 8개만 → 64 라인 자리에 열 256 라인</text>
  <text x="10" y="122" fill="currentColor" font-size="12">ld = 272 (행 간격 2176 B = 17 라인)</text>
  <rect x="40" y="130" width="640" height="22" class="f-ok" stroke="currentColor"/>
  <text x="40" y="168" fill="currentColor" font-size="12">set 0</text><text x="660" y="168" fill="currentColor" font-size="12">127</text>
  <text x="120" y="168" fill="currentColor" font-size="12">0, 17, 34, … mod 128 → 128 set 전부 → 열 256 라인이 1024 라인 안에</text>
  <rect x="10" y="186" width="720" height="56" rx="6" fill="none" stroke="currentColor"/>
  <text x="22" y="207" fill="currentColor">행 k 의 set = (시작 라인 + k·m) mod 128, m = 행 간격(라인 수). 쓰이는 set 수 = 128 / gcd(m, 128).</text>
  <text x="22" y="228" fill="currentColor">n=256 → m=16 → 8 set · n=512 → 4 · n=1024 → 2 · n=2048 → m=128 → 1 set (8 라인!). m 홀수 → 전부.</text>
</svg>
```

시뮬레이터가 **설명하는 것**: 순서별 L1 미스 순서 (jki ≫ ijk ≫ ikj > tiled), 2의 거듭제곱 ld 의 충돌, 타일 크기의 최적점이 ld 와 캐시 크기에 따라 움직이는 것. **설명하지 못하는 것**: (1) **ikj 가 T = 64 타일보다 빠른 것** — 시뮬에서는 타일이 미스가 더 적지만 (n=128: 8 188 vs 19 328), 실측은 반대였다. 미스가 아니라 안쪽 루프 길이·명령 수·load/store 포트가 한계라서다. (2) **하드웨어 프리페처** — ikj 의 B 행 스트리밍은 미스여도 순차라 미리 가져와진다. (3) **L2 와 그 아래** — n = 1024 의 B (8 MB) 는 L2 (20 MB) 안이라 L1 미스 비용이 작다. (4) **TLB** — 열 걷기의 페이지 넘김. 그래서 시뮬은 "ijk/jki 가 왜 느린가" 와 "2의 거듭제곱이 왜 나쁜가" 는 잘 설명하고, "잘 된 두 구현 중 누가 이기나" 는 측정으로 정한다.

### Part C — Cortex-M7 류 16 KB D-cache: 패딩과 타일

시나리오: Cortex-M7 (예: STM32H7 급, D-cache 16 KB · 4-way · 32 B 라인 — 크기는 칩마다 다르니 데이터시트 확인) 에서 128×128 float 완전 연결 층(FC layer)을 GEMM 으로 돌린다. 행렬 하나 64 KB, 셋이면 192 KB → 캐시의 12배. `demo_sim.c` 표 3 (3C 분해, 실제 출력):

```text
== table 3: Cortex-M7-like D-cache 16 KB / 32 B / 4-way, float, n=128 — 3C split ==
ld    variant     accesses     misses    rate%   compuls.  capacity  conflict
128   ijk          4210688    2133136   50.660       6144    260096   1866896
128   ikj          6324224     266240    4.210       6144    260096         0
128   jki          6324224    4227072   66.839       6144    274432   3946496
128   tiled8       6569984      42480    0.647       6144     32768      3568
128   tiled16      6438912      39664    0.616       6144     30720      2800
128   tiled32      6373376      86320    1.354       6144     14336     65840
128   tiled64      6340608     272384    4.296       6144    266240         0
136   ijk          4210688     266240    6.323       6144    260096         0
136   ikj          6324224     266240    4.210       6144    260096         0
136   jki          6324224     280576    4.437       6144    274432         0
136   tiled8       6569984      38977    0.593       6144     32768        65
136   tiled16      6438912      31042    0.482       6144     30720     -5822
136   tiled32      6373376      23957    0.376       6144     14336      3477
136   tiled64      6340608      86422    1.363       6144    266240   -185962
```

- **ld = 128 (행 512 B = 16 라인, 짝수)**: ijk 2 133 136 미스 중 **1 866 896 이 conflict**. 열이 128 set 중 8 set (= 32 라인) 에만 들어간다. jki 는 더 나쁘다.
- **ld = 136 (행 544 B = 17 라인, 홀수)**: ijk·ikj·jki 의 conflict 가 0 이 되고 셋 다 **capacity 미스만** 남는다 (266 240, 266 240, 280 576). 이 크기에선 순서보다 용량이 문제 → 타일링이 답.
- **타일**: 3·T²·4 B ≤ 16 KB → T ≤ 36 → 후보 중 **T = 32** (타일 셋 = 12 KB). 패딩하면 실제로 T = 32 가 가장 적다 (23 957, ikj 의 1/11). 패딩 없이는 T = 32 타일의 행이 8 set 에 몰려 86 320 으로 나빠지고 **T = 16 이 이긴다** — `best_tile` 이 ld 에 따라 다른 답을 내는 이유.
- **conflict 가 음수** (tiled16 ld=136 −5 822, tiled64 ld=136 −185 962): 같은 용량의 완전 연관 LRU 가 4-way LRU 보다 **더 많이** 놓쳤다는 뜻이다. T = 64 는 타일 셋이 48 KB 라 캐시(16 KB)를 넘고, 완전 연관 LRU 는 ikj 와 똑같이 266 240 을 놓친다. 캐시보다 조금 큰 데이터를 순환 접근하면 LRU 는 "곧 다시 쓸 라인을 정확히 먼저 버리는" 최악의 선택을 하는데, set 으로 나뉘면 일부 set 은 그 순환에서 벗어난다. Hill 의 3C 정의는 LRU 가 아닌 정책뿐 아니라 이런 순환 패턴에서도 음수가 나올 수 있다 (ARCH-M05 의 같은 관찰).

펌웨어 실무로 옮기면:

1. **행 간격을 2의 거듭제곱으로 두지 않는다.** 이미지 줄(stride), 행렬 ld, DMA 프레임 버퍼 줄 간격을 "한 라인 더" 패딩. 행 시작은 라인 정렬로 유지 (DMA·SIMD 정렬, ARCH-M12 의 clean/invalidate 단위와도 맞는다).
2. **타일은 "작업 집합 ≤ 캐시의 절반~3/4" 로 어림하고 측정/시뮬레이션으로 확정.** 연관도가 낮을수록 (4-way) 여유를 더 둔다.
3. **패킹(packing)**: 타일을 연속 버퍼에 복사하면 ld 와 무관하게 충돌이 사라진다 (연습 3: 84 604 → 20 709, 패딩 20 651 과 같다). M7 이라면 그 버퍼를 **DTCM** 에 두면 캐시 자체를 우회한다 — TCM 은 캐시가 아니라 1-사이클 SRAM 이라 충돌도 미스도 없다 (DTCM 크기·대기 상태는 칩마다 다르다).
4. 캐시 없는 Cortex-M4/M0 에서는 루프 순서의 캐시 효과는 없다. 남는 것은 주소 계산 비용과 플래시 대기 상태(ARCH-B12) 정도.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-M07/so && /tmp/esn_ARCH-M07/so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-M07/san && /tmp/esn_ARCH-M07/san
```

```text
-- Part A: loop orders and tiling (exact vs ijk) --
PASS  C2[0] == 19 && C2[1] == 22 && C2[2] == 43 && C2[3] == 50
...
PASS  variants_match(100, 104)
-- Part B: compact LRU cache sim + matmul address traces --
      LRU 2-way pattern: MMHMMHMH
PASS  strcmp(pat, "MMHMMHMH") == 0
...
      M6-L1 n=128 ijk   accesses   4210688 misses   173064  miss rate  4.110%  misses/n^3 0.0825
      M6-L1 n=128 ikj   accesses   6324224 misses    19328  miss rate  0.306%  misses/n^3 0.0092
      M6-L1 n=128 jki   accesses   6324224 misses   671616  miss rate 10.620%  misses/n^3 0.3203
      M6-L1 n=128 tiled accesses   6373376 misses     8188  miss rate  0.128%  misses/n^3 0.0039
PASS  r[V_JKI].misses > r[V_IJK].misses
PASS  r[V_IJK].misses > r[V_IKJ].misses
PASS  r[V_IKJ].misses > r[V_TILED].misses
-- Part C: firmware D-cache (16 KB, 4-way, 32 B, float): ld padding and tile choice --
...
PASS  ldp == 136
      M7 ijk n=128: ld=128 misses 2133136, ld=136 misses 266240 (ikj ld=136: 266240)
PASS  b.misses * 4 < a.misses
PASS  b.misses == ikj
      M7 tiled misses ld=136: T8=38977 T16=31042 T32=23957 T64=86422  → best T=32
      M7 tiled misses ld=128: T8=42480 T16=39664 T32=86320 T64=272384  → best T=16
PASS  bt == 32
...
43/43 passed
```

약 0.4초. ASan+UBSan 빌드도 경고 없이 `43/43 passed`. 시뮬레이터 결과는 결정적이라 매 실행 같은 숫자다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ikj 에서 C 를 지우지 않음 | 호출자가 지웠다고 가정 → 두 번째 호출에서 누적 (테스트 `C3 = 99`) | i 마다 `C[i][·] = 0` 후 k 루프 |
| 타일 블록 안에서 C 를 지움 | kk 마다 지워져 마지막 kk 블록 몫만 남는다 | 블록 루프 전에 C 전체를 한 번 |
| `for (j = jj; j < jj + T; j++)` | T 가 n 의 약수가 아니면 범위 밖 (T = 50, n = 64) | `je = imin(jj + T, n)` |
| `-Rpass` remark 만 보고 "ijk 도 벡터화됨" | ld == 1 일 때만 도는 버전. 실제로는 스칼라 `fmadd` | `-S` 로 실제 도는 루프 확인 |
| 기본 빌드 ikj vs ijk 차이를 전부 캐시 덕으로 | ikj 만 NEON `fmla.2d` (2배 폭 × 인터리브) | `-fno-vectorize -fno-slp-vectorize` 빌드와 나란히 |
| "타일링 = 항상 빠름" | 이 맥 n = 512/1024 에서 T = 64 는 ikj 보다 느렸다 — 안쪽 루프가 64 원소라 짧고, L2 가 커서 ikj 의 스트리밍이 이미 빠르다 | 타일 스윕 측정, T 는 L1 이 아니라 **병목 계층** 기준 |
| n = 1024, 2048 로만 측정 | 2의 거듭제곱 ld 는 열 걷기와 타일을 몇 개 set 에 몰아넣는다 — 순서 효과와 충돌 효과가 섞인다 | ld = n + 16 (1 라인) 패딩 버전도 잰다 |
| 패딩은 공짜 | 크기가 늘어 캐시를 넘을 수 있다 (예: 64×64 float 16 KB → 72 열 18 KB 가 16 KB 캐시를 넘어 ikj 미스가 늘었다 — 시뮬 5 568 → 13 066) | 패딩 후에도 시뮬/측정으로 확인 |
| 시뮬레이터 연관도를 사실처럼 말함 | Apple 은 L1 연관도·교체 정책을 공개하지 않는다 | "8-way 가정" 이라고 말하고 순서만 주장 |
| 3·T²·e ≤ C 를 꽉 채워 T 를 고름 | 4-way 처럼 연관도가 낮으면 타일끼리·자기 충돌 | 캐시의 1/2~3/4, 그리고 패딩/패킹 |

코드로 보면 가장 흔한 두 개:

```c
/* 틀림: kk 블록마다 C 타일을 지움 (앞 kk 몫이 사라짐) + T 가 n 의 약수가 아니면 범위 밖 */
for (int ii = 0; ii < n; ii += T) for (int jj = 0; jj < n; jj += T) for (int kk = 0; kk < n; kk += T) {
    for (int i = ii; i < ii + T; i++) for (int j = jj; j < jj + T; j++) C[i*ld+j] = 0.0;
    for (int i = ii; i < ii + T; i++) for (int k = kk; k < kk + T; k++) /* ... C[i][j] += ... */;
}

/* 고침 */
for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) C[i*ld+j] = 0.0;
for (int ii = 0; ii < n; ii += T) for (int jj = 0; jj < n; jj += T) for (int kk = 0; kk < n; kk += T) {
    int ie = imin(ii+T, n), je = imin(jj+T, n), ke = imin(kk+T, n);
    for (int i = ii; i < ie; i++) for (int k = kk; k < ke; k++) {
        double r = A[i*ld+k];
        for (int j = jj; j < je; j++) C[i*ld+j] += r * B[k*ld+j];
    }
}
```

## 8. 면접 Q&A

<details><summary>Q1. 행렬 곱에서 루프 순서만 바꿨는데 왜 20배 넘게 빨라지나요?</summary>

FLOP 수는 같고 **메모리 접근 순서** 가 다르다. 행 우선 배열에서 안쪽 루프가 열을 걸으면 (ijk 의 B, jki 의 A·C) 접근마다 새 라인을 가져오고, 행을 걸으면 라인 하나(128 B = double 16개)로 16번 쓴다. 안쪽 반복당 미스가 ijk 1.06, ikj 0.125, jki 2 — 이 맥 n = 2048 실측도 ikj 13.1 / ijk 0.64 / jki 0.32 GFLOP/s 로 같은 순서다. 단 기본 빌드에서는 ikj 만 NEON 으로 벡터화되므로 그 몫(벡터화 끈 빌드와의 차이)을 빼고 말해야 정확하다.

꼬리: "컴파일러가 알아서 바꿔 주지 않나?" → LLVM 에 loop-interchange 패스가 있지만 이 clang -O2 에서는 돌지 않았다 (`-Rpass=loop-interchange` remark 없음). 별칭·부동소수 순서 문제로 일반적으로 보수적이다. 소스에서 순서를 고르는 것이 엔지니어의 일.
</details>

<details><summary>Q2. 안쪽 반복당 미스를 어떻게 어림하나요?</summary>

안쪽 루프 변수에 대해 각 배열 접근의 **간격(stride)** 을 본다. 간격 0 (안쪽에서 안 바뀜) → 레지스터, 0 미스. 간격 e (연속) → e/L 미스. 간격 ld·e ≥ L (열) → 1 미스. 그리고 "다음 바깥 반복까지 그 라인이 살아남는가" 를 따진다 — 살아남으면 더 줄어든다. 예: n = 256 시뮬에서 ikj 는 C 행 2 KB 가 L1 에 남아 0.125 가 아니라 0.063. 이 방법은 연습 1 에서 작은 캐시로 6 순서 모두 ±1% 로 맞았다.

꼬리: "쓰기는?" → write-allocate 캐시면 쓰기 미스도 라인을 가져온다 (RFO). C 행 쓰기도 e/L 로 센다. 쓰기 정책별 DRAM 바이트는 ARCH-M06, RFO 대역폭은 MEM-M06.
</details>

<details><summary>Q3. 타일 크기는 어떻게 고르나요?</summary>

1차 어림: 한 블록 계산의 작업 집합(A·B·C 타일 3개 = 3·T²·e)이 목표 캐시의 1/2~3/4 에 들어가게. 16 KB D-cache float 이면 T ≤ 36 → 32. 그다음 **반드시 측정/시뮬레이션**: (1) 연관도가 낮고 ld 가 2의 거듭제곱이면 타일 행이 몇 set 에 몰린다 (시뮬: ld = 128 에서 T = 32 가 T = 16 보다 나빴다), (2) 안쪽 루프가 너무 짧으면 오버헤드가 커진다, (3) 진짜 병목 계층이 L1 이 아닐 수 있다 — 이 맥은 L2 가 커서 n ≤ 1024 에서는 B 가 L2 에 들어간다.

꼬리: "실제 BLAS 는?" → 계층마다 블로킹 (레지스터 마이크로 커널 → L1 → L2 → L3/메모리), 그리고 타일을 연속 버퍼에 **패킹** 해서 ld 와 무관하게 충돌을 없앤다 (Goto & van de Geijn 2008). 이 맥 Accelerate `cblas_dgemm` 은 n = 2048 에서 427 (중앙값) GFLOP/s — 손 타일링의 수십 배. AMX/SME 같은 행렬 유닛도 쓰는 것으로 알려져 있지만 Apple 이 세부를 밝히지 않는다.
</details>

<details><summary>Q4. n = 1024, 2048 처럼 2의 거듭제곱 크기가 왜 특히 나쁜가요?</summary>

행 간격 m 라인으로 열을 걸으면 쓰이는 set 수가 `S / gcd(m, S)` 다. M6 L1 (128 set 가정) 에서 n = 2048 double 은 행 간격 16 KB = 128 라인 → **set 1개**, 즉 열 전체가 8 way 에 산다. L2 · TLB 도 비슷하게 2의 거듭제곱 간격에 약하다 (페이지 16 KB 이면 n = 2048 열 걷기는 접근마다 새 페이지). 고침: ld 를 한 라인 패딩 (n + 16 double) 하거나, 타일을 패킹한다. 이 맥 측정: n = 512 에서 ld 512 → 528 로 바꾸자 jki 0.41 → 4.64 GFLOP/s (11배), ijk 1.41 → 2.93, T=64 타일 11.6 → 20.1.

꼬리: "패딩의 비용은?" → 메모리 1–2%, 그리고 행렬이 캐시 경계를 넘을 수 있다 (64×64 float 16 KB 가 18 KB 가 되어 16 KB 캐시에서 ikj 미스가 늘었다 — 시뮬). 크기를 바꾸면 다시 잰다.
</details>

<details><summary>Q5. 벤치마크에서 SIMD 효과와 캐시 효과를 어떻게 분리하나요?</summary>

세 단계. (1) `-Rpass=loop-vectorize` 로 어느 루프가 벡터화됐나 본다. (2) remark 를 믿지 말고 `-S` 로 실제 도는 루프를 본다 — 이 레슨에서 ijk 와 jki 는 remark 상 벡터화됐지만 `ld == 1` 일 때만 들어가는 버전이었고, 실제로는 스칼라 `fmadd` 였다. (3) `-fno-vectorize -fno-slp-vectorize` 빌드로 같은 표를 다시 잰다. 이 맥에서: n = 2048 ikj 기본 13.06 vs novec 6.78 (SIMD 1.9배), novec 안에서 ikj/ijk = 10.8배 (캐시 몫). ijk·jki 는 두 빌드가 같다 (0.93–1.03배).

꼬리: "ijk 를 벡터화하려면?" → 안쪽 k 가 부동소수 합(reduction)이라 순서를 바꿔야 벡터화되는데, IEEE 순서를 지키는 한 컴파일러는 못 바꾼다 (`-ffast-math` 의 reassociation 이 필요). 그보다 루프를 ikj 로 바꾸는 게 정답.
</details>

<details><summary>Q6. 캐시 시뮬레이터로 실측을 "설명" 한다는 게 어디까지 유효한가요?</summary>

유효한 것: 순서별 L1 미스의 **순서와 배수**, 2의 거듭제곱 ld 의 충돌, 타일 크기 최적점이 ld·용량에 따라 움직이는 것. 한계: (1) Apple 은 L1 연관도·교체 정책을 공개하지 않아 8-way LRU 는 가정, (2) L2·SLC·DRAM 계층과 하드웨어 프리페처가 없다 — ikj 의 B 스트리밍은 미스여도 프리페처가 숨긴다, (3) 명령 수·루프 오버헤드·load/store 포트 수가 없다 — 이 맥에서 ikj 가 타일링(T=64)보다 빨랐던 이유는 미스가 아니라 이쪽이다. 그래서 결론은 "시뮬로 가설을 세우고, 측정으로 확인하고, 안 맞으면 모델에 없는 요소를 찾는다".

꼬리: "그럼 L2 까지 넣으면?" → 다단 캐시 모델은 MEM-M03, 계층별 대역폭 천장은 MEM-A08 (roofline).
</details>

<details><summary>Q7. 캐시가 16 KB 인 Cortex-M7 에서 128×128 float GEMM 을 짜야 한다면?</summary>

(1) ld 를 136 (행 = 17 라인) 으로 패딩하거나 행렬을 패킹. (2) T = 32 타일 (3 타일 = 12 KB). 시뮬에서 ijk(ld=128) 2 133 136 미스 → 타일+패딩 23 957 (89배 적다). (3) 가능하면 자주 쓰는 타일(가중치 B 타일)을 **DTCM** 에 복사 — TCM 은 캐시가 아니라 대기 없는 SRAM 이라 미스·충돌이 없고, DMA 로 다음 타일을 미리 가져오면 계산과 겹친다 (이중 버퍼링). (4) DMA 가 쓰는 버퍼를 캐시 가능한 SRAM 에 두면 clean/invalidate 가 필요 (ARCH-M12).

꼬리: "M4 (캐시 없음) 라면?" → 캐시 효과는 없다. 대신 플래시 대기 상태와 ART 가속기(ARCH-B12), SIMD(`SMLAD` 같은 DSP 명령)가 주제가 된다. CMSIS-NN/CMSIS-DSP 가 하는 일.
</details>

<details><summary>Q8. "타일링하면 무조건 빨라진다" 에 동의하나요?</summary>

아니다 — 이 맥에서 직접 반례를 봤다. n = 512 / 1024 / 2048 모두 ikj (13–15 GFLOP/s) 가 T = 64 타일(8.6–10.8)보다 빨랐다. 이유 (추정 포함): ikj 의 B 행 스트리밍은 순차라 하드웨어 프리페처가 잘 숨기고, n ≤ 1024 이면 B (8 MB) 가 L2 (`hw.perflevel0.l2cachesize` 20 MB) 안이다. 반면 T = 64 타일은 안쪽 루프가 64 원소라 짧고, 두 구현 모두 FMA 하나마다 **load 2 + store 1** (C 를 매번 읽고 쓴다) 이라 L1 포트가 병목이다 — 타일링은 미스만 줄이고 이 병목은 그대로다. 그다음 단계는 **레지스터 블로킹** (C 의 4×8 조각을 레지스터에 두고 k 루프 동안 load/store 를 없앰) 이고, 그게 BLAS 마이크로 커널이다. 측정으로도 확인된다: 타일을 키우면 안쪽 루프가 길어져 n = 2048 T = 512 가 13.4 로 ikj 수준이 되고, ld 를 패딩한 n = 512 의 T = 64 는 20.1 로 ikj (14.7) 를 넘었다 — 충돌을 없애야 타일이 제값을 한다.

꼬리: "그럼 이 레슨의 타일링은 언제 이기나?" → 캐시가 작고 L2 가 없거나 느린 곳 (Cortex-M7 시뮬: ikj 266 240 vs 타일 23 957 미스), 그리고 행렬이 마지막 캐시 계층을 넘는 큰 n.
</details>

## 9. 추가 연습

**연습 1 — 여섯 순서 전부.** e = 8 B, L = 32 B, 1 KB 4-way 캐시, n = 128, ld = 132 일 때 ijk, jik, ikj, kij, jki, kji 의 안쪽 반복당 미스를 예측하고 시뮬레이터로 확인하라. M6 (L = 128) 에서는?

<details><summary>답</summary>

안쪽 k (ijk, jik): A 행 e/L + B 열 1 = **1.25**. 안쪽 j (ikj, kij): B 행 + C 행 = 2e/L = **0.5**. 안쪽 i (jki, kji): A 열 + C 열 = **2**. 시뮬 1.258 / 0.508 / 2.008 (±1%). 캐시를 1 KB 로 고른 이유: 행 하나(1 KB)가 캐시 전체라서 "다음 바깥 반복까지 아무 행도 못 버틴다" 는 분석의 가정이 성립한다 — 2 KB 로 키우면 C 행이 일부 살아남아 ikj 가 0.288 로 떨어진다. ld = 132 (33 라인, 홀수) 는 충돌을 빼고 용량 효과만 보려고. M6: 1.0625 / 0.125 / 2. `predicted_miss_per_iter`, `sim_order` 로 확인.
</details>

**연습 2 — 캐시 친화 전치.** `dst[j][i] = src[i][j]` (float 128×128) 를 그대로 하면 16 KB 4-way 32 B 캐시에서 미스가 몇 개? 8×8 블록 전치로 바꾸면?

<details><summary>답</summary>

naive 는 src 를 행으로, dst 를 열로 걷는다 — dst 열 128 라인이 j 사이에 못 버텨 대부분 미스: **18 432**. 8×8 블록이면 src 8 라인·dst 8 라인 (각 32 B = float 8개) 만 블록 동안 쓰고 다 쓰고 나서 버린다 → **4 096 = compulsory** (두 행렬 128 KB / 32 B). 블록 한 변 = 라인당 원소 수(8) 가 자연스러운 선택. 면접 단골인 "캐시 친화 전치" 의 답이고, 이미지 회전·FFT 전치·NAND 페이지 재배열에도 같은 패턴. `transpose_blocked`, `sim_transpose` 로 확인.
</details>

**연습 3 — 패킹.** ld = 128 (패딩 없음) 그대로, (jj, kk) 마다 B 타일 32×32 를 연속 버퍼 P 에 복사한 뒤 P 로 계산하면 미스가 어떻게 되나? 복사 비용도 트레이스에 넣는다. 루프 순서는 jj → kk → [복사] → ii → i → k → j (본문의 ii → jj → kk 와 다르다 — 복사한 타일을 모든 ii 가 재사용하게).

<details><summary>답</summary>

패킹 없음 84 604 → 패킹 **20 709**, 같은 순서에 ld = 136 패딩 20 651 과 거의 같다. 복사본은 32×32×4 = 4 KB 연속이라 128 set 에 고르게 퍼진다. 복사 비용(B 읽기 + P 쓰기 n² 번씩)은 그 타일을 n/T = 4 번 재사용해서 갚는다. 장점: 사용자가 준 행렬의 ld 를 바꿀 수 없을 때(라이브러리 API) 도 된다. GotoBLAS/BLIS/OpenBLAS 가 이렇게 한다. `mm_tiled_packed` 는 결과가 기준과 같은지(T = 32, 48) 까지 확인.
</details>

**연습 4 — 어림셈.** n = 2048 double 에서 (a) 1 초 걸리면 몇 GFLOP/s? (b) L1 로 들어오는 바이트를 ijk (안쪽 반복마다 B 라인 하나), ikj (i 마다 B 전체), 타일 T = 64 (블록마다 A·B 타일 + C 한 번) 로 어림하면? flop/byte 는?

<details><summary>답</summary>

(a) 2·2048³ = 17.18 G → **17.18 GFLOP/s**. (b) ijk = n³·L = **1 099.5 GB** (0.016 flop/B), ikj = n³·e = **68.7 GB** (0.25 flop/B), 타일 = 2n³e/T + n²e = **2.18 GB** (7.88 flop/B) — ikj 대비 약 T/2 = 32배 적다. 실측과 맞춰 보면: ijk 26.7 초 → L1 채움 약 41 GB/s, ikj 1.3 초 → 52 GB/s. 둘 다 이 칩 메모리 대역폭 안이라 "ikj 는 대역폭이 아니라 load/store 포트·명령 수가 한계" 라는 Q8 의 결론과 맞는다. flop/byte 와 대역폭 천장으로 상한을 긋는 방법이 roofline (MEM-A08). `gflops`, `bytes_*` 로 확인.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-M07/pr && /tmp/esn_ARCH-M07/pr
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/esn_ARCH-M07/sanp && /tmp/esn_ARCH-M07/sanp
```

```text
-- 연습 1: six loop orders, predicted vs simulated misses per inner iteration --
      ijk  predicted 1.250  simulated 1.258
      jik  predicted 1.250  simulated 1.258
      ikj  predicted 0.500  simulated 0.508
      kij  predicted 0.500  simulated 0.508
      jki  predicted 2.000  simulated 2.008
      kji  predicted 2.000  simulated 2.008
PASS  ok
PASS  predicted_miss_per_iter(O_IJK, 8, 128) == 1.0625
PASS  predicted_miss_per_iter(O_KIJ, 8, 128) == 0.125
-- 연습 2: blocked transpose (float, n=128, 16 KB 4-way 32 B) --
PASS  memcmp(d1, d2, sizeof(float) * n * n) == 0
PASS  d1[5 * n + 3] == s[3 * n + 5]
PASS  memcmp(d1, d2, sizeof(float) * n * n) == 0
      naive 18432 misses, blocked T=8 4096 (compulsory 4096)
PASS  mb * 2 < mn
PASS  mb == (uint64_t)(2 * n * n * 4 / 32)
-- 연습 3: packing the B tile (M7-like 16 KB 4-way 32 B, float, n=128, ld=128 unpadded) --
      T=32: ld=128 plain 84604, ld=128 packed 20709, ld=136 padded 20651
PASS  packed * 2 < plain
PASS  packed < padded + padded / 4
PASS  memcmp(C1, C2, sizeof(float) * n * n) == 0
PASS  memcmp(C1, C2, sizeof(float) * n * n) == 0
-- 연습 4: GFLOP/s and traffic estimates (n=2048, double, L=128) --
PASS  gflops(2048, 1.0) > 17.17 && gflops(2048, 1.0) < 17.18
      ijk 1099.5 GB, ikj 68.7 GB, tiled T=64 2.18 GB → flop/byte 0.016 / 0.250 / 7.88
PASS  bi / bk == 16.0
PASS  bk / bt > 31.0 && bk / bt < 32.0
15/15 passed
```

관찰용 파일 (CHECK 없음):

<!-- code: demo_bench.c -->

<!-- code: demo_sim.c -->

## 10. 핵심 정리

- 같은 O(n³) 라도 **안쪽 루프가 무엇을 열로 걷나** 가 성능을 정한다. 안쪽 반복당 미스: 안쪽 k (ijk) `1 + e/L`, 안쪽 j (ikj) `2e/L`, 안쪽 i (jki) `2`. 행 우선이면 ikj.
- M6 실측 n = 2048 double: ikj 13.1 · T=64 타일 8.6 · ijk 0.64 · jki 0.32 GFLOP/s. 벡터화를 끄면 ikj 6.8 → **캐시(순서) 몫 약 11배, SIMD 몫 약 2배**. Accelerate `dgemm` 427 GFLOP/s (천장 참고).
- `-Rpass=loop-vectorize` remark 를 믿지 말고 `-S` 로 실제 루프를 본다 (ijk·jki 의 벡터 경로는 `ld == 1` 일 때만). `-fno-vectorize` 빌드와 나란히 재야 SIMD 이득을 캐시 이득으로 착각하지 않는다.
- 2의 거듭제곱 행 간격 m 라인 → 열/타일이 `S / gcd(m, S)` 개 set 에만 들어간다 (M6 n = 2048 은 set 1개). ld 를 **한 라인 패딩** (행 간격 = 홀수 라인) 하거나 타일을 **패킹**.
- 타일 크기 1차 어림 `3·T²·e ≤ 캐시의 1/2~3/4` → 시뮬/측정으로 확정. 패딩 여부에 따라 최적 T 가 바뀐다 (M7 시뮬: 패딩 32, 없으면 16).
- 타일링은 미스만 줄인다. M6 에서는 L2 가 크고 프리페처가 순차 스트림을 숨겨 ikj 가 T = 64 타일보다 빨랐다 — 다음 병목은 FMA 당 load 2 + store 1, 답은 레지스터 블로킹 (BLAS 마이크로 커널).
- 시뮬레이터는 L1 미스의 순서·배수를 설명하고 (가정: 8-way LRU), L2·프리페처·명령 수는 설명 못 한다. 가설 → 측정 → 모델에 없는 요소 찾기.
- Cortex-M7 16 KB D-cache, 128×128 float: ijk(ld 128) 2.13 M 미스 → 패딩 + T=32 타일 2.4 만 (89배). 핫 타일은 DTCM 에, DMA 버퍼는 ARCH-M12 규칙.

## 참고자료

- Randal E. Bryant, David R. O'Hallaron, *Computer Systems: A Programmer's Perspective* 3판 — 6.6.2 "Rearranging Loops to Increase Spatial Locality" (행렬 곱 여섯 순서와 안쪽 반복당 미스), 6.6.3 블로킹.
- Monica S. Lam, Edward E. Rothberg, Michael E. Wolf, "The Cache Performance and Optimizations of Blocked Algorithms", ASPLOS 1991 — 타일 자기 충돌(self-interference)과 타일 크기 선택.
- Kazushige Goto, Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", ACM TOMS 2008 — 패킹과 계층별 블로킹 (GotoBLAS → OpenBLAS/BLIS).
- Mark D. Hill, Alan J. Smith, "Evaluating Associativity in CPU Caches", IEEE Trans. Computers 1989 — 3C (compulsory/capacity/conflict) 정의.
- John L. Hennessy, David A. Patterson, *Computer Architecture: A Quantitative Approach* 6판 — 2장 캐시 최적화 (loop interchange, blocking).
- Ulrich Drepper, *What Every Programmer Should Know About Memory* (2007) §6.2 — 행렬 곱 전치·블로킹 실측.
- Arm, *Cortex-M7 Processor Technical Reference Manual* (DDI 0489) — L1 D-cache: 4-way, 32 B 라인, 크기 구현 선택 (문서 기준). TCM 인터페이스.
- Apple, Accelerate framework `cblas_dgemm` 문서 (BLAS). 내부에서 AMX/SME 를 쓰는지는 Apple 이 자세히 밝히지 않는다 — 측정값만 인용.
- LLVM 문서 — Auto-Vectorization in LLVM, `-Rpass=loop-vectorize` / `-Rpass-missed` / `-Rpass-analysis` 옵션.
- 관련 레슨: ARCH-B10 (AMAT), ARCH-B12 (플래시 대기 상태), ARCH-M04 (직접 사상 시뮬레이터), ARCH-M05 (N-way · 3C), ARCH-M06 (쓰기 정책), ARCH-M08 (128 B 라인 · 측정 방식), ARCH-M12 (DMA clean/invalidate), MEM-M03 (다단 캐시), MEM-M06 (RFO 대역폭), MEM-A08 (roofline).

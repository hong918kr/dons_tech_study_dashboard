---
id: ARCH-M03
time: 90분
updated: 2026-11-11
---
# ARCH-M03 분기 예측 실측: 정렬 데이터 vs 랜덤 데이터

## 학습 목표

- 같은 임계값 필터(`a[i] >= 128` 인 원소의 합)가 **정렬 입력에서 0.27 ns/원소, 랜덤 입력에서 2.44 ns/원소** (Apple M6, 9배) 로 갈리는 것을 직접 재고, 그 차이로 **분기 미스 1번 ≈ 4.3–4.5 ns ≈ 18–19 사이클** 을 추정할 수 있다.
- `cc -O2 -S` 로 "내 branchy 코드가 정말 분기하는가" 를 확인할 수 있다. clang -O2 는 평범한 `if` 루프를 **NEON 으로 벡터화** 하거나 **CSEL 로 if-conversion** 해서 정렬 = 랜덤이 되게 만든다 — 그래서 벤치마크 전에 asm 부터 본다.
- branchless 를 두 가지로(삼항 → `csel`, 마스크 `-(x>=t) & x` → 역시 `csel`) 쓰고, **branchless 가 지는 경우**(선택이 긴 루프 의존 사슬 위에 놓일 때, 정렬 데이터에서 1.6배 느림)를 실측으로 설명한다.
- 미스율 모델을 숫자로 다룬다: 단일 2비트 카운터의 iid 미스율 `pq/(1-2pq)`, 최선 정적 예측 `min(p,1-p)`, 최소제곱 직선으로 "미스율 → ns" 기울기 구하기, 손익분기 미스율.
- 펌웨어 쪽으로 옮긴다: 넘침 없는 비교 마스크(`ge_mask_i32`), ISR 실행 시간을 데이터와 무관하게 만드는 branchless ADC 루프, Cortex-M4/M7 의 IT 블록 vs Cortex-M0+ 의 진짜 분기, "C 로 branchless 를 써도 컴파일러가 분기로 되돌린다".

## 1. 문제

"정렬된 배열을 처리하면 왜 더 빠른가요?" — Stack Overflow 에서 가장 유명한 질문이자, 펌웨어·실리콘·저지연 면접에서 **파이프라인과 추측 실행을 실제로 이해하는지** 보는 단골 질문이다. 답을 말로 하는 건 쉽다(분기 예측). 면접관이 보는 건 그 다음이다: "몇 사이클인데요?", "branchless 로 바꾸면 항상 빠른가요?", "컴파일러가 이미 바꿔 놓았으면요?", "Cortex-M 에서는요?". 이 레슨은 그 숫자를 이 맥(Apple M6)에서 직접 잰다.

이미 아는 것(다시 가르치지 않는다): 5단 파이프라인에서 분기가 EX 에서 결정되면 플러시 2사이클(ARCH-M01 규칙), 2비트 포화 카운터 예측기를 시뮬레이터에 넣는 것(ARCH-M02), NZCV 플래그와 조건 코드(ARCH-B02), 플래시 대기 상태와 prefetch 가 분기에서 깨지는 것(ARCH-B12). 다음 단계(여기서 만들지 않음): `CMP/B.cond/CSEL/CSINC/CSNEG` 를 명령 수준에서 하나씩(ARM-B05), `[[likely]]`·룩업 테이블 같은 저지연 C++ 기법(LLAT-B07), `perf stat -e branch-misses` 로 미스 수를 PMU 에서 직접 세기(LPERF-M01 — macOS 에서는 PMU 를 쉽게 못 읽어서 여기서는 **시간으로 역산** 한다), DWT 사이클 카운터로 IRQ 지연 재기(BM-A10).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">분기 하나 b.hi (a[i] &lt; t 이면 건너뜀) — 예측이 맞을 때 / 틀릴 때 / csel 일 때</text>
  <text x="10" y="52" fill="currentColor">예측 맞음</text>
  <rect x="110" y="38" width="60" height="22" class="f-ok" stroke="currentColor"/><text x="118" y="54" fill="currentColor" font-size="12">cmp</text>
  <rect x="170" y="38" width="60" height="22" class="f-ok" stroke="currentColor"/><text x="178" y="54" fill="currentColor" font-size="12">b.hi</text>
  <rect x="230" y="38" width="60" height="22" class="f-ok" stroke="currentColor"/><text x="238" y="54" fill="currentColor" font-size="12">ldrb</text>
  <rect x="290" y="38" width="60" height="22" class="f-ok" stroke="currentColor"/><text x="298" y="54" fill="currentColor" font-size="12">add</text>
  <rect x="350" y="38" width="60" height="22" class="f-ok" stroke="currentColor"/><text x="358" y="54" fill="currentColor" font-size="12">cmp …</text>
  <text x="430" y="54" fill="currentColor" font-size="12">손해 없음 (0.27 ns/원소)</text>
  <text x="10" y="112" fill="currentColor">예측 틀림</text>
  <rect x="110" y="98" width="60" height="22" class="f-ok" stroke="currentColor"/><text x="118" y="114" fill="currentColor" font-size="12">cmp</text>
  <rect x="170" y="98" width="60" height="22" class="f-warn" stroke="currentColor"/><text x="178" y="114" fill="currentColor" font-size="12">b.hi</text>
  <rect x="230" y="98" width="180" height="22" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="238" y="114" fill="currentColor" font-size="12">틀린 길 수십 개 명령 (추측)</text>
  <text x="110" y="138" fill="currentColor" font-size="12">b.hi 가 실행 단계에서 결정 → 추측 명령 버림(flush) → 옳은 주소에서 다시 fetch</text>
  <rect x="420" y="98" width="200" height="22" class="f-warn" stroke="currentColor"/>
  <text x="428" y="114" fill="currentColor" font-size="12">빈 파이프 ≈ 18–19 사이클</text>
  <rect x="620" y="98" width="60" height="22" class="f-ok" stroke="currentColor"/><text x="628" y="114" fill="currentColor" font-size="12">ldrb</text>
  <text x="420" y="156" fill="currentColor" font-size="12">M6 실측: 미스 ≈ 4.3–4.5 ns</text>
  <text x="10" y="200" fill="currentColor">csel (branchless)</text>
  <rect x="130" y="186" width="60" height="22" class="f-acc" stroke="currentColor"/><text x="138" y="202" fill="currentColor" font-size="12">ldrb</text>
  <rect x="190" y="186" width="60" height="22" class="f-acc" stroke="currentColor"/><text x="198" y="202" fill="currentColor" font-size="12">cmp</text>
  <rect x="250" y="186" width="60" height="22" class="f-acc" stroke="currentColor"/><text x="258" y="202" fill="currentColor" font-size="12">csel</text>
  <rect x="310" y="186" width="60" height="22" class="f-acc" stroke="currentColor"/><text x="318" y="202" fill="currentColor" font-size="12">add</text>
  <text x="390" y="202" fill="currentColor" font-size="12">제어 의존이 데이터 의존으로 바뀜: 추측도 flush 도 없다</text>
  <text x="24" y="230" fill="currentColor" font-size="12">대신 csel 은 cmp 결과를 기다린다. 결과가 루프 사슬에 들어가면 그 지연이 매 원소에 붙는다</text>
  <rect x="10" y="248" width="720" height="44" rx="6" fill="none" stroke="currentColor"/>
  <text x="24" y="267" fill="currentColor">정렬: N…N T…T → 미스는 경계에서 한두 번. 랜덤(p = 0.5): 원소 2개마다 1번 미스.</text>
  <text x="24" y="285" fill="currentColor" font-size="12">차이 (2.44 − 0.27) ns / 0.5 미스 = 미스당 4.33 ns — 이 레슨의 핵심 계산</text>
</svg>
```

체크 테스트(`starter.c` → `solution.c`)는 **결정적인 것만** 본다: 세 필터가 모든 입력에서 같은 합을 내는가, 2비트 카운터 모델이 정해진 패턴에서 정확한 미스 수를 내는가, 측정표 → 기울기·미스 비용 계산이 맞는가, 넘침 없는 마스크가 경계값에서 맞는가. **시간은 `demo_bench.c` 에서만** 재고, "분기가 남았나" 는 `asm_check.sh` 가 `-S` 결과로 확인한다.

### Part A — 세 가지 필터와 정답 기준

- `sum_ge_branchy(a, n, t)` — 평범한 `if (a[i] >= t) s += a[i]`. `t` 는 0..256 (256 이면 아무것도 고르지 않는다 — `uint8_t` 의 경계).
- `sum_ge_mask(a, n, t)` — `if`/`?:` 없이: 비교 결과 0/1 을 마스크 0/전부1 로 바꿔 `& x`.
- `sum_ge_hist(a, n, t)` — 오라클: 값별 개수를 세서 `Σ v·cnt[v]` (v ≥ t). 순서와 무관하므로 "정렬해도 합은 같다" 를 독립적으로 확인한다.
- `sort_u8(a, n)` — counting sort, O(n + 256). 벤치마크의 정렬 입력(16M 바이트)을 만든다.

### Part B — 분기가 진짜 남았나, 미스 하나는 몇 ns 인가

- 측정 전에 asm: `demo_kernels.c` 의 커널 12개를 `cc -O2 -S` 로 보고 `asm_check.sh` 로 "branchy 에는 데이터 의존 `b.cond`, branchless 에는 `csel`" 을 기계적으로 확인한다.
- `sim_2bit(a, n, t, init)` — 분기 하나를 2비트 포화 카운터 하나로 예측할 때 미스 수 (모델의 예측치).
- `fit_slope(x, y, n, &icpt)` — 최소제곱 직선. 임계값 스윕(미스율 0–0.5)의 실측표를 넣어 **기울기 = 미스당 ns** 를 구한다.
- `miss_cost_ns(ns_unpred, ns_pred, miss_rate)` — 두 점으로 구하는 미스당 ns. 클럭 추정(의존 `add` 사슬)으로 사이클로 바꾼다.
- `ge_mask_i32(x, t)` — `x >= t` 이면 `0xFFFFFFFF`. **비교 연산자 금지, 넘침(UB) 금지.** 32비트 차의 부호 비트를 쓰는 흔한 트릭(`ge_mask_i32_naive`, 주어짐)이 어디서 틀리는지도 CHECK 한다.

### Part C — ADC ISR: 데이터와 무관한 실행 시간

DMA 가 채운 ADC 블록(4096 샘플)에서 임계값 이상 샘플의 합과 개수를 ISR/태스크가 계산한다. 분기가 있으면 실행 시간이 신호 모양에 따라 달라진다(지터, WCET 분석이 어려워짐).

- `adc_sum_ge(s, n, t, &count)` — int32 샘플, `ge_mask_i32` 로 분기 없이 합·개수.
- `adc_sum_ge_i16(s, n, t)` — int16 샘플: int32 로 넓히면 차가 넘치지 않으니 32비트 부호 비트로 충분하다. 단 음수 `>>` 는 구현 정의(C11 6.5.7p5) → unsigned 시프트로.
- 관찰: `demo_cm.c` 를 Cortex-M4/M7/M0+ 로 `-S` — 같은 C 가 M4 에서는 IT 블록(분기 없음), M0+ 에서는 `blt` 분기, 그리고 "branchless 로 쓴 C" 도 M0+ 에서는 분기로 되돌아온다.

## 2. Problem (English)

You are asked to quantify branch misprediction cost on a real core (Apple M6, arm64) and decide when branchless code pays off.

**Part A.** Implement `uint64_t sum_ge_branchy(const uint8_t *a, size_t n, unsigned t)` (plain `if`), `uint64_t sum_ge_mask(...)` (no `if`/`?:`: build an all-ones/zero mask from the comparison), `uint64_t sum_ge_hist(...)` (histogram oracle, order-independent) and `void sort_u8(uint8_t *a, size_t n)` (counting sort). `t` ranges over 0..256. All three sums must agree on random, sorted and constant inputs.

**Part B.** Before timing anything, prove from `cc -O2 -S` output that your "branchy" kernel contains a data-dependent conditional branch and your branchless kernels contain `csel` (clang -O2 vectorizes or if-converts the naive loop). Implement `size_t sim_2bit(const uint8_t *a, size_t n, unsigned t, unsigned init)` (one 2-bit saturating counter; states 0–1 predict not-taken, 2–3 taken), `double fit_slope(const double *x, const double *y, int n, double *icpt)` (least squares; 0 for `n < 2` or all-equal `x`), `double miss_cost_ns(double ns_unpred, double ns_pred, double miss_rate)` (0 if `miss_rate <= 0`), and `uint32_t ge_mask_i32(int32_t x, int32_t t)` returning `0xFFFFFFFF` iff `x >= t`, using no comparison operators, no branches and no signed overflow.

**Part C.** Implement `int64_t adc_sum_ge(const int32_t *s, size_t n, int32_t t, size_t *count)` and `int32_t adc_sum_ge_i16(const int16_t *s, size_t n, int16_t t)` branch-free, correct for `INT32_MIN/MAX`, `INT16_MIN/MAX`.

Examples: `sum_ge_*(0..255, 256, 128) == 24512`; `sim_2bit(0..255, 256, 128, 1) == 2`; alternating T,N,T,N from state 1 → every prediction wrong; `miss_cost_ns(2.440, 0.273, 0.5) == 4.334`; `ge_mask_i32(INT32_MAX, -1) == 0xFFFFFFFF` while the 32-bit sign-bit trick returns 0.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M03
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
mkdir -p /tmp/esn_ARCH-M03
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-M03/st && /tmp/esn_ARCH-M03/st; echo "exit $?"
```

실제 출력 (발췌):

```text
-- Part A: three filters agree, counting sort --
FAIL  sum_ge_branchy(ramp, 256, 128) == 24512  (line 116)
...
PASS  sum_ge_branchy(ramp, 256, 256) == 0 && sum_ge_mask(ramp, 256, 256) == 0 && sum_ge_hist(ramp, 256, 256) == 0
...
-- Part B: 2-bit counter model, line fit, miss cost, overflow-safe mask --
FAIL  sim_2bit(ramp, 256, 128, 1) == 2  (line 171)
...
      2-bit sim on 1M random bytes: t=128 miss 0.0000, t=64 miss 0.0000 (best static: 0.5, 0.25)
...
PASS  ge_mask_i32_naive(INT32_MAX, -1) == 0
PASS  ge_mask_i32_naive(INT32_MIN, 1) == 0xFFFFFFFFu
-- Part C: ADC block, branchless sum/count --
      triangle+noise, t=1500: count 0, sum 0
FAIL  sum == rs && c == rc && c > 0 && c < 4096  (line 249)
...
12/44 passed
exit 1
```

통과하는 12개는 "0 을 돌려주면 우연히 맞는" 경계 케이스와, 주어진 `ge_mask_i32_naive` 가 **틀린다는 것** 을 기록한 CHECK 다.

## 4. 힌트

<details><summary>힌트 1 — 마스크는 부호 없는 0 - 1</summary>

`(x >= t)` 는 int 0/1. `-(uint64_t)(x >= t)` 는 0 또는 `0xFFFF…FFFF` (부호 없는 산술은 2^64 로 감긴다 — C11 6.2.5p9, UB 아님). 그 마스크와 `x` 를 AND 하면 고를 때만 `x`. 부호 있는 `-(int)(x>=t)` 도 동작하지만, 마스크는 unsigned 로 다루는 습관이 안전하다.
</details>

<details><summary>힌트 2 — 2비트 카운터는 "윗비트가 예측"</summary>

상태 c ∈ {0,1,2,3}. 예측 = `c >> 1` (0 = not-taken). 미스 판정을 **먼저** 하고, 그 다음 taken 이면 `c<3` 일 때 +1, 아니면 `c>0` 일 때 -1. 손으로 한 번 따라가 보라: 정렬된 0..255, t=128, 시작 1 — 0..127 동안 c 는 어디까지 내려가 있고, 128 과 129 에서 각각 무엇을 예측하나? (CHECK 의 답은 2.)
</details>

<details><summary>힌트 3 — 기울기 = 공분산 / 분산, 그리고 "== 0" 을 믿지 말 것</summary>

`slope = Σ(x-x̄)(y-ȳ) / Σ(x-x̄)²`. 교과서식 `n·Σx² − (Σx)²` 는 x 가 전부 0.3 이어도 이 맥에서 **2.22e-16** 이 나온다(0 이 아님) → 기울기가 엉뚱한 큰 수가 된다. "x 가 전부 같은가" 를 직접 검사하라.
</details>

<details><summary>힌트 4 — 넘침 없는 부호 비트</summary>

`x - t` 를 int32 로 하면 `INT32_MAX - (-1)` 이 넘친다(UB). int64 로 넓히면 차는 ±2^32 안이라 안전하다. 그 차를 `uint64_t` 로 바꾸고 `>> 63` 하면 "x < t" 가 0/1 로 나온다 (unsigned 시프트라 구현 정의도 없다). 1 → 0, 0 → 0xFFFFFFFF 는 `neg - 1u`.
</details>

<details><summary>힌트 5 — 개수와 합을 한 비트로</summary>

`bit = ge_mask_i32(s[i], t) & 1` (0/1). 개수는 `cnt += bit`. 합은 64비트 마스크 `-(int64_t)bit` (0 또는 -1) 와 `(int64_t)s[i]` 의 AND. `(int32_t)0xFFFFFFFFu` 같은 변환은 구현 정의(C11 6.3.1.3p3)라 피한다.
</details>

## 5. 풀이

### Part A — 세 판과 "-O2 가 내 if 를 어떻게 바꾸나"

| 함수 | 핵심 줄 | 메모 |
|---|---|---|
| `sum_ge_branchy` | `if (a[i] >= t) s += a[i];` | 소스는 분기지만 **-O2 결과는 분기가 아닐 수 있다** (아래) |
| `sum_ge_mask` | `s += -(uint64_t)(x >= t) & x;` | clang 은 이것을 다시 `cmp; csel` 로 만든다 (마스크 연산이 사라짐) |
| `sum_ge_hist` | `cnt[a[i]]++` 후 `Σ_{v≥t} v·cnt[v]` | 순서 무관 → 정렬/랜덤 입력의 합이 같다는 독립 오라클 |
| `sort_u8` | 개수 세고 다시 쓰기 | 1600만 바이트에 qsort 대신 O(n) |

같은 `if` 루프를 최적화 수준만 바꿔 컴파일하면 (`demo_kernels.c` 의 `k_auto_u8`, Apple clang 21, arm64):

| 빌드 | 루프 안에 나온 것 | 정렬 vs 랜덤 |
|---|---|---|
| `-O0` | `b.hs` / `b.lo` (진짜 분기) | 다르다 (하지만 -O0 은 다른 이유로 느려서 측정 대상이 아님) |
| `-O1` | `cmp w2, w9 ; csel x9, xzr, x9, hi ; add` | 같다 — if-conversion |
| `-O2` | 16바이트씩 `tbl` + `cmhi.4s` + `bic` + `add.2d` (NEON) | 같다 — 벡터화. 꼬리 처리 루프만 `csel` |
| `-O2` + `#pragma clang loop vectorize(disable)` | `cmp ; csel ; add` | 같다 |
| 위 + `__builtin_expect(a[i] >= t, 0)` | `add x9, x8, x9 ; csel x8, x8, x9, hi` | 같다 — expect 는 분기를 강제하지 않는다 |
| 위 + 조건 블록 안 `__asm__ volatile("" : "+r"(s))` | `cmp w2, w9 ; b.hi` | **다르다 — 측정하고 싶은 것** |

빈 `asm volatile` 이 효과가 있는 이유: 컴파일러는 그 asm 이 무엇을 하는지 모르므로, `a[i] < t` 일 때 실행하면 안 되는 블록을 무조건 실행하는 코드(select)로 바꿀 수 없다. 명령은 하나도 생기지 않는다. `optnone` 이나 `-O0` 처럼 루프 전체를 망가뜨리지 않고 **분기만 남긴다.**

### Part B — 증명부터: 분기가 정말 남았나 (asm)

```bash
cc -std=c11 -O2 -S demo_kernels.c -o - | less      # 함수별 루프 확인
sh asm_check.sh                                     # 기계적 검사
```

`-S` 결과에서 루프 부분 (명령 그대로, 주석만 덧붙임):

```text
_k_branchy_u8:                           ; 측정용 branchy
LBB2_3:  ldrb  w9, [x0]
         cmp   w2, w9
         b.hi  LBB2_2                     ; t > x 이면 건너뜀  ← 데이터 의존 분기
         ldrb  w9, [x0]
         add   x8, x8, x9
         b     LBB2_2
LBB2_2:  add   x0, x0, #1 ; subs x1, x1, #1 ; b.eq LBB2_1   ; 루프 카운터 (항상 예측됨)

_k_csel_u8:  (삼항)                      _k_mask_u8:  (-(x>=t) & x)
LBB3_1:  ldrb  w9, [x0], #1               LBB4_1:  ldrb  w9, [x0], #1
         cmp   w2, w9                              cmp   w2, w9
         csel  w9, wzr, w9, hi                     csel  x9, xzr, x9, hi
         add   x8, x8, x9                          add   x8, x9, x8
         subs  x1, x1, #1 ; b.ne LBB3_1            subs  x1, x1, #1 ; b.ne LBB4_1

_k_branchy_i32:  ldr w9,[x0] ; cmp w9, w2 ; b.lt LBB5_2 ; ldrsw x9,[x0] ; add x8,x8,x9
_k_csel_i32:     ldr w9,[x0],#4 ; cmp w9, w2 ; csel w9, wzr, w9, lt ; add x8, x8, w9, sxtw
```

`asm_check.sh` 실제 출력 (`b.ne/b.eq` 는 루프 카운터라 세지 않는다):

```text
PASS  k_auto_u8        expect neon    data-branches=2 csel=1 neon-cmp=5
PASS  k_scalar_u8      expect csel    data-branches=0 csel=1 neon-cmp=0
PASS  k_branchy_u8     expect branch  data-branches=1 csel=0 neon-cmp=0
PASS  k_csel_u8        expect csel    data-branches=0 csel=1 neon-cmp=0
PASS  k_mask_u8        expect csel    data-branches=0 csel=1 neon-cmp=0
PASS  k_branchy_i32    expect branch  data-branches=1 csel=0 neon-cmp=0
PASS  k_csel_i32       expect csel    data-branches=0 csel=1 neon-cmp=0
PASS  k_max_branchy    expect branch  data-branches=1 csel=0 neon-cmp=0
PASS  k_max_csel       expect csel    data-branches=0 csel=1 neon-cmp=0
PASS  k_hash_natural   expect csel    data-branches=0 csel=1 neon-cmp=0
PASS  k_hash_branchy   expect branch  data-branches=1 csel=0 neon-cmp=0
PASS  k_hash_csel      expect csel    data-branches=0 csel=1 neon-cmp=0
```

`k_auto_u8` 의 분기 2개는 루프 밖 크기 분기(`cmp x1, #3 ; b.hi`), csel 1개는 15바이트 이하 꼬리 루프다. 마스크 판과 삼항 판은 **같은 명령** 이 됐다 — C 에서 어떤 모양으로 썼는지보다 결과 asm 이 중요하다.

#### 측정: 정렬 vs 랜덤 (`demo_bench.c main`)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c demo_kernels.c -o /tmp/esn_ARCH-M03/bench
/tmp/esn_ARCH-M03/bench clock ; /tmp/esn_ARCH-M03/bench main
```

기계: `Apple M6` (Mac18,1), perflevel0/1/2 = 2 Super / 4 Performance / 6 Efficiency, `hw.cachelinesize` 128, `Apple clang version 21.0.0 (clang-2100.0.123.102)`. QoS `USER_INTERACTIVE` 요청 (코어 고정은 macOS 에서 불가 — 힌트일 뿐), 측정 중 본 코어 번호 6–11 (S/P, ARCH-M08 의 매핑). **load average 5.0–5.2** (다른 세션 여럿이 돌고 있었다). 각 칸 = 배열 한 번 훑기의 ns/원소, 데우기 1회 + **7회 중앙값**. 입력은 xorshift32 랜덤 바이트(=균등, t=128 → p ≈ 0.5)와 그것을 정렬한 것.

| u8, N = 16M, t = 128 | 정렬 ns/원소 | 랜덤 ns/원소 | 랜덤/정렬 |
|---|---|---|---|
| auto (-O2 그대로, NEON) | 0.217 | 0.217 | 1.00× |
| scalar (-O2, 벡터화만 끔 → if-conversion csel) | 0.243 | 0.253 | 1.04× |
| **branchy (`b.hi`)** | **0.273** | **2.440** | **8.93×** |
| csel (삼항) | 0.244 | 0.248 | 1.02× |
| mask `-(x>=t)&x` (→ csel) | 0.244 | 0.240 | 0.99× |
| int32, N = 4M, t = 0: branchy / csel | 0.268 / 0.290 | 2.396 / 0.277 | 8.9× / 0.96× |

클럭 추정 (`bench clock`): `add x, x, #1` 100개를 이어 붙인 의존 사슬이 `0.2321 ns/add` → **≈ 4.31 GHz** (정수 `add` 지연 1사이클 가정, cpus 6–10). Apple 은 코어별 클럭을 공개하지 않고 sysctl 에도 없다. S 코어와 P 코어의 클럭이 다를 수 있어 **± 10% 정도의 추정** 으로만 쓴다.

미스 비용 (Part B CHECK 와 같은 계산):

```text
miss_cost = (2.440 − 0.273) ns / 0.5 미스 = 4.33 ns/미스  →  × 4.31 GHz ≈ 18.7 사이클
```

같은 명령 몇 개를 도는 루프가 9배 느려진 것 전부가 "틀린 길을 버리고 다시 채우는 시간" 이다. 데스크톱급 깊은 out-of-order 코어에서 흔히 말하는 "미스 15–20 사이클" 과 같은 범위다 (Apple 공식 숫자는 없다 — 이것이 이 칩에서의 **측정 추정치**).

#### 측정: 임계값 스윕 → 직선 맞춤 (`bench sweep`)

두 점만 쓰면 잡음에 약하다. 랜덤 입력에서 t 를 0..256 으로 바꾸면 taken 확률 p 가 1 → 0 으로 움직이고, 미스율은 대략 `min(p, 1-p)` (0 → 0.5 → 0) 의 텐트 모양이 된다. 같은 run 의 실측 (4M 바이트, 7회 중앙값, cpus 5–11):

| t | p | min(p,1-p) | 2비트 sim | branchy ns | csel ns |
|---|---|---|---|---|---|
| 0 | 1.0000 | 0 | 0 | 0.286 | 0.247 |
| 32 | 0.8752 | 0.1248 | 0.1397 | 0.797 | 0.243 |
| 64 | 0.7505 | 0.2495 | 0.2992 | 1.306 | 0.238 |
| 96 | 0.6253 | 0.3747 | 0.4408 | 1.986 | 0.249 |
| 128 | 0.5004 | 0.4996 | 0.5000 | 2.387 | 0.243 |
| 160 | 0.3755 | 0.3755 | 0.4417 | 1.997 | 0.255 |
| 192 | 0.2502 | 0.2502 | 0.3002 | 1.458 | 0.269 |
| 224 | 0.1248 | 0.1248 | 0.1397 | 0.861 | 0.263 |
| 256 | 0 | 0 | 0 | 0.247 | 0.249 |

(17점 중 9점. 전체 17점은 `solution.c` Part B 의 `mx/my` 배열.) 직선 맞춤: **`ns/원소 = 0.272 + 4.498 × 미스율` → 미스당 4.50 ns ≈ 19.4 사이클.** 두 점 계산(4.33 ns)과 4% 차이.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">랜덤 u8, 임계값 t 스윕: branchy 는 텐트, csel 은 평평 (M6 실측, 7회 중앙값)</text>
  <line x1="80" y1="250" x2="720" y2="250" stroke="currentColor"/>
  <line x1="80" y1="36" x2="80" y2="250" stroke="currentColor"/>
  <line x1="76" y1="166" x2="720" y2="166" stroke="currentColor" stroke-dasharray="2 4"/>
  <line x1="76" y1="82" x2="720" y2="82" stroke="currentColor" stroke-dasharray="2 4"/>
  <g fill="currentColor" font-size="12">
    <text x="62" y="254">0</text><text x="62" y="170">1</text><text x="34" y="86">2 ns</text>
    <text x="76" y="268">0</text><text x="232" y="268">64</text><text x="390" y="268">128</text><text x="550" y="268">192</text><text x="706" y="268">256</text>
    <text x="360" y="284">임계값 t (p = taken 확률이 1 → 0)</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="5 4" points="80,227.2 120,203.5 160,179.9 200,156.3 240,132.7 280,109.1 320,85.5 360,61.9 400,38.2 440,61.9 480,85.5 520,109.1 560,132.7 600,156.3 640,179.9 680,203.5 720,227.2"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="80,226.0 120,204.3 160,183.1 200,161.8 240,140.3 280,108.6 320,83.2 360,61.7 400,49.5 440,53.9 480,82.3 520,106.2 560,127.5 600,153.1 640,177.7 680,202.1 720,229.3"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" stroke-dasharray="1 3" points="80,229.3 120,230.1 160,229.6 200,230.1 240,230.0 280,229.6 320,229.1 360,229.1 400,229.6 440,228.5 480,228.6 520,228.8 560,227.4 600,228.9 640,227.9 680,228.9 720,229.1"/>
  <line x1="80" y1="306" x2="110" y2="306" stroke="currentColor" stroke-width="2.5"/><text x="116" y="310" fill="currentColor" font-size="12">branchy 실측 (최대 2.39)</text>
  <line x1="290" y1="306" x2="320" y2="306" stroke="currentColor" stroke-dasharray="5 4"/><text x="326" y="310" fill="currentColor" font-size="12">0.272 + 4.498·min(p,1−p)</text>
  <line x1="520" y1="306" x2="550" y2="306" stroke="currentColor" stroke-width="2.5" stroke-dasharray="1 3"/><text x="556" y="310" fill="currentColor" font-size="12">csel 실측 (0.24–0.27)</text>
</svg>
```

모델 선택 메모: 각 점에서 `(ns − 0.272) / min(p,1−p)` 를 계산하면 4.1–4.8 ns 로 거의 일정하지만, 2비트 카운터 미스율 `pq/(1−2pq)` 로 나누면 3.5–4.5 ns 로 가운데가 처진다. 즉 **실제 예측기는 단일 2비트 카운터보다 낫고, iid 데이터에서 이론 최선인 "다수 쪽 찍기" 에 가깝다.** iid 랜덤은 어떤 예측기도 min(p,1−p) 아래로 내려갈 수 없다(과거가 미래에 대한 정보를 주지 않으므로).

#### 현대 예측기는 2비트 카운터가 아니다 (`bench period`)

길이 P 의 랜덤 바이트 블록을 반복해 4M 을 채우면, 결과 패턴은 주기 P 로 반복된다. 단일 2비트 카운터는 주기와 상관없이 약 50% 를 틀리지만 (`2bit-sim` 열), 실제 코어는 **과거 분기 결과의 이력(global history)** 으로 패턴을 외운다 (TAGE 계열로 알려져 있다 — Apple 구현은 미공개). 실측 (load ≈ 5):

| P | 1 | 4 | 16 | 64 | 256 | 1024 | 4096 | 16384 | 65536 | 262144 | 1048576 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| branchy ns | 0.251 | 0.454 | 0.264 | 0.269 | 0.274 | 0.470 | 0.571 | 0.643 | 1.471 | 2.323 | 2.435 |
| 2비트 sim 미스율 | 0 | 0.75 | 0.50 | 0.48 | 0.46 | 0.50 | 0.49 | 0.50 | 0.50 | 0.50 | 0.50 |

- **P ≤ 256 랜덤 패턴은 거의 완벽히 외운다** (0.27 ns ≈ 정렬 입력). 2비트 카운터라면 50% 미스 → 2.4 ns 였을 것이다.
- P = 1024–16384 에서 일부, P ≥ 262144 에서는 완전히 랜덤과 같다. 이 경계는 예측기 테이블 크기·이력 길이의 흔적이지만, 이것만으로 구조를 단정하지는 않는다.
- P = 4 의 0.454 는 부하가 높던 다음 run 에서 0.262 였다 (대신 그 run 은 P = 64 가 0.567 로 튀었다) — 한 칸의 튐은 잡음으로 보고, 숫자 하나를 결론으로 쓰지 않는다.
- 함의: **"랜덤처럼 보이는" 테스트 패턴이 짧은 주기로 반복되면 예측기가 외워서 벤치마크가 실제보다 좋게 나온다.** 벤치마크 입력은 예측기 기억보다 길게(여기서는 4M) 만든다.

#### branchless 가 지는 경우 — "-O2 가 알아서 csel 로 바꾼 것" 이 정렬 데이터에서 느리다

`csel` 은 조건이 나올 때까지 결과를 못 낸다. 선택된 값이 **다음 원소 계산의 입력** 이면(루프 의존 사슬), csel 판은 매 원소마다 사슬 전체를 기다린다. branchy 판은 예측이 맞는 한 "안 갱신하는" 원소를 사슬에서 빼 버린다. 조건부 FNV-1a 해시 `if (a[i] >= t) h = (h ^ a[i]) * 16777619u;` 로 확인했다 (`bench hash`, 16M, 7회 중앙값, load ≈ 7, cpus 0–11 — E 코어도 잠깐 섞였다):

```text
_k_hash_natural (-O2 가 고른 코드):  eor w11, w0, w10 ; mul w11, w11, w9 ; cmp w2, w10 ; csel w0, w0, w11, hi
```

| 조건부 FNV-1a | t=128 정렬 | t=128 랜덤 | t=224 정렬 | t=224 랜덤 |
|---|---|---|---|---|
| natural (-O2 → csel) | 1.175 | 1.438 | 1.239 | 1.371 |
| branchy (`b.hi`) | **0.724** | 2.835 | **0.450** | **0.938** |
| csel (`__builtin_unpredictable`) | 1.210 | 1.458 | 1.247 | 1.776 |
| 반복 4회 범위 (load 10–11): natural | 1.08–1.19 | 1.23–1.29 | 0.73–1.05 | 1.22–1.41 |
| 반복 4회 범위: branchy | 0.62–0.66 | 2.65–2.71 | 0.36–0.44 | 0.90–1.06 |

- csel 사슬 = `eor`(1) + `mul`(3) + `csel`(1) ≈ 5 사이클 = 1.16 ns @ 4.31 GHz → 랜덤 입력의 csel 실측 1.2–1.4 ns 와 대략 맞는다.
- **설명 못 한 관찰:** csel 판도 정렬 입력에서 더 빨랐다 — t=128 에서 2–20%, t=224 에서 10–50% (load 5–11 의 5회 run 모두; load 40 run 은 제외). "조건과 무관하게 매 원소 5사이클" 이라는 단순 모델로는 안 나오는 결과다. 코어가 예측 가능한 csel 조건을 이용하는 어떤 최적화가 있다고 **추정** 할 뿐, Apple 은 공개하지 않았고 여기서 확인하지 못했다. 그래도 **정렬 입력에서 branchy 가 csel 보다 빠르다** 는 결론은 모든 run 에서 같았다.
- branchy 정렬 t=128: 절반만 사슬에 들어감 → 0.72 ns. **컴파일러가 스스로 만든 branchless 가 정렬 입력에서 1.6배 느리다.** t=224 (12.5% 만 갱신) 이면 랜덤 입력에서도 branchy 가 이긴다: 미스율 ≈ 0.125, 0.45 + 0.125 × 4.5 ≈ 1.01 ns 예상 vs 실측 0.94.
- 같은 이유의 고전 예는 "달리는 최댓값" `m = a[i] > m ? a[i] : m` 인데, 이 맥에서는 **차이가 안 났다**: 랜덤 u32 16M 에서 branchy 0.244 / csel 0.245 ns, 오름차순 0.270 / 0.241. csel 사슬이 `cmp + csel` 2사이클이면 0.46 ns 가 나와야 하는데 약 1사이클(0.245 ns)이다. 이 경우도 csel 조건이 매우 예측 가능하다(랜덤 1600만 개에서 갱신 13번) — 위 관찰과 같은 원인일 수 있지만 확인하지 못했다. 교과서 예측이 실제 칩에서 안 맞을 수 있다 — 그래서 잰다.
- 정리: **branchless = 미스 비용을 "항상 내는 작은 비용" 으로 바꾸는 거래.** 손익분기 미스율 `m* = (ns_csel − ns_branchy_예측됨) / 미스비용` (연습 4): u8 합은 m* < 0 (csel 이 항상 이김), 해시 t=224 는 m* ≈ 0.175.

#### 측정 잡음 — 부하가 높을 때 다시 재 보면

같은 바이너리를 **load average 36–43** (다른 에이전트들이 대량 빌드 중)일 때 다시 돌린 실제 결과:

| u8 16M t=128 | 정렬 | 랜덤 | (load 5 일 때) |
|---|---|---|---|
| branchy | 0.328 | 3.215 | 0.273 / 2.440 |
| csel | 0.594 | 0.440 | 0.244 / 0.248 |
| mask (= 같은 asm) | 1.115 | 0.459 | 0.244 / 0.240 |
| int32 랜덤 branchy | — | 6.242 | 2.396 |

같은 asm 인 csel 과 mask 가 2배 다르고, "branchless 가 정렬에서 2.4배 느리다" 같은 거짓 결론이 나온다 (cpus 0–11 — E 코어로도 밀려났다). 대응: `uptime` 을 같이 기록, 중앙값, 같은 asm 끼리 비교해 잡음 크기를 본다, 부하가 낮을 때 다시. 이 레슨의 표는 load ≈ 5 run 의 것이고, CHECK 의 숫자도 그 run 에서 왔다.

load ≈ 10 에서 한 번 더 (재확인): branchy 정렬 0.340 / 랜덤 2.960 (8.7×), csel 0.319 / 0.350, mask 0.287 / 0.274, int32 branchy 0.288 / 2.617. 결론(branchy 만 9배, branchless 는 ±10% 안)은 같다. 같은 run 의 클럭 추정은 3.38 GHz (cpus 4–11, E 코어가 섞임) — **사이클 환산은 어느 코어에서 쟀는지에 따라 흔들린다.**

### Part B 보충 — 모델 함수들

- `sim_2bit`: 정렬 0..255, t=128, 시작 1 → 0..127 에서 0 까지 내려가고, 128(T, 예측 N) 미스 → 1, 129(T, 예측 N) 미스 → 2, 이후 맞음: **2번.** 교대 T,N,T,N 은 시작 1 에서 **100%** 미스(1↔2 사이를 오가며 늘 반대로 예측), 시작 0 이면 0↔1 사이에서 T 만 틀려 **50%**. 2비트 카운터의 약점이 그대로 보인다. iid p=0.75 에서는 0.25 가 아니라 **0.30** (연습 1 의 닫힌 식).
- `fit_slope`: 평균을 뺀 합으로 계산(수치 안정), x 가 모두 같으면 0. `n·Σx² − (Σx)² == 0` 검사는 실패한다(위 힌트 3, 실제 2.22e-16).
- `ge_mask_i32` 의 표 (`naive` = 32비트 차 `(uint32_t)x − (uint32_t)t` 의 부호 비트):

| x | t | 정답 x ≥ t | int64 차 | naive 32비트 차 | naive 결과 |
|---|---|---|---|---|---|
| 5 | 3 | 예 | 2 | 2 | 맞음 |
| INT32_MAX | -1 | 예 | 2^31 (양수) | 0x80000000 (부호 1) | **틀림 (0)** |
| INT32_MIN | 1 | 아니오 | −2^31 − 1 (음수) | 0x7FFFFFFF (부호 0) | **틀림 (전부 1)** |

부호 있는 int32 로 `x - t` 를 쓰면 이 두 줄은 아예 **UB** (C11 6.5p5) 다. 그래서 "부호 비트 트릭" 은 피연산자 범위가 작을 때(Part C 의 int16)만 32비트로 한다.

### Part C — ADC 블록과 Cortex-M

`adc_sum_ge` 는 `bit = ge_mask_i32(s, t) & 1` 하나로 개수와 합을 만든다. CHECK 는 삼각파(진폭 2048, 주기 512) + 노이즈 신호, `INT32_MIN/MAX` 섞인 배열, 임계값 `INT32_MIN`(전부)·`INT32_MAX`(MAX 인 샘플만), 랜덤 16 임계값을 branchy 기준과 비교한다. solution 실행 줄: `triangle+noise, t=1500: count 1543, sum 3498985`.

같은 필터를 Cortex-M 으로 (`demo_cm.c`, 루프 부분 발췌, 실제 출력):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm.c -o -
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -mthumb -O2 -S demo_cm.c -o -
```

```text
Cortex-M4 / M7  adc_sum_ge (평범한 if):   ldrsh r5,[r4,#8]! ; cmp r5, r2 ; it lt ; movlt r5, #0 ; add r1, r5   (× 4 언롤)
                adc_sum_ge_mask:         위와 똑같은 명령
Cortex-M0+      adc_sum_ge (평범한 if):   ldrsh r3,[r7,r3] ; cmp r3, r2 ; mov r6, r4 ; blt .LBB0_19 ; mov r6, r3
                adc_sum_ge_mask:         위와 똑같은 명령 (blt)
                adc_sum_ge_sign (d>>31): 위와 똑같은 명령 (blt) — 컴파일러가 부호 비트 트릭을 cmp+분기로 되돌림
                adc_sum_ge_sign_opaque:  ldrsh r5,[r0,r4] ; subs r6, r5, r2 ; asrs r6, r6, #31 ; bics r5, r6 ; adds r3, r5, r3
```

- **Cortex-M4/M7 (Thumb-2)**: `it lt; movlt` — IT 블록 조건 실행. 분기가 아니므로 예측도 미스도 없다. Thumb-2 의 IT 는 AArch64 `csel` 의 친척이다 (AArch64 에는 IT 가 없다). 조건이 거짓이어도 IT 안의 명령은 보통 사이클을 쓴다고 알려져 있다 — 그래서 실행 시간이 데이터와 무관해진다 (사이클 수는 코어 TRM 으로 확인 필요).
- **Cortex-M0+ (ARMv6-M)**: IT 도 CSEL 도 없다 → 조건 선택이 `blt` 분기가 된다. 소스에서 마스크·부호 비트 트릭으로 써도 clang 은 "이건 결국 `s >= t ? s : 0`" 이라고 알아보고 **분기로 되돌린다.** `d` 와 `neg` 두 값에 빈 asm 장벽(`__asm__("" : "+r"(v))`)을 걸어야 `subs/asrs/bics` 로 진짜 branchless 가 남는다 (한쪽만 걸면 `bmi` 나 `blt` 가 다시 생겼다 — 둘 다 실제로 해 봤다). 상수 시간 코드(SEC-B07)가 같은 문제를 겪는다.
- 분기 비용의 크기: Cortex-M3/M4 는 동적 분기 예측기가 없고 짧은 3단 파이프라인이라, taken 분기 하나가 파이프라인 리필 몇 사이클 수준이다(정확한 수는 TRM 의 명령 타이밍 표로 확인). Cortex-M7 은 분기 예측기를 가진 6단 dual-issue 파이프라인이다(세부는 미확인). 어느 쪽이든 M6 의 19 사이클보다 훨씬 작다. 대신 **플래시에서 실행하면** 분기가 prefetch 버퍼/ART 를 깨서 대기 상태가 붙는다 (ARCH-B12).
- 그래서 MCU 에서 branchless 의 주된 이유는 평균 속도가 아니라 **지터·WCET**: 분기 경로가 데이터에 따라 바뀌면 최악 경로를 찾아 재야 한다(RTOS-A03, TEST-A04). branchless 루프는 한 경로뿐이라 측정 한 번이 곧 최악이다 — 단, 위처럼 **asm 으로 확인한 경우에만.**

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-M03/so && /tmp/esn_ARCH-M03/so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-M03/san && /tmp/esn_ARCH-M03/san
```

```text
-- Part A: three filters agree, counting sort --
PASS  sum_ge_branchy(ramp, 256, 128) == 24512
...
-- Part B: 2-bit counter model, line fit, miss cost, overflow-safe mask --
PASS  sim_2bit(ramp, 256, 128, 1) == 2
...
      2-bit sim on 1M random bytes: t=128 miss 0.5001, t=64 miss 0.2991 (best static: 0.5, 0.25)
...
      M6 sweep fit: ns/elem = 0.272 + 4.498 * missrate
...
      miss cost = 4.33 ns = 18.7 cycles @ 4.31 GHz (dependent-add estimate)
...
-- Part C: ADC block, branchless sum/count --
      triangle+noise, t=1500: count 1543, sum 3498985
...
44/44 passed
```

ASan+UBSan 빌드도 경고 없이 `44/44 passed` — 넘침이 없다는 것(UBSan 은 signed overflow 를 잡는다)이 Part B 규칙의 기계적 확인이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| asm 을 안 보고 `-O2` 로 벤치 → "정렬해도 똑같네, 분기 예측은 신화" | clang -O2 가 NEON 벡터화 또는 csel if-conversion 을 해서 분기가 없다 (0.217 / 0.217) | `-S` 로 루프에 `b.cond` 가 있는지 확인, `asm_check.sh` |
| `__builtin_expect` 로 분기를 강제하려 함 | 확률 힌트일 뿐, 이 clang 은 그래도 `csel` 을 냈다 | 조건 블록 안 빈 `asm volatile` (명령 0개), 또는 다른 TU 의 함수 호출 |
| `-O0` 로 "분기 판" 을 만듦 | 분기는 남지만 모든 변수가 스택을 오가서 비교 대상(-O2 csel)과 조건이 달라진다 | 같은 -O2, 같은 TU 에서 차이 한 가지만 |
| `optnone` 을 함수에 붙임 | 루프 전체가 -O0 급 → 미스 비용이 아니라 최적화 유무를 잰다 | 위와 같음 |
| 마스크를 `(x - t) >> 31` (int32) 로 | `INT32_MAX - (-1)` 넘침은 UB, 음수 `>>` 는 구현 정의 | int64 차 + unsigned `>> 63` (`ge_mask_i32`) |
| "C 로 branchless 로 썼으니 됐다" | M0+ 에서 마스크·부호 트릭 모두 `blt` 로 되돌아옴 | 타깃별 `-S` 확인, 필요하면 값 장벽 |
| "branchless 는 항상 빠르다" | 선택이 루프 사슬 위면 매 원소 사슬 지연을 낸다: 해시 정렬 입력 0.72 → 1.18 ns | 손익분기 미스율 계산 + 둘 다 측정 |
| 테스트 패턴을 짧게 반복 (예: 랜덤 256바이트 × 반복) | 예측기가 외운다: P=256 이면 0.27 ns (정렬과 같다) | 예측기 기억보다 긴 입력 (M 단위) |
| 한 번 재고 결론 | load 40 에서 같은 asm 이 2배 차이 | `uptime` 기록, 7회 중앙값, 같은 asm 끼리 잡음 확인 |
| 2비트 카운터로 미스율을 추정 | 실제 예측기가 더 좋다 — iid 에서 min(p,1-p) 에 가깝다 | 모델은 상·하한으로만, 비용은 직선 맞춤 |

코드로 보면 첫 번째와 다섯 번째:

```c
/* 틀림 1: -O2 에서 벡터화/csel → 분기 측정이 안 된다 */
uint64_t f(const uint8_t *a, size_t n, unsigned t) { uint64_t s = 0;
    for (size_t i = 0; i < n; i++) if (a[i] >= t) s += a[i]; return s; }
/* 고침 1: 벡터화 끄고, 조건 블록을 무조건 실행 못 하게 (명령은 안 생김) */
uint64_t g(const uint8_t *a, size_t n, unsigned t) { uint64_t s = 0;
    _Pragma("clang loop vectorize(disable) interleave(disable)")
    for (size_t i = 0; i < n; i++) if (a[i] >= t) { __asm__ volatile("" : "+r"(s)); s += a[i]; } return s; }

/* 틀림 5: x - t 넘침(UB) + 음수 >> (구현 정의) */
uint32_t bad(int32_t x, int32_t t) { return ~(uint32_t)((x - t) >> 31); }
/* 고침 5 */
uint32_t good(int32_t x, int32_t t) { uint64_t d = (uint64_t)((int64_t)x - (int64_t)t); return (uint32_t)(d >> 63) - 1u; }
```

## 8. 면접 Q&A

<details><summary>Q1. 정렬된 배열이 왜 더 빠른가요? 숫자로 말해 보세요.</summary>

`if (a[i] >= 128)` 분기의 결과가 정렬 입력에서는 N…N T…T 라 예측기가 경계에서 한두 번만 틀리고, 랜덤 입력에서는 원소 2개마다 1번 틀린다. 틀리면 그 분기 뒤에 추측으로 가져온 명령을 전부 버리고 옳은 주소에서 다시 채운다. Apple M6 에서 잰 값: 정렬 0.273, 랜덤 2.440 ns/원소 → (2.44 − 0.27)/0.5 = **미스당 4.3 ns ≈ 19 사이클** (4.3 GHz 추정). 임계값 스윕 직선 맞춤으로도 4.5 ns.

꼬리: "그 숫자 어떻게 믿죠?" → (1) asm 으로 루프에 `b.hi` 가 있는지, 비교 대상에 `csel` 이 있는지 확인, (2) 7회 중앙값 + load 기록, (3) 두 점 계산과 17점 직선 맞춤이 4% 안에서 일치, (4) PMU 가 있으면 `branch-misses` 로 미스 수를 직접 세서 교차 확인 (리눅스 `perf stat`, LPERF-M01 — macOS 에서는 못 해서 시간으로 역산했다).
</details>

<details><summary>Q2. 그럼 -O2 로 컴파일하면 정렬/랜덤이 왜 똑같이 나오죠?</summary>

clang -O2 가 이 루프를 분기 없는 코드로 바꿨기 때문이다. 기본 -O2 는 NEON 으로 벡터화(`cmhi.4s` 로 16바이트를 한꺼번에 비교하고 `bic` 로 마스킹), 벡터화를 끄면 `cmp; csel` 로 if-conversion, -O1 도 csel. 분기가 없으니 예측할 것도 없다. 측정하려면 조건 블록 안에 빈 `asm volatile` 을 넣어 if-conversion 을 막았다 — `__builtin_expect` 는 효과가 없었다.

꼬리: "그럼 컴파일러가 알아서 해 주니 신경 안 써도 되나요?" → 아니다. (1) 컴파일러는 루프 몸체가 단순할 때만 바꾼다 — 함수 호출, 저장, 나눗셈 같은 부작용이 있으면 분기가 남는다. (2) 바꾼 것이 더 느릴 수도 있다 (Q4).
</details>

<details><summary>Q3. branchless 로 바꾸는 방법은? CSEL 과 마스크 트릭은 뭐가 다른가요?</summary>

삼항 `x >= t ? x : 0` 이나 마스크 `-(x >= t) & x` 모두 이 clang 에서 **같은 `cmp; csel`** 이 됐다. 컴파일러가 마스크 패턴을 알아보고 select 로 정규화하기 때문이다. 차이는 타깃에서 난다: AArch64 는 `csel/csinc/csneg`, Thumb-2(M3/M4/M7)는 `it` 블록, x86 은 `cmov`, ARMv6-M(M0/M0+)는 아무것도 없어서 분기가 된다. 진짜 branchless 가 필요하면(상수 시간·지터) 타깃마다 asm 을 확인한다 — M0+ 에서는 마스크 코드도 `blt` 로 되돌아와서 값 장벽이 필요했다.

꼬리: "x - t 의 부호 비트로 마스크를 만들면?" → 32비트에서 넘친다 (`INT32_MAX - (-1)`). 64비트로 넓히고 unsigned 시프트(`ge_mask_i32`). 피연산자가 int16 이면 int32 차로 충분하다.
</details>

<details><summary>Q4. branchless 가 더 느린 경우가 있나요?</summary>

있다. csel 은 조건과 두 입력이 모두 준비돼야 결과를 낸다. 선택된 값이 다음 반복의 입력이면(루프 의존 사슬) 그 지연을 매 원소 낸다. 예측이 잘 되는 branchy 는 추측으로 사슬을 건너뛴다. 실측: 조건부 FNV-1a 해시에서 -O2 가 스스로 만든 csel 판이 정렬 입력에서 1.18 ns, branchy 0.72 ns (1.6배). 갱신이 드물면(t=224, 12.5%) 랜덤 입력에서도 branchy 가 이겼다 (0.94 vs 1.37). 또 branchless 는 양쪽 값을 모두 계산하므로 한쪽이 비싸면(나눗셈, 캐시 미스 나는 로드) 손해다.

꼬리: "그럼 언제 무엇을?" → 손익분기 미스율 `m* = (csel 비용 − 예측된 branchy 비용) / 미스 비용`. 데이터의 미스율이 m* 보다 크면 branchless. 정렬 여부나 분포를 모르면 둘 다 재 본다. "달리는 최댓값은?" → 교과서상 csel 이 지는 예인데 M6 에서는 차이가 없었다(약 1사이클/원소) — 실제 칩은 교과서와 다를 수 있다.
</details>

<details><summary>Q5. 분기 예측기는 어떻게 동작하나요? 2비트 카운터면 충분한가요?</summary>

교과서 기본은 PC 로 인덱싱한 2비트 포화 카운터 테이블(ARCH-M02 에서 구현). 루프 분기처럼 거의 한쪽인 분기는 잘 맞히지만, 교대 패턴 T,N,T,N 은 시작 상태에 따라 50–100% 틀린다. 현대 고성능 코어는 과거 분기 결과 이력(global history)과 여러 길이의 이력을 쓰는 테이블(TAGE 계열이 대표)로 패턴을 외운다. M6 실측: 길이 256 의 랜덤 패턴을 반복하면 거의 완벽히 맞혀서 정렬 입력과 같은 0.27 ns 가 나왔고, 길이 26만 이상이면 완전 랜덤과 같았다.

꼬리: "iid 랜덤에서도 이력이 도움 되나요?" → 아니다. 과거가 미래에 대한 정보를 주지 않으므로 최선은 다수 쪽 찍기 = min(p,1−p). 실측 미스 비용이 이 모델로 나눌 때 가장 일정했다 (4.1–4.8 ns). 2비트 카운터 하나는 p=0.75 에서 0.25 가 아니라 0.30 을 틀린다 (pq/(1−2pq)).
</details>

<details><summary>Q6. Cortex-M 펌웨어에서도 이걸 신경 써야 하나요?</summary>

평균 속도 이유는 약하다. Cortex-M3/M4 는 동적 예측기 없이 짧은 파이프라인이라 분기 비용이 몇 사이클이고, 컴파일러가 짧은 조건문을 이미 IT 블록으로 만든다 (이 레슨의 필터가 M4/M7 에서 `it lt; movlt`). 신경 쓸 이유는 (1) **지터/WCET** — ISR 경로가 데이터에 따라 바뀌면 최악 경로 측정이 어렵다, (2) **플래시 대기 상태** — 분기가 prefetch/ART 를 깨면 대기 사이클이 붙는다 (ARCH-B12), (3) **상수 시간** — 비밀 데이터에 따라 분기하면 타이밍 부채널 (SEC-B07). M0/M0+ 는 IT 도 csel 도 없어서 branchless 를 원하면 asm 확인 + 값 장벽까지 가야 한다.

꼬리: "M7 은?" → M7 은 분기 예측을 하는 더 깊은 dual-issue 파이프라인이다(세부 수치는 TRM 확인). 그래도 미스 비용은 M6 의 19 사이클보다 훨씬 작다. 실제 측정은 DWT CYCCNT 로 (BM-A10).
</details>

<details><summary>Q7. 이 측정에서 가장 조심한 함정 세 가지는?</summary>

(1) **측정 대상이 실제로 존재하는가**: -O2 가 분기를 없앴다 → asm 확인. (2) **입력이 의도대로인가**: 짧은 패턴 반복은 예측기가 외운다 → 입력을 수 MB 로, 정렬 입력은 같은 데이터를 정렬해 "같은 값, 다른 순서" 만 다르게. 합이 같은지 CHECK(오라클 `sum_ge_hist`). (3) **잡음**: macOS 는 코어를 고정할 수 없고, load 40 에서 같은 asm 이 2배 차이 났다 → `uptime` 기록, 중앙값, 코어 번호 기록, 같은 asm 끼리 비교.

꼬리: "왜 클럭을 직접 재요?" → 사이클로 말하려면 클럭이 필요한데 sysctl 에 없다. 의존 `add` 사슬은 1 사이클/명령이라 ns/add 의 역수가 GHz — 단, S/P 코어가 섞이면 ±10% 오차.
</details>

## 9. 추가 연습

**연습 1 — 2비트 카운터의 iid 미스율.** 결과가 독립이고 taken 확률이 p 일 때, 단일 2비트 포화 카운터의 정상 상태 미스율을 닫힌 식으로 구하라. p = 0.5, 0.75, 0.9375 에서 값은? 최선 정적 예측 min(p, 1−p) 와 비교하면?

<details><summary>답</summary>

카운터는 위로 p, 아래로 q = 1−p 로 움직이는 출생-사망 사슬이고, 정상 분포는 π_k ∝ p^k q^(3−k) (k = 0..3). 미스율 = (π0 + π1)·p + (π2 + π3)·q. 분자 = pq³ + p²q² + p²q² + p³q = pq(p+q)² = pq, 분모 = q³ + pq² + p²q + p³ = p² + q² = 1 − 2pq. 따라서 **m(p) = pq / (1 − 2pq).** p=0.5 → 0.5, p=0.75 → 0.1875/0.625 = **0.30**, p=0.9375 → 0.0664 (min 은 0.0625). p 가 0.5 나 0/1 근처에서는 최선과 같고, 중간(0.75)에서 20% 더 틀린다 — 히스테리시스 때문에 소수 쪽 결과 하나에 상태가 흔들리기 때문. 시뮬레이션과 식이 0.004 안에서 맞는다 (practice.c). Part B 스윕의 실측은 이 식보다 min(p,1−p) 에 가까웠다.
</details>

**연습 2 — branchless 필터 복사.** `a[i] >= t` 인 원소만 `out` 으로 복사하는 함수를 분기 없이 쓰라 (stream compaction — 패킷 필터, DSP 블록에서 샘플 골라내기). 힌트: 버릴 원소도 일단 쓴다.

<details><summary>답</summary>

`out[k] = a[i]; k += (a[i] >= t);` — 매 원소 무조건 `out[k]` 에 쓰고, 고를 때만 k 를 1 늘린다. 다음 원소가 같은 자리를 덮으니 버린 값은 사라진다. k ≤ i 이므로 out 이 n 바이트면 범위 안이다 (out 크기가 "고른 개수" 만큼만이면 넘친다 — 계약에 적어 둘 것). clang -O2 결과는 `strb w12, [x3, x8] ; cmp w2, w12 ; cinc x8, x8, ls` — `cinc`(조건부 +1)로 분기가 없다. 랜덤 입력에서 branchy 판은 여기서도 원소 2개당 미스 1번.
</details>

**연습 3 — branchless lower_bound.** 정렬된 int32 배열에서 첫 `a[i] >= key` 의 위치를 분기 없이(루프 횟수가 n 에만 의존) 구하라.

<details><summary>답</summary>

`base = a; len = n; while (len > 1) { half = len/2; base += (base[half-1] < key) * half; len -= half; } return (base - a) + (*base < key);` (n = 0 이면 0). 루프는 ⌈log2 n⌉ 번, 비교 결과는 주소 선택에만 쓰인다. clang -O2: `cmp w10, w2 ; csel x10, x9, xzr, lt ; add x8, x8, x10, lsl #2` — csel. 랜덤 키 탐색에서 branchy 이진 탐색은 단계마다 50% 미스지만, branchless 판은 대신 **로드 → 비교 → 다음 로드 주소** 가 사슬이 되어 캐시 미스 지연을 숨기지 못한다. 큰 배열에서는 prefetch 를 같이 쓰는 기법이 알려져 있다. 중복 많은 배열·n = 0..1000·키 극단값에서 선형 탐색과 같은지 CHECK.
</details>

**연습 4 — 손익분기 미스율.** branchy 의 비용을 `예측될 때 ns + 미스율 × 미스 비용`, csel 비용을 데이터 무관 상수로 놓고, 둘이 같아지는 미스율 m* 을 구하라. M6 숫자로: (a) u8 합 — branchy 정렬 0.273, csel 0.244, 미스 4.33 ns. (b) 조건부 해시 t=224 — branchy 정렬 0.450, csel 1.239, 미스 4.50 ns. (b) 의 랜덤 입력(미스율 ≈ 0.125)에서는 어느 쪽?

<details><summary>답</summary>

m* = (csel − branchy_예측) / 미스비용. (a) (0.244 − 0.273)/4.33 = **−0.0067 ≤ 0** → csel 이 어떤 데이터에서도 이긴다 (분기 판은 명령이 하나 더 많아서 예측돼도 약간 느리다). (b) (1.239 − 0.450)/4.50 = **0.175** → 미스율 17.5% 아래면 branchy. 랜덤 t=224 의 미스율 ≈ 0.125 < 0.175 → branchy 예상 0.450 + 0.125·4.50 = 1.01 ns < 1.24 → branchy 가 이긴다 (실측 0.94 vs 1.37, 맞는 방향). 이 식이 면접에서 "branchless 가 항상 좋냐" 에 대한 정량적인 답이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-M03/pr && /tmp/esn_ARCH-M03/pr
```

```text
-- 연습 1: 2-bit counter iid closed form --
PASS  near(two_bit_iid_missrate(0.5), 0.5, 1e-12)
PASS  near(two_bit_iid_missrate(0.75), 0.3, 1e-12)
PASS  two_bit_iid_missrate(1.0) == 0 && two_bit_iid_missrate(0.0) == 0
      t= 16 p=0.9375  sim 0.0664  formula 0.0664  best-static 0.0625
      t= 64 p=0.7505  sim 0.2990  formula 0.2993  best-static 0.2495
      t=128 p=0.5010  sim 0.5002  formula 0.5000  best-static 0.4990
      t=192 p=0.2504  sim 0.3000  formula 0.3005  best-static 0.2504
      t=240 p=0.0624  sim 0.0662  formula 0.0662  best-static 0.0624
PASS  ok
-- 연습 2: branchless filter copy --
PASS  ok2
PASS  filter_ge_branchless(a, 0, 0, out2) == 0
PASS  filter_ge_branchless(a, 5000, 0, out2) == 5000 && filter_ge_branchless(a, 5000, 256, out2) == 0
-- 연습 3: branchless lower_bound --
PASS  ok3
PASS  lower_bound_branchless(one, 1, 7) == 0 && lower_bound_branchless(one, 1, 8) == 1 && lower_bound_branchless(one, 0, 8) == 0
-- 연습 4: break-even miss rate (M6 numbers) --
      sum: m* = -0.0067 (<= 0 -> csel always wins)   hash: m* = 0.175
PASS  m1 <= 0
PASS  near(m2, 0.1753, 0.001)
PASS  0.450 + 0.125 * 4.50 < 1.239
PASS  breakeven_missrate(1.0, 2.0, 0.0) == 0
13/13 passed
```

관찰용 파일 (CHECK 없음):

<!-- code: demo_kernels.c -->

<!-- code: demo_bench.c -->

<!-- code: demo_cm.c -->

<!-- code: asm_check.sh -->

## 10. 핵심 정리

- 분기 미스 = 추측으로 가져온 명령을 버리고 다시 채우는 시간. **Apple M6 실측: 미스당 4.3–4.5 ns ≈ 18–19 사이클** (정렬 0.27 vs 랜덤 2.44 ns/원소, 9배; 임계값 스윕 직선 기울기 4.50 ns).
- **측정 전에 asm.** clang -O2 는 평범한 `if` 필터를 NEON 벡터화 또는 `csel` 로 바꾼다. `__builtin_expect` 로는 분기가 안 남고, 조건 블록 안 빈 `asm volatile` 이 명령 0개로 분기를 남긴다. `asm_check.sh` 로 기계적 확인.
- branchless(삼항·마스크 모두 `csel`)는 미스를 "항상 내는 작은 비용" 으로 바꾸는 거래. 선택이 루프 의존 사슬 위면 정렬 데이터에서 진다 (조건부 해시 0.72 → 1.18 ns). 손익분기 `m* = (csel − branchy_예측)/미스비용`.
- 미스율 모델: iid 최선 = min(p,1−p), 단일 2비트 카운터 = pq/(1−2pq) (p=0.75 → 0.30). 실제 예측기는 이력으로 패턴을 외운다 — 주기 256 랜덤 패턴은 완벽히, 26만 이상은 못 외움. 벤치 입력은 길게.
- 넘침 없는 비교 마스크: int64 차 + unsigned `>> 63` (`ge_mask_i32`). 32비트 차 부호 비트는 `INT32_MAX` vs `-1` 에서 틀리고 signed 면 UB.
- Cortex-M: M4/M7 은 IT 블록(분기 없음), M0+ 는 분기 — C 로 쓴 마스크·부호 트릭도 되돌아온다(값 장벽 필요). MCU 에서 branchless 의 이유는 평균 속도보다 **지터/WCET/상수 시간**.
- 잡음: load 40 에서 같은 asm 이 2배 차이. `uptime`, 7회 중앙값, 코어 번호, 같은 asm 끼리 비교.

## 참고자료

- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487) — `CSEL/CSINC/CSNEG`, `B.cond`, 조건 코드 (C6 A64 instruction set).
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403) — IT 블록; *ARMv6-M Architecture Reference Manual* (DDI 0419) — IT/조건 실행 없음.
- Arm, *Cortex-M4 Technical Reference Manual* (DDI 0439) 명령 타이밍 표, *Cortex-M7 TRM* (DDI 0489) 분기 예측 — 사이클 수는 여기서 확인 (이 레슨은 수치를 단정하지 않음).
- J. E. Smith, "A Study of Branch Prediction Strategies", ISCA 1981 — 2비트 포화 카운터.
- A. Seznec, P. Michaud, "A case for (partially) TAgged GEometric history length branch prediction", JILP 2006 — TAGE.
- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach* — 3장 동적 분기 예측, 추측 실행.
- Stack Overflow, "Why is processing a sorted array faster than processing an unsorted array?" (2012) — 이 실험의 원형.
- LLVM: `__builtin_unpredictable`, `#pragma clang loop vectorize(disable)` (Clang Language Extensions 문서).
- ISO/IEC 9899:2011 — 6.2.5p9 unsigned 감김, 6.3.1.3p3 범위 밖 signed 변환(구현 정의), 6.5p5 signed overflow(UB), 6.5.7p5 음수 `>>`(구현 정의).
- 관련 레슨: ARCH-M01/M02 (파이프라인 플러시, 2비트 예측기 시뮬레이터), ARCH-B02 (NZCV), ARCH-B12 (플래시 prefetch 와 분기), ARCH-M08 (측정 방법·코어 번호 매핑), ARM-B05 (CSEL 명령군), LLAT-B07 (저지연 branchless 기법), LPERF-M01 (PMU branch-misses), SEC-B07 (상수 시간), RTOS-A03 · TEST-A04 (WCET), BM-A10 (DWT 사이클 측정).

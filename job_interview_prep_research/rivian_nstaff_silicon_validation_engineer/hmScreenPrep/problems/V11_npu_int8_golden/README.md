# V11 · AI Accelerator Validation: INT8 GEMM Golden Model + Bit-Exact Compare

> **ID**: V11 · **Priority**: P0 · **Pattern**: golden model + comparator + failure signature · **Time**: 35 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V11` / `./run.sh V11 sol`

## 1. Problem

You validate the NPU of an in-house AI accelerator SoC. A quantized fully-connected (or 1×1 conv) layer runs on the NPU as an int8 GEMM. Write:

1. the **C golden model** that the compiler team signs off on (same math as TFLite/gemmlowp),
2. a **comparator** that reports how the NPU output differs from golden,
3. a **diagnose** step that turns the report into a likely bug class, and
4. a **seeded stimulus generator** with corner values.

**Math (the spec from the compiler team).** `A[M][K]`, `B[K][N]`, `C[M][N]` are row-major int8; weights are symmetric (zero point 0).

```
acc[m][n] = bias[n] + sum_k (A[m][k] - a_zp) * B[k][n]          int32 accumulate
C[m][n]   = clamp(requant(acc, mult[c], shift[c]) + out_zp, act_min, 127)
            c = n (per-channel) or 0 (per-tensor);  act_min = relu ? out_zp : -128

requant(x, mult, shift) = RDBPOT(SRDHM(x, mult), shift)
  SRDHM(a, b):  if a == b == INT32_MIN return INT32_MAX
                p = (int64)a * b;  nudge = p >= 0 ? 1<<30 : 1 - (1<<30)
                return (int32)((p + nudge) / 2^31)            C division, truncates toward 0
  RDBPOT(x, e): mask = (1<<e) - 1;  rem = x & mask;  thr = (mask >> 1) + (x < 0)
                return (x >> e) + (rem > thr)                 arithmetic shift, 0 <= e <= 31
real scale = mult / 2^31 / 2^shift,  mult in [2^30, 2^31)
```

```c
typedef struct { int32_t a_zp, out_zp; const int32_t *mult; const int8_t *shift;
                 bool per_channel, relu; } QParams;
typedef enum { CMP_BITEXACT = 0, CMP_TOL1 = 1 } CmpMode;
typedef struct { uint32_t mismatches, hist[3]; int32_t max_abs; int first_m, first_n;
                 uint32_t col_bad[CMP_MAX_N]; } CmpReport;
typedef enum { DIAG_PASS, DIAG_TILE_EDGE, DIAG_ONE_CHANNEL, DIAG_ROUNDING, DIAG_CORRUPTION } Diag;

int32_t srdhm(int32_t a, int32_t b);
int32_t rdbpot(int32_t x, int e);
int8_t  requant_i8(int32_t acc, int32_t mult, int shift, int32_t out_zp,
                   int32_t act_min, int32_t act_max);
void    gemm_ref(const int8_t *A, const int8_t *B, const int32_t *bias, int M, int K, int N,
                 const QParams *q, int8_t *C, int32_t *acc_out /* may be NULL */);
void    cmp_tensors(const int8_t *gold, const int8_t *hw, int M, int N, CmpMode mode, CmpReport *r);
Diag    diagnose(const CmpReport *r, int N, int tile);
void    gen_tensor(int8_t *t, size_t n, uint32_t seed, int8_t zp);
```

- **cmp_tensors**: `CMP_BITEXACT` (default) fails on any difference; `CMP_TOL1` accepts `|diff| <= 1`. Report `mismatches` (outside tolerance), `max_abs` and `hist` (`|d| == 0`, `== 1`, `> 1`) over **all** elements, the first mismatch in row-major order (`-1, -1` if none), and per-column mismatch counts `col_bad[n]`.
- **diagnose**, rules in this order: no mismatch → `PASS`; `N % tile != 0` and every bad column is `>= tile * floor(N / tile)` → `TILE_EDGE`; all mismatches in one column → `ONE_CHANNEL`; `max_abs == 1` → `ROUNDING`; else → `CORRUPTION`.
- **gen_tensor**: LCG `s = s * 1664525 + 1013904223` (mod 2^32), one step per element. If `(s >> 8) % 8 == 0` the element is `CORNER[(s >> 4) & 3]` with `CORNER = { -128, 127, 0, zp }`, else `(int8_t)(s >> 24)`.
- No malloc. `N <= CMP_MAX_N (32)`, `K <= 64`.

The tests include a provided **NPU behavioural model** `npu_gemm(..., bug)` that computes 8 output columns per tile (`NPU_TILE = 8`) and can inject four bugs: `BUG_TRUNC_ROUND` (floor instead of round), `BUG_ACC16` (accumulator wraps at 16 bits), `BUG_NO_BIAS_CH` (bias of channel 5 dropped) and `BUG_TILE_EDGE` (the last, partial tile drops `k = K-1`). Your comparator and diagnose must catch and classify each.

### Example

```
A = [[11, 21, 31], [-9, 1, 127]]   B = [[1, -1], [2, 0], [3, 4]]   bias = [10, -20]
a_zp = 1, out_zp = -1, per-tensor mult = 0x40000000 (0.5), shift = 1   -> scale 0.25

acc = [[150, 90], [378, 494]]
C   = [[37, 22], [94, 123]]        150 * 0.25 = 37.5 -> 38, - 1 = 37
relu, out_zp = -100 -> C = [[-62, -77], [-5, 24]]

Layer M=6, K=32, N=12 vs the NPU model (bit-exact mode):
  BUG_NONE        0 mismatches                       -> PASS
  BUG_TRUNC_ROUND 37 mismatches, max |d| 1           -> ROUNDING     (TOL1: 0 mismatches)
  BUG_ACC16       27 mismatches, max |d| 86          -> CORRUPTION   (exactly the 27 with |acc| > 32767)
  BUG_NO_BIAS_CH  6 mismatches, all in column 5      -> ONE_CHANNEL
  BUG_TILE_EDGE   16 mismatches, first at (0, 8)     -> TILE_EDGE
```

### Constraints

- `M, N <= 32`, `K <= 64`, `0 <= shift <= 31`. Right shift of a negative `int32_t` is arithmetic (gcc/clang).
- The golden model must be **bit-exact** with the formulas above, including how ties round.

### Clarifying questions to ask (first 3 minutes)

1. Is the golden reference TFLite/gemmlowp-style fixed point, or float scale + round? (They differ in ties.)
2. Per-tensor or per-channel quantization? Symmetric weights? Where is the zero point applied?
3. Is the accumulator 32-bit on the HW? Is bias added before or after accumulation, in int32?
4. Is bit-exact the pass criterion, or is ±1 LSB acceptable for some layers?
5. What tile sizes does the NPU use, and which layer shapes do not divide evenly?
6. How do I get the HW output — DMA to DDR and read back, or a debug port?

## 2. Your workspace

`./run.sh V11`로 바로 테스트하세요. 테스트 5묶음(마지막 묶음에 버그 4종)이 있고, 아래쪽 `npu_gemm` 모델은 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 공식을 "그대로" 옮기는 것이 golden model</summary>

`srdhm`/`rdbpot`은 위 의사코드를 한 줄씩 옮기면 됩니다. "더 깔끔한" 반올림(`llround`, `(x + half) >> e`)으로 바꾸면 음수 tie에서 1 LSB 차이가 나고, 그 순간 golden이 아닙니다. 64비트 곱(`(int64_t)a * b`)을 잊으면 overflow(UB)입니다.

</details>

<details>
<summary>Hint 2 — 비교기는 "얼마나·어디서"를 같이 센다</summary>

원소마다 `d = |hw - gold|`를 구해 `hist[d==0 ? 0 : d==1 ? 1 : 2]++`, `max_abs` 갱신, `d > tol`이면 `mismatches++`, `col_bad[n]++`, 첫 번째면 `(m, n)` 저장. 이 네 가지 요약이 곧 버그의 지문(signature)입니다.

</details>

<details>
<summary>Hint 3 — 진단 규칙은 "공간 패턴 → 크기" 순서</summary>

먼저 **어디서** 틀렸나(마지막 partial tile 열에만? 한 열에만?)를 보고, 그다음 **얼마나** 틀렸나(최대 1이면 반올림)를 봅니다. 순서가 바뀌면 bias 누락(최대 3)이나 tile 버그가 엉뚱하게 분류됩니다. tile 경계 = `tile * (N / tile)`.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 양자화 데이터 경로

<div class="chart" data-src="chart2.svg" data-caption="int8 × int8 → int32 누적 → SRDHM → RDBPOT → +zp, clamp. 아래는 테스트 2번 C[0][0] 계산과, 주입 버그 4종이 어느 단계에 있는지"></div>

비트 폭을 먼저 말할 수 있어야 합니다. `A − a_zp`는 9비트(최대 ±255), 곱은 17비트(`|p| <= 255 × 128 = 32 640`), K = 32개를 더하면 17 + 5 = **22비트**. 그래서 16비트 누산기(BUG_ACC16)는 틀리고, 32비트면 K가 2^15까지 안전합니다.

### 4.2 Tile 경계 버그 — 실패 위치가 원인을 말한다

<div class="chart" data-src="chart1.svg" data-caption="BUG_TILE_EDGE의 HW − golden. 실패가 열 8–11(마지막 partial tile)에만 몰려 있고 col_bad = 0,…,0,4,4,4,4. 행 2, 3은 빠뜨린 항이 작아(A−zp = 2, 1) 출력 LSB보다 작아서 운 좋게 통과"></div>

이 그림에서 두 가지를 말합니다.

- **위치가 원인을 가리킵니다.** 실패가 열 ≥ 8에만 있으면 "remainder/partial tile 경로" 버그입니다. N = 16처럼 tile 배수인 shape만 테스트하면 이 버그는 영원히 안 보입니다 → stimulus에 **N % TILE != 0인 shape**을 반드시 넣습니다.
- **값이 작으면 버그가 숨습니다.** 행 2, 3은 같은 버그를 겪었지만 빠진 항(2·B, 1·B)이 scale ≈ 1/1000 아래로 사라졌습니다. 그래서 corner 값(−128, 127)을 섞는 stimulus가 필요합니다.

### 4.3 손으로 따라가기 (trace)

C[0][1] 하나(테스트 5, per-channel `mult[1] = 0x42000000` = 0.515625, `shift = 9`, `out_zp = −4`):

| 단계 | golden | BUG_TRUNC_ROUND | BUG_ACC16 |
|---|---|---|---|
| acc | 53 752 | 53 752 | 53 752 − 65 536 = **−11 784** (16비트 wrap) |
| SRDHM | 27 715.875 → 27 716 | floor → 27 715 | −6 076.125 → −6 076 |
| RDBPOT(·, 9) | 54.13 → 54 | 27 715 >> 9 = 54 | −11.87 → −12 |
| + out_zp | **50** | 50 (이 원소는 같음) | **−16** (d = 66) |

C[0][0]: acc 16 348 → SRDHM 8 174 → 8 174 / 512 = 15.96 → golden 16 − 4 = **12**, truncate는 15 − 4 = 11 (d = −1). 그래서 TRUNC 버그의 첫 mismatch가 (0, 0)입니다.

버그별 비교 결과 (bit-exact, `M=6 K=32 N=12`, 총 72원소):

| 주입 버그 | mismatches | hist (0 / 1 / >1) | max \|d\| | first | col_bad | diagnose |
|---|---|---|---|---|---|---|
| 없음 | 0 | 72 / 0 / 0 | 0 | (−1, −1) | 전부 0 | PASS |
| TRUNC_ROUND | 37 | 35 / 37 / 0 | 1 | (0, 0) | 4 2 4 3 3 4 3 5 3 4 1 1 | ROUNDING (TOL1이면 0개) |
| ACC16 | 27 | 45 / 0 / 27 | 86 | (0, 1) | 3 2 1 3 1 3 3 0 4 4 1 2 | CORRUPTION (27개 = \|acc\| > 32 767인 원소 전부) |
| NO_BIAS_CH (ch 5) | 6 | 66 / 0 / 6 | 3 | (0, 5) | 열 5만 6 | ONE_CHANNEL |
| TILE_EDGE | 16 | 56 / 2 / 14 | 13 | (0, 8) | 0×8, 4 4 4 4 | TILE_EDGE |

stimulus: `gen_tensor(g, 64, 1, 5)` → 처음 4개 `60, 94, −127, −76`, 64개 중 corner 경로가 10번(−128 ×2, 127 ×2, 0 ×5, zp ×1). 테스트 5의 층은 `A = gen(seed 1, zp 3)`, `B = gen(seed 2, zp 0)`, `bias[n] = ±(1000 + 250n)`, `mult[n] = 0x40000000 + n·0x02000000`, `shift = 9` → `gold[0] = 12, gold[1] = 50, gold[11] = −3, gold[71] = −71`.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `srdhm`의 음수 tie: `srdhm(-3, 1<<30)`은 −1.5인데 결과는 **−1**(양수 1.5는 2). nudge가 `1 − 2^30`이고 C 나눗셈이 0 쪽으로 자르기 때문입니다. 반면 `rdbpot(-5, 1)`은 −3(0에서 먼 쪽). 이 비대칭까지 똑같아야 bit-exact입니다. 테스트 1번이 이것을 고정합니다.
- `gemm_ref`는 `acc_out`으로 int32 누산값을 내보냅니다. 비교기는 출력만 보지만, **진단할 때는 중간값**이 필요합니다(테스트 5c: 실패 27개가 정확히 `|acc| > 32767`인 원소와 일치 → 누산기 폭 버그 확정).
- `cmp_tensors`의 `hist`·`max_abs`는 tolerance와 무관하게 전체 원소 기준입니다. TOL1 모드에서도 "±1이 37개 있었다"는 정보가 남아야 회귀를 추적할 수 있습니다.
- `diagnose`는 휴리스틱입니다. 확정이 아니라 **어디를 먼저 볼지** 알려 주는 용도이고, 리포트에 `first (m, n)`과 `col_bad`를 그대로 남겨 RTL 엔지니어가 파형에서 그 좌표를 찾게 합니다.
- `gen_tensor`는 `(int8_t)(uint8_t)(s >> 24)`로 변환합니다. 범위 밖 값을 signed로 바로 바꾸는 것은 implementation-defined라 두 단계로 씁니다.

### 4.5 복잡도

- `gemm_ref`: 시간 **O(M·K·N)**, 추가 메모리 O(1). 비교·진단: O(M·N) + O(N).
- 실제 층(예: 4096×4096)은 C 모델이 초 단위라 괜찮지만, 시뮬레이터 위의 RTL은 그 층 하나에 몇 시간입니다. 그래서 RTL 시뮬레이션에는 작은 shape + 경계 shape를, FPGA/에뮬레이터·실리콘에는 실제 네트워크 층을 씁니다.

---

## 5. 임베디드 관점으로 말할 것

- **검증 루프**: (1) seed로 A/B/bias 생성 → (2) DMA로 DDR/SRAM에 올리고 NPU 디스크립터 실행 → (3) 결과를 읽어 golden과 비교 → (4) 실패하면 seed·shape·첫 좌표·col_bad를 로그. seed만 있으면 실패를 **재현**할 수 있다는 것이 핵심입니다.
- **bare-metal에서**: golden은 같은 SoC의 CPU(A-core)에서 돌려도 되고, 미리 계산한 기대값을 이미지에 넣어도 됩니다. 캐시가 있으면 NPU가 쓴 버퍼를 읽기 전에 **cache invalidate**(DMA coherency) 해야 하고, 이것을 빠뜨린 "가짜 mismatch"가 첫 실리콘에서 가장 흔합니다 → [V3](../V3_dma_desc_ring/index.html).
- **성능 측정은 같은 하네스로**: 같은 층을 돌리며 사이클 카운터로 latency, `2·M·K·N / time`으로 실효 TOPS, 피크 대비 **MAC utilization**을 계산합니다 → [V4](../V4_perf_bench_stats/index.html).
- **커버리지**: shape(정확히 나눠지는/안 나눠지는 M, N, K), per-tensor/per-channel, relu on/off, zero point 극값, 포화가 일어나는 scale을 축으로 cross coverage를 잡습니다 → [V5](../V5_test_runner_coverage/index.html).

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"Golden model이 맞다는 건 어떻게 아나?"** — 컴파일러 팀의 레퍼런스(TFLite 커널 등)와 같은 입력으로 교차 비교하고, 손으로 계산 가능한 작은 케이스(테스트 2번), 경계값(INT32_MIN·tie) 단위 테스트로 고정합니다. golden과 HW가 다르면 **세 번째 구현**(예: Python/NumPy)으로 누가 틀렸는지 판정합니다.
  *"I don't trust a golden model until it matches the framework reference on real layers and passes hand-computed corner cases, including tie rounding."*
- **"FPGA/HAPS에서 통과했는데 실리콘에서 실패하면?"** — 같은 seed·shape로 재현 → 실패 위치 패턴(tile, 채널, 특정 SRAM bank 주소)을 봅니다. 패턴이 데이터 무관하고 주파수·전압에 따라 변하면 타이밍/전원 문제라 shmoo로 넘기고([V7](../V7_shmoo_margin/index.html)), 항상 같은 좌표면 기능 버그나 메모리 결함입니다([V1](../V1_march_memtest/index.html)).
  *"If the failing coordinates move with voltage and frequency it's margin; if they're stable and data-dependent it's logic — that split decides who gets the bug."*
- **"테스트가 몇 개면 충분한가? 시간은?"** — 무작위만으로는 tile 경계·포화 같은 희귀 경로가 안 나옵니다. directed(경계 shape, corner 값) + random(seed sweep) + 실제 네트워크 층의 3단 구성으로 하고, RTL 시뮬레이션은 작은 shape, 에뮬레이터는 층 단위, 실리콘은 전체 네트워크를 돌립니다.
  *"Directed corners for the rare paths, seeded random for breadth, and real network layers for realism — each on the platform fast enough to run it."*
- **"±1 허용하면 안 되나?"** — 최종 정확도엔 영향이 거의 없어도, ±1 차이는 **HW와 컴파일러 모델이 다르다**는 신호라 다음 층에서 누적되고, 무엇보다 진짜 버그를 가립니다(TRUNC 버그는 TOL1에서 0개). 기본은 bit-exact, 허용은 명시적 waiver로만.
  *"Bit-exact is the default because a tolerance hides real bugs — the truncation bug here passes a ±1 check completely."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why does bit-exact matter? | 컴파일러 팀은 golden model로 양자화 정확도를 검증하고 배포합니다. HW가 1 LSB라도 다르면 그 검증이 무효가 되고, 층을 지나며 차이가 누적됩니다. 또 bit-exact여야 diff 0이 아닌 모든 것을 버그 후보로 볼 수 있습니다. |
| How do you generate stimuli? | 세 종류: random(seed 기반 LCG/PRBS, 재현 가능), directed corner(−128/127/0/zp, 포화 scale, K 최대, N % TILE != 0), 실제 네트워크 층(실제 가중치·활성값 분포). 실패 시 seed와 shape만 로그하면 재현됩니다. |
| How do you compute TOPS and MAC utilization? | TOPS = 2 × MAC 수 × 클럭(1 MAC = 곱+덧셈 2 ops). 예: 1 600 sparse TOPS는 2:4 sparsity 기준이라 dense로는 800 TOPS, 2 GHz면 800e12 / (2 × 2e9) = 20만 MAC. 실효 utilization = 측정 ops/s ÷ 피크; 작은 층·partial tile·DMA 대기에서 크게 떨어집니다. |
| Memory-bound vs compute-bound? | roofline: 산술 강도 = ops / DRAM bytes. GEMM 6×32×12는 4 608 ops / 648 B ≈ 7 ops/B. batch 1 FC(1×4096×4096)는 약 2 ops/B라 거의 항상 memory bound. 기계 균형점(피크 TOPS ÷ DRAM BW, 예: 800 TOPS / 200 GB/s = 4 000 ops/B)보다 낮으면 BW가 상한입니다. |
| How do you validate DMA + SRAM tiling? | tile 크기와 맞지 않는 shape, SRAM bank 경계를 넘는 버퍼, double buffering(ping-pong) 전환 시점을 directed로 넣고, 출력 비교 외에 guard 영역(버퍼 앞뒤 canary)이 안 덮였는지 확인합니다. |
| How do you choose accumulator width? | `8 + 8 + 1(zp) + log2(K)` 비트가 필요합니다. K = 4 096이면 17 + 12 = 29비트 → 32비트 누산기면 충분, 16비트면 곱 두 개(65 280)만 더해도 넘칩니다(테스트 5c). |
| What about sparsity? | 2:4 구조적 sparsity는 4개 중 2개 0인 가중치를 압축하고 metadata로 위치를 기록해 MAC을 건너뜁니다. 검증은 압축/메타데이터 경로를 따로 directed로 넣고, 결과는 dense golden과 bit-exact 비교합니다. |
| What is a power virus test? | MAC 배열을 최대로 토글시키는 패턴(교대 비트, 최대 utilization 층)으로 최대 전류·di/dt를 만들어 전원망(IR drop)과 열을 검증합니다. 같은 하네스로 돌리고 비교까지 해서 "전원 스트레스 중에도 bit-exact"를 확인합니다. |
| Pre-silicon: FPGA/HAPS vs emulator? | RTL 시뮬레이션 ~Hz–kHz(가시성 최고), 에뮬레이터(Palladium/Veloce) ~1 MHz(가시성 좋음, 비쌈), FPGA 프로토타입(HAPS) ~10–100 MHz(SW 개발·긴 테스트, 디버그 가시성 낮음). 같은 bare-metal 테스트를 세 곳에서 그대로 돌리도록 HAL을 분리해 두는 것이 핵심입니다. |

## 7. 자주 하는 실수

- SRDHM을 `llround((double)a * b / 2^31)`(tie는 0에서 먼 쪽)로 단순화 → 음수 tie에서 1 차이 (테스트 1번 `srdhm(-3, 1<<30) == -1`). RDBPOT를 `(x + half) >> e`로 쓰면 반대로 `rdbpot(-5, 1)`이 −2가 되어 틀립니다
- `a * b`를 32비트로 곱함 → overflow UB, UBSan이 잡음
- zero point를 B 쪽이나 출력 뒤에 적용, 또는 `a_zp`를 빼지 않음 → 테스트 2번 acc 불일치
- relu에서 `act_min = 0`으로 둠 → out_zp가 0이 아니면 틀림 (테스트 2번 relu, out_zp = −100)
- per-channel인데 `mult[0]`만 씀 → 열마다 scale이 달라 대부분 불일치
- 비교기가 TOL1 모드에서 `hist`·`max_abs`까지 버림 → ±1 경향 추적 불가
- 진단 순서를 "크기 먼저"로 둠 → TILE 버그 중 |d| = 1인 원소가 많은 경우 ROUNDING으로 오분류
- tile 배수 shape(N = 16)만 테스트 → partial tile 버그를 영원히 못 봄

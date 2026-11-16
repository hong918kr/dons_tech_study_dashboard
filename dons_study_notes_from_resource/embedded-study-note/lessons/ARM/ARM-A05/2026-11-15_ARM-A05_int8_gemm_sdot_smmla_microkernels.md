---
id: ARM-A05
time: 120분
updated: 2026-11-15
---
# ARM-A05 int8 GEMM 마이크로커널: SDOT·SMMLA

## 학습 목표

- int8 GEMM 을 **패킹(packing) → 마이크로커널(micro-kernel) → 가장자리 처리(edge handling)** 세 층으로 나누고, 각 층이 왜 필요한지 설명한다.
- 같은 GEMM 을 스칼라 → **SDOT 4×4 (.S)** → **SDOT 8×8 (intrinsics)** → **SMMLA 8×8 (.S, FEAT_I8MM)** 로 구현한다. M, N = 1..17, K = 0..37 의 모든 모양(10,982개)을 int64 참조와 비트 단위로 맞춘다.
- int32 누산기의 **오버플로 지평선** K ≤ 131071 을 유도하고, K = 131072 에서 감싸는(wrap) 것을 실제로 재현한다. 버그 4종(UDOT, zip 누락, K 패딩 누락, 가장자리 덮어쓰기)을 주입하고 테스트가 잡는 것을 단언한다.
- SDOT/SMMLA 의 **지연과 처리량을 직접 재서** "이론 피크"를 만든다. 그 피크 대비 GEMM 효율(%)을 계산하고, 4×4 가 왜 40% 근처에서 멈추는지 Little 의 법칙으로 설명한다.
- 양자화 FC 레이어(int8 → int32 누산 → bias·재양자화 → int8)를 만들고, 배치 1(GEMV)에서 타일 설계가 왜 무너지는지 숫자로 보인다.

## 1. 문제

AI 추론의 시간 대부분은 int8 행렬곱(GEMM)이다. 합성곱(im2col 후), FC 레이어, 트랜스포머의 Q·K·V 투영이 모두 GEMM 이다. 그래서 AI 가속기·로보틱스 SoC 면접에서는 "int8 GEMM 커널 짜 봐", "왜 패킹해?", "이론 피크 대비 몇 % 나와?", "누산기 몇 개 필요해?"가 이어서 나온다. NPU 의 systolic array 도 같은 문제를 하드웨어로 푼다. 타일링, int32 누산, 재양자화는 그대로다. 이 레슨은 ARM-M06 의 `dot_s8`(SDOT 로 벡터 하나의 내적)을 **행렬 타일**로 키운다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-weight="bold">GEMM 을 타일로 쪼개기: C 타일(MR×NR) 하나 = A 패널 · B 패널, K 방향은 깊이 KU 블록으로</text> <rect x="200" y="50" width="200" height="70" fill="none" stroke="currentColor"/> <text x="300" y="134" fill="currentColor" text-anchor="middle" font-size="12">B (K×N, int8)</text> <rect x="260" y="50" width="40" height="70" class="f-acc" stroke="currentColor"/> <line x1="260" y1="60" x2="300" y2="60" stroke="currentColor"/> <line x1="260" y1="70" x2="300" y2="70" stroke="currentColor"/> <line x1="260" y1="80" x2="300" y2="80" stroke="currentColor"/> <line x1="260" y1="90" x2="300" y2="90" stroke="currentColor"/>
<line x1="260" y1="100" x2="300" y2="100" stroke="currentColor"/> <line x1="260" y1="110" x2="300" y2="110" stroke="currentColor"/> <text x="280" y="44" fill="currentColor" text-anchor="middle" font-size="12">NR 열</text> <rect x="44" y="150" width="140" height="140" fill="none" stroke="currentColor"/> <text x="114" y="306" fill="currentColor" text-anchor="middle" font-size="12">A (M×K, int8)</text> <rect x="44" y="180" width="140" height="40" class="f-acc" stroke="currentColor"/> <line x1="64" y1="180" x2="64" y2="220" stroke="currentColor"/> <line x1="84" y1="180" x2="84" y2="220" stroke="currentColor"/> <line x1="104" y1="180" x2="104" y2="220" stroke="currentColor"/>
<line x1="124" y1="180" x2="124" y2="220" stroke="currentColor"/> <line x1="144" y1="180" x2="144" y2="220" stroke="currentColor"/> <line x1="164" y1="180" x2="164" y2="220" stroke="currentColor"/> <text x="38" y="204" fill="currentColor" text-anchor="end" font-size="12">MR</text> <text x="114" y="174" fill="currentColor" text-anchor="middle" font-size="12">k-블록 (KU 깊이)</text> <rect x="200" y="150" width="200" height="140" fill="none" stroke="currentColor"/> <text x="300" y="306" fill="currentColor" text-anchor="middle" font-size="12">C (M×N, int32)</text> <rect x="260" y="180" width="40" height="40" class="f-warn" stroke="currentColor"/>
<text x="280" y="204" fill="currentColor" text-anchor="middle" font-size="12">타일</text> <line x1="184" y1="200" x2="260" y2="200" stroke="currentColor" stroke-dasharray="4 3"/> <line x1="280" y1="120" x2="280" y2="180" stroke="currentColor" stroke-dasharray="4 3"/> <text x="420" y="50" fill="currentColor" font-size="12">패널 = A 의 MR 행 / B 의 NR 열</text> <text x="420" y="72" fill="currentColor" font-size="12">  (미리 연속으로 재배치 = packing)</text> <text x="420" y="94" fill="currentColor" font-size="12">k-블록 깊이 KU: SDOT 4, SMMLA 8</text> <text x="420" y="116" fill="currentColor" font-size="12">C 타일 = Σ_b A블록 · B블록</text>
<text x="420" y="138" fill="currentColor" font-size="12">누산기는 K 끝까지 레지스터에 → 저장 1번</text> <text x="420" y="160" fill="currentColor" font-size="12"></text> <text x="420" y="182" fill="currentColor" font-size="12" font-family="monospace">SDOT 4×4  : 누산기 4, 블록당 SDOT 4</text> <text x="420" y="204" fill="currentColor" font-size="12" font-family="monospace">SDOT 8×8  : 누산기 16, 블록당 SDOT 16</text> <text x="420" y="226" fill="currentColor" font-size="12" font-family="monospace">SMMLA 8×8 : 누산기 16, 블록당 SMMLA 16</text> <text x="420" y="248" fill="currentColor" font-size="12"></text> <text x="420" y="270" fill="currentColor" font-size="12">가장자리: 패킹 때 0 채움,</text>
<text x="420" y="292" fill="currentColor" font-size="12">C 에는 유효한 mr×nr 만 복사</text>
</svg>
```

| 커널 | 파일 | 타일 MR×NR | 깊이 KU | 명령 1개 | 누산기 | 블록당 로드 |
|---|---|---|---|---|---|---|
| `gemm_scalar` | starter/solution.c (완성) | 1×1 | 1 | `madd` 1 MAC | 1 (w 레지스터) | B 는 열 방향 stride 접근 |
| `sdot_4x4_kernel` | .S (TODO) | 4×4 | 4 | `sdot` 16 MAC | 4 (v0–v3) | A 16 B + B 16 B → 64 MAC |
| `sdot_8x8_kernel_neon` | .c intrinsics (TODO) | 8×8 | 4 | `sdot` 16 MAC | 16 | A 32 B + B 32 B → 256 MAC |
| `smmla_8x8_kernel` | .S (TODO) | 8×8 | 8 | `smmla` 32 MAC | 16 (v16–v31) | A 64 B + B 64 B → 512 MAC |

### Part A — 패킹 + 커널 3개

- `pack_A(A, M, K, lda, MR, KU, out)`: A 를 MR 행 패널로 자르고, 패널 안은 깊이 KU 블록 순서로 놓는다. 블록 하나 = MR 행 × KU 바이트(행 우선). 범위 밖은 0 이다. `pack_B` 는 같은 방식으로 NR 열 패널을 만든다. 블록 안에서 **열 하나의 KU 바이트가 연속**이 되게 놓는다. SDOT(KU=4)과 SMMLA(KU=8)의 패킹은 공식이 같고 KU 만 다르다.
- `sdot_4x4_kernel` (.S): `sdot vR.4s, vB.16b, vA.4b[R]` (by-element 형식) 4번으로 C 의 4행을 만든다.
- `sdot_8x8_kernel_neon` (intrinsics): `vdotq_laneq_s32` 16번/블록, 누산기 16개.
- `smmla_8x8_kernel` (.S): `smmla` 16번/블록, 결과 2×2 조각을 `zip1/zip2 .2d` 로 행으로 풀어서 저장한다.
- 드라이버 `gemm_packed` / `gemm_s8` 는 완성품이다. 꽉 찬 타일은 C 에 바로 쓰고, 가장자리 타일은 임시 타일에 받아 유효한 부분만 복사한다.

### Part B — 가장자리, 오버플로 지평선, 버그 4종

- 모든 M, N ∈ 1..17, K ∈ 0..37 (10,982 모양) × 커널 3개를 전 범위 랜덤 int8 로 시험한다. C 바깥(ldc 여유 열 + 아래 8행)은 카나리(canary)로 채워, 바깥에 쓰면 잡는다. 패킹 작업 버퍼는 쓰레기 `0x5A` 로 채운 뒤 부른다.
- K = 131071 은 정확하고, K = 131072 (모두 −128)는 `INT32_MIN` 으로 감싼다. 이것을 커널 3개 모두에서 단언한다.
- 버그: ① SDOT 대신 UDOT, ② SMMLA 결과를 zip 없이 저장, ③ K 패딩 누락(B 만 / A·B 둘 다), ④ 가장자리 타일을 C 에 통째로 쓰기. 각 버그가 **어떤 데이터에서 잡히고 어떤 데이터에서는 숨는지**를 단언한다.

### Part C — 양자화 FC 레이어와 피크 대비 효율

- `requant_s8(acc, bias, mult, shift, zp)` 와 `qlinear_s8(...)`: 가중치 W[N][K] 를 `pack_B_nk` 로 **한 번만** 패킹해 두고, 입력 X (M = 배치)만 매번 패킹해 SMMLA GEMM → bias → 곱셈·반올림 시프트 → zp → int8 포화.
- `demo_peak.S` + `demo_bench.c`: SDOT/SMMLA 지연·처리량 → 직접 잰 단일 코어 피크 → 64/256/1024 정육면체 GEMM 의 GOPS 와 효율, 배치 1 GEMV.

## 2. Problem (English)

> Implement an int8 GEMM `C[M×N] (int32) = A[M×K] (int8) · B[K×N] (int8)`, all row-major with leading dimensions `lda, ldb, ldc`.
>
> 1. `void pack_A(const int8_t *A, size_t M, size_t K, size_t lda, size_t MR, size_t KU, int8_t *out)` and `pack_B(...)` — panel-major packing with zero padding: `out[((p*kb + b)*MR + r)*KU + t] = A[(p*MR+r)*lda + b*KU+t]` (0 if out of range), `kb = ceil(K/KU)`. `pack_B` stores `KU` consecutive k-values of each column.
> 2. Micro-kernels with the signature `void kern(const int8_t *Ap, const int8_t *Bp, size_t kb, int32_t *C, size_t ldc)` that **overwrite** a full MR×NR tile: (a) 4×4 with `SDOT` by-element (AArch64 assembly), (b) 8×8 with `vdotq_laneq_s32` intrinsics, (c) 8×8 with `SMMLA` (FEAT_I8MM, assembly). `kb == 0` must produce a zero tile. Do not touch v8–v15.
> 3. Results must equal an int64 reference bit-for-bit for every shape with `K ≤ 131071`. State why that bound is tight.
> 4. `int8_t requant_s8(int32_t acc, int32_t bias, int32_t mult, int shift, int zp)` = clamp(zp + ⌊((acc+bias)·mult + 2^(shift−1)) / 2^shift⌋, −128, 127), `0 ≤ mult < 2^24`, `1 ≤ shift ≤ 40`; and a quantized linear layer using pre-packed weights.
> 5. Measure GOPS for n = 64, 256, 1024 and report efficiency against a peak you derive from your own latency/throughput measurements.
>
> Example: `A = [[1,2],[3,4]]`, `B = [[5,6],[7,8]]` → `C = [[19,22],[43,50]]`. With `K = 131072` and all inputs −128 the int32 result wraps to `INT32_MIN` (the instructions are modular, not saturating).

## 3. 시작 코드

`starter.c` = 테스트 하네스 + 스칼라 기준선·드라이버(완성) + 패킹·intrinsic 커널·Part C 의 TODO. `starter.S` = asm 커널 2개의 TODO(스텁은 C 를 건드리지 않고 `ret`). `shared_bugs.S` = 테스트가 잡아야 하는 asm 버그 2개(TODO 아님).

<!-- code: starter.c -->

<!-- code: starter.S -->

<!-- code: shared_bugs.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S shared_bugs.S -o /tmp/ARM-A05_v && /tmp/ARM-A05_v
```

처음 실행 결과 (실제 출력, 앞과 끝, exit 1):

```text
FAIL starter.c:239  memcmp(out, want, 64) == 0
FAIL starter.c:246  ob[0] == 0 && ob[1] == 10 && ob[2] == 20 && ob[3] == 0 && ob[7] == 0
…
info: sdot4x4(.S)    10982 shapes, 10982 bad
…
info: bug no K-pad  : B only bad 24/24, A&B bad (K%8!=0) 21/21, A&B bad (K%8==0) 3/3
FAIL starter.c:388  onlyB == 0 && both_tail == 21 && both_full == 0
…
FAIL starter.c:439  lbad == 0
6/40 passed
```

처음부터 통과하는 6개는 이렇다. 스칼라 기준선, `packA_bytes` 크기 계산, M = 0 / N = 0 모양, zip 버그 검출(버그 커널은 완성품), 그리고 스텁의 0 이 우연히 맞는 `requant` 2개(0 → 0, −0.5 → 0)다.

관찰·측정용 파일 (테스트와 별개):

```bash
cc -std=c11 -O2 demo_trace.c solution.S -o /tmp/ARM-A05_trace && /tmp/ARM-A05_trace      # 커널 한 블록 손계산 대조 (5장 표)
cc -std=c11 -O2 -DNO_MAIN demo_bench.c solution.c solution.S shared_bugs.S demo_peak.S -o /tmp/ARM-A05_bench
/tmp/ARM-A05_bench            # peak + gemm (약 20초). /tmp/ARM-A05_bench peak 로 피크만 → bench_m6.txt
```

## 4. 힌트

<details><summary>힌트 1 — 패킹은 "커널이 읽는 순서 그대로" 쓰는 것</summary>

커널 루프는 `ld1 {A블록}, [x0], #바이트` / `ld1 {B블록}, [x1], #바이트` 로 **포인터를 앞으로만** 민다. 그러니 패킹은 (패널 → k-블록 → 행 또는 열 → 깊이 t) 순서로 바이트를 하나씩 `*out++` 하면 된다. 범위 밖이면 0 을 쓴다. 건너뛰면 안 된다.
</details>

<details><summary>힌트 2 — SDOT by-element 의 피연산자 순서</summary>

`sdot vd.4s, vn.16b, vm.4b[i]`: `vd.s[j] += Σ_t vn.b[4j+t] · vm.b[4i+t]`. lane j 가 **열**이 되려면 vn 에 B 블록(열 j 의 4바이트가 lane j)을 넣고, vm 에 A 블록을 넣는다. 그러면 `[i]` 가 행을 고른다. intrinsic 은 `vdotq_laneq_s32(acc, b, a, i)` 이고, i 는 컴파일 시간 상수여야 한다(루프 변수 불가 → 매크로로 펼친다).
</details>

<details><summary>힌트 3 — SMMLA 는 "B 를 전치한 2×8"을 받는다</summary>

`smmla vd.4s, vn.16b, vm.16b` = vd(2×2) += vn(2×8) · vm(2×8)ᵀ. vn 의 두 행 = A 의 행 2개, vm 의 두 행 = B 의 **열** 2개다(각 8 깊이). `pack_B` 가 "열 하나의 KU 바이트 연속"이므로 KU = 8 로 패킹하면 열쌍 16바이트가 그대로 vm 이 된다. 결과 lane 순서는 [r0c0, r0c1, r1c0, r1c1] 이다.
</details>

<details><summary>힌트 4 — 저장: zip1/zip2 .2d</summary>

acc[p][0] = [C(2p,0) C(2p,1) | C(2p+1,0) C(2p+1,1)], acc[p][1] = [C(2p,2) C(2p,3) | C(2p+1,2) C(2p+1,3)]. 하위 64비트끼리 붙이면(`zip1 .2d`) 행 2p 의 열 0..3, 상위끼리(`zip2 .2d`) 붙이면 행 2p+1 의 열 0..3 이다.
</details>

<details><summary>힌트 5 — 재양자화의 반올림</summary>

int64 로 `p = (acc + bias) · mult` (최대 |p| < 2^32 · 2^24 = 2^56), `r = (p + 2^(shift−1)) >> shift`. 음수의 `>>` 는 C11 에서 **구현 정의**(6.5.7p5)이고 clang/gcc 는 산술 시프트라 floor 가 된다. 테스트의 참조는 나눗셈 + floor 보정으로 따로 계산한다.
</details>

## 5. 풀이

### 0. 기능 확인 — 이 칩에 SDOT, I8MM 이 있나

```text
$ sysctl hw.optional.arm | grep -E 'DotProd|I8MM|FEAT_SME2:|sme_max_svl'
hw.optional.arm.FEAT_DotProd: 1
hw.optional.arm.FEAT_I8MM: 1
hw.optional.arm.FEAT_SME2: 1
hw.optional.arm.sme_max_svl_b: 64
```

- SDOT/UDOT 은 FEAT_DotProd 다(Armv8.2 선택, v8.4 부터 필수). SMMLA/UMMLA/USMMLA 와 USDOT 은 FEAT_I8MM 이다(v8.2 선택, v8.6 부터 필수). 둘 다 Arm ARM 기준이며 문서 확인을 권장한다.
- Apple clang 의 기본 타깃은 dotprod 를 이미 포함하지만 **i8mm 은 포함하지 않는다**. 실측 결과다. 확장 없이 `smmla` 를 쓰면 `error: instruction requires: i8mm` 이 나오고, 같은 파일의 `sdot` 은 통과한다. intrinsic `vmmlaq_s32` 도 `requires target feature 'i8mm'` 에러가 난다. 그래서 .S 에는 `.arch_extension dotprod` / `.arch_extension i8mm`, C 에는 `__attribute__((target("+dotprod")))` (연습의 `vmmlaq_s32` 는 `target("+i8mm")`) 를 둔다. 플래그 없는 체커 명령으로 빌드하기 위해서다. 리눅스 gcc 의 에러는 `selected processor does not support 'smmla …'` 이다(VM 실측).
- SME2(512비트 SVL, `sme_max_svl_b: 64`)도 있다. int8 외적(outer product) `SMOPA` 는 이 레슨의 다음 단계다(코드 없음).

### Part A — 패킹 레이아웃

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-weight="bold">pack_A 예: A 5×6 (A[i][k] = 10i + k), MR = 4, KU = 4 → 64 바이트 (테스트가 그대로 확인)</text> <text x="57.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k0</text> <text x="91.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k1</text> <text x="125.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k2</text> <text x="159.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k3</text> <text x="193.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k4</text> <text x="227.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k5</text>
<text x="261.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k6</text> <text x="295.0" y="44" fill="currentColor" text-anchor="middle" font-size="12">k7</text> <text x="34" y="66" fill="currentColor" text-anchor="end" font-size="12">i0</text> <rect x="40" y="50" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="57.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="74" y="50" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="91.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">1</text>
<rect x="108" y="50" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="125.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">2</text> <rect x="142" y="50" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="159.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">3</text> <rect x="176" y="50" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="193.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">4</text> <rect x="210" y="50" width="34" height="24" class="f-acc" stroke="currentColor"/>
<text x="227.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">5</text> <rect x="244" y="50" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="261.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="278" y="50" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="295.0" y="66" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <text x="34" y="90" fill="currentColor" text-anchor="end" font-size="12">i1</text>
<rect x="40" y="74" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="57.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">10</text> <rect x="74" y="74" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="91.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">11</text> <rect x="108" y="74" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="125.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">12</text> <rect x="142" y="74" width="34" height="24" class="f-acc" stroke="currentColor"/>
<text x="159.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">13</text> <rect x="176" y="74" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="193.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">14</text> <rect x="210" y="74" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="227.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">15</text> <rect x="244" y="74" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="261.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="278" y="74" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="295.0" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <text x="34" y="114" fill="currentColor" text-anchor="end" font-size="12">i2</text> <rect x="40" y="98" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="57.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">20</text> <rect x="74" y="98" width="34" height="24" class="f-acc" stroke="currentColor"/>
<text x="91.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">21</text> <rect x="108" y="98" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="125.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">22</text> <rect x="142" y="98" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="159.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">23</text> <rect x="176" y="98" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="193.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">24</text>
<rect x="210" y="98" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="227.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">25</text> <rect x="244" y="98" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="261.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="278" y="98" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="295.0" y="114" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text>
<text x="34" y="138" fill="currentColor" text-anchor="end" font-size="12">i3</text> <rect x="40" y="122" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="57.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">30</text> <rect x="74" y="122" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="91.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">31</text> <rect x="108" y="122" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="125.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">32</text>
<rect x="142" y="122" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="159.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">33</text> <rect x="176" y="122" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="193.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">34</text> <rect x="210" y="122" width="34" height="24" class="f-acc" stroke="currentColor"/> <text x="227.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">35</text> <rect x="244" y="122" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="261.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="278" y="122" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="295.0" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <text x="34" y="162" fill="currentColor" text-anchor="end" font-size="12">i4</text> <rect x="40" y="146" width="34" height="24" class="f-ok" stroke="currentColor"/> <text x="57.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">40</text> <rect x="74" y="146" width="34" height="24" class="f-ok" stroke="currentColor"/>
<text x="91.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">41</text> <rect x="108" y="146" width="34" height="24" class="f-ok" stroke="currentColor"/> <text x="125.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">42</text> <rect x="142" y="146" width="34" height="24" class="f-ok" stroke="currentColor"/> <text x="159.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">43</text> <rect x="176" y="146" width="34" height="24" class="f-ok" stroke="currentColor"/> <text x="193.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">44</text>
<rect x="210" y="146" width="34" height="24" class="f-ok" stroke="currentColor"/> <text x="227.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">45</text> <rect x="244" y="146" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="261.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="278" y="146" width="34" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="295.0" y="162" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text>
<text x="40" y="190" fill="currentColor" font-size="12">파랑 = 패널 0, 초록 = 패널 1</text> <text x="40" y="210" fill="currentColor" font-size="12">점선 = K 패딩 (A 에 없음 → 0)</text> <text x="330" y="36" fill="currentColor" font-size="12">out[] (16 바이트 = 4행 × 4깊이 씩)</text> <text x="330" y="56" fill="currentColor" font-size="12">패널0 블록0  (out[0..15])</text> <rect x="330" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="342.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="355" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/>
<text x="367.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">1</text> <rect x="380" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="392.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">2</text> <rect x="405" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="417.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">3</text> <rect x="430" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="442.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">10</text>
<rect x="455" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="467.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">11</text> <rect x="480" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="492.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">12</text> <rect x="505" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="517.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">13</text> <rect x="530" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/>
<text x="542.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">20</text> <rect x="555" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="567.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">21</text> <rect x="580" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="592.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">22</text> <rect x="605" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="617.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">23</text>
<rect x="630" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="642.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">30</text> <rect x="655" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="667.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">31</text> <rect x="680" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="692.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">32</text> <rect x="705" y="60" width="25" height="22" class="f-acc" stroke="currentColor"/>
<text x="717.5" y="76" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">33</text> <text x="330" y="100" fill="currentColor" font-size="12">패널0 블록1  (out[16..31])</text> <rect x="330" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="342.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">4</text> <rect x="355" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="367.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">5</text> <rect x="380" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/>
<text x="392.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="405" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="417.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="430" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="442.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">14</text> <rect x="455" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/>
<text x="467.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">15</text> <rect x="480" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="492.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="505" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="517.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="530" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/>
<text x="542.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">24</text> <rect x="555" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="567.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">25</text> <rect x="580" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="592.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="605" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/>
<text x="617.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="630" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="642.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">34</text> <rect x="655" y="104" width="25" height="22" class="f-acc" stroke="currentColor"/> <text x="667.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">35</text> <rect x="680" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/>
<text x="692.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="705" y="104" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="717.5" y="120" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <text x="330" y="144" fill="currentColor" font-size="12">패널1 블록0  (out[32..47])</text> <rect x="330" y="148" width="25" height="22" class="f-ok" stroke="currentColor"/> <text x="342.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">40</text> <rect x="355" y="148" width="25" height="22" class="f-ok" stroke="currentColor"/>
<text x="367.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">41</text> <rect x="380" y="148" width="25" height="22" class="f-ok" stroke="currentColor"/> <text x="392.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">42</text> <rect x="405" y="148" width="25" height="22" class="f-ok" stroke="currentColor"/> <text x="417.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">43</text> <rect x="430" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="442.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text>
<rect x="455" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="467.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="480" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="492.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="505" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="517.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="530" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/>
<text x="542.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="555" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="567.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="580" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="592.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="605" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="617.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text>
<rect x="630" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="642.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="655" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="667.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="680" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="692.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="705" y="148" width="25" height="22" class="f-mut" stroke="currentColor"/>
<text x="717.5" y="164" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <text x="330" y="188" fill="currentColor" font-size="12">패널1 블록1  (out[48..63])</text> <rect x="330" y="192" width="25" height="22" class="f-ok" stroke="currentColor"/> <text x="342.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">44</text> <rect x="355" y="192" width="25" height="22" class="f-ok" stroke="currentColor"/> <text x="367.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">45</text> <rect x="380" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/>
<text x="392.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="405" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="417.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="430" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="442.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="455" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="467.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text>
<rect x="480" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="492.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="505" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="517.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="530" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="542.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="555" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/>
<text x="567.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="580" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="592.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="605" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="617.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="630" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="642.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text>
<rect x="655" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="667.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="680" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="692.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="705" y="192" width="25" height="22" class="f-mut" stroke="currentColor"/> <text x="717.5" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <text x="330" y="244" fill="currentColor" font-size="12">회색 = 패딩 0. 4바이트마다 같은 행 → sdot 의 .4b[r] 한 칸</text>
</svg>
```

- 블록 하나 = MR × KU 바이트 = 레지스터 하나(4×4)다. SDOT by-element 의 `.4b[r]` 가 4바이트씩 행을 고르므로, **행 r 의 4바이트가 연속**이어야 한다. 그림의 `out[0..15]` 가 그대로 `v4` 가 된다.
- 패킹 비용은 O(MK + KN), 계산은 O(MNK)다. 원소 수로 보면 패킹/계산 비는 약 2/n 이다(n = 1024 면 1/512). 그래도 n = 64 에서는 크게 보인다(아래 측정). 가중치(B)는 모델 로드 때 **한 번** 패킹하고, 활성값(A)만 매번 패킹한다(Part C).
- 패킹이 주는 것: 커널 루프의 로드가 전부 **연속·순차**가 된다(stride·TLB·캐시 라인 낭비 없음). 0 패딩 덕분에 커널에 꼬리 처리가 없다. 정렬도 패널 단위로 맞출 수 있다.
- 테스트는 이 그림의 64바이트를 `want[]` 로 그대로 비교한다. 랜덤 300개 모양에서는 "읽는 쪽" 공식(`P[(((i/MR)·kb + k/KU)·MR + i%MR)·KU + k%KU]`)으로 되짚는다. `pack_B_nk(W)` 가 `pack_B(Wᵀ)` 와 바이트 단위로 같은지도 본다.

### Part A — SDOT 4×4 한 블록 따라가기

위 그림의 `패널0 블록0` 을 A 블록으로 쓴다. B 블록은 열 0..3 의 4바이트를 각각 `[1,1,1,1]`, `[2,2,2,2]`, `[3,3,3,3]`, `[−1,−1,−1,−1]` 로 두고 `sdot_4x4_kernel(Ap, Bp, kb=1, C, 4)` 를 부른다 (`demo_trace.c`, 실제 출력):

| 명령 | 고르는 A 4바이트 | 계산 (열 j = (j+1)·Σ, 열 3 = −Σ) | 결과 (실측) |
|---|---|---|---|
| `sdot v0.4s, v5.16b, v4.4b[0]` | 0 1 2 3 (Σ = 6) | 6·[1, 2, 3, −1] | `row 0: 6 12 18 -6` |
| `sdot v1.4s, v5.16b, v4.4b[1]` | 10 11 12 13 (Σ = 46) | 46·[1, 2, 3, −1] | `row 1: 46 92 138 -46` |
| `sdot v2.4s, v5.16b, v4.4b[2]` | 20..23 (Σ = 86) | | `row 2: 86 172 258 -86` |
| `sdot v3.4s, v5.16b, v4.4b[3]` | 30..33 (Σ = 126) | | `row 3: 126 252 378 -126` |

- 블록 하나에 SDOT 4개(64 MAC)와 로드 2개(32 B)가 든다. **MAC 2개당 1바이트**를 읽는다. 8×8 은 블록당 SDOT 16개(256 MAC)에 로드 64 B 라서 MAC 4개당 1바이트다. 타일이 클수록 로드 대비 연산이 늘어난다. 레지스터 하나에 든 A·B 값을 더 많은 곱셈에 재사용하기 때문이다.
- 실제 기계어 (`/usr/bin/objdump -d --mattr=+i8mm,+dotprod`, Apple 문법):

```text
      14: 4cdf7004     	ld1.16b	{ v4 }, [x0], #16
      18: 4cdf7025     	ld1.16b	{ v5 }, [x1], #16
      1c: 4f84e0a0     	sdot	v0, v5, v4[0]
      20: 4fa4e0a1     	sdot	v1, v5, v4[1]
      24: 4f84e8a2     	sdot	v2, v5, v4[2]
      28: 4fa4e8a3     	sdot	v3, v5, v4[3]
```

lane 번호는 인코딩의 H:L 비트(bit 11, bit 21)에 들어간다. `4f84e0a0` → `4fa4e0a1` 은 bit 21(L)이 바뀌고, `[2]` 는 bit 11(H)이 켜진다(`e0` → `e8`). 덧붙여, 이 Mac 의 objdump 는 `_sdot_4x4_kernel` 을 같은 주소의 `ltmp0` 라벨 이름으로 보여준다.

### Part A — SMMLA: 2×8 · (2×8)ᵀ

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-weight="bold">SMMLA v16.4s, v0.16b, v4.16b : 2×8 · (2×8)ᵀ = 2×2 int32 누적, 그리고 zip 으로 행 풀기</text> <text x="20" y="46" fill="currentColor" font-size="12">v0 = A 행쌍 p (바이트 0–7 = 행 2p, 8–15 = 행 2p+1)</text> <rect x="20" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="34.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a0</text> <rect x="48" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="62.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a1</text>
<rect x="76" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="90.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a2</text> <rect x="104" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="118.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a3</text> <rect x="132" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="146.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a4</text> <rect x="160" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/>
<text x="174.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a5</text> <rect x="188" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="202.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a6</text> <rect x="216" y="54" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="230.0" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">a7</text> <rect x="20" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="34.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b0</text>
<rect x="48" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="62.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b1</text> <rect x="76" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="90.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b2</text> <rect x="104" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="118.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b3</text> <rect x="132" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/>
<text x="146.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b4</text> <rect x="160" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="174.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b5</text> <rect x="188" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="202.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b6</text> <rect x="216" y="78" width="28" height="24" class="f-acc" stroke="currentColor"/> <text x="230.0" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">b7</text>
<text x="20" y="126" fill="currentColor" font-size="12">v4 = B 열쌍 q (바이트 0–7 = 열 2q, 8–15 = 열 2q+1)</text> <rect x="20" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="34.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c0</text> <rect x="48" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="62.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c1</text> <rect x="76" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="90.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c2</text>
<rect x="104" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="118.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c3</text> <rect x="132" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="146.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c4</text> <rect x="160" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="174.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c5</text> <rect x="188" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/>
<text x="202.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c6</text> <rect x="216" y="134" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="230.0" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">c7</text> <rect x="20" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="34.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d0</text> <rect x="48" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="62.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d1</text>
<rect x="76" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="90.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d2</text> <rect x="104" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="118.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d3</text> <rect x="132" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="146.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d4</text> <rect x="160" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/>
<text x="174.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d5</text> <rect x="188" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="202.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d6</text> <rect x="216" y="158" width="28" height="24" class="f-ok" stroke="currentColor"/> <text x="230.0" y="174" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">d7</text> <text x="320" y="56" fill="currentColor" font-size="12">v16 (2×2, 행 우선) += </text> <rect x="320" y="70" width="110" height="44" class="f-warn" stroke="currentColor"/>
<text x="375" y="88" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">Σ a·c</text> <text x="375" y="106" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(2p,2q)</text> <rect x="430" y="70" width="110" height="44" class="f-warn" stroke="currentColor"/> <text x="485" y="88" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">Σ a·d</text> <text x="485" y="106" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(2p,2q+1)</text> <rect x="320" y="114" width="110" height="44" class="f-warn" stroke="currentColor"/>
<text x="375" y="132" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">Σ b·c</text> <text x="375" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(2p+1,2q)</text> <rect x="430" y="114" width="110" height="44" class="f-warn" stroke="currentColor"/> <text x="485" y="132" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">Σ b·d</text> <text x="485" y="150" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(2p+1,2q+1)</text> <text x="550" y="88" fill="currentColor" font-size="12">lane 0, 1</text>
<text x="550" y="132" fill="currentColor" font-size="12">lane 2, 3</text> <text x="320" y="180" fill="currentColor" font-size="12">명령 1개 = 32 MAC (SDOT 은 16 MAC)</text> <text x="20" y="210" fill="currentColor" font-size="12">저장: acc[p][0] = v16, acc[p][1] = v17 (열쌍 0, 1) → 행 2p 와 2p+1 의 열 0..3</text> <text x="82" y="238" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">v16</text> <rect x="90" y="222" width="64" height="24" class="f-warn" stroke="currentColor"/> <text x="122" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,0)</text> <rect x="154" y="222" width="64" height="24" class="f-warn" stroke="currentColor"/>
<text x="186" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,1)</text> <rect x="218" y="222" width="64" height="24" class="f-warn" stroke="currentColor"/> <text x="250" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,0)</text> <rect x="282" y="222" width="64" height="24" class="f-warn" stroke="currentColor"/> <text x="314" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,1)</text> <text x="82" y="270" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">v17</text>
<rect x="90" y="254" width="64" height="24" class="f-warn" stroke="currentColor"/> <text x="122" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,2)</text> <rect x="154" y="254" width="64" height="24" class="f-warn" stroke="currentColor"/> <text x="186" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,3)</text> <rect x="218" y="254" width="64" height="24" class="f-warn" stroke="currentColor"/> <text x="250" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,2)</text> <rect x="282" y="254" width="64" height="24" class="f-warn" stroke="currentColor"/>
<text x="314" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,3)</text> <text x="462" y="238" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">zip1.2d</text> <rect x="470" y="222" width="64" height="24" class="f-ok" stroke="currentColor"/> <text x="502" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,0)</text> <rect x="534" y="222" width="64" height="24" class="f-ok" stroke="currentColor"/> <text x="566" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,1)</text>
<rect x="598" y="222" width="64" height="24" class="f-ok" stroke="currentColor"/> <text x="630" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,2)</text> <rect x="662" y="222" width="64" height="24" class="f-ok" stroke="currentColor"/> <text x="694" y="238" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r,3)</text> <text x="462" y="270" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">zip2.2d</text> <rect x="470" y="254" width="64" height="24" class="f-ok" stroke="currentColor"/>
<text x="502" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,0)</text> <rect x="534" y="254" width="64" height="24" class="f-ok" stroke="currentColor"/> <text x="566" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,1)</text> <rect x="598" y="254" width="64" height="24" class="f-ok" stroke="currentColor"/> <text x="630" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,2)</text> <rect x="662" y="254" width="64" height="24" class="f-ok" stroke="currentColor"/>
<text x="694" y="270" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">C(r+1,3)</text> <line x1="352" y1="250" x2="400" y2="250" stroke="currentColor"/> <text x="20" y="296" fill="currentColor" font-size="12">r = 2p. zip1 = 두 레지스터의 하위 64비트끼리, zip2 = 상위 64비트끼리.</text> <text x="20" y="314" fill="currentColor" font-size="12">이 단계를 빼면 shared_bugs.S 의 bug_smmla_nozip: 8×8 타일 64개 중 48개가 틀린다 (실측).</text>
</svg>
```

- SMMLA 한 개는 32 MAC 으로 SDOT(16 MAC)의 두 배다. 그 대가로 **K 를 8 단위**로 패딩하고, **M 과 N 을 2 단위**로 묶는다. 출력이 2×2 조각이라서 저장 전에 zip 이 필요하다.
- 8×8 타일 = 행쌍 4 × 열쌍 4 = 누산기 16개(v16–v31)다. 블록마다 `ld1 {v0–v3}` (A 64 B), `ld1 {v4–v7}` (B 64 B) 뒤에 SMMLA 16개가 온다. 이러면 레지스터 32개가 모두 쓰인다: 누산기 16, A 4, B 4, 그리고 v8–v15 8개는 일부러 비워 둔다. v8–v15 는 하위 64비트가 callee-saved 라서, 쓰면 프롤로그에서 `stp d8, d9, …` 로 저장해야 한다. 저장 단계에서는 v0–v3 을 임시로 재사용한다.
- 단위 테스트(`test_kernel_unit`)에 같은 아이디어가 있다. 아래는 `demo_trace.c` 실측이다. A 행 i 의 바이트를 모두 i+1, B 열 j 의 처음 3바이트를 j+1(나머지 0)로 두면 C(r,c) = 3(r+1)(c+1) 이다:

```text
smmla row 0: 3 6 9 12 15 18 21 24
smmla row 1: 6 12 18 24 30 36 42 48
```

- Mac objdump 는 `--mattr=+i8mm` 을 줘도 `4e84a410  smmla` 처럼 **피연산자 없이** 찍는다(실측, CommandLineTools llvm-objdump 도 같음). VM 의 GNU objdump 는 `smmla v16.4s, v0.16b, v4.16b` 로 정상 표시한다. 인코딩 `0x4e84a410` = `0100 1110 100 | Rm=00100 | 1010 01 | Rn=00000 | Rd=10000` 이다.

### Part A — intrinsics 8×8: 컴파일러가 만든 것

`ROW8` 매크로 16번이 그대로 SDOT 16개가 되었는지, 누산기가 스필(spill)되지 않았는지 `clang -O2 -S` 로 확인했다 (실제 출력 일부):

```text
LBB4_1:
	ldp	q24, q25, [x0], #32
	ldp	q26, q27, [x1], #32
	sdot	v23, v26, v24[0]
	sdot	v22, v27, v24[0]
	sdot	v21, v26, v24[1]
	…                                  (16개)
	sdot	v0, v27, v25[3]
	subs	x2, x2, #1
	b.ne	LBB4_1
```

clang 은 누산기를 v0–v7, v16–v23 에 두고 v8–v15 를 피했다. 로드는 `ldp q` 2개로 합쳤다. 손 asm 과 사실상 같은 루프다. `int32x4_t acc[8][2]` 배열도 완전히 레지스터로 올라갔다.

### 스칼라 기준선이 정말 스칼라인가

`#pragma clang loop vectorize(disable) interleave(disable)` 를 단 내부 루프의 -O2 결과다 (실제):

```text
LBB0_8:
	ldrsb	w17, [x14], #1
	ldrsb	w19, [x15]
	madd	w13, w19, w17, w13
	add	x15, x15, x6
	subs	x16, x16, #1
	b.ne	LBB0_8
```

B 를 `x6 = ldb` 간격으로 읽는다(열 방향). n = 1024 에서는 원소마다 다른 캐시 라인을 건드린다. 그래서 스칼라가 64 일 때 4.2 GOPS 에서 1024 일 때 0.9 GOPS 로 떨어진다. 패킹이 없는 코드의 전형이다. gcc 에는 이 pragma 가 없어서 `optimize("no-tree-vectorize")` 로 분기했다(VM 빌드에서 `-Werror=unknown-pragmas` 로 실제로 걸렸다).

### Part B — 오버플로 지평선

| 항목 | 값 | 비고 |
|---|---|---|
| 최악의 곱 (양) | (−128)·(−128) = 16384 = 2^14 | 모든 k 에서 같은 부호로 쌓임 |
| 최악의 곱 (음) | (−128)·127 = −16256 | 음 방향 한계 2^31 / 16256 = 132104 → 더 느슨함 |
| 지평선 K | ⌊(2^31 − 1) / 16384⌋ = **131071** | 131071 · 16384 = 2147467264 ≤ 2147483647 |
| K = 131072 | 2^17 · 2^14 = 2^31 | int32 로는 INT32_MIN (감쌈) |
| SDOT / SMMLA 의미 | 4개 / 8개 곱의 합을 lane 에 **모듈러 덧셈** | 포화(saturating) 아님 — 실측 |

- 감쌈은 명령마다 모듈러 2^32 로 일어난다. 그래서 최종 결과는 int64 참조를 2^32 로 줄인 값과 같다. 테스트는 커널 3개 모두에서 `C[0] == INT32_MIN`, 아직 안 넘친 열 1 은 `2147451008` 임을 단언한다. 연습 1 은 `INT32_MAX + 8·16384` 를 한 명령으로 넣어 `-2147352577` 이 되는 것을 하드웨어와 C 모델 양쪽에서 확인한다.
- 실무: K 가 131071 을 넘을 수 있으면 K 를 잘라 int32 결과를 int64 로 모은다(연습 2: K = 300000 정확). 아니면 `SADALP` 로 주기적으로 int64 로 비운다(ARM-M06 의 `dot_s8_long`). u8×s8(USMMLA)이면 최악 |곱| = 255·128 = 32640 이라 지평선이 **65793** 으로 절반이 된다(연습 3).

### Part B — 버그 4종: 언제 잡히고 언제 숨나 (실제 출력)

```text
info: bug udot      : 0..127 data bad shapes 0/20, full-range bad 20/20
info: bug nozip     : 8x8x16 wrong elements 48/64
info: bug no K-pad  : B only bad 0/24, A&B bad (K%8!=0) 21/21, A&B bad (K%8==0) 0/3
info: bug no edge   : 16x8 bad 0, 13x5 bad 63000 (>=1000 = canary hit)
```

| 버그 | 숨는 조건 | 잡는 조건 | 교훈 |
|---|---|---|---|
| UDOT (부호 없는 곱) | 데이터가 0..127 이면 결과 동일 (20/20 통과) | 음수 포함 (20/20 실패) | 테스트 데이터는 **전 범위** (−128 포함) |
| SMMLA zip 누락 | — | 64개 중 48개 틀림 | 행마다 2개(짝수 행 열 0–1, 홀수 행 열 6–7)는 우연히 맞는다 — 몇 칸만 보는 테스트는 놓친다 |
| K 패딩: B 만 누락 | **항상 맞는다** (A 의 0 × 쓰레기 = 0) | — | int8 은 한쪽 0 이면 충분. float 은 0 × NaN = NaN 이라 안 된다 |
| K 패딩: A·B 둘 다 누락 | K % 8 == 0 (3/3 통과) | K % 8 ≠ 0 (21/21 실패) | K 를 **모든 나머지**로 시험해야 한다 |
| 가장자리 통째로 쓰기 | M, N 이 타일 배수 (16×8 통과) | 13×5 → 카나리 63개 파손 | 값만 비교하면 못 잡는다 → C 바깥 카나리 |

**돌연변이(mutant) 검사** — 정답 코드를 scratch 에 복사해 한 줄씩 망가뜨리고 하네스가 FAIL 하는지 확인했다(레슨 디렉터리에는 남기지 않음):

| mutant | 바꾼 것 | 결과 | 잡은 테스트 |
|---|---|---|---|
| M1 | `pack_A` 패딩을 0 → 1 | 37/40 | 패킹 단위 테스트, B-only 버그 검사. **GEMM 10,982 모양은 전부 통과**(B 의 0 이 가림) |
| M2 | SDOT 4×4 행 0 이 `.4b[1]` 사용 | 37/40 | 단위, 전 모양(10,693 틀림 — K = 0 인 289개만 통과), stride. 지평선은 통과(모든 행이 같은 −128) |
| M3 | SMMLA 행 2 를 zip2 로 | 35/40 | 단위, 전 모양(9,434), stride, 버그 검사, 레이어 |
| M4 | 반올림 상수 `1<<shift` | 36/40 | requant 이름 붙은 경우, 랜덤 20만, 레이어 |
| M5 | intrinsics 행 5 lane 0 | 36/40 | 단위, 전 모양(7,548), stride, 가장자리 버그 검사(SDOT 8×8 사용) |
| M6 | `pack_B_nk` 가 마지막 k 를 버림 | 38/40 | `pack_B_nk == pack_B(Wᵀ)`, 레이어 |
| M7 | SMMLA 마지막 누산기의 B 레지스터 | 36/40 | 단위, 전 모양(4,477), stride, 레이어 |

M1 이 핵심이다. **결과 비교만으로는 못 잡는 버그가 있다**. 레이아웃 계약은 패킹 단위 테스트가 따로 지켜야 한다.

### Part C — 재양자화와 FC 레이어

```text
y = clamp( zp + ⌊ ((acc + bias)·mult + 2^(shift−1)) / 2^shift ⌋ , −128, 127 )
```

| 입력 (acc, bias, mult, shift, zp) | 실수값 | 결과 | 확인 |
|---|---|---|---|
| (1, 0, 1, 1, 0) | 0.5 | 1 | 반 → +∞ 쪽 |
| (−1, 0, 1, 1, 0) | −0.5 | 0 | "0 에서 먼 쪽" 반올림이었다면 −1 |
| (−3, 0, 1, 1, 0) | −1.5 | −1 | |
| (100, 28, 3, 2, −10) | 128·3/4 = 96 → 96 − 10 | 86 | zp 더하기 |
| (±10^6, 0, 2^20, 20, 0) | ±10^6 | 127 / −128 | 포화 |

- 이 레슨의 반올림 규칙은 **floor(x + 1/2)** 하나다. 프레임워크마다 다르다. TFLite 는 Q31 배수 + "SaturatingRoundingDoublingHighMul" + round-half-away 계열을 쓴다고 알고 있다(문서 확인 권장). 실제 모델과 비트까지 맞추려면 그 파이프라인을 그대로 옮겨야 한다. 고정소수점 포화·반올림 명령은 ARM-A07 에서 다룬다.
- `qlinear_s8` 는 4줄이다: X 패킹 → `gemm_packed(&KD_MMLA8, …, Wp, acc)` → 원소마다 `requant_s8`. 가중치 패킹(`pack_B_nk`)은 레이어 밖, 모델 로드 시점이다. 테스트: M ∈ {1, 3, 8, 13} × 랜덤 K ≤ 200, N ≤ 40 을 40번 돌려 int64 참조 + 별도 floor 나눗셈 requant 와 **전부 일치**(`lbad == 0`). requant 단독은 랜덤 20만 개로 확인했다.

### Part C — 이론 피크를 직접 잰다 (demo_peak.S)

남의 "SDOT 몇 개/사이클" 숫자 대신 직접 잰다. `demo_peak.S` 의 루프 하나는 같은 명령 16개다. ① **1 사슬**(모두 v16 에 누적) = 지연, ② **누산기 4개** 돌려쓰기 = 4×4 커널과 같은 상황, ③ **누산기 16개** = 처리량. 샘플마다 앞뒤로 `add` 사슬(1 사이클/명령 가정)을 재서 ns 를 사이클로 바꾼다(ARM-M03 방식). 앞뒤 CPU 번호가 같은 샘플만 코어 종류별로 묶는다(이 M6: 0–5 E, 6–7 Super, 8–11 Perf — ARCH-M08 매핑). 101 샘플 × 5회 실행, **load average 22–27** (다른 작성 에이전트 10개 동시 실행). 원본은 `bench_m6.txt` 다.

```svg
<svg viewBox="0 0 740 334" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-weight="bold">실측 (Perf 코어, 5회 실행 중앙값): 명령 1개당 사이클 — 누산기 수가 처리량을 정한다</text> <text x="172" y="56" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">SDOT  1 사슬 (지연)</text> <rect x="180" y="40" width="225.0" height="22" class="f-warn" stroke="currentColor"/> <text x="413.0" y="56" fill="currentColor" font-size="12">3 cyc</text> <text x="172" y="88" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">SDOT  누산기 4</text> <rect x="180" y="72" width="56.2" height="22" class="f-acc" stroke="currentColor"/> <text x="244.25" y="88" fill="currentColor" font-size="12">0.75 cyc</text>
<text x="172" y="120" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">SDOT  누산기 16</text> <rect x="180" y="104" width="25.1" height="22" class="f-ok" stroke="currentColor"/> <text x="213.05" y="120" fill="currentColor" font-size="12">0.334 cyc</text> <text x="172" y="152" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">SMMLA 1 사슬 (지연)</text> <rect x="180" y="136" width="452.2" height="22" class="f-warn" stroke="currentColor"/> <text x="640.25" y="152" fill="currentColor" font-size="12">6.03 cyc</text> <text x="172" y="184" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">SMMLA 누산기 4</text>
<rect x="180" y="168" width="113.2" height="22" class="f-acc" stroke="currentColor"/> <text x="301.25" y="184" fill="currentColor" font-size="12">1.51 cyc</text> <text x="172" y="216" fill="currentColor" text-anchor="end" font-size="12" font-family="monospace">SMMLA 누산기 16</text> <rect x="180" y="200" width="52.5" height="22" class="f-ok" stroke="currentColor"/> <text x="240.5" y="216" fill="currentColor" font-size="12">0.7 cyc</text> <line x1="180" y1="232" x2="180" y2="238" stroke="currentColor"/> <text x="180" y="252" fill="currentColor" text-anchor="middle" font-size="12">0</text> <line x1="255" y1="232" x2="255" y2="238" stroke="currentColor"/>
<text x="255" y="252" fill="currentColor" text-anchor="middle" font-size="12">1</text> <line x1="330" y1="232" x2="330" y2="238" stroke="currentColor"/> <text x="330" y="252" fill="currentColor" text-anchor="middle" font-size="12">2</text> <line x1="405" y1="232" x2="405" y2="238" stroke="currentColor"/> <text x="405" y="252" fill="currentColor" text-anchor="middle" font-size="12">3</text> <line x1="480" y1="232" x2="480" y2="238" stroke="currentColor"/> <text x="480" y="252" fill="currentColor" text-anchor="middle" font-size="12">4</text> <line x1="555" y1="232" x2="555" y2="238" stroke="currentColor"/> <text x="555" y="252" fill="currentColor" text-anchor="middle" font-size="12">5</text>
<line x1="630" y1="232" x2="630" y2="238" stroke="currentColor"/> <text x="630" y="252" fill="currentColor" text-anchor="middle" font-size="12">6</text> <line x1="705" y1="232" x2="705" y2="238" stroke="currentColor"/> <text x="705" y="252" fill="currentColor" text-anchor="middle" font-size="12">7</text> <line x1="180" y1="232" x2="705" y2="232" stroke="currentColor"/> <text x="10" y="280" fill="currentColor" font-size="12">누산기 4개: 지연 3 / 4 = 0.75, 6 / 4 = 1.5 → 정확히 지연에 묶인다 (Little 의 법칙).</text> <text x="10" y="300" fill="currentColor" font-size="12">필요 누산기 ≥ 지연 × 처리량 = 3 × 3 = 9 (SDOT), 6 × 1.5 = 9 (SMMLA) → 8×8 타일(16개)은 충분.</text>
<text x="10" y="320" fill="currentColor" font-size="12">ops/사이클: SDOT 32 / 0.334 ≈ 96, SMMLA 64 / 0.70 ≈ 92 → 피크는 거의 같다.</text>
</svg>
```

| Perf 코어 (5회 중앙값) | SDOT | SMMLA |
|---|---|---|
| 지연 (1 사슬) | **3.00** 사이클 | **6.03** 사이클 |
| 누산기 4개 | 0.750 cyc/명령 (= 3/4) | 1.509 (= 6/4) |
| 누산기 16개 (처리량) | **0.334** → 명령 3개/사이클 | **0.70** (0.67–0.74) → 약 1.5개/사이클 |
| ops/사이클 (MAC = 2 ops) | 32 × 3 = **96** | 64 × 1.43–1.5 = **92–96** |
| 클럭 (add 사슬) | 3.64–3.84 GHz, 중앙값 3.76 | |
| 단일 코어 피크 (측정값으로 계산) | 32 / 0.334 × 3.76 = **361 GOPS** | 64 / 0.70 × 3.76 = **343 GOPS** |

- **SMMLA 는 명령당 일이 2배지만 처리량이 절반이다.** 그래서 이 Perf 코어에서는 SDOT 과 피크가 같다(측정). SMMLA 의 이득은 피크가 아니라 다른 데 있다. 같은 일에 명령 수가 절반이다. 로드 바이트당 MAC 은 타일이 정한다(8×8 이면 둘 다 4 MAC/B). 다른 코어(예: Neoverse V1/V2, Cortex-X 계열)는 SMMLA 처리량이 SDOT 과 같아 2배 이득이라는 자료를 본 기억이 있지만, 확인하지 못했다(Software Optimization Guide 확인 권장).
- Super 코어로 묶인 샘플은 표본이 작고(n = 1–31) 흔들렸다. 그래서 표에는 넣지 않았다. 앞선 실행들(`bench_m6.txt` 아래쪽 "early run")에서는 SDOT 0.25 cyc/명령(4개/사이클) 근처가 가장 자주 나왔고, SMMLA 는 0.53–0.57 이었다. 최종 5회에서는 SDOT 0.12–0.23 (n ≤ 6) 처럼 믿기 어려운 값도 섞였다. E 코어의 사이클 비는 Perf 와 같았고(SDOT 0.331–0.335), 클럭은 1.9–2.6 GHz 였다.
- 같은 칩에서도 코어 종류마다 피크가 다르다. "이론 피크"를 말할 때는 **어느 코어, 어느 클럭, 명령 몇 개/사이클**을 같이 말해야 한다. macOS 는 코어 고정이 안 된다. 그래서 ns 중앙값(코어가 섞임)이 아니라 **Perf 묶음의 사이클 비 × Perf 클럭**으로 피크를 계산한다. 첫 버전은 ns 중앙값을 썼다가 SMMLA 피크가 220 GOPS 로 나와 효율이 137% 가 되었다(`bench_m6.txt` 의 early run 2). 그래서 바꿨다.

### Part C — GEMM GOPS 와 피크 대비 효율 (5회 실행의 중앙값)

GOPS = 2n³ / 시간. ".pk" = 미리 패킹(커널 + 가장자리만), 나머지 = 패킹 포함. 효율 = .pk ÷ 위 피크(SDOT 361, SMMLA 343).

| n | scalar | sdot4 | sdot4.pk | sdot8 | sdot8.pk | smmla8 | mmla8.pk | 효율 sdot4 / sdot8 / mmla8 |
|---|---|---|---|---|---|---|---|---|
| 64 | 4.2 | 56.3 | 193.4 | 57.1 | 272.5 | 62.6 | 260.5 | 54% / 76% / 76% |
| 256 | 4.1 | 114.9 | 167.3 | 131.5 | 233.9 | 164.2 | 249.2 | 46% / 65% / 73% |
| 1024 | 0.9 | 115.5 | 138.9 | 238.3 | 247.1 | 223.1 | 249.7 | 39% / 69% / 73% |

실행 간 흔들림이 크다. 예를 들어 n = 1024 의 sdot8.pk 는 203–329 GOPS 였다. 그래서 숫자 하나보다 **비율과 경향**을 읽어야 한다.

1. **4×4 는 지연에 묶인다.** 누산기 4개 → SDOT 4/3 개/사이클 = 피크의 44% 가 상한이다(`tput_sdot4` 실측 157–160 GOPS 와 일치). n = 1024 에서 39% 로 그 상한 근처다. n = 64 에서 54% 로 상한을 넘는 것은, K = 64 면 사슬이 16 블록으로 짧아서 다음 타일의 독립 사슬이 비순차 실행(OoO) 창 안에서 겹치기 때문으로 보인다(추정).
2. **8×8 (누산기 16 ≥ 필요 9)은 65–76%** 다. 남은 손실은 로드·루프 분기·저장, 가장자리 타일 복사, 그리고 n = 1024 에서 캐시 블로킹 없이 A 패널 전체(1 MB)를 B 패널마다 다시 읽는 것이다(Q7). 부하 22–27 의 공유 머신이라는 점도 있다.
3. **패킹 비용**: n = 64 에서 패킹 포함 GOPS 는 미리 패킹의 약 1/3–1/5 다. 바이트 단위 분기 있는 패킹 루프가 계산(0.5 M ops)보다 오래 걸린다. n = 1024 에서는 차이가 4–17% 로 줄어든다. 가중치를 미리 패킹하는 이유다.
4. **스칼라는 미리 패킹 8×8 보다 65–275배 느리다.** 벡터화보다 B 의 열 방향 접근이 n = 1024 에서 더 치명적이다.

**배치 1 (GEMV, M = 1, N = K = 1024, 미리 패킹)**: sdot4 31.6, sdot8 31.4, smmla8 29.8 GOPS. 타일 행 MR 개 중 1개만 쓸모 있으니 유효 일은 1/4, 1/8 이다. 미리 패킹 GEMM 의 1024 숫자에 곱하면 139/4 ≈ 35, 247/8 ≈ 31, 250/8 ≈ 31 로 측정과 맞는다. 산술 강도(연습 4)도 GEMV 는 바이트당 2 ops(1024³ GEMM 은 341)라 메모리에 묶이는 영역이다. LLM 디코드처럼 배치가 1 이면 이 설계 자체를 바꿔야 한다(Q8).

### 리눅스/ELF 에서도 같은 파일

`.S` 는 `#ifdef __APPLE__` 의 `SYM/FTYPE/FSIZE` 매크로로 Mach-O(`_name`)와 ELF(`.type`, `.size`)를 모두 지원한다. Lima VM(aarch64 Linux, `/proc/cpuinfo` 에 `asimddp`, `i8mm`)에서 실제로 빌드·실행했다:

```text
gcc 15.2.0   : solution 40/40 passed, practice 14/14 passed
clang 21.1.8 : solution 40/40 passed, practice 14/14 passed
readelf -sW solution.o : sdot_4x4_kernel FUNC size 76, smmla_8x8_kernel FUNC size 252
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o /tmp/ARM-A05_v && /tmp/ARM-A05_v
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c solution.S shared_bugs.S -o /tmp/ARM-A05_asan && /tmp/ARM-A05_asan
```

실제 출력 (-O2, 약 0.1 초. ASan/UBSan 빌드도 같은 결과로 0.45 초, 경고 없음):

```text
info: sdot4x4(.S)    10982 shapes, 0 bad
info: sdot8x8(neon)  10982 shapes, 0 bad
info: smmla8x8(.S)   10982 shapes, 0 bad
info: bug udot      : 0..127 data bad shapes 0/20, full-range bad 20/20
info: bug nozip     : 8x8x16 wrong elements 48/64
info: bug no K-pad  : B only bad 0/24, A&B bad (K%8!=0) 21/21, A&B bad (K%8==0) 0/3
info: bug no edge   : 16x8 bad 0, 13x5 bad 63000 (>=1000 = canary hit)
40/40 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀리나 | 고침 |
|---|---|---|
| `sdot v0.4s, v4.16b, v5.4b[0]` (A 와 B 자리 바꿈) | lane = 행, 원소 선택 = 열이 되어 C 가 전치된다(정사각 대칭 데이터면 숨음) | **vn = B 블록**(lane = 열), **vm = A 블록**, `[i]` = 행 |
| `vdotq_laneq_s32(acc, b, a, r)` 를 `for (r…)` 안에서 | lane 은 컴파일 시간 상수여야 해서 컴파일 에러 | 매크로(`ROW8`)로 펼친다 |
| SMMLA 누산기를 그대로 저장 | 2×2 조각이다 (64 중 48 틀림) | `zip1/zip2 .2d` 로 행 재조립 |
| K 패딩 생략 | 쓰레기 × 쓰레기가 더해진다. K % KU ≠ 0 에서만 틀림 | 패킹에서 0 을 **쓴다**(건너뛰지 않음). 테스트는 K 의 모든 나머지 |
| 가장자리 타일도 C 에 바로 저장 | C 바깥(다음 행, 다른 버퍼)을 덮는다. 값 비교로는 못 봄 | 임시 타일 → 유효 mr×nr 만 복사. 테스트는 카나리 |
| `cbz x2` 없이 루프 시작 | kb = 0 이면 `subs` 가 0 → 2^64−1 로 돌아 사실상 무한 루프·범위 밖 로드 | 루프 앞에서 `cbz x2, 2f` (0 타일 저장) |
| 누산기를 v8–v15 에 | 하위 64비트 callee-saved — 호출자의 d8–d15 를 부순다 | v16–v31 사용, 아니면 `stp d8, d9, [sp, #-64]!` … 복원 |
| `.arch_extension i8mm` 없음 | `error: instruction requires: i8mm` (Apple 기본 타깃에 없음, 실측) | .S 에 `.arch_extension i8mm`, C 는 `target("+i8mm")` |
| 이론 피크를 ns 중앙값으로 | E/P/Super 코어가 섞여 피크가 낮게 잡히고 효율이 137% 가 됨(실제로 겪음) | 코어 종류별로 묶어 사이클 비 × 그 코어 클럭 |
| K 가 커질 수 있는데 int32 누산 | K ≥ 131072 에서 조용히 감쌈(포화도 아님) | K 분할 + int64 합(연습 2) 또는 SADALP 비우기 |

## 8. 면접 Q&A

<details><summary>Q1. SDOT 과 SMMLA 는 뭐가 다르고, 언제 SMMLA 가 유리한가?</summary>

SDOT 은 4개 int8 곱의 합을 lane 마다 더한다. 명령 1개 = 16 MAC 이다. SMMLA 는 2×8 · (2×8)ᵀ = 2×2 를 더한다. 명령 1개 = 32 MAC 이다. 이 M6 Perf 코어에서 실측하면 SDOT 은 3개/사이클(지연 3), SMMLA 는 약 1.5개/사이클(지연 6)이다. 그래서 **ops/사이클은 96 으로 같다**. SMMLA 의 이점은 같은 일에 명령 수가 절반이라는 것이다(디코드·발행 슬롯, 루프 오버헤드 비중이 준다). 로드 바이트당 MAC 은 명령이 아니라 타일이 정한다. 8×8 이면 둘 다 4 MAC/B 다. 그래서 프런트엔드가 병목인 코어, 또는 SMMLA 처리량이 SDOT 과 같은 코어에서 유리하다. 대가는 K 를 8, M/N 을 2 단위로 묶어야 한다는 것과 zip 비용이다. M = 1 (GEMV) 에서는 행쌍의 절반이 버려진다. 결론은 "칩마다 재봐야 한다"다.
</details>

<details><summary>Q2. 왜 패킹하나? 패킹 안 하고 바로 SDOT 하면?</summary>

커널 루프의 로드를 연속·순차로 만들려는 것이다. 원본 B 의 한 열을 따라 4바이트(k 방향)를 모으려면 `ldb` 간격으로 4번 읽어 재배열해야 한다. 그러면 루프 안에 셔플이 생기고, 캐시 라인을 1바이트만 쓰고 버리고, TLB 미스도 난다(스칼라 기준선이 1024 에서 0.9 GOPS 로 떨어진 이유). 패킹은 O(MK + KN) 비용을 한 번 내고 O(MNK) 루프를 깨끗하게 만든다. 0 패딩으로 꼬리 처리도 커널에서 사라진다. 가중치는 모델 로드 때 한 번 패킹하고 재사용한다. n = 64 에서는 패킹이 오히려 지배적이었다(GOPS 1/4).
</details>

<details><summary>Q3. int32 누산기는 K 가 얼마까지 안전한가? 넘으면?</summary>

최악은 모든 곱이 (−128)(−128) = 2^14 인 경우다. 그래서 ⌊(2^31−1)/2^14⌋ = 131071 까지 안전하고, 131072 에서 정확히 2^31 → INT32_MIN 으로 감싼다(실측, 3 커널 모두). SDOT/SMMLA 는 포화가 아니라 모듈러다. 대책: K 를 131064 같은 KU 배수 조각으로 나눠 int64 로 합치기(연습 2, K = 300000 정확), 또는 주기적으로 `SADALP` 로 int64 에 비우기. u8×s8 이면 지평선이 65793 으로 줄어든다. 실제 모델에서 K 는 보통 수천이라 문제가 안 되지만, "왜 안전한지"를 숫자로 말할 수 있어야 한다.
</details>

<details><summary>Q4. 마이크로커널 타일 크기는 어떻게 정하나?</summary>

세 가지 제약을 맞춘다. ① **지연 숨기기**: 독립 누산기 ≥ 지연 × 처리량 = 3 × 3 = 9 (SDOT), 6 × 1.5 = 9 (SMMLA). 4×4(누산기 4)는 피크의 44% 가 상한이고 실측 39% 였다. ② **레지스터 예산**: v0–v31 중 누산기 + A/B 블록 레지스터. v8–v15 는 callee-saved 라 쓰면 저장 비용이 든다. 8×8 SDOT = 누산기 16 + 입력 4, SMMLA = 16 + 8. ③ **로드 대비 연산**: 블록당 MAC / 로드 바이트가 클수록 좋다(4×4: 2, 8×8 SDOT: 4, 8×8 SMMLA: 4 MAC/B). 그다음 실측으로 고른다. 12×8 같은 비정사각 타일도 흔하다.
</details>

<details><summary>Q5. K 방향 0 패딩, 한쪽만 해도 되나? M/N 방향은?</summary>

int8 에서는 **한쪽만 0 이면 충분**하다. 패딩 자리의 곱이 0 × 쓰레기 = 0 이기 때문이다(실측: B 패딩을 빼도 24/24 통과, A·B 둘 다 빼면 K % 8 ≠ 0 에서 21/21 실패). float GEMM 에서는 안 된다. 쓰레기가 NaN/Inf 면 0 × NaN = NaN, 0 × Inf = NaN 이라 **양쪽 다** 0 이어야 한다. M/N 방향 패딩 값은 버려지는 출력에만 영향을 준다. 그래도 저장하면 안 된다(가장자리 버그 = 카나리 63개 파손). 실무에서는 둘 다 0 으로 채우는 게 안전한 기본값이다. 패킹 단위 테스트가 레이아웃을 따로 지켜야 하는 이유도 여기 있다. mutant M1(A 패딩 = 1)은 GEMM 결과로는 안 잡혔다.
</details>

<details><summary>Q6. "이론 피크 대비 몇 %?" — 피크를 어떻게 계산했나?</summary>

피크 = (명령당 ops) × (사이클당 명령) × (클럭) × (코어 수). 명령당 ops 는 정의에서 나온다(SDOT 32, SMMLA 64). 사이클당 명령은 독립 누산기 16개 루프로 직접 쟀다(SDOT 3, SMMLA ~1.5). 클럭은 add 사슬로 쟀다(Perf 3.76 GHz). 그러면 단일 Perf 코어 피크는 361 / 343 GOPS 다. 8×8 커널은 65–76%, 4×4 는 39–54% 다. 다른 사람이 쓴 숫자를 인용하지 않은 이유는 코어 종류마다 다르고(Super 는 SDOT 4개/사이클로 보임), macOS 는 코어 고정이 안 되기 때문이다. 멀티코어 피크는 코어 종류별 피크의 합인데, 메모리 대역폭과 열 제한에 먼저 걸리는지도 따져야 한다.
</details>

<details><summary>Q7. (staff) n = 8192, 멀티코어로 키우면 이 코드를 어떻게 바꾸겠나?</summary>

지금은 B 패널(8 열 × K)마다 A 전체를 다시 훑는다. n = 1024 면 B 패널 128개 × A 1 MB = 128 MB 를 다시 읽는다(어느 캐시 단계에서 오는지는 측정하지 않았다). Goto/BLIS 식 블로킹으로 바꾼다. **KC**(K 블록)는 A 마이크로패널 + B 마이크로패널이 L1 에, **MC×KC** 의 A 블록은 L2 에, **KC×NC** 의 B 블록은 L3/SLC 에 맞춘다. 패킹도 블록 단위로 한다. 병렬화는 보통 NC(jc) 또는 MC(ic) 루프를 코어에 나눈다. 같은 클러스터의 코어는 L2 를 공유하니 B 블록을 공유하게 묶는다. 이종 코어(Super/Perf/E)는 정적 분할이면 느린 코어가 꼬리를 잡으니 작은 조각의 동적 분배가 필요하다. 마지막으로 이 칩에는 SME2 가 있으니, 큰 GEMM 은 SMOPA 외적 타일로 넘기는 것이 다음 단계다(streaming mode 진입 비용 vs 크기, 측정 필요).
</details>

<details><summary>Q8. (staff) 배치 1 (LLM 토큰 디코드, 실시간 제어 추론)에서는 이 설계가 틀린다. 왜, 어떻게 바꾸나?</summary>

M = 1 이면 8×8 타일에서 7/8 이 버려진다. 실측 GEMV 는 약 30 GOPS 로, 미리 패킹 GEMM 의 1/8 이다. 또 산술 강도가 바이트당 2 ops 라서, 가중치를 한 번 읽는 메모리 대역폭이 상한이 된다. 바꿀 것: ① GEMV 전용 커널 — 출력 여러 개를 lane 에 두고 K 방향으로 vector SDOT(`sdot v.4s, w.16b, x.16b`)을 쌓는다. 활성값 x 는 레지스터에 재사용한다. ② 가중치 바이트를 줄인다 — int4 가중치(weight-only quant), 그룹 스케일. 대역폭이 상한이니 이것이 곧 속도다. ③ 프리페치와 큰 페이지로 DRAM 스트리밍 효율을 올린다. ④ 여러 요청을 묶어(batching) M 을 키운다. NPU 설계에서도 systolic array 가 배치 1 에서 놀지 않게 하는 것이 핵심 문제다.
</details>

<details><summary>Q9. 양자화: 영점(zero-point)과 재양자화는 커널에 어떻게 들어가나?</summary>

실수 x ≈ s_x(q_x − z_x), w ≈ s_w·q_w (가중치는 보통 대칭, z_w = 0). 그러면 Σ(q_x − z_x)q_w = Σ q_x q_w − z_x·colsum(W) 이다. 정수 GEMM 은 그대로 두고, 가중치 열합 colsum 을 로드 때 한 번 계산해 빼면 된다(연습 3, USMMLA u8×s8). 결과 int32 에 bias(int32, 스케일 s_x·s_w)를 더한다. 그다음 실수 배율 s_x·s_w/s_y 를 정수 mult·2^−shift 로 근사해 곱하고 반올림 시프트, z_y 더하고 int8 포화한다. 채널별(per-channel) 스케일이면 mult/shift 가 열마다 다르다(`mult[n]`). 반올림 규칙이 프레임워크와 다르면 결과가 1 LSB 씩 어긋나므로 비트 정확(bit-exact) 검증의 대상이다.
</details>

<details><summary>Q10. (실리콘 검증 관점) 이 커널이 맞다는 것을 어떻게 보장했나?</summary>

① 명령 수준: C 로 쓴 SMMLA/SDOT-by-element **명령 모델**과 실제 명령을 랜덤 + 극단값(전부 −128, ±127 섞기, 누산기 INT32_MAX 근처) 2만 개로 비교했다(연습 1). ② 커널 수준: 단위 타일의 손계산 값 + ldc 바깥 카나리. ③ 시스템 수준: 작은 모양 전수(10,982 × 3), 큰 랜덤 + stride, 지평선 경계, 레이어 int64 참조. ④ 테스트의 테스트: 버그 4종의 숨음/잡힘 조건 단언, 그리고 mutant 7개가 모두 FAIL. 실리콘 검증의 "참조 모델 vs DUT, 커버리지, 오류 주입"과 같은 구조다.
</details>

## 9. 추가 연습

1. **명령 모델**: `smmla_model(d, n, m)` 와 `sdot_lane_model(d, n, m, L)` 을 C 로 쓰고, `vmmlaq_s32` / `vdotq_laneq_s32(…, 2)` 실제 명령과 2만 개 입력에서 비교하라. 누산기가 INT32_MAX 근처일 때 감쌈까지 맞아야 한다.

<details><summary>답</summary>

int32 덧셈 넘침은 C 에서 UB 이므로 모델은 `uint32_t` 로 더하고 마지막에 `int32_t` 로 바꾼다. 곱 하나는 int 범위(|곱| ≤ 16384)라 안전하다. 실측 결과 불일치 0, 그리고 `INT32_MAX + 8·16384 = -2147352577` (lane 0), lane 1 = 131072 를 확인했다. SMMLA 의 피연산자 해석은 "vn 의 행 i = 바이트 8i..8i+7, vm 의 행 j = 바이트 8j..8j+7, d[2i+j]" 다. 이 모델이 하드웨어와 맞는다는 것이 본문 커널 설계의 근거다.
</details>

2. **긴 K**: `gemm_s8_long(...)` 이 int64 C 를 돌려주게 하라. K 를 `KC = 131064` (8 의 배수, ≤ 131071) 조각으로 나눠 int32 GEMM 을 반복하고 int64 에 더한다. K = 300000, 모두 −128 이면?

<details><summary>답</summary>

300000 · 16384 = **4915200000** (int32 불가). 열 2 를 3개 중 1개만 127 로 바꾸면 200000·16384 − 100000·16256 = **1651200000** 이다. 한 번에 int32 로 돌리면 4915200000 − 2^32 = **620232704** 로 감싼다. 셋 다 실측 일치. 조각 경계는 `A + k0` (lda 그대로), `B + k0·ldb` 다. 조각마다 패킹을 다시 하므로 패킹 비용은 그대로다.
</details>

3. **u8 × s8 + 영점**: ReLU 뒤 활성값은 0..255 (uint8, 영점 za)다. `practice.S` 의 `.macro MMLA8x8` 로 `usmmla_8x8_kernel` 을 만들고 `C = Σ(x − za)·w = USMMLA 결과 − za·colsum(W)` 를 구현하라. 지평선은?

<details><summary>답</summary>

`usmmla vd.4s, vn(u8).16b, vm(s8).16b` — A 가 vn 이라 A 가 부호 없음이다. 바이트 레이아웃은 같아서 `pack_A` 를 그대로 쓴다(`uint8_t*` → `int8_t*` 캐스트, 바이트 불변). 랜덤 60 모양에서 int64 참조와 일치했다. 부호 확인: x = 255, w = −1, K = 8 → USMMLA −2040, 같은 바이트를 SMMLA 로 돌리면 (−1)(−1)·8 = **+8** 이다(잘못된 명령 선택이 조용히 틀린다). 지평선: 최악 |곱| = 255·128 = 32640 → ⌊2^31/32640⌋ = **65793**.
</details>

4. **타일 낭비와 산술 강도**: `util_permille(M, N, MR, NR)` (쓸모 있는 출력 비율)와 `ai_milli(M, N, K)` = 2MNK / (MK + KN + 4MN) (ops/byte)를 구현하고 GEMV 와 정육면체를 비교하라.

<details><summary>답</summary>

GEMV 1×1024 를 8×8 로 돌리면 125‰, 4×4 면 250‰, 17×17 을 8×8 로 돌리면 289/576 = 501‰ 이다. 산술 강도(실측 출력): `64^3 21.33  256^3 85.33  1024^3 341.33  GEMV(1x1024x1024) 1.990` ops/B. roofline: 피크 361 GOPS 를 DRAM 대역폭(이 Mac 에서 부하 중 약 70–80 GB/s 로 보였다 — ARM-M06 측정, 추정)으로 나누면 손익분기 강도는 약 5 ops/B 다. 그래서 GEMM 은 계산에 묶이고 GEMV 는 메모리에 묶인다. 본문 GEMV 측정(~30 GOPS)은 그보다 먼저 타일 낭비(1/8)에 묶였다.
</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-A05_p && /tmp/ARM-A05_p
```

```text
info: AI ops/B  64^3 21.33  256^3 85.33  1024^3 341.33  GEMV(1x1024x1024) 1.990
14/14 passed
```

## 10. 핵심 정리

- GEMM = **패킹**(연속·0 패딩) + **마이크로커널**(누산기를 레지스터에 K 끝까지) + **가장자리**(임시 타일 → 유효 부분만 복사).
- `sdot vR.4s, vB.16b, vA.4b[R]`: vn = B 블록(lane = 열), `[R]` = A 의 행. SDOT 16 MAC, KU = 4.
- `smmla` = 2×8 · (2×8)ᵀ → [r0c0 r0c1 r1c0 r1c1], 32 MAC, KU = 8, 저장은 `zip1/zip2 .2d`.
- int32 지평선 K ≤ 131071 (= ⌊(2^31−1)/2^14⌋). 넘으면 모듈러로 감싼다(포화 아님). u8×s8 은 65793.
- 필요 누산기 ≥ 지연 × 처리량. M6 Perf 실측: SDOT 3 × 3 = 9, SMMLA 6 × 1.5 = 9 → 4×4 는 44% 상한, 8×8 은 충분.
- M6 Perf 단일 코어 피크(직접 잼): SDOT ≈ SMMLA ≈ 96 ops/사이클 ≈ 350–360 GOPS. 8×8 GEMM 65–76%, 4×4 39–54%.
- 테스트는 전 범위 데이터 + K 의 모든 나머지 + C 바깥 카나리 + 패킹 레이아웃 단위 테스트. 결과 비교만으로 숨는 버그가 있다.
- 배치 1 은 타일 낭비 + 바이트당 2 ops → GEMV 전용 커널·가중치 압축으로 설계를 바꾼다.

## 참고자료

- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — SDOT (by element / vector), SMMLA/UMMLA/USMMLA, ZIP1/ZIP2, FEAT_DotProd / FEAT_I8MM 도입 버전 (문서 확인 권장)
- [Arm C Language Extensions — Neon intrinsics](https://developer.arm.com/architectures/instruction-sets/intrinsics/) — `vdotq_laneq_s32`, `vmmlaq_s32`, `vusmmlaq_s32`
- Goto & van de Geijn, "Anatomy of High-Performance Matrix Multiplication", ACM TOMS 2008 — 패킹·블로킹(MC/KC/NC)의 원전
- [BLIS](https://github.com/flame/blis) / [ruy](https://github.com/google/ruy) / [XNNPACK](https://github.com/google/XNNPACK) — 실제 int8 마이크로커널과 패킹 형식 (SDOT/I8MM 경로)
- Jacob et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018 — 영점·재양자화 공식
- Arm Neoverse / Cortex-X Software Optimization Guide — 코어별 SDOT/SMMLA 지연·처리량 (이 레슨은 인용하지 않고 M6 에서 직접 잼)
- 관련 레슨: ARM-M06 (NEON 기초, SDOT 내적, 지평선), ARM-M03 (지연·처리량 측정, 코어 종류), ARM-M01 (callee-saved d8–d15), ARM-A07 (포화·고정소수점, 반올림), ARM-A11 (자동 벡터화), ARCH-M08 (CPU 번호 ↔ 코어 종류), 다음 단계: SME2 SMOPA, EML 트랙 (임베디드 ML 배포)

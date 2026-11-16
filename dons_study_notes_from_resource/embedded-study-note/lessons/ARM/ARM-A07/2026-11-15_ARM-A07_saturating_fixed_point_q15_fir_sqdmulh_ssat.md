---
id: ARM-A07
time: 120분
updated: 2026-11-15
---
# ARM-A07 포화·고정소수점 명령: SQADD, SQDMULH, SSAT

## 학습 목표

- Q15/Q31 고정소수점(fixed-point)의 곱·합·반올림을 **비트 단위로 하나의 명세**로 정의하고(ETSI 기본 연산과 같은 정의), 32비트 연산만 쓰는 UB 없는 C 기준 구현을 만든다.
- 같은 Q15 FIR 을 **NEON**(`SQDMULL/SQDMLAL`, `SQADD`, `SQRSHRN`, `SQDMULH/SQRDMULH`)과 **Cortex-M4 Thumb-2 DSP**(`SMULBB`, `QDADD`, `QADD`, `SSAT`, `SMLAD`)로 쓰고, 맥과 QEMU 양쪽에서 같은 골든 해시(0xc93a6433)가 나오는 것으로 **ISA 사이 비트 일치**를 증명한다.
- "포화하는 입력은 −32768·−32768 하나뿐"을 2^32 쌍 전수 검사로 확인하고, 포화 덧셈에 **결합법칙이 없다**는 사실이 벡터화 방향(출력 방향 vs 탭 방향)을 정한다는 것을 버그 주입으로 보인다.
- 포화 여부를 알려 주는 **APSR.Q / FPSR.QC**(sticky)를 읽고, 컴파일러가 이 플래그를 모른다는 함정(-O2 에서 QC=0)을 실측으로 재현한다.
- 헤드룸(headroom) 정리 Σ|h| ≤ 32767 을 증명하고, 그 조건에서만 빠른 비포화 `SMLAD` 경로가 명세와 같다는 것(그리고 Q=0 이어도 틀릴 수 있다는 것)을 실측으로 보인다.

## 1. 문제

모터 전류 루프, 오디오·음성 코덱, IMU/ADC 필터는 대부분 **Q15 고정소수점**으로 돈다. 면접에서는 "SQDMULH 가 포화하는 입력은?", "SQDMULH 와 SQRDMULH 차이?", "왜 Q15 곱에 doubling 이 들어가?", "포화 누산을 SIMD 로 어떻게 나눠?", "M4 에서 SMLAD 써도 돼?"가 꼬리를 문다. 실제 칩(STM32F4/G4 의 Cortex-M4, nRF52 의 M4F, Cortex-A 의 NEON)에서 **같은 필터가 플랫폼마다 1 LSB 라도 다르면** 회귀 테스트·코덱 적합성 시험·센서 보정이 전부 깨진다. 그래서 "비트 정확(bit-exact)"이 이 레슨의 기준이다.

Q15 는 int16 하나를 [−1, 1) 의 소수로 읽는 규칙이다: 값 = 정수 / 2^15. 두 Q15 를 곱하면 Q30 이 나오고, 왼쪽으로 1비트 밀면(doubling) Q31 이 된다. 그 상위 16비트가 다시 Q15 다. 이 "doubling" 때문에 −1.0 × −1.0 = +1.0 이 Q31 로 표현되지 않아 **딱 한 경우만 포화**한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">Q15 × Q15 → Q31 → Q15: SQDMULH / SQRDMULH 가 하는 일</text>
<line x1="40" y1="55" x2="700" y2="55" stroke="currentColor"/>
<line x1="40" y1="48" x2="40" y2="62" stroke="currentColor"/><line x1="370" y1="48" x2="370" y2="62" stroke="currentColor"/><line x1="700" y1="48" x2="700" y2="62" stroke="currentColor"/>
<text x="40" y="78" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-32768 = -1.0</text>
<text x="370" y="78" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<text x="700" y="78" fill="currentColor" text-anchor="end" font-family="monospace" font-size="12">32767 = 0.99997</text>
<text x="40" y="40" fill="currentColor" font-size="12">Q15: 값 = int16 / 2^15 — +1.0 은 없다 (비대칭)</text>
<rect x="20" y="100" width="90" height="34" class="f-mut" stroke="currentColor"/><text x="65" y="122" fill="currentColor" text-anchor="middle" font-family="monospace">a (Q15)</text>
<rect x="20" y="144" width="90" height="34" class="f-mut" stroke="currentColor"/><text x="65" y="166" fill="currentColor" text-anchor="middle" font-family="monospace">b (Q15)</text>
<line x1="110" y1="139" x2="150" y2="139" stroke="currentColor"/><text x="130" y="132" fill="currentColor" text-anchor="middle">×</text>
<rect x="150" y="120" width="150" height="38" fill="none" stroke="currentColor"/><text x="225" y="137" fill="currentColor" text-anchor="middle" font-family="monospace">a·b  (Q30)</text><text x="225" y="152" fill="currentColor" text-anchor="middle" font-size="12">|a·b| ≤ 2^30, 안 넘침</text>
<line x1="300" y1="139" x2="340" y2="139" stroke="currentColor"/><text x="320" y="132" fill="currentColor" text-anchor="middle" font-size="12">×2</text>
<rect x="340" y="120" width="190" height="38" class="f-acc" stroke="currentColor"/><text x="435" y="137" fill="currentColor" text-anchor="middle" font-family="monospace">sat32(2ab) (Q31)</text><text x="435" y="152" fill="currentColor" text-anchor="middle" font-size="12">= L_mult = SQDMULL</text>
<rect x="560" y="100" width="170" height="30" fill="none" stroke="currentColor"/><text x="645" y="120" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">hi16 → SQDMULH</text>
<rect x="560" y="148" width="170" height="30" fill="none" stroke="currentColor"/><text x="645" y="168" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">+0x8000, hi16 → SQRDMULH</text>
<line x1="530" y1="139" x2="560" y2="115" stroke="currentColor"/><line x1="530" y1="139" x2="560" y2="163" stroke="currentColor"/>
<rect x="20" y="205" width="710" height="54" class="f-warn" stroke="currentColor"/>
<text x="30" y="226" fill="currentColor">유일한 포화: a = b = -32768 → 2ab = 2^31 &gt; INT32_MAX → 0x7FFFFFFF (Q31 의 0.99999…)</text>
<text x="30" y="248" fill="currentColor">2^32 쌍 전수 검사: SQDMULH/SQRDMULH/SQDMULL 각각 포화 쌍 1, 불일치 0</text>
<text x="20" y="285" fill="currentColor">버림 vs 반올림: SQDMULH(-1, 1) = floor(-2/65536) = -1 (−∞ 쪽 버림, 평균 −0.5 LSB 치우침)</text>
<text x="20" y="307" fill="currentColor">              SQRDMULH(-1, 1) = floor((-2+32768)/65536) = 0</text>
</svg>
```

이 레슨의 **명세(파이프라인 하나)** — 모든 구현이 이것과 비트 단위로 같아야 한다:

| 이름 | 정의 | NEON | Cortex-M4 |
|---|---|---|---|
| `L_mult(a,b)` | `sat32(2·a·b)` | `SQDMULL` | `SMULBB` + `QADD p,p` |
| `L_add(a,b)` | `sat32(a+b)` | `SQADD .4s` | `QADD` |
| `L_mac(acc,a,b)` | `L_add(acc, L_mult(a,b))` — **포화 두 번** | `SQDMLAL` (= SQDMULL + SQADD) | `SMULBB` + `QDADD` |
| `round(L)` | `L_add(L, 0x8000) >> 16` | `SQRSHRN #16` (같음을 증명) | `QADD` + `ASR #16` |
| FIR | `y[i] = round( L_mac 누적, k = 0..T−1 순서, (h[k], x[i+T−1−k]) )` | 출력 8개 = lane 8개 | 탭 하나씩 |

`x` 는 `n+T−1` 개(앞 T−1 개 = 과거 샘플). 포화 덧셈은 결합법칙이 없으므로 **누적 순서까지 명세**다.

### Part A — 명세와 세 구현

- **C 기준 구현** (`solution.c`, TODO): `q31_add_sat`, `q15_lmult`, `q31_mac`, `q31_round`, `q15_mul`(SQDMULH), `q15_mulr`(SQRDMULH), `q15_add_sat`(SQADD .h), `fir_q15_ref`. 32비트 연산만, **UB 없이**(넘친 다음에 검사하면 이미 UB).
- **NEON intrinsics** (TODO): `q15_mul_neon`(`vqdmulhq_s16`), `q15_add_neon`(`vqaddq_s16`), `fir_q15_neon`(`vqdmlal_n_s16`/`vqdmlal_high_n_s16` + `vqaddq_s32` + `vshrn_n_s32`), 꼬리는 스칼라 intrinsic(`vqdmlalh_s16`, `vqrshrns_n_s32`).
- **손 asm** (`solution.S`, TODO): `q15_scale_asm`(`SQRDMULH` by scalar), `fir_q15_asm`(`SQDMULL/SQDMULL2` + `SQADD` + `SQRSHRN/SQRSHRN2`, 꼬리는 스칼라 형태 `sqdmull s,h,h`).
- 테스트의 정답은 int64 로 넓게 계산한 **오라클**(`o_*`)이다. 모든 a × 경계 b(양쪽 순서) + 2^20 무작위, FIR 은 탭 14종 × 길이 26종 × 필터 4종 × 신호 4종.

### Part B — 포화 경계와 버그 주입

- `shared_bugs.S` 의 버그 3개 + C 버그 1개를 **테스트가 잡는지** CHECK 한다: ① 이득에 SQRDMULH 대신 SQDMULH(반올림 누락) ② 마지막에 SQRSHRN 대신 SQSHRN(버림) ③ SMULL+SHL+ADD(포화 없음 → 감쌈) ④ 탭 방향 분할 누적(짝/홀 탭 누산기 → 마지막 SQADD).
- ③④ 는 "안전한 필터 + 무작위 신호"의 **약한 테스트를 통과**해 버린다는 것도 CHECK 로 박는다. 전 스케일 구형파(full-scale square wave), 필터 부호에 맞춘 최악 신호(matched), −32768 연속이 있어야 잡힌다.
- 손 계산 예(포화 두 번이 결과를 바꾸는 입력 4개)와 **헤드룸 정리**(Σ|h| ≤ 32767 이면 포화 파이프라인 = 정확 계산)를 테스트로 확인한다.

### Part C — 센서 체인과 Cortex-M4

- `chain_process()` — 블록 단위 스트리밍: `[과거 T−1 | 새 블록]` 버퍼 → FIR(asm) → 이득(SQRDMULH) → 오프셋(SQADD). 무작위 블록 크기 0..64 로 잘라 넣어도 한 번에 계산한 결과와 같아야 한다.
- `run_qemu.sh m4` (QEMU `mps2-an386`, Cortex-M4): 같은 명세를 `SMULBB+QDADD`(asm)와 `SMLAD`+`SSAT`(ACLE)로. APSR.Q 를 `mrs` 로 읽어 sticky 동작을 확인하고, 맥과 같은 골든 해시를 낸다.

## 2. Problem (English)

> **Bit-exact Q15 FIR on two ARM ISAs.**
>
> Fixed-point semantics (the "spec"): `L_mult(a,b) = sat32(2ab)`, `L_mac(acc,a,b) = sat32(acc + L_mult(a,b))` applied tap by tap in order `k = 0..T−1`, `round(L) = sat32(L + 0x8000) >> 16`, and `y[i] = round(Σ_k L_mac(h[k], x[i+T−1−k]))` where `x` holds `n+T−1` samples.
>
> 1. Implement the scalar primitives in portable C using only 32-bit arithmetic and no undefined behaviour.
> 2. Implement `void fir_q15(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y)` with NEON intrinsics and in AArch64 assembly; also `q15_scale(y, x, n, g)` = `SQRDMULH(x, g)`.
> 3. Implement the same FIR on a Cortex-M4 (ARMv7E-M DSP extension). Prove bit-exactness against the AArch64 version.
> 4. Explain when a faster non-saturating dual-MAC (`SMLAD`) implementation is bit-exact, and demonstrate a case where it is not even though the Q flag stays clear.
>
> Constraints: any `n` (0, non-multiples of 8), `1 ≤ T ≤ 64`, never write outside `y[0..n)`, leaf functions, don't touch `v8–v15`. Must build with plain `cc -O2`.
>
> Examples: `SQDMULH(−32768, −32768) = 32767`; `SQDMULH(−1, 1) = −1` but `SQRDMULH(−1, 1) = 0`; `L_mac(INT32_MIN, −32768, −32768) = −1` (not 0); `round(0x7FFF8000) = 32767`; with `h = {32767, −32768, −32768, 16384}`, `x = {3, −32768, 32767, −32768}` the single output is `1` (a single-saturation MAC gives `2`).
>
> Follow-ups: Which input pair saturates SQDMULH? Why can't you split a saturating accumulation across SIMD lanes by tap? How do you detect that saturation happened in a block? What do you do on a Cortex-M3?

## 3. 시작 코드

`starter.c` = 테스트 하네스 + 오라클 + 버그 4번(C) + TODO(C 기준 구현 8개, intrinsics 3개, `chain_process`). `starter.S` = asm TODO 2개(스텁은 `y` 를 건드리지 않고 `ret`). `shared_bugs.S` = Part B 버그 3개(TODO 아님).

<!-- code: starter.c -->

<!-- code: starter.S -->

<!-- code: shared_bugs.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S shared_bugs.S -o /tmp/ARM-A07_starter && /tmp/ARM-A07_starter
```

처음 실행 결과 (실제 출력, 앞과 뒤):

```text
FAIL starter.c:347  suite_add32() == 0
FAIL starter.c:348  suite_bin16(w_lmult_hi, o_lmult_hi) == 0 && suite_bin16(w_lmult_lo, o_lmult_lo) == 0
FAIL starter.c:349  suite_bin16(q15_mul, o_mul) == 0
…
info: golden FNV-1a of the spec (oracle) = 0xc93a6433
FAIL starter.c:381  golden_hash(fir_q15_ref) == GOLDEN_FNV && golden_hash(fir_q15_neon) == GOLDEN_FNV && golden_hash(fir_q15_asm) == GOLDEN_FNV
…
FAIL starter.c:445  out[0] == 32767 && out[CHAIN_BLOCK - 1] == 32767
7/38 passed
```

처음부터 통과하는 7개는 TODO 와 무관한 것들이다: 명령 자체의 의미 확인(SQDMLAL/SQRSHRN vs 오라클), 골든 상수 자체 확인, 버그 4종 검출(버그 함수는 완성품), 헤드룸 정리.

관찰용 파일과 M4 레인:

```bash
cc -std=c11 -O2 demo_exhaustive.c -o /tmp/ARM-A07_ex && time /tmp/ARM-A07_ex                       # 2^32 쌍 전수 (약 9 s)
cc -std=c11 -O2 demo_qc_reorder.c practice.S -o /tmp/ARM-A07_qc && /tmp/ARM-A07_qc               # -O2 에서 QC 가 사라지는 함정
cc -std=c11 -O2 demo_bench.c solution.S shared_bugs.S -o /tmp/ARM-A07_bench && /tmp/ARM-A07_bench # → bench_m6.txt
limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A07/run_qemu.sh m4'       # Cortex-M4 → m4_out.txt
limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A07/run_qemu.sh m4dis'    # 인코딩 → m4_dis.txt
limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A07/run_qemu.sh m3probe'  # M3 에서 QADD → UNDEFINSTR
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -c m4_fir.S -o /tmp/ARM-A07_m4fir.o           # VM 없이 "어셈블 되나?"
```

## 4. 힌트

<details><summary>힌트 1 — UB 없는 포화 덧셈</summary>

`a + b` 를 먼저 계산하고 부호를 보는 방식은 이미 넘친 뒤라 C 에서는 UB 다(C11 6.5p5). 더하기 **전에** `b > 0 && a > INT32_MAX − b` / `b < 0 && a < INT32_MIN − b` 로 판단한다. 이 두 뺄셈은 넘치지 않는다.
</details>

<details><summary>힌트 2 — L_mult 가 넘치는 경우는 하나뿐</summary>

`int16 × int16` 은 `int` 로 승격되어 |p| ≤ 2^30 이라 안전하다. `2p` 가 넘치는 건 `p == 0x40000000`(= −32768 · −32768) 하나뿐이다.
</details>

<details><summary>힌트 3 — NEON FIR 은 어느 방향으로 나누나</summary>

lane j 가 **출력 i+j** 를 맡게 한다. 탭 k 마다 `x + i + T − 1 − k` 에서 8개를 읽으면 lane j 에 `x[i+j+T−1−k]` 가 들어온다. `h[k]` 는 복제(`ld1r`, `vqdmlal_n_s16`)해서 8 lane 에 곱한다. 탭 방향으로 나누면 누적 순서가 바뀌어 포화 때 틀린다.
</details>

<details><summary>힌트 4 — 8 lane 을 int32 로 받기</summary>

`SQDMULL` 은 하위 4 lane(.4h → .4s), `SQDMULL2` 는 상위 4 lane(.8h 의 위쪽 → .4s). 누산기 두 개(v4, v5)를 `SQADD .4s` 로 누적하고, 끝에 `SQRSHRN v6.4h, v4.4s, #16` + `SQRSHRN2 v6.8h, v5.4s, #16`.
</details>

<details><summary>힌트 5 — 스트리밍 체인</summary>

`buf = [과거 T−1 | 새 n]` 에 대해 FIR 은 정확히 n 개의 출력을 낸다. 끝나면 `buf` 의 **마지막** T−1 개를 앞으로 옮긴다. 겹치므로 `memmove`. 처음엔 과거가 0(전원 켜진 직후의 무음).
</details>

## 5. 풀이

### 0. 명세를 하나로 고정한다

"포화 Q15 FIR" 이라고만 하면 구현마다 답이 다르다. 곱을 Q31 로 포화하나? 누적을 32비트에서 포화하나 64비트로 하나? 반올림은 언제, 어느 쪽으로? 이 레슨은 **ITU-T/ETSI 음성 코덱 기본 연산(basic operators)** 과 같은 정의를 고른다(`L_mult`, `L_mac`, `round` — 이름도 그대로 빌렸다, 원문 확인 권장). 이유는 셋이다.

| 이유 | 내용 |
|---|---|
| 하드웨어에 그대로 있다 | NEON `SQDMLAL` = `L_mac`, M4 `QDADD` = `L_mac` 의 합 부분. 한 명령 = 한 연산 |
| 비트 정확이 가능하다 | 정수 연산뿐, 순서까지 정하면 어느 ISA 에서나 같은 비트 |
| 업계 관례 | AMR/G.729 같은 코덱 적합성 시험은 이 정의로 만든 테스트 벡터와 **비트 단위 일치**를 요구한다(일반론, 표준 원문 확인 권장) |

선택하지 않은 것: "64비트로 정확히 더한 뒤 끝에서 한 번 포화"(SMLALD 형). 이건 **다른 필터**다 — 부분합이 한 번 포화했다 돌아오는 입력에서 답이 다르다(Part C "cancel" 실측).

### Part A — C 기준 구현: UB 없이 포화

```c
int32_t q31_add_sat(int32_t a, int32_t b) {
    if (b > 0 && a > INT32_MAX - b) return INT32_MAX;   // 더하기 전에 판단: 넘치지 않는 뺄셈
    if (b < 0 && a < INT32_MIN - b) return INT32_MIN;
    return a + b;
}
int32_t q15_lmult(int16_t a, int16_t b) {
    int32_t p = (int32_t)a * b;                         // |p| <= 2^30
    if (p == 0x40000000) return INT32_MAX;              // -32768·-32768 하나만
    return p * 2;
}
int16_t q31_round(int32_t L) { return (int16_t)(q31_add_sat(L, 0x8000) >> 16); }
```

- `>> 16` 에 음수가 들어간다: C11 6.5.7p5 에서 **구현 정의**(UB 아님). clang/gcc 는 산술 시프트로 정의한다. 오라클은 이것도 피하려고 `fdiv`(floor 나눗셈)를 쓴다.
- `q15_mul = L_mult >> 16`(SQDMULH), `q15_mulr = round(L_mult)`(SQRDMULH). 후자가 맞는 이유: `L_add(L_mult, 0x8000)` 이 포화하려면 `2ab ≥ 2^31 − 2^15`, 즉 `ab ≥ 2^30 − 2^14` 인데 −32768² 이 아닌 최대 곱은 `(−32768)(−32767) = 2^30 − 2^15` 이라 그 하나만 포화한다 → SQRDMULH 의 넓은 정의 `sat16((2ab + 2^15) >> 16)` 과 모든 입력에서 같다.

반올림 표 (테스트에 박힌 값):

| `L` (Q31) | Q15 로 본 값 | `round(L)` | 설명 |
|---|---|---|---|
| `0x7FFF8000` | 32767.5 | 32767 | `L + 0x8000` 이 넘침 → 포화 → `0x7FFFFFFF >> 16` |
| `0x18000` | 1.5 | 2 | 동점은 **+∞ 쪽** |
| `−0x18000` | −1.5 | −1 | +∞ 쪽 (0 에서 먼 쪽이 아님!) |
| `−0x8000` | −0.5 | 0 | |
| `−0x8001` | −0.50002 | −1 | |

### Part A — 포화 쌍은 정말 하나뿐인가 (2^32 전수)

테스트는 2 s 예산 때문에 "모든 a × 경계 b 17개(양쪽) + 2^20 무작위"만 돈다. 전수 검사는 `demo_exhaustive.c` 로 한 번 돌렸다 (실제 출력, `bench_m6.txt`):

```text
pairs checked: 4294967296
SQDMULH  mismatches 0, saturating pairs 1
SQRDMULH mismatches 0, saturating pairs 1
SQDMULL  mismatches 0, saturating pairs 1
/tmp/ARM-A07_ex  8.84s user 0.02s system 99% cpu 8.894 total
```

M6 의 NEON 이 세 명령 모두 명세와 2^32 쌍 전부 일치하고, 포화는 (−32768, −32768) 한 쌍뿐이다.

### Part A — 반올림 두 가지 쓰기는 같은 함수다

NEON intrinsic 판은 ETSI 식 그대로 `vqaddq_s32(acc, 0x8000)` + `vshrn_n_s32(·, 16)`, asm 판은 `SQRSHRN #16` 한 명령이다. 같은가?

- `SQRSHRN #16`: `sat16(⌊(L + 2^15) / 2^16⌋)` 를 **넓은 정밀도**로 계산.
- ETSI: `sat32(L + 2^15) >> 16`. `L + 2^15` 가 넘치는 건 `L ≥ 2^31 − 2^15` 일 때뿐이고, 그때 넓은 값은 `⌊…⌋ = 32768 → sat16 = 32767`, ETSI 는 `0x7FFFFFFF >> 16 = 32767`. 같다. 넘치지 않으면 `>> 16` 결과가 이미 int16 범위라 `sat16` 은 할 일이 없다. 아래쪽(`L + 2^15 ≥ −2^31 + 2^15`)은 넘치지 않는다.

테스트(`fails == 0` 블록)는 경계 19개 + 2^20 무작위 L 에서 `vqrshrns_n_s32(L,16) == o_round(L) == (int16_t)(o_add32(L,0x8000) >> 16)` 를 확인한다. clang 은 `vshrn #16` 두 개 + `vcombine` 을 **`uzp2.8h`**(홀수 halfword = 상위 16비트만 모으기) 한 명령으로 바꿨다(`clang -S` 실측) — 의미가 같으니 허용되는 변환이다.

### Part A — NEON: 출력 방향으로 벡터화

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">lane j = 출력 i+j. 탭 k 마다 x 를 한 칸씩 과거로 밀며 8개 읽는다</text>
<g font-family="monospace" font-size="12">
<text x="10" y="54" fill="currentColor">x[]</text>
<rect x="50" y="38" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="70" y="55" fill="currentColor" text-anchor="middle">i</text>
<rect x="90" y="38" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="110" y="55" fill="currentColor" text-anchor="middle">i+1</text>
<rect x="130" y="38" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="150" y="55" fill="currentColor" text-anchor="middle">…</text>
<rect x="170" y="38" width="80" height="24" class="f-mut" stroke="currentColor"/><text x="210" y="55" fill="currentColor" text-anchor="middle">i+T-2</text>
<rect x="250" y="38" width="80" height="24" class="f-acc" stroke="currentColor"/><text x="290" y="55" fill="currentColor" text-anchor="middle">i+T-1</text>
<rect x="330" y="38" width="250" height="24" class="f-acc" stroke="currentColor"/><text x="455" y="55" fill="currentColor" text-anchor="middle">… i+T+6</text>
<text x="600" y="55" fill="currentColor">k=0: ld1 8개</text>
<rect x="210" y="76" width="320" height="22" fill="none" stroke="currentColor" stroke-dasharray="4 3"/><text x="540" y="92" fill="currentColor">k=1: 2바이트 앞</text>
<text x="10" y="128" fill="currentColor">v0.8h</text>
<rect x="60" y="112" width="560" height="24" fill="none" stroke="currentColor"/>
<text x="95" y="129" fill="currentColor" text-anchor="middle">L0</text><text x="165" y="129" fill="currentColor" text-anchor="middle">L1</text><text x="235" y="129" fill="currentColor" text-anchor="middle">L2</text><text x="305" y="129" fill="currentColor" text-anchor="middle">L3</text><text x="375" y="129" fill="currentColor" text-anchor="middle">L4</text><text x="445" y="129" fill="currentColor" text-anchor="middle">L5</text><text x="515" y="129" fill="currentColor" text-anchor="middle">L6</text><text x="585" y="129" fill="currentColor" text-anchor="middle">L7</text>
<line x1="130" y1="112" x2="130" y2="136" stroke="currentColor"/><line x1="200" y1="112" x2="200" y2="136" stroke="currentColor"/><line x1="270" y1="112" x2="270" y2="136" stroke="currentColor"/><line x1="340" y1="112" x2="340" y2="136" stroke="currentColor"/><line x1="410" y1="112" x2="410" y2="136" stroke="currentColor"/><line x1="480" y1="112" x2="480" y2="136" stroke="currentColor"/><line x1="550" y1="112" x2="550" y2="136" stroke="currentColor"/>
<text x="10" y="162" fill="currentColor">v1.8h</text><rect x="60" y="146" width="560" height="24" class="f-mut" stroke="currentColor"/><text x="340" y="163" fill="currentColor" text-anchor="middle">h[k] × 8 (ld1r)</text>
<text x="10" y="196" fill="currentColor">v4.4s</text><rect x="60" y="180" width="275" height="24" class="f-ok" stroke="currentColor"/><text x="198" y="197" fill="currentColor" text-anchor="middle">sqdmull + sqadd (L0–L3)</text>
<text x="345" y="196" fill="currentColor">v5.4s</text><rect x="395" y="180" width="225" height="24" class="f-ok" stroke="currentColor"/><text x="508" y="197" fill="currentColor" text-anchor="middle">sqdmull2 + sqadd (L4–L7)</text>
</g>
<text x="10" y="232" fill="currentColor" font-weight="bold">손 계산 예 (테스트 X_TWO_SAT/H_TWO_SAT): 같은 네 곱, 포화를 몇 번 하느냐로 답이 갈린다</text>
<g font-family="monospace" font-size="12">
<text x="10" y="254" fill="currentColor">k  h[k]    x     L_mult        명세 acc (두 번 포화)   한 번만 포화한 acc</text>
<text x="10" y="274" fill="currentColor">0  32767  -32768  -2147418112   -2147418112             -2147418112</text>
<text x="10" y="294" fill="currentColor">1 -32768   32767  -2147418112   -2147483648 (포화)      -2147483648</text>
<text x="10" y="314" fill="currentColor">2 -32768  -32768   2147483647*        -1                       0  (2^31 을 그대로 더함)</text>
<text x="10" y="334" fill="currentColor">3  16384       3        98304        98303 → round 1        98304 → round 2</text>
</g>
<text x="10" y="354" fill="currentColor" font-size="12">* 곱 자체가 포화. 명세 = sat(acc + sat(2ab)), "한 번만" = sat(acc + 2ab). 다섯 경로 모두 y = 1</text>
</svg>
```

- 탭 루프 안에서 lane 사이 통신(가로 합)이 **없다**. 그래서 순서가 명세 그대로다. 대신 출력이 8개 미만인 꼬리는 스칼라 형태(`sqdmull s2, h0, h1` / `sqadd s4, s4, s2` / `sqrshrn h6, s4, #16`)로 돈다 — AArch64 는 포화 명령에 **스칼라 SIMD 형태**(Sd, Hn …)가 있어 같은 의미를 1개짜리로 쓸 수 있다.
- `SQDMLAL` 이 정말 "두 번 포화"인지 명령 자체로 확인했다: `vqdmlal_s16(INT32_MIN, −32768, −32768)` lane = −1 (한 번이면 0). 경계 19 × 17 × 17 조합 전부 오라클과 같다(CHECK).

### Part A — 손 asm 한 줄씩

탭 루프(`fir_q15_asm` 의 `2:`):

| 명령 | 하는 일 | 왜 이 명령 |
|---|---|---|
| `ld1 {v0.8h}, [x8]` | x 8개 | 비정렬 OK (AArch64 Normal 메모리) |
| `sub x8, x8, #2` | 한 샘플 과거로 | 컨볼루션은 x 를 거꾸로 걷는다 |
| `ld1r {v1.8h}, [x9], #2` | h[k] 를 8 lane 에 복제 + 다음 탭 | by-element(`v1.h[0]`) 형태도 가능(연습 3) |
| `sqdmull` / `sqdmull2` | lane 0–3 / 4–7 의 `sat32(2·x·h)` | 곱이 Q31 로 넓어지므로 반씩 |
| `sqadd v4.4s` / `v5.4s` | `sat32(acc + p)` | 두 번째 포화 |
| 끝: `sqrshrn` / `sqrshrn2 #16` | Q31 → Q15, 반올림 + 포화 | `round` 한 명령 |

clang 이 intrinsic 판을 어떻게 컴파일했나(`clang -S -O2` 실측, 안쪽 루프):

```text
LBB10_14:
	ldr	q3, [x11], #-2
	ld1r.4h	{ v4 }, [x12], #2
	sqdmlal.4s	v1, v3, v4
	ext.16b	v3, v3, v3, #8
	sqdmlal.4s	v2, v3, v4
	subs	x13, x13, #1
	b.ne	LBB10_14
```

컴파일러는 `SQDMLAL`(곱+포화합 융합)을 써서 탭당 SIMD 연산이 손 asm(SQDMULL×2 + SQADD×2)보다 적다. 의미는 같다(`SQDMLAL` = `sat(acc + sat(2ab))`). 손 asm 은 `SQADD` 를 눈에 보이게 하려고 일부러 풀어 썼다 — 아래 처리량 표에서 intrinsic 판이 조금 빠른 이유로 보인다(추정).

### Part B — 버그 4종과 "약한 테스트"

실제 출력 (`/tmp/ARM-A07_solution`):

```text
info: golden FNV-1a of the spec (oracle) = 0xc93a6433
info: bug mismatches — scale_sqdmulh 753733, fir_trunc 9581, addwrap weak/strong 0/17742, tapsplit weak/strong 0/36135
info: saturating vs exact FIR differ — safe filters 0/200, hot filters 174/200
38/38 passed
```

| 버그 | 바뀐 것 | 약한 테스트(안전 필터 + 무작위) | 강한 테스트(필터 4종 × 신호 4종) |
|---|---|---|---|
| ① `bug_scale_sqdmulh` | `sqrdmulh` → `sqdmulh` | — (이득 스위트, 약 164만 출력 중 753,733 개가 1 LSB 다름) | 잡힘 |
| ② `bug_fir_trunc` | `sqrshrn` → `sqshrn` | 잡힘 | 9,581 출력 틀림 |
| ③ `bug_fir_addwrap` | `smull`+`shl`+`add` | **0 — 통과해 버린다** | 17,742 출력 틀림 |
| ④ `bug_fir_tapsplit` | 짝/홀 탭 누산기 → `SQADD` | **0 — 통과해 버린다** | 36,135 출력 틀림 |

③④ 가 약한 테스트를 통과하는 것은 우연이 아니다. 아래 정리 때문에 **안전한 필터에서는 포화가 한 번도 일어나지 않아** 포화 유무·누적 순서가 결과에 영향을 못 준다. 포화 코드를 검증하려면 포화가 실제로 나는 입력(구형파, matched, −32768 연속, 뜨거운 필터)이 반드시 있어야 한다.

추가로 **비공개 사본에 돌연변이(mutant) 7개**를 넣어 하네스가 잡는지 확인했다(레슨 폴더에는 남기지 않음): 꼬리만 `sqshrn`, round 를 감싸는 덧셈으로, NEON 탭 순서 뒤집기, 체인 history 갱신 누락, L_mult 포화 검사 제거, asm lane 4–7 만 `add`, NEON 꼬리를 "한 번만 포화" `L_mac` 으로 — 7개 모두 FAIL 이 났다. 마지막 것은 **손 계산 예 하나만** 잡았다(스위트는 통과). 무작위·구형파로는 "두 번 vs 한 번 포화"의 1 LSB 차이가 반올림에 묻히기 쉬워서, 경계를 정확히 노리는 입력을 손으로 만든 것이 값을 했다.

### Part B — 헤드룸 정리: Σ|h| ≤ 32767 이면 포화가 없다

**정리.** 모든 탭이 `|h_k| ≤ 32767` 이고 `Σ|h_k| ≤ 32767`(Q15 로 L1 노름 < 1.0)이면, **어떤 입력** x 에 대해서도 명세 파이프라인은 한 번도 포화하지 않는다. 따라서 결과 = 포화 없는 정확 계산 `sat16(⌊(2S + 2^15)/2^16⌋)`, `S = Σ h_k x_k`.

**증명.** `h_k ≠ −32768` 이므로 `L_mult` 은 포화하지 않는다. 임의의 부분합에 대해 `|2·Σ_{k≤j} h_k x_k| ≤ 2·32768·Σ|h_k| ≤ 2·32768·32767 = 2^31 − 2^16 < 2^31`. 반올림의 `L + 2^15 ≤ 2^31 − 2^16 + 2^15 < 2^31` 도 넘치지 않는다. ∎

테스트는 400개 필터(반은 안전, 반은 전 범위 무작위)로 확인했다: 안전 0/200 개가 다르고, 뜨거운 필터는 174/200 개가 다르다(위 출력). 펌웨어 관점: 계수 설계 단계에서 **Σ|h| ≤ 1** 로 스케일하면(필요하면 출력에서 이득을 다시 곱한다) 포화가 "설계상 불가능"해지고, 빠른 비포화 경로(Part C 의 SMLAD)를 쓸 자격이 생긴다. 반대로 DC 이득이 1 을 넘는 필터는 포화가 정상 동작의 일부다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">부분합(Q31)의 궤적: 안전한 필터는 ±2^31 띠에 닿지 않는다</text>
<rect x="60" y="40" width="660" height="22" class="f-warn" stroke="currentColor"/><text x="70" y="56" fill="currentColor" font-size="12">+2^31 위: 포화 (명세는 여기서 자르고, SMLAD 는 감싸거나 그대로 간다)</text>
<rect x="60" y="198" width="660" height="22" class="f-warn" stroke="currentColor"/><text x="70" y="214" fill="currentColor" font-size="12">−2^31 아래: 포화</text>
<line x1="60" y1="130" x2="720" y2="130" stroke="currentColor" stroke-dasharray="3 3"/><text x="30" y="134" fill="currentColor" font-size="12">0</text>
<polyline points="60,130 140,112 220,120 300,98 380,106 460,90 540,100 620,94 700,96" fill="none" stroke="currentColor" stroke-width="2"/>
<text x="560" y="86" fill="currentColor" font-size="12">안전 (Σ|h| ≤ 32767): 띠 안</text>
<polyline points="60,130 160,62 260,62 360,126 460,190 560,196" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="7 4"/>
<text x="170" y="82" fill="currentColor" font-size="12">"cancel" h={+,+,−,−}: 명세는 위에서 잘린 뒤 내려가 −32764</text>
<text x="380" y="182" fill="currentColor" font-size="12">SMLAD(정확 합)는 0 → 결과가 다르다, Q=0</text>
<text x="10" y="242" fill="currentColor" font-size="12">그림은 개념도 (축척 아님). 숫자는 run_qemu.sh m4 의 실제 출력: "cancel: smlad y=0 spec y=-32764 Q=0"</text>
</svg>
```

### Part C — 블록 스트리밍 체인

```c
void chain_process(chain_t *c, const int16_t *in, int16_t *out, size_t n) {
    size_t hist = c->T - 1;
    memcpy(c->buf + hist, in, n * sizeof *in);          // [과거 T-1 | 새 n]
    fir_q15_asm(c->buf, n, c->h, c->T, out);
    q15_scale_asm(out, out, n, c->gain);                // SQRDMULH, 제자리
    for (size_t i = 0; i < n; i++) g_offs[i] = c->offset;
    q15_add_neon(out, out, g_offs, n);                  // SQADD
    memmove(c->buf, c->buf + n, hist * sizeof *in);     // 겹침 → memmove
}
```

테스트는 1000 샘플을 무작위 블록 크기 0..64 로 쪼개 넣고, `[0 × (T−1) | 신호]` 를 한 번에 오라클로 돌린 결과(+ 이득 + 오프셋)와 비교한다(5가지 설정: 이득 1.0/0.7071/−1.0/0.5, 오프셋 최대값 포함). 마지막 CHECK 는 30000·(32767/32768) + 16384 가 **32767 에 붙는지**(감싸면 음수가 된다)다. 블록 크기 0 과 `n < T−1` 인 작은 블록에서도 history 가 맞아야 한다 — mutant "history 갱신 누락"은 5개 체인 테스트 중 T > 1 인 4개가 잡았다.

### Part C — Cortex-M4: 같은 명세를 DSP 명령으로

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">같은 명세, 세 가지 탭 루프</text>
<rect x="10" y="34" width="230" height="200" fill="none" stroke="currentColor"/>
<text x="20" y="54" fill="currentColor" font-weight="bold">NEON (8 출력 / 탭)</text>
<g font-family="monospace" font-size="12">
<text x="20" y="78" fill="currentColor">ld1  {v0.8h}  x[…]</text><text x="20" y="98" fill="currentColor">ld1r {v1.8h}  h[k]</text>
<text x="20" y="118" fill="currentColor">sqdmull  v2 ← 2xh</text><text x="20" y="138" fill="currentColor">sqdmull2 v3 ← 2xh</text>
<text x="20" y="158" fill="currentColor">sqadd v4 += v2</text><text x="20" y="178" fill="currentColor">sqadd v5 += v3</text>
<text x="20" y="206" fill="currentColor">끝: sqrshrn #16</text>
</g>
<rect x="255" y="34" width="230" height="200" class="f-ok" stroke="currentColor"/>
<text x="265" y="54" fill="currentColor" font-weight="bold">M4 fir_m4_q31 (1 출력 / 탭)</text>
<g font-family="monospace" font-size="12">
<text x="265" y="78" fill="currentColor">ldrsh r9,  [px], #-2</text><text x="265" y="98" fill="currentColor">ldrsh r10, [ph], #2</text>
<text x="265" y="118" fill="currentColor">smulbb r9, r9, r10</text><text x="265" y="138" fill="currentColor">qdadd r5, r5, r9</text>
<text x="265" y="158" fill="currentColor">  = sat(acc+sat(2p))</text>
<text x="265" y="206" fill="currentColor">끝: qadd 0x8000; asrs 16</text>
</g>
<rect x="500" y="34" width="230" height="200" class="f-warn" stroke="currentColor"/>
<text x="510" y="54" fill="currentColor" font-weight="bold">M4 fir_m4_smlad (2 탭 / 명령)</text>
<g font-family="monospace" font-size="12">
<text x="510" y="78" fill="currentColor">ldr r1, [x+i+j]  {x0|x1}</text><text x="510" y="98" fill="currentColor">ldr r3, [hr+j]   {h'0|h'1}</text>
<text x="510" y="118" fill="currentColor">smlad acc,r3,r1,acc</text><text x="510" y="138" fill="currentColor">  acc+lo·lo+hi·hi</text>
<text x="510" y="158" fill="currentColor">  포화 없음, 넘치면 Q</text>
<text x="510" y="206" fill="currentColor">끝: +0x4000; ssat #16,asr15</text>
</g>
<text x="10" y="258" fill="currentColor">hr[j] = h[T-1-j] (계수를 시간 역순으로 저장) → x 와 계수를 같은 방향으로 2개씩 읽는다. T 홀수면 hr[T] = 0 패딩.</text>
<text x="10" y="280" fill="currentColor">왼쪽 두 개 = 명세 그대로 (항상 비트 일치). 오른쪽 = "끝에서 한 번 포화" → Σ|h| ≤ 32767 일 때만 일치.</text>
<text x="10" y="302" fill="currentColor">골든 해시: 맥 C/NEON/asm = 0xc93a6433 = M4 fir_m4_q31 (QEMU mps2-an386 실측)</text>
<text x="10" y="322" fill="currentColor" font-size="12">M4 의 x 2개 읽기는 비정렬 LDR (i 가 홀수) — QEMU 에서 문제없이 돌았다. LDRD/LDM 은 비정렬이면 항상 fault (ARMv7-M, 문서 확인 권장)</text>
</svg>
```

`run_qemu.sh m4` 실제 출력(`m4_out.txt`, PASS 줄 일부 생략):

```text
ARM-A07 M4 lane: Cortex-M4 DSP (SMULBB/QDADD/QADD/SSAT/SMLAD) vs spec
PASS line 163: fails == 0
…
Q flag: qadd ok/sat/ok 011, clear 0, ssat sat/ok 10, smlabb ovf 1 r=0xbfff0000, smlad 2^31 1 r=0x80000000, lmult(-1,-1) 1
…
PASS line 211: suite_fir(fir_m4_q31, 1) == 0
PASS line 212: suite_fir(fir_m4_smlad, 0) == 0
PASS line 217: y[0] == 1
golden FNV-1a (fir_m4_q31) = 0xc93a6433
PASS line 221: gq == GOLDEN_FNV && golden_hash(o_fir) == GOLDEN_FNV
hot DC: smlad y=-4 spec y=32767 Q=1
PASS line 233: ya[0] != yb[0] && q_ovf == 1
cancel: smlad y=0 spec y=-32764 Q=0
PASS line 242: ya[0] == 0 && yb[0] != 0 && q_mid == 0
15/15 passed
qemu exit=0
```

읽는 법:

- **Q 플래그 줄**: `QADD` 가 포화 안 하면 0, 하면 1, 그 뒤 포화 안 하는 `QADD` 를 해도 **1 그대로**(sticky) — `msr APSR_nzcvq` 로 지울 때까지. `SSAT` 이 자르면 1. `SMLABB`(32767² + INT32_MAX)는 **감싸서**(0xBFFF0000) Q=1 — 포화가 아니라 "넘쳤다는 표시"만 한다. `SMLAD` 로 (−32768)²·2 = 2^31 → 0x80000000(감쌈) + Q=1. `SMULBB` 자체는 넘치지 않고 그 뒤 `QADD p,p` 가 포화해 Q=1.
- **hot DC**: Σ|h| = 4.0 인 필터에 DC 최대 입력 → SMLAD 의 32비트 합이 감싸서 y = −4, 명세는 32767. Q=1 이 "틀렸다"를 알려 준다.
- **cancel**: h = {32767, 32767, −32767, −32767}, x = 32767 상수. 정확한 합은 0 이고 SMLAD 는 32비트를 넘지 않아 **Q=0** 인데, 명세는 둘째 탭에서 Q31 포화한 뒤 내려와 −32764. **Q=0 이 비트 일치를 보장하지 않는다** — SMLAD 의 Q 는 "자기 32비트 합이 넘쳤나"이지 명세의 Q31 포화(2배 스케일)를 본 게 아니다.

`m4dis`(실제 인코딩, `m4_dis.txt`) — 탭 하나에 6명령:

```text
     dee:	f936 9902 	ldrsh.w	r9, [r6], #-2
     df2:	f937 ab02 	ldrsh.w	sl, [r7], #2
     df6:	fb19 f90a 	smulbb	r9, r9, sl
     dfa:	fa89 f595 	qdadd	r5, r5, r9
     dfe:	f1b8 0801 	subs.w	r8, r8, #1
     e02:	d1f4      	bne.n	dee <fir_m4_q31+0x1e>
```

gcc 14.2.1 이 ACLE `__smlad` 판을 컴파일한 안쪽 루프는 **두 탭에 6명령**(`ldr.w` ×2, `adds`, `cmp`, `smlad`, `bcc`)이고, 출력에서 `add.w #16384` → `mov.w asr #15` → `ssat #16` 을 따로 냈다(asm 의 `ssat r0, #16, r0, asr #15` 한 명령으로 합칠 수 있다: `m4_ssat16_asr15`). 두 `int16` 읽기(`ld2`)는 하나의 비정렬 `ldr.w` 로 합쳐졌다.

**ACLE 함정 (실측)**: 처음엔 Q 테스트를 `__smlabb(0x7FFF, 0x7FFF, big)` 로 썼는데 Q=0 이 나와 FAIL 했다. 디스어셈블해 보니 gcc 가 상수 피연산자의 SMLABB 를 **`add r5, r3` 로 접어 버려** SMLABB 명령 자체가 없었다(`__qadd`, `__ssat`, `__smlad` 는 이번 빌드에선 명령이 남았다). 컴파일러는 Q 를 "부작용"으로 취급하지 않는다 → Q 를 관찰하는 코드는 asm 으로 쓴다(`m4_smlabb`). 맥의 FPSR.QC 도 같은 문제다(연습 1, `demo_qc_reorder.c`).

**Cortex-M3 에서는?** `run_qemu.sh m3probe` 실제 출력:

```text
ssat #16 of 100000 on Cortex-M3 = 32767
PASS ssat runs on Cortex-M3
HardFault: CFSR=0x00010000 HFSR=0x40000000
PASS qadd on Cortex-M3 -> UNDEFINSTR (escalated: FORCED)
qemu exit=0
```

`SSAT/USAT` 은 ARMv7-M 기본 명령이라 M3 에도 있고(arm-none-eabi-gcc 도 M3 에서 `__ARM_FEATURE_SAT=1`, `__ARM_FEATURE_DSP` 없음), `QADD/SMLAD/SMULBB` 는 **DSP 확장(ARMv7E-M)** 이라 M3 에서 UNDEFINSTR(CFSR bit 16) UsageFault → UsageFault 가 꺼져 있어 HardFault 로 승격(HFSR.FORCED, bit 30). CFSR 해석은 ARM-M12 참고. M3 에서는 `SMULL`/`MLA` + 비교·`SSAT` 로 포화를 흉내 내야 하고, 명세를 지키면 비트 결과는 같지만 사이클은 늘어난다.

### 처리량 (demo_bench.c)

실제 출력 (`bench_m6.txt`, 3회 중 1회. n = 4096, M MAC/s, 9 샘플 중앙값, load 8–11):

```text
taps          C ref   C oracle64    NEON intr          asm   (M MAC/s, median of 9, n = 4096)
8               774         1291        22809        19904   neon/ref 29.5x  asm/ref 25.7x
32              758         1093        22332        17074   neon/ref 29.5x  asm/ref 22.5x
64              624         1128        19103        16774   neon/ref 30.6x  asm/ref 26.9x
```

- 세 번 돌린 범위: NEON 19–24 G MAC/s, asm 17–20 G MAC/s, C 기준 0.6–0.8 G MAC/s. **비율(약 25–30배)** 이 더 믿을 만하다(다른 작업이 함께 돌고, 어느 코어(Super/Performance/Efficiency)에서 돌았는지 고정하지 못했다).
- int64 오라클이 32비트 C 기준보다 빠르다: AArch64 에서는 int64 곱·비교가 한 명령이고 `csel` 로 분기 없이 자르지만, 32비트 기준 구현은 "넘치기 전 검사" 분기가 탭마다 있다(추정). 32비트 MCU 에서는 반대일 수 있다 — 거기선 측정하지 않았다.

### 리눅스/ELF 에서도 같은 파일

`.S` 는 `SYM()/FTYPE()/FSIZE()` 매크로로 Mach-O(`_name`)와 ELF(`.type`, `.size`)를 함께 지원한다. Lima VM(aarch64 Linux, gcc 15.2 / clang)에서 실제로 빌드해 돌렸다: gcc·clang 모두 solution 38/38, practice 11/11, starter 7/38(exit 1), 골든 해시도 같은 0xc93a6433. 연습 2 의 `SQRDMLAH` 는 `target("arch=armv8.1-a")` 로 켰다 — `target("rdm")` 은 gcc 가 거부하고 clang(리눅스)은 `v8.1a` 를 요구해서, 세 컴파일러가 모두 받는 문자열을 실측으로 골랐다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o /tmp/ARM-A07_solution && /tmp/ARM-A07_solution   # 38/38 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c solution.S shared_bugs.S -o /tmp/ARM-A07_asan && /tmp/ARM-A07_asan   # 38/38, 경고 없음
```

M4 레인 (QEMU): 같은 명세를 Thumb-2 DSP 로.

<!-- code: m4_fir.S -->

<!-- code: m4_harness.c -->

```bash
limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A07/run_qemu.sh m4'    # 15/15 passed, qemu exit=0
```

## 7. 자주 하는 실수

1. **넘친 다음에 검사** — `int32_t s = a + b; if ((a > 0 && b > 0 && s < 0) …)` 는 C 에서 이미 UB 다(clang -O2 는 그 검사를 지울 수 있다). 고친 코드: 더하기 전에 `a > INT32_MAX − b` 로 판단(5장), 또는 int64 로 더하고 자르기.
2. **SQDMULH 를 "반올림 곱"으로 착각** — 버림(−∞ 쪽)이라 평균 −0.5 LSB 치우침(DC 오프셋)이 생긴다. 이득·믹서처럼 결과가 바로 출력되는 곳은 `SQRDMULH`. 버그 ①: 이득 스위트 약 164만 출력 중 753,733 개가 1 LSB 달랐다.
3. **포화 누산을 탭 방향으로 SIMD 화** — 포화 덧셈은 결합법칙이 없다. 짝/홀 탭 누산기를 따로 두면 안전 필터에선 통과하고 뜨거운 입력에서 36,135 출력이 틀렸다(버그 ④). 고친 방법: lane = 출력.
4. **약한 테스트로 포화 코드를 "검증"** — 무작위 입력 + 작은 계수는 포화를 한 번도 일으키지 않는다(헤드룸 정리). 버그 ③④ 는 그런 테스트에서 0 개가 틀렸다. 구형파·matched·−32768 연속·뜨거운 필터·손으로 만든 경계 예를 반드시 넣는다.
5. **Q/QC 를 C 코드 한가운데서 읽기** — 컴파일러는 이 플래그를 모른다. `-O2` 에서 결과를 안 쓰는 포화 연산은 지워지고(QC=0), 상수 입력은 컴파일 때 접히고(QC=0), gcc 는 상수 `__smlabb` 을 `add` 로 바꿨다(Q=0). 고친 방법: 포화 연산 + 플래그 읽기를 noinline 함수나 asm 안에 가두고, 결과를 포인터로 내보낸다(연습 1).
6. **SMLAD 의 Q=0 을 "명세와 같다"로 해석** — "cancel" 실측: Q=0 인데 SMLAD y=0, 명세 y=−32764. SMLAD 경로를 쓰려면 Q 가 아니라 **Σ|h| ≤ 32767 을 초기화 때 검사**한다.
7. **스트리밍에서 history 를 memcpy 로 당기기 / 안 당기기** — 겹치는 복사는 `memmove`. 갱신을 빠뜨리면 블록 경계마다 과거가 틀린다(mutant 4: T > 1 인 체인 테스트 4개 FAIL).

## 8. 면접 Q&A

<details><summary>Q1. SQDMULH 가 포화하는 입력은? 왜 "doubling" 인가?</summary>

(−32768, −32768) 하나뿐이다(2^32 전수 검사 실측: 포화 쌍 1). Q15 × Q15 = Q30 이고 소수점 위치를 Q31 에 맞추려고 2배 한다. −1.0 × −1.0 = +1.0 은 Q31 의 최댓값(1 − 2^−31)보다 커서 0x7FFFFFFF 로 포화한다. Q15 가 비대칭(−1.0 은 있고 +1.0 은 없음)이라서 생기는 유일한 구멍이다. 꼬리질문 "그럼 −32768 을 안 쓰면?" → 계수 범위를 [−32767, 32767] 로 제한하는 설계가 흔하다. 그러면 `L_mult` 포화는 원천적으로 사라진다.
</details>

<details><summary>Q2. SQDMULH vs SQRDMULH, 어디에 무엇을 쓰나?</summary>

SQDMULH = `sat16(⌊2ab/2^16⌋)`(버림, −∞ 쪽), SQRDMULH = `sat16(⌊(2ab + 2^15)/2^16⌋)`(반올림, 동점은 +∞ 쪽). 버림은 평균 −0.5 LSB 치우침이 쌓여 DC 오프셋·리미트 사이클을 만든다. 출력에 바로 나가는 이득·믹서·계수 갱신에는 SQRDMULH. 내부 누산이 더 넓은 정밀도로 이어지고 마지막에 한 번 반올림한다면, 곱마다 반올림할 필요가 없어 Q31 누산(SQDMLAL) + 마지막 SQRSHRN 이 정석이다. M4 에는 SQRDMULH 같은 16비트 반올림 곱 한 명령이 없고 `SMULBB` 후 시프트·반올림을 따로 하거나 `SMMULR`(32×32 상위 반올림) 같은 다른 명령을 쓴다(문서 확인 권장).
</details>

<details><summary>Q3. 포화 누산 FIR 을 SIMD 로 어떻게 나누나? 탭 방향이면 왜 안 되나?</summary>

포화 덧셈은 결합법칙이 없다: `sat(sat(a+b)+c) ≠ sat(a+sat(b+c))`. 탭을 lane 에 나누면 누적 순서가 바뀌어 포화가 일어나는 입력에서 답이 달라진다(버그 ④: 36,135 출력). 그래서 lane = 출력으로 나눈다 — 각 lane 이 자기 출력의 탭을 명세 순서 그대로 누적한다. 대가는 x 를 탭마다 다시 읽는 것(같은 x 가 8개 출력에 재사용되는 대신 L1 에서 읽음)이다. 꼬리질문 "포화를 안 쓰면 탭 방향도 되나?" → 헤드룸 정리가 성립하면(포화가 일어날 수 없으면) 정확 계산이라 순서 무관 → 탭 방향·트리 리덕션 모두 OK. 증명과 함께 써야 한다.
</details>

<details><summary>Q4. 블록에서 포화가 일어났는지 어떻게 알아내나?</summary>

AArch32/M-profile: APSR.Q(bit 27), AArch64 NEON: FPSR.QC(bit 27). 둘 다 sticky — 포화 명령이 1 로 만들고 소프트웨어가 지울 때까지 남는다(QEMU M4 실측: 011 패턴, M6 실측: 연습 1). 블록 시작에 지우고 끝에 읽으면 "이 블록 어딘가에서 포화"를 한 비트로 안다(어디인지는 모른다). 꼬리질문 "C 에서 그냥 읽으면?" → 컴파일러가 플래그를 모르므로 포화 연산을 지우거나(결과 미사용), 접거나(상수), 옮긴다 — `demo_qc_reorder.c` 실측: -O2 에서 "dead"·"const" 케이스 QC=0, -O0 에선 1. ACLE 는 `__saturation_occurred()` 같은 인터페이스를 두지만(gcc arm_acle.h 에 `__ARM_FEATURE_QBIT` 아래 정의) 컴파일러가 이 의존성을 얼마나 지켜 주는지는 확인 권장 — 확실한 건 asm/noinline 경계다.
</details>

<details><summary>Q5. Cortex-M4 에서 SMLAD(비포화 듀얼 MAC)를 써도 되는 조건은?</summary>

명세가 "Q31 에서 탭마다 포화"라면, 포화가 **한 번도 일어날 수 없다는 증명**이 있을 때만 같다. 충분조건: `|h_k| ≤ 32767`, `Σ|h_k| ≤ 32767` → 모든 부분합 `|2S_j| < 2^31` → 포화 없음 → 정확 합 S 로 `sat16((S + 2^14) >> 15)` 이 명세와 같다(5장 증명, QEMU 테스트 PASS). 꼬리질문 "실행 중에 Q 를 보고 판단하면?" → 안 된다. "cancel" 실측에서 Q=0 인데 결과가 달랐다. SMLAD 의 Q 는 32비트(=명세의 2배 스케일 기준 2^32 범위) 오버플로만 본다. 초기화 때 계수의 L1 노름을 검사해 경로를 고르는 것이 맞다.
</details>

<details><summary>Q6. 같은 펌웨어를 Cortex-M3 에 올리면?</summary>

`SSAT/USAT` 은 ARMv7-M 기본이라 동작하고, `QADD/QDADD/SMULBB/SMLAD/QADD16` 은 DSP 확장(ARMv7E-M: M4/M7)이라 M3 에서 UNDEFINSTR UsageFault(실측: CFSR=0x00010000, UsageFault 비활성 → HFSR.FORCED=0x40000000). 빌드 단계에서 `__ARM_FEATURE_DSP` 로 갈라 M3 용은 `SMULL`/`MLA` + `SSAT` 또는 분기로 포화를 구현한다. 명세가 정수 정의라 결과 비트는 같아야 하고, 그걸 **같은 골든 해시**로 확인한다. ARMv8-M Mainline(M33)은 DSP 확장이 선택 사항이다(문서 확인 권장).
</details>

<details><summary>Q7. 왜 "비트 정확"을 고집하나? 1 LSB 쯤이야?</summary>

(1) 코덱·통신 표준은 기준 C 코드와 비트 단위 일치하는 테스트 벡터로 적합성을 판정한다. (2) 회귀 테스트: 골든 출력 파일과 `cmp` 만으로 검증 가능 — 허용 오차를 정하는 순간 버그가 오차 안에 숨는다(버그 ② 는 1 LSB 차이). (3) 여러 플랫폼(M4 펌웨어, A 코어 리눅스, PC 시뮬레이터)이 같은 결과를 내야 현장 데이터를 PC 에서 재현할 수 있다. 이 레슨은 그 증거로 **맥 NEON/asm 과 QEMU M4 가 같은 FNV-1a 해시 0xc93a6433** 을 낸다. 꼬리질문 "float 모델과는?" → float 와 비트 일치는 불가능하므로 float 모델과는 SNR/최대 오차로, 고정소수점 구현끼리는 비트로 비교하는 2단 구조로 간다.
</details>

<details><summary>Q8. (staff) 필터의 DC 이득이 1.7 이다. Q15 FIR 설계를 어떻게 바꾸겠나? 이 설계가 틀리는 경우는?</summary>

선택지: (a) 계수를 Q14(또는 Q13)로 저장하고 출력에서 1~2비트 왼쪽 시프트 + 포화 — 계수 정밀도 1비트를 내주고 헤드룸을 산다. (b) 누산을 넓힌다: M4 `SMLALD`(64비트), NEON `SMLAL`(int32 lane 은 Q30 합 2^31/2^30 = 2 탭이면 위험하니 int64 lane `SMLAL2 .2d` 나 주기적 비우기) → "끝에서 한 번 포화" 명세로 바꾼다. 단 이건 **다른 명세**라 골든 벡터를 새로 만든다. (c) 블록 부동소수점(block floating point): 블록마다 입력 지수를 정해 정규화. 틀리는 경우: 신호가 실제로 최대 스케일 구형파에 가까우면 (a) 의 출력 포화가 정상 동작이 되고, (b) 는 중간 포화가 사라져 기존 명세와의 비트 일치가 깨진다 — 어느 쪽이 "정답"인지는 시스템 요구(클리핑 허용, 표준 적합성)가 정한다.
</details>

<details><summary>Q9. SQRDMLAH 는 SQADD(acc, SQRDMULH(a,b)) 와 같은가?</summary>

거의 같지만 한 군데가 다르다(연습 2 실측). SQRDMLAH(ARMv8.1 RDM)는 `acc·2^16 + 2ab + 2^15` 를 넓게 계산해 **한 번만** 반올림·포화한다. 두 단계 판은 곱을 먼저 32767 로 포화한 뒤 더한다. a = b = −32768 이고 acc < 0 일 때만 결과가 1 다르다: acc = −1 → 융합 32767, 두 단계 32766. 모든 a × 경계 b × 경계 acc 의 약 793만 조합에서 정확히 5개(acc 가 음수인 경계 5개), 무작위 2^20 에서 예측과 어긋난 경우 0.
</details>

## 9. 추가 연습

1. **블록 포화 감지 (FPSR.QC)** — `int fir_q15_checked(x, n, h, T, y)` 가 블록 FIR 을 돌리고 포화가 있었으면 1 을 돌려준다. QC 의 sticky 동작도 확인하라.
<details><summary>답</summary>

`qc_clear()`(`mrs x0, fpsr; bic x0, x0, #(1<<27); msr fpsr, x0`) → FIR → `qc_get()`(`ubfx x0, x0, #27, #1`). 핵심은 **noinline 경계**다. 처음 버전(인라인)은 -O2 에서 안전/뜨거운 필터 모두 0 을 돌려줘 FAIL 했다 — 결과를 안 쓰는 FIR 이 지워졌고, sticky 확인용 `vqaddq_s16` 은 `qc_get` 뒤로 스케줄됐다(`clang -S` 에서 `bl _qc_clear` 바로 다음에 `bl _qc_get`). `demo_qc_reorder.c` 의 실제 출력:

```text
1 dead  : QC=0
2 const : QC=0  lane0=32767
3 late  : QC=1  lane0=32767
4 fenced: QC=1  z[0]=32767
```

(-O0 에서는 네 줄 모두 QC=1.) "late" 가 이번엔 맞았지만 보장이 아니다. 테스트: 안전 필터 0, 뜨거운 필터 + 구형파 1, 꼬리 경로(n=3) 1, 포화 → 비포화 연속 후 1 유지, 지운 뒤 0.
</details>

2. **SQRDMLAH 융합 vs 두 단계** — `vqrdmlahh_s16(acc,a,b)` 와 `vqaddh_s16(acc, vqrdmulhh_s16(a,b))` 가 다른 입력을 모두 특징지어라.
<details><summary>답</summary>

융합 = `sat16(⌊(acc·2^16 + 2ab + 2^15)/2^16⌋)` = `sat16(acc + ⌊(2ab+2^15)/2^16⌋)`. 두 단계 = `sat16(acc + sat16(⌊(2ab+2^15)/2^16⌋))`. 안쪽 `sat16` 이 일을 하는 건 그 값이 32768 일 때, 즉 a = b = −32768 뿐(Q1). 그때 acc ≥ 0 이면 둘 다 32767 로 포화해 같고, acc < 0 이면 융합 = acc + 32768, 두 단계 = acc + 32767 → **정확히 1 차이**. 테스트: 모든 a × 경계 b 11 × 경계 acc 11, a=b=−32768 에서 acc 전부 65536 개, 무작위 2^20 — 예측식과 어긋남 0.
</details>

3. **값싼 16비트 누산 FIR** — `acc16 = SQADD.8h(acc16, SQDMULH(x, h[k]))`, `y = acc16`. asm 으로 쓰고 Q31 판과의 차이 범위를 증명·측정하라.
<details><summary>답</summary>

`practice.S` 의 `fir16_asm`: `sqdmulh v2.8h, v0.8h, v1.h[0]`(by-element) + `sqadd v4.8h`. 포화가 없으면(Σ|h| ≤ 32767 − T 로 잡음) `fir16 = Σ⌊x h/2^15⌋ ∈ (S − T, S]`, `fir32 = ⌊S + 1/2⌋ ∈ (S − 1/2, S + 1/2]` 이므로 `fir16 − fir32 ∈ [−T, 0]`(정수). 실측: 최대 |d| = 40 LSB(T 최대 64), 평균 d = −8.74 LSB(17,600 출력) — 버림 치우침이 탭 수만큼 쌓인다. 이것이 "곱마다 Q15 로 버리는" 저가 파이프라인을 피하는 이유다.
</details>

4. **float → Q15** — ADC 보정 결과(float)를 Q15 로: 범위 밖은 포화, NaN 은 0.
<details><summary>답</summary>

`vcvtq_n_s32_f32(v, 15)`(FCVTZS 고정소수점 형: ×2^15, 0 쪽 버림, int32 로 포화, NaN → 0) + `vqmovn_s32`(SQXTN: int16 으로 포화). C 기준은 UB 를 피해 `isnan` → 0, `v ≥ 32767` → 32767, `v ≤ −32768` → −32768, 나머지만 `(int32_t)v`. 테스트: 1.0 → 32767, −1.0 → −32768, 0.5 → 16384, ±0.9/32768 → 0, ±Inf, ±NaN, ±1e30, 비정규수, 무작위 비트 패턴 256 × 4099 개 — 불일치 0.
</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-A07_practice && /tmp/ARM-A07_practice   # 11/11 passed
```

## 10. 핵심 정리

- 비트 정확은 **명세를 하나로 고정**하는 데서 시작한다: `L_mult = sat32(2ab)`, `L_mac = sat32(acc + L_mult)`(포화 두 번, 탭 순서 포함), `round = sat32(L + 0x8000) >> 16`.
- Q15 곱이 포화하는 입력은 **(−32768, −32768) 하나뿐**(2^32 전수 실측). SQDMULH 는 버림(−0.5 LSB 치우침), SQRDMULH 는 반올림.
- 포화 덧셈은 결합법칙이 없다 → SIMD 는 **lane = 출력**으로 나눈다. 탭 방향 분할·비포화 덧셈은 약한 테스트를 통과하니 포화를 일으키는 입력으로 검증한다.
- **Σ|h| ≤ 32767 이면 포화가 일어날 수 없다** → 그때만 비포화 `SMLAD` + 끝의 `SSAT` 이 명세와 같다. Q=0 은 비트 일치의 증거가 아니다(cancel: Q=0, 0 vs −32764).
- APSR.Q / FPSR.QC 는 sticky. 컴파일러는 이 플래그를 모른다 → 포화 연산과 플래그 읽기를 noinline/asm 경계 안에 둔다.
- `SSAT` 은 M3 에도 있고, `QADD/QDADD/SMULBB/SMLAD` 는 M4 DSP 확장. M3 에서 실행하면 UNDEFINSTR → HardFault(FORCED).
- 맥 NEON/asm 과 QEMU Cortex-M4 가 같은 골든 해시(0xc93a6433)를 낸다 = 두 ISA 가 같은 필터를 돌린다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — SQDMULH, SQRDMULH, SQDMULL/SQDMLAL, SQADD, SQRSHRN, SQRDMLAH, FPSR.QC 의사코드: https://developer.arm.com/documentation/ddi0487/latest
- ARMv7-M Architecture Reference Manual (DDI 0403) — QADD/QDADD, SSAT, SMULBB/SMLABB, SMLAD, APSR.Q, CFSR/HFSR: https://developer.arm.com/documentation/ddi0403/latest
- Arm C Language Extensions (ACLE) — `__qadd`, `__qdbl`, `__ssat`, `__smlad`, `__smlabb`, `__saturation_occurred`, NEON intrinsics: https://arm-software.github.io/acle/
- Arm Neon Intrinsics Reference: https://developer.arm.com/architectures/instruction-sets/intrinsics/
- ITU-T G.191 Software Tool Library (STL) — ETSI/ITU basic operators (`L_mult`, `L_mac`, `round`): https://www.itu.int/rec/T-REC-G.191 (정의는 원문 확인 권장)
- CMSIS-DSP `arm_fir_q15` / `arm_fir_fast_q15` 문서 (계수 시간 역순 저장, 빠른 버전의 포화 차이): https://arm-software.github.io/CMSIS-DSP/latest/
- 이 트랙: ARM-M06(NEON 기초, lane 배치·SDOT), ARM-M09(예외 프레임), ARM-M12(CFSR/HFSR 해석), ARM-B13/B14(Thumb-2 on QEMU). 다음 단계: DSP 트랙(필터 설계·고정소수점 이론).

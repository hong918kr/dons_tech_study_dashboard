---
id: ARM-M06
time: 80분
updated: 2026-11-15
---
# ARM-M06 NEON 기초: 벡터 덧셈·내적·리덕션

## 학습 목표

- NEON(Advanced SIMD) 레지스터를 **lane 배치(arrangement)** `.16b / .8h / .4s / .2d` 로 읽고, 같은 128비트가 배치에 따라 어떤 값이 되는지 lldb 실측으로 설명한다.
- 같은 커널(float 벡터 덧셈, float 내적, int8 내적)을 **스칼라 C / intrinsics(`arm_neon.h`) / 손 asm(.S)** 세 버전으로 쓰고, 0..67 모든 길이의 꼬리(tail)까지 같은 테스트로 검증한다.
- 가로 리덕션(horizontal reduction)을 고른다: `FADDP` ×2(FADDV 는 없다), `ADDV`, `ADDP d`, `SADDLV`, `FMAXV`. 어셈블러에 실제로 물어봐서 확인한다.
- float 내적이 스칼라와 **비트가 다른 이유**(재결합)를 설명하고, 오차 한계 `γ_n·Σ|a_i b_i|` 로 테스트한다. int8 내적은 int64 참조와 정확히 일치시키고, int32 lane 의 **오버플로 지평선**(n = 524288 에서 감쌈)을 계산·재현한다.
- `SDOT` 의 처리량(GOPS)을 재고, "누산기 1개 = 지연 시간에 묶임"을 숫자로 보인다. 펌웨어 커널(int8 완전연결층, 프리앰블 상관기)에 적용한다.

## 1. 문제

NEON 은 Cortex-A 계열 SoC(Jetson, 자동차 ADAS, 드론 컴패니언 보드, Apple M 칩)에서 신호처리·센서 융합·int8 추론을 돌리는 기본 도구다. 면접에서는 "NEON 으로 dot product 짜 봐", "꼬리는 어떻게 처리해?", "결과가 스칼라랑 왜 달라?", "SDOT 몇 GOPS 나와?"가 이어서 나온다. SSD 컨트롤러 펌웨어의 Cortex-R/A 코어에서도 CRC·XOR 패리티·ECC 보조 계산을 NEON 으로 돌리는 경우가 있다. 레지스터는 ARM-B11 의 그 v0–v31(128비트)이다. 다만 이번에는 스칼라 뷰(`s0`, `d0`)가 아니라 **여러 원소를 한 번에** 다루는 벡터 뷰로 쓴다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">v 레지스터 하나(128비트)를 lane 으로 나누는 4가지 배치 — lldb 실측 (SDOT 1회 직후)</text> <g fill="currentColor" font-size="12" font-family="monospace"><text x="102" y="44">128</text><text x="257" y="44">96</text><text x="402" y="44">64</text><text x="547" y="44">32</text><text x="700" y="44">0</text></g> <text x="10" y="74" fill="currentColor" font-family="monospace">v1.16b</text> <rect x="120" y="56" width="580" height="26" class="f-mut" stroke="currentColor"/> <line x1="156.2" y1="56" x2="156.2" y2="82" stroke="currentColor"/> <line x1="192.5" y1="56" x2="192.5" y2="82" stroke="currentColor"/> <line x1="228.8" y1="56" x2="228.8" y2="82" stroke="currentColor"/>
<line x1="265.0" y1="56" x2="265.0" y2="82" stroke="currentColor"/> <line x1="301.2" y1="56" x2="301.2" y2="82" stroke="currentColor"/> <line x1="337.5" y1="56" x2="337.5" y2="82" stroke="currentColor"/> <line x1="373.8" y1="56" x2="373.8" y2="82" stroke="currentColor"/> <line x1="410.0" y1="56" x2="410.0" y2="82" stroke="currentColor"/> <line x1="446.2" y1="56" x2="446.2" y2="82" stroke="currentColor"/> <line x1="482.5" y1="56" x2="482.5" y2="82" stroke="currentColor"/>
<line x1="518.8" y1="56" x2="518.8" y2="82" stroke="currentColor"/> <line x1="555.0" y1="56" x2="555.0" y2="82" stroke="currentColor"/> <line x1="591.2" y1="56" x2="591.2" y2="82" stroke="currentColor"/> <line x1="627.5" y1="56" x2="627.5" y2="82" stroke="currentColor"/> <line x1="663.8" y1="56" x2="663.8" y2="82" stroke="currentColor"/> <text x="138.1" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">16</text> <text x="174.4" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">15</text>
<text x="210.6" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">14</text> <text x="246.9" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">13</text> <text x="283.1" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">12</text> <text x="319.4" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">11</text> <text x="355.6" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">10</text> <text x="391.9" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">9</text> <text x="428.1" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">8</text>
<text x="464.4" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">7</text> <text x="500.6" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">6</text> <text x="536.9" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">5</text> <text x="573.1" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">4</text> <text x="609.4" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">3</text> <text x="645.6" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">2</text> <text x="681.9" y="74" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<text x="10" y="120" fill="currentColor" font-family="monospace">v0.8h</text> <rect x="120" y="102" width="580" height="26" fill="none" stroke="currentColor"/> <line x1="192.5" y1="102" x2="192.5" y2="128" stroke="currentColor"/> <line x1="265.0" y1="102" x2="265.0" y2="128" stroke="currentColor"/> <line x1="337.5" y1="102" x2="337.5" y2="128" stroke="currentColor"/> <line x1="410.0" y1="102" x2="410.0" y2="128" stroke="currentColor"/> <line x1="482.5" y1="102" x2="482.5" y2="128" stroke="currentColor"/>
<line x1="555.0" y1="102" x2="555.0" y2="128" stroke="currentColor"/> <line x1="627.5" y1="102" x2="627.5" y2="128" stroke="currentColor"/> <text x="156.2" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">-1</text> <text x="228.8" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">-116</text> <text x="301.2" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">-1</text> <text x="373.8" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">-84</text> <text x="446.2" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
<text x="518.8" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">26</text> <text x="591.2" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="663.8" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">10</text> <text x="10" y="166" fill="currentColor" font-family="monospace">v0.4s</text> <rect x="120" y="148" width="580" height="26" class="f-acc" stroke="currentColor"/> <line x1="265.0" y1="148" x2="265.0" y2="174" stroke="currentColor"/> <line x1="410.0" y1="148" x2="410.0" y2="174" stroke="currentColor"/>
<line x1="555.0" y1="148" x2="555.0" y2="174" stroke="currentColor"/> <text x="192.5" y="166" fill="currentColor" text-anchor="middle" font-family="monospace">-116</text> <text x="337.5" y="166" fill="currentColor" text-anchor="middle" font-family="monospace">-84</text> <text x="482.5" y="166" fill="currentColor" text-anchor="middle" font-family="monospace">26</text> <text x="627.5" y="166" fill="currentColor" text-anchor="middle" font-family="monospace">10</text> <text x="10" y="212" fill="currentColor" font-family="monospace">v0.2d</text> <rect x="120" y="194" width="580" height="26" fill="none" stroke="currentColor"/>
<line x1="410.0" y1="194" x2="410.0" y2="220" stroke="currentColor"/> <text x="265.0" y="212" fill="currentColor" text-anchor="middle" font-family="monospace">0xffffff8cffffffac</text> <text x="555.0" y="212" fill="currentColor" text-anchor="middle" font-family="monospace">0x0000001a0000000a</text> <text x="120" y="242" fill="currentColor" font-size="12">← lane 15 / 7 / 3 / 1 (상위)</text> <text x="700" y="242" fill="currentColor" font-size="12" text-anchor="end">lane 0 (하위) = 메모리 첫 원소 →</text> <text x="10" y="270" fill="currentColor">같은 128비트를 int16 로 읽으면 -84 가 (-84, -1) 두 조각이 된다: -84 = 0xFFFFFFAC → 하위 0xFFAC = -84, 상위 0xFFFF = -1.</text> <text x="10" y="292" fill="currentColor">v1 은 a = {1..16} 을 ld1 {v1.16b} 로 읽은 것 (lldb hex: 0x100f0e…0201, 리틀 엔디언이라 원소 0 이 최하위 바이트).</text>
</svg>
```

- 이름 뒤 `.4s` 가 **배치**다: "32비트(single) 원소 4개". `.16b` = 바이트 16개, `.8h` = 하프워드 8개, `.2d` = 64비트 2개. 명령은 같아도 배치가 연산 단위를 정한다: `add v0.4s` 는 32비트 덧셈 4번, `add v0.16b` 는 8비트 덧셈 16번이다.
- lane 0 은 **최하위** 비트 쪽이고, `ld1` 로 읽으면 메모리의 첫 원소가 들어간다(리틀 엔디언).
- 맥의 `objdump`/lldb 는 Apple 문법으로 `sdot.4s v0, v1, v2` 처럼 배치를 명령 이름 뒤에 붙여서 보여준다. 같은 명령이다.

| 버전 | 어디에 | 무엇을 보나 |
|---|---|---|
| 1. 스칼라 C | `*_scalar` (주어짐) | 기준선. `#pragma clang loop vectorize(disable)` 로 **스칼라로 남게** 하고 asm 으로 확인 |
| 2. intrinsics | `*_neon` (TODO, C) | `vld1q_f32`, `vfmaq_f32`, `vaddvq_f32`, `vdotq_s32`, `vaddvq_s32` |
| 3. 손 asm | `*_asm` (TODO, .S) | `ld1 {v.4s}`, `fmla`, `faddp`, `sdot`, `addv`, 꼬리 루프 |

### Part A — 세 커널, 세 버전

- `void vadd_f32_*(float *dst, const float *a, const float *b, size_t n)` — `dst[i] = a[i] + b[i]`. 4개씩 벡터로, 남은 0..3개는 스칼라로. `dst == a`(제자리) 허용.
- `float dot_f32_*(const float *a, const float *b, size_t n)` — lane 4개에 `FMLA` 로 모은 뒤 가로 합, 꼬리는 `FMADD`.
- `int32_t dot_s8_*(const int8_t *a, const int8_t *b, size_t n)` — 16바이트씩 `SDOT`, 끝에 `ADDV`, 꼬리는 `LDRSB` + `MADD`. 계약: `n ≤ 131071`(결과가 int32 에 들어가는 최대 길이, 아래 Part B).
- intrinsic 버전은 C 에서, asm 버전은 `.S` 에서. 스칼라 버전은 주어진다.

### Part B — 꼬리, 오차, 버그, 오버플로 지평선

- 모든 길이 n = 0..67 × 시작 오프셋(16바이트 정렬 깨기) × 특수값(±Inf, NaN, −0, 비정규수)을 같은 테스트 함수(`suite_*`)로 돌린다. dst 앞뒤 canary 로 범위 밖 쓰기도 잡는다.
- float 내적은 비트 비교가 아니라 **오차 한계**로 비교한다(5장). 작은 정수 입력에서는 어떤 순서든 정확하므로 비트까지 같아야 한다.
- `shared_bugs.S` 의 일부러 틀린 함수 5개 — 꼬리 누락, 리덕션 반쪽, `UDOT`, 꼬리 `LDRB`, lane 오버플로 — 를 **테스트가 잡는지** 확인한다. "0..127 만 넣는 약한 테스트는 UDOT 버그를 못 잡는다"도 CHECK 로 박아 둔다.
- `int64_t dot_s8_long_asm(a, b, n)` — 어떤 n 이든 정확. int32 lane 을 **32767 회 SDOT 마다** `SADALP` 로 int64 누산기에 비운다. 비우지 않는 버전은 n = 524288 에서 틀리는 것을 보인다.

### Part C — 펌웨어 커널

- `size_t fc_s8_argmax(W, x, bias, y, rows, cols)` — 키워드 인식(KWS) 모델의 마지막 int8 완전연결층(fully-connected layer). `y[r] = bias[r] + W[r,:]·x`, 가장 큰 행 번호를 돌려준다. `cols = 250` 이라 행마다 시작 주소가 16바이트 정렬이 아니다.
- `size_t best_lag_f32(x, y, n, max_lag)` — 두 마이크 사이 지연(TDOA) 또는 무선 프리앰블 동기화. `corr(L) = Σ x[i]·y[i+L]` 이 최대인 L. `dot_f32_asm` 을 비정렬 포인터 `y + L` 로 41번 부른다.

## 2. Problem (English)

> **NEON basics: vector add, dot product, reduction (AArch64).**
>
> Implement each kernel twice — with `arm_neon.h` intrinsics and in hand-written AArch64 assembly — and validate both against the provided scalar C versions:
>
> 1. `void vadd_f32(float *dst, const float *a, const float *b, size_t n)` — element-wise add; `dst` may equal `a`.
> 2. `float dot_f32(const float *a, const float *b, size_t n)` — 4-lane `FMLA` accumulation, horizontal reduction, scalar tail. The result need not be bit-identical to a sequential loop, but must satisfy `|result − exact| ≤ γ_n · Σ|a_i·b_i|`, `γ_n = n·u/(1 − n·u)`, `u = 2^-24`.
> 3. `int32_t dot_s8(const int8_t *a, const int8_t *b, size_t n)` using `SDOT` (FEAT_DotProd), exact for `n ≤ 131071`.
> 4. `int64_t dot_s8_long(const int8_t *a, const int8_t *b, size_t n)` — exact for any `n`; derive when an int32 lane can overflow and flush it in time.
> 5. Firmware: an int8 fully-connected layer with argmax, and a cross-correlation lag search built on your `dot_f32`.
>
> Constraints: any `n` (including 0 and non-multiples of 4/16), any alignment, no reads or writes outside `[p, p+n)`, leaf functions, do not touch `v8–v15`. Must build with plain `cc -O2` (no `-march` flags).
>
> Examples: `dot_s8` of 16 × (−128·−128) = 262144; `dot_s8` with `n = 131071` all −128 = 2147467264; `dot_s8_long` with `n = 524288` all −128 = 8589934592 (a single never-flushed int32 accumulator returns −8589934592).
>
> Follow-ups: Why is there no `FADDV`? How many accumulators do you need and why? What GOPS do you get and what bounds it? What do you do on a core without dot-product instructions?

## 3. 시작 코드

`starter.c` = 테스트 하네스 + 스칼라 버전(완성) + intrinsic 버전·Part C 의 TODO. `starter.S` = asm 버전 TODO(스텁은 0 을 돌려주거나 dst 를 건드리지 않고 바로 `ret`). `shared_bugs.S` = 테스트가 잡아야 하는 버그 5종(TODO 아님).

<!-- code: starter.c -->

<!-- code: starter.S -->

<!-- code: shared_bugs.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S shared_bugs.S -o st && ./st
```

처음 실행 결과 (실제 출력, 앞부분과 끝):

```text
FAIL starter.c:193  suite_vadd(vadd_f32_neon) == 0
FAIL starter.c:194  suite_vadd(vadd_f32_asm) == 0
FAIL starter.c:198  d[0] == 11 && d[3] == 44 && d[4] == 5
…
FAIL starter.c:246  dot_s8_long_asm(a, b, 524288) == 8589934592LL
…
info: dot_f32 n=64: asm != scalar bits in 1000/1000, asm != neon in 0/1000; max |err|/bound = 16777214.9688 (n>=16: 993207.1895)
…
14/59 passed
```

통과하는 14개는 이렇다. 스칼라 기준선 3개, 버그 관련 7개(버그 함수는 완성품이라 처음부터 잡힌다), 스텁의 0 이 우연히 맞는 4개(n = 0 내적, `cols = 0`, `best_lag` 의 D = 0 과 n = 0)다.

관찰용 파일 (테스트와 별개):

```bash
bash demo_insn_check.sh                                    # 리덕션 명령이 실제로 있나 (어셈블러에 물어보기)
cc -std=c11 -g -O0 demo_lanes.c solution.S -o /tmp/ARM-M06_lanes && lldb -b -s demo_lanes.lldb /tmp/ARM-M06_lanes   # → lanes_m6.txt
cc -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_emit.c -o -    # 평범한 C 루프의 자동 벡터화 결과
cc -std=c11 -O2 -DRUN demo_emit.c -o de && ./de                        # 같은 C 함수가 n 에 따라 다른 비트를 낸다
cc -std=c11 -O2 demo_bench.c solution.S practice.S shared_bugs.S -o /tmp/ARM-M06_bench && /tmp/ARM-M06_bench   # → bench_m6.txt
```

## 4. 힌트

<details>
<summary>힌트 1 — 루프 뼈대는 셋 다 같다</summary>

`cmp n, #W; b.lo 꼬리` → 본문(W개씩: 로드 → 연산 → 저장/누적, `n -= W`, `n >= W` 이면 반복) → 리덕션 → 꼬리(1개씩, `cbz` 로 0개 처리). W 는 float 이면 4, int8 이면 16. `ld1 {v0.4s}, [x1], #16` 은 로드와 포인터 증가(post-index)를 한 번에 한다(ARM-B04). 꼬리에서 `subs` + `b.ne` 를 쓰려면 들어가기 전에 `cbz` 로 n = 0 을 걸러야 한다.

</details>

<details>
<summary>힌트 2 — 누산기는 세로로, 가로 합은 맨 끝에 한 번</summary>

루프 안에서는 `fmla v0.4s, v1.4s, v2.4s` 로 lane 별로만 더한다(세로). lane 4개를 하나로 합치는 가로 연산은 비싸고 지연도 길다. 그래서 루프가 끝난 뒤 한 번만 한다. float 은 `faddp v0.4s, v0.4s, v0.4s` → `faddp s0, v0.2s`, int32 는 `addv s0, v0.4s`.

</details>

<details>
<summary>힌트 3 — SDOT 이 하는 일</summary>

`sdot v0.4s, v1.16b, v2.16b`: lane k(0..3) 에 대해 `v0.s[k] += Σ_{j=0..3} v1.b[4k+j] · v2.b[4k+j]`. 바이트는 부호 있는 8비트로 곱하고, 곱은 32비트로 넓혀서 더한다. intrinsic 은 `vdotq_s32(acc, a16, b16)`. 이 명령은 FEAT_DotProd 라서 C 함수에는 `__attribute__((target("+dotprod")))`, .S 에는 `.arch_extension dotprod` 가 필요할 수 있다(맥 기본 타깃은 이미 포함).

</details>

<details>
<summary>힌트 4 — float 비교는 오차 한계로</summary>

lane 4개로 나눠 더하면 덧셈 순서가 바뀐다. float 덧셈은 결합법칙이 성립하지 않으므로 결과 비트가 달라진다. "틀렸다"가 아니다. 참조는 double 로 계산한다(float 곱은 double 에서 정확). 허용 오차는 `γ_n Σ|a_i b_i|` 로 둔다. 어떤 덧셈 순서에도 성립하는 한계다.

</details>

<details>
<summary>힌트 5 — int32 lane 은 언제 넘치나</summary>

최악의 곱은 (−128)·(−128) = 16384 다. int32 는 2^31−1 까지이므로 ⌊(2^31−1)/16384⌋ = 131071 개까지 안전하다. SDOT 1회에 lane 하나가 곱 몇 개를 받는지 세면 "몇 회마다 비워야 하나"가 나온다. 비울 때는 `sadalp v3.2d, v0.4s`(int32 쌍 → int64 에 누적) 후 `movi v0.16b, #0`.

</details>

## 5. 풀이

### 0. 기능 확인 — 이 칩에 SDOT 이 있나

```text
$ sysctl hw.optional.arm.FEAT_DotProd hw.optional.arm.FEAT_I8MM hw.optional.neon machdep.cpu.brand_string
hw.optional.arm.FEAT_DotProd: 1
hw.optional.arm.FEAT_I8MM: 1
hw.optional.neon: 1
machdep.cpu.brand_string: Apple M6
```

SDOT/UDOT 은 FEAT_DotProd 다. Armv8.2 에서 선택 사항이고 Armv8.4 부터 필수다(Arm ARM 기준, 확인 권장). Cortex-A55/A76 이후 코어에는 있고, **Cortex-A53/A72 에는 없다**(연습 2 가 그 경우). Apple clang 의 기본 타깃은 `-target-cpu apple-m1`(`cc -###` 실측)이고 `__ARM_FEATURE_DOTPROD 1` 이 이미 정의되어 있다. 하지만 리눅스 generic `-march=armv8-a` 는 포함하지 않는다. `demo_insn_check.sh` 실측:

```text
--- 리눅스 generic armv8-a 타깃에서 SDOT (.arch_extension 없이 / 있이):
ERR  instruction requires: dotprod
OK   with .arch_extension dotprod
```

그래서 `.S` 에는 `.arch_extension dotprod`, C 에는 `__attribute__((target("+dotprod")))` 를 붙였다. 체커의 평범한 `cc -O2` 명령과 VM 의 gcc 에서 모두 빌드된다. gcc 는 `"dotprod"` 만 쓰면 `error: arch extension 'dotprod' should be prefixed by '+'` 를 낸다(VM gcc 15 실측). 그래서 `"+dotprod"` 로 썼고, clang 도 이 형식을 받는다. 실제 제품 코드는 런타임 검사를 함께 한다: 리눅스는 `getauxval(AT_HWCAP) & HWCAP_ASIMDDP`, 맥은 위 sysctl.

### Part A — vadd: 한 줄씩 (n = 6)

하네스의 예: `a = {1,2,3,4,5,2^-149}`, `b = {10,20,30,40,−0,2^-149}`.

| # | 명령 | 결과 | 설명 |
|---|---|---|---|
| 1 | `cmp x3, #4` / `b.lo 2f` | 6 ≥ 4 → 본문 | |
| 2 | `ld1 {v0.4s}, [x1], #16` | v0 = {1,2,3,4}, x1 += 16 | 정렬 필요 없음 |
| 3 | `ld1 {v1.4s}, [x2], #16` | v1 = {10,20,30,40} | |
| 4 | `fadd v0.4s, v0.4s, v1.4s` | v0 = {11,22,33,44} | lane 4개 동시에 |
| 5 | `st1 {v0.4s}, [x0], #16` | dst[0..3] 기록 | |
| 6 | `sub` / `cmp x3, #4` / `b.hs 1b` | x3 = 2 → 빠져나감 | |
| 7 | `cbz x3, 4f` | 2 ≠ 0 → 꼬리 | |
| 8 | `ldr s0` / `ldr s1` / `fadd s0` / `str s0` | dst[4] = 5 + (−0) = 5 | x3 = 1 |
| 9 | 같은 4개 | dst[5] = 2^-148 | 비정규수 유지 (FPCR.FZ = 0) |
| 10 | `subs` → 0 / `ret` | | |

원소별 덧셈은 lane 끼리 섞이지 않는다. 그래서 순서가 바뀌어도 결과가 **비트까지** 스칼라와 같고, 테스트도 비트로 비교한다(NaN 끼리는 같다고 본다). AArch64 의 NEON 은 FPCR 을 그대로 따르는 IEEE 연산이다. AArch32 의 NEON(ARMv7)은 float 연산을 항상 flush-to-zero 로 처리했다(Arm ARM 의 "Standard FPSCR value", 확인 권장). 그래서 같은 비정규수 테스트가 32비트 코어에서는 0 을 낼 수 있다.

### Part A — dot_f32: 세로로 모으고, 가로로 한 번

```svg
<svg viewBox="0 0 740 370" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="14" fill="currentColor" font-weight="bold">dot_f32_asm, n = 11: 4-lane 본문 2회 → 가로 리덕션(faddp ×2) → 스칼라 꼬리 3회</text> <rect x="110" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/> <text x="136.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p0</text> <rect x="162" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/> <text x="188.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p1</text> <rect x="214" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/> <text x="240.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p2</text>
<rect x="266" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/> <text x="292.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p3</text> <rect x="318" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/> <text x="344.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p4</text> <rect x="370" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/> <text x="396.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p5</text> <rect x="422" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/>
<text x="448.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p6</text> <rect x="474" y="34" width="52" height="26" class="f-acc" stroke="currentColor"/> <text x="500.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p7</text> <rect x="526" y="34" width="52" height="26" class="f-warn" stroke="currentColor"/> <text x="552.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p8</text> <rect x="578" y="34" width="52" height="26" class="f-warn" stroke="currentColor"/> <text x="604.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p9</text>
<rect x="630" y="34" width="52" height="26" class="f-warn" stroke="currentColor"/> <text x="656.0" y="52" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">p10</text> <text x="10" y="52" fill="currentColor">p_i = a_i·b_i</text> <text x="530" y="31" fill="currentColor" font-size="12">꼬리 (n % 4 = 3)</text> <text x="114" y="31" fill="currentColor" font-size="12">본문 1회차 (i = 0..3)</text> <text x="322" y="31" fill="currentColor" font-size="12">본문 2회차 (i = 4..7)</text> <rect x="60" y="100" width="108" height="28" class="f-acc" stroke="currentColor"/>
<text x="114" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">acc0=p0+p4</text> <line x1="136.0" y1="60" x2="114" y2="100" stroke="currentColor"/> <line x1="344.0" y1="60" x2="114" y2="100" stroke="currentColor"/> <rect x="180" y="100" width="108" height="28" class="f-acc" stroke="currentColor"/> <text x="234" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">acc1=p1+p5</text> <line x1="188.0" y1="60" x2="234" y2="100" stroke="currentColor"/> <line x1="396.0" y1="60" x2="234" y2="100" stroke="currentColor"/>
<rect x="300" y="100" width="108" height="28" class="f-acc" stroke="currentColor"/> <text x="354" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">acc2=p2+p6</text> <line x1="240.0" y1="60" x2="354" y2="100" stroke="currentColor"/> <line x1="448.0" y1="60" x2="354" y2="100" stroke="currentColor"/> <rect x="420" y="100" width="108" height="28" class="f-acc" stroke="currentColor"/> <text x="474" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">acc3=p3+p7</text> <line x1="292.0" y1="60" x2="474" y2="100" stroke="currentColor"/>
<line x1="500.0" y1="60" x2="474" y2="100" stroke="currentColor"/> <text x="560" y="119" fill="currentColor" font-size="12">fmla v0.4s (lane 별 FMA)</text> <rect x="110" y="160" width="140" height="28" fill="none" stroke="currentColor"/> <text x="180" y="179" fill="currentColor" text-anchor="middle" font-family="monospace">acc0+acc1</text> <rect x="350" y="160" width="140" height="28" fill="none" stroke="currentColor"/> <text x="420" y="179" fill="currentColor" text-anchor="middle" font-family="monospace">acc2+acc3</text> <line x1="114" y1="128" x2="180" y2="160" stroke="currentColor"/>
<line x1="234" y1="128" x2="180" y2="160" stroke="currentColor"/> <line x1="354" y1="128" x2="420" y2="160" stroke="currentColor"/> <line x1="474" y1="128" x2="420" y2="160" stroke="currentColor"/> <text x="560" y="179" fill="currentColor" font-size="12" font-family="monospace">faddp v0.4s, v0, v0</text> <rect x="230" y="218" width="140" height="28" class="f-ok" stroke="currentColor"/> <text x="300" y="237" fill="currentColor" text-anchor="middle" font-family="monospace">s0</text> <line x1="180" y1="188" x2="300" y2="218" stroke="currentColor"/><line x1="420" y1="188" x2="300" y2="218" stroke="currentColor"/>
<text x="560" y="237" fill="currentColor" font-size="12" font-family="monospace">faddp s0, v0.2s</text> <rect x="110" y="276" width="520" height="28" class="f-warn" stroke="currentColor"/> <text x="370" y="295" fill="currentColor" text-anchor="middle" font-family="monospace">s0 = fma(a10,b10, fma(a9,b9, fma(a8,b8, s0)))</text> <line x1="300" y1="246" x2="300" y2="276" stroke="currentColor"/> <text x="640" y="295" fill="currentColor" font-size="12" font-family="monospace">fmadd ×3</text> <text x="10" y="334" fill="currentColor">스칼라 C 는 ((((p0 + p1) + p2) + …) + p10) 한 줄 사슬. 덧셈 순서가 달라서 반올림도 달라진다</text> <text x="10" y="356" fill="currentColor">→ 비트 비교 금지, 오차 한계로 비교. (같은 순서로 리덕션하는 intrinsic 버전과는 비트까지 같다: 0/1000 차이)</text>
</svg>
```

리덕션 명령을 고를 때는 "있다고 믿지 말고 어셈블러에 묻는다". `demo_insn_check.sh` 실제 출력:

```text
faddv s0, v0.4s                ERR  invalid operand for instruction
faddp v0.4s, v0.4s, v0.4s      OK   faddp.4s	v0, v0, v0
faddp s0, v0.2s                OK   faddp.2s	s0, v0
fmaxv s0, v0.4s                OK   fmaxv.4s	s0, v0
fmaxv d0, v0.2d                ERR  invalid operand for instruction
addv s0, v0.4s                 OK   addv.4s	s0, v0
addv d0, v0.2d                 ERR  invalid operand for instruction
addp d0, v0.2d                 OK   addp.2d	d0, v0
saddlv d0, v0.4s               OK   saddlv.4s	d0, v0
```

- **FADDV 는 NEON 에 없다.** 에러가 "unknown mnemonic" 이 아니라 "invalid operand" 인 것은 `faddv` 가 SVE 명령(`faddv s0, p0, z0.s`)으로는 존재하기 때문이다. NEON 에서는 `FADDP` 두 번이다. `vaddvq_f32` intrinsic 도 clang 이 정확히 그렇게 내린다(아래).
- `FMAXV/FMINV/FMAXNMV` 는 `.4s` 만 된다(`.2d` 는 에러). 정수 `ADDV` 도 `.2d` 는 없다. 64비트 lane 2개는 `ADDP d0, v0.2d` 로 더한다(`dot_s8_long_asm` 이 이걸 쓴다).
- `SADDLV d0, v0.4s` 는 int32 4개를 int64 로 넓혀서 더한다. 하지만 lane 이 이미 감쌌다면 소용없다(Part B 버그 5).

intrinsic 버전을 clang 이 내린 결과 (`cc -O2 -S solution.c`, 루프와 리덕션만):

```text
_dot_f32_neon:
LBB4_3:
	ldr	q1, [x11], #16          ; vld1q_f32 → ldr q (리틀 엔디언에서는 ld1 {v.4s} 와 같은 결과)
	ldr	q2, [x8], #16
	fmla.4s	v0, v2, v1              ; vfmaq_f32
	...
LBB4_4:
	faddp.4s	v0, v0, v0      ; vaddvq_f32 = faddp ×2
	faddp.2s	s0, v0
```

리덕션 순서가 내 asm 과 같으므로 **intrinsic 버전과 asm 버전은 비트까지 같다**(하네스 info: `asm != neon in 0/1000`). 스칼라와는 1000번 중 822번 다르다.

### Part A — dot_s8: SDOT 한 번을 lldb 로

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="12">sdot v0.4s, v1.16b, v2.16b — 바이트 4개씩 곱해 int32 lane 하나에 더한다 (그림은 메모리 순서: 원소 0 이 왼쪽)</text> <text x="10" y="58" fill="currentColor">v1 (a)</text> <rect x="120" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="138.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="156" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="174.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">2</text> <rect x="192" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/>
<text x="210.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">3</text> <rect x="228" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="246.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">4</text> <rect x="264" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="282.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">5</text> <rect x="300" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="318.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">6</text>
<rect x="336" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="354.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">7</text> <rect x="372" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="390.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">8</text> <rect x="408" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="426.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">9</text> <rect x="444" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/>
<text x="462.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">10</text> <rect x="480" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="498.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">11</text> <rect x="516" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="534.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">12</text> <rect x="552" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="570.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">13</text>
<rect x="588" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="606.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">14</text> <rect x="624" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="642.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">15</text> <rect x="660" y="40" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="678.0" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">16</text> <text x="10" y="94" fill="currentColor">v2 (b)</text>
<rect x="120" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="138.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="156" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="174.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="192" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="210.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="228" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/>
<text x="246.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="264" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="282.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="300" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="318.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="336" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="354.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="372" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="390.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <rect x="408" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="426.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text> <rect x="444" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="462.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text> <rect x="480" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/>
<text x="498.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text> <rect x="516" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="534.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text> <rect x="552" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="570.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text> <rect x="588" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="606.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text>
<rect x="624" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="642.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text> <rect x="660" y="76" width="36" height="26" class="f-mut" stroke="currentColor"/> <text x="678.0" y="94" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-2</text> <text x="10" y="148" fill="currentColor">곱 (int16 폭)</text> <rect x="120" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="138.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="156" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="174.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">2</text> <rect x="192" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="210.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">3</text> <rect x="228" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="246.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">4</text> <rect x="264" y="130" width="36" height="26" fill="none" stroke="currentColor"/>
<text x="282.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">5</text> <rect x="300" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="318.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">6</text> <rect x="336" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="354.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">7</text> <rect x="372" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="390.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">8</text>
<rect x="408" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="426.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-18</text> <rect x="444" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="462.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-20</text> <rect x="480" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="498.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-22</text> <rect x="516" y="130" width="36" height="26" fill="none" stroke="currentColor"/>
<text x="534.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-24</text> <rect x="552" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="570.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-26</text> <rect x="588" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="606.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-28</text> <rect x="624" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="642.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-30</text>
<rect x="660" y="130" width="36" height="26" fill="none" stroke="currentColor"/> <text x="678.0" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">-32</text> <text x="10" y="118" fill="currentColor" font-size="12">×  (lane 별, 부호 있는 8비트)</text> <path d="M122 162 L122 172 L262 172 L262 162" fill="none" stroke="currentColor"/> <line x1="192" y1="172" x2="192" y2="200" stroke="currentColor"/> <path d="M187 194 L192 202 L197 194" fill="none" stroke="currentColor"/> <rect x="120" y="204" width="144" height="28" class="f-acc" stroke="currentColor"/>
<text x="192" y="223" fill="currentColor" text-anchor="middle" font-family="monospace">lane0: 0 + 10</text> <path d="M266 162 L266 172 L406 172 L406 162" fill="none" stroke="currentColor"/> <line x1="336" y1="172" x2="336" y2="200" stroke="currentColor"/> <path d="M331 194 L336 202 L341 194" fill="none" stroke="currentColor"/> <rect x="264" y="204" width="144" height="28" class="f-acc" stroke="currentColor"/> <text x="336" y="223" fill="currentColor" text-anchor="middle" font-family="monospace">lane1: 0 + 26</text> <path d="M410 162 L410 172 L550 172 L550 162" fill="none" stroke="currentColor"/>
<line x1="480" y1="172" x2="480" y2="200" stroke="currentColor"/> <path d="M475 194 L480 202 L485 194" fill="none" stroke="currentColor"/> <rect x="408" y="204" width="144" height="28" class="f-acc" stroke="currentColor"/> <text x="480" y="223" fill="currentColor" text-anchor="middle" font-family="monospace">lane2: 0 + -84</text> <path d="M554 162 L554 172 L694 172 L694 162" fill="none" stroke="currentColor"/> <line x1="624" y1="172" x2="624" y2="200" stroke="currentColor"/> <path d="M619 194 L624 202 L629 194" fill="none" stroke="currentColor"/>
<rect x="552" y="204" width="144" height="28" class="f-acc" stroke="currentColor"/> <text x="624" y="223" fill="currentColor" text-anchor="middle" font-family="monospace">lane3: 0 + -116</text> <text x="10" y="223" fill="currentColor">v0.4s (+=)</text> <line x1="408" y1="232" x2="408" y2="262" stroke="currentColor"/> <path d="M403 256 L408 264 L413 256" fill="none" stroke="currentColor"/> <rect x="336" y="266" width="144" height="28" class="f-ok" stroke="currentColor"/> <text x="408" y="285" fill="currentColor" text-anchor="middle" font-family="monospace">addv s0 → -164</text>
<text x="10" y="285" fill="currentColor">가로 합</text> <text x="10" y="318" fill="currentColor" font-size="12">명령 1개 = 곱 16개 + 덧셈 16개 (= 32 OP). 곱은 int32 로 넓혀 더하므로 lane 안에서는 최악 131071 개 곱까지 안전.</text>
</svg>
```

`demo_lanes.c` 는 `a = {1..16}`, `b = {1×8, −2×8}` 로 `dot_s8_asm` 을 부른다. SDOT 직후의 실제 lldb transcript(`lanes_m6.txt`, 일부):

```text
(lldb) register read -f "int8_t[]" v1 v2
      v1 = {1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16}
      v2 = {1 1 1 1 1 1 1 1 -2 -2 -2 -2 -2 -2 -2 -2}
(lldb) register read -f "int32_t[]" v0
      v0 = {10 26 -84 -116}
(lldb) register read -f "int16_t[]" v0
      v0 = {10 0 26 0 -84 -1 -116 -1}
(lldb) register read -f "uint64_t[]" v0
      v0 = {0x0000001a0000000a 0xffffff8cffffffac}
(lldb) register read -f "float32[]" v0
      v0 = {1.40129846E-44 3.64337601E-44 NaN NaN}
dot_s8_asm = -164
```

lane 0 = 1+2+3+4 = 10, lane 1 = 5+…+8 = 26, lane 2 = −2·(9+…+12) = −84, lane 3 = −2·(13+…+16) = −116. 합은 −164 다. 같은 비트를 float 로 읽으면 10 → 비정규수 1.4e-44, 음수 int32 → NaN 이 된다. lldb 의 `-f` 포맷이 곧 배치다.

꼬리는 반드시 **`ldrsb`(부호 확장)** 로 읽는다. `ldrb` 는 0x80 을 128 로 읽는다(버그 4). clang 이 스칼라 C 를 내린 결과도 `ldrsb w9, [x0], #1` / `madd` 였다.

### 스칼라 버전이 정말 스칼라인가

`#pragma clang loop vectorize(disable) interleave(disable)` 를 붙인 `dot_f32_scalar` 의 실제 출력:

```text
_dot_f32_scalar:
	movi.2d	v0, #0000000000000000
	cbz	x2, LBB1_2
LBB1_1:
	ldr	s1, [x0], #4
	ldr	s2, [x1], #4
	fmadd	s0, s1, s2, s0          ; s += a*b 를 한 식 안에서 fuse (-ffp-contract=on)
	subs	x2, x2, #1
	b.ne	LBB1_1
```

pragma 를 빼면(`demo_emit.c`) clang 은 이렇게 한다. 자세한 분석은 ARM-A11 몫이고 여기서는 결과만 본다:

| 함수 | clang -O2 자동 벡터화 결과 (실측) | 의미 |
|---|---|---|
| `vadd_plain` | `ldp q` ×4 + `fadd.4s` ×4 (16개/회) + 런타임 별칭(alias) 검사 | 원소별이라 자유롭게 벡터화 |
| `dot_s8_plain` | **`sdot.4s` 2개(누산기 2개, 32바이트/회)** + `addv` | 정수 덧셈은 결합법칙 OK |
| `dot_f32_plain` | `fmul.4s` 로 곱만 벡터, **`fadd s0, s0, sN` 16개를 순서대로** | float 합 순서를 못 바꿈 |

float 내적은 컴파일러가 덧셈 순서를 바꿀 권한이 없다(`-ffast-math`/`-fassociative-math` 없이). 그래서 사람이 NEON 으로 써야 빨라진다. 덤으로 실측된 함정이 하나 있다. 벡터 본문은 `fmul`+`fadd`(fuse 안 함)인데 스칼라 나머지 루프는 `fmadd` 다. 그래서 **같은 C 함수가 n 에 따라 다른 반올림**을 한다:

```text
$ cc -std=c11 -O2 -DRUN demo_emit.c -o de && ./de
dot_f32_plain n=2  (스칼라 루프: fmadd)      = 0x1p-24
dot_f32_plain n=16 (벡터 본문: fmul + fadd) = 0x0p+0
```

입력은 ARM-B11 의 fused/unfused 판별 입력이다(나머지 원소 0). 수학적으로 같은 합인데 결과가 다르다.

### Part B — 오차 한계와 실측

Higham(*Accuracy and Stability of Numerical Algorithms*, 3장 식 3.5)에 따르면, 내적을 **어떤 순서로** 계산하든 다음이 성립한다.

`|fl(aᵀb) − aᵀb| ≤ γ_n · Σ|a_i b_i|`, `γ_n = n·u / (1 − n·u)`, float 은 `u = 2^-24`

곱 n개와 덧셈 n−1개가 각 원소에 최대 n번의 반올림을 준다는 데서 나온다. FMA 를 쓰면 반올림이 줄어서 한계 안에 들어간다. 참조값은 double 로 계산한다(24비트×24비트 = 48비트라 곱이 정확하다). double 합의 오차 `γ_n(2^-53)·Σ` 도 허용 오차에 더했다. 실측(solution 실행):

```text
info: dot_f32 n=64: asm != scalar bits in 822/1000, asm != neon in 0/1000; max |err|/bound = 0.8355 (n>=16: 0.0900)
```

- 비트가 다른 경우가 82% 다. 비트 비교 테스트를 쓰면 정답 코드가 "실패"한다.
- 한계 대비 최대 오차가 0.84 인 것은 n = 1 같은 짧은 경우다. 반올림 1번이 한계 `u/(1−u)` 에 거의 닿는다. n ≥ 16 에서는 0.09 로, 한계가 꽤 느슨하다. 리덕션 반쪽 버그(lane 2·3 버림)는 이 한계를 수백만 배 넘어서 확실히 잡힌다.
- VM 의 gcc 15 빌드에서는 811/1000 이었다. gcc `-std=c11` 은 contraction 이 꺼져서 스칼라가 `fmul`+`fadd` 다(ARM-B11 표). 그래도 오차 한계 테스트는 59/59 통과했다.

### Part B — 버그 5종과 테스트

| 버그 (`shared_bugs.S`) | 정답과 다른 줄 | 언제만 틀리나 | 잡는 테스트 |
|---|---|---|---|
| `bug_vadd_notail` | 꼬리 루프 없음 | n % 4 ≠ 0 | n = 0..67 전부 → `suite_vadd > 0` |
| `bug_dot_f32_halfreduce` | `faddp v0.4s` 단계 빠짐 → lane 0+1 만 | n ≥ 4 | 오차 한계 |
| `bug_dot_s8_udot` | `udot` | 음수 바이트가 있을 때 | 전 범위 입력. **0..127 만 넣으면 통과**(CHECK 로 확인) |
| `bug_dot_s8_ldrb` | 꼬리 `ldrb` | 꼬리에 음수가 있을 때 | n % 16 ≠ 0 + 전 범위 입력 |
| `bug_dot_s8_naive64` | lane 을 안 비움 | 최악 입력에서 n ≥ 524288 | 지평선 경계 테스트 |

"음수가 있을 때만", "n % 16 ≠ 0 일 때만"처럼 조건부로 틀리는 버그가 대부분이다. 길이는 0..67(int8 은 16의 배수 ±1 포함), 값은 **전 범위**, 시작 오프셋 0..15 로 테스트를 짜야 하는 이유다.

### Part B — int32 lane 의 오버플로 지평선

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">int32 lane 하나의 값 (입력 전부 -128): SDOT 1회마다 +4×16384 = +65536</text> <line x1="90" y1="180.0" x2="700" y2="180.0" stroke="currentColor"/> <line x1="90" y1="44" x2="90" y2="310" stroke="currentColor"/> <line x1="90" y1="60.0" x2="700" y2="60.0" stroke="currentColor" stroke-dasharray="5 4"/> <text x="84" y="64.0" fill="currentColor" font-size="12" text-anchor="end">2^31−1</text> <line x1="90" y1="300.0" x2="700" y2="300.0" stroke="currentColor" stroke-dasharray="5 4"/> <text x="84" y="304.0" fill="currentColor" font-size="12" text-anchor="end">−2^31</text>
<text x="84" y="184.0" fill="currentColor" font-size="12" text-anchor="end">0</text> <path d="M90.0 180.0 L390.0 60.0" fill="none" stroke="currentColor" stroke-width="3"/> <line x1="390.0" y1="60.0" x2="390.0" y2="300.0" stroke="currentColor" stroke-dasharray="2 3"/> <path d="M390.0 300.0 L690.0 180.0" fill="none" stroke="currentColor" stroke-width="3"/> <path d="M90.0 188.0 L390.0 68.0 L390.0 188.0 L690.0 68.0" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="7 3"/> <text x="103.73291015625" y="214.0" fill="currentColor" font-size="12">굵은 선: bug_dot_s8_naive64 (비우지 않음)</text> <text x="103.73291015625" y="232.0" fill="currentColor" font-size="12">점선: dot_s8_long_asm — 32767회마다</text>
<text x="103.73291015625" y="248.0" fill="currentColor" font-size="12">sadalp 로 int64 에 옮기고 0 으로</text> <text x="382.0" y="290.0" fill="currentColor" font-size="12" text-anchor="end">32768회째 2^31 → −2^31 로 감쌈 (트랩 없음)</text> <text x="390.0" y="322.0" fill="currentColor" font-size="12" text-anchor="middle">32768 회 = n 524288</text> <text x="690.0" y="322.0" fill="currentColor" font-size="12" text-anchor="end">SDOT 횟수 →</text> <text x="10" y="345" fill="currentColor" font-size="12">lane 은 SDOT 1회에 곱 4개를 받는다 → 131071 / 4 = 32767 회까지 안전 (= 524272 바이트 + 꼬리 15).</text>
</svg>
```

| 양 | 값 | 근거 |
|---|---|---|
| 최악의 곱 | (−128)·(−128) = 16384 | 양수 쪽이 더 크다. −128·127 = −16256 |
| int32 lane 하나가 견디는 곱 개수 | ⌊(2^31−1)/16384⌋ = **131071** | 131071·16384 = 2147467264 ≤ 2147483647 |
| SDOT 1회에 lane 하나가 받는 곱 | 4 (바이트 4k..4k+3) | 그림 2 |
| 비우기 전 최대 SDOT 횟수 | ⌊131071/4⌋ = **32767** 회 = 524272 바이트 | `dot_s8_long_asm` 의 블록 |
| `int32_t` 반환 계약 | n ≤ 131071 | lane 이 아니라 **최종 합**이 int32 에 들어가야 함 |
| 비우지 않는 int64 버전이 틀리는 첫 n | **524288** (= 32768 회) | lane = 131072·16384 = 2^31 → −2^31 |

실제 테스트 결과(모두 PASS): `dot_s8_long_asm(n=524287) = 8589918208`, `bug_dot_s8_naive64(n=524287) = 8589918208`(지평선 바로 앞이라 아직 맞음), `dot_s8_long_asm(n=524288) = 8589934592`, **`bug_dot_s8_naive64(n=524288) = −8589934592`**. lane 4개가 각각 −2^31 이 되고, `SADDLV` 가 그것을 int64 로 정확하게(!) 더해서 −2^33 이 나온다. 이 값을 C 에서 int32 로 계산하면 UB(부호 있는 오버플로)다. 그래서 테스트는 asm 결과를 손으로 쓴 기댓값과 비교한다. ISA 에서 SDOT 의 lane 덧셈은 모듈로 2^32 로 정의되어 있고 트랩이 없다.

비우는 비용: 32767 회마다 `sadalp` + `movi` 2개다. 본문 대비 무시할 수준이다. 블록 대신 SDOT 마다 `saddw`/`sadalp` 를 쓰면 정확하지만 명령이 두 배가 된다.

### Part C — 펌웨어 커널

```c
for (size_t r = 0; r < rows; r++) {                    /* fc_s8_argmax */
    y[r] = bias[r] + dot_s8_asm(W + r * cols, x, cols); /* cols=250: W 행 시작이 16B 정렬 아님 → ld1 은 괜찮다 */
    if (y[r] > y[best]) best = r;                       /* > 라서 동점이면 앞 행 */
}
```

- 12행 × 250열, 20회: int64 참조와 `y` 전체 `memcmp` 일치, argmax 일치. 홀수 회차에는 한 행을 x 와 똑같이 심어서 그 행이 1등인지도 확인한다(Σx² 가 다른 행의 무작위 내적보다 훨씬 크다). `cols = 0` 이면 `y = bias`.
- 실제 KWS/TinyML 의 int8 층(CMSIS-NN, TFLite Micro, Arm Compute Library 류)도 핵심은 이 int32 누산 내적이다. 그 뒤에 requantize(고정소수점 곱 + shift + 포화)가 붙는다. 그 부분은 ARM-A07 의 포화·고정소수점 명령 영역이다.
- `best_lag_f32`: ±1 의사잡음 250개(프리앰블)를 D 샘플 늦추고 0.8배로 줄인 뒤 ±0.3 잡음을 더했다. D ∈ {17, 0, 40, 3, 29} 를 전부 맞힌다. 정답 lag 의 상관값은 약 0.8·250 = 200, 다른 lag 는 ±0.8 짜리 무작위 항 250개의 합이라 표준편차가 약 0.8·√250 ≈ 13 이다. 여유가 크다. 그래서 float 재결합 오차가 argmax 를 바꿀 수 없다. 비교 대상이 이렇게 크게 떨어져 있을 때만 float 결과로 결정을 내려도 안전하다.

### 처리량 측정 (demo_bench.c)

샘플 하나는 약 32M 원소를 처리하고, 샘플 11개의 중앙값을 낸다. 이것을 5회 실행해 다시 중앙값을 냈다(`bench_m6.txt`). **측정 중 load average 가 42~47** 이었다(다른 작성 에이전트 10개가 동시 실행). QoS USER_INTERACTIVE 는 힌트일 뿐이라 어느 코어(Super/Performance/Efficiency)에서 돌았는지 알 수 없다. 절대값보다 **비율**을 보라.

| vadd_f32 GFLOP/s | scalar | auto | neon | asm |
|---|---|---|---|---|
| n = 1K | 2.28 | 14.35 | 8.78 | 9.21 |
| n = 64K | 2.16 | 9.26 | 8.69 | 8.75 |
| n = 4M (48 MB) | 2.00 | 6.76 | 4.74 | 6.68 |

| dot_f32 GFLOP/s | scalar | auto | neon | asm | asm4 (연습 1) |
|---|---|---|---|---|---|
| n = 1K | 1.93 | 2.90 | 10.39 | 8.14 | **29.96** |
| n = 64K | 1.94 | 2.90 | 7.86 | 7.71 | 24.85 |
| n = 4M (32 MB) | 1.92 | 2.97 | 7.12 | 8.76 | 20.18 |

| dot_s8 GOPS | scalar | auto (sdot ×2) | neon | asm | nodot (연습 2) |
|---|---|---|---|---|---|
| n = 1K | 5.22 | **118.36** | 66.60 | 63.77 | 34.43 |
| n = 64K | 5.06 | 75.92 | 37.63 | 39.24 | 20.12 |
| n = 4M (8 MB) | 4.66 | 69.81 | 37.01 | 36.39 | 17.93 |

읽는 법 (해석은 추정, 확인 권장):

1. **누산기 1개는 지연 시간에 묶인다.** `dot_f32_asm` 의 FMLA 는 모두 v0 하나에 쌓인다. 그래서 다음 FMLA 가 앞 결과를 기다린다. 누산기 4개(`asm4`)로 바꾸면 1K 에서 3.7배가 된다. int8 도 같다: clang 의 자동 벡터화 버전(SDOT 누산기 2개)이 내 1개짜리보다 1.9배 빠르다. 스칼라 FMA 사슬(1.93) 대비 1-누산기 벡터(8.14)가 약 4배인 것도 맞아떨어진다. 둘 다 같은 지연에 묶여 있고, 벡터는 lane 이 4개다. 지연 사이클 수를 직접 재는 법은 ARM-M03 이다.
2. **n = 4M 은 메모리 대역폭에 묶인다.** vadd asm 6.68 GFLOP/s × 12 B = 약 80 GB/s, dot_f32 asm4 20.18/2 × 8 B = 약 81 GB/s, dot_s8 auto 69.81/2 × 2 B = 약 70 GB/s. 커널이 달라도 바이트 처리량이 비슷하다. 이 부하 상태에서 이 맥의 스트리밍 한계가 그쯤이라는 뜻이다(캐시 크기·SLC 는 미확인).
3. **vadd 는 처음부터 연산이 아니라 로드/스토어가 일이다.** 원소 하나에 12바이트를 옮기고 덧셈은 1번이다. clang 자동 버전은 16개/회로 풀어서(`ldp q` ×4) 1K 에서 내 4개/회 asm 보다 빠르다. 루프 오버헤드(포인터 증가·비교·분기)가 원소 4개마다 붙기 때문이다.
4. **SDOT vs 넓히기(SMULL+SADALP)**: 같은 누산기 1개 구조에서 SDOT 이 약 1.85배다(63.77 vs 34.43). 명령 수가 16바이트당 1개 vs 4개라서다.
5. 64K 에서 dot_s8 이 떨어진다(입력 2×64 KB). 첫 레벨 데이터 캐시를 넘었을 가능성이 크다(크기 미확인).

### 리눅스/ELF 에서도 같은 파일

`.S` 머리의 `SYM(x)` 매크로는 맥에서는 `_x`, ELF 에서는 `x` + `.type x,%function` + `.size` 가 된다. 끝에는 `.note.GNU-stack` 이 붙는다. Lima VM(aarch64 Ubuntu, `/proc/cpuinfo` 에 `asimddp` 있음)에서 실제로 빌드했다: gcc 15.2 와 clang 21.1 둘 다 starter `14/59` exit 1, solution `59/59`, practice `10/10`. `readelf -s` 에 `vadd_f32_asm 68 FUNC GLOBAL`, `dot_s8_long_asm 100 FUNC GLOBAL` 이 찍혔다. 리눅스는 `fmaf` 때문에 `-lm` 이 필요하다.

## 6. 정답 코드

C 쪽 (intrinsics + Part C):

<!-- code: solution.c diff=starter.c -->

asm 쪽:

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o sol && ./sol
# info: dot_f32 n=64: asm != scalar bits in 822/1000, asm != neon in 0/1000; max |err|/bound = 0.8355 (n>=16: 0.0900)
# 59/59 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c solution.S shared_bugs.S -o san && ./san
# 59/59 passed (sanitizer 보고 0)
```

주의: ASan 은 **asm 안의 메모리 접근을 검사하지 못한다**(계측 코드를 넣을 수 없으므로). asm 의 범위 밖 읽기/쓰기는 하네스의 canary(쓰기)로 잡는다. 읽기 쪽 페이지 경계 검사는 ARM-M07 에서 mmap 가드 페이지로 한다.

## 7. 자주 하는 실수

1. **꼬리를 잊는다.** n = 64, 1024 처럼 4/16 의 배수로만 테스트하면 통과한다(`bug_vadd_notail`). → 길이 0..67 을 전부 돌린다. 꼬리는 `cbz` 로 0개를 먼저 거르고 1개씩 처리한다.
2. **리덕션을 반만 한다.** `faddp s0, v0.2s` 하나만 쓰면 lane 0+1 만 더해진다. → `faddp v0.4s, v0.4s, v0.4s` 다음에 `faddp s0, v0.2s`. 또는 intrinsic `vaddvq_f32`.
3. **부호를 잃는다.** `udot` 이나 꼬리 `ldrb` 는 0x80 을 +128 로 읽는다. 0..127 만 쓰는 테스트(이미지 픽셀 같은)로는 안 보인다(실측 CHECK). → int8 은 `sdot` / `ldrsb` / `vld1q_s8`. 전 범위 + 음수 꼬리로 테스트한다.
4. **float 내적을 비트 비교한다.** 1000번 중 822번 "실패"가 나온다. 반대로 허용 오차를 감으로(`1e-3`) 잡으면 진짜 버그(리덕션 반쪽)를 놓칠 수 있다. → `γ_n Σ|a_i b_i|` 처럼 근거 있는 한계를 쓰고, 작은 정수 입력으로 정확성도 따로 본다.
5. **int32 누산기를 무한히 믿는다.** int8×int8 은 곱이 작아 보여도 lane 하나가 131071 개 곱에서 끝난다. 반환형만 int64 로 바꾸는 것(`bug_dot_s8_naive64`)은 해결이 아니다. → 지평선을 계산해서 블록마다 `sadalp` 로 비운다.
6. **누산기를 하나만 쓴다(성능 실수).** 정답이지만 FMLA/SDOT 지연에 묶여 3.7배를 버린다. → 독립 누산기 4개 이상을 쓰고 마지막에 트리로 합친다(연습 1). 필요한 개수는 대략 "지연 × 사이클당 발행 수"다.
7. **기능 플래그 없이 SDOT 을 쓴다.** 리눅스 generic 타깃에서 `instruction requires: dotprod`, gcc 의 `target("dotprod")` 는 `'+'` 접두사 에러가 난다(둘 다 실측). → `.arch_extension dotprod` / `target("+dotprod")` 를 쓰고, 런타임에 HWCAP/sysctl 로 확인한 뒤 디스패치한다.
8. **벤치마크가 아무것도 안 잰다.** 결과를 안 쓰거나, 같은 인자로 부르는 순수 함수를 인라인하면 컴파일러가 반복문 밖으로 빼거나 지운다. → `volatile` 함수 포인터로 부르고 결과를 `volatile` sink 에 더한다(demo_bench.c). 측정에서 v8–v15 를 쓴다면 d8–d15 저장도 잊지 말 것(ARM-B11).

## 8. 면접 Q&A

<details>
<summary>Q1. NEON 레지스터에서 lane 0 은 어디이고, ld1 {v0.4s} 와 ldr q0 는 같은가?</summary>

lane 0 은 최하위 비트 쪽이고, 메모리의 첫 원소가 들어간다. 리틀 엔디언에서는 `ld1 {v0.4s}, [x0]` 과 `ldr q0, [x0]` 의 결과가 같다. clang 은 `vld1q_f32` 를 `ldr q` 로 내렸다(실측). 빅 엔디언에서는 다르다. `ld1` 은 원소 단위로 엔디언을 바꾸고 `ldr q` 는 128비트 하나로 다룬다(Arm ARM 기준, 확인 권장). 둘 다 정렬을 요구하지 않는다(Normal 메모리, SCTLR.A = 0 일 때). 다만 캐시 라인을 걸치는 접근은 코어에 따라 조금 느릴 수 있다. 하네스는 오프셋 0..15 로 비정렬도 확인했다.

꼬리: "`ld1 {v0.4s-v3.4s}` 는?" → 연속된 레지스터 4개에 64바이트를 한 번에 읽는다(연습 1). `ld2/ld3/ld4` 는 구조체 배열을 deinterleave 한다. 예: RGB 바이트 → R, G, B 레지스터 각각.

</details>

<details>
<summary>Q2. 왜 FADDV 가 없나? 가로 합은 어떻게 하나?</summary>

AArch64 NEON 에는 정수 `ADDV`(`.4s` 까지, `.2d` 없음)와 `FMAXV/FMINV/FMAXNMV/FMINNMV`(`.4s`)는 있다. 하지만 float 덧셈 가로 합 `FADDV` 는 없다(어셈블러 실측: invalid operand. SVE 에는 있다). float 은 `FADDP` 를 두 번 쓴다. 가로 연산은 lane 사이로 데이터를 옮겨야 해서 세로 연산보다 지연이 길다. 그래서 원칙은 **루프 안에서는 세로(lane 별)로만 누적하고, 가로 합은 끝에 한 번**이다. 덤으로 float 가로 합의 순서((a0+a1)+(a2+a3))는 명령이 정한다. 그래서 같은 리덕션을 쓰는 intrinsic 버전과 asm 버전이 비트까지 같았다. SVE 의 `FADDA` 는 순서대로 더하는 (느린) 버전이다.

</details>

<details>
<summary>Q3. NEON 내적 결과가 스칼라 C 와 다르다. 버그인가?</summary>

대부분 아니다. lane 4개로 나눠 더하면 덧셈 순서가 바뀌고, float 덧셈은 결합법칙이 성립하지 않는다. 어떤 순서든 오차는 `γ_n Σ|a_i b_i|` 안에 있어야 하고, 그것을 테스트한다. 실측으로 822/1000 이 비트로 달랐고 최대 오차는 한계의 0.84배(n ≥ 16 에서는 0.09배)였다. 같은 이유로 컴파일러는 `-ffast-math`(정확히는 `-fassociative-math`) 없이는 float 합을 벡터화하지 못한다. clang -O2 는 곱만 벡터로 하고 `fadd` 16개를 순서대로 남겼다(실측, 2.9 GFLOP/s). 순서를 바꿔도 된다고 **사람이** 판단하고 NEON 으로 쓰는 것이 핵심이다. 재현성이 필요하면(예: 골든 모델과 비트 비교) 리덕션 순서를 스펙으로 고정하고 C 참조도 같은 순서로 짠다.

</details>

<details>
<summary>Q4. SDOT 하나는 몇 OP 이고, 측정한 GOPS 는 무엇에 묶였나?</summary>

`sdot v.4s, v.16b, v.16b` 1개 = 곱 16 + 덧셈 16 = 32 OP(MAC 16개). 1K 에서 내 asm 은 63.8 GOPS, 즉 초당 약 2.0 G 개의 SDOT 이다. 누산기가 하나뿐이라 SDOT 들이 서로를 기다린다. 누산기 2개인 clang 버전은 118 GOPS 였다. 최고치에 가려면 **필요 누산기 수 ≈ 명령 지연(사이클) × 사이클당 SDOT 발행 수**(리틀의 법칙)만큼 독립 사슬이 있어야 한다. 이 칩의 정확한 지연·발행 폭은 이 레슨에서 재지 않았다(ARM-M03 방법으로 측정 가능). 4M 에서는 약 70 GB/s 의 메모리 대역폭이 상한이었다. 연산 강도가 2 OP/바이트로 낮아서 큰 데이터는 결국 대역폭 싸움이다.

꼬리: "int8 행렬곱이라면?" → 같은 데이터를 여러 번 재사용하도록 타일링해서 연산 강도를 올린다. 이 칩에는 FEAT_I8MM(`SMMLA`, 2×8 · 8×2 블록)도 있다(sysctl = 1). 그것은 행렬 블록용이다.

</details>

<details>
<summary>Q5. int8 내적에서 int32 누산기가 넘칠 수 있나? 언제?</summary>

있다. 최악의 곱은 (−128)² = 16384 이므로 lane 하나는 131071 개 곱까지 안전하다. SDOT 은 lane 하나에 1회당 4개를 넣는다. 그래서 32767 회(524272 바이트)마다 int64 로 비워야 한다. 비우지 않으면 n = 524288 최악 입력에서 lane 이 2^31 → −2^31 로 감싸고, 결과는 +2^33 대신 −2^33 이 된다(실측). 하드웨어는 트랩하지 않는다. 실무에서는 입력 길이 상한(층의 cols)이 보통 이보다 훨씬 작다. 그래서 "길이 제한을 문서화 + assert" 가 흔한 답이다. 대신 **누가 그 숫자를 계산했는가**를 물어볼 것이다. 넓히기 경로(연습 2 의 SMULL→int16)는 곱 하나가 int16 에 들어가는지(16384 ≤ 32767)도 따로 확인해야 한다.

</details>

<details>
<summary>Q6. SDOT 이 없는 코어(Cortex-A53/A72)에서는?</summary>

`SMULL/SMULL2`(int8×int8 → int16 8개) + `SADALP`(int16 쌍 → int32 lane 누적)로 같은 결과를 만든다(연습 2, 64K 에서 SDOT 대비 약 0.5배). 빌드·배포는 이렇게 한다. ① 컴파일 타임에 `__ARM_FEATURE_DOTPROD` 매크로로 갈라 빌드한다. ② 런타임에 리눅스 `getauxval(AT_HWCAP) & HWCAP_ASIMDDP`, 맥 `sysctl hw.optional.arm.FEAT_DotProd` 로 확인하고 함수 포인터로 디스패치한다. ③ gcc/clang 의 `target_clones`/function multiversioning 을 쓴다(지원 범위는 컴파일러 버전별로 확인). 잘못 디스패치하면 SDOT 이 없는 코어에서 SIGILL(UNDEFINED) 이 난다.

</details>

<details>
<summary>Q7. Cortex-M 에서도 NEON 을 쓸 수 있나?</summary>

없다. NEON(Advanced SIMD)은 A-profile 기능이다(일부 R-profile 코어도 가짐, 예: Cortex-R52 — 확인 권장). Cortex-M4/M7 은 **32비트 범용 레지스터 안의 SIMD**(DSP 확장)만 있다. 예: `SMLAD` 는 int16 쌍 두 개를 곱해 누산하고, `SADD8` 은 바이트 4개를 더한다. CMSIS-DSP/NN 이 이것을 쓴다. Cortex-M55/M85 는 **Helium(MVE)** 이 있다. 128비트 벡터를 FP 레지스터 위에서 "beat" 단위로 나눠 실행하며, NEON 과 비슷하지만 다른 ISA 다. 꼬리 predication(`VCTP`, low-overhead loop `LETP`)이 있어서 스칼라 꼬리 루프가 필요 없다. 면접 답: "같은 int8 KWS 커널이 A-코어에서는 SDOT, M4/M7 에서는 SMLAD, M55 에서는 Helium `VMLADAV`."

</details>

<details>
<summary>Q8. 꼬리 처리 방법은 스칼라 루프 말고 뭐가 있나?</summary>

① 스칼라 꼬리 — 이 레슨의 방법. 단순하고 정확하지만 n 이 작으면 비중이 크다. ② **겹치는 마지막 벡터** — `vadd` 처럼 같은 원소를 두 번 써도 결과가 같은(idempotent) 연산은 마지막 4개를 `p+n−4` 에서 한 번 더 처리한다. 단 n ≥ 4 여야 하고, 제자리(`dst == a`)면 이미 바뀐 값을 다시 읽어서 틀린다. 내적처럼 누적하는 연산은 두 번 세면 안 되므로 마스킹이 필요하다. ③ 버퍼를 16의 배수로 패딩하고 0으로 채운다. 펌웨어에서 버퍼를 직접 할당한다면 가장 싸다. ④ SVE/Helium 의 predication(`whilelt` / `VCTP`). ⑤ 정렬된 16B 읽기는 페이지를 넘지 않는다는 성질로 끝을 넘겨 읽고 마스킹한다 — libc 가 쓰는 방법이고, ARM-M07 의 영역이다.

</details>

## 9. 추가 연습

1. **누산기 4개.** `float dot_f32_asm4(a, b, n)` — `ld1 {v4.4s-v7.4s}` 로 16개씩, 독립 FMLA 사슬 4개, 남은 4개 묶음, 트리 합, 스칼라 꼬리. 같은 오차 한계 테스트를 통과하고, 벤치마크에서 1-누산기 대비 몇 배인지 재 보라.
2. **SDOT 없는 int8 내적.** `int32_t dot_s8_nodot(a, b, n)` — `SMULL/SMULL2` + `SADALP`. 곱이 int16 에 들어가는지 먼저 증명하라.
3. **피크 검출.** `float maxabs_f32(x, n)` — `fabs` + `fmax` 세로, `fmaxv` 가로(n = 0 → 0). 오디오 클리핑 감지나 ADC 포화 감지에 쓰인다.
4. **SAXPY by-element.** `void saxpy_f32_asm(float *y, float a, const float *x, size_t n)` — `y[i] = fma(a, x[i], y[i])`. `a` 를 `dup` 하지 말고 `fmla v.4s, v.4s, v0.s[0]` 를 쓴다. `fmaf` 참조와 **비트까지** 같아야 한다(왜 이번엔 비트 비교가 맞나?).

<details>
<summary>연습 1 답 — 누산기 4개</summary>

`ld1 {v4.4s, v5.4s, v6.4s, v7.4s}, [x0], #64` 와 b 쪽 `v16–v19` 를 읽고, `fmla v0..v3` 4개를 쓴다. 이 4개는 서로 의존하지 않으므로 코어가 겹쳐서 실행한다. 남은 n % 16 중 4개 묶음은 v0 에 더하고, `fadd v0,v0,v1` / `fadd v2,v2,v3` / `fadd v0,v0,v2` 트리 후 `faddp` ×2, 그다음 스칼라 꼬리다. v16–v19 를 쓴 이유는 v8–v15 를 피하기 위해서다(하위 64비트 callee-saved → 쓰면 저장 필요).

실측(1K): 8.14 → 29.96 GFLOP/s(3.7배). 4M 에서는 20 GFLOP/s 로, 메모리 대역폭 한계(약 80 GB/s)에 닿는다. 오차 한계 테스트는 n = 0..140 × 오프셋 4개에서 통과했고, 작은 정수 입력에서는 비트까지 정확했다.

</details>

<details>
<summary>연습 2 답 — SMULL + SADALP</summary>

|곱| ≤ 128·128 = 16384 ≤ 32767 이므로 int16 에 들어간다. `smull v3.8h, v1.8b, v2.8b`(아래 8바이트) / `smull2 v4.8h, v1.16b, v2.16b`(위 8바이트) → `sadalp v0.4s, v3.8h` / `sadalp v0.4s, v4.8h`. lane k 에는 바이트 {2k, 2k+1, 8+2k, 8+2k+1} 의 곱이 쌓인다. SDOT 과 묶는 방식은 다르지만 lane 4개의 **합**은 같다. lane 당 곱 4개/16바이트라서 지평선도 SDOT 과 같다. n = 131071 전부 −128 → 2147467264 테스트를 통과했다. 16바이트당 명령이 4개(SDOT 은 1개)라서 실측 처리량은 1-누산기 SDOT 의 약 54%(1K: 34.4 vs 63.8 GOPS)였다.

</details>

<details>
<summary>연습 3 답 — fabs / fmax / fmaxv</summary>

누산기를 0 으로 시작한다(|x| ≥ 0 이라 항등원). 본문은 `fabs v1.4s` → `fmax v0.4s, v0.4s, v1.4s`, 끝에 `fmaxv s0, v0.4s`, 꼬리는 `fabs s1` / `fmax s0, s0, s1`. `fmax`/`fabs` 는 반올림이 없으므로 순서와 상관없이 **비트까지** `fmaxf(fabsf)` 참조와 같다. −0 만 있는 입력 → +0, −Inf 포함 → +Inf. NaN 은 계약 밖이다: `fmax` 는 NaN 을 전파하고 `fmaxnm` 은 무시한다(ARM-B11 연습 2). 센서 데이터에 NaN 이 섞일 수 있으면 어느 쪽을 원하는지 먼저 정해야 한다.

</details>

<details>
<summary>연습 4 답 — by-element FMLA</summary>

인자는 `x0 = y`, `s0 = a`, `x1 = x`, `x2 = n` 이다(정수·FP 카운터가 따로라서 n 은 x2, ARM-B11). 본문: `ld1 {v1.4s}, [x0]`(y, 포인터는 아직 그대로) → `ld1 {v2.4s}, [x1], #16` → `fmla v1.4s, v2.4s, v0.s[0]` → `st1 {v1.4s}, [x0], #16`. 꼬리: `fmadd s1, s0, s2, s1`.

이번에는 원소마다 **독립적인 fused 연산 한 번**이다. lane 사이 합이 없으므로 순서 문제가 없고 `fmaf` 와 비트까지 같다. vadd 와 같은 이유다. by-element 형식은 행렬곱·FIR 필터에서 계수 하나를 벡터 전체에 곱할 때 `dup` 명령을 아낀다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o pr && ./pr
# 10/10 passed   (ASan/UBSan 빌드도 10/10, VM gcc·clang 빌드도 10/10)
```

## 10. 핵심 정리

- v0–v31 은 128비트이고 배치 `.16b/.8h/.4s/.2d` 가 lane 폭을 정한다. lane 0 = 최하위 = 메모리 첫 원소다. lldb `register read -f "int32_t[]" v0` 로 lane 을 본다.
- 뼈대: `cmp/b.lo` → 본문(W개씩 `ld1` + 연산, post-index) → 가로 합 한 번 → `cbz` + 스칼라 꼬리. n = 0..67 × 오프셋 × 전 범위 값으로 테스트한다.
- 가로 합: float `faddp` ×2(**FADDV 없음**), int32 `addv s`, int64 `addp d`, 넓히며 `saddlv`, 최대 `fmaxv`(.4s 만). 루프 안에서는 세로로만 누적한다.
- float 내적은 재결합 때문에 비트가 다르다(822/1000). 오차 한계 `γ_n Σ|a_i b_i|` 로 비교하고, 같은 순서의 intrinsic 과는 비트까지 같다. 컴파일러는 fast-math 없이 float 합을 벡터화하지 못한다.
- `SDOT v.4s, v.16b, v.16b` = lane 당 int8 곱 4개 + 누적, 명령당 32 OP. FEAT_DotProd → `.arch_extension dotprod` / `target("+dotprod")` + 런타임 확인. 없으면 `SMULL` + `SADALP`.
- int32 lane 지평선: 131071 곱 = SDOT 32767 회 → `sadalp` 로 int64 에 비운다. 비우지 않으면 n = 524288 에서 +2^33 이 −2^33 이 된다(실측).
- 성능: 누산기 1개는 지연에 묶인다(누산기 4개로 3.7배). 큰 n 은 약 70–80 GB/s 대역폭에 묶인다(부하 중 실측).
- 부호(`sdot`/`ldrsb`), 꼬리, 리덕션 반쪽, 누산기 폭 — 조건부로만 틀리는 버그들이라서 테스트 입력 설계가 곧 실력이다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — SDOT/UDOT (FEAT_DotProd), FMLA (vector, by element), FADDP, ADDV, ADDP (scalar), SADDLV, SADALP, SMULL/SMULL2, FMAXV, LD1 (multiple structures): <https://developer.arm.com/documentation/ddi0487/latest>
- Arm Intrinsics reference (Neon): `vfmaq_f32`, `vaddvq_f32`, `vdotq_s32`, `vaddvq_s32`: <https://developer.arm.com/architectures/instruction-sets/intrinsics/>
- Arm C Language Extensions (ACLE) — `__ARM_FEATURE_DOTPROD`, `arm_neon.h`: <https://github.com/ARM-software/acle>
- N. J. Higham, *Accuracy and Stability of Numerical Algorithms*, 2nd ed., SIAM 2002 — 3장 (내적 오차 한계 γ_n).
- Apple, Determining instruction set characteristics (`hw.optional.arm.FEAT_*`): <https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics>
- Linux kernel, ARM64 ELF hwcaps (`HWCAP_ASIMDDP`): <https://docs.kernel.org/arch/arm64/elf_hwcaps.html>
- Clang, Loop hint pragmas (`#pragma clang loop vectorize(disable)`): <https://clang.llvm.org/docs/LanguageExtensions.html#extensions-for-loop-hint-optimizations>
- Arm, Optimizing C code with Neon intrinsics: <https://developer.arm.com/documentation/102467/latest>
- 관련 레슨: ARM-B04 (post-index 주소 지정), ARM-B10 (스칼라 memcpy/strlen), ARM-B11 (v 레지스터 뷰·FMA·d8–d15), ARM-M03 (지연/처리량 측정), ARM-M07 (NEON 바이트 처리·페이지 경계), ARM-A07 (포화·고정소수점 SIMD), ARM-A11 (자동 벡터화 출력)

---
id: ARM-B11
time: 60분
updated: 2026-11-11
---
# ARM-B11 부동소수점·SIMD 레지스터 기초: V/D/S, FCVT

## 학습 목표

- FP/SIMD 레지스터 파일(v0–v31)의 **뷰(view)** `q/d/s/h/b` 를 그림으로 그리고, "스칼라 `s0` 에 쓰면 v0 의 위쪽 96비트가 0이 된다"를 실제 M6 값으로 확인한다.
- AAPCS64 의 FP 인자 규칙을 쓴다: FP 인자는 `s0..s7`/`d0..d7`, 반환은 `s0`/`d0`, 정수 인자와 **번호를 따로 센다**(NGRN / NSRN). callee-saved 는 `d8..d15` 의 **하위 64비트만**이다.
- `ldr s` / `fmul` / `fmadd` / `fmsub` 로 `dot3`·`lerp` 를 짠다. C 참조는 `a*b+c` 가 아니라 `fmaf` 로 써야 하는 이유(`-ffp-contract`)를 실제 컴파일러 출력으로 설명한다.
- `FCVTZS/FCVTZU`(0 쪽 자름 + 포화 + NaN→0)와 `FCVTNS/MS/PS/AS` 반올림 4형제, `SCVTF/UCVTF`, 고정소수점 `#fbits` 형식을 쓴다. C 에서 UB 인 입력은 손으로 쓴 기댓값으로 검증한다.
- 펌웨어 코드로 옮긴다: Q15/Q16.16 변환, 12비트 ADC → 전압, 콜백을 부르는 EMA 필터(callee-saved `d8/d9` 저장)를 만들고 ABI 검사기로 확인한다.

## 1. 문제

센서 값, PID 게인, 오디오 샘플처럼 펌웨어에도 float 는 많다. 크래시 덤프나 `objdump` 에서 `fcvtzs w0, s0`, `fmadd`, `ucvtf s1, w0, #12` 가 나오면 "이 asm 뭐 하는 코드야?"에 바로 답해야 한다. "NaN 을 int 로 바꾸면 ARM 에서 뭐가 나오나요?"도 Apple·NVIDIA·Tesla 펌웨어 면접의 단골 꼬리질문이다. Cortex-M4F/M7 의 FPU(`s0..s31`, FPSCR)와 AArch64 SoC(SSD 컨트롤러의 Cortex-A/R, Jetson, Apple M 칩)는 명령 이름과 개념이 거의 같다. 그래서 여기서 익힌 것이 둘 다에 쓰인다.

AArch64 에는 정수 레지스터 파일(x0–x30)과 **별도의** FP/SIMD 레지스터 파일(v0–v31, 각 128비트)이 있다. 같은 v0 를 크기별로 부르는 이름이 `q0`(128) · `d0`(64) · `s0`(32) · `h0`(16) · `b0`(8) 이다. 정수의 `x0`/`w0` 와 같은 방식이다. 다만 FP 쪽은 **스칼라 쓰기가 위쪽 전부(최대 120비트)를 0으로 만든다.**

```svg
<svg viewBox="0 0 740 420" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">v0 하나의 다섯 가지 이름 — 그리고 스칼라 쓰기 vs 원소 삽입 (M6 실측 값)</text> <text x="10" y="56" fill="currentColor">x0 / w0</text>
<rect x="410" y="40" width="290" height="26" class="f-mut" stroke="currentColor"/> <line x1="555" y1="40" x2="555" y2="66" stroke="currentColor"/>
<text x="430" y="58" fill="currentColor">x0 상위 32</text><text x="580" y="58" fill="currentColor">w0 = x0[31:0]</text> <text x="10" y="80" fill="currentColor" font-size="12">(정수 레지스터 파일은 따로)</text>
<g fill="currentColor" font-size="12" font-family="monospace">
<text x="118" y="106">127</text><text x="398" y="106">64</text><text x="543" y="106">32</text><text x="617" y="106">16</text><text x="655" y="106">8</text><text x="690" y="106">0</text> </g>
<text x="10" y="128" fill="currentColor">v0 (128비트)</text> <rect x="120" y="112" width="580" height="26" fill="none" stroke="currentColor"/>
<g stroke="currentColor"><line x1="410" y1="112" x2="410" y2="138"/><line x1="555" y1="112" x2="555" y2="138"/><line x1="627" y1="112" x2="627" y2="138"/><line x1="664" y1="112" x2="664" y2="138"/></g>
<g stroke="currentColor" stroke-width="3">
<line x1="120" y1="152" x2="700" y2="152"/><line x1="410" y1="170" x2="700" y2="170"/><line x1="555" y1="188" x2="700" y2="188"/><line x1="627" y1="206" x2="700" y2="206"/><line x1="664" y1="224" x2="700" y2="224"/> </g>
<g fill="currentColor" font-family="monospace">
<text x="10" y="156">q0  (v0 전체)</text><text x="10" y="174">d0  double</text><text x="10" y="192">s0  float</text><text x="10" y="210">h0  half</text><text x="10" y="228">b0  byte</text> </g>
<text x="10" y="262" fill="currentColor">v0 = 전부 1 에서 <tspan font-family="monospace">fmov s0, #1.0</tspan> 후:</text> <rect x="120" y="270" width="290" height="26" class="f-warn" stroke="currentColor"/>
<rect x="410" y="270" width="145" height="26" class="f-warn" stroke="currentColor"/> <rect x="555" y="270" width="145" height="26" class="f-acc" stroke="currentColor"/>
<g fill="currentColor" font-family="monospace"><text x="190" y="288">0000000000000000</text><text x="448" y="288">00000000</text><text x="593" y="288">3F800000</text></g>
<text x="10" y="322" fill="currentColor">v1 = 전부 1 에서 <tspan font-family="monospace">mov v1.s[0], w8</tspan> 후:</text> <rect x="120" y="330" width="290" height="26" class="f-ok" stroke="currentColor"/>
<rect x="410" y="330" width="145" height="26" class="f-ok" stroke="currentColor"/> <rect x="555" y="330" width="145" height="26" class="f-acc" stroke="currentColor"/>
<g fill="currentColor" font-family="monospace"><text x="190" y="348">FFFFFFFFFFFFFFFF</text><text x="448" y="348">FFFFFFFF</text><text x="593" y="348">3F800000</text></g>
<text x="10" y="384" fill="currentColor">스칼라 쓰기(fmov / ldr s / fadd s …)는 위쪽을 0으로 지운다.</text> <text x="10" y="406" fill="currentColor">벡터 원소 삽입(INS, mov v1.s[0], w8)만 나머지 lane 을 보존한다.</text>
</svg>
```

| 레지스터 | AAPCS64 역할 | 누가 보존? |
|---|---|---|
| v0–v7 | FP/SIMD 인자 1~8번째, 반환값(`s0`/`d0`) | 호출자 (caller-saved) |
| v8–v15 | 일반 | **하위 64비트(`d8..d15`)만** 피호출자가 보존. 상위 64비트는 caller-saved |
| v16–v31 | 임시 | 호출자 |
| FPCR / FPSR | 제어(반올림·FZ·DN) / 누적 예외 플래그 | 이 맥에서 둘 다 `0x0` 실측 (RN, FZ=0) |

Apple arm64 도 FP 인자·callee-saved 규칙은 AAPCS64 와 같다. 차이는 가변 인자(전부 스택), `long double` = `double`, 작은 정수 인자의 확장 책임 같은 것들이다(ARM-B01 이 다룬다; Apple 문서 "Writing ARM64 code for Apple platforms" 기준, 확인 권장).

### Part A — 레지스터 뷰, FP 인자, 기본 변환

- `void vreg_views(uint64_t out[4])` — 위 그림을 그대로 실행해서 `v0`/`v1` 의 하위/상위 64비트를 돌려준다.
- `float dot3(const float *a, const float *b)` — `ldr s` / `fmul` / `fmadd` 두 번. `double dot3d(...)` 도 같은 모양으로.
- `float lerp(float a, float b, float t)` = `a + t*(b-a)` — `fsub` + `fmadd`.
- `double mix_args(int32_t i, float f, int64_t l, double d)` = `l*d + i*f` — 인자가 `w0, s0, x1, d1` 로 들어온다(카운터가 둘).
- `f2i_sat`(FCVTZS w,s) · `d2l_sat`(FCVTZS x,d) · `f2u_sat`(FCVTZU) · `i2f/u2f/l2d/ul2d`(SCVTF/UCVTF).

### Part B — 경계: 포화, NaN, 반올림 방향, t = 1

- FCVTZS/FCVTZU 에 NaN · ±Inf · ±0 · 2^31 · 비정규수(denormal)를 넣는다. 기댓값은 **손으로** 쓴다. C 캐스트로 계산하면 UB 다.
- `cvt_ns / cvt_ms / cvt_ps / cvt_as` — 2.5, −2.5, 3.5, −3.5, 0.5, −0.5, 1.4, −1.6 표를 채운다.
- `lerp(1e8, 0.1, 1)` 가 0.1 이 아니라 **0** 이 되는 이유를 찾는다. `lerp_exact` = `fmsub` + `fmadd` 로 t=1 에서 정확히 b 를 돌려준다.

### Part C — 펌웨어: 고정소수점, ADC, 콜백 필터

- `float_to_q15_sat(x)` — `fcvtzs w0, s0, #15` + int16 범위 clamp. `float_to_q16` / `q16_to_float` — `#16` 형식.
- `adc_to_volts(raw12, vref)` = `raw/4096 * vref` — `ucvtf s1, w0, #12` 한 줄로 나누기까지.
- `ema_step(read, y, alpha)` = `y + alpha*(read() - y)` — 비-리프 함수. `y`·`alpha` 를 콜백 너머로 살리려면 `d8/d9` 에 두고 저장/복원한다. 하네스는 콜백 안에서 caller-saved FP 레지스터를 **전부** 망가뜨리고, 검사기 `cs_check_call` 로 d8–d15 가 보존됐는지 본다.

## 2. Problem (English)

> **AArch64 scalar floating point: views, calling convention, conversions.**
>
> Write in AArch64 assembly (leaf functions, no stack, unless stated):
>
> 1. `float dot3(const float *a, const float *b)` computing `fma(a2,b2, fma(a1,b1, a0*b0))` with `FMUL`/`FMADD`; `double dot3d(...)`; `float lerp(float a, float b, float t)` as `a + t*(b-a)`; `double mix_args(int32_t i, float f, int64_t l, double d)` returning `l*d + (double)i*(double)f`.
> 2. `int32_t f2i_sat(float)`, `int64_t d2l_sat(double)`, `uint32_t f2u_sat(float)`: truncate toward zero, saturate out-of-range values, map NaN to 0. Also `cvt_ns/ms/ps/as` (nearest-even, floor, ceil, nearest-away) and `i2f/u2f/l2d/ul2d`.
> 3. `float lerp_exact(float a, float b, float t)` that returns exactly `b` at `t == 1` and exactly `a` at `t == 0`.
> 4. Firmware: `int32_t float_to_q15_sat(float)` (result in [-32768, 32767]), `float q16_to_float(int32_t)`, `float adc_to_volts(uint32_t raw12, float vref)`, and a non-leaf `float ema_step(float (*read)(void), float y, float alpha)` that must survive a callback which clobbers every caller-saved FP register.
>
> Example: `f2i_sat(NAN) == 0`, `f2i_sat(3e9f) == INT32_MAX`, `cvt_ns(2.5f) == 2`, `cvt_as(2.5f) == 3`, `lerp(1e8f, 0.1f, 1.0f) == 0.0f` but `lerp_exact(1e8f, 0.1f, 1.0f) == 0.1f`, `float_to_q15_sat(1.0f) == 32767`.
>
> Follow-ups: Which FP registers are callee-saved, and why only 64 bits? Why does writing `s0` zero the rest of `v0`? Why can't the test use `(int)x` as the reference? What does x86 return for NaN → int? How do you load `0.1f` into `s0`?

## 3. 시작 코드

테스트 하네스 (starter/solution 공용, 손대지 않는다):

<!-- code: harness.c -->

TODO 가 있는 asm (모든 함수가 0/0.0 을 돌려주는 스텁 — 메모리를 건드리지 않고 바로 `ret`):

<!-- code: starter.S -->

테스트 도구 (TODO 아님): 콜백용 `clobber_fp_scratch` 와 callee-saved 검사기 `cs_check_call`.

<!-- code: shared_abi.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_abi.S -o st && ./st
```

처음 실행 결과 (실제 출력, 앞부분과 끝):

```text
FAIL harness.c:100  v[0] == 0x000000003F800000ull
FAIL harness.c:101  v[1] == 0
FAIL harness.c:102  v[2] == 0xFFFFFFFF3F800000ull
FAIL harness.c:103  v[3] == 0xFFFFFFFFFFFFFFFFull
FAIL harness.c:106  dot3(a1, b1) == 32.0f
FAIL harness.c:110  dot3(a2, b2) == 0x1p-24f
FAIL harness.c:122  bad == 0
… (FAIL 69줄)
info: ema y=0x0p+0 (0), v8-v15 upper-64 changed mask=0x00 (allowed by AAPCS64)
15/84 passed
```

통과하는 15개는 "스텁이 0을 돌려주면 우연히 맞는" 것들이다 (`f2i_sat(NAN) == 0`, `-0.0 → 0` 등). 그래서 **0을 돌려주는 경계 케이스는 단독으로는 아무것도 증명하지 못한다**. 범위 안 값 테스트와 함께 봐야 한다.

관찰용 파일 (테스트와 별개, 전부 이 디렉터리):

```bash
cc -std=c11 -O2 -S -fno-asynchronous-unwind-tables [-ffp-contract=off|fast] demo_emit.c -o -   # clang 이 FP C 를 뭘로 내리나
cc -std=c11 -O2 demo_c_cast.c -o dc && ./dc        # (int)NaN, (int)3e9f: 실행 vs 상수 접기 (C 에서 UB)
cc -std=c11 -O1 -g -fsanitize=float-cast-overflow demo_c_cast.c -o dcu && ./dcu
cc -c -DBAD demo_fmov_imm.S -o /dev/null           # FMOV 즉시값 한계 (어셈블러 에러)
cc -std=c11 -O2 demo_bugs.c demo_bugs.S shared_abi.S -o db && ./db   # ema 의 ABI 버그 2종
cc -std=c11 -g -O0 harness.c solution.S shared_abi.S -o /tmp/ARM-B11_dbg && lldb -b -s demo_views.lldb /tmp/ARM-B11_dbg
cc -std=c11 -O2 demo_fjcvtzs.c -o dfj && ./dfj     # FJCVTZS (JavaScript 변환 명령)
```

## 4. 힌트

<details>
<summary>힌트 1 — FP 레지스터 이름이 곧 타입이다</summary>

`fmul s0, s0, s1` 은 float 곱셈이고, `fmul d0, d0, d1` 은 double 곱셈이다. 명령 이름은 같고, 레지스터 접두사(s/d)가 정밀도를 정한다. 메모리에서 float 하나를 읽을 때는 `ldr s2, [x0, #4]` 를 쓴다. 오프셋은 바이트 단위다(float 는 4, double 은 8).

</details>

<details>
<summary>힌트 2 — FMADD 의 피연산자 순서</summary>

`fmadd Sd, Sn, Sm, Sa` = `Sa + Sn*Sm`, 반올림은 한 번이다. `fmsub Sd, Sn, Sm, Sa` = `Sa − Sn*Sm`. 더해지는 값(addend)이 **맨 끝**에 온다. `lerp` 는 `fsub` 로 `b−a` 를 만든 뒤 `fmadd s0, s2(t), s1(b−a), s0(a)`.

</details>

<details>
<summary>힌트 3 — 변환은 명령 하나</summary>

float→int32 는 `fcvtzs w0, s0`, double→int64 는 `fcvtzs x0, d0`, 부호 없는 쪽은 `fcvtzu` 다. 반대 방향은 `scvtf s0, w0` / `ucvtf d0, x0` 이다. 포화와 NaN→0 은 **명령이 알아서** 한다. 비교·분기가 필요 없다. 반올림 방향은 가운데 글자로 정한다: N(nearest even), M(minus ∞), P(plus ∞), A(away), Z(zero).

</details>

<details>
<summary>힌트 4 — t = 1 에서 정확한 lerp</summary>

`a + t*(b−a)` 는 `b−a` 를 계산할 때 이미 반올림된다. 그 뒤로는 t=1 이어도 b 를 되찾을 수 없다. 반올림이 일어나기 전에 a 를 지우는 모양을 생각해 보자: `(a − t*a) + t*b`. 각 괄호를 fused 명령 하나로 만들면 t=1 일 때 첫 단계가 정확히 0 이다.

</details>

<details>
<summary>힌트 5 — 고정소수점과 비-리프 함수</summary>

`fcvtzs w0, s0, #15` 는 `trunc(x × 2^15)` 이다(곱셈을 따로 하지 않는다). `ucvtf s1, w0, #12` 는 `raw / 2^12` 다. `ema_step` 은 `blr x0` 로 콜백을 부르므로 LR(x30)을 저장해야 한다(ARM-B08). 콜백 뒤에도 필요한 `y`, `alpha` 는 `s8`, `s9` 에 두고, 그 전에 `stp d8, d9, [sp, #16]` 로 저장한다.

</details>

## 5. 풀이

### 0. 레지스터 뷰 — lldb 로 직접 보기

`vreg_views` 첫 두 명령(`movi v0.2d, #-1` → `fmov s0, #1.0`)을 lldb 로 한 줄씩 실행한 실제 transcript (`demo_views.lldb`, 프롬프트 줄만 추림):

```text
->  0x100004548 <+4>:  fmov   s0, #1.00000000
(lldb) register read v0 d0 s0
      v0 = {0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff}
      d0 = NaN
      s0 = NaN
(lldb) si
->  0x10000454c <+8>:  fmov   x1, d0
(lldb) register read v0 q0 d0 s0
      v0 = {0x00 0x00 0x80 0x3f 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00}
      v0 = {0x00 0x00 0x80 0x3f 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00}
      d0 = 5.2635442471208903E-315
      s0 = 1
(lldb) register read -f x d0 s0
      d0 = 0x000000003f800000
      s0 = 0x3f800000
```

- lldb 는 v0 를 **리틀 엔디언 바이트 순서**로 찍는다. `00 00 80 3f` = `0x3F800000` = 1.0f (IEEE 754 분해는 ARCH-B03).
- `q0` 를 물어도 `v0` 로 나온다(같은 레지스터). 이 맥의 lldb 는 `h0`/`b0` 이름을 모른다(`error: Invalid register name 'h0'`).
- `d0 = 5.26e-315` 는 0x000000003F800000 을 double 로 읽은 값, 즉 **비정규 double** 이다. 뷰만 바뀌고 비트는 그대로다.
- 모두 1이던 v0 에 `fmov s0, #1.0` 을 하면 위 96비트가 0이 된다. 정수 쪽 `w0` 쓰기가 x0 위 32비트를 지우는 것과 같은 원리다. 이렇게 하면 "옛 값의 위쪽 절반" 에 대한 의존(partial register dependency)이 생기지 않는다. 그래서 out-of-order 코어가 레지스터 이름을 쉽게 바꿀 수 있다(설계 의도는 일반적인 설명 — 확인 권장).

### Part A — dot3: 한 줄씩 (fused 가 보이는 입력)

하네스의 특별 입력: `x = 1 + 2^-12`, `a = {1, x, 0}`, `b = {−(1+2^-11), x, 0}`. `x·x = 1 + 2^-11 + 2^-24` 이다. 여기서 2^-24 는 [1,2) 구간 float 의 **반 ulp** 이므로, 따로 반올림하면 정확히 tie 가 되어 짝수 쪽 `1+2^-11` 로 간다.

| # | 명령 | 결과 | 설명 |
|---|---|---|---|
| 1 | `ldr s0, [x0]` / `ldr s1, [x1]` | s0 = 1, s1 = −1.00048828125 | a[0], b[0] |
| 2 | `fmul s0, s0, s1` | s0 = −(1+2^-11) | 정확 (반올림할 것 없음) |
| 3 | `ldr s2, [x0, #4]` / `ldr s3, [x1, #4]` | s2 = s3 = 1.000244140625 | a[1], b[1] |
| 4 | `fmadd s0, s2, s3, s0` | s0 = **2^-24 ≈ 5.96e-8** | x·x 를 반올림하지 않고 더한 뒤 한 번 반올림 |
| 5 | `ldr` ×2, `fmadd s0, s2, s3, s0` | s0 = 2^-24 | 0·0 + s0 |

곱을 먼저 반올림하면(비융합) 4번이 `(1+2^-11) − (1+2^-11) = 0` 이 된다. 하네스는 둘 다 확인한다: `dot3(a2,b2) == 0x1p-24f`, 그리고 문장을 나눠 쓴 C 는 `unfused == 0.0f`.

**그래서 C 참조는 `fmaf` 로 써야 한다.** 같은 dot3 를 C 식 한 줄로 쓰면 컴파일러가 contraction(곱+합을 FMA 로 합치기)을 할지, 어느 곱을 합칠지를 정한다. `demo_emit.c` 의 실제 출력 (Apple clang 21, `-O2`):

```text
_madd_expr:           ; return a*b + c;               (기본 = -ffp-contract=on)
	fmadd	s0, s0, s1, s2
_madd_stmt:           ; float p = a*b; return p + c;
	fmul	s0, s0, s1
	fadd	s0, s0, s2
_dot3_c:              ; a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
	ldp	s0, s1, [x0]
	ldp	s2, s3, [x1]
	fmul	s1, s1, s3             ; a1*b1 을 먼저 반올림!
	fmadd	s0, s0, s2, s1         ; a0*b0 + (a1*b1)
	ldr	s1, [x0, #8]
	ldr	s2, [x1, #8]
	fmadd	s0, s1, s2, s0
```

clang 은 **`a1*b1` 을 반올림하고 `a0*b0` 를 fuse** 했다. 내 asm 과 반대 순서다. 그래서 위 특별 입력에서 C `dot3_c` 는 `0x0p+0` 을 낸다(`./dc` 실측). 컴파일러·플래그별로 정리하면 이렇다 (실측):

| 컴파일러 / 플래그 | `a*b+c` (한 식) | `p=a*b; p+c` (두 문장) | 비고 |
|---|---|---|---|
| Apple clang 기본 (`cc -###` → `-ffp-contract=on`) | `fmadd` | `fmul`+`fadd` | 식 안에서만 합침 |
| clang `-ffp-contract=off` | `fmul`+`fadd` | `fmul`+`fadd` | dot3_c 는 SLP 벡터화(`fmul.2s`)까지 됨 — ARM-A11 |
| clang `-ffp-contract=fast` | `fmadd` | `fmadd` | 문장을 넘어서도 합침 |
| VM gcc 15 `-std=c11` | — | `fmul`+`fadd` | ISO 모드는 off (gcc 문서 기준) |
| VM gcc 15 `-std=gnu11` | — | `fmadd` | GNU 모드 기본은 fast |

실리콘 검증에서 "C 골든 모델과 펌웨어 결과가 1 ulp 다르다"는 버그는 대부분 이것이다. 참조를 `fmaf()` 로 **명시** 하면 어느 컴파일러에서도 같은 값이 나온다. 하네스가 그렇게 짰고, VM 의 gcc·clang 빌드에서도 84/84 였다.

### Part A — lerp, mix_args: 인자 카운터가 둘

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">정수 카운터(NGRN)와 FP 카운터(NSRN)는 따로 센다 — AAPCS64 / Apple arm64 공통</text>
<text x="240" y="46" fill="currentColor">정수: x0 x1 x2 …</text><text x="450" y="46" fill="currentColor">FP/SIMD: v0 v1 v2 …</text> <g fill="currentColor" font-family="monospace" font-size="12">
<text x="10" y="80">mix_args(int32 i,</text><text x="10" y="96">  float f, int64 l, double d)</text> <text x="10" y="140">ema_step(read, float y,</text><text x="10" y="156">  float alpha)</text>
<text x="10" y="200">adc_to_volts(uint32 raw,</text><text x="10" y="216">  float vref)</text> </g> <g stroke="currentColor">
<rect x="240" y="66" width="56" height="30" class="f-acc"/><rect x="300" y="66" width="56" height="30" class="f-acc"/><rect x="360" y="66" width="56" height="30" fill="none"/>
<rect x="450" y="66" width="56" height="30" class="f-ok"/><rect x="510" y="66" width="56" height="30" class="f-ok"/><rect x="570" y="66" width="56" height="30" fill="none"/>
<rect x="240" y="126" width="56" height="30" class="f-acc"/><rect x="300" y="126" width="56" height="30" fill="none"/><rect x="360" y="126" width="56" height="30" fill="none"/>
<rect x="450" y="126" width="56" height="30" class="f-ok"/><rect x="510" y="126" width="56" height="30" class="f-ok"/><rect x="570" y="126" width="56" height="30" fill="none"/>
<rect x="240" y="186" width="56" height="30" class="f-acc"/><rect x="300" y="186" width="56" height="30" fill="none"/><rect x="360" y="186" width="56" height="30" fill="none"/>
<rect x="450" y="186" width="56" height="30" class="f-ok"/><rect x="510" y="186" width="56" height="30" fill="none"/><rect x="570" y="186" width="56" height="30" fill="none"/> </g>
<g fill="currentColor" font-family="monospace" font-size="12"> <text x="246" y="86">w0=i</text><text x="306" y="86">x1=l</text><text x="456" y="86">s0=f</text><text x="516" y="86">d1=d</text><text x="650" y="86">→ d0</text>
<text x="246" y="146">x0=fn</text><text x="456" y="146">s0=y</text><text x="516" y="146">s1=α</text><text x="650" y="146">→ s0</text>
<text x="246" y="206">w0=raw</text><text x="456" y="206">s0=vr</text><text x="650" y="206">→ s0</text> </g>
<rect x="10" y="236" width="230" height="26" class="f-mut" stroke="currentColor"/><text x="18" y="254" fill="currentColor">v0–v7 인자/반환 (caller-saved)</text>
<rect x="245" y="236" width="250" height="26" class="f-ok" stroke="currentColor"/><text x="253" y="254" fill="currentColor">v8–v15: d8–d15(하위 64)만 callee-saved</text>
<rect x="500" y="236" width="230" height="26" class="f-mut" stroke="currentColor"/><text x="508" y="254" fill="currentColor">v16–v31 임시 (caller-saved)</text>
<text x="10" y="288" fill="currentColor">float 와 double 은 같은 v 번호를 s/d 뷰로 쓴다.</text> <text x="10" y="310" fill="currentColor">i 가 x0 을 썼어도 f 는 s0, l 은 x1 — 섞여 있어도 각자 0번부터 센다.</text>
</svg>
```

`mix_args` 는 4줄짜리지만 변환 명령 3종이 다 나온다:

| 명령 | 하는 일 | 정확한가 |
|---|---|---|
| `scvtf d2, w0` | int32 → double | 항상 정확 (32비트 < 53비트 가수) |
| `fcvt d3, s0` | float → double (정밀도 변환) | 항상 정확 |
| `fmul d2, d2, d3` | `i*f` | 반올림 가능 (32+24 비트 > 53) |
| `scvtf d3, x1` | int64 → double | \|l\| > 2^53 이면 반올림 |
| `fmadd d0, d3, d1, d2` | `l*d + i*f` | 반올림 1회 |

`fcvt` 는 FP 정밀도끼리(s↔d↔h) 바꾸고, `scvtf/fcvtzs` 는 정수↔FP 를 바꾼다. 비트를 **그대로** 옮기는 것은 `fmov w8, s0` 이다(변환 아님 — 연습 4). 이 세 가지를 헷갈리는 게 crash dump 읽기에서 제일 흔한 실수다.

### Part A — FCVT 계열: 인코딩 한 장으로

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">FP ↔ 정수 변환 (스칼라, 정수형) — 비트 필드와 이 레슨 오브젝트의 실제 워드</text> <g stroke="currentColor">
<rect x="10" y="36" width="30" height="30" class="f-acc"/><rect x="40" y="36" width="50" height="30" class="f-mut"/><rect x="90" y="36" width="110" height="30" class="f-mut"/>
<rect x="200" y="36" width="60" height="30" class="f-acc"/><rect x="260" y="36" width="30" height="30" class="f-mut"/><rect x="290" y="36" width="60" height="30" class="f-warn"/>
<rect x="350" y="36" width="80" height="30" class="f-warn"/><rect x="430" y="36" width="130" height="30" class="f-ok"/><rect x="560" y="36" width="80" height="30" fill="none"/><rect x="640" y="36" width="80" height="30" fill="none"/>
</g> <g fill="currentColor" font-family="monospace" font-size="12"> <text x="16" y="56">sf</text><text x="52" y="56">00</text><text x="110" y="56">11110</text><text x="208" y="56">ftype</text><text x="270" y="56">1</text>
<text x="296" y="56">rmode</text><text x="356" y="56">opcode</text><text x="438" y="56">000000 / scale</text><text x="584" y="56">Rn</text><text x="664" y="56">Rd</text>
<text x="18" y="82">31</text><text x="48" y="82">30:29</text><text x="120" y="82">28:24</text><text x="210" y="82">23:22</text><text x="268" y="82">21</text>
<text x="298" y="82">20:19</text><text x="360" y="82">18:16</text><text x="470" y="82">15:10</text><text x="584" y="82">9:5</text><text x="664" y="82">4:0</text> </g> <g fill="currentColor" font-family="monospace" font-size="12">
<text x="10" y="112">워드        명령                  rmode opcode  의미</text> <text x="10" y="132">1e200000    fcvtns w0, s0         00    000     nearest, ties to even</text>
<text x="10" y="150">1e280000    fcvtps w0, s0         01    000     toward +∞ (ceil)</text> <text x="10" y="168">1e300000    fcvtms w0, s0         10    000     toward −∞ (floor)</text>
<text x="10" y="186">1e380000    fcvtzs w0, s0         11    000     toward 0 (C 캐스트 방향)</text> <text x="10" y="204">1e240000    fcvtas w0, s0         00    100     nearest, ties away (roundf)</text>
<text x="10" y="222">1e390000    fcvtzu w0, s0         11    001     부호 없음</text> <text x="10" y="240">1e220000    scvtf  s0, w0         00    010     int → FP (FPCR 반올림)</text>
<text x="10" y="258">1e230000    ucvtf  s0, w0         00    011     uint → FP</text> <text x="10" y="276">9e780000    fcvtzs x0, d0         sf=1, ftype=01 (double), 나머지 같음</text>
<text x="10" y="294">1e18c400    fcvtzs w0, s0, #15    bit21=0 (고정소수점형), scale=110001=49=64−15</text> <text x="10" y="312">1e02c000    scvtf  s0, w0, #16    bit21=0, opcode 010, scale=110000=48=64−16</text> </g>
</svg>
```

(워드는 `cc -c solution.S && objdump -d` 의 실제 값. 필드 이름은 Arm ARM "Conversion between floating-point and integer" / "…and fixed-point" 인코딩 표 기준이다.) 반올림 방향은 명령 안에 2~3비트로 **박혀** 있다. 그래서 FPCR 의 반올림 모드와 상관없이 항상 같다. 예외는 `scvtf/ucvtf` 와 `frintx` 계열이다. 이들은 FPCR.RMode 를 따른다(이 맥은 FPCR = 0x0 → RN 실측).

### Part B — FCVTZS 경계: ISA 는 정의, C 는 UB

| 입력 (float) | `fcvtzs w` | `fcvtzu w` | C `(int32_t)x` |
|---|---|---|---|
| NaN, −NaN | 0 | 0 | UB |
| +Inf / −Inf | INT32_MAX / INT32_MIN | UINT32_MAX / 0 | UB |
| 3e9 / −3e9 | INT32_MAX / INT32_MIN | 3000000000 / 0 | UB |
| 2^31 (2147483648.0f) | INT32_MAX (딱 1 넘침) | 2147483648 | UB |
| −2^31 | INT32_MIN (정확) | 0 | 정의됨 |
| 2147483520.0f (2^31 아래 최대 float) | 2147483520 | 2147483520 | 정의됨 |
| −0.5, −0.99999994 | 0 | 0 | 정의됨 (정수부 0) |
| −0.0, 0x1p−149 (최소 비정규수) | 0 | 0 | 정의됨 |

하네스 기댓값은 이 표를 **손으로** 옮긴 것이다. 무작위 100만 비트 패턴(NaN·Inf·비정규 포함)은 "범위를 먼저 검사한 뒤에만 캐스트하는" 참조 모델 `ref_f2i_sat` 과 비교한다. C11 6.3.1.4p1: 정수부가 대상 타입에 안 들어가면 동작이 정의되지 않는다. 그 UB 가 실제로 어떻게 나타나는지 (`demo_c_cast.c`, 실측):

```text
== -O2
runtime : (int32_t)NaN=0  (int32_t)3e9f=2147483647  (int32_t)-3e9f=-2147483648
folded  : (int32_t)3e9f=0
== -O0
runtime : (int32_t)NaN=0  (int32_t)3e9f=2147483647  (int32_t)-3e9f=-2147483648
folded  : (int32_t)3e9f=2147483647
== -fsanitize=float-cast-overflow
demo_c_cast.c:11:68: runtime error: nan is outside the range of representable values of type 'int'
demo_c_cast.c:21:22: runtime error: 3e+09 is outside the range of representable values of type 'int'
```

- 실행 중의 캐스트는 clang 이 그냥 `fcvtzs` 로 내리므로(아래 demo_emit) 하드웨어의 포화값이 나온다.
- 같은 캐스트를 **컴파일 시간에 접으면(`-O2`) 0** 이 나온다. LLVM 은 UB 결과를 poison 으로 취급한다. `-O0` 에서는 실행 경로를 타서 2147483647 이 나온다. 최적화 레벨에 따라 답이 바뀌는 것이다. VM 의 gcc 15 `-O2` 는 같은 줄을 2147483647 로 접었다. 컴파일러마다 다르다.
- 그래서 하네스는 범위 밖 입력을 C 로 계산하지 않는다. sanitizer 빌드(`-fsanitize=address,undefined,float-cast-overflow`)도 84/84, 보고 0건으로 통과한다.

### Part B — 반올림 4형제 (+ Z)

| 입력 | NS (even) | AS (away) | MS (floor) | PS (ceil) | ZS (trunc) |
|---|---|---|---|---|---|
| 2.5 | **2** | 3 | 2 | 3 | 2 |
| −2.5 | **−2** | −3 | −3 | −2 | −2 |
| 3.5 | 4 | 4 | 3 | 4 | 3 |
| −3.5 | −4 | −4 | −4 | −3 | −3 |
| 0.5 | **0** | 1 | 0 | 1 | 0 |
| −0.5 | 0 | −1 | −1 | 0 | 0 |
| 1.4 | 1 | 1 | 1 | 2 | 1 |
| −1.6 | −2 | −2 | −2 | −1 | −1 |

- NS 는 **tie 일 때만** 짝수 쪽으로 간다(2.5→2, 3.5→4). "은행가 반올림"이라 합계 편향이 없다. C 의 `nearbyintf/lrintf`(기본 모드)와 같다.
- AS 는 `roundf` 다. clang 은 `(int)roundf(x)` 를 `fcvtas w0, s0` **한 줄**로, `(int)floorf(x)` 를 `fcvtms` 로 내린다. `(int)lrintf(x)` 는 `frintx s0, s0; fcvtzs x0, s0` 두 줄이다(demo_emit 실측). lrint 는 "현재 FPCR 모드"를 따라야 하므로 모드가 고정된 FCVTNS 를 못 쓴다.
- 정수 결과에는 −0 이 없다: `cvt_ps(-0.5f) == 0`.
- 하네스: 위 표 8×5 를 손으로 쓴 배열과 비교하고, 무작위 20만 개(절반은 k/4 꼴이라 tie 가 자주 나옴)를 libm 과 비교한다. 포화와 NaN→0 은 4형제 모두 같다.

### Part B — lerp 는 왜 t = 1 에서 b 를 놓치나

`lerp(1e8, 0.1, 1)` 을 손으로 추적하면:

| 단계 | 명령 | 값 |
|---|---|---|
| 1 | `fsub s1, s1, s0` | 0.1 − 1e8 = −99999999.9 → float 로 반올림 → **−1e8** (1e8 근처 ulp = 8) |
| 2 | `fmadd s0, s2, s1, s0` | 1·(−1e8) + 1e8 = **0** |

`b−a` 를 만드는 순간 b 의 정보(0.1)가 사라졌다. `lerp_exact` 는 반올림 전에 a 를 지운다:

| 단계 | 명령 | t = 1 | t = 0 |
|---|---|---|---|
| 1 | `fmsub s3, s2, s0, s0` (= a − t·a, fused) | a − a = **정확히 0** | a − 0 = a |
| 2 | `fmadd s0, s2, s1, s3` (= t·b + s3, fused) | b + 0 = **b** | 0 + a = **a** |

fused 라서 1단계의 `t·a` 가 반올림되지 않는다. t=1 이면 정확히 0 이 된다. 무작위 20만 개(a, b ∈ [−1e6, 1e6]) 실측: `info: lerp(a,b,1) != b in 18975 / 200000 random cases; lerp_exact: 0` — 약 9.5% 에서 b 를 놓친다. 대신 `lerp_exact` 는 FMA 가 두 번 필요하다. 단조성(t 가 커지면 결과도 단조)까지 보장하는 lerp 는 더 까다로운 문제다(C++20 `std::lerp` 의 요구사항 — 이 레슨 범위 밖).

### Part C — 고정소수점: `#fbits` 는 공짜 곱셈

`fcvtzs w0, s0, #15` 의 의미 (Arm ARM FPToFixed 의사코드 기준): x × 2^15 를 **무한 정밀도로** 계산하고 0 쪽으로 자른 뒤 int32 로 포화한다. 그래서 `1e10 × 2^15` 처럼 float 로 넘칠 곱도 inf 를 거치지 않고 INT32_MAX 가 된다. Q15 는 int16 범위가 필요하므로 clamp 를 두 번 더 한다:

| x | `fcvtzs #15` 후 w0 | min(·, 32767) | max(·, −32768) |
|---|---|---|---|
| 0.5 | 16384 | 16384 | **16384** |
| 1.0 | 32768 | 32767 | **32767** (1.0 은 Q15 로 표현 불가) |
| −1.0 | −32768 | −32768 | **−32768** |
| 1e10 | 2147483647 | 32767 | **32767** |
| NaN | 0 | 0 | **0** |

clang 도 똑같이 한다. `(int32_t)(x * 65536.0f)` → `fcvtzs w0, s0, #16` 한 줄, `(float)raw / 4096.0f * vref` → `ucvtf s1, w0, #12` + `fmul` (demo_emit 실측). 2의 거듭제곱 곱셈/나눗셈은 정확한 연산이라서 합쳐도 결과가 같다. 그래서 컴파일러가 합칠 수 있다.

ADC 변환 검증: 12비트 raw 0..4095 **전수**(+4096)를 `((float)raw / 4096.0f) * 3.3f` 와 비트 단위로 비교했다. raw < 2^24 이면 `raw/4096` 은 float 로 정확하다. 그래서 반올림은 `* vref` 한 번뿐이고, C 와 비트까지 같다.

### Part C — ema_step: 비-리프 함수와 callee-saved d8/d9

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">ema_step 의 32바이트 프레임 (sp 16B 정렬) — 저장하는 건 d8/d9 의 하위 64비트뿐</text> <g stroke="currentColor">
<rect x="120" y="40" width="200" height="34" class="f-mut"/><rect x="120" y="74" width="200" height="34" class="f-mut"/>
<rect x="120" y="108" width="200" height="34" class="f-ok"/><rect x="120" y="142" width="200" height="34" class="f-ok"/> </g> <g fill="currentColor" font-family="monospace">
<text x="20" y="62">sp+24</text><text x="20" y="96">sp+16</text><text x="20" y="130">sp+8</text><text x="20" y="164">sp+0</text>
<text x="135" y="62">d9 (호출자의 값)</text><text x="135" y="96">d8 (호출자의 값)</text><text x="135" y="130">x30 (LR)</text><text x="135" y="164">x29 (호출자 FP)</text> </g> <g fill="currentColor" font-family="monospace" font-size="12">
<text x="350" y="56">stp x29, x30, [sp, #-32]!</text><text x="350" y="76">mov x29, sp</text><text x="350" y="96">stp d8, d9, [sp, #16]</text>
<text x="350" y="116">fmov s8, s0 ; fmov s9, s1   // y, α</text><text x="350" y="136">blr x0         // read()</text>
<text x="350" y="156">fsub / fmadd   // y + α(x − y)</text><text x="350" y="176">ldp d8, d9, [sp, #16]</text><text x="350" y="196">ldp x29, x30, [sp], #32</text> </g>
<text x="10" y="230" fill="currentColor">ldp d8 은 v8 의 상위 64비트를 0으로 만든다 → 검사기 실측 upper mask = 0x03 (v8, v9).</text> <text x="10" y="252" fill="currentColor">AAPCS64 는 하위 64비트만 보존하라고 하므로 이 손상은 허용된다.</text>
</svg>
```

검증은 세 겹이다.

1. **값**: 6개 샘플(1, 1, 0.5, 2, −1, 1.25), α = 0.25 로 6단계를 돌린다. 매 단계 `fmaf(α, x−y, y)` 와 비트까지 같아야 한다. 최종 y = 0.597412109375 (`0x1.31ep-1`, 실측).
2. **caller-saved 파괴**: 콜백 `sensor_read` 가 `clobber_fp_scratch` 로 v0–v7, v16–v31 을 0xA5 로 덮는다. y 를 `s2` 같은 데 두면 값이 망가진다.
3. **callee-saved 보존**: `cs_check_call` 이 d8–d15 에 센티널 `0xC0DE00000000000k` 를 심고 ema_step 을 부른 뒤 비교한다. 하위 64비트가 바뀌면 bit k 를 켠다(ABI 위반 → FAIL). 상위 64비트가 바뀌면 bit 8+k 를 켠다(정보로만 출력).

일부러 틀린 두 버전(`demo_bugs.S`)을 같은 검사기로 돌린 실제 출력:

```text
ema_bug_scratch  got=-0x1.4b4b4ap-51 want=0x1.4p-1  d8-d15 clobbered=0x00  upper=0x00
ema_bug_nosave   got=0x1.4p-1       want=0x1.4p-1  d8-d15 clobbered=0x03  upper=0x03
```

- `ema_bug_scratch`: y 를 s2 에 두었다. 콜백이 v2 를 0xA5A5… 로 덮어서 결과가 `-2.87e-16` 같은 쓰레기가 됐다.
- `ema_bug_nosave`: **값은 맞다.** 하지만 호출자의 d8/d9 를 망가뜨렸다. 이 버그는 반환값 테스트로는 절대 안 잡힌다. 호출자가 그 레지스터에 루프 변수를 두고 있을 때, 엉뚱한 곳에서 터진다. 이런 버그를 잡으려고 검사기를 만든 것이다.

**상위 64비트 관찰** (solution, 실측): 맥 = `mask=0x03`(v8, v9 — ema_step 의 `ldp d8, d9` 가 지움). VM gcc 빌드 = `mask=0x83`. v15 가 더 있는 이유는 gcc 가 컴파일한 **C 콜백** `sensor_read` 가 `str d15, [sp, #16]` … `ldr s15, …` … `ldr d15, [sp, #16]` 로 d15 를 썼기 때문이다(VM `objdump` 확인). 즉 컴파일러도 v8–v15 의 상위 절반은 보존하지 않는다. 128비트 전체를 함수 호출 너머로 살려야 하는 NEON 코드는 호출자가 직접 저장해야 한다(ARM-M06).

### FP 상수 로딩: FMOV 즉시값은 8비트

`fmov s0, #imm` 의 imm8 = `a:b:cdefgh` 는 부호 1 + 지수 3 + 가수 4비트다. 그래서 표현 가능한 값은 ±(16..31)/16 × 2^(−3..4) 뿐이다. 범위는 0.125 ~ 31 이고 0 은 없다(Arm ARM VFPExpandImm 기준). `demo_fmov_imm.S` 실측:

```text
       0: 1e2e1000     	fmov	s0, #1.00000000      ; imm8 = 0x70
       4: 1e281000     	fmov	s0, #0.12500000      ; 0x40 (최소)
       8: 1e27f000     	fmov	s0, #31.00000000     ; 0x3F (최대)
       c: 1e3f1000     	fmov	s0, #-1.50000000     ; 0xF8
      10: 1e609000     	fmov	d0, #2.50000000      ; 0x04
      18: 1e2703e0     	fmov	s0, wzr              ; "#0.0" 이라고 써도 이것으로
$ cc -c -DBAD demo_fmov_imm.S -o /dev/null
demo_fmov_imm.S:18:14: error: expected compatible register or floating-point constant
    fmov s0, #0.1
demo_fmov_imm.S:19:14: error: expected compatible register or floating-point constant
    fmov s0, #32.0
demo_fmov_imm.S:20:14: error: expected compatible register or floating-point constant
    fmov s0, #0.1171875
```

`0.1f` 같은 값은 clang 이 `mov w8, #0xcccd; movk w8, #0x3dcc, lsl #16; fmov s0, w8` 로 만든다(demo_emit `c_k` 실측). 정수 레지스터에 비트를 만든 뒤 옮기는 것이다(MOVZ/MOVK 는 ARM-B03). 상수가 많으면 리터럴 풀이나 `adrp`+`ldr s` 를 쓴다(ARM-A09).

### Linux/ELF 에서도 같은 파일

`.S` 파일 머리의 `SYM(x)` 매크로가 맥에서는 `_x`, ELF 에서는 `x` + `.type x,%function` + `.size` 가 된다. 끝에는 `.note.GNU-stack` 을 붙여 실행 가능 스택 경고를 막는다. Lima VM(aarch64 Ubuntu)에서 실제로 빌드했다 (`gcc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_abi.S -lm`). 결과: starter `15/84 passed` exit 1, solution `84/84` exit 0, practice `26/26`, clang 21 빌드도 84/84. `readelf -s` 에 `vreg_views 48 FUNC GLOBAL` 처럼 크기와 타입이 찍힌다. 리눅스는 `fmaf`·`roundf` 때문에 `-lm` 이 필요하다. 맥은 libSystem 에 포함돼 있어서 필요 없다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_abi.S -o sol && ./sol
# info: lerp(a,b,1) != b in 18975 / 200000 random cases; lerp_exact: 0
# info: ema y=0x1.31ep-1 (0.597412109), v8-v15 upper-64 changed mask=0x03 (allowed by AAPCS64)
# 84/84 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined harness.c solution.S shared_abi.S -o san && ./san
# 84/84 passed (sanitizer 보고 0)
```

## 7. 자주 하는 실수

1. **참조를 `a*b + c` 로 쓴다.** 기본 `-ffp-contract=on` 에서 clang 은 식 안의 곱 하나를 골라 fuse 한다. dot3_c 는 내 asm 과 다른 곱을 골랐고, 특별 입력에서 0 vs 2^-24 로 갈렸다. gcc `-std=gnu11` 은 문장을 넘어서도 합친다. → 고친 코드: `fmaf(a[2], b[2], fmaf(a[1], b[1], a[0]*b[0]))`.
2. **범위 밖 float→int 기댓값을 C 캐스트로 만든다.** UB 다. clang `-O2` 는 `(int32_t)3e9f` 를 0 으로 접었고, `-O0` 은 2147483647, gcc 는 2147483647 이었다. → 손으로 쓴 기댓값, 또는 "먼저 범위 검사 후 캐스트" 하는 참조 모델.
3. **콜백 너머로 쓸 값을 caller-saved(s0–s7, s16–s31)에 둔다.** `ema_bug_scratch` 의 결과가 `-0x1.4b4b4ap-51` 이었다. → `s8..s15` 에 두고 `stp d8, d9` 로 저장한다.
4. **d8 을 쓰면서 저장하지 않는다.** 반환값은 맞아서 단위 테스트를 통과한다(`ema_bug_nosave`). → 검사기처럼 센티널로 확인한다. 저장할 때는 `str s8` 이 아니라 **`d8`(64비트)** 로 한다. `str s8`/`ldr s8` 은 bit 32–63 을 잃는다(ABI 는 하위 64비트 보존을 요구).
5. **`fmov w0, s0` 를 변환으로 착각한다.** `fmov` 는 비트 복사다(1.0f → 0x3F800000 = 1065353216). 값 변환은 `fcvtzs`. 반대로 `scvtf` 와 `ucvtf` 는 같은 비트를 다르게 읽는다: `i2f(0xFFFFFFFF) == -1.0f`, `u2f(0xFFFFFFFF) == 4294967296.0f`.
6. **FP 상수를 `fmov s0, #0.1` 로 쓴다.** → 어셈블러 에러(위 실측). 비트 패턴을 `movz/movk` 로 만들고 `fmov s0, w8` 로 옮긴다.
7. **FPSR 마스크를 `and w0, w9, #0x9f` 로 쓴다.** 실제로 이 레슨 연습 1 을 쓰다가 났다: `error: expected compatible register or logical immediate`. 0x9F = 1001_1111 은 연속된 1의 회전이 아니다(ARM-B03). → `mov w10, #0x9f; and w0, w9, w10`.
8. **`fcvtzs` 결과를 int16 으로 바로 쓴다.** 포화는 **목적 레지스터 폭(32/64)** 기준이다. Q15 처럼 더 좁은 타입은 따로 clamp 해야 한다(`float_to_q15_sat(1.0f)` = 32768 → 32767).

## 8. 면접 Q&A

<details>
<summary>Q1. ARM 에서 NaN 이나 3e9 를 int32 로 바꾸면? x86 은?</summary>

AArch64 `FCVTZS` 는 0 쪽으로 자르고, 범위 밖은 INT32_MAX/MIN 으로 포화하고, NaN 은 0 을 낸다. 트랩은 없고 FPSR.IOC(invalid) 플래그만 켜진다(연습 1 에서 실측: NaN, 3e9, −Inf → IOC; 2.5 → IXC). x86 SSE `cvttss2si` 는 NaN·범위 밖이면 "integer indefinite" 0x80000000 을 낸다(Intel SDM 기준, 이 레슨에서 미실측). 그래서 같은 C 코드(`(int)x`, UB)가 두 아키텍처에서 다른 값을 낸다. Rust 의 `f32 as i32` 는 "포화 + NaN→0" 으로 정의돼 있어서 ARM 에서는 명령 하나다. x86 에서는 보정 코드가 붙는다(언어 정의는 Rust reference 기준).

꼬리: "C 에서 포화 변환이 필요하면?" → `isnan` 과 범위를 먼저 검사한다(이 하네스의 `ref_f2i_sat`). 또는 asm/intrinsic 으로 FCVTZS 를 직접 쓴다.

</details>

<details>
<summary>Q2. FP 레지스터 중 callee-saved 는? 왜 64비트만?</summary>

v8–v15 의 하위 64비트(d8–d15)만이다. 나머지(v0–v7, v16–v31, v8–v15 의 상위 64비트)는 caller-saved 다. 스칼라 double 코드는 d 레지스터 몇 개만 살리면 충분하다. 128비트 전부를 callee-saved 로 하면 이 레지스터를 쓰는 모든 함수가 16바이트씩 저장/복원해야 한다. 그래서 비용 타협이다(AArch32 VFP 의 d8–d15 관례를 이어받은 것으로 알려져 있음 — 확인 권장). 실측으로 보면, `ldp d8, d9` 복원과 gcc 의 `ldr d15` 복원이 모두 상위 절반을 0으로 만들었다(upper mask 0x03 / 0x83).

꼬리: "누가 비용을 내나?" → callee-saved 는 그 레지스터를 **쓰는** 피호출자가 낸다(ema_step 의 `stp d8, d9`). caller-saved 는 호출 너머로 값을 **살려야 하는** 호출자가 낸다.

</details>

<details>
<summary>Q3. 왜 s0 에 쓰면 v0 의 위쪽이 0이 되나?</summary>

w0 쓰기가 x0 위 32비트를 0으로 만드는 것과 같은 설계다. 결과가 이전 값의 위쪽에 의존하지 않으므로, 레지스터 리네이밍 하드웨어가 merge(부분 갱신) 연산을 할 필요가 없다. 32비트 ARM(AArch32)의 s0/s1 이 d0 의 두 절반이던 겹침 구조와 다르다. AArch64 에서 s1 은 d0 의 위 절반이 아니라 v1 의 아래 32비트다. lane 하나만 바꾸고 나머지를 지키려면 `mov v1.s[0], w8`(INS)처럼 **벡터 원소** 명령을 쓴다(실측: `0xFFFFFFFF3F800000`).

</details>

<details>
<summary>Q4. FMA 를 쓰면 왜 결과가 달라지나? 언제 문제가 되나?</summary>

`fmadd` 는 `a*b+c` 를 무한 정밀도로 계산하고 **한 번만** 반올림한다. 비융합은 곱에서 한 번, 합에서 또 한 번 반올림한다. 대부분은 FMA 가 더 정확하다(특별 입력에서 2^-24 vs 0). 문제는 **재현성**이다. 같은 C 소스라도 컴파일러, `-ffp-contract`, `-std=c11` vs `gnu11` 에 따라 FMA 여부와 위치가 바뀐다. 그래서 ARM 펌웨어 결과와 x86 골든 모델이 1 ulp 어긋난다. 해결: 비교가 중요한 코드는 `fmaf()` 를 명시하거나 `#pragma STDC FP_CONTRACT OFF`/`-ffp-contract=off` 로 고정한다. 그리고 비교는 bit-exact 대신 ulp 허용 오차로 한다.

</details>

<details>
<summary>Q5. s0 에 0.1f 를 어떻게 넣나? 3.0 은?</summary>

3.0 = 24/16 × 2^1 이라서 imm8 에 들어간다 → `fmov s0, #3.0` 한 줄. 0.1 은 2진 유한소수가 아니라서 안 된다(실측 에러). 방법은 셋이다. ① `movz/movk` 로 비트 패턴 0x3DCCCCCD 를 w8 에 만들고 `fmov s0, w8`(clang 의 선택, 실측) ② 리터럴 풀 `ldr s0, =…`/`ldr s0, label` ③ `adrp x8, const@PAGE; ldr s0, [x8, const@PAGEOFF]`(ARM-A09). 0.0 은 `fmov s0, wzr` 또는 `movi d0, #0`.

</details>

<details>
<summary>Q6. float 를 Q15 로 바꾸는 가장 짧은 코드는?</summary>

`fcvtzs w0, s0, #15` 가 x·2^15 를 자르고 int32 포화까지 한다. 곱셈 명령이 따로 필요 없다. 다만 int16 범위는 별도 clamp(`cmp`+`csel` ×2)가 필요하다. 1.0 → 32768 → 32767 이 그 경우다. 반대 방향 `scvtf s0, w0, #15` 는 q/2^15. 오디오 코덱·모터 제어(FOC)의 float↔Q 변환이 이 명령 하나로 끝난다. clang 도 `(int)(x*65536.0f)` 를 `fcvtzs w0, s0, #16` 으로 내린다(실측). fbits 범위는 32비트 정수 1–32, 64비트 1–64 다(인코딩상 scale = 64 − fbits).

</details>

<details>
<summary>Q7. FJCVTZS 라는 명령이 왜 있나?</summary>

ARMv8.3 FEAT_JSCVT 다. JavaScript 의 `ToInt32` 는 범위 밖 값을 **2^32 로 감싸는(modulo)** 의미다. 그래서 FCVTZS 의 포화와 다르다. JS 엔진이 자주 쓰는 변환이라 명령 하나로 만들었다. M6 실측 (`demo_fjcvtzs.c`): `3e9 → fjcvtzs −1294967296, fcvtzs 2147483647`; `4294967297.5 → 1 vs 2147483647`; `Inf → 0 vs 2147483647`; `−2.7 → −2 둘 다`. "명령어 집합이 소프트웨어 생태계를 따라간다"는 예다.

</details>

<details>
<summary>Q8. FPCR / FPSR 은? Cortex-M 은?</summary>

FPCR 은 제어 레지스터다: RMode[23:22](반올림), FZ[24](비정규수를 0으로), DN[25](default NaN), 예외 트랩 enable. FPSR 은 누적(sticky) 예외 플래그다: IOC, DZC, OFC, UFC, IXC, IDC 와 AArch32 호환 QC(비트 위치는 Arm ARM 기준). 둘 다 EL0 에서 `mrs/msr` 할 수 있다. 이 맥에서 FPCR = FPSR = 0x0 으로 실측했다. Cortex-M4F/M7 은 둘을 합친 **FPSCR** 하나를 쓰고(`vmrs/vmsr`), 레지스터는 s0–s31(M7 DP 는 d0–d15)이다. 예외 진입 시 lazy stacking(ARM-M09)이 FP 문맥 저장 비용을 줄인다. FZ 를 켜면 비정규수 처리가 빨라질 수 있지만(코어에 따라 다름), 결과가 IEEE 와 달라진다. DSP 루프에서만 켜는 이유다.

</details>

## 9. 추가 연습

1. **포화를 감지하라.** `uint32_t f2i_flags(float x, int32_t *out)` — FPSR 을 지우고 `fcvtzs` 한 뒤 FPSR 의 예외 비트를 돌려준다. 2.0 / 2.5 / NaN / 3e9 / −Inf / −2^31 / 최소 비정규수 / −0 의 기댓값을 먼저 예측하라.
2. **FMIN vs FMINNM.** `fmin_arm`, `fminnm_arm` — NaN 이 하나 섞였을 때, 둘 다 NaN 일 때, (+0, −0) 일 때 무엇을 돌려주나? C `fminf` 와 같은 쪽은?
3. **PWM 듀티 → 타이머 비교값.** `uint32_t duty_to_ccr(float duty, uint32_t arr)` = clamp(round_half_away(duty·arr), 0, arr). 음수·NaN → 0, 넘치면 arr. 분기 없이.
4. **부호 비트를 GPR 로.** `fabs_gpr(x)`, `copysign_gpr(mag, sgn)` — `fmov` 로 GPR 에 옮겨 비트 연산(ARM-B06의 `bfxil`). NaN 의 부호까지 `fabsf`/`copysignf` 와 비트 단위로 같게.
5. **FCVTZS 의 소프트웨어 모델 (C).** FP 명령 없이 비트만으로 `int32_t soft_fcvtzs(uint32_t bits)` 를 쓰고, 하드웨어와 2^31 근처 지수 전수 + 전체 공간 251 간격으로 대조하라(IEEE 분해는 ARCH-B03).

<details>
<summary>연습 1 답 — FPSR 플래그</summary>

`msr fpsr, xzr`(누적 플래그 지우기) → `fcvtzs w8, s0` → `str w8, [x0]` → `mrs x9, fpsr` → `mov w10, #0x9f; and w0, w9, w10` (0x9f 는 논리 즉시값이 아님, 7. 실수 7). 전체는 아래 practice.S.

결과 (테스트로 확인): 2.0 → 0, 2.5 → IXC(0x10), NaN → IOC(0x01), 3e9 → **IOC 만**(포화는 inexact 가 아니라 invalid — FPToFixed 의사코드에서 overflow 면 IOC, 아니고 버린 비트가 있으면 IXC), −Inf → IOC, −2^31 → 0, 0x1p−149 → IXC(FZ=0 이므로 IDC 아님), −0 → 0. C 의 `fetestexcept(FE_INVALID)` 가 읽는 것이 이 비트다.

</details>

<details>
<summary>연습 2 답 — NaN 처리 차이</summary>

`fmin s0, s0, s1`: NaN 이 하나라도 있으면 NaN. `fminnm s0, s0, s1`: quiet NaN 하나는 무시하고 숫자를 낸다. 이것이 IEEE 754-2008 minNum 이고 C `fminf` 와 같다(무작위 20만 쌍 비교 일치). 둘 다 NaN 이면 NaN. (+0, −0) 은 두 명령 모두 순서와 상관없이 **−0** 이었다(테스트로 확인 — Arm 의 FPMin 은 −0 < +0 으로 취급). signaling NaN 이 섞이면 fminnm 도 NaN 을 낸다(Arm ARM 기준, 미실측).

</details>

<details>
<summary>연습 3 답 — 분기 없는 CCR 계산</summary>

`ucvtf s1, w0`(arr→float) → `fmul s0, s0, s1` → `fcvtau w8, s0`(ties-away, 음수/NaN → 0 으로 부호 없는 포화) → `cmp w8, w0; csel w0, w8, w0, ls`(부호 없는 min). 분기 0개.

`duty_to_ccr(0.5, 999)` = 499.5 → **500**(AS 라서. NS 였으면 500 도 짝수라 같지만, 498.5 같은 경우는 갈린다). 음수 클램프를 `fcvtau` 의 포화가 대신 해 준다. 무작위 arr ∈ [1, 65535] × duty ∈ [−0.5, 1.5] 20만 개를 C 참조와 비교해서 일치했다.

</details>

<details>
<summary>연습 4 답 — fabs / copysign</summary>

`fabs_gpr` = `fmov w8, s0` → `and w8, w8, #0x7fffffff` → `fmov s0, w8`. `copysign_gpr` = `fmov w8, s0` / `fmov w9, s1` → `bfxil w9, w8, #0, #31` → `fmov s0, w9`.

`0x7fffffff` 는 논리 즉시값이다(31개 연속 1). `bfxil w9, w8, #0, #31` 은 w9[30:0] ← w8[30:0] 이고 w9 의 부호 비트는 그대로 둔다. 실전에서는 `fabs s0, s0` 한 줄이 더 낫다(FP 파이프에 머묾). 이 연습은 fmov 가 비트 복사라는 점을 익히는 데 목적이 있다. 100만 개 임의 비트 패턴(NaN 포함)이 libm 과 비트 단위로 같았다.

</details>

<details>
<summary>연습 5 답 — soft_fcvtzs</summary>

```c
if (exp == 0xFF) return frac ? 0 : (sign ? INT32_MIN : INT32_MAX);  /* NaN, ±Inf */
if (exp < 127) return 0;                    /* |x| < 1 */
int e = exp - 127;
if (e >= 31) return sign ? INT32_MIN : INT32_MAX;   /* −2^31 은 우연히 MIN 과 같음 */
uint32_t m = frac | 0x800000;
uint32_t mag = e >= 23 ? m << (e - 23) : m >> (23 - e);   /* 소수부 버림 */
return sign ? -(int32_t)mag : (int32_t)mag;
```

지수 156–158 × 양 부호 전수(5033만 개)와 전체 2^32 를 251 간격(1711만 개)으로 하드웨어 `fcvtzs` 와 대조해서 불일치 0. `e >= 31` 한 줄로 "포화"와 "−2^31 정확" 두 경우가 함께 처리된다는 점이 핵심이다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_abi.S -o pr && ./pr
# 26/26 passed   (ASan/UBSan 빌드도 26/26, VM gcc 빌드도 26/26)
```

## 10. 핵심 정리

- v0–v31 은 128비트이고, `q/d/s/h/b` 는 같은 레지스터의 아래쪽 뷰다. **스칼라 쓰기는 위쪽을 0으로**, 원소 삽입(`mov v.s[i], w`)만 나머지를 보존한다.
- FP 인자는 `s0..s7`/`d0..d7`, 반환은 `s0`/`d0`. 정수 인자와 **카운터가 따로**다(`mix_args(i,f,l,d)` → w0, s0, x1, d1).
- callee-saved 는 **d8–d15(하위 64비트)만**이다. 콜백 너머로 쓸 FP 값은 s8–s15 에 두고 `stp d8, d9` 로 저장한다. 반환값 테스트로는 안 잡히니 센티널로 검사한다.
- `fmadd Sd,Sn,Sm,Sa = Sa + Sn·Sm`(반올림 1회). C 참조는 `fmaf` 로 명시한다. `a*b+c` 는 `-ffp-contract`/`-std` 에 따라 바뀐다.
- `FCVTZS/ZU` = 0 쪽 자름 + 폭 기준 포화 + NaN→0 + FPSR.IOC. C `(int)x` 는 범위 밖이면 UB 다(`-O2` 상수 접기 = 0, 실행 = 포화). N/M/P/A/Z 는 명령에 박힌 반올림 방향이다. `roundf`→`fcvtas`, `floorf`→`fcvtms`.
- `#fbits` 고정소수점 형식: `fcvtzs w0, s0, #15`(Q15), `ucvtf s1, w0, #12`(÷4096). 컴파일러도 쓴다.
- `lerp = a + t(b−a)` 는 t=1 에서 b 를 놓친다(9.5% 실측). `fmsub`+`fmadd` 의 `(a − t·a) + t·b` 는 양 끝이 정확하다.
- FMOV 즉시값은 ±(16..31)/16·2^(−3..4) 뿐이다. 0.1 은 `movz/movk` + `fmov s, w`.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — FCVTZS/FCVTNS/…/SCVTF/UCVTF (scalar, integer & fixed-point), FMADD/FMSUB, FMOV (scalar, immediate), VFPExpandImm, FPToFixed, FPMin/FPMinNum, FPCR/FPSR: <https://developer.arm.com/documentation/ddi0487/latest>
- A64 Instruction Set Architecture (인코딩 표): <https://developer.arm.com/documentation/ddi0602/latest>
- Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64) — 6.1.2 SIMD and Floating-Point registers, 6.8 Parameter passing (NSRN): <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
- Apple, Writing ARM64 code for Apple platforms: <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
- Clang User's Manual, `-ffp-contract`: <https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract>
- GCC Optimize Options, `-ffp-contract`: <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>
- ISO C11 (N1570) 6.3.1.4 Real floating and integer: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- 관련 레슨: ARCH-B03 (IEEE 754 분해), ARCH-B02 (NZCV), ARM-B01 (Apple vs AAPCS64), ARM-B03 (MOVZ/MOVK·논리 즉시값), ARM-B06 (BFXIL), ARM-B08 (스택 프레임), ARM-B09 (lldb), ARM-M01 (HFA·구조체 반환), ARM-M06 (NEON), ARM-A09 (ADRP), ARM-A11 (벡터화 출력)

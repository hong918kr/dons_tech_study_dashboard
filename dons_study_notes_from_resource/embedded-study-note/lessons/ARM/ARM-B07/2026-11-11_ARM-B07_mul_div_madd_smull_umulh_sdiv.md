---
id: ARM-B07
time: 60분
updated: 2026-11-11
---
# ARM-B07 곱셈·나눗셈: MADD, SMULL, UMULH, SDIV

## 학습 목표

- `MUL/MADD/MSUB/MNEG`, `SMULL/UMULL` (32×32→64), `SMULH/UMULH` (64×64→128 의 상위 절반), `UDIV/SDIV` 가 **각각 무엇을 계산하는지** 한 줄로 말하고, `.S` 함수로 직접 쓴다.
- 128비트 곱 = `mul`(하위) + `umulh`/`smulh`(상위) 두 명령이라는 것과, **하위 64비트는 signed/unsigned 가 같고 상위만 다르다**는 이유를 식으로 설명한다.
- 나머지 = `udiv/sdiv` + `msub` 두 줄로 만들고, `unsigned __int128` / C 의 `%` 와 랜덤·경계 입력으로 대조한다.
- **ARM 은 0 나누기에 트랩이 없다** (`x/0 → 0`, `INT64_MIN/-1 → INT64_MIN`) 를 이 Mac 에서 실측하고, 같은 입력이 x86-64 (Rosetta 2) 에서는 `SIGFPE` 라는 것을 실제로 보인다. C 에서는 둘 다 UB 라는 것도.
- 펌웨어 고정소수점: `UMULH` + `EXTR` 로 Q32.32 곱을 만들어 24 MHz 타이머 tick → ns 변환을 하고, 오차 한계를 증명·측정한다.

## 1. 문제

크래시 덤프에서 `mov x8, #0xcccccccccccccccc` / `movk` / `umulh` / `lsr #3` 를 보고 "이거 그냥 `x / 10` 이에요" 라고 바로 말할 수 있는가, 그리고 "ARM 에서 0 으로 나누면 어떻게 되나요?" 에 "트랩 없이 0 이 나와서 버그가 **조용히** 숨는다" 라고 답할 수 있는가 — 펌웨어 면접의 단골 asm 읽기 + ISA 질문이다.

SSD 컨트롤러 펌웨어에서도 늘 나오는 계산들이다: LBA → (die, plane, block, page) 로 쪼개는 `/` 와 `%`, 타임스탬프 tick → ns 변환, 통계용 64비트 누적. Cortex-R/A 코어에서는 이것들이 전부 이 레슨의 명령 몇 개로 내려간다. Cortex-M0 처럼 나눗셈 명령이 아예 없는 코어도 있다 (Q5).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">64비트 × 64비트 = 128비트 곱 — 어느 명령이 어느 조각을 주나</text>
  <text x="40" y="50" fill="currentColor" font-family="monospace">a = b = 0xFFFFFFFFFFFFFFFF  →  a*b = 2^128 - 2^65 + 1</text>
  <rect x="40" y="64" width="320" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="360" y="64" width="320" height="34" class="f-ok" stroke="currentColor"/>
  <text x="200" y="86" fill="currentColor" text-anchor="middle" font-family="monospace">hi = 0xFFFFFFFFFFFFFFFE</text>
  <text x="520" y="86" fill="currentColor" text-anchor="middle" font-family="monospace">lo = 0x0000000000000001</text>
  <g fill="currentColor" font-size="12">
    <text x="40" y="114">127</text><text x="342" y="114">64</text><text x="364" y="114">63</text><text x="672" y="114">0</text>
  </g>
  <text x="200" y="136" fill="currentColor" text-anchor="middle">UMULH (unsigned) / SMULH (signed)</text>
  <text x="520" y="136" fill="currentColor" text-anchor="middle">MUL — signed/unsigned 결과 동일</text>
  <rect x="200" y="152" width="320" height="30" class="f-acc" stroke="currentColor"/>
  <text x="360" y="172" fill="currentColor" text-anchor="middle" font-family="monospace">[95:32]  EXTR x0, hi, lo, #32</text>
  <text x="530" y="172" fill="currentColor">→ Q32.32 곱 (Part C)</text>
  <line x1="200" y1="98" x2="200" y2="152" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="520" y1="98" x2="520" y2="152" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="360" y="210" width="320" height="30" class="f-mut" stroke="currentColor"/>
  <text x="520" y="230" fill="currentColor" text-anchor="middle" font-family="monospace">UMULL/SMULL x0, w0, w1</text>
  <text x="40" y="230" fill="currentColor">32비트 × 32비트 → 64비트 전부:</text>
  <rect x="520" y="256" width="160" height="30" class="f-warn" stroke="currentColor"/>
  <text x="600" y="276" fill="currentColor" text-anchor="middle" font-family="monospace">MUL w0, w0, w1</text>
  <text x="40" y="276" fill="currentColor">32비트 곱 (하위 32비트만, 넘친 부분은 버림):</text>
  <text x="40" y="314" fill="currentColor">C 의 uint32 * uint32 는 이쪽(mul w)이다 → 64비트가 필요하면 (uint64_t)a * b 로 써야 umull 이 나온다.</text>
</svg>
```

| 명령 | 계산 | 비고 |
|---|---|---|
| `mul xd, xn, xm` | `xn*xm` 하위 64비트 | `madd xd, xn, xm, xzr` 의 별칭 |
| `madd xd, xn, xm, xa` / `msub` | `xa + xn*xm` / `xa - xn*xm` | **더하는 값이 맨 끝** operand |
| `mneg xd, xn, xm` | `-(xn*xm)` | `msub ..., xzr` 의 별칭 |
| `umull/smull xd, wn, wm` | 32×32→64 (zero/sign 확장 후 곱) | `umaddl/smaddl ..., xzr` 의 별칭 |
| `umulh/smulh xd, xn, xm` | 128비트 곱의 비트 [127:64] | **64비트 형식만** 있다 |
| `udiv/sdiv xd, xn, xm` | 0 쪽으로 버리는 몫 | 나머지 명령은 없다 → `msub` |

### Part A — 곱셈: 128비트 곱과 32→64 확장 곱

`mul128_u(a, b, &hi, &lo)` (unsigned, `UMULH`), `mul128_s` (signed, `SMULH`), `mulhi_u64`, `umull32`, `smull32`, 그리고 `madd_u64 / msub_u64 / mneg_s64`. 모두 스택을 안 쓰는 leaf 함수다. `unsigned __int128` 참조 모델과 20만 개 랜덤 입력, 이름 붙인 경계값 (`UINT64_MAX²`, `INT64_MIN²`, `INT64_MIN × -1`) 으로 대조한다.

### Part B — 나눗셈·나머지와 "C 에선 UB, ISA 에선 정의됨"

`udiv_u64`, `sdiv_s64`, `sdiv_s32`, `mod_u64`, `mod_s64` (`DIV` + `MSUB`). 0 나누기와 `INT64_MIN / -1` 은 **asm 을 호출하고 손으로 쓴 기대값**과 비교한다 (C 로 계산하면 그 자체가 UB). 그리고 하네스에 **일부러 심은 버그 3개** (`udiv` 로 signed 나머지, `umulh` 로 signed 상위, `mul w` 로 32×32) 를 테스트가 잡는지 확인한다. 마지막으로 같은 나눗셈을 x86-64 에서 돌려 `SIGFPE` 를 실측한다 (`demo_div_trap.c`).

### Part C — 펌웨어: Q32.32 고정소수점으로 타이머 tick → ns

`q32_mul(a, b) = (a*b) >> 32` (`MUL` + `UMULH` + `EXTR`) 와 `calc_mult(freq) = round(1e9·2³² / freq)` (`MOVZ/MOVK` + `UDIV`). 24 MHz 타이머 (이 Mac 의 `mach_absolute_time` tick 이 정확히 이것) 에서 `ticks_to_ns` 의 오차가 **하루치 tick 전 범위에서 `t/2³³ + 1` ns 이하**임을 테스트한다. FPU 없는 MCU 나 ISR 안에서 실수 연산 대신 쓰는 바로 그 방식이다.

## 2. Problem (English)

> **Multiply and divide in AArch64 assembly.**
>
> Write leaf functions in a `.S` file (no stack, no calls, AAPCS64: args in x0–x7/w0–w7, result in x0/w0):
>
> 1. `void mul128_u(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)` and `void mul128_s(int64_t a, int64_t b, int64_t *hi, uint64_t *lo)` — the full 128-bit product. `uint64_t mulhi_u64(uint64_t, uint64_t)`, `uint64_t umull32(uint32_t, uint32_t)`, `int64_t smull32(int32_t, int32_t)`, `madd_u64(a,b,c) = c + a*b`, `msub_u64(a,b,c) = c - a*b`, `mneg_s64(a,b) = -(a*b)` (wrapping).
> 2. `udiv_u64`, `sdiv_s64`, `int32_t sdiv_s32(int32_t, int32_t)`, and `mod_u64` / `mod_s64` using one divide plus `MSUB`. Division by zero and `INT64_MIN / -1` must return the architecturally defined result (no trap, no branch).
> 3. `uint64_t q32_mul(uint64_t a, uint64_t b)` returning bits [95:32] of the 128-bit product, and `uint64_t calc_mult(uint64_t freq_hz)` returning `round(1e9 * 2^32 / freq_hz)`; `ticks_to_ns(t) = q32_mul(t, calc_mult(f))`.
>
> Examples: `mul128_u(UINT64_MAX, UINT64_MAX)` → hi `0xFFFFFFFFFFFFFFFE`, lo `1`. `mod_s64(-7, 2) == -1`, `mod_s64(7, -2) == 1`. `sdiv_s64(INT64_MIN, -1) == INT64_MIN`, `udiv_u64(x, 0) == 0`, `mod_u64(x, 0) == x`. `q32_mul(1.5 in Q32.32, 2.0) == 3.0`. `calc_mult(24000000) == 178956970667`.
>
> Follow-ups: Why are the low 64 bits identical for signed and unsigned multiply? What does x86 do on `INT64_MIN / -1`? How does a compiler divide by 10 without `UDIV`? How do you detect 64-bit multiply overflow? What happens on a Cortex-M0, which has no divide instruction?

## 3. 시작 코드

하네스 (C, `starter.S` 와 `solution.S` 공용 — 프로토타입 + `CHECK` + 참조 모델):

<!-- code: harness.c -->

(빌드는 아래 `starter.S` 와 함께 — `cc ... harness.c starter.S`.)

TODO 가 있는 asm (`mov x0, #0` 스텁: 틀린 값을 돌려주지만 메모리는 안 건드리고 무한 루프도 없다):

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S -o /tmp/ARM-B07_starter && /tmp/ARM-B07_starter
```

처음 실행 결과 (실제 출력 일부, exit 1):

```text
FAIL  harness.c:66  hi == 0xFFFFFFFFFFFFFFFEull && lo == 1u
FAIL  harness.c:68  hi == 1u && lo == 0u
...
      200000 mismatches, first: a=dc1b77ae0bf34dad b=0000000000000193
FAIL  harness.c:82  report(bad, "mul128_u, mulhi_u64 vs unsigned __int128")
...
PASS  udiv_u64(12345, 0) == 0u
PASS  sdiv_s64(-12345, 0) == 0
FAIL  harness.c:141  sdiv_s64(INT64_MIN, -1) == INT64_MIN
...
INFO  1 day = 2073600000000 ticks -> approx 0 ns, exact 86400000000000 ns, drift -86400000000000 ns
9/50 passed
```

스텁이 `0` 을 돌려주기 때문에 "0 나누기 → 0" 체크 몇 개는 우연히 통과한다 — 이것 자체가 "ARM 에선 0 나누기 결과가 그냥 0" 이라는 힌트다. 심어 둔 버그 3개를 잡는 체크도 (버그 함수는 starter 에도 이미 있으므로) 처음부터 PASS 다.

**Linux/ELF 에서도 같은 파일이 빌드된다.** 파일 맨 위의 `SYM()` 매크로가 Mach-O 의 `_` 접두사와 ELF 의 `.type`/`.size` 차이를 흡수한다:

```c
#ifdef __APPLE__
#  define SYM(n)      _##n                 /* Mach-O: C 이름 앞에 '_' */
#  define TYPE_FN(n)                       /* Mach-O 에는 .type/.size 가 없다 */
#  define SIZE_FN(n)
#else
#  define SYM(n)      n                    /* ELF: 이름 그대로 */
#  define TYPE_FN(n)  .type n, %function
#  define SIZE_FN(n)  .size n, . - n
#endif
```

Lima VM (aarch64 Ubuntu) 에서 실제로 빌드·실행했다:

```text
$ limactl shell elinux -- bash -lc 'for c in gcc clang; do $c --version | head -1; \
    $c -std=c11 -Wall -Wextra -Werror -O2 $D/harness.c $D/solution.S -o /tmp/ARM-B07_work/sol_$c && /tmp/ARM-B07_work/sol_$c | tail -1; done'
gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
50/50 passed
Ubuntu clang version 21.1.8 (6ubuntu1)
50/50 passed
$ readelf -s /tmp/ARM-B07_work/sol_gcc | grep -E "mul128_u|q32_mul"
    96: 00000000000029cc    20 FUNC    GLOBAL DEFAULT   12 mul128_u
   101: 0000000000002a70    16 FUNC    GLOBAL DEFAULT   12 q32_mul
```

`.size` 덕분에 ELF 심볼에 크기 (20 B = 명령 5개, 16 B = 4개) 와 `FUNC` 타입이 붙는다 — gdb/perf 가 함수 경계를 아는 근거다. Mac `nm` 에서는 `_mul128_u` 처럼 `_` 가 붙어 보인다.

## 4. 힌트

<details>
<summary>힌트 1 — 128비트 곱은 명령 두 개</summary>

`mul x8, x0, x1` 이 하위 64비트, `umulh x9, x0, x1` 이 상위 64비트. 둘 다 같은 입력을 받으므로 순서는 상관없다. 저장은 `str x9, [x2]` / `str x8, [x3]` (포인터는 3·4번째 인자 = x2, x3).

</details>

<details>
<summary>힌트 2 — signed 버전은 상위 명령만 바꾼다</summary>

하위 64비트는 2의 보수에서 signed/unsigned 가 **같다** (mod 2⁶⁴ 산술). 다른 건 상위 절반뿐 → `smulh`. 32×32→64 도 마찬가지로 `umull`/`smull` 이 입력을 zero/sign 확장해 준다.

</details>

<details>
<summary>힌트 3 — 나머지 명령은 없다</summary>

`r = a - (a/b)*b`. `udiv x8, x0, x1` 다음 `msub x0, x8, x1, x0` (= `x0 - x8*x1`). `msub` 의 마지막 operand 가 "빼지는 쪽" 이다. b = 0 이면 몫이 0 이라 `r = a` 가 저절로 나온다.

</details>

<details>
<summary>힌트 4 — Q32.32 곱은 128비트 곱의 가운데</summary>

`(hi:lo) >> 32` 의 하위 64비트 = `hi` 의 하위 32비트를 위로, `lo` 의 상위 32비트를 아래로. 시프트 두 번 + orr 대신 `extr x0, x9, x8, #32` 한 개.

</details>

<details>
<summary>힌트 5 — calc_mult 의 상수 1e9 · 2³²</summary>

`1e9 = 0x3B9ACA00` 이므로 `1e9 << 32 = 0x3B9ACA00_00000000` — 0 이 아닌 halfword 가 둘이라 `movz #0x3B9A, lsl #48` + `movk #0xCA00, lsl #32` 두 줄 (ARM-B03). 반올림은 나누기 전에 `freq/2` 를 더한다: `add x8, x8, x0, lsr #1` (시프트된 레지스터 operand).

</details>

## 5. 풀이

### Part A — 곱셈 명령은 한 가족 (Data-processing, 3 source)

`MUL/MADD/MSUB/MNEG/SMULL/UMULL/SMULH/UMULH` 는 전부 **같은 인코딩 한 칸**에서 `op31` 과 `o0` 두 필드만 다르다. 아래 32비트 값은 `solution.S` 를 어셈블한 오브젝트의 실제 `objdump -d` 결과다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Data-processing (3 source) 인코딩 — 곱셈 가족 전부</text>
  <g stroke="currentColor">
    <rect x="34" y="40" width="21" height="30" class="f-mut"/>
    <rect x="55" y="40" width="42" height="30" class="f-mut"/>
    <rect x="97" y="40" width="105" height="30" class="f-mut"/>
    <rect x="202" y="40" width="63" height="30" class="f-acc"/>
    <rect x="265" y="40" width="105" height="30" class="f-ok"/>
    <rect x="370" y="40" width="21" height="30" class="f-acc"/>
    <rect x="391" y="40" width="105" height="30" class="f-ok"/>
    <rect x="496" y="40" width="105" height="30" class="f-ok"/>
    <rect x="601" y="40" width="105" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="44" y="60">sf</text><text x="76" y="60">op54</text><text x="149" y="60">11011</text>
    <text x="233" y="60">op31</text><text x="317" y="60">Rm</text><text x="380" y="60">o0</text>
    <text x="443" y="60">Ra</text><text x="548" y="60">Rn</text><text x="653" y="60">Rd</text>
    <text x="44" y="86">31</text><text x="76" y="86">30:29</text><text x="149" y="86">28:24</text>
    <text x="233" y="86">23:21</text><text x="317" y="86">20:16</text><text x="380" y="86">15</text>
    <text x="443" y="86">14:10</text><text x="548" y="86">9:5</text><text x="653" y="86">4:0</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="44" y="112">1</text><text x="76" y="112">00</text><text x="149" y="112">11011</text><text x="233" y="112">000</text>
    <text x="317" y="112">00001</text><text x="380" y="112">0</text><text x="443" y="112">00010</text><text x="548" y="112">00000</text><text x="653" y="112">00000</text>
    <text x="44" y="134">1</text><text x="76" y="134">00</text><text x="149" y="134">11011</text><text x="233" y="134">110</text>
    <text x="317" y="134">00001</text><text x="380" y="134">0</text><text x="443" y="134">11111</text><text x="548" y="134">00000</text><text x="653" y="134">01001</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="34" y="156">↑ 0x9b010800  madd  x0, x0, x1, x2     ↑ 0x9bc17c09  umulh x9, x0, x1 (Ra=11111)</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="34" y="190">op31 o0  명령       실제 예 (objdump)</text>
    <text x="34" y="208">000  0   MADD       9b010800 madd x0,x0,x1,x2 / 9b017c08 mul x8,x0,x1 (Ra=xzr)</text>
    <text x="34" y="226">000  1   MSUB       9b018800 msub x0,x0,x1,x2 / 9b01fc00 mneg x0,x0,x1</text>
    <text x="34" y="244">001  0   SMADDL     9b217c00 smull x0,w0,w1   (SMSUBL = 001 1)</text>
    <text x="34" y="262">101  0   UMADDL     9ba17c00 umull x0,w0,w1   (UMSUBL = 101 1)</text>
    <text x="34" y="280">010/110  SMULH/UMULH 9b417c09 smulh / 9bc17c09 umulh  (64비트만: sf=1)</text>
  </g>
</svg>
```

- `mul`·`mneg`·`smull`·`umull` 은 **별칭(alias)** 이다: Ra 에 `xzr`(11111) 을 넣은 `madd/msub/smaddl/umaddl`. 그래서 disassembler 가 `madd ..., xzr` 대신 `mul` 로 보여준다.
- 나눗셈은 다른 칸 (Data-processing, 2 source) 이다: `udiv x0, x0, x1` = `0x9ac10800`, `sdiv` = `0x9ac10c00` (opcode 필드 000010 / 000011). `sf=0` 이면 32비트판 (`sdiv w0, w0, w1` = `0x1ac10c00`).
- 즉시값(immediate) 형식은 없다. `mul x0, x0, #3`, `udiv x0, x0, #10` 은 어셈블러가 거부한다 (7절에 실제 에러).

**왜 하위 64비트는 signed/unsigned 가 같은가.** 64비트 패턴 `a` 를 unsigned 로 읽으면 `A = a + 2⁶⁴·s_a` (s_a = 부호 비트). 그러면

`A·B = a·b + 2⁶⁴·(s_a·b + s_b·a) + 2¹²⁸·s_a·s_b`

둘째·셋째 항은 2⁶⁴ 의 배수라 **mod 2⁶⁴ (하위 64비트) 에 영향이 없다**. 상위 64비트에는 영향이 있다 → 보정식 `smulh = umulh − (a<0 ? b : 0) − (b<0 ? a : 0)` (mod 2⁶⁴). 이것을 직접 짜는 것이 추가 연습 1.

`mul128_u(UINT64_MAX, UINT64_MAX, &hi, &lo)` 한 줄씩:

| # | 명령 | x0 | x1 | x8 | x9 | 메모리 |
|---|---|---|---|---|---|---|
| 0 | (진입) | `FFFF…FFFF` | `FFFF…FFFF` | ? | ? | |
| 1 | `mul x8, x0, x1` | | | `0000…0001` | | |
| 2 | `umulh x9, x0, x1` | | | | `FFFF…FFFE` | |
| 3 | `str x9, [x2]` | | | | | `*hi = FFFF…FFFE` |
| 4 | `str x8, [x3]` | | | | | `*lo = 1` |

(2⁶⁴−1)² = 2¹²⁸ − 2⁶⁵ + 1 이므로 상위 = 2⁶⁴ − 2, 하위 = 1. 같은 입력을 `mul128_s` 에 넣으면 (−1)·(−1) = 1 → hi = 0, lo = 1: **하위는 같고 상위만 다르다**.

**clang 은 같은 일을 어떻게 하나** (`demo_clang.c`, `clang -O2 -S -fno-asynchronous-unwind-tables`, 지시어 줄만 걸러낸 실제 출력):

```text
_c_mul128_u:                  ; return (unsigned __int128)a * b;
	mul	x8, x1, x0
	umulh	x1, x1, x0            ; __int128 반환값은 x0(하위):x1(상위)
	mov	x0, x8
	ret
_c_mul128_s:                  ; return (__int128)a * b;
	mul	x8, x1, x0
	smulh	x1, x1, x0
	mov	x0, x8
	ret
_c_madd:                      ; return c + a * b;
	madd	x0, x1, x0, x2
	ret
_c_widen_ok:                  ; return (uint64_t)a * b;   (a, b 는 uint32_t)
	umull	x0, w1, w0
	ret
_c_widen_bug:                 ; return a * b;             ← 32비트 곱 후 확장
	mul	w0, w1, w0
	ret
```

`c_widen_bug` 가 하네스의 `umull32_bug` 와 **글자 그대로 같은 명령** 이다. C 의 `uint32_t * uint32_t` 는 32비트에서 계산되고 (C-B02 의 정수 승격 규칙), 결과를 `uint64_t` 로 돌려줘도 넘친 비트는 이미 사라졌다. 실제로 `umull32_bug(0xFFFFFFFF, 0xFFFFFFFF) = 0x1`, 정답은 `0xFFFFFFFE00000001`. 하나 더: `w` 레지스터에 쓰면 `x` 의 상위 32비트가 0 이 되므로 (ARM-B01) 반환값의 상위가 "쓰레기" 가 아니라 **깨끗한 0** 이다 — 그래서 이 버그는 더 찾기 어렵다.

### Part B — 나눗셈: ISA 가 정의한 결과, C 에선 UB

Arm ARM 의 `SDIV/UDIV` 의사코드는 한 줄로 요약된다 (DDI 0487, SDIV 항목 — 확인 권장):

```text
if IsZero(operand2) then result = 0;
else result = RoundTowardsZero(Real(operand1) / Real(operand2));
X[d] = result<datasize-1:0>;          // 64(또는 32)비트로 잘라 넣는다
```

- **0 나누기 → 0.** 예외(exception)도 플래그도 없다.
- **`INT64_MIN / -1`**: 수학적 몫은 +2⁶³ 인데 64비트로 자르면 `0x8000000000000000` = `INT64_MIN`. 역시 트랩 없음.
- 나머지 `a − q·b` 를 `msub` 로 만들면: `mod_u64(a, 0) = a − 0·0 = a`, `mod_s64(INT64_MIN, −1) = INT64_MIN − INT64_MIN·(−1) = INT64_MIN − INT64_MIN = 0` (mod 2⁶⁴).

`mod_s64(-7, 2)` 추적:

| # | 명령 | x0 | x1 | x8 |
|---|---|---|---|---|
| 0 | (진입) | −7 (`0xFFFF…FFF9`) | 2 | ? |
| 1 | `sdiv x8, x0, x1` | | | −3 (0 쪽 버림, −3.5 → −3) |
| 2 | `msub x0, x8, x1, x0` | −7 − (−3)(2) = **−1** | | |

C99 이후의 `/` 와 `%` 도 "0 쪽 버림, 나머지 부호 = 피제수 부호" 라서 `sdiv + msub` 가 C 의 `%` 와 정확히 일치한다 (C11 6.5.5p6: `(a/b)*b + a%b == a`). 파이썬의 `-7 % 2 == 1` (floor division) 과는 다르다 — 면접에서 잘 섞는 포인트.

| 입력 | AArch64 `SDIV`/`UDIV` (+`MSUB`) | C11 추상 기계 | x86-64 `IDIV`/`DIV` |
|---|---|---|---|
| `7 / 0` | 0 | **UB** (6.5.5p5) | #DE 예외 → `SIGFPE` |
| `7 % 0` | 7 (`a − 0·0`) | **UB** | #DE → `SIGFPE` |
| `INT64_MIN / -1` | `INT64_MIN` | **UB** (몫이 표현 불가, 6.5.5p6) | #DE → `SIGFPE` |
| `INT64_MIN % -1` | 0 | **UB** (같은 조항: `a%b` 도 UB) | #DE → `SIGFPE` |

**하네스가 이 줄들을 C 로 계산하지 않는 이유.** C 표준은 "x/0 의 결과" 를 정의하지 않는다. 컴파일러는 그런 입력이 **오지 않는다고 가정**하고 최적화해도 된다. 그래서 `expect = a / 0;` 같은 참조 계산은 "ISA 결과" 가 아니라 "컴파일러가 아무거나 낸 값" 이 될 수 있다. 하네스는 asm (= ISA 가 정의한 동작) 을 호출하고 **손으로 쓴 상수** `0`, `INT64_MIN`, `12345` 와 비교한다. 랜덤 대조 루프는 `b == 0` 과 `(INT64_MIN, -1)` 조합을 `continue` 로 건너뛴다.

**랜덤 피제수/제수를 어떻게 뽑나.** 64비트 균등 난수 두 개를 나누면 몫이 거의 항상 0~몇 이라 나눗셈 경로를 제대로 못 흔든다. `rnd_mag()` 는 난수를 0~63 비트 오른쪽으로 밀어 **크기(비트 길이)가 고르게** 퍼지게 한다 → 몫이 0 부터 2⁶³ 근처까지 골고루 나온다.

**심어 둔 버그 3개 — 테스트가 잡는가** (실제 값, `solution.S` 의 버그 함수를 호출):

| 버그 함수 | 입력 | 버그 결과 | 정답 | 왜 |
|---|---|---|---|---|
| `mod_s64_bug` (`udiv`) | (−7, 2) | 1 | −1 | −7 을 2⁶⁴−7 로 봄 → q = 0x7FFF…FFFC |
| `mod_s64_bug` | (7, −2) | 7 | 1 | −2 를 거대한 수로 봄 → q = 0 |
| `mulhi_s64_bug` (`umulh`) | (−1, 1) | 0 | −1 | unsigned 로는 (2⁶⁴−1)·1 의 상위 = 0 |
| `umull32_bug` (`mul w`) | (0x10000, 0x10000) | 0 | 0x100000000 | 2³² 이 32비트에서 넘쳐 사라짐 |

교훈: 부호 버그는 **음수 입력이 하나라도 있으면** 바로 드러나지만, 양수만 테스트하면 영원히 통과한다. 경계값 표에 −1, −7, `INT64_MIN` 이 꼭 들어가야 하는 이유.

**x86 대조 실측** (`demo_div_trap.c` — 같은 소스를 arm64 네이티브와 `-arch x86_64` (Rosetta 2 로 실행) 로 빌드. `arch -x86_64 /usr/bin/true` 가 성공해서 Rosetta 설치를 먼저 확인했다):

<!-- code: demo_div_trap.c -->

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 demo_div_trap.c -o /tmp/ARM-B07_div_arm && /tmp/ARM-B07_div_arm
arch = arm64
  hw sdiv 7 / 0                = 0 (0x0000000000000000)
  hw udiv 7 / 0                = 0 (0x0000000000000000)
  hw sdiv INT64_MIN / -1       = -9223372036854775808 (0x8000000000000000)
  C   7 / 0          (UB)      = 0 (0x0000000000000000)
  C   INT64_MIN / -1 (UB)      = -9223372036854775808 (0x8000000000000000)
  C   INT64_MIN % -1 (UB)      = 0 (0x0000000000000000)
done (no crash)

$ cc -arch x86_64 -std=c11 -Wall -Wextra -Werror -O2 demo_div_trap.c -o /tmp/ARM-B07_div_x86 && /tmp/ARM-B07_div_x86
arch = x86_64
  hw sdiv 7 / 0                -> SIGFPE si_code=7 (FPE_INTDIV)
  hw udiv 7 / 0                -> SIGFPE si_code=7 (FPE_INTDIV)
  hw sdiv INT64_MIN / -1       -> SIGFPE si_code=7 (FPE_INTDIV)
  C   7 / 0          (UB)      -> SIGFPE si_code=7 (FPE_INTDIV)
  C   INT64_MIN / -1 (UB)      -> SIGFPE si_code=7 (FPE_INTDIV)
  C   INT64_MIN % -1 (UB)      -> SIGFPE si_code=7 (FPE_INTDIV)
done (no crash)
```

- x86 은 0 나누기와 몫 오버플로 **둘 다 같은 #DE (divide error)** 예외라서 `si_code` 도 둘 다 `FPE_INTDIV` 로 보인다 (`FPE_INTOVF` 가 아니다). 이것은 Rosetta 2 위에서 관찰한 값이다 — 네이티브 Intel Mac 에서도 같을 것으로 보이지만 직접 확인하지는 않았다.
- `INT64_MIN % -1` 도 x86 에서 죽는다: 나머지도 `idiv` 한 번으로 계산하기 때문 (몫이 넘치면 나머지가 0 이어도 #DE). 실제 x86-64 clang 출력 (`a % b`):

```text
_c_smod:                        (push/pop rbp 프레임 줄 생략)
	movq	%rdi, %rax
	movq	%rdi, %rcx
	orq	%rsi, %rcx
	shrq	$32, %rcx
	je	LBB0_1          ; 둘 다 상위 32비트가 0 이면 빠른 divl 경로
	cqto
	idivq	%rsi            ; ← INT64_MIN % -1 은 여기서 #DE
	movq	%rdx, %rax          ; 나머지는 RDX
	retq
LBB0_1:
	xorl	%edx, %edx
	divl	%esi
	movl	%edx, %eax
	retq
```

  같은 함수의 arm64 출력은 `sdiv x8, x0, x1` / `msub x0, x8, x1, x0` / `ret` 세 줄. x86 쪽의 "둘 다 32비트면 `divl`" 분기는 64비트 `idivq` 가 느린 코어를 위한 clang 의 최적화로 보인다 (의도는 추정).
- UBSan 은 C 경로를 잡는다 (`cc -O1 -fsanitize=undefined demo_div_trap.c`, stderr 실제 출력):

```text
demo_div_trap.c:51:82: runtime error: division by zero
demo_div_trap.c:52:82: runtime error: division of -9223372036854775808 by -1 cannot be represented in type 'int64_t' (aka 'long long')
```

  보고가 2개뿐인 이유: UBSan 은 **같은 소스 위치는 한 번만** 보고한다. 51번 줄 (`c_sdiv`) 은 `7/0` 에서 이미 보고했으므로 `INT64_MIN / -1` 은 조용히 넘어갔다. asm 경로 (`hw_*`) 는 UBSan 이 아예 보지 못한다 — asm 은 C 가 아니므로 UB 도 아니다.

### Part C — Q32.32: 타이머 tick 을 ns 로

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Q32.32: 64비트 = 정수부 32비트 . 소수부 32비트  (값 = 비트 패턴 / 2^32)</text>
  <rect x="40" y="40" width="320" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="360" y="40" width="320" height="34" class="f-ok" stroke="currentColor"/>
  <text x="200" y="62" fill="currentColor" text-anchor="middle" font-family="monospace">0x00000029 = 41</text>
  <text x="520" y="62" fill="currentColor" text-anchor="middle" font-family="monospace">0xAAAAAAAB ≈ 0.6666666667</text>
  <text x="200" y="92" fill="currentColor" text-anchor="middle">정수부 [63:32]</text>
  <text x="520" y="92" fill="currentColor" text-anchor="middle">소수부 [31:0]</text>
  <text x="40" y="120" fill="currentColor" font-family="monospace">mult = calc_mult(24 MHz) = 178956970667 = 0x29_AAAAAAAB ≈ 41.666666667 ns/tick</text>
  <rect x="40" y="138" width="150" height="30" class="f-mut" stroke="currentColor"/>
  <text x="115" y="158" fill="currentColor" text-anchor="middle">ticks (정수)</text>
  <text x="205" y="158" fill="currentColor">×</text>
  <rect x="225" y="138" width="150" height="30" class="f-mut" stroke="currentColor"/>
  <text x="300" y="158" fill="currentColor" text-anchor="middle">mult (Q32.32)</text>
  <text x="390" y="158" fill="currentColor">→</text>
  <rect x="410" y="138" width="150" height="30" class="f-warn" stroke="currentColor"/>
  <text x="485" y="158" fill="currentColor" text-anchor="middle">128비트 (Q96.32)</text>
  <text x="575" y="158" fill="currentColor">→ &gt;&gt;32 → ns</text>
  <text x="40" y="196" fill="currentColor" font-family="monospace">3 ticks: 3 × 0x29_AAAAAAAB = 0x7D_00000001 → &gt;&gt;32 → 0x7D = 125 ns (정확)</text>
  <text x="40" y="220" fill="currentColor" font-family="monospace">1 day : 2073600000000 ticks → 86400000000160 ns (정확값보다 +160 ns)</text>
  <text x="40" y="246" fill="currentColor">정수 × Q32.32 도, Q32.32 × Q32.32 도 "곱한 뒤 &gt;&gt;32" — 같은 q32_mul 하나로 된다.</text>
</svg>
```

**같은 함수 하나로 두 해석.** Q32.32 값 a = A·2³², b = B·2³² 이면 a·b = AB·2⁶⁴ 이고, Q32.32 로 돌려놓으려면 `>> 32`. tick 수 t (정수) 와 mult = M·2³² 이면 t·mult = tM·2³², 정수 ns 를 얻으려면 역시 `>> 32`. 그래서 `ticks_to_ns(t, m)` = `q32_mul(t, m)`.

`q32_mul` 첫 호출 (1.5 × 2.0) 을 lldb 로 한 명령씩 따라간 실제 기록 (`demo_q32.lldb`, `lldb -b -s demo_q32.lldb /tmp/ARM-B07_dbg`, 주소 줄 일부 생략):

```text
(lldb) b q32_mul
(lldb) run
->  0x100002534 <+0>:  mul    x8, x0, x1
    0x100002538 <+4>:  umulh  x9, x0, x1
    0x10000253c <+8>:  extr   x0, x9, x8, #0x20
    0x100002540 <+12>: ret
(lldb) register read x0 x1
      x0 = 0x0000000180000000
      x1 = 0x0000000200000000
(lldb) si
(lldb) register read x8
      x8 = 0x0000000000000000
(lldb) si
(lldb) register read x9
      x9 = 0x0000000000000003
(lldb) si
(lldb) register read x0
      x0 = 0x0000000300000000
```

| # | 명령 | x8 (lo) | x9 (hi) | x0 |
|---|---|---|---|---|
| 0 | 진입 | | | `0x1_80000000` (1.5) |
| 1 | `mul` | `0x0` | | |
| 2 | `umulh` | | `0x3` | |
| 3 | `extr x0, x9, x8, #32` | | | `0x3_00000000` (3.0) |

128비트 곱은 `0x3_0000000000000000` (= 3.0 × 2⁶⁴, hi = 3, lo = 0) 이고, 그 가운데 64비트 `0x3_00000000` (= 3.0 × 2³²) 이 Q32.32 의 3.0 이다. `EXTR Xd, Xn, Xm, #lsb` 는 `(Xn:Xm) >> lsb` 의 하위 64비트 — 레지스터 쌍을 가로지르는 시프트를 명령 하나로 한다.

**오차 한계.** 진짜 비율 r = 1e9/24e6 = 41.666… ns. `calc_mult` 는 r·2³² = 178956970666.67 을 반올림해 178956970667 로 만든다 → mult 의 오차 |e| ≤ 1/2 (단위 2⁻³² ns). 그러면

`|ticks_to_ns(t) − t·r| ≤ t·|e|/2³² + 1 ≤ t/2³³ + 1` ns (+1 은 버림 때문)

하네스는 0~하루치 tick 범위의 랜덤 20만 개로 이 부등식을 확인하고, 하루치 (2,073,600,000,000 tick) 의 실제 드리프트 **+160 ns** 를 출력한다 (e = +1/3 이므로 2.07e12/(3·2³²) ≈ 160.9 → 160). 하루에 160 ns, 즉 1.9 ppb — 24 MHz 크리스털 자체의 오차 (보통 ±10~50 ppm) 보다 4자리 작다.

**이 Mac 의 진짜 타임베이스** (`demo_timebase.c` 를 `solution.S` 와 링크, 실제 출력):

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 demo_timebase.c solution.S -o /tmp/ARM-B07_tb && /tmp/ARM-B07_tb
cntfrq_el0 = 1000000000 Hz, mach timebase = 125/3 ns/tick (= 24000000 Hz)
mult (Q32.32 ns/tick) = 178956970667 = 0x29aaaaaaab
ticks now = 13340106802623 (uptime 154.40 h)
q32 ns = 555837783443660, exact ns = 555837783442625, diff = 1035 ns
CLOCK_UPTIME_RAW - exact = 166 ns (호출 사이 경과 시간 포함)
```

- `mach_absolute_time()` 의 tick 은 `125/3` ns = 24 MHz 다. 154 시간 동안 쌓인 tick 에서 Q32.32 근사의 오차는 1035 ns — 위 식 t/(3·2³²) = 13340106802623 / 12884901888 ≈ 1035 와 맞는다.
- 흥미로운 점: 이 M6 의 `CNTFRQ_EL0` 은 **1 GHz** 다. Armv8.6 부터 시스템 카운터 주파수가 1 GHz 로 고정되었다고 알고 있다 (Arm ARM 의 Generic Timer 장, 확인 권장). macOS 는 호환성 때문에 `mach_absolute_time` 단위를 24 MHz 로 유지하는 것으로 보인다 (커널 구현은 확인하지 않았다). 카운터를 직접 읽는 벤치마크는 ARM-M03 몫.
- 같은 아이디어가 Linux `clocksource` 의 `mult`/`shift` (`ns = (cycles * mult) >> shift`) 다. 커널은 보통 32비트 mult 와 64비트 곱을 쓰고 오버플로를 피하려고 주기적으로 기준점을 옮긴다 (세부 구현은 커널 버전마다 다름). 64×64→128 의 `umulh` 가 있으면 범위 걱정이 훨씬 줄어든다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S -o /tmp/ARM-B07_solution && /tmp/ARM-B07_solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined harness.c solution.S -o /tmp/ARM-B07_san && /tmp/ARM-B07_san
```

실제 출력 끝부분 (전부 PASS, exit 0, 약 0.01 s; ASan/UBSan 빌드도 `50/50 passed`, 보고 없음):

```text
PASS  report(bad, "q32_mul vs (u128 곱) >> 32")
PASS  mult == 178956970667ull
PASS  calc_mult(1000000000u) == 1ull << 32
PASS  calc_mult(0) == 0
PASS  ticks_to_ns(F, mult) == 1000000000u
PASS  ticks_to_ns(3, mult) == 125u
PASS  report(bad, "하루치 tick 범위 오차 한계")
INFO  1 day = 2073600000000 ticks -> approx 86400000000160 ns, exact 86400000000000 ns, drift +160 ns
50/50 passed
```

`calc_mult(0) == 0` 은 일부러 넣은 체크다: 설정 실수로 주파수가 0 이어도 ARM 은 죽지 않고 mult = 0 → 모든 시간이 0 ns 가 된다. "크래시 대신 조용히 틀린 값" 의 전형 — 펌웨어라면 `freq == 0` 을 먼저 검사해야 한다 (추가 연습 3).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `madd x0, x2, x0, x1` 로 `c + a*b` | 더하는 값은 **마지막** operand (Ra). 이건 `x1 + x2*x0` | `madd x0, x0, x1, x2` |
| signed 128비트 상위에 `umulh` | 음수 입력에서 상위가 틀림 (`mulhi_s64_bug(-1,1) = 0`) | `smulh` |
| C 에서 `uint64_t r = a32 * b32;` | 32비트 곱 → `mul w0` (실측 clang 출력). 2³² 이상 사라짐 | `(uint64_t)a32 * b32` → `umull` |
| signed 나머지에 `udiv` | 음수를 2⁶⁴ 근처 큰 수로 봄 (`mod_s64_bug(-7,2) = 1`) | `sdiv` + `msub` |
| "0 나누기면 예외가 나서 알 수 있겠지" | ARM 은 조용히 0. 결과가 정상처럼 흘러감 | 나누기 전에 `cbz x1, err` (연습 3) |
| `udiv x0, x0, #10` / `mul x0, x0, #3` / `umulh w0, w0, w1` / `umull x0, x0, x1` | 즉시값 형식 없음, `umulh` 은 64비트만, `umull` 입력은 w | 레지스터에 상수 로드 (`mov x8, #10`) / 올바른 폭 |
| 하네스에서 `0u - 15u` 를 64비트 기대값으로 | `0u` 는 32비트 `unsigned int` → `0xFFFFFFF1`. 이 레슨 작성 중 **실제로 낸 버그** (solution 이 49/50 으로 FAIL) | `UINT64_MAX - 14u` 또는 `UINT64_C(0) - 15` |
| 랜덤 테스트만 믿고 carry 경로 생략 | `q32_mul_round` 에서 `adc` 를 빼도 랜덤 20만 개는 **전부 통과** (carry 확률 2⁻³³). 이름 붙인 경계값 1개만 FAIL | carry 를 일으키는 입력을 손으로 넣는다 (연습 5) |

어셈블러 에러는 실제로 이렇게 나온다 (`cc -c bad.S`):

```text
bad.S:1:11: error: invalid operand for instruction
    umulh w0, w0, w1
          ^
bad.S:2:18: error: invalid operand for instruction
    udiv x0, x0, #10
                 ^
bad.S:4:15: error: invalid operand for instruction
    umull x0, x0, x1
              ^
```

`adc` 를 지운 `q32_mul_round` 로 practice 를 돌린 실제 결과 (랜덤 대조는 PASS, 경계값 하나만 FAIL):

```text
FAIL  practice.c:91  q32_mul_round(UINT64_MAX, 0x80000000ull) == 0x8000000000000000ull
25/26 passed
```

## 8. 면접 Q&A

<details>
<summary>Q1. ARM 은 왜 정수 0 나누기에 트랩이 없나요? 그럼 펌웨어는 어떻게 하나요?</summary>

Arm ARM 이 `SDIV/UDIV` 의 0 나누기 결과를 0 으로 **정의**했기 때문이다 (AArch64 에는 정수 나눗셈 예외 자체가 없다). 예외 경로가 없으면 파이프라인·예외 모델이 단순해지고, 실제 소프트웨어(C)는 어차피 나누기 전에 검사해야 한다는 판단으로 이해하고 있다 (설계 의도는 추정). 대가: 버그가 조용히 0 을 만들어 흘러간다. 대책은 ① 나누기 전 `cbz`/assert ② 개발 빌드에서 UBSan (`-fsanitize=integer-divide-by-zero`) ③ Cortex-M (ARMv7-M) 이라면 `SCB->CCR.DIV_0_TRP = 1` 로 0 나누기를 UsageFault 로 바꿀 수 있다 (UFSR.DIVBYZERO — 결함 분석은 ARM-M12). SSD 컨트롤러에 흔한 Cortex-R (ARMv7-R) 은 `SCTLR.DZ` 비트로 0 나누기를 Undefined Instruction 예외로 만들 수 있다 (ARMv7-R ARM 기준, 확인 권장). 즉 "트랩을 켤 수 있는" 32비트 프로파일과 달리 AArch64 는 그런 스위치가 없다. 참고로 RISC-V 도 트랩이 없고 `x/0 = -1(전부 1)`, `x%0 = x` 로 정의한다 (ARM 과 결과가 다르다 — 확인 권장).

</details>

<details>
<summary>Q2. `INT_MIN / -1` 은 각각 어떻게 되나요? `INT_MIN % -1` 은요?</summary>

C: 둘 다 UB (C11 6.5.5p6 — 몫이 표현 불가하면 `/` 와 `%` 모두 UB). AArch64: `sdiv` 는 잘라서 `INT_MIN`, `sdiv+msub` 나머지는 0 (이 레슨 실측). x86-64: `idiv` 가 #DE → `SIGFPE` (나머지도 `idiv` 한 명령으로 구하므로 `%` 도 죽는다 — Rosetta 2 에서 실측). 꼬리: "그럼 `abs(INT_MIN)` 은?" → 역시 C 에선 UB, ISA 의 `neg` 는 `INT_MIN` 으로 wrap (ARM-B05).

</details>

<details>
<summary>Q3. 컴파일러는 `x / 10` 을 나눗셈 없이 어떻게 하나요? 왜 그게 맞나요?</summary>

`M = ⌈2⁶⁷/10⌉ = 0xCCCCCCCCCCCCCCCD` 를 곱해 상위 64비트를 취하고 (`umulh`) 3 더 민다: `q = (x·M) >> 67`. M = 2⁶⁷/10 + ε (ε = 2/10) 이고, 오차 항 x·ε/2⁶⁷ < 2⁶⁴·0.2/2⁶⁷ = 0.025 가 "x/10 의 소수부 + 오차 < 1" 을 깨지 않을 만큼 작아서 64비트 전 범위에서 정확하다 (Granlund–Montgomery). 실제 clang 출력: `mov x8, #0xcccccccccccccccc` / `movk x8, #52429` / `umulh x8, x0, x8` / `lsr x0, x8, #3`. 이유: `udiv` 는 곱셈보다 몇 배 느리다 (코어마다 다름, 여기서는 측정하지 않음). 추가 연습 2 에서 0..2,000,000 전수 + 랜덤 + 2^k 경계로 검증했다. signed 상수 나눗셈은 음수 보정이 붙는다: `x / 8` → `add x8, x0, #7` / `cmp x0, #0` / `csel x8, x8, x0, lt` / `asr x0, x8, #3` (실제 clang 출력, `asr` 만 쓰면 −1/8 = −1 이 되어 틀림).

</details>

<details>
<summary>Q4. 64비트 곱셈 오버플로는 어떻게 검출하나요?</summary>

unsigned: 128비트 곱의 상위(`umulh`) 가 0 이 아니면 넘침. signed: `smulh` 결과가 하위의 부호 확장 (`lo asr #63`) 과 다르면 넘침 → `cmp x9, x8, asr #63` / `cset w0, ne` (추가 연습 4). C 에서는 `__builtin_mul_overflow` — clang 이 실제로 `umulh` / `cmp xzr, x8` / `cset w8, ne` 로 내리는 것을 확인했다. 나눗셈으로 검사하는 `a != 0 && r / a != b` 방식은 느리고 signed 에서는 `INT_MIN/-1` 함정이 있다.

</details>

<details>
<summary>Q5. Cortex-M0 에는 나눗셈 명령이 없다는데, `a / b` 를 쓰면 어떻게 되나요? M3/M4 는요?</summary>

ARMv6-M (M0/M0+) 은 `SDIV/UDIV` 가 없고 32×32→64 `UMULL` 도 없다 (`MULS` 32×32→32 만). 컴파일러가 `__aeabi_uidiv` / `__aeabi_idiv` (64비트는 `__aeabi_uldivmod`) 런타임 함수를 호출한다 — 수십~수백 사이클, 코드 크기도 증가. 0 나누기 시 이 함수들은 `__aeabi_idiv0` 을 부르도록 되어 있어 거기서 정책을 정할 수 있다 (RTABI 기준, 확인 권장). ARMv7-M (M3/M4/M7) 은 `SDIV/UDIV` 가 있고 (M3/M4 TRM 기준 2~12 사이클, 값 의존 — 확인 권장), `SMULL/UMULL/SMLAL/UMLAL` 도 있다. ISR 안에서 나눗셈을 피하고 시프트/역수 곱으로 바꾸는 습관은 여기서 나온다.

</details>

<details>
<summary>Q6. MCU 에서 고정소수점은 왜 쓰고, Q 포맷은 어떻게 고르나요?</summary>

FPU 가 없거나 (M0/M3), ISR 에서 FPU 레지스터 lazy stacking 비용을 피하거나, 결정적인 비트 단위 결과가 필요할 때. Q 포맷은 "필요한 정수 범위 + 필요한 해상도" 로 정한다: 오디오/제어는 Q15/Q31 (`SMULL` 후 `>>31`, 포화는 ARM-A07 의 `SSAT`/`SQDMULH`), 시간 변환은 이 레슨의 Q32.32 처럼 넓은 범위가 필요하다. 곱하면 소수 비트 수가 더해지므로 (Q32.32 × Q32.32 = Q64.64) 다시 시프트해야 하고, 시프트 전 128비트 중간값이 필요해서 `umulh` 가 핵심이다. 반올림은 시프트 전에 2^(n−1) 을 더한다 (연습 5, `adds`/`adc`).

</details>

<details>
<summary>Q7. 32비트 ARM 리눅스 커널에서 `u64 / u32` 를 그냥 쓰면 안 된다는 이유는?</summary>

A32 에는 64비트 나눗셈 명령이 없어서 gcc 가 `__aeabi_uldivmod` 같은 libgcc 함수를 부르는데, 커널은 libgcc 를 링크하지 않는다 → 링크 에러. 그래서 `do_div(n, base)` (n 을 몫으로 바꾸고 나머지 반환) / `div_u64()` 매크로를 쓴다. AArch64 는 `udiv x` 가 있으므로 이 제약이 없다. 상수 나눗셈은 양쪽 다 컴파일러가 곱셈으로 바꾼다 (Q3).

</details>

## 9. 추가 연습

### 연습 1 — SMULH 없이 signed 상위 64비트

`int64_t smulh_via_umulh(int64_t a, int64_t b)` — `umulh` 와 보정만으로.

<details>
<summary>답 보기</summary>

5절의 식 `hi_s = umulh(a,b) − (a<0 ? b : 0) − (b<0 ? a : 0)`. 조건부 선택은 분기 없이 `and x9, x1, x0, asr #63` — `x0 asr #63` 은 a<0 이면 전부 1, 아니면 0 이라 AND 로 b 또는 0 이 된다. 명령 5개. 쓰임: signed 곱 상위가 없는 ISA 나 SIMD 에서 같은 보정을 쓴다.

</details>

### 연습 2 — magic number 로 x/10

`uint64_t udiv10(uint64_t x)` — `udiv` 금지.

<details>
<summary>답 보기</summary>

`mov x8, #0xCCCCCCCCCCCCCCCC` (반복 패턴이라 논리 즉시값 하나로 인코딩됨, ARM-B03) → `movk x8, #0xCCCD` → `umulh x8, x0, x8` → `lsr x0, x8, #3`. clang 출력과 같다. 테스트는 0..2,000,000 전수, 크기 고른 랜덤 20만, 2^k−1, 2^k, 2^k+1 (k=0..63), `UINT64_MAX`.

</details>

### 연습 3 — 안전한 signed 나눗셈

`int safe_sdiv(int64_t a, int64_t b, int64_t *q, int64_t *r)` — 0 = OK, −1 = 0 나누기, −2 = `INT64_MIN / -1`. 에러면 `*q`, `*r` 를 건드리지 않는다.

<details>
<summary>답 보기</summary>

`cbz x1, 1f` (0 검사) → `cmn x1, #1` / `b.ne 2f` (b == −1 인가: `cmn` 은 `x1 + 1` 로 플래그 설정) → `mov x8, #0x8000000000000000` / `cmp x0, x8` / `b.eq 3f` → 정상 경로 `sdiv` + `msub` + 두 `str`. 다른 방법: `cmp x0, #1` 은 x0 == `INT64_MIN` 일 때만 V 플래그가 선다 (`INT64_MIN − 1` 이 signed overflow) → `b.vs` 로 상수 로드 없이 검사할 수 있다 (플래그 계산은 ARCH-B02).

</details>

### 연습 4 — 곱셈 오버플로 검출

`int mul_overflow_u64(uint64_t a, uint64_t b, uint64_t *res)` / `int mul_overflow_s64(int64_t, int64_t, int64_t *)` — `__builtin_mul_overflow` 와 같은 계약 (잘린 결과는 항상 저장, 넘치면 1).

<details>
<summary>답 보기</summary>

unsigned: `mul` + `umulh` → `cmp x9, #0` / `cset w0, ne`. signed: `mul` + `smulh` → `cmp x9, x8, asr #63` / `cset w0, ne` — 128비트 값이 64비트에 들어가려면 상위가 하위 부호 비트의 복사본이어야 한다. `INT64_MIN × −1` → 넘침 1, 결과 `INT64_MIN`; `INT64_MIN × 1` → 0. 테스트는 `__builtin_mul_overflow` 와 20만 쌍 대조.

</details>

### 연습 5 — 반올림 Q32.32 곱 (128비트 덧셈)

`uint64_t q32_mul_round(uint64_t a, uint64_t b) = (a*b + 2³¹) >> 32`.

<details>
<summary>답 보기</summary>

`mov x10, #0x80000000` (add 즉시값은 12비트(+lsl 12) 라 2³¹ 은 못 넣는다) → `adds x8, x8, x10` (lo 에 더하고 올림수를 C 플래그로) → `adc x9, x9, xzr` (hi 에 C 더하기) → `extr x0, x9, x8, #32`. 경계값 `(UINT64_MAX, 0x80000000)`: lo = `0xFFFFFFFF80000000` + 2³¹ 이 정확히 넘쳐서 hi 로 carry → 결과 `0x8000000000000000`. `adc` 를 빼면 `0x7FFFFFFF00000000` — 7절의 실측처럼 랜덤 테스트로는 못 잡는다.

</details>

### 연습 6 — UMADDL 로 전력 누적 (V × I 합)

`uint64_t dot_u32(const uint32_t *a, const uint32_t *b, size_t n)` — 전압·전류 ADC 샘플 곱의 64비트 합.

<details>
<summary>답 보기</summary>

루프 본체 `ldr w9, [x0], #4` / `ldr w10, [x1], #4` / `umaddl x8, w9, w10, x8` / `subs x2, x2, #1` / `b.ne 1b`, 진입 시 `cbz x2` 로 n = 0 처리. `umaddl` 은 32×32→64 곱과 64비트 누적을 한 명령에 — 32비트 `madd w` 를 쓰면 `(2³²−1)² × 2` 가 2 가 되어 버린다 (테스트의 `0xFFFFFFFC00000002` 체크). SIMD 판 내적은 ARM-M06.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-B07_practice && /tmp/ARM-B07_practice   # 26/26 passed
```

ASan/UBSan 빌드 (`-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined`) 도 `26/26 passed`, Lima VM 의 gcc 15.2 빌드도 통과했다.

## 10. 핵심 정리

- 128비트 곱 = `mul` (하위, signed/unsigned 공통) + `umulh`/`smulh` (상위). `umulh/smulh` 는 64비트 형식만 있다.
- `madd xd, xn, xm, xa` = `xa + xn·xm` — **더하는 값이 마지막**. `mul`/`mneg`/`umull`/`smull` 은 Ra = `xzr` 인 별칭.
- C 의 `u32 * u32` 는 32비트 곱 (`mul w`). 64비트 결과가 필요하면 `(uint64_t)a * b` → `umull`.
- 나머지 명령은 없다: `udiv/sdiv` + `msub` (`r = a − q·b`). 0 쪽 버림, 나머지 부호 = 피제수 (C99 와 동일).
- ARM: `x/0 = 0`, `INT64_MIN/−1 = INT64_MIN`, 트랩 없음. x86: 둘 다 #DE → `SIGFPE`. C: 둘 다 UB → 테스트 기대값은 손으로 쓴다.
- 상수 나눗셈은 컴파일러가 `umulh` + 시프트 (magic number) 로 바꾼다: `/10` = `umulh(x, 0xCCCC…CD) >> 3`.
- Q32.32 곱 = `mul` + `umulh` + `extr #32`. tick→ns 변환 오차 ≤ t/2³³ + 1 ns (24 MHz, 하루 +160 ns 실측).

## 참고자료

- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — C6 장: `MADD`, `MSUB`, `SMULH`, `UMULH`, `SMADDL`, `UMADDL`, `SDIV`, `UDIV`, `EXTR` 의사코드, C4 장 "Data-processing (3 source)" 인코딩
- [Arm A64 Instruction Set Architecture — SDIV](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/SDIV--Signed-Divide-) (0 나누기 결과 정의)
- [Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst) — `__int128` 반환 x0:x1
- [Apple — Writing ARM64 code for Apple platforms](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms)
- [ARMv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — SDIV/UDIV, CCR.DIV_0_TRP, UFSR.DIVBYZERO
- [Run-time ABI for the Arm Architecture (RTABI)](https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst) — `__aeabi_idiv`, `__aeabi_uldivmod`, `__aeabi_idiv0`
- Intel SDM Vol. 2 — `IDIV`: #DE on divide by zero and quotient overflow
- T. Granlund, P. Montgomery, *Division by Invariant Integers using Multiplication*, PLDI 1994
- C11 (N1570) 6.5.5 Multiplicative operators — p5 (0 나누기 UB), p6 (몫이 표현 불가하면 `/`·`%` 모두 UB)
- Henry S. Warren, *Hacker's Delight* 2판, 8~10장 (곱셈 상위, 상수 나눗셈)

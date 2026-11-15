---
id: ARCH-B01
time: 60분
updated: 2026-11-11
---
# ARCH-B01 수 체계: 2진·16진·2의 보수 변환기

## 학습 목표

- n비트(1~64) 패턴 하나를 **무부호 보기(비트 패턴)** 와 **부호 보기(값)** 로 2/10/16진 문자열로 바꾸고, n = 64 에서도 `1ull << 64` 같은 UB 없이 동작하게 만든다.
- `sign_extend(value, bits)` 를 `(x ^ m) - m` 한 줄로 구현하고, 그것이 Cortex-M4 / Apple M6 에서 `sbfx`·`sxtb` **명령 하나** 가 된다는 것을 실제 asm 으로 확인한다.
- n비트 경계값(최소 −2ⁿ⁻¹, 최대 2ⁿ⁻¹−1, −1 = 모두 1)을 n = 1..64 표로 검증하고, "−(−128) = −128", "n = 1 이면 범위가 −1..0" 같은 면접 함정을 손으로 계산한다.
- 센서 레지스터의 12비트 2의 보수 값, 레지스터 안의 부호 있는 트림 필드, AArch64 `B`/`BL` 의 imm26 오프셋, 디버그 셸 숫자 입력 같은 **펌웨어 현장의 부호 확장** 을 코드로 처리한다.

## 1. 문제

"0xB4 를 int8 로 읽으면?", "12비트 ADC 값 0xE70 은 몇 도?", "이 브랜치의 imm26 은 몇 바이트 점프?" — 펌웨어·실리콘 검증 면접의 첫 10분은 거의 항상 **비트 패턴과 값 사이를 오가는 손계산** 이다. 레지스터 덤프, 로직 분석기 캡처, 디스어셈블리를 읽는 일 자체가 이 변환이라서, 여기서 실수하면 뒤의 디버깅 이야기가 전부 흔들린다.

핵심은 **같은 n비트 패턴이 두 가지로 읽힌다** 는 것이다. 무부호(unsigned)로 읽으면 0..2ⁿ−1, 2의 보수(two's complement)로 읽으면 MSB 의 무게가 +2ⁿ⁻¹ 대신 **−2ⁿ⁻¹** 이 되어 −2ⁿ⁻¹..2ⁿ⁻¹−1 이다. 하드웨어(가산기)는 둘을 구별하지 않는다 — 구별은 우리가 읽을 때 한다.

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">4비트 패턴 16개 — 같은 패턴, 두 가지 읽기</text>
  <text x="10" y="56" fill="currentColor">무부호</text>
  <text x="10" y="92" fill="currentColor">패턴</text>
  <text x="10" y="128" fill="currentColor">2의 보수</text>
  <g stroke="currentColor" fill="none">
    <rect x="100" y="72" width="640" height="30"/>
  </g>
  <rect x="380" y="72" width="40" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="420" y="72" width="40" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="700" y="72" width="40" height="30" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="120" y="92">0000</text><text x="160" y="92">0001</text><text x="200" y="92">0010</text><text x="240" y="92">0011</text>
    <text x="280" y="92">0100</text><text x="320" y="92">0101</text><text x="360" y="92">0110</text><text x="400" y="92">0111</text>
    <text x="440" y="92">1000</text><text x="480" y="92">1001</text><text x="520" y="92">1010</text><text x="560" y="92">1011</text>
    <text x="600" y="92">1100</text><text x="640" y="92">1101</text><text x="680" y="92">1110</text><text x="720" y="92">1111</text>
    <text x="120" y="56">0</text><text x="160" y="56">1</text><text x="200" y="56">2</text><text x="240" y="56">3</text>
    <text x="280" y="56">4</text><text x="320" y="56">5</text><text x="360" y="56">6</text><text x="400" y="56">7</text>
    <text x="440" y="56">8</text><text x="480" y="56">9</text><text x="520" y="56">10</text><text x="560" y="56">11</text>
    <text x="600" y="56">12</text><text x="640" y="56">13</text><text x="680" y="56">14</text><text x="720" y="56">15</text>
    <text x="120" y="128">0</text><text x="160" y="128">1</text><text x="200" y="128">2</text><text x="240" y="128">3</text>
    <text x="280" y="128">4</text><text x="320" y="128">5</text><text x="360" y="128">6</text><text x="400" y="128">7</text>
    <text x="440" y="128">-8</text><text x="480" y="128">-7</text><text x="520" y="128">-6</text><text x="560" y="128">-5</text>
    <text x="600" y="128">-4</text><text x="640" y="128">-3</text><text x="680" y="128">-2</text><text x="720" y="128">-1</text>
  </g>
  <line x1="420" y1="36" x2="420" y2="140" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="300" y="158" fill="currentColor" font-size="12">부호 경계: 7 → -8 (smax → smin)</text>
  <text x="600" y="158" fill="currentColor" font-size="12">무부호 경계: 15 → 0</text>
  <text x="10" y="190" fill="currentColor">2의 보수 값 = −b₃·2³ + b₂·2² + b₁·2¹ + b₀  (MSB 만 음의 무게) = 무부호 값 − 16·b₃</text>
  <text x="10" y="212" fill="currentColor">색칠한 칸: smax = 0111 · smin = 1000 · −1 = 1111 — n 이 몇이든 같은 모양</text>
  <text x="10" y="234" fill="currentColor">범위가 비대칭: −8..7. 그래서 −(−8) 은 4비트에 없다 → 다시 −8 (neg_n, Part B)</text>
</svg>
```

모든 함수는 `uint64_t` 위에서 비트를 다루고, 값으로 돌려줄 때만 `int64_t` 로 바꾼다. 패턴→값 변환 `u64_as_i64()` 는 주어진다 (왜 필요한지는 풀이 Part A).

### Part A — n비트 기본기 (n = 1..64)

- `mask_n(n)` — 하위 n비트가 1인 마스크. **n = 64 에서 `1ull << 64` 금지** (C11 6.5.7p3, UB).
- `trunc_n(v, n)` — n비트 레지스터에 쓰면 남는 값.
- `sign_extend(v, bits)` — v 의 하위 bits 비트를 2의 보수로 읽은 값. 위쪽 비트는 무시.
- `to_str(v, n, base, is_signed, buf, cap)` — base 2/10/16.
  - 무부호 보기 = **비트 패턴**: 2진은 `0b`+n자리, 16진은 `0x`+⌈n/4⌉자리(대문자), 10진은 패딩 없음.
  - 부호 보기 = **값**: `-` + 크기, 2/16진도 최소 자리 (`-0x80`, `0x7F`, `0x0`).
  - 잘못된 n/base, 버퍼 부족 → −1. 검증: 고정 시드 xorshift64 로 2만 개를 `printf("%llu/%lld/%0*llX")` 와 비교.

### Part B — 경계값과 손계산

- `umax_n / smin_n / smax_n`, `fits_s(x, n)`, `fits_u(x, n)`, `neg_n(v, n)` (n비트 안의 −x), `resize(v, from, to, is_signed)` (폭 바꾸기: 넓히면 부호/0 확장, 좁히면 자름).
- n = 1..64 전부에 대해 최소/최대/−1 표를 검사하고, 대표 n 의 표를 출력한다.
- **손계산 10문제** — 먼저 손으로 풀고, 답은 풀이 Part B 의 접힌 칸에서 확인한다 (체크 테스트 H1–H10 이 같은 문제를 코드로 검산).

| # | 문제 |
|---|---|
| H1 | 8비트 `0xB4` 를 int8 로 읽으면? |
| H2 | 12비트 레지스터에 −1 을 쓰면 16진으로? |
| H3 | 6비트 필드 `0x3A` 를 부호 있는 값으로? |
| H4 | −37 을 8비트 2진으로? |
| H5 | `int8_t` 변수에 200 을 넣으면 (모듈러 변환 가정) 값과 패턴은? |
| H6 | 16비트 부호 있는 `0x7FFF + 1` 은? |
| H7 | 10비트 2의 보수 ADC 값 `0x2F0` 은? |
| H8 | 32비트 `0xFFFFFFFF` 를 64비트로 0 확장 / 부호 확장하면? |
| H9 | 1비트 부호 있는 필드에 1 이 들어 있으면 값은? 최대값은? |
| H10 | 8비트에서 −(−128) 은? |

### Part C — 펌웨어 현장

1. **온도 센서 레지스터** — 16비트 레지스터의 [15:4] 에 12비트 2의 보수 온도가 왼쪽 정렬, 1 LSB = 0.0625 °C (TI TMP102 의 12비트 모드가 이런 형식). `temp_raw_to_udeg(reg)` → µ°C (정수, 소수 없이 정확).
2. **부호 있는 트림 필드** — 가상의 CAL 레지스터 [12:8] 에 5비트 부호 트림(−16..15). `trim_get`, `trim_set(reg, val, &ok)` — 범위 밖이면 레지스터를 건드리지 않고 `ok = false`. (필드 RMW 자체는 C-B04 참고.)
3. **AArch64 `B`/`BL`** — [31] = link, [30:26] = `00101`, [25:0] = imm26, 바이트 오프셋 = sext(imm26) × 4 (명령 자신의 주소 기준). `a64_b_decode`, `a64_b_encode` (정렬·±128 MiB 범위 검사). 기대값은 `demo_branch.s` 를 **이 맥에서 실제로 어셈블한 결과**.
4. **디버그 셸 숫자 파서** — `poke 0x4001_0000 -1` 같은 명령의 숫자 부분. `parse_num(s, n, &out)`: `[-](0x|0b)?digits`, `-` 없으면 무부호 n비트, 있으면 부호 n비트에 들어가야 한다 (어셈블러의 `.byte 255` / `.byte -128` 둘 다 OK 와 같은 규칙). 64비트 넘침 검출. `to_str` → `parse_num` 왕복 5000 × 6 조합.

## 2. Problem (English)

> **Arbitrary-width integer converter.** Implement in C11 (no compiler builtins, no undefined behavior for any `n` in `1..64`):
>
> 1. `uint64_t mask_n(unsigned n)`, `uint64_t trunc_n(uint64_t v, unsigned n)`, `int64_t sign_extend(uint64_t v, unsigned bits)`.
> 2. `int to_str(uint64_t v, unsigned n, unsigned base, bool is_signed, char *buf, size_t cap)` for base 2, 10, 16. Unsigned view prints the raw n-bit pattern (binary: exactly n digits, hex: ceil(n/4) uppercase digits, with `0b`/`0x` prefixes); signed view prints the two's-complement value as sign + magnitude. Return the length, or -1 on bad arguments or a too-small buffer.
> 3. `umax_n`, `smin_n`, `smax_n`, `fits_s`, `fits_u`, `neg_n`, `resize(v, from, to, is_signed)`.
> 4. Firmware: decode a left-justified 12-bit two's-complement temperature (0.0625 °C/LSB) to micro-degrees; get/set a 5-bit signed field at bits [12:8] with range checking; decode/encode AArch64 `B`/`BL` (imm26, offset = sext(imm26) * 4); parse `[-](0x|0b)?digits` into an n-bit pattern with overflow detection.
>
> Examples: `sign_extend(0x80, 8) == -128`, `to_str(0x80, 8, 16, true, ...) -> "-0x80"`, `to_str(5, 4, 2, false, ...) -> "0b0101"`, `mask_n(64) == 0xFFFFFFFFFFFFFFFF`, `a64_b_decode(0x17FFFFFF) -> offset -4`, `parse_num("-129", 8, &v) == false`.
>
> Follow-ups: Why is `(int32_t)(x << 20) >> 20` not portable C? What does `-INT64_MIN` do? How far can a single AArch64 `B` reach?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
# 처음엔 FAIL 줄이 쭉 나오고 마지막 줄이 "17/89 passed", exit 1 — TODO 를 채워 89/89 를 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — mask_n 은 n = 64 만 따로</summary>

`(1ull << n) - 1` 은 n = 1..63 에서만 정의된다. n ≥ 64 이면 `UINT64_MAX` 를 바로 돌려준다. 분기 없는 대안: `UINT64_MAX >> (64 - n)` (이건 n = 1..64 에서 안전하고 n = 0 이 UB).

</details>

<details>
<summary>힌트 2 — 부호 확장은 "부호 비트를 뒤집고 빼기"</summary>

x = 하위 bits 비트, m = `1 << (bits-1)` (부호 비트 자리). `(x ^ m) - m` 을 **uint64 로** 계산하면 64비트 2의 보수 패턴이 나온다. 부호 비트가 0 이면 `x + m - m = x`, 1 이면 `x - m - m = x - 2m = x - 2^bits`. 마지막에 `u64_as_i64()`.

</details>

<details>
<summary>힌트 3 — to_str 의 부호 보기는 "크기" 를 uint64 로</summary>

값 s 가 음수면 크기 = `0 - (uint64_t)s`. `-s` 를 int64 로 하면 s = INT64_MIN 에서 오버플로(UB). 자리는 `mag % base` 로 낮은 자리부터 뽑으니 역순으로 모았다가 뒤집어 쓴다. 최대 길이 `-0b` + 64 = 67 + NUL.

</details>

<details>
<summary>힌트 4 — 경계값은 mask_n 하나로</summary>

`umax = mask_n(n)`, `smax = mask_n(n) >> 1`, `smin = -smax - 1`. `1ll << 63` 같은 signed 시프트는 쓰지 않는다.

</details>

<details>
<summary>힌트 5 — parse_num 의 넘침 검사는 곱하기 전에</summary>

`acc * base + d` 가 64비트를 넘는지는 계산 **전에** `acc > (UINT64_MAX - d) / base` 로 본다. 음수는 크기가 `2^(n-1)` 이하인지 보고, 패턴 = `trunc_n(0 - acc, n)`.

</details>

## 5. 풀이

### Part A — 마스크, 부호 확장, 문자열

**mask_n.** C11 6.5.7p3: 시프트 양이 음수이거나 (승격된) 왼쪽 피연산자의 폭 이상이면 UB. 그런데 하드웨어는 UB 를 "에러" 로 내지 않고 **조용히 다른 값을** 낸다. `demo_ub_shift.c` 실측 (Apple M6, Apple clang 21.0.0):

```text
$ cc -std=c11 -O2 demo_ub_shift.c -o /tmp/esn_ARCH-B01/ub && /tmp/esn_ARCH-B01/ub
n=62  naive=0x3FFFFFFFFFFFFFFF  safe=0x3FFFFFFFFFFFFFFF  shr=0x3FFFFFFFFFFFFFFF
n=63  naive=0x7FFFFFFFFFFFFFFF  safe=0x7FFFFFFFFFFFFFFF  shr=0x7FFFFFFFFFFFFFFF
n=64  naive=0x0000000000000000  safe=0xFFFFFFFFFFFFFFFF  shr=0xFFFFFFFFFFFFFFFF
imm=-3  imm<<2=-12  imm*4=-12

$ cc -std=c11 -O2 -fsanitize=undefined demo_ub_shift.c -o /tmp/esn_ARCH-B01/ubsan && /tmp/esn_ARCH-B01/ubsan
demo_ub_shift.c:15:81: runtime error: shift exponent 64 is too large for 64-bit type 'unsigned long long'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior demo_ub_shift.c:15:81 
demo_ub_shift.c:18:82: runtime error: left shift of negative value -3
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior demo_ub_shift.c:18:82 
(이후 stdout 4줄은 위와 같음 — 계속 실행된다)
```

`naive_mask(64)` 가 **0** 이 된 이유는 asm 에 있다. clang 은 `(1 << n) - 1` 을 `~(-1 << n)` 으로 바꿨고, arm64 `lsl` 은 시프트 양을 **mod 64** 로 쓴다 → `-1 << 0 = -1` → `mvn` → 0. "모든 비트 켜기" 가 "모든 비트 끄기" 가 된다. `-Wall -Wextra` 는 n 이 변수라 아무 경고도 내지 않았다.

```text
_naive_mask:
	mov	x8, #-1                         ; =0xffffffffffffffff
	lsl	x8, x8, x0
	mvn	x0, x8
	ret
```

`imm << 2` 는 이 컴파일러에서 우연히 −12 를 냈지만 UBSan 은 UB 로 잡는다(C11 6.5.7p4: 음수 E1 의 왼쪽 시프트는 UB). 그래서 Part C 의 브랜치 오프셋은 `* 4` 로 쓴다.

**sign_extend — `(x ^ m) - m`.** 12비트 예 `0xE70`:

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">sign_extend(0xE70, 12) — 부호 비트(bit 11)를 뒤집고 2048 을 뺀다 (uint64 산술)</text>
  <g font-family="monospace" fill="currentColor" font-size="13">
    <text x="10" y="56">x         = 0x0000_0000_0000_0E70   (3696)</text>
    <text x="10" y="84">m         = 0x0000_0000_0000_0800   (bit 11)</text>
    <text x="10" y="112">x ^ m     = 0x0000_0000_0000_0670   (1648)</text>
    <text x="10" y="140">(x^m) - m = 0xFFFF_FFFF_FFFF_FE70   (mod 2^64)</text>
    <text x="10" y="168">u64_as_i64 → -400                 (= 3696 - 4096)</text>
  </g>
  <rect x="470" y="38" width="280" height="140" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="482" y="60" font-weight="bold">왜 되나</text>
    <text x="482" y="82">부호 비트 b = 0: (x + m) - m = x</text>
    <text x="482" y="104">부호 비트 b = 1: (x - m) - m = x - 2^12</text>
    <text x="482" y="126">→ 정확히 "MSB 무게를 −2^11 로" 읽은 값</text>
    <text x="482" y="148">빌림(borrow)이 위쪽 52비트를 1 로 채움</text>
    <text x="482" y="170">= 하드웨어 SBFX 가 하는 일</text>
  </g>
  <text x="10" y="210" fill="currentColor">폭을 넓힐 때(resize): 위쪽은 전부 부호 비트의 복사본 — 0x80 (8비트) → 0xFF80 (16비트) → 0xFFFF_FF80 (32비트)</text>
  <rect x="10" y="226" width="160" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="170" y="226" width="160" height="28" fill="none" stroke="currentColor"/>
  <text x="90" y="245" fill="currentColor" text-anchor="middle" font-family="monospace">1111 1111</text>
  <text x="250" y="245" fill="currentColor" text-anchor="middle" font-family="monospace">1000 0000</text>
  <text x="345" y="245" fill="currentColor" font-size="12">← 왼쪽 8비트는 오른쪽 바이트의 MSB(1)를 복사한 것</text>
</svg>
```

왜 흔한 시프트 트릭 `(int64_t)(x << (64-n)) >> (64-n)` 을 정답으로 쓰지 않나:

| 단계 | C11 에서의 지위 |
|---|---|
| `(int64_t)u`, u > INT64_MAX | **구현 정의** (6.3.1.3p3) — clang/gcc 는 모듈러로 문서화 |
| 음수 `>>` | **구현 정의** (6.5.7p5) — clang/gcc 는 산술 시프트 |
| signed 로 먼저 바꾸고 `<<` 하는 변형 | 음수거나 넘치면 **UB** (6.5.7p4) |

실무에서는 시프트 트릭이 어디서나 동작하지만, 면접에서 "이식 가능한가?" 라고 물으면 위 표를 말할 수 있어야 한다. 테스트에서는 이 트릭을 **오라클** 로만 쓴다. `u64_as_i64` 도 같은 이유로 `(int64_t)u` 대신 `-(int64_t)(~u) - 1` 을 쓴다 — 이게 느릴까? `demo_asm.c` 실측:

```text
$ cc -O2 -S -o - demo_asm.c | grep -vE '^\s*(\.|;|$)|cfi|^l'          # arm64 (Apple M6), 지시어 줄 제거
_sext12:                                ; @sext12
	sbfx	w0, w0, #0, #12
	ret
_sext8:                                 ; @sext8
	sxtb	w0, w0
	ret
  ... (_sext_var 10줄 생략: neg/lsr 로 마스크, lsl 로 m, and/eor/sub)
_as_i32:                                ; @as_i32
	ret

$ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o - | grep -vE '^\s*(\.|@|$)|cfi|^\.L'
sext12:                                 @ @sext12
	sbfx	r0, r0, #0, #12
	bx	lr
sext8:                                  @ @sext8
	sxtb	r0, r0
	bx	lr
sext_var:                               @ @sext_var
	rsb.w	r2, r1, #32
	mov.w	r3, #-1
	lsr.w	r2, r3, r2
	subs	r1, #1
	movs	r3, #1
	lsl.w	r1, r3, r1
	ands	r0, r2
	eors	r0, r1
	subs	r0, r0, r1
	bx	lr
as_i32:                                 @ @as_i32
	bx	lr
```

폭이 상수면 컴파일러가 `(x ^ m) - m` 패턴을 알아보고 **SBFX(signed bit-field extract) 한 개** 로 만든다. 이식 가능한 `as_i32` 는 `ret` 만 남는다 — 명령 0개. 폭이 변수인 `sext_var` 는 C 식 그대로다: `rsb` 로 32−bits, `lsr` 로 마스크, `lsl` 로 m, 마지막 `ands/eors/subs` 가 `((v & mask) ^ m) - m`. 이식성을 위해 성능을 포기할 필요가 없다.

**to_str — 두 보기를 분리한 이유.** 레지스터 덤프·로직 분석기는 패턴(`0xFF80`, 자리 고정)을 보여주고, 로그·디버거 watch 는 값(`-128`)을 보여준다. 둘을 섞은 형식(`0xFFFFFF80` 처럼 32비트로 부호 확장된 16진)이 실무 혼란의 주원인이라, 무부호 보기는 **정확히 n비트의 패턴**, 부호 보기는 **부호 + 크기** 로 못 박았다.

| 호출 | 결과 |
|---|---|
| `to_str(0x80, 8, 16, false)` | `0x80` |
| `to_str(0x80, 8, 16, true)` | `-0x80` |
| `to_str(0x80, 8, 10, true)` | `-128` |
| `to_str(5, 4, 2, false)` | `0b0101` (n 자리 고정) |
| `to_str(3, 5, 16, false)` | `0x03` (⌈5/4⌉ = 2자리) |
| `to_str(INT64_MIN 패턴, 64, 2, true)` | `-0b1000…0` (길이 67) |

크기 계산 `mag = 0 - (uint64_t)s` 는 s = INT64_MIN 에서도 정의된다(무부호 산술은 mod 2⁶⁴). `-s` 는 UB. 2진·16진 자리는 `% base` 로 뽑았지만 base 가 2의 거듭제곱이면 `& (base-1)`, `>> log2(base)` 와 같다 — 16진 한 자리 = 니블 하나.

### Part B — 경계값 표와 손계산

solution 이 n = 1..64 전부를 검사한 뒤 대표 행을 출력한다 (실제 출력):

```text
  n | umax                 | smin                 | smax                 | -1 (16진 패턴)
  1 | 1                    | -1                   | 0                    | 0x1
  4 | 15                   | -8                   | 7                    | 0xF
  8 | 255                  | -128                 | 127                  | 0xFF
 12 | 4095                 | -2048                | 2047                 | 0xFFF
 16 | 65535                | -32768               | 32767                | 0xFFFF
 32 | 4294967295           | -2147483648          | 2147483647           | 0xFFFFFFFF
 64 | 18446744073709551615 | -9223372036854775808 | 9223372036854775807  | 0xFFFFFFFFFFFFFFFF
```

- **n = 1**: 범위가 −1..0 이다. "1비트 부호 있는 비트필드에 1 을 넣으면 −1" — `struct { int f:1; }` 에서 `s.f = 1; if (s.f == 1)` 이 거짓이 되는 고전 버그. (plain `int` 비트필드가 signed 인지도 구현 정의, C11 6.7.2p5.)
- **n = 64**: 마스크·smin·smax 모두 시프트 없이 `mask_n` 에서 나온다 — `1ll << 63` 은 signed 오버플로라 UB.
- **비대칭**: smin 의 크기가 smax 보다 1 크다. `neg_n(smin) == smin` (H10), `abs(INT_MIN)` 은 UB.
- **fits_s(x, n)** 는 "n비트 필드/즉치값에 넣을 수 있나" 의 질문이다. 동치 표현: `sign_extend(x, n) == x` — 자르고 부호 확장해서 원래대로 돌아오면 들어간다. 어셈블러가 imm 범위를 검사하는 방법이 바로 이것 (ARCH-B08 의 imm6/imm9 검사).
- **resize** = 하드웨어의 폭 변환. 8→16 부호 확장 = Verilog `{{8{a[7]}}, a}`, ARM `SXTB/SXTH`, x86 `MOVSX`. 0 확장 = `UXTB/UXTH`, `MOVZX`. 좁히기는 그냥 자르기 — 값이 바뀌었는지는 `fits_s/fits_u` 로 따로 본다.

손계산 답 (먼저 §1 표를 손으로 풀어볼 것):

<details>
<summary>H1–H5 답</summary>

| # | 답 | 계산 |
|---|---|---|
| H1 | **−76** | 0xB4 = 180, MSB=1 → 180 − 256 = −76 |
| H2 | **0xFFF** | −1 = n비트 모두 1. 12비트 → 3니블 F |
| H3 | **−6** | 0x3A = 11 1010₂ = 58, MSB(bit5)=1 → 58 − 64 = −6 |
| H4 | **0b11011011** (0xDB) | 37 = 0010 0101 → 반전 1101 1010 → +1 = 1101 1011. 검산: 219 − 256 = −37 |
| H5 | **−56, 0xC8** | 200 − 256 = −56. 패턴은 200 = 0xC8 그대로 (C 에서 이 변환은 구현 정의, C11 6.3.1.3p3) |

</details>

<details>
<summary>H6–H10 답</summary>

| # | 답 | 계산 |
|---|---|---|
| H6 | **−32768** | 0x7FFF + 1 = 0x8000 → 16비트 smin. 실제 `int16_t` 연산이면 int 로 승격되어 32768 이 되고, 다시 int16 에 넣을 때 구현 정의 변환 (C-B02 참고) |
| H7 | **−272** | 0x2F0 = 752 ≥ 512 → 752 − 1024 = −272 |
| H8 | **0x0000_0000_FFFF_FFFF / 0xFFFF_FFFF_FFFF_FFFF** | 0 확장은 4294967295, 부호 확장은 −1. `uint32_t → int64_t` 는 0 확장, `int32_t → int64_t` 는 부호 확장 — 타입이 결정한다 |
| H9 | **−1, 최대 0** | 1비트 2의 보수: 패턴 0 = 0, 패턴 1 = −2⁰ = −1 |
| H10 | **−128** | 0x80 → 반전 0x7F → +1 = 0x80. 8비트에 +128 이 없다 |

</details>

### Part C — 펌웨어 현장의 부호 확장

**(1) 왼쪽 정렬 12비트 온도.** `reg >> 4` 로 정렬을 풀고(무부호 시프트라 정의됨) 12비트 부호 확장 → × 62500 µ°C. 0x1900 → 0x190 = 400 → 25 °C, 0xE700 → 0xE70 = −400 → −25 °C. 흔한 지름길 `(int16_t)reg >> 4` 는 16비트 패턴을 그대로 부호 있는 값으로 만든 뒤 산술 시프트로 정렬까지 한 번에 하는데, 위 Part A 표의 구현 정의 두 개에 기대는 코드다. 하위 4비트(이 형식에선 0)는 무시되도록 0xFFFF → −62500 도 검사한다. µ°C 정수로 받은 이유: 0.0625 = 1/16 이라 LSB 단위로는 정확하지만 m°C(62.5)는 정수가 아니다 — 부동소수 없는 MCU 에서의 표준 기법.

**(2) 5비트 트림 필드.** 읽기 = 필드 꺼내기(C-B04) + `sign_extend(.., 5)`. 쓰기는 `fits_s(val, 5)` 로 **먼저 범위 검사** 하고, `trunc_n((uint64_t)val, 5)` 로 패턴을 만든다: −1 → `0x1F`, −16 → `0x10`. 범위 검사를 빼면 `trim_set(reg, 16)` 이 `0x10` = **−16** 을 써서 오실레이터를 반대 방향 끝까지 돌린다 — 잘림이 부호를 뒤집는 전형적인 사고. 32개 값 전부를 무작위 레지스터 위에 썼다 읽어 "필드 외 비트 보존" 까지 확인한다.

**(3) AArch64 B/BL imm26.**

```svg
<svg viewBox="0 0 760 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">브랜치 오프셋 = 부호 확장된 즉치값 × 명령 크기</text>
  <text x="10" y="52" fill="currentColor">AArch64 B/BL (32비트)</text>
  <rect x="190" y="36" width="30" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="220" y="36" width="110" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="330" y="36" width="410" height="28" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="205" y="55">op</text><text x="275" y="55">0 0 1 0 1</text><text x="535" y="55">imm26</text>
    <text x="205" y="80">31</text><text x="275" y="80">30..26</text><text x="535" y="80">25..0</text>
  </g>
  <text x="190" y="104" fill="currentColor" font-size="12">op=0 B / op=1 BL · sext(imm26) × 4 · 기준 = 명령 자신 · ±128 MiB</text>
  <text x="10" y="146" fill="currentColor">Thumb B.N T2 (16비트)</text>
  <rect x="190" y="130" width="110" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="300" y="130" width="300" height="28" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="245" y="149">1 1 1 0 0</text><text x="450" y="149">imm11</text>
    <text x="245" y="174">15..11</text><text x="450" y="174">10..0</text>
  </g>
  <text x="190" y="198" fill="currentColor" font-size="12">sext(imm11) × 2 · 기준 = 명령 + 4 · −2048..+2046 B (연습 3)</text>
</svg>
```

명령은 항상 4바이트 정렬이라 하위 2비트는 늘 0 → 인코딩에서 빼서 범위를 4배로 늘린다. `demo_branch.s` 를 이 맥에서 어셈블한 실제 결과:

```text
$ cc -c demo_branch.s -o /tmp/esn_ARCH-B01/br.o && xcrun llvm-objdump -d --stop-address=0x20 /tmp/esn_ARCH-B01/br.o
0000000000000000 <ltmp0>:
       0: d503201f     	nop
       4: 17ffffff     	b	0x0 <ltmp0>
       8: 97fffffe     	bl	0x0 <ltmp0>
       c: 14000002     	b	0x14 <fwd>
      10: d503201f     	nop

0000000000000014 <fwd>:
      14: 14000000     	b	0x14 <fwd>
      18: 14040000     	b	0x100018 <far>
      1c: 17fffff9     	b	0x0 <ltmp0>
```

손으로 읽기: `0x17ffffff` → [30:26] = 00101 (B), imm26 = `0x3FFFFFF` = 26비트 −1 → ×4 = −4 → 0x4 − 4 = 0x0 ✓. `0x97fffffe` → bit31 = 1 (BL), imm26 = −2 → −8 ✓. `0x14040000` → imm26 = 0x40000 → ×4 = 0x100000 ✓. 이 6개를 decode/encode 양쪽 테스트에 그대로 넣었다. 범위 끝: +0x7FFFFFC → `0x15FFFFFF`, −0x8000000 → `0x16000000`, +0x8000000 과 비정렬 오프셋은 거부.

**(4) parse_num.** 규칙은 어셈블러·디버거와 같다: 부호 없는 리터럴은 무부호 범위(8비트에 `255`, `0xFF` OK), `-` 붙은 리터럴은 부호 범위(`-128` OK, `-129` 거부). 그래서 8비트에 `128` 과 `-128` 이 **같은 패턴 0x80** 으로 들어간다 — 패턴 기준 도구라서 맞는 동작이다. 넘침은 누적 전에 `acc > (UINT64_MAX - d) / base` 로 막는다 (`18446744073709551616` 거부). 마지막으로 `to_str`(3진법 × 2보기) → `parse_num` 왕복 3만 번이 원래 패턴으로 돌아오는지 본다 — 두 함수가 서로의 명세를 검사하는 셈.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 표 출력 후 "89/89 passed", exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 경고 없이 89/89
```

데모 (체크 테스트 아님, 관찰용):

```bash
cc -std=c11 -O2 demo_ub_shift.c -o ub && ./ub                          # n=64 naive 마스크 = 0
cc -std=c11 -O2 -fsanitize=undefined demo_ub_shift.c -o ubsan && ./ubsan
cc -O2 -S -o - demo_asm.c                                               # arm64: sbfx / sxtb
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
cc -c demo_branch.s -o br.o && xcrun llvm-objdump -d --stop-address=0x20 br.o
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `(1ull << n) - 1` 에 n = 64 | UB. arm64 실측 결과 **0** (lsl 이 mod 64) | `n >= 64 ? UINT64_MAX : (1ull << n) - 1` 또는 `UINT64_MAX >> (64 - n)` |
| `1ll << 63` 으로 INT64_MIN | signed 오버플로 UB | `INT64_MIN` 또는 `-smax - 1` |
| `mag = -s` (s = INT64_MIN) | 결과가 int64 범위 밖 → UB | `mag = 0 - (uint64_t)s` |
| `off = sext(imm) << 2` | 음수 왼쪽 시프트 UB (UBSan 실측) | `sext(imm) * 4` |
| 무부호 보기에서 패딩 생략 (`0x3` vs `0x03`) | 레지스터 폭 정보를 잃어 덤프 비교가 깨짐 | `width = (n + 3) / 4` 자리까지 '0' |
| 범위 검사 없이 필드에 `val & 0x1F` | 16 → 0x10 = −16, 부호가 뒤집힘 | `if (!fits_s(val, 5)) return error;` |
| `uint16_t reg; int32_t t = reg >> 4;` | 무부호 시프트로 정렬만 하고 부호 확장을 안 함 → 0xE700 이 3696 (−400 이어야) | `sign_extend(reg >> 4, 12)` |
| 비트 수 < 64 인 ASR 을 `~(~x >> k)` 로 | n비트 바깥의 1 들이 밀려 들어옴 (추가 연습 5 에서 실제로 걸림) | `~(trunc_n(~x, n) >> k)` 후 `trunc_n` |
| 넘침 검사를 곱한 뒤에 (`if (acc*10 < acc)`) | 무부호 넘침은 단조 감소를 보장하지 않음 | `acc > (UINT64_MAX - d) / base` 를 먼저 |

## 8. 면접 Q&A

<details>
<summary>Q1. 왜 거의 모든 CPU 가 2의 보수를 쓰나요?</summary>

덧셈 회로 하나로 부호/무부호 덧셈·뺄셈을 모두 한다: `a − b = a + ~b + 1` (가산기의 carry-in 에 1). 0 이 하나뿐이고(부호-크기·1의 보수는 +0/−0 두 개), 비교·확장 규칙이 단순하다. 플래그만 다르게 해석한다: 무부호 넘침은 C, 부호 넘침은 V (ARCH-B02).
꼬리질문: "그럼 하드웨어는 a+b 가 부호 있는 덧셈인지 어떻게 아나요?" → 모른다. 결과 비트는 같고, C 와 V 를 **둘 다** 계산해 두면 다음 명령(BCS 냐 BVS/BGE 냐)이 골라 쓴다. 가산기 구조는 ARCH-B04.

</details>

<details>
<summary>Q2. 12비트 값을 C 로 부호 확장해 보세요. 그 코드는 이식 가능한가요?</summary>

`int32_t s = (int32_t)((x & 0xFFF) ^ 0x800) - 0x800;` — `x ^ 0x800` 은 0..4095 라 int32 로 안전하고, 빼기도 범위 안이라 **완전히 정의된** 코드다. `(int32_t)(x << 20) >> 20` 은 ① 큰 uint32 → int32 변환 ② 음수 `>>` 두 군데가 구현 정의(C11 6.3.1.3p3, 6.5.7p5). gcc/clang 은 문서로 보장하므로 실무에선 흔하다.
꼬리질문: "어떤 명령이 되나요?" → 상수 폭이면 `SBFX r0, r0, #0, #12` 하나 (Cortex-M3/M4 이상, AArch64). 8/16비트면 `SXTB/SXTH`. 이 맥의 clang 실측으로 확인했다 (풀이 Part A).

</details>

<details>
<summary>Q3. abs(INT_MIN), -INT64_MIN, INT_MIN / -1 은 어떻게 되나요?</summary>

셋 다 결과가 표현 범위 밖이라 **UB** (C11 6.5p5; `INT_MIN / -1` 은 6.5.5p6 에 따라 몫을 표현 못 하면 UB). x86 에서 `INT_MIN / -1` 은 #DE 예외로 프로세스가 죽고, AArch64 `SDIV` 는 예외 없이 INT_MIN 을 돌려준다 — 같은 C 코드가 플랫폼마다 다르게 깨진다. 크기가 필요하면 `0u - (unsigned)x` 처럼 무부호로 계산한다 (`to_str` 이 그렇게 한다).
꼬리질문: "printf("%d", INT_MIN) 은 그럼 어떻게 구현하나요?" → 같은 방법. 무부호 크기를 뽑아 자리를 낸다.

</details>

<details>
<summary>Q4. char 는 signed 인가요? 0xB4 를 char 에 넣고 int 로 비교하면?</summary>

구현 정의다. 이 맥에서 실제 확인한 매크로: Apple arm64 는 `__CHAR_UNSIGNED__` 가 **없음**(signed char), `--target=thumbv7em-none-eabi` 와 `--target=aarch64-linux-gnu` 는 `__CHAR_UNSIGNED__ 1` (ARM AAPCS 는 unsigned char). 그래서 `char c = 0xB4; if (c == 0xB4)` 는 Cortex-M 에선 참, 맥에선 거짓(c = −76 으로 승격). 바이트 버퍼는 항상 `uint8_t`.
꼬리질문: "x86 Linux 는?" → signed. 같은 프로토콜 파서가 보드와 PC 테스트에서 다르게 동작하는 원인.

</details>

<details>
<summary>Q5. AArch64 B 명령 하나로 얼마나 멀리 점프할 수 있나요? 넘으면?</summary>

imm26 × 4 → ±128 MiB (정확히는 −2²⁷ .. 2²⁷−4). BL 도 같다. 조건 분기 `B.cond`/`CBZ` 는 imm19 × 4 = ±1 MiB, `TBZ` 는 imm14 × 4 = ±32 KiB. 범위를 넘으면 링커가 **veneer(range extension thunk)** — 주소를 레지스터에 만들어 `BR` 하는 작은 코드 조각 — 를 끼워 넣는다. Thumb 16비트 `B` 는 ±2 KiB 수준이라 Cortex-M 에서 함수 사이 호출은 32비트 `BL`(±16 MiB) 을 쓴다.
꼬리질문: "왜 ×4 인가?" → 명령이 4바이트 정렬이라 하위 2비트가 항상 0. 버리면 같은 비트로 4배 범위. Thumb 은 2바이트 정렬이라 ×2.

</details>

<details>
<summary>Q6. 2의 보수 말고 다른 표현은 어디서 아직 쓰나요?</summary>

부호-크기: IEEE 754 부동소수의 부호 비트 (ARCH-B03), 일부 센서·ADC 출력. 1의 보수: IP/TCP/UDP 체크섬의 1의 보수 합 (end-around carry). 바이어스(excess-K): float 지수(bias 127), 일부 DAC 의 offset binary 코드. offset binary 는 2의 보수의 MSB 만 뒤집은 것이라 변환이 XOR 하나다 (추가 연습 1 에서 n = 2..12 전수 확인).
꼬리질문: "C23 은?" → C23 부터 signed 정수는 2의 보수로 **규정** 된다. 하지만 오버플로가 UB 인 것은 그대로다.

</details>

<details>
<summary>Q7. -7 >> 1 과 -7 / 2 는 같나요?</summary>

다르다. 산술 시프트는 −∞ 쪽 내림 → −4, C 의 `/` 는 0 쪽 자르기 → −3 (C99 부터 규정). 그래서 컴파일러는 `x / 2` 를 `(x + (x >> 31 & 1)) >> 1` 처럼 음수 보정을 넣어 만든다. 또 음수 `>>` 자체가 구현 정의라 이식 가능한 코드에선 패턴 위에서 직접 계산한다 (추가 연습 5).

</details>

## 9. 추가 연습

### 연습 1 — 다른 부호 표현

8비트 `0x80` 과 `0xFF` 를 부호-크기, 1의 보수, 2의 보수, excess-128 로 읽어라. 그리고 "excess-2ⁿ⁻¹ = MSB 를 뒤집은 2의 보수" 를 n = 2..12 전수로 확인하라.

<details>
<summary>답 보기</summary>

| 패턴 | 부호-크기 | 1의 보수 | 2의 보수 | excess-128 |
|---|---|---|---|---|
| 0x80 | −0 (= 0) | −127 | −128 | 0 |
| 0xFF | −127 | −0 (= 0) | −1 | 127 |

excess-K 값 = 패턴 − K. K = 2ⁿ⁻¹ 이면 패턴 − 2ⁿ⁻¹ = `sign_extend(패턴 ^ 2ⁿ⁻¹, n)`. float32 지수는 K = 127 (0x7F → 0, 0x01 → −126) — ARCH-B03 참고.

</details>

### 연습 2 — BCD 시각 레지스터

RTC 는 시·분·초를 BCD(binary-coded decimal, 니블 하나 = 10진 한 자리)로 저장하는 경우가 많다 (STM32 RTC_TR/RTC_DR 도 BCD 필드. 정확한 비트 배치는 해당 RM 확인). 단순화한 `0x00HHMMSS` 형식에서 `rtc_to_seconds` 와 그 역함수를 만들고, 잘못된 BCD(`0x6A`)와 범위 밖(24시)을 거부하라. 0..86399 초 전수 왕복.

<details>
<summary>답 보기</summary>

니블마다 `> 9` 이면 무효. 변환은 `r = r*10 + nibble` (상위 자리부터), 역방향은 `x % 10` 을 니블에 넣고 `x /= 10`. `0x59` 는 16진으로 89 지만 BCD 로는 59 — 디버거에서 RTC 레지스터를 10진으로 보면 틀리는 이유. 사람이 읽기 쉽고 표시용 변환이 필요 없어서 RTC 가 BCD 를 쓴다.

</details>

### 연습 3 — Thumb 16비트 B (T2, imm11)

`demo_thumb_branch.s` 를 Cortex-M4 타깃으로 어셈블하면 아래가 나온다 (실측). `t2_b_decode/encode` 로 같은 인코딩을 만들고 범위 끝(+2046, −2048)을 확인하라.

```text
$ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -c demo_thumb_branch.s -o /tmp/esn_ARCH-B01/t.o
$ xcrun llvm-objdump -d /tmp/esn_ARCH-B01/t.o
00000000 <back>:
       0: bf00         	nop
       2: e7fd         	b	0x0 <back>              @ imm = #-0x6
       4: e001         	b	0xa <fwd>               @ imm = #0x2
       6: bf00         	nop
       8: bf00         	nop

0000000a <fwd>:
       a: e7fe         	b	0xa <fwd>               @ imm = #-0x4
```

<details>
<summary>답 보기</summary>

`0xE7FD`: [15:11] = 11100, imm11 = `0x7FD` = 11비트 −3 → ×2 = −6 → target = 0x2 + 4 − 6 = 0x0. AArch64 와 달리 기준이 **명령 + 4** 다 (Thumb 에서 PC 를 읽으면 현재 명령 + 4). 자기 자신으로의 분기가 −4 (`e7fe`, 흔한 "while(1);" 패턴)인 것이 그 증거. 범위: imm11 이 −1024..1023 → −2048..+2046 바이트 (`0xE400`, `0xE3FF`).

</details>

### 연습 4 — 포화와 Q15

`sat_s(x, n)` (n비트 부호 범위로 자르기 대신 붙잡기)와 `q15_from_milli(m)` (−1000..1000 m 단위 → Q15 패턴)을 만들어라. +1.0 은 어떻게 되나?

<details>
<summary>답 보기</summary>

Q15 는 16비트 2의 보수를 2¹⁵ 로 나눈 값: 0x4000 = 0.5, 0x8000 = −1.0, 최대 0x7FFF = 0.99997. **+1.0 은 표현 불가** → 포화해서 0x7FFF. 포화 없이 자르면 32768 → 0x8000 = −1.0 이 되어 오디오/제어 루프에서 최대 출력이 최소 출력으로 뒤집힌다. DSP 명령 `SSAT`/`QADD` (Cortex-M4) 가 이 일을 하드웨어로 한다.

</details>

### 연습 5 — ASR 과 나눗셈

음수 `>>` 를 쓰지 않고 n비트 패턴 위의 산술 시프트 `asr_n(v, n, k)` 와, C 의 `/` 와 같은 결과를 내는 `div_pow2(x, k)` 를 만들어라. 10만 개 무작위로 `x / (1 << k)` 및 clang 의 `x >> k` 와 비교.

<details>
<summary>답 보기</summary>

음수 패턴은 "n비트 안에서 반전 → 논리 시프트 → 반전": `~(trunc_n(~x, n) >> k)` 를 n비트로 자른다. 처음엔 `trunc_n` 없이 `~(~x >> k)` 로 썼다가 실제로 FAIL 이 났다 — 64비트 `~x` 의 위쪽 1 들이 n비트 안으로 밀려 들어온다. 나눗셈은 음수일 때 `2^k − 1` 을 더한 뒤 ASR (내림 → 0 쪽 자르기로 보정): −7, k=1 → (−7+1) >> 1 = −3.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   # 22/22 passed
```

## 10. 핵심 정리

- n비트 패턴 하나 = 두 가지 읽기: 무부호 0..2ⁿ−1, 2의 보수 −2ⁿ⁻¹..2ⁿ⁻¹−1 (MSB 무게만 음수). 하드웨어는 구별하지 않는다.
- −1 = 모두 1, smin = 1000…0, smax = 0111…1. 범위가 비대칭이라 −smin = smin, `abs(INT_MIN)` 은 UB.
- 마스크: `n >= 64 ? ~0ull : (1ull << n) - 1`. `1ull << 64` 는 UB, arm64 실측 결과 마스크가 0 이 된다.
- 부호 확장 = `(x ^ m) - m` (m = 부호 비트). 상수 폭이면 `SBFX`/`SXTB` 한 개. 시프트 트릭은 구현 정의 두 개에 기댄다.
- 비트 조작은 uint64 위에서, 값으로 바꿀 때만 signed. 크기는 `0 - (uint64_t)s`, 오프셋 배율은 `* 4` (음수 `<<` 는 UB).
- 필드에 쓰기 전에 `fits_s/fits_u` — 잘림은 부호를 뒤집는다 (16 → −16).
- 브랜치 오프셋 = sext(imm) × 명령 크기: AArch64 imm26 ×4 ±128 MiB (기준 = 명령 자신), Thumb imm11 ×2 (기준 = 명령 + 4).
- `char` 부호는 플랫폼마다 다르다 (Apple arm64 signed, ARM AAPCS unsigned) → 바이트는 `uint8_t`.

## 참고자료

- ISO/IEC 9899:2011 (C11) — 6.3.1.3 (signed/unsigned 변환), 6.5.7 (시프트 UB/구현 정의), 6.5.5 (나눗셈)
- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — B/BL imm26, SBFX/SXTB 인코딩
- [Armv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — Thumb B (T2), SBFX, SSAT
- [Procedure Call Standard for the Arm Architecture (AAPCS)](https://github.com/ARM-software/abi-aa) — `char` 는 unsigned
- [Apple: Writing ARM64 code for Apple platforms](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) — Apple arm64 ABI 와 AAPCS64 의 차이
- [TI TMP102 데이터시트](https://www.ti.com/product/TMP102) — 12비트 왼쪽 정렬 2의 보수 온도 형식
- Bryant & O'Hallaron, *Computer Systems: A Programmer's Perspective*, 2장 (정수 표현)
- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition*, 1장 (수 체계)

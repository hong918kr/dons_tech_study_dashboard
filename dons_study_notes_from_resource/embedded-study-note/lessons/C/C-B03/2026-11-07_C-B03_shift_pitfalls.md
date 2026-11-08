---
id: C-B03
time: 50분
updated: 2026-11-07
---
# C-B03 Shift 연산의 함정: 폭 초과·음수·산술 시프트

## 학습 목표

- 시프트 식 하나를 보고 **정의됨 / 구현 정의(implementation-defined) / 정의되지 않은 동작(UB)** 중 무엇인지 C11 6.5.7 규칙으로 판정한다.
- `x << 32` 가 x86·arm64 에서는 `x`, Cortex-M 에서는 `0` 이 되는 **하드웨어 이유**와, 그래서 "내 보드에서 돌았다"가 증거가 안 되는 이유를 설명한다.
- 어떤 `n` 이 와도 안전한 `shl32()` / `shr32()` / `sar32()` / `rotl32()` 를 쓰고 경계값(0, 31, 32, 32 초과, 음수)으로 검증한다.
- `-fsanitize=undefined` 보고를 읽고, 펌웨어에서는 `-fsanitize-trap` 으로 같은 검사를 HardFault 로 바꿀 수 있음을 안다.
- CAN 신호처럼 **가변 폭 필드**를 꺼내고 부호 확장할 때 폭 경계(`len == 32`, 64비트 payload)를 UB 없이 처리한다.

## 1. 문제

시프트는 레지스터 마스크, 바이트 조립, 고정소수점 연산 어디에나 나오고, 면접에서는 "`1 << 31` 이 왜 문제죠?", "`x << 32` 는 뭐가 나오죠?" 로 **C 표준을 정확히 아는지** 가르는 단골 질문이다.

C11 6.5.7 의 규칙은 짧다. ① 두 피연산자 각각에 정수 승격(integer promotion)을 하고, 결과 타입은 **승격된 왼쪽 피연산자**의 타입이다. ② 오른쪽 값(시프트 양)이 **음수이거나 그 타입의 폭(width) 이상이면 UB**. ③ `<<` 는 unsigned 면 모듈러로 정의, signed 면 "0 이상이고 결과가 표현 가능할 때만" 정의 — 아니면 UB. ④ 음수의 `>>` 는 UB가 아니라 **구현 정의**.

```svg
<svg viewBox="0 0 720 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">시프트 식 판정표 (int = 32비트, x 는 uint32_t, b 는 uint8_t)</text>
  <rect x="10" y="40" width="220" height="210" rx="8" class="f-ok" stroke="currentColor"/>
  <rect x="250" y="40" width="200" height="210" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="470" y="40" width="240" height="210" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-weight="bold">
    <text x="24" y="64">정의됨</text><text x="264" y="64">구현 정의</text><text x="484" y="64">UB (무엇이든 가능)</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="24" y="94">x &lt;&lt; 31, x &gt;&gt; 31</text>
    <text x="24" y="120">1u &lt;&lt; 31</text>
    <text x="24" y="146">b &lt;&lt; 8   (int 0xAB00)</text>
    <text x="24" y="172">7 &gt;&gt; 1   (양수 &gt;&gt;)</text>
    <text x="24" y="198">(uint64_t)b &lt;&lt; 32</text>
    <text x="264" y="94">-1 &gt;&gt; 1</text>
    <text x="264" y="120">(음수 &gt;&gt; n)</text>
    <text x="264" y="146">(int32_t)0xFFFFFFFFu</text>
    <text x="264" y="172">(범위 밖 → signed 변환)</text>
    <text x="484" y="94">x &lt;&lt; 32, x &gt;&gt; 32</text>
    <text x="484" y="120">x &lt;&lt; -1</text>
    <text x="484" y="146">1 &lt;&lt; 31,  -1 &lt;&lt; 1</text>
    <text x="484" y="172">b &lt;&lt; 24   (b ≥ 0x80)</text>
    <text x="484" y="198">(uint32_t)b &lt;&lt; 32</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="24" y="232">값이 표준으로 정해짐</text>
    <text x="264" y="222">컴파일러가 문서화해야 함</text>
    <text x="264" y="240">(GCC/clang: 산술 시프트)</text>
    <text x="484" y="222">컴파일러가 "안 일어난다"고</text>
    <text x="484" y="240">가정하고 최적화할 수 있음</text>
  </g>
</svg>
```

### Part A — UB 관찰 + 폭을 넘어도 안전한 논리 시프트

먼저 `demo_shift_ub.c` 를 **실행 전에 결과를 예측해 적고**, 네 가지로 실행해 비교한다: arm64 네이티브, x86-64 (Rosetta 2), `-fsanitize=undefined`, Cortex-M3 (QEMU). 그 다음 아래 두 함수를 구현한다. `n` 은 `unsigned` 아무 값이며, 비트가 모두 밀려나면 수학적으로 맞는 값 `0` 을 돌려준다.

- `uint32_t shl32(uint32_t x, unsigned n)`, `uint32_t shr32(uint32_t x, unsigned n)`

### Part B — 이식 가능한 산술 시프트와 회전

- `int32_t sar32(int32_t x, unsigned n)` — `floor(x / 2^n)`. 음수에 `>>` 를 직접 쓰지 않는다(구현 정의 의존 금지). `n ≥ 32` 면 음수 → `-1`, 0 이상 → `0`.
- `uint32_t rotl32(uint32_t x, unsigned n)` — 왼쪽 회전. `n == 0` 이나 `n == 32` 에서 `x >> 32` 가 생기지 않게.

### Part C — CAN 신호 디코더 (Intel byte order)

차량 ECU(모터 제어기, BMS)는 8바이트 CAN 프레임 안에 여러 신호를 비트 단위로 채워 보낸다. DBC 파일이 "신호 X = start bit 16, 길이 16, signed" 처럼 정의한다.

- `load_le64(d)` — 8바이트 → `uint64_t` (d[0] 이 최하위). `d[i] << (8*i)` 함정 주의.
- `can_get_raw(d, start, len)` — `1 ≤ len ≤ 32`, `start + len ≤ 64`. **`len == 32` 도** 맞아야 한다.
- `can_get_signed(d, start, len)` — 같은 필드를 len 비트 2의 보수로 보고 부호 확장. Part A/B 헬퍼 조합으로.

## 2. Problem (English)

> **Safe shifts.** In C11, shifting by a negative amount or by `>=` the width of the promoted left operand is undefined, left-shifting a negative or overflowing signed value is undefined, and right-shifting a negative value is implementation-defined.
>
> 1. Implement `shl32(x, n)` and `shr32(x, n)` for `uint32_t x` and any `unsigned n`, returning `0` when `n >= 32`.
> 2. Implement `int32_t sar32(int32_t x, unsigned n)` returning `floor(x / 2^n)` (so `sar32(-7, 1) == -4`) for any `n`, without relying on implementation-defined behavior. Implement `rotl32(x, n)` that is correct for every `n`, including `0` and multiples of 32.
> 3. Decode signals from an 8-byte little-endian CAN payload: `can_get_raw(d, start, len)` and `can_get_signed(d, start, len)` for `1 <= len <= 32`, `start + len <= 64`.
>
> Example: payload `34 12 18 FC EF BE AD DE` → `can_get_signed(d, 16, 16) == -1000`, `can_get_raw(d, 32, 32) == 0xDEADBEEF`, `can_get_signed(d, 63, 1) == -1`.
>
> Follow-up: what does `x << 32` evaluate to on x86-64 vs. a Cortex-M4, and why is "it works on my board" not an argument?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "6/32 passed" — n == 0 같은 우연히 맞는 경우만 통과. TODO를 채워 32/32 로
```

관찰용 데모 (lesson 디렉터리에 같이 있음):

```bash
cc -std=c11 -O0 demo_shift_ub.c -o demo && ./demo                          # arm64 (Apple Silicon)
cc -std=c11 -O0 -arch x86_64 demo_shift_ub.c -o demo_x86 && ./demo_x86       # x86-64 (Rosetta 2)
cc -std=c11 -g -O0 -fsanitize=undefined demo_shift_ub.c -o demo_ub && ./demo_ub
# Cortex-M3: Lima VM(elinux) 안에서
arm-none-eabi-gcc -std=c11 -O0 -mcpu=cortex-m3 -mthumb --specs=rdimon.specs \
    -Wl,-Ttext-segment=0x100000 demo_shift_ub.c -o demo_m3 && qemu-arm -cpu cortex-m3 ./demo_m3
```

> `-Wl,-Ttext-segment=0x100000`: 기본 링크 주소(0x8000)로는 `qemu-arm` 이 "Error mapping file: Operation not permitted" 로 실패했다 (VM의 최소 mmap 주소 제한으로 추정). 시프트 양은 `volatile` 에서 읽어 컴파일러가 미리 계산하지 못하게 했다 → CPU의 실제 시프트 명령이 실행된다.

## 4. 힌트

<details>
<summary>힌트 1 — 경계 검사는 시프트 "전에"</summary>

`r = x << n; if (n >= 32) r = 0;` 는 이미 늦다. 시프트하는 순간 UB이고, 컴파일러는 "n < 32 였겠지"라고 가정할 수 있다. 조건 연산자로 **시프트를 아예 실행하지 않는 경로**를 만든다.

</details>

<details>
<summary>힌트 2 — 음수를 오른쪽으로 밀지 않고 floor 나눗셈 하기</summary>

`x < 0` 이면 `~x` 는 `-x - 1` 로 **0 이상**이다. 0 이상 값의 `>>` 는 정의되어 있다. 밀고 나서 다시 `~` 하면? `-7`: `~-7 = 6`, `6 >> 1 = 3`, `~3 = -4`.

</details>

<details>
<summary>힌트 3 — 회전의 두 번째 항</summary>

`(x << n) | (x >> (32 - n))` 는 `n == 0` 에서 `x >> 32`. 시프트 양을 `& 31` 로 접으면 n=0 일 때 두 항이 모두 `x` 라 OR 해도 `x`.

</details>

<details>
<summary>힌트 4 — 부호 확장 = 왼쪽 정렬 후 산술 시프트</summary>

len 비트 필드의 부호 비트를 bit 31 로 올리고(`shl32(raw, 32-len)`), 다시 `sar32(…, 32-len)` 로 내리면 위쪽이 부호로 채워진다. 중간에 `uint32_t → int32_t` 변환이 필요한데, 범위 밖 변환은 구현 정의라 주어진 `u32_to_s32()` 를 쓴다. `len == 32` 면 시프트 양 0 — 그대로 통과.

</details>

## 5. 풀이

### Part A — 관찰 결과와 하드웨어 차이

`demo_shift_ub.c` 를 네 환경에서 실제로 돌린 결과 (`x = 0x12345679`, 시프트 양은 런타임 값):

| # | 식 | 표준 | arm64 (M-시리즈) | x86-64 (Rosetta) | Cortex-M3 (QEMU) |
|---|---|---|---|---|---|
| 1 | `x << 32` | UB | `0x12345679` | `0x12345679` | `0x00000000` |
| 2 | `x << 33` | UB | `0x2468ACF2` | `0x2468ACF2` | `0x00000000` |
| 3 | `x >> 32` | UB | `0x12345679` | `0x12345679` | `0x00000000` |
| 3b | `x << 256` | UB | `0x12345679` | `0x12345679` | `0x12345679` |
| 4 | `1 << 31` | UB | `-2147483648` | 같음 | 같음 |
| 5 | `x << -1` | UB | `0x80000000` | `0x80000000` | `0x00000000` |
| 6 | `-1 << 1` | UB | `-2` | 같음 | 같음 |
| 7 | `-1 >> 1` | 구현 정의 | `-1` | `-1` | `-1` |
| 8 | `(uint8_t)0xAB << 8` | 정의됨 | `0xAB00` | 같음 | 같음 |
| 9 | `(uint8_t)0x80 << 24` | UB | `-2147483648` | 같음 | 같음 |
| 10 | `(uint32_t)b << 32` | UB | `0x000000AB` | `0x000000AB` | `0x00000000` |
| 11 | `(uint64_t)b << 32` | 정의됨 | `0x000000AB00000000` | 같음 | 같음 |
| 12 | `(uint16_t)0xFFFF << 16` | UB | `-65536` | 같음 | 같음 |

같은 소스인데 **폭 초과 줄(1, 2, 3, 5, 10)이 칩마다 다르다.** 이유는 CPU의 "레지스터 지정 시프트" 명령이 시프트 양을 읽는 방식이 달라서다.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">x &lt;&lt; n 에서 CPU가 n 을 읽는 방식</text>
  <rect x="10" y="40" width="340" height="120" rx="8" class="f-acc" stroke="currentColor"/>
  <rect x="370" y="40" width="340" height="120" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="24" y="64" font-weight="bold">x86 SHL r32,CL / arm64 LSLV Wd</text>
    <text x="24" y="90" font-family="monospace">shift = n &amp; 31   (5비트만)</text>
    <text x="24" y="116" font-family="monospace">n=32 → 0칸 → x</text>
    <text x="24" y="138" font-family="monospace">n=-1 → 31칸 → x&lt;&lt;31</text>
    <text x="384" y="64" font-weight="bold">Cortex-M (T32) LSL/LSR Rd,Rm</text>
    <text x="384" y="90" font-family="monospace">shift = n &amp; 0xFF (하위 8비트)</text>
    <text x="384" y="116" font-family="monospace">32 ≤ shift ≤ 255 → 0</text>
    <text x="384" y="138" font-family="monospace">n=256 → 0칸 → x,  n=-1 → 0</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="190">표준은 어느 쪽도 고르지 않았다 → n ≥ 32 를 UB 로 두어 모든 CPU가 명령 1개로 시프트하게 했다.</text>
    <text x="10" y="214">대가: 프로그래머가 n ≥ 32 를 직접 막아야 하고, 안 막으면 결과는 칩·최적화 레벨마다 다르다.</text>
    <text x="10" y="238">64비트 시프트(x86 REX.W, arm64 Xd)는 6비트(n &amp; 63)를 본다.</text>
  </g>
</svg>
```

**UB 는 "칩마다 다른 값"으로 끝나지 않는다.** 컴파일러는 UB가 일어나지 않는다고 가정해도 된다. `demo_shift_opt.c` 의 `mask_is_zero(n) { return (1u << n) == 0; }` — "시프트 결과가 0 이면 n 이 너무 컸다"는 사후 검사 — 를 `n = 32` 로 돌린 실제 결과:

```text
mac arm64 -O0: mask_is_zero(32) = 0      cortex-m3 gcc -O0: mask_is_zero(32) = 1
mac arm64 -O2: mask_is_zero(32) = 0      cortex-m3 gcc -O2: mask_is_zero(32) = 0
```

Cortex-M3 에서 `-O0` 은 LSL 이 0 을 만들어 `1`, `-O2` 는 GCC가 "1u<<n 은 절대 0이 아니다"라고 보고 함수를 지워 **printf 인자를 상수 0** 으로 넣었다 (`ldr r3,[r3]` 로 volatile 은 읽지만 `movs r1, #0`). clang 도 같다 — arm64 `-O2 -S` 결과는 `mov w0, #0; ret` 한 줄. 디버그 빌드에서 통과하던 검사가 릴리스 빌드에서 사라지는 전형적인 UB 버그다.

**UBSan 보고** (`-fsanitize=undefined`, 실제 출력 일부):

```text
demo_shift_ub.c:20:49: runtime error: shift exponent 32 is too large for 32-bit type 'uint32_t' (aka 'unsigned int')
demo_shift_ub.c:24:48: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
demo_shift_ub.c:25:49: runtime error: shift exponent -1 is negative
demo_shift_ub.c:26:47: runtime error: left shift of negative value -1
demo_shift_ub.c:29:48: runtime error: left shift of 128 by 24 places cannot be represented in type 'int'
demo_shift_ub.c:33:51: runtime error: left shift of 65535 by 16 places cannot be represented in type 'int'
```

`[7] -1 >> 1` 과 `[8] b << 8` 은 **보고가 없다** — 각각 구현 정의, 정의됨이라서. 메시지 두 종류를 구분하자: "shift exponent …" 는 시프트 **양** 문제(`-fsanitize=shift-exponent`), "left shift of …" 는 **왼쪽 값** 문제(`-fsanitize=shift-base`).

**`uint8_t << 8` 은 왜 괜찮나** (승격 규칙 자체는 C-B02 참고): `b` 는 `int` 로 승격되고 폭 판정도 승격된 타입(32비트) 기준이라 `b << 8` 은 폭 안이고 값 `0xAB00` 도 int 에 들어간다. 문제는 결과가 `int` 의 부호 비트에 닿을 때다 — `b << 24` (b ≥ 0x80), `(uint16_t)0xFFFF << 16`. 그리고 C-B02 의 처방 `(uint32_t)b << 24` 도 **32 칸부터는 모자란다** (`[10]`). 64비트 조립이면 `(uint64_t)` 로 캐스트해야 한다.

컴파일 타임에 상수면 경고로 잡힌다 (clang, `-Wall`):

```text
warning: shifting a negative signed value is undefined [-Wshift-negative-value]      -1 << 1
warning: shift count >= width of type [-Wshift-count-overflow]                        x << 32
```

`1 << 31` 은 `-Wall` 에 안 걸리고 `-Wshift-sign-overflow` 를 따로 켜야 한다: `signed shift result (0x80000000) sets the sign bit of the shift expression's type ('int') and becomes negative`.

**헬퍼 구현**: 조건 연산자로 `n >= 32` 일 때 시프트를 아예 평가하지 않는다 (`?:` 는 고른 쪽만 평가 — C11 6.5.15p4).

```c
return (n >= 32u) ? 0u : (x << n);
```

clang `-O2` 결과 — arm64: `lsl w8,w0,w1; cmp w1,#31; csel w0,wzr,w8,hi`, Cortex-M: `lsls r0,r1; cmp r1,#31; it hi; movhi r0,#0`. 분기 없이 명령 몇 개로 끝난다. (시프트를 먼저 실행하는 기계어지만, 기계어 수준에서는 시프트가 UB가 아니므로 문제없다. UB는 **C 소스** 의 성질이다.)

### Part B — floor 나눗셈으로서의 산술 시프트

| x | n | `x / 2^n` (C11 6.5.5: 0 쪽으로 자름) | `floor(x / 2^n)` = 산술 시프트 |
|---|---|---|---|
| 7 | 1 | 3 | 3 |
| -7 | 1 | **-3** | **-4** |
| -1 | 1 | 0 | -1 |
| `INT32_MIN` | 31 | -1 | -1 |

음수 `>>` 는 구현 정의지만 GCC 매뉴얼은 "sign extension" 이라고 문서화했고 clang도 같다. 그래도 표준만으로 이식 가능하게 쓰고 싶으면 **0 이상 값만 시프트**한다:

```c
if (n >= 32u) n = 31u;                    /* 31칸이면 부호만 남음: -1 또는 0 */
return (x < 0) ? ~(~x >> n) : (x >> n);
```

`x < 0` 일 때 `~x = -x - 1 ≥ 0` (int32_t 는 2의 보수가 보장 — C11 7.20.1.1). `~x >> n = floor((-x-1)/2^n)`, 다시 `~` 하면 `-floor((-x-1)/2^n) - 1 = floor(x/2^n)`. 컴파일러는 이걸 알아본다 — clang `-O2` 결과는 arm64 `cmp; csel; asr w0,w0,w8`, Cortex-M `cmp r1,#31; it hs; movhs r1,#31; asrs r0,r1`. **이식성 비용 0.**

**회전**: `rotl32(x, n) = (x << (n & 31)) | (x >> ((32 - n) & 31))`. n=0 → `x | x = x`. 두 시프트 양이 항상 0..31 이라 UB가 없다. clang 은 이 패턴을 인식해 arm64 `neg w8,w1; ror w0,w0,w8`, Cortex-M `rsbs r1,r1,#0; rors r0,r1` 로 바꾼다 (왼쪽 n = 오른쪽 -n 회전). 반대로 `(x << n) | (x >> (32 - n))` 는 n=0 에서 UB — 해시·CRC·암호 코드에서 실제로 자주 나온 버그다.

### Part C — 가변 폭 필드: 64비트 payload, len == 32, 부호 확장

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">can_get_signed(d, start=20, len=12): 0xFC1 → -63</text>
  <g font-family="monospace" fill="currentColor">
    <text x="10" y="50">payload = 0xDEADBEEF_FC181234      (load_le64)</text>
    <text x="10" y="74">raw = (payload &gt;&gt; 20) &amp; mask12 = 0x00000FC1</text>
    <text x="10" y="100">① shl32(raw, 20) = 0xFC100000   필드 부호 비트 → bit 31</text>
  </g>
  <rect x="10" y="110" width="700" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="10" y="110" width="180" height="30" class="f-acc" stroke="currentColor"/>
  <g font-family="monospace" fill="currentColor">
    <text x="22" y="130">1111 1100 0001</text><text x="210" y="130">0000 0000 0000 0000 0000</text>
    <text x="10" y="166">② u32_to_s32(...) = -66060288     (구현 정의 없이)</text>
    <text x="10" y="190">③ sar32(..., 20)  = 0xFFFFFFC1 = -63</text>
  </g>
  <rect x="10" y="200" width="700" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="530" y="200" width="180" height="30" class="f-acc" stroke="currentColor"/>
  <g font-family="monospace" fill="currentColor">
    <text x="22" y="220">1111 1111 1111 1111 1111  ← 부호로 채움</text><text x="542" y="220">1111 1100 0001</text>
  </g>
</svg>
```

단계별:

1. **load_le64** — `v |= (uint64_t)d[i] << (8u * i)`. 캐스트를 빼면 `d[i]` 는 int 로 승격 → i=3 에서 부호 비트(UB, C-B02), i=4 부터 폭 초과(UB). `(uint32_t)d[i]` 도 i=4 부터 폭 초과 — 위 데모 `[10]`.
2. **can_get_raw** — `(uint32_t)(payload >> start) & mask`. `start ≤ 63` 이라 64비트 시프트는 안전. 마스크는 `(1u << len) - 1u` 가 len=32 에서 UB 이므로 `shr32(0xFFFFFFFFu, 32u - len)` — len=32 → 0칸 → 전부 1. Part A 헬퍼가 경계 분기를 대신한다.
3. **can_get_signed** — `k = 32 - len`; `sar32(u32_to_s32(shl32(raw, k)), k)`. len=32 → k=0 → 변환만. len=1 → k=31 → 1비트 플래그 1 이 `-1` 이 된다.

`u32_to_s32()` 가 왜 필요한가: `(int32_t)0xFC100000u` 처럼 범위 밖 값을 signed 로 바꾸는 건 **구현 정의** (C11 6.3.1.3p3) — 모든 주류 컴파일러가 모듈러로 정의하지만 표준만으로는 보장되지 않는다. `(int32_t)(u - 0x80000000u) + INT32_MIN` 은 모든 단계가 범위 안이라 정의되며, clang `-O2` 는 이를 **`ret` 한 줄** (아무 명령 없음) 로 만든다.

> 실전 메모: 많은 코드베이스가 `(int32_t)(raw << k) >> k` 한 줄로 부호 확장한다. GCC/clang/armclang 에서 실제로 동작하지만 두 군데(범위 밖 변환, 음수 `>>`)가 구현 정의다. 면접에서는 "동작은 하지만 구현 정의 두 곳에 기댄다, MISRA C 에서는 signed 비트 연산 자체를 제한한다"까지 말할 수 있으면 좋다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 32/32 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_san && ./sol_san   # 32/32, 보고 0건
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `mask = (1u << len) - 1u;` (len 1..32) | len=32 → 폭 초과 UB. x86 에선 `1u<<0` 이라 마스크 0, Cortex-M 에선 0-1 = 0xFFFFFFFF — 칩마다 다른 버그 | `shr32(0xFFFFFFFFu, 32u - len)` 또는 len≥32 분기 |
| `r = x << n; if (n >= 32) r = 0;` | 검사가 시프트 뒤 → 이미 UB, 컴파일러가 검사를 지울 수 있음 | `r = (n >= 32u) ? 0u : x << n;` |
| `(x << n) \| (x >> (32 - n))` | n=0 에서 `x >> 32` UB | `(x << (n & 31)) \| (x >> (-n & 31))` |
| `v \|= (uint32_t)d[i] << (8*i)` 로 64비트 조립 | i≥4 에서 32비트 폭 초과 | `(uint64_t)d[i] << (8*i)` |
| `#define BIT(n) (1UL << (n))` 로 64비트 레지스터 | Cortex-M(ILP32)은 `unsigned long` 이 32비트 → n≥32 UB. 맥(LP64)에선 멀쩡해 못 잡음 | `UINT64_C(1) << (n)` / `1ULL << (n)` |
| `x / 2` 대신 `x >> 1` (signed) | 음수에서 -3 vs -4 로 값이 다르고, 음수 `>>` 는 구현 정의 | 나눗셈이 의도면 `/` 를 쓴다 — 컴파일러가 알아서 시프트로 바꾼다 (연습 2) |
| 시프트 양을 `int` 로 받고 음수 검사 생략 | `x << -1` UB. Cortex-M 에선 0, x86 에선 `x<<31` | 시프트 양은 `unsigned` 로 받고 범위 검사 |

## 8. 면접 Q&A

<details>
<summary>Q1. C에서 시프트가 UB가 되는 경우와 구현 정의인 경우를 구분해 말해보세요.</summary>

UB: ① 시프트 양이 음수 ② 시프트 양 ≥ 승격된 왼쪽 피연산자의 폭 ③ signed 왼쪽 값이 음수인 `<<` ④ signed 양수 `<<` 결과가 표현 범위를 넘음 (`1 << 31`, `0x80 << 24`). 구현 정의: 음수의 `>>`. unsigned 의 `<<` 는 넘쳐도 모듈러로 정의됨 (`0xFFFFFFFFu << 4 == 0xFFFFFFF0u`). 꼬리질문 "결과 타입은?" → 승격된 **왼쪽** 피연산자 타입. `x << 2ULL` 은 x 가 uint32_t 면 결과도 uint32_t.

</details>

<details>
<summary>Q2. uint32_t x 에 대해 x << 32 는 무엇이 나오나요? "x86 에선 x 가 나오던데요?"</summary>

UB 라서 "무엇이 나온다"는 답이 없다. x86 `SHL r32, CL` 은 시프트 양을 5비트로 마스킹해 `x`, arm64 `LSLV` 도 32 로 나눈 나머지라 `x`, Cortex-M 의 T32 `LSL Rd, Rm` 은 Rm 하위 8비트를 쓰고 32 이상이면 0. 실제로 같은 데모가 맥에선 `0x12345679`, Cortex-M3 에선 `0x00000000`. 게다가 최적화기는 n < 32 라고 가정하므로 `-O0` 과 `-O2` 결과도 다르다 (Cortex-M3 `mask_is_zero(32)` 가 1 → 0). 그래서 "내 보드에서 돌았다"는 증거가 아니다.

</details>

<details>
<summary>Q3. -7 >> 1 과 -7 / 2 는 같나요? 컴파일러는 x / 4 를 어떻게 컴파일하나요?</summary>

다르다. `/` 는 0 쪽으로 잘라 -3 (C99부터 표준), 산술 시프트는 floor 라 -4. 그래서 컴파일러는 signed `x / 4` 를 시프트 하나로 못 바꾸고 음수일 때 `2^n - 1` 을 더한 뒤 시프트한다 — clang arm64 실제 출력: `add w8,w0,#3; cmp w0,#0; csel w8,w8,w0,lt; asr w0,w8,#2`. Cortex-M: `asrs r1,r0,#31; add.w r0,r0,r1,lsr #30; asrs r0,r0,#2` (부호 비트로 bias 3 을 만듦). unsigned 나눗셈이면 `lsr` 하나. 꼬리질문: "그럼 음수 나눗셈을 빨리 하려면?" → 의도가 floor 인지 trunc 인지 먼저 정하고, 값이 항상 0 이상이면 unsigned 타입을 쓴다.

</details>

<details>
<summary>Q4. uint8_t b = 0x80; b << 24 는 안전한가요? (uint32_t)b << 24 는? (uint32_t)b << 32 는?</summary>

`b << 24`: b 가 int 로 승격되어 `0x80000000` 이 int 범위를 넘음 → UB (UBSan: "left shift of 128 by 24 places cannot be represented in type 'int'"). `(uint32_t)b << 24`: unsigned 라 정의, `0x80000000u`. `(uint32_t)b << 32`: 폭 초과 UB — 캐스트 타입이 결과 비트 위치보다 넓어야 한다 → `(uint64_t)b << 32`. 반면 `b << 8` 은 정의됨 (`0xAB00` 은 int 범위).

</details>

<details>
<summary>Q5. 32비트 회전(rotate)을 C로 써보세요. 컴파일러가 ROR 명령 하나로 만들어 주나요?</summary>

`(x << (n & 31)) | (x >> (-n & 31))` — 두 시프트 양이 항상 0..31 이라 n=0 에서도 UB 없음. clang 은 이 관용구를 인식해 arm64 `ror`, Cortex-M `rors` 한 개로 만든다 (실측). 흔한 오답 `(x << n) | (x >> (32 - n))` 은 n=0 에서 `x >> 32`. C23 에는 `<stdbit.h>` 가 생겼지만 회전 함수는 없고, C++20 은 `std::rotl` 이 있다.

</details>

<details>
<summary>Q6. 펌웨어에서는 UBSan 런타임이 없는데 이런 버그를 어떻게 잡나요?</summary>

① 드라이버의 순수 로직(필드 추출, 고정소수점)을 호스트 유닛 테스트로 빼서 `-fsanitize=undefined` 로 돌린다. ② 타깃에서는 `-fsanitize=shift -fsanitize-trap=shift` (GCC: `-fsanitize-undefined-trap-on-error`) — 런타임 라이브러리 없이 검사 실패 시 trap 명령. clang thumbv7m 실측: `cmp r1,#32; itt lo; lsllo r0,r1; bxlo lr` 다음에 `.inst.n 0xdefe` (UDF) → UsageFault/HardFault 로 잡힌다. ③ 컴파일 경고: `-Wshift-count-overflow`, `-Wshift-negative-value`, `-Wshift-sign-overflow`. ④ MISRA C:2012 Rule 12.2 (시프트 양은 0 ~ 폭-1) 정적 분석.

</details>

<details>
<summary>Q7. C++20 이나 C23 에서 이 규칙이 바뀌었나요?</summary>

C++20 (P1236) 은 signed 를 2의 보수로 못박으면서 signed `<<` 를 모듈러로, 음수 `>>` 를 산술 시프트로 **정의**했다. 하지만 시프트 양이 음수이거나 폭 이상이면 C++20 에서도 UB. C23 도 2의 보수를 의무화했지만 6.5.7 의 시프트 규칙(음수 `<<` UB, 음수 `>>` 구현 정의, 폭 초과 UB)은 그대로다. 즉 펌웨어 C 코드는 여전히 이 레슨의 헬퍼가 필요하다.

</details>

## 9. 추가 연습

### 연습 1 — GENMASK(h, l)

비트 h..l 이 1인 마스크 `genmask32(h, l)` (0 ≤ l ≤ h ≤ 31). `genmask32(31, 0)` 도 UB 없이.

<details>
<summary>답 보기</summary>

`(0xFFFFFFFFu >> (31u - h)) & (0xFFFFFFFFu << l)` — 위쪽 자르기와 아래쪽 자르기를 따로 하면 두 시프트 양이 모두 0..31 이다. `((1u << (h+1)) - 1) & ~((1u << l) - 1)` 은 h=31 에서 `1u << 32`. Linux `include/linux/bits.h` 의 `GENMASK()` 가 바로 이 이유로 `~0UL >> (BITS_PER_LONG - 1 - h)` 형태를 쓴다.

</details>

### 연습 2 — `/` 와 같은 결과를 내는 2^n 나눗셈

`div_pow2_s32(x, n)` (0 ≤ n ≤ 30) 이 모든 x 에서 `x / (1 << n)` 과 같게. `sar32` 를 써라.

<details>
<summary>답 보기</summary>

음수면 `2^n - 1` 을 더한 뒤 floor 시프트: `sar32(x + ((x < 0) ? (int32_t)((1u << n) - 1u) : 0), n)`. x 가 음수라 더해도 오버플로가 없다. 컴파일러가 `x / 4` 에 대해 만드는 `add #3; csel; asr #2` 와 같은 아이디어 (Q3).

</details>

### 연습 3 — 64비트 레지스터의 비트 n

`bit64(n)` (0..63). `1UL << n` 이 왜 이식성이 없는지 설명하라.

<details>
<summary>답 보기</summary>

`UINT64_C(1) << n`. `unsigned long` 은 맥/리눅스 64비트(LP64)에선 64비트지만 Cortex-M(ILP32)에선 32비트다. 같은 `1UL << 40` 이 맥 `-Wall` 에선 조용하고 `--target=thumbv7m-none-eabi` 에선 `warning: shift count >= width of type [-Wshift-count-overflow]` (실측). 호스트 테스트를 통과한 코드가 타깃에서 깨지는 전형.

</details>

### 연습 4 — Q15 고정소수점 곱셈

`q15_mul(a, b)`: Q15 두 값을 곱해 반올림하고 포화. `-1.0 × -1.0` 은 `0x7FFF`.

<details>
<summary>답 보기</summary>

`p = (int32_t)a * b` (최대 2^30) → `r = sar32(p + (1 << 14), 15)` → `[INT16_MIN, INT16_MAX]` 로 포화. `p >> 15` 를 바로 쓰면 음수 p 에서 구현 정의. 모터 제어(FOC)·오디오 DSP 의 기본 연산이고 Cortex-M4 는 `SMULBB`/`QDADD` 등 DSP 명령이 있다. `(-32768)×(-32768) >> 15 = 32768` 은 int16 에 안 들어가서 포화가 필요하다.

</details>

### 연습 5 — 판정 퀴즈

정의 / 구현 정의 / UB 로 분류하라 (int 32비트): ① `(uint8_t)0xAB << 8` ② `(uint16_t)0x8001 << 16` ③ `(uint32_t)(uint16_t)0x8001 << 16` ④ `0xFFFFFFFFu >> 31` ⑤ `-8 >> 1` ⑥ `1u << -1` ⑦ `INT32_MAX << 1` ⑧ `x << 31` (x 는 uint32_t)

<details>
<summary>답 보기</summary>

① 정의 (`0xAB00`) ② UB (int 로 승격 → `0x80010000` 이 int 범위 초과) ③ 정의 (`0x80010000u`) ④ 정의 (`1`) ⑤ 구현 정의 (GCC/clang: -4) ⑥ UB (시프트 양 음수 — 왼쪽이 unsigned 여도) ⑦ UB (signed 오버플로) ⑧ 정의 (unsigned, 모듈러). practice.c 는 "정의" 항목만 실행해 값을 확인한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
```

## 10. 핵심 정리

- 결과 타입 = **승격된 왼쪽** 피연산자. 시프트 양이 음수이거나 그 폭 이상이면 UB (`x << 32`, `x << -1`).
- signed `<<` 는 음수이거나 넘치면 UB (`1 << 31`, `-1 << 1`, `(uint8_t)0x80 << 24`). 음수 `>>` 는 구현 정의.
- 하드웨어: x86·arm64 32비트 시프트는 `n & 31`, Cortex-M 은 `n & 0xFF` 후 ≥32 면 0 → 같은 코드가 칩마다 다른 값. 최적화기는 UB 경로를 지운다.
- 안전한 시프트: **시프트 전에** `n >= 32 ? 0 : x << n`. 비용은 cmp + csel/it 한두 개.
- 이식 가능한 산술 시프트: `x < 0 ? ~(~x >> n) : x >> n` (컴파일러가 `asr` 하나로 만든다). `x >> 1` ≠ `x / 2` (음수).
- 회전: `(x << (n & 31)) | (x >> (-n & 31))` → `ror` 한 개.
- 캐스트는 결과 비트 위치보다 넓게: 64비트 조립은 `(uint64_t)b << 32`, 64비트 비트는 `UINT64_C(1) << n` (`1UL` 은 ILP32 에서 32비트).
- 확인 도구: `-fsanitize=undefined` (shift-exponent / shift-base), 타깃은 `-fsanitize-trap`, 경고 `-Wshift-sign-overflow`.

## 참고자료

- [ISO C11 최종 초안 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) — 6.5.7 Bitwise shift operators, 6.3.1.3 Signed and unsigned integers, 6.5.5 나눗셈 자르기, 7.20.1.1 Exact-width integer types, J.3.5 구현 정의 목록
- [GCC 매뉴얼 — Integers implementation](https://gcc.gnu.org/onlinedocs/gcc/Integers-implementation.html) — 음수 `>>` 는 sign extension, signed `<<` 확장 동작
- [Clang UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — `-fsanitize=shift-base/shift-exponent`, `-fsanitize-trap`
- [Arm ARMv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest/) — LSL/LSR/ASR (register): Rm[7:0] 사용
- [Arm A64 LSLV](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/LSLV--Logical-Shift-Left-Variable-) — shift amount = Rm modulo datasize
- [Intel SDM Vol. 2 — SAL/SAR/SHL/SHR](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — count masked to 5 bits (64-bit: 6 bits)
- [Linux include/linux/bits.h — GENMASK](https://github.com/torvalds/linux/blob/master/include/linux/bits.h)
- [P1236R1 — Alternative wording for P0907R4 Signed Integers are Two's Complement (C++20)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1236r1.html)

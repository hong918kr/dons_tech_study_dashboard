# N05A · 비트 기본
<!-- theme: pastel -->

> **묶음**: A 비트 기본 · **문제 수**: 9 (★★★ 3 · ★★ 3 · ★ 3) · **목표**: 비트 뒤집기·popcount·정렬·위치·rotate·sign-extend·필드 추출을 **종이 20분 안에 UB 없이** · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · **이전/다음**: 이전 없음 (목록이 첫 페이지) / [N05B 비트 응용·엔디안·패킹](2026-11-01_N05B_bits_endian_packing.html) · **코드**: onsitePrep/n05/a/ (`./run.sh` → A1–A9 PASS)

## 0. TL;DR · 사용법

- 이 페이지가 훈련하는 것: 스크린 질문(니블 뒤집기)과 **같은 계열의 "한 줄 비트 연산"**을 말로 설명하면서 쓰는 습관. 공식 자체는 [C 정리 C6·C7](2026-10-29_c_basics_cram.html)에 있으니, 여기선 **종이에서 틀리는 지점(UB·폭·엣지)**에 집중.
- 외울 한 줄 3개
  - 루프 push/pop: `r = (r << 1) | (x & 1u); x >>= 1;` — 니블이면 `<< 4`, `& 0xFu` ([N02 회고](2026-10-31_N02_screen_coding_retro.html)와 같은 골격).
  - sign-extend: `m = 1u << (bits-1); v = (x ^ m) - m;`
  - 안전한 mask: `width == 32 ? 0xFFFFFFFFu : (1u << width) - 1u` — `1u << 32` 는 UB.
- ★★★ (온사이트 확률 높음): **A1 비트 뒤집기 · A6 sign-extend · A8 필드 추출/삽입**. 시간이 없으면 이 셋만.
- 사용법: §1 문제지를 인쇄 → 타이머 20분(★★ 15분, ★ 10분) → 종이에 풀기 → 문제별 `<details>`를 힌트 1 → 힌트 2 → 모범 답 → trace → 테스트 순으로 열어 채점.
- 모든 문제 공통 루틴(clarify → 예시 → 코드 → trace → 테스트·확장)은 [N05 목록 §1](2026-11-01_N05_c_paper_drill_bank.html).
- 채점 공통: `1u`/`0x...u` 접미사, 시프트 양 `< 32`, 0과 all-ones 엣지, trace 단위 고정(비트면 비트, 니블이면 니블).

## 1. 문제지 (인쇄용 — 답 없음)

### A1 · Reverse the bit order of a 32-bit word ★★★

Reverse the order of the 32 bits of `x` (bit 0 becomes bit 31). Write a simple loop version first, then an O(log n) version with no loop.

```c
uint32_t rev_bits32_loop(uint32_t x);
uint32_t rev_bits32_fast(uint32_t x);
```

Example: `0x00000001` → `0x80000000`, `0x12345678` → `0x1E6A2C48`.

### A2 · Popcount three ways + parity ★★

Count the set bits of a 32-bit value three ways (bit-by-bit, clear-lowest-bit, parallel SWAR). Then return the parity (1 if the number of set bits is odd).

```c
unsigned popcnt_naive(uint32_t x);
unsigned popcnt_kernighan(uint32_t x);
unsigned popcnt_swar(uint32_t x);
unsigned parity32(uint32_t x);
```

Example: `0x12345678` → popcount 13, parity 1.

### A3 · Power of two, next power of two, align up/down ★★

Test whether `x` is a power of two; return the smallest power of two `>= x`; align an address down/up to a power-of-two boundary `a`.

```c
bool     is_pow2(uint32_t x);
uint32_t next_pow2(uint32_t x);
uint32_t align_down(uint32_t x, uint32_t a);
uint32_t align_up(uint32_t x, uint32_t a);
```

Example: `next_pow2(1000)` → `1024`, `align_up(0x40001001, 0x1000)` → `0x40002000`.

### A4 · Highest / lowest set bit index, clz without builtins ★★

Return the index of the most significant and least significant set bit (or -1 for 0), and count leading zeros — without `__builtin_clz`.

```c
int      msb_index(uint32_t x);
int      lsb_index(uint32_t x);
unsigned clz32(uint32_t x);
```

Example: `x = 0x00012345` → msb 16, lsb 0, clz 15.

### A5 · 32-bit rotate left / right ★

Rotate `x` left/right by `n` bits. `n` can be any value including 0 and `>= 32`.

```c
uint32_t rotl32(uint32_t x, unsigned n);
uint32_t rotr32(uint32_t x, unsigned n);
```

Example: `rotl32(0x12345678, 4)` → `0x23456781`.

### A6 · Sign-extend an N-bit two's-complement field ★★★

A 12-bit ADC returns a two's-complement sample in the low bits of a 32-bit register (upper bits are garbage). Convert the low `bits` bits of `x` to a signed `int32_t`. Support any `bits` from 1 to 32.

```c
int32_t sign_extend32(uint32_t x, unsigned bits);
```

Example: `sign_extend32(0x800, 12)` → `-2048`, `sign_extend32(0xABCDEFFF, 12)` → `-1`.

### A7 · Swap odd/even bits; swap nibbles inside each byte ★

Swap every even bit with the odd bit next to it (bit 0 with bit 1, bit 2 with bit 3, …). Separately, swap the two nibbles inside every byte of a 32-bit word, and inside a single byte.

```c
uint32_t swap_odd_even(uint32_t x);
uint32_t swap_nibbles_in_bytes(uint32_t x);
uint8_t  swap_nibbles8(uint8_t b);
```

Example: `swap_odd_even(0xAAAAAAAA)` → `0x55555555`, `swap_nibbles_in_bytes(0x12345678)` → `0x21436587`.

### A8 · Extract / insert an arbitrary (pos, width) field ★★★

Read and write the bit field `[pos+width-1 : pos]` of a 32-bit register value without touching other bits. `width` may be 32. Reject invalid `(pos, width)` safely.

```c
uint32_t field_mask(unsigned pos, unsigned width);
uint32_t field_get(uint32_t reg, unsigned pos, unsigned width);
uint32_t field_set(uint32_t reg, unsigned pos, unsigned width, uint32_t val);
```

Example: `field_get(0xDEADBEEF, 4, 12)` → `0xBEE`, `field_set(0xDEADBEEF, 28, 4, 0x1)` → `0x1EADBEEF`.

### A9 · Bit set/clear/toggle/test macros + longest run of 1s ★

Write macros to set, clear, toggle and test bit `n` of a 32-bit variable. Then return the length of the longest run of consecutive 1 bits.

```c
#define BIT_SET(x, n)    /* ... */
#define BIT_CLEAR(x, n)  /* ... */
#define BIT_TOGGLE(x, n) /* ... */
#define BIT_TEST(x, n)   /* ... evaluates to 0 or 1 */
unsigned max_consecutive_ones(uint32_t x);
```

Example: `max_consecutive_ones(0x6E)` (binary `0110 1110`) → `3`.

## A1 · 32-bit 비트 순서 뒤집기 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 32비트 값의 비트 순서를 거꾸로 (bit 0 ↔ bit 31, bit 1 ↔ bit 30 …). 루프 버전 + 루프 없는 빠른 버전 두 개.
- **상황**: 스크린에서 나온 "니블 순서 뒤집기"의 **비트 단위 버전**. SPI/I2S처럼 LSB-first ↔ MSB-first 로 비트 순서가 다른 장치, CRC의 reflected 입력/출력, FFT bit-reversal 주소 등에서 실제로 쓴다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `rev_bits32_loop` | 비트 하나씩 꺼내 반대쪽에 쌓기 | 정확히 32번 반복 (앞쪽 0도 옮겨야 함) |
| `rev_bits32_fast` | 분기·루프 없이 고정 단계로 뒤집기 | 마스크 상수는 unsigned |

- **예시를 말로 풀면**: `0x00000001`은 맨 아래 비트 하나만 1 → 뒤집으면 맨 위 비트만 1인 `0x80000000`. `0x12345678` → `0x1E6A2C48`.
- **지켜야 할 조건**: 입력은 그냥 `uint32_t` 값(버퍼 아님). 앞쪽 0비트도 자리를 차지한다는 점(`0x1`은 `0x1`이 아님).

### 1. Problem (English)

"Given a 32-bit unsigned integer, return it with its bits in reverse order. Start with whatever is simplest, then tell me how you'd make it faster."

```c
uint32_t rev_bits32_loop(uint32_t x);
uint32_t rev_bits32_fast(uint32_t x);
```

- `0x00000001` → `0x80000000`
- `0x0000000F` → `0xF0000000`
- `0x12345678` → `0x1E6A2C48`

### 2. 먼저 물어볼 것 (clarify)

- "Is the input always exactly 32 bits, so leading zeros count as bits?" → Yes, fixed 32-bit width.
- "Integer in a register, or a byte buffer in memory?" → Integer. (Buffer version is a follow-up.)
- "Do you care about speed, or is a clear loop fine first?" → Loop first, then optimize.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (폭, 값 vs 버퍼) |
| 2–5 | 4비트 예 `1011 → 1101` 손으로, 접근 말하기 |
| 5–15 | 루프 버전 → mask-swap 버전 |
| 15–18 | 4비트 루프 trace 또는 mask-swap 2단계 trace |
| 18–20 | 테스트 4개 + LUT/`rbit` 확장 언급 |

- 소리 내서: "I pop the lowest bit of x and push it into the result from the right; after 32 steps the first bit I pushed has travelled to bit 31."
- 소리 내서: "The fast version swaps neighbours at every scale — 1-bit pairs, 2-bit pairs, nibbles, bytes, halves — five steps, constant time."

<details><summary>힌트 1 (막히면 5분 후)</summary>

N02 니블 뒤집기의 루프 버전에서 `4`를 `1`로, `0xF`를 `1`로 바꾸면 된다. 반복 횟수는 64/4=16 이 아니라 32/1=32.

</details>

<details><summary>힌트 2</summary>

빠른 버전: "인접한 1비트끼리 swap → 인접한 2비트 묶음끼리 swap → 4비트 → 8비트 → 16비트". 각 단계의 마스크는 `0x55555555`, `0x33333333`, `0x0F0F0F0F`, `0x00FF00FF`. 마지막 16비트 swap은 그냥 rotate.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* Loop: pop the low bit of x, push it into r. 32 iterations, always. */
uint32_t rev_bits32_loop(uint32_t x)
{
    uint32_t r = 0;
    for (unsigned i = 0; i < 32u; i++) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }
    return r;
}

/* Mask-swap: swap adjacent 1-, 2-, 4-, 8-, 16-bit groups. 5 steps, no branch. */
uint32_t rev_bits32_fast(uint32_t x)
{
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    x = (x >> 16) | (x << 16);
    return x;
}
```

- 시간/공간: 루프 O(32) = O(w), mask-swap O(log w) = 5단계 고정 · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

루프 버전, 4비트로 축소한 예 `x = 1011b (0xB)`, 4번 반복:

| i | x & 1 | r (이진) | x 남은 것 |
|---|---|---|---|
| 0 | 1 | 1 | 101 |
| 1 | 1 | 11 | 10 |
| 2 | 0 | 110 | 1 |
| 3 | 1 | 1101 | 0 |

mask-swap 버전, `x = 0x12345678`:

| 단계 | 마스크 | 결과 |
|---|---|---|
| 1비트 swap | `0x55555555` | `0x2138A9B4` |
| 2비트 swap | `0x33333333` | `0x84C2A6E1` |
| 4비트 swap | `0x0F0F0F0F` | `0x482C6A1E` |
| 8비트 swap | `0x00FF00FF` | `0x2C481E6A` |
| 16비트 swap | rotate 16 | `0x1E6A2C48` |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `0x12345678` → `0x1E6A2C48`.
- 0: `0` → `0`. all-ones: `0xFFFFFFFF` → `0xFFFFFFFF`.
- 경계: `0x1` → `0x80000000`, `0x80000000` → `0x1` (최상위 비트가 시프트로 잘려 나가지 않는지).
- round-trip: `rev(rev(x)) == x` (랜덤 10만 개로 확인함).
- 교차 검증: loop 결과 == fast 결과 (랜덤 10만 개).

</details>

### 4. 꼬리 질문

- "Can you do it even faster, or with less code?"

<details><summary>답</summary>

A 16-entry nibble lookup table (or a 256-entry byte table) reverses each chunk, then you place the chunks in reverse order — 8 lookups for 32 bits. On ARM there is a single instruction, `RBIT`, which clang emits for `__builtin_bitreverse32`; it exists from Cortex-M3 (ARMv7-M) up, not on M0.
한국어 핵심: LUT(니블 16칸)로 8번 조회, ARM은 `RBIT` 한 명령.

</details>

- "What about 64-bit, or a byte buffer?"

<details><summary>답</summary>

For 64-bit, same five masks widened to `ull` plus one more 32-bit swap stage — six steps. For a buffer, reverse the bits inside each byte and reverse the byte order, exactly like the nibble buffer version in N02, but with a per-byte bit reverse instead of a nibble swap.
한국어 핵심: 64비트는 6단계, 버퍼는 "바이트 순서 뒤집기 + 바이트 안 비트 뒤집기" ([N02 §2.C](2026-10-31_N02_screen_coding_retro.html)).

</details>

- "Why do you write `0x55555555u` with a `u`?"

<details><summary>답</summary>

`0x55555555` happens to fit in `int`, but `0xAAAAAAAA` does not and silently becomes unsigned, so mixing is confusing. Writing every mask unsigned keeps all the arithmetic in `uint32_t` and avoids any signed-shift questions.
한국어 핵심: 마스크·리터럴은 전부 `u` — 부호 있는 시프트 논쟁 원천 차단.

</details>

### 5. 채점 · 자주 틀리는 것

- 반복 횟수를 "x가 0이 될 때까지"로 하면 앞쪽 0이 안 옮겨짐 → `0x1`이 `0x1`로 나옴. **고정 32번**.
- `r << 1` 을 `x` 를 꺼내기 **전에** 해야 함 (마지막에 한 번 더 시프트하면 1비트 밀림).
- mask-swap에서 `(x >> k) & m` 과 `(x & m) << k` — 마스크를 시프트 전에/후에 거는 위치 혼동.
- 마지막 16비트 단계에 마스크를 붙여도 맞지만, 붙이지 않는 rotate가 더 간단.
- 니블 뒤집기(N02)와 혼동해 `& 0xF` 를 쓰는 실수 — trace 단위를 비트로 고정.

## A2 · popcount 3가지 + parity (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 32비트 값에서 1인 비트 개수 세기를 3가지 방법으로, 그리고 1의 개수가 홀수인지(parity) 구하기.
- **상황**: validation에서 **비트 에러 수(BER)** = `popcount(expected ^ actual)`, ECC/패리티 비트 검사, 인터럽트 status에 몇 개가 동시에 떴는지 셀 때. 면접관은 "더 빠르게?"로 2→3번 방법을 끌어낸다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `popcnt_naive` | 비트 하나씩 검사 | 이해하기 쉬운 기준 답 |
| `popcnt_kernighan` | 가장 낮은 1을 하나씩 지우며 세기 | 반복 횟수 = 1의 개수 |
| `popcnt_swar` | 분기 없이 병렬로 합치기 | 고정 단계, 곱셈 1회 허용 |
| `parity32` | 1의 개수가 홀수면 1 | popcount 없이 |

- **예시를 말로 풀면**: `0x12345678` = 1,2,3,4,5,6,7,8 니블의 1 개수 합 = 1+1+2+1+2+2+3+1 = 13 → parity 1.
- **지켜야 할 조건**: 0 입력, all-ones(32) 처리. 라이브러리/builtin 쓰지 말 것 (꼬리 질문에서 언급은 OK).

### 1. Problem (English)

"Count the number of set bits in a 32-bit word. Give me the obvious way, then a faster way. Then compute parity."

```c
unsigned popcnt_naive(uint32_t x);
unsigned popcnt_kernighan(uint32_t x);
unsigned popcnt_swar(uint32_t x);
unsigned parity32(uint32_t x);
```

- `0x00000000` → 0, parity 0
- `0x0000F0F0` → 8, parity 0
- `0x12345678` → 13, parity 1

### 2. 먼저 물어볼 것 (clarify)

- "Is a hardware popcount instruction or compiler builtin allowed?" → No, write it yourself.
- "Is the input sparse (few bits set) or dense?" → Unknown — that's why we compare methods.
- "Parity: odd parity = 1 when the count is odd?" → Yes.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (builtin 금지, 희소/밀집) |
| 2–5 | `0xF0F0` 손으로 8, 세 방법 이름만 먼저 |
| 5–12 | naive → Kernighan → SWAR |
| 12–14 | parity XOR fold |
| 14–15 | 테스트 + `__builtin_popcount`/`CNT` 언급 |

- 소리 내서: "`x & (x - 1)` clears the lowest set bit, so this loop runs once per set bit — great when the word is sparse, like an error mask."
- 소리 내서: "Parity is just XOR of all bits; I fold the word in half five times so bit 0 ends up holding the XOR of everything."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`x - 1`은 가장 낮은 1을 0으로, 그 아래 0들을 1로 바꾼다. 그래서 `x & (x - 1)`은 가장 낮은 1 하나만 지운다. parity는 `x ^= x >> 16` 부터 시작.

</details>

<details><summary>힌트 2</summary>

SWAR: 2비트 칸마다 개수(0–2) → 4비트 칸마다(0–4) → 8비트 칸마다(0–8) → 4바이트 합. 첫 단계 공식은 `x - ((x >> 1) & 0x55555555)` (2비트 `ab` 의 개수 = `ab - a`). 마지막은 `* 0x01010101` 후 `>> 24` 로 바이트 합이 최상위 바이트에 모인다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* 1) Naive: test every bit. Stops after the top set bit (<= 32 iterations). */
unsigned popcnt_naive(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        n += x & 1u;
        x >>= 1;
    }
    return n;
}

/* 2) Kernighan: x & (x-1) clears the lowest set bit. Iterations = number of 1s. */
unsigned popcnt_kernighan(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        x &= x - 1u;
        n++;
    }
    return n;
}

/* 3) SWAR: add bits in parallel inside 2-, 4-, 8-bit lanes, then sum bytes. */
unsigned popcnt_swar(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);                  /* 2-bit counts 0..2 */
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);  /* 4-bit counts 0..4 */
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;                  /* 8-bit counts 0..8 */
    return (x * 0x01010101u) >> 24;                    /* top byte = sum    */
}

/* Parity: 1 if odd number of 1s. XOR-fold halves down to bit 0. */
unsigned parity32(uint32_t x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1u;
}
```

- 시간/공간: naive O(최상위 1 위치) ≤ 32 · Kernighan O(popcount) · SWAR O(1) (5–6 연산) · parity O(log w) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

SWAR, `x = 0x12345678`:

| 단계 | 의미 | 값 |
|---|---|---|
| 2비트 | 각 2비트 칸의 1 개수 | `0x11245564` |
| 4비트 | 각 니블의 1 개수 | `0x11212231` (니블 1,1,2,1,2,2,3,1) |
| 8비트 | 각 바이트의 1 개수 | `0x02030404` |
| 곱셈 | `* 0x01010101` → 최상위 바이트 | `0x0D0B0804` → `0x0D` = 13 |

parity, `x = 0x12345678`: `^>>16` → `0x1234444C`, `^>>8` → `0x12267008`, `^>>4` → `0x13041708`, `^>>2`, `^>>1` → 최종 bit 0 = **1** (13은 홀수).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `0x12345678` → 13, `0xF0F0` → 8.
- 0 → 0 (루프가 한 번도 안 돎), parity 0.
- all-ones `0xFFFFFFFF` → 32 (SWAR의 8비트 칸 최대 8, 합 32가 한 바이트에 들어감), parity 0.
- 경계: `0x80000000` → 1 (최상위 비트), `0x1` → 1.
- 교차 검증: 세 방법 == `__builtin_popcount` (테스트 코드에서만, 랜덤 10만 개).

</details>

### 4. 꼬리 질문

- "Count bit errors between an expected and a received 4 KB buffer."

<details><summary>답</summary>

XOR each 32-bit word of the two buffers and add the popcount of the result; that's the bit error count, and dividing by total bits gives the BER. Use byte loads or `memcpy` into a `uint32_t` if the buffers might be unaligned.
한국어 핵심: `err += popcount(a[i] ^ b[i])` — 버퍼 비교는 [N05D D2](2026-11-01_N05D_buffers_strings_memory.html).

</details>

- "What does the hardware give you?"

<details><summary>답</summary>

x86 has `POPCNT`; AArch64 has `CNT` on a NEON register plus an add-across; Cortex-M has nothing, so the compiler's `__builtin_popcount` expands to roughly the SWAR sequence. If it's in a hot loop on an M-class core, a 256-entry byte table is another option.
한국어 핵심: x86 `POPCNT`, ARMv8 `CNT`, Cortex-M은 SWAR/LUT.

</details>

- "Why does the SWAR first step use subtraction?"

<details><summary>답</summary>

For a 2-bit field `ab`, the value is `2a + b` and the count is `a + b`, so count = value − a, which is `x - ((x >> 1) & 0x55555555)`. It can't borrow across fields because value ≥ a in every field.
한국어 핵심: 2비트 칸 `2a+b - a = a+b`, 칸 사이 borrow 없음.

</details>

### 5. 채점 · 자주 틀리는 것

- naive에서 `int` 로 받아 음수 시프트(부호 확장으로 무한 루프) — 반드시 `uint32_t`.
- Kernighan을 `x & (x + 1)` 이나 `x | (x - 1)` 로 헷갈리기.
- SWAR 4비트 단계에서 마스크를 한쪽에만 걸기 — 두 피연산자 모두 `& 0x33333333`.
- parity를 `popcount % 2` 로만 답하고 XOR fold를 모르는 것 (꼬리 질문에서 나옴).

## A3 · power of 2 판별 · 다음 2의 거듭제곱 · align up/down (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: (1) 값이 2의 거듭제곱인지, (2) x 이상인 가장 작은 2의 거듭제곱, (3) 주소를 2의 거듭제곱 경계로 내림/올림.
- **상황**: DMA 버퍼·캐시 라인·페이지(4 KB) 정렬, 링버퍼 크기 검사(2의 거듭제곱이어야 `& (N-1)` 가능), PCIe BAR 크기(항상 2의 거듭제곱, 자기 크기에 정렬 — [LP04](2026-10-31_LP04_enumeration_resources_hotplug.html)).

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `is_pow2` | 1비트만 set인지 | 0은 false |
| `next_pow2` | x 이상 최소 2^k | 0 → 1, 2^31 초과 → 0 (표현 불가) |
| `align_down` | a의 배수로 내림 | a는 2의 거듭제곱 |
| `align_up` | a의 배수로 올림 | 이미 정렬된 값은 그대로 |

- **예시를 말로 풀면**: 1000 → 1024. 주소 `0x40001001`을 4 KB로 올림 → `0x40002000`, 내림 → `0x40001000`.
- **지켜야 할 조건**: 루프 없이(또는 고정 5단계). 경계값(0, 1, 2^31, UINT32_MAX)에서 동작을 정해서 말하기.

### 1. Problem (English)

"Write helpers I can use in a DMA driver: is-power-of-two, round up to the next power of two, and align an address up or down to a power-of-two boundary."

```c
bool     is_pow2(uint32_t x);
uint32_t next_pow2(uint32_t x);
uint32_t align_down(uint32_t x, uint32_t a);
uint32_t align_up(uint32_t x, uint32_t a);
```

- `is_pow2(64)` → true, `is_pow2(96)` → false, `is_pow2(0)` → false
- `next_pow2(5)` → 8, `next_pow2(8)` → 8, `next_pow2(0)` → 1
- `align_up(13, 8)` → 16, `align_down(0x40001FFF, 0x1000)` → `0x40001000`

### 2. 먼저 물어볼 것 (clarify)

- "Is `a` guaranteed to be a power of two?" → Yes (otherwise the mask trick is wrong; I'd assert it).
- "What should `next_pow2` return when the answer doesn't fit in 32 bits?" → Return 0 as an error.
- "Is 0 a power of two for your use?" → No.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (a가 2^k, overflow 반환값) |
| 2–4 | `64 = 1000000b`, `63 = 0111111b` → AND = 0 말하기 |
| 4–11 | 세 함수 |
| 11–13 | `next_pow2(1000)` 비트 채우기 trace |
| 13–15 | 테스트 + align_up overflow 언급 |

- 소리 내서: "A power of two has exactly one bit set; subtracting one turns that bit off and every bit below it on, so the AND is zero."
- 소리 내서: "For next power of two I decrement first so an exact power maps to itself, smear the top bit downward, then add one."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`a - 1`은 하위 k비트가 전부 1인 마스크. `~(a - 1)`로 AND하면 하위 비트를 버림 = 내림. 올림은 먼저 `a - 1`을 더하고 내림.

</details>

<details><summary>힌트 2</summary>

next_pow2: `x--` 후 `x |= x >> 1; >> 2; >> 4; >> 8; >> 16` 이면 최상위 1 아래가 전부 1이 된다 → `+1`. `x--`를 빼먹으면 8 → 16 이 된다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdbool.h>
#include <stdint.h>

/* Exactly one bit set. 0 is NOT a power of two. */
bool is_pow2(uint32_t x)
{
    return x != 0u && (x & (x - 1u)) == 0u;
}

/* Smallest power of two >= x. 0 -> 1. Returns 0 if x > 2^31 (no fit). */
uint32_t next_pow2(uint32_t x)
{
    if (x <= 1u)
        return 1u;
    if (x > 0x80000000u)
        return 0u;
    x--;                 /* so an exact power of two maps to itself */
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;        /* now every bit below the top 1 is set */
    return x + 1u;
}

/* a must be a power of two. align_up may wrap to 0 near UINT32_MAX. */
uint32_t align_down(uint32_t x, uint32_t a) { return x & ~(a - 1u); }
uint32_t align_up(uint32_t x, uint32_t a)   { return (x + a - 1u) & ~(a - 1u); }
```

- 시간/공간: 전부 O(1) (next_pow2는 고정 5단계) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`next_pow2(1000)`:

| 단계 | x (hex) | 설명 |
|---|---|---|
| `x--` | `0x3E7` (999) | 정확한 2^k 대비 |
| `x or= x >> 1` | `0x3F7` | |
| `x or= x >> 2` | `0x3FF` | 이미 bit 9 아래 전부 1 |
| `>> 4, 8, 16` | `0x3FF` | 변화 없음 |
| `x + 1` | `0x400` = 1024 | |

`align_up(13, 8)`: `13 + 7 = 20 = 10100b` → `& ~7 (…11000b)` → `10000b = 16`.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `next_pow2(1000)` → 1024, `align_up(0x40001001, 0x1000)` → `0x40002000`.
- 0: `is_pow2(0)` false, `next_pow2(0)` → 1, `align_up(0, 4096)` → 0.
- 이미 정렬/정확한 2^k: `next_pow2(8)` → 8, `align_up(16, 8)` → 16, `align_up(5, 1)` → 5.
- 경계: `next_pow2(0x80000000)` → `0x80000000`, `next_pow2(0x80000001)` → 0 (에러), `align_up(0xFFFFFFF9, 8)` → 0 (wrap — 문서화된 동작).
- 성질 검사: 2..70000 모든 x에 대해 결과가 2^k 이고 `p >= x` 이고 `p/2 < x`.

</details>

### 4. 꼬리 질문

- "What happens if `align_up` overflows? How would you make it safe?"

<details><summary>답</summary>

`x + a - 1` wraps past `UINT32_MAX` and the result becomes a small number, which in a DMA driver means programming a buffer at address 0. I'd check `x > UINT32_MAX - (a - 1)` first and return an error, or do the math in 64 bits.
한국어 핵심: wrap → 주소 0 으로 DMA. 미리 `x > UINT32_MAX - (a-1)` 검사.

</details>

- "How do you check that a pointer is 64-byte aligned?"

<details><summary>답</summary>

Cast to `uintptr_t` and test `(p & 63u) == 0`. Never do bit math on the pointer type itself; `uintptr_t` is the integer type guaranteed to round-trip a pointer.
한국어 핵심: `((uintptr_t)p & (64u - 1u)) == 0`.

</details>

- "How would you get next_pow2 with clz?"

<details><summary>답</summary>

For `x > 1`, it's `1u << (32 - clz32(x - 1))`, guarding the case where that shift would be 32. That's one instruction on ARM (`CLZ`) instead of five shift-ORs.
한국어 핵심: `1u << (32 - clz(x-1))`, 시프트 32 되는 경우 가드 (A4와 연결).

</details>

### 5. 채점 · 자주 틀리는 것

- `is_pow2`에서 `x != 0` 빠뜨리기 → 0이 true.
- `next_pow2`에서 `x--` 빠뜨리기 → 정확한 2^k가 두 배가 됨.
- `align_up`에 2의 거듭제곱 아닌 a를 넣어도 조용히 틀린 답 — 전제 조건을 말로 하거나 assert.
- `~(a - 1)` 대신 `~a - 1` 같은 괄호 실수.
- 32비트 주소에 64비트 `uintptr_t` 섞을 때 `~(a - 1u)` 가 `uint32_t` 폭이면 상위 32비트가 0으로 지워짐 — 64비트에선 마스크 타입을 맞출 것.

## A4 · 최상위/최하위 set bit 위치 · clz (builtin 없이) (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 1인 비트 중 가장 높은 것의 위치(msb), 가장 낮은 것의 위치(lsb), 그리고 앞쪽 0의 개수(clz)를 builtin 없이.
- **상황**: 인터럽트 status 레지스터에서 **가장 우선순위 높은/낮은 pending 비트** 고르기, 우선순위 비트맵 스케줄러(RTOS ready list), log2(크기) 계산(BAR 크기 → 비트 수), 부동소수 정규화.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `msb_index` | 최상위 1의 인덱스 0..31 | x == 0 → -1 |
| `lsb_index` | 최하위 1의 인덱스 0..31 | x == 0 → -1 |
| `clz32` | 앞쪽 0 개수 | x == 0 → 32 |

- **예시를 말로 풀면**: `0x00012345` = 이진으로 bit 16이 최상위 1 → msb 16, 앞쪽 0이 15개 → clz 15, bit 0이 1 → lsb 0. `0x68 = 0110 1000b` → lsb 3.
- **지켜야 할 조건**: 32번 루프보다 나은 방법(이진 탐색) 제시. `__builtin_clz(0)` 은 정의되지 않음 — 0 처리 명시.

### 1. Problem (English)

"Find the position of the highest set bit and the lowest set bit in a 32-bit word, and implement count-leading-zeros, without compiler builtins."

```c
int      msb_index(uint32_t x);
int      lsb_index(uint32_t x);
unsigned clz32(uint32_t x);
```

- `0x00000001` → msb 0, lsb 0, clz 31
- `0x80000000` → msb 31, lsb 31, clz 0
- `0x00000000` → msb -1, lsb -1, clz 32

### 2. 먼저 물어볼 것 (clarify)

- "What should I return for zero?" → -1 for positions, 32 for clz.
- "Bit 0 is the least significant bit?" → Yes.
- "Is a 32-iteration loop acceptable, or do you want something logarithmic?" → Start with the loop idea, write the binary search.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (0 반환값, bit 0 = LSB) |
| 2–4 | 루프 방법 말로만 → 이진 탐색 제안 |
| 4–11 | msb 이진 탐색 → lsb = msb(x & -x) → clz = 31 - msb |
| 11–13 | `0x00012345` trace |
| 13–15 | 테스트 + `CLZ`/`RBIT` 명령 언급 |

- 소리 내서: "I binary-search the top bit: if anything is set in the upper 16 bits, the answer is at least 16 and I shift down; then 8, 4, 2, 1."
- 소리 내서: "`x & -x` isolates the lowest set bit, so the lowest index is just the highest index of that single-bit value."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`if (x >> 16) { n += 16; x >>= 16; }` 을 16, 8, 4, 2, 1 순서로. 마지막 1 단계는 시프트 안 해도 됨.

</details>

<details><summary>힌트 2</summary>

lsb: `x & (~x + 1u)` 는 최하위 1만 남긴다 (unsigned에서 `-x` 와 같음). 그 값의 msb가 곧 lsb. clz = `31 - msb`, 0일 때 `31 - (-1) = 32` 로 자연스럽게 맞음.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* Index of highest set bit (0..31), -1 if x == 0. Binary search: 5 steps. */
int msb_index(uint32_t x)
{
    if (x == 0u)
        return -1;
    int n = 0;
    if (x >> 16) { n += 16; x >>= 16; }
    if (x >> 8)  { n += 8;  x >>= 8;  }
    if (x >> 4)  { n += 4;  x >>= 4;  }
    if (x >> 2)  { n += 2;  x >>= 2;  }
    if (x >> 1)  { n += 1; }
    return n;
}

/* Index of lowest set bit, -1 if x == 0. Isolate it with x & -x, then msb. */
int lsb_index(uint32_t x)
{
    return msb_index(x & (~x + 1u));     /* ~x + 1u == -x in unsigned */
}

/* Count leading zeros. clz32(0) == 32 by definition here (the builtin is UB at 0). */
unsigned clz32(uint32_t x)
{
    return (unsigned)(31 - msb_index(x));   /* x == 0: 31 - (-1) = 32 */
}
```

- 시간/공간: O(log w) = 5번 비교 · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`msb_index(0x00012345)`:

| 단계 | 검사 | n | x |
|---|---|---|---|
| 16 | `x >> 16 = 0x1` ≠ 0 | 16 | `0x1` |
| 8 | `0x1 >> 8 = 0` | 16 | `0x1` |
| 4, 2 | 0 | 16 | `0x1` |
| 1 | `0x1 >> 1 = 0` | **16** | |

`lsb_index(0x68)`: `0x68 & -0x68` = `0110 1000 & …1001 1000` = `0x08` → `msb_index(0x08)` = 3.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `0x00012345` → msb 16, clz 15. `0x68` → lsb 3.
- 0: msb -1, lsb -1, clz 32 (builtin과 달리 정의됨).
- 경계: `0x1` (msb 0, clz 31), `0x80000000` (msb 31, lsb 31, clz 0), `0xFFFFFFFF` (msb 31, lsb 0).
- 교차 검증: 랜덤 10만 개로 `__builtin_clz`/`__builtin_ctz` 와 비교 (0 제외).

</details>

### 4. 꼬리 질문

- "An interrupt status register has many pending bits. Service them lowest-first."

<details><summary>답</summary>

Loop while status is nonzero: take `i = lsb_index(status)`, call the handler for bit `i`, then clear it locally with `status &= status - 1`. This is exactly the ISR dispatch pattern in C5.
한국어 핵심: `while (s) { i = ctz(s); h[i](); s &= s - 1; }` → [N05C C5](2026-11-01_N05C_register_mmio_driver.html).

</details>

- "What instruction does the CPU give you?"

<details><summary>답</summary>

ARM has `CLZ` (ARMv5 and Cortex-M3+, not M0), and count-trailing-zeros is `RBIT` followed by `CLZ`. `__builtin_clz(0)` is undefined in GCC/Clang even though the ARM instruction returns 32, so you still guard zero in C.
한국어 핵심: ARM `CLZ`, ctz = `RBIT`+`CLZ`, builtin은 0에서 UB.

</details>

- "How do you compute floor(log2(x)) and ceil(log2(x))?"

<details><summary>답</summary>

Floor log2 is just `msb_index(x)`. Ceil log2 is `msb_index(x - 1) + 1` for `x > 1`, and 0 for `x == 1`. For example a 12 KB BAR needs ceil(log2) = 14 address bits.
한국어 핵심: floor = msb, ceil = `msb(x-1)+1`.

</details>

### 5. 채점 · 자주 틀리는 것

- x == 0 처리 빠뜨림 → 이진 탐색이 0을 반환해 bit 0과 구별 불가.
- `-x` 를 부호 있는 타입에서 쓰면 INT_MIN 오버플로 — unsigned에서 `~x + 1u`.
- 이진 탐색에서 `n += 16` 하고 `x >>= 16` 빠뜨리기.
- clz 와 msb 관계 `clz = 31 - msb` 를 `32 - msb` 로 쓰는 off-by-one.

## A5 · 32-bit rotate left/right (★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 비트를 왼쪽/오른쪽으로 n칸 회전 (밀려난 비트가 반대쪽으로 들어옴). n=0, n≥32도 안전하게.
- **상황**: 해시·CRC·LFSR·암호 코드, 그리고 **walking-1 패턴 생성**(메모리/버스 테스트에서 `rotl(pattern, 1)`)에 쓴다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `rotl32` | 왼쪽 회전 | n은 mod 32 |
| `rotr32` | 오른쪽 회전 | n은 mod 32 |

- **예시를 말로 풀면**: `0x12345678` 을 왼쪽으로 4비트(니블 하나) 회전 → 맨 앞 니블 1이 맨 뒤로 → `0x23456781`.
- **지켜야 할 조건**: 어떤 n에서도 32 이상 시프트(UB)가 생기지 않을 것.

### 1. Problem (English)

"Implement 32-bit rotate left and rotate right. `n` can be any unsigned value."

```c
uint32_t rotl32(uint32_t x, unsigned n);
uint32_t rotr32(uint32_t x, unsigned n);
```

- `rotl32(0x12345678, 4)` → `0x23456781`
- `rotr32(0x80000001, 1)` → `0xC0000000`
- `rotl32(0x12345678, 0)` → `0x12345678`, `rotl32(0x12345678, 36)` → `0x23456781`

### 2. 먼저 물어볼 것 (clarify)

- "Should `n >= 32` wrap modulo 32, or is it an error?" → Wrap modulo 32.
- "Is it always 32-bit?" → Yes; 64-bit is a follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (n 범위) |
| 2–3 | `x << n` 과 `x >> (32-n)` OR 말하기 |
| 3–7 | 코드 |
| 7–8 | n=0 에서 `x >> 32` UB 짚기 → `& 31` |
| 8–10 | 테스트 |

- 소리 내서: "The naive `x >> (32 - n)` is undefined when n is zero, because shifting by the full width is UB in C, so I mask the shift count with 31."
- 소리 내서: "Compilers recognize this exact pattern and emit a single `ROR` instruction."

<details><summary>힌트 1 (막히면 5분 후)</summary>

왼쪽 회전 = `(x << n) | (x >> (32 - n))`. 이걸 n=0 으로 trace 해 보라.

</details>

<details><summary>힌트 2</summary>

n=0 이면 `x >> 32` → C에서 시프트 양 ≥ 폭은 UB. 이 Mac(arm64, clang -O0) 실측은 `x >> 32` → x 그대로(시프트 양 mod 32), 32-bit ARM(A32)은 0 [미검증] — 플랫폼·최적화마다 다름. `(32 - n) & 31` 로 0이 되게.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* n is taken mod 32. (32 - n) & 31 keeps n == 0 from becoming x >> 32 (UB). */
uint32_t rotl32(uint32_t x, unsigned n)
{
    n &= 31u;
    return (x << n) | (x >> ((32u - n) & 31u));
}

uint32_t rotr32(uint32_t x, unsigned n)
{
    n &= 31u;
    return (x >> n) | (x << ((32u - n) & 31u));
}
```

- 시간/공간: O(1) · 공간 O(1). 이 Mac clang -O2 `-S` 확인: `rotr32` → `ror` 한 명령, `rotl32` → `neg` + `ror` (AArch64엔 rotate-left가 없음).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 호출 | n (mask 후) | 왼쪽 조각 | 오른쪽 조각 | 결과 |
|---|---|---|---|---|
| `rotl32(0x12345678, 4)` | 4 | `x << 4 = 0x23456780` | `x >> 28 = 0x1` | `0x23456781` |
| `rotl32(0x12345678, 0)` | 0 | `x << 0 = x` | `x >> 0 = x` (32 & 31 = 0) | `x` |
| `rotl32(0x12345678, 36)` | 4 | 위와 같음 | | `0x23456781` |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `rotl32(0x12345678, 4)` → `0x23456781`, `rotr32` → `0x81234567`.
- n=0, n=32 → 그대로 (UB 없음 — `-fsanitize=undefined` 로 확인).
- 경계: `rotl32(0x80000001, 1)` → `0x3` (최상위 비트가 bit 0으로).
- 0과 all-ones는 회전해도 그대로.
- round-trip: `rotr(rotl(x, n), n) == x`, n = 0..69. 그리고 `rotr(x, 31) == rotl(x, 1)`.

</details>

### 4. 꼬리 질문

- "Generate a walking-ones pattern for a 32-bit data bus test."

<details><summary>답</summary>

Start with `p = 1u` and do `p = rotl32(p, 1)` 32 times; each value has exactly one bit set and after 32 steps it's back to 1. A walking-zeros pattern is just `~p`.
한국어 핵심: `p = rotl32(p, 1)` 32번 = walking-1, `~p` = walking-0 → [N05F F1](2026-11-01_N05F_memtest_integrity.html).

</details>

- "What about a 64-bit rotate?"

<details><summary>답</summary>

Same shape with `uint64_t`, mask with 63, and `(64u - n) & 63u` for the other side. The important part is the operand type: `x` must already be 64-bit before the shift.
한국어 핵심: `& 63`, 피연산자가 시프트 전에 64비트.

</details>

### 5. 채점 · 자주 틀리는 것

- `x >> (32 - n)` 그대로 → n=0 에서 UB.
- `n &= 31` 빠뜨리기 → n=32 에서 UB.
- 부호 있는 `int x` 로 받기 → 오른쪽 시프트가 산술 시프트(부호 비트 복제)라 회전이 아님.
- rotl/rotr 방향 혼동 — 예시 하나로 즉시 확인.

## A6 · N-bit 2의 보수 필드 sign-extend (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 레지스터 하위 N비트에 들어 있는 "N비트짜리 부호 있는 값"을 정상적인 `int32_t` 로 바꾸기. 상위 비트는 쓰레기일 수 있음.
- **상황**: 12-bit ADC, 온도 센서(13-bit), IMU(16-bit), PCIe/센서 레지스터의 부호 있는 offset 필드처럼 **폭이 8/16/32가 아닌 부호 있는 값**은 validation 스크립트·펌웨어에서 매일 만난다. 틀리면 −1℃가 4095℃로 찍힌다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `sign_extend32` | 하위 bits 비트를 2의 보수로 해석해 int32 반환 | bits 1..32, 상위 쓰레기 무시, UB·구현 정의 동작 없이 |

- **예시를 말로 풀면**: 12비트에서 `0x7FF` = 가장 큰 양수 2047, `0x800` = 가장 작은 음수 −2048, `0xFFF` = −1. 레지스터가 `0xABCDEFFF` 를 줘도 하위 12비트만 보면 `0xFFF` → −1.
- **지켜야 할 조건**: `bits` 가 32인 경우, 1인 경우. 부호 있는 오버플로나 부호 있는 값의 왼쪽 시프트를 쓰지 말 것.

### 1. Problem (English)

"Our ADC gives a 12-bit two's-complement sample in the low bits of a 32-bit register; the upper bits are undefined. Write a function that returns the sample as a signed 32-bit integer. Make it work for any field width."

```c
int32_t sign_extend32(uint32_t x, unsigned bits);
```

- `sign_extend32(0x7FF, 12)` → `2047`
- `sign_extend32(0x800, 12)` → `-2048`
- `sign_extend32(0xABCDEFFF, 12)` → `-1`

### 2. 먼저 물어볼 것 (clarify)

- "Are the upper bits guaranteed zero, or can they be garbage?" → Garbage — mask first.
- "Is the field always at bit 0?" → Yes; if not, extract it first with A8's `field_get`.
- "Range of `bits`?" → 1 to 32.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (상위 쓰레기, 위치, 폭 범위) |
| 2–5 | `0x800`, `0xFFF`, `0x7FF` 를 손으로 −2048, −1, 2047 |
| 5–13 | mask → XOR → subtract, bits 32 분기 |
| 13–17 | `0x800`, `0xFFF` trace |
| 17–20 | 테스트 + shift 방식과 비교(꼬리) |

- 소리 내서: "Flipping the sign bit with XOR maps the signed range onto 0 to 2^N − 1 in order; subtracting 2^(N−1) shifts it back so the sign bit is worth minus 2^(N−1)."
- 소리 내서: "I avoid `(int32_t)(x << 20) >> 20` because right-shifting a negative int is implementation-defined and left-shifting into the sign bit is UB."

<details><summary>힌트 1 (막히면 5분 후)</summary>

2의 보수 N비트 값 = (하위 N−1비트) − (sign bit × 2^(N−1)). 먼저 `field = x & ((1u << bits) - 1u)`, 그리고 `m = 1u << (bits - 1)`.

</details>

<details><summary>힌트 2</summary>

`(field ^ m) - m`. sign bit가 0이면 XOR로 m이 더해졌다가 빼져서 그대로, 1이면 XOR로 m이 빠졌다가 한 번 더 빼져서 −2m 효과. 둘 다 `int32_t` 범위 안이 되도록 캐스트를 빼기 **전에**.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* Sign-extend the low `bits` bits (1..32) of x as an N-bit two's-complement value.
 * Bits above the field are ignored (masked off first). */
int32_t sign_extend32(uint32_t x, unsigned bits)
{
    if (bits == 0u || bits >= 32u)
        return (int32_t)x;           /* 32: reinterpret (impl-defined, 2's compl. everywhere) */
    uint32_t field = x & ((1u << bits) - 1u);
    uint32_t m = 1u << (bits - 1u);  /* sign bit of the field */
    /* (field ^ m) moves the range to 0..2^bits-1 with sign flipped; subtracting m
     * recenters it. Both operands fit in int32_t, so no overflow, no impl-defined cast. */
    return (int32_t)(field ^ m) - (int32_t)m;
}
```

- 시간/공간: O(1) · 공간 O(1).
- 왜 이 캐스트 순서인가: `(int32_t)((field ^ m) - m)` 처럼 unsigned로 빼고 캐스트하면 `0xFFFFF800` 같은 큰 unsigned를 int32로 바꾸는 **구현 정의 변환**(C11 6.3.1.3p3)이 된다. bits ≤ 31이면 `field ^ m < 2^31` 이고 `m ≤ 2^30` 이므로 각자 int32에 들어가고 빼기도 오버플로 없음.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`bits = 12` → mask `0xFFF`, `m = 0x800`:

| x | field | field ^ m | − m | 결과 |
|---|---|---|---|---|
| `0x800` | `0x800` | `0x000` = 0 | 0 − 2048 | **−2048** |
| `0xFFF` | `0xFFF` | `0x7FF` = 2047 | 2047 − 2048 | **−1** |
| `0x7FF` | `0x7FF` | `0xFFF` = 4095 | 4095 − 2048 | **2047** |
| `0xABCDEFFF` | `0xFFF` | `0x7FF` | 2047 − 2048 | **−1** |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `0x7FF` → 2047, `0x800` → −2048, `0xFFF` → −1, `0x001` → 1, `0` → 0.
- 상위 쓰레기: `0xABCDEFFF` (12) → −1.
- 경계 폭: bits=1 (`1` → −1, `0` → 0), bits=31 (`0x40000000` → −2^30), bits=32 (`0xFFFFFFFF` → −1, `0x7FFFFFFF` → INT32_MAX).
- round-trip: −2048..2047 모든 v에 대해 `sign_extend32((uint32_t)v & 0xFFF, 12) == v`.

</details>

### 4. 꼬리 질문

- "What does the Linux kernel do?"

<details><summary>답</summary>

`sign_extend32(value, index)` in `include/linux/bitops.h` takes the sign bit index (bits − 1), shifts the value left so that bit lands in bit 31, then does an arithmetic right shift back. That relies on GCC behavior the kernel assumes (two's complement, arithmetic shift); in portable C the XOR-subtract form avoids it.
한국어 핵심: 커널은 `(s32)(v << (31 - index)) >> (31 - index)`, 인자는 폭이 아니라 **sign bit 인덱스**.

```c
int32_t se_shift(uint32_t x, unsigned bits)       /* relies on impl-defined >> */
{
    unsigned s = 32u - bits;
    return (int32_t)(x << s) >> s;
}
```

</details>

- "Now convert that 12-bit sample to millivolts with a 3.3 V reference, without floating point."

<details><summary>답</summary>

For a bipolar ±Vref ADC, `mv = (int32_t)sample * 3300 / 2048`; the product fits easily in 32 bits (2048 × 3300 ≈ 6.8 M). Multiply before divide to keep precision, and note that C integer division truncates toward zero, so negative values round toward zero.
한국어 핵심: 곱하고 나누기, 음수 나눗셈은 0쪽으로 잘림 → [N05I 고정소수점](2026-11-01_N05I_validation_c.html).

</details>

- "The field is at bits [27:16], not at bit 0."

<details><summary>답</summary>

Extract first: `sign_extend32(field_get(reg, 16, 12), 12)`. Or shift the field so its sign bit lands at bit 31 and use one arithmetic right shift, if the platform behavior is acceptable.
한국어 핵심: A8 `field_get` 으로 꺼낸 다음 A6.

</details>

### 5. 채점 · 자주 틀리는 것

- 상위 쓰레기 mask 안 함 → `0xABCDEFFF` 가 −1이 아닌 이상한 값.
- `1 << (bits - 1)` (int) — bits=32 에서 `1 << 31` UB.
- bits=32 를 일반 경로로 보내 `1u << 32` UB.
- `(int32_t)x << 20` 처럼 부호 있는 값을 왼쪽 시프트 → 오버플로 UB.
- 마지막 결과를 unsigned로 계산 후 int32로 캐스트 → 구현 정의 (대부분 동작하지만, 말로 설명할 줄 알아야 Staff 시그널).

## A7 · 홀짝 비트 swap · 바이트 안 니블 swap (★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: (1) bit 0↔1, 2↔3 … 처럼 이웃 비트끼리 자리 바꾸기, (2) 각 바이트 안에서 상위 니블 ↔ 하위 니블 바꾸기 (32비트 전체와 1바이트 버전).
- **상황**: A1 mask-swap의 **한 단계만 떼어 낸 문제**. 니블 swap은 스크린 문제(니블 뒤집기)의 절반이고, BCD 표시·니블 단위 패킹된 센서 데이터·배선이 뒤바뀐 보드(bit lane swap) 디버그에서 나온다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `swap_odd_even` | 이웃 비트 쌍 교환 | 루프 없이 한 줄 |
| `swap_nibbles_in_bytes` | 4바이트 각각의 니블 교환 | 바이트 순서는 유지 |
| `swap_nibbles8` | 1바이트 니블 교환 | 결과는 uint8_t |

- **예시를 말로 풀면**: `0xA = 1010b` → 짝·홀 바꾸면 `0101b = 0x5`, 그래서 `0xAAAAAAAA` → `0x55555555`. `0x12345678` 니블 swap → `0x21436587`.
- **지켜야 할 조건**: 마스크 상수의 의미를 말로 설명 (`0x5 = 0101` 은 짝수 비트).

### 1. Problem (English)

"Swap each pair of adjacent bits in a 32-bit word. Then swap the high and low nibble of every byte."

```c
uint32_t swap_odd_even(uint32_t x);
uint32_t swap_nibbles_in_bytes(uint32_t x);
uint8_t  swap_nibbles8(uint8_t b);
```

- `swap_odd_even(0x0000000B)` → `0x00000007` (`1011` → `0111`)
- `swap_odd_even(0x12345678)` → `0x2138A9B4`
- `swap_nibbles_in_bytes(0x12345678)` → `0x21436587`, `swap_nibbles8(0xAB)` → `0xBA`

### 2. 먼저 물어볼 것 (clarify)

- "Bit 0 swaps with bit 1, bit 2 with bit 3, and so on?" → Yes.
- "Byte order stays the same for the nibble swap?" → Yes, only inside each byte.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify |
| 1–3 | `1011 → 0111` 손으로, 마스크 의미 |
| 3–6 | 세 함수 |
| 6–8 | `0x12345678` 니블 swap trace |
| 8–10 | 테스트 (자기 역함수) |

- 소리 내서: "`0x55555555` selects the even bits; I move even bits up by one and odd bits down by one, then OR them."
- 소리 내서: "Each of these is its own inverse, so applying it twice must give the input back — that's my quick test."

<details><summary>힌트 1 (막히면 5분 후)</summary>

짝수 비트 = `x & 0x55555555`, 홀수 비트 = `x & 0xAAAAAAAA`. 짝수는 `<< 1`, 홀수는 `>> 1`.

</details>

<details><summary>힌트 2</summary>

니블은 같은 구조에 마스크 `0x0F0F0F0F`, 시프트 4. 1바이트 버전은 `(b << 4) | (b >> 4)` 인데 `b << 4` 는 int로 승격되어 8비트를 넘으므로 `(uint8_t)` 로 잘라야 함.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* Swap bit 0<->1, 2<->3, ... : 0x55555555 selects even bits, 0xAAAAAAAA odd bits. */
uint32_t swap_odd_even(uint32_t x)
{
    return ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
}

/* Swap the two nibbles inside every byte: 0x12345678 -> 0x21436587. */
uint32_t swap_nibbles_in_bytes(uint32_t x)
{
    return ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
}

/* Single byte version. Cast back: b << 4 is promoted to int. */
uint8_t swap_nibbles8(uint8_t b)
{
    return (uint8_t)((b << 4) | (b >> 4));
}
```

- 시간/공간: O(1) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`swap_nibbles_in_bytes(0x12345678)`:

| 조각 | 계산 | 값 |
|---|---|---|
| 상위 니블 → 아래 | `(x >> 4) & 0x0F0F0F0F` | `0x01030507` |
| 하위 니블 → 위 | `(x & 0x0F0F0F0F) << 4` | `0x20406080` |
| OR | | `0x21436587` |

`swap_odd_even(0xB)`: `(1011 >> 1) & 0101 = 0101`, `(1011 & 0101) << 1 = 0010` → OR = `0111` = 7.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `0x12345678` → 니블 `0x21436587`, 홀짝 `0x2138A9B4`.
- 0, all-ones → 그대로.
- 경계: `0x80000000` → `0x40000000` (bit 31은 bit 30과 짝), `0x1` → `0x2`.
- 1바이트: `0xAB` → `0xBA`, `0x0F` → `0xF0`.
- 자기 역함수: `f(f(x)) == x` (랜덤 1만 개).

</details>

### 4. 꼬리 질문

- "Swap bits i and j of a word (any positions)."

<details><summary>답</summary>

If the two bits differ, flip both; otherwise do nothing. `d = ((x >> i) ^ (x >> j)) & 1u; x ^= (d << i) | (d << j);` — branch-free and works for any `i`, `j` under 32.
한국어 핵심: 두 비트가 다르면 둘 다 toggle — XOR 트릭.

</details>

- "How does this relate to the nibble-reverse screen question?"

<details><summary>답</summary>

Reversing nibbles is byte-reverse plus nibble-swap-in-each-byte; reversing bits adds the 1-bit and 2-bit swap stages before that. So these one-step swaps are the building blocks of all the reverse functions.
한국어 핵심: 니블 뒤집기 = bswap + 니블 swap, 비트 뒤집기 = 1·2·4비트 swap + bswap.

</details>

### 5. 채점 · 자주 틀리는 것

- 마스크를 `0xAAAAAAAA` 로 시작했는데 시프트 방향을 그대로 둬서 반대로 됨.
- `swap_nibbles8` 에서 `(uint8_t)` 캐스트 빠뜨림 → `-Wconversion` 경고, 반환형 넓으면 상위 쓰레기.
- 니블 swap 마스크를 시프트 **후**에 걸어야 할 것을 전에 거는 실수 (`(x >> 4) & 0x0F0F0F0F` 맞음, `(x & 0x0F0F0F0F) >> 4` 틀림).

## A8 · 임의 (pos, width) 필드 추출/삽입 — width=32 포함 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 레지스터 값에서 `[pos+width-1 : pos]` 비트 필드를 꺼내고(get), 다른 비트를 건드리지 않고 새 값을 넣기(set). width=32와 잘못된 (pos, width)도 안전하게.
- **상황**: 모든 레지스터 드라이버·validation 스크립트의 기본기. 데이터시트의 "CTRL[11:4] = DIV" 를 코드로. 면접관이 노리는 곳은 **width = 32 일 때 `1u << 32` UB**와 **넓은 val이 이웃 필드를 망가뜨리는 것**. 공식 자체는 [C 정리 C6](2026-10-29_c_basics_cram.html) — 여기선 **입력 검증·경계 증명**까지.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `field_mask` | 필드 위치의 1 마스크 | width 1..32, pos + width ≤ 32, 아니면 0 |
| `field_get` | 필드 값 (오른쪽 정렬) | 잘못된 입력 → 0 |
| `field_set` | 필드만 바꾼 새 레지스터 값 | val은 width로 잘림, 잘못된 입력 → reg 그대로 |

- **예시를 말로 풀면**: `0xDEADBEEF` 의 bit 4부터 12비트 = 가운데 `BEE` → `0xBEE`. 최상위 니블(bit 28, 4비트)을 1로 → `0x1EADBEEF`.
- **지켜야 할 조건**: 시프트 양이 항상 0..31. `pos + width` 가 unsigned overflow로 작아지는 입력도 거를 것.

### 1. Problem (English)

"Write get/set helpers for a register bit field given its position and width. They'll be used everywhere in the driver, so they need to be correct for every legal field, including a full 32-bit field, and must not corrupt neighbouring fields."

```c
uint32_t field_mask(unsigned pos, unsigned width);
uint32_t field_get(uint32_t reg, unsigned pos, unsigned width);
uint32_t field_set(uint32_t reg, unsigned pos, unsigned width, uint32_t val);
```

- `field_get(0xDEADBEEF, 4, 12)` → `0xBEE`
- `field_set(0xDEADBEEF, 4, 12, 0x123)` → `0xDEAD123F`
- `field_set(0xDEADBEEF, 4, 12, 0xFFFF5)` → `0xDEADFF5F` (value truncated to 12 bits)
- `field_get(0xDEADBEEF, 0, 32)` → `0xDEADBEEF`

### 2. 먼저 물어볼 것 (clarify)

- "If `val` is wider than the field, truncate or reject?" → Truncate (some teams prefer to assert — mention it).
- "What should invalid `(pos, width)` do?" → No UB; get returns 0, set returns the register unchanged.
- "Is this on a value, or directly on the volatile register?" → On a value; the MMIO read-modify-write wrapper is a follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (truncate vs reject, invalid 입력, 값 vs 레지스터) |
| 2–5 | `0xDEADBEEF`, pos 4 width 12 → mask `0x0000FFF0` 손으로 |
| 5–13 | `field_mask` (검증 + width 32 분기) → get → set |
| 13–17 | set trace + width 32 trace |
| 17–20 | 테스트 + volatile RMW/W1C 꼬리 |

- 소리 내서: "The only tricky part is the mask: `(1u << width) - 1` is undefined when width is 32, so that case gets its own branch."
- 소리 내서: "For set I clear the field with the inverted mask and OR in the shifted value masked again, so an oversized value can't spill into neighbours."

<details><summary>힌트 1 (막히면 5분 후)</summary>

검증 조건: `width == 0`, `pos >= 32`, `width > 32 - pos`. 마지막을 `pos + width > 32` 로 쓰면 width가 거대할 때 덧셈이 wrap 해서 통과해 버린다.

</details>

<details><summary>힌트 2</summary>

`low = (width == 32) ? 0xFFFFFFFF : (1u << width) - 1`, `mask = low << pos`. width가 32면 검증에서 pos는 0일 수밖에 없으니 `<< pos` 도 안전. set = `(reg & ~m) | ((val << pos) & m)`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* Mask for bits [pos+width-1 : pos]. Valid: 1 <= width, pos + width <= 32.
 * Invalid -> 0 (callers then read 0 / leave reg unchanged). */
uint32_t field_mask(unsigned pos, unsigned width)
{
    if (width == 0u || pos >= 32u || width > 32u - pos)  /* no pos+width overflow */
        return 0u;
    uint32_t low = (width == 32u) ? 0xFFFFFFFFu        /* 1u << 32 is UB */
                                  : ((1u << width) - 1u);
    return low << pos;
}

uint32_t field_get(uint32_t reg, unsigned pos, unsigned width)
{
    uint32_t m = field_mask(pos, width);
    return m ? (reg & m) >> pos : 0u;
}

/* Clear the field, then OR the shifted value masked to the field (oversized val is cut). */
uint32_t field_set(uint32_t reg, unsigned pos, unsigned width, uint32_t val)
{
    uint32_t m = field_mask(pos, width);
    if (m == 0u)
        return reg;
    return (reg & ~m) | ((val << pos) & m);
}
```

- 시간/공간: O(1) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`field_set(0xDEADBEEF, 4, 12, 0xFFFF5)`:

| 단계 | 계산 | 값 |
|---|---|---|
| 검증 | width 12, pos 4, 12 ≤ 28 | OK |
| low | `(1u << 12) - 1` | `0x00000FFF` |
| m | `low << 4` | `0x0000FFF0` |
| `reg & ~m` | `0xDEADBEEF & 0xFFFF000F` | `0xDEAD000F` |
| `(val << 4) & m` | `0x00FFFF50 & 0x0000FFF0` | `0x0000FF50` |
| OR | | `0xDEADFF5F` |

width 32: `field_mask(0, 32)` → 분기로 `low = 0xFFFFFFFF`, `<< 0` → `0xFFFFFFFF`. `field_mask(1, 32)` → `32 > 31` → 0 (invalid).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: get `(0xDEADBEEF, 4, 12)` → `0xBEE`, set `(…, 28, 4, 1)` → `0x1EADBEEF`.
- width 32: get = reg 전체, set = val 전체.
- 경계: `(31, 1)` 최상위 비트, `(0, 1)` 최하위 비트.
- 넓은 val: `0xFFFF5` → 12비트로 잘려 이웃 비트 보존.
- invalid: `(0, 0)`, `(1, 32)`, `(32, 1)`, `(30, 4)`, `(4, UINT_MAX)` → mask 0, get 0, set은 reg 그대로.
- 전수: 모든 합법 (pos, width) 528개에 대해 `get(set(r, v)) == v & low` 이고 필드 밖 비트 불변.

</details>

### 4. 꼬리 질문

- "Now do it directly on a memory-mapped register."

<details><summary>답</summary>

Read the `volatile` register once into a local, apply `field_set`, write it back once — exactly one read and one write. It's not atomic, so if an ISR touches the same register I'd disable interrupts around it or use the hardware's SET/CLR alias registers.
한국어 핵심: `v = *reg; *reg = field_set(v, …);` 1 read + 1 write, ISR 공유 시 IRQ off → [N05C C2](2026-11-01_N05C_register_mmio_driver.html).

```c
void reg_field_write(volatile uint32_t *reg, unsigned pos, unsigned width, uint32_t val)
{
    *reg = field_set(*reg, pos, width, val);
}
```

</details>

- "What if the register is write-1-to-clear?"

<details><summary>답</summary>

Never read-modify-write a W1C register: the read returns every pending 1 and writing them back clears interrupts you haven't handled. You write only the bits you want to clear, `*status = bit`.
한국어 핵심: W1C엔 RMW 금지, 지울 비트만 쓰기 (C 정리 C6 함정).

</details>

- "Make it 64-bit, or compile-time constant masks."

<details><summary>답</summary>

For 64-bit, change the types to `uint64_t`, compare against 64, and use `1ull` and `~0ull` — `1u << 40` is still UB because `1u` is 32-bit. For constant fields, Linux-style `GENMASK(h, l)` plus `FIELD_GET`/`FIELD_PREP` macros let the compiler fold everything.
한국어 핵심: 64비트는 `1ull`, 상수 필드는 `GENMASK`/`FIELD_PREP`.

</details>

### 5. 채점 · 자주 틀리는 것

- `(1u << width) - 1` 를 width 32 에서 그대로 → UB. 이 Mac(arm64 -O0) 실측: `1u << 32` → 1, mask → `0x00000000` — 크래시 없이 조용히 틀림 (컴파일러·최적화마다 다름).
- `~0u >> (32 - width)` 대안은 width 0 에서 `>> 32` UB — 쓰려면 width ≥ 1 검증이 먼저.
- `pos + width > 32` 검사 → 거대한 width에서 wrap.
- set에서 `(val << pos)` 에 마스크 안 걸기 → 이웃 필드 오염.
- `1 << pos` (int) → pos 31에서 UB. 전부 `u`.

## A9 · n번째 비트 set/clear/toggle/test 매크로 + 연속 1 개수 최대값 (★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: (1) 변수의 n번째 비트를 set/clear/toggle/test 하는 매크로 4개, (2) 32비트 값에서 연속된 1의 가장 긴 길이.
- **상황**: 매크로는 펌웨어 코드리뷰 단골(괄호·`1u`). 연속 1 길이는 **shmoo/마진 결과 비트맵에서 가장 넓은 pass 구간(eye width)** 찾기, 연속 free 블록 찾기와 같은 모양.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `BIT_SET/CLEAR/TOGGLE` | x의 n번째 비트 변경 | 매크로 인자 전부 괄호, `1u` |
| `BIT_TEST` | n번째 비트가 1이면 1 | 결과는 정확히 0 또는 1 |
| `max_consecutive_ones` | 가장 긴 1 연속 길이 | bit 31과 bit 0은 이어지지 않음 |

- **예시를 말로 풀면**: `0x6E = 0110 1110b` → 1 연속 구간은 길이 2와 3 → 답 3.
- **지켜야 할 조건**: 매크로가 `BIT_SET(r, i + 1)` 같은 식 인자에서도 맞을 것. 32번 비트 하나씩 세는 것보다 짧은 방법이면 가산점.

### 1. Problem (English)

"Write the four classic bit macros — set, clear, toggle, test bit n. Then, given a 32-bit pass/fail bitmap, return the length of the longest run of 1s."

```c
#define BIT_SET(x, n)    /* ... */
#define BIT_CLEAR(x, n)  /* ... */
#define BIT_TOGGLE(x, n) /* ... */
#define BIT_TEST(x, n)   /* ... evaluates to 0 or 1 */
unsigned max_consecutive_ones(uint32_t x);
```

- `x = 0; BIT_SET(x, 31); BIT_SET(x, 0);` → `x == 0x80000001`
- `max_consecutive_ones(0x6E)` → 3
- `max_consecutive_ones(0xFFFFFFFF)` → 32, `max_consecutive_ones(0x80000001)` → 1

### 2. 먼저 물어볼 것 (clarify)

- "Is `x` always a 32-bit unsigned variable?" → Yes.
- "Does the run wrap around from bit 31 to bit 0?" → No, it's a linear bitmap.
- "Should `BIT_TEST` return 0/1 or just nonzero?" → Exactly 0 or 1.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (wrap 여부, test 반환값) |
| 1–4 | 매크로 4개 (괄호 말하면서) |
| 4–7 | 연속 1: 루프 카운트 방법 → `x & (x << 1)` 방법 |
| 7–8 | `0x6E` trace |
| 8–10 | 테스트 |

- 소리 내서: "Every macro argument is parenthesized and I use `1u`, so `BIT_SET(r, i + 1)` and bit 31 both behave."
- 소리 내서: "ANDing x with itself shifted by one shortens every run of ones by one bit; the number of iterations until it's zero is the longest run."

<details><summary>힌트 1 (막히면 5분 후)</summary>

test는 `((x) >> (n)) & 1u` — `(x) & BIT(n)` 은 0 아니면 `BIT(n)` 이라 "정확히 1"이 아님 (`!!` 를 붙이는 방법도 있음).

</details>

<details><summary>힌트 2</summary>

연속 1: `while (x) { x &= x << 1; n++; }`. 1의 구간 길이 L은 한 번 할 때마다 L−1이 된다. 단순 방법(비트 하나씩 보면서 현재 길이·최대 길이 갱신)도 정답 — 둘 다 말하기.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* n must be 0..31. 1u (not 1) so bit 31 is not a signed-int overflow. */
#define BIT(n)            (1u << (n))
#define BIT_SET(x, n)     ((x) |=  BIT(n))
#define BIT_CLEAR(x, n)   ((x) &= ~BIT(n))
#define BIT_TOGGLE(x, n)  ((x) ^=  BIT(n))
#define BIT_TEST(x, n)    (((x) >> (n)) & 1u)          /* 0 or 1 */

/* Longest run of consecutive 1 bits. Each x &= x << 1 shortens every run by 1,
 * so the loop count is the longest run. No wrap-around between bit 31 and bit 0. */
unsigned max_consecutive_ones(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        x &= x << 1;
        n++;
    }
    return n;
}
```

- 시간/공간: 매크로 O(1) · 연속 1 O(최장 길이) ≤ 32 · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`max_consecutive_ones(0x6E)`:

| 반복 | x (이진) | n |
|---|---|---|
| 시작 | `0110 1110` (구간 2, 3) | 0 |
| 1 | `0100 1100` (`0x4C`, 구간 1, 2) | 1 |
| 2 | `0000 1000` (`0x08`, 구간 1) | 2 |
| 3 | `0000 0000` | **3** |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 매크로: bit 0, bit 31 set → `0x80000001`; toggle 두 번 → 원래 값; 이미 0인 비트 clear → 변화 없음; test 결과 0/1.
- 정상: `0x6E` → 3, `0x0FF00F00` → 8.
- 0 → 0, all-ones → 32.
- wrap 없음: `0x80000001` → 1 (2가 아님). 교대 패턴 `0x55555555` → 1.

</details>

### 4. 꼬리 질문

- "Return where the longest run starts, too — it's the centre of our shmoo eye."

<details><summary>답</summary>

Scan bit by bit keeping the current run start and length and the best start and length; when a 0 ends a run, compare and update. The centre of the passing window is `best_start + best_len / 2`, which is the setting you'd program.
한국어 핵심: 현재/최대 (start, len) 갱신 루프, 중앙 = start + len/2 → [N05I shmoo](2026-11-01_N05I_validation_c.html).

</details>

- "Why are function-like macros risky here? Would you use inline functions instead?"

<details><summary>답</summary>

Macros evaluate their arguments as text, so `BIT_SET(x, i++)` is fine once but a macro that used `n` twice would increment twice, and missing parentheses break precedence. A `static inline` function is type-checked and evaluates arguments once; the macro's only advantage is working on any integer type and on `volatile` registers without casts.
한국어 핵심: 매크로 = 인자 텍스트 치환(두 번 평가·우선순위 위험), `static inline` 이 타입 안전.

</details>

### 5. 채점 · 자주 틀리는 것

- `1 << n` (int) → n=31 UB. `1u`.
- 매크로 인자 괄호 누락: `~BIT(n)` 에서 `BIT(n)` 바깥 괄호가 없으면 `~1u << n` 으로 파싱.
- `BIT_TEST` 가 `x & BIT(n)` → 결과가 0/1이 아닌데 `== 1` 비교에서 틀림.
- 64비트 변수에 `1u << 40` → UB. 64비트용은 `1ull`.

## 출처

1. cppreference — Arithmetic operators (shift: 시프트 양 ≥ 폭이면 UB, 부호 있는 음수 오른쪽 시프트 구현 정의): https://en.cppreference.com/w/c/language/operator_arithmetic
2. C11 draft N1570 — 6.5.7 (bitwise shift), 6.3.1.3 (signed/unsigned 변환): https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
3. Sean Eron Anderson, Bit Twiddling Hacks (reverse, popcount, parity, next pow2, sign extend): https://graphics.stanford.edu/~seander/bithacks.html
4. Linux kernel `include/linux/bitops.h` (sign_extend32), `include/linux/bits.h` (GENMASK): https://github.com/torvalds/linux/blob/master/include/linux/bitops.h
5. GCC Other Builtins (`__builtin_clz`/`ctz` 0 입력 undefined): https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
6. Arm A-profile/M-profile ISA — `RBIT`, `CLZ`, `ROR` 명령: https://developer.arm.com/documentation/
- 위 출처는 표준 레퍼런스로 인용 — 이번 작성 세션에서 웹 페이지를 다시 열어 확인하지는 않음. 모든 코드 결과값은 onsitePrep/n05/a/run.sh 로 이 Mac(arm64, clang, ASan+UBSan)에서 컴파일·실행 검증.

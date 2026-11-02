# N02 · 기술 스크린 코딩 회고 — "Reverse order of input buffer data" (니블 뒤집기)
<!-- theme: pastel -->

> **실제 문제 (10-30)**: Reverse order of input buffer data: change hexadecimal `0x1234567890abcdef` to `0xfedcba0987654321` · **평가 포인트**: 문법보다 로직 (면접관 설명) · **결과**: Don의 로직 ✅ 정답 · **코드**: `onsitePrep/nibble_reverse.c` (ALL PASS) · **목록**: [온사이트 플랜](2026-10-31_rivian_onsite_plan.html)

## TL;DR

- 이 문제는 **바이트 뒤집기가 아니라 니블(4-bit, hex 한 자리) 뒤집기**다. 출력에서 `ef` → `fe` 로 바이트 안의 순서까지 바뀐 게 단서.
  - 니블 뒤집기: `0x1234567890abcdef` → **`0xfedcba0987654321`** ✔ (요구 출력)
  - 바이트 뒤집기(bswap64): → `0xefcdab9078563412` ✘
  - 비트 뒤집기: → `0xf7b3d5091e6a2c48` ✘
- **Don의 코드는 로직이 맞다**: 4비트씩 16번, 아래 니블을 꺼내 결과에 밀어 넣기. 
- 아쉬운 점 3개: ① 함수 이름 `rev_bits` (실제는 nibble) ② trace 주석에서 `i = 8`, `0xef` 처럼 **바이트 단위로 섞여** 설명이 흔들림 ③ 테스트 케이스와 꼬리(버퍼 버전, 빠른 버전)를 먼저 꺼내지 못함.
- 온사이트에서 같은 계열이 나오면: **clarify(단위가 bit/nibble/byte?) → 루프 버전 → trace 2스텝 → 테스트 → O(log n) 버전 / 버퍼 버전 제안** 순서.

## 1. Don이 쓴 답 (원본)

```c
uint64_t rev_bits(uint64_t input){
    uint64_t res = 0;
    uint64_t i = 16;
    while (i > 0){
       1) res = res << 4;
       2)  res = res | (input & 0xF);
       3)  input = input >> 4;
        i--;
    }
    return res;
}
```

| 항목 | 평가 |
|---|---|
| 알고리즘 | ✅ 정답. 16 니블 × (결과 왼쪽 shift 4 → 입력의 하위 니블 OR → 입력 오른쪽 shift 4) |
| 경계 | ✅ `uint64_t` 라 shift UB 없음, 루프 16회 고정 |
| 이름 | 🟡 `rev_bits` → `rev_nibbles64` 가 정확. 면접관이 "bits?"라고 물으면 혼란 |
| trace | 🟡 주석의 `i = 8`, `res = 0xef` 는 **바이트 단위 사고**가 섞임. 니블이면 첫 스텝 결과는 `0xf` |
| main/printf | 🟡 `printf()` 비어 있음 → `printf("0x%016" PRIx64 "\n", out);` (`<inttypes.h>`) |
| 테스트 | 🟡 말로 꺼내지 않음 → 0, 모든 F, `0xF` 하나(→ 최상위로 이동), 두 번 적용하면 원래 값 |

### 1.1 올바른 trace (말로 연습)

| 스텝 | input (하위 니블) | res |
|---|---|---|
| 0 | `0x1234567890abcdef` (f) | `0x0` |
| 1 | `0x01234567890abcde` (e) | `0xf` |
| 2 | `0x001234567890abcd` (d) | `0xfe` |
| 3 | `0x0001234567890abc` (c) | `0xfed` |
| … | … | … |
| 16 | `0x0` | `0xfedcba0987654321` ✔ |

- 영어: "Each iteration I shift the result left by one hex digit and append the lowest hex digit of the input, then drop that digit from the input. After sixteen digits the order is reversed."

## 2. 같은 문제의 4가지 버전 (꼬리 질문 대비)

### A. 루프 버전 (Don 답 정리)

```c
/* A. Don's answer, cleaned up: pop the low nibble of `in`, push it into `res`. 16 iterations. */
uint64_t rev_nibbles64(uint64_t in)
{
    uint64_t res = 0;
    for (int i = 0; i < 16; i++) {
        res = (res << 4) | (in & 0xFu);
        in >>= 4;
    }
    return res;
}
```

### B. O(log n) mask-swap 버전 — "더 빠르게?"

```c
/* B. O(log n) mask-and-swap: reverse bytes, then swap the two nibbles inside each byte. */
uint64_t bswap64(uint64_t x)
{
    x = ((x >> 8)  & 0x00FF00FF00FF00FFull) | ((x & 0x00FF00FF00FF00FFull) << 8);
    x = ((x >> 16) & 0x0000FFFF0000FFFFull) | ((x & 0x0000FFFF0000FFFFull) << 16);
    return (x >> 32) | (x << 32);
}

uint64_t rev_nibbles64_fast(uint64_t x)
{
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0Full) | ((x & 0x0F0F0F0F0F0F0F0Full) << 4);
    return bswap64(x);
}
```

- 원리: **니블 뒤집기 = 바이트 순서 뒤집기 + 각 바이트 안의 두 니블 swap**. 분기 없음, 4단계 고정. 실무에선 `__builtin_bswap64` → ARM `REV` 한 명령.
- 랜덤 10,000개로 A와 B 결과가 같은지 테스트함.

### C. 버퍼 버전 — 문제 제목이 "input **buffer** data" ★ 온사이트 꼬리 1순위

```c
/* C. Buffer version ("input buffer data"), any length, in place.
 * buf[0] is the most significant byte as written: {12 34 .. ef} -> {fe dc .. 21}. */
static inline uint8_t swap_nib(uint8_t b) { return (uint8_t)((b << 4) | (b >> 4)); }

void rev_nibbles_buf(uint8_t *buf, size_t n)
{
    if (n == 0)
        return;
    size_t i = 0, j = n - 1;
    while (i < j) {
        uint8_t a = buf[i];
        buf[i] = swap_nib(buf[j]);
        buf[j] = swap_nib(a);
        i++;
        j--;
    }
    if (i == j)
        buf[i] = swap_nib(buf[i]);      /* odd length: middle byte only swaps its nibbles */
}
```

- 테스트: 8 B `{12 34 56 78 90 ab cd ef}` → `{fe dc ba 09 87 65 43 21}` · 3 B `{12 34 56}` → `{65 43 21}` (홀수: 가운데 바이트도 니블 swap) · 1 B `{ab}` → `{ba}` · 길이 0.
- **clarify 할 것**: "Is the buffer an array of bytes with byte 0 as the most significant, or a uint64 in memory? On a little-endian CPU, `0x1234567890abcdef` is stored as `ef cd ab 90 78 56 34 12`." → 메모리 표현을 물어보는 것 자체가 임베디드 시그널.

### D. 일반화 — "비트 단위면? 바이트 단위면?"

```c
/* D. Generic: reverse 64/w units of w bits (w = 1 bits, 4 nibbles, 8 bytes, 16, 32). */
uint64_t rev_units64(uint64_t x, unsigned w)
{
    uint64_t mask = (1ull << w) - 1u;   /* w <= 32, so no 1<<64 UB */
    uint64_t res = 0;
    for (unsigned i = 0; i < 64u / w; i++) {
        res = (res << w) | (x & mask);
        x >>= w;
    }
    return res;
}
```

| w | 의미 | `0x1234567890abcdef` 결과 |
|---|---|---|
| 1 | 비트 뒤집기 | `0xf7b3d5091e6a2c48` |
| 4 | 니블 뒤집기 | `0xfedcba0987654321` |
| 8 | 바이트 뒤집기 (bswap) | `0xefcdab9078563412` |

- 비트 뒤집기 빠른 버전: 1·2·4비트 mask-swap 후 bswap (→ [C 정리 C7](2026-10-29_c_basics_cram.html)) 또는 **16-entry 니블 LUT**.

## 3. 같은 계열로 온사이트에서 나올 법한 변형 (연습 리스트)

| 변형 | 핵심 | 연결 |
|---|---|---|
| bswap16/32/64 (엔디안 변환) | shift+mask, `__builtin_bswap*` | [C7·C8](2026-10-29_c_basics_cram.html) |
| 비트 뒤집기 32/64 | mask-swap 5~6단계, LUT | [C7](2026-10-29_c_basics_cram.html) |
| 바이트 버퍼 뒤집기 / 회전 | 두 포인터, 3-reverse 회전 | [C2](2026-10-29_c_basics_cram.html) |
| 비트 필드 추출/삽입 | GENMASK, FIELD_PREP | [C6](2026-10-29_c_basics_cram.html), [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) |
| popcount, parity, 최상위 비트 위치 | `x &= x-1`, XOR fold | [C7](2026-10-29_c_basics_cram.html) |
| little/big endian 바이트 배열 ↔ 정수 | shift로 조립 (정렬·엔디안 무관) | [C8](2026-10-29_c_basics_cram.html) |
| hex 문자열 ↔ 정수 | 문자→니블, 오버플로 | [C3](2026-10-29_c_basics_cram.html) |
| CRC / checksum | 비트 단위 루프 → 테이블 | [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html) |
| 메모리 패턴 테스트 | walking 1, march | [V1](hmScreenPrep/problems/V1_march_memtest/index.html) |
| 2개의 32-bit → 64-bit | hi-lo-hi | [C4](2026-10-29_c_basics_cram.html) |

## 4. 다음 코딩 라운드 습관 (이번 회고에서 나온 것)

1. 시작 전 **단위 확인**: "Reverse by bits, nibbles, or bytes? The example swaps `ef` to `fe`, so nibbles — correct?"
2. 이름을 정확히: `rev_nibbles64`.
3. **trace는 표처럼 2–3스텝만** 정확히 (단위를 바꾸지 않기).
4. 테스트 4개 말하기: 예시 · 0 · 한 니블만(`0xF` → 최상위) · 두 번 적용 = 원래 값.
5. 끝나면 먼저 제안: "I can also do it in O(log n) with masks, or generalize it to a byte buffer of any length."

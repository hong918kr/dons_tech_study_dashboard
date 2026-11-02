# N05B · 비트 응용 · 엔디안 · 패킹
<!-- theme: pastel -->
> **묶음**: B 비트 응용·엔디안·패킹 · **문제 수**: 8 (B1–B8) · **목표**: 바이트 순서·wire 포맷·비트 단위 데이터를 종이 20분 안에 UB 없이 · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · [C 정리 C7 비트 트릭 · C8 엔디안](2026-10-29_c_basics_cram.html) · **이전/다음**: [N05A 비트 기본](2026-11-01_N05A_bit_basics.html) / [N05C 레지스터·MMIO](2026-11-01_N05C_register_mmio_driver.html) · **코드**: onsitePrep/n05/b/ (`run.sh` → B1–B8 PASS)

## 0. TL;DR · 사용법

- 이 페이지가 훈련하는 것: **"메모리 속 바이트"와 "정수 값"을 오가는 코드** — bswap, 바이트 배열↔정수, 센서 패킷 직렬화, 비트맵, Gray code, 비트 스트림, 레지스터 diff, Morton interleave. 스크린의 니블 뒤집기([N02](2026-10-31_N02_screen_coding_retro.html))와 같은 계열이고, 실제 validation 코드(덤프 해석, 패킷 체크, 레지스터 비교)에 그대로 나온다.
- C 정리 [C7·C8](2026-10-29_c_basics_cram.html)에 이미 있는 것(bswap32, get_le32/get_be32, is_little_endian)은 **다시 설명하지 않는다** — 여기서는 16/64-bit, 임의 n바이트, 구조체 단위로 확장.
- 외울 한 줄 3개:
  - **"Assemble from bytes with shifts — never cast a byte pointer to `uint32_t *`."** (정렬 fault + strict aliasing + 호스트 엔디안 의존 3중 함정)
  - **Gray**: `g = b ^ (b >> 1)`, 역변환 = prefix XOR (`g ^= g >> 16; … g ^= g >> 1;`)
  - **wire 포맷 ≠ struct**: `memcpy(buf, &s, sizeof s)` 금지 → 필드마다 `put_be16/put_be32/put_be64`
- ★★★ (온사이트 확률 높음): **B1** (bswap 3종 + 엔디안 판별), **B3** (센서 패킷 직렬화). ★★: B2 · B4 · B5 · B6 · B8. ★: B7.
- 순서 추천: B1 → B2 → B3 (같은 헬퍼가 이어짐) → B4 → B7 → B5 → B6 → B8.
- 사용법: §1을 인쇄 → 타이머 20분 → 종이에 풀기 → 문제별 `<details>`로 채점 → [N05 §4 오답 표](2026-11-01_N05_c_paper_drill_bank.html)에 기록.

## 1. 문제지 (인쇄용 — 답 없음)

### B1 · bswap16/32/64 + host endianness

Implement byte-swap for 16, 32 and 64-bit values using only shifts and masks (no builtins), and a function that reports whether the host is little-endian at run time.

```c
uint16_t bswap16(uint16_t x);
uint32_t bswap32(uint32_t x);
uint64_t bswap64(uint64_t x);
bool host_is_little_endian(void);
```

Example: `bswap64(0x0102030405060708)` → `0x0807060504030201`.

### B2 · Byte array ↔ integer (LE/BE, any alignment)

Read/write an n-byte (1..8) unsigned integer from/to a byte buffer in little- or big-endian order. The pointer may be at any (odd) address. Must not depend on host endianness.

```c
uint64_t load_le(const uint8_t *p, size_t n);
uint64_t load_be(const uint8_t *p, size_t n);
void store_le(uint8_t *p, uint64_t v, size_t n);
void store_be(uint8_t *p, uint64_t v, size_t n);
```

Example: bytes `12 34 56` → `load_be(p,3)` = `0x123456`, `load_le(p,3)` = `0x563412`.

### B3 · Sensor packet serialize / deserialize

An IMU sample header must go over the wire as exactly 22 bytes, big-endian: `[0] magic 0xA5, [1] sensor_id, [2..3] flags, [4..7] seq, [8..15] timestamp_ns, [16..21] accel x,y,z (int16)`. Write serialize (returns bytes written, 0 if buffer too small) and deserialize (returns false on short buffer or bad magic). Must be independent of struct padding and host endianness.

```c
typedef struct { uint8_t sensor_id; uint16_t flags; uint32_t seq;
                 uint64_t timestamp_ns; int16_t accel[3]; } imu_sample_t;
size_t imu_serialize(const imu_sample_t *s, uint8_t *buf, size_t cap);
bool   imu_deserialize(const uint8_t *buf, size_t len, imu_sample_t *s);
```

Example: id=3, flags=1, seq=0x01020304, ts=0x123456789, accel={-1,256,-32768} → `A5 03 00 01 01 02 03 04 00 00 00 01 23 45 67 89 FF FF 01 00 80 00`.

### B4 · Bitmap allocator

A `uint32_t` array tracks `nbits` resources (bit=1 allocated; `nbits` not necessarily a multiple of 32). `bm_alloc` returns the lowest free index and marks it, or -1 if full. `bm_free` returns false on out-of-range or double free.

```c
int  bm_alloc(uint32_t *map, size_t nbits);
bool bm_free(uint32_t *map, size_t nbits, size_t idx);
```

Example: `map = {0xFFFFFFFF, 0x00000007}`, nbits=40 → returns 35, `map[1]` becomes `0x0000000F`.

### B5 · Gray code ↔ binary

Convert a 32-bit binary value to reflected Gray code and back.

```c
uint32_t bin_to_gray(uint32_t b);
uint32_t gray_to_bin(uint32_t g);
```

Example: `bin_to_gray(5)` = 7 (`101` → `111`), `gray_to_bin(4)` = 7.

### B6 · Bit-stream writer / reader (MSB-first)

Write fields of 1..32 bits into a byte buffer MSB-first, then read them back. Writer: `bw_put` (false if it would overflow `cap`), `bw_flush` (zero-pads the last byte, returns total bytes). Reader: `br_get` (false if not enough bits).

```c
bool   bw_put(bitwriter_t *w, uint32_t val, unsigned nbits);
size_t bw_flush(bitwriter_t *w);
bool   br_get(bitreader_t *r, unsigned nbits, uint32_t *out);
```

Example: put(0b101,3), put(0b11,2), put(0x1FF,9), flush → bytes `BF FC`.

### B7 · Register diff

Given the old and new value of a 32-bit register, list every changed bit (LSB first) with its direction. Write at most `max_out` entries, but return the total number of changes.

```c
typedef struct { uint8_t bit; bool now_set; } bit_change_t;
size_t reg_diff(uint32_t old_v, uint32_t new_v, bit_change_t *out, size_t max_out);
```

Example: old=`0x000000F0`, new=`0x00000118` → 5 changes: bit3 0→1, bit5 1→0, bit6 1→0, bit7 1→0, bit8 0→1.

### B8 · Morton (Z-order) interleave 16+16 → 32

Interleave the bits of x (even positions) and y (odd positions) into a 32-bit Morton code, and decode it back. No loops in the fast version.

```c
uint32_t morton_encode(uint16_t x, uint16_t y);
void     morton_decode(uint32_t m, uint16_t *x, uint16_t *y);
```

Example: x=5 (`101`), y=3 (`011`) → `011011` = 27.

## B1 · bswap16/32/64 직접 구현 + 엔디안 판별 (★★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 16/32/64비트 정수의 **바이트 순서를 거꾸로** 만드는 함수 3개 + "이 CPU가 little-endian인가?"를 런타임에 알려주는 함수 1개.
- **상황**: big-endian 장치(네트워크 패킷, 일부 센서·PCIe 장치의 BE 레지스터, SOME/IP 헤더)와 little-endian SoC 사이에서 값을 주고받을 때 매번 나온다. 레지스터 덤프의 바이트 순서를 맞추는 것도 같은 작업.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `bswap16` | 2바이트 순서 뒤집기 | shift·mask만, builtin 금지 |
| `bswap32` | 4바이트 순서 뒤집기 | 위와 같음 |
| `bswap64` | 8바이트 순서 뒤집기 | 위와 같음 |
| `host_is_little_endian` | 호스트 엔디안 판별 | 런타임에, UB 없이 |

- **예시를 말로 풀면**: `0x0102030405060708`의 바이트를 [01 02 03 04 05 06 07 08]로 보면, 결과는 그 바이트를 거꾸로 놓은 [08 07 06 05 04 03 02 01]. 비트나 니블이 아니라 **바이트(8비트) 단위**.
- **지켜야 할 조건**: 고정 폭 타입, unsigned 시프트, 폭 이상 시프트 금지, 두 번 적용하면 원래 값.

### 1. Problem (English)

"Implement `bswap16`, `bswap32`, `bswap64` with shifts and masks only. Then tell me, at run time, whether this machine is little-endian."

```c
uint16_t bswap16(uint16_t x);
uint32_t bswap32(uint32_t x);
uint64_t bswap64(uint64_t x);
bool host_is_little_endian(void);
```

- `bswap16(0x1234)` → `0x3412`
- `bswap32(0x12345678)` → `0x78563412`
- `bswap64(0x1234567890abcdef)` → `0xefcdab9078563412` (N02 니블 뒤집기 결과 `0xfedcba0987654321`와 **다름** — 단위 확인!)

### 2. 먼저 물어볼 것 (clarify)

- "Byte swap, not bit or nibble reverse — right?" → Yes, bytes.
- "Can I use compiler builtins like `__builtin_bswap32`?" → Not for the main answer; mention them as the production choice.
- "For endianness — run-time check is fine, or do you want compile-time?" → Run-time function; mention `__BYTE_ORDER__` as compile-time.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (단위 = byte, builtin 금지) |
| 2–5 | `0x12345678` 바이트 4개를 손으로 이동 → 접근 말하기 |
| 5–15 | bswap16 → bswap32 → bswap64(3단계 swap) → union probe |
| 15–18 | bswap64를 `0x0102030405060708`로 3단계 trace |
| 18–20 | 테스트 + builtin/REV 확장 |

- 소리 내서: **"For 64-bit I swap neighbouring bytes, then 16-bit halves, then 32-bit halves — three mask-and-shift steps instead of eight separate byte moves."**
- 소리 내서: **"For endianness I store a known 32-bit pattern in a union and look at byte 0; reading a different union member is defined in C, unlike in C++."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

bswap32는 바이트 4개 각각을 "어디서 어디로 몇 비트 이동"인지 표로 써 보면 된다. 바이트 0은 24비트 왼쪽, 바이트 3은 24비트 오른쪽, 가운데 둘은 8비트씩.

</details>

<details><summary>힌트 2</summary>

64비트는 "인접 바이트 swap (mask `0x00FF00FF…`, shift 8)" → "인접 16비트 swap (mask `0x0000FFFF…`, shift 16)" → "32비트 반쪽 swap (shift 32)". 엔디안은 `0x01020304`를 넣고 가장 낮은 주소 바이트가 `0x04`인지 보면 된다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>

static inline uint16_t bswap16(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));      /* promoted to int, cast truncates */
}

static inline uint32_t bswap32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) |
           ((x << 8) & 0x00FF0000u) | (x << 24);
}

static inline uint64_t bswap64(uint64_t x)       /* swap bytes, then halves, then words */
{
    x = ((x & 0x00FF00FF00FF00FFull) << 8)  | ((x >> 8)  & 0x00FF00FF00FF00FFull);
    x = ((x & 0x0000FFFF0000FFFFull) << 16) | ((x >> 16) & 0x0000FFFF0000FFFFull);
    return (x << 32) | (x >> 32);
}

static bool host_is_little_endian(void)
{
    union { uint32_t u32; uint8_t b[4]; } probe = { .u32 = 0x01020304u };
    return probe.b[0] == 0x04;                   /* LSB at lowest address */
}
```

- 시간 O(1) (bswap64 = 3단계), 공간 O(1).
- bswap32 / is_little_endian의 `char *` 방식은 [C 정리 C7·C8](2026-10-29_c_basics_cram.html)에 있음 — 여기선 union 방식과 64-bit를 추가.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

bswap64(`0x0102030405060708`):

| 단계 | 연산 | x |
|---|---|---|
| 0 | 입력 | `0x0102030405060708` |
| 1 | 인접 바이트 swap (mask `00FF…`, 8) | `0x0201040306050807` |
| 2 | 인접 16비트 swap (mask `0000FFFF…`, 16) | `0x0403020108070605` |
| 3 | 32비트 반쪽 swap (32) | `0x0807060504030201` ✔ |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: `0x12345678` → `0x78563412`, `0x0102030405060708` → `0x0807060504030201`.
- zero / all-ones: 0 → 0, `0xFFFF…` → 그대로 (변화 없는 값도 확인).
- boundary: `0xFF` → 최상위 바이트로 (`0xFF00000000000000`), `0x80000001` → `0x01000080` (최상위 비트 이동 — signed shift였으면 깨짐).
- round-trip: `bswap(bswap(x)) == x` (랜덤 10만 개로 검증).
- cross-check: `bswap64(w)` == 두 bswap32를 반대로 붙인 것.
- 엔디안: `__BYTE_ORDER__` 매크로 결과와 런타임 결과 비교 — 이 Mac(arm64)은 `1` (little) 출력 확인.

</details>

### 4. 꼬리 질문

- "What would you use in production, and what does it compile to on ARM?"

<details><summary>답</summary>

I'd use `__builtin_bswap32/64` (or the Linux `swab32` / `be32_to_cpu` helpers), which compile to a single `REV` instruction on ARMv7/ARMv8; `REV16` swaps bytes within each halfword. Clang and GCC also recognise the shift-mask pattern and emit `REV` anyway, so the hand version is not slower at -O2.
한국어 핵심: 실무는 builtin / `be32_to_cpu`, ARM에선 `REV` 1개 명령.

</details>

- "How do you detect endianness at compile time, and why might you prefer that?"

<details><summary>답</summary>

GCC and Clang predefine `__BYTE_ORDER__` and `__ORDER_LITTLE_ENDIAN__`, so `#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__` removes the branch entirely. But the best code doesn't need to know: if you always assemble from bytes with shifts (B2), the code is correct on both orders.
한국어 핵심: 컴파일 타임 매크로가 있지만, shift로 조립하면 엔디안을 몰라도 된다.

</details>

- "A big-endian peripheral DMAs 32-bit words into a buffer. How do you fix up the whole buffer?"

<details><summary>답</summary>

Loop over the words and bswap32 each one in place, using `memcpy` into a `uint32_t` if the buffer may be unaligned. Better: many controllers have an endian-swap bit in the DMA or bus bridge, so check the TRM before doing it in software.
한국어 핵심: 워드마다 bswap32 — 먼저 하드웨어 byte-swap 설정 비트가 있는지 확인.

</details>

### 5. 채점 · 자주 틀리는 것

- 상수에 `u` / `ull` 접미사 — `0x00FF00FF00FF00FF`에 `ull` 없으면 의도와 다른 타입이 될 수 있음.
- bswap16의 `x << 8`은 int로 승격됨 → 결과를 `(uint16_t)`로 잘라야 `-Wconversion`에도 깨끗.
- 64비트에서 `(x << 32)`는 x가 `uint64_t`일 때만 OK — `uint32_t`에 32 시프트는 UB.
- 니블 뒤집기(N02)와 바이트 뒤집기를 섞지 않기: 예시 출력 `ef` vs `fe`로 단위 확인.
- 엔디안 판별에서 `int`와 `char` 크기 가정 대신 `uint32_t`와 `uint8_t` 사용.

## B2 · 바이트 배열 ↔ 정수 (LE/BE, 정렬 무관) (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 바이트 버퍼의 임의 위치에서 1–8바이트를 읽어 정수로 만들고(LE/BE 둘 다), 반대로 정수를 버퍼에 쓰기.
- **상황**: 패킷·EEPROM 덤프·센서 FIFO(24-bit ADC 샘플 등)에서 값이 홀수 주소에 걸쳐 있는 경우가 흔하다. 캐스트로 읽으면 Cortex-M0 같은 코어에서 unaligned fault가 나고, strict aliasing 위반이며, 호스트 엔디안에 따라 값이 바뀐다.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `load_le` | p[0]이 LSB인 n바이트 → 정수 | n = 1..8, 바이트 단위 접근만 |
| `load_be` | p[0]이 MSB인 n바이트 → 정수 | 위와 같음 |
| `store_le` | 정수 하위 n바이트 → p[0]부터 LSB | 상위 바이트는 버림 |
| `store_be` | 정수 하위 n바이트 → p[0]부터 MSB | 위와 같음 |

- **예시를 말로 풀면**: 바이트 `12 34 56`을 big-endian으로 읽으면 첫 바이트가 가장 큰 자리 → `0x123456`. little-endian으로 읽으면 마지막 바이트가 가장 큰 자리 → `0x563412`.
- **지켜야 할 조건**: 포인터 캐스트 금지, 호스트 엔디안과 무관, 8바이트까지 오버플로 없음, 부호 확장 없음.

### 1. Problem (English)

"We parse a byte stream where fields can start at any offset. Write load/store helpers for 1 to 8 byte little- and big-endian unsigned values. Don't assume alignment or host endianness."

```c
uint64_t load_le(const uint8_t *p, size_t n);
uint64_t load_be(const uint8_t *p, size_t n);
void store_le(uint8_t *p, uint64_t v, size_t n);
void store_be(uint8_t *p, uint64_t v, size_t n);
```

- `buf = EE 12 34 56 78`, `load_le(&buf[1], 4)` → `0x78563412`, `load_be(&buf[1], 4)` → `0x12345678`
- `load_be(p, 3)` on `12 34 56` → `0x123456` (24-bit ADC)
- `store_be(p, 0x123456, 3)` → `12 34 56`

### 2. 먼저 물어볼 것 (clarify)

- "Is n always between 1 and 8?" → Yes, assume valid; n=0 returns 0 naturally.
- "Unsigned only, or do you want sign extension for signed fields?" → Unsigned; sign-extend separately (see [N05A A6](2026-11-01_N05A_bit_basics.html)).
- "Can the buffer be unaligned?" → Yes, any address.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (n 범위, 부호, 정렬) |
| 2–5 | `12 34 56` 손으로 BE/LE 조립 |
| 5–15 | load_be → load_le (역순 루프) → store 둘 |
| 15–18 | load_le 4스텝 trace |
| 18–20 | 테스트 + "왜 캐스트 안 하나" 설명 |

- 소리 내서: **"I build the value with byte loads and shifts, so it works at any alignment and on either host endianness — no pointer casts, no aliasing issues."**
- 소리 내서: **"Big-endian means the first byte is most significant, so I shift-and-OR in forward order; little-endian is the same loop running backwards."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

`v = (v << 8) | next_byte` 를 반복하면, 먼저 넣은 바이트가 결국 가장 높은 자리가 된다. BE는 앞에서부터, LE는 뒤에서부터 넣으면 된다.

</details>

<details><summary>힌트 2</summary>

`size_t` 역방향 루프는 `for (i = n; i-- > 0; )` 형태 — `i >= 0` 조건은 unsigned라 영원히 참. store는 `v >>= 8` 하면서 하위 바이트를 하나씩 떼어 낸다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

/* n = 1..8 bytes. Byte loads/stores only -> alignment-safe, host-endian independent. */
uint64_t load_le(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = n; i-- > 0; )                /* MSB is the LAST byte */
        v = (v << 8) | p[i];
    return v;
}

uint64_t load_be(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++)               /* MSB is the FIRST byte */
        v = (v << 8) | p[i];
    return v;
}

void store_le(uint8_t *p, uint64_t v, size_t n)
{
    for (size_t i = 0; i < n; i++, v >>= 8)
        p[i] = (uint8_t)v;
}

void store_be(uint8_t *p, uint64_t v, size_t n)
{
    for (size_t i = n; i-- > 0; v >>= 8)
        p[i] = (uint8_t)v;
}
```

- 시간 O(n), 공간 O(1). `v`가 `uint64_t`라 `p[i]`(int 승격)를 OR해도 부호 문제 없음.
- 고정 32비트 버전(get_le32/get_be32/put_le32)은 [C 정리 C8](2026-10-29_c_basics_cram.html).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`load_le(&buf[1], 4)`, 바이트 = `12 34 56 78`:

| i | p[i] | v (after) |
|---|---|---|
| 3 | `0x78` | `0x78` |
| 2 | `0x56` | `0x7856` |
| 1 | `0x34` | `0x785634` |
| 0 | `0x12` | `0x78563412` ✔ |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: 홀수 offset(`&buf[1]`)에서 4바이트 LE/BE.
- zero / empty: n=0 → 0; n=1 → LE·BE 같은 값.
- boundary: n=8 (`0x123456789ABCDEF0`), 모든 바이트 `FF` → `~0ull`, `load_be(ff,4)` = `0xFFFFFFFF` (부호 확장 없음).
- truncation: `store_le(p, 0xAABBCCDD, 2)` → `DD CC`만 쓰고 이웃 바이트 안 건드림.
- round-trip: n=1..8 각각 store → load가 원래 값.

</details>

### 4. 꼬리 질문

- "Why not just `*(uint32_t *)&buf[1]`?"

<details><summary>답</summary>

Three problems: an unaligned load can fault on cores like Cortex-M0 or on device memory, it violates strict aliasing so the compiler may reorder or drop it, and it gives a host-endian value. The shift version is portable; if I want one load, I `memcpy` into a `uint32_t` and then byte-swap if needed — compilers turn that into a single `LDR` (+ `REV`).
한국어 핵심: 정렬 fault · strict aliasing · 엔디안 3중 문제 → shift 또는 memcpy.

</details>

- "How would you read a signed 24-bit big-endian sample?"

<details><summary>답</summary>

`load_be(p, 3)` gives a 24-bit unsigned value; then sign-extend with `(v ^ 0x800000) - 0x800000` done in a signed 32-bit type. That's the same trick as the 12-bit ADC sign-extension in A6.
한국어 핵심: unsigned로 읽고 → `(x ^ m) - m` 으로 부호 확장 ([A6](2026-11-01_N05A_bit_basics.html)).

```c
int32_t s24 = (int32_t)(load_be(p, 3) ^ 0x800000u) - 0x800000;
```

</details>

- "This runs per byte. Is it slow?"

<details><summary>답</summary>

For n known at compile time, clang and GCC fold the loop into one load plus a byte-reverse when the target allows unaligned access. For bulk data I'd parse in larger chunks or let DMA land the data aligned. In validation code clarity usually wins over a few cycles.
한국어 핵심: 상수 n이면 컴파일러가 LDR+REV로 접어 줌 — 대량이면 정렬된 버퍼로.

</details>

### 5. 채점 · 자주 틀리는 것

- `for (size_t i = n - 1; i >= 0; i--)` — unsigned라 무한 루프 (n=0이면 바로 wrap).
- `p[i] << 24`처럼 `uint8_t`를 int 상태로 24 시프트 → `0x80 << 24`는 signed overflow (C8의 그 함정). 여기선 누적 변수가 `uint64_t`라 안전.
- LE/BE 루프 방향을 반대로 — 예시 하나 손으로 trace하면 바로 잡힘.
- store에서 n보다 많이 쓰거나 상위 바이트를 남기는 실수.

## B3 · 센서 패킷 구조체 → wire 포맷 직렬화/역직렬화 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: IMU(가속도) 샘플 구조체를 정해진 **22바이트 big-endian 바이트열**로 바꾸고, 바이트열을 다시 구조체로 되돌리기.
- **상황**: 카메라·IMU·레이더 샘플은 timestamp·sensor id·flags·sequence 헤더를 달고 SoC 사이, 또는 차량 Ethernet으로 이동한다 (Rivian의 실제 포맷은 모름 — 이 레이아웃은 연습용 [추정]). validation에서는 이 헤더를 직접 만들고 파싱해서 seq gap·timestamp jitter를 본다 ([I1](2026-11-01_N05I_validation_c.html), [G7](2026-11-01_N05G_state_machines_parsers.html)과 연결).
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `imu_serialize` | 구조체 → 22바이트 BE | cap 부족이면 0 반환, 아니면 22 |
| `imu_deserialize` | 22바이트 → 구조체 | 길이 부족 또는 magic ≠ 0xA5면 false |
| (헬퍼) `put_be16/32/64`, `get_be16/32/64` | 정수 ↔ BE 바이트 | 바이트 단위만 |

- **wire 레이아웃**

| offset | 크기 | 필드 |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | sensor_id |
| 2 | 2 | flags |
| 4 | 4 | seq |
| 8 | 8 | timestamp_ns |
| 16 | 6 | accel x, y, z (int16 ×3) |

- **예시를 말로 풀면**: seq가 `0x01020304`면 offset 4부터 `01 02 03 04`가 그대로 (큰 자리 먼저). accel `-1`은 `FF FF`, `-32768`은 `80 00`.
- **지켜야 할 조건**: 구조체 패딩·호스트 엔디안과 무관, 버퍼 정렬 무관, signed 변환에서 UB/구현 정의 동작 없음.

### 1. Problem (English)

"Here's our IMU sample struct. The wire format is fixed: 22 bytes, big-endian, magic byte first. Write serialize and deserialize. It has to work no matter how the compiler lays out the struct."

```c
typedef struct {
    uint8_t  sensor_id;
    uint16_t flags;
    uint32_t seq;
    uint64_t timestamp_ns;
    int16_t  accel[3];
} imu_sample_t;
size_t imu_serialize(const imu_sample_t *s, uint8_t *buf, size_t cap);
bool   imu_deserialize(const uint8_t *buf, size_t len, imu_sample_t *s);
```

- id=3, flags=1, seq=`0x01020304`, ts=`0x123456789`, accel={-1, 256, -32768} → `A5 03 00 01 01 02 03 04 00 00 00 01 23 45 67 89 FF FF 01 00 80 00`
- `imu_serialize(&s, buf, 21)` → 0
- deserialize of a buffer starting with `5A` → false

### 2. 먼저 물어볼 것 (clarify)

- "Is the wire format big-endian for every field, including the accel samples?" → Yes, all big-endian.
- "Is there a CRC or length field I should add?" → Not now; mention it as an extension.
- "On deserialize, what should I validate?" → Length and magic; reject otherwise.
- "Can the buffer be unaligned (e.g. inside a larger frame)?" → Yes.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (엔디안, 검증 항목, 정렬) |
| 2–5 | offset 표 그리기 + "왜 memcpy(struct) 안 되나" 한 문장 |
| 5–15 | put/get_be16 → 32/64를 16으로 조합 → serialize → deserialize |
| 15–18 | 예시의 seq, accel[0] 바이트 trace |
| 18–20 | 테스트 + CRC/버전 필드 확장 |

- 소리 내서: **"I never memcpy the struct — padding and host endianness would leak onto the wire. I write each field at a fixed offset with big-endian helpers."**
- 소리 내서: **"Converting int16 to uint16 is well-defined modulo 2^16; the reverse is implementation-defined for negative values, so I map it explicitly."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

offset 표를 먼저 쓰고, `put_be16` 하나만 제대로 만들면 `put_be32 = put_be16(상위) + put_be16(하위)`, `put_be64 = put_be32 ×2`로 쌓을 수 있다. get도 같은 구조.

</details>

<details><summary>힌트 2</summary>

음수 accel: 보낼 때는 `(uint16_t)s->accel[i]`로 충분 (모듈로 변환, 정의됨). 받을 때 `u >= 0x8000`이면 `u - 0x10000`을 `int32_t`로 계산한 뒤 `int16_t`로 — 이 값은 범위 안이라 변환이 정의됨.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define IMU_MAGIC     0xA5u
#define IMU_WIRE_LEN  22u       /* fixed wire size, independent of sizeof(imu_sample_t) */

typedef struct {
    uint8_t  sensor_id;
    uint16_t flags;             /* bit0 valid, bit1 saturated, ... */
    uint32_t seq;
    uint64_t timestamp_ns;
    int16_t  accel[3];          /* raw counts x, y, z */
} imu_sample_t;                 /* in-memory layout: padding is the compiler's choice */

static void put_be16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)(v >> 8); b[1] = (uint8_t)v; }
static void put_be32(uint8_t *b, uint32_t v) { put_be16(b, (uint16_t)(v >> 16)); put_be16(b + 2, (uint16_t)v); }
static void put_be64(uint8_t *b, uint64_t v) { put_be32(b, (uint32_t)(v >> 32)); put_be32(b + 4, (uint32_t)v); }
static uint16_t get_be16(const uint8_t *b) { return (uint16_t)((b[0] << 8) | b[1]); }
static uint32_t get_be32(const uint8_t *b) { return ((uint32_t)get_be16(b) << 16) | get_be16(b + 2); }
static uint64_t get_be64(const uint8_t *b) { return ((uint64_t)get_be32(b) << 32) | get_be32(b + 4); }

size_t imu_serialize(const imu_sample_t *s, uint8_t *buf, size_t cap)
{
    if (cap < IMU_WIRE_LEN)
        return 0;
    buf[0] = IMU_MAGIC;
    buf[1] = s->sensor_id;
    put_be16(&buf[2], s->flags);
    put_be32(&buf[4], s->seq);
    put_be64(&buf[8], s->timestamp_ns);
    for (int i = 0; i < 3; i++)                  /* int16 -> uint16 is well-defined (mod 2^16) */
        put_be16(&buf[16 + 2 * i], (uint16_t)s->accel[i]);
    return IMU_WIRE_LEN;
}

bool imu_deserialize(const uint8_t *buf, size_t len, imu_sample_t *s)
{
    if (len < IMU_WIRE_LEN || buf[0] != IMU_MAGIC)
        return false;
    s->sensor_id    = buf[1];
    s->flags        = get_be16(&buf[2]);
    s->seq          = get_be32(&buf[4]);
    s->timestamp_ns = get_be64(&buf[8]);
    for (int i = 0; i < 3; i++) {                /* portable uint16 -> int16 */
        uint16_t u = get_be16(&buf[16 + 2 * i]);
        s->accel[i] = (int16_t)(u >= 0x8000u ? (int32_t)u - 0x10000 : (int32_t)u);
    }
    return true;
}
```

- 시간 O(1) (고정 22바이트), 공간 O(1).
- 실측: 이 Mac(arm64 clang)에서 `sizeof(imu_sample_t)` = **24** (tail padding), wire = 22 → struct memcpy가 틀린 이유를 숫자로 보여 줌.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 필드 | 값 | 헬퍼 | buf[offset..] |
|---|---|---|---|
| magic | `0xA5` | 직접 | [0] `A5` |
| flags | `0x0001` | put_be16 | [2] `00` [3] `01` |
| seq | `0x01020304` | put_be32 → be16(`0x0102`), be16(`0x0304`) | [4..7] `01 02 03 04` |
| accel[0] | `-1` → `(uint16_t)` `0xFFFF` | put_be16 | [16] `FF` [17] `FF` |
| (역) accel[2] | [20..21] `80 00` → u=`0x8000` ≥ `0x8000` | u − 0x10000 | −32768 ✔ |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: 예시 → 정확한 22바이트 `memcmp`, 그리고 deserialize 왕복이 같은 구조체.
- boundary: seq `0xFFFFFFFF`, ts `~0ull`, accel `{32767, -32768, 0}`, flags `0xFFFF` 왕복.
- error: cap 21 → 0, len 21 → false, magic `0x5A` → false.
- alignment: 버퍼를 `&raw[1]`(홀수 주소)에 두고 ASan/UBSan 깨끗.
- struct ≠ wire: `sizeof(imu_sample_t) != 22` 확인 (24 출력).

</details>

### 4. 꼬리 질문

- "Why not `__attribute__((packed))` and memcpy?"

<details><summary>답</summary>

Packed fixes the padding but not the byte order — the wire would be host-endian — and it's a compiler extension. Packed members can also be misaligned, so taking their address or accessing them on strict-alignment cores generates slow or faulting code. Field-by-field serialization is portable and explicit.
한국어 핵심: packed는 패딩만 해결, 엔디안·이식성·정렬 문제는 남는다.

</details>

- "How would you make this format evolve without breaking old receivers?"

<details><summary>답</summary>

Put a version byte and a length field in the header, and have receivers skip unknown trailing bytes using the length — that's the TLV idea in G3. Never reorder existing fields; only append.
한국어 핵심: version + length 필드 → 새 필드는 뒤에만 추가 ([G3 TLV](2026-11-01_N05G_state_machines_parsers.html)).

</details>

- "How do you detect corruption on the link?"

<details><summary>답</summary>

Append a CRC over the 22 bytes — CRC-16 or CRC-32 depending on the link — and check it before trusting any field. On automotive Ethernet the frame already has an FCS, but an end-to-end CRC plus sequence counter (AUTOSAR E2E style) also catches corruption inside the software stack [추정 — Rivian 사용 여부 모름].
한국어 핵심: 끝에 CRC + seq 카운터로 end-to-end 보호 ([F 페이지](2026-11-01_N05F_memtest_integrity.html)).

</details>

- "Where does `timestamp_ns` come from, and what can go wrong?"

<details><summary>답</summary>

Ideally from a time base synchronised across ECUs, e.g. gPTP (IEEE 802.1AS) on automotive Ethernet, captured in hardware at sample time [추정 — Rivian 구현 모름]. Problems: a free-running counter that wraps, timestamps taken in software after the interrupt latency, and clock domain mismatch between sensors.
한국어 핵심: 동기화된 하드웨어 타임스탬프가 이상적 — wrap · ISR 지연 · 도메인 불일치 주의 ([I1](2026-11-01_N05I_validation_c.html)).

</details>

### 5. 채점 · 자주 틀리는 것

- `memcpy(buf, s, sizeof *s)` — 패딩·엔디안이 그대로 나감 (24 ≠ 22).
- `get_be32`에서 `(uint32_t)` 캐스트 없이 `<< 16` / `<< 24` — int 승격 후 signed overflow 가능.
- `(int16_t)u` 직접 캐스트 — C11에선 범위 밖이면 구현 정의 (대부분 동작하지만 "왜 안전한지" 말할 수 있어야).
- 길이 체크를 magic 체크보다 늦게 — `buf[0]`을 읽기 전에 `len` 확인.
- offset 계산 실수 (`16 + 2*i`) — 표를 먼저 그리면 방지.

## B4 · 비트맵 할당기 (첫 번째 0 비트) (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: `uint32_t` 배열을 비트맵으로 써서, **가장 낮은 번호의 빈 슬롯**을 찾아 할당하고, 해제할 때 잘못된 해제를 거부.
- **상황**: DMA descriptor 슬롯, 태그 번호(NVMe/SSD 커맨드 태그 — Don의 SSD 경험), IRQ 벡터, 버퍼 풀 인덱스 관리. Linux의 `find_first_zero_bit`와 같은 일.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `bm_alloc` | 첫 0 비트 → 1로, 인덱스 반환 | 없으면 -1, `nbits` 밖은 절대 할당 안 함 |
| `bm_free` | 해당 비트 1 → 0 | 범위 밖 또는 이미 0이면 false |

- **예시를 말로 풀면**: 40개 슬롯, word0은 32개 모두 사용 중, word1은 아래 3비트(32·33·34번) 사용 중 → 다음 빈 것은 35번. 할당 후 word1 = `0xF`.
- **지켜야 할 조건**: `nbits`가 32 배수가 아니어도 됨 (마지막 word의 남는 비트), 꽉 찬 word는 한 번에 건너뛰기, `1u` 시프트.

### 1. Problem (English)

"We have up to `nbits` hardware slots tracked in a bitmap, one bit per slot, 1 means in use. Write alloc that returns the lowest free slot, and free that catches double frees."

```c
int  bm_alloc(uint32_t *map, size_t nbits);
bool bm_free(uint32_t *map, size_t nbits, size_t idx);
```

- `map={0xFFFFFFFF, 0x7}`, nbits=40 → 35
- `map={0xFFFFFFFF, 0xFF}`, nbits=40 → -1 (bits 40..63 exist in memory but aren't slots)
- `bm_free(map, 40, 39)` twice → true, then false

### 2. 먼저 물어볼 것 (clarify)

- "Lowest free index, or any free index?" → Lowest (deterministic).
- "Are the unused tail bits in the last word guaranteed zero?" → Don't rely on it.
- "Is this called from an ISR too?" → Assume single context for now; discuss locking as a follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (lowest? tail bits? ISR?) |
| 2–5 | 예시 word1 = `0x7` → `~` = `…FFF8` → ctz = 3 말하기 |
| 5–15 | ctz32 → bm_alloc (word skip, tail check) → bm_free |
| 15–18 | 예시 trace |
| 18–20 | 테스트 + ctz builtin/atomic 확장 |

- 소리 내서: **"I invert each word, so free slots become ones; a word that inverts to zero is full and I skip all 32 at once. Then count trailing zeros to get the bit."**
- 소리 내서: **"The last word may have padding bits, so I reject any index at or beyond nbits."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

"첫 0 비트"는 `~word`의 "첫 1 비트". 첫 1 비트 위치 = trailing zero 개수.

</details>

<details><summary>힌트 2</summary>

인덱스 = `w * 32 + ctz(~map[w])`. 이게 `nbits` 이상이면 남은 건 패딩뿐 → -1. free는 `map[idx/32]`의 `1u << (idx%32)`가 이미 0인지 먼저 확인.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* bit = 1 means allocated. nbits need not be a multiple of 32. */
static unsigned ctz32(uint32_t x)                /* precondition: x != 0 */
{
    unsigned n = 0;
    while ((x & 1u) == 0) {
        x >>= 1;
        n++;
    }
    return n;
}

int bm_alloc(uint32_t *map, size_t nbits)
{
    size_t nwords = (nbits + 31) / 32;
    for (size_t w = 0; w < nwords; w++) {
        uint32_t free_bits = ~map[w];
        if (free_bits == 0)
            continue;                            /* word full: skip 32 at once */
        size_t idx = w * 32 + ctz32(free_bits);
        if (idx >= nbits)
            return -1;                           /* only tail padding was free */
        map[w] |= 1u << (idx % 32);
        return (int)idx;
    }
    return -1;
}

bool bm_free(uint32_t *map, size_t nbits, size_t idx)
{
    if (idx >= nbits)
        return false;
    uint32_t bit = 1u << (idx % 32);
    if ((map[idx / 32] & bit) == 0)
        return false;                            /* double free */
    map[idx / 32] &= ~bit;
    return true;
}
```

- 시간 O(nbits/32 + 32) (word 스캔 + ctz 루프; builtin ctz면 O(nbits/32)), 공간 O(1).
- 반환형 `int`라 `nbits ≤ INT_MAX` 가정.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`map = {0xFFFFFFFF, 0x00000007}`, nbits = 40, nwords = 2:

| w | map[w] | free_bits = ~map[w] | 판단 | idx |
|---|---|---|---|---|
| 0 | `0xFFFFFFFF` | `0x00000000` | full → skip | — |
| 1 | `0x00000007` | `0xFFFFFFF8` | ctz = 3 | 32 + 3 = 35 < 40 ✔ |
| — | map[1] ⟵ `0x7` OR `1u<<3` | | | `0x0000000F`, return 35 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: 예시 → 35.
- 순서: 빈 비트맵에서 40번 alloc → 0,1,…,39 순서, 41번째 → -1 (tail 40..63 안 줌).
- free + 재사용: 17, 3 해제 → 다음 alloc은 3, 그 다음 17 (lowest-first).
- error: idx 40 free → false, 같은 idx 두 번 free → 두 번째 false.
- boundary: nbits = 32에서 bit 31 할당 (`1u << 31` — `1 << 31`이면 UB), nbits = 0 → -1.

</details>

### 4. 꼬리 질문

- "Make it faster."

<details><summary>답</summary>

Replace my ctz loop with `__builtin_ctz`, which is `RBIT` + `CLZ` on ARM — two instructions. For large maps, keep a hint of the last word that had a free bit, or a second-level summary bitmap where each bit says "this word has a free slot".
한국어 핵심: builtin ctz (ARM `RBIT`+`CLZ`) + 시작 힌트 또는 2단 비트맵.

</details>

- "The ISR also allocates. What breaks and how do you fix it?"

<details><summary>답</summary>

`map[w] |= bit` is a read-modify-write, so main and ISR can both see the same free bit and both "allocate" it. Fix with a short IRQ-disable around find+set, or a lock-free loop with LDREX/STREX (C11 `atomic_compare_exchange_weak`) that retries if the word changed.
한국어 핵심: RMW 레이스 → 짧은 IRQ off 또는 CAS 재시도 ([E7](2026-11-01_N05E_ring_queue_isr.html)).

</details>

- "Allocate k contiguous slots instead of one."

<details><summary>답</summary>

Scan for a run of k zero bits: walk bit by bit, count consecutive zeros, reset on a one, and return the start when the count reaches k; then set the range. For k ≤ 32 within a word you can test `(map & (mask_k << pos)) == 0`. That's essentially Linux `bitmap_find_next_zero_area`.
한국어 핵심: 연속 0 k개 탐색 → 범위 set (Linux `bitmap_find_next_zero_area`).

</details>

### 5. 채점 · 자주 틀리는 것

- `1 << 31` (signed) → UB. 항상 `1u`.
- 마지막 word의 tail 비트를 할당해 버리는 실수 (`idx >= nbits` 체크 누락).
- `~map[w] == 0` 체크 없이 ctz → ctz(0)은 무한 루프 / builtin이면 UB.
- double free 감지 누락 — validation에서는 이게 버그를 잡는 장치.
- `nwords = nbits / 32` (올림 안 함) → 마지막 부분 word를 못 봄.

## B5 · Gray code ↔ binary 변환 (★★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 이진수를 Gray code(인접 값이 1비트만 다른 코드)로 바꾸고, 다시 이진수로 되돌리기.
- **상황**: 비동기 FIFO의 read/write 포인터를 다른 클럭 도메인으로 넘길 때(CDC), 로터리 엔코더·앱솔루트 엔코더 값 읽을 때. silicon validation에서 FIFO 포인터 레지스터를 덤프하면 Gray로 나와서 직접 풀어야 하는 경우가 있다.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `bin_to_gray` | binary → Gray | 한 줄 |
| `gray_to_bin` | Gray → binary | 루프 없이 (5단계) |

- **예시를 말로 풀면**: 5 = `101` → 각 비트를 "자기 XOR 왼쪽 이웃" → `111` = 7. 반대로 Gray `100` → 위에서부터 누적 XOR → `1`, `1^0=1`, `1^0=1` → `111` = 7.
- **지켜야 할 조건**: 32비트 전 범위, 연속한 값의 Gray는 정확히 1비트 차이 (wrap 포함).

### 1. Problem (English)

"An async FIFO exposes its pointers in Gray code. Write the conversions both ways."

```c
uint32_t bin_to_gray(uint32_t b);
uint32_t gray_to_bin(uint32_t g);
```

- `bin_to_gray(5)` → 7, `bin_to_gray(8)` → 12
- `gray_to_bin(4)` → 7
- `bin_to_gray(0xFFFFFFFF)` → `0x80000000`

### 2. 먼저 물어볼 것 (clarify)

- "Standard reflected binary Gray code?" → Yes.
- "Full 32-bit width, or the FIFO pointer width?" → 32-bit; narrower values work since high bits are 0.
- "Is a loop OK for the inverse, or do you want O(log n)?" → Give both.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify |
| 2–5 | 0..7 Gray 표 손으로 (000 001 011 010 110 111 101 100) |
| 5–10 | 한 줄 encode → loop decode → shift 5단계 decode |
| 10–13 | gray_to_bin(4) trace |
| 13–15 | 테스트 + FIFO 이야기 |

- 소리 내서: **"Encoding is b XOR b shifted right by one. Decoding is a prefix XOR from the top bit down; I can do it with five doubling shifts instead of 32 iterations."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

decode: binary의 bit i = Gray의 bit 31부터 bit i까지 전부 XOR. 즉 `g ^ (g>>1) ^ (g>>2) ^ … ^ (g>>31)`.

</details>

<details><summary>힌트 2</summary>

그 31번 XOR을 "2배씩" 접기: 시프트 1, 2, 4, 8, 16으로 다섯 번 `g ^= g >> s` 하면 모든 상위 비트가 누적된다 (순서는 어느 쪽이든 결과 같음).

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

uint32_t bin_to_gray(uint32_t b)
{
    return b ^ (b >> 1);
}

uint32_t gray_to_bin(uint32_t g)                 /* b[i] = XOR of g[31..i] (prefix XOR) */
{
    g ^= g >> 16;
    g ^= g >> 8;
    g ^= g >> 4;
    g ^= g >> 2;
    g ^= g >> 1;
    return g;
}

uint32_t gray_to_bin_loop(uint32_t g)            /* paper version: O(width) */
{
    uint32_t b = 0;
    for (; g != 0; g >>= 1)
        b ^= g;
    return b;
}
```

- encode O(1), decode O(log w) = 5단계 (loop 버전 O(w)), 공간 O(1).
- 시프트 순서는 16→1이든 1→16이든 결과 같음 (테스트로 확인한 쪽은 16→1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`gray_to_bin(4)`, g = `100`:

| 단계 | 연산 | g |
|---|---|---|
| 0 | 입력 | `100` |
| 1 | `^= g>>16`, `>>8`, `>>4` | `100` (상위 비트 없음) |
| 2 | `^= g>>2` → `100 ^ 001` | `101` |
| 3 | `^= g>>1` → `101 ^ 010` | `111` = 7 ✔ |

loop 버전: b=0 → `^100` = `100` → `^010` = `110` → `^001` = `111`.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: 5→7, 7→4, 8→12, 그리고 역변환.
- zero: 0 ↔ 0.
- round-trip: 0..2^20 전부 + 상위 값 20만 개 `gray_to_bin(bin_to_gray(b)) == b` (두 decode 버전 모두).
- Gray 성질: 이웃 `b`, `b+1`의 Gray XOR의 popcount == 1.
- wrap: `0xFFFFFFFF` → 0 도 1비트 차이; 4비트 FIFO(깊이 16) 15→0도 1비트.

</details>

### 4. 꼬리 질문

- "Why does an async FIFO use Gray pointers?"

<details><summary>답</summary>

The pointer is sampled by a synchroniser in another clock domain. If several bits change at once, the receiver can catch a mix of old and new bits — a value that never existed. With Gray code only one bit changes per increment, so the sampled value is either the old or the new pointer, both safe.
한국어 핵심: 1비트만 바뀌므로 CDC 샘플링 결과가 "이전 또는 새 값" 둘 중 하나.

</details>

- "Does it still work if the FIFO depth isn't a power of two?"

<details><summary>답</summary>

Not directly: the wrap from N−1 back to 0 must also be a one-bit change, which only holds for 2^k counts with plain reflected Gray. For other depths you need a special sequence (e.g. a symmetric subset of the Gray cycle) or a different handshake scheme.
한국어 핵심: wrap도 1비트여야 하므로 2의 거듭제곱 깊이에서만 그대로 됨.

</details>

### 5. 채점 · 자주 틀리는 것

- `b ^ (b << 1)` (방향 반대) — 예시 5→7로 확인.
- decode를 `g ^ (g >> 1)` 한 번으로 끝내는 실수 (encode와 혼동).
- loop decode에서 `b ^= g` 후 `g >>= 1` 순서 — 둘 다 해야 함, signed 타입이면 `>>` 부호 확장 위험.
- full/empty 비교를 Gray 그대로 하는 경우 상위 2비트 규칙 필요 (Cummings 방식) — 말로 언급만.

## B6 · 비트 스트림 writer/reader (MSB-first) (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 1–32비트짜리 필드를 바이트 경계와 상관없이 **이어 붙여 쓰고**, 같은 순서로 **다시 읽기**. 비트 순서는 MSB-first.
- **상황**: CAN 신호(Motorola/big-endian 신호 배치), 압축 헤더, JTAG 체인 데이터, 센서 설정 비트 필드, H.264/HEVC 헤더 파싱처럼 바이트에 맞춰지지 않은 필드.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `bw_put` | val의 하위 nbits를 스트림에 추가 | nbits 1..32, 용량 넘으면 false (아무것도 안 씀) |
| `bw_flush` | 남은 비트를 0으로 채워 마지막 바이트 출력 | 총 바이트 수 반환 |
| `br_get` | 다음 nbits를 읽어 out에 | 비트 부족하면 false |

- **예시를 말로 풀면**: `101`, `11`, `111111111` 를 이어 쓰면 14비트 `10111111111111` → 앞 8비트 `10111111` = `0xBF`, 나머지 6비트 `111111` + 패딩 `00` = `0xFC`.
- **지켜야 할 조건**: val의 남는 상위 비트는 무시, 32비트 시프트 UB 없음, 버퍼 넘침 없음.

### 1. Problem (English)

"Write a bit writer that packs fields of arbitrary width, 1 to 32 bits, MSB-first, into a byte buffer, plus the matching reader."

```c
typedef struct { uint8_t *buf; size_t cap; size_t pos; uint64_t acc; unsigned nacc; } bitwriter_t;
typedef struct { const uint8_t *buf; size_t len; size_t pos; uint64_t acc; unsigned nacc; } bitreader_t;
bool   bw_put(bitwriter_t *w, uint32_t val, unsigned nbits);
size_t bw_flush(bitwriter_t *w);
bool   br_get(bitreader_t *r, unsigned nbits, uint32_t *out);
```

- put(5,3), put(3,2), put(0x1FF,9), flush → `BF FC`, returns 2
- reading back 3, 2, 9 bits → 5, 3, 0x1FF; then 2 bits → 0 (padding); then 1 bit → false
- put(1,1), put(0xDEADBEEF,32) into 5-byte buffer, then put(0xFF,8) → false (41 > 40 bits)

### 2. 먼저 물어볼 것 (clarify)

- "MSB-first within each byte and within each field?" → Yes.
- "If val has bits above nbits, mask them or reject?" → Mask.
- "On overflow, partial write or nothing?" → Nothing; return false.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (비트 순서, 마스크, overflow 정책) |
| 2–5 | 예시 14비트를 종이에 쓰고 8비트씩 자르기 |
| 5–15 | writer (acc/nacc, emit 루프, flush) → reader (refill 루프) |
| 15–18 | writer trace 3 put + flush |
| 18–20 | 테스트 + LSB-first 변형 |

- 소리 내서: **"I keep an accumulator with a count of pending bits. Each put shifts the new field in at the bottom; whenever I have eight or more bits, I emit the top byte."**
- 소리 내서: **"The accumulator is 64-bit: at most 7 pending bits plus 32 new ones, so it never overflows, and I build the mask without shifting by 32."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

가장 단순한 버전은 비트 하나씩: `for i = nbits-1 .. 0: bit = (val >> i) & 1` 을 `buf[bitpos/8]`의 `7 - bitpos%8` 위치에 넣기. 시간 없으면 이걸로 먼저 맞히고 accumulator로 개선 제안.

</details>

<details><summary>힌트 2</summary>

writer: `acc = (acc << n) | val_masked; nacc += n; while (nacc >= 8) { nacc -= 8; emit acc >> nacc; }`. 마스크는 `0xFFFFFFFFu >> (32 - n)` (n ≥ 1이라 시프트 ≤ 31). reader는 반대로 `nacc < n`인 동안 바이트를 넣는다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buf; size_t cap; size_t pos;        /* pos = bytes emitted */
    uint64_t acc; unsigned nacc;                 /* pending bits, nacc < 8 between calls */
} bitwriter_t;

bool bw_put(bitwriter_t *w, uint32_t val, unsigned nbits)        /* MSB-first, nbits 1..32 */
{
    if (nbits == 0 || nbits > 32)
        return false;
    if ((w->pos * 8 + w->nacc + nbits + 7) / 8 > w->cap)
        return false;                            /* would not fit even after flush */
    w->acc = (w->acc << nbits) | (val & (0xFFFFFFFFu >> (32 - nbits)));
    w->nacc += nbits;
    while (w->nacc >= 8) {
        w->nacc -= 8;
        w->buf[w->pos++] = (uint8_t)(w->acc >> w->nacc);
    }
    w->acc &= (1ull << w->nacc) - 1u;            /* keep only the pending bits */
    return true;
}

size_t bw_flush(bitwriter_t *w)                  /* zero-pad last byte, return total bytes */
{
    if (w->nacc) {
        w->buf[w->pos++] = (uint8_t)(w->acc << (8 - w->nacc));
        w->acc = 0;
        w->nacc = 0;
    }
    return w->pos;
}

typedef struct {
    const uint8_t *buf; size_t len; size_t pos;
    uint64_t acc; unsigned nacc;
} bitreader_t;

bool br_get(bitreader_t *r, unsigned nbits, uint32_t *out)       /* MSB-first, nbits 1..32 */
{
    if (nbits == 0 || nbits > 32 || (r->len - r->pos) * 8 + r->nacc < nbits)
        return false;
    while (r->nacc < nbits) {
        r->acc = (r->acc << 8) | r->buf[r->pos++];
        r->nacc += 8;
    }
    r->nacc -= nbits;
    *out = (uint32_t)(r->acc >> r->nacc) & (0xFFFFFFFFu >> (32 - nbits));
    r->acc &= (1ull << r->nacc) - 1u;
    return true;
}
```

- 시간 O(nbits/8) per call, 공간 O(1). 용량 체크는 "flush까지 포함한 최종 바이트 수"로 해서 flush가 절대 넘치지 않음.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 호출 | acc (2진, pending만) | nacc | 출력 |
|---|---|---|---|
| put(`101`, 3) | `101` | 3 | — |
| put(`11`, 2) | `10111` | 5 | — |
| put(`111111111`, 9) | `10111111111111` (14비트) | 14 → 6 | `acc >> 6` = `10111111` = `0xBF` |
| (mask) | `111111` | 6 | — |
| flush | `111111 << 2` | 0 | `11111100` = `0xFC` |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: 예시 → `BF FC`, 그리고 같은 폭으로 읽어 원래 값.
- empty / 패딩: flush 후 남은 2비트 = 0, 그 다음 읽기 → false.
- boundary: 1비트 + 32비트 필드 (5바이트에 걸침), 정확히 40비트 채우기 OK / 41비트 → false.
- error: nbits 0 또는 33 → false (writer, reader 둘 다).
- masking: put(`0xFFFFFFF0`, 4) → 하위 4비트 0만 기록.
- round-trip: 폭 1..32 섞어 128필드 쓰고 읽기.

</details>

### 4. 꼬리 질문

- "Now make it LSB-first, like CAN Intel-format signals or DEFLATE."

<details><summary>답</summary>

The accumulator fills from the top instead: `acc |= (uint64_t)val << nacc; nacc += n;` and I emit the low byte `acc & 0xFF`, then `acc >>= 8`. The reader takes `acc & mask` and shifts right. Same structure, opposite direction.
한국어 핵심: LSB-first = `acc |= val << nacc`, 하위 바이트부터 출력.

```c
w->acc |= (uint64_t)(val & mask) << w->nacc;  w->nacc += nbits;
while (w->nacc >= 8) { w->buf[w->pos++] = (uint8_t)w->acc; w->acc >>= 8; w->nacc -= 8; }
```

</details>

- "How do you read a signed field?"

<details><summary>답</summary>

Read it unsigned with `br_get`, then sign-extend from nbits with `(v ^ m) - m` where `m = 1u << (nbits - 1)`, computed in a signed type. Same as the A6 ADC case.
한국어 핵심: unsigned로 읽고 A6 방식으로 부호 확장.

</details>

- "What's the bit-at-a-time version and when is it fine?"

<details><summary>답</summary>

Loop over each bit and set `buf[pos >> 3] |= bit << (7 - (pos & 7))`. It's slower but trivially correct and easy to trace on paper, so in an interview I'd write it first if I'm short on time, then explain the accumulator as the optimisation.
한국어 핵심: 비트 단위 버전 먼저 → 시간 남으면 accumulator로.

</details>

### 5. 채점 · 자주 틀리는 것

- 마스크를 `(1u << nbits) - 1`로 → nbits = 32에서 UB. `0xFFFFFFFFu >> (32 - nbits)`.
- emit 후 acc의 이미 나간 상위 비트를 안 지움 → 다음 마스크가 틀릴 수 있음 (64비트라 당장은 안 보여도).
- 용량 체크를 flush 고려 없이 → 마지막 부분 바이트에서 1바이트 오버런.
- reader의 남은 비트 계산 `(len - pos) * 8 + nacc` 에서 nacc 빠뜨림.
- 비트 순서(MSB/LSB-first)를 clarify 안 하고 시작.

## B7 · 두 레지스터 값 비교 → 바뀐 비트 목록 (★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 레지스터의 이전 값과 새 값을 비교해서 **어느 비트가 0→1, 1→0으로 바뀌었는지** 목록으로 만들기.
- **상황**: 테스트 전후 레지스터 스냅샷 비교, interrupt status가 어떤 이벤트로 바뀌었는지, reset 후 기본값과 다른 비트 찾기 ([C8 레지스터 sanity](2026-11-01_N05C_register_mmio_driver.html)와 연결).
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `reg_diff` | 바뀐 비트를 LSB부터 `{bit, now_set}`으로 기록 | out에 최대 max_out개만, 반환은 전체 개수 |

- **예시를 말로 풀면**: old `0xF0`(bit 4–7 켜짐), new `0x118`(bit 3, 4, 8 켜짐) → bit 4는 그대로, bit 3·8은 새로 켜짐, bit 5·6·7은 꺼짐 → 5개.
- **지켜야 할 조건**: out 버퍼 넘침 금지, out이 NULL이고 max_out = 0이면 개수만.

### 1. Problem (English)

"You snapshot a status register before and after a test. Write a function that reports each bit that changed and whether it rose or fell."

```c
typedef struct { uint8_t bit; bool now_set; } bit_change_t;
size_t reg_diff(uint32_t old_v, uint32_t new_v, bit_change_t *out, size_t max_out);
```

- `0xF0` → `0x118`: returns 5 — {3,rise} {5,fall} {6,fall} {7,fall} {8,rise}
- equal values → 0
- `0` → `0xF` with max_out=2 → returns 4, out has bits 0 and 1 only

### 2. 먼저 물어볼 것 (clarify)

- "Order — LSB first is OK?" → Yes.
- "If the caller's array is too small, truncate or fail?" → Truncate, but return the total count.
- "Any bits to ignore, like a free-running counter field?" → Not now; good extension.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify |
| 2–4 | `0xF0 ^ 0x118 = 0x1E8` 손으로 |
| 4–9 | XOR → 비트 순회 → now_set = new의 그 비트 |
| 9–12 | trace |
| 12–15 | 테스트 + rose/fell 한 줄, ignore mask 확장 |

- 소리 내서: **"XOR gives me exactly the changed bits; for each one, the new value of that bit tells me the direction. Rose is `~old & new`, fell is `old & ~new`."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

`changed = old ^ new`. 그 다음은 1인 비트만 순회.

</details>

<details><summary>힌트 2</summary>

`changed`를 오른쪽으로 밀면서 bit 번호를 세면 `changed == 0`에서 일찍 끝난다. 방향은 `(new >> bit) & 1`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t bit;
    bool    now_set;                             /* true: 0->1, false: 1->0 */
} bit_change_t;

/* Lists changed bits LSB-first. Returns total number of changes (may exceed max_out). */
size_t reg_diff(uint32_t old_v, uint32_t new_v, bit_change_t *out, size_t max_out)
{
    uint32_t changed = old_v ^ new_v;
    size_t n = 0;
    for (unsigned bit = 0; changed != 0; bit++, changed >>= 1) {
        if ((changed & 1u) == 0)
            continue;
        if (n < max_out) {
            out[n].bit = (uint8_t)bit;
            out[n].now_set = ((new_v >> bit) & 1u) != 0;
        }
        n++;
    }
    return n;
}
```

- 시간 O(최상위 바뀐 비트 위치 + 1) ≤ 32, 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

old = `0x0F0`, new = `0x118`, changed = `0x1E8` = `1 1110 1000`:

| bit | changed bit | new bit | 기록 | n |
|---|---|---|---|---|
| 0–2 | 0 | — | skip | 0 |
| 3 | 1 | 1 | {3, 0→1} | 1 |
| 4 | 0 | — | skip | 1 |
| 5 | 1 | 0 | {5, 1→0} | 2 |
| 6, 7 | 1, 1 | 0, 0 | {6, 1→0} {7, 1→0} | 4 |
| 8 | 1 | 1 | {8, 0→1} → changed = 0, 종료 | 5 ✔ |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: 예시 5개 (실행 출력: `bit 3: 0->1`, `bit 5: 1->0`, `bit 6: 1->0`, `bit 7: 1->0`, `bit 8: 0->1`).
- zero: 같은 값 → 0.
- boundary: 0 → `0xFFFFFFFF` → 32개, bit 31만 1→0 → 1개 (bit 31 시프트 안전).
- truncation: max_out = 2인데 4개 변경 → 반환 4, 버퍼는 2개만 (ASan 깨끗).
- count-only: out = NULL, max_out = 0 → 8.

</details>

### 4. 꼬리 질문

- "Some bits are expected to change — a counter field, or self-clearing bits. How do you ignore them?"

<details><summary>답</summary>

Pass an ignore mask and compute `changed = (old ^ new) & ~ignore`. In a register test table I'd store that mask per register, alongside the RW and W1C masks, so the diff only flags unexpected changes.
한국어 핵심: `(old ^ new) & ~ignore_mask` — 레지스터 테이블에 마스크 저장.

</details>

- "How would you print bit names instead of numbers?"

<details><summary>답</summary>

Keep a table of `{bit, "NAME"}` per register, generated from the register description (IP-XACT or SystemRDL), and look up each changed bit; unknown bits print as `RSVD[n]`. Changes in reserved bits are themselves worth flagging as a silicon issue.
한국어 핵심: 비트 이름 테이블 (IP-XACT/SystemRDL에서 생성), reserved 비트 변화는 그 자체로 이슈.

</details>

### 5. 채점 · 자주 틀리는 것

- 반환값을 기록한 개수(min)로 → 호출자가 잘린 걸 모름.
- `out[n]` 쓰기 전에 `n < max_out` 체크 누락 → 버퍼 오버런.
- 방향을 old 비트로 판단하면서 반대로 해석.
- `uint8_t bit`에 32 이상 넣지 않도록 (32비트면 문제 없음 — 64비트 확장 시 타입 확인).

## B8 · Morton (Z-order) 비트 interleave 16+16 → 32 (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 16비트 x와 y의 비트를 **번갈아 끼워** 32비트 하나로 만들기 (x는 짝수 자리, y는 홀수 자리), 그리고 되돌리기.
- **상황**: 이미지/텍스처 tiling(GPU·ISP·NPU 메모리 레이아웃), 2D 좌표의 캐시·DRAM 지역성, 공간 인덱스. 카메라 파이프라인 검증에서 "tiled 포맷 버퍼를 linear로 비교"할 때 주소 계산에 나온다.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `morton_encode` | (x, y) → x 비트 i는 2i, y 비트 i는 2i+1 | 루프 없이 마스크 4단계 |
| `morton_decode` | 32비트 → (x, y) | 역연산 |

- **예시를 말로 풀면**: x = 5 = `101`, y = 3 = `011`. 위에서부터 (y2 x2)(y1 x1)(y0 x0) = (0 1)(1 0)(1 1) → `011011` = 27.
- **지켜야 할 조건**: 16비트 전 범위, x = `0xFFFF`면 `0x55555555`, y = `0xFFFF`면 `0xAAAAAAAA`.

### 1. Problem (English)

"Interleave two 16-bit coordinates into a 32-bit Morton code — x in the even bits, y in the odd bits — without a per-bit loop. Then decode."

```c
uint32_t morton_encode(uint16_t x, uint16_t y);
void     morton_decode(uint32_t m, uint16_t *x, uint16_t *y);
```

- (5, 3) → 27
- (1, 0) → 1, (0, 1) → 2, (1, 1) → 3
- (`0xFFFF`, 0) → `0x55555555`

### 2. 먼저 물어볼 것 (clarify)

- "x in the even bits, y in the odd bits?" → Yes.
- "Is a 16-iteration loop acceptable as a first version?" → Yes, but they want the mask version.
- "Any hardware instruction allowed (e.g. x86 PDEP)?" → No; mention it.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (짝/홀 배치) |
| 2–5 | (5,3) 손으로 interleave → 27 |
| 5–12 | spread16 (8, 4, 2, 1) → compact16 (역순) → encode/decode |
| 12–16 | spread16(5) 4단계 trace |
| 16–20 | 테스트 (loop 버전과 비교) + 용도 |

- 소리 내서: **"Spreading puts a zero between every bit. I do it in four steps — move the top 8 bits up by 8, then 4, 2, 1 — masking each time so groups don't overlap."**
- 소리 내서: **"Decoding is the same masks in reverse order with right shifts."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

목표: 비트 i → 비트 2i. 한 번에 못 하니까, 16비트를 8+8로 나눠 상위 8비트를 8칸 위로, 다시 각 8비트를 4+4로… 이진 분할.

</details>

<details><summary>힌트 2</summary>

마스크 순서: `0x00FF00FF` (shift 8) → `0x0F0F0F0F` (4) → `0x33333333` (2) → `0x55555555` (1). 각 단계 `x = (x | (x << s)) & mask`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

static uint32_t spread16(uint16_t v)             /* ...dcba -> .d.c.b.a (bit i -> bit 2i) */
{
    uint32_t x = v;
    x = (x | (x << 8)) & 0x00FF00FFu;
    x = (x | (x << 4)) & 0x0F0F0F0Fu;
    x = (x | (x << 2)) & 0x33333333u;
    x = (x | (x << 1)) & 0x55555555u;
    return x;
}

static uint16_t compact16(uint32_t x)            /* inverse: keep even bits, squeeze */
{
    x &= 0x55555555u;
    x = (x | (x >> 1)) & 0x33333333u;
    x = (x | (x >> 2)) & 0x0F0F0F0Fu;
    x = (x | (x >> 4)) & 0x00FF00FFu;
    x = (x | (x >> 8)) & 0x0000FFFFu;
    return (uint16_t)x;
}

uint32_t morton_encode(uint16_t x, uint16_t y)   /* x -> even bits, y -> odd bits */
{
    return spread16(x) | (spread16(y) << 1);
}

void morton_decode(uint32_t m, uint16_t *x, uint16_t *y)
{
    *x = compact16(m);
    *y = compact16(m >> 1);
}
```

- 시간 O(log 16) = 4단계, 공간 O(1). 30만 개 랜덤 (x, y)에서 비트 단위 loop 버전과 일치 확인.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

spread16(5), x = `0x0005` = `101`:

| 단계 | 연산 | x (hex) | x (2진, 하위) |
|---|---|---|---|
| 0 | 입력 | `0x0005` | `0000 0101` |
| 1 | `(x \| x<<8) & 0x00FF00FF` | `0x0005` | `0000 0101` |
| 2 | `(x \| x<<4) & 0x0F0F0F0F` | `0x0005` | `0000 0101` |
| 3 | `(x \| x<<2) & 0x33333333` | `0x0011` | `0001 0001` |
| 4 | `(x \| x<<1) & 0x55555555` | `0x0011` | `0001 0001` (bit0→0, bit2→4 ✔) |

spread16(3) = `0b101`, `<< 1` = `0b1010` → OR `0b10001` = `0b11011` = 27 ✔

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- normal: (5,3) → 27, decode(27) → (5,3).
- zero: (0,0) → 0.
- boundary: (`0xFFFF`,0) → `0x55555555`, (0,`0xFFFF`) → `0xAAAAAAAA`, 둘 다 → `0xFFFFFFFF`, (`0x8000`,0) → `0x40000000` (최상위 x 비트 → bit 30).
- single bits: (1,0)=1, (0,1)=2, (1,1)=3.
- round-trip + 기준 구현 비교: 랜덤 30만 쌍, loop 버전과 같음, decode가 원래 값.

</details>

### 4. 꼬리 질문

- "Why would hardware store images in Morton or tiled order?"

<details><summary>답</summary>

Neighbouring pixels in 2D end up close in memory, so a small square block hits the same cache lines and DRAM pages. GPUs, ISPs and accelerators use tiled or swizzled layouts for that reason, and validation code has to convert addresses to compare against a linear golden image.
한국어 핵심: 2D 이웃이 메모리에서도 가까움 → 캐시·DRAM 효율, 검증 시 주소 변환 필요.

</details>

- "Do it in 3D with 10 bits per axis."

<details><summary>답</summary>

Same idea with gaps of two: bit i goes to 3i. Masks are `0x030000FF` (shift 16), `0x0300F00F` (8), `0x030C30C3` (4), `0x09249249` (2), applied after masking the input to 10 bits; then OR `spread(x)`, `spread(y) << 1`, `spread(z) << 2` (마스크는 0..1023 전부 loop 버전과 비교해 검증함).
한국어 핵심: 3D는 비트 i → 3i, 입력 마스크 + 4단계.

</details>

- "Is there a faster way?"

<details><summary>답</summary>

On x86 with BMI2, `_pdep_u32(x, 0x55555555)` does the spread in one instruction; on ARM there's no direct equivalent, so the mask version or a 256-entry lookup table (spread one byte at a time) is typical.
한국어 핵심: x86 PDEP 1개 명령, ARM은 마스크 또는 256 LUT.

</details>

### 5. 채점 · 자주 틀리는 것

- 입력을 `uint32_t`로 넓히기 전에 시프트 → `uint16_t`는 int로 승격되지만 결과 타입 혼동; 첫 줄에서 `uint32_t x = v;`.
- 마스크 순서를 encode/decode에서 같은 방향으로 씀 (decode는 역순 + 오른쪽 시프트).
- decode 시작에 `x &= 0x55555555` 빠뜨림 → y 비트가 섞임.
- x/y 짝·홀 배치를 clarify 안 함 → 결과가 swap됨.

## 출처

이번 작성 중 웹 페이지는 조회하지 않았고, 모든 코드는 이 Mac(arm64, Apple clang, `-std=c11 -fsanitize=address,undefined`)에서 컴파일·실행으로 검증했다. 근거로 삼은 표준 레퍼런스:

1. C11 draft N1570 — 6.3.1.3 (signed/unsigned 변환), 6.5.2.3 각주 95 (union type punning), 6.5.7 (shift UB): https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
2. cppreference — Implicit conversions / integer promotion: https://en.cppreference.com/w/c/language/conversion
3. Sean Eron Anderson, Bit Twiddling Hacks (interleave bits, Gray code 관련): https://graphics.stanford.edu/~seander/bithacks.html
4. Clifford E. Cummings, "Simulation and Synthesis Techniques for Asynchronous FIFO Design" (SNUG 2002, Gray 포인터): http://www.sunburst-design.com/papers/CummingsSNUG2002SJ_FIFO1.pdf
5. Linux kernel — include/linux/bitmap.h (`find_first_zero_bit`, `bitmap_find_next_zero_area`), include/linux/unaligned.h (`get_unaligned_be32`): https://github.com/torvalds/linux/blob/master/include/linux/bitmap.h
6. GCC — Other Built-in Functions (`__builtin_bswap32`, `__builtin_ctz`, `__BYTE_ORDER__`): https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html

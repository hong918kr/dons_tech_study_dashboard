---
id: C-B13
time: 50분
updated: 2026-11-07
---
# C-B13 엔디안 판별과 바이트 스왑

## 학습 목표

- 리틀/빅 엔디안(little/big endian)에서 `0x11223344` 가 메모리에 어떻게 놓이는지 그림과 **메모리 덤프(memory dump)** 로 확인한다.
- 런타임(union, `unsigned char*`)과 컴파일타임(`__BYTE_ORDER__`) 두 가지로 엔디안을 판별하고, 각 방법이 표준상 왜 합법인지 말할 수 있다.
- `bswap16/32/64` 를 시프트로 구현하고 `__builtin_bswap*` 와 대조한다. 컴파일러가 이를 ARM `REV`/`REV16` 한 명령으로 바꾸는 것을 확인한다.
- 면접 정답인 **호스트 엔디안 무관 접근** — `read_be32()`/`write_le16()` 을 바이트 배열에서 직접 조립 — 을 쓰고, `*(uint32_t *)(buf + off)` 가 왜 틀렸는지 3가지 이유를 댄다.
- I2C 센서(MSB 먼저), Ethernet/IPv4 헤더(네트워크 바이트 순서)를 파싱한다.

## 1. 문제

펌웨어는 늘 "남이 정한 바이트 순서"의 데이터를 받는다 — I2C 센서 레지스터, CAN 신호, 네트워크 헤더, 플래시에 저장된 구조체. 바이트 순서를 한 번 틀리면 온도가 −200 °C로 나오고 IP 주소가 거꾸로 찍힌다. 면접에서는 "엔디안 판별 함수 써보세요" → "그럼 패킷에서 32비트 필드는 어떻게 읽나요?" 로 이어지는 **꼬리질문 단골**이다.

정수 `0x11223344` 는 4바이트다. **어느 바이트를 낮은 주소에 놓느냐**가 엔디안이다.

- **리틀 엔디안**: 가장 *낮은 자리* 바이트(LSB, `0x44`)를 낮은 주소에. Cortex-M, x86, Apple Silicon(arm64), RISC-V 대부분.
- **빅 엔디안**: 가장 *높은 자리* 바이트(MSB, `0x11`)를 낮은 주소에. 네트워크 바이트 순서(network byte order), 많은 I2C/SPI 센서, 옛 PowerPC/68k.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">uint32_t x = 0x11223344; 를 주소 0x100 에 저장</text>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="215" y="55">0x100</text><text x="295" y="55">0x101</text><text x="375" y="55">0x102</text><text x="455" y="55">0x103</text>
  </g>
  <text x="10" y="88" fill="currentColor">리틀 엔디안</text>
  <text x="10" y="148" fill="currentColor">빅 엔디안</text>
  <g stroke="currentColor" fill="none">
    <rect x="175" y="66" width="80" height="34"/><rect x="255" y="66" width="80" height="34"/>
    <rect x="335" y="66" width="80" height="34"/><rect x="415" y="66" width="80" height="34"/>
    <rect x="175" y="126" width="80" height="34"/><rect x="255" y="126" width="80" height="34"/>
    <rect x="335" y="126" width="80" height="34"/><rect x="415" y="126" width="80" height="34"/>
  </g>
  <rect x="175" y="66" width="80" height="34" class="f-acc"/>
  <rect x="175" y="126" width="80" height="34" class="f-warn"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="215" y="88">44</text><text x="295" y="88">33</text><text x="375" y="88">22</text><text x="455" y="88">11</text>
    <text x="215" y="148">11</text><text x="295" y="148">22</text><text x="375" y="148">33</text><text x="455" y="148">44</text>
  </g>
  <g fill="currentColor">
    <text x="510" y="80">LSB 먼저</text><text x="510" y="98" font-size="12">Cortex-M, x86, arm64</text>
    <text x="510" y="140">MSB 먼저</text><text x="510" y="158" font-size="12">네트워크, 많은 센서</text>
  </g>
  <text x="10" y="200" fill="currentColor">판별 아이디어: 0x100 번지 한 바이트만 읽어 본다 → 0x44 면 리틀, 0x11 이면 빅.</text>
  <text x="10" y="225" fill="currentColor">CPU 레지스터 안의 값(0x11223344)은 같다. 다른 건 오직 "메모리에 펼쳐 놓은 순서".</text>
</svg>
```

### Part A — 엔디안 판별 + 메모리 덤프

- `is_le_union()` — `union { uint32_t u; uint8_t b[4]; }` 로 판별.
- `is_le_charptr()` — `(const unsigned char *)&one` 으로 첫 바이트를 읽어 판별.
- `endian_ct()` — 전처리기 `__BYTE_ORDER__` 로 **컴파일타임에** `ENDIAN_LITTLE/BIG/UNKNOWN` 결정.
- `hexdump(p, n, out, cap)` — 임의 객체의 바이트를 낮은 주소부터 `"44 33 22 11"` 문자열로. 버퍼가 모자라면 0 반환.

### Part B — 바이트 스왑 bswap16/32/64

- `bswap16/32/64` 를 **시프트와 마스크만으로** 구현 (내장 함수 금지). 테스트는 무작위 값 1000개를 `__builtin_bswap16/32/64` 와 대조한다.
- `cpu_to_be32(x)` — `htonl` 과 같은 일. 리틀 호스트면 스왑, 빅 호스트면 그대로.

### Part C — 호스트 엔디안 무관 읽기/쓰기 + 실전 파싱

- `read_be16`, `read_be32`, `read_le32`, `write_le16`, `write_be32` — `uint8_t *` 에서 **한 바이트씩 조립**. 정렬되지 않은 주소(홀수 주소)에서도 동작해야 한다.
- `tmp117_mC(rx)` — TI TMP117 온도 센서가 I2C로 보낸 2바이트(MSB 먼저, 16비트 2의 보수, 1 LSB = 7.8125 m°C)를 밀리°C로.
- `parse_eth_ipv4(frame, len, out)` — Ethernet II + IPv4 헤더에서 total length, protocol, src/dst IP 추출. src IP는 프레임 오프셋 26 — **4의 배수가 아니다**.

## 2. Problem (English)

> **Endianness detection and byte-order-safe parsing.**
>
> Implement in C11 (no compiler builtins inside your implementations):
>
> 1. `bool is_le_union(void)`, `bool is_le_charptr(void)` and a compile-time `endian_t endian_ct(void)`. Write `size_t hexdump(const void *p, size_t n, char *out, size_t cap)` that prints bytes in address order as lowercase hex separated by single spaces; return 0 if `cap < 3*n`.
> 2. `uint16_t bswap16(uint16_t)`, `uint32_t bswap32(uint32_t)`, `uint64_t bswap64(uint64_t)` using shifts and masks; they must agree with `__builtin_bswap*` for all inputs. Implement `cpu_to_be32()`.
> 3. Implement `read_be16/read_be32/read_le32/write_le16/write_be32` on byte arrays so that the result is independent of host endianness and works at any alignment. Use them to (a) convert a TMP117 reading (`{0x0C,0x80}` → 25000 m°C, `{0xFF,0x80}` → −1000 m°C) and (b) parse an Ethernet II + IPv4 header. Explain why `ntohl(*(uint32_t *)(frame + 26))` is wrong.
>
> Example: `bswap64(0x0102030405060708) == 0x0807060504030201`; `read_le32({DE AD BE EF}) == 0xEFBEADDE`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "2/24 passed" — TODO 를 채워 24/24 를 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 판별은 "알려진 값의 첫 바이트"</summary>

`0x01020304` (또는 `1`) 을 저장하고 가장 낮은 주소의 바이트를 본다. union이면 `v.b[0]`, 포인터면 `*(const unsigned char *)&x`. 첫 바이트가 LSB(`0x04` 또는 `1`)면 리틀.

</details>

<details>
<summary>힌트 2 — 컴파일타임은 #if</summary>

GCC/Clang은 `__BYTE_ORDER__`, `__ORDER_LITTLE_ENDIAN__`, `__ORDER_BIG_ENDIAN__` 를 미리 정의한다. `#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__`. 정의 안 된 컴파일러를 위해 `#else` 를 둔다.

</details>

<details>
<summary>힌트 3 — bswap32 는 "바이트마다 이동 거리"</summary>

바이트 3(`0x11`)은 오른쪽으로 24, 바이트 2는 오른쪽 8, 바이트 1은 왼쪽 8, 바이트 0은 왼쪽 24. 가운데 둘은 이동 후 **마스크로 한 바이트만** 남긴다. 64비트는 "32비트 반쪽 교환 → 16비트 → 8비트" 3단계가 깔끔하다.

</details>

<details>
<summary>힌트 4 — read_be32 는 캐스트 위치가 생명</summary>

`p[0] << 24` 는 `p[0]` 가 `int` 로 승격된 뒤 시프트 → `0x80` 이상이면 signed 오버플로 **UB** (C-B02 참고). `(uint32_t)p[0] << 24` 처럼 시프트 **전에** 넓힌다.

</details>

<details>
<summary>힌트 5 — 16비트 2의 보수 → int32</summary>

`(int16_t)raw` 는 raw > 0x7FFF 일 때 **구현 정의** 변환이다. 이식성 있게: `(raw & 0x8000) ? (int32_t)raw - 65536 : (int32_t)raw`. 그 다음 `* 125 / 16` (= 7.8125).

</details>

## 5. 풀이

### Part A — 판별 3가지와 덤프

| 방법 | 코드 핵심 | 표준상 근거 | 비고 |
|---|---|---|---|
| union | `v.u = 0x01020304; v.b[0]` | C11 6.5.2.3p3 각주 95: 다른 멤버로 읽으면 객체 표현을 새 타입으로 **재해석(type punning)** — C에선 허용 | **C++에선 UB** (활성 멤버 규칙). C++은 `memcpy`/`std::bit_cast`/`std::endian` |
| 문자 포인터 | `*(const unsigned char *)&one` | 6.5p7: 어떤 객체든 문자 타입으로 접근 가능. 6.3.2.3p7: 문자 포인터로 바꾸면 **가장 낮은 주소 바이트**를 가리킴 | 반대 방향(바이트 배열을 `uint32_t*` 로 읽기)은 aliasing 위반 (Part C) |
| 컴파일타임 | `#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__` | 표준 아님 — GCC/Clang 확장. C23은 `<stdbit.h>` 의 `__STDC_ENDIAN_NATIVE__` 로 표준화 | `#else` 로 모르는 컴파일러 처리 |

- 표준 C는 바이트 순서를 정하지 않는다 (6.2.6.1p1: 타입 표현은 그 절에서 정한 것 외에는 unspecified). 그래서 "판별"이라는 문제가 성립한다. PDP-11 같은 **미들 엔디안**(`__ORDER_PDP_ENDIAN__`)도 있었기에 `UNKNOWN` 가지를 남겨 둔다.
- `uint8_t` 는 실제 모든 컴파일러에서 `unsigned char` 의 typedef지만, 표준이 "문자 타입"이라고 보장하는 건 `char`/`signed char`/`unsigned char` 뿐이다. aliasing 규칙에 기대는 코드(`is_le_charptr`, `hexdump`)는 `unsigned char *` 로 쓰는 습관이 안전하다.
- 런타임 판별도 `-O2` 에서는 상수로 접힌다(constant folding) — 실행 비용 0. 그래도 `#if` 가 필요한 이유는 **코드 자체를 고르기 위해서**(예: 빅 엔디안 호스트에서 스왑 코드를 아예 빼기).

`hexdump` 는 바이트마다 상위 니블 `b >> 4`, 하위 니블 `b & 0xF` 로 `"0123456789abcdef"` 테이블을 찾는다. `printf` 없이 동작하므로 ISR/부트로더 디버그 출력에도 쓸 수 있다. 맥(arm64)에서 `demo_dump.c` 를 돌린 실제 결과:

```text
$ cc -std=c11 -O2 demo_dump.c -o demo_dump && ./demo_dump
__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
union pun: b[0]=0x04 -> little endian
uint16_t 0xABCD        @0x16b5dab9e : cd ab
uint32_t 0x11223344    @0x16b5dab98 : 44 33 22 11
uint64_t 0x0102..08    @0x16b5dab90 : 08 07 06 05 04 03 02 01
int32_t -2             @0x16b5dab8c : fe ff ff ff
float 1.0f             @0x16b5dab88 : 00 00 80 3f
char[] "ABCD"          @0x16b5dab80 : 41 42 43 44
```

마지막 줄이 포인트: **바이트 배열(문자열)은 엔디안과 무관하다.** 엔디안은 *여러 바이트짜리 스칼라*를 메모리에 펼칠 때만 생긴다. `float 1.0f = 0x3F800000` 도 리틀이라 `00 00 80 3f`.

### Part B — bswap: 바이트마다 이동 거리

```svg
<svg viewBox="0 0 720 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b13a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">bswap32(0x11223344) — 바이트 4개가 각자 다른 거리만큼 이동</text>
  <g stroke="currentColor" fill="none">
    <rect x="120" y="45" width="80" height="34"/><rect x="200" y="45" width="80" height="34"/><rect x="280" y="45" width="80" height="34"/><rect x="360" y="45" width="80" height="34"/>
    <rect x="120" y="175" width="80" height="34"/><rect x="200" y="175" width="80" height="34"/><rect x="280" y="175" width="80" height="34"/><rect x="360" y="175" width="80" height="34"/>
  </g>
  <rect x="120" y="45" width="80" height="34" class="f-acc"/><rect x="360" y="175" width="80" height="34" class="f-acc"/>
  <rect x="360" y="45" width="80" height="34" class="f-warn"/><rect x="120" y="175" width="80" height="34" class="f-warn"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="160" y="67">11</text><text x="240" y="67">22</text><text x="320" y="67">33</text><text x="400" y="67">44</text>
    <text x="160" y="197">44</text><text x="240" y="197">33</text><text x="320" y="197">22</text><text x="400" y="197">11</text>
  </g>
  <text x="10" y="67" fill="currentColor">x</text><text x="10" y="197" fill="currentColor">결과</text>
  <g stroke="currentColor" marker-end="url(#b13a)">
    <line x1="160" y1="80" x2="395" y2="172"/><line x1="240" y1="80" x2="315" y2="172"/>
    <line x1="320" y1="80" x2="245" y2="172"/><line x1="400" y1="80" x2="165" y2="172"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="470" y="70">x&gt;&gt;24             → 0x00000011</text>
    <text x="470" y="98">(x&gt;&gt;8)&amp;0x0000FF00 → 0x00003300</text>
    <text x="470" y="126">(x&lt;&lt;8)&amp;0x00FF0000 → 0x00220000</text>
    <text x="470" y="154">x&lt;&lt;24             → 0x44000000</text>
  </g>
  <text x="470" y="190" fill="currentColor">네 항을 OR → 0x44332211</text>
  <text x="10" y="240" fill="currentColor">바깥 두 항: 시프트가 나머지 비트를 밀어내 마스크 불필요. 가운데 두 항: 이웃 바이트가 딸려와 마스크 필수.</text>
  <text x="10" y="262" fill="currentColor">unsigned 라서 &gt;&gt; 는 0을 채운다 (논리 시프트) — signed 였다면 부호 비트가 번질 수 있다.</text>
</svg>
```

**bswap64 — 분할 정복(3단계)**: 바이트 8개를 하나씩 옮기면 8항이 필요하다. 대신 "큰 덩어리부터 교환":

| 단계 | 연산 | `0x0102030405060708` → |
|---|---|---|
| ① 32비트 반쪽 교환 | `(x & 0x00000000FFFFFFFF) << 32 \| (x & 0xFFFFFFFF00000000) >> 32` | `0x0506070801020304` |
| ② 각 32비트 안에서 16비트 교환 | 마스크 `0x0000FFFF0000FFFF` / `0xFFFF0000FFFF0000`, 시프트 16 | `0x0708050603040102` |
| ③ 각 16비트 안에서 바이트 교환 | 마스크 `0x00FF00FF00FF00FF` / `0xFF00FF00FF00FF00`, 시프트 8 | `0x0807060504030201` |

같은 아이디어를 4·2·1비트로 내리면 C-B01 연습 4의 비트 뒤집기다. 대안: `((uint64_t)bswap32((uint32_t)x) << 32) | bswap32((uint32_t)(x >> 32))`.

**bswap16의 승격**: `x << 8` 에서 `x` 는 `int` 로 승격된다(C-B02). 최대 `0xFFFF << 8 = 0xFFFF00` 이라 int에 들어가고 UB 아님. 결과를 `(uint16_t)` 로 잘라야 상위 `0xFF` 가 사라진다.

**컴파일러는 패턴을 알아본다** — `demo_asm.c` 를 실제로 컴파일한 결과 (불필요한 지시어 제거):

```text
$ clang -std=c11 -O2 -S demo_asm.c -o -                                   # Apple Silicon arm64
_bswap32_shift:  rev   w0, w0
_bswap16_shift:  rev16 w0, w0
_read_be32:      ldr   w8, [x0]
                 rev   w0, w8
_read_le32:      ldr   w0, [x0]

$ clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -ffreestanding -O2 -S demo_asm.c -o -
bswap32_shift:   rev   r0, r0
read_be32:       ldrb r1,[r0] / lsls #24 / ldrb [r0,#1] / lsls #16 / ... (바이트 로드 4번 + 시프트 + 더하기, bx 포함 11명령)

$ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -O2 -S demo_asm.c -o -
read_be32:       ldr   r0, [r0]
                 rev   r0, r0
```

- 시프트 bswap32 → **`REV` 한 명령** (Cortex-M0 포함, ARMv6-M 이상에 REV/REV16/REVSH 존재). 그래서 `__builtin_bswap32` 와 성능 차이가 없다.
- `read_be32` 는 Cortex-M4/arm64에서 `LDR + REV` 2명령. ARMv7-M은 단일 `LDR/STR` 의 비정렬 접근을 허용하므로 컴파일러가 합쳐 준다. **Cortex-M0(ARMv6-M)은 비정렬 접근이 무조건 HardFault** 라 컴파일러가 알아서 바이트 4번 읽기로 둔다. `-mno-unaligned-access` 를 주면 M4에서도 바이트 읽기로 바뀌는 것을 확인했다.
- 즉 바이트 조립 코드는 "느린 이식성 코드"가 아니라, 타깃에 맞게 **최적 코드가 나오는 정답 코드**다.

`cpu_to_be32` 는 `endian_ct() == ENDIAN_LITTLE ? bswap32(x) : x`. 테스트는 결과를 `hexdump` 해서 어느 호스트든 `"11 22 33 44"` 인지 본다 — "메모리에 빅 엔디안으로 놓인다"가 이 함수의 정확한 의미다.

### Part C — 바이트 배열에서 조립하기 (면접 정답)

네트워크/센서 데이터는 `uint8_t` 버퍼로 들어온다. 32비트 필드를 읽는 흔한 오답과 정답:

```c
uint32_t src = ntohl(*(uint32_t *)(frame + 26));   /* 오답 */
uint32_t src = read_be32(frame + 26);              /* 정답 */
```

오답이 틀린 이유 3가지:

1. **정렬(alignment)**: `frame + 26` 은 4의 배수가 아니다. 정렬 안 된 포인터로 변환하는 것 자체가 UB (C11 6.3.2.3p7). Cortex-M0는 HardFault, M3/M4도 `LDRD/LDM` 이나 `CCR.UNALIGN_TRP=1` 이면 fault.
2. **strict aliasing**: `uint8_t` 배열 객체를 `uint32_t` lvalue로 읽는 것은 6.5p7 위반 → 최적화가 읽기를 재배치/삭제할 수 있다.
3. **호스트 엔디안 의존**: `ntohl` 을 빼먹거나 두 번 쓰는 실수가 생기고, 코드 리뷰로 잡기 어렵다. 바이트 조립은 "바이트 0이 최상위" 라는 **데이터 형식**만 기술하므로 호스트가 무엇이든 맞다.

`demo_misaligned.c` 로 1번을 실제로 보면 (맥에선 HW가 비정렬을 허용해 값은 나오지만, UBSan이 잡는다):

```text
$ cc -std=c11 -g -O1 -fsanitize=undefined demo_misaligned.c -o mis && ./mis
demo_misaligned.c:7:18: runtime error: load of misaligned address 0x00016f74aba1 for type 'const uint32_t' (aka 'const unsigned int'), which requires 4 byte alignment
(note: pointer points here … 메모리 덤프 줄 생략)
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior demo_misaligned.c:7:18
v = 0xefbeadde (host order, not BE!)
$ cc -std=c11 -Wall -Wextra -O2 -Wcast-align demo_misaligned.c
demo_misaligned.c:7:19: warning: cast from 'uint8_t *' (aka 'unsigned char *') to 'const uint32_t *' (aka 'const unsigned int *') increases required alignment from 1 to 4 [-Wcast-align]
```

값도 `0xefbeadde` — 빅 엔디안 데이터 `DE AD BE EF` 를 호스트 순서로 읽어 뒤집혔다 (2번·3번 문제까지 한 번에).

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Ethernet II + IPv4 프레임 — 필드 오프셋과 4바이트 정렬</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="45" width="150" height="34"/><rect x="160" y="45" width="150" height="34"/><rect x="310" y="45" width="60" height="34"/>
    <rect x="370" y="45" width="110" height="34"/><rect x="480" y="45" width="40" height="34"/>
    <rect x="520" y="45" width="90" height="34"/><rect x="610" y="45" width="100" height="34"/>
  </g>
  <rect x="310" y="45" width="60" height="34" class="f-mut"/>
  <rect x="520" y="45" width="90" height="34" class="f-warn"/><rect x="610" y="45" width="100" height="34" class="f-warn"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="85" y="67">dst MAC (6)</text><text x="235" y="67">src MAC (6)</text><text x="340" y="67">type</text>
    <text x="425" y="67">ver/IHL..len</text><text x="500" y="67">…</text><text x="565" y="67">src IP</text><text x="660" y="67">dst IP</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="98">0</text><text x="160" y="98">6</text><text x="310" y="98">12</text><text x="370" y="98">14</text>
    <text x="520" y="98">26</text><text x="610" y="98">30</text><text x="690" y="98">34</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="135">type   = read_be16(f+12)  → 0x0800 (IPv4)</text>
    <text x="20" y="157">totlen = read_be16(f+16)  → 84,  proto = f[23] → 1 (ICMP)</text>
    <text x="20" y="179">src    = read_be32(f+26)  → 0xC0A8010A = 192.168.1.10</text>
  </g>
  <text x="10" y="215" fill="currentColor">버퍼가 4바이트 정렬이어도 26 % 4 == 2 → src/dst IP는 비정렬. (리눅스 드라이버가 NET_IP_ALIGN(보통 2)로</text>
  <text x="10" y="237" fill="currentColor">버퍼 앞에 2바이트를 비워 IP 헤더를 정렬시키는 이유.) 바이트 조립은 정렬과 무관하게 안전하다.</text>
</svg>
```

**읽기/쓰기 헬퍼의 모양** — 엔디안 이름은 "데이터 형식"을 말한다:

| 함수 | 바이트 0의 역할 | 핵심 식 |
|---|---|---|
| `read_be32(p)` | 최상위(MSB) | `(uint32_t)p[0]<<24 \| (uint32_t)p[1]<<16 \| (uint32_t)p[2]<<8 \| p[3]` |
| `read_le32(p)` | 최하위(LSB) | `p[0] \| (uint32_t)p[1]<<8 \| (uint32_t)p[2]<<16 \| (uint32_t)p[3]<<24` |
| `write_be32(p,v)` | MSB 저장 | `p[0]=(uint8_t)(v>>24); … p[3]=(uint8_t)v;` |
| `write_le16(p,v)` | LSB 저장 | `p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);` |

호스트 엔디안을 묻는 코드가 **한 줄도 없다** — 이것이 Rob Pike가 "byte order fallacy" 글에서 말한 요점이다. 실무 라이브러리도 같은 모양: Linux `get_unaligned_be32()/put_unaligned_le16()`, Zephyr `sys_get_be32()/sys_put_le16()`.

**TMP117 (I2C 센서, MSB 먼저)**: 레지스터 읽기 응답 `rx[0]=MSB, rx[1]=LSB`.

| rx | raw (`read_be16`) | 부호 확장 s | `s*125/16` (m°C) |
|---|---|---|---|
| `0C 80` | `0x0C80` = 3200 | 3200 | 25000 (+25 °C) |
| `FF 80` | `0xFF80` | −128 | −1000 (−1 °C) |
| `80 00` | `0x8000` | −32768 | −256000 |

- 부호 확장을 `(int16_t)raw` 로 하면 GCC/Clang에선 동작하지만 C11 6.3.1.3p3상 **구현 정의**다. `raw - 65536` 방식은 어디서나 같은 결과.
- `s * 125 / 16`: 최대 |s|·125 ≈ 4.1M 로 int32 여유. 음수 나눗셈은 0쪽으로 버림(C99부터 규정).
- 센서마다 순서가 다르다: MPU-6050 은 `ACCEL_XOUT_H` 가 낮은 주소(빅), Bosch BMI160 은 LSB 레지스터가 낮은 주소(리틀). **데이터시트의 레지스터 맵이 형식을 정한다** → 헬퍼 이름을 그대로 고르면 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 24/24 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_san && ./sol_san   # 24/24, 경고 없음
```

관찰용 데모 (하네스 아님):

```bash
cc -std=c11 -O2 demo_dump.c -o demo_dump && ./demo_dump                       # 타입별 메모리 배치
clang -std=c11 -O2 -S demo_asm.c -o -                                          # arm64: rev / rev16 / ldr+rev
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -ffreestanding -O2 -S demo_asm.c -o -
cc -std=c11 -g -O1 -fsanitize=undefined demo_misaligned.c -o mis && ./mis      # 비정렬 load UBSan 보고
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `p[0] << 24 \| p[1] << 16 …` | `p[0]` 이 int로 승격 → `0x80<<24` 는 signed 오버플로 UB (C-B02) | `(uint32_t)p[0] << 24` |
| `*(uint32_t *)(buf + off)` | 비정렬 UB(M0 HardFault) + strict aliasing 위반 + 호스트 순서 | `read_be32(buf + off)` 또는 `memcpy` 후 스왑 |
| `ntohl(read_be32(p))` | 이미 호스트 값인데 또 스왑 → 리틀 호스트에서 뒤집힘 | 둘 중 하나만. 바이트 조립을 쓰면 `ntohl` 불필요 |
| `(x >> 8) \| (x << 8)` 를 32비트에 사용 | 가운데 바이트 마스크 없음 → 이웃 바이트가 섞임 | 4항 + 마스크 `0xFF00`, `0xFF0000` |
| bswap16 결과를 캐스트 없이 `int` 로 비교 | `x << 8` 의 상위 비트(`0xFF0000`)가 남음 | `(uint16_t)((x << 8) \| (x >> 8))` |
| `(int16_t)raw` 로 부호 확장 | 범위 밖 변환은 구현 정의 (6.3.1.3p3) | `(raw & 0x8000) ? (int32_t)raw - 65536 : raw` |
| 구조체를 통째로 `memcpy` 해 송신 | 패딩(C-B12) + 엔디안 + 정렬이 수신측과 다름 | 필드별 `write_be*/write_le*` 직렬화 |
| 문자열/바이트 배열도 스왑 | 엔디안은 다바이트 스칼라에만 있다 | 배열은 그대로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 엔디안 판별 함수를 써보세요. union 방식은 합법인가요?</summary>

`uint32_t x = 1; return *(unsigned char *)&x == 1;` 또는 `union { uint32_t u; uint8_t b[4]; }`. C에서 union으로 다른 멤버를 읽는 type punning은 허용된다(C11 6.5.2.3 각주 95, 트랩 표현 가능성만 예외). **C++에서는 UB** — `memcpy`, C++20 `std::bit_cast`, 또는 `std::endian::native` 를 쓴다. 문자 포인터 방식은 6.5p7(문자 타입 aliasing 허용)로 C/C++ 모두 OK.

</details>

<details>
<summary>Q2. 런타임 말고 컴파일타임에 알 수 있나요? 왜 굳이?</summary>

GCC/Clang `__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__`, C23 `<stdbit.h>` 의 `__STDC_ENDIAN_NATIVE__`, C++20 `std::endian`. 런타임 판별도 -O2에선 상수로 접히지만, `#if` 는 **코드 자체를 고르거나** `_Static_assert` 로 "이 펌웨어는 리틀 엔디안 전용"을 빌드 시 강제할 때 쓴다. 꼬리질문 "Cortex-M은 엔디안을 바꿀 수 있나?" → ARMv7-M은 구현이 리셋 시 엔디안을 정하고(`AIRCR.ENDIANNESS` 읽기 전용) 실행 중 못 바꾼다. 실제 MCU는 거의 모두 리틀.

</details>

<details>
<summary>Q3. 받은 패킷 버퍼에서 오프셋 26의 32비트 빅 엔디안 필드를 읽으세요.</summary>

`(uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]`. `*(uint32_t*)` 캐스트는 ① 비정렬 UB/HardFault ② strict aliasing ③ 호스트 순서 의존이라 오답. 꼬리질문 "느리지 않나?" → clang -O2가 Cortex-M4/arm64에서 `LDR + REV` 두 명령으로, M0에선 바이트 로드로 만든다(직접 확인함). 대안으로 `memcpy(&v, p, 4)` 후 `be32_to_cpu(v)` 도 정렬·aliasing 안전.

</details>

<details>
<summary>Q4. htonl / ntohl 을 구현해 보세요. 둘의 차이는?</summary>

리틀 호스트에선 둘 다 `bswap32`, 빅 호스트에선 둘 다 항등 함수 — 구현은 같고 **의도(방향)** 만 다르다. bswap은 자기 역함수(involution)라서다. bswap32는 4항 시프트+마스크 또는 분할 정복. 꼬리질문 "64비트는?" → `htonll` 은 표준이 아니다(macOS엔 있음). `htobe64`/`be64toh` (glibc·BSD `<endian.h>`, POSIX.1-2024에 편입) 를 쓰거나 바이트 조립.

</details>

<details>
<summary>Q5. ARM에 엔디안 관련 명령이 있나요? Cortex-M 비정렬 접근 규칙은?</summary>

`REV`(32비트 바이트 역순), `REV16`(각 하프워드 안에서 바이트 교환: `0x11223344→0x22114433`), `REVSH`(하위 하프워드 바이트 교환 후 부호 확장) — ARMv6-M(Cortex-M0)부터 있다. CMSIS `__REV()`, `__REV16()`, `__REVSH()`. 비정렬: M0/M0+ 는 모든 비정렬 접근이 HardFault. M3/M4/M7은 단일 `LDR/LDRH/STR/STRH` 만 비정렬 허용(느림), `LDM/STM/LDRD/STRD` 는 fault, `SCB->CCR.UNALIGN_TRP=1` 이면 전부 trap.

</details>

<details>
<summary>Q6. 비트 순서(bit endianness)도 있나요?</summary>

CPU 레지스터 안의 비트 순서는 프로그래머에게 보이지 않는다(시프트는 값 기준). 비트 순서가 문제 되는 곳은 ① **직렬 버스 전송 순서** — I2C·CAN은 MSB 먼저, UART는 LSB 먼저, SPI는 설정 가능 ② C **비트필드 배치** — 구현 정의라 레지스터/프로토콜 표현에 쓰면 이식성이 깨진다(C-B01 Q4) ③ CAN DBC의 Intel(리틀)/Motorola(빅) 신호 배치 (추가 연습 3).

</details>

## 9. 추가 연습

### 연습 1 — ARM REV16 / REVSH 를 C로

`rev16(0x11223344) == 0x22114433`, `revsh(0x12340080) == -32768` (하위 16비트만 교환 후 부호 확장).

<details>
<summary>답 보기</summary>

`rev16`: `((x & 0x00FF00FF) << 8) | ((x & 0xFF00FF00) >> 8)` — 짝수 바이트는 왼쪽, 홀수 바이트는 오른쪽. clang은 이걸 `rev16` 한 명령으로 바꾼다(demo_asm.c). `revsh`: `h = bswap16((uint16_t)x)`, `(h & 0x8000) ? (int32_t)h - 65536 : h`.

</details>

### 연습 2 — 24비트 ADC 샘플 부호 확장

ADS1220 같은 24비트 ADC는 2의 보수 3바이트를 MSB 먼저 보낸다. `{7F FF FF} → 8388607`, `{80 00 00} → -8388608`, `{FF FF FF} → -1`.

<details>
<summary>답 보기</summary>

`v = p[0]<<16 | p[1]<<8 | p[2]` (uint32_t로 넓혀서), `(v & 0x800000) ? (int32_t)v - 0x1000000 : (int32_t)v`. 흔한 대안 `((int32_t)(v << 8)) >> 8` 은 범위 밖 변환(구현 정의) + 음수 오른쪽 시프트(구현 정의) 두 번에 기댄다.

</details>

### 연습 3 — CAN 신호: Intel vs Motorola

CAN 데이터 8바이트 중 byte 2–3의 16비트 신호. DBC에서 Intel이면 리틀, Motorola면 빅. `{.., 0x12, 0x34, ..}` → Intel `0x3412`, Motorola `0x1234`.

<details>
<summary>답 보기</summary>

Intel: `data[b] | data[b+1] << 8`, Motorola: `data[b] << 8 | data[b+1]`. 실제 DBC 신호는 바이트 경계가 아닌 **비트 시작 위치 + 길이** 로 정의되고, Motorola는 비트 번호 매기기도 달라서 Mid 레벨 비트필드 추출(C-B04)과 결합해야 한다.

</details>

### 연습 4 — 매직 넘버로 바이트 순서 판별

pcap 파일 헤더는 쓴 기계의 순서로 `0xA1B2C3D4` 를 저장한다. 헤더 4바이트로 파일의 엔디안을 판별하고(0=리틀, 1=빅, -1=아님) 이후 필드를 그 순서로 읽어라.

<details>
<summary>답 보기</summary>

`read_le32(hdr) == 0xA1B2C3D4` 면 리틀, `read_be32(hdr) == …` 면 빅. 이후 `rd32(p, order)` 로 분기. **호스트 엔디안이 아니라 데이터의 엔디안**을 판별한다는 점이 핵심. ELF의 `EI_DATA`, TIFF의 `II/MM`, 부트로더 이미지 헤더도 같은 패턴.

</details>

### 연습 5 — memcpy 로 네이티브 로드 후 스왑

`read_be32_memcpy(p)`: `memcpy(&v, p, 4)` 로 읽고 리틀 호스트면 `bswap32`. 홀수 주소에서도 `read_be32` 와 같은 값.

<details>
<summary>답 보기</summary>

`memcpy` 는 바이트 복사로 정의되므로 정렬·aliasing 문제가 없고, 컴파일러가 단일 `LDR`(비정렬 허용 타깃) 또는 바이트 로드로 바꾼다. Linux `get_unaligned_be32()` 의 일반 구현이 이 모양이다.

</details>

### 연습 6 — SPI LCD 프레임버퍼 스왑

ILI9341 같은 SPI LCD는 RGB565 픽셀을 MSB 먼저 받는다. 리틀 엔디안 MCU의 `uint16_t` 프레임버퍼를 DMA로 그대로 보내려면? `swap16_buf(px, n)` 을 쓰고, 스왑 후 메모리 첫 바이트가 `0xF8`(빨강 `0xF800` 의 MSB)인지 확인하라.

<details>
<summary>답 보기</summary>

각 원소에 `bswap16`. 바이트 단위 DMA가 낮은 주소부터 보내므로, 메모리에 MSB가 먼저 놓이도록 미리 뒤집는다. 대안: SPI를 16비트 프레임 모드로 설정(하드웨어가 MSB 먼저 시프트) — STM32 `SPI_CR1.DFF`(F4) / `DS`(F0·L4 계열) 설정으로 스왑 자체를 없애는 게 실무 최적화.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- 엔디안 = 다바이트 스칼라를 메모리에 펼치는 순서. 리틀: LSB가 낮은 주소(Cortex-M, x86, arm64). 빅: 네트워크·많은 센서.
- 판별: `*(unsigned char *)&one == 1` (C/C++ OK), union punning (C OK, C++ UB), `__BYTE_ORDER__`/C23 `__STDC_ENDIAN_NATIVE__` (컴파일타임).
- bswap32 = `x>>24 | (x>>8)&0xFF00 | (x<<8)&0xFF0000 | x<<24`. 64비트는 32→16→8 분할 정복. 컴파일러가 `REV`/`REV16` 로 바꿔 준다.
- **면접 정답**: 바이트 배열에서 `read_be32`/`write_le16` 로 조립 — 호스트 엔디안·정렬·aliasing 모두 무관. 시프트 전에 `(uint32_t)` 캐스트.
- `*(uint32_t *)(buf + off)` 는 비정렬 UB(M0 HardFault) + aliasing 위반 + 순서 의존.
- 부호 있는 센서 값은 `raw - 65536` (또는 `- 0x1000000`) 로 이식성 있게 부호 확장.
- 데이터시트 레지스터 맵 / 프로토콜 문서가 "데이터의 엔디안"을 정한다. 호스트 엔디안은 거의 물을 필요가 없다.

## 참고자료

- [Rob Pike, "The byte order fallacy" (2012)](https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html) — 바이트 조립 방식의 근거
- [GCC — Common Predefined Macros (`__BYTE_ORDER__`)](https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html)
- [GCC — Other Built-in Functions (`__builtin_bswap16/32/64`)](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html)
- [Linux kernel — Unaligned Memory Accesses](https://docs.kernel.org/core-api/unaligned-memory-access.html)
- [ARMv7-M Architecture Reference Manual — REV/REV16/REVSH, 비정렬 접근, AIRCR.ENDIANNESS](https://developer.arm.com/documentation/ddi0403/latest/)
- [TI TMP117 데이터시트 — 온도 레지스터 형식(7.8125 m°C/LSB)](https://www.ti.com/lit/ds/symlink/tmp117.pdf)
- [RFC 791 — Internet Protocol (헤더 형식, Appendix B 데이터 전송 순서)](https://www.rfc-editor.org/rfc/rfc791)
- C11 표준 6.2.6.1 (타입 표현), 6.3.1.3 (정수 변환), 6.3.2.3p7 (포인터 정렬·문자 포인터), 6.5p7 (aliasing), 6.5.2.3 각주 95 (union type punning)

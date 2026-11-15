---
id: ALGO-B13
time: 70분
updated: 2026-11-11
---
# ALGO-B13 비트 트릭 2: 비트 반전·니블 스왑·패리티

## 학습 목표

- 비트 반전(reverse bits)을 **루프 · 마스크-시프트 분할정복(divide & conquer) · 256-entry 표(LUT)** 세 가지로 8/32비트에 구현하고, 표를 어떻게 만드는지(컴파일 타임 매크로 vs 부팅 때 점화식) 설명할 수 있다.
- "니블 뒤집어 주세요(reverse the nibbles)"를 들으면 **두 가지 뜻**(바이트 안 두 니블 swap / 니블 순서 전체 역순)을 먼저 확인하고, 둘 다 구현할 수 있다 — Don 이 2026-10-30 실제 HM 스크린에서 받은 문제다.
- 이 레슨의 모든 재배열(비트 반전, 홀짝 교환, 니블 swap, 니블 역순, bswap)을 **"비트 i → 비트 i XOR m"** 하나로 보고, 분할정복의 각 단계가 m 의 비트 하나라는 것을 말할 수 있다.
- 패리티(parity)를 루프 · XOR 접기(fold) · 표로 구하고, UART 8E1 프레임에서 패리티가 1비트 오류는 전부 잡고 2비트 오류는 전부 놓친다는 것을 전수 실험으로 보인다.
- 방식별 속도(이 맥 ns/op)·메모리(표 바이트)·명령 수(arm64 / Cortex-M4 / Cortex-M0)를 비교하고, clang 이 분할정복 코드를 `rbit` 한 개로 바꾸는 것과 M0 에는 `rbit` 이 없다는 것을 직접 확인한다.

## 1. 문제

비트 재배열은 펌웨어 코딩 면접의 단골이다(문제가 짧고, 시프트·마스크·정수 승격(integer promotion) 실수가 바로 드러난다). Don 은 Rivian 실리콘 검증 HM 스크린(2026-10-30)에서 "Reverse order of input buffer data: `0x1234567890abcdef` → `0xfedcba0987654321`" 를 받았다. 로직은 맞혔지만 회고(N02)에서 남은 숙제는 **단위를 먼저 확인하기 · trace 를 단위에 맞게 말하기 · 빠른 버전/버퍼 버전 꼬리질문에 먼저 답하기**였다. 이 레슨이 그 숙제를 끝낸다.

현장에서 이 연산들이 나오는 곳:

- **SPI LSB-first 장치**: MCU SPI 가 MSB-first 만 되거나(또는 DMA 경로에서 비트 순서 옵션을 못 쓸 때) 장치는 LSB-first 를 원하면, 각 바이트의 비트를 뒤집어 보낸다. **바이트 순서는 그대로**다 — 엔디안 스왑(C-B13)과 헷갈리는 순간 버그.
- **반사(reflected) CRC**: UART·이더넷처럼 LSB 를 먼저 보내는 직렬 하드웨어에서 온 CRC(CRC-32, CRC-16/MODBUS)는 입력·출력 비트가 반전된 정의다(ALGO-M01, ALGO-M02, COMM-B09).
- **패리티**: UART 8E1/7E1(COMM-B01), SWD 요청 패킷(TOOL-A02), LIN PID(AUTO-B06), ARINC 429(AERO-B03), SRAM/캐시 패리티(MEM-B06).
- **FFT 재배열**: radix-2 FFT 는 인덱스를 log2(N) 비트 반전한 순서로 섞는다(DSP-M01).

같은 값 `0x12345678` 에 "뒤집기"를 다섯 가지로 적용하면 결과가 전부 다르다. 면접관이 말한 "뒤집기"가 어느 것인지 묻는 것이 첫 번째 할 일이다.

```svg
<svg viewBox="0 0 740 286" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">0x12345678 의 여러 "뒤집기" — 칸 하나 = 니블(hex 한 자리), 굵은 선 = 바이트 경계</text>
  <text x="10" y="50" fill="currentColor">원래 값</text> <rect x="230" y="34" width="320" height="24" class="f-mut" stroke="currentColor"/> <text x="250 290 330 370 410 450 490 530" y="51" fill="currentColor" font-family="monospace" text-anchor="middle">12345678</text> <text x="565" y="50" fill="currentColor">비트 i 그대로</text>
  <text x="10" y="86" fill="currentColor">바이트 안 니블 swap</text> <rect x="230" y="70" width="320" height="24" class="f-ok" stroke="currentColor"/> <text x="250 290 330 370 410 450 490 530" y="87" fill="currentColor" font-family="monospace" text-anchor="middle">21436587</text> <text x="565" y="86" fill="currentColor">m = 4   (뜻 1)</text>
  <text x="10" y="122" fill="currentColor">니블 순서 역순</text> <rect x="230" y="106" width="320" height="24" class="f-acc" stroke="currentColor"/> <text x="250 290 330 370 410 450 490 530" y="123" fill="currentColor" font-family="monospace" text-anchor="middle">87654321</text> <text x="565" y="122" fill="currentColor">m = 28  (뜻 2, 스크린 문제)</text>
  <text x="10" y="158" fill="currentColor">바이트 역순 (bswap)</text> <rect x="230" y="142" width="320" height="24" class="f-mut" stroke="currentColor"/> <text x="250 290 330 370 410 450 490 530" y="159" fill="currentColor" font-family="monospace" text-anchor="middle">78563412</text> <text x="565" y="158" fill="currentColor">m = 24  (엔디안, C-B13)</text>
  <text x="10" y="194" fill="currentColor">비트 역순 (reverse bits)</text> <rect x="230" y="178" width="320" height="24" class="f-warn" stroke="currentColor"/> <text x="250 290 330 370 410 450 490 530" y="195" fill="currentColor" font-family="monospace" text-anchor="middle">1E6A2C48</text> <text x="565" y="194" fill="currentColor">m = 31  (LeetCode 190)</text>
  <text x="10" y="230" fill="currentColor">홀짝 비트 교환</text> <rect x="230" y="214" width="320" height="24" class="f-ok" stroke="currentColor"/> <text x="250 290 330 370 410 450 490 530" y="231" fill="currentColor" font-family="monospace" text-anchor="middle">2138A9B4</text> <text x="565" y="230" fill="currentColor">m = 1</text>
  <path d="M310 34v24M390 34v24M470 34v24M310 70v24M390 70v24M470 70v24M310 106v24M390 106v24M470 106v24M310 142v24M390 142v24M470 142v24M310 178v24M390 178v24M470 178v24M310 214v24M390 214v24M470 214v24" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="268" fill="currentColor">공통 규칙: 비트 i 가 비트 (i XOR m) 로 간다. m 의 비트 k 가 1 = "거리 2^k 짝 바꾸기" 한 단계.</text>
</svg>
```

### Part A — 비트 반전 세 가지 (8비트 · 32비트)

- `rev8_loop`, `rev32_loop`: 한 비트씩 꺼내 결과에 밀어 넣는다.
- `rev8_dc`, `rev32_dc`: 마스크-시프트 분할정복. 8비트는 3단계(4·2·1), 32비트는 5단계(1·2·4·8·16).
- `rev32_lut`: 주어진 `static const uint8_t REV8[256]`(컴파일 타임 매크로로 생성)로 바이트마다 뒤집고 바이트 순서도 뒤집는다.
- `rev8_table_build`: 같은 표를 **실행 중에** 점화식으로 만든다. 테스트가 주어진 표와 `memcmp` 로 비교한다.

### Part B — 니블 두 가지 뜻, 홀짝 교환, 패리티

- 뜻 1: `swap_nibbles8` (0xAB → 0xBA), `nib_swap_bytes32` (0x12345678 → 0x21436587).
- 뜻 2: `rev_nibbles32_loop` (→ 0x87654321), `rev_nibbles64_dc` (스크린 예시 0x1234567890ABCDEF → 0xFEDCBA0987654321, 반복문 없이).
- `swap_odd_even32`: 비트 2k ↔ 2k+1.
- `parity32_loop`, `parity32_fold`, `parity32_lut`(주어진 `PARITY8[256]` 사용): 1의 개수가 홀수면 1.

### Part C — 펌웨어: SPI LSB-first 버퍼와 UART 8E1

- `spi_prepare_lsb_first(dst, src, n)`: MSB-first SPI 로 LSB-first 장치에 보낼 버퍼를 만든다(제자리 `dst == src` 허용, `n == 0` 허용). 테스트는 선 위 비트 순서를 시뮬레이션해서 "장치가 기대하는 순서 == 주변장치가 실제로 내보내는 순서"를 확인한다.
- `uart_frame_8e1(d)`, `uart_decode_8e1(f, &out)`: 11비트 프레임(start, d0..d7, 짝수 패리티, stop)을 만들고 검사한다. 전수 실험: 256 데이터 × 모든 1비트 오류 / 데이터 2비트 오류.

## 2. Problem (English)

```text
Implement, in C11 with no compiler builtins:

  uint8_t  rev8_loop(uint8_t b);      uint8_t  rev8_dc(uint8_t b);
  uint32_t rev32_loop(uint32_t x);    uint32_t rev32_dc(uint32_t x);
  uint32_t rev32_lut(uint32_t x);     /* uses a given const uint8_t REV8[256] */
  void     rev8_table_build(uint8_t t[256]);

  uint8_t  swap_nibbles8(uint8_t b);           /* 0xAB -> 0xBA                         */
  uint32_t nib_swap_bytes32(uint32_t x);       /* 0x12345678 -> 0x21436587             */
  uint32_t rev_nibbles32_loop(uint32_t x);     /* 0x12345678 -> 0x87654321             */
  uint64_t rev_nibbles64_dc(uint64_t x);       /* 0x1234567890abcdef -> 0xfedcba0987654321, no loop */
  uint32_t swap_odd_even32(uint32_t x);        /* bit 2k <-> bit 2k+1                  */
  unsigned parity32_loop/fold/lut(uint32_t x); /* 1 if the number of set bits is odd   */

  void spi_prepare_lsb_first(uint8_t *dst, const uint8_t *src, size_t n);
  uint16_t uart_frame_8e1(uint8_t d);          /* bit0 start=0, bits1..8 data LSB first,
                                                  bit9 even parity, bit10 stop=1        */
  int uart_decode_8e1(uint16_t f, uint8_t *out); /* 0 ok, -1 framing, -2 parity          */

Examples:
  rev32(0x00000001) = 0x80000000      rev32(0x12345678) = 0x1E6A2C48
  rev8(0xB4) = 0x2D                   parity(0x12345678) = 1  (13 set bits)
  uart_frame_8e1(0x55) = 0x4AA        uart_frame_8e1(0x01) = 0x602

Constraints: every function is O(1) extra memory, no UB for any input
(watch int promotion: uint8_t << 24 is an int shift). Cross-check all methods
against each other: exhaustive for 8/16-bit inputs, random + walking-ones for 32/64-bit.
Follow-up: which method would you ship on a Cortex-M0? On a Cortex-M4? Why?
```

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 17개 함수 본문이다(전부 1~6줄). 두 표 `REV8[]`, `PARITY8[]` 와 하네스(참조 구현 `ref_xor_perm`, 선 위 비트 시뮬레이터 `wire_bits`)는 주어진다. 하네스는 전수/무작위 대조에서 **첫 불일치 하나만** 찍고 개수를 센다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:165  rev8_loop(0x01u) == 0x80u
...
  first mismatch rev32_dc: in=0x1 got=0x1 want=0x80000000
FAIL starter.c:206  g_bad == 0
...
  first mismatch rev_nibbles64_dc: in=0xd4cbbd2ccea3fffb got=0xd4cbbd2ccea3fffb want=0xbfff3aecc2dbbc4d
FAIL starter.c:251  g_bad == 0
...
uart 8E1: roundtrip 0/256, 1-bit parity err 0/2304, framing 512/512, 2-bit undetected 0/7168
FAIL starter.c:313  ok == 256 && par1 == 2304 && frm1 == 512
FAIL starter.c:314  undetected2 == 256 * 28
5/42 passed
```

(exit 1. 통과한 5개는 스텁이 우연히 맞는 것 — 예: `rev32_dc(0) == 0 && rev32_dc(0xFFFFFFFF) == 0xFFFFFFFF` 는 "그대로 돌려주기"로도 참이다. 그래서 0 과 전부 1 은 **약한 테스트**다.)

## 4. 힌트

<details><summary>힌트 1 — 루프: "꺼내서 밀어 넣기"</summary>

입력의 LSB 를 꺼내(`x & 1`) 결과를 왼쪽으로 한 칸 밀고 붙인다(`r = (r << 1) | bit`), 입력은 오른쪽으로 한 칸(`x >>= 1`). 처음 꺼낸 비트 0 이 w 번 밀려 맨 위(비트 w-1)에 도착한다. 니블 역순도 똑같다 — 1 대신 4 비트씩, 마스크 `0xF`.

</details>

<details><summary>힌트 2 — 분할정복 한 단계 = 두 반쪽을 맞바꾸기</summary>

거리 s 짝 바꾸기: `((x >> s) & m) | ((x & m) << s)`. m 은 짝 중 **아래쪽** 자리(s=1 → 0x55..., s=2 → 0x33..., s=4 → 0x0F...). 위쪽을 내릴 때는 **시프트 먼저, 마스크 나중**, 아래쪽을 올릴 때는 **마스크 먼저, 시프트 나중**. 마지막 16 은 마스크가 필요 없다 (`(x >> 16) | (x << 16)` = 회전).

</details>

<details><summary>힌트 3 — LUT: 바이트 k 는 바이트 3-k 로, 그리고 캐스트</summary>

`REV8[x & 0xFF]` 가 결과의 맨 위 바이트. `REV8[...]` 는 `uint8_t` 라서 `<< 24` 하기 전에 int 로 승격된다 — 0x80 << 24 는 int 에 안 들어가 UB. `(uint32_t)REV8[...] << 24` 로 쓴다.

</details>

<details><summary>힌트 4 — 표 만들기 점화식</summary>

i 의 비트를 b7..b0 이라 하면, `i >> 1` 의 반전은 i 의 반전에서 b0 만 빠지고 한 칸 **위로** 올라간 모양이다. 그러니 `t[i] = (t[i >> 1] >> 1) | ((i & 1) << 7)`. i 를 1 부터 오름차순으로 돌면 `t[i >> 1]` 은 항상 먼저 만들어져 있다.

</details>

<details><summary>힌트 5 — 니블 역순 64비트를 반복문 없이</summary>

니블 역순 = 바이트 안 니블 swap(거리 4) + 바이트 역순(거리 8, 16, 32). 그림 1 의 m 으로 보면 60 = 4 + 8 + 16 + 32.

</details>

<details><summary>힌트 6 — 패리티 접기와 UART 프레임</summary>

XOR 은 결합·교환 법칙이 성립한다. `x ^= x >> 16` 뒤에 아래 16비트의 패리티 = 원래 32비트의 패리티. 8 → 4 → 2 → 1 까지 접으면 비트 0 이 답. 표 버전은 8비트까지 접고 `PARITY8[]`. UART 프레임은 `(1u << 10) | (p << 9) | (d << 1)`.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | "Reverse **bits, nibbles, or bytes**? For nibbles: swap the two nibbles in each byte (0x12 → 0x21), or reverse the order of all hex digits (0x12345678 → 0x87654321)? Integer or byte buffer? If buffer, is byte 0 the most significant?" |
| 2. 브루트포스 | 루프: w 번 꺼내 밀어 넣기. O(w) 연산, 추가 메모리 O(1). trace 를 **그 단위로** 2스텝 말하기 |
| 3. 더 나은 생각 + 불변식 | 분할정복: 비트 i → i XOR (w-1). 단계마다 인덱스의 비트 하나를 뒤집는다 → log2(w) 단계 |
| 4. 코드 | 마스크 상수는 아래쪽 짝 자리. `uint32_t`/`uint64_t` 만, 캐스트 먼저 |
| 5. 소리 내 말할 테스트 | 0, 전부 1(약함), `1` → 최상위, `0x80000000` → 1, 걷는 1 32개, 문제 예시, **두 번 적용하면 원래 값**, 8/16비트 전수 |
| 6. 복잡도 | 루프 O(w), 분할정복 O(log w) + 상수 5개, LUT w/8 번 로드 + 표 256 B. 스택 0, 분기 없음(루프 빼고) |

스크린에서 Don 의 답은 2단계까지 맞았다. 1단계 질문 한 줄과 5단계 테스트 목록이 "시니어 신호"다.

### Part A — 루프: trace 를 단위에 맞게

`rev8_loop(0xB4)` (0xB4 = 1011 0100). 불변식: k 번 돈 뒤 `r` 의 아래 k 비트 = 입력 b0..b(k-1) 을 거꾸로 놓은 것, `b` 에는 아직 안 꺼낸 비트만 남는다.

| i | 꺼낸 비트 (b & 1) | r (2진) | 남은 b (2진) |
|---|---|---|---|
| 시작 | — | `0` | `10110100` |
| 0 | 0 | `0` | `01011010` |
| 1 | 0 | `00` | `00101101` |
| 2 | 1 | `001` | `00010110` |
| 3 | 0 | `0010` | `00001011` |
| 4 | 1 | `00101` | `00000101` |
| 5 | 1 | `001011` | `00000010` |
| 6 | 0 | `0010110` | `00000001` |
| 7 | 1 | `00101101` = **0x2D** | `00000000` |

니블 역순도 같은 모양이다. 스크린 문제 trace 는 **니블 단위로** 말한다: `res` 가 `0xf` → `0xfe` → `0xfed` … (회고에서 `0xef` 처럼 바이트 단위가 섞인 것이 감점 포인트였다.)

루프는 32비트에서 32번 돈다. `while (x)` 로 일찍 끝내는 변형(CMSIS 의 v6-M용 `__RBIT`, 아래)은 남은 shift 를 마지막에 한 번 더 해야 하고 **입력에 따라 시간이 달라진다**.

### Part A — 분할정복: 인덱스의 비트를 하나씩 뒤집는다

32비트에서 비트 i 의 목적지는 31 - i 이고, 0 ≤ i ≤ 31 이면 **31 - i = i XOR 31** (31 = 11111₂ 에서 빼기는 받아내림이 없으니 비트마다 뒤집기와 같다). "거리 2^k 짝 바꾸기" 한 단계는 인덱스의 비트 k 만 뒤집는다(i → i XOR 2^k). 그러니 k = 0..4 다섯 단계를 **어떤 순서로든** 하면 i XOR 31 = 비트 반전이다. 단계들은 서로 교환 가능하고(XOR 은 교환 법칙), 각 단계는 자기 자신의 역원이다(두 번 하면 원래대로). 수학적으로는 (Z/2)^5 군의 작용이다 — 연습 1 이 이것을 함수 하나(`perm_xor32(x, m)`)로 만든다.

8비트 `rev8_dc` 의 세 단계 (a = b7 … h = b0):

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b13a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">rev8_dc: 짝 바꾸기 3단계 — 블록 색이 다르면 서로 자리를 바꾼다</text>
  <text x="10" y="58" fill="currentColor">입력</text> <rect x="200" y="40" width="144" height="26" class="f-acc" stroke="currentColor"/> <rect x="344" y="40" width="144" height="26" class="f-ok" stroke="currentColor"/> <text x="218 254 290 326 362 398 434 470" y="58" fill="currentColor" font-family="monospace" text-anchor="middle">abcdefgh</text> <text x="505" y="58" fill="currentColor">비트 i</text>
  <path d="M272 68 L410 100M416 68 L278 100" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="10" y="122" fill="currentColor">거리 4 (니블 swap)</text> <rect x="200" y="104" width="72" height="26" class="f-acc" stroke="currentColor"/> <rect x="272" y="104" width="72" height="26" class="f-ok" stroke="currentColor"/> <rect x="344" y="104" width="72" height="26" class="f-acc" stroke="currentColor"/> <rect x="416" y="104" width="72" height="26" class="f-ok" stroke="currentColor"/> <text x="218 254 290 326 362 398 434 470" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">efghabcd</text> <text x="505" y="122" fill="currentColor">i XOR 4   (8비트 회전)</text>
  <path d="M236 132 L302 164M308 132 L242 164M380 132 L446 164M452 132 L386 164" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="10" y="186" fill="currentColor">거리 2</text> <rect x="200" y="168" width="36" height="26" class="f-acc" stroke="currentColor"/> <rect x="236" y="168" width="36" height="26" class="f-ok" stroke="currentColor"/> <rect x="272" y="168" width="36" height="26" class="f-acc" stroke="currentColor"/> <rect x="308" y="168" width="36" height="26" class="f-ok" stroke="currentColor"/> <rect x="344" y="168" width="36" height="26" class="f-acc" stroke="currentColor"/> <rect x="380" y="168" width="36" height="26" class="f-ok" stroke="currentColor"/> <rect x="416" y="168" width="36" height="26" class="f-acc" stroke="currentColor"/> <rect x="452" y="168" width="36" height="26" class="f-ok" stroke="currentColor"/> <text x="218 254 290 326 362 398 434 470" y="186" fill="currentColor" font-family="monospace" text-anchor="middle">ghefcdab</text> <text x="505" y="186" fill="currentColor">i XOR 6   마스크 0x33</text>
  <path d="M218 196 L250 228M254 196 L222 228M290 196 L322 228M326 196 L294 228M362 196 L394 228M398 196 L366 228M434 196 L466 228M470 196 L438 228" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="10" y="250" fill="currentColor">거리 1 = 결과</text> <rect x="200" y="232" width="288" height="26" class="f-warn" stroke="currentColor"/> <text x="218 254 290 326 362 398 434 470" y="250" fill="currentColor" font-family="monospace" text-anchor="middle">hgfedcba</text> <text x="505" y="250" fill="currentColor">i XOR 7 = 7 - i   마스크 0x55</text>
  <text x="10" y="286" fill="currentColor">32비트는 같은 그림에 단계 2개(거리 8, 16)가 더 붙는다: 5단계, 상수 4개 + 회전 1번.</text>
</svg>
```

32비트 `rev32_dc(0x12345678)` 를 단계마다 찍으면 (거리 1 → 2 → 4 → 8 → 16):

| 단계 | 값 | 2진 |
|---|---|---|
| 입력 | `0x12345678` | `00010010001101000101011001111000` |
| 거리 1 (`0x55555555`) | `0x2138A9B4` | `00100001001110001010100110110100` |
| 거리 2 (`0x33333333`) | `0x84C2A6E1` | `10000100110000101010011011100001` |
| 거리 4 (`0x0F0F0F0F`) | `0x482C6A1E` | `01001000001011000110101000011110` |
| 거리 8 (`0x00FF00FF`) | `0x2C481E6A` | `00101100010010000001111001101010` |
| 거리 16 (회전) | `0x1E6A2C48` | `00011110011010100010110001001000` |

첫 단계 결과 `0x2138A9B4` 가 그대로 `swap_odd_even32(0x12345678)` 다(그림 1 의 m = 1). 홀짝 교환은 "비트 반전의 첫 단계만" 이다.

### Part A — 256-entry 표: 어떻게 만들고 어디에 두나

| 만드는 법 | 위치 | 장단점 |
|---|---|---|
| 매크로 `R2/R4/R6` (Bit Twiddling Hacks) — 주어진 `REV8[]` | `static const` → 링커가 읽기 전용 섹션에 둔다. 이 맥 바이너리에서 `size -m` 의 `__TEXT,__const` = **528 B** (REV8 256 + PARITY8 256 + REV4 16). MCU 에선 `.rodata` = flash | RAM 0, 부팅 시간 0. 매크로가 읽기 어렵다 |
| 생성 스크립트 출력(Python 으로 `0x00, 0x80, …` 256개)을 붙여 넣기 | 위와 같음 | 가장 흔한 실무 방식. 스크립트와 표가 따로 놀 수 있다 → 테스트에서 재계산 비교 |
| 부팅 때 점화식 `rev8_table_build` | RAM 256 B (`.bss`) | 코드가 짧다. 부팅 후·ISR 켜기 전에 반드시 한 번. RAM 이 flash 보다 귀한 MCU 에선 손해 |
| C++ `constexpr` | `.rodata` | CPP-M13 에서 다룬다 |

매크로가 맞는 이유: 인덱스 i 를 2비트 숫자 넷 d3 d2 d1 d0 로 보면 반전은 각 숫자를 뒤집고(00→00, 01→10, 10→01, 11→11, 즉 0,2,1,3) 순서를 거울로 놓는 것이다. 맨 바깥 `R6(0), R6(2), R6(1), R6(3)` 은 64칸 블록 넷이고 블록 번호가 d3(인덱스의 맨 위 숫자)다 — 결과의 **맨 아래** 숫자를 0,2,1,3 으로 정한다. 한 겹 안쪽(`R6` 안의 `R4` 넷)은 d2 → 결과의 둘째 숫자(+0/2/1/3 × 4), 그 안쪽은 d1 → ×16, 맨 안쪽 `R2` 는 d0 → 결과의 맨 위 숫자(× 64). 숫자 단위(2비트)로 보면 "숫자 순서 거울 + 숫자 안 반전" 이고, 이것도 m = 6 ⊕ 1 = 7 이다.

점화식 `t[i] = (t[i >> 1] >> 1) | ((i & 1) << 7)` 의 근거: i = 2q + b0 이면 반전(i) = b0·2^7 + 반전(q) / 2 (q 는 7비트라 반전(q) 의 비트 0 은 항상 0 — 나눗셈이 정확하다). 하네스는 표를 0xEE 로 채운 뒤 빌드하고 `REV8[]` 와 `memcmp` 한다.

`rev32_lut` 는 바이트 4개를 각각 표로 뒤집고 바이트 자리를 거울로 놓는다(바이트 k → 3-k): 8비트 반전 + bswap = 32비트 반전. m 으로 쓰면 31 = 7 + 24.

### Part B — 니블: 두 가지 뜻과 64비트

| 함수 | m | 방법 |
|---|---|---|
| `swap_nibbles8` | 4 (8비트) | `(b << 4) \| (b >> 4)` 를 `uint8_t` 로 자름 |
| `nib_swap_bytes32` | 4 | 분할정복의 "거리 4" 한 단계 |
| `rev_nibbles32_loop` | 28 = 4+8+16 | 루프 8번, 4비트씩 |
| `rev_nibbles64_dc` | 60 = 4+8+16+32 | 거리 4, 8, 16, 32 네 단계 |

`rev_nibbles64_dc(0x1234567890ABCDEF)` 단계별 (실제 계산):

| 단계 | 값 |
|---|---|
| 입력 | `0x1234567890ABCDEF` |
| 거리 4 (바이트 안 니블 swap) | `0x2143658709BADCFE` |
| 거리 8 | `0x43218765BA09FEDC` |
| 거리 16 | `0x87654321FEDCBA09` |
| 거리 32 (회전) | `0xFEDCBA0987654321` ✔ |

거리 8·16·32 세 단계가 bswap64 다. 그래서 면접 답 한 줄: **"nibble reverse = swap nibbles in each byte, then byte-swap"**. 하네스는 이 관계(`rev_nibbles32(y) == __builtin_bswap32(nib_swap_bytes32(y))`)와 "64비트 = 32비트 두 개를 바꿔 붙이기", "비트 반전 = 니블 역순 ∘ 니블 안 비트 반전(m = 28 XOR 3 = 31)"을 무작위 20만 개로 확인한다.

### Part B — 패리티: 접기와 표

접기의 불변식: `x ^= x >> s` 뒤, 아래 s 비트의 비트 j 는 x_j XOR x_(j+s) 다. 아래 s 비트 전체의 XOR = 원래 아래 2s 비트 전체의 XOR. 위쪽 쓰레기 비트는 다음 단계에서 보지 않으니 지울 필요가 없다. `0x12345678` (1 이 13개):

| 단계 | 보는 비트 | 그 비트들 |
|---|---|---|
| `x ^= x >> 16` | 아래 16 | `0x444C` |
| `x ^= x >> 8` | 아래 8 | `0x08` |
| `x ^= x >> 4` | 아래 4 | `0x8` = 1000₂ |
| `x ^= x >> 2` | 아래 2 | 10₂ |
| `x ^= x >> 1` | 비트 0 | **1** (13 은 홀수) ✔ |

변형: 4비트까지 접은 뒤 `(0x6996 >> (x & 0xF)) & 1` — 16비트 상수 하나가 "4비트 패리티 표"다(0x6996 = 0110 1001 1001 0110₂, 비트 n = n 의 패리티). `parity32_lut` 는 8비트까지 접고 `PARITY8[]`. 루프 `while (x) { p ^= x & 1; x >>= 1; }` 는 최상위 1 의 위치만큼 돈다 — 시간이 입력에 따라 달라진다(벤치마크에서 실제로 보인다). Kernighan 루프(`x &= x - 1`, ALGO-B12)는 1 의 개수만큼.

### Part C — SPI LSB-first: 바이트 순서는 그대로

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b13t" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">선 위의 비트 순서 (왼쪽이 먼저 나감) — 데이터 바이트 0x12 = 0001 0010₂</text>
  <path d="M250 30 L700 30" stroke="currentColor" marker-end="url(#b13t)"/>
  <text x="705" y="34" fill="currentColor">t</text>
  <text x="10" y="58" fill="currentColor">장치가 기대 (LSB-first)</text> <rect x="250" y="42" width="320" height="24" class="f-ok" stroke="currentColor"/> <text x="270 310 350 390 430 470 510 550" y="59" fill="currentColor" font-family="monospace" text-anchor="middle">01001000</text> <text x="585" y="58" fill="currentColor">b0 → b7</text>
  <text x="10" y="94" fill="currentColor">MSB-first 로 0x12 전송</text> <rect x="250" y="78" width="320" height="24" class="f-warn" stroke="currentColor"/> <text x="270 310 350 390 430 470 510 550" y="95" fill="currentColor" font-family="monospace" text-anchor="middle">00010010</text> <text x="585" y="94" fill="currentColor">틀림</text>
  <text x="10" y="130" fill="currentColor">MSB-first 로 REV8[0x12]=0x48</text> <rect x="250" y="114" width="320" height="24" class="f-ok" stroke="currentColor"/> <text x="270 310 350 390 430 470 510 550" y="131" fill="currentColor" font-family="monospace" text-anchor="middle">01001000</text> <text x="585" y="130" fill="currentColor">일치 ✔</text>
  <text x="10" y="170" fill="currentColor" font-weight="bold">UART 8E1 프레임 (UART 는 원래 LSB-first) — uart_frame_8e1(0x12) = 0x424</text>
  <rect x="250" y="184" width="40" height="24" class="f-mut" stroke="currentColor"/> <rect x="290" y="184" width="320" height="24" class="f-acc" stroke="currentColor"/> <rect x="610" y="184" width="40" height="24" class="f-warn" stroke="currentColor"/> <rect x="650" y="184" width="40" height="24" class="f-mut" stroke="currentColor"/> <text x="270 310 350 390 430 470 510 550 590 630 670" y="201" fill="currentColor" font-family="monospace" text-anchor="middle">00100100001</text>
  <text x="270" y="226" fill="currentColor" text-anchor="middle">start</text> <text x="450" y="226" fill="currentColor" text-anchor="middle">d0 … d7</text> <text x="630" y="242" fill="currentColor" text-anchor="middle">P(짝수)</text> <text x="670" y="226" fill="currentColor" text-anchor="middle">stop</text>
  <text x="10" y="201" fill="currentColor">bit 0 → bit 10</text>
</svg>
```

핵심: LSB-first 는 **바이트 안의 비트 순서** 문제지 바이트 순서 문제가 아니다. 그래서 각 바이트에 `REV8[]` 만 적용하고 바이트 순서(`dst[i]` ↔ `src[i]`)는 그대로 둔다. 테스트는 7바이트 메시지와 무작위 1000바이트(제자리)에서 `wire_bits(src, LSB-first) == wire_bits(prepared, MSB-first)` 를 비트 8000개 단위로 비교하고, 한 번 더 적용하면 원래 버퍼가 되는지(수신 방향)도 본다. 이 연산은 m = 7 (바이트마다 비트 반전) 이고, RISC-V 에는 이것 하나만 하는 `brev8`(Zbkb) 명령이 있다(RISCV-M02).

주의: 장치가 **여러 바이트짜리 필드 전체**를 LSB-first 로 원하면(필드의 비트 0 이 맨 먼저) 바이트 순서도 바뀐다 — 필드를 리틀 엔디안 바이트로 놓고 각 바이트를 뒤집어야 한다. 데이터시트 타이밍도의 첫 비트 이름(D0 인지 D23 인지)을 확인하라.

### Part C — UART 8E1: 패리티가 잡는 것과 못 잡는 것

`uart_frame_8e1(d) = (1 << 10) | (P(d) << 9) | (d << 1)`. 짝수 패리티(even)는 "데이터 + 패리티 비트의 1 개수가 짝수"가 되게 붙인다 → P(d) = popcount(d) 의 홀짝. 디코더는 start = 0, stop = 1 (아니면 -1), 그다음 `P(d) XOR 받은 패리티 비트 != 0` 이면 -2.

실제 전수 결과 (solution 실행 출력):

```text
uart 8E1: roundtrip 256/256, 1-bit parity err 2304/2304, framing 512/512, 2-bit undetected 7168/7168
```

- 데이터·패리티 9비트 중 어느 1비트가 뒤집혀도 1 의 개수 홀짝이 바뀌므로 256 × 9 = 2304 개 **전부** 검출.
- 데이터 2비트 오류는 홀짝이 그대로 → 256 × C(8,2) = 7168 개 **전부** 통과(잘못된 데이터를 OK 로). 패리티는 "홀수 개 오류 검출기"다. 2비트 이상이 걱정되면 CRC(ALGO-M01) 또는 Hamming/SECDED(MEM-B06/B07).
- STM32 USART 처럼 패리티를 켜면 **워드 길이 안의 MSB 자리를 패리티가 차지**하는 주변장치가 있다(7E1 = 워드 길이 8 + 패리티 켬). 레퍼런스 매뉴얼의 USART 패리티 절을 확인하자 — 레지스터 설정 실수 1순위.

### 벤치마크 — `demo_bench.c` (속도·메모리)

이 맥(Apple Silicon, `sysctl` 표기 Apple M6), `-O2`, 측정마다 워밍업 1회 + 9회 중앙값, 결과는 `volatile` sink 로. **chain** = `x = f(x) ^ i` (앞 결과가 다음 입력 → 지연시간), **array** = 독립 입력 4096개 × 256회 (처리량, 컴파일러 자동 벡터화 허용). 세 번 돌린 것 중 하나 (실제 출력; 실행마다 LUT 줄은 0.5↔0.9 ns 정도 흔들렸다):

```text
reverse bits 32:
  rev32_loop             chain   9.44 ns/op   array   9.27 ns/op
  rev32_dc               chain   0.47 ns/op   array   0.08 ns/op
  rev32_lut (256 B)      chain   2.96 ns/op   array   0.79 ns/op
  rev32_lut16 (16 B)     chain   3.73 ns/op   array   1.45 ns/op
  __builtin_bitreverse32 chain   0.46 ns/op   array   0.08 ns/op
reverse nibbles 32:
  rev_nibbles32_loop     chain   2.50 ns/op   array   0.45 ns/op
  rev_nibbles (dc)       chain   0.97 ns/op   array   0.14 ns/op
parity 32:
  parity32_loop          chain   4.66 ns/op   array  13.35 ns/op
  parity32_fold          chain   2.63 ns/op   array   0.21 ns/op
  parity32_lut (256 B)   chain   2.67 ns/op   array   0.48 ns/op
  __builtin_parity       chain   2.58 ns/op   array   0.10 ns/op
SPI buffer 4 KiB, reverse bits in every byte:
  rev8_loop               0.017 ns/byte  (58.25 GB/s)
  rev8_dc                 0.016 ns/byte  (61.68 GB/s)
  REV8[b] (256 B)         0.185 ns/byte  (5.41 GB/s)
  __builtin_bitreverse8   0.010 ns/byte  (95.33 GB/s)
tables: REV8 256 B, PARITY8 256 B, REV4 16 B (all static const)
```

읽는 법 (절대 ns 가 아니라 **비율**):

- `rev32_dc` 가 builtin 과 같은 속도다 — clang 이 다섯 단계를 **`rbit` 한 명령**으로 알아봤기 때문(아래 어셈블리). 반면 arm64 에서 `rev32_loop` 는 알아보지 못하고 32번 도는 루프로 남아 ~20배 느리다.
- LUT 는 이 CPU 에서 분할정복보다 **느리다**(chain 6배, array 10배). 로드 4개 + 조립이 `rbit` 1개를 못 이긴다. SPI 버퍼에서는 더 벌어진다: 루프·dc·builtin 셋 다 NEON `rbit.16b`(16바이트를 한 번에)로 벡터화된 것을 `-S` 출력에서 확인했고, 표 조회는 벡터화가 안 된다 → ~10배.
- `parity32_loop` 의 chain(4.7) < array(13.4) 는 모순이 아니다: chain 에서는 입력이 `parity ^ i` (12비트 이하)라 루프가 12번 이하로 돌고, array 는 무작위 32비트라 ~32번 돈다. **데이터에 따라 시간이 변하는 코드**는 이렇게 측정 방법에 따라 숫자가 바뀐다 — ISR 최악 시간(WCET)을 잡을 때 위험한 성질.
- `__builtin_parity` 는 arm64 스칼라에서 접기와 같은 코드가 나왔다(아래). array 에서 더 빠른 건 루프 전체가 NEON `cnt.16b` 로 벡터화돼서다(`-S` 에서 확인).

메모리: `REV8` 256 B, `PARITY8` 256 B, 니블 표 `REV4` 16 B. 분할정복은 표 0 B, 대신 32비트 상수 3~4개(아래 M0 리터럴 풀 12 B).

### 어셈블리 — `demo_asm.c` (arm64 · Cortex-M4 · Cortex-M0)

`clang -O2 -S` (Apple clang 21) 실제 출력에서 함수 본문만:

```text
arm64 (이 맥)                 Cortex-M4 (thumbv7em)          Cortex-M0 (thumbv6m)
_rev32_dc:                    rev32_dc:                      rev32_dc:
    rbit  w0, w0                  rbit  r0, r0                   rev   r0, r0
    ret                           bx    lr                       lsrs  r1, r0, #4
_rev8_dc:                     rev32_loop:                        ldr   r2, .LCPI1_0   @ 0x0f0f0f0f
    rbit  w8, w0                  rbit  r0, r0   ← 루프도!        ands  r1, r2
    lsr   w0, w8, #24             bx    lr                       ... (거리 2, 1 단계: 총 19 명령 + bx)
_rev32_loop:  (32번 도는 루프 그대로, bfi 사용)
```

- Cortex-M4 에서는 `rev32_loop` 까지 `rbit` 으로 바뀌었다(루프를 다 펼친 뒤 패턴을 알아본 것으로 보인다). arm64 에선 같은 루프가 안 바뀌었다 — 컴파일러 인식은 **보장이 아니다**.
- Cortex-M0(ARMv6-M)에는 `rbit` 이 없다. `rbit r0, r0` 를 thumbv6m 으로 어셈블하면 `error: instruction requires: thumb2`. 대신 clang 은 `rev`(바이트 역순, M0 에도 있음) + 거리 4·2·1 세 단계로 만든다 — 바이트 역순 = 거리 8·16 두 단계를 명령 하나로 처리한 것.
- 니블 역순 64비트는 M4 에서 니블 swap 후 반쪽마다 `rev` (10 명령 + `bx`), arm64 에서는 니블 swap + `rev16` + 손 조립(12 명령 + `ret`)으로 나왔다 — `rev x` 한 번으로 줄지는 않았다.
- 패리티 접기: arm64 `eor w8, w0, w0, lsr #16` 처럼 시프트가 붙은 XOR 5개 + `and`. M4 도 같은 모양(`eor.w … lsr #16`). M0 는 시프트와 XOR 를 따로 해서 10개 + 2개.

함수 크기(`nm -S`, 리터럴 풀 포함, 바이트):

| 함수 | Cortex-M4 | Cortex-M0 |
|---|---|---|
| `rev32_loop` | 6 | 130 |
| `rev32_dc` | 6 | 52 |
| `rev32_lut` (+ 표 256 B 별도) | 42 | 40 |
| `swap_odd_even32` | 20 | 24 |
| `parity32_fold` | 26 | 26 |

결론: **M4/M7·A-class → 분할정복(또는 `__RBIT`)을 쓰면 컴파일러가 `rbit` 으로**. M0 → dc 52 B vs LUT 40 B + 256 B. M0 의 `ldrb` 는 2 사이클이라 LUT 도 빠르지 않다(명령 수 비슷 — 사이클은 실측 안 함). flash 대기 상태(wait state)가 있는 고속 MCU 에서는 표 로드가 더 느려질 수 있다.

CMSIS: `__RBIT()` 은 `cmsis_gcc.h` (CMSIS-Core V5.4.1, 이 맥의 STM32Cube F4 1.28.3 사본에서 확인)에서 ARMv7-M/7E-M/8-M Mainline 이면 인라인 asm `rbit`, 그 밖(v6-M 등)이면 **C 루프**(`for (value >>= 1U; value != 0U; value >>= 1U)` + 마지막 `result <<= s`)로 정의된다 — v6-M 에서는 입력에 따라 시간이 다르다. `__REV()` 는 GCC ≥ 4.5 면 `__builtin_bswap32` → `rev`. `__REV16`, `__REVSH` 도 있다(C-B13).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
# uart 8E1: roundtrip 256/256, 1-bit parity err 2304/2304, framing 512/512, 2-bit undetected 7168/7168
# 42/42 passed   (0.2 초 이내)
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 42/42, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

테스트 설계: 모든 함수의 기준은 **인덱스로 정의한 참조** `ref_xor_perm(x, n, m)` (비트 i 를 i^m 자리에 놓는 32/64번 루프) 하나다. 구현 기법(시프트·마스크·표)과 전혀 다른 정의라 같은 실수를 공유하지 않는다. 8비트는 256개 전수, 32비트는 16비트 전수를 세 모양(아래 반 `v`, 위 반 `v<<16`, 양쪽)으로 + 무작위 20만 + 걷는 1/걷는 0 + "두 번 적용 = 원래 값", 64비트는 무작위 20만. 테스트 쪽에서만 `__builtin_bitreverse32` 와도 대조한다.

## 7. 자주 하는 실수

**1. 같은 마스크를 양쪽에, 시프트 순서 반대로.** `((x & 0x55555555) >> 1) | ((x & 0x55555555) << 1)` — 위쪽 짝(홀수 비트)을 내리려면 `x >> 1` 을 **먼저** 하고 마스크해야 한다(또는 `0xAAAAAAAA` 로 마스크 후 시프트). 실제로 첫 단계만 이렇게 바꾸면 37/42 (`first mismatch rev32_dc: in=0x10000 got=0x28000 want=0x8000`).

**2. `uint8_t` 표 값을 캐스트 없이 `<< 24`.** `REV8[x & 0xFF] << 24` 는 int 승격 후 시프트라 0x80 << 24 = 2^31 이 int 에 안 들어간다 → UB (C11 6.5.7p4). 실제로 이 변형은 `-O2` 에서 **42/42 통과**하고, UBSan 에서만 `runtime error: left shift of 128 by 24 places cannot be represented in type 'int'` 가 나왔다. 조용한 UB 라 더 위험하다 → `(uint32_t)REV8[...] << 24`.

**3. SPI LSB-first 를 "바이트 순서 뒤집기"로.** 엔디안 스왑과 비트 반전을 혼동해 `dst[i] = src[n - 1 - i]` 로 짜면 38/42 (선 위 비트 비교 3개 + 바이트 확인 1개 FAIL). 비트 반전은 **바이트 안**, bswap 은 **바이트 사이**. 32비트 비트 반전 = 둘 다(m = 7 + 24).

**4. 접기를 한 단계 빼먹기.** `x ^= x >> 1` 없이 `x & 1` → 40/42 (`first mismatch parity32_fold: in=0xc41a0000 got=0x1 want=0x0`). 32비트는 5단계(16, 8, 4, 2, 1)가 모두 필요하다. 0 과 0xFFFFFFFF 테스트는 이 버그를 못 잡는다.

**5. 짝수/홀수 패리티 혼동.** `P(d) ^ 1` (홀수 패리티)로 프레임을 만들면 짝수로 검사하는 디코더가 전부 -2: `roundtrip 0/256`, 38/42. 송수신 양쪽 설정(8E1 vs 8O1)이 다르면 실제로 모든 바이트가 parity error 로 버려진다.

**6. n 비트 반전에서 `>> (32 - n)` 의 n = 0.** 32 시프트는 UB (C11 6.5.7p3: 시프트 양 ≥ 폭). 연습 4 는 n = 0 을 따로 처리한다. 같은 이유로 `(1u << n) - 1` 마스크도 n = 32 에서 UB(C-B01).

**7. 8비트 함수를 넓은 타입으로.** `uint32_t swap_nib(uint32_t b) { return (b << 4) | (b >> 4); }` 에 0xAB 를 넣으면 0xABA — 위로 밀린 니블이 잘리지 않는다. 반환형을 `uint8_t` 로 하거나 `& 0xFF`.

**8. 뜻을 확인하지 않고 짠다.** "reverse the nibbles" 에 `nib_swap_bytes32` 를 내면 0x12345678 → 0x21436587, 면접관이 원한 것은 0x87654321 일 수 있다. 예시 출력에서 **바이트 안 순서까지 바뀌었는지**(`ef` → `fe`)가 단서다.

## 8. 면접 Q&A

<details><summary>Q1. "Reverse the nibbles of this value." — 코드 쓰기 전에 뭐라고 하겠어요?</summary>

소리 내서: "Just to confirm the unit and the meaning: do you want the two nibbles **inside each byte** swapped, so 0x12 becomes 0x21 and 0x12345678 becomes 0x21436587 — or the **order of all hex digits** reversed, so 0x12345678 becomes 0x87654321? And is the input a uint32/uint64 value, or a byte buffer in memory — if a buffer, is byte 0 the most significant digit pair?" 예시가 주어졌으면 예시로 확정한다: `0x1234567890abcdef → 0xfedcba0987654321` 은 `ef` 가 `fe` 가 되었으니 니블 단위 전체 역순(bswap 이면 `0xefcdab90…`). 그다음 루프 → trace 2스텝(니블 단위로 "0xf, 0xfe…") → 테스트(0, 0xF → 최상위, 두 번 = 원래, 예시) → 빠른 버전(니블 swap + bswap, M4 에선 `rev` 두 개) → 버퍼 버전(연습 2) 순서로 꺼낸다. 꼬리: "버퍼 길이가 홀수면?" → 가운데 바이트는 자기 니블만 swap.

</details>

<details><summary>Q2. 루프 · 분할정복 · LUT 중 무엇을 출하하겠어요? 코어별로.</summary>

Cortex-M3/M4/M7, Cortex-A, arm64: `__RBIT()` 또는 분할정복 C (clang 이 `rbit` 으로 바꾸는 것을 이 레슨에서 확인 — 단 보장은 아니니 핫패스면 `-S` 로 확인하거나 `__RBIT`). 1 명령, 표 0 B, 상수 시간. Cortex-M0/M0+ (v6-M, `rbit` 없음): 분할정복이 `rev` + 3단계로 약 20 명령/52 B, LUT 는 40 B + 표 256 B 에 `ldrb` 4번. flash 가 넉넉하고 바이트 단위로 대량 처리하면(SPI 버퍼) `REV8[b]` 가 가장 단순하다. 단 이 맥에서는 LUT 가 벡터화가 안 돼 ~10배 느렸다 — SIMD 가 있는 CPU 에서 "LUT 가 빠르다"는 옛 상식이 틀린다. 루프는 코드가 가장 짧지만 32번 돈다. CMSIS v6-M `__RBIT` 의 C 루프처럼 조기 종료하면 입력에 따라 시간이 달라 WCET 에 불리.

</details>

<details><summary>Q3. 엔디안 스왑과 비트 반전은 어떻게 다르고, 각각 언제 쓰나요?</summary>

bswap(m = 24): 바이트 단위 순서만 바꾼다 — 메모리 표현의 문제(리틀 엔디안 CPU 가 빅 엔디안 네트워크/레지스터 값을 읽을 때, C-B13/C-M05). 비트 반전(m = 31)은 바이트 안의 비트까지 — **직렬 선 위의 순서** 문제(LSB-first vs MSB-first 직렬 인터페이스, 반사 CRC, FFT 인덱스). SPI LSB-first 장치에 버퍼를 보낼 때는 m = 7(바이트마다 비트만)이다. ARM 명령으로는 `REV`(bswap), `REV16`(16비트 반쪽마다 bswap), `RBIT`(비트 반전). 꼬리: "UART 는 LSB-first 인데 왜 소프트웨어가 신경 안 쓰나?" → UART 주변장치가 직렬화 순서를 하드웨어로 처리하고, 소프트웨어는 바이트 값만 본다. 비트 순서가 소프트웨어로 새어 나오는 건 비트뱅(bit-bang), 로직 애널라이저 디코딩(COMM-A10), 반사 CRC 같은 곳이다.

</details>

<details><summary>Q4. CRC 에 "reflected" 가 붙은 건 뭐고, 비반사 엔진으로 반사 CRC 를 낼 수 있나요?</summary>

refin = 입력 바이트를 LSB 먼저 넣는다, refout = 최종 레지스터를 비트 반전해서 낸다. LSB-first 로 보내면서 하드웨어 시프트 레지스터로 CRC 를 계산하던 직렬 링크(UART 계열, 이더넷)에서 온 정의다. 소프트웨어로는 레지스터를 **오른쪽으로** 시프트하고 다항식도 반전한 값(0x8005 → 0xA001, CRC-32 0x04C11DB7 → 0xEDB88320)을 쓰면 반전 연산 없이 바로 나온다. 연습 3 에서 실제로 확인: CRC-16/ARC(반사, init 0) = rev16(CRC-16/UMTS(바이트마다 비트 반전한 입력)), 그리고 check 값 ARC 0xBB3D · UMTS 0xFEE8 · MODBUS 0x4B37 이 RevEng 카탈로그와 일치. init 이 0 이 아니면 init 도 rev16 해서 넣어야 한다. 꼬리: "STM32 CRC 유닛과 소프트웨어 CRC-32 가 안 맞아요" → 입력/출력 반사 옵션과 워드 바이트 순서(COMM-B09).

</details>

<details><summary>Q5. 패리티는 무엇을 잡고 무엇을 못 잡나요? 어디에 아직 쓰이나요?</summary>

홀수 개 비트 오류는 전부, 짝수 개는 전혀 못 잡는다 — 이 레슨 전수 결과: 1비트 2304/2304 검출, 데이터 2비트 7168/7168 미검출. 고칠 수도 없다(어느 비트인지 모름). 그래도 싸다(XOR 트리, 하드웨어 게이트 몇 개, 지연 1단). 쓰이는 곳: UART, SWD 요청/데이터 패리티(TOOL-A02), LIN PID 의 P0/P1(AUTO-B06), ARINC 429 홀수 패리티(AERO-B03), 캐시 태그·SRAM 패리티(오류 나면 다시 읽어 오면 되는 곳). 정정이 필요하면 Hamming/SECDED(MEM-B06/B07), 버스트 오류는 CRC. 꼬리: "x86 은 패리티 명령이 있나?" → 전용 명령은 없고 PF 플래그가 결과 **하위 8비트**의 짝수 패리티를 나타낸다. 32비트 패리티는 접기 또는 `popcnt & 1`.

</details>

<details><summary>Q6. 32비트 MCU 에서 64비트 비트 반전은? ISR 에서 써도 되나요?</summary>

rev64(x) = (rev32(lo) << 32) | rev32(hi) — 반쪽마다 뒤집고 자리를 바꾼다(m = 63 = 31 + 32, "거리 32" 단계 = 레지스터 두 개 맞바꾸기, 비용 0). 실제로 M4 에서 `rev_nibbles64_dc` 는 반쪽마다 니블 swap 후 `rev r0` / `rev r1` 로 바꿔 넣은 10 명령(+ `bx`)이 됐다. ISR: 이 함수들은 전부 순수 함수(전역 상태 없음, 재진입 가능)이고 `const` 표는 flash 라 초기화 순서 문제도 없다. 런타임으로 만든 표(`rev8_table_build`)라면 ISR 을 켜기 **전에** 만들어야 하고, 루프·조기 종료 버전은 최악 시간을 따로 잡아야 한다. 스택 사용은 0~16 B 수준.

</details>

## 9. 추가 연습

### 연습 1 — `perm_xor32(x, m)`: 하나로 다 하기

m(0..31)의 비트 k 가 1 이면 "거리 2^k 짝 바꾸기"를 적용하는 함수를 쓰고, m = 31, 1, 4, 28, 24, 7 이 각각 비트 반전·홀짝·니블 swap·니블 역순·bswap·바이트마다 비트 반전임을, 그리고 `perm(perm(x, a), b) == perm(x, a ^ b)` 를 무작위로 확인하라.

<details><summary>답 보기</summary>

```c
static const uint32_t lo[5] = { 0x55555555u, 0x33333333u, 0x0F0F0F0Fu, 0x00FF00FFu, 0x0000FFFFu };
for (unsigned k = 0; k < 5; k++)
    if ((m >> k) & 1u) { unsigned s = 1u << k; x = ((x >> s) & lo[k]) | ((x & lo[k]) << s); }
```

s = 16 에서도 마스크를 쓰면 회전과 같다(`(x >> 16) & 0xFFFF` | `(x & 0xFFFF) << 16`). 합성 법칙은 5. 풀이 "분할정복: 인덱스의 비트를 하나씩 뒤집는다" 의 증명 그대로다(XOR 은 교환·결합). 이 틀로 안 되는 것: 회전, 임의 순열(Hacker's Delight 7장의 일반 순열 기법). `perm_xor32(0x12345678, 24) == __builtin_bswap32(0x12345678)` 도 확인.

</details>

### 연습 2 — 버퍼 니블 역순 (스크린 꼬리질문 1순위)

`void rev_nibbles_buf(uint8_t *buf, size_t n)`: 바이트 배열(`buf[0]` = 가장 높은 자리, 쓴 순서 그대로)의 hex 자리 순서를 제자리에서 뒤집어라. `{12 34 56 78 90 ab cd ef}` → `{fe dc ba 09 87 65 43 21}`, 길이 3·1·0, 그리고 8바이트 버퍼 = 빅 엔디안으로 읽은 64비트의 니블 역순인지 무작위로 확인.

<details><summary>답 보기</summary>

두 포인터(ALGO-B09)로 양 끝 바이트를 맞바꾸면서 **각자 니블 swap**. 홀수 길이면 가운데 바이트는 자기 니블만 swap. `n == 0` 이면 `n - 1` 이 `SIZE_MAX` 로 넘어가니 먼저 반환.

```c
if (n == 0) return;
size_t i = 0, j = n - 1;
while (i < j) { uint8_t a = buf[i]; buf[i++] = swap_nib(buf[j]); buf[j--] = swap_nib(a); }
if (i == j) buf[i] = swap_nib(buf[i]);
```

`{12 34 56}` → `{65 43 21}`. O(n) 시간, O(1) 추가 메모리. 면접에서 "메모리에 있는 uint64 를 버퍼로 주면?" 이라고 하면 리틀 엔디안 CPU 에서 `0x1234567890abcdef` 는 `ef cd ab 90 78 56 34 12` 로 놓인다는 것부터 말한다.

</details>

### 연습 3 — 반사 CRC = 비트 반전 두 번

다항식 0x8005 로 MSB-first(비반사) CRC-16 과, 오른쪽 시프트 + 0xA001 로 LSB-first(반사) CRC-16 을 짜라. "123456789" 로 CRC-16/UMTS(init 0, 비반사) = 0xFEE8, CRC-16/ARC(init 0, 반사) = 0xBB3D, CRC-16/MODBUS(init 0xFFFF, 반사) = 0x4B37 을 확인하고, 반사(d, init) = rev16(비반사(바이트마다 rev8(d), rev16(init))) 을 무작위 길이·init 2000개로 확인하라.

<details><summary>답 보기</summary>

반사 엔진은 비반사 엔진의 레지스터를 **거울에 비춘** 것과 같은 상태를 들고 간다: 바이트를 위가 아닌 아래에 XOR 하고, 오른쪽으로 시프트하고, 빠져나가는 비트(비트 0)가 1 이면 거울 다항식 0xA001 을 XOR. 실제 출력 `CRC-16/UMTS=0xFEE8  CRC-16/ARC=0xBB3D  CRC-16/MODBUS=0x4B37` — 세 값 모두 RevEng 카탈로그 check 값과 같다. 교훈: 반사 CRC 를 만들려고 바이트마다 `REV8[]` 를 부르고 끝에 rev16 하는 건 맞지만 느리다 — 엔진을 거울로 짜면 반전이 0번이다. 테이블/slicing 가속은 ALGO-M02, 비트 단위 엔진 파라미터화는 ALGO-M01.

</details>

### 연습 4 — FFT 비트 반전 재배열

`rev_n(i, nbits)`(1 ≤ nbits ≤ 32, nbits = 0 → 0)과 `bitrev_permute(a, log2n)` 을 짜라. N = 8 이면 순서가 `0 4 2 6 1 5 3 7`, N = 1024 에서 a[i] = rev10(i), 두 번 하면 원래 배열.

<details><summary>답 보기</summary>

`rev_n(i, n) = rev32(i) >> (32 - n)` — 32비트로 뒤집으면 i 의 아래 n 비트가 맨 위 n 비트로 가니 내려 준다. n = 0 은 시프트 32(UB)라 따로. 재배열은 `j = rev_n(i)` 가 `i` 보다 클 때만 맞바꾼다(짝마다 한 번; `j == i` 는 제자리, `j < i` 는 이미 바뀜). O(N) 이고 rev 가 `rbit` 이면 원소당 몇 명령. DSP-M01 의 radix-2 FFT 첫 단계가 이것이고, CMSIS-DSP 는 미리 계산한 비트 반전 표(`armBitRevTable` 류 — 이름은 버전마다 다를 수 있음)를 쓴다.

</details>

### 연습 5 — 16 B 니블 표

`REV4[16]` 하나로 32비트 반전을 하라(니블 8개를 각각 표로 뒤집고 니블 순서를 역순으로). 16비트 전수(아래/위 반) + 무작위 10만으로 `rev32_dc` 와 대조.

<details><summary>답 보기</summary>

비트 반전 = 니블 역순(m = 28) ∘ 니블 안 비트 반전(m = 3). 루프 `r = (r << 4) | REV4[x & 0xF]; x >>= 4;` 8번. 표 16 B 는 M0 에서도 부담이 없고 레지스터에 들어갈 만큼 작다(64비트 상수 하나로 넣는 변형도 가능). 이 맥 실측: chain 3.7 ns, array 1.45 ns — 256 B 표(3.0 / 0.8)보다 조금 느리고 `rbit` 보다는 한참 느리다. 표 크기와 속도의 교환: 16 B ↔ 256 B ↔ 0 B(+명령).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
# CRC-16/UMTS=0xFEE8  CRC-16/ARC=0xBB3D  CRC-16/MODBUS=0x4B37
# 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 16/16, 보고 없음
```

## 10. 핵심 정리

- 먼저 묻기: **bits / nibbles / bytes?** 니블이면 "바이트 안 swap(0x12→0x21)" vs "hex 자리 전체 역순(0x12345678→0x87654321)", 정수 vs 버퍼, 버퍼면 byte 0 이 최상위인지.
- 모든 재배열 = 비트 i → i XOR m. 거리 2^k 짝 바꾸기 = m 의 비트 k. 반전 31, 홀짝 1, 니블 swap 4, 니블 역순 28, bswap 24, 바이트마다 반전 7 (64비트 니블 역순 60).
- 한 단계: `((x >> s) & m) | ((x & m) << s)`, m = 아래쪽 짝 자리. 비트 반전 32 = 5단계, 니블 역순 = 니블 swap + bswap.
- 표: 매크로/생성 스크립트 → `static const` (flash, 이 맥 `__TEXT,__const`), 런타임 점화식 `t[i] = (t[i>>1] >> 1) | ((i&1) << 7)` → RAM 256 B. `(uint32_t)` 캐스트 먼저(승격 UB).
- 패리티: 접기 16·8·4·2·1 후 `& 1` (또는 4비트까지 접고 `0x6996 >> x`). 1비트 오류 100% 검출, 2비트 0%.
- 이 맥: dc = builtin = `rbit` 1개(0.47 ns chain), LUT 는 6~10배 느림(벡터화 안 됨), arm64 에서 루프는 20배. M4: `rbit` 있음(루프까지 인식), M0: 없음 → `rev` + 3단계 52 B. CMSIS `__RBIT`/`__REV`.
- 펌웨어: SPI LSB-first = 바이트마다 `REV8[]`, 바이트 순서는 그대로. 반사 CRC 는 거울 엔진(0xA001, 0xEDB88320). UART 는 LSB-first + 패리티.

## 참고자료

- Sean Eron Anderson, *Bit Twiddling Hacks* — "Reverse bits the obvious way", "Reverse bits in word by lookup table"(R2/R4/R6 매크로), "Compute parity …"(P2/P4/P6, 0x6996 기법): <https://graphics.stanford.edu/~seander/bithacks.html>
- H. S. Warren Jr., *Hacker's Delight* 2nd ed., Addison-Wesley 2012 — 5장 Counting Bits(5-2 Parity), 7장 Rearranging Bits and Bytes(7-1 Reversing Bits and Bytes, 일반 순열).
- LeetCode 190 Reverse Bits. 홀짝 교환은 *Cracking the Coding Interview* 6판 5장 "Pairwise Swap" (문제 번호는 기억에 의존).
- Arm, *ARMv7-M Architecture Reference Manual* — RBIT, REV, REV16, REVSH. *Cortex-M0 Devices Generic User Guide* — ARMv6-M 명령 집합(REV/REV16/REVSH 있음, RBIT 없음; 이 레슨에서 어셈블러 오류로도 확인).
- CMSIS-Core `cmsis_gcc.h` V5.4.1 — `__RBIT`, `__REV`, `__REV16`, `__REVSH` (이 맥 `~/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3/Drivers/CMSIS/Core/Include/cmsis_gcc.h` 에서 확인). 원본: <https://github.com/ARM-software/CMSIS_5>
- RevEng CRC Catalogue, 16-bit: <https://reveng.sourceforge.io/crc-catalogue/16.htm> — CRC-16/ARC check 0xBB3D, CRC-16/UMTS check 0xFEE8, CRC-16/MODBUS check 0x4B37 (연습 3 계산 결과와 일치).
- Ross N. Williams, "A Painless Guide to CRC Error Detection Algorithms", 1993 — 반사(reflected) 알고리즘과 refin/refout.
- RISC-V Bit-Manipulation ISA extensions — Zbb `rev8`(바이트 역순), Zbkb `brev8`(바이트마다 비트 반전): <https://github.com/riscv/riscv-bitmanip>
- Don 의 스크린 회고: `job_interview_prep_research/rivian_nstaff_silicon_validation_engineer/2026-10-31_N02_screen_coding_retro.md` (실제 문제, 4가지 버전, 다음 라운드 습관).
- 관련 레슨: C-B01 (마스크·시프트 UB), C-B13 (엔디안·bswap), C-M05 (엔디안 독립 직렬화), ALGO-B09 (투 포인터), ALGO-B12 (popcount·Kernighan), ALGO-A07 (Gray 코드·SWAR), ALGO-M01/M02 (CRC 비트 단위·테이블), ARM-B06 (RBIT/REV/UBFX), RISCV-M02 (Zbb), COMM-B01 (UART 프레임), COMM-B04 (SPI 모드), COMM-B09 (HW CRC 유닛 반사 옵션), COMM-A10 (LA 디코더), MEM-B06/B07 (패리티·Hamming·SECDED), TOOL-A02 (SWD 패리티), AUTO-B06 (LIN PID 패리티), AERO-B03 (ARINC 429), DSP-M01 (FFT 비트 반전), CPP-M13 (constexpr 표)

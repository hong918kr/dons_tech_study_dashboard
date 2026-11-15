---
id: ALGO-M02
time: 80분
updated: 2026-11-11
---
# ALGO-M02 CRC 가속: 테이블 방식과 CRC-32 Slicing

## 학습 목표

- CRC-32/ISO-HDLC(zlib·Ethernet·PNG 의 그 CRC-32)를 **비트 단위 → 256엔트리 바이트 테이블(+ 생성기) → slicing-by-4 / slicing-by-8** 로 단계적으로 바꾸고, 각 단계가 왜 같은 값을 내는지 **선형성(linearity) 보조정리 하나**로 증명한다.
- 테스트 쪽에서 zlib `crc32()` / `crc32_combine()` / `get_crc_table()` 과 전수·랜덤 대조하고, 카탈로그 check 값 0xCBF43926 으로 확인한다.
- 처리량(MB/s)과 테이블 메모리(0 B / 64 B / 1 KiB / 4 KiB / 8 KiB)를 이 맥에서 실측하고, 같은 루프가 Cortex-M4/M0 에서 **바이트당 몇 명령**인지 실제 asm 으로 본다 — "slicing 은 M0 에선 이득이 없다"를 숫자로 말한다.
- 스트리밍(청크로 나눠 이어 부르기 = 한 번에)과 **crc32_combine**(GF(2) 다항식 곱셈 x^(8n) mod P)을 구현하고, 펌웨어 이미지의 **잔여값(residue) 검사**와 여러 DMA 블록의 병렬 CRC 를 합치는 데 쓴다.

## 1. 문제

부트로더의 이미지 검사, 플래시 쓰기 검증, 이더넷 FCS — 펌웨어 코딩 면접에서 "CRC 짜 보세요"의 다음 꼬리질문은 거의 항상 "**이거 빠르게 하려면?**" 이다. 답은 테이블이고, 시니어 라운드에서는 "테이블이 왜 맞는지, 메모리는 얼마, M0 에선?, 조각으로 나눠 계산하면?"까지 간다.

ALGO-M01 이 CRC 를 다항식 나눗셈으로 이해하고 파라미터(poly, init, refin/refout, xorout)를 맞추는 비트 단위 엔진을 다뤘다. 이 레슨은 **하나의 CRC 만** 골라서 빠르게 만든다. 대상은 가장 흔한 CRC-32:

| 이름 (RevEng 카탈로그) | width | poly | init | refin/refout | xorout | check("123456789") | residue |
|---|---|---|---|---|---|---|---|
| CRC-32/ISO-HDLC (별칭 CRC-32, zlib, PKZIP, Ethernet FCS) | 32 | 0x04C11DB7 | 0xFFFFFFFF | true / true | 0xFFFFFFFF | **0xCBF43926** | 0xDEBB20E3 |

반사형(reflected)이라 비트가 LSB 부터 들어가고, 코드에서는 다항식을 비트 순서 반전한 **0xEDB88320** 을 쓰며 레지스터를 **오른쪽으로** 민다 (반사가 왜 있는지 — UART 가 LSB 먼저 보낸다 — 는 ALGO-M01). API 는 zlib 과 같은 모양 `crc = f(crc, p, n)` 으로, 처음엔 `crc = 0` 을 넘기고 이어서 부르면 스트리밍이 된다(안에서 `~crc` 로 시작해 `~c` 로 끝나므로).

세 구현의 차이는 **한 바이트를 먹을 때 레지스터 c 에 걸리는 직렬 의존 사슬(dependency chain)** 의 길이다:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">레지스터 c 를 따라가는 직렬 사슬 — 짧을수록 빠르다</text>
  <text x="10" y="56" fill="currentColor">비트 단위</text>
  <g stroke="currentColor">
    <rect x="120" y="40" width="44" height="24" class="f-mut"/><rect x="176" y="40" width="44" height="24" class="f-mut"/>
    <rect x="232" y="40" width="44" height="24" class="f-mut"/><rect x="288" y="40" width="44" height="24" class="f-mut"/>
    <rect x="344" y="40" width="44" height="24" class="f-mut"/><rect x="400" y="40" width="44" height="24" class="f-mut"/>
    <rect x="456" y="40" width="44" height="24" class="f-mut"/><rect x="512" y="40" width="44" height="24" class="f-mut"/>
  </g>
  <g font-family="monospace" font-size="12">
    <text x="128" y="57" fill="currentColor">bit0</text><text x="184" y="57" fill="currentColor">bit1</text><text x="240" y="57" fill="currentColor">bit2</text><text x="296" y="57" fill="currentColor">bit3</text>
    <text x="352" y="57" fill="currentColor">bit4</text><text x="408" y="57" fill="currentColor">bit5</text><text x="464" y="57" fill="currentColor">bit6</text><text x="520" y="57" fill="currentColor">bit7</text>
  </g>
  <text x="570" y="50" fill="currentColor">바이트당 8단계</text>
  <text x="570" y="66" fill="currentColor">(shift, and, xor)</text>
  <text x="10" y="126" fill="currentColor">바이트 테이블</text>
  <g stroke="currentColor">
    <rect x="120" y="108" width="190" height="28" class="f-acc"/><rect x="340" y="108" width="190" height="28" class="f-acc"/>
  </g>
  <text x="128" y="127" fill="currentColor" font-family="monospace" font-size="12">T[(c^b0)&amp;FF] ^ c&gt;&gt;8</text>
  <text x="348" y="127" fill="currentColor" font-family="monospace" font-size="12">T[(c^b1)&amp;FF] ^ c&gt;&gt;8</text>
  <line x1="310" y1="122" x2="338" y2="122" stroke="currentColor" marker-end="url(#m02a)"/>
  <text x="545" y="118" fill="currentColor">바이트당 조회 1번</text>
  <text x="545" y="134" fill="currentColor">(조회도 c 를 기다린다)</text>
  <text x="10" y="196" fill="currentColor">slicing-by-8</text>
  <g stroke="currentColor">
    <rect x="120" y="170" width="52" height="22" class="f-ok"/><rect x="176" y="170" width="52" height="22" class="f-ok"/>
    <rect x="232" y="170" width="52" height="22" class="f-ok"/><rect x="288" y="170" width="52" height="22" class="f-ok"/>
    <rect x="344" y="170" width="52" height="22" class="f-ok"/><rect x="400" y="170" width="52" height="22" class="f-ok"/>
    <rect x="456" y="170" width="52" height="22" class="f-ok"/><rect x="512" y="170" width="52" height="22" class="f-ok"/>
  </g>
  <g font-family="monospace" font-size="12">
    <text x="128" y="186" fill="currentColor">T7[.]</text><text x="184" y="186" fill="currentColor">T6[.]</text><text x="240" y="186" fill="currentColor">T5[.]</text><text x="296" y="186" fill="currentColor">T4[.]</text>
    <text x="352" y="186" fill="currentColor">T3[.]</text><text x="408" y="186" fill="currentColor">T2[.]</text><text x="464" y="186" fill="currentColor">T1[.]</text><text x="520" y="186" fill="currentColor">T0[.]</text>
  </g>
  <path d="M146 192 L340 222 M202 192 L340 222 M258 192 L340 222 M314 192 L340 222 M370 192 L340 222 M426 192 L340 222 M482 192 L340 222 M538 192 L340 222" stroke="currentColor" fill="none"/>
  <rect x="290" y="222" width="100" height="24" class="f-acc" stroke="currentColor"/>
  <text x="304" y="239" fill="currentColor">XOR 트리 → c</text>
  <text x="575" y="186" fill="currentColor">8개 조회는 서로 독립</text>
  <text x="575" y="202" fill="currentColor">→ 동시에 발행</text>
  <text x="10" y="282" fill="currentColor">c 에 걸리는 사슬: 비트 단위 = 8단계/바이트, 테이블 = 조회 1번/바이트, slicing-by-8 = 조회 1번 + XOR 3층 / 8바이트.</text>
  <text x="10" y="304" fill="currentColor">대가는 메모리: 0 B → 1 KiB → 8 KiB. 비순차(out-of-order) 코어일수록 독립 조회의 이득이 크다 (5. 풀이 실측).</text>
</svg>
```

### Part A — 비트 단위 기준과 256엔트리 테이블

1. `crc32_bitwise(crc, p, n)` — 기준 구현. 바이트마다 `c ^= b` 후 8번 `c = (c >> 1) ^ (c & 1 ? 0xEDB88320 : 0)`.
2. `crc32_make_table(t)` — **테이블 생성기**: `t[i]` = 하위 바이트가 i 인 레지스터를 8비트 민 결과. zlib `get_crc_table()` 256개와 전부 같아야 한다.
3. `crc32_table(crc, p, n)` — Sarwate 바이트 테이블: `c = T[(c ^ b) & 0xFF] ^ (c >> 8)`.

### Part B — slicing-by-4/8, 그리고 combine

1. `crc32_make_slice_tables(t)` — `T[k][i]` = "바이트 i 를 넣고 0 바이트 k 개를 더 넣은 효과" (k = 1..7). 재귀식 `T[k][i] = (T[k-1][i] >> 8) ^ T[0][T[k-1][i] & 0xFF]`.
2. `crc32_slice4`, `crc32_slice8` — 4/8바이트를 레지스터에 한 번에 XOR 하고, 바이트별로 다른 테이블을 조회해 XOR. 꼬리(n mod 8)는 바이트 테이블. **정렬되지 않은 포인터, 길이 0~7, 빅 엔디언 호스트**에서도 맞아야 한다.
3. `gf2_mulmod`, `x_pow_8n`, `crc32_combine_m(crc1, crc2, len2)` — 두 조각의 CRC 만으로 이어 붙인 전체의 CRC 를 O(log len2) 번의 다항식 곱셈으로. zlib `crc32_combine()` 과 대조.

### Part C — 부트로더 이미지와 DMA 블록

1. `fw_image_seal(img, len)` — 이미지 뒤에 CRC 를 **리틀 엔디언**으로 4바이트 붙인다.
2. `fw_image_ok(img, n)` — 붙은 CRC 를 따로 꺼내 비교하지 않고, **전체의 CRC 가 상수 0x2144DF1C 인지**만 본다(잔여값). 이유를 증명한다.
3. `crc32_parallel(p, n, parts)` — 이미지를 parts 개 블록으로 나눠 따로 계산(DMA 채널·코어마다)하고 combine 으로 합친다.

처리량·테이블 크기·크로스 타깃 asm 은 `demo_bench.c`, `demo_gentable.c`, `demo_m4.c` 에 있다 (5. 풀이 뒷부분).

## 2. Problem (English)

Implement CRC-32 (the zlib/Ethernet one: reflected, poly 0x04C11DB7, init and xorout 0xFFFFFFFF) at increasing speed, with a zlib-compatible streaming API:

```c
uint32_t crc32_bitwise(uint32_t crc, const uint8_t *p, size_t n);   /* reference, 8 shifts per byte      */
void     crc32_make_table(uint32_t t[256]);                          /* table generator                   */
uint32_t crc32_table (uint32_t crc, const uint8_t *p, size_t n);     /* one lookup per byte (Sarwate)     */
void     crc32_make_slice_tables(uint32_t t[8][256]);                /* t[0] given; fill t[1..7]          */
uint32_t crc32_slice4(uint32_t crc, const uint8_t *p, size_t n);     /* 4 bytes per iteration             */
uint32_t crc32_slice8(uint32_t crc, const uint8_t *p, size_t n);     /* 8 bytes per iteration             */
uint32_t crc32_combine_m(uint32_t crc1, uint32_t crc2, uint64_t len2); /* crc(A||B) from crc(A), crc(B), |B| */
bool     fw_image_ok(const uint8_t *img, size_t n);                  /* payload + little-endian CRC32     */
uint32_t crc32_parallel(const uint8_t *p, size_t n, unsigned parts); /* per-block CRCs merged by combine  */
```

Constraints: no malloc (tables are static arrays), `p` may be unaligned, `n` may be 0, must be correct on any host endianness, `crc = f(f(0, A), B)` must equal `f(0, A||B)`.

Examples:

```text
crc32_*(0, "123456789", 9)                          = 0xCBF43926
crc32_*(0, "", 0)                                   = 0x00000000
crc32_*(0, "a", 1)                                  = 0xE8B7BE43
crc32_*(0, "The quick brown fox jumps over the lazy dog", 43) = 0x414FA339
table[1] = 0x77073096, table[128] = 0xEDB88320, table[255] = 0x2D02EF8D
crc32_combine_m(crc("12345"), crc("6789"), 4)       = 0xCBF43926
crc32 of ("123456789" + 26 39 F4 CB)                = 0x2144DF1C   (residue)
```

Follow-ups: Why is the table correct? What does slicing-by-8 cost in memory and what does it buy on an in-order Cortex-M4 vs an out-of-order core? How would you verify a 1 MiB image received in 256-byte UART chunks? Two DMA engines each CRC half the image — how do you get the CRC of the whole?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -lz -o starter && ./starter
```

`-lz` 는 **테스트 쪽**(zlib `crc32()` / `crc32_combine()` / `get_crc_table()` 대조)에만 필요하다. 채울 TODO 는 열두 군데(Part A 3, Part B 6, Part C 3). 스텁은 0 이나 입력 그대로를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, FAIL 줄 대부분 생략):

```text
FAIL starter.c:131  crc32_bitwise(0, chk, 9) == 0xCBF43926u
…
table vs zlib get_crc_table(): 255/256 entries differ
slice tables T[0..7] vs bit-by-bit definition: 2040/2048 entries differ
all 1- and 2-byte messages: bitwise/table vs zlib mismatches=131328
len 0..72 x offset 0..7 (584 cases): mismatches table=576 slice4=576 slice8=576
random 3000 msgs (len 0..4096, offset 0..7) vs zlib crc32(): bitwise=3000 table=3000 slice4=3000 slice8=3000
streaming in random chunks (1..300 B), 500 msgs: mismatches=1500
combine: 2000 random splits vs one-shot and zlib crc32_combine(): mismatches=3998
fw image: 1000 sealed images ok-fail=1000, single-bit flips missed=0
parallel blocks (parts 0,1,2,3,4,7,8,16) x 200 lengths: mismatches=1592
6/33 passed
```

(exit 1. 통과한 6개는 스텁이 우연히 맞는 경우 — 빈 입력의 CRC = 0, 0 바이트 combine, "검출 실패 0" 등.)

## 4. 힌트

<details><summary>힌트 1 — 바이트를 먼저 XOR 해도 된다</summary>

비트 단위 루프에서 바이트 b 의 8비트는 c 의 하위 8비트에 하나씩 XOR 되는데, 레지스터를 미는 순서와 b 를 미는 순서가 같다. 그래서 바이트 시작에 `c ^= b` 를 한 번 하고, 그다음 8번은 "입력 0" 으로 밀어도 같다. 이제 8번 미는 동작은 c 만의 함수다.

</details>

<details><summary>힌트 2 — 8번 미는 동안 c 의 상위 24비트는 무슨 일을 하나</summary>

k 번째 스텝은 현재 비트 0 을 보고 poly 를 XOR 할지 정한다. 그 비트는 원래 c 의 비트 k (k < 8)다. 즉 **XOR 여부는 하위 바이트만 결정**한다. 상위 24비트는 그냥 8칸 오른쪽으로 간다(`c >> 8`). XOR 는 선형이니 "하위 바이트만 있을 때의 결과"를 미리 표로 → `T[c & 0xFF] ^ (c >> 8)`.

</details>

<details><summary>힌트 3 — 4바이트를 한꺼번에</summary>

힌트 1 을 4바이트로: 레지스터에 LE 워드 w 를 한 번에 XOR 하면, 그 뒤는 "0 바이트 4개" 로 미는 것과 같다. 0 바이트로 미는 동작 G 는 선형 → c 의 바이트 4개 각각의 기여를 XOR. 바이트 0 은 4스텝 전부, 바이트 3 은 1스텝만 겪는다. 그래서 바이트 0 은 `T[3]`, 바이트 3 은 `T[0]`.

</details>

<details><summary>힌트 4 — combine 은 "0 바이트 len2 개를 덧붙인 효과" 하나면 된다</summary>

0 바이트 하나로 미는 것 = 레지스터 다항식에 x^8 을 곱하고 mod P. 그러면 A 뒤에 B 를 이어 처리한 레지스터 = (A 뒤 레지스터)·x^(8|B|) ⊕ (B 의 기여). init 와 xorout 이 둘 다 ~0 이면 상수가 정리되어 `crc(A||B) = crc(A)·x^(8|B|) ⊕ crc(B)`. x^(8n) 은 제곱-곱셈.

</details>

<details><summary>힌트 5 — 잔여값: CRC 를 붙이면 레지스터가 어떻게 되나</summary>

메시지 뒤 레지스터를 R 이라 하면 붙는 CRC 는 ~R. 이것을 LE 4바이트로 처리하면(힌트 3) 레지스터는 G(R ^ ~R) = G(0xFFFFFFFF) — 메시지와 무관한 상수다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 어느 CRC-32 인가(zlib/Ethernet 의 ISO-HDLC? iSCSI 의 32C?), 파라미터 5개, 결과를 어떤 바이트 순서로 붙이나, 버퍼 정렬 보장 여부, 타깃(M0/M4/A-class), 플래시/RAM 예산, 하드웨어 CRC 유닛 유무 |
| 2. 단순 해법 | 비트 단위: 바이트당 시프트 8번. 시간 O(8n), 메모리 0. 검증용 기준으로 남겨 둔다 |
| 3. 개선 + 불변식 | 불변식: "루프 시작마다 c = 지금까지 처리한 접두부의 (조건화된) CRC 레지스터". 바이트 테이블은 레지스터 하위 바이트만 XOR 여부를 정한다는 사실로, slicing 은 0 바이트 스텝의 선형성으로 정당화 |
| 4. 코드 | 아래 Part A/B/C |
| 5. 소리 내어 말할 테스트 | check 0xCBF43926, 빈 입력 = 0, 길이 1~7(꼬리만), 8의 배수 ± 1, 홀수 주소 시작, 전부 0x00/0xFF, 청크 분할 = 한 번에, zlib 대조 |
| 6. 복잡도 | 시간 O(n) 모두 같고 상수가 다르다. 메모리 0 / 1 KiB / 4 KiB / 8 KiB, 스택 O(1), 재귀 없음. combine 은 O(log len) 번의 32×32 GF(2) 곱 |

### Part A — 비트 단위 기준

반사형이라 레지스터의 비트 31 쪽이 x^0, 비트 0 쪽이 x^31 이다. `c >> 1` 은 모든 항에 x 를 곱하는 것이고, 밀려 나가는 비트 0(x^31 항)이 1 이면 x^32 가 생기므로 x^32 ≡ (P − x^32) = 반사 표현 0xEDB88320 을 XOR 한다. 즉 **입력 0 인 한 비트 스텝 S(c) = c·x mod P** 이고, 이것은 GF(2) 위의 선형 사상이다 — 이 레슨의 모든 증명이 이 한 줄에서 나온다.

```c
uint32_t c = ~crc;                                     /* zlib 규약: 이전 결과를 받아 다시 조건화 */
while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
}
return ~c;
```

`0u - (c & 1u)` 는 c 가 홀수면 0xFFFFFFFF, 짝수면 0 — 분기 없는 마스크다(부호 없는 뺄셈의 넘침은 C 에서 정의된 mod 2^32 동작). init 과 xorout 이 같은 ~0 이라 `~crc` 로 시작해 `~c` 로 끝내면, 결과를 다시 넣어 이어 부를 때 조건화가 정확히 상쇄된다 → 스트리밍이 공짜다.

### Part A — 바이트 테이블이 맞는 이유 (증명)

**보조정리 1.** 바이트 스텝 B(c, b) = S^8(c ⊕ b) 이고, S^8(c) = T[c & 0xFF] ⊕ (c >> 8), 단 T[i] = S^8(i).

증명. 비트 단위 루프는 바이트 시작에 `c ^= b` 를 하고 S 를 8번 하므로 첫 등식은 정의다. S 는 선형이므로 c = lo ⊕ hi (lo = 하위 8비트, hi = 나머지)로 쪼개 S^8(c) = S^8(lo) ⊕ S^8(hi). S 의 k 번째 적용(k = 0..7)에서 검사되는 비트 0 은 원래 레지스터의 비트 k 다. hi 는 비트 0~7 이 모두 0 이므로 8번 동안 poly XOR 이 한 번도 일어나지 않고 S^8(hi) = hi >> 8 = c >> 8. lo 쪽은 정의상 T[lo]. ∎

그래서 테이블 생성기는 **i = 0..255 각각에 비트 스텝 8번** — 256 × 8 = 2048 스텝이면 끝난다. T[0x80] 은 7번 밀린 뒤 비트 0 이 1 이 되어 마지막에 poly 를 한 번 XOR → T[128] = 0xEDB88320 (테스트가 확인한다). 생성 과정을 i = 0xCE("1" 의 첫 인덱스)로 따라가면, 밀기 전 비트 0 이 0,1,1,1,0,0,1,0 이고 c 는 0x67 → 0xEDB88313 → 0x9B64C2A9 → 0xA00AE274 → 0x5005713A → 0x2802B89D → 0xF9B9DF6E → **0x7CDCEFB7** = T[0xCE].

**트레이스: "123456789", 바이트 테이블** (c 는 조건화된 레지스터, 시작 ~0):

| 바이트 | c (전) | idx = (c^b)&FF | T[idx] | c &gt;&gt; 8 | c (후) |
|---|---|---|---|---|---|
| '1' 0x31 | FFFFFFFF | CE | 7CDCEFB7 | 00FFFFFF | 7C231048 |
| '2' 0x32 | 7C231048 | 7A | B0D09822 | 007C2310 | B0ACBB32 |
| '3' 0x33 | B0ACBB32 | 01 | 77073096 | 00B0ACBB | 77B79C2D |
| '4' 0x34 | 77B79C2D | 19 | 646BA8C0 | 0077B79C | 641C1F5C |
| '5' 0x35 | 641C1F5C | 69 | 346ED9FC | 00641C1F | 340AC5E3 |
| '6' 0x36 | 340AC5E3 | D5 | F6B9265B | 00340AC5 | F68D2C9E |
| '7' 0x37 | F68D2C9E | A9 | AF0A1B4C | 00F68D2C | AFFC9660 |
| '8' 0x38 | AFFC9660 | 58 | 65B0D9C6 | 00AFFC96 | 651F2550 |
| '9' 0x39 | 651F2550 | 69 | 346ED9FC | 00651F25 | 340BC6D9 |

마지막 ~0x340BC6D9 = **0xCBF43926**. (참고로 xorout 을 빼먹은 0x340BC6D9 는 카탈로그의 CRC-32/JAMCRC check 값과 같다 — 7. 실수 2.)

### Part B — slicing-by-4/8 이 맞는 이유 (증명)

**보조정리 2 (워드 단위로 먼저 XOR).** 0 바이트 스텝 Z(c) = S^8(c) 라 하고, G = Z^4 (0 바이트 4개). 바이트 b0..b3 을 차례로 처리한 결과는 G(c ⊕ w), w = b0 | b1&lt;&lt;8 | b2&lt;&lt;16 | b3&lt;&lt;24 (리틀 엔디언 워드).

증명. 귀납. x = c ⊕ w 로 두면 Z(x) = T[x & FF] ⊕ (x >> 8) = T[(c ⊕ b0) & FF] ⊕ (c >> 8) ⊕ (w >> 8) = B(c, b0) ⊕ (w >> 8). 즉 "한 스텝 뒤의 x" 는 "한 스텝 뒤의 c" 에 남은 바이트 b1..b3 를 같은 자리(하위부터)에 XOR 한 것이다. 이 모양이 유지되므로 4스텝 뒤 G(c ⊕ w) = 4바이트 처리 결과. ∎

**보조정리 3 (바이트별 분해).** G(x) = T₃[x₀] ⊕ T₂[x₁] ⊕ T₁[x₂] ⊕ T₀[x₃] (x_j = x 의 j 번째 바이트, T_k[i] = 바이트 i 뒤에 0 바이트 k 개).

증명. G 는 선형이라 G(x) = ⊕_j G(x_j &lt;&lt; 8j). 바이트 j 에만 값이 있는 레지스터는 처음 j 번의 Z 에서 하위 바이트가 0 이므로 T[0] = 0 이 더해지고 그냥 8칸씩 내려온다. j 스텝 뒤 하위 바이트에 x_j 가 오고, 그 스텝에서 T[x_j], 남은 3 − j 스텝은 0 바이트 스텝 → T_{3−j}[x_j]. ∎ 생성 재귀 T_k[i] = Z(T_{k−1}[i]) = T[T_{k−1}[i] & FF] ⊕ (T_{k−1}[i] >> 8) 은 "0 바이트 하나 더" 그대로다. slicing-by-8 은 같은 논리로 8바이트: lo = c ⊕ (바이트 0~3), hi = 바이트 4~7 (hi 는 c 와 섞지 않는다 — c 는 32비트라 앞 4바이트에만 겹친다), 바이트 j 는 T_{7−j}.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">slicing-by-4 한 번: c = FFFFFFFF, 입력 "1234" (31 32 33 34)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="220" height="28" class="f-mut"/><rect x="20" y="78" width="220" height="28" class="f-mut"/><rect x="20" y="124" width="220" height="28" class="f-acc"/>
  </g>
  <g font-family="monospace">
    <text x="32" y="59" fill="currentColor">c        = FFFFFFFF</text><text x="32" y="97" fill="currentColor">le32(p)  = 34333231</text><text x="32" y="143" fill="currentColor">x = c ^ w = CBCCCDCE</text>
  </g>
  <line x1="20" y1="114" x2="240" y2="114" stroke="currentColor"/>
  <g stroke="currentColor">
    <rect x="300" y="40" width="200" height="26" class="f-ok"/><rect x="300" y="76" width="200" height="26" class="f-ok"/>
    <rect x="300" y="112" width="200" height="26" class="f-ok"/><rect x="300" y="148" width="200" height="26" class="f-ok"/>
  </g>
  <g font-family="monospace">
    <text x="310" y="58" fill="currentColor">T3[CE] = 96929935</text><text x="310" y="94" fill="currentColor">T2[CD] = 99770513</text><text x="310" y="130" fill="currentColor">T1[CC] = 674F9842</text>
    <text x="310" y="166" fill="currentColor">T0[CB] = 0CB61B38</text>
  </g>
  <path d="M240 138 L298 53 M240 138 L298 89 M240 138 L298 125 M240 138 L298 161" stroke="currentColor" fill="none"/>
  <text x="515" y="58" fill="currentColor">바이트 0: 4스텝 겪음</text>
  <text x="515" y="94" fill="currentColor">바이트 1: 3스텝</text>
  <text x="515" y="130" fill="currentColor">바이트 2: 2스텝</text>
  <text x="515" y="166" fill="currentColor">바이트 3: 1스텝</text>
  <rect x="300" y="196" width="200" height="28" class="f-acc" stroke="currentColor"/>
  <text x="310" y="215" fill="currentColor" font-family="monospace">XOR = 641C1F5C</text>
  <path d="M400 174 L400 194" stroke="currentColor"/>
  <text x="20" y="250" fill="currentColor">바이트 테이블 트레이스의 '4' 행 결과도 641C1F5C — 같은 값, 직렬 조회 4번 대신 독립 조회 4번.</text>
  <text x="20" y="272" fill="currentColor">x 의 바이트를 낮은 쪽부터 꺼내므로 le32 는 반드시 리틀 엔디언으로 조립한다 (호스트 엔디언과 무관하게).</text>
  <text x="20" y="292" fill="currentColor">slicing-by-8 의 "12345678" 결과 651F2550 도 위 표 '8' 행과 같다 (계산 확인).</text>
</svg>
```

**엔디언과 정렬.** `le32()` 는 바이트 네 개를 시프트로 조립한다. 정렬 요구도, strict aliasing 문제도, 호스트 엔디언 의존도 없다. arm64 와 Cortex-M4 의 clang -O2 는 이것을 `ldr` 하나로 합친다(M4 asm 에서 확인: `ldr r4, [r1]`). M4 는 비정렬 LDR 을 하드웨어가 처리하지만(M0 은 HardFault), 컴파일러가 합칠지 말지를 타깃별로 알아서 정한다 — M0 asm 에서는 바이트 로드 4개(`ldrb`)로 남았다. `*(const uint32_t *)p` 로 읽으면 정렬 안 된 p 에서 C 표준상 UB 이고 M0 에선 실제로 fault 다 (7. 실수 4). zlib 같은 라이브러리는 빅 엔디언 호스트용으로 바이트 스왑한 테이블을 따로 둔다; 이 구현은 조립을 LE 로 고정해서 같은 테이블로 끝난다.

**왜 빨라지나.** 바이트 테이블에서 다음 조회 인덱스는 방금 조회한 값에 의존한다 — 로드 지연(L1 수 사이클)이 바이트마다 직렬로 쌓인다. slicing-by-8 은 8개 조회의 인덱스가 모두 이미 아는 lo/hi 에서 나오므로 동시에 발행되고, c 에 걸리는 사슬은 8바이트에 "조회 1번 + XOR 몇 층" 뿐이다. 실측(아래)에서 이 맥은 table 587 → slice4 1524 → slice8 2645 MB/s (64 KiB). 4.5배지 8배가 아닌 이유: 이제 병목이 사슬이 아니라 8바이트당 로드 10개 + ALU 20여 개의 **처리량**이 된다.

### Part B — 스트리밍과 combine (GF(2) 다항식 곱셈)

**스트리밍**은 보조정리 없이도 자명하다: 함수가 `~crc` 로 들어가 `~c` 로 나오므로, 결과를 다음 호출에 넘기면 레지스터가 끊김 없이 이어진다. 테스트는 1~300 바이트 무작위 청크 500개 메시지로 확인한다(UART DMA 반 버퍼 콜백마다 부르는 모양).

**combine**: A 와 B 를 서로 다른 곳에서 계산했고, 이어 붙인 A‖B 의 CRC 가 필요하다. 0 바이트 스텝 Z 는 c·x^8 mod P (S 를 8번), 그리고 B 를 처리하는 동작은 "Z^m(시작 레지스터) ⊕ L(B)" (m = |B|, L 은 B 에만 의존하는 선형 항). 그러면

- crc2 = ~( Z^m(~0) ⊕ L(B) ) — B 만 처리한 것
- A‖B 의 레지스터 = Z^m(~crc1) ⊕ L(B) = Z^m(crc1) ⊕ Z^m(~0) ⊕ L(B) = Z^m(crc1) ⊕ ~crc2
- 따라서 **crc(A‖B) = crc1·x^(8m) mod P ⊕ crc2**.

init = xorout 이어서 ~0 항이 깨끗이 상쇄된 것이다(다른 CRC 면 보정 상수가 붙는다). 남은 일은 GF(2)[x]/P 에서의 곱셈 두 가지:

```c
static uint32_t gf2_mulmod(uint32_t a, uint32_t b) {      /* 반사 표현: 비트 31 = x^0 */
    uint32_t r = 0;
    for (uint32_t m = 1u << 31; m; m >>= 1) {             /* a 의 x^0, x^1, ... 항 */
        if (a & m) r ^= b;                                /* r += b·x^j */
        b = (b >> 1) ^ (0xEDB88320u & (0u - (b & 1u)));   /* b ← b·x  (= 비트 스텝 S) */
    }
    return r;
}
/* x^(8n): r = 1 (비트 31), sq = x^8 (비트 23), 제곱-곱셈 */
```

`b ← b·x` 가 비트 단위 CRC 스텝과 **완전히 같은 식**이라는 점이 핵심이다 — CRC 레지스터가 곧 GF(2)[x]/P 의 원소다. x^(8n) 은 n 의 비트마다 제곱 1번(+ 곱 1번)이라 gf2_mulmod 최대 2·⌈log₂(n+1)⌉ 번, 각 32회 반복. 테스트는 n = 0..600 에서 "x 를 8n 번 곱하기"와 대조하고, 2000개 무작위 분할에서 zlib `crc32_combine()` 및 한 번에 계산한 값과 대조한다. (zlib 1.2.12 이후의 `crc32_combine` 도 같은 multmodp/x2nmodp 방식이다 — 그 전 버전은 32×32 GF(2) 행렬 제곱을 썼다. 버전별 세부는 확인 권장.)

**불변식 정리.** 세 구현 모두 "루프 i 번째 반복 시작에 c = 접두부 p[0..i) 의 조건화된 레지스터" 를 유지한다. 바이트 테이블은 보조정리 1, slicing 은 보조정리 2·3 이 한 반복이 이 불변식을 보존함을 보이고, 꼬리 루프는 바이트 테이블과 같다. 종료 시 ~c 가 CRC.

### Part C — 잔여값으로 이미지 검사

부트로더는 보통 "이미지 끝 4바이트 = CRC" 를 따로 읽어 비교한다. 더 깔끔한 방법: 끝 4바이트까지 **전부** CRC 해서 상수와 비교한다.

**정리.** 임의의 메시지 M 뒤에 crc32(M) 을 리틀 엔디언으로 붙이면 crc32(M ‖ crc) = 0x2144DF1C (M 과 무관).

증명. M 뒤의 레지스터를 R 이라 하면 crc32(M) = ~R. 이것을 LE 바이트 4개로 처리한 레지스터는 보조정리 2 에 의해 G(R ⊕ ~R) = G(0xFFFFFFFF) = 0xFFFFFFFF·x^32 mod P. 계산하면 0xDEBB20E3 — RevEng 카탈로그의 residue 필드와 같다. 출력 조건화(~)를 거치면 **0x2144DF1C**. ∎

테스트는 이 상수가 1000개 무작위 이미지(길이 0~3999)에서 모두 나오는지, 그리고 아무 비트 하나를 뒤집으면 반드시 다른 값이 되는지(CRC 는 모든 1비트 오류를 잡는다 — ALGO-M01) 본다. 장점: 이미지 길이가 4의 배수가 아니어도, CRC 필드 위치를 파싱하지 않아도, 스트리밍으로 받는 그대로 마지막에 한 번 비교. **조건: LE 로 붙여야 한다.** BE 로 붙이면 보조정리 2 의 w 가 ~R 이 아니게 되어 상수가 깨진다 — Python `zlib.crc32(b"123456789" + crc.to_bytes(4, "big"))` = 0x3CC9742C (이 레슨에서 실행). Ethernet FCS 도 같은 원리로 수신기가 잔여값을 본다 (매직 넘버 0xC704DD7B 는 비트 반전 표현 — 확인 권장).

### Part C — DMA 블록 병렬 CRC

`crc32_parallel` 은 이미지를 parts 개 연속 블록으로 나눠 각자 `crc32_slice8(0, …)` 하고, 왼쪽부터 `acc = combine(acc, c_k, len_k)` 로 합친다. 블록 계산은 서로 독립이라 두 코어(예: 듀얼 코어 MCU), 여러 DMA 채널 + 하드웨어 CRC, 또는 한 코어의 여러 레지스터 스트림으로 병렬화된다. 합치는 비용은 블록마다 O(log len) 곱셈이라 블록이 크면 무시할 만하다. 경계: parts = 0 은 1 로, parts > n 이면 빈 블록 없이 n 개 이하로 (블록 크기 ⌈n/parts⌉ 로 자르므로). 테스트는 parts ∈ {0,1,2,3,4,7,8,16} × 길이 200개(0~7 포함)를 zlib 한 번 계산과 대조.

이 맥의 zlib 이 46 GB/s 인 이유도 아마 이것이다(아래 표): `__crc32d` 한 줄 사슬이 12 GB/s 인데 그 약 4배 → 여러 스트림을 따로 돌려 합치는(또는 PMULL folding) 하드웨어 경로로 보인다. Apple libz 의 내부 구현은 공개 문서로 확인하지 못했다 — 확인 권장.

### 테이블 생성기와 플래시 예산 — `demo_gentable.c`

펌웨어에선 테이블을 런타임에 RAM 에 만들지(2048 스텝, 1 KiB RAM) 빌드 때 만들어 `const` 로 플래시에 둘지(1 KiB 플래시, RAM 0) 고른다. 생성기는 C 소스를 출력한다:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_gentable.c -o demo_gentable
./demo_gentable > crc32_tab.c && ./demo_gentable 4 > crc32_tab4.c && ./demo_gentable 8 > crc32_tab8.c   # (이 레슨에서는 /tmp 에 생성)
for f in crc32_tab crc32_tab4 crc32_tab8; do
  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c $f.c -o $f.o; done
llvm-size -A crc32_tab.o crc32_tab4.o crc32_tab8.o | grep -E 'rodata|:'
```

```text
/* generated by demo_gentable.c — CRC-32/ISO-HDLC, reflected poly 0xEDB88320 */
#include <stdint.h>
const uint32_t crc32_tab[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
…
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};
```

```text
crc32_tab.o  :
.rodata             1024      0
crc32_tab4.o  :
.rodata             4096      0
crc32_tab8.o  :
.rodata             8192      0
```

(위: 생성된 crc32_tab.c 의 처음 5줄과 마지막 3줄. 아래: llvm-size 의 .rodata 줄 — 코드 0, 순수 상수 테이블.) 32 KiB 플래시 M0 에 8 KiB 는 큰돈이다. STM32 같은 칩에서 플래시는 대기 상태(wait state)가 있어 테이블 조회가 SRAM 보다 느릴 수 있고, 캐시/가속기(ART 등)가 이를 가린다 — 칩별로 다르니 측정이 답이다(확인 권장).

### 크로스 타깃 — 같은 루프의 명령 수 (`demo_m4.c`)

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o m4.s
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_m4.c -o m0.s
```

Cortex-M4, 바이트 테이블 안쪽 루프(컴파일러가 4번 펼침) 중 한 바이트분 — 실제 출력:

```text
	ldrb	r4, [r1, #1]
	eor.w	r0, r2, r0, lsr #8
	eor.w	r2, r4, r0
	uxtb	r2, r2
	ldr.w	r2, [r12, r2, lsl #2]
```

시프트가 `eor.w … lsr #8` 에, 인덱스 ×4 가 주소 모드 `lsl #2` 에 접혀 들어가 바이트당 5명령이다. 안쪽 루프(내부 루프 레이블부터 되돌아가는 분기까지) 정적 명령 수를 세면:

| 구현 | Cortex-M4 | 바이트당 | Cortex-M0 | 바이트당 |
|---|---|---|---|---|
| 비트 단위 | 60 명령 / 2 B | 30 | 98 명령 / 2 B | 49 |
| 바이트 테이블 | 22 명령 / 4 B | 5.5 | 31 명령 / 4 B | 7.75 |
| slicing-by-8 | 108 명령 / 32 B (`ldr` 40개) | 3.4 | 126 명령 / 16 B (스필 7 + 리로드 7) | 7.9 |

M4 에서 slicing-by-8 은 데이터를 `ldr` 로 4바이트씩 읽고 바이트 추출을 `ubfx` 하나로 해서 명령 수가 테이블의 약 60% 다. M0 은 비정렬 `ldr` 이 없어 `ldrb` 4개 + 시프트 + 덧셈으로 조립하고, 레지스터 8개(r0~r7)로 8개 테이블 기준 주소를 못 들고 있어 **스택 스필**이 생긴다 → 테이블 방식보다 오히려 많다. 결론: **M0 에선 바이트 테이블(또는 니블 테이블)까지, slicing 은 M4/M7 이상**. (사이클 수는 칩의 플래시 대기·버스에 달려 있어 여기서 주장하지 않는다 — 명령 수는 정적 개수이고 실제 사이클은 측정 필요.)

### 처리량 실측 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -lz -o demo_bench && ./demo_bench     # 약 0.75 초
```

이 맥(`sysctl machdep.cpu.brand_string` = Apple M6, Apple clang 21.0.0, -O2), 크기마다 약 2 MiB 를 한 표본으로 워밍업 후 9 표본 중앙값. 실제 출력:

```text
zlib 1.2.12, all 8 implementations agree with zlib crc32() on len 0..300
implementation             table B  64 B MB/s 1 KiB MB/s 64 KiB MB/s
bitwise (8 steps/byte)           0        213        190        188
nibble table (16 ent)           64        444        323        314
byte table (Sarwate)          1024        908        600        587
slicing-by-4                  4096       2834       1564       1524
slicing-by-8                  8192       5519       2727       2645
zlib crc32()                     0       6223      29537      46603
ARMv8 __crc32b                   0       1896       1507       1454
ARMv8 __crc32d                   0      26214      13530      11782
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">64 KiB 처리량 (MB/s, 로그 눈금) — 위 실측값 그대로</text>
  <g stroke="currentColor" stroke-dasharray="2 3">
    <line x1="200" y1="32" x2="200" y2="250"/><line x1="350" y1="32" x2="350" y2="250"/>
    <line x1="500" y1="32" x2="500" y2="250"/><line x1="650" y1="32" x2="650" y2="250"/>
  </g>
  <g font-size="12"><text x="190" y="268" fill="currentColor">100</text><text x="335" y="268" fill="currentColor">1000</text>
  <text x="480" y="268" fill="currentColor">10000</text><text x="625" y="268" fill="currentColor">100000</text></g>
  <g stroke="currentColor">
    <rect x="200" y="38" width="41" height="18" class="f-mut"/><rect x="200" y="64" width="75" height="18" class="f-mut"/><rect x="200" y="90" width="115" height="18" class="f-acc"/>
    <rect x="200" y="116" width="177" height="18" class="f-acc"/><rect x="200" y="142" width="213" height="18" class="f-acc"/><rect x="200" y="168" width="174" height="18" class="f-warn"/>
    <rect x="200" y="194" width="311" height="18" class="f-warn"/><rect x="200" y="220" width="400" height="18" class="f-ok"/>
  </g>
  <g font-size="12">
    <text x="10" y="52" fill="currentColor">bitwise · 0 B</text><text x="248" y="52" fill="currentColor">188</text><text x="10" y="78" fill="currentColor">nibble · 64 B</text><text x="282" y="78" fill="currentColor">314</text>
    <text x="10" y="104" fill="currentColor">byte table · 1 KiB</text><text x="322" y="104" fill="currentColor">587</text>
    <text x="10" y="130" fill="currentColor">slicing-by-4 · 4 KiB</text><text x="384" y="130" fill="currentColor">1524</text>
    <text x="10" y="156" fill="currentColor">slicing-by-8 · 8 KiB</text><text x="420" y="156" fill="currentColor">2645</text>
    <text x="10" y="182" fill="currentColor">__crc32b (HW, 1 B)</text><text x="381" y="182" fill="currentColor">1454</text>
    <text x="10" y="208" fill="currentColor">__crc32d (HW, 8 B)</text><text x="518" y="208" fill="currentColor">11782</text>
    <text x="10" y="234" fill="currentColor">zlib crc32()</text><text x="607" y="234" fill="currentColor">46603</text>
  </g>
  <text x="10" y="286" fill="currentColor" font-size="12">막대 길이 = 150·log10(MB/s ÷ 100) px. 두 번째 실행도 ±5% 안에서 같은 순서였다.</text>
</svg>
```

읽는 법:
- **비율**: 비트 → 테이블 3.1배, 테이블 → slice8 4.5배, 바이트 하나씩 넣는 하드웨어 `__crc32b`(1454)는 slicing-by-4 수준 — 명령 하나라도 **바이트마다 직렬**이라서다. 8바이트씩 `__crc32d` 는 8배 가까이 뛴다.
- **64 B 열이 더 빠른 이유**(해석): 짧은 버퍼에서는 연속한 호출끼리 의존이 없어(매번 ~0 에서 시작) 비순차 코어가 이전 호출의 꼬리와 다음 호출을 겹쳐 실행한다. 즉 64 B 열은 "여러 독립 패킷의 처리량", 64 KiB 열은 "한 사슬의 지연"에 가깝다. 반대로 zlib 은 64 B 에서 6 GB/s 로 떨어진다 — 큰 버퍼용 경로의 준비 비용으로 보인다. 짧은 UART/CAN 프레임이라면 단순한 구현이 손해가 아니다.
- 2회 실행: 1 KiB·64 KiB 열은 모든 행이 ±5% 안. 64 B 열은 흔들림이 커서(byte table 908 / 1135, `__crc32d` 26214 / 29127) 경향만 볼 것.

### 복잡도

| 함수 | 시간 | 테이블 / 추가 메모리 | 스택 | 비고 |
|---|---|---|---|---|
| crc32_bitwise | 8n 스텝 | 0 | O(1) | 기준·부트 ROM 용 |
| crc32_make_table | 256 × 8 스텝 | 1 KiB | O(1) | 빌드 때 하면 0 |
| crc32_table | n 조회 | 1 KiB | O(1) | M0 의 현실적 최선(또는 니블 64 B) |
| crc32_make_slice_tables | 7 × 256 조회 | +7 KiB | O(1) | |
| crc32_slice4 / slice8 | n/4·4 / n/8·8 조회 + 꼬리 ≤ 7 | 4 / 8 KiB | O(1) | 꼬리는 바이트 테이블 |
| gf2_mulmod | 32 반복 | 0 | O(1) | |
| crc32_combine_m | ≤ 2⌈log₂(len2+1)⌉ + 1 회 gf2_mulmod | 0 | O(1) | zlib 처럼 x^(2^k) 32개 표를 두면 제곱이 사라진다 |
| crc32_parallel | O(n) + parts × combine | 0 | O(1) | 블록 계산은 병렬 가능 |
| fw_image_ok | O(n) | 0 | O(1) | 길이·위치 파싱 없음 |

재귀 없음. 최악 = 평균 (데이터 의존 분기가 없다 — 비트 단위도 마스크로 분기 제거). 시간이 데이터와 무관하다는 것은 ISR 예산을 정할 때 장점이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -lz -o solution && ./solution      # 33/33 passed, 약 0.2 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -lz -o sol_san && ./sol_san   # 오류 없음
```

실제 출력:

```text
table vs zlib get_crc_table(): 0/256 entries differ
slice tables T[0..7] vs bit-by-bit definition: 0/2048 entries differ
all 1- and 2-byte messages: bitwise/table vs zlib mismatches=0
len 0..72 x offset 0..7 (584 cases): mismatches table=0 slice4=0 slice8=0
random 3000 msgs (len 0..4096, offset 0..7) vs zlib crc32(): bitwise=0 table=0 slice4=0 slice8=0
streaming in random chunks (1..300 B), 500 msgs: mismatches=0
combine: 2000 random splits vs one-shot and zlib crc32_combine(): mismatches=0
fw image: 1000 sealed images ok-fail=0, single-bit flips missed=0
parallel blocks (parts 0,1,2,3,4,7,8,16) x 200 lengths: mismatches=0
33/33 passed
```

## 7. 자주 하는 실수

**1. 반사형 테이블에 MSB-first 인덱스를 쓴다.** 책·블로그의 비반사(MSB-first) 코드 `c = (c << 8) ^ T[(c >> 24) ^ b]` 와 반사형 테이블을 섞으면 조용히 틀린다. "123456789" → 0xC40ED0B0 (이 레슨에서 실행; 정답 0xCBF43926). 규칙: 반사형 = `>> 8` + 하위 바이트 인덱스, 비반사 = `<< 8` + 상위 바이트 인덱스, 테이블도 각자 따로(연습 3).

**2. 조건화(init/xorout)를 빼먹거나 두 번 한다.** init 0·xorout 0 이면 "123456789" → 0x2DFD2D88, init ~0·xorout 0 이면 0x340BC6D9(= CRC-32/JAMCRC). 스트리밍에서는 반대 실수가 흔하다: 이전 결과를 `~` 해서 넘기면(`crc32(~a, …)`) 함수 안의 `~` 와 겹쳐 0x154F19C5 가 나온다 (정답 0xCBF43926, 실행 확인). 규약을 하나로 정할 것 — zlib 식(완성된 CRC 를 주고받는다)이면 호출자는 `~` 를 절대 안 한다.

**3. slicing 테이블 순서를 거꾸로 쓴다.** 첫 바이트(lo 의 하위)에 T[0] 을 쓰면 4바이트 이상인 무작위 메시지 999/999 개가 틀렸다(4바이트 미만은 꼬리 루프만 돌아 맞는다 — 짧은 테스트만 하면 못 잡는 이유). 보조정리 3: 첫 바이트가 가장 많은 스텝을 겪으므로 가장 큰 k.

**4. `*(const uint32_t *)p` 로 워드를 읽는다.** 정렬 안 된 p 에서 C11 6.3.2.3 상 UB, strict aliasing 위반 가능, Cortex-M0 에서는 비정렬 LDR 이 HardFault, 빅 엔디언 호스트에선 바이트 순서가 반대가 된다. `le32()` 처럼 바이트를 조립하거나 `memcpy` 로 읽으면 컴파일러가 가능한 타깃에서 `ldr` 하나로 만든다(M4 asm 확인). 테스트가 오프셋 0~7 을 전부 도는 이유.

**5. 청크별 CRC 를 XOR 해서 합친다.** `crc(A) ^ crc(B)` 는 crc(A‖B) 가 아니다 — 무작위 분할 1000/1000 개 틀렸다(실행). A 쪽에 x^(8|B|) 를 곱해야 한다(combine). 또 청크마다 `crc = 0` 으로 다시 시작해도 안 된다: 이전 결과를 넘겨 이어 부르거나, 따로 계산했으면 combine.

**6. CRC 를 빅 엔디언으로 붙이고 잔여값 검사를 한다.** "123456789" + BE CRC 의 CRC 는 0x3CC9742C — 상수가 아니다(Python 으로 실행). 잔여값 0x2144DF1C 는 LE 로 붙였을 때만. 프로토콜이 BE 를 요구하면 잔여값 대신 필드를 꺼내 비교한다.

**7. 테이블을 처음 쓰기 전에 안 만든다 / ISR 과 경쟁한다.** 런타임 생성이면 `main` 초반(인터럽트 켜기 전)에 한 번. "처음 호출 때 lazy 생성"은 ISR 과 메인 루프가 동시에 처음 부르면 반쯤 만들어진 테이블을 읽는다. 펌웨어에선 `const` 생성 테이블(플래시)이 가장 단순하고 안전하다. 그리고 zlib 의 `uLong`/`z_crc_t` 는 64비트 호스트(macOS)에서 `unsigned long` = 64비트다 — 32비트로 캐스트해서 비교할 것.

## 8. 면접 Q&A

<details><summary>Q1. 테이블 방식이 비트 단위와 같은 값을 내는 이유를 30초 안에 설명해 보세요.</summary>

CRC 레지스터를 밀 때 poly 를 XOR 할지 말지는 밀려 나가는 비트가 정하는데, 8번 미는 동안 밀려 나가는 비트는 전부 처음의 하위 8비트(입력 바이트를 XOR 한 뒤)다. 상위 24비트는 XOR 결정에 관여하지 않고 8칸 이동만 한다. 그리고 모든 연산이 XOR 라 선형이므로, 하위 바이트 256가지 경우의 "8번 민 결과" 를 미리 표로 만들고 상위 부분(`c >> 8`)에 XOR 하면 된다. 꼬리질문 "테이블은 왜 256 × 4 바이트?" → 인덱스가 8비트, 값이 32비트 레지스터. 니블 테이블은 16 × 4 = 64 B 로 조회 2번/바이트(연습 2).

</details>

<details><summary>Q2. slicing-by-8 은 왜 빠르고, 한계는? 꼬리: 16이나 32로 늘리면?</summary>

바이트 테이블은 조회 인덱스가 직전 조회 결과에 의존해 로드 지연이 바이트마다 직렬로 쌓인다. slicing-by-8 은 8개 인덱스가 이미 아는 값에서 나와 동시에 발행되고, 직렬 사슬은 8바이트당 한 번. 이 맥에서 587 → 2645 MB/s(4.5배). 한계: 테이블이 8 KiB 라 L1 을 차지하고(다른 코드와 경쟁), 로드 포트 처리량이 새 병목이 된다. slicing-by-16 은 16 KiB 로 더 커지고 이득이 줄어든다(Intel 의 slicing-by-8 논문 이후 zlib 등도 다른 길 — 여러 독립 스트림 "braiding" 이나 PMULL/PCLMULQDQ folding — 으로 갔다). M0 같은 in-order 소형 코어에선 병렬 발행 자체가 없어 이득이 명령 수 감소뿐이고, 레지스터가 부족해 그마저 사라진다(실측 asm 표).

</details>

<details><summary>Q3. 1 MiB 펌웨어 이미지를 UART 로 256 바이트씩 받습니다. RAM 은 4 KiB. CRC 검사 어떻게 하죠?</summary>

전체를 들고 있을 필요가 없다: 청크마다 `crc = crc32(crc, chunk, 256)` 으로 이어 계산하고(스트리밍), 청크는 바로 플래시에 쓴다. 마지막 청크까지 받은 뒤 이미지 끝 4바이트(LE CRC)까지 포함한 전체 CRC 가 0x2144DF1C 이면 OK(잔여값 — 길이·필드 위치 파싱 불필요). 꼬리질문: "플래시에 쓴 값이 맞는지는?" → 받은 데이터의 CRC 와 별개로, 다 쓴 뒤 **플래시에서 다시 읽어** CRC 를 계산해야 쓰기 오류를 잡는다(SSD 의 read-after-write 검증과 같은 생각). 메타데이터(길이, 버전, CRC)는 헤더에, 헤더 자체에도 CRC. 보안(서명)은 별개 — Q6.

</details>

<details><summary>Q4. 이미지 앞 절반과 뒤 절반을 두 DMA 엔진이 따로 CRC 했습니다. 전체 CRC 는?</summary>

crc(A‖B) = crc(A)·x^(8|B|) mod P ⊕ crc(B). CRC 레지스터는 GF(2)[x]/P 의 원소이고 0 바이트 하나 처리 = x^8 곱셈이라, |B| 바이트 뒤에 A 의 기여는 x^(8|B|) 이 곱해진 채 남는다. init 과 xorout 이 둘 다 ~0 이라 상수 항이 상쇄된다. x^(8n) 은 제곱-곱셈 O(log n), 곱셈은 32회 shift-xor. zlib 의 `crc32_combine()` 과 같은 결과(2000개 무작위 분할에서 확인). 꼬리: "XOR 만 하면 안 되나?" → 안 된다, 실측 1000/1000 틀림. "B 의 길이를 모르면?" → 못 한다, 길이가 필수 입력이다.

</details>

<details><summary>Q5. 하드웨어 CRC 가 있으면 소프트웨어 테이블은 필요 없나요? (ARMv8 CRC32 명령, STM32 CRC 유닛)</summary>

ARMv8 의 `CRC32B/H/W/X` 는 바로 이 다항식(반사 0xEDB88320)을, `CRC32CB/…` 는 Castagnoli 를 계산하고, **조건화(~)는 하지 않는다** — 이 레슨의 demo/practice 에서 `~__crc32b(~0, …)` 가 zlib 과 같음을 확인했다. 이 맥에서 8바이트씩 `__crc32d` 는 11.8 GB/s 로 slice8 의 4.5배. x86 SSE4.2 의 `crc32` 명령은 CRC-32C 전용이라 zlib CRC-32 에는 못 쓴다(그래서 iSCSI·ext4·SCTP 가 32C 를 고른 이유 중 하나). MCU 쪽: STM32F1/F4 같은 초기 계열의 CRC 유닛은 다항식 0x04C11DB7 고정, 32비트 워드 단위, init 0xFFFFFFFF, 반사·xorout 없음으로 알려져 있어 결과가 zlib CRC-32 와 다르고 CRC-32/MPEG-2 쪽이다(연습 3) — 반면 F0/F3/F7/L4/G4 등은 다항식·초기값·입출력 반사를 설정할 수 있다. 칩별 RM 확인 권장. 소프트웨어 테이블은 부트 ROM·다른 칩·호스트 툴(이미지 생성기)과 값이 맞는지 검증할 때 여전히 필요하다.

</details>

<details><summary>Q6. 공격자가 이미지를 바꾸면 CRC 가 막아 주나요?</summary>

아니다. CRC 는 선형이라 원하는 값으로 맞추기 쉽다 — 아무 메시지 뒤에 4바이트를 계산해 붙이면 CRC 를 원하는 값으로 만들 수 있다(연습 5: "123456789" + E5 E1 D0 CD → 0xDEADBEEF, Python zlib 로도 확인). 비트 몇 개를 바꾼 뒤 CRC 를 다시 맞추는 것도 O(1) 이다. CRC 는 **무작위 오류**(노이즈, 플래시 비트 플립) 검출용이고, 위·변조 방지는 HMAC 이나 서명(secure boot 의 ECDSA/RSA 검증)으로 한다. 둘 다 쓰는 게 보통이다: CRC 로 빠르게 전송 오류를 거르고, 서명으로 출처를 확인.

</details>

<details><summary>Q7. CRC-32 와 CRC-32C 중 무엇을 고르나요? 그리고 테이블은 다항식마다 다시 만들어야 하나요?</summary>

새 설계라면 32C 가 흔한 선택이다: Castagnoli(1993)가 찾은 다항식은 데이터 워드 길이 범위에서 해밍 거리 성질이 802.3 CRC-32 보다 좋고(구체 길이는 Koopman 의 표 확인 권장), x86(SSE4.2)과 ARMv8 모두 명령이 있다. 호환성이 필요하면(zip, PNG, Ethernet, 기존 부트로더) ISO-HDLC. 테이블은 다항식마다 다르다 — 생성기에 poly 만 바꾸면 된다(연습 1 은 0x82F63B78 로 같은 코드, check 0xE3069283). 반사 여부가 바뀌면 루프 모양(시프트 방향·인덱스)도 바뀐다(연습 3).

</details>

<details><summary>Q8. 8비트 AVR 이나 RAM 2 KiB 짜리 MCU 라면?</summary>

32비트 레지스터 연산 자체가 비싸다(8비트 레지스터 4개). 1 KiB 테이블은 플래시에 두되 AVR 같은 하버드 구조에선 플래시 읽기가 별도 명령(`pgm_read_dword` 류)이라 RAM 테이블과 코드가 다르다(확인 권장). 플래시가 부족하면 니블 테이블(64 B, 연습 2). 더 작게는 CRC-16/CRC-8 로 내리는 것도 설계 선택이다 — 프레임이 짧으면 16비트로도 충분한 검출력이 나온다(ALGO-M01, COMM-B08). 하드웨어 CRC 유닛이 있으면 DMA 로 먹이는 게 CPU 시간 0 에 가장 가깝다.

</details>

## 9. 추가 연습

### 연습 1 — CRC-32C (Castagnoli) 바이트 테이블

반사 다항식 0x82F63B78(Castagnoli 0x1EDC6F41 의 반사)로 같은 테이블 CRC 를 만들고 check 값 0xE3069283(RevEng CRC-32/ISCSI)을 확인하라. 이 맥의 ARMv8 `__crc32cb` 와도 대조하라.

<details><summary>답 보기</summary>

생성기와 루프는 Part A 와 똑같고 poly 상수만 다르다. 확인값: TC[1] = 0xF26B8303. 하드웨어 명령은 조건화가 없으므로 `h = 0xFFFFFFFF; for … h = __crc32cb(h, b); crc = ~h`. 무작위 1000개(길이 0~599)에서 테이블 = 비트 단위 = 하드웨어 명령 (`__ARM_FEATURE_CRC32` 가 정의된 이 맥에서 1000개 모두 검사됨). `<arm_acle.h>` 는 매크로가 있을 때만 include 해서 다른 호스트에서도 빌드된다.

</details>

### 연습 2 — 니블 테이블 (64 바이트)

플래시가 빠듯한 M0 를 위해 16엔트리 테이블로 CRC-32 를 계산하라. 한 바이트에 조회 몇 번?

<details><summary>답 보기</summary>

보조정리 1 을 4비트로: TN[i] = S^4(i), 스텝은 `c = TN[c & 0xF] ^ (c >> 4)`. 바이트마다 `c ^= b` 후 2번 — 반사형이라 **하위 니블 먼저**. 64 B 로 바이트 테이블(1 KiB)의 약 절반 속도(이 맥 64 KiB 기준 314 vs 587 MB/s, 비트 단위 188 의 1.7배). 테스트: check 0xCBF43926 + 무작위 1000개 vs 비트 단위.

```c
while (n--) { c ^= *p++; c = TN[c & 0xFu] ^ (c >> 4); c = TN[c & 0xFu] ^ (c >> 4); }
```

</details>

### 연습 3 — 비반사(MSB-first) CRC-32/MPEG-2 와 32비트 워드 공급

같은 다항식 0x04C11DB7 을 반사 없이 쓰는 CRC-32/MPEG-2(init ~0, xorout 0, check 0x0376E6E7)와 CRC-32/BZIP2(xorout ~0, check 0xFC891918)를 테이블로 만들어라. 그리고 STM32 식 하드웨어 유닛처럼 32비트 워드를 통째로 넣는 함수가 "워드를 BE 바이트로 펼쳐 MPEG-2" 와 같음을 보여라.

<details><summary>답 보기</summary>

비반사 테이블: `TM[i] = S'^8(i << 24)` (왼쪽으로 밀고 비트 31 이 1 이면 poly XOR), TM[1] = 0x04C11DB7. 스텝: `c = (c << 8) ^ TM[(c >> 24) ^ b]`. 워드 공급은 `c ^= w` 후 0 바이트 4스텝 — 보조정리 2 의 MSB-first 판(여기선 워드의 **상위** 바이트가 먼저 처리되므로 BE 펼침과 같다). 무작위 1000개 워드열에서 일치. 주의: 리틀 엔디언 MCU 의 바이트 버퍼를 `uint32_t*` 로 CRC 유닛에 넣으면 바이트 순서가 뒤집혀 PC 쪽 `crc32()` 와 안 맞는 것이 STM32 CRC 의 단골 질문이다 — 입력 반사·출력 반사·바이트 반전 설정이 있는 칩인지 RM 확인 권장.

</details>

### 연습 4 — 지워진 플래시(0xFF × n)의 CRC 를 O(log² n) 로

이미지 뒤 섹터 끝까지 0xFF 패딩이 있다. 플래시를 읽지 않고 crc(img ‖ FF^pad) 를 구하라.

<details><summary>답 보기</summary>

combine 으로 배가: piece = crc(FF^len) 을 들고 `piece = combine(piece, piece, len); len *= 2`, n 의 비트가 1 이면 `acc = combine(acc, piece, len)`. combine 하나가 O(log n) 곱셈이라 전체 O(log² n). 그다음 `combine(crc(img), crc_ff(pad), pad)`.

n = 0..3000 전부와 4096, 65535, 65536, 131079, 1 MiB 에서 직접 루프와 일치, 그리고 1000 B 이미지 + 7192 B 패딩 = 8 KiB 섹터 전체 CRC 와 일치. (0x00 패딩이면 더 쉽다: crc 를 ~ 해서 x^(8n) 곱하고 다시 ~.)

</details>

### 연습 5 — CRC 위조: 4바이트로 원하는 CRC 만들기

메시지 M 과 목표값 t 가 주어질 때 crc32(M ‖ X) = t 가 되는 4바이트 X 를 구하라.

<details><summary>답 보기</summary>

M 뒤 레지스터 R = ~crc32(M), 원하는 최종 레지스터 S = ~t. 보조정리 2 로 X 처리 뒤 레지스터 = G(R ⊕ w) 이므로 w = R ⊕ G⁻¹(S). G 는 0 바이트 스텝 4번이고, 한 스텝 S = T[i] ⊕ (R' >> 8) 에서 S 의 최상위 바이트 = T[i] 의 최상위 바이트(R' >> 8 의 최상위는 0)다. T 의 최상위 바이트 256개가 모두 다르므로(테스트로 확인) i 를 역표로 찾고 R' = ((S ⊕ T[i]) << 8) | i. 이것을 4번. X = w 의 LE 바이트. 무작위 2000쌍 전부 성공. 예: "123456789" + E5 E1 D0 CD → 0xDEADBEEF (Python `zlib.crc32` 로도 0xdeadbeef 확인). → Q6: CRC 는 보안이 아니다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 14/14 passed (zlib 불필요)
```

```text
ex1 crc32c: 1000 random vs bitwise mismatches=0; ARMv8 __crc32cb checked=1000 mismatches=0
ex2 nibble table (64 bytes): 1000 random mismatches=0
ex3 msb-first: table vs bitwise mismatches=0, word-feed vs BE bytes mismatches=0
ex4 crc of 0xFF x n: n=0..3000 and 5 big sizes up to 1 MiB, mismatches=0
ex5 forge 4 bytes: 2000 random (msg, target) pairs, misses=0
ex5 example: "123456789" + E5 E1 D0 CD -> crc32 = DEADBEEF
14/14 passed
```

## 10. 핵심 정리

- CRC-32/ISO-HDLC: 반사 poly 0xEDB88320, `~crc` 로 들어가 `~c` 로 나온다 → 스트리밍 공짜. check 0xCBF43926, 빈 입력 0.
- 바이트 테이블 `c = T[(c ^ b) & 0xFF] ^ (c >> 8)`: 8번 미는 동안 XOR 여부는 하위 바이트만 정한다 + 선형성. 생성 = 256 × 비트 스텝 8번, 1 KiB.
- slicing-by-k: LE 워드를 한 번에 XOR 하면 남은 건 0 바이트 스텝(선형) → 바이트 j 는 T_{k−1−j}. T_k = T_{k−1} 에 0 바이트 한 스텝 더. 4 KiB / 8 KiB.
- 이 맥 64 KiB: 비트 188 · 니블 314 · 테이블 587 · slice4 1524 · slice8 2645 · `__crc32d` 11782 · zlib 46603 MB/s. 사슬 길이가 속도를 정한다.
- Cortex-M0 에선 slicing 이 이득 없음(비정렬 ldr 없음 + 레지스터 스필, 7.9 vs 7.75 명령/바이트) → 테이블이나 니블까지.
- combine: crc(A‖B) = crc(A)·x^(8|B|) mod P ⊕ crc(B). CRC 레지스터 = GF(2)[x]/P 원소, `b ← b·x` = 비트 스텝.
- LE 로 CRC 를 붙이면 전체 CRC = 0x2144DF1C (residue 0xDEBB20E3 의 ~). BE 로 붙이면 성립 안 함.
- CRC 는 오류 검출이지 보안이 아니다 — 4바이트로 아무 값이나 만들 수 있다.

## 참고자료

- RevEng CRC catalogue (Greg Cook) — CRC-32/ISO-HDLC(check 0xCBF43926, residue 0xDEBB20E3), CRC-32/ISCSI(0xE3069283), CRC-32/MPEG-2(0x0376E6E7), CRC-32/BZIP2(0xFC891918), CRC-32/JAMCRC(0x340BC6D9): <https://reveng.sourceforge.io/crc-catalogue/17plus.htm> (이 레슨의 값은 모두 코드로 재현)
- zlib `crc32.c` / `zlib.h` — `crc32()`, `crc32_combine()`, `get_crc_table()` API; 이 맥의 libz 1.2.12 와 대조: <https://github.com/madler/zlib>
- D. V. Sarwate, "Computation of Cyclic Redundancy Checks via Table Look-Up", *Communications of the ACM* 31(8), 1988 — 바이트 테이블.
- M. E. Kounavis, F. L. Berry, "A Systematic Approach to Building High Performance Software-Based CRC Generators", IEEE ISCC 2005 — slicing-by-8 (Intel). 서지 세부는 기억 의존 — 확인 권장.
- R. N. Williams, "A Painless Guide to CRC Error Detection Algorithms" (1993) — 반사/비반사 테이블, Rocksoft 파라미터 모델.
- Mark Adler 의 Stack Overflow 답변들 — crc32_combine 의 GF(2) 원리, 잔여값 (zlib 저자 설명).
- G. Castagnoli, S. Bräuer, M. Herrmann, "Optimization of Cyclic Redundancy-Check Codes with 24 and 32 Parity Bits", *IEEE Trans. Communications* 41(6), 1993 — CRC-32C.
- P. Koopman, CRC Polynomial Zoo / "32-Bit Cyclic Redundancy Codes for Internet Applications" (DSN 2002) — 다항식별 해밍 거리 비교.
- Arm, *Arm A-profile Architecture Reference Manual* — CRC32B/H/W/X, CRC32CB/… 명령; ACLE `__crc32b/__crc32d/__crc32cb` (이 맥에서 실행 확인).
- ST, STM32 각 계열 Reference Manual "CRC calculation unit" 장 — 고정/설정 가능 다항식, 입력·출력 반사 (계열별 차이 확인 권장).
- 이 레슨의 asm 은 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4` / `thumbv6m-none-eabi -mcpu=cortex-m0` `-O2 -S demo_m4.c` 실제 출력, 크기는 `llvm-size -A`.
- 관련 레슨: ALGO-M01 (CRC 원리·파라미터·검출 능력), ALGO-B14 (덧셈 체크섬), ALGO-B13 (비트 트릭·패리티), ALGO-M15 (정수 산술, ÷ 없는 M0), COMM-B08 (체크섬 vs CRC 검출), BM-A04 (이미지 CRC·부트로더), STOR-B03 (레코드 체크섬), C-M05 (엔디언 독립 직렬화), C-M14 (ISR 공유 데이터)

---
id: ALGO-B14
time: 60분
updated: 2026-11-11
---
# ALGO-B14 체크섬: 합 체크섬·인터넷 체크섬·Fletcher

## 학습 목표

- 8비트 합(sum)·XOR·2의 보수 체크섬(Intel HEX), RFC 1071 인터넷 체크섬(1의 보수 합, end-around carry), Fletcher-16/32 를 C 로 짜고, **공개 예제**(RFC 1071 §3, IPv4 헤더, Fletcher "abcde" 등)와 **다른 길로 계산한 기준 구현**(닫힌 식, uint64 + 나머지)으로 맞춰 본다.
- 각 체크섬이 **무엇을 못 보는지** 수식으로 말한다: 합은 바이트 순서, XOR 는 같은 비트 열의 두 번 뒤집힘, 인터넷 체크섬은 16비트 워드 순서와 0x0000/0xFFFF, Fletcher-16 은 0x00/0xFF. 그리고 랜덤 오류 주입 실험 표(실측)로 확인한다.
- RFC 1624 증분 갱신(라우터의 TTL 감소)과, 나눗셈기가 없는 Cortex-M0 에서 `% 255` 없이 Fletcher 를 계산하는 **지연 환원(deferred reduction) + 255 접기**를 구현하고, 32비트 누산기가 넘치지 않는 최대 블록(5802 바이트)을 증명한다.
- 체크섬이 실제로 쓰이는 곳(NMEA `*hh`, IPv4/UDP/TCP 헤더, Intel HEX, OSPF·zlib 의 Fletcher 계열)과 CRC 로 넘어가야 할 때(ALGO-M01)를 구분한다.

## 1. 문제

UART 프레임, 부트로더의 HEX 레코드, IP 헤더 — 펌웨어 면접에서 "이 체크섬 짜 보세요 → 이 오류는 잡나요? → M0 에서 빠르게?" 는 10분짜리 단골 문제다. 코드는 다섯 줄인데, 꼬리질문은 **검출 능력**과 **넘침(overflow)·엔디언·나눗셈 비용**으로 깊어진다.

체크섬(checksum)은 "데이터 전체를 짧은 숫자 하나로 요약해 같이 보내고, 받는 쪽이 다시 계산해 비교"하는 오류 검출(error detection) 방법이다. 이 레슨의 다섯 가지는 모두 **덧셈 계열**이다 — 덧셈은 순서를 바꿔도 결과가 같으므로(교환 법칙), 무엇을 더하느냐와 어떤 나머지(mod)로 자르느냐가 검출 능력을 정한다. 다항식 나눗셈을 쓰는 CRC 는 ALGO-M01(비트 단위)·ALGO-M02(테이블/slicing)에서 다룬다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">메시지 b0 b1 b2 b3 b4 (n = 5) 를 무엇으로 요약하나 — 바이트별 가중치</text>
  <text x="10" y="52" fill="currentColor">sum8 / xor8</text>
  <g stroke="currentColor">
    <rect x="150" y="38" width="50" height="22" class="f-mut"/><rect x="205" y="38" width="50" height="22" class="f-mut"/>
    <rect x="260" y="38" width="50" height="22" class="f-mut"/><rect x="315" y="38" width="50" height="22" class="f-mut"/>
    <rect x="370" y="38" width="50" height="22" class="f-mut"/>
  </g>
  <text x="165" y="54" fill="currentColor">×1</text><text x="220" y="54" fill="currentColor">×1</text>
  <text x="275" y="54" fill="currentColor">×1</text><text x="330" y="54" fill="currentColor">×1</text>
  <text x="385" y="54" fill="currentColor">×1</text>
  <text x="440" y="54" fill="currentColor">가중치가 모두 같다 → 순서를 바꿔도 같은 값</text>
  <text x="10" y="102" fill="currentColor">inet16</text>
  <g stroke="currentColor">
    <rect x="150" y="88" width="50" height="22" class="f-acc"/><rect x="205" y="88" width="50" height="22" class="f-ok"/>
    <rect x="260" y="88" width="50" height="22" class="f-acc"/><rect x="315" y="88" width="50" height="22" class="f-ok"/>
    <rect x="370" y="88" width="50" height="22" class="f-acc"/><rect x="425" y="88" width="50" height="22" class="f-mut" stroke-dasharray="4 3"/>
  </g>
  <text x="160" y="104" fill="currentColor">×256</text><text x="222" y="104" fill="currentColor">×1</text>
  <text x="270" y="104" fill="currentColor">×256</text><text x="332" y="104" fill="currentColor">×1</text>
  <text x="380" y="104" fill="currentColor">×256</text><text x="437" y="104" fill="currentColor">0 채움</text>
  <text x="490" y="104" fill="currentColor">BE 16비트 워드, mod 65535</text>
  <text x="10" y="152" fill="currentColor">Fletcher-16 s1</text>
  <g stroke="currentColor">
    <rect x="150" y="138" width="50" height="22" class="f-ok"/><rect x="205" y="138" width="50" height="22" class="f-ok"/>
    <rect x="260" y="138" width="50" height="22" class="f-ok"/><rect x="315" y="138" width="50" height="22" class="f-ok"/>
    <rect x="370" y="138" width="50" height="22" class="f-ok"/>
  </g>
  <text x="165" y="154" fill="currentColor">×1</text><text x="220" y="154" fill="currentColor">×1</text>
  <text x="275" y="154" fill="currentColor">×1</text><text x="330" y="154" fill="currentColor">×1</text>
  <text x="385" y="154" fill="currentColor">×1</text>
  <text x="440" y="154" fill="currentColor">s1 = Σ b_i  (mod 255)</text>
  <text x="10" y="194" fill="currentColor">Fletcher-16 s2</text>
  <g stroke="currentColor">
    <rect x="150" y="180" width="50" height="22" class="f-warn"/><rect x="205" y="180" width="50" height="22" class="f-warn"/>
    <rect x="260" y="180" width="50" height="22" class="f-warn"/><rect x="315" y="180" width="50" height="22" class="f-warn"/>
    <rect x="370" y="180" width="50" height="22" class="f-warn"/>
  </g>
  <text x="165" y="196" fill="currentColor">×5</text><text x="220" y="196" fill="currentColor">×4</text>
  <text x="275" y="196" fill="currentColor">×3</text><text x="330" y="196" fill="currentColor">×2</text>
  <text x="385" y="196" fill="currentColor">×1</text>
  <text x="440" y="196" fill="currentColor">s2 = Σ (n − i)·b_i  (mod 255)</text>
  <text x="440" y="214" fill="currentColor">→ 위치마다 가중치가 달라 순서가 보인다</text>
  <line x1="10" y1="236" x2="730" y2="236" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="260" fill="currentColor" font-family="monospace">s1 += b;  s2 += s1;</text>
  <text x="175" y="260" fill="currentColor">를 n 번 돌리면 b_i 는 s1 에 1번, s2 에는 (n − i)번 들어간다.</text>
  <text x="10" y="284" fill="currentColor">그래서 테스트의 기준 구현은 루프 대신 이 닫힌 식(closed form)을 uint64 로 계산해 대조한다.</text>
  <text x="10" y="308" fill="currentColor">인터넷 체크섬도 같다: 1의 보수 합 = Σ 워드 (mod 65535), 단 0 이 아닌 합은 0x0000 대신 0xFFFF 로 표현된다.</text>
</svg>
```

### Part A — 체크섬 여섯 함수와 공개 예제

- `sum8` (바이트 합 mod 256), `xor8` (바이트 XOR), `sum8_twos` (데이터 + 이 바이트 ≡ 0 mod 256 — Intel HEX 레코드 방식).
- `inet_checksum` — RFC 1071: **big-endian** 16비트 워드의 1의 보수 합(ones' complement sum), 홀수 길이는 끝에 0 바이트를 붙인 것처럼(끝 바이트 = 상위 바이트), 넘친 자리올림은 아래로 다시 더하고(end-around carry), 마지막에 비트 반전.
- `fletcher16` (mod 255, 결과 `s2 << 8 | s1`), `fletcher32` (16비트 워드를 **little-endian** 으로 조립, 홀수 길이는 0 채움, mod 65535, 결과 `s2 << 16 | s1`).
- 테스트: RFC 1071 §3 예제(합 0xDDF2), IPv4 헤더(체크섬 0xB861, 필드 포함 재계산 = 0), Fletcher-16 "abcde"/"abcdef"/"abcdefgh" → 0xC8F0/0x2057/0x0627, Fletcher-32 → 0xF04FC729/0x56502D2A/0xEBE19591, Intel HEX EOF 레코드 → 0xFF, 빈 입력, 랜덤 3000개(길이 0~300; 랜덤 / 0x00·0xFF 만 / 전부 0xFF)를 기준 구현과 대조, 131072 바이트 0xFF(누산기 한계).

### Part B — 증분 갱신과 나눗셈 없는 Fletcher

- `inet_update16(hc, m, m2)` — 16비트 필드 하나가 m → m2 로 바뀌었을 때 전체를 다시 더하지 않고 새 체크섬을 낸다 (RFC 1624 식 3). 랜덤 헤더 20000개에서 전체 재계산과 같아야 한다 (새 값으로 0x0000/0xFFFF 도 섞는다).
- `fold255(x)` — `%` 없이 x mod 255. 경계값 + 랜덤 10^6 개를 `%` 와 대조.
- `fletcher16_fast` — 블록 5802 바이트마다 한 번만 환원. "5802 가 최대"인 것을 테스트가 부등식으로 확인, 전부 0xFF 인 최악 입력(블록 경계 ±50 포함)에서 기준과 같아야 한다.

### Part C — 부트로더와 라우터

- `ihex_parse(line, rec)` — 부트로더가 UART 로 받는 Intel HEX 한 줄 `:LLAAAATT<data>CC` 를 형식 → 길이(LL + 5 바이트) → 체크섬(전체 합 ≡ 0) 순서로 검사. 반환 `IHEX_OK / ERR_FORMAT / ERR_LEN / ERR_CKSUM`.
- `ipv4_forward(h, len)` — 라우터가 패킷을 넘길 때 하는 일: 헤더 검사(버전 4, IHL ≥ 5, 길이) → 체크섬 검증 → TTL ≤ 1 이면 버림(-2) → TTL 감소 → **증분 갱신**으로 체크섬 고치기. IHL 5·6 랜덤 헤더 20000개.

오류 주입 실험(1·2비트, 바이트/워드 교환, 0x00↔0xFF 등)은 시간이 걸리므로 `demo_detect.c` 에 따로 있다 (5. 풀이 마지막).

## 2. Problem (English)

Implement the classic additive checksums used in embedded links and network headers:

```c
uint8_t  sum8(const uint8_t *p, size_t n);          /* Σ bytes mod 256 */
uint8_t  xor8(const uint8_t *p, size_t n);          /* XOR of bytes */
uint8_t  sum8_twos(const uint8_t *p, size_t n);     /* byte c such that sum8(p) + c ≡ 0 (mod 256) */
uint16_t inet_checksum(const uint8_t *p, size_t n); /* RFC 1071, big-endian words, odd length zero-padded */
uint16_t fletcher16(const uint8_t *p, size_t n);    /* mod 255, returns (s2 << 8) | s1 */
uint32_t fletcher32(const uint8_t *p, size_t n);    /* 16-bit little-endian words, mod 65535, (s2 << 16) | s1 */
uint16_t inet_update16(uint16_t hc, uint16_t m, uint16_t m_new);   /* RFC 1624 incremental update */
uint16_t fletcher16_fast(const uint8_t *p, size_t n);              /* no '%' or '/' in the inner loop */
```

Constraints: no dynamic allocation, O(n) time, O(1) extra memory; `inet_checksum` must be correct for n ≤ 131072 using a 32-bit accumulator; `fletcher16_fast` must use 32-bit accumulators and never overflow; target is a Cortex-M0 (no hardware divider).

Examples:

```text
fletcher16("abcde")  = 0xC8F0      fletcher32("abcde") = 0xF04FC729
inet_checksum(00 01 f2 03 f4 f5 f6 f7) = 0x220D        (sum = 0xDDF2, RFC 1071 §3)
inet_checksum(IPv4 header 4500 0073 0000 4000 4011 [0000] c0a8 0001 c0a8 00c7) = 0xB861
sum8_twos(00 00 00 01) = 0xFF      -> Intel HEX ":00000001FF"
```

Follow-ups: Which corruptions does each checksum miss? Why one's complement? How do you update the IP checksum after decrementing TTL without re-summing? How large can a block be before a 32-bit Fletcher accumulator overflows?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열한 군데(Part A 6개, Part B 3개, Part C 2개). `hexval()`, 기준 구현 `ref_*`, 하네스는 주어진다. 스텁은 0(또는 입력 그대로)을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:148  fletcher16(abcde, 5) == 0xC8F0
…
random 3000 msgs: mismatches sum8=2969 xor8=1966 twos=2969 inet=2401 f16=993 f32=2472
…
RFC 1624 incremental vs full: 20000 updates, mismatches=19999
fletcher16_fast vs ref: 400 msgs (len 0..40000, half all-0xFF), mismatches=199
…
ipv4_forward: 20000 random headers, bad=20000
FAIL starter.c:294  badfw == 0
10/48 passed
```

(exit 1. 통과한 10개는 스텁의 0 이 우연히 정답과 같은 경우 — 예: "필드 포함 검증 결과 = 0", "빈 입력의 Fletcher = 0".)

## 4. 힌트

<details><summary>힌트 1 — 인터넷 체크섬은 "넓은 누산기 + 마지막에 접기"</summary>

워드마다 접을 필요 없다. `uint32_t` 에 BE 워드(`p[0] << 8 | p[1]`)를 그냥 더하고, 끝에서 `while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);`. 한 번 접은 결과가 또 넘칠 수 있으니 `if` 가 아니라 `while` (7. 실수 1). 홀수 끝 바이트는 `p[n-1] << 8`.

</details>

<details><summary>힌트 2 — Fletcher 는 두 줄</summary>

`s1 = (s1 + b) % 255; s2 = (s2 + s1) % 255;`. 32 는 같은 모양에 워드 `p[i] | p[i+1] << 8`, mod 65535. 결과 배치는 s2 가 위. 테스트 벡터가 안 맞으면 먼저 바이트 순서(LE/BE)와 결과의 s1/s2 위치를 의심하라.

</details>

<details><summary>힌트 3 — 증분 갱신은 "옛 값 빼고 새 값 더하기", 단 1의 보수로</summary>

체크섬 HC = ~S (S = 1의 보수 합). 새 합 S' = S − m + m2. 1의 보수에서 −m 은 ~m. 그래서 S' = ~HC + ~m + m2 (1의 보수 덧셈 = 더하고 접기), HC' = ~S'. 2의 보수로 `hc + m − m2` 하면 자리올림이 사라져 틀린다.

</details>

<details><summary>힌트 4 — mod 255 를 나눗셈 없이</summary>

256 = 255 + 1 이므로 256 ≡ 1 (mod 255). 그래서 x = 256·h + l ≡ h + l. 상위 바이트를 하위 바이트에 더하는 것("접기")을 값이 256 근처로 줄어들 때까지 반복하고, 마지막에 255 이상이면 255 를 뺀다. 32비트 값은 몇 번 접어야 할까? (3번이면 부족하다 — 7. 실수 4)

</details>

<details><summary>힌트 5 — 블록 크기는 s2 의 최악값으로</summary>

블록 시작에 s1, s2 ≤ 254, 바이트가 전부 255 라면 k 바이트 뒤 s2 ≤ 254 + 254k + 255·k(k+1)/2. 이것이 2^32 − 1 이하인 최대 k 를 찾는다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 어떤 체크섬인가(표준이 있나: RFC 1071? Fletcher-16?), 바이트 순서(와이어는 BE), 길이 상한(누산기 크기), 홀수 길이, 결과 바이트 배치, 체크섬 필드 자체는 0 으로 두고 계산하나 |
| 2. 단순 해법 | 바이트마다 더하고 매번 `%` — O(n), 메모리 O(1). 정답이지만 M0 에서는 `%` 가 함수 호출 |
| 3. 개선 + 불변식 | 넓은 누산기에 모았다가 **블록 끝에서만** 환원. 불변식: "누산기 값 ≡ 지금까지의 진짜 합 (mod M), 그리고 누산기 < 2^32" |
| 4. 코드 | 아래 Part A/B |
| 5. 소리 내어 말할 테스트 | 빈 입력, 길이 1, 홀수 길이, 전부 0xFF(최악 자리올림), 공개 test vector, 필드 포함 재계산 = 0 |
| 6. 복잡도 | 시간 O(n), 추가 메모리 O(1), 스택 몇 바이트, 재귀 없음. 상수: 바이트당 덧셈 1~2개 |

### Part A — sum8, xor8, 2의 보수

`sum8` 은 `uint8_t` 누산기라 자동으로 mod 256 이다 (부호 없는 정수의 넘침은 C 에서 정의된 동작: 2^N 으로 나눈 나머지. `uint8_t` 는 연산 전에 `int` 로 승격되므로 `(uint8_t)(s + p[i])` 로 잘라 준다). `xor8` 은 비트 열(column)마다 1 의 개수가 홀수인지 — 즉 **비트 위치별 패리티 8개**다 (ALGO-B13 의 패리티 참고). 그래서 테스트의 기준 구현은 "열마다 1 을 세서 홀짝"으로 다른 길을 간다.

`sum8_twos` 는 `0 − sum8` 을 8비트로 자른 값이다. 받는 쪽은 **체크섬 바이트까지 다 더해 0 인지**만 보면 되므로 검증 코드가 짧다. Intel HEX 가 이 방식이다: `:00000001FF` → 00+00+00+01 = 0x01, 체크섬 0xFF, 합 0x100 ≡ 0. (Motorola S-record 는 비슷하지만 **1의 보수**(`~sum`) 를 쓴다 — 연습 아님, 헷갈리지 않게만.)

### Part A — 인터넷 체크섬 (RFC 1071): end-around carry

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">RFC 1071 §3 예제: 00 01 f2 03 f4 f5 f6 f7</text>
  <g font-family="monospace">
    <text x="30" y="50" fill="currentColor">  0001</text>
    <text x="30" y="70" fill="currentColor">+ f203   = 0f204</text>
    <text x="30" y="90" fill="currentColor">+ f4f5   = 1e6f9</text>
    <text x="30" y="110" fill="currentColor">+ f6f7   = 2ddf0</text>
  </g>
  <rect x="300" y="52" width="36" height="26" class="f-warn" stroke="currentColor"/>
  <text x="313" y="70" fill="currentColor" font-family="monospace">2</text>
  <rect x="340" y="52" width="100" height="26" class="f-acc" stroke="currentColor"/>
  <text x="372" y="70" fill="currentColor" font-family="monospace">ddf0</text>
  <text x="300" y="100" fill="currentColor">비트 16 위로 넘친 자리올림(2)을</text>
  <text x="300" y="118" fill="currentColor">버리지 않고 아래로 다시 더한다</text>
  <path d="M318 52 C 318 32, 428 32, 428 50" fill="none" stroke="currentColor" marker-end="url(#b14a)"/>
  <defs><marker id="b14a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <g font-family="monospace">
    <text x="470" y="70" fill="currentColor">ddf0 + 2 = ddf2</text>
    <text x="470" y="94" fill="currentColor">~ddf2    = 220d</text>
  </g>
  <text x="470" y="118" fill="currentColor">(이 값을 체크섬 필드에)</text>
  <line x1="10" y1="140" x2="730" y2="140" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="164" fill="currentColor" font-weight="bold">IPv4 헤더 20바이트 = BE 워드 10개, 체크섬은 워드 5</text>
  <g stroke="currentColor" font-family="monospace">
    <rect x="10" y="176" width="70" height="26" class="f-mut"/><rect x="82" y="176" width="70" height="26" class="f-mut"/>
    <rect x="154" y="176" width="70" height="26" class="f-mut"/><rect x="226" y="176" width="70" height="26" class="f-mut"/>
    <rect x="298" y="176" width="70" height="26" class="f-ok"/><rect x="370" y="176" width="70" height="26" class="f-warn"/>
    <rect x="442" y="176" width="70" height="26" class="f-mut"/><rect x="514" y="176" width="70" height="26" class="f-mut"/>
    <rect x="586" y="176" width="70" height="26" class="f-mut"/><rect x="658" y="176" width="70" height="26" class="f-mut"/>
  </g>
  <g font-family="monospace">
    <text x="27" y="194" fill="currentColor">4500</text><text x="99" y="194" fill="currentColor">0073</text>
    <text x="171" y="194" fill="currentColor">0000</text><text x="243" y="194" fill="currentColor">4000</text>
    <text x="315" y="194" fill="currentColor">4011</text><text x="387" y="194" fill="currentColor">b861</text>
    <text x="459" y="194" fill="currentColor">c0a8</text><text x="531" y="194" fill="currentColor">0001</text>
    <text x="603" y="194" fill="currentColor">c0a8</text><text x="675" y="194" fill="currentColor">00c7</text>
  </g>
  <text x="298" y="222" fill="currentColor">TTL|proto</text><text x="380" y="222" fill="currentColor">checksum</text>
  <text x="10" y="250" fill="currentColor">보낼 때: 체크섬 칸 = 0 으로 두고 합 → 0x2479c → 0x479e → ~ = 0xb861</text>
  <text x="10" y="272" fill="currentColor">받을 때: 칸까지 전부 더하면 0x479e + 0xb861 = 0xffff (= −0) → ~ = 0 이면 OK</text>
  <text x="10" y="292" fill="currentColor">라우터: TTL 0x40 → 0x3f 이면 워드 4 가 0x100 줄어듦 → 체크섬은 0x100 늘어 0xb961 (Part C)</text>
</svg>
```

```c
uint32_t sum = 0;
while (n > 1) { sum += (uint32_t)p[0] << 8 | p[1]; p += 2; n -= 2; }
if (n) sum += (uint32_t)p[0] << 8;                    /* 홀수 끝 = 상위 바이트 */
while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
return (uint16_t)~sum;
```

**왜 접기가 mod 65535 인가.** 2^16 = 65535 + 1 ≡ 1 (mod 65535) 이므로 x = 2^16·h + l ≡ h + l. 접어도 나머지가 변하지 않는다. 0 이 아닌 합을 접으면 결과는 [1, 0xFFFF] 안에 있다 (0 이 아닌 수를 접어서 0 이 될 수 없다). 0xFFFF ≡ 0 이라 1의 보수 체계에는 0 이 두 개(+0 = 0x0000, −0 = 0xFFFF) 있고, **합이 0x0000 인 것은 데이터가 전부 0 일 때뿐**이다. 테스트의 기준 구현은 이 사실을 그대로 쓴다: 정수 총합 T 에 대해 `S = (T == 0) ? 0 : 1 + (T − 1) mod 65535`.

**누산기 크기.** 워드 하나 ≤ 0xFFFF 이므로 k 워드의 합 ≤ 65535k. 2^32 − 1 = (2^16 − 1)(2^16 + 1) = 65535 × 65537 이므로 65535k ≤ 2^32 − 1 ⇔ k ≤ 65537. 즉 **65537 워드 = 131074 바이트**까지 안전하다. IP 패킷은 최대 65535 바이트라 충분하다. 테스트는 131072 바이트 0xFF 로 경계 근처를 확인한다 (결과 0x0000: 0xFFFF 들의 합 = −0 → 보수 = 0).

**트레이스 (IPv4 헤더, 체크섬 칸 0).**

| 워드 | 4500 | 0073 | 0000 | 4000 | 4011 | 0000 | c0a8 | 0001 | c0a8 | 00c7 |
|---|---|---|---|---|---|---|---|---|---|---|
| 누적 | 4500 | 4573 | 4573 | 8573 | c584 | c584 | 1862c | 1862d | 246d5 | 2479c |

접기: 0x479c + 0x2 = 0x479e, 보수 0xb861. 헤더에 실린 값과 같다 (테스트 `inet_checksum(ip, 20) == 0xB861`, 그리고 칸을 채운 헤더 전체 → 0).

**바이트 순서 독립성 (RFC 1071 §2(B)).** BE 로 더하든 LE 로 더하든(두 바이트를 바꿔 읽든) 결과는 **바이트를 바꾼 같은 값**이 된다: 워드마다 바이트를 바꾸는 것은 16비트 값을 8비트 회전(rotate)하는 것이고, 2^8·x 를 mod 65535 로 보면 회전과 같으며, 덧셈과 교환한다. 그래서 리틀 엔디언 MCU 는 `uint16_t` 로 그냥 읽어 더한 뒤 마지막에 한 번만 스왑하면 된다 (연습 5, 랜덤 3000개로 확인). 이 성질은 mod 65535(= 2^16 − 1)이기 때문에 생긴다 — 그냥 mod 65536 합이면 성립하지 않는다.

### Part A — Fletcher-16/32: 두 번째 합이 위치를 본다

```c
s1 = (s1 + p[i]) % 255u;
s2 = (s2 + s1) % 255u;
```

"abcde" 트레이스 (a=97 … e=101):

| i | 바이트 | s1 = (s1 + b) mod 255 | s2 = (s2 + s1) mod 255 |
|---|---|---|---|
| 0 | 'a' 97 | 97 | 97 |
| 1 | 'b' 98 | 195 | 292 → 37 |
| 2 | 'c' 99 | 294 → 39 | 76 |
| 3 | 'd' 100 | 139 | 215 |
| 4 | 'e' 101 | 240 = 0xF0 | 455 → 200 = 0xC8 |

결과 `s2 << 8 | s1` = 0xC8F0. 닫힌 식으로도: Σb = 495 ≡ 240, Σ(5 − i)b_i = 5·97 + 4·98 + 3·99 + 2·100 + 1·101 = 1475 = 5·255 + 200 ≡ 200. 두 길이 같은 답이다 — "abcdef"(0x2057), "abcdefgh"(0x0627)도 이 레슨 작업 중 Python 으로 루프·닫힌 식 두 방법으로 재현했고, 테스트도 루프(학생 코드)와 닫힌 식(`ref_f16`)을 3000개 대조한다.

**Fletcher-32 의 바이트 순서.** 위키백과의 Fletcher-32 test vector(0xF04FC729 등)는 문자열을 **little-endian 16비트 워드**로 묶은 값이다. 같은 "abcde" 를 BE 로 묶으면 0x4FF029C7 이 나온다(이 레슨에서 계산). 인터넷 체크섬과 달리 Fletcher 는 표준 바이트 순서가 프로토콜마다 정해지므로, 구현 전에 **"워드를 어떻게 묶나"** 를 꼭 물어야 한다. 이 코드는 `p[i] | p[i+1] << 8` 로 명시적으로 조립해서 호스트 엔디언과 무관하다.

**왜 255 인가.** 255 = 2^8 − 1 이라 mod 255 를 나눗셈 없이 "접기"로 할 수 있다 (Part B). 대가: 0x00 과 0xFF 가 같은 나머지(0)라 구분이 안 된다 — 이것이 Fletcher-16 의 맹점(blind spot)이다. 1의 보수 합의 ±0 과 같은 뿌리다.

### Part B — RFC 1624 증분 갱신

S = 1의 보수 합, HC = ~S. 필드 m 이 m2 로 바뀌면 S' = S − m + m2 (mod 65535). 1의 보수에서 −m = ~m 이고 S = ~HC 이므로

  HC' = ~(~HC + ~m + m2)     (RFC 1624 식 3, 덧셈은 접기 포함)

**전체 재계산과 정확히 같은 이유.** 덧셈 피연산자 중 ~HC = S ≥ 1 (헤더가 전부 0 일 수 없으니) 이므로 정수 합이 0 이 아니고, 접은 결과는 [1, 0xFFFF] 에 있다. 전체 재계산 S' 도 [1, 0xFFFF] 에 있고 둘은 mod 65535 로 같다. [1, 0xFFFF] 의 65535 개 값은 나머지가 모두 다르므로 **같은 값**이다. 이 논증이 RFC 1624 가 식 2(`HC − ~m − m2` 꼴)를 버린 이유와 맞닿는다: 식 2 는 결과를 [0, 0xFFFE] 쪽으로 표현해서 ±0 경계에서 틀린다. 이 레슨 작업 중 식 2 를 직접 짜서 랜덤 200000번 돌리면 **5번** 틀렸다 (예: HC=0x9238, m=0x328F, m2=0xC4C7 → 식 2 = 0xFFFF, 전체 재계산 = 0x0000, 식 3 = 0x0000). 테스트 20000번에서 식 3 은 0번 틀린다.

라우터는 패킷마다 TTL 을 하나 줄이므로 이 식이 빠른 경로(fast path)에 들어간다. 바이트 1개의 변경이라 워드 하나(TTL|protocol)만 바꾸면 된다. 체크섬 오프로드 하드웨어(NETHW-B07)와 고속 계산 기법은 NET-B02 가 깊게 다룬다.

### Part B — Cortex-M0 의 `% 255` 와 지연 환원

ARMv6-M(Cortex-M0/M0+)에는 나눗셈 명령이 없다. 같은 Fletcher-16 루프를 `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -O2 -S` 로 컴파일하면 (이 맥에서 실제로 본 것):

```text
mod255:                          ; Cortex-M0: x % 255
    push  {r7, lr}
    movs  r1, #255
    bl    __aeabi_uidivmod       ; 런타임 라이브러리의 소프트웨어 나눗셈 호출
    mov   r0, r1
    pop   {r7, pc}
```

순진한 `fletcher16` 루프는 2바이트씩 펼쳐져 **바이트당 `bl __aeabi_uidivmod` 2번**이 된다. Cortex-M4(`thumbv7em`)에서는 같은 `% 255` 가 `movw/movt` 상수 + `umull`(32×32→64 곱) + 시프트로 바뀐다 — 역수 곱셈. M0 의 `muls` 는 하위 32비트만 주므로 이 변환을 못 하고 함수를 부른다. (소프트웨어 나눗셈 한 번의 사이클 수는 라이브러리 구현에 따라 다르다. 이 레슨은 M0 실측이 없으니 "바이트당 함수 호출 2번" 까지만 말한다.)

해결은 두 단계다.

1. **지연 환원(deferred reduction)**: s1, s2 를 `uint32_t` 에 그냥 더하고, 넘치기 전에만 환원한다. 블록 시작에 s1, s2 ≤ 254, 바이트가 모두 255 인 최악에서 k 바이트 뒤
   s1 ≤ 254 + 255k, s2 ≤ 254 + Σ_{j=1..k}(254 + 255j) = 254 + 254k + 255·k(k+1)/2.
   k = 5802: 254 + 1,473,708 + 4,292,798,265 = 4,294,272,227 ≤ 4,294,967,295. k = 5803 이면 4,295,752,246 으로 넘친다. **최대 블록 = 5802 바이트** (테스트가 uint64 로 같은 부등식을 돌려 `kmax == 5802` 확인).
2. **255 접기**: 256 ≡ 1 (mod 255) 이므로 x ≡ (x & 0xFF) + (x >> 8). 32비트 x 는 4번 접으면 ≤ 256 (상한: 2^32−1 → ≤ 2^24+254 → ≤ 2^16+255 → ≤ 511 → ≤ 256), 마지막에 255 이상이면 255 를 빼서 [0, 254]. 이 레슨 작업 중 2^32 개 전부를 `%` 와 비교: 4번 접기는 0개 틀림, **3번 접기는 65793개 틀림** (첫 반례 0xFF0000FF).

M0 로 컴파일한 `fletcher16_fast` 의 안쪽 루프 (실제 출력, 2바이트씩 펼침):

```text
.LBB0_8:
    ldrb  r2, [r6]
    adds  r2, r4, r2        ; s1 += b
    adds  r3, r2, r3        ; s2 += s1
    ldrb  r4, [r6, #1]
    adds  r4, r2, r4
    adds  r3, r4, r3
    adds  r6, r6, #2
    subs  r7, r7, #2
    bne   .LBB0_8
```

함수 호출이 없고 2바이트에 9개 명령. 접기(`lsrs/uxtb/adds` × 4 + `cmp/subs`)는 5802 바이트에 한 번이다.

**이 맥(Apple Silicon M 시리즈, -O2)의 처리량** — `demo_bench.c`, 64 KiB 버퍼, 200회 반복 × 9번 중 중앙값 (실제 출력, 한 번 실행):

| 체크섬 | ns/byte | GB/s |
|---|---|---|
| sum8 (uint8_t acc) | 0.008 | 121.36 |
| sum8 (uint32 acc) | 0.012 | 83.49 |
| xor8 | 0.008 | 121.36 |
| inet16 (RFC 1071) | 0.056 | 17.78 |
| fletcher16 naive %255 | 2.350 | 0.43 |
| fletcher16 deferred | 0.253 | 3.96 |
| fletcher32 %65535 | 1.195 | 0.84 |
| crc16 bitwise | 6.833 | 0.15 |

절대값보다 비율을 볼 것. sum8/xor8 는 컴파일러가 NEON 으로 벡터화해서 비현실적으로 빠르고 실행마다 흔들린다(다른 실행에서 93 GB/s). 이 맥에서는 `% 255` 가 곱셈으로 바뀌어 있는데도 순진한 Fletcher 가 지연 환원보다 **약 9배** 느리다 — 나머지 연산이 s1 → s2 의존 사슬 위에 있어서다. 비트 단위 CRC-16 은 지연 환원 Fletcher 보다 약 27배 느리다; 테이블 CRC(ALGO-M02)나 하드웨어 CRC 유닛(COMM-B09)이 그 차이를 줄인다.

### Part C — Intel HEX 레코드 검사

검사 순서가 중요하다: **형식 → 길이 → 체크섬**. `:` 로 시작, 16진 문자 짝수 개, 최소 11글자(`:LLAAAATTCC`), 바이트 수 = LL + 5. 그 다음에야 "전부 더해 0". 체크섬은 길이 필드를 믿을 수 있게 해 주지 않는다 — 아래 실험의 마지막 줄처럼 앞에 0x00 을 끼워 넣은 데이터는 덧셈 체크섬 넷 다 못 잡는다. 레코드 길이를 LL 과 대조하는 것이 그 구멍을 막는다. 테스트: 위키백과 예 `:0B0010006164647265737320676170A7`("address gap", 이 레슨에서 합 ≡ 0 확인), EOF `:00000001FF`, CC 1 틀림, 데이터 1비트, LL 불일치, `:` 없음, 홀수 글자, 16진이 아닌 글자, `:` 하나.

### Part C — IPv4 포워딩

순서: 칸 포함 재계산 = 0 인지 → TTL ≤ 1 이면 버림(실제 라우터는 ICMP Time Exceeded 도 보냄, 여기선 생략) → 워드 4(TTL|protocol)의 옛 값 m 저장 → TTL 감소 → 새 값 m2 로 `inet_update16`. 예제 헤더(TTL 0x40)는 0x3F 가 되고 체크섬은 0xB861 → **0xB961** (합이 0x100 줄면 보수는 0x100 는다). 랜덤 IHL 5·6 헤더 20000개에서 감소 후 기준 구현 재계산 = 0 을 확인한다. 실제 라우터는 검증을 하드웨어에 맡기기도 하고(NETHW-B07), IPv6 는 헤더 체크섬 자체가 없다(RFC 8200) — 링크 계층 CRC 와 상위 계층(UDP/TCP) 체크섬에 맡긴다.

### 오류 주입 실험 — `demo_detect.c`

32바이트 랜덤 메시지(xorshift32 고정 시드)에 오류 패턴을 하나 넣고 "체크섬 값이 바뀌었나"를 센다. 패턴마다 200000회, 실제로 데이터가 안 바뀐 경우(같은 값끼리 교환)는 빼고 셌다. 마지막 열 CRC-16/CCITT-FALSE(poly 0x1021, init 0xFFFF, check 0x29B1 자가 확인)는 비교용이다. 실제 출력:

```text
error pattern (32-byte msg)     trials     sum8     xor8   inet16     fl16     fl32    crc16
1-bit flip                      200000 100.000% 100.000% 100.000% 100.000% 100.000% 100.000%
2-bit flip (anywhere)           200000  93.117%  87.814%  97.065% 100.000% 100.000% 100.000%
2-bit, same bit pos             200000  43.888%   0.000%  75.940% 100.000% 100.000% 100.000%
adjacent byte swap              200000   0.000%   0.000% 100.000%  99.998% 100.000% 100.000%
adjacent 16-bit word swap       200000   0.000%   0.000%   0.000%  99.615% 100.000%  99.998%
one byte 0x00 -> 0xFF           200000 100.000% 100.000% 100.000%   0.000% 100.000% 100.000%
aligned word 0x0000 -> 0xFFFF   200000 100.000%   0.000%   0.000%   0.000%   0.000% 100.000%
4 random bytes overwritten      200000  99.609%  99.613%  99.973%  99.998% 100.000%  99.999%
burst 2..16 bits                200000  99.763%  99.758% 100.000% 100.000% 100.000% 100.000%
prepend two 0x00 bytes          200000   0.000%   0.000%   0.000%   0.000%   0.000% 100.000%
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">맹점 네 가지 — 바뀌었는데 합이 그대로인 이유</text>
  <text x="10" y="48" fill="currentColor">① XOR: 같은 비트 열의 두 뒤집힘</text>
  <g stroke="currentColor">
    <rect x="30" y="60" width="160" height="22" class="f-mut"/><rect x="30" y="86" width="160" height="22" class="f-mut"/>
    <rect x="130" y="60" width="20" height="22" class="f-warn"/><rect x="130" y="86" width="20" height="22" class="f-warn"/>
  </g>
  <text x="36" y="76" fill="currentColor" font-family="monospace">1 0 1 1 0 1 0 0</text>
  <text x="36" y="102" fill="currentColor" font-family="monospace">0 1 1 0 0 0 1 1</text>
  <text x="30" y="130" fill="currentColor">열의 1 개수 홀짝이 그대로 → xor8 불변</text>
  <text x="390" y="48" fill="currentColor">② 합: 순서를 못 본다 (가중치 모두 1)</text>
  <g stroke="currentColor">
    <rect x="410" y="60" width="50" height="22" class="f-acc"/><rect x="464" y="60" width="50" height="22" class="f-ok"/>
    <rect x="410" y="94" width="50" height="22" class="f-ok"/><rect x="464" y="94" width="50" height="22" class="f-acc"/>
  </g>
  <text x="424" y="76" fill="currentColor" font-family="monospace">0x3A</text><text x="478" y="76" fill="currentColor" font-family="monospace">0x91</text>
  <text x="424" y="110" fill="currentColor" font-family="monospace">0x91</text><text x="478" y="110" fill="currentColor" font-family="monospace">0x3A</text>
  <text x="530" y="92" fill="currentColor">a + b = b + a</text>
  <text x="390" y="138" fill="currentColor">inet16: 바이트 교환은 본다 (×256 vs ×1)</text>
  <text x="390" y="156" fill="currentColor">하지만 16비트 워드 교환은 못 본다</text>
  <line x1="10" y1="172" x2="730" y2="172" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="196" fill="currentColor">③ mod 255 에서 0x00 ≡ 0xFF</text>
  <text x="30" y="220" fill="currentColor" font-family="monospace">Δ = 0xFF − 0x00 = 255 ≡ 0</text>
  <text x="30" y="242" fill="currentColor">→ Fletcher-16 의 s1, s2 둘 다 불변</text>
  <text x="30" y="264" fill="currentColor">(Δ·가중치도 255 의 배수)</text>
  <text x="390" y="196" fill="currentColor">④ mod 65535 에서 0x0000 ≡ 0xFFFF (±0)</text>
  <text x="410" y="220" fill="currentColor" font-family="monospace">Δ = 0xFFFF ≡ 0</text>
  <text x="410" y="242" fill="currentColor">→ inet16, Fletcher-32 불변</text>
  <text x="410" y="264" fill="currentColor">(Fletcher-16 도: 두 바이트 Δ = 255)</text>
  <text x="10" y="290" fill="currentColor">공통: Δ 들의 가중합 ≡ 0 (mod M) 인 오류는 못 본다. CRC 에는 이런 구조가 없다.</text>
</svg>
```

**0% 와 100% 가 아닌 칸을 하나씩.** 2비트 오류를 "서로 다른 비트 256개 중 2개"로 고르면 같은 비트 열·다른 바이트일 확률은 31/255 다.

| 칸 | 측정 | 설명 (기대값) |
|---|---|---|
| xor8, 2비트 아무 곳 | 87.814% | 같은 열·다른 바이트면 무조건 놓침: 1 − 31/255 = 87.84% |
| sum8, 2비트 아무 곳 | 93.117% | 같은 열이고 방향이 반대(+2^k, −2^k)면 상쇄. 비트 7 은 같은 방향도 상쇄(+128 +128 = 256 ≡ 0). 놓칠 확률 (31/255)·(7/8·1/2 + 1/8) = 6.84% → 93.16% |
| inet16, 2비트 아무 곳 | 97.065% | 16비트 레인이 같고(15/255) 방향 반대(1/2)일 때만: 1 − 15/510 = 97.06%. 비트 15 의 같은 방향 두 번은 0x10000 → 접으면 1 이라 **잡힌다** — 1의 보수의 장점 |
| xor8, 같은 비트 열 | 0% | 정의상 열 패리티 불변 |
| sum8, 같은 비트 열 | 43.888% | 비트 0~6 은 방향 반대일 때(1/2), 비트 7 은 항상 놓침 → 놓침 7/16 + 1/8 = 56.25%, 검출 43.75% |
| inet16, 같은 비트 열 | 75.940% | 두 바이트가 같은 레인(짝·짝 또는 홀·홀, 확률 15/31)이고 방향 반대(1/2)면 놓침: 1 − 15/62 = 75.8% |
| sum8·xor8, 인접 바이트 교환 | 0% | 순서를 못 본다 |
| inet16, 인접 바이트 교환 | 100% | 워드 안 교환: Δ = 255·d, 워드 경계 교환: Δ = d − 256d = −255d. 0 < \|d\| < 256 이면 65535 = 255·257 의 배수가 될 수 없다 |
| fl16, 인접 바이트 교환 | 99.998% | Δs1 = 0, Δs2 = ±d. d ≡ 0 (mod 255) 인 쌍 = {0x00, 0xFF} 만 놓침: 확률 2/(256·255) ≈ 0.003% |
| sum8·xor8·inet16, 워드 교환 | 0% | 같은 레인끼리 자리만 바뀜 → 합 그대로 |
| fl16, 워드 교환 | 99.615% | Δs2 = 2·(B − A), A·B = 각 워드의 두 바이트 합. A ≡ B (mod 255) 면 놓침 ≈ 1/255 = 0.39% |
| crc16, 워드 교환 | 99.998% | 오류 패턴이 최대 32비트로 CRC-16 의 보장 범위(버스트 ≤ 16) 밖 → 약 2^−16 확률로 놓침 |
| fl16, 0x00 → 0xFF | 0% | ③. 반면 sum8 은 Δ = 255 ≢ 0 (mod 256) 이라 잡는다 |
| 0x0000 → 0xFFFF | xor8·inet16·fl16·fl32 0% | xor8: 0xFF ^ 0xFF = 0. inet16·fl32: ④. fl16: 바이트 둘 다 ③. sum8 은 Δ = 510 ≡ 254 라 잡는다 |
| 4바이트 덮어쓰기 | 8비트 ≈ 99.61%, inet16 99.973% | 무작위 변경엔 "체크섬 비트 수"만큼: 1 − 1/256 = 99.609%. inet16 이 1 − 1/65535 보다 낮은 것은 주로, 바뀐 바이트가 모두 같은 레인(짝수 또는 홀수 자리)에 몰리면 그 레인의 8비트 차이 합(Σd = 0)만으로 상쇄되기 때문 (같은 레인 확률 약 1/8 × 상쇄 확률 수백 분의 1 ≈ 측정값 0.027% 규모) |
| 버스트 2~16비트 | sum8·xor8 99.76% | 9비트 이상 버스트는 바이트 2~3개에 걸쳐 상쇄될 수 있다. inet16·Fletcher·CRC 는 이 실험에서 놓친 것 0 |
| 앞에 0x00 두 바이트 | CRC 빼고 0% | 0 은 합을 바꾸지 않고, Fletcher 도 s1 = 0 인 동안 s2 가 안 변한다. CRC 는 init = 0xFFFF 라 잡는다 (init = 0 인 CRC 였다면 같이 놓친다). → **길이는 따로 검사**해야 한다 (Part C) |

결론: 1비트는 모두 잡는다. 2비트·순서 오류부터 갈린다: **xor8 < sum8 < inet16 < Fletcher < CRC**. 체크섬과 CRC 의 체계적 비교(해밍 거리, 메시지 길이별)는 COMM-B08 이, CRC 자체는 ALGO-M01 이 다룬다.

### 복잡도

| 함수 | 시간 | 추가 메모리 | 메모 |
|---|---|---|---|
| sum8 / xor8 / sum8_twos | O(n) | O(1) | 바이트당 연산 1개 |
| inet_checksum | O(n) | O(1) | n ≤ 131074 바이트(32비트 누산기). 더 길면 블록마다 접거나 uint64 |
| fletcher16 / 32 | O(n) | O(1) | 바이트(워드)당 `%` 2개 |
| fletcher16_fast | O(n) | O(1) | 5802 바이트마다 접기 2번. 안쪽 루프 덧셈 2개 |
| inet_update16 | O(1) | O(1) | 헤더 길이와 무관 |
| ihex_parse | O(L) | 260 바이트 스택 | L = 줄 길이 ≤ 521 |

재귀 없음, 스택은 지역 변수 몇 개(ihex 만 레코드 버퍼). ISR 에서 바이트 하나씩 받아도 sum/xor/Fletcher 는 상태(s1, s2)만 들고 있으면 되므로 O(1)/바이트로 스트리밍된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 48/48 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_detect.c -o demo_detect && ./demo_detect   # 약 1.4 초
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench      # 약 1.3 초
```

## 7. 자주 하는 실수

**1. end-around carry 를 한 번만 접는다.** `sum = (sum & 0xFFFF) + (sum >> 16);` 한 줄로 끝내면 접은 결과가 다시 0x10000 이 될 수 있다. 예: 워드 FFFF FFFF 0001 → 합 0x1FFFF → 한 번 접기 0x10000 → `(uint16_t)~` = 0xFFFF, 정답은 두 번 접어 0x0001 → 0xFFFE. 이 레슨 작업 중 랜덤 메시지 100000개(1~1500 바이트)에서 한 번 접기는 **271개** 틀렸다 — 드물어서 단위 테스트 몇 개로는 안 보인다. 고친 코드: `while (sum >> 16) ...`.

**2. 홀수 끝 바이트를 하위 바이트로 더한다.** RFC 1071 은 0 바이트를 **뒤에** 붙인 것과 같다 → BE 워드에서 끝 바이트는 상위(`<< 8`). `sum += p[n-1]` 로 짜면 홀수 길이 메시지 약 50000개 중 49645개 틀렸다(끝 바이트가 0 이 아니면 거의 항상). LE 로 더하는 경우엔 반대로 하위가 맞다 (연습 5) — 엔디언과 패딩 위치를 같이 바꿔야 한다.

**3. Fletcher 를 `uint8_t` 넘침(mod 256)으로 짠다.** `uint8_t s1, s2; s1 += b; s2 += s1;` 은 mod 256 이라 Fletcher-16 이 아니다. "abcde" → 0xC3EF (정답 0xC8F0). u-blox UBX 프로토콜의 CK_A/CK_B 가 바로 이 mod-256 형태라(문서는 "8-Bit Fletcher" 라고 부른다) 헷갈리기 쉽다. 프로토콜 문서의 의사코드를 그대로 따를 것.

**4. 255 접기를 덜 한다 / 255 를 0 으로 안 바꾼다.** 32비트 값을 3번만 접으면 최대 511 까지 남아 한 번 빼도 255·256 이 남는다. 전수 검사에서 65793개 틀림(첫 반례 0xFF0000FF). 또 마지막 `x >= 255 ? x − 255 : x` 를 빼먹으면 255 가 그대로 나와 표준 결과(0~254)와 다르다 — 전부 0xFF 인 메시지에서 바로 드러난다(테스트가 블록마다 0xFF 를 넣는 이유).

**5. 증분 갱신을 2의 보수로 한다.** `hc = hc + m − m2` (uint16 넘침)는 자리올림을 버린다. 랜덤 200000번 중 **44892번** 틀렸다. RFC 1624 식 2(`HC − ~m − m2` 를 1의 보수로)도 ±0 경계에서 5번 틀렸다 (5. 풀이). 식 3 `~(~HC + ~m + m2)` 만 항상 맞다.

**6. 체크섬 칸을 0 으로 안 두고 계산한다.** 보낼 때는 칸 = 0 으로 두고 계산해 넣는다. 받을 때는 칸까지 포함해 다시 계산해 0(인터넷 체크섬) 또는 합 0(Intel HEX)인지 본다. 받는 쪽에서 칸을 0 으로 지우고 계산해 비교해도 되지만, 그럼 버퍼를 고쳐야 한다(DMA 로 받은 읽기 전용 버퍼라면 곤란).

**7. 체크섬이 맞으면 "정상"이라고 믿는다.** 앞에 0 바이트가 붙은 메시지, 바이트가 뒤바뀐 메시지(sum/xor)는 체크섬이 맞는다. 길이 필드·시퀀스 번호·형식 검사를 같이 해야 한다 (Part C 의 검사 순서).

## 8. 면접 Q&A

<details><summary>Q1. 인터넷 체크섬은 왜 그냥 16비트 합(mod 65536)이 아니라 1의 보수 합인가요?</summary>

세 가지. (1) **자리올림을 버리지 않는다**: mod 65536 합에서 비트 15 의 자리올림은 사라진다 — 두 워드의 비트 15 가 같은 방향으로 뒤집히면 +0x8000 +0x8000 = 0x10000 ≡ 0 이라 못 잡는다(실험의 sum8 비트 7 과 같은 현상). 1의 보수는 0x10000 → 1 로 접으니 잡는다. 비트 위치가 순환적으로 대칭이 된다. (2) **바이트 순서 독립**: mod 2^16 − 1 이라 워드의 바이트 교환 = 8비트 회전이 덧셈과 교환한다. LE CPU 는 그냥 읽어 더하고 결과만 스왑(RFC 1071 §2(B), 연습 5). (3) **증분 갱신**이 깔끔하다(RFC 1624). 대가는 ±0: 0x0000 과 0xFFFF 워드를 구분 못 한다(실험 0%).

</details>

<details><summary>Q2. 받는 쪽은 어떻게 검증하나요? 왜 결과가 0 이 되나요?</summary>

체크섬 칸 C = ~S 를 포함해 다시 더하면 S + ~S = 0xFFFF (−0). 그 보수는 0 이므로 `inet_checksum(헤더 전체) == 0` 이면 OK. 송신·수신이 같은 함수를 쓴다. Intel HEX 는 2의 보수라 "전부 더해 mod 256 이 0". 주의: UDP 는 계산 결과가 0 이면 0xFFFF 로 보낸다 — 칸의 0 은 "체크섬 안 씀"이라는 뜻이라서다(RFC 768, IPv4 에서만 허용; IPv6 에선 UDP 체크섬이 원칙적으로 필수 — RFC 8200, 터널용 예외는 RFC 6935). 연습 4.

</details>

<details><summary>Q3. 체크섬과 CRC 중 언제 무엇을 쓰나요?</summary>

CRC 가 검출력은 확실히 위다: 버스트 길이 ≤ 차수(16) 는 전부, 짧은 메시지에서 여러 비트 오류도 보장한다(ALGO-M01). 덧셈 체크섬은 위 표처럼 구조적 맹점이 있다. 체크섬이 살아남는 이유는 **비용과 증분성**: 테이블 없이 바이트당 덧셈 1~2개, 필드 하나 바뀌면 O(1) 갱신, 소프트웨어로 쉽게. 그래서 층을 나눈다 — 링크마다 하드웨어 CRC(Ethernet FCS CRC-32), 종단 간(end-to-end)엔 소프트웨어 체크섬(IP/UDP/TCP). Stone & Partridge(SIGCOMM 2000)는 링크 CRC 를 통과했는데 TCP 체크섬에서 걸리는 오류가 실제 네트워크에 꽤 있음을 보였다 — 라우터·호스트 메모리/버스 오류는 링크 CRC 가 못 본다. 펌웨어에서: 플래시 이미지·부트로더 = CRC-32 이상(BM-A04), 짧은 UART/센서 프레임 = CRC-8/16 이 기본, 레거시 프로토콜이 정한 경우만 체크섬.

</details>

<details><summary>Q4. Cortex-M0 에서 Fletcher-16 을 빠르게 하려면? 꼬리: 블록 크기는 어떻게 정하죠?</summary>

M0 는 나눗셈 명령이 없어 `% 255` 가 `bl __aeabi_uidivmod` 가 된다(이 레슨에서 asm 확인, 순진한 루프는 바이트당 2번). 32비트 누산기에 모았다가 5802 바이트마다 한 번 환원하고, 환원도 `%` 대신 255 접기(256 ≡ 1)로 한다 → 안쪽 루프는 `ldrb/adds/adds`. 블록 크기는 최악(바이트 255, 시작값 254)에서 s2 의 상한 254 + 254k + 255·k(k+1)/2 ≤ 2^32 − 1 을 푼다 → 5802. 같은 계산으로 Fletcher-32(워드 65535, 시작값 65534)는 360 워드, 위키백과 구현은 여유를 두고 359 를 쓴다. Adler-32 는 zlib 이 NMAX = 5552 를 쓴다. Cortex-M3/M4 는 `UDIV` 가 있고, 상수 나머지는 컴파일러가 `umull` 역수 곱셈으로 바꾼다(이 레슨에서 thumbv7em asm 확인).

</details>

<details><summary>Q5. 0x00 과 0xFF 를 구분 못 하는 게 실제로 문제가 되나요?</summary>

된다. 지워진 NOR/NAND 플래시는 0xFF, 초기화된 RAM·끊긴 버스는 0x00 이 흔하다. "블록이 전부 0x00 이어야 하는데 0xFF 로 읽힘(지워진 페이지)" 같은 오류를 Fletcher-16 은 못 잡는다(실험 0%). 1의 보수 합도 0x0000/0xFFFF 워드를 구분 못 한다. SSD 펌웨어 관점에서 말하면: erase 상태와 zero-fill 을 구분해야 하는 메타데이터에 이런 체크섬을 쓰면 안 된다. 그래서 플래시 레코드엔 CRC 와 함께 "전부 0xFF 인지" 를 따로 검사하기도 한다(STOR-B03, torn write).

</details>

<details><summary>Q6. UART 수신 ISR 에서 바이트마다 체크섬을 갱신해도 되나요? 인터넷 체크섬은 조각이 홀수 길이면?</summary>

sum/xor/Fletcher 는 상태 1~2개로 바이트마다 O(1) 갱신 → ISR 에서 해도 프레임 끝에 바로 판정할 수 있다. 상태는 채널(프레임)마다 따로 두고, 메인 루프가 읽는다면 프레임 완료 플래그 이후에만 읽는다(공유 데이터 규칙은 C-M14). 인터넷 체크섬을 조각(DMA 청크, mbuf 체인)마다 따로 계산할 땐, 조각이 **홀수 오프셋에서 시작**하면 그 조각의 부분합을 바이트 스왑해서 더한다 — 바이트 순서 독립성 덕분이다(Q1). 나눗셈이 있는 Fletcher 를 ISR 에서 매 바이트 `%` 로 돌리는 건 M0 에선 피하고, 지연 환원(바이트 수 카운트 후 5802 마다 접기)으로.

</details>

<details><summary>Q7. Fletcher 와 Adler-32 의 차이는? 왜 Adler 는 65521 인가요?</summary>

Adler-32(zlib, RFC 1950)는 Fletcher-32 와 같은 두 합 구조인데 (1) 바이트 단위, (2) A 가 1 에서 시작(빈 입력 = 1, 앞쪽 0 바이트가 B 에 반영됨), (3) 법이 65535 대신 소수 65521. 소수라 더 강할 것 같지만, 짧은 메시지에선 합이 법(65521)까지 차오르지 못해 값이 고르게 퍼지지 않는다 — SCTP 가 Adler-32 를 CRC32c 로 바꾼 RFC 3309 의 이유가 바로 "작은 패킷에서 약함" 이었다. Maxino & Koopman(IEEE TDSC 2009)의 평가에서는 같은 비트 수면 Fletcher 가 Adler 보다 대체로 낫고, 둘 다 좋은 CRC 보다 약하다 (세부 수치는 논문 확인). 그리고 65521 은 2^k − 1 이 아니라 접기를 못 쓰고 진짜 `%` 가 필요하다 — zlib 이 블록(NMAX 5552)으로 나머지 횟수를 줄이는 이유. 연습 3.

</details>

## 9. 추가 연습

### 연습 1 — NMEA `*hh` 검사

GPS 모듈의 NMEA 0183 문장 `$GPGLL,4916.45,N,12311.12,W,225444,A,*1D` 는 `$` 와 `*` 사이 문자들의 XOR 를 16진 두 자리로 붙인다. `bool nmea_star_ok(const char *s)` 를 짜라. `$`·`*` 없음, 한 자리, 소문자 16진도 처리. 그리고 XOR 의 약점을 테스트로 보여라. (문장 전체를 상태 기계로 파싱하는 건 ALGO-M12.)

<details><summary>답 보기</summary>

```c
uint8_t x = 0;
const char *q = s + 1;
for (; *q && *q != '*'; q++) x ^= (uint8_t)*q;
if (*q != '*') return false;
int hi = hex1(q[1]), lo = hi < 0 ? -1 : hex1(q[2]);   /* hi 가 '\0' 이면 q[2] 를 읽지 않는다 */
return hi >= 0 && lo >= 0 && x == (uint8_t)(hi << 4 | lo);
```

약점: `4916.45` 를 `4961.45` 로 바꿔도(두 문자 교환) 체크섬 `*1D` 가 그대로 맞는다 — 위도가 틀린 문장이 통과한다. XOR·합은 순서를 못 본다. 두 번째 예 `$GPGGA,…*47` 도 이 레슨에서 직접 계산해 확인했다.

</details>

### 연습 2 — Fletcher-16 체크 바이트

메시지 뒤에 두 바이트 c0, c1 을 붙여 **전체의 Fletcher-16 이 0** 이 되게 하라 (받는 쪽은 "0 인가"만 보면 된다). "abcde" 의 체크 바이트는?

<details><summary>답 보기</summary>

f0 = s1, f1 = s2 라 하면 c0 = 255 − ((f0 + f1) mod 255), c1 = 255 − ((f0 + c0) mod 255). 확인: c0 를 더하면 s1 = f0 + c0 ≡ −f1, s2 = f1 + (f0 + c0) ≡ 0. c1 을 더하면 s1 ≡ −f1 + c1 ≡ 0 (c1 ≡ −(f0 + c0) ≡ f1), s2 ≡ 0 + 0 = 0. 즉 c1 ≡ f1 이다. "abcde": f0 = 0xF0 = 240, f1 = 0xC8 = 200 → c0 = 255 − 185 = 70 = 0x46, c1 = 255 − 55 = 200 = 0xC8. 랜덤 2000개(전부 0 인 메시지 포함 — 이때 c0 = c1 = 0xFF)로 확인.

</details>

### 연습 3 — Adler-32

`uint32_t adler32(const uint8_t *p, size_t n)`: A = 1 + Σb, B = Σ A_i (둘 다 mod 65521), 결과 B << 16 | A. 위키백과 예 "Wikipedia" → 0x11E60398. 5552 바이트 블록으로 `%` 를 줄여라.

<details><summary>답 보기</summary>

```c
uint32_t a = 1, b = 0;
while (n) {
    size_t blk = n < 5552u ? n : 5552u;
    n -= blk;
    do { a += *p++; b += a; } while (--blk);
    a %= 65521u; b %= 65521u;
}
return b << 16 | a;
```

기준은 닫힌 식 A = 1 + Σb_i, B = n + Σ(n − i)b_i (A 의 초기값 1 이 n 번 B 에 들어간다). 블록 경계(5552의 배수 ± 몇)와 전부 0xFF 를 포함한 300개로 대조. "Wikipedia" 값은 이 레슨에서 Python `zlib.adler32` 와 직접 루프 두 방법으로 재현했다.

</details>

### 연습 4 — UDP/IPv4 체크섬과 의사 헤더

UDP 체크섬은 UDP 헤더 + 데이터 앞에 **의사 헤더**(pseudo-header: 출발/목적 IPv4 주소, 0, 프로토콜 17, UDP 길이)를 붙여 계산한다. 의사 헤더를 실제로 복사하지 말고 합에만 넣는 `udp4_checksum(src, dst, udp, len)` 을 짜라. 결과가 0 이면 0xFFFF 로.

<details><summary>답 보기</summary>

```c
uint32_t s = (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + 17u + (uint32_t)len;
s = ones_sum(udp, len, s);                   /* 접지 않은 BE 워드 합에 이어서 */
uint16_t c = (uint16_t)~fold16(s);
return c == 0 ? 0xFFFF : c;
```

1의 보수 합은 순서·묶음과 무관하므로 의사 헤더를 따로 더해도 된다. 테스트: 의사 헤더를 진짜로 앞에 붙인 버퍼의 `inet_checksum` 과 2000번 대조 + 수신 측 검증(칸 채운 뒤 전체 = 0). 결과가 0 인 패킷은 "마지막 데이터 워드 = (그 워드를 0 으로 둔) 체크섬" 으로 일부러 만들어 0xFFFF 가 나오는지 확인한다. 이더넷/IP/UDP 전체 파서는 COMM-A04.

</details>

### 연습 5 — 리틀 엔디언으로 읽고 마지막에만 스왑

LE MCU 에서 `uint16_t` 로 그냥 읽어 더한 뒤 결과만 바이트 스왑해도 RFC 1071 결과와 같음을 코드로 보여라. 홀수 길이에서 끝 바이트는 어디에?

<details><summary>답 보기</summary>

LE 워드 = `p[1] << 8 | p[0]`, 홀수 끝 바이트는 **하위**(`s += p[n-1]`) — BE 에서 상위였던 것을 바이트 스왑한 위치. 결과 `c` 를 `c << 8 | c >> 8` 로 바꾼다. 이유: 워드마다 바이트를 바꾸는 것 = ×2^8 (mod 65535) 이고, 이것이 합·접기·보수와 모두 교환한다 (보수: ~x = 0xFFFF − x, 회전해도 0xFFFF 는 그대로). 랜덤 3000개(길이 0~256)로 확인. RFC 1071 §2(B) 가 바로 이 방법(호스트 순서로 더하고 끝에서 한 번 스왑)을 권한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed
```

## 10. 핵심 정리

- sum8 = 바이트 합 mod 256, xor8 = 비트 열 패리티 8개, 2의 보수 체크섬(Intel HEX) = "전부 더해 0".
- RFC 1071: BE 16비트 워드, 홀수 끝 = 상위 바이트, `while (sum >> 16)` 로 접기(end-around carry = mod 65535), 마지막 `~`. 칸 포함 재계산 = 0. ±0 때문에 0x0000/0xFFFF 워드 구분 불가.
- Fletcher: s1 += b, s2 += s1 → s2 = Σ(n − i)b_i 라 **순서가 보인다**. mod 255 라 0x00 ≡ 0xFF. Fletcher-32 test vector 는 LE 워드.
- 증분 갱신은 RFC 1624 식 3 `~(~HC + ~m + m')`. 2의 보수로 하면 틀린다(실측 44892/200000).
- M0: `% 255` = `bl __aeabi_uidivmod`. 32비트 누산기에 5802 바이트까지 모으고, 256 ≡ 1 로 4번 접어 환원.
- 실험(32바이트): 1비트는 전부 잡음. 2비트 xor8 87.8% · sum8 93.1% · inet16 97.1% · Fletcher 100%. 교환: sum/xor 0%, inet 은 바이트 교환 잡고 워드 교환 0%. 앞에 0 붙이기는 덧셈 체크섬 전부 0% → 길이 검사 필수.
- 체크섬은 싸고 증분 가능, CRC 는 검출 보장 — 링크엔 CRC, 종단 간엔 체크섬, 새 설계면 CRC(ALGO-M01/M02).

## 참고자료

- RFC 1071, "Computing the Internet Checksum" (1988) — §2 성질(바이트 순서 독립, 증분, 지연 접기), §3 수치 예제(00 01 f2 03 f4 f5 f6 f7 → 합 ddf2): <https://www.rfc-editor.org/rfc/rfc1071>
- RFC 1624, "Computation of the Internet Checksum via Incremental Update" (1994) — 식 3 과 식 2 의 ±0 문제: <https://www.rfc-editor.org/rfc/rfc1624>
- RFC 791 (IPv4 헤더 체크섬), RFC 768 (UDP: 의사 헤더, 0 = 체크섬 없음), RFC 8200 (IPv6: 헤더 체크섬 없음, UDP 체크섬 필수)
- J. G. Fletcher, "An Arithmetic Checksum for Serial Transmissions", *IEEE Trans. Communications* 30(1), 1982 — Fletcher 체크섬 원 논문.
- Wikipedia, "Fletcher's checksum" — test vector("abcde" → 0xC8F0, Fletcher-32 LE 워드 → 0xF04FC729 등), 블록 크기 5802/359 구현: <https://en.wikipedia.org/wiki/Fletcher%27s_checksum> (이 레슨의 벡터 6개는 루프·닫힌 식 두 방법으로 재현)
- Wikipedia, "Intel HEX" — 레코드 형식과 체크섬, 예 `:0B0010006164647265737320676170A7`: <https://en.wikipedia.org/wiki/Intel_HEX>
- RFC 1950, "ZLIB Compressed Data Format" — Adler-32 정의 (연습 3); zlib `adler32.c` 의 NMAX 5552.
- RFC 2328 (OSPFv2) §12.1.7 — LSA 체크섬에 Fletcher 체크섬 사용 (LS age 필드 제외).
- RFC 3309 — SCTP 체크섬을 Adler-32 에서 CRC32c 로 변경 (짧은 패킷에서 Adler-32 가 약함).
- T. Maxino, P. Koopman, "The Effectiveness of Checksums for Embedded Control Networks", *IEEE Trans. Dependable and Secure Computing* 6(1), 2009 — XOR/합/1의 보수/Fletcher/Adler/CRC 비교. 권수·호 서지는 기억에 의존 — 확인 권장.
- J. Stone, C. Partridge, "When the CRC and TCP Checksum Disagree", ACM SIGCOMM 2000.
- NMEA 0183 체크섬(`*hh` = `$`~`*` 사이 XOR) — 예 문장 `$GPGLL,…*1D` 는 직접 계산해 확인.
- ARM, *ARMv6-M Architecture Reference Manual* — 나눗셈 명령 없음(Cortex-M0/M0+). 이 레슨의 asm 은 `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -O2 -S` / `thumbv7em … cortex-m4` 실제 출력.
- 관련 레슨: ALGO-M01 (CRC 원리), ALGO-M02 (CRC 테이블/slicing), ALGO-M12 (NMEA 파서), ALGO-B13 (패리티), NET-B02 (인터넷 체크섬 심화·RFC 1624), NETHW-B07 (체크섬 오프로드), COMM-B08 (체크섬 vs CRC 검출 실험), COMM-B10 (길이 prefix + 체크섬 프레임), COMM-A04 (Ethernet/IPv4/UDP 파서), DRONE-M07 (UBX 체크섬), STOR-B03 (레코드 체크섬·torn write), BM-A04 (이미지 CRC), C-M05 (엔디언 독립 직렬화), C-M14 (ISR 공유 데이터)

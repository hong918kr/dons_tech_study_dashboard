---
id: COMM-B08
time: 60분
updated: 2026-11-11
---
# COMM-B08 오류 검출 능력 실험: 체크섬 vs CRC

## 학습 목표

- 검출 실험의 기준을 세운다: 코드워드(codeword) = 데이터 ‖ 검사값, 오류는 검사값까지 포함한 **와이어 비트**에 넣고, "바뀌었는데 수신 측 검사를 통과" = 미검출(undetected)로 센다.
- sum8 · Fletcher-16 · CRC-8 · CRC-16 · CRC-32 에 랜덤 1~4비트 오류와 길이 L = 1…40 버스트(burst) 오류를 수백만 번 넣어 미검출률을 재고, **이론값**(가중치 분포 A_k / C(N,k), 버스트 2^-(r-1) · 2^-r)과 숫자로 맞춘다.
- CRC 가 GF(2) 위에서 선형이라는 점을 써서 **신드롬(syndrome) 표**를 만들고, 2·3비트 전수 탐색으로 경험적 해밍 거리(Hamming distance, HD)와 "HD 가 떨어지는 길이"(CRC-8 0x07: 119비트, CRC-32: 268 / 2974비트)를 직접 찾는다.
- **와이어 비트 순서와 CRC 반사(reflection)의 짝**이 버스트 보장을 좌우함을 보인다 (UART LSB-first 위의 CRC-8/SMBUS 는 8비트 버스트도 놓친다).
- 요구사항(프레임 길이, 필요한 HD, 최대 버스트, 버스 비트 순서)에서 가장 짧은 CRC 를 고르는 함수를 짜고, 면접에서 "왜 이 CRC 인가"를 수치로 말한다.

## 1. 문제

"프레임 끝에 체크섬 하나 붙이면 되지 않나요?" — 펌웨어 면접과 보드 bring-up 에서 프로토콜을 설계할 때 반드시 나오는 질문이고, 답은 "어떤 오류를 얼마나 놓치는지 숫자로" 해야 한다.

Don 이 RF 모듈 AT 커맨드 링크나 SSD 펌웨어 UART 콘솔에서 본 것처럼, 실제 선 위의 오류는 랜덤 1비트보다 **몰려서**(EMI 스파이크, 클럭 어긋남 → 버스트) 온다. 그리고 검출 코드는 세 가지 다른 이유로 놓친다.

1. **체크섬(sum, Fletcher)** — 덧셈이라 서로 상쇄되는 변화(+2^k, −2^k)를 못 본다. 놓치는 확률이 **데이터에 따라** 달라진다 (ALGO-B14 에서 맹점 네 가지를 봤다).
2. **CRC** — 오류 다항식이 생성 다항식 g(x) 의 배수일 때만 놓친다. 데이터와 **무관**하고, 길이 ≤ r 인 버스트는 하나도 놓치지 않는다. 다만 HD 는 **프레임 길이**에 따라 떨어진다.
3. **비트 순서** — CRC 의 "버스트 ≤ r 보장"은 와이어에 나가는 순서와 CRC 레지스터에 들어가는 순서가 같을 때만 성립한다.

이 레슨은 세 가지를 모두 실험으로 확인한다. CRC 알고리즘 자체(다항식 나눗셈, 테이블)는 ALGO-M01 · ALGO-M02 몫이므로 여기서는 비트 단위 엔진 하나만 짧게 짠다.

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">코드워드 = 데이터 0x41 || CRC-8/SMBUS 0xC0, 와이어 MSB-first — 오류를 넣는 세 가지 방법</text>
  <g fill="currentColor" font-size="12"><text x="168" y="44">0</text><text x="202" y="44">1</text><text x="236" y="44">2</text><text x="270" y="44">3</text><text x="304" y="44">4</text><text x="338" y="44">5</text><text x="372" y="44">6</text><text x="406" y="44">7</text><text x="440" y="44">8</text><text x="474" y="44">9</text><text x="504" y="44">10</text><text x="538" y="44">11</text><text x="572" y="44">12</text><text x="606" y="44">13</text><text x="640" y="44">14</text><text x="674" y="44">15</text></g>
  <text x="10" y="44" fill="currentColor" font-size="12">와이어 비트 번호 →</text>
  <g><rect x="160" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="194" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="228" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="262" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="296" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="330" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="364" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="398" y="50" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="432" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="466" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="500" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="534" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="568" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="602" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="636" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="670" y="50" width="32" height="22" class="f-acc" stroke="currentColor"/></g>
  <g fill="currentColor" font-family="monospace"><text x="172" y="66">0</text><text x="206" y="66">1</text><text x="240" y="66">0</text><text x="274" y="66">0</text><text x="308" y="66">0</text><text x="342" y="66">0</text><text x="376" y="66">0</text><text x="410" y="66">1</text><text x="444" y="66">1</text><text x="478" y="66">1</text><text x="512" y="66">0</text><text x="546" y="66">0</text><text x="580" y="66">0</text><text x="614" y="66">0</text><text x="648" y="66">0</text><text x="682" y="66">0</text></g>
  <text x="10" y="66" fill="currentColor">보낸 코드워드</text>
  <text x="250" y="90" fill="currentColor" font-size="12">데이터 0x41</text><text x="512" y="90" fill="currentColor" font-size="12">검사값 CRC-8 = 0xC0</text>
  <text x="10" y="120" fill="currentColor">(a) 랜덤 k=2 비트</text>
  <g><rect x="160" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="194" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="228" y="104" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="262" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="296" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="330" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="364" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="398" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="432" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="466" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="500" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="534" y="104" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="568" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="602" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="636" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="670" y="104" width="32" height="22" class="f-mut" stroke="currentColor"/></g>
  <g fill="currentColor" font-family="monospace"><text x="240" y="120">1</text><text x="546" y="120">1</text></g>
  <text x="10" y="172" fill="currentColor">(b) 버스트 L = 6</text>
  <g><rect x="160" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="194" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="228" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="262" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="296" y="156" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="330" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="364" y="156" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="398" y="156" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="432" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="466" y="156" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="500" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="534" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="568" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="602" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="636" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="670" y="156" width="32" height="22" class="f-mut" stroke="currentColor"/></g>
  <g fill="currentColor" font-family="monospace"><text x="308" y="172">1</text><text x="376" y="172">1</text><text x="410" y="172">1</text><text x="478" y="172">1</text></g>
  <path d="M296 184 L296 189 L498 189 L498 184" fill="none" stroke="currentColor"/>
  <text x="357" y="201" fill="currentColor" font-size="12">양 끝 1, 사이 랜덤</text>
  <text x="10" y="224" fill="currentColor">(c) e = g 모양</text>
  <g><rect x="160" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="194" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="228" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="262" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="296" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="330" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="364" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="398" y="208" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="432" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="466" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="500" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="534" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="568" y="208" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="602" y="208" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="636" y="208" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="670" y="208" width="32" height="22" class="f-warn" stroke="currentColor"/></g>
  <g fill="currentColor" font-family="monospace"><text x="410" y="224">1</text><text x="614" y="224">1</text><text x="648" y="224">1</text><text x="682" y="224">1</text></g>
  <path d="M398 236 L398 241 L702 241 L702 236" fill="none" stroke="currentColor"/>
  <text x="510" y="253" fill="currentColor" font-size="12">L = 9 = r+1</text>
  <text x="10" y="276" fill="currentColor">(c) 받은 값 40 C7</text>
  <g><rect x="160" y="260" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="194" y="260" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="228" y="260" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="262" y="260" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="296" y="260" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="330" y="260" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="364" y="260" width="32" height="22" class="f-mut" stroke="currentColor"/><rect x="398" y="260" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="432" y="260" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="466" y="260" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="500" y="260" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="534" y="260" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="568" y="260" width="32" height="22" class="f-acc" stroke="currentColor"/><rect x="602" y="260" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="636" y="260" width="32" height="22" class="f-warn" stroke="currentColor"/><rect x="670" y="260" width="32" height="22" class="f-warn" stroke="currentColor"/></g>
  <g fill="currentColor" font-family="monospace"><text x="172" y="276">0</text><text x="206" y="276">1</text><text x="240" y="276">0</text><text x="274" y="276">0</text><text x="308" y="276">0</text><text x="342" y="276">0</text><text x="376" y="276">0</text><text x="410" y="276">0</text><text x="444" y="276">1</text><text x="478" y="276">1</text><text x="512" y="276">0</text><text x="546" y="276">0</text><text x="580" y="276">0</text><text x="614" y="276">1</text><text x="648" y="276">1</text><text x="682" y="276">1</text></g>
  <text x="10" y="312" fill="currentColor">수신 측: CRC-8(0x40) = 0xC7 = 받은 검사값 → 통과. 그런데 4비트가 바뀌었다 = 미검출 오류.</text>
  <text x="10" y="332" fill="currentColor">오류 1 0000 0111 = x^8 + x^2 + x + 1 = g(x) 자체. CRC 는 오류가 g 의 배수일 때만 놓친다 — 데이터와 무관.</text>
</svg>
```

### Part A — 검출 코드 다섯 개와 코드워드

- 파라미터형 비트 단위 CRC 엔진 `crc_compute(param, data, n)` 하나로 CRC-8/SMBUS(0x07), CRC-16/CCITT-FALSE(0x1021, init 0xFFFF), CRC-32/ISO-HDLC(0x04C11DB7 반사형)를 계산한다. RevEng 카탈로그 check 값("123456789" → 0xF4, 0x29B1, 0xCBF43926)과 맞추고, 레지스터 트릭 없이 정의대로 하는 "비트 배열 긴 나눗셈" 기준 구현과 랜덤 대조한다. sum8 · Fletcher-16 은 ALGO-B14 축약판을 그대로 쓴다.
- `cw_encode(alg, data, n, out, cap)`: 데이터 뒤에 검사값을 붙인다. 반사형 CRC-32 만 리틀 엔디언(LE), 나머지는 빅 엔디언(BE). 용량이 모자라면 −1 을 돌려주고 아무것도 쓰지 않는다. 제자리(out == data) 인코딩도 된다.
- `cw_residual(alg, cw, len)`: 수신 측 계산. (데이터로 다시 계산한 검사값) XOR (받은 검사값). 0 이면 통과로 본다.

### Part B — 오류 주입, 미검출률, 신드롬과 HD

- `inject_bits` (서로 다른 와이어 비트 k 개) / `inject_burst` (길이 L: **첫 비트와 끝 비트는 반드시**, 사이 L−2 비트는 동전 던지기). 와이어 비트 번호는 `flip_wire(buf, i, order)` 로 MSB-first 또는 LSB-first 를 따른다.
- `count_undetected(alg, ndata, kind, param, order, trials, seed)`: 랜덤 데이터 → 인코드 → 주입 → 검증. 고정 시드 xorshift32 라 매번 같은 숫자가 나온다.
- `crc_syndromes`: 0 데이터 코드워드에서 비트 하나씩 뒤집어 잔차를 기록한 표. `min_undetected_weight(syn, n, maxk)`: 1·2·3비트 전수로 "가장 가벼운 미검출 오류"(= 경험적 HD)를 찾는다.
- 수백만 회 실험은 `demo_detect.c`, HD 가 떨어지는 길이는 `demo_hd.c`. 실제 출력을 5절에 표로 붙였다.

### Part C — 비트 순서와 코드 선택 (프로토콜 설계)

- `burst_undetected_exhaustive(syn, n, L)`: 길이 L 버스트를 **모든 위치 × 사이 비트 모든 조합**으로 전수 검사한다. 그레이 코드(Gray code)로 패턴당 XOR 한 번이면 된다.
- 같은 CRC 를 MSB-first(SPI, I2C) 와이어와 LSB-first(UART) 와이어에 놓고 비교한다. CRC-8/SMBUS 오류 패턴 `D8 06` 이 UART 에서는 길이 8 버스트인데도 통과하는 것을 보인다.
- `choose_check(ndata, need_hd, burst_max, order)`: 요구사항을 **전수 검증으로** 만족하는 가장 짧은 CRC 를 돌려준다. 체크섬은 보장이 데이터에 따라 달라 이 방법으로 증명할 수 없으므로 후보에서 뺀다.

## 2. Problem (English)

You are designing the integrity check for a small sensor/telemetry link. Measure, don't guess.

```c
typedef enum { ALG_SUM8, ALG_FL16, ALG_CRC8, ALG_CRC16, ALG_CRC32, ALG_COUNT } alg_t;
typedef enum { ORD_MSB, ORD_LSB } order_t;          /* bit order on the wire, within a byte */
typedef struct { unsigned width; uint32_t poly, init; bool refin, refout; uint32_t xorout; } crc_param;

uint32_t crc_compute(const crc_param *p, const uint8_t *d, size_t n);           /* bitwise, width 8..32 */
long     cw_encode(alg_t a, const uint8_t *data, size_t n, uint8_t *out, size_t cap); /* -1 if cap too small */
uint32_t cw_residual(alg_t a, const uint8_t *cw, size_t len);                   /* 0 == accepted */
void     inject_bits (uint8_t *buf, unsigned nbits, unsigned k, order_t o, uint32_t *rng);
void     inject_burst(uint8_t *buf, unsigned nbits, unsigned L, order_t o, uint32_t *rng);
uint32_t count_undetected(alg_t a, size_t ndata, err_kind kind, unsigned param, order_t o,
                          uint32_t trials, uint32_t seed);
int      crc_syndromes(alg_t a, size_t ndata, order_t o, uint32_t *syn, size_t cap);
int      min_undetected_weight(const uint32_t *syn, int nbits, int maxk);       /* maxk <= 3 */
uint64_t burst_undetected_exhaustive(const uint32_t *syn, int nbits, int L);    /* L <= 24 */
alg_t    choose_check(size_t ndata, int need_hd, int burst_max, order_t o);
```

Constraints: no heap; every encoder takes an output capacity; tests are deterministic (fixed-seed xorshift32); the self-test runs in under 2 s. A "burst of length L" has its first and last bits flipped, and the L−2 bits between are random.

Examples:

```text
CRC-8/SMBUS("123456789") = 0xF4   CRC-16/CCITT-FALSE = 0x29B1   CRC-32/ISO-HDLC = 0xCBF43926
cw_encode(CRC16, "123456789")  -> ... 0x29 0xB1          (big-endian)
cw_encode(CRC32, "123456789")  -> ... 0x26 0x39 0xF4 0xCB (little-endian, reflected CRC)
'A' with CRC-8: 41 C0.  Flip wire bits 7,13,14,15 -> 40 C7 -> residual 0 -> undetected (error == g(x))
choose_check(8 bytes, HD 4, bursts <= 8, MSB-first) -> CRC8
choose_check(8 bytes, HD 4, bursts <= 8, LSB-first) -> CRC16
```

Follow-ups: Why does a degree-r CRC catch every burst ≤ r, and what fraction of longer bursts does it miss? Why does CRC-8 lose HD past 119 data bits? Why do UART protocols use reflected CRCs? When is a checksum good enough?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데다 (Part A 3개, Part B 5개, Part C 2개). `reflect`, `sum8`, `fletcher16`, `check_value`, `flip_wire`, 기준 구현 `ref_crc`, 하네스는 주어진다. 스텁은 멈추거나 죽지 않게 안전한 틀린 값을 돌려준다 (`count_undetected` 는 "전부 놓침", 신드롬은 −1, 버스트 전수는 `UINT64_MAX`). 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:187  crc_compute(&CRC8_SMBUS, s9, 9) == 0xF4u
FAIL starter.c:188  crc_compute(&CRC16_CCITT_FALSE, s9, 9) == 0x29B1u
…
crc_compute vs long division: 900 cases, mismatches=897
…
FAIL starter.c:353  choose_check(32, 3, 8, ORD_MSB) == ALG_CRC16
FAIL starter.c:354  choose_check(8, 4, 17, ORD_MSB) == ALG_CRC32
14/45 passed
```

(exit 1. 통과한 14개는 스텁의 값이 우연히 맞은 경우다 — 예: sum8 · Fletcher 테스트 벡터, 용량 부족 → −1, "sum8 은 2비트를 놓친다(> 0)".)

## 4. 힌트

<details><summary>힌트 1 — CRC 엔진은 레지스터 하나</summary>

MSB-first 레지스터 c(width 비트)에 init 을 넣고, 바이트마다 `c ^= byte << (width − 8)` 한 다음 8번 "맨 위 비트가 1 이면 `(c << 1) ^ poly`, 아니면 `c << 1`" 을 하고 width 비트로 자른다. refin 이면 바이트를 먼저 뒤집고, refout 이면 끝에서 레지스터를 width 비트로 뒤집은 뒤 xorout 을 XOR 한다. width = 32 일 때 `1u << 32` 는 UB 다 — 마스크는 따로 만든다.

</details>

<details><summary>힌트 2 — "미검출" 의 정확한 정의</summary>

`memcmp(rx, cw) != 0 && cw_verify(rx)`. 바뀌지 않았는데 통과한 것(오류 0개)은 미검출이 아니다. 주입기가 같은 비트를 두 번 뒤집으면 오류가 사라지므로 `inject_bits` 는 위치가 겹치지 않게 다시 뽑는다.

</details>

<details><summary>힌트 3 — 신드롬 표: 왜 비트 하나씩이면 충분한가</summary>

CRC 는 init · xorout 을 빼면 GF(2) 위의 선형 함수다. 잔차 R(코드워드) 는 정상 코드워드에서 0 이고, 오류 e 를 XOR 하면 R(cw ⊕ e) = R(cw) ⊕ L(e) = L(e) 다. L 은 선형이므로 L(e) = (뒤집은 비트들의 L(e_i)) 의 XOR. 0 데이터 코드워드에서 비트 i 하나만 뒤집은 잔차가 곧 syn[i] 다. sum8 · Fletcher 는 덧셈의 올림(carry) 때문에 선형이 아니다 → 신드롬 표가 없다 (그래서 −1).

</details>

<details><summary>힌트 4 — 버스트 전수는 그레이 코드</summary>

시작 s 에서 x = syn[s] ^ syn[s+L−1] 로 두고, 사이 m = L−2 비트의 모든 조합 2^m 개를 그레이 코드 순서로 돈다: g = 1, 2, …, 2^m − 1 단계에서 뒤집히는 비트는 `__builtin_ctz(g)` 번째다. 그때마다 `x ^= syn[s + 1 + ctz(g)]`, x == 0 이면 미검출 하나. 패턴당 O(1) 이다.

</details>

<details><summary>힌트 5 — choose_check 의 순서</summary>

검사값이 짧은 순서(CRC-8 → CRC-16 → CRC-32)로, (1) `crc_syndromes(ndata, order)`, (2) `min_undetected_weight(syn, n, need_hd − 1) > need_hd − 1` 이면 HD 조건 통과, (3) L = 1 … burst_max 버스트 전수가 모두 0 이면 채택. 처음 통과한 것을 돌려준다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **모델을 먼저 정한다.** "프레임 = 데이터 ‖ 검사값, 오류는 검사값까지 포함한 선 위 비트에. 수신 측이 다시 계산해 다르면 버린다. 미검출 = 바뀌었는데 통과."
2. **오류 모델을 둘로 나눈다.** 독립 비트 오류(k비트) → HD 가 말해 준다. 몰린 오류(버스트) → 검사값 비트 수 r 가 말해 준다.
3. **CRC 는 데이터와 무관, 체크섬은 데이터에 의존.** 그래서 CRC 는 표로 "보장"을 말할 수 있고 체크섬은 "평균"만 말할 수 있다.
4. **길이와 비트 순서**를 확인한다. HD 는 프레임이 길어지면 떨어지고, 버스트 보장은 비트 순서가 맞아야 성립한다.
5. 숫자로 고른다: "36 바이트 프레임, UART, 버스트 ≤ 16 → 반사형 CRC-16 이나 CRC-32."

### Part A — CRC 엔진과 코드워드

세 CRC 의 파라미터 (Rocksoft/RevEng 모델, check 값은 이 레슨 코드로 재현):

| 이름 | width | poly | init | refin/refout | xorout | check("123456789") | 쓰는 곳 (대표값) |
|---|---|---|---|---|---|---|---|
| CRC-8/SMBUS | 8 | 0x07 | 0x00 | 아니오 | 0x00 | 0xF4 | SMBus PEC, I2C 센서 |
| CRC-16/CCITT-FALSE (= IBM-3740) | 16 | 0x1021 | 0xFFFF | 아니오 | 0x0000 | 0x29B1 | 여러 임베디드 프레임 |
| CRC-32/ISO-HDLC | 32 | 0x04C11DB7 | 0xFFFFFFFF | 예 | 0xFFFFFFFF | 0xCBF43926 | Ethernet, zip, PNG |

**한 바이트 추적.** CRC-8/SMBUS 로 '1'(0x31) 하나를 계산한다. init 0 이라 c = 0x31 에서 시작한다.

| 단계 | 맨 위 비트 | `c << 1` (8비트) | poly XOR? | 결과 c |
|---|---|---|---|---|
| 시작 | — | — | — | 0x31 |
| 1 | 0 | 0x62 | 아니오 | 0x62 |
| 2 | 0 | 0xC4 | 아니오 | 0xC4 |
| 3 | 1 | 0x88 | 0x88 ^ 0x07 | 0x8F |
| 4 | 1 | 0x1E | 0x1E ^ 0x07 | 0x19 |
| 5 | 0 | 0x32 | 아니오 | 0x32 |
| 6 | 0 | 0x64 | 아니오 | 0x64 |
| 7 | 0 | 0xC8 | 아니오 | 0xC8 |
| 8 | 1 | 0x90 | 0x90 ^ 0x07 | **0x97** |

**검사값의 바이트 순서.** CRC-16(비반사)은 상위 바이트를 먼저, CRC-32(반사형)는 하위 바이트를 먼저 붙인다. 이렇게 하면 데이터의 마지막 비트 바로 뒤에 검사값의 최고차 비트가 오므로 코드워드 전체가 하나의 연속된 다항식이 되고, 수신 측에서 "데이터 ‖ CRC 전체의 CRC" 가 상수가 된다 (실측: CRC-8 · CRC-16 은 0x00 / 0x0000, CRC-32/ISO-HDLC 는 0x2144DF1C — xorout 전 레지스터 값으로는 0xDEBB20E3 이다). 순서를 거꾸로 붙이면 데이터와 CRC 경계에 걸친 버스트를 놓친다 (7절 실수 3).

**잔차(residual)를 TODO 로 둔 이유.** `cw_verify` 는 "잔차 == 0" 한 줄이지만, 잔차를 값으로 돌려받으면 Part B 의 신드롬 표를 같은 함수로 만들 수 있다. 하드웨어 CRC 유닛(COMM-B09)도 결국 이 값을 레지스터로 보여 준다.

### Part B — 랜덤 k비트 오류: 실측 vs 이론

`demo_detect.c` 실제 출력 (32 B 데이터, 칸마다 1,000,000 회, N = 코드워드 비트 수):

```text
alg            N        k=1        k=2        k=3        k=4
sum8         264          0      68787      11486      15355
fletcher16   272          0          0        241        108
crc8         264          0       4233          0       7969
crc16        272          0          0          0         24
crc32        288          0          0          0          0

CRC theory (exact): expected count = 1e6 * A_k / C(N,k)
alg            N          A_2          A_3          A_4 |     E[k=2]     E[k=3]     E[k=4]
crc8         264          147            0      1553256 |     4234.4        0.0     7851.5
crc16        272            0            0         6587 |        0.0        0.0       29.5
crc32        288            0            0            0 |        0.0        0.0        0.0
```

**CRC 이론값은 근사가 아니라 정확한 값이다.** CRC 의 미검출은 데이터와 무관하므로, 길이 N 코드에서 "XOR 하면 0 이 되는 비트 k 개 집합"의 개수 A_k (가중치 분포)를 세면 랜덤 k비트 오류의 미검출 확률은 정확히 A_k / C(N,k) 다. `demo_detect` 는 A_2 · A_3 를 신드롬 전수로, A_4 는 "쌍 XOR 값이 같은 쌍의 쌍"을 정렬해 센다 (4비트 집합 하나는 쌍 나누기 3가지로 세 번 세어지고, 인덱스가 겹치는 경우는 2비트 미검출이므로 A_2·(N−2) 를 뺀다).

| 칸 | 실측 | 이론 | 왜 |
|---|---|---|---|
| 모든 코드, k = 1 | 0 | 0 | 비트 하나의 신드롬은 0 이 아니다 (x^i 는 g 로 안 나눠짐; 합은 ±2^k ≢ 0) |
| sum8, k = 2 | 68787 (6.88 %) | 68439 (6.844 %) | 같은 비트 열(column)·다른 바이트일 확률 8·C(33,2)/C(264,2) = 0.1217, 그중 상쇄 확률 9/16 (비트 0~6 은 방향이 맞을 때 1/2, 비트 7 은 ±128 둘이 항상 256 ≡ 0) → 0.0684. 차이 +1.4σ |
| Fletcher-16, k = 2 | 0 | — | 상쇄하려면 Δs1 ≡ 0 이고 Δs2 ≡ 0 (mod 255) 이어야 하는데, 같은 열 두 비트는 Δs2 = ±2^a·(바이트 거리) 라 거리가 255 의 배수(≥ 255 바이트)여야 한다 |
| Fletcher-16, k = 3 | 241 | — | 예: 비트 열 a 의 0→1 두 개(+2^a, +2^a)와 **그 가운데 바이트**의 비트 a+1 1→0(−2^(a+1)). Δs1 = 0, Δs2 = 2^a·(i + j − 2k) = 0. 데이터 값에 달린 확률이다 |
| CRC-8, k = 2 | 4233 | 4234.4 | 0x07 의 주기(period)가 127 → 거리가 127 의 배수인 비트 쌍은 미검출. 264비트에서 (264−127) + (264−254) = **147** 쌍 (연습 3 의 닫힌 식) |
| CRC-8 · CRC-16, k = 3 | 0 | 0 | g(1) = 0 (항 개수 짝수) → (x+1) 이 g 를 나눈다 → **홀수 개 비트 오류는 전부 검출** |
| CRC-8, k = 4 | 7969 | 7851.5 | g 자체(4항)가 가중치 4 코드워드. 8비트 검사값이면 랜덤 짝수 오류의 약 2^-7 을 놓친다 (+1.3σ) |
| CRC-16, k = 4 | 24 | 29.5 | A_4 = 6587. HD = 4 라 4비트부터 일부 놓친다 |
| CRC-32, k ≤ 4 | 0 | 0 | 256 데이터 비트는 "HD ≥ 6 구간"(≤ 268비트, 아래) 안 → 5비트 이하 오류는 **보장**으로 전부 검출 |

### Part B — 버스트: 왜 "r 이하는 전부, r+1 은 2^-(r-1), 그 이상은 2^-r" 인가

길이 L 버스트 오류는 다항식으로 e(x) = x^i · b(x), deg b = L−1, b 의 최고차·최저차 계수가 1 이다. g 의 상수항이 1 이면 g 와 x^i 는 서로소이므로 **g | e ⇔ g | b**.

- L ≤ r: deg b < r = deg g 이고 b ≠ 0 → g 로 나눠질 수 없다 → **전부 검출**.
- L = r+1: deg b = r → g | b ⇔ b = g. 사이 L−2 = r−1 비트의 조합 2^(r−1) 개 중 정확히 1개 → **2^-(r−1)**.
- L > r+1: b = q·g, q 는 차수 L−1−r 이고 양 끝 계수 1 → 2^(L−r−2) 개. 비율 2^(L−r−2) / 2^(L−2) = **2^-r**.

`demo_detect.c` 실제 출력에서 몇 줄 (L 마다 250,000 회; th = 이론 기대 횟수):

```text
  L       sum8 fletcher16       crc8      crc16      crc32 |   th crc8  th crc16  th crc32
  1          0          0          0          0          0 |      0.00      0.00         0
  7          0          0          0          0          0 |      0.00      0.00         0
  8          0          4          0          0          0 |      0.00      0.00         0
  9       1969          0       1975          0          0 |   1953.12      0.00         0
 10       1030          0       1027          0          0 |    976.56      0.00         0
 16        960          0       1019          0          0 |    976.56      0.00         0
 17        922         10        901         12          0 |    976.56      7.63         0
 18        950          2        977          3          0 |    976.56      3.81         0
 32       1064          7        977          4          0 |    976.56      3.81         0
 33        948          3        931          7          0 |    976.56      3.81   0.00012
 40        973          2        998          3          0 |    976.56      3.81   5.8e-05

crc16, L=18..40 summed: 100 undetected of 5750000 trials (theory 2^-16 -> 87.7)
crc32, L=18..40 summed: 0 undetected of 5750000 trials (theory 2^-32 -> 0.0013)
```

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">버스트 길이 L 별 미검출률 — demo_detect.c 실측 (32 B 데이터, L 마다 250,000 회, 로그 눈금)</text>
  <line x1="62" y1="34" x2="62" y2="290" stroke="currentColor"/>
  <line x1="62" y1="290" x2="720" y2="290" stroke="currentColor"/>
  <line x1="58" y1="40" x2="720" y2="40" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="20" y="44" fill="currentColor">1e-2</text>
  <line x1="58" y1="100" x2="720" y2="100" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="20" y="104" fill="currentColor">1e-3</text>
  <line x1="58" y1="160" x2="720" y2="160" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="20" y="164" fill="currentColor">1e-4</text>
  <line x1="58" y1="220" x2="720" y2="220" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="20" y="224" fill="currentColor">1e-5</text>
  <line x1="58" y1="280" x2="720" y2="280" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="20" y="284" fill="currentColor">1e-6</text>
  <text x="6" y="316" fill="currentColor">미검출 0</text>
  <line x1="62" y1="312" x2="720" y2="312" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="64" y="336" fill="currentColor">1</text>
  <text x="180" y="336" fill="currentColor">8</text>
  <text x="196" y="336" fill="currentColor">9</text>
  <text x="312" y="336" fill="currentColor">16</text>
  <text x="328" y="336" fill="currentColor">17</text>
  <text x="444" y="336" fill="currentColor">24</text>
  <text x="576" y="336" fill="currentColor">32</text>
  <text x="592" y="336" fill="currentColor">33</text>
  <text x="708" y="336" fill="currentColor">40</text>
  <text x="640" y="354" fill="currentColor">L (비트)</text>
  <line x1="196" y1="46.4" x2="208" y2="46.4" stroke="currentColor" stroke-dasharray="5 4"/>
  <line x1="212" y1="64.5" x2="720" y2="64.5" stroke="currentColor" stroke-dasharray="5 4"/>
  <line x1="328" y1="190.9" x2="340" y2="190.9" stroke="currentColor" stroke-dasharray="5 4"/>
  <line x1="344" y1="209.0" x2="720" y2="209.0" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="482" y="54" fill="currentColor">점선 = 이론 2^-8 (CRC-8, L ≥ 10)</text>
  <text x="450" y="274" fill="currentColor">점선 = 이론 2^-16 (CRC-16, L ≥ 18)</text>
  <line x1="194" y1="34" x2="194" y2="318" stroke="currentColor" stroke-opacity="0.35" stroke-dasharray="1 3"/>
  <text x="196" y="100" fill="currentColor">r=8</text>
  <line x1="326" y1="34" x2="326" y2="318" stroke="currentColor" stroke-opacity="0.35" stroke-dasharray="1 3"/>
  <text x="328" y="100" fill="currentColor">r=16</text>
  <line x1="590" y1="34" x2="590" y2="318" stroke="currentColor" stroke-opacity="0.35" stroke-dasharray="1 3"/>
  <text x="592" y="100" fill="currentColor">r=32</text>
  <g><circle cx="200.5" cy="46.2" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="217.0" cy="63.1" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="233.5" cy="64.2" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="250.0" cy="64.2" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="266.5" cy="64.0" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="283.0" cy="63.7" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="299.5" cy="64.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="316.0" cy="64.9" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="332.5" cy="66.0" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="349.0" cy="65.2" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="365.5" cy="64.4" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="382.0" cy="64.1" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="398.5" cy="63.9" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="415.0" cy="64.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="431.5" cy="65.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="448.0" cy="65.1" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="464.5" cy="64.5" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="481.0" cy="65.4" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="497.5" cy="65.6" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="514.0" cy="64.7" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="530.5" cy="64.0" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="547.0" cy="64.6" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="563.5" cy="63.6" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="580.0" cy="62.3" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="596.5" cy="65.3" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="613.0" cy="65.6" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="629.5" cy="63.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="646.0" cy="66.5" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="662.5" cy="65.0" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="679.0" cy="63.0" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="695.5" cy="65.3" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="712.0" cy="64.6" r="3.5" class="f-warn" stroke="currentColor"/></g>
  <g><rect x="200.5" y="43.1" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="217.0" y="60.2" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="233.5" y="62.1" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="250.0" y="61.1" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="266.5" y="62.1" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="283.0" y="60.0" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="299.5" y="61.1" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="316.0" y="60.4" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="332.5" y="63.6" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="349.0" y="61.5" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="365.5" y="62.2" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="382.0" y="61.6" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="398.5" y="61.7" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="415.0" y="61.2" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="431.5" y="59.9" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="448.0" y="60.7" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="464.5" y="61.6" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="481.0" y="60.7" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="497.5" y="62.4" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="514.0" y="60.7" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="530.5" y="61.7" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="547.0" y="59.8" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="563.5" y="60.4" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="580.0" y="61.5" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="596.5" y="62.7" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="613.0" y="61.8" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="629.5" y="63.0" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="646.0" y="61.9" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="662.5" y="60.5" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="679.0" y="61.1" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="695.5" y="63.1" width="6" height="6" class="f-acc" stroke="currentColor"/><rect x="712.0" y="60.9" width="6" height="6" class="f-acc" stroke="currentColor"/></g>
  <g><path d="M184.0 203.8 L188.0 210.8 L180.0 210.8 Z" class="f-mut" stroke="currentColor"/><path d="M332.5 179.9 L336.5 186.9 L328.5 186.9 Z" class="f-mut" stroke="currentColor"/><path d="M349.0 221.8 L353.0 228.8 L345.0 228.8 Z" class="f-mut" stroke="currentColor"/><path d="M365.5 197.9 L369.5 204.9 L361.5 204.9 Z" class="f-mut" stroke="currentColor"/><path d="M382.0 203.8 L386.0 210.8 L378.0 210.8 Z" class="f-mut" stroke="currentColor"/><path d="M398.5 197.9 L402.5 204.9 L394.5 204.9 Z" class="f-mut" stroke="currentColor"/><path d="M415.0 185.7 L419.0 192.7 L411.0 192.7 Z" class="f-mut" stroke="currentColor"/><path d="M431.5 203.8 L435.5 210.8 L427.5 210.8 Z" class="f-mut" stroke="currentColor"/><path d="M448.0 203.8 L452.0 210.8 L444.0 210.8 Z" class="f-mut" stroke="currentColor"/><path d="M464.5 203.8 L468.5 210.8 L460.5 210.8 Z" class="f-mut" stroke="currentColor"/><path d="M481.0 221.8 L485.0 228.8 L477.0 228.8 Z" class="f-mut" stroke="currentColor"/><path d="M497.5 193.2 L501.5 200.2 L493.5 200.2 Z" class="f-mut" stroke="currentColor"/><path d="M514.0 197.9 L518.0 204.9 L510.0 204.9 Z" class="f-mut" stroke="currentColor"/><path d="M530.5 221.8 L534.5 228.8 L526.5 228.8 Z" class="f-mut" stroke="currentColor"/><path d="M547.0 221.8 L551.0 228.8 L543.0 228.8 Z" class="f-mut" stroke="currentColor"/><path d="M563.5 211.2 L567.5 218.2 L559.5 218.2 Z" class="f-mut" stroke="currentColor"/><path d="M580.0 189.2 L584.0 196.2 L576.0 196.2 Z" class="f-mut" stroke="currentColor"/><path d="M596.5 211.2 L600.5 218.2 L592.5 218.2 Z" class="f-mut" stroke="currentColor"/><path d="M613.0 211.2 L617.0 218.2 L609.0 218.2 Z" class="f-mut" stroke="currentColor"/><path d="M629.5 197.9 L633.5 204.9 L625.5 204.9 Z" class="f-mut" stroke="currentColor"/><path d="M646.0 211.2 L650.0 218.2 L642.0 218.2 Z" class="f-mut" stroke="currentColor"/><path d="M662.5 221.8 L666.5 228.8 L658.5 228.8 Z" class="f-mut" stroke="currentColor"/><path d="M679.0 182.6 L683.0 189.6 L675.0 189.6 Z" class="f-mut" stroke="currentColor"/><path d="M695.5 189.2 L699.5 196.2 L691.5 196.2 Z" class="f-mut" stroke="currentColor"/><path d="M712.0 221.8 L716.0 228.8 L708.0 228.8 Z" class="f-mut" stroke="currentColor"/></g>
  <g><path d="M335.5 174.6 L340.0 179.1 L335.5 183.6 L331.0 179.1 Z" class="f-ok" stroke="currentColor"/><path d="M352.0 210.7 L356.5 215.2 L352.0 219.7 L347.5 215.2 Z" class="f-ok" stroke="currentColor"/><path d="M368.5 197.4 L373.0 201.9 L368.5 206.4 L364.0 201.9 Z" class="f-ok" stroke="currentColor"/><path d="M385.0 192.7 L389.5 197.2 L385.0 201.7 L380.5 197.2 Z" class="f-ok" stroke="currentColor"/><path d="M401.5 203.3 L406.0 207.8 L401.5 212.3 L397.0 207.8 Z" class="f-ok" stroke="currentColor"/><path d="M418.0 210.7 L422.5 215.2 L418.0 219.7 L413.5 215.2 Z" class="f-ok" stroke="currentColor"/><path d="M434.5 221.3 L439.0 225.8 L434.5 230.3 L430.0 225.8 Z" class="f-ok" stroke="currentColor"/><path d="M451.0 197.4 L455.5 201.9 L451.0 206.4 L446.5 201.9 Z" class="f-ok" stroke="currentColor"/><path d="M467.5 210.7 L472.0 215.2 L467.5 219.7 L463.0 215.2 Z" class="f-ok" stroke="currentColor"/><path d="M484.0 239.4 L488.5 243.9 L484.0 248.4 L479.5 243.9 Z" class="f-ok" stroke="currentColor"/><path d="M500.5 221.3 L505.0 225.8 L500.5 230.3 L496.0 225.8 Z" class="f-ok" stroke="currentColor"/><path d="M517.0 197.4 L521.5 201.9 L517.0 206.4 L512.5 201.9 Z" class="f-ok" stroke="currentColor"/><path d="M533.5 197.4 L538.0 201.9 L533.5 206.4 L529.0 201.9 Z" class="f-ok" stroke="currentColor"/><path d="M550.0 188.7 L554.5 193.2 L550.0 197.7 L545.5 193.2 Z" class="f-ok" stroke="currentColor"/><path d="M566.5 203.3 L571.0 207.8 L566.5 212.3 L562.0 207.8 Z" class="f-ok" stroke="currentColor"/><path d="M583.0 203.3 L587.5 207.8 L583.0 212.3 L578.5 207.8 Z" class="f-ok" stroke="currentColor"/><path d="M599.5 188.7 L604.0 193.2 L599.5 197.7 L595.0 193.2 Z" class="f-ok" stroke="currentColor"/><path d="M616.0 192.7 L620.5 197.2 L616.0 201.7 L611.5 197.2 Z" class="f-ok" stroke="currentColor"/><path d="M632.5 221.3 L637.0 225.8 L632.5 230.3 L628.0 225.8 Z" class="f-ok" stroke="currentColor"/><path d="M649.0 197.4 L653.5 201.9 L649.0 206.4 L644.5 201.9 Z" class="f-ok" stroke="currentColor"/><path d="M665.5 203.3 L670.0 207.8 L665.5 212.3 L661.0 207.8 Z" class="f-ok" stroke="currentColor"/><path d="M682.0 192.7 L686.5 197.2 L682.0 201.7 L677.5 197.2 Z" class="f-ok" stroke="currentColor"/><path d="M698.5 185.2 L703.0 189.7 L698.5 194.2 L694.0 189.7 Z" class="f-ok" stroke="currentColor"/><path d="M715.0 210.7 L719.5 215.2 L715.0 219.7 L710.5 215.2 Z" class="f-ok" stroke="currentColor"/></g>
  <g><circle cx="70.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="86.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="103.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="119.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="136.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="152.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="169.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="185.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="202.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="218.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="235.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="251.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="268.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="284.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="301.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="317.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="334.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="350.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="367.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="383.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="400.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="416.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="433.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="449.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="466.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="482.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="499.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="515.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="532.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="548.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="565.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="581.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="598.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="614.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="631.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="647.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="664.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="680.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="697.0" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="713.5" cy="312.0" r="3.5" class="f-ok" stroke="currentColor"/></g>
  <text x="70" y="305" fill="currentColor">L ≤ 7: 다섯 개 모두 0</text>
  <text x="334" y="305" fill="currentColor">○ CRC-32: L = 1..40 전부 0 (이론: L ≥ 34 에서 2^-32)</text>
  <circle cx="480.0" cy="124.0" r="3.5" class="f-warn" stroke="currentColor"/><text x="490" y="128" fill="currentColor">sum8</text>
  <rect x="477.0" y="138.0" width="6" height="6" class="f-acc" stroke="currentColor"/><text x="490" y="145" fill="currentColor">CRC-8 (0x07)</text>
  <path d="M480.0 154.0 L484.0 161.0 L476.0 161.0 Z" class="f-mut" stroke="currentColor"/><text x="490" y="162" fill="currentColor">Fletcher-16</text>
  <path d="M480.0 170.5 L484.5 175.0 L480.0 179.5 L475.5 175.0 Z" class="f-ok" stroke="currentColor"/><text x="490" y="179" fill="currentColor">CRC-16/CCITT-FALSE</text>
</svg>
```

읽는 법:

- CRC 는 경계가 칼 같다: CRC-8 은 L ≤ 8 에서 0, L = 9 에서 1975 (이론 1953, 2^-7), L ≥ 10 에서 ≈ 977 (2^-8). CRC-16 은 L ≤ 16 에서 0, L = 17 에서 12 (이론 7.6, +1.6σ), L ≥ 18 합계 100 (이론 87.7). CRC-32 는 40 까지 0 이고, 이론으로도 L ≥ 34 에서 2^-32 (약 43억 회에 한 번) 라 이 실험 규모로는 0 이 정상이다.
- **sum8 이 버스트에서는 CRC-8 과 거의 겹친다.** MSB-first 와이어에서 길이 ≤ 8 버스트는 두 바이트에 걸치는데, 앞 바이트의 변화는 하위 a 비트 안(|Δ| < 2^a, 0 아님)이고 뒤 바이트의 변화는 2^a 의 배수라 합이 256 의 배수가 될 수 없다. 그래서 sum8 도 "8비트 이하 버스트는 전부" 잡는다. 하지만 랜덤 2비트는 6.9 % 를 놓친다 — **HD 가 2** 라는 게 CRC-8 과의 차이다.
- Fletcher-16 은 L = 8 에서 4번 놓쳤다. 바이트 경계에 딱 맞은 8비트 버스트가 0x00 ↔ 0xFF 를 만든 경우다 (mod 255 에서 0 ≡ 255, ALGO-B14 맹점 ③). 기대값 ≈ 250000 × (1/8 정렬) × (2^-6 사이 비트 전부 1) × (2/256 데이터가 00 또는 FF) ≈ 3.8.

### Part B — 신드롬 표와 경험적 HD

데이터 1 바이트짜리 CRC-8 코드워드(16비트, MSB-first)의 신드롬 표 (`crc_syndromes` 실제 값 — 0 데이터로 만들지만 데이터와 무관하므로 'A' 코드워드 41 C0 에도 그대로 쓴다):

| 와이어 비트 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| syn | 89 | C7 | E0 | 70 | 38 | 1C | 0E | 07 | 80 | 40 | 20 | 10 | 08 | 04 | 02 | 01 |

- 검사값 칸(8~15)의 신드롬은 그 비트 자신이다 (받은 검사값만 바뀌므로).
- 데이터 칸은 x^(끝에서의 거리) mod g: 비트 7(데이터 마지막)은 x^8 mod g = x^2 + x + 1 = 0x07.
- 그림 1 의 (c): syn[7] ^ syn[13] ^ syn[14] ^ syn[15] = 07 ^ 04 ^ 02 ^ 01 = **0** → 미검출. 어떤 데이터에서도 그렇다.

`min_undetected_weight` 는 이 표에서 1비트(syn == 0), 2비트(같은 값 두 개), 3비트(syn[i] ^ syn[j] == syn[k]) 순서로 전수한다. 3비트는 C(N,3) 이라 N = 288 이면 약 390만 번이다. 테스트 결과: CRC-8 은 14 바이트(120비트)에서 "3비트까지 없음 → HD ≥ 4", 15 바이트(128비트)에서 **2** (주기 127 을 넘었다).

길이를 늘리며 HD 가 떨어지는 지점은 `demo_hd.c` 가 찾는다. 길이 N 을 1비트씩 늘리며, 새 비트 n 의 신드롬 S_n 으로 "S_n ^ S_a 가 이미 본 쌍 XOR 와 같은가"(4비트), "S_n ^ S_a ^ S_b 가 쌍 XOR 와 같은가"(5비트)를 해시로 확인한다. 실제 출력:

```text
CRC-8   poly 0x07        g(1) = 0 (x+1 divides g: every odd-weight error caught)
  2-bit undetected   first at codeword    128 bits (data    120 bits)
  4-bit undetected   first at codeword      9 bits (data      1 bits)
CRC-16  poly 0x1021      g(1) = 0 (x+1 divides g: every odd-weight error caught)
  2-bit undetected   first at codeword  32768 bits (data  32752 bits)
  4-bit undetected   first at codeword     17 bits (data      1 bits)
CRC-32  poly 0x04C11DB7  g(1) = 1 (odd weights possible)
  2-bit undetected   none up to codeword  40000 bits
  3-bit undetected   none up to codeword   4000 bits
  4-bit undetected   first at codeword   3007 bits (data   2975 bits)
  5-bit undetected   first at codeword    301 bits (data    269 bits)
```

해석 ("데이터 비트 ≤ X 까지 HD = d" 로 바꾸면 Koopman 의 CRC 표 표기와 같다):

| CRC | 직접 찾은 경계 | 결론 | 문헌 (Koopman, 확인 권장) |
|---|---|---|---|
| CRC-8 0x07 | 2비트 미검출이 데이터 120비트부터 | HD 4: 데이터 ≤ 119비트 (≈ 14 바이트), 그 위는 HD 2 | 0x83 (Koopman 표기) HD 4 → 119 와 일치 |
| CRC-16 0x1021 | 2비트 미검출이 32752비트부터 | HD 4: 데이터 ≤ 32751비트 (≈ 4 KB) | HD 4 → 32751 과 일치 |
| CRC-32 0x04C11DB7 | 5비트 미검출이 269비트, 4비트가 2975비트부터 | HD 6: ≤ 268비트, HD 5: ≤ 2974비트 | 268 / 2974 일치. HD 4 ≤ 91607, HD 3 ≤ 4294967263 은 문헌값 (3비트 탐색은 O(N^2) 라 생략) |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">데이터 길이에 따른 해밍 거리 (HD) — 경계는 demo_hd.c 로 직접 찾음 (회색 = 문헌값 인용)</text>
  <text x="10" y="60" fill="currentColor">CRC-8 0x07</text>
  <text x="10" y="110" fill="currentColor">CRC-16 0x1021</text>
  <text x="10" y="160" fill="currentColor">CRC-32 0x04C11DB7</text>
  <rect x="130.0" y="40" width="120.4" height="30" class="f-ok" stroke="currentColor"/>
  <text x="134.0" y="60" fill="currentColor">HD 4</text>
  <rect x="250.4" y="40" width="459.6" height="30" class="f-warn" stroke="currentColor"/>
  <text x="274.4" y="60" fill="currentColor">HD 2 (120 비트부터)</text>
  <rect x="130.0" y="90" width="261.9" height="30" class="f-ok" stroke="currentColor"/>
  <text x="134.0" y="110" fill="currentColor">HD 4</text>
  <rect x="391.9" y="90" width="318.1" height="30" class="f-warn" stroke="currentColor"/>
  <text x="395.9" y="110" fill="currentColor">HD 2 (32752 비트부터)</text>
  <rect x="130.0" y="140" width="140.8" height="30" class="f-ok" stroke="currentColor"/>
  <text x="134.0" y="160" fill="currentColor">HD ≥6</text>
  <rect x="270.8" y="140" width="60.6" height="30" class="f-ok" stroke="currentColor"/>
  <text x="274.8" y="160" fill="currentColor">HD 5</text>
  <rect x="331.5" y="140" width="86.3" height="30" class="f-mut" stroke="currentColor"/>
  <text x="335.5" y="160" fill="currentColor">HD 4</text>
  <rect x="417.8" y="140" width="270.9" height="30" class="f-mut" stroke="currentColor"/>
  <text x="421.8" y="160" fill="currentColor">HD 3</text>
  <rect x="688.7" y="140" width="21.3" height="30" class="f-warn" stroke="currentColor"/>
  <text x="679" y="132" fill="currentColor" font-size="12">2</text>
  <line x1="130" y1="186" x2="710" y2="186" stroke="currentColor"/>
  <line x1="130.0" y1="186" x2="130.0" y2="191" stroke="currentColor"/><text x="122.0" y="205" fill="currentColor">1</text>
  <line x1="188.0" y1="186" x2="188.0" y2="191" stroke="currentColor"/><text x="180.0" y="205" fill="currentColor">10</text>
  <line x1="246.0" y1="186" x2="246.0" y2="191" stroke="currentColor"/><text x="238.0" y="205" fill="currentColor">100</text>
  <line x1="304.0" y1="186" x2="304.0" y2="191" stroke="currentColor"/><text x="296.0" y="205" fill="currentColor">1k</text>
  <line x1="362.0" y1="186" x2="362.0" y2="191" stroke="currentColor"/><text x="354.0" y="205" fill="currentColor">10k</text>
  <line x1="420.0" y1="186" x2="420.0" y2="191" stroke="currentColor"/><text x="412.0" y="205" fill="currentColor">100k</text>
  <line x1="478.0" y1="186" x2="478.0" y2="191" stroke="currentColor"/><text x="470.0" y="205" fill="currentColor">1M</text>
  <line x1="536.0" y1="186" x2="536.0" y2="191" stroke="currentColor"/><text x="528.0" y="205" fill="currentColor">10M</text>
  <line x1="594.0" y1="186" x2="594.0" y2="191" stroke="currentColor"/><text x="586.0" y="205" fill="currentColor">100M</text>
  <line x1="652.0" y1="186" x2="652.0" y2="191" stroke="currentColor"/><text x="644.0" y="205" fill="currentColor">1G</text>
  <line x1="710.0" y1="186" x2="710.0" y2="191" stroke="currentColor"/><text x="702.0" y="205" fill="currentColor">10G</text>
  <line x1="269.7" y1="32" x2="269.7" y2="186" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="239.7" y="226" fill="currentColor">↑ 이 레슨의 32 B 프레임 = 256 데이터 비트</text>
  <text x="520" y="226" fill="currentColor">데이터 비트 (로그 눈금)</text>
  <text x="10" y="244" fill="currentColor" font-size="12">CRC-32: 268 / 2974 는 demo_hd 실측, 91607 / 4294967263 은 Koopman 표 (계산량 때문에 생략). HD 2 = 2비트 오류도 놓칠 수 있음.</text>
</svg>
```

이 표가 "**짧은 센서 프레임엔 CRC-8 로 충분한가?**" 에 대한 답이다. 14 바이트까지는 HD 4 (3비트 이하 전부) 이지만, 32 바이트 프레임에 CRC-8 을 쓰면 HD 가 2 로 떨어져 2비트 오류의 0.42 % 를 놓친다.

### Part C — 비트 순서: 버스트 보장은 "순서가 맞을 때만"

증명에서 "버스트 = 연속된 다항식 계수" 라고 가정했다. 그런데 와이어에서 연속인 비트가 다항식에서도 연속이려면, 바이트 안에서 **먼저 나가는 비트가 CRC 레지스터에 먼저 들어가는 비트**여야 한다.

- 비반사 CRC (CRC-8/SMBUS, CRC-16/CCITT-FALSE): 바이트의 bit7 을 먼저 넣는다 → MSB-first 버스(SPI 대부분, I2C/SMBus)와 짝.
- 반사형 CRC (CRC-32/ISO-HDLC, CRC-16/MODBUS): bit0 을 먼저 넣는다 → LSB-first 버스(UART, Ethernet)와 짝.

짝이 안 맞으면 다항식 위치 p 는 와이어 위치 8·⌊p/8⌋ + 7 − (p mod 8) 로 간다. 와이어 길이 L 버스트가 다항식에선 최대 L + 14 까지 벌어지므로 "≤ r 이면 전부" 보장이 사라진다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">같은 오류 패턴 D8 06, 두 가지 비트 순서로 줄 세우기 (CRC-8/SMBUS)</text>
  <text x="10" y="56" fill="currentColor">다항식 순서</text><text x="10" y="72" fill="currentColor" font-size="12">(MSB-first: I2C·SPI)</text>
  <g><rect x="200" y="46" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="232" y="46" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="264" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="296" y="46" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="328" y="46" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="360" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="392" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="424" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="456" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="488" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="520" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="552" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="584" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="616" y="46" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="648" y="46" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="680" y="46" width="30" height="22" class="f-mut" stroke="currentColor"/></g>
  <g fill="currentColor" font-family="monospace"><text x="212" y="62">1</text><text x="244" y="62">1</text><text x="276" y="62">0</text><text x="308" y="62">1</text><text x="340" y="62">1</text><text x="372" y="62">0</text><text x="404" y="62">0</text><text x="436" y="62">0</text><text x="468" y="62">0</text><text x="500" y="62">0</text><text x="532" y="62">0</text><text x="564" y="62">0</text><text x="596" y="62">0</text><text x="628" y="62">1</text><text x="660" y="62">1</text><text x="692" y="62">0</text></g>
  <path d="M200 74 L200 80 L678 80 L678 74" fill="none" stroke="currentColor"/>
  <text x="200" y="96" fill="currentColor" font-size="12">길이 15 &gt; r = 8 → "r 이하 버스트" 보장 밖. 오류 다항식 = g 의 배수라 미검출</text>
  <text x="10" y="136" fill="currentColor">UART 와이어 순서</text><text x="10" y="152" fill="currentColor" font-size="12">(LSB-first)</text>
  <g><rect x="200" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="232" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="264" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="296" y="126" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="328" y="126" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="360" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="392" y="126" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="424" y="126" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="456" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="488" y="126" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="520" y="126" width="30" height="22" class="f-warn" stroke="currentColor"/><rect x="552" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="584" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="616" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="648" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/><rect x="680" y="126" width="30" height="22" class="f-mut" stroke="currentColor"/></g>
  <g fill="currentColor" font-family="monospace"><text x="212" y="142">0</text><text x="244" y="142">0</text><text x="276" y="142">0</text><text x="308" y="142">1</text><text x="340" y="142">1</text><text x="372" y="142">0</text><text x="404" y="142">1</text><text x="436" y="142">1</text><text x="468" y="142">0</text><text x="500" y="142">1</text><text x="532" y="142">1</text><text x="564" y="142">0</text><text x="596" y="142">0</text><text x="628" y="142">0</text><text x="660" y="142">0</text><text x="692" y="142">0</text></g>
  <path d="M296 154 L296 160 L550 160 L550 154" fill="none" stroke="currentColor"/>
  <text x="200" y="176" fill="currentColor" font-size="12">와이어에선 비트 3..10 = 길이 8 ≤ r 버스트 → 그런데 통과! (선 위의 EMI 한 번이 이 모양이면 끝)</text>
  <g fill="currentColor" font-size="12"><text x="210" y="40">0</text><text x="242" y="40">1</text><text x="274" y="40">2</text><text x="306" y="40">3</text><text x="338" y="40">4</text><text x="370" y="40">5</text><text x="402" y="40">6</text><text x="434" y="40">7</text><text x="466" y="40">8</text><text x="498" y="40">9</text><text x="526" y="40">10</text><text x="558" y="40">11</text><text x="590" y="40">12</text><text x="622" y="40">13</text><text x="654" y="40">14</text><text x="686" y="40">15</text></g>
  <text x="240" y="122" fill="currentColor" font-size="12">byte0 = D8 (bit0 먼저)</text><text x="496" y="122" fill="currentColor" font-size="12">byte1 = 06</text>
  <line x1="10" y1="192" x2="730" y2="192" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="214" fill="currentColor">가장 짧은 미검출 버스트 (비트 순서 맞음 → 안 맞음), demo_hd.c · practice.c 실측:</text>
  <text x="30" y="236" fill="currentColor" font-family="monospace">CRC-8/SMBUS</text><text x="170" y="236" fill="currentColor" font-family="monospace">9 → 8</text>
  <text x="390" y="236" fill="currentColor" font-family="monospace">CRC-16/CCITT</text><text x="530" y="236" fill="currentColor" font-family="monospace">17 → 17</text>
  <text x="30" y="256" fill="currentColor" font-family="monospace">CRC-16/MODBUS</text><text x="170" y="256" fill="currentColor" font-family="monospace">17 → 11</text>
  <text x="390" y="256" fill="currentColor" font-family="monospace">CRC-32</text><text x="530" y="256" fill="currentColor" font-family="monospace">33 → 31</text>
  <text x="10" y="280" fill="currentColor" font-size="12">규칙: 바이트 안에서 먼저 나가는 비트 = CRC 레지스터에 먼저 들어가는 비트 (UART ↔ 반사형, I2C/SPI ↔ 비반사형)</text>
</svg>
```

실측 (`solution.c` Part C 전수, 데이터 8 바이트):

```text
exhaustive bursts (8B data): crc8 L<=8 MSB=0 LSB=16, L=9 MSB=64 | crc16 L<=16 MSB=0 LSB=0, L=17 MSB=64 | crc32 (4B) L<=20 LSB=0 MSB=0
```

- CRC-8/SMBUS: MSB 와이어 L ≤ 8 → 0, UART(LSB) 와이어 → **16개** 놓침. L = 9 = r+1 에서는 시작 위치마다 정확히 1개(= g 자체) → 72 − 9 + 1 = 64.
- CRC-16/CCITT 는 UART 에서도 L ≤ 16 이 0 이다. 운이 좋은 경우다 — 가장 짧은 미검출 와이어 버스트가 17 로, 순서가 맞을 때와 같다 (demo_hd 2부). 반면 같은 16비트라도 **CRC-16/MODBUS 를 MSB-first 와이어에 쓰면 11비트 버스트도 놓친다** (연습 4·5).
- CRC-32 를 MSB-first(SPI) 에 쓰면 길이 31 미검출 버스트가 있다 (`05 8F F4 6A 70` 을 바이트 경계에 XOR, 연습 4). L ≤ 20 전수에서는 안 나오므로 테스트는 0 이다.

"가장 짧은 미검출 와이어 버스트"를 정확히 구하는 방법(연습 4): 와이어 길이 ≤ r 인 미검출 오류는 다항식 길이 ≤ r + 14, 즉 deg e ≤ r + 13 이므로 g 의 배수 q·g (q(0) = 1, deg q ≤ 13 → 2^13 개)를 8가지 바이트 정렬로 놓아 보면 빠짐없이 나온다. 없으면 답은 r+1 이상이다 (r+1 은 g 자체로 항상 존재).

### Part C — 요구사항 → 코드 선택

`choose_check` 는 CRC-8 → CRC-16 → CRC-32 순서로 (1) 신드롬, (2) need_hd−1 비트 이하 전수, (3) 길이 ≤ burst_max 버스트 전수를 돌려 처음 통과한 것을 고른다. 테스트에 넣은 결정들:

| 요구사항 | 결과 | 이유 |
|---|---|---|
| 8 B, HD 4, 버스트 ≤ 8, MSB-first (SPI 센서) | CRC-8 | 72비트 ≤ 127 이라 HD 4, 순서 맞음 |
| 같은 요구, LSB-first (UART) | CRC-16 | CRC-8 이 UART 순서에서 8비트 버스트를 놓침 |
| 32 B, HD 3, 버스트 ≤ 8, MSB | CRC-16 | 264비트 > 주기 127 → CRC-8 은 HD 2 |
| 8 B, HD 4, 버스트 ≤ 17, MSB | CRC-32 | 17비트 버스트는 16비트 검사값으로 보장 불가 (L = r+1 은 2^-15) |
| 65 B (> MAX_DATA) | 없음 (ALG_COUNT) | 용량 밖 — 버퍼 크기도 인터페이스의 일부 |

체크섬을 후보에서 뺀 이유: 신드롬 전수는 "데이터와 무관"할 때만 증명이 된다. sum8 · Fletcher 는 데이터에 따라 놓치는 패턴이 바뀌므로 보장을 말하려면 데이터 전체를 돌려야 한다. 실무에선 이렇게 정리한다: **CPU 가 아주 약하고 상위 계층에 다른 검사가 있으면 Fletcher, 그 외 새 설계는 CRC** (Maxino & Koopman 2009 의 결론과 같은 방향).

### 복잡도와 실행 시간

| 함수 | 시간 | 메모리 |
|---|---|---|
| `crc_compute` | O(8n) 비트 단계 (테이블이면 O(n), ALGO-M02) | O(1) |
| `count_undetected` | O(trials · n) | 스택 버퍼 3개 (≤ 68 B) |
| `crc_syndromes` | O(N · n) — 비트마다 CRC 한 번 | syn[N] (N ≤ 544) |
| `min_undetected_weight` | O(N^3) 최악 (3비트) | O(1) |
| `burst_undetected_exhaustive` | O(N · 2^(L−2)) | O(1) |
| `demo_hd` 4비트 / 5비트 | O(N^2) / O(N^3), 해시 2^23 칸 | 약 72 MB 정적 배열 (실험용, 펌웨어 아님) |

`solution.c` 전체 0.6 초, `demo_detect.c` 약 11 초(k비트 2,000만 + 버스트 5,000만 회), `demo_hd.c` 0.1 초 (M 시리즈 Mac).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 45/45 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_detect.c -o demo_detect && ./demo_detect   # 약 11 초
cc -std=c11 -Wall -Wextra -Werror -O2 demo_hd.c -o demo_hd && ./demo_hd               # 약 0.1 초
```

실제 출력 (`solution`):

```text
crc_compute vs long division: 900 cases, mismatches=0
32B data, 20000 trials each:  k=1..4 undetected
  sum8            0   1402    230    277
  fletcher16      0      0      6      2
  crc8            0     89      0    158
  crc16           0      0      0      1
  crc32           0      0      0      0
crc8 burst L=9: 307 / 40000 undetected (theory 2^-7 -> 312.5)
exhaustive bursts (8B data): crc8 L<=8 MSB=0 LSB=16, L=9 MSB=64 | crc16 L<=16 MSB=0 LSB=0, L=17 MSB=64 | crc32 (4B) L<=20 LSB=0 MSB=0
45/45 passed
```

## 7. 자주 하는 실수

**1. 안 바뀐 것도 "미검출"로 센다.** 주입기가 같은 비트를 두 번 뒤집거나, 버스트를 "L 비트 전부 랜덤"으로 만들면 오류가 0 이 되는 경우가 생긴다. 그대로 세면 모든 코드가 1/2^(L) 정도 놓치는 것처럼 보인다.

```c
if (memcmp(rx, cw, len) != 0 && cw_verify(a, rx, len)) miss++;   /* 바뀌었는데 통과 */
```

**2. 버스트 정의를 느슨하게 한다.** "시작 s 부터 L 비트를 랜덤" 이면 양 끝이 0 일 수 있어 실제 길이가 L 보다 짧다. 그러면 L = r+1 에서 2^-(r−1) 이 아니라 더 낮은 값이 나와 이론과 안 맞는다. 첫 비트와 끝 비트는 **반드시** 뒤집는다.

**3. CRC 바이트 순서를 비트 순서와 반대로 붙인다.** CRC-16/CCITT-FALSE 를 MSB-first 와이어에서 LE 로 붙였을 때 (데이터 8 B, 버스트 전수, 이 레슨 코드로 실측): L = 13, 14, 15, 16 에서 각각 3, 4, 3, 2 개의 미검출 — 데이터/CRC 경계에 걸친 버스트다. 붙이는 순서도 다항식이 이어지게 정한다 (비반사 → BE, 반사형 → LE; 반사형 CRC 를 쓰는 Modbus RTU 가 CRC 하위 바이트를 먼저 보내는 것과 일치).

**4. `1u << 32`.** width = 32 마스크를 `(1u << width) − 1` 로 만들면 UB 다. `width == 32 ? 0xFFFFFFFF : …` 로 나눈다.

**5. 체크섬에 "2^-r" 을 그대로 적용한다.** sum8 은 8비트지만 랜덤 2비트 오류의 6.9 % 를 놓친다 (2^-8 = 0.39 % 의 18 배). 체크섬의 검출률은 오류 모델과 데이터에 따라 다르므로 실험이나 분석 없이 비트 수로 말하면 안 된다.

**6. `rand()` 로 실험한다.** 플랫폼마다 다른 수열이 나와 문서의 숫자를 재현할 수 없다. 시드를 고정한 xorshift32 처럼 직접 정의한 생성기를 쓴다.

**7. CRC-8 을 길이 확인 없이 쓴다.** 0x07 의 HD 4 는 데이터 119비트(약 14 바이트)까지다. 64 바이트 프레임에 CRC-8 을 붙이면 2비트 오류도 놓친다. 프레임 최대 길이를 정했으면 표(Koopman)나 `demo_hd` 로 HD 를 확인한다.

## 8. 면접 Q&A

<details><summary>Q1. 체크섬과 CRC 중 무엇을 쓰겠습니까? 기준은?</summary>

오류 모델과 비용으로 고른다. CRC 는 (1) 데이터와 무관한 보장 — 길이 ≤ r 버스트 전부, 길이에 따라 정해진 HD — 이 있고, (2) 테이블(256 B) 하나면 바이트당 시프트 · XOR · 표 조회 정도라 대부분의 MCU 에서 충분히 싸다. 하드웨어 CRC 유닛도 흔하다 (COMM-B09). 체크섬은 증분 갱신이 쉽고(인터넷 체크섬, RFC 1624) 아주 약한 CPU 에서 싸지만, 상쇄되는 오류를 데이터에 따라 놓친다 (실측 sum8 2비트 6.9 %). 새 링크 프로토콜이면 CRC, 기존 표준(IP 헤더)이나 상위 계층 보조 검사면 체크섬.

</details>

<details><summary>Q2. 해밍 거리(HD)가 뭐고, 왜 프레임 길이에 따라 달라지죠?</summary>

HD = 서로 다른 두 유효 코드워드의 최소 비트 차이 = 가장 가벼운 미검출 오류의 비트 수. HD = d 이면 d−1 비트 이하 오류는 전부 잡는다. CRC 에서 비트 i 의 신드롬은 x^i mod g 이고, 이 값들은 주기적으로 반복된다. 프레임이 길어질수록 "XOR 하면 0 이 되는 적은 수의 조합"이 생기기 쉬워 HD 가 떨어진다. 실측: CRC-8 0x07 은 데이터 119비트까지 HD 4, 120비트부터 HD 2 (주기 127). CRC-32 는 268비트까지 HD 6, 2974비트까지 HD 5. 그래서 "CRC-32 니까 안전" 이 아니라 "이 길이에서 HD 몇" 을 말해야 한다.

</details>

<details><summary>Q3. 차수 r CRC 가 길이 ≤ r 버스트를 전부 잡는 이유를 증명해 보세요. 더 긴 건?</summary>

버스트 e = x^i · b(x), deg b = L−1, b(0) = 1. g 의 상수항이 1 이라 g 와 x^i 는 서로소 → g | e ⇔ g | b. L ≤ r 이면 deg b < deg g 이고 b ≠ 0 이라 나눠지지 않는다. L = r+1 이면 b = g 하나뿐 → 2^-(r−1). L > r+1 이면 b = q·g 의 개수 2^(L−r−2) / 전체 2^(L−2) = 2^-r. 실측 CRC-8: L = 9 에서 1975/250000 (이론 1953), L ≥ 10 에서 ≈ 977 (이론 976.6). 꼬리: 이 증명은 "와이어에서 연속 = 다항식에서 연속" 일 때만 맞다 (Q4).

</details>

<details><summary>Q4. UART 프로토콜(Modbus 등)은 왜 반사형 CRC 를 쓰나요?</summary>

UART 는 바이트의 bit0 을 먼저 보낸다. 반사형 CRC 는 bit0 을 레지스터에 먼저 넣으므로 와이어에서 연속인 비트가 다항식에서도 연속이 된다 → 버스트 보장이 그대로 산다. 비반사 CRC 를 UART 에 쓰면 바이트 안 순서가 뒤집혀 와이어 길이 L 버스트가 다항식 길이 L+14 까지 벌어진다. 실측: CRC-8/SMBUS 를 UART 순서에 쓰면 8비트 버스트 `D8 06` 을 놓친다. CRC-16/MODBUS 를 MSB-first 와이어에 쓰면 11비트 버스트도 놓친다. 하드웨어 쪽 이점도 있다: 직렬 시프트 레지스터로 들어오는 순서대로 바로 계산할 수 있다. 반대로 I2C/SMBus 는 MSB-first 라 PEC 가 비반사 CRC-8 이다.

</details>

<details><summary>Q5. init 과 xorout 은 왜 0 이 아닌 값을 쓰죠? 검출 능력과 관계있나요?</summary>

랜덤 비트 오류에 대한 검출 능력(HD, 버스트)은 g 만으로 정해진다 — init · xorout 은 상수를 더할 뿐이라 신드롬이 같다. 차이는 **길이 · 0 에 관한 오류**다. init = 0 이면 앞에 붙은 0x00 바이트가 CRC 를 바꾸지 못한다 (연습 1: CRC-8/SMBUS 1000/1000 놓침, init 0xFFFF 인 CRC-16 은 0/1000). 수신 측이 "데이터 ‖ CRC 전체의 CRC == 0" 으로 검사하고 xorout = 0 이면, 뒤에 붙은 0 바이트도 결과를 0 으로 유지해 못 본다. xorout ≠ 0 이면 상수가 0 이 아니게 된다 (CRC-32: 0x2144DF1C). 또 전부 0 인 프레임(선이 죽어 0 만 들어옴)이 "CRC 0 = 정상"으로 통과하는 것도 막는다. 그래도 길이는 LEN 필드로 따로 검사하는 게 정석이다 (COMM-B10).

</details>

<details><summary>Q6. CRC-16 과 CRC-32 의 차이를 실제 숫자로 말해 보세요.</summary>

가정을 밝히고 계산한다: UART 115200 8N1 → 초당 11520 바이트, 36 바이트 프레임이면 초당 320 프레임. 노이즈가 심해 프레임 1000개 중 1개가 깨지고, 깨질 때 오류가 HD · 버스트 보장 밖의 "랜덤" 모양이라고 하자 (최악 쪽 가정). 미검출 프레임 비율 ≈ 1e-3 × 2^-r. sum8 (2^-8 처럼 보더라도): 초당 1.25e-3 → 약 13분에 한 번. CRC-16: 초당 4.9e-6 → 약 57시간(2.4일)에 한 번. CRC-32: 초당 7.5e-11 → 약 425년에 한 번. 그래서 펌웨어 업데이트처럼 틀리면 안 되는 데이터는 CRC-32 (이미지 전체는 별도 해시/서명), 짧은 센서 프레임은 길이를 보고 CRC-8/16.

</details>

<details><summary>Q7. UART 수신 ISR 에서 CRC 를 어디서 계산하나요?</summary>

세 가지. (1) ISR 에서 바이트마다 테이블 CRC 갱신 — 상태는 레지스터 값 하나라 O(1), 하지만 ISR 시간이 늘어난다. (2) ISR 은 링 버퍼에 넣기만 하고, 메인 루프의 프레임 파서(COMM-B10)가 프레임이 완성되면 한 번에 계산 — 보통 이게 기본. (3) DMA 로 받고(COMM-M11) 하드웨어 CRC 유닛으로 계산(COMM-B09). 어느 쪽이든 CRC 를 확인하기 전에는 페이로드를 쓰지 않는다. 그리고 실패 카운터(crc_err)를 남겨 링크 품질을 볼 수 있게 한다.

</details>

<details><summary>Q8. 벤치에서 CRC 오류가 쏟아집니다. 무엇부터 보나요?</summary>

비율부터 본다. **100 % 실패**면 설정 불일치다: poly · init · refin/refout · xorout, 검사값 바이트 순서, CRC 가 덮는 범위(SOF 포함 여부). 양쪽으로 "123456789" check 값을 찍어 비교하면 바로 나온다. **가끔 실패**면 물리 계층이다: baud 오차(COMM-B03), 접지 · 케이블 길이 · 종단, 오버런(COMM-B02). 로직 애널라이저로 실패한 프레임을 잡아(HWLAB-M05/M06) 몇 비트가 어떤 모양으로 틀렸는지 본다. 버스트 모양이면 EMI · 클럭, 한 바이트 통째로면 오버런 · 버퍼 처리 버그일 가능성이 높다.

</details>

## 9. 추가 연습

### 연습 1 — 앞에 붙은 0 바이트

랜덤 40 바이트 메시지 앞에 0x00 세 바이트를 붙여도 검사값이 같은지 1000번 세어라: CRC-8/SMBUS(init 0), CRC-16/CCITT-FALSE(init 0xFFFF), sum8, Fletcher-16.

<details><summary>답 보기</summary>

실측: CRC-8 1000/1000, sum8 1000/1000, Fletcher-16 1000/1000 이 같다 (= 못 본다). CRC-16 은 0/1000. init = 0 인 CRC 의 레지스터는 0 을 넣어도 0 이고, 합 계열도 0 을 더해 봐야 그대로다 (Fletcher 는 s1 = 0 동안 s2 도 안 변함). → init ≠ 0 을 쓰거나, 길이를 프레임에 넣어 검사한다.

</details>

### 연습 2 — 주기와 "2비트를 전부 잡는 길이"

x^n ≡ 1 (mod g) 인 가장 작은 n (주기 T)을 구하는 `crc_period` 를 짜고, CRC-8 0x07, CRC-16 0x1021, 0x8005 의 주기와 "데이터 ≤ T − r 비트면 2비트 오류 전부 검출" 을 확인하라. 경계에서 실제로 놓치는 예를 하나 만들어라.

<details><summary>답 보기</summary>

T = 127, 32767, 32767 → 데이터 한계 119, 32751 비트. 비트 i, j 두 개의 오류는 x^i(1 + x^(j−i)) 이고 g ∤ x^i 이므로 g | (1 + x^(j−i)) ⇔ T | (j − i). 코드워드가 T+1 비트 이상이어야 거리 T 쌍이 생긴다. 예: CRC-8, 데이터 15 바이트(코드워드 128비트)에서 와이어 비트 0 과 127 을 뒤집으면 잔차 0. x^8+x^2+x+1 = (x+1)(x^7+x^6+x^5+x^4+x^3+x^2+1) (곱해 보면 x^7~x^3 이 상쇄) 이고, 주기 127 = 2^7 − 1 은 코드로 실측했다 → 7차 인수가 원시(primitive) 다항식이라는 뜻.

</details>

### 연습 3 — A_2 의 닫힌 식

32 바이트 CRC-8 코드워드(N = 264)에서 2비트 미검출 쌍 수를 신드롬 전수와 닫힌 식 Σ_{m≥1} (N − m·T) 로 각각 구하고, 랜덤 2비트 미검출률을 계산하라.

<details><summary>답 보기</summary>

(264 − 127) + (264 − 254) = 137 + 10 = **147**. 전수도 147. 미검출률 147 / C(264,2) = 147 / 34716 = **0.4234 %**. `demo_detect` 의 100만 회 실측 4233 회와 맞는다. CRC-16 64 바이트(544비트 < 32767)는 0.

</details>

### 연습 4 — 순서가 안 맞을 때 가장 짧은 미검출 버스트

다항식 위치 p → 와이어 위치 8·⌊p/8⌋ + 7 − p%8 로 놓고, g 의 배수 e = q·g (q(0) = 1, deg q ≤ 13)를 바이트 정렬 8가지로 놓아 와이어 길이 최솟값을 구하라. CRC-8, CRC-16/0x1021, CRC-16/0x8005, CRC-32. CRC-32 의 경우 실제 바이트 패턴을 뽑아 랜덤 메시지 1000개에 XOR 해 정말 통과하는지 확인하라.

<details><summary>답 보기</summary>

실측: CRC-8 **8**, CRC-16/0x1021 **17**, CRC-16/0x8005 **11**, CRC-32 **31** (순서가 맞으면 각각 9, 17, 17, 33). CRC-32 패턴 = `05 8F F4 6A 70` (MSB-first 와이어에서 길이 31), 바이트 경계 어디에 XOR 해도 1000/1000 통과. 왜 이 탐색이 완전한가: 와이어 길이 Lw 의 오류는 다항식 길이 ≤ Lw + 14 (양 끝 바이트 안에서 최대 7비트씩 벌어짐). Lw ≤ r 이면 deg e ≤ r + 13 → deg q ≤ 13. 그래서 CRC-16/0x1021 의 "17" 은 "16 이하가 하나도 없다" 는 완전한 결론이다.

</details>

### 연습 5 — CRC-16/MODBUS 와 UART

CRC-16/MODBUS(poly 0x8005, init 0xFFFF, refin/refout, xorout 0)를 엔진 파라미터로 추가해 check 값 0x4B37 을 확인하고, LSB-first 와이어에서 데이터 8 바이트의 버스트 L ≤ 16 전수가 0 이고 L = 17 에서 위치마다 1개인지 확인하라. 검사값은 어떤 바이트 순서로 붙이나?

<details><summary>답 보기</summary>

check 0x4B37, L ≤ 16 → 0, L = 17 → 64 (= 80 − 17 + 1). 반사형이므로 하위 바이트 먼저(LE): "123456789" 뒤에 `37 4B`. Modbus RTU 가 CRC 하위 바이트를 먼저 보내는 것과 같다 (COMM-B13). 연습 4 결과대로 이 CRC 를 MSB-first(SPI) 링크에 그대로 가져가면 11비트 버스트도 놓친다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 15/15 passed
```

## 10. 핵심 정리

- 미검출 = "바뀌었는데 통과". 오류는 검사값까지 포함한 와이어 비트에 넣고, 버스트는 양 끝 비트를 반드시 뒤집는다.
- CRC 는 GF(2) 선형 → 미검출은 **데이터와 무관**, "오류 다항식이 g 의 배수" 일 때만. 신드롬 표 하나로 전수 분석이 된다. 체크섬은 데이터에 따라 놓친다.
- 버스트: 길이 ≤ r 전부 검출, r+1 은 2^-(r−1), 그 이상 2^-r (실측 CRC-8 L=9 1975 vs 1953, CRC-16 L≥18 100 vs 87.7).
- k비트: 미검출률 = A_k / C(N,k) (정확). g(1) = 0 이면 홀수 비트 오류 전부. 32 B 실측: sum8 2비트 6.9 %, CRC-8 0.42 %, CRC-16 · CRC-32 0.
- HD 는 길이에 따라 떨어진다: CRC-8 0x07 HD 4 ≤ 119비트, CRC-16 0x1021 HD 4 ≤ 32751, CRC-32 HD 6 ≤ 268 · HD 5 ≤ 2974 (직접 찾음, Koopman 과 일치).
- 비트 순서: UART ↔ 반사형, SPI/I2C ↔ 비반사형. 어긋나면 CRC-8 은 8비트, CRC-32 는 31비트 버스트도 놓친다. 검사값 바이트 순서도 다항식이 이어지게.
- 고르는 법: 프레임 최대 길이 → 필요한 HD → 최대 버스트 → 버스 순서 → 가장 짧은 CRC. init ≠ 0 과 LEN 필드로 0/길이 오류를 막는다.

## 참고자료

- P. Koopman, CRC Polynomial Zoo — 다항식별 데이터 길이 대 HD 표 (이 레슨의 119 / 32751 / 268 / 2974 를 대조): <https://users.ece.cmu.edu/~koopman/crc/>
- P. Koopman, T. Chakravarty, "Cyclic Redundancy Code (CRC) Polynomial Selection for Embedded Networks", DSN 2004 — 짧은 임베디드 프레임에서 다항식 고르기.
- P. Koopman, "32-Bit Cyclic Redundancy Codes for Internet Applications", DSN 2002 — 802.3 CRC-32 의 길이별 HD (91607 / 4294967263 은 여기 수치를 인용; 서지 확인 권장).
- T. Maxino, P. Koopman, "The Effectiveness of Checksums for Embedded Control Networks", IEEE TDSC 2009 — 합 · Fletcher · Adler · CRC 비교.
- W. W. Peterson, D. T. Brown, "Cyclic Codes for Error Detection", Proc. IRE, 1961 — 버스트 검출 정리의 원 논문.
- Greg Cook, RevEng CRC catalogue — CRC-8/SMBUS, CRC-16/IBM-3740(CCITT-FALSE), CRC-16/MODBUS, CRC-32/ISO-HDLC 의 파라미터 · check · residue (0xDEBB20E3): <https://reveng.sourceforge.io/crc-catalogue/all.htm>
- Ross N. Williams, "A Painless Guide to CRC Error Detection Algorithms" (1993) — Rocksoft 파라미터 모델 (width/poly/init/refin/refout/xorout).
- SMBus Specification (PEC = CRC-8, poly x^8+x^2+x+1, MSB-first) / Modbus over Serial Line V1.02 (CRC 하위 바이트 먼저) — 세부 절 번호는 문서 확인 권장.
- 관련 레슨: ALGO-B14 (sum/inet/Fletcher 와 맹점), ALGO-M01 (CRC 원리), ALGO-M02 (테이블/slicing), CPP-M13 (constexpr CRC LUT), COMM-B03 (baud 오차), COMM-B09 (하드웨어 CRC 유닛), COMM-B10 (LEN + CRC16 프레임), COMM-B13 (Modbus RTU), COMM-M10 (견고한 파서), HWLAB-M05 · HWLAB-M06 (로직 애널라이저), TEST-M02 (파서 퍼징)

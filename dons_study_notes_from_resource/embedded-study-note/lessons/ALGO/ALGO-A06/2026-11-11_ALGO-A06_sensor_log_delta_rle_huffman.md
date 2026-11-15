---
id: ALGO-A06
time: 120분
updated: 2026-11-11
---
# ALGO-A06 센서 로그 압축: 델타·RLE·허프만

## 학습 목표

- int16 센서 로그에 **델타(mod 2^16) → zigzag → LEB128 varint → 바이트 RLE → 정적 정규 허프만**을 단계별로 적용하고, 단계마다 바이트 수와 **0차 엔트로피**(Shannon H0)를 재서 "어느 단계가 무엇을 줄였나"를 숫자로 말할 수 있다.
- int16 차이가 17비트라는 문제를 **2^16 나머지 델타**로 푼다. zigzag 전단사, varint 인코딩의 유일성(엄격한 디코더), RLE 최악 크기 n + ⌈n/128⌉ 을 증명한다.
- 허프만 부호 길이를 **두 큐**(two-queue)로 만들고, **길이 ≤ 15 제한**(휴리스틱 + 연습에서 최적 package-merge), **정규 부호**(canonical code), MSB-first 비트 I/O, 길이표만으로 푸는 디코더를 구현한다. Kraft 부등식과 H ≤ L̄ < H+1 을 테스트로 확인한다.
- 펌웨어 블록 형식(모드 자동 선택 + stored 폴백 → 블록 ≤ 4 + 2n), 손상 입력에서 범위 밖 접근 없음, 비트 하나 뒤집힘이 얼마나 자주 **안 잡히는지**(→ CRC 필요)를 측정한다.

## 1. 문제

펌웨어 면접에서 "센서 로그를 플래시에 더 많이 담아라"는 알고리즘(엔트로피·부호)과 펌웨어(정수 폭, RAM, 플래시 페이지, 손상) 질문이 한 번에 나오는 단골 설계+코딩 문제다. 압축 라이브러리를 못 쓰는 MCU에서 **직접 짤 수 있는 가장 단순하고 효과 큰 조합**이 이 레슨의 파이프라인이다.

센서 값은 천천히 변한다 → 값 자체보다 **차이**(delta)가 작다 → 작은 수는 **적은 바이트**(varint)로 → 차이 0 이 연달아 나오면 **런**(RLE)으로 → 남은 바이트 분포의 치우침은 **허프만**으로. 각 단계는 앞 단계가 만든 구조 하나씩을 먹는다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a06a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">파이프라인과 단계별 바이트 (solution.c 실제 출력, 온도 24 h · 가속도 10 min)</text>
  <g stroke="currentColor">
    <rect x="10" y="45" width="100" height="50" rx="6" class="f-mut"/>
    <rect x="135" y="45" width="100" height="50" rx="6" class="f-mut"/>
    <rect x="260" y="45" width="100" height="50" rx="6" class="f-acc"/>
    <rect x="385" y="45" width="100" height="50" rx="6" class="f-acc"/>
    <rect x="510" y="45" width="100" height="50" rx="6" class="f-acc"/>
    <rect x="635" y="45" width="95" height="50" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="60" y="66">int16 원본</text><text x="60" y="84" font-family="monospace">2 B/샘플</text>
    <text x="185" y="66">델타 mod 2^16</text><text x="185" y="84">+ zigzag</text>
    <text x="310" y="66">LEB128 varint</text><text x="310" y="84" font-family="monospace">1..3 B</text>
    <text x="435" y="66">바이트 RLE</text><text x="435" y="84" font-family="monospace">lit / run</text>
    <text x="560" y="66">정규 허프만</text><text x="560" y="84" font-family="monospace">≤ 15 bit</text>
    <text x="682" y="66">블록</text><text x="682" y="84">모드 선택</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5">
    <path d="M110 70 L133 70" marker-end="url(#a06a)"/><path d="M235 70 L258 70" marker-end="url(#a06a)"/>
    <path d="M360 70 L383 70" marker-end="url(#a06a)"/><path d="M485 70 L508 70" marker-end="url(#a06a)"/>
    <path d="M610 70 L633 70" marker-end="url(#a06a)"/>
  </g>
  <text x="10" y="128" fill="currentColor" font-weight="bold">온도 (1 Hz, 0.01 °C, 86400 샘플)</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="150">172800</text><text x="185" y="150">172800</text><text x="310" y="150">86422</text>
    <text x="435" y="150">22086</text><text x="560" y="150">12343</text><text x="682" y="150">12371</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="185" y="168">H0 6.17→0.76 bit</text><text x="310" y="168">50.0 %</text><text x="435" y="168">12.8 %</text>
    <text x="560" y="168">7.1 %</text><text x="682" y="168">7.2 % (14×)</text>
  </g>
  <text x="10" y="205" fill="currentColor" font-weight="bold">가속도 (100 Hz, xyz 섞임, 4096 LSB/g, 충격 + 포화)</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="227">360000</text><text x="185" y="227">360000</text><text x="310" y="227">183370</text>
    <text x="435" y="227">184782</text><text x="560" y="227">141838</text><text x="682" y="227">140428</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="185" y="245">H0 6.94→5.86 bit</text><text x="310" y="245">50.9 %</text><text x="435" y="245" class="f-warn">51.3 % (늘어남)</text>
    <text x="560" y="245">39.4 %</text><text x="682" y="245">39.0 % (VH)</text>
  </g>
  <text x="10" y="282" fill="currentColor">델타·zigzag 는 크기 대신 분포를 바꾼다. RLE 는 0 런이 많은 온도에만 듣는다.</text>
</svg>
```

### Part A — 델타 · zigzag · varint · RLE

- `delta16_enc/dec(x, n, nch, d)` — 채널 nch 개가 섞여(interleaved, `x y z x y z …`) 들어온다. 같은 채널의 이전 값과의 차를 **2^16 나머지**로 계산해 16비트에 담는다(첫 프레임은 0 기준).
- `zz16_enc/dec` — 0, −1, 1, −2, … → 0, 1, 2, 3, … 전단사. 전부 unsigned 산술(구현 정의 동작 없이).
- `vu16_put/get` — LEB128 varint(16비트 값 → 1..3 B). 디코더는 **엄격**: 잘림, 3바이트 초과, 값 > 0xFFFF, 비최소(overlong) 인코딩을 거부.
- `rle_enc/dec` — 바이트 RLE. 형식은 이 레슨이 정한다(아래). 디코더는 손상 입력에서도 범위 밖 접근이 없어야 한다.
- 테스트: zigzag 65536 개 전수(순열 + 공식), 델타의 경계값(INT16_MIN↔MAX), 채널별 델타 손 계산, varint 알려진 벡터(300 → `AC 02`)와 65536 개 왕복, 무작위 3바이트 20만 개에서 "디코드되면 재인코드가 같다", RLE 손 계산·600 개 왕복·최악 크기.

### Part B — 정적 정규 허프만

- `huff_lengths(freq, len)` — 두 큐로 부호 길이(제한 없음). `huff_limit(freq, len, 15)` — 길이 ≤ 15, Kraft ≤ 1 로 고치기. `huff_canon(len, code)` — RFC 1951 방식 정규 부호.
- `bw_put / br_bit` — MSB-first 비트 쓰기/읽기. `huff_dec_sym` — 길이표에서 만든 `count[]`/`sym[]` 만으로 한 기호 디코드.
- 테스트: CLRS 예(길이 1,3,3,3,4,4 · 비용 224), 기호 2~7개 400 경우를 **전수 탐색 최적**과 비교, 피보나치 빈도(깊이 19 → 15로 제한), 작은 L 에서 휴리스틱 ≥ 최적, RFC 1951 예의 부호값, 접두 성질, 비트 I/O 알려진 바이트, 블록 400 개 왕복 + H ≤ L̄ < H+1, 손상 헤더/잘림/덧붙임 거부.

### Part C — 펌웨어 센서 로거 블록

- 블록 = `[mode][nch][nframes u16][payload]`. 인코더는 STORED · V · VR · VH · VRH 다섯 후보 중 가장 작은 것을 고른다 → **어떤 입력도 4 + 2n 바이트를 넘지 않는다**.
- 합성 로그 두 개(설명은 5. 풀이): 온도 24시간(1 Hz, 블록 4096 샘플), 3축 가속도 10분(100 Hz, 블록 1024 프레임). 단계별 바이트·엔트로피 표, 모든 블록 왕복.
- 경계: 빈 블록, 1 샘플, INT16 극값 진동, 무작위 int16(→ STORED), 블록 한도 초과 거부, 무작위 신호 300 블록.
- 손상: 모든 잘린 접두사와 1바이트 덧붙임 거부, 비트 1개 뒤집기 3000 회 × 2 블록 + 무작위 바이트열 3000 개에서 **출력 cap 뒤 카나리아가 그대로**인지. 뒤집힘이 안 잡히는 비율을 센다.

## 2. Problem (English)

Design a lossless compressor for an MCU sensor logger. Input: `nframes` frames of `nch` interleaved `int16_t` channels (≤ 16384 samples per block). Implement:

```c
void     delta16_enc(const int16_t *x, size_t n, int nch, uint16_t *d);   /* per-channel, mod 2^16 */
uint16_t zz16_enc(uint16_t d);           uint16_t zz16_dec(uint16_t z);
int      vu16_put(uint8_t *p, uint16_t v); /* 1..3 bytes */
int      vu16_get(const uint8_t *p, size_t avail, uint16_t *v);   /* strict: 0 on error */
size_t   rle_enc(const uint8_t *in, size_t n, uint8_t *out);
long     rle_dec(const uint8_t *in, size_t n, uint8_t *out, size_t cap);  /* -1 on error */
int      huff_lengths(const uint32_t *freq, uint8_t *len);          /* optimal, unlimited */
void     huff_limit(const uint32_t *freq, uint8_t *len, int maxbits);
int      huff_canon(const uint8_t *len, uint16_t *code);            /* -1 if Kraft > 1 */
size_t   pipe_encode(const int16_t *x, size_t nframes, int nch, uint8_t *out, size_t cap, pstat_t *st);
long     pipe_decode(const uint8_t *in, size_t len, int16_t *x, size_t cap, int *nch);
```

Constraints: no `malloc`; every decoder must reject (not crash on) truncated, padded or corrupted input and never write past `cap`; block size ≤ 4 + 2·n bytes for any input; Huffman codes ≤ 15 bits. Report, per stage, the byte count and the order-0 entropy of that stage's symbols, and compare with the achieved bits/symbol.

Example: `x = {32767, −32768}` → deltas mod 2^16 `{32767, 1}` → zigzag `{65534, 2}` → varint `FE FF 03 02` (4 bytes). Frequencies `a:45 b:13 c:12 d:16 e:9 f:5` → lengths `1 3 3 3 4 4`, canonical codes `a=0 b=100 c=101 d=110 e=1110 f=1111`, cost 224 bits.

Follow-ups: why zigzag before varint? deltas of int16 need 17 bits — how do you store them? why can't Huffman reach the entropy here? what does one flipped bit in flash do?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter -lm && ./starter
```

TODO 는 아홉 군데: ① `zz16_enc/dec` ② `delta16_enc/dec` ③ `vu16_put/get` ④ `rle_enc/dec` ⑤ `huff_lengths` ⑥ `huff_limit` ⑦ `huff_canon` ⑧ `bw_put/br_bit` ⑨ `huff_dec_sym`. 주어지는 것: int16↔비트 변환, varint 스트림 래퍼, 디코더 표(`hdec_build`), 허프만 블록 형식, 파이프라인 블록 형식(`pipe_encode/decode`), 생성기, 하네스. 스텁은 "0 바이트 / 오류" 를 돌려주므로 멈추거나 죽지 않는다. 처음 실행(실제 출력, 중간 생략):

```text
FAIL starter.c:462  bij
FAIL starter.c:463  formula
…
FAIL starter.c:494  vu16_put(p, 300) == 2 && p[0] == 0xAC && p[1] == 0x02
…
FAIL starter.c:771  trunc
13/60 passed
```

(exit 1, 0.1 초. 통과하는 13 개는 스텁의 "오류(-1/0)" 반환이 우연히 맞는 거부 검사와 생성기 범위 검사 등.)

## 4. 힌트

<details><summary>힌트 1 — 17비트 문제는 "피하는" 게 답</summary>

int16 끼리 빼면 −65535..65535, 17비트다. 그런데 디코더는 x[i] = x[i−1] + d 만 하면 된다. 이 덧셈을 **mod 2^16** 으로 하면 d 도 mod 2^16 이면 충분하다. `(uint16_t)(u - prev)` (u, prev 는 uint16 비트 패턴) — unsigned 이니 전부 정의된 동작이다.

</details>

<details><summary>힌트 2 — zigzag 는 부호 비트를 맨 아래로</summary>

`(d << 1) ^ mask`, mask = 부호가 음수면 0xFFFF, 아니면 0. 산술 시프트(`int16 >> 15`)는 음수에서 구현 정의라 `0u - (d >> 15)` (d 는 uint16)로 만든다. 역함수는 `(z >> 1) ^ (0 − (z & 1))`.

</details>

<details><summary>힌트 3 — RLE 는 "런 ≥ 3 만 런"</summary>

길이 r 을 재서 r ≥ 3 이면 대기 중인 리터럴을 먼저 내보내고 `[0x80 + r − 3][b]`. 아니면 한 바이트를 리터럴 덩어리에 넣고(최대 128), 꽉 차면 `[len − 1][bytes…]` 로 내보낸다. 디코더는 복사 전에 "입력이 k 바이트 남았나, 출력에 k 칸 남았나" 를 둘 다 본다.

</details>

<details><summary>힌트 4 — 두 큐: 힙 없이 허프만</summary>

잎을 빈도 오름차순으로 정렬해 두면, 새로 만든 내부 노드는 무게가 비감소다(늘 두 최소의 합이니까). 그래서 "잎 큐 앞"과 "내부 노드 큐 앞" 중 작은 것을 두 번 뽑으면 된다. 부모 번호는 항상 자식보다 크므로, 뿌리부터 번호 역순으로 `depth[v] = depth[par[v]] + 1`.

</details>

<details><summary>힌트 5 — 길이 제한은 Kraft 합을 정수로</summary>

K = Σ 2^(15 − len) 를 들고 다니면 Kraft ≤ 1 ⇔ K ≤ 2^15. 15 를 넘는 길이를 15 로 자르면 K 가 넘친다. 15 보다 짧은 부호 중 가장 긴 것을 1 늘릴 때마다 K 가 2^(15−len−1) 씩 준다. 끝나면 남은 여유로 빈도 큰 기호를 줄인다.

</details>

<details><summary>힌트 6 — 정규 부호 디코드는 길이별 "첫 부호"</summary>

길이 L 의 첫 부호를 first, 그 길이의 부호 개수를 count 라 하면, 지금까지 읽은 L 비트 값 code 가 `code − first < count` 일 때 L 비트 부호다. 아니면 `first = (first + count) << 1`, code 에 비트 하나 더.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 확인 질문 | 무손실? 샘플 폭(int16)·채널 수·샘플링 속도? 블록 단위로 랜덤 접근/손상 격리가 필요한가? RAM·플래시 페이지 크기? 인코더는 ISR 인가 태스크인가? |
| 2. 기준선 | 원본 2 B/샘플. 일반 압축기(zlib)는 MCU 에 무겁다(창 RAM). 0차 엔트로피 H0 로 "값 자체를 기호로 볼 때의 한계"를 먼저 잰다 |
| 3. 아이디어 | 신호가 느림 → 델타 → 작은 부호 있는 수 → zigzag + varint → 0 런 → RLE → 남은 치우침 → 허프만. 불변식: 각 단계는 **전단사**(왕복 가능)이고 크기 상한이 있다 |
| 4. 코드 | 단계 함수 + 블록 형식(모드 + stored 폴백) + 엄격한 디코더 |
| 5. 테스트 | 전수(zigzag·varint 65536), 알려진 답(varint 300, CLRS, RFC 1951), 무작위 왕복, 최악 크기, 손상 퍼징, 단계별 엔트로피 |
| 6. 복잡도 | 블록 n 샘플: O(n) + 허프만 O(256²) 상수. 추가 RAM = 빈도 1 KB + 길이/부호표 ~0.8 KB (+ 버퍼, Part C) |

### Part A — 델타는 왜 mod 2^16 인가

φ: int16 → Z/2^16 (2의 보수 비트 패턴)는 전단사다. 인코더는 d_i = φ(x_i) − φ(x_{i−nch}) (mod 2^16), 디코더는 φ(x_i) = φ(x_{i−nch}) + d_i (mod 2^16). 귀납법으로 모든 i 에서 정확히 복원된다 — **진짜 정수 차를 몰라도** 된다. 진짜 차는 (−2^16, 2^16) 안에서 한 나머지에 두 후보가 있지만, 디코더는 고를 필요가 없다: 나머지 덧셈 결과는 int16 하나로 정해진다.

부수 효과: 큰 점프가 작게 보일 수 있다. 32767 → −32768 은 진짜 차 −65535 지만 mod 2^16 으로 **1** 이다. 테스트의 "INT16 극값 진동" 64 샘플은 그래서 V 모드 70 바이트(헤더 4 + 첫 값 3 + 1 B × 63)로 줄었다. int32 로 넓혀 zigzag32 를 쓰면 ±65535 → zz 131069/131070 → 3 B × 63 이다.

C 함정: `int16_t d = x[i] - x[i-1];` 의 뺄셈은 int 로 승격돼 오버플로 UB 는 없지만, 결과를 int16 에 넣는 변환은 범위 밖이면 **구현 정의**(C11 6.3.1.3p3)다. GCC 문서는 "2^N 나머지로 줄인다"고 명시하고 clang 도 같게 동작해 우연히 같은 결과가 나오지만, 이 레슨은 uint16 산술로 처음부터 정의된 동작만 쓴다(`bits_i16` 도 int 산술로 직접 변환).

### Part A — zigzag 가 전단사인 이유

Z(d) = 2d (d ≥ 0), −2d − 1 (d < 0), d ∈ [−2^15, 2^15). 음 아닌 d 는 짝수 {0, 2, …, 2^16 − 2} 로, 음수는 홀수 {1, 3, …, 2^16 − 1} 로 가고 각각 단사이며 두 상은 서로소, 합집합 = [0, 2^16). 따라서 전단사.

비트 공식이 Z 와 같은 이유: m = 0 − (d >> 15) 는 d ≥ 0 이면 0, 아니면 0xFFFF. d ≥ 0 이면 (2d) ^ 0 = 2d. d < 0 이면 2d mod 2^16 = 2^16 + 2d (2d ≥ −2^16 이므로), 이것을 0xFFFF 와 XOR = 0xFFFF − (2^16 + 2d) = −2d − 1. 테스트는 65536 개 전부에서 공식·순열·역함수를 확인한다.

**왜 필요한가**: −1 의 16비트 패턴은 0xFFFF 라서 varint 로 3 바이트다. zigzag 후 1 → 1 바이트. 노이즈 ±8 LSB 의 가속도에서 zigzag 를 빼면 음수 델타가 전부 3 B 가 된다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">비트 배치: zigzag · LEB128 varint · RLE 토큰</text>
  <text x="10" y="50" fill="currentColor">d = −3 (0xFFFD)</text>
  <text x="180" y="50" fill="currentColor" font-family="monospace">1111 1111 1111 1101</text>
  <text x="10" y="72" fill="currentColor">d &lt;&lt; 1</text>
  <text x="180" y="72" fill="currentColor" font-family="monospace">1111 1111 1111 1010</text>
  <text x="10" y="94" fill="currentColor">^ mask(0xFFFF)</text>
  <text x="180" y="94" fill="currentColor" font-family="monospace">0000 0000 0000 0101</text>
  <text x="380" y="94" fill="currentColor">= 5 = −2·(−3) − 1 → varint 1 B</text>
  <text x="10" y="132" fill="currentColor" font-weight="bold">varint 300 = 0b1_0010_1100 → 7비트씩 아래부터</text>
  <g stroke="currentColor">
    <rect x="30" y="145" width="22" height="26" class="f-warn"/><rect x="52" y="145" width="154" height="26" class="f-acc"/>
    <rect x="236" y="145" width="22" height="26" class="f-mut"/><rect x="258" y="145" width="154" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="41" y="163">1</text><text x="129" y="163">010 1100</text>
    <text x="247" y="163">0</text><text x="335" y="163">000 0010</text>
  </g>
  <text x="30" y="190" fill="currentColor">0xAC: 연속 비트 1 + 아래 7비트</text>
  <text x="236" y="190" fill="currentColor">0x02: 끝 + 다음 7비트</text>
  <text x="440" y="155" fill="currentColor">16비트 값 → 7 + 7 + 2 비트 = 최대 3 B</text>
  <text x="440" y="175" fill="currentColor">3째 바이트는 0x00..0x03 만 허용</text>
  <text x="440" y="195" fill="currentColor">끝 바이트 0x00 (첫 바이트 제외) → 거부</text>
  <text x="10" y="232" fill="currentColor" font-weight="bold">RLE 토큰 (이 레슨의 형식)</text>
  <g stroke="currentColor">
    <rect x="30" y="245" width="90" height="28" class="f-ok"/><rect x="120" y="245" width="190" height="28" class="f-mut"/>
    <rect x="390" y="245" width="90" height="28" class="f-warn"/><rect x="480" y="245" width="70" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="264" font-family="monospace">0x00..7F</text><text x="215" y="264">c+1 바이트 그대로 (1..128)</text>
    <text x="435" y="264" font-family="monospace">0x80..FF</text><text x="515" y="264">값 b</text>
  </g>
  <text x="30" y="295" fill="currentColor">리터럴: 비용 k+1, 이득 없음</text>
  <text x="390" y="295" fill="currentColor">런: b 를 (c−0x80)+3 번 (3..130), 비용 2</text>
  <text x="10" y="320" fill="currentColor">길이 2 런은 리터럴로 둔다: 런 토큰도 2 B 인데 리터럴 덩어리를 끊으면 제어 바이트가 하나 더 생길 수 있다.</text>
</svg>
```

### Part A — varint: 엄격한 디코더 = 유일한 인코딩

LEB128(unsigned): 아래 7비트씩, 뒤에 더 있으면 최상위 비트 1. 16비트 값이면 7+7+2 비트라 최대 3 바이트. 디코더가 받아들이는 바이트열의 조건을 정하면 **인코딩이 유일**해진다:

1. 마지막 바이트(연속 비트 0)가 0x00 이면 첫 바이트일 때만 허용 — 그 외엔 "앞 바이트에서 끝났어야 했다"(비최소).
2. 3째 바이트에 연속 비트가 있거나 값이 0x03 을 넘으면 거부.

이 두 조건을 통과한 바이트열은 정확히 `vu16_put` 이 내는 것이다(인코더는 v ≥ 0x80 인 동안만 연속 비트를 붙이므로 마지막 바이트는 v < 0x80 의 나머지 = 0 은 v = 0 일 때뿐). 테스트: 무작위 3 바이트 20만 개 중 디코드에 성공한 것은 전부 재인코드가 같다. 유일성이 왜 중요한가: 손상 탐지(이상한 바이트열을 받아들이지 않음)와 "같은 데이터 = 같은 바이트"(중복 제거·해시)에 쓴다. protobuf 의 varint 도 같은 LEB128 계열이다(DIST-B06 에서 메시지 형식으로 다룬다).

### Part A — RLE 최악 크기 n + ⌈n/128⌉ 증명

출력은 리터럴 덩어리(k 바이트 → k+1)와 런(r ≥ 3 바이트 → 2)으로 나뉜다. 런마다 입력보다 r − 2 ≥ 1 바이트 짧다. 리터럴 덩어리는 제어 바이트 1 만큼 길다. 리터럴 덩어리가 끝나는 이유는 셋뿐이다: (a) 다음이 런, (b) 128 로 꽉 참, (c) 입력 끝. (a) 인 덩어리의 +1 은 바로 뒤 런의 −1 이상으로 상쇄된다. 그러므로 출력 − n ≤ (꽉 찬 덩어리 수 F) + (입력 끝에서 끝난 덜 찬 덩어리 ≤ 1). 덜 찬 마지막 덩어리가 있으면 리터럴 바이트가 128F + 1 이상이라 F ≤ ⌊(n−1)/128⌋, 합 ≤ ⌈n/128⌉. 없으면 F ≤ ⌊n/128⌋ ≤ ⌈n/128⌉. ∎ 런이 하나도 없는 입력(주기 2, `55 AA 55 AA …`)은 정확히 이 한계에 닿는다 — 테스트 `tight`.

인코드 추적(온도 varint 바이트의 한 조각):

| 위치 | 입력 | 런 길이 r | 동작 | 출력 누적 |
|---|---|---|---|---|
| 0 | `00 00 00 00 00` | 5 | 런 → `82 00` | `82 00` |
| 5 | `0C` | 1 | 리터럴 시작 [5] | |
| 6 | `00 00` | 2 | < 3 → 리터럴 | |
| 7 | `00` | 1 (뒤가 0B) | 리터럴 | |
| 8 | `0B` | 1 | 리터럴 (4 바이트 대기) | |
| 9 | `00 00 00 00` | 4 | 리터럴 내보내고 런 | `… 03 0C 00 00 0B 81 00` |

입력 13 바이트 → 출력 9 바이트. 온도 전체에서 varint 86422 B 의 대부분이 `00`(zz = 0 의 비율 0.878, 0 런 5274 개, 평균 길이 14.4 — 별도 통계 프로그램 실제 출력)이라 RLE 가 22086 B 로 줄인다. 가속도는 0 런이 거의 없어 183370 → 184782 B 로 **늘어난다**(제어 바이트) — 그래서 블록 모드 선택이 필요하다.

### Part B — 두 큐 허프만과 최적성

CLRS 예(빈도 f5 e9 c12 b13 d16 a45)의 실제 병합 순서:

| 단계 | 잎 큐 앞 | 내부 큐 | 뽑은 둘 | 새 노드 |
|---|---|---|---|---|
| 1 | f5 e9 … | — | f5, e9 | n1 = 14 |
| 2 | c12 b13 … | 14 | c12, b13 | n2 = 25 |
| 3 | d16 a45 | 14 25 | n1 14, d16 | n3 = 30 |
| 4 | a45 | 25 30 | n2 25, n3 30 | n4 = 55 |
| 5 | a45 | 55 | a45, n4 55 | 뿌리 100 |

깊이: a 1, b·c·d 3, e·f 4 → 비용 45 + 3·(13+12+16) + 4·(9+5) = 224 (테스트가 확인).

**두 큐가 맞는 이유**: k 번째 새 노드 무게 w_k = (그 시점 최소 둘의 합). 뽑힌 원소들은 시간에 따라 비감소(새 노드는 뽑힌 것보다 크거나 같으니까)이므로 w_k 도 비감소 → 내부 노드 큐는 이미 정렬된 FIFO, 앞이 최소. 그러니 "두 큐 앞 중 작은 것" = 전체 최소. 잎 정렬 O(n log n)(여기선 n ≤ 256 이라 삽입 정렬), 병합 O(n).

**허프만이 최적인 이유**(Huffman 1952, CLRS 16.3 의 뼈대): (1) 가장 드문 두 기호 x, y 가 최대 깊이에서 형제인 최적 트리가 있다 — 최대 깊이의 형제 쌍과 자리를 바꾸면 비용이 늘지 않는다(교환 논증). (2) x, y 를 무게 f_x + f_y 인 한 기호 z 로 합친 문제의 트리 T' 에 대해 cost(T) = cost(T') + f_x + f_y. (1)(2) 와 기호 수에 대한 귀납법으로 최적. 테스트는 기호 2~7개 400 경우에서 **모든 길이 조합을 전수 탐색**한 최소 비용(Kraft ≤ 1 인 길이 벡터만 — Kraft–McMillan 정리로 그런 길이면 접두 부호가 존재)과 같음을 확인하고, 허프만 트리는 꽉 찬 이진 트리라 Kraft 합이 **정확히 1** 인 것도 본다.

### Part B — Kraft, 엔트로피, 그리고 "1 비트의 벽"

- **Kraft**: 접두 부호의 각 부호어는 깊이 L 의 잎 2^(L − l) 개를 혼자 차지한다(겹치면 접두사) → Σ 2^(L−l) ≤ 2^L. 역으로 Kraft 를 만족하는 길이면 정규 할당이 넘치지 않는다 — `huff_canon` 이 길이별로 `next + count ≤ 2^b` 를 확인하는 게 바로 이 검사다.
- **H ≤ L̄**: L̄ − H = Σ p_i log2(p_i / 2^−l_i) ≥ −log2 Σ 2^−l_i ≥ 0 (Gibbs 부등식 + Kraft). **L̄ < H + 1**: 샤논 길이 ⌈−log2 p_i⌉ 가 Kraft 를 만족하고 L̄ < H+1 이니, 최적인 허프만은 그 이하. 테스트는 블록 400 개에서 둘 다 확인한다(기호가 하나뿐이면 H = 0 인데 길이 1 이 필요해 상한이 깨진다 — 그 경우는 제외).
- **1 비트의 벽**: 허프만은 기호마다 **최소 1 비트**. 온도의 zz 값은 0 이 87.8 %(±6 이 4.6 % 씩, ±7 이 1.4 % 씩)라 H0 = 0.756 bit/샘플인데, zz 를 바로 허프만하면 ≥ 1 bit/샘플 = 10800 B 가 바닥이다. RLE 가 "0 이 14개" 를 한 기호로 묶어 기호당 정보량을 올려야 벽을 넘는다. 실측 payload 는 12343 − 2904(헤더) = 9439 B = 0.874 bit/샘플 — 10800 B 벽 아래다. 엔트로피 한계(8162 B)까지 가려면 산술 부호/ANS 처럼 분수 비트를 쓰는 부호나 더 나은 모델(연습 3 의 0-런)이 필요하다.

### Part B — 길이 제한: 휴리스틱과 그 대가

15 비트를 넘는 부호는 디코드 표·비트 버퍼(32비트 누산기에 2개 이상 안 들어감)·헤더 니블(4비트) 모두를 깬다. DEFLATE 도 15 비트로 제한한다(RFC 1951). 피보나치 빈도 20 개는 최대 깊이 19 의 치우친 트리를 만든다(실제 출력):

```text
fib20: unlimited max=19 cost=46344 | limited(15) max=15 cost=46374
limit heuristic vs brute-force optimum: 6/120 cases worse, total extra 247 bits over 10836
```

`huff_limit` 의 정확성: 자른 뒤 K 의 넘침은 잘린 기호 수 이하(각 기호의 원래 기여는 < 1 이었고 지금 1). 넘치는 동안 15 보다 짧은 기호가 반드시 있다(전부 15 면 K = 기호 수 ≤ 256 ≤ 2^15). 한 번 늘릴 때 K 가 ≥ 1 줄어 유한 번에 K ≤ 2^15 → Kraft 성립. 여유 채우기는 K 를 넘기지 않는 변화만 한다. **최적은 아니다**: 작은 L(3, 4)에서 전수 탐색과 비교하면 120 중 6 경우가 더 나빴다(전체 +2.3 %). 최적은 Larmore–Hirschberg 의 package-merge(O(nL)) — 연습 1 에서 구현해 피보나치 20 에서 46348 비트(휴리스틱 46374)를 얻는다. 이 레슨의 센서 블록에선 제한이 아예 필요 없었다: 허프만 표 162 개(온도 44, 가속도 118)의 제한 전 최대 깊이가 10 / 12 (별도 통계 프로그램 실제 출력).

### Part B — 정규 부호와 디코더

같은 길이면 기호 번호 순으로 연속된 값을 주는 규칙(RFC 1951 3.2.2): `next[b] = (next[b−1] + count[b−1]) << 1`. RFC 의 예(A~H 길이 3,3,3,3,3,2,4,4)는 테스트에서 정확히 `010 011 100 101 110 00 1110 1111` 이 나온다. 장점: 헤더에 **길이만** 저장하면 된다(이 레슨: 256 × 4 비트 = 128 B). JPEG DHT 는 "길이별 개수 16 개 + 기호 목록"을 저장한다(MEDIA-B05).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">CLRS 예의 정규 부호 트리 (길이 a1 b3 c3 d3 e4 f4) 와 "1110" 디코드</text>
  <g stroke="currentColor" fill="none" stroke-width="1.4">
    <path d="M200 50 L110 100"/><path d="M200 50 L290 100"/>
    <path d="M290 100 L230 150"/><path d="M290 100 L350 150"/>
    <path d="M230 150 L195 200"/><path d="M230 150 L265 200"/>
    <path d="M350 150 L315 200"/><path d="M350 150 L385 200"/>
    <path d="M385 200 L350 250"/><path d="M385 200 L420 250"/>
  </g>
  <g stroke="currentColor">
    <circle cx="200" cy="50" r="8" class="f-mut"/><circle cx="290" cy="100" r="8" class="f-mut"/>
    <circle cx="230" cy="150" r="8" class="f-mut"/><circle cx="350" cy="150" r="8" class="f-mut"/>
    <circle cx="385" cy="200" r="8" class="f-mut"/>
    <rect x="94" y="92" width="32" height="20" class="f-ok"/><rect x="179" y="192" width="32" height="20" class="f-ok"/>
    <rect x="249" y="192" width="32" height="20" class="f-ok"/><rect x="299" y="192" width="32" height="20" class="f-ok"/>
    <rect x="334" y="242" width="32" height="20" class="f-warn"/><rect x="404" y="242" width="32" height="20" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="110" y="107">a</text><text x="195" y="207">b</text><text x="265" y="207">c</text>
    <text x="315" y="207">d</text><text x="350" y="257">e</text><text x="420" y="257">f</text>
    <text x="140" y="70">0</text><text x="260" y="70">1</text><text x="248" y="122">0</text><text x="335" y="122">1</text>
    <text x="200" y="172">0</text><text x="260" y="172">1</text><text x="320" y="172">0</text><text x="380" y="172">1</text>
    <text x="355" y="222">0</text><text x="415" y="222">1</text>
  </g>
  <text x="460" y="60" fill="currentColor" font-weight="bold">sym[] = a b c d e f</text>
  <text x="460" y="80" fill="currentColor" font-family="monospace">len code first count index</text>
  <text x="460" y="100" fill="currentColor" font-family="monospace"> 1    1    0     1     0</text>
  <text x="460" y="120" fill="currentColor" font-family="monospace"> 2   11    2     0     1</text>
  <text x="460" y="140" fill="currentColor" font-family="monospace"> 3  111    4     3     1</text>
  <text x="460" y="160" fill="currentColor" font-family="monospace"> 4 1110   14     2     4</text>
  <text x="460" y="185" fill="currentColor">len 4: code−first = 0 &lt; 2</text>
  <text x="460" y="205" fill="currentColor">→ sym[4 + 0] = e</text>
  <text x="460" y="235" fill="currentColor">불변식: code ≥ first (매 단계)</text>
  <text x="10" y="290" fill="currentColor">같은 길이 부호는 기호 순으로 연속 → (first, count) 로 디코드. 표 = count[16] + sym[256]</text>
</svg>
```

디코더 불변식: 길이 L 에서 code ≥ first_L. 처음 0 ≥ 0. code − first ≥ count 라서 다음 길이로 갈 때 code' = 2·code + b ≥ 2(first + count) = first_{L+1}. 그래서 `code − first < count` 비교 하나로 "이 길이의 부호인가" 가 결정된다(zlib 의 `puff.c` 가 쓰는 방식과 같은 꼴 — 확인 권장). 비트당 반복 1 번이라 느리다(Part C 측정) — 빠른 디코더는 앞 9~10 비트 표 조회(연습 아이디어).

**블록 끝 검사**: 기호 수 m 을 헤더에 두므로 패딩 비트를 기호로 오해하지 않는다. 디코더는 m 개를 푼 뒤 (1) 마지막 바이트 안에서 끝났는지 (2) 남은 패딩 비트가 0 인지 (3) 남는 바이트가 없는지를 확인한다. 이 세 검사가 "잘린 블록·덧붙인 블록 전부 거부" 테스트와 손상 탐지율을 올린다.

### Part C — 합성 로그 생성기 (결정적, xorshift32)

- **온도**(`gen_temp`, seed 2026): 진짜 값 = 24.000 °C + 하루 주기 삼각파 ±2.5 °C + 평균 회귀 랜덤 워크 드리프트(수백 m°C) + 잡음(균등 4개 합, σ ≈ 6.3 m°C). 12비트 디지털 센서처럼 **1/16 °C(0.0625 °C) 로 반올림 양자화**한 뒤 0.01 °C 정수로 저장(round(code × 6.25)). 그래서 값이 계단처럼 머물고(델타 0), 계단 경계 근처에선 ±6/±7 로 깜빡인다. 범위 21.44~26.56 °C(실제 출력).
- **가속도**(`gen_accel`, seed 1111): ±8 g 범위 가정 4096 LSB/g, 평평하게 놓임(x 40, y −25, z 4096) + 축마다 σ ≈ 8.5 LSB 잡음. 평균 15 초에 한 번 충격: 진폭 6000~48000 LSB × 축별 이득(−1..1), 3 프레임마다 부호가 바뀌는 감쇠 진동(×7/8 / 프레임) 40 프레임, int16 로 **포화**(10분에 포화 샘플 22 개, −32768..32767 모두 등장).

### Part C — 단계별 측정 (solution.c 실제 출력)

```text
== temperature 1 Hz: 86400 frames x 1 ch, block = 4096 frames, 22 blocks ==
  stage           bytes   %raw   H0 (bits/sym, sym)     H0 bound bytes
  raw int16      172800  100.0    6.170 /16b  4.394 /B     66641
  delta+zz       172800  100.0    0.756 /16b  0.444 /B      8162
  varint          86422   50.0              0.760 /B      8207
  RLE             22086   12.8              3.497 /B      9656
  Huffman(RLE)    12343    7.1   payload 3.415 bits/B, headers 2904 B
  chosen/block    12371    7.2   (= 1.15 bits/sample)  modes: VR=1 VRH=21
== accel 100 Hz xyz: 60000 frames x 3 ch, block = 1024 frames, 59 blocks ==
  raw int16      360000  100.0    6.943 /16b  5.212 /B    156216
  delta+zz       360000  100.0    5.862 /16b  3.830 /B    131902
  varint         183370   50.9              5.814 /B    133255
  RLE            184782   51.3              5.840 /B    134902
  Huffman(RLE)   141838   39.4   payload 5.802 bits/B, headers 7788 B
  chosen/block   140428   39.0   (= 6.24 bits/sample)  modes: VH=59
```

읽는 법:

- `/16b` 열 = 샘플 하나(16비트 값)를 기호로 본 H0, `/B` 열 = 바이트를 기호로 본 H0. "H0 bound bytes" = H0 × 기호 수 / 8 = 그 단계 기호를 **0차 부호기**로 줄일 수 있는 한계. zigzag 는 전단사(값의 이름만 바꿈)라 16비트 H0 가 델타와 똑같다 — 그래서 한 줄. zigzag 의 일은 엔트로피가 아니라 **varint 를 짧게** 하는 것.
- 온도: 델타가 H0 를 6.17 → 0.756 bit/샘플로 떨어뜨린 게 핵심(신호 모델). varint 는 그걸 바이트로 옮겼을 뿐(1 B/샘플 = 8 bit 인데 H0 는 0.76), RLE 가 런을 먹고, 허프만이 RLE 바이트의 치우침(3.497 → 3.415 bit/B, 블록별 표라 전역 H0 보다 약간 낮다)을 먹는다. **헤더 2904 B 가 출력의 23 %** — 블록 22 개 × 132 B. 연습 2(고정 표)가 이걸 없앤다. 마지막 384 샘플짜리 짧은 블록은 헤더가 아까워 VR 을 골랐다.
- 가속도: 잡음이 진짜 정보라 델타 후에도 5.86 bit/샘플. VH(varint→허프만)가 payload ≈ 5.88 bit/샘플로 16비트 H0 에 거의 붙었다. 0 런이 없어 RLE 는 해롭고, 모든 블록이 VH 를 골랐다.

### Part C — 블록 크기, zlib 와 비교 (`demo_bench.c`)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench -lm -lz && ./demo_bench
```

실제 출력 (1) — 블록 크기(샘플)별 총 크기. 우리 것은 블록 헤더 포함, zlib 는 같은 블록을 `compress2(level 9)` 한 것(원본 int16 바이트 / 우리 varint 바이트), 참고용:

| 데이터 | 블록 샘플 | 우리 % | zlib9(원본) % | zlib9(varint) % |
|---|---|---|---|---|
| 온도 | 128 | 15.7 | 10.9 | 11.3 |
| 온도 | 512 | 13.2 | 7.0 | 7.0 |
| 온도 | 2048 | 8.7 | 5.4 | 5.1 |
| 온도 | 8192 | 6.4 | 4.9 | 4.2 |
| 온도 | 16384 | 6.0 | 4.9 | 4.1 |
| 가속도 | 126 | 52.9 | 80.0 | 51.0 |
| 가속도 | 510 | 49.5 | 65.0 | 41.1 |
| 가속도 | 2046 | 40.0 | 58.8 | 38.5 |
| 가속도 | 8190 | 37.7 | 54.6 | 37.9 |
| 가속도 | 16383 | 37.4 | 53.1 | 37.9 |

- 작은 블록일수록 우리 쪽 132 B 허프만 헤더가 치명적이다(온도 128 샘플 블록 = 원본 256 B 에 헤더 132 B). zlib 은 작은 데이터에선 고정 허프만 표를 쓸 수 있어(DEFLATE 의 fixed 블록) 덜 아프다.
- 온도에서 zlib 가 이긴다: LZ77 은 "같은 계단 패턴의 반복"까지 먹는다(ALGO-A05). 가속도에선 원본에 바로 zlib 를 쓰면 53 % 로 **델타 없는 압축기가 진다** — 신호 모델(델타)이 일반 압축기보다 중요하다는 증거. 델타+varint 위에 zlib 를 얹어도 우리 파이프라인과 비슷(37.9 vs 37.4 %).
- MCU 관점: zlib 압축은 창(최대 32 KB) + 해시 표 RAM 이 필요하다(정확한 수치는 설정에 따라 다름 — 확인 권장). 이 레슨의 파이프라인은 Part C 아래 표의 RAM 으로 끝난다.

실제 출력 (2) — 블록 4096 샘플, 단계별 **누적** 인코드 시간과 디코드 시간(ns/샘플, 7 회 중앙값 [최소–최대]). Apple M6, Apple clang 21.0.0, `uptime` load averages **44.64 58.54 41.18**(병렬 작업 10 개가 같이 돌았다 — 절대값은 믿지 말고 비율만):

| 데이터 | delta+zz | +varint | +RLE | +허프만(VRH) | 인코드 전체 | 디코드 전체 |
|---|---|---|---|---|---|---|
| 온도 | 0.24 | 1.41 | 2.92 | 2.86 [2.01–9.73] | 4.86 | 4.78 |
| 가속도 | 0.32 | 0.63 | 2.15 | 6.27 | 14.71 [7.83–16.49] | 16.87 [13.12–19.14] |

같은 바이너리를 바로 두 번 더 돌리면 온도 디코드 3.53 / 6.38, 가속도 디코드 14.48 / 15.18 ns 로 ±50 % 흔들렸다(코어 이동). 흔들림 속에서도 일관된 것: **가속도 디코드가 온도의 2.5~4 배** — 비트 하나씩 읽는 디코더는 시간이 읽는 부호 비트 수에 비례하고(가속도 6.24 vs 온도 1.15 bit/샘플), 인코드 전체는 후보 두 개(VH, VRH)를 다 허프만하므로 단계 합보다 크다. MCU(수십~수백 MHz)에서는 이 비율 그대로 수십~수백 배 느려진다고 보고, 블록 하나 시간을 실측해 로깅 주기 안에 드는지 확인해야 한다.

### Part C — 펌웨어로 옮길 때: RAM · 스택 · 어디서 돌리나

| 항목 | 이 레슨(호스트) | MCU 에서 줄이는 법 |
|---|---|---|
| 원본 블록 | 2n B (어차피 로거가 모음) | 블록 = 플래시 페이지 몇 개 분량 |
| 단계 버퍼 | varint ≤ 3n, RLE ≤ 3n + 3n/128, 허프만 ≤ 2·RLE + 132 (정적) | **두 번 훑기**: 1차에 델타→varint→RLE 를 즉석 계산하며 빈도만 세고, 2차에 다시 계산하며 부호를 플래시 페이지 버퍼로 바로 쓴다 → 중간 버퍼 0 |
| 빈도표 | 256 × uint32 = 1 KB | 블록 < 65536 기호면 uint16 = 512 B |
| 길이/부호표 | 256 + 512 B | 디코더는 count[16] + sym[256] |
| `huff_lengths` 스택 | **9264 B** (arm64 `-fstack-usage` 실측) | uint64 w[512] → uint32, int par/depth → int16 이면 절반 이하. 또는 Moffat–Katajainen 제자리(in-place) 계산(확인 권장) |
| `huff_block_enc` / `huff_limit` 스택 | 2000 / 1072 B (실측) | 정적 작업 영역으로 |

- **ISR 에서 압축하지 않는다**: ISR 은 샘플을 링 버퍼(DS-M10)에 넣기만, 블록이 차면 로거 태스크가 압축 → 플래시. 허프만 표 계산(O(256²) 최악)은 결정적이지만 ISR 지연 예산엔 너무 길다.
- **블록 독립성**: 블록마다 델타 기준을 0 으로 다시 시작 → 블록 하나가 깨져도 다음 블록은 산다, 블록 단위 랜덤 접근 가능. 대가: 블록 첫 샘플이 절대값(온도 2150 → zz 4300 → 2 B).
- **최악 보장**: 모드 선택 + STORED 덕분에 블록 ≤ 4 + 2n. 플래시 페이지 예약을 이 값으로 하면 넘칠 일이 없다(무작위 int16 블록은 정확히 4 + 2·16384 로 STORED 가 됐다 — 테스트).

Cortex-M4 코드 생성 (`demo_m4.c`, `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -` 실제 출력 일부):

```text
zz16_enc:
	sxth	r1, r0
	lsls	r0, r0, #1
	eor.w	r0, r0, r1, lsr #15
	uxth	r0, r0
	bx	lr
zz16_dec:
	and	r1, r0, #1
	rsbs	r1, r1, #0
	eor.w	r0, r1, r0, lsr #1
	uxth	r0, r0
	bx	lr
```

zigzag 는 명령 4 개(시프트를 피연산자에 접는 Thumb-2 `eor.w … lsr #15`). `dzv_enc`(델타+zigzag+varint 루프)에서 컴파일러는 z < 65536 을 알아서 varint `while` 을 `cmp r7, #128` / `cmp.w r7, #16384` 두 비교로 펼쳤고, 루프를 4 배 펼쳤다(출력에서 `ldrh … sub.w … sxth … lsls … eor.w … asr #15 … uxth` 가 샘플마다 반복). 즉 델타+zigzag 는 샘플당 5 명령 정도라 병목은 허프만 쪽이다.

### Part C — 손상: 비트 하나가 뒤집히면

실제 출력:

```text
1-bit flips (3000 each): temp block err=2293 wrong=707 same=0 | accel block err=371 wrong=2629 same=0
```

- 범위 밖 쓰기: 0 회(카나리아 16 개 그대로, ASan/UBSan 깨끗). 잘린 접두사 전부와 1 바이트 덧붙임 전부 거부.
- 하지만 **안 잡힌 손상**이 온도(VRH) 24 %, 가속도(VH) **88 %**. 허프만은 비트가 하나 바뀌어도 금방 다시 동기화되고(self-synchronizing 경향), 기호 수·varint 개수가 우연히 맞으면 형식 검사로는 알 수 없다. RLE 를 거친 온도 블록은 길이가 어긋나기 쉬워 더 잘 잡힌다. 결론: **형식 검사는 무결성 검사가 아니다** → 블록마다 CRC-32(ALGO-M01/M02)를 붙여라. 그러면 안 잡힐 확률은 대략 2^−32 수준으로 떨어진다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution     # 60/60 passed, 0.36 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san -lm && ./solution_san   # 60/60, 보고 없음, 1.4 s
```

**버그 주입 확인** (solution.c 사본에 그럴듯한 버그 하나씩, 실제 결과):

| 주입한 버그 | 결과 | 잡은 검사 |
|---|---|---|
| zigzag mask 를 `0 − (d>>15)` 대신 `d>>15` | 50/60 | 전수 순열·공식, 모든 왕복 |
| 길이 제한 루프가 len == 15 도 늘림(`>=` → `>`) | 58/60 | 피보나치 max=16, 작은 L 검사 |
| RLE 디코더 런 길이 +2 (인코더는 +3) | 54/60 | RLE 왕복, 온도 블록 |
| varint 디코더가 비최소 인코딩 허용 | 58/60 | 거부 벡터, 무작위 유일성 |
| 정규 디코더 `first = (first + count) << 1` 에서 `+ count` 누락 | 54/60 | 블록 왕복 |
| 다채널 델타를 직전 샘플(다른 축)과 계산 — 인코더·디코더 둘 다 | 58/60 | **왕복은 통과**. 채널별 손 계산과 가속도 압축률(39.0 → 66.7 %)만 잡음 |
| 비트 쓰기 바이트 반전 + 읽기 LSB-first (서로 맞춘 뒤집기) | 55/60 | 알려진 바이트 `BF 0C`, 패딩 검사 |

## 7. 자주 하는 실수

**1. 델타를 int16 에 담고 끝.** `int16_t d = x[i] - x[i-1];` 은 범위 밖 변환이 구현 정의(C11 6.3.1.3p3)다. 더 나쁜 변형은 포화(saturate)시키거나 `int32` 델타를 `uint16` zigzag 에 넣는 것 — 정보가 사라진다. → uint16 산술로 mod 2^16 델타(또는 int32 로 넓혀 17비트 zigzag + varint ≤ 3 B).

**2. zigzag 에서 mask 대신 부호 비트 하나.** `(d << 1) ^ (d >> 15)` 는 음수에서 −2d − 1 이 아니라 2d+1 비슷한 엉뚱한 값을 낸다. 실제 주입: 50/60. → `0u - (d >> 15)` 로 0xFFFF 마스크를 만든다. `(int16_t)d >> 15` 는 음수 시프트라 구현 정의다.

**3. 섞인 채널을 1차원으로 델타.** x y z x y z 에서 x[i] − x[i−1] 은 z − y 같은 쓸모없는 차다. 왕복은 멀쩡히 통과해서 테스트가 "왕복만" 보면 못 잡는다. 실제: 가속도 39.0 % → 66.7 %. → `x[i] − x[i−nch]`, 그리고 손 계산 테스트.

```c
uint16_t prev = i >= (size_t)nch ? i16_bits(x[i - (size_t)nch]) : 0;   /* 같은 채널 */
```

**4. 관대한 varint 디코더.** 연속 비트가 계속되는 한 읽으면 손상 바이트열을 끝없이(또는 32/64비트 시프트 UB 까지) 읽고, `80 00` 같은 비최소 인코딩을 받아들여 같은 값의 바이트열이 여러 개가 된다. → 최대 바이트 수, 마지막 바이트 값 범위, 비최소 거부.

**5. RLE 의 런 길이 오프셋을 인코더/디코더에서 따로 쓴다.** `+3` 과 `+2` 가 갈리면 54/60. → `RLE_MIN_RUN` 상수 하나를 양쪽에서. 길이 2 런을 런으로 만드는 것도 흔한 실수: 크기가 안 줄고 리터럴만 끊긴다.

**6. 기호가 하나뿐인 블록.** 허프만 트리가 뿌리 하나라 길이 0 이 나오면 비트가 0 개 → 디코더는 몇 개인지 알 길이 없다. → 길이 1 을 준다(+ 헤더의 기호 수 m). 0 개도 따로(헤더만 4 B).

**7. 비트 순서를 한쪽만 바꾼다(또는 둘 다 바꿔 놓고 왕복만 테스트한다).** 쓰기와 읽기를 같이 뒤집으면 왕복은 통과할 수 있는데 다른 구현(PC 쪽 파서, JPEG/DEFLATE 규약)과 호환이 깨진다. → 알려진 바이트 벡터(`101 1 11110000 11` → `BF 0C`)로 고정.

**8. 길이 제한 후 Kraft 를 안 본다.** 15 로 자르기만 하면 Kraft > 1 → 정규 할당이 넘쳐 부호가 겹친다(`huff_canon` 이 -1). 늘리는 대상 조건을 `len > 15` 로 잘못 쓰면 16 비트 부호가 나온다(주입: fib20 max=16).

## 8. 면접 Q&A

<details><summary>Q1. zigzag 없이 바로 varint 하면 안 되나요?</summary>

음수의 2의 보수 패턴은 상위 비트가 전부 1 이라 varint 로 가장 길어진다. 16비트면 −1 = 0xFFFF → 3 B. protobuf 에서 `int32` 필드에 음수를 넣으면 64비트로 부호 확장해 인코딩하므로 10 바이트(64/7 올림)가 된다 — 그래서 음수가 흔한 필드엔 zigzag 를 쓰는 `sint32/sint64` 타입이 있다(DIST-B06). 꼬리: "부호 비트 + 크기(sign-magnitude)는?" → 0 이 두 개(+0/−0)라 전단사가 아니고, zigzag 는 크기 순서(0, −1, 1, −2 …)를 유지해 작은 |d| 가 작은 수가 된다.

</details>

<details><summary>Q2. int16 의 차는 17비트입니다. 어떻게 저장하죠?</summary>

두 방법. (a) **mod 2^16 델타**: 디코더도 mod 2^16 으로 더하므로 정확히 복원된다(군 Z/2^16 의 덧셈). 16비트에 들어가고 varint ≤ 3 B. 부수 효과로 INT16_MAX ↔ MIN 같은 감기는 점프가 ±1 이 된다. (b) int32 로 넓혀 zigzag32: 값 ≤ 131070(17비트), varint 역시 ≤ 3 B. 둘 다 맞다. 펌웨어에선 (a) 가 버퍼를 uint16 으로 유지해 RAM 이 반이다. 틀린 답은 "int16 에 넣는다"(구현 정의 변환) 와 "포화시킨다"(정보 손실). 꼬리: "센서가 포화되면?" → 그 자체로 int16 극값이 반복될 뿐 델타는 0 — 이 레슨 가속도 데이터에 포화 22 개가 있고 왕복 일치.

</details>

<details><summary>Q3. 허프만은 최적이라면서 왜 엔트로피(0.756 bit)에 못 미치나요?</summary>

허프만은 "기호마다 정수 비트 길이인 접두 부호" 중 최적이다. 그래서 H ≤ L̄ < H + 1 이고, 확률이 0.878 인 기호에도 최소 1 비트를 줘야 한다 → zz 에 바로 쓰면 ≥ 1 bit/샘플(10800 B). RLE 로 기호를 묶어(런 하나 = 여러 샘플) 기호당 정보를 늘리면 그 벽 아래(실측 0.874 bit/샘플)로 간다. 분수 비트를 쓰는 **산술 부호/범위 부호/ANS(tANS·rANS)** 는 H 에 훨씬 가깝게 가지만 상태·나눗셈(rANS)·표 크기 비용이 있다. 그리고 H0 는 0차 모델의 한계일 뿐: 이전 기호를 조건으로 하는 문맥 모델은 더 낮게 갈 수 있다(연습 3 의 0-런이 그 맛보기). 꼬리: "블록별 허프만이 전역 H0 보다 낮게 나온 이유?" → 블록마다 분포가 달라 표가 적응한 것(헤더 비용을 대신 낸다).

</details>

<details><summary>Q4. 정규(canonical) 허프만을 쓰는 이유는?</summary>

(1) 헤더에 **길이만** 저장하면 된다 — 트리 모양이나 부호값을 보낼 필요 없음(이 레슨 128 B 니블, JPEG DHT 는 길이별 개수 16 B + 기호 목록, DEFLATE 는 길이표를 다시 RLE+허프만으로 압축). (2) 디코더가 `count[len]` + `sym[]` 만으로 돈다(트리 포인터 없음, RAM 작음). (3) 같은 길이 부호가 연속이라 표 조회 디코더를 만들기 쉽다. (4) 인코더·디코더가 독립 구현이어도 같은 길이 → 같은 부호(결정적) — RFC 1951 예가 정확히 나오는지로 확인. 꼬리: "길이표가 Kraft 를 넘으면?" → 디코더가 거부해야 한다(`hdec_build` 의 left < 0). 안 그러면 손상 헤더로 표 범위 밖을 읽는다.

</details>

<details><summary>Q5. 길이를 15 로 제한하는 이유와 방법은? 최적인가요?</summary>

이유: 디코드 표/비트 누산기 크기, 헤더 필드 폭(4비트), 최악 디코드 시간 보장. 깊이는 빈도가 피보나치처럼 치우치면 n − 1 까지 간다(피보나치 20 개 → 19). 방법: 휴리스틱(자르고 Kraft 넘침을 가장 긴 짧은 부호를 늘려 갚기 — JPEG 표준 부록의 절차도 같은 계열, 확인 권장)은 결정적이고 간단하지만 최적이 아니다(작은 L 120 경우 중 6 경우 손해). 최적은 **package-merge**(Larmore–Hirschberg 1990, O(nL)) — 동전 수집 문제로 바꾼다(연습 1). 피보나치 20 에서도 차이는 0.06 %(46374 vs 46348 비트)였고, 이 레슨의 센서 블록 표 162 개는 제한 전 최대 깊이가 12 라 제한이 작동하지도 않았다. 꼬리: "한계를 너무 작게 하면?" → 기호 수 ≤ 2^L 이어야 가능, 그리고 손실이 커진다.

</details>

<details><summary>Q6. MCU 에서 정적 허프만(블록마다 표) vs 고정 표 vs 적응형, 무엇을 고르나요?</summary>

| | 블록마다 표 | 미리 학습한 고정 표 | 적응형(부호기·복호기가 같이 갱신) |
|---|---|---|---|
| 헤더 | 132 B/블록 | 0 (플래시 상수) | 0 |
| 인코드 | 두 번 훑기 | 한 번 | 한 번, 기호마다 갱신 비용 |
| 분포 변화 | 따라감 | 못 따라감(평활 필요) | 따라감 |
| 손상 | 블록 단위 격리 | 블록 단위 | 손상 뒤 상태가 어긋나 전부 망가짐 |

실측(연습 2): 온도 256 샘플 블록에서 블록별 표 14.3 % vs 고정 표 8.2 %, 4096 샘플에서도 7.1 vs 5.8 %. 소량·고빈도 로그(블록이 작아야 하는 경우)엔 고정 표가 압도적. 꼬리: "학습 데이터에 없던 바이트가 나오면?" → 모든 기호 빈도 +1(라플라스 평활)로 부호를 미리 배정, 또는 블록 헤더 플래그로 "이 블록은 자체 표" 폴백.

</details>

<details><summary>Q7. 플래시에 쓴 압축 로그의 비트 하나가 깨지면 어떻게 되나요?</summary>

측정: 디코더는 범위 밖 접근 없이 끝나지만, 비트 1 개 뒤집기의 24 %(온도 VRH) ~ 88 %(가속도 VH)는 **오류 없이 틀린 데이터**를 낸다. 허프만은 금방 재동기화되고 기호 수가 맞으면 형식 검사가 통과하기 때문. 대책: (1) 블록 독립(델타 기준 재시작) → 손상은 그 블록에 갇힘 (2) 블록마다 CRC-32(ALGO-M02) → 탐지 (3) NAND 라면 ECC 가 먼저 대부분을 고친다(Don 의 SSD/FTL 경험: 압축 데이터는 1 비트가 많은 샘플에 번지므로 ECC 실패 영향이 원본보다 크다) (4) 블록 헤더에 시퀀스 번호/타임스탬프 → 전원 차단 후 마지막 온전한 블록 찾기. 꼬리: "압축 전 CRC, 후 CRC?" → 압축 후 블록에 걸어야 플래시 손상을 디코드 전에 거른다. 압축 전(원본) CRC 를 하나 더 두면 코덱 버그까지 잡는다.

</details>

<details><summary>Q8. 압축하면 오히려 커지는 입력은? 최악 크기를 보장할 수 있나요?</summary>

모든 무손실 압축기에는 커지는 입력이 있다(비둘기집: n 바이트 입력 2^(8n) 개를 더 짧은 출력에 모두 1:1 로 보낼 수 없다). 그래서 **최악을 정하는 것**이 설계다: 블록마다 후보 중 최소를 고르고 STORED 를 포함하면 블록 ≤ 4 + 2n — 무작위 int16 블록이 정확히 그 크기였다. 단계별 상한도 안다: varint ≤ 3n, RLE ≤ m + ⌈m/128⌉, 허프만 ≤ 15·m 비트 + 132 B. 플래시 페이지·RAM 버퍼 예약을 이 상한으로 한다. 꼬리: "모드 바이트 없이?" → 압축 결과가 크면 원본을 쓰고 '압축 안 됨' 비트 하나는 어딘가 있어야 한다 — 0 비트로는 불가능(같은 비둘기집).

</details>

<details><summary>Q9. 이런 코덱은 어떻게 테스트하나요? 왕복이면 충분한가요?</summary>

아니다. 이 레슨의 실제 버그 주입에서 "다채널 델타를 다른 축과 계산" 은 **왕복이 통과**했고, "비트 순서를 양쪽 다 뒤집기" 도 왕복만으론 위험하다. 층별로: (1) 전수 검사(zigzag·varint 65536) (2) 알려진 답 벡터(varint 300 = AC 02, CLRS 224, RFC 1951 부호값, 비트 쓰기 BF 0C) (3) 기준 구현 대조(허프만 vs 전수 탐색 최적) (4) 성질 검사(Kraft, H ≤ L̄ < H+1, RLE 상한, 블록 ≤ 4+2n, 디코드 성공 ⇒ 재인코드 동일) (5) 무작위 왕복 (6) 손상/잘림 퍼징 + 카나리아 + ASan/UBSan (7) 압축률 회귀(가속도 < 50 % 같은 수치 기준). 꼬리: "실제 센서 데이터는?" → 필드 로그 한 묶음을 고정 테스트 벡터로 저장해 압축률 회귀를 막는다.

</details>

## 9. 추가 연습

### 연습 1 — package-merge: 최적 길이 제한 허프만

빈도와 한계 L 이 주어질 때 Σ f·len 이 최소이고 len ≤ L 인 길이를 구하라(Larmore–Hirschberg). 작은 경우 전수 탐색과 같은지, 본문 휴리스틱보다 나쁘지 않은지, 제한이 필요 없으면 허프만과 같은 비용인지 확인하라.

<details><summary>답 보기</summary>

각 기호를 레벨 1..L 에 "폭 2^−l 동전"으로 놓는 동전 수집 문제다. 레벨 L 목록 = 잎(빈도 오름차순). 레벨 j 목록 = 잎 ∪ (레벨 j+1 목록을 앞에서부터 둘씩 묶은 꾸러미), 무게순 병합. 레벨 1 목록의 앞 **2n − 2** 개를 고르고, 고른 꾸러미 P 개는 레벨 j+1 의 앞 2P 개를 고른 것으로 내려보낸다(꾸러미는 무게순 = 번호순으로 병합되므로 앞 P 개가 정확히 꾸러미 0..P−1). 기호 길이 = 고른 항목들에 그 잎이 나타난 횟수. 시간 O(nL), 이 구현은 레벨별 목록을 다 들고 있어 메모리도 O(nL). 실제 출력:

```text
ex1: package-merge == brute force on 120 cases; heuristic worse on 4
ex1: fib20 L=15: package-merge 46348 bits, heuristic 46374 bits
```

</details>

### 연습 2 — 미리 학습한 고정 표 (헤더 없음)

온도는 앞 6 시간, 가속도는 앞 2 분으로 RLE 바이트 빈도를 학습(모든 바이트 +1 평활)해 길이 ≤ 15 정규 표를 만들고, 나머지를 블록 256/1024/4096 샘플로 압축해 블록별 표 방식과 비교하라. 블록 형식은 `[0xF0][nch][nframes][RLE 바이트 수 u32][부호]`.

<details><summary>답 보기</summary>

```text
ex2: temp  block=  256 samples: raw 129600  per-block table  18512 (14.3%)  fixed table  10574 (8.2%)
ex2: temp  block= 1024 samples: raw 129600  per-block table  14726 (11.4%)  fixed table   8151 (6.3%)
ex2: temp  block= 4096 samples: raw 129600  per-block table   9251 (7.1%)  fixed table   7550 (5.8%)
ex2: accel block=  384 samples: raw 288000  per-block table 148800 (51.7%)  fixed table 113433 (39.4%)
ex2: accel block= 1536 samples: raw 288000  per-block table 118664 (41.2%)  fixed table 109943 (38.2%)
ex2: accel block= 6144 samples: raw 288000  per-block table 109981 (38.2%)  fixed table 109061 (37.9%)
```

고정 표가 모든 크기에서 이겼다. 헤더 132 B 가 사라지고, 학습 분포가 시험 분포와 비슷하기(같은 센서, 같은 생성기) 때문. 실제 제품에선 온도가 다른 환경(냉동고, 엔진룸)에 가면 분포가 바뀐다 → 평활 + "자체 표" 폴백 플래그. 표 256 B 길이(또는 니블 128 B)는 플래시 상수로 두고 디코더 쪽(PC)과 버전 번호로 맞춘다.

</details>

### 연습 3 — zigzag 영역의 0-런 부호

zz 값 스트림에서 0 이 아닌 v 는 `varint(v)`, 0 이 k 개(1..65536) 이어지면 `00 varint(k−1)`. 0x00 이 런 표시로 안전한 이유를 말하고 바이트 RLE 와 비교하라.

<details><summary>답 보기</summary>

0 이 아닌 v 의 varint 첫 바이트는 v < 128 이면 v ≠ 0, 아니면 연속 비트가 있어 ≥ 0x80 — 절대 0x00 이 아니다. 그래서 디코더는 0x00 을 보면 다음 varint 를 런 길이로 읽는다. `{0,0,0,7,0}` → `00 02 07 00 00`. 실제 출력:

```text
ex3: temp zero-run 21286 B vs varint+RLE 22086 B | +Huffman: 10855 B vs 12343 B
```

허프만을 얹으면 12 % 더 작다: 바이트 RLE 는 런 길이가 제어 바이트(0x80+r−3)에 섞여 분포가 퍼지는데, 0-런은 "0 런 길이"와 "±6/±7" 이 분리돼 치우침이 크다. 데이터의 구조(0 이 특별)를 아는 형식이 범용 형식보다 낫다는 예. JPEG 의 AC 계수 (run, size) 부호가 같은 발상이다(MEDIA-B05).

</details>

### 연습 4 — 2차 델타 (delta-of-delta)

Δ²x_i = x_i − 2x_{i−1} + x_{i−2} (mod 2^16). (a) 속도가 천천히 변하는 엔코더 위치(15비트 카운터)에서 zz(Δ) 와 zz(Δ²) 의 H0 를 비교하라. (b) 백색 잡음에서는 Var(Δ²)/Var(Δ) 가 얼마이고 엔트로피로 몇 비트 손해인가?

<details><summary>답 보기</summary>

(b) 독립 분산 σ² 이면 Var(Δ) = 2σ², Var(Δ²) = (1 + 4 + 1)σ² = 6σ² → 비율 3. 가우시안이면 미분 엔트로피는 ½log2(2πeσ²) 라 ½·log2 3 ≈ 0.792 비트 손해. 실제 출력:

```text
ex4: ramp H(zz d1)=6.294 H(zz d2)=1.601 bits | white noise H(zz d1)=5.634 H(zz d2)=6.419 (+0.785), Var(d2)/Var(d1)=2.984
```

예측기 차수는 신호에 맞춰야 한다: 매끄러운 위치·적분량엔 2차가 4.7 비트 이득, 잡음이 지배하는 가속도엔 손해. 드론 블랙박스(Betaflight 류)도 필드마다 다른 예측기(이전값, 직선 외삽 등)를 고른다고 알려져 있다(DRONE-M10, 확인 권장). 블록마다 예측기를 고르는 것은 이 레슨의 "모드 선택"과 같은 틀로 넣을 수 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 15/15 passed (solution.c 를 include)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san   # 15/15, 보고 없음
```

## 10. 핵심 정리

- 압축 = **모델 + 부호**. 델타가 모델(온도 H0 6.17 → 0.756 bit/샘플), varint·RLE·허프만은 부호. zigzag 는 엔트로피를 안 바꾸는 전단사 — varint 를 짧게 하는 게 일.
- int16 델타는 **mod 2^16** 으로: 디코더도 mod 2^16 덧셈이면 정확. 전부 uint16 산술로(int16 로의 범위 밖 변환은 구현 정의).
- varint 디코더는 엄격하게(잘림·3 B 초과·> 0xFFFF·비최소 거부) → 인코딩이 유일. RLE 최악 n + ⌈n/128⌉, 런은 ≥ 3 만.
- 허프만: 두 큐로 O(n), 교환 논증으로 최적, Kraft 합 = 1. H ≤ L̄ < H+1 이지만 **기호당 ≥ 1 비트** → 치우친 분포는 RLE/0-런으로 묶어야 벽을 넘는다.
- 길이 ≤ 15: 자르고 Kraft 넘침을 갚는 휴리스틱(가끔 손해) vs package-merge(최적, O(nL)). 정규 부호 → 헤더엔 길이만, 디코더는 count/sym 표만.
- 블록 = 모드 선택 + STORED 폴백 → 크기 ≤ 4 + 2n 보장. 블록마다 델타 재시작 → 손상 격리. 작은 블록엔 헤더가 비싸다 → 고정 표.
- 형식 검사 ≠ 무결성: 비트 1 개 뒤집힘의 24~88 % 가 조용히 틀린다 → 블록 CRC-32. 디코더는 손상 입력에서 범위 밖 접근 0(카나리아 + ASan).
- 테스트는 왕복만으론 부족: 다채널 델타 버그는 왕복을 통과했다. 전수·알려진 답·기준 대조·성질·압축률 회귀를 같이.

## 참고자료

- D. A. Huffman, "A Method for the Construction of Minimum-Redundancy Codes", *Proceedings of the IRE* 40(9), 1952 — 허프만 부호 원 논문.
- L. L. Larmore, D. S. Hirschberg, "A Fast Algorithm for Optimal Length-Limited Huffman Codes", *Journal of the ACM* 37(3):464–473, 1990 — package-merge, O(nL): <https://ics.uci.edu/~dhirschb/pubs/LenLimHuff.pdf>
- RFC 1951, *DEFLATE Compressed Data Format Specification 1.3*, §3.2.2 (정규 허프만 부호, 최대 15 비트, A~H 예): <https://www.rfc-editor.org/rfc/rfc1951>
- Protocol Buffers — Encoding (varint, ZigZag, `sint32`, 음수 `int32` 의 10 바이트): <https://protobuf.dev/programming-guides/encoding/>
- T. M. Cover, J. A. Thomas, *Elements of Information Theory*, 5장 — Kraft–McMillan, H ≤ L < H+1, 허프만 최적성.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, "Huffman codes" 절 — a:45 … f:5 예와 교환 논증.
- ISO/IEC 9899:2011 (C11) 6.3.1.3 — 부호 있는/없는 정수 변환(2^N 나머지 · 구현 정의).
- ITU-T T.81 (JPEG) 부록 K — 허프만 표 생성과 16 비트 길이 제한 절차. 절 번호는 기억에 의존 — 확인 권장.
- J. van Leeuwen, "On the construction of Huffman trees", ICALP 1976 — 정렬된 입력의 두 큐 O(n) 구성. 서지 기억에 의존 — 확인 권장.
- A. Moffat, J. Katajainen, "In-place calculation of minimum-redundancy codes", WADS 1995 — 제자리 길이 계산. 확인 권장.
- zlib `contrib/puff/puff.c` (Mark Adler) — count/symbol 표로 정규 부호를 푸는 참조 디코더. 세부 확인 권장.
- 관련 레슨: DIST-B06 (varint·zigzag·TLV), MEDIA-B05 (JPEG 허프만·런), ALGO-A05 (LZ77/LZSS), DRONE-M10 (블랙박스 예측 인코딩), ALGO-M01/M02 (CRC), DS-M10 (링 버퍼), DS-B12 (힙), ALGO-B12/B13 (비트 트릭), C-M14 (ISR 공유 데이터)

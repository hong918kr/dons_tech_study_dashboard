---
id: ALGO-A07
time: 110분
updated: 2026-11-11
---
# ALGO-A07 비트 트릭 고급: Gray 코드·SWAR·부분집합 순회

## 학습 목표

- Gray 변환 `g = b ^ (b >> 1)` 과 역변환(prefix-XOR, 32비트에서 5단계)을 구현하고, "이웃한 코드는 정확히 한 비트 차이"와 "5단계면 충분하다(순서도 상관없다)"를 **증명**한 뒤 16비트 전수 + 2^32 전 범위로 확인한다.
- 절대 엔코더(absolute rotary encoder)가 전이 중에 읽힐 때, 바이너리는 최대 2^(n−1)−1 칸 오판독, Gray 는 0 칸임을 **모든 전이 × 바뀌는 비트의 모든 부분집합**으로 시뮬레이션하고, 바이너리 오판독이 멀티턴 카운트를 **영구히 한 바퀴씩** 틀리게 만드는 것을 실측한다.
- SWAR(SIMD Within A Register) `haszero(v) = (v − 0x01010101) & ~v & 0x80808080` 이 "0 이 아님 ⇔ 0 바이트 있음" 임을 증명하고, **바이트별 플래그는 틀릴 수 있다**(0x00 바로 위 0x01)는 것을 2^32 전수로 세어(196607개) 유도값과 맞춘다. 가장 낮은 플래그는 정확하므로 ctz 로 첫 0 바이트를 찾는다.
- 워드 단위 strlen 의 "정렬된 과독(over-read)은 페이지를 넘지 않는다" 논리와 "그래도 ISO C 에서는 UB" 를 정직하게 말하고, 길이를 아는 버퍼에서는 과독 없는 워드 단위 검색을 쓴다.
- `sub = (sub − 1) & mask` 부분집합 순회(공집합 포함, 2^popcount 개, 모든 마스크 합 3^n)와 Gosper's hack(같은 popcount 의 다음 수, 32비트 꼭대기에서의 끝 조건)을 증명·구현하고 C(n,k) 와 n ≤ 20 전수 대조, CRC-8 이 못 잡는 k비트 오류 패턴 열거에 쓴다.

## 1. 문제

B12·B13 이 "비트 한두 개 다루기" 였다면, 이 레슨은 **워드 하나를 작은 칸 여러 개 또는 집합 하나로 보는** 기법이다 — 시니어 펌웨어 면접에서 "엔코더 값이 가끔 튄다", "UART 버퍼에서 줄바꿈을 빨리 찾아라", "가능한 조합을 전부 테스트하라" 같은 질문의 뼈대가 된다.

네 가지 도구를 만든다.

1. **Gray 코드** — 이웃한 두 수가 한 비트만 다른 코드. 기계적 엔코더처럼 여러 비트가 "거의 동시에" 바뀌는 장치에서 쓴다. 바이너리 0111→1000 은 4비트가 한꺼번에 바뀌어야 하는데, 트랙마다 미세한 정렬 오차가 있으면 그 사이 순간에 0000~1111 어떤 값으로도 읽힐 수 있다.
2. **SWAR 바이트 검사** — 32비트 레지스터를 바이트 4칸으로 보고 "0 인 칸이 있나"를 명령 3개로 판정한다. `strlen`·`memchr` 의 워드 단위 구현이 이 식을 쓴다.
3. **부분집합 순회** — 마스크의 부분집합을 큰 것부터 공집합까지 `sub = (sub − 1) & mask` 로 돈다. 비트마스크 DP(A03 TSP 가 쓴다)의 기본 연산.
4. **Gosper's hack** — popcount 가 같은 다음 수. "n 비트 중 k 개를 고르는 모든 조합"을 정수 증가 순서로 돈다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">4비트 엔코더가 7 → 8 로 넘어가는 순간 (바뀌는 비트 = 칠한 칸)</text>
  <text x="20" y="50" fill="currentColor">바이너리</text>
  <text x="20" y="80" fill="currentColor" font-family="monospace">7 = </text>
  <text x="20" y="115" fill="currentColor" font-family="monospace">8 = </text>
  <g stroke="currentColor">
    <rect x="70" y="62" width="34" height="26" class="f-warn"/><rect x="104" y="62" width="34" height="26" class="f-warn"/><rect x="138" y="62" width="34" height="26" class="f-warn"/><rect x="172" y="62" width="34" height="26" class="f-warn"/>
    <rect x="70" y="97" width="34" height="26" class="f-warn"/><rect x="104" y="97" width="34" height="26" class="f-warn"/><rect x="138" y="97" width="34" height="26" class="f-warn"/><rect x="172" y="97" width="34" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="87" y="80">0</text><text x="121" y="80">1</text><text x="155" y="80">1</text><text x="189" y="80">1</text>
    <text x="87" y="115">1</text><text x="121" y="115">0</text><text x="155" y="115">0</text><text x="189" y="115">0</text>
  </g>
  <text x="20" y="150" fill="currentColor">4비트가 각자 다른 순간에 바뀐다 → 2^4 = 16 가지 중 아무거나</text>
  <text x="20" y="170" fill="currentColor" font-family="monospace">0000(0) 0001(1) … 1111(15)</text>
  <text x="20" y="190" fill="currentColor">최악: 0 또는 15 로 읽힘 = 7칸 오차 (n비트면 2^(n−1)−1)</text>
  <line x1="370" y1="35" x2="370" y2="200" stroke="currentColor" stroke-dasharray="4 4"/>
  <text x="390" y="50" fill="currentColor">Gray</text>
  <text x="390" y="80" fill="currentColor" font-family="monospace">7 = </text>
  <text x="390" y="115" fill="currentColor" font-family="monospace">8 = </text>
  <g stroke="currentColor">
    <rect x="440" y="62" width="34" height="26" class="f-ok"/><rect x="474" y="62" width="34" height="26" fill="none"/><rect x="508" y="62" width="34" height="26" fill="none"/><rect x="542" y="62" width="34" height="26" fill="none"/>
    <rect x="440" y="97" width="34" height="26" class="f-ok"/><rect x="474" y="97" width="34" height="26" fill="none"/><rect x="508" y="97" width="34" height="26" fill="none"/><rect x="542" y="97" width="34" height="26" fill="none"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="457" y="80">0</text><text x="491" y="80">1</text><text x="525" y="80">0</text><text x="559" y="80">0</text>
    <text x="457" y="115">1</text><text x="491" y="115">1</text><text x="525" y="115">0</text><text x="559" y="115">0</text>
  </g>
  <text x="390" y="150" fill="currentColor">한 비트만 바뀐다 → 2 가지뿐</text>
  <text x="390" y="170" fill="currentColor" font-family="monospace">0100(→7) 또는 1100(→8)</text>
  <text x="390" y="190" fill="currentColor">최악: 0칸 오차 (옛 값 아니면 새 값)</text>
  <rect x="10" y="215" width="720" height="100" fill="none" stroke="currentColor"/>
  <text x="25" y="240" fill="currentColor">solution.c 실측 (8비트, 모든 전이 × 바뀌는 비트의 모든 부분집합):</text>
  <text x="25" y="262" fill="currentColor" font-family="monospace">binary worst 127칸, 이상한 판독 1792/2304 · gray worst 0칸, 0/512</text>
  <text x="25" y="284" fill="currentColor">멀티턴 추적에 10번째 샘플마다 전이 중 판독을 섞으면: binary 최종 −2바퀴(−512칸) 영구 오차,</text>
  <text x="25" y="304" fill="currentColor">gray 는 최대 1칸 순간 오차, 최종 0.</text>
</svg>
```

```svg
<svg viewBox="0 0 740 370" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">haszero(0xFFFF0100): 빌림(borrow)이 0x00 바이트에서 시작해 0x01 바이트에 거짓 플래그를 만든다</text>
  <g fill="currentColor" text-anchor="middle"><text x="250" y="46">byte 3</text><text x="350" y="46">byte 2</text><text x="450" y="46">byte 1</text><text x="550" y="46">byte 0 (메모리 맨 앞)</text></g>
  <g fill="currentColor"><text x="15" y="75">v</text><text x="15" y="115">v − 0x01010101</text><text x="15" y="155">~v</text><text x="15" y="195">&amp; 0x80808080 = haszero</text><text x="15" y="235">정확한 마스크</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="210" y="55" width="80" height="28"/><rect x="310" y="55" width="80" height="28"/><rect x="410" y="55" width="80" height="28"/><rect x="510" y="55" width="80" height="28"/>
    <rect x="210" y="95" width="80" height="28"/><rect x="310" y="95" width="80" height="28"/><rect x="410" y="95" width="80" height="28"/><rect x="510" y="95" width="80" height="28"/>
    <rect x="210" y="135" width="80" height="28"/><rect x="310" y="135" width="80" height="28"/><rect x="410" y="135" width="80" height="28"/><rect x="510" y="135" width="80" height="28"/>
    <rect x="210" y="175" width="80" height="28"/><rect x="310" y="175" width="80" height="28"/>
    <rect x="210" y="215" width="80" height="28"/><rect x="310" y="215" width="80" height="28"/><rect x="410" y="215" width="80" height="28"/>
  </g>
  <rect x="410" y="175" width="80" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="510" y="175" width="80" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="510" y="215" width="80" height="28" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="250" y="74">FF</text><text x="350" y="74">FF</text><text x="450" y="74">01</text><text x="550" y="74">00</text>
    <text x="250" y="114">FE</text><text x="350" y="114">FD</text><text x="450" y="114">FF</text><text x="550" y="114">FF</text>
    <text x="250" y="154">00</text><text x="350" y="154">00</text><text x="450" y="154">FE</text><text x="550" y="154">FF</text>
    <text x="250" y="194">00</text><text x="350" y="194">00</text><text x="450" y="194">80</text><text x="550" y="194">80</text>
    <text x="250" y="234">00</text><text x="350" y="234">00</text><text x="450" y="234">00</text><text x="550" y="234">80</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M 520 100 C 505 88, 500 88, 488 100"/><path d="M 420 100 C 405 88, 400 88, 388 100"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="482" y="92">빌림</text><text x="382" y="92">빌림</text></g>
  <text x="600" y="194" fill="currentColor" font-size="12">진짜 0 (정확)</text>
  <text x="15" y="270" fill="currentColor">byte 1 = 0x01 은 아래에서 빌림을 받아 0x01 − 1 − 1 = 0xFF,</text>
  <text x="15" y="290" fill="currentColor">~0x01 = 0xFE 도 최상위 비트가 1 → 거짓 플래그.</text>
  <text x="15" y="312" fill="currentColor">그래도 ① 플래그가 하나라도 있으면 0 바이트가 있다</text>
  <text x="15" y="332" fill="currentColor">② 가장 낮은 플래그(byte 0)는 항상 정확 → ctz/8 = 첫 0 바이트.</text>
  <text x="15" y="354" fill="currentColor">2^32 전수: 플래그가 틀린 워드 196607개 = 3·65536 − 1 (demo_exhaustive.c).</text>
</svg>
```

### Part A — Gray · 부분집합 순회 · Gosper (핵심 알고리즘)

- `gray_enc(b)`, `gray_dec(g)` — 역변환은 루프 31번이 아니라 시프트-XOR 5단계.
- `submask_list(mask, out, cap)` — 부분집합을 큰 것부터 공집합까지, 개수 반환(= 2^popcount).
- `next_same_popcount(x)` — Gosper. `x == 0` 이거나 32비트 안에 다음이 없으면 0.
- 테스트: Gray 16비트 전수(왕복·역함수·한 비트 차이·중복 없음) + 32비트 랜덤 2^18 개를 비트 단위 기준과 대조. n = 12 의 모든 마스크 4096개를 브루트 목록과 원소 단위 대조, Σ = 3^n (n = 0..15). Gosper 는 n = 1..20 의 모든 k 를 "한 번 훑기"로 전수 대조 + C(n,k) 개수 + 끝 조건, 32비트 랜덤은 루프 기준 구현과 대조.

### Part B — SWAR 바이트 검사 (증명과 함정)

- `haszero32`, `zero_bytes_exact`(바이트별로 정확한 마스크), `first_zero_le`(ctz/8), `hasvalue32`, `hasless32`.
- `strlen_word(s)` — 바이트로 4-정렬까지, 정렬된 워드 단위, 마지막 워드에서 위치 계산.
- `find_byte(buf, len, c)` — 길이를 아는 버퍼: 머리·몸통·꼬리, `buf[len]` 이후는 절대 읽지 않는다.
- 테스트: 바이트 값이 {00, 01, 02, 7F, 80, 81, FF, …} 16가지에서 나온 워드 65536개 + 랜덤 2^19 개(절반은 0x00/0x01 바이트를 일부러 섞음) 전부 바이트 단위 기준과 대조. hasless 는 n = 0..128 전부, n = 129 반례를 찾아 출력. strlen 은 시작 오프셋 0..7 × 길이 0..399, 0x01·0x80 이 많은 쓰레기 뒤에.

### Part C — 펌웨어: 엔코더 판독 · CRC 오류 패턴

- `encoder_misread_worst(nbits, gray, &bad, &total)` — 모든 전이 p→p+1 에서 바뀌는 비트의 **모든 부분집합**(Part A 순회)으로 판독을 만들어 최악 오차를 잰다.
- `enc_track(t, raw, nbits, gray)` — 멀티턴 위치 추적: 해독 → 부호 있는 최단 변화량 [−N/2, N/2) → 누적.
- `crc8_undetected(nbits, k, &patterns)` — 길이 nbits 코드워드의 무게 k 오류 패턴 C(nbits,k) 개를 Gosper 로 열거해 CRC-8(다항식 0x07, ALGO-M01)이 못 잡는 수를 센다. nbits = 32 는 Gosper 의 꼭대기 끝 조건을 그대로 시험한다.

## 2. Problem (English)

Implement the following word-level bit tricks in portable C11 (no compiler builtins inside the functions except the provided `ctz32`):

```c
uint32_t gray_enc(uint32_t b);                 /* binary -> reflected Gray */
uint32_t gray_dec(uint32_t g);                 /* inverse, O(log w) steps, no loop over bits */
uint64_t submask_list(uint32_t mask, uint32_t *out, uint64_t cap);   /* all submasks, descending, incl. 0 */
uint32_t next_same_popcount(uint32_t x);       /* Gosper; 0 if x == 0 or no successor in 32 bits */

uint32_t haszero32(uint32_t v);                /* nonzero iff some byte of v is 0 */
uint32_t zero_bytes_exact(uint32_t v);         /* 0x80 exactly in each zero byte */
unsigned first_zero_le(uint32_t v);            /* index of first zero byte in memory order (LE), 4 if none */
uint32_t hasvalue32(uint32_t v, uint8_t c);
uint32_t hasless32(uint32_t v, unsigned n);    /* some byte < n, valid for 0 <= n <= 128 */
size_t   strlen_word(const char *s);
size_t   find_byte(const uint8_t *buf, size_t len, uint8_t c);       /* must not read past buf[len-1] */

uint32_t encoder_misread_worst(int nbits, bool gray, uint64_t *bad, uint64_t *total);
int64_t  enc_track(enc_track_t *t, uint32_t raw, int nbits, bool gray);
uint64_t crc8_undetected(int nbits, int k, uint64_t *patterns);
```

Constraints: branch-free where possible, O(1) extra memory, no undefined behaviour for any input (`x == 0`, `x == 0xFFFFFFFF`, top-packed ones), little-endian target. Examples:

```text
gray_enc(7) = 0b0100, gray_enc(8) = 0b1100, gray_dec(0x80000000) = 0xFFFFFFFF
submask_list(0b10101) -> 10101 10100 10001 10000 00101 00100 00001 00000   (8 = 2^3)
next_same_popcount(0b0011100) = 0b0100011, next_same_popcount(0xF0000000) = 0
haszero32(0xFFFF0100) = 0x00008080  (byte 1 is a false flag), zero_bytes_exact = 0x00000080
first_zero_le(0x00FFFFFF) = 3, hasless32(0, 129) = 0  (n > 128 breaks)
```

Follow-ups: prove the Gray decode; when are haszero's per-byte flags wrong; is the word-at-a-time `strlen` legal C; total cost of iterating all submasks of all masks; what happens to Gosper at the top of the word.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 14군데. 기준 구현(비트·바이트 하나씩), `ctz32`(B12 에서 만든 것, 여기선 주어짐), 원형 거리 `cdist`, CRC 나머지 `poly_rem8`, 하네스는 주어진다. 스텁은 0(또는 `len`, 4)을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:205  gray_enc(0) == 0 && gray_enc(1) == 1 && gray_enc(2) == 3 && gray_enc(3) == 2 && gray_enc(4) == 6
gray 16-bit exhaustive: roundtrip bad=65535 inverse bad=65535 ref bad=65535 adjacent!=1bit=65536 dup=65280 wide=0
submask n=12: all 4096 masks vs brute, bad=4096, total visits=0 (3^12=531441)
gosper exhaustive n=1..20, every k: bad=2096920
swar: 589824 words: haszero iff bad=129902 exact bad=129902 first bad=129902 hasvalue bad=598753 | …
strlen_word: 400 cases (offset 0..7, len 0..399) bad=397
     8 |            0         0/0          |          0  0/0
…
5/39 passed
```

(exit 1, 0.13 초. 통과한 5개는 TODO 와 무관하거나 스텁이 우연히 맞는 것: 주어진 도우미 확인, "다음 없음 = 0" 경계(스텁이 늘 0), `flag_diff > 0`(스텁 0 이 기준과 다르니 참), 길이 0 검색, `poly_rem8` 확인.)

## 4. 힌트

<details><summary>힌트 1 — Gray 역변환: b_i 는 위쪽 g 비트 전부의 XOR</summary>

`g_i = b_i ^ b_{i+1}` 이므로 위에서부터 `b_31 = g_31`, `b_i = g_i ^ b_{i+1}`. 즉 b_i = g_i ⊕ g_{i+1} ⊕ … ⊕ g_31 (위로 누적 XOR). `g ^= g >> 1` 은 "자기 + 한 칸 위", 그 다음 `>> 2` 는 "두 칸 묶음 + 그 위 두 칸 묶음" … 칸이 1, 2, 4, 8, 16 으로 두 배씩 커진다(B12 SWAR popcount 와 같은 모양).

</details>

<details><summary>힌트 2 — sub − 1 은 "마스크 위치만으로 센 수" 에서 1 을 뺀다</summary>

`sub − 1` 은 sub 의 최하위 1 을 0 으로, 그 아래를 전부 1 로 만든다. `& mask` 로 마스크 밖 비트를 지우면, 마스크 위치만 모아 만든 p비트 수에서 정확히 1 을 뺀 것이 된다. 공집합(0)을 방문한 **뒤에** 멈춰야 2^p 개다 — 0 − 1 = 0xFFFFFFFF 이고 `& mask` 하면 다시 mask 로 돌아간다(무한 루프 주의).

</details>

<details><summary>힌트 3 — Gosper: 가장 낮은 "01" 을 "10" 으로</summary>

다음 수 = 최하위 1-덩어리(연속된 1)의 바로 위 0 을 1 로 올리고, 덩어리의 나머지(길이 − 1 개)를 맨 아래로 내린다. `c = x & -x` (덩어리 시작), `r = x + c` (덩어리 지우고 위 0 을 1 로). `r ^ x` 는 덩어리 + 새 비트 = 1 이 (길이+1)개. 그걸 `>> 2` 하고 `/ c`(= 덩어리 시작 위치만큼 오른쪽 시프트)하면 1 이 (길이 − 1)개 바닥에 남는다. 덩어리가 bit 31 까지 차 있으면 `r` 이 0 으로 넘친다.

</details>

<details><summary>힌트 4 — haszero 의 세 항</summary>

`v − 0x01..` : 각 바이트에서 1 을 빼면 0x00 만 0xFF 로 "넘어간다"(최상위 비트가 새로 1). `~v` : 원래 최상위 비트가 1 이던 바이트(0x80..0xFF)는 빼도 최상위 비트가 1 일 수 있으니 걸러 낸다. `& 0x80..` : 각 바이트의 최상위 비트만 본다. 바이트 사이로 번지는 빌림은 0x00 바이트에서만 시작한다 — 그 위쪽만 의심하라.

</details>

<details><summary>힌트 5 — 엔코더: 전이 중 판독 = 바뀌는 비트의 부분집합</summary>

`a = code(p)`, `b = code(p+1)`, `diff = a ^ b`. 판독 순간 diff 의 각 비트는 이미 바뀌었거나 아직이다 → 가능한 판독 = `a ^ sub` (sub ⊆ diff). Part A 의 순회를 그대로 쓴다. 멀티턴 추적에서 변화량은 `(cur − last) & mask` 를 `[−N/2, N/2)` 로 옮긴다(`d >= N/2` 이면 `d − N`).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (예: "버퍼에서 0 바이트를 빨리 찾아라") |
|---|---|
| 1. 질문 확인 | 정렬 보장? 길이를 아나(NUL 종료 vs 길이)? 엔디안? 워드 폭 32/64? 끝을 넘어 읽어도 되나(MPU·주변장치 영역)? |
| 2. 브루트 + 복잡도 | 바이트 루프 O(n), 바이트당 비교·분기 1번 |
| 3. 더 나은 아이디어 + 불변식 | 4(8)바이트를 한 번에: `haszero` 는 "0 바이트 있음 ⇔ 결과 ≠ 0", 가장 낮은 플래그는 정확 → ctz/8. 불변식: "지금까지 본 워드에는 0 바이트가 없다" |
| 4. 코드 | 머리(정렬까지 바이트) · 몸통(워드) · 꼬리(바이트, 길이를 알 때) |
| 5. 소리 내어 말할 테스트 | 길이 0, 시작 오프셋 0..3(7), 0x01·0x80·0xFF 바이트, 0x00 바로 위 0x01, 마지막 워드에 0 이 없음, 최상위 바이트가 0 |
| 6. 복잡도 | 시간 O(n/4) 워드 + O(1) 머리·꼬리, 추가 메모리 O(1), 스택 O(1). 과독 최대 3바이트(NUL 종료판) 또는 0(길이판) |

Gray·부분집합·Gosper 도 같은 틀이다: "정의 → 브루트(비트 루프, 전체 스캔) → 식 + 증명 → 경계(0, 꼭대기, 공집합) → 전수 대조".

### Part A — Gray 코드: 한 비트 성질과 역변환

**정의와 한 비트 성질.** `G(b) = b ^ (b >> 1)`. b → b+1 에서 b 의 뒤쪽 1 이 t 개면 `b ^ (b+1) = 2^(t+1) − 1` (아래 t+1 비트가 뒤집힘). G 는 GF(2) 위 선형이므로 `G(b) ^ G(b+1) = G(b ^ (b+1)) = (2^(t+1)−1) ^ (2^t − 1) = 2^t` — **정확히 한 비트**(bit t). 2^n − 1 → 0 (원형) 도 `G(2^n−1) = 2^(n−1)` 과 `G(0) = 0` 이 한 비트 차이라서 n비트 엔코더 디스크 한 바퀴가 끊김 없이 이어진다(16비트 전수에서 0xFFFF→0 포함 확인, 32비트는 2^32 전수).

**역변환 = 위로 누적 XOR.** `g_i = b_i ⊕ b_{i+1}` 을 위에서부터 풀면 `b_i = g_i ⊕ g_{i+1} ⊕ … ⊕ g_31`. 시프트 연산자 S(`x >> 1`)로 쓰면 G = I + S (GF(2) 위), 역은 I + S + S² + … + S³¹ (S³² = 0 이므로 (I+S)(I+S+…+S³¹) = I + S³² = I). 그리고

  (I + S)(I + S²)(I + S⁴)(I + S⁸)(I + S¹⁶) = Σ_{j=0}^{31} S^j

(곱을 전개하면 지수 0..31 이 2진 표기로 정확히 한 번씩 나온다). 그래서 `g ^= g>>16; g ^= g>>8; … g ^= g>>1` 다섯 줄이 역변환이다. 다섯 인수는 모두 S 의 다항식이라 **서로 교환** — 순서를 1, 2, 4, 8, 16 으로 바꿔도 같은 결과다(실제로 바꾼 변형을 돌리면 39/39). 반대로 `>> 16` 한 줄을 빼면 16비트 전수 테스트는 **전부 통과**하고 32비트 랜덤·`gray_dec(0x80000000)` 에서만 잡힌다(실제 변형 37/39, 7. 실수 2). 테스트 범위가 구현 폭보다 좁으면 이렇게 놓친다.

손 계산 (8비트, g = 0b1101_0110 = 0xD6):

| 단계 | 값 | 의미 |
|---|---|---|
| 시작 g | 1101 0110 | |
| `g ^= g >> 4` (16·8 은 8비트에서 0) | 1101 1011 | 4칸 묶음 누적 |
| `g ^= g >> 2` | 1110 1101 | 2칸 |
| `g ^= g >> 1` | 1001 1011 = 0x9B | 완료 |
| 확인 G(0x9B) = 0x9B ^ 0x4D | 1101 0110 = 0xD6 ✓ | |

Cortex-M4 코드(`demo_asm.c` 실제 출력): `gray_enc` = `eor.w r0, r0, r0, lsr #1` **한 명령**, `gray_dec` = `eor.w r0, r0, r0, lsr #16/#8/#4/#2/#1` 다섯 명령 — 배럴 시프터가 두 번째 피연산자를 공짜로 민다. M0+ 는 시프트 피연산자가 없어 `lsrs` + `eors` 열 명령.

### Part A — 부분집합 순회: 왜 `(sub − 1) & mask` 인가

마스크 위치를 p_0 < p_1 < … < p_{k−1} 이라 하고, 부분집합 sub 를 "그 위치의 비트만 모은 k비트 수" σ(sub) 로 보자(압축 좌표). 주장: `σ((sub − 1) & mask) = σ(sub) − 1` (sub ≠ 0).

증명: sub 의 최하위 1 이 위치 p_j 에 있다. `sub − 1` 은 그 비트를 0 으로, 그 **아래 모든 비트**를 1 로 만들고 위는 그대로 둔다. `& mask` 하면 아래 비트 중 마스크 위치 p_0..p_{j−1} 만 1 로 남는다. 압축 좌표에서 보면 "최하위 1(j번)을 0 으로, 그 아래 j 개를 1 로" = 정확히 1 감소. ∎ 따라서 mask(σ = 2^k − 1)에서 시작해 0 까지 **각 부분집합을 한 번씩, 감소 순서로** 방문한다. 압축 좌표의 대소와 원래 수의 대소는 같으니 원래 값으로도 감소 순서다(테스트가 `subs[j] < subs[j−1]` 를 확인).

```svg
<svg viewBox="0 0 740 275" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mask = 1 0 1 0 1 의 부분집합 순회 = 압축 좌표 3비트 카운터를 7 → 0 으로 내리기</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="50">sub (5비트)</text><text x="170" y="50">σ(sub)</text><text x="260" y="50">다음 = (sub − 1) &amp; mask</text>
    <text x="20" y="75">1 0 1 0 1</text><text x="170" y="75">111 (7)</text><text x="260" y="75">10100 &amp; 10101 = 10100</text>
    <text x="20" y="97">1 0 1 0 0</text><text x="170" y="97">110 (6)</text><text x="260" y="97">10011 &amp; 10101 = 10001</text>
    <text x="20" y="119">1 0 0 0 1</text><text x="170" y="119">101 (5)</text><text x="260" y="119">10000 &amp; 10101 = 10000</text>
    <text x="20" y="141">1 0 0 0 0</text><text x="170" y="141">100 (4)</text><text x="260" y="141">01111 &amp; 10101 = 00101</text>
    <text x="20" y="163">0 0 1 0 1</text><text x="170" y="163">011 (3)</text><text x="260" y="163">… 00100, 00001</text>
    <text x="20" y="185">0 0 0 0 0</text><text x="170" y="185">000 (0)</text><text x="260" y="185">방문한 뒤 멈춤 (0−1 = FFFFFFFF → 다시 mask)</text>
  </g>
  <rect x="15" y="130" width="130" height="16" class="f-acc" opacity=".35"/>
  <rect x="520" y="60" width="200" height="90" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="530" y="80">10000 − 1 = 01111:</text><text x="530" y="98">최하위 1 을 0 으로,</text><text x="530" y="116">그 아래를 전부 1 로</text><text x="530" y="134">→ &amp; mask = 압축 좌표 −1</text>
  </g>
  <text x="15" y="218" fill="currentColor">방문 수 = 2^popcount(mask) = 8. 모든 mask ⊆ {0..n−1} 의 합 = 3^n</text>
  <text x="15" y="240" fill="currentColor">(원소마다 mask 밖 / mask 안·sub 밖 / 둘 다 — 3가지).</text>
  <text x="15" y="262" fill="currentColor">실측: n = 12 → 531441 = 3^12, n = 15 → 14348907 = 3^15 (solution.c).</text>
</svg>
```

**Σ = 3^n.** (mask, sub) 쌍, sub ⊆ mask ⊆ [n] 의 개수: 원소 i 마다 (i ∉ mask) / (i ∈ mask, i ∉ sub) / (i ∈ sub) 세 가지 → 3^n. 그래서 "모든 마스크의 모든 부분집합" DP 는 O(3^n): n = 15 에서 1435만, n = 20 에서 35억 — 펌웨어 오프라인 도구에서도 n ≈ 15~16 이 현실적 상한이다. 부분집합 **합**만 필요하면 비트마다 한 번 훑는 SOS(sum over subsets) DP 가 O(n·2^n)(연습 3: n = 14 에서 114688 덧셈 vs 4782969 방문).

**공집합.** 루프는 "방문 → 0 이면 멈춤 → 다음" 순서여야 한다. `while (sub) { …; sub = (sub−1)&mask; }` 는 공집합을 빼먹고(2^p − 1 개), `for (sub = mask; sub >= 0; …)` 는 unsigned 라 무한 루프다. mask = 0xFFFFFFFF 는 2^32 번 — 개수를 `uint32_t` 로 세면 0 으로 넘친다(그래서 반환형이 `uint64_t`; demo 에서 4294967296 확인).

### Part A — Gosper's hack: 유도와 꼭대기

같은 popcount 에서 x 다음으로 큰 수 y: x 의 최하위 1-덩어리가 위치 t 부터 m 개(비트 t..t+m−1)라 하자. 더 큰 수가 되려면 어떤 0 을 1 로 올려야 하고, 가장 작은 증가는 "덩어리 바로 위 0(위치 t+m)을 1 로 + 덩어리의 나머지 m−1 개를 맨 아래(0..m−2)로" 이다. 식이 정확히 이것을 한다:

| 식 | x = 0011100 (t=2, m=3) | 의미 |
|---|---|---|
| `c = x & -x` | 0000100 | 2^t |
| `r = x + c` | 0100000 | 덩어리가 자리올림으로 사라지고 bit t+m 이 1 |
| `r ^ x` | 0111100 | 비트 t..t+m, 1 이 m+1 개 |
| `>> 2` | 0001111 | m+1 개를 t−2 .. t+m−2 로 |
| `/ c` (= `>> t`) | 0000011 | m−1 개가 바닥에 |
| `\| r` | 0100011 | 답 |

**꼭대기.** 덩어리가 bit 31 까지 차 있으면(t+m = 32) `x + c` 가 2^32 으로 넘쳐 **r = 0**. 이때 x 는 popcount 가 같은 32비트 수 중 최대(위쪽 m 비트가 전부 1)이고, 반대로 최대값은 항상 이런 모양이다. 그래서 `r == 0` ⇔ "다음 없음" — 이 검사를 빼면 0xF0000000 의 다음이 3 같은 쓰레기가 된다(실제 변형 35/39). 또 `x == 0` 이면 c = 0 → **0 으로 나누기**(C 에서 UB. Cortex-M3/M4 는 CCR.DIV_0_TRP 가 꺼져 있으면 UDIV 결과 0, 켜져 있으면 UsageFault). n < 32 비트 우주에서는 호출자가 `y >= 2^n` 에서 멈춘다(CRC 열거에서 nbits = 32 일 때만 0 이 끝 신호).

**나눗셈은 사라진다.** c 가 2의 거듭제곱이라는 걸 clang 이 알아서, `/ c` 를 `ctz` 시프트로 바꿨다 — 이 맥 arm64 `-O2`: `neg/and/adds/eor/lsr/rbit/clz/lsr/orr/csel` 분기 없는 10명령, **udiv 없음**. Cortex-M4 `-O2` 도 `rbit` + `clz` + `lsr.w`, udiv 없음(같은 파일 `-O0` 에서는 udiv 1개). M0+ 는 `rbit/clz` 도 없어서 de Bruijn 곱셈 `* 0x077CB531 >> 27` + 32바이트 표로 ctz 를 만들었다(B12 연습 4 와 같은 상수). 그래서 `demo_bench.c` 의 "udiv 판 vs ctz 판" 은 같은 코드라 시간이 같다(2.32 vs 2.50 ns, 잡음 범위).

**순서.** Gosper 는 k-조합을 **정수 증가 순서** = colex(co-lexicographic) 순서로 돈다. 그래서 조합의 순위(rank)는 Σ C(p_i, i+1) (조합 수 체계, 연습 5에서 n = 20 전 k 확인). 전 범위 확인: 33개 사슬(k = 0..32)이 2^32 개 값을 정확히 한 번씩 방문, k = 16 사슬 길이 601080390 = C(32,16) (`demo_exhaustive.c`).

### Part B — haszero: 증명과 "바이트별 플래그는 틀린다"

바이트 b_0(최하위)..b_3, 빌림 β_0 = 0. `v − 0x01010101` 의 바이트 i 결과 r_i = b_i − 1 − β_i (mod 256), 빌림 β_{i+1} = [b_i < 1 + β_i]. 플래그 f_i = bit7(r_i) ∧ ¬bit7(b_i).

1. **0 바이트가 없으면 결과 0.** 모든 b_i ≥ 1 → 귀납으로 β_i = 0 → r_i = b_i − 1. f_i = 1 이려면 b_i ≤ 0x7F 이면서 b_i − 1 ≥ 0x80 — 불가능. ∎
2. **0 바이트가 있으면 결과 ≠ 0, 그리고 가장 낮은 플래그가 정확.** j = 가장 낮은 0 바이트. 그 아래는 1 에 의해 β_j = 0, r_j = 0xFF, b_j = 0 → f_j = 1. 아래쪽 플래그는 1번 논리로 0. 그러므로 **ctz(h)/8 = j** — little-endian 에서 메모리상 첫 0 바이트. ∎
3. **위쪽 거짓 플래그.** 빌림을 받은(β_i = 1) 바이트는 r_i = b_i − 2: f_i = 1 ⇔ b_i ∈ {0x00, 0x01}. 빌림은 0x00 과 0x01 을 지나며 계속된다. 그래서 거짓 플래그 = "0x00 바로 위로 이어지는 0x01 들". 바이트별 플래그가 정확한 마스크와 다른 워드 ⇔ 인접한 (b_i = 0x00, b_{i+1} = 0x01) 쌍이 있다 (i = 0,1,2). 포함-배제: 각 사건 2^16 개, i=0 과 i=1 (b_1 이 0x01 이면서 0x00) 및 i=1 과 i=2 는 동시 불가, i=0 과 i=2 는 `01 00 01 00` 한 워드 → **3·65536 − 1 = 196607**. `demo_exhaustive.c` 2^32 전수 실측도 196607.

`~v` 항이 왜 필요한가: 없으면 0x81 → 0x80 처럼 0x81..0xFF 바이트가 최상위 비트를 그대로 남겨 거짓 양성. 정확한 마스크가 필요하면(바이트 개수 세기, big-endian 에서 첫 0 = 최상위 쪽 → clz):

```c
~(((v & 0x7F7F7F7F) + 0x7F7F7F7F) | v | 0x7F7F7F7F)   /* 0 인 바이트에만 0x80 */
```

각 바이트에서 `(b & 0x7F) + 0x7F ≤ 0xFE` 라 칸 사이 자리올림이 없고, 그 bit7 = [b & 0x7F ≠ 0], `| v` 로 bit7 = [b ≠ 0], 나머지 7비트를 1 로 채운 뒤 반전 → 0x80 ⇔ b = 0. 연산 하나 더(and·add·or·or·not) 대신 바이트마다 정확하다. 연습 2 에서 haszero 플래그로 '\n' 을 세면 "\n" 바로 뒤 0x0B('\n' ^ 0x01) 때문에 200개 버퍼에서 225개를 더 셌다(실측).

Cortex-M4 (`demo_asm.c` 실제 출력): `haszero32` = `sub.w r1, r0, #16843009` / `bic.w r0, r1, r0` / `bic r0, r0, #2139062143` — 상수 0x01010101 과 0x7F7F7F7F 가 Thumb-2 수정 즉시값(modified immediate)이라 **3명령**, 리터럴 로드 없음. `first_zero_le` 는 + `rbit` / `clz` / `lsrs #3` — h = 0 이면 clz(rbit(0)) = 32 → 32 >> 3 = 4 라서 C 의 `h ? … : 4` 삼항식이 **분기 없이** 사라졌다. M0+ 는 즉시값이 안 되어 `ldr` 리터럴 2개(`0xFEFEFEFF` 를 더함 = 0x01010101 빼기).

### Part B — hasvalue · hasless

`hasvalue(v, c) = haszero(v ^ (0x01010101 · c))` — c 와 같은 바이트만 0 이 된다. **hasless(v, n) = (v − 0x01010101·n) & ~v & 0x80808080** 은 "어떤 바이트 < n" 이고 0 ≤ n ≤ 128 에서만 맞다. 증명은 haszero 와 같은 틀: 빌림 없는 바이트에서 b ≥ n 이면 r = b − n ≤ b, b < 0x80 이면 bit7 = 0 (b ≥ 0x80 은 `~v` 가 거름) → 플래그 없음. b < n ≤ 128 이면 r = b − n + 256 ∈ [256 − n, 255] ⊆ [128, 255] → bit7 = 1, b < 128 → 플래그. 빌림은 b < n 인 바이트에서만 시작하므로 가장 낮은 플래그는 정확. n = 129 면 b = 0 에서 r = 256 − 129 = 127, bit7 = 0 → **놓친다**: solution.c 가 찾은 첫 반례가 바로 v = 0x00000000 (특수 워드 65536개 중 2138개에서 틀림). 출처인 Bit Twiddling Hacks 도 조건을 "0<=n<=128" 로 적는다.

### Part B — 워드 단위 strlen: 정렬, 페이지, 그리고 UB

```c
while ((uintptr_t)p & 3u) { if (*p == 0) return p - s; p++; }   /* 머리: 바이트 */
for (;;) { uint32_t w; memcpy(&w, p, 4);                         /* 몸통: 정렬된 워드 */
           if (haszero32(w)) return (p - s) + first_zero_le(w); p += 4; }
```

**왜 실제로는 안전한가.** 정렬된 4바이트 워드는 4의 배수 경계에서 시작하므로 페이지(4 KiB/16 KiB, 모두 4의 배수) 경계를 **넘지 않는다**. NUL 바이트가 있는 페이지는 접근 가능하니 같은 워드의 나머지(최대 3바이트)도 폴트가 나지 않는다. Cortex-M MPU 영역도 최소 32바이트·크기 정렬(ARMv7-M 기준)이라 정렬 워드가 영역을 가르지 않는다. musl 의 `strlen.c` 가 정확히 이 구조다(`HASZERO(x) ((x)-ONES & ~(x) & HIGHS)`, `typedef size_t __attribute__((__may_alias__)) word;` 로 앨리어싱 규칙을 피하고 GNUC 에서만 켠다).

**왜 ISO C 에서는 UB 인가.** 마지막 워드가 객체(문자열이 든 배열/할당) **끝을 넘어** 읽을 수 있다. 객체 밖 접근은 정의되지 않은 동작이고, 도구는 그대로 잡는다 — 6바이트 `malloc` 에 "hello" 를 넣고 돌리면 ASan 실제 출력:

```text
==18554==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000977 …
READ of size 4 at 0x602000000977 thread T0
0x602000000977 is located 1 bytes after 6-byte region [0x602000000970,0x602000000976)
```

그래서 solution.c 테스트는 문자열을 512바이트 정렬 배열 `spool` **안에** 두고(과독이 같은 객체 안이라 합법), libc 는 sanitizer 가 `strlen` 을 가로채서(interceptor) 문제가 안 보일 뿐이다. 정직한 답: "libc 구현은 플랫폼 보장(페이지·정렬)에 기대어 표준 밖에서 한다. 내 코드라면 **길이를 아는 API**(`find_byte`, `memchr`, `strnlen`)를 쓰고 과독을 0 으로 만든다." 펌웨어에서 하나 더: 버퍼 바로 뒤가 **읽으면 상태가 바뀌는 레지스터**(FIFO 데이터 레지스터 등)인 메모리 맵에서는 과독이 부작용이다 — 문자열이 그런 곳에 있을 일은 드물지만, DMA 버퍼 검색 코드를 주변장치 창에 재사용하면 생길 수 있다.

`memcpy(&w, p, 4)` 는 `*(uint32_t *)p` 의 엄격 앨리어싱 위반을 피하면서 정렬된 주소면 `ldr` 한 번으로 컴파일된다. 정렬을 안 맞추면: M3/M4 의 `LDR` 은 비정렬도 되지만(CCR.UNALIGN_TRP 가 꺼져 있을 때, 느림), M0/M0+ 는 비정렬 워드 접근이 HardFault 다.

### Part C — 절대 엔코더: 오판독과 멀티턴

모델: n비트 디스크가 p → p+1 로 넘어갈 때 트랙(비트)마다 전환 순간이 조금씩 다르다. 판독은 모든 비트를 한 순간에 래치하므로, 바뀌는 비트 집합 `diff = code(p) ^ code(p+1)` 의 **임의의 부분집합**만 바뀐 상태로 읽힐 수 있다. 오차 = 해독값과 {p, p+1} 사이 원형 거리의 최솟값. solution.c 실제 출력:

```text
  bits | binary worst  bad/total            | gray worst  bad/total
     4 |            7        48/80         |          0  0/32
     8 |          127      1792/2304       |          0  0/512
    10 |          511      9216/11264      |          0  0/2048
    12 |         2047     45056/53248      |          0  0/8192
```

- 바이너리 최악 = 2^(n−1) − 1: 전이 2^(n−1)−1 → 2^(n−1) 에서 n비트가 전부 바뀌어 0 이나 2^n−1 로 읽힌다(n = 2..12 전부 이 값인지 CHECK).
- 판독 경우의 총수도 셀 수 있다: 뒤쪽 1 이 t 개인 p 는 2^(n−t−1) 개, 각자 2^(t+1) 경우 → t 마다 2^n, 여기에 원형 전이 2^n−1 → 0 의 2^n → **(n+1)·2^n** (n=8: 2304 ✓). 그중 "옛 값/새 값" 2개를 뺀 (n−1)·2^n 이 전부 이상한 판독이다. Gray 는 전이마다 2 경우뿐 → 2·2^n, 이상한 판독 0.

**멀티턴 추적.** 한 바퀴 안 값만 주는 엔코더로 누적 위치를 만들려면 샘플마다 `d = (cur − last) & mask` 를 `[−N/2, N/2)` 로 옮겨 더한다. 샘플 사이 실제 이동이 N/2 미만이면 방향이 유일하다(그 이상이면 앞으로 N/2+1 과 뒤로 N/2−1 을 구별할 수 없다 — 샘플링 정리와 같은 이야기). 경계: 정확히 N/2 는 뒤로(−N/2) 보낸다(`d >= N/2`); `>` 로 쓰면 경계 테스트가 FAIL(실제 변형 38/39).

여기서 **바이너리 오판독은 순간 오차로 끝나지 않는다.** 변화량 둘(last → 오판독 d, d → 다음 정상값)이 각각 [−N/2, N/2) 로 접히면서, d 가 반 바퀴 가까이 튀면 두 접힘의 합이 실제 변화량과 **N 만큼** 달라진다 — 한 바퀴를 영구히 잃거나 얻는다. 8비트, |속도| ≤ 60칸/샘플, 200000 샘플, 10번째 샘플마다 전이 중 판독(19841회) 실측:

```text
  multiturn gray  : 19841 misreads, clean mismatches=0, max |err|=1, final err=0 (= 0 turns)
  multiturn binary: 19841 misreads, clean mismatches=0, max |err|=1048, final err=-512 (= -2 turns)
```

Gray 는 해독값이 항상 p 나 p+1 이라 최대 1칸 순간 오차, 다음 샘플에서 사라진다(합이 망원 급수로 정확). 로봇 관절이라면 바이너리 쪽은 "−2바퀴" — 기어비를 곱하면 치명적이다.

현실 메모(확인 권장): 광학식 절대 엔코더는 디스크 트랙을 Gray 패턴으로 만드는 것이 고전적 설계이고, SSI 인터페이스 엔코더는 출력 형식을 Gray/바이너리 중 고르는 제품이 많다. 자기식 엔코더 IC(각도를 내부에서 계산해 SPI 로 바이너리를 주는 종류)는 이 문제를 칩 안에서 처리한다 — 그래도 **여러 레지스터를 나눠 읽는** 경우(상위·하위 바이트 따로) 같은 "전이 중 판독" 이 생기니 래치/더블 리드 규칙을 데이터시트에서 확인한다. 클록 도메인 사이 FIFO 포인터에 Gray 를 쓰는 이유도 같다(ARCH-A07 이 다룬다).

### Part C — CRC-8 이 못 잡는 오류 패턴 (Gosper 열거)

CRC(초기값 0, xorout 0)는 GF(2) 선형이라 "오류 패턴 e(x) 가 생성 다항식 g(x) 로 나눠떨어지면 못 잡는다". 32비트 코드워드(데이터 24 + CRC 8)에서 무게 k 인 e 를 Gosper 로 C(32,k) 개 전부 돈다. 첫 패턴은 `2^k − 1`, 끝은 `next_same_popcount` 가 0 을 돌려줄 때(꼭대기 조건이 바로 여기서 쓰인다). n = 9..20 에서는 2^n 전수 브루트와 개수를 대조했다. 실제 출력:

```text
CRC-8 poly 0x07, 32-bit codeword (24 data + 8 crc): undetected / patterns
  weight 1: 0 / 32
  weight 2: 0 / 496
  weight 3: 0 / 4960
  weight 4: 284 / 35960
```

읽는 법: g(x) = x⁸ + x² + x + 1 은 x = 1 에서 1+1+1+1 = 0 이라 (x+1) 을 인수로 가진다 → **홀수 무게 오류는 전부** 잡는다(무게 1, 3 = 0). 무게 2 오류 x^i(x^d + 1) 은 x^d ≡ 1 (mod g) 일 때만 못 잡는데, 이 레슨 작업용 계산에서 x 의 위수(order)는 127 → 코드워드 127비트 이하면 무게 2 도 전부 잡는다. 그래서 이 길이의 해밍 거리(HD)는 4: 무게 4 패턴 35960개 중 284개(0.79%)가 통과한다. 다항식별 HD 표(Koopman 의 CRC 표)와 맞춰 보는 것은 추가 확인 권장. M01 은 CRC 엔진 자체를, 이 레슨은 "가능한 오류를 전부 열거"하는 도구를 맡는다.

### 복잡도 정리

| 함수 | 시간 | 추가 메모리 | 비고 |
|---|---|---|---|
| `gray_enc` / `gray_dec` | O(1) / O(log w) = 5단계 | 0 | M4: 1 / 5 명령 |
| `submask_list` | O(2^popcount) | 0 (+ 출력) | 모든 마스크 합 O(3^n) |
| `next_same_popcount` | O(1) | 0 | M4 약 12명령, 사슬 전체 C(n,k) 단계 |
| `haszero32` / `first_zero_le` | O(1) | 0 | M4: 3 / 6 명령, 분기 없음 |
| `strlen_word` / `find_byte` | O(n/4 + 1) | 0 | 과독 ≤ 3B / 0B |
| `encoder_misread_worst` | O((n+1)·2^n) 판독 | 0 | 바이너리 기준 |
| `enc_track` | O(1)/샘플 | 상태 16B | 이동 < N/2 /샘플 필요 |
| `crc8_undetected` | O(C(n,k) · 24) | 0 | `poly_rem8` 비트 루프 |

### 전 범위 — `demo_exhaustive.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_exhaustive.c -o demo_exhaustive && ./demo_exhaustive
```

이 맥(Apple M6, Apple clang 21.0.0), 실행 직전 `uptime` load averages **129.28 65.01 31.60** (다른 작성 에이전트 10개가 동시에 빌드 중 — 시간은 참고만). 실제 출력:

```text
gray   : 2^32 values, roundtrip bad=0, adjacent-not-1-bit=0  (12.86 s)
haszero: 2^32 words, iff bad=0, first-zero(ctz) bad=0, per-byte flags differ in 196607 words (derived 3*65536-1=196607)  (22.77 s)
submask: mask=0xFFFFFFFF visited 4294967296 (2^32=4294967296), xor=00000000  (2.05 s)
gosper : k=16 chain length 601080390 (C(32,16)=601080390), last=0xFFFF0000
gosper : 33 chains, total visits 4294967296 (2^32=4294967296), bad=0, xor of all=00000000  (11.14 s)
```

B12 의 함정(양쪽이 같은 명령이 되어 컴파일러가 루프를 지움)은 여기선 없다 — 각 줄이 0.00 초가 아니라 수 초 걸렸다. 기준은 바이트·비트 루프라 haszero 와 같은 명령이 될 수 없다.

### 벤치마크 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

워밍업 1 + 7회 중앙값 [최소..최대], ns/호출. 바이트 루프에는 빈 `asm volatile` 장벽을 넣어 clang 이 strlen/memchr 관용구로 바꾸지 못하게 했다. load averages **90.14 73.10 38.38** — 최대값이 튀는 칸이 많으니 **비율**만 볼 것 (첫 실행, 실제 출력에서 열 정리):

| len | strlen 바이트 | strlen 워드(4B) | libc strlen | 바이트/워드 | 워드/libc |
|---|---|---|---|---|---|
| 15 | 6.6 | 3.1 | 1.7 | 2.15 | 1.81 |
| 255 | 62.7 | 16.7 | 5.6 | 3.74 | 2.98 |
| 4095 | 1014.4 | 302.2 | 71.9 | 3.36 | 4.20 |
| 65535 | 15371.0 | 4677.4 | 1080.6 | 3.29 | 4.33 |

| len | '\n' 찾기 바이트 | `find_byte` 워드 | libc memchr | 바이트/워드 | 워드/libc |
|---|---|---|---|---|---|
| 16 | 4.7 | 2.4 | 1.4 | 1.92 | 1.80 |
| 256 | 68.3 | 30.6 | 5.8 | 2.23 | 5.23 |
| 4096 | 1014.4 | 388.5 | 68.9 | 2.61 | 5.64 |
| 65536 | 16080.6 | 7403.2 | 1258.1 | 2.17 | 5.88 |

```text
  gray_dec loop(31)  14.47 [ 8.90..23.78]
  gray_dec 5-step     0.79 [ 0.74.. 1.42]
  gosper (udiv)       2.32 [ 1.80.. 2.60]
  gosper (ctz)        2.50 [ 1.72.. 5.30]
```

두 번째 실행(같은 부하)도 모양은 같았다: 바이트/워드 1.9~3.6배(4096 '\n' 칸 하나만 0.93 — 그 실행에서 워드판 최대값이 1441.9 로 튐), 큰 버퍼에서 libc 가 워드판보다 4~9배(튄 칸 하나 13배), Gray 5단계가 루프보다 14~18배. 읽을 것:

- 4바이트 SWAR 은 바이트 루프보다 **약 2~3.7배**. 이론상 4배가 안 나오는 건 머리·꼬리, 워드마다의 분기와 ctz 때문.
- libc 는 더 빠르다: 이 맥의 libc 는 NEON 16바이트 벡터를 쓰는 것으로 알려져 있다(구현은 확인 안 함 — 결과만 실측). 같은 아이디어를 더 넓은 레지스터로. Cortex-M4 에는 NEON 이 없으므로 M4 에서는 4바이트 SWAR 이 "가장 넓은 레지스터" 다(M55/M85 의 Helium(MVE)은 128비트 벡터 — 확인 권장).
- Gosper 두 판이 같은 이유는 위 asm: 컴파일러가 나눗셈을 이미 ctz 시프트로 바꿨다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 39/39 passed, 0.15 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 39/39, 보고 없음
```

요약 줄 (실제 출력): `gray 16-bit exhaustive: roundtrip bad=0 … adjacent!=1bit=0 dup=0`, `submask n=12: … bad=0, total visits=531441`, `gosper exhaustive n=1..20, every k: bad=0`, `swar: 589824 words: haszero iff bad=0 exact bad=0 first bad=0 hasvalue bad=0 | per-byte flags of haszero differ from exact in 13558 words`, `hasless n=0..128 … bad=0 | n=129 wrong on 2138 words (first 0x00000000)`, `strlen_word: 400 cases … bad=0`.

**버그 주입 확인** (solution.c 의 사본에 한 줄씩, 이 레슨 작업 중 실제로 돌린 결과): haszero 상수를 `0x01010100` 으로 → 32/39 · `gray_dec` 에서 `>> 16` 빼기 → 37/39 · Gosper 꼭대기 검사 `r == 0` 빼기 → 35/39 · 부분집합 순회가 공집합 전에 멈춤 → 35/39 · `enc_track` 의 `>=` 를 `>` 로 → 38/39 · (대조군) `gray_dec` 시프트 순서를 1,2,4,8,16 으로 → 39/39 (버그가 아님, Part A 증명).

## 7. 자주 하는 실수

**1. 부분집합 순회에서 공집합을 빠뜨리거나 무한 루프.** `while (sub) {…}` 는 2^p − 1 개, `for (sub = mask; sub >= 0; …)` 는 unsigned 라 영원히. → "방문 → `if (sub == 0) break;` → `sub = (sub−1) & mask`". 실제 변형(공집합 직전에 멈춤): 35/39.

**2. Gray 역변환을 필요한 폭보다 적게.** `>> 16` 을 빼면 16비트 값에서는 완벽하다. 16비트 전수 테스트만 하면 통과 — 실제 변형이 32비트 랜덤과 `gray_dec(0x80000000)` 에서만 FAIL(37/39). → 단계 수 = log2(폭), 테스트는 구현 폭 전체(2^32 은 demo).

**3. Gosper 의 0 과 꼭대기.** `x == 0` 은 0 으로 나누기(UB), 꼭대기(`r == 0`)를 안 막으면 0xF0000000 → 3 같은 쓰레기로 사슬이 처음으로 돌아가 끝나지 않을 수 있다(실제 변형 35/39). → 두 경우 모두 0 = "다음 없음" 으로 정의하고 테스트로 못 박는다.

**4. haszero 의 바이트별 플래그를 믿는다.** "플래그 개수 = 0 바이트 개수" 로 쓰면 0x00 위의 0x01 을 더 센다. 연습 2 실측: '\n' 세기에서 200개 버퍼에 225개 과다. → 개수·위치(최하위 말고)가 필요하면 정확한 마스크 `~(((v&0x7F..)+0x7F..)|v|0x7F..)`.

**5. 상수 한 바이트 오타.** `0x01010100` 처럼 한 칸이 빠지면 그 바이트의 0 을 못 본다. 실제 변형 32/39 — 0x00 이 최하위 바이트에 오는 테스트가 있어야 잡힌다. → `0x01010101u * c` 처럼 곱으로 만들면 오타가 줄고, 64비트는 `~0ull / 255`.

**6. big-endian 에서 ctz.** 메모리상 첫 바이트가 최상위 바이트인데, haszero 의 가장 **높은** 플래그는 거짓일 수 있다 → clz 로 찾으면 틀린다. → 정확한 마스크 + clz. 이 레슨 코드는 `#error` 로 little-endian 만 받는다.

**7. hasless 에 n > 128.** n = 129 면 v = 0 에서도 0 을 돌려준다(실측 반례). → n ≤ 128 을 assert(상수 n 이면 `_Static_assert`), 넘으면 바이트 루프 — 다른 식을 쓰려면 이 레슨처럼 기준 구현과 전수 대조부터.

**8. 엔코더 변화량 경계.** `d > N/2` 로 쓰면 정확히 반 바퀴가 +N/2 로 간다 — 범위가 [−N/2, N/2) 가 아니라 (−N/2, N/2] 가 되고, 정의와 테스트가 어긋난다(실제 변형 38/39). 진짜 문제는 그 전: 샘플 사이 이동이 N/2 에 가까우면 어느 쪽이든 틀린다 → 샘플링 주기 × 최대 속도 < N/2 를 설계 조건으로 적는다.

## 8. 면접 Q&A

<details><summary>Q1. 절대 엔코더는 왜 Gray 코드를 쓰나요? 바이너리면 정확히 얼마나 나빠지죠?</summary>

전이 순간 여러 트랙이 동시에 바뀌어야 하는데 물리적으로 동시일 수 없다. 바이너리 p → p+1 은 뒤쪽 1 개수 + 1 비트가 바뀌고, 그 부분집합 아무거나로 읽힐 수 있다 → n비트에서 최악 2^(n−1) − 1 칸(실측 n = 4..12 전부). Gray 는 한 비트만 바뀌므로 판독이 옛 값 아니면 새 값 — 오차 0(실측). 더 무서운 건 멀티턴: 바이너리 오판독이 변화량 접기를 깨뜨려 **바퀴 수가 영구히** 틀린다(실측 −2바퀴), Gray 는 최대 1칸 순간 오차. 꼬리: "Gray 도 틀릴 수 있나?" → 트랙 정렬 오차가 한 칸 폭보다 크면(두 전이가 겹침) 한 칸 이상 틀릴 수 있다 — 그래서 디스크 설계가 "한 번에 한 트랙만 전환" 을 보장해야 한다. 꼬리 2: "증분(quadrature) 엔코더는?" → A/B 두 채널이 90° 위상차 = 2비트 Gray(00→01→11→10) 그 자체다.

</details>

<details><summary>Q2. Gray → 이진을 O(log w) 에 하는 이유를 증명해 보세요. 시프트 순서를 바꿔도 되나요?</summary>

b_i = g_i ⊕ … ⊕ g_31 (위로 누적 XOR) 이다. 시프트 연산자 S 로 G = I + S, 역은 Σ_{j<32} S^j, 그리고 (I+S)(I+S²)(I+S⁴)(I+S⁸)(I+S¹⁶) = Σ_{j<32} S^j (0..31 의 이진 전개가 유일). 인수가 모두 S 의 다항식이라 교환 가능 → 순서 무관(시프트 순서를 뒤집은 변형도 39/39 실측). 일반적으로 "결합적 연산의 prefix scan 을 log 단계로" 라는 병렬 prefix 의 SWAR 판이다. 꼬리: "64비트면?" → `>> 32` 한 단계 추가, 6단계. 꼬리 2: "M4 에서 몇 사이클?" → 실제 asm 은 `eor.w … lsr #k` 다섯 명령, 각 1사이클로 알려져 있다(사이클 수는 TRM 확인 권장).

</details>

<details><summary>Q3. haszero 가 맞다는 걸 증명하세요. 결과의 각 0x80 플래그가 "그 바이트가 0" 을 뜻하나요?</summary>

빌림 β 를 바이트 단위로 추적: 0 바이트가 없으면 빌림이 한 번도 안 생기고 r = b − 1 이라 "원래 bit7 = 0 인데 결과 bit7 = 1" 이 불가능 → 0. 0 바이트가 있으면 가장 낮은 것 j 에서 빌림 없이 0xFF → 플래그. 그래서 "≠ 0 ⇔ 0 바이트 있음" 이고 가장 낮은 플래그는 정확하다. 하지만 j 위로는 빌림이 0x00/0x01 을 타고 올라가 **0x01 바이트에 거짓 플래그**가 선다 — 2^32 중 196607 워드(3·2^16 − 1, 포함-배제와 전수 실측 일치). 그래서 little-endian 에서 첫 0 = ctz/8 는 맞고, 개수 세기·big-endian 첫 0 은 정확한 마스크가 필요. 꼬리: "`~v` 는 왜?" → 0x81..0xFF 에서 bit7 이 그대로 남는 거짓 양성을 막는다.

</details>

<details><summary>Q4. 워드 단위 strlen 은 문자열 끝을 넘어 읽습니다. 괜찮은가요?</summary>

플랫폼 관점: 정렬된 워드는 페이지·MPU 영역 경계를 가르지 않으므로 폴트가 없다 — libc(musl `strlen.c`, `may_alias` 워드 타입)가 이 근거로 한다. 언어 관점: 객체 밖 읽기는 ISO C 에서 UB 이고, ASan 은 6바이트 할당에서 "READ of size 4 … 1 bytes after 6-byte region" 으로 잡는다(실측). 내 펌웨어 코드라면 길이를 받는 API(`find_byte`, `memchr`, `strnlen`)로 과독 0 을 만들고, 꼭 NUL 종료를 빠르게 해야 하면 libc 를 쓰거나, 버퍼를 워드 배수로 패딩해 과독이 같은 객체 안에 머물게 한다. 꼬리: "MCU 에서 다른 위험은?" → 버퍼 뒤에 읽기-부작용 레지스터가 매핑된 경우, 그리고 M0/M0+ 의 비정렬 워드 접근 HardFault(그래서 머리를 바이트로 정렬).

</details>

<details><summary>Q5. 모든 마스크의 모든 부분집합을 도는 비용은? 더 빠르게 할 수 있나요?</summary>

Σ_mask 2^popcount(mask) = 3^n (원소마다 3가지 상태, 실측 n = 12, 15 일치). 부분집합 순회 DP(예: 작업 묶음 최소 수, 연습 4)는 O(3^n) — n = 15 에서 1.4×10^7, n = 20 이면 3.5×10^9. 필요한 게 부분집합 **합/최솟값** 처럼 "원소별로 분해되는 집계"면 SOS(zeta) 변환 O(n·2^n): 비트마다 `if (m & bit) f[m] += f[m ^ bit]` (연습 3, n = 14 에서 114688 vs 4782969). 꼬리: "상위집합 순회는?" → `sup = (sup + 1) | mask`, 2^(n − popcount) 개(연습 3). 꼬리 2: LeetCode 78 Subsets 는 이 순회의 가장 단순한 형태.

</details>

<details><summary>Q6. Gosper's hack 을 설명하고, 32비트 꼭대기·0·나눗셈 없는 MCU 에서 어떻게 되나요?</summary>

최하위 1-덩어리 위 0 을 1 로 올리고(r = x + (x & −x)) 덩어리 나머지를 바닥으로(`((r ^ x) >> 2) / c`). 덩어리가 bit 31 까지 차면 r = 0 — 이게 "다음 없음" 과 동치(최대값 = 위쪽 k 비트). x = 0 이면 c = 0 으로 나누기. 둘 다 0 반환으로 정의. 나눗셈: c 가 2의 거듭제곱이라 clang 이 `>> ctz(x)` 로 바꿨다 — M4 `rbit+clz+lsr`, M0+ 는 de Bruijn 표, arm64 도 udiv 없음(실제 asm). 꼬리: "어디에 쓰나?" → n 비트 중 k 개 조합 전부: k 채널 선택, k비트 오류 패턴(Part C: CRC-8 무게 4 패턴 35960개 중 284개 미검출), 테스트 벡터 생성. 순서는 colex 라 순위 = Σ C(p_i, i+1)(연습 5).

</details>

<details><summary>Q7. 이 트릭들의 최악 실행 시간(WCET)은? ISR 에서 써도 되나요?</summary>

`gray_enc/dec`, `haszero`, `first_zero_le` 는 **분기 없는 상수 시간**(M4 asm: 1/5/3/6명령), Gosper 한 단계도 상수 시간(M4 asm 약 12명령, 꼭대기에서의 조건부 return 하나) — ISR 에서 안전. 엔코더 해독 + 변화량 누적도 O(1)이라 엔코더 샘플링 ISR 에 넣을 수 있다(단 64비트 누적 위치를 태스크가 읽을 때는 32비트 MCU 에서 두 번에 나눠 읽으니 찢어짐(tearing)을 막는 임계 구역이나 시퀀스 카운터 필요). `strlen_word`/`find_byte` 는 길이에 비례, 부분집합/Gosper **사슬 전체**는 지수적(C(32,16) = 6×10^8 단계) — ISR 금지, 오프라인/태스크에서. 꼬리: "엔코더를 몇 Hz 로 읽어야?" → 최대 속도 × 주기 < N/2 (멀티턴 방향 결정 조건), 여유를 두면 N/4.

</details>

<details><summary>Q8. 64비트로 바꾸거나 SIMD 가 있으면 무엇이 달라지나요?</summary>

상수만 바꾼다: `ONES = ~0ull / 255`, `HIGHS = ONES * 0x80`(musl 의 `ONES`/`HIGHS` 정의와 같은 방식), 정렬 8, 첫 0 = ctz/8 (0..7). Gray 역변환은 `>> 32` 한 단계 추가. 64비트 MCU 가 아닌 Cortex-M 에서 uint64 SWAR 은 레지스터 두 개로 쪼개져 이득이 줄어든다. NEON/Helium 같은 SIMD 가 있으면 바이트 비교를 레인별로 정확히 하므로 haszero 의 빌림 문제가 아예 없다 — 이 맥 libc 가 큰 버퍼에서 워드판보다 4~6배 빠른 이유로 보인다(실측은 속도만, libc 내부는 확인 안 함). 꼬리: "SWAR 의 핵심 제약은?" → 칸 사이 자리올림/빌림. haszero 는 그 번짐을 '가장 낮은 플래그는 정확' 으로 견디고, 정확한 마스크는 `& 0x7F` 로 번짐 자체를 막는다.

</details>

## 9. 추가 연습

### 연습 1 — 반사(reflect)로 Gray 수열 만들기

n비트 Gray 수열을 "n−1비트 수열 + 그것을 거꾸로 뒤집고 최상위 비트를 켠 것" 으로 만들어라(이름 reflected binary code 의 유래). n = 1..16 에서 `b ^ (b >> 1)` 과 같음을 확인. LeetCode 89 Gray Code 가 이 문제다.

<details><summary>답 보기</summary>

길이 2^i 의 앞 절반이 있을 때 `out[2^i + j] = out[2^i − 1 − j] | 2^i`. 이음매(2^i − 1 ↔ 2^i)는 같은 값에 최상위 비트만 붙으니 한 비트 차이, 뒤 절반 내부는 앞 절반을 거꾸로 읽은 것이라 한 비트 차이가 유지된다. 식과 같다는 증명(귀납, 앞 절반은 out[j] = G(j)): i비트 안에서 2^i − 1 − j = ~j 이고 G(~j) = G(j) ⊕ 2^(i−1) (G 가 선형, G(2^i − 1) = 2^(i−1)) → out[2^i + j] = G(j) ⊕ 2^(i−1) ⊕ 2^i. 한편 G(2^i + j) = 2^i ⊕ 2^(i−1) ⊕ G(j). 같다. ∎ practice.c 가 n = 1..16 전수로도 확인. 반사판은 메모리 2^n 개가 필요하고 식은 O(1) — 펌웨어에서는 식.

</details>

### 연습 2 — 64비트 SWAR 로 '\n' 개수 세기

UART 로그 버퍼(8-정렬, 길이 임의)에서 '\n' 의 개수를 64비트 워드 단위로 세라. 정확한 마스크판과 haszero 플래그판을 둘 다 만들어 기준과 비교하고 haszero 판이 얼마나 틀리는지 재라. 테스트 데이터는 '\n' 바로 뒤에 0x0B 가 자주 오게 만든다.

<details><summary>답 보기</summary>

`w ^= ONES64 * '\n'` 로 '\n' 을 0 으로 만들고 정확한 마스크의 popcount 를 더한다. 꼬리 7바이트 미만은 바이트로. haszero 판은 '\n'(0x0A) 바로 위 0x0B(= 0x0A ^ 0x01 → XOR 후 0x01) 에서 거짓 플래그를 더 센다 — 실측(practice.c): 200개 버퍼에서 정확판 오차 0, haszero 판 총 225개 과다. 이건 "줄 수 세기" 에서 실제로 날 수 있는 버그다: 일반 텍스트엔 0x0B 가 드물어 테스트를 통과하다가 바이너리 섞인 로그에서 틀린다.

```c
n += pc64(~(((w & 0x7F..7F) + 0x7F..7F) | w | 0x7F..7F));   /* 바이트별 정확 */
```

</details>

### 연습 3 — 상위집합 순회와 SOS DP

(a) `sup = (sup + 1) | mask` 로 n비트 우주에서 mask 의 모든 상위집합을 돌고 개수가 2^(n − popcount) 인지 n = 10 전수로 확인. (b) f[m] = Σ_{s ⊆ m} a[s] 를 부분집합 순회(O(3^n))와 비트별 SOS(O(n·2^n))로 각각 계산해 n = 14 에서 같음을 확인하고 작업량을 비교하라.

<details><summary>답 보기</summary>

(a) `sup + 1` 은 mask 밖 비트만 모은 압축 좌표에서 +1 이 되게 하려고 `| mask` 로 mask 비트를 "이미 1" 로 채워 자리올림이 그 위를 건너뛰게 한다(부분집합 순회의 거울). 끝은 `sup == full`. (b) SOS: 비트 b 마다 b 가 켜진 모든 m 에 `f[m] += f[m ^ (1<<b)]` — 단계 b 후 f[m] = "b 이하 비트에서만 m 과 다를 수 있는 부분집합들의 합". 실측: 덧셈 n·2^(n−1) = 114688, 순회 방문 4782969 = 3^14, 결과 일치. 메모리 2^n 워드가 필요하므로 MCU 에서는 n ≤ 12~14 정도.

</details>

### 연습 4 — 부분집합 DP: 플래시 쓰기 작업 묶기

크기 w_i 바이트인 쓰기 작업 n ≤ 12 개를 용량 C 인 페이지 버퍼에 나눠 담는 최소 묶음 수(bin packing)를 O(3^n) 부분집합 DP 로 구하라. n ≤ 9 에서 분할 브루트포스(가지치기 재귀)와 400개 랜덤 인스턴스로 대조.

<details><summary>답 보기</summary>

`dp[m] = min over g ⊆ m, lowbit(m) ∈ g, wsum[g] ≤ C of dp[m \ g] + 1`. 최하위 원소를 반드시 포함하는 그룹만 보면 같은 분할을 여러 순서로 세지 않는다(순회는 `rest = m ^ lowbit` 의 부분집합). `wsum[]` 은 `wsum[m] = wsum[m ^ lb] + w[ctz(lb)]` 로 O(2^n). 실측: 랜덤 400개 브루트와 불일치 0, 12개 작업(합 2900B)을 1024B 페이지에 → 3묶음(하한 ⌈2900/1024⌉ = 3 과 같음). 실제 FTL 에서는 이런 최적화 대신 first-fit 류 휴리스틱을 쓰지만, 휴리스틱의 최적성 간격을 잴 **기준**으로 이 DP 가 쓸모 있다(A03 의 2-opt 대조와 같은 역할).

</details>

### 연습 5 — k-조합의 colex 순위와 역순위

k비트 조합 x 의 순위 rank(x) = Σ_i C(p_i, i+1) (p_0 < p_1 < … 는 1 의 위치) 와 그 역함수를 구현하라. Gosper 로 n = 20 의 모든 k 를 돌며 rank 가 0, 1, 2, … 로 정확히 1씩 늘고 unrank(rank(x)) = x 인지, 그리고 rank(0xFFFF0000) = C(32,16) − 1 인지 확인하라.

<details><summary>답 보기</summary>

조합 수 체계(combinatorial number system): colex 순서에서 x 보다 앞선 조합 = "최상위 1(p_{k−1})보다 아래에서 k 개를 고른 것 C(p_{k−1}, k)" + "최상위는 같고 나머지 k−1 개가 앞선 것" → 재귀적으로 Σ C(p_i, i+1). 역순위는 위에서부터 C(p, i) ≤ r 인 가장 큰 p 를 고른다(탐욕). Gosper 순서 = 정수 증가 = colex 이므로 rank(next(x)) = rank(x) + 1 — practice.c 가 n = 20 의 모든 k 에서 확인. 쓸모: "조합 번호 r 번째 테스트 케이스" 를 상태 없이 재현(테스트 장비가 중단 후 재개할 때), 조합을 인덱스로 하는 표.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
```

## 10. 핵심 정리

- Gray: `g = b ^ (b>>1)`, 이웃 = 정확히 1비트(G 가 선형 → G(b)^G(b+1) = 2^t). 역 = 위로 누적 XOR = `>>16,8,4,2,1` 다섯 단계, 인수가 교환 가능해 순서 무관. 테스트는 구현 폭 전체로(`>>16` 빠짐은 16비트 테스트를 통과한다).
- 엔코더: 전이 중 판독 = 바뀌는 비트의 부분집합. 바이너리 최악 2^(n−1)−1 칸 + 멀티턴 바퀴 손실(실측 −2바퀴), Gray 0칸. 멀티턴 변화량은 `[−N/2, N/2)` 로 접고, 주기 × 최대 속도 < N/2.
- haszero = `(v−0x01..)&~v&0x80..`: ≠0 ⇔ 0 바이트 있음, 가장 낮은 플래그만 정확(ctz/8 = LE 첫 0). 0x00 위 0x01 에 거짓 플래그 — 2^32 중 196607 워드. 개수·BE 는 정확한 마스크 `~(((v&0x7F..)+0x7F..)|v|0x7F..)`.
- hasless 는 n ≤ 128 에서만(v = 0, n = 129 반례). hasvalue = haszero(v ^ c·0x01..).
- 워드 strlen: 정렬 워드는 페이지/MPU 경계를 넘지 않아 실제로는 안전, ISO C 로는 객체 밖 읽기 = UB(ASan 이 잡음). 길이를 알면 과독 0 인 머리·몸통·꼬리.
- 부분집합: `sub = (sub−1)&mask` = 압축 좌표 −1, 공집합 방문 후 멈춤, 2^popcount 개, 전체 3^n. 집계면 SOS O(n·2^n).
- Gosper: `c=x&-x; r=x+c; r==0 → 끝; ((r^x)>>2)/c | r`. x=0 은 0 나누기. 나눗셈은 컴파일러가 ctz 시프트로 바꿈(M4·arm64 asm). 순서 = colex. CRC-8 0x07 의 32비트 코드워드: 무게 1·2·3 전부 검출, 무게 4 는 35960 중 284 미검출.
- M4 비용(실제 asm): Gray 1/5명령, haszero 3명령, 첫 0 바이트 6명령 분기 없음, Gosper 약 12명령(udiv 없음).

## 참고자료

- S. E. Anderson, *Bit Twiddling Hacks* — "Determine if a word has a zero byte"(기법은 Alan Mycroft 1987 뉴스그룹 글), "…has a byte equal to n"(Stephen M. Bennet, 2009), "…has a byte less than n"(Juha Järvi 2005, 조건 0≤n≤128), "Compute the lexicographically next bit permutation"(ctz 판과 나눗셈 판): <https://graphics.stanford.edu/~seander/bithacks.html>
- M. Beeler, R. W. Gosper, R. Schroeppel, *HAKMEM*, MIT AI Memo 239, 1972 — ITEM 175 (Gosper): 같은 개수의 1 을 가진 다음 수: <https://www.inwap.com/pdp10/hbaker/hakmem/hacks.html>
- musl libc `src/string/strlen.c` — `HASZERO`, `may_alias` 워드, 정렬 후 워드 루프: <https://git.musl-libc.org/cgit/musl/plain/src/string/strlen.c>
- Gray code (Wikipedia) — 반사 이진 코드, F. Gray 1947 출원·1953 특허(번호 US 2,632,058 — 확인 권장), 위치 엔코더·비동기 FIFO 포인터, `GrayToBinary32` 의 16/8/4/2/1 시프트: <https://en.wikipedia.org/wiki/Gray_code>
- H. S. Warren Jr., *Hacker's Delight*, 2nd ed. — 2장 "next higher number with same number of 1-bits"(snoob), Gray 코드 장, 워드 단위 0 바이트 찾기. 장 번호는 기억에 의존 — 확인 권장.
- D. E. Knuth, *TAOCP* Vol. 4A — 7.1.3 Bitwise tricks and techniques, 7.2.1.1 Generating all n-tuples (Gray 코드), 7.2.1.3 Generating all combinations (colex, 조합 수 체계). 절 번호는 기억에 의존 — 확인 권장.
- ARM, *ARMv7-M Architecture Reference Manual* — CCR.DIV_0_TRP / UNALIGN_TRP, MPU 영역 최소 크기. 세부는 확인 권장.
- P. Koopman, CRC 다항식 HD 표 (users.ece.cmu.edu/~koopman/crc/) — 무게별 미검출 실측과 대조해 볼 것(이 레슨은 표 값을 인용하지 않음).
- 관련 레슨: ALGO-B12 (popcount·`x & -x`·ctz/clz·de Bruijn), ALGO-B13 (비트 반전·패리티), ALGO-A03 (비트마스크 DP TSP — 부분집합을 상태로), ALGO-M01 (CRC 비트 단위 엔진·다항식 나눗셈), ARCH-A07 (Gray 포인터 비동기 FIFO), C-B01 (시프트 UB), C-M05 (엔디안 독립 직렬화)

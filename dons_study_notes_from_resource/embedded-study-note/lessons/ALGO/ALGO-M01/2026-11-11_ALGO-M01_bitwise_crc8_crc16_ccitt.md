---
id: ALGO-M01
time: 80분
updated: 2026-11-11
---
# ALGO-M01 CRC 원리: 비트 단위 CRC-8·CRC-16-CCITT

## 학습 목표

- CRC 를 **GF(2) 다항식 나눗셈의 나머지**로 설명하고, 한 바이트를 레지스터 방식과 long division 방식 두 가지로 손 계산해 같은 값(0x97)이 나오는 것을 보인다.
- Rocksoft 모델의 6개 파라미터(width, poly, init, refin, refout, xorout)로 동작하는 **비트 단위 CRC 엔진 하나**를 짜서 CRC-8/SMBUS(0xF4), CRC-16/CCITT-FALSE(= RevEng 이름 CRC-16/IBM-3740, 0x29B1), CRC-16/MODBUS(0x4B37)를 포함한 카탈로그 20개 알고리즘의 `"123456789"` check 값과 residue 값을 모두 맞춘다.
- 반사(reflection)가 왜 있는지(UART·HDLC 는 LSB 먼저 보낸다)와, Modbus 코드에 보이는 `crc >> 1 ^ 0xA001` 가 같은 엔진의 다른 모양이라는 것을 설명한다.
- CRC 가 무엇을 **보장**하는지(1비트, 홀수 비트, 길이 ≤ w 버스트, 주기 안의 2비트)를 증명하고, 전수 측정 결과(버스트 w+1 놓침 2^-(w-1), 그 이상 2^-w)로 확인한다.
- SMBus PEC, Sensirion 센서 워드 CRC, Modbus RTU 프레임처럼 실제 버스에서 CRC 를 붙이고 검사하는 코드를 쓴다.

## 1. 문제

펌웨어 코딩 면접에서 "CRC 짜 보세요"는 **파라미터 하나 틀리면 값이 통째로 다른** 문제다. 코드는 열 줄인데, 꼬리질문은 "왜 XOR 인가 → 0x1021 은 무슨 뜻인가 → 왜 0xFFFF 로 시작하나 → 무엇을 보장하나 → MCU 에서 어떻게 빠르게"로 깊어진다. 이 레슨은 그 중 **원리와 정확성**을 맡는다. 테이블·slicing 같은 속도는 ALGO-M02, 단순 합/XOR 체크섬과의 비교는 ALGO-B14 에서 했다.

**CRC 한 문장.** 메시지 비트열을 GF(2)(덧셈 = XOR, 자리올림 없음) 위의 다항식 M(x) 로 보고, M(x)·x^w 를 생성 다항식 G(x)(차수 w)로 나눈 **나머지 R(x)** (w 비트)를 붙여 보낸다. 받는 쪽은 같은 나눗셈을 해서 나머지가 맞는지 본다. 덧셈이 XOR 라서 뺄셈도 XOR, "나눗셈"은 맨 앞 1 이 있는 자리에 G 를 맞춰 XOR 하는 일의 반복이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CRC-8/SMBUS 레지스터: G(x) = x^8 + x^2 + x + 1 (poly 0x07), 왼쪽으로 민다 (direct 알고리즘)</text>
  <g stroke="currentColor">
    <rect x="120" y="70" width="60" height="34" class="f-mut"/><rect x="180" y="70" width="60" height="34" class="f-mut"/>
    <rect x="240" y="70" width="60" height="34" class="f-mut"/><rect x="300" y="70" width="60" height="34" class="f-mut"/>
    <rect x="360" y="70" width="60" height="34" class="f-mut"/><rect x="420" y="70" width="60" height="34" class="f-acc"/>
    <rect x="480" y="70" width="60" height="34" class="f-acc"/><rect x="540" y="70" width="60" height="34" class="f-acc"/>
  </g>
  <text x="138" y="92" fill="currentColor">b7</text><text x="198" y="92" fill="currentColor">b6</text>
  <text x="258" y="92" fill="currentColor">b5</text><text x="318" y="92" fill="currentColor">b4</text>
  <text x="378" y="92" fill="currentColor">b3</text><text x="438" y="92" fill="currentColor">b2</text>
  <text x="498" y="92" fill="currentColor">b1</text><text x="558" y="92" fill="currentColor">b0</text>
  <text x="120" y="62" fill="currentColor">← 시프트 방향 (crc &lt;&lt;= 1)</text>
  <circle cx="70" cy="87" r="14" class="f-warn" stroke="currentColor"/>
  <text x="63" y="92" fill="currentColor">⊕</text>
  <line x1="120" y1="87" x2="86" y2="87" stroke="currentColor"/>
  <text x="18" y="60" fill="currentColor">입력 비트</text>
  <line x1="70" y1="65" x2="70" y2="72" stroke="currentColor"/>
  <text x="10" y="130" fill="currentColor">top = 나가는 b7 ⊕ 입력</text>
  <line x1="70" y1="101" x2="70" y2="170" stroke="currentColor"/>
  <line x1="70" y1="170" x2="620" y2="170" stroke="currentColor"/>
  <line x1="620" y1="170" x2="620" y2="87" stroke="currentColor"/>
  <line x1="620" y1="87" x2="600" y2="87" stroke="currentColor"/>
  <text x="300" y="190" fill="currentColor">top = 1 이면 b2 b1 b0 에 XOR (0x07 의 1 자리)</text>
  <line x1="450" y1="170" x2="450" y2="104" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="510" y1="170" x2="510" y2="104" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="570" y1="170" x2="570" y2="104" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="226" fill="currentColor" font-family="monospace">top = ((crc &gt;&gt; 7) ^ bit) &amp; 1;  crc = (crc &lt;&lt; 1) &amp; 0xFF;  if (top) crc ^= 0x07;</text>
  <text x="10" y="252" fill="currentColor">x^8 항은 레지스터에 없다: 밀려 나가는 비트가 곧 "몫의 한 자리"이고, 그 자리를 G 로 지우는 일이 poly XOR 다.</text>
  <text x="10" y="276" fill="currentColor">그래서 poly 표기 0x07 / 0x1021 / 0x8005 는 최고차항(x^w)을 뺀 나머지 계수다 (normal 표기).</text>
</svg>
```

### Part A — 파라미터화된 비트 단위 엔진과 다항식 나눗셈

- `crc_params` = { width(1~32), poly, init, refin, refout, xorout, check, residue } — RevEng 카탈로그가 쓰는 Rocksoft 모델 그대로.
- `crc_mask(w)` (w = 32 에서 `1u << 32` UB 를 피할 것), `reflect(x, nbits)`.
- `crc_bitwise(p, d, n)` — direct 알고리즘: init → 바이트마다(refin 이면 비트 반전) MSB 먼저 8 단계 → refout → xorout.
- `crc_polydiv(p, d, n)` — 교과서식: 비트 배열 위에 M(x)·x^w + Init(x)·x^L (L = 8n) 을 놓고 long division. 같은 값이 나와야 한다.
- 테스트: 세 주인공 check 값, 카탈로그 20개(width 3·4·5·8·15·16·32) check 값, 빈 메시지, 1바이트 메시지 전수, 임의 파라미터 × 랜덤 메시지 3000개를 **다른 길로 짠 기준 구현**(바이트마다 `(R·x^8 + B·x^w) mod G` 를 uint64 로)과 대조.

### Part B — 반사형, 덧붙이기와 잔여값, 검출 능력 증명

- `crc_reflected(p, d, n)` — refin = refout = true 전용. 레지스터를 처음부터 뒤집힌 모양으로 두고 **오른쪽**으로 민다, poly 도 뒤집는다(0x8005 → 0xA001). 비트 반전이 전혀 없다. 엔진과 같아야 한다.
- `crc_append(p, buf, n)` — CRC 를 데이터 뒤에 붙인다. refout 이면 **하위 바이트 먼저**(LE), 아니면 상위 바이트 먼저(BE).
- `crc_residue_ok(p, frame, len)` — 받는 쪽: 데이터 + CRC 전체를 엔진에 넣으면 레지스터가 **상수(residue)** 가 된다. 카탈로그 residue 16개(width 8/16/32)와 맞춘다.
- 테스트(전수): 4바이트 데이터 codeword 에서 모든 1·2·3비트 오류, 모든 길이 ≤ w 버스트를 넣어 **하나도 놓치지 않음**. 더 큰 표(무게 4, 버스트 w+1~w+4, 2비트 한계)는 `demo_detect.c`.

### Part C — 센서 버스에서 CRC

- `smbus_read_word_ok(addr7, cmd, rx[3], &word)` — SMBus/PMBus Read Word 의 PEC(Packet Error Code, CRC-8/SMBUS)는 **주소 바이트까지** 포함한 선로의 5바이트 전체가 대상이다. 다른 장치가 대답하면 PEC 가 틀린다.
- `sht_parse(rx[6], &t_mC, &rh_mpct)` — Sensirion SHT3x 측정 프레임(온도 워드 + CRC, 습도 워드 + CRC; CRC-8 poly 0x31 init 0xFF = 카탈로그 CRC-8/NRSC-5 파라미터). 데이터시트 예 CRC(0xBEEF) = 0x92. 밀리 단위 정수 변환까지.
- `modbus_build_read(slave, start, qty, out)` / `modbus_frame_ok(f, len)` — Modbus RTU 0x03 요청, CRC-16/MODBUS 를 하위 바이트 먼저. 흔히 인용되는 예 `01 03 00 00 00 0A C5 CD` 를 재현.

## 2. Problem (English)

Implement a table-free, parameterised CRC engine (Rocksoft model) and use it on real buses:

```c
typedef struct { const char *name; unsigned width; uint32_t poly, init; bool refin, refout;
                 uint32_t xorout, check, residue; } crc_params;

uint32_t crc_bitwise(const crc_params *p, const uint8_t *d, size_t n);   /* width 1..32 */
uint32_t crc_polydiv(const crc_params *p, const uint8_t *d, size_t n);   /* textbook long division */
uint32_t crc_reflected(const crc_params *p, const uint8_t *d, size_t n); /* refin=refout=true, right shift */
size_t   crc_append(const crc_params *p, uint8_t *buf, size_t n);        /* returns n + width/8 */
bool     crc_residue_ok(const crc_params *p, const uint8_t *frame, size_t len);
```

Constraints: no lookup tables, no dynamic allocation, O(n·8) bit steps, O(1) extra memory (the long-division version may use a static bit buffer for n ≤ 256). Must be correct for width = 32 and width < 8.

Examples (RevEng catalogue check values, input = ASCII `"123456789"`):

```text
CRC-8/SMBUS      width=8  poly=0x07   init=0x00   refin=F refout=F xorout=0x00 -> 0xF4
CRC-16/IBM-3740  width=16 poly=0x1021 init=0xFFFF refin=F refout=F xorout=0    -> 0x29B1  (a.k.a. CCITT-FALSE)
CRC-16/MODBUS    width=16 poly=0x8005 init=0xFFFF refin=T refout=T xorout=0    -> 0x4B37
Modbus RTU request 01 03 00 00 00 0A  -> append CRC -> 01 03 00 00 00 0A C5 CD
```

Follow-ups: Why is the x^16 term missing from 0x1021? What do init and xorout protect against? Which errors are guaranteed to be detected, and up to what frame length? Why do UART-era CRCs come reflected?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열한 군데(Part A 4개, Part B 3개, Part C 4개). 카탈로그 표 `CAT[]`, 기준 구현 `ref_crc`, 하네스는 주어진다. 스텁은 안전한 틀린 값(0, init, 입력 그대로)을 돌려준다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:192  crc_mask(1) == 0x1u && crc_mask(8) == 0xFFu && crc_mask(15) == 0x7FFFu && crc_mask(32) == 0xFFFFFFFFu
FAIL starter.c:193  reflect(0x01, 8) == 0x80 && reflect(0x31, 8) == 0x8C && reflect(0x8005, 16) == 0xA001
…
  engine CRC-16/IBM-3740    got 0xFFFF want 0x29B1
…
random params x msgs 3000: mismatches bitwise=2992 polydiv=2903 reflected=1071
residue check: 16 algorithms x 50 frames (ok + 1-bit flipped), bad=816
…
FAIL starter.c:362  badmb == 0
6/40 passed
```

(exit 1. 통과한 6개는 스텁이 우연히 맞는 경우다 — init 을 그대로 돌려주는 스텁이 "빈 메시지의 CRC = init" 과 같고, false 를 돌려주는 스텁이 "깨진 프레임은 거부" 와 같다.)

## 4. 힌트

<details><summary>힌트 1 — 레지스터 한 단계는 세 줄</summary>

들어오는 비트 `in` 과 레지스터 최상위 비트를 XOR 한 것이 `top`. 레지스터를 한 칸 왼쪽으로 밀고(마스크로 w 비트만 남김) `top` 이 1 이면 poly 를 XOR. 바이트는 refin 이 false 면 bit7 부터, true 면 bit0 부터 넣는다(= 바이트를 reflect 한 뒤 bit7 부터). 마지막에 refout 이면 w 비트 reflect, 그 다음 xorout.

</details>

<details><summary>힌트 2 — width 가 32 일 때와 8 보다 작을 때</summary>

마스크 `(1u << w) - 1` 은 w = 32 에서 UB 다(시프트 양 ≥ 폭). `w >= 32 ? 0xFFFFFFFF : ...` 로 따로. 최상위 비트는 `crc >> (w - 1)` 로 읽으면 w = 3 이든 32 든 같은 코드다. "바이트를 레지스터 위쪽 8비트에 XOR" 하는 흔한 최적화(`crc ^= b << (w - 8)`)는 w < 8 에서 깨지므로, 엔진은 비트를 하나씩 넣는다.

</details>

<details><summary>힌트 3 — long division 의 init</summary>

direct 알고리즘이 init 레지스터에서 시작해 L 비트를 처리하면 결과 = (Init(x)·x^L + M(x)·x^w) mod G 다(5. 풀이에서 증명). 그래서 비트 배열(길이 L + w, 칸 j = 지수 L + w − 1 − j) 맨 앞 w 칸에 init 을 XOR 하고 나누면 된다. L &lt; w 라도(빈 메시지 포함) 배열 안에 들어간다.

</details>

<details><summary>힌트 4 — 반사형을 오른쪽 시프트로</summary>

모든 것을 거울에 비추면: 레지스터의 "최상위"가 bit0 이 되고, 왼쪽 시프트는 오른쪽 시프트, poly 는 reflect(poly), init 은 reflect(init). 메시지 비트는 LSB 먼저 bit0 에 XOR. 끝에서 refout 반전을 할 필요가 없다(레지스터가 이미 뒤집힌 모양). init 이 0xFFFF 처럼 대칭이면 reflect 를 빼먹어도 티가 안 난다 — 랜덤 init 으로 테스트하라.

</details>

<details><summary>힌트 5 — 잔여값(residue)</summary>

데이터 뒤에 CRC 를 "읽는 순서가 이어지도록" 붙이면 codeword 전체를 나눈 나머지는 데이터와 무관한 상수다. refout 알고리즘은 레지스터 bit0 이 먼저 나가야 하므로 하위 바이트 먼저, 아니면 상위 바이트 먼저. 엔진 결과는 `레지스터 ^ xorout` 이므로 비교 전에 xorout 을 다시 XOR.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 어느 CRC 인가 — **이름 대신 6개 파라미터**(width, poly, init, refin, refout, xorout)와 check 값을 받는다. CRC 를 붙이는 바이트 순서, 메시지 최대 길이, 테이블 쓸 RAM/Flash 가 있나 |
| 2. 단순 해법 | 비트 단위 시프트 레지스터 — 바이트당 8 단계, 메모리 O(1). 테이블 없이도 정답 |
| 3. 불변식 / 정당성 | "i 비트를 읽은 뒤 레지스터 = (Init·x^i + M_i·x^w) mod G" (M_i = 처음 i 비트). 귀납으로 증명 (아래) |
| 4. 코드 | 아래 Part A/B |
| 5. 소리 내어 말할 테스트 | `"123456789"` check 값, 빈 메시지 = init 변환값, width 32 와 width &lt; 8, 1비트 깨면 거부, 받는 쪽 잔여값 |
| 6. 복잡도 | 시간 O(8n) 비트 단계, 추가 메모리 O(1), 스택 몇 바이트, 재귀 없음. 속도가 문제면 테이블(ALGO-M02) |

### Part A — 엔진의 불변식과 증명

레지스터 값을 w 비트 다항식 r(x) 로 본다. 한 단계에서 하는 일은 "r 을 x 배 하고, 들어온 비트 b 를 x^w 자리에 더한 뒤 mod G":

r' = (r·x + b·x^w) mod G.

왜 코드가 이것인가: r·x + b·x^w 의 x^w 계수는 (r 의 최상위 비트) ⊕ b = `top`. 그 계수가 1 이면 G 를 한 번 빼면(XOR) x^w 항이 지워지고 아래 계수들에 poly 가 XOR 된다. x^w 계수가 0 이면 그대로 w 비트 안에 있다. 이것이 `top`/시프트/XOR 세 줄이다.

**불변식.** 비트 m_1, m_2, … 를 읽었을 때 i 단계 뒤 r_i = (Init·x^i + M_i(x)·x^w) mod G, 여기서 M_i = m_1 x^{i-1} + … + m_i.
- i = 0: r_0 = Init (차수 &lt; w 이므로 mod 해도 그대로). ✓
- i → i+1: r_{i+1} = (r_i·x + m_{i+1}·x^w) mod G = (Init·x^{i+1} + M_i·x^{w+1} + m_{i+1}·x^w) mod G = (Init·x^{i+1} + M_{i+1}·x^w) mod G. ✓ (mod 는 곱셈·덧셈과 교환한다)

그래서 L 비트 뒤의 레지스터 = (Init·x^L + M·x^w) mod G 이고, 이것이 `crc_polydiv` 가 비트 배열 맨 앞 w 칸에 init 을 XOR 하고 나누는 이유다. init = 0 이면 교과서 정의 "M·x^w mod G" 와 정확히 같다.

**한 바이트 손 계산 — CRC-8/SMBUS, '1' = 0x31.** `demo_trace.c` 실제 출력:

```text
(1) direct register, CRC-8/SMBUS, init 0x00, byte 0x31 = 00110001 (MSB first)
step  in  out  in^out  reg after shift   reg after XOR
  1    0   0     0     00000000        00000000
  2    0   0     0     00000000        00000000
  3    1   0     1     00000000        00000111  (^ 00000111)
  4    1   0     1     00001110        00001001  (^ 00000111)
  5    0   0     0     00010010        00010010
  6    0   0     0     00100100        00100100
  7    0   0     0     01001000        01001000
  8    1   0     1     10010000        10010111  (^ 00000111)
CRC = 0x97

(2) long division of 0x31 * x^8 by G = 100000111
    0011000100000000
  ^   100000111         (G * x^5)
  = 0001000111100000
  ^    100000111        (G * x^4)
  = 0000000110010000
  ^        100000111    (G * x^0)
  = 0000000010010111
remainder = 10010111 = 0x97
```

두 방식이 같은 0x97. 레지스터 방식에서 `in^out = 1` 인 단계(3, 4, 8)가 long division 에서 G 를 XOR 한 세 줄(x^5, x^4, x^0)과 대응한다 — 몫이 x^5 + x^4 + 1 이다. 레지스터 방식은 "몫 비트가 정해지는 순간 바로 XOR" 해서 0 을 w 개 덧붙일 필요가 없다(direct, 비증강). 옛 문서의 **증강(augmented)** 알고리즘은 메시지 뒤에 0 을 w 비트 붙여서 같은 나눗셈을 하고, 이때 "init" 의 뜻이 달라진다(7. 실수 5).

### Part A — 엔진 코드 읽기

```c
uint8_t b = p->refin ? (uint8_t)reflect(d[i], 8) : d[i];
for (int k = 7; k >= 0; k--) {
    uint32_t top = ((crc >> (w - 1)) ^ ((uint32_t)b >> k)) & 1u;
    crc = (crc << 1) & mask;
    if (top) crc ^= p->poly;
}
```

- `crc >> (w - 1)` 로 최상위를 읽으니 w = 3 (CRC-3/GSM)부터 32 까지 한 코드. `crc << 1` 은 uint32_t 라 정의된 동작(넘친 비트는 버려짐), 마스크로 w 비트만 남긴다.
- 끝: `if (refout) crc = reflect(crc, w); return (crc ^ xorout) & mask;`
- `crc_polydiv` 는 정의를 그대로 옮긴 O(8n·w) 버전이다. 엔진의 정답 근거로 쓰고, 실전 코드로는 쓰지 않는다.

테스트의 기준 구현 `ref_crc` 는 일부러 다른 길을 간다: 바이트 단위 Horner `R ← (R·x^8 + B·x^w) mod G` 를 uint64 로 계산하고 위에서부터 G 를 빼는 나머지 연산. 엔진·polydiv·ref 세 개가 카탈로그 20개 + 임의 파라미터 3000개에서 모두 일치했다:

```text
catalogue 20 algorithms: engine mismatches=0, polydiv mismatches=0
random params x msgs 3000: mismatches bitwise=0 polydiv=0 reflected=0
```

이름 확인: RevEng 카탈로그는 poly 0x1021 / init 0xFFFF / 비반사 / xorout 0 / check 0x29B1 을 **CRC-16/IBM-3740** 으로 올리고, CRC-16/AUTOSAR 와 **CRC-16/CCITT-FALSE** 를 별칭으로 둔다. 반대로 CRC-16/CCITT·CCITT-TRUE 는 **CRC-16/KERMIT**(반사형, init 0, check 0x2189)의 별칭이다. 즉 레슨 제목의 "CRC-16-CCITT" 는 보통 IBM-3740 을 가리키지만, 같은 이름으로 KERMIT 을 뜻하는 문서도 있다 — 면접에서 "CCITT 라고 하면 어느 쪽이냐" 를 되묻는 것이 정답이다. (IBM-3740 이라는 이름은 IBM 3740 데이터 입력 시스템의 플로피 형식에서 온 것으로 알려져 있다 — 확인 권장.) Python 표준 라이브러리로도 교차 확인했다: `binascii.crc_hqx(b"123456789", 0xFFFF)` = 0x29b1, `crc_hqx(…, 0)` = 0x31c3(XMODEM), `zlib.crc32` = 0xcbf43926.

### Part B — 반사(reflection)는 왜 있나

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">UART 는 LSB 먼저 보낸다 → 선로 순서대로 나누려면 바이트를 거꾸로 읽어야 한다</text>
  <text x="10" y="50" fill="currentColor">0x31 = 0011 0001 의 UART 프레임 (8N1, 시간 →)</text>
  <g stroke="currentColor">
    <rect x="30" y="62" width="50" height="28" class="f-mut"/>
    <rect x="80" y="62" width="50" height="28" class="f-acc"/><rect x="130" y="62" width="50" height="28" class="f-mut"/>
    <rect x="180" y="62" width="50" height="28" class="f-mut"/><rect x="230" y="62" width="50" height="28" class="f-mut"/>
    <rect x="280" y="62" width="50" height="28" class="f-acc"/><rect x="330" y="62" width="50" height="28" class="f-acc"/>
    <rect x="380" y="62" width="50" height="28" class="f-mut"/><rect x="430" y="62" width="50" height="28" class="f-mut"/>
    <rect x="480" y="62" width="50" height="28" class="f-mut"/>
  </g>
  <text x="38" y="81" fill="currentColor">start</text>
  <text x="88" y="81" fill="currentColor">b0=1</text><text x="138" y="81" fill="currentColor">b1=0</text>
  <text x="188" y="81" fill="currentColor">b2=0</text><text x="238" y="81" fill="currentColor">b3=0</text>
  <text x="288" y="81" fill="currentColor">b4=1</text><text x="338" y="81" fill="currentColor">b5=1</text>
  <text x="388" y="81" fill="currentColor">b6=0</text><text x="438" y="81" fill="currentColor">b7=0</text>
  <text x="488" y="81" fill="currentColor">stop</text>
  <text x="545" y="81" fill="currentColor">선로 비트 순서 = 1000 1100</text>
  <text x="10" y="122" fill="currentColor">하드웨어 시프트 레지스터(HDLC·옛 모뎀)는 도착하는 순서대로 비트를 넣는다 = refin true.</text>
  <text x="10" y="142" fill="currentColor">소프트웨어가 같은 값을 내려면: (a) 바이트를 reflect 해서 MSB 먼저 넣거나, (b) 전부 거울에 비춘다.</text>
  <line x1="10" y1="160" x2="730" y2="160" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="184" fill="currentColor" font-weight="bold">(b) 거울에 비춘 레지스터: 오른쪽으로 민다, poly = reflect(0x8005) = 0xA001</text>
  <g stroke="currentColor">
    <rect x="120" y="200" width="36" height="28" class="f-mut"/><rect x="156" y="200" width="36" height="28" class="f-mut"/>
    <rect x="192" y="200" width="36" height="28" class="f-mut"/><rect x="228" y="200" width="36" height="28" class="f-mut"/>
    <rect x="264" y="200" width="36" height="28" class="f-mut"/><rect x="300" y="200" width="36" height="28" class="f-mut"/>
    <rect x="336" y="200" width="36" height="28" class="f-mut"/><rect x="372" y="200" width="36" height="28" class="f-mut"/>
    <rect x="408" y="200" width="36" height="28" class="f-mut"/><rect x="444" y="200" width="36" height="28" class="f-mut"/>
    <rect x="480" y="200" width="36" height="28" class="f-mut"/><rect x="516" y="200" width="36" height="28" class="f-mut"/>
    <rect x="552" y="200" width="36" height="28" class="f-mut"/><rect x="588" y="200" width="36" height="28" class="f-mut"/>
    <rect x="624" y="200" width="36" height="28" class="f-mut"/><rect x="660" y="200" width="36" height="28" class="f-warn"/>
  </g>
  <text x="124" y="219" fill="currentColor">b15</text><text x="664" y="219" fill="currentColor">b0</text>
  <text x="480" y="250" fill="currentColor">→ bit0 이 나간다 (crc &gt;&gt;= 1)</text>
  <text x="10" y="276" fill="currentColor" font-family="monospace">crc ^= byte;  for 8: crc = (crc &amp; 1) ? (crc &gt;&gt; 1) ^ 0xA001 : crc &gt;&gt; 1;</text>
  <text x="10" y="300" fill="currentColor">= Modbus 코드의 정체. 반전 연산이 없다. 결과 bit0 이 먼저 나가야 하니 CRC 는 하위 바이트 먼저.</text>
  <text x="10" y="322" fill="currentColor">refin = refout = true 인 이유: 입력도 출력도 "선로 순서" 하나로 일관되게.</text>
</svg>
```

반사의 존재 이유는 **선로 순서**다. UART 와 HDLC/X.25 는 바이트의 LSB 를 먼저 보낸다. 선로에 붙은 하드웨어 CRC 회로는 도착하는 비트를 그대로 넣으므로, 바이트 관점에서 보면 "LSB 먼저 읽기" = refin. 그 회로가 계산한 나머지도 선로로 나갈 때 x^{w-1} 계수부터 나가야 하는데, 바이트 단위로 LSB 먼저 보내는 장치에서 그렇게 되려면 레지스터를 뒤집어 저장한 값(= refout)을 하위 바이트부터 보내면 된다. 그래서 반사형 CRC 는 거의 항상 refin = refout = true 이고, 소프트웨어는 "거울에 비춘 레지스터"로 짜면 반전 연산이 하나도 없다. `crc_reflected` 가 그것이고, 랜덤 테스트에서 엔진과 0 개 차이였다.

**정당성.** 길이 w 의 비트열에 대한 반전 R 은 XOR 와 교환하고(R(a ⊕ b) = R(a) ⊕ R(b)), 왼쪽 시프트 후 마스크는 R 을 거치면 오른쪽 시프트가 되며, 최상위 비트 검사는 bit0 검사가 된다. 엔진의 한 단계 f 에 대해 R∘f∘R⁻¹ 이 바로 오른쪽 시프트 단계이므로, 초기값 R(init) 에서 같은 비트들을 넣으면 매 단계 레지스터가 엔진 레지스터의 반전이고, 끝에서 refout 반전이 상쇄된다. ∎

메시지 바이트 전체를 `crc ^= byte` 로 한 번에 넣는 Modbus 모양은 w ≥ 8 에서 비트 하나씩 넣는 것과 같다 — 비트 k 는 k 단계 뒤에 bit0 에 도착하고, 그 사이의 poly XOR 들은 XOR 이라 순서와 무관하게 더해진다. `crc_reflected` 는 w &lt; 8(CRC-4/G-704, CRC-5/USB)까지 쓰려고 비트 하나씩 넣는다.

### Part B — 덧붙이기와 잔여값

보내는 쪽: 데이터 뒤에 CRC 를 붙인다. 받는 쪽은 두 방법이 있다. (1) 데이터만 다시 계산해서 붙은 값과 비교, (2) **codeword 전체**를 엔진에 넣고 레지스터가 상수 residue 인지 확인. (2) 는 하드웨어 수신기가 하는 방식이다(프레임 끝을 몰라도 계속 넣다가 "지금 residue 인가" 만 보면 된다).

왜 상수인가 (xorout = 0, 비반사로 보이면 충분): 데이터 M 뒤에 R = (Init·x^L + M·x^w) mod G 를 붙인 codeword 의 다항식은 M·x^w + R. 이것까지 읽은 레지스터 = (Init·x^{L+w} + (M·x^w + R)·x^w) mod G = (x^w·(Init·x^L + M·x^w + R)) mod G = 0, 괄호 안이 G 의 배수이므로. xorout ≠ 0 이면 붙인 값이 R ⊕ X 라서 레지스터 = X·x^w mod G — 여전히 **데이터와 무관한 상수**. 반사형은 위의 거울 논리로 같다. 측정:

```text
residue check: 16 algorithms x 50 frames (ok + 1-bit flipped), bad=0
```

CRC-32/ISO-HDLC 의 residue 0xDEBB20E3, CRC-16/IBM-SDLC(X.25)의 0xF0B8 같은 "매직 넘버"가 이 상수다. 카탈로그 정의: residue = "초기화 후 오류 없는 codeword 를 읽고, refout 이면 반전한 레지스터"(xorout 적용 전). 그래서 `crc_residue_ok` 는 `crc_bitwise(...) ^ xorout == residue` 로 비교한다. width 가 8의 배수가 아닌 알고리즘(CRC-3/5/15)은 바이트로 덧붙일 수 없어서, 연습 1의 비트 API 로 residue 를 확인한다.

### Part B — 무엇을 보장하나 (증명)

**선형성.** 길이가 같은 두 메시지 a, b 에 대해 레지스터(a ⊕ b) = 레지스터(a) ⊕ 레지스터_{init=0}(b) (위 식이 Init 과 M 에 대해 선형이므로). 그래서 오류 패턴 E 가 들어간 codeword 를 받으면 레지스터 = residue ⊕ (E(x)·x^w mod G). G 는 상수항이 1 이라 gcd(x^w, G) = 1 이므로

**오류 E 를 놓친다 ⇔ G(x) 가 E(x) 를 나눈다.** 데이터·init·xorout 과 무관하다.

| 오류 종류 | 조건 | 이유 |
|---|---|---|
| 1비트 E = x^i | 항상 검출 | G 는 항이 2개 이상, x^i 의 약수는 x^k 뿐 |
| 홀수 개 비트 | (x+1) \| G 면 항상 검출 | E(1) = 1 (항이 홀수 개), G(1) = 0 → G ∤ E. 0x07·0x1021·0x8005 모두 항 개수 짝수 → x+1 인수 |
| 버스트 길이 b ≤ w | 항상 검출 | E = x^s·B, deg B = b−1 &lt; w, B ≠ 0 → G ∤ B, gcd(G, x^s) = 1 |
| 버스트 b = w+1 | 2^(w−2) 개 중 1개 놓침 = 2^−(w−1) | B 의 차수 w, 양 끝 1 → G \| B ⇔ B = G |
| 버스트 b > w+1 | 비율 2^−w | B = G·q, q 는 차수 b−1−w 이고 양 끝 1 → 2^(b−w−2) 개 / 2^(b−2) |
| 2비트 E = x^i(x^d + 1) | d 가 주기의 배수일 때만 놓침 | 주기 = x^d ≡ 1 (mod G) 인 최소 d. codeword 길이 N ≤ 주기면 2비트 전부 검출 |

여기서 "버스트"는 **CRC 가 읽는 비트 순서**에서 연속이다. 반사형(Modbus)은 바이트 안에서 LSB 먼저이므로, 바이트 안 MSB 먼저로 번호를 매긴 "버스트"는 바이트 경계에서 CRC 순서로는 연속이 아니다 — 이 레슨 작업 중 처음에 그렇게 짰더니 MODBUS 만 길이 ≤ 16 버스트 1114111 개 중 **4개를 놓쳤다** (IBM-3740 은 0). 선로 순서로 고친 뒤 0. 보장은 "선로에서 연속인 버스트"에 대한 것이고, 그래서 CRC 를 붙이는 바이트 순서도 선로 순서를 따라야 한다.

`solution.c` 전수 결과 (4바이트 데이터):

```text
CRC-8/SMBUS      40-bit codeword: 1..3-bit errors 10700 tried, 0 missed; bursts <= 8: 4351 tried, 0 missed
CRC-16/IBM-3740  48-bit codeword: 1..3-bit errors 18472 tried, 0 missed; bursts <= 16: 1114111 tried, 0 missed
CRC-16/MODBUS    48-bit codeword: 1..3-bit errors 18472 tried, 0 missed; bursts <= 16: 1114111 tried, 0 missed
```

(시도 수 검산: CRC-8 의 40비트에서 40 + C(40,2) + C(40,3) = 40 + 780 + 9880 = 10700.)

### 측정 — `demo_detect.c` (8바이트 데이터, 전수)

신드롬 S_i = x^i mod G 를 미리 구해 XOR 로 셌고(위 선형성), 무게 1·2 는 실제 엔진으로도 전수 대조했다 (마지막 열, 놓친 개수 0, 0 으로 신드롬과 일치). 실제 출력:

```text
[1] 8-byte data: undetected errors by weight (exhaustive)
crc               bits          w=1          w=2          w=3            w=4   engine check (w1,w2)
CRC-8/SMBUS         72     0/72         0/2556       0/59640    8224/1028790   0, 0
CRC-16/IBM-3740     80     0/80         0/3160       0/82160     162/1581580   0, 0
CRC-16/MODBUS       80     0/80         0/3160       0/82160     725/1581580   0, 0
```

- 1·2·3비트는 모두 검출 → 이 길이에서 해밍 거리(HD) ≥ 4. 4비트 오류는 놓치는 것이 있으므로 HD = 4.
- 같은 HD=4 라도 4비트 오류를 놓치는 개수는 0x1021 이 162, 0x8005 가 725 — 다항식의 "질"이 다르다. CRC-8 은 8224/1028790 ≈ 0.80%. 짝수 무게 오류는 E(1) = 0 이라 나머지도 (x+1) 의 배수 → 가능한 나머지가 2^7 = 128 가지뿐이므로, 무작위라면 1/128 ≈ 0.78% 를 예상할 수 있는 수준이다.

```text
[2] bursts in the CRC's bit order, 8-byte data codeword (exhaustive)  — 발췌
CRC-8/SMBUS        8       4160        0    0.000e+00    0.000e+00
CRC-8/SMBUS        9       8192       64    7.812e-03    7.812e-03
CRC-8/SMBUS       10      16128       63    3.906e-03    3.906e-03
CRC-16/IBM-3740   16    1064960        0    0.000e+00    0.000e+00
CRC-16/IBM-3740   17    2097152       64    3.052e-05    3.052e-05
CRC-16/IBM-3740   18    4128768       63    1.526e-05    1.526e-05
CRC-16/IBM-3740   20   15990784      244    1.526e-05    1.526e-05
CRC-16/MODBUS     17    2097152       64    3.052e-05    3.052e-05
```

이론과 소수 셋째 자리까지 같다. 놓친 개수 자체도 증명대로다: b = w+1 은 시작 위치마다 정확히 1개(B = G) → 시작 위치 수 64. b = w+2 는 q = x+1 하나 → 63, b = w+4 는 q 가 4개 → 61 × 4 = 244. 그래서 세 다항식의 버스트 표가 **똑같다** — 버스트 검출은 차수 w 만으로 정해진다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CRC-16 버스트 길이별 놓친 비율 (demo_detect 실측, 세로축 로그, 80비트 codeword)</text>
  <line x1="90" y1="40" x2="90" y2="210" stroke="currentColor"/>
  <line x1="90" y1="210" x2="720" y2="210" stroke="currentColor"/>
  <text x="20" y="64" fill="currentColor">2^-15</text>
  <line x1="86" y1="60" x2="720" y2="60" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="20" y="114" fill="currentColor">2^-16</text>
  <line x1="86" y1="110" x2="720" y2="110" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="34" y="206" fill="currentColor">0</text>
  <rect x="110" y="206" width="50" height="4" class="f-ok" stroke="currentColor"/>
  <rect x="200" y="206" width="50" height="4" class="f-ok" stroke="currentColor"/>
  <rect x="290" y="206" width="50" height="4" class="f-ok" stroke="currentColor"/>
  <rect x="380" y="206" width="50" height="4" class="f-ok" stroke="currentColor"/>
  <rect x="470" y="60" width="50" height="150" class="f-warn" stroke="currentColor"/>
  <rect x="560" y="110" width="50" height="100" class="f-acc" stroke="currentColor"/>
  <rect x="650" y="110" width="50" height="100" class="f-acc" stroke="currentColor"/>
  <text x="122" y="230" fill="currentColor">b=1</text><text x="212" y="230" fill="currentColor">b=8</text>
  <text x="298" y="230" fill="currentColor">b=15</text><text x="388" y="230" fill="currentColor">b=16</text>
  <text x="478" y="230" fill="currentColor">b=17</text><text x="568" y="230" fill="currentColor">b=18</text>
  <text x="658" y="230" fill="currentColor">b=20</text>
  <text x="110" y="190" fill="currentColor">b ≤ 16: 0 개 (보장)</text>
  <text x="430" y="52" fill="currentColor">64/2097152</text>
  <text x="520" y="102" fill="currentColor">63/4128768 · 244/15990784</text>
  <text x="10" y="252" fill="currentColor">IBM-3740 과 MODBUS 가 같은 값 — 버스트 검출은 G 의 차수만으로 정해진다</text>
</svg>
```

```text
[3] 2-bit errors vs data length (exhaustive, syndromes) and polynomial period  — 발췌
CRC-8/SMBUS      period(x mod G) = 127 bits
   data    15 B (   128-bit codeword): undetected 2-bit errors = 1
   data    16 B (   136-bit codeword): undetected 2-bit errors = 9
   engine, 16-byte data: flip bits 3 and 3+127 -> UNDETECTED; 3 and 3+126 -> detected
CRC-16/IBM-3740  period(x mod G) = 32767 bits
   data  4094 B ( 32768-bit codeword): undetected 2-bit errors = 1
   data  8000 B ( 64016-bit codeword): undetected 2-bit errors = 31249
   engine, 4095-byte data: flip bits 3 and 3+32767 -> UNDETECTED; 3 and 3+32766 -> detected
```

**실전 결론.** CRC-8/SMBUS(0x07)는 주기가 127 이라 데이터 14바이트까지만 2비트 오류를 전부 잡는다 — SMBus 블록 전송처럼 수십 바이트를 CRC-8 하나로 덮으면 2비트 보장이 깨진다. 0x1021·0x8005 는 주기 32767 (= 2^15 − 1; 둘 다 (x+1) × 차수 15 원시 다항식 꼴이라 이 값이 나온다), 데이터 4093바이트까지. 전체 실행 0.06 초.

### Part C — 센서 버스

**SMBus PEC.** Read Word 는 선로에 `[addr<<1|0] [cmd] (repeated start) [addr<<1|1] [lo] [hi] [PEC]` 가 흐른다. PEC 는 **주소 바이트 두 개를 포함한** 앞 5바이트 전체의 CRC-8/SMBUS. 그래서 엉뚱한 장치(0x41)가 대답하거나 다른 명령의 응답이 섞이면 PEC 가 틀린다 — 테스트가 그것을 확인한다. 워드는 하위 바이트 먼저. PMBus(BMC-B02)도 같은 PEC 를 쓴다. 5000개 랜덤 트랜잭션 + 1비트 오류 주입에서 오류 0.

**Sensirion 워드 CRC.** SHT3x 계열은 16비트 워드마다 CRC-8(poly 0x31, init 0xFF)을 붙인다. 카탈로그에서 같은 파라미터는 CRC-8/NRSC-5(check 0xF7)이고, 엔진이 데이터시트 예 CRC(0xBEEF) = 0x92 를 재현했다 (Python 직접 루프로도 0x92). 변환 T = −45 + 175·S/65535 °C 를 밀리도 정수로 하면 **175000·S 가 2^32 를 넘는다**: S ≥ 24543 이면 넘친다(7. 실수 6). 그래서 곱은 `uint64_t`(또는 Cortex-M 에서는 `umull` 한 개로 끝나는 32×32→64).

**Modbus RTU.** 요청 `01 03 0000 000A` 에 CRC-16/MODBUS 0xCDC5 를 **하위 바이트 먼저** 붙여 `… C5 CD`. 받는 쪽은 프레임 전체의 CRC 가 0(residue 0)인지만 본다. 3.5 문자 침묵으로 프레임을 자르는 일, 함수 코드별 길이 검사는 COMM-B13 이 다룬다.

### 복잡도

| 함수 | 시간 | 추가 메모리 | 스택·메모 |
|---|---|---|---|
| crc_bitwise | O(8n) 단계 (각 단계 시프트·XOR 몇 개) | O(1) | 재귀 없음, 지역 변수 몇 개 |
| crc_polydiv | O(8n·w) | static 비트 배열 8·256 + 32 바이트 | 교과서 확인용 |
| crc_reflected | O(8n) | O(1) | 비트 반전 없음 |
| crc_append / residue_ok | O(8n) | O(1) | |
| Part C 함수들 | O(프레임 길이) | 스택 ≤ 5 바이트 버퍼 | ISR 에서도 가능한 크기 |

최악 = 평균: 반복 횟수가 데이터와 무관하다. 다만 `if (top)` 분기형은 Cortex-M0 에서 비트마다 조건 분기가 생겨 **실행 시간이 데이터에 따라 조금씩 다르다**. `demo_asm.c` 를 `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S` 로 본 실제 결과: 분기형은 비트마다 `lsls / sxth / cmp / bpl / eors` (부호 비트로 bit15 검사 후 조건 분기), 마스크형 `crc = (crc << 1) ^ (0x1021 & -(crc >> 15 & 1))` 는 비트마다 `lsrs #15 / rsbs / ands / eors` 로 바이트 하나 안에 조건 분기가 없다. Cortex-M4 타깃에서는 마스크형이 4바이트씩 펼쳐지고 `ubfx` 로 비트를 뽑는다. 처리량 자체는 테이블이 훨씬 빠르다(ALGO-M02).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 40/40 passed, exit 0, 약 0.4 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace && ./demo_trace
cc -std=c11 -Wall -Wextra -Werror -O2 demo_detect.c -o demo_detect && ./demo_detect   # 약 0.06 초
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_asm.c -o -     # asm 만
```

이 맥: Apple clang 21.0.0, arm64 (Apple Silicon M-series).

## 7. 자주 하는 실수

**1. `(1u << w) - 1` 을 w = 32 에 쓴다.** C11 6.5.7: 시프트 양 ≥ 폭이면 UB. UBSan 실제 메시지 `shift exponent 32 is too large for 32-bit type 'unsigned int'`, 그리고 이 맥 -O0 결과 마스크 = **0x00000000** (arm64 의 가변 시프트는 양을 32 로 나눈 나머지만 쓰므로 1 &lt;&lt; 0 − 1 = 0). CRC-32 결과가 통째로 0 이 된다. 고친 코드: `w >= 32 ? 0xFFFFFFFFu : (1u << w) - 1u`.

**2. `uint8_t` 를 그대로 24 비트 민다.** MSB-first CRC-32 를 바이트 단위로 짤 때 `crc ^= d[i] << 24;` — d[i] 는 `int` 로 승격되고, 0x80 이상이면 부호 비트로 넘어가 UB. UBSan: `left shift of 128 by 24 places cannot be represented in type 'int'`. 고친 코드: `crc ^= (uint32_t)d[i] << 24;`. 16비트 int MCU(AVR, MSP430)에서는 `uint8_t << 8` 도 같은 문제가 생길 수 있으니 항상 먼저 넓은 부호 없는 타입으로 캐스트.

**3. 반사를 절반만 한다.** CRC-16/MODBUS 파라미터에서 실제 값(check 0x4B37 이어야 함):

| 잘못 | 결과 |
|---|---|
| refin = refout = false (반사 전부 빠뜨림) | 0xAEE7 |
| refin 만 true | 0xECD2 |
| refout 만 true | 0xE775 |
| 뒤집힌 poly 0xA001 을 MSB-first 엔진에 | 0xCF32 |
| 0xA001 + refin/refout (이중 반사) | 0x3D7B |

다섯 개가 전부 달라서 "어디가 틀렸는지"는 값만 봐서는 모른다. 카탈로그 check 값 하나로 6개 파라미터를 한꺼번에 확인하는 게 첫 테스트여야 하는 이유.

**4. 오른쪽 시프트판에서 init 을 reflect 하지 않는다.** Modbus(0xFFFF)·CRC-32(0xFFFFFFFF)는 init 이 대칭이라 티가 안 난다. 랜덤 init 3000개에서 **2613개** 틀렸다. 테스트는 대칭이 아닌 값으로.

**5. "init" 의 정의를 섞는다 (direct vs augmented).** 옛 증강 알고리즘(메시지 뒤 0 w 비트를 붙여 나누는 방식)에서 init 0xFFFF 는 direct 알고리즘의 init 0xFFFF·x^16 mod G = **0x1D0F** 와 같다. 그래서 같은 "CRC-CCITT, init 0xFFFF" 라는 설명으로 0x29B1(IBM-3740)과 0xE5CC(SPI-FUJITSU, 별칭 AUG-CCITT, direct init 0x1D0F)라는 두 값이 돌아다닌다. 엔진으로 두 값 모두 재현했고, 반대로 direct 0xFFFF 는 증강 init 0x84CF 에 해당한다(Python 으로 계산). 카탈로그 init 은 "첫 메시지 비트를 읽기 전 레지스터" — direct 기준으로 읽는다.

**6. 센서 값 변환에서 32비트 곱이 넘친다.** `175000u * S` 는 S ≥ 24543 에서 2^32 를 넘는다. 실측: S = 0x6666 에서 25000 m°C 가 나와야 하는데 uint32 로는 **−40537**, S = 0xFFFF 에서 130000 대신 −1074. CRC 는 맞는데 값이 틀리는 버그라 더 찾기 어렵다. `175000ull * S`.

**7. 버스트를 선로 순서가 아닌 순서로 센다 / CRC 바이트 순서를 바꿔 붙인다.** 반사형인데 CRC 를 상위 바이트 먼저 붙이면 잔여값 검사가 실패하고(값이 상수가 아니게 된다), 버스트 보장도 선로 순서로만 성립한다(위 Part B: MSB-first 번호로 센 MODBUS 버스트에서 4개 놓침).

## 8. 면접 Q&A

<details><summary>Q1. CRC 를 수학적으로 설명해 보세요. 왜 덧셈이 아니라 XOR 인가요?</summary>

비트열을 GF(2) 계수 다항식으로 보고 M(x)·x^w 를 G(x) 로 나눈 나머지를 붙인다. GF(2) 에서는 덧셈 = 뺄셈 = XOR 이라 자리올림이 없다. 자리올림이 없으니 비트 i 의 오류가 다른 자리로 번지지 않고, 전체가 **선형**이 된다: 놓치는 오류 = G 의 배수인 오류 다항식. 그래서 "어떤 오류를 잡나"를 G 의 대수적 성질(인수, 주기, 차수)로 증명할 수 있다. 덧셈 체크섬(ALGO-B14)은 자리올림과 mod 때문에 이런 깔끔한 보장이 없다. 정수 나눗셈이 아니라 다항식 나눗셈이라 하드웨어는 시프트 레지스터 + XOR 게이트 몇 개로 비트당 1클록에 끝난다.

</details>

<details><summary>Q2. 0x1021 은 무슨 뜻이고, 왜 x^16 항이 없죠? 0x8408 이나 0x8810 이라고 쓰는 문서도 있던데요.</summary>

G(x) = x^16 + x^12 + x^5 + 1. 최고차항은 항상 1 이라 생략하고 나머지 계수 x^15..x^0 을 16비트로 쓴 것이 normal 표기 0x1021. 레지스터에서 x^16 계수는 "밀려 나가는 비트"(몫)라 저장할 필요가 없다. 0x8408 은 같은 다항식의 reversed 표기(반사형 오른쪽 시프트 코드에 넣는 값 = reflect(0x1021, 16)), 0x8810 은 Koopman 표기(최고차항을 쓰고 +1 을 생략: (0x1021 >> 1) | 0x8000). 세 숫자는 같은 G 다. 면접에서는 "어느 표기냐"를 먼저 묻는다. 엔진에 reversed 값을 넣으면 0xCF32 같은 엉뚱한 값이 나온다(7. 실수 3).

</details>

<details><summary>Q3. init 을 0xFFFF 로, xorout 을 0xFFFF 로 두는 이유는?</summary>

init = 0 이면 레지스터가 0 인 동안 0 비트를 넣어도 0 이므로 **앞에 붙은 0x00** 을 못 본다(연습 2 실측: XMODEM 1000/1000 놓침, IBM-3740 0/1000). init ≠ 0 이면 Init·x^L 항이 길이 L 에 따라 바뀌어 잡힌다. xorout 은 받는 쪽이 잔여값으로 검사할 때 의미가 있다: xorout = 0 이면 올바른 codeword 뒤 레지스터가 0 이라 **뒤에 붙은 0x00** 도 계속 0 → 통과(연습 2: MODBUS 1000/1000 통과, IBM-SDLC·CRC-32 는 0/1000). xorout ≠ 0 이면 residue 가 0 이 아닌 상수라 0 이 더 들어오면 바뀐다. 둘 다 "선로가 0 에 붙는(stuck-at-0) 고장"과 길이 오류를 잡으려는 장치다. Modbus 는 프레임 길이를 침묵 구간·함수 코드로 따로 알기 때문에 xorout 0 으로도 버틴다.

</details>

<details><summary>Q4. CRC-16 이 보장하는 것을 말해 보세요. "65536 분의 1 확률로 놓친다"는 말은 맞나요?</summary>

보장: 1비트 전부, 홀수 개 비트 전부(G 에 x+1 인수, 0x1021·0x8005 해당), 선로 순서로 길이 ≤ 16 인 버스트 전부, codeword 가 주기(32767 비트) 이하면 2비트 전부 → 짧은 프레임에서 HD = 4 (이 레슨 측정: 80비트에서 3비트 이하 놓침 0, 4비트는 162/1581580 놓침). "2^−16 확률"은 **무작위 오류**(또는 길이 > 17 버스트)에 대한 비율이고, 실측도 b ≥ 18 에서 정확히 1.526e−5 = 2^−16, b = 17 은 2^−15 였다. 보장 범위 안의 오류는 확률이 아니라 0 이다. 그리고 HD 는 메시지 길이에 따라 떨어지므로 "이 다항식은 이 길이까지 HD 몇"으로 말해야 한다(Koopman 의 CRC 표가 그 자료).

</details>

<details><summary>Q5. 왜 어떤 CRC 는 반사형이죠? 하드웨어 CRC 주변장치와 값이 안 맞으면 어떻게 디버깅하나요?</summary>

UART·HDLC 는 LSB 먼저 보내고, 선로에 붙은 시프트 레지스터는 도착 순서대로 처리한다. 바이트로 보면 LSB 먼저 = refin, 결과도 같은 순서로 내보내니 refout, 그리고 CRC 는 하위 바이트부터 붙는다. 소프트웨어는 오른쪽 시프트 + reflect(poly) 로 짜면 반전 연산이 없다(Modbus 의 0xA001). 하드웨어 유닛과 안 맞을 때는: (1) `"123456789"` 를 넣어 check 값을 얻고 카탈로그와 비교해 어떤 알고리즘인지 확정, (2) 6개 파라미터를 하나씩(반사 4조합, init, xorout) 바꿔 보기, (3) 유닛의 입력 폭(바이트/워드 쓰기)과 엔디언 확인 — 32비트 워드로 넣으면 바이트 순서가 바뀐다. STM32 계열 CRC 유닛의 기본 설정은 poly 0x04C11DB7, init 0xFFFFFFFF, 반사 없음, xorout 없음 — 즉 CRC-32/MPEG-2 와 같은 설정으로 알려져 있다(칩 RM 확인 권장, COMM-B09 에서 직접 맞춘다).

</details>

<details><summary>Q6. width 가 32 인 엔진과 8 보다 작은 엔진을 하나의 코드로 짤 때 함정은?</summary>

(1) 마스크 `1u << 32` UB (실측 마스크 0 → CRC-32 가 0). (2) 최상위 비트를 `crc & 0x8000` 처럼 상수로 읽으면 width 를 못 바꾼다 → `crc >> (w-1)`. (3) "바이트를 레지스터 위 8비트에 XOR"(`crc ^= b << (w-8)`)는 w &lt; 8 에서 음수 시프트 → 비트 하나씩 넣거나 w &lt; 8 은 따로. (4) refout 반전은 w 비트 기준(reflect(x, w)), 8이나 16 고정이 아니다. (5) 바이트를 넓은 타입으로 캐스트하지 않으면 int 승격 UB. 이 레슨의 테스트가 width 1~32 랜덤 파라미터를 3000개 돌리는 이유다.

</details>

<details><summary>Q7. CRC 로 펌웨어 이미지 위변조를 막을 수 있나요?</summary>

없다. CRC 는 선형이라 공격자가 원하는 CRC 를 쉽게 만든다: 메시지 뒤 2바이트를 바꾸면 CRC-16 은 임의의 목표값이 되고, 그 2바이트는 **정확히 하나** 있다(연습 5 — s ↦ s·x^16 mod G 는 gcd(x^16, G) = 1 이라 가역인 선형 사상). 그래서 CRC 는 우연한 오류 검출용이고, 무결성/인증은 SHA-256 + 서명이나 HMAC 이 맡는다(BM-A04, 보안 부트). CRC 를 "암호학적 해시"로 착각한 프로토콜은 위조 공격을 받았다.

</details>

<details><summary>Q8. MCU 에서 비트 단위 CRC 는 바이트당 얼마나 걸리고, 언제 이걸로 충분한가요?</summary>

바이트당 8 단계 × (시프트, 비트 검사, 조건부 XOR) 수준이다 — Cortex-M0 실제 asm 은 비트당 4~5 명령(분기형 `lsls/sxth/cmp/bpl/eors`, 마스크형 `lsrs/rsbs/ands/eors`). 짧은 프레임(SMBus 5바이트, 센서 워드 2바이트, Modbus 8바이트)에는 이것으로 충분하고 Flash/RAM 을 0 바이트 더 쓴다. 수 KB 이미지나 고속 스트림이면 256 엔트리 테이블(CRC-16 이면 512 바이트), 니블 테이블(16 엔트리) 또는 하드웨어 유닛으로 간다 — ALGO-M02. 또 실행 시간을 데이터와 무관하게 만들고 싶으면(타이밍 예측) 마스크형을 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — 비트 단위 API 와 8의 배수가 아닌 길이

USB 토큰(11비트 + CRC-5)이나 CAN 헤더처럼 메시지가 바이트 단위가 아닐 때를 위해 `uint32_t crc_bits(const crc_params *p, const uint8_t *bits, size_t nbits)` 를 짜라 (`bits[i]` 는 읽는 순서 그대로 0/1). 그리고 이것으로 카탈로그 20개 **모두**의 residue 를 확인하라 — width 3·4·5·15 포함.

<details><summary>답 보기</summary>

레지스터 한 단계 `step(reg, in)` 을 nbits 번 부르고 refout·xorout. refin 은 "비트를 어떤 순서로 줄지"의 문제라 비트 API 에선 무시된다(호출자가 이미 읽는 순서로 준다). codeword 는 메시지 비트 뒤에 CRC 값을 **refout 이면 LSB 먼저, 아니면 MSB 먼저** 붙인다 — 레지스터가 내보내는 순서. 0~60 비트 랜덤 메시지로 20개 × 40개 모두 residue 일치:

```text
ex1: residue via bit codewords, 20 algorithms x 40 (0..60-bit msgs), bad=0
```

바이트 API 와의 일치(바이트 → 읽는 순서 비트로 풀어서)도 2000개로 확인.

</details>

### 연습 2 — init 은 앞쪽 0 을, xorout 은 뒤쪽 0 을

예측하고 측정하라: (a) 메시지 앞에 0x00 을 1~4개 붙여도 CRC 가 그대로인 알고리즘은 XMODEM 과 IBM-3740 중 어느 쪽? (b) 올바른 프레임 뒤에 0x00 을 1~4개 붙였을 때 잔여값 검사를 통과하는 것은 MODBUS·IBM-SDLC·CRC-32 중 어느 것?

<details><summary>답 보기</summary>

(a) 레지스터 = (Init·x^L + M·x^w) mod G. 앞에 0 을 붙이면 M 은 그대로, L 만 는다. Init = 0 이면 항이 없으니 불변(XMODEM), Init ≠ 0 이면 Init·x^L 이 바뀐다(G ∤ Init·(x^{L'} − x^L) — 주기의 배수만큼 붙이지 않는 한). (b) 잔여 레지스터가 0 이면 0 을 더 넣어도 0 → 통과(xorout = 0 인 MODBUS). xorout ≠ 0 이면 잔여 레지스터 = X·x^w mod G ≠ 0 이라 0 이 들어오면 바뀐다. 실제 출력:

```text
ex2: leading zeros unchanged: XMODEM 1000/1000, IBM-3740 0/1000
ex2: trailing zeros pass residue check: MODBUS 1000/1000, IBM-SDLC 0/1000, CRC-32 0/1000
```

</details>

### 연습 3 — 주기와 2비트 검출 한계

`uint32_t poly_period(unsigned w, uint32_t poly)` (x^k ≡ 1 mod G 인 최소 k) 와 `max_bytes_all_2bit(w, poly)` (모든 2비트 오류를 잡는 최대 데이터 바이트 수)를 짜고, CRC-8/SMBUS 의 경계를 엔진 전수로 확인하라.

<details><summary>답 보기</summary>

r = 1 에서 시작해 `r = step(r, 0)` (= r·x mod G) 을 반복하다 r = 1 이 되는 횟수. 상수항이 0 인 G(x | G)는 x 가 가역이 아니라 주기가 없다 → 0. codeword N = 8n + w 비트에서 2비트 오류의 거리 d ≤ N − 1 이므로 "모두 검출" ⇔ N − 1 &lt; 주기 ⇔ 8n ≤ 주기 − w → n = ⌊(주기 − w)/8⌋. CRC-8/SMBUS: ⌊119/8⌋ = 14, CRC-16 (0x1021): ⌊32751/8⌋ = 4093. CRC-15/CAN 은 주기 127 (카탈로그 poly 0x4599). 엔진 전수:

```text
ex3: CRC-8/SMBUS all 2-bit errors: 14-byte data missed 0, 15-byte data missed 1
```

15바이트(128비트 codeword)에서 놓치는 쌍은 거리 127 인 (첫 비트, 마지막 비트) 단 하나 — demo_detect 의 "1" 과 같다.

</details>

### 연습 4 — 미지의 CRC 파라미터 역추적 (RevEng 식)

로직 분석기로 어떤 센서의 프레임 5개(길이 8, 8, 8, 8, 13 바이트)와 각각의 CRC-16 을 잡았다. width = 16, refin = refout 만 가정하고 poly, init, xorout 을 찾아라. 해가 하나가 아니라면 왜인가?

<details><summary>답 보기</summary>

같은 길이 두 프레임의 CRC 를 XOR 하면 init·xorout 이 사라진다: crc(a) ⊕ crc(b) = crc_{init=0, xorout=0}(a ⊕ b). 그래서 poly(상수항 1 인 32768개) × 반사 2가지만 이 식으로 거르고, 남은 후보마다 init 65536개를 돌리며 xorout 은 첫 프레임에서 정하고 나머지 프레임(길이 13 포함)으로 검증한다. 0.1 초 안에 끝난다. 실제 출력:

```text
ex4: solution 1: poly=0x3D65 init=0x1234 refin=refout=1 xorout=0xFFFF
ex4: solution 2: poly=0x3D65 init=0xF917 refin=refout=1 xorout=0x3B28
ex4: the other solution agrees with hidden on 1000/1000 random messages (len 0..99)
```

해가 **두 개**이고 모든 메시지에서 같은 CRC 를 낸다. 이유: G = (x+1)·P 이면 (P 의 차수 15) P·x^L mod G = P·(x^L mod (x+1)) = P·1 = P — 길이와 무관한 상수다. 그래서 init 에 P 를 XOR 하면 결과 레지스터에 늘 P 가 XOR 되고, 이를 xorout 이 흡수한다. 검산: 0x1234 ⊕ 0xF917 = 0xEB23 = G/(x+1) (Python 다항식 나눗셈으로 확인), 0xFFFF ⊕ 0x3B28 = 0xC4D7 = reflect(0xEB23) (refout). 즉 (x+1) 인수를 가진 CRC 는 init/xorout 쌍이 항상 두 개씩 동치다 — 역추적 결과가 카탈로그와 init 이 달라도 틀린 게 아닐 수 있다.

</details>

### 연습 5 — CRC 위조

임의의 10바이트 메시지 뒤에 2바이트를 붙여 CRC-16/MODBUS(또는 IBM-3740)가 원하는 값이 되게 하라. 해는 몇 개인가?

<details><summary>답 보기</summary>

메시지 부분의 레지스터를 한 번 구해 두고(`crc_raw_update`), 2바이트 65536가지를 이어서 넣어 본다(가지당 16 단계). 해는 정확히 **1개**: 붙이는 s 에 대한 레지스터는 (r·x^16 + S(x)·x^16) mod G 꼴의 아핀 함수이고, S ↦ S·x^16 mod G 는 차수 &lt; 16 인 다항식 공간에서 일대일이다(gcd(x^16, G) = 1 이라 x^16 이 mod G 에서 가역). 16 비트 → 16 비트 일대일 = 전단사. 실제로는 연립 일차방정식(GF(2) 위 16×16)을 풀면 O(w^2) 에 나온다.

```text
ex5: forged 8 messages (2-byte suffix), bad=0
```

(bad = 0 은 "해의 개수 = 1, 그리고 붙인 결과 CRC = 목표" 를 8개 모두 만족.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 12/12 passed, 약 0.1 초
```

## 10. 핵심 정리

- CRC = GF(2) 에서 (Init·x^L + M·x^w) mod G. 레지스터 한 단계: `top = msb ^ in; crc <<= 1; if (top) crc ^= poly`. poly 는 x^w 를 뺀 normal 표기(0x07, 0x1021, 0x8005).
- 이름 말고 6개 파라미터 + check 값으로 말한다: CRC-8/SMBUS 0xF4, CRC-16/IBM-3740(= CCITT-FALSE) 0x29B1, CRC-16/MODBUS 0x4B37. "CCITT" 는 KERMIT 을 뜻하기도 한다.
- 반사 = 선로 순서(UART/HDLC LSB 먼저). 오른쪽 시프트 + reflect(poly)(0xA001) + reflect(init), CRC 는 하위 바이트 먼저.
- 받는 쪽: codeword 전체 → 레지스터 = residue(데이터와 무관한 상수). init ≠ 0 은 앞쪽 0, xorout ≠ 0 은 (잔여값 검사의) 뒤쪽 0 을 잡는다.
- 놓침 ⇔ G | E. 보장: 1비트, 홀수 비트((x+1) 인수), 버스트 ≤ w(선로 순서), 주기 이내 2비트. 버스트 w+1 은 2^−(w−1), 그 이상 2^−w (실측 일치).
- 0x07 은 주기 127 → 데이터 14바이트까지만 2비트 보장. 0x1021·0x8005 는 4093바이트까지, 짧은 프레임 HD = 4.
- 함정: `1u << 32`, `uint8_t << 24` 승격 UB, 반사 절반, 대칭 init 으로만 테스트, direct/augmented init(0xFFFF ↔ 0x1D0F), 센서 변환 곱셈 넘침.
- CRC 는 선형이라 위조가 쉽다 — 인증은 해시+서명. 속도는 테이블(ALGO-M02).

## 참고자료

- RevEng CRC Catalogue (Greg Cook) — 이 레슨의 20개 파라미터·check·residue 값의 출처: <https://reveng.sourceforge.io/crc-catalogue/> (1~15비트: `1-15.htm`, 16비트: `16.htm`, 17비트 이상: `17plus.htm`, 파라미터 정의: `legend.htm`). IBM-3740 의 별칭 CCITT-FALSE·AUTOSAR, KERMIT 의 별칭 CCITT·CCITT-TRUE 도 여기서 확인.
- Ross N. Williams, "A Painless Guide to CRC Error Detection Algorithms" (1993) — Rocksoft 모델(width/poly/init/refin/refout/xorout), direct vs 증강 알고리즘.
- Philip Koopman, "Cyclic Redundancy Code (CRC) Polynomial Selection For Embedded Networks" (DSN 2004, Koopman & Chakravarty) 와 Koopman 의 CRC Zoo — 다항식별 길이-HD 표, Koopman 표기. 이 레슨의 HD/주기 값은 직접 측정한 것.
- W. W. Peterson, D. T. Brown, "Cyclic Codes for Error Detection", Proc. IRE 49(1), 1961 — 버스트·홀수 오류 검출 정리의 원 논문 (서지 세부는 확인 권장).
- SMBus Specification 2.0 — PEC = CRC-8 (x^8 + x^2 + x + 1), 주소 바이트 포함. PMBus 도 동일.
- Sensirion SHT3x-DIS 데이터시트 — 워드 CRC(poly 0x31, init 0xFF, 예 0xBEEF → 0x92)와 T/RH 변환식. 예 값은 이 레슨에서 엔진·Python 으로 재현.
- Modbus over Serial Line Specification and Implementation Guide (Modbus.org) — RTU CRC-16, 하위 바이트 먼저. 예 프레임 `01 03 00 00 00 0A C5 CD` 는 직접 계산해 확인.
- Python `binascii.crc_hqx`, `zlib.crc32` — check 값 교차 확인에 사용.
- 관련 레슨: ALGO-B14 (덧셈 체크섬과 검출 실험), ALGO-B13 (패리티·비트 트릭), ALGO-M02 (테이블·slicing·CRC-32 가속), ALGO-M12 (NMEA XOR 체크섬 파서), COMM-B08 (체크섬 vs CRC 검출 실험), COMM-B09 (하드웨어 CRC 유닛 맞추기), COMM-B13 (Modbus RTU 프레임), COMM-M04 (CAN CRC-15·비트 스터핑), BMC-B02 (PMBus), RF-B09 (BLE CRC-24), ARM-A06 (ARMv8 CRC32 명령), BM-A04 (이미지 CRC/SHA-256), AUTO-M05 (E2E CRC), TEST-M11 (Flash CRC 자가진단).

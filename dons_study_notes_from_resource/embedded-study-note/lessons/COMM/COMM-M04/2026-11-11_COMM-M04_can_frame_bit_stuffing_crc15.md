---
id: COMM-M04
time: 90분
updated: 2026-11-11
---
# COMM-M04 CAN 프레임 구조: 비트 스터핑과 CRC-15

## 학습 목표

- Classic CAN 2.0A/B 데이터·리모트 프레임의 필드(SOF, ID, RTR/SRR, IDE, r0/r1, DLC, DATA, CRC, ACK, EOF)를 **비트 번호까지** 외워서 쓰고, 표준 ID 와 확장 ID 가 어디서 갈라지는지 설명한다.
- CRC-15/CAN(poly 0x4599)을 시프트 레지스터로 구현하고, check 값 0x059E 와 "프레임 + 자기 CRC → 나머지 0" 으로 스스로 검증한다.
- 비트 스터핑(bit stuffing)을 인코더와 **비트 하나씩 받는 디스터퍼**로 구현한다. 스터프 비트가 다음 연속에 들어가는 점, 마지막 CRC 비트 뒤에도 들어가는 점을 코드와 그림으로 보인다.
- 수신기가 잡는 오류(stuff / CRC / form)를 일부러 넣어 보고, 단일 비트 오류는 모두 잡지만 **2비트 오류는 스터핑 때문에 드물게 빠져나간다**는 것을 실측한다.
- 최악 스터핑 수에 대한 Davis/Burns 상한 ⌊(g−1)/4⌋ 와 DP 로 구한 정확한 최댓값을 비교해, 버스 부하 계산에 쓸 프레임 길이를 숫자로 말한다.

## 1. 문제

CAN 은 차량·로봇·드론의 기본 필드버스다. 면접에서는 "스터핑 규칙이 정확히 뭐죠? 어디부터 어디까지? CRC 는 스터핑 전 비트로 계산하나요?", bring-up 에서는 "로직 애널라이저(logic analyzer, LA)에 찍힌 비트가 왜 프레임마다 길이가 다르지?" 가 바로 나온다. 둘 다 프레임을 비트 단위로 직접 만들어 본 사람만 막힘없이 답한다.

CAN 컨트롤러(STM32 bxCAN/FDCAN, NXP FlexCAN, MCP2515)는 이 일을 하드웨어로 한다. 펌웨어는 메일박스에 ID·DLC·데이터만 넣는다. 그래도 비트 수준을 알아야 하는 때가 있다.

- LA·오실로스코프 캡처를 읽을 때 (HWLAB-M05/M06): 스터프 비트를 빼야 ID 가 보인다.
- 버스 부하·최악 응답 시간을 계산할 때 (COMM-M05, COMM-M08): 프레임 길이가 데이터에 따라 달라지고, 최악값이 필요하다.
- 오류 카운터(TEC/REC)가 왜 오르는지 볼 때 (COMM-M07): stuff error 인지 CRC error 인지 form error 인지가 원인을 가리킨다.
- FPGA·소프트웨어 CAN, 검증 장비, fault injection 테스트를 만들 때 (SIV 트랙).

이 레슨은 CAN 프레임 인코더/디코더를 비트 단위로 짠다. 그리고 **기억에 의존하지 않고** 검증한다. 기준은 다섯 가지다. (1) CRC check 값, (2) 나머지 0, (3) 수만 개 랜덤·전수 왕복, (4) 따로 짠 Python 인코더(`verify_frame.py`)의 비트열, (5) 최악 스터핑 공식과 직접 탐색의 비교. CAN 의 버스 규칙은 wired-AND 다. **dominant = 0, recessive = 1** 이고, 한 노드라도 0 을 내면 버스는 0 이다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">Classic CAN 데이터 프레임 — 필드와 비트 수 (괄호 = 비트 수, 0 = dominant, 1 = recessive)</text>
  <text x="10" y="48" fill="currentColor" font-weight="bold">표준 (2.0A)</text>
  <g stroke="currentColor">
    <rect x="10" y="56" width="30" height="34" class="f-mut"/><rect x="40" y="56" width="110" height="34" class="f-acc"/>
    <rect x="150" y="56" width="34" height="34" class="f-acc"/><rect x="184" y="56" width="30" height="34" class="f-mut"/>
    <rect x="214" y="56" width="26" height="34" class="f-mut"/><rect x="240" y="56" width="50" height="34" class="f-mut"/>
    <rect x="290" y="56" width="150" height="34" class="f-ok"/><rect x="440" y="56" width="110" height="34" class="f-warn"/>
    <rect x="550" y="56" width="26" height="34" class="f-mut"/><rect x="576" y="56" width="30" height="34" class="f-mut"/>
    <rect x="606" y="56" width="26" height="34" class="f-mut"/><rect x="632" y="56" width="70" height="34" class="f-mut"/>
    <rect x="702" y="56" width="48" height="34" fill="none" stroke-dasharray="3 3"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="25" y="72">SOF</text><text x="25" y="86">(1) 0</text>
    <text x="95" y="72">ID[10:0]</text><text x="95" y="86">(11)</text>
    <text x="167" y="72">RTR</text><text x="167" y="86">(1)</text>
    <text x="199" y="72">IDE</text><text x="199" y="86">(1) 0</text>
    <text x="227" y="72">r0</text><text x="227" y="86">(1)</text>
    <text x="265" y="72">DLC</text><text x="265" y="86">(4)</text>
    <text x="365" y="72">DATA</text><text x="365" y="86">(8n, n = 0..8)</text>
    <text x="495" y="72">CRC</text><text x="495" y="86">(15)</text>
    <text x="563" y="72">CD</text><text x="563" y="86">1</text>
    <text x="591" y="72">ACK</text><text x="591" y="86">(1)</text>
    <text x="619" y="72">AD</text><text x="619" y="86">1</text>
    <text x="667" y="72">EOF</text><text x="667" y="86">(7) 1</text>
    <text x="726" y="72">IFS</text><text x="726" y="86">(3) 1</text>
  </g>
  <path d="M10 98 L10 104 L550 104 L550 98" fill="none" stroke="currentColor"/>
  <text x="280" y="118" fill="currentColor" text-anchor="middle">스터핑 구간 = SOF..CRC 끝 (34 + 8n 비트) — 이 뒤는 고정 형식, 스터핑 없음</text>
  <path d="M10 124 L10 130 L440 130 L440 124" fill="none" stroke="currentColor"/>
  <text x="225" y="144" fill="currentColor" text-anchor="middle">CRC-15 계산 범위 = SOF..DATA 끝 (스터핑 전 비트)</text>
  <text x="10" y="178" fill="currentColor" font-weight="bold">확장 (2.0B)</text>
  <g stroke="currentColor">
    <rect x="10" y="186" width="30" height="34" class="f-mut"/><rect x="40" y="186" width="90" height="34" class="f-acc"/>
    <rect x="130" y="186" width="34" height="34" class="f-acc"/><rect x="164" y="186" width="30" height="34" class="f-acc"/>
    <rect x="194" y="186" width="130" height="34" class="f-acc"/><rect x="324" y="186" width="34" height="34" class="f-acc"/>
    <rect x="358" y="186" width="26" height="34" class="f-mut"/><rect x="384" y="186" width="26" height="34" class="f-mut"/>
    <rect x="410" y="186" width="44" height="34" class="f-mut"/><rect x="454" y="186" width="120" height="34" class="f-ok"/>
    <rect x="574" y="186" width="90" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="25" y="202">SOF</text><text x="25" y="216">0</text>
    <text x="85" y="202">ID-A[28:18]</text><text x="85" y="216">(11)</text>
    <text x="147" y="202">SRR</text><text x="147" y="216">1</text>
    <text x="179" y="202">IDE</text><text x="179" y="216">1</text>
    <text x="259" y="202">ID-B[17:0]</text><text x="259" y="216">(18)</text>
    <text x="341" y="202">RTR</text><text x="341" y="216">(1)</text>
    <text x="371" y="202">r1</text><text x="397" y="202">r0</text>
    <text x="432" y="202">DLC</text><text x="432" y="216">(4)</text>
    <text x="514" y="202">DATA (8n)</text><text x="619" y="202">CRC (15)</text>
    <text x="700" y="210">+ CD ACK AD EOF</text>
  </g>
  <text x="10" y="244" fill="currentColor">스터핑 전 비트 번호(SOF = 0) — 표준: RTR 12, IDE 13, DLC 15..18, DATA 19..</text>
  <text x="10" y="264" fill="currentColor">확장: SRR 12, IDE 13, ID-B 14..31, RTR 32, DLC 35..38, DATA 39.. (같은 base ID 면 12번에서 표준 데이터 승)</text>
  <text x="10" y="284" fill="currentColor">프레임 길이(스터핑 전): 표준 44 + 8n, 확장 64 + 8n 비트 (+ IFS 3). 리모트 프레임은 RTR=1, DATA 없음(DLC 는 요청 길이).</text>
</svg>
```

그림의 CD 는 CRC delimiter, AD 는 ACK delimiter 다.

### Part A — CRC-15, 필드, 스터핑, 인코더

- `crc15_update(crc, bit)`: ISO 11898-1 의 CRC_RG 시프트 레지스터를 그대로 쓴다. 다항식은 x^15+x^14+x^10+x^8+x^7+x^4+x^3+1 = 0x4599, init 0, 반사·xorout 없음. `crc15_bytes("123456789") == 0x059E` (RevEng CRC-15/CAN check) 여야 한다.
- `can_build_fields(f, bits, cap)`: SOF..CRC 끝까지 **스터핑 전** 비트열을 만든다(표준 34+8n, 확장 54+8n). ID 범위 초과·DLC > 15 이면 `CAN_E_ARG`, 용량 부족이면 `CAN_E_CAP` 을 돌려주고 아무것도 쓰지 않는다. DLC 9..15 는 Classic CAN 에서 8 바이트로 다룬다.
- `can_stuff(in, n, out, cap, &nstuff)`: 같은 값 5개(스터프 비트 포함) 뒤에 반대 비트를 넣는다. 마지막 입력 비트 뒤에도 넣는다. cap 밖에는 절대 쓰지 않는다.
- `can_encode(f, acked, out, cap, &nstuff)`: 스터핑된 SOF..CRC 뒤에 CRC delimiter(1), ACK slot, ACK delimiter(1), EOF(1×7)를 붙인다. `acked = false` 이면 송신기가 내보내는 값(ACK slot 1), `true` 이면 다른 노드가 덮어쓴 버스 값(ACK slot 0)이다.
- 알려진 프레임 5개(표준/확장, 데이터/리모트, 0x00×8, 0x1FFFFFFF)를 Python 독립 구현의 비트열과 **문자열로** 대조한다.

### Part B — 비트 단위 수신 상태 기계와 결함 주입

- `can_rx_bit(rx, bit)` 디스패처는 주어진다. idle 에서 첫 dominant 를 SOF 로 보고, 상태별 함수 세 개를 부른다.
  - `rx_destuff`: 스터프 자리(같은 값 5개 다음)면 반대 값인지 확인하고 버린다. 아니면 저장한다. DLC 를 받는 순간(표준 19번째, 확장 39번째 비트) 남은 길이 `need` 를 정한다.
  - `rx_end_stuffed`: 필드를 꺼내고 CRC 를 비교한다.
  - `rx_tail`: CRC delimiter, ACK, ACK delimiter, EOF 의 고정 형식을 검사한다.
- 넣어서 확인할 결함과 기대 결과는 다음과 같다.
  - 스터프 비트 뒤집기 → **그 위치에서** `CAN_ERR_STUFF`
  - 데이터·ID·CRC 비트를 바꾸고 옛 CRC 유지 → `CAN_ERR_CRC`
  - CRC delimiter·ACK delimiter·EOF 1~6번째 비트를 dominant 로 → `CAN_ERR_FORM` (위치 포함)
  - EOF 7번째 dominant → OK + overload 표시
  - ACK slot recessive → 수신기는 OK, `acked = false`
  - 잘린 입력·전부 idle → `CAN_ERR_TRUNC`
- 단일 비트 전수 뒤집기에서는 "잡힘" 또는 "내용이 같음(ACK slot, EOF 7번째)" 만 나와야 한다. 2비트 오류 실험은 `demo_flip.c` 에서 한다.

### Part C — LA 주석과 최악 프레임 길이

- `can_fields(rx, spans, cap)`: 받은 프레임의 필드별 **와이어 비트 구간**(스터프 비트 포함)과 값을 채운다. LA 디코더가 화면에 그리는 바로 그 정보다. 500 kbit/s 에서 µs 로 바꿔 출력한다.
- `can_frame_bits_bound(n, ext)`: Davis/Burns/Bril/Lukkien(2007) 최악 프레임 길이 공식을 구현한다(IFS 3 포함). 표준 8n + 47 + ⌊(34+8n−1)/4⌋, 확장 8n + 67 + ⌊(54+8n−1)/4⌋.
- `max_stuff_exhaustive(n, ...)`: DLC 0, 1 표준 데이터 프레임을 **모든 ID × 모든 데이터**로 인코드해 실제 최대 스터프 수를 구한다. `demo_worst.c` 는 DP 로 DLC 0..8 의 정확한 최댓값을 구하고 상한과 비교한다.

## 2. Problem (English)

Implement a bit-exact Classic CAN (ISO 11898-1, CAN 2.0A/B) frame encoder and a bit-serial receiver. Bus levels: 0 = dominant, 1 = recessive. One `uint8_t` per bit, no heap.

```c
typedef struct { uint32_t id; bool ide, rtr; uint8_t dlc; uint8_t data[8]; } can_frame_t;
#define CAN_E_CAP (-1)   /* output too small */
#define CAN_E_ARG (-2)   /* id out of range (11/29 bit), dlc > 15 */

uint16_t crc15_update(uint16_t crc, unsigned bit);                     /* poly 0x4599, init 0 */
int  can_build_fields(const can_frame_t *f, uint8_t *bits, size_t cap); /* SOF..CRC, unstuffed */
int  can_stuff(const uint8_t *in, size_t n, uint8_t *out, size_t cap, int *nstuff);
int  can_encode(const can_frame_t *f, bool acked, uint8_t *out, size_t cap, int *nstuff);

typedef enum { CAN_BUSY, CAN_OK, CAN_ERR_STUFF, CAN_ERR_CRC, CAN_ERR_FORM, CAN_ERR_TRUNC } can_status;
can_status can_rx_bit(can_rx_t *rx, unsigned bit);       /* feed one bus bit; idle until first dominant */
int  can_fields(const can_rx_t *rx, can_span *sp, int cap);              /* LA-style field spans */
int  can_frame_bits_bound(int n, bool ext);                              /* worst case incl. 3-bit IFS */
```

Rules: stuff from SOF through the last CRC bit; after five equal consecutive bits (stuff bits count), insert the complement, including after the final CRC bit. CRC covers the unstuffed SOF..DATA bits. Six equal bits in the stuffed region is a stuff error. CRC delimiter, ACK delimiter, and EOF are fixed-form (recessive). A dominant last EOF bit is not an error for a receiver. Classic DLC 9..15 means 8 data bytes.

Examples:

```text
crc15("123456789" as bytes, MSB first)  -> 0x059E
stuff 0000011111                         -> 0000011111 with stuff: 00000[1]1111[0]1   (12 bits)
std ID 0x123, DLC 2, data 11 22 (transmitter view, 62 bits, 2 stuff bits at wire 17 and 40):
  00010010001100000110000100010010001000001100101101111111111111
std ID 0x000, DLC 0                      -> 34 zero bits unstuffed (CRC = 0), 6 stuff bits, 50 bits on the wire
```

Follow-ups: Why is the stuff bit counted in the next run? Why does including SOF in the CRC not change it? Why does a single flipped stuff bit always cause a stuff error at exactly that bit, while two flips can slip through? What is the longest 8-byte standard frame, and at 500 kbit/s how long does it take?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데다. Part A 4개(`crc15_update`, `can_build_fields`, `can_stuff`, `can_encode`), Part B 3개(`rx_destuff`, `rx_end_stuffed`, `rx_tail`), Part C 3개(`can_fields`, `can_frame_bits_bound`, `max_stuff_exhaustive`)다. 하네스, `crc15_bits`/`crc15_bytes`, 디스패처 `can_rx_bit`, `can_decode`, 알려진 비트열 표는 주어진다. 디코더 시험의 일부(고정 필드, LA 주석)는 인코더 대신 Python 이 만든 K1 비트열을 입력으로 쓴다. 그래서 인코더가 틀려도 디코더만 따로 맞춰 볼 수 있다. 처음 실행 결과(실제 출력, 중간 생략):

```text
CRC-15/CAN("123456789") = 0x0000
FAIL starter.c:261  crc15_bytes(s9, 9) == 0x059Eu
FAIL starter.c:266  can_build_fields(&f1, raw, sizeof raw) == 34 + 16
…
stuff-bit flips: 0 tested, wrong=2000
…
FAIL starter.c:501  m0 == 6 && m1 == 7
6/39 passed
```

(exit 1, 멈추지 않는다. 스텁은 `CAN_E_CAP`, `DS_STUFF_ERR`, `rx_fail(...)` 같은 안전한 틀린 값을 돌려준다.)

## 4. 힌트

<details><summary>힌트 1 — CRC-15 는 "들어온 비트 XOR 맨 위 비트"</summary>

`fb = bit ^ crc[14]`, `crc = (crc << 1) & 0x7FFF`, `fb` 이면 `crc ^= 0x4599`. 메시지 · x^15 를 g(x) 로 나눈 나머지와 같다(init 0 이라 긴 나눗셈 그대로). 바이트를 넣을 때는 MSB 부터 넣는다. ALGO-M01 의 비트 단위 CRC 와 같은 구조이고, 폭만 15 비트다.

</details>

<details><summary>힌트 2 — 확장 ID 는 11 + 18 로 쪼갠다</summary>

ID-A = `id >> 18`(상위 11비트), ID-B = `id & 0x3FFFF`. 순서는 SOF, ID-A, **SRR=1, IDE=1**, ID-B, RTR, r1=0, r0=0, DLC 다. 표준의 RTR 자리(12번)에 SRR 이 오는 구조라서, 디코더는 13번 비트(IDE)를 보기 전까지 표준인지 확장인지 모른다.

</details>

<details><summary>힌트 3 — 스터핑: "넣고 나서 센다"</summary>

비트를 내보낸 뒤 `run` 을 갱신한다. `run == 5` 이면 반대 비트를 하나 더 내보내고, `last` 를 그 비트로, `run = 1` 로 바꾼다. 스터프 비트가 새 연속의 첫 비트가 되는 것이다. 이 순서로 하면 마지막 CRC 비트 뒤의 스터프 비트도 자동으로 들어간다.

</details>

<details><summary>힌트 4 — 디스터퍼는 길이를 모른 채 시작한다</summary>

`need` 를 처음에 최댓값(118)으로 둔다. 받은 비트 수가 19 이고 IDE(13번)=0 이면 `need = 34 + 8·len(RTR=12번, DLC=15..18번)` 이다. 39 이고 IDE=1 이면 `need = 54 + 8·len(RTR=32번, DLC=35..38번)` 이다. `nu == need` 가 되어도 `run == 5` 면 아직 끝이 아니다. 다음 와이어 비트는 스터프 비트여야 한다.

</details>

<details><summary>힌트 5 — 필드 구간은 upos[] 로</summary>

디스터퍼가 저장할 때 `upos[k] = 와이어 위치` 를 함께 적어 두면, 필드의 와이어 구간은 `[upos[시작], upos[끝]]` 이다(사이에 낀 스터프 비트도 포함된다). CRC 의 끝은 `crc_end`(뒤따른 스터프 비트 포함)다. 그 뒤 고정 필드는 crc_end+1, +2, +3, +4..+10 이다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **레벨부터**: dominant 0, recessive 1, wired-AND. idle = recessive. SOF 는 dominant 하나다.
2. **필드**: SOF, 중재 필드(ID [+SRR IDE ID-B] RTR), 제어 필드(IDE/r1 r0 DLC), 데이터, CRC 15 + delimiter, ACK slot + delimiter, EOF 7, IFS 3.
3. **CRC 범위**: SOF..DATA 의 스터핑 **전** 비트다. **스터핑 범위**: SOF..CRC 끝이다. 고정 형식 필드(delimiter, ACK delim, EOF)에는 스터핑이 없다. 그래서 6개 연속 1 인 EOF 가 "프레임 끝" 표시가 된다.
4. **왜 스터핑하나**: 수신기는 recessive→dominant 에지로 재동기(resynchronization)를 한다. 같은 값이 길게 이어지면 에지가 없어 클럭이 밀린다. 5비트마다 에지를 보장하는 장치다(COMM-M06 의 SJW 와 연결).
5. **검증 방법**까지 말한다: check 값, 나머지 0, 독립 구현 대조.

### Part A — CRC-15/CAN

| 파라미터 | 값 |
|---|---|
| 폭 | 15 |
| 다항식 | x^15+x^14+x^10+x^8+x^7+x^4+x^3+1 → 0x4599 (최고차항 뺀 표기) |
| init / refin / refout / xorout | 0 / false / false / 0 |
| check ("123456789", MSB first) | **0x059E** — C 시프트 레지스터와 Python 긴 나눗셈 둘 다 실측 |
| 나머지(residue) | 0: 프레임 비트 + 자기 CRC 를 다시 넣으면 0 (랜덤 3000 프레임 실측) |

init 이 0 이라 앞에 붙은 0 비트는 CRC 를 바꾸지 않는다. 그래서 SOF(항상 0)를 CRC 에 넣든 빼든 값이 같다(연습 5). 이 성질은 양날이다. "앞에 0 이 더 붙은" 오류는 CRC 로 못 잡는다. CAN 에서는 SOF 가 하드 동기 기준점이라 그런 오류가 생기지 않는다.

### Part A — 필드 배치 (스터핑 전 비트 번호)

| 비트 | 표준 | 확장 |
|---|---|---|
| 0 | SOF (0) | SOF (0) |
| 1..11 | ID[10:0] | ID-A = ID[28:18] |
| 12 | RTR (데이터 0, 리모트 1) | SRR (1) |
| 13 | IDE (0) | IDE (1) |
| 14 | r0 (0) | ID-B 시작 (ID[17:0] → 14..31) |
| 15..18 | DLC | — |
| 19.. | DATA, 그 뒤 CRC 15 | 32 RTR, 33 r1, 34 r0, 35..38 DLC, 39.. DATA |
| 스터핑 구간 길이 | 34 + 8n | 54 + 8n |

r0·r1·SRR 은 송신기가 정해진 값(r0/r1 = 0, SRR = 1)을 보낸다. 수신기는 어느 값이든 받아들인다(ISO 11898-1 확인 권장). 그래서 디코더는 이 비트를 검사하지 않는다. 또 CAN 2.0A 는 상위 7비트가 모두 recessive 인 ID(0x7F0..0x7FF)를 금지한다고 알려져 있다(Bosch 2.0 확인 권장). 이 모델은 ID 를 제한하지 않는다.

### Part A — 스터핑 규칙과 K1 추적

규칙은 "보낼 비트열에 같은 값이 5개 이어지면 바로 반대 값 하나를 끼운다" 이다. 끼운 비트는 다음 연속의 첫 비트로 센다. 그래서 `0000011111` 은 `00000[1]1111[0]1` 이 된다. 두 번째 스터프는 입력 1 네 개 + 스터프 1 = 5 에서 나온다.

K1 = 표준 ID 0x123, DLC 2, DATA 11 22 을 따라가 보자. 언스터프 50비트(34 + 16), 스터프 2개, 와이어 62비트다.

| 와이어 | 내용 | 연속 | 비고 |
|---|---|---|---|
| 0 | SOF 0 | 0×1 | |
| 1..11 | ID 001 0010 0011 | … 마지막 1 | |
| 12, 13, 14 | RTR 0, IDE 0, r0 0 | 0×3 | |
| 15, 16 | DLC 비트3 0, 비트2 0 | 0×5 | → 다음은 스터프 |
| **17** | **스터프 1** | 1×1 | DLC 필드 **안** |
| 18, 19 | DLC 비트1 1, 비트0 0 | | DLC 구간 = 와이어 15..19 |
| 20..35 | 0001 0001, 0010 0010 | 마지막 0×1 | |
| 36..39 | CRC 0x04B7 의 앞 4비트 0000 | 0×5 | DATA 마지막 0 과 이어짐 |
| **40** | **스터프 1** | | CRC 필드 **안** |
| 41..51 | CRC 나머지 100 1011 0111 | | |
| 52..61 | CD 1, ACK, AD 1, EOF 1111111 | | 고정 형식 |

```svg
<svg viewBox="0 0 760 242" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">ID 0x123, DLC 2, DATA 11 22 — 버스 관점 와이어 비트 62개 (위 = recessive 1, 아래 = dominant 0)</text>
  <path d="M31.0 52 L31.0 58 L40.2 58 L40.2 52" fill="none" stroke="currentColor"/><text x="35.6" y="48" fill="currentColor" text-anchor="middle">SOF</text>
  <path d="M42.2 80 L42.2 86 L163.4 86 L163.4 80" fill="none" stroke="currentColor"/><text x="102.8" y="76" fill="currentColor" text-anchor="middle">ID 0x123</text>
  <path d="M165.4 52 L165.4 58 L174.6 58 L174.6 52" fill="none" stroke="currentColor"/><text x="170.0" y="48" fill="currentColor" text-anchor="middle">RTR</text>
  <path d="M176.6 80 L176.6 86 L185.8 86 L185.8 80" fill="none" stroke="currentColor"/><text x="181.2" y="76" fill="currentColor" text-anchor="middle">IDE</text>
  <path d="M187.8 52 L187.8 58 L197.0 58 L197.0 52" fill="none" stroke="currentColor"/><text x="192.4" y="48" fill="currentColor" text-anchor="middle">r0</text>
  <path d="M199.0 80 L199.0 86 L253.0 86 L253.0 80" fill="none" stroke="currentColor"/><text x="226.0" y="76" fill="currentColor" text-anchor="middle">DLC 2</text>
  <path d="M255.0 52 L255.0 58 L432.2 58 L432.2 52" fill="none" stroke="currentColor"/><text x="343.6" y="48" fill="currentColor" text-anchor="middle">DATA 11 22</text>
  <path d="M434.2 80 L434.2 86 L611.4 86 L611.4 80" fill="none" stroke="currentColor"/><text x="522.8" y="76" fill="currentColor" text-anchor="middle">CRC 0x04B7</text>
  <path d="M613.4 52 L613.4 58 L622.6 58 L622.6 52" fill="none" stroke="currentColor"/><text x="618.0" y="48" fill="currentColor" text-anchor="middle">CD</text>
  <path d="M624.6 80 L624.6 86 L633.8 86 L633.8 80" fill="none" stroke="currentColor"/><text x="629.2" y="76" fill="currentColor" text-anchor="middle">A</text>
  <path d="M635.8 52 L635.8 58 L645.0 58 L645.0 52" fill="none" stroke="currentColor"/><text x="640.4" y="48" fill="currentColor" text-anchor="middle">AD</text>
  <path d="M647.0 80 L647.0 86 L723.4 86 L723.4 80" fill="none" stroke="currentColor"/><text x="685.2" y="76" fill="currentColor" text-anchor="middle">EOF</text>
  <rect x="220.4" y="112" width="11.2" height="46" class="f-warn" opacity="0.7"/>
  <rect x="478.0" y="112" width="11.2" height="46" class="f-warn" opacity="0.7"/>
  <rect x="623.6" y="112" width="11.2" height="46" class="f-ok" opacity="0.6"/>
  <polyline points="16.0,120 30.0,120 30.0,150 41.2,150 41.2,150 52.4,150 52.4,150 63.6,150 63.6,120 74.8,120 74.8,150 86.0,150 86.0,150 97.2,150 97.2,120 108.4,120 108.4,150 119.6,150 119.6,150 130.8,150 130.8,150 142.0,150 142.0,120 153.2,120 153.2,120 164.4,120 164.4,150 175.6,150 175.6,150 186.8,150 186.8,150 198.0,150 198.0,150 209.2,150 209.2,150 220.4,150 220.4,120 231.6,120 231.6,120 242.8,120 242.8,150 254.0,150 254.0,150 265.2,150 265.2,150 276.4,150 276.4,150 287.6,150 287.6,120 298.8,120 298.8,150 310.0,150 310.0,150 321.2,150 321.2,150 332.4,150 332.4,120 343.6,120 343.6,150 354.8,150 354.8,150 366.0,150 366.0,120 377.2,120 377.2,150 388.4,150 388.4,150 399.6,150 399.6,150 410.8,150 410.8,120 422.0,120 422.0,150 433.2,150 433.2,150 444.4,150 444.4,150 455.6,150 455.6,150 466.8,150 466.8,150 478.0,150 478.0,120 489.2,120 489.2,120 500.4,120 500.4,150 511.6,150 511.6,150 522.8,150 522.8,120 534.0,120 534.0,150 545.2,150 545.2,120 556.4,120 556.4,120 567.6,120 567.6,150 578.8,150 578.8,120 590.0,120 590.0,120 601.2,120 601.2,120 612.4,120 612.4,120 623.6,120 623.6,150 634.8,150 634.8,120 646.0,120 646.0,120 657.2,120 657.2,120 668.4,120 668.4,120 679.6,120 679.6,120 690.8,120 690.8,120 702.0,120 702.0,120 713.2,120 713.2,120 724.4,120 734.4,120" fill="none" stroke="currentColor" stroke-width="1.6"/>
  <text x="30.0" y="172" fill="currentColor" font-size="12">0</text><path d="M30.0 154 L30.0 160" stroke="currentColor"/>
  <text x="142.0" y="172" fill="currentColor" font-size="12">10</text><path d="M142.0 154 L142.0 160" stroke="currentColor"/>
  <text x="254.0" y="172" fill="currentColor" font-size="12">20</text><path d="M254.0 154 L254.0 160" stroke="currentColor"/>
  <text x="366.0" y="172" fill="currentColor" font-size="12">30</text><path d="M366.0 154 L366.0 160" stroke="currentColor"/>
  <text x="478.0" y="172" fill="currentColor" font-size="12">40</text><path d="M478.0 154 L478.0 160" stroke="currentColor"/>
  <text x="590.0" y="172" fill="currentColor" font-size="12">50</text><path d="M590.0 154 L590.0 160" stroke="currentColor"/>
  <text x="702.0" y="172" fill="currentColor" font-size="12">60</text><path d="M702.0 154 L702.0 160" stroke="currentColor"/>
  <text x="190.4" y="196" fill="currentColor">↑ 17: 스터프 1 (RTR·IDE·r0·DLC3·DLC2 = 0 다섯 개 뒤)</text>
  <text x="489.2" y="214" fill="currentColor" text-anchor="end">DATA 끝 0 + CRC 앞 0000 → 40: 스터프 1 ↑</text>
  <text x="634.8" y="232" fill="currentColor" text-anchor="end">53: ACK slot — 송신기는 1, 수신기가 0 으로 덮어씀 ↑</text>
</svg>
```

### Part A — 알려진 비트열: 두 개의 독립 구현

`verify_frame.py` 는 C 와 **다른 방법**으로 같은 비트열을 만든다. CRC 는 리스트 위의 긴 나눗셈(메시지·x^15 mod G), 스터핑은 문자열을 훑는 루프다. 실제 출력:

```text
$ python3 verify_frame.py
CRC-15/CAN check("123456789") = 0x059E
K1 std 0x123 data DLC2 11 22: unstuffed=50 stuff=2 wire=62 crc=0x04B7
  00010010001100000110000100010010001000001100101101111111111111
K2 std 0x000 data DLC8 00x8: unstuffed=98 stuff=16 wire=124 crc=0x145B
  0000010000010000011000001000001000001000001000001000001000001000001000001000001000001000001000001000010100010110111111111111
K3 std 0x555 remote DLC8: unstuffed=34 stuff=1 wire=45 crc=0x608E
  010101010101100100011000001100011101111111111
K4 ext 0x18DAF110 data DLC8: unstuffed=118 stuff=13 wire=141 crc=0x2833
  011000110110111011110001000100000100100000100001000010000010000011100000100000100000100000100000100000100000100000101010000011100111111111111
K5 ext 0x1FFFFFFF data DLC0: unstuffed=54 stuff=7 wire=71 crc=0x1C88
  01111101111101111101111101111101111101000001000011100100010001111111111
```

이 다섯 줄은 `solution.c` 의 `KNOWN[]` 표에 그대로 들어 있다. C 인코더 출력과 문자열로 비교해 모두 일치한다. K2(전부 0)는 스터프 16개로 와이어가 124비트다. K5(ID 전부 1)는 ID 구간에서 `11111[0]` 이 반복된다. 둘 다 "데이터에 따라 프레임 길이가 달라진다" 의 예다.

### Part B — 수신 상태 기계

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="m4a" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0 L8 4 L0 8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">can_rx_bit: 와이어 비트 하나씩 — 디스터핑 + 파싱 + 검사</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="90" height="44" rx="8" class="f-mut"/><rect x="130" y="34" width="300" height="110" rx="8" class="f-acc"/>
    <rect x="460" y="40" width="130" height="44" rx="8" class="f-mut"/><rect x="460" y="140" width="130" height="34" rx="8" class="f-mut"/>
    <rect x="460" y="196" width="130" height="34" rx="8" class="f-mut"/><rect x="460" y="252" width="130" height="34" rx="8" class="f-mut"/>
    <rect x="250" y="252" width="150" height="34" rx="8" class="f-mut"/><rect x="40" y="252" width="140" height="34" rx="8" class="f-ok"/>
    <rect x="640" y="40" width="110" height="34" rx="8" class="f-warn"/><rect x="640" y="96" width="110" height="34" rx="8" class="f-warn"/>
    <rect x="640" y="196" width="110" height="34" rx="8" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="55" y="60" text-anchor="middle">IDLE</text><text x="55" y="76" text-anchor="middle">1 은 무시</text><text x="280" y="54" text-anchor="middle" font-weight="bold">STUFFED (rx_destuff)</text>
    <text x="140" y="74">run == 5 → 스터프 자리:</text><text x="152" y="90">같은 값이면 STUFF, 다르면 버림</text>
    <text x="140" y="108">아니면 ub[nu] 저장, upos[nu] = 와이어 위치</text><text x="140" y="124">nu == 19 (IDE=0) / 39 (IDE=1) → need 확정</text>
    <text x="140" y="140">nu == need 이고 run != 5 → 끝</text><text x="525" y="58" text-anchor="middle">rx_end_stuffed</text><text x="525" y="74" text-anchor="middle">필드 추출 + CRC 비교</text>
    <text x="525" y="162" text-anchor="middle">CD = 1 ?</text><text x="525" y="218" text-anchor="middle">ACK 기록, AD = 1 ?</text>
    <text x="525" y="274" text-anchor="middle">EOF 1..6 = 1 ?</text><text x="325" y="268" text-anchor="middle">EOF 7번째</text><text x="325" y="282" text-anchor="middle">0 이면 overload 표시</text>
    <text x="110" y="274" text-anchor="middle">CAN_OK</text><text x="695" y="62" text-anchor="middle">CAN_ERR_STUFF</text>
    <text x="695" y="118" text-anchor="middle">CAN_ERR_CRC</text><text x="695" y="218" text-anchor="middle">CAN_ERR_FORM</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m4a)">
    <path d="M100 62 L128 62"/><path d="M430 62 L458 62"/>
    <path d="M525 84 L525 138"/><path d="M525 174 L525 194"/>
    <path d="M525 230 L525 250"/><path d="M460 269 L402 269"/>
    <path d="M250 269 L182 269"/><path d="M430 40 C520 16 600 26 638 50"/>
    <path d="M590 70 C615 80 625 100 638 110"/><path d="M590 157 C620 170 630 190 640 205"/>
    <path d="M590 213 L638 213"/><path d="M590 269 C630 260 650 245 680 232"/>
  </g>
  <g fill="currentColor">
    <text x="14" y="102">0 → SOF</text><text x="490" y="115">일치</text>
    <text x="545" y="16">6개 연속</text><text x="556" y="102">불일치</text>
    <text x="10" y="312">err_pos = 오류 난 와이어 비트(SOF = 0). CRC 불일치는 마지막 CRC 비트(또는 뒤 스터프)에서 보고 (이 모델).</text>
    <text x="10" y="326">실제 컨트롤러는 CRC error 의 error flag 를 ACK delimiter 다음 비트부터 낸다 (ISO 11898-1 확인 권장).</text>
  </g>
</svg>
```

핵심은 세 가지다.

**① 스터프 자리 판단은 "저장 전" 에 한다.** `run == 5` 인 상태에서 들어온 비트는 데이터가 아니다. 그래서 `rx_destuff` 첫머리에서 따로 처리한다. 같은 값이면 6개 연속이므로 stuff error 이고, 다르면 버리고 `last = bit, run = 1` 로 둔다. 인코더와 정확히 거울 관계다.

**② 길이는 받으면서 정해진다.** 디코더는 13번(IDE)을 받기 전까지 표준인지 모르고, DLC 를 받기 전까지 길이를 모른다. 그래서 `need` 를 처음엔 최댓값으로 두고 19번째·39번째 비트에서 확정한다. 하드웨어 CAN 컨트롤러의 BSP(bit stream processor)도 같은 일을 한다.

**③ 마지막 CRC 비트 뒤 스터프.** 와이어의 같은 값 연속이 마지막 CRC 비트에서 5개가 되면, 송신기는 스터프 비트를 하나 더 보낸다. 수신기는 이것을 CRC delimiter 로 착각하면 안 된다. 그래서 `nu == need` 여도 `run == 5` 면 DS_MORE 를 돌려주고 다음 비트를 스터프로 처리한다. solution 의 랜덤 왕복 2만 개 중 675 개(3.4 %)가 이런 프레임이고, 모두 통과한다.

### Part B — 결함 주입 결과

| 주입 | 기대 | 실측 (solution) |
|---|---|---|
| 스터프 비트 하나 뒤집기 | 앞 5개와 같아져 6연속 → 그 위치에서 STUFF | 6819 개 전부 STUFF, err_pos 일치 |
| ID·DATA·CRC 비트 변경 + 옛 CRC, 재스터핑 | CRC | 2000 개 모두 오류(길이 필드가 아니면 CRC) |
| CRC delimiter = 0 | FORM @ 52 (K1) | FORM @ 52 |
| ACK delimiter = 0 | FORM @ 54 | FORM @ 54 |
| EOF 1~6번째 = 0 | FORM @ 그 위치 | 6/6 |
| EOF 7번째 = 0 | OK + overload | OK, overload = true |
| ACK slot = 1 (아무도 ACK 안 함) | 수신기 OK, acked = false | OK (송신기의 ACK error 는 COMM-M07) |
| 마지막 비트 잘림 / 전부 idle | TRUNC | TRUNC |

DLC·IDE·RTR 비트를 바꾸면 수신기는 다른 길이를 기대한다. 그래서 CRC 가 아니라 stuff/form 오류로 잡히기도 한다. 테스트는 이 경우 "오류이기만 하면 된다" 로 둔다.

**단일 비트 전수**: 프레임 400개 × 모든 위치(38033 회)에 대해, solution 실측은 잡힘 37233, 내용이 같음 800(프레임마다 ACK slot 과 EOF 7번째), **미검출 0** 이다. 뒤에 idle 11비트를 붙이면 "길어진" 프레임은 idle 의 1 이 6개 이어져 stuff error 가 된다(TRUNC 0).

### Part B — 2비트 오류는 빠져나간다 (demo_flip.c)

CRC-15/CAN 의 해밍 거리는 6 이라고 흔히 말한다(Bosch 2.0 은 "5개 이하 랜덤 오류 검출" — 확인 권장). 하지만 이것은 **스터핑 전** 비트열 이야기다. 와이어에서 비트 하나가 바뀌면 스터프 조건이 생기거나 사라질 수 있다. 그러면 수신기가 그 뒤 비트를 한 칸 밀려 읽는다. 다른 비트 하나가 반대로 밀림을 되돌리면, 사이 구간만 1비트 밀린 "다른 메시지" 가 된다. 그 메시지의 CRC 가 우연히 맞을 확률은 대략 2^-15 이다.

```text
$ ./demo_flip          (실제 출력, 2.1 s)
undetected 2-bit example: flips @45,@83 (stuff bits sent 5, seen by rx 5)
  sent    ext 0x08D1AA3C DLC  7 data 82 E4 06 3C 9F 88 D8
  decoded ext 0x08D1AA3C DLC  7 data 87 72 03 1E 4F 80 D8  CRC 0x04C8
  sent: 00100011010011011010101000111100000101111000001101110010000010011000111100100111110100010001101100000100100110010001011111111
  recv: 00100011010011011010101000111100000101111000011101110010000010011000111100100111110000010001101100000100100110010001011111111
… (예 2개 더)
1-bit (3000 frames)  285961  det 279961 (stuff 40374 crc 215585 form 24002 trunc 0)  same 6000  UNDET 0  crc-only-would-miss 2
2-bit (1500 frames) 7440321  det 7438816 (stuff 1747528 crc 5625133 form 66155 trunc 0)  same 1500  UNDET 5  crc-only-would-miss 155
2-bit undetected rate = 6.72e-07 per error pattern
```

읽는 법은 다음과 같다.

- 단일 비트 285,961 회에서 미검출은 **0** 이다. 다만 고정 필드 검사(CRC delim·ACK delim·EOF)를 빼고 CRC 만 봤다면 2 회를 놓쳤다(crc-only-would-miss). 1비트 밀림은 프레임 끝의 고정 필드 위치를 어긋나게 해서 form error 로 잡힌다. 특히 버스의 ACK slot 이 0 이라는 점이 효과가 크다.
- 2비트 7,440,321 패턴(프레임마다 모든 위치 쌍) 중 **5 개가 미검출** 이다(≈ 6.7e-7). 첫 예에서 와이어 45 를 뒤집으면(0→1) `00001 0000` 쪽 런이 바뀌어 스터프 해석이 한 칸 달라진다. 83 에서 다시 맞춰진다. 그래서 데이터 E4 06 3C 9F 88 이 72 03 1E 4F 80, 곧 **정확히 1비트 오른쪽으로 밀린 값** 이 되었고 CRC 0x04C8 도 맞았다.
- 이것이 CAN 의 잘 알려진 약점이다(Unruh·Mathony·Kaiser 1990, Charzinski 1994 — 서지 확인 권장). ISO CAN FD 가 CRC 필드에 **스터프 카운트**와 고정 스터프 비트를 넣은 이유이기도 하다(COMM-M08, 세부 비트 수 확인 권장). 이 확률은 "프레임이 2비트 깨졌을 때의 조건부 확률" 이다. 실제 잔여 오류율은 BER 과 곱해서 본다.

### Part C — LA 처럼 읽기

`can_fields` 는 디코더가 저장한 `upos[]` 를 써서 필드별 와이어 구간을 낸다. K1, 500 kbit/s(비트당 2 µs) 실제 출력:

```text
LA decode @500 kbit/s (2 us/bit), stuff bits at wire 17 40
  [  0..  0]    0.0 us  SOF    0x0
  [  1.. 11]    2.0 us  ID     0x123
  [ 12.. 12]   24.0 us  RTR    0x0
  [ 13.. 13]   26.0 us  IDE    0x0
  [ 14.. 14]   28.0 us  r0     0x0
  [ 15.. 19]   30.0 us  DLC    0x2
  [ 20.. 35]   40.0 us  DATA   0x2
  [ 36.. 51]   72.0 us  CRC    0x4B7
  [ 52.. 52]  104.0 us  CRCdel 0x1
  [ 53.. 53]  106.0 us  ACK    0x0
  [ 54.. 54]  108.0 us  ACKdel 0x1
  [ 55.. 61]  110.0 us  EOF    0x7F
```

DLC 는 4비트인데 와이어에서는 15..19, 5비트 구간이다. 사이에 스터프 비트가 껴 있기 때문이다. sigrok/Saleae CAN 디코더가 스터프 비트를 따로 표시하는 이유가 이것이다(HWLAB-M06). 이 프레임은 62비트 + IFS 3 = 65비트이고, 500 kbit/s 에서 130 µs 다.

### Part C — 최악 스터핑: 상한 vs 실제

**상한 유도.** 스터핑 구간 g 비트에서 첫 스터프 비트는 5비트 뒤에 나올 수 있다. 그다음부터는 스터프 비트가 새 연속의 첫 비트가 되므로 **4 비트마다** 하나씩 나올 수 있다(`11111[0]0000[1]1111[0]…`). 그래서 스터프 수 ≤ 1 + ⌊(g−5)/4⌋ = ⌊(g−1)/4⌋ 이다. Tindell 의 옛 식 ⌊g/5⌋ 는 스터프 비트가 다음 연속에 들어가는 점을 빠뜨려 **과소평가** 했다. Davis 등(2007)이 이를 고쳤다(서지 확인 권장). 표준 8 바이트면 g = 98 이고 상한은 24, 프레임은 44 + 64 + 24 + 3 = **135 비트** 다.

**실제 최댓값.** 상한은 ID·DLC·고정 비트·**CRC 가 데이터로 정해진다는 점**을 무시한다. `demo_worst.c` 는 세 방법으로 실제 최댓값을 구한다. (1) 랜덤 100만, (2) `0000 1111` 주기 패턴 8위상 × 모든 ID, (3) DP 다. DP 의 상태는 (CRC 레지스터 15비트, 마지막 비트, 연속 길이 1..4) = 2^18 이다. 자유 비트(ID·데이터)마다 0/1 로 넓히며 상태별 최대 스터프 수를 유지하고, 끝에서 그 상태의 CRC 15비트까지 마저 스터핑해 센다. 이렇게 하면 모든 ID × 모든 데이터를 빠짐없이 덮는다. DP 가 찾은 최악 프레임은 역추적으로 복원한 뒤 실제 인코더로 다시 세어 확인했다. DLC 0·1 은 solution.c 의 전수 탐색(2048 × 256)과도 일치한다.

```text
$ ./demo_worst          (실제 출력, 4.9 s)
worst-case stuff bits, classic DATA frames (DLC = n)
type  n bound | random crafted    DP | DP worst frame (re-encoded stuff count)
std   0     8 |      6       6     6 | ID 0x000 data -> 6
std   1    10 |      7       7     7 | ID 0x7C0 data 7D -> 7
std   2    12 |      8       9     9 | ID 0x007 data 0F 0F -> 9
std   3    14 |      9      11    12 | ID 0x03F data FE 10 78 -> 12
std   4    16 |     10      13    14 | ID 0x03E data 10 78 03 FE -> 14
std   5    18 |     11      15    15 | ID 0x7C0 data F0 F0 F0 3E 1E -> 15
std   6    20 |     12      17    18 | ID 0x7FE data 0F 0F 0F 0F F8 78 -> 18
std   7    22 |     13      20    21 | ID 0x01F data 87 87 C1 E1 E1 E1 E1 -> 21
std   8    24 |     13      20    22 | ID 0x078 data 01 E1 E1 E1 E1 E1 E0 0F -> 22
ext   0    13 |      9       8    11 | ID 0x07C30FFC data -> 11
ext   1    15 |     10      10    13 | ID 0x1F0C3FF0 data F0 -> 13
ext   2    17 |     10      12    15 | ID 0x1F0C3DF0 data 08 3C -> 15
ext   3    19 |     13      14    17 | ID 0x01EE1E1E data E1 E1 FF -> 17
ext   4    21 |     12      15    19 | ID 0x1F0C1F0F data 00 F0 F0 F0 -> 19
ext   5    23 |     12      18    21 | ID 0x17C30F0F data F0 F0 F0 FF 87 -> 21
ext   6    25 |     14      19    23 | ID 0x000C3FE0 data 0F 0F 0F F8 78 78 -> 23
ext   7    27 |     14      23    25 | ID 0x1F0C3C3E data 87 87 87 87 87 80 00 -> 25
ext   8    29 |     15      24    27 | ID 0x000C3C3C data 3C 3C 3F FF 0F 0F 0F 0F -> 27
std 8 data bytes, DLC 8..15 free: DP max 22 (at DLC 8), bound 24
ext 8 data bytes, DLC 8..15 free: DP max 28 (at DLC 15), bound 29
```

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">표준 데이터 프레임 최대 스터프 비트 수 (demo_worst 실측) — 상한 vs DP 정확값 vs 랜덤 100만</text>
  <path d="M50 250 L740 250 M50 250 L50 40" stroke="currentColor" fill="none"/>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="44" y="254">0</text><text x="44" y="187">8</text><text x="44" y="121">16</text><text x="44" y="54">24</text>
  </g>
  <path d="M50 183.3 L740 183.3 M50 116.7 L740 116.7 M50 50 L740 50" stroke="currentColor" stroke-dasharray="2 4" opacity="0.4" fill="none"/>
  <g stroke="currentColor">
    <rect x="62" y="183.3" width="18" height="66.7" class="f-mut"/><rect x="80" y="200" width="18" height="50" class="f-acc"/><rect x="98" y="200" width="18" height="50" class="f-warn"/>
    <rect x="138" y="166.7" width="18" height="83.3" class="f-mut"/><rect x="156" y="191.7" width="18" height="58.3" class="f-acc"/><rect x="174" y="191.7" width="18" height="58.3" class="f-warn"/>
    <rect x="214" y="150" width="18" height="100" class="f-mut"/><rect x="232" y="175" width="18" height="75" class="f-acc"/><rect x="250" y="183.3" width="18" height="66.7" class="f-warn"/>
    <rect x="290" y="133.3" width="18" height="116.7" class="f-mut"/><rect x="308" y="150" width="18" height="100" class="f-acc"/><rect x="326" y="175" width="18" height="75" class="f-warn"/>
    <rect x="366" y="116.7" width="18" height="133.3" class="f-mut"/><rect x="384" y="133.3" width="18" height="116.7" class="f-acc"/><rect x="402" y="166.7" width="18" height="83.3" class="f-warn"/>
    <rect x="442" y="100" width="18" height="150" class="f-mut"/><rect x="460" y="125" width="18" height="125" class="f-acc"/><rect x="478" y="158.3" width="18" height="91.7" class="f-warn"/>
    <rect x="518" y="83.3" width="18" height="166.7" class="f-mut"/><rect x="536" y="100" width="18" height="150" class="f-acc"/><rect x="554" y="150" width="18" height="100" class="f-warn"/>
    <rect x="594" y="66.7" width="18" height="183.3" class="f-mut"/><rect x="612" y="75" width="18" height="175" class="f-acc"/><rect x="630" y="141.7" width="18" height="108.3" class="f-warn"/>
    <rect x="670" y="50" width="18" height="200" class="f-mut"/><rect x="688" y="66.7" width="18" height="183.3" class="f-acc"/><rect x="706" y="141.7" width="18" height="108.3" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="89" y="268">n=0</text><text x="165" y="268">1</text><text x="241" y="268">2</text><text x="317" y="268">3</text><text x="393" y="268">4</text>
    <text x="469" y="268">5</text><text x="545" y="268">6</text><text x="621" y="268">7</text><text x="697" y="268">8</text>
    <text x="71" y="179">8</text><text x="89" y="196">6</text><text x="679" y="46">24</text><text x="697" y="62">22</text><text x="715" y="137">13</text>
  </g>
  <g stroke="currentColor"><rect x="60" y="280" width="14" height="12" class="f-mut"/><rect x="250" y="280" width="14" height="12" class="f-acc"/><rect x="470" y="280" width="14" height="12" class="f-warn"/></g>
  <g fill="currentColor"><text x="80" y="291">상한 ⌊(34+8n−1)/4⌋</text><text x="270" y="291">DP 정확 최댓값</text><text x="490" y="291">랜덤 100만 개 중 최댓값</text></g>
</svg>
```

정리하면 다음과 같다.

- **상한은 안전하지만 2비트 정도 비관적이다.** 표준 8 B 의 실제 최댓값은 22, 상한은 24 다. 그래서 실제 최악 프레임은 133 비트(IFS 포함), 공식은 135 비트다. 응답 시간 분석(COMM-M05·M08)에는 135 를 쓰면 안전하다. 2비트 차이는 1.5 % 다.
- **랜덤 탐색은 최악을 못 찾는다.** 100만 개 랜덤의 최댓값은 13 이다. 무작위 데이터로 "최악 길이" 를 재면 **크게** 과소평가한다. 손으로 만든 패턴(0x0F/0x78/0x3C…)이 20 까지 가고, 마지막 두 비트는 CRC 를 맞춰야 해서 DP 같은 체계적 탐색이 필요하다.
- **스터핑 없는 길이는 111 비트(표준 8 B)** 다. 그래서 같은 페이로드라도 프레임 시간은 222 ~ 266 µs(500 kbit/s)로 20 % 가까이 흔들린다. 지터 예산에 들어가는 숫자다.
- DLC 9..15 를 허용하면 확장 프레임에서 최댓값이 27 → 28 로 하나 늘어난다(DP 실측, DLC 15 에서). 그래도 상한 29 이내다. 표준은 22 그대로다.

### 복잡도와 메모리

| 함수 | 시간 | 메모리 |
|---|---|---|
| 인코더 | O(프레임 비트), 최대 157 | 호출자 버퍼 + 스택 118 B |
| 수신기 `can_rx_bit` | 비트당 O(1), 끝에서 CRC O(118) | `can_rx_t` 약 400 B (ub 118 + upos 236 + 스터프 로그) |
| `max_stuff_exhaustive(1)` | 2048 × 256 인코드 ≈ 50 ms | 스택 |
| demo DP | 2^18 상태 × 2 × ~100 비트 | 상태표 1 MB + 역추적 ≈ 27 MB (demo 전용 malloc) |

펌웨어라면 CRC 를 끝에서 다시 계산하지 말고 비트마다 갱신한다. 실제 하드웨어 BSP 가 그렇게 한다. 여기서는 검증하기 쉽도록 ub[] 에 모아 두었다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 39/39 passed, exit 0, 약 0.1 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
python3 verify_frame.py                                                          # 독립 구현 비트열
cc -std=c11 -Wall -Wextra -O2 demo_worst.c -o demo_worst && ./demo_worst          # 약 5 s
cc -std=c11 -Wall -Wextra -O2 demo_flip.c -o demo_flip && ./demo_flip             # 약 2 s (solution.c 를 include)
```

실제 출력(`solution`, LA 표는 위와 같아 생략):

```text
CRC-15/CAN("123456789") = 0x059E
K1 std 0x123 DLC2 11 22 (62 bits, 2 stuff): 00010010001100000110000100010010001000001100101101111111111111
random round trip: 20000 frames, bad=0
stuff-bit flips: 6819 tested, wrong=0
single-bit flips: 38033, detected=37233 (stuff 5438, crc 28595, form 3200, trunc 0), same-content=800, undetected=0
exhaustive max stuff (std data): DLC0 6 (bound 8, 1 frames, e.g. ID 0x000) | DLC1 7 (bound 10, 32 frames, e.g. ID 0x010 data EE)
39/39 passed
```

DLC 0 의 최댓값 6 을 내는 프레임은 ID 0x000 **하나뿐** 이다. 스터핑 전 34비트가 모두 0 이다(CRC(0…0) = 0). DLC 1 은 32 개 프레임이 7 을 낸다.

## 7. 자주 하는 실수

**1. CRC 를 스터핑된 비트로 계산한다.** CRC 는 스터핑 **전** SOF..DATA 로 계산한다. 스터핑은 그다음, CRC 까지 포함해서 한다. 순서를 바꾸면 스터프가 하나라도 있는 프레임은 모두 CRC error 가 난다(연습 5: 1785/1785 프레임에서 값이 다름).

```c
k = build_fields(...);              /* SOF..DATA */
put_bits(bits, &k, crc15_bits(bits, k), 15);   /* CRC 를 붙인 뒤 */
w = can_stuff(bits, k, out, cap, &ns);          /* 스터핑 */
```

**2. 스터프 비트를 다음 연속에 안 센다.** `run = 0` 으로 리셋하면 `0000011111` 에서 두 번째 스터프를 빠뜨린다. 정답은 `last = 스터프값, run = 1` 이다. 수신기도 같은 규칙이어야 한다. 아니면 정상 프레임에서 stuff error 가 나거나 스터프 비트를 데이터로 읽는다.

**3. 마지막 CRC 비트 뒤 스터프를 빠뜨린다.** 인코더가 "CRC 끝나면 스터핑 끝" 으로 루프를 끊는 경우다. 수신기는 다음 비트를 스터프로 기대하는데, CRC delimiter(1)가 오면 그것이 스터프로 소비된다. 그 결과 form error 가 나거나 길이가 1 어긋난다. 마지막 CRC 비트 뒤에 스터프가 붙는 프레임은 드물지 않다(verify_frame.py 함수로 랜덤 2만 개를 세어 보면 약 3.4 %).

**4. 고정 형식 필드까지 스터핑한다.** EOF 7 개의 1 을 스터핑하면 `11111 0 11` 이 되어 EOF 가 아니다. CRC delimiter 이후는 고정 형식이고, EOF 의 6개 연속 1 은 일부러 스터핑 규칙을 깨서 "프레임 끝" 을 표시한다. 같은 원리로 error flag(dominant 6개)도 일부러 규칙을 깨서 모든 노드에 오류를 알린다(COMM-M07).

**5. 확장 ID 를 29비트 그대로 한 번에 쓴다.** ID-A(상위 11) → SRR → IDE → ID-B(하위 18) 순서다. `put_bits(id, 29)` 로 쓰면 SRR·IDE 자리에 ID 비트가 들어가 다른 프레임이 된다. 반대로 디코더도 `id = A << 18 | B` 로 합친다.

**6. 리모트 프레임에 데이터를 붙인다.** RTR = 1 이면 DLC 가 8 이어도 데이터 필드가 없다(DLC 는 "요청하는 길이"). `data_len = rtr ? 0 : min(dlc, 8)` 를 인코더와 디코더가 같이 써야 한다.

**7. 최악 프레임 길이를 랜덤 데이터로 잰다.** 랜덤 100만 개의 최대 스터프는 13, 실제 최악은 22(표준 8 B)다. 버스 부하·응답 시간 예산은 상한 공식(135 비트)이나 정확한 최댓값(133)으로 잡는다.

## 8. 면접 Q&A

<details><summary>Q1. 비트 스터핑은 왜 하나요? 왜 하필 5비트인가요?</summary>

CAN 에는 별도 클럭 선이 없다. 수신기는 SOF 의 하강 에지로 하드 동기(hard sync)를 하고, 그 뒤로는 recessive→dominant 에지마다 재동기(resync)를 해서 비트 타이밍 오차를 SJW 만큼 보정한다(COMM-M06). 같은 값이 길게 이어지면 에지가 없어 오차가 쌓인다. 5비트마다 반대 비트를 넣으면 최악에도 10비트 안에 같은 방향 에지가 하나 생긴다. 5 라는 숫자는 오버헤드(최악 약 25 %)와 동기 보장 사이의 설계 선택이다. 부수 효과로 "6개 연속" 이 정상 데이터에 절대 나오지 않는다. 그래서 error flag(6 dominant)와 EOF(7 recessive)가 신호 역할을 할 수 있다.

</details>

<details><summary>Q2. 스터핑은 정확히 어디서 어디까지인가요? CRC 범위는요?</summary>

스터핑은 SOF 부터 CRC sequence 끝까지다. 마지막 CRC 비트 뒤에도 조건이 맞으면 들어간다. CRC delimiter, ACK slot, ACK delimiter, EOF, IFS 는 고정 형식이다. CRC 는 스터핑 **전** 비트로, SOF..DATA 끝을 덮는다. init 이 0 이라 SOF(0)를 넣든 빼든 같은 값이 나온다. 송신 순서는 "필드 → CRC 계산 → 붙이기 → 전체 스터핑" 이고, 수신은 거꾸로 "디스터핑 → CRC 비교" 다.

</details>

<details><summary>Q3. CRC-15 를 검증된 값으로 어떻게 확인하나요?</summary>

세 가지로 한다. (1) RevEng 카탈로그의 CRC-15/CAN check 값 0x059E 를 "123456789" 에 대해 재현한다. (2) 프레임 비트 + 자기 CRC 를 다시 넣으면 나머지가 0 이다(residue 0). 이 레슨은 랜덤 3000 프레임으로 확인했다. (3) 다른 방법으로 짠 구현(Python 긴 나눗셈)과 비트열을 문자열로 대조한다. 기억에 있는 "어디서 본 프레임 비트열" 은 증거로 쓰지 않는다.

</details>

<details><summary>Q4. 표준 프레임과 확장 프레임이 같은 base ID 로 동시에 송신하면 누가 이기나요?</summary>

12번 비트에서 갈린다. 표준 데이터 프레임은 RTR = 0(dominant), 확장은 SRR = 1(recessive)이다. 표준이 dominant 를 냈으므로 이기고, 확장 노드는 중재에서 지고 수신기로 바뀐다. 표준이 **리모트** 프레임이면 12번이 둘 다 1 이다. 그러면 13번 IDE(표준 0, 확장 1)에서 표준이 이긴다. 연습 2 가 이를 비트로 확인하고, 시뮬레이션은 COMM-M05 에서 한다. 스터핑은 중재를 방해하지 않는다. 지금까지 보낸 비트가 같으면 스터프 비트도 같은 자리에 같은 값으로 들어가기 때문이다.

</details>

<details><summary>Q5. 8 바이트 표준 프레임은 최악 몇 비트인가요? 500 kbit/s 에서 몇 µs?</summary>

공식(Davis 등)으로는 8·8 + 47 + ⌊97/4⌋ = 135 비트(IFS 포함), 500 kbit/s 에서 270 µs 다. 초당 최대 약 3704 프레임이다. 스터핑이 없으면 111 비트, 222 µs 다. 공식은 상한이다. DP 로 모든 ID·데이터를 탐색하면 실제 최댓값은 스터프 22개, 133 비트다. 응답 시간 분석에는 135 를 쓰는 것이 보수적이고 표준이다. 랜덤 데이터로 재면 스터프 13개 정도밖에 안 나와 과소평가한다는 점도 덧붙인다.

</details>

<details><summary>Q6. 수신기가 잡는 오류 종류와, 각각 어디서 잡히는지 말해 보세요.</summary>

수신 측 오류는 셋이다. **Stuff error**: 스터핑 구간에서 같은 값 6개, 그 6번째 비트에서 잡힌다. **CRC error**: 계산값 ≠ 받은 CRC 다. 실제로는 ACK delimiter 다음부터 error flag 를 낸다(확인 권장). **Form error**: 고정 형식 비트(CRC delim, ACK delim, EOF)가 dominant 일 때다. 단, 수신기에게 EOF 마지막 비트의 dominant 는 오류가 아니라 overload 조건이다. 송신 측 오류는 둘이다. **Bit error**(보낸 값 ≠ 읽은 값, 중재 구간·ACK slot 제외)와 **ACK error**(ACK slot 이 recessive)이고, COMM-M05/M07 몫이다. 이 레슨의 결함 주입 표가 각 경우를 위치까지 확인한다.

</details>

<details><summary>Q7. CRC 의 해밍 거리가 6 이면 2비트 오류는 항상 잡히지 않나요?</summary>

스터핑 전 비트열에 대해서는 그렇다. 하지만 와이어 오류가 스터프 비트를 만들거나 없애면 수신기의 비트 정렬이 밀린다. 그러면 CRC 가 보는 오류 패턴은 2비트가 아니라 "구간 전체가 1비트 밀린" 긴 패턴이 된다. demo_flip 에서 2비트 오류 7,440,321 패턴 중 5 개(6.7e-7)가 미검출이었다. 예는 데이터 E4 06 3C 9F 88 → 72 03 1E 4F 80(1비트 밀림)이다. 단일 비트는 28만 회 중 0 개였는데, 밀림이 프레임 끝의 고정 필드(특히 ACK slot = 0 다음의 ACK delimiter)와 어긋나 form error 로 잡혔기 때문이다. CAN FD(ISO)는 이를 막으려고 스터프 카운트를 CRC 필드에 넣었다(COMM-M08).

</details>

<details><summary>Q8. LA 로 찍은 CAN 파형에서 ID 를 손으로 읽는 법은?</summary>

먼저 비트 폭을 맞춘다(500 kbit/s → 2 µs). 다음으로 SOF(idle 뒤 첫 하강 에지)부터 비트를 적는다. 같은 값 5개 다음 비트는 지운다(스터프). 지운 뒤 1..11 번이 ID 다. 예: K1 은 와이어 17 의 스터프 때문에 DLC 가 와이어 15..19 에 걸친다. 디코더 화면에서 필드 폭이 이상해 보이면 대개 스터프 비트다. 그다음 CRC 를 확인한다. 디코더가 CRC 오류를 표시하면 샘플링 포인트·비트레이트가 맞는지(COMM-M06), 종단 저항 120 Ω × 2 가 있는지를 먼저 본다.

</details>

## 9. 추가 연습

### 연습 1 — 전부 0 인 프레임의 스터프 위치

표준 ID 0x000, DLC 0 데이터 프레임의 스터핑 전 비트열은 무엇인가? 스터프 비트의 와이어 위치를 손으로 구하고 코드로 확인하라.

<details><summary>답 보기</summary>

SOF 0, ID 0×11, RTR·IDE·r0 0, DLC 0000 으로 19개가 모두 0 이다. CRC(0…0) = 0(init 0)이라 CRC 15비트도 0 이다. 그래서 34비트가 전부 0 이다. 첫 스터프는 0 다섯 개 뒤 와이어 5 다. 이후 "스터프 1 + 0 다섯 개" 가 6비트 주기로 반복되어 **5, 11, 17, 23, 29, 35** 의 6개다(29, 35 는 CRC 안). 와이어 50비트. 이 프레임이 DLC 0 표준 프레임 중 스터프 최댓값(6)을 내는 **유일한** 프레임이다(solution 전수, demo DP 일치).

</details>

### 연습 2 — 표준 vs 확장, 처음 갈라지는 비트

base ID 0x123 인 표준 데이터 프레임, 표준 리모트 프레임, 확장 프레임(ID = 0x123 << 18 | 1)의 언스터프 비트열을 비교해, 처음 달라지는 비트 번호와 각자의 값을 구하라. 와이어(스터핑 후) 비트도 그 지점까지 같은지 확인하라.

<details><summary>답 보기</summary>

표준 데이터 vs 확장은 **12번**(RTR 0 vs SRR 1)에서 갈려 표준이 이긴다. 표준 리모트 vs 확장은 12번이 1 = 1 로 같다. **13번**(IDE 0 vs 1)에서 갈려 표준 리모트가 이긴다. 앞 12비트가 같으므로 스터핑 결과도 같다. 모든 송신 노드가 같은 스터프 비트를 같은 자리에 낸다는 뜻이고, 그래서 스터핑이 중재를 방해하지 않는다(COMM-M05).

</details>

### 연습 3 — 버스 시간 예산

500 kbit/s 와 1 Mbit/s 에서 표준 8 B 프레임의 (a) 상한 길이, (b) DP 정확 최악, (c) 스터핑 없음의 전송 시간과, 상한 기준 초당 최대 프레임 수를 구하라.

<details><summary>답 보기</summary>

(a) 135 비트 → 270 µs @500k, 3703.7 프레임/s. 1 Mbit/s 에서는 7407.4 프레임/s 다. (b) 44 + 64 + 22 + 3 = 133 비트 → 266 µs. (c) 111 비트 → 222 µs. 확장 8 B 상한은 160 비트다. 같은 페이로드에서도 프레임 시간이 20 % 흔들린다. 1 kHz 제어 루프에 노드 몇 개를 올릴 수 있는지는 상한으로 계산한다(COMM-M08 에서 CAN FD 와 비교).

</details>

### 연습 4 — 2비트 차이, 둘 다 유효한 프레임

demo_flip 이 찾은 두 와이어 비트열(와이어 45, 83 만 다름)을 practice.c 의 **다른 구현**(배열을 한 번에 훑는 디코더)으로 각각 디코드하라. 둘 다 유효한지, 데이터가 어떻게 다른지 확인하라.

<details><summary>답 보기</summary>

해밍 거리는 2 이고, 두 비트열 모두 디코드 OK 다(ID 0x08D1AA3C, DLC 7). 데이터는 82 E4 06 3C 9F 88 D8 → 87 72 03 1E 4F 80 D8 이다. E4 → 72, 06 → 03, 3C → 1E 처럼 사이 구간이 정확히 1비트 밀렸다. 첫 뒤집기는 스터프 해석을 한 칸 바꿔 정렬을 밀고, 두 번째 뒤집기가 되돌린다. 밀린 메시지의 CRC 가 우연히(≈2^-15) 맞았다. solution 의 상태 기계(demo_flip)와 practice 의 단순 디코더가 같은 결론을 낸다.

</details>

### 연습 5 — CRC 범위 실험

랜덤 표준 프레임 2000 개에 대해 (a) CRC 를 SOF 포함/제외로 계산한 값이 같은지, (b) 스터핑된 비트열로 계산한 CRC 가 언스터프 CRC 와 다른지 세어라.

<details><summary>답 보기</summary>

(a) 2000/2000 같다. init = 0 인 시프트 레지스터에 0 을 넣으면 레지스터가 그대로 0 이기 때문이다. (b) 스터프가 있는 1785 개 모두에서 다르다. 우연히 같을 확률은 프레임당 약 2^-15 라서 "항상" 은 아니지만, 이 시드에서는 전부 다르다. 순서를 틀리면 사실상 모든 프레임이 CRC error 가 된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 15/15 passed
```

실제 출력:

```text
ex1 ID 0x000 DLC0: 50 wire bits, 6 stuff at 5 11 17 23 29 35
ex2 std data vs ext: first diff bit 12 (std 0 / ext 1); std remote vs ext: bit 13 (std 0 / ext 1)
ex3 std 8B: bound 135 bits = 270 us @500k (3703.7 frames/s), exact 133 bits = 266 us, no-stuff 111 bits = 222 us; @1M 7407.4 frames/s
ex4 hamming 2 (@45,@83) sent=0 recv=0, data[0..2] 82 E4 06 -> 87 72 03
ex5 SOF in/out same CRC: 2000/2000; CRC over stuffed bits differs: 1785/1785 frames with stuff
15/15 passed
```

## 10. 핵심 정리

- 레벨: dominant 0, recessive 1, wired-AND. 표준 SOF·ID11·RTR·IDE·r0·DLC4·DATA·CRC15·CD·ACK·AD·EOF7 (+IFS3). 확장은 RTR 자리에 SRR=1, IDE=1 뒤 ID-B 18 비트.
- CRC-15/CAN: poly 0x4599, init 0, check 0x059E, 나머지 0. 범위는 **스터핑 전** SOF..DATA 다(SOF 는 넣어도 같은 값).
- 스터핑: SOF..CRC 끝, 같은 값 5개 → 반대 비트. 스터프 비트는 다음 연속의 첫 비트이고, 마지막 CRC 비트 뒤에도 들어간다. 고정 필드는 스터핑하지 않는다.
- 수신기: 디스터핑하면서 IDE·DLC 로 길이를 정한다. 오류 셋은 stuff(6연속), CRC, form(CD·AD·EOF)이다. EOF 마지막 dominant 는 overload 다.
- 단일 비트 오류: 28만 회 0 미검출(고정 필드 검사가 정렬 밀림을 잡는다). 2비트: 스터핑 때문에 6.7e-7 미검출이다. 그래서 CAN FD 에 스터프 카운트가 들어갔다.
- 최악 길이: 상한 8n+47+⌊(33+8n)/4⌋(표준 8 B 135 비트), 실제 최댓값은 DP 로 133. 랜덤 탐색은 크게 과소평가한다.
- 검증은 기억이 아니라 코드 두 벌(C 시프트 레지스터 + Python 긴 나눗셈), check 값, 왕복 수만 개로 한다.

## 참고자료

- ISO 11898-1:2015 Road vehicles — CAN — Part 1: Data link layer and physical signalling (프레임 형식, 스터핑, 오류 종류; 이 레슨의 corner rule 은 "확인 권장" 으로 표시).
- Robert Bosch GmbH, CAN Specification Version 2.0 (1991) — Part A/B 프레임 형식, CRC_RG 알고리즘, 오류 검출, "receivers accept r0/r1 in all combinations".
- R. I. Davis, A. Burns, R. J. Bril, J. J. Lukkien, "Controller Area Network (CAN) schedulability analysis: Refuted, revisited and revised", Real-Time Systems 35(3), 2007 — 최악 프레임 길이 ⌊(g−1)/4⌋ 와 Tindell 식 정정.
- K. Tindell, A. Burns, A. Wellings, "Calculating Controller Area Network (CAN) message response times", Control Engineering Practice, 1995.
- J. Unruh, H.-J. Mathony, K.-H. Kaiser, "Error detection analysis of automotive communication protocols", SAE 900699, 1990 / J. Charzinski, "Performance of the error detection mechanisms in CAN", 1st iCC, 1994 — 스터핑에 의한 미검출 2비트 오류 (서지 확인 권장).
- Greg Cook, CRC RevEng catalogue — CRC-15/CAN: width 15, poly 0x4599, init 0, check 0x059E, residue 0: <https://reveng.sourceforge.io/crc-catalogue/all.htm>
- Linux SocketCAN 문서 (실제 can0 에서 프레임 다루기 = COMM-M09): <https://docs.kernel.org/networking/can.html>
- 관련 레슨: COMM-B08 (CRC 검출 능력), ALGO-M01 (비트 단위 CRC), COMM-B10 (프레임 파서), HWLAB-M05 · HWLAB-M06 (LA 타이밍·디코더), COMM-M05 (중재), COMM-M06 (비트 타이밍), COMM-M07 (오류 카운터·bus-off), COMM-M08 (CAN FD), COMM-M09 (SocketCAN)

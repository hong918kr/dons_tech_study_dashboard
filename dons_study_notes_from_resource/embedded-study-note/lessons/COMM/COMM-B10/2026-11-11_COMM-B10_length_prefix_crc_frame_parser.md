---
id: COMM-B10
time: 60분
updated: 2026-11-11
---
# COMM-B10 길이 prefix + 체크섬 프레임 인코더/파서

## 학습 목표

- `[SOF][LEN][TYPE][PAYLOAD][CRC16]` 프레임의 모든 필드를 **숫자로** 정의한다: SOF 값, LEN 이 세는 범위, 최대 LEN, CRC 알고리즘(CRC-16/CCITT-FALSE)과 덮는 바이트, CRC 바이트 순서.
- 출력 용량(capacity)을 검사하는 인코더와, 바이트 하나씩 받는 **파서 상태 머신**(state machine)을 정적 버퍼만으로 짠다 — UART RX 인터럽트에서 부를 수 있는 모양.
- 잘린 프레임, CRC 오류, LEN 과대값, 스트림 중간 시작, 데이터 안의 SOF 값 — 다섯 가지 오류를 직접 주입하고, 단순 파서가 **다음 정상 프레임까지 잃는** 이유를 바이트 단위로 설명한다.
- 오류가 나면 "가짜 SOF 바로 다음 바이트부터 다시 훑는" 재동기화(resync)를 68바이트 고정 버퍼로 구현하고, 랜덤 쓰레기 + 프레임 스트림에서 정상 프레임이 전부 복구됨을 테스트로 보인다.
- 길이+CRC16 프레이밍의 한계(거짓 수락 ≈ 2^-16 / 가짜 SOF, 최악 바이트당 비용)를 실측 숫자로 말한다.

## 1. 문제

센서 허브·RF 모듈·부트로더와 MCU 가 UART 로 이야기할 때, 면접관이 "패킷 포맷 하나 정하고 파서 짜 보세요 → 중간에 바이트가 빠지면요?" 를 묻는 이유: **UART 는 바이트만 주고 패킷 경계는 주지 않는다.** 경계를 되찾는 방법(framing)이 펌웨어 통신 코드의 절반이다.

UART 하드웨어(STM32 USART, nRF UARTE)는 바이트 하나가 들어올 때마다 RXNE 플래그(또는 인터럽트)를 세운다 (COMM-B01, COMM-B02). 그 위에서 "어디서 패킷이 시작하고 끝나는가"를 정하는 방법은 크게 셋이다.

| 방법 | 경계 표시 | 레슨 |
|---|---|---|
| **길이 prefix (이 레슨)** | 시작 표시(SOF) + 길이 필드. 끝은 길이로 계산 | COMM-B10, COMM-M10 |
| 바이트 스터핑 | 구분 바이트를 데이터에서 못 쓰게 이스케이프 | COMM-B11 (SLIP), COMM-B12 (COBS) |
| 침묵 간격 | 일정 시간 이상 조용하면 프레임 끝 | COMM-B13 (Modbus RTU) |

길이 prefix 방식은 MAVLink(드론), u-blox UBX(GNSS), 여러 BLE/LoRa 모듈의 바이너리 명령 모드가 쓰는 모양이다. Don 이 RF 모듈을 붙이며 본 AT 명령(텍스트, COMM-B14)이 끝나고 바이너리 모드로 넘어가면 대개 이 형태다. 장점은 데이터를 손대지 않는다는 것(스터핑 없음, 오버헤드 고정 5바이트), 약점은 **동기를 잃었을 때** — 이 레슨의 Part B·C 가 바로 그 이야기다.

이 레슨의 프레임 형식 (전부 이 레슨이 정한 값이다):

| 필드 | 크기 | 값 / 의미 |
|---|---|---|
| SOF (start of frame) | 1 | 항상 `0xA5` |
| LEN | 1 | **PAYLOAD 바이트 수만** (TYPE, CRC 제외). 0..64 (`FRAME_MAX_PAYLOAD`). 65 이상이면 오류 |
| TYPE | 1 | 메시지 종류 (0..255, 해석은 상위 계층 — 연습 3) |
| PAYLOAD | LEN | 아무 바이트나 (0xA5 포함) |
| CRC16 | 2 | CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, 반사 없음(refin/refout = false), xorout 0x0000. **덮는 범위 = LEN, TYPE, PAYLOAD** (SOF 제외). 와이어에는 **상위 바이트 먼저**(big-endian) |

전체 길이 = LEN + 5, 최대 69바이트. 115200 baud 8N1 이면 바이트당 10비트 → 86.8 µs, 최대 프레임 69 × 86.8 µs ≈ 5.99 ms.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">UART 8N1 위의 SOF 0xA5 (= 1010 0101b): LSB 먼저 → 1 0 1 0 0 1 0 1 (COMM-B01)</text>
  <path d="M80,60 H130 V100 H180 V60 H230 V100 H280 V60 H330 V100 H430 V60 H480 V100 H530 V60 H680" fill="none" stroke="currentColor" stroke-width="2"/>
  <g stroke="currentColor" stroke-dasharray="2,3" opacity="0.5">
    <line x1="130" y1="40" x2="130" y2="108"/><line x1="180" y1="40" x2="180" y2="108"/><line x1="230" y1="40" x2="230" y2="108"/>
    <line x1="280" y1="40" x2="280" y2="108"/><line x1="330" y1="40" x2="330" y2="108"/><line x1="380" y1="40" x2="380" y2="108"/>
    <line x1="430" y1="40" x2="430" y2="108"/><line x1="480" y1="40" x2="480" y2="108"/><line x1="530" y1="40" x2="530" y2="108"/>
    <line x1="580" y1="40" x2="580" y2="108"/><line x1="630" y1="40" x2="630" y2="108"/>
  </g>
  <text x="40" y="64" fill="currentColor" font-size="12">1</text><text x="40" y="104" fill="currentColor" font-size="12">0</text>
  <g fill="currentColor" font-size="12">
    <text x="88" y="124">idle</text><text x="140" y="124">start</text>
    <text x="195" y="124">d0=1</text><text x="245" y="124">d1=0</text><text x="295" y="124">d2=1</text><text x="345" y="124">d3=0</text>
    <text x="395" y="124">d4=0</text><text x="445" y="124">d5=1</text><text x="495" y="124">d6=0</text><text x="545" y="124">d7=1</text>
    <text x="590" y="124">stop</text><text x="640" y="124">idle</text>
  </g>
  <text x="10" y="160" fill="currentColor" font-weight="bold">그 위의 프레임: TYPE 0x01, PAYLOAD "Hi" (0x48 0x69) → 7바이트</text>
  <line x1="315" y1="182" x2="480" y2="182" stroke="currentColor"/>
  <line x1="315" y1="182" x2="315" y2="190" stroke="currentColor"/><line x1="480" y1="182" x2="480" y2="190" stroke="currentColor"/>
  <text x="318" y="176" fill="currentColor" font-size="12">LEN = 2 = PAYLOAD 바이트 수만</text>
  <g stroke="currentColor">
    <rect x="60" y="194" width="80" height="44" class="f-acc"/>
    <rect x="145" y="194" width="80" height="44" class="f-mut"/>
    <rect x="230" y="194" width="80" height="44" class="f-mut"/>
    <rect x="315" y="194" width="80" height="44" class="f-ok"/>
    <rect x="400" y="194" width="80" height="44" class="f-ok"/>
    <rect x="485" y="194" width="80" height="44" class="f-warn"/>
    <rect x="570" y="194" width="80" height="44" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="68" y="209">SOF [0]</text><text x="153" y="209">LEN [1]</text><text x="238" y="209">TYPE [2]</text>
    <text x="323" y="209">PL[0] [3]</text><text x="408" y="209">PL[1] [4]</text><text x="493" y="209">CRC_HI [5]</text><text x="578" y="209">CRC_LO [6]</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="14">
    <text x="88" y="230">A5</text><text x="173" y="230">02</text><text x="258" y="230">01</text>
    <text x="343" y="230">48</text><text x="428" y="230">69</text><text x="513" y="230">27</text><text x="598" y="230">72</text>
  </g>
  <line x1="145" y1="252" x2="480" y2="252" stroke="currentColor"/>
  <line x1="145" y1="244" x2="145" y2="252" stroke="currentColor"/><line x1="480" y1="244" x2="480" y2="252" stroke="currentColor"/>
  <text x="150" y="272" fill="currentColor" font-size="12">CRC-16/CCITT-FALSE 범위 = LEN·TYPE·PAYLOAD → 0x2772 (SOF 는 제외)</text>
  <text x="490" y="258" fill="currentColor" font-size="12">↑ 0x27 먼저 (big-endian)</text>
  <text x="10" y="318" fill="currentColor" font-size="12">각 바이트가 위 파형처럼 start/stop 비트에 싸여 10비트씩 나간다 → 7바이트 = 70비트 = 115200 baud 에서 약 608 µs</text>
</svg>
```

(위 바이트 `A5 02 01 48 69 27 72` 는 `solution.c` 의 인코더 출력이며, 테스트에 그대로 들어 있다. 파이썬으로 따로 짠 비트 단위 CRC 로도 0x2772 를 확인했다.)

### Part A — CRC, 인코더, 바이트 단위 파서

1. `crc16_update(crc, b)`: CRC-16/CCITT-FALSE 한 바이트 갱신(MSB 먼저, 비트 단위). `"123456789"` → **0x29B1** (RevEng 카탈로그의 check 값, 코드로 재확인).
2. `frame_encode(type, payload, len, out, cap)`: LEN > 64 → `FE_ERR_LEN`, `cap < len + 5` → `FE_ERR_CAP` (둘 다 `out` 을 건드리지 않음), 아니면 쓴 바이트 수.
3. `fp_step(p, b)`: 상태 `ST_SOF → ST_LEN → ST_TYPE → ST_PAYLOAD → ST_CRC_HI → ST_CRC_LO`. SOF 뒤 바이트는 `raw[]`(68바이트)에 그대로 기록, CRC 는 바이트마다 누적. 완성되면 `EV_FRAME` 과 함께 `raw + 2` 에 PAYLOAD.
4. 테스트: "Hi" 프레임 바이트 정확히, LEN 0 프레임, 용량 경계(69/68), LEN 0..64 × TYPE 4종 = 260 프레임 연속 스트림 왕복.

### Part B — 오류 주입

`fp_step` 의 오류 경로: LEN > 64 → `len_err++`, CRC 불일치 → `crc_err++`, 둘 다 `ST_SOF` 로. 기본(Part A/B) 파서는 오류를 낸 바이트까지 버리고 다음 0xA5 를 찾는다. 다음을 주입하고 **결과를 정확히** 단언한다 (기본 파서가 무엇을 잃는지까지):

| # | 주입 | 기본 파서 결과 (테스트로 확인) |
|---|---|---|
| B1 | TYPE·PAYLOAD·CRC 의 모든 단일 비트 뒤집기 (13바이트 × 8 = 104가지) | 그 프레임만 `crc_err`, 다음 프레임은 받음 |
| B2 | `A5 41 …`, `A5 FF …` (LEN 65, 255) | `len_err` 2, 뒤 프레임 받음 |
| B3 | 잘린 프레임(앞 8바이트만) 뒤에 Y, Z, W | **Y, Z 손실**, `crc_err` 1, W 만 받음 |
| B4 | 스트림 중간에서 시작: (a) 꼬리에 0xA5 없음 (b) 꼬리 PAYLOAD 에 `A5 08` | (a) 손실 없음 (b) **Y 손실** |
| B5 | PAYLOAD 64바이트 전부 0xA5, CRC 상위/하위 바이트가 0xA5 인 프레임 | 오류 0 — 동기가 맞으면 0xA5 는 그냥 데이터 |
| B6 | 쓰레기 0xA5 한 바이트 바로 뒤에 진짜 프레임 | 진짜 SOF 를 LEN(0xA5 = 165)으로 읽어 **Y 손실** |

### Part C — 재동기화 파서

`fp_feed(p, b)` 의 `resync` 모드: 오류가 나면 "SOF 뒤에 먹은 바이트(`raw[0..raw_n)`)" 를 버리지 말고 **처음부터 다시 훑는다** — 그 안에 진짜 SOF 가 있을 수 있다. 버퍼는 고정 68바이트, malloc 없음. 그리고:

- B3, B4b, B6, B2 를 다시 돌려 **손실 0** 을 단언.
- 고정 시드 스트림: 프레임 3000개(그중 10% 는 SOF·LEN 포함 아무 곳 1비트 손상) 사이사이에 쓰레기 0..24바이트(1/4 이 0xA5) — 손상 안 된 프레임 **전부가 순서대로**, 손상·가짜 프레임은 **하나도** 나오지 않아야 한다.
- 통계 `frames_ok / crc_err / len_err / resyncs` 와 바이트당 최악 `fp_step` 횟수(≤ 2347 이론 한계)를 출력.

## 2. Problem (English)

> **Length-prefixed frame codec.** A sensor hub sends frames over UART:
>
> `[SOF=0xA5][LEN][TYPE][PAYLOAD × LEN][CRC_HI][CRC_LO]`
>
> - `LEN` counts payload bytes only, `0 ≤ LEN ≤ 64`. Total frame size is `LEN + 5`.
> - CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR) over `LEN, TYPE, PAYLOAD`, sent MSB first.
>
> Implement, without dynamic allocation:
>
> ```c
> int  frame_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap);
>      // returns bytes written, FE_ERR_LEN if len > 64, FE_ERR_CAP if cap < len + 5
> void fp_init(frame_parser *p, bool resync, frame_cb cb, void *ctx);
> int  fp_feed(frame_parser *p, uint8_t byte);   // called once per received byte; returns frames delivered
> ```
>
> `fp_feed` must be callable from a UART RX interrupt: O(1) memory, bounded work per byte, the callback gets
> `(type, payload, len)` for every frame whose CRC matches. Keep counters `frames_ok`, `crc_err`, `len_err`.
>
> Example: `frame_encode(0x01, "Hi", 2, out, 16)` → 7, `out = A5 02 01 48 69 27 72`.
> Feeding those 7 bytes returns 0,0,0,0,0,0,1 and delivers `(0x01, {0x48,0x69}, 2)`.
>
> Follow-ups: (1) The receiver powers up in the middle of a frame — what happens? (2) A frame is truncated and the next one follows immediately — how many frames do you lose? (3) Make the parser recover every intact frame after any corruption, with a bounded buffer. (4) What is the probability that garbage is accepted as a frame?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 다섯 개: A1 `crc16_update`, A2 `frame_encode`, A3+B1 `fp_step`(정상 경로 + 오류 경로), C1 `fp_feed` 의 재동기화 루프. 하네스·스트림 조립 도우미·`fp_deliver` 는 주어진다. 스텁은 "CRC 그대로 / FE_ERR_CAP / EV_NONE" 을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:162  crc16((const uint8_t *)"123456789", 9) == 0x29B1
FAIL starter.c:168  frame_encode(0x01, hi, 2, out, sizeof out) == 7 && memcmp(out, hi_frame, 7) == 0
…
B1 single-bit flips in TYPE/PAYLOAD/CRC: 104 cases, bad=104
…
FAIL starter.c:375  ngood > 2500 && rx_equals(exp, ngood)
FAIL starter.c:377  naive_got < ngood
6/32 passed
```

(exit 1.) Part A·B 만 채우고 C1 을 비워 두면 **27/32** — B 의 "기본 파서는 Y 를 잃는다" 테스트는 통과하고, C 의 "손실 0" 테스트 5개가 실패한다. 단계별로 확인하기 좋다.

## 4. 힌트

<details><summary>힌트 1 — CRC 한 바이트: 위에서 넣고 위에서 뺀다</summary>

반사 없는(MSB 먼저) CRC 는 바이트를 레지스터 **상위 8비트**에 XOR 한 다음, 8번 "최상위 비트가 1 이면 왼쪽으로 밀고 0x1021 XOR, 아니면 그냥 밀기". `uint16_t` 로 캐스트해 위로 넘친 비트를 버린다. init 0xFFFF 는 함수 밖(`crc16()`, 파서의 SOF 처리)에서 넣는다. 원리는 ALGO-M01, 표 방식은 ALGO-M02 / CPP-M13.

</details>

<details><summary>힌트 2 — 인코더는 "검사 먼저, 쓰기 나중"</summary>

`len > 64` 와 `cap < len + 5` 를 **아무것도 쓰기 전에** 검사한다 (테스트가 `out[0]` 이 그대로인지 본다). CRC 는 `out + 1` 부터 `len + 2` 바이트 — 방금 쓴 LEN·TYPE·PAYLOAD 를 그대로 다시 읽으면 송신 쪽과 수신 쪽 범위가 어긋날 일이 없다. `len == 0` 이면 `payload` 가 NULL 일 수 있으니 `memcpy` 를 건너뛴다.

</details>

<details><summary>힌트 3 — 파서는 "지금 무엇을 기다리나" 하나만 기억한다</summary>

상태 6개면 충분하다. `ST_SOF` 에선 0xA5 가 아니면 버린다. SOF 를 보면 `raw_n = 0`, `crc = 0xFFFF`. 그 뒤 모든 바이트는 상태와 상관없이 먼저 `raw[raw_n++]` 에 넣어 두면 PAYLOAD 위치(`raw + 2`)와 CRC 위치가 자동으로 정해지고, Part C 에서 "다시 훑을 재료"도 된다. LEN 은 **받자마자** 검사해야 `raw[]` 가 넘치지 않는다. LEN = 0 이면 TYPE 다음에 바로 CRC.

</details>

<details><summary>힌트 4 — 왜 다음 프레임을 잃나: 바이트를 세어 보라</summary>

B3: 잘린 X 는 LEN = 20 이라 SOF 뒤 24바이트를 기다린다. 받은 건 7바이트뿐 → 나머지 17바이트를 **Y(9바이트) 전체와 Z 의 앞 8바이트**로 채운다. CRC 가 틀려 버리면 Y·Z 의 SOF 도 같이 버려졌다. 고치려면? "버린 바이트 안에 진짜 SOF 가 있었다" → 버리지 말고 다시 보면 된다.

</details>

<details><summary>힌트 5 — 재동기화 버퍼는 왜 68바이트로 충분한가</summary>

오류가 나면 다시 훑을 바이트 = (가짜 SOF 뒤에 먹은 바이트) + (아직 안 훑은 바이트). 다시 훑는 도중 또 오류가 나면, 새 가짜 SOF 는 이 목록 **안에** 있으므로 그 SOF 한 바이트 이상만큼 목록이 짧아진다. 처음 목록은 `raw[]` 전체(최대 68). 그래서 `uint8_t q[68]` 하나와 인덱스 두 개로 끝난다 (`memmove` 로 남은 부분을 밀고 `raw` 를 앞에 복사).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 형식 확정 | SOF 값, LEN 이 세는 범위(payload 만? 전체?), 최대 LEN(버퍼 크기 = 최악 손실 구간), CRC 종류·범위·바이트 순서, LEN=0 허용 여부 |
| 2. 인코더 | 검사 → 쓰기, 반환값으로 오류 구분, 호출자가 버퍼와 용량을 준다 |
| 3. 파서 | 바이트 하나 = 함수 호출 하나, 상태 6개, CRC 누적, 정적 버퍼. 프레임 완성 시 콜백 |
| 4. 오류 | LEN 즉시 검사, CRC 불일치, 잘림/중간 시작 → "다음 프레임까지 잃는다" 를 바이트로 보여 주기 |
| 5. 재동기화 | 버린 바이트 다시 훑기, 버퍼 상한 증명, 바이트당 최악 비용, ISR 에서 할지 메인 루프에서 할지 |
| 6. 한계 | 거짓 수락 ≈ 2^-16 / 가짜 SOF, 끝에 걸린 미완성 프레임 → 타임아웃(COMM-M10) |

### Part A — CRC 와 인코더

CRC-16/CCITT-FALSE 의 파라미터는 width 16, poly 0x1021, init 0xFFFF, refin = refout = false, xorout 0x0000, check("123456789") = 0x29B1. "CCITT" 라는 이름이 붙은 변형이 여럿이라(XMODEM 은 init 0, KERMIT 는 반사형) **파라미터 다섯 개를 문서에 적는 것**이 이 레슨의 첫 번째 교훈이다. 이름만 적으면 상대 팀은 다른 변형을 고른다 (7. 실수 3).

설계 결정과 이유:

| 결정 | 이유 |
|---|---|
| LEN = PAYLOAD 만 | 수신 쪽 계산이 `2 + LEN + 2` 로 단순. "TYPE 포함" 이나 "프레임 전체" 로 정해도 되지만 **반드시 문서에 적고** 양쪽이 같은 상수를 쓴다 |
| 최대 LEN 64 | 수신 버퍼가 68바이트로 정해진다. 그리고 LEN 검사 자체가 첫 번째 필터: 랜덤 바이트가 LEN 으로 통과할 확률은 65/256 ≈ 25.4% — 가짜 SOF 의 약 75% 를 1바이트 만에 걸러 낸다 |
| CRC 에서 SOF 제외 | SOF 는 상수라 정보가 없다(덮어도 init 이 바뀐 것과 같다). MAVLink 도 시작 바이트를 빼고 계산한다 |
| CRC 상위 바이트 먼저 | 반사 없는 CRC 는 레지스터의 MSB 가 먼저 나가는 것이 자연스럽고, 그러면 **LEN 부터 CRC_LO 까지 전체의 CRC 가 0** 이 된다 (잔여값 검사, 연습 1). Modbus 는 반사형 CRC 라 하위 바이트 먼저다 (COMM-B13) |
| CRC 를 바이트마다 누적 | 마지막 바이트에서 한 번에 64바이트를 돌리지 않으므로 바이트당 일이 고르다 (ISR 지연 예측 가능) |

### Part A — 파서 상태 머신

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="b10ar" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">fp_step(): 바이트 하나 = 화살표 하나</text>
  <g stroke="currentColor">
    <rect x="20" y="80" width="100" height="40" rx="6" class="f-acc"/>
    <rect x="142" y="80" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="264" y="80" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="386" y="80" width="100" height="40" rx="6" class="f-ok"/>
    <rect x="508" y="80" width="100" height="40" rx="6" class="f-warn"/>
    <rect x="630" y="80" width="100" height="40" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="38" y="105">ST_SOF</text><text x="160" y="105">ST_LEN</text><text x="280" y="105">ST_TYPE</text>
    <text x="396" y="105">ST_PAYLOAD</text><text x="518" y="105">ST_CRC_HI</text><text x="640" y="105">ST_CRC_LO</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b10ar)">
    <line x1="120" y1="100" x2="140" y2="100"/>
    <line x1="242" y1="100" x2="262" y2="100"/>
    <line x1="364" y1="100" x2="384" y2="100"/>
    <line x1="486" y1="100" x2="506" y2="100"/>
    <line x1="608" y1="100" x2="628" y2="100"/>
    <path d="M314,80 C340,25 540,25 556,78"/>
    <path d="M420,80 C405,55 470,55 455,78"/>
    <path d="M45,80 C30,55 95,55 80,78"/>
    <path d="M192,120 V160 H75 V122"/>
    <path d="M670,120 V200 H55 V122"/>
    <path d="M690,120 V230 H35 V122" stroke-dasharray="5,3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="18" y="48">0xA5 아닌 바이트 버림</text>
    <text x="104" y="76">0xA5</text>
    <text x="210" y="76">≤64</text>
    <text x="372" y="140">LEN개 반복 (CRC 누적)</text>
    <text x="400" y="30">LEN = 0 이면 TYPE 다음 바로 CRC</text>
    <text x="80" y="178">LEN &gt; 64 → len_err++ (EV_ERR_LEN)</text>
    <text x="260" y="195">CRC 일치 → frames_ok++, EV_FRAME (콜백에 raw+2, LEN)</text>
    <text x="260" y="225">CRC 불일치 → crc_err++ (EV_ERR_CRC)</text>
  </g>
  <text x="10" y="262" fill="currentColor" font-size="12">SOF 뒤의 모든 바이트는 raw[] 에 기록: raw[0]=LEN, raw[1]=TYPE, raw[2..]=PAYLOAD, 끝 2바이트=CRC (최대 68)</text>
  <text x="10" y="284" fill="currentColor" font-size="12">Part C: 오류 화살표(아래 둘)를 탈 때 raw[] 를 버리지 않고 ST_SOF 에서 처음부터 다시 훑는다</text>
</svg>
```

"Hi" 프레임을 한 바이트씩 넣은 추적 (CRC 값은 코드 출력과 파이썬 재계산이 일치):

| 입력 | 상태(전) | 한 일 | raw_n | crc(후) | 상태(후) | 반환 |
|---|---|---|---|---|---|---|
| A5 | ST_SOF | SOF 확인, raw_n = 0, crc = FFFF | 0 | FFFF | ST_LEN | 0 |
| 02 | ST_LEN | 2 ≤ 64 → len = 2 | 1 | C1B2 | ST_TYPE | 0 |
| 01 | ST_TYPE | len ≠ 0 → PAYLOAD 로 | 2 | 6B4C | ST_PAYLOAD | 0 |
| 48 | ST_PAYLOAD | raw_n = 3 < 2+2 | 3 | 5801 | ST_PAYLOAD | 0 |
| 69 | ST_PAYLOAD | raw_n = 4 = 2+2 → CRC 로 | 4 | 2772 | ST_CRC_HI | 0 |
| 27 | ST_CRC_HI | 상위 바이트 저장(raw) | 5 | 2772 | ST_CRC_LO | 0 |
| 72 | ST_CRC_LO | 받은 0x2772 == 계산 0x2772 → 전달 | 6 | — | ST_SOF | **1** |

메모리: 파서 구조체 하나(`raw[68]` + 상태·CRC·통계 약 40바이트)가 전부다. 시간: 바이트당 CRC 8회전 + 분기 몇 개 — O(1).

### Part B — 오류 다섯 가지를 바이트로 보기

**CRC 오류 (B1).** CRC-16/CCITT 다항식은 항이 둘 이상이라 단일 비트 오류를 **항상** 잡는다(오류 다항식 x^k 는 생성 다항식으로 나누어떨어지지 않는다). 그래서 104가지 전부 `crc_err` 1개, 다음 프레임 정상 — 테스트 결과 `bad=0`. 길이가 맞으니 동기를 잃지 않는다. (LEN 바이트의 비트 오류는 길이를 바꾸므로 동기를 잃는다 → Part C 의 랜덤 손상 스트림에서 다룬다.) 검출 능력 전반(버스트, HD)은 COMM-B08.

**LEN > MAX (B2).** LEN 바이트에서 바로 `len_err`. 이 검사가 없으면 `raw[]` 를 넘쳐 쓰는 버퍼 오버플로가 된다 — 파서 보안 버그의 단골(TEST-M02 퍼징으로 찾는 종류). "LEN 을 믿기 전에 범위부터" 는 면접관이 꼭 본다.

**잘림 (B3) 과 중간 시작 (B4b).** 둘 다 같은 병이다: 파서가 **가짜 시작점**에 동기를 맞추고, 그 가짜 LEN 만큼 진짜 바이트를 삼킨다. B4b 의 실제 바이트(PAYLOAD[2] 부터 시작):

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">B4b: 수신이 X 의 PAYLOAD[2] 부터 시작 — X 꼬리 안의 "A5 08" 이 가짜 SOF</text>
  <g fill="currentColor" font-size="12">
    <text x="20" y="58">X 꼬리 (12바이트)</text><text x="368" y="58">Y (9바이트)</text><text x="629" y="58">Z …</text>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="66" width="27" height="26" class="f-mut"/><rect x="49" y="66" width="27" height="26" class="f-warn"/>
    <rect x="78" y="66" width="27" height="26" class="f-mut"/><rect x="107" y="66" width="27" height="26" class="f-mut"/>
    <rect x="136" y="66" width="27" height="26" class="f-mut"/><rect x="165" y="66" width="27" height="26" class="f-mut"/>
    <rect x="194" y="66" width="27" height="26" class="f-mut"/><rect x="223" y="66" width="27" height="26" class="f-mut"/>
    <rect x="252" y="66" width="27" height="26" class="f-mut"/><rect x="281" y="66" width="27" height="26" class="f-mut"/>
    <rect x="310" y="66" width="27" height="26" class="f-mut"/><rect x="339" y="66" width="27" height="26" class="f-mut"/>
    <rect x="368" y="66" width="27" height="26" class="f-acc"/><rect x="397" y="66" width="27" height="26" class="f-ok"/>
    <rect x="426" y="66" width="27" height="26" class="f-ok"/><rect x="455" y="66" width="27" height="26" class="f-ok"/>
    <rect x="484" y="66" width="27" height="26" class="f-ok"/><rect x="513" y="66" width="27" height="26" class="f-ok"/>
    <rect x="542" y="66" width="27" height="26" class="f-ok"/><rect x="571" y="66" width="27" height="26" class="f-ok"/>
    <rect x="600" y="66" width="27" height="26" class="f-ok"/>
    <rect x="629" y="66" width="27" height="26" class="f-acc"/><rect x="658" y="66" width="27" height="26" class="f-mut"/>
    <rect x="687" y="66" width="27" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="26" y="84">62</text><text x="55" y="84">A5</text><text x="84" y="84">08</text><text x="113" y="84">65</text>
    <text x="142" y="84">66</text><text x="171" y="84">67</text><text x="200" y="84">68</text><text x="229" y="84">69</text>
    <text x="258" y="84">6A</text><text x="287" y="84">6B</text><text x="316" y="84">63</text><text x="345" y="84">30</text>
    <text x="374" y="84">A5</text><text x="403" y="84">04</text><text x="432" y="84">21</text><text x="461" y="84">30</text>
    <text x="490" y="84">31</text><text x="519" y="84">32</text><text x="548" y="84">33</text><text x="577" y="84">11</text>
    <text x="606" y="84">EF</text><text x="635" y="84">A5</text><text x="664" y="84">0A</text><text x="693" y="84">22</text>
  </g>
  <line x1="49" y1="104" x2="424" y2="104" stroke="currentColor" stroke-width="2"/>
  <line x1="49" y1="96" x2="49" y2="112" stroke="currentColor"/><line x1="424" y1="96" x2="424" y2="112" stroke="currentColor"/>
  <text x="100" y="128" fill="currentColor" font-size="12">가짜 프레임: LEN=8 → SOF 뒤 12바이트를 기다림. Y 의 "A5 04" 를 CRC 로 읽고 CRC 오류</text>
  <line x1="426" y1="140" x2="627" y2="140" stroke="currentColor" stroke-dasharray="4,3"/>
  <text x="430" y="158" fill="currentColor" font-size="12">기본 파서: 0xA5 없음 → 버림 → Y 손실</text>
  <path d="M420,172 H90 V96" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#b10ar2)"/>
  <defs>
    <marker id="b10ar2" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="9" markerHeight="9" markerUnits="userSpaceOnUse" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="100" y="190" fill="currentColor" font-size="12">재동기화: raw[] = 08 65 … 30 A5 04 를 가짜 SOF 다음 바이트(08)부터 다시 훑기</text>
  <line x1="368" y1="212" x2="627" y2="212" stroke="currentColor" stroke-width="2"/>
  <line x1="368" y1="204" x2="368" y2="220" stroke="currentColor"/><line x1="627" y1="204" x2="627" y2="220" stroke="currentColor"/>
  <text x="280" y="240" fill="currentColor" font-size="12">→ raw 안의 A5(Y 의 SOF)를 찾아 이어서 Y 완성 (crc 0x11EF 일치)</text>
</svg>
```

B3 도 같다: X(LEN 20) 의 앞 8바이트 뒤에 Y·Z 가 오면 X 는 Y 전체(9) + Z 앞 8바이트를 삼키고, Z 의 남은 7바이트에는 0xA5 가 없어(테스트가 이 전제를 `has_sof` 로 확인) **Y·Z 둘 다** 잃는다. 잘린 프레임 하나가 최대 LEN+4 = 68바이트, 즉 짧은 프레임이면 **여러 개**를 데려간다.

**SOF 값이 데이터 안에 (B5).** 동기가 맞은 상태에서 0xA5 는 아무 의미가 없다 — 파서는 PAYLOAD·CRC 상태에서 값을 보지 않는다. 길이 prefix 방식이 스터핑 없이 되는 이유다. 테스트는 PAYLOAD 64바이트 전부 0xA5, 그리고 탐색으로 찾은 "CRC 상위 바이트가 0xA5"(TYPE 0x00, 데이터 0x72 → CRC 0xA579) 와 "하위 바이트가 0xA5"(데이터 0x2B → 0x6EA5) 프레임을 넣는다 — 오류 0. 대가는 동기를 **잃었을 때** 이 0xA5 들이 가짜 SOF 가 된다는 것(B4b).

**A5 A5 (B6).** 쓰레기 0xA5 하나가 진짜 프레임 바로 앞에 있으면, 진짜 SOF 가 LEN 자리에 들어가 165 > 64 → `len_err`. 기본 파서는 그 바이트(진짜 SOF)를 버렸으므로 진짜 프레임 전체를 잃는다. 노이즈가 많은 RS-485 에서 흔히 보는 모양.

### Part C — 재동기화: 버린 바이트를 다시 본다

규칙 하나: **오류가 나면 가짜 SOF 다음 바이트부터 다시 훑는다.** 가짜 SOF 다음 바이트들은 `raw[]` 에 그대로 있다(SOF 뒤 모든 바이트를 기록한 이유). 구현:

```c
uint8_t q[FP_RAW_MAX]; size_t qn = 1, i = 0; q[0] = b;      /* 훑을 목록 = 이번 바이트 */
while (i < qn) {
    fp_event ev = fp_step(p, q[i++]);
    if (ev == EV_FRAME) { fp_deliver(p); frames++; }
    else if (ev == EV_ERR_LEN || ev == EV_ERR_CRC) {
        size_t rest = qn - i;                     /* 아직 안 훑은 바이트 */
        memmove(q + p->raw_n, q + i, rest);       /* 뒤로 밀고 */
        memcpy(q, p->raw, p->raw_n);              /* 가짜 SOF 뒤 바이트를 앞에 */
        qn = p->raw_n + rest; i = 0; p->stats.resyncs++;
    }
}
```

**왜 버퍼가 넘치지 않나 (증명).** 첫 오류에서 목록 = `raw` 전체 ≤ 68. 다시 훑는 동안 파서는 `ST_SOF` 에서 시작하므로, 다음 오류의 `raw` 는 목록 안의 어떤 SOF(위치 s) **뒤**에서 먹은 바이트뿐이다. 새 목록 = 목록[s+1 ..] → 길이가 최소 1 줄어든다. 그래서 항상 ≤ 68 이고, `memmove` 의 목적지(`raw_n ≤ i`)도 원본보다 앞이라 안전하다. 같은 이유로 루프는 끝나고, 한 바이트 입력의 최악 `fp_step` 수는 1 + 68 + 67 + … + 1 = **1 + 68·69/2 = 2347** 이다.

**왜 정상 프레임을 잃지 않나.** 정상 프레임의 SOF 위치를 P 라 하자. 파서가 P 보다 앞의 가짜 SOF 에 걸려 있으면, 그 가짜 프레임은 (a) 오류로 끝나 P 를 포함한 뒷부분이 다시 훑어지거나, (b) **CRC 까지 우연히 맞아** 거짓 수락된다. (b) 만이 손실을 만든다. 가짜 프레임이 스트림 끝에서 미완성으로 남는 경우는 바이트가 더 와야 끝나므로 → 테스트는 꼬리에 0x00 × 68 을 붙이고, 실기에서는 바이트 타임아웃/IDLE 라인 때 flush 한다 (연습 4, COMM-M10, COMM-M11).

실제 결과 (`solution.c` C4, 고정 시드):

```text
C4 stream: 146478 bytes, 2685 good + 315 corrupted frames, 35672 garbage bytes
   naive : recovered 1398/2685 good (lost 1287)  frames_ok=1398 crc_err=1084 len_err=4363
   resync: recovered 2685/2685 good             frames_ok=2685 crc_err=1906 len_err=7353 resyncs=9259 max_steps/byte=200
```

기본 파서는 이 거친 스트림(쓰레기의 1/4 이 0xA5)에서 정상 프레임의 **48%** 를 잃었고, 재동기화 파서는 0개. `len_err` 가 `crc_err` 보다 훨씬 많다 — 가짜 SOF 의 약 3/4 이 LEN 검사 1바이트로 걸러진다는 앞의 계산과 맞는다.

### 실험 — `demo_resync.c` (실측)

프레임 100000개, 쓰레기 0..24바이트 간격, 쓰레기 중 0xA5 비율과 손상률을 바꿔 가며 (`./demo_resync`, 약 2.4 초):

| 쓰레기 중 0xA5 비율 | 손상 | 기본 파서 복구 | 재동기화 복구 | 거짓 수락 (기본 / 재동기화) | fp_step / 입력 바이트 | 최악 step / feed |
|---|---|---|---|---|---|---|
| 0 (랜덤만, ≈1/256) | 0% | 98.43% | 100.000% (100000/100000) | 0 / 0 | 1.01 | 95 |
| 1/16 | 0% | 78.31% | 99.996% (99996/100000) | 1 / 2 | 1.14 | 167 |
| 1/4 | 0% | 51.33% | 99.998% (99998/100000) | 1 / 2 | 1.44 | 212 |
| 1/2 | 0% | 40.89% | 99.997% (99997/100000) | 1 / 2 | 1.63 | 256 |
| 0 (랜덤만, ≈1/256) | 10% | 98.30% | 100.000% (90107/90107) | 0 / 0 | 1.08 | 125 |
| 1/16 | 10% | 78.04% | 99.999% (89999/90000) | 1 / 1 | 1.21 | 174 |
| 1/4 | 10% | 51.35% | 99.999% (90142/90143) | 1 / 1 | 1.51 | 206 |
| 1/2 | 10% | 40.74% | 99.999% (90035/90036) | 0 / 1 | 1.70 | 279 |

```text
실험 2: 랜덤 60000000 바이트 (0xA5 30111627개), 재동기화 파서
  거짓 수락 62개 (이론 ≈ 58.3 = #A5 × 65/512 × 2^-16), len_err=26284838 crc_err=3825671, step/byte=3.73, 최악 step/feed=362
```

읽는 법:

- **재동기화가 손실을 거의 없앤다**, 그러나 0 은 아니다. 남은 손실은 전부 **거짓 수락**이 진짜 프레임 일부를 삼킨 경우다 (거짓 수락 1번당 진짜 프레임 1~2개).
- **거짓 수락률의 이론**: 가짜 SOF 마다 LEN 이 통과할 확률 × CRC 가 우연히 맞을 확률 2^-16. 실험 2 에서 0xA5 가 절반이라 LEN 자리가 0..64 일 확률은 (1/2)·(65/256) = 65/512. 3011만 × 65/512 × 2^-16 ≈ 58.3, 실측 62 (같은 조건 다른 시드로 60M 바이트를 돌렸을 땐 45 — 포아송 잡음 범위 ±8 안팎). CRC 검사 횟수(`crc_err` 3825671)를 2^16 으로 나눠도 58.4 — 숫자가 서로 맞는다.
- **비용**: 평균 fp_step 은 입력 바이트당 1.0~1.7회(쓰레기만 들어오면 3.7회)로 싸다. 하지만 **최악 한 바이트**에 95~362 step 이 몰린다(이론 상한 2347). 이것이 "재동기화는 ISR 이 아니라 메인 루프에서" 의 근거다 (아래, Q5).

거짓 수락을 줄이는 방법(면접 단골 꼬리질문): CRC-32 (2^-32), 2바이트 sync word(예: UBX 의 `B5 62`, 가짜 SOF 가 1/256 → 1/65536), TYPE 별 허용 LEN 검사(연습 3), 시퀀스 번호, 프레임 간 최소 침묵, 상위 계층 ACK/재전송(COMM-M14).

### ISR 에 둘까, 메인 루프에 둘까

| 배치 | 장점 | 단점 |
|---|---|---|
| RX ISR 에서 `fp_feed` | 지연 최소, 버퍼 하나 | 재동기화 최악 2347 step + 콜백이 ISR 안 → 다음 바이트의 RDR 을 못 읽어 하드웨어 overrun (STM32 의 ORE 플래그) |
| ISR 은 링 버퍼에 넣기만, 메인 루프가 `fp_feed` | ISR 은 몇 사이클, 파서 비용은 평균으로 흡수 | 링 버퍼 크기 = (메인 루프 최대 지연) × (바이트율). 넘치면 소프트웨어 overflow → 그래도 재동기화가 회복 (연습 2) |
| DMA circular + IDLE 인터럽트 | 바이트당 인터럽트 없음 | 위치 계산·랩어라운드 (COMM-M11, DS-A02) |

115200 baud 에서 바이트 간격은 86.8 µs. 비트 단위 CRC 한 바이트가 Cortex-M 에서 대략 수십 사이클이라(추정, 측정 아님) 평균 경로는 ISR 에서도 넉넉하지만, 최악 수백 step × (CRC + 분기)는 72 MHz 에서도 바이트 간격을 넘을 수 있다. 그래서 실전 기본형은 "ISR → 링 버퍼 → 메인 루프 파서 → TYPE 디스패처" 다 (연습 2, 3).

### 복잡도 정리

| 항목 | 값 |
|---|---|
| RAM | 파서 1개 ≈ 68 + 40 바이트, `fp_feed` 스택 68바이트 (`q[]`) |
| 인코더 | O(LEN), 출력 버퍼는 호출자가 (`LEN + 5` 이상) |
| 파서(기본) | 바이트당 O(1) |
| 파서(재동기화) | 평균 바이트당 1~2 step(실측), 최악 2347 step (증명), 상태는 그대로 고정 크기 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 32/32 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_resync.c -o demo_resync && ./demo_resync   # 약 2.4 초, 위 표
```

`solution.c` 실행 출력 중 테스트 외 줄:

```text
B1 single-bit flips in TYPE/PAYLOAD/CRC: 104 cases, bad=0
B5 CRC with 0xA5: hi-frame type=0x00 data=0x72, lo-frame type=0x00 data=0x2B
C4 stream: 146478 bytes, 2685 good + 315 corrupted frames, 35672 garbage bytes
   naive : recovered 1398/2685 good (lost 1287)  frames_ok=1398 crc_err=1084 len_err=4363
   resync: recovered 2685/2685 good             frames_ok=2685 crc_err=1906 len_err=7353 resyncs=9259 max_steps/byte=200
32/32 passed
```

## 7. 자주 하는 실수

**1. LEN 을 검사하기 전에 버퍼에 쓴다.** `p->len = b;` 하고 PAYLOAD 를 `buf[i++]` 로 받으면 LEN 255 에서 68바이트 버퍼를 넘친다 — 원격에서 메모리를 덮을 수 있는 전형적인 파서 취약점. 고친 코드: `case ST_LEN: if (b > FRAME_MAX_PAYLOAD) { len_err++; st = ST_SOF; return EV_ERR_LEN; }` 를 **저장보다 먼저**. 인코더도 같은 원칙: 검사 후 쓰기.

**2. LEN 의 의미가 양쪽에서 다르다.** 송신은 "TYPE 포함 길이", 수신은 "PAYLOAD 만" 으로 짜면 모든 프레임이 1바이트씩 어긋나 CRC 오류 — 보통 "가끔 되는" 게 아니라 "전부 안 되는" 버그라 금방 보이지만, LEN 0 만 쓰는 PING 같은 명령에서만 시험하면 놓친다. 형식 표를 문서에 두고 양쪽이 같은 헤더(`FRAME_OVERHEAD`, `FRAME_MAX_PAYLOAD`)를 include 한다.

**3. "CRC-16" 이라고만 쓴다.** CCITT-FALSE(init FFFF), XMODEM(init 0), KERMIT(반사), MODBUS(반사, init FFFF, poly 0x8005) 는 전부 "CRC-16" 이다. "123456789" check 값(이 레슨 0x29B1)을 문서와 단위 테스트에 같이 둔다. CRC 바이트 순서도 마찬가지 — 이 레슨에서 CRC 를 little-endian 으로 붙이면 잔여값이 0 이 아니라 0xFB1A 가 된다 (연습 1).

**4. CRC 범위가 한 바이트 어긋난다.** 송신은 `out + 1` 부터(LEN 부터), 수신은 SOF 를 받을 때 이미 `crc16_update(0xFFFF, 0xA5)` 해 버리는 식. 고친 코드: SOF 에서는 `crc = 0xFFFF` 로 **초기화만**, 갱신은 LEN 부터.

**5. 오류 뒤 바이트를 그냥 버린다.** 동작은 하지만 잘림·중간 시작·쓰레기 0xA5 마다 다음 프레임(들)을 잃는다 — 이 레슨 C4 스트림에서 48%, demo 에서 0xA5 쓰레기 1/16 만 돼도 22%. 고친 코드: Part C 재동기화.

**6. 재동기화를 재귀로 짠다.** 오류 → `for (k...) fp_feed(p, raw[k])` 처럼 자기 자신을 부르면, 다시 훑는 중 또 오류가 날 때 재귀가 쌓이고 `raw[]` 를 읽으면서 동시에 덮어쓴다(같은 버퍼). 고친 코드: 별도 `q[]` 에 복사해 두고 **반복문** 하나로, 길이가 줄어든다는 불변식으로 끝남을 보장.

**7. 끝에 걸린 미완성 프레임을 잊는다.** 가짜 SOF + 큰 가짜 LEN 뒤에 진짜 짧은 프레임이 오고 라인이 조용해지면, 진짜 프레임은 바이트가 더 올 때까지 갇혀 있다. 요청-응답 프로토콜이면 영원히 응답이 없다. 고친 코드: 바이트 타임아웃/IDLE 에서 `fp_flush()` (연습 4, COMM-M10).

## 8. 면접 Q&A

<details><summary>Q1. 길이 prefix, 바이트 스터핑(SLIP/COBS), 침묵 간격(Modbus RTU) 중 무엇을 고르나요?</summary>

길이 prefix: 오버헤드 고정(여기선 5바이트), 데이터를 안 건드려 DMA 로 바로 보낼 수 있고 수신도 "몇 바이트 남았나" 를 안다. 대신 동기를 잃으면 다시 찾기가 어렵다(가짜 SOF) — 재동기화와 CRC 가 필수. 스터핑: 구분 바이트가 데이터에 절대 안 나오므로 **다음 구분 바이트에서 무조건 재동기화** — 강건하다. 대가는 가변 오버헤드(SLIP 최악 2배, COBS 최악 약 0.4%)와 인코딩 비용 (COMM-B11, B12). 침묵 간격: 바이트 사이 타이밍을 정확히 재야 하고 USB-UART 브리지·OS 지연에 약하다 (COMM-B13). 정리: 링크가 깨끗하고 CPU·DMA 친화가 중요하면 길이+CRC(+재동기화), 노이즈·핫플러그가 잦으면 COBS 를 기본으로 권한다. 둘을 합친 COBS + 길이 + CRC 도 흔하다.

</details>

<details><summary>Q2. 수신기가 프레임 중간에 켜지면 어떻게 되나요? 몇 개를 잃을 수 있죠?</summary>

꼬리 안에 0xA5 가 없으면 다음 SOF 부터 정상(B4a, 손실 0). 있으면 그 0xA5 를 SOF 로 오인하고 LEN 이 0..64 면(약 25%) LEN+4 바이트를 삼킨 뒤 CRC 오류. 기본 파서는 그 구간에 걸린 진짜 프레임을 전부 잃는다 — 최대 68바이트 구간이므로 짧은 프레임 여러 개(B3 에서 2개). 재동기화 파서는 삼킨 구간을 다시 훑어 0개를 잃는다. 꼬리질문 "부팅 직후 첫 프레임이 꼭 필요하면?" → 송신 쪽이 주기적으로 보내거나, 요청-응답에서 타임아웃 후 재요청(COMM-M14).

</details>

<details><summary>Q3. CRC 는 왜 SOF 를 빼고 LEN 부터 계산했나요? 바이트 순서는 왜 big-endian 이죠?</summary>

SOF 는 상수라 덮어도 검출력이 늘지 않는다 — CRC 레지스터 초기값이 다른 상수로 바뀐 것과 같다. 반대로 LEN 은 **반드시** 덮어야 한다: LEN 이 깨지면 경계가 틀어지는데, 그걸 잡는 게 CRC 다. 순서: 반사 없는 CRC 는 레지스터의 최상위 비트가 다항식 나눗셈의 "앞" 이므로 상위 바이트를 먼저 보내면 수신 쪽이 CRC 바이트까지 같은 루프로 돌렸을 때 잔여값이 0 이 된다(연습 1에서 확인, LE 로 보내면 0xFB1A). 반사형 CRC(Modbus, MAVLink 의 X.25 계열)는 반대로 하위 바이트 먼저가 자연스럽다. 결국 "자연스러운" 순서는 CRC 종류가 정하고, 중요한 건 **문서에 적는 것**.

</details>

<details><summary>Q4. 재동기화의 버퍼 크기와 바이트당 최악 비용을 증명해 보세요.</summary>

다시 훑을 목록은 처음에 `raw[]` (≤ 68). 다시 훑는 동안 새 오류가 나면 새 목록은 "목록 안의 새 가짜 SOF 뒤" 이므로 최소 1 짧다. 따라서 목록 ≤ 68 (버퍼 68바이트, 스택), 오류 횟수 ≤ 68, 총 step ≤ 1 + 68 + 67 + … + 1 = 2347. 실측 최악은 95~362 (랜덤 스트림), 평균은 입력 바이트당 1.0~1.7 (쓰레기만이면 3.7). 대안으로 "링 버퍼에 원본 스트림을 남겨 두고 읽기 포인터만 SOF+1 로 되돌리는" 방법도 있다 — 복사가 없지만 버퍼를 프레임 최대 길이 이상 보존해야 한다(COMM-M10, DS-A02 의 Bip buffer 와 같은 고민).

</details>

<details><summary>Q5. 이 파서를 UART RX 인터럽트에서 돌려도 되나요?</summary>

기본 파서는 된다: 바이트당 O(1), CRC 8회전. 재동기화는 평균은 싸도 최악 수백~2347 step 이 한 바이트에 몰리고, 콜백(프레임 처리)까지 ISR 안에서 돌면 그 사이 다음 바이트가 RDR 에 덮여 하드웨어 overrun(STM32 ORE)이 난다. 실전형: ISR 은 `DR` 을 읽어 2의 거듭제곱 링 버퍼에 넣기만(몇 사이클), 메인 루프나 RTOS 태스크가 꺼내 `fp_feed`, 콜백은 큐에 넣거나 바로 디스패치. 링 버퍼는 단일 생산자-단일 소비자라 head/tail 각각 한쪽만 쓰면 락이 필요 없다(volatile/메모리 순서 주의 — C-M14, DS-A02). 연습 2 가 메인 루프가 400바이트 시간 동안 멈춰 372바이트를 버린 경우에도 나머지 490프레임을 전부 복구함을 보인다.

</details>

<details><summary>Q6. 쓰레기가 프레임으로 받아들여질 확률은? 어떻게 줄이죠?</summary>

가짜 SOF 하나당 ≈ P(LEN 유효) × 2^-16. 랜덤 바이트면 P(LEN 유효) = 65/256 이므로 가짜 SOF 당 약 3.9 × 10^-6, 바이트당(0xA5 확률 1/256 곱) 약 1.5 × 10^-8. demo 의 극단적 스트림(0xA5 50%)에서 6000만 바이트에 62개(이론 58.3). 줄이는 법: CRC-32(×2^-16 더), 2바이트 sync(가짜 SOF 1/256 로), TYPE 별 LEN 표 검사(연습 3 의 SET_LED 는 LEN = 1 만 허용), 시퀀스 번호, 응용 계층에서 값 범위 검사. 안전 관련이면 "잔여 오류 확률 × 메시지율 × 수명" 으로 목표를 세운다(Koopman 의 CRC 선택 가이드 참고 — COMM-B08).

</details>

<details><summary>Q7. 로직 애널라이저로 이 링크가 이상할 때 무엇부터 보나요?</summary>

(1) 바이트 레벨이 맞는가: baud·8N1·idle high, 프레이밍 에러(COMM-B02/B03) — 디코더가 바이트를 못 만들면 프레임 이야기는 의미 없다. (2) SOF 직후 LEN 이 기대값인가, 프레임 길이(마지막 바이트까지 시간)가 LEN + 5 바이트 시간과 맞는가. (3) 펌웨어 카운터: `len_err` 가 오르면 동기 문제·쓰레기, `crc_err` 만 오르면 비트 오류(노이즈, baud 오차) 또는 CRC 변형/범위/순서 불일치(모든 프레임이 실패하면 거의 후자), 소프트웨어 overflow 가 오르면 메인 루프 지연. (4) 송신 쪽에서 알려진 프레임("Hi" = `A5 02 01 48 69 27 72`)을 보내 수신 쪽 디코드와 비교. 통계 카운터를 디버그 명령으로 꺼낼 수 있게 해 두는 것이 필드 디버깅의 절반이다 (Don 의 SSD 펌웨어 UART 콘솔 경험과 같은 결).

</details>

## 9. 추가 연습

### 연습 1 — CRC 잔여값(residue) 검사

수신 쪽이 CRC 를 따로 꺼내 비교하지 않고, `crc16(frame + 1, n − 1) == 0` (LEN 부터 CRC_LO 까지 전부) 으로 검사하는 `residue_ok()` 를 짜라. LEN 0..64 전부에서 참, "Hi" 프레임의 LEN..CRC 모든 단일 비트 뒤집기에서 거짓, CRC 를 little-endian 으로 붙이면 잔여값이 무엇인지 확인하라.

<details><summary>답</summary>

반사 없는 CRC 에서 n 비트 메시지 M 을 처리한 레지스터는 R = (I·x^n + M)·x^16 mod G 다 (I = init 0xFFFF, G = 생성 다항식). CRC 를 상위 바이트 먼저 붙인 M' = M·x^16 + R 을 다시 처리하면 (I·x^(n+16) + M·x^16 + R)·x^16 = ((I·x^n + M)·x^16 + R)·x^16 ≡ (R + R)·x^16 = 0 (mod G) — GF(2) 에서 R + R = 0. 그래서 잔여값이 0 이다 (xorout = 0 이라 가능; xorout 이 있으면 0 대신 고정 상수가 나온다). LE 로 붙이면 다른 다항식을 붙인 셈이라 0 이 아니다 — "Hi" 프레임에서 0xFB1A (실측). 장점: 수신 상태 머신이 CRC 바이트도 PAYLOAD 처럼 누적하면 되므로 `ST_CRC_LO` 가 `crc == 0` 비교 하나로 끝난다.

</details>

### 연습 2 — ISR → 링 버퍼 → 메인 루프

`uart_rx_isr(rb, byte)` 는 128바이트(2의 거듭제곱) 링 버퍼에 넣기만 하고 가득 차면 버리며 `overflow++` (DS-B04). `main_loop_poll()` 은 쌓인 바이트를 전부 재동기화 파서에 넣는다. 프레임 500개를 보내며 (a) 100바이트마다 poll → 손실 0, (b) 중간에 400바이트 시간 동안 poll 이 멈춤 → overflow 가 생기지만, **버려진 구간과 겹치지 않는 프레임은 전부** 받아야 한다.

<details><summary>답</summary>

실측: (a) overflow 0, 500/500. (b) poll 이 t = 9999 에서 마지막으로 비운 뒤 멈추므로 128바이트가 차고 t = 10128 부터 다음 poll(t = 10499)까지 **372바이트**를 버린다. 그 구간에 걸린 프레임 10개만 사라지고 나머지 490/490 을 받았다 (`crc_err` 1 — 잘린 프레임 하나가 다음 바이트를 삼켰다가 재동기화로 풀림). 링 버퍼 크기 결정: 메인 루프 최대 지연 × 바이트율 + 여유. 115200 baud 에서 10 ms 지연이면 약 116바이트 → 128 은 빠듯, 256 권장. 하드웨어 overrun(ORE)과 소프트웨어 overflow 는 다른 카운터로 센다. 실제 ISR 에서는 head 를 ISR 만, tail 을 메인만 쓰고, 인덱스는 `volatile`(+ 필요하면 배리어)로 (C-M14).

</details>

### 연습 3 — TYPE 디스패처와 응답 프레임

표 `{type, min_len, max_len, handler}` 로 PING(0x01, LEN 0) → 0x81, ECHO(0x02, LEN 0..64) → 0x82 + 같은 데이터, SET_LED(0x03, LEN 정확히 1, 값 0/1) → 0x83 [값], 그 외/길이 위반/값 위반 → NAK 0x7F [TYPE, 이유] 를 만들라. 응답 프레임도 같은 인코더로 만들고, 호스트 쪽 파서로 다시 읽어 검증하라.

<details><summary>답</summary>

핸들러를 부르기 **전에** 표의 길이 범위로 거른다 — 핸들러는 길이가 맞다고 믿고 `pl[0]` 을 읽을 수 있다. 응답 TYPE = 요청 | 0x80 은 흔한 관례(요청/응답 구분). 테스트: PING → 0x81 LEN 0, ECHO "hello" → 그대로, SET_LED 1 → LED 켜짐, SET_LED 7 → NAK(BAD_ARG) 이고 LED 는 그대로, SET_LED 2바이트·PING 1바이트 → NAK(BAD_LEN), TYPE 0x42 → NAK(UNKNOWN), LEN 65 요청 → 파서가 `len_err` 로 버려 **응답 없음**(NAK 를 보낼 TYPE 도 모른다). 이 길이 표는 거짓 수락 방어도 겸한다 (Q6).

</details>

### 연습 4 — idle 라인에서 flush

스트림 `[프레임 A][A5 3C (쓰레기: 가짜 SOF + LEN 60)][프레임 B 9바이트]` 뒤에 라인이 조용해진다. 재동기화 파서만으로는 B 가 가짜 프레임(SOF 뒤 64바이트 대기) 안에 갇혀 나오지 않음을 확인하고, 바이트 타임아웃(또는 UART IDLE 인터럽트) 때 부르는 `fp_flush()` 로 B 를 꺼내라.

<details><summary>답</summary>

`fp_flush`: 상태가 `ST_SOF` 가 아니면 `raw[0..raw_n)` 을 복사하고 `ST_SOF` 로 돌린 뒤 같은 재동기화 루프로 다시 훑는다. 다시 훑다가 또 미완성으로 끝나면 한 번 더 — 매번 짧아지므로 끝난다. 실측: flush 전 A 만 받음(B 갇힘), flush 가 B 를 1개 전달, 이미 `ST_SOF` 면 0. 타임아웃 값은 "프레임 안 바이트 간 최대 간격" 보다 길고 "프레임 간 최소 간격" 보다 짧게 — 115200 에서 몇 바이트 시간(수백 µs~수 ms)이 보통이다. 타이머·IDLE 플래그와 연결하는 부분은 COMM-M10 / COMM-M11.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 16/16 passed
```

```text
practice 2 scenario 0: overflow=0 bytes (stream pos -1..-1), frames needed 500 got 500, total rx 500, crc_err=0 len_err=0
practice 2 scenario 1: overflow=372 bytes (stream pos 10128..10499), frames needed 490 got 490, total rx 490, crc_err=1 len_err=0
16/16 passed
```

## 10. 핵심 정리

- 프레임 형식은 숫자로: SOF 0xA5, LEN = PAYLOAD 만(0..64), CRC-16/CCITT-FALSE(0x1021, init FFFF, 반사 없음, xorout 0, check 0x29B1), 범위 LEN..PAYLOAD, 상위 바이트 먼저. "CRC-16" 이라는 이름만으로는 아무것도 정해지지 않는다.
- 인코더: 검사(LEN, 용량) 먼저, 쓰기 나중. 파서: 상태 6개, 바이트당 O(1), SOF 뒤 바이트는 전부 `raw[68]` 에.
- LEN 은 받자마자 범위 검사 — 안 하면 버퍼 오버플로. LEN 검사만으로 가짜 SOF 의 약 75% 가 1바이트 만에 걸러진다.
- 동기가 맞으면 데이터 속 0xA5 는 무해(스터핑 불필요). 동기를 잃으면 가짜 SOF 가 최대 68바이트를 삼켜 **다음 정상 프레임까지** 잃는다(잘림, 중간 시작, `A5 A5`).
- 재동기화 = 오류 시 가짜 SOF 다음 바이트부터 다시 훑기. 목록이 매번 짧아져 버퍼 68, 최악 2347 step/바이트, 평균 1~2. 실측 손실: 기본 파서 최대 59% → 재동기화는 거짓 수락으로 인한 몇 개뿐.
- 거짓 수락 ≈ 가짜 SOF × 65/256 × 2^-16 (실측 62 vs 이론 58.3). 줄이려면 CRC-32, 2바이트 sync, TYPE 별 길이 표.
- ISR 은 링 버퍼에 넣기만, 파서·디스패치는 메인 루프. 끝에 걸린 미완성 프레임은 타임아웃/IDLE 에서 flush.

## 참고자료

- Greg Cook, *Catalogue of parametrised CRC algorithms* (RevEng) — CRC-16/IBM-3740 (별칭 CRC-16/CCITT-FALSE): width 16, poly 0x1021, init 0xFFFF, check 0x29B1: <https://reveng.sourceforge.io/crc-catalogue/16.htm> (check 값은 이 레슨 코드로 재확인)
- R. N. Williams, "A Painless Guide to CRC Error Detection Algorithms" (1993) — 레지스터 모델, 잔여값(residue) 성질.
- P. Koopman, T. Chakravarty, "Cyclic Redundancy Code (CRC) Polynomial Selection for Embedded Networks", DSN 2004, 그리고 Koopman 의 CRC Zoo — 다항식별 HD 와 데이터 길이: <https://users.ece.cmu.edu/~koopman/crc/>
- MAVLink 직렬화 문서 (패킷 형식: 시작 바이트 0xFE(v1)/0xFD(v2), 길이, CRC-16/MCRF4XX + CRC_EXTRA, 시작 바이트 제외) — 세부는 기억에 의존, 원문 확인 권장: <https://mavlink.io/en/guide/serialization.html>
- u-blox, *u-blox M8 Receiver Description incl. Protocol Specification* — UBX 프레임(sync `B5 62`, class, id, 2바이트 LE 길이, 8비트 Fletcher CK_A/CK_B). ALGO-B14 참고.
- STMicroelectronics, STM32 USART 레퍼런스 매뉴얼 (예: RM0090 STM32F4) — RXNE, ORE(overrun), IDLE 플래그. 레지스터 비트 위치는 RM 확인 권장.
- 관련 레슨: COMM-B01 (UART 프레임), COMM-B02 (수신기·overrun), COMM-B08 (체크섬 vs CRC 검출력), COMM-B09 (HW CRC), COMM-B11 (SLIP), COMM-B12 (COBS), COMM-B13 (Modbus RTU), COMM-B14 (AT 명령 파서), COMM-M10 (견고한 파서: 타임아웃·복원율), COMM-M11 (DMA + IDLE), COMM-M14 (ARQ), ALGO-B14 (체크섬), ALGO-M01/M02 (CRC 원리·테이블), CPP-M13 (constexpr CRC LUT), C-M07 (테이블 상태 머신), C-M14 (ISR 공유 변수), DS-B04 (2의 거듭제곱 링 버퍼), DS-A02 (ISR↔태스크 링 버퍼·Bip buffer), TEST-M02 (파서 퍼징), HWLAB-M06 (로직 애널라이저 디코더)

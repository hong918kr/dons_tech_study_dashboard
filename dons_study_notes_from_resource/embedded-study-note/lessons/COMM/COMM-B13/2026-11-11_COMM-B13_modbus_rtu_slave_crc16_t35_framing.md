---
id: COMM-B13
time: 60분
updated: 2026-11-11
---
# COMM-B13 Modbus RTU 프레임: 함수 코드, CRC-16, 3.5문자 침묵

## 학습 목표

- Modbus RTU 가 **구분 바이트 없이 침묵 시간(silence)으로** 프레임을 자르는 규칙(t3.5 = 프레임 경계, t1.5 초과 = 불완전 프레임)을 정수 µs 로 정확히 구현하고, baud 별 타이머 값(19200 이하 = 문자 시간 × 1.5/3.5, 초과 = 고정 750/1750 µs)을 계산한다.
- CRC-16/MODBUS(반사형 0xA001, init 0xFFFF, **하위 바이트 먼저**)를 짜서 check 값 0x4B37 과 공개 예 `01 03 00 00 00 0A C5 CD` 로 확인하고, "CRC 포함 전체의 CRC = 0" 인 이유를 말한다.
- 슬레이브의 처리 순서(길이 → CRC → 주소/broadcast → FC → 예외)를 지키며 FC 0x03 / 0x06 과 예외 0x01/0x02/0x03 응답을 바이트 단위로 맞춘다. **CRC 오류와 broadcast 에는 답하지 않는** 이유를 설명한다.
- RS-485 반이중 버스(마스터 1 + 슬레이브 2) 모델에서 마스터 드라이버의 응답 검증·재시도를 짜고, DE 를 너무 일찍 내리는 버그와 USB-RS485 어댑터의 문자 간 틈(t1.5 위반)을 재현한다.

## 1. 문제

Modbus RTU 는 PLC·인버터·전력량계·온도 컨트롤러가 아직도 RS-485 위에서 쓰는 산업 표준이라, 펌웨어 면접에서 "RTU 프레임 끝을 어떻게 알죠?", "CRC 바이트 순서는?", "CRC 가 틀리면 예외로 답하나요?" 가 그대로 나오고, 보드 bring-up 에서는 "응답이 안 와요" 의 원인 절반이 타이밍·DE 제어·바이트 순서다.

COMM-B10~B12 의 프레임은 SOF·길이·END 같은 **바이트**로 경계를 표시했다. Modbus RTU 는 다르다: 프레임 안의 문자들은 쉬지 않고 붙여 보내고, 프레임 사이에는 **3.5 문자 시간 이상** 선을 조용히 둔다. 수신기는 "마지막 문자 뒤 t3.5 동안 아무것도 안 오면 프레임 끝" 으로 판단한다. 그래서 프레이머의 입력은 바이트만이 아니라 **타임스탬프가 붙은 바이트**다. 이 레슨에서 타임스탬프 = 그 문자의 stop bit 가 끝난 시각(수신 ISR 이 불리는 순간)이고, 문자 하나는 항상 11비트(8E1, 패리티 없으면 8N2 — COMM-B01 의 UART 프레임)다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">9600 baud 에서 FC03 트랜잭션 한 번 (solution.c 버스 모델의 실제 로그, 단위 µs)</text>
  <line x1="30" y1="72" x2="740" y2="72" stroke="currentColor" stroke-dasharray="2 3"/>
  <g stroke="currentColor">
    <rect x="85.4" y="62" width="126.5" height="20" class="f-acc"/>
    <rect x="267.2" y="62" width="395.4" height="20" class="f-ok"/>
  </g>
  <g stroke="currentColor" stroke-width="0.6">
    <line x1="101.2" y1="62" x2="101.2" y2="82"/><line x1="117.0" y1="62" x2="117.0" y2="82"/>
    <line x1="132.8" y1="62" x2="132.8" y2="82"/><line x1="148.6" y1="62" x2="148.6" y2="82"/>
    <line x1="164.4" y1="62" x2="164.4" y2="82"/><line x1="180.2" y1="62" x2="180.2" y2="82"/>
    <line x1="196.0" y1="62" x2="196.0" y2="82"/>
  </g>
  <text x="85" y="54" fill="currentColor" font-family="monospace" font-size="12">M: 01 03 00 00 00 0A C5 CD</text>
  <text x="300" y="54" fill="currentColor" font-family="monospace" font-size="12">S1: 01 03 14 5A 01 … 10 09 CB 7D  (25 B = 28650 µs)</text>
  <text x="95" y="100" fill="currentColor" font-size="12">8 B = 9168</text>
  <path d="M212 92 V100 H267 V92" fill="none" stroke="currentColor"/>
  <text x="214" y="114" fill="currentColor" font-size="12">t3.5=4011</text>
  <path d="M663 92 V100 H718 V92" fill="none" stroke="currentColor"/>
  <text x="664" y="114" fill="currentColor" font-size="12">t3.5</text>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <text x="30" y="134">0</text><text x="85" y="134">4011</text><text x="212" y="134">13179</text>
    <text x="267" y="134">17190</text><text x="663" y="134">45840</text><text x="718" y="134">49851</text>
  </g>
  <text x="270" y="150" fill="currentColor" font-size="12">↑ 슬레이브 판정·응답 시작</text>
  <text x="724" y="150" fill="currentColor" font-size="12" text-anchor="end">마스터 판정 ↑</text>
  <line x1="10" y1="160" x2="750" y2="160" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="178" fill="currentColor" font-weight="bold">확대: 첫 문자 0x01 (주소) — 11비트, 8E1, LSB 먼저</text>
  <g stroke="currentColor" stroke-dasharray="2 3" stroke-width="0.6">
    <line x1="60" y1="188" x2="60" y2="228"/><line x1="120" y1="188" x2="120" y2="228"/><line x1="180" y1="188" x2="180" y2="228"/>
    <line x1="240" y1="188" x2="240" y2="228"/><line x1="300" y1="188" x2="300" y2="228"/><line x1="360" y1="188" x2="360" y2="228"/>
    <line x1="420" y1="188" x2="420" y2="228"/><line x1="480" y1="188" x2="480" y2="228"/><line x1="540" y1="188" x2="540" y2="228"/>
    <line x1="600" y1="188" x2="600" y2="228"/><line x1="660" y1="188" x2="660" y2="228"/><line x1="720" y1="188" x2="720" y2="228"/>
  </g>
  <path d="M30 192 H60 V224 H120 V192 H180 V224 H600 V192 H740" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="32" y="186" fill="currentColor" font-size="12">idle 1</text>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <text x="90" y="244">start</text><text x="150" y="244">d0</text><text x="210" y="244">d1</text><text x="270" y="244">d2</text>
    <text x="330" y="244">d3</text><text x="390" y="244">d4</text><text x="450" y="244">d5</text><text x="510" y="244">d6</text>
    <text x="570" y="244">d7</text><text x="630" y="244">P</text><text x="690" y="244">stop</text>
  </g>
  <g font-size="12" fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="90" y="262">0</text><text x="150" y="262">1</text><text x="210" y="262">0</text><text x="270" y="262">0</text>
    <text x="330" y="262">0</text><text x="390" y="262">0</text><text x="450" y="262">0</text><text x="510" y="262">0</text>
    <text x="570" y="262">0</text><text x="630" y="262">1</text><text x="690" y="262">1</text>
  </g>
  <text x="10" y="290" fill="currentColor">0x01: 1 이 한 개 → 짝수 패리티 P = 1. 문자 = 11 × 104.17 µs = 1145.8 µs (8N2 도 11비트)</text>
  <text x="10" y="312" fill="currentColor">위 막대의 칸 하나가 이 11비트다. Modbus RTU 는 바이트 값이 아니라 "칸 사이의 빈 시간" 으로 프레임을 자른다.</text>
</svg>
```

위 그림의 숫자는 이 레슨 코드(`tc = round(11e6/9600) = 1146 µs`, `t3.5 = ⌈3.5 × 11e6/9600⌉ = 4011 µs`)가 실제로 찍은 로그다 (5. 풀이 Part C).

```svg
<svg viewBox="0 0 760 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">문자 사이 침묵 규칙 (9600 baud: t1.5 = 1719 µs, t3.5 = 4011 µs)</text>
  <g stroke="currentColor">
    <rect x="30" y="34" width="40" height="22" class="f-mut"/><rect x="72" y="34" width="40" height="22" class="f-mut"/>
    <rect x="142" y="34" width="40" height="22" class="f-mut"/><rect x="184" y="34" width="40" height="22" class="f-mut"/>
    <rect x="30" y="84" width="40" height="22" class="f-mut"/><rect x="72" y="84" width="40" height="22" class="f-mut"/>
    <rect x="212" y="84" width="40" height="22" class="f-warn"/><rect x="254" y="84" width="40" height="22" class="f-warn"/>
    <rect x="30" y="134" width="40" height="22" class="f-mut"/><rect x="72" y="134" width="40" height="22" class="f-mut"/>
    <rect x="302" y="134" width="40" height="22" class="f-acc"/><rect x="344" y="134" width="40" height="22" class="f-acc"/>
  </g>
  <text x="232" y="50" fill="currentColor" font-size="12">g ≤ t1.5</text>
  <text x="302" y="100" fill="currentColor" font-size="12">t1.5 &lt; g &lt; t3.5</text>
  <text x="392" y="150" fill="currentColor" font-size="12">g ≥ t3.5</text>
  <text x="470" y="50" fill="currentColor">같은 프레임 — 정상</text>
  <text x="470" y="100" fill="currentColor">프레임 불완전(NOK) — t3.5 뒤 통째로 버림</text>
  <text x="470" y="150" fill="currentColor">경계 — 앞 프레임 판정, 여기서 새 프레임</text>
  <g stroke="currentColor">
    <rect x="30" y="188" width="200.6" height="14" class="f-ok"/>
    <rect x="230.6" y="188" width="267.4" height="14" class="f-warn"/>
    <rect x="498" y="188" width="232" height="14" class="f-acc"/>
  </g>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <text x="30" y="220">0</text><text x="230" y="220">t1.5 = 1719</text><text x="498" y="220">t3.5 = 4011</text>
    <text x="700" y="220">침묵 (µs) →</text>
  </g>
  <text x="10" y="236" fill="currentColor" font-size="12">g (침묵) = 다음 문자의 start 시각 − 앞 문자의 stop 끝 시각 (타임스탬프 차이 − 문자 시간 1146)</text>
</svg>
```

### Part A — CRC-16/MODBUS, 타이머, 시간 기반 프레이머

- `uint16_t crc16_modbus(p, n)` — poly 0x8005 의 반사형 0xA001, init 0xFFFF, 출력 XOR 없음. 테스트: "123456789" → 0x4B37, `01 03 00 00 00 0A` → 0xCDC5 (와이어 `C5 CD`), `11 03 00 6B 00 03` → 0x8776, 빈 입력 → 0xFFFF, 그리고 **다른 길**(비반사 MSB-first 0x8005 + 비트 뒤집기)로 짠 기준 구현과 랜덤 2000개 대조.
- `rtu_timing(baud, &t)` — 문자 시간 `tc = round(11e6/baud)`, baud ≤ 19200 이면 `t1.5 = ⌈1.5·11e6/baud⌉`, `t3.5 = ⌈3.5·11e6/baud⌉`, 초과면 750 / 1750 µs. 1200·9600·19200·38400·115200 값 확인.
- 프레이머 `rtu_rx_byte(r, b, t_end)` (수신 ISR 이 부름) + `rtu_rx_poll(r, now)` (타이머가 부름). 상태 INIT → IDLE → RX. t3.5 침묵이면 프레임을 콜백으로 넘긴다. 테스트: 침묵이 **정확히 t3.5** 인 두 프레임은 둘로 나뉨, poll 을 t3.5 − 1 µs 에 부르면 아직 안 나옴, 타임스탬프가 2^32 µs 를 넘어 wrap 해도 같음.

### Part B — 슬레이브와 결함 주입

- `adu_add_crc`, `pdu_fc03`, `pdu_fc06`, `mb_slave_handle`. 레지스터 16개(0 ID, 1 FW 버전, 2 온도×10 은 읽기 전용; 3 설정값 0..1000; 4 모드 0..2; 5..15 범용).
- 예외: 지원 안 하는 FC → 01 (FC 256가지 전수), 개수 0 또는 126 → 03, 범위 밖 주소·읽기 전용 쓰기 → 02, 설정값 1001 → 03, PDU 길이 틀림 → 03. 응답 바이트를 정확히 비교 (`01 83 02 C0 F1` 등).
- 무응답이어야 하는 것: 1비트 오류 64가지 전부, 마지막 바이트 잘림, 4 바이트 미만, 남의 주소, broadcast(주소 0) — 단 broadcast FC06 은 **실행은 한다**.
- 프레이밍 결함: 두 프레임 사이 침묵 t3.5 − 1 µs (합쳐지고 t1.5 초과 → 폐기), 프레임 안 침묵 t1.5 (OK) vs t1.5 + 1 (폐기) vs 관대 모드, 프레임 한가운데 t3.5 침묵(두 조각 → 둘 다 CRC 로 걸러짐), **스트림 중간에 켜짐**(t3.5 침묵 전까지 버림), 256 B 초과.
- 프레이머 경유 랜덤 요청 3000개를 기준 모델(그림자 레지스터)과 바이트 단위로 비교.

### Part C — 마스터 드라이버와 RS-485 버스

- `mb_build_req` (8 B 요청), `mb_check_resp` (길이 → CRC → 주소 → 예외 → FC → byte count/에코), `drv_xfer` (타임아웃·CRC 오류만 최대 3회 재시도, broadcast 는 응답을 안 기다림).
- 버스 모델(주어짐): 마스터 1 + 슬레이브 2, 송신자 외 모두가 같은 바이트를 같은 시각에 받는다(반이중 2선식). 결함: 없는 장치(5번), 응답 비트 잡음 1회(재시도로 회복), **DE 를 TXE 에서 내림**(CRC 상위 바이트 유실 → 무응답), USB-RS485 어댑터가 요청 중간에 1 ms 쉼 @115200 (엄격 모드면 폐기, 관대 모드면 통과, 1750 µs 면 둘 다 실패).
- 랜덤 400 트랜잭션(슬레이브 1/2/없는 3, 읽기/쓰기, 10% 잡음)을 그림자 모델과 비교.

## 2. Problem (English)

Implement a Modbus RTU slave and a minimal master driver. The receive path is fed one character at a time with a timestamp (µs, the end of the character's stop bit). Characters are 11 bits on the wire.

```c
uint16_t crc16_modbus(const uint8_t *p, size_t n);          /* reflected 0xA001, init 0xFFFF, no final XOR */
void     rtu_timing(uint32_t baud, rtu_timing_t *t);        /* tc, t1.5, t3.5 in µs */
void     rtu_rx_byte(rtu_rx_t *r, uint8_t b, uint32_t t_end_us);   /* called from the RX ISR */
void     rtu_rx_poll(rtu_rx_t *r, uint32_t now_us);                 /* called from a timer tick */
size_t   mb_slave_handle(mb_slave_t *s, const uint8_t *adu, size_t n, uint8_t *rsp, size_t cap);
size_t   mb_build_req(uint8_t addr, uint8_t fc, uint16_t a, uint16_t b, uint8_t *out, size_t cap);
int      mb_check_resp(const uint8_t *req, const uint8_t *rsp, size_t n, uint16_t *regs, size_t regs_cap);
```

Rules: a silent interval ≥ t3.5 ends a frame; a silent interval > t1.5 between two characters of the same frame makes the frame invalid (discard it). For baud > 19200 use t1.5 = 750 µs, t3.5 = 1750 µs. After power-up, ignore characters until t3.5 of silence has been seen. Max ADU is 256 bytes. The CRC is sent low byte first. Support FC 0x03 (quantity 1..125) and FC 0x06; return exception 0x01 / 0x02 / 0x03 as `addr, fc|0x80, code, CRC`. Never reply to a frame with a bad CRC or to a broadcast (address 0). No dynamic allocation; every writer takes an output capacity.

Examples:

```text
request  01 03 00 00 00 0A C5 CD   (read 10 holding registers from 0x0000 of slave 1)
response 01 03 14 5A 01 01 02 00 FD 01 2C 00 01 10 05 10 06 10 07 10 08 10 09 CB 7D
request  01 03 00 0F 00 02 + CRC  -> 01 83 02 C0 F1      (16 registers: 15 + 2 is out of range)
request  01 10 ...                -> 01 90 01 8D C0      (function not supported)
request  01 06 00 03 00 64 78 21  -> 01 06 00 03 00 64 78 21   (echo)
9600 baud: tc = 1146 µs, t1.5 = 1719 µs, t3.5 = 4011 µs
```

Follow-ups: How does the slave know a frame ended? Why not reply with an exception when the CRC is wrong? What happens to t3.5 at 115200 baud, and why is it fixed there? How would you implement the timers on an STM32? Your master gets no reply — what do you check first?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열한 군데 (Part A 4개: `crc16_modbus`, `rtu_timing`, `rtu_rx_poll`, `rtu_rx_byte` / Part B 4개: `adu_add_crc`, `pdu_fc03`, `pdu_fc06`, `mb_slave_handle` / Part C 3개: `mb_build_req`, `mb_check_resp`, `drv_xfer`). 바이트 헬퍼 `get_be16/put_be16`(C-M05), `rtu_rx_init`, 버스 모델(`line_tx`, `bus_transact`), 기준 CRC, 하네스는 주어진다. 스텁은 0·타임아웃을 돌려줘서 시뮬레이터 루프가 멈추지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:321  crc16_modbus((const uint8_t *)"123456789", 9) == 0x4B37
…
crc16_modbus vs reference (reflect + MSB-first 0x8005): 2000 msgs, mismatches=2000
…
slave via framer: 3000 random requests (ok=0 exc=0), mismatches=3000
…
bus: 400 random transactions (0 frames on wire incl. retries, 0 recovered by retry), bad=279
FAIL starter.c:619  bad == 0 && retried > 0
3/69 passed
```

(exit 1. 통과한 3개는 스텁의 0/타임아웃이 우연히 정답과 같은 경우.)

## 4. 힌트

<details><summary>힌트 1 — CRC 는 "XOR 하고 8번 오른쪽으로"</summary>

반사형(reflected) CRC 는 LSB 부터 처리하므로 오른쪽 시프트다. `crc ^= byte;` 다음 8번: 맨 아래 비트가 1 이면 `crc = (crc >> 1) ^ 0xA001`, 아니면 `crc >>= 1`. 0xA001 은 0x8005 의 비트를 뒤집은 값이다. 결과의 **하위 바이트를 먼저** 보낸다 — Modbus 에서 little-endian 은 CRC 하나뿐이다.

</details>

<details><summary>힌트 2 — 침묵은 "타임스탬프 차이 − 문자 시간"</summary>

타임스탬프는 문자의 **끝**이다. 이번 문자의 시작 = `t_end − tc`. 침묵 = 이번 시작 − 앞 문자 끝. 쉬지 않고 붙여 보낸 문자는 침묵 0 이다. 타임스탬프 차이를 그대로 침묵으로 쓰면 1 문자만큼 길게 재게 된다 (7. 실수 3). 시간은 `uint32_t` 뺄셈으로만 비교하라 — 그래야 2^32 µs (약 71분) wrap 에 안전하다.

</details>

<details><summary>힌트 3 — 프레임 "닫기" 는 두 곳에서 일어난다</summary>

(1) 타이머 틱 `rtu_rx_poll(now)`: 마지막 문자 끝에서 t3.5 지났으면 닫는다. (2) 새 문자가 왔을 때: 그 문자의 시작 시각으로 먼저 `rtu_rx_poll` 을 불러, t3.5 이상 쉬었으면 앞 프레임을 닫고 나서 새 프레임을 시작한다. 이렇게 하면 타이머 틱을 놓쳐도 결과가 같다. INIT 상태에서 온 문자는 버리지만 `last_us` 는 갱신한다(침묵 타이머 재시작).

</details>

<details><summary>힌트 4 — 슬레이브의 검사 순서</summary>

길이 ≥ 4 (주소 + FC + CRC 2) → CRC → 주소(0 / 내 것 / 남의 것) → FC. **CRC 를 주소보다 먼저** 본다: CRC 가 틀리면 주소 바이트도 믿을 수 없다. FC03 의 예외 순서는 스펙 상태도대로 "개수 1..125 아니면 03 → 범위 밖이면 02". `start + qty` 는 `uint16_t` 변수에 담지 말 것 (7. 실수 5).

</details>

<details><summary>힌트 5 — 마스터는 무엇을 재시도하나</summary>

응답이 없거나(타임아웃) CRC 가 틀리면 선 위의 일시적 문제일 수 있으니 재시도. 예외 응답은 슬레이브가 **제대로 받고 거절한** 것이라 다시 보내도 같다 — 바로 반환. 주소·FC·길이가 안 맞는 응답은 프로토콜 오류로 보고한다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 범위 확인 | RTU 인가 ASCII 인가, baud·패리티(기본 8E1), 내 주소, 지원 FC, 레지스터 맵과 "40001 표기" 여부, 응답 타임아웃, t1.5 를 엄격히 볼 것인가 |
| 2. 계층 나누기 | 시간 프레이머(ISR + 타이머) → ADU 검사(길이·CRC·주소) → PDU 디스패치(FC) → 응답 조립(+CRC). 각 계층이 따로 테스트된다 |
| 3. 불변식 | 프레이머: "RX 상태의 buf 는 마지막 t3.5 침묵 이후 받은 문자들". 슬레이브: "CRC 가 맞은 프레임에만, unicast 에만 답한다" |
| 4. 코드 | 아래 Part A/B/C |
| 5. 테스트 | 공개 CRC 예, 경계값(침묵 = t1.5, t3.5, t3.5 − 1), 예외별 정확한 바이트, broadcast, 스트림 중간 시작, wrap |
| 6. 비용 | 바이트당 O(1) (CRC 8 시프트), RAM = 256 B 수신 버퍼 + 256 B 응답 버퍼 + 상태 몇 개. 동적 할당 없음 |

### Part A — CRC-16/MODBUS

RevEng 카탈로그의 CRC-16/MODBUS: width 16, poly 0x8005, init 0xFFFF, refin = refout = true, xorout 0x0000, check 0x4B37. 반사형은 레지스터를 오른쪽으로 밀며 LSB 부터 처리하므로 다항식도 뒤집은 0xA001 을 쓴다 (원리는 ALGO-M01, 테이블 가속은 ALGO-M02).

첫 바이트 0x01 의 비트 단위 트레이스:

| 단계 | LSB | 연산 | crc |
|---|---|---|---|
| init ^ 0x01 | | | FFFE |
| 1 | 0 | >> 1 | 7FFF |
| 2 | 1 | >> 1 ^ A001 | 9FFE |
| 3 | 0 | >> 1 | 4FFF |
| 4 | 1 | >> 1 ^ A001 | 87FE |
| 5 | 0 | >> 1 | 43FF |
| 6 | 1 | >> 1 ^ A001 | 81FE |
| 7 | 0 | >> 1 | 40FF |
| 8 | 1 | >> 1 ^ A001 | 807E |

요청 `01 03 00 00 00 0A` 바이트별 crc: 807E → 2140 → F020 → D8F1 → 8419 → **CDC5**. 와이어에는 하위 `C5` 다음 상위 `CD`. 같은 계산을 CRC 바이트까지 이어 가면 `C5` 뒤 00CD, `CD` 뒤 **0000** 이 된다 — 반사형이고 xorout 이 0 이면 "CRC 를 하위 바이트부터 붙인 프레임 전체의 CRC = 0" 이다 (연습 1). 그래서 수신 측은 `crc16_modbus(frame, n) == 0` 한 줄로 검사할 수도 있다. 이 코드는 읽기 쉽게 "앞 n−2 바이트의 CRC == 받은 CRC" 로 비교했다.

### Part A — 타이머 값

스펙(Modbus over Serial Line V1.02)의 규칙을 정수로:

- 문자 시간 tc = 11 비트 / baud. 9600 → 1145.83 µs → `round` 1146.
- baud ≤ 19200: t1.5 = 1.5 tc, t3.5 = 3.5 tc. 정수는 **올림** — "적어도 3.5 문자" 를 지키는 쪽으로 반올림한다 (`(38500000 + baud − 1) / baud`).
- baud > 19200: 고정 t1.5 = 750 µs, t3.5 = 1750 µs (스펙의 권장값 — 원문 확인 권장).

`demo_timing.c` 실제 출력:

| baud | tc (정확 / 정수) | t1.5 (정확 / 정수) | t3.5 (정확 / 정수) | t1.5 = 몇 문자 | t3.5 = 몇 문자 |
|---|---|---|---|---|---|
| 1200 | 9166.67 / 9167 | 13750.00 / 13750 | 32083.33 / 32084 | 1.50 | 3.50 |
| 9600 | 1145.83 / 1146 | 1718.75 / 1719 | 4010.42 / 4011 | 1.50 | 3.50 |
| 19200 | 572.92 / 573 | 859.38 / 860 | 2005.21 / 2006 | 1.50 | 3.50 |
| 38400 | 286.46 / 286 | 750 / 750 | 1750 / 1750 | 2.62 | 6.11 |
| 57600 | 190.97 / 191 | 750 / 750 | 1750 / 1750 | 3.93 | 9.16 |
| 115200 | 95.49 / 95 | 750 / 750 | 1750 / 1750 | 7.85 | 18.33 |

19200 을 넘으면 문자 수로는 **더 관대해진다**(115200 에서 t3.5 = 18 문자). 고정값이 있는 이유는 8. Q3.

### Part A — 프레이머 상태 머신

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b13a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">rtu_rx_byte (문자마다) + rtu_rx_poll (타이머) 상태 머신</text>
  <g stroke="currentColor">
    <rect x="30" y="110" width="130" height="50" rx="10" class="f-mut"/>
    <rect x="310" y="110" width="130" height="50" rx="10" class="f-ok"/>
    <rect x="590" y="110" width="140" height="50" rx="10" class="f-acc"/>
  </g>
  <text x="72" y="140" fill="currentColor" font-weight="bold">INIT</text>
  <text x="355" y="140" fill="currentColor" font-weight="bold">IDLE</text>
  <text x="615" y="134" fill="currentColor" font-weight="bold">RX</text>
  <text x="615" y="152" fill="currentColor" font-size="12">why = T15 | OVF</text>
  <path d="M60 110 C 40 60, 130 60, 110 110" fill="none" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="20" y="52" fill="currentColor" font-size="12">문자: 버림, 타이머 재시작</text>
  <line x1="160" y1="135" x2="308" y2="135" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="180" y="127" fill="currentColor" font-size="12">침묵 ≥ t3.5</text>
  <path d="M440 122 C 500 80, 540 80, 588 122" fill="none" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="455" y="78" fill="currentColor" font-size="12">문자 → len = 0, buf[0]</text>
  <path d="M620 110 C 600 55, 710 55, 690 110" fill="none" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="560" y="44" fill="currentColor" font-size="12">문자: 침묵 &gt; t1.5 → T15</text>
  <text x="560" y="60" fill="currentColor" font-size="12">len = 256 → OVF, 아니면 저장</text>
  <path d="M588 150 C 540 200, 500 200, 440 150" fill="none" stroke="currentColor" marker-end="url(#b13a)"/>
  <text x="430" y="210" fill="currentColor" font-size="12">침묵 ≥ t3.5 → why ≠ 0 이면 폐기(카운트)</text>
  <text x="430" y="226" fill="currentColor" font-size="12">아니면 cb(buf, len, last + t3.5)</text>
  <text x="10" y="254" fill="currentColor" font-size="12">"침묵 ≥ t3.5" 는 poll(now) 에서, 또는 새 문자가 왔을 때 그 문자의 start 시각으로 먼저 poll 해서 판정한다.</text>
</svg>
```

```c
void rtu_rx_byte(rtu_rx_t *r, uint8_t b, uint32_t t_end_us) {
    uint32_t t_start = t_end_us - r->tm.tc_us;            /* 이 문자의 start bit 시각 */
    uint32_t silence = since(t_start, r->last_us);        /* 앞 문자 끝 → 이 문자 시작 */
    rtu_rx_poll(r, t_start);                              /* t3.5 가 지났으면 앞 프레임부터 닫는다 */
    if (r->state == RTU_INIT) r->n_init_drop++;
    else {
        if (r->state == RTU_IDLE) { r->state = RTU_RX; r->len = 0; r->why = 0; }
        else if (r->strict_t15 && silence > r->tm.t15_us) r->why |= RTU_BAD_T15;
        if (r->len < RTU_MAX_ADU) r->buf[r->len++] = b; else r->why |= RTU_BAD_OVF;
    }
    r->last_us = t_end_us;
}
```

**경계값 규칙.** 스펙 문장 그대로: 프레임 경계는 "적어도 3.5 문자" (≥), 불완전 프레임은 "1.5 문자를 넘는" 침묵 (>). 테스트가 양쪽 경계를 1 µs 단위로 확인한다: 침묵 4011 → 둘로 나뉨, 4010 → 합쳐지고 1719 를 넘었으니 폐기, 1719 → 정상, 1720 → 폐기.

**트레이스 — 스트림 중간에 켜짐 (9600, init 시각 0).** 슬레이브가 리셋에서 깨어났을 때 버스에서는 이미 다른 프레임이 흐르고 있을 수 있다. 스펙의 초기 상태 규칙(t3.5 침묵을 볼 때까지 아무것도 받지 않음)이 없으면 꼬리 바이트를 프레임 머리로 착각한다.

| 입력 | 문자 끝 t | 침묵 | 상태 전 → 후 | 동작 |
|---|---|---|---|---|
| 앞 프레임 꼬리 5 B (1000 부터) | 2146 … 6730 | 1000, 0, … | INIT → INIT | 버림 ×5 |
| REQ 8 B (2000 쉬고) | 9876 … 17898 | 2000 < 4011 | INIT → INIT | 버림 ×8 |
| REQ 8 B (4011 쉬고) | 23055 … 31077 | 4011 ≥ t3.5 | INIT → IDLE → RX | 저장 |
| poll(35088) | | 4011 | RX → IDLE | 콜백: 8 B, t = 35088 |

결과 `n_init_drop = 13`, 프레임 1개 — 테스트 그대로다.

### Part B — 슬레이브

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">ADU = [주소][PDU = FC + 데이터][CRC lo][CRC hi] — 숫자 필드는 BE, CRC 만 LE</text>
  <g stroke="currentColor">
    <rect x="30" y="34" width="66" height="26" class="f-acc"/><rect x="98" y="34" width="66" height="26" class="f-ok"/>
    <rect x="166" y="34" width="66" height="26" class="f-mut"/><rect x="234" y="34" width="66" height="26" class="f-mut"/>
    <rect x="302" y="34" width="66" height="26" class="f-mut"/><rect x="370" y="34" width="66" height="26" class="f-mut"/>
    <rect x="438" y="34" width="66" height="26" class="f-warn"/><rect x="506" y="34" width="66" height="26" class="f-warn"/>
  </g>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <text x="63" y="52">01</text><text x="131" y="52">03</text><text x="199" y="52">00</text><text x="267" y="52">00</text>
    <text x="335" y="52">00</text><text x="403" y="52">0A</text><text x="471" y="52">C5</text><text x="539" y="52">CD</text>
  </g>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <text x="63" y="76">주소 1</text><text x="131" y="76">FC 03</text><text x="233" y="76">start = 0x0000</text>
    <text x="369" y="76">qty = 0x000A (10)</text><text x="505" y="76">CRC 0xCDC5, lo 먼저</text>
  </g>
  <text x="590" y="52" fill="currentColor" font-size="12">요청 8 B</text>
  <g stroke="currentColor">
    <rect x="30" y="100" width="56" height="26" class="f-acc"/><rect x="88" y="100" width="56" height="26" class="f-ok"/>
    <rect x="146" y="100" width="56" height="26" class="f-mut"/><rect x="204" y="100" width="72" height="26" class="f-mut"/>
    <rect x="278" y="100" width="72" height="26" class="f-mut"/><rect x="352" y="100" width="72" height="26" class="f-mut"/>
    <rect x="426" y="100" width="40" height="26" class="f-mut" stroke-dasharray="4 3"/><rect x="468" y="100" width="72" height="26" class="f-mut"/>
    <rect x="542" y="100" width="56" height="26" class="f-warn"/><rect x="600" y="100" width="56" height="26" class="f-warn"/>
  </g>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <text x="58" y="118">01</text><text x="116" y="118">03</text><text x="174" y="118">14</text><text x="240" y="118">5A 01</text>
    <text x="314" y="118">01 02</text><text x="388" y="118">00 FD</text><text x="446" y="118">…</text><text x="504" y="118">10 09</text>
    <text x="570" y="118">CB</text><text x="628" y="118">7D</text>
  </g>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <text x="58" y="142">주소</text><text x="116" y="142">FC</text><text x="174" y="142">20 B</text><text x="240" y="142">reg0 ID</text>
    <text x="314" y="142">reg1 FW</text><text x="388" y="142">reg2 25.3°C</text><text x="504" y="142">reg9</text><text x="599" y="142">CRC</text>
  </g>
  <text x="664" y="118" fill="currentColor" font-size="12">응답 25 B</text>
  <g stroke="currentColor">
    <rect x="30" y="166" width="56" height="26" class="f-acc"/><rect x="88" y="166" width="56" height="26" class="f-warn"/>
    <rect x="146" y="166" width="56" height="26" class="f-mut"/><rect x="204" y="166" width="56" height="26" class="f-warn"/>
    <rect x="262" y="166" width="56" height="26" class="f-warn"/>
  </g>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <text x="58" y="184">01</text><text x="116" y="184">83</text><text x="174" y="184">02</text><text x="232" y="184">C0</text><text x="290" y="184">F1</text>
  </g>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <text x="58" y="208">주소</text><text x="116" y="208">03 | 0x80</text><text x="174" y="208">예외 02</text><text x="261" y="208">CRC</text>
  </g>
  <text x="340" y="184" fill="currentColor" font-size="12">예외 응답 5 B (start 15 + qty 2 &gt; 16)</text>
  <text x="10" y="236" fill="currentColor" font-size="12">ADU ≤ 256 B = 주소 1 + PDU ≤ 253 + CRC 2. FC03 최대 125개 → 응답 PDU 1 + 1 + 250 = 252 B 로 들어간다.</text>
  <text x="10" y="256" fill="currentColor" font-size="12">비트 오류가 주소 바이트에 나면 다른 슬레이브가 "내 것" 으로 볼 수 있다 → 그래서 CRC 를 주소보다 먼저 본다.</text>
</svg>
```

**처리 순서와 그 이유.**

| 단계 | 실패 시 | 이유 |
|---|---|---|
| 길이 ≥ 4 | 무응답, `n_short` | 주소 + FC + CRC 2 가 최소. 이보다 짧으면 CRC 계산 대상이 없다 |
| CRC | 무응답, `n_crc` | 깨진 프레임의 주소·FC 를 믿을 수 없다. 엉뚱한 슬레이브가 답하면 버스 충돌. 마스터는 타임아웃으로 안다 |
| 주소 | 남의 것 → 무응답 `n_other` | 버스의 모든 노드가 모든 프레임을 듣는다 (다른 슬레이브의 **응답**도) |
| broadcast (0) | FC06 은 실행, 그 외 무시. 항상 무응답 | 모든 슬레이브가 동시에 답하면 충돌. 스펙: broadcast 는 쓰기 명령만 |
| FC | 03·06 외 → 예외 01 | 받은 것은 확실하니 이유를 알려 준다 |
| FC03 | 길이 ≠ 5 → 03, qty ∉ 1..125 → 03, start + qty > 16 → 02 | 스펙 상태도의 순서: 개수 먼저, 주소 다음 |
| FC06 | 길이 ≠ 5 → 03, 주소 밖/읽기 전용 → 02, 값 범위 → 03 | 읽기 전용·값 범위는 이 mock 의 정책 (실제 장치는 02/03/04 등 제각각 — 매뉴얼 확인) |

**예외 코드.** Modbus Application Protocol 의 정의: 01 Illegal Function, 02 Illegal Data Address, 03 Illegal Data Value, 04 Server Device Failure (그 밖에 05·06·08·0A·0B). 응답은 `FC | 0x80` + 코드 1 바이트. 이 레슨은 01~03 만 만든다.

**FC03 응답 만들기.** `byte count = 2·qty` (1 바이트, 최대 250), 값은 레지스터마다 **big-endian**(`put_be16`). 길이를 `uint32_t` 로 비교하는 이유는 7. 실수 5.

**FC06 은 에코.** 정상 응답 = 요청과 바이트까지 같다 (`01 06 00 03 00 64 78 21`). 마스터는 `memcmp` 한 번으로 검증한다.

**랜덤 시험.** 프레이머를 거친 랜덤 요청 3000개(FC03 qty 0..19 · start 0..19, FC06 주소 0..19 · 값 0..1199)에서 실제 결과는 정상 1376 · 예외 1624, 기준 모델과 불일치 0.

### Part C — 마스터 드라이버와 버스

`mb_check_resp` 의 순서: 길이 0 → 타임아웃, < 4 → 길이 오류, CRC, **주소가 요청과 같은가**(다른 슬레이브의 응답을 내 것으로 받으면 안 된다), `FC | 0x80` 이면 예외 코드 반환, FC 일치, FC03 은 `n == 5 + 2·qty` 이고 byte count == 2·qty, FC06 은 완전 에코.

`drv_xfer` 의 재시도 정책: 타임아웃·CRC 오류만 최대 3번. FC03 과 FC06 은 **멱등(idempotent)** 이라 — 같은 값을 다시 써도 결과가 같다 — 응답만 깨진 경우의 재시도가 안전하다. 테스트 "잡음 1회" 에서 응답 비트가 깨지면 트랜잭션이 정확히 2번 일어나고 값이 맞는다.

**버스 로그** (solution.c 실제 출력, 9600, 슬레이브 1 에서 10개 읽기):

```text
bus log @9600 (tc=1146 t1.5=1719 t3.5=4011 us):
  t=   4011 us  M   -> 01 03 00 00 00 0A C5 CD  (8 B)
  t=  17190 us  S1  frame 8 B detected (t3.5) -> reply
  t=  17190 us  S2  frame 8 B detected (t3.5) -> no reply
  t=  17190 us  S1 -> 01 03 14 5A 01 01 02 00 FD 01 2C 00 01 10 05 10 06 10 07 10 08 10 09 CB 7D  (25 B)
  t=  49851 us  M   frame 25 B detected (t3.5)
  t=  49851 us  S2  frame 25 B detected (t3.5) -> no reply
```

읽는 법: 요청 끝 = 4011 + 8·1146 = 13179, 그 뒤 t3.5 = 17190 에 두 슬레이브가 동시에 판정. S2 는 주소가 달라 무시, S1 은 바로 응답(turnaround 0). 응답 끝 45840 + t3.5 = 49851 에 마스터 판정. **S2 는 S1 의 응답도 프레임으로 받는다** — 테스트가 `n_other == 2` 로 확인한다. 2선식 RS-485 에서는 응답도 모든 노드에게 들리기 때문이다.

**결함 주입 결과 (테스트가 확인하는 것).**

| 결함 | 선 위에서 생기는 일 | 결과 |
|---|---|---|
| 없는 장치 5번 | 아무도 답 안 함 | 3번 시도 후 `MB_E_TIMEOUT` |
| 범위 밖 읽기 | 슬레이브가 예외 02 | 1번만 시도, 반환 2 |
| 응답 비트 1개 잡음 | 마스터 CRC 오류 | 재시도 1번 → `MB_OK` (트랜잭션 2) |
| DE 를 TXE 에서 내림 | 요청 마지막 바이트(CRC 상위 `CD`)가 선에 안 나감 | 슬레이브 `n_crc` +3, `MB_E_TIMEOUT` |
| 요청 4번째 바이트 앞 1000 µs 틈 @115200 | 750 &lt; 1000 &lt; 1750 | 엄격: `n_t15` +3, 타임아웃 / 관대: 정상 |
| 같은 틈이 1750 µs | 프레임 경계 → 4 B + 4 B | 관대해도 두 조각 모두 CRC 오류 (`n_crc` 6) |
| broadcast FC06 | 두 슬레이브 모두 실행, 무응답 | `MB_OK` (응답을 기다리지 않음) |

**DE 버그를 자세히.** RS-485 트랜시버는 DE(driver enable) 핀이 켜져 있어야 선을 구동한다. UART 의 TXE(송신 데이터 레지스터 빔) 인터럽트는 마지막 바이트가 **시프트 레지스터로 옮겨진** 순간이지 선에서 다 나간 순간이 아니다. 거기서 DE 를 내리면 마지막 문자가 잘린다 — 마지막 바이트는 늘 CRC 상위 바이트라 슬레이브는 CRC 오류로 조용히 버리고, 마스터는 "응답 없음" 만 본다. TC(transmission complete) 에서 내려야 한다. STM32 의 USART 는 하드웨어 DE 출력(DEM)과 assert/deassert 시간 설정을 가진 계열이 많다 (비트 이름·유무는 해당 칩 RM 확인 권장).

**USB-RS485 어댑터.** PC 쪽 마스터는 OS·USB 스케줄링 때문에 문자 사이에 수백 µs~ms 틈을 넣기 쉽다. 115200 에서 t1.5 = 750 µs 라 1 ms 틈은 스펙상 불완전 프레임이다. 그래서 실무 슬레이브 상당수가 t1.5 를 검사하지 않고 t3.5 만 본다(이 코드의 `strict_t15 = false`). 대가: 정말 잘린 프레임도 CRC 에 맡기게 된다 — CRC-16 이 걸러 주므로 보통은 괜찮다는 판단이다.

**처리량** (`demo_timing.c` 실제 출력 일부, turnaround 0, 사이클 = 요청 8 B + t3.5 + 응답 (5 + 2N) B + t3.5):

| baud | N=1: 사이클 / 초당 / 침묵 비율 | N=10 | N=125 |
|---|---|---|---|
| 9600 | 25.21 ms / 39.7 / 31.8% | 45.83 ms / 21.8 / 17.5% | 309.38 ms / 3.2 / 2.6% |
| 19200 | 12.60 ms / 79.3 / 31.8% | 22.92 ms / 43.6 / 17.5% | 154.69 ms / 6.5 / 2.6% |
| 115200 | 4.93 ms / 202.7 / 71.0% | 6.65 ms / 150.4 / 52.6% | 28.61 ms / 34.9 / 12.2% |

9600·N=10 의 45.83 ms 는 버스 모델에서 실제로 잰 45840 µs 와 같다 (테스트 `bus.now == 45840`). 115200 에서 짧은 폴링은 시간의 70% 가 침묵이다 — 레지스터를 **한 번에 많이** 읽는 것이 처리량을 올리는 가장 쉬운 방법이다. 슬레이브 turnaround 가 5 ms 면 9600·N=10 은 21.8 → 19.7 회/초로 준다 (demo 출력).

### 복잡도와 메모리

| 부분 | 시간 | 메모리 |
|---|---|---|
| `crc16_modbus` | O(n), 바이트당 시프트 8번 | O(1) (테이블판은 512 B ROM — ALGO-M02) |
| `rtu_rx_byte` / `rtu_rx_poll` | O(1) (닫을 때 콜백) | 256 B 버퍼 + 상태 ~32 B |
| `mb_slave_handle` | O(n) (CRC) + O(qty) | 스택 253 B (응답 PDU) |
| `mb_check_resp` | O(n) | O(1) |

ISR 에는 `rtu_rx_byte` 만 둔다 (O(1), 나눗셈 없음). CRC 검사와 FC 처리는 프레임 판정 후 메인 루프나 낮은 우선순위에서 해도 된다 — 응답은 어차피 t3.5 뒤에야 나갈 수 있다. 판정된 프레임을 메인 루프로 넘길 때는 버퍼를 두 개 두거나(핑퐁) "처리 중" 플래그로 다음 프레임 수신을 막는다 (공유 데이터 규칙 C-M14).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 69/69 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_timing.c -o demo_timing && ./demo_timing   # 타이머 표 + 처리량 표
```

## 7. 자주 하는 실수

**1. CRC 를 상위 바이트 먼저 보낸다.** 레지스터 값이 전부 big-endian 이라 CRC 도 `put_be16` 으로 넣기 쉽다. 그러면 거의 모든 프레임이 상대에게 CRC 오류다 — 연습 1 의 실측: 랜덤 2000 프레임에서 CRC 바이트를 바꾸면 2000개 중 **4개**(하위 == 상위 인 경우)만 통과. "가끔 된다" 가 더 헷갈리게 만든다. 고친 코드: `buf[n] = c & 0xFF; buf[n+1] = c >> 8;`.

**2. CRC 오류에 예외로 답한다.** 깨진 프레임의 주소로 답하면, 주소 바이트가 깨진 경우 **다른 슬레이브인 척** 답하게 되고 진짜 슬레이브의 응답과 충돌할 수 있다. CRC 오류는 무응답 + 카운터. 마스터는 타임아웃으로 재시도한다. broadcast 도 마찬가지로 예외조차 보내지 않는다.

**3. 타임스탬프 차이를 침묵으로 쓴다.** RX 인터럽트 시각의 차이 Δ 는 침묵 + 문자 시간이다. `Δ > t1.5` 로 검사하면 실제 침묵이 0.5 문자(9600 에서 573 µs)만 넘어도 프레임을 버리고, `Δ ≥ t3.5` 로 자르면 침묵 2.5 문자에서 프레임을 끊는다. 고친 코드: `silence = (t_end − tc) − last_end`. 같은 함정이 타이머 구현에도 있다: RXNE(문자 끝)에서 t1.5 타이머를 걸면 다음 문자의 RXNE 는 침묵 + 1 문자 뒤에 오므로, 침묵 0.5 문자만 넘어도 타이머가 먼저 만료된다 — RXNE 기준이면 t1.5 + tc (= 2.5 문자)로 걸어야 한다. t3.5 도 같은 이유로 RXNE 기준 그대로 걸면 실효 경계가 **침묵 2.5 문자**가 된다. 정상 트래픽(프레임 안 틈 ≤ 1.5 문자)은 끊기지 않아서 많은 구현이 이 근사를 쓰지만, 그렇다면 그렇다고 알고 쓰자.

**4. UART IDLE 인터럽트를 t3.5 로 쓴다.** 많은 MCU 의 IDLE line 감지는 **약 1 문자** 동안 선이 조용하면 걸린다(칩마다 정의 확인). 스펙상 같은 프레임인 1.2 문자 틈에서도 프레임이 끊기고, 반대로 프레임 사이가 정확히 3.5 문자인지는 확인하지 못한다. IDLE 은 "DMA 버퍼를 비울 때" 신호로 쓰고(COMM-M11), 프레임 경계 판정은 t3.5 타이머나 수신 타임아웃(STM32 USART 의 receiver timeout 기능 등 — RM 확인)으로 한다.

**5. `start + qty` 를 16비트에 담는다.** `uint16_t end = start + qty; if (end > NREGS) …` 는 start = 0xFFFF, qty = 2 에서 end = 1 로 wrap 해 검사를 통과하고 `reg[0xFFFF]` 를 읽는다(버퍼 밖 읽기 — 원격에서 메모리를 덤프당하는 버그). 고친 코드: `(uint32_t)start + qty > MB_NREGS`. 이 레슨의 랜덤 시험은 start 를 0..19 로만 뽑으므로 이 버그를 못 잡는다 — 그래서 코드 리뷰 항목으로 외워 둔다.

**6. 문서의 "40001" 을 그대로 주소로 쓴다.** 장치 매뉴얼의 "Holding Register 40001" 은 데이터 모델 표기이고, 프레임에 들어가는 PDU 주소는 **0x0000** 이다 (4x 영역의 1번 = 주소 0). 1 차이로 옆 레지스터를 읽으면 값은 그럴듯해서 오래 못 찾는다. 매뉴얼마다 0-based / 1-based 표기가 섞여 있으니 ID 레지스터처럼 값이 알려진 곳을 먼저 읽어 확인한다.

**7. DE 를 TXE 에서 내린다.** Part C 그대로. 증상은 "슬레이브 CRC 오류 카운터만 오르고 응답 없음". 스코프로 A/B 선을 보면 마지막 문자가 중간에 잘려 있다. TC 인터럽트(또는 하드웨어 DE)로 고친다.

**8. 응답 주소를 검사하지 않는다.** 앞 요청에 늦게 온 응답이나 다른 슬레이브의 응답을 지금 요청의 답으로 받으면 엉뚱한 레지스터 값이 들어간다. 주소·FC·byte count 를 모두 요청과 맞춰 본다 (`MB_E_ADDR`, `MB_E_FC`, `MB_E_LEN` 테스트).

## 8. 면접 Q&A

<details><summary>Q1. RTU 는 왜 SOF 나 길이 필드 없이 시간으로 프레임을 자르나요? 단점은?</summary>

1979년 Modicon 시절의 단순한 하드웨어에서는 "문자 사이 타이머" 가 가장 싼 경계 검출이었고, 모든 바이트 값(0x00~0xFF)을 데이터로 쓸 수 있어 바이트 스터핑(COMM-B11/B12)이 필요 없다. ASCII 모드는 반대로 `:` 와 CR LF 로 경계를 표시하는 대신 바이트당 2 문자를 쓴다(연습 3). 단점: (1) 송신 측이 문자를 **끊김 없이** 내보내야 한다 — OS·USB 어댑터·느린 ISR 이 틈을 만들면 깨진다. (2) 수신 측에 µs 단위 타이머가 필요하다. (3) 프레임 사이에 최소 3.5 문자를 낭비한다(115200 에서 짧은 폴링은 시간의 70% 가 침묵 — demo). (4) 바이트 스트림만 남는 경로(TCP 터널, 버퍼링되는 무선 모뎀)에서는 경계 정보가 사라진다 — 그래서 Modbus TCP 는 MBAP 길이 필드를 쓴다(연습 4).

</details>

<details><summary>Q2. CRC 가 틀린 요청에는 왜 예외 응답을 안 보내나요? 꼬리: 마스터는 그럼 어떻게 아나요?</summary>

CRC 가 틀리면 프레임의 어느 바이트도 믿을 수 없다 — 주소가 깨졌다면 내 프레임이 아닐 수 있고, 내가 답하면 진짜 대상 슬레이브와 동시에 선을 구동해 충돌한다. 스펙도 "CRC 오류 프레임은 버린다" 이고 예외는 "제대로 받았지만 처리할 수 없는" 경우만이다. 마스터는 **응답 타임아웃**으로 안다: 타임아웃 → 재시도 → 그래도 실패면 통신 오류 보고. 타임아웃 값은 스펙이 정하지 않으니 최악 turnaround + 응답 길이 + 여유로 정한다(수십~수백 ms 가 흔한 설정 — 장치 매뉴얼 확인). broadcast 뒤에 마스터가 기다리는 turnaround delay 도 같은 성격의 설정값이다(스펙 예시는 100~200 ms 대 — 원문 확인 권장).

</details>

<details><summary>Q3. 19200 baud 를 넘으면 t3.5 를 왜 1750 µs 로 고정하나요?</summary>

115200 에서 정직한 t3.5 = 3.5 × 95.5 = 334 µs, t1.5 = 143 µs 다. 문자마다 µs 단위 타이머를 다시 거는 비용, ISR 지연(다른 인터럽트에 밀리는 시간), 송신 측의 문자 간 틈이 이 크기와 비슷해져 오판이 늘어난다. 고정값 750/1750 µs 는 이 부담을 줄이려는 스펙의 권장값이다(문자 수로는 18 문자 — 더 관대). 대가: 고속에서도 프레임 사이 최소 1.75 ms 를 쉬어야 해서 처리량이 침묵에 묶인다(demo: 115200·N=1 이 초당 203회에서 멈춤). 면접에선 "타이머 해상도와 인터럽트 부하 vs 처리량의 타협" 이라고 답하면 된다.

</details>

<details><summary>Q4. STM32 에서 t1.5 / t3.5 를 어떻게 구현하나요? ISR 과 메인 루프는 어떻게 나누죠?</summary>

방법 1 (어디서나): RXNE ISR 에서 바이트를 버퍼에 넣고 하드웨어 타이머(one-shot)를 다시 건다(t1.5 를 볼 거면 비교 채널 하나 더). RXNE 는 문자 **끝**이라 다음 RXNE 는 침묵 + 1 문자 뒤에 온다 — 정확히 재려면 t1.5 + tc, t3.5 + tc 로 걸고(판정이 1 문자 늦어짐), t3.5 만 걸면 실효 경계가 2.5 문자인 근사가 된다(7. 실수 3). 타이머 만료 ISR 이 "프레임 완료" 플래그를 세우고 메인 루프가 CRC·FC 처리 후 응답을 보낸다. 방법 2: USART 의 수신 타임아웃(receiver timeout, 비트 단위 값 — 11 × 3.5 ≈ 39 비트) 기능이 있는 계열이면 하드웨어가 침묵을 재 준다. DMA 로 받고 타임아웃 인터럽트에서 길이를 읽으면 바이트당 인터럽트도 없다(COMM-M11 의 IDLE + DMA 와 같은 구조). 기능 유무·레지스터 이름은 칩 RM 확인 권장. 어느 쪽이든 ISR 은 O(1) 로 짧게: 바이트 저장 + 타이머 재시작. CRC 와 레지스터 접근은 메인 루프. 응답 송신은 DMA + TC 에서 DE 해제.

</details>

<details><summary>Q5. 마스터가 응답을 못 받습니다. 무엇부터 보나요?</summary>

아래에서 위로. (1) 물리: A/B 선 바뀜(제조사마다 A/B 표기가 반대인 경우가 있다), 120 Ω 종단이 양 끝에만 있는지, idle 상태를 정하는 바이어스(fail-safe) 저항, GND 공유. (2) 스코프/로직 애널라이저로 요청이 선에 **끝까지** 나가는지 — 마지막 문자가 잘리면 DE 타이밍(실수 7). 슬레이브의 응답 파형이 있는데 마스터가 못 받으면 마스터의 RE/DE 전환 지연. (3) UART 설정: baud, **8E1 vs 8N1**(스펙 기본은 짝수 패리티, 패리티 없음이면 stop 2 — 한쪽만 8N1 이면 문자 길이가 10 비트로 달라 깨진다). (4) 프레임: 슬레이브 주소, CRC 바이트 순서(실수 1), 40001 오프셋(실수 6). (5) 타이밍: PC 어댑터의 문자 간 틈(t1.5), 마스터가 슬레이브의 turnaround 보다 짧은 타임아웃을 쓰는지. 슬레이브 쪽 카운터(`n_crc`, `n_t15`, `n_other`)가 있으면 이 순서가 몇 분으로 줄어든다 — 진단 카운터를 처음부터 넣는 이유.

</details>

<details><summary>Q6. CRC-16/MODBUS 는 정확히 무엇이고, 왜 수신 측에서 전체 CRC 가 0 이 되나요?</summary>

width 16, poly 0x8005, init 0xFFFF, refin/refout true, xorout 0 (check "123456789" = 0x4B37 — RevEng). 같은 다항식의 CRC-16/ARC 는 init 0 이라 값이 다르다 — 이름이 비슷한 CRC 가 많으니 **파라미터 5개**로 말해야 한다. 0 이 되는 이유: 반사형 CRC 레지스터에서 "레지스터 값을 하위 바이트부터 데이터로 밀어 넣으면" 레지스터가 자기 자신을 XOR 해 0 이 된다(xorout 이 0 일 때). 이 레슨의 트레이스에서 C5 뒤 00CD, CD 뒤 0000. 그래서 수신 측은 CRC 바이트를 떼지 않고 전체를 한 번에 돌려도 된다 — DMA 로 받은 버퍼를 그대로 하드웨어 CRC 유닛에 넣을 때 편하다(COMM-B09). 검출력: 16비트 이하 버스트 전부 검출, 1비트 오류 전부(이 레슨 테스트: 64가지 전수) — 일반론은 COMM-B08.

</details>

<details><summary>Q7. 쓰기를 재시도해도 안전한가요?</summary>

FC06/FC16 처럼 "이 값으로 설정" 하는 명령은 멱등이라, 요청은 갔는데 응답만 깨진 경우 같은 쓰기를 다시 해도 결과가 같다(이 레슨의 잡음 시험). 위험한 경우는 장치가 쓰기에 **부수 효과**를 거는 레지스터 — "명령 레지스터에 1 을 쓰면 카운터 +1 / 밸브 한 스텝" 같은 설계. 재시도가 동작을 두 번 실행한다. 대책: 장치 쪽에 시퀀스 번호 레지스터를 두거나, 마스터가 재시도 전에 상태를 읽어 확인하거나, 그런 레지스터는 재시도 금지로 표시. 프로토콜 수준의 중복 제거는 COMM-M14(stop-and-wait ARQ).

</details>

<details><summary>Q8. broadcast 로 쓴 값이 잘 들어갔는지 마스터는 어떻게 아나요?</summary>

모른다 — 응답이 없으니까. 그래서 broadcast 는 "모든 장치 동기 시각 설정" 처럼 실패해도 다음 주기에 다시 보내면 되는 명령에만 쓰고, 중요한 값은 unicast 로 쓰거나 broadcast 뒤에 각 장치를 읽어 확인한다. 마스터는 broadcast 뒤에 슬레이브들이 처리할 시간(turnaround delay)을 기다린 후 다음 요청을 보낸다. 슬레이브 구현에서 흔한 버그는 broadcast 를 처리하다 **예외 응답을 보내 버리는 것** — 이 레슨의 `mb_slave_handle` 은 디스패치 뒤에 `a == 0` 이면 무조건 0 을 반환한다.

</details>

## 9. 추가 연습

### 연습 1 — CRC 잔여값 검사

`bool adu_crc_ok(f, n)` 를 `crc16_modbus(f, n) == 0` 한 줄로 짜고, 랜덤 프레임 2000개에서 항상 참임을 보여라. 그리고 CRC 두 바이트를 바꿔 보낸(상위 먼저) 프레임이 몇 개나 통과하는지 세라.

<details><summary>답 보기</summary>

`return n >= 4 && crc16_modbus(f, n) == 0;`. 실측: 2000개 모두 통과, 바이트를 바꾼 프레임은 **4개** 통과 — 정확히 하위 바이트 == 상위 바이트 였던 4개(그때는 바꿔도 같은 프레임). 확률로 약 1/256 이니 2000개 중 8개 정도가 기대값이고, 4개는 그 범위 안이다. 실수 1 의 수치 근거.

</details>

### 연습 2 — FC 0x10 Write Multiple Registers

PDU `[10][start 2][qty 2][byte count 1][값 2·qty]`, qty 1..123, byte count == 2·qty, PDU 길이 == 6 + byte count (아니면 03), 범위 밖 02. 성공 응답 PDU `[10][start][qty]`. 검사가 모두 끝난 뒤에만 쓴다(원자성).

<details><summary>답 보기</summary>

```c
if (pn < 6) return -3;
uint16_t start = get_be16(pdu + 1), qty = get_be16(pdu + 3);
uint8_t bc = pdu[5];
if (qty < 1 || qty > 123 || bc != 2u * qty || pn != 6u + bc) return -3;
if ((uint32_t)start + qty > NREGS) return -2;
for (unsigned i = 0; i < qty; i++) regs[start + i] = get_be16(pdu + 6 + 2 * i);
memcpy(out, pdu, 5); return 5;
```

123 인 이유: 요청 PDU = 1 + 2 + 2 + 1 + 2·qty ≤ 253 → qty ≤ 123.5. 범위 밖 요청에서 `regs[14]` 가 그대로인지 테스트한다 — 앞쪽 일부만 쓰고 예외를 내면 장치 상태가 반쯤 바뀐다. 응답 예 `11 10 00 01 00 02` 의 CRC 는 `12 98` (practice.c 출력, 널리 쓰이는 예제 값과 같다).

</details>

### 연습 3 — Modbus ASCII 프레임

`:` + 바이트마다 대문자 16진 2 글자 + LRC + CR LF. LRC = 주소부터 데이터까지 바이트 합의 2의 보수(ALGO-B14 의 `sum8_twos`). 인코더와, 형식·LRC 를 검사하는 디코더를 짜라. `01 03 00 00 00 0A` 는?

<details><summary>답 보기</summary>

합 = 0x0E → LRC = 0x100 − 0x0E = 0xF2 → `":01030000000AF2\r\n"` (17 글자). 디코더는 LRC 까지 모두 더해 0 인지 본다(체크섬 바이트 포함 합 = 0 — Intel HEX 와 같은 방식). 이 연습은 소문자를 거부하는 정책. 길이 2..253 랜덤 1000개 왕복. ASCII 는 문자 사이 최대 1 초까지 틈을 허용하는(스펙 기본값 — 원문 확인) 대신 프레임이 두 배로 길고, 검출력도 LRC(8비트 합)라 RTU 의 CRC-16 보다 약하다.

</details>

### 연습 4 — Modbus TCP ↔ RTU 게이트웨이

TCP 쪽 ADU = MBAP `[transaction id 2][protocol id 2 = 0][length 2 = 1 + PDU][unit id 1]` + PDU, CRC 없음. `tcp_to_rtu` (unit id → RTU 주소, CRC 붙이기), `rtu_to_tcp` (CRC 검사 후 떼고 같은 transaction id 로 감싸기), 슬레이브 무응답이면 예외 0x0B 를 만드는 `gw_exception`.

<details><summary>답 보기</summary>

`00 01 00 00 00 06 01 03 00 00 00 0A` → `01 03 00 00 00 0A C5 CD`. 응답 25 B → TCP 29 B, length 필드 = 23 (= 25 − CRC 2). 검사: protocol id ≠ 0 거부, length ≠ 실제 길이 거부, 출력 용량 부족 거부, 응답 CRC 오류면 TCP 로 넘기지 않음. 슬레이브 무응답: `00 07 00 00 00 03 05 83 0B` (0x0B = Gateway Target Device Failed to Respond; 0x0A = Gateway Path Unavailable). 게이트웨이는 TCP 쪽에서 여러 요청을 받아도 RS-485 쪽은 한 번에 하나라 큐가 필요하고, RTU 쪽 경계는 여전히 t3.5 로 자른다 — 이 레슨의 프레이머를 그대로 쓴다.

</details>

### 연습 5 — 32비트 값과 워드 순서

float·uint32 는 레지스터 두 개에 나눠 담는다. 레지스터 안은 big-endian 이지만 **어느 레지스터가 상위 워드인지는 표준이 없다** (ABCD vs CDAB). `u32_to_regs(v, r, word_swap)`, `regs_to_float(r, word_swap)` 을 짜고 25.3f 로 확인하라.

<details><summary>답 보기</summary>

25.3f = 0x41CA6666. ABCD → `[0x41CA, 0x6666]`, CDAB → `[0x6666, 0x41CA]`. CDAB 로 저장된 값을 ABCD 로 읽으면 0x666641CA = 2.71839e+23 (practice.c 출력) — 터무니없는 값이라 금방 보이지만, uint32 카운터는 그럴듯한 큰 수가 나와 놓치기 쉽다. float 비트 해석은 포인터 캐스팅 대신 `memcpy` (strict aliasing — C-M13). 실무: 장치 매뉴얼의 "word order / swap" 설정을 확인하고, 값을 아는 레지스터로 검증한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 27/27 passed
```

## 10. 핵심 정리

- RTU 프레임 = `[주소][FC][데이터][CRC lo][CRC hi]`, ≤ 256 B. 경계는 바이트가 아니라 **침묵**: ≥ t3.5 = 경계, 프레임 안 > t1.5 = 불완전(폐기). 침묵 = 다음 문자 start − 앞 문자 끝.
- 문자 = 11비트 (8E1 기본, 패리티 없으면 8N2). 9600: tc 1146, t1.5 1719, t3.5 4011 µs. 19200 초과는 고정 750 / 1750 µs.
- 켜진 직후는 t3.5 침묵을 볼 때까지 버린다 (스트림 중간 시작 대비). 시간 비교는 `uint32_t` 뺄셈 (wrap 안전).
- CRC-16/MODBUS: 0xA001 반사, init 0xFFFF, xorout 0, check 0x4B37, **하위 바이트 먼저**. `01 03 00 00 00 0A` → `C5 CD`. CRC 포함 전체의 CRC = 0.
- 슬레이브 순서: 길이 → CRC → 주소/broadcast → FC → 예외. CRC 오류·남의 주소·broadcast 에는 **절대 답하지 않는다**. 예외 = `FC | 0x80` + 01/02/03.
- FC03: qty 1..125 (03) → 범위 (02), 값은 BE. FC06: 응답 = 요청 에코. `start + qty` 는 32비트로.
- 마스터: 주소·FC·길이까지 검증, 타임아웃·CRC 만 재시도 (멱등 쓰기라 안전). RS-485 DE 는 TC 에서 내린다 (TXE 면 CRC 상위 바이트가 잘린다).

## 참고자료

- Modbus Organization, *MODBUS over Serial Line Specification and Implementation Guide V1.02* (2006) — RTU 문자 11비트, 짝수 패리티 기본, t1.5/t3.5 와 19200 초과 고정값(750 µs / 1.75 ms), RTU 수신 상태도(초기 상태 포함), 주소 0 = broadcast·1..247, CRC 하위 바이트 먼저, 2선식 RS-485·바이어스·종단: <https://modbus.org/docs/Modbus_over_serial_line_V1_02.pdf> (수치는 기억과 HWLAB-M05 에 의존 — 원문 확인 권장)
- Modbus Organization, *MODBUS Application Protocol Specification V1.1b3* — FC 0x03 (qty 1..125) · 0x06 · 0x10 (qty 1..123) 정의와 예외 상태도, 예외 코드 01~0B: <https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf>
- Modbus Organization, *MODBUS Messaging on TCP/IP Implementation Guide V1.0b* — MBAP 헤더, unit id, 게이트웨이 (연습 4): <https://modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf>
- CRC RevEng catalogue, CRC-16/MODBUS (poly 0x8005, init 0xFFFF, refin/refout, check 0x4B37): <https://reveng.sourceforge.io/crc-catalogue/16.htm> — 이 레슨의 check 값과 `C5 CD`, `76 87`, `C0 F1`, `8D C0`, `78 21`, `CB 7D`, `12 98` 은 모두 직접 계산(코드 2가지 + Python)해 확인했다.
- Simply Modbus (예제 프레임 `11 03 00 6B 00 03 76 87`, `11 10 00 01 00 02 12 98`): <https://www.simplymodbus.ca/FAQ.htm>
- Texas Instruments, *The RS-485 Design Guide* (SLLA272) — 종단, fail-safe 바이어스, DE 제어 (문서 번호는 기억에 의존 — 확인 권장).
- ST, 해당 STM32 계열 Reference Manual 의 USART 장 — receiver timeout, IDLE line, 하드웨어 DE(driver enable) 출력과 assert/deassert 시간 (계열마다 유무·비트 이름이 다름 — RM 확인 권장).
- 관련 레슨: COMM-B01 (UART 프레임, 패리티), COMM-B02 (16x 오버샘플링 수신기), COMM-B03 (baud 오차), COMM-B08 (CRC vs 체크섬 검출 실험), COMM-B09 (하드웨어 CRC 유닛), COMM-B10 (길이 prefix + CRC 프레임), COMM-B11 / COMM-B12 (SLIP / COBS), COMM-B14 (AT 커맨드 파서), COMM-M10 (재동기화 파서), COMM-M11 (DMA + IDLE 수신), COMM-M14 (ARQ 재시도), HWLAB-M05 연습 4 (LA 로 t1.5/t3.5 판정), ALGO-B14 (2의 보수 체크섬 = LRC), ALGO-M01 / ALGO-M02 (CRC 원리 / 테이블), C-M05 (엔디언 독립 직렬화), C-M13 (memcpy 타입 펀닝), C-M14 (ISR 공유 데이터), BM-B12 / BM-M04 (실제 UART 드라이버)

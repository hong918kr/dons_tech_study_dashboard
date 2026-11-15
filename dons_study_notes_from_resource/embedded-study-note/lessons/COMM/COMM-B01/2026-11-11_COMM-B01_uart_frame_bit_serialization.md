---
id: COMM-B01
time: 60분
updated: 2026-11-11
---
# COMM-B01 UART 프레임: start/data/parity/stop 비트 직렬화

## 학습 목표

- UART 한 문자가 선로 위에서 어떤 비트열이 되는지 손으로 그린다: idle = 1, start = 0, data **LSB first**, (parity), stop = 1. `'A'` = 0x41 8N1 → `0,1,0,0,0,0,0,1,0,1`.
- 설정(data 5~9비트 × parity N/E/O × stop 1/2 = 30가지)마다 프레임 길이와 효율(8N1 = 80%, 8E1 = 72.7%)을 계산하고, 송신기(문자 → 비트 배열)와 수신기(비트 배열 → 문자 + 오류 플래그)를 짠다. 30가지 설정 × 모든 값을 전수 왕복 검사한다.
- 수신 오류를 **서로 다른 플래그**(start / PE / FE)로 구분하고, 프레임의 비트 하나를 뒤집는 결함 주입 67424번으로 "어느 자리가 틀리면 어떤 플래그가 켜지는가"를 증명한다. 패리티 없는 설정에서 data 비트 오류는 **조용히** 틀린 문자가 된다는 것도.
- 비트 시간마다 샘플 하나를 받는 수신 상태 기계(`rx_step`)로 스트림을 받고, 설정 불일치(7E1 → 8N1 등)가 터미널에 어떻게 보이는지 표로 외운다 — bring-up 에서 "글자가 깨져요" 를 1분 안에 진단하는 지식.

## 1. 문제

UART 는 보드 bring-up 에서 가장 먼저 살리는 인터페이스(부트 로그, 디버그 콘솔, RF 모듈 AT 명령)이고, 면접에서는 "8N1 에서 'A' 를 그려 보세요 → 왜 LSB 부터? → 설정이 틀리면 화면에 뭐가 보이죠?" 로 바로 이어진다.

UART(Universal Asynchronous Receiver/Transmitter)는 **비동기(asynchronous)** 직렬 통신이다. 클럭 선이 없으므로 송신·수신이 미리 같은 baud(초당 비트 수)와 같은 프레임 형식(frame format)을 약속해야 한다. 선은 쉴 때 1(idle high)이고, 문자 하나는 이렇게 나간다.

- **start 비트** 1개 = 0. idle 1 → 0 으로 떨어지는 에지(falling edge)가 "문자 시작" 신호다. 수신기는 이 에지에 맞춰 비트 시간을 센다.
- **data 비트** 5~9개, **LSB(d0) 먼저**. 그래서 0x41 = 0100 0001 이 선로에서는 1000 0010 순서로 보인다.
- **parity 비트** 0개 또는 1개(선택). 짝수(even) 패리티면 data + parity 의 1 개수가 짝수, 홀수(odd)면 홀수가 되게 붙인다.
- **stop 비트** 1개 또는 2개 = 1. 선을 다시 1 로 돌려놓아 다음 start 에지가 생길 수 있게 한다.

설정은 보통 `8N1`(data 8, parity None, stop 1)처럼 쓴다. `7E1` 은 data 7 + Even + stop 1, `8O2` 는 data 8 + Odd + stop 2.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">'A' = 0x41 을 8N1 로 보낼 때 TX 선로 (idle high, LSB first)</text>
  <text x="8" y="64" fill="currentColor" font-size="12">1</text><text x="8" y="114" fill="currentColor" font-size="12">0</text>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity="0.45"><line x1="110" y1="34" x2="110" y2="140"/><line x1="164" y1="34" x2="164" y2="140"/><line x1="218" y1="34" x2="218" y2="140"/><line x1="272" y1="34" x2="272" y2="140"/><line x1="326" y1="34" x2="326" y2="140"/><line x1="380" y1="34" x2="380" y2="140"/><line x1="434" y1="34" x2="434" y2="140"/><line x1="488" y1="34" x2="488" y2="140"/><line x1="542" y1="34" x2="542" y2="140"/><line x1="596" y1="34" x2="596" y2="140"/><line x1="650" y1="34" x2="650" y2="140"/></g>
  <rect x="110" y="60" width="54" height="50" class="f-warn" opacity="0.5"/>
  <rect x="596" y="60" width="54" height="50" class="f-ok" opacity="0.5"/>
  <path d="M30 60 H110 V110 H164 V60 H218 V110 H272 H326 H380 H434 H488 V60 H542 V110 H596 V60 H650 H720" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <text x="133" y="48" fill="currentColor" font-family="monospace" font-weight="bold">0</text><circle cx="137" cy="110" r="4.5" class="f-acc" stroke="currentColor"/><text x="119" y="134" fill="currentColor" font-size="12">start</text>
  <text x="187" y="48" fill="currentColor" font-family="monospace" font-weight="bold">1</text><circle cx="191" cy="60" r="4.5" class="f-acc" stroke="currentColor"/><text x="184" y="134" fill="currentColor" font-size="12">d0</text>
  <text x="241" y="48" fill="currentColor" font-family="monospace" font-weight="bold">0</text><circle cx="245" cy="110" r="4.5" class="f-acc" stroke="currentColor"/><text x="238" y="134" fill="currentColor" font-size="12">d1</text>
  <text x="295" y="48" fill="currentColor" font-family="monospace" font-weight="bold">0</text><circle cx="299" cy="110" r="4.5" class="f-acc" stroke="currentColor"/><text x="292" y="134" fill="currentColor" font-size="12">d2</text>
  <text x="349" y="48" fill="currentColor" font-family="monospace" font-weight="bold">0</text><circle cx="353" cy="110" r="4.5" class="f-acc" stroke="currentColor"/><text x="346" y="134" fill="currentColor" font-size="12">d3</text>
  <text x="403" y="48" fill="currentColor" font-family="monospace" font-weight="bold">0</text><circle cx="407" cy="110" r="4.5" class="f-acc" stroke="currentColor"/><text x="400" y="134" fill="currentColor" font-size="12">d4</text>
  <text x="457" y="48" fill="currentColor" font-family="monospace" font-weight="bold">0</text><circle cx="461" cy="110" r="4.5" class="f-acc" stroke="currentColor"/><text x="454" y="134" fill="currentColor" font-size="12">d5</text>
  <text x="511" y="48" fill="currentColor" font-family="monospace" font-weight="bold">1</text><circle cx="515" cy="60" r="4.5" class="f-acc" stroke="currentColor"/><text x="508" y="134" fill="currentColor" font-size="12">d6</text>
  <text x="565" y="48" fill="currentColor" font-family="monospace" font-weight="bold">0</text><circle cx="569" cy="110" r="4.5" class="f-acc" stroke="currentColor"/><text x="562" y="134" fill="currentColor" font-size="12">d7</text>
  <text x="619" y="48" fill="currentColor" font-family="monospace" font-weight="bold">1</text><circle cx="623" cy="60" r="4.5" class="f-acc" stroke="currentColor"/><text x="609" y="134" fill="currentColor" font-size="12">stop</text>
  <text x="44" y="134" fill="currentColor" font-size="12">idle</text><text x="668" y="134" fill="currentColor" font-size="12">idle</text>
  <path d="M164 148 V154 H596 V148" fill="none" stroke="currentColor"/>
  <text x="184" y="172" fill="currentColor">data 8비트: d0(LSB) 가 먼저 → 0x41 = 0100 0001 이 선로에서는 1000 0010 순서</text>
  <text x="10" y="198" fill="currentColor">● = 비트 중앙 샘플. 이 레슨 모델에서는 배열 원소 하나 = 비트 시간 하나 = 샘플 하나</text>
  <text x="10" y="218" fill="currentColor">uart_encode(0x41, 8N1) → <tspan font-family="monospace">0,1,0,0,0,0,0,1,0,1</tspan>  (start 0 … stop 1)</text>
  <text x="10" y="238" fill="currentColor">115200 baud: 1비트 = 8.68 µs, 프레임 10비트 = 86.8 µs → 최대 11520 문자/s, 효율 8/10 = 80%</text>
</svg>
```

이 레슨의 모델은 오버샘플링(oversampling) 없이 **배열 원소 하나 = 비트 시간 하나** 다. 즉 수신기가 비트 중앙에서 이미 정확히 한 번씩 샘플했다고 가정한다(로직 애널라이저 캡처를 비트 단위로 잘라 놓은 것과 같다). 실제 USART 의 16배 오버샘플링·글리치 거부·overrun 은 COMM-B02, baud 오차 허용 한계는 COMM-B03 에서 다룬다.

### Part A — 한 프레임 만들기와 풀기

- `uart_frame_bits(cfg)` — 1 + data + (parity ? 1 : 0) + stop. 설정이 범위 밖이면 `UART_E_CFG`. `uart_eff_permille(cfg)` — 효율 = data / 프레임, 천분율 반올림 (8N1 → 800).
- `uart_parity_bit(data, nbits, parity)` — 하위 nbits 의 1 개수로 EVEN/ODD 패리티 비트. NONE 이면 0.
- `uart_encode(data, cfg, bits, cap)` — 비트 배열(값 0/1)로. 검사 순서: 설정 → 값이 data_bits 에 들어가는가(`UART_E_DATA`) → 버퍼 크기(`UART_E_CAP`). 오류면 **아무것도 쓰지 않는다**.
- `uart_decode(bits, n, cfg, &out)` — 음수 = API 오류(`UART_E_CFG`, 프레임보다 짧음 `UART_E_SHORT`, 0/1 아닌 값 `UART_E_LEVEL`), 0 이상 = 수신 플래그(Part B). 플래그가 켜져도 `*out` 에는 data 자리 값을 넣는다(상태 레지스터의 PE/FE 와 데이터 레지스터가 따로인 것처럼).
- 테스트: 'A' 를 8N1/7E1/8E1/8O2 로 정확한 비트열과 비교, 'C' 7E1, 0x55, 그리고 **30가지 설정 × 2^data 값 전부**(5952 프레임) 왕복 + 패리티 성질 확인.

### Part B — 오류 검출과 결함 주입

- `uart_check(bits, cfg, data)` — start 자리가 1 이면 `UART_F_START`(프레임이 아님), 패리티 불일치 `UART_F_PARITY`(PE), stop 자리 중 하나라도 0 이면 `UART_F_FRAME`(FE, framing error). 여러 개가 **동시에** 켜질 수 있다(OR).
- `uart_field_at(cfg, pos, &index)` — 프레임 안 위치 → START / DATA(d0..) / PARITY / STOP. 로직 애널라이저 화면을 읽을 때 머릿속에 있는 지도다.
- 결함 주입 전수: 30 설정 × 모든 값 × 프레임의 모든 비트 위치 하나씩 뒤집기. 기대 플래그를 `uart_field_at` 으로 예측해 디코더 결과와 대조한다. 그 밖에 API 오류 5종, PE+FE 동시, 2비트 data 오류(패리티가 놓침 — ALGO-B13), break(전부 0), idle(전부 1).

### Part C — 스트림: 송신 타임라인, 비트당 1샘플 수신기, 설정 불일치

- `uart_tx_stream(msg, n, cfg, lead, gap, bits, cap)` — idle `lead` 비트 + 문자마다 [프레임][idle `gap` 비트]. cap 밖에는 절대 쓰지 않는다.
- `uart_rx_step(rx, level, &data, &flags)` — 비트 시간마다 한 번 불리는 상태 기계(타이머 ISR 로 도는 소프트웨어 UART 수신기의 모양). `uart_rx_run(...)` — 타임라인 전체를 흘리고 통계(frames, ok, PE, FE, overflow, partial)를 낸다. **정책: PE/FE 문자는 버리고 센다.**
- 테스트: 6가지 설정 × 간격 0/1/3 왕복, 송신·수신 버퍼 경계, 잘린 캡처, break 뒤 복구, 설정 불일치 4종(7E1→8N1, 8N1→7E1, 8E1→8N1, 8N1↔8N2), 그리고 스트림 **중간에서 듣기 시작**해도 한 문자 시간 idle 뒤의 문자는 반드시 받는다.

## 2. Problem (English)

You are writing the framing layer of a UART for a firmware test bench. Represent the line as an array of bit-times, one element per bit, each 0 or 1. Idle is 1, start bit is 0, data goes LSB first, optional even/odd parity, then 1 or 2 stop bits (= 1).

```c
typedef struct { uint8_t data_bits; uint8_t parity; uint8_t stop_bits; } uart_cfg_t; /* 5..9, N/E/O, 1..2 */

int  uart_frame_bits(const uart_cfg_t *c);                    /* total bits per char, or UART_E_CFG */
int  uart_eff_permille(const uart_cfg_t *c);                  /* data/frame x 1000, rounded */
unsigned uart_parity_bit(uint16_t data, unsigned nbits, unsigned parity);
int  uart_encode(uint16_t data, const uart_cfg_t *c, uint8_t *bits, size_t cap);   /* bits written or <0 */
int  uart_decode(const uint8_t *bits, size_t n, const uart_cfg_t *c, uint16_t *out); /* <0 API error, else flags */
int  uart_field_at(const uart_cfg_t *c, int pos, int *index);  /* START / DATA / PARITY / STOP / NONE */
int  uart_tx_stream(const uint8_t *msg, size_t n, const uart_cfg_t *c,
                    unsigned lead, unsigned gap, uint8_t *bits, size_t cap);
int  uart_rx_step(uart_rx_t *rx, uint8_t level, uint16_t *data, int *flags); /* 1 when a char completes */
```

Constraints: no dynamic allocation; every function that writes takes a capacity and never writes past it; errors are distinct codes (config, capacity, data out of range, bad level, short input) and receive flags are a bitmask (start, parity, framing) so several can be reported at once. The receiver accepts a start bit only on a 1→0 transition.

Examples:

```text
encode 0x41 'A' 8N1 -> 0,1,0,0,0,0,0,1,0,1           (10 bits, efficiency 80%)
encode 0x41 'A' 8E1 -> 0,1,0,0,0,0,0,1,0,0,1         (two 1s -> even parity bit 0)
encode 0x41 'A' 8O2 -> 0,1,0,0,0,0,0,1,0,1,1,1
encode 0x43 'C' 7E1 -> 0,1,1,0,0,0,0,1,1,1
decode 8N1 frame with stop = 0              -> flags = FRAMING, out = data bits as received
decode 8E1 frame with parity and stop wrong -> flags = PARITY | FRAMING
send "Hello" 7E1, receive 8N1               -> no errors, but chars with an odd number of 1s get bit 7 set
```

Follow-ups: Why idle high and why LSB first? What does the terminal show if one side is 7E1 and the other 8N1? Which single-bit errors can a UART receiver detect? If you start listening in the middle of a back-to-back stream, how long an idle gap guarantees resynchronisation?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데다 — Part A 5개(`uart_frame_bits`, `uart_eff_permille`, `uart_parity_bit`, `uart_encode`, `uart_decode`), Part B 2개(`uart_check`, `uart_field_at`), Part C 3개(`uart_tx_stream`, `uart_rx_step`, `uart_rx_run`). 설정 검사 `uart_cfg_ok`, 수신기 초기화 `uart_rx_init`, 하네스는 주어진다. 스텁은 0(또는 `FLD_NONE`)을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:179  uart_frame_bits(&C8N1) == 10
…
FAIL starter.c:197  uart_encode(0x41, &C8N1, b, sizeof b) == 10 && bits_eq(b, "0100000101")
…
  first round-trip mismatch: cfg 5N1 value 0x0 r=0 f=0 out=0xFFFF
round trip: 30 configs, 5952 frames, bad=5952
…
1-bit inject: 0 trials, bad=0, silent wrong chars=0 (all in parity=N data bits)
…
8N1 -> 7E1 rx: frames=0 ok=0 PE=0 FE=0
…
mid-stream start: 0 offsets, tail lost at 0, offsets with junk/errors before the gap: 0
4/67 passed
```

(exit 1. 통과한 4개는 스텁의 0 이 우연히 정답인 경우 — 'A' 의 짝수 패리티 = 0, NONE 패리티 = 0, 범위 밖 위치 = `FLD_NONE`, 중간 시작 테스트의 "실패 0개"(오프셋이 0개라서; 바로 위 줄의 길이 CHECK 가 대신 FAIL).)

## 4. 힌트

<details><summary>힌트 1 — 프레임은 네 칸짜리 표다</summary>

위치 0 = start, 1..d = data(d0..d_{d-1}), d+1 = parity(있으면), 나머지 = stop. 길이 L = 1 + d + P + S. 인코더·디코더·`uart_field_at` 이 모두 이 표 하나로 나온다. 최대 길이는 1 + 9 + 1 + 2 = **13** (12 가 아니다 — 7. 실수 1).

</details>

<details><summary>힌트 2 — LSB first 는 "i 번째 data 비트 = (data >> i) & 1"</summary>

보낼 때 `bits[1 + i] = (data >> i) & 1`, 받을 때 `d |= bits[1 + i] << i`. 비트를 뒤집는 함수는 필요 없다. 거꾸로 `>> (7 - i)` 로 짜면 MSB first(SPI 기본, COMM-B04)가 되어 'A' 가 0x82 로 읽힌다.

</details>

<details><summary>힌트 3 — 패리티 비트 = "1 개수를 짝/홀로 맞추는 값"</summary>

ones = data 의 1 개수. EVEN: 비트 = ones & 1 (ones 가 홀수면 1 을 더해 짝수로). ODD: 비트 = !(ones & 1). 헷갈리면 'A'(1 이 2개)로 확인: EVEN 0, ODD 1. 계산은 반드시 하위 nbits 만 — 7비트 설정에서 bit7 까지 세면 안 된다.

</details>

<details><summary>힌트 4 — 디코더는 "읽고, 검사는 전부, 플래그는 OR"</summary>

길이·레벨 같은 API 오류는 먼저 음수로 끝낸다. 그다음 data 를 모아 `*out` 에 넣고, start / parity / 모든 stop 을 **하나도 건너뛰지 않고** 검사해 플래그를 OR 한다. 첫 오류에서 `return` 하면 PE+FE 동시 테스트가 깨진다.

</details>

<details><summary>힌트 5 — 수신 상태 기계는 세 상태</summary>

WAIT_HIGH(선이 1 이 되길 기다림) → IDLE(1 이 이어짐) → 0 을 보면 FRAME(buf[0] = 0, n = 1) → 비트를 모아 n == L 이면 `uart_decode(buf)`, 1 반환. 다음 상태는 마지막 stop 이 1 이면 IDLE, 0 이면 WAIT_HIGH. 이 마지막 규칙을 빼면 break 하나가 FE 프레임 여러 개가 된다(7. 실수 5).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "선은 idle 1, start 0 의 하강 에지로 동기, data LSB first, parity 선택, stop 1. 8N1 이면 10비트라 효율 80%, 115200 baud 에서 11520 문자/s."
2. 'A' 를 그린다: `0 | 1 0 0 0 0 0 1 0 | 1`. 비트 중앙에 샘플 점.
3. 수신 쪽: start 에지 → 비트 중앙에서 샘플(실제 칩은 16배 오버샘플링, COMM-B02) → parity·stop 검사 → PE/FE 플래그 + 데이터 레지스터 → ISR 이 링 버퍼로(DS-B03, BM-M04).
4. 오류: start 자리가 1 = 가짜 start, parity 불일치 = PE, stop 이 0 = FE(또는 break). 패리티는 1비트 오류만, 2비트는 못 본다 → 프레임 단위 CRC 가 필요(COMM-B08, COMM-B10).
5. 디버깅: 설정 불일치 표(아래)로 "글자가 반쯤 깨진다 = 7E1 vs 8N1" 같은 진단.

### Part A — 프레임 규칙과 레이아웃

| 위치 | 필드 | 값 | 이유 |
|---|---|---|---|
| (프레임 앞) | idle | 1 | 쉬는 선을 1 로 두어야 start 의 1 → 0 에지가 생긴다. 선이 끊기거나 GND 에 붙으면 계속 0 → break 로 보여 고장을 알 수 있다 |
| 0 | start | 0 | 수신기의 시간 기준점. 이 에지에서 반 비트 뒤가 start 중앙, 그 뒤 1비트마다 샘플 |
| 1 .. d | data d0..d_{d-1} | 값의 비트 i | LSB first (관례 — Q2) |
| d+1 | parity (선택) | EVEN: ones & 1, ODD: !(ones & 1) | 1비트 오류 검출 |
| 나머지 | stop × S | 1 | 선을 idle 로 돌려 다음 start 에지를 보장, 수신기에 처리 시간 |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">같은 'A' (0x41), 설정별 프레임 — S=start  d=data  P=parity  T=stop</text>
  <text x="10" y="57" fill="currentColor" font-family="monospace" font-weight="bold">8N1</text>
  <rect x="70" y="34" width="40" height="36" class="f-warn" stroke="currentColor"/><text x="86" y="48" fill="currentColor" font-size="12">S</text><text x="86" y="64" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="110" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="123" y="48" fill="currentColor" font-size="12">d0</text><text x="126" y="64" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="150" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="163" y="48" fill="currentColor" font-size="12">d1</text><text x="166" y="64" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="190" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="203" y="48" fill="currentColor" font-size="12">d2</text><text x="206" y="64" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="230" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="243" y="48" fill="currentColor" font-size="12">d3</text><text x="246" y="64" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="270" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="283" y="48" fill="currentColor" font-size="12">d4</text><text x="286" y="64" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="310" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="323" y="48" fill="currentColor" font-size="12">d5</text><text x="326" y="64" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="350" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="363" y="48" fill="currentColor" font-size="12">d6</text><text x="366" y="64" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="390" y="34" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="403" y="48" fill="currentColor" font-size="12">d7</text><text x="406" y="64" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="430" y="34" width="40" height="36" class="f-ok" stroke="currentColor"/><text x="446" y="48" fill="currentColor" font-size="12">T</text><text x="446" y="64" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <text x="564" y="57" fill="currentColor">10비트 · 80.0%</text>
  <text x="10" y="103" fill="currentColor" font-family="monospace" font-weight="bold">7E1</text>
  <rect x="70" y="80" width="40" height="36" class="f-warn" stroke="currentColor"/><text x="86" y="94" fill="currentColor" font-size="12">S</text><text x="86" y="110" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="110" y="80" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="123" y="94" fill="currentColor" font-size="12">d0</text><text x="126" y="110" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="150" y="80" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="163" y="94" fill="currentColor" font-size="12">d1</text><text x="166" y="110" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="190" y="80" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="203" y="94" fill="currentColor" font-size="12">d2</text><text x="206" y="110" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="230" y="80" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="243" y="94" fill="currentColor" font-size="12">d3</text><text x="246" y="110" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="270" y="80" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="283" y="94" fill="currentColor" font-size="12">d4</text><text x="286" y="110" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="310" y="80" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="323" y="94" fill="currentColor" font-size="12">d5</text><text x="326" y="110" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="350" y="80" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="363" y="94" fill="currentColor" font-size="12">d6</text><text x="366" y="110" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="390" y="80" width="40" height="36" class="f-acc" stroke="currentColor"/><text x="406" y="94" fill="currentColor" font-size="12">P</text><text x="406" y="110" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="430" y="80" width="40" height="36" class="f-ok" stroke="currentColor"/><text x="446" y="94" fill="currentColor" font-size="12">T</text><text x="446" y="110" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <text x="564" y="103" fill="currentColor">10비트 · 70.0%</text>
  <text x="10" y="149" fill="currentColor" font-family="monospace" font-weight="bold">8E1</text>
  <rect x="70" y="126" width="40" height="36" class="f-warn" stroke="currentColor"/><text x="86" y="140" fill="currentColor" font-size="12">S</text><text x="86" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="110" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="123" y="140" fill="currentColor" font-size="12">d0</text><text x="126" y="156" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="150" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="163" y="140" fill="currentColor" font-size="12">d1</text><text x="166" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="190" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="203" y="140" fill="currentColor" font-size="12">d2</text><text x="206" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="230" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="243" y="140" fill="currentColor" font-size="12">d3</text><text x="246" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="270" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="283" y="140" fill="currentColor" font-size="12">d4</text><text x="286" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="310" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="323" y="140" fill="currentColor" font-size="12">d5</text><text x="326" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="350" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="363" y="140" fill="currentColor" font-size="12">d6</text><text x="366" y="156" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="390" y="126" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="403" y="140" fill="currentColor" font-size="12">d7</text><text x="406" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="430" y="126" width="40" height="36" class="f-acc" stroke="currentColor"/><text x="446" y="140" fill="currentColor" font-size="12">P</text><text x="446" y="156" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="470" y="126" width="40" height="36" class="f-ok" stroke="currentColor"/><text x="486" y="140" fill="currentColor" font-size="12">T</text><text x="486" y="156" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <text x="564" y="149" fill="currentColor">11비트 · 72.7%</text>
  <text x="10" y="195" fill="currentColor" font-family="monospace" font-weight="bold">8O2</text>
  <rect x="70" y="172" width="40" height="36" class="f-warn" stroke="currentColor"/><text x="86" y="186" fill="currentColor" font-size="12">S</text><text x="86" y="202" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="110" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="123" y="186" fill="currentColor" font-size="12">d0</text><text x="126" y="202" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="150" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="163" y="186" fill="currentColor" font-size="12">d1</text><text x="166" y="202" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="190" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="203" y="186" fill="currentColor" font-size="12">d2</text><text x="206" y="202" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="230" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="243" y="186" fill="currentColor" font-size="12">d3</text><text x="246" y="202" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="270" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="283" y="186" fill="currentColor" font-size="12">d4</text><text x="286" y="202" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="310" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="323" y="186" fill="currentColor" font-size="12">d5</text><text x="326" y="202" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="350" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="363" y="186" fill="currentColor" font-size="12">d6</text><text x="366" y="202" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="390" y="172" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="403" y="186" fill="currentColor" font-size="12">d7</text><text x="406" y="202" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="430" y="172" width="40" height="36" class="f-acc" stroke="currentColor"/><text x="446" y="186" fill="currentColor" font-size="12">P</text><text x="446" y="202" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="470" y="172" width="40" height="36" class="f-ok" stroke="currentColor"/><text x="486" y="186" fill="currentColor" font-size="12">T</text><text x="486" y="202" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="510" y="172" width="40" height="36" class="f-ok" stroke="currentColor"/><text x="526" y="186" fill="currentColor" font-size="12">T</text><text x="526" y="202" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <text x="564" y="195" fill="currentColor">12비트 · 66.7%</text>
  <text x="10" y="241" fill="currentColor" font-family="monospace" font-weight="bold">9N1</text>
  <rect x="70" y="218" width="40" height="36" class="f-warn" stroke="currentColor"/><text x="86" y="232" fill="currentColor" font-size="12">S</text><text x="86" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="110" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="123" y="232" fill="currentColor" font-size="12">d0</text><text x="126" y="248" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="150" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="163" y="232" fill="currentColor" font-size="12">d1</text><text x="166" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="190" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="203" y="232" fill="currentColor" font-size="12">d2</text><text x="206" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="230" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="243" y="232" fill="currentColor" font-size="12">d3</text><text x="246" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="270" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="283" y="232" fill="currentColor" font-size="12">d4</text><text x="286" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="310" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="323" y="232" fill="currentColor" font-size="12">d5</text><text x="326" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="350" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="363" y="232" fill="currentColor" font-size="12">d6</text><text x="366" y="248" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="390" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="403" y="232" fill="currentColor" font-size="12">d7</text><text x="406" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="430" y="218" width="40" height="36" class="f-mut" stroke="currentColor"/><text x="443" y="232" fill="currentColor" font-size="12">d8</text><text x="446" y="248" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="470" y="218" width="40" height="36" class="f-ok" stroke="currentColor"/><text x="486" y="232" fill="currentColor" font-size="12">T</text><text x="486" y="248" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <text x="564" y="241" fill="currentColor">11비트 · 81.8%</text>
  <text x="10" y="276" fill="currentColor">7E1: 1 이 2개(짝수) → P = 0. 'A' 는 8N1 과 7E1 의 선로 모양이 똑같다 (Part C 참고).</text>
  <text x="10" y="296" fill="currentColor">8O2: 1 이 2개 → 홀수로 만들려고 P = 1. 효율 = data 비트 / 프레임 비트.</text>
</svg>
```

인코더 추적 — `uart_encode(0x43 'C', 7E1)`. 0x43 = 100 0011 (7비트), 1 이 3개 → 짝수 패리티 비트 = 1:

| k | 필드 | 식 | 값 |
|---|---|---|---|
| 0 | start | 0 | 0 |
| 1 | d0 | (0x43 >> 0) & 1 | 1 |
| 2 | d1 | (0x43 >> 1) & 1 | 1 |
| 3~6 | d2..d5 | | 0 0 0 0 |
| 7 | d6 | (0x43 >> 6) & 1 | 1 |
| 8 | parity | ones = 3 → 3 & 1 | 1 |
| 9 | stop | 1 | 1 |

결과 `0110000111`, 반환 10. 테스트 문자열과 같다. 디코더는 같은 표를 거꾸로 읽는다: `d |= bits[1 + i] << i`.

프레임 길이와 처리량(throughput). 문자/s = baud / L 이고 효율 = d / L:

| 설정 | L (비트) | 효율 | 115200 baud 문자/s | 9600 baud 문자/s |
|---|---|---|---|---|
| 8N1 | 10 | 80.0% | 11520 | 960 |
| 7E1 | 10 | 70.0% | 11520 | 960 |
| 8E1, 8O1, 8N2 | 11 | 72.7% | ≈ 10473 | ≈ 873 |
| 8E2, 8O2 | 12 | 66.7% | 9600 | 800 |
| 9N1 | 11 | 81.8% | ≈ 10473 | ≈ 873 |
| 5N1 | 7 | 71.4% | ≈ 16457 | ≈ 1371 |

면접 함정: "115200 이면 초당 14400 바이트" (= 115200 / 8) 는 틀리다. start·stop 때문에 8N1 에서도 11520 이다. 4 KiB 로그 덤프 = 40960 비트 = 355.6 ms (연습 1).

검사 순서와 "아무것도 쓰지 않는다". 인코더는 설정 → 값 범위 → 용량 순으로 **쓰기 전에** 모두 검사한다. 그래서 cap 이 1 모자라도 버퍼가 그대로다(테스트가 0xEE 가드로 확인). 값 범위: 7비트 설정에 0x80 을 주면 `UART_E_DATA`. 실제 USART 는 이런 값을 받으면 위 비트를 그냥 버리거나 패리티로 덮어쓴다 — 하드웨어는 조용하지만, API 는 시끄러운 쪽이 버그를 일찍 찾는다.

전수 왕복: 30 설정 × 2^d 값 = 6 × (32 + 64 + 128 + 256 + 512) = **5952 프레임**, 불일치 0. 이렇게 싼 경우는 무작위 대신 전부 돌린다. 9비트 data(9N1, RS-485 멀티드롭의 주소 표시 비트 등)까지 포함하므로 값 타입은 `uint16_t`.

### Part B — 플래그와 결함 주입

수신 플래그는 실제 USART 상태 레지스터를 흉내 낸다: STM32 의 `PE`(parity error), `FE`(framing error), `NE`(noise), `ORE`(overrun) — 이 레슨은 앞의 둘 + "start 자리가 1"(진짜 칩에선 start 확인 실패 = 프레임 자체가 안 생김, COMM-B02) 만 다룬다. 플래그는 비트마스크라 PE 와 FE 가 동시에 켜질 수 있고, 데이터는 오류가 있어도 레지스터에 들어온다 — 버릴지 말지는 **드라이버 정책**이다(Part C).

결함 주입 결과 (solution 실제 출력):

```text
round trip: 30 configs, 5952 frames, bad=0
1-bit inject: 67424 trials, bad=0, silent wrong chars=16192 (all in parity=N data bits)
```

| 뒤집힌 자리 | 플래그 | 데이터 |
|---|---|---|
| start | `F_START` | 그대로 |
| data d_i, parity 있음 | `F_PARITY` | d_i 가 뒤집힌 값 |
| data d_i, parity 없음 | **없음 (0)** | d_i 가 뒤집힌 값 — 조용한 오류 |
| parity | `F_PARITY` | 그대로 |
| stop (어느 것이든) | `F_FRAME` | 그대로 |

67424 = Σ_설정 2^d × L. 조용한 오류 16192 는 정확히 parity N 설정의 data 비트 위치 수: 2(stop 1/2) × Σ_{d=5..9} d·2^d = 2 × 8096. 즉 **8N1 은 data 비트 오류를 하나도 못 잡는다** — stop/start 자리만 지킨다. 패리티가 있으면 1비트는 전부 잡지만, 2비트는 전부 놓친다: 테스트에서 8E1 'A' 의 d0·d1 을 같이 뒤집으면 0x41 → 0x42 가 플래그 0 으로 통과한다(전수 실험은 ALGO-B13). 그래서 실무 프로토콜은 패리티 대신(또는 위에) 프레임 단위 CRC 를 둔다(COMM-B08 검출 능력, COMM-B10 프레임 포맷, Modbus RTU 는 패리티 + CRC-16 둘 다 — COMM-B13).

특수한 두 모양. **break** = 프레임 전체가 0(start, data, parity, stop 모두). 8N1 에선 data 0x00 + FE, 8O1 에선 PE + FE 까지 켜진다. 상대가 리셋 중이거나 선이 끊기면 이렇게 보이고, LIN·DMX512 는 일부러 break 를 "프레임 시작" 신호로 쓴다(길이 기준 검출은 COMM-B02). **idle**(전부 1)을 프레임으로 읽으면 `F_START` — 프레임이 아니다.

### Part C — 비트당 1샘플 수신기

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="cb01a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">uart_rx_step: 비트 시간마다 레벨 1개를 받는 수신 상태 기계</text>
  <rect x="30" y="70" width="150" height="46" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="62" y="98" fill="currentColor" font-family="monospace">WAIT_HIGH</text>
  <rect x="300" y="70" width="140" height="46" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="350" y="98" fill="currentColor" font-family="monospace">IDLE</text>
  <rect x="560" y="70" width="150" height="46" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="610" y="98" fill="currentColor" font-family="monospace">FRAME</text>
  <path d="M10 50 L40 70" fill="none" stroke="currentColor" marker-end="url(#cb01a)"/>
  <text x="10" y="44" fill="currentColor" font-size="12">처음 (캡처 시작)</text>
  <line x1="180" y1="93" x2="298" y2="93" stroke="currentColor" marker-end="url(#cb01a)"/>
  <text x="200" y="86" fill="currentColor" font-size="12">level = 1</text>
  <line x1="440" y1="93" x2="558" y2="93" stroke="currentColor" marker-end="url(#cb01a)"/>
  <text x="452" y="86" fill="currentColor" font-size="12">level = 0 (start)</text>
  <text x="452" y="110" fill="currentColor" font-size="12">buf[0]=0, n=1</text>
  <path d="M70 70 C 60 40, 130 40, 120 68" fill="none" stroke="currentColor" marker-end="url(#cb01a)"/>
  <text x="128" y="50" fill="currentColor" font-size="12">0</text>
  <path d="M340 70 C 330 40, 410 40, 400 68" fill="none" stroke="currentColor" marker-end="url(#cb01a)"/>
  <text x="410" y="50" fill="currentColor" font-size="12">1</text>
  <path d="M600 70 C 590 36, 680 36, 670 68" fill="none" stroke="currentColor" marker-end="url(#cb01a)"/>
  <text x="560" y="40" fill="currentColor" font-size="12">n &lt; L: buf[n++] = level</text>
  <path d="M620 116 C 600 170, 420 170, 380 118" fill="none" stroke="currentColor" marker-end="url(#cb01a)"/>
  <text x="420" y="176" fill="currentColor" font-size="12">n == L 이고 마지막 stop = 1</text>
  <path d="M680 116 C 660 250, 160 250, 105 118" fill="none" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#cb01a)"/>
  <text x="130" y="244" fill="currentColor" font-size="12">n == L 이고 마지막 stop = 0 (FE · break): 선이 아직 low → 1 을 먼저 기다린다</text>
  <text x="10" y="270" fill="currentColor">두 화살표 모두 uart_decode(buf) 로 문자 1개 + 플래그(PE/FE)를 내보낸다 (반환 1).</text>
  <text x="10" y="290" fill="currentColor">start 는 "1 다음의 0" 만 인정 — 캡처 중간 시작·break 뒤의 가짜 start 를 줄인다.</text>
</svg>
```

수신 추적 — `uart_tx_stream("A", 8N1, lead=2, gap=0)` = `1 1 0 1 0 0 0 0 0 1 0 1`:

| i | level | 상태 (전 → 후) | n | 동작 |
|---|---|---|---|---|
| 0 | 1 | WAIT_HIGH → IDLE | – | 선이 high 인 것을 확인 |
| 1 | 1 | IDLE → IDLE | – | |
| 2 | 0 | IDLE → FRAME | 1 | 1 → 0 에지 = start, buf[0] = 0 |
| 3 | 1 | FRAME | 2 | d0 = 1 |
| 4~8 | 0 | FRAME | 3~7 | d1..d5 = 0 |
| 9 | 1 | FRAME | 8 | d6 = 1 |
| 10 | 0 | FRAME | 9 | d7 = 0 |
| 11 | 1 | FRAME → IDLE | 10 = L | decode → 0x41, flags 0, 반환 1 |

이것이 바로 타이머 인터럽트로 도는 **소프트웨어 UART 수신기**(bit-banged RX)의 모양이다: 핀 변화 인터럽트(EXTI)로 start 에지를 잡고, 반 비트 뒤부터 1비트 주기 타이머 ISR 에서 `rx_step(gpio_read())` 를 부른다. 하드웨어 USART 는 같은 일을 오버샘플링으로 하고(COMM-B02), 끝나면 RXNE 플래그 + 인터럽트를 준다. 어느 쪽이든 ISR 은 문자를 링 버퍼에 넣고 끝내며(DS-B03, BM-M04), 파싱은 메인 루프에서 한다(COMM-B14).

정책 결정 세 가지. (1) **오류 문자는 버리고 센다** — `frames/ok/parity_err/frame_err/overflow` 통계는 현장에서 "가끔 깨짐" 을 숫자로 말하게 해 준다. Linux 의 termios 는 이 정책을 옵션으로 둔다: `INPCK` 로 패리티 검사, `IGNPAR` 면 오류 문자를 버리고, `PARMRK` 면 `\377 \0` 를 앞에 붙여 표시한다. (2) 출력 버퍼가 차면 새 문자를 버리고 `overflow` 를 센다(테스트: cap 4 에 14자 → 4개, overflow 10). (3) 캡처가 프레임 도중에 끝나면 `partial = 1`.

**설정 불일치** — 실제 출력:

```text
7E1 -> 8N1 rx: 48 65 6C 6C 6F AC A0 55 41 D2 D4 21 8D 0A
8N1 -> 7E1 rx: frames=14 ok=9 PE=5 FE=0
8E1 -> 8N1 rx: frames=14 ok=5 PE=0 FE=9
8N1 -> 8N2 rx, back-to-back: frames=10 ok=0 FE=10
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">설정 불일치: ',' (0x2C) 를 7E1 로 보내고 8N1 로 받으면</text>
  <text x="10" y="58" fill="currentColor">TX 7E1</text>
  <text x="10" y="128" fill="currentColor">RX 8N1</text>
  <rect x="90" y="36" width="48" height="36" class="f-warn" stroke="currentColor"/><text x="110" y="50" fill="currentColor" font-size="12">S</text><text x="110" y="66" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="138" y="36" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="155" y="50" fill="currentColor" font-size="12">d0</text><text x="158" y="66" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="186" y="36" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="203" y="50" fill="currentColor" font-size="12">d1</text><text x="206" y="66" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="234" y="36" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="251" y="50" fill="currentColor" font-size="12">d2</text><text x="254" y="66" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="282" y="36" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="299" y="50" fill="currentColor" font-size="12">d3</text><text x="302" y="66" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="330" y="36" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="347" y="50" fill="currentColor" font-size="12">d4</text><text x="350" y="66" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="378" y="36" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="395" y="50" fill="currentColor" font-size="12">d5</text><text x="398" y="66" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="426" y="36" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="443" y="50" fill="currentColor" font-size="12">d6</text><text x="446" y="66" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="474" y="36" width="48" height="36" class="f-acc" stroke="currentColor"/><text x="494" y="50" fill="currentColor" font-size="12">P</text><text x="494" y="66" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="522" y="36" width="48" height="36" class="f-ok" stroke="currentColor"/><text x="542" y="50" fill="currentColor" font-size="12">T</text><text x="542" y="66" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="90" y="106" width="48" height="36" class="f-warn" stroke="currentColor"/><text x="110" y="120" fill="currentColor" font-size="12">S</text><text x="110" y="136" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="138" y="106" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="155" y="120" fill="currentColor" font-size="12">d0</text><text x="158" y="136" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="186" y="106" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="203" y="120" fill="currentColor" font-size="12">d1</text><text x="206" y="136" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="234" y="106" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="251" y="120" fill="currentColor" font-size="12">d2</text><text x="254" y="136" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="282" y="106" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="299" y="120" fill="currentColor" font-size="12">d3</text><text x="302" y="136" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="330" y="106" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="347" y="120" fill="currentColor" font-size="12">d4</text><text x="350" y="136" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="378" y="106" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="395" y="120" fill="currentColor" font-size="12">d5</text><text x="398" y="136" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="426" y="106" width="48" height="36" class="f-mut" stroke="currentColor"/><text x="443" y="120" fill="currentColor" font-size="12">d6</text><text x="446" y="136" fill="currentColor" font-family="monospace" font-weight="bold">0</text>
  <rect x="474" y="106" width="48" height="36" class="f-acc" stroke="currentColor"/><text x="491" y="120" fill="currentColor" font-size="12">d7</text><text x="494" y="136" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <rect x="522" y="106" width="48" height="36" class="f-ok" stroke="currentColor"/><text x="542" y="120" fill="currentColor" font-size="12">T</text><text x="542" y="136" fill="currentColor" font-family="monospace" font-weight="bold">1</text>
  <path d="M498 74 V104" stroke="currentColor" marker-end="url(#cb01b)"/>
  <defs><marker id="cb01b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="584" y="58" fill="currentColor">0x2C 에 1 이 3개</text>
  <text x="584" y="76" fill="currentColor">→ 짝수 패리티 P = 1</text>
  <text x="584" y="128" fill="currentColor">패리티가 d7 로</text>
  <text x="584" y="146" fill="currentColor">읽힘 → 0xAC</text>
  <text x="10" y="178" fill="currentColor">두 설정 모두 10비트 → stop 자리가 맞아 PE·FE 없음. 수신 측은 "정상" 문자 0xAC 를 받는다.</text>
  <text x="10" y="198" fill="currentColor">1 이 짝수 개인 문자('H' 0x48 등)는 P = 0 이라 그대로 → 화면엔 글자 일부만 깨진다.</text>
  <text x="10" y="218" fill="currentColor">실측 "Hello, UART!\r\n" → <tspan font-family="monospace">48 65 6C 6C 6F AC A0 55 41 D2 D4 21 8D 0A</tspan></text>
  <text x="10" y="238" fill="currentColor">(솔루션 실행 출력 그대로. 오류 플래그 0개, 14자 중 5자가 bit7 = 1)</text>
</svg>
```

- **7E1 → 8N1**: 프레임 길이가 같아(10) 오류 0. 패리티 비트가 d7 로 읽혀, 1 이 홀수 개인 문자만 0x80 이 더해진다. `,`(0x2C) → 0xAC, ` `(0x20) → 0xA0, `R` → 0xD2, `\r` → 0x8D. 플래그가 없으니 드라이버 통계로는 못 찾는다 — 화면을 봐야 한다.
- **8N1 → 7E1**: d7(ASCII 는 0)이 패리티 자리로 읽힌다. 7비트에 1 이 홀수 개인 문자 5개(`, ␠ R T \r`)만 PE. "일부 문자만 사라진다".
- **8E1 → 8N1**: 패리티 비트가 stop 자리에 온다. 패리티 = 0 인(1 이 짝수 개) 9개가 FE. 진짜 stop 은 idle 처럼 보여서 다음 문자는 다시 맞는다.
- **8N1 ↔ 8N2**: 8N2 로 보내고 8N1 로 받는 것은 항상 괜찮다(남는 stop = idle). 거꾸로 8N1 back-to-back 을 엄격한 8N2 수신기로 받으면 두 번째 stop 자리에 다음 start(0)가 와서 전부 FE + 다음 프레임의 start 를 먹어 정렬이 무너진다(14자 중 10프레임만, 정상 0). 간격 1비트만 있어도 정상. STM32 는 2 stop 설정에서도 **첫 stop 만** FE 검사한다고 알려져 있다 — 그러면 이 경우도 받는다(연습 3, RM 확인 권장).

`demo_mismatch.c` 는 인쇄 가능 ASCII 95자 + CRLF = 97자를 6 × 6 조합으로 보낸 결과다 (실제 출력, 칸 = ok / PE / FE / silent, silent = 오류 플래그 없이 받았지만 메시지 순서에 맞지 않는 글자):

간격 0 (back-to-back):

| TX \ RX | 8N1 | 8E1 | 8O1 | 7E1 | 7O1 | 8N2 |
|---|---|---|---|---|---|---|
| 8N1 | 97/0/0/0 | 7/35/62/5 | 5/39/62/1 | 49/48/0/0 | 48/49/0/0 | 0/0/74/0 |
| 8E1 | 48/0/49/0 | 97/0/0/0 | 0/97/0/0 | 0/48/49/0 | 48/49/49/0 | 48/0/49/0 |
| 8O1 | 49/0/48/0 | 0/97/0/0 | 97/0/0/0 | 49/48/48/0 | 0/49/48/0 | 49/0/48/0 |
| 7E1 | 97/0/0/48 | 9/37/57/4 | 6/35/57/5 | 97/0/0/0 | 0/97/0/0 | 3/0/69/2 |
| 7O1 | 97/0/0/49 | 7/32/58/5 | 7/40/58/4 | 0/97/0/0 | 97/0/0/0 | 2/0/70/2 |
| 8N2 | 97/0/0/0 | 48/49/0/0 | 49/48/0/0 | 49/48/0/0 | 48/49/0/0 | 97/0/0/0 |

간격 1비트:

| TX \ RX | 8N1 | 8E1 | 8O1 | 7E1 | 7O1 | 8N2 |
|---|---|---|---|---|---|---|
| 8N1 | 97/0/0/0 | 48/49/0/0 | 49/48/0/0 | 49/48/0/0 | 48/49/0/0 | 97/0/0/0 |
| 7E1 | 97/0/0/48 | 0/97/0/0 | 97/0/0/48 | 97/0/0/0 | 0/97/0/0 | 97/0/0/48 |
| 7O1 | 97/0/0/49 | 97/0/0/49 | 0/97/0/0 | 0/97/0/0 | 97/0/0/0 | 97/0/0/49 |

(간격 1 의 8E1·8O1·8N2 행은 간격 0 과 같아 생략.) 읽는 법: **수신 프레임이 송신보다 길면**(8N1 → 8E1, 간격 0) 다음 start 를 먹어 정렬이 깨지고 PE·FE·silent 가 뒤섞인다 — 터미널엔 쓰레기. **길이가 같거나 짧으면** 문자 경계는 유지되고, 증상이 절반쯤의 PE 또는 FE(패리티 비트 값이 반반) 아니면 조용한 bit7 오염이다. 7E1 → 8O1(간격 1)은 오류 0 에 48자 오염: RX 가 보는 패리티 자리는 TX 의 stop(항상 1)이고 8비트(7 data + 짝수 패리티)는 1 이 언제나 짝수라 홀수 패리티 비트 = 1 이 늘 맞는다. 한 가지 원칙으로 정리된다: **오류 플래그 0 ≠ 설정 일치.**

**중간에서 듣기 시작하기.** 수신기를 back-to-back 스트림의 아무 비트에서 켜면, data 안의 1 → 0 에지를 start 로 오인할 수 있다. 테스트는 "Hello, UART!\r\n"(140비트)의 모든 오프셋에서 시작해 본다:

```text
mid-stream start: 140 offsets, tail lost at 0, offsets with junk/errors before the gap: 103
```

140개 중 103개 오프셋에서 간격 앞 부분은 쓰레기 또는 PE/FE 였지만, 10비트 idle 뒤의 "OK\r\n" 은 **모든** 오프셋에서 정확했다. 이유: 가짜 start 는 0 비트에서만 시작하므로 가장 늦어도 마지막 data/parity 비트 자리다. 거기서 시작한 가짜 프레임은 L 비트 뒤 = idle 간격 안 D + P 번째 비트에서 끝난다. 그러므로 **idle ≥ D + P 비트면 재동기가 보장되고**, 한 문자 시간(L 비트) idle 은 넉넉한 조건이다. 연습 4 가 6가지 설정에서 "D + P 면 실패 0, D + P − 1 이면 실패 있음" 을 확인한다. 실무 결론: 연속 스트림 중간에 붙는 수신기(늦게 켜진 MCU, 핫플러그된 USB-시리얼)는 문자 경계를 잃을 수 있으니, 패킷 사이에 idle 을 두거나 프레이밍(SLIP/COBS, COMM-B11/B12)으로 다시 맞춘다. STM32 의 IDLE line 인터럽트(COMM-M11)도 이 idle 을 패킷 경계로 쓴다.

### 복잡도와 RAM

인코드·디코드 O(L), L ≤ 13. 수신 상태 기계는 상태 3개 + 버퍼 13바이트 + 카운터 1개 — 비트를 `uint16_t` 시프트 레지스터에 모으면 버퍼 없이 4바이트로도 된다(실제 USART 의 시프트 레지스터가 그렇다). 이 레슨은 `uart_decode` 를 재사용하려고 비트 배열을 둔다. 스택·힙 사용 없음, 정적 버퍼만.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 67/67 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_mismatch.c -o demo_mismatch && ./demo_mismatch   # 불일치 행렬, 0.1 초 미만
```

## 7. 자주 하는 실수

**1. 최대 프레임 길이를 12 로 잡는다.** 1 + 9 + 1 + 2 = 13 이다. 이 레슨을 쓰면서 실제로 `#define UART_MAX_FRAME 12` 로 시작했고, 왕복 테스트(호출자 버퍼 64바이트)는 통과했지만 결함 주입 테스트가 `uint8_t fr[UART_MAX_FRAME]` 에 9E2 프레임을 인코드하다 `UART_E_CAP` 을 받아 쓰레기 프레임을 디코드했다: `1-bit inject: 67424 trials, bad=13310`. 인코더가 용량을 검사했기 때문에 넘침이 아니라 **실패**로 드러났다 — 용량 인자가 있는 API 의 가치. 고친 코드: `#define UART_MAX_FRAME 13 /* start 1 + data 9 + parity 1 + stop 2 */`.

**2. MSB first 로 보낸다.** `bits[1 + i] = (data >> (7 - i)) & 1`. 'A' 0x41 이 수신 측에 0x82 로 보인다(비트 반전). 로직 애널라이저 디코더 설정이 "MSB first" 로 되어 있어도 같은 증상이 난다 — 둘 중 어느 쪽이 틀렸는지는 파형에서 start 바로 뒤 비트가 d0(0x41 이면 1)인지 보면 된다.

**3. 패리티 짝/홀을 거꾸로, 또는 폭을 틀리게.** EVEN 패리티 비트는 1 개수가 **홀수일 때 1** 이다(합쳐서 짝수로). 그리고 하위 nbits 만 센다. STM32 계열에서는 한 가지가 더 있다: 레지스터의 word length(M 비트)는 **패리티를 포함**한다고 알려져 있다 — 8E1 을 원하면 9비트 word + PCE, "8비트 word + 패리티" 로 설정하면 실제로는 7E1 이 나간다(HAL 의 `UART_WORDLENGTH_9B` + `UART_PARITY_EVEN` = 8E1). 패밀리별 RM 확인 권장 — 연습 2.

**4. 디코더가 첫 오류에서 돌아간다.** `if (bits[k] != p) return UART_F_PARITY;` 로 짜면 PE+FE 동시 프레임(테스트의 `(UART_F_PARITY | UART_F_FRAME)`)에서 FE 를 놓치고, break(8O1 에서 PE+FE)를 "패리티 오류" 로만 보고한다. 하드웨어 상태 레지스터처럼 전부 검사하고 OR 한다.

**5. FE 뒤에 바로 IDLE 로 간다.** `rx->state = RX_IDLE;` (마지막 stop 이 0 이어도). 선이 아직 low 이므로 다음 비트의 0 을 또 start 로 본다. 실측: "AB" + 0 × 25 + "CD" 에서 정답은 FE 1개 + "ABCD" 인데, 이 버그는 FE 3개 + `41 42 14` — 'C', 'D' 대신 쓰레기 1개. 고친 코드: `rx->state = level ? RX_IDLE : RX_WAIT_HIGH;`.

**6. 처리량을 baud / 8 로 계산한다.** 115200 / 8 = 14400 B/s 가 아니라 8N1 이면 11520, 8E1 이면 ≈ 10473. 로그를 UART 로 쏟는 펌웨어에서 TX 버퍼 크기·DMA 길이·타임아웃을 이 숫자로 잡는다(연습 1).

**7. 수신 플래그 0 이면 설정이 맞다고 믿는다.** 7E1 → 8N1 은 오류 0 개로 bit7 만 오염된다(Part C 표). bring-up 첫날에는 플래그 대신 **알려진 문자열**(예: 펌웨어가 부팅 시 0x55 'U' 나 "AT\r" 에 대한 "OK")을 바이트 단위로 비교한다.

## 8. 면접 Q&A

<details><summary>Q1. UART 선은 왜 idle 이 high 이고 start 비트는 0 인가요?</summary>

수신기가 문자 시작을 알 방법은 레벨 변화뿐이다. 쉬는 상태와 start 가 달라야 에지가 생기고, stop(= idle 과 같은 1)이 매 문자 끝에 선을 되돌려 다음 start 에지를 보장한다. high 를 idle 로 둔 데에는 역사적·실용적 이유가 있다: 텔레타이프의 전류 루프는 쉴 때 전류가 흐르는 상태(mark)라 선이 끊기면 "계속 space" → break 로 바로 보였다. TTL UART 도 같은 관례라 선이 GND 로 붙거나 상대가 꺼지면 연속 0 = break/FE 로 드러난다. RS-232 레벨에서는 극성이 뒤집혀 mark(논리 1, idle)가 −3 ~ −15 V, space(0)가 +3 ~ +15 V — 스코프로 RS-232 를 보면 idle 이 음전압인 이유. 꼬리: "TTL 쪽에 RS-232 를 바로 물리면?" → 극성과 전압이 모두 달라 MAX232 같은 레벨 변환기가 필요하다.

</details>

<details><summary>Q2. 왜 LSB 를 먼저 보내나요? SPI 는 왜 MSB 가 기본이죠?</summary>

UART 의 LSB first 는 관례다 — 초기 직렬 장치(텔레타이프 코드, 시프트 레지스터 하드웨어)가 그렇게 굳었고, 지금은 표준(16550, 모든 MCU USART)이 그렇다. 기술적 필연은 아니다. 그래서 각 프로토콜이 따로 정한다: SPI 는 장치 레지스터 주소·명령이 MSB first 가 기본이고(COMM-B04, 많은 MCU SPI 에 LSBFIRST 옵션), I2C 는 MSB first(COMM-B06). 실무 포인트는 "로직 애널라이저 디코더 설정과 펌웨어가 같은가" 다. 순서가 바뀌면 0x41 ↔ 0x82 처럼 **비트 반전** 값이 보이니, 이 패턴을 알면 바로 진단한다(비트 반전 함수는 ALGO-B13).

</details>

<details><summary>Q3. 패리티는 무엇을 잡고 무엇을 못 잡나요? 언제 쓰나요?</summary>

이 레슨의 전수 주입: 패리티가 있으면 프레임 안 1비트 오류는 전부 플래그가 켜진다(data 와 parity 자리 = PE, stop = FE, start = 프레임 아님). 2비트 data 오류는 1 개수의 짝·홀이 그대로라 전부 놓친다(0x41 → 0x42 예). 패리티 없는 8N1 은 data 오류를 하나도 못 본다(16192 / 16192 조용함). 오버헤드는 문자당 1비트(8N1 80% → 8E1 72.7%). 쓰는 곳: Modbus RTU 기본은 짝수 패리티(패리티 없으면 stop 2 개로 문자 길이 11비트 유지 — Modbus over Serial Line 사양, COMM-B13), 일부 계량기·산업 장비의 7E1. 새 설계라면 문자 패리티 대신 패킷 CRC 를 권한다 — 버스트 오류와 여러 비트 오류를 잡는다(COMM-B08). 둘 다 쓰는 것도 흔하다: 패리티는 하드웨어가 공짜로 문자 단위 즉시 검출, CRC 는 패킷 단위 보장.

</details>

<details><summary>Q4. 콘솔에 글자가 반쯤 깨져 나옵니다. 무엇부터 의심하나요?</summary>

순서: (1) baud — 완전한 쓰레기 + FE 다발이면 baud 나 클럭(HSE 값) 문제(COMM-B03, HWLAB-M05 의 26 MHz 크리스털 사례). (2) 형식 불일치 — 이 레슨의 표: 영문자 일부만 bit7 이 켜져 보이면 7E1 ↔ 8N1(오류 플래그 0), 일부 문자만 사라지고 PE 가 쌓이면 8N1 → 7E1, 절반쯤 FE 면 8E1 → 8N1. (3) 극성·레벨 — RS-232 를 TTL 로 반전 없이 받으면 거의 모든 프레임이 FE. (4) 그다음 로직 애널라이저: start 에지에서 비트 폭을 재 baud 확인, start 뒤 비트를 LSB 로 읽어 값 확인, stop 자리 레벨 확인. 내 경험 쪽 예: RF 모듈 AT 명령이 "OK" 대신 깨진 글자를 돌려주면 모듈 기본 설정(baud·패리티)이 펌웨어와 다른 경우가 많아, 데이터시트의 기본값으로 먼저 열고 `AT+...` 로 바꾼다.

</details>

<details><summary>Q5. 8N2 는 왜 있나요? 송신·수신의 stop 개수가 다르면?</summary>

stop 비트는 "최소 idle 시간" 이다. 느린 수신기(옛 기계식 텔레타이프, 소프트웨어 UART)에 다음 문자 전 처리 시간을 주고, 클럭 오차가 큰 링크에서 다음 start 에지 찾기에 여유를 준다. 수신기는 보통 첫 stop 만 보면 되므로 **송신 8N2 / 수신 8N1 은 항상 안전**하다(남은 stop = idle). 반대로 송신 8N1 / 수신이 두 stop 을 모두 검사하면 back-to-back 문자에서 두 번째 stop 자리에 다음 start 가 와 FE 가 난다(이 레슨 실측: 14자 중 정상 0). STM32 USART 는 2 stop 설정이어도 첫 stop 만 FE 검사하는 것으로 알려져 있어 이 경우도 받는다(RM 확인 권장). 그래서 "확신이 없으면 TX 는 2 stop, RX 는 1 stop" 이 안전한 조합이다. 대가는 처리량 −9%(10 → 11비트).

</details>

<details><summary>Q6. 연속 스트림 도중에 수신을 시작하면 어떻게 되나요? 어떻게 다시 맞추나요?</summary>

수신기는 첫 1 → 0 에지를 start 로 믿으므로 data 안의 에지에 잘못 맞출 수 있고, back-to-back 이면 그 잘못된 정렬이 여러 문자 동안 이어질 수 있다(이 레슨: 140개 오프셋 중 103개에서 쓰레기·오류). 0x55 연속처럼 0101… 패턴은 오정렬되어도 오류 플래그가 안 날 수도 있다. 다시 맞추는 법: (1) idle — D + P 비트 이상 쉬면 반드시 정렬된다(5. 풀이의 증명, 연습 4). (2) break — 한 문자보다 긴 0 은 무조건 FE 를 만들고 수신기를 WAIT_HIGH 로 보낸다(LIN, DMX512 가 프레임 시작에 쓰는 이유). (3) 상위 프레이밍 — SLIP/COBS 의 구분자, 길이 + CRC 프레임 재검색(COMM-B10~B12, COMM-M10). 문자 동기와 패킷 동기는 별개의 층이다.

</details>

<details><summary>Q7. ISR 에서는 무엇을 하고, PE/FE 는 어떻게 처리하나요? DMA 는요?</summary>

RX ISR: 상태 레지스터를 읽고(PE/FE/NE/ORE), 데이터 레지스터를 읽어 RXNE 를 내리고(STM32 F4 계열은 SR 읽기 → DR 읽기 순서로 오류 플래그가 지워진다고 알려져 있음 — RM 확인 권장, 새 계열은 ICR 레지스터에 써서 지움), 정상 문자는 링 버퍼에 넣고, 오류는 카운터만 올린다 — 이 레슨의 `uart_rx_run` 정책. 파싱·printf 는 ISR 밖(COMM-B14, BM-M04). 오류 문자를 그대로 넘길지(표시해서) 버릴지는 상위 프로토콜이 CRC 를 갖고 있는지에 따라 정한다: CRC 가 있으면 넘겨도 프레임 단위로 버려진다. 고속(≥ 1 Mbaud)·긴 패킷이면 문자마다 인터럽트 대신 circular DMA + IDLE 인터럽트(COMM-M11) — 그때는 문자별 PE/FE 를 놓치기 쉬워(DMA 는 데이터만 옮긴다) 패킷 CRC 가 사실상 필수다.

</details>

<details><summary>Q8. 송수신 클럭이 3% 다르면 어떻게 되나요?</summary>

수신기는 start 에지에서 맞춘 뒤 다시 맞추지 않고 비트 중앙을 센다. 8N1 의 마지막 샘플(stop 중앙)은 에지 후 9.5 비트 시간이라 누적 어긋남은 9.5 × e. 이것이 반 비트를 넘으면 틀린 비트를 샘플한다 → 이상적 한계 |e| < 0.5 / 9.5 ≈ 5.3%(양쪽 오차 합). 실제 수신기는 start 검출 지연(오버샘플 1개)·다수결 샘플 폭·느린 에지 때문에 여유가 더 작아, 흔한 설계 규칙은 각 측 ±2% 안팎(대표값). 3% 는 한쪽 오차라면 대개 아직 받고, 상대도 반대로 2% 틀렸다면 위험하다. 11비트 프레임(8E1)은 마지막 샘플이 10.5 비트라 더 민감하다. 정확한 측정은 COMM-B03 의 스윕 실험.

</details>

## 9. 추가 연습

### 연습 1 — 전송 시간

`uint64_t uart_tx_time_us(nbytes, baud, cfg, gap_bits)` — 문자마다 L + gap 비트, µs 반올림. 4 KiB 를 115200 8N1 로? 문자 사이 2비트 간격(ISR 이 늦어 생기는 틈)이 있으면?

<details><summary>답 보기</summary>

`(nbytes · (L + gap) · 10^6 + baud/2) / baud` (64비트로 곱한 뒤 나눈다 — 32비트면 4096 × 10 × 10^6 이 넘친다). 4096 B 8N1 = 40960 비트 / 115200 = **355556 µs**, 간격 2 이면 426667 µs (+20%). 1 MB 를 921600 8N1 로 = 10850694 µs ≈ 10.85 s. 9600 baud 1문자 = 1042 µs. 펌웨어 의미: 115200 에서 printf 한 줄(80자) ≈ 6.9 ms — 블로킹 printf 가 제어 루프 주기를 깨는 이유(DMA TX 또는 링 버퍼, C-M10·SYSD-B08).

</details>

### 연습 2 — STM32 식 설정으로 옮기기 (대표값)

`int stm32_map(cfg, has_m1, &o)` — `o.word_bits` = data + parity(STM32 word length 는 패리티 포함), `pce`, `ps`(1 = odd), `stop_code`(1 stop = 0b00, 2 stop = 0b10). word 는 8/9 비트, M1 이 있는 계열은 7 비트도. 불가능하면 −1. 이 정의는 이 레슨의 모의 정의다 — 실제 비트 위치와 지원 범위는 패밀리 RM 확인 권장.

<details><summary>답 보기</summary>

`w = d + (parity != NONE)`; `w > 9 || w < (has_m1 ? 7 : 8)` 이면 −1. 결과: 8N1 → 8, **8E1 → 9**, 7E1 → 8, 8O2 → 9 + PS=1 + STOP=0b10, 7N1 → M1 계열만 7, 9E1(=10) 불가, 5N1 불가. 일반 PC(16550 계열)는 5~8 data 를 지원하지만 MCU USART 는 보통 7~9 word 라 5·6 비트 장비(옛 Baudot 계열)는 소프트웨어 UART 가 필요하다. 이 레슨의 30가지 설정 모델은 하드웨어보다 넓다.

</details>

### 연습 3 — "첫 stop 만 검사" 하는 수신기

`rx_run(..., lenient)` — lenient 면 프레임이 첫 stop 에서 끝나고 그 stop 만 FE 검사. 8N1 back-to-back 을 8N2 수신기로 엄격/관대하게 받아 비교하라.

<details><summary>답 보기</summary>

L 을 `L − (S − 1)` 로, 검사할 stop 수를 1 로. 실제 출력: `ex3: 8N1 back-to-back -> 8N2 strict ok=0 FE=10 | lenient ok=14 FE=0`. 진짜 8N2 스트림도 lenient 로 14자 정상(두 번째 stop 은 idle 로 지나감). 대가: 송신이 정말 2 stop 을 약속했는데 두 번째가 0 인 오류는 못 본다 — 실제로는 그 0 이 다음 start 처럼 보이게 된다. 하드웨어(STM32 로 알려진 동작)가 lenient 쪽을 택한 것은 호환성이 검출보다 중요해서다.

</details>

### 연습 4 — 재동기에 필요한 최소 idle

back-to-back 접두(무작위 1~6자)의 모든 비트 오프셋에서 수신을 시작한다. idle 간격 g 뒤 꼬리 2자를 항상 받으려면 g 의 최솟값은? 증명하고 6가지 설정에서 실험으로 확인하라.

<details><summary>답 보기</summary>

g_min = D + P. 증명(충분): 간격 시작 시점에 진행 중인 가짜 프레임은 0 비트에서 시작했고, 간격 바로 앞 S 비트는 stop(1)이므로 가장 늦은 시작은 마지막 data/parity 자리 = 간격 시작 − S − 1. 끝나는 곳은 그 + L − 1 = 간격 시작 + D + P − 1. 그러니 g ≥ D + P 면 가짜 프레임이 간격 안(마지막 stop 샘플 = 1)에서 끝나 IDLE 이 되고, 꼬리의 진짜 start 를 1 → 0 에지로 잡는다. 필요: g = D + P − 1 이면 마지막 data 비트가 0 이고 그 앞이 1 인 접두에서, 가짜 프레임의 stop 이 진짜 start 자리에 걸려 꼬리를 잃는다. 실제 출력 (600 접두 × 모든 오프셋):

```text
ex4: cfg 8N1  gap=8 (D+P): 0/21400 offsets lose the tail | gap=7: 1558
ex4: cfg 7E1  gap=8 (D+P): 0/20400 offsets lose the tail | gap=7: 1436
ex4: cfg 8E1  gap=9 (D+P): 0/23562 offsets lose the tail | gap=8: 1735
ex4: cfg 8N2  gap=8 (D+P): 0/23309 offsets lose the tail | gap=7: 1863
ex4: cfg 8O2  gap=9 (D+P): 0/25248 offsets lose the tail | gap=8: 1928
ex4: cfg 5N1  gap=5 (D+P): 0/13993 offsets lose the tail | gap=4: 1007
```

stop 개수는 답에 안 들어간다(8N1 과 8N2 둘 다 8). "한 문자 시간 idle" 은 L ≥ D + P 라 항상 충분한, 외우기 쉬운 규칙이다.

</details>

### 연습 5 — 캡처로 설정 추측하기

모르는 장비의 TX 를 비트 단위로 캡처했다(baud 는 안다고 하자, baud 추정은 COMM-A10). 후보 {8N1, 8E1, 8O1, 7E1, 7O1, 8N2} 중 설정을 고르는 `uart_guess_cfg` 를 짜라. 오류 수만으로 충분한가?

<details><summary>답 보기</summary>

점수 = (PE + FE + partial) 최소 → 동점이면 인쇄 가능 문자(0x20~0x7E, CR/LF/TAB) 수 최대 → 그래도 동점이면 후보 순서. 오류 수만으로는 **부족**하다: 7E1 캡처를 8N1 로 읽어도 오류 0 이다(테스트가 확인). 인쇄 가능 점수가 동점을 깬다. 8N2 캡처는 8N1 로 고른다 — 수신 쪽에선 동등. 5가지 진짜 설정 × 간격 0/1 = 10 캡처 전부 정답. 텍스트가 아닌 바이너리 프로토콜이면 인쇄 가능 점수 대신 "알려진 헤더 바이트" 나 CRC 통과율을 쓴다. 로직 애널라이저의 자동 판별도 비슷한 휴리스틱이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 32/32 passed
```

## 10. 핵심 정리

- 선 = idle 1. 문자 = start 0 + data LSB first + (parity) + stop 1×S. 'A' 8N1 = `0 1000 0010 1`.
- L = 1 + D + P + S (최대 1 + 9 + 1 + 2 = 13). 효율 D/L: 8N1 80%, 8E1 72.7%. 문자/s = baud / L (115200 8N1 = 11520, baud/8 아님).
- EVEN 패리티 비트 = 1 개수 & 1, ODD = 반대. 하위 D 비트만 센다. STM32 word length 는 패리티 포함(8E1 = 9비트 word).
- 플래그: start 자리 1 = 프레임 아님, PE, FE(stop 0). 동시에 켜질 수 있고 데이터는 그대로 남는다. 버릴지는 드라이버 정책.
- 패리티: 1비트 오류 전부, 2비트 0. 8N1 은 data 오류를 하나도 못 본다 → 패킷 CRC.
- 수신기: 1 → 0 에지만 start, FE 뒤에는 high 를 먼저 기다린다. idle ≥ D + P 비트면 재동기 보장.
- 불일치: 7E1 ↔ 8N1 은 오류 0 에 bit7 오염, 8E1 → 8N1 은 절반 FE, 긴 RX 프레임 + back-to-back 은 쓰레기. 오류 0 ≠ 설정 일치.

## 참고자료

- National Semiconductor/TI, *PC16550D UART with FIFOs* 데이터시트 — LCR 의 word length 5~8, parity, stop 1/1.5/2, LSR 의 PE/FE/BI(break) 비트: <https://www.ti.com/lit/ds/symlink/pc16550d.pdf>
- STMicroelectronics, RM0090 (STM32F405/407 Reference Manual) USART 장 — word length(M), PCE/PS, STOP, SR 의 PE/FE/NE/ORE, 2 stop 수신 동작. 새 계열(M1 비트, ICR)은 RM0351(STM32L4) 등. 이 레슨의 STM32 서술은 기억에 의존 — 본문 표시대로 RM 확인 권장.
- POSIX `termios` (`man 3 termios`) — CS5~CS8, PARENB, PARODD, CSTOPB, INPCK, IGNPAR, PARMRK.
- TIA/EIA-232-F — mark/space 전압 레벨(mark = 논리 1 = 음전압).
- Modbus Organization, *MODBUS over Serial Line Specification and Implementation Guide V1.02* — RTU 문자 형식(11비트, 기본 짝수 패리티): <https://modbus.org/docs/Modbus_over_serial_line_V1_02.pdf>
- sigrok UART 디코더 문서 — 비트 순서·패리티·stop 설정 옵션: <https://sigrok.org/wiki/Protocol_decoder:Uart>
- 관련 레슨: COMM-B02 (16x 오버샘플링 수신기, 글리치·break·overrun), COMM-B03 (baud 오차와 허용 한계), COMM-B04 (SPI, MSB first), COMM-B08 (패리티·체크섬·CRC 검출 능력), COMM-B10~B12 (패킷 프레이밍), COMM-B13 (Modbus RTU), COMM-B14 (UART 커맨드 라인), COMM-M10 (재동기 파서), COMM-M11 (DMA + IDLE), COMM-A10 (LA 디코더), ALGO-B13 (패리티·비트 반전, 8E1 전수 실험), HWLAB-M05 (LA 로 UART baud 측정), DS-B03 (링 버퍼), BM-B12 / BM-M04 (폴링·인터럽트 UART 드라이버), C-M10 (UART 로깅 printf)

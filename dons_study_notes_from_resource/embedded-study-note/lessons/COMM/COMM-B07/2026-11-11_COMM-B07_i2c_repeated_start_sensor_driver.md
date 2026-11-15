---
id: COMM-B07
time: 60분
updated: 2026-11-11
---
# COMM-B07 I2C 레지스터 읽기: repeated START와 센서 드라이버

## 학습 목표

- "포인터(레지스터 주소) 쓰기 → **repeated START(Sr)** → 읽기" 시퀀스를 바이트 단위로 말하고 `i2c_write_read()` 로 구현한다. 로그 `S 90 A 0F A Sr 91 A A7 N P` 를 보고 바이트마다 누가 SDA 를 정했는지 설명할 수 있다.
- 레지스터 맵을 가진 mock 온도 센서 슬레이브를 만든다: 포인터, 자동 증가(auto-increment)와 랩, 읽기 전용 비트, 맵 밖 포인터는 NACK.
- 같은 읽기를 STOP + START 로 하면 무엇이 깨지는지 **로그로** 보여 준다: 다른 마스터(또는 다른 RTOS 태스크)가 사이에 끼어든다, STOP 에서 포인터를 리셋하는 디바이스, MSB·LSB 가 다른 변환에서 오는 찢어진 읽기(torn read).
- 드라이버를 짠다: WHO_AM_I 확인 → CONFIG 쓰기 + 마스크 되읽기 → DRDY 폴링 연속 읽기 → 고정소수점 raw 를 m°C 로 (바닥 나눗셈, 65536 값 전수 검사).

## 1. 문제

센서 bring-up 의 첫 줄이 "WHO_AM_I 읽어서 0xA7 나오나"다. 그 한 줄이 `S addr+W reg Sr addr+R data P` 이고, 면접에서는 "왜 중간에 STOP 을 안 넣나요?" "마지막 바이트는 왜 NACK?" "16비트 온도를 두 번에 나눠 읽으면?" 으로 이어진다.

I2C 센서는 거의 다 **레지스터 맵** 구조다. 디바이스 안에 포인터(pointer, 레지스터 주소) 하나가 있고, 마스터가 쓰기 트랜잭션의 첫 데이터 바이트로 포인터를 정한다. 읽기는 "포인터부터 차례로" 나온다. 그래서 "레지스터 0x0F 읽기" 는 두 단계다: (1) 쓰기 방향으로 포인터 = 0x0F, (2) 읽기 방향으로 바이트 받기. 방향을 바꾸려면 주소 바이트(R/W 비트 포함)를 다시 보내야 하고, 그러려면 START 가 다시 필요하다. 이때 STOP 없이 START 를 다시 내는 것이 **repeated START(Sr)** 다. UM10204 는 이것을 "combined format" 이라 부른다.

SDA·SCL 의 비트 수준 규칙(wired-AND, SCL high 중 SDA 변화 = START/STOP, 9번째 클럭의 ACK)은 COMM-B06 에서 했다. 이 레슨은 그 위의 **트랜잭션 층**을 다룬다. 코드는 COMM-B06 모델 축약판이다: 바이트와 ACK·START·STOP 만 남기고, 모든 이벤트를 로직 애널라이저(logic analyzer) 디코더처럼 한 줄 로그로 찍는다. 풀업 저항 값은 HWLAB-B03, 디코더 자체는 HWLAB-M06 참고.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">WHO_AM_I(0x0F) 읽기 — Sr 한 번 vs STOP + START (바이트 = solution.c 로그 그대로)</text>
<text x="10" y="46" fill="currentColor">(a) i2c_write_read: 포인터 쓰기 → Sr → 읽기. 한 트랜잭션.</text>
<rect x="10" y="56" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="23" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">S</text>
<rect x="36" y="56" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="54" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">90</text>
<rect x="72" y="56" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="81" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="90" y="56" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="108" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0F</text>
<rect x="126" y="56" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="135" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="144" y="56" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="157" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">Sr</text>
<rect x="170" y="56" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="188" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">91</text>
<rect x="206" y="56" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="215" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="224" y="56" width="36" height="24" class="f-ok" stroke="currentColor"/><text x="242" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A7</text>
<rect x="260" y="56" width="18" height="24" class="f-acc" stroke="currentColor"/><text x="269" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">N</text>
<rect x="278" y="56" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="291" y="72" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">P</text>
<text x="314" y="73" fill="currentColor">→ 0xA7 정답</text>
<text x="10" y="114" fill="currentColor">(b) i2c_write → STOP → i2c_read, 그 사이 버스를 기다리던 M2 가 끼어든다</text>
<rect x="10" y="124" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="23" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">S</text>
<rect x="36" y="124" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="54" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">90</text>
<rect x="72" y="124" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="81" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="90" y="124" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="108" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0F</text>
<rect x="126" y="124" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="135" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="144" y="124" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="157" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">P</text>
<rect x="178" y="124" width="26" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="191" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">S</text>
<rect x="204" y="124" width="36" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="222" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">90</text>
<rect x="240" y="124" width="18" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="249" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="258" y="124" width="36" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="276" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">02</text>
<rect x="294" y="124" width="18" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="303" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="312" y="124" width="26" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="325" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">Sr</text>
<rect x="338" y="124" width="36" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="356" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">91</text>
<rect x="374" y="124" width="18" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="383" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="392" y="124" width="36" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="410" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text>
<rect x="428" y="124" width="18" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="437" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">N</text>
<rect x="446" y="124" width="26" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="459" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">P</text>
<rect x="480" y="124" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="493" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">S</text>
<rect x="506" y="124" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="524" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">91</text>
<rect x="542" y="124" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="551" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="560" y="124" width="36" height="24" class="f-warn" stroke="currentColor"/><text x="578" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">28</text>
<rect x="596" y="124" width="18" height="24" class="f-acc" stroke="currentColor"/><text x="605" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">N</text>
<rect x="614" y="124" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="627" y="140" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">P</text>
<text x="178" y="166" fill="currentColor" font-size="12">M2: 자기 일(CONFIG 읽기) — ptr 가 02 → 03 으로 옮겨짐</text>
<text x="480" y="184" fill="currentColor" font-size="12">0x28 = THIGH_MSB</text>
<text x="10" y="224" fill="currentColor">(c) STOP 에서 포인터를 0 으로 되돌리는 디바이스 (M2 없음)</text>
<rect x="10" y="234" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="23" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">S</text>
<rect x="36" y="234" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="54" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">90</text>
<rect x="72" y="234" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="81" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="90" y="234" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="108" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0F</text>
<rect x="126" y="234" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="135" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="144" y="234" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="157" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">P</text>
<rect x="178" y="234" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="191" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">S</text>
<rect x="204" y="234" width="36" height="24" class="f-acc" stroke="currentColor"/><text x="222" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">91</text>
<rect x="240" y="234" width="18" height="24" class="f-ok" stroke="currentColor"/><text x="249" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<rect x="258" y="234" width="36" height="24" class="f-warn" stroke="currentColor"/><text x="276" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0C</text>
<rect x="294" y="234" width="18" height="24" class="f-acc" stroke="currentColor"/><text x="303" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">N</text>
<rect x="312" y="234" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="325" y="250" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">P</text>
<text x="348" y="251" fill="currentColor">→ 0x0C = TEMP_MSB 가 읽힘</text>
<rect x="10" y="292" width="16" height="14" class="f-acc" stroke="currentColor"/><text x="32" y="304" fill="currentColor" font-size="12">우리 마스터가 SDA 를 정함</text>
<rect x="226" y="292" width="16" height="14" class="f-ok" stroke="currentColor"/><text x="248" y="304" fill="currentColor" font-size="12">슬레이브가 SDA 를 정함</text>
<rect x="420" y="292" width="16" height="14" class="f-mut" stroke="currentColor"/><text x="442" y="304" fill="currentColor" font-size="12">버스 조건 S/Sr/P</text>
<rect x="592" y="292" width="16" height="14" class="f-warn" stroke="currentColor"/><text x="614" y="304" fill="currentColor" font-size="12">틀린 데이터</text>
<rect x="10" y="312" width="16" height="14" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="32" y="324" fill="currentColor" font-size="12">점선 = 다른 마스터 M2 의 트랜잭션. 90/91 = 7비트 주소 0x48 을 왼쪽으로 1 밀고 R/W 비트를 붙인 바이트</text>
</svg>
```

(b) 줄이 이 레슨의 핵심이다. 우리 MCU 가 포인터를 0x0F 로 쓰고 STOP 을 내는 순간 버스는 비었다(bus free). 버스를 기다리던 다른 마스터 M2 가 같은 센서의 CONFIG(0x02)를 읽으면 포인터는 0x03 으로 옮겨진다. 우리가 이어서 읽으면 THIGH_MSB(0x28)를 받는다. 같은 일이 펌웨어 안에서도 생긴다: 하나의 I2C 컨트롤러를 두 RTOS 태스크가 mutex 없이 쓰면, 태스크 A 의 `write()` 와 `read()` 사이에 태스크 B 의 트랜잭션이 들어간다.

### mock 센서 레지스터 맵 (이 레슨의 가상 부품, 7비트 주소 0x48)

| 주소 | 이름 | R/W | 리셋 값 | 설명 |
|---|---|---|---|---|
| 0x00 | TEMP_MSB | R | 0x00 | 온도 int16(2의 보수) 상위 바이트, 1 LSB = 1/128 °C = 7.8125 m°C |
| 0x01 | TEMP_LSB | R | 0x00 | 하위 바이트 |
| 0x02 | CONFIG | R/W | 0x00 | bit0 EN(연속 변환), bit2:1 AVG(저장만), bit7 DRDY(읽기 전용, TEMP_MSB 를 읽으면 0) |
| 0x03/0x04 | THIGH | R/W | 0x2800 (80 °C) | 경보 상한 (저장만) |
| 0x05/0x06 | TLOW | R/W | 0x0A00 (20 °C) | 경보 하한 (저장만) |
| 0x07–0x0E | — | — | 0x00 | 예약: 읽으면 0, 쓰면 ACK 후 무시 |
| 0x0F | WHO_AM_I | R | 0xA7 | 부품 ID |

- 포인터는 읽기·쓰기 바이트마다 1 증가하고 0x0F 다음은 0x00 (랩). 포인터 바이트로 0x10 이상이 오면 NACK.
- **변환 모델**: EN=1 이면 STOP 을 `conv_every` 번 볼 때마다 다음 온도 값이 TEMP 에 들어가고 DRDY=1. 트랜잭션 도중(START~STOP)에는 TEMP 가 바뀌지 않는다 — 읽는 동안 출력을 잠그는 섀도(shadow) 레지스터 흉내다. 시간을 "STOP 개수"로 세는 것은 모델 단순화다.
- 실제 부품과 비교 (대표값, 데이터시트 확인 권장): TI TMP117 은 16비트 레지스터(포인터가 워드 단위), 온도 해상도 7.8125 m°C, 기본 주소 0x48, Device ID 레지스터 0x0F. LM75 계열은 0.5 °C(9비트)를 16비트에 왼쪽 정렬하고, 포인터를 유지하므로 포인터를 한 번 써 두면 온도를 계속 읽을 수 있다. 이 mock 은 8비트 레지스터 + 자동 증가 쪽(ST·Bosch 류)에 가깝게 만들었다.

### Part A — 레지스터 접근: 슬레이브 레지스터 파일 + 마스터 세 함수 + 변환

1. 슬레이브 `dev_on_write()` 는 SLA+W 다음 첫 바이트를 포인터로 받고(0x10 이상 NACK), 이후 바이트는 `reg[ptr] = b; ptr++` 로 처리한다. `dev_on_read()` 는 `reg[ptr]` 를 내보내고 `ptr++`, TEMP_MSB 를 내보낼 때 DRDY 를 지운다.
2. 마스터 `i2c_write()`(S SLA+W 데이터 P, 0바이트면 주소만 보내는 probe), `i2c_read()`(S SLA+R 데이터 P, **마지막 바이트만 NACK**), `i2c_write_read()`(S SLA+W 쓰기 **Sr** SLA+R 읽기 P).
3. `tmp_raw_to_mC()`: raw × 1000 / 128 = raw × 125 / 16 을 바닥(floor) 나눗셈으로. 65536 개 전수 검사.

### Part B — 에러 경로와 "STOP + START 는 왜 위험한가"

1. 주소 NACK(장치 없음) → `I2C_ENACK_ADDR`, 맵 밖 포인터 → `I2C_ENACK_DATA`, 둘 다 즉시 STOP. 인자 오류는 버스를 건드리지 않고 `I2C_EARG`.
2. 같은 WHO_AM_I 읽기를 STOP+START(`i2c_write_then_read`, 주어짐)와 Sr 로 해서 로그 비교: (a) 다른 마스터 M2 가 끼어든다 → 0x28, (b) STOP 에서 포인터를 0 으로 되돌리는 디바이스 → 0x0C. Sr 로는 둘 다 0xA7.
3. 1.992 °C(0x00FF) → 2.000 °C(0x0100) 로 바뀌는 순간 MSB 와 LSB 를 따로 읽으면 0x0000 = **0 m°C** 가 나온다. 한 트랜잭션 burst 로는 1992.

### Part C — 센서 드라이버

1. `tmp_init()`: WHO_AM_I 확인(틀리면 CONFIG 를 건드리지 않고 `TMP_EID`) → CONFIG = EN | AVG=2 (0x05) → 되읽어 쓰기 가능 비트만 비교(`TMP_ECFG`). 버스 에러는 그대로 위로.
2. `tmp_read_mC()`: 포인터 0x00 + Sr + 2바이트 burst.
3. `tmp_read_samples()`: 샘플마다 DRDY 폴링(최대 8번, 넘으면 `TMP_ETIMEOUT`) → `tmp_read_mC`. 6개 값이 빠짐·중복 없이 순서대로 나오는지, EN 을 안 켜면 정확히 8번 폴링 후 포기하는지, 중간에 장치가 사라지면 에러를 그대로 넘기는지 확인.

## 2. Problem (English)

You are bringing up a digital temperature sensor on an I2C bus (7-bit address `0x48`). It exposes 8-bit registers behind an internal register pointer that auto-increments on every data byte (wrapping `0x0F → 0x00`). `WHO_AM_I` (`0x0F`) reads `0xA7`. Temperature is a big-endian `int16` at `0x00/0x01` with 1 LSB = 1/128 °C.

Implement, on top of the given bus primitives `bus_start()` (emits `S`, or `Sr` if the bus is already owned), `bus_stop()`, `bus_tx(byte) -> ack`, `bus_rx(ack) -> byte`:

```c
int i2c_write(I2cBus *b, uint8_t a7, const uint8_t *w, size_t wn);            // S SLA+W w.. P
int i2c_read(I2cBus *b, uint8_t a7, uint8_t *r, size_t rn);                   // S SLA+R r.. P
int i2c_write_read(I2cBus *b, uint8_t a7, const uint8_t *w, size_t wn,
                   uint8_t *r, size_t rn);                                     // S SLA+W w.. Sr SLA+R r.. P
int32_t tmp_raw_to_mC(int16_t raw);                                           // floor(raw * 1000 / 128)
int tmp_init(TmpDrv *d);            // check ID, write CONFIG, verify by read-back (mask read-only bits)
int tmp_read_mC(TmpDrv *d, int32_t *mC);                                      // one burst read
int tmp_read_samples(TmpDrv *d, int32_t *out, size_t n, size_t *got);         // poll DRDY, bounded
```

Constraints: no dynamic allocation; on any NACK issue STOP and return an error (`-1` address NACK, `-2` data NACK); the master NACKs only the last byte it reads; invalid arguments (`a7 > 0x7F`, zero lengths, NULL) return `-3` without touching the bus. Also implement the slave side: pointer write, register write, register read with auto-increment, NACK a pointer `≥ 0x10`.

Examples (bus log, as a logic-analyzer decoder would print it):

```text
i2c_write_read(0x48, {0x0F}, 1, r, 1)       -> 0,   r = {0xA7}   S 90 A 0F A Sr 91 A A7 N P
i2c_write(0x48, {0x02, 0x05}, 2)            -> 0                 S 90 A 02 A 05 A P
i2c_write_read(0x49, {0x0F}, 1, r, 1)       -> -1                S 92 N P
i2c_write_read(0x48, {0x10}, 1, r, 1)       -> -2                S 90 A 10 N P
tmp_raw_to_mC(3200) = 25000, tmp_raw_to_mC(1) = 7, tmp_raw_to_mC(-1) = -8
```

Follow-ups: Why a repeated START instead of STOP + START? What breaks if you read the MSB and LSB in two transactions? What would you check first if the address is NACKed?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데: 슬레이브 2개(`dev_on_write`, `dev_on_read`), 마스터 3개(`i2c_write`, `i2c_read`, `i2c_write_read`), 변환 1개, 드라이버 3개(`tmp_init`, `tmp_read_mC`, `tmp_read_samples`). 버스 모델(`bus_start/stop/tx/rx`, 로그, `on_free` 훅), 레지스터 get/put 도우미, STOP+START 비교 함수, 하네스는 주어진다. 스텁은 `I2C_EARG`·0·NACK 를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:344  rc == I2C_OK && r[0] == TMP_ID
FAIL starter.c:345  log
   got : 
   want: S 90 A 0F A Sr 91 A A7 N P
FAIL starter.c:346  bus.n_start == 1 && bus.n_rstart == 1 && bus.n_stop == 1
…
FAIL starter.c:555  rc == I2C_ENACK_ADDR && got == 0
FAIL starter.c:566  bad == 0
8/63 passed
```

(exit 1. 통과한 8개는 "인자 오류면 `I2C_EARG` + 버스 무변화" 7개와, 찢어진 읽기 실험에서 스텁이 우연히 0 을 낸 1개다.)

## 4. 힌트

<details><summary>힌트 1 — 슬레이브: "첫 바이트냐 아니냐"는 상태로 구분한다</summary>

`dev_addr()` 가 SLA+W 를 받으면 상태를 `DS_PTR` 로 둔다. `dev_on_write()` 에서 `DS_PTR` 이면 그 바이트가 포인터다(범위 검사 → `DS_WDATA`). `DS_WDATA` 면 `dev_reg_put(d, d->ptr, b)` 후 `ptr = (ptr + 1) & 0x0F`. 포인터 자체는 상태 기계 밖의 **레지스터**라 START/Sr 로 지워지지 않는다 — 그래서 Sr 뒤의 읽기가 이어받는다.

</details>

<details><summary>힌트 2 — 마스터: 세 함수의 뼈대는 같다</summary>

`bus_start` → `bus_tx(SLA)` 가 NACK 면 `bus_stop` 후 `I2C_ENACK_ADDR` → 데이터 루프. 쓰기 루프는 `bus_tx` 가 NACK 면 `bus_stop` 후 `I2C_ENACK_DATA`. 읽기 루프는 `r[i] = bus_rx(b, i + 1 < rn)` — 마지막에만 `false`(NACK). `i2c_write_read` 는 쓰기 루프 다음에 STOP 대신 `bus_start` 를 한 번 더 부르면 된다. 버스가 이미 busy 라 모델이 알아서 `Sr` 로 찍는다.

</details>

<details><summary>힌트 3 — raw → m°C 는 정수로, 음수 쪽 반올림 방향을 정한다</summary>

1000/128 = 125/16. `int32_t x = raw * 125` 는 최대 |32768 × 125| = 4,096,000 이라 넘치지 않는다. C 의 `/` 는 0 쪽으로 자른다(truncation) → −1 이 −7 이 된다. 바닥을 원하면 음수일 때 `-((-x + 15) / 16)`. 기준 구현 `floor(raw * 7.8125)` 는 double 에서 정확하다 (7.8125 = 125/16, 분모가 2의 거듭제곱).

</details>

<details><summary>힌트 4 — 드라이버: 되읽기 비교는 마스크로</summary>

CONFIG 를 0x05 로 쓰고 되읽으면 0x85 가 올 수 있다 — 쓰자마자 변환이 끝나 DRDY(bit7, 읽기 전용)가 섰기 때문이다. `(cfg & CFG_RW_MASK) != 0x05` 로 비교한다. 폴링 루프는 "DRDY=0 인 읽기 횟수"를 세서 `TMP_MAX_POLL` 에 닿으면 포기 — 무한 루프 금지.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 7비트 주소인가 8비트 표기인가(0x48 vs 0x90), 레지스터 폭(8/16비트), 자동 증가 여부와 랩, 멀티바이트 값의 바이트 순서(MSB 먼저?), 멀티 마스터·다른 태스크와 버스 공유 여부, DRDY/인터럽트 핀 유무 |
| 2. 프로토콜 규칙 | 포인터 쓰기 → Sr → 읽기. 마스터는 마지막 바이트에 NACK, 그 다음 STOP. 어떤 NACK 이든 즉시 STOP |
| 3. 코드 | 아래 Part A/B/C |
| 4. 소리 내어 말할 테스트 | ID 읽기 로그, burst 쓰기·읽기, 포인터 랩, 주소/데이터 NACK, 끼어드는 마스터, 찢어진 읽기, DRDY 타임아웃 |
| 5. 비용 | 레지스터 n 바이트 읽기 = (1 + 1 + 1 + n) 바이트 × 9클럭 + S·Sr·P. 400 kHz 에서 2바이트 읽기 = 5바이트 × 9클럭 = 45클럭 ≈ 113 µs + S·Sr·P 조건 시간. RAM 은 호출자 버퍼뿐 |

### Part A — 한 트랜잭션을 한 줄씩 따라가기

`i2c_write_read(0x48, {0x0F}, 1, r, 1)` 을 모델에서 한 단계씩:

| 단계 | 마스터 호출 | 로그 토큰 | SDA 를 정한 쪽 | 슬레이브 상태 | ptr | busy |
|---|---|---|---|---|---|---|
| 1 | `bus_start` | `S` | 마스터 (SCL high 중 ↓) | ADDR | (이전 값) | 1 |
| 2 | `bus_tx(0x90)` | `90 A` | 마스터 8비트, 슬레이브 ACK | PTR | — | 1 |
| 3 | `bus_tx(0x0F)` | `0F A` | 마스터 8비트, 슬레이브 ACK | WDATA | **0x0F** | 1 |
| 4 | `bus_start` | `Sr` | 마스터 | ADDR | 0x0F (유지) | 1 (유지) |
| 5 | `bus_tx(0x91)` | `91 A` | 마스터 8비트, 슬레이브 ACK | RDATA | 0x0F | 1 |
| 6 | `bus_rx(false)` | `A7 N` | 슬레이브 8비트, 마스터 NACK | IDLE | 0x00 (랩) | 1 |
| 7 | `bus_stop` | `P` | 마스터 (SCL high 중 ↑) | IDLE | 0x00 | 0 → 기다리던 마스터 가능 |

주소 바이트 0x90 = 0x48 << 1 | 0(W), 0x91 = 0x48 << 1 | 1(R). 데이터시트·로직 애널라이저·HAL 이 이 두 표기를 섞어 쓴다 (7. 실수 1).

비트 수준으로 보면 Sr 과 STOP+START 의 차이는 이렇다. 아래 파형은 모델의 마스터 순서(SCL low → SDA 정함 → SCL high → SCL low)대로 0x0F(0000 1111, MSB 먼저), 슬레이브 ACK, 그 다음 0x91(1001 …)의 앞 4비트를 그린 것이다.

```svg
<svg viewBox="0 0 760 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="8" y="30" fill="currentColor" font-weight="bold">(a) repeated START:  … 0F  A  Sr  91 …   (버스를 놓지 않는다)</text>
<circle cx="450" cy="95" r="4" class="f-acc" stroke="currentColor"/>
<text x="8" y="58" fill="currentColor">SCL</text>
<text x="8" y="102" fill="currentColor">SDA</text>
<polyline points="96,62 105,62 105,40 123,40 123,62 141,62 141,40 159,40 159,62 177,62 177,40 195,40 195,62 213,62 213,40 231,40 231,62 249,62 249,40 267,40 267,62 285,62 285,40 303,40 303,62 321,62 321,40 339,40 339,62 357,62 357,40 375,40 375,62 393,62 393,40 411,40 411,62 432,62 432,40 468,40 468,62 489,62 489,40 507,40 507,62 525,62 525,40 543,40 543,62 561,62 561,40 579,40 579,62 597,62 597,40 615,40 615,62 634,62" fill="none" stroke="currentColor" stroke-width="1.6"/>
<polyline points="96,106 245,106 245,84 389,84 389,106 425,106 425,84 450,84 450,106 485,106 485,84 521,84 521,106 593,106 593,84 634,84" fill="none" stroke="currentColor" stroke-width="1.6"/>
<path d="M114 40V110M150 40V110M186 40V110M222 40V110M258 40V110M294 40V110M330 40V110M366 40V110M402 40V110M498 40V110M534 40V110M570 40V110M606 40V110" stroke="currentColor" stroke-dasharray="2 3" opacity="0.5"/>
<rect x="105" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="114" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="141" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="150" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="177" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="186" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="213" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="222" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="249" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="258" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="285" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="294" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="321" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="330" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="357" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="366" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="393" y="114" width="18" height="17" class="f-ok" stroke="currentColor"/><text x="402" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<text x="450" y="126" fill="currentColor" text-anchor="middle" font-weight="bold">Sr</text>
<rect x="489" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="498" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="525" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="534" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="561" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="570" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="597" y="114" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="606" y="127" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<text x="8" y="180" fill="currentColor" font-weight="bold">(b) STOP + START:  … 0F  A  P  S  91 …   (사이에 버스가 빈다)</text>
<circle cx="446" cy="245" r="4" class="f-warn" stroke="currentColor"/>
<rect x="460" y="184" width="66" height="78" class="f-warn" opacity="0.35"/><text x="493" y="209" fill="currentColor" text-anchor="middle" font-size="12">버스 빔</text>
<text x="493" y="225" fill="currentColor" text-anchor="middle" font-size="12">M2 가능</text>
<circle cx="540" cy="245" r="4" class="f-acc" stroke="currentColor"/>
<text x="8" y="208" fill="currentColor">SCL</text>
<text x="8" y="252" fill="currentColor">SDA</text>
<polyline points="96,212 105,212 105,190 123,190 123,212 141,212 141,190 159,190 159,212 177,212 177,190 195,190 195,212 213,212 213,190 231,190 231,212 249,212 249,190 267,190 267,212 285,212 285,190 303,190 303,212 321,212 321,190 339,190 339,212 357,212 357,190 375,190 375,212 393,212 393,190 411,190 411,212 432,212 432,190 550,190 550,212 569,212 569,190 587,190 587,212 605,212 605,190 623,190 623,212 641,212 641,190 659,190 659,212 677,212 677,190 695,190 695,212 714,212" fill="none" stroke="currentColor" stroke-width="1.6"/>
<polyline points="96,256 245,256 245,234 389,234 389,256 446,256 446,234 540,234 540,256 565,256 565,234 601,234 601,256 673,256 673,234 714,234" fill="none" stroke="currentColor" stroke-width="1.6"/>
<path d="M114 190V260M150 190V260M186 190V260M222 190V260M258 190V260M294 190V260M330 190V260M366 190V260M402 190V260M578 190V260M614 190V260M650 190V260M686 190V260" stroke="currentColor" stroke-dasharray="2 3" opacity="0.5"/>
<rect x="105" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="114" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="141" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="150" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="177" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="186" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="213" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="222" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="249" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="258" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="285" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="294" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="321" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="330" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="357" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="366" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="393" y="264" width="18" height="17" class="f-ok" stroke="currentColor"/><text x="402" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
<text x="446" y="276" fill="currentColor" text-anchor="middle" font-weight="bold">P</text>
<text x="540" y="276" fill="currentColor" text-anchor="middle" font-weight="bold">S</text>
<rect x="569" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="578" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<rect x="605" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="614" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="641" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="650" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
<rect x="677" y="264" width="18" height="17" class="f-acc" stroke="currentColor"/><text x="686" y="277" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
<text x="8" y="324" fill="currentColor" font-size="12">비트 칸: 강조색 = 마스터가 SDA 를 정한 비트, 다른 색(A) = 슬레이브가 SDA 를 당긴 ACK. 점선 = SCL high, 비트를 읽는 시점.</text>
<text x="8" y="342" fill="currentColor" font-size="12">규칙: SDA 는 SCL low 동안만 바뀐다. SCL high 중 SDA 가 내려가면 START(S·Sr), 올라가면 STOP(P).</text>
</svg>
```

- **Sr**: ACK 클럭이 끝나면 SDA 를 놓아 high 로 만들고(SCL low 동안), SCL 을 올린 뒤 SCL high 중 SDA 를 내린다. 이 마지막 엣지가 START 와 똑같아서 슬레이브는 "주소부터 다시" 상태가 된다. 그러나 그 앞에 STOP(SCL high 중 SDA ↑)이 없으므로 버스를 보는 모든 노드는 **버스가 계속 사용 중**이라고 본다.
- **P + S**: SDA 를 low 로 잡은 채 SCL 을 올리고 SDA 를 올린다 → STOP. 이제 버스는 비었다. 다음 START 전에는 bus free time(t_BUF)이 필요하다 — UM10204 표의 대표값 Standard-mode 4.7 µs, Fast-mode 1.3 µs (스펙 표 확인 권장). 이 틈에 다른 마스터가 START 할 수 있다.

슬레이브 쪽 상태 기계:

```svg
<svg viewBox="0 0 760 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b07a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">mock 센서 슬레이브 상태 기계 (dev_start / dev_addr / dev_on_write / dev_on_read / dev_stop)</text>
  <g transform="translate(0,14)">
  <rect x="20" y="140" width="90" height="40" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="65" y="165" fill="currentColor" text-anchor="middle" font-family="monospace">IDLE</text>
  <rect x="200" y="140" width="90" height="40" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="245" y="165" fill="currentColor" text-anchor="middle" font-family="monospace">ADDR</text>
  <rect x="420" y="60" width="90" height="40" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="465" y="85" fill="currentColor" text-anchor="middle" font-family="monospace">PTR</text>
  <rect x="620" y="60" width="90" height="40" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="665" y="85" fill="currentColor" text-anchor="middle" font-family="monospace">WDATA</text>
  <rect x="420" y="220" width="90" height="40" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="465" y="245" fill="currentColor" text-anchor="middle" font-family="monospace">RDATA</text>
  <line x1="110" y1="160" x2="198" y2="160" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="116" y="134" fill="currentColor" font-size="12">S 또는 Sr</text>
  <text x="116" y="152" fill="currentColor" font-size="12">(어디서든)</text>
  <line x1="290" y1="150" x2="418" y2="85" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="300" y="104" fill="currentColor" font-size="12">주소 일치 + W</text>
  <text x="300" y="120" fill="currentColor" font-size="12">/ ACK</text>
  <line x1="290" y1="170" x2="418" y2="235" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="300" y="240" fill="currentColor" font-size="12">주소 일치 + R</text>
  <text x="300" y="256" fill="currentColor" font-size="12">/ ACK</text>
  <line x1="510" y1="80" x2="618" y2="80" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="516" y="54" fill="currentColor" font-size="12">b &lt; 0x10 / ACK</text>
  <text x="516" y="72" fill="currentColor" font-size="12" font-family="monospace">ptr = b</text>
  <path d="M640 100 C 630 140, 700 140, 690 102" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="548" y="150" fill="currentColor" font-size="12" font-family="monospace">reg[ptr] = b; ptr++ / ACK</text>
  <path d="M440 260 C 430 300, 500 300, 490 262" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="520" y="290" fill="currentColor" font-size="12">마스터 ACK → 다음 바이트</text>
  <text x="520" y="306" fill="currentColor" font-size="12" font-family="monospace">out = reg[ptr]; ptr++</text>
  <path d="M465 60 C 465 40, 65 40, 65 138" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#b07a)"/>
  <text x="160" y="30" fill="currentColor" font-size="12">b ≥ 0x10 / NACK (맵 밖)</text>
  <path d="M420 255 C 250 320, 80 300, 62 182" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#b07a)"/>
  <text x="120" y="316" fill="currentColor" font-size="12">마스터 NACK → SDA 놓고 대기</text>
  <path d="M215 180 C 190 230, 110 230, 90 182" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#b07a)"/>
  <text x="140" y="240" fill="currentColor" font-size="12">주소 불일치</text>
  <text x="560" y="190" fill="currentColor" font-size="12">P → IDLE (어느 상태에서든).</text>
  <text x="560" y="206" fill="currentColor" font-size="12">옵션: ptr = 0, 변환 1회 진행</text>
  <text x="560" y="236" fill="currentColor" font-size="12" font-weight="bold">ptr 는 상태가 아니다:</text>
  <text x="560" y="252" fill="currentColor" font-size="12">S·Sr 로 지워지지 않는다</text>
  <text x="560" y="268" fill="currentColor" font-size="12">→ Sr 뒤 읽기가 이어받는다</text>
</g>
</svg>
```

주의할 점 두 가지. (1) 마스터가 NACK 하면 슬레이브는 SDA 를 놓고 다음 START/STOP 만 기다린다. 마스터가 그 뒤에 또 읽으면(버그) 아무도 SDA 를 당기지 않아 0xFF 가 읽힌다 — `bus_rx` 가 그렇게 모델링한다. (2) 슬레이브가 포인터 바이트를 NACK 하면 같은 트랜잭션에서 더 쓰지 않는다 (`bus_tx` 가 `sel` 을 지운다).

**raw → m°C.** 1 LSB = 1/128 °C 이므로 m°C = raw × 1000/128 = raw × 125/16. 정수로 계산하고 16 으로 나눌 때 방향을 정해야 한다. 이 레슨은 **바닥(floor)** 을 택했다: (a) 모든 raw 에서 결과가 참값보다 0 이상 1 m°C 미만 작다 — 오차의 부호가 일정하다. (b) 드라이버들이 흔히 쓰는 `(raw * 125) >> 4` 와 같은 값이다 (단, C11 에서 음수의 `>>` 는 구현 정의라 이 코드는 나눗셈으로 썼다). (c) 단조 증가. 예: 3200 → 25000, 1 → 7 (7.8125), −1 → −8 (−7.8125), 32767 → 255992, −32768 → −256000. 테스트는 65536 개 전부를 `floor(raw * 7.8125)`(double 에서 정확)와 대조한다.

### Part B — 에러 경로

| 상황 | 로그 (실제 출력) | 반환 |
|---|---|---|
| 0x49 에 아무도 없음 | `S 92 N P` | `I2C_ENACK_ADDR`, 버퍼 그대로 |
| 포인터 0x10 (맵 밖) | `S 90 A 10 N P` | `I2C_ENACK_DATA` |
| 0x49 에서 읽기 | `S 93 N P` | `I2C_ENACK_ADDR` |
| `wn = 0`, `rn = 0`, `NULL`, `a7 = 0x80` | (없음) | `I2C_EARG`, `n_start` 변화 없음 |

NACK 뒤에 **반드시 STOP**. STOP 없이 반환하면 버스는 "busy" 로 남고, 다음 `bus_start` 가 Sr 로 찍힌다 — 멀티 마스터 버스에서는 남들이 영원히 기다린다. 이 줄을 빼먹은 변형을 돌려 보면 로그 비교 3개가 깨진다 (`S 92 N` 뒤에 `P` 가 없음).

### Part B — STOP + START vs Sr: solution.c 실제 출력

```text
--- WHO_AM_I 읽기, M2 가 버스가 비기를 기다리는 중 ---
  STOP+START : S 90 A 0F A P S(M2) 90 A 02 A Sr 91 A 00 N P S 91 A 28 N P   -> 0x28
  Sr         : S 90 A 0F A Sr 91 A A7 N P S(M2) 90 A 02 A Sr 91 A 00 N P   -> 0xA7
--- 포인터를 STOP 에서 리셋하는 디바이스 ---
  STOP+START : S 90 A 0F A P S 91 A 0C N P   -> 0x0C
  Sr         : S 90 A 0F A Sr 91 A A7 N P   -> 0xA7
--- 1.992 °C → 2.000 °C 로 바뀌는 순간 읽기 ---
  따로 2번   : S 90 A 00 A Sr 91 A 00 N P S 90 A 01 A Sr 91 A 00 N P   -> 0 m°C
  burst 1번  : S 90 A 00 A Sr 91 A 00 A FF N P   -> 1992 m°C
```

**끼어드는 마스터.** 모델의 `bus->on_free` 훅은 "STOP 을 보고 버스가 빈 것을 안 다른 마스터"다. STOP 직후 한 번 불린다. M2 는 자기 나름 정상인 일(CONFIG 읽기)을 했을 뿐인데 공유 상태인 **포인터**를 옮겼다. Sr 버전에서는 우리 트랜잭션이 끝날 때까지 STOP 이 없으므로 M2 는 그 뒤에 들어온다. 두 마스터가 *동시에* START 하는 경우는 중재(arbitration)가 가른다 — COMM-M02. 같은 문제가 단일 마스터 펌웨어에도 있다: RTOS 태스크 둘이 I2C 컨트롤러를 공유하면 "포인터 쓰기"와 "읽기"가 별도 API 호출인 순간 사이에 다른 태스크가 들어올 수 있다. 해결은 (1) 한 번의 호출로 Sr 트랜잭션을 끝내는 API(`HAL_I2C_Mem_Read`, Linux `i2c_transfer` 의 메시지 2개), (2) 버스 mutex — 둘 다다.

**STOP 에서 포인터를 리셋하는 디바이스.** 모델 옵션 `ptr_reset_on_stop`. 실제로 데이터시트에 "읽기는 repeated START 로 해야 한다" 고 적힌 부품이 있다 (어떤 부품이 STOP 에서 무엇을 초기화하는지는 부품마다 다르다 — 데이터시트 확인). 이런 부품에 STOP+START 를 쓰면 매번 TEMP_MSB 가 읽힌다. ID 검사가 이 실수를 bring-up 첫날 잡아 준다.

**찢어진 읽기(torn read).** 16비트 값을 두 트랜잭션으로 읽으면 사이에 변환이 끝날 수 있다. 0x00FF → 0x0100 에서 MSB 는 옛 값(0x00), LSB 는 새 값(0x00)을 받아 0x0000 = 0 °C — 참값 1.992 °C 나 2.000 °C 와 2 °C 가까이 차이 난다. 한 트랜잭션 burst 는 모델의 섀도 규칙(START~STOP 동안 갱신 안 함) 덕분에 항상 한 변환의 두 바이트다. 실제 부품의 대응은 다양하다(대표 예, 데이터시트 확인 권장): Bosch BMP280 류의 burst read 중 데이터 레지스터 잠금(shadowing), ST LIS3DH 류의 BDU(block data update) 비트, 16비트 레지스터를 한 단위로 래치하는 TMP117 류. 어느 경우든 **한 트랜잭션 burst** 가 전제다.

### Part C — 드라이버

```text
tmp_init:  S 90 A 0F A Sr 91 A A7 N P          ID 확인
           S 90 A 02 A 05 A P                  CONFIG = EN | AVG 2
           S 90 A 02 A Sr 91 A 85 N P          되읽기: 0x85 & 0x07 == 0x05 → OK (DRDY 는 무시)
```

순서의 이유: **ID 를 먼저** 본다. 다른 부품(같은 주소의 EEPROM 등)에 CONFIG 를 쓰는 사고를 막는다 — 테스트는 ID 가 0x55 이면 CONFIG 가 그대로 0x00 이고 트랜잭션이 1개뿐인지 확인한다. **되읽기**는 쓰기 보호·다른 리비전·버스 문제(`cfg_stuck` 옵션)를 잡는다.

연속 읽기의 트랜잭션 흐름 (`conv_every = 3`, 실제로 센 수):

| 트랜잭션 | 내용 | 그 STOP 에서 디바이스 |
|---|---|---|
| 1–3 | init (ID, CONFIG 쓰기, 되읽기) | EN=0 → 셈 안 함 / 1 / 2 |
| 4 | CONFIG 폴링 → DRDY=0 | 3 → 변환: TEMP = 25.000 °C, DRDY=1 |
| 5 | CONFIG 폴링 → DRDY=1 | 1 |
| 6 | TEMP burst → 25000 m°C, DRDY 지움 | 2 |
| 7–9 | 폴(0) · 폴(1) · 읽기 → 25500 | … |

샘플 6개에 3 + 6 × 3 = **21** 트랜잭션, 결과 `25000 25500 -500 -40000 125000 7` (solution.c 출력 그대로). EN 을 안 켜면 변환이 없으므로 DRDY 폴링이 정확히 8번(`TMP_MAX_POLL`) 돌고 `TMP_ETIMEOUT`, `got = 0`. 장치 주소가 중간에 사라지면 첫 폴링에서 `I2C_ENACK_ADDR` 을 그대로 돌려준다 — 드라이버가 버스 에러를 자기 에러로 바꾸지 않아야 위에서 "센서 없음"과 "센서 고장"을 구분한다 (계층별 에러 변환 규칙은 COMM-M12).

실제 펌웨어에서는 폴링 사이에 변환 시간만큼 쉬거나(RTOS `vTaskDelay`), 센서의 ALERT/DRDY 핀을 EXTI 인터럽트로 받아 그때 읽는다. 인터럽트 방식이면 `tmp_read_mC` 를 ISR 에서 직접 부르지 말고 플래그·큐로 태스크에 넘긴다 — I2C 트랜잭션은 수십~수백 µs 걸리고, HAL 의 블로킹 I2C 함수는 ISR 에서 쓰기에 맞지 않는다. 비동기 큐는 COMM-M13.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 63/63 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

## 7. 자주 하는 실수

**1. 7비트 주소와 8비트 표기를 섞는다.** 데이터시트 "0x48", 로직 애널라이저 "0x90 Write", STM32 HAL 의 `DevAddress` 는 7비트 주소를 왼쪽으로 한 칸 민 값을 받는다(HAL 문서 확인 권장), Linux `i2c_client->addr` 는 7비트다. 0x90 을 7비트 자리에 넣으면 이 레슨 API 는 `I2C_EARG` 를 돌려주지만, 실제 HAL 에서는 엉뚱한 주소로 나가 NACK 이 난다. 주소 NACK 이면 먼저 이것부터 의심한다.

**2. 마지막 바이트도 ACK 한다.** `bus_rx(b, true)` 로 다 ACK 하도록 바꿔 돌리면 로그 비교 7개가 깨진다 (`S 90 A 0F A Sr 91 A A7 A P`). 이 모델에서는 로그만 틀리지만, 진짜 버스에서는 슬레이브가 "더 읽겠구나" 하고 다음 바이트의 MSB 를 SDA 에 내놓는다. 그 비트가 0 이면 마스터는 STOP(SDA ↑)을 만들 수 없다 — 버스가 걸린다(COMM-M03 의 9클럭 복구). 규칙: **마지막 바이트 = NACK, 그 다음 STOP 또는 Sr.**

**3. 16비트 값을 레지스터 하나씩 두 번에 읽는다.** "MSB 읽기 함수" 를 두 번 부르는 드라이버는 대부분의 시간 동안 맞다가, 변환이 그 사이에 끝나는 순간 틀린다. 이 레슨의 예에서 0 m°C (참값 1992/2000). 드문 만큼 현장에서 잡기 어렵다. 한 번의 burst + 부품의 섀도/BDU 기능.

**4. 음수 변환을 0 쪽으로 자른다.** `return raw * 125 / 16;` 은 음수에서 0 쪽으로 잘린다. 65536 개 전수 비교에서 **30720 개**가 바닥 기준과 다르다 (음수 32768 개 중 16 의 배수가 아닌 것 전부, 예: −1 → −7). `(raw / 128) * 1000` 은 소수를 버려 **65024 개**가 틀리고, raw 를 `uint16_t` 로 읽으면 음수 **32768 개**가 전부 틀린다(−0.5 °C 가 511.5 °C 로). 이 레슨 작업 중 세 변형 모두 실제로 돌려서 센 수다.

**5. 되읽기를 마스크 없이 비교한다.** `if (cfg != 0x05)` 는 DRDY 가 선 순간 0x85 를 보고 `TMP_ECFG` 를 낸다 — 이 변형은 테스트 2개를 깬다. 읽기 전용·자동으로 바뀌는 비트(상태 플래그)는 비교에서 빼야 한다.

**6. 포인터 랩을 빠뜨린다.** 슬레이브에서 `ptr++` 만 하면 0x0F 다음이 0x10 이 되어 이후 읽기는 0, 쓰기는 무시된다. 테스트의 "0x0E 부터 3바이트 읽기"와 "0x0F 부터 4바이트 쓰기 → CONFIG 에 도달" 이 잡는다. 실제 부품의 랩 규칙(전체 랩 / 블록 안에서 랩 / 증가 안 함)은 데이터시트마다 다르다.

**7. 드라이버의 레지스터 읽기를 `i2c_write` + `i2c_read` 로 짠다.** 단일 마스터 벤치에서는 잘 돈다. 이 레슨 하네스에서 `tmp_reg_read` 를 STOP+START 판으로 바꾸면 로그·트랜잭션 수·연속 읽기 값 등 8개가 깨진다 — 연속 읽기에서는 STOP 이 늘어나 변환 타이밍이 밀리면서 값 순서가 틀어진다.

**8. 폴링 루프에 상한이 없다 / 하나 어긋난다.** `while (!(cfg & DRDY)) read(...)` 는 EN 을 안 켰거나 센서가 죽으면 영원히 돈다. `polls++ > MAX` 처럼 비교를 바꾸면 10번 폴링한다 — 테스트가 정확히 8번을 확인해서 잡는다. 실제로는 폴링 횟수보다 **시간** 상한(변환 시간 × 여유)이 더 정확하다.

## 8. 면접 Q&A

<details><summary>Q1. 레지스터를 읽을 때 왜 STOP + START 대신 repeated START 를 쓰나요? 단일 마스터면 상관없지 않나요?</summary>

세 가지 이유. (1) **원자성**: STOP 을 내면 버스가 비고, 다른 마스터가 들어와 디바이스의 포인터를 옮길 수 있다 — 이 레슨 로그에서 WHO_AM_I 대신 0x28 이 읽혔다. 단일 마스터여도 같은 컨트롤러를 쓰는 RTOS 태스크 사이에서 같은 일이 생긴다. (2) **디바이스 요구**: STOP 에서 내부 상태(포인터 등)를 초기화하는 부품이 있고, 데이터시트가 "repeated START 필수"라고 적기도 한다. (3) **표준**: SMBus 의 Read Byte/Word 프로토콜 자체가 `S Addr Wr A Comm A Sr Addr Rd A Data NA P` 다. Linux `i2c_smbus_read_byte_data()`, STM32 HAL `HAL_I2C_Mem_Read()` 가 이 형태를 만든다. 비용은 없다 — 오히려 STOP + t_BUF 만큼 빠르다.

꼬리: "그럼 Sr 로 버스를 계속 잡고 있으면 되나?" → 오래 잡으면 다른 마스터가 굶는다. 레지스터 접근 하나를 한 트랜잭션으로 묶는 정도가 맞다. read-modify-write 처럼 읽고 쓰는 것을 묶어야 하면 연습 3 처럼 `S W Sr R Sr W P` 도 가능하지만, 여러 레지스터에 걸친 일관성은 소프트웨어 락의 일이다.

</details>

<details><summary>Q2. 마스터는 왜 마지막 바이트에 NACK 을 보내나요? ACK 하면 어떻게 되나요?</summary>

읽기에서는 슬레이브가 SDA 를 쥐고 있다. 마스터의 ACK 는 "한 바이트 더", NACK 는 "그만, SDA 놓아라" 다. NACK 를 받은 슬레이브는 SDA 를 놓고, 마스터는 SDA 를 자유롭게 움직여 STOP(SCL high 중 SDA ↑) 또는 Sr(SCL high 중 SDA ↓)을 만든다. 마지막에 ACK 하면 슬레이브는 다음 바이트의 MSB 를 내놓는다. 그 비트가 0 이면 SDA 가 low 로 잡혀 STOP 이 안 만들어지고, 버스가 걸린 것처럼 보인다. 마스터 리셋 뒤 SDA stuck-low 와 같은 상황이라 9클럭 + STOP 복구(COMM-M03)가 필요하다.

</details>

<details><summary>Q3. 로직 애널라이저에서 Sr 과 STOP+START 는 어떻게 구분해 보이나요?</summary>

Sr: 9번째 클럭(ACK) 뒤 SCL low 동안 SDA 가 high 로 올라가고, SCL 이 올라간 상태에서 SDA 가 떨어진다. 그 앞에 STOP 이 없다. sigrok/Saleae 의 I2C 디코더는 이를 "Start repeat"/"Sr" 로 따로 표시한다(HWLAB-M06). STOP+START: SCL high 중 SDA 상승(P), 그 다음 두 선 모두 high 인 구간(bus free, t_BUF 이상)이 보이고, 그 뒤 SCL high 중 SDA 하강(S). 이 레슨의 파형 그림 (a)/(b) 가 정확히 그 차이다. 측정 팁: 디코더 결과만 믿지 말고 Sr 직전의 SDA 상승이 SCL low 동안인지 확인한다 — SCL high 중에 올라가면 그건 STOP 이다.

</details>

<details><summary>Q4. 16비트 온도를 읽는 코드에서 무엇을 조심하나요?</summary>

(1) **한 트랜잭션 burst**: MSB·LSB 를 따로 읽으면 사이에 변환이 끝나 값이 찢어진다(이 레슨: 1.992→2.000 °C 경계에서 0 °C). (2) 부품의 일관성 기능을 켠다 — BDU, 섀도, 16비트 래치 (부품마다 다름). (3) 바이트 순서: 이 센서와 TMP117 류는 MSB 먼저, SMBus Read Word 는 LSB 먼저다. Linux 에서는 `i2c_smbus_read_word_data()` 가 LSB-first 라 big-endian 레지스터에는 `i2c_smbus_read_word_swapped()` 를 쓴다. (4) 부호 확장: `uint8_t` 두 개를 합친 값을 `int16_t` 로 해석해야 음수가 맞다 — `uint16_t` 로 두면 −0.5 °C 가 511.5 °C 가 된다. (5) 스케일은 정수로: m°C(int32)로 바꾸면 FPU 없는 MCU 에서도 빠르고 로그·비교가 쉽다.

</details>

<details><summary>Q5. 드라이버 init 에서 무엇을 어떤 순서로 확인하나요?</summary>

ID → 리셋(필요하면) → 설정 쓰기 → 되읽기 → 첫 데이터 유효성. ID 를 먼저 보는 이유는 잘못된 부품에 쓰기를 하지 않기 위해서다(같은 주소대에 EEPROM 이 있으면 설정 쓰기가 EEPROM 내용을 바꾼다). 되읽기는 쓰기 보호, 리비전 차이, 버스의 비트 오류를 잡는다. 비교는 쓰기 가능한 비트만(`& CFG_RW_MASK`). 에러는 계층을 지켜 올린다: 버스 NACK 은 `I2C_ENACK_ADDR` 그대로, ID 불일치는 `TMP_EID` — 위에서 "배선/전원 문제"와 "다른 부품"을 구분할 수 있다.

</details>

<details><summary>Q6. 센서가 주소에 NACK 합니다. 무엇부터 보나요?</summary>

빠른 것부터: (1) 주소 표기 — 7비트/8비트 혼동(0x48 vs 0x90), 주소 핀(ADDR/SA0) 배선 상태. (2) 전원·리셋 — 센서 VDD, 파워업 후 시간, 리셋 핀. (3) 버스 레벨 — 두 선이 idle 에서 high 인가. 풀업이 없거나 너무 크면 엣지가 느려 비트가 틀린다(HWLAB-B03). SDA 가 low 에 붙어 있으면 이전 트랜잭션 중 리셋된 슬레이브가 잡고 있는 것 → 9클럭 복구(COMM-M03). (4) 스캔 — Linux 면 `i2cdetect -y <bus>`, MCU 면 연습 4 의 스캔 함수로 0x08~0x77 을 훑는다. (5) 스코프 — 주소 바이트가 실제로 나가는지, 9번째 클럭에 SDA 가 내려오는지. ACK 자리에서 SDA 가 중간 레벨이면 두 드라이버가 싸우거나 레벨 시프터 문제다.

</details>

<details><summary>Q7. 이 드라이버를 STM32 에서 인터럽트/DMA 로 돌린다면 구조가 어떻게 바뀌나요?</summary>

블로킹 `i2c_write_read` 는 한 함수 안에서 START→…→STOP 을 끝낸다. 인터럽트판은 같은 시퀀스를 **상태 기계**로 쪼갠다: 이벤트 인터럽트(START 보냄, 주소 ACK, TX 빈칸, RX 찼음, STOP)마다 다음 단계를 진행하고, 끝나면 완료 콜백. HAL 의 `HAL_I2C_Mem_Read_IT/_DMA` 가 그 형태다. 읽기 바이트가 많으면 DMA 가 RX 를 받고, 마지막 바이트의 NACK·STOP 타이밍은 컨트롤러가 처리한다 (패밀리마다 주의점이 달라 RM 확인 권장). DRDY 폴링 대신 센서의 DRDY 핀 EXTI → 태스크 깨우기 → 비동기 읽기 → 콜백에서 큐로. 여러 디바이스가 버스를 공유하면 트랜잭션 큐(COMM-M13)와 mutex 가 필요하다.

</details>

<details><summary>Q8. Linux 커널 드라이버라면 이 시퀀스를 어떻게 쓰나요?</summary>

세 층이 있다. (1) `i2c_transfer(adap, msgs, 2)`: `msgs[0]` = 쓰기(포인터), `msgs[1]` = `I2C_M_RD` 읽기. 메시지 사이를 repeated START 로 잇고 끝에 STOP 한 번, 성공하면 메시지 수를 돌려준다(연습 2). (2) SMBus 헬퍼: `i2c_smbus_read_byte_data(client, reg)`, `i2c_smbus_read_i2c_block_data()` — 어댑터가 순수 SMBus 여도 동작한다. (3) `regmap`: `regmap_read`, `regmap_bulk_read`, `regmap_update_bits` 가 캐시·락·엔디언을 처리한다. 버스 공유는 어댑터 락이 처리하므로 드라이버는 "한 접근 = 한 호출" 만 지키면 된다.

</details>

## 9. 추가 연습

### 연습 1 — 경보 한계 쓰기: m°C → raw 역변환

`int16_t tmp_mC_to_raw(int32_t mC)` (가장 가까운 정수로 반올림, int16 범위로 자르기)와 `tmp_set_limits(d, low_mC, high_mC)`(THIGH·TLOW 4바이트를 **한 트랜잭션** burst 로 쓰고 되읽어 확인, low > high 면 버스를 건드리지 않고 `TMP_EARG`)를 짜라. raw → m°C → raw 가 65536 개 전부 제자리인지 보여라.

<details><summary>답 보기</summary>

```c
int64_t num = (int64_t)mC * 16;                       /* raw = mC × 16 / 125 */
int64_t q = num >= 0 ? (num + 62) / 125 : -((-num + 62) / 125);
if (q > INT16_MAX) q = INT16_MAX;
if (q < INT16_MIN) q = INT16_MIN;
return (int16_t)q;
```

125 가 홀수라 `num / 125` 가 정확히 .5 인 경우는 없다(2·num = 125 × 홀수 가 되려면 좌변이 홀수여야 한다) → 반올림 방향 논쟁이 필요 없다. 왕복이 제자리인 이유: 바닥 변환 오차가 1 m°C 미만, raw 간격이 7.8125 m°C 라 |mC/7.8125 − raw| < 0.128 < 0.5. 반대 방향(m°C → raw → m°C)의 오차는 랜덤 20000 개에서 [−4, +3] m°C 로 나왔다. `tmp_set_limits(-1000, 30000)` 의 로그(실제 출력): `S 90 A 03 A 0F A 00 A FF A 80 A P S 90 A 03 A Sr 91 A 0F A 00 A FF A 80 N P` — 30 °C = 3840 = 0x0F00, −1 °C = −128 = 0xFF80. 한 트랜잭션으로 쓰는 이유는 찢어진 읽기와 같다: 센서가 상한만 바뀐 중간 상태로 경보를 판단하지 않게.

</details>

### 연습 2 — Linux 식 `i2c_transfer()`

`I2cMsg { addr, flags(I2C_M_RD), len, buf }` 배열을 받아 메시지 사이는 Sr, 끝에만 P 를 내고 성공하면 메시지 수를 돌려주는 `i2c_transfer(b, msgs, n)` 을 짜라. 버스를 건드리기 전에 모든 메시지를 검사(0바이트 읽기 금지). `i2c_write_read` 와 같은 파형이 나오는지, 읽기 뒤에 쓰기가 오는 3개짜리도 되는지 확인.

<details><summary>답 보기</summary>

```c
for (int i = 0; i < n; i++) {
    I2cMsg *m = &msgs[i];
    bool rd = m->flags & I2C_M_RD;
    bus_start(b);                                     /* i > 0 이면 busy → Sr */
    if (!bus_tx(b, rd ? sla_r(m->addr) : sla_w(m->addr))) { bus_stop(b); return I2C_ENACK_ADDR; }
    for (uint16_t k = 0; k < m->len; k++) {
        if (rd) m->buf[k] = bus_rx(b, k + 1u < m->len);   /* 메시지마다 마지막 바이트 NACK */
        else if (!bus_tx(b, m->buf[k])) { bus_stop(b); return I2C_ENACK_DATA; }
    }
}
bus_stop(b);
return n;
```

실제 출력: 메시지 `[W 03] [R 2바이트] [W 02 05]` → 반환 3, 로그 `S 90 A 03 A Sr 91 A 28 A 00 N Sr 90 A 02 A 05 A P` (읽기 메시지의 마지막 바이트 00 은 NACK, 그 다음 Sr 로 쓰기). 둘째 메시지 주소가 없으면 `S 90 A 0F A Sr 93 N P` 와 `I2C_ENACK_ADDR`. Linux 의 실제 에러 코드(`-ENXIO`, `-EREMOTEIO` 등)는 어댑터 드라이버마다 다르다 — 커널 문서 확인 권장.

</details>

### 연습 3 — read-modify-write 와 lost update

`tmp_update_bits(d, reg, mask, val)` (읽기 → 바꿀 게 없으면 쓰지 않음 → 쓰기)를 짜고, 그 사이에 M2 가 CONFIG 의 AVG 비트를 켜면 무엇이 사라지는지 보여라. 그리고 STOP 없이 `S W reg Sr R old N Sr W reg new P` 로 버스를 쥔 채 하는 `tmp_update_bits_locked()` 로 고쳐라.

<details><summary>답 보기</summary>

실제 출력:

```text
  보통 RMW  : S 90 A 02 A Sr 91 A 00 N P S(M2) 90 A 02 A Sr 91 A 00 N Sr 90 A 02 A 06 A P S 90 A 02 A 01 A P  -> CONFIG 0x01
  Sr 로 쥠  : S 90 A 02 A Sr 91 A 00 N Sr 90 A 02 A 01 A P S(M2) 90 A 02 A Sr 91 A 01 N Sr 90 A 02 A 07 A P  -> CONFIG 0x07
```

보통판은 우리가 읽은 0x00 을 바탕으로 0x01 을 써서 M2 가 켠 0x06 을 지운다(lost update). 잠근 판은 NACK 뒤 STOP 대신 Sr 로 이어 쓰므로 M2 는 우리가 끝난 뒤 0x01 을 읽고 0x07 을 만든다. 같은 MCU 안의 태스크끼리라면 Sr 대신 버스 mutex 로 RMW 전체를 감싸는 것이 보통이다(Linux 의 `regmap_update_bits` 는 regmap 락으로). 바꿀 게 없으면 트랜잭션 1개로 끝나는 것도 테스트한다 — 플래시처럼 쓰기 횟수가 아까운 디바이스에서 중요하다.

</details>

### 연습 4 — 버스 스캔 (i2cdetect 흉내)

0x08~0x77 (0x00–0x07, 0x78–0x7F 는 예약 주소)에 주소만 보내고(`i2c_write(b, a, NULL, 0)`) ACK 한 주소를 모으는 `size_t i2c_scan(b, found, cap)` 을 짜라. 반환은 찾은 총 개수, 저장은 cap 개까지(snprintf 식).

<details><summary>답 보기</summary>

```c
size_t cnt = 0;
for (uint8_t a = 0x08; a <= 0x77; a++)
    if (i2c_write(b, a, NULL, 0) == I2C_OK) { if (cnt < cap) found[cnt] = a; cnt++; }
return cnt;
```

센서 2개(0x48, 0x4A)에서 `cnt = 2`, `found = {0x48, 0x4A}`, 트랜잭션 112개. `cap = 1` 이면 반환 2, 저장 1개. 주의: 주소만 쓰는 probe(SMBus Quick Write)를 "쓰기 명령"으로 해석하는 디바이스가 있어서, Linux `i2cdetect` 는 주소 범위에 따라 Quick Write 대신 Read Byte 로 probe 한다(EEPROM 대역 등 — man 페이지 확인 권장). bring-up 보드에서 스캔은 "무엇이 살아 있나"를 보는 첫 도구지만, 운영 펌웨어에 넣을 일은 드물다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 28/28 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- 레지스터 읽기 = `S SLA+W reg A Sr SLA+R data… N P`. 포인터는 디바이스 안의 레지스터라 Sr 를 넘어 유지된다. 0x90/0x91 = 7비트 0x48 + R/W.
- Sr 는 STOP 없이 방향을 바꾼다 → 버스가 계속 busy → 다른 마스터·다른 태스크가 포인터를 못 옮긴다. STOP+START 로 하면 이 레슨에서 0xA7 대신 0x28(끼어든 마스터), 0x0C(STOP 에서 포인터 리셋)가 읽혔다.
- 마스터는 마지막 읽기 바이트에 NACK, 어떤 NACK 이든 즉시 STOP. 안 그러면 슬레이브가 SDA 를 쥐거나 버스가 busy 로 남는다.
- 멀티바이트 값은 한 트랜잭션 burst 로. 따로 읽으면 0x00FF→0x0100 경계에서 0 °C 같은 찢어진 값.
- 고정소수점: m°C = floor(raw × 125 / 16). C 의 `/` 는 0 쪽으로 자르므로 음수에서 30720/65536 개가 달라진다. `int16_t` 부호 해석을 잊지 말 것.
- 드라이버 init: ID → 설정 쓰기 → 쓰기 가능 비트만 되읽어 비교. 폴링은 상한을 두고, 버스 에러는 계층을 지켜 그대로 올린다.
- 실제 API: STM32 `HAL_I2C_Mem_Read`, Linux `i2c_transfer`(메시지 2개) / `i2c_smbus_read_byte_data` / `regmap` 가 같은 Sr 시퀀스를 만든다.

## 참고자료

- NXP, *UM10204 I2C-bus specification and user manual* — START/repeated START/STOP 정의, combined format, ACK/NACK, t_BUF 등 타이밍 표: <https://www.nxp.com/docs/en/user-guide/UM10204.pdf> (이 레슨의 t_BUF 숫자는 대표값 — 표 확인 권장)
- System Management Interface Forum, *SMBus Specification* (3.x) — Read Byte / Read Word 프로토콜의 repeated START, Read Word 의 LSB-먼저 순서.
- Linux kernel docs, "Implementing I2C device drivers" 와 "The SMBus Protocol" — `i2c_transfer`, `i2c_smbus_read_byte_data`, `i2c_smbus_read_word_swapped`: <https://docs.kernel.org/i2c/writing-clients.html>, <https://docs.kernel.org/i2c/smbus-protocol.html>
- Linux `i2c-tools` — `i2cdetect(8)` man 페이지의 probe 방식 설명.
- STMicroelectronics, STM32Cube HAL 사용자 매뉴얼 — `HAL_I2C_Mem_Read`, `_IT`, `_DMA` 와 `DevAddress` 표기 (패밀리별 UM 확인).
- Texas Instruments, *TMP117 datasheet* — 16비트 온도 레지스터, 7.8125 m°C 해상도, Device ID 레지스터, Data_Ready 플래그 (이 레슨의 mock 과 비교용, 수치는 데이터시트 확인 권장).
- 각 제조사 LM75 계열 데이터시트 — 포인터 레지스터 유지, 9비트 0.5 °C 형식.
- Bosch BMP280 데이터시트(데이터 레지스터 shadowing), ST LIS3DH 데이터시트(BDU 비트) — 찢어진 읽기 방지 기능의 예 (세부는 데이터시트 확인).
- 관련 레슨: COMM-B06 (I2C 비트 수준, 이 레슨 모델의 원판), COMM-B05 (SPI 레지스터 접근, 같은 레지스터 맵 개념), COMM-M01 (클럭 스트레칭), COMM-M02 (멀티마스터 중재), COMM-M03 (SDA stuck-low 9클럭 복구), COMM-M12 (I2C/SPI 겸용 bus_ops 드라이버·에러 변환), COMM-M13 (비동기 트랜잭션 큐), COMM-A10 (LA 디코더), HWLAB-B03 (풀업·rise time), HWLAB-M06 (sigrok 식 디코더), CPP-B07 (RAII 칩 셀렉트 — 락·트랜잭션 범위의 C++ 판), C-M14 (ISR 공유 데이터).

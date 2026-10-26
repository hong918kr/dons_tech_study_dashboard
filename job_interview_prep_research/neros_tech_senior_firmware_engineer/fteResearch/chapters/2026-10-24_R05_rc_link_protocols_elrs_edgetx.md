# 📡 R05 ELRS · EdgeTX · CRSF · SBUS · DShot · MSP · MAVLink 와 테스트
> 조종기 stick에서 모터까지 신호가 지나가는 프로토콜 7개를 실제 소스(ExpressLRS · EdgeTX · Betaflight, 2026-10-23~24 커밋)로 확인하고, 프레임 구조 · 타이밍 계산 · 고장 모드 · FTE가 어떻게 테스트하는지까지 정리한다. 실행 가능한 `crsf.py` / `dshot.py` 포함.

---

## 0. 이 단원 한눈에

- **무엇**: RC 링크(ELRS) · 조종기 OS(EdgeTX) · 직렬 프로토콜(CRSF · SBUS · MSP · MAVLink) · 모터 프로토콜(DShot)
- **왜**: Neros 공고에 ELRS 6회, MAVLink 4회, EdgeTX/OpenTX, CRSF/SBUS가 나온다 ([K05 스캔](../../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md)). FW Test Engineer 공고 우대 항목에도 "RF product testing, Betaflight, ELRS"가 있다
- **어떻게 읽나**: 1장(전체 경로) → 프로토콜별 장(구조 → 타이밍 → 고장 → 테스트) → 9장(SSD 비교) → 10장(체크리스트)
- **코드**: [`../code/crsf.py`](../code/crsf.py) (CRSF 인코드 · 디코드 · 스트리밍 파서 · fuzz), [`../code/dshot.py`](../code/dshot.py) (DShot 프레임 · bidir GCR 텔레메트리)
- 소스 확인 기준: ExpressLRS `36c1bf2` (2026-10-23), EdgeTX `917802d` (2026-10-24), Betaflight `d593400` (2026-10-25 +1100). 경로는 이 커밋 기준이다. master는 계속 바뀌니 다시 볼 땐 `git log -1`로 확인

관련 단원:

- 시스템 전체 그림은 [R02](2026-10-24_R02_drone_system_and_open_source_map.md)
- FC 안쪽(RX task · rc smoothing · failsafe 단계)은 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md)
- MAVLink를 쓰는 PX4/ArduPilot 테스트 생태계는 [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)
- 계측기 · 감쇠기 · 차폐 박스 배선은 [R08](2026-10-24_R08_hil_bench_design_instruments.md)
- 테스트 기법 분류(conformance · fuzz · soak)는 [R07](2026-10-24_R07_test_methodology_pyramid.md)

---

## 1. 왜 중요한가 (FTE 관점)

드론에서 "조종이 안 먹는다", "갑자기 failsafe", "모터가 떨린다" 같은 현장 증상은 거의 다 이 장의 프로토콜 경계에서 생긴다. 경계마다 **다른 팀의 코드**가 만난다. 조종기(EdgeTX) ↔ TX 모듈(ELRS) ↔ RX(ELRS) ↔ FC(Betaflight/PX4) ↔ ESC(BLHeli/AM32/Bluejay). FTE의 가치는 이 경계를 **양쪽 코드를 믿지 않고** 선에서 직접 보는 것이다.

SSD 검증에서 PCIe/NVMe 경계를 protocol analyzer로 본 것과 같은 일이다. 다른 점은 세 가지다.

- 규격서가 약하다. CRSF는 TBS 문서가 있지만 ELRS가 확장했고, DShot은 공식 규격서가 없고 Betaflight 코드 + 커뮤니티 문서가 사실상 규격이다
- 무선 구간이 있다. 패킷 손실은 "버그"가 아니라 "정상 상황"이다. 테스트는 손실이 있을 때 시스템이 **어떻게 망가지는지(degrade)** 를 본다
- 안전이 걸려 있다. 파서 버그 하나가 "모터 최대 출력"이 될 수 있다. failsafe 경로가 테스트 1순위다

---

## 2. 신호 경로 전체 그림

```text
 [stick ADC]                                                         [motor]
     |                                                                  ^
     v                                                                  |
 +---------+  CRSF over UART      +---------+   RF (FHSS)   +---------+ |
 | EdgeTX  |--------------------->| ELRS TX |~~~~~~~~~~~~~~>| ELRS RX | |
 | handset |  400k..5.25M baud    | module  |  OTA packet   |         | |
 |         |<---------------------|         |<~~~~~~~~~~~~~~|         | |
 +---------+  telemetry (CRSF)    +---------+  telemetry    +---------+ |
     ^  0xEE addr, 22B ch + status                 |  CRSF 0xC8, 420000 baud
     |                                             v  (or SBUS 100k 8E2 inverted)
     |                                      +--------------+   DShot300/600   +-----+
     |                                      |  FC          |----------------->| ESC |
     |                                      |  Betaflight  |<-----------------|     |
     |                                      |  / PX4       |  bidir eRPM (GCR)+-----+
     |                                      +--------------+
     |                                         |  MSP (USB/UART)  -> configurator / test PC
     |                                         |  MAVLink (UART)  -> companion (Jetson) / GCS
     +-----------------------------------------+
            telemetry 경로: FC -> CRSF -> RX -> RF -> TX -> EdgeTX (Lua, 화면)
```

| 구간 | 프로토콜 | 물리 계층 | 누가 정의? | 주된 FTE 질문 |
|---|---|---|---|---|
| 조종기 → TX 모듈 | CRSF (handset 변형) | UART, 단선 반이중 또는 양선 | TBS 규격 + ELRS 확장 | 프레임 주기와 RF 패킷 주기가 동기되나 |
| TX ↔ RX | ELRS OTA | 2.4 GHz / 900 MHz, LoRa · FLRC · GFSK | ELRS 코드가 규격 | LQ · 감도 · failsafe · 재연결 시간 |
| RX → FC | CRSF 또는 SBUS | UART 420k 8N1 / 100k 8E2 반전 | TBS 규격 / Futaba (비공개, 역공학) | 파서 견고성 · 지연 · failsafe 신호 |
| FC → ESC | DShot | GPIO 펄스 폭 (타이머 + DMA 또는 bitbang) | Betaflight · ESC 커뮤니티 | CRC · 타이밍 · 명령 처리 · eRPM |
| FC ↔ PC | MSP v1/v2 | USB VCP / UART | Betaflight · INAV | 자동화 손잡이로 쓸 때 신뢰성 |
| FC ↔ companion/GCS | MAVLink v2 | UART / UDP | mavlink.io XML | CRC_EXTRA · seq 손실 · 서명 |

> ⚠️ Gemini 정정: Gemini는 RX→FC 구간을 "고속 UART 420k ~ 5.25M baud"로 그렸다. 5.25 Mbaud는 **조종기 ↔ TX 모듈** 구간 이야기다. ELRS `src/lib/Handset/CRSFHandset.cpp`의 `TxToHandsetBauds[] = {400000, 115200, 5250000, 3750000, 1870000, 921600, 2250000}`, EdgeTX `radio/src/telemetry/crossfire.h`의 `CROSSFIRE_BAUDRATES[] = {115200, 400000, 921600, 1870000, 3750000, 5250000}`. RX→FC는 Betaflight 기본 `CRSF_BAUDRATE 420000` (`src/main/rx/crsf_protocol.h`), ELRS RX 기본도 420000 (`src/user_defines.txt`: "If not defined, will default to 420000").

---

## 3. CRSF (Crossfire Serial Protocol)

### 3.1 목적과 위치

TBS가 Crossfire 제품용으로 만든 직렬 프로토콜이다. ELRS가 이걸 그대로 채택해서 사실상 FPV 표준이 됐다. 한 프레임 형식으로 RC 채널(위로), 텔레메트리(아래로), 설정 파라미터(Lua 메뉴), MSP 터널까지 다 실어 나른다.

### 3.2 baud — 숫자가 세 개다

| 출처 | 값 | 어디 구간 |
|---|---|---|
| TBS 규격 (crsf.md) 단선 반이중 | 400000, 8N1, 3.3 V, 반전/비반전 | 조종기 ↔ TX 모듈 (조종기가 master) |
| TBS 규격 양선 전이중 | 416666, 8N1, 비반전만 | 일반 UART 연결 |
| Betaflight 기본 / ELRS RX 기본 | **420000** | RX → FC (사실상 표준) |
| Betaflight `USE_CRSF_OFFICIAL_SPEC` | 416666 | 공식 규격 모드 |
| ELRS 조종기 쪽 | 400000 ~ 5250000 (자동 baud 감지) | 조종기 ↔ TX 모듈 |

420000과 416666은 0.8% 차이다. UART는 보통 ±2~3% 오차까지 받으니 섞여도 대부분 동작한다. **하지만 테스트할 때는 이 차이가 "가끔 나는 framing error"의 후보가 된다.** 로직 분석기 UART 디코더를 420000으로 걸었는데 416666 장치를 보면, 긴 프레임 끝쪽 비트에서 샘플 위치가 밀린다.

ELRS 코드에 실제로 있는 규칙: 조종기 baud가 400000이면 packet rate를 500 Hz로 제한한다 (`CRSFHandset.cpp`: "Packet rate limited to 500Hz if we are on 400k baud"). 테스트 항목이 된다 — "400k 조종기에서 F1000을 고르면 어떻게 되나?"

### 3.3 프레임 구조

```text
 byte:   0        1        2        3 .......... N+2      N+3
       +--------+--------+--------+-------------------+--------+
       | sync / | len    | type   | payload (N bytes) | crc8   |
       | addr   |        |        |                   |        |
       +--------+--------+--------+-------------------+--------+
                 len = N + 2  (type + payload + crc)
                 crc = CRC-8 poly 0xD5, init 0, over [type .. payload]  (sync, len 제외)
                 전체 = len + 2 <= 64 bytes   ->  len 범위 2..62 (TBS 규격)
```

- 바이트 0: 직렬 선에서는 **항상 0xC8** 이 원칙이다. ELRS `SerialCRSF.cpp` 주석: "CRSF on a serial port _always_ has 0xC8 as a sync byte rather than the device_id". 그런데 TBS 규격은 수신 쪽이 0xC8 · 0x00(broadcast) · 장치 주소 세 가지를 다 받으라고 한다. EdgeTX는 오래된 모듈 호환을 위해 0xEA(RADIO_ADDRESS)도 받는다 (`_validHdr`)
- type ≥ 0x28 이면 **extended header**: payload 앞에 `[dest][origin]` 2바이트가 붙는다 (파라미터 · 명령 · MSP 터널)
- 엔디안: **big endian** (멀티바이트 필드). 단 RC 채널 비트 패킹은 little-endian 비트 순서다. 둘을 섞어서 헷갈리는 게 흔한 버그다
- 명령 프레임 0x32는 payload 안에 CRC-8 **poly 0xBA** 가 하나 더 있다 (TBS 규격)

주소 표 (ELRS `src/include/crsf_protocol.h`):

| 주소 | 의미 |
|---|---|
| 0x00 | broadcast |
| 0xC8 | flight controller (= 직렬 sync) |
| 0xEA | radio transmitter (조종기) |
| 0xEC | CRSF receiver |
| 0xEE | CRSF transmitter (TX 모듈) |
| 0xEF | ELRS Lua |

### 3.4 자주 보는 frame type (ELRS `crsf_protocol.h`로 확인)

| type | 이름 | payload | 방향 | 비고 |
|---|---|---|---|---|
| 0x02 | GPS | 15 B: lat int32 (deg×1e7), lon int32, ground speed uint16 (km/h×10), heading uint16 (deg×100), alt uint16 (m + 1000 offset), sats uint8 | FC → 조종기 | |
| 0x07 | Vario | int16 cm/s | FC → | |
| 0x08 | Battery | voltage int16 (0.1 V), current int16 (0.1 A), capacity uint24 (mAh), remaining uint8 (%) | FC → | ID 바이트 선택 |
| 0x0B | Heartbeat | | | |
| 0x14 | Link statistics | 10 B (아래 표) | RX → FC, TX → 조종기 | LQ/RSSI 출처 |
| 0x16 | RC channels packed | 22 B (16 × 11 bit) | 조종기 → TX, RX → FC | 핵심 |
| 0x1E | Attitude | pitch · roll · yaw int16, LSB = 100 µrad | FC → | ±180° |
| 0x21 | Flight mode | null-terminated 문자열 | FC → | |
| 0x28 / 0x29 | Device ping / info | extended | 양방향 | 장치 탐색 |
| 0x2B~0x2D | Parameter entry / read / write | extended | 양방향 | ELRS Lua 메뉴 |
| 0x2E | ELRS status | extended | | ELRS 전용 |
| 0x32 | Command | extended + inner CRC 0xBA | | bind 명령 등 |
| 0x3A | Handset (timing) | sub 0x10: update_interval · offset (LSB 100 ns) | TX → 조종기 | 조종기 mixer를 RF 주기에 맞춤 |
| 0x7A~0x7C | MSP req / resp / write | extended | | CRSF 안에 MSP 터널 |

> ⚠️ Gemini 정정 / 확인: 작업 지시서에 있던 frame type 목록(0x16 RC, 0x14 link stats, 0x08 battery, 0x02 GPS, 0x1E attitude, 0x21 flight mode)은 ELRS 헤더와 **모두 일치**한다. Gemini 대화의 "0xC8/0xAA 가짜 헤더"에서 0xAA는 CRSF 값이 아니다. 앞서 연습한 T2 파서(sync 0xAA)의 값이다. CRSF fuzz에는 0xC8 · 0xEA · 0xEE · 0xEC · 0x00을 가짜 sync로 써야 한다.

### 3.5 RC channels (0x16) 비트 배치

```text
 payload byte:   0         1         2         3     ...   21
 bit (LSB=0):  76543210  76543210  76543210  76543210      76543210
              |ch0[7:0]|ch1[4:0]ch0[10:8]|ch2[1:0]ch1[10:5]|ch2[9:2]| ...
               ^ ch0 bit0 = byte0 bit0  (little-endian bit stream)

 16 ch x 11 bit = 176 bit = 22 bytes
 frame = C8 18 16 <22 bytes> crc   -> 26 bytes total, len field = 0x18 (24)
```

값 매핑:

| tick | µs (ELRS 표기) | TBS 공식 `(t-992)*5/8+1500` | 의미 |
|---|---|---|---|
| 0 | 880 | 880.0 | 확장 한계 (-121%) |
| 172 | 988 | 987.5 | -100% |
| 191 | 1000 | 999.4 | |
| 992 | 1500 | 1500.0 | 중앙 |
| 1792 | 2000 | 2000.0 | |
| 1811 | 2012 | 2011.9 | +100% |
| 1984 | 2120 | 2120.0 | 확장 한계 (+121%) |

반올림 함정: 공식대로 988 µs를 tick으로 바꾸면 172.8 → **173**이다. ELRS는 172를 "988us"로 부른다. Betaflight는 `scale = (2012-988)/(1811-172) = 0.62477…`로 선형 변환한다 (`crsf.c` `crsfReadRawRC`). 끝값 1 tick 차이로 "-100% 스위치 판정"이 갈리는 버그를 FTE는 경계값 테스트로 잡는다. `crsf.py` self-test 3번이 이 함정을 assert 한다.

EdgeTX가 TX 모듈로 보내는 프레임은 조금 다르다 (`radio/src/pulses/crossfire.cpp` `createCrossfireChannelsFrame`):

- 첫 바이트가 **0xEE** (MODULE_ADDRESS), len = **25** = 1(type) + 22(ch) + **1(status 바이트)** + 1(crc)
- status 바이트: bit0 = armed (Switch 모드), bit1 = CH5로 arm 하는 모드인지. ELRS의 "arming extension"이다 (ELRS `CRSF_CHANNELS_STATUS_ARMED`)
- 채널값 = `limit(0, 992 + pulses*4/5, 1984)`, pulses 범위 ±1024 → ±819 → 173..1811

즉 "RC 프레임은 항상 26바이트"는 RX→FC에서만 맞다. 조종기→모듈은 27바이트다. 파서가 len을 하드코딩하면 여기서 깨진다.

### 3.6 Link statistics (0x14)

| 순서 | 필드 | 타입 | 단위 |
|---|---|---|---|
| 1 | uplink RSSI ant1 | uint8 | dBm × -1 |
| 2 | uplink RSSI ant2 | uint8 | dBm × -1 |
| 3 | uplink LQ | uint8 | % |
| 4 | uplink SNR | int8 | dB |
| 5 | active antenna | uint8 | 0/1 |
| 6 | RF mode | uint8 | enum (ELRS는 rate index) |
| 7 | uplink TX power | uint8 | enum (0,10,25,100,500,1000,2000,250,50 mW) |
| 8 | downlink RSSI | uint8 | dBm × -1 |
| 9 | downlink LQ | uint8 | % |
| 10 | downlink SNR | int8 | dB |

10바이트 payload → 프레임 14바이트. "uplink" = 땅 → 기체, "downlink" = 기체 → 땅. RSSI가 **부호 없는 값에 -1을 곱한 것**이라 파서에서 부호 처리 실수가 잦다. ELRS는 여기에 `downlink_RSSI_2`를 덧붙인 `elrsLinkStatistics_t`를 내부적으로 쓴다.

ELRS RX는 16채널 모드가 아니면 **ch14 = LQ, ch15 = RSSI** 를 채널로도 내보낸다 (`SerialCRSF.cpp`). FC에서 AUX 채널 값을 검증할 때 "ch15가 왜 계속 변하지?"의 답이다.

### 3.7 타이밍 계산

UART 8N1 = 바이트당 10비트 (start 1 + data 8 + stop 1).

| 프레임 | 바이트 | 400000 | 416666 | 420000 | 5250000 |
|---|---|---|---|---|---|
| RC (RX→FC) | 26 | 650 µs | 624 µs | **619 µs** | 49.5 µs |
| RC + status (조종기→모듈) | 27 | 675 µs | 648 µs | 643 µs | 51.4 µs |
| Link stats | 14 | 350 µs | 336 µs | 333 µs | 26.7 µs |
| 최대 64 B | 64 | 1600 µs | 1536 µs | 1524 µs | 122 µs |

RX→FC 링크 사용률 (RC 프레임만, 420000 baud):

| ELRS packet rate | 주기 | 26 B 전송 시간 | 사용률 |
|---|---|---|---|
| 50 Hz | 20 ms | 619 µs | 3.1% |
| 250 Hz | 4 ms | 619 µs | 15.5% |
| 500 Hz | 2 ms | 619 µs | 31% |
| 1000 Hz (F1000) | 1 ms | 619 µs | **62%** |

F1000에서 420k는 여유가 적다. 여기에 link stats와 MSP 터널이 끼면 더 줄어든다. Betaflight는 CRSF v3 baud 협상(`USE_CRSF_V3`)과 `frameTimeNeededUs`를 baud에 맞춰 다시 계산하는 코드가 있다 (`crsf.c` 717행 부근). 고속 baud가 실제로 어떻게 협상되는지는 [확인 필요].

Betaflight 파서의 시간 상수 (`src/main/rx/crsf.c`):

- `CRSF_TIME_NEEDED_PER_FRAME_US 1748` — 64바이트 프레임 약 1550 µs를 올림
- 마지막 바이트 이후가 아니라 **프레임 시작 후 1748 µs가 지나 들어온 바이트는 새 프레임 시작으로 본다**. 즉 Betaflight는 sync 바이트 검색이 아니라 **시간 간격으로 프레임 경계를 잡는다**
- `CRSF_LINK_STATUS_UPDATE_TIMEOUT_US 250000` (250 ms)

### 3.8 파서 설계 세 가지 — 실제 코드 비교

같은 CRSF인데 세 프로젝트의 파서 정책이 다르다. FTE에게 아주 좋은 재료다. 같은 깨진 바이트열을 넣으면 **결과가 다를 수 있다**.

| 프로젝트 | 파일 | 프레임 시작 판단 | CRC 실패 시 | 길이 검사 |
|---|---|---|---|---|
| Betaflight | `src/main/rx/crsf.c` `crsfDataReceive` | **시간**: 프레임 시작 후 `frameTimeNeededUs` 지나면 위치 0으로 리셋. 시작 바이트 값은 보지 않음 | 프레임 버림, 다음 바이트부터 새 프레임 | `crsfFrameLengthIsValid()` 실패 시 즉시 리셋 |
| ExpressLRS | `src/lib/CrsfProtocol/CRSFParser.cpp` | 바이트 값: 0xC8 · 0xEA · 0xEC 중 하나 | 상태 IDLE로, **이미 소비한 바이트는 다시 보지 않음** | len 2..62 밖이면 IDLE |
| EdgeTX | `radio/src/pulses/crossfire.cpp` `_processFrames` | 바이트 값: 0xC8 또는 0xEA | **1바이트만 버리고 다시 검사** ("CRC errors or invalid length errors MUST just advance the buffer by one") | `_lenIsSane` |

의미:

- EdgeTX 방식은 깨진 프레임 안에 진짜 프레임 시작이 숨어 있으면 살려낸다. EdgeTX 테스트 `frameParser_droppedBytes`가 정확히 이걸 검증한다: 길이 바이트가 빠진 프레임 뒤에 숨은 4개 프레임을 모두 찾는지
- ELRS 방식은 단순하고 빠르다. 대신 가짜 sync + 큰 len이 들어오면 그 len만큼 바이트를 먹어 버린다. 뒤의 진짜 프레임 1개를 잃을 수 있다
- Betaflight 방식은 시간 기반이라 **바이트 값 fuzz만으로는 테스트가 부족하다**. 바이트 사이 간격(inter-byte gap)을 조절하는 주입기가 있어야 진짜 동작을 본다. 순수 바이트 스트림을 PC에서 몰아 보내면(USB-UART가 바이트를 몰아서 전송) 실제 RX와 다른 타이밍이 된다

`crsf.py`의 `StreamParser`는 EdgeTX 정책(1바이트 resync)을 따랐다.

### 3.9 고장 모드 (failure modes)

| 고장 | 원인 예 | 증상 | 잡는 테스트 |
|---|---|---|---|
| baud 불일치 | 416666 vs 420000, 클럭 오차 | 긴 프레임 끝에서 CRC 에러 증가 | baud ±3% sweep, CRC 에러율 기록 |
| 반전 설정 오류 | SBUS/CRSF inverted 옵션 | 프레임 0개 | 설정 매트릭스 테스트 |
| 바이트 유실 | UART overrun, DMA 설정, ISR 지연 | 간헐 CRC 에러, 프레임 1개 손실 | 유실 주입 + 다음 프레임 복구 확인 |
| 가짜 sync | 노이즈, payload 안의 0xC8 | 프레임 손실 또는 (최악) 오파싱 | fake sync fuzz |
| len 범위 밖 | 노이즈 | 버퍼 overrun (방어 없을 때) | len 0, 1, 63, 64, 0xFF 주입 |
| 끝값 반올림 | 172 vs 173 | -100% 스위치 판정 오류 | 경계값 테스트 |
| 반이중 충돌 | 조종기 ↔ 모듈 단선에서 응답 타이밍 어긋남 | 텔레메트리 손실 | 로직 분석기로 TX/RX 슬롯 확인 |
| 링크 끊김 시 프레임 정지 | ELRS RX는 새 패킷이 없으면 RC 프레임을 안 보냄 | FC가 자체 타임아웃으로 failsafe | failsafe 시간 측정 (6장) |

### 3.10 CRSF 테스트하는 법

**(a) conformance (규격 적합성)**

- 프레임 길이: RC 26 B (RX→FC), 27 B (조종기→모듈) 고정 확인
- CRC: 골든 벡터로 검증. EdgeTX `radio/src/tests/crossfire.cpp`의 `TEST(Crossfire, crc8)` 벡터 `00 0C 14 00 00 00 00 FF 01 03 00 00 00 F4` (crc = 0xF4)를 `crsf.py`가 그대로 쓴다
- 채널 비트 순서: ch0만 0x7FF → payload `FF 07 00 …` (`crsf.py` 테스트 2)
- 값 매핑 경계: 172 / 992 / 1811 / 0 / 1984
- extended 프레임 dest/origin 처리, 알 수 없는 type 무시 (규격: "extra trailing bytes are ignored")

**(b) 파서 견고성 (robustness / fuzz)**

- 바이트 단위 변이: bit flip · 삭제 · 삽입(가짜 sync 우선) · 중복 · 잘라내기
- 청크 경계 무작위: 1~7바이트씩 끊어 넣기 (UART DMA idle 인터럽트가 실제로 이렇게 준다)
- 불변식(invariant) 검사: "반환된 프레임은 모두 CRC가 맞다", "버퍼는 max_buf를 넘지 않는다", "예외가 나지 않는다"
- Python 도구 아이디어: `hypothesis`로 "임의 바이트열 + 유효 프레임 삽입 → 유효 프레임은 반드시 나온다(앞뒤 오염이 프레임 경계를 침범하지 않으면)" 같은 성질 기반 테스트. `boofuzz`는 네트워크 · 직렬 타깃에 변이 패킷을 보내고 타깃 생존을 감시하는 프레임워크라 HIL에서 실제 FC UART에 붙일 수 있다 (serial connection 지원 여부와 사용법은 [확인 필요])
- C 코드 쪽: libFuzzer로 `crsfDataReceive` 같은 함수를 PC에서 돌리는 하네스. Betaflight는 `STATIC_UNIT_TESTED`로 이 함수를 테스트에서 부를 수 있게 열어 둔다

**(c) resync 테스트 (구체 케이스)**

1. 정상 프레임 앞에 쓰레기 3바이트 → 정상 프레임 1개
2. 가짜 `C8 18` + 5바이트 + 정상 프레임 → 정상 프레임 1개 (EdgeTX/crsf.py 방식), ELRS 방식에서는 가짜 len 24만큼 먹으므로 결과가 다를 수 있다 → **구현별 기대값을 문서화**
3. 프레임 중간 1바이트 삭제 → 그 프레임은 버려지고 다음 프레임 2개는 산다
4. len = 0xFF → 버퍼를 255바이트 기다리지 않고 즉시 버린다
5. 0xC8 3E + 0 60개를 100번 → 버퍼 상한 유지

**(d) 타이밍 / 지터**

- 로직 분석기 UART 디코더(sigrok `uart`, Saleae Async Serial)로 CRSF 선을 찍는다. sigrok 공식 디코더 목록에 **CRSF 디코더는 없다** (2026-10-24 sigrok.org/wiki/Protocol_decoders 확인). UART 디코드 결과를 CSV로 뽑아 `crsf.py`로 2차 디코드하면 된다
- 측정 항목: 프레임 간격 분포(평균, p99, 최대), 프레임 내부 바이트 간격(Betaflight 시간 기반 파서에 영향), 조종기 CRSF 주기와 RF packet rate의 위상 관계(ELRS 0x3A timing 프레임이 맞추는 것)
- 주입기: USB-UART는 바이트 간격을 제어하기 어렵다. 정밀 주입은 MCU(예: RP2040 PIO)로 따로 만드는 게 낫다 [추정, 하드웨어 구매 전 Don과 예산 확인]

---

## 4. ExpressLRS (ELRS)

### 4.1 무엇이고 어디서 도나

오픈소스 RC 링크 펌웨어. TX 모듈과 RX 양쪽에서 돈다. Semtech SX127x(900 MHz), SX1280(2.4 GHz), **LR1121(sub-GHz + 2.4 GHz 둘 다 가능)**, LR2021 칩을 구동한다.

현재 master의 빌드 타깃은 **ESP32 / ESP32-S3 / ESP32-C3 / ESP8285만** 있다 (`src/targets/` 파일 목록: esp32-rx.ini, esp32-tx.ini, esp32c3-*, esp32s3-*, esp8285-*). 

> ⚠️ Gemini 정정: Gemini 그림의 "Radio TX Board: STM32/ESP32", "Radio RX: STM32 + SX1280/LR1121"은 현재 ELRS master와 다르다. STM32 타깃 ini가 master에 없다. 다만 **Betaflight FC 안에 SPI로 붙은 ELRS 수신기**(`rx_spi_expresslrs`)는 STM32 FC 위에서 돈다 (Betaflight 테스트 `src/test/unit/rx_spi_expresslrs_unittest.cc` 존재). 구형 ELRS 버전에는 STM32 타깃이 있었다 — 언제 빠졌는지는 [확인 필요].
>
> ⚠️ Gemini 정정: "SX127x/LR1121(900MHz)" — LR1121은 900 MHz 전용이 아니라 sub-GHz와 2.4 GHz를 모두 다루는 칩이고, ELRS는 이걸로 dual band 모드(`RATE_LORA_DUAL_*`)를 만든다.

### 4.2 Gemini가 말한 경로 검증

| Gemini 주장 | 실제 (`36c1bf2`) | 판정 |
|---|---|---|
| `src/lib/CrsfProtocol/` CRSF 구조체 + CRC(0xD5) | 디렉터리 있음: CRSFConnector · CRSFEndpoint · CRSFParser · CRSFRouter · RxTxEndpoint. 구조체/상수는 `src/include/crsf_protocol.h`, CRC 구현은 `src/lib/CRC/crc.cpp` | 경로 맞음, 내용 설명은 부분 정정 |
| `src/lib/FHSS/` 시드 · 채널 점프 | `FHSS.cpp`, `FHSS.h`, `random.cpp` | 맞음 |
| `src/lib/LQCALC/` 슬라이딩 윈도우 | `LQCALC.h` (템플릿 1개 파일) | 맞음 |
| EdgeTX `radio/src/pulses/crossfire.cpp` | 있음. 텔레메트리 쪽은 `radio/src/telemetry/crossfire.cpp` | 맞음 |

FTE가 더 볼 곳: `src/lib/OTA/OTA.h` (무선 패킷 구조), `src/src/common.cpp` (packet rate 표), `src/src/rx_main.cpp` (연결 상태 · failsafe), `src/src/rx-serial/SerialCRSF.cpp` · `SerialSBUS.cpp` (FC 쪽 출력), `src/lib/Handset/CRSFHandset.cpp` (조종기 쪽), `src/test/` (단위 테스트).

### 4.3 packet rate 표 (코드에서 직접 뽑음)

`src/src/common.cpp`의 `ExpressLRS_AirRateConfig[]`(변조 설정)와 `ExpressLRS_AirRateRFperf[]`(감도 · 시간)를 합쳤다. 구조체 필드는 `src/include/common.h`: interval(µs), TLMinterval, FHSShopInterval, PayloadLength, numOfSends / RXsensitivity(dBm), TOA(µs), DisconnectTimeoutMs, RxLockTimeoutMs.

**SX1280 (2.4 GHz)**

| enum | 변조 | interval µs | 실효 rate | TOA µs | TLM 기본 | hop 간격(패킷) | OTA 크기 | sends | 감도 dBm | Disconnect ms |
|---|---|---|---|---|---|---|---|---|---|---|
| FLRC_2G4_1000HZ | FLRC | 1000 | 1000 Hz | 389 | 1:128 | 2 | 8 B | 1 | -104 | 2500 |
| FLRC_2G4_500HZ | FLRC | 2000 | 500 Hz | 389 | 1:128 | 2 | 8 B | 1 | -104 | 2500 |
| FLRC_2G4_500HZ_DVDA | FLRC | 1000 | 500 Hz | 389 | 1:128 | 2 | 8 B | 2 | -104 | 2500 |
| FLRC_2G4_250HZ_DVDA | FLRC | 1000 | 250 Hz | 389 | 1:128 | 2 | 8 B | 4 | -104 | 2500 |
| LORA_2G4_500HZ | LoRa SF5 BW800 | 2000 | 500 Hz | 1507 | 1:128 | 4 | 8 B | 1 | -105 | 2500 |
| LORA_2G4_333HZ_8CH | LoRa SF5 | 3003 | 333 Hz | 2374 | 1:128 | 4 | 13 B | 1 | -105 | 2500 |
| LORA_2G4_250HZ | LoRa SF6 | 4000 | 250 Hz | 3300 | 1:64 | 4 | 8 B | 1 | -108 | 3000 |
| LORA_2G4_150HZ | LoRa SF7 | 6666 | 150 Hz | 5871 | 1:32 | 4 | 8 B | 1 | -112 | 3500 |
| LORA_2G4_100HZ_8CH | LoRa SF7 | 10000 | 100 Hz | 7605 | 1:32 | 4 | 13 B | 1 | -112 | 3500 |
| LORA_2G4_50HZ | LoRa SF8 | 20000 | 50 Hz | 10798 | 1:16 | 2 | 8 B | 1 | -115 | 4000 |

**SX127x (900 MHz)**

| enum | 변조 | interval µs | 실효 rate | TOA µs | TLM | hop | OTA | sends | 감도 dBm | Disconnect ms |
|---|---|---|---|---|---|---|---|---|---|---|
| LORA_900_200HZ | SF6 BW500 | 5000 | 200 Hz | 4380 | 1:64 | 4 | 8 B | 1 | -112 | 3000 |
| LORA_900_100HZ_8CH | SF6 | 10000 | 100 Hz | 6690 | 1:32 | 4 | 13 B | 1 | -112 | 3500 |
| LORA_900_100HZ | SF7 | 10000 | 100 Hz | 8770 | 1:32 | 4 | 8 B | 1 | -117 | 3500 |
| LORA_900_50HZ | SF8 | 20000 | 50 Hz | 18560 | 1:16 | 4 | 8 B | 1 | -120 | 4000 |
| LORA_900_25HZ | SF9 | 40000 | 25 Hz | 29950 | 1:8 | 2 | 8 B | 1 | -123 | 6000 |
| LORA_900_50HZ_DVDA | SF6 | 5000 | 50 Hz | 4380 | 1:64 | 2 | 8 B | 4 | -112 | 3000 |

**LR1121 추가 모드** (같은 파일 `RADIO_LR1121` 블록): `FSK_900_1000HZ_8CH` (GFSK 300 kbps, 1000 µs, 13 B, 감도 -101), `LORA_900_250HZ`, `LORA_900_200HZ_8CH`, `FSK_2G4_1000HZ`, `FSK_2G4_500HZ_DVDA`, `FSK_2G4_250HZ_DVDA`, 그리고 **`LORA_DUAL_150HZ`, `LORA_DUAL_100HZ_8CH`** (900 MHz 설정 bw/sf + 2.4 GHz 설정 bw2/sf2를 동시에 씀).

읽는 법:

- **실효 rate = 1e6 / (interval × numOfSends)**. DVDA("Deja Vu Diversity Aid")는 같은 데이터를 빠른 슬롯으로 2번 또는 4번 반복해서 보내는 모드다. 이름 풀이는 ELRS 문서 기준 [확인 필요]
- **TOA < interval** 이어야 한다. LoRa 500 Hz는 2000 µs 슬롯에 1507 µs를 공중에 있다. 남는 493 µs에 주파수 전환 · 처리 · 송수신 전환을 다 해야 한다. 타이밍 여유가 작은 모드가 지터 테스트의 1순위다
- 감도 숫자는 코드에 적힌 "expected min RF sensitivity"다. 측정값이 아니다. 감쇠기 테스트로 **실측해서 이 숫자와 비교**하는 게 FTE의 일이다
- DisconnectTimeoutMs는 RX가 "connected" 상태를 포기하고 재동기 모드로 가는 시간이다. **FC의 failsafe 시간이 아니다** (6.4 참고)

텔레메트리 비율 → 초당 downlink 패킷 (계산: rate / ratio):

| 모드 | rate | TLM 기본 | downlink 패킷/s | uplink RC 패킷/s |
|---|---|---|---|---|
| F1000 | 1000 | 1:128 | 7.8 | 992.2 |
| 2.4 LoRa 500 | 500 | 1:128 | 3.9 | 496.1 |
| 2.4 LoRa 250 | 250 | 1:64 | 3.9 | 246.1 |
| 2.4 LoRa 150 | 150 | 1:32 | 4.7 | 145.3 |
| 2.4 LoRa 50 | 50 | 1:16 | 3.1 | 46.9 |
| 900 LoRa 25 | 25 | 1:8 | 3.1 | 21.9 |

비율은 enum `TLM_RATIO_1_128 … TLM_RATIO_1_2`, 그리고 `TLM_RATIO_DISARMED`("disarm 중엔 기본 비율, arm 중엔 텔레메트리 없음") 가 있다 (`common.h`). 값 변환은 `TLMratioEnumToValue()`: `1 << (8 + TLM_RATIO_NO_TLM - enumval)`. 기본값들은 "초당 3~8개 downlink"로 거의 일정하게 맞춰져 있다는 게 보인다. **텔레메트리가 많은 시나리오(MAVLink over ELRS, MSP 터널)는 이 비율을 바꾸게 되고, 그만큼 RC uplink 슬롯이 준다**.

### 4.4 OTA 패킷 구조 (`src/lib/OTA/OTA.h`)

두 가지 크기:

```text
 OTA4 (8 bytes, "standard", CRC-14)
 byte0: [crcHigh:6][type:2]        type: 00 RCDATA, 01 DATA, 10 SYNC (uplink) / 00 LINKSTATS (downlink)
 byte1..5: 4 channels x 10 bit (CH0-3)      <- RCDATA
 byte6: [isArmed:1][switches:7]
 byte7: crcLow (8)                 CRC-14 poly 0x2E57 (implicit leading 1)

 OTA8 (13 bytes, "full res" / 8ch, CRC-16)
 byte0: [isArmed:1][isHighAux:1][uplinkPower:3][stubbornAck:1][packetType:2]
 byte1..5:  chLow  (CH0-3, 4 x 10 bit)
 byte6..10: chHigh (AUX2-5 or AUX6-9)
 byte11..12: crc16 little-endian    poly 0x3D65
```

SYNC 패킷 내용 (`OTA_Sync_s`): `fhssIndex`, `nonce`, `rfRateEnum`, `switchEncMode:1`, `newTlmRatio:3`, `geminiMode:1`, `otaProtocol:2`, `UID4`, `UID5`. RX는 이걸로 현재 hop 위치 · rate · 텔레메트리 비율을 맞춘다.

Downlink link stats (`OTA_LinkStats_s`): `uplink_RSSI_1:7` + `antenna:1`, `uplink_RSSI_2:7` + `modelMatch:1`, `lq:7` + `trueDiversityAvailable:1`, `SNR` int8. 비트필드에 꽉 채워 넣어서 4바이트다.

CRC 초기값이 바인딩에 묶여 있다 (`OTA.cpp`):

```c
OtaCrcInitializer = (UID[4] << 8) | UID[5];
OtaCrcInitializer ^= (uint16_t)OTA_VERSION_ID << 8;   // OTA_VERSION_ID = 4 (targets.h)
```

그래서 다른 바인딩 문구 또는 다른 OTA 버전의 패킷은 **CRC에서 탈락**한다. 이건 "필터"이지 **보안이 아니다**. ELRS 링크 코드에서 암호화 단계는 찾지 못했다 (`grep -ri "encrypt\|aes\|chacha" src/lib src/src` → LR1121 칩 펌웨어 업데이트 관련 opcode만 나옴). 방산 맥락에서 이 차이는 크다 (4.9).

### 4.5 FHSS — 바인딩 문구에서 hop 순서까지

```text
 binding phrase "my phrase"
        |
        v   MD5( '-DMY_BINDING_PHRASE="my phrase"' )[0:6]     (python/build_flags.py, binary_configurator.py)
 UID[0..5]
        |
        +--> seed = UID[2]<<24 | UID[3]<<16 | UID[4]<<8 | (UID[5] ^ OTA_VERSION_ID)   (OtaGetUidSeed)
        |         |
        |         v   rngSeed(seed); LCG  seed = (214013*seed + 2531011) mod 2^31; out = seed>>16
        |    FHSSrandomiseFHSSsequenceBuild(): 
        |      - 길이 = floor(256 / freqCount) * freqCount
        |      - 각 블록(freqCount개) 첫 칸 = sync channel (= freqCount/2)
        |      - 블록 안의 나머지 칸을 같은 블록 안 임의 칸과 swap  -> 블록마다 모든 채널 1번씩
        |
        +--> CRC init = UID[4..5] ^ (OTA_VERSION_ID<<8)
```

- 2.4 GHz ISM 도메인: 2400.4 ~ 2479.4 MHz, **80 채널**, 간격 (2479.4-2400.4)/79 = 1.0 MHz, 시퀀스 길이 240
- FCC915: 903.5 ~ 926.9 MHz, **40 채널**, 간격 0.6 MHz, 시퀀스 길이 240. EU868은 13 채널(시퀀스 247), AU915 20, IN866 4 등 (`FHSS.cpp` `domains[]`)
- 소스 주석이 요구사항을 적어 놨다: "1. 0 every n hops 2. No two repeated channels 3. Equal occurance of each … 4. Pseudorandom". 이게 그대로 **테스트 오라클**이다. ELRS의 `test/test_fhss/test_fhss.cpp`가 `test_fhss_unique`(블록 안에서 같은 주파수 두 번 금지), `test_fhss_same`(같은 시드 → 같은 시퀀스) 등으로 확인한다

hop 속도 계산 (interval × hop 간격):

| 모드 | 패킷 간격 | hop 간격 | 한 주파수 체류 | hop/s | 240칸 한 바퀴 |
|---|---|---|---|---|---|
| F1000 | 1 ms | 2 | 2 ms | 500 | 0.48 s |
| 2.4 LoRa 500 | 2 ms | 4 | 8 ms | 125 | 1.92 s |
| 2.4 LoRa 50 | 20 ms | 2 | 40 ms | 25 | 9.6 s |
| 900 LoRa 200 (FCC915) | 5 ms | 4 | 20 ms | 50 | 4.8 s |

hop 간격은 SYNC 패킷의 4비트 필드에 실린다 (`common.h` 주석: "Max value of 16 since only 4 bits have been assigned in the sync package").

FTE 관점 포인트: 시퀀스는 **바인딩 문구만 알면 누구나 재현 가능**하다. 공개 알고리즘 + MD5 + LCG다. 아마추어 링크에선 충분하지만, 의도적 방해(재밍 · 추종 재밍)를 가정하는 방산 링크에선 약점이다 [추정].

### 4.6 LQ 계산 (`src/lib/LQCALC/LQCALC.h`)

- `LQCALC<100>`: 최근 **100개 패킷 슬롯**의 비트맵. 슬롯이 시작될 때 `inc()`, 유효 패킷을 받으면 `add()`. `getLQ() = LQ * 100 / count`
- `reset()`은 count를 0으로 안 만든다. 주석: failsafe 뒤 LQ가 "100에서 내려오는" 게 아니라 "0에서 올라오도록". 즉 재연결 직후 LQ가 낮게 보이는 건 **정상 설계**다. 테스트에서 "재연결 후 LQ가 왜 바로 100이 아니지?"를 버그로 올리지 않도록
- 100 슬롯 = 시간으로는 rate에 따라 다르다: F1000 → 0.1 s, 500 Hz → 0.2 s, 50 Hz → 2 s. **같은 LQ 80%라도 50 Hz에선 2초 평균, 1000 Hz에선 0.1초 평균**이다. 서로 다른 rate의 LQ 그래프를 겹쳐 비교하면 안 된다
- 텔레메트리 슬롯을 LQ 분모에 넣는지는 [확인 필요] (`rx_main.cpp`에서 `LQCalc.inc()` 호출 위치를 더 읽어야 함)

### 4.7 연결 상태 머신과 failsafe

`connectionState_e` (`common.h`): `connected`, `tentative`(RX만), `awaitingModelId`(TX만), `disconnected`, 그 뒤 특수 상태(`wifiUpdate`, `serialUpdate` …), 실패 상태(`radioFailed`, `hardwareUndefined`).

```text
            sync 패킷 수신                   LQ > minLqForChaos && 주파수 오프셋 안정
 disconnected --------------> tentative ---------------------------------------> connected
      ^                          |  RxLockTimeoutMs 안에 못 잠금                    |
      |                          v                                                  |
      +------------- LostConnection() <---- now - LastValidPacket > DisconnectTimeoutMs
```

RX → FC 출력이 링크 손실에 어떻게 반응하나 (실제 코드):

- CRSF: `SerialCRSF::sendRCFrame()`은 `if (!frameAvailable) return;` — **새 패킷이 없으면 RC 프레임을 안 보낸다**. 그래서 FC는 "프레임이 끊김"으로 손실을 안다. TBS 규격도 failsafe "cut" 타입에선 RC 프레임을 멈추라고 하고, FC는 1초 기다린 뒤 failsafe를 시작하라고 권한다
- SBUS: `SerialSBUS::sendRCFrame()`은 failsafe 모드가 `FAILSAFE_NO_PULSES`면 출력을 멈추고, 아니면 계속 보내면서 flags 바이트에 `SBUS_FLAG_FAILSAFE_ACTIVE`, 패킷을 놓치면 `SBUS_FLAG_SIGNAL_LOSS`를 세운다. RX Lua 파라미터 "SBUS failsafe"의 선택지는 "No Pulses;Last Pos"
- PWM 수신기: ELRS 문서 기준 "connected 상태에서 LQ 0이 되거나 유효 채널 패킷 없이 1초 지나면" failsafe, 값은 988~2012 µs 범위에서 WebUI로 설정 (expresslrs.org PWM Receivers 페이지)

FC 쪽 (Betaflight `src/main/rx/rx.c`, `flight/failsafe.c`):

- `RXLOSS_TRIGGER_INTERVAL` 150 ms: 이 시간 동안 프레임이 없으면 `rxSignalReceived = false` (OSD "RXLOSS")
- `MAX_INVALID_PULSE_TIME_MS` 300 ms: 채널 값을 유지하는 시간
- `failsafe_delay` 기본 15 = 1.5 s: stage 1 → stage 2 (drop/land) 전환

> ⚠️ Gemini 정정: Gemini는 "RF 감쇠기로 통신을 끊었을 때 정확히 0.5초 뒤 모터 제어 신호가 0으로 떨어지는지"를 판정 기준으로 들었다. 실제 시간은 FC 설정에 달려 있다. Betaflight 기본값만 봐도 150 ms(RXLOSS) → 1.5 s(stage 2) 단계가 있고, stage 2 동작이 "drop"(모터 정지)인지 "land"인지 "GPS rescue"인지에 따라 DShot이 0이 되는 시점이 다르다. **기대값은 숫자 하나가 아니라 "설정 → 기대 시퀀스" 표로 정의**해야 한다. 자세한 단계는 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md).

### 4.8 Gemini 모드 · dual band · diversity

이름 주의: 여기 "Gemini"는 ELRS 기능 이름이다. 이 노트의 출발 자료인 Google Gemini와 관계없다.

| 기능 | 코드 근거 | 동작 |
|---|---|---|
| Antenna diversity (단일 radio, 안테나 2개) | `switchDiversityAntennas()`, link stats `antenna` 비트 | 수신 품질로 안테나를 전환 |
| True diversity (radio 2개) | `isDualRadio()`, `trueDiversityAvailable` | 두 radio가 동시에 수신 |
| **Gemini** (TX radio 2개) | `TX_RADIO_MODE_GEMINI = 0`, `inGeminiMode()`, SYNC의 `geminiMode` 비트 | 같은 패킷을 **같은 밴드의 두 주파수**로 동시에 송신. 두 번째 주파수 = 현재 시퀀스 인덱스 + (채널 수 / 2) (`FHSSGeminiFreq`) — 2.4 GHz면 약 40 MHz 떨어진 곳 |
| **Dual band** (LR1121) | `FHSSuseDualBand`, `RATE_LORA_DUAL_*` | 900 MHz와 2.4 GHz로 동시 송신. 코드 주석: dual band에선 Gemini 강제("Whats the point of using a single frequency!") |
| Team900 / Team2.4 | 문서 · 주석 용어 (`user_defines.txt`, `FHSS.cpp` 주석) | 900 MHz 제품군 / 2.4 GHz 제품군을 부르는 커뮤니티 이름. 코드 enum은 아님 |

FTE 의미: Gemini와 dual band는 **주파수 다이버시티**다. 좁은 대역 간섭이 한 주파수를 죽여도 다른 쪽이 산다. 테스트는 "한쪽 경로를 일부러 죽였을 때 LQ가 얼마나 유지되는가"로 한다 (6.6). TX가 Gemini인데 RX가 true diversity가 아니면 TX가 Switch 모드로 내려간다 (`tx_main.cpp` 164행 부근) — 이것도 설정 조합 테스트 항목이다.

### 4.9 방산 · anti-jam 맥락 [추정]

Neros 공고에 "anti-jam radios" 문구가 있고, ELRS가 6번 나온다. 공고 문구 외에는 전부 추정이다.

- ELRS의 FHSS는 **간섭 회피(coexistence)** 용 설계다. 의도적 재밍 대응이 목표가 아니다. 시퀀스 생성이 공개돼 있고 링크 암호화가 없다
- 방산 회사가 ELRS를 쓴다면 [추정]: (1) 프로토타입 · 훈련용, (2) ELRS를 포크해서 시퀀스 생성 · 인증 · 주파수 계획을 바꿈, (3) 별도 anti-jam 무전기를 쓰고 FC 쪽 인터페이스만 CRSF로 맞춤 — 어느 쪽인지 모른다
- 어느 경우든 FTE가 할 일은 같다: **링크 품질 지표(LQ · RSSI · SNR)를 감쇠 · 간섭 조건에서 정량 측정하고, 링크가 죽을 때 기체가 정의된 대로 행동하는지**를 자동으로 반복 확인하는 것
- 온사이트에서 이 주제를 꺼낼 땐 단정하지 말 것: "ELRS FHSS is designed for coexistence, not adversarial jamming, so I'd expect a defense product to either harden it or put a different radio behind the same CRSF interface — either way the test bench looks similar." 정도가 안전하다

### 4.10 ELRS 자체 테스트 (PlatformIO native)

확인한 사실:

- `src/platformio.ini`에 `[env:native]` (`platform = native`, `test_ignore = test_embedded`, `-D UNIT_TEST=1 -D TARGET_NATIVE`, 하드웨어 의존 라이브러리 `lib_ignore`)
- CI (`.github/workflows/build.yml`): `cd src` → `platformio pkg install --platform native` → `PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" pio test -e native`
- 테스트 프레임워크: **Unity** (`RUN_TEST`, `TEST_ASSERT_*`; 17개 파일이 `unity.h` include)
- `src/test/` 폴더: test_crc, test_crsf, test_crsf_endpoint, test_embedded(native에서 제외), test_fhss, test_fifo, test_fmap, test_gps, test_mdns, test_msp, test_msp_vtx, test_msp2crsf2msp, test_ota, test_scorpion, test_stubborn, test_telemetry (cpp 합계 약 5,970줄)

| 테스트 | 보는 것 | FTE가 배울 점 |
|---|---|---|
| test_crc | CRC14/16 구현 호환, 무작위 · 연속 bit flip 검출률 (`test_crc14_flip_random` 등) | CRC 검출 능력을 **수치로** 확인하는 방식 |
| test_fhss | 첫 채널 = sync, 블록 내 유일성, 같은 시드 재현성, 도메인별 | 요구사항 주석 → 테스트 1:1 |
| test_ota/test_switches | 채널 · 스위치 인코딩 (hybrid8, wide, 12ch, 16ch, full res) | 비트 패킹 회귀 테스트 |
| test_crsf | device info, 버전 변환 | 프로토콜 필드 |
| test_stubborn | StubbornSender/Receiver (텔레메트리 · MSP 조각 전송 재시도) | 손실 있는 링크 위 신뢰 전송 |

로컬 실행 (Don이 직접 할 때):

```text
git clone --depth 1 https://github.com/ExpressLRS/ExpressLRS
cd ExpressLRS/src
pip install platformio
PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" pio test -e native
pio test -e native -f test_fhss          # 하나만
```

눈여겨볼 점: native env의 `lib_ignore`에 `LQCALC`, `LBT`, `SX127xDriver`, `LR1121Driver`, `LR2021Driver`, `PWM`, `WIFI` 등이 들어 있다. 즉 **LQ 계산 템플릿은 native 단위 테스트 대상이 아니다**. 4.6의 LQ 동작(reset 뒤 0부터 올라옴, 100 슬롯 창)을 검증하는 테스트는 FTE가 HIL에서 직접 만들어야 한다.

빠진 것(=FTE가 채울 곳): 단위 테스트는 RF 동작 · 타이밍 · 실제 칩을 보지 않는다. `test_embedded`는 칩 위 테스트지만 CI에서 안 돈다. 그래서 **HIL에서 링크 품질 · failsafe · 재연결을 자동 측정하는 층**이 비어 있다. 이게 Neros FTE 자리의 가치 제안이 될 수 있다 [추정].

---

## 5. EdgeTX (조종기 OS)

### 5.1 역할

조종기 MCU(STM32 계열, 일부 ESP32 계열 [확인 필요])에서 도는 펌웨어. stick ADC → input → mixer → 채널 → 모듈 프로토콜(CRSF 등) 출력. 텔레메트리를 받아 화면 · 음성 · Lua 스크립트로 보여준다. ELRS 설정 메뉴(Lua)도 CRSF 파라미터 프레임으로 여기서 돈다.

### 5.2 확인한 코드 위치

| 파일 | 내용 |
|---|---|
| `radio/src/pulses/crossfire.cpp` | `createCrossfireChannelsFrame` (0xEE, len 25, status 바이트), `_processFrames` (1바이트 resync 파서), 모듈 UART 초기화 |
| `radio/src/telemetry/crossfire.cpp` / `.h` | 텔레메트리 디코드, `CROSSFIRE_BAUDRATES[]`, 주소 상수 (RADIO 0xEA, MODULE 0xEE, UART_SYNC 0xC8, CHANNELS_ID 0x16) |
| `radio/src/mixer_scheduler.cpp` | mixer 주기 스케줄러 (CRSF 모듈의 timing 프레임에 맞춤) |
| `radio/src/tests/crossfire.cpp` | gtest: `createCrossfireChannelsFrame`, `ExpressLRSArmingExtension_CH5Mode` / `_SwitchMode`, `crc8`, `frameParser_incompleteFrames`, `frameParser_length`, `frameParser_badFrames`, `frameParser_multipleJumboFrames`, `frameParser_droppedBytes` |
| `radio/util/crossfire-parse.py` | 캡처된 CRSF 바이트를 파싱하는 유틸 |
| `companion/src/simulation/telemetryprovidercrossfire.*` | PC 시뮬레이터에서 CRSF 텔레메트리 흉내 |

### 5.3 EdgeTX 테스트 실행

- 프레임워크: **googletest** (`radio/src/tests/gtests.cpp`, CMake 타깃 `gtests-radio`, 상위 타깃 `tests-radio`)
- `tools/commit-tests.sh`: 타깃별로 `cmake` → `cmake --build . --target native-configure` → `cmake --build native --target tests-radio`
- `radio/src/tests/CMakeLists.txt`: AddressSanitizer 옵션 `TESTS_ASAN` 있음 (AppleClang에선 끔)
- 정확한 로컬 빌드 옵션(어떤 `PCB=` 값을 줘야 하는지)은 [확인 필요] — `tools/commit-tests.sh`의 `get_target_build_options`를 읽으면 나온다

### 5.4 조종기 구간 테스트 아이디어

- **stick → CRSF 지연**: 실제 stick 대신 trainer 포트 입력이나 시뮬레이터로 **알려진 계단 입력**을 넣고, 모듈 CRSF 선에서 값이 바뀌는 시각을 잰다 ([K05](../../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md) latency 표의 첫 구간)
- **mixer 주기 ↔ RF 주기 동기**: ELRS TX가 0x3A timing 프레임으로 조종기 mixer 주기를 RF packet rate에 맞춘다. 맞으면 CRSF 프레임 간격이 RF interval과 같고 위상이 고정된다. 안 맞으면 "beat" 형태로 대기 시간이 출렁인다. 로직 분석기로 CRSF 프레임 간격 히스토그램을 보면 바로 보인다
- **baud 협상**: 400k/1.87M/5.25M 각각에서 프레임 손실 0인지, 고속에서 선 길이 · 레벨 변환기 때문에 깨지지 않는지
- **Lua 파라미터 경로**: extended 프레임(0x2B~0x2D) 읽기/쓰기 반복 soak — 설정 메뉴가 멈추는 버그는 현장에서 자주 보고된다 [추정]

---

## 6. RF 링크 테스트 방법 (ELRS 중심)

> **법 · 안전 경고.** 의도적인 RF 방해(재밍) 신호를 공중으로 내보내는 것은 미국에서 FCC가 금지한다 (jammer 사용 · 판매 · 광고 금지). 간섭 · 재밍 실험은 **차폐 박스(RF shield box) 안에서 동축 케이블로만(conducted)**, 또는 허가받은 시설에서만 한다. 송신 출력이 있는 장비(ELRS TX는 최대 1 W급 제품도 있음)를 수신기에 직결하면 **수신기 입력단이 탈 수 있다** — 반드시 감쇠기를 먼저 넣고, 감쇠기 정격 전력도 확인한다. 장비 구매(감쇠기, 차폐 박스, SDR)는 사기 전에 Don과 예산 확인.

### 6.1 conducted 셋업

```text
  +---------+   SMA   +-------+   +----------------+   +-------+   SMA   +---------+
  | ELRS TX |--coax-->| 30 dB |-->| prog. step att |-->| splitter|-coax->| ELRS RX |
  | module  |         | fixed |   | 0..95 dB, 1 dB |   | (opt.)  |       | (+ FC)  |
  +---------+         +-------+   +----------------+   +---+---+         +---------+
       ^                                   ^               |
       | CRSF (UART)                       | USB/LAN       +--> SDR / spectrum analyzer
       |                                   |                    (monitor, or inject
  +----------+   USB   +---------------------------+            interferer via combiner)
  | CRSF     |<------->| test PC (Python)          |
  | injector |         | - step attenuation        |
  +----------+         | - read link stats (0x14)  |
                       | - log LQ/RSSI/SNR vs dB   |
                       +---------------------------+
   TX, RX 각각 차폐 박스 안. 안테나 대신 동축. 박스 사이 누설 << 측정 범위여야 함.
```

핵심 수치 감각:

- 경로 손실 합 = 고정 감쇠 + 가변 감쇠 + 케이블 · 커넥터 손실. 측정 범위 상한은 **차폐 누설**이 정한다. 박스 격리가 부족하면 감쇠기를 더 올려도 RSSI가 안 떨어지는 "바닥"이 생긴다 — 그 바닥이 측정 오류의 첫 번째 신호다
- 수신 전력 = TX 출력(dBm) - 총 감쇠(dB). 예: TX 100 mW = +20 dBm, 총 125 dB → -105 dBm (2.4 LoRa 500 Hz 감도 근처)

> ⚠️ Gemini 정정: Gemini는 "감쇠량을 95 dB까지 올려 드론이 20 km 떨어진 상황을 모사"한다고 했다. 자유공간 경로 손실(FSPL)은 `20·log10(d_km) + 20·log10(f_MHz) + 32.44`. 2.44 GHz, 20 km면 약 **126 dB**다. 실제 비행에선 여기에 안테나 이득(+), 지면 반사 · 프레넬 영역 · 몸체 가림(-)이 더해진다. "감쇠 X dB = 거리 Y km"는 **TX 출력 · 안테나 이득 · 페이딩 여유를 같이 적을 때만** 의미가 있다. 감쇠기 테스트의 결과물은 거리보다 **"수신 전력(dBm) 대 LQ" 곡선**으로 적는 게 정직하다.

### 6.2 LQ / RSSI vs 감쇠 곡선 (sensitivity sweep)

절차:

1. rate 고정 (예: 2.4 LoRa 500 Hz), TX 출력 고정 (dynamic power 끔), 텔레메트리 비율 고정
2. 감쇠를 1 dB씩 올리며 각 단계 N초 머문다 (LQ 창이 100 슬롯이므로 최소 수 초 — 50 Hz면 2초 창이라 더 길게)
3. 각 단계에서 기록: uplink RSSI, uplink LQ, SNR, downlink 값, FC가 본 RC 프레임 수, CRC 에러 수
4. 끊긴 뒤 감쇠를 다시 내리며 기록 (**히스테리시스** — 연결 회복은 끊김보다 더 좋은 신호를 요구한다)
5. rate별 반복 → 실측 감도 vs 코드의 `RXsensitivity` 비교

기대 모양:

```text
 LQ %
 100 |##############################
     |                              ##
  50 |                                #
     |                                 #
   0 |__________________________________###########
     -60   -80   -90  -100  -104 -106  -110   RX power (dBm)
                                ^ knee: code says -105 dBm for 2.4 LoRa 500
 RSSI: 기울기 -1 dB/dB 직선 -> 감도 근처에서 노이즈 바닥에 붙어 평평해짐
```

판정 아이디어:

- LQ 50% 지점이 코드 감도 ± N dB 안인지 (N은 팀이 정함)
- RSSI 직선성: 기울기가 -1에서 벗어나면 측정 체인(누설, 포화) 문제
- 펌웨어 버전 A/B를 같은 셋업에서 비교 → **RF 회귀 테스트**. SSD의 shmoo plot과 같은 발상이다

### 6.3 링크 예산 계산 예 (계산, 측정 아님)

조건: TX +20 dBm(100 mW), TX/RX 안테나 각 2 dBi, 감도는 코드 값. 최대 허용 경로 손실 = 20 + 2 + 2 - 감도.

| 모드 | 감도 | 허용 손실 | FSPL 거리 (2.44 GHz, 여유 0 dB) | 10 dB 페이드 여유 |
|---|---|---|---|---|
| F1000 (FLRC) | -104 | 128 dB | 약 24.6 km | 약 7.8 km |
| LoRa 500 | -105 | 129 dB | 약 27.6 km | 약 8.7 km |
| LoRa 50 | -115 | 139 dB | 약 87 km | 약 27.6 km |

자유공간은 이론 상한이다. 저고도 FPV는 지면 · 장애물 때문에 훨씬 짧다. 이 표의 쓸모는 "**rate를 10배 낮추면 감도 10 dB → 거리 약 3배**"라는 감각과, 시험 결과를 해석할 기준선이다.

### 6.4 failsafe 시간 · 재연결 시간 측정

```text
 time --->
 attenuator: ____0 dB____|^^^^^^ 120 dB (link cut) ^^^^^^|____0 dB____________
                          t0                              t3
 RX CRSF RC frames: ||||||||                               ...  ||||||||||||
                          t1 (last frame)                       t4 (first frame back)
 FC state:          OK   OK  | RXLOSS (t1+150ms) | FS stage2 (t1+~1.5s) | ... | OK
 DShot value:       thr  thr thr thr ...  -> 0 or land-throttle (t2)     ...  (re-arm rules)
```

측정 항목 (로직 분석기 3채널 + 감쇠기 트리거 GPIO):

- t1 - t0: RF 끊김 → RX가 CRSF 출력을 멈추기까지 (ELRS는 새 패킷이 없으면 안 보내므로 거의 다음 슬롯)
- t2 - t1: FC가 failsafe 동작을 DShot에 반영하기까지 (설정별 기대 시퀀스와 비교)
- t4 - t3: 신호 복구 → 첫 RC 프레임 (재동기: sync 채널 대기 + tentative → connected)
- 반복: 수백 회 → 분포 (평균 · p99 · 최대). 재연결 시간은 hop 시퀀스 위치에 따라 달라지므로 **분포가 넓을 것**이다 [추정]

안전: 이 테스트는 **프롭 없이**, 모터는 빼거나 전류 제한 전원으로. failsafe 테스트는 "모터가 예상과 다르게 도는" 상황을 일부러 만드는 테스트다.

### 6.5 range test (실외) 방법론

- 실외 거리 시험은 비용이 크다. 먼저 conducted로 "감도 곡선"을 확보하고, 실외는 **곡선과 맞는지 확인하는 표본 시험**으로 쓴다
- 흔한 커뮤니티 방법: TX 출력을 크게 낮추고(예: 10 mW 수준) 짧은 거리에서 LQ가 떨어지는 지점을 찾은 뒤 출력 비율로 외삽. 출력 10 dB 차이 = 자유공간 거리 약 3.16배. ELRS 공식 문서의 권장 절차는 [확인 필요]
- 기록: GPS 위치(FC 블랙박스 · CRSF GPS 텔레메트리), 고도, 안테나 방향, LQ · RSSI 시계열. **같은 경로를 펌웨어 A/B로 반복**
- 법: 공중 송신은 해당 지역 규정(FCC Part 15 ISM 출력 한도, 도메인 설정)을 지킨다. 900 MHz 도메인을 잘못 고르면 그 자체가 위반이 될 수 있다

### 6.6 간섭 · 재밍 에뮬레이션 (차폐 환경 한정)

- 방법: conducted 경로에 combiner로 SDR 신호를 합친다. 신호 종류 — (1) 단일 톤을 한 ELRS 채널에, (2) 대역 일부를 덮는 잡음, (3) 2.4 GHz Wi-Fi 같은 버스트 트래픽 재생
- 측정: 간섭 대역폭(몇 채널을 덮나) vs LQ. 80채널 중 k개가 막히면 이론상 LQ ≈ (80-k)/80 근처가 기대값이다 [추정 — 실제로는 sync 채널이 막히면 재연결이 어려워지는 등 비선형]
- **sync 채널(freqCount/2) 집중 방해**: 연결된 상태에선 영향이 작고, 끊긴 뒤 재연결은 sync 채널에서 기다리므로 크게 지연될 것이다 [추정]. 이게 "재연결 견고성" 테스트 케이스가 된다
- Gemini · dual band 효과: 한 밴드만 방해 → LQ 유지되는지
- 다시 강조: **차폐 박스 + 동축 + 출력 제한** 밖에서는 하지 않는다. SDR 송신 기능을 쓰는 것 자체가 규정 대상이다

### 6.7 SDR · 스펙트럼으로 hop 확인

- 수신 전용 SDR(예: HackRF, bladeRF, USRP — 구매 전 예산 확인)로 2.4 GHz 대역 waterfall을 보면 hop 패턴이 보인다
- 테스트: 실측 hop 순서를 `FHSSrandomiseFHSSsequenceBuild` Python 재구현 결과와 비교 → "바인딩 문구 X일 때 시퀀스가 맞나", "도메인 설정이 맞나(채널 수, 시작 주파수)". 규제 적합성 사전 점검으로도 쓸 수 있다 [추정]

---

## 7. SBUS

### 7.1 구조

Futaba의 비공개 규격을 커뮤니티가 역공학한 것이다. 단방향 (RX → FC).

```text
 UART: 100000 baud, 8 data, EVEN parity, 2 stop  (8E2),  INVERTED logic
 byte:  0      1 .................... 22    23        24
      +------+-----------------------------+---------+--------+
      | 0x0F | 16 ch x 11 bit = 22 bytes   | flags   | 0x00   |
      +------+-----------------------------+---------+--------+
 flags bit0 = ch17 (digital), bit1 = ch18 (digital),
       bit2 = frame lost (signal loss), bit3 = failsafe active
 채널 비트 순서: CRSF와 같은 little-endian 11-bit 패킹
```

근거: Betaflight `src/main/rx/sbus.c` (`SBUS_BAUDRATE 100000`, `SBUS_PORT_OPTIONS (SERIAL_STOPBITS_2 | SERIAL_PARITY_EVEN)`, `SBUS_FRAME_BEGIN_BYTE 0x0F`, `SBUS_FRAME_SIZE = 데이터 + 2`), `sbus_channels.c` (`SBUS_FLAG_CHANNEL_17 (1<<0)`, `_18 (1<<1)`), `sbus_channels.h` (`SIGNAL_LOSS (1<<2)`, `FAILSAFE_ACTIVE (1<<3)`). ELRS `SerialSBUS.cpp`도 `0x0F` + 채널 + flags + `0x00`을 쓴다. 채널 범위 Betaflight `SBUS_DIGITAL_CHANNEL_MIN 173` / `MAX 1812`.

### 7.2 타이밍

- 바이트당 12비트 (start 1 + data 8 + parity 1 + stop 2) → 25 × 12 = 300비트 → 100 kbaud에서 **3.0 ms**. Betaflight `SBUS_TIME_NEEDED_PER_FRAME 3000`과 일치
- 바이트당 120 µs, Betaflight interbyte 타임아웃 = 5 × 120 = 약 600 µs
- 프레임 간격: 원래 Futaba 기준 14 ms(아날로그 서보) / 7 ms(고속) 로 알려져 있다 [확인 필요]. ELRS SBUS 출력 콜백 간격은 `SBUS_CALLBACK_INTERVAL_MS = 9`
- **CRC가 없다**. parity 비트(바이트당 1비트)와 시작 · 끝 바이트뿐이다. 무결성 보호가 CRSF보다 약하다

### 7.3 고장 모드와 테스트

| 고장 | 테스트 |
|---|---|
| 반전 안 맞음 (F4 FC는 하드웨어 인버터 필요 등) | 반전/비반전 매트릭스, 0프레임 감지 |
| parity 오류 무시하는 구현 | parity 깨진 바이트 주입 → 프레임이 버려지는지 |
| CRC 없음 → 비트 오류가 그대로 채널값으로 | 단일 bit flip 주입 → FC가 받아들이는지 기록 (위험 평가) |
| failsafe 비트 처리 | bit3 세운 프레임 → FC가 즉시 failsafe로 가는지, bit2만 세우면? |
| ch17/ch18 디지털 채널 | 0/1 → 173/1812 매핑 |
| 끝 바이트 0x00 아닌 변형 | 일부 수신기는 0x04 등 다른 끝 바이트를 쓴다는 커뮤니티 보고 [확인 필요] — 파서가 끝 바이트를 엄격히 볼지 정책 결정 필요 |

로직 분석기: sigrok에 **`sbus_futaba` 디코더가 있다** (입력은 `uart` 디코더 출력). UART 디코더를 100000 8E2 inverted로 설정하고 그 위에 쌓는다 (sigrok.org/wiki/Protocol_decoders, 2026-10-24 확인).

---

## 8. DShot

### 8.1 구조

FC → ESC 디지털 스로틀. 아날로그 PWM(OneShot/MultiShot) 대신 16비트 디지털 프레임. 공식 규격서는 없고 Betaflight 코드와 커뮤니티 문서(brushlesswhoop.com)가 사실상 규격이다.

```text
 bit: 15                         5    4    3       0
     +----------------------------+----+---------+
     | value (11 bit)             | T  | CRC (4) |      MSB first on the wire
     +----------------------------+----+---------+
 value 0       : disarmed / motor stop (= command 0)
 value 1..47   : special commands  (DSHOT_CMD_MAX = 47)
 value 48..2047: throttle, 2000 steps (DSHOT_MIN_THROTTLE 48, MAX 2047)
 T             : telemetry request (구형 별선 ESC telemetry 요청)
 v12 = (value << 1) | T
 CRC normal = (v12 ^ v12>>4 ^ v12>>8) & 0xF
 CRC bidir  = (~(v12 ^ v12>>4 ^ v12>>8)) & 0xF
```

Betaflight `src/main/drivers/dshot.c` `prepareDshotPacket()`가 정확히 이렇다 (nibble XOR 3번, `useDshotTelemetry`면 `csum = ~csum`).

비트 하나의 모양: 비트 시작은 항상 high, high 시간이 길면 1, 짧으면 0. 1의 high 시간은 0의 두 배 (T1H = 비트의 75%, T0H = 37.5%).

```text
 one bit (normal DShot, idle low):
   "1":  ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|____     T1H = 0.75 * bit
   "0":  ‾‾‾‾‾‾‾‾|_____________     T0H = 0.375 * bit
         |<------ bit ------>|
 bidirectional DShot: 전체 반전 (idle high, 펄스가 low)
```

### 8.2 타이밍 표 (brushlesswhoop 표, `dshot.py`로 재계산)

| DShot | bitrate | bit | T1H | T0H | 16비트 프레임 |
|---|---|---|---|---|---|
| 150 | 150 kbit/s | 6.67 µs | 5.00 µs | 2.50 µs | 106.7 µs |
| 300 | 300 kbit/s | 3.33 µs | 2.50 µs | 1.25 µs | 53.3 µs |
| 600 | 600 kbit/s | 1.67 µs | 1.25 µs | 0.625 µs | 26.7 µs |
| 1200 | 1200 kbit/s | 0.83 µs | 0.625 µs | 0.313 µs | 13.3 µs |

> 확인: Gemini/K05의 "DShot600 16비트 × 1.67 µs ≈ 27 µs"는 맞다 (26.7 µs).

### 8.3 special commands (0~47)

Betaflight `src/main/drivers/dshot_command.h`와 brushlesswhoop 표:

| 값 | 이름 (BF enum) | 비고 |
|---|---|---|
| 0 | MOTOR_STOP | |
| 1~5 | BEACON1~5 | 다음 명령까지 260 ms 이상 |
| 6 | ESC_INFO | 12 ms 이상 |
| 7, 8 | SPIN_DIRECTION_1 / 2 | 6번 반복 전송 필요 |
| 9, 10 | 3D_MODE_OFF / ON | 6번 |
| 11 | SETTINGS_REQUEST | 미구현 |
| 12 | SAVE_SETTINGS | 6번, 이후 35 ms |
| 13, 14 | EXTENDED_TELEMETRY_ENABLE / DISABLE | 6번, EDT 지원 ESC만 |
| 20, 21 | SPIN_DIRECTION_NORMAL / REVERSED | 6번 |
| 22~29 | LED0~3 ON/OFF | BLHeli32 전용 |
| 30, 31 | AUDIO_STREAM / SILENT_MODE (KISS) | |
| 32~35 | 텔레메트리 신호선 제어 | 6번 (brushlesswhoop) |
| 42~47 | 개별 텔레메트리 요청 (온도 · 전압 · 전류 · 소비 · eRPM · eRPM period) | brushlesswhoop |

규칙: 명령은 **모터가 멈춘 상태에서만** 처리된다 (0~36, brushlesswhoop). Betaflight는 `DSHOT_CMD_TYPE_INLINE`(모터 신호에 섞어 보냄) / `DSHOT_CMD_TYPE_BLOCKING`(모터 꺼진 상태에서 블로킹 전송) 두 방식이 있다.

FTE 포인트: "6번 반복"과 "명령 사이 대기 시간"은 **ESC 쪽 상태 머신**의 요구다. FC가 이걸 어기면 "가끔 방향 설정이 안 저장됨" 같은 간헐 버그가 된다. 로직 분석기로 명령 전송 횟수와 간격을 세는 자동 검사가 좋은 회귀 테스트다.

### 8.4 bidirectional DShot (eRPM 텔레메트리)

```text
 FC drives (inverted DShot frame)        line released, ESC drives (GCR, not inverted)
 |<---- 16 bits (26.7 us @600) ---->|<-- ~30 us -->|<---- 21 bits @ 750 kbit/s (28 us) ---->|
 ‾‾‾|_|‾‾|__|‾ ... (low pulses)      ‾‾‾‾‾‾‾‾‾‾‾‾‾‾  0 then NRZI(GCR(20 bits))

 ESC answer (16 bit before GCR):
  bit 15..13  12..4        3..0
     eee      mmmmmmmmm    crc        period_us = m << e   (e-RPM period)
  crc: xor of all 4 nibbles == 0xF  (Betaflight decode_bb_value)
  0x0FFF -> motor stopped
  GCR: each nibble -> 5 bits (20), prefix 0 -> 21 bits, a '1' toggles the line
  decode: gcr = line ^ (line >> 1)
```

근거: Betaflight `dshot_bitbang_decode.c` `decode_bb_value()` (GCR 5b→4b 테이블, `(csum & 0xf) != 0xf`면 무효), `dshot.c` `dshot_decode_eRPM_telemetry_value()` (`value = (value & 0x01ff) << ((value & 0xfe00) >> 9)`, 결과는 eRPM/100). 30 µs 간격 · 5/4 bitrate · 21비트는 brushlesswhoop 문서.

eRPM → 기계 RPM: `mechanical RPM = eRPM / (모터 극 수 / 2)`. Betaflight `erpmToHz = ERPM_PER_LSB / SECONDS_PER_MINUTE / (motorPoleCount / 2.0f)`. 14극 모터(7 pole pair)에서 140,000 eRPM = 20,000 RPM. 이 값이 **RPM filter**(모터 회전수 기반 노치)에 들어간다.

> ⚠️ Gemini 정정 (작은 것): Gemini는 bidir DShot의 eRPM이 "동적 노치 필터에 쓰인다"고 했다. Betaflight에서 eRPM을 쓰는 건 **RPM filter**(`USE_RPM_FILTER`, `rpm_filter_lpf_hz`)이고, dynamic notch는 gyro FFT 기반의 별개 필터다. 둘 다 노치라서 섞어 부른 듯하다.

**Extended DShot Telemetry (EDT)**: 12비트 값의 상위 4비트(`eee` + 다음 비트)에서 마지막 비트가 0이고 0이 아니면 eRPM이 아니라 EDT 프레임. type 0x2 온도(°C), 0x4 전압(0.25 V), 0x6 전류(A), 0x8/0xA/0xC debug, 0xE state/event. Betaflight `dshot_decode_telemetry_value()`가 `isErpm = !isEdtEnabled || (telemetryType & 0x01) || (telemetryType == 0)`으로 가른다.

### 8.5 타이밍 예산 계산 — bidir DShot이 PID 주기에 들어가나

한 번의 교환 = FC 프레임 + 약 30 µs 전환 + 응답 21비트 (5/4 rate).

| DShot | FC 프레임 | 간격 | 응답 21비트 | 합계 (여유 제외) | 8 kHz (125 µs) | 4 kHz (250 µs) |
|---|---|---|---|---|---|---|
| 300 | 53.3 µs | 30 µs | 56.0 µs | 139.3 µs | 안 들어감 | 들어감 |
| 600 | 26.7 µs | 30 µs | 28.0 µs | 84.7 µs | 들어감 | 들어감 |

계산상 DShot300 + bidir은 8 kHz 루프 한 주기에 들어가지 않는다. Betaflight가 이 조합을 어떻게 제한 · 경고하는지는 [확인 필요]. 이런 "타이밍 예산 표"를 만들어 두면 설정 매트릭스 테스트에서 **불가능한 조합을 미리 걸러** 시험 시간을 줄인다.

### 8.6 CRC 검출 능력 — 숫자로 알기

`dshot.py` self-test 4번 결과:

- 4비트 XOR CRC는 **모든 1비트 오류를 잡는다**
- 그러나 **같은 nibble 위치의 비트 2개**가 뒤집히면(예: 마스크 `0x0110`) **2048개 값 전부에서 못 잡는다**. XOR가 상쇄되기 때문이다
- 의미: 크로스토크 · 반사로 생기는 버스트 오류에 약하다. 안전 판단(예: 모터가 갑자기 최대 출력)을 CRC 하나에 기대면 안 되고, FC 쪽 "값 변화율 제한" 같은 다른 방어가 필요한지 FTE가 질문할 수 있다

GCR 응답 쪽은 단일 비트 오류를 GCR 테이블 위반 또는 CRC로 대부분 잡는다 (self-test 9번: 무작위 300개 × 20비트 위치에서 90% 이상 검출 assert).

### 8.7 DShot 테스트하는 법

**conformance**

- 프레임 인코딩: 모든 2048 값 × T비트 × normal/bidir를 기준 구현과 비교 (`dshot.py` 테스트 2 — Betaflight 함수를 줄 단위로 옮긴 것과 대조)
- 고정 벡터: `0` → `0x0000`, bidir `0` → `0x000F`, `48` → `0x0606`, `48`+T → `0x0617`, bidir `48` → `0x0609`
- 파형: 로직 분석기로 bit 주기 · T1H · T0H 측정 → 허용 오차 (ESC 펌웨어가 받는 오차 범위는 [확인 필요])
- 명령: 반복 횟수 · 간격 · "모터 정지 중에만" 규칙

**robustness**

- ESC 시뮬레이터(MCU)로 잘못된 CRC 프레임을 FC에 되돌려 주기 (bidir 응답 fuzz) → FC가 eRPM 무효 처리하고 RPM filter가 이상해지지 않는지
- 응답 없음(ESC 꺼짐) → `DSHOT_TELEMETRY_NOEDGE` 류 처리, 오류 카운터 증가 확인

**타이밍 · 디코더**

- sigrok 공식 디코더 목록에 **DShot 디코더는 없다** (2026-10-24 확인). 선택지: sigrok `pwm` 디코더로 duty를 뽑아 후처리, Saleae 확장(커뮤니티 DShot 확장 존재 여부 [확인 필요]), 또는 raw 샘플을 CSV로 뽑아 `dshot.py`의 `waveform_to_frame()`으로 직접 디코드
- 샘플링: DShot600 T0H 0.625 µs를 구분하려면 최소 수 MHz, 넉넉히 **24 MS/s 이상**. bidir 응답 750 kbit/s도 마찬가지
- 측정 항목: CRSF 프레임 끝 → DShot 값 변화 시간 (latency 구간), DShot 프레임 간격 지터 (PID 루프 지터의 외부 관측)

---

## 9. MSP (MultiWii Serial Protocol)

### 9.1 구조 (Betaflight `src/main/msp/msp_serial.c`, `msp_protocol.h`)

```text
 MSP v1:  '$' 'M' dir  size  cmd  payload[size]  chk
          chk = XOR(size, cmd, payload...)
          dir: '<' to FC (request), '>' from FC (response), '!' error
 MSP v1 jumbo: size = 255 -> 뒤에 실제 크기 2바이트 (JUMBO_FRAME_SIZE_LIMIT 255)

 MSP v2:  '$' 'X' dir  flag  cmd(2, LE)  size(2, LE)  payload  crc8
          crc8 = CRC-8 DVB-S2 (poly 0xD5) over flag..payload   (crc8_dvb_s2)
 MSP v2 over v1: v1 frame with cmd = 255 (MSP_V2_FRAME_ID), v2 header inside
```

오버헤드: v1 6바이트, v2 9바이트. CRSF와 **같은 CRC-8 0xD5**를 v2가 쓴다 — `crsf.py`의 `crc8_d5`를 그대로 재사용할 수 있다.

자주 쓰는 명령 (`msp_protocol.h`): `MSP_API_VERSION 1`, `MSP_STATUS 101`, `MSP_RAW_IMU 102`, `MSP_MOTOR 104`, `MSP_RC 105`, `MSP_ATTITUDE 108`, `MSP_SET_RAW_RC 200`. 헤더 주석: 클라이언트는 먼저 `MSP_API_VERSION`으로 버전을 확인하라.

### 9.2 FTE에게 MSP가 뭔가

HIL 자동화의 **손잡이**다. Configurator가 하는 모든 일(설정 읽기 · 쓰기, 센서값 읽기, 모터 테스트, RC 덮어쓰기)을 Python에서 할 수 있다. 다만:

- MSP는 요청-응답이다. 응답 timeout · 재시도 · 순서 꼬임을 클라이언트가 처리해야 한다
- `MSP_SET_RAW_RC`로 RC를 넣는 것과 CRSF로 RC를 넣는 것은 **다른 경로**다. 실제 비행 경로를 시험하려면 CRSF 주입이 맞고, MSP RC는 편의용이다 (Betaflight `rx_msp_override_unittest.cc`가 MSP override 기능을 시험한다)
- CRSF 안에 MSP를 터널링할 수 있다 (0x7A/0x7B/0x7C, ELRS `CRSF_MSP_REQ_PAYLOAD_SIZE 8`, `RESP 58`) — 무선으로 FC 설정 변경. 느리고 조각나므로 텔레메트리 비율의 영향을 받는다

### 9.3 MSP 테스트 아이디어

- 명령별 응답 길이 · 필드 conformance (API 버전별 표로 관리)
- 잘못된 체크섬 · 잘린 프레임 · 알 수 없는 명령 → FC가 '!' 오류 응답 또는 무시, 멈추지 않음
- **soak**: 1시간 동안 `MSP_STATUS` 폴링 + 비행 루프 부하 → 루프 시간(cycle time) 영향 측정. MSP 처리가 비행 루프를 밀어내지 않는지
- Betaflight 단위 테스트 참고: `msp_serial_unittest.cc`, `msp_reboot_unittest.cc` ([R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md))

---

## 10. MAVLink v2

### 10.1 구조 (mavlink.io serialization, message signing 페이지 확인)

```text
 offset: 0     1    2         3         4    5      6       7..9       10 .. n+9   n+10..n+11  (+13)
       +-----+----+---------+---------+----+------+-------+----------+-----------+-----------+-----------+
       | FD  |len |incompat |compat   |seq |sysid |compid | msgid 24 | payload n | crc16 LE  | signature |
       +-----+----+---------+---------+----+------+-------+----------+-----------+-----------+-----------+
 v2: 최소 12 B, 최대 280 B (서명 포함). v1: STX 0xFE, msgid 8 bit, 최소 8 B, 최대 263 B
 crc16 = CRC-16/MCRF4XX over [len .. payload] + CRC_EXTRA (1 byte)
 incompat_flags bit0 (0x01) = MAVLINK_IFLAG_SIGNED -> 13 B signature appended
 signature = link_id(1) + timestamp(6, 10 us units since 2015-01-01 GMT) + sig(6)
 sig = first 48 bits of SHA-256(secret_key(32B) + header + payload + CRC + link_id + timestamp)
```

**CRC_EXTRA**: 메시지 정의(이름, 기본 필드의 타입 · 이름 · 순서, 배열 길이)로 만든 CRC를 1바이트로 접은 값 `(crc & 0xFF) ^ (crc >> 8)`. 패킷에 실리지 않고 양쪽이 알고 있어야 한다. **송수신 측 XML 정의가 다르면 CRC가 틀린다** — 버전 불일치를 잡는 장치다. 확장 필드(extension)는 계산에서 빠진다.

**필드 재배열**: 와이어 순서는 크기 큰 순(8 → 4 → 2 → 1바이트), 같은 크기는 원래 순서 유지, 확장 필드는 맨 뒤.

**payload 끝 0 잘라내기 (v2만)**: 끝쪽 0바이트를 보내지 않고 len을 줄인다. 첫 바이트는 남긴다. 수신 측은 0으로 채워 복원해야 한다. → **파서 테스트 단골 항목**: "len이 정의보다 짧은 메시지"를 정상으로 받아야 한다.

**서명 재전송 방지**: 스트림 = (sysid, compid, link_id). timestamp는 스트림 안에서 엄격히 증가. 이전보다 오래됐거나, 서명이 틀리거나, 로컬 시간보다 1분(6,000,000 단위) 넘게 뒤처지면 버림. 서명 없는 패킷을 받을지는 구현 정책 (예: USB 같은 보안 채널, `RADIO_STATUS`는 항상 허용 등을 문서가 제안).

### 10.2 크기 · 시간 감각 (계산)

- HEARTBEAT payload 9 B → v2 프레임 21 B (서명 시 34 B). 57600 baud 8N1에서 21 × 10 / 57600 = **3.6 ms**, 115200에서 1.8 ms
- 텔레메트리 링크가 느리면(무선 모뎀, ELRS MAVLink 모드) 메시지 스케줄링이 핵심이 된다

ELRS도 MAVLink를 다룬다: RX 직렬 프로토콜 enum에 `PROTOCOL_MAVLINK` (`common.h`), `src/lib/MAVLink/`, `src/src/rx-serial/SerialMavlink.cpp`. 즉 **ELRS 링크 위로 MAVLink를 실어 PX4/ArduPilot와 GCS를 잇는 구성**이 가능하다. Neros 공고의 ELRS + MAVLink 조합과 맞물릴 수 있다 [추정].

### 10.3 MAVLink 테스트 아이디어

- **pymavlink를 오라클로**: 내 파서 또는 펌웨어 출력을 pymavlink 디코드 결과와 비교 ([R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)의 autotest가 이 방식)
- seq 번호로 **손실률 측정**: 링크 품질 지표를 프로토콜 계층에서 얻는다
- CRC_EXTRA 불일치 시나리오: dialect XML 버전이 다른 GCS ↔ FC → 메시지가 조용히 버려지는지, 경고가 나는지
- 끝 0 잘림 메시지 수신, incompat flag 미지원 비트 → 버림 확인
- 서명: 키 불일치, timestamp 역행, 1분 넘게 지난 패킷 재전송(replay) → 거부 확인. 방산 맥락에서 의미가 크다 [추정]
- 대용량 · 버스트: 파라미터 전체 다운로드, 미션 업로드 중 링크 감쇠 → 재시도 로직

---

## 11. SSD 검증에서는 이랬다 → 드론에서는 이렇다

| 관점 | 데이터센터 SSD (Don 경험 범위) | 드론 RC/모터 링크 |
|---|---|---|
| 규격 | PCIe · NVMe 규격서가 두껍고 compliance 프로그램이 있다 | CRSF는 짧은 TBS 문서 + ELRS 확장, DShot · SBUS는 코드와 커뮤니티 문서가 규격. **"규격 = 레퍼런스 구현"** |
| 프로토콜 관찰 | protocol analyzer로 PCIe 트래픽, DSO로 신호 | 로직 분석기 + UART/PWM 디코더, CRSF · DShot은 디코더를 직접 만들어야 할 수 있음 |
| 오류 검출 | LCRC/ECRC, 재전송(ACK/NAK), NVMe 상태 코드 | CRSF CRC-8, ELRS OTA CRC-14/16, DShot 4-bit XOR, SBUS는 parity만. **재전송 없음** (RC는 최신 값이 중요해서 옛 패킷 재전송 의미 없음) |
| 오류 대응 | error reporting/handling scheme, 재시도, 복구 | 다음 프레임 기다리기, 일정 시간 지나면 failsafe 단계 진입 |
| 손실의 의미 | 데이터 손실은 실패 | 패킷 손실은 정상 상황, **손실률 대비 거동**이 테스트 대상 |
| 텔레메트리 | NVMe telemetry log, health monitoring | CRSF link stats(LQ/RSSI/SNR), bidir DShot eRPM, MAVLink 상태 |
| 마진 시험 | shmoo (전압 · 온도 · 타이밍 sweep) | 감쇠 sweep(LQ vs dBm), baud 오차 sweep, DShot 타이밍 오차 sweep — **같은 shmoo 발상** |
| stress로 잠재 결함 | stress 기반 latent defect 발굴 | fuzz · 장시간 soak · 간섭 주입으로 간헐 오류 발굴 |
| 저수준 버스 | I2C/SPMI/RFFE 루트코즈 (최근 RF 칩셋 통합) | RF 칩(Semtech) SPI 제어, 칩 상태 · 타이밍. RF 칩셋 통합 경험이 ELRS 칩 드라이버 읽기에 바로 연결된다 |
| 자동화 | 챔버 플랫폼 SDK(UART 테스트 시퀀스 자동화 등) | 감쇠기 · 전원 · CRSF 주입기 · 로직 분석기를 묶는 Python 하네스 ([R08](2026-10-24_R08_hil_bench_design_instruments.md), [R09](2026-10-24_R09_scalable_reusable_test_platform.md)) |
| 안전 | 데이터 무결성 | **사람 · 기체 안전**. 모터 오동작 = 부상 위험. failsafe 경로가 최우선 |

정리하면: SSD에서 "프로토콜 적합성 + 오류 처리 + 마진(shmoo) + 장기 stress"를 했던 틀이 그대로 쓰인다. 바뀌는 건 (1) 규격 대신 레퍼런스 코드를 읽어 기대값을 만드는 것, (2) 손실이 정상인 세계에서 "얼마나 우아하게 망가지나"를 정량화하는 것, (3) RF 측정 체인을 믿을 수 있게 만드는 것(누설 · 보정)이다.

---

## 12. 테스트 아이디어 / 체크리스트

### 12.1 CRSF

- [ ] CRC-8 0xD5 골든 벡터 (EdgeTX `crc8` 테스트 벡터) 통과
- [ ] RC 프레임 길이: RX→FC 26 B, 조종기→모듈 27 B (status 바이트)
- [ ] 채널 경계값 0 / 172 / 992 / 1811 / 1984 왕복, 988 µs ↔ 172/173 반올림 정책 문서화
- [ ] len 0, 1, 63, 64, 0xFF 주입 → 즉시 버림, 다음 프레임 정상
- [ ] 가짜 sync (0xC8 · 0xEA · 0xEE · 0x00) + 그럴듯한 len 주입 → 구현별 기대 결과 표와 일치
- [ ] 1바이트 삭제 · 삽입 · bit flip fuzz (1만 회 이상) → 크래시 0, 잘못된 프레임 수용 0 (CRC-8 우연 통과 확률 고려)
- [ ] 청크 경계 무작위 (1~7 B) 에서 결과 동일
- [ ] baud ±3% sweep에서 CRC 에러율 기록
- [ ] 바이트 간격 조절 주입 (Betaflight 시간 기반 경계 확인)
- [ ] link stats 부호(RSSI ×-1, SNR int8) 디코드 확인

### 12.2 ELRS 링크

- [ ] rate별 LQ vs 수신 전력 곡선, knee가 코드 감도 ± 허용치 안
- [ ] 끊김 · 회복 히스테리시스 기록
- [ ] 링크 끊김 → RX CRSF 출력 정지 시간
- [ ] 끊김 → FC RXLOSS / failsafe stage → DShot 값 시퀀스가 설정별 기대표와 일치
- [ ] 회복 → 첫 RC 프레임까지 재연결 시간 분포 (수백 회)
- [ ] 다른 바인딩 문구 TX가 같은 공간에 있을 때 영향 없음 (CRC init 필터)
- [ ] 텔레메트리 비율 변경 시 uplink rate · LQ 영향
- [ ] Gemini / dual band: 한 경로 차단 시 LQ 유지
- [ ] 400k 조종기에서 F1000 선택 시 동작 (500 Hz 제한 규칙)
- [ ] `pio test -e native` CI 결과를 내 HIL 결과와 같은 대시보드에

### 12.3 SBUS

- [ ] 100000 8E2 inverted 파라미터 정확
- [ ] parity 오류 바이트 → 프레임 버림
- [ ] flags bit2 / bit3 각각 FC 반응
- [ ] ch17/18 디지털 매핑
- [ ] sigrok `sbus_futaba` 디코더로 캡처 자동 판정

### 12.4 DShot

- [ ] 2048 값 × T × normal/bidir 인코딩 기준 구현 일치
- [ ] 파형 T1H/T0H/bit 주기 허용 오차 안 (DShot300/600)
- [ ] 명령 6회 반복 · 대기 시간 규칙 준수 (방향 · 저장 · EDT)
- [ ] bidir: 응답 CRC 오류 · 무응답 처리, eRPM → RPM 변환 (극 수 설정)
- [ ] PID 주기 × DShot 속도 × bidir 조합 타이밍 예산 표 → 불가능 조합 경고 확인
- [ ] failsafe 시 DShot 값 = 0 (또는 설정된 land 스로틀)

### 12.5 MSP / MAVLink

- [ ] MSP v1 XOR / v2 CRC-8 체크섬, jumbo 프레임
- [ ] MSP 오류 입력 → FC 멈춤 없음, '!' 응답
- [ ] MSP 폴링 soak 중 루프 시간 영향
- [ ] MAVLink v2 CRC_EXTRA 불일치 시나리오
- [ ] 끝 0 잘림 메시지 수신
- [ ] 서명: 키 불일치 · replay · 1분 초과 → 거부
- [ ] seq 기반 손실률 계산이 link stats LQ와 상관 있는지

---

## 13. 직접 해 볼 것

1. **코드 돌리기 (지금 바로, 하드웨어 없음)**: `python3 code/crsf.py`, `python3 code/dshot.py` → 둘 다 PASS. 그 다음 `crsf.py`의 `StreamParser`를 ELRS 정책(CRC 실패 시 소비한 바이트 버림)으로 바꾼 `ElrsStyleParser`를 만들어, 같은 fuzz 입력에서 두 파서가 찾는 프레임 수 차이를 세어 보기. **"구현마다 기대값이 다르다"를 숫자로 확인**
2. **FHSS 재현**: `src/lib/FHSS/FHSS.cpp`의 `FHSSrandomiseFHSSsequenceBuild`와 `random.cpp` LCG를 Python으로 옮기고, 바인딩 문구 → MD5 → UID → seed → 240칸 시퀀스를 출력. 요구사항 4개(블록 첫 칸 sync, 블록 내 중복 없음, 균등, 의사난수)를 assert. ELRS `test_fhss.cpp`와 같은 성질을 Python으로 테스트하는 셈
3. **ELRS 단위 테스트 실행**: `cd ExpressLRS/src && PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" pio test -e native`. 실패 없으면 `test_crc`를 읽고 CRC14 검출률 측정 방식을 정리
4. **EdgeTX 파서 테스트 읽기**: `radio/src/tests/crossfire.cpp`의 `frameParser_droppedBytes` 3개 벡터를 `crsf.py`에 옮겨 같은 결과가 나오는지 확인 (주의: 이 벡터는 0xEA 시작 텔레메트리 프레임이라 `VALID_START`에 이미 포함)
5. **(하드웨어, 예산 확인 후)** ELRS RX + Betaflight FC + 로직 분석기: USB-UART로 CRSF 주입 → FC DShot 출력 캡처 → `dshot.py`로 디코드 → CRSF 값 대비 DShot 값 매핑 표. **프롭 없이, 모터 분리 또는 전류 제한 전원**
6. **(하드웨어, 예산 확인 후)** ELRS TX/RX 쌍 + 고정 감쇠기 + 스텝 감쇠기 + 차폐 박스: 6.2절 곡선 1개 (rate 1개). TX 출력은 최저로 시작, 감쇠기 먼저 연결
7. **MSP 클라이언트**: Betaflight SITL 또는 실제 FC에 `MSP_API_VERSION` → `MSP_STATUS` → `MSP_ATTITUDE` 폴링, v2 프레임은 `crsf.py`의 `crc8_d5` 재사용 ([K05](../../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md) 4번 실습과 같음)

---

## 14. 확인 안 된 것

- ELRS STM32 타깃이 master에서 빠진 시점 · 버전 [확인 필요]
- DVDA의 정식 이름 풀이와 공식 문서 설명 [확인 필요]
- ELRS LQ 분모에 텔레메트리 슬롯이 들어가는지 (`rx_main.cpp`의 `LQCalc.inc()` 위치 추가 확인) [확인 필요]
- CRSF v3 고속 baud 협상의 실제 절차와 F1000에서 권장 baud [확인 필요]
- ELRS 공식 range test 권장 절차 [확인 필요]
- SBUS 원래 프레임 간격(7 ms / 14 ms)과 끝 바이트 변형(0x04 등) [확인 필요]
- DShot ESC 쪽 타이밍 허용 오차, Betaflight가 DShot300 + bidir + 8 kHz 조합을 어떻게 처리하는지 [확인 필요]
- Saleae용 커뮤니티 DShot / CRSF 확장 존재 여부 [확인 필요]
- boofuzz 직렬(serial) 연결 사용법 [확인 필요]
- EdgeTX 로컬 테스트 빌드에 필요한 `PCB=` 옵션 [확인 필요]
- MAVLink HEARTBEAT CRC_EXTRA 값(생성 헤더 기준 50으로 기억) — 이 단원에서는 소스로 다시 확인하지 않았다 [확인 필요]
- Neros가 ELRS를 그대로 쓰는지, 포크인지, 다른 anti-jam 무전기 + CRSF 인터페이스인지 [추정 · 공고 외 정보 없음]
- Gemini가 말한 Neros 제품명(Archer, Archer AI, Crossbow GCS, APEX) [Gemini 주장 · 미확인] — 이 단원 주제와 직접 관계없어 쓰지 않았다

---

## 15. 참고 링크

소스 (shallow clone, 2026-10-24):

- ExpressLRS: https://github.com/ExpressLRS/ExpressLRS — `src/include/crsf_protocol.h`, `src/include/common.h`, `src/src/common.cpp`, `src/lib/OTA/OTA.h`, `src/lib/OTA/OTA.cpp`, `src/lib/FHSS/FHSS.cpp`, `src/lib/FHSS/random.cpp`, `src/lib/LQCALC/LQCALC.h`, `src/lib/CrsfProtocol/CRSFParser.cpp`, `src/lib/Handset/CRSFHandset.cpp`, `src/src/rx_main.cpp`, `src/src/tx_main.cpp`, `src/src/rx-serial/SerialCRSF.cpp`, `src/src/rx-serial/SerialSBUS.cpp`, `src/platformio.ini`, `src/test/`, `.github/workflows/build.yml`, `src/python/build_flags.py`
- EdgeTX: https://github.com/EdgeTX/edgetx — `radio/src/pulses/crossfire.cpp`, `radio/src/telemetry/crossfire.h`, `radio/src/tests/crossfire.cpp`, `radio/src/tests/CMakeLists.txt`, `tools/commit-tests.sh`, `CMakeLists.txt`
- Betaflight: https://github.com/betaflight/betaflight — `src/main/rx/crsf.c`, `src/main/rx/crsf_protocol.h`, `src/main/rx/sbus.c`, `src/main/rx/sbus_channels.c`, `src/main/rx/rx.c`, `src/main/flight/failsafe.c`, `src/main/drivers/dshot.c`, `src/main/drivers/dshot_command.h`, `src/main/drivers/dshot_bitbang_decode.c`, `src/main/msp/msp_serial.c`, `src/main/msp/msp_protocol.h`, `src/test/unit/rx_crsf_unittest.cc`

문서:

- TBS CRSF spec: https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md
- DShot / bidirectional DShot: https://brushlesswhoop.com/dshot-and-bidirectional-dshot/
- Extended DShot Telemetry: https://github.com/bird-sanctuary/extended-dshot-telemetry
- MAVLink serialization: https://mavlink.io/en/guide/serialization.html
- MAVLink message signing: https://mavlink.io/en/guide/message_signing.html
- sigrok protocol decoders: https://sigrok.org/wiki/Protocol_decoders
- ELRS PWM receivers (failsafe 설명): https://www.expresslrs.org/hardware/pwm-receivers/
- PlatformIO unit testing: https://docs.platformio.org/page/plus/unit-testing.html

이 노트 세트:

- [R00 개요 · Gemini 정정 목록](2026-10-24_R00_overview_and_gemini_corrections.md)
- [R01 FTE vs SSD 검증](2026-10-24_R01_fte_vs_ssd_validation.md)
- [R02 시스템 · 오픈소스 배치도](2026-10-24_R02_drone_system_and_open_source_map.md)
- [R03 Betaflight / INAV](2026-10-24_R03_betaflight_inav_internals_and_testing.md)
- [R04 PX4 / ArduPilot](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)
- [R06 Companion · 영상 · Linux · RTOS](2026-10-24_R06_companion_video_linux_rtos.md)
- [R07 테스트 방법론](2026-10-24_R07_test_methodology_pyramid.md)
- [R08 HIL 벤치](2026-10-24_R08_hil_bench_design_instruments.md)
- [R09 플랫폼 아키텍처](2026-10-24_R09_scalable_reusable_test_platform.md)
- [R10 로드맵 · 실습](2026-10-24_R10_roadmap_labs_references.md)
- 코드: [crsf.py](../code/crsf.py) · [dshot.py](../code/dshot.py)

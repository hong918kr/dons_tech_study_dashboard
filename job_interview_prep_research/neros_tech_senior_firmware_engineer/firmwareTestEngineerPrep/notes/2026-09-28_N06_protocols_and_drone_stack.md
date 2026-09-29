# 📡 N06 · 테스터 관점의 프로토콜과 드론 스택 — UART·I2C·SPI·Ethernet, Betaflight·ELRS·PX4, RF 테스트

> JD 필수: "Familiarity with embedded communication protocols — **I2C, SPI, UART, Ethernet**". 우대: "Experience **testing RF products**", "Experience with FPV drone software including **Betaflight, ExpressLRS, PX4, Ardupilot**". 
> 버스는 Don이 이미 잘 안다 (Apple PCIe/I2C/SPMI/RFFE 루트코즈, SK hynix I2C/SPI SoC verification) → 여기서는 **"Python으로 어떻게 테스트하고 어떻게 고장을 주입하나"**에 집중. 드론 스택은 45분 HM 대화에서 막히지 않을 정도로.

---

## 1. 버스별 한눈 비교 (테스터 관점)

| | UART | I2C | SPI | Ethernet/UDP |
|---|---|---|---|---|
| 선 | TX, RX (+GND) | SDA, SCL (open-drain, pull-up) | SCLK, MOSI, MISO, CS | 차동쌍, PHY |
| 클럭 | 없음 (양쪽 baud 약속) | master가 SCL, slave가 늘릴 수 있음 | master가 SCLK | PHY가 처리 |
| 주소 | 없음 (1:1) | 7bit 주소 | CS 선으로 선택 | IP:port |
| 드론 예시 | GPS, RC 수신기(CRSF), ESC telemetry, 디버그 콘솔 | 기압계, 자력계, 일부 센서 | IMU(gyro/accel), OSD, flash | 컴패니언 컴퓨터, GCS, 비디오 |
| Python | `pyserial` | `smbus2` (Linux) | `spidev` (Linux) | `socket` |
| 대표 고장 | baud 불일치, 버퍼 오버런, 프레임 깨짐 | NACK, bus hang, 주소 충돌 | 모드 불일치, CS 타이밍, 속도 과다 | 패킷 손실, 지연, 순서 뒤바뀜 |

---

## 2. UART

### 2.1 원리 복습

- 프레임: start bit(0) + 데이터 8bit(LSB 먼저) + (parity) + stop bit(1). **8N1** = 1바이트에 10bit → 115200 baud면 초당 약 11520바이트
- 클럭이 없어서 **양쪽 baud가 대략 ±2~3% 안에 맞아야** 한다 (일반적인 경험칙)
- CRSF는 **420000 baud, 8N1, 반전 없음** (ExpressLRS 문서) → 초당 약 42000바이트

### 2.2 baud 불일치 증상 (디버깅 질문 단골)

- 쓰레기 문자, 특히 `0xFF`/`0x00`이 섞임, **framing error** 카운트 증가
- 2배·절반 관계면 규칙적인 패턴의 쓰레기
- 짧은 메시지는 가끔 되고 긴 메시지는 깨짐 → 클럭 오차 누적 (baud가 약간만 어긋난 경우)
- 확인법: 스코프/logic analyzer로 **1bit 폭을 재서 실제 baud 계산** (1/115200 ≈ 8.68µs)

### 2.3 pyserial로 테스트하기

```python
import serial, time

with serial.Serial("/dev/hil/fc", 115200, timeout=0.1) as port:   # timeout 필수
    port.reset_input_buffer()                  # 이전 쓰레기 비우기
    port.write(b"version\r\n")
    deadline = time.monotonic() + 2.0
    buf = b""
    while time.monotonic() < deadline:
        buf += port.read(port.in_waiting or 1)  # 있는 만큼 읽기, 없으면 timeout까지 1바이트 대기
        if b"# " in buf:                        # 프롬프트 보이면 끝
            break
    else:
        raise TimeoutError(f"no prompt, got {buf!r}")
```

- `timeout=None`이면 **영원히 블록** → HIL에서 금지
- `read(n)`은 n바이트 **또는** timeout까지. `readline()`은 `\n`까지 또는 timeout
- 응답이 여러 조각으로 온다는 걸 항상 가정 (부분 읽기) → 누적 버퍼 + 구분자 탐색
- 코딩 문제 04(시리얼 DUT 드라이버)에서 직접 연습

### 2.4 고장 주입 (UART)

- **baud 살짝 틀리게** (예: 420000 대신 400000, 440000) → FC가 링크 유지하는지, 에러 카운트 올리는지
- **바이트 손상**: 프레임 중간 비트 플립 → CRC가 걸러내는지
- **바이트 드롭 / 잘린 프레임**: 파서가 **재동기화**하는지 (다음 sync byte에서 복구)
- **쓰레기 폭주**: 랜덤 바이트를 최대 속도로 → FC가 죽거나 루프 타이밍이 깨지지 않는지 (robustness, 퍼징)
- **신호 끊김**: 송신 중단 → failsafe
- 방법: USB-UART 어댑터를 Python으로 직접 제어, 또는 중간에 **MCU 프록시**를 끼워 바이트 변조

---

## 3. I2C

### 3.1 원리 복습

- START → **7bit 주소 + R/W bit** → 매 바이트 뒤 수신측이 **ACK(0)/NACK(1)** → STOP
- 레지스터 읽기 전형: `START, addr+W, reg, RESTART, addr+R, data..., NACK, STOP`
- open-drain + pull-up → 누구든 선을 0으로 당길 수 있다
- **clock stretching**: slave가 준비 안 됐으면 SCL을 0으로 붙잡아 master를 기다리게 함. 이걸 지원 안 하는 master와 만나면 데이터 깨짐

### 3.2 bus hang과 복구

- 증상: SDA가 **계속 0**에 붙어 있음. master가 START를 못 만듦
- 원인: 트랜잭션 도중 master가 리셋됨 → slave는 아직 데이터 비트를 내보내는 중(SDA를 0으로 잡고 있음)
- 복구: master가 **SCL을 최대 9번 토글**해서 slave가 바이트를 끝내게 한 뒤 **STOP** 생성 (표준적인 bus recovery 절차)
- 테스트 포인트: 부팅 시 bus recovery가 있는지, 센서 하나가 hang을 걸면 FC가 어떻게 되는지 (센서 fail 처리)

### 3.3 Python (Linux, smbus2)

```python
from smbus2 import SMBus

BARO_ADDR = 0x76
with SMBus(1) as bus:                              # /dev/i2c-1
    who = bus.read_byte_data(BARO_ADDR, 0xD0)      # chip-id 레지스터 (BMP280 예: 0xD0 → 0x58)
    assert who == 0x58, f"unexpected chip id 0x{who:02x}"
    raw = bus.read_i2c_block_data(BARO_ADDR, 0xF7, 6)   # 여러 바이트 연속 읽기
```

- `i2cdetect -y 1`: 버스에 응답하는 주소 스캔 (NACK 안 나는 주소)
- 테스트 장비 쪽에서는 FC의 I2C 트래픽을 **logic analyzer로 디코드**해 검증하는 경우가 더 많다 (FC가 master이므로)
- 코딩 문제 05(레지스터 비트필드)에서 read-modify-write 연습

### 3.4 고장 주입 (I2C)

- 센서 **제거/무응답** → NACK → FC가 센서 fail 플래그 올리는지, arm을 막는지
- **SDA를 0으로 강제**(트랜지스터로 당김) → bus hang 복구 확인
- 느린 slave 흉내(clock stretch 길게) → 타임아웃 처리
- 잘못된 chip-id 반환 (대체 부품 흉내) → 드라이버가 인식·거부하는지

---

## 4. SPI

### 4.1 원리 복습

- master가 SCLK, **CS를 low로** 해서 slave 선택, MOSI/MISO로 **동시에** 주고받음 (full duplex)
- **모드 = (CPOL, CPHA)**

| mode | CPOL (idle 클럭) | CPHA (샘플링 엣지) |
|---|---|---|
| 0 | 0 (low) | 0 (첫 엣지 = 상승) |
| 1 | 0 | 1 (둘째 엣지 = 하강) |
| 2 | 1 (high) | 0 (첫 엣지 = 하강) |
| 3 | 1 | 1 (둘째 엣지 = 상승) |

- 많은 IMU는 mode 0과 3을 지원 [데이터시트마다 확인]
- 모드가 틀리면: 데이터가 **1bit 밀린** 것처럼 보임 (예: WHO_AM_I가 0x47 대신 0x23 또는 0x8E 식)
- IMU 레지스터 읽기 관례: 주소 바이트의 **MSB=1이면 read** (많은 InvenSense/Bosch 칩)
- 빠른 데이터 읽기는 DMA + FIFO watermark 인터럽트 → 드라이버 버그가 많은 곳 = HIL 대상

### 4.2 Python (Linux, spidev)

```python
import spidev
spi = spidev.SpiDev()
spi.open(0, 0)                 # bus 0, CS 0 → /dev/spidev0.0
spi.max_speed_hz = 1_000_000
spi.mode = 0b00                # CPOL=0, CPHA=0
resp = spi.xfer2([0x80 | 0x75, 0x00])   # read WHO_AM_I(0x75) 예시 — MSB=1이 read
print(hex(resp[1]))
spi.close()
```

### 4.3 고장 주입 (SPI)

- 클럭 속도를 스펙 이상으로 → 어디서 깨지나 (마진 스윕 = Don의 shmoo 경험과 같은 사고)
- CS를 트랜잭션 중간에 해제
- MISO를 고정값(0x00/0xFF)으로 → "센서 죽음" 감지하는지 (0xFF 연속은 흔한 "칩 없음" 신호)

---

## 5. Ethernet / UDP

- 드론에서: **컴패니언 컴퓨터(Linux) ↔ FC**, GCS ↔ 라디오 모뎀, 비디오 스트림. UART 대신 Ethernet을 쓰는 FC도 있음 [추정: 제품마다 다름]
- telemetry는 보통 **UDP**: 지연이 작고, 오래된 telemetry는 재전송해 봐야 쓸모없음
- **MAVLink over UDP**: GCS(QGroundControl, Mission Planner)가 관례적으로 **UDP 14550**에서 수신. PX4 SITL은 GCS용 14550, offboard API용 14540을 쓴다 (PX4 문서 기준 [추정: 버전별 차이 가능])

```python
import socket, time
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 14550))
sock.settimeout(1.0)
stamps = []
t_end = time.monotonic() + 5
while time.monotonic() < t_end:
    try:
        data, addr = sock.recvfrom(2048)
    except socket.timeout:
        continue
    if data[:1] == b"\xfd":            # MAVLink v2 start byte (v1은 0xFE)
        stamps.append(time.monotonic())
print(f"{len(stamps)/5:.1f} msgs/s")
```

- 고장 주입: Linux `tc qdisc ... netem`으로 **패킷 손실·지연·지터·순서 뒤바뀜** 주입 (예: `tc qdisc add dev eth0 root netem loss 10% delay 50ms 20ms`)
- 검증: 시퀀스 번호로 손실률 계산, 명령 → ACK 지연 분포(p50/p99), 링크 끊김 → GCS/FC 양쪽 failsafe

---

## 6. 드론 스택 입문

### 6.1 Betaflight (FPV 레이싱·프리스타일 FC 펌웨어)

- **오픈소스 FC 펌웨어**, 주로 STM32 F4/F7/H7 MCU. Neros Archer의 flight stack이 Betaflight 계열이라는 정황이 있음 (형제 공고 키워드) [추정]
- **루프**: gyro를 읽고 PID를 계산해 모터 출력까지 → 보통 **수 kHz** (gyro 8kHz 계열, BMI270은 3.2kHz 등 센서에 따라 다름) [추정: 버전·보드별]. 이 루프 타이밍이 흔들리면(jitter) 비행 품질이 나빠짐 → **CPU load·loop time**은 HIL에서 측정할 대표 지표
- **scheduler**: 우선순위가 다른 task들(gyro/PID, RX, OSD, telemetry, blackbox)을 협조적으로 돌림
- **MSP (MultiWii Serial Protocol)**: Configurator·OSD·다른 장비가 FC와 대화하는 바이너리 프로토콜. v1 헤더 `$M<`(요청) / `$M>`(응답), 길이·명령·payload·XOR 체크섬. 코딩 문제 03에서 직접 구현
- **CLI**: USB VCP(가상 COM)로 접속해 `#` 입력하면 텍스트 CLI. `status`, `version`, `get`, `set`, `save`, `diff` → **텍스트 기반이라 Python 자동화에 쉬움**
- **Blackbox**: 비행 중 gyro·PID·모터 출력 등을 **onboard flash나 SD에 고속 기록** → 비행 후 분석. HIL에서도 blackbox를 뽑아 모터 출력·루프 타이밍을 검증 가능
- **SITL 타깃**: PC에서 Betaflight를 프로세스로 실행. Gazebo와 UDP(127.0.0.1:9002/9003)로 연결, **UART1이 TCP 5761**로 열려 Configurator/MSP 접속 가능 (Betaflight SITL 문서) → **리그 없이 MSP 자동화 테스트 개발 가능**
- 테스트할 것: arm 조건(가속도계 보정, throttle low, RX 유효), failsafe 단계(stage 1/2), 모터 믹서, 설정 저장/복원(`diff`로 비교), 펌웨어 업데이트 후 설정 유지

### 6.2 ExpressLRS (ELRS) — RC 링크

- **오픈소스 RC 링크 시스템**. 조종기(TX 모듈)와 기체 수신기(RX)가 RF로 통신하고, RX는 FC에 **CRSF 프로토콜(UART)**로 채널 값을 넘김
- 주파수: **2.4GHz**, **900MHz**(sub-GHz, 장거리)
- **packet rate**: 2.4GHz LoRa 모드 50/150/250/500Hz, FLRC 모드 **F500/F1000**(최저 지연, 대신 거리 짧음). F1000은 400K보다 높은 baud 필요 (ELRS 문서)
- 트레이드오프: **packet rate ↑ → 지연 ↓, 거리(감도) ↓**. 장거리 공격 드론이면 낮은 rate + 900MHz 쪽이 유리할 수 있음 [추정]
- **telemetry ratio**: Off, 1:128 … 1:2 — 패킷 몇 개 중 하나를 downlink(기체→조종기)에 쓰나. telemetry ↑ = 제어 업링크 몫 ↓
- 링크 지표: **RSSI**(수신 세기, dBm), **LQ**(Link Quality, 최근 패킷 성공률 %), **SNR**
- CRSF 프레임: `[0xC8 sync][len][type][payload][CRC8]`, CRC8은 poly 0xD5 (type+payload 대상). RC 채널 프레임(type 0x16)은 16채널 × 11bit = 22바이트 payload. 코딩 문제 02에서 파서 구현
- Neros는 **C2·비디오 라디오를 in-house**로 만든다 → 실제 링크는 ELRS가 아닐 수 있음. 공고에 ELRS가 우대로 있으니 적어도 일부 제품·테스트에 쓰는 것으로 보임 [추정]
- 테스트할 것: 링크 손실 → failsafe 시간, LQ 저하 구간 동작, packet rate 전환, telemetry 레이트, 바인딩, 펌웨어 버전 호환

### 6.3 PX4 · ArduPilot — 자율비행 스택

- **PX4**: 오픈소스 autopilot. NuttX RTOS 위에서 동작, 내부 통신은 **uORB**(publish/subscribe 메시지 버스), 외부는 **MAVLink**. 미션·position hold·offboard 제어
- **ArduPilot**: 역시 오픈소스 autopilot (ArduCopter/Plane/Rover), MAVLink, 강력한 SITL
- **MAVLink**: 드론 ↔ GCS/컴패니언 메시지 프로토콜. v2 start byte `0xFD`, 메시지 ID(예: HEARTBEAT = 0, 보통 1Hz), 시스템/컴포넌트 ID, CRC, 선택적 서명
- Python 도구: **pymavlink**, **MAVSDK-Python** → 테스트 자동화에 흔히 사용
- Betaflight와 차이: Betaflight = 사람이 직접 조종하는 **acro 중심**, GPS 자율 기능은 제한적. PX4/ArduPilot = **자율 비행·미션 중심**. Archer AI의 terminal guidance·GPS-denied position hold는 autonomy 쪽 [추정]
- 테스트할 것: SITL에서 미션 시나리오(이륙→웨이포인트→착륙), GPS 끊김 시 모드 전환, geofence, MAVLink 명령 ACK

---

## 7. RF 제품 테스트 기초 (JD 우대 — Don의 Apple RF 경험과 연결)

### 7.1 개념

- **Link budget** (dB 덧셈): 수신 전력 = 송신 전력 + 송신 안테나 이득 + 수신 안테나 이득 − 경로 손실 − 기타 손실. 이것이 **수신 감도**보다 얼마나 큰가 = **link margin**
- 자유공간 경로 손실(FSPL, dB) ≈ 20·log10(d[km]) + 20·log10(f[MHz]) + 32.44 → 거리 2배 = 약 6dB 손실. 같은 거리에서 2.4GHz가 900MHz보다 약 8.5dB 더 손실 (20·log10(2400/900))
- 감도는 packet rate(=대역폭·변조)에 따라 달라짐 → 낮은 rate가 더 멀리 감

### 7.2 conducted 테스트 — 감쇠기로 "거리"를 만든다

```text
 ┌─────────┐   SMA 케이블   ┌──────────────────────┐   케이블   ┌──────────┐
 │ TX 모듈 │──────────────▶│ programmable         │──────────▶│ RX (+FC) │
 │(or 조종기)│              │ attenuator (0~95dB,  │           │ 차폐 박스 │
 └─────────┘               │ USB/LAN으로 제어)     │           └──────────┘
                           └──────────────────────┘
                 Python: 감쇠 스윕 → RSSI/LQ 기록 → failsafe 경계 측정
```

- 안테나 대신 **케이블로 직결**(conducted) + **차폐 박스(shielded/RF enclosure)** → 주변 Wi-Fi 등 간섭 없이 **재현 가능한** 측정
- **감쇠 스윕**: 감쇠를 1dB씩 늘리며 RSSI·LQ·패킷 손실을 기록 → LQ가 떨어지기 시작하는 지점, failsafe 지점을 찾는다 = **감도 곡선**. 펌웨어 버전 간 곡선을 비교하면 **RF 성능 회귀**를 잡는다
- 급격한 감쇠 변화로 **링크 끊김 → 재연결 시간** 측정
- 간섭 주입: 신호 발생기로 인접 채널 노이즈 → 재밍 저항 (Neros가 강조하는 EW 환경) [추정: 사내 방법은 모름]
- radiated 테스트(안테나 포함, 챔버·필드)는 별도 단계

### 7.3 Don 경험 연결 (레쥬메 범위)

- Apple **RF-Hardware (Wireless) Chipset Integration**: 새 RF 칩이 전체 시스템을 만날 때 생기는 **PCIe/I2C/SPMI/RFFE** 인터페이스 실패 루트코즈, DSO·protocol analyzer로 신호·버스·시스템 레벨 디버그, **factory test-node 아키텍처**, 신뢰성 vs 성능/전력 **마진 sign-off**
- 드론 RF 테스트로의 번역: "RF 프런트엔드 제어 버스(RFFE/SPMI)부터 시스템 레벨까지 봤다 → 링크 성능이 떨어질 때 **RF 문제인지, 펌웨어·타이밍 문제인지** 가를 수 있다"
- 링크 버짓·감쇠 스윕 수치 경험이 실제로 있으면 **(Don: 실제 사례 채우기)**, 없으면 "integration 쪽이었고 RF 성능 측정 자체는 RF 팀과 협업했다"로 정직하게

---

## 8. 면접 예상 질문 + 영어 모범 답변

### Q1. "How would you test the UART link between the receiver and the flight controller?"

> Functionally, I'd inject CRSF frames from a USB-UART adapter at 420 kbaud and check the flight controller reports the same channel values — sweep every channel through its range and check endpoints. Then robustness: slightly wrong baud rates, bit flips so the CRC has to reject frames, truncated frames to make sure the parser resyncs on the next sync byte, a flood of random bytes to check the loop timing doesn't suffer, and finally stopping the stream to measure the failsafe time against the spec. Every case logs the raw bytes sent, so a failure is reproducible.

### Q2. "The IMU WHO_AM_I read returns the wrong value on some boards."

> I'd put a logic analyzer on the SPI lines first. If the value looks like the right one shifted by a bit, that's an SPI mode or sampling-edge mismatch. If it's 0xFF or 0x00, the chip isn't responding — chip select, power or a soldering issue. If only some boards fail, I'd check whether they have a second-source part with a different ID, and whether the SPI clock is at the edge of the margin on those boards — sweeping the clock speed tells you quickly.

### Q3. "An I2C sensor stops responding after a flight controller reset."

> Classic bus hang: the controller reset mid-transaction while the sensor was driving SDA low. The fix is a bus-recovery sequence at boot — clock SCL up to nine times until SDA is released, then a STOP. To test it, I'd deliberately reset the flight controller during I2C traffic in a loop on the rig and assert the sensor always comes back.

### Q4. "What do you know about Betaflight / ExpressLRS?"

> Betaflight is the open-source flight controller firmware most FPV drones run — a fast gyro-and-PID loop in the kilohertz range, MSP for configuration and OSD, a text CLI over USB, and a blackbox logger. It also has a SITL target that exposes the UARTs over TCP, which is great for developing test automation before touching a rig. ExpressLRS is the open-source RC link; the receiver talks CRSF to the flight controller over UART, and you trade packet rate against range and latency, with a configurable telemetry ratio for downlink. I haven't flown or developed on them myself, but I've studied the protocols and implemented CRSF and MSP parsers in Python while preparing.

### Q5. "How would you test an RF product like our radio?"

> Start conducted, in a shielded box, with a programmable attenuator between transmitter and receiver, so every measurement is repeatable. Sweep attenuation and record RSSI, link quality and packet loss to get the sensitivity curve, and compare that curve across firmware versions to catch RF regressions. Then measure link-loss and reconnect times, and add interference to test robustness. Radiated and field testing come after. From my RF integration work at Apple, I'd also watch the control side — the front-end control buses and timing — because a link that degrades isn't always an RF problem.

### Q6. "How would you verify telemetry over UDP from the companion computer?"

> Listen on the port, count messages per second against the expected rate, use sequence numbers to compute loss, and check field values are in range. Then use netem on the Linux side to inject loss, delay and reordering, and verify both ends degrade gracefully and trigger their failsafes when the link is gone.

---

## 9. 체크리스트

- [ ] 버스 비교표(§1)를 보고 각 버스의 대표 고장 1개씩 영어로 말하기
- [ ] UART baud 불일치 증상 3가지 + 1bit 폭으로 baud 계산하는 법
- [ ] I2C bus hang 원인과 9-clock 복구를 30초로 설명 (Q3)
- [ ] SPI mode 0~3 표 외우기, 모드 틀리면 "1bit 밀림" 기억
- [ ] CRSF 프레임 구조 `[C8][len][type][payload][crc8 0xD5]`와 420000 baud 기억
- [ ] Betaflight SITL(TCP 5761), MSP `$M<`/`$M>`, CLI `#` 기억
- [ ] ELRS packet rate ↔ 거리·지연, telemetry ratio 의미를 한 문장으로
- [ ] 감쇠기 스윕 그림(§7.2) 그리며 Q5 답변 연습, Apple RF 경험 부분 실제 사례로 채우기

---

## 출처 (확인일 2026-09-29)

- ExpressLRS 공식 문서 — packet rate(50/150/250/500Hz LoRa, F500/F1000 FLRC), telemetry ratio 옵션: https://www.expresslrs.org/quick-start/transmitters/lua-howto/ · https://www.expresslrs.org/info/telem-bandwidth/
- CRSF 420000 baud, 8N1, 비반전: https://gist.github.com/GOROman/9c7eadf78eb522bbb801beb9162a8db5
- Betaflight SITL — Gazebo UDP 9002/9003, UART1 = TCP 5761: https://betaflight.com/docs/development/SITL

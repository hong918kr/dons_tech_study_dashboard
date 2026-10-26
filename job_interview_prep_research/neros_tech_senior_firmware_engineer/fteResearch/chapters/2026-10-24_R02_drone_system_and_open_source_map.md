# 🛰️ R02 드론 시스템 구조 · 보드 · 버스 · 오픈소스 배치도 · 신호 경로
> FPV/전술 드론을 보드 단위로 쪼개고, 보드 사이 버스, 각 보드에서 도는 오픈소스와 라이선스, 그리고 stick에서 모터까지 신호가 지나가는 길을 hop별 시간 예산과 고장 모드로 정리한다. 테스트 벤치에서 "어디에 probe를 꽂을지"를 정하는 지도.

---

## 0. 이 단원 한 줄 요약

- 드론은 **보드 여러 장이 버스로 이어진 분산 시스템**이다. FC 하나만 보는 게 아니라, 조종기 · TX 모듈 · RX · FC · ESC · VTX · GPS · companion · GCS가 각자 펌웨어를 갖고 있다.
- 각 보드에는 **다른 오픈소스**가 올라간다. Betaflight(FC), ExpressLRS(TX/RX), EdgeTX(조종기), Bluejay/AM32(ESC), OpenHD/WFB-ng(영상), PX4/ArduPilot(자율 FC), QGC/Mission Planner/ATAK(지상).
- 신호 경로는 hop마다 **프로토콜 · 주기 · 지연 원인**이 다르다. 계산 가능한 부분(프레임 시간, 루프 주기)과 측정해야만 아는 부분(필터 지연, 기계 응답, 영상 파이프라인)을 구분해야 한다.
- FTE 관점의 결론: **경계(interface)마다 probe point를 만들고, 같은 timebase로 동시에 잡는다.** 그게 HIL 벤치 설계의 출발점이다 ([R08](2026-10-24_R08_hil_bench_design_instruments.md)).

---

## 1. 왜 중요한가 (FTE 관점)

FTE는 "FC 펌웨어 하나"를 테스트하는 사람이 아니다. Neros Firmware Test Engineer 공고는 업무를 "drone and ground control software" 테스트 스위트라고 쓴다 [확인됨 공고]. 즉 **공중 쪽과 지상 쪽 둘 다**다.

온사이트에서 나온 질문 세 개도 전부 이 단원의 지도 위에 있다 (K05 기준).

| 온사이트 질문 | 이 단원의 어디 | 왜 지도가 필요한가 |
|---|---|---|
| Michael: Betaflight 최우선 task | 3.4 FC 보드, 6.1 control uplink의 FC hop | 스케줄러가 시간 예산의 한 hop이다 |
| Michael: ground → TX → RX → FC latency | 6.1 control uplink 전체 | hop별로 쪼개야 측정할 수 있다 |
| Jon: 패킷 문제 | 4장 버스 표, 7장 고장 모드 | 어느 버스에서 어떤 프레임이 깨지는지 알아야 한다 |
| Adam: 보드 테스트 | 3장 보드 해부, 5장 전원 레일 | 보드별 레일 · 버스 · 기대값을 알아야 bring-up 체크리스트를 짠다 |

SSD 검증에서도 "host → PCIe → controller → FW → NAND"처럼 경로를 쪼개서 봤다. 드론은 그 경로에 **RF 구간과 사람(조종사), 기계(모터 · 프롭)** 가 들어간다. 그 차이를 이 단원에서 눈에 보이게 만든다. SSD와의 일반 비교는 [R01](2026-10-24_R01_fte_vs_ssd_validation.md)에 있다.

---

## 2. 전체 그림 — 공중(Air) / 지상(Ground) 구획

### 2.1 한 장 요약 그림

```text
 ========================== GROUND SEGMENT ==========================
  [Pilot hands]                         [Operator / GCS]
       |                                       |
  +----v-----------------+          +----------v------------------+
  | Radio handset        |          | GCS laptop / tablet          |
  | MCU: STM32 (F4/H7)   |          | QGC / Mission Planner / ATAK |
  | FW : EdgeTX          |          | (MAVLink, CoT, video decode) |
  +----+-----------------+          +----------+------------------+
       | CRSF over UART (internal / JR bay)    | USB / Ethernet / Wi-Fi
  +----v-----------------+          +----------v------------------+
  | TX module (ELRS)     |          | Telemetry radio / video RX  |
  | ESP32 + SX1280/LR1121|          | (MAVLink radio, WFB-ng RX,  |
  +----+-----------------+          |  goggles, HDZero/DJI VRX)   |
       |                            +----------+------------------+
  ~~~~~|~~~~~~~~~~~ RF: 2.4 GHz / sub-GHz C2,  5.8 GHz video ~~~~~|~~~~
       |                                       |
 ========================== AIR SEGMENT ============================
  +----v-----------------+          +----------v------------------+
  | RX (ELRS)            |          | VTX (analog / HDZero / DJI / |
  | ESP8285 + SX1280 등  |          |  Walksnail / OpenHD)         |
  +----+-----------------+          +----------^------------------+
       | CRSF UART 420000                      | CVBS / MIPI / HDMI
  +----v-----------------------------------+   |  + SmartAudio/Tramp/MSP-DP
  | Flight Controller (FC)                 |---+
  | MCU: STM32F4/F7/H7, AT32F435 ...       |<--- camera (analog / MIPI)
  | FW : Betaflight / INAV / PX4 / ArduPilot|
  | IMU(SPI) Baro(I2C/SPI) Flash(SPI) OSD  |<--- GPS + mag (UART + I2C)
  +----+-------------------------+---------+
       | DShot x4 (timer+DMA)    | MAVLink / MSP (UART or Ethernet)
  +----v-------------+     +-----v------------------------------+
  | 4-in-1 ESC        |     | Companion computer (Jetson 등)    |
  | MCU: EFM8/STM32/  |     | Linux + ROS 2 / GStreamer / TensorRT|
  |      AT32/GD32    |     +-----^------------------------------+
  | FW: Bluejay/AM32  |           | MIPI CSI / USB / Ethernet camera
  +----+-------------+     +-----+------+
       | 3-phase power     | Payload    |
  [4 x BLDC motor]         +------------+
       ^
  [LiPo pack] -> PDB / ESC power -> BEC 5V / 9-10V / 3.3V rails
```

그림의 보드 이름 · MCU는 "흔한 구성"이다. 기체마다 다르다. Neros 기체가 어떤 조합인지는 공고 문구 외에는 모른다 [추정].

> ⚠️ Gemini 정정: Gemini 그림은 지상국을 "Crossbow GCS (Embedded Linux + Zynq)", 기체를 "Archer"로 단정했다. 공고에서 확인되는 건 Senior Firmware Engineer 공고의 "Neros **Archer** platform including flight control and radio link code" 한 줄뿐이다. **Crossbow · APEX는 109개 공고 어디에도 없다** → [Gemini 주장 · 미확인]. Zynq는 Firmware Engineer – Tennessee 공고의 "Significant FPGA experience"와 엮은 [추정]일 뿐이다.

### 2.2 Neros 공고가 직접 언급하는 부품 · 시스템

공고 109개를 grep 해서 하드웨어 단어가 나오는 문장만 뽑았다 (`research/job_board_snapshots/2026-10-23_neros_jobs.json`).

| 공고 | 문구 (원문 발췌) | 이 단원과의 연결 |
|---|---|---|
| UAV Systems Engineer | "Installing and connecting electronic components: FC, ESC, GPS, VTX, RX, antennas, PDB" | 3장 보드 해부 목록 그 자체 |
| UAV Systems Engineer | "ELRS, CRSF, SBUS", "Betaflight / INAV / ArduPilot" | 4장 버스, 8장 오픈소스 |
| UAV Systems Engineer | "Analog 5.8, DJI, Walksnail, HDZero", "VTX, antennas, RSSI/LQ" | 3.8 영상 |
| UAV Systems Engineer | "PDB, LiPo/Li-ion, BEC/step-down", "STM32 (F4/F7/H7), UART/SPI/I2C/CAN" | 5장 전원, 3.4 FC |
| UAV Systems Engineer | "Linux, Raspberry Pi, GStreamer, OpenHD" | 3.8 디지털 영상 |
| Senior Firmware Engineer | "Neros Archer platform including flight control and radio link code", "ARM Cortex-M processors - e.g. STM32" | FC + radio 펌웨어가 사내 개발 대상 |
| CODEC Video Compression Engineer | "build low cost FPV digital video, similar to DJI O4" | 디지털 영상 자체 개발 [확인됨 공고] |
| Lead RF Test & Integration Engineer | "digital video links, analog video links, LoRa command-and-control links, telemetry links, or mesh/point-to-point radio" | RF 링크 종류 목록 |
| Principal Connectivity Engineer | "video, command-and-control, telemetry, repeater, mesh, point-to-point, and ground-to-air connectivity" | 중계 · 메시까지 확장 |
| Autonomy Platform & Runtime Lead · Perception Lead | "NVIDIA Jetson and TensorRT, Qualcomm ..., Hailo" | 3.10 companion |
| Firmware Engineer – Tennessee | "STM32 family", "Significant FPGA experience" | FPGA가 들어간 보드가 있다 [추정] |
| FPV Technician · FPV Test Pilot | "Betaflight and ESC software" | ESC 펌웨어도 관리 대상 |
| Drone Support Specialist | "flight controllers, ESCs, motors, firmware, radio control systems, video transmission, batteries, and ground control" | 시스템 전체 = 지원 범위 |

Firmware Test Engineer 공고 자체는 부품 이름 대신 "I2C, SPI, UART, Ethernet"과 "Betaflight, ExpressLRS, PX4, Ardupilot"을 쓴다 [확인됨 공고]. 즉 **버스 + 오픈소스 스택**이 FTE의 언어다.

---

## 3. 보드별 해부 (Physical anatomy)

### 3.1 프레임 · 프롭 · 크기

- 프레임은 탄소섬유(carbon fiber) 판 + 암(arm)이 흔하다. 크기는 프롭 지름(인치)으로 부른다. FPV Technician 공고는 "larger FPV drones (10", 13", and up)"를 쓴다 [확인됨 공고].
- FTE에게 프레임이 중요한 이유: **진동**. 프레임 공진과 프롭 불균형이 gyro 노이즈가 되고, 그게 필터 · PID 동작을 바꾼다. 그래서 같은 펌웨어도 기체마다 Blackbox 스펙트럼이 다르다.
- 벤치에서는 프롭을 떼고 테스트한다. 그러면 진동 · 부하 조건이 실제 비행과 다르다는 걸 항상 기억해야 한다 (테스트 커버리지의 빈칸).

### 3.2 모터 (BLDC, KV)

- FPV 모터는 **outrunner BLDC**(3상, 센서리스)다. 홀 센서 없이 back-EMF zero-crossing으로 위치를 추정한다.
- **KV** = 무부하에서 1 V당 RPM. 같은 크기에서 KV가 높으면 빠르고 전류를 많이 먹는다. 큰 프롭 · 높은 셀 수(S)에는 낮은 KV를 쓴다.
- 극수(pole count): 흔한 구성은 12N14P (14극 = 7 pole pair) [확인 필요: 기체별]. ESC가 보내는 eRPM(electrical RPM)을 기계 RPM으로 바꾸려면 **RPM = eRPM ÷ (극수 ÷ 2)**.
- FTE 포인트: Betaflight의 **RPM filter**는 bidirectional DShot으로 받은 eRPM을 쓴다. 그래서 `motor_poles` 설정이 틀리면 노치 필터가 엉뚱한 주파수를 잡는다 — 설정 검증 테스트 항목이 된다.

### 3.3 ESC (Electronic Speed Controller)

FC가 보낸 스로틀 명령을 받아 3상 MOSFET 브리지를 스위칭한다. FPV는 보통 **4-in-1 ESC** (한 보드에 ESC 4개)를 FC 아래에 쌓는다 (stack).

| 항목 | 내용 | 확인 상태 |
|---|---|---|
| 입력 프로토콜 | DShot150/300/600 (디지털), 예전엔 PWM · OneShot · MultiShot | Betaflight `motor_types.h`에 ONESHOT125/42 · MULTISHOT · DSHOT150/300/600 · PROSHOT1000 존재, **DSHOT1200은 removed 주석** [확인됨 소스] |
| 출력 | 3상 PWM으로 MOSFET 구동, 센서리스 6-step commutation | 일반 지식 |
| 텔레메트리 | bidirectional DShot (eRPM, 확장 텔레메트리 EDT), 또는 별도 선의 ESC serial telemetry | 4장 |
| 8-bit ESC MCU | SiLabs EFM8 BB21 / BB51 (Busy Bee) | Bluejay README [확인됨] |
| 32-bit ESC MCU | STSPIN32F0, STM32F051, STM32G071, GD32E230, AT32F415, AT32F421, MCXA133 | AM32 README [확인됨] |

ESC 펌웨어 3종 정리:

| 펌웨어 | 대상 MCU | 라이선스 | 상태 |
|---|---|---|---|
| BLHeli_S | 8-bit SiLabs | 원래 BLHeli 계열 | 구형, Bluejay가 후속 |
| **Bluejay** | EFM8 BB21/BB51 (BB10은 0.18.1에서 지원 종료) | GPL-3.0 [확인됨 GitHub] | BLHeli_S 16.7 기반 오픈소스 후속, bidirectional DShot 지원 |
| BLHeli_32 | 32-bit ARM | **closed source** | 2024년 중반 개발사(BLHeli AS)가 서비스 종료. 기존 ESC는 동작하지만 펌웨어 갱신 서버가 닫힘 (Hackaday, Oscar Liang 보도) |
| **AM32** | 32-bit (위 표) | GPL-3.0 [확인됨 GitHub] | BLHeli_32의 오픈소스 대안으로 자리 잡음 |

> ⚠️ Gemini 정정: Gemini는 ESC를 "MCU: STM32G0 / $1짜리 칩"으로 단정했다. STM32G071은 AM32 지원 MCU 중 **하나**일 뿐이고, 8-bit EFM8 · AT32 · GD32도 흔하다. "$1" 가격은 근거가 없다 → 삭제.

FTE 포인트 (방산 맥락 [추정]): BLHeli_32 종료 이유로 보도된 것이 "전쟁용 드론 사용 · 수출 규제"였다. 방산 드론 회사에게 **ESC 펌웨어의 공급망 · 갱신 가능성**은 리스크 항목이다. 그래서 ESC 펌웨어 버전을 기체 구성(configuration) 기록에 넣고, ESC 펌웨어 교체 시 회귀 테스트를 도는 게 자연스럽다.

### 3.4 Flight Controller (FC)

FC는 MCU 하나 + 센서 + 여러 UART를 가진 보드다.

| 구성 요소 | 흔한 부품 | 버스 | 펌웨어 쪽 위치 (Betaflight) |
|---|---|---|---|
| MCU | STM32F405/F411 (F4), F722/F745 (F7), H743/H750/H723/H730 (H7), G473 (G4), AT32F435 | — | `src/platform/` 아래 `STM32`, `AT32`, `APM32`, `GD32`, `PICO`, `ESP32`, `SIMULATOR`, `X32` 디렉터리 [확인됨 소스, master] |
| IMU (gyro + accel) | ICM-42688-P, BMI270, (구형) MPU6000 | **SPI** + DRDY/EXTI 인터럽트 | `src/main/drivers/accgyro/` |
| 기압계 (baro) | BMP280, DPS310, SPL06 등 | I2C 또는 SPI | `src/main/drivers/barometer/` |
| 지자기 (mag) | GPS 모듈에 함께 붙는 경우 많음 (QMC5883L 등) [확인 필요] | I2C | `src/main/drivers/compass/` |
| Blackbox 저장 | SPI NOR flash (예: 128 Mbit = 16 MB) 또는 SD 카드 | SPI / SDIO | `src/main/drivers/flash/`, `sdcard*.c`, `src/main/blackbox/` |
| 아날로그 OSD | MAX7456 / AT7456E 계열 | SPI | `src/main/drivers/max7456.c` [확인됨 소스] |
| 아날로그 VTX 내장 (일부 AIO) | RTC6705 | SPI 계열 | `src/main/drivers/vtx_rtc6705.c` [확인됨 소스] |
| 전압 · 전류 센싱 | 분압 저항 + shunt/전류 센서 | ADC | `src/main/drivers/adc.c` |
| USB | MCU 내장 USB FS (VCP) | USB | `serial_usb_vcp.h` |
| UART 여러 개 | RX, GPS, VTX 제어, ESC 텔레메트리, companion | UART | `serial_uart.c` |
| CAN (일부 고급 FC) | DroneCAN 장치 | CAN | Betaflight `drivers/can` 존재, PX4/ArduPilot에서 주로 사용 |

> ⚠️ Gemini 정정: Gemini는 Blackbox flash를 "16MB~128MB SPI NOR"라고 했다. 흔한 W25Q128 같은 칩은 **128 Mbit = 16 MB**다. Mbit와 MB를 섞은 것으로 보인다. 용량은 보드마다 다르니 숫자 대신 "보드 사양서 확인"이 맞다.

> ⚠️ Gemini 정정: Gemini는 "IMU 값을 최대 8kHz로 읽는다"를 모든 FC에 일반화했다. gyro 샘플률은 **센서마다 다르다**. ICM-42688-P 계열은 8 kHz 이상 ODR을 지원하지만, BMI270은 Betaflight에서 더 낮은 rate(3.2 kHz로 알려짐)로 돈다 [확인 필요: Betaflight 문서에서 BMI270 gyro rate]. "8 kHz = 125 µs"는 계산은 맞지만 "항상 8 kHz"는 아니다.

FC 위 펌웨어 선택지 (같은 보드에 하나만 올라간다 — 자세한 비교는 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md), [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)):

| 펌웨어 | 실행 모델 | 대표 MCU | 강점 |
|---|---|---|---|
| Betaflight | bare-metal + 자체 cooperative scheduler | F4/F7/H7/G4, AT32 등 | 수동 비행 반응성, 2025.12부터 Altitude Hold · Position Hold 추가 |
| INAV | Betaflight 계열과 같은 뿌리(Cleanflight)의 bare-metal | F4/F7/H7 | GPS 내비게이션 · 미션 · 고정익 |
| PX4 | NuttX RTOS + uORB pub/sub | F7/H7 (Pixhawk 계열) | 모듈 구조, MAVLink/uXRCE-DDS로 companion 연동 |
| ArduPilot | ChibiOS RTOS (STM32), Linux 보드도 지원 | F4/F7/H7 | 기체 종류 폭, `Tools/autotest` |

> ⚠️ Gemini 정정: Gemini는 Betaflight를 "지도 찍고 혼자 찾아가기 · GPS Position Hold는 거의 못 한다"고 했다. **Betaflight 2025.12 릴리스는 Altitude Hold와 Position Hold를 추가했다** (빌드 옵션 `ALTITUDE_HOLD`, `POSITION_HOLD`, GPS 필수, mag 권장 — Betaflight 공식 문서). 웨이포인트 미션은 여전히 INAV/PX4/ArduPilot 영역이지만, "위치 유지 불가"는 이제 틀린 말이다.

### 3.5 PDB · 전원 · BEC

자세한 표는 5장. 핵심만:

- LiPo 한 셀은 nominal 3.7 V, 만충 4.2 V. 6S는 nominal 22.2 V, 만충 25.2 V. 4S는 14.8 V / 16.8 V (계산값).
- 배터리 → ESC (직접 대전류) + FC/PDB의 BEC (step-down) → 5 V, 9~10 V, 3.3 V 레일.
- 4-in-1 ESC 스택에서는 PDB가 따로 없고 ESC 보드가 배터리 패드를 갖는 경우가 많다.

### 3.6 Radio RX (ELRS)

| 항목 | 내용 | 확인 상태 |
|---|---|---|
| RF 칩 | 2.4 GHz: Semtech **SX1280** (LoRa + FLRC). sub-GHz: **SX127x** (LoRa). 듀얼밴드: **LR1121** (sub-GHz + 2.4 GHz, LoRa + GFSK) | ELRS `src/lib/SX1280Driver/`, `src/lib/LR1121Driver/` 존재, `common.cpp` rate 표에 SX127x · SX1280 · LR11XX 설정 [확인됨 소스] |
| MCU | 흔한 2.4 GHz RX는 ESP8285, TX 모듈은 ESP32. 일부 구형 · 특정 RX는 STM32 | Betaflight `crsf_protocol.h` 주석 "Avoid using with ExpressLRS STM32 receivers" — STM32 ELRS RX가 존재함 [확인됨 소스] |
| FC 쪽 출력 | CRSF over UART, 기본 420000 baud | Betaflight `CRSF_BAUDRATE 420000` [확인됨 소스], ELRS 2.0.1 이후 RX는 임의 baud 지원 (릴리스 노트) |
| 설정 | Web UI (Wi-Fi), 조종기 Lua script | ELRS 문서 |

> ⚠️ Gemini 정정: Gemini는 "SX127x/LR1121(900MHz)"로 LR1121을 900 MHz 칩으로 묶었다. LR1121은 **sub-GHz와 2.4 GHz를 모두** 지원하는 멀티밴드 칩이고, ELRS rate 표에 `LORA_DUAL`(두 밴드 동시) 모드까지 있다. 또 Gemini 그림의 "Radio RX: STM32 + SX1280"은 흔한 구성이 아니다 — 대부분은 ESP8285 + SX1280이다.

> ⚠️ Gemini 정정: Gemini 그림은 RX → FC UART를 "420k ~ 5.25M baud"로 썼다. FC 쪽은 기본 420000이 표준이고, **921600 · 1.87M · 3.75M 같은 "Mega Baud"는 조종기(handset) ↔ TX 모듈 쪽** 설정이다 (ELRS 2.0.1 / EdgeTX 2.5 릴리스 노트). 5.25M은 공식 문서에서 확인하지 못했다 [확인 필요].

### 3.7 GPS · mag

- GPS 모듈은 u-blox M10 계열(예: MAX-M10S 모듈)이 흔하다 [확인 필요: 부품 단위]. FC와 UART로 연결, UBX(u-blox binary) 또는 NMEA 프로토콜.
- mag는 같은 케이블의 I2C 선을 쓰는 경우가 많다. 모터 전류선과 가까우면 자기 간섭이 생긴다 → 설치 위치가 테스트 변수.
- Betaflight GPS Rescue, 2025.12 Position Hold, INAV/PX4/ArduPilot 내비게이션이 GPS를 쓴다.
- 방산 맥락: State Estimation & Navigation Lead 공고는 "GPS denied and GPS degraded navigation"을 쓴다 [확인됨 공고]. GPS 품질이 나빠지는 상황이 정상 운용 조건이라는 뜻 → GNSS 시뮬레이터 · 재생(replay) 테스트가 필요해진다 [추정].

### 3.8 영상: 카메라 · VTX · 수신 측

| 방식 | 공중 쪽 | 지상 쪽 | 오픈소스 여부 | FC와의 연결 |
|---|---|---|---|---|
| Analog 5.8 GHz | CVBS 카메라 → 아날로그 VTX (예: RTC6705) | 아날로그 VRX / 고글 | 하드웨어 표준, 펌웨어는 VTX 제조사 | VTX 제어 SmartAudio(TBS) · Tramp(IRC) half-duplex UART, OSD는 FC의 MAX7456이 영상 위에 문자 합성 |
| HDZero | 전용 카메라 + HDZero VTX | HDZero 고글 / VRX | 일부 펌웨어 공개 [확인 필요] | MSP DisplayPort (OSD 문자를 FC가 MSP로 보냄) |
| DJI O3 / O4 | DJI air unit | DJI 고글 | closed | MSP DisplayPort (canvas OSD) |
| Walksnail (Avatar) | air unit | 고글 | closed | MSP DisplayPort |
| OpenHD / WFB-ng | Linux SBC (Raspberry Pi 등) + 카메라 + Wi-Fi 칩 (monitor mode injection) | Linux SBC + Wi-Fi 칩 | **GPL-3.0** [확인됨 GitHub] | MAVLink 텔레메트리도 같이 실어 보냄, GStreamer로 인코딩 · 디코딩 |

- CODEC 공고가 "similar to DJI O4"를 만든다고 하니, Neros는 **자체 디지털 영상 링크**를 개발 중이다 [확인됨 공고]. 그 테스트(glass-to-glass latency, 손실 시 화면 동작)는 FTE 범위에 들어올 수 있다 [추정].
- 영상 세부는 [R06](2026-10-24_R06_companion_video_linux_rtos.md).

> ⚠️ Gemini 정정: Gemini는 OpenHD/WFB-ng를 "Wi-Fi 칩의 모니터 모드를 해킹해 연결 절차 없이 날것의 패킷"이라고 설명했다. 방향은 맞지만 "해킹"이 아니라 **raw 802.11 frame injection** (monitor mode)과 **FEC**(forward error correction)로 재전송 없이 손실을 견디는 설계다. 재전송이 없으니 지연이 예측 가능해지는 대신, 손실은 FEC 한계를 넘으면 그대로 화면 깨짐이 된다.

### 3.9 VTX 제어와 OSD — 작지만 테스트 항목이 많은 곳

- **SmartAudio / Tramp**: FC가 VTX의 채널 · 출력(power) · pit mode를 UART로 바꾼다. half-duplex 한 선이라 baud · 극성 문제로 "가끔 안 먹는" 버그가 생긴다 [추정: 흔한 증상].
- **VTX table**: 지역 · 주파수 · 출력 단계를 FC 설정으로 정의 (`vtx_table.c`).
- **MSP DisplayPort**: 디지털 영상 시스템이 OSD를 직접 그리게, FC가 "어디에 어떤 문자"를 MSP로 보낸다. 링크 대역을 OSD가 먹는다.
- FTE 포인트: 방산에서는 **출력 · 주파수 설정이 규제 · 운용 요구**와 연결된다 [추정]. 설정이 의도대로 들어갔는지 텔레메트리로 확인하는 테스트가 필요하다.

### 3.10 Companion computer · payload

- Jetson (Orin Nano/NX 등) + Linux. 카메라는 MIPI CSI 또는 USB/Ethernet.
- FC와는 UART(MAVLink, MSP) 또는 Ethernet(PX4 uXRCE-DDS over UDP, MAVLink over UDP). Ethernet은 Pixhawk FMUv6X 같은 고급 FC에만 있다 — **일반 F4/F7 FPV FC에는 Ethernet이 없다**.
- Payload: 공고에 구체 이름은 없다. Gemini가 말한 "APEX"는 [Gemini 주장 · 미확인].

> ⚠️ Gemini 정정: Gemini 그림은 Jetson ↔ FC를 "UART / Ethernet, MAVLink / microDDS"로 Betaflight에도 그대로 붙였다. Betaflight 쪽 사실은 이렇다 (master 소스): `src/main/rx/mavlink.c`에 **MAVLink를 serial RX로 받는 기능**(`USE_SERIALRX_MAVLINK`, `RC_CHANNELS_OVERRIDE` 처리, 460800 baud 설정)이 있고, `rx/msp_override.c`도 있다. 즉 Betaflight에서 companion은 **"가상 조종기"처럼 RC 채널을 넣는 방식**이고, PX4처럼 setpoint(위치 · 속도 목표)를 받는 게 아니다. DDS(uXRCE-DDS)는 PX4 이야기다. 또 "microDDS"는 정식 이름이 아니고 PX4 문서 용어는 **uXRCE-DDS** (eProsima Micro XRCE-DDS, Apache-2.0)다. 어느 Betaflight 릴리스부터 MAVLink RX가 들어갔는지는 [확인 필요].

### 3.11 지상 쪽 장비

| 장비 | 하드웨어 | 펌웨어/소프트웨어 | 드론과의 링크 |
|---|---|---|---|
| Radio handset | STM32 계열 MCU (많은 기종이 F4, 일부 신형 H7) [확인 필요: 기종별], Hall gimbal → ADC | **EdgeTX** (GPL-2.0), 예전 OpenTX | 내부 또는 외부(JR bay) ELRS TX 모듈과 CRSF |
| TX module | ESP32 + SX1280 / LR1121 + PA/LNA | ExpressLRS | RF C2 링크 |
| Goggles / VRX | 방식별 (analog, HDZero, DJI, Walksnail) | 제조사 펌웨어 | 영상 수신, 일부는 DVR 녹화 |
| GCS laptop/tablet | Windows/Linux/Android | QGroundControl, Mission Planner, ATAK | MAVLink (텔레메트리 라디오, UDP), TAK는 CoT 메시지 |
| 테스트용 PC | Linux | pymavlink, MSP 클라이언트, 로직 분석기 SW | USB-UART, USB VCP, Ethernet |

EdgeTX 소스에서 확인한 경로 [확인됨 소스, main]: `radio/src/pulses/crossfire.cpp` (CRSF 출력), `radio/src/telemetry/crossfire.cpp` (CRSF 텔레메트리 수신), `radio/src/mixer.cpp`, `radio/src/mixer_scheduler.cpp` (mixer 실행 시점 — 모듈 주기에 맞춰 돌리는 부분으로 보인다 [확인 필요: 동작 세부]).

ATAK-CIV 저장소(`deptofdefense/AndroidTacticalAssaultKit-CIV`)는 GPL-3.0이고 **archived** 상태다 [확인됨 GitHub API]. 디렉터리에 `takcot`, `takproto`, `pluginsdk.zip`이 있다 — CoT(Cursor on Target) 메시지와 플러그인 구조. 현재 TAK 생태계의 주 개발 위치는 다를 수 있다 [확인 필요].

---

## 4. 보드 사이 버스 — 전체 배선도

### 4.1 그림

```text
                         (ground)                       (air)
 +-----------+  CRSF UART   +-----------+  RF   +-----------+
 | EdgeTX    |=============>| ELRS TX   |~~~~~~>| ELRS RX   |
 | handset   |<=============| module    |<~~~~~~|           |
 +-----------+  telemetry   +-----------+  tlm  +-----+-----+
   ^ USB (sim/joystick)                             | CRSF UART 420000 (TX+RX pair)
   |                                                v
 +-----------+                         +------------------------------+
 | Test PC   |---- USB VCP (MSP/CLI) ->|  FC (Betaflight / INAV)      |
 | (Linux)   |---- SWD (J-Link) ------>|                              |
 +-----------+                         |  SPI1 --> IMU (DRDY -> EXTI) |
                                       |  SPI2 --> Flash (Blackbox)   |
                                       |  SPI3 --> MAX7456 OSD        |
                                       |  I2C  --> Baro, Mag          |
                                       |  UART --> GPS (UBX/NMEA)     |
                                       |  UART --> VTX SmartAudio/Tramp (half-duplex)
                                       |  UART <-- ESC serial telemetry (1-wire)
                                       |  UART <-> MSP DisplayPort (digital VTX)
                                       |  UART <-> Companion (MAVLink RX / MSP)
                                       |  ADC  <-- VBAT divider, current sensor
                                       |  TIM+DMA x4 --> DShot (bidirectional: eRPM back)
                                       +---------------+--------------+
                                                       | 4 signal + GND
                                                       v
                                       +------------------------------+
                                       | 4-in-1 ESC (Bluejay / AM32)  |
                                       +------------------------------+
```

PX4/ArduPilot FC라면 이 그림에서 MSP 자리를 MAVLink가, ESC serial telemetry 자리를 DroneCAN이 차지하는 경우가 많다. companion은 Ethernet(UDP)으로도 붙는다.

### 4.2 버스 · 프로토콜 표

| 링크 | 물리 계층 | 속도 · 주기 | 프레임 | 무결성 | 방향 | 확인 상태 |
|---|---|---|---|---|---|---|
| Handset → TX module (CRSF) | UART (internal), JR bay는 S.Port 핀 한 선 half-duplex [확인 필요] | 400000 기본, Mega Baud 921600 / 1870000 / 3750000 | CRSF | CRC-8 | 양방향 (RC ↓, 텔레메트리 ↑) | ELRS 2.0.1 릴리스 노트 |
| TX ↔ RX (ELRS OTA) | 2.4 GHz LoRa/FLRC/GFSK, sub-GHz LoRa/GFSK | packet rate 25 Hz ~ 1000 Hz (rate 표) | OTA4 / OTA8 packet | CRC + FHSS 동기 | 양방향, telemetry ratio (예: 1:128) | ELRS `src/src/common.cpp` [확인됨 소스] |
| RX → FC (CRSF) | UART 8N1, 3.3 V | 420000 baud (Betaflight 기본, `USE_CRSF_OFFICIAL_SPEC`면 416666) | `[sync/addr 0xC8][len][type][payload][crc]`, RC packed type 0x16, payload 22 bytes | **CRC-8 DVB-S2 (poly 0xD5)**, type~payload | 양방향 (FC → RX로 텔레메트리) | Betaflight `crsf_protocol.h`, `crsf.c` [확인됨 소스] |
| RX → FC (SBUS, 구형) | **반전된** UART 8E2 | 100000 baud | 25 bytes, 16ch × 11 bit | 끝 바이트 · flag | 단방향 | 일반 지식, Betaflight `sbus.c` |
| FC → IMU | SPI (수 MHz~수십 MHz) [확인 필요: 칩별] + DRDY 인터럽트 | gyro ODR (센서 설정) | 레지스터 read burst | 없음 (WHO_AM_I로 존재 확인) | 양방향 | 일반 지식 |
| FC → Baro / Mag | I2C 100/400 kHz 또는 SPI | 수십 Hz~ [확인 필요] | 레지스터 | 없음 | 양방향 | 일반 지식 |
| FC → Flash | SPI (QSPI/OSPI 지원 보드도 있음) | — | page program | 칩 ECC 없음이 보통 | 쓰기 위주 | Betaflight `bus_quadspi.c`, `bus_octospi.c` 존재 |
| FC → ESC (DShot) | 타이머 + DMA 또는 GPIO bitbang, 단선 | DShot600 = 600 kbit/s | 16 bit: 11 throttle + 1 telemetry request + 4 CRC | 4-bit XOR CRC | 단방향 | 일반 지식, Betaflight `dshot.h`: throttle 48~2047 [확인됨 소스] |
| ESC → FC (bidirectional DShot) | 같은 선, 극성 반전 | 응답 bitrate = DShot bitrate × 5/4 | 21-bit GCR 인코딩 → 16 bit (eRPM 12 + CRC 4) | 4-bit CRC | 역방향 | Betaflight `dshot_bitbang.c`의 input freq 계산 (×5×2/24 · oversample), `dshot_bitbang_decode.c`의 21-bit [확인됨 소스] |
| ESC → FC (serial telemetry) | 단선 UART | [확인 필요: 115200 baud로 알려짐] | 온도 · 전압 · 전류 · 소모량 · eRPM | CRC-8 [확인 필요] | 역방향 | 확인 필요 |
| FC ↔ VTX (SmartAudio/Tramp) | half-duplex UART 한 선 | 저속 (SmartAudio 4800 계열로 알려짐) [확인 필요] | 명령/응답 | CRC | 양방향 | 확인 필요 |
| FC ↔ digital VTX (MSP DisplayPort) | UART | 115200 이상 [확인 필요] | MSP v1/v2 | XOR / CRC-8 DVB-S2 | 양방향 | 일반 지식 |
| FC ↔ Configurator / test PC (MSP, CLI) | USB VCP (USB FS) | USB 1 ms frame 단위 polling | MSP v1 `$M<`, v2 `$X<` | v1 XOR, v2 CRC-8 DVB-S2 | 양방향 | 일반 지식 |
| FC ↔ Companion (MAVLink) | UART (또는 Ethernet UDP: PX4/ArduPilot) | 921600 등 설정 | MAVLink v2 `0xFD` 헤더 10 B + payload + CRC 2 B (+ 서명 13 B) | CRC-16/MCRF4XX + CRC_EXTRA | 양방향 | MAVLink 문서 |
| FC ↔ GPS | UART | 설정에 따름 | UBX / NMEA | UBX: 8-bit Fletcher checksum | 양방향 | 일반 지식 |
| Debug | SWD (SWDIO/SWCLK/NRST), SWO/RTT | — | — | — | — | [R08](2026-10-24_R08_hil_bench_design_instruments.md) |

> ⚠️ Gemini 정정: Gemini는 CRSF 프레임을 `[Addr][Len][Type][Payload][CRC8(0xD5)]`라고 썼고 이건 맞다. 다만 Betaflight 소스를 보면 **CRC를 두 종류** 계산하는 코드가 있다: `crc8_dvb_s2`(일반 프레임)와 `crc8_poly_0xba`(일부 command 프레임). 파서 테스트에서 command 프레임까지 다룰 때는 이 차이를 놓치지 말 것 [확인됨 소스, 용도 세부는 R05에서].

### 4.3 버스에서 "계산되는" 시간

아래는 공식 · 소스 값으로 직접 계산한 것이다. 측정이 아니라 **하한(전송 시간)** 이다.

| 항목 | 계산 | 결과 |
|---|---|---|
| CRSF RC frame @ 420000 baud | 26 bytes × 10 bit (8N1) ÷ 420000 | **≈ 619 µs** |
| CRSF RC frame @ 416666 baud | 260 ÷ 416666 | ≈ 624 µs |
| CRSF RC frame @ 400000 (handset → TX 기본) | 260 ÷ 400000 | 650 µs |
| CRSF RC frame @ 921600 | 260 ÷ 921600 | ≈ 282 µs |
| CRSF RC frame @ 1870000 | 260 ÷ 1870000 | ≈ 139 µs |
| CRSF 최대 64 bytes @ 420000 | 640 ÷ 420000 | ≈ 1524 µs (Betaflight 주석 "~1550us", 여유 두고 1748 µs 사용) |
| SBUS frame | 25 bytes × 12 bit (8E2) ÷ 100000 | 3000 µs |
| DShot150 bit / frame | 1 ÷ 150 kHz, × 16 | 6.67 µs / 106.7 µs |
| DShot300 bit / frame | 1 ÷ 300 kHz, × 16 | 3.33 µs / 53.3 µs |
| DShot600 bit / frame | 1 ÷ 600 kHz, × 16 | **1.67 µs / 26.7 µs** |
| Bidir DShot600 응답 (21 GCR bit @ 750 kbit/s) | 21 × 1.333 µs | ≈ 28 µs (+ 전환 gap, [확인 필요]) |
| Bidir DShot300 응답 | 21 × 2.667 µs | 56 µs (+ gap) |
| PID loop 8 kHz / 4 kHz / 3.2 kHz / 2 kHz | 1 ÷ f | 125 / 250 / 312.5 / 500 µs |
| ELRS 1000 / 500 / 250 / 150 / 50 Hz | 1 ÷ f (`common.cpp` interval 값과 일치) | 1000 / 2000 / 4000 / 6666 / 20000 µs |
| MAVLink v2 `SET_POSITION_TARGET_LOCAL_NED` (payload 53 B + 12 B) @ 921600 | 65 × 10 ÷ 921600 | ≈ 705 µs |
| 같은 메시지 @ 57600 (저속 텔레메트리 라디오) | 650 ÷ 57600 | ≈ 11.3 ms |
| MSP v1 16채널 RC (6 B 헤더+체크섬 + 32 B) @ 115200 UART라면 | 380 ÷ 115200 | ≈ 3.3 ms |

계산 스크립트 (표준 라이브러리만):

```python
def frame_us(nbytes, bits_per_byte, baud):
    """UART frame time in microseconds (8N1 -> 10 bits, 8E2 -> 12 bits)."""
    return nbytes * bits_per_byte / baud * 1e6

def dshot_us(rate_kbit):
    bit = 1e3 / rate_kbit            # us per bit
    frame = 16 * bit                 # 16-bit command frame
    tlm = 21 * (1e3 / (rate_kbit * 1.25))  # 21 GCR bits at 5/4 bitrate
    return bit, frame, tlm

if __name__ == "__main__":
    assert round(frame_us(26, 10, 420000)) == 619
    assert round(frame_us(25, 12, 100000)) == 3000
    bit, frame, tlm = dshot_us(600)
    assert round(bit, 2) == 1.67 and round(frame, 1) == 26.7 and round(tlm) == 28
    print("ok")
```

관찰 두 개:

- **bidirectional DShot300은 8 kHz 루프에 빡빡하다.** 53.3 µs 송신 + gap + 56 µs 응답이면 125 µs 안에 겨우 들어가거나 넘친다. 그래서 "8 kHz면 DShot600"이라는 커뮤니티 권장이 나온다 [확인 필요: Betaflight 문서의 권장 조합]. 테스트 매트릭스에 **(PID rate × DShot rate × bidir on/off)** 조합을 넣을 이유다.
- **CRSF 한 프레임(619 µs)이 PID 루프 5번(125 µs × 5 = 625 µs)과 비슷하다.** RX 경로의 지연은 PID 루프보다 한 자릿수 크다. 그래서 "최우선은 gyro/PID, RX는 그다음"이라는 스케줄러 설계가 자연스럽다 ([R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md)).

---

## 5. 전원 레일 — bring-up과 brown-out 테스트의 지도

```text
 [LiPo nS]--XT60--+--------------------------> ESC power (4-in-1)  (대전류)
                  |                                  |
                  |                             current sensor --> FC ADC
                  +--VBAT divider ----------------> FC ADC (vbat)
                  |
                  +--[BEC 10V/9V]--> VTX / camera / digital air unit
                  +--[BEC 5V]------> RX, GPS, LED, (일부) companion
                  +--[LDO 3.3V]----> MCU, IMU, baro, flash  (FC 보드 안)
                  +--[별도 DC-DC]--> Jetson (전류 큼, 부팅 시 피크)
```

| 레일 | 흔한 소비자 | 고장 시 증상 | 테스트 |
|---|---|---|---|
| VBAT (배터리 직결) | ESC, BEC 입력 | 전압 sag → BEC 출력 흔들림, ESC desync | 전원 공급기 전압 ramp-down, 부하 계단 |
| 10 V / 9 V | VTX, 카메라, digital air unit | 영상 끊김, VTX 리셋 | 레일 전압 · ripple을 오실로스코프로, VTX 출력 변경 시 전류 |
| 5 V | RX, GPS | RX 재부팅 → failsafe | 5 V 레일 순간 drop 주입 → failsafe 진입 시간 측정 |
| 3.3 V | MCU, IMU | MCU brown-out reset, gyro 데이터 이상 | BOR 레벨 확인, 리셋 원인 레지스터 로그 |
| Jetson 전원 | companion | 부팅 피크가 다른 레일을 끌어내림 [추정] | 부팅 중 다른 레일 동시 캡처 |

- 셀 전압 계산: 6S 만충 25.2 V, nominal 22.2 V. 4S 만충 16.8 V, nominal 14.8 V. 저전압 경고 임계값은 설정값이므로 공급기로 **임계값 근처를 천천히 지나가게** 해서 경고 · 동작을 확인한다.
- 안전: LiPo 충전 · 방전, 대전류 배선은 위험 작업이다. 벤치에서는 **전류 제한된 전원 공급기**를 먼저 쓰고, LiPo는 방화 백 · 감시 하에서만. 프롭은 항상 뗀다.
- Adam 질문 연결: 전원 넣기 전에 무전원 단락 체크 → 전류 제한 인가 → 레일 확인 → 클럭/리셋 → SWD → 버스 ID 읽기 순서. 이 표가 "레일 확인" 단계의 기대값 목록이 된다 ([R08](2026-10-24_R08_hil_bench_design_instruments.md)).

---

## 6. End-to-end 신호 경로와 시간 예산

원칙: **계산값은 하한**, 실제는 측정한다. 각 hop에서 "평균"보다 **분포(특히 꼬리)** 가 중요하다. 이유: 패킷 주기 대기처럼 **균등 분포로 랜덤한 대기**가 여러 개 겹치기 때문이다.

### 6.1 (1) Control uplink — stick → motor

```text
 stick -> ADC -> EdgeTX mixer -> CRSF UART -> ELRS TX -> (wait RF slot) -> air -> ELRS RX
   H1        H2              H3            H4                H5          H6
 -> CRSF UART 420k -> FC UART/DMA -> RX task -> rc smoothing/feedforward -> PID loop
        H7                H8            H9              H10                 H11
 -> mixer -> DShot600 -> ESC commutation -> motor/prop torque -> attitude change
     H12       H13            H14                 H15
```

| Hop | 내용 | 프로토콜 | 계산 가능한 하한 | 지연 · 지터 원인 | 측정 방법 |
|---|---|---|---|---|---|
| H1 | stick → ADC 샘플 | 아날로그 (Hall) | — | ADC 샘플 주기, 필터 | 조종기 trainer 포트/USB joystick 입력으로 stick 대신 **알려진 계단 입력** [확인 필요: EdgeTX 입력 주입 방법] |
| H2 | EdgeTX mixer 계산 | 내부 | — | mixer 주기, 모듈 주기와의 위상 | `mixer_scheduler.cpp` 동작 읽기, 조종기 CRSF 출력선 캡처 |
| H3 | handset → TX CRSF | UART | 650 µs @ 400k, 139 µs @ 1.87M | baud 설정 | 로직 분석기 ch1: handset → TX 선 |
| H4 | TX 내부 처리 · 다음 RF slot 대기 | — | 0 ~ 1 packet interval (500 Hz면 0~2 ms, 평균 ~1 ms) | packet rate, handset 출력과 RF slot의 위상 (ELRS가 handset 타이밍을 맞추는 기능이 있음 [확인 필요: 이름 · 동작]) | ch1 ↔ ch2 시간차의 분포 |
| H5 | 공중 전송 (air time) | LoRa/FLRC/GFSK | 변조 · 패킷 크기로 결정 [확인 필요: rate별 air time] | 패킷 손실 시 다음 패킷까지 1 interval 추가 | RF 감쇠기로 손실률을 바꿔 가며 분포 비교 |
| H6 | RX 수신 · 디코드 | — | — | FHSS 동기, 처리 시간 | — |
| H7 | RX → FC CRSF | UART 420000 | **619 µs** | RX가 프레임을 언제 내보내는지 (패킷 수신 직후인지) | 로직 분석기 ch2: RX → FC 선 |
| H8 | FC UART 수신 | DMA/IRQ | — | 인터럽트 지연, 프레임 끝 판정 | FC 안 GPIO 토글 (프레임 완료 시) |
| H9 | RX task가 프레임 처리 | 스케줄러 | 0 ~ task 대기 | 스케줄러가 RX task를 언제 고르나 | Betaflight CLI `tasks`, GPIO 토글, DWT cycle counter |
| H10 | rc smoothing / feedforward | 필터 | — | **필터 cutoff가 낮을수록 group delay 증가** (가장 큰 설정 의존 항목일 수 있음 [추정]) | Blackbox의 rcCommand vs setpoint 비교, 또는 DShot 변화 시점 |
| H11 | PID loop | 8 kHz 등 | 0 ~ 125 µs 대기 (8 kHz) | gyro 동기 루프 위상 | GPIO 토글 |
| H12 | mixer | 내부 | 짧음 | — | — |
| H13 | DShot600 송신 | DShot | 26.7 µs | 다음 루프까지 대기 | 로직 분석기 ch3: DShot 선 디코드 |
| H14 | ESC 처리 · commutation 반영 | — | — | ESC 펌웨어 처리 주기, PWM 주파수 | bidirectional DShot eRPM 변화 시점 |
| H15 | 모터 · 프롭 기계 응답 | 물리 | — | 관성, 부하 — **ms 단위로 전자 구간보다 클 수 있음** [추정] | eRPM 기울기, 추력 스탠드, 고속 카메라 |

전자 구간 하한을 더해 보면 (500 Hz 예시, 손실 없음):

| 구간 | 하한 |
|---|---|
| H3 (400k) | 0.65 ms |
| H4 평균 대기 | ~1.0 ms (0~2 ms 균등 가정) |
| H5 + H6 | [확인 필요] |
| H7 | 0.62 ms |
| H9 + H11 | 0 ~ 0.125 ms + task 대기 |
| H13 | 0.03 ms |
| 합 (H5/H6/H10 제외) | **대략 2.3 ms 근처 + 측정 안 된 항목** (계산값의 합일 뿐, 측정값 아님) |

이 표의 메시지: **packet rate 대기와 rc smoothing이 큰 항목이고, PID 루프 · DShot은 작다.** 그러니 "8 kHz 루프로 latency를 줄였다"보다 "packet rate · RX 출력 타이밍 · 필터 설정"이 체감 지연을 더 바꾼다 [추정, 측정으로 확인할 것].

측정 셋업 (프롭 없이, K05 계획과 같은 방향):

```text
 [Test PC] --USB--> [step generator: USB-UART injecting CRSF into TX module or FC]
                         |
   logic analyzer ch1 ---+  (CRSF into TX module)
   logic analyzer ch2 -------- (CRSF RX -> FC)
   logic analyzer ch3 -------- (DShot motor 1)
   logic analyzer ch4 -------- (FC GPIO toggle: RX frame done / PID loop)
 shared timebase -> per-step deltas -> histogram (p50, p99, max)
```

- 계단 입력을 수천 번 반복해 분포를 얻는다. 한 번 재는 숫자는 의미가 없다 (packet 위상이 랜덤).
- 무엇을 "도착"으로 볼지 정의가 필요하다: CRSF 프레임의 **해당 채널 값이 바뀐 첫 프레임**, DShot의 **스로틀 값이 바뀐 첫 프레임**.
- 이 셋업은 RF를 빼면 하드웨어 없이도 일부 가능하다: USB-UART로 FC에 직접 CRSF를 넣고 DShot을 보는 것 (H7~H13만).

### 6.2 (2) Telemetry downlink — FC → 조종기 / GCS

```text
 FC sensors -> telemetry task -> CRSF frames (battery, GPS, attitude, link stats)
   -> CRSF UART -> ELRS RX -> wait downlink slot (telemetry ratio, e.g. 1:64, 1:128)
   -> air -> ELRS TX -> CRSF -> EdgeTX telemetry -> screen / Lua / logging
 (PX4/ArduPilot) FC -> MAVLink -> telemetry radio / companion / UDP -> QGC / Mission Planner
```

| Hop | 지연 원인 | 측정 |
|---|---|---|
| FC telemetry task | task 주기, 센서 업데이트 주기 | FC GPIO 토글 / Blackbox |
| RX downlink 대기 | **telemetry ratio**: uplink packet N개 중 1개만 downlink. 예: 500 Hz에 1:128이면 downlink 슬롯은 초당 약 3.9개 (계산값) | TX 쪽 CRSF 출력 캡처 |
| 다중 센서 순환 | 여러 종류 telemetry 프레임을 번갈아 보내므로 특정 값 갱신 주기가 더 길어진다 | EdgeTX 로그의 값 갱신 간격 분포 |
| GCS 쪽 | MAVLink 스트림 rate 설정, 무선 대역 | pymavlink로 메시지 수신 간격 기록 |

- 테스트 포인트: **link statistics(LQ, RSSI, SNR)** 자체가 텔레메트리다. 링크가 나빠질 때 그 값이 늦게 · 드물게 오므로, "링크가 나쁘다"는 보고가 늦어진다 — 오퍼레이터 UX 문제이자 테스트 대상 [추정].
- ELRS rate 표에서 rate별 기본 telemetry ratio가 다르다 (예: FLRC 1000 Hz 1:128, LoRa 2.4 GHz 50 Hz 1:16) [확인됨 소스, `common.cpp`].

### 6.3 (3) Video — glass-to-glass

```text
 scene -> lens -> sensor exposure/readout -> ISP -> (digital: encoder) -> VTX/radio
   -> air -> VRX/goggles -> (digital: decoder, jitter buffer) -> display scan-out -> eye
 analog: sensor -> CVBS -> FM modulate -> air -> demod -> display (no encode/decode)
```

| 단계 | 아날로그 | 디지털 (HDZero/DJI/Walksnail/OpenHD) | 측정 |
|---|---|---|---|
| 노출 · 리드아웃 | 프레임/필드 주기 영향 (NTSC 59.94 field/s → 필드 하나 ≈ 16.7 ms, PAL 50 → 20 ms, 계산값) | 센서 frame rate 영향 | LED 깜빡임을 찍고 화면에서 포토다이오드로 감지 |
| 인코딩 | 없음 | slice/frame 단위 인코딩, 시스템마다 다름 | — |
| 전송 | 연속 아날로그 (손실 = 잡음) | 패킷, FEC, 재전송 여부에 따라 지터 | 패킷 손실 주입 (감쇠기) |
| 디코딩 · 버퍼 | 없음 | jitter buffer 크기가 지연과 끊김의 trade-off | — |
| 디스플레이 | 고글 패널 scan-out | 동일 | 고속 카메라로 원본 LED와 화면 동시 촬영 |

- 제조사 공칭 latency 숫자는 여기 쓰지 않는다 — 측정 조건이 제각각이라 [확인 필요].
- glass-to-glass는 **평균과 꼬리, 그리고 손실 시 동작(멈춤? 깨짐? 블랙?)** 을 함께 본다. 방산 FPV에서는 영상이 곧 조준이므로 손실 동작이 중요하다 [추정].
- OSD 경로는 별도다: 아날로그는 FC의 MAX7456이 영상 위에 문자 합성, 디지털은 MSP DisplayPort로 VTX에 문자 정보를 보낸다. **OSD 갱신 지연 ≠ 영상 지연**.

### 6.4 (4) Autonomy — camera → Jetson → FC

```text
 camera -> (MIPI CSI / USB) -> Jetson capture -> preprocessing -> TensorRT inference
   -> tracker / planner (ROS 2 nodes) -> setpoint
   -> PX4: MAVLink SET_POSITION_TARGET_* or uXRCE-DDS topic (UART / UDP)
   -> Betaflight: MAVLink RC_CHANNELS_OVERRIDE (serial RX) or MSP override (RC 채널 주입)
   -> FC controller -> DShot -> motor
```

| Hop | 지연 원인 | 측정 |
|---|---|---|
| 카메라 → 메모리 | 노출, 리드아웃, 드라이버 버퍼 수 | 프레임 timestamp (센서 · 드라이버) |
| 추론 | 모델 크기, 정밀도(FP16/INT8), GPU 경합 | Jetson에서 단계별 timestamp, 부하 걸고 재측정 |
| ROS 2 중계 | executor, QoS, DDS 설정 | ROS 2 tracing [확인 필요: 도구 이름], 메시지 header stamp |
| Jetson → FC 링크 | UART 전송 시간 (예: 65 B @ 921600 ≈ 0.7 ms), 링크 큐 | Jetson 송신 GPIO + FC 수신 GPIO를 같은 로직 분석기로 |
| FC 처리 | PX4: uORB 토픽 → controller 주기. Betaflight: RC 채널 경로 = 6.1의 H9~H13과 같음 | FC GPIO / 로그 |

- **시간 동기화**가 핵심 문제다. Jetson 시계와 FC 시계가 다르다. MAVLink `TIMESYNC`, PTP(Ethernet), 혹은 GPIO 펄스로 두 쪽 timestamp를 맞춘다 [확인 필요: PX4 · Betaflight 지원 범위]. Platform 공고는 "reconciling recorded sensor timing with offline ground truth"를 쓴다 [확인됨 공고] — 시간 정렬이 실제 업무 주제라는 뜻.
- Betaflight에 companion 제어를 붙이면 **companion이 조종사와 같은 RC 경로**로 들어간다. 그래서 failsafe · arming · 모드 전환 규칙이 조종기 입력과 어떻게 섞이는지(누가 우선인가)를 테스트해야 한다 [추정].
- Autonomy Platform & Runtime Lead 공고: "without degrading control loop timing" [확인됨 공고 발췌] — companion 부하가 FC 루프 타이밍을 해치지 않아야 한다는 요구. FTE에게는 **"companion 트래픽을 최대로 넣고 FC 루프 지터를 재는"** 테스트 아이디어가 된다.

---

## 7. Hop별 고장 모드와 그걸 잡는 테스트

| Hop / 보드 | 고장 모드 | 증상 | 잡는 테스트 | 피라미드 위치 ([R07](2026-10-24_R07_test_methodology_pyramid.md)) |
|---|---|---|---|---|
| Handset ADC | gimbal 보정 틀어짐, 데드밴드 | 중립에서 드리프트 | 보정 후 채널 값 범위 · 중립 확인 자동화 | HIL (ground) |
| EdgeTX mixer | 모델 설정 오류, 채널 순서(AETR/TAER) | 엉뚱한 축 반응 | CRSF 출력 디코드해 채널 매핑 확인 | 단위/HIL |
| Handset → TX UART | baud 불일치, 인버터 품질 | 간헐적 프레임 손실 | baud 매트릭스 × 장시간 프레임 카운트 | HIL |
| ELRS RF | 거리 · 간섭 · 재밍, binding phrase 불일치 | LQ 하락, failsafe | RF 감쇠기 sweep, 간섭원 추가, 손실률 vs failsafe 진입 시간 | HIL (conducted RF) |
| ELRS TX/RX 버전 | OTA 프로토콜 버전 불일치 | 연결 안 됨 | 버전 조합 매트릭스 | 통합 |
| RX → FC CRSF | 노이즈로 깨진 바이트, 잘린 프레임, 가짜 sync | 채널 튐 또는 프레임 drop | **깨진 CRSF 스트림 주입** (CRC 오류, len 이상, 0xC8 가짜 헤더) → resync · 프레임 카운터 확인 | 단위(파서) + HIL |
| RX 전원 | 5 V drop으로 RX 재부팅 | 수백 ms 링크 공백 | 5 V 레일 drop 주입, failsafe 단계 진입 확인 | HIL |
| FC 스케줄러 | task overrun, CPU load 과다 | gyro 루프 지터, 늦은 RX 처리 | `tasks` · `status` 스냅샷, GPIO 토글로 루프 주기 분포 | HIL + 회귀 |
| IMU SPI | 센서 미검출, DRDY 누락, 값 고착 | arming 거부 또는 이상 자세 | WHO_AM_I, 샘플 카운트, 고착 감지 | bring-up + HIL |
| IMU 진동 | 프레임 공진, 필터 설정 | 모터 과열, 떨림 | Blackbox 스펙트럼 비교 (버전 간) | 비행 테스트 |
| Baro / mag | I2C hang, 자기 간섭 | 고도/방향 이상 | I2C 버스 hang 복구 테스트, 모터 전류별 mag 값 변화 | HIL |
| Blackbox flash | 가득 참, 쓰기 지연 | 로그 손실 또는 루프 지연 | 가득 찬 상태로 비행 테스트 시뮬, 로그 연속성 검사 | HIL |
| DShot | 신호 무결성(긴 선, 노이즈), 타이밍 | 모터 떨림, desync | 로직 분석기 디코드 + CRC 오류율, bidir 에러율 (`USE_DSHOT_TELEMETRY_STATS` 통계) | HIL |
| Bidir DShot | 응답 디코드 실패 | RPM filter 오동작 | eRPM 수신률 통계, 일부러 선 노이즈 | HIL |
| ESC 펌웨어 | 버전 · 설정 차이, 동기 상실 | 특정 스로틀에서 모터 멈춤 | ESC 버전 고정 + 스로틀 sweep (프롭 없이 → 추력 스탠드) | HIL / 추력 스탠드 |
| VTX 제어 | SmartAudio 응답 없음 | 출력/채널 변경 실패 | 명령 후 VTX 상태 읽기 검증 | HIL |
| 영상 링크 | 손실, 지터, 인코더 과부하 | 끊김, 지연 증가 | 감쇠기 + glass-to-glass 반복 측정 | HIL |
| GPS | 신호 없음, 품질 저하 | rescue/hold 오동작 | GNSS 시뮬레이터 또는 SITL GPS 주입 [확인 필요: 장비] | SITL + HIL |
| Companion 링크 | 메시지 폭주, heartbeat 끊김 | FC 루프 지터, 잘못된 모드 | 트래픽 최대 부하 + FC 루프 지터, heartbeat 중단 → FC 동작 | SITL + HIL |
| 전원 | brown-out, 레일 ripple | 리셋, 센서 이상 | 공급기 ramp, 부하 계단, 레일 동시 캡처 | bring-up + HIL |
| GCS | 스트림 rate 설정, 버전 불일치 | 화면 값 늦음 | pymavlink로 수신 간격 · 누락 집계 | 통합 |

공통 패턴: 거의 모든 hop에 **"정상 스트림 + 비정상 주입 + 카운터/분포 관찰"** 이 들어간다. 이 패턴을 보드마다 새로 짜지 않고 재사용하는 게 [R09](2026-10-24_R09_scalable_reusable_test_platform.md)의 주제다.

---

## 8. 오픈소스 배치도

### 8.1 계층 그림

```text
 LAYER          | HANDSET     | TX/RX       | FC              | ESC        | VIDEO        | COMPANION      | GCS / PC
 ---------------+-------------+-------------+-----------------+------------+--------------+----------------+--------------
 bootloader     | STM32 ROM/  | ESP ROM     | STM32 ROM DFU,  | AM32       | (vendor)     | UEFI / U-Boot  | -
                | EdgeTX BL   | bootloader  | (PX4/AP: own BL)| bootloader |              | (Jetson: vendor)|
 OS / runtime   | bare-metal  | Arduino/ESP-| BF/INAV: bare-  | bare-metal | Linux (Open- | Linux (Yocto/  | Windows/
                | + EdgeTX    | IDF (Platfo-| metal; PX4:NuttX| (C / asm)  | HD/WFB-ng)   | Ubuntu L4T),   | Linux/Android
                | kernel [확인 | rmIO build) | AP: ChibiOS     |            |              | PREEMPT_RT?    |
                |  필요]      |             |                 |            |              |                |
 drivers        | ADC, LCD,   | SX1280/     | SPI IMU, DShot, | commutation| camera, Wi-Fi| V4L2, CSI      | USB serial
                | UART module | LR1121 drv  | UART, flash     | PWM        | injection    |                |
 app            | EdgeTX mixer| ELRS link,  | scheduler, PID, | Bluejay/   | GStreamer    | ROS 2, TensorRT| QGC, MP, ATAK
                | Lua         | FHSS        | mixer, failsafe | AM32       | pipelines    | apps           | Configurator
 protocol       | CRSF        | ELRS OTA,   | CRSF, MSP,      | DShot,     | MSP-DP, MAV- | MAVLink, DDS   | MAVLink, MSP,
                |             | CRSF        | MAVLink, DShot  | telemetry  | Link relay   | (uXRCE-DDS)    | CoT
 tools          | Companion   | Configurator| BF Configurator,| ESC        | -            | rosbag, tracing| Blackbox
                | simulator   | Web UI, Lua | Blackbox viewer | configurator|             |                | Explorer, pyulog
 build / CI     | CMake       | PlatformIO  | make (BF/INAV), | Keil/make  | CMake, meson | colcon, Yocto  | qmake/CMake
                |             |             | CMake (PX4),    | [확인 필요]|              |                |
                |             |             | waf (ArduPilot) |            |              |                |
```

그림 안 셀 내용 중 "bootloader" 줄은 보드마다 차이가 커서 대표 예다. ELRS의 PlatformIO 빌드는 `src/platformio.ini` 존재로 확인 [확인됨 소스]. AM32는 README가 Keil 프로젝트 빌드와 bootloader 선설치(ST-LINK 등)를 설명한다 [확인됨]. ArduPilot의 waf, PX4의 CMake는 일반 지식 — R04에서 확인.

### 8.2 오픈소스 표 (라이선스는 GitHub API `license.spdx_id` + 저장소 문서로 확인)

| 프로젝트 | 어디서 도나 | 언어 | 라이선스 (확인) | Neros JD 언급 (K05) | FTE 관련성 |
|---|---|---|---|---|---|
| Betaflight | FC (STM32/AT32 등) | C | **GPL-3.0** | 13 | ⭐⭐⭐ 온사이트 직결, 사내 포크 추정 |
| Betaflight Configurator | PC (웹/앱) | JavaScript | GPL-3.0 | (Betaflight에 포함) | MSP 사용 예시 교재 |
| Blackbox Log Viewer | PC | JavaScript | GPL-3.0 | (Betaflight에 포함) | 로그 = 테스트 아티팩트 |
| INAV | FC | C | GPL-3.0 | 2 | ⭐ 내비게이션 비교 |
| PX4-Autopilot | FC (Pixhawk 계열) | C/C++ | **BSD-3-Clause** | 10 | ⭐⭐ SITL/HITL · MAVSDK 테스트 |
| NuttX | PX4 아래 RTOS | C | Apache-2.0 | (PX4에 포함) | RTOS 동작 이해 |
| MAVSDK | PC / companion | C++ (+ 바인딩) | BSD-3-Clause | — | 통합 테스트 API |
| ArduPilot | FC, Linux 보드 | C++ | **GPL-3.0** | 10 | ⭐⭐ `Tools/autotest` 교재 |
| ChibiOS | ArduPilot 아래 RTOS | C | 저장소 라이선스 GPL-3.0, **HAL 파일 헤더는 Apache-2.0** (RT 커널은 GPL-3.0 / 상용 이중 라이선스로 알려짐 [확인 필요]) | (ArduPilot에 포함) | RTOS |
| Mission Planner | Windows PC | C# | GPL-3.0 | 1 | GCS |
| ExpressLRS | TX 모듈, RX | C/C++ | **GPL-3.0** | 6 | ⭐⭐⭐ latency · RF · 패킷 |
| EdgeTX | Handset (STM32) | C/C++ | **GPL-2.0** | 1 | ⭐⭐ latency 첫 구간 |
| MAVLink (XML 정의 + 생성된 C 라이브러리) | 어디든 (FC, companion, GCS) | XML / C 헤더 | **MIT** (정의 XML · 생성 코드) | 4 | ⭐⭐ 테스트 자동화 통로 |
| MAVLink generator (mavgen 툴체인) | 빌드 PC | Python | **LGPL-3.0** | (MAVLink에 포함) | — |
| pymavlink | 테스트 PC | Python | **LGPL-3.0 이상**, 단 mavgen이 생성한 코드는 MIT | (MAVLink에 포함) | ⭐⭐ 테스트 스크립트 |
| QGroundControl | GCS PC/태블릿 | C++/Qt | **Apache-2.0 / GPL-3.0 이중** (Apache 쪽은 상용 Qt 라이선스 필요) | 2 | ⭐ GCS 테스트 |
| ATAK-CIV | Android | Java/C++ | GPL-3.0 (저장소 archived) | 3 (ATAK/TAK) | ⭐ 지상 연동 |
| Bluejay | 8-bit ESC | Assembly | GPL-3.0 | 0 (공고엔 "ESC software"만) | ⭐ ESC 회귀 |
| AM32 | 32-bit ESC | C | GPL-3.0 | 0 | ⭐ ESC 회귀 |
| OpenHD | Linux SBC (air/ground) | C++ | GPL-3.0 | 1 | ⭐ 영상 · 텔레메트리 |
| WFB-ng | Linux SBC | C/C++/Python | GPL-3.0 | (OpenHD와 함께) | ⭐ 영상 링크 |
| GStreamer | Linux | C | LGPL [확인 필요: 플러그인별 상이] | 3 | ⭐ 영상 파이프라인 |
| Micro XRCE-DDS Client | FC (PX4) | C | Apache-2.0 | (DDS 2) | PX4 ↔ ROS 2 |
| ROS 2 | companion | C++/Python | Apache-2.0 [확인 필요: 패키지별] | 2 | — |
| Zephyr | MCU 보드 | C | Apache-2.0 | 1 | ⭐ RTOS |
| FreeRTOS-Kernel | MCU 보드 | C | MIT | 1 | ⭐ RTOS |
| Yocto / Buildroot / U-Boot | Linux 보드 | 다양 | 다양 (U-Boot · Buildroot GPL-2.0 [확인 필요]) | 5 · 5 · 1 | ⭐ (EmbeddedLinux 노트) |

"Neros JD 언급" 숫자는 K05 표(공고 109개 스캔)를 옮긴 것이다. 0은 이번 grep에서 이름이 안 나왔다는 뜻 — ESC 펌웨어는 "Betaflight and ESC software"처럼 일반명으로만 나온다.

> ⚠️ Gemini 정정: Gemini 표의 "MAVLink: 헤더 끝에 CRC-16"은 맞지만, 라이선스 관점이 빠졌다. **MAVLink는 생성된 C 헤더가 MIT라서 closed-source 앱에 써도 된다** (MAVLink 공식 문서 문구: "can therefore be used in any closed-source application without publishing the source code"). 반면 테스트 스크립트에 쓰는 pymavlink는 LGPL-3.0이다. 사내 배포 툴에 pymavlink를 넣을 때는 LGPL 조건을 본다.

### 8.3 Neros 맥락에서의 배치 [추정]

- FPV 기체(수동, Archer 계열 [공고에 "Archer platform"만 확인]) → FC는 **Betaflight 기반 사내 포크**, RC 링크는 **ELRS 기반 또는 사내 radio link 코드**. 근거: Flight Software 공고의 "Betaflight internals — modes, RC/mixer path, or a maintained fork", Senior FW 공고의 "flight control and radio link code" [확인됨 공고 문구, 해석은 추정].
- 자율 기능 · 상태 추정 · companion 연동 → PX4 생태계 병행. 근거: "Port and tune sensors and control algorithms across flight stacks (Betaflight, PX4)" [확인됨 공고].
- 지상 → 조종기 펌웨어(EdgeTX 계열 경험 요구), GCS(QGC/TAK 계열), 자체 디지털 영상 (CODEC 공고).
- FTE는 이 모든 것의 **릴리스 전 빌드 안정성**을 본다 [확인됨 공고: K05의 FTE 공고 요약 "릴리스 전 빌드 안정성"].

---

## 9. 라이선스가 방산 회사에 주는 의미 [추정 · 법률 조언 아님]

이 절은 공부용 정리다. 실제 판단은 회사 법무가 한다. 여기서는 "엔지니어가 알아야 할 질문"까지만 쓴다.

### 9.1 GPL이 실제로 요구하는 것

| 질문 | GPL-2.0 / GPL-3.0의 일반적 해석 |
|---|---|
| 포크를 사내에서만 쓰면? | **배포(distribution / conveying)가 없으면 소스 공개 의무가 생기지 않는다**. 사내 벤치 · 사내 비행 테스트는 배포가 아니라는 게 일반적 해석 |
| 펌웨어가 든 드론을 고객에게 넘기면? | 바이너리를 넘기는 것 = 배포. **그 바이너리를 받은 사람(recipient)에게** 대응 소스(Corresponding Source)를 제공하거나 제공 약속(written offer)을 해야 한다 |
| 공개 대상은 "전 세계"인가? | 아니다. 의무는 **받은 사람**에 대한 것이다. 다만 받은 사람도 GPL에 따라 재배포할 권리가 있다 |
| 추가 제한(재배포 금지 계약)을 붙일 수 있나? | GPL은 추가 제한을 금지한다. 그래서 "바이너리는 주되 소스 재배포는 금지" 같은 조건이 충돌한다 |
| GPL-3.0의 "Installation Information" | "User Product"(소비자 제품 등)에 대해 수정 펌웨어 설치 방법까지 요구한다. 군용 납품 기체가 User Product인지는 해석 문제 [추정] |
| 수출 통제와 충돌하면? | GPL-3.0 12조는 "다른 의무 때문에 GPL 조건을 지킬 수 없으면 배포하지 말라"는 취지다. 수출 통제(ITAR/EAR) 대상 수정본은 이 지점에서 복잡해진다 [추정] |

> ⚠️ Gemini 정정: Gemini는 ArduPilot GPLv3에 대해 "회사가 추가로 개발한 소스 코드 **모두에게 무료로 공개**해야 하는 강력한 전염성 의무"라고 썼다. 정확하지 않다. (1) 의무는 **배포할 때만** 생긴다. (2) 대상은 **바이너리를 받은 사람**이다. (3) 같은 프로그램으로 결합된 부분에 적용되며, 별도 프로그램(예: 별도 프로세스로 통신하는 companion 앱)은 일반적으로 따로 본다. "모두에게"는 과장이다.

### 9.2 "PX4는 BSD라서 closed-source가 가능하다"는 Gemini 주장

- 사실 부분: PX4-Autopilot은 BSD-3-Clause, 그 아래 NuttX는 Apache-2.0, MAVSDK는 BSD-3-Clause, uXRCE-DDS는 Apache-2.0 [확인됨 GitHub]. **permissive 라이선스라 수정본을 공개하지 않고 배포해도 된다** (저작권 고지 등은 지켜야 함).
- 추정 부분: "그래서 방산 기업이 PX4를 선호한다"는 Gemini의 결론은 **Neros의 실제 선택 이유로 확인된 게 아니다** [추정]. 공고는 Betaflight와 PX4를 둘 다 쓴다.
- 잊기 쉬운 사실: **Betaflight는 GPL-3.0이다** [확인됨 GitHub]. 공고가 "maintained fork"를 말하므로, Betaflight 포크가 든 기체를 외부에 넘기면 GPL 의무가 그 포크에 걸린다 [추정]. ELRS(GPL-3.0), EdgeTX(GPL-2.0), Bluejay/AM32(GPL-3.0)도 같다. 즉 FPV 스택은 **거의 전부 GPL**이다.
- 따라서 "PX4 = closed 가능, Betaflight = closed 불가"라는 이분법은 반만 맞다. 실제로는 **(a) 무엇을 누구에게 배포하나, (b) 독점 코드를 GPL 코드와 같은 바이너리에 넣나, 별도 보드 · 별도 프로세스로 분리하나**가 갈림길이다 [추정].

### 9.3 FTE가 챙길 수 있는 것 (엔지니어링 관점)

| 항목 | 왜 | FTE가 할 수 있는 것 |
|---|---|---|
| SBOM (Software Bill of Materials) | 어떤 오픈소스 · 어떤 버전 · 어떤 라이선스가 바이너리에 들었나 | 빌드 산출물마다 SBOM 생성 · 보관을 CI에 넣기 |
| 소스-바이너리 대응 | GPL "Corresponding Source"를 나중에 정확히 재현해야 할 수 있다 | **재현 가능한 빌드(reproducible build)** 확인 테스트, 빌드 해시 기록 (Bazel이 강점인 부분 — [R09](2026-10-24_R09_scalable_reusable_test_platform.md)) |
| 라이선스 경계 | 독점 코드가 GPL 바이너리에 섞이는지 | 의존성 그래프 검사 [추정: 회사 정책 따라] |
| 수출 통제 표시 | 수정본의 통제 여부 | 법무 결정을 따르고, 아티팩트 저장소 접근 제어 |

---

## 10. SSD 검증에서는 이랬다 → 드론에서는 이렇다

Don의 경험 범위 안에서만 비교한다 (NOTES_SPEC 기준).

| 주제 | SSD 검증에서는 이랬다 | 드론에서는 이렇다 |
|---|---|---|
| 시스템 경계 | host ↔ PCIe ↔ controller FW ↔ NAND. 보드는 사실상 한 장(eSSD) + host | 보드 7~10장(조종기, TX, RX, FC, ESC, VTX, GPS, companion, GCS)이 각자 펌웨어. **경계가 훨씬 많다** |
| 주 버스 | PCIe 3/4/5 커버리지, NVMe 명령, UART 테스트 시퀀스 | UART(CRSF/MSP/MAVLink/GPS), SPI(IMU/flash/OSD), I2C, 단선 DShot, RF |
| 무결성 | NVMe/PCIe 레이어의 오류 보고 체계 | 프로토콜마다 다른 CRC (CRC-8 DVB-S2, 4-bit XOR, CRC-16/MCRF4XX, SBUS는 거의 없음) — **버스마다 깨짐 감지 능력이 다르다** |
| 텔레메트리 | NVMe telemetry 디버그 기능 — host가 log page로 읽음 | CRSF/MAVLink 텔레메트리는 **RF 대역을 나눠 쓰는** 저율 스트림. Blackbox는 기체 안 flash에 고율 로그 |
| 시간 | 명령 latency (I/O 경로) | 제어 latency = RF 슬롯 대기 + UART + 스케줄러 + 필터 + 기계 응답. **사람이 체감**한다 |
| 환경 스트레스 | 챔버 온도 제어 · 스케줄링 · stress로 latent defect 발굴 | 진동 · RF 감쇠/간섭 · 전압 sag · GPS 저하. 챔버 대신 **감쇠기 · 전원 공급기 · 추력 스탠드** |
| 전원 | eSSD 상태 모니터링, power 관련 테스트 | LiPo 셀 수, BEC 레일, brown-out이 곧 failsafe 이벤트 |
| 펌웨어 종류 | Cortex-R/M · Xtensa FW, 자사 코드 | 대부분 **오픈소스 포크** (GPL 다수). upstream 변경을 따라가는 회귀가 일 |
| 실패의 결과 | 데이터 손실 · 가용성 | 기체 손실 · 안전 · 임무 실패 |
| 공장 | factory test-node 아키텍처 (최근 RF 칩셋 쪽) | 보드별 bring-up + 링크 binding + 센서 보정 + ESC 버전 확인이 공장 테스트 항목 [추정] |
| 계측 | DSO · protocol analyzer로 PCIe/I2C/SPMI/RFFE 루트코즈 | 로직 분석기(CRSF/DShot 디코드), 오실로스코프(레일), RF 감쇠기, SWD/RTT — **같은 기술, 다른 프로토콜** |

한 줄로: Don이 SSD에서 해 온 "경로를 쪼개고, stress를 주입하고, 오류 보고를 신뢰할 수 있게 만드는" 일은 그대로 옮겨진다. 새로 배울 것은 **RF 구간, 오픈소스 포크 관리, 기계 · 사람이 들어간 루프**다.

---

## 11. 테스트 아이디어 체크리스트

### 11.1 시스템 지도 기반

- [ ] 기체 구성표(configuration manifest): 보드별 하드웨어 리비전 · MCU · 펌웨어 이름/버전/커밋 · 설정 해시를 한 파일로. 테스트 결과마다 첨부
- [ ] 보드 사이 모든 링크에 probe point(테스트 패드 · 커넥터 분기) 목록 만들기
- [ ] 버스별 "정상 프레임 레이트" 기대값 표 (CRSF 프레임/초 = packet rate, DShot 프레임/초 = PID rate 등)와 자동 비교
- [ ] 모든 로직 분석기 캡처에 같은 timebase, 테스트 ID, 펌웨어 버전 메타데이터

### 11.2 Control uplink

- [ ] CRSF 주입 (USB-UART) → DShot 변화까지 지연 분포, packet rate별
- [ ] (PID rate × DShot rate × bidir on/off) 매트릭스에서 루프 주기 지터 · DShot 에러율
- [ ] rc smoothing 설정별 stick → setpoint 지연 (Blackbox로)
- [ ] 깨진 CRSF 스트림 (CRC 오류 / 잘린 프레임 / 가짜 0xC8 / len 상한 초과) → 프레임 drop만 되고 채널 튐 없음
- [ ] 링크 손실 → failsafe 단계 진입 시간, 복구 시 동작

### 11.3 Telemetry · 영상 · autonomy

- [ ] telemetry ratio별 텔레메트리 갱신 주기 분포 (EdgeTX 로그 / CRSF 캡처)
- [ ] RF 감쇠 sweep 중 LQ · RSSI 보고값과 실제 손실률 비교 (보고값을 믿을 수 있나)
- [ ] glass-to-glass 반복 측정 (LED + 포토다이오드), 손실 주입 시 화면 동작 분류
- [ ] companion 최대 트래픽에서 FC 루프 지터 (Autonomy 공고 "without degrading control loop timing")
- [ ] companion heartbeat 중단 → FC가 정의된 동작으로 가는가
- [ ] companion RC override와 조종기 입력이 동시에 올 때 우선순위 (Betaflight 경로)

### 11.4 전원 · bring-up

- [ ] 레일별 기대 전압 · 허용 범위 표로 bring-up 자동 판정
- [ ] VBAT 천천히 ramp-down → 경고 임계값 · 동작 확인
- [ ] 5 V 순간 drop → RX 재부팅 → failsafe 기록
- [ ] Jetson 부팅 피크 중 다른 레일 동시 캡처

### 11.5 오픈소스 · 릴리스

- [ ] upstream(Betaflight/ELRS/ESC 펌웨어) 새 버전 → 사내 포크 머지 후 회귀 스위트 자동 실행
- [ ] 빌드 재현성 (같은 커밋 → 같은 해시)
- [ ] SBOM 생성 · 라이선스 목록 자동 수집
- [ ] ESC 펌웨어 버전 확인을 공장 테스트 · 비행 전 점검에 포함 [추정]

---

## 12. 직접 해 볼 것

하드웨어가 필요한 실습은 **사기 전에 Don과 예산 확인**. 프롭 장착 모터 회전, LiPo 충전은 위험 작업이라 프롭 제거 · 전류 제한 전원 · 방화 백을 기본으로 한다.

| # | 할 것 | 필요한 것 | 산출물 | 연결 단원 |
|---|---|---|---|---|
| 1 | 4.3 계산 스크립트를 `code/`에 넣고 baud · rate 표를 자동 생성 | 노트북 | 표 + 자체 테스트 | 이 단원 |
| 2 | Betaflight 소스에서 이 단원에 쓴 경로를 직접 열어 보기: `src/main/rx/crsf.c`, `crsf_protocol.h`, `rx/mavlink.c`, `rx/msp_override.c`, `drivers/dshot.h`, `drivers/dshot_bitbang_decode.c`, `src/platform/` | 노트북 | "이 단원 확인 필요 항목" 몇 개를 확인됨으로 | [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) |
| 3 | ELRS `src/src/common.cpp` rate 표를 읽고 rate별 interval · telemetry ratio · 칩 표 만들기 | 노트북 | 표 | [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) |
| 4 | EdgeTX `radio/src/pulses/crossfire.cpp`, `mixer_scheduler.cpp` 읽고 H2~H4 동작 정리 | 노트북 | 1쪽 메모 | [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) |
| 5 | 이 단원 6.1 그림대로 로직 분석기 측정 (RF 없이 USB-UART → FC → DShot) | FC, USB-UART, 로직 분석기, 전류 제한 전원 — **사기 전에 Don과 예산 확인** | 지연 히스토그램 | [R08](2026-10-24_R08_hil_bench_design_instruments.md) |
| 6 | 같은 측정에 ELRS TX/RX 추가, packet rate 바꿔 분포 비교 | ELRS TX · RX — **예산 확인** | rate별 분포 그림 | [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) |
| 7 | 자기 기체(가상) 구성표 템플릿 만들기 (11.1 첫 항목) | 노트북 | YAML/JSON 템플릿 | [R09](2026-10-24_R09_scalable_reusable_test_platform.md) |
| 8 | 9장 라이선스 표를 "우리 기체에 무엇이 들어가나" 형태 SBOM 연습으로 바꾸기 | 노트북 | SBOM 예시 | [R09](2026-10-24_R09_scalable_reusable_test_platform.md) |

---

## 13. 확인 안 된 것 (다음에 소스 · 문서로 확인)

- [ ] BMI270의 Betaflight gyro rate (3.2 kHz로 알려짐) — Betaflight 문서/드라이버
- [ ] bidirectional DShot 응답 전 전환 gap 시간, 그리고 PID rate별 권장 DShot rate
- [ ] ESC serial telemetry의 baud · 프레임 길이 · CRC (KISS 텔레메트리 형식)
- [ ] SmartAudio / Tramp baud · 프레임
- [ ] MSP DisplayPort 기본 baud, HDZero 펌웨어 공개 범위
- [ ] ELRS의 handset 타이밍 동기 기능 이름과 동작 (H4 대기를 줄이는 부분), rate별 air time
- [ ] ELRS handset 쪽 5.25M baud가 공식 옵션인지
- [ ] JR bay 외부 모듈 CRSF의 물리 계층 (한 선 half-duplex, 극성)
- [ ] Betaflight MAVLink serial RX(`USE_SERIALRX_MAVLINK`)가 들어간 첫 릴리스와 기본 빌드 포함 여부
- [ ] EdgeTX 지원 기종별 MCU (F4 vs H7)
- [ ] ChibiOS RT 커널 라이선스 세부 (GPL-3.0 / 상용 이중) — ArduPilot 배포 시 의미
- [ ] GStreamer, ROS 2, U-Boot, Buildroot 라이선스 세부
- [ ] 현재 TAK 오픈소스 개발 위치 (ATAK-CIV 저장소는 archived)
- [ ] MAVLink `TIMESYNC` · PTP를 PX4/Betaflight가 어디까지 지원하나
- [ ] Neros 기체의 실제 보드 구성, Betaflight 포크 여부, radio link가 ELRS 기반인지 사내 독자인지 — 공고 외 정보 없음 [추정]
- [ ] Gemini가 말한 "Crossbow GCS", "APEX" — [Gemini 주장 · 미확인], 공고에 없음

---

## 14. 참고 링크

소스 (이 단원 작성 중 직접 열어 확인):

- Betaflight 저장소 (GPL-3.0): https://github.com/betaflight/betaflight
- Betaflight `src/main/rx/crsf_protocol.h` (CRSF 420000 / 416666, 0xC8, RC payload 22): https://github.com/betaflight/betaflight/blob/master/src/main/rx/crsf_protocol.h
- Betaflight `src/main/rx/crsf.c` (frame time 주석, crc8_dvb_s2 / crc8_poly_0xba): https://github.com/betaflight/betaflight/blob/master/src/main/rx/crsf.c
- Betaflight `src/main/rx/mavlink.c` (MAVLink serial RX, RC_CHANNELS_OVERRIDE): https://github.com/betaflight/betaflight/blob/master/src/main/rx/mavlink.c
- Betaflight `src/main/drivers/dshot.h`, `dshot_bitbang_decode.c`, `src/platform/STM32/dshot_bitbang.c`: https://github.com/betaflight/betaflight/tree/master/src/main/drivers
- Betaflight `src/main/drivers/motor_types.h` (DSHOT1200 removed): https://github.com/betaflight/betaflight/blob/master/src/main/drivers/motor_types.h
- Betaflight 2025.12 Release Notes: https://betaflight.com/docs/wiki/release/Betaflight-2025-12-Release-Notes
- Betaflight Position Hold 2025.12 가이드: https://betaflight.com/docs/wiki/guides/current/Position-Hold-2025-12
- Betaflight ESC Firmware 문서: https://betaflight.com/docs/wiki/getting-started/hardware/esc-firmware
- ExpressLRS 저장소 (GPL-3.0): https://github.com/ExpressLRS/ExpressLRS
- ExpressLRS `src/src/common.cpp` (rate 표): https://github.com/ExpressLRS/ExpressLRS/blob/master/src/src/common.cpp
- ExpressLRS 2.0.1 릴리스 공지 (Mega Baud, RX 임의 baud): https://intofpv.com/printthread.php?tid=19275
- ExpressLRS Radio Preparation 문서: https://www.expresslrs.org/quick-start/transmitters/tx-prep/
- EdgeTX 저장소 (GPL-2.0): https://github.com/EdgeTX/edgetx
- MAVLink 개발자 가이드 License 절: https://mavlink.io/en/#license
- pymavlink README (LGPL-3.0, 생성 코드 MIT): https://github.com/ArduPilot/pymavlink
- QGroundControl 라이선스 (Apache-2.0 / GPL-3.0 이중): https://github.com/mavlink/qgroundcontrol/blob/master/.github/COPYING.md
- PX4-Autopilot (BSD-3-Clause): https://github.com/PX4/PX4-Autopilot
- ArduPilot (GPL-3.0): https://github.com/ArduPilot/ardupilot
- INAV (GPL-3.0): https://github.com/iNavFlight/inav
- ChibiOS (저장소 GPL-3.0, HAL 헤더 Apache-2.0): https://github.com/ChibiOS/ChibiOS
- Apache NuttX (Apache-2.0): https://github.com/apache/nuttx
- MAVSDK (BSD-3-Clause): https://github.com/mavlink/MAVSDK
- Micro XRCE-DDS Client (Apache-2.0): https://github.com/eProsima/Micro-XRCE-DDS-Client
- Bluejay (GPL-3.0, EFM8 BB21/BB51): https://github.com/bird-sanctuary/bluejay
- AM32 (GPL-3.0, 지원 MCU 목록): https://github.com/am32-firmware/AM32
- OpenHD (GPL-3.0): https://github.com/OpenHD/OpenHD
- WFB-ng (GPL-3.0): https://github.com/svpcom/wfb-ng
- ATAK-CIV (GPL-3.0, archived): https://github.com/deptofdefense/AndroidTacticalAssaultKit-CIV
- Zephyr (Apache-2.0): https://github.com/zephyrproject-rtos/zephyr
- FreeRTOS-Kernel (MIT): https://github.com/FreeRTOS/FreeRTOS-Kernel
- Mission Planner (GPL-3.0): https://github.com/ArduPilot/MissionPlanner

기사 · 2차 자료:

- Hackaday, "The End Of BLHeli_32: Long Live AM32?" (2024-06): https://hackaday.com/tag/blheli/
- Oscar Liang, "End of BLHeli_32": https://oscarliang.com/end-of-blheli_32
- Oscar Liang, Betaflight 2025.12 (4.6) 기능: https://oscarliang.com/betaflight-4-6/
- Oscar Liang, Position Hold / Altitude Hold 설정: https://oscarliang.com/betaflight-position-hold/
- Bidirectional DShot 프레임 구조 (eRPM 12 bit + CRC 4, GCR, 극성 반전) — Bitcraze 블로그: https://www.bitcraze.io/?p=14862
- Apache NuttX DShot 드라이버 문서: https://nuttx.apache.org/docs/latest/components/drivers/character/timers/dshot.html

사내 자료:

- 공고 스냅샷: `../research/job_board_snapshots/2026-10-23_neros_jobs.json`
- K05 오픈소스 지도: `../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md`
- Gemini 대화 원본 (검증 대상): `/Users/donh/Downloads/Neros 오픈소스 및 테스트 분석_prompt_gemini.pdf`

다른 단원:

- [R00 개요 · Gemini 정정 목록](2026-10-24_R00_overview_and_gemini_corrections.md)
- [R01 FTE vs SSD 검증](2026-10-24_R01_fte_vs_ssd_validation.md)
- [R03 Betaflight / INAV 내부](2026-10-24_R03_betaflight_inav_internals_and_testing.md)
- [R04 PX4 / ArduPilot 테스트 생태계](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)
- [R05 RC 링크 프로토콜](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)
- [R06 Companion · 영상 · Linux · RTOS](2026-10-24_R06_companion_video_linux_rtos.md)
- [R07 테스트 방법론 피라미드](2026-10-24_R07_test_methodology_pyramid.md)
- [R08 HIL 벤치 설계](2026-10-24_R08_hil_bench_design_instruments.md)
- [R09 스케일러블 테스트 플랫폼](2026-10-24_R09_scalable_reusable_test_platform.md)
- [R10 로드맵 · 랩 · 참고 자료](2026-10-24_R10_roadmap_labs_references.md)

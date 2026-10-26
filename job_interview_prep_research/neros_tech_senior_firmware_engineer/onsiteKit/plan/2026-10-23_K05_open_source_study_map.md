# 🧭 K05 Neros가 쓰는 오픈소스 지도 + 앞으로 해 볼 것

> 2026-10-23. 온사이트(10-22) 뒤 결과 대기 중. Neros 공고 **109개 전부**(Greenhouse API, `research/job_board_snapshots/2026-10-23_neros_jobs.json`)에서 오픈소스 · 도구 이름을 뽑고, 온사이트에서 실제로 나온 질문(Michael: **Betaflight 최우선 task · ground → TX → RX → FC latency**, Jon: **패킷 문제**, Adam: **보드 테스트**)에 맞춰 공부 순서를 정했다. 공고 문장은 [확인됨 공고], 프로젝트 내부 설명은 내가 아는 내용이며 **코드에서 직접 확인할 것**은 [확인 필요]로 표시.

## 1. 한눈에 — 공고에 나온 오픈소스 (언급된 공고 수)

| 분야 | 오픈소스 | 공고 수 | 대표 공고 | 이 자리(FW Test)와의 거리 |
|---|---|---|---|---|
| **비행 제어 (FC)** | **Betaflight** | **13** | **Firmware Test Engineer**, Firmware Engineer – Flight Software, FPV Test Pilot/Technician | ⭐⭐⭐ 온사이트에서 직접 나옴 |
| | **PX4** (+ NuttX · uORB · EKF2) | 10 | **Firmware Test Engineer**, Flight Software, Flight Software Manager, Autonomy Runtime | ⭐⭐ |
| | **ArduPilot** | 10 | **Firmware Test Engineer**, Senior FW, State Estimation, Integration Lead | ⭐⭐ (자동 테스트 프레임워크가 좋은 교재) |
| | INAV | 2 | UAV Systems Engineer, Drone Support | ⭐ (GPS 위치 유지 — Flight SW "position/altitude hold") |
| **RC 링크** | **ExpressLRS (ELRS)** | 6 | **Firmware Test Engineer**, Senior FW, Ground Software, Integration | ⭐⭐⭐ latency 질문의 TX → RX 구간 |
| | CRSF (프로토콜) · SBUS | 1+ | UAV Systems Engineer | ⭐⭐⭐ RX → FC 구간, 패킷 파서 연습 |
| **조종기 (ground)** | EdgeTX / OpenTX | 1 | Senior Firmware Engineer, Ground Software | ⭐⭐ latency의 ground 구간 |
| **통신 프로토콜** | MAVLink | 4 | Flight Software, Autonomy Runtime, Platform | ⭐⭐ (PX4/ArduPilot 테스트 자동화 통로) |
| **지상 소프트웨어** | QGroundControl · Mission Planner · ATAK/TAK | 2 · 1 · 3 | Ground Software Manager, Senior SWE | ⭐ |
| **영상** | GStreamer · OpenHD · RTSP/WebRTC | 3 · 1 · 1 | Video CODEC, Ground SW Manager, UAV Systems | ⭐ |
| **임베디드 Linux** | Yocto · Buildroot · U-Boot · Mender/RAUC/SWUpdate · PREEMPT_RT | 5 · 5 · 1 · 1 | Embedded Linux, Platform, Ground SW | ⭐ (EmbeddedLinux 노트에서 이미 다룸) |
| **RTOS** | FreeRTOS · Zephyr (+ PX4의 NuttX, ArduPilot의 ChibiOS) | 1 · 1 | Firmware Engineer – Tennessee | ⭐⭐ |
| **자율 · 로보틱스** | ROS 2 · DDS · TensorRT · Jetson | 2 · 2 · 2 · 2 | Autonomy Runtime, Perception | — |
| **빌드 · CI** | Bazel · CMake · Jenkins · GitLab CI · GitHub Actions | 4 · — · 2 · 2 · 1 | **Firmware Test Engineer**, Platform, Full-Stack | ⭐⭐⭐ JD 필수/우대 |
| **데이터 · 대시보드** | InfluxDB · Redis · PostgreSQL · Django/Flask | 1씩 | Full-Stack SWE III, Data Platform | ⭐⭐ (테스트 결과 대시보드 — Jon 팀) |

> **Firmware Test Engineer JD 원문** [확인됨 공고, updated 2026-10-06]: 필수 — HIL 테스트 시스템 구축, Python 테스트 자동화, I2C · SPI · UART · Ethernet, **GitLab CI · Jenkins**, Git. 우대 — C/C++, **Bazel** · CMake · make, RF 제품 테스트, **"FPV Drone software including Betaflight, ExpressLRS, PX4, Ardupilot"**. 업무 — drone · ground control 소프트웨어 테스트 스위트, 테스트 프레임워크, CI/CD 연동, 릴리스 전 빌드 안정성.

> **Flight Software JD**가 힌트를 더 준다 [확인됨 공고]: "Port and tune sensors and control algorithms across flight stacks (**Betaflight, PX4**)", "partnering with our Test organization on **SITL/HITL** and flight-test execution", 우대 "**Betaflight internals** — modes, RC/mixer path, or a maintained fork", "PX4 ecosystem — MAVLink, uORB, EKF2, … QGroundControl". → **Betaflight를 기반으로 한 자체 포크 + PX4 병행**, 그리고 **Test 조직이 SITL/HITL을 맡는다** [추정].

## 2. 신호 하나가 지나가는 길 — Michael의 latency 질문을 그림으로

```text
 [조종기 stick]──ADC──[EdgeTX mixer]──CRSF/UART──[ELRS TX module]
        ~~ 2.4 GHz / 900 MHz RF (packet rate: 50~1000 Hz) ~~
 [ELRS RX]──CRSF @420000 baud──[FC: Betaflight RX task → rc smoothing → PID loop]──DShot──[ESC]──[motor]
        영상: [camera]──[VTX]~~RF~~[goggles / ground]      텔레메트리: FC → CRSF → RX → RF → TX → EdgeTX
```

| 구간 | 지연을 만드는 것 | 측정 방법 |
|---|---|---|
| stick → EdgeTX 출력 | ADC 샘플 주기, mixer 주기, CRSF 출력 주기 | 조종기 CRSF 출력 선을 로직 분석기로, stick 대신 **알려진 계단 입력** (트레이너 포트 / 시뮬레이션) |
| TX → RF → RX | **ELRS packet rate**(예: 500 Hz면 주기 2 ms → 평균 대기 ~1 ms), 패킷 손실 시 다음 패킷까지 | TX 입력 CRSF와 RX 출력 CRSF를 **같은 로직 분석기 두 채널**로 → 같은 채널 값이 나타나는 시각 차 |
| RX → FC | CRSF 프레임 길이 ÷ 420 kbaud (26바이트 ≈ 0.6 ms), FC RX task 주기, **rc smoothing 필터** | CRSF 프레임 끝 ↔ DShot 출력 변화 (FC 안은 GPIO 토글 + DWT로 쪼갬) |
| FC 루프 | gyro/PID 루프 주기(예: 8 kHz → 125 µs) | Betaflight CLI `tasks`(task별 주기 · 실행 시간) [확인 필요: 출력 형식] |
| FC → 모터 | DShot600 프레임 16비트 × 1.67 µs ≈ 27 µs, ESC 처리, 모터 · 프롭 기계 응답 | DShot 선 디코드, bidirectional DShot eRPM으로 실제 회전 변화 |
| 전체 (glass-to-glass 포함) | 위 합 + 영상 | 고속 카메라로 stick과 모터/화면을 같이 촬영, 또는 LED + 포토다이오드 |

> 한 줄 답 (다음에 쓸 것): "I'd break the pipeline at every interface I can probe — the CRSF line out of the radio, the CRSF line into the flight controller, and the DShot line out — capture them on one logic analyzer with a shared timebase, inject a known step, and measure each hop. Inside the flight controller I'd use GPIO toggles or the cycle counter. Then I'd repeat it thousands of times to get a distribution, not one number, because the packet-rate wait is random."

## 3. 오픈소스별 — 무엇을 보고, 테스트 엔지니어로 무엇을 가져오나

### 3.1 Betaflight (최우선)

| 볼 것 | 어디 [확인 필요: 버전마다 경로가 바뀜] | 왜 |
|---|---|---|
| 메인 루프 → 스케줄러 | `src/main/main.c` → `scheduler()` (`src/main/scheduler/scheduler.c`) | 온사이트 질문 그 자체 |
| task 테이블 · 우선순위 | `src/main/fc/tasks.c`의 task 정의 배열, `scheduler.h`의 우선순위 enum · task 구조체 | **gyro · filter · PID** task가 실시간(gyro 동기)으로 돈다 — 다른 task는 남는 시간에 우선순위 + 대기 시간 기준으로 선택 [확인 필요] |
| RC 경로 | `src/main/rx/` (CRSF · SBUS 파서), `rc.c` (rc smoothing), mixer | Flight SW JD "RC/mixer path" |
| 모터 출력 | `drivers/dshot*`, bidirectional DShot | latency 끝단 |
| 블랙박스 로그 | `blackbox/` + Blackbox Explorer | 테스트 아티팩트 · 회귀 비교 |
| **MSP** (MultiWii Serial Protocol) | `msp/` | Configurator가 쓰는 명령 프로토콜 → **Python으로 FC를 자동 조작하는 통로** (HIL 테스트의 손잡이) |
| CLI | USB VCP: `status`(CPU load), `tasks`, `diff all` | 설정을 코드처럼 관리, 테스트 전후 상태 덤프 |
| **유닛 테스트** | `src/test/unit/` (googletest), `make test` | 이미 있는 테스트 문화 — 무엇이 테스트되고 무엇이 안 되나 |
| **SITL** | `make TARGET=SITL` (시뮬레이터와 UDP로 연결) | Flight SW JD "SITL/HITL" — 하드웨어 없이 CI에서 비행 로직 테스트 |

### 3.2 ExpressLRS (latency · RF · 패킷)

- 구조: TX 모듈 · RX 펌웨어 (ESP32/ESP8285 + Semtech SX127x/SX128x 라디오, 일부 STM32), PlatformIO 빌드, Web UI · Lua 설정
- 볼 것: **packet rate와 telemetry ratio**, FHSS(주파수 도약) 타이밍, TX ↔ RX 동기, CRSF 출력
- 테스트 관점: 패킷 손실률 · link quality(LQ) · RSSI를 로그로, 거리/감쇠기로 링크 마진 측정 (JD "Experience testing RF products"와 연결)

### 3.3 CRSF · SBUS · MSP · MAVLink · DShot — 프레임 형식 (Jon 패킷 문제 연습 재료)

| 프로토콜 | 프레임 | 검사 | 연습 |
|---|---|---|---|
| **CRSF** | `[addr][len][type][payload…][crc]`, len = type + payload + crc 바이트 수, 보통 420000 baud | **CRC-8 poly 0xD5**(DVB-S2), type부터 payload까지 | **T2 파서를 CRSF로 바꾸기** — sync 대신 addr, 길이 상한 · resync · CRC |
| SBUS | 25바이트, 100000 baud 8E2 **반전된 UART**, 16채널 × 11비트 비트 패킹 | 끝 바이트 · flag (failsafe 비트) | 11비트 언패킹 (비트 연산) |
| MSP v1 / v2 | `$M<` size cmd payload xor / `$X<` flag cmd(16) size(16) payload crc8 | v1 XOR, v2 CRC-8 DVB-S2 | FC와 실제로 대화하는 Python 클라이언트 |
| MAVLink v2 | `0xFD` len incompat compat seq sysid compid msgid(24) payload crc16 (+ 서명) | CRC-16/MCRF4XX + CRC_EXTRA | pymavlink를 정답(oracle)으로 내 파서 검증 |
| DShot | 16비트: 스로틀 11 + telemetry 요청 1 + CRC 4 | 4비트 XOR CRC | 로직 분석기 캡처 디코드 |

### 3.4 PX4 · ArduPilot — "잘 만든 테스트 시스템"의 교재

| | PX4 | ArduPilot |
|---|---|---|
| RTOS | NuttX | ChibiOS (STM32) |
| 내부 통신 | uORB (publish/subscribe) | 라이브러리 + 스케줄러 |
| 시뮬레이션 | SITL + Gazebo, HITL | SITL (`sim_vehicle.py`) |
| **자동 테스트** | MAVSDK 기반 통합 테스트 | **`Tools/autotest`** — SITL에 실제 미션을 날리고 pymavlink로 판정 |
| 로그 | ULog → pyulog · Flight Review | DataFlash → MAVExplorer |
| 지상국 | QGroundControl | Mission Planner · MAVProxy |

> 가져올 것: "시뮬레이터에 비행을 시키고, 텔레메트리로 **판정을 자동화**한다" — Neros의 Betaflight 포크에 같은 구조를 만드는 것이 FW Test 팀이 할 일과 거의 같다 [추정].

### 3.5 그 밖 (필요할 때)

- EdgeTX: 조종기 mixer · Lua 스크립트 · CRSF 출력, companion 시뮬레이터 — latency의 첫 구간
- OpenHD / WFB-ng · GStreamer: 디지털 영상 링크 (Ukraine UAV Systems JD) — glass-to-glass latency
- Yocto/Buildroot · Mender/RAUC: companion computer / ground 장비 (이미 EmbeddedLinux 노트에 있음)
- Bazel: FW Test JD 우대 — 멀티 타깃 빌드 · 테스트 캐시. Betaflight는 make 기반이라 "Bazel로 감싸기"가 작은 실습 주제

## 4. 앞으로 해 볼 것 — 우선순위 (결과 대기 중 + 다음 기회 대비)

| # | 할 것 | 산출물 | 온사이트 질문 연결 | 시간 |
|---|---|---|---|---|
| 1 | **Betaflight 소스 읽기**: main → scheduler → task 테이블 → gyro/PID/RX task, 우선순위와 선택 규칙을 직접 확인 | 1쪽 노트 + 호출 그림 ("최우선 task는 X, 이유 Y") | Michael ① | 3~4시간 |
| 2 | **Betaflight 빌드 + 유닛 테스트 + SITL** (Mac/Lima VM) | `make test` 통과 로그, SITL 실행 | Flight SW JD SITL/HITL | 반나절 |
| 3 | **CRSF 파서 (Python + C)** — T2를 CRSF 형식(CRC-8 0xD5)으로, resync · 길이 상한 · 테스트 | `typealong/t8_crsf.py/.c` + 실제 캡처로 검증 | **Jon 패킷 문제** | 2~3시간 |
| 4 | **MSP Python 클라이언트** — FC(또는 SITL)에 연결해 attitude · status 읽기, pytest로 감싸기 | 작은 HIL 하네스 | FW Test JD "test framework" | 반나절 |
| 5 | **latency 측정 실습** — FC + ELRS RX + 로직 분석기 (프롭 없이), CRSF 주입 → DShot 변화 시간 분포 | 히스토그램 + 측정 방법 문서 | **Michael ②** | 하드웨어 필요 (1일) |
| 6 | ArduPilot `Tools/autotest` · PX4 MAVSDK 테스트 구조 읽기 | "Betaflight 포크에 적용하면" 설계 1쪽 | HIL/CI 설계 | 3시간 |
| 7 | 보드 테스트 답 다듬기 (Adam) — 입력/출력 확인 앞에 무전원 단락 · 전류 제한 · 레일 · 클럭/리셋 넣기 | K02 5 · 6절 업데이트 | Adam | 30분 |
| 8 | (선택) Betaflight 빌드를 GitHub Actions/GitLab CI로 + 유닛 테스트 + 바이너리 크기 회귀 | 공개 저장소 | JD CI/CD | 반나절 |

### 하드웨어가 필요할 때 (5번) — 최소 구성 [추정 가격대]

- Betaflight FC (STM32F405/F7/H7급), ELRS RX (2.4 GHz), ELRS TX 모듈 **또는** USB-UART로 CRSF를 직접 주입
- 로직 분석기 (Saleae 또는 저가 24 MHz + sigrok/PulseView)
- ESC + 모터는 **프롭 없이**, 전류 제한 전원 — 안전 먼저
- 사기 전에 Don 확인 (예산 · 공간)

## 5. 다음 대화에서 할 수 있는 것

- 1번: Betaflight 저장소를 받아서 scheduler · task 테이블을 함께 읽고 노트 작성 (코드 경로 · 우선순위 규칙을 실제 소스로 확인)
- 3번: CRSF 파서를 typealong에 T8로 추가 (Python + C, 테스트 포함)
- 2 · 4번: Lima VM에서 Betaflight 빌드 · `make test` · SITL

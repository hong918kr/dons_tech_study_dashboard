# 🧭 R00 개요 · 읽는 순서 · Gemini 정정 목록

> 드론 Firmware Test Engineering(FTE) 리서치 노트 11단원의 지도. 단원끼리 어떻게 이어지는지, 시간에 맞는 읽기 코스, 한 장 요약, 그리고 출발 자료(Gemini 대화 PDF 22쪽)에서 틀렸거나 과장된 부분을 단원별로 모은 정정 표.

## 1. 이 노트가 답하려는 질문

Don이 던진 질문 다섯 개가 이 노트 전체의 뼈대다.

| # | 질문 | 주로 답하는 단원 |
|---|---|---|
| 1 | FTE는 무슨 일을 하나? 데이터센터 SSD 검증과 무엇이 다른가? 내가 오해하는 건 없나? | [R01](2026-10-24_R01_fte_vs_ssd_validation.md) |
| 2 | Neros가 쓰는 오픈소스는 드론의 어디에서, 어떻게 쓰이나? | [R02](2026-10-24_R02_drone_system_and_open_source_map.md) · [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) · [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · [R06](2026-10-24_R06_companion_video_linux_rtos.md) |
| 3 | 그것들을 어떻게 테스트해야 하나? | 각 단원의 "테스트" 절 + [R07](2026-10-24_R07_test_methodology_pyramid.md) |
| 4 | 테스트 플랫폼 · HIL 벤치로도 할 수 있나? | [R08](2026-10-24_R08_hil_bench_design_instruments.md) |
| 5 | 테스트 인프라를 어떻게 가져가야 스케일되고 재사용되는 플랫폼이 되나? | [R09](2026-10-24_R09_scalable_reusable_test_platform.md) |
| + | 그래서 나는 무엇부터 해 보나? | [R10](2026-10-24_R10_roadmap_labs_references.md) |

## 2. 단원 지도

```text
                 R01  FTE란? (SSD 검증과 비교 · 오해 바로잡기)
                         │
                         ▼
                 R02  드론 시스템 지도 (보드 · 버스 · 오픈소스 · 신호 경로 · 라이선스)
                         │
      ┌──────────────┬───┴──────────┬───────────────────┐
      ▼              ▼              ▼                   ▼
    R03            R04            R05                 R06
 Betaflight     PX4 /         ELRS · EdgeTX ·      Companion · 영상 ·
 / INAV         ArduPilot     CRSF · DShot ·       Linux · RTOS · GCS
 (FPV FC)       (자율 FC +    MSP · MAVLink
                그들의 테스트   (링크 · 프로토콜)
                생태계)
      └──────────────┴───┬──────────┴───────────────────┘
                         ▼
                 R07  테스트 방법론 (피라미드 · 기법 카탈로그 · 오라클 · 템플릿)
                         │
                         ▼
                 R08  HIL 벤치 (계측기 · 배선 · 안전 · 무인 복구)      ← 하드웨어 층
                         │
                         ▼
                 R09  스케일 · 재사용 플랫폼 (capability 모델 · CI · 데이터 · 벤치 운영)  ← 소프트웨어 층
                         │
                         ▼
                 R10  로드맵 · 랩 13개 · 하드웨어 단계 · 참고 자료 · 용어집
```

## 3. 읽기 코스

### 3.1 1시간 — 큰 그림만

1. 이 페이지 4절 "한 장 요약"
2. [R01](2026-10-24_R01_fte_vs_ssd_validation.md) 비교 표와 "흔한 오해" 절
3. [R02](2026-10-24_R02_drone_system_and_open_source_map.md) 보드 배치도와 신호 경로 표
4. [R09](2026-10-24_R09_scalable_reusable_test_platform.md) 원칙과 층 그림

### 3.2 하루 — 테스트하는 사람의 눈

1. R01 → R02 전체
2. [R07](2026-10-24_R07_test_methodology_pyramid.md) 피라미드와 기법 카탈로그 표
3. [R08](2026-10-24_R08_hil_bench_design_instruments.md) 토폴로지 그림과 계측기 표
4. R09 전체
5. 예제 코드 두 개 실행: `code/bench_drivers.py`, `code/platform_sketch.py`

### 3.3 2주 이상 — 소스까지

R10의 8주 계획을 따른다. 소스 읽기는 R03(Betaflight) → R04(ArduPilot autotest · PX4 MAVSDK 테스트) → R05(ELRS) 순서.

## 4. 한 장 요약 — 꼭 가져갈 12가지

1. **FTE는 "판정하는 장치와 소프트웨어"를 만드는 일이다.** SSD 검증의 자동화 · 플랫폼 · 스트레스 감각은 그대로 쓰인다. 새로 익힐 것은 닫힌 루프 제어, 실시간(µs) 타이밍, RF 링크, 그리고 빠른 릴리스 속도다. → R01
2. **가장 큰 차이는 오라클(합격 기준)이다.** SSD는 "쓴 데이터 = 읽은 데이터"처럼 정답이 분명하다. 드론은 제어 시스템이라 step response · 지연 분포 · 허용 오차 · 통계로 판정한다. → R01, R07
3. **드론은 보드 여러 장의 시스템이다.** FC · 무선 RX/TX · ESC · 조종기 · companion · 영상 · GCS가 서로 다른 오픈소스를 돌린다. 버스 하나하나가 테스트 지점이다. → R02
4. **Betaflight는 RTOS가 없는 협조형 scheduler다.** gyro → filter → PID → mixer → DShot 경로가 가장 높은 우선순위다. `src/main/fc/tasks.c`의 `task_attributes[]`, `src/main/scheduler/scheduler.c`, `src/main/fc/core.c`의 `taskMainPidLoop`가 핵심 파일이다. → R03
5. **Betaflight 저장소에 이미 테스트 도구가 있다.** googletest 유닛 테스트 약 90개(`src/test/unit/`, `make test`)와 SITL 끝단 테스트 하네스(`src/test/sitl/sitl_harness.py`, RC는 UDP 9004 · MSP는 TCP 5761)가 있다. 포크 테스트는 이 위에서 시작한다. → R03, R10
6. **PX4와 ArduPilot은 "자기 자신을 테스트하는 법"의 교과서다.** ArduPilot은 `Tools/autotest`에 SITL 위의 Python 시나리오(`wait_*` 헬퍼 · sim-time 타임아웃)가 있다. PX4는 `test/mavsdk_tests`(C++/Catch2, CI는 SIH)를 쓴다. 둘 다 "기체 추상화 + 조건 대기 + 로그 기반 판정" 구조다. → R04
7. **프로토콜은 테스트하기 가장 좋은 경계다.** CRSF · SBUS · DShot · MSP · MAVLink는 프레임이 정해져 있다. 그래서 conformance · fuzz · resync · 타이밍 테스트를 자동화하기 쉽다. → R05
8. **failsafe 시간은 외우지 말고 설정에서 계산한다.** Betaflight 기본값은 `failsafe_delay`=15, 즉 1.5 s의 stage 1이다. Gemini가 말한 "0.5 s 후 모터 0"이 아니다. 판정 기준은 보드의 실제 설정에서 유도해야 한다. → R03, R07, R08
9. **SITL · HITL · HIL 벤치는 서로 다른 것이다.** SITL은 PC 안에서 FW와 물리를 같이 돌린다. PX4 HITL은 진짜 FC에 가짜 센서를 MAVLink로 넣는 방식이다(커뮤니티 지원, 센서 드라이버를 거치지 않음). HIL 벤치는 진짜 보드에 진짜 버스와 계측기를 붙인다. 각각 잡는 버그가 다르다. → R07
10. **HIL 벤치의 핵심은 "사람 없이 돌다가, 망가져도 스스로 복구하는 것"이다.** 전원 · 플래시 · 리셋 · 자극 · 관측 · 측정 · 복구가 모두 Python으로 돌아야 한다. 결과도 PASS / FAIL / INFRA_FAIL로 나눠야 한다. → R08
11. **스케일되는 플랫폼은 WHAT · HOW · WHERE를 가른다.** 테스트 의도, 프로토콜 · 드라이버, 벤치 · 자원을 분리한다. 기체는 상속이 아니라 capability 조합으로 모델링한다. 그러면 기체 · 펌웨어 스택 · 벤치가 늘어도 한 층만 고치면 된다. 같은 테스트를 책상 → CI → nightly → release → 공장 → RMA까지 다시 쓴다. → R09
12. **Don의 SSD 챔버 SDK 경험은 드론 HIL 플랫폼과 거의 같은 모양이다.** 장비 제어, 스케줄링, 상태 모니터링, 시퀀스 자동화, 다른 엔지니어가 쓰는 SDK가 그대로 대응된다. 이것이 가장 강한 연결 고리다. → R01, R09

## 5. Gemini 정정 목록 (단원별로 모음)

출발 자료는 Gemini와의 대화 PDF(2026-10-24, 22쪽)다. 방향은 대체로 맞았다. 하지만 숫자 · 경로 · 단정이 틀린 곳이 많았다. 각 단원 본문에 `> ⚠️ Gemini 정정:` 상자로 표시했고, 여기에는 중요한 것만 모았다.

### 5.1 Neros 회사 · 제품에 대한 단정

| Gemini 주장 | 바로잡은 내용 | 단원 |
|---|---|---|
| 지상국은 "Crossbow GCS (Embedded Linux + Zynq)", 기체는 "Archer / Archer AI", 페이로드는 "APEX" | 공고에서 확인되는 건 "Neros **Archer** platform including flight control and radio link code" 한 줄이다. **Crossbow · APEX · Archer AI는 109개 공고 어디에도 없다** → [Gemini 주장 · 미확인] | R02 |
| FTE는 "모든 펌웨어 팀의 수문장(Gatekeeper)"이고 합격/불합격을 판정한다 | 공고는 "ensure the stability of builds before releases"까지만 말한다. 릴리스 거부권은 공고로 알 수 없다 [확인 필요] | R01 |
| Bazel은 "Neros가 매우 강조하는 빌드 시스템" | FTE 공고에서 Bazel은 Nice to have이고, "Bazel, Cmake, make or another embedded build system"으로 나열될 뿐이다 | R01 |
| ESC/BMS 같은 작은 보드는 RTOS로 돈다 | 추정일 뿐이다. 공개 공고에서 RTOS는 Tennessee 쪽 nice-to-have로만 나온다 | R06 |

### 5.2 Betaflight / INAV

| Gemini 주장 | 바로잡은 내용 | 단원 |
|---|---|---|
| RC loss 후 "정확히 0.5 s 뒤 모터 0" | 기본 `failsafe_delay`=15, 즉 1.5 s의 stage 1이다. 그동안은 armed 상태로 idle이고, 이어서 기본 절차 DROP_IT → disarm으로 간다. 검사는 scheduler에서 10 ms마다 돈다 | R03 · R07 · R08 |
| CRSF 파서는 0xC8을 찾고 1바이트씩 resync한다 | Betaflight는 바이트 사이 시간 간격(`frameTimeNeededUs` 1748 µs)으로 프레임을 자른다. 길이를 확인하고, CRC가 틀리면 프레임 전체를 버린다. 1바이트 resync는 PC 쪽 스트림 파서의 기법이다 | R03 |
| DShot = 11비트 + 4비트 CRC | 11비트 throttle + 1비트 telemetry 요청 + 4비트 CRC = 16비트다. bidirectional DShot은 CRC를 반전한다. DSHOT1200은 제거됐다 | R03 · R05 |
| Blackbox는 125 µs마다 기록한다 | 기본 기록률은 PID 루프의 1/4이다. 설정으로 바뀐다 | R03 |
| Blackbox flash는 "16 MB~128 MB" | 흔한 W25Q128은 128 **Mbit** = 16 MB다. Mbit와 MB를 섞은 것이다 | R02 |
| Betaflight는 GPS Position Hold를 거의 못 한다 | 2025.12 릴리스부터 `ALTITUDE_HOLD` · `POSITION_HOLD`가 들어왔다. master에는 GPS_RESCUE와 flight-plan task도 있다. 웨이포인트 미션은 여전히 INAV/PX4/ArduPilot 영역이다 | R01 · R02 · R03 |
| IMU는 항상 8 kHz | 센서마다 다르다. ICM-42688-P는 8 kHz, BMI270은 3.2 kHz다 | R02 · R08 |
| MSP로 FC를 "강제 제어"한다 | `MSP_SET_MOTOR`는 disarm 상태의 모터 테스트 값만 쓴다. `MSP_SET_RAW_RC`는 MSP RX provider나 MSP override 모드가 있어야 동작한다 | R03 |
| HITL은 MSP_SIMULATOR로 한다 | **INAV에만 있다** (`0x201F`). Betaflight에는 그런 명령이 없다 | R03 |
| SITL 위치 `src/main/target/SITL/` | Betaflight는 지금 `src/platform/SIMULATOR/`에 있다. 옛 경로는 INAV에서만 맞다 | R03 · R10 |
| CI는 GitLab에서 `make test` | upstream은 GitHub Actions(`.github/workflows/ci.yml` 등)에서 `make checks`와 `make test-all`을 돈다. Neros 내부 CI는 [추정] | R07 |

### 5.3 PX4 / ArduPilot

| Gemini 주장 | 바로잡은 내용 | 단원 |
|---|---|---|
| PX4 HITL은 "HIL_SENSOR만 쏘면 된다" | `SYS_HITL=1`이 필요하다. 커뮤니티 지원이고 Gazebo Classic에서만 문서화돼 있으며, 센서 드라이버를 우회한다. 새 Gazebo는 `gz_bridge`를 쓴다. 대안은 SIH다 | R04 · R07 |
| PX4 테스트는 C++ · Python · Swift로 짠다 | 저장소 안의 통합 테스트는 C++/Catch2만 있다. CI는 Gazebo가 아니라 SIH를 쓴다 | R04 |
| ArduPilot autotest는 "저수준 패킷 직접 파싱" | 수백 개의 고수준 `wait_*` 헬퍼와 MAVProxy 위에 서 있다. sim-time 타임아웃과 최소 유지 시간으로 판정한다 | R04 |
| ArduPilot은 "제품 객체와 테스트 객체를 분리한다" | 실제로는 `vehicle_test_suite.py` 기반 클래스를 쓴다. 기체별 테스트는 그 기체 클래스의 메서드다. "제품 객체 하나만 추가하면 테스트 1000개" 주장은 과장이다 | R09 |
| ArduPilot은 강하게 결합된(tightly coupled) 구조다 | AP_HAL이 이식성을 만든다. "통째로 결합"은 단순화가 지나치다 | R04 |
| ROS 2/DDS는 PX4 쪽 이야기다 | ArduPilot에도 `AP_DDS`가 있다(4.5부터) | R04 · R06 |
| PX4는 F7/H7만 쓴다 | STM32F4 보드도 있다 | R04 |
| GPL은 추가 개발 코드를 "모두에게 무료로 공개"해야 한다 | 의무는 **배포할 때**, **바이너리를 받은 사람에게** 생긴다. 같은 프로그램으로 결합된 부분에 적용되고, 별도 프로세스로 통신하는 앱은 보통 따로 본다. Betaflight도 GPLv3라는 점이 빠졌다 (법률 자문 아님) | R02 · R04 |

### 5.4 무선 링크 · 영상 · Linux

| Gemini 주장 | 바로잡은 내용 | 단원 |
|---|---|---|
| LR1121 = 900 MHz 칩, RX = STM32 + SX1280 | LR1121은 sub-GHz와 2.4 GHz를 모두 지원한다(`LORA_DUAL` 모드). 흔한 RX는 ESP8285 + SX1280이다 | R02 |
| RX = STM32 기반 | 지금 ExpressLRS master의 빌드 타깃은 ESP32/ESP8285뿐이다. STM32는 FC에 내장된 "SPI ExpressLRS" 수신기(Betaflight 쪽)로만 남았다 | R05 |
| 가짜 헤더 0xAA로 파서 테스트 | 0xAA는 예전 연습 문제 파서의 sync 값이다. CRSF sync(주소)는 0xC8(FC) · 0xEE(TX 모듈) 등이다 | R05 |
| CRSF 파서 = 1바이트 버리고 재동기 (하나의 정답) | 구현마다 다르다. Betaflight는 시간 간격으로 프레임을 자르고, ExpressLRS는 CRC 실패 시 소비한 바이트를 다 버리고, EdgeTX는 1바이트만 버리고 다시 찾는다. 같은 깨진 입력에 결과가 다를 수 있으니 기대값을 구현별로 적어야 한다 | R05 |
| RC 프레임은 항상 26바이트 | RX → FC는 26바이트다. EdgeTX → TX 모듈은 주소 0xEE와 상태 바이트가 붙어 27바이트(length 25)다 | R05 |
| bidirectional DShot eRPM → dynamic notch | eRPM은 **RPM filter**에 들어간다 | R05 |
| RX → FC UART "420k ~ 5.25M baud" | FC 쪽 표준은 420000이다. 921600 · 1.87M · 3.75M은 조종기 ↔ TX 모듈 쪽 설정이다. 5.25M은 확인하지 못했다 | R02 |
| ESC MCU는 "STM32G0, $1짜리" | STM32G071은 AM32가 지원하는 MCU 중 하나일 뿐이다. EFM8 · AT32 · GD32도 흔하다. 가격은 근거가 없다 | R02 |
| 감쇠기 0~95 dB = 20 km를 "완벽히 모사" | RCDAT-6000-90은 0~90 dB다. 2.4 GHz에서 20 km 자유공간 손실은 이미 약 126 dB다. 감쇠기는 평균 경로 손실만 흉내 낸다. fading과 간섭은 따로 다뤄야 한다. conducted 시험은 **링크 마진**을 재는 것이다 | R01 · R07 · R08 |
| SPI 센서 에뮬레이터가 필수 | FC가 SPI master라서 에뮬레이터가 레지스터 읽기에 실시간으로 답해야 하고, 보드의 IMU도 떼어 내야 한다. 더 싼 대안은 SITL 소켓이나 녹화된 IMU 재생이다 | R07 · R08 |
| SWD는 굽기 · RTT 로그 전용이고 명령은 못 보낸다 | RTT는 양방향이다. SWD로 동작 중에 메모리를 읽고 쓸 수도 있다. 명령을 UART/CRSF/MSP로 보내는 진짜 이유는 **출하되는 경로를 테스트하기 위해서**다 | R08 |
| RTT는 "CPU 부하 없이" | 오버헤드가 작지만 0은 아니다. µs 타이밍 측정에는 GPIO 토글 + 로직 분석기가 더 안전하다 | R01 |
| OpenHD/WFB-ng는 "아무 Wi-Fi 칩을 해킹" | raw 802.11 injection(monitor mode) + FEC다. 패치된 드라이버를 쓰는 특정 칩셋이 필요하다 | R02 · R06 |
| "microDDS" | 정식 이름은 Micro XRCE-DDS / uXRCE-DDS다(PX4 v1.14부터 Fast-RTPS bridge를 대체) | R06 |
| Jetson 위 OS는 Yocto | 기본은 JetPack/L4T(Ubuntu)다. Yocto는 meta-tegra를 쓰는 선택지다 | R06 |
| RTSP로 저지연 영상 | RTSP는 세션 제어다. `rtspsrc` 기본 jitter buffer가 2000 ms라서 설정하지 않으면 지연이 크다 | R06 |
| PREEMPT_RT는 외부 패치 | 6.12부터 mainline이다 (x86 · arm64 · riscv) | R06 |

### 5.5 Gemini가 맞게 짚은 것

정정만 있는 건 아니다. 아래는 확인 결과 맞았다.

- CRSF 프레임 `[addr][len][type][payload][crc]`, CRC-8 poly 0xD5, RC 채널 16 × 11비트 = 22바이트
- SBUS 25바이트, 100 kbaud 8E2, 반전 신호
- Betaflight는 bare-metal 협조형 scheduler이고, PX4는 NuttX + uORB + EKF2, ArduPilot은 ChibiOS
- ArduPilot `Tools/autotest`는 테스트 플랫폼 설계의 좋은 참고서다
- 기체가 달라도 "물리적 벤치 · 계측 장비는 공통, 대화하는 프로토콜만 다르다"는 큰 그림
- latency는 한 번의 숫자가 아니라 분포(히스토그램 · p99)로 봐야 한다

## 6. 이전 노트(K05) 경로 정정

[K05 오픈소스 지도](../../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md)에 `[확인 필요]`로 적었던 Betaflight 경로를 R03에서 master 소스로 확인했다 (commit `d593400a`, 2026-10-25).

| K05에 적은 것 | 실제 (master) |
|---|---|
| SITL은 `src/main/target/SITL/` | `src/platform/SIMULATOR/` (`sitl.c`, `udplink.c`, `target/SITL/target.h`, `README.md`) |
| 유닛 테스트 Makefile은 `src/test/unit/` | 소스는 `src/test/unit/*.cc`, Makefile은 `src/test/Makefile`, googletest는 `lib/test/gtest` |
| task 테이블 · 우선순위 [확인 필요] | `src/main/fc/tasks.c`의 `task_attributes[]` + `DEFINE_TASK`, `src/main/scheduler/scheduler.h`의 `taskPriority_e` · `taskId_e` |
| PID 루프 위치 | `src/main/fc/core.c`: `taskGyroSample` → `taskFiltering` → `taskMainPidLoop` (rc → pid → mixer → blackbox) |
| MSP | `src/main/msp/msp.c`(처리), `msp_serial.c`(프레이밍), `msp_protocol.h` |
| (없던 것) | `src/test/sitl/sitl_harness.py` — upstream SITL 끝단 테스트 하네스 |

## 7. 예제 코드

모두 표준 라이브러리만 쓰고, 파일 끝에 자체 테스트가 있다. `cd fteResearch/code && python3 -I <파일>`.

| 파일 | 내용 | 단원 |
|---|---|---|
| [msp_client.py](../code/msp_client.py) | MSP v1 · v2 인코드/디코드, CRC-8/DVB-S2, resync 스트림 파서, 가짜 FC와 끝단 테스트 | R03 |
| [mavlink_v2_heartbeat_codec.py](../code/mavlink_v2_heartbeat_codec.py) | MAVLink v2 프레임, CRC-16/MCRF4XX + CRC_EXTRA 계산 | R04 |
| [crsf.py](../code/crsf.py) | CRSF RC 채널 · link statistics 인코드/디코드, 스트림 파서 (resync · 길이 상한) | R05 |
| [dshot.py](../code/dshot.py) | DShot 프레임 · checksum · telemetry 비트 · bidirectional 반전 CRC | R05 |
| [step_response_oracle.py](../code/step_response_oracle.py) | step response 지표, 지연 분포 요약, 통계적 판정 (몇 번 돌려야 하나 · flaky) | R07 |
| [bench_drivers.py](../code/bench_drivers.py) | 계측기 인터페이스 + fake, 벤치 smoke test, "RC loss → failsafe" 테스트, PASS/FAIL/INFRA_FAIL | R08 |
| [platform_sketch.py](../code/platform_sketch.py) | capability 기반 DUT 모델, 테스트 선택, 벤치 스케줄러 · 잠금, 결과 레코드, flaky 탐지 | R09 |

## 8. 확인 표시 읽는 법

| 표시 | 뜻 |
|---|---|
| `[확인 필요]` | 공식 문서나 소스로 확인하지 못했다. 쓰기 전에 직접 확인 |
| `[추정]` | 공고 문장이나 일반론에서 추론했다. 특히 Neros 내부 스택은 거의 모두 추정 |
| `[Gemini 주장 · 미확인]` | Gemini가 말했지만 근거를 찾지 못했다 (예: Crossbow, APEX) |
| `검증 안 함 · 가이드` | 명령어를 직접 실행하지 않았다. 문서와 소스로만 확인 |
| `> ⚠️ Gemini 정정:` | Gemini 내용이 틀렸거나 과장된 곳 |

## 9. 남은 질문 (Neros에 들어가면 확인할 것)

- Betaflight 포크가 어느 upstream 버전에서 갈라졌고, 얼마나 자주 rebase하나
- Archer 외 기체 라인업과 FC 스택 (Betaflight 포크 vs PX4 vs 기타)
- 테스트 결과로 릴리스를 막을 권한이 FTE에게 있나
- CI 시스템(GitLab? Jenkins?)과 빌드 시스템(Bazel 도입 정도)
- 벤치 규모 · 계측기 · RF 차폐 시설
- 공장 EOL 테스트와 개발 HIL이 같은 코드를 쓰나

## 10. 관련 페이지

- [K05 Neros 오픈소스 지도 + 앞으로 할 것](../../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md) — 공고 109개 스캔 표, latency 파이프라인
- [Q01 새 서브시스템 통합 — FW는 무엇을 포팅 · 테스트하나](../../onsiteKit/qa/2026-10-20_Q01_fw_side_of_subsystem_integration.md)
- [N02 드론 HIL (9월 노트)](../../firmwareTestEngineerPrep/notes/2026-09-28_N02_hil_for_drones.md)
- 출발 자료: `~/Downloads/Neros 오픈소스 및 테스트 분석_prompt_gemini.pdf` (22쪽)

# 🛩️ R04 PX4 / ArduPilot 구조와 그들의 자체 테스트 생태계

> PX4(NuttX + uORB + work queue)와 ArduPilot(AP_HAL + ChibiOS + AP_Scheduler)의 구조를 실제 소스로 확인하고, 두 프로젝트가 자기 코드를 어떻게 테스트하는지(PX4 MAVSDK 통합 테스트 · SIH/Gazebo SITL · HITL · ULog, ArduPilot autotest · SITL 물리 모델 · Replay)를 코드 단위로 읽는다. 끝에서 Neros식 테스트 플랫폼이 무엇을 빌려 오면 좋을지 정리한다.

---

## 0. 이 단원을 읽는 법

- 이 단원의 **주인공은 테스트 생태계**다 (5~8절). 1~4절의 구조 설명은 테스트 코드를 읽기 위한 최소 배경이다.
- 소스 경로 · 함수 이름 · 파라미터 이름은 **2026-10-24에 shallow clone 한 저장소에서 직접 grep 해서 확인**했다.
- PX4-Autopilot: `main` 커밋 `cd2b157` (2026-10-24)
- ArduPilot: `master` 커밋 `437a330` (2026-10-24)
- SITL · 테스트를 **직접 돌려 보지는 않았다**. 11절의 명령어는 공식 문서 · CI 설정에서 옮긴 **가이드**다. 직접 실행해서 확인한 것은 `../code/mavlink_v2_heartbeat_codec.py` 자체 테스트뿐이다.
- Gemini 대화(15~20쪽)의 PX4 vs ArduPilot 설명은 맞는 부분이 많지만, 테스트 생태계 설명에 틀리거나 과장된 곳이 있다. 해당 위치에 `> ⚠️ Gemini 정정:` 으로 표시했고 7절에 모아 두었다.
- 관련 단원: 드론 전체 구조는 [R02](2026-10-24_R02_drone_system_and_open_source_map.md), Betaflight 내부는 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md), MAVLink 등 프로토콜은 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md), 테스트 피라미드는 [R07](2026-10-24_R07_test_methodology_pyramid.md), 플랫폼 설계는 [R09](2026-10-24_R09_scalable_reusable_test_platform.md).

---

## 1. 왜 중요한가 (FTE 관점)

Neros Firmware Test Engineer 공고의 우대 항목에 "FPV Drone software including Betaflight, ExpressLRS, PX4, Ardupilot"가 있다 [확인됨 공고]. Flight Software 공고에는 "Port and tune sensors and control algorithms across flight stacks (Betaflight, PX4)"와 "partnering with our Test organization on SITL/HITL"이 있다 [확인됨 공고].

FTE 입장에서 PX4 · ArduPilot은 두 가지 의미가 있다.

1. **테스트 대상일 수 있다.** Neros 기체 중 일부가 PX4(또는 ArduPilot) 기반이면, 그 펌웨어의 릴리스 회귀를 FTE가 책임진다 [추정].
2. **테스트 시스템의 교과서다.** 두 프로젝트는 10년 넘게 "실제로 날지 않고 비행 로직을 검증하는 법"을 오픈소스로 쌓아 왔다. 시뮬레이터 연결, 조건 대기 helper, 고장 주입, 로그 기반 판정, CI 매트릭스가 모두 공개돼 있다. Betaflight 포크용 HIL/SITL 플랫폼을 처음 짓는다면 여기서 설계 패턴을 그대로 빌릴 수 있다.

> 핵심 문장: "PX4와 ArduPilot은 둘 다 **SITL 위에서 MAVLink로 기체를 조작하고, 시뮬레이션 시간 기준 timeout으로 조건을 기다리고, 실패하면 로그를 남기는** 같은 골격을 갖고 있다. 차이는 언어(C++ Catch2 + MAVSDK vs Python + pymavlink)와 helper 라이브러리의 두께다."

---

## 2. PX4 구조

### 2.1 큰 그림

PX4는 **모듈들이 uORB라는 publish/subscribe 버스로만 대화하는** 구조다. 하드웨어 위에서는 NuttX RTOS가, SITL에서는 POSIX(Linux/macOS)가 아래에 깔린다. 같은 모듈 코드가 둘 다에서 돈다.

```text
 ┌──────────────────────────────────────────────────────────────────────────┐
 │                        PX4 모듈 (src/modules/*)                           │
 │  sensors ─▶ ekf2 ─▶ flight_mode_manager ─▶ mc_pos_control ─▶ mc_att_control│
 │                                                   │             │        │
 │  commander (arming · failsafe · mode)   navigator │   mc_rate_control     │
 │  mavlink (GCS/companion 연결)  logger (ULog)       ▼             ▼        │
 │                                           control_allocator ─▶ 출력 드라이버│
 ├──────────────────────── uORB (msg/*.msg → C++ struct) ─────────────────────┤
 │      platforms/common/uORB : Publication / Subscription / SubscriptionCallback │
 ├──────────────────────── work queue (px4_work_queue) ──────────────────────┤
 │  wq:rate_ctrl  wq:SPI0..6  wq:I2C0..4  wq:nav_and_controllers  wq:INS0..3 …  │
 ├─────────────────────────────────────────────────────────────────────────┤
 │   NuttX (보드: boards/px4/fmu-v5x 등)        │   POSIX (SITL: boards/px4/sitl)│
 └─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 uORB — 메시지 정의가 곧 인터페이스

- 메시지는 `msg/*.msg` 파일로 정의한다. 확인 시점에 `msg/` 아래 항목이 237개였다 (`msg/versioned/` 하위 디렉터리 포함).
- 예: `msg/SensorCombined.msg` 는 `timestamp`, `gyro_rad[3]`, `accelerometer_m_s2[3]`, clipping 비트필드, calibration count 같은 필드를 갖는다. 빌드 시 C++ 구조체와 직렬화 코드가 생성된다.
- API는 `platforms/common/uORB/` 에 있다: `Publication.hpp`, `Subscription.hpp`, `SubscriptionCallback.hpp`, `SubscriptionInterval.hpp`, `SubscriptionBlocking.hpp`.
- 모든 메시지에 `timestamp`(부팅 후 µs)가 있다. 로그(ULog)도 uORB 토픽을 그대로 기록하므로, **"버스에 흐른 것 = 로그에 남는 것"** 이 된다. 테스트 오라클을 만들기 쉬운 이유다.

FTE에게 uORB가 중요한 이유:

- 모듈 경계가 메시지로 고정돼 있어서 **모듈 하나만 떼어 테스트**하기 쉽다 (입력 토픽을 publish, 출력 토픽을 subscribe).
- 센서 고장 주입도 "센서 토픽을 안 내보내거나 값을 고정"하는 식으로 구현된다 (5.2절 failure injection).
- Betaflight처럼 전역 변수와 직접 호출로 엮인 코드보다 **관측점(probe point)이 많다**. 비교는 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md).

### 2.3 Work queue — 스레드를 아끼는 스케줄링

PX4 모듈 대부분은 자기 스레드를 갖지 않는다. 대신 `WorkItem` / `ScheduledWorkItem` 을 상속하고, 정해진 **work queue 스레드** 하나를 여러 모듈이 공유한다.

- 정의 위치: `platforms/common/include/px4_platform_common/px4_work_queue/WorkQueueManager.hpp`
- 확인한 큐 예시: `wq:rate_ctrl`, `wq:SPI0`~`wq:SPI6`, `wq:I2C0`~`wq:I2C4`, `wq:nav_and_controllers`, `wq:INS0`~`wq:INS3`, `wq:hp_default`, `wq:uavcan`, `wq:ttyS0`~`wq:ttyS8`, `wq:vte`
- 각 큐의 스택 크기 · 우선순위는 Kconfig 값(`CONFIG_WQ_RATE_CTRL_PRIORITY` 등)으로 정해진다.

**콜백 구동(data-driven) 예 — `mc_rate_control`**

- `src/modules/mc_rate_control/MulticopterRateControl.cpp` 생성자: `WorkItem(MODULE_NAME, px4::wq_configurations::rate_ctrl)`
- 헤더: `uORB::SubscriptionCallbackWorkItem _vehicle_angular_velocity_sub{this, ORB_ID(vehicle_angular_velocity)};`
- 초기화에서 `_vehicle_angular_velocity_sub.registerCallback()` → 새 각속도 메시지가 publish 될 때마다 `Run()`이 rate_ctrl 큐에서 실행된다.

즉 **각속도 루프는 타이머가 아니라 "자이로 데이터 도착"이 깨운다**. 루프 주기 = 자이로 필터 출력 주기다. Betaflight의 "gyro 동기 PID task"와 개념이 비슷하다.

> FTE 관점: 루프 지터를 재려면 "타이머 주기"가 아니라 **`vehicle_angular_velocity` publish 간격과 `Run()` 실행 시간**을 봐야 한다. PX4는 `work_queue status`, `top` 같은 셸 명령과 로그의 타임스탬프로 이걸 볼 수 있다 [확인 필요: 셸 명령 출력 형식은 실행해서 확인].

### 2.4 제어 · 추정 파이프라인 (멀티콥터)

```text
 IMU 드라이버 ─▶ sensors ─▶ vehicle_angular_velocity ─────────────────────────────┐
     (wq:SPIx)        │                                                            │
                      └▶ sensor_combined ─▶ ekf2 ─▶ vehicle_attitude                │
 GPS/baro/mag ─────────────────────────────▶ (EKF) ─▶ vehicle_local_position       │
                                                       vehicle_global_position      │
 RC / MAVLink ─▶ manual_control ─▶ flight_mode_manager ─▶ trajectory_setpoint       │
 mission ─▶ navigator ───────────────────────────────▶ (위치 setpoint)               │
                                                            │                      │
                     mc_pos_control ─▶ vehicle_attitude_setpoint                    │
                     mc_att_control ─▶ vehicle_rates_setpoint                       │
                     mc_rate_control ◀──────────────────────────────────────────────┘
                          └─▶ vehicle_torque_setpoint / vehicle_thrust_setpoint
                     control_allocator ─▶ actuator_motors ─▶ PWM/DShot/UAVCAN 출력
```

| 단계 | 모듈 (경로) | 입력 토픽 → 출력 토픽 | 테스트 포인트 |
|---|---|---|---|
| 센서 정리 | `src/modules/sensors` | 드라이버 raw → `sensor_combined`, `vehicle_angular_velocity` | clipping 플래그, 센서 선택(voting), 캘리브레이션 count |
| 상태 추정 | `src/modules/ekf2` (+ `EKF/`) | `sensor_combined`, GPS, baro, mag → attitude, local/global position | 센서 고장 시 fusion 전환, innovation 검사, 리셋 |
| 모드/궤적 | `src/modules/flight_mode_manager`, `navigator` | RC/미션 → `trajectory_setpoint` | 모드 전환 조건, 미션 항목 처리 |
| 위치 제어 | `src/modules/mc_pos_control` | setpoint + 추정 위치 → attitude setpoint | 속도/가속 제한, 착륙 감지와의 상호작용 |
| 자세 제어 | `src/modules/mc_att_control` | attitude setpoint → rates setpoint | 큰 각도 오차에서의 동작 |
| 각속도 제어 | `src/modules/mc_rate_control` | rates setpoint + 각속도 → torque/thrust | 루프 주기, 적분기 포화, 지터 |
| 제어 할당 | `src/modules/control_allocator` | torque/thrust → 모터별 명령 | 모터 고장 시 재할당, 포화 |
| 감시 | `src/modules/commander` | 모든 상태 → arming, failsafe, nav_state | 2.5절 |

EKF2는 단위 테스트가 특히 두껍다: `src/modules/ekf2/test/` 에 `test_EKF_gps.cpp`, `test_EKF_mag.cpp`, `test_EKF_height_fusion.cpp`, `test_EKF_withReplayData.cpp` 등 30개 가까운 파일과 `sensor_simulator/`, `replay_data/` 가 있다. **추정기를 비행 없이 데이터로 검증**하는 좋은 예다.

### 2.5 Arming · Failsafe — commander

`src/modules/commander/` 가 기체의 "상태 기계" 담당이다.

- `HealthAndArmingChecks/` : arming 전 검사. `checks/` 아래에 `accelerometerCheck`, `baroCheck`, `batteryCheck`, `companionComputerCheck`, `cpuResourceCheck` 등 검사기가 파일 하나씩 있다. 결과는 MAVLink event로 GCS에 이유와 함께 전달된다.
- `failsafe/` : `failsafe.cpp`, `framework.cpp/.h`, `failsafe_test.cpp`(gtest), `emscripten.cpp`(웹 시뮬레이터 빌드용).
- `framework.h` 의 `enum class Action`: `None`, `Warn`, `FallbackPosCtrl`, `FallbackAltCtrl`, `FallbackStab`, `Hold`, `RTL`, `Land`, `Descend`, `Disarm`, `Terminate`.
- 실패 조건은 `failsafe_flags` uORB 메시지의 불리언(`manual_control_signal_lost`, `gcs_connection_lost`, `battery_low_remaining_time`, `fd_critical_failure` …)으로 모인다. failsafe 로직은 **플래그 → 행동**만 결정하는 순수 함수에 가깝다.

**주요 파라미터** (`src/modules/commander/commander_params.yaml` 에서 확인)

| 파라미터 | 의미 | 기본값 |
|---|---|---|
| `COM_RC_LOSS_T` | 조종 입력(RC/조이스틱) 끊김 판정 시간 | 0.5 s |
| `NAV_RCL_ACT` | 조종 입력 끊김 시 행동 (1 Hold, 2 Return, 3 Land, 5 Terminate, 6 Disarm, 7 Hold-no-failsafe) | 2 (Return) |
| `COM_DL_LOSS_T` | GCS 데이터링크 끊김 판정 시간 | 10 s |
| `COM_FAIL_ACT_T` | failsafe 조건 발생 후 실제 행동까지 Hold로 기다리는 시간 | 5 s |
| `COM_LOW_BAT_ACT` | 배터리 부족 시 행동 | [확인 필요: 기본값] |
| `COM_RCL_EXCEPT` | 특정 모드(미션/오프보드 등)에서 RC 끊김을 무시 | 비트마스크 |

**failsafe 단위 테스트 읽기** — `src/modules/commander/failsafe/failsafe_test.cpp`

- 파일 상단 주석: `// to run: make tests TESTFILTER=failsafe_test`
- `FailsafeTester` 가 실제 `Failsafe` 를 상속해 `checkStateAndMode()`를 테스트용 규칙으로 바꾼다. 예: `CHECK_FAILSAFE(status_flags, manual_control_signal_lost, ActionOptions(Action::RTL).clearOn(...))`, `CHECK_FAILSAFE(status_flags, gcs_connection_lost, Action::Descend)`.
- `SetUp()`에서 `COM_FAIL_ACT_T = 5` 로 고정.
- `TEST_F(FailsafeTest, General)` 흐름: 시간 5 s, 무장 상태, POSCTL → `manual_control_signal_lost = true` → 즉시 `Action::Hold` → 시간을 6 s 진행 → `Action::RTL` → `gcs_connection_lost = true` → `Action::Descend`.

포인트: **시간을 인자로 넘긴다** (`failsafe.update(time, state, ...)`). 그래서 실제 5초를 기다리지 않고 `time += 6_s` 한 줄로 "5초 지연 후 RTL"을 검증한다. 시간 의존 로직을 테스트 가능하게 만드는 전형적 설계다. Betaflight failsafe 단계(stage 1/2)를 테스트할 때도 같은 원칙이 필요하다.

추가로 PX4 CI에는 `failsafe_sim.yml` 이 있다. emscripten으로 failsafe 로직을 **웹 페이지 시뮬레이터**(`make failsafe_web`)로 빌드한다. 플래그를 체크박스로 켜고 끄며 어떤 행동이 선택되는지 브라우저에서 볼 수 있는 도구다 [확인 필요: 공개 호스팅 URL].

### 2.6 파라미터

- 모듈마다 `module.yaml` 또는 `*_params.yaml` 에 파라미터를 선언한다 (예: `commander_params.yaml`, `ekf2/params_gnss.yaml`). 빌드 시 메타데이터(JSON)가 생성되어 QGroundControl이 설명 · 범위를 표시한다.
- 런타임: 셸에서 `param show COM_RC_LOSS_T`, `param set NAV_RCL_ACT 3`. MAVLink `PARAM_*` 메시지와 MAVLink FTP로 GCS가 읽고 쓴다 (MAVSDK 테스트에 `test_param_ftp.cpp` 가 있다).
- SITL 테스트 설정 JSON의 `"env": {"PX4_PARAM_EKF2_EN": 0}` 처럼 **환경 변수로 파라미터를 주입**할 수 있다 (5.3절).

### 2.7 로깅 — ULog

- 모듈: `src/modules/logger/` (`logger.cpp`, `log_writer_file.cpp`, `log_writer_mavlink.cpp`, `logged_topics.cpp`, `watchdog.cpp`)
- 형식: **ULog** — 자기 기술(self-describing) 형식. 파일 안에 uORB 메시지 형식 정의와 데이터가 함께 들어 있다 (`docs/en/dev_log/ulog_file_format.md`).
- `SDLOG_MODE`: 0 = 무장~해제(기본), 1 = 부팅~해제, 2 = 부팅~종료, 3 = AUX1 스위치, 4 = 첫 무장~종료.
- `SDLOG_PROFILE`: EKF2 replay용, 고속 PID 튜닝용 등 기록 묶음 선택.
- `log_writer_mavlink` 로 MAVLink 위로 로그 스트리밍도 가능하다.
- 분석 도구: **pyulog** (`ulog_info`, `ulog_messages`, `ulog_params`, `ulog2csv`), **Flight Review** (logs.px4.io, 웹 업로드 · 플롯), PlotJuggler.

---

## 3. ArduPilot 구조

### 3.1 큰 그림

ArduPilot은 **차량별 프로그램(ArduCopter, ArduPlane, Rover, ArduSub, Blimp, AntennaTracker)** 이 거대한 공용 `libraries/` 를 쓰고, 그 밑에 **AP_HAL**(하드웨어 추상화 계층)이 있는 구조다.

```text
 ┌──────────────────────────────────────────────────────────────────────┐
 │  차량 코드:  ArduCopter/  ArduPlane/  Rover/  ArduSub/  Blimp/          │
 │   (Copter.cpp 의 scheduler_tasks[], mode_*.cpp 28개, events.cpp …)    │
 ├──────────────────────────────────────────────────────────────────────┤
 │  libraries/ : AP_AHRS  AP_NavEKF3  AC_AttitudeControl  AP_Motors        │
 │               AP_Arming  AP_Logger  AP_Param  GCS_MAVLink  AP_Scheduler  │
 │               AP_DDS  AP_Scripting(Lua)  SITL  AP_DAL  AP_Vehicle …     │
 ├──────────────────────────── AP_HAL (인터페이스) ──────────────────────────┤
 │  AP_HAL_ChibiOS  │  AP_HAL_Linux  │  AP_HAL_SITL  │  AP_HAL_ESP32  │ QURT │
 ├──────────────────┴────────────────┴───────────────┴───────────────────────┤
 │  ChibiOS + STM32 (hwdef.dat)  │  Linux SBC  │  호스트 PC (SITL)  │  ESP32   │
 └──────────────────────────────────────────────────────────────────────┘
```

확인한 AP_HAL 구현: `libraries/AP_HAL_ChibiOS`, `AP_HAL_Linux`, `AP_HAL_SITL`, `AP_HAL_ESP32`, `AP_HAL_QURT`, `AP_HAL_Empty`.

### 3.2 hwdef — 보드를 "데이터"로 정의

- `libraries/AP_HAL_ChibiOS/hwdef/` 아래에 보드 디렉터리가 확인 시점 471개 있다.
- 각 보드는 `hwdef.dat`(+ 부트로더용 `hwdef-bl.dat`)라는 텍스트 파일 하나로 MCU, 클럭, 핀, SPI 장치, UART 순서를 선언한다. 빌드 때 `chibios_pins.py` 가 이것을 헤더로 변환한다.

`MatekH743/hwdef.dat` 앞부분 (실제 내용 발췌):

```text
MCU STM32H7xx STM32H743xx
APJ_BOARD_ID AP_HW_MATEKH743
OSCILLATOR_HZ 8000000
FLASH_SIZE_KB 2048
FLASH_RESERVE_START_KB 128
STM32_ST_USE_TIMER 12
PA13 JTMS-SWDIO SWD
PA14 JTCK-SWCLK SWD
# SPI1 for IMU1 (MPU6000)
PA5 SPI1_SCK SPI1
PA6 SPI1_MISO SPI1
PD7 SPI1_MOSI SPI1
PC15 IMU1_CS CS
```

> FTE 관점: 보드 정의가 데이터 파일이므로 **"보드별 테스트 매트릭스"를 hwdef에서 자동 생성**할 수 있다 (어떤 보드에 IMU 몇 개, UART 몇 개). Betaflight에도 비슷한 target/config 파일이 있다 [확인 필요: Betaflight unified target 형식, R03 참조].

### 3.3 AP_Scheduler — 협력형 task 테이블

ArduPilot 메인 루프는 PX4처럼 메시지가 깨우는 구조가 아니다. **고정 주기 메인 루프 + 시간 예산이 있는 task 테이블**이다 (메인 루프 자체는 ChibiOS 스레드 하나 위에서 돌고, 그 밖에 IO · 타이머 스레드가 있다).

- `libraries/AP_Scheduler/AP_Scheduler.cpp`: `#define SCHEDULER_DEFAULT_LOOP_RATE 400`, 파라미터 `SCHED_LOOP_RATE` (설명상 범위 50~400, 코드에서 50~2000으로 clamp).
- `ArduCopter/Copter.cpp` 에 task 테이블이 있다. 두 종류다.

**FAST_TASK** — 매 루프 실행 (순서대로):

```text
FAST_TASK_CLASS(AP_InertialSensor, &copter.ins, update)   // IMU 읽기
FAST_TASK(run_rate_controller_main)                       // 각속도 제어
FAST_TASK(run_custom_controller)
FAST_TASK(motors_output_main)                             // 모터 출력
FAST_TASK(read_AHRS)                                      // 자세 추정 갱신
FAST_TASK(read_inertia)
FAST_TASK(check_ekf_reset)
FAST_TASK(update_flight_mode)                             // 현재 모드의 run()
FAST_TASK(update_home_from_EKF)
```

**SCHED_TASK(함수, 주기 Hz, 최대 실행 시간 µs, 우선순위)** — 남는 시간에 실행:

| task | Hz | max µs | prio |
|---|---|---|---|
| `rc_loop` | 250 | 130 | 3 |
| `throttle_loop` | 50 | 75 | 6 |
| `fence_check` | 25 | 100 | 7 |
| `AP_GPS::update` | 50 | 200 | 9 |
| `update_batt_compass` | 10 | 120 | 15 |
| `auto_disarm_check` | 10 | 50 | 27 |
| `read_rangefinder` | 20 | 100 | 33 |
| `run_nav_updates` | 50 | 100 | 45 |
| `three_hz_loop` | 3 | 75 | 57 |

- Copter에는 `FSTRATE_ENABLE` 파라미터(`ArduCopter/Parameters.cpp`)로 켜는 **fast rate thread**도 있다. 각속도 제어를 자이로 속도(나누기 divisor)로 별도 스레드에서 돌리는 옵션이다 [확인 필요: 기본 비활성(0), 지원 보드 범위].
- 스케줄러는 task별 실행 시간 초과를 기록하고, `SCHED_DEBUG` 등으로 볼 수 있다 [확인 필요: 파라미터 이름 · 출력].

> Betaflight 스케줄러와 비교: Betaflight도 gyro/PID가 최우선이고 나머지 task는 "남는 시간 + 우선순위 + 대기 시간"으로 고른다 ([R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md)). ArduPilot의 `max µs` 열은 **task별 시간 예산을 코드에 박아 둔 것**이라 테스트에서 "예산 초과 횟수"를 메트릭으로 쓸 수 있다.

### 3.4 제어 · 추정 파이프라인 (Copter)

```text
 AP_InertialSensor (FAST) ─▶ AP_AHRS ─▶ AP_NavEKF3 (EKF3 기본, EKF2도 남아 있음)
                                          │
 RC_Channels (rc_loop 250Hz) ─▶ 현재 mode_*.cpp run() (update_flight_mode)
 GCS_MAVLink (명령) ───────────▶   │  ex) ModeRTL, ModeLoiter, ModeAuto, ModeGuided
                                   ▼
                     AC_PosControl / AC_WPNav (위치 · 경로)
                                   ▼
                     AC_AttitudeControl (자세 → 각속도 → rate PID)
                                   ▼
                     AP_Motors (믹서) ─▶ SRV_Channels ─▶ PWM / DShot / CAN ESC
```

- 모드는 `ArduCopter/mode_*.cpp` 파일 하나가 한 모드다 (확인 시점 28개). 클래스 계층이 뚜렷해서 "모드별 테스트"가 자연스럽다.
- 상태 추정은 `libraries/AP_NavEKF3/` 가 주력이다. `AP_DAL`(Data Abstraction Layer)이 EKF 입력을 한 곳으로 모아서, 로그만으로 EKF를 똑같이 재실행하는 **Replay**가 가능하다 (6.7절).

### 3.5 Arming · Failsafe

- 사전 검사: `libraries/AP_Arming/` + 차량별 `ArduCopter/AP_Arming_Copter.cpp`. 실패 이유는 `PreArm: ...` STATUSTEXT로 나간다. autotest에는 `assert_prearm_failure("Compasses inconsistent")` 처럼 **문구로 검증하는 helper**가 있다.
- failsafe 처리: `ArduCopter/events.cpp` — `failsafe_radio_on_event()`, `handle_battery_failsafe()`, `failsafe_gcs_check()`. 메인 루프 정지 감시는 `ArduCopter/failsafe.cpp`, EKF 이상은 `ekf_check.cpp`, 추락 감지는 `crash_check.cpp`.

`failsafe_radio_on_event()` 핵심 흐름 (실제 코드 요약):

1. `LOGGER_WRITE_ERROR(FAILSAFE_RADIO, FAILSAFE_OCCURRED)` — 먼저 로그에 남긴다.
2. `FS_THR_ENABLE` 값으로 원하는 행동을 고른다 (RTL / SmartRTL / Land / DO_LAND_START / Brake-or-Land).
3. 예외 조건: 지상이면 즉시 disarm, 이미 착륙 중이고 배터리 failsafe면 착륙 계속, `FS_OPTIONS` 에 따라 착륙 계속 등.

| 파라미터 | 값 (`ArduCopter/Parameters.cpp` @Values) |
|---|---|
| `FS_THR_ENABLE` | 0 Disabled, 1 always RTL, 2 (4.0+에서 제거), 3 always Land, 4 SmartRTL or RTL, 5 SmartRTL or Land, 6 DO_LAND_START/DO_RETURN_PATH_START or RTL, 7 Brake or Land |
| `FS_GCS_ENABLE` | 0 Disabled, 1 RTL, 3 SmartRTL or RTL, 4 SmartRTL or Land, 5 Land, 6 DO_LAND_START or RTL, 7 Brake or Land (GCS 끊김 5초 이상) |
| `FS_OPTIONS` | 비트마스크: 착륙 중이면 계속, 미션 계속 등 |
| `SIM_RC_FAIL` (SITL 전용) | 0 정상, 1 RC 펄스 없음, 2 스로틀만 950 µs 나머지 중립 |

### 3.6 파라미터 · 로깅

- 파라미터: `libraries/AP_Param/`. 코드 안 `// @Param:`, `// @Values:` 주석에서 메타데이터를 추출한다 (`Tools/autotest/param_metadata/`). 그래서 위 표의 값 목록을 **소스 주석에서 바로** 확인할 수 있다.
- 로그: `libraries/AP_Logger/` 가 **DataFlash 형식 `.bin`** 을 SD/플래시에 쓴다. 메시지 형식이 로그 앞부분 FMT 메시지로 기술된다 (ULog처럼 자기 기술형).
- 분석 도구: pymavlink의 `mavlogdump.py`, MAVProxy 저장소의 **MAVExplorer**, Mission Planner/UAV Log Viewer [확인 필요: 각 도구의 현재 유지 상태].
- `Tools/LogAnalyzer/` 는 현재 트리에 없다 (2026-10-24 clone의 `Tools/` 목록에서 확인). 예전 문서나 Gemini가 LogAnalyzer를 말하면 낡은 정보일 수 있다.

---

## 4. 한 표로 비교 + 라이선스 + Neros가 둘 다 적은 이유

### 4.1 나란히 보기

| 항목 | PX4 | ArduPilot |
|---|---|---|
| 하드웨어 위 OS | NuttX (`boards/px4/fmu-v2` ~ `fmu-v6x` 등) | ChibiOS (`AP_HAL_ChibiOS`, hwdef 471개) |
| 그 밖 실행 환경 | POSIX(SITL, Linux 보드), ROS 2 연동 | Linux SBC(`AP_HAL_Linux`), ESP32, QURT, SITL |
| 모듈 간 통신 | uORB pub/sub (`msg/*.msg`) | 라이브러리 객체 직접 호출 (싱글톤 · frontend/backend 패턴) |
| 실행 모델 | work queue 스레드 + uORB 콜백 구동 | 고정 주기 메인 루프 + FAST_TASK + 시간 예산 task 테이블 |
| 기본 루프 | 각속도 루프 = 자이로 데이터 도착 시 | `SCHED_LOOP_RATE` 400 Hz 기본 (Copter fast rate thread 옵션) |
| 추정기 | EKF2 (`src/modules/ekf2`) | EKF3 (`AP_NavEKF3`), EKF2도 존재 |
| 상태 기계 | commander (HealthAndArmingChecks + failsafe framework) | 차량 코드 events.cpp/failsafe.cpp + AP_Arming |
| 파라미터 선언 | YAML (`module.yaml`, `*_params.yaml`) | 소스 주석 `@Param` + `AP_GROUPINFO` |
| 로그 | ULog `.ulg` | DataFlash `.bin` |
| 로그 도구 | pyulog, Flight Review, PlotJuggler | pymavlink(mavlogdump), MAVExplorer, UAV Log Viewer |
| 로그 재실행 | `replay` 모듈 (`export replay=...ulg`, `replay_mode=ekf2`) | `Tools/Replay` + `check_replay.py` (CI에서 비트 단위 비교) |
| 시뮬레이터 | SIH(내장), Gazebo(gz), Gazebo Classic, jMAVSim/JSBSim/FlightGear 등 | 내장 물리 모델(`libraries/SITL/SIM_*.cpp`), Gazebo, JSBSim, RealFlight, JSON 외부 연결 |
| 통합 테스트 | `test/mavsdk_tests` (C++ · Catch2 · MAVSDK) | `Tools/autotest` (Python · pymavlink · MAVProxy) |
| 단위 테스트 | gtest, `make tests` | gtest, `./waf check` |
| ROS 2 | uXRCE-DDS client(`uxrce_dds_client`), zenoh | `AP_DDS` (CI `test_dds.yml`) |
| 거버넌스 | Dronecode Foundation (Linux Foundation) | ArduPilot 프로젝트 (비영리 단체) [확인 필요: 법인 형태] |
| 라이선스 | **BSD 3-Clause** (`LICENSE` 첫 줄) | **GPLv3** (`COPYING.txt`, README "version 3") |

> ⚠️ Gemini 정정: Gemini 표(12쪽)는 "상위 보드(Jetson) 연동: PX4 최강(MAVLink/microDDS 표준) vs ArduPilot MAVLink 표준"이라고 했다. ArduPilot에도 `libraries/AP_DDS` 와 CI 워크플로 `test_dds.yml` 이 있다. ROS 2 연동 성숙도의 차이는 있을 수 있지만 "ArduPilot은 MAVLink만"은 사실이 아니다.

> ⚠️ Gemini 정정: Gemini(18~19쪽)는 ArduPilot을 "uORB 같은 중간 버스 없이 직접 함수를 호출하고 메모리를 공유하는 tightly coupled 구조"라고 했다. 버스가 없는 것은 맞다. 하지만 ArduPilot의 이식성은 **AP_HAL 추상화 계층**에서 나오고, 라이브러리들은 frontend/backend 구조로 드라이버를 갈아 끼운다. "코드를 뜯어고치기 어렵다"는 평가는 주관적이며, 실제로 hwdef 파일만으로 보드 400여 개를 지원한다.

> ⚠️ Gemini 정정: Gemini 표는 PX4 하드웨어를 "STM32 F7/H7"로 적었다. 저장소에는 `boards/px4/fmu-v2`, `fmu-v3`, `fmu-v4` 같은 STM32F4 계열 보드도 남아 있다 (지원 수준은 버전마다 다름 [확인 필요]).

### 4.2 라이선스 — BSD-3 vs GPLv3 [추정 · 법률 자문 아님]

확인한 사실:

- PX4-Autopilot `LICENSE`: "BSD 3-Clause License, Copyright (c) 2012 - 2025, PX4 Development Team".
- ArduPilot `COPYING.txt`: "GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007". README: "licensed under the GNU General Public License, version 3".

일반적인 이해 [추정, 법률 자문 아님 — 실제 판단은 회사 법무]:

| 질문 | BSD-3 (PX4) | GPLv3 (ArduPilot) |
|---|---|---|
| 수정한 소스를 비공개로 유지 | 가능. 저작권 고지 · 면책 문구 유지 의무 | **배포하지 않으면** 공개 의무 없음 (내부 사용) |
| 수정 펌웨어가 든 제품을 판매(배포) | 소스 공개 의무 없음 | 바이너리를 받은 **수령자**에게 대응 소스 제공 의무, GPLv3 조건 전달 |
| 같은 바이너리에 독점 코드 링크 | 가능 | 링크된 전체가 GPL 조건을 따라야 한다는 해석이 일반적 |
| 별도 프로세스(companion 컴퓨터)의 독점 소프트웨어 | 무관 | MAVLink로 통신하는 별도 프로그램은 보통 별개 저작물로 본다 [추정] |
| 하위 구성요소 | NuttX(Apache-2.0 [확인 필요]) 등 서드파티 라이선스 별도 | ChibiOS(GPLv3 / HAL은 Apache-2.0 [확인 필요]) 등 |

> ⚠️ Gemini 정정: Gemini(15, 19쪽)는 GPLv3면 "회사가 추가 개발한 소스 코드 모두를 무료로 공개해야 한다"고 했다. 더 정확히는 **바이너리를 배포받은 상대방에게** 소스를 제공할 의무가 생기는 것이고, 전 세계 공개 의무나 "무료 공개" 의무와는 다르다. 방산에서 납품처가 정부 한 곳이면 실무 영향이 달라질 수 있다 [추정]. 어느 쪽이든 FTE가 판단할 일은 아니고, **빌드 산출물에 어떤 라이선스 코드가 섞였는지 SBOM으로 추적**하는 것이 테스트 · 릴리스 쪽에서 할 일이다. PX4 CI에는 실제로 `sbom_license_check.yml`, `sbom_monthly_audit.yml` 이 있다.

Betaflight는 GPLv3다 [확인 필요: R03에서 확인]. Neros가 Betaflight 포크를 제품에 넣는다면 GPL 관리는 이미 하고 있을 것이다 [추정].

### 4.3 Neros 공고는 왜 Betaflight와 PX4를 둘 다 적었나 [추정]

공고 사실 [확인됨 공고]: Flight Software 공고 "Port and tune sensors and control algorithms across flight stacks (Betaflight, PX4)", 우대 "Betaflight internals — modes, RC/mixer path, or a maintained fork", "PX4 ecosystem — MAVLink, uORB, EKF2, … QGroundControl".

가능한 해석 [모두 추정]:

1. **제품군이 둘로 나뉜다.** 저지연 수동 조종(FPV) 기체는 Betaflight 포크, GPS/자율 기능이 필요한 기체는 PX4. Gemini는 이를 "Archer(Betaflight) / Archer AI(PX4 + Jetson)"로 설명했다 [Gemini 주장 · 미확인].
2. **알고리즘 이식.** PX4/INAV의 위치 유지 · 추정 기능을 Betaflight 포크로 옮기거나, 반대로 Betaflight의 필터 · 튜닝 감각을 PX4 쪽으로 가져가는 작업. 공고의 "port … across flight stacks"가 이것을 직접 말한다.
3. **센서 드라이버 공유.** 같은 IMU/기압계를 두 스택에서 써야 하므로 드라이버를 양쪽에 맞춘다.
4. **비교 기준(reference).** PX4/ArduPilot의 검증된 동작을 Betaflight 포크 변경의 **기준 동작(oracle)** 으로 쓸 수 있다 (예: 같은 failsafe 시나리오에서 두 스택의 행동 비교).

FTE에게 주는 의미: 테스트 플랫폼이 **스택 하나에 묶이면 안 된다**. "기체를 무장한다 · RC를 끊는다 · 모드를 읽는다"를 스택 독립 인터페이스로 만들고, 아래에 MSP/CRSF 드라이버(Betaflight)와 MAVLink 드라이버(PX4/ArduPilot)를 꽂는 구조가 필요하다 (8절, [R09](2026-10-24_R09_scalable_reusable_test_platform.md)). 이 생각 자체는 Gemini 17~18쪽의 `NerosDrone` 클래스 예시와 같다. 방향은 맞다.

---

## 5. PX4의 테스트 생태계

### 5.1 층 구조

| 층 | 무엇 | 어디 | 실행 |
|---|---|---|---|
| 단위 (gtest) | 함수 · 클래스 단위. failsafe, EKF, uORB, 수학 라이브러리 | `*Test.cpp`, `src/modules/ekf2/test/`, `src/modules/commander/failsafe/failsafe_test.cpp` | `make tests` (`TESTFILTER=<regex>`) |
| 기능 (functional gtest) | 파라미터 · uORB가 살아 있는 상태의 테스트 | `docs/en/test_and_ci/unit_tests.md` "Functional Test" | `make tests` |
| SITL 단위 | PX4 셸 안에서 도는 테스트 | 같은 문서 "SITL Unit Test" | [확인 필요] |
| **SITL 통합 (MAVSDK)** | 비행 시나리오 전체 | `test/mavsdk_tests/` (test 파일 22개, `TEST_CASE` 72개, 비활성 포함) | `mavsdk_test_runner.py` |
| ROS 2 통합 | ROS 2 인터페이스 | `integration_testing_px4_ros2_interface.md`, `ros_integration_tests.yml` | CI |
| Fuzzing | 파서 등 | `fuzzing.yml` (google fuzztest 서브모듈) | CI |
| Sanitizer | ASan / TSan / UBSan 빌드로 SITL 테스트 | `sitl_tests.yml` 매트릭스 | CI |
| 정적 검사 | 포맷, 모듈 문서, clang-tidy | `checks.yml`, `clang-tidy.yml` | CI |
| HITL | 실제 FC + PC 시뮬레이터 | `docs/en/simulation/hitl.md` | 수동 (community supported) |
| 비행 시험 | Test cards | `docs/en/test_cards/`, `test_and_ci/test_flights.md` | 사람 |

### 5.2 MAVSDK 통합 테스트 — 코드로 읽기

**(1) 테스트 케이스는 짧은 "시나리오 문장"이다** — `test/mavsdk_tests/test_multicopter_failsafe.cpp`

```cpp
TEST_CASE("Land on GPS lost during mission (baro height mode)", "[multicopter]")
{
	AutopilotTester tester;
	tester.connect(connection_url);

	tester.set_height_source(AutopilotTester::HeightSource::Baro);
	tester.wait_until_ready();

	AutopilotTester::MissionOptions mission_options;
	mission_options.rtl_at_end = true;
	tester.prepare_square_mission(mission_options);
	tester.arm();
	tester.execute_mission_and_lose_gps();
	std::chrono::seconds until_disarmed_timeout = std::chrono::seconds(300);
	tester.wait_until_disarmed(until_disarmed_timeout);
}
```

- 프레임워크는 **Catch2** (`test/mavsdk_tests/catch2/catch.hpp`). `TEST_CASE("이름", "[태그]")` 의 태그가 나중에 설정 파일의 `test_filter` 와 연결된다.
- 같은 파일에 "mag lost → 계속", "baro lost/stuck → 계속" 같은 변형이 나란히 있다. 하나는 `#if 0` 으로 꺼져 있고 주석에 이유("estimator sometimes diverges right after landing")가 있다. **불안정 테스트를 숨기지 않고 이유와 함께 끄는** 관행이다.

**(2) 모든 동작은 `AutopilotTester` helper로** — `autopilot_tester.h/.cpp`

`connect()` 는 MAVSDK 연결 후 plugin들을 만든다: `Action`, `Failure`, `Info`, `ManualControl`, `Mission`, `MissionRaw`, `Offboard`, `Param`, `Telemetry`, `Events`, `MavlinkPassthrough`. 그리고 PX4 events를 구독해 로그 레벨 · 메시지 · 이벤트 이름을 콘솔에 찍는다. **실패했을 때 기체가 무슨 말을 했는지 테스트 로그에 남기는** 장치다.

`wait_until_ready()`:

```cpp
CHECK(poll_condition_with_timeout(
	[this]() { return _telemetry->health_all_ok(); }, std::chrono::seconds(30)));
// MAVSDK 이슈 #1852: health_all_ok()가 true여도 global position이 아직 유효 아닐 수 있음 (hysteresis)
CHECK(poll_condition_with_timeout(
	[this]() {	return _telemetry->health().is_armable;	}, std::chrono::seconds(45)));
```

`arm()` — 재시도가 들어 있다:

```cpp
// wait_until_ready() 이후에도 is_armable 플래그가 잠깐 깜빡일 수 있어서
// arm 명령이 거부될 수 있다. 받아들여질 때까지 재시도.
REQUIRE(poll_condition_with_timeout(
	[this]() { return _action->arm() == Action::Result::Success; }, std::chrono::seconds(30)));
```

`execute_mission_and_lose_gps()` — 고장 주입:

```cpp
CHECK(_param->set_param_int("SYS_FAILURE_EN", 1) == Param::Result::Success);
start_and_wait_for_mission_sequence(1);
CHECK(_failure->inject(Failure::FailureUnit::SensorGps, Failure::FailureType::Off, 0) == Failure::Result::Success);
// 위치 보조가 없으므로 blind descend. MAVSDK에 Descend 모드가 없으니 호출자는 disarm만 기다린다.
```

- MAVSDK `Failure` plugin은 MAVLink `MAV_CMD_INJECT_FAILURE` 를 보낸다. PX4 쪽에서는 `src/modules/failure_injection_manager/` 와 commander가 이 명령을 처리하고, 셸 명령 `failure gps off` (`src/systemcmds/failure/failure.cpp`)로도 같은 일을 할 수 있다.
- `SYS_FAILURE_EN=1` 이 없으면 고장 주입이 거부된다. **실기체에서 실수로 고장 주입이 되지 않도록 하는 안전 장치**다.

**(3) 판정의 심장 — `poll_condition_with_timeout()`** (`autopilot_tester.h`)

```cpp
if (_telemetry && _telemetry->attitude_quaternion().timestamp_us != 0) {
	// 연결됨: timeout을 autopilot 시간 기준으로 계산
	const int64_t start_time_us = _telemetry->attitude_quaternion().timestamp_us;
	while (!fun()) {
		std::this_thread::sleep_for(duration_us / check_resolution);   // check_resolution = 100
		// 외부 watchdog이 죽여야 할 수도 있다. 절대 timeout을 안 넣은 이유:
		// 호스트가 바쁘면 PX4가 충분히 빨리 못 돌 수 있기 때문.
		const int64_t elapsed_time_us = _telemetry->attitude_quaternion().timestamp_us - start_time_us;
		if (elapsed_time_us > duration_us.count()) { ... return false; }
	}
} else {
	// 아직 연결 전: 호스트 시간(steady_clock) 기준
}
```

이 함수에서 배울 것 세 가지:

1. **timeout은 기체 시간(sim time)으로 잰다.** `--speed-factor 10` 으로 10배 빠르게 돌려도, 호스트가 느려서 시뮬레이션이 늦어져도 "기체 기준 300초"는 같은 의미다. 벽시계로 재면 CI 부하에 따라 테스트가 흔들린다 (flaky).
2. **폴링 간격은 timeout의 1/100.** 조건 함수(람다)만 바꾸면 무엇이든 기다릴 수 있다.
3. **무한 대기 가능성은 바깥 watchdog(러너의 `timeout_min`, CI의 `timeout-minutes: 45`)이 막는다.** 층마다 timeout이 있다.

`CHECK` 와 `REQUIRE` 의 차이도 중요하다. `CHECK` 는 실패를 기록하고 계속, `REQUIRE` 는 그 테스트를 즉시 중단한다. 예: `wait_until_ready()` 는 `CHECK`, `arm()`/`wait_until_disarmed()` 는 `REQUIRE`.

**(4) 그 밖에 테스트가 쓰는 helper** — `wait_until_hovering()`, `wait_until_altitude(rel_alt, timeout, delta)`, `wait_until_fixedwing()`, `check_home_within(radius)`(ground truth 기준), `prepare_square_mission()`, `prepare_straight_mission()`, `execute_mission()`(시작 3초 내 수락 + 500초 내 완료). 미션 업로드 뒤 `sleep_for(1s)` 에는 "PX4가 미션이 생겼다는 걸 알아차릴 시간" 주석이 있다. **"알려진 대기"를 주석과 함께 남기는** 습관.

> ground truth: SIH/Gazebo는 시뮬레이터의 **실제 위치**를 따로 내보낸다. 테스트는 EKF 추정 위치가 아니라 ground truth로 "정말 홈에 돌아왔나"를 판정할 수 있다. 실기체/HIL에는 없는 오라클이다.

### 5.3 러너 · 설정 · CI

**설정 파일** — `test/mavsdk_tests/configs/sih-sitl.json` (발췌)

```text
{
  "mode": "sitl",
  "model_prefix": "sihsim_",
  "mavlink_connection": "udpin://0.0.0.0:14540",
  "tests": [
    { "model": "quadx", "test_filter": "[multicopter],[offboard],[offboard_attitude]", "timeout_min": 10 },
    { "model": "quadx", "test_filter": "[offboard_attitude]", "timeout_min": 10,
      "env": { "PX4_PARAM_EKF2_EN": 0, "PX4_PARAM_ATT_EN": 1 } },
    { "model": "hex", "test_filter": "[motor_failure_injection]", "timeout_min": 15 },
    { "model": "standard_vtol", "test_filter": "[vtol]", "timeout_min": 10 },
    ...
  ]
}
```

설정이 하는 일: **(기체 모델) × (테스트 태그 필터) × (파라미터/환경) × (timeout)** 을 데이터로 적는다. 같은 테스트 코드를 "EKF2 끄고 attitude estimator Q로" 다시 돌리는 것도 JSON 한 블록이다. 이것이 **시나리오 라이브러리와 구성 매트릭스를 분리**하는 방식이다.

**러너** — `test/mavsdk_tests/mavsdk_test_runner.py` (+ `integration_test_runner/test_runner.py`, `process_helper.py`, `logger_helper.py`)

- 옵션: `--log-dir`, `--speed-factor`, `--iterations`(반복 실행 — flaky 탐지), `--abort-early`, `--gui`, `--model`, `--case`, `--debugger`, `--upload`(로그를 Flight Review로 업로드), `--verbose`, `--build-dir`.
- 러너가 PX4 SITL 프로세스를 띄우고, 테스트 바이너리를 Catch2 필터로 실행하고, 각 테스트마다 PX4 로그를 모은다.

**CI** — `.github/workflows/sitl_tests.yml`

| 매트릭스 모델 | 홈 위치 | 빌드 타입 |
|---|---|---|
| quadx | 알래스카 (59.6°N) | AddressSanitizer |
| hex | 취리히 | ThreadSanitizer |
| standard_vtol | 취리히 | push면 Coverage, PR이면 RelWithDebInfo |
| xvert | 취리히 | UndefinedBehaviorSanitizer |

단계: `make px4_sitl_default` → MAVSDK `.deb` 설치(버전은 `test/mavsdk_tests/MAVSDK_VERSION`, 확인 시점 3.17.2) → `DONT_RUN=1 make px4_sitl_default mavsdk_tests` → core dump 설정 → `mavsdk_test_runner.py --speed-factor 10 --abort-early --model <m> test/mavsdk_tests/configs/sih-sitl.json` (45분 제한) → **실패 시 `.log`, `.ulg` 업로드** → core 파일이 있으면 `gdb ... -ex "thread apply all bt"` → coredump 업로드.

배울 점:

- **홈 위치를 바꿔 가며** 돌린다 (고위도 알래스카 — 자기장 · 좌표 변환 경계 조건).
- **sanitizer를 모델마다 하나씩** 배정해 CI 시간을 늘리지 않고 메모리/스레드/UB 오류를 잡는다.
- 실패 아티팩트(로그 · core · 백트레이스)를 **자동으로** 남긴다. 재현을 사람에게 맡기지 않는다.

### 5.4 SITL — SIH와 Gazebo

**SIH (Simulation-In-Hardware)** — `src/modules/simulation/simulator_sih/` (`sih.cpp`, `aero.hpp`, `sih_params.yaml`)

- PX4 안의 모듈로 도는 **가벼운 내장 물리 모델**이다. 외부 시뮬레이터 프로세스가 필요 없다.
- 실행 (문서 `docs/en/sim_sih/index.md`): `make px4_sitl_sih sihsim_quadx`. 그 밖에 `sihsim_hex`, `sihsim_airplane`, `sihsim_xvert`, `sihsim_standard_vtol`, `sihsim_rover_ackermann`.
- 속도: `PX4_SIM_SPEED_FACTOR=10 make px4_sitl_sih sihsim_quadx`. `sih.cpp` 에 `ENABLE_LOCKSTEP_SCHEDULER` 분기와 `lockstep_loop()` 가 있다. **lockstep** = 시뮬레이터가 시간을 한 스텝 진행해야 펌웨어도 그만큼 진행. 결정적(deterministic)에 가까운 재현이 된다.
- 다중 기체: `./Tools/simulation/sitl_multiple_run.sh 3 sihsim_quadx px4_sitl_sih`.
- **SIH on hardware**: `SYS_HITL=2` 로 실제 FC 안에서 SIH를 돌린다 (모터 출력은 내보내지 않음). PC 없이 FC 단독으로 "가짜 비행" — 보드 수준 smoke test에 쓸 수 있다 [확인 필요: 안전 설정 세부].
- MAVSDK CI가 Gazebo 대신 **SIH를 쓴다**는 점이 중요하다. 그래픽 · 외부 의존성이 없어 CI가 빠르고 안정적이다.

**Gazebo (새 gz)** — `src/modules/simulation/gz_bridge/`, 모델은 서브모듈 `Tools/simulation/gz` (= `PX4/PX4-gazebo-models`)

- 실행: `make px4_sitl gz_x500` (기체 x500, `SYS_AUTOSTART` 4001). 변형: `gz_x500_depth`, `gz_x500_vision`, `gz_x500_lidar_down` 등.
- 카메라 · 깊이 · 라이다가 필요한 **인지(perception) / companion 테스트**에 적합하다.
- Gazebo Classic(`Tools/simulation/gazebo-classic/`)은 구버전이지만 HITL은 아직 여기만 지원한다 (5.5절).

**MAVLink 기반 외부 시뮬레이터** — `src/modules/simulation/simulator_mavlink/SimulatorMavlink.cpp` 가 `HIL_SENSOR`, `HIL_GPS`, `HIL_STATE_QUATERNION` 을 받고 `HIL_ACTUATOR_CONTROLS` 를 보낸다. jMAVSim, Gazebo Classic, HITL이 이 경로다. 그 밖의 가짜 센서 모듈: `sensor_gps_sim`, `sensor_baro_sim`, `sensor_mag_sim`, `sensor_airspeed_sim`, `battery_simulator`, `pwm_out_sim`.

| | SIH | Gazebo (gz) | Gazebo Classic |
|---|---|---|---|
| 프로세스 | PX4 안 | 별도 gz 서버 | 별도 gzserver |
| 센서 | IMU/GPS/baro/mag 수준 | 카메라 · 라이다 등 풍부 | 풍부 |
| CI 적합성 | 높음 (공식 MAVSDK CI) | 중간 (GPU/의존성) | 낮아지는 중 |
| HITL | SIH on hardware (`SYS_HITL=2`) | 지원 안 함 [확인 필요] | HITL 지원 (`SYS_HITL=1`) |
| 용도 | 비행 로직 · failsafe 회귀 | 인지 · companion · 시각화 | HITL, 레거시 |

### 5.5 HITL

`docs/en/simulation/hitl.md` 의 핵심 (실제 문구 기반):

- 경고: **HITL은 community supported** — "현재 PX4 버전에서 동작하지 않을 수도 있다".
- 정의: 일반 PX4 펌웨어를 **실제 FC 하드웨어**에서 "HITL 모드"로 돌린다. 시뮬레이터(Gazebo Classic)는 PC에서 돌고, FC와 USB/UART로 연결된다. 시뮬레이터가 QGroundControl과 PX4 사이 MAVLink 게이트웨이 역할을 한다.
- 호환 airframe: HIL Quadcopter X (`SYS_AUTOSTART` 1001), HIL Standard VTOL QuadPlane (1002).
- HITL 구성에서는 **실제 센서를 시작하지 않는다**. commander · sensors 같은 핵심 모듈이 시작 시 HITL 모드로 일부 기능을 우회한다.
- 문서 결론: "HITL은 실제 하드웨어에서 표준 펌웨어를 돌리지만, **SITL이 오히려 표준 시스템 코드를 더 많이 실행한다**."
- 이더넷이 있는 보드는 UDP로도 연결할 수 있다 (예: Raspberry Pi, `posix-configs/rpi/px4_hil.config`).

```text
 ┌──────────── PC ─────────────┐        USB/UART (MAVLink)        ┌──── FC (실제 보드) ────┐
 │ Gazebo Classic (물리)        │ ── HIL_SENSOR / HIL_GPS ───────▶ │ PX4 (SYS_HITL=1)        │
 │   └ MAVLink 게이트웨이        │ ◀── HIL_ACTUATOR_CONTROLS ────── │ 센서 드라이버 미기동      │
 │ QGroundControl (UDP)         │                                   │ 스케줄링 · 메모리 · CPU 실제│
 └─────────────────────────────┘                                   └─────────────────────────┘
```

> ⚠️ Gemini 정정: Gemini(19~20쪽)는 "PX4는 테스트 PC가 MAVLink `HIL_SENSOR` 패킷만 쏴 주면 펌웨어가 알아서 가상 비행 모드로 잘 돌아간다"고 했다. 실제로는 (1) HITL airframe / `SYS_HITL=1` 설정이 필요하고, (2) 공식 지원은 Gazebo Classic 조합뿐이며 community supported 상태이고, (3) **센서 드라이버 · SPI 타이밍 · 실제 IMU 노이즈는 우회되므로 검증되지 않는다.** HITL이 검증하는 것은 "실제 MCU에서의 CPU 부하 · 스케줄링 · 메모리 · 통신 경로"이지 센서 드라이버가 아니다. 센서 경로까지 보려면 Gemini가 말한 "SPI 센서 에뮬레이터" 같은 진짜 HIL 벤치가 필요하다 ([R08](2026-10-24_R08_hil_bench_design_instruments.md)).

| 검증 대상 | SITL (SIH) | HITL (PX4 문서 방식) | HIL 벤치 (센서 에뮬레이션) |
|---|---|---|---|
| 비행 로직 · failsafe | ✅ | ✅ | ✅ |
| 실제 MCU CPU 부하 · 스택 · 타이밍 | ❌ | ✅ | ✅ |
| 센서 드라이버 · SPI/I2C 타이밍 | ❌ | ❌ (우회) | ✅ |
| 모터 출력 신호 (PWM/DShot) | ❌ | 부분 [확인 필요] | ✅ (로직 분석기) |
| CI 자동화 난이도 | 낮음 | 중간 (보드 · USB 관리) | 높음 (계측기 · 전원 · 안전) |

### 5.6 로그 기반 오라클 — ULog · pyulog · Flight Review · replay

- CI는 실패 시 `.ulg` 를 아티팩트로 올린다. 사람이 Flight Review에 올려 본다. 러너의 `--upload` 는 이 업로드를 자동화한다.
- **pyulog** 로 로그를 코드에서 읽으면 "비행 후 판정"이 가능하다. 예: 미션 동안 `vehicle_local_position` 의 속도 최대값, `vehicle_status` 의 nav_state 전이 순서, `failsafe_flags` 가 켜진 시각과 `vehicle_status` 가 바뀐 시각의 차이(=failsafe 반응 시간).
- Flight Review가 보여 주는 대표 지표: 비행 모드 구간, 자세 · 각속도 추종(setpoint vs 실제), 진동(가속도 FFT, actuator FFT), 배터리 전압/전류, CPU 부하, 로그 dropout [확인 필요: 현재 페이지 구성].
- `docs/en/dev_log/flight_log_analysis_statistical.md` 는 **여러 로그를 통계로 분석**하는 방법을 다룬다 — 회귀 비교에 쓸 수 있는 내용 [확인 필요: 내용 상세].
- **replay**: `src/modules/replay/`. 문서(`docs/en/debug/system_wide_replay.md`): `export replay=<절대경로.ulg>` 후 `make px4_sitl_default`. EKF만 다시 돌리려면 `export replay_mode=ekf2`. 끝나면 `unset replay; unset replay_mode`. 로그를 남길 때 replay용 `SDLOG_PROFILE` 비트가 필요하다.

pyulog로 "failsafe 반응 시간"을 재는 스케치 (실행 안 해 봄 — 토픽 · 필드 이름은 [확인 필요]):

```python
# pip install pyulog  (가이드 — 실행 안 해 봄)
from pyulog import ULog

def first_true_time(ulog, topic, field):
    d = ulog.get_dataset(topic).data
    for t, v in zip(d["timestamp"], d[field]):
        if v:
            return t
    return None

def failsafe_reaction_us(path):
    ulog = ULog(path, ["failsafe_flags", "vehicle_status"])
    t_flag = first_true_time(ulog, "failsafe_flags", "manual_control_signal_lost")
    st = ulog.get_dataset("vehicle_status").data
    for t, fs in zip(st["timestamp"], st["failsafe"]):
        if t >= t_flag and fs:
            return t - t_flag
    return None
```

---

## 6. ArduPilot의 테스트 생태계

### 6.1 층 구조

| 층 | 무엇 | 어디 | 실행 |
|---|---|---|---|
| 단위 (gtest) | 라이브러리 함수 | `libraries/*/tests/` (wscript 있는 디렉터리 27개), 예: `libraries/AP_Math/tests/test_crc8.cpp` | `./waf configure --board sitl` → `./waf check` (빌드 + 실행), `./waf tests`(빌드만) |
| **SITL autotest** | 차량별 시나리오 수백 개 | `Tools/autotest/arducopter.py` 등 | `Tools/autotest/autotest.py build.Copter test.Copter.<Test>` |
| Replay | EKF 재실행 결과의 비트 단위 일치 | `Tools/Replay/`, `check_replay.py` | autotest `Replay` 테스트 + CI `test_replay.yml` |
| 예제 · 스크립트 | `libraries/*/examples`, Lua 스크립트 | `test_scripting.yml`, `test_scripts.yml` | CI |
| 보드 빌드 | ChibiOS 보드들 빌드 · 크기 | `test_chibios.yml`, `test_size.yml` | CI |
| ASAN | 단위 테스트 ASan | `test_unit_tests.yml` (`unit-tests-asan`) | CI |
| 기타 | DDS, CAN periph, Linux SBC, Renode 에뮬레이션, coverage | `test_dds.yml`, `test_sitl_periph.yml`, `test_linux_sbc.yml`, `test_renode.yml`, `test_coverage.yml` | CI |

Renode(`test_renode.yml`, `Tools/renode/`)는 **MCU 에뮬레이터 위에서 실제 펌웨어 바이너리**를 돌리는 방향이다. SITL(호스트 빌드)과 HITL(실제 보드) 사이의 층이다 [확인 필요: 어떤 보드 · 무엇을 검증하는지].

### 6.2 autotest의 구조

```text
 Tools/autotest/autotest.py               ← 진입점. "build.Copter", "test.Copter.ThrottleFailsafe" 같은 step 실행
     │   tester_class_map = { "test.Copter": AutoTestCopter, "test.CopterTests1a": ..., ... }
     ▼
 vehicle_test_suite.py : class TestSuite(abc.ABC)      ← 공통 기반 (약 2만 줄)
     │   SITL 프로세스 관리, MAVProxy 연결, pymavlink self.mav
     │   wait_* helper, set_parameter, set_rc, context_push/pop, run_tests …
     │   공통 테스트들 (CRSF, MSP_DJI, FRSky 텔레메트리 등)
     ▼
 arducopter.py : class AutoTestCopter(TestSuite)       ← Copter 전용 helper + 테스트 메서드 (약 2.4만 줄)
     │   tests1a() ~ tests2d() : 테스트 목록을 9묶음으로 쪼갬 (CI 병렬화)
     ▼
 class AutoTestCopterTests1a(AutoTestCopter): def tests(self): return self.tests1a()
```

- `autotest.py` 의 step 이름 정규식: `((fly|drive|dive|test)[.][^.]+)[.](.*)` → `test.Copter.ThrottleFailsafe` 처럼 **테스트 하나만** 지정할 수 있다.
- `build.Copter` → `bin/arducopter` SITL 바이너리를 빌드한다.
- `autotest.py` 의 주석에 묶음별 CI 소요 시간이 적혀 있다: `test.CopterTests1a ~14m on CI`, `PlaneTests1a ~12m`, `QuadPlaneTests1a ~11m`.
- 테스트 정의 객체 `class Test`: `function`, `kwargs`, `attempts`(재시도 횟수), `speedup`. **docstring이 없으면 `ValueError`** — 모든 테스트는 한 줄 설명을 가져야 한다.
- `disabled_tests()` 는 끈 테스트와 이유를 dict로 돌려준다. 예: `"GroundEffectCompensation_takeOffExpected": "Flapping"`, `"SMART_RTL_EnterLeave": "Causes a panic"`.
- 규모: `arducopter.py` 의 `tests1a()~tests2d()` 목록 항목이 grep 기준 약 540줄 (일부는 `Test(...)` 래퍼). Plane · Rover · Sub · Helicopter · QuadPlane도 각자 파일이 있다.

### 6.3 테스트 한 개가 도는 순서 — `run_one_test_attempt()`

`vehicle_test_suite.py` 의 실제 흐름 요약:

1. 테스트별 로그 파일 `<log_name>-<TestName>.txt` 를 열고 stdout과 MAVProxy 로그를 함께 기록 (`TeeBoth`).
2. `context_push()` — 지금 상태(파라미터, message hook, SITL 명령줄 등)를 스택에 저장.
3. (처음 한 번) **"깨끗한 파라미터" 스냅샷** — 나중에 테스트가 파라미터를 새게 했는지(leak) 검사.
4. `check_rc_defaults()`, `reboot_sitl_before_test()`, 기본 모드로 전환, 현재 waypoint 0, MAVLink 버퍼 비우기.
5. `test.speedup` 이 있으면 그 테스트만 시뮬레이션 속도 변경.
6. **테스트 함수 실행.** 예외가 나면 = 실패. 스택 트레이스와 "가장 최근 onboard 로그 파일 경로"를 출력.
7. 끝나면 `wait_heartbeat()` — 응답이 없으면 **SITL이 죽은 것**. `dumpstack.sh`/`dumpcore.sh` 로 프로세스 상태를 남기고 실패.
8. `context_pop()` — 저장한 파라미터 · hook을 복원. 복원이 실패해도 실패.
9. **아직 무장 상태면 `ArmedAtEndOfTestException("Still armed at end of test")`** — 강제 disarm, 필요하면 SITL 재시작.
10. `run_one_test()` 는 `attempts` 만큼 재시도한다. 재시도 로그는 `-retry-N.txt` 로 따로 남는다.

> 이 흐름이 Neros 플랫폼에 주는 교훈은 8절에 정리. 요점: **테스트 사이 격리(context), 테스트 후 불변식 검사(살아 있나 · 무장 해제됐나), 재시도의 가시성**.

### 6.4 실제 테스트 읽기 — `ThrottleFailsafe` (`arducopter.py`)

메서드 정의: `def ThrottleFailsafe(self, side=60, timeout=360):` / docstring `'''Test Throttle Failsafe'''`. 하위 테스트(subtest) 여러 개로 나뉜다. 앞부분 실제 코드:

```python
self.start_subtest("If you haven't taken off yet RC failure should be instant disarm")
self.change_mode("STABILIZE")
self.set_parameter("DISARM_DELAY", 0)
self.wait_ready_to_arm()
self.arm_vehicle()
self.set_parameter("SIM_RC_FAIL", 1)
self.disarm_wait(timeout=1)
self.set_parameter("SIM_RC_FAIL", 0)
self.set_parameter("DISARM_DELAY", 10)

# failsafe 꺼짐(FS_THR_ENABLE=0)이면 아무 행동도 없어야 한다
self.start_subtest("Radio failsafe disabled test: FS_THR_ENABLE=0 should take no failsafe action")
self.set_parameter('FS_THR_ENABLE', 0)
self.set_parameter('FS_OPTIONS', 0)
self.takeoffAndMoveAway()
self.set_parameter("SIM_RC_FAIL", 1)
self.delay_sim_time(5, reason="radio failsafe to not trigger")
self.wait_mode("ALT_HOLD")
self.set_parameter("SIM_RC_FAIL", 0)
...
# RTL로 가서 완료되는지
self.start_subtest("Radio failsafe RTL with no options test: FS_THR_ENABLE=1 & FS_OPTIONS=0")
self.set_parameter("SIM_RC_FAIL", 1)
self.wait_mode("RTL")
self.wait_rtl_complete()
self.set_parameter("SIM_RC_FAIL", 0)
```

subtest 목록 (실제 문자열에서 정리):

| # | subtest | 자극 | 기대 (판정 helper) |
|---|---|---|---|
| 1 | 이륙 전 RC 끊김 → 즉시 disarm | `SIM_RC_FAIL=1` | `disarm_wait(timeout=1)` |
| 2 | `FS_THR_ENABLE=0` → 아무 일 없음 | RC 끊김 5초 | `wait_mode("ALT_HOLD")` 유지 |
| 3 | 복구 → 모드 스위치 동작 | RC 끊김 → 복구 → `set_rc(5, 1050/1950)` | `wait_mode("RTL")` → `CIRCLE` → `STABILIZE` |
| 4 | `FS_THR_ENABLE=1` → RTL 완료 | RC 끊김 | `wait_mode("RTL")`, `wait_rtl_complete()` |
| 5 | `FS_THR_ENABLE=3` → Land | RC 끊김 | `wait_mode("LAND")`, `wait_landed_and_disarmed()` |
| 6 | `=4` SmartRTL→RTL | RC 끊김 | `wait_mode("SMART_RTL")`, `wait_disarmed()` |
| 7 | `=5` SmartRTL→Land | RC 끊김 | 위와 같음 |
| 8 | GPS 없음 + RC 끊김 → RTL 불가 → Land | `SIM_GPS1_ENABLE=0` + RC 끊김 | `wait_mode("LAND")`, 복구 후 `wait_ekf_happy()` |
| 9 | SmartRTL 경로 없음 → RTL | GPS 끊었다 복구 | `wait_statustext("SmartRTL deactivated: bad position")` |

이 테스트에서 배울 것:

- **자극은 파라미터 하나** (`SIM_RC_FAIL`, `SIM_GPS1_ENABLE`). SITL 시뮬레이터가 그 파라미터를 보고 RC 펄스를 끊거나 GPS를 끈다 (`libraries/SITL/SITL.cpp` 의 `RC_FAIL` @Values: 0 Disabled, 1 No RC pulses, 2 All channels neutral except throttle 950 µs). 테스트 코드는 시뮬레이터 내부를 몰라도 된다.
- **설정 공간을 표처럼 훑는다** (`FS_THR_ENABLE` 0/1/3/4/5 × GPS 유무). Neros의 Betaflight failsafe 설정(stage 1/2, 절차)도 같은 식으로 훑을 수 있다.
- **"아무 일도 없어야 한다"도 테스트한다** (subtest 2). 기능 꺼짐 상태의 무반응 검증은 SSD에서도 자주 빠지는 칸이다.
- **복합 고장** (GPS + RC). failsafe의 fallback 체인(RTL 불가 → Land)을 검증한다.
- **STATUSTEXT 문구로 판정** (subtest 9). 펌웨어 메시지 문구가 바뀌면 테스트가 깨지므로, 문구는 사실상 API가 된다.

### 6.5 Pass/Fail은 어떻게 정해지나 — `wait_*` 와 sim-time timeout

ArduPilot autotest에는 **assert 함수가 거의 없다.** 대신 모든 판정이 "조건이 timeout 안에 성립하나, 안 하면 예외"다.

`wait_mode()` (실제 코드):

```python
def wait_mode(self, mode, timeout=60):
    """Wait for mode to change."""
    self.progress("Waiting for mode %s" % mode)
    tstart = self.get_sim_time()
    # poll=False: 예약된 heartbeat에 맞춰 루프 (폴링 왕복 속도로 돌며 로그 폭주 방지)
    while not self.mode_is(mode, drain_mav=False, poll=False):
        custom_num = self.mav.messages['HEARTBEAT'].custom_mode
        self.progress("mav.flightmode=%s Want=%s custom=%u" % (
            self.mav.flightmode, mode, custom_num))
        if (timeout is not None and
                self.get_sim_time_cached() > tstart + timeout):
            raise WaitModeTimeout("Did not change mode")
    self.progress("Got mode %s" % mode)
```

`wait_rtl_complete()` (Copter, 실제 코드 요약): `GLOBAL_POSITION_INT` 를 계속 받아 고도(`alt <= alt_max`, 기본 1 m)와 홈 거리(`< distance_max`, 기본 10 m)를 확인. **사후조건은 "disarm 됐는가"**. disarm 됐는데 홈이 아니면 `NotAchievedException("Did not get home")`, timeout(기본 250 s)이면 `AutoTestTimeoutException("Did not get home and disarm")`.

`wait_and_maintain_range()` 계열 (`wait_altitude`, `wait_groundspeed`, `wait_roll`, `wait_pitch` 등이 모두 이걸 쓴다):

- `current_value_getter` (보통 MAVLink 메시지에서 값 하나) + `validator` (범위 판정) + `timeout` + **`minimum_duration`**.
- `minimum_duration` 이 핵심이다. **값이 범위에 "들어온 순간"이 아니라 "N초 동안 유지"돼야 통과.** 진동하면서 범위를 스치는 제어기를 걸러 낸다.
- timeout이면 `AutoTestTimeoutException("Failed to attain %s want %s, reached %s")` — 실패 메시지에 **원하는 값과 도달한 평균값**이 들어간다. 디버깅이 메시지만으로 시작된다.
- 1초마다 진행 상황을 `progress()` 로 찍는다 (`(maintain=3.2/5.0)` 같은 형식).

예외 클래스 계층 (`vehicle_test_suite.py`): `ErrorException` ← `AutoTestTimeoutException` ← `WaitModeTimeout`, `WaitAltitudeTimout`, `WaitGroundSpeedTimeout`, `WaitRollTimeout`, `WaitDistanceTimeout`, `WaitLocationTimeout`, `WaitWaypointTimeout`, `SetRCTimeout`, `MsgRcvTimeoutException`. 그리고 `NotAchievedException`, `PreconditionFailedException`, `ArmedAtEndOfTestException`. **실패 종류가 타입으로 구분**되어 리포트 집계가 쉽다.

수치 오라클 예 — `RTLSpeed` (실제 코드):

```python
def RTLSpeed(self):
    """Test RTL Speed parameters"""
    rtl_speed_ms = 7
    wpnav_speed_ms = 4
    wpnav_accel_mss = 3
    tolerance = 0.5
    self.load_mission("copter_rtl_speed.txt")
    self.set_parameters({'WP_ACC': wpnav_accel_mss, 'RTL_SPEED_MS': rtl_speed_ms, 'WP_SPD': wpnav_speed_ms})
    self.change_mode('LOITER')
    self.wait_ready_to_arm()
    self.arm_vehicle()
    self.change_mode('AUTO')
    self.set_rc(3, 1600)
    self.wait_altitude(19, 25, relative=True)
    self.wait_groundspeed(wpnav_speed_ms-tolerance, wpnav_speed_ms+tolerance, minimum_duration=20, timeout=30)
    self.change_mode('RTL')
    self.wait_groundspeed(rtl_speed_ms-tolerance, rtl_speed_ms+tolerance, minimum_duration=5)
    ...
```

**파라미터로 기대값을 정하고 → 텔레메트리로 측정하고 → 허용 오차 + 유지 시간으로 판정**한다. 사양(파라미터) 자체가 오라클이 된다.

> ⚠️ Gemini 정정: Gemini(16쪽)는 ArduPilot autotest를 "pymavlink로 MAVLink 패킷을 **저수준에서 직접 파싱하고 쏘면서** 가상 드론을 가혹하게 굴리는 방식"이라고 했다. 패킷 파싱은 pymavlink(`mavutil`)가 하고, 테스트 코드는 `GLOBAL_POSITION_INT.relative_alt` 같은 **필드 단위 객체**를 다룬다. 그 위에 `wait_*`, `set_rc`, `change_mode`, `takeoff`, `load_mission` 같은 **수백 개의 고수준 helper**가 있다. "저수준"이라기보다 "MAVSDK 대신 자체 helper 라이브러리를 쓴다"가 정확하다. 또 SITL 연결은 기본적으로 **MAVProxy**를 거친다 (`mavproxy_options()`).

### 6.6 SITL — 물리 모델 · sim_vehicle.py · MAVProxy

**내장 물리 모델** — `libraries/SITL/`

- 차량 모델: `SIM_Multicopter.cpp` + `SIM_Frame.cpp`(프레임 · 모터 배치 · 추력 모델), `SIM_Helicopter.cpp`, `SIM_Plane.cpp`, `SIM_QuadPlane.cpp`, `SIM_Rover.cpp`, `SIM_Submarine.cpp`, `SIM_Blimp.cpp`, `SIM_BalanceBot.cpp`.
- 외부 시뮬레이터 연결: `SIM_Gazebo.cpp`, `SIM_JSBSim.cpp`, `SIM_FlightAxis.cpp`(RealFlight), `SIM_XPlane.cpp`, `SIM_Webots.cpp`, `SIM_Morse.cpp`, `SIM_AirSim.cpp`, `SIM_Scrimmage.cpp`, `SIM_SilentWings.cpp`, **`SIM_JSON.cpp`**(아무 물리 엔진이나 JSON over UDP로 붙이는 범용 인터페이스).
- **장치 시뮬레이터가 매우 많다**: `SIM_CRSF.cpp`, `SIM_ELRS.cpp`, `SIM_FETtecOneWireESC.cpp`, `SIM_Battery.cpp`, `SIM_BattMonitor_SMBus_*.cpp`, `SIM_ADSB*.cpp`, `SIM_Airspeed_DLVR.cpp`, `SIM_AS5600.cpp`(I2C 엔코더), `SIM_DroneCANDevice.cpp`, `SIM_EFI_*.cpp` 등. 즉 **센서/장치 프로토콜 수준의 가짜 장치**를 SITL 안에 둔다. Neros가 ELRS/CRSF 경로를 SITL로 검증하려면 참고할 만하다.
- `SIM_*` 파라미터로 고장 주입: `SIM_RC_FAIL`, `SIM_GPS1_ENABLE`, 그 밖에 센서 노이즈 · 글리치 · 바람 등 [확인 필요: 전체 목록은 `SITL.cpp` 의 `@Param`].

**sim_vehicle.py** — `Tools/autotest/sim_vehicle.py` ("Framework to start a simulated vehicle and connect it to MAVProxy")

- 주요 옵션 (소스 `add_option` 에서 확인): `-v/--vehicle`, `-f/--frame`, `--vehicle-binary`, `-L/--location`, `-w/--wipe-eeprom`, `-S/--speedup`, `--out`, `--map`, `--console`.
- 사람이 손으로 SITL을 띄워 MAVProxy 콘솔에서 `mode guided`, `arm throttle`, `takeoff 10` 같은 명령을 치며 탐색할 때 쓴다. autotest는 이것과 별개로 자체적으로 SITL을 띄운다.

**pymavlink 사용 형태** (autotest 안): `self.mav` 가 `mavutil` 연결이다. `self.assert_receive_message('VFR_HUD', timeout=...)`, `self.mav.messages['HEARTBEAT'].custom_mode`, `mav.recv_match(type='SYS_STATUS', blocking=True, timeout=1)` 같은 식이다. 메시지 hook(`install_message_hook_context`)으로 **비행 내내 특정 메시지를 감시하는 불변식**(예: "이 테스트 동안 고도가 절대 X 아래로 가면 안 됨")도 건다.

### 6.7 Replay — 로그로 EKF를 재실행해 비트 단위 비교

- 도구: `Tools/Replay/` (`Replay.cpp`, `LogReader.cpp`, `DataFlashFileReader.cpp`, `check_replay.py`, `CheckLogs.py`). `AP_DAL` 이 EKF 입력을 로그로 남겨 두기 때문에 가능하다.
- autotest `Replay` 테스트 (`arducopter.py`): 로그 비율 제한을 풀고(`LOG_DARM_RATEMAX=0`, `LOG_FILE_RATEMAX=0`, `LOG_FILE_BUFSIZE=32767`) 센서 조합별 비행을 한다: `GPS`, `GPSForYaw`, `WindAndAirspeed`, `BodyOdom`, `Beacon`, `OpticalFlow`.
- 각 로그에 대해: **dropped block이 없는지 먼저 확인** (`assert_log_has_no_dropped_blocks` — 빠진 로그로는 충실한 재실행이 불가) → Replay 실행 → `check_replay.check_log()`.
- `check_replay.py` 의 판정: EKF 메시지(EKF3 `XKF1`~`XKF5`, `XKFS`, `XKQ` 등, EKF2 `NKF*`)에서 **코어 번호 `C < 100` 은 원래 비행, `C >= 100` 은 재실행 결과**. 같은 메시지 · 같은 코어끼리 **모든 필드가 정확히 같아야** 한다 (`accuracy` 옵션이 0이면 완전 일치). 처리한 메시지 수가 0이거나 원본과 100개 넘게 차이 나도 실패.
- CI: `test_replay.yml` (GCC · clang 두 툴체인), `build_ci.sh` 의 `replay` 타깃은 `./waf replay` 와 `tool/AP_DAL_Standalone` 빌드도 확인한다.

왜 강력한가: EKF 코드를 고친 PR이 **같은 입력에서 같은 출력**을 내는지(리팩터링) 또는 **어디서 달라지는지**(의도한 변경)를 로그만으로 증명한다. 비행 없이, 시뮬레이터 없이, 결정적으로.

### 6.8 보너스 — autotest 안의 CRSF · MSP 테스트

`vehicle_test_suite.py` 에는 `class CRSF(Telem)`, `class MSP_Generic(Telem)`, `class MSP_DJI`, `class FRSky*`, `class LTM`, `class DEVO`, `class IBus*` 가 있다. 즉 **ArduPilot autotest는 Betaflight 계열 프로토콜(CRSF, MSP)도 테스트한다**.

`CRSF` 테스트 (실제 코드 요약):

```python
def CRSF(self):
    '''Test RC CRSF'''
    self.context_push()
    self.set_parameter("SERIAL5_PROTOCOL", 23)            # serial5 = RC 입력
    port = self.spare_network_port()
    self.customise_SITL_commandline(["--serial5=tcp:%u" % port])   # SITL UART를 TCP로
    crsf = CRSF(("127.0.0.1", port))
    crsf.connect()
    crsf.write_data_id(crsf.dataid_vtx_frame)             # 미리 만든 CRSF 바이트열 주입
    self.delay_sim_time(5, reason="VTX frame to be processed")
    ...
    self.context_pop()
    self.disarm_vehicle(force=True)
    self.reboot_sitl()
```

- 패턴: **SITL의 UART를 TCP 소켓으로 바꾸고, 테스트가 그 소켓에 원시 프로토콜 바이트를 쓴다.** 하드웨어 없이 파서를 실전 경로로 시험한다.
- 주입 바이트 예: `[0xC8, 0x8, 0xF, 0xCE, 0x30, ...]` (0xC8 = CRSF FC 주소).
- 한계: 이 테스트는 "주입 후 죽지 않음"만 본다 (명시적 결과 검증 없음). 6.3절의 "테스트 후 heartbeat 확인"이 사실상 판정이다. Neros가 같은 패턴을 쓴다면 **깨진 프레임 · 길이 초과 · CRC 오류를 섞고, 링크 통계 · 채널 값을 실제로 확인**하는 쪽으로 강화해야 한다 ([R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)).

---

## 7. Gemini의 "MAVSDK 중심 vs autotest 중심" — 무엇이 맞고 무엇을 고쳐야 하나

Gemini(15~17쪽)의 요지: "PX4는 MAVSDK라는 고수준 API(C++/Python/Swift)로 객체지향 통합 테스트를 짜고 ROS 2 · companion 연동 검증에 강하다. ArduPilot은 `Tools/autotest` 의 Python 스크립트가 pymavlink로 저수준 MAVLink를 다루며 수백 가지 엣지 케이스를 검증하는 '교과서'다."

정리하면 **방향은 맞고, 세부가 틀리거나 과장됐다.**

| 주장 | 판정 | 실제 (소스 확인) |
|---|---|---|
| PX4 통합 테스트는 MAVSDK 기반 | ✅ 맞음 | `test/mavsdk_tests/`, `AutopilotTester` 가 MAVSDK plugin 사용 |
| C++/Python/Swift로 짠다 | ⚠️ 부분 | MAVSDK는 여러 언어 binding이 있지만 **PX4 저장소의 테스트는 C++ + Catch2만** |
| 객체지향 방식 | ⚠️ 과장 | `AutopilotTester` helper 클래스 + 절차형 `TEST_CASE`. ArduPilot도 클래스 상속(TestSuite → AutoTestCopter) 구조라 "객체지향"은 차이가 아님 |
| PX4는 companion/ROS 2 검증에 특화 | ⚠️ 부분 | ROS 2 통합 테스트가 따로 있음(`ros_integration_tests.yml`). 하지만 MAVSDK 테스트 대부분은 미션 · failsafe · VTOL 같은 **비행 로직**이다 |
| ArduPilot은 pymavlink "저수준" | ❌ 부정확 | 필드 단위 메시지 객체 + 수백 개 helper (`wait_*`, `set_rc`, `takeoff` …). 저수준은 pymavlink 내부 |
| ArduPilot은 수백 가지 시나리오 | ✅ 맞음 | Copter 목록만 약 540항목 (grep 기준), 9묶음으로 CI 병렬 |
| PX4 테스트는 적고 ArduPilot은 많다 (함의) | ⚠️ 보완 | PX4는 MAVSDK 통합 테스트 수는 적지만(72 `TEST_CASE`) **gtest 단위 테스트(EKF, failsafe 등)와 sanitizer 매트릭스**가 두껍다. 검증 무게가 다른 층에 있다 |
| 둘의 철학이 다르다 | ✅ 맞음 | 아래 표 |

**더 정확한 대비**

| 축 | PX4 | ArduPilot |
|---|---|---|
| 테스트 언어 | C++ (Catch2) | Python |
| 기체와 대화 | MAVSDK (고수준 API, 표준 MAVLink 마이크로서비스) | pymavlink + MAVProxy + 자체 helper |
| 시뮬레이터 | SIH (CI), Gazebo | 내장 `SIM_*` 모델 |
| 고장 주입 | 표준 MAVLink `MAV_CMD_INJECT_FAILURE` (+ `SYS_FAILURE_EN`) | SITL 전용 `SIM_*` 파라미터 |
| timeout 기준 | 기체 시간 (`attitude_quaternion().timestamp_us`) | sim 시간 (`get_sim_time()`) |
| 구성 매트릭스 | JSON 설정 (모델 × 태그 × env × timeout) | 차량별 클래스 + tests1a~2d 묶음 |
| 테스트 격리 | 테스트마다 PX4 프로세스 재시작 (러너) [확인 필요: 세부] | `context_push/pop`, 테스트 전 reboot, 무장 상태 검사 |
| 강점 층 | 단위(gtest) + sanitizer + 적은 수의 통합 | 대규모 SITL 시나리오 + Replay |
| 실기체로 옮기기 | MAVSDK가 표준 MAVLink만 쓰므로 **같은 테스트를 실기체/HITL에 붙이기 쉬움** [추정] | `SIM_*` 고장 주입은 SITL 전용이라 실기체에선 다른 주입 수단 필요 |

> ⚠️ Gemini 정정 (요약): "MAVSDK 중심 vs autotest 중심"은 **테스트를 무엇으로 쓰느냐(라이브러리)의 차이**이지, 하나는 고수준 · 하나는 저수준인 것은 아니다. 두 프로젝트 모두 "SITL + MAVLink + sim-time timeout을 가진 wait-for-condition + 실패 시 로그"라는 같은 골격을 쓴다. Neros 관점에서 더 중요한 차이는 **고장 주입 방식**이다. PX4 방식(표준 MAVLink 명령)은 실기체 · HIL에도 그대로 쓸 수 있고, ArduPilot 방식(SITL 파라미터)은 시뮬레이터 안에서만 쓸 수 있지만 훨씬 다양하다.

---

## 8. Neros식 테스트 플랫폼이 빌려 올 것

Neros는 Betaflight 포크(MSP/CRSF) + PX4 계열(MAVLink)이 섞여 있을 가능성이 높다 [추정]. 두 프로젝트에서 빌려 올 것을 정리한다. 전체 설계는 [R09](2026-10-24_R09_scalable_reusable_test_platform.md).

| # | 빌려 올 것 | 출처 (확인한 코드) | Neros 적용 [추정] |
|---|---|---|---|
| 1 | **기체 추상화** (테스트는 "무엇을"만, 드라이버가 "어떻게") | PX4 `AutopilotTester`, ArduPilot `TestSuite` → `AutoTestCopter` | `Vehicle` 인터페이스 아래 `BetaflightMSP`, `MavlinkVehicle`, `SitlVehicle`, `HilBenchVehicle` |
| 2 | **wait-for-condition helper** (조건 람다 + timeout + 진행 로그) | `poll_condition_with_timeout`, `wait_and_maintain_range` | `wait_until(pred, timeout, clock=vehicle_clock)` 하나로 모든 판정 |
| 3 | **sim/기체 시간 기준 timeout** | 두 프로젝트 모두 | SITL은 sim time, HIL은 FC 타임스탬프(MSP/텔레메트리) 기준. 벽시계는 바깥 watchdog에만 |
| 4 | **유지 시간(minimum_duration)** | `wait_and_maintain_*` | "고도 ±0.5 m를 5초 유지" — 스치는 값으로 통과하지 않게 |
| 5 | **시나리오 라이브러리와 구성 매트릭스 분리** | `sih-sitl.json`, tests1a~2d | YAML: 기체 × 펌웨어 빌드 × 파라미터 세트 × 시나리오 태그 × timeout |
| 6 | **고장 주입을 1급 API로** | MAVSDK `Failure`, `SIM_RC_FAIL` | `inject_rc_loss()`, `inject_gps_loss()` — SITL은 시뮬레이터, HIL은 RF 감쇠기 · 전원 · 신호 차단 ([R08](2026-10-24_R08_hil_bench_design_instruments.md)) |
| 7 | **안전 잠금** | `SYS_FAILURE_EN` | 고장 주입 · 모터 구동 API는 벤치 안전 상태(프롭 없음 · 전류 제한)를 확인해야 열림 |
| 8 | **테스트 격리와 사후 불변식** | `context_push/pop`, `ArmedAtEndOfTestException`, heartbeat 확인 | 테스트 후 반드시: 살아 있나, disarm인가, 파라미터 원복됐나 |
| 9 | **로그 기반 오라클** | `check_replay.py`, pyulog, Flight Review | 비행 후 Blackbox/ULog를 파싱해 반응 시간 · 오버슈트 · 지터 판정 ([R07](2026-10-24_R07_test_methodology_pyramid.md)) |
| 10 | **결정적 재실행 (replay)** | ArduPilot Replay, PX4 replay | 필터 · 추정기 변경을 기록된 IMU 데이터로 bit-exact 비교 |
| 11 | **실패 아티팩트 자동 수집** | PX4 CI의 `.ulg`/core/gdb bt 업로드, autotest 테스트별 로그 | 실패 시 FC 로그 · 시리얼 캡처 · 로직 분석기 파일 · 전원 로그를 한 묶음으로 |
| 12 | **sanitizer · 환경 변형 매트릭스** | ASan/TSan/UBSan × 모델 × 홈 위치 | SITL 빌드에 sanitizer, 위치 · 바람 · 배터리 조건 변형 |
| 13 | **flaky 관리의 가시성** | `#if 0` + 이유, `disabled_tests()` + 이유, `attempts`, `--iterations` | 끈 테스트 목록과 이유를 대시보드에, 재시도 성공은 "통과"가 아니라 "flaky 통과"로 별도 집계 |
| 14 | **프로토콜 주입 via 소켓** | ArduPilot `CRSF` 테스트의 `--serial5=tcp:` | Betaflight SITL의 UART를 소켓으로 → CRSF/MSP 바이트 주입 (가능 여부 [확인 필요], R03) |
| 15 | **문구 · 이벤트를 API로 취급** | `wait_statustext`, PX4 events 구독 | FC 메시지 문구 변경은 테스트 영향 리뷰 대상 |

스케치 — 두 프로젝트의 패턴을 합친 최소 형태 (실행용 코드 아님, 형태만):

```python
import time

class TestTimeout(Exception):
    pass

class Vehicle:                       # 스택 독립 인터페이스
    def clock_s(self) -> float: ...  # SITL: sim time, HIL: FC 타임스탬프
    def mode(self) -> str: ...
    def armed(self) -> bool: ...
    def inject(self, failure: str, on: bool) -> None: ...

def wait_until(v, pred, timeout_s, hold_s=0.0, what="condition", poll_s=0.05):
    t0 = v.clock_s()
    held_since = None
    last = None
    while True:
        now = v.clock_s()
        if now - t0 > timeout_s:
            raise TestTimeout(f"{what}: not met in {timeout_s}s (last={last})")
        ok, last = pred()
        if ok:
            held_since = held_since if held_since is not None else now
            if now - held_since >= hold_s:
                return last
        else:
            held_since = None
        time.sleep(poll_s)          # 폴링은 벽시계, 판정은 기체 시계

def test_rc_loss_returns_home(v):
    v.inject("rc_loss", True)
    wait_until(v, lambda: (v.mode() in ("RTL", "RETURN"), v.mode()), timeout_s=5, what="enter RTL")
    wait_until(v, lambda: (not v.armed(), v.armed()), timeout_s=250, what="landed+disarmed")
```

> 실행 가능한 플랫폼 뼈대는 [R09](2026-10-24_R09_scalable_reusable_test_platform.md)의 `../code/platform_sketch.py` 를 본다. MAVLink 프레임이 바이트 수준에서 어떻게 생겼는지는 이 단원의 [`../code/mavlink_v2_heartbeat_codec.py`](../code/mavlink_v2_heartbeat_codec.py) 를 본다 (아래 8.1).

### 8.1 MAVLink v2 프레임 직접 만들어 보기 — `../code/mavlink_v2_heartbeat_codec.py`

pymavlink와 MAVSDK가 아래에서 하는 일을 표준 라이브러리만으로 재현한 예제다. **실행해서 자체 테스트 통과를 확인했다** (`python3 mavlink_v2_heartbeat_codec.py` → `self-test OK`).

- CRC-16/MCRF4XX: 카탈로그 check 값 `crc("123456789") = 0x6F91` 확인.
- **CRC_EXTRA를 메시지 정의에서 계산**: 메시지 이름 + 와이어 순서(크기 큰 타입 먼저) 필드의 타입 · 이름 → HEARTBEAT = 50, COMMAND_LONG = 152 (MAVLink 헤더에 공개된 값과 일치).
- v2 프레임: `FD len incompat compat seq sysid compid msgid(3B) payload crc(2B)`. **payload 끝의 0 바이트는 잘라서 보낸다** (COMMAND_LONG 33 → 32바이트), 받는 쪽은 0으로 채워 복원.
- 스트림 파서: 쓰레기 바이트 · 쪼개진 읽기 · CRC 오류 프레임을 섞어 넣어도 정상 프레임만 뽑는다. CRC 오류 시 **1바이트만 버리고 재동기**.

실행 결과 (HEARTBEAT, 쿼드 · PX4 · armed):

```text
hb  : fd 09 00 00 07 01 01 00 00 00 00 00 03 00 02 0c 81 04 03 f0 b4
arm : fd 20 00 00 08 ff be 4c 00 00 00 00 80 3f 00 00 ... 90 01 01 01 cc 09
stats: {'ok': 2, 'bad_crc': 1, 'unknown_msgid': 1, 'skipped_bytes': 23}
self-test OK
```

FTE 포인트: CRC_EXTRA 때문에 **양쪽의 메시지 정의(XML 버전)가 다르면 CRC가 맞지 않아 프레임이 조용히 버려진다.** "GCS에서 특정 메시지만 안 보인다" 같은 버그의 흔한 원인이다. 테스트 플랫폼은 파서 통계(`bad_crc`, `unknown_msgid`)를 메트릭으로 노출해야 한다. 이 파서의 한계(가짜 STX가 큰 길이를 가리키면 진짜 프레임이 잠시 지연됨)도 테스트 케이스 아이디어가 된다.

---

## 9. SSD 검증에서는 이랬다 → 드론에서는 이렇다

Don의 경험 범위 안에서만 비교한다 (데이터센터 SSD FW 검증: 챔버 테스트 플랫폼 SDK, web 기반 test automation, NVMe telemetry 디버그 기능, error reporting/handling scheme 등).

| 주제 | SSD 검증에서는 이랬다 | PX4/ArduPilot · 드론에서는 이렇다 |
|---|---|---|
| 테스트 대상과 대화하는 통로 | UART 테스트 시퀀스, NVMe 명령 · telemetry | MAVLink (MAVSDK/pymavlink), Betaflight는 MSP/CRSF |
| "기다리기" | 명령 완료 · 상태 polling + timeout | `wait_mode`, `wait_altitude` … + **sim/기체 시간** timeout + 유지 시간 |
| 환경 스트레스 | 챔버 온도 제어 · 스케줄링 | 시뮬레이터 환경(바람 · 위치 · 센서 노이즈), HIL에서는 RF 감쇠 · 전원 |
| 고장 주입 | 에러 주입 · 전원 차단 시나리오 [일반론] | `MAV_CMD_INJECT_FAILURE`, `SIM_RC_FAIL`, `SIM_GPS1_ENABLE` |
| 상태 모니터링 | eSSD 상태 모니터링, health monitoring debug FW | `SYS_STATUS`, PX4 events, `PreArm:` STATUSTEXT, `failsafe_flags` |
| 실패 분석 | NVMe telemetry 로그, error reporting scheme | ULog/DataFlash, Flight Review, Replay |
| 플랫폼을 남이 쓴다 | 챔버 플랫폼 SDK를 다른 엔지니어가 사용 | autotest helper를 수백 명 기여자가 사용 — API 안정성 · docstring 필수 |
| 결정성 | 같은 펌웨어 · 같은 시퀀스면 대체로 재현 | 물리 · 타이밍 의존 → lockstep · replay로 결정성을 **만들어야** 함 |
| 안전 | 데이터 손실 · 장비 손상 | **사람 · 기체 안전** — `SYS_FAILURE_EN` 같은 잠금, 프롭 제거, 전류 제한 |
| 합격 기준 | 사양 수치(성능 · 신뢰성) | 사양 파라미터가 곧 기대값 (`RTL_SPEED_MS` → groundspeed ± 0.5 m/s, 5초 유지) |

말로 옮기면: "SSD 챔버 플랫폼에서 하던 '상태를 polling 하면서 timeout 안에 조건을 기다리고, 실패하면 telemetry를 덤프'하는 구조가 PX4/ArduPilot 테스트에도 그대로 있다. 다른 점은 시간이다. 시뮬레이터는 실제 시간보다 빠르거나 느리게 돌기 때문에 timeout을 기체 시간으로 재야 하고, 제어기는 값을 스치고 지나가므로 '유지 시간'을 판정에 넣어야 한다."

---

## 10. 테스트 아이디어 · 체크리스트

### 10.1 PX4/ArduPilot 기반 기체의 릴리스 회귀에서 볼 것

- [ ] 사전 무장 검사: 센서 하나씩 빼거나 값 이상을 넣었을 때 **정확한 이유 문구**로 거부되는가
- [ ] RC 끊김: 판정 시간(`COM_RC_LOSS_T` / Copter RC failsafe 타이밍) 측정, 설정별 행동(Hold/RTL/Land/Disarm) 전체 매트릭스
- [ ] 지상에서 RC 끊김 → 즉시 disarm (ArduPilot subtest 1과 같은 칸)
- [ ] failsafe 꺼짐 상태에서 **아무 행동도 없는지**
- [ ] GCS 링크 끊김 (`COM_DL_LOSS_T`, `FS_GCS_ENABLE`)
- [ ] 복합 고장: GPS 없음 + RC 끊김 → RTL 불가 → Land로 떨어지는 fallback 체인
- [ ] 배터리 부족 단계별 행동, 착륙 중 다른 failsafe가 겹칠 때 착륙 유지
- [ ] PX4 `COM_FAIL_ACT_T` 지연(Hold 대기) 동안 사용자 모드 전환 허용 · 스틱 override 금지 동작
- [ ] 센서 고장(GPS off, mag off/stuck, baro off/stuck) 중 미션 계속/착륙 판정 — PX4 `test_multicopter_failsafe.cpp` 와 같은 칸
- [ ] 모터 고장 주입 (hex, `[motor_failure_injection]`) → 제어 할당 재구성
- [ ] 모드 전환 · 미션 수치 오라클: 속도 · 고도 ± 허용 오차 + 유지 시간
- [ ] 테스트 후 불변식: 프로세스 생존, disarm, 파라미터 원복
- [ ] sanitizer SITL 빌드에서 시나리오 통과 (ASan/TSan/UBSan)
- [ ] 홈 위치 · 환경 변형 (고위도, 강풍)
- [ ] 로그: dropout 없음, 필요한 토픽 기록, replay로 EKF bit-exact

### 10.2 테스트 플랫폼 자체 체크리스트

- [ ] 모든 timeout이 기체/sim 시간 기준인가, 벽시계 timeout은 바깥 watchdog에만 있는가
- [ ] 모든 판정 helper가 실패 메시지에 "원한 값 vs 도달 값"을 넣는가
- [ ] 테스트마다 docstring(한 줄 설명)이 강제되는가
- [ ] 끈 테스트 · flaky 테스트가 이유와 함께 목록화되는가
- [ ] 재시도 통과가 따로 집계되는가
- [ ] 실패 시 로그 · core · 캡처가 자동 업로드되는가
- [ ] 고장 주입 API에 안전 잠금이 있는가
- [ ] 같은 시나리오가 SITL과 HIL에서 **같은 코드**로 도는가 (드라이버만 교체)

---

## 11. 직접 해 볼 것 (모두 가이드 — 이 단원 작성 중 실행하지 않음)

> 환경: Ubuntu 22.04/24.04 (또는 Mac에서는 Don의 Lima VM). 각 프로젝트의 **공식 설치 스크립트는 내용을 읽어 본 뒤** 실행한다. 이 단원 작성 중에는 clone만 했고 빌드 · 설치 스크립트는 돌리지 않았다. 하드웨어 불필요.

### 11.1 PX4 — SIH SITL 띄우고 MAVSDK 테스트 하나 돌리기 [가이드]

```text
# 1) 소스 (서브모듈 포함)
git clone https://github.com/PX4/PX4-Autopilot.git --recursive
cd PX4-Autopilot
#    개발 환경: Tools/setup/ubuntu.sh (내용 확인 후) 또는 문서의 docker 컨테이너 사용

# 2) SIH 시뮬레이션으로 쿼드 띄우기 (pxh> 셸이 뜬다)
make px4_sitl_sih sihsim_quadx
#    pxh> commander takeoff
#    pxh> failure gps off          # SYS_FAILURE_EN=1 필요: param set SYS_FAILURE_EN 1
#    pxh> listener vehicle_status   # uORB 토픽 실시간 보기

# 3) MAVSDK C++ 라이브러리 설치 (버전은 test/mavsdk_tests/MAVSDK_VERSION, 확인 시점 3.17.2)
#    CI와 같은 방식: libmavsdk-dev_<ver>_ubuntu22.04_amd64.deb 를 GitHub release에서 받아 dpkg -i

# 4) 테스트 빌드 (실행은 하지 않음)
DONT_RUN=1 make px4_sitl_default mavsdk_tests

# 5) 테스트 하나만 실행 (10배속)
test/mavsdk_tests/mavsdk_test_runner.py test/mavsdk_tests/configs/sih-sitl.json \
    --speed-factor 10 --model quadx --case 'Land on GPS lost during mission (baro height mode)'

# 6) 반복 실행으로 flaky 여부 보기
test/mavsdk_tests/mavsdk_test_runner.py test/mavsdk_tests/configs/sih-sitl.json \
    --speed-factor 10 --model quadx --iterations 5 --abort-early

# 7) failsafe 단위 테스트
make tests TESTFILTER=failsafe_test
```

> 주의: 문서(`integration_testing_mavsdk.md`)는 `DONT_RUN=1 make px4_sitl gazebo-classic mavsdk_tests` 로, CI(`sitl_tests.yml`)는 `make px4_sitl_default mavsdk_tests` 로 빌드한다. SIH 설정으로 돌릴 때 어느 빌드 디렉터리가 맞는지(`--build-dir`)는 [확인 필요].

결과 보기: 러너가 남긴 `.ulg` 를 `pip install pyulog` 후 `ulog_info <file>.ulg`, `ulog_messages <file>.ulg` 로 열어 본다. failsafe 시각 · nav_state 전이를 5.6절 스케치로 뽑아 본다.

### 11.2 ArduPilot — SITL 띄우고 autotest 테스트 하나 돌리기 [가이드]

```text
# 1) 소스
git clone --recurse-submodules https://github.com/ArduPilot/ardupilot.git
cd ardupilot
#    개발 환경: Tools/environment_install/install-prereqs-ubuntu.sh (내용 확인 후) — pymavlink, MAVProxy 포함

# 2) 손으로 SITL 띄우기 (MAVProxy 콘솔 + 지도)
Tools/autotest/sim_vehicle.py -v ArduCopter --console --map -w
#    MAV> mode guided
#    MAV> arm throttle
#    MAV> takeoff 10
#    MAV> param set SIM_RC_FAIL 1     # RC 끊김 주입 → RTL로 바뀌는지 관찰
#    MAV> param set SIM_RC_FAIL 0

# 3) autotest로 테스트 하나만
Tools/autotest/autotest.py build.Copter test.Copter.ThrottleFailsafe
Tools/autotest/autotest.py build.Copter test.Copter.RTLSpeed

# 4) Replay 테스트 (EKF bit-exact 비교)
Tools/autotest/autotest.py build.Copter test.Copter.Replay

# 5) 단위 테스트 (gtest)
./waf configure --board sitl
./waf check            # 빌드 + 실행.  ./waf tests 는 빌드만
```

결과 보기: 테스트별 로그 `<log_name>-ThrottleFailsafe.txt` (buildlogs 경로 [확인 필요]), onboard `.bin` 로그는 `mavlogdump.py --types MODE,ERR <file>.bin` 또는 MAVExplorer로 연다 [확인 필요: 명령 옵션].

### 11.3 읽기 과제 (빌드 없이 지금 할 수 있는 것)

1. `test/mavsdk_tests/autopilot_tester.cpp` 의 `execute_mission_and_lose_mag()` 를 읽고 `poll_condition_with_timeout` 로 무엇을 기다리는지 적기.
2. `Tools/autotest/vehicle_test_suite.py` 의 `wait_and_maintain_range()` 를 읽고 `minimum_duration` 과 `timeout` 의 관계를 그림으로.
3. `arducopter.py` 의 `GCSFailsafe` 를 `ThrottleFailsafe` 처럼 subtest 표로 정리.
4. `Tools/Replay/check_replay.py` 를 읽고, Betaflight Blackbox에 같은 방식의 "필터 replay 비교"가 가능한지 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md)과 연결해 메모.
5. `../code/mavlink_v2_heartbeat_codec.py` 에 `ATTITUDE`(msgid 30) 정의를 추가하고 CRC_EXTRA를 자체 테스트에 넣기. (작성 중 이 코드의 `crc_extra` 로 계산해 보니 39 — 공식 생성 헤더와의 대조는 [확인 필요])

---

## 12. 확인 안 된 것

- SITL/MAVSDK/autotest를 **실제로 실행하지 않았다**. 11절 명령은 문서 · CI 설정 기반 가이드.
- PX4 `COM_LOW_BAT_ACT` 기본값, `SYS_HITL` 값 정의 위치 (문서상 1 = HITL, 2 = SIH on hardware).
- PX4 Gazebo(gz)의 HITL 지원 여부 (문서는 Gazebo Classic만 언급).
- PX4 MAVSDK 러너가 테스트마다 PX4 프로세스를 재시작하는지 세부 동작.
- PX4 `work_queue status`, `top` 등 셸 명령 출력 형식.
- PX4 failsafe 웹 시뮬레이터 공개 URL.
- `docs/en/dev_log/flight_log_analysis_statistical.md` 의 구체 내용, Flight Review의 현재 플롯 구성.
- pyulog 스케치의 토픽 · 필드 이름 (`failsafe_flags.manual_control_signal_lost`, `vehicle_status.failsafe`)이 현재 버전과 일치하는지.
- ArduPilot Copter fast rate thread 기본값 · 지원 범위, `SCHED_DEBUG` 이름.
- ArduPilot `SIM_*` 고장 주입 파라미터 전체 목록.
- ArduPilot Renode CI가 검증하는 범위.
- MAVExplorer / UAV Log Viewer / `mavlogdump.py` 옵션의 현재 상태.
- NuttX · ChibiOS 하위 라이선스(Apache-2.0 / GPLv3 혼합) 정확한 범위. 라이선스 해석 전체는 **법률 자문 아님**.
- Betaflight SITL에서 UART를 TCP로 바꿔 CRSF/MSP를 주입할 수 있는지 (R03에서 확인 필요).
- Neros 제품 구성 (Archer / Archer AI 등) — [Gemini 주장 · 미확인].
- `ATTITUDE` CRC_EXTRA 39 — 이 단원의 코드로 계산한 값. 공식 생성 헤더(`mavlink/c_library_v2`)와 직접 대조하지 않음.

---

## 13. 참고 링크

소스 (2026-10-24 shallow clone, `git clone --depth 1 --filter=blob:none`):

- PX4-Autopilot `main` @ `cd2b157`: https://github.com/PX4/PX4-Autopilot
- ArduPilot `master` @ `437a330`: https://github.com/ArduPilot/ardupilot

이 단원에서 직접 읽은 파일 (PX4):

- `test/mavsdk_tests/test_multicopter_failsafe.cpp`, `autopilot_tester.h/.cpp`, `mavsdk_test_runner.py`, `configs/sih-sitl.json`, `MAVSDK_VERSION`
- `.github/workflows/sitl_tests.yml`, `failsafe_sim.yml`, `checks.yml`
- `src/modules/commander/failsafe/failsafe_test.cpp`, `framework.h`, `commander_params.yaml`
- `src/modules/mc_rate_control/MulticopterRateControl.cpp/.hpp`
- `platforms/common/include/px4_platform_common/px4_work_queue/WorkQueueManager.hpp`
- `msg/SensorCombined.msg`, `src/modules/simulation/` (sih, gz_bridge, simulator_mavlink)
- `docs/en/simulation/hitl.md`, `docs/en/sim_sih/index.md`, `docs/en/sim_gazebo_gz/index.md`, `docs/en/test_and_ci/integration_testing_mavsdk.md`, `docs/en/test_and_ci/unit_tests.md`, `docs/en/dev_log/logging.md`, `docs/en/log/flight_log_analysis.md`, `docs/en/debug/system_wide_replay.md`

이 단원에서 직접 읽은 파일 (ArduPilot):

- `Tools/autotest/vehicle_test_suite.py` (`TestSuite`, `Test`, `run_one_test_attempt`, `wait_mode`, `wait_altitude`, `wait_and_maintain*`, `CRSF`)
- `Tools/autotest/arducopter.py` (`ThrottleFailsafe`, `RTLSpeed`, `LoiterToAlt`, `Replay`, `wait_rtl_complete`, `tests1a`, `disabled_tests`)
- `Tools/autotest/autotest.py`, `Tools/autotest/sim_vehicle.py`, `Tools/scripts/build_ci.sh`
- `Tools/Replay/check_replay.py`
- `.github/workflows/test_sitl_copter.yml`, `test_replay.yml`, `test_unit_tests.yml`
- `ArduCopter/Copter.cpp`, `ArduCopter/events.cpp`, `ArduCopter/Parameters.cpp`
- `libraries/AP_Scheduler/AP_Scheduler.cpp`, `libraries/AP_HAL_ChibiOS/hwdef/MatekH743/hwdef.dat`, `libraries/SITL/SITL.cpp`, `wscript`

문서 · 도구:

- PX4 User Guide (MAVSDK 통합 테스트): https://docs.px4.io/main/en/test_and_ci/integration_testing_mavsdk.html
- PX4 HITL: https://docs.px4.io/main/en/simulation/hitl.html
- PX4 SIH: https://docs.px4.io/main/en/sim_sih/
- PX4 Flight Review: https://logs.px4.io/ (소스 https://github.com/PX4/flight_review)
- pyulog: https://github.com/PX4/pyulog
- MAVSDK: https://mavsdk.mavlink.io/
- ArduPilot autotest 문서: https://ardupilot.org/dev/docs/the-ardupilot-autotest-framework.html [확인 필요: URL]
- ArduPilot SITL: https://ardupilot.org/dev/docs/sitl-simulator-software-in-the-loop.html
- ArduPilot 라이선스: https://ardupilot.org/dev/docs/license-gplv3.html
- MAVLink 직렬화 (CRC_EXTRA, v2 truncation): https://mavlink.io/en/guide/serialization.html
- MAVProxy / MAVExplorer: https://github.com/ArduPilot/MAVProxy

관련 단원: [R00 개요](2026-10-24_R00_overview_and_gemini_corrections.md) · [R01 FTE vs SSD](2026-10-24_R01_fte_vs_ssd_validation.md) · [R02 시스템 지도](2026-10-24_R02_drone_system_and_open_source_map.md) · [R03 Betaflight/INAV](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · [R05 RC 링크 · 프로토콜](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · [R06 Companion · Linux](2026-10-24_R06_companion_video_linux_rtos.md) · [R07 방법론](2026-10-24_R07_test_methodology_pyramid.md) · [R08 HIL 벤치](2026-10-24_R08_hil_bench_design_instruments.md) · [R09 플랫폼](2026-10-24_R09_scalable_reusable_test_platform.md) · [R10 로드맵](2026-10-24_R10_roadmap_labs_references.md)

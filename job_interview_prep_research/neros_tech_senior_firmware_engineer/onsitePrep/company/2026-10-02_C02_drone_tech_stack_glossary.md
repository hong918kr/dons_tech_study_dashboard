# 🛩️ C02 · 드론 회사 기술 스택 — 데이터 흐름과 층별 용어 사전

> **질문 (2026-10-02)**: 드론 회사 공고에 나오는 용어(IMU, PX4, EKF, GNC, ELRS, MAVLink, SITL…)가 각각 스택의 어디에 있고 무슨 뜻인지. **답**: 드론 SW는 **센서 → 인식 → 상태 추정 → 유도·제어 → 비행 제어기 → 모터** 한 줄로 흐른다. 앞쪽 셋(인식·추정·유도)은 주로 **Linux 컴패니언 컴퓨터**에서, 뒤쪽(자세 제어·모터 출력)은 **STM32 비행 제어기**에서 돈다. 옆으로는 통신 링크와 GCS가 붙고, 바닥에는 빌드·테스트 인프라가 깔린다. 마지막 절에 층 → Neros 팀 → Don과의 거리 표가 있다.

## 1. 근거로 쓴 자료

- Neros FW·임베디드 공고 13개 (2026-10-01 수집): `research/job_board_snapshots/2026-10-01_fw_embedded_jds.md`
- Neros autonomy 계열 공고 10개 (2026-10-02 수집): `research/job_board_snapshots/2026-10-02_autonomy_jds.md`
- 이어지는 페이지: [C01 autonomy란?](2026-10-02_C01_autonomy_vs_embedded_ai.md) · [N01 회사 스택 지도](../notes/2026-10-01_N01_company_stack_map.md) · [N02 STM32의 의미](../notes/2026-10-01_N02_stm32_what_it_means.md)
- 기존 자료: [드론 아키텍처 노트](../../practice/html/00_drone_architecture.html)
- 표기: 공고 문구로 확인되는 Neros 사실은 [확인됨], 업계 일반 지식으로 Neros도 그럴 것이라고 보는 건 [추정]. 용어 정의 자체는 업계 일반 지식이다

## 2. 한 장 그림 — 데이터가 흐르는 순서

```
 ┌──────────── 센서 ────────────┐
 │ IMU(gyro·accel) · baro · mag │  ← SPI/I2C로 FC에 직결 (수 kHz)
 │ GNSS · 카메라(EO/열화상)      │  ← UART / MIPI CSI-2 / USB로 컴패니언에
 └──────────────┬───────────────┘
                │
  [컴패니언 컴퓨터: embedded Linux, Jetson급 + 가속기]   ← autonomy 조직
   Perception ─ 표적 탐지·분류·추적 (image-space track)
        │
   State Estimation ─ 내 위치·속도·자세 + 표적 상태 + 불확실성 (EKF, VIO)
        │
   Guidance ─ "어디로 가야 하나" → 가속도/속도/자세 setpoint
        │       ── MAVLink / 자체 프로토콜 (UART·Ethernet) ──
        ▼
  [비행 제어기 FC: STM32, Betaflight 또는 PX4]            ← 펌웨어 조직
   Control ─ cascaded PID: 위치 → 속도 → 자세(angle) → 각속도(rate)
   Mixer   ─ roll/pitch/yaw/throttle → 모터 4개 각각의 출력
        │       ── DShot (디지털 ESC 프로토콜) ──
        ▼
   ESC → BLDC 모터 → 프로펠러

  옆: RC 링크(ELRS/CRSF) · 비디오 링크 · 텔레메트리 → 핸드셋/고글/GCS
  바닥: Bazel·Yocto 빌드, CI, SITL/HIL, 비행 로그, factory test
```

- 두 컴퓨터를 나누는 이유: 자세 제어는 **수 kHz, 지터에 매우 민감** → bare-metal/RTOS MCU. 인식·추정은 **연산량이 크고 수십 Hz** → Linux + 가속기
- FPV 드론 기본형은 컴패니언 없이 **조종사 → RC 링크 → FC**만으로 난다. autonomy 기능(terminal guidance, position hold, GPS-denied nav)이 붙을 때 컴패니언이 들어간다 [확인됨: Autonomy Lead 공고의 기능 목록]
- 경계에서 만나는 것: "integration between an onboard mission computer and a flight controller or autopilot, using PX4, ArduPilot and MAVLink … arming logic, flight modes and failsafe behavior at that boundary" [확인됨: Autonomy Platform 공고]

## 3. 센서 층

| 용어 | 뜻 | 드론에서 왜 중요한가 |
|---|---|---|
| **IMU** | Inertial Measurement Unit. gyroscope(각속도) + accelerometer(가속도), 보통 6축. MEMS 칩 하나 | 자세 제어의 **입력 그 자체**. FC가 SPI로 수 kHz 읽는다. 진동 노이즈가 최대 적 → 필터링 |
| **magnetometer** | 지자기 센서 (나침반) | yaw(방위) 기준. 모터 전류에 쉽게 오염 |
| **barometer (baro)** | 기압 센서 | 고도 추정. 프롭 바람·온도에 흔들림 |
| **GNSS** | GPS·GLONASS·Galileo·BeiDou 통칭. 보통 UART로 NMEA/UBX 수신 | 절대 위치. **재밍·스푸핑에 약함** → Neros가 GPS-denied nav를 따로 뽑는 이유 [확인됨: State Estimation 공고] |
| **RTK GNSS** | 기준국 보정으로 cm급 정확도 | 비행 후 ground truth로 쓴다 ("RTK GNSS post-processing" [확인됨: Senior Platform 공고]) |
| **EO / IR (thermal, LWIR)** | Electro-Optical(가시광) / 적외선·열화상 카메라 | 표적 탐지 입력. 야간 작전은 열화상 [확인됨: Perception 공고] |
| **MIPI CSI-2, GMSL, USB3/GigE Vision** | 카메라 → SoC 인터페이스. GMSL은 긴 케이블용 SerDes | 컴패니언 쪽 센서 드라이버 일 [확인됨: Senior Platform 공고] |
| **PPS / trigger line / PTP** | Pulse-Per-Second(GNSS 1Hz 펄스), 카메라 노출 트리거, Precision Time Protocol | 센서·컴퓨터·FC의 **시간을 맞춰야** 추정과 로그 재생이 맞는다 [확인됨: Platform 공고] |
| **calibration** | 센서별 bias·scale·축 정렬·렌즈 왜곡 값 | 개체마다 다름 → 공장에서 측정해 저장 ("per unit calibration data … provisioning at the factory" [확인됨]) |

## 4. 비행 제어기(FC) 층 — STM32와 flight stack

### 하드웨어

- **FC (Flight Controller)**: IMU·baro가 붙은 작은 보드. MCU는 대부분 **STM32 F4/F7/H7** (Cortex-M4/M7). Neros도 STM32 [확인됨: 리크루터·Michael, Tennessee·Ukraine 공고]
- **컴패니언 컴퓨터 (companion / mission computer)**: FC 옆의 Linux SoM. 카메라, 인식, 고수준 판단. NVIDIA Jetson, Qualcomm, TI, Hailo가 후보로 나옴 [확인됨: Platform 공고 문구, 실제 선택은 미공개]
- **Archer**: Neros 기체 이름 중 하나. "embedded software on the Neros Archer platform including flight control and radio link code" [확인됨: Ukraine 공고]

### flight stack 비교 — Betaflight / PX4 / ArduPilot

| 항목 | **Betaflight** | **PX4** | **ArduPilot** |
|---|---|---|---|
| 출신 | FPV 레이싱·프리스타일 (Cleanflight 포크) | 학계(ETH) → Dronecode 재단 | 취미·상용 오토파일럿, 가장 오래됨 |
| 실행 환경 | **bare-metal**, 스케줄러 루프 | **NuttX RTOS** (Linux에서도 돎) | ChibiOS 또는 Linux (HAL 추상화) |
| 중심 기능 | 조종사 손맛: rate 모드, 낮은 지연, 튜닝 | 자율비행: 미션, offboard, 위치 제어 | 자율비행 + 기체 종류 폭넓음(멀티콥터·고정익·로버·보트) |
| 상태 추정 | 자세만 (complementary 계열), GPS는 rescue용 | **EKF2** (위치·속도·자세 전부) | EKF3 |
| 내부 통신 | 함수 호출·전역 상태 | **uORB** pub/sub | 자체 라이브러리 구조 |
| 외부 프로토콜 | **MSP**, CRSF | **MAVLink** | MAVLink |
| 라이선스 | GPL-3.0 | BSD-3 (상용 포크에 유리) | GPL-3.0 |
| GCS·설정 툴 | Betaflight Configurator | QGroundControl | Mission Planner, QGC |

- Neros는 **Betaflight와 PX4를 둘 다** 쓰고 그 사이에서 포팅한다: "Port and tune sensors and control algorithms across flight stacks (Betaflight, PX4), preserving flight feel that expert pilots depend on" [확인됨: Flight SW 공고]. 해석: 조종사 손맛은 Betaflight, 자율 기능은 PX4 쪽 구조가 유리 [추정]
- ArduPilot은 Autonomy Lead·Platform 공고에 이름만 나온다 [확인됨]
- 라이선스 차이는 방산 회사의 포크 전략과 관련 있을 수 있다 [추정]

### 제어 용어

| 용어 | 뜻 |
|---|---|
| **PID** | Proportional(오차) + Integral(누적 오차) + Derivative(오차 변화율). 드론 제어의 기본 단위. 튜닝 = 세 게인 고르기 |
| **cascaded loop** | 루프를 겹쳐 쌓음: 바깥 루프 출력이 안쪽 루프 setpoint. 위치(수십 Hz) → 속도 → 자세 angle → 각속도 rate(수 kHz). **안쪽일수록 빠르다** |
| **rate mode / angle mode** | rate(acro): 스틱 = 회전 속도, FPV 조종사 기본. angle(self-level): 스틱 = 기울기 각도 |
| **position hold / altitude hold** | 위치·고도 유지 루프를 rate 루프 위에 얹은 비행 모드 [확인됨: Flight SW 공고 "position and altitude hold"] |
| **flight mode, arming, failsafe** | 비행 모드 전환 / 모터 활성화 조건 / 링크 끊김·배터리 부족 시 행동. **안전 로직의 핵심** |
| **mixer** | roll·pitch·yaw·throttle 명령 → 모터 N개 출력으로 바꾸는 행렬. 기체 형태(쿼드 X, 헥사)마다 다름 |
| **ESC** | Electronic Speed Controller. FC 명령을 받아 BLDC 모터의 3상 전류를 만든다. 자체 MCU와 펌웨어(BLHeli_32, AM32, Bluejay)가 있다 |
| **DShot** | FC → ESC **디지털** 프로토콜 (DShot300/600 = kbit/s). 16비트 프레임 = 11비트 throttle + telemetry 요청 1비트 + CRC 4비트. 옛 PWM/OneShot의 아날로그 오차를 없앰. **bidirectional DShot**으로 ESC가 모터 RPM을 돌려줌 → RPM filter |
| **gyro filtering (LPF, notch, RPM filter)** | 모터 진동 주파수를 IMU 신호에서 지운다. Betaflight 튜닝의 큰 몫 |
| **actuator saturation** | 모터 출력이 한계에 닿음 → 원하는 가속을 못 냄. GNC 공고의 "limited control authority" [확인됨] |

- DShot·ESC 펌웨어 이름은 공고에 없다. FPV 업계 표준이라 Neros도 쓸 가능성이 높다 [추정]

## 5. 상태 추정 층 (State Estimation & Navigation)

| 용어 | 뜻 |
|---|---|
| **state** | 기체의 위치·속도·자세(+ 센서 bias). "target state" = 표적의 위치·속도 [확인됨: SE 공고] |
| **Kalman filter (KF)** | 예측(모델로 다음 상태 계산) → 갱신(측정으로 보정)을 반복. 각 값의 **불확실성(covariance)**도 함께 들고 다닌다. 선형 시스템에서 최적 |
| **EKF** | Extended KF. 비선형 모델을 현재 지점에서 선형화(야코비안)해서 KF 적용. PX4 EKF2, ArduPilot EKF3가 이것 |
| **UKF, error-state / invariant filter** | 비선형을 더 잘 다루는 변형들 [확인됨: SE 공고에 열거] |
| **factor graph / sliding window smoothing** | 최근 N개 시점을 한꺼번에 최적화 (필터보다 정확, 무거움) |
| **complementary filter** | gyro(단기 정확, 장기 drift) + accel(장기 정확, 단기 노이즈)을 주파수로 섞는 가벼운 자세 추정. Betaflight 계열 |
| **sensor fusion** | 여러 센서를 하나의 추정으로 합치는 것 전반 |
| **VIO** | Visual-Inertial Odometry. 카메라 특징점 이동 + IMU 적분으로 **GPS 없이** 상대 위치 추정. 시간이 갈수록 drift |
| **terrain-relative navigation** | 지형(카메라 영상·고도)을 지도와 대조해 절대 위치 보정 [확인됨: SE 공고] |
| **GPS-denied / degraded** | 재밍·스푸핑 환경. Neros autonomy의 전제 [확인됨: Evaluation 공고 "contested environments where GPS and communications are denied"] |
| **observability** | 주어진 센서로 그 상태를 원리적으로 알아낼 수 있는가 (예: 정지 상태에서 yaw는 IMU만으로 안 보임) |
| **drift, consistency** | 오차가 시간에 따라 커지는 정도 / 필터가 말하는 불확실성이 실제 오차와 맞는가 |

## 6. 유도·제어 층 — GNC 팀이 무엇인가

- **GNC = Guidance, Navigation, and Control.** 항공우주·미사일 분야의 전통적인 팀 이름
- **Navigation**: 나는 어디 있나 (= 5절 상태 추정)
- **Guidance**: 어디로, 어떻게 가야 하나 → 가속도·경로 명령을 만든다
- **Control**: 그 명령을 실제 자세·추력으로 따라간다 (= 4절 FC 루프)
- Neros의 Senior GNC 공고는 이 중 **terminal guidance와 그 제어 경로**를 맡는다: "guidance law, aimpoint command, achievable-acceleration management, and the interface to flight control" [확인됨]

| 용어 | 뜻 |
|---|---|
| **terminal guidance / homing** | 표적까지 마지막 구간을 스스로 유도 |
| **proportional navigation (PN)** | 시선각(line-of-sight) 변화율에 비례하는 가속을 준다 → 충돌 코스를 유지. 미사일 유도의 고전. APN = 표적 가속까지 보정한 변형 |
| **zero-effort miss (ZEM), time-to-go** | 지금부터 아무것도 안 하면 얼마나 빗나가나 / 충돌까지 남은 시간 |
| **aimpoint** | 표적의 어디를 노리나 |
| **engagement authorization, abort criteria** | 공격 허가·중단 조건. 안전·윤리 로직 [확인됨: GNC·Perception 공고] |
| **inner loop / outer loop** | FC 자세 루프(안쪽) / 유도 명령(바깥). GNC는 "autopilot and inner-loop interfaces"를 다룬다 [확인됨] |

## 7. 인식 층 (Perception)

| 용어 | 뜻 |
|---|---|
| **detection / classification / recognition** | 영상에서 물체 찾기 / 무엇인지 분류 / 특정 대상(아군·민간·표적) 식별 |
| **image-space tracking** | 영상 좌표에서 같은 표적을 프레임마다 따라감. 놓치면 **re-acquire** |
| **classical CV** | correlation·template matching·optical flow·feature matching. 학습 없이 규칙과 수학으로 |
| **learned detection** | 신경망 검출기 (YOLO류). Neros는 "classical → learned로 넘어가는 중, deterministic fallback 유지" [확인됨: Perception Lead 공고] |
| **quantization** | 모델을 FP32 → INT8 등으로 줄여 가속기에서 빠르게. 정확도·지연 trade-off 측정이 필수 [확인됨] |
| **TensorRT / QNN·SNPE / TIDL / Hailo** | NVIDIA / Qualcomm / TI / Hailo의 추론 런타임 [확인됨: Platform 공고 열거] |
| **precision / recall / operating point** | 오탐 비율 / 미탐 비율 / 그 둘 사이에서 고른 임계값 |
| **track continuity, ID switch** | 추적이 끊기지 않는 정도 / 다른 물체로 착각해 ID가 바뀌는 횟수 |
| **perception output contract** | 인식이 추정·유도에 넘기는 형식: confidence, "검출 없음/억제됨/센서 저하" 신호 [확인됨] |
| **ATR, seeker** | Automatic Target Recognition / 표적 탐지·추적 센서 헤드 (방산 용어) |

## 8. 컴패니언 런타임 층 (Autonomy Platform & Runtime)

| 용어 | 뜻 |
|---|---|
| **runtime / middleware** | 모듈들이 메시지를 주고받고 정해진 주기로 실행되는 틀 |
| **ROS 2, DDS, Zenoh, LCM** | 로보틱스 pub/sub 미들웨어. ROS 2는 DDS 위에 있음. Zenoh·LCM은 더 가벼운 대안 [확인됨: 공고 열거] |
| **latency accounting, deadline monitoring** | sensor-to-actuator 지연을 실측해 예산 안인지 감시, release gate로 강제 [확인됨] |
| **CPU affinity / isolation, PREEMPT_RT** | 실시간 스레드를 특정 코어에 고정 / Linux 실시간 패치 |
| **lock-free, zero-copy** | 락 없는 큐 / 복사 없이 버퍼 공유 → 지연·지터 감소 |
| **ring buffer + triggered capture** | 최근 N초를 계속 링버퍼에 쓰다가 이벤트 때 덤프 [확인됨] — [N05 ring buffer](../notes/2026-10-01_N05_ring_buffer_onsite.md)와 직결 |
| **deterministic replay** | 비행 로그를 다시 넣으면 같은 결과가 나오게 → 회귀 테스트·디버깅 |

## 9. 통신 링크 층

| 용어 | 뜻 | Neros 근거 |
|---|---|---|
| **RC link** | 조종기 → 드론 제어 명령 링크 | |
| **ExpressLRS (ELRS)** | 오픈소스 RC 링크 시스템. LoRa 변조(SX127x/SX128x), 900MHz·2.4GHz, 낮은 지연·긴 거리 | [확인됨: Test·Ukraine·Autonomy Lead 공고에 이름] |
| **CRSF** | Crossfire Serial protocol. RC 수신기 ↔ FC UART 프로토콜 (원래 TBS Crossfire). ELRS도 이 프레임을 씀. 채널 값 + 텔레메트리 양방향 | [추정: ELRS를 쓰면 CRSF] |
| **LoRa C2 link** | LoRa 변조 기반 command-and-control 링크 | [확인됨: RF Test 공고 "LoRa command-and-control links"] |
| **MAVLink** | 드론용 경량 메시지 프로토콜 (헤더 + payload + CRC, 메시지 ID 정의서 기반). PX4·ArduPilot ↔ GCS·컴패니언 | [확인됨: Flight SW·Platform 공고] |
| **MSP** | MultiWii Serial Protocol. Betaflight 설정·텔레메트리용 시리얼 프로토콜 (Configurator, OSD, DJI 고글 등이 사용) | [추정: 공고엔 없음, Betaflight를 쓰면 접함] |
| **video link (analog / digital)** | FPV 영상 하향 링크. 아날로그 = 지연 최소, 디지털 = 화질 | [확인됨: RF Test 공고] |
| **OSD** | On-Screen Display. 영상 위에 배터리·고도 등을 겹침 | [추정] |
| **mesh, repeater, point-to-point** | 다중 노드 네트워크 / 중계기 / 1:1 링크 | [확인됨: Connectivity 공고] |
| **EW, jamming, anti-jam** | 전자전·전파 방해와 대응(frequency hopping, nulling) | [확인됨: EW Test 공고] |

## 10. GCS와 주변 장비

- **GCS (Ground Control Station)**: 지상에서 드론 상태를 보고 명령하는 SW·장비. PX4 계열은 QGroundControl [확인됨: Flight SW 공고 "QGroundControl tooling"]
- Neros는 "drone & ground control software"를 함께 테스트한다 [확인됨: FW Test 공고] → FW Test 범위에 GCS 포함
- **Peripherals**: 핸드셋·헤드셋(고글)·디스플레이·ground control systems. STM32 + FPGA + 비디오 처리 [확인됨: Tennessee 공고]
- **datalink, payload integration**: 기체 쪽 데이터 링크와 탑재물(카메라 등) 연동 [확인됨: Flight SW 공고]

## 11. 개발 인프라 층

| 용어 | 뜻 | Neros 근거 |
|---|---|---|
| **Bazel** | Google 출신 빌드 시스템. **hermetic**(외부 환경 영향 없음)·**reproducible**(같은 입력 → 같은 바이너리)·캐시. 여러 타깃 크로스 컴파일에 강함 | [확인됨: Platform 공고 "e.g. Bazel", Test 공고 nice-to-have] |
| **CMake / make** | 전통적 빌드 도구 | [확인됨: Test 공고] |
| **cross-compile toolchain, SDK** | 호스트(x86)에서 타깃(ARM)용 바이너리를 만드는 컴파일러 세트 / 다른 팀이 쓰는 라이브러리 묶음 | [확인됨] |
| **Yocto / Buildroot** | 커스텀 embedded Linux 이미지를 만드는 빌드 프레임워크. Yocto = 레이어·레시피, 크고 유연. Buildroot = 단순 | [확인됨: Embedded Linux 공고] |
| **BSP, device tree, U-Boot** | 보드 지원 패키지 / 하드웨어 기술 파일 / 부트로더 | [확인됨] |
| **secure boot, OTA (Mender, RAUC, SWUpdate)** | 서명된 이미지만 부팅 / 무선 업데이트와 롤백 | [확인됨] |
| **CI/CD (GitLab CI, Jenkins)** | 커밋마다 빌드·테스트 자동 실행 | [확인됨: Test 공고] |
| **SITL** | Software-In-The-Loop. FC 펌웨어를 PC에서 빌드해 **시뮬레이터**(Gazebo 등)와 붙여 비행. 하드웨어 없음 | [확인됨: Flight SW·Manager 공고 "SITL/HITL"] |
| **HIL / HITL** | Hardware-In-The-Loop. **실제 FC 보드**에 시뮬레이터가 가짜 센서 값을 주입하고 출력(모터 명령)을 읽음. 타이밍·드라이버 버그까지 잡음 | [확인됨: Test 공고 핵심 요건] |
| **open-loop replay vs closed-loop sim** | 녹화 로그를 넣어 출력만 비교 / 출력이 다시 입력에 영향을 주는 시뮬레이션 | [확인됨: Evaluation 공고 multi-tier regression] |
| **regression gate, release gate** | 지표가 기준 밑이면 빌드를 막는다 | [확인됨] |
| **factory test** | 생산 라인에서 개체마다 센서·모터·링크·calibration을 검사하고 기록. EVT/DVT/PVT, GR&R, test limits | [확인됨: RF Test 공고 / Neros엔 factory test FW 없음, 들은 정보] — [N08](../notes/2026-10-01_N08_factory_test_firmware.md) |

## 12. 층 → Neros 팀 → Don과의 거리

| 층 | 대표 용어 | Neros 팀 (공고) | Don과의 거리 | 근거·연결점 |
|---|---|---|---|---|
| 센서·드라이버 | IMU, SPI/I2C, MIPI, PPS | Flight SW, Autonomy Platform | **가까움** | SoC verification(I2C·SPI·DMA), Apple 새 실리콘 버스 루트코즈 |
| 비행 제어기 FW | STM32, Betaflight/PX4, PID, mixer, DShot | Flight SW, Ukraine FW | 중간 | Cortex-M bare-metal C는 있음. **flight stack·STM32 직접 경험 없음** |
| 상태 추정 | EKF, VIO, GPS-denied | State Estimation & Navigation | 멂 | 응용수학(선형대수·확률) 바탕만. 실무 없음 |
| 유도·제어 (GNC) | PN, terminal guidance | GNC | 멂 | 관련 경험 없음 |
| 인식 | detection, quantization | Perception | 멂 | 커리어 방향(AI 가속)과는 맞음, 경험 없음 |
| 컴패니언 런타임 | ROS 2, latency, ring buffer 로깅, 시간 동기 | Autonomy Platform & Runtime | 중간 | NVMe telemetry 디버그 기능, ring buffer, 멀티코어 FW. 갭: 현대 C++ on Linux, ROS 2 |
| 공통 runtime·빌드 | logging·telemetry·IPC, Bazel | Platform (FW) | 중간 | error reporting scheme, 사내 SDK. 갭: Bazel |
| embedded Linux | Yocto, kernel, U-Boot, OTA | Platform (Embedded Linux) | 멂 | 레쥬메에 없음 |
| 통신 링크 | ELRS/CRSF, MAVLink, LoRa | Flight SW, Connectivity, RF Test | 중간 | Apple RF chipset 통합(RFFE)·UART 시퀀스. ELRS·MAVLink 경험 없음 |
| GCS·주변기기 | handset, goggles, video | Peripherals (Tennessee) | 멂 | FPGA bring-up은 있음 (Solidigm) |
| 테스트 인프라 | HIL, SITL, CI, regression gate | **FW Test** ← 지금 트랙, Evaluation | **가까움** | 챔버 테스트 자동화 플랫폼, Python, stress·latent defect |
| factory test | 개체별 검사·calibration, GR&R | FW Test, RF Test, Platform | **가장 가까움** | Apple factory test-node 아키텍처, bring-up→NPI→MP |

### 온사이트에서 쓸 문장

- 스택을 모른다는 인상을 피할 때: "I see the stack as sensors into perception and estimation on the companion computer, guidance handing setpoints to the STM32 flight controller, which runs the cascaded rate loop and mixer out to the ESCs over DShot."
- 내 위치를 말할 때: "My strongest overlap is at the two ends of that pipeline — bus-level hardware bring-up, and the test and factory infrastructure that proves every unit works."
- 모르는 용어가 나오면: "I haven't worked with that directly — is it closer to X or Y?" 하고 층을 물어본다

## 체크

- [ ] 센서 → 인식 → 추정 → 유도·제어 → FC → 모터 순서를 보지 않고 그릴 수 있다
- [ ] Betaflight·PX4·ArduPilot 차이를 RTOS·추정·프로토콜 세 가지로 말할 수 있다
- [ ] cascaded loop에서 왜 안쪽 루프가 더 빠른지 한 문장으로 말할 수 있다
- [ ] SITL과 HIL의 차이, 그리고 HIL이 잡는 버그 종류를 말할 수 있다
- [ ] ELRS/CRSF, MAVLink, MSP가 각각 어디와 어디를 잇는지 말할 수 있다
- [ ] 12절 표에서 "가까움" 세 층과 그 근거 경험을 말할 수 있다

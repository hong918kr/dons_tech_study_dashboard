# ✈️ S01 · Flight Software — 무엇을 만들고, 어떻게 테스트하나

> Flight Software 팀은 기체를 실제로 날리는 펌웨어를 만든다: 센서 → 추정(estimator) → 제어 루프 → 모터 출력, 그 위의 flight mode·failsafe·datalink·payload. 단위 테스트는 FW 개발자가 쓰지만, **"실제 센서 타이밍과 실제 모터 출력에서 기체가 의도대로 반응하나"**는 테스트 조직이 SITL/HITL·벤치·비행 시험으로 증명해야 한다. 이 노트는 그 테스트 시스템을 직군 이해 → 고장 모드 → 테스트 레벨 → 테스트베드 → 면접 답변 순서로 정리한다. HIL 리그의 기본 블록도는 [FTE N02 HIL](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N02_hil_for_drones.html)에 있고, 여기서는 **Flight SW 전용 테스트 시스템**으로 확장한다.

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | flight mode(position/altitude hold), 제어 알고리즘, 센서 포팅·튜닝(Betaflight·PX4), datalink·payload 통합, safety·degradation 동작 [확인됨 공고 5195301007] |
| 주로 고장 나는 곳 | 루프 타이밍 지터, 센서 노이즈·진동 aliasing, 모드 전환 경계, failsafe 진입·복귀, RC 링크 손실, 스택 포팅 시 "flight feel" 변화, 비행에서만 재현되는 버그 |
| 대표 테스트베드 | SITL(물리 시뮬 + 실제 FW 빌드), HITL(실제 FC + 센서 주입 + 모터 출력 캡처), thrust stand, 진동 shaker, tethered/gimbal rig, 비행 시험 + blackbox 로그 분석 |
| 합격 기준 예 | 제어 루프 주기 지터 p99 < 50µs, RC loss → failsafe 진입 < 500ms, 모드 전환 시 attitude 과도 오차 < 10°, 모터 출력 프레임 누락 0 [추정] |
| Don 연결 | 비행 스택 경험 ❌(정직하게). 대신 **HW 결합 버그 루트코즈**(Apple), 스트레스로 latent defect 찾기, UART 시퀀스 자동화 SDK(SK hynix) → "테스트 인프라와 루트코즈" 쪽으로 기여 |

## 1. 이 직군 이해하기 — Neros 공고 근거

- **Firmware Engineer – Flight Software** [확인됨 공고 5195301007]: "build the flight-critical firmware that flies the aircraft — flight modes, control algorithms, and the vehicle-side integrations", "Port and tune sensors and control algorithms across flight stacks (**Betaflight, PX4**), preserving flight feel", "debug issues that only reproduce in flight", "**Write the unit and integration tests** for your firmware, partnering with our **Test organization on SITL/HITL and flight-test execution**"
- **Flight Software Manager** [확인됨 공고 5164187007]: 범위 = "flight controls, avionics, payload integration, datalink, and onboard health", "Own the flight software team's test strategy and unit/integration testing, partnering with the Test organization on SITL/HITL and flight-test execution", "Define and maintain the software interfaces consumed by autonomy"
- **Firmware Test Engineer** (Don 진행 중) [확인됨 공고 4941340007]: "validate the Neros drone & ground control software", "Hands-on experience building, setting up HIL test systems", nice-to-have "Betaflight, ExpressLRS, PX4, Ardupilot"
- **Ukraine Senior FW** [확인됨 공고 5086180007]: "flight control and radio link code" on Archer, "supporting testing including creating test cases, reviewing test plans and executing tests"
- **MCU**: STM32 사용 [확인됨 10-01 Michael], 테스트 FW가 가장 급함 [확인됨 10-01 Michael]

> 읽기: 공고 두 개가 똑같이 "**partnering with the Test organization on SITL/HITL and flight-test execution**"이라고 쓴다. 즉 역할 분담이 이미 정해져 있다 — **단위·통합 테스트 = Flight SW, SITL/HITL 인프라와 비행 시험 실행 = Test 조직**. Don이 들어갈 자리가 바로 SITL/HITL을 세우는 쪽이다.

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
 IMU (gyro/acc, SPI, 1–8 kHz) ─┐
 Baro (I2C/SPI, 50–100 Hz) ────┤
 Mag (I2C, 10–100 Hz) ─────────┤──► sensor drivers ──► filters (LPF, notch, RPM filter)
 GPS (UART, 5–10 Hz, UBX) ─────┘                              │
                                                              ▼
 RC rx (CRSF over UART, 150–500 Hz) ──► RC input ──► flight mode logic ──► setpoints
 Datalink / MAVLink (UART/Eth) ─────┘          │  (acro, angle, alt hold, pos hold, failsafe)
                                               ▼
                         estimator (attitude: complementary/EKF; position: EKF2 in PX4)
                                               │
                                               ▼
                 cascaded control: position → velocity → attitude → rate PID (2–8 kHz)
                                               │
                                               ▼
                         mixer ──► DShot300/600 or PWM ──► 4 ESC ──► motors
                                               │
                    blackbox logger (flash/SD) · telemetry (CRSF/MAVLink) · payload I/O
```

- 루프 주기 숫자는 Betaflight/PX4의 일반적인 설정 범위 [추정]. Neros의 실제 설정은 미확인
- 용어 정리: [C02 드론 스택 용어 사전](../../onsitePrep/site/company/2026-10-02_C02_drone_tech_stack_glossary.html), 프로토콜(CRSF·DShot·MSP): [FTE N06](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N06_protocols_and_drone_stack.html)

## 3. 무엇이 고장 나나

| Failure mode | 증상 | 어느 레벨에서 잡나 |
|---|---|---|
| 제어 루프 overrun (태스크 시간 초과) | 지터, 진동, 간헐적 "toilet bowl" | HITL (GPIO 토글 + LA로 루프 주기 측정), 비행 로그 |
| 센서 드라이버 버그 (축 방향·스케일·FIFO overflow) | 이륙 직후 flip, 드리프트 | component(실센서 벤치) + HITL 주입 |
| 진동 aliasing / 필터 설정 오류 | 모터 과열, oscillation | shaker + thrust stand, 비행 blackbox FFT |
| 모드 전환 경계 (angle ↔ alt hold, GPS 상실) | 고도 급강하, 갑작스런 자세 변화 | SITL 시나리오(대량), HITL 회귀 |
| RC 링크 손실 → failsafe | 기체가 계속 날아감 / 즉시 추락 | HITL(CRSF 주입 중단), 비행 시험 |
| failsafe 복귀(링크 회복) 처리 | 회복 후 throttle 점프 | SITL + HITL |
| estimator 발산 (GPS glitch, baro 압력 급변) | position hold 이탈 | SITL 고장 주입 (GPS jump, baro step) |
| 스택 포팅 회귀 (Betaflight→PX4) | "flight feel" 변화 (stick 응답) | 동일 입력 재생 → 출력 비교 (golden log) |
| 모터 출력 프로토콜 (DShot CRC, 타이밍) | 모터 stutter, ESC desync | HITL 출력 캡처 디코딩 |
| 부팅·arming 로직 | 잘못된 상태에서 arm 됨 (안전) | HITL arming 매트릭스 |
| blackbox 로깅 손실 | 사고 분석 불가 | HITL 장시간 + flash 가득 참 |
| 전압 sag / brownout 리셋 | 공중 리부트 | 전원 프로파일 주입(PSU), thrust stand |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | FW 개발자 [확인됨 공고 5195301007] | 필터 계수, PID 수식, mixer 행렬, CRSF 파서, 모드 상태 머신 | host (x86 빌드, gtest/Unity) | 모든 PR, < 2분 |
| component | FW 개발자 + Test | 센서 드라이버 ↔ 실제 센서, DShot 출력 ↔ 실제 ESC | 벤치 보드 | PR (태그된 경로 변경 시) / nightly |
| integration (SITL) | Test 조직 | 실제 FW 코드 + 물리 시뮬, 모드·failsafe·고장 주입 시나리오 수백 개 | CI 컨테이너 (Gazebo/jMAVSim/자체 시뮬) | PR 스모크 20개 / nightly 전체 |
| HIL (HITL) | **Test 조직** | 실제 FC 보드, 센서 신호 주입, RC 주입, 모터 출력 캡처, 타이밍 측정 | HIL 리그 팜 | PR 스모크 15분 / nightly 2시간 |
| system (벤치·구속) | Test + Flight SW | 실제 기체: thrust stand, tethered/gimbal rig, 진동 | 랩 | release candidate |
| field (비행 시험) | Test 파일럿 [확인됨 공고 존재: FPV Test Pilot 5103154007] + Flight SW | 실비행, blackbox 수집, 자동 로그 분석 | 시험장 | release gate |

> 원칙: **같은 시나리오 정의(YAML)**를 SITL과 HITL에서 공유하고, 비행 로그에도 같은 판정 함수(assertions over logs)를 쓴다. 테스트 피라미드 설명은 [FTE N02 2.1](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N02_hil_for_drones.html).

## 5. 테스트베드 설계

### 5.1 HITL 리그 (Flight SW 전용)

```text
                    ┌────────────── Rig host (Linux PC, pytest runner) ──────────────┐
                    │  sim engine (vehicle dynamics, 1 kHz)   scenario runner   log store │
                    └───┬──────────────┬──────────────┬──────────────┬───────────────┘
            USB/Eth     │       SPI/I2C│emulation     │ UART         │ USB
                ┌───────▼─────┐  ┌─────▼──────┐  ┌────▼──────┐  ┌────▼─────────┐
                │ Debug probe │  │ Sensor     │  │ CRSF RC   │  │ Output capture│
                │ (SWD flash, │  │ injector   │  │ injector  │  │ MCU/FPGA:     │
                │  reset)     │  │ (fake IMU/ │  │ (USB-UART)│  │ DShot decode, │
                └───────┬─────┘  │ baro/GPS)  │  └────┬──────┘  │ timestamp µs  │
                        │        └─────┬──────┘       │         └────▲─────────┘
                        ▼              ▼              ▼              │ 4× motor lines
                ┌──────────────────────────────────────────────────────┐
                │        DUT: flight controller (STM32), real firmware │── GPIO "loop tick" ──► LA
                └──────────────────────────────────────────────────────┘
                        ▲ programmable PSU (brownout, sag profiles) + relay (hard power cycle)
```

- **닫힌 루프**: 출력 캡처(모터 명령) → sim engine이 기체 운동 계산 → 다음 센서 값 주입. 이게 SITL과의 차이 — **실제 MCU·실제 드라이버·실제 타이밍**
- 센서 주입 방식 두 가지: (a) **버스 에뮬레이션** — 주입기가 IMU 칩인 척 SPI 응답 (진짜에 가깝지만 어렵다) (b) **FW 내 test hook** — 드라이버 아래에서 값 교체 (쉽지만 드라이버 경로를 우회) [추정: 둘 다 업계에서 쓰임]. 처음엔 (b)로 시작, 드라이버 회귀는 component 벤치에서

### 5.2 장비 표

| 장비 | 용도 | 대략 비용 [추정] |
|---|---|---|
| FC 보드 (DUT) × 리그당 1 | 실제 펌웨어 | $50–200 |
| Debug probe (ST-LINK V3 / J-Link) | 플래시, 리셋, 크래시 덤프 | $40–500 |
| 센서 주입기 (STM32/FPGA 보드) | SPI/I2C 슬레이브 에뮬레이션 | $50–300 |
| 출력 캡처기 (MCU 타이머 캡처 또는 FPGA) | DShot 디코딩, µs 타임스탬프 | $50–300 |
| USB-UART 3–4개 | CRSF 주입, CLI, 로그 | $20 |
| Programmable PSU + relay | 전압 프로파일, 하드 리셋 | $300–1,500 |
| Logic analyzer (Saleae 급) | 루프 지터, 버스 디버그 | $500–1,500 |
| Thrust stand (RCbenchmark 급) | 추력·전류·RPM | $1,000–5,000 |
| 진동 shaker | 필터·aliasing 검증 | $2,000–15,000 |
| Tethered / gimbal rig | 실기체 구속 비행 | 자체 제작 $500–3,000 |

### 5.3 자동화 구조 (pytest)

| 계층 | 이름 예 | 역할 |
|---|---|---|
| fixture (session) | `rig`, `sim`, `probe` | 리그 열기, sim engine 기동, 플래시 1회 |
| fixture (function) | `fc` (reset + 기본 파라미터), `rc` (CRSF 주입기), `motors` (출력 캡처) | 테스트마다 깨끗한 상태 |
| driver | `CrsfInjector.send(channels, rate_hz)`, `DshotCapture.read(window_ms)`, `SensorInjector.set(imu=…, baro=…)`, `Psu.profile([...])` | 계측기 추상화 |
| scenario | `scenarios/failsafe_rc_loss.yaml` | SITL·HITL 공유 |
| assertions | `assert_attitude_within(log, deg=10, after_s=2)` | 로그 기반 판정 — 비행 로그에도 재사용 |
| marker | `@pytest.mark.hitl`, `@pytest.mark.sitl`, `@pytest.mark.slow` | 어디서 돌릴지 |

- conftest 패턴(리그 session scope, 실패 시 로그 저장)은 [문제 04](../python/problems/04_hil_conftest.md) 그대로

### 5.4 리그 수 산정 [추정]

- 제품 3종(Archer, Archer AI, Bandit) × FC 보드 리비전 2 = 6 조합, 조합당 최소 2대(PR용 + nightly용) = **12대**
- PR당 HITL 스모크 15분, 하루 PR 40개 → 10 리그-시간, nightly 전체 2시간 × 6조합 = 12 리그-시간 → 12대면 여유 있음. 리그 예약·헬스는 [N04 문제 B](../notes/2026-10-02_N04_system_design_test_tooling.md)

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 (stimulus) | 관측 | 합격 기준 [추정] |
|---|---|---|---|---|
| F01 | 제어 루프 타이밍 | 정상 호버 + 최대 CPU 부하(로깅·텔레메트리 동시) | GPIO tick 주기 (LA) | 평균 = 설정 주기 ±1%, p99 지터 < 50µs, overrun 0 |
| F02 | arming 안전 매트릭스 | throttle 높음/센서 미보정/RC 없음 각각 arm 시도 | arm 상태, 모터 출력 | 금지 조건에서 arm 0회, 모터 출력 0 |
| F03 | RC 링크 손실 failsafe | 호버 중 CRSF 주입 중단 | 모드 전환 시각, 모터 출력 | 손실 후 failsafe 진입 ≤ 설정값(예 500ms), 정의된 동작(착륙/RTH) |
| F04 | 링크 복귀 | failsafe 중 CRSF 재개 (throttle 50%) | 출력 과도 | 즉시 full-throttle 점프 없음, 정의된 재-arm 규칙 준수 |
| F05 | altitude hold 외란 | baro step +2m, 돌풍(sim) | 고도 오차 | 3s 내 오차 < 0.5m, overshoot < 1m |
| F06 | GPS glitch | GPS 위치 50m jump 1초 | estimator 위치, 모드 | glitch 거부 또는 alt-hold로 degrade, 급기동 없음 |
| F07 | 모드 전환 경계 | angle → pos hold → acro 빠른 연속 전환 | attitude 오차 | 전환 시 자세 과도 < 10°, 적분기 windup 없음 |
| F08 | 모터 출력 프로토콜 | 전 throttle sweep | DShot 프레임 디코딩 | CRC 오류 0, 프레임 누락 0, 명령값 = 기대 mixer 출력 ±1 |
| F09 | 진동 내성 | shaker 200–400Hz 진동 + 호버 | gyro FFT, 모터 온도 | 필터 후 해당 대역 노이즈 > 20dB 감쇠 |
| F10 | brownout | PSU 16.8V → 9V sag 200ms | 리셋 여부, 로그 | 리셋 없음 또는 리셋 시 공중 재기동 정책 준수, 로그에 이벤트 기록 |
| F11 | 스택 포팅 회귀 | 기록된 stick 입력 재생 (old/new FW) | rate setpoint, 모터 출력 | 두 FW 응답 차이 RMS < 기준 (flight feel 유지) |
| F12 | blackbox 지속성 | 30분 호버 + flash 가득 참 | 로그 파일, 루프 타이밍 | flash full 시에도 F01 기준 유지, 로그 손상 0 |

## 7. 면접 시나리오 — "Design a test system for our flight controller firmware"

### 요구사항 질문 (영어)

- "How many flight-controller variants and flight stacks are we covering — Betaflight forks, PX4, or both?"
- "What's the release cadence, and what has to pass before firmware goes to flight test?"
- "Which failures hurt most today — timing, mode logic, sensor issues, or things that only show in flight?"
- "Do we already have SITL, or is the simulator part of the scope?"
- "Who writes the tests — Flight SW engineers, the Test team, or both? That decides how simple the API must be."

### 가정 숫자 [추정]

- FC 변종 6, 하루 PR 40, 시나리오 300개(SITL), HITL 시나리오 60개
- SITL 1개 30초 → 300개 병렬 10 컨테이너 = 15분. HITL 스모크 15개 × 1분 = 15분/PR

### 블록도

- 5.1 HITL 리그 + SITL 컨테이너 + 결과 파이프라인([N04 문제 C](../notes/2026-10-02_N04_system_design_test_tooling.md))

### 핵심 결정 & trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| SITL vs HITL 비중 | 시나리오 수는 SITL에서(싸고 병렬), **타이밍·드라이버·프로토콜은 HITL에서만** | SITL은 실제 MCU 타이밍을 모른다 |
| 센서 주입 | FW test hook으로 시작 → 버스 에뮬레이션은 IMU 하나만 | hook은 드라이버 경로를 우회 → component 벤치로 보완 |
| 판정 | 로그 기반 assertion 라이브러리 (SITL·HITL·비행 공통) | 초기 투자 필요, 대신 비행 로그 자동 판정 가능 |
| 시나리오 정의 | YAML (입력 타임라인 + 고장 주입 + 기대 판정) | 비개발자(파일럿, 시험 엔지니어)도 작성 가능 |
| 실패 분류 | product fail vs infra error (리그 스모크 실패 = infra) | 신뢰를 지킨다 |
| 비행 시험 | HITL 통과한 RC만, 비행 후 blackbox 자동 업로드·판정 | 비행 시간은 가장 비싼 자원 |

### 실패 모드 (테스트 시스템 자체)

- sim과 실기 불일치 → 비행 로그로 sim 파라미터 주기적 보정, "sim에서 통과·비행에서 실패" 케이스를 회귀 시나리오로 추가
- 리그 노후(케이블, 센서 주입기) → 리그별 스모크 통과율 추적, quarantine
- 타이밍 측정이 측정 자체로 왜곡 → GPIO 토글은 1–2 사이클, 로그 기반 측정은 오버헤드 확인

### 영어 2분 요약

> "I'd split it by what each layer can actually prove. Flight SW engineers own unit tests on the host. The test team owns two shared assets: SITL, where the real flight code runs against a physics model so we can run hundreds of mode, failsafe and fault-injection scenarios in parallel on every PR; and HITL rigs, where the real STM32 board gets injected sensor data and RC frames, and we capture the actual DShot output with microsecond timestamps — that's the only place timing, drivers and protocols are real. Scenarios are defined once in YAML and run on both. Judgments are assertions over logs, so the exact same checks run on blackbox logs after flight tests. Release candidates go through thrust stand and tethered tests, then flight test, and every flight log is uploaded and checked automatically. I'd classify rig failures as infrastructure, never as firmware regressions, and track flaky rates per scenario."

## 8. 꼬리 질문

**Q. What can't HITL catch?**

> "Aerodynamics, real vibration coupling, propwash, real RF conditions and pilot behavior. That's why I keep thrust stand, shaker and flight test in the plan, and feed every flight failure back as a new SITL or HITL scenario."

**Q. How do you measure control-loop jitter without disturbing it?**

> "Toggle a GPIO at the start of each loop and capture it on a logic analyzer or a timer-capture MCU — a couple of cycles of overhead. Then compute period distribution, p99 and max, and correlate overruns with logging or telemetry bursts."

**Q. How do you test failsafe safely?**

> "Never first on a free-flying vehicle. SITL for the logic, HITL for the timing with the real receiver path, then props-off on the bench, then tethered, then flight with a safety pilot and a geofence."

**Q. How would you catch a 'flight feel' regression when porting from Betaflight to PX4?**

> "Record stick inputs and sensor data from real flights, replay them into both builds in SITL or HITL, and compare rate setpoints and motor outputs. A step-response comparison gives a number pilots can agree with before they fly."

**Q. A bug only reproduces in flight. What do you do?**

> "Make the vehicle tell me more: blackbox at higher rate around the event, a trigger that captures a window before and after. Then replay that log into SITL or HITL to reproduce on the ground, and once it reproduces, it becomes a regression test."

**Q. Have you worked with Betaflight or PX4?**

> "Not hands-on — I'm honest about that. What I bring is building test infrastructure around real hardware and root-causing failures that only show up in the full system, which is most of what makes HITL useful. The flight-stack specifics I'd learn from the Flight SW team quickly."

## 9. Don 경험 연결 (레쥬메 범위)

| Flight SW 테스트 요소 | Don 경험 | 말할 문장 |
|---|---|---|
| "비행에서만 재현되는 버그" | Apple: 새 실리콘이 전체 HW/SW 시스템과 만날 때의 루트코즈 (PCIe/I2C/SPMI/RFFE) | "My job has been the bugs that only show up when everything is integrated." (Don: 구체 사례 하나) |
| 스트레스로 latent defect | Apple: stress 기반 latent defect 발굴 | "Stress and fault injection find what nominal tests never will." |
| UART 시퀀스 자동화 | SK hynix 챔버 플랫폼 + SDK (UART sequence) | "Injecting CRSF over UART on a rig is the same kind of automation I built for chamber tests." |
| 계측 | DSO, protocol analyzer, LA, Trace32 | "Timing claims need an instrument, not a log." |
| 비행 스택 | ❌ 없음 | 위 8절 마지막 답처럼 정직하게 |

## 체크

- [ ] 2절 데이터 흐름을 보지 않고 종이에 그리기 (센서 → 추정 → 제어 → 출력)
- [ ] 6절에서 F01·F03·F08을 영어로 각각 20초 설명
- [ ] 7절 영어 2분 요약을 소리 내어 2번
- [ ] "SITL로 되는 것 / HITL에서만 되는 것"을 한 문장씩
- [ ] [FTE N02 HIL](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N02_hil_for_drones.html) 3·4절 블록도와 이 노트 5.1을 비교

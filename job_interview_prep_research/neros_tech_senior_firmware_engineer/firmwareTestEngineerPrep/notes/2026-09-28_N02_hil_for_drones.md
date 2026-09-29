# 🛩️ N02 · HIL 원리부터 — 드론 Flight Controller HIL 리그 설계

> JD 필수 요건 1·2번: "5+ years of software testing with a focus on embedded systems and **HIL testing**", "Hands-on experience **building, setting up HIL test systems**". 이 노트 하나로 "우리 FC용 HIL 리그를 설계해 봐" 질문에 화이트보드로 답할 수 있게 한다.
> 순서: 테스트 계층(SIL/SITL/HIL/필드) → 리그 블록도 → 각 블록 역할 → HIL로 되는 것·안 되는 것 → flaky 원인 → Don 경험 연결 → 예상 질문과 영어 답.

---

## 1. 한 줄 정의

**HIL (Hardware-in-the-Loop)** = 진짜 펌웨어를 **진짜 보드(DUT, Device Under Test)** 위에서 돌리되, 보드가 보는 "바깥 세상"(센서 입력, RC 입력, 모터 부하, 전원)은 **테스트 장비가 흉내 내거나 제어**하는 테스트 방식이다.

- 펌웨어 입장에서는 비행 중인 것과 구분이 안 되도록 입력을 준다
- 테스트 장비는 펌웨어의 **출력**(모터 명령, telemetry, 로그)을 받아 기대값과 비교한다
- 사람이 조종기를 들고 날리지 않아도 되므로 **CI에서 매 커밋마다** 돌릴 수 있다

핵심 문장 (영어로 외워 둘 것):

> HIL means the real firmware runs on the real flight controller, and the test rig closes the loop around it — it feeds the inputs the board would see in flight and checks the outputs it produces.

---

## 2. 테스트 계층 — SIL · SITL · HIL · 필드

| 계층 | 무엇이 진짜인가 | 무엇이 가짜인가 | 속도·비용 | 잡을 수 있는 버그 |
|---|---|---|---|---|
| Unit test (host) | 함수·모듈 코드 | 나머지 전부 (mock) | 초 단위, 공짜 | 로직 버그, 경계값, 파서 |
| SIL / SITL | 펌웨어 전체 (PC용으로 컴파일) | MCU, 센서, 물리 (시뮬레이터) | 분 단위, 서버만 있으면 됨 | 제어 로직, 상태 머신, failsafe 흐름 |
| HIL | 펌웨어 + 실제 MCU·보드 | 센서 입력·RC·부하 일부 | 분~시간, 리그 비용 | 타이밍, 드라이버, DMA, 인터럽트, 부트, 전원 |
| Bench / Iron bird | 기체 전체 (프롭 없이) | 비행 자체 | 사람 필요 | 배선, ESC 연동, 전력, EMI |
| Flight test / Field | 전부 | 없음 | 비쌈, 느림, 위험 | 진동, 날씨, RF 환경, 재밍 |

- **SITL (Software-In-The-Loop)**: 펌웨어를 x86 Linux 프로세스로 빌드하고 물리 시뮬레이터(Gazebo 등)와 UDP로 연결해서 돌린다. Betaflight·PX4·ArduPilot 모두 SITL 타깃이 있다
- **HIL**이 SITL보다 느리고 비싸지만 반드시 필요한 이유: **MCU 위에서만 생기는 버그**가 있다. 인터럽트 우선순위, DMA 충돌, 스택 오버플로, 클럭 설정, 부트로더, flash 쓰기, 전원 브라운아웃 등
- 반대로 HIL로 **안 되는 것**은 필드로 간다: 실제 진동이 IMU에 주는 영향, 실제 RF 간섭, 온도

### 2.1 FW 테스트 피라미드

```text
                 ▲ 느림·비쌈·적게
          ┌─────────────┐
          │ Flight test │   릴리즈 전, 사람이 날림
         ┌┴─────────────┴┐
         │  HIL regression│  nightly, 리그 N대
        ┌┴───────────────┴┐
        │   HIL smoke     │  MR마다 5~10분
       ┌┴─────────────────┴┐
       │       SITL        │  MR마다, 서버에서 병렬
      ┌┴───────────────────┴┐
      │  Unit tests (host)  │  커밋마다, 수천 개, 초 단위
      └─────────────────────┘
                 ▼ 빠름·싸고·많이
```

- 원칙: **버그는 가능한 한 아래 계층에서 잡는다.** HIL에서 잡힌 버그가 unit test로 재현 가능하면 unit test를 추가한다
- HIL 리그는 비싸고 대수가 적다 → **HIL에서만 볼 수 있는 것만** HIL에 올린다 (예: 파서 로직은 unit, 파서가 DMA로 실제 UART를 받는 건 HIL)

---

## 3. 드론 FC HIL 리그 — 블록도

```text
                         ┌──────────────────────────── CI (GitLab runner, tag: hil-fc-01) ───┐
                         │   pytest  →  test library  →  drivers  →  transports              │
                         └───┬──────────┬───────────┬───────────┬──────────┬──────────────┬──┘
                             │USB       │USB        │USB/UART   │USB       │Ethernet/UDP  │GPIO/USB
                             ▼          ▼           ▼           ▼          ▼              ▼
                     ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌──────────────┐
                     │ Debug    │ │ Programm-│ │ RC 주입   │ │ Logic   │ │ GCS link │ │ PSU + relay  │
                     │ probe    │ │ able PSU │ │ (CRSF    │ │ analyzer│ │ / radio  │ │ (power cycle)│
                     │ SWD/JTAG │ │ + 전류측정│ │  emulator│ │ / capture│ │ 에뮬레이터│ │ + boot pin   │
                     └────┬─────┘ └────┬─────┘ └────┬─────┘ └────▲────┘ └────┬─────┘ └──────┬───────┘
                          │ SWD        │ VBAT        │ UART(RX)   │ DShot    │ telemetry    │
                          ▼            ▼             ▼            │ ×4       ▼              ▼
                     ┌────────────────────────────────────────────┴──────────────────────────────┐
                     │                     DUT: Flight Controller (STM32급 MCU [추정])             │
                     │   gyro/accel(SPI)  baro(I2C)  mag  GPS(UART)  OSD/VTX  blackbox flash       │
                     └──────────────▲─────────────────────────────────────────────────────────────┘
                                    │ 센서 주입 (옵션: 실제 센서 대신 sim 값 / 또는 실제 센서 + 모션 테이블)
                             ┌──────┴───────┐
                             │ 물리 시뮬레이터│  host PC: 기체 dynamics, 모터 출력 → 자세 → 센서값
                             └──────────────┘
```

위 그림을 화이트보드에 그릴 때는 **DUT를 가운데**, 왼쪽에 "입력(stimulus)", 오른쪽에 "출력(observation)", 위에 "제어(CI host)", 아래에 "전원"을 두면 설명이 깔끔하다.

---

## 4. 블록별 역할과 설계 포인트

### 4.1 DUT (Flight Controller)

- 양산과 **같은 하드웨어 리비전**을 쓴다. 리비전이 여러 개면 리그도 리비전별로 (Board A/B 변종 [추정])
- 테스트 전용 빌드와 양산 빌드를 둘 다 돌릴 수 있어야 한다. 테스트 훅은 **양산 바이너리에도 남아 있어야 진짜 검증** (test-only 빌드만 검증하면 양산 바이너리는 검증 안 된 것)
- 커넥터 마모 → 테스트용 **pogo pin 지그나 하네스**로 연결 [추정: 대량 양산 공장이므로 흔한 방식]

### 4.2 플래싱과 디버그 (debug probe)

- SWD/JTAG probe (ST-Link, J-Link 등) 또는 USB DFU로 매 테스트 전에 **해당 커밋의 바이너리를 플래시**
- 플래시 후 **버전 확인**: 부팅 후 버전 문자열·git hash를 읽어서 기대값과 비교. 이게 없으면 "옛날 펌웨어로 테스트 통과"라는 최악의 거짓 PASS가 생긴다
- 크래시 시 probe로 레지스터·스택 덤프를 자동 수집해 CI artifact로 남긴다

### 4.3 RC 링크 주입 (CRSF / ExpressLRS)

- 실제 조종기와 RF 링크 대신, FC의 RX UART에 **CRSF 프레임을 직접 쏘는** 에뮬레이터를 붙인다 (USB-UART 어댑터 + Python, 또는 작은 MCU)
- CRSF 기본 baud는 **420000**, 8N1, 반전 없음 (ExpressLRS 문서 기준)
- 이걸로 테스트할 것: arm/disarm 스위치, 스틱 입력 → 모터 반응, **RC 신호 끊김 → failsafe 진입**, 링크 품질(LQ) 저하 시 동작
- 더 현실적으로 하려면 실제 ELRS TX/RX 쌍을 쓰고 사이에 **프로그래머블 RF attenuator**를 넣는다 (N06 RF 테스트 참고)

### 4.4 모터 출력 캡처 (DShot / PWM)

- FC는 ESC에 모터 명령을 **DShot**(디지털, 16bit 프레임: throttle 11bit + telemetry 1bit + CRC 4bit) 또는 PWM으로 보낸다
- 리그는 이 신호를 **logic analyzer**(예: Saleae, Python API 있음)나 **캡처용 MCU**(타이머 input capture)로 받아 디코드
- 검증 항목: disarm 상태에서 모터 출력 0, arm 후 idle throttle, 스틱 입력에 맞는 **믹서 결과**(quad-X에서 roll 입력 시 좌우 모터 차이), 프레임 레이트, CRC 에러 없음
- 실제 ESC + 모터(프롭 없이)를 붙이면 전류·발열까지 볼 수 있지만 리그가 커진다. smoke용은 캡처만, 일부 리그만 실제 ESC [추정]

### 4.5 센서 주입

두 가지 방식이 있다.

- **방식 A — 실제 센서 그대로**: IMU는 보드에 붙어 있고 가만히 있으므로 "정지 상태"만 테스트 가능. 모션 테이블(짐벌/회전대)에 올리면 자세 변화도 테스트 가능 → 비싸지만 드라이버까지 진짜
- **방식 B — 시뮬레이터 값 주입**: 펌웨어에 "센서 소스 = 외부"인 HIL 모드를 두고, 물리 시뮬레이터가 계산한 가속도·각속도를 UART/USB로 넣는다. 폐루프 비행 시뮬레이션 가능. 대신 **센서 드라이버는 우회**됨
- 실무에선 둘을 섞는다: 드라이버 검증은 A, 제어·상태 머신 검증은 B
- GPS: UART에 NMEA/UBX 문장을 재생해서 주입. **GPS-denied 시나리오**(신호 끊김, 위치 튐)를 쉽게 만들 수 있다

### 4.6 전원 (PSU + relay)

- **프로그래머블 PSU**(SCPI over USB/LAN)로 배터리 전압을 흉내: 정상 16.8V(4S) → 저전압 경고 → critical
- **relay 또는 USB 허브 포트 제어**로 하드 파워 사이클. 테스트 사이에 "깨끗한 부팅"을 보장
- 전류 측정으로 **부팅 전류, idle 전류, sleep 전류** 회귀를 잡는다
- 브라운아웃 테스트: 전압을 순간적으로 떨어뜨려 리셋 후 정상 복구되는지, flash 쓰기 도중 전원 끊김에도 설정이 안 깨지는지

### 4.7 GCS 링크 / telemetry

- 지상국(GCS) 쪽 프로토콜로 telemetry를 받고 명령을 보낸다 (MSP, MAVLink, 또는 사내 프로토콜 [추정])
- 검증: telemetry **레이트**(예: 10Hz ± 허용오차), 필드 값 범위, 시퀀스 번호 누락, 명령 → ACK 지연
- Neros는 라디오와 GCS(Flatbow/Crossbow)를 in-house로 만든다 → 이 링크 자체도 테스트 대상일 가능성 큼 [추정]

### 4.8 CI runner

- 리그마다 호스트 PC(또는 Raspberry Pi/NUC)가 붙어 있고, 여기에 **GitLab runner를 tag와 함께 등록** (예: `hil`, `fc-rev-b`)
- 한 리그에 동시에 한 잡만: GitLab `resource_group` 또는 테스트 프레임워크 레벨 lock (N05 참고)
- 리그 **health check 잡**을 따로 둔다: 테스트 전에 probe 연결, PSU 응답, UART 루프백을 확인해서 "리그 고장"과 "펌웨어 버그"를 구분

---

## 5. HIL로 되는 것 · 안 되는 것

| 잘 되는 것 (HIL 대상) | 어렵거나 안 되는 것 (다른 계층) |
|---|---|
| 부팅 시퀀스, 부트로더, 펌웨어 업데이트 (A/B, 롤백) | 실제 진동 → IMU 노이즈, 필터 튜닝 (필드/모션 테이블) |
| arm/disarm 로직, failsafe (RC loss, low battery) | 실제 RF 환경, 재밍, 멀티패스 (필드 / RF 챔버) |
| 모터 출력 믹서, DShot 타이밍, 프레임 레이트 | 공기역학, 프롭 효율 (필드) |
| UART/I2C/SPI 드라이버, DMA, 인터럽트 부하 | 온도 극한 (챔버 — Don 경력 영역!) |
| 스케줄러 타이밍: loop rate, jitter, CPU load | 대량 기체 간 상호 간섭 |
| 설정 저장 (flash), 전원 끊김 내성 | 사람 조종 감각 |
| telemetry 레이트·내용, 명령 응답 | 장시간 배터리 노화 |

---

## 6. Flaky 테스트의 원인 — 리그에서 실제로 생기는 것

HIL은 진짜 하드웨어라서 **테스트가 가끔 이유 없이 실패**하는 일이 unit test보다 훨씬 많다. HM이 가장 궁금해할 것 중 하나.

| 원인 | 증상 | 대책 |
|---|---|---|
| USB 재열거 (enumeration) | 파워 사이클 후 `/dev/ttyACM0`가 `/dev/ttyACM1`로 바뀜, 또는 아직 안 나타남 | udev rule로 **시리얼번호 기반 고정 이름** (`/dev/hil/fc`), 포트 등장까지 **polling wait** (sleep 고정값 금지) |
| 부팅 타이밍 | 부팅 끝나기 전에 명령을 보내서 무시됨 | "ready" 배너/응답을 기다리는 `wait_until`, 타임아웃은 넉넉히 + 로그 |
| 고정 sleep | 느린 러너에서만 실패 | 전부 이벤트 기반 대기로 교체 |
| 전원 | PSU 전류 제한, relay 접점 바운스, 케이블 전압 강하 | PSU 설정 확인을 health check에 포함, relay 후 settle 시간 |
| 테스트 간 상태 누수 | 앞 테스트가 바꾼 설정이 다음 테스트에 영향 | fixture에서 **매 테스트 전 설정 초기화 또는 파워 사이클** |
| 리그 간 차이 | 리그 3번에서만 실패 | 리그 ID를 결과에 기록 → 리그별 실패율 대시보드 |
| 하드웨어 마모 | 커넥터 접촉 불량으로 간헐 실패 | 실패 재현 시 리그 health check, 지그 교체 주기 |
| 허용 오차 너무 빡빡 | 레이트 9.97Hz라서 FAIL | 스펙에서 온 허용 오차로, 통계적으로 판단 (평균·p99) |

**원칙**: flaky를 재시도로 덮지 않는다. 재시도로 통과해도 **"flaky" 라벨과 함께 기록**하고 추적한다 (N05 flaky 정책).

---

## 7. 좋은 HIL 테스트 케이스 하나의 모양

```python
@pytest.mark.hil
def test_rc_loss_triggers_failsafe(fc, rc, motors):
    fc.arm_via_rc(rc)                         # 준비: arm 상태로
    motors.wait_for_output(min_throttle=True, timeout=2.0)

    rc.stop()                                 # 자극: RC 신호 중단
    t0 = time.monotonic()

    fc.wait_for_state("FAILSAFE", timeout=1.5)  # 관찰: 스펙 시간 안에 failsafe
    elapsed = time.monotonic() - t0
    assert elapsed < FAILSAFE_DEADLINE_S, f"failsafe took {elapsed:.2f}s"

    motors.wait_for_output(stopped=True, timeout=FAILSAFE_STAGE2_S)  # 스펙대로 모터 정지(또는 착륙) 확인
```

- **Arrange → Act → Assert**가 분명하고, 대기는 전부 timeout 있는 이벤트 대기
- 실패 메시지에 **실제 값**이 들어간다 → CI 로그만 보고 원인을 짐작할 수 있다
- 요구사항(스펙의 failsafe 시간)과 **1:1로 추적** 가능 → 테스트 이름이 곧 요구사항

---

## 8. Don 경험 → HIL로 번역 (레쥬메 범위만)

| Don이 한 일 (레쥬메) | HIL 언어로 말하면 |
|---|---|
| SK hynix: 챔버 테스트 플랫폼 SDK — 온도 제어 API, 테스트 시나리오 스케줄링, eSSD 상태 모니터링, **UART 기반 테스트 시퀀스 자동화**, 다른 엔지니어들이 대량 챔버를 쓰게 함 | 실제 DUT 여러 대를 **환경(온도) 자극 + UART 명령/응답**으로 자동 테스트한 **HIL 프레임워크**. 스케줄링 = 리그 공유·잡 큐, 상태 모니터링 = 리그 health check, SDK = 다른 팀이 쓰는 test library |
| SK hynix: chip reliability system (NAND V6/7, PCIe 3/4/5 고속 인터페이스 커버리지) | 인터페이스 커버리지를 체계적으로 설계한 **테스트 시스템 구축** 경험 |
| SK hynix: shmoo·health monitoring debug FW (SoC/SI 팀 협업) | 마진 스윕 테스트 = 파라미터를 바꿔 가며 PASS/FAIL 경계를 찾는 **sweep 테스트**. 드론에선 전압·baud·패킷 레이트 스윕으로 전이 |
| Apple: **factory test-node 아키텍처** 설계·리드, stress 기반 시나리오로 MP 전에 latent defect 발굴 | 양산 라인 테스트 설계 + **stress/soak 테스트**. Neros 주당 ~1,200대 양산 → factory test와 HIL이 같은 test library를 공유하면 레버리지 큼 |
| Apple: PCIe/I2C/SPMI/RFFE 루트코즈, DSO·protocol analyzer | HIL에서 실패가 났을 때 **"리그 문제냐 펌웨어 문제냐"를 버스 레벨에서 가르는** 능력 |
| Skills: Python, Embedded C/C++ | 테스트 코드와 **테스트 대상 코드를 둘 다 읽는** 테스터 |

- 레쥬메에 "HIL"이라는 단어는 없다 → 면접에서 먼저 "I didn't call it HIL at the time, but..."으로 **정직하게 번역**한다
- 숫자(DUT 대수, 챔버 수, 테스트 시간 단축 등)는 **(Don: 실제 수치 채우기)** — 지어내지 말 것

---

## 9. 면접 예상 질문 + 영어 모범 답변

### Q1. "How would you design a HIL rig for our flight controller?"

구조: **목표 → 블록도 → 자극/관찰 → 자동화 → 신뢰성 → 확장** 순서로 2~3분.

> First I'd pin down what the rig must catch that SITL can't — timing, drivers, boot and power behavior on the real MCU. Then the rig itself: the flight controller in the middle, flashed over SWD by the CI host so every run tests the exact commit, and we read the version back after boot.
>
> On the input side, I'd inject RC over the receiver UART with a CRSF emulator, replay GPS on its UART, and control battery voltage with a programmable supply plus a relay for hard power cycles. On the output side, I'd capture the DShot lines with a logic analyzer or a small capture MCU and decode throttle and CRC, and read telemetry the way the ground station does.
>
> The host runs pytest with a layered library — tests call keywords like `arm()` or `wait_for_state()`, which sit on device drivers and transports — so the same tests can run against SITL. Each rig registers as a tagged GitLab runner with exclusive locking, and a health-check job runs first so we can tell a broken rig from a firmware bug.
>
> Then I'd scale: short smoke suite on every merge request, full regression and soak nightly, and track pass rates per test and per rig so flakiness is visible instead of hidden by retries.

### Q2. "What's the difference between SITL and HIL, and when would you use each?"

> SITL compiles the flight firmware for the host and connects it to a physics simulator, so it's fast, cheap and parallel — great for control logic, state machines and failsafe flows. HIL runs the same firmware on the real board, so it catches what only exists on the MCU — interrupt priorities, DMA, peripheral drivers, boot, flash and power. I'd push every test as low in the pyramid as it can go and keep HIL for what only HIL can see.

### Q3. "A HIL test fails once in twenty runs. What do you do?"

> I don't retry it into green. First I check whether it's the rig or the firmware — the health check, which rig, whether it fails on other rigs. Then I make it reproducible: loop it overnight with full logs and a logic-analyzer capture on failure. Most HIL flakiness I'd expect comes from fixed sleeps, USB re-enumeration after power cycles, or state leaking between tests, so I'd check those first. While it's being fixed, it goes into a quarantine lane so it doesn't block merges, but it stays visible with an owner.

### Q4. "Have you built a HIL system before?"

> Not under that name, but yes in substance. At SK hynix I built and deployed a test-platform SDK that drove large temperature chambers full of real SSDs — APIs for temperature control, scheduling test scenarios, monitoring each drive's status, and automating UART-based test sequences — and other engineers used it to run their own tests. That's real hardware in the loop with environmental stimulus and automated pass/fail. At Apple I designed the factory test-node architecture and used stress-based scenarios to surface latent defects before mass production. What's new for me is the flight-controller-specific stimulus, like CRSF and DShot, and I've been studying exactly that.

### Q5. "What can't you test in HIL?"

> Real vibration into the IMU, the real RF environment including jamming, aerodynamics, and temperature extremes unless the rig goes into a chamber. So HIL doesn't replace flight test — it makes flight test cheaper by making sure we only fly builds that already passed everything the bench can check.

---

## 10. 체크리스트

- [ ] 블록도(§3)를 빈 종이에 5분 안에 그리고 영어로 설명해 보기
- [ ] SITL vs HIL 차이를 30초 영어로 말하기 (Q2)
- [ ] Flaky 원인 표(§6)에서 3개를 골라 "증상 → 대책"을 말로 설명
- [ ] Q1 모범 답변을 소리 내어 2번 연습 (2~3분 안에)
- [ ] Q4 답변에 들어갈 Don 실제 수치 채우기 (챔버 DUT 수, 사용자 수 등)
- [ ] DShot 16bit 프레임 구성(11+1+4)과 CRSF 420000 baud 기억
- [ ] §7 failsafe 테스트 코드를 보지 않고 다시 써 보기

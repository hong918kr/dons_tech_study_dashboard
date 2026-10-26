# 🧰 R08 HIL 벤치 설계 — 계측기 · 배선 · 안전 · Python 드라이버

> HIL 벤치는 "보드 + 계측기 + 케이블 + 안전장치 + 그걸 사람 없이 돌리는 Python"이다. 이 단원은 벤치가 해야 할 일 8가지, 대표 토폴로지 4개(ASCII 배선도), 계측기별 역할 · Python 라이브러리 · 함정, 공통 timebase, RF conducted 셋업, 안전, 무인 복구, 벤치 smoke test, 설정을 데이터로, 1대 → N대 랙 확장까지 다룬다. 예제 코드: [`../code/bench_drivers.py`](../code/bench_drivers.py).

---

## 0. 이 단원의 위치

- 앞 단원 [R07](2026-10-24_R07_test_methodology_pyramid.md)은 "무엇을 어느 층에서 테스트하나"(unit → SITL → HITL → HIL → 비행 → 공장)를 정했다
- 이 단원 R08은 그 중 **HIL 층의 물리적인 몸통** — 책상 위에 무엇을 놓고, 어떻게 선을 잇고, Python이 각 장비를 어떻게 부르는지
- 다음 단원 [R09](2026-10-24_R09_scalable_reusable_test_platform.md)는 이 벤치 **위에** 올라가는 소프트웨어 플랫폼 (테스트 라이브러리 층 구조, 스케줄러, 결과 DB, CI 연동)
- 프로토콜 세부(CRSF 프레임, DShot 비트 타이밍)는 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md), Betaflight 내부 failsafe/스케줄러는 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md), 영상 · companion은 [R06](2026-10-24_R06_companion_video_linux_rtos.md)
- 예전 노트 [N02 HIL 원리](../../firmwareTestEngineerPrep/notes/2026-09-28_N02_hil_for_drones.md)는 "HIL이 뭐고 블록도가 어떻게 생겼나"를 다뤘다. 여기서는 겹치지 않게 **계측기 하나하나와 벤치 운영**으로 내려간다
- 새 서브시스템을 붙일 때 FW 쪽에서 무엇을 포팅하는지는 [Q01](../../onsiteKit/qa/2026-10-20_Q01_fw_side_of_subsystem_integration.md). 그 7층 중 "테스트 FW가 측정하고 PC가 판정한다"는 원칙이 이 벤치 설계의 바탕이다

---

## 1. 요약

- 벤치가 해야 할 일은 8개다: **power · flash · reset · stimulate · observe · measure · isolate · recover unattended**. 하나라도 사람 손이 필요하면 CI에 못 붙는다
- 대표 토폴로지는 4개: ① FC 단독 ② FC + RX + TX (RF conducted) ③ ESC + 모터 + thrust stand 풀스택 ④ companion · 영상
- 계측기는 "무엇을 할 수 있나"보다 **"원격으로 제어 가능한가, 상태를 되읽을 수 있나, 죽었을 때 어떻게 알 수 있나"**로 고른다
- 지연(latency) 측정은 **하나의 logic analyzer 안에 모든 신호를 넣어서** 공통 timebase를 만든다. 호스트 PC 시간은 ms 단위 이하에선 믿지 않는다
- RF는 **선으로만**(conducted) 다룬다. SMA 케이블 + 고정 감쇠기(pad) + 프로그래머블 감쇠기 + shield box. 공중에 재밍 신호를 쏘는 건 법적으로 금지
- 안전: 프롭 없음, 전류 제한 먼저, 퓨즈, e-stop, interlock. 무인 벤치에는 LiPo를 쓰지 않는다 (PSU 사용)
- 벤치는 매 실행 전에 자기 자신을 검사한다(**bench smoke test**). 벤치가 고장이면 결과는 FAIL이 아니라 **INFRA_FAIL**
- 벤치 구성은 코드가 아니라 **데이터**(YAML 인벤토리: 시리얼 번호, 포트, 능력)

---

## 2. 왜 중요한가 (FTE 관점)

- JD 필수 요건이 "**building, setting up HIL test systems**"이다. HIL 테스트를 "쓰는" 사람이 아니라 벤치를 "짓는" 사람을 찾는다
- HIL에서 가장 시간을 많이 잡아먹는 건 테스트 로직이 아니라 **벤치 신뢰성**이다. flaky의 대부분은 USB 재열거, 전원, 케이블, 고정 sleep에서 온다 ([N02 §6](../../firmwareTestEngineerPrep/notes/2026-09-28_N02_hil_for_drones.md))
- 계측기 선택이 나중에 바꾸기 가장 비싸다. 처음부터 "원격 제어 + 되읽기 + 고유 시리얼"이 되는 장비로 가야 N대로 늘릴 수 있다
- Neros는 대량 양산을 말한다 [추정: 공고 문구 기반, 구체 수치 미확인]. 같은 계측기 · 같은 드라이버를 **HIL 벤치와 공장 테스트 station이 공유**하면 레버리지가 크다 — Don의 factory test-node 경험과 바로 연결
- Don의 강점이 정확히 여기 있다: 챔버 테스트 플랫폼 SDK(온도 제어 · 스케줄링 · eSSD 상태 모니터링 · UART 시퀀스 자동화)는 "환경 자극 + 상태 관측 + 무인 운영"이었고, 최근 RF 칩셋 통합에서 DSO · protocol analyzer로 버스 레벨 루트코즈를 했다

---

## 3. 벤치가 해야 할 일 — 요구사항 8개

### 3.1 한 장 요약

| # | 능력 | 뜻 | 대표 장비 | 없으면 생기는 일 |
|---|---|---|---|---|
| 1 | Power | DUT 전원을 원격으로 켜고 끄고, 전압 · 전류 제한을 정하고, 전류를 읽는다 | 프로그래머블 PSU, relay, USB 허브 포트 전원 | 사람이 배터리를 꽂아야 함. 쇼트 시 보드 손상 |
| 2 | Flash | 테스트할 커밋의 바이너리를 정확히 올리고 **검증**한다 | SWD probe (J-Link, ST-Link, CMSIS-DAP), DFU | 옛날 펌웨어로 거짓 PASS |
| 3 | Reset | 소프트 리셋 · 하드 리셋 · 부트 모드 진입 | probe의 NRST, GPIO/relay로 BOOT 핀 | 멈춘 보드를 못 살림 |
| 4 | Stimulate | 비행 중 FC가 볼 입력을 준다 | RC 주입(CRSF), GPS 재생, PSU 전압 변경, RF 감쇠, 센서 주입 | 테스트할 게 부팅밖에 없음 |
| 5 | Observe | FC가 무엇을 하는지 본다 | UART/MSP/MAVLink telemetry, RTT 로그, blackbox | 판정 불가 |
| 6 | Measure | 시간 · 전기 · 물리량을 숫자로 잰다 | logic analyzer, 오실로스코프, 전류 측정, thrust stand | "느낌상 빠름" |
| 7 | Isolate | 벤치끼리, 벤치와 바깥 세상 사이를 분리한다 | shield box, 케이블 RF, 전기 절연(USB isolator), 네트워크 분리 | 옆 벤치 RF가 섞임, 그라운드 루프 |
| 8 | Recover unattended | 사람 없이 이상 상태에서 돌아온다 | 전원 사이클, probe 리셋, bootloader 복구, 벤치 offline 표시 | 밤새 벤치 1대가 멈추면 큐 전체가 멈춤 |

### 3.2 각 능력의 "완료 조건"

| 능력 | 완료 조건 (이게 되면 됐다) |
|---|---|
| Power | `psu.output(True)` 후 `measure_current()`가 기대 범위. 전류 제한이 **출력 ON 전에** 설정됨 |
| Flash | 플래시 후 **보드가 스스로 보고한 build id == CI가 기대한 build id** (해시 비교, 이름 비교 아님) |
| Reset | 모든 리셋 경로 후 부팅 배너가 `wait_until`로 확인됨 |
| Stimulate | 자극을 **되읽기**로 확인 (감쇠기 `ATT?`, PSU `MEAS:VOLT?`) |
| Observe | 관측 채널이 끊기면 테스트가 "관측 실패"로 끝남 (조용히 PASS 아님) |
| Measure | 측정값에 단위 · 장비 시리얼 · 교정일이 같이 기록됨 |
| Isolate | 벤치 A 실행 중 벤치 B에서 RF/전원 영향 없음을 한 번은 실험으로 확인 |
| Recover | 복구 사다리(§10)의 단계마다 자동 시도 → 실패 시 벤치를 offline으로 빼고 사람에게 알림 |

### 3.3 기능 요구사항 말고 "비기능" 요구사항

| 항목 | 질문 |
|---|---|
| 반복성 | 같은 펌웨어로 100번 돌리면 결과 분포가 좁은가? |
| 처리량 | 한 사이클(전원 → 플래시 → 테스트 → 정리)이 몇 분인가? 플래시가 병목인가? |
| 가용성 | 벤치가 한 달에 몇 번 사람 손을 필요로 하나? |
| 관측성 | 실패 1건에 대해 로그 · 캡처 · 장비 상태가 자동으로 남나? |
| 교체성 | DUT 하나, 케이블 하나, 계측기 하나를 바꾸는 데 몇 분? 설정 파일만 고치면 되나? |
| 이식성 | 같은 테스트가 SITL과 HIL에서 둘 다 도나? (인터페이스 추상화, R09) |

---

## 4. 대표 벤치 토폴로지 4개

설계 원칙: **DUT를 가운데**, 위에 호스트(제어), 왼쪽에 자극, 오른쪽에 관측, 아래에 전원. 모든 디지털 신호의 "지연 측정용 사본"은 하나의 logic analyzer로 모은다.

### 4.1 토폴로지 A — FC 단독 벤치 (가장 작고 가장 많이 둔다)

목적: 부팅 · 플래시 · 드라이버 · 스케줄러 타이밍 · RC 파서 · failsafe 로직 · DShot 출력 · 설정 저장. 무선 없음, 모터 없음.

```text
                     ┌──────────────────── Bench host (NUC / mini PC, Linux) ────────────────────┐
                     │  GitLab runner (tag: hil-fc)  →  pytest  →  bench drivers (R08)  →  USB/LAN│
                     └──┬───────────┬────────────┬────────────┬────────────┬────────────┬────────┘
                        │USB        │USB         │USB         │USB         │LAN         │USB
                        ▼           ▼            ▼            ▼            ▼            ▼
                 ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
                 │SWD probe │ │USB-UART#1│ │USB-UART#2│ │ GPIO/IO  │ │ Prog PSU │ │ Logic    │
                 │(J-Link / │ │ CRSF 주입 │ │ MSP/CLI  │ │ (FT232H) │ │ (SCPI)   │ │ analyzer │
                 │ ST-Link) │ │ 420000 8N1│ │ telemetry│ │ BOOT/MARK│ │ +relay   │ │ 8~16 ch  │
                 └────┬─────┘ └────┬─────┘ └────┬─────┘ └──┬───┬───┘ └────┬─────┘ └─▲──▲──▲──┘
                      │SWDIO/SWCLK │TX→FC RX    │TX/RX     │   │MARK      │VBAT     │  │  │
                      │NRST/GND    │            │          │BOOT          │(퓨즈)   │  │  │
                      ▼            ▼            ▼          ▼   └──────────┼─────────┘  │  │
                 ┌────────────────────────────────────────────────────┐   │ ch7 MARK   │  │
                 │      DUT: Flight Controller (프롭 · 모터 · ESC 없음)    │◄──┘            │  │
                 │  gyro(SPI) baro(I2C) flash   UART RX ◄ CRSF        │  ch0-3 DShot ───┘  │
                 │                               DShot M1..M4 ────────┼───────────────────┘
                 │                                                    │  ch4 CRSF 사본 (RX 핀)
                 └────────────────────────────────────────────────────┘
```

| 연결 | 무엇을 위해 | 메모 |
|---|---|---|
| SWD (SWDIO, SWCLK, NRST, GND, VTref) | 플래시, 리셋, 메모리 읽기, RTT | VTref로 타깃 전압 감지. NRST 연결해야 하드 리셋 가능 |
| USB-UART #1 → FC RX UART | CRSF 프레임 주입 (실제 RX 대신) | 420000 baud는 비표준 → 어댑터가 정확히 내는지 LA로 확인 |
| USB-UART #2 ↔ FC UART (또는 FC USB VCP) | MSP/CLI로 상태 읽기, 설정 | FC 자체 USB를 쓰면 FC 리셋 때마다 USB 재열거 → udev 필수 |
| GPIO (FT232H 등) | BOOT0 핀, MARK 신호(LA로), 리셋 대체 경로 | MARK = "호스트가 지금 X를 했다"는 표시 |
| PSU + relay → VBAT | 전원, 전류 측정, 저전압 시나리오 | relay는 "PSU 응답 없음" 때의 비상 차단 경로 |
| LA ch0~3 ← DShot, ch4 ← CRSF 입력, ch7 ← MARK | 공통 timebase로 지연 · 프레임 레이트 측정 | LA 그라운드를 FC GND에 |

### 4.2 토폴로지 B — FC + RX + TX, RF conducted 벤치

목적: 실제 무선 링크(ELRS 계열 또는 사내 무선 [추정])를 포함한 RC 경로 · 링크 품질 · failsafe · 링크 복구. 공중에 전파를 내지 않는다.

```text
  Bench host ──USB──► TX 모듈 (handset 대신 USB-UART로 CRSF 주입 또는 실제 handset)
     │                    │ SMA (TX ANT 포트)
     │                    ▼
     │              ┌───────────┐   ┌────────────────────┐   ┌───────────┐
     │              │ 고정 pad   │──►│ 프로그래머블 감쇠기  │──►│ 고정 pad   │──┐
     │              │ 30~40 dB  │   │ 0~90 dB, 0.25 dB step│  │ 20~30 dB  │  │ SMA
     │              └───────────┘   └─────────▲──────────┘   └───────────┘  │
     │                                        │ LAN (HTTP/Telnet) / USB      │
     ├────────────────────────────────────────┘                              ▼
     │                                                          ┌──────────────────────────┐
     │      ┌─────────────────── RF shield box ────────────────│ RX 모듈 (안테나 대신 SMA) │
     │      │  피드스루: SMA, USB 필터, DC 필터                 └────────────┬─────────────┘
     │      │                                                              │ UART (CRSF)
     │      │                                                              ▼
     │      │                                                 ┌──────────────────────────┐
     ├──USB─┼── SWD probe ───────────────────────────────────►│  DUT: Flight Controller  │
     ├──USB─┼── USB-UART (MSP/CLI) ──────────────────────────►│                          │
     │      │                                                 └──────────┬───────────────┘
     │      │                                                            │ DShot ×4
     │      └────────────────────────────────────────────────────────────┼──────────────
     │                                                                   ▼
     └──USB──► Logic analyzer: ch0 TX 입력 CRSF · ch1 RX 출력 CRSF · ch2-5 DShot · ch7 MARK
```

- TX 쪽과 RX 쪽 디지털 신호를 **같은 LA**에 넣으면 "TX 입력 → RX 출력 = 무선 구간 지연", "RX 출력 → DShot 변화 = FC 내부 지연"을 한 캡처에서 뺄 수 있다 (Gemini p.10의 아이디어 자체는 맞다)
- shield box는 RX와 FC를 넣는 쪽이 일반적 [확인 필요: 실제 구성은 벤치마다 다름]. 목적은 "케이블로만 신호가 간다"를 보장하는 것
- 감쇠기 앞뒤의 고정 pad는 **장비 보호용**이다. TX 출력이 감쇠기 최대 입력이나 RX 최대 입력을 넘지 않게 (§8)

### 4.3 토폴로지 C — 풀스택: FC + ESC + 모터 + thrust stand

목적: DShot → ESC → 모터 회전까지. ESC telemetry/bidirectional DShot RPM, 전류 · 전압 sag, 스로틀 급변 시 FC 리셋 여부, EMI가 IMU · 무선에 주는 영향, 추력 · 효율.

```text
                ┌──────────── 안전 케이지 (문 열림 = interlock open) ───────────┐
                │                                                            │
   PSU(고전류)──┼─[퓨즈]─[e-stop 접촉기]─[전류 shunt]──┬── ESC 4-in-1 ──── 모터 1~4
   or 배터리     │                                     │       ▲  (프롭은 thrust stand 측정
   (사람 있을 때만)│                                     │       │   대상 1개에만, 가드 안에서)
                │                                     │       │ DShot ×4 / telemetry
                │                                     └─► FC ─┘
                │                     thrust stand(로드셀 · 토크 · RPM · 전압 · 전류)
                └──────────────────────────────┬─────────────────────────────┘
                                               │ USB (stand 소프트웨어 / API)
   Bench host ──────────────────────────────────┤
     ├─ SWD probe, USB-UART(CRSF 주입), LA (DShot + ESC telemetry), 전류 프로브 → 스코프
     └─ interlock 상태 입력 (GPIO) — 열려 있으면 테스트 시작 거부
```

| 고려 | 내용 |
|---|---|
| 전원 | 모터 여러 개는 순간 수십 A. 일반 벤치 PSU 범위를 넘는 경우가 많다 → 고전류 PSU 또는 배터리. 배터리는 **사람이 있을 때만** |
| 역전류 | ESC의 능동 제동/감속은 전류를 전원 쪽으로 되돌릴 수 있다. 대부분의 PSU는 전류를 흡수(sink)하지 못해 전압이 튈 수 있다 → 큰 커패시터, 회생 흡수 장치, 또는 배터리 병렬 [확인 필요: 사용 ESC · PSU 사양으로 확인] |
| 프롭 | 기본은 **프롭 없음**. 추력 측정이 필요할 때만 thrust stand에 고정한 모터 1개에, 가드 · 케이지 안에서 |
| thrust stand | Tyto Robotics Flight Stand 15: 15 kgf 추력, 8 Nm 토크, 180 V · 150 A 상한. 소프트웨어는 Windows 전용이고 **Python API**로 원격 제어 가능 (네트워크 노출 경고 있음) |
| EMI | 모터가 돌 때 IMU 노이즈 · 무선 LQ · GPS 품질이 변하는지 = 단독 벤치로는 못 보는 것 |

### 4.4 토폴로지 D — companion · 영상 벤치

목적: Jetson류 companion ↔ FC 링크(MAVLink 등), 카메라 → 인코더 → 무선 영상 → 디코더 → 화면의 **glass-to-glass latency**, 부팅 순서 · 업데이트(A/B) ([R06](2026-10-24_R06_companion_video_linux_rtos.md)).

```text
  [LED 깜빡이(GPIO, 호스트 제어)] ──빛──► [카메라] ─► [Companion (Jetson류)] ─► [영상 TX] ═SMA+감쇠기═► [영상 RX/GCS]
         │                                         │ UART/Ethernet (MAVLink 등)                 │ HDMI/디스플레이
         │                                         ▼                                            ▼
         │                                       [FC]                               [화면 앞 photodiode]
         │                                                                                     │
         └──────────── LA ch0: LED 구동 신호 ───────────── LA ch1: photodiode 비교기 출력 ───────┘
                       glass-to-glass latency = ch1 상승 - ch0 상승 (수백~수천 회 반복 → 분포)
```

- 카메라로 화면을 찍어서 시계를 비교하는 방법보다 **LED + photodiode + LA**가 반복 자동화에 좋다 [확인 필요: 디스플레이 갱신 주기 · 노출 시간이 분해능을 제한]
- companion은 Linux라서 콘솔 UART, 네트워크 SSH, 전원 사이클, 부트 미디어 복구(eMMC/SD)까지 필요 → labgrid의 Strategy 모델이 잘 맞는 영역 (§12.5)

### 4.5 토폴로지별 비교

| | A FC 단독 | B RF conducted | C 풀스택 | D companion/영상 |
|---|---|---|---|---|
| 상대 비용 | 낮음 | 중간 (감쇠기 · shield box) | 높음 (stand · 고전류 전원 · 케이지) | 중간~높음 |
| 대수 | 많이 (CI 병렬) | 몇 대 | 1~2대 | 몇 대 |
| 무인 운영 | 쉬움 | 쉬움 (RF는 선으로만) | 제한적 (안전) | 중간 (Linux 복구 복잡) |
| 잡는 버그 | 드라이버, 스케줄러, 파서, failsafe 로직 | 링크 품질, 링크 끊김/복구, 무선 지연 | 전력 · EMI · ESC 연동 | 영상 지연, companion 연동, 업데이트 |
| 실행 주기 [추정] | MR마다 smoke | nightly | 릴리즈 전 · 주간 | nightly · 릴리즈 전 |

---

## 5. 계측기 하나씩 — 역할 · 인터페이스 · Python · 함정 · 가격대

가격대 표기: `$` < 100 USD, `$$` 100~1,000, `$$$` 1,000~10,000, `$$$$` > 10,000. 확인한 값만 숫자로 쓰고 나머지는 `[확인 필요]`.

### 5.1 한 장 요약표

| 장비 | 벤치에서 역할 | 인터페이스 | Python | 가격대 |
|---|---|---|---|---|
| 프로그래머블 PSU | 전원 · 전류 제한 · 전류 측정 · 저전압 시나리오 | LAN(LXI/SCPI), USB-TMC, RS-232 | PyVISA + SCPI | `$$`~`$$$` [확인 필요] |
| 전자 부하 (e-load) | 전원 쪽을 테스트할 때 부하 흉내, 회생 전류 흡수 실험 | LAN/USB SCPI | PyVISA | `$$`~`$$$` [확인 필요] |
| relay 보드 / 네트워크 PDU | 하드 전원 차단, BOOT 핀, 인터락 | USB(HID/FTDI), LAN(HTTP/SNMP) | 벤더 API, pyftdi, requests 대체(urllib) | `$`~`$$` |
| 포트 전원 제어 USB 허브 | USB 장치 전원 사이클 (probe, USB-UART, FC USB) | USB | `uhubctl` CLI 호출 | `$`~`$$` |
| SWD probe | 플래시, 리셋, 메모리, RTT | USB (일부 LAN) | pylink-square, pyOCD, OpenOCD(telnet/TCL), probe-rs CLI | `$`~`$$$` [확인 필요] |
| USB-UART | CRSF 주입, MSP/CLI, GPS 재생, 콘솔 | USB | pyserial | `$` |
| GPIO/USB-IO | 마커, BOOT, 버튼 흉내, 인터락 읽기 | USB | pyftdi (FT232H), LabJack LJM | `$`~`$$` |
| Logic analyzer | 공통 timebase, 프로토콜 디코드, 지연 분포 | USB | Saleae `logic2-automation`, sigrok-cli | `$`~`$$` (Saleae Logic 8: 499 USD, Logic Pro 8: 999 USD — Saleae 비교 페이지 기준) |
| 오실로스코프 | 아날로그 품질 (전원 리플, 신호 무결성, 리셋 파형) | LAN/USB SCPI | PyVISA | `$$`~`$$$$` |
| 전류 프로브 | 순간 전류, 돌입 전류 | 스코프 입력 | 스코프 경유 | `$$`~`$$$` [확인 필요] |
| 프로그래머블 RF 감쇠기 | 링크 경로 손실 · 링크 끊기 | USB, LAN(HTTP/Telnet) | urllib/socket (HTTP GET, Telnet) | Mini-Circuits RCDAT-6000-90: 1,003 USD (1~9개, 웹스토어 표기) |
| RF 분배기 · 결합기 · 고정 pad | 신호 나누기/합치기, 보호 | SMA | 없음 (수동) | `$`~`$$` |
| RF shield box | 방사 차단, 벤치 간 격리 | 피드스루 | 없음 (뚜껑 센서는 GPIO) | `$$`~`$$$` [확인 필요] |
| Thrust stand | 추력 · 토크 · RPM · 효율 | USB | Tyto Flight Stand Python API | `$$$` [확인 필요] |
| Photodiode + 비교기 / LED | 영상 glass-to-glass | GPIO → LA | LA 경유 | `$` |
| 온도 챔버 | 온도 극한 | LAN/RS-232 | 벤더 프로토콜 | `$$$$` |

### 5.2 프로그래머블 PSU (+ SCPI 기초)

역할: 배터리 대신 전원. 전압 설정 · **전류 제한** · 출력 ON/OFF · 전압/전류 측정. 저전압 경고 · 브라운아웃 시나리오.

SCPI 기초 (계측기 공통 문법):

| 명령 | 뜻 | 메모 |
|---|---|---|
| `*IDN?` | 제조사, 모델, 시리얼, 펌웨어 | smoke test에서 "살아 있나 + 맞는 장비인가" |
| `*RST` | 초기 상태 | 출력이 꺼지는지 장비마다 확인 [확인 필요] |
| `*OPC?` | 앞 명령 완료되면 `1` | 설정 후 바로 측정할 때 순서 보장 |
| `SYST:ERR?` | 에러 큐 읽기 | 명령 오타가 **조용히 무시**되는 걸 잡는다. 매 시퀀스 끝에 확인 |
| `VOLT 16.8` / `CURR 1.0` / `OUTP ON` | 전압 · 전류 제한 · 출력 | 정확한 명령 트리는 장비 매뉴얼 [확인 필요] |
| `MEAS:VOLT?` / `MEAS:CURR?` | 측정 | 설정값이 아니라 **측정값**을 기록 |

PyVISA 리소스 이름 형식 (PyVISA 문서 확인):

| 종류 | 형식 | 예 |
|---|---|---|
| LAN, VXI-11 | `TCPIP[board]::host address[::LAN device name][::INSTR]` | `TCPIP0::10.0.0.21::INSTR` |
| LAN, raw socket | `TCPIP[board]::host address::port::SOCKET` | `TCPIP0::10.0.0.21::5025::SOCKET` (5025는 흔한 SCPI 소켓 포트 [확인 필요: 장비별]) |
| USB-TMC | `USB[board]::manufacturer ID::model code::serial number[::USB interface number][::INSTR]` | 시리얼 번호가 이름에 들어 있음 → 여러 대 구분 쉬움 |
| 시리얼 | `ASRLboard[::INSTR]` | 장비에 따라 SCPI가 아닌 독자 명령 |

```python
# 실제 벤치용 스케치 (pyvisa 설치 필요 — 이 저장소 code/ 예제는 표준 라이브러리만 씀)
import pyvisa
rm = pyvisa.ResourceManager("@py")          # pyvisa-py 백엔드 (NI-VISA 없이)
psu = rm.open_resource("TCPIP0::10.0.0.21::5025::SOCKET",
                       read_termination="\n", write_termination="\n", timeout=2000)
print(psu.query("*IDN?"))
psu.write("CURR 1.0")                        # 1) 전류 제한 먼저
psu.write("VOLT 16.8")                       # 2) 전압
psu.write("OUTP ON")                         # 3) 출력
assert psu.query("*OPC?").strip() == "1"
print(psu.query("MEAS:CURR?"))
print(psu.query("SYST:ERR?"))               # 0,"No error" 류가 나와야 함
```

함정:

| 함정 | 증상 | 대책 |
|---|---|---|
| 전류 제한을 출력 ON 뒤에 설정 | 쇼트 보드에 순간 큰 전류 | 순서 고정: 제한 → 전압 → ON (코드의 `bring_up`) |
| PSU는 배터리가 아니다 | 내부 저항 · sag가 없어서 "배터리에선 나는 버그"가 안 남 | 직렬 저항/프로그래머블 출력 저항 기능(있는 장비만), 또는 C 벤치에서 실제 배터리 [확인 필요] |
| 회생 전류 흡수 못 함 | 모터 감속 때 출력 전압 상승 | §4.3 표 참고 |
| 원격 센스 미사용 | 케이블 전압 강하로 DUT 전압이 설정보다 낮음 | 4선(remote sense) 또는 DUT 쪽 전압을 따로 측정 |
| 명령 무시 | 오타 명령에 에러 없이 진행 | `SYST:ERR?` 확인, 설정 후 되읽기 |
| LAN 장비 IP 변경 | DHCP로 주소 바뀜 | 고정 IP 또는 DHCP 예약, 인벤토리에 MAC 기록 |

### 5.3 전자 부하 (electronic load)

- FC 벤치에선 필수 아님. **전원 보드 · BMS · 배터리 쪽**을 테스트하거나, ESC 회생 전류를 흉내 낼 때 쓴다
- 모드: CC(정전류) · CV · CR · CP. 동적 부하(펄스)로 스로틀 펀치를 흉내
- PSU + e-load 조합 = 간이 "배터리 시뮬레이터"는 아니다. 전용 배터리 시뮬레이터는 따로 있다 [확인 필요]

### 5.4 relay 보드 · 네트워크 PDU · USB 포트 전원

| 장비 | 쓰는 곳 | 메모 |
|---|---|---|
| USB relay 보드 | DUT VBAT 하드 차단, BOOT 핀, 버튼 흉내, 인터락 체인 | 기계식 relay는 접점 bounce → 전환 후 settle 시간. 수명(스위칭 횟수) 있음 |
| 네트워크 PDU (AC) | 계측기 자체를 전원 사이클 (멈춘 PSU, 멈춘 호스트 허브) | "마지막 수단" 복구 경로 |
| 포트 전원 USB 허브 + `uhubctl` | 멈춘 USB-UART, probe, FC USB 전원 사이클 | 허브가 **per-port power switching(PPPS)**을 지원해야 함 |

`uhubctl` (GitHub README 확인):

```text
uhubctl                         # 지원 허브와 위치 목록
uhubctl -l 1-1 -p 2 -a off      # 허브 1-1의 2번 포트 전원 끔
uhubctl -l 1-1 -p 2 -a cycle    # 끄고 → 대기(-d) → 켬
```

- 일부 허브는 **ganged** 스위칭: 포트 하나를 끄면 묶인 포트들이 같이 꺼진다
- 일부 허브는 데이터만 끊고 VBUS 전원은 안 끊는다 → 장치를 꽂고 실제로 꺼지는지 시험해 봐야 한다
- USB3 허브는 USB2/USB3 가상 허브 두 개로 보인다. 둘 다 꺼야 효과가 있다 (uhubctl이 기본으로 처리)
- Raspberry Pi 내장 포트는 ganged라서 포트별 제어가 안 된다 (모델별 상세는 README)

### 5.5 SWD 디버그 probe — 플래시 · 리셋 · 메모리 · RTT

| 도구 | 무엇 | 벤치에서 쓰는 법 | 메모 |
|---|---|---|---|
| SEGGER J-Link + **pylink-square** | 상용 probe + Python 래퍼 | `import pylink` | 패키지 이름은 `pylink-square`, import 이름은 `pylink` |
| ST-Link / CMSIS-DAP + **pyOCD** | Python으로 쓴 probe 도구 · 라이브러리 | `pyocd` CLI 또는 Python API | 타깃 지원 팩 필요할 수 있음 |
| **OpenOCD** | 범용 오픈소스 디버거 서버 | `-c "program fw.elf verify reset exit"` 한 줄로 플래시 | gdb/telnet/TCL 포트로 원격 제어 |
| **probe-rs** | Rust로 쓴 probe 도구 | `probe-rs run --chip <chip>` (플래시 + 실행 + RTT 콘솔), `attach` (리셋 · 플래시 없이 붙기) | 원격 호스트 기능도 있음 |

pylink 메서드 (pylink 문서 확인):

```python
import pylink                                   # pip install pylink-square
jl = pylink.JLink()
jl.open(serial_no=801234567)                    # 시리얼로 특정 probe 선택 (여러 벤치 필수)
jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
jl.connect("STM32F405RG", speed="auto")         # 칩 이름은 J-Link 장치 목록 기준 [확인 필요]
jl.flash_file("fw.bin", 0x08000000)             # flash_file(path, addr, on_progress=None, power_on=False)
jl.reset(ms=0, halt=False)                      # reset(ms=0, halt=True) 가 기본값 → halt=False로 실행
jl.rtt_start()                                  # rtt_start(block_address=None)
print(jl.rtt_read(0, 1024))                     # 위 방향 버퍼 0 (target → host)
jl.rtt_write(0, list(b"status\n"))              # 아래 방향 버퍼 (host → target)
words = jl.memory_read32(0x20000000, 4)         # 실행 중 RAM 읽기
```

pyOCD Python API (pyOCD 문서 예제):

```python
from pyocd.core.helpers import ConnectHelper
from pyocd.flash.file_programmer import FileProgrammer
with ConnectHelper.session_with_chosen_probe() as session:
    target = session.board.target
    FileProgrammer(session).program("my_firmware.bin")
    target.reset_and_halt()
    print("pc: 0x%X" % target.read_core_register("pc"))
```

OpenOCD 플래시 (OpenOCD 매뉴얼 확인):

```text
openocd -f board/stm32f3discovery.cfg -c "program filename.elf verify reset exit"
openocd -f board/stm32f3discovery.cfg -c "program filename.bin exit 0x08000000"   # .bin은 주소 필요
```

**RTT가 뭔가** (SEGGER KB 확인):

- 타깃 RAM에 **control block**과 링 버퍼들을 두고, probe가 CPU를 멈추지 않고(background 모드) 그 RAM을 읽고 쓴다
- **양방향**이다: up 채널(target → host)과 down 채널(host → target)이 따로 있다
- 디버거가 없을 때 기본 모드는 non-blocking (버퍼가 차면 버리거나 잘라 씀). `BLOCK_IF_FIFO_FULL` 모드는 버퍼가 찰 때 애플리케이션을 멈출 수 있다 → **비행 펌웨어엔 쓰면 안 되는 모드**
- 속도는 SEGGER 페이지 안에서도 수치가 엇갈린다 (최대 2 MB/s · 3.5 MB/s 표기 둘 다 있음). "UART보다 훨씬 빠르고 CPU 부담이 작다" 정도로 기억

> ⚠️ Gemini 정정: Gemini(p.20~21)는 "SWD 디버그 핀은 명령을 내리는 곳이 아니라 플래시 · 리셋 · RTT 로그 **전용**"이라고 했다. 반만 맞다. RTT는 down 채널이 있어 **호스트 → 타깃 명령도 보낼 수 있고**, SWD로 실행 중 RAM을 읽고 쓰는 것도 가능하다. 진짜 이유는 다르다: (1) 양산 기체엔 SWD가 연결돼 있지 않으므로 **출하되는 경로(UART · CRSF · MSP · MAVLink)로 테스트해야** 그 경로가 검증된다, (2) SWD 접근이 타이밍에 아주 작은 영향이라도 줄 수 있다. 그래서 "명령은 제품 인터페이스로, SWD/RTT는 플래시 · 진단 · 크래시 덤프 · 백도어 관측용"이 설계 원칙이지 "SWD로는 명령 못 함"이 아니다.

probe 함정:

| 함정 | 대책 |
|---|---|
| probe 여러 개가 꽂혀 있는데 "첫 번째 probe" 사용 | 항상 시리얼로 선택 (`open(serial_no=...)`, pyOCD `--uid`, probe-rs `--probe vid:pid:serial`) |
| 플래시 성공 메시지만 믿음 | 부팅 후 보드가 보고한 build id를 CI 기대값과 비교 (`bring_up`) |
| read-out protection(RDP) 걸린 보드 | mass erase 필요 → 복구 사다리 마지막 단계 [확인 필요: 칩별 절차] |
| 저전력 모드에서 SWD 끊김 | debug-in-sleep 설정 또는 connect-under-reset |
| VTref 미연결 | 레벨 감지 실패 → 연결 불안정 |
| 플래시가 병목 | 큰 이미지는 수 초~수십 초 [확인 필요]. 같은 이미지면 건너뛰기(OpenOCD `preverify`) |

### 5.6 USB-UART와 pyserial

- 용도: CRSF 주입(FC RX 핀으로), MSP/CLI, GPS NMEA/UBX 재생, 콘솔
- CRSF는 **420000 baud**(ELRS 문서 기준, 자세한 건 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)). 표준 baud가 아니므로 어댑터 칩이 그 값을 낼 수 있는지 확인. FTDI 계열이 흔히 쓰인다 [확인 필요: 칩별 실제 오차는 LA로 측정]
- pyserial URL 핸들러 (pyserial 문서 확인): `loop://` (루프백, 테스트용), `socket://` (raw TCP), `rfc2217://` (네트워크 시리얼 서버), `spy://` (송수신 로그), `hwgrep://` (VID:PID · 설명 정규식으로 포트 찾기)

```python
import serial
from serial.tools import list_ports
for p in list_ports.comports():
    print(p.device, p.serial_number, p.vid, p.pid, p.location)   # 시리얼 번호로 고르기
s = serial.Serial("/dev/hil/fc-01-crsf", baudrate=420000, timeout=0.05)
s.write(frame_bytes)
```

함정:

| 함정 | 증상 | 대책 |
|---|---|---|
| `/dev/ttyUSB0` 번호가 재부팅마다 바뀜 | 다른 장비에 명령 | udev 규칙으로 시리얼 기반 고정 이름 (§12.2), 또는 `/dev/serial/by-id/` |
| FTDI latency timer | 작은 패킷이 묶여서 늦게 도착 (기본값이 ms 단위) [확인 필요: Linux 기본 16 ms] | `/sys/bus/usb-serial/devices/ttyUSBx/latency_timer`를 1로 [확인 필요] |
| 호스트 OS 스케줄링 | 주입 프레임 간격이 흔들림 → "CRSF 4 ms 간격" 같은 정밀 주기가 안 나옴 | 정밀 주기가 필요하면 **작은 MCU가 프레임을 생성**하고 PC는 내용만 바꾼다 |
| FC 자체 USB VCP | FC 리셋 = USB 사라짐 → 포트 핸들 무효 | 재열거 대기(`wait_until(port exists)`) 후 다시 열기 |
| 3.3V/5V 레벨, 신호 반전 | SBUS는 반전 신호 | 어댑터 레벨 · 반전 설정 확인 |

### 5.7 GPIO / USB-IO — 마커, BOOT, 버튼

| 장비 | Python | 메모 |
|---|---|---|
| FT232H 브레이크아웃 | pyftdi. URL 형식 `ftdi://[vendor][:[product][:serial\|:bus:address\|:index]]/interface`, 예 `ftdi://ftdi:232h:FT0FMF6V/1` (pyftdi 문서 확인) | GPIO + SPI + I2C master. 시리얼로 여러 개 구분 |
| LabJack (U3/T4/T7 등) | `labjack-ljm` (LJM 라이브러리) [확인 필요: 모델별] | 아날로그 입력 · DAC · 카운터까지. 느린 신호 측정 겸용 |
| MCU 보드 (Pico 등)를 IO 확장기로 | pyserial로 간단한 명령 | 정밀 타이밍이 필요하면 이게 제일 유연 |

- **MARK 신호**: 호스트가 "지금 링크를 끊는다" 같은 행동을 할 때 GPIO를 토글하고, 그 선을 LA에 넣는다. 그러면 호스트 행동이 DUT 신호와 같은 timebase에 찍힌다 (§7)
- USB GPIO 자체의 지연(수백 µs~ms)은 MARK가 "언제 찍혔는지"에 영향 없음 — LA는 **선이 실제로 바뀐 순간**을 기록하기 때문. 다만 "MARK와 실제 행동(감쇠기 변경) 사이" 지연은 남는다 → 감쇠기 효과를 RX 출력 신호로 직접 보는 게 더 정확 (코드 예제는 "마지막 정상 프레임" 기준으로 잰다)

### 5.8 Logic analyzer — 벤치의 시계

Saleae Logic 2 Automation API (Saleae 문서 확인):

- 패키지 `logic2-automation`, Logic 2.4.0 이상 앱 필요. 앱의 Preferences에서 automation server를 켜면 기본 포트 **10430**에서 대기 (gRPC)
- 즉 **Logic 2 앱이 떠 있어야 한다** → 헤드리스 서버에서 쓰려면 앱 실행 방식까지 설계해야 한다 [확인 필요: 헤드리스 실행 옵션]

```python
from saleae import automation
with automation.Manager.connect(port=10430) as manager:
    dev = automation.LogicDeviceConfiguration(
        enabled_digital_channels=[0, 1, 2, 3, 4, 7],
        digital_sample_rate=100_000_000,          # 장비가 지원하는 값 중에서
        digital_threshold_volts=3.3)
    cap = automation.CaptureConfiguration(
        capture_mode=automation.TimedCaptureMode(duration_seconds=5.0))
    with manager.start_capture(device_configuration=dev,
                               capture_configuration=cap) as capture:
        capture.wait()
        uart = capture.add_analyzer("Async Serial", label="crsf",
                                    settings={"Input Channel": 4, "Bit Rate (Bits/s)": 420000})
        capture.export_data_table(filepath="out/crsf.csv", analyzers=[uart])
        capture.export_raw_data_csv(directory="out", digital_channels=[0, 1, 2, 3, 7])
```

- 위 `Async Serial` 설정 키 이름은 예시다 [확인 필요: 앱 버전별 analyzer 설정 키]. 문서 예제에서 확인된 건 `SPI` analyzer와 `MISO/Clock/Enable` 키
- 모델: Logic 8 = 8채널 100 MS/s, Logic Pro 8 = 8채널 500 MS/s, Logic Pro 16 = 16채널 500 MS/s (Saleae 페이지/비교표 기준)

sigrok / PulseView / sigrok-cli (sigrok wiki 확인):

```text
sigrok-cli -d fx2lafw --config samplerate=1m --samples 100          # 저가 FX2 기반 LA로 캡처
sigrok-cli -i capture.sr -P uart:baudrate=115200:parity_type=odd    # 저장된 캡처를 UART 디코드
sigrok-cli -i capture.sr -P i2c,i2cfilter,edid -A i2c=data-read:data-write
sigrok-cli [...] -o example.csv -O csv:dedup:header=false
```

- 저가 FX2 계열 LA(24 MHz급)는 홈 벤치 · 학습용으로 좋다. DShot600 비트 폭이 약 1.67 µs라서 디코드는 가능하지만 엣지 지연 측정 분해능은 샘플 주기(약 42 ns @ 24 MS/s)에 묶인다
- USB 대역폭: 높은 샘플레이트 + 많은 채널 + 긴 캡처는 USB를 꽉 채운다. 같은 USB 컨트롤러에 다른 장비를 몰지 말 것 (§12.1)

LA 함정:

| 함정 | 대책 |
|---|---|
| 그라운드 미연결 / 긴 그라운드 리드 | 신호마다 가까운 GND, 짧게 |
| 임계 전압 불일치 (1.8V 신호에 3.3V 임계) | 채널별 임계 설정 |
| 캡처 길이 부족으로 이벤트를 놓침 | 트리거 캡처 또는 링 버퍼 모드 [확인 필요: 모드 이름] |
| 결과를 사람이 눈으로 봄 | CSV/raw export → Python으로 판정, 실패 시 캡처 파일을 CI artifact로 |

### 5.9 오실로스코프 · 전류 프로브

- LA는 0/1만 본다. **아날로그 품질**(전원 리플, 브라운아웃 때 레일 모양, 리셋 핀 파형, 신호 링잉)은 스코프로
- 스코프도 SCPI(PyVISA)로 원격 캡처 · 측정값 읽기 · 화면 저장 가능. 명령 트리는 제조사마다 다르다 [확인 필요]
- 전류 프로브: 홀 센서 클램프(DC 포함)는 모터 돌입 전류 · 스로틀 펀치 측정용. shunt + 차동 프로브도 방법
- Don 연결: 최근 RF 칩셋 통합에서 DSO · protocol analyzer로 PCIe/I2C/SPMI/RFFE 루트코즈를 했다 → "테스트가 실패했을 때 리그 문제인지 FW 문제인지 버스 레벨에서 가르는" 역할에 바로 쓰인다

### 5.10 RF 장비 — 감쇠기 · 분배기/결합기 · shield box

Mini-Circuits RCDAT-6000-90 (Mini-Circuits 웹스토어/매뉴얼 확인):

| 항목 | 값 |
|---|---|
| 주파수 | 1 MHz ~ 6 GHz (900 MHz · 2.4 GHz 둘 다 덮음) |
| 감쇠 범위 | 0 ~ 90 dB |
| 분해능 | 0.25 dB |
| 제어 | USB + Ethernet. Ethernet은 HTTP GET 또는 Telnet (기본 Telnet 포트 23, HTTP 80) |
| 명령 예 | `:SETATT=12.75`, `:ATT?`, `:MN?`(모델), `:SN?`(시리얼), 다채널은 `:CHAN:2:SETATT:15.75` |
| HTTP 예 | `http://10.10.10.10/:SETATT=12.75` |
| 전원 켤 때 값 | `:STARTUPATT:...`로 지정 가능 → **켜질 때 최대 감쇠로** 두면 안전 |
| 최대 입력 전력 | [확인 필요: 데이터시트] |
| 가격 | 1,003 USD (1~9개, 웹스토어 표기, 재고 0 표시) |

```python
# 표준 라이브러리만으로 HTTP 제어 (실제 장비 IP로)
from urllib.request import urlopen
def att_set(ip, db):
    return urlopen(f"http://{ip}/:SETATT={db:.2f}", timeout=2).read().decode().strip()
def att_get(ip):
    return float(urlopen(f"http://{ip}/:ATT?", timeout=2).read().decode())
```

- 분배기(splitter)/결합기(combiner): TX 1대 → RX 여러 대, 또는 신호 + 간섭원을 합칠 때 (간섭원은 **선 안에서만**)
- shield box: 피드스루(SMA, USB 필터, DC 필터)를 통해 선만 들어간다. 뚜껑 닫힘을 GPIO로 읽어 interlock에 넣을 수 있다
- 결합 손실 · 케이블 손실은 주파수마다 다르다 → **교정 표**로 관리 (§11)

### 5.11 Thrust stand

- Tyto Robotics (구 RCbenchmark) Flight Stand 15: 15 kgf 추력, 8 Nm 토크, 180 V · 150 A 상한 (Tyto 페이지). Standard 100 Hz · Pro 1,000 Hz 샘플링 [확인 필요: 검색 결과 요약 기준]
- 소프트웨어는 Windows 전용, **Python API** 제공(GitLab의 `flightstand-api`), 네트워크로 원격 제어 가능하지만 "같은 네트워크의 누구나 스로틀을 켤 수 있다"는 경고 → 벤치 네트워크 분리 필수
- FTE 용도: 모터 · ESC 펌웨어 회귀(같은 스로틀 → 같은 RPM · 추력 · 전류인가), 스텝 응답, ESC telemetry 값 검증(ESC가 보고한 전류 vs stand 측정)

### 5.12 그 밖에

| 장비 | 언제 |
|---|---|
| 회전대 / 모션 테이블 | IMU 드라이버를 진짜 움직임으로 검증 (센서 주입 대신) |
| GPS 시뮬레이터 / 녹화 재생 | GPS 모듈까지 포함한 테스트 (UART NMEA 재생은 모듈을 우회) [확인 필요: 장비 · 법규] |
| SDR / 스펙트럼 분석기 | 무선 출력 · 채널 점유 · 호핑 관찰 (선으로 연결) |
| 관리형 스위치 | Ethernet 기반 링크 테스트, 지연 · 손실 주입은 Linux `tc netem`이 흔한 방법 [확인 필요] |
| 온도 챔버 | Don의 본 영역. 드론 FC · 무선 · 배터리의 온도 극한 |

---

## 6. 센서 입력은 어떻게 넣나 — 세 가지 방법

| 방법 | 어떻게 | 진짜인 것 | 우회되는 것 | 난이도 |
|---|---|---|---|---|
| ① 실제 센서 + 실제 움직임 | 보드를 회전대/짐벌에 올림 | 센서 · SPI · 드라이버 · 필터 · 제어 | 없음 (단 움직임 범위 제한) | 장비 비쌈 |
| ② 펌웨어 안 주입 지점 | 테스트 빌드/모드에서 드라이버 출력 자리에 외부 값 주입 (UART/MSP/MAVLink) | 필터 · 제어 · 상태 머신 | 센서, SPI, 드라이버 타이밍 | 펌웨어 지원 필요 |
| ③ 하드웨어 센서 에뮬레이터 | FC의 SPI 버스에 MCU/FPGA가 센서인 척 응답 | SPI 드라이버, DMA, EXTI | 센서 칩 자체 | 매우 어려움 |

- PX4는 ②를 공식으로 지원한다: HITL에서 실제 FC 하드웨어 위의 PX4가 USB/UART로 시뮬레이터와 연결되고, HITL 설정은 실제 센서를 시작하지 않는다 (PX4 문서). 단 문서 스스로 "community supported, 최신 버전에서 동작 안 할 수 있음"이라고 적는다
- INAV는 SITL에서 시뮬레이터(X-Plane, RealFlight)가 센서를 대신하고, X-Plane HITL 플러그인이 별도 프로젝트로 있다 (INAV 문서)
- Betaflight 메인라인의 하드웨어 HIL 센서 주입 모드는 확인 못 했다 [확인 필요]. SITL 타깃은 있다 ([R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md))

> ⚠️ Gemini 정정: Gemini(p.19, 21)는 "Linux PC는 8 kHz를 못 맞추니 보조 MCU/FPGA를 끼워 8 kHz로 가짜 자이로 데이터를 SPI로 밀어 넣는다"고 했다. 방향은 이해되지만 쉽게 말하면 안 된다. (1) FC의 SPI에서 **FC가 master, 센서가 slave**다. 에뮬레이터는 데이터를 "밀어 넣는" 게 아니라 FC가 MHz급 SPI 클럭으로 레지스터를 읽을 때 **그 순간에 맞는 바이트로 응답**해야 한다. (2) WHO_AM_I · 설정 쓰기 · 설정 되읽기 · FIFO/데이터 레지스터 · data-ready 인터럽트까지 센서의 **init 시퀀스와 레지스터 맵 전체**를 흉내 내야 드라이버가 받아 준다. (3) 자이로는 보통 FC 보드에 납땜돼 있어 같은 SPI 라인을 공유한다 → 실제 센서를 떼거나 CS를 분리하는 **보드 개조**가 필요하다. (4) ICM-42688-P는 Betaflight에서 8 kHz, BMI270은 3.2 kHz로 센서마다 다르다 (Betaflight CLI 문서). 그래서 실무에서는 먼저 ②(펌웨어 주입 지점)나 ①(실제 움직임)를 쓰고, ③은 SPI 드라이버 자체를 검증해야 할 때 FPGA로 하는 특수 프로젝트로 본다 [추정].

---

## 7. Timebase · 동기화 — "몇 ms 걸렸나"를 믿을 수 있게

### 7.1 원칙

- **원칙 1**: 지연을 재는 두 사건은 **같은 장비의 같은 시계**로 찍는다. 그게 logic analyzer다
- **원칙 2**: 호스트 PC의 `time.monotonic()`은 "호스트가 명령을 보낸 시각"이지 "선이 바뀐 시각"이 아니다. USB 스택 · OS 스케줄링 · 장비 내부 처리 지연이 끼어 있다 → ms 이하 판정에 쓰지 않는다
- **원칙 3**: 호스트 행동을 DUT 신호와 맞추려면 **MARK GPIO**를 LA에 넣는다
- **원칙 4**: 장비가 여러 대(LA 2대, 스코프, thrust stand)면 **공통 기준 신호**(PPS 또는 주기 펄스)를 모두에 넣어 사후 정렬한다

### 7.2 신호 → LA 채널 배정 예 (토폴로지 B)

| LA 채널 | 신호 | 무엇을 재나 |
|---|---|---|
| ch0 | TX 모듈 입력 CRSF (호스트/handset → TX) | 링크 시작점 |
| ch1 | RX 출력 CRSF (RX → FC) | 무선 구간 지연 = ch1 - ch0, 프레임 유실 |
| ch2~5 | DShot M1~M4 | FC 내부 지연 = DShot 변화 - ch1, 프레임 레이트, 값 디코드 |
| ch6 | FC 디버그 GPIO (펌웨어가 루프 시작마다 토글) [테스트 빌드 옵션] | 루프 주기 · 지터 |
| ch7 | MARK (호스트 GPIO) | "감쇠기를 올림" 같은 호스트 행동 |

### 7.3 여러 시계 맞추기

| 방법 | 정밀도 감각 | 쓰는 곳 |
|---|---|---|
| 같은 LA에 다 넣기 | 샘플 주기 (10 ns @ 100 MS/s) | 기본값. 가능하면 항상 |
| MARK GPIO | LA 샘플 주기 + MARK와 실제 행동 사이 지연 | 호스트 행동 정렬 |
| PPS (GPS 1 Hz 펄스) 를 모든 장비에 | 장비 입력 분해능 | 장비 여러 대, 장시간 |
| 펌웨어 타임스탬프 (DWT cycle counter 등) + GPIO 동기 펄스 | MCU 클럭 | FC 내부 구간 분해 (RX 태스크 → PID → 출력) [확인 필요: 코어별] |
| NTP/PTP 로 호스트들 맞추기 | ms (NTP) ~ µs (PTP, 하드웨어 지원 시) [확인 필요] | 로그 정렬, 판정용 아님 |

### 7.4 지연은 한 번이 아니라 분포로

- 스텝 입력을 수백~수천 번 반복 → 평균 · 표준편차 · p99 · 최대
- 판정은 "p99 < X ms, 최대 < Y ms" 같은 **스펙 기반**으로 ([R07](2026-10-24_R07_test_methodology_pyramid.md))
- 히스토그램 모양이 바뀌면(두 봉우리 등) 회귀 신호. 평균만 보면 놓친다

---

## 8. RF conducted 셋업 — 선으로만, 계산하고, 보호하고

### 8.1 왜 conducted인가

- 실내에서 안테나로 쏘면 반사 · 멀티패스 · 옆 벤치 간섭으로 **반복성**이 없다
- 공중으로 나가는 전파는 법 규제 대상이다. 특히 **재밍(jamming)은 미국에서 연방법 위반** (FCC가 재머의 판매 · 사용을 금지) — 테스트 목적이어도 개인 · 일반 회사가 공중에 재밍 신호를 내면 안 된다. 방산 회사가 허가된 시험장에서 하는 건 별개 [추정]
- 그래서 벤치는 **TX 안테나 포트 → 케이블 → 감쇠 → RX 안테나 포트**로만 연결한다. "간섭"도 결합기로 선 안에서만 섞는다

### 8.2 링크 버짓 계산 — 감쇠기 숫자를 "거리"로 착각하지 말 것

자유공간 경로 손실 (FSPL, dB) = 20·log10(d_km) + 20·log10(f_MHz) + 32.44

| 거리 | 915 MHz | 2440 MHz |
|---|---|---|
| 1 km | 91.7 dB | 100.2 dB |
| 5 km | 105.6 dB | 114.2 dB |
| 10 km | 111.7 dB | 120.2 dB |
| 20 km | 117.7 dB | 126.2 dB |

ELRS 수신 감도 한계 (ELRS Signal Health 페이지 일부):

| 대역 · 레이트 | 감도 한계 |
|---|---|
| 2.4 GHz 50 Hz | -115 dBm |
| 2.4 GHz 250 Hz | -108 dBm |
| 2.4 GHz 500 Hz | -105 dBm |
| 2.4 GHz F1000 (FLRC) | -104 dBm |
| 900 MHz 25 Hz | -123 dBm |
| 900 MHz 200 Hz | -112 dBm |

예: TX 출력 +20 dBm(100 mW), 2.4 GHz 500 Hz(감도 -105 dBm), 안테나 이득 무시 → 끊길 때까지 허용 경로 손실 ≈ **125 dB**. 즉 "링크가 끊기는 지점"을 벤치에서 만들려면 케이블 + 고정 pad + 프로그래머블 감쇠기 합이 125 dB를 넘어야 한다.

```text
  TX(+20 dBm) ─[케이블 ~1 dB]─[pad 30 dB]─[step 0..90 dB]─[pad 10 dB]─[케이블 ~1 dB]─ RX
  RX 입력 = 20 - 1 - 30 - (0..90) - 10 - 1 = -22 dBm .. -112 dBm
  → step 0 dB: 강한 신호 (RX 과입력 아님?) / step ~83 dB: 감도 한계 근처 / step 90 dB: 끊김
```

- 예제 코드의 `FakeRcInjector`는 이 계산을 그대로 쓴다: `rx_power = tx_dbm - fixed_pad - step_db`, 감도보다 낮으면 프레임이 FC에 도착하지 않는다 (TX +10 dBm, pad 40 dB, 감도 -108 dBm → 0 dB에서 -30 dBm, 90 dB에서 -120 dBm)
- **고정 pad는 TX 출력 바로 뒤에**: 감쇠기를 0 dB로 둔 순간에도 RX 최대 입력, 감쇠기 최대 입력을 넘지 않게 [확인 필요: RX 칩 · 감쇠기 최대 입력 데이터시트]
- 감쇠기 **전원 켤 때 값을 최대 감쇠로** (`STARTUPATT`) → 벤치 재부팅 때 순간 과입력 방지
- **누설(leakage)**: 보드 · 케이블 · 커넥터에서 전파가 새어 나와 공중으로 RX에 닿으면, 감쇠기를 90 dB로 올려도 링크가 안 끊긴다. 그래서 shield box가 필요하다. "감쇠기 최대인데 LQ가 안 떨어진다" = 누설 의심 1순위

> ⚠️ Gemini 정정: Gemini(p.9, 10, 21~22)는 "감쇠기 0~95 dB", "0 dB에서 90 dB까지 올리며 드론이 20 km 멀어졌을 때", "95 dB까지 올리면 20 km 떨어진 상황을 완벽히 모사"라고 했다. 세 가지가 틀리다. (1) 감쇠 범위는 **모델마다** 다르다 — 확인한 RCDAT-6000-90은 0~90 dB, 0.25 dB step. (2) 2.4 GHz에서 20 km 자유공간 손실만 약 126 dB다. 벤치의 총 손실(케이블 + 고정 pad + step)과 TX 출력 · 안테나 이득을 같이 계산해야 "몇 km 상당"을 말할 수 있고, step 감쇠기 숫자 하나는 거리가 아니다. (3) 감쇠기는 **평균 경로 손실만** 흉내 낸다. 페이딩 · 멀티패스 · 도플러 · 실제 재밍 파형은 없다(채널 에뮬레이터나 감쇠 스윕/hopping 기능으로 일부만 근사). 그래서 "완벽히 모사"가 아니라 "경로 손실과 링크 끊김/복구를 반복 가능하게 재현"이 정확한 표현이다.

### 8.3 RF 벤치로 할 수 있는 테스트

| 테스트 | 방법 | 판정 |
|---|---|---|
| 감도 곡선 | step 감쇠를 0.25~1 dB씩 올리며 LQ · RSSI · 프레임 유실 기록 | 레이트별 곡선이 기준선 대비 회귀 없음 |
| 링크 끊김 → failsafe | 감쇠를 최대로 한 번에 | §9 · 예제 코드 |
| 링크 복구 | 끊김 후 감쇠 복귀 | 복구 지연, 재무장 차단(Betaflight `failsafe_recovery_delay`) |
| 깜빡임 (flapping) | 감도 한계 근처에서 감쇠를 흔들기 | 상태 머신이 진동하지 않음, 히스테리시스 |
| 페이딩 근사 | 감쇠 sweep/hopping 프로파일 | LQ 통계 [확인 필요: 장비 프로파일 기능] |
| 텔레메트리 역방향 | RX → TX 방향 LQ · 텔레메트리 레이트 | 다운링크 유지 |
| 간섭 (선 안에서만) | 결합기로 CW · 다른 링크 신호 합성 | 호핑 회피 동작 [추정 · 법규 확인 필요] |

---

## 9. 예제 — "RC 링크 끊김 → failsafe → 모터 0 이 X ms 안에"

### 9.1 스펙부터 정한다 (Betaflight 문서 확인 값)

| 단계 | Betaflight 동작 | 기본값 |
|---|---|---|
| 신호 끊김 판정 | 유효 데이터가 100 ms 넘게 없으면 signal loss | 100 ms |
| Stage 1 (guard) | 대체 스틱 값(센터, 스로틀 0) 적용, **아직 armed** → 모터는 idle로 돈다 | `failsafe_delay` = 15 (0.1 s 단위, 4.5에서 1.5 s, 4.4에서 1.0 s), **마지막 정상 패킷부터** 카운트, 최소 200 ms |
| Stage 2 | 기본 **Drop**: 즉시 disarm, 모터 정지. 또는 Landing / GPS Rescue | 부팅 후 5 s 이전엔 Stage 2 진입 안 함 |
| 복구 | 신호가 일정 시간 좋아야 제어 복귀, 그동안 재무장 불가 | `failsafe_recovery_delay` 4.5에서 500 ms |

그래서 Betaflight 4.5 기본 설정 + Drop이라면 "마지막 정상 프레임부터 약 1.5 s 후 모터 0"이 기대값이다. 스펙 마진(예 +50 ms)은 팀이 정한다. Neros 포크의 실제 값은 모른다 [추정].

> ⚠️ Gemini 정정: Gemini(p.22)는 "RC Loss 때 정확히 0.5초 뒤에 모터 제어 신호가 0으로 떨어지는지"를 예로 들었다. Betaflight 기본값과 맞지 않는다. 4.5 기본 Stage 1 guard는 1.5 s이고, 그동안 기체는 **armed 상태로 idle 스로틀**이다. 즉 "링크 끊김 직후 모터 0"을 기대하면 정상 펌웨어를 FAIL로 판정한다. 기대값은 반드시 해당 펌웨어 설정(`failsafe_delay`, `failsafe_procedure`)에서 가져와야 한다.

### 9.2 테스트 흐름

```text
 smoke test ─► bring_up(전류제한→전압→ON→flash→reset→build id 확인→idle 전류)
     │
     ▼
 RC 링크 ON(감쇠 0 dB) ─► arm(스로틀 저) ─► "ARMED" (RTT로 확인) ─► 스로틀 1300 µs
     │
     ▼
 LA 시작 ─► MARK=1 ─► 감쇠기 90 dB (링크 끊김) ─► 기다림 (deadline + 500 ms)
     │
     ▼
 LA 정지 ─► 모터 4개 모두 0이 된 첫 시각 찾기 ─► (그 시각 - 마지막 정상 프레임) ≤ deadline ? PASS : FAIL
     │
     └─► 정리: 감쇠 0, RC OFF (다음 테스트를 위해)
```

### 9.3 코드 구조 ([`../code/bench_drivers.py`](../code/bench_drivers.py))

| 부분 | 내용 |
|---|---|
| `Clock` / `FakeClock` | 주입 가능한 시계. `sleep_ms`가 1 ms씩 진행하며 가짜 장비 · 가짜 DUT의 tick을 부른다. 실제 `time.sleep` 없음 → 결정적, 빠름 |
| `wait_until` | 타임아웃 있는 이벤트 대기. 고정 sleep 금지 원칙 |
| 인터페이스 | `PowerSupply`, `PowerRelay`, `Attenuator`, `Debugger`(flash · reset · build id · RTT 양방향), `SerialPort`, `LogicAnalyzer`, `RcInjector` |
| `SimFlightController` | Betaflight 문서 수치를 흉내 낸 작은 상태 머신 (100 ms 판정, Stage 1 idle, Stage 2 Drop). 버그 주입 옵션 `stage2_drops_motors=False` |
| Fake 장비 | `FakeRcInjector`는 링크 버짓으로 프레임 전달 여부 결정, `FakeLogicAnalyzer`는 공통 시계로 엣지 기록 + `mark()` |
| `BENCH_CONFIG` | §13 YAML 인벤토리와 같은 모양의 dict |
| `bench_smoke_test` | 모든 장비 `identify`, UART 루프백, 감쇠기 되읽기 → 실패 시 `BenchError` |
| `bring_up` | 전류 제한 → 전압 → ON → flash 해시 확인 → reset → **CI가 준 build id**와 비교 → 실패 시 전원 사이클 1회 → 그래도 실패면 "bricked?" |
| `test_rc_link_loss_failsafe` | 위 흐름. 결과 `PASS` / `FAIL` / `INFRA_FAIL` |
| `_self_test` | 6개 시나리오 (아래) |

실행 결과 (2026-10-24, macOS, `python3 -I bench_drivers.py`):

```text
[ok ] good fw, healthy bench: verdict=PASS {"idle_current_a": 0.12, "rx_power_at_cut_dbm": -120.0, "frames_sent": 613, "frames_delivered": 101, "deadline_ms": 1550, "ms_from_cut": 1499.0, "ms_from_last_good_frame": 1500.0}
[ok ] buggy fw is caught: verdict=FAIL {"idle_current_a": 0.12, "rx_power_at_cut_dbm": -120.0, "frames_sent": 613, "frames_delivered": 101, "deadline_ms": 1550, "reason": "motors never reached 0"}
[ok ] dead psu -> INFRA_FAIL: verdict=INFRA_FAIL {"reason": "smoke: psu not responding: psu: no response (VISA timeout)"}
[ok ] corrupt image -> INFRA_FAIL (bricked?): verdict=INFRA_FAIL {"reason": "DUT did not report expected build id (bricked?)"}
[ok ] rf budget: 0dB->-30.0 dBm, 90dB->-120.0 dBm (sens -108)
[ok ] rtt down->up: 'STATE DISARMED'
SELF-TEST PASS
```

읽는 법:

- 정상 펌웨어: 마지막 정상 프레임부터 1500 ms에 모터 0 → deadline 1550 ms 안 → PASS. MARK 기준으로는 1499 ms (마지막 프레임이 끊기 1 ms 전에 도착했기 때문). **어느 시점부터 재느냐가 스펙의 일부**다
- 버그 펌웨어(Stage 2에서 모터를 안 끔): "motors never reached 0" FAIL — 테스트가 버그를 잡는다는 증거 (테스트의 테스트)
- PSU가 죽은 벤치: 펌웨어 테스트는 시작도 안 하고 INFRA_FAIL. 펌웨어 개발자에게 "네 코드가 깨졌다"는 거짓 알림을 안 보낸다
- 깨진 이미지: 전원 사이클 1회 복구 시도 후 "bricked?" INFRA_FAIL
- 실제 벤치로 옮길 때: Fake 클래스 대신 PyVISA · urllib(감쇠기) · pylink · pyserial · logic2-automation 구현을 꽂고, `dut.last_good_rx_us`(가짜만 아는 값) 대신 **LA ch1(RX 출력 CRSF)의 마지막 프레임 시각**을 쓴다

---

## 10. 무인 복구 — 복구 사다리

밤새 도는 벤치는 반드시 이상 상태에 빠진다. 사람 없이 **가벼운 것부터 무거운 것 순서로** 시도하고, 각 단계 결과를 로그에 남긴다.

| 단계 | 행동 | 무엇을 고치나 | 확인 방법 |
|---|---|---|---|
| 1 | 소프트 리셋 (CLI/MSP reboot 명령) | 펌웨어 논리 상태 | 부팅 배너 · build id |
| 2 | probe 하드 리셋 (NRST) | 멈춘 CPU, HardFault 루프 | SWD IDCODE 읽힘, 부팅 배너 |
| 3 | probe halt + 재플래시 + 리셋 | 깨진 flash 내용 | 해시 일치 + build id |
| 4 | DUT 전원 사이클 (PSU OUTP OFF/ON 또는 relay) | 주변장치 래치업, 센서 멈춤 | 전류 정상 범위, 부팅 |
| 5 | USB 포트 전원 사이클 (`uhubctl`) | 멈춘 USB-UART · probe 펌웨어 | 장치 재열거, `identify` 성공 |
| 6 | 부트로더 복구 (BOOT0 핀을 relay/GPIO로 → 시스템 부트로더/DFU로 플래시) | SWD가 안 붙는 보드 (잘못된 클럭/핀 설정 펌웨어) [확인 필요: 칩별] | DFU 장치 등장 → 플래시 → 정상 부팅 |
| 7 | 계측기 AC 전원 사이클 (네트워크 PDU) | 멈춘 PSU · 감쇠기 · 허브 | `*IDN?` 응답 |
| 8 | 벤치 offline 표시 + 사람 호출 | 물리 고장 | 티켓, 대시보드에서 빠짐 |

**bricked 판정 신호**:

| 신호 | 의미 |
|---|---|
| 전원 ON 직후 전류가 제한값에 붙음 | 쇼트 · 래치업 → 즉시 OFF, 재시도 금지, offline |
| 전류는 정상, SWD IDCODE 안 읽힘 | 클럭/전원 도메인 문제, 잘못된 option bytes [확인 필요] |
| SWD는 되는데 부팅 배너 없음 | 펌웨어 문제 (테스트 대상일 수 있음 → INFRA가 아니라 FAIL일 수도) |
| 부트로더 복구까지 실패 | 하드웨어 → 사람 |

- 핵심 판단: **"이건 벤치 고장인가, 펌웨어 버그인가?"** 새 펌웨어가 부팅을 못 하면 그건 **FAIL(버그)**이다. 같은 벤치에서 golden 펌웨어(§11)도 부팅 못 하면 그건 **INFRA_FAIL**이다. 복구 사다리 중간에 golden 이미지를 한 번 플래시해 보는 게 이 둘을 가른다
- 복구 횟수 · 단계를 벤치별로 집계 → "벤치 3번이 일주일에 5번 단계 5까지 감" = 그 벤치 USB 허브 교체 신호

---

## 11. Bench health · 교정 · golden unit

### 11.1 Bench smoke test (매 잡 시작 전, 수십 초 이내)

- [ ] 모든 계측기 `identify` (`*IDN?`, `:MN?`, probe 시리얼) — 인벤토리의 시리얼과 **일치**하는가 (다른 장비가 꽂혀 있지 않은가)
- [ ] PSU: 출력 OFF 상태 전압 0, 설정 되읽기, `SYST:ERR?` 비어 있음
- [ ] 감쇠기: 임의 값 설정 → 되읽기 → 기본값 복귀
- [ ] UART: 루프백 지그 또는 DUT 에코 응답
- [ ] probe: 타깃 전압(VTref) 감지, IDCODE 읽기
- [ ] LA: 짧은 캡처로 MARK 토글이 보이는가 (LA ↔ GPIO 경로 확인)
- [ ] 인터락: 케이지 문 · shield box 뚜껑 닫힘
- [ ] DUT idle 전류가 범위 안 (golden 기준 ± 허용)
- [ ] 디스크 여유 (캡처 파일), 호스트 시간 동기
- [ ] 결과: 하나라도 실패 → 잡 결과 `INFRA_FAIL`, 벤치 상태 `degraded`, 테스트 진행 안 함

### 11.2 교정 (calibration)

| 대상 | 무엇을 | 주기 감각 |
|---|---|---|
| PSU 전압/전류 측정 | 외부 DMM과 비교 | 제조사 권장 · 연 1회 [확인 필요] |
| 전류 프로브 | 영점 · 이득 | 사용 전 영점, 정기 교정 |
| RF 경로 손실 | 케이블 + pad + 감쇠기 0 dB 삽입 손실을 **주파수별로** 측정해 표로 저장 | 케이블 교체 · 재조립 때마다 |
| 감쇠기 step 정확도 | 몇 개 step에서 실제 감쇠 확인 | 정기 [확인 필요] |
| thrust stand 로드셀 | 기준 무게로 | 이동 · 재조립 후 |
| LA 임계 · 시간 | 알려진 주기 신호(함수발생기 · MCU 클럭 출력) 측정 | 가끔 |

- 교정 값과 날짜를 **인벤토리(§13)에** 기록하고, 측정 결과에 장비 시리얼 · 교정일을 같이 남긴다 → 6개월 뒤 "그때 수치가 왜 달랐지?"에 답할 수 있다

### 11.3 Golden unit · golden image

- **golden DUT**: 알려진 정상 보드 1~2개. 벤치를 새로 만들거나 고친 뒤 golden DUT + golden image로 전체 스위트가 통과하는지 확인 → "벤치 자체의 합격 시험"
- **golden image**: 릴리즈된 안정 펌웨어. 복구 사다리에서 "벤치 문제냐 새 펌웨어 문제냐"를 가르는 기준
- golden DUT도 마모된다 (커넥터, flash 쓰기 횟수). 사용 횟수를 세고 교체 주기를 둔다

---

## 12. 1대에서 N대 랙으로

### 12.1 USB 토폴로지 한계

| 한계 | 내용 | 대책 |
|---|---|---|
| 장치 수 | USB 버스(호스트 컨트롤러)당 장치 주소 127개 [USB 규격, 허브 포함] | 컨트롤러 여러 개 (PCIe USB 카드) |
| 허브 깊이 | 허브 계층 제한 (규격상 최대 tier) [확인 필요: 정확한 tier 수] | 허브를 깊게 쌓지 않기 |
| 대역폭 공유 | 같은 허브/컨트롤러 아래 장치들이 대역폭을 나눔. 고속 LA 캡처가 다른 장치를 굶김 | LA는 전용 컨트롤러/포트 |
| 재열거 폭풍 | 허브 하나 리셋 → 아래 장치 전부 재열거 | 벤치별로 허브 분리 |
| 전력 | bus-powered 허브는 전류 부족 → 간헐 끊김 | self-powered 허브 |
| 그라운드 루프 | USB 그라운드로 DUT · 계측기 · 호스트가 이어져 노이즈 · 오측정 | USB 절연기 (단, 속도 제한 확인) |

### 12.2 안정된 장치 이름 — udev

```text
# /etc/udev/rules.d/99-hil.rules  (예시, 시리얼 값은 실제 장비로)
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{serial}=="FT6ABC12", SYMLINK+="hil/fc-01-crsf"
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{serial}=="FT6ABC34", SYMLINK+="hil/fc-01-msp"
# FC 자체 USB VCP는 FC 시리얼(=MCU UID 기반일 수 있음)로 [확인 필요: 펌웨어별]
SUBSYSTEM=="tty", ATTRS{serial}=="0x8D3A51234567", SYMLINK+="hil/fc-01-vcp"
```

- 같은 모델 어댑터가 여러 개면 **USB 시리얼 번호**로 구분. 시리얼 번호가 없는(혹은 다 같은) 싸구려 어댑터는 랙에 쓰지 않는다. 불가피하면 물리 포트 경로(`ID_PATH`)로 묶는다 — 대신 포트를 옮기면 깨진다
- `udevadm info -a -n /dev/ttyUSB0`로 속성 확인 후 규칙 작성, `udevadm control --reload && udevadm trigger`
- 테스트 코드는 `/dev/ttyUSB3` 같은 이름을 **절대** 쓰지 않는다. 인벤토리의 논리 이름만

### 12.3 네트워크 계측기를 선호

- LAN(LXI/SCPI, HTTP) 장비는 USB 재열거 · 대역폭 · 그라운드 문제가 없고, 호스트를 바꿔도 IP만 알면 된다
- 벤치 장비 전용 네트워크(VLAN) — thrust stand API처럼 "네트워크의 누구나 스로틀"인 장비가 있으므로 사무실 네트워크와 분리
- 고정 IP/DHCP 예약 + 인벤토리에 MAC 기록

### 12.4 벤치당 호스트 vs 공유 호스트

| | 벤치당 호스트 (NUC · 미니 PC · SBC) | 공유 호스트 (서버 1대가 N 벤치) |
|---|---|---|
| 장애 격리 | 좋음 (한 호스트 문제 = 한 벤치) | 나쁨 (USB 컨트롤러 하나 문제 = 여러 벤치) |
| USB 한계 | 벤치별로 여유 | 금방 도달 |
| 관리 | 호스트 N대 (이미지 · 업데이트 자동화 필요) | 1대 |
| CI runner | 벤치당 runner 1개, tag = 벤치 능력 | runner 1개 + 벤치 lock |
| 추천 [추정] | USB 장비 많은 FC 벤치 | LAN 장비 위주 벤치 |

### 12.5 labgrid 모델 (개념 차용)

labgrid(오픈소스 임베디드 보드 제어 · 테스트 라이브러리, 문서 확인)의 용어는 벤치 설계를 설명하기 좋다:

| labgrid 용어 | 뜻 | 이 단원에 대응 |
|---|---|---|
| Target | 제어할 임베디드 시스템 하나 | DUT + 그 벤치 |
| Resource | 접근 정보를 담은 수동 객체 (예 `RawSerialPort`, `NetworkPowerPort`). USB처럼 나타났다 사라지는 건 ManagedResource | 인벤토리 항목 (포트, IP, 시리얼) |
| Driver | Resource를 써서 동작을 하는 객체 (예 `NetworkPowerDriver`의 on/off/cycle/get) | `PowerSupply`, `Debugger` 구현체 |
| Strategy | Target을 상태 사이로 옮기는 특수 Driver (off → bootloader → shell) | `bring_up` + 복구 사다리 |
| Place | coordinator가 관리하는 자원 묶음. 쓰기 전에 acquire, 끝나면 release | 벤치 1대 = place 1개, 독점 사용 |
| Coordinator | 자원 · place · 상호 배제를 관리하는 gRPC 서버 | R09 스케줄러의 벤치 lock |
| Exporter | 자기 호스트에 붙은 자원을 coordinator에 등록 | 벤치당 호스트 |

```yaml
# labgrid 환경 파일 예 (labgrid getting started 문서)
targets:
  main:
    resources:
      RawSerialPort:
        port: "/dev/ttyUSB0"
    drivers:
      ManualPowerDriver:
        name: "example"
      SerialDriver: {}
```

- labgrid는 Linux 보드(U-Boot · 셸) 쪽에 강하다 → 토폴로지 D(companion) 벤치에 바로 쓸 만하다. MCU FC 벤치는 같은 개념으로 자체 드라이버를 짜거나 labgrid 드라이버를 확장 [추정]
- pytest와 붙이는 방법: `pytest --lg-env local.yaml ...` (labgrid 문서). 자세한 플랫폼 층은 [R09](2026-10-24_R09_scalable_reusable_test_platform.md)

### 12.6 랙 물리 설계 체크

- [ ] 벤치마다 같은 배치 · 같은 케이블 색 · 같은 라벨 규칙 (사진을 위키에)
- [ ] 벤치 하나를 빼서 고치는 동안 나머지는 계속 돈다 (공유 전원 · 공유 허브 최소화)
- [ ] 랙 전원: 계측기 AC는 네트워크 PDU, DUT DC는 벤치별 PSU + 퓨즈
- [ ] 발열 · 환기 (PSU · 호스트 · shield box 내부)
- [ ] RF 벤치 간 간격 · shield box로 상호 간섭 차단, 같은 대역 벤치 동시 실행 시 LQ 비교 실험 1회

---

## 13. 벤치 설정은 데이터로 — YAML 인벤토리

테스트 코드는 "PSU"라는 **논리 이름**만 안다. 어느 장비가 어느 IP/포트/시리얼인지는 설정 파일이 안다. 벤치를 늘릴 때 코드는 그대로, 파일만 늘어난다.

```yaml
# benches/hil-fc-01.yaml  (예시 — 예제 코드의 BENCH_CONFIG 와 같은 모양)
bench_id: hil-fc-01
location: rack-A / shelf-2
topology: B_rf_conducted
capabilities: [swd, crsf_inject, msp, rf_atten, la_8ch, dshot_capture]   # CI runner tag 로도 사용
dut:
  board: fc-rev-b
  serial: FC-000123
  mcu: STM32F405          # [예시]
  golden: false
instruments:
  psu:
    driver: visa_scpi
    resource: "TCPIP0::10.0.0.21::5025::SOCKET"
    serial: "PSU-SN-XXXX"
    volts: 16.8
    current_limit_a: 1.0
    cal_due: 2027-03-01
  relay:
    driver: usb_relay
    serial: "RLY-SN-XXXX"
    channel: 1
  atten:
    driver: minicircuits_http
    url: "http://10.0.0.22/"
    serial: "ATT-SN-XXXX"
    max_db: 90
    startup_db: 90
    path_loss_db: {915: 41.2, 2440: 42.8}   # 교정 표 (pad + 케이블, 0 dB step 기준) [예시 값]
  probe:
    driver: pylink
    serial: "000801234567"
    target: STM32F405RG
  uart_crsf:
    driver: pyserial
    dev: /dev/hil/fc-01-crsf
    baud: 420000
  uart_msp:
    driver: pyserial
    dev: /dev/hil/fc-01-msp
    baud: 115200
  la:
    driver: saleae_logic2
    serial: "LA-SN-XXXX"
    channels: {TX_CRSF: 0, RX_CRSF: 1, M1: 2, M2: 3, M3: 4, M4: 5, LOOP: 6, MARK: 7}
  gpio:
    driver: pyftdi
    url: "ftdi://ftdi:232h:FTXXXXXX/1"
    pins: {BOOT0: 0, MARK: 1, INTERLOCK_IN: 2}
limits:
  idle_current_a: [0.05, 0.30]
  boot_timeout_ms: 2000
maintenance:
  last_golden_pass: 2026-10-20
  notes: "SMA 케이블 #3 교체 2026-10-18"
```

- `capabilities` = 이 벤치가 어떤 테스트를 돌릴 수 있나. 테스트는 필요한 능력을 선언하고(`@requires("rf_atten")`), 스케줄러가 맞는 벤치를 고른다 (R09)
- `driver` 필드로 실제/가짜 구현을 바꾼다 → 같은 테스트가 Fake 벤치(개발자 노트북), SITL, 실제 벤치에서 돈다
- 시리얼 번호는 smoke test에서 **실제 장비가 보고한 값과 비교** → "누가 케이블을 바꿔 꽂았다"를 잡는다
- 교정 값(`path_loss_db`)이 설정에 있으므로 테스트는 "RX 입력 -100 dBm으로 맞춰"처럼 **물리량으로** 요청할 수 있다

---

## 14. 안전

> 프롭을 단 모터 회전, LiPo 충전 · 방전은 손 · 눈 부상과 화재 위험이 있다. 무인 벤치는 이 위험이 사람 없이 일어날 수 있다는 뜻이므로 설계 단계에서 막는다.

| 위험 | 대책 | 벤치 코드에서 |
|---|---|---|
| 프롭 회전 | **기본은 프롭 없음**. 추력이 필요하면 thrust stand + 가드 + 케이지. 무인 실행은 프롭 없는 벤치만 | 프롭 장착 벤치는 `capabilities`에 표시, 무인 스케줄 제외 |
| 쇼트 · 과전류 | PSU 전류 제한을 ON 전에. 인라인 퓨즈. 첫 전원은 smoke stopper/낮은 전류 제한 | `bring_up` 순서 고정 |
| 고전류 경로 | 굵은 선, 확실한 커넥터(XT60 등), 접점 발열 확인 | 전류 상한 모니터 → 넘으면 OFF |
| 비상 정지 | 손 닿는 곳에 e-stop. ESC 쪽 DC를 **물리적으로** 끊는 접촉기 (소프트웨어 OFF만 믿지 않음) | e-stop 상태를 GPIO로 읽어 테스트 시작 거부 |
| interlock | 케이지 문 · shield box 뚜껑 스위치 → relay 체인 | 열려 있으면 smoke test 실패 |
| LiPo | 무인 벤치엔 쓰지 않는다(PSU 사용). 쓸 땐 사람 상주, 내화 백/박스, 전용 밸런스 충전기, 손상 · 부풀음 배터리 즉시 격리, 보관은 storage 전압 [확인 필요: 셀당 값은 제조사 기준] | 배터리 벤치는 무인 스케줄 금지 |
| 정전기 (ESD) | 손목 스트랩, ESD 매트 | — |
| RF | 공중 방사 금지, 고출력 TX는 항상 부하(감쇠/더미 로드) 연결 후 켜기. 안테나 포트 개방 상태 송신은 PA 손상 가능 [확인 필요: 모듈별] | 감쇠기 startup = 최대, TX 켜기 전에 경로 확인 |
| 소프트웨어 기본값 | 펌웨어가 부팅 직후 모터 출력 0인지 ([Q01](../../onsiteKit/qa/2026-10-20_Q01_fw_side_of_subsystem_integration.md) §4) | 모든 테스트 끝에 disarm · RC OFF · 감쇠 복귀 · 출력 OFF (teardown) |

- teardown은 **테스트가 실패하거나 예외가 나도** 반드시 실행 (pytest fixture의 yield 뒤, `try/finally`)
- "안전 상태"를 하나의 함수로: `bench.safe_state()` = 모터 명령 0 → disarm → RC OFF → 감쇠 최대 → PSU OFF. 복구 사다리 · 비상 시 모두 이걸 먼저 부른다

---

## 15. 케이블 · 커넥터 관리

벤치 flaky의 큰 비중이 여기서 나온다. 지루하지만 규칙으로 만든다.

| 항목 | 규칙 |
|---|---|
| 라벨 | 케이블 **양 끝**에 같은 ID (`B01-UART-CRSF`). 인벤토리에 ID 기록 |
| 색 | 전원 빨강/검정, 신호선 규칙, RF 케이블은 길이별 색 등 팀 규칙 |
| 커넥터 마모 | FC의 작은 커넥터(JST-SH/GH 류)는 삽입 횟수에 약함 → 벤치 쪽에 **중간 하네스**를 두고 그쪽을 교체. 양산 테스트면 pogo pin 지그 |
| SMA | 토크 렌치로 일정하게 (손으로 대충 = 손실 · 반복성 변화) [확인 필요: 규격 토크 값]. 체결 횟수 수명 있음 → 자주 빼는 곳엔 커넥터 세이버 |
| RF 케이블 | 구부림 반경 지키기, 고정. 움직이면 손실이 바뀐다 → 교정 후 건드리지 않기 |
| 스트레인 릴리프 | 모든 케이블을 고정점에 묶기. 진동(모터 벤치)에서 특히 |
| 그라운드 | 신호마다 그라운드 리턴, LA · 스코프 그라운드는 DUT 근처. 그라운드 루프는 USB 절연기로 |
| 길이 | 고속 신호(SWD, DShot)는 짧게. SWD가 길면 속도를 낮춰야 붙는다 |
| 교체 기록 | 무엇을 언제 바꿨는지 인벤토리 `maintenance`에. 결과 그래프의 변화 시점과 맞춰 본다 |

---

## 16. SSD 검증에서는 이랬다 → 드론에서는 이렇다

Don의 경험 범위(챔버 테스트 플랫폼 SDK, UART 시퀀스 자동화, eSSD 상태 모니터링, chip reliability system, factory test-node, DSO · protocol analyzer)만으로 비교한다. 수치는 쓰지 않는다.

| 항목 | SSD (챔버 랙) | 드론 (HIL 벤치) |
|---|---|---|
| DUT | 같은 폼팩터 SSD 여러 대, 슬롯에 꽂음 | FC · RX · ESC · companion 등 **서로 다른 보드**, 선으로 연결 |
| 주 자극 | 온도 프로파일(챔버), 호스트 I/O 워크로드, 전원 | RC 입력, RF 경로 손실, 전압, 센서/GPS 재생, (모터 부하) |
| 호스트 인터페이스 | PCIe/NVMe (표준, 풍부한 상태 정보) + UART 디버그 | UART(CRSF · MSP · MAVLink), SWD/RTT, USB VCP — 제각각 |
| 시간 요구 | 초~시간 단위 (온도 램프, 내구성) | **ms~µs 단위** (failsafe 지연, 루프 지터, DShot) → LA 공통 timebase 필수 |
| 관측 | NVMe 로그/텔레메트리, SMART, 디버그 UART | telemetry, RTT, blackbox, **LA 파형** |
| 전원 | 슬롯 전원 제어, 전원 손실 시나리오 | PSU · relay · 고전류 경로, 회생 전류, 배터리 특성 |
| RF | 없음 | **핵심 자극**. 감쇠기 · shield box · 법규 |
| 안전 | 열 · 전기 | + **회전체(프롭)**, LiPo 화재 |
| 스케일 | 챔버 하나에 다수 DUT → 슬롯 스케줄링 | 벤치 하나에 DUT 1개(보통) → **벤치 N대** + 능력 태그 |
| 상태 모니터링 | eSSD 상태 모니터링 (Don SDK) | 벤치 health + DUT health 둘 다 (smoke test, 복구 사다리) |
| 같은 점 | 장비 제어 API를 SDK로 감싸 **다른 엔지니어가 쓰게** 함, 시나리오 스케줄링, UART 시퀀스 자동화 | 정확히 같은 구조: 계측기 드라이버 → 벤치 라이브러리 → 테스트 → 스케줄러 (R09) |

면접에서 말할 수 있는 연결 (경험 범위 안):

> "At SK hynix I built a chamber test-platform SDK — temperature control, scenario scheduling, drive health monitoring, UART-driven test sequences — that other engineers used to run their own tests. A drone HIL bench is the same shape with a different stimulus set: instead of temperature and NVMe workloads, it's RC frames, RF path loss, supply voltage and motor load, and the timing requirements go from minutes to milliseconds, so a shared logic-analyzer timebase becomes the center of the bench."

> "In my recent RF chipset integration work I used scopes and protocol analyzers to root-cause bus issues, which is exactly what you need when a HIL test fails and you have to decide whether it's the bench or the firmware."

---

## 17. 테스트 아이디어 · 체크리스트

### 17.1 벤치를 새로 지을 때

- [ ] 8가지 능력(§3) 각각에 장비와 "완료 조건"이 있다
- [ ] 모든 장비가 원격 제어 + 되읽기 + 고유 시리얼
- [ ] 인벤토리 YAML 작성, 시리얼을 smoke test가 대조
- [ ] udev 규칙으로 안정된 이름, 테스트 코드에 `/dev/ttyUSBn` 없음
- [ ] LA 채널 배정표(§7.2) 작성, MARK GPIO 연결
- [ ] RF 경로 링크 버짓 계산(§8.2), 고정 pad 위치 확인, 감쇠기 startup 최대
- [ ] 누설 시험: 감쇠기 최대에서 링크가 실제로 끊기는가
- [ ] 안전: 전류 제한 순서, 퓨즈, e-stop, interlock, teardown, `safe_state()`
- [ ] 복구 사다리 단계별로 일부러 고장 내 보고 자동 복구 확인 (USB 뽑기, 펌웨어 깨기, PSU 끄기)
- [ ] golden DUT + golden image로 전체 스위트 통과 = 벤치 합격

### 17.2 벤치에서 돌릴 테스트 아이디어

| 영역 | 테스트 | 필요한 토폴로지 |
|---|---|---|
| 부팅 | 콜드 부팅 시간 분포, 부팅 직후 모터 출력 0 | A |
| 플래시 | 업데이트 중 전원 차단 → 복구, 설정 보존 | A |
| 전원 | 저전압 경고 임계, 브라운아웃 리셋 후 정상 복귀, 부팅 · idle 전류 회귀 | A |
| RC 파서 | 깨진 CRSF 프레임 · 가짜 sync 바이트 · 길이 오류 주입 → 파서 생존 · resync (R05) | A |
| failsafe | 링크 끊김 → Stage 1 → Stage 2 시간, 복구 지연, 재무장 차단 | A (UART 끊기) / B (RF) |
| 링크 품질 | 감도 곡선, flapping, 텔레메트리 역방향 | B |
| 지연 | 스틱 스텝 → DShot 변화 분포 (무선 구간 / FC 구간 분리) | B |
| 스케줄러 | 루프 주기 · 지터 (디버그 GPIO), CPU load 회귀 | A |
| 모터 | DShot 프레임 레이트, 믹서 방향, bidirectional RPM 값 vs 실제 | A / C |
| 전력 · EMI | 스로틀 펀치 시 레일 처짐 · FC 리셋 여부, 모터 회전 중 IMU 노이즈 · LQ | C |
| 영상 | glass-to-glass 분포, 링크 손실 시 영상 복구 | D |
| 벤치 자체 | smoke test, 복구 사다리, 벤치 간 결과 차이 | 전부 |

---

## 18. 직접 해 볼 것

### 18.1 하드웨어 없이 (오늘)

1. `python3 -I code/bench_drivers.py` 실행 → 6개 시나리오 PASS 확인
2. `FirmwareImage(failsafe_delay_ds=10)`(4.4 기본 1.0 s)로 바꾸고 deadline을 맞춰 다시 돌려 보기 → "스펙은 설정에서 온다" 체감
3. `FakeRcInjector`의 `tx_dbm`을 20으로, `fixed_pad_db`를 20으로 바꾸면 90 dB step으로 링크가 끊기는지 계산 → 안 끊기면 pad를 얼마나 늘려야 하나 (§8.2)
4. 새 Fake 장비 추가 연습: `FakeUsbHub`(포트 전원 사이클)를 만들고 복구 사다리 5단계를 `bring_up`에 넣기
5. `bench_smoke_test`에 "인벤토리 시리얼 vs `identify()` 결과 비교"를 추가

### 18.2 최소 홈 벤치 BOM — **사기 전에 Don과 예산 확인**

| 품목 | 용도 | 가격대 | 메모 |
|---|---|---|---|
| Betaflight 지원 FC (F4/F7급) | DUT | `$` [확인 필요] | 프롭 · 모터 없이 |
| ELRS RX + TX 모듈 (또는 RX만) | RC 경로 | `$`~`$$` [확인 필요] | 처음엔 USB-UART로 CRSF 주입만 해도 됨 |
| SWD probe (ST-Link 또는 J-Link EDU 류) | 플래시 · RTT | `$` [확인 필요: EDU는 비상업 라이선스] | FC에 SWD 패드 노출 여부 확인 |
| USB-UART 2개 (FTDI 계열, 시리얼 번호 있는 것) | CRSF 주입, MSP | `$` | 420000 baud 확인 |
| FT232H 브레이크아웃 | GPIO 마커, BOOT0 | `$` | pyftdi |
| 저가 LA (FX2 계열 24 MHz) 또는 Saleae Logic 8 | 공통 timebase | `$` / `$$` (Logic 8: 499 USD) | sigrok-cli 또는 logic2-automation |
| 원격 제어 되는 벤치 PSU (USB/LAN) | 전원 · 전류 측정 | `$$` [확인 필요] | 일부 저가 모델은 SCPI가 아닌 독자 명령 |
| 포트 전원 USB 허브 (PPPS) | USB 전원 사이클 | `$`~`$$` | uhubctl 호환 목록 확인 |
| 퓨즈 홀더 · XT60 피그테일 · ESD 매트 | 안전 | `$` | — |
| (나중에) SMA 고정 pad 몇 개 + 케이블 | RF conducted 입문 | `$`~`$$` | 프로그래머블 감쇠기(`$$`~`$$$`)는 필요성 확인 후 |

- 1단계는 **토폴로지 A**만: FC + probe + USB-UART + LA + PSU. RC 링크 끊김은 "USB-UART 주입을 멈추는 것"으로 대신한다 → 예제 코드의 Fake를 실제 드라이버로 하나씩 교체
- 모터 · 프롭 · LiPo는 이 단계에서 사지 않는다. 필요해지면 안전 장비(케이지, e-stop, 퓨즈, 내화 백)와 함께 따로 예산 확인

### 18.3 읽어 볼 것

- pylink 문서의 RTT 메서드, SEGGER RTT KB 페이지 (up/down 채널, 모드)
- Saleae logic2-automation getting started
- Mini-Circuits 프로그래머블 감쇠기 프로그래밍 매뉴얼 (HTTP/Telnet 명령 절)
- labgrid overview (Resource / Driver / Strategy / Place)
- Betaflight Failsafe 위키 (Stage 1/2, 기본값)

---

## 19. 확인 안 된 것

| 항목 | 상태 |
|---|---|
| Neros의 실제 벤치 구성 · 계측기 · 대수 · 운영 방식 | [추정] — 공고에 "HIL 벤치 구축" 문구만 |
| Neros 무선 링크가 ELRS 기반인지, 사내 독자 링크인지, 감도 수치 | [추정] / [Gemini 주장 · 미확인] |
| Neros 포크의 failsafe 설정값 | 모름 — Betaflight 4.5 기본값으로 예시 |
| Betaflight 메인라인의 하드웨어 HIL 센서 주입 모드 | [확인 필요] |
| ICM-42688 SPI 클럭 기본값 (빌드 매크로로 변경 가능하다는 것만 확인) | [확인 필요] |
| RX 칩 · 감쇠기 최대 입력 전력 | [확인 필요: 데이터시트] |
| FTDI latency timer 기본값과 sysfs 경로 | [확인 필요] |
| 각 장비 가격(감쇠기 · Saleae 외) | [확인 필요] |
| Saleae Logic 2 헤드리스 실행 방법, Async Serial analyzer 설정 키 이름 | [확인 필요] |
| Tyto Flight Stand 샘플링 속도(Standard 100 Hz / Pro 1,000 Hz)는 검색 요약 기준 | [확인 필요: 데이터시트] |
| PSU가 ESC 회생 전류를 못 흡수해 생기는 전압 상승의 크기 | [확인 필요: 장비 조합별 측정] |
| USB 허브 tier 최대 수 | [확인 필요: USB 규격] |
| SMA 체결 토크 · 체결 수명 수치 | [확인 필요] |
| LiPo storage 전압 등 배터리 수치 | [확인 필요: 제조사 기준] |
| STM32 시스템 부트로더 · RDP 복구 절차 세부 | [확인 필요: 칩별 AN] |

---

## 20. 참고 링크

계측기 · 라이브러리 (이 단원에서 WebFetch로 확인):

- pylink (J-Link Python) API: https://pylink.readthedocs.io/en/latest/pylink.html
- SEGGER RTT: https://kb.segger.com/RTT
- pyOCD API 예제: https://pyocd.io/docs/api_examples.html · 명령 레퍼런스: https://pyocd.io/docs/command_reference.html
- probe-rs: https://probe.rs/docs/tools/probe-rs/
- OpenOCD Flash Programming: https://openocd.org/doc/html/Flash-Programming.html
- pyserial URL handlers: https://pyserial.readthedocs.io/en/latest/url_handlers.html
- PyVISA resource names: https://pyvisa.readthedocs.io/en/latest/introduction/names.html
- Saleae logic2-automation: https://saleae.github.io/logic2-automation/getting_started.html
- Saleae Logic Pro 16: https://www.saleae.com/products/saleae-logic-pro-16 · 비교: https://cdn.saleae.com/compare-logic
- sigrok-cli: https://sigrok.org/wiki/Sigrok-cli
- Mini-Circuits RCDAT-6000-90: https://www.minicircuits.com/WebStore/dashboard.html?model=RCDAT-6000-90
- Mini-Circuits 프로그래머블 감쇠기 매뉴얼: https://www.minicircuits.com/softwaredownload/Prog_Manual-6-Programmable_Attenuator.pdf
- uhubctl: https://github.com/mvp/uhubctl
- pyftdi URL scheme: https://eblot.github.io/pyftdi/urlscheme.html
- labgrid overview: https://labgrid.readthedocs.io/en/latest/overview.html · getting started: https://labgrid.readthedocs.io/en/latest/getting_started.html
- Tyto Robotics Flight Stand 15: https://www.tytorobotics.com/pages/flight-stand-15 · 소프트웨어 매뉴얼: https://tytorobotics.com/blogs/manuals-and-datasheets/flight-stand-software-user-manual

드론 펌웨어 · 링크:

- Betaflight Failsafe: https://betaflight.com/docs/wiki/guides/current/Failsafe
- Betaflight CLI (자이로 레이트): https://betaflight.com/docs/wiki/guides/current/Cli
- ExpressLRS Signal Health (감도 표): https://www.expresslrs.org/info/signal-health/
- PX4 HITL: https://docs.px4.io/main/en/simulation/hitl.html
- INAV SITL 문서: https://github.com/iNavFlight/inav/tree/master/docs/SITL

이 저장소 안:

- 예제 코드: [`../code/bench_drivers.py`](../code/bench_drivers.py)
- 이전 노트: [N02 HIL for drones](../../firmwareTestEngineerPrep/notes/2026-09-28_N02_hil_for_drones.md) · [Q01 서브시스템 통합 FW 쪽](../../onsiteKit/qa/2026-10-20_Q01_fw_side_of_subsystem_integration.md)
- 관련 단원: [R00](2026-10-24_R00_overview_and_gemini_corrections.md) · [R01](2026-10-24_R01_fte_vs_ssd_validation.md) · [R02](2026-10-24_R02_drone_system_and_open_source_map.md) · [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · [R06](2026-10-24_R06_companion_video_linux_rtos.md) · [R07](2026-10-24_R07_test_methodology_pyramid.md) · [R09](2026-10-24_R09_scalable_reusable_test_platform.md) · [R10](2026-10-24_R10_roadmap_labs_references.md)

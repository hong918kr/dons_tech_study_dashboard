# 🎮 S03 · Ground & Peripherals 펌웨어 — 무엇을 만들고, 어떻게 테스트하나

> 드론은 하늘에 있지만 운용자가 손에 쥐는 것은 **지상 장비**다: RC 핸드셋(스틱·버튼·지상 라디오), 헤드셋/고글(비디오 디스플레이), 그리고 GCS 노트북에 USB/시리얼로 붙는 지상 라디오 모듈. Neros에는 이 장비를 맡는 **Peripherals Team**(Tennessee 사무소)이 있고, STM32 + FPGA + embedded Linux로 디스플레이·핸드셋·지상 제어 시스템을 만든다 [확인됨 공고 5095368007]. 이 직군의 펌웨어 테스트는 "사람 손 → 펌웨어 → 무선 → 기체"와 "기체 → 무선 → 화면 → 사람 눈"을 **사람 없이** 재현하는 리그를 만드는 일이다. 이 노트는 그 테스트베드를 면접에서 설계할 수 있게 정리한다.

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | RC 핸드셋 FW(스틱 ADC·버튼·햅틱·화면), 지상 라디오 링크 FW(CRSF/ELRS 계열 송신), 헤드셋/고글 FW(비디오 수신·디스플레이·OSD), GCS와의 USB/시리얼 브리지, 배터리·충전 관리 [확인됨 공고 5095368007 "display systems, handsets and ground control systems", "headsets, handsets"] |
| 주로 고장 나는 곳 | 입력 경로(ADC 노이즈·데드밴드·캘리브레이션), 링크 지연·failsafe, 디스플레이 프레임 드롭·tearing, USB 열거 실패, 배터리 잔량 오판, 부팅/업데이트 중 brick, 낙하·온도에서 커넥터·버튼 |
| 대표 테스트베드 | **GPIO/DAC 주입 + 레퍼런스 air unit + 캡처 카드**로 만든 "손·눈 없는 핸드셋/고글 리그", 스틱 로봇(서보) 1대, USB 허브 전원 스위칭, 프로그래머블 PSU, 온도 챔버 |
| 합격 기준 예 | 스틱 → 기체 RC 채널 **p99 < 20ms** [추정], 링크 끊김 후 failsafe 진입 **< 1s** [추정], USB 열거 **1,000/1,000회 성공**, 비디오 화면 표시 지연 회귀 **+5ms 이내** [추정] |
| Don 연결 | SK hynix 챔버 테스트 플랫폼(온도·스케줄링·**UART 시퀀스** 자동화), Apple **factory test-node** 아키텍처, I2C/SPI 버스 루트코즈 |

## 1. 이 직군 이해하기 — Neros 공고 근거

- **Peripherals Team**: "design of new peripheral systems including **display systems, handsets and ground control systems**" — Tennessee 사무소 Firmware Engineer [확인됨 공고 5095368007]
- 하드웨어: **STM32 필수**, "Significant **FPGA** experience", "Strong **embedded Linux** skills" — 한 제품 안에 MCU·FPGA·Linux가 같이 있을 수 있다는 뜻 [확인됨 공고 5095368007]. 어느 장비에 무엇이 들어가는지는 [추정]
- 인터페이스: "SPI, I2C, UARTs, **DMA pipelines, ADC/DAC**, interrupts", "Synchronization between clock domains", "camera interfaces, video formats and compression" [확인됨 공고 5095368007] → 핸드셋의 스틱(ADC)·햅틱/오디오(DAC)·헤드셋의 비디오
- 프로토타입 → 양산까지: "from concept, through rapid prototyping and into full scale production" [확인됨 공고 5095368007] → 제조 테스트로 이어짐 ([S07](2026-10-03_S07_manufacturing_factory_test.md))
- 지상 소프트웨어 쪽 짝: **Ground Software Manager**(GCS, low-latency video, ATAK 연동)와 **Lead, Software Application**(운용자·엔지니어용 web/desktop/Android 툴, 오프라인 우선) 공고가 열려 있다 [확인됨 공고 5173033007 · 5242951007]. 지상 장비 FW는 이 앱들과 USB/시리얼/네트워크로 만난다
- 연결 아키텍처: "ground stations, repeaters, radios, antennas … video links, command-and-control links" [확인됨 공고 5253713007 Principal Connectivity] → 지상 라디오와 중계기도 테스트 대상
- 테스트 팀 공고: "validate the Neros **drone & ground control software**" [확인됨 공고 4941340007] → 지상 장비는 FW Test의 정식 범위
- 용어(CRSF·ELRS·OSD·GCS)가 낯설면: [C02 드론 스택 용어 사전](../../onsitePrep/site/company/2026-10-02_C02_drone_tech_stack_glossary.html) · [FTE N06 프로토콜과 드론 스택](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N06_protocols_and_drone_stack.html)

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
 [운용자 손]                                                         [운용자 눈]
     │                                                                    ▲
 스틱(홀센서/포텐셔미터) ─ADC─┐                                   헤드셋 디스플레이(OLED/LCD)
 버튼·스위치 ──GPIO/IRQ──────┤                                            ▲ MIPI DSI / LVDS
 트리거·안전 스위치 ─────────┤                                  ┌──────────┴──────────┐
                             ▼                                   │ 헤드셋 FW            │
                  ┌────────────────────┐                         │ 비디오 RX → 디코드   │
                  │ 핸드셋 MCU (STM32)  │                         │ → OSD 합성 → 표시    │
                  │ 스캔 1~2kHz [추정]  │                         │ (FPGA 또는 Linux SoC)│
                  │ 캘리브·데드밴드·expo│                         └──────────▲──────────┘
                  │ 모드/arm 로직       │── UI ──► 핸드셋 화면              │ 비디오 링크
                  └───┬──────────┬──────┘                                   │ (아날로그/디지털)
          CRSF 등 UART│          │USB (HID / CDC)                           │
                      ▼          ▼                                          │
            ┌──────────────┐   GCS 노트북 ── MAVLink/시리얼 ── 미션·텔레메트리 표시
            │ 지상 라디오 TX│                                                │
            │ (RC·C2 링크)  │ ════ 무선 RC/C2 (수십~수백 Hz) ═══► [기체 RX → FC]
            └──────────────┘ ◄════ 텔레메트리(RSSI·LQ·배터리) ═══          │
                                                       [기체 카메라 → VTX] ┘
```

- **두 방향의 시간 예산**: 손 → 모터(command latency)와 카메라 → 눈(glass-to-glass). 둘 다 FPV 조종감을 좌우한다. 비디오 쪽 상세는 [S04](2026-10-03_S04_video_fpga_pipeline.md), 무선 링크 상세는 [S02](2026-10-03_S02_radio_link_connectivity.md)
- **상태 머신이 안전을 결정**: arm/disarm, failsafe(링크 끊김), 저전압 경고, 모드 스위치. 핸드셋 FW 버그는 곧 **기체 오동작**이다
- **GCS 경계**: 핸드셋/라디오가 USB로 GCS에 joystick(HID) 또는 시리얼 브리지(CDC)로 붙는 구성이 흔하다 [추정]. GCS 앱 테스트는 소프트웨어 팀, **브리지 FW와 프로토콜 계약**은 이 리그의 몫

## 3. 무엇이 고장 나나

| Failure mode | 증상 | 어느 테스트 레벨에서 잡나 |
|---|---|---|
| ADC 노이즈 / 캘리브레이션 저장 실패 | 스틱 중립에서 기체가 흐름(drift), 재부팅 후 끝점 틀어짐 | component(주입) · HIL · factory |
| 데드밴드·expo 수식 off-by-one | 끝점에서 채널 값이 1811이 아니라 1810, 중앙 점프 | unit · component |
| 스캔 루프 jitter (ISR 경쟁, 화면 갱신이 막음) | RC 프레임 간격 들쭉날쭉, 조종감 끊김 | HIL (타이밍 캡처) |
| 버튼 debounce 부족 / 긴 누름 처리 | arm 스위치 한 번에 arm→disarm 토글 | component · HIL |
| 링크 끊김 시 failsafe 미진입·지연 | 기체가 마지막 명령 유지 | HIL · system (RF 감쇠기) |
| USB 열거 실패 / 재연결 후 장치 이름 바뀜 | GCS가 조이스틱을 못 찾음, COM 포트 번호 변경 | integration (USB 스트레스) |
| MAVLink/시리얼 브리지 버퍼 오버플로 | 텔레메트리 끊김, GCS 지도 위치 멈춤 | integration · soak |
| 디스플레이 파이프라인 프레임 드롭·tearing | 화면 끊김, OSD 깜빡임 | HIL (HDMI/카메라 캡처) |
| 배터리 잔량 추정 오류 / 충전 상태 머신 | 30%에서 갑자기 꺼짐, 충전 중 과열 | component(PSU 모사) · environmental |
| FW 업데이트 중 전원 차단 | brick, 부트로더 진입 불가 | HIL (전원 릴레이) · factory |
| 온도·낙하 후 커넥터·버튼 고장 | 간헐적 입력 손실 | environmental · field |
| 버전 불일치 (핸드셋 FW ↔ 기체 RX FW ↔ GCS) | 바인딩 실패, 채널 매핑 틀림 | system (버전 매트릭스) |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | FW 개발자 | 스틱 수식(데드밴드·expo·채널 매핑), CRSF 패킹, debounce 상태 머신, 배터리 SOC 테이블 | host (gcc + Unity/pytest+ctypes) | PR |
| component | FW Test | 실제 보드 1개 + **입력 주입**(DAC로 스틱 전압, GPIO로 버튼) → UART로 나가는 RC 프레임 검증 | 벤치 리그 | PR (짧은 스모크) / nightly (전체) |
| integration | FW Test | 핸드셋 + 지상 라디오 + **레퍼런스 air unit(RX+FC)** + GCS 브리지 — 프로토콜 계약, 버전 매트릭스, USB | HIL 랙 | nightly |
| HIL | FW Test | 위 + 전원 릴레이·RF 감쇠기·비디오 캡처 — failsafe, 업데이트 중 전원 차단, 지연 측정 | HIL 랙 | nightly / release |
| system | FW Test + 비행팀 | 실제 기체 바인딩, 고글로 비행 전 체크리스트, 다중 핸드셋/중계기 | 시험장 실내 케이지 | release |
| field | 시험 조종사 + 텔레메트리 | 실 비행에서 링크 품질·조종감·배터리 수명, 로그 회수 | 필드 | release 후 (로그 분석) |

- 원칙: **사람이 손대야만 되는 테스트를 최대한 HIL로 내린다**. 사람 테스트는 "느낌"(조종감, 화면 가독성)만 남긴다
- CI 파이프라인 일반론은 [N04 system design](../notes/2026-10-02_N04_system_design_test_tooling.md) · [FTE N05 CI](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N05_ci_cd_git.html)

## 5. 테스트베드 설계

```text
            ┌──────────────── CI runner PC (pytest) ─────────────────┐
            │ drivers: StickInjector · ButtonBank · Psu · UsbHub      │
            │          CrsfSniffer · ReferenceAirUnit · VideoCapture   │
            └──┬───────┬────────┬────────┬─────────┬────────┬────────┘
               │USB    │USB     │SCPI    │USB      │USB     │USB3
               ▼       ▼        ▼        ▼         ▼        ▼
         ┌────────┐ ┌──────┐ ┌──────┐ ┌───────┐ ┌──────┐ ┌──────────┐
         │DAC 8ch │ │GPIO/ │ │Prog. │ │USB hub│ │SWD   │ │HDMI/USB  │
         │(스틱   │ │relay │ │PSU   │ │(포트별│ │probe │ │캡처 카드  │
         │전압)   │ │보드  │ │(배터리│ │전원   │ │(플래시│ │+ 포토다이오드│
         └───┬────┘ └──┬───┘ │모사) │ │스위치)│ │·복구) │ └────▲─────┘
             │스틱 핀   │버튼  └──┬───┘ └──┬────┘ └──┬───┘      │
             ▼          ▼         ▼        ▼         ▼          │
        ┌──────────────── DUT: 핸드셋 / 헤드셋 ────────────────┐  │
        │ (스틱 포텐셔미터는 테스트 하네스로 DAC 출력에 연결)    │──┘ 디스플레이 출력
        └───────┬───────────────────────────────┬──────────────┘
                │ RF (SMA 케이블 + 가변 감쇠기 30~90dB)      │ CRSF UART 탭 (로직 분석기/USB-UART)
                ▼                                ▼
        ┌──────────────────────┐        ┌──────────────────┐
        │ 레퍼런스 air unit      │        │ CrsfSniffer      │
        │ RX + FC(벤치용, 모터 X)│──UART─►│ (채널 값·타임스탬프)│
        └──────────────────────┘        └──────────────────┘
          (실드 박스 안: 다른 리그와 RF 간섭 방지)
```

| 장비 | 용도 | 대략 비용 [추정] |
|---|---|---|
| 8ch DAC 보드 (예: USB DAQ) | 스틱 포텐셔미터 대신 전압 주입 (0~3.3V, 12bit) | $150–500 |
| GPIO/릴레이 보드 | 버튼·스위치 누름, 전원 차단 | $50–150 |
| 프로그래머블 PSU (SCPI) | 배터리 전압 곡선 모사, 저전압 경고, 전류 측정 | $400–1,500 |
| 포트별 전원 스위칭 USB 허브 | USB 재연결·열거 스트레스, 리그 리셋 | $100–300 |
| SWD probe (ST-LINK/J-Link) | 플래시, 업데이트 실패 후 복구 | $30–500 |
| 가변 RF 감쇠기 + 실드 박스 | 링크 약화/끊김 재현, 리그 간 간섭 차단 | $500–3,000 |
| HDMI/USB 캡처 카드 + 포토다이오드 | 화면 내용·지연 측정 (헤드셋/핸드셋 화면) | $150–600 |
| 레퍼런스 air unit (RX + 벤치 FC, 프롭 없음) | 기체 쪽 수신 확인 | $100–300 |
| 스틱 로봇 (서보 2축, 선택) | 실제 기구부·홀센서 경로까지 검증 (주입이 못 보는 부분) | $300–1,000 (자작) |
| 온도 챔버 (공용) | -20~+50°C 동작 [추정 운용 범위] | 공용 자산 |

**자동화 구조 (pytest)**

- session fixture: `rig`(YAML로 포트·장비 매핑), `psu`, `swd`, `usb_hub` — 리그는 세션당 한 번 연다 ([문제 04 conftest](../python/problems/04_hil_conftest.md))
- function fixture: `handset`(전원 사이클 + 부팅 대기 + CLI 프롬프트), `air_unit`(바인딩 확인), `stick`(`stick.set(roll=0.0, pitch=1.0)` → DAC 전압 변환), `buttons`(`buttons.press("ARM", ms=300)`)
- 관측 driver: `CrsfSniffer.frames(timeout)` → `(t_us, channels[16])`, `VideoCapture.wait_for(pattern)`, `UsbProbe.enumerate()`
- marker: `@pytest.mark.rf`(감쇠기 필요), `@pytest.mark.video`, `@pytest.mark.slow`(soak) → 리그 capability에 맞춰 선택 실행
- 실패 증거: CRSF 로그, 캡처 영상 5초, DUT 콘솔, PSU 전류 로그를 artifact로 ([N04 문제 C](../notes/2026-10-02_N04_system_design_test_tooling.md))

**리그 수 산정** [추정]

- 제품 3종(핸드셋·헤드셋·지상 라디오) × HW 리비전 2개 = 6 조합. PR 스모크 8분, nightly 전체 90분
- 하루 PR 20개 × 8분 = 160분 → 조합당 1대로도 낮 시간 처리 가능. nightly 90분 × 6 = 9시간 → **조합당 1대 + 예비 2대 = 리그 8대**
- RF 테스트는 실드 박스가 비싸므로 **RF 리그 2대**만 감쇠기 장착, 나머지는 케이블 직결(고정 감쇠)

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 (stimulus) | 관측 | 합격 기준 |
|---|---|---|---|---|
| GP-01 | 스틱 끝점·중앙 매핑 | DAC로 0%, 50%, 100% 전압 (캘리브 후) | CRSF 채널 값 | 172 / 992 / 1811 ±2 [추정 CRSF 범위] |
| GP-02 | 스틱 선형성 sweep | 0→100% 64단계 | 채널 값 vs 전압 | 최대 오차 < 0.5% FS, 단조 증가 |
| GP-03 | 입력 → 송신 지연 | DAC 계단 입력 + 동시 GPIO 마커 | 첫 변화 CRSF 프레임 시각 | p99 < 10ms, 최대 < 15ms [추정] |
| GP-04 | RC 프레임 간격 jitter | 화면 갱신·햅틱 동시 부하 | 프레임 간격 분포 | 목표 주기 ±10% 이내 99.9% [추정] |
| GP-05 | arm 스위치 debounce | 1ms 간격 바운스 10회 주입 | arm 상태 전환 횟수 | 정확히 1회 |
| GP-06 | 링크 끊김 failsafe | 감쇠기 30dB → 90dB 계단 | air unit FC의 failsafe 플래그 | 링크 상실 후 failsafe < 1s [추정] |
| GP-07 | 링크 복구 | 90dB → 30dB | 채널 재수신, arm 상태 | 자동 재연결 < 2s, **자동 re-arm 없음** |
| GP-08 | USB 열거 스트레스 | 허브 포트 전원 on/off 1,000회 | OS 열거 결과, VID/PID, 장치 경로 | 1,000/1,000 성공, 이름 불변 |
| GP-09 | GCS 브리지 soak | MAVLink 텔레메트리 50Hz 8시간 | 시퀀스 번호 누락, 지연 | 누락 0, p99 지연 < 50ms [추정] |
| GP-10 | 저전압 경고 | PSU 4.2V → 3.3V/cell 곡선 | 화면 경고·햅틱·텔레메트리 | 임계값 ±50mV에서 경고, 셧다운 전 저장 완료 |
| GP-11 | 업데이트 중 전원 차단 | 플래시 진행 10%/50%/90%에서 릴레이 차단 | 재부팅 후 상태 | 100% 이전 버전 또는 복구 모드로 부팅, brick 0 |
| GP-12 | 버전 매트릭스 바인딩 | 핸드셋 FW N, N-1 × RX FW N, N-1 | 바인딩·채널 매핑 | 지원 조합 전부 통과, 미지원은 명확한 에러 표시 |

- 테스트 케이스 하나의 모양(전제·자극·관측·판정·정리)은 [FTE N02 7절](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N02_hil_for_drones.html) 참고

## 7. 면접 시나리오 — "Design a test system for our handset and goggles firmware"

### 요구사항 질문 (영어)

- "Which products are in scope — handset, goggles, ground radio — and how many hardware revisions are live?"
- "What's the most expensive failure in the field today: control latency, link loss behavior, video, or bricked updates?"
- "Do I have access to the stick hardware path, or should I inject at the ADC input?"
- "Should this run on every merge, or is nightly acceptable for the RF and video parts?"
- "Does the same rig need to serve the factory line later?"

### 가정 숫자 [추정]

- 제품 3종 × 리비전 2 = 6 조합, 하루 PR 20개, 리그 8대(5절)
- RC 프레임 수백 Hz, 입력 → 송신 지연 목표 p99 10ms, failsafe 1s, 비디오 지연 회귀 허용 +5ms
- 스모크 8분(PR), 전체 90분(nightly), soak 8시간(주 1회)

### 블록도

- 5절 다이어그램: 입력 주입(DAC·GPIO) → DUT → RF 케이블+감쇠기 → 레퍼런스 air unit → CRSF 스니퍼, 디스플레이 → 캡처, 전원·USB는 PC가 제어

### 핵심 결정과 trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| 스틱 자극 | **DAC 전압 주입** 기본 + 스틱 로봇 1대 | 주입은 빠르고 정확하지만 기구부·홀센서는 못 본다 → 로봇은 nightly에만 |
| 관측 지점 | 무선 너머 **레퍼런스 air unit**에서 CRSF 캡처 | 핸드셋 UART 탭만 보면 무선 구간 버그를 놓친다. 대신 RF 환경 통제 필요 |
| RF 환경 | 케이블 + 감쇠기 + 실드 박스 | 공중 전파보다 재현성 높음. 안테나·전파 특성은 system/field로 미룸 |
| 화면 검증 | 캡처 카드로 **패턴/OSD 인식** + 포토다이오드로 지연 | 사람 눈 대비 객관적. 디스플레이 화질(색·밝기)은 별도 광학 테스트 |
| 리그 구성 | 제품별 전용 리그 vs 범용 리그 + 하네스 교체 | 전용은 단순·빠름, 범용은 장비 절약. 초기엔 전용, 제품이 늘면 하네스 표준화 |
| 실패 분류 | 리그 장비 예외 = `infra error` | 감쇠기·캡처 카드 고장이 FW 회귀로 보이지 않게 |

### 실패 모드

- 리그 간 RF 간섭 → 실드 박스, 채널 고정, 리그별 바인딩 ID
- 캡처 카드가 프레임을 떨굼 → 캡처 자체 프레임 카운터와 비교, 의심 시 infra error
- DAC 주입이 실제 스틱보다 "너무 깨끗함" → 노이즈 주입 모드(±1 LSB 랜덤) 추가
- 레퍼런스 air unit FW가 바뀌어 기준이 흔들림 → air unit 버전을 고정(pin)하고 매트릭스 테스트만 별도로

### 영어 2분 요약

> "I'd build a bench rig that replaces the operator's hands and eyes. A DAC drives the stick inputs and a relay board presses buttons, so every input is scripted and repeatable. On the output side, I don't stop at the handset's UART — the RF goes through a cable and a programmable attenuator into a reference air unit, and I sniff the CRSF frames there, so I'm testing what the aircraft actually receives. A capture card and a photodiode watch the goggles' display. The PC also controls power and USB, so I can test brownouts, interrupted updates and USB re-enumeration. Pytest fixtures wrap each instrument; PR runs get an eight-minute smoke, nightly runs the full suite including failsafe and latency, and a stick robot covers the mechanical path that injection can't. Rig and instrument faults are reported as infrastructure errors, separately from firmware failures."

## 8. 꼬리 질문

**Q. Why inject at the ADC instead of moving the real sticks?**

> "Injection gives exact, repeatable values at high speed, so I can sweep linearity and measure latency precisely. It skips the gimbal and sensor, so I keep one robot rig for that path and run it nightly."

**Q. How do you measure input-to-output latency precisely?**

> "I toggle a GPIO marker at the same instant as the DAC step, capture both the marker and the decoded CRSF frames on one timebase, and report the distribution — p50, p99, max — not a single number."

**Q. How do you test failsafe without flying?**

> "Cable the RF through a programmable attenuator inside a shielded box, ramp it until the link drops, and assert the flight controller on the reference air unit enters failsafe within the budget — and that it never re-arms by itself when the link comes back."

**Q. The handset, receiver and GCS all ship on different schedules. How do you handle compatibility?**

> "A version matrix job: current and previous release of each, nightly. Supported pairs must pass; unsupported pairs must fail cleanly with a clear message, never silently mis-map channels."

**Q. What would you not automate?**

> "Subjective feel — stick ergonomics, display readability in sunlight. That stays with test pilots, but I'd give them a structured checklist and attach the build ID so their feedback lands in the same results database."

## 9. Don 경험 연결 (레쥬메 범위)

| 이 노트의 포인트 | Don 레쥬메 근거 | 말할 문장 |
|---|---|---|
| 사람 없이 장비를 스크립트로 제어 | SK hynix 챔버 테스트 web 플랫폼 + SDK (온도 제어 · 스케줄링 · eSSD 상태 · **UART 시퀀스** 자동화) | "I've built a platform that scripted chambers, power and UART sequences for other engineers — this rig is the same idea with sticks and RF instead of temperature." |
| 양산 전환을 고려한 리그 | Apple **factory test-node** 아키텍처, bring-up → NPI → MP | "I design bench rigs so they can become factory stations later — same drivers, different fixtures." |
| 버스·주변장치 루트코즈 | Apple I2C/SPMI/RFFE, Solidigm SoC verification (I2C, SPI, DMA) | "When a stick reads wrong, I can go down to the ADC, DMA and I2C level, not just report a failed test." |
| 장비 사용 | DSO, protocol analyzer, JTAG/스코프/LA | (Don: 실제로 쓴 장비 모델·측정 사례 — 레쥬메에 없음) |
| 갭 | 핸드셋·고글 제품, RF 링크, 비디오 경험 없음 | 정직하게: "I haven't shipped a handset, but the test architecture is the same pattern I've used — inject, observe on the far side, control power." |

## 체크

- [ ] 2절 데이터 흐름을 보지 않고 그리기 (손 → 기체, 카메라 → 눈 두 방향)
- [ ] 5절 리그 블록도를 3분 안에 화이트보드로
- [ ] 6절 GP-03 · GP-06 · GP-11의 합격 기준과 측정 방법을 영어로 30초씩
- [ ] 7절 영어 2분 요약 소리 내어 2회
- [ ] [S02 라디오 링크](2026-10-03_S02_radio_link_connectivity.md) · [S04 비디오](2026-10-03_S04_video_fpga_pipeline.md)와 겹치는 부분(감쇠기, 캡처) 구분하기

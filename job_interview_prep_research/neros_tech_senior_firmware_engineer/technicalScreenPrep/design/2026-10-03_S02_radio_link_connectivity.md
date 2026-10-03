# 📶 S02 · Radio link & Connectivity — 무엇을 만들고, 어떻게 테스트하나

> FPV 드론은 링크가 끊기면 끝이다. Neros는 C2·video 라디오를 직접 만들고, RC 링크(ExpressLRS/CRSF), LoRa C2, 디지털·아날로그 비디오, repeater·mesh까지 다룬다. 이 직군의 펌웨어는 "패킷이 오가나"가 아니라 **"감쇠·페이딩·재밍 속에서 얼마나 오래, 얼마나 빨리 오가나"**로 평가된다. 이 노트는 링크 펌웨어를 위한 테스트 시스템 — 차폐 박스 + 감쇠기 + 채널 에뮬레이터 + SDR 재머로 이루어진 **conducted 테스트베드**와, chamber·range의 **radiated 테스트** — 를 면접에서 설계할 수 있게 정리한다.

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | video·C2·telemetry·repeater·mesh·point-to-point·ground-to-air 링크 아키텍처와 그 위의 embedded SW, 진단, field update [확인됨 공고 5253713007] |
| 주로 고장 나는 곳 | 감도 열화(desense), 간섭·공존, hopping 동기 상실, 재연결 지연, 링크 품질 지표 오류, 비디오 지연 급증, field 업데이트 후 회귀, 재밍 하 링크 붕괴 |
| 대표 테스트베드 | 차폐 RF box + 프로그래머블 step attenuator + RF switch matrix + 채널 에뮬레이터 + SDR 간섭원/재머 + 스펙트럼 분석기; chamber/OTA; 실거리 range test |
| 합격 기준 예 | 감도점(PER 10%)이 기준 대비 ±2dB, -X dBm에서 PER < 1%, 링크 손실 후 재연결 < 1s, C2 end-to-end 지연 p99 < 기준, 재밍 조건별 링크 유지 시간 [추정] |
| Don 연결 | **Apple RF 칩셋 통합**(RF-adjacent FW, RFFE), factory test-node, DSO·protocol analyzer. ELRS/LoRa 실무 ❌ — 링크 버짓·테스트 자동화로 연결 |

## 1. 이 직군 이해하기 — Neros 공고 근거

- **Principal Connectivity Engineer** [확인됨 공고 5253713007]: "connects our FPV drones, ground stations, repeaters, radios, antennas, embedded systems, video links, command-and-control links", "link budget, waveform/modulation, antennas, embedded compute, power, thermal, latency", "video, command-and-control, telemetry, repeater, mesh, point-to-point, and ground-to-air connectivity", "telemetry, diagnostics, and **field-update** considerations", nice: "**contested RF environments**", "EW-aware communications"
- **Lead RF Test & Integration Engineer** [확인됨 공고 5226790007]: "RF qualification testing for FPV drone video links, command-and-control links, antennas, RF front ends", "EVT/DVT/PVT", "**conducted RF tests, radiated tests, range/performance checks, and automated production test limits**", 장비 "VNAs, spectrum analyzers, signal generators, power meters, **SDRs, chambers/OTA setups, Python automation**", "EVM/BER/**PER**, sensitivity, … desense, coexistence, link budget", nice: "digital video links, analog video links, **LoRa command-and-control links**", "conducted-to-radiated correlation"
- **Senior EW Test & Evaluation Engineer** [확인됨 공고 5253709007]: "full test loop … against **jamming threats** — benchtop and radiated (OTA)", "barrage, swept, and reactive jamming; frequency hopping, DSSS, spatial nulling", "derive and document the specifications … verify them through iterative re-testing"
- **Firmware Test Engineer** [확인됨 공고 4941340007]: nice "Experience **testing RF products**", "ExpressLRS"
- **Ukraine Senior FW** [확인됨 공고 5086180007]: Archer의 "flight control and **radio link code**"

> 읽기: RF 측정(감도, EVM, 안테나)은 RF Test 팀, 재밍 캠페인은 EW T&E 팀이 맡는다. **FW Test가 기여할 지점**은 그 사이 — 링크 **펌웨어**(hopping, 재연결, 패킷 처리, 품질 지표, 업데이트)를 **자동화된 conducted 리그**에서 매 빌드 회귀 테스트하는 것 [추정: 역할 분담은 공고 문구에서 추론].

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
  GROUND                                                     AIR (drone)
  pilot sticks ─► handset / GCS ─► TX module FW ──RF──► RX FW ─► CRSF/UART ─► flight controller
                                     │  ▲                │  │
                    hop sequence,    │  │ telemetry      │  └─► link stats (RSSI, LQ, SNR) ─► FC / OSD
                    packet rate,     │  └────────────────┘      (uplink 150–500 Hz, downlink 1:N) [추정]
                    power control    │
  C2 / mission ─► LoRa C2 radio ◄──RF──► air C2 radio ─► FC / companion (low rate, long range)
  goggles / GCS ◄── video RX ◄──RF── video TX ◄── camera (analog or digital, low latency)
                       ▲
                  repeater / mesh node (relay beyond LOS)
```

- 링크 펌웨어의 핵심 일: **동기(sync) 획득 → hopping 추종 → 패킷 송수신 + CRC → 품질 지표 계산 → 실패 시 재획득 → 적응(전력·rate·채널)**
- 숫자(패킷 rate, 텔레메트리 비율)는 ELRS 계열의 일반 범위 [추정]. Neros 자체 라디오의 파라미터는 미확인 — in-house 라디오 설계는 회사 사이트 기준 [컨텍스트 파일 2.2]
- 프로토콜 기본: [FTE N06 프로토콜과 드론 스택](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N06_protocols_and_drone_stack.html) · 용어: [C02](../../onsitePrep/site/company/2026-10-02_C02_drone_tech_stack_glossary.html)

## 3. 무엇이 고장 나나

| Failure mode | 증상 | 어느 레벨에서 잡나 |
|---|---|---|
| 감도 열화 (desense: 자기 기체 노이즈) | 실기체에서만 range 짧음 | conducted(모듈 단독) vs 통합 기체 비교, radiated |
| hopping 동기 상실 / 재획득 느림 | 짧은 페이드 후 수 초 끊김 | conducted 리그: 감쇠 step + 페이딩 |
| 패킷 rate 전환 버그 | rate 변경 시 링크 drop | 리그 자동 매트릭스 (rate × power × 감쇠) |
| 품질 지표(LQ/RSSI) 계산 오류 | OSD는 정상인데 실제로 끊김 → 잘못된 failsafe 판단 | 리그: 주입한 PER vs 보고된 LQ 비교 |
| 재연결 / 바인딩 상태 머신 | 전원 순서에 따라 바인딩 실패 | 리그: 전원 순서 매트릭스 |
| 텔레메트리 큐 overflow | 다운링크 지연 증가, 업링크 영향 | 리그: 텔레메트리 부하 + 지연 측정 |
| 비디오 지연 급증 / 프레임 drop | 조종 불가 | 비디오 리그: glass-to-glass 지연 측정 |
| 공존 (C2 ↔ video ↔ GPS 간섭) | 특정 채널 조합에서만 열화 | conducted 다중 라디오 + radiated |
| 재밍 하 붕괴 (barrage/swept/reactive) | 링크 유지 시간 급감 | SDR 재머 conducted → EW 팀 OTA |
| repeater/mesh 경로 전환 | 노드 이동 시 끊김 | 다중 노드 리그 (switch matrix로 경로 감쇠 제어) |
| field update 회귀 / 업데이트 중단 | brick, 구버전 호환 깨짐 | 리그: 업데이트 중 전원 차단, 버전 매트릭스 |
| 온도·전압에 따른 주파수 drift | 저온에서 sync 실패 | 챔버(온도) + conducted |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | 링크 FW 개발자 | 패킷 인코딩/CRC, hop 시퀀스 생성, 상태 머신, 품질 지표 계산 | host | 모든 PR |
| component | FW + RF Test | 무선 모듈 단독: 출력 전력, 감도, 주파수 정확도 | RF 벤치 (계측기) | 보드 리비전·RF 관련 변경 시 |
| integration (conducted 리그) | **FW Test** | TX↔RX를 케이블+감쇠기로 연결, 감쇠·페이딩·간섭 프로파일 자동 재생, PER·지연·재연결 측정 | 차폐 박스 리그 팜 | PR 스모크 10분 / nightly 매트릭스 |
| HIL | FW Test | RX → 실제 FC까지 (CRSF 경로, failsafe 판단에 링크 지표 반영) | 리그 + FC | nightly |
| system (radiated) | RF Test / EW T&E | chamber/OTA, 안테나 패턴, 통합 기체 desense, 재밍 OTA 캠페인 | chamber, 야외 range | release / DVT |
| field | Test + 파일럿 | 실거리 range test, 지형·repeater, 링크 로그 수집 | 시험장 | release gate |

> 원칙: **conducted 리그에서 링크 FW의 논리와 타이밍을 매 빌드** 검증하고, radiated·field는 RF 성능과 통합 효과를 본다. 둘을 잇는 게 **conducted-to-radiated correlation** [확인됨 공고 5226790007 문구].

## 5. 테스트베드 설계

### 5.1 Conducted 링크 리그

```text
  ┌──────────── Rig host (pytest runner) ────────────┐
  │ scenario: attenuation/fading/jammer timeline      │◄── results ingest (N04 문제 C)
  └──┬───────────┬───────────────┬──────────────┬────┘
     │USB/UART   │ USB/LAN       │ LAN          │ USB/UART
 ┌───▼────┐  ┌───▼──────────┐ ┌──▼───────────┐ ┌▼────────┐
 │ TX DUT │  │ step atten.  │ │ channel emu  │ │ RX DUT  │──CRSF──► FC (선택: HIL 모드)
 │(shield │══│ 0–110 dB,    │═│ fading,      │═│(shield  │
 │ box A) │  │ 0.25 dB step │ │ multipath,   │ │ box B)  │
 └────────┘  └──────────────┘ │ doppler      │ └─────────┘
                              └──────┬───────┘
                         combiner ◄──┤◄── SDR interferer / jammer (barrage, swept, reactive)
                                     └──► coupler ──► spectrum analyzer / SDR monitor
  ═ = coax (SMA). DUT 안테나 포트를 케이블로, 각 DUT는 차폐 박스 안 (누설 > 80 dB 격리) [추정]
```

- **왜 conducted인가**: 같은 dB 경로를 **재현 가능하게** 매번 만든다. 사무실 공기(radiated)는 매번 다르다
- **경로 손실 보정**: 케이블·커플러·combiner 손실을 VNA/파워미터로 측정해 리그 config에 저장 → "감쇠기 설정 = 실제 경로 손실"이 아니다
- **다중 노드(repeater/mesh)**: RF switch matrix + 경로별 감쇠기로 노드 간 링크 세기를 시간에 따라 바꿔 "기체가 repeater 쪽으로 이동"을 재현

### 5.2 장비 표

| 장비 | 용도 | 대략 비용 [추정] |
|---|---|---|
| 차폐 RF box × 2 | DUT 격리 | $1,000–5,000 / 개 |
| 프로그래머블 step attenuator (0–110 dB) | 거리 시뮬레이션 | $1,000–5,000 |
| RF switch matrix | 다중 노드 경로, 계측기 공유 | $2,000–15,000 |
| 채널 에뮬레이터 | fading·multipath·doppler | $20,000–150,000 (SDR 자체 구현 시 수천 달러) |
| SDR (USRP / bladeRF / HackRF 급) | 간섭원·재머·모니터 | $300–10,000 |
| 스펙트럼 분석기 | 출력·스퓨리어스·hop 관찰 | $5,000–50,000 |
| 파워미터 / VNA | 경로 손실 보정 | $1,000–30,000 |
| combiner·coupler·케이블·DC block | 배선 | $500–2,000 |
| 비디오 지연 측정 (LED + 포토다이오드 + 카메라/고속 캡처) | glass-to-glass 지연 | $200–2,000 |
| 온도 챔버 (공유) | drift | 기존 장비 공유 |

### 5.3 자동화 구조 (pytest)

| 계층 | 이름 예 | 역할 |
|---|---|---|
| fixture (session) | `rf_rig` (보정 테이블 로드), `atten`, `jammer`, `sa` | 계측기 연결, SCPI 세션 |
| fixture (function) | `link` (TX·RX 리셋 + 바인딩 + sync 대기), `traffic` (패킷 카운터 시퀀스 생성) | 테스트마다 깨끗한 링크 |
| driver | `Attenuator.set_db(x)`, `Attenuator.ramp(profile)`, `Jammer.barrage(center, bw, dbm)`, `Jammer.sweep(...)`, `LinkStats.read()` | 계측기 추상화 (SCPI/pyvisa, SDR API) |
| 측정 | `measure_per(duration_s)`, `measure_latency(n)`, `time_to_reconnect()` | 시퀀스 번호 기반 손실·순서·지연 |
| scenario | `profiles/fade_6db_10hz.yaml`, `profiles/jam_swept_2g4.yaml` | 감쇠·재머 타임라인 |
| marker | `@pytest.mark.rf`, `@pytest.mark.jam`, `@pytest.mark.slow` | 리그 종류 지정 |

- **측정 원칙**: 링크 펌웨어가 보고하는 LQ/RSSI를 **믿지 않는다**. 테스트가 자체 시퀀스 번호로 PER을 계산하고, 펌웨어 보고값은 그 비교 대상 (F06)

### 5.4 리그 수 산정 [추정]

- 링크 종류 3 (RC/C2, LoRa C2, video) × 하드웨어 리비전 2 = 6 조합, 각 1대 + 재밍 전용 1대 = **7 리그**
- nightly 매트릭스: 감쇠 20 step × packet rate 4 × 전력 3 = 240 점 × 30초 = 2시간/리그 → 조합당 1대면 충분, PR 스모크는 대표 6점 10분

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 (stimulus) | 관측 | 합격 기준 [추정] |
|---|---|---|---|---|
| R01 | 감도 곡선 (waterfall) | 감쇠 0.5dB step 증가 | PER vs 경로 손실 | PER 10% 지점이 기준(이전 릴리즈) 대비 ±1dB 이내 |
| R02 | 링크 지연 | 정상 신호, 1만 패킷 | uplink end-to-end 지연 | p50·p99가 packet rate 대비 기준 이내, p99 회귀 < 10% |
| R03 | 짧은 페이드 회복 | 300ms 동안 +40dB 후 원복 | 재동기 시간 | 신호 복귀 후 재연결 < 500ms (hop 동기 유지) |
| R04 | 완전 손실 → 재연결 | 5s 차단 | failsafe 신호, 재연결 시간 | RX가 손실 선언 ≤ 기준, 복귀 후 < 1s 재연결 |
| R05 | 페이딩 채널 | Rayleigh, doppler(상대속도 150km/h 상당) | PER, LQ | 같은 평균 SNR의 정적 채널 대비 PER 열화 < 기준 |
| R06 | 품질 지표 정확도 | 주입 PER 0/5/20/50% | FW 보고 LQ | 보고값과 실측의 차이 < 5%p |
| R07 | packet rate 전환 | 링크 중 rate 변경 명령 | 끊김 시간 | 전환 중 손실 < N 패킷, 재바인딩 불필요 |
| R08 | 텔레메트리 부하 | 다운링크 최대 + 업링크 정상 | 업링크 지연, 큐 깊이 | 업링크 p99 지연 영향 < 10% |
| R09 | barrage 재밍 | 대역 전체 noise, J/S 단계 증가 | 링크 유지 시간, PER | J/S별 링크 유지가 스펙(EW 팀 도출) 이상 |
| R10 | swept 재밍 | 주파수 sweep 재머 | hop 회피, PER | sweep 속도별 PER < 스펙 |
| R11 | field update 중단 | 업데이트 중 전원 차단 3지점 | 부팅, 버전 | brick 0, 이전 버전 또는 새 버전으로 정상 부팅 |
| R12 | 버전 호환 | TX 구버전 × RX 신버전 매트릭스 | 바인딩, 기능 | 정의된 호환 범위에서 링크 성립, 비호환은 명확한 오류 |

## 7. 면접 시나리오 — "Design a test system for our radio link firmware"

### 요구사항 질문 (영어)

- "Which links are in scope — RC and C2, LoRa C2, video, repeaters? Same firmware team or separate?"
- "What does the RF test team already own — conducted RF measurements, chamber? I want the firmware rig to complement it, not duplicate it."
- "What metrics define a good link here — range at a PER threshold, latency, reconnect time, behavior under jamming?"
- "How often does link firmware change, and does every PR need a rig run?"
- "Do we need to test against specific jammer profiles from the EW team?"

### 가정 숫자 [추정]

- 링크 FW PR 하루 10개, 링크 종류 3, nightly 매트릭스 240점
- PR 스모크: 대표 감쇠 6점 + 페이드 1 + 재연결 1 = 10분

### 블록도

- 5.1 conducted 리그 + 결과 파이프라인([N04 문제 C](../notes/2026-10-02_N04_system_design_test_tooling.md)) + RF Test 팀 chamber와의 correlation 루프

### 핵심 결정 & trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| conducted vs radiated | 매 빌드는 **conducted**, radiated는 release·HW 변경 시 | conducted는 안테나·기체 desense를 못 본다 → correlation 측정으로 보정 |
| 채널 에뮬레이터 | 처음엔 step attenuator 타임라인(싸다), 페이딩이 문제가 되면 SDR 기반 → 상용 에뮬레이터 | 비용 vs 현실성 |
| 측정 기준 | 테스트 자체 시퀀스 번호로 PER·지연 | 펌웨어 보고값 버그를 잡을 수 있다 |
| 재밍 | SDR로 barrage/swept를 conducted에서 회귀, 새 위협·OTA는 EW 팀 캠페인 | 회귀 자동화 vs 현실 위협 |
| 기준선 | 이전 릴리즈의 감도 곡선을 golden으로 저장, ±1dB 비교 | 절대 스펙보다 회귀 검출이 빠르다 |
| 리그 보정 | 주 1회 자동 경로 손실 측정 (VNA/파워미터), 변하면 리그 quarantine | 보정 드리프트가 가짜 회귀를 만든다 |

### 실패 모드 (테스트 시스템 자체)

- 차폐 박스 누설 → 감쇠를 올려도 PER이 안 오름. 리그 셀프 체크: 최대 감쇠에서 링크가 반드시 끊겨야 함
- 커넥터 마모 → 경로 손실 변화. 정기 보정 + 커넥터 saver
- 재머가 다른 리그를 간섭 → 재밍 리그는 별도 차폐, 출력 상한 설정 (규제·안전)

### 영어 2분 요약

> "RF performance and jamming campaigns belong to the RF test and EW teams, so I'd focus the firmware test system on what changes with every build: the link firmware's behavior. The core is a conducted rig: transmitter and receiver in shielded boxes, connected through cables, a programmable step attenuator, a fading channel and an SDR that can play barrage or swept jamming. A pytest scenario drives an attenuation and interference timeline, and the test computes PER and latency from its own sequence numbers rather than trusting the link-quality the firmware reports — that's actually one of the things we test. Every PR gets a ten-minute smoke run; nightly runs a full waterfall across packet rates and power levels and compares it to the last release within a dB. Path loss is recalibrated weekly, and a rig that fails its self-check is quarantined. Radiated and range tests run at release, and we keep a conducted-to-radiated correlation so the bench numbers mean something in the field."

## 8. 꼬리 질문

**Q. Why not just do range tests?**

> "They're essential but slow, weather-dependent and not repeatable. A conducted rig reproduces the same path loss and fading every night, so a one-dB regression in a firmware build is visible the next morning."

**Q. How do you measure end-to-end latency across a radio link?**

> "Put a sequence number and a timestamp from a shared clock in each packet — or toggle GPIOs on both sides into the same logic analyzer — and compute the distribution. Report p50, p99 and max, not the average."

**Q. What's conducted-to-radiated correlation?**

> "Measuring the same device both ways and recording the offset, so a conducted sensitivity number predicts radiated range. If the integrated drone is much worse than the module alone, that gap is desense from the aircraft's own electronics."

**Q. How would you test frequency hopping under swept jamming?**

> "Use an SDR to sweep across the band at different rates and powers, while the link carries sequenced traffic. I'd plot PER and link-hold time versus sweep rate and J/S, and compare against the spec the EW team derives."

**Q. The firmware reports 100% link quality but the pilot lost control. What happened?**

> "Then the metric is wrong or measures the wrong thing — maybe it counts received packets but not late ones. I'd reproduce with a known injected loss pattern on the rig and compare the reported LQ against measured PER and latency."

**Q. Have you tested RF products?**

> "My RF background is integration rather than RF measurement: at Apple I root-cause failures when new RF chipsets meet the full system, including RFFE control. I haven't worked with ExpressLRS or LoRa directly, but automating the rig, defining pass criteria and root-causing link failures is where I'd contribute."

## 9. Don 경험 연결 (레쥬메 범위)

| 링크 테스트 요소 | Don 경험 | 말할 문장 |
|---|---|---|
| RF 칩셋과 시스템의 경계 | Apple RF-Hardware Chipset Integration, RFFE·I2C·SPMI 루트코즈 | "RF failures often aren't RF — they're control, timing or power at the boundary." (Don: 사례) |
| 제조·acceptance 테스트 | Apple factory test-node 아키텍처 | "Production limits and fixtures are part of the test design, not an afterthought." |
| 장시간 자동화·스케줄링 | SK hynix 챔버 테스트 플랫폼 + SDK | "Long unattended sweeps with recovery and logging are what the chamber platform did." |
| 계측 | DSO, protocol analyzer | "I trust instruments over self-reported metrics." |
| ELRS·LoRa·EW | ❌ 직접 경험 없음 | 8절 마지막 답처럼 정직하게 |

## 체크

- [ ] 5.1 conducted 리그를 보지 않고 그리기 (TX box — attenuator — channel emu — combiner/jammer — RX box)
- [ ] R01 waterfall, R03 페이드 회복, R06 LQ 정확도를 영어로 20초씩
- [ ] "conducted vs radiated", "conducted-to-radiated correlation"을 한 문장씩
- [ ] 7절 영어 2분 요약 소리 내어 2번
- [ ] RF Test 팀 · EW T&E 팀 · FW Test의 역할 분담을 30초로

# 🃏 S09 · 시나리오 카드 16장 — 10분 system design 연습용

> S01~S08의 7절(면접 시나리오)을 **연습 카드**로 모았다. 카드마다 면접관이 던질 문장, 첫 대사, 약속 3개(무엇을 증명할지), 리그의 핵심 부품, 꼭 말할 숫자 하나, 빠지기 쉬운 함정, 깊게 볼 노트를 적었다. 매일 2장을 무작위로 뽑아 **10분 타이머 + 소리 내어** 답한다. 전체 틀은 [S00 4절](2026-10-03_S00_overview.md), 결과 파이프라인 · HIL 팜은 [N04](../notes/2026-10-02_N04_system_design_test_tooling.md).

## 0. 연습 방법

1. 카드 번호를 무작위로 2개 고른다 (주사위, `python3 -c "import random; print(random.sample(range(1,17),2))"`)
2. **질문 문장만** 보고 10분 타이머 — [N04 0절](../notes/2026-10-02_N04_system_design_test_tooling.md) 6단계 순서로 말하며 종이에 블록도
3. 카드의 "약속 · 리그 · 숫자 · 함정"과 비교해서 빠진 것 표시
4. 해당 노트 7절 영어 2분 요약을 한 번 소리 내어 읽기
5. 녹음해 두고, 첫 1분에 요구사항 질문을 했는지 확인

## 1. Flight Software

### 카드 1 · "Design a test system for our flight controller firmware."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "Flight SW engineers own unit tests; I'd own two shared assets — SITL for scale and HITL for timing." |
| 약속 3개 | 제어 루프 타이밍 · failsafe 진입과 복귀 · 모드 전환 안정성 |
| 리그 핵심 | 실제 STM32 FC + 센서 주입(SPI slave MCU) + CRSF 주입 + DShot 출력 캡처(µs 타임스탬프) + PSU |
| 숫자 하나 | PR 스모크 ~10분, nightly 수백 시나리오, 루프 지터 p99 기준 [추정] |
| 함정 | HITL만으로 공기역학을 증명하려 함 → thrust stand · 비행 시험을 남겨 둘 것 |
| 노트 | [S01](2026-10-03_S01_flight_software.md) 5 · 7절 |

### 카드 2 · "How would you test RC-loss failsafe?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "I'd define failsafe as a timeline — link lost at t0, expected stage changes at t1 and t2 — and assert on that timeline from the outside." |
| 약속 3개 | 손실 감지 시간 · 단계별 동작(hold → land/disarm) · 링크 복귀 후 재무장 금지 |
| 리그 핵심 | CRSF 주입기에서 프레임 중단 / 부분 손실 / 지연, 모터 출력 캡처로 동작 판정 |
| 숫자 하나 | 감지 → 진입 < 수백 ms 수준의 예산 [추정], 경계값 ± 1프레임 |
| 함정 | 완전 손실만 테스트 → **간헐 손실 · 지터 · 링크 복귀 순간**이 더 위험 |
| 노트 | [S01](2026-10-03_S01_flight_software.md) 6절 · [S08](2026-10-03_S08_system_integration_release.md) |

## 2. Radio link

### 카드 3 · "Design a test system for our radio link firmware."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "RF performance belongs to RF test; I'd focus on what changes with every firmware build — the link's behavior under loss and interference." |
| 약속 3개 | 감쇠별 PER · 지연 · 재연결 시간 · 재밍 중 동작 |
| 리그 핵심 | 차폐 박스 2개 + 케이블 + step attenuator + 채널 에뮬레이터 + SDR 재머 + 스펙트럼 분석기 |
| 숫자 하나 | nightly waterfall(감쇠 sweep)을 지난 릴리즈와 dB 단위로 비교 [추정] |
| 함정 | 펌웨어가 보고하는 link quality를 그대로 믿음 → **시퀀스 번호로 직접 PER 계산** |
| 노트 | [S02](2026-10-03_S02_radio_link_connectivity.md) |

### 카드 4 · "How would you test the link under jamming?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "Conducted first, so it's repeatable: an SDR injects barrage or swept interference at a scripted power while I measure PER and recovery." |
| 약속 3개 | 링크 유지 · hopping/회피 동작 · 손실 시 failsafe로 정확히 넘어감 |
| 리그 핵심 | combiner로 신호 + 간섭 합성, 간섭 전력 · 대역 · 패턴을 시나리오로 |
| 숫자 하나 | J/S 비(간섭 대 신호)별 PER 곡선 [추정] |
| 함정 | 방사(radiated) 환경에서만 시험 → 재현 불가. 방사는 릴리즈 시점 상관관계 확인용 |
| 노트 | [S02](2026-10-03_S02_radio_link_connectivity.md) · EW T&E 공고 |

## 3. Ground & Peripherals

### 카드 5 · "Design a test system for our handset and goggles firmware."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "I'd build a rig that replaces the operator's hands and eyes." |
| 약속 3개 | 스틱 → 기체 수신 값의 정확도 · 지연 · 버튼/메뉴/전원 이벤트 견고성 |
| 리그 핵심 | DAC 스틱 주입 + 버튼 릴레이 + 감쇠기 → 기준 air unit + CRSF sniffer + 화면 캡처/포토다이오드 + USB 전원 제어 |
| 숫자 하나 | 스틱 sweep 선형성 오차, 입력 → 프레임 지연 분포 [추정] |
| 함정 | 핸드셋 UART에서 판정 → **기체가 실제로 받은 것**에서 판정해야 함 |
| 노트 | [S03](2026-10-03_S03_ground_and_peripherals.md) |

## 4. Video / FPGA

### 카드 6 · "How would you measure and gate glass-to-glass latency?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "A single measurement is meaningless — latency depends on where the event lands in exposure and scan, so I collect a distribution." |
| 약속 3개 | p50/p99/max 예산 · 단계별 지연(어디가 느려졌나) · 회귀 없음 |
| 리그 핵심 | 카메라 앞 LED(무작위 시점) + 화면 포토다이오드 + µs 타이머 MCU + 단계별 GPIO 마커 |
| 숫자 하나 | 수백 샘플로 p99 게이트 [추정] |
| 함정 | 평균만 보고 판정, 소프트웨어 타임스탬프만 사용 |
| 노트 | [S04](2026-10-03_S04_video_fpga_pipeline.md) |

### 카드 7 · "Design a test system for our video pipeline."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "Three questions, three rigs: is it correct, is it fast, is it robust." |
| 약속 3개 | 프레임 정확성(raw = CRC 일치, 압축 = SSIM 기준) · 지연 · 오류 후 복구 |
| 리그 핵심 | 패턴 생성기(프레임 카운터 포함) → FPGA, 비트 오류 · 링크 끊김 주입, 열 챔버 soak |
| 숫자 하나 | 수백만 프레임 soak로 CDC 버그 잡기 [추정] |
| 함정 | 비트스트림 · FW · 수신기 버전을 기록하지 않음 → 회귀 원인 추적 불가 |
| 노트 | [S04](2026-10-03_S04_video_fpga_pipeline.md) |

## 5. Platform

### 카드 8 · "Design a test system for our platform libraries (logging, telemetry, config)."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "Platform bugs multiply across every product line, so I gate early and cheap." |
| 약속 3개 | 모든 타깃 × 소비자 빌드 · API/ABI · 스키마 계약 유지 · 자원 예산 |
| 리그 핵심 | host 테스트 + sanitizer + fuzz, CI 빌드 매트릭스, ABI diff, map 파일 footprint diff |
| 숫자 하나 | 라이브러리별 flash/RAM 예산, PR마다 diff [추정] |
| 함정 | 소비자 팀 빌드를 PR에서 안 돌림 → 일주일 뒤 다른 팀이 발견 |
| 노트 | [S05](2026-10-03_S05_platform_firmware.md) |

### 카드 9 · "How do you prove config storage survives power loss?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "I cut power at every stage of the write, thousands of times, and require that the device always boots with either the old or the new config — never garbage." |
| 약속 3개 | 원자적 갱신(A/B 슬롯 + CRC + 시퀀스) · 부팅 성공 · 데이터 손실 범위 |
| 리그 핵심 | 릴레이 제어 PSU + **GPIO 마커로 쓰기 단계와 동기화된 차단** + 부팅 후 검증 |
| 숫자 하나 | 수천~수만 사이클 [추정], 무작위가 아니라 단계별로 |
| 함정 | 무작위 타이밍 차단만 → 위험한 창(window)을 거의 못 맞힘 |
| 노트 | [S05](2026-10-03_S05_platform_firmware.md) · [onsitePrep N06 config 문제](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.html) |

## 6. Embedded Linux & autonomy

### 카드 10 · "Design a test system for our companion computer platform."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "Three benches, each proving one promise: it always boots, it meets its timing, and it behaves the same on the same input." |
| 약속 3개 | 부팅/OTA/롤백 · 지연과 시간 동기 · 결정적 재생 |
| 리그 핵심 | 전원 스위치 + 시리얼 콘솔 보드 팜, GPIO + LA 지연 측정, PPS 비교, MCAP/rosbag 재생 |
| 숫자 하나 | 야간 수백 회 부팅 사이클, 단계별 전원 차단 [추정] |
| 함정 | 시스템이 자기 지연을 스스로 측정 → **외부 LA**로 독립 측정 |
| 노트 | [S06](2026-10-03_S06_embedded_linux_autonomy.md) |

### 카드 11 · "How would you test A/B OTA with rollback?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "The invariant is simple: after any interruption, the device boots a valid, signed image — and never silently rolls back to a vulnerable one." |
| 약속 3개 | 중단된 업데이트 복구 · 새 이미지 health check 실패 시 롤백 · 서명/버전 롤백 방지 |
| 리그 핵심 | 보드 여러 대 + 단계별 전원 차단 + 손상/무서명 이미지 주입 |
| 숫자 하나 | 업데이트 단계 수 × 차단 지점 × 반복 [추정] |
| 함정 | 행복 경로만 테스트, 다운그레이드 경로를 잊음 |
| 노트 | [S06](2026-10-03_S06_embedded_linux_autonomy.md) · [S05](2026-10-03_S05_platform_firmware.md) |

## 7. Manufacturing

### 카드 12 · "Design a factory test system for our flight controller and drone."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "I'd split the line into stations by what each one can physically observe, and let the station — not the device — own pass/fail." |
| 약속 3개 | 조립 결함 검출 · 유닛별 캘리브레이션 · 추적성(시리얼 → 모든 측정값) |
| 리그 핵심 | pogo 보드 테스트 → 차폐 RF 스테이션 → 턴테이블 IMU 캘리브 → EOL 시스템 체크 |
| 숫자 하나 | 주 ~1,200대 [확인됨 컨텍스트] 1교대 기준 takt ≈ 97초/대 [추정 계산] |
| 함정 | 스테이션 자체 고장을 고려 안 함 → golden unit, known-bad 세트, GR&R |
| 노트 | [S07](2026-10-03_S07_manufacturing_factory_test.md) · [onsitePrep N08](../../onsitePrep/site/notes/2026-10-01_N08_factory_test_firmware.html) |

### 카드 13 · "How many test stations do we need?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "Start from takt: available seconds per shift divided by units per shift, then compare each station's cycle time." |
| 약속 3개 | 병목 스테이션 식별 · 수율과 재테스트 반영 · 증설 순서 |
| 리그 핵심 | 스테이션별 cycle time 측정, multi-up 픽스처 |
| 숫자 하나 | 필요 수 = cycle time ÷ takt × (1 + 재테스트율), 올림 |
| 함정 | 재테스트 · 픽스처 교체 · 작업자 시간을 빼먹음 |
| 노트 | [S07](2026-10-03_S07_manufacturing_factory_test.md) 5절 |

## 8. System integration & release

### 카드 14 · "Design how we test and release a full drone + ground system."

| 항목 | 내용 |
|---|---|
| 첫 대사 | "I'd treat a release as a bundle with one manifest, and attach that manifest to every test result." |
| 약속 3개 | 팀 경계의 호환성 · 시스템 지연과 failsafe · 릴리즈 게이트 통과 근거 |
| 리그 핵심 | iron bird(전 기체 전자장비 + 감쇠기 + PSU) + 계약 테스트(녹화 트래픽 재생) + 비행 로그 자동 분석 |
| 숫자 하나 | 버전 매트릭스는 전수 대신 pairwise, RC에 8시간 soak [추정] |
| 함정 | 비행 시험을 회귀 테스트로 씀 → 비행에서 찾은 버그는 **가장 싼 레벨로 내려서** 회귀 테스트화 |
| 노트 | [S08](2026-10-03_S08_system_integration_release.md) |

### 카드 15 · "A bug only reproduces in flight. What do you do?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "First get the evidence off the aircraft — blackbox, link logs, versions — then try to reproduce it at the cheapest level that can show it." |
| 약속 3개 | 증거 확보 · 재현 레벨 결정(SITL → HITL → iron bird → tethered) · 회귀 테스트로 고정 |
| 리그 핵심 | 비행 로그를 SITL/HITL 자극으로 재생, 진동 · 전원 · 링크 조건을 하나씩 추가 |
| 숫자 하나 | 재현율(몇 번 중 몇 번) — 수정 확인 기준 |
| 함정 | 수정했다는 증거 없이 닫음 → 같은 조건에서 N회 무재현 기준 |
| 노트 | [S01](2026-10-03_S01_flight_software.md) · [S08](2026-10-03_S08_system_integration_release.md) · [FTE N08 디버깅 시나리오](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N08_test_and_debug_scenarios.html) |

### 카드 16 · "You're the second test engineer here. What do you build in your first 90 days?"

| 항목 | 내용 |
|---|---|
| 첫 대사 | "Whatever removes the most risk per week of effort — I'd ask where escapes are coming from today, then start with one HITL rig and a results pipeline everyone can see." |
| 약속 3개 | 30일: 리그 1대 + PR 스모크 · 60일: 결과 대시보드 + flaky 추적 · 90일: factory test FW 첫 스테이션 또는 iron bird |
| 리그 핵심 | 재사용 가능한 driver/fixture 계층 (리그 2대째부터 속도가 붙게) |
| 숫자 하나 | 테스트 FW 팀 2명 → 연내 최대 5명 [확인됨 10-01 Michael] |
| 함정 | 처음부터 완벽한 프레임워크 → 먼저 하나를 끝까지 돌리고 확장 |
| 노트 | [S08](2026-10-03_S08_system_integration_release.md) 팀 확장 · [N04](../notes/2026-10-02_N04_system_design_test_tooling.md) |

## 체크

- [ ] 카드 1 · 14 · 12를 각각 10분 타이머로 (가장 나올 법한 셋)
- [ ] 무작위 2장 × 매일 (10-04 ~ 10-07)
- [ ] 모든 카드에서 첫 1분 안에 요구사항 질문 2개 이상
- [ ] 카드 16 답을 Don 자신의 말로 다시 쓰기 (역질문으로도 쓸 수 있다)

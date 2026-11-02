# BV14 · 보드 검증 계획 · 보드 팜 · 자동화 · HW/SW 버그 가르기
<!-- theme: pastel -->

> **묶음**: F 디버그 · 계획 · 실전 · **우선**: P0 ★ Staff 시그널 최다 ("Write a validation plan for this board", "Automate 50 boards nightly") · **선행 노트**: [BV13 디버그 시나리오](2026-11-01_BV13_board_debug_scenarios.html), [BV12 SI/PI·shmoo·PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html), [BV10 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html), [BV11 랩 장비](2026-11-01_BV11_lab_instruments_measurement.html) · **이전 노트**: [BV13](2026-11-01_BV13_board_debug_scenarios.html) · **다음 노트**: [BV15 자동차 보드](2026-11-01_BV15_automotive_board_considerations.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), PCIe 쪽 짝 노트 [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html) (12영역 PCIe 플랜·rule of three·하네스), [LP15](2026-10-31_LP15_debug_scenarios.html), [deck §6 검증 설계](2026-10-25_rivian_hm_screen_prep.html) · **코드**: [farm_orchestrator.py](onsitePrep/bv14/farm_orchestrator.py) · [inventory.json](onsitePrep/bv14/inventory.json) · [run_all.sh](onsitePrep/bv14/run_all.sh) · [run_log.txt](onsitePrep/bv14/outputs/run_log.txt) · [summary.txt](onsitePrep/bv14/outputs/summary.txt)

## 0. TL;DR

- 이 노트로 답할 것: "이 보드 검증 계획을 써 봐라", "3주밖에 없으면?", "보드 몇 장, 왜?", "끝났다는 걸 어떻게 아나", "50장 nightly 자동화 설계", "200번에 1번 실패하면", "팜을 어떻게 건강하게 유지하나", "HW냐 SW냐 어떻게 가르고 HW 팀을 어떻게 설득하나", "EVT/DVT/PVT·ECO 때 회귀 범위".
- **외울 것 ① 계획 = 9칸**: 범위 → **요구사항 추적(spec ID ↔ test ID ↔ 결과)** → 테스트 매트릭스(기능·인터페이스·전원·클럭·리셋·성능·PVT·스트레스·soak) → 조건(V·T·보드·칩 corner) → **샘플 수와 이유** → **숫자로 된 통과 기준** → 일정·우선순위 → 리스크 → **종료 기준**.
- **외울 것 ② 3주면 risk-based**: "새 IP·처음 쓰는 기능·다른 팀을 막는 것·안전/고객 영향"이 P0. 재사용된 성숙 IP는 smoke만. **안 하는 것을 명시하고 리스크를 합의**하는 것이 Staff 시그널.
- **외울 것 ③ 팜 = 보드마다 4개 손잡이 + DB**: 원격 전원(PDU/relay) · 콘솔(UART server) · JTAG 프로브 · boot-mode 제어 + 인벤토리(SN·rev·rework·칩 lot/corner). 결과는 전부 조건 태그와 함께 DB로, **retry는 기록하고 flaky는 PASS가 아니다**, 실패 순간 **재현 패키지** 자동 생성, 망가진 보드는 **격리(quarantine)** 해서 통계 오염 방지.
- **외울 것 ④ HW/SW triage = 무엇을 따라가나**: 재현률 → 다른 보드/칩(swap) → 다른 FW(bisect) → 조건 스윕 → 측정 증거 → 리포트. 설득은 **판정 기준을 먼저 합의**하고 데이터로.
- 실습: [farm_orchestrator.py](onsitePrep/bv14/farm_orchestrator.py) — mock PDU·챔버·UART 로그 생성기 + 로그 파서 + 타임아웃 hang 감지 + retry/flaky 분류 + 상관관계 triage 힌트 + JSON/표 + 재현 패키지, 8장 mock 인벤토리, `python3 -I`로 실제 실행 (§6).
- **30초 영어 (헤드라인 "Write a validation plan for this new board")**: "I'd start from the requirements — the SoC and board specs, the interfaces, the power and thermal envelope — and give each one an ID that maps to tests, so coverage is measurable. The test matrix covers functional, interface, power, clock and reset, performance, PVT corners, stress and soak, each with numeric pass criteria and the conditions it runs at. Sample size comes from what I need to conclude: several boards across chip corners so board-to-board variation shows, and enough cycles per test that a zero-failure result actually bounds the failure rate. Then I prioritize by risk — new IP and anything that blocks other teams first — and say explicitly what's out of scope. Execution is automated on a board farm with remote power, consoles, JTAG and boot control, results tagged with board, chip and conditions in a database, and the exit criteria are agreed up front: all P0 requirements executed and passing, no open P0 bugs without a workaround, soak hours met, and a stable nightly regression."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "학교 시험 출제 + 무인 시험장"

- **검증 계획 = 시험 출제 계획서**. 선생님이 시험을 내기 전에 ① **시험 범위**(어느 단원까지 = 보드의 어느 블록까지) ② **출제 기준표**(교과서 학습 목표 하나하나가 몇 번 문제로 확인되나 = 요구사항 ↔ 테스트 **추적성**) ③ **문제 유형표**(객관식·서술형·실기 = 기능·인터페이스·전원·성능·스트레스) ④ **응시 조건**(시간·장소 = 온도·전압·보드 수) ⑤ **합격선**(60점 = 숫자로 된 pass criteria) ⑥ **시험 일정**을 정한다. 출제 기준표가 없으면 "어떤 단원을 안 물어봤는지" 아무도 모른다.
- **보드 팜 = 무인 시험장**. 수십 명(보드)이 매일 밤 같은 시험을 본다. 감독관(오케스트레이터)이 **전원을 켜 주고**(PDU), **답안지를 걷고**(UART 로그), **문제가 생기면 깨우고**(JTAG·전원 사이클), **출석부**(인벤토리: 누가 어느 반·어느 학년·전학 이력)를 관리한다. 답안이 이상하게 비어 있으면(hang) "시간 초과"로 기록하고 다음 문제로.
- **flaky test = 채점이 들쭉날쭉한 문제**. 같은 학생이 같은 답을 써도 어떤 날은 맞고 어떤 날은 틀린다 → 문제(테스트)가 나쁜 건지 학생(보드)이 가끔 실수하는 건지 따로 조사해야 하고, **"재시험에서 맞았으니 100점"으로 처리하면 안 된다**.
- **HW/SW triage = 오답이 학생 탓인지 문제 탓인지 가르기**. 다른 학생(다른 보드)도 틀리나, 작년 시험지(이전 FW)로도 틀리나, 조용한 교실(상온)에서도 틀리나 — 따라가는 것을 찾는다 ([BV13 §2.3](2026-11-01_BV13_board_debug_scenarios.html)).

### 1.2 그림 — 계획에서 팜, 팜에서 결론까지

```text
 ┌──────────── 계획 (사람이 씀, 리뷰 받음) ────────────┐     ┌──────────── 실행 (자동, 매일 밤) ──────────────────────┐
 │ 요구사항 REQ-xxx  ──▶  테스트 TC-yyy  ──▶ 조건·N·기준 │ ──▶ │ 스케줄러 → 보드 예약 → 전원/부팅/헬스 체크 → 테스트 실행 │
 │ (spec, datasheet,       (기능/IF/전원/        (V,T,보드, │     │  → 로그 파싱 → PASS/FAIL/HANG/FLAKY → 실패 시 재현 패키지│
 │  board spec)            PVT/stress/soak)      corner)    │     └───────────────────────────┬────────────────────────┘
 └──────────────────────────────────────────────────────┘                                 ▼
                                                                 ┌──────────── 결론 (사람 + 데이터) ─────────────────────┐
           ◀──── 커버리지·종료 기준 피드백 ─────────────────────── │ 결과 DB → 대시보드(요구사항별 상태, 추세, flaky 목록)    │
                                                                 │ → triage(무엇을 따라가나) → 버그 → HW/SW/silicon 담당  │
                                                                 │ → 수정 → 회귀 테스트 추가 → 종료 기준 충족?              │
                                                                 └──────────────────────────────────────────────────────┘
```

### 1.3 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| validation plan | Validation Plan | 무엇을·어떤 조건에서·몇 장으로·어떤 기준으로 검증할지 쓴 문서 | 면접 설계 질문의 본체 |
| requirement | Requirement | "VDD_CORE는 ±3% 이내" 같은 검증 가능한 문장 하나 (ID 부여) | 테스트의 출발점 |
| traceability | Requirement traceability (matrix, RTM) | 요구사항 ↔ 테스트 ↔ 결과를 연결한 표 | "무엇을 안 했나"가 보임 = 커버리지 |
| test matrix | Test matrix | 테스트 × 조건(온도·전압·보드) 격자 | 조합 폭발 관리 |
| pass criteria | Pass/fail criteria | 숫자로 된 합격선 (값·횟수·신뢰도) | "잘 되면"은 기준이 아님 |
| PVT (특성화) | Process, Voltage, Temperature | 칩 공정 corner × 전압 × 온도 끝 조건 | 마진 확인 (BV12) |
| corner (TT/FF/SS) | Typical / Fast / Slow process corner | 일부러 빠르게/느리게 만든 칩 샘플 | 칩 편차를 대표 |
| stress / soak | Stress test / Soak test | 최대 부하 / 장시간 (12–72 h+) 연속 실행 | 시간이 쌓여야 나오는 버그 |
| board farm | Board farm / test rack | 원격 제어 가능한 보드 여러 장을 모아 둔 랙 | 사람 없이 매일 회귀 |
| PDU | Power Distribution Unit (network/switched) | 콘센트별로 네트워크에서 켜고 끌 수 있는 멀티탭 | 원격 전원 사이클 |
| relay board | Relay board | GPIO로 접점을 붙였다 떼는 스위치 보드 | 전원·리셋 버튼·boot-mode 핀 제어 |
| console server | Console / UART server (ser2net 등) | 보드 UART를 네트워크 포트로 노출 | 원격 로그 수집 |
| JTAG probe pool | (프로브 공유) | 디버그 프로브 여러 개를 팜 보드에 고정 배정 | hang 시 상태 덤프 |
| boot-mode control | (부트 모드 제어) | strap/스위치를 릴레이로 바꿔 복구 모드 진입 | 벽돌 보드 원격 복구 |
| inventory | Board inventory DB | SN·rev·BOM alt·rework·칩 lot/corner·슬롯 | 실패 상관관계 분석 |
| orchestrator | Test orchestrator / harness | 예약·전원·실행·파싱·기록을 자동으로 하는 프로그램 | 이 노트의 코드 |
| SCPI | Standard Commands for Programmable Instruments | 계측기 공통 텍스트 명령 (`*IDN?`, `MEAS:VOLT?`) | 장비 자동화 (PyVISA) |
| VISA / PyVISA | Virtual Instrument Software Architecture / Python 바인딩 | 계측기와 USB·LAN·GPIB로 통신하는 표준 API | 측정을 코드로 |
| nightly regression | Nightly regression | 매일 밤 새 빌드로 전체(또는 대표) 테스트 | 회귀를 하루 안에 발견 |
| flaky test | Flaky test | 같은 조건에서 PASS/FAIL이 바뀌는 테스트 | 신뢰를 갉아먹음 [6] |
| quarantine | Quarantine | 고장 보드·불안정 테스트를 통계에서 격리 | 결과 오염 방지 |
| repro package | Reproduction package | 로그·덤프·조건·명령을 묶은 재현 꾸러미 | 다른 팀이 질문 없이 재현 |
| signature | Failure signature | 실패를 구분하는 안정 문자열 (테스트 + 코드) | dedup·추세 |
| EVT/DVT/PVT (보드) | Engineering / Design / Production Validation (or Verification) Test | 보드 빌드 단계 이름 (회사마다 풀네임 다름) [3] | 단계별 검증 범위가 다름 |
| ECO | Engineering Change Order | 보드 설계 변경 지시 | 회귀 범위 결정 |
| errata | Errata | 알려진 silicon 버그 + workaround 목록 | silicon 결론의 출구 |
| MTBF | Mean Time Between Failures | 평균 고장 간격 (soak에서 하한 추정) | 신뢰성 지표 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 검증 계획의 구조 (문서 목차)

| # | 섹션 | 담는 것 | 면접에서 한 문장 |
|---|---|---|---|
| 1 | 범위 (scope) | 대상 보드·rev·SoC stepping, **포함/제외** 블록, 다른 팀 범위 경계 (DV·SI/PI·compliance·safety) | "In scope, out of scope, and who owns the boundary." |
| 2 | 참조 | SoC 데이터시트/TRM, 보드 회로도·스펙, 인터페이스 표준, errata | "Every requirement points to a source document." |
| 3 | 요구사항 목록 + 추적성 | REQ ID · 출처 · 검증 방법(test/inspection/analysis) · TC ID · 우선순위 | "Coverage is requirements executed over requirements total." |
| 4 | 테스트 매트릭스 | 카테고리별 테스트 + 조건 격자 (§2.3) | "Functional through soak, each with conditions." |
| 5 | 조건 · 샘플 수 | V·T·F 코너, 보드 수, 칩 corner, 반복 횟수 + **왜** (§2.4) | "Sample size comes from the conclusion I need." |
| 6 | 통과 기준 | 숫자: 값 범위, 반복 N·실패 0, 신뢰도, 에러율 상한 (§2.5) | "Numbers, not adjectives." |
| 7 | 환경 · 도구 | 팜 구성, 장비, 챔버, FW 빌드 정책 | "Same harness for every board." |
| 8 | 일정 · 우선순위 | 단계·주차·의존성, P0/P1/P2 (§2.6) | "Risk-based, and blockers first." |
| 9 | 리스크 · 완화 | 보드/장비 부족, FW 지연, errata, rev 변경 (§2.7) | "Each risk has an owner and a fallback." |
| 10 | 보고 · 종료 기준 | 대시보드, 주간 상태, exit criteria (§2.8) | "Done is agreed before we start." |

### 2.2 요구사항 추적성 (spec ↔ test ↔ 결과)

- 규칙: 요구사항 1개 = **검증 가능한 한 문장** + ID + 출처 + 검증 방법. 테스트 1개는 여러 요구사항을 덮을 수 있고, 요구사항 1개는 여러 테스트(조건별)로 덮일 수 있다.
- 커버리지 = (실행된 요구사항 / 전체) 와 (통과한 요구사항 / 전체)를 **따로** 보고. 테스트 개수는 커버리지가 아니다.
- 역방향 추적: **ECO·부품 변경 → 영향 net → 영향 요구사항 → 재실행할 테스트** (§2.13). 추적성이 있으면 회귀 범위를 "느낌"이 아니라 표로 정한다.

| REQ ID | 요구사항 (출처) | 검증 방법 | TC ID | 우선 |
|---|---|---|---|---|
| REQ-PWR-003 | VDD_CORE 0.75 V ±3% (정상 부하~최대 부하) (SoC DS §전기 특성) [추정 값] | test | TC-PWR-003a (PMIC telemetry), TC-PWR-003b (scope droop) | P0 |
| REQ-RST-001 | PORn은 마지막 rail PGOOD 후 ≥ X ms에 해제 (SoC DS 타이밍) | test (scope) | TC-RST-001 | P0 |
| REQ-DDR-002 | LPDDR5 6400 MT/s 2ch에서 March C- 무오류, −40/25/105 °C | test | TC-DDR-002 × 3 corners | P0 |
| REQ-I2C-001 | 회로도의 모든 I2C 장치가 기대 주소에서 ACK | test | TC-I2C-001 (rev별 기대 목록) | P0 |
| REQ-ETH-004 | 1000BASE-T1 링크 업 < X s, PRBS BER < 1e-10 [추정 기준] | test | TC-ETH-004 | P1 |
| REQ-BRD-010 | 모든 rail에 측정용 테스트 포인트 존재 | inspection | (회로도 리뷰) | P1 |

### 2.3 테스트 매트릭스 — 카테고리

| 카테고리 | 무엇을 | 대표 테스트 | 통과 기준 예 | 관련 노트 |
|---|---|---|---|---|
| 기능 (functional) | 블록이 설계대로 동작 | 레지스터 reset 값/RW, 타이머, 인터럽트, DMA, NPU 레퍼런스 추론 결과 비교 | 전 항목 PASS, golden 출력과 bit-exact | [BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html) |
| 인터페이스 | 보드 위 모든 연결 | I2C 기대 주소 스캔, SPI flash JEDEC ID + RW, UART loopback, Ethernet PHY ID → loopback → PRBS, PCIe 링크/열거, CSI-2 test pattern + CRC 카운터 | 연결 100%, BER 상한, CRC 0 | [BV10](2026-11-01_BV10_board_interface_tests.html), [LP14](2026-10-31_LP14_validation_methodology.html) |
| 전원 | rail·순서·droop·전력 | rail 정확도, 시퀀스 타이밍, 부하 계단 droop, 모드별 전력 | 값 ± 허용오차, 순서·간격 spec | [BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html) |
| 클럭 | osc·PLL·refclk | 주파수 ppm, PLL lock 시간, refclk jitter | ppm·jitter spec | [BV04](2026-11-01_BV04_clocks_osc_pll_refclk.html) |
| 리셋 | 모든 리셋 경로 | cold/pin/WDT/SW 리셋 × N, strap 래치값, reset reason | N회 무실패, 래치 = 설계 | [BV05](2026-11-01_BV05_reset_strap_boot_mode.html) |
| 성능 | 목표 대비 % | DDR 대역폭, PCIe throughput, NPU TOPS·latency, 카메라 프레임 레이트 | 이론/목표의 X% | [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html) |
| PVT / 마진 | 끝 조건에서 동작 + 여유량 | V×T shmoo, 고온/저온 memtest·링크, training eye, lane margining | 목표 코너 + 마진 ≥ 목표 | [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) |
| 스트레스 | 최대 동시 부하 | NPU+DDR+카메라+PCIe 동시, DVFS 전환 10^5회, 전원 사이클 10^3회 | hang 0, 에러 0 (N·시간 명시) | [BV13 S10](2026-11-01_BV13_board_debug_scenarios.html) |
| Soak | 장시간 | 24–72 h 혼합 부하 + 온도 프로파일 | 실패 0 / 누적 device-hours → MTBF 하한 | §2.14 |

### 2.4 조건과 샘플 수 — "몇 장, 왜"

- **왜 1장으로 안 되나**: 1장에 대한 1,000회는 **그 보드**에 대한 결론이다. 보드 편차(조립·부품), 칩 편차(corner·lot)는 **다른 개체**로만 보인다 → 층화(stratify): 칩 corner × 보드 × 조건.
- **통계 규칙** (LP14 §2.5 그대로): 실패 0 / N회 → 95% 상한 ≈ 3/N (rule of three). "1/1,000 미만"을 말하려면 ≈ 3,000회 무실패. 보드 여러 장에 나눠 돌려도 되지만, 보드별로 따로도 본다(특정 보드에 몰린 실패는 "율"이 아니라 "불량 보드").
- **soak도 같은 식**: T device-hours 동안 고장 0 → 고장률 95% 상한 ≈ 3/T → **MTBF 하한 ≈ T/3** (Poisson 근사, 일정 고장률 가정 [추정: 근사]).
- 단계별 전형적 규모 [추정: 회사·프로그램마다 다름, 면접에선 "근거"를 말하는 것이 핵심]:

| 단계 | 보드 수 | 칩 | 왜 그 숫자인가 |
|---|---|---|---|
| bring-up | 3–5 | TT 위주 | 1장은 망가질 수 있고, 2장 이상이어야 "이 보드만?"을 판단, 병렬 디버그 |
| functional validation | 8–20 | TT + 일부 FF/SS | 보드 편차가 보이고, 팜 병렬로 회귀 시간을 줄임 |
| PVT 특성화 | corner당 ≥ 3칩 (TT/FF/SS) × 보드 | skew lot | corner 대표값 + 칩 간 분산의 최소 추정 |
| 비교 기준: 부품 qualification (칩 벤더) | AEC-Q100 HTOL 등: **3 lot × 77개 = 231개, 0 fail** [5] | 양산 lot | 통계적 신뢰성 — **보드 검증과 목적이 다름** (그래서 숫자도 다르다) |

- 조건 격자 줄이기: 전 조합(테스트 × V 3 × T 3 × 보드 10)은 폭발 → ① 기능은 상온·nominal 전 보드 ② 마진에 민감한 것(DDR·SerDes·PLL)만 V×T 전체 ③ 코너는 최악 조합(고온+저전압+SS, 저온+고전압+FF) 우선 ④ 나머지는 대표 보드.

### 2.5 통과 기준 쓰는 법

| 나쁜 기준 | 좋은 기준 |
|---|---|
| "DDR이 안정적으로 동작" | "LPDDR5 6400, March C- + random pattern 4 GiB 전 영역, −40/25/105 °C 각 보드 5장 × 20회, 오류 0, ECC corrected 0" |
| "링크가 잘 뜸" | "cold boot 3,000회 (보드 6장 분산), 매회 기대 speed/width, 링크 업 < X ms, 실패 0 → 95% 상한 1e-3" |
| "전원 정상" | "VDD_CORE 평균 0.75 V ±3%, 부하 계단 0→X A에서 droop < 4% / < 10 µs, 순서 = 표, 간격 ≥ 데이터시트 최소" |
| "카메라 됨" | "8채널 동시 test pattern 1 h, 프레임 드롭 0, CSI-2 CRC/ECC 에러 0, 프레임 레이트 ±0.1%" |

- 기준에는 항상 **값 + 조건 + 반복 횟수 + 판정(0 fail / 상한)** 이 들어간다. 그리고 **측정 방법**(어느 장비·어느 점)도 — 같은 ripple도 프로브 방식에 따라 10배 다르게 나온다 ([BV11](2026-11-01_BV11_lab_instruments_measurement.html)).

### 2.6 3주 제약에서 우선순위·일정

- **우선순위 점수** = (실패 가능성) × (영향: 안전·고객·다른 팀 차단) × (발견이 늦으면 드는 비용). 새 IP·첫 실리콘 블록·보드 신규 회로·다른 팀(드라이버·알고리즘)이 기다리는 기능이 위로. 재사용된 성숙 IP(예: 이전 칩과 같은 UART)는 smoke만.
- **자동화부터 깐다**: 1주차에 팜(전원·콘솔·부팅·파서)을 먼저 세우면 2–3주차가 밤에도 돈다. 수동 테스트로 3주를 쓰면 회귀가 불가능.
- 명시적으로 **안 하는 것** 목록 + 리스크 수용자(PM/리드) 서명. "모든 것을 조금씩"보다 "P0를 깊게".

| 주 | 목표 | 내용 | 보드 | 산출물 |
|---|---|---|---|---|
| 1 | bring-up 완료 + 팜 가동 | Day-0 체크리스트(BV06), 전원·클럭·리셋·JTAG·부트·DDR P0, 팜: PDU·콘솔·boot-mode·파서·DB, smoke suite nightly | 3–5 | bring-up 로그, smoke 결과, 팜 v1 |
| 2 | 기능·인터페이스 P0 전체 자동화 | 인터페이스 매트릭스(I2C/SPI/ETH/PCIe/CSI-2), 리셋 매트릭스, 성능 1차, 고온/저온 P0 (챔버 1대에 보드 2–3장) | 8–10 | 요구사항 커버리지 1차, 버그 목록 |
| 3 | 마진·스트레스·soak + 종료 판단 | V×T shmoo(DDR·SerDes), 스트레스 프로파일, 48 h soak, flaky 정리, 회귀 안정화, 보고 | 8–10 + 챔버 | 검증 보고서, 리스크·errata 목록, P1/P2 계획 |

### 2.7 리스크 · 완화

| 리스크 | 영향 | 완화 |
|---|---|---|
| 보드 수 부족 / 보드 고장 | 통계·병렬성 감소 | 보드 배정 우선순위(P0 테스트 전용 보드), golden board 1장은 실험 금지, 입고 즉시 스크린 |
| 챔버·장비 경합 | PVT 지연 | 챔버 예약표, 대표 보드만 챔버, 국부 thermal forcer 대안 |
| FW/드라이버 미완성 | 테스트 실행 불가 | bare-metal 테스트 FW 우선(FPGA에서 미리 검증), 의존성 표 |
| silicon errata로 블록 차단 | 해당 영역 중단 | workaround 빌드 플래그, errata ID를 결과에 태그, 우회 불가 시 범위 조정 합의 |
| 팜 인프라 불안정 (콘솔·PDU·프로브) | 가짜 실패 → 신뢰 상실 | 헬스 체크, quarantine, 인프라 실패를 테스트 실패와 분리 집계 |
| 중간에 rev 변경(ECO) | 결과 무효화 위험 | 결과에 rev 태그, 추적성 기반 영향 테스트만 재실행 (§2.13) |
| flaky 테스트 증가 | 진짜 실패를 묻음 | flaky 정책 (§2.11), 담당자·기한 |

### 2.8 "끝났다는 걸 어떻게 아나" — 종료 기준 (시작 전에 합의)

1. **P0 요구사항 100% 실행, 100% PASS** (또는 PASS가 아니면 **승인된 waiver/errata + workaround** 존재).
2. P1 요구사항 ≥ 합의된 % 실행, 미실행 항목은 리스크 목록에.
3. **열린 P0 버그 0**, P1 버그는 원인 파악 + 계획.
4. PVT: 목표 코너 전부 실행, 마진 ≥ 목표 (또는 spec 조정 합의).
5. Soak/스트레스: 누적 device-hours ≥ 목표, 실패 0 (또는 원인 파악).
6. **회귀 안정성**: 마지막 N(예: 5) nightly에서 새 실패 0, flaky 비율 < 임계, 인프라 가용률 ≥ 목표.
7. 보고서: 결과·커버리지·마진·남은 리스크·errata — 리뷰 완료.

- 핵심 문장: "Done isn't 'I ran out of time' — it's exit criteria agreed before we start, and when time runs out first, I report exactly which criteria aren't met and what the residual risk is."

### 2.9 Worked example — "새 자동차 AI SoC 검증 보드" 계획 (전부 `[추정]`: Rivian 실제 구성 미확인)

**보드 가정** [추정]: SoC (멀티코어 Arm + NPU + ISP), LPDDR5 2ch, 다채널 PMIC + 자동차 SBC(system basis chip), 12 V 차량 입력, 카메라 8채널 (SerDes 디시리얼라이저 → MIPI CSI-2), 1000BASE-T1 자동차 Ethernet PHY 2개, PCIe Gen4 x4 (NVMe 또는 두 번째 SoC), QSPI NOR 부트 + UFS, CAN-FD, I2C 센서(온도·전류 모니터), JTAG·디버그 UART, 소켓 버전 보드 일부.

**범위**

| 포함 | 제외 (다른 팀/단계) |
|---|---|
| 보드 전원·클럭·리셋·부트, LPDDR5, 모든 보드 인터페이스, 성능 1차, PVT(−40/25/105 °C), 스트레스·soak | 전기 컴플라이언스 측정(SI 팀), EMC(CISPR 25 — 별도 랩), 기능 안전 분석(safety 팀, 단 safety mechanism 테스트는 포함), 양산 SLT 프로그램(다음 단계) |

**테스트 매트릭스 (발췌)**

| TC | 카테고리 | 테스트 | 조건 | 보드 × 반복 | 통과 기준 | 우선 |
|---|---|---|---|---|---|---|
| TC-PWR-001 | 전원 | rail 정확도 (PMIC telemetry + DMM SCPI 교차) | 입력 9/12/16 V × −40/25/105 °C | 5 × 3 | 전 rail ±3% | P0 |
| TC-PWR-004 | 전원 | NPU 부하 계단 droop | 25/105 °C, Vin 9 V | 3 × 100 계단 | droop < 4%, 리셋 0 | P0 |
| TC-PWR-008 | 전원 (자동차) | cold crank 프로파일 입력 (ISO 16750 계열) [추정] | 프로그래머블 PSU | 3 × 50 | 리셋 없음 또는 규정 동작 | P1 |
| TC-RST-002 | 리셋 | cold/pin/WDT/SW 리셋 매트릭스 | 25 °C | 6 × 1,000 각 | 실패 0 (95% 상한 3e-3/보드 6장 합계 5e-4) | P0 |
| TC-BOOT-001 | 부트 | strap 래치값 + 부트 시간 | 3 온도 | 전 보드 × 100 | 래치 = 설계, 부트 < X ms | P0 |
| TC-DDR-002 | DDR | training eye 기록 + March C- 4 GiB | −40/25/105 °C × VDDQ ±3% | 5 (TT/FF/SS 포함) × 20 | 오류 0, eye ≥ 목표 | P0 |
| TC-CAM-003 | CSI-2 | 8채널 동시 test pattern 1 h | 25/105 °C | 3 × 3 | 드롭 0, CRC 0 | P0 |
| TC-ETH-004 | Ethernet | 1000BASE-T1 링크 루프 + PRBS | 3 온도 | 4 × 500 링크 / BER 측정 | 링크 실패 0, BER 상한 | P1 |
| TC-PCIE-001 | PCIe | 링크 루프 (LP14 영역 1·2·4) | 3 온도 | 4 × 3,000 | LP14 기준 | P0 |
| TC-NPU-002 | 기능·성능 | 레퍼런스 모델 추론 bit-exact + 처리량 | 25/105 °C, DVFS 각 OPP | 5 × 1,000 inferences | bit-exact, 처리량 ≥ 목표 X% | P0 |
| TC-SAFE-001 | 안전 메커니즘 | ECC 주입·WDT 만료·온도 경보 경로 | 25 °C | 3 × 100 | 검출·보고·복구 100% | P1 |
| TC-STR-001 | 스트레스 | NPU+DDR+CAM+PCIe 동시, 온도 사이클 | −40↔105 °C ramp | 6 × 48 h | hang 0, 에러 0 | P0 |

**일정** — §2.6 표 그대로 + 의존성: TC-NPU는 NPU 드라이버(다른 팀) 2주차 중반 → 그 전엔 bare-metal 레퍼런스 커널로 대체. 챔버 1대 → 2주차 후반부터 상시 점유, 보드 3장 고정.

### 2.10 보드 팜 설계

```text
                         ┌──────────────── Lab network (격리 VLAN) ───────────────┐
  CI / scheduler ──────► │ Orchestrator host(s)  ── results DB ── dashboard        │
  (빌드 hash, 테스트 셋) │   │  reservation/lock (보드 단위)                        │
                         │   ├─ PDU / relay ctrl ───────────────┐ (SNMP/HTTP/CLI)  │
                         │   ├─ console server (ser2net) ───────┤ (TCP per UART)   │
                         │   ├─ JTAG probe pool (OpenOCD/T32) ──┤ (USB/LAN)        │
                         │   ├─ GPIO/relay: boot-mode, reset btn┤                  │
                         │   ├─ instruments: PSU·DMM·scope ─────┤ (SCPI / PyVISA)  │
                         │   └─ chamber ctrl ───────────────────┤ (SCPI/Modbus)    │
                         └──────────────────────────────────────┼──────────────────┘
                                                                ▼
     ┌─────── Rack slot k ─────────────────────────────────────────────────────────────┐
     │ DUT board (SN, rev, rework, SoC lot/corner)                                      │
     │   PWR ◄ PDU outlet k   UART ◄ console port k   JTAG ◄ probe k   BOOT ◄ relay k    │
     │   ETH (DUT 네트워크, 이미지 다운로드/SSH)   USB (복구 다운로드)   온도 센서 / 열전대│
     └──────────────────────────────────────────────────────────────────────────────────┘
```

| 구성요소 | 역할 | 설계 포인트 |
|---|---|---|
| 원격 전원 (PDU / relay) | 하드 전원 사이클 = 모든 복구의 최후 수단 | 보드별 독립 outlet, off 시간(방전) 설정, **전류 readback** 가능하면 baseline 비교, API/CLI로 제어 [4] |
| 콘솔 (UART server) | 부트로더·커널·테스트 FW 출력 수집 | SSH와 **독립**(네트워크 죽어도 보임), 보드 ID ↔ 포트 **고정 매핑**, 모든 바이트 타임스탬프 로그 [4] |
| JTAG 프로브 풀 | hang 시 PC·레지스터 덤프, 벽돌 복구, flash 프로그래밍 | 보드당 고정 배정(공유 시 잠금), nightly IDCODE 헬스 체크 |
| boot-mode 제어 | strap/스위치를 릴레이로 → 복구 모드(USB/UART/JTAG boot) | 하나의 "unwedge" 시퀀스: 전원 off → 복구 모드 → 재이미지 → 정상 모드 [4] |
| 장비 (SCPI) | PSU 전압·전류, DMM, scope 캡처 트리거 | PyVISA 추상화 + 장비별 드라이버, 측정값도 결과 DB로 [7] |
| 챔버 | 온도 조건 | set → 안정 → soak 후 테스트, 실제 보드 온도 센서로 확인 |
| 인벤토리 DB | 보드 속성 · 상태 · 이력 | SN·rev·BOM alt·rework·SoC lot/corner·DRAM lot·슬롯·상태(ACTIVE/QUARANTINED/RMA)·누적 전원 사이클 |
| 예약 · 잠금 | 사람과 자동화가 같은 보드를 동시에 안 쓰게 | labgrid의 coordinator가 "place"를 잠그는 방식처럼 [1] |
| 헬스 체크 | 테스트 전 보드·인프라 상태 확인 | 부팅 → 콘솔 "READY" → IDCODE → 기본 rail. 2회 실패 → quarantine + 알림 |

- **기존 오픈소스 참고 설계**: **labgrid** — 하드웨어 접근을 *resource*(수동적 기술: 시리얼 포트·전원 포트)와 *driver*(능동: 전원 on/off/cycle, 콘솔, shell)로 나누고, exporter(보드에 물린 PC)·coordinator(잠금·목록)·client로 분리, *strategy*가 보드를 off → bootloader → shell 상태로 옮긴다 [1]. **LAVA** — 중앙 서버(웹 UI·스케줄링) + dispatcher worker(장치 조작), PDU는 명령줄 훅으로 연결 [2]. 면접에서는 "I'd reuse labgrid- or LAVA-style abstractions rather than invent one" 정도면 충분.

### 2.11 자동화 — 오케스트레이터 · SCPI · DB · nightly · flaky · 재현 패키지

**오케스트레이터 계층** (이 노트의 코드가 같은 구조, §6)

```text
 nightly job(build=g1a2b3c4, suite=board_p0, boards=rev A+B, temps=[25,85])
   └─ for temp: chamber.set_and_soak(temp)
        └─ for board in reserved(boards):
             health_check: power_cycle → expect "SHELL READY" (timeout) → [fail x2 → QUARANTINE, skip]
             for test in suite:
                 attempt = run(test): send "run <test>" → read lines with idle_timeout
                                       → PASS / FAIL(code) / HANG(silent | test_timeout | PANIC | RESET)
                 on non-PASS: power_cycle → retry (≤ R), record every attempt
                 class = PASS | FLAKY | FAIL | HANG | FAIL_INCONSISTENT
                 non-PASS → repro package (uart.log + meta.json + repro cmd)
   └─ results → DB ; triage(signature × inventory attributes) ; report ; exit code for CI
```

**FW ↔ 오케스트레이터 계약** (BV08과 같이 설계): 한 줄 한 판정, 앵커된 정규식 (`T:<test> PASS`, `T:<test> FAIL code=0xNNNN k=v…`), 긴 테스트는 **idle timeout보다 짧은 주기로 progress 줄** (안 그러면 살아 있는 테스트가 hang으로 오판 — §6에서 실제로 재현), 부팅 끝 표식(`SHELL READY`), reset reason 줄.

**SCPI / PyVISA 측정 예시** `[미검증: 장비 없음, 명령은 장비별 매뉴얼 확인]`

```python
import pyvisa                                   # PyVISA 표준 사용법 [7]
rm = pyvisa.ResourceManager()
psu = rm.open_resource("TCPIP0::10.0.0.21::INSTR")   # 자원 문자열 형식은 장비/인터페이스별
psu.read_termination = psu.write_termination = "\n"
print(psu.query("*IDN?"))                       # IEEE 488.2 공통 명령: 장비 식별
psu.write("VOLT 12.0"); psu.write("CURR 1.0"); psu.write("OUTP ON")   # 전형적 SCPI PSU 명령
i_boot = float(psu.query("MEAS:CURR?"))         # 부팅 후 전류 → baseline과 비교해 S1류 이상 감지
```

**결과 DB 스키마 (최소)**

| 테이블 | 열 | 용도 |
|---|---|---|
| run | run_id, build_hash, suite, start/end, host, seed | 하룻밤 단위 |
| board | sn, rev, bom_alt, rework, soc_lot, corner, dram_lot, slot, status | 인벤토리 (조인 대상) |
| result | run_id, sn, test_id, req_ids, temp_c, v_cond, class, attempts, verdicts, signature, detail, duration | 한 줄 = (보드, 조건, 테스트) |
| measurement | run_id, sn, name, value, unit, instrument | 전류·전압·eye 폭·링크 시간 추세 |
| artifact | result_id, path(uart.log, jtag_dump, scope.png), sha | 재현 패키지 |

**대시보드에 보일 것**: 요구사항별 상태(추적성), 보드 × 테스트 격자(오늘), 테스트별 first-pass rate 추세, **새 시그니처**(어제 없던 것) 강조, flaky 목록(담당·기한), 팜 가용률, quarantine 보드.

**nightly 회귀 흐름**: 빌드 → 이미지 배포(JTAG/USB/네트워크) → 헬스 체크 → suite → 결과 비교(전날·마지막 good 빌드, 실패율은 신뢰구간으로) → 새 실패면 **자동 bisect 후보**(빌드 범위) + 티켓 → 아침 리포트.

**flaky 테스트 정책** — Google 사례에서도 pass→fail 전환의 대부분이 flaky 테스트에서 나와 엔지니어가 진짜 실패를 무시하게 된다는 문제가 보고됨 [6] [검색 요약으로 확인, 본문 미열람]:
1. retry는 허용하되 **모든 attempt를 기록**, retry로 통과하면 **FLAKY** (절대 PASS로 합치지 않음).
2. 테스트별 flaky 비율 추세 → 임계 초과 시 **gating에서 제외(quarantine)** 하되 계속 실행·추적, 담당자·기한 부여.
3. **보드 flaky vs 테스트 flaky 구분**: 같은 테스트가 특정 보드에서만 → 보드 문제(BV13 S8), 모든 보드에서 낮은 비율 → 테스트/FW/타이밍.
4. 같은 코드라도 FLAKY와 결정적 FAIL은 **다른 버그로 분리**해서 추적 (§6 실제 출력: B04 vs B08).
5. 간헐 실패는 별도 **loop 모드**로 율 + 95% 상한을 잰다 (§6 `--loop`).

**재현 패키지**: uart.log (모든 attempt) · meta.json (보드 속성·rework·조건·빌드·seed·attempt별 판정·**재현 명령**) · (실장비) JTAG 덤프, PMIC fault 레지스터, scope 캡처. 기준: **"다른 팀이 나에게 질문하지 않고 재현할 수 있나"**.

### 2.12 HW/SW triage 프로세스 + HW 팀 설득

```text
 새 실패 시그니처
   │ 1. 재현률 측정 (loop N회, 95% 상한)        ── 재현 안 됨 → flaky 목록 + 자동 증거 수집 강화
   ▼
 2. 무엇을 따라가나?  (인벤토리 조인 자동 힌트 → 사람이 실험으로 확인)
   ├─ 특정 보드만 ── ABA swap (칩/DRAM/슬롯) ── 보드에 남음 → 보드팀 (조립/BOM)
   │                                         └ 칩을 따라감 → corner 마진? spec 내 실패? → silicon 팀
   ├─ 특정 rev만 ── 2×2 (FW old/new × rev A/B) → ECO/BOM diff → 보드팀
   ├─ 특정 빌드부터 ── git bisect run (자동 판정) → SW/FW 담당
   ├─ 특정 조건만 ── V×T 스윕·shmoo → 마진 → SI/PI·silicon·spec 결정
   └─ 전부 ── 최소 SW(bare-metal)로 재현? ── 재현 → silicon/설계 (errata 후보) / 안 됨 → SW 스택
   ▼
 3. 증거 묶음: 파형·레지스터 덤프·로그·조건·재현률·배제한 것 (BV13 §2.5 6칸)
 4. 담당 팀 + 판정 기준 합의 → 수정 → 같은 loop로 수정 확인 (rule of three) → 회귀 테스트 추가
```

- **HW 팀 설득법** (behavioral 겹침):
  1. **판정 기준을 먼저 합의**: "bare-metal 최소 시퀀스로도 재현되면, 또는 같은 FW에서 보드 swap으로 실패가 따라가면 보드 쪽으로 보자" — 결과 전에 합의하면 결과가 결론이 된다.
  2. **원본 데이터를 공유**: 파형 원본 파일·레지스터 덤프·매트릭스, 내 해석은 따로 표기.
  3. **그들의 언어로**: "rise time 1.6 µs vs spec 1 µs at 100 kHz, 풀업 4.7 kΩ → 2.2 kΩ면 계산상 X" — 회로 수정 제안까지.
  4. **비용으로 우선순위**: "이 실패가 nightly 결과의 30%를 막고 있다", "workaround는 FW로 지금, 수정은 rev C".
  5. **우리 것이면 같은 채널에서 인정**하고 회귀 추가 — 신뢰가 다음 설득을 쉽게 한다.

### 2.13 EVT/DVT/PVT + ECO 회귀 범위

| 보드 단계 [3] | 보드 쪽 목표 | 검증(이 직무)의 초점 | 회귀 범위 |
|---|---|---|---|
| Proto / EVT | 설계가 동작하나, 문제 찾기 (production-intent 첫 빌드) | bring-up, 기능·인터페이스 전체, 초기 PVT, 디버그블리티 피드백 | 전체 (매 빌드) |
| DVT | 양산 의도 설계가 스펙·환경을 만족하나 (테스트 한계 적용) | PVT 전체, 스트레스·soak, 자동차 환경(온도·전원 이벤트), 안전 메커니즘 | 전체 + PVT 코너 |
| PVT | 양산 공정이 수율·속도로 만들 수 있나 | 제조 테스트/SLT로 이관, 샘플링 회귀, 공정 변경 감시 | 대표 suite + 제조 테스트 |
| ECO (단계 중간) | 특정 변경 | **추적성 기반 영향 테스트** + nightly 전체 | 아래 표 |

- 이름 주의: EVT/DVT/PVT의 V가 Validation인지 Verification인지 회사마다 다르다 [3] — 면접에서 단어보다 **각 단계의 목적**을 말한다. silicon 쪽 stepping(A0 → B0)과도 구분.

| ECO 종류 | 영향 범위 결정 | 재실행 |
|---|---|---|
| 수동 부품 값 변경 (풀업·cap) | 그 net의 인터페이스 | 해당 인터페이스 테스트 × 모든 코너, 여러 보드 |
| 부품 대체 (BOM alternate) | 그 부품이 관여하는 모든 요구사항 (주소·기본값·타이밍·온도 등급) | 해당 기능 + PVT, 입고 스크린 |
| 레이아웃 변경 (재배선) | SI/PI 영향 신호 | 마진 테스트(eye·shmoo) + 기능 |
| 전원 회로 변경 | 하류 전부 | 전원·리셋·부트 전체 + 스트레스 |
| board ID 변경 | FW 설정 경로 | 부트·설정 테스트, 모든 rev 대상 |

### 2.14 메트릭

| 메트릭 | 정의 | 왜 / 함정 |
|---|---|---|
| 요구사항 커버리지 | 실행 REQ / 전체, PASS REQ / 전체 (P0·P1 따로) | 테스트 개수 ≠ 커버리지 |
| first-pass rate | retry 없이 PASS / 실행 (테스트별) | retry 포함 pass rate는 flaky를 숨김 |
| flaky 비율 | FLAKY / 실행 (테스트별·보드별) | 추세가 올라가면 신뢰 하락 신호 |
| 재현률 + 상한 | k/N + Clopper-Pearson 95% 상한 | "0번 봤다" ≠ "0이다" |
| 누적 soak | device-hours, 실패 수 → MTBF 하한 ≈ T/3 (실패 0) | 1장 × 100 h ≠ 100장 × 1 h (편차 vs 노화) |
| 버그 burn-down | 열린 버그 수(P0/P1) 추세, find/fix rate | 줄지 않으면 일정 리스크 |
| 팜 가용률 | 테스트 가능한 보드-시간 / 전체 | 인프라 실패와 DUT 실패 분리 |
| MTTR (팜) | quarantine → 복귀 시간 | 보드 부족의 원인 |
| 회귀 탐지 지연 | 버그 유입 빌드 → 발견까지 시간 | nightly의 가치 측정 |

### 2.15 Staff 레벨 시그널 문장

- "I'd define exit criteria before we start, so 'done' isn't a negotiation at the end."
- "Coverage is measured against requirements, not test count."
- "A retry can turn red into yellow, never into green."
- "Infrastructure failures are reported separately — a broken cable shouldn't look like a silicon bug."
- "With three weeks, I'd go deep on P0 and write down what we're not doing and who accepted that risk."
- "I make every failure self-documenting, so one occurrence on the farm is enough to start debugging."
- "When we disagree about hardware versus software, I propose the experiment and the decision rule first."
- "The test plan, the bring-up firmware and the farm are one system — the firmware's output format is designed for the parser."

## 3. 보드에서 볼 것 · 잴 것 — 팜이 기록하는 것

### 3.1 테스트 한 번마다 남겨야 하는 것

| 항목 | 출처 | 왜 |
|---|---|---|
| 판정 + 모든 attempt + 시그니처 | 파서 | flaky·dedup |
| 보드 속성 (SN·rev·BOM·rework·lot/corner·슬롯) | 인벤토리 조인 | 상관관계 |
| 조건 (온도 setpoint + 실제 칩 온도, 입력 전압, OPP) | 챔버·센서·FW | PVT 재사용 |
| 빌드 hash, 테스트 FW 버전, seed | CI | 재현·bisect |
| 부팅 정보 (reset reason, boot count, 부트 시간, training eye) | UART 파서 | 추세 (마진 하락 조기 경보) |
| 전류·전압 telemetry | PSU SCPI, PMIC | S1·S10류 조기 감지 |
| 실패 시 증거 (UART 전문, JTAG 덤프, PMIC fault, scope 캡처) | 훅 | 재현 패키지 |

### 3.2 예시 — 아침 리포트 `[예시 출력]` (실제 mock 출력은 §6)

```text
[예시 출력]
NIGHTLY 2026-11-12  build g7f3e21  suite board_p0  boards 18/20 active (2 quarantined: B07 boot, B15 console)
REQ coverage  P0: 142/150 executed, 139 pass   P1: 61/90 executed
NEW signatures since last good (g1a2b3c4):
  csi2_test_pattern:0x0210 (CRC)  3/18 boards, all rev B, 85C only   -> follows rev B + temp  [ticket BV-412]
FLAKY trend  ddr_march_c 0.4% (7d avg 0.1%)  ↑   eth_phy_loopback 0.0%
Farm availability 93.1%   PDU events 1,204   longest hang: B06 pcie_link (JTAG dump attached)
```

| 줄 | 읽는 법 |
|---|---|
| `boards 18/20 active (2 quarantined …)` | 인프라/보드 상태가 먼저 — 결과 해석의 분모 |
| `REQ coverage P0: 142/150 executed, 139 pass` | 커버리지는 요구사항 기준, 실행과 통과를 분리 |
| `NEW signatures … follows rev B + temp` | 자동 triage 힌트 → 사람이 실험으로 확인할 1순위 |
| `FLAKY trend … ↑` | 비율 추세 — 낮아도 올라가면 조사 |

## 4. 꼬리 질문 체인

### 체인 A — "Write a validation plan for this new board." (BV00 원본)

Q → 꼬리1 "How do you prioritize with only 3 weeks?" → 꼬리2 "How many boards do you need and why?" → 꼬리3 "How do you know you're done?"

<details><summary>A-Q. Write a validation plan for this new board.</summary>

- **EN**: "I'd structure it in nine parts: scope — including what's explicitly out, like compliance or EMC owned by other teams; requirements with IDs traced to the spec; a test matrix covering functional, interface, power, clock, reset, performance, PVT, stress and soak; the conditions — voltage, temperature, boards and chip corners; sample sizes with the reasoning; numeric pass criteria; schedule and priority; risks with mitigations; and exit criteria. For an automotive AI SoC board, the P0 core is power and reset paths, LPDDR at temperature, the camera inputs and the high-speed links, because those are new, margin-sensitive and block the software teams."
- 한국어 핵심: 9칸 + 자동차 AI SoC면 전원·리셋·LPDDR 고온·카메라·고속 링크가 P0.

</details>

<details><summary>A-꼬리1. How do you prioritize with only 3 weeks?</summary>

- **EN**: "By risk: likelihood of failure times impact — safety, customers, or blocking another team — times the cost of finding it late. New IP and new board circuits first, reused mature blocks get a smoke test. And I spend the first week getting the farm automated, so weeks two and three run overnight too. I write down what we're not doing and get the lead to accept that risk explicitly."
- 한국어 핵심: 위험 점수, 새 것·차단하는 것 먼저, 1주차 자동화, 안 하는 것 명시·합의.

</details>

<details><summary>A-꼬리2. How many boards do you need and why?</summary>

- **EN**: "It depends on the conclusion. For bring-up, three to five, so one can break and I can tell 'this board' from 'every board'. For validation, enough to see board-to-board variation and run the regression in parallel — eight to twenty — including fast and slow corner chips. For statistics I use the rule of three: zero failures in N trials bounds the rate below about three over N, so a 1-in-1,000 claim needs about 3,000 clean cycles, spread over boards but also looked at per board. Component qualification like AEC-Q100 uses three lots of 77 parts, but that's a different question — reliability of the part, not validation of our board."
- 한국어 핵심: 단계별 수 + 근거, rule of three, AEC-Q100(3×77)과 목적 구분.

</details>

<details><summary>A-꼬리3. How do you know you're done?</summary>

- **EN**: "Exit criteria agreed at the start: all P0 requirements executed and passing, or covered by an approved waiver with a workaround; no open P0 bugs; PVT corners complete with margin at or above target; soak hours met; and the nightly regression stable for several consecutive runs with flaky rates under a threshold. If time runs out first, I report precisely which criteria aren't met and the residual risk, rather than calling it done."
- 한국어 핵심: 사전 합의 종료 기준, 시간이 먼저 끝나면 미충족 항목·잔여 리스크 보고.

</details>

### 체인 B — "How would you build automation for 50 boards running nightly?"

Q → 꼬리1 "A test fails 1 in 200 runs. What do you do?" → 꼬리2 "How do you keep the farm healthy?"

<details><summary>B-Q. Build automation for 50 boards running nightly.</summary>

- **EN**: "Each slot gets four handles: a switched PDU outlet or relay for hard power, a console port on a console server, a dedicated JTAG probe, and relay control of boot mode for recovery — plus Ethernet and a temperature sensor. An orchestrator reserves boards, deploys the build, runs a health check, then the suite: it sends a command, parses one machine-readable verdict line per test with an idle timeout and a test timeout, power-cycles and retries on failure, and records every attempt. Results go to a database joined with the board inventory — revision, rework, chip lot and corner — so failures are automatically correlated. Non-pass results produce a repro package. I'd build on labgrid- or LAVA-style abstractions for power and console rather than invent them."
- 한국어 핵심: 슬롯당 전원·콘솔·JTAG·boot-mode, 헬스 체크 → 파싱·타임아웃·retry 기록 → 인벤토리 조인 → 재현 패키지.

</details>

<details><summary>B-꼬리1. A test fails 1 in 200 runs. What do you do?</summary>

- **EN**: "First I make sure every occurrence is self-documenting — full console log, JTAG snapshot, telemetry, conditions. Then I measure the rate with a dedicated loop rather than relying on nightly samples: in my mock farm a board with a true 20 percent failure rate on one test was fully green in four of six nightly runs, while a 500-run loop measured 17 percent with a 95 percent upper bound of 20. Then I slice by board, slot, temperature and build to see what it follows, stress the suspected axis to raise the rate, and after a fix I need about 600 clean runs to claim it's below 1 in 200."
- 한국어 핵심: 자동 증거 → loop로 율 (mock 실측) → 속성 분해 → 스트레스 → 3/p 무실패로 수정 확인.

</details>

<details><summary>B-꼬리2. How do you keep the farm healthy?</summary>

- **EN**: "A health check before every board's run — boot to a ready marker, read IDCODE, check basic rails — and a board that fails twice is quarantined automatically and excluded from pass rates, with an alert to whoever owns the farm. Infrastructure failures — console, PDU, probe — are reported separately from DUT failures. I track farm availability and time to repair, keep a golden board as a canary, rotate suspicious cables and probes, and the orchestrator's own parser has negative controls so a harness bug can't produce fake passes."
- 한국어 핵심: 헬스 체크·자동 격리·인프라 분리 집계·가용률/MTTR·카나리 보드·파서 negative control.

</details>

### 체인 C — "How do you decide whether a failure is hardware or software?"

Q → 꼬리1 "The HW team says it's software. How do you convince them?"

<details><summary>C-Q. How do you decide hardware versus software?</summary>

- **EN**: "By what the failure follows, tested one variable at a time. Reproduce and measure the rate; swap boards and chips with an A-B-A swap; run older builds and bisect; sweep voltage and temperature; and reduce the software to a minimal bare-metal sequence. A failure that stays with one board is assembly or BOM; one that follows the chip is silicon or corner margin; one that starts at a build is software; one that survives with the smallest software and every board is a design or silicon issue. The measurements — waveforms and register dumps — are the evidence for whichever team owns it."
- 한국어 핵심: 따라가는 것 (보드/칩/빌드/조건/최소 SW) + 측정 증거.

</details>

<details><summary>C-꼬리1. The HW team says it's software. How do you convince them?</summary>

- **EN**: "I don't argue conclusions; I propose an experiment and agree on the decision rule before running it — for example, 'if it reproduces with a ten-line bare-metal sequence, or moves with the board in a swap using identical firmware, we treat it as hardware'. Then I share raw data — the waveform files, the dumps, the matrix — and translate it into their terms, like rise time versus spec and what pull-up value would fix it. If the data points at us, I own it publicly and add the regression. That track record is what makes the next conversation easy."
- 한국어 핵심: 판정 규칙 선합의 → 원본 데이터 → 그들 언어 → 우리 것이면 인정.

</details>

### 체인 D (추가) — "A board ECO is coming. What do you re-run?"

<details><summary>D-Q. Rev B adds three ECOs. How do you scope regression?</summary>

- **EN**: "From traceability, backwards: each ECO changes specific nets or parts, those map to interfaces and requirements, and those map to tests. A pull-up change means that bus's interface tests across all corners on several boards; a BOM alternate means everything that part touches, including its address, defaults and temperature grade; a power-circuit change means power, reset and boot plus stress. On top of that the full nightly suite runs on the new revision, and results are tagged by revision so rev A history stays valid. The new revision isn't accepted until the impacted tests pass on multiple boards."
- 한국어 핵심: ECO → net/부품 → 요구사항 → 테스트, + nightly 전체, rev 태그.

</details>

<details><summary>D-꼬리1. What changes between EVT, DVT and PVT for you?</summary>

- **EN**: "At EVT the question is 'does the design work' — bring-up, full functional and interface coverage, early margins, and feedback on debuggability. At DVT it's 'does the production-intent design meet spec across environment' — full PVT, stress and soak, automotive power events, safety mechanisms. At PVT it's 'can the factory build it' — my tests turn into manufacturing and system-level test content, and regression becomes sampling and change monitoring. Companies name the V differently, so I describe the purpose."
- 한국어 핵심: EVT 동작/DVT 스펙·환경/PVT 공정 → 테스트가 제조/SLT로.

</details>

### 체인 E (추가) — "What would you report to management weekly?"

<details><summary>E-Q. What metrics do you report?</summary>

- **EN**: "Requirement coverage — executed and passing, P0 and P1 separately; open bugs by priority with a burn-down; first-pass rate and flaky rate per test, trended; accumulated soak device-hours; farm availability; and the residual risks against the exit criteria. One page, with the top three risks and what I need — boards, chamber time, a decision."
- 한국어 핵심: 커버리지·버그 burn-down·first-pass/flaky 추세·soak·팜 가용률·리스크 + 요청.

</details>

<details><summary>E-꼬리1. Why first-pass rate instead of pass rate?</summary>

- **EN**: "Because a pass rate that includes retries hides flakiness — a test that passes on the third try every night looks green. First-pass rate and a separate flaky count keep the real signal visible."
- 한국어 핵심: retry 포함 pass rate는 flaky를 숨김.

</details>

## 5. Don 경험 연결

- "At SK hynix I built the chamber reliability test platform — scheduling, temperature control, UART sequencing, result collection and the SDK other engineers used — which is this exact architecture: automate the stress, tag every result with its conditions, and capture evidence on failure. We added shmoo and margin features for NAND and PCIe Gen3–5 interfaces, repeating points to separate a hard wall from a marginal region." `[숫자 채우기: 챔버 수 / 동시 DUT / 사용 엔지니어 수 / 단축한 시간]` (→ §2.10, §2.11, 체인 B) — **Staff 시그널 카드**
- "At Apple I designed factory test nodes around stress scenarios to expose latent defects before shipment, which is the PVT and SLT end of a validation plan — deciding what to test, at which conditions, with what pass criteria and how much test time." `[숫자 채우기: 노드 수·발견한 결함 유형]` (→ §2.13, 체인 D)
- "At Solidigm, as a Staff engineer on a PCIe Gen6 SSD controller, I verified I2C, SPI, DMA, SRAM and DRAM blocks with bare-metal tests on FPGA before RTL freeze and worked with the silicon validation team on performance — so the plan, the test firmware and the automation were designed together, and on silicon we only debugged what FPGA couldn't show." (→ §2.15 마지막 문장, 체인 A)
- 통계 문장 (UC Berkeley 수학): "I'm careful with 'it passed overnight' — zero failures in N runs only bounds the rate below about three over N, and a retry that passes is a flaky result, not a pass."

## 6. 실습 / 코드

### 6.1 파일과 구조

- [farm_orchestrator.py](onsitePrep/bv14/farm_orchestrator.py) (621줄, Python 3.9 stdlib only) · [inventory.json](onsitePrep/bv14/inventory.json) (8장) · [run_all.sh](onsitePrep/bv14/run_all.sh) (아래 출력 전부 재생성 → [run_log.txt](onsitePrep/bv14/outputs/run_log.txt)) · 산출물: [results.json](onsitePrep/bv14/outputs/results.json), [summary.txt](onsitePrep/bv14/outputs/summary.txt), `outputs/repro/<SN>_<temp>C_<test>/{uart.log,meta.json}` 9개.

| 부분 | 내용 | 실제 랩에서 바뀌는 것 |
|---|---|---|
| 로그 파서 | 앵커된 정규식: `START` / `PASS` / `FAIL code=0xNNNN k=v` / `SHELL READY` / `RESET_REASON=` / `PANIC` | 그대로 (FW 출력 계약) |
| hang 감지 | `run_one_test()`: idle timeout(3 s 무출력) + test timeout(테스트별) + PANIC/RESET 줄 → HANG | 그대로, 시간만 실제 시계 |
| retry / 분류 | 실패 시 전원 사이클 → 재시도 ≤ 2, `classify()` → PASS / FLAKY / FAIL / HANG / FAIL_INCONSISTENT | 그대로 |
| 헬스 체크 | 부팅 2회 실패 → QUARANTINED, 테스트는 NOT_RUN (INFRA) | + IDCODE, rail telemetry |
| triage 힌트 | 시그니처 × (intermittent/deterministic) 별로 보드·rev·온도·corner 상관 | + DB 쿼리 |
| 재현 패키지 | uart.log(전 attempt) + meta.json(보드 속성·rework·조건·빌드·seed·**재현 명령**) | + JTAG 덤프·scope 캡처 |
| loop 모드 | `--loop SN TEST N` → k/N + Clopper-Pearson 95% 상한 | 그대로 |
| mock | `MockPDU`(on/off/cycle 이벤트), `MockChamber`(set+soak), `MockUART`(지연 있는 줄 스트림, timeout 시 None), `MockBoard`(고장 모델별 로그 생성), `SimClock` | 실제 PDU API·ser2net 소켓·PyVISA |

- mock 고장 모델 (inventory의 `fault_model`, 실제 보드는 이걸 알려 주지 않음): B04 `ddr_march_c` 간헐 p=0.2 · B05/B08 rev B ECO로 I2C 0x50 NACK · B06 `pcie_link` 무응답 · B07 PMIC fault로 부팅 불가 · B08 85 °C에서 DDR 실패(SS corner).

### 6.2 실제 실행 출력 (macOS, `python3 -I`, 2026-11-01) [확인됨]

**① 파서·분류기 self-test (negative/positive control)**

```text
$ python3 -I farm_orchestrator.py --selftest
parser controls
  PASS line parsed                                           ok
  FAIL line + code parsed                                    ok
  'BYPASS' inside text is NOT a pass                         ok
  unanchored 'T:x PASS' mid-line is NOT a pass               ok
  PANIC detected as fatal                                    ok
hang detection controls
  START then silence -> HANG(after_START)                    ok
  chatty but no verdict -> HANG(test_timeout)                ok
  PASS for another test name is ignored                      ok
classifier controls
  P -> PASS                                                  ok
  F,P -> FLAKY (retry never yields clean PASS)               ok
  F,F,F same sig -> FAIL                                     ok
  H,H,H -> HANG                                              ok
  F(0x3),F(0x4),F(0x3) -> FAIL_INCONSISTENT                  ok
SELFTEST PASS
exit=0
```

**② 전체 팜 실행 (8 boards × 8 tests × 2 temps, seed 1)**

```text
$ python3 -I farm_orchestrator.py
BOARD FARM RUN  fw=bringup-fw 0.9.3-g1a2b3c4  seed=1  retries=2  sim_time=1546s
cell = 25C/85C   P=pass  K=flaky(pass on retry)  F=fail  H=hang  X=inconsistent  -=not run
SN   rev cn | pmic_rails pll_lock   ddr_march_ i2c_scan   spi_flash_ eth_phy_lo pcie_link  csi2_test_ | health
--------------------------------------------------------------------------------------------------------------------
B01  A   TT | P/P        P/P        P/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=2
B02  A   TT | P/P        P/P        P/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=2
B03  A   FF | P/P        P/P        P/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=2
B04  A   TT | P/P        P/P        K/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=3
B05  B   TT | P/P        P/P        P/P        F/F        P/P        P/P        P/P        P/P        | ACTIVE pc=8
B06  A   TT | P/P        P/P        P/P        P/P        P/P        P/P        H/H        P/P        | ACTIVE pc=8
B07  A   SS | -/-        -/-        -/-        -/-        -/-        -/-        -/-        -/-        | QUARANTINED pc=2
B08  B   SS | P/P        P/P        P/F        F/F        P/P        P/P        P/P        P/P        | ACTIVE pc=11

PER-TEST (excluding NOT_RUN)
test                run pass flaky fail hang  first-pass rate
pmic_rails           14   14     0    0    0  100.0%
pll_lock             14   14     0    0    0  100.0%
ddr_march_c          14   12     1    1    0   85.7%
i2c_scan             14   10     0    4    0   71.4%
spi_flash_id         14   14     0    0    0  100.0%
eth_phy_loopback     14   14     0    0    0  100.0%
pcie_link            14   12     0    0    2   85.7%
csi2_test_pattern    14   14     0    0    0  100.0%

FAILURES (non-PASS rows, retries shown)
  B04 25C ddr_march_c        FLAKY  ddr_march_c:0x0003     attempts=FAIL,PASS  addr=0x80204F88 exp=0x55555555 got=0x55555455
  B05 25C i2c_scan           FAIL   i2c_scan:0x0102        attempts=FAIL,FAIL,FAIL  addr=0x50 bus=3
  B06 25C pcie_link          HANG   pcie_link:HANG(after_START) attempts=HANG,HANG,HANG  UART silent > 3.0s (after_START)
  B08 25C i2c_scan           FAIL   i2c_scan:0x0102        attempts=FAIL,FAIL,FAIL  addr=0x50 bus=3
  B05 85C i2c_scan           FAIL   i2c_scan:0x0102        attempts=FAIL,FAIL,FAIL  addr=0x50 bus=3
  B06 85C pcie_link          HANG   pcie_link:HANG(after_START) attempts=HANG,HANG,HANG  UART silent > 3.0s (after_START)
  B08 85C ddr_march_c        FAIL   ddr_march_c:0x0003     attempts=FAIL,FAIL,FAIL  addr=0x80E623B0 exp=0xAAAAAAAA got=0xAAAA2AAA
  B08 85C i2c_scan           FAIL   i2c_scan:0x0102        attempts=FAIL,FAIL,FAIL  addr=0x50 bus=3

FARM HEALTH: quarantined=B07 (INFRA, excluded from pass rates)

TRIAGE HINTS (signature -> which variable it follows)
  ddr_march_c:0x0003         deterministic boards=B08 temps=85 classes=FAIL
      - single board B08 (rev B, SS corner, LOT18-W02, rework=none) -> board-specific: swap SoC / move to golden board
      - only at 85 C -> temperature-correlated: margin (shmoo V x T, training eye)
  ddr_march_c:0x0003         intermittent  boards=B04 temps=25 classes=FLAKY
      - single board B04 (rev A, TT corner, LOT17-W03, rework=none) -> board-specific: swap SoC / move to golden board
      - seen 1 time(s) -> too few events to correlate with temperature; loop it first
      - intermittent -> measure rate (rule of three), stress to raise rate, keep in flaky list
  i2c_scan:0x0102            deterministic boards=B05,B08 temps=25/85 classes=FAIL
      - every rev B board, no other rev -> follows board revision: diff ECO/BOM A/B
  pcie_link:HANG(after_START) deterministic boards=B06 temps=25/85 classes=HANG
      - single board B06 (rev A, TT corner, LOT17-W11, rework=['R214 0R->DNP (PERST# strap)']) -> board-specific: swap SoC / move to golden board
      - hang -> JTAG halt at timeout to read PC/regs; check reset/strap of that block

wrote outputs/results.json, outputs/summary.txt, 9 repro packages under outputs/repro
exit=1
```

| 관찰 | 의미 |
|---|---|
| B07 `-/-` 전부 + `QUARANTINED pc=2` | 부팅 2회 실패 → 테스트 실패가 아니라 INFRA로 격리 → per-test 표의 run = 14 (7장 × 2온도), 분모 오염 없음 |
| B04 `K/P`, attempts `FAIL,PASS` | retry로 통과했지만 **FLAKY**로 남음 — first-pass rate 85.7%에 반영 |
| B08 `ddr P/F` + "only at 85 C" + "SS corner" | BV13 S7 패턴 — 온도 상관 + corner 정보가 힌트에 자동 포함 |
| 같은 `ddr_march_c:0x0003`이 deterministic(B08)·intermittent(B04)로 **두 줄** | 같은 코드라도 메커니즘이 다를 수 있어 분리 추적 (§2.11-4) |
| `i2c_scan:0x0102` "every rev B board, no other rev" | BV13 S12 — ECO/BOM diff로 바로 안내 |
| B06 HANG 힌트에 rework 이력(`R214 … PERST# strap`) | 인벤토리 조인의 가치: 사람이 스프레드시트를 안 만들어도 rework가 1순위 가설로 |
| `exit=1` | 비-PASS가 있으면 CI 실패 코드 |
| `sim_time=1546s` | 챔버 soak 2회(600 s × 2) 포함 시뮬레이션 시간 — 실제로는 즉시 끝남 |

**③ negative control — FW heartbeat가 idle timeout보다 느리면 (개발 중 실제로 겪은 버그를 플래그로 재현)**

```text
$ python3 -I farm_orchestrator.py --heartbeat 5 | sed -n '/PER-TEST/,/^$/p'
PER-TEST (excluding NOT_RUN)
test                run pass flaky fail hang  first-pass rate
pmic_rails           14   14     0    0    0  100.0%
pll_lock             14   14     0    0    0  100.0%
ddr_march_c          14    0     0    0   14    0.0%
i2c_scan             14   10     0    4    0   71.4%
...
```

- 첫 구현에서 mock DDR 테스트가 progress 줄 없이 4.8 s 뒤에 FAIL을 찍었고, 3 s idle timeout 때문에 **진짜 FAIL이 HANG으로 오분류**됐다. → 고침: FW 계약에 "긴 테스트는 idle timeout보다 짧은 주기로 progress 출력". `--heartbeat 5`는 이 오류를 일부러 재현하는 negative control: 정상 보드 7장 전부에서 ddr_march_c가 HANG 14/14 — **하네스-FW 계약이 깨지면 이렇게 보인다**는 증거.

**④ 같은 팜, nightly seed 6개 — B04 (진짜 p_fail = 0.2)**

```text
seed 1: B04  A   TT | P/P        P/P        K/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=3
seed 2: B04  A   TT | P/P        P/P        P/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=2
seed 3: B04  A   TT | P/P        P/P        P/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=2
seed 4: B04  A   TT | P/P        P/P        P/K        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=3
seed 5: B04  A   TT | P/P        P/P        P/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=2
seed 6: B04  A   TT | P/P        P/P        P/P        P/P        P/P        P/P        P/P        P/P        | ACTIVE pc=2
```

- 6번 중 4번 **전부 green**. nightly는 저확률 사건의 표본 몇 개일 뿐 → "어젯밤 green = 고쳐짐"이 아니다. (기대값 확인: 하룻밤 B04 ddr 첫 시도 2번이 모두 PASS일 확률 = 0.8² = 0.64 → 6번 중 약 3.8번 green 예상, 관측 4번.)

**⑤ loop 모드 — 율과 95% 상한**

```text
$ python3 -I farm_orchestrator.py --loop B04 ddr_march_c 500
loop B04 ddr_march_c @25C: 86 fail / 500 runs = 17.20%  first_fail=1  95% upper bound=0.2022 (1 in 5)
$ python3 -I farm_orchestrator.py --loop B01 ddr_march_c 300
loop B01 ddr_march_c @25C: 0 fail / 300 runs = 0.00%  first_fail=None  95% upper bound=0.0099 (1 in 101)
$ python3 -I farm_orchestrator.py --loop B08 ddr_march_c 50 --temp 85
loop B08 ddr_march_c @85C: 50 fail / 50 runs = 100.00%  first_fail=1  95% upper bound=1.0000 (1 in 1)
$ python3 -I farm_orchestrator.py --loop B08 ddr_march_c 50 --temp 25
loop B08 ddr_march_c @25C: 0 fail / 50 runs = 0.00%  first_fail=None  95% upper bound=0.0582 (1 in 17)
$ python3 -I farm_orchestrator.py --loop B06 pcie_link 20 --temp 25
loop B06 pcie_link @25C: 20 fail / 20 runs = 100.00%  first_fail=1  95% upper bound=1.0000 (1 in 1)
$ python3 -I farm_orchestrator.py --loop B07 pll_lock 5
B07 does not boot -> fix INFRA first
exit=2
```

| 줄 | 해석 |
|---|---|
| B04 86/500 = 17.2%, 상한 20.2% | 진짜 p=0.2를 포함 (상한이 제 역할). 단 하룻밤 1–2번 시도로는 이 율을 절대 알 수 없음 |
| B01 0/300 → 상한 0.0099 | rule of three: 3/300 = 0.010, 독립 계산 `1-0.05**(1/300) = 0.009936` 과 일치 [확인됨] |
| B08 85 °C 50/50 vs 25 °C 0/50 | 온도 상관이 "1번 봤다"가 아니라 데이터로 확정 (25 °C 상한 5.8%) |
| B07 `exit=2` | 부팅 안 되는 보드에서 테스트 율을 재지 않는다 — 인프라/보드 먼저 |

**⑥ 재현 패키지 예 — `outputs/repro/B06_25C_pcie_link/meta.json` (발췌)**

```text
{
  "sn": "B06", "rev": "A", "soc_lot": "LOT17-W11", "corner": "TT",
  "rework": ["R214 0R->DNP (PERST# strap)"],
  "pdu": "pdu2:2", "uart": "ttyFARM5", "jtag": "probe-06",
  "temp_c": 25, "test": "pcie_link", "fw_build": "bringup-fw 0.9.3-g1a2b3c4", "seed": 1,
  "attempts": [{"verdict": "HANG", "signature": "pcie_link:HANG(after_START)", "dur_s": 3.21,
                "detail": "UART silent > 3.0s (after_START)"}, ... x3],
  "repro_cmd": "python3 -I farm_orchestrator.py --loop B06 pcie_link 20 --temp 25 --seed 1"
}
uart.log:
### attempt 1 verdict=HANG sig=pcie_link:HANG(after_START)
[  713.550] T:pcie_link START
[  713.750] T:pcie_link waiting for LTSSM L0          ← 마지막 줄: LTSSM 대기에서 멈춤 → LP15 S1 / BV13 S2·S9
```

- `repro_cmd`를 실제로 실행해 같은 결과(20/20 HANG)가 나오는 것까지 확인 [확인됨, ⑤의 B06 줄].

### 6.3 다음 개선 TODO (면접에서 말할 거리) `[미구현]`

1. HANG 시 JTAG attach → PC·코어 상태 덤프를 repro에 추가 (OpenOCD/Trace32 스크립트 훅).
2. 결과를 SQLite에 적재 + 전날·마지막 good 빌드와 **신뢰구간 비교**로 "새 실패" 판정.
3. 새 시그니처 → 빌드 범위로 자동 `git bisect run` 후보 생성.
4. 보드별 병렬 실행 (스레드/프로세스, 보드 잠금), PDU 전류 readback baseline.
5. 실장비 백엔드: PDU HTTP/SNMP, ser2net TCP 소켓, PyVISA PSU/DMM — mock과 같은 인터페이스로 교체.

## 7. 자주 틀리는 것

1. **테스트 개수를 커버리지로 보고** — 요구사항 대비 실행/통과로.
2. **통과 기준이 형용사** ("안정적으로") — 값·조건·N·판정·측정 방법.
3. **retry 통과를 PASS로 합침** — flaky를 숨기고 진짜 버그를 묻는다. FLAKY는 따로, first-pass rate로 보고.
4. **인프라 실패를 DUT 실패로 집계** — 고장 보드·케이블이 실패율을 오염. 헬스 체크 + quarantine + 분리 집계.
5. **보드 1장 × 많은 반복 = 보드 편차까지 커버했다고 착각** — 층화(칩 corner × 보드 × 조건).
6. **"nightly green이니 고쳐짐"** — mock에서 p=0.2 버그가 6번 중 4번 green. loop + rule of three.
7. **3주에 모든 것을 조금씩** — P0 깊게, 안 하는 것 명시·리스크 합의.
8. **종료 기준을 끝에 협상** — 시작 전에 합의.
9. **파서가 느슨함** — `PASS` 부분 문자열 매칭("BYPASS")이나 다른 테스트의 PASS를 인정. 앵커된 정규식 + negative control (self-test).
10. **idle timeout과 FW heartbeat 불일치** — 살아 있는 긴 테스트가 HANG으로 오판 (§6 ③에서 실제 재현).
11. **AEC-Q100 샘플 수를 보드 검증 샘플 수로 인용** — 부품 qualification(3 lot × 77)과 보드 validation은 목적이 다르다.

## 8. 출처

1. labgrid documentation — Overview (coordinator/exporter/client, resources vs drivers, places, NetworkPowerDriver, ConsoleProtocol, strategies) · https://labgrid.readthedocs.io/en/latest/overview.html
2. LAVA (Linaro Automated Validation Architecture) — Introduction (central server + dispatcher workers) · https://lava.pengutronix.de/static/docs/v2/index.html · AGL lava-docker README (`pdu_generic` 명령 훅) · https://git.automotivelinux.org/AGL/lava-docker/tree/README.md?h=15.0.2
3. Instrumental — EVT, DVT, PVT (build stages, quantities, purpose) · https://instrumental.com/build-better-handbook/evt-dvt-pvt · Embedded Artistry field manual — Production Validation and Test · https://embeddedartistry.com/fieldmanual-terms/production-validation-and-test/
4. EmbeddedRelated — Embedded Linux board farms 101: the requirements that actually matter (console server, remote power, recovery mode, unwedge sequence, reimaging) · https://www.embeddedrelated.com/showarticle/1782.php
5. AEC-Q100 sample sizes (3 lots × 77 = 231, 0 fail) — Accendo Reliability · https://accendoreliability.com/aec-q100-test-sample-sizes/ · MPS webinar "Fundamentals of AEC-Q100" · https://media.monolithicpower.com/mps_cms_document/w/e/Webinar_-_Fundamentals_of_AEC-Q100-6Nov2018.pdf
6. Google Testing Blog — Flaky Tests at Google and How We Mitigate Them (2016) · https://testing.googleblog.com/2016/05/flaky-tests-at-google-and-how-we.html · Where do our flaky tests come from? (2017) · https://testing.googleblog.com/2017/04/where-do-our-flaky-tests-come-from.html (본문 일부만 열람, 84% 수치는 검색 요약)
7. PyVISA documentation — Communicating with your instrument (`ResourceManager`, `open_resource`, `query('*IDN?')`, termination) · https://pyvisa.readthedocs.io/en/latest/introduction/communication.html
8. git-bisect documentation (`git bisect run`) · https://github.com/gitster/git/blob/maint/Documentation/git-bisect.txt
9. Rule of three (statistics) · https://en.wikipedia.org/wiki/Rule_of_three_(statistics)
10. 내부: [LP14 검증 방법론 (12영역 PCIe 플랜, rule of three, 하네스 아키텍처, flaky 정책)](2026-10-31_LP14_validation_methodology.html), [BV13 디버그 시나리오](2026-11-01_BV13_board_debug_scenarios.html), [deck §6 검증 설계](2026-10-25_rivian_hm_screen_prep.html), [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html)

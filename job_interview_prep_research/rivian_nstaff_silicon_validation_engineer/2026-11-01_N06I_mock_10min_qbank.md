# N06I · 모의 면접 — 10분 말하기 Q 뱅크 (프롬프트 30 + 채점표 + 모의 세트 3)
<!-- theme: pastel -->

> **묶음**: N06 검증 방법론과 테스트 플랜 설계 — 마지막 페이지 (N06A–H 종합 연습) · **우선**: P1 (D-5 이후 모의 면접용) · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) / 없음 (N06 마지막) → [온사이트 플랜](2026-10-31_rivian_onsite_plan.html) · **관련**: [BV16 보드 모의 Q 뱅크](2026-11-01_BV16_mock_interview_qbank.html) (중복 없음 — 보드 bring-up 30문항) · [deck §4 스토리, §5.1, §6, §7](2026-10-25_rivian_hm_screen_prep.html) · [LP14 PCIe 검증 방법론](2026-10-31_LP14_validation_methodology.html) · [BV14 검증 계획과 보드 팜](2026-11-01_BV14_board_validation_plan_automation.html) · [LP15](2026-10-31_LP15_debug_scenarios.html) · [BV13](2026-11-01_BV13_board_debug_scenarios.html) · **코드**: 없음 (말하기 연습 페이지)

## 0. TL;DR · 사용법

- 이 페이지 = Rivian Technical 2(도메인) 라운드용 **말하기 프롬프트 30개**: 블록 테스트 플랜 12 (T01–T12) · 방법론 8 (M01–M08) · 디버그 6 (D01–D06) · Staff/리더십 4 (L01–L04). 질문은 **Rivian 실리콘 검증 면접관 말투**로 썼다 — 자동차 AI SoC(RAP1 계열 [확인됨: context]), 카메라 8개, LPDDR5, PCIe, automotive Ethernet, NPU, HAPS 프로토타입 → A0 → PVT → SLT. 면접관 후보 Manjiang Zhang(FPGA/HAPS · bring-up · PVT · SLT · Marvell Ethernet PHY · 센서 timestamp/sync 특허 [확인됨: deck §1])이 물을 법한 꼬리를 붙였다.
- **모든 답 뼈대는 같은 7단계** (N06A 프레임): ① clarify → ② block picture → ③ category matrix → ④ priority · platform → ⑤ pass criteria · coverage → ⑥ automation · regression → ⑦ risks · A0 first week. 디버그·Staff 프롬프트도 이 7칸에 맞춰 바꿔 썼다 (§1.2 · §1.3 표).
- 펼친 뼈대 = **90초 압축판** (단계마다 영어 1–3줄). **10분 버전**은 같은 줄을 §1.1 시간표만큼 늘려 말한다 — 특히 ③ 매트릭스에서 카테고리마다 한 문장씩.
- **연습 루프 (프롬프트 하나에 20분)**: ① 질문 + 꼬리만 보고 휴대폰 녹음 시작 → ② 타이머 10분, 소리 내서 → ③ 꼬리 3개 각 60–90초 → ④ 녹음 다시 들으며 §1 채점표로 0/1/2 채점, 빠진 카테고리 약어 기록 → ⑤ `<details>` 펼쳐서 비교 → ⑥ §8 연습 기록 표에 한 줄.
- **★ 먼저**: T01 I2C · T04 DMA · T05 LPDDR5 · T09 CSI-2 카메라 · T11 NPU · M02 종료 기준 · M05 플랫폼 재사용 · D01 HAPS↔A0 · L01 pre-si 버그. 온사이트 전에 **모의 세트 3개**(§7)를 각각 1회 이상.
- **중복 피함**: 보드 bring-up 질문(Day 1, DDR training 디버그, memtest 작성, ECC, 보드 팜, HW-or-SW, 자동차 보드)은 [BV16](2026-11-01_BV16_mock_interview_qbank.html)에, 기본 개념 Q1–Q10은 [deck §5.1](2026-10-25_rivian_hm_screen_prep.html)에, 디버그 16선은 [deck §7](2026-10-25_rivian_hm_screen_prep.html)에 있다. 여기는 **"블록/SoC 테스트 플랜을 설계해 말하기"** 와 그 방법론 · 새 디버그 · Staff에만 집중.
- Staff 프롬프트의 숫자는 **절대 지어내지 않는다** → `[숫자 채우기]` 칸. 온사이트 전 deck §4 S1–S8과 함께 채울 것.

## 1. 채점표

녹음을 다시 들으면서 항목마다 **0 / 1 / 2** 를 준다. 애매하면 낮은 점수. 프롬프트 종류별로 표가 다르다.

### 1.1 블록 테스트 플랜 (T01–T12) · 방법론 (M01–M08) — 12항목, 24점 만점

| # | 항목 | 0점 | 1점 | 2점 | 녹음에서 확인할 것 |
|---|---|---|---|---|---|
| 1 | ① Clarify | 질문 없이 바로 시작 | 질문 1개 | 질문 2–4개 + "I'll assume …" 로 가정을 말함 | 첫 30초 |
| 2 | ② Block picture | 그림 없음 | 블록 이름만 | 인터페이스 5종(bus · IRQ · DMA · clock/reset · PHY/pins) + 상대 장치까지 | 0:30–1:30 |
| 3 | ③ 카테고리 커버 | 12종 중 5개 이하 | 6–9개 | 10개 이상 **그리고** ERR · RST · PVT 포함 | 아래 약어로 체크 |
| 4 | ④ 우선순위 | 없음 | P0/P1 나열만 | P0 이유(bring-up 차단 · 새 RTL · DV가 못 덮음) + "what I won't test" | |
| 5 | ④ 플랫폼 | 언급 없음 | "FPGA and silicon" 한 줄 | 항목별로 FPGA/Si 근거 (로직 → FPGA, 아날로그·타이밍·전원 → Si) | |
| 6 | ⑤ 통과 기준 | "it works" | 숫자 1–2개 | 매트릭스 행마다 측정 가능한 숫자 + 샘플 수(rule of three) | 숫자 개수 세기 |
| 7 | ⑤ 커버리지 · 종료 | 없음 | "coverage" 단어만 | requirement ID 추적 + 종료 기준을 시작 전에 합의 | |
| 8 | ⑥ 자동화 · 회귀 | 없음 | "I'd automate it" | HAL로 같은 테스트 · 회귀 계층 · 결과에 chip ID/V/F/T 태그 | |
| 9 | ⑦ 리스크 · A0 첫 주 | 없음 | 리스크 1개 | 리스크 2개 이상 + 완화 + A0 day-1 순서 | |
| 10 | 시간 관리 | 6분 미만 또는 12분 초과 | 10±2분이지만 ③이 2분 미만 | 7단계 모두 + ③ ≥ 3분 + 꼬리 시간 남김 | 타임스탬프 |
| 11 | 전달력 | 필러("um", "so basically") 많고 구조 없음 | 구조는 있으나 숫자 적음 | "First / Second / Third" 신호 + 숫자 3개 이상 + 천천히 | 비원어민 면접관 대비 [추정: deck §1.3] |
| 12 | Don 훅 | 없음 | 일반론 ("I did similar things") | 구체적 경험 1개 + 결과, 과장 없음 | |

- **판정**: 20–24 = 온사이트 OK · 15–19 = 낮은 항목 2개만 다음 회차 목표 · 14 이하 = [N06A 템플릿](2026-11-01_N06A_methodology_test_plan_template.html) 다시 읽고 같은 프롬프트 재도전.
- **카테고리 약어** (③ 체크 · §8 기록용, N06A 12종 순서): **REG** 레지스터 · **FUNC** 기본 기능 · **CFG** 설정 조합 · **BND** 경계/corner · **IRQ** 인터럽트 · **ERR** 에러/negative · **RST** 리셋/전원 상태 · **CONC** 동시성/stress · **PERF** 성능 · **PVT** PVT/margin · **SOAK** 장시간 · **SYS** 시스템/interop.

```text
 10분 시간표 (블록 테스트 플랜)                         방법론 프롬프트는 같은 순서, 총 6-8분
 0:00 ─ ① clarify (30 s)  "Before I start, a few questions..."
 0:30 ─ ② block picture (1 min)  손으로 그림: bus / IRQ / DMA / clk-rst / PHY-pins / partner
 1:30 ─ ③ category matrix (3 min)  REG FUNC CFG BND IRQ ERR RST CONC PERF PVT SOAK SYS
 4:30 ─ ④ priority + platform (1 min)  P0 bring-up / P1 / P2, FPGA vs Si, "won't test"
 5:30 ─ ⑤ pass criteria + coverage (1 min)  numbers, sample size, requirement IDs
 6:30 ─ ⑥ automation + regression (1 min)  HAL, same test everywhere, nightly, DB tags
 7:30 ─ ⑦ risks + A0 first week (1 min)
 8:30 ─ 남은 시간: 면접관 꼬리 질문
```

### 1.2 디버그 (D01–D06) — 10항목, 20점 만점

| # | 항목 (7단계 대응) | 0점 | 1점 | 2점 |
|---|---|---|---|---|
| 1 | ① 사실 확인 | 바로 가설 | 빈도 또는 범위 중 하나 | 빈도 · 언제부터 · 어떤 칩/보드/조건 · 무엇이 바뀌었나 |
| 2 | ② 실패 경로 그림 | 없음 | 블록 나열 | 데이터 경로를 단계로 그리고 "어느 단계에서 처음 틀리나"로 좁힘 |
| 3 | ③ 가설 매트릭스 | 1개에 꽂힘 | 2–3개 | HW · SW · board · test · infra 를 모두 후보로 + 근거 |
| 4 | ④ 실험 순서 | 무작위 | 순서 있음 | 싼 것부터 + 한 번에 하나만 바꾸기(swap · A-B-A · bisect · 조건 스윕) |
| 5 | ④ 계측 | "add logs" | 도구 이름 | 도구 + 무엇을 트리거/카운트하는지 (GPIO 트리거, 에러 카운터, raw capture) |
| 6 | ⑤ 고쳐졌다는 증거 | "it passed" | 재실행 | rule of three 숫자 (1/p 버그 → ≈3/p 무실패) + 버그를 되돌리면 다시 실패 |
| 7 | ⑥ 회귀 · 재발 방지 | 없음 | 테스트 추가 | 테스트 추가 + 계획에서 왜 놓쳤나 리뷰 + errata/workaround 문서 |
| 8 | ⑦ 첫 1시간 | 없음 | 막연함 | 첫 1시간에 할 2–3개를 구체적으로 |
| 9 | 소통 | 없음 | "tell the team" | 누구에게(design · DV · SW · product), 어떤 증거로, 언제 |
| 10 | 시간 | 3분 미만 또는 10분 초과 | 5–8분, 한쪽 치우침 | 6–8분 + 꼬리 시간 |

### 1.3 Staff · 리더십 (L01–L04) — 7항목, 14점 만점

| # | 항목 | 0점 | 1점 | 2점 |
|---|---|---|---|---|
| 1 | 구조 (7칸 = STAR 확장) | 시간순 나열 | S-T-A-R 중 일부 | ①틀 → ②상황 → ③선택지 → ④결정 → ⑤결과 → ⑥남긴 장치 → ⑦Rivian 적용 |
| 2 | 내 역할 | "we" 만 | 섞임 | "I decided / I built" 가 분명 + 팀 기여도 인정 |
| 3 | 숫자 | 없음 | 1개 | `[숫자 채우기]` 칸 전부 실제 숫자로 |
| 4 | Staff 범위 | 내 태스크 | 팀 하나 | 여러 팀 · 분기 단위 · 메커니즘(도구·프로세스)을 남김 |
| 5 | 갈등 · 설득 | 회피 | 의견으로 | 실험 + 판정 규칙 선합의 + 데이터 |
| 6 | 배운 점 · 적용 | 없음 | 일반론 | Rivian A0/HAPS 상황에 구체적으로 연결 |
| 7 | 길이 | 1분 미만 또는 4분 초과 | 3–4분 | 2–2.5분 + 꼬리 대비 |

### 1.4 녹음 채점 방법 (5분)

1. 녹음을 1.5배속으로 듣는다. 7단계가 **시작된 시각**을 적는다 (예: ① 0:00 ② 0:40 ③ 1:50 …).
2. ③ 구간에서 말한 카테고리 약어에 체크. 빠진 것 = §8 "빠진 카테고리" 칸.
3. 말한 숫자를 센다 (BER, rule of three, 온도, 클럭, 퍼센트). 3개 미만이면 #6 · #11 감점.
4. "um / like / so basically" 가 분당 3회 넘으면 #11 = 0.
5. 가장 낮은 항목 1개만 "다음에 고칠 한 가지"로 적는다 — 여러 개 고치려 하면 아무것도 안 고쳐진다.

## 2. 질문 목록 한눈에

| ID | Q (요약) | 묶음 | 링크 |
|---|---|---|---|
| [T01 ★](#t01-i2c-controller) | I2C controller (4 instances: serializers, PMIC, temp sensors) | 블록 | [N06C](2026-11-01_N06C_plans_peripherals_soc_infra.html) C1 · [BV10](2026-11-01_BV10_board_interface_tests.html) · [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html) |
| [T02](#t02-interrupt-controller-integration) | GIC-class interrupt controller + hundreds of interrupt lines | 블록 | [N06C](2026-11-01_N06C_plans_peripherals_soc_infra.html) C6 |
| [T03](#t03-watchdog-and-timers-on-an-asil-part) | Watchdog + timers on an ASIL-rated part | 블록 | [N06C](2026-11-01_N06C_plans_peripherals_soc_infra.html) C5 · [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F4 · [BV15](2026-11-01_BV15_automotive_board_considerations.html) |
| [T04 ★](#t04-dma-engine-on-the-camera-to-npu-path) | Multi-channel DMA on the camera → NPU path | 블록 | [N06D](2026-11-01_N06D_plans_dma_memory.html) D1 · [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) |
| [T05 ★](#t05-lpddr5-memory-controller-and-phy) | LPDDR5/5X controller + PHY — beyond "run a memtest" | 블록 | [N06D](2026-11-01_N06D_plans_dma_memory.html) D2 · [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html) |
| [T06](#t06-noc-interconnect-and-qos) | NoC / interconnect QoS with ISP, NPU, encoder sharing DRAM | 블록 | [N06D](2026-11-01_N06D_plans_dma_memory.html) D4 |
| [T07](#t07-pcie-controller-in-10-minutes) | PCIe Gen4/5 RC — the plan in 10 minutes | 블록 | [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E1 · [LP14](2026-10-31_LP14_validation_methodology.html) |
| [T08 ★](#t08-automotive-ethernet-mac-phy) | Automotive Ethernet MAC + 1000BASE-T1 PHY + TSN | 블록 | [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E2 · [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) |
| [T09 ★](#t09-csi-2-camera-input-and-isp) | 8 cameras → CSI-2 receivers → ISP | 블록 | [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E3 · [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html) |
| [T10](#t10-sensor-timestamping-and-time-sync) | Hardware timestamping + time sync for sensor fusion | 블록 | [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E4 · [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html) |
| [T11 ★](#t11-in-house-npu) | In-house neural engine on A0 | 블록 | [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F1 · [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) |
| [T12](#t12-power-management-dvfs-thermal) | Power domains · DVFS · thermal throttling | 블록 | [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F2 |
| [M01](#m01-dv-signed-off-why-do-we-need-you) | DV signed off at 100% — why do we need you? | 방법론 | [N06A](2026-11-01_N06A_methodology_test_plan_template.html) |
| [M02 ★](#m02-a0-to-b0-what-does-done-mean) | B0 tape-out in 10 weeks — what's "done" for A0? | 방법론 | [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) |
| [M03](#m03-100-coverage-and-a-bug-escaped) | 100% coverage, bug escaped to the road-test fleet | 방법론 | [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) · [N06G](2026-11-01_N06G_platforms_regression_automation.html) |
| [M04](#m04-how-many-parts-boards-and-runs) | How many parts, boards, runs — give me numbers | 방법론 | [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) · [LP14](2026-10-31_LP14_validation_methodology.html) |
| [M05 ★](#m05-one-test-haps-emulator-a0-slt) | One test on HAPS, emulator, A0, SLT | 방법론 | [N06G](2026-11-01_N06G_platforms_regression_automation.html) |
| [M06](#m06-regression-tiers-and-eco) | Regression tiers: per build, nightly, weekly, ECO/stepping | 방법론 | [N06G](2026-11-01_N06G_platforms_regression_automation.html) · [BV14](2026-11-01_BV14_board_validation_plan_automation.html) |
| [M07](#m07-soc-pvt-characterization-plan) | SoC-level PVT characterization plan | 방법론 | [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) · [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) |
| [M08](#m08-slt-content-with-a-time-budget) | SLT content in a fixed per-part budget | 방법론 | [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F5 · [N06G](2026-11-01_N06G_platforms_regression_automation.html) |
| [D01 ★](#d01-passes-on-haps-corrupted-bands-on-a0) | Passes on HAPS, corrupted line bands on A0 | 디버그 | [N06H](2026-11-01_N06H_debug_methodology_triage.html) · [N06G](2026-11-01_N06G_platforms_regression_automation.html) |
| [D02](#d02-npu-mismatch-only-hot-on-two-parts) | NPU mismatch only at hot, 2 of 10 parts | 디버그 | [N06H](2026-11-01_N06H_debug_methodology_triage.html) · [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) |
| [D03](#d03-nightly-pass-rate-cliff) | Nightly 99% → 93% overnight, 40 unrelated failures | 디버그 | [N06H](2026-11-01_N06H_debug_methodology_triage.html) · [BV14](2026-11-01_BV14_board_validation_plan_automation.html) |
| [D04](#d04-debug-build-passes-release-build-fails) | Debug build passes, release build fails | 디버그 | [N06H](2026-11-01_N06H_debug_methodology_triage.html) |
| [D05](#d05-corruption-only-when-everything-runs-together) | Corruption only when camera + NPU + encoder run together | 디버그 | [N06H](2026-11-01_N06H_debug_methodology_triage.html) · [N06D](2026-11-01_N06D_plans_dma_memory.html) |
| [D06](#d06-two-measurements-disagree) | Scope says 40 ns, software log says 300 ns | 디버그 | [N06H](2026-11-01_N06H_debug_methodology_triage.html) · [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E4 |
| [L01 ★](#l01-a-bug-you-caught-before-silicon) | A bug you caught before silicon (S1) | Staff | [deck §4 S1](2026-10-25_rivian_hm_screen_prep.html) · [N06G](2026-11-01_N06G_platforms_regression_automation.html) |
| [L02](#l02-driving-a-silicon-issue-to-closure) | Convincing design + driving to erratum/fix (S3 · S8) | Staff | [deck §4 S3 · S8](2026-10-25_rivian_hm_screen_prep.html) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) |
| [L03](#l03-infrastructure-other-engineers-adopted) | Infrastructure other engineers adopted (S5 · S2) | Staff | [deck §4 S5 · S2](2026-10-25_rivian_hm_screen_prep.html) · [BV14](2026-11-01_BV14_board_validation_plan_automation.html) |
| [L04](#l04-road-test-issue-under-schedule-pressure) | Road-test issue under schedule pressure (S6 · S4) | Staff | [deck §4 S6 · S4](2026-10-25_rivian_hm_screen_prep.html) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) |

- 블록 프롬프트에서 다루지 않은 N06 블록(C2 SPI · C3 UART · C4 GPIO · D3 SRAM ECC · D5 SMMU · F3 secure boot · F4 안전 · F5 SLT)은 각 페이지에서 매트릭스만 훑는다 — F4는 T03에, F5는 M08에, D3·D5는 T04/T05 꼬리에 일부 들어가 있다. **즉석 연습용**: 아무 블록이나 하나 골라 §1.1 시간표로 10분.

## 3. 블록 테스트 플랜 12

> 모든 블록 답의 ③ 매트릭스는 12 카테고리 순서로 말한다 — 순서를 고정하면 빠뜨린 걸 스스로 알아챈다. 숫자 중 Rivian 구성은 전부 `[추정]`.

### T01 · I2C controller ★

- **Q.** "Our SoC has four instances of a licensed I2C controller. They configure the camera serializers, the PMIC and the temperature sensors. Walk me through your validation plan."
- **꼬리1.** "After a sensor reset, the bus hangs with SDA held low. Where is that in your plan?"
- **꼬리2.** "Timing like tHD;STA and rise time — is that a silicon test or a board test?"
- **꼬리3.** "What would you deliberately not test, because DV already did?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Is it licensed IP, like DesignWare, or ours? Controller only, or target mode too? Which speeds: 100 kHz, 400 kHz, 1 MHz Fast-mode Plus? FIFO and DMA? I'll assume licensed, both modes, up to 1 MHz, with FIFOs and DMA."
- **② Block picture** — "APB register port, TX/RX FIFOs, an IRQ to the GIC, a DMA handshake, and open-drain SCL/SDA pads through the pinmux. Outside: pull-ups, bus capacitance and the partner devices."
- **③ Matrix** — "Registers: reset values, RW and W1C status bits. Function: write, read, repeated start, 7- and 10-bit addresses. Config: speed × address mode × FIFO threshold × DMA on/off. Boundary: zero-length, FIFO-full, multi-KB through DMA. Interrupts: each source raised, masked, cleared. Errors: NACK on address and data, arbitration lost with two controllers, clock stretching past timeout, stuck SDA. Reset/power: reset mid-transfer, clock gating, suspend/resume. Concurrency: all four instances plus DMA traffic. Performance: effective throughput. PVT: timing at corners. Soak: millions of transactions. System: real devices through the Linux driver."
- **④ Priority · platform** — "P0 is register sanity, read/write at 100 and 400 kHz, NACK handling and bus recovery, because camera and PMIC configuration depend on it. Controller logic runs on FPGA first. Pad timing, rise time and glitch filtering only mean something on silicon."
- **⑤ Pass · coverage** — "Zero mismatches in 10^6 transactions per speed. At 400 kHz, tHD;STA of at least 0.6 µs and rise time of at most 300 ns, measured with an analyzer. Recovery succeeds on 100% of injected stuck-bus events. Every requirement ID has a silicon result."
- **⑥ Automation** — "Same C tests through a HAL. A second instance in target mode gives a self-checking loopback with no external device. Runs nightly on the farm."
- **⑦ Risks · A0** — "Pull-ups and capacitance are board-dependent, so I separate IP bugs from board issues with the internal loopback first. On A0 day one: register sanity, scan the bus, read the temperature sensor."
- **꼬리1** — "It's a negative test in the plan. Hold SDA low with a GPIO or a misbehaving target, and check the controller detects it with a timeout status or an interrupt. Then check the recovery sequence: up to nine SCL pulses, then a STOP. Pass means recovery every time and the next transaction works."
- **꼬리2** — "Both. The controller derives timing from counter registers, so on silicon I verify the programmed values give spec timing at the pins. Rise time depends on the pull-up and bus capacitance, so that part is board characterization. I label which is which."
- **꼬리3** — "Exhaustive FSM-state and register-field combinations that DV already covered in simulation. I spend the time on pads, real devices, timing at temperature, and interrupt and DMA integration in the real SoC."
- **한국어 핵심**: 4개 인스턴스 · target 모드 loopback으로 자가 검증 · P0 = NACK + 9클럭 bus recovery · Fast-mode 0.6 µs / 300 ns(UM10204 [1]) · 타이밍 중 rise time은 보드 몫 · DV가 덮은 FSM 조합은 반복 안 함.
- **보러 갈 곳**: [N06C](2026-11-01_N06C_plans_peripherals_soc_infra.html) C1 I2C 블록 전체 · [BV10](2026-11-01_BV10_board_interface_tests.html) (보드 측정 관점) · [BV16 Q06](2026-11-01_BV16_mock_interview_qbank.html) (응답 없는 I2C 디버그) · [V8 i2c scan/recover](hmScreenPrep/problems/V8_i2c_scan_recover/index.html)

</details>

### T02 · Interrupt controller integration

- **Q.** "We use a GICv3-class interrupt controller with a few hundred shared interrupt lines from cameras, Ethernet, PCIe and the NPU. How do you validate interrupt integration on A0?"
- **꼬리1.** "How do you prove every line is wired to the right interrupt ID?"
- **꼬리2.** "How would you measure interrupt latency, and what number would you report?"
- **꼬리3.** "Edge versus level — what bug does that classically cause?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Which GIC version, and how many cores and clusters? Which types are used: SPIs for peripherals, PPIs for per-core timers, SGIs for inter-processor interrupts, LPIs through an ITS for PCIe MSI? Is the integration map from source to ID to trigger type a spreadsheet I can generate tests from?"
- **② Block picture** — "The distributor takes the SPI lines; each core has a redistributor and a CPU interface; an ITS translates MSI writes into LPIs. Sources sit in every clock and power domain."
- **③ Matrix** — "Registers: distributor and redistributor reset values. Function: enable, pending, active, EOI per ID. Config: priority, group, edge or level, affinity to each core. Boundary: lowest and highest IDs, equal priorities. Interrupts: preemption, priority masking. Errors: spurious ID 1023, an interrupt arriving while disabled. Reset/power: wake from a powered-down core. Concurrency: storms from many sources to many cores. Performance: source-to-handler latency. Soak: millions of SGIs. System: per-ID counts under the Linux workload."
- **④ Priority · platform** — "P0 is the connectivity sweep: fire every source and check it arrives with the right ID, trigger type and target core. It's mostly logic, so FPGA catches most of it. Latency, wake-up and clock-domain effects are silicon."
- **⑤ Pass · coverage** — "100% of integration-map entries verified on silicon. Zero lost interrupts in 10^7 SGIs. Latency reported as min/p50/p99/max, with p99 under the budget [추정: from the RT team]."
- **⑥ Automation** — "Tests are generated from the integration spreadsheet, so a map change regenerates them. Per-ID results feed the coverage table."
- **⑦ Risks · A0** — "The risks are integration errors, like swapped IDs or the wrong trigger type, and lost edges across clock domains. I run the sweep on A0 right after register sanity, because every other block's tests depend on interrupts."
- **꼬리1** — "Each source needs a software trigger: a test-interrupt bit in the peripheral, or the real event, like a timer expiring. Where no trigger exists, setting pending in the distributor proves only the GIC side, not the wire. I mark which IDs were proven from the source side."
- **꼬리2** — "Toggle one GPIO at the source event and another as the first instruction of the handler, and capture both on a logic analyzer over thousands of events. Report the distribution, cold and warm cache, idle and loaded. Never just the average."
- **꼬리3** — "A pulse source configured as level gets missed. A level source configured as edge fires once and never again, if the handler doesn't clear the source before EOI. That's the classic 'interrupts stop after the first one' bug."
- **한국어 핵심**: 통합 맵(스프레드시트)에서 테스트 자동 생성 → 모든 소스 발사 · ID/트리거/코어 확인 · set-pending은 GIC만 증명 · 지연은 GPIO 2개 + 분포 · edge/level 혼동 = "첫 번째 이후 안 옴".
- **보러 갈 곳**: [N06C](2026-11-01_N06C_plans_peripherals_soc_infra.html) C6 interrupt controller · [deck §7 #4](2026-10-25_rivian_hm_screen_prep.html) (인터럽트 누락 디버그)

</details>

### T03 · Watchdog and timers on an ASIL part

- **Q.** "Validate the watchdogs and system timers — keep in mind this is an ASIL-rated automotive part."
- **꼬리1.** "How do you show the watchdog never false-trips?"
- **꼬리2.** "How do you measure timer accuracy?"
- **꼬리3.** "What does fault injection mean here, and what's FTTI?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Which watchdogs: per core, system, a window watchdog in a safety island? What's the safety reaction: reset, an interrupt to a safety MCU, an external error pin? What's the FTTI budget? Which clock feeds the watchdog?"
- **② Block picture** — "A counter on an independent reference clock, refresh and window registers with a lock key, a pre-timeout interrupt, and a reset output to the reset controller and the error pin."
- **③ Matrix** — "Registers: lock/unlock sequence, write-once bits. Function: kick in time means no reset; stop kicking means reset, and the reset-reason register says watchdog. Config: timeout × prescaler × window. Boundary: kicks exactly at the window edges, counter wrap. Interrupts: pre-timeout warning. Errors: early kick in window mode, wrong key, write after lock. Reset/power: low-power states, debugger halt. PVT: timeout accuracy over temperature. Soak: days of correct kicking. System: the safety MCU's response."
- **④ Priority · platform** — "P0: it resets the chip and records the reason, and bring-up firmware can service or disable it, or it resets my bring-up. Window edges and accuracy need silicon with the real oscillator."
- **⑤ Pass · coverage** — "Injection-to-reset-pin time inside the FTTI, measured on a scope. 100 of 100 injections detected. Zero false trips over the PVT soak. Period error within the clock spec [추정: ppm], measured against a frequency counter."
- **⑥ Automation** — "The harness has to treat an expected reset as a pass: arm, wait for the reboot, read the reset reason, record it."
- **⑦ Risks · A0** — "The watchdog shouldn't depend on the clock whose failure it must catch, and debugger interaction causes confusion. On A0, control of the watchdog in bring-up firmware comes first."
- **꼬리1** — "Run the whole PVT and load matrix with correct kicking and count resets; zero is the only pass. Also kick at the earliest and latest legal moment of the window, because that's where an off-by-one in the comparator shows."
- **꼬리2** — "Against a calibrated frequency counter or a GNSS-disciplined reference, over a long gate time and across temperature, reported in ppm. 50 ppm sounds small, but it's 4.3 seconds a day."
- **꼬리3** — "Fault injection means creating the fault on purpose: stop kicking, use the wrong key, stop its clock. Then check the whole chain: detected, reported, safe state reached. FTTI is the time from a fault to a possible hazard, so detection plus reaction has to fit inside it."
- **한국어 핵심**: P0 = 리셋 + reset reason + bring-up FW가 제어 가능 · 오탐 0 (PVT soak) · window 가장자리 kick · 50 ppm = 하루 4.3초 · fault injection = 감지 → 보고 → 안전 상태, FTTI 안.
- **보러 갈 곳**: [N06C](2026-11-01_N06C_plans_peripherals_soc_infra.html) C5 timer/watchdog · [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F4 functional safety · [BV15](2026-11-01_BV15_automotive_board_considerations.html) (watchdog fault injection, FTTI) · [BV16 Q30 꼬리2](2026-11-01_BV16_mock_interview_qbank.html)

</details>

### T04 · DMA engine on the camera-to-NPU path ★

- **Q.** "A multi-channel DMA engine moves camera frames from the ISP into LPDDR and feeds tensors to the NPU. Design the test plan."
- **꼬리1.** "How do you test for cache-coherency bugs with DMA?"
- **꼬리2.** "What errors can you actually inject on silicon?"
- **꼬리3.** "What bandwidth do you expect, and how do you know the measurement is right?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "How many channels? Which descriptor format: linked list, ring, scatter-gather? Is there a 2D strided mode for image lines? Is the DMA coherent through the interconnect, or do we do cache maintenance? Is it programmed by firmware or by the Linux driver?"
- **② Block picture** — "A register port, a descriptor fetch engine, per-channel FIFOs, an AXI master into the NoC, and interrupt and error lines to the GIC. It goes through the SMMU. The source is the ISP buffer; the destination is the NPU input in DDR or SRAM."
- **③ Matrix** — "Registers. A single transfer. A size × alignment matrix: 1 byte to max, aligned and unaligned to bus width and cache line, crossing 4 KB. Descriptor chains, scatter-gather, ring wrap, 2D strides. Interrupts: per descriptor, coalescing, end of chain. Errors: decode error, SMMU fault, abort mid-transfer, malformed descriptor. Reset: channel reset in flight. Concurrency: all channels with arbitration, plus coherency. Performance: bandwidth vs size and channels, latency. PVT: data integrity at corners. Soak: hours with a CRC per buffer."
- **④ Priority · platform** — "P0 is a single copy and a short chain with a data compare and a completion interrupt, because the camera and NPU paths need them. FPGA covers descriptor logic. Real bandwidth, DDR latency and coherency timing are silicon."
- **⑤ Pass · coverage** — "Zero mismatches over [추정: 10^9 bytes] per size/alignment bin. Every error class reports the right status, with no bus hang. Bandwidth at least [추정: X%] of the theoretical port rate."
- **⑥ Automation** — "Data patterns carry address and sequence number, so a mismatch says where and which transfer. A random size/alignment generator with a logged seed. Runs nightly."
- **⑦ Risks · A0** — "The risk is coherency and ordering under load, not the happy path. On A0: register sanity, a 4 KB SRAM-to-SRAM copy, then DDR once memory is up."
- **꼬리1** — "Two directions. When the CPU writes and DMA reads, the CPU needs a cache clean first. When DMA writes and the CPU reads, it needs an invalidate, and buffers aligned to cache lines, so a neighboring variable isn't written back over DMA data. I deliberately dirty the lines around the buffer and run with maintenance on and off. The 'off' run must fail; that proves the test can see the bug."
- **꼬리2** — "Illegal programming: an address outside the map, an unmapped SMMU page, an illegal alignment, a bad descriptor length, an abort while running. For each, check the status and error interrupt, no bus hang, and that the channel recovers without a chip reset."
- **꼬리3** — "Theoretical is width × clock; a 128-bit port at 1 GHz is 16 GB/s. Real is lower because of descriptor fetches and DRAM efficiency. I sweep transfer size: small transfers are overhead-bound, and large ones should approach the port or DRAM limit. I validate the timer by timing a known delay, and subtract measurement overhead."
- **한국어 핵심**: 크기 × 정렬 매트릭스(4 KB 경계 · 캐시 라인) · 주소+시퀀스 번호 패턴 · 에러 주입 후 복구 · coherency 테스트는 "maintenance 끄면 반드시 실패"로 검증 · 128 bit × 1 GHz = 16 GB/s.
- **보러 갈 곳**: [N06D](2026-11-01_N06D_plans_dma_memory.html) D1 DMA · [deck §6.2 · §7 #1](2026-10-25_rivian_hm_screen_prep.html) · [V3 DMA descriptor ring](hmScreenPrep/problems/V3_dma_desc_ring/index.html) · [LP08 DMA · IOMMU](2026-10-31_LP08_dma_iommu_coherency.html)

</details>

### T05 · LPDDR5 memory controller and PHY ★

- **Q.** "The SoC has several LPDDR5/5X channels [추정]. What's your validation plan for the memory controller and PHY — and I mean beyond 'run a memtest'."
- **꼬리1.** "Training passes at room temperature. What changes at −40 and at 125?"
- **꼬리2.** "How do you validate frequency switching while traffic is running?"
- **꼬리3.** "How do you measure bandwidth efficiency, and what's realistic?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "LPDDR5 or 5X, and what target data rate? How many channels and ranks? Is there inline ECC? Who owns training: boot firmware or a PHY microcontroller? What temperature grade?"
- **② Block picture** — "The controller (scheduler, refresh, QoS ports) and the PHY with its training engine, then the memory package. Boot firmware runs training and can save the results."
- **③ Matrix** — "Registers. Init and training success rate over many boots, with training results logged per boot. Memory tests: data bus, address bus, March C-, random with seed. Config: every frequency point and rank/channel mode. Boundary: highest addresses, bank/row edges. Interrupts and errors: ECC with injected single- and double-bit errors, error counters. Reset/power: self-refresh entry/exit, power-down, retention. Temperature-driven refresh from MR4 [추정 detail]. Concurrency: QoS with many masters. Performance: efficiency per access pattern, latency. PVT: Vref and timing margin sweeps cold/room/hot. Soak: 72 hours at hot."
- **④ Priority · platform** — "P0 is training at a reduced rate plus a basic memtest, so software can run; then full speed. FPGA uses a different memory model, so nearly everything that matters here is silicon."
- **⑤ Pass · coverage** — "Training succeeds in 100% of [추정: ≥1000] cold boots per board at each temperature. Zero memtest errors over [N] hours. Margin above the agreed minimum at every corner. Bandwidth efficiency per pattern above target."
- **⑥ Automation** — "Training results are logged every boot and trended, because drift is an early warning. Shmoo scripts sweep Vref and delays. Chamber campaigns run on the farm."
- **⑦ Risks · A0** — "Temperature: an eye trained at room shifts at 125. Board SI. On A0: low-speed training, data and address bus tests, then step the speed up."
- **꼬리1** — "Timing drifts with temperature, so the PHY either retrains periodically or tracks, or we lose margin. I train at room and run hot, train hot and run cold, and measure margin each time. At high temperature the device also asks for faster refresh, and the controller has to follow it."
- **꼬리2** — "Stream data with a checker, then trigger thousands of switches at random points, including near self-refresh entry. I look for data errors, stalls longer than the real-time budget (the camera can't wait), and corrupted training state."
- **꼬리3** — "Efficiency is measured bandwidth divided by data rate × bus width. Sequential streams get high efficiency; small random accesses get much lower, because of row misses and read/write turnarounds. I report per pattern, never one number."
- **한국어 핵심**: training 성공률(수천 부팅 × 온도) + 결과 로그 트렌드 · 상온 train → 고온 run 교차 · DFS 중 트래픽 + 지연 예산 · 효율 = 실측 / (rate × 폭), 패턴별 · FPGA로는 거의 못 봄.
- **보러 갈 곳**: [N06D](2026-11-01_N06D_plans_dma_memory.html) D2 DDR · [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html) · [BV16 Q18 · Q19 · Q20](2026-11-01_BV16_mock_interview_qbank.html) (training 디버그 · memtest · ECC — 여기선 반복 안 함) · [V1 March](hmScreenPrep/problems/V1_march_memtest/index.html)

</details>

### T06 · NoC interconnect and QoS

- **Q.** "The CPU clusters, ISP, NPU and video encoder all share DRAM through the NoC. How do you validate the interconnect — especially QoS?"
- **꼬리1.** "The camera path must never drop a frame, even when the NPU saturates DRAM. Prove it."
- **꼬리2.** "How do you test ordering, and decode or timeout errors?"
- **꼬리3.** "Which parts need silicon, and which can be done on emulation?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Crossbar or mesh? What's the QoS mechanism: priorities, regulators, bandwidth limiters? Which masters are real-time, like the ISP and display, and which are best-effort? Is there an architecture performance model to compare against?"
- **② Block picture** — "Masters: CPUs, ISP, NPU, encoder, DMA, PCIe. Targets: DDR channels, SRAM, peripherals. Plus firewalls, a coherency domain for the CPUs, and performance counters."
- **③ Matrix** — "Registers: QoS and firewall config. Function: a connectivity matrix, every master to every allowed target. Config: QoS setting combinations. Boundary: max outstanding transactions. Errors: decode error on unmapped addresses, timeout on a powered-down target, firewall violations. Ordering: same-ID ordering and barriers. Reset/power: a target powered down mid-access. Concurrency: all masters at once, the main event. Performance: per-master bandwidth and latency vs the model. PVT: performance at low-voltage corners. Soak: days at full load."
- **④ Priority · platform** — "P0 is connectivity and error responses, since nothing else works without them. Emulation can preview QoS behavior, but real numbers need silicon, because DRAM timing and clock ratios differ."
- **⑤ Pass · coverage** — "Zero camera frame drops, and the ISP buffer never below its watermark, over [N hours] with the NPU and encoder saturating DRAM. Per-master bandwidth within [추정: ±10%] of the model. Every error gets a response, with no hang."
- **⑥ Automation** — "A traffic generator per master (a DMA channel or an NPU kernel with configurable rate), driven by one scenario file. Interconnect and DDR counters collected per run."
- **⑦ Risks · A0** — "The risk is a corner where a best-effort master starves a real-time one, so scenarios come from vehicle use cases, not just synthetic ones. On A0: the connectivity matrix first."
- **꼬리1** — "Build the worst case on purpose: NPU and encoder at max DRAM bandwidth, CPUs streaming, all cameras at max resolution and frame rate. Measure ISP buffer fill against the watermark, not just 'no drops'. Run for hours at every DDR frequency point, and report the minimum headroom."
- **꼬리2** — "Ordering: a producer writes data then a flag, and the consumer checks sequence numbers, so it must never see the flag before the data. Errors: access unmapped, powered-off and firewalled regions, and check the error response, the logged address, and that the system keeps running."
- **꼬리3** — "Connectivity, error responses and protocol behavior on emulation or FPGA. Contention numbers and real-time headroom only on silicon."
- **한국어 핵심**: 연결 매트릭스 + 에러 응답이 P0 · 핵심은 "모두 동시에" 최악 시나리오 · 측정은 drop 여부가 아니라 **watermark 여유** · 실제 숫자는 실리콘에서만.
- **보러 갈 곳**: [N06D](2026-11-01_N06D_plans_dma_memory.html) D4 cache · coherency · interconnect · [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html)

</details>

### T07 · PCIe controller in 10 minutes

- **Q.** "We have a PCIe Gen4/Gen5 controller in root-complex mode for an external SSD and a companion device [추정]. Give me your plan in ten minutes."
- **꼬리1.** "No endpoint device is available yet. How do you test DMA?"
- **꼬리2.** "How many link-up cycles before you sign off?"
- **꼬리3.** "How do you measure margin, not just 'link up'?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "RC only, or EP mode too? What Gen and width? What's on the other side? Which features are in scope: MSI-X, ASPM and L1 substates, AER, SR-IOV?"
- **② Block picture** — "The licensed controller (transaction, data link and physical layers), the SerDes PHY, refclk and PERST#, the SMMU and interrupt path, and the Linux driver stack."
- **③ Matrix** — "My twelve areas: link bring-up loops, the speed × width matrix, equalization at Gen3 and above, signal quality through error counters and margining, enumeration and BARs, MSI/MSI-X, DMA, performance, power states, resets (hot, warm, FLR, PERST#), error paths with injection and AER, and soak."
- **④ Priority · platform** — "P0: link at Gen1, then target speed, enumeration, config access, a simple DMA. Protocol logic on FPGA with a slower PHY. Equalization, margin and real speeds are silicon."
- **⑤ Pass · coverage** — "Target Gen and width in 100% of about 3000 cycles to claim below 1 in 1000. Zero uncorrectable errors during soak, and correctable errors under threshold. Throughput as a percentage of theoretical. Margin above target on every lane at every corner."
- **⑥ Automation** — "A link-cycle harness: reset trigger, check LTSSM and link status, record every cycle, with lspci and setpci checks in Linux."
- **⑦ Risks · A0** — "Interop with real devices, board SI, reset sequencing. On A0: Gen1 x1 first."
- **꼬리1** — "Make the other end something I control: a second SoC using the Linux PCI endpoint framework, or an FPGA with an XDMA/QDMA endpoint. Then DMA tests are self-checking from both sides."
- **꼬리2** — "Rule of three: zero failures in N cycles bounds the rate below 3/N at 95%. Below 1 in 1000 needs about 3000 clean cycles, spread over parts and boards, and repeated at temperature."
- **꼬리3** — "Gen4 and later define lane margining at the receiver. Software steps the sampling point in time and voltage and counts errors, which gives per-lane margin without a scope. Add equalization preset sweeps and error-counter trends."
- **한국어 핵심**: LP14 12영역을 한 문장으로 · 상대편을 내가 통제(EP framework / XDMA) · 3000 사이클 = 1/1000 · Lane Margining at Receiver.
- **보러 갈 곳**: [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E1 · [LP14](2026-10-31_LP14_validation_methodology.html) (12영역 · 통계 · EP framework) · [LP03 LTSSM](2026-10-31_LP03_link_training_ltssm.html) · [LP13 도구](2026-10-31_LP13_linux_pcie_tools.html) · [N01](2026-10-31_N01_pcie_lspci_ltssm.html)

</details>

### T08 · Automotive Ethernet MAC + PHY ★

- **Q.** "Our SoC has an Ethernet MAC with TSN features and an external 1000BASE-T1 PHY over SGMII or RGMII [추정]. You know I designed Ethernet PHYs before — walk me through how you'd validate it."
- **꼬리1.** "Walk me through your loopback ladder. What does each stage prove?"
- **꼬리2.** "You see zero errors. How do you claim a BER target?"
- **꼬리3.** "How do you validate PTP hardware timestamp accuracy?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "100BASE-T1, 1000BASE-T1 or multi-gig? RGMII or SGMII to the PHY, or an integrated PHY? Which TSN features: 802.1AS time sync, scheduled traffic? Is there a switch on chip, and what's the link partner — a zonal gateway?"
- **② Block picture** — "MAC with DMA rings and checksum offload, PCS, the MAC-PHY interface, the PHY driving one twisted pair, MDIO management, a PTP timestamp unit, and interrupts."
- **③ Matrix** — "Registers, including the PHY over MDIO. Function: link-up with master/slave config, link time, ping. Config: speeds, VLAN, offloads. Boundary: 64 bytes to jumbo. Interrupts and coalescing. Errors: injected bad FCS, runts, oversize, pause frames. Reset: link drop and recovery, cable unplug. Concurrency: all queues plus TSN traffic. Performance: RFC 2544 throughput, latency and frame loss at 64 to 1518 bytes. PVT: BER over temperature, RGMII delay margin. Soak. System: interop with the real switch, and timestamp accuracy."
- **④ Priority · platform** — "P0: MDIO access, link up, MAC loopback, ping. MAC logic on FPGA. RGMII delays, the PHY, BER and cable diagnostics on silicon and boards."
- **⑤ Pass · coverage** — "Zero frame loss at line rate for every RFC 2544 frame size over [N minutes]. Zero errors over 3/BER bits for the BER target. Timestamp offset within [추정: ±X ns] of a reference."
- **⑥ Automation** — "A hardware traffic generator or a second SoC, scripted. Counters read with ethtool -S, results per frame size, nightly."
- **⑦ Risks · A0** — "Interop and EMC, plus RGMII delay settings, which are a classic gap between board and IP. On A0: MDIO and loopbacks first."
- **꼬리1** — "Each stage adds one piece. MAC loopback proves DMA and MAC logic. PCS loopback adds encoding. PHY loopback adds the PHY digital and the MAC-PHY interface, so RGMII/SGMII timing. External loopback or a link partner adds the analog front end and the cable. The first stage that fails localizes the problem."
- **꼬리2** — "Zero errors in N bits bounds BER below about 3/N at 95% confidence. For 1e-10 that's 3×10^10 bits, 30 seconds at 1 Gb/s. For 1e-12 it's 3×10^12 bits, about 50 minutes. Then repeat at temperature and cable lengths."
- **꼬리3** — "Compare against an independent reference. Feed a PPS or known-time event into the timestamp capture input, or sync to a reference grandmaster and compare clocks. Log offset for hours and report p99 and max. Details in T10."
- **한국어 핵심**: loopback 사다리(MAC → PCS → PHY → 외부) · RFC 2544 7개 프레임 크기 64–1518 [2] · BER 3/N (1e-12 = 1 Gb/s에서 50분) · RGMII 지연 = 보드↔IP 틈 · 면접관 PHY 출신 → PCS/PHY 용어 정확히.
- **보러 갈 곳**: [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E2 · [LP00A Ethernet 기초](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [L3 RFC 2544](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) · [V6 PRBS](hmScreenPrep/problems/V6_prbs_crc_checker/index.html) · [deck §7 #10 · #14](2026-10-25_rivian_hm_screen_prep.html)

</details>

### T09 · CSI-2 camera input and ISP ★

- **Q.** "Eight cameras come in over serializer links to CSI-2 D-PHY receivers, then the ISP [추정 구성]. Design the validation plan for the camera input pipeline."
- **꼬리1.** "Real sensors aren't ready on A0 day one. What do you use?"
- **꼬리2.** "How do you validate ISP output when there's no single 'right' image?"
- **꼬리3.** "How do you tell a dropped frame from a duplicated one?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Resolution, frame rate and bit depth per camera? D-PHY or C-PHY, lanes per port, lane rate? Virtual channels used to merge cameras on one port? Where does ISP output go: DDR, NPU, encoder? Is there a bit-exact C model of the ISP?"
- **② Block picture** — "Sensor, serializer, cable, deserializer on the board, then the CSI-2 receiver: PHY, packet header ECC, payload CRC, virtual-channel and data-type demux. Then the frame write to DDR, ISP stages, and the NPU."
- **③ Matrix** — "Registers. PHY: lane counts, lane rates, HS-settle settings, lane skew. Function: data types like RAW12, YUV and embedded data, frame and line counters. Config: resolution × format × VC mapping. Boundary: min and max line lengths, blanking. Interrupts: frame start/end, error. Errors: injected header ECC errors (single-bit corrected, double detected), payload CRC errors, truncated frames, wrong data type. Reset: start/stop the stream thousands of times, sensor reset mid-frame. Concurrency: all cameras at max rate with DRAM loaded. Performance: frame-end to buffer-ready latency. ISP: per-stage golden compare. PVT: lane-rate margin at temperature. Soak: 24 hours on the counters."
- **④ Priority · platform** — "P0: one camera, a test pattern, captured to DDR with the correct size and CRC; then multi-camera. FPGA can run the protocol layer and ISP logic from pattern generators. The D-PHY receiver at real lane rates is silicon only."
- **⑤ Pass · coverage** — "Zero drops and zero CRC errors over 24 hours with all cameras. Continuous frame counters. ISP stages bit-exact with the C model. Latency p99 under the budget [추정]."
- **⑥ Automation** — "Pattern sources with embedded frame numbers and line CRCs, so checking is automatic. Counters dumped per run. Per-stage golden compare."
- **⑦ Risks · A0** — "Real sensors behave differently from generators (blanking, embedded data, start-up timing), and eight cameras stress DRAM. On A0: a single-camera pattern capture."
- **꼬리1** — "A CSI-2 pattern generator: an FPGA board with a CSI-2 transmitter, a commercial generator, or the serializer's built-in test pattern. Known content means the checker can be exact. Real sensors come later for interop."
- **꼬리2** — "Two levels. Each ISP stage is deterministic, so it's bit-exact against the algorithm team's C model. For the full pipeline with real sensors, I use test charts and metrics like color error or PSNR within an agreed tolerance."
- **꼬리3** — "Frame-start packets can carry a frame number, so the checker expects it to increment. A gap is a drop, a repeat is a duplicate. I cross-check with hardware timestamps: the interval should be 1/fps."
- **한국어 핵심**: 패턴 생성기로 시작(정확한 체크) → 실제 센서 · 헤더 ECC/페이로드 CRC 오류 주입 · 프레임 번호 + timestamp 간격으로 drop/dup · ISP는 단계별 bit-exact, 전체는 허용 오차 · D-PHY는 실리콘에서만 · camera는 Don 갭 → 용어 정확히.
- **보러 갈 곳**: [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E3 · [LP00A MIPI 기초](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [V9 CSI-2 packet](hmScreenPrep/problems/V9_csi2_packet_parse/index.html) · [V12 frame timestamp](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) · [deck §7 #8](2026-10-25_rivian_hm_screen_prep.html)

</details>

### T10 · Sensor timestamping and time sync

- **Q.** "Camera, LiDAR and radar data have to be fused on a common time base. How would you validate the SoC's hardware timestamping and time sync?" (면접관 특허 분야 [확인됨: deck §1.2])
- **꼬리1.** "What's your ground truth?"
- **꼬리2.** "How do you validate camera FSYNC timing across eight cameras?"
- **꼬리3.** "The grandmaster disappears. What do you test?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "What's the time source: gPTP over automotive Ethernet from a grandmaster, a GNSS PPS, or both? Where are timestamps taken: PHY, MAC, CSI-2 frame start, a capture unit on a pin? Does the SoC generate camera FSYNC? What accuracy does the fusion team need?"
- **② Block picture** — "Grandmaster over Ethernet to the PTP hardware clock in the MAC. PPS into a capture input. The SoC's system counter, timestamp latches at CSI-2 frame start, and FSYNC outputs to the cameras."
- **③ Matrix** — "Registers. Function: servo locks, PPS captured. Config: sync interval, one- or two-step. Boundary: counter wrap, epoch, leap seconds. Errors: grandmaster loss and holdover, PPS glitches, link down. Reset: time to re-lock. Concurrency: accuracy under full network and camera load. Performance: offset and jitter distribution. PVT: oscillator drift over temperature in holdover. Soak: days of offset logs. System: aligned timestamps in the fused output."
- **④ Priority · platform** — "P0: the hardware clock runs, PPS capture works, FSYNC toggles. FPGA can check the logic; the oscillator, timestamp points and real latencies are silicon."
- **⑤ Pass · coverage** — "Offset to the grandmaster within the fusion budget at p99 and max. FSYNC skew across cameras within [추정: µs-level] on the scope. Holdover drift within [Z] over [T]."
- **⑥ Automation** — "A GNSS-disciplined reference and a time-interval counter in the farm, logging offset every second. Long-run plots per build."
- **⑦ Risks · A0** — "Measuring software timestamps instead of hardware ones, and asymmetric path delays. On A0: PPS capture compared to the scope."
- **꼬리1** — "An independent reference. A GNSS-disciplined PPS goes into both the SoC capture input and a scope. The SoC toggles a GPIO at a programmed time, and the scope measures the error. Optically, an LED strobe at a known time, seen by the camera, checks exposure-to-timestamp end to end."
- **꼬리2** — "Scope probes on the FSYNC lines at the camera connectors, measuring skew across cameras and against PPS. Then check the data side: per-camera frame-start timestamps should show the same alignment."
- **꼬리3** — "Holdover. Kill the grandmaster, measure the drift rate, and check that an 'unsynchronized' status reaches consumers. Then reconnect: it should re-lock without a silent time jump, or flag the step."
- **한국어 핵심**: 기준 = GNSS PPS + 스코프 / LED 스트로브 · 하드웨어 timestamp vs 소프트웨어 timestamp 구분 · holdover 드리프트 + 재동기 시 점프 표시 · 면접관 특허 → 꼬리 깊을 가능성.
- **보러 갈 곳**: [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E4 · [L4 PPS discipline](hmScreenPrep/problems/L4_pps_time_discipline/index.html) · [deck §6.6 · §7 #13](2026-10-25_rivian_hm_screen_prep.html) (sensor-to-inference 지연 · 1초 차이 디버그)

</details>

### T11 · In-house NPU ★

- **Q.** "How would you validate our in-house neural engine on A0 — functionally, for performance, and for robustness?" (RAP1 계열 자체 NPU [확인됨: context])
- **꼬리1.** "Bit-exact or tolerance — how do you decide pass/fail for INT8 versus FP16?"
- **꼬리2.** "Measured throughput is 40% of the advertised TOPS. Is the chip broken?"
- **꼬리3.** "How do you validate the NPU under thermal throttling?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Which data types: INT8, INT4, FP16? Is there a bit-exact simulator from the compiler team? How are jobs submitted: a command queue, firmware on a controller? How big is local SRAM? Which networks matter most: the driving model? Any ECC or safety mechanisms?"
- **② Block picture** — "Command processor, MAC arrays, vector and activation units, local SRAM with DMA to DDR through the NoC, interrupts, its own power and clock domain with DVFS."
- **③ Matrix** — "Registers. Function: each operator (conv, matmul, pooling, activations) bit-exact against the model. Config: shapes, strides, padding, layouts, tile sizes. Boundary: partial tiles, max dimensions, saturation and rounding. Interrupts: completion and error. Errors: illegal commands, memory faults, ECC injection, a hang watchdog, recovery without a chip reset. Reset/power: power-gating between jobs, DVFS mid-job. Concurrency: multiple contexts plus camera DRAM traffic. Performance: per-layer utilization, latency, inferences per second, TOPS per watt. PVT: Vmin and Fmax with real networks. Soak: days of inference with output checksums. System: end-to-end accuracy of the driving network."
- **④ Priority · platform** — "P0: one small conv bit-exact, job submit and completion interrupt, then one full network. Function on emulation and FPGA. Performance, power, thermal and Vmin are silicon."
- **⑤ Pass · coverage** — "Integer paths bit-exact across the operator suite and [N] random shapes. Network accuracy within [Δ] of reference. Per-layer utilization compared to the compiler's prediction. Vmin with real networks above the guardband."
- **⑥ Automation** — "A random-shape generator with a seed, golden from the model, perf counters captured per layer, nightly."
- **⑦ Risks · A0** — "A full-load NPU can droop the rail, so errors show only with certain networks. Throttling distorts perf numbers. On A0: one conv, bit-exact."
- **꼬리1** — "Integer paths are deterministic, so hardware and model must agree bit for bit; any difference is a bug. Floating point can legitimately differ with accumulation order, so I agree a per-operator tolerance with the compiler team and still require run-to-run determinism."
- **꼬리2** — "Not necessarily. Peak TOPS assumes every MAC busy every cycle, often with sparsity counted in. I check per-layer utilization against the roofline: memory-bound layers are limited by DDR bandwidth. If a compute-bound layer is far below its roofline, that's a hardware or compiler issue to dig into."
- **꼬리3** — "Run a sustained network with heat applied, logging temperature, frequency and throughput over time. Throttling should trigger at the right thresholds, outputs should stay bit-exact through frequency changes, and the reported perf is the steady state, not the cold first minute."
- **한국어 핵심**: 연산자별 golden(INT bit-exact, FP 허용 오차 + 결정성) · partial tile · hang 복구 · 성능은 레이어별 utilization vs roofline · 실제 네트워크가 Vmin을 정한다(전압 droop) · throttling 중 결과 불변.
- **보러 갈 곳**: [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F1 · [deck §6.3 · §7 #7](2026-10-25_rivian_hm_screen_prep.html) · [V11 NPU INT8 golden](hmScreenPrep/problems/V11_npu_int8_golden/index.html) · [V4 perf stats](hmScreenPrep/problems/V4_perf_bench_stats/index.html)

</details>

### T12 · Power management, DVFS, thermal

- **Q.** "Validate the SoC's power management: power domains, DVFS for the CPUs and the NPU, thermal sensors and throttling."
- **꼬리1.** "How do you test DVFS transitions with traffic in flight?"
- **꼬리2.** "How do you validate thermal sensor accuracy?"
- **꼬리3.** "Suspend/resume passed 100 times. Are you done?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "How many switchable domains? Who sequences them: PMU firmware or a hardware controller? Which operating points? Is the PMIC on I2C or SPMI? How many thermal sensors, and where? What low-power modes does a parked car use?"
- **② Block picture** — "The power controller, per-domain switches with isolation and retention, the clock controller and PLLs, the voltage loop with the PMIC, and thermal sensors with alarm interrupts."
- **③ Matrix** — "Registers. Function: each domain's on/off sequence, isolation, retention restored. Config: every operating point and every transition between them. Boundary: fastest transition rate, voltage limits. Interrupts: thermal alarms, power-good. Errors: PMIC fault, over-temperature shutdown, a request during a transition. Reset/power: thousands of suspend/resume cycles, every wake source. Concurrency: DVFS during DMA and NPU traffic. Performance: transition latency, power per mode. PVT: Vmin per operating point. Soak. System: vehicle power modes."
- **④ Priority · platform** — "P0: default domains on, PMIC communication, one safe operating point. This block is almost entirely silicon, since FPGA has no real power switches."
- **⑤ Pass · coverage** — "Zero failures in about 3000 suspend/resume cycles to claim below 1 in 1000. Zero data errors across [N] DVFS transitions with traffic. Thermal sensors within [추정: ±X °C] of reference. Power per mode within budget."
- **⑥ Automation** — "A power analyzer on the main rails, time-aligned with test markers. Chamber control and a cycle harness."
- **⑦ Risks · A0** — "Retention bugs only show after a real power-off. Droop during transitions. On A0: rails, then one operating point."
- **꼬리1** — "Run data-integrity workloads (DMA with checksums, an NPU job with golden), switch operating points at random times thousands of times, and check for errors and stalls. On a scope, check the order: voltage up before frequency up, frequency down before voltage down."
- **꼬리2** — "Soak the part at several setpoints on a thermal forcer or in a chamber, and compare each on-die sensor with the reference thermocouple. Report the offset per sensor and per part, then check the alarm thresholds fire at the right temperature."
- **꼬리3** — "100 clean cycles only bounds the failure rate below 3%. A parked car suspends daily for a decade, around 3650 times, so I want thousands of clean cycles across parts and temperatures."
- **한국어 핵심**: 거의 전부 실리콘 · DVFS 순서(전압↑ 후 주파수↑, 주파수↓ 후 전압↓) + 트래픽 중 전환 · 온도 센서 = forcer/chamber 기준 비교 · 100회 = 3% 상한일 뿐.
- **보러 갈 곳**: [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F2 · [BV03 전원](2026-11-01_BV03_power_tree_sequencing_pmic.html) · [BV12 PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)

</details>

## 4. 방법론 8

> 방법론 답도 7칸을 쓴다: ① 질문의 범위 확인 → ② 큰 그림 → ③ 항목 목록(매트릭스) → ④ 우선순위 → ⑤ 기준/숫자 → ⑥ 자동화 → ⑦ 리스크. 6–8분이 목표.

### M01 · DV signed off — why do we need you?

- **Q.** "DV signed off this block with 100% functional coverage. Why do we still need you, and what goes in your plan that isn't in theirs?"
- **꼬리1.** "Give me an example of a bug only silicon can show."
- **꼬리2.** "How do you reuse what DV built?"
- **꼬리3.** "How do you get the designer to actually review your plan?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "What did their coverage include: block-level or full-chip, assertions, gate-level sims? What did they waive?"
- **② Picture** — "DV proves the RTL matches the spec, in simulation, with models of everything around it. Validation proves the real chip on a real board, with real devices, firmware and conditions."
- **③ Matrix** — "Things only silicon shows: analog and mixed signal (PHYs, PLLs, DDR training); timing and clock-domain issues at real frequency; power delivery and droop; temperature; part-to-part variation; the real software stack; and rare events, billions of cycles that simulation never reaches."
- **④ Priority** — "So I deliberately skip exhaustive register and state checks they already did, and spend time on interfaces, PVT, concurrency and long runs."
- **⑤ Criteria** — "I still want silicon evidence per requirement. A short smoke set proves the DV-covered features also work on the real part."
- **⑥ Automation** — "Reuse DV assets: their sequences become C tests, and their reference models become my golden."
- **⑦ Risk** — "The risk is a gap between the two plans. I review mine against their coverage report so the gap is explicit."
- **꼬리1** — "A clock-domain crossing that's fine in simulation but fails at real frequency at a low-voltage corner. Or a DDR eye that collapses at hot. Neither exists in RTL simulation."
- **꼬리2** — "Ask for their verification plan and coverage report, and map their features to my requirement IDs. Port their directed sequences and reference models. Their waiver list becomes my priority list."
- **꼬리3** — "Make it cheap: one page per block, specific questions like 'is this mode supported, what's the max?', and always ask which corner cases worry them. A designer's worry is the best test idea."
- **한국어 핵심**: DV = RTL vs spec (시뮬), validation = 실제 칩 · 실리콘에서만: 아날로그 · 실주파수 타이밍 · 전원 · 온도 · 칩 편차 · 실제 SW · 희귀 사건 · DV waiver = 내 우선순위.
- **보러 갈 곳**: [N06A](2026-11-01_N06A_methodology_test_plan_template.html) ("DV와 중복 피하기" 표, 검증 용어 5개) · [BV01](2026-11-01_BV01_si_board_validation_big_picture.html) · [deck §0.5.6](2026-10-25_rivian_hm_screen_prep.html) (RTL 이야기 나올 때)

</details>

### M02 · A0 to B0 — what does "done" mean?

- **Q.** "We tape out B0 in ten weeks. What data do you bring to that decision, and how do you define 'validation done' for A0?"
- **꼬리1.** "The bug find rate is still rising. What do you tell the program?"
- **꼬리2.** "One P1 feature hasn't been tested yet. Do we tape out?"
- **꼬리3.** "What's a waiver, and who signs it?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Is B0 a metal-only ECO or a full-layer change? When is the ECO input cutoff? Who owns the decision?"
- **② Picture** — "The decision has two parts: which A0 bugs B0 must fix, and whether anything untested could need a silicon fix. Validation's job is to shrink that second list."
- **③ Matrix** — "I bring requirement coverage per block (executed, passed, waived), open bugs by severity and root-cause status, errata with workaround status, untested areas with a risk rating, a PVT margin summary, and the find-rate trend."
- **④ Priority** — "In the ten weeks, first finish the areas where a bug can only be fixed in silicon (analog, safety mechanisms, boot ROM, anything without a software workaround)."
- **⑤ Criteria** — "Exit criteria agreed up front: all P0/P1 requirements executed, passing or waived with an owner; no open S1 without root cause; every erratum either has a verified workaround or is in the ECO; PVT corners complete; regression stable for N days."
- **⑥ Automation** — "All of that comes out of the results database weekly. Nobody builds a spreadsheet the night before."
- **⑦ Risk** — "If we're not done at the cutoff, I don't just say 'not ready'. I say exactly which criteria are open and what each risks, and the program decides with data."
- **꼬리1** — "A rising find rate means the curve hasn't bent yet. I'd show where the new bugs are. If they cluster in one block, I'd propose a targeted push there before the cutoff, and say plainly that maturity, not coverage, is the issue."
- **꼬리2** — "It depends on whether a bug there could only be fixed in silicon. If software can work around it, accept the risk with a waiver. If not, test it now or explicitly accept the respin risk, in writing."
- **꼬리3** — "A documented decision not to meet a criterion, with the reason, risk, mitigation and approver. It's signed by the requirement owner and the program, not by validation alone."
- **한국어 핵심**: B0 결정 = "고칠 것" + "아직 모르는 것" · 실리콘에서만 고칠 수 있는 영역 먼저 · 종료 기준은 시작 전에 합의 · 마감에 못 끝나면 열린 기준 + 리스크를 숫자로 · waiver = 사유·리스크·승인자.
- **보러 갈 곳**: [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) (종료 기준 예시 · 메트릭 · 주간 리포트) · [BV14](2026-11-01_BV14_board_validation_plan_automation.html) (Staff 시그널 문장 · 주간 리포트) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) (errata 수명주기)

</details>

### M03 · 100% coverage and a bug escaped

- **Q.** "Your dashboard says 100% requirement coverage, but a bug escaped to the road-test fleet. What went wrong, and how do you fix the process?"
- **꼬리1.** "How do you test the test?"
- **꼬리2.** "What does cross coverage mean in silicon validation?"
- **꼬리3.** "How do you do traceability without a heavyweight tool?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "What escaped, under what conditions, and which requirement did it belong to?"
- **② Picture** — "Coverage counts requirements with a passing test. A passing test doesn't prove it would fail on a bug."
- **③ Matrix** — "How coverage lies: the check doesn't really check, for example it reads a cached value or compares a buffer with itself. The requirement is too coarse: 'DMA works' covers a thousand conditions. A missing cross: tested, but never at hot with concurrent traffic. Tested on one part only. An FPGA result counted as silicon. A flaky test retried into green."
- **④ Priority** — "Fix the common ones first: negative controls, and splitting coarse requirements into condition-level ones."
- **⑤ Criteria** — "Coverage per requirement × platform × condition. A test only counts if its negative control fails."
- **⑥ Automation** — "Mutation runs: break the check or inject the fault on purpose, and confirm the test goes red, every week."
- **⑦ Risk** — "Process: every escape gets a 'why did the plan miss it' review, a new regression test, and an update to the plan template."
- **꼬리1** — "Run it on something known-bad: corrupt one byte, disable a clock, flip a CRC bit, and it must fail. Also check that every comparison reads the hardware, not a variable."
- **꼬리2** — "Which combinations of feature and condition have evidence, say each DMA mode × each temperature × with and without concurrency. A heat map shows the holes."
- **꼬리3** — "A spreadsheet or YAML of requirement IDs; tests print the IDs in their result line; a script joins results. The tool doesn't matter, the join does."
- **한국어 핵심**: 커버리지가 거짓말하는 6가지(가짜 체크 · 거친 요구사항 · cross 누락 · 칩 1개 · FPGA를 실리콘으로 셈 · flaky를 retry로 green) · negative control 없으면 카운트 안 함 · escape마다 계획 리뷰.
- **보러 갈 곳**: [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) ("커버리지 100%인데 버그" · traceability) · [N06G](2026-11-01_N06G_platforms_regression_automation.html) (test of the test · mutation) · [deck §5.1 Q7 · Q10](2026-10-25_rivian_hm_screen_prep.html) · [V5 runner/coverage](hmScreenPrep/problems/V5_test_runner_coverage/index.html)

</details>

### M04 · How many parts, boards and runs?

- **Q.** "How many parts, how many boards and how many runs do you need before you sign off a block? Give me numbers."
- **꼬리1.** "Zero failures in 500 runs. What can you claim?"
- **꼬리2.** "One board 3000 times, or 30 boards 100 times each?"
- **꼬리3.** "What are corner lots, and why would you ask for them?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "What's the claim: 'it works', 'failure rate below X', or 'margin above Y across process'?"
- **② Picture** — "Three dimensions. Runs per part find rare events. Parts find process variation. Boards separate board effects from silicon."
- **③ Matrix** — "Bring-up: 3–5 parts. Validation: 8–20 parts, including fast and slow corner lots. Characterization: more parts across lots [추정: ~30+]. Boards: at least three, so one bad board can't look like a silicon bug."
- **④ Priority** — "Spend on the dimension of risk: analog margin needs more parts and corners; a rare digital race needs more runs."
- **⑤ Criteria (math)** — "Rule of three: zero failures in N independent runs bounds the rate below about 3/N at 95%. Below 1 in 1000 needs about 3000 clean runs. At 30 seconds a cycle, that's 25 hours on one board, or overnight on three."
- **⑥ Automation** — "That's why the harness exists: cycles spread across parts and boards, every run tagged."
- **⑦ Risk** — "Independence. 3000 runs on one part say little about the next part, so I stratify by part, board and condition."
- **꼬리1** — "3 over 500 is 0.6%. So I can say the rate is below about 1 in 167 with 95% confidence, not that it's zero."
- **꼬리2** — "They answer different questions. 3000 on one board bounds that board's rate. 30 × 100 shows whether the rate varies; if failures cluster on one board, that's a bad board, not a rate. I'd do the second, then deep-loop any outlier."
- **꼬리3** — "Wafers deliberately processed at the fast or slow edge of the process window (SS, FF, sometimes the skewed corners). They show whether margin holds across the variation production will see."
- **한국어 핵심**: 3/N (95%) · 1/1000 = 3000회 = 30 s/회면 25시간 · bring-up 3–5 / validation 8–20 + 코너 lot / 보드 ≥3 · 독립성 → stratify · 0/500 = "< 1/167".
- **보러 갈 곳**: [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) (계산 연습 4개) · [LP14](2026-10-31_LP14_validation_methodology.html) (rule of three 표, Clopper-Pearson) · [BV16 Q27 꼬리2](2026-11-01_BV16_mock_interview_qbank.html)

</details>

### M05 · One test: HAPS, emulator, A0, SLT ★

- **Q.** "Today we run on HAPS prototypes, A0 comes in six months, and later SLT needs content. How do you architect test content so it's written once?"
- **꼬리1.** "What breaks when a test moves from HAPS to silicon?"
- **꼬리2.** "A block doesn't exist on the FPGA image. Then what?"
- **꼬리3.** "What does your result line look like, and why?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Which platforms: HAPS, an emulator, A0 boards, an SLT handler? Same CPU and memory map on all? What's missing on HAPS: PHYs, the DDR PHY, some analog?"
- **② Picture** — "Three layers. Test logic on top; a thin HAL in the middle; a platform config at the bottom with addresses, clock rates, timeouts, and which blocks exist."
- **③ Matrix** — "Differences to absorb: speed (HAPS can be 10–100× slower, so timeouts scale), memory map and sizes, stubs for absent analog, reset and clock sequences, the console and debug channel."
- **④ Priority** — "Pass/fail logic must be platform-independent. Platform knowledge lives only in the config."
- **⑤ Criteria** — "A test is portable when it passes on FPGA and runs unchanged on silicon: same source, ideally the same binary with a config blob."
- **⑥ Automation** — "One result-line format everywhere, parsed by the same tools, so coverage merges across platforms. SLT reuses a subset with production limits."
- **⑦ Risk** — "Tests that accidentally depend on FPGA slowness, races that never show there. Mitigation: randomize delays on HAPS; run at several frequencies on silicon."
- **꼬리1** — "Timing assumptions: delay loops counted in iterations, polls without timeouts. Analog stubs replaced by real PHYs. Initial values: FPGA memory may start zeroed, silicon SRAM is random. And the real clock tree and reset timing."
- **꼬리2** — "The HAL reports capabilities, and the test reports 'skipped: not present'. Coverage shows it as not run on that platform, never as a pass."
- **꼬리3** — "One line per test: test ID, requirement IDs, platform, chip ID, voltage, frequency, temperature, seed, build hash, verdict and duration. Machine-parsable, so the same parser feeds coverage everywhere, and any failure is reproducible."
- **한국어 핵심**: 3층(테스트 · HAL · 플랫폼 config) · timeout 스케일 · 없는 블록 = SKIP(≠PASS) · 결과 한 줄 형식 통일 · FPGA 느림에 숨은 race → HAPS 랜덤 지연 · Rivian은 "first silicon 훨씬 전에 HW prototyping 플랫폼에서 SW" [확인됨: context].
- **보러 갈 곳**: [N06G](2026-11-01_N06G_platforms_regression_automation.html) (HAL · 테스트 프레임워크 · FPGA vs Si 표) · [deck §5.1 Q2 · Q3 · §6.1](2026-10-25_rivian_hm_screen_prep.html) · [BV16 Q02](2026-11-01_BV16_mock_interview_qbank.html) (보드 쪽 readiness — 겹치는 부분)

</details>

### M06 · Regression tiers and ECO

- **Q.** "Design the regression strategy: what runs on every build, nightly, weekly, and when a metal ECO or a new stepping lands?"
- **꼬리1.** "What's your flaky-test policy?"
- **꼬리2.** "Nightly takes 14 hours on 20 boards and has to fit in 8. What do you cut?"
- **꼬리3.** "How does auto-bisect work?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "How many boards and tests? How long is the full suite? How often do firmware and test builds land? Is an ECO coming?"
- **② Picture** — "A pyramid: smoke per build, nightly, weekly full, PVT campaigns, and triggered sets for ECOs and steppings."
- **③ Matrix** — "Smoke, 10–15 minutes: boot, register sanity, one test per block. Nightly: all P0/P1 functional tests on several boards. Weekly: everything plus long soaks. PVT campaign: per stepping. ECO: tests traced back from the changed blocks, plus smoke and the full suite on the new stepping, tagged by stepping."
- **④ Priority** — "Inside a run: fast-fail tests first, recently changed and highest-risk areas first."
- **⑤ Criteria** — "Nightly first-pass rate, flaky rate under a threshold [추정: ~2%], time from commit to result."
- **⑥ Automation** — "Auto-bisect on new failures, signature dedup into tickets, a health check before every run."
- **⑦ Risk** — "Suites grow past the farm. I prune tests that never fail and overlap others."
- **꼬리1** — "A retry that passes marks the test flaky, tracked separately and never counted as a pass. Flaky tests get an owner and a deadline. Quarantined tests still run but don't gate."
- **꼬리2** — "Cut by value. Move tests that haven't failed in months and overlap others to weekly. Parallelize better across boards. Move long soaks to weekly. Select by what changed."
- **꼬리3** — "Test X is red at build N and green at N−k, so binary-search the builds between on an idle board: about log2(k) runs, and the first bad commit goes on the ticket. For a flaky failure, each step needs enough reruns to be confident."
- **한국어 핵심**: smoke(빌드마다) · nightly(P0/P1) · weekly(전체 + soak) · PVT 캠페인 · ECO = 추적성 역방향 + stepping 태그 · retry PASS = flaky(≠PASS) · bisect ≈ log2(k).
- **보러 갈 곳**: [N06G](2026-11-01_N06G_platforms_regression_automation.html) (회귀 계층 · flaky · bisect) · [BV14](2026-11-01_BV14_board_validation_plan_automation.html) (ECO 회귀 · 보드 팜) · [BV16 Q28](2026-11-01_BV16_mock_interview_qbank.html) (보드 팜 설계 — 여기선 반복 안 함)

</details>

### M07 · SoC PVT characterization plan

- **Q.** "Plan PVT characterization for the SoC. Which blocks, which axes, how many parts, and how do you report it?"
- **꼬리1.** "What's temperature inversion, and why does it matter for us?"
- **꼬리2.** "What's guardband, and who uses your Vmin data?"
- **꼬리3.** "How do you keep characterization from taking six months?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "Is the goal setting production Vmin, Fmax and guardbands, or checking interface margins? Which operating points ship? How many corner-lot parts are there? What's the range? AEC-Q100 Grade 1 is −40 to 125 °C ambient."
- **② Picture** — "Axes: process (SS/TT/FF parts), voltage per rail, temperature, frequency. Outputs: shmoo plots, Vmin per block per temperature, margin tables, distributions across parts."
- **③ Matrix** — "CPU and NPU Vmin and Fmax with real workloads; DDR interface margins; SerDes margins for PCIe and Ethernet; PLL lock range; SRAM Vmin; sensor accuracy (thermal, voltage monitors)."
- **④ Priority** — "First the shipping operating points, and the blocks with the least pre-silicon timing margin. Coarse sweeps everywhere, fine sweeps only near boundaries."
- **⑤ Criteria** — "Margin at or above the agreed guardband at every corner, reported as a distribution over parts, not just the worst part."
- **⑥ Automation** — "Python drives the supplies, the thermal forcer or chamber, and on-chip registers. The device runs a test set at each point, and the database renders shmoos automatically."
- **⑦ Risk** — "Temperature inversion: at advanced nodes and low voltage, cold can be the slow corner. Never assume hot is worst."
- **꼬리1** — "At low voltage, threshold-voltage effects dominate, so transistors get slower at cold. Vmin may be set by cold, not hot. At 5 nm that's expected, so every Vmin search runs at both extremes."
- **꼬리2** — "Guardband is the margin between the characterized limit and the operating point. It covers aging, measurement error, and noise not present in the lab. The product and test team set ATE/SLT limits from it, and the power team sets DVFS tables."
- **꼬리3** — "Binary-search Vmin instead of full grids, use coarse-then-fine sweeps, parallelize parts across chambers, and start with fewer parts, expanding only where the distribution looks wide."
- **한국어 핵심**: 축 4개(P · V · T · F) · 블록별 Vmin/Fmax + 인터페이스 margin · 분포로 보고 · 온도 역전(저전압 · 저온이 느림) · guardband 소비자 = 테스트팀(ATE/SLT 한계) + 전원팀(DVFS 표) · 이분 탐색으로 시간 절약.
- **보러 갈 곳**: [BV12 shmoo · PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html) · [deck §5.1 Q4 · §4 S2](2026-10-25_rivian_hm_screen_prep.html) · [V7 shmoo](hmScreenPrep/problems/V7_shmoo_margin/index.html) · [BV16 Q23](2026-11-01_BV16_mock_interview_qbank.html) (chamber shmoo 실행 — 여기선 계획 수준)

</details>

### M08 · SLT content with a time budget

- **Q.** "Production SLT has about a minute per part [추정 숫자]. Which of your validation tests go in, and how do you choose?" (면접관 업무 = production SLT [확인됨: deck §1.1])
- **꼬리1.** "How do you know an SLT test is earning its time?"
- **꼬리2.** "SLT catches a failure that ATE missed. What happens next?"
- **꼬리3.** "How do validation and SLT share code?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Clarify** — "What's the time budget, and what does ATE already cover (scan, MBIST, parametrics)? What's the DPPM target? What does the SLT platform look like, a handler with a socketed board?"
- **② Picture** — "ATE finds structural defects fast. SLT runs real workloads in a real system and catches what scan misses: at-speed paths, interactions, power delivery."
- **③ Matrix** — "Candidates: boot, DDR training plus a stress pattern, an NPU network with golden at stressed voltage and temperature, PCIe and Ethernet link-up with traffic, a camera loopback, power-mode transitions."
- **④ Priority** — "Choose by unique catch: tests that found failures in validation that ATE didn't. Rank by unique failures caught per second."
- **⑤ Criteria** — "Each test's limits come from characterization data, for example a reduced voltage to screen weak parts, and its value is tracked."
- **⑥ Automation** — "The same test source as validation through the HAL. SLT is a configuration of validation content, not a rewrite."
- **⑦ Risk** — "Overkill, failing good parts with tight limits, versus escapes. Field returns and validation failures feed content updates."
- **꼬리1** — "Track unique fails per test, the parts only that test caught, over volume. If a test hasn't uniquely caught anything in a large volume, shorten or drop it."
- **꼬리2** — "Retest to confirm, then failure analysis. Ask if ATE could catch it cheaper and feed a pattern back to the DFT/ATE team. Check validation data for a systematic issue that hit more parts."
- **꼬리3** — "SLT runs a subset of validation tests through the same HAL, with tighter budgets and production limits, and the same result format, so SLT failures correlate directly with validation data."
- **한국어 핵심**: ATE = 구조적 · SLT = 실제 워크로드 · 고르는 기준 = 초당 unique catch · 한계값은 특성화 데이터에서 · 같은 소스 · 같은 결과 형식 · 과잉 불량 vs escape 균형 · Don 훅 = Apple factory test-node (S4).
- **보러 갈 곳**: [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F5 SoC 시나리오 · SLT · [N06G](2026-11-01_N06G_platforms_regression_automation.html) · [deck §5.1 Q5 · §4 S4](2026-10-25_rivian_hm_screen_prep.html) · [BV16 Q04](2026-11-01_BV16_mock_interview_qbank.html) (ATE vs SLT 정의 — 여기선 콘텐츠 선택)

</details>

## 5. 디버그 6

> 디버그 답의 7칸: ① 사실 확인 → ② 실패 경로 그림 → ③ 가설 매트릭스 → ④ 실험 순서 · 어디서 재현 → ⑤ 고쳐졌다는 증거 → ⑥ 회귀 · 재발 방지 → ⑦ 첫 1시간. deck §7 · BV13 · LP15 시나리오와 겹치지 않는 새 상황만.

### D01 · Passes on HAPS, corrupted bands on A0 ★

- **Q.** "Camera frames go through the ISP into DDR fine on HAPS. On A0, with the same sensor model, about one frame in 2000 has a corrupted band of lines. Walk me through it."
- **꼬리1.** "It only happens while the NPU is running."
- **꼬리2.** "It goes away at a higher core voltage."
- **꼬리3.** "Could you have caught this on HAPS?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Facts** — "Same ISP config and firmware build? Fixed lines or random positions? All parts and boards? DDR and ISP clocks on A0 versus HAPS? Any CSI-2 CRC errors or FIFO overflow flags?"
- **② Path** — "Sensor, CSI-2 receiver, line buffer, ISP, DMA, NoC, DDR. The job is to find the first stage where the data is wrong."
- **③ Hypotheses** — "PHY or receiver errors; a receiver FIFO overflow when DRAM latency spikes; DMA or cache coherency, a CPU write-back over part of the buffer; DDR data errors; a clock-domain crossing in the ISP at real frequency; or a driver race that HAPS's slowness hid."
- **④ Experiments** — "Cheapest first: read the CRC counters and overflow flags. Capture raw frames before the ISP; if raw is clean, it's downstream. Then change one variable at a time: lower the ISP clock, lower the DDR rate, remove other traffic, raise voltage."
- **⑤ Proof** — "Below 1 in 2000 needs about 6000 clean frames by the rule of three. I'd want far more, say a million frames, about nine hours at 30 fps, and reverting the fix must bring the failure back."
- **⑥ Regression** — "A pixel checker with embedded patterns and line CRCs goes into nightly, plus a stress variant with DRAM loaded."
- **⑦ First hour** — "Counters, a raw capture, and the band size. If the band equals a line-buffer or burst size, that points to a specific unit."
- **꼬리1** — "Then it's bandwidth or QoS: the receiver or ISP starves when the NPU saturates DRAM. I check watermark counters and the NoC QoS settings. If the buffer underflows with correct QoS, it's a design issue with the data to prove it."
- **꼬리2** — "Then it's margin, not logic: a timing path. I shmoo voltage × frequency at temperature, compare parts, and if slow-corner parts are worse, it's a speed path. Design gets the conditions and the minimal reproducer."
- **꼬리3** — "Partly. Random delays and heavy traffic on HAPS can expose races, but not real timing margin. That's why the A0 plan runs the same test under stress at corners, not only at nominal."
- **한국어 핵심**: "처음 틀린 단계" 찾기(raw capture로 ISP 앞/뒤 분리) · 카운터 · 띠 크기 = 단서 · NPU 동시 = QoS · 전압 의존 = margin · 증거 = 3/p + 되돌리면 재발 · FPGA는 기능, 실리콘은 타이밍.
- **보러 갈 곳**: [N06H](2026-11-01_N06H_debug_methodology_triage.html) (6단계) · [N06G](2026-11-01_N06G_platforms_regression_automation.html) (FPGA vs silicon 표) · [deck §6.4](2026-10-25_rivian_hm_screen_prep.html) · T06 · T09

</details>

### D02 · NPU mismatch only hot on two parts

- **Q.** "NPU output matches golden at room temperature on every part. At about 110 °C junction, two of ten parts show mismatches in one layer type. What do you do?"
- **꼬리1.** "The mismatches are always in the high bits of the accumulator."
- **꼬리2.** "Who do you tell, and what do you give them?"
- **꼬리3.** "Can software work around it?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Facts** — "Which layer type? Deterministic, same input every time, or random? Which lots are the two parts from? Voltage and frequency at the time? Was throttling active?"
- **② Path** — "MAC array, accumulators, local SRAM, DMA from DDR, and the NPU rail from the PMIC."
- **③ Hypotheses** — "A speed path in the MAC or accumulator at hot; SRAM Vmin at temperature; voltage droop during that high-activity layer; DDR errors at hot corrupting weights; a DVFS transition mid-job. Not the golden, because room temperature passes."
- **④ Experiments** — "Move a failing part to another board: does it follow the part? Shmoo voltage × frequency at hot on that layer, comparing good and bad parts. Read ECC counters. Scope the NPU rail during the layer for droop."
- **⑤ Proof** — "Characterize the boundary, for example 'fails below voltage X at temperature T on these parts', then decide: raise the operating voltage, screen in SLT, or fix in B0."
- **⑥ Regression** — "That layer becomes a PVT stress test and an SLT candidate at reduced voltage."
- **⑦ First hour** — "Rerun the failing layer alone at hot on a failing part, log V/F/T, and map which output bits and columns mismatch."
- **꼬리1** — "High accumulator bits point to the carry chain, the longest path. If lowering the frequency makes it go away, it's timing; I hand design the condition and a minimal input."
- **꼬리2** — "Design, and the product/test team, because it affects SLT limits. I give them the conditions, which parts, the shmoo, a minimal reproducer, and the bit pattern."
- **꼬리3** — "Often: more voltage guardband in the DVFS table at hot, or a lower frequency via thermal policy. The cost is power and performance, and I quantify it so the program can choose."
- **한국어 핵심**: 칩을 따라가나(보드 swap) · 고온 V×F shmoo로 경계 · rail droop 측정 · 비트 위치 = 경로 단서 · 결정 = 전압 guardband / SLT 스크린 / B0 수정 · 테스트팀에 알림(SLT 한계).
- **보러 갈 곳**: [N06H](2026-11-01_N06H_debug_methodology_triage.html) (새 시나리오 "NPU 결과가 온도 따라 달라짐") · [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F1 · M07

</details>

### D03 · Nightly pass-rate cliff

- **Q.** "The nightly pass rate dropped from 99% to 93% overnight: about 40 failures across unrelated blocks. Where do you start?"
- **꼬리1.** "The last good build also fails now."
- **꼬리2.** "Bisect points to a commit that only changed a timeout constant."
- **꼬리3.** "How do you stop this costing a morning next time?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Facts** — "What changed overnight: firmware build, test code, tool versions, farm infrastructure, boards, lab temperature? All boards or a subset? Same failure signature?"
- **② Picture** — "Forty unrelated failures at once are rarely forty bugs. It's one common cause: the build, the infrastructure, or something shared like clocks, DDR config or boot."
- **③ Hypotheses** — "Infrastructure: a power switch, the console server, the network. A common firmware change, like clock or cache settings. A test-framework change. A batch of boards. The environment."
- **④ Experiments** — "Group failures by signature, board and slot. Rerun the last good build on the same boards: if it passes, it's the build, so bisect; if it fails, it's infrastructure or boards, so health-check them."
- **⑤ Proof** — "Back to the baseline pass rate on the same boards, and the failing tests pass on rerun."
- **⑥ Regression** — "Health checks before runs so infrastructure failures are labeled as such, and automatic bisect."
- **⑦ First hour** — "Don't open forty tickets. Find the one cause."
- **꼬리1** — "Then it's not the build. Health-check the boards and rack: power, console, JTAG probe firmware, network, temperature. Check whether a tool or a lab image was updated."
- **꼬리2** — "Then the timeout was hiding something: either the new value is too short for a slow but legal hardware response, or the old value masked a real slow path. I measure the actual response time distribution before deciding which."
- **꼬리3** — "Pre-merge smoke on real hardware, infrastructure monitoring, infra failures reported separately from DUT failures, and auto-bisect, so the morning report already names the commit."
- **한국어 핵심**: 40개 동시 = 원인 1개 · last-good 빌드 재실행으로 빌드 vs 인프라 분리 · 서명/보드/슬롯으로 묶기 · timeout 변경 = 실제 응답 분포 측정 · 인프라 실패 별도 집계.
- **보러 갈 곳**: [N06H](2026-11-01_N06H_debug_methodology_triage.html) (새 시나리오 "회귀가 빌드 X부터 깨짐") · [N06G](2026-11-01_N06G_platforms_regression_automation.html) · [BV14](2026-11-01_BV14_board_validation_plan_automation.html) (health check · quarantine)

</details>

### D04 · Debug build passes, release build fails

- **Q.** "A DMA test passes with the debug build and fails with the release build on silicon. Hardware or software?"
- **꼬리1.** "Adding one printf makes it pass."
- **꼬리2.** "How would you show it's actually hardware?"
- **꼬리3.** "Which barrier, and where?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Facts** — "Which flags differ: optimization level, asserts, logging? Does it fail every run? Same failing transfer?"
- **② Picture** — "The CPU writes descriptors and registers; the DMA reads memory. In release, the compiler can reorder, merge, or drop accesses, and the code runs faster."
- **③ Hypotheses** — "A missing volatile on register access; a missing barrier between writing the descriptor and ringing the doorbell; cache maintenance on the wrong range; undefined behavior or uninitialized data; or a real hardware race that debug-build slowness hid."
- **④ Experiments** — "Diff the disassembly of the register sequence. Build only that file at -O0 to bisect. Add a barrier and check volatile. Trace bus transactions if the SoC has a trace unit."
- **⑤ Proof** — "Removing the fix brings the failure back, and the fixed release build passes about 3/p runs."
- **⑥ Regression** — "Regressions run the shipped release configuration. Register access goes only through one accessor layer, checked by static analysis."
- **⑦ Risk** — "If it's hardware, say the doorbell overtakes the descriptor inside the chip, it becomes an erratum with a documented barrier workaround."
- **꼬리1** — "A printf is a delay and a compiler barrier, so it hides the bug. It's evidence pointing to ordering or timing, not a fix."
- **꼬리2** — "Write the minimal sequence with explicit barriers, even in assembly. If it still fails, compare with the documented ordering rules and file it with design, along with the trace."
- **꼬리3** — "The descriptor writes to normal memory must be visible before the doorbell write to device memory, which on Arm means a barrier such as DSB or DMB. In Linux, writel already includes a write barrier before the store; writel_relaxed doesn't."
- **한국어 핵심**: release = 재배치 · 병합 · 빠름 → volatile · barrier · 캐시 범위 · UB · 숨은 HW race · printf = 지연 + 컴파일러 barrier → 증거일 뿐 · 회귀는 출시 설정으로.
- **보러 갈 곳**: [N06H](2026-11-01_N06H_debug_methodology_triage.html) (새 시나리오 "디버그 빌드 통과 · 릴리스 실패") · [N05C register/MMIO](2026-11-01_N05C_register_mmio_driver.html) · [N05H C pitfalls](2026-11-01_N05H_c_pitfalls_read_code.html) · T04

</details>

### D05 · Corruption only when everything runs together

- **Q.** "Each block passes alone. When the cameras, the NPU and the video encoder run together, the NPU's input buffer is occasionally corrupted."
- **꼬리1.** "The corrupted data looks like an older frame."
- **꼬리2.** "Synthetic traffic generators reproduce it without the driver."
- **꼬리3.** "How do you find out which master wrote the bad data?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Facts** — "What's the corruption pattern: a whole cache line, a fixed offset, random bytes? How often? What's the smallest combination of blocks that reproduces it?"
- **② Picture** — "Three masters reading and writing DDR through the NoC, the CPU managing buffer ownership, the SMMU mapping each master."
- **③ Hypotheses** — "A software ownership race (a buffer reused before the NPU finishes); cache coherency (CPU writing back stale lines); overlapping SMMU mappings; an interconnect ordering bug (completion before data is visible); a DDR-controller bug under bank conflicts; droop under combined load."
- **④ Experiments** — "Find the minimal pair. The size of the corruption is a clue: 64 bytes points to a cache line, a burst size points to the interconnect. Add guard regions and canaries, put writer ID and sequence numbers into the data, and check SMMU fault logs. Then replace software with traffic generators."
- **⑤ Proof** — "No corruption over [N hours] with the fix, and reverting it brings the failure back."
- **⑥ Regression** — "A concurrency stress test with embedded writer IDs goes into nightly, plus ownership assertions in the driver."
- **⑦ Risk** — "If it's hardware ordering, it's an erratum and maybe a B0 fix, so capture the evidence carefully."
- **꼬리1** — "Stale data: a missing invalidate, a buffer reused too early, or a write not yet visible when the NPU starts. I check the completion signaling order from writer to NPU."
- **꼬리2** — "Then it's hardware. Reduce to two generators, capture an interconnect trace if available, and bring in the NoC designers with the transaction log."
- **꼬리3** — "Make the data self-describing (master ID, sequence number, address), and use a firewall or debug watchpoint on that address range to catch the offending write."
- **한국어 핵심**: 최소 조합 · 손상 크기 = 단서(64 B 캐시 라인 / 버스트) · 데이터에 writer ID · 시퀀스 · 주소 · 트래픽 생성기로 SW 제거 → 재현되면 HW · 오래된 프레임 = 소유권/무효화/가시성 순서.
- **보러 갈 곳**: [N06H](2026-11-01_N06H_debug_methodology_triage.html) (새 시나리오 "동시 실행 시만 데이터 손상") · [N06D](2026-11-01_N06D_plans_dma_memory.html) D4 · D5 · T06

</details>

### D06 · Two measurements disagree

- **Q.** "You're measuring hardware timestamp accuracy. The scope says the error is within 40 ns; the software log shows 300 ns of jitter. Which do you trust, and what do you report?"
- **꼬리1.** "Someone points out your two scope probes have different cable lengths."
- **꼬리2.** "How many samples do you need?"
- **꼬리3.** "How do you present this to the fusion team?"

<details><summary>답 뼈대 펼치기 (90초판)</summary>

- **① Facts** — "What does each actually measure? The scope measures a hardware event against PPS; the log measures timestamp plus read latency plus scheduling. Same quantity (offset or jitter)? Same duration and sample count?"
- **② Path** — "Event, hardware latch, register, driver read, log. Each stage adds its own error."
- **③ Hypotheses** — "The software number includes interrupt latency and scheduling jitter, which isn't timestamp error. The scope has probe and cable skew and its own timebase. The averaging windows differ."
- **④ Experiments** — "Calibrate the measurement. Feed one known reference, like a split GNSS PPS, to both paths, characterize each one's error, then decide which measures the requirement."
- **⑤ Proof** — "Report hardware timestamp accuracy from the hardware path with its uncertainty stated. Report software delivery latency separately, as a different metric."
- **⑥ Regression** — "A time-interval counter in the farm, logging both metrics on every build."
- **⑦ Risk** — "Fusion uses the hardware timestamp, but the software latency still matters for the real-time budget. Neither number should be dropped."
- **꼬리1** — "Coax adds roughly 5 ns per meter, so a two-meter difference is about 10 ns of skew. I deskew the probes on a common signal before trusting a 40 ns result."
- **꼬리2** — "With a 1 Hz PPS, an hour is 3600 samples, and I'd log for many hours across temperature, reporting p50, p99 and max, not just the mean."
- **꼬리3** — "A small table: what's measured, the method, the uncertainty, p50/p99/max, and conditions. Hardware accuracy and software latency are separate rows."
- **한국어 핵심**: "같은 걸 재는가?"가 먼저 · 같은 기준을 두 경로에 넣어 측정 자체를 검증 · HW 정확도와 SW 지연은 다른 메트릭 · 동축 ≈ 5 ns/m deskew · 분포로 보고.
- **보러 갈 곳**: [N06H](2026-11-01_N06H_debug_methodology_triage.html) (새 시나리오 "측정값이 장비마다 다름") · [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E4 · [BV11 장비](2026-11-01_BV11_lab_instruments_measurement.html) · T10

</details>

## 6. Staff · 리더십 4

> Behavioral 2라운드와도 겹친다(N08 예정). 답 7칸 = ① 어떤 이야기를 할지 한 줄 → ② 상황(규모 숫자) → ③ 걸린 것 · 선택지 → ④ 내가 내린 결정 → ⑤ 결과(숫자) → ⑥ 남긴 장치(도구 · 프로세스) → ⑦ Rivian A0/HAPS에 어떻게 쓸지. **숫자는 `[숫자 채우기]` — 지어내지 말 것.** 기밀은 "a PCIe controller block", "a wireless chipset"처럼 일반화.

| 스토리 (deck §4) | 쓰는 프롬프트 | 채울 숫자 |
|---|---|---|
| S1 Solidigm FPGA pre-si RTL 버그 | L01 | 블록 수 · 버그 수 · respin급 1건 설명 · 단축 주 |
| S2 SK hynix shmoo | L03 (보조), M07 훅 | 축 · 반복 횟수 · 발견 포인트 |
| S3 Apple 통합 루트코즈 | L02 | 증상 · 재현율 · 원인 · 수정 |
| S4 Apple factory test-node | L04 (보조), M08 훅 | 스트레스 시나리오 수 · 잡은 결함 |
| S5 SK hynix 플랫폼 SDK | L03 | 챔버 수 · 동시 DUT · 사용자 수 · 배수 |
| S6 SK hynix 고객 telemetry | L04 | 해결한 필드 이슈 수 · 기간 |
| S8 PCIe 루트코즈 | L02 (보조) | Gen/폭 · 조건 · 사이클 수 |

### L01 · A bug you caught before silicon ★

- **Q.** "Tell me about a bug you found before silicon that would have been expensive to find later."
- **꼬리1.** "How did you convince the designer it was RTL and not your test?"
- **꼬리2.** "What did FPGA miss that silicon later found?"
- **꼬리3.** "What exactly was your part, and what was the team's?"

<details><summary>답 뼈대 펼치기 (2–2.5분판)</summary>

- **① Frame** — "I'll take the pre-silicon FPGA work on a PCIe Gen6 SSD controller at Solidigm."
- **② Situation** — "Before RTL freeze, we had an FPGA image with [숫자 채우기: 블록 수] blocks. I owned bring-up and functional validation of [숫자 채우기: 블록, e.g. DMA / I2C / DRAM controller] in bare-metal C, on Cortex-R8/R82/M0+ subsystems."
- **③ Stakes** — "Every RTL bug found after tape-out costs a respin or a permanent workaround. Before freeze it costs a day of a designer's time."
- **④ Decision** — "I ordered tests by what would be impossible to fix after tape-out: register-map and integration checks, then the data path, then concurrency and boundary stress. Failures were narrowed with FPGA debug probes and Trace32, then handed to design as a minimal register sequence."
- **⑤ Result** — "We found [숫자 채우기: N] RTL issues before freeze. One was [숫자 채우기: 한 줄 설명], which would have meant a respin. That saved [숫자 채우기: 주/비용]."
- **⑥ Mechanism** — "Those tests became the silicon bring-up suite, the same source running on silicon, so on day one we only debugged what FPGA couldn't show."
- **⑦ At Rivian** — "On HAPS for the next chip I'd do the same, and track which silicon bugs FPGA couldn't have caught, so the pre-silicon plan improves each generation."
- **꼬리1** — "I didn't argue; I reduced it. A minimal register sequence, the expected value from the spec paragraph, the probe capture of the actual signal, and a run of the same sequence on a known-good block. Then the designer could reproduce it in simulation in minutes."
- **꼬리2** — "[숫자 채우기: 실제 사례 — 없다면 정직하게] Typically timing at real frequency, analog PHY behavior and power, the things the FPGA can't represent. That's why I treat FPGA green as a baseline, not a sign-off."
- **꼬리3** — "I owned [숫자 채우기: 내 블록 · 테스트 수]; the design team owned the fixes, and [숫자 채우기: 동료 역할]. I'll be precise because silicon day one was [숫자 채우기: 직접 vs 옆에서 본 일]."
- **한국어 핵심**: 이 질문은 면접관 업무(FPGA/HAPS → bring-up) 정중앙 · "고칠 수 없는 것 먼저" 우선순위 · 최소 재현 시퀀스로 설득 · 같은 소스 → 실리콘 · "I" 분명히, 과장 없이.
- **보러 갈 곳**: [deck §4 S1](2026-10-25_rivian_hm_screen_prep.html) · [N06G](2026-11-01_N06G_platforms_regression_automation.html) · [BV16 Q02 꼬리3](2026-11-01_BV16_mock_interview_qbank.html) (같은 이력, 보드 쪽 표현)

</details>

### L02 · Driving a silicon issue to closure

- **Q.** "Tell me about a time you had to convince the design or hardware team that a failure was theirs, and drive it all the way to a fix or an erratum."
- **꼬리1.** "What if you'd been wrong?"
- **꼬리2.** "How did you escalate without it becoming blame?"
- **꼬리3.** "How do you make sure the fix actually lands in the next stepping?"

<details><summary>답 뼈대 펼치기 (2–2.5분판)</summary>

- **① Frame** — "I'll take a new wireless chipset integration at Apple, a [숫자 채우기: PCIe / I2C / SPMI / RFFE] failure."
- **② Situation** — "The symptom was [숫자 채우기: 증상], on [숫자 채우기: N of M] boards, at a rate of [숫자 채우기: %], under [숫자 채우기: 조건]."
- **③ Stakes · options** — "Hardware said software timing; software said hardware. Meanwhile [숫자 채우기: 막힌 일정/팀]."
- **④ Decision** — "I proposed the experiment and the decision rule before anyone argued conclusions: analyzer and scope on the bus before touching software, and a minimal reproducer. If it reproduces with the smallest software on every board, it's hardware."
- **⑤ Result** — "Root cause: [숫자 채우기: 원인]. Fix: [숫자 채우기: 수정]. Reproduction went from [숫자 채우기: %] to zero over [숫자 채우기: N] runs."
- **⑥ Mechanism** — "I added the case to the factory test and the regression, and documented the workaround until the hardware fix."
- **⑦ At Rivian** — "The same errata lifecycle: root cause, firmware workaround, documented erratum, tracked to the stepping that fixes it, verified on B0 with the same test."
- **꼬리1** — "Then the experiment did its job. I'd fix my side, add the regression that would have caught it, and say so in the same channel where I raised it. That track record makes the next escalation easy."
- **꼬리2** — "Facts, cost, ask: what we know, what it's blocking and what that costs per week, and the one experiment or person we need. Never 'your bug'."
- **꼬리3** — "Every erratum has an owner, the fixing stepping, and the test that proves it. When B0 arrives, that test runs in the first week, and the erratum is closed only with that result."
- **한국어 핵심**: 결론 말고 **실험 + 판정 규칙 선합의** · analyzer/scope 먼저 · 최소 재현 · 회귀 + factory test 추가 · errata = 소유자 · 수정 stepping · 증명 테스트.
- **보러 갈 곳**: [deck §4 S3 · S8](2026-10-25_rivian_hm_screen_prep.html) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) (errata 수명주기 · escalation 문장) · [BV16 Q29](2026-11-01_BV16_mock_interview_qbank.html) (HW or SW — 여기선 closure까지)

</details>

### L03 · Infrastructure other engineers adopted

- **Q.** "Our validation team will roughly double next year. Tell me about a time you built infrastructure that made other engineers faster."
- **꼬리1.** "How did you get people to actually adopt it?"
- **꼬리2.** "What would you do differently?"
- **꼬리3.** "Two teams want conflicting features. Who wins?"

<details><summary>답 뼈대 펼치기 (2–2.5분판)</summary>

- **① Frame** — "The chamber reliability test platform and SDK at SK hynix."
- **② Situation** — "Before, each engineer ran chamber tests by hand: [숫자 채우기: 사람 수, 시간/주]."
- **③ Options** — "Per-team scripts would be fast to start but would diverge. One shared platform cost more up front but scales. I chose the platform plus an SDK."
- **④ Decision** — "I built first what everyone repeated: scheduling, temperature control, UART sequencing and result collection. Shmoo sweeps came next, the S2 work."
- **⑤ Result** — "[숫자 채우기: 챔버 수] chambers, [숫자 채우기: 동시 DUT] DUTs at once, [숫자 채우기: 사용 엔지니어 수] engineers, [숫자 채우기: 수동 대비 배수] faster."
- **⑥ Mechanism** — "An SDK with docs and examples, so other engineers wrote tests, not infrastructure, and some contributed back."
- **⑦ At Rivian** — "With the team doubling, I'd do the same for the test framework and the farm: one HAL, one result format, one onboarding path, so a new engineer writes a test in their first week."
- **꼬리1** — "Solve a pain they already had, so the first version saved them time in week one. Sit with the first users, and fix their complaints fast."
- **꼬리2** — "[숫자 채우기: 실제 아쉬운 점] For example, defining the result format and database earlier, because migrating old results later cost time."
- **꼬리3** — "Whatever serves the shared roadmap: what unblocks the most validation coverage per week. I make the trade-off visible and get the leads to agree, rather than deciding quietly."
- **한국어 핵심**: 반복 작업부터 플랫폼화 · SDK = 다른 사람이 테스트만 쓰게 · 숫자 4개 반드시 채움 · Rivian: 팀 2배 → HAL · 결과 형식 · 온보딩.
- **보러 갈 곳**: [deck §4 S5 · S2](2026-10-25_rivian_hm_screen_prep.html) · [BV14](2026-11-01_BV14_board_validation_plan_automation.html) · [N06G](2026-11-01_N06G_platforms_regression_automation.html) · [BV16 Q28 Don 훅](2026-11-01_BV16_mock_interview_qbank.html)

</details>

### L04 · Road-test issue under schedule pressure

- **Q.** "A road-test vehicle reports camera dropouts about once every ten hours, and the program wants an answer this week. How would you lead it — and tell me about a time you did something similar." (면접관 경력: "resolved road test issues" [확인됨: deck §1.1])
- **꼬리1.** "It's Friday and you don't have a root cause."
- **꼬리2.** "The camera team and the silicon team each think it's the other."
- **꼬리3.** "How do you decide between containment and root cause?"

<details><summary>답 뼈대 펼치기 (2–2.5분판)</summary>

- **① Frame** — "First the plan for this case, then a time I did it: customer telemetry at SK hynix."
- **② Situation (this case)** — "Clarify: how many vehicles, which silicon and software versions, which cameras, and conditions like temperature, vibration and time since boot. Picture the path: sensor, serializer, cable, deserializer, CSI-2, ISP."
- **③ Options** — "Run two tracks in parallel with owners. Data: pull counters and logs from the vehicles. Lab: reproduce on the bench under stress, using temperature, cable stress and full DRAM load."
- **④ Decision** — "If it's safety-relevant, containment first: detection and recovery in software. Then root cause."
- **⑤ Communication** — "A daily update with facts, ranked hypotheses, the next experiments, and an honest ETA."
- **⑥ Mechanism (story)** — "If the logs don't have the data, add it. At SK hynix I built the telemetry path that let us debug customer failures from logs instead of guessing: [숫자 채우기: 해결한 이슈 수 · 기간 · 결과]."
- **⑦ Close** — "A regression test, and if it's a part issue, a factory or SLT screen, like my factory test-node work at Apple (S4)."
- **꼬리1** — "I report what's known, what's ruled out, the containment status, and the next 48 hours of experiments. A precise 'not yet' with a plan beats a guess."
- **꼬리2** — "Agree the decision rule first, for example 'if the CSI-2 CRC counter increments, it's the link; if it stays clean and frames are late, it's downstream'. Then instrument both sides at once."
- **꼬리3** — "Containment protects the vehicles now; root cause protects the next million. With safety impact, containment ships first and root cause continues, and both are tracked."
- **한국어 핵심**: 두 트랙(차량 데이터 · 랩 재현) + 담당자 · 안전 관련이면 containment 먼저 · 매일 사실 · 가설 · 다음 실험 · ETA · 로그에 없으면 telemetry 추가(S6) · 닫기 = 회귀 + 스크린(S4).
- **보러 갈 곳**: [deck §4 S6 · S4](2026-10-25_rivian_hm_screen_prep.html) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) (escalation · 버그 리포트) · T09 · D01

</details>

## 7. 모의 세트 3개

각 세트 **45분 = 테스트 플랜 1 + 방법론 1 + 디버그 1** + 채점. 혼자 할 때는 휴대폰 녹음 + 타이머. 파트너(또는 Claude)가 면접관이면 아래 "면접관 대본"을 건넨다.

| 시간 | 할 것 |
|---|---|
| 0:00–0:02 | 녹음 시작 · 프롬프트 읽기 · 종이 1장 준비 (그림용) |
| 0:02–0:12 | **테스트 플랜 10분** (§1.1 시간표) |
| 0:12–0:17 | 꼬리 3개 (각 ≈ 90초) |
| 0:17–0:25 | **방법론 6분** + 꼬리 2개 |
| 0:25–0:33 | **디버그 6분** + 꼬리 2개 |
| 0:33–0:45 | 녹음 1.5배속 재생 · §1 채점 · §8 기록 · 펼쳐서 비교 |

| 세트 | 테스트 플랜 | 방법론 | 디버그 | 노리는 것 | 교체용 (2회차) |
|---|---|---|---|---|---|
| **세트 1 · 기본기** | T01 I2C | M01 DV vs validation | D03 회귀 절벽 | 7단계 프레임을 몸에 붙이기 · 12 카테고리 빠짐없이 | T02 GIC · M06 · D04 |
| **세트 2 · 데이터 경로** | T09 CSI-2 + ISP | M04 샘플 수 계산 | D01 HAPS ↔ A0 | 카메라 갭 메우기 · 숫자로 말하기 (rule of three) | T04 DMA · M03 · D05 |
| **세트 3 · Manjiang 맞춤** | T11 NPU | M02 B0 종료 기준 | D02 NPU 고온 불일치 | FPGA → A0 → PVT → SLT 한 줄로 잇기 · Staff 시그널 | T08 Ethernet 또는 T10 timestamp · M08 SLT · D06 |

- 세트 순서: 1 → 2 → 3 (각 하루 간격). 점수가 20 미만인 세트는 **교체용 프롬프트로** 다시 (같은 질문을 반복하면 외운 답만 늘어난다).
- 60분 버전 (D-2): 세트 3 + Staff 1개(L01 또는 L04) 2.5분 + 꼬리.
- **면접관 대본** (파트너용): ① 질문만 읽고 조용히 듣는다. ② 4분쯤 Don이 매트릭스에 있으면 끊고 꼬리1을 묻는다 (실제 면접처럼 끊긴 뒤 다시 프레임으로 돌아오는 연습). ③ 10분이 되면 "Let's move on" 하고 꼬리2 · 3. ④ 디버그는 Don이 가설을 말하면 하나를 골라 "That's ruled out — the counters are clean"처럼 막는다. ⑤ 끝나고 §1 표로 같이 채점.

## 8. 연습 기록 표

카테고리 약어: REG · FUNC · CFG · BND · IRQ · ERR · RST · CONC · PERF · PVT · SOAK · SYS. 점수는 §1.1 = /24, §1.2 = /20, §1.3 = /14.

| 날짜 | 세트 · 프롬프트 | 걸린 시간 | 점수 | 빠진 카테고리 | 다음에 고칠 한 가지 |
|---|---|---|---|---|---|
| 11-__ | 예시: 세트 1 · T01 | 11:40 | 17/24 | RST · SOAK | ③에서 RST 먼저 말하고 SOAK은 숫자로 |
| 11-__ | | | | | |
| 11-__ | | | | | |
| 11-__ | | | | | |
| 11-__ | | | | | |
| 11-__ | | | | | |
| 11-__ | | | | | |
| 11-__ | | | | | |
| 11-__ | | | | | |
| 11-__ | | | | | |

- 세 번 이상 같은 카테고리가 빠지면 → [N06A](2026-11-01_N06A_methodology_test_plan_template.html) 카테고리 12종 체크리스트를 종이에 쓰는 것부터 다시. 자주 빠지는 순서 예상 [추정]: RST → SOAK → BND → SYS.
- 같은 프롬프트 점수가 2회 연속 20 이상이면 그 프롬프트는 졸업 — 교체용으로.

## 9. 출처

이 Q 뱅크는 대부분 기존 노트를 재배열한 것이다. 외부 수치는 아래에서 확인한 것만 [확인됨]으로 썼고, 나머지 스펙 수치·Rivian 구성은 본문에 `[추정]`.

1. NXP UM10204 I2C-bus specification and user manual — https://www.nxp.com/docs/en/user-guide/UM10204.pdf (Fast-mode tHD;STA ≥ 0.6 µs, tr ≤ 300 ns; Standard tr ≤ 1000 ns; Fm+ tr ≤ 120 ns; SDA stuck LOW → nine clock pulses; 2026-11-01 확인)
2. RFC 2544 Benchmarking Methodology for Network Interconnect Devices — https://www.rfc-editor.org/rfc/rfc2544 (프레임 크기 64·128·256·512·1024·1280·1518, throughput = 손실 없는 최대 rate; 2026-11-01 확인)
3. [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) — §2 N06I 항목, 7단계 프레임, 카테고리 12종
4. [deck](2026-10-25_rivian_hm_screen_prep.html) — §1 면접관(Manjiang) 경력·특허, §4 S1–S8, §5.1 Q1–Q10, §6, §7
5. [context](rivian_nstaff_silicon_validation_engineer_context.html) — RAP1 · ACM3 · RivLink · HW prototyping 플랫폼에서 pre-silicon SW
6. [BV16 보드 모의 Q 뱅크](2026-11-01_BV16_mock_interview_qbank.html) — 중복 확인 기준
7. [LP14 PCIe 검증 방법론](2026-10-31_LP14_validation_methodology.html) — rule of three 표 · 12영역 · BER 시간 계산
8. [BV14 검증 계획 · 보드 팜](2026-11-01_BV14_board_validation_plan_automation.html) — Staff 시그널 · 종료 기준 · flaky 정책
9. [BV12 SI/PI · shmoo · PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html), [BV15 자동차 보드](2026-11-01_BV15_automotive_board_considerations.html) — PVT · AEC-Q100 · FTTI

# N06B · 커버리지 · 메트릭 · 통계 · 종료 기준
<!-- theme: pastel -->

> **묶음**: N06 방법론 (커버리지 · 통계 · "끝났다"의 정의) · **우선**: P0 (온사이트 Technical 2 단골 "How do you know you're done?") · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06A 방법론·템플릿](2026-11-01_N06A_methodology_test_plan_template.html) / [N06C 연습① peripheral](2026-11-01_N06C_plans_peripherals_soc_infra.html) · **관련**: [LP14 §2.5 rule of three](2026-10-31_LP14_validation_methodology.html) · [BV14 §2.2 추적성, §2.8 종료 기준, §2.14 메트릭, 체인 E 주간 보고](2026-11-01_BV14_board_validation_plan_automation.html) · [deck §5.1 Q10 coverage report](2026-10-25_rivian_hm_screen_prep.html) · [V5 test runner + coverage](hmScreenPrep/problems/V5_test_runner_coverage/index.html) · [N06G 회귀·자동화](2026-11-01_N06G_platforms_regression_automation.html) · [N06H 디버그](2026-11-01_N06H_debug_methodology_triage.html) · **코드**: onsitePrep/n06/b/ — [coverage_report.py](onsitePrep/n06/b/coverage_report.py) · [stats_problems.py](onsitePrep/n06/b/stats_problems.py) · [gen_mock.py](onsitePrep/n06/b/gen_mock.py) · [requirements.csv](onsitePrep/n06/b/requirements.csv) · [bugs.csv](onsitePrep/n06/b/bugs.csv) · [run.sh](onsitePrep/n06/b/run.sh) · [run_output.txt](onsitePrep/n06/b/run_output.txt)

## 0. TL;DR

- 이 페이지로 답할 것: **"How do you know you're done?"**, "커버리지 리포트에 뭐가 들어가나", "몇 번·몇 개 칩·몇 장 보드 돌려야 하나", "1/500 버그 고쳤다는 걸 어떻게 증명하나", "커버리지 100%였는데 버그가 고객에게 갔다 — 왜?", "매주 매니저에게 뭘 보고하나".
- **외울 것 ① 커버리지 = 계획(요구사항 목록)이 분모, 증거(PASS 결과)가 분자.** 테스트 개수는 커버리지가 아니다. 실리콘 검증에선 **requirement + cross(조건 × 코너) 커버리지가 주**, 테스트 FW의 code coverage는 보조.
- **외울 것 ② 통계 3개 숫자**: 0 실패 / n회 → 95% 상한 **≈ 3/n** (rule of three) · "1/500 미만"을 말하려면 **1,497회 무실패** · 실패가 k번 있으면 Clopper-Pearson 상한 (예: 3/2000 → 상한 **0.39%**, 관측 0.15%의 2.6배).
- **외울 것 ③ 종료 기준 7줄** (시작 전에 합의): P0 100% PASS 또는 waiver · P1 ≥ 합의 % · P0 코너 셀 전부 · 신뢰성 요구는 **상한 < 목표** · 열린 S1/S2 버그 0 · **테스트가 실패할 수 있음을 증명(negative control)** · 칩·보드 분산(≥3 칩, ≥2 보드).
- **"커버리지가 거짓말하는 4가지"**: 체크가 실제 HW를 안 읽음 · 조건 조합 누락(cross hole) · 테스트가 실패를 못 함 · 요구사항 자체가 없음 → 처방은 negative control · fault injection · escape 분석.
- 코드 n06/b: mock CSV(블록·요구사항·테스트·칩·보드·V/F/T 코너·결과) → 블록별 커버리지·pass rate·상한·**종료 판정** — 실제 출력: DMA CONDITIONAL · I2C DONE · DDR NOT DONE · CSI NOT DONE (§6).
- **30초 영어 답**: "Done is defined before we start, as exit criteria the design, DV and product owners sign off on. Every P0 requirement has passing evidence on the release-candidate build at every required corner — or an approved waiver with a workaround. Reliability requirements need a statistical bound, not just 'it passed': zero failures in about 3/p runs. No open severity-1 or -2 bugs, every P0 test has been shown to fail when the hardware is broken, and the evidence spans at least three chips including SS and FF corners and more than one board. If time runs out first, I report exactly which criteria aren't met and the residual risk — that's a decision for the program, not for me to hide."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "학교 졸업 사정"

- **요구사항 목록 = 졸업 요건표** (필수 과목 P0, 선택 과목 P1). 학생(칩)이 졸업하려면 필수 과목 전부 이수 + 선택 과목 몇 % 이상.
- **커버리지 = 이수한 과목 / 졸업 요건 과목 수.** "시험을 200번 봤다"(테스트 개수)는 졸업 요건과 무관 — 수학만 200번 봤을 수도 있다.
- **cross 커버리지 = 과목 × 조건.** 체육 과목은 "여름에도, 겨울에도" 통과해야 하는 식. 실리콘은 **전압 × 주파수 × 온도 × 칩 공정 코너**에서 통과해야 한다.
- **통계(rule of three) = "결석 0번"의 의미.** 3일 출석해서 결석 0이면 "성실하다"고 못 한다. 300일 결석 0이면 "결석률 1% 미만"이라고 말할 수 있다. 0번 봤다 ≠ 0이다.
- **종료 기준 = 졸업 사정 규정을 입학할 때 공지.** 졸업 직전에 기준을 정하면 "시간 다 됐으니 졸업"이 된다.
- **커버리지 100%인데 버그 escape = 시험지가 정답을 안 보고 채점됨.** 채점기(체크)가 고장 나면 모든 과목이 A+로 보인다 → "채점기가 틀린 답을 틀렸다고 하는지" 먼저 확인(negative control).

### 1.2 그림 — 요구사항에서 "끝났다"까지

```text
 spec 문단 ─► REQ-DMA-006 (검증 가능한 한 문장 + P0 + 필요한 코너 + 목표 실패율)
                  │  (계획 = 분모)
                  ▼
           TC-DMA-006 (C 테스트, FPGA·실리콘 공통)
                  │  실행 × (칩 TT/SS/FF) × (보드) × (V/F/T 코너) × (FW 빌드)
                  ▼
   결과 DB:  run_id · build · block · req · test · chip · board · corner · iter · fails · result
                  │  (증거 = 분자, 최신 빌드만)
                  ▼
   ┌─────────── 집계 ────────────────────────────────────────────────┐
   │ 요구사항 커버리지  P0 6/6 · P1 4/4 (waiver 1)                     │
   │ cross 커버리지    51/52 셀                                       │
   │ 신뢰성 상한       0 fail / 3300 → UB95 = 0.00091 < 0.001 ✔        │
   │ 버그             open S1/S2 = 0                                  │
   │ negative control DMA-005 미증명 → WARN                           │
   └──────────────────────────────────────────────────────────────────┘
                  ▼
        종료 기준 E1–E7 판정 ─► DONE / CONDITIONAL / NOT DONE ─► sign-off 리뷰
```

### 1.3 용어 사전

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| requirement coverage | 요구사항 커버리지 | 요구사항 중 PASS 증거가 있는 비율 | 실리콘 검증의 주 지표. 분모는 계획 |
| feature coverage | 기능 커버리지 | 기능(feature) 단위로 묶어 본 커버리지 | 요구사항보다 굵은 단위 — 경영진 보고용 |
| register coverage | 레지스터 커버리지 | 레지스터 필드마다 reset 값·R/W 속성 확인 비율 | bring-up 첫 주의 기본, 자동 생성 가능 |
| cross coverage | 교차 커버리지 | 요구사항 × 조건(코너·모드) 조합 셀 중 채운 비율 | "상온만 테스트" 같은 구멍을 숫자로 보여 줌 |
| code coverage | 코드 커버리지 | 테스트 FW·드라이버 코드 중 실행된 줄/분기 | 에러 처리 경로 미실행 발견용 — 보조 지표 |
| functional coverage (DV) | 기능 커버리지 (설계 검증) | RTL 시뮬레이션에서 "이 값·조합이 한 번이라도 나왔나"를 세는 것 | DV 쪽 용어 — validation과 이름만 같고 대상이 다름 |
| covergroup / bin | (SystemVerilog) | 커버리지를 세는 바구니 묶음 / 바구니 하나 | 면접에서 이름만 알아들으면 됨 |
| traceability | 추적성 | spec ↔ 요구사항 ↔ 테스트 ↔ 결과 연결 | "이 요구사항 증거 보여 줘"에 3클릭 |
| rule of three | 3의 법칙 | 0 실패 / n회 → 실패율 95% 상한 ≈ 3/n | "0번 봤다"를 숫자로 말하는 법 |
| Clopper-Pearson | 정확 이항 신뢰구간 | 실패 k번일 때 실패율 상한을 이항분포로 직접 계산 | k > 0일 때 표준. 보수적(넓게 잡음) |
| success-run | 무실패 표본 수 공식 | n = ln(1−C) / ln(R) | 칩·부품 몇 개 필요? (R99/C95 → 299개) |
| exit criteria | 종료 기준 | "끝났다"의 사전 합의 조건 목록 | Staff 시그널 1순위 |
| waiver | 면제 승인 | 기준 미충족을 리스크로 인정하고 승인 | 숨기지 않고 문서로 남김 |
| errata | 정오표 | 알려진 실리콘 결함 + workaround 목록 | waiver의 근거 |
| negative control | 음성 대조군 | 일부러 실패해야 하는 조건에서 FAIL이 나오는지 | "테스트가 실패할 수 있나" 증명 |
| escape | 유출 버그 | 검증을 통과해 다음 단계(고객·양산)에서 발견된 버그 | 커버리지 신뢰도 평가 |
| first-pass rate | 1회 통과율 | retry 없이 PASS한 비율 | retry 포함 pass rate는 flaky를 숨김 |
| V/F/T corner | 전압/주파수/온도 코너 | 예: LV-Fmax-125C (저전압·최고주파수·고온) | margin이 가장 작은 곳 |
| process corner | 공정 코너 | TT(typical)·SS(slow)·FF(fast) skew 칩 | SS는 타이밍, FF는 누설·hold 쪽이 취약 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 커버리지 종류 — UVM 몰라도 되는 설명

> 핵심: **커버리지는 "무엇에 대한 비율인가"가 전부**다. 분모가 무엇인지 말하지 않는 커버리지 숫자는 의미가 없다.

| 종류 | 분모 (무엇에 대한 %) | 분자 | 실리콘 검증에서 | 예 [예시] |
|---|---|---|---|---|
| **requirement coverage** ★ | 계획에 적힌 요구사항 전체 (P0·P1 따로) | 최신 빌드에서 모든 필요 셀이 PASS인 요구사항 | **주 지표** | P0 6/6, P1 3/4 + waiver 1 |
| feature coverage | 기능 목록 (DMA: single · SG · 2D · 에러 · IRQ · 성능) | 기능에 속한 요구사항이 전부 covered | 경영진·리뷰용 요약 | 6 기능 중 5 완료 |
| **register coverage** | 레지스터 필드 전체 (IP-XACT / SystemRDL 기술에서 자동 추출) | reset 값 · 속성(RW/RO/W1C/WO) · reserved · side-effect 확인한 필드 | bring-up 첫 주, 자동 생성 | 212/212 필드 reset 값 일치 |
| **cross / condition coverage** ★ | 요구사항 × V/F/T 코너 × 공정 코너 × (모드) 셀 | PASS 증거가 있는 셀 | **구멍 찾기의 핵심** | DDR 35셀 중 33 (SS·LV-Fmax-125C 막힘) |
| code coverage (테스트 FW·드라이버) | 테스트 FW/드라이버의 줄·분기 | 실행된 줄·분기 (gcov 등) | 보조 — 에러 처리 분기 미실행 발견 | 드라이버 에러 분기 40% → abort 경로 미검증 |
| DV functional coverage | RTL 시뮬레이션의 covergroup bin | 시뮬에서 한 번이라도 나온 bin | **DV 팀 소관** — 우리는 "DV가 덮은 것"을 받아 중복 회피 | DMA 크기 bin 0/1/2–63/64+/max |

- **DV functional coverage를 30초로**: DV 엔지니어는 RTL 시뮬레이션에서 랜덤 자극을 넣는다. "DMA 길이가 0인 경우가 한 번이라도 나왔나? 길이 0 × 정렬 안 된 주소 조합은?" 같은 관심 값을 **bin(바구니)** 으로 정의하고, 시뮬레이션이 그 바구니를 채웠는지 센다. 바구니 묶음이 **covergroup**, 값 하나가 **coverpoint**, 두 coverpoint 조합이 **cross**. RTL code coverage(line·branch·toggle·FSM 상태)는 "RTL 코드 줄이 실행됐나"다.
- **validation과의 관계** (면접 문장): "DV coverage tells me the design was exercised in simulation; it says nothing about timing, analog, power or PVT on real silicon. So I take DV's coverage closure report as input — I don't repeat their random stimulus — and my coverage is requirement-by-corner evidence on real chips."
- **register coverage 자동화**: 레지스터 기술 파일(IP-XACT = IEEE 1685, SystemRDL = Accellera 표준 [5][6])에서 필드 목록 → 테스트 자동 생성: reset 값 읽기 → RW는 walking-1/0x5A/0xA5 패턴 write-read → RO는 write 후 불변 → W1C는 1 쓰면 clear → reserved는 0 읽힘. 주의: **side-effect 레지스터**(read-to-clear, FIFO pop, 쓰면 동작 시작)는 자동 테스트에서 제외 목록으로.
- **cross 폭발 다루기**: 요구사항 30 × V/F/T 5 × 공정 3 = 450셀. 전부 채우지 않는다 — 요구사항마다 **필요 코너 등급**을 계획에 적는다 (이 페이지 코드의 `corners` 열): `NOM`(상온·nominal 1셀) / `PVT`(nominal + 극한 4코너 × TT/SS/FF = 15셀). margin에 민감한 것(DDR·SerDes·PLL·CSI D-PHY·I2C 타이밍)만 PVT, 레지스터·에러 경로는 NOM.

### 2.2 Traceability — "어떤 요구사항이든 증거까지 3클릭"

- 사슬: **spec 문단 → REQ ID → TC ID → 최신 결과(빌드·칩·보드·코너·로그)**. 역방향: 실패 로그 → TC → REQ → spec 문단 (→ 설계자 연락).
- 요구사항 한 줄의 필드 (이 페이지 requirements.csv와 같음): `block · req_id · priority · title(검증 가능한 문장) · corners(NOM/PVT) · p_max(목표 실패율, 신뢰성 요구만) · waiver(errata ID) · neg_control(Y/N)` + 실제로는 spec 출처·owner·검증 방법(test/inspection/analysis).
- 결과 한 줄의 필드: `run_id · build · block · req_id · test_id · chip · board · vft_corner · iterations · fails · result` + 실제로는 seed·로그 경로·장비 ID·시작 시각.
- 규칙 4개:
  1. 요구사항은 **검증 가능한 문장** ("빠르다" ❌ → "4 KiB burst에서 AXI 이론 대역폭의 ≥ 90%" ✅).
  2. **N:M 허용**: TC 하나가 REQ 여러 개, REQ 하나가 TC 여러 개(조건별).
  3. **증거는 최신 빌드(release candidate)만** — fw1.3에서 PASS는 fw1.4의 증거가 아니다 (코드 §6에서 이전 빌드는 추세용으로만 씀).
  4. **분모는 계획에서**: 결과 DB만 보고 커버리지를 계산하면 **한 번도 안 돌린 요구사항이 아예 안 보인다** (DDR-007 NOT_RUN이 보이는 건 requirements.csv가 있어서).
- 면접 문장: "If you point at any requirement, I can show you the test, the latest result on the release-candidate build, which chips, boards and corners, and the log — in three clicks. And it works backwards: an ECO touches these nets, so these requirements, so these tests get re-run."

### 2.3 통계 기초 — rule of three · Clopper-Pearson · 가정 (LP14 §2.5 요약 + 추가)

> 자세한 유도와 PCIe 숫자 표는 [LP14 §2.5](2026-10-31_LP14_validation_methodology.html). 여기선 공식 3개와 함정만.

- **0 실패 / n회** → 95% 상한 p_U = 1 − 0.05^(1/n) ≈ **3/n** [1]. 신뢰수준별 분자: 90% → 2.30 · 95% → 3.00 · 99% → 4.61 (= −ln α) [1] — 스크립트 출력으로 확인 (§2.4 문제 1).
- **k 실패 / n회** → Clopper-Pearson 단측 상한: P(X ≤ k; n, p_U) = α 를 p_U에 대해 푼다. 이항 CDF를 직접 쓰므로 "exact", 실제 커버리지가 명목값 이상이라 **보수적**(넓게 나옴) [2][3]. 손으로는 포아송 근사: k=0 → 3.00/n, k=1 → 4.74/n, k=2 → 6.30/n, k=3 → 7.75/n.
- **목표 p_max를 주장하려면 몇 번**: 0 실패 기준 n ≥ ln(α)/ln(1 − p_max) ≈ 3/p_max. 실패가 생기면 같은 p_max를 위해 필요한 n이 커진다 (k=1 → ≈ 4.74/p).
- **success-run (부품·칩 수)**: 무작위 n개 전부 PASS → "모집단 R 이상이 PASS"를 신뢰도 C로: n = ln(1 − C)/ln(R) [4]. AEC-Q100의 3 lot × 77 = 231개 0 fail은 이 식의 R99/C90에 해당 [4].
- **가정 (면접에서 꼭 덧붙일 것)**:
  - **독립 · 동일 조건**: 같은 칩·보드 1,000회는 그 개체에 대한 결론. 조건(온도·전압·빌드)이 바뀌면 p가 바뀌므로 조건별로 따로 센다.
  - **무작위 표본 vs 코너 칩**: skew lot의 SS/FF 칩은 무작위 표본이 아니다 — "최악을 대표"하는 **경계 사례 증거**지 모집단 비율 추정이 아니다.
  - **연속 통과 vs 무작위**: 수정 직후 연속 1,500회 PASS는 "그 조건에서 1/500 미만" 증거. 다른 칩·온도에 대한 주장은 아님.
  - **다중 비교**: 30개 요구사항 × 15셀을 각각 95%로 보면 어딘가는 우연히 튄다 → 실패는 재실행으로 지우지 말고 **재실행 횟수까지 기록**.

### 2.4 계산 연습 문제 4개 (★ 손으로 풀고, 답은 열기) — 전부 stats_problems.py로 검증

> 계산기 없이 말로 할 수 있게: **3/n**, **ln 0.05 ≈ −3**, **ln(1−p) ≈ −p**, **0.9^n**.

#### 문제 1 · Rule of three — "부팅 600회 0 실패면 뭐라고 말하나?"

새 A0 보드에서 cold boot 600회, 실패 0. 매니저가 "부팅 안정성 100%라고 써도 되나?" 묻는다. 95%·90%·99% 신뢰 상한은?

<details><summary>풀이 · 답</summary>

- 0 실패 / 600 → 95% 상한 ≈ 3/600 = **0.005 = 1/200**. 정확값 1 − 0.05^(1/600) = **0.004980 (1 in 201)** — 3/n과 0.4% 차이.
- 90% → 2.303/600 = 0.00384, 99% → 4.605/600 = 0.00768 (스크립트 출력 §6).
- **말로**: "We can't say 100%. We can say the boot failure rate is below 1 in 200 with 95% confidence. If the requirement is 1 in 1000, we need about 3,000 fail-free boots — and spread across chips and boards, otherwise it's a statement about this one board."
- 한국어 핵심: "100%"가 아니라 "1/200 미만(95%)". 1/1000을 원하면 약 3,000회.

</details>

#### 문제 2 · 실패율 상한 — "2,000회 중 3번 실패"

PCIe 링크 리셋 사이클 2,000회 중 링크 업 실패 3번. 관측 실패율과 95% 상한은? 별도로 soak 5,000 device-hour 고장 0이면 MTBF 하한은?

<details><summary>풀이 · 답</summary>

- 관측: 3/2000 = **0.15%**.
- Clopper-Pearson 95% 단측 상한: **0.003872 (1 in 258)** — 관측값의 2.6배. 포아송 근사 7.754/2000 = 0.003877과 일치. 독립 검증: math.comb로 직접 계산한 P(X ≤ 3; 2000, 0.003872) = 0.050015 ≈ 0.05 ✔.
- soak: 0 고장 / 5,000 h → 고장률 상한 3/5000 = 0.0006 /h → **MTBF ≥ 1,667 h (95%)**. (일정 고장률·포아송 가정 — 초기 고장(infant mortality)이나 마모는 이 모델 밖.)
- **말로**: "Observed 0.15%, but with only three events the 95% upper bound is about 0.39%, one in 258. If the spec is one in a thousand, we're not just 'close' — we're not demonstrating it at all."
- 한국어 핵심: 실패가 적을수록 상한은 관측값보다 훨씬 크다 (3건 → 2.6배).

</details>

#### 문제 3 · "1/500 버그를 고쳤다"를 증명하려면 몇 번?

1/500 확률로 나던 DMA 데이터 손상. FW 수정 후 몇 번 무실패여야 "고쳤다(최소 1/500 미만)"? 돌리다 1번 실패하면? "10배 좋아졌다(1/5000 미만)"는? 스트레스 조건(고온·저전압)에서 재현율이 1/20이었다면?

<details><summary>풀이 · 답</summary>

- 0 실패로 상한 < 1/500: n ≥ ln 0.05 / ln 0.998 = 1496.4 → **1,497회**. 이 숫자의 의미: 버그가 그대로(1/500)라면 1,497회 연속 무실패일 확률 = 0.998^1497 = **0.0499** — 즉 "운으로 통과"할 확률 5% 미만.
- 같은 식이 "재현에 몇 번 필요한가"의 답이기도 하다: 1/500 버그를 95% 확률로 1번 이상 보려면 역시 1,497회. → **수정 전 재현 실험에 0/200 PASS는 아무 의미 없다.**
- 1번 실패가 나오면: 먼저 **같은 시그니처인지** 확인(다르면 다른 버그). 같으면 수정 불완전 의심. 통계만 보면 k=1에서 상한 < 1/500을 위해 **총 2,371회**(추가 실패 없이) 필요.
- 10배 개선 주장(< 1/5000): **14,978회** — 보드 병렬 + 밤샘 필요한 규모. 사이클 30 s면 125시간 → 보드 10장이면 12.5시간.
- 스트레스 조건에서 1/20로 재현되던 버그: 그 조건에서 0 실패 **59회**면 "그 조건에서 1/20 미만"(0.95^59 = 0.048). 빠르지만 **두 단계로 말한다**: 스트레스 조건 59회+ (메커니즘이 고쳐졌다) → nominal 1,497회 (원래 요구 수준).
- **말로**: "To claim it's below one in five hundred I need about fifteen hundred consecutive fail-free runs — 1,497 — at the condition where it failed. If I had a stress condition that reproduced it one in twenty, I'd first show sixty clean runs there, which proves the mechanism is gone, then run the fifteen hundred at nominal across several boards overnight."
- 한국어 핵심: 1/500 → 1,497회 · 실패 1번 섞이면 2,371회 · 스트레스 1/20 → 59회 먼저.

</details>

#### 문제 4 · 칩 몇 개, 보드 몇 장, 코너 어떻게?

① 무작위 칩 몇 개가 0 fail이면 "99%의 칩이 margin 기준 충족(95% 신뢰)"? ② AEC-Q100의 77개 × 3 lot은 무슨 뜻? ③ 공정 코너마다 3개씩 9개 칩으로 "칩 10%에만 생기는 결함"을 볼 확률은? ④ 3 공정 × 3 칩 × 5 V/F/T 코너, 세션당 15분이면 시간은?

<details><summary>풀이 · 답</summary>

- ① success-run n = ln(1 − 0.95)/ln(0.99) = **299개** (R99/C90이면 230개, R99.9/C95면 2,995개). → **검증 단계에서 모집단 통계는 불가능** — 그건 양산 테스트(ATE/SLT)와 qualification의 몫.
- ② 231개 0 fail → 90% 상한 0.0099 ≈ **1%** (R99/C90). lot 하나(77개)만 보면 0.0295 ≈ 3% [4]. 목적이 **신뢰성 qualification**이라 보드 검증 숫자와 다르다 ([BV14 §2.4](2026-11-01_BV14_board_validation_plan_automation.html)).
- ③ 결함이 칩의 10%에 있다면 n개 중 1개 이상 볼 확률 1 − 0.9^n: n=3 → **0.271**, n=9 → **0.613**, n=22 → **0.902** (90%를 원하면 22개). → 소수 칩으로는 "드문 칩 결함"을 놓친다 — 그래서 **코너 칩(SS/FF)으로 경계를 보고**, 칩 간 분산은 margin(마진 크기의 분포)으로 본다. PASS/FAIL보다 **margin 숫자**가 소수 표본에서 정보가 훨씬 많다 ([BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)).
- ④ 3 × 3 × 5 = **45 세션 × 15분 = 11.25시간**, 보드 3장 병렬 → **3.75시간** + 온도 스텝마다 챔버 안정화(soak) 시간 [추정: 스텝당 30–60분] — 실제론 **온도를 바깥 루프**로 돌려 챔버 전환 횟수를 최소화.
- 보드 수: **≥ 2장**이어야 "이 보드만?"을 가른다 (코드의 E7). 카메라 보드가 1장뿐이면 CSI 결과는 "칩 문제인지 보드 문제인지" 모름 → §6 CSI 판정.
- **말로**: "Validation can't prove population statistics — 299 chips for 99% at 95% is production-test territory. What I do in validation is bound the corners: at least three chips each of TT, SS and FF, at the worst voltage-temperature combinations, on at least two boards, and I record margin, not just pass/fail, so the distribution tells me how close the worst chip is."
- 한국어 핵심: 모집단 통계(299개)는 양산 몫, 검증은 코너 칩으로 경계 + margin 분포 + 보드 ≥ 2.

</details>

### 2.5 메트릭 — 무엇을 매주 보나 (BV14 §2.14 일반화)

| 메트릭 | 정의 | 읽는 법 · 함정 |
|---|---|---|
| requirement coverage (P0/P1) | covered REQ / 계획 REQ, 최신 빌드 | P0·P1 따로. 실행률(executed)과 통과율(covered) 구분 |
| cross coverage 히트맵 | 블록 × 코너 셀 채움률 | 빈칸이 **최악 코너**에 몰려 있으면 위험 (DDR 예: SS·LV-Fmax-125C) |
| pass rate per build | PASS 세션 / 실행 세션, 빌드별 | 빌드 간 하락 = 회귀. BLOCKED는 분모에서 빼고 따로 셈 |
| first-pass rate · flaky 비율 | retry 없이 PASS / 실행 · FLAKY / 실행 | retry 포함 pass rate는 flaky를 숨김 |
| 버그 find/fix 추세 | 주별 신규 버그 수 vs 수정 수 | **find rate가 꺾였나**: 테스트가 새 영역에 들어갈 때마다 다시 오르는 게 정상. 꺾였는데 커버리지가 낮으면 "안 찾고 있는 것" |
| open 버그 severity 분포 | S1/S2/S3/S4 열린 수 | S1/S2 0이 종료 조건. S3가 쌓이면 errata 문서 부담 |
| 테스트 실행 시간 | 회귀 전체 wall-clock, 테스트별 | nightly가 밤을 넘기면 계층 재조정 (N06G) |
| 보드/팜 가용률 | 테스트 가능 보드-시간 / 전체 | 인프라 실패와 DUT 실패를 분리 |
| 신뢰성 상한 | 요구사항별 UB95 vs p_max | "0번 봤다"를 숫자로 |
| escape 수 | 다음 단계(SLT·고객)에서 발견된 버그 | 커버리지 신뢰도의 사후 지표 (§2.7) |

```text
 버그 수/주
   12 │      ●                          find (신규)
   10 │   ●     ●                       fix  (수정)  ○
    8 │ ●   ○      ●   ○ ○
    6 │   ○    ○ ○   ●       ●  ← PVT 캠페인 시작 → 다시 상승 (정상)
    4 │                ○ ●     ○ ○
    2 │                    ●       ● ●
    0 └──────────────────────────────────► 주
      A0 bring-up   기능 검증     PVT       sign-off 후보
  읽는 법: find < fix 가 몇 주 연속 + 커버리지 ≥ 목표 + open S1/S2 0 → 종료 후보
```

- **severity 정의 [예시 — 회사마다 다름]**: S1 = 기능 상실·데이터 손상·안전 영향, workaround 없음 · S2 = 주요 기능 영향, workaround 있음(성능·전력 비용) · S3 = 제한적 영향·드문 조건 · S4 = 외관·문서.

### 2.6 종료 기준(exit criteria) 템플릿 — 시작 전에 합의

| ID | 기준 | 측정 방법 | 임계 [예시] | 증거 | 결정권자 |
|---|---|---|---|---|---|
| E1 | P0 요구사항 100% PASS 또는 승인 waiver | 최신 빌드 결과 DB | 100% | 커버리지 리포트 | 검증 리드 |
| E2 | P1 요구사항 커버리지 | 같음 | ≥ 90%, 나머지는 리스크 목록 | 같음 | 검증 리드 + PM |
| E3 | P0 cross 셀(요구사항 × V/F/T × 공정) 전부 PASS | 셀 히트맵 | 빈칸 0 | 히트맵 | 검증 리드 + 설계 |
| E4 | 신뢰성 요구사항 상한 < 목표 | Clopper-Pearson 95% | UB95 < p_max | 상한 표 | 검증 + 품질 |
| E5 | 열린 S1/S2 버그 0 | 버그 트래커 | 0 (S3+는 errata 문서화) | 버그 리스트 | 버그 리뷰 위원회 |
| E6 | 모든 P0 테스트가 실패할 수 있음 증명 | negative control / fault injection 기록 | 100% (미증명은 WARN) | 대조군 로그 | 검증 리드 |
| E7 | 표본 분산 | P0 실행의 칩·보드 수 | ≥ 3칩 (TT/SS/FF), ≥ 2보드 | 결과 DB | 검증 리드 |
| E8 | 회귀 안정성 | nightly 이력 | 마지막 5 nightly 신규 실패 0, flaky < 2% | 회귀 대시보드 | 검증 리드 |
| E9 | errata 전부 workaround 확인 | errata 문서 ↔ 테스트 | 100% workaround를 테스트로 검증 | errata 표 | 설계 + SW |
| E10 | 리포트 리뷰 sign-off | 리뷰 회의록 | 설계·DV·SW·제품 서명 | 회의록 | 프로그램 |

- **단계별로 다르다** (같은 틀, 다른 임계):
  - **A0 bring-up exit**: 부팅·디버그 접근·클럭/리셋·DDR 기본·주요 I/O 링크 업 → "기능 검증 시작 가능" 판정.
  - **기능 검증 exit**: E1·E2·E5·E6 중심, 코너는 NOM 위주.
  - **특성화(PVT) exit**: E3·E4 + margin ≥ 목표, shmoo 리포트.
  - **SLT 이관 exit**: 양산 테스트로 줄여 넣을 콘텐츠 선정·시간 예산·escape 커버 확인.
- 판정 3단계 (코드와 같음): **DONE**(전부 PASS) · **CONDITIONAL**(waiver 또는 WARN만 — 서명자가 리스크를 알고 승인) · **NOT DONE**(하나라도 FAIL).
- Staff 문장: "Exit criteria are a contract, not a mood. They're agreed with design, DV, software and product before the first A0 arrives. When the schedule runs out first, I don't move the goalposts — I publish which criteria are red, the residual risk of each, and what it would take to close them, and the program decides."

### 2.7 "커버리지 100%인데 버그가 나왔다" — 커버리지가 거짓말하는 경우

| # | 원인 | 실제 모습 [예시] | 어떻게 막나 |
|---|---|---|---|
| 1 | **체크가 실제 HW를 안 읽음** | DMA 결과를 캐시된 사본과 비교(무효화 안 함) · 버퍼를 자기 자신과 비교 · 상태 레지스터 대신 SW shadow 변수 확인 | negative control: 일부러 목적지 1바이트 오염 → FAIL 나는지 |
| 2 | **조건 조합 누락 (cross hole)** | 고온은 nominal V에서만, 저전압은 상온에서만 → 고온+저전압 조합 미검증 · DMA 단독만, DDR 트래픽 동시 없음 | 코너 등급을 계획에 명시, 히트맵으로 빈칸 확인, 동시성 행 필수 |
| 3 | **테스트가 실패를 못 함** | timeout을 PASS로 처리 · 반환 코드 무시 · 릴리스 빌드에서 assert 꺼짐 · 비교 길이 0 | mutation(체크 일부러 틀리게) · 결과 라인에 비교 바이트 수 기록 · N06G "test of the test" |
| 4 | **요구사항 자체가 없음 (spec gap)** | spec에 "링크 재학습 중 DMA"가 없어서 요구사항 없음 → 커버리지 100%는 **적힌 것에 대한** 100% | 설계자·SW·고객 사용 시나리오 리뷰, 과거 errata·escape에서 요구사항 추가 |
| 5 | **표본 부족** | 0/300 PASS로 covered → 실제 1/500 버그 | 신뢰성 요구사항에 p_max 명시 → 상한 기준 (E4) |
| 6 | **표본 편중** | TT 칩만, 보드 1장만, 한 lot만 | E7 (칩·보드 분산), 코너 칩 |
| 7 | **환경 차이** | 테스트 FW에선 OK, 고객 SW 스택(다른 드라이버·워크로드·전력 관리) 에서 실패 · 노화 | 시스템/interop 행, 실제 SW 스택 회귀, soak |
| 8 | **stale 증거** | fw1.3에서 PASS, fw1.4에서 안 돌림 | 증거는 최신 빌드만 (코드에서 강제) |

- **escape 분석 템플릿 (5줄)**: ① 무엇이 escape됐나 (증상·조건·발견 단계) ② 그 요구사항이 계획에 있었나 (없음 → #4) ③ 있었다면 그 셀이 실행됐나 (안 됨 → #2/#6) ④ 실행됐다면 테스트가 그 실패를 잡을 수 있었나 (negative control 재현 → #1/#3) ⑤ 바꿀 것: 요구사항 추가 · 테스트 수정 · 종료 기준 수정 + **같은 종류 구멍 전수 조사**.
- 면접 문장: "When a bug escapes past 100% coverage, coverage measured the wrong thing. I walk it back: was the requirement in the plan, was that corner executed, could the test have failed at all. In my experience the most common answer is the third — the check didn't read what we thought it read — which is why every P0 test needs a negative control before its PASS counts."

### 2.8 주간 리포트 1장 템플릿 (BV14 체인 E 일반화)

```text
 Silicon validation weekly — A0, week 2026-W44  (build fw1.4)          [예시]
 ────────────────────────────────────────────────────────────────────────────
 1. Status        DMA CONDITIONAL · I2C DONE · DDR NOT DONE · CSI NOT DONE
 2. Coverage      P0 16/18 (89%)  P1 10/11 (91%, 1 waiver)  cells 137/141
                  heat-map holes: DDR SS@LV-Fmax-125C (blocked), CSI SS@HV-Fmax-m40C (fail)
 3. Reliability   DMA-006 UB95 0.00091 < 1e-3 ✔   DDR-004 0.00249 > 1e-3 (+1,795 runs)
                  CSI-003 2/2505 → UB95 0.00251 > 1e-3
 4. Bugs          open S1 0 · S2 2 (BUG-DDR-11, BUG-CSI-17) · S3 1   find 4 / fix 5 this week
 5. Trend         pass rate DMA fw1.3 33% → fw1.4 98% · flaky 1.2% · farm availability 93%
 6. Top 3 risks   (1) CSI only one camera board → can't separate chip vs board
                  (2) DDR worst corner blocked by hang → no evidence where margin is smallest
                  (3) DMA-005 error test never shown to fail
 7. Asks          2nd camera board by W46 · 3 days chamber time · decision on ERR-DMA-01
 8. Next week     DDR soak +1,800 runs on 3 boards · CSI root-cause at -40 °C · DMA-005 neg control
```

- 원칙: **1장 · 숫자 · 빨간 것 먼저 · 요청(ask) 명시.** 매니저가 1분 안에 "무엇이 막혔고 나에게 뭘 원하나"를 알아야 한다.

## 3. 실전 적용 — 예시: DMA 블록 커버리지 리포트 한 장

> 상황 [추정]: Rivian AI SoC A0, 시스템 DMA (멀티채널, descriptor/scatter-gather, 2D stride) — 실제 IP 구성 미확인. 요구사항은 [N06D D1 DMA 테스트 플랜](2026-11-01_N06D_plans_dma_memory.html)의 매트릭스에서 P0/P1만 뽑은 형태 [예시].

### 3.1 계획 (분모) — requirements.csv의 DMA 10줄

| REQ | P | 요구사항 (검증 가능한 문장) | 코너 | p_max | 비고 |
|---|---|---|---|---|---|
| DMA-001 | P0 | 레지스터 reset 값 · RW/RO/W1C 속성 | NOM | — | 자동 생성 |
| DMA-002 | P0 | 1 B ~ 64 KiB 단일 mem-to-mem 복사 정확 | PVT | — | 15셀 |
| DMA-003 | P0 | 크기 × 정렬(오프셋 0–63 B) 매트릭스 | NOM | — | TC 2개(003a/b) |
| DMA-004 | P0 | descriptor chain · SG · ring wrap | PVT | — | BUG-DMA-03 수정 확인 |
| DMA-005 | P0 | 잘못된 주소 → abort + 에러 IRQ + 채널 복구 | NOM | — | **negative control 미증명** |
| DMA-006 | P0 | 8채널 stress 데이터 무결성, 실패율 < 1e-3/run | PVT | 0.001 | 신뢰성 요구 |
| DMA-007 | P1 | 4 KiB burst에서 AXI 이론 대역폭 ≥ 90% | NOM | — | BUG-DMA-07 (S3) open |
| DMA-008 | P1 | 인터럽트 coalescing (개수·timeout) | NOM | — | |
| DMA-009 | P1 | non-coherent master 캐시 clean/invalidate | NOM | — | |
| DMA-010 | P1 | 최대 프레임 2D stride 전송 | NOM | — | ERR-DMA-01 waiver |

### 3.2 증거 → 판정 (실제 스크립트 출력에서 DMA 부분, §6 전체)

```text
---- block DMA ----
REQ      PRI STATUS   CELLS RUNS  ITER FAIL     UB95  P_MAX  NOTE
DMA-001  P0  PASS      1/1     3     3    0
DMA-002  P0  PASS     15/15   15   300    0
DMA-003  P0  PASS      1/1     3   192    0
DMA-004  P0  PASS     15/15   15   750    0
DMA-005  P0  PASS      1/1     2    20    0                  no negative control
DMA-006  P0  PASS     15/15   15  3300    0  0.00091  0.001  bound OK (3/n=0.00091)
DMA-007  P1  PASS      1/1     2    10    0
DMA-008  P1  PASS      1/1     1    10    0
DMA-009  P1  PASS      1/1     2    20    0
DMA-010  P1  WAIVED    0/1     1    20    1                  waiver ERR-DMA-01
coverage   P0 6/6 = 100.0% (waived 0, executed 6/6)   P1 4/4 = 100.0% (waived 1, executed 4/4)
cross cov  51/52 req x V/F/T x process cells =  98.1%
pass rate  (sessions) fw1.3 1/3 =  33.3% -> fw1.4 58/59 =  98.3%; blocked 0
samples    P0 chips=6 (FF/SS/TT) boards=3
bugs       open S1/S2: none; open S3+: BUG-DMA-07
fail at    DMA-010 1/20 on A0-TT-01 EVB-01 NV-Fnom-25C
  E6 P0 tests proven to fail (neg ctrl)      WARN  DMA-005
VERDICT DMA: CONDITIONAL (waivers 1, warnings 1)
```

### 3.3 이걸 회의에서 말하는 법 (2분, English)

- "DMA is conditional, not done. All six P0 requirements pass on fw1.4 at every required corner — 51 of 52 cells, the missing one is the waived 2D-stride case."
- "Data integrity under eight-channel stress: 3,300 iterations across TT, SS and FF chips at five V/F/T corners, zero failures, so the 95% upper bound is 0.09% — under the 0.1% target."
- "The fw1.3 to fw1.4 pass-rate jump, 33% to 98%, is the ring-wrap fix for BUG-DMA-03; those fw1.3 runs are trend only, not evidence."
- "Two things keep it conditional. First, ERR-DMA-01: max-size 2D stride fails one in twenty; the workaround is splitting the transfer in the driver, and I need product to sign the waiver. Second, the bad-address abort test has never been shown to fail — I'll run it with the error interrupt masked and confirm the test reports FAIL. Until then its PASS doesn't count for much."
- "Open S3: bandwidth is 3% short at 64-byte bursts — goes in the errata, not blocking."
- 한국어 핵심: P0 100% + 신뢰성 상한 OK여도 **waiver 1 + negative control 미증명 1** → CONDITIONAL. 이전 빌드는 추세용.

### 3.4 판정을 바꾸려면 (action list)

| 상태 → 목표 | 할 일 | 예상 비용 [추정] |
|---|---|---|
| E6 WARN → PASS | DMA-005: 에러 IRQ mask 상태에서 테스트가 FAIL 보고하는지 + abort 후 채널 복구 안 되게 만든 mutation | 0.5일 |
| waiver → 서명 | ERR-DMA-01 workaround(드라이버에서 분할)를 테스트로 검증, 제품·SW 서명 | 1일 + 리뷰 |
| (보너스) DMA-007 S3 | errata 문서화, 64 B burst 성능 수치 공개 | 0.5일 |

## 4. 꼬리 질문 체인

### 체인 A — "How do you know you're done validating a block?"

Q → 꼬리1 "Who defines the criteria?" → 꼬리2 "Schedule says tape-out/ship in two weeks and two criteria are red. What do you do?" → 꼬리3 "How is your coverage different from DV's?"

<details><summary>A-Q. How do you know you're done validating a block?</summary>

- **EN**: "Done means the exit criteria we agreed before starting are met. Concretely: every P0 requirement has passing evidence on the release-candidate build at every corner it requires, or an approved waiver; P1 above an agreed percentage; reliability requirements backed by a statistical upper bound below target; zero open severity-1 and -2 bugs; every P0 test proven able to fail; and evidence from at least three chips across process corners on more than one board. I compute that automatically from the results database and the plan, so it's a report, not an opinion."
- 한국어 핵심: 사전 합의된 E1–E7을 DB에서 자동 판정.

</details>

<details><summary>A-꼬리1. Who defines the criteria?</summary>

- **EN**: "Validation drafts them, because we know what's measurable, but design, DV, software and product sign them before A0 arrives. Design tells me which corners are risky, product tells me which features customers depend on, and software tells me which errata workarounds they can live with. Getting signatures up front is what makes the final review short."
- 한국어 핵심: 초안은 검증, 서명은 설계·DV·SW·제품 — A0 전에.

</details>

<details><summary>A-꼬리2. Two criteria are red and the date is fixed. What do you do?</summary>

- **EN**: "I don't relabel red as green. I publish exactly which criteria are open, the residual risk of each — for example 'DDR worst corner has no evidence because the board hangs' — and options with cost: more boards in parallel, a narrower claim like 'qualified to 105 °C instead of 125 °C', or ship with an errata and a software mitigation. The program owns the decision; my job is to make the risk impossible to misunderstand."
- 한국어 핵심: 기준을 바꾸지 말고 리스크·선택지·비용을 명확히, 결정은 프로그램.

</details>

<details><summary>A-꼬리3. How is your coverage different from DV's coverage?</summary>

- **EN**: "DV's functional coverage counts which stimulus values and combinations happened in RTL simulation — it proves the logic was exercised. It can't see timing, analog, power integrity or process variation. My coverage is requirement-by-corner evidence on real chips. I read DV's closure report so I don't repeat what they covered, and I spend silicon time where only silicon can answer."
- 한국어 핵심: DV = 시뮬에서 자극 공간, validation = 실제 칩·코너에서 요구사항 증거.

</details>

### 체인 B — "How many times do you run a test?"

Q → 꼬리1 "You ran 300 times with no failures. Is it good?" → 꼬리2 "A 1-in-500 bug was fixed. Prove it." → 꼬리3 "How many chips?"

<details><summary>B-Q. How many times do you run a test?</summary>

- **EN**: "It depends on the claim. A functional check at a corner needs a handful of runs; a reliability claim needs statistics. With zero failures in n runs, the 95% upper bound on the failure rate is about 3 over n — the rule of three. So to claim below one in a thousand I need about 3,000 fail-free runs, spread across chips and boards, at the conditions that matter."
- 한국어 핵심: 기능은 몇 번, 신뢰성은 3/p.

</details>

<details><summary>B-꼬리1. 300 runs, no failures — is that good?</summary>

- **EN**: "It tells me the failure rate is below about one percent, one in a hundred, with 95% confidence. If the requirement is one in a thousand, 300 clean runs say nothing either way — I need ten times more."
- 한국어 핵심: 0/300 → 1% 미만만 말할 수 있다.

</details>

<details><summary>B-꼬리2. A 1-in-500 bug was fixed. Prove it.</summary>

- **EN**: "First confirm the original rate with enough runs, because seeing zero failures in 200 runs before the fix proves nothing. After the fix, 1,497 consecutive clean runs at the failing condition gives a 95% bound below one in 500 — if the bug were still there, the chance of that many clean runs is under five percent. If I have a stress condition that raises the rate to one in twenty, sixty clean runs there is fast evidence the mechanism is gone, then the full run at nominal. And I add the failure signature to nightly so it can't come back silently."
- 한국어 핵심: 수정 전 재현율 확정 → 수정 후 1,497 무실패(또는 스트레스 59회 먼저) → 회귀에 고정.

</details>

<details><summary>B-꼬리3. How many chips?</summary>

- **EN**: "Population claims like '99% of parts pass' need about 300 random parts with zero failures — that's qualification and production test, not validation. In validation I bound the corners: at least three chips each of TT, SS and FF from the skew lot, at the worst voltage-temperature combinations, on at least two boards. I record margin rather than pass/fail, because with three chips a margin distribution tells me far more than three green checkmarks."
- 한국어 핵심: 모집단은 양산 몫, 검증은 코너 칩 × 보드 ≥ 2 × margin.

</details>

### 체인 C — "Coverage was 100%, but a bug escaped to the customer. What happened?"

Q → 꼬리1 "How do you know a test can fail?" → 꼬리2 "What changes in your process?"

<details><summary>C-Q. Coverage was 100%, but a bug escaped. What happened?</summary>

- **EN**: "Coverage measured the wrong thing. I'd walk the escape back through four questions: was the requirement even in the plan, was that exact condition combination executed, could the test have detected the failure, and was the sample big enough. Typical answers are a missing cross — hot was tested at nominal voltage and low voltage at room temperature, but never together — or a check that compared against a cached copy, so it could never fail."
- 한국어 핵심: 계획에 있었나 → 셀 실행됐나 → 테스트가 잡을 수 있었나 → 표본 충분했나.

</details>

<details><summary>C-꼬리1. How do you know a test can fail?</summary>

- **EN**: "I make it fail on purpose. A negative control — corrupt one destination byte, mask the interrupt, or point it at a wrong expected value — must produce FAIL. For error-path tests I use the IP's error-injection registers if they exist. I also log what the check compared, like the number of bytes, so a check that compared zero bytes is visible. A test that has never failed hasn't been tested."
- 한국어 핵심: negative control · 오류 주입 · 비교량 로그.

</details>

<details><summary>C-꼬리2. What changes in your process after the escape?</summary>

- **EN**: "Three things: add the missing requirement or cell to the plan with a test that reproduces the escape, fix the class of hole rather than the instance — audit every other test for the same check pattern — and if needed tighten the exit criteria, for example making negative controls blocking instead of a warning. Then I report the escape analysis to the same people who signed the exit criteria."
- 한국어 핵심: 인스턴스가 아니라 같은 종류 구멍 전수 조사 + 기준 강화.

</details>

### 체인 D — "What do you report to management every week?"

<details><summary>D-Q. What's on your weekly report?</summary>

- **EN**: "One page: status per block against the exit criteria — done, conditional, not done; coverage for P0 and P1 with the corner heat-map holes; reliability bounds versus targets; open bugs by severity with find and fix rates; pass-rate trend per build and flaky rate; then the top three risks and what I need — boards, chamber time, or a decision. Red items first, numbers not adjectives."
- 한국어 핵심: 판정 · 커버리지 · 상한 · 버그 추세 · 리스크 3개 · 요청.

</details>

<details><summary>D-꼬리1. Bug find rate dropped to near zero. Are you done?</summary>

- **EN**: "Not by itself. A falling find rate means either the silicon is clean or we stopped looking somewhere new. I check it against coverage: if coverage is high and the find rate dropped across several weeks and new areas, that's a good sign. If coverage is still low, the drop means the farm is stuck or we're re-running the same tests. Find rate usually jumps again when a PVT campaign starts — that's expected."
- 한국어 핵심: find rate 하락은 커버리지와 같이 읽어야 의미.

</details>

## 5. Don 경험 연결

- "At SK hynix I automated chamber reliability testing for NAND and PCIe interfaces, so 'how many cycles across how many devices at which temperatures' was a daily question — we ran [숫자 채우기] DUTs per chamber and reported failure rates per corner, not pass/fail."
- "At Solidigm, during pre-silicon FPGA bring-up of the Cortex-R and PCIe/NVMe subsystem, I verified I2C, SPI, DMA, SRAM and DRAM blocks before RTL freeze — [숫자 채우기] tests that we traced to subsystem requirements, which is the same requirement-to-evidence chain I'd use here."
- "At Apple, factory test nodes for latent defects taught me that a test which never fails is suspicious — I'd validate the screen by injecting or selecting known-bad units before trusting its PASS [숫자 채우기: 사례]."

## 6. 실습 / 코드

### 6.1 파일 구조와 데이터 설계

| 파일 | 역할 |
|---|---|
| [requirements.csv](onsitePrep/n06/b/requirements.csv) | **계획(분모)**: 4블록 29 요구사항 — block · req_id · priority · title · corners(NOM/PVT) · p_max · waiver · neg_control |
| [gen_mock.py](onsitePrep/n06/b/gen_mock.py) | mock 결과 생성(결정적, 랜덤 없음) → results.csv 167행: run_id · build · block · req_id · test_id · chip · board · vft_corner · iterations · fails · result |
| [bugs.csv](onsitePrep/n06/b/bugs.csv) | 버그 트래커 export 5건 (severity S1–S4, open/closed) |
| [coverage_report.py](onsitePrep/n06/b/coverage_report.py) | 블록별 requirement·cross 커버리지, pass rate(빌드별), Clopper-Pearson/rule-of-three 상한, 필요한 추가 실행 수, E1–E7 판정 |
| [stats_problems.py](onsitePrep/n06/b/stats_problems.py) | §2.4 문제 4개 숫자 검증 (coverage_report의 함수 재사용) |
| [run.sh](onsitePrep/n06/b/run.sh) | 생성 → 리포트 → 통계 검증, 전부 `python3 -I` (stdlib만) |
| [run_output.txt](onsitePrep/n06/b/run_output.txt) | 아래 출력 원본 |

- 설계 의도 — 블록마다 다른 종료 판정이 나오게 시나리오를 넣음: **DMA** = 다 좋은데 waiver 1 + negative control 미증명 → CONDITIONAL · **I2C** = 깨끗 → DONE · **DDR** = 최악 코너가 hang으로 BLOCKED + soak 부족 + P1 미실행 → NOT DONE · **CSI** = 한 코너에서 실제 CRC 실패 + 카메라 보드 1장 → NOT DONE.
- 코너 모델: V/F/T 5개 (`NV-Fnom-25C` nominal, `LV/HV × Fmax × 125C/m40C` 극한 4개) × 공정 TT/SS/FF (칩 ID `A0-SS-02`에서 추출). `PVT` 요구사항 = 15셀, `NOM` = nominal 1셀(공정 무관).
- 규칙: 증거는 **최신 빌드(fw1.4)만**, BLOCKED는 실행에서 제외, FAIL 세션이 하나라도 있으면 요구사항 FAIL, 필요 셀 중 빈칸 있으면 PARTIAL, PASS가 아닌데 waiver 있으면 WAIVED.
- 상한: k=0이면 1 − α^(1/n), k>0이면 이항 CDF(log-gamma) bisection으로 Clopper-Pearson; 필요 실행 수는 n에 대한 이분 탐색.

### 6.2 실제 실행 출력 (macOS, `./run.sh`, 2026-11-01) [확인됨]

```text
wrote 167 rows -> results.csv
== N06B exit-criteria report: 29 reqs, 167 result rows, 5 bugs; evidence build = fw1.4 ==
   rules: 95% one-sided bound, P1 target >= 90%, >= 3 chips and >= 2 boards on P0 runs

---- block DMA ----
REQ      PRI STATUS   CELLS RUNS  ITER FAIL     UB95  P_MAX  NOTE
DMA-001  P0  PASS      1/1     3     3    0
DMA-002  P0  PASS     15/15   15   300    0
DMA-003  P0  PASS      1/1     3   192    0
DMA-004  P0  PASS     15/15   15   750    0
DMA-005  P0  PASS      1/1     2    20    0                  no negative control
DMA-006  P0  PASS     15/15   15  3300    0  0.00091  0.001  bound OK (3/n=0.00091)
DMA-007  P1  PASS      1/1     2    10    0
DMA-008  P1  PASS      1/1     1    10    0
DMA-009  P1  PASS      1/1     2    20    0
DMA-010  P1  WAIVED    0/1     1    20    1                  waiver ERR-DMA-01
coverage   P0 6/6 = 100.0% (waived 0, executed 6/6)   P1 4/4 = 100.0% (waived 1, executed 4/4)
cross cov  51/52 req x V/F/T x process cells =  98.1%
pass rate  (sessions) fw1.3 1/3 =  33.3% -> fw1.4 58/59 =  98.3%; blocked 0
samples    P0 chips=6 (FF/SS/TT) boards=3
bugs       open S1/S2: none; open S3+: BUG-DMA-07
fail at    DMA-010 1/20 on A0-TT-01 EVB-01 NV-Fnom-25C
  E1 P0 requirements 100% pass or waived     PASS
  E2 P1 requirements >= 90%                  PASS  100.0%
  E3 P0 cross-coverage cells all hit         PASS
  E4 failure-rate bound < p_max (95%)        PASS
  E5 open S1/S2 bugs = 0                     PASS
  E6 P0 tests proven to fail (neg ctrl)      WARN  DMA-005
  E7 sample spread chips/boards              PASS  chips 6, boards 3
VERDICT DMA: CONDITIONAL (waivers 1, warnings 1)

---- block I2C ----
REQ      PRI STATUS   CELLS RUNS  ITER FAIL     UB95  P_MAX  NOTE
I2C-001  P0  PASS      1/1     3     3    0
I2C-002  P0  PASS     15/15   15  1500    0
I2C-003  P0  PASS      1/1     3    60    0
I2C-004  P0  PASS      1/1     3  1600    0  0.00187  0.002  bound OK (3/n=0.00187)
I2C-005  P1  PASS      1/1     2    20    0
I2C-006  P1  PASS      1/1     1    10    0
coverage   P0 4/4 = 100.0% (waived 0, executed 4/4)   P1 2/2 = 100.0% (waived 0, executed 2/2)
cross cov  20/20 req x V/F/T x process cells = 100.0%
pass rate  (sessions) fw1.3 2/2 = 100.0% -> fw1.4 27/27 = 100.0%; blocked 0
samples    P0 chips=6 (FF/SS/TT) boards=3
bugs       open S1/S2: none; open S3+: none
  E1 P0 requirements 100% pass or waived     PASS
  E2 P1 requirements >= 90%                  PASS  100.0%
  E3 P0 cross-coverage cells all hit         PASS
  E4 failure-rate bound < p_max (95%)        PASS
  E5 open S1/S2 bugs = 0                     PASS
  E6 P0 tests proven to fail (neg ctrl)      PASS
  E7 sample spread chips/boards              PASS  chips 6, boards 3
VERDICT I2C: DONE

---- block DDR ----
REQ      PRI STATUS   CELLS RUNS  ITER FAIL     UB95  P_MAX  NOTE
DDR-001  P0  PASS     15/15   15  2010    0  0.00149  0.002  bound OK (3/n=0.00149)
DDR-002  P0  PARTIAL  14/15   14    14    0
DDR-003  P0  PASS      1/1     2    40    0
DDR-004  P0  PASS      1/1     3  1200    0  0.00249  0.001  bound > p_max: need +1795 fail-free runs
DDR-005  P1  PASS      1/1     2   100    0
DDR-006  P1  PASS      1/1     2    20    0
DDR-007  P1  NOT_RUN   0/1     0     0    0
coverage   P0 3/4 =  75.0% (waived 0, executed 4/4)   P1 2/3 =  66.7% (waived 0, executed 2/3)
cross cov  33/35 req x V/F/T x process cells =  94.3%
pass rate  (sessions) fw1.4 38/38 = 100.0%; blocked 1
samples    P0 chips=6 (FF/SS/TT) boards=3
bugs       open S1/S2: BUG-DDR-11; open S3+: none
  E1 P0 requirements 100% pass or waived     FAIL  DDR-002:PARTIAL
  E2 P1 requirements >= 90%                  FAIL  66.7%
  E3 P0 cross-coverage cells all hit         FAIL  DDR-002@SS/LV-Fmax-125C(BLOCKED)
  E4 failure-rate bound < p_max (95%)        FAIL  DDR-004 UB=0.00249 >= 0.001 (+1795 runs)
  E5 open S1/S2 bugs = 0                     FAIL  BUG-DDR-11
  E6 P0 tests proven to fail (neg ctrl)      PASS
  E7 sample spread chips/boards              PASS  chips 6, boards 3
VERDICT DDR: NOT DONE (5 criteria failing)

---- block CSI ----
REQ      PRI STATUS   CELLS RUNS  ITER FAIL     UB95  P_MAX  NOTE
CSI-001  P0  PASS      1/1     3     3    0
CSI-002  P0  PASS     15/15   15   150    0
CSI-003  P0  FAIL     14/15   15  2505    2  0.00251  0.001  bound > p_max: need +3789 fail-free runs
CSI-004  P0  PASS      1/1     2    20    0
CSI-005  P1  PASS      1/1     1     5    0
CSI-006  P1  PASS      1/1     1     5    0
coverage   P0 3/4 =  75.0% (waived 0, executed 4/4)   P1 2/2 = 100.0% (waived 0, executed 2/2)
cross cov  33/34 req x V/F/T x process cells =  97.1%
pass rate  (sessions) fw1.4 36/37 =  97.3%; blocked 0
samples    P0 chips=6 (FF/SS/TT) boards=1
bugs       open S1/S2: BUG-CSI-17; open S3+: none
fail at    CSI-003 2/167 on A0-SS-01 EVB-03 HV-Fmax-m40C
  E1 P0 requirements 100% pass or waived     FAIL  CSI-003:FAIL
  E2 P1 requirements >= 90%                  PASS  100.0%
  E3 P0 cross-coverage cells all hit         FAIL  CSI-003@SS/HV-Fmax-m40C(FAIL)
  E4 failure-rate bound < p_max (95%)        FAIL  CSI-003 UB=0.00251 >= 0.001 (+3789 runs)
  E5 open S1/S2 bugs = 0                     FAIL  BUG-CSI-17
  E6 P0 tests proven to fail (neg ctrl)      PASS
  E7 sample spread chips/boards              FAIL  chips 6, boards 1
VERDICT CSI: NOT DONE (5 criteria failing)

==== summary ====
BLOCK  P0 cov  P1 cov   cells  VERDICT
DMA    100.0%  100.0%   98.1%  CONDITIONAL
I2C    100.0%  100.0%  100.0%  DONE
DDR     75.0%   66.7%   94.3%  NOT DONE
CSI     75.0%  100.0%   97.1%  NOT DONE

P1 rule of three: 0 failures in 600 boot cycles
   exact 95% UB = 0.004980 (1 in 201); 3/n = 0.005000
   90% confidence: -ln(alpha) = 2.303 -> UB ~ 0.00384
   95% confidence: -ln(alpha) = 2.996 -> UB ~ 0.00499
   99% confidence: -ln(alpha) = 4.605 -> UB ~ 0.00768
P2 failure-rate upper bound: 3 failures in 2000 cycles
   observed 0.1500%; exact Clopper-Pearson 95% UB = 0.003872 (1 in 258)
   Poisson approx 7.754/n = 0.003877
   soak: 0 failures in 5000 device-hours -> rate UB = 0.000600 /h, MTBF >= 1667 h
P3 claim a 1-in-500 bug is fixed
   0-fail runs needed for UB < 1/500: 1497; P(0 fails in 1497 | still 1/500) = 0.0499
   if 1 failure appears: need 2371 total runs with no more
   claim 10x better (UB < 1/5000): 14978 runs
   at a stress condition where rate is 1/20: 59 runs
P4 how many chips / boards
   success-run R=0.99 C=0.95: n = 299 parts, 0 fail
   success-run R=0.99 C=0.9: n = 230 parts, 0 fail
   success-run R=0.999 C=0.95: n = 2995 parts, 0 fail
   AEC-Q100 77/lot: 90% UB = 0.0295; 231 pooled: 0.0099
   P(see >=1 bad part | 10% bad, n=3) = 0.271
   P(see >=1 bad part | 10% bad, n=9) = 0.613
   P(see >=1 bad part | 10% bad, n=22) = 0.902
   chips for 90% chance to see a 10% defect: 22
   matrix 3 process x 3 chips x 5 V/F/T = 45 sessions x 15 min = 11.25 h; 3 boards parallel = 3.75 h (+ chamber soak per T step)
```

- **독립 검증** (별도 `python3 -I -c`, math.comb로 이항 CDF 직접 합산 — 스크립트 함수 미사용) [확인됨]: 1 − 0.05^(1/600) = 0.0049804 · P(X ≤ 3; 2000, 0.003872) = 0.050015 · P(X ≤ 1; 2371, 0.002) = 0.04992 (2370이면 0.05000 → 2371이 최소) · P(X ≤ 2; 6294, 0.001) = 0.04999 (6293이면 0.05002) · 0.998^1496 = 0.05004 / 0.998^1497 = 0.04994 · 0.95^58 = 0.0510 / 0.95^59 = 0.0485 · 0.99^298 = 0.05004 / 0.99^299 = 0.04954 → 스크립트와 전부 일치. (LP14 §2.5의 "1,496 사이클"은 1496.37을 내림한 값, 올림하면 1,497.)

### 6.3 출력 한 줄씩 읽기

- **헤더** `evidence build = fw1.4`: 결과 DB에 빌드가 2개(fw1.3, fw1.4) — 증거는 최신 것만. `rules:` 줄은 종료 기준의 임계값(신뢰수준·P1 목표·칩/보드 최소). 면접에서 "기준은 파라미터"라고 말할 수 있는 근거.
- **REQ 표 열**: `CELLS` = 필요한 셀 중 PASS 증거가 있는 셀 (PVT 15, NOM 1) · `RUNS` = 실행 세션 수(BLOCKED 제외) · `ITER` = 세션 안 반복 총합 · `FAIL` = 실패 반복 수 · `UB95` = 신뢰성 요구사항만 계산하는 95% 상한 · `P_MAX` = 목표.

**DMA**
- `DMA-002/004/006 15/15`: 5 V/F/T × TT/SS/FF 전 셀 PASS. `DMA-004`는 fw1.3에서 3세션 중 2세션 FAIL(BUG-DMA-03)이었지만 fw1.4 증거만 보므로 PASS — 그 실패는 `pass rate fw1.3 1/3 = 33.3%`로 추세에만 나타남.
- `DMA-005 ... no negative control`: PASS지만 이 테스트가 실패할 수 있음이 증명 안 됨 → E6 WARN. "PASS의 신뢰도"를 따로 표시하는 열이 있다는 게 핵심.
- `DMA-006 UB95 0.00091 < 0.001`: 3,300회 0 실패 → 정확값 0.000907, 3/n = 0.000909. 목표 1e-3을 **겨우** 넘김 — 실행을 2,000회만 했으면 0.0015로 FAIL이었을 것.
- `DMA-010 WAIVED 0/1 ... 1 fail`: 실제로 1/20 실패(`fail at` 줄에 칩·보드·코너)했지만 ERR-DMA-01 waiver가 있어서 covered로 계산(P1 4/4 = 100%, waived 1).
- `cross cov 51/52`: 빠진 1셀은 waiver된 DMA-010의 nominal 셀. E3는 **P0 셀만** 보므로 PASS.
- `pass rate fw1.4 58/59 = 98.3%`: 실패 1세션 = DMA-010. pass rate는 요구사항 커버리지와 다른 지표 — 둘 다 보고한다.
- `bugs ... open S3+: BUG-DMA-07`: S3는 종료를 막지 않음(errata 후보).
- `VERDICT CONDITIONAL (waivers 1, warnings 1)`: FAIL 기준은 없고, 서명자가 알아야 할 것 2개.

**I2C**
- 모든 행 PASS, `I2C-004 0.00187 < 0.002`: bus-stuck 복구 1,600회 0 실패 → "1/500 미만" 주장 가능(1,497회 이상이므로). `fw1.3 2/2 -> fw1.4 27/27`: 이전 빌드도 깨끗. → **DONE**. 이게 "끝난 블록"의 모습: 모든 기준이 숫자로 PASS.

**DDR**
- `DDR-002 PARTIAL 14/14 runs, 14/15 cells`: 15번째 셀(SS 칩 · LV-Fmax-125C)이 `BLOCKED` — 보드 hang(BUG-DDR-11)으로 테스트 시작도 못 함. **빈칸이 하필 timing margin이 가장 작은 최악 코너** — 커버리지 94.3%라는 숫자가 이 위험을 가린다. 그래서 E3는 빈칸을 **이름으로** 출력.
- `pass rate fw1.4 38/38 = 100.0%; blocked 1`: pass rate 100%인데 NOT DONE — BLOCKED를 분모에서 빼면 pass rate는 좋아 보인다. **pass rate만 보고하면 안 되는 이유.**
- `DDR-004 UB 0.00249 > 0.001: need +1795`: 1,200회 0 실패는 "0.25% 미만"까지만 말함. 총 2,995회(=1,200 + 1,795) 필요 → 보드 3장에 600회씩 더.
- `DDR-007 NOT_RUN`: requirements.csv(계획)가 있어서 보이는 행. 결과 DB만 보면 존재 자체가 안 보임. → P1 2/3 = 66.7% → E2 FAIL.
- 판정 **NOT DONE (5 criteria failing)**: E1·E2·E3·E4·E5. 해야 할 일이 기준별로 바로 나온다 — hang 원인 분석(BUG-DDR-11) → 최악 코너 실행 → soak 추가 → DDR-007 실행.

**CSI**
- `CSI-003 FAIL 14/15 cells, 2 fails / 2505`: 실패 2회가 **한 셀에 몰림** — `fail at CSI-003 2/167 on A0-SS-01 EVB-03 HV-Fmax-m40C`. 관측 실패율 0.08%는 목표 0.1%보다 낮아 보이지만 UB95 = 0.00251 > 0.001. 그리고 실패가 무작위가 아니라 **SS 칩 · 고전압 · −40 °C**에 몰렸으니 "율"이 아니라 **조건 의존 버그**로 다뤄야 한다 (N06H 디버그로: 온도 의존 D-PHY HS settle 타이밍 의심 [추정]).
- `+3789 fail-free runs`: 그래도 통계만으로 닫으려면 k=2에서 총 6,294회 필요 — 비현실적 → 정답은 root cause 수정 후 다시 0부터.
- `samples ... boards=1` → E7 FAIL: 카메라 보드 EVB-03 1장뿐. 실패가 칩 탓인지 그 보드(커넥터·케이블·전원) 탓인지 **이 데이터로는 판별 불가** → 두 번째 카메라 보드가 주간 리포트의 "ask"가 된다 (§2.8).
- `pass rate 36/37 = 97.3%`: 세션 단위로는 97%로 좋아 보이지만 P0 신뢰성 요구가 깨져 있음.

**summary**: 커버리지 %만 보면 DDR 94.3%·CSI 97.1%로 "거의 다 됨"처럼 보이지만 둘 다 NOT DONE. **퍼센트가 아니라 기준별 판정을 보고하라**는 이 페이지의 결론이 출력에 그대로 나온다.

**stats 부분**: §2.4 문제 1–4의 답이 그대로 (위 독립 검증과 일치).

### 6.4 확장 아이디어 (면접에서 말할 거리) [미구현]

- 블록 × 코너 **히트맵 HTML** 출력, 빈칸 클릭 → 마지막 로그.
- `neg_control`을 수기 플래그가 아니라 **대조군 실행 결과 행**(result=EXPECTED_FAIL)으로 계산.
- flaky 판정: 같은 셀 같은 빌드에서 PASS/FAIL 혼재 → FLAKY 상태 추가, first-pass rate 열.
- ECO 영향 분석: 변경된 블록·net → 요구사항 → 재실행 목록 자동 생성 (BV14 §2.13).

## 7. 자주 틀리는 것

1. **테스트 개수를 커버리지라고 말함** — "1,200개 테스트 돌렸다"는 분모가 없다. 요구사항 기준 % + 분모를 말한다.
2. **결과 DB만으로 커버리지 계산** — 한 번도 안 돌린 요구사항이 사라진다 (DDR-007). 분모는 계획에서.
3. **"0 실패니까 100%"** — 0/300은 "1% 미만"일 뿐. rule of three로 상한을 말한다.
4. **실패 k번인데 k/n만 보고** — 3/2000 = 0.15%라 말하고 상한 0.39%를 빼먹음. 적은 실패일수록 상한이 관측값보다 훨씬 크다.
5. **pass rate만 보고** — BLOCKED를 분모에서 빼면 DDR처럼 100%가 된다. blocked 수와 셀 빈칸을 같이.
6. **이전 빌드 PASS를 증거로 씀** — stale 증거. 최신 빌드만, 이전 빌드는 추세.
7. **retry로 PASS** — flaky를 숨긴다. first-pass rate + retry 횟수 기록.
8. **1장 보드·TT 칩만으로 결론** — 그 개체에 대한 결론일 뿐. 칩 TT/SS/FF × 보드 ≥ 2.
9. **종료 기준을 마지막 주에 정함** — "시간 다 됐으니 끝"이 된다. A0 전에 서명.
10. **커버리지 %로 "거의 다 됐다"** — 94%의 빈칸이 최악 코너일 수 있다. 빈칸을 이름으로 보고.

## 8. 출처

1. Wikipedia, "Rule of three (statistics)" — 0 사건/n → 95% 상한 3/n, 신뢰수준별 분자(−ln α: 99% → 4.61): https://en.wikipedia.org/wiki/Rule_of_three_(statistics)
2. Wikipedia, "Binomial proportion confidence interval" — Clopper-Pearson 정의·보수성·beta quantile 형태: https://en.wikipedia.org/wiki/Binomial_proportion_confidence_interval
3. NIST/SEMATECH e-Handbook of Statistical Methods §7.2.4.1, "Confidence intervals" (이항 비율, 정확 이항 방법·단측 한계): https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm
4. Accendo Reliability, "AEC-Q100 Test Sample Sizes" — 77/lot × 3 lot = 231, success-run n = ln(1−C)/ln(R), R99/C90: https://accendoreliability.com/aec-q100-test-sample-sizes/
5. Accellera, SystemRDL 2.0 specification (2018) — 레지스터 기술 언어: https://accellera.org/images/downloads/standards/systemrdl/SystemRDL_2.0_Jan2018.pdf
6. Accellera SystemRDL working group overview (레지스터 수명주기, UVM·IP-XACT와의 관계): https://accellera.org/activities/working-groups/systemrdl/systemrdl-details

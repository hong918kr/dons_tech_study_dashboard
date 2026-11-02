# N06A · 방법론 기초 · 공통 테스트 플랜 템플릿 — 카테고리 12종 + 10분 말하기 7단계
<!-- theme: pastel -->
> **묶음**: 방법론 기초 (모든 블록 연습 페이지 C–F · 모의 면접 I의 뼈대) · **우선**: P0 ★ · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) / [N06B 커버리지·종료 기준](2026-11-01_N06B_coverage_metrics_exit_criteria.html) · **관련**: [BV01 큰 그림 (§1.3 검증 용어 5개, §2 칩 일생)](2026-11-01_BV01_si_board_validation_big_picture.html) · [LP14 PCIe 검증 방법론 (§2.1–2.3)](2026-10-31_LP14_validation_methodology.html) · [BV14 보드 검증 계획 (§2.1–2.8)](2026-11-01_BV14_board_validation_plan_automation.html) · [deck §5.1 Q6](2026-10-25_rivian_hm_screen_prep.html) · [BV10 보드 인터페이스 (I2C 보드 쪽)](2026-11-01_BV10_board_interface_tests.html) · [V8 I2C scan·recover](hmScreenPrep/problems/V8_i2c_scan_recover/index.html) · 자세한 I2C 블록 연습은 [N06C](2026-11-01_N06C_plans_peripherals_soc_infra.html) · **코드**: [onsitePrep/n06/a/](onsitePrep/n06/a/run.sh) ([test_matrix_gen.py](onsitePrep/n06/a/test_matrix_gen.py) · [i2c_features.json](onsitePrep/n06/a/i2c_features.json) · [run_output.txt](onsitePrep/n06/a/run_output.txt))

## 0. TL;DR

- 이 페이지로 답할 것: **"How do you write a validation plan for a new IP block?"**, "Design a test plan for X (아무 블록)", "3주밖에 없으면 뭘 먼저?", "뭘 일부러 안 테스트하나?", "DV가 이미 했는데 왜 또 해?", "설계자에게 어떻게 리뷰받나?". 블록 연습 페이지(N06C–F)는 전부 이 페이지의 **틀에 칸만 바꿔 끼우는** 구조.
- **외울 것 ① 카테고리 12종 = 4개씩 3묶음**: **"블록이 맞나"** 레지스터 · 기본 기능 · 설정 조합 · 경계 / **"틀어질 때"** 인터럽트 · 에러 · 리셋·전원 · 동시성·스트레스 / **"실제 세상"** 성능 · PVT·margin · soak · 시스템·interop. 주문: **레·기·조·경 / 인·에·리·스 / 성·피·소·시**.
- **외울 것 ② 10분 7단계 (시간 예산)**: ① Clarify 0:30 → ② 블록 그림 1:00 → ③ 카테고리 매트릭스 3:00 → ④ 우선순위·플랫폼 1:00 → ⑤ 통과 기준·커버리지 1:00 → ⑥ 자동화·회귀 1:00 → ⑦ 리스크·A0 첫 주 1:00 → 남는 1:30은 꼬리 질문. 단계마다 쓸 영어 문장은 §2.8, 한 장 요약은 §2.9.
- **외울 것 ③ Staff 시그널 3문장**: "I don't repeat what DV already proved in simulation — I spend silicon time on what only silicon can show." · "Exit criteria are agreed before we start." · "Every test is written once and runs on FPGA, silicon and SLT."
- 우선순위 공식(기억용): **bring-up 차단 > 고객 영향 > RTL 변경 > DV 사각지대 > errata 이력** — 코드에서는 `5B + 4I + 3C + 2G + 1E` 점수로 구현 (§2.5, §6).
- §3에 **I2C 컨트롤러 전체 플랜**(범위 → feature → 매트릭스 20행 → P0 → 셋업 → 종료 기준 → 10분 영어 답)을 실제 UM10204 타이밍 숫자로 한 번 끝까지 적용. 블록 상세 연습판은 [N06C C1](2026-11-01_N06C_plans_peripherals_soc_infra.html).
- **30초 영어 답 (headline)**: "I start from the spec and the register map and turn every feature into a requirement with an ID. For each feature I walk a fixed checklist of twelve categories — registers, basic function, configuration combinations, boundaries, interrupts, error injection, reset and power states, concurrency, performance, PVT margin, soak, and system interop with real devices and the real driver — and give every row a measurable pass criterion and a platform: FPGA, silicon, or both. Then I prioritize by what blocks bring-up, what the customer feels, what RTL changed, and what DV can't see, so the P0 set runs on day one of A0 with a binary already proven on the FPGA. I review the plan with the designer and DV lead so we don't duplicate simulation coverage, and the exit criteria are agreed before the first test runs."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 새 아파트 "입주 전 사전점검"

- 입주 전 사전점검표를 떠올리면 된다. 점검표는 **방(블록)** 이 바뀌어도 **항목 종류(카테고리)** 는 같다: 전기·수도·창호·도배·환기·누수… 거실이든 욕실이든 "전기 켜지나, 물 새나, 문 닫히나"를 같은 순서로 본다. → **카테고리 12종 체크리스트**.
- 그런데 같은 항목도 **조건**이 있다: 비 오는 날 누수(=PVT), 모든 수도꼭지를 동시에 틀 때 수압(=동시성·stress), 한 달 살아 본 뒤의 결로(=soak).
- **시공사가 이미 한 검사(=DV, 설계 검증)** 를 다시 하지 않는다. 시공사는 도면대로 지었는지(설계)를 봤고, 입주자는 **실제로 살 때** 문제가 있는지를 본다. 단, 시공사가 못 보는 것(실제 가구 넣었을 때, 겨울 결로)은 입주자만 볼 수 있다. → **"DV vs 실리콘에서만" 표**.
- 시간이 없으면 **가스·전기 안전부터**(=bring-up 차단 P0), 그다음 매일 쓰는 것(고객 영향), 이번에 새로 바꾼 자재(RTL 변경)… 순서. → **우선순위**.
- 그리고 "다 됐다"는 **"하자 0건 또는 보수 일정 합의"** 처럼 **미리 정한 기준**으로 판단 (= exit criteria, [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html)).

### 1.2 그림 — 스펙에서 sign-off까지 한 장

```text
 spec / TRM / register map ──► feature 목록 (F01..Fn) ──► requirement ID (REQ-I2C-007 ...)
                                         │
                       ┌─────────────────┴──────────────────┐
                       │  feature × 카테고리 12종 × 조건     │   ← 이 페이지의 핵심 (§2.4)
                       │  = 테스트 행 (ID · 조건 · 통과 기준)│
                       └─────────────────┬──────────────────┘
               우선순위 P0/P1/P2 (§2.5)   │   플랫폼 매핑 FPGA / Si / 둘 다 (§2.7)
                                         ▼
   pre-si: FPGA·에뮬에서 테스트 바이너리 검증 ──► A0: P0 bring-up ──► P1 기능 ──► P2 특성화(PVT·soak)
                                         │
                 결과 DB (chip ID · board · V/F/T · build hash · PASS/FAIL · log)
                                         ▼
          커버리지 · 버그 · errata ──► 종료 기준 판정 (N06B) ──► sign-off ──► SLT 콘텐츠 이관
```

### 1.3 용어 사전

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| DV | Design Verification | RTL(설계 코드)을 시뮬레이션·formal로 검사 | 이미 덮은 걸 실리콘에서 반복하지 않으려면 DV 범위를 알아야 함 |
| Validation | Post-silicon validation | 실물 칩·보드·SW가 실제 조건에서 의도대로 도는지 | 이 직무 그 자체 |
| Characterization | — | 얼마나 여유가 있나 (전압·온도·주파수 끝까지) | PVT·shmoo 행의 목적; 통과/실패가 아니라 **margin 숫자**가 산출물 |
| Qualification | Reliability qualification | 수명·스트레스 신뢰성 인증 (자동차: AEC-Q100) | 우리 플랜이 아니라 품질팀 플랜 — 범위 밖으로 명시 |
| Production test | ATE · SLT | 양산에서 "이 한 개가 불량인가" | validation 테스트 중 일부가 SLT 콘텐츠로 넘어감 |
| Test plan (vplan) | Validation plan | 무엇을·어떤 조건에서·어디서·어떤 기준으로 시험할지 적은 문서 | 면접 질문 "Design a test plan for X"의 답 형식 |
| Feature | — | 스펙에서 뽑은 기능 단위 (예: clock stretching) | 행의 단위; feature 누락 = 커버리지 구멍 |
| Requirement ID | — | 검증 가능한 한 문장 + 고유 ID + 출처 | traceability(추적성)의 키 |
| Traceability | — | spec 문단 ↔ REQ ↔ test ↔ 결과 연결 | "이 요구사항 증거 보여줘"에 3클릭 답 |
| Test matrix | — | feature × 카테고리 × 조건 표 | 빠진 칸이 눈에 보인다 |
| Pairwise | All-pairs testing | 모든 **두 축 값 쌍**이 한 번씩은 나오게 조합 수를 줄이는 법 | 조건 폭발 대응 (§2.5, §6) |
| P0/P1/P2 | Priority tiers | P0 bring-up 필수 · P1 기능 완성 · P2 특성화·완전성 | 시간 부족 시 무엇을 버릴지 |
| Exit criteria | — | "끝났다"의 사전 합의 기준 | Staff 시그널 1순위 ([N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html)) |
| Waiver | — | 테스트 생략·실패 허용을 **이유·승인자와 함께** 기록 | 빈칸과 "의도적으로 안 함"을 구분 |
| A0 / B0 | First silicon / base-layer stepping | 첫 실리콘 / 전 레이어 수정판 | 플랜이 단계별로 다름 (§2.2) |
| Errata | — | 공개·내부 실리콘 버그 목록 + workaround | 과거 errata = 우선순위 가중치 |
| HAL | Hardware Abstraction Layer | 플랫폼 차이를 감추는 얇은 C 층 | "write once, run on FPGA and silicon" |
| Golden model | Reference model | 기대값을 만드는 기준 (C 모델·이전 칩·FPGA) | 기능 행의 통과 기준 |
| Negative control | — | 일부러 고장을 넣어 테스트가 **실패하는지** 확인 | 안 깨지는 테스트는 아무것도 증명 못 함 |
| PVT | Process · Voltage · Temperature | 공정 corner(SS/TT/FF) × 전압 × 온도 | 실리콘에서만 보는 영역 |
| Soak | — | 수십 시간 연속 실행 | 희귀 이벤트·누수·드리프트 |
| Interop | Interoperability | 실제 상대 장치(센서·EEPROM·PHY)와 동작 | 스펙 해석 차이는 상대 장치와만 드러남 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 검증 용어 5개 — "테스트 플랜에서 무엇이 달라지나"

용어 정의 자체는 [BV01 §1.3](2026-11-01_BV01_si_board_validation_big_picture.html)·[LP14 §2.1](2026-10-31_LP14_validation_methodology.html)에 있다. 여기선 **플랜 문서의 어느 칸이 바뀌는지**만.

| 활동 | 플랜의 목표 문장 | 샘플 수 · 조건 | 통과 기준 형태 | 주 플랫폼 | 산출물 | 이 플랜에서 |
|---|---|---|---|---|---|---|
| Verification (DV) | "RTL = spec" | 시뮬레이션 seed 수천, 칩 0개 | functional/code coverage 목표 | 시뮬·formal·에뮬 | coverage report, RTL 버그 | **입력으로 받음** (DV coverage report → 중복 제거) |
| Validation | "실물이 실제 사용 조건에서 의도대로" | 칩 수 개~수십 × 보드 | PASS/FAIL + 숫자 (에러 0 in N) | FPGA → 실리콘 | 결과 DB, 버그, errata | **본 플랜** |
| Characterization | "어디까지 버티나" | corner 칩 (SS/TT/FF) × V × T × F | margin ≥ 목표 (숫자) | 실리콘 + 챔버 | shmoo, margin 표, Vmin/Fmax | **P2 행** (PVT·margin) — 상세 [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) |
| Qualification | "수년간 안 망가지나" | AEC-Q100 스트레스: 예) HTOL 3 lot × 77개 0 fail [3] | 스트레스 후 0 fail | 신뢰성 랩 | qual report | **범위 밖** (명시) — soak 결과만 공유 |
| Production test | "이 한 개가 불량인가" | 전 수량, 테스트 시간 예산 (초~분) | bin, yield | ATE · SLT | 테스트 프로그램 | **출구**: 어떤 validation 테스트를 SLT로 넘길지 표시 |

- 면접 한 줄: "Verification asks whether the design matches the spec; my plan asks whether the silicon in a real system does what the product needs, how much margin it has, and which of my tests are cheap and sharp enough to become production SLT content."

### 2.2 칩 일정 위의 검증 단계 — 단계마다 플랜에서 켜지는 칸

칩 일생 전체는 [BV01 §2](2026-11-01_BV01_si_board_validation_big_picture.html). 여기선 **플랜 문서가 단계별로 어떻게 자라나**.

```text
 스펙 리뷰 ─► pre-si 테스트 작성 ─► tape-out ─► A0 도착 ─► 기능 validation ─► 특성화(PVT) ─► B0/errata ─► SLT 이관
 (v0.1 플랜)   (FPGA/에뮬에서 P0 통과)   (대기: 팜·셋업)   (P0 1–2주)   (P1)           (P2)          (회귀 범위)    (P0 중 일부)
```

| 단계 | 플랜에서 하는 일 | 테스트 콘텐츠 | 다음 단계로 가는 문 (gate) |
|---|---|---|---|
| 스펙 리뷰 | feature·REQ 추출, **testability 요청**(에러 주입 레지스터, 카운터, debug 상태 노출), DV에 coverage 범위 질문 | 없음 — 플랜 v0.1 | 설계자·DV 리뷰 완료, 미정 항목 목록 |
| Pre-si (FPGA·에뮬) | P0/P1 테스트를 C로 작성, HAL로 플랫폼 차이 분리, FPGA에서 디버그 | REG·FUN·INT·ERR·RST·CFG 대부분 | **P0 테스트가 FPGA에서 PASS** = 실리콘 Day 1 바이너리 준비 |
| Tape-out → A0 | 실리콘 전용 행 작성 (PHY·PLL·온도 센서·퓨즈), 팜·장비 준비, 결과 DB 스키마 | PRF·PVT·SOAK 준비 | 셋업 dry-run |
| A0 bring-up | P0만, 한 번에 하나씩, 아래 계층부터 | P0 (§3.4) | P0 100% 실행, 차단 버그 목록 |
| 기능 validation | P1 전체 자동화, 회귀 nightly, 버그 → errata/workaround | P1 | P1 ≥ 합의 %, 열린 S1 0 |
| 특성화 | corner 칩 × V × T, soak | P2 (PVT·SOAK·성능 상세) | margin 표 ≥ 목표 |
| Stepping (A1/B0) | 수정 확인 + **변경 영향 범위만 회귀** (추적성 사용) | errata 재현 테스트 + 영향 REQ | 수정 확인, 새 실패 0 |
| SLT 이관 | 짧고 결함 검출력 높은 테스트 골라 넘김 | P0 일부 + stress 축약판 | SLT 시간 예산 · escape 목표 |

- 핵심: 플랜은 **한 번 쓰고 끝나는 문서가 아니라** 단계마다 칸이 채워지는 살아 있는 문서. 면접에선 "The plan starts at spec review, not at A0 — the most valuable thing I do there is ask for testability hooks."

### 2.3 공통 테스트 플랜 템플릿 (문서 목차) — 블록 하나용

보드 전체 플랜의 10절 목차는 [BV14 §2.1](2026-11-01_BV14_board_validation_plan_automation.html). 아래는 **IP 블록 하나**용 14절 — 그대로 복사해서 칸을 채우면 된다.

```text
VALIDATION PLAN — <block name> (<IP version>, SoC <name> <stepping>)        rev <x.y>  owner <name>
 1. Scope / out of scope        - modes in scope; what DV, board, SI, safety, qual teams own instead
 2. References                  - spec/TRM section, register map rev, protocol standard (e.g. UM10204 rev 7),
                                  integration spec (clocks, resets, IRQ, DMA req lines, pinmux), errata of previous chip
 3. Feature list + REQ IDs      - F01..Fn, each -> REQ-<blk>-nnn "verifiable sentence" (source, method: test/inspect/analysis)
 4. Test matrix                 - feature x 12 categories x conditions -> test ID, condition grid, pairwise set
 5. Conditions & samples        - V/T/F corners, #chips (TT/SS/FF), #boards, iterations, and WHY that number
 6. Platform mapping            - FPGA / emulator / silicon / SLT per test; what is stubbed on FPGA
 7. Priority                    - P0 bring-up / P1 functional / P2 characterization; scoring rule
 8. Pass criteria               - numbers: value + condition + repetitions + verdict (0 fail in N, p99 < X)
 9. Equipment & setup           - analyzer, scope, partner devices, loopbacks, fault injectors, chamber; ASCII setup
10. Dependencies                - FW/driver, boards, FPGA image, tools, other teams; date needed
11. Risks & mitigations         - each with owner and fallback
12. Schedule                    - pre-si / A0 week 1 / weeks 2-3 / PVT campaign / stepping
13. Exit criteria               - agreed before start (see N06B)
14. Reporting                   - results DB fields, dashboard, weekly 1-pager, waiver list, SLT handoff list
```

| 절 | 가장 흔한 실수 | 고치는 문장 |
|---|---|---|
| 1 범위 | 비범위를 안 씀 → 나중에 "왜 안 했어?" | "Out of scope, owned by X" 를 한 줄씩 |
| 3 feature | feature = 레지스터 목록으로 착각 | 레지스터는 REG 행 하나, feature는 **동작** 단위 |
| 4 매트릭스 | 정상 경로만 | 12종 체크리스트로 빈칸을 **눈으로** 확인 (§6 코드의 lint) |
| 5 샘플 | "1장 1000번" | 칩·보드 편차는 **다른 개체**로만 보인다 → [BV14 §2.4](2026-11-01_BV14_board_validation_plan_automation.html) |
| 8 통과 기준 | 형용사 ("안정적") | 값 + 조건 + 반복 + 판정 → [BV14 §2.5](2026-11-01_BV14_board_validation_plan_automation.html) |
| 10 의존성 | FW가 늦어 실행 불가 | bare-metal 테스트 FW 자체 보유 |
| 13 종료 | 시간 다 되면 끝 | 사전 합의, 미충족 항목 = 잔여 리스크로 보고 |

### 2.4 카테고리 12종 체크리스트 ★ (블록 무관)

**주문: 레·기·조·경 / 인·에·리·스 / 성·피·소·시** — English: "Registers, Function, Config, Boundaries · Interrupts, Errors, Reset, Stress · Performance, PVT, Soak, System."

| # | 코드 | 카테고리 | 스스로 묻는 질문 | 대표 테스트 (블록 무관) | 통과 기준 형태 | 잘 빼먹는 것 |
|---|---|---|---|---|---|---|
| ① | REG | 레지스터 | 맵이 스펙과 같나? | reset 값 전 필드 · RW walking 1/0 · RO 쓰기 무시 · W1C · reserved 0 유지 · 잘못된 주소 접근 응답 | 전 필드 일치, 불일치 0 | **W1C** · 다른 블록 주소 alias · 쓰기 후 읽기 순서(posted write) |
| ② | FUN | 기본 기능 | 정상 경로가 되나? | 대표 모드 1개로 전송·연산 + golden 비교 | 데이터 일치 N회 0 mismatch | 비교를 **HW에서 다시 읽어서** 하는지 |
| ③ | CFG | 설정 조합 | 모드·폭·속도 조합이 다 되나? | 모드 × 속도 × 폭 매트릭스 (pairwise) | 조합별 FUN 통과 | 모드 **전환** (A→B 바꿀 때) |
| ④ | BND | 경계·corner | 0 · 1 · max · max+1 · wrap · 정렬? | 길이 0/1/max, FIFO full/empty, 카운터 wrap, 4 KiB 경계 | hang 0, 상태 비트 정확 | **길이 0**과 **max+1** |
| ⑤ | INT | 인터럽트 | 모든 소스가 정확히 한 번? | 소스별 발생 · mask · clear · 동시 다발 · coalescing · GIC까지 라우팅 | 누락 0 · spurious 0 · latency 측정 | clear 직후 재발생 race, level vs edge |
| ⑥ | ERR | 에러·negative | 고장 나면 **알아채고 회복**하나? | 잘못된 설정, 오류 주입(CRC·NACK·timeout), 상대 장치 비정상 | 검출 + 보고 + 회복 + 다음 전송 PASS | **회복 후** 다음 전송 확인 |
| ⑦ | RST | 리셋·전원 상태 | 동작 중 끊겨도 깨끗이 돌아오나? | soft/block/SoC reset × idle/전송 중, clock gating, suspend/resume, 전원 도메인 off | reset 값 복귀 + 다음 전송 PASS, N천 회 0 fail | **전송 중** reset, retention 레지스터 |
| ⑧ | STR | 동시성·stress | 남들과 같이 돌 때도 되나? | 다른 master DMA·DDR 부하, 같은 IP 여러 인스턴스, IRQ storm, 버스 경합 | 에러 0 · hang 0 (T분) | 같은 IP **여러 인스턴스** 동시 |
| ⑨ | PRF | 성능 | 목표만큼 빠른가? | throughput · latency (p50/p99) · CPU 사용률 · bus 효율 | ≥ 목표 (% of 이론) | p99 (평균만 보고) |
| ⑩ | PVT | PVT·margin | 끝 조건에서도? 여유는? | V×T corner × SS/TT/FF, 타이밍 margin, shmoo | 전 corner PASS + margin ≥ 목표 | **저온** (고온만 봄) |
| ⑪ | SOAK | soak·reliability | 오래 돌려도? | 24–72 h 혼합 부하, 온도 사이클 | 0 fail in T → rate < 3/T (95%) | 카운터 **추세** (0이 아니라 증가율) |
| ⑫ | SYS | 시스템·interop | 실제 SW·실제 상대와? | 실제 드라이버(Linux), 실제 센서/PHY/메모리, 실제 use case | end-to-end 시나리오 PASS | 상대 장치 **여러 벤더** |

- 사용법: 블록을 받으면 12행을 위에서 아래로 소리 내어 훑는다. 해당 없으면 **"N/A because …"** 라고 한 마디 (= waiver). 침묵으로 건너뛰면 면접관은 "빼먹었다"로 채점한다 [추정].
- 묶음의 의미: 1묶음(①–④)은 **DV와 가장 많이 겹치는** 곳 → 실리콘에서는 얇게(smoke + 통합 문제), 3묶음(⑨–⑫)은 **실리콘에서만** → 두껍게. 2묶음(⑤–⑧)은 반반 — 특히 리셋·동시성은 DV가 "가능은 하지만 덜 하는" 영역.

### 2.5 우선순위 정하는 법

순서대로 묻는다 (앞의 기준이 이기면 뒤는 안 봐도 됨):

1. **Bring-up 차단?** — 이게 안 되면 다른 팀(드라이버·카메라·NPU)이 멈추나? (REG·기본 FUN·INT·RST)
2. **고객 영향** — 제품이 이 기능을 쓰나, 얼마나 자주, 안전 관련인가? (자동차: 센서 설정 I2C는 매 부팅 사용)
3. **RTL 변경** — 이전 칩 대비 새로 만들었거나 바뀐 부분 (RTL diff · 설계자 인터뷰). 재사용 성숙 IP는 smoke만.
4. **DV 사각지대** — 아날로그·타이밍·전원·장시간·실제 상대 장치 (§2.6)
5. **Errata 이력** — 이 IP·이 벤더의 과거 버그 유형 (I2C라면 bus hang, clock stretching 타이밍)

| 등급 | 정의 | 언제 | 예 (I2C) |
|---|---|---|---|
| P0 | 없으면 bring-up 불가 또는 다른 팀 차단 | A0 1주차, FPGA에서 사전 통과 필수 | reset 값, 400 kHz 읽기/쓰기, NACK 보고, 동작 중 reset |
| P1 | 기능 완성 — 제품이 쓰는 모든 경로 | A0 2–4주, nightly 회귀 | 모든 속도·모드, FIFO/DMA, 에러 전부, 실제 센서 interop |
| P2 | 특성화·완전성 | PVT 캠페인, 남는 시간 | V×T corner 타이밍, 72 h soak, 안 쓰는 모드 (10-bit 등) |

- 점수식(§6 코드와 동일, `[예시]` 가중치): **score = 5·B + 4·I + 3·C + 2·G + 1·E** (각 0–3, B는 bring-up 관련 카테고리에서만, G = max(feature, 카테고리 고유 DV gap)). P0 ≥ 22, P1 ≥ 15. 단 PVT·SOAK는 P0가 될 수 없음 (A0 bring-up을 막지 않으므로).
- `[예시]` 계산: I2C 7-bit 기본 읽기/쓰기(B3 I3 C0 G1 E0) = 15+12+0+2+0 = **29 → P0**. 10-bit 주소(B0 I1 C0 G0 E0) = **4 → P2**. 같은 블록 안에서도 행마다 등급이 다르다는 게 포인트.
- 3주 일정 표는 [BV14 §2.6](2026-11-01_BV14_board_validation_plan_automation.html). 점수식은 **"객관적 근거를 보여 주는 도구"** 이지 정답이 아니다 — 리뷰에서 PM·설계자와 가중치를 바꿀 수 있다고 말할 것.

### 2.6 "DV와 중복 피하기" — DV가 잘하는 것 vs 실리콘에서만 보이는 것

| 영역 | DV (시뮬·formal·에뮬)가 잘하는 것 | 실리콘에서만 보이는 것 | 플랜에서 하는 일 |
|---|---|---|---|
| 레지스터·프로토콜 논리 | 필드 RW, 상태기계 전이, 프로토콜 규칙 — **가시성 100%, 수천 seed** | 통합 실수: 주소 맵·클럭·리셋 배선·pinmux·IRQ 번호 | REG·FUN은 **smoke 수준** + 통합 항목만 |
| 희귀 조합·corner | constrained-random + coverage | 거의 없음 (단, 시뮬 안 된 SW 시퀀스) | DV coverage hole 목록을 받아 그 칸만 |
| Clock domain crossing | CDC 툴(정적), 일부 시뮬 | 실제 metastability·주파수 비율에 따른 간헐 실패 | 여러 클럭 비율·DVFS 중 stress |
| 아날로그·PHY·IO | 모델만 (이상화) | 실제 rise time·노이즈·drive strength·ESD | 타이밍 측정, PVT 행 |
| 타이밍 path | STA(정적 타이밍 분석)이 corner별로 | 실제 칩 편차 (SS/FF), 온도·전압 droop | corner 칩 × V × T shmoo |
| 전원·리셋 | 이상적 전원, 리셋 시퀀스 논리 | rail ramp·droop, 실제 POR 타이밍, 도메인 on/off 중 glitch | RST·전원 상태 수천 회 |
| 장시간·희귀 이벤트 | 시뮬은 ~ms 실시간 수준까지 [추정] | 10^12 사이클급 이벤트, 카운터 wrap, 누수·드리프트 | SOAK, 카운터 wrap 테스트 |
| 실제 SW 스택 | 거의 없음 (에뮬에서 일부) | 드라이버 타이밍·순서, OS 지연, 인터럽트 latency | SYS 행 (실제 드라이버) |
| 실제 상대 장치 | VIP(검증 IP) = 스펙 그대로인 상대 | 스펙 해석이 다른 실제 센서·PHY·EEPROM | interop 매트릭스 (여러 벤더) |
| 열·성능 실측 | 성능 모델 추정 | 실제 throughput·thermal throttling·전력 | PRF·PVT 행 |
| 제조 편차 | 없음 | 칩마다 다른 Vmin/Fmax, 약한 개체 | 여러 칩, SLT 이관 |

- 규칙 3개: ① **DV coverage report와 RTL diff를 받는다** — 플랜 리뷰의 입력 ② DV가 덮은 기능이라도 **"통합되어 실리콘에 올라온 것"** 은 smoke 1회(배선·클럭·리셋·pinmux 실수 확인) ③ 실리콘에서 발견된 버그가 시뮬에서 재현되면 → **DV coverage 구멍**으로 피드백 (재현 안 되면 실리콘 고유 효과 의심, [BV01 §2.4](2026-11-01_BV01_si_board_validation_big_picture.html)).
- 면접 문장: "I ask DV for their coverage report and the RTL diff before I write a single test. Anything they closed with high coverage gets a one-shot integration smoke on silicon; my depth goes into analog, timing, power, long-duration, real software and real partner devices — the things a simulator structurally can't see."

### 2.7 플랫폼 매핑 — 카테고리별 기본값 (추가 항목)

| 카테고리 | FPGA / 에뮬 | 실리콘 | 비고 |
|---|---|---|---|
| REG · FUN · CFG · BND · INT · ERR | ✅ 주 개발·디버그 | ✅ 같은 바이너리 재실행 | 실리콘 Day 1에는 **차이만** 디버그 |
| RST | 🟡 논리만 (전원 도메인·POR 없음) | ✅ 실제 전원·리셋 | FPGA에 전원 도메인이 없으면 stub |
| STR | 🟡 저속 (10–100× 느림, [BV01 §2.1](2026-11-01_BV01_si_board_validation_big_picture.html)) | ✅ 실제 부하 | timeout은 HAL에서 스케일 |
| PRF | 🟡 smoke만 (클럭 비율 다름 → 숫자 무의미) | ✅ | FPGA 숫자를 성능 결론에 쓰지 말 것 |
| PVT · SOAK | ❌ | ✅ | 실리콘 전용 |
| SYS | ✅ 드라이버 개발 | ✅ 실제 상대 장치 | |

상세 구현(HAL·timeout 스케일·stub)은 [N06G](2026-11-01_N06G_platforms_regression_automation.html).

### 2.8 10분 말하기 7단계 프레임 ★ (영어 문장 템플릿 + 시간 예산)

```text
 0:00 ─┬─ ① Clarify ─┬─ ② Block ─┬────── ③ Matrix (12 categories) ──────┬─ ④ Prio ─┬─ ⑤ Pass ─┬─ ⑥ Auto ─┬─ ⑦ Risk ─┬── Q&A ── 10:00
       0:30          1:30        │                                       4:30       5:30       6:30       7:30       8:30
                                 └ 화이트보드: 12행 표를 먼저 그려 놓고 채운다
```

| 단계 | 시간 | 할 일 | 영어 문장 템플릿 (빈칸 채우기) | 화이트보드 |
|---|---|---|---|---|
| ① Clarify | 0:30 | 범위 확정 질문 3개, 답 없으면 가정 선언 | "Before I start — is this our own IP or licensed? Which modes are in scope? Is there an FPGA image and a DV coverage report? If not, I'll assume ___." | 가정 3줄 |
| ② Block picture | 1:00 | 블록과 주변(버스·IRQ·DMA·클럭·리셋·핀/PHY·상대 장치) 그림 | "Here's how I picture it: the ___ sits on the ___ bus, raises interrupts to the GIC, talks to ___ over ___. Everything I test is one of these interfaces or the logic in between." | 박스 그림 |
| ③ Matrix | 3:00 | 12카테고리를 위에서 아래로, 블록 특화 테스트 1–2개씩 | "I go through the same twelve categories for every block. Registers: ___. Function: ___. Config combos: ___ — I'll use pairwise there. Boundaries: ___. Interrupts: ___. Errors: I'd inject ___ and check it recovers. Reset: reset in the middle of ___. Stress: alongside ___. Performance: ___. PVT: ___. Soak: ___. System: real driver plus ___." | 12행 표 (코드만) |
| ④ Priority · platform | 1:00 | P0 3–5개, 무엇을 FPGA/실리콘에서, 일부러 안 하는 것 | "P0 is what blocks bring-up: ___, ___, ___ — those are already passing on the FPGA before A0 arrives. PVT, soak and performance are silicon-only. I'm deliberately not re-testing ___, because DV closed it in simulation." | P0 동그라미 |
| ⑤ Pass criteria · coverage | 1:00 | 숫자 기준 2–3개 + 커버리지 정의 | "Every row has a number: for example ___ with zero errors in ___ transfers, or ___ within ___ at all corners. Coverage is requirements with a passing result over total requirements, tracked per corner." | 기준 예 2개 |
| ⑥ Automation · regression | 1:00 | 같은 테스트 FPGA·실리콘·SLT, nightly, 결과 DB | "Each test is plain C behind a thin HAL, so the same binary runs on FPGA and silicon. A smoke subset runs on every build, the full P0/P1 set nightly on the board farm, and every result is stored with chip ID, board, voltage, temperature and build hash." | 플랫폼 화살표 |
| ⑦ Risk · A0 first week | 1:00 | 리스크 2개 + 첫 주 계획 + 종료 기준 한 줄 | "The biggest risks are ___ and ___; my mitigation is ___. On A0 day one I'd run ___, then ___. And we agree exit criteria up front: all P0/P1 pass or waived, zero open severity-1 bugs, margins met at every corner." | 리스크 2줄 |
| 꼬리 질문 | 1:30 | 면접관 질문 | "Happy to go deeper on any row — I'd suggest ___ because that's where I'd expect silicon bugs." | — |

- **전환 문장** (단계 사이 연결, 시간 관리용): "That's the scope — let me draw it." · "Now the matrix; I'll go category by category." · "Given limited time, here's what I'd do first." · "How do we know it passed?" · "To scale this…" · "Finally, risks."
- **면접관이 끊으면**: 끊긴 지점이 그 사람이 관심 있는 곳이다 → 거기서 깊게 들어가고, 마지막에 "To close the loop quickly: priorities, pass criteria and exit — ___" 로 남은 단계를 20초에 압축.
- **③에서 시간 넘기지 않기**: 12행 중 블록 특화가 강한 3–4행만 깊게(예: I2C는 ERR·RST·PVT 타이밍), 나머지는 한 줄씩. 3분 = 행당 평균 15초.

### 2.9 한 장 요약 — 인쇄용 ★

| 7단계 (시간) | 핵심 영어 한 줄 | 12카테고리 (묶음) | 행마다 채울 칸 |
|---|---|---|---|
| ① Clarify (0:30) | "Our IP or licensed? Modes in scope? FPGA image, DV report?" | ① REG ② FUN ③ CFG ④ BND — 블록이 맞나 | 테스트 항목 |
| ② Block (1:00) | "It sits on ___, IRQ to GIC, DMA, pins/PHY to ___." | ⑤ INT ⑥ ERR ⑦ RST ⑧ STR — 틀어질 때 | 조건·파라미터 |
| ③ Matrix (3:00) | "Same twelve categories for every block." | ⑨ PRF ⑩ PVT ⑪ SOAK ⑫ SYS — 실제 세상 | 플랫폼 FPGA/Si/둘 다 |
| ④ Priority (1:00) | "P0 = blocks bring-up, pre-proven on FPGA. Not re-testing what DV closed." | 우선: 차단 > 고객 > RTL 변경 > DV gap > errata | 통과 기준 (숫자) |
| ⑤ Pass (1:00) | "Value + condition + repetitions + verdict." | 기준 예: 0 errors in N · p99 < X · margin ≥ Y | 우선순위 P0/P1/P2 |
| ⑥ Automate (1:00) | "Write once, run on FPGA, silicon, SLT; nightly; results DB." | 회귀: smoke / nightly / weekly / PVT 캠페인 | REQ ID |
| ⑦ Risk (1:00) | "Risks ___; A0 day one ___; exit agreed up front." | 종료: P0/P1 pass or waived · S1 0 · margins met | waiver 이유 |

## 3. 실전 적용 — 예시: I2C 컨트롤러 전체 플랜

> 블록 선택 이유: 모든 자동차 SoC에 여러 개 있고(카메라 센서 설정, PMIC, EEPROM, 온도 센서), 스펙(UM10204)이 공개라 숫자를 검증 가능 [1]. CSI-2 수신기 버전은 [N06E E3](2026-11-01_N06E_plans_highspeed_io_sensor.html), I2C 블록 연습 상세판(clarify·버그·꼬리 질문 더 많음)은 [N06C C1](2026-11-01_N06C_plans_peripherals_soc_infra.html). 여기선 **템플릿 14절을 한 번 끝까지 채우는 것**이 목적. Rivian 고유 사항은 전부 `[추정]`.

### 3.1 범위 · 가정 (템플릿 1–2절)

- **대상** `[추정]`: SoC 안 I2C 컨트롤러 IP (licensed, 예: DesignWare 계열처럼 controller+target 모드, TX/RX FIFO, DMA handshake, 인스턴스 여러 개). 1.8 V IO, 외부 풀업.
- **범위**: Standard-mode 100 kHz · Fast-mode 400 kHz · Fast-mode Plus 1 MHz, 7-bit 및 10-bit 주소, controller + target 모드, clock stretching, bus clear. **비범위**: High-speed 3.4 Mbit/s·Ultra Fast-mode 5 Mbit/s (제품 미사용 가정), SMBus PEC (드라이버 팀), 보드 풀업 값·신호 무결성 (보드 팀 — [BV10](2026-11-01_BV10_board_interface_tests.html)), 신뢰성 qual (품질팀).
- **참조**: UM10204 rev 7.0 (2021-10-01) — rev 7부터 용어가 master/slave → **controller/target** [1]; IP databook; SoC 통합 스펙 (클럭·리셋·IRQ 번호·DMA request line·pinmux); 이전 칩 errata.

### 3.2 블록 그림

```text
              APB (register access)                 IRQ ──► GIC (SPI #n)
  CPU ───────────────┐                              ▲
                     ▼                              │
   DMA ◄──req/ack──► ┌────────────────────────────────────┐    SCL ──┬── R_pullup ── VDD_IO (1.8 V)
   engine            │ I2C controller                     │◄─────────┤
                     │  regs · TX FIFO · RX FIFO          │    SDA ──┼── R_pullup
   clk (ic_clk) ───► │  controller FSM · target FSM       │◄─────────┤
   reset ─────────►  │  SCL gen (HCNT/LCNT) · glitch filt │          ├── camera sensor (target @0x__)
                     │  timeout · bus-clear logic         │          ├── PMIC (target)
                     └────────────────────────────────────┘          ├── EEPROM (target)
                                                                     └── [lab] protocol analyzer · fault injector
```

### 3.3 Feature 목록 · requirement ID (템플릿 3절)

| F | Feature | REQ 예 (검증 가능한 문장) | 출처 |
|---|---|---|---|
| F01 | 레지스터 맵 · reset 값 | REQ-I2C-001: 모든 레지스터 필드가 databook reset 값과 일치 | databook |
| F02 | controller 7-bit 쓰기/읽기 (repeated START 포함) | REQ-I2C-010: 100k/400k/1M에서 1–256 B 쓰기/읽기 데이터 일치 | UM10204 §3.1 [1] |
| F03 | 10-bit 주소 | REQ-I2C-015: 10-bit 주소 target과 쓰기/읽기 | UM10204 §3.1.11 [1] |
| F04 | clock stretching (target이 SCL low 유지) | REQ-I2C-020: target이 SCL을 늘려도 데이터 손실 없이 대기, timeout 설정 시 timeout 보고 | UM10204 §3.1.9 [1] |
| F05 | NACK · arbitration lost · SCL-low timeout | REQ-I2C-030: 각 오류가 상태·IRQ로 보고되고 다음 전송 성공 | UM10204 §3.1.6, §3.1.8 [1] |
| F06 | TX/RX FIFO · 임계값 | REQ-I2C-040: 임계값 0/half/depth-1에서 IRQ 정확, overflow 보고 | databook |
| F07 | DMA handshake | REQ-I2C-050: DMA로 1–4096 B 전송, 데이터 일치 | databook · SoC 통합 |
| F08 | 인터럽트 (mask/status/W1C) | REQ-I2C-060: 소스별 1회 발생, mask 존중, W1C 동작 | databook |
| F09 | bus clear (SDA stuck → 9 클럭) | REQ-I2C-070: SDA stuck low 시 최대 9 SCL 펄스 후 STOP, 버스 복구 | UM10204 §3.1.16 [1] |
| F10 | target 모드 | REQ-I2C-080: SoC가 target으로 응답 (외부 controller 상대) | databook |
| F11 | 저전력 (clock gating · suspend/resume) | REQ-I2C-090: resume 후 레지스터 복원, 첫 전송 성공 | SoC PM 스펙 |

### 3.4 테스트 매트릭스 (템플릿 4–8절, 20행)

타이밍 숫자는 UM10204 rev 7 Table 11 [1]: tLOW min 4.7 / 1.3 / 0.5 µs, tHIGH min 4.0 / 0.6 / 0.26 µs, tHD;STA min 4.0 / 0.6 / 0.26 µs, tSU;DAT min 250 / 100 / 50 ns, tBUF min 4.7 / 1.3 / 0.5 µs, tr max 1000 / 300 / 120 ns, Cb max 400 / 400 / 550 pF (Sm / Fm / Fm+). 글리치 필터 tSP 50 ns (Fm·Fm+) [1].

| ID | 카테고리 | 테스트 항목 | 조건 · 파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|---|
| T01 | REG | reset 값 · RW walking 1/0 · RO · W1C · reserved | 전 레지스터, 인스턴스 전부 | 둘 다 | 불일치 0 필드 | P0 |
| T02 | FUN | EEPROM 쓰기 → 읽기 비교 | 400 kHz, 7-bit, 16 B page | 둘 다 | 1,000회 0 mismatch | P0 |
| T03 | CFG | 속도 × 방향 × 길이 | {100k, 400k, 1M} × {wr, rd, wr-Sr-rd} × {1, 16, 256} B, pairwise 10 케이스 (§6) | 둘 다 | 케이스별 데이터 일치 | P1 |
| T04 | CFG · PVT | SCL 타이밍 측정 (analyzer/scope) | 100k/400k/1M, 실제 버스 부하 | Si | tLOW/tHIGH/tHD;STA/tSU;DAT/tBUF ≥ Table 11 최소 [1], fSCL ≤ 모드 최대 | P1 |
| T05 | BND | 길이 0 (주소만) · 1 B · FIFO depth · depth+1 · 256 B | 400 kHz | 둘 다 | hang 0, 바이트 수 정확, STOP 생성 | P1 |
| T06 | FUN · BND | repeated START (write-Sr-read, 레지스터 포인터 읽기) | 센서 레지스터 읽기 패턴 | 둘 다 | 데이터 일치, STOP 없이 Sr 확인 (analyzer) | P0 |
| T07 | FUN · ERR | clock stretching | target stretch 1 µs / 1 ms / timeout 초과 | 둘 다 | ≤ timeout: 데이터 일치 · 초과: timeout 상태+IRQ, 버스 복구 | P0 |
| T08 | INT | 소스별 IRQ (tx_empty, rx_full, stop_det, nack, arb_lost) | mask on/off × 소스 5 | 둘 다 | 소스당 정확히 1회, mask 시 0회, W1C 후 status 0, GIC까지 도착 | P0 |
| T09 | ERR | 주소 NACK · 데이터 NACK | 없는 주소, target이 n번째 바이트 NACK | 둘 다 | NACK 상태+IRQ, 컨트롤러가 STOP 생성, 다음 전송 PASS | P0 |
| T10 | ERR | arbitration lost | 외부 controller (fault injector "lose_arbitration") [2] | 둘 다 | arb_lost 보고, 버스 해제, 재시도 PASS | P1 |
| T11 | ERR · RST | SDA stuck low → bus clear | fault injector "incomplete_address_phase"/"incomplete_write_byte" [2], stuck 시점: 주소 후 / 바이트 중간 | 둘 다 | ≤ 9 SCL 펄스 내 SDA 해제 + STOP [1], 100회 0 fail; 해제 안 되면 "stuck" 보고 (hang 금지) | P0 |
| T12 | ERR | SCL stuck low (target 고장) | SCL을 강제로 low | 둘 다 | timeout 보고 (SMBus 기준 사용 시 tTIMEOUT 25–35 ms [5]), SW에 실패 반환 (hang 0) | P1 |
| T13 | RST | 동작 중 reset | soft/block/SoC warm reset × idle/전송 중 | 둘 다 | reset 값 복귀, 버스 idle (SCL·SDA high), 다음 전송 PASS, 1,000 사이클 0 fail | P0 |
| T14 | RST | clock gating · suspend/resume | 전송 없음 / 전송 중 요청 | Si (FPGA 논리만) | resume 후 설정 유지 또는 복원, 첫 전송 PASS, 1,000 사이클 0 fail | P1 |
| T15 | STR | 동시성 | 전 I2C 인스턴스 동시 + DDR DMA 부하 + IRQ storm | 둘 다 | 30분 데이터 에러 0, hang 0 | P1 |
| T16 | PRF | throughput · CPU 부하 (PIO vs DMA) | 1 MHz, 256 B 연속 | Si (FPGA smoke) | 유효 throughput ≥ 이론의 X% [추정 목표], DMA 모드 CPU 사용률 < Y% | P2 |
| T17 | PRF | IRQ latency (stop_det → ISR 진입) | GPIO 토글 + scope | Si | p99 < Z µs [추정 목표] | P2 |
| T18 | PVT | 타이밍·기능 at corner | V {min, nom, max} × T {−40, 25, 125 °C} × SS/TT/FF | Si | 전 corner T02·T04 PASS, 가장 빡빡한 파라미터 margin ≥ 목표 | P2 |
| T19 | SOAK | 장시간 센서 폴링 + 주기적 bus clear 주입 | 72 h, 카메라 센서 + PMIC | Si | 0 fail in 72 h × N보드 → rate 상한 3/(72·N) per hour (95%) | P2 |
| T20 | SYS | 실제 드라이버 · 실제 장치 interop | Linux i2c-dev/i2c-tools + 센서·PMIC·EEPROM 각 2벤더 | 둘 다 | 장치별 probe 성공, 설정 R/W 일치, 부팅 1,000회 0 실패 | P1 |

- 행 수가 12종 전부를 덮는지 확인: REG T01 · FUN T02/T06 · CFG T03/T04 · BND T05 · INT T08 · ERR T07/T09–T12 · RST T13/T14 · STR T15 · PRF T16/T17 · PVT T18 · SOAK T19 · SYS T20 ✅.

### 3.5 우선순위 · A0 첫 주 · 일부러 안 하는 것 (템플릿 7·12절)

- **A0 1주차 P0 순서**: T01 레지스터 → T02 EEPROM 쓰기/읽기 (가장 싼 "살아 있다" 증명) → T06 repeated START (센서 레지스터 읽기가 이 패턴이라 카메라 팀 차단) → T08·T09 IRQ·NACK → T13 동작 중 reset → T07·T11 stretching·bus clear. 전부 FPGA에서 이미 PASS한 바이너리.
- P1이 더하는 것: 속도 조합 전체, 실측 타이밍, FIFO/DMA, arbitration, 실제 장치 interop, 동시성. P2: corner 특성화, soak, 성능 숫자, 10-bit·target 모드 상세.
- **일부러 안 하는 것**: 컨트롤러 FSM의 모든 상태 전이·random 시퀀스 (DV가 VIP로 coverage 닫음 — coverage report 확인 후), High-speed 모드 (비범위), 풀업 값 최적화 (보드 팀). 이유와 승인자를 waiver 표에 기록.

### 3.6 장비 · 셋업 (템플릿 9절)

```text
  [Host PC: orchestrator + results DB] ──UART/JTAG──► [SoC EVB] ──SCL/SDA (1.8 V)──┬── EEPROM (2 vendors)
          │                                               │                        ├── camera sensor / PMIC
          ├── SCPI ─► programmable PSU (V corners)        └── GPIO (trigger)      ├── fault injector (2nd controller
          ├── SCPI ─► scope (SCL/SDA, tr, timing)                                 │   or Linux i2c-gpio injector [2])
          └── USB ──► I2C protocol analyzer (decode, timing, NACK/stretch log)    └── 외부 controller (arb/target 테스트)
  thermal chamber / thermostream (T18)
```

- 관찰 지점: 컨트롤러 status·raw interrupt·abort source 레지스터, TX/RX FIFO level, 드라이버 에러 코드, analyzer 로그 (ACK/NACK, stretch 길이), scope 측정 (tr, tLOW). Linux에서는 `i2cdetect -y <bus>`, `i2cget`/`i2cset`/`i2ctransfer` (i2c-tools) [4].

### 3.7 종료 기준 · 리스크 · 리포팅 (템플릿 11·13·14절)

- **종료 기준** (시작 전 합의, [N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html)): P0·P1 행 100% 실행·PASS 또는 승인된 waiver · S1/S2 버그 0 · 타이밍 측정이 전 corner에서 Table 11 충족 · 72 h soak 0 fail · 마지막 5 nightly 새 실패 0.
- **리스크**: ① 실제 센서 샘플·드라이버 지연 → EEPROM·fault injector로 먼저, interop은 나중 ② bus hang이 보드 결함(풀업·short)과 섞임 → 보드 swap·scope로 분리 ([BV13](2026-11-01_BV13_board_debug_scenarios.html)) ③ FPGA에는 실제 IO 타이밍이 없음 → T04/T18은 실리콘 전용으로 계획.
- **리포팅**: 행마다 chip ID · board SN · V/T · build hash · analyzer 캡처 링크 → 결과 DB → 주간 1장 ([BV14 체인 E](2026-11-01_BV14_board_validation_plan_automation.html)).

### 3.8 10분 영어 답 (7단계 적용)

1. **Clarify** — "Is this a licensed IP like a DesignWare-class controller or ours? Which speeds — I'll assume Standard, Fast and Fast-mode Plus, controller and target, no High-speed. Is there an FPGA image and a DV coverage report?"
2. **Block** — "It's an APB peripheral with TX/RX FIFOs, a DMA handshake and one interrupt line to the GIC. Off-chip it's two open-drain lines with pull-ups to camera sensors, the PMIC and an EEPROM."
3. **Matrix** — "Registers: reset values, RW, W1C. Function: EEPROM write-read compare. Config: speed by direction by length, pairwise. Boundaries: zero-length, FIFO depth plus one. Interrupts: every source once, masked and unmasked. Errors — the interesting part for I2C: address and data NACK, arbitration lost, clock stretching past the timeout, and SDA stuck low to exercise the nine-clock bus clear. Reset in the middle of a byte. Stress: all instances plus DMA traffic. Performance: throughput and IRQ latency. PVT: measure tLOW, tHIGH and setup/hold against the spec table at voltage and temperature corners. Soak: 72 hours of sensor polling. System: the Linux driver against two vendors of each real device."
4. **Priority** — "P0 is register sanity, a basic write-read, repeated-start reads because the camera team needs them, NACK reporting, reset mid-transfer, and bus recovery. I won't re-run DV's random protocol sequences on silicon."
5. **Pass** — "Zero mismatches in a thousand transfers; bus recovered within nine clocks a hundred times out of a hundred; every timing parameter above the UM10204 minimum at every corner."
6. **Automation** — "Same C tests on FPGA and silicon behind a HAL, fault injection through a second controller, analyzer and scope captures attached to each result, nightly on the farm."
7. **Risk / A0** — "The risk is bus hangs that are really board issues, so I swap boards and scope the lines before blaming silicon. Day one is registers, a write-read, and the bus-clear test. Exit: all P0/P1 pass or waived, timing met at all corners, 72-hour soak clean."

## 4. 꼬리 질문 체인

### 체인 A — "How do you write a validation plan for a new IP block?" (인덱스 원본)

<details>
<summary>Q. How do you write a validation plan for a new IP block?</summary>

"I start from the spec and register map and turn features into requirements with IDs. Then for each feature I walk the same twelve categories — registers through system interop — so gaps are visible, and each row gets a condition grid, a platform and a numeric pass criterion. I prioritize, attach exit criteria, and review it with the designer and DV before writing tests."
- 한국어 핵심: spec → feature → REQ → 12카테고리 행 → 조건·플랫폼·숫자 기준 → 우선순위 → 리뷰.

</details>

<details>
<summary>꼬리1. You have 3 weeks before A0 and the plan has 300 rows. What do you do?</summary>

"I rank rows by whether they block bring-up, customer impact, RTL change, and what DV can't see, and I make sure the top tier — maybe 30 to 40 rows — is passing on the FPGA before silicon arrives. I'd rather have the P0 set automated and deep than every row half-written. The rest is scheduled after A0 with an explicit list of what's deferred, signed off by the lead."
- 한국어 핵심: 점수로 정렬 → P0를 FPGA에서 완성·자동화 → 나머지는 연기 목록에 서명.

</details>

<details>
<summary>꼬리2. What do you deliberately NOT test?</summary>

"Anything DV closed with good coverage that doesn't depend on silicon physics — random protocol sequences, every FSM transition — gets one integration smoke, not a campaign. Modes the product doesn't use go to P2 or out of scope. Things other teams own — board signal integrity, reliability qualification — are listed as out of scope with the owner. The key is that it's written down with a reason, so a skipped test is a decision, not a gap."
- 한국어 핵심: DV가 닫은 것은 smoke만, 미사용 모드는 P2, 타 팀 소관은 비범위 — 전부 이유와 함께 기록.

</details>

<details>
<summary>꼬리3. How do you get the designer to review it?</summary>

"I make the review cheap: one table, feature by category, with the questions I need answered highlighted — which modes are risky, what changed in the RTL, where DV coverage is weak. Designers respond to specifics, so I ask 'what would you be nervous about on silicon?' rather than 'please review'. I also ask for testability hooks early — error-injection bits, status counters — while the RTL can still change, and I close the loop by sending them the first silicon results."
- 한국어 핵심: 리뷰를 싸게(한 장 표 + 구체 질문), "실리콘에서 뭐가 불안하세요?", 테스트 훅은 RTL freeze 전에 요청.

</details>

### 체인 B — "DV already has 100% coverage. Why do we need your plan?"

<details>
<summary>Q. DV reports 100% functional coverage. Why test it again on silicon?</summary>

"Coverage measures what the testbench exercised against the model it was given; it can't see analog behavior, real timing at a process corner, power delivery, or a partner device that reads the spec differently. Integration mistakes — a wrong IRQ number, a reset not wired, pinmux defaults — also only show up on the real chip. So I don't repeat DV; I test the things a simulator structurally can't."
- 한국어 핵심: 커버리지 = "모델 대비 자극했다" — 아날로그·타이밍·전원·실제 상대·통합 실수는 못 봄.

</details>

<details>
<summary>꼬리1. Which DV tests would you still re-run on silicon?</summary>

"A thin smoke of each feature, because it proves the integration — clocks, resets, address map, interrupts — and it's nearly free if the test is the same C code we ran on the FPGA. I'd also re-run anything DV flagged as hard to cover or closed only with formal, and anything tied to a previous erratum."
- 한국어 핵심: 기능별 smoke(통합 증명) + DV가 어렵다고 한 칸 + 과거 errata 관련.

</details>

<details>
<summary>꼬리2. You found a silicon bug. What do you tell DV?</summary>

"First I try to reproduce it in simulation or on the FPGA with the same register sequence. If it reproduces, it's an RTL bug and a coverage hole, so DV adds a cover point and a test so the next chip can't escape the same way. If it doesn't reproduce, I look at silicon-only effects — timing at that corner, voltage, clock ratio — and that becomes a characterization item."
- 한국어 핵심: 시뮬 재현 → 되면 RTL 버그 + DV coverage 구멍 보강, 안 되면 실리콘 고유 효과.

</details>

### 체인 C — "The condition space explodes. How do you cut it?"

<details>
<summary>Q. Speeds, modes, lengths, voltages, temperatures — the combinations explode. What do you do?</summary>

"I split axes into functional and physical. Functional axes like mode, speed and length I cover with pairwise combinations — every pair of values appears at least once — which on our I2C example cut 27 cases to 10. Physical axes — voltage, temperature, process corner — I only cross with the tests sensitive to margin, like timing and data integrity, and I run the worst-case corners first: hot plus low voltage plus slow silicon, cold plus high voltage plus fast."
- 한국어 핵심: 기능 축은 pairwise, 물리 축은 margin 민감 테스트에만, 최악 corner 먼저.

</details>

<details>
<summary>꼬리1. When is pairwise not enough?</summary>

"When a bug needs three or more conditions at once — NIST's empirical work shows most failures involve one or two parameters but some need up to six. So I add explicit rows for combinations I have a reason to fear: ones in the errata history, the RTL change, or the designer's worry list, and I raise to three-way coverage for the riskiest feature."
- 한국어 핵심: 대부분 1–2개 상호작용이지만 일부는 더 많음 [6] → 위험 조합은 명시 행 + 3-way.

</details>

### 체인 D — "The spec isn't final. How do you write the plan?"

<details>
<summary>Q. The spec is still changing. How can you plan?</summary>

"I write the plan against requirement IDs, not paragraphs, and keep an open-questions list in the plan itself. Stable parts — the register interface, the protocol standard — get tests written now; volatile parts get placeholders with the owner and the date they freeze. When something changes, traceability tells me exactly which tests to update instead of re-reading the whole plan."
- 한국어 핵심: REQ ID 단위 + 미정 목록 + 추적성 → 변경 영향만 수정.

</details>

<details>
<summary>꼬리1. An ECO lands after A0. What do you re-run?</summary>

"I trace from the changed logic to the affected requirements and re-run those tests plus the smoke suite, across the corners where the original bug showed up. If the ECO touched clocks, resets or power, I widen to every block in that domain, because those changes don't stay local."
- 한국어 핵심: 변경 → 영향 REQ → 해당 테스트 + smoke, 클럭·리셋·전원 변경이면 도메인 전체.

</details>

## 5. Don 경험 연결

- "At Solidigm I validated SoC IP — I2C, SPI, DMA, SRAM and DRAM controllers — on the FPGA prototype before RTL freeze, and that's where I learned to plan by feature and category rather than by register: across [숫자 채우기] blocks we wrote [숫자 채우기] tests and filed [숫자 채우기] RTL bugs before freeze."
- "Because those same tests later ran with the silicon validation team during performance tuning, I already think of the plan as write-once, run on FPGA and on silicon."
- "At SK hynix the chamber reliability platform was essentially the PVT and soak rows of a plan — [숫자 채우기] chambers, temperature corners, and shmoo on NAND and PCIe interfaces — so I know those rows cost the most time and must be scheduled first, not last."
- "At Apple I root-cause I2C, SPMI and RFFE issues with a protocol analyzer and scope, which is exactly how I'd measure the timing and error rows of an I2C plan on silicon."

## 6. 실습 / 코드

### 6.1 파일과 실행

- [test_matrix_gen.py](onsitePrep/n06/a/test_matrix_gen.py) — Python stdlib. feature 목록(JSON) × 12 카테고리 × 조건 축 → 테스트 ID 표(markdown). 하는 일: ① feature × 카테고리 격자 (빈칸·waiver 보이기) ② 테스트 행: ID · REQ · 조건 · **full vs pairwise 케이스 수**(greedy all-pairs) · 플랫폼 · 점수 · P0/P1/P2 ③ 카테고리별 통과 기준 템플릿 ④ 요약 + **plan lint** (12종 전부 존재? feature마다 ERR 행 또는 waiver? P0인데 실리콘 전용?). lint 실패 시 exit 1 → 플랜 리뷰 CI 게이트로 쓸 수 있음.
- [i2c_features.json](onsitePrep/n06/a/i2c_features.json) — §3의 I2C feature 11개 `[예시]`. F07(DMA)·F11(저전력)에는 **일부러 ERR 행을 빼 둠** → lint가 잡는지 확인용.
- [run.sh](onsitePrep/n06/a/run.sh) — run 1: 원본 (lint FAIL, exit 1) · run 2: DMA abort/잘못된 주소, resume 중 전송 ERR 행 추가 (lint PASS, exit 0). 전체 출력은 [run_output.txt](onsitePrep/n06/a/run_output.txt).

```bash
cd onsitePrep/n06/a && ./run.sh          # = python3 -I test_matrix_gen.py i2c_features.json ...
```

### 6.2 실제 출력 (macOS, python3 -I, 2026-11-01) [확인됨]

아래 격자·표는 스크립트가 출력한 markdown을 **그대로** 붙인 것 (run 1).

#### 1. I2C feature x category grid (X = test row, w = waived, . = none)

| feature | REG | FUN | CFG | BND | INT | ERR | RST | STR | PRF | PVT | SOAK | SYS |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| F01 Register map + reset values | X | . | . | . | . | w | X | . | . | . | . | . |
| F02 Controller write/read (7-bit) | . | X | X | X | . | w | . | . | X | X | . | X |
| F03 10-bit addressing | . | X | . | X | . | w | . | . | . | . | . | . |
| F04 Clock stretching by target | . | X | . | X | . | X | . | . | . | X | . | . |
| F05 NACK / arbitration-lost / timeout | . | . | . | . | X | X | . | X | . | . | . | . |
| F06 TX/RX FIFO + thresholds | . | X | . | X | X | X | . | . | . | . | . | . |
| F07 DMA handshake | . | X | . | X | . | . | X | X | X | . | . | . |
| F08 Interrupts (mask/status/W1C) | X | . | . | . | X | w | . | X | . | . | . | . |
| F09 Bus clear / recovery (9 clocks) | . | . | . | . | . | X | X | . | . | . | X | . |
| F10 Target (slave) mode | . | X | X | . | . | X | . | . | . | . | . | . |
| F11 Low-power: clock gating + suspend/resume | . | . | . | . | . | . | X | . | . | . | X | . |

#### 2. test table (37 rows, sorted P0 first)

| test ID | req | conditions | cases full/pairwise | platform | score | pri |
|---|---|---|---|---|---|---|
| I2C-F02-FUN-01 | SPEC-3.1 | speed{100k,400k,1M} x dir{wr,rd,wr-rd(Sr)} x len{1,16,256} | 27/10 | both | 29 | P0 |
| I2C-F04-FUN-01 | SPEC-3.1.9 | speed{100k,400k,1M} x stretch{1us,1ms,> timeout} | 9/9 | both | 28 | P0 |
| I2C-F01-RST-01 | SPEC-REG | trigger{soft reset,block reset,SoC warm reset} x when{idle,mid-transfer} | 6/6 | both | 26 | P0 |
| I2C-F09-RST-01 | SPEC-3.1.16 | trigger{soft reset,block reset,SoC warm reset} x when{idle,mid-transfer} | 6/6 | both | 26 | P0 |
| I2C-F05-INT-01 | SPEC-3.1.6,3.1.8 | fault{addr NACK,data NACK,arb lost,SCL low timeout} x speed{100k,1M} | 8/8 | both | 24 | P0 |
| I2C-F04-BND-01 | SPEC-3.1.9 | speed{100k,400k,1M} x stretch{1us,1ms,> timeout} | 9/9 | both | 23 | P0 |
| I2C-F04-ERR-01 | SPEC-3.1.9 | speed{100k,400k,1M} x stretch{1us,1ms,> timeout} | 9/9 | both | 23 | P0 |
| I2C-F01-REG-01 | SPEC-REG | - | 1/1 | both | 22 | P0 |
| I2C-F04-PVT-01 | SPEC-3.1.9 | V{Vmin,Vnom,Vmax} x T{-40C,25C,125C} | 9/9 | Si | 25 | P1 |
| I2C-F09-SOAK-01 | SPEC-3.1.16 | hours{24h,72h} | 2/2 | Si | 23 | P1 |
| I2C-F05-STR-01 | SPEC-3.1.6,3.1.8 | load{DMA+DDR traffic,all I2C ports,IRQ storm} | 3/3 | both | 21 | P1 |
| I2C-F06-INT-01 | IP-FIFO | thresh{0,half,depth-1} x fill{empty,full,overflow} | 9/9 | both | 21 | P1 |
| I2C-F07-PRF-01 | IP-DMA | metric{throughput,latency} | 2/2 | Si (FPGA smoke) | 21 | P1 |
| I2C-F07-RST-01 | IP-DMA | trigger{soft reset,block reset,SoC warm reset} x when{idle,mid-transfer} | 6/6 | both | 21 | P1 |
| I2C-F07-STR-01 | IP-DMA | load{DMA+DDR traffic,all I2C ports,IRQ storm} | 3/3 | both | 21 | P1 |
| I2C-F08-INT-01 | IP-INT | src{tx_empty,rx_full,stop_det,nack,arb_lost} x mode{masked,unmasked} | 10/10 | both | 21 | P1 |
| I2C-F08-REG-01 | IP-INT | - | 1/1 | both | 21 | P1 |
| I2C-F09-ERR-01 | SPEC-3.1.16 | stuck{SDA low,SCL low} x when{after addr,mid-byte} | 4/4 | both | 21 | P1 |
| I2C-F11-RST-01 | SOC-PM | trigger{soft reset,block reset,SoC warm reset} x when{idle,mid-transfer} | 6/6 | both | 20 | P1 |
| I2C-F11-SOAK-01 | SOC-PM | hours{24h,72h} | 2/2 | Si | 20 | P1 |
| I2C-F05-ERR-01 | SPEC-3.1.6,3.1.8 | fault{addr NACK,data NACK,arb lost,SCL low timeout} x speed{100k,1M} | 8/8 | both | 19 | P1 |
| I2C-F06-FUN-01 | IP-FIFO | thresh{0,half,depth-1} x fill{empty,full,overflow} | 9/9 | both | 19 | P1 |
| I2C-F07-BND-01 | IP-DMA | len{1,255,4096} x burst{1,4} x dir{tx,rx} | 12/6 | both | 19 | P1 |
| I2C-F07-FUN-01 | IP-DMA | len{1,255,4096} x burst{1,4} x dir{tx,rx} | 12/6 | both | 19 | P1 |
| I2C-F02-PVT-01 | SPEC-3.1 | V{Vmin,Vnom,Vmax} x T{-40C,25C,125C} | 9/9 | Si | 18 | P1 |
| I2C-F02-SYS-01 | SPEC-3.1 | stack{Linux i2c-dev,bare-metal} x partner{EEPROM,sensor,PMIC} | 6/6 | both | 18 | P1 |
| I2C-F02-PRF-01 | SPEC-3.1 | metric{throughput,latency} | 2/2 | Si (FPGA smoke) | 16 | P1 |
| I2C-F06-ERR-01 | IP-FIFO | thresh{0,half,depth-1} x fill{empty,full,overflow} | 9/9 | both | 16 | P1 |
| I2C-F02-BND-01 | SPEC-3.1 | speed{100k,400k,1M} x dir{wr,rd,wr-rd(Sr)} x len{1,16,256} | 27/10 | both | 14 | P2 |
| I2C-F02-CFG-01 | SPEC-3.1 | speed{100k,400k,1M} x dir{wr,rd,wr-rd(Sr)} x len{1,16,256} | 27/10 | both | 14 | P2 |
| I2C-F06-BND-01 | IP-FIFO | thresh{0,half,depth-1} x fill{empty,full,overflow} | 9/9 | both | 14 | P2 |
| I2C-F08-STR-01 | IP-INT | load{DMA+DDR traffic,all I2C ports,IRQ storm} | 3/3 | both | 13 | P2 |
| I2C-F10-CFG-01 | SPEC-3.1 | addr{7b,10b} x speed{100k,400k,1M} x dir{wr,rd} | 12/6 | both | 6 | P2 |
| I2C-F10-ERR-01 | SPEC-3.1 | addr{7b,10b} x speed{100k,400k,1M} x dir{wr,rd} | 12/6 | both | 6 | P2 |
| I2C-F03-BND-01 | SPEC-3.1.11 | speed{100k,400k} x dir{wr,rd} | 4/4 | both | 4 | P2 |
| I2C-F03-FUN-01 | SPEC-3.1.11 | speed{100k,400k} x dir{wr,rd} | 4/4 | both | 4 | P2 |
| I2C-F10-FUN-01 | SPEC-3.1 | addr{7b,10b} x speed{100k,400k,1M} x dir{wr,rd} | 12/6 | both | 4 | P2 |

(§3 카테고리별 통과 기준 템플릿 표는 [run_output.txt](onsitePrep/n06/a/run_output.txt) 참고 — §2.4 표의 "통과 기준 형태" 열과 같은 내용.)

#### 4. summary — run 1 (원본) → run 2 (ERR 행 추가)

```text
===== run 1: plan as written (expect lint FAIL, exit 1) =====
rows=37  executable cases: full=309  pairwise=228  (74% of full)
P0:  8 rows  both=8
P1: 20 rows  Si=4  Si (FPGA smoke)=2  both=14
P2:  9 rows  both=9
score = 5*blocker + 4*impact + 3*changed + 2*dv_gap + 1*errata ; P0>=22 P1>=15
waivers=4 (each must have a reason + owner in the plan review)
LINT: F07 DMA handshake: no ERR/negative row and no waiver
LINT: F11 Low-power: clock gating + suspend/resume: no ERR/negative row and no waiver
RESULT plan_lint FAIL findings=2
exit=1

===== run 2: add the missing ERR rows (DMA abort/bad address; resume with transfer in flight) =====
rows=39  executable cases: full=363  pairwise=240  (66% of full)
P0:  8 rows  both=8
P1: 22 rows  Si=4  Si (FPGA smoke)=2  both=16
P2:  9 rows  both=9
score = 5*blocker + 4*impact + 3*changed + 2*dv_gap + 1*errata ; P0>=22 P1>=15
waivers=4 (each must have a reason + owner in the plan review)
RESULT plan_lint PASS findings=0
exit=0
```

### 6.3 출력에서 읽을 것 (면접 거리)

- **P0 8행이 전부 "both"** = 실리콘 Day 1 바이너리가 FPGA에서 이미 검증됨 (lint 규칙 3). PVT·SOAK는 점수가 높아도(F04-PVT 25점) P1로 캡 — "특성화는 bring-up을 막지 않는다".
- **pairwise 효과**: 3축 조합(F02 27 → 10, F07 12 → 6, F10 12 → 6)에서만 줄어든다. 2축 이하는 pairwise = 전체 조합이라 그대로 — 이 예에서 전체 309 → 228 (74%). 축이 많은 블록(Ethernet·CSI-2)일수록 효과가 커진다.
- **lint가 잡은 2건이 실제로 흔한 누락**: DMA 경로의 에러(잘못된 주소·전송 중 abort)와 저전력 상태의 에러(전송 중 suspend 요청). 정상 경로 위주로 feature를 적으면 정확히 이 두 칸이 빈다.
- 한계 `[미구현]`: 가중치는 `[예시]`; 3-way coverage, 기존 결과 DB와 연결해 "실행/통과 커버리지" 계산은 N06B 코드 쪽.

## 7. 자주 틀리는 것

1. **정상 경로만 말하기** — 면접에서 가장 흔한 감점. ERR·RST·PVT 세 칸은 블록이 뭐든 반드시 말한다 (§6 lint가 잡은 것도 이 칸).
2. **카테고리를 건너뛸 때 침묵** — "N/A because …" 한 마디가 있어야 "고려했다"로 들린다.
3. **레지스터 목록 = 테스트 플랜** 으로 착각 — 레지스터는 행 하나; 플랜의 단위는 feature(동작) × 카테고리.
4. **DV 테스트를 실리콘에서 통째로 재실행** — 시간 낭비 + Staff 시그널 반대. DV coverage report를 받아 중복을 빼고 smoke만.
5. **FPGA 성능 숫자로 결론** — 클럭 비율이 달라 무의미. PRF·PVT·SOAK는 실리콘 전용.
6. **형용사 통과 기준** ("안정적으로", "잘") — 값 + 조건 + 반복 + 판정.
7. **조건 폭발을 그대로 두기 / 반대로 nominal만** — 기능 축은 pairwise, 물리 축은 margin 민감 테스트에만 최악 corner부터.
8. **③ 매트릭스에서 5분 이상 쓰기** — 우선순위·통과 기준·자동화·리스크를 말할 시간이 사라진다. 행당 15초, 블록 특화 3–4행만 깊게.
9. **플랜을 A0에 시작** — 가장 값진 일(테스트 훅 요청, FPGA에서 P0 사전 검증)은 RTL freeze 전에 끝나야 한다.
10. **I2C 용어** — UM10204 rev 7부터 master/slave 대신 **controller/target** [1]. 면접에서 둘 다 알아듣지만, 최신 용어를 쓰면 스펙을 실제로 읽은 사람처럼 들린다.

## 8. 출처

1. NXP, UM10204 I2C-bus specification and user manual, Rev. 7.0, 2021-10-01 (속도 모드, Table 11 타이밍, §3.1.16 bus clear, controller/target 용어) — https://www.nxp.com/docs/en/user-guide/UM10204.pdf
2. Linux kernel documentation, Linux I2C fault injection (i2c-gpio injectors: scl/sda, incomplete_address_phase, incomplete_write_byte, lose_arbitration) — https://docs.kernel.org/i2c/gpio-fault-injection.html
3. AEC-Q100 stress test qualification (HTOL 3 lot × 77) — BV14 출처 [5] 재인용, 원문: https://www.aecouncil.com/AECDocuments.html
4. Linux kernel documentation, I2C dev-interface / i2c-tools — https://docs.kernel.org/i2c/dev-interface.html
5. TI BQ41Z50 datasheet, SMBus timing specifications (tTIMEOUT 25–35 ms) — https://ti.com/document-viewer/BQ41Z50/datasheet/GUID-08DC80FA-34FF-42B6-A547-A111D15B4C31
6. NIST, Automated Combinatorial Testing for Software (대부분의 실패는 1–2개 파라미터 상호작용, 경험적으로 1–6개) — https://csrc.nist.gov/projects/automated-combinatorial-testing-for-software

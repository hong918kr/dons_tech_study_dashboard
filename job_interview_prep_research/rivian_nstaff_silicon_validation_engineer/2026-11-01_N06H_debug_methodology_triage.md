# N06H · 디버그 방법론 · 실패 triage — "1-in-1000 실패, 어떻게 풀래?"를 말하는 틀 + 새 시나리오 6개
<!-- theme: pastel -->
> **묶음**: N06 검증 방법론 (디버그) · **우선**: P0 · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06G 플랫폼·회귀·자동화](2026-11-01_N06G_platforms_regression_automation.html) / [N06I 모의 면접 Q 뱅크](2026-11-01_N06I_mock_10min_qbank.html) · **관련**: [deck §5.1 Q8, §6.4, §7 시나리오 16개](2026-10-25_rivian_hm_screen_prep.html), [BV13 보드 디버그 시나리오 12선](2026-11-01_BV13_board_debug_scenarios.html), [LP15 PCIe 디버그 시나리오](2026-10-31_LP15_debug_scenarios.html), [BV14 §2.12 HW/SW triage](2026-11-01_BV14_board_validation_plan_automation.html), [BV01 §2.6 errata, §2.11 silicon/board/SW](2026-11-01_BV01_si_board_validation_big_picture.html), [LP14 통계](2026-10-31_LP14_validation_methodology.html), [BV12 shmoo/PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · **코드**: [onsitePrep/n06/h/flaky_bisect_sim.py](onsitePrep/n06/h/flaky_bisect_sim.py), [run.sh](onsitePrep/n06/h/run.sh), [실제 출력](onsitePrep/n06/h/run_output.txt)

## 0. TL;DR

- 이 페이지로 답할 것: **"A test fails 1 in 1000 on silicon — walk me through it"**, "HW냐 SW냐 어떻게 가르나", "고쳐졌다는 걸 어떻게 증명하나", "버그 리포트·errata·다른 팀 escalation을 어떻게 하나", 그리고 기존 노트에 **없는** 시나리오 6개(온도 따라 NPU 결과 변동 · 빌드 X부터 회귀(bisect) · 한 보드만 부팅 2배 느림 · 장비마다 측정값 다름 · debug 빌드 통과/release 실패 · 동시 실행 시만 데이터 손상).
- **외울 것 ① 6단계**: **Reproduce → Localize → Hypothesize → Instrument → Fix/workaround → Regress**. 각 단계에 "끝났다"는 기준이 있다(재현률 숫자 · 실패가 따라가는 변수 · 반증 가능한 가설 · 실패 순간 증거 · 통계적 확인 · 회귀 테스트 ID).
- **외울 것 ② 판단표 5열**: 결과가 **칩을 따라가면 silicon, 보드에 남으면 board, 빌드를 따라가면 SW, golden/체커를 바꿨더니 사라지면 test**. 실험 = 칩 swap · 보드 swap · V/F 의존성 · FPGA 재현 · 이전 stepping · 최소 SW · 체커 교차검증.
- **외울 것 ③ 통계**: 실패율 p일 때 "고쳤다" = 실패 0회로 **≈ 3/p 회** (1-in-1000 → 2,995회, 실제 계산 §6). **flaky 테스트로 bisect하면 단계마다 재실행이 필요** — p=0.05, 128 빌드면 단계당 96회 (1회씩이면 정답률 1%, 실제 시뮬레이션). 그래서 **먼저 stress로 p를 올리고**(0.05→0.30이면 단계당 14회), **실패 signature로 걸러라**(안 거르면 정답률 98% → 23%).
- Staff 시그널: **"I decide the pass/fail criterion with the other team before the experiment"**, **"a failure must be self-documenting"**, **"every root cause ends as a regression test, a checklist item, or an erratum — never just a closed ticket."**
- **30초 영어 답 (headline)**: "First I turn one-in-a-thousand into a number I trust and a failure that documents itself: loop it on the farm, capture state at the failure, and correlate across chips, boards, temperature and builds. That correlation tells me what the failure follows — chip, board, build or test — and I confirm it with a swap or a bisect. Then I stress the suspected axis to raise the failure rate, instrument the exact moment with a GPIO trigger and counters, and form hypotheses I can disprove. When we fix or work around it, I prove it statistically — about three thousand clean runs for a one-in-a-thousand bug — and the test stays in the regression."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "가끔 시동이 안 걸리는 차"

- 정비사가 "가끔 시동이 안 걸린다"는 차를 받았다. 일단 **재현**부터: 하루 100번 시동을 걸어 보니 1번 실패(재현률 1%). 그다음 **어디 문제인지 좁힌다**: 배터리를 다른 차 것으로 바꿔도 실패? 키를 바꾸면? 추운 아침에만? 그다음 **가설**("추울 때 배터리 전압이 떨어져 스타터가 못 돈다")을 세우고, **계측기**(전압 로거)를 달아 실패 순간을 잡는다. 고친 뒤엔 **300번 연속 성공**해야 "고쳤다"고 말한다(1% 문제를 3/p로). 마지막으로 **점검표에 항목을 추가**해 다음 차에서도 확인.
- 칩에서도 같다. 다만 "부품을 바꿔 본다"가 **칩 swap · 보드 swap · 펌웨어 빌드 bisect · FPGA에서 같은 테스트**로 바뀌고, "계측기"가 **scope · logic analyzer · 성능/에러 카운터 · trace buffer · GPIO 트리거**가 된다.
- 그리고 가장 자주 잊는 후보 하나: **"검사 장비(테스트 자체)가 틀렸다"**. 정비사의 전압계가 고장이면 차는 멀쩡하다.

### 1.2 그림 — 6단계와 각 단계의 산출물

```text
  ① Reproduce          ② Localize              ③ Hypothesize         ④ Instrument            ⑤ Fix / WA            ⑥ Regress
  ────────────          ───────────             ─────────────         ─────────────           ──────────            ──────────
  loop N회              무엇을 따라가나?          반증 가능한 가설 2–3    실패 순간 증거            FW/드라이버/칩          회귀 테스트 ID
  재현률 k/N             chip? board? build?     "X라면 Y를 바꾸면       GPIO 트리거 → scope/LA   chicken bit           nightly/PVT 캠페인
  실패 signature         temp? V/F? test?        Z가 보여야 한다"        카운터·trace·덤프        다음 stepping          errata 문서
       │                      │                        │                     │                       │                      │
       └── 산출물: 숫자 ──────┴── 산출물: 변수 1개 ────┴── 산출물: 실험 계획 ─┴── 산출물: 원인 증거 ──┴── 산출물: 통계 확인 ──┘
                                          ▲                                          │
                                          └───────── 가설이 틀리면 ②로 돌아감 ◄───────┘
```

### 1.3 용어 사전

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| triage | — | 실패를 분류해서 **누가 볼지** 정하는 일 (응급실 분류) | 엉뚱한 팀에 가면 몇 주 날아감 |
| repro rate | reproduction rate | N번 돌려서 몇 번 실패하나 (k/N) | 모든 판단의 출발점; "가끔"은 숫자가 아님 |
| signature | failure signature | 실패를 구별하는 지문: 에러 코드 + 실패 위치 + 패턴 | 서로 다른 버그 두 개를 하나로 착각하지 않게 |
| flaky test | — | 같은 조건에서 통과/실패가 섞이는 테스트 | bisect·회귀 판정을 망가뜨림 |
| bisect | binary search over builds/conditions | good과 bad 사이를 반씩 잘라 처음 나빠진 지점 찾기 | 128 빌드도 7단계 |
| ABA swap | — | 부품을 A→B로 옮겼다가 다시 A로 | 우연·접촉 불량 배제 |
| stepping | silicon revision (A0, A1, B0) | 칩 버전. 글자=트랜지스터층 변경, 숫자=배선층만 | "이전 stepping에도 있었나"가 큰 단서 |
| errata / erratum | — | 칩의 알려진 버그 목록 / 한 항목 | 고객·SW팀이 workaround를 적용하게 함 |
| workaround (WA) | — | 칩은 그대로 두고 FW·설정으로 피하는 방법 | 실리콘 재제작 없이 출하 가능 |
| chicken bit | — | 새 기능/최적화를 끄는 숨은 설정 비트 | 가장 흔한 HW 버그 workaround |
| rule of three | — | 0번 실패/n번 → 실패율 95% 상한 ≈ 3/n [2] | "고쳤다"의 최소 횟수 |
| shmoo | — | 전압×주파수(×온도) 격자에서 pass/fail 지도 | 마진 문제인지 기능 문제인지 |
| V/F/T | voltage / frequency / temperature | 칩 동작 조건 3축 | 의존성이 있으면 아날로그·타이밍 쪽 |
| golden | golden reference / model | 정답 출력 (C 모델, 이전 칩, 다른 구현) | golden이 틀리면 "버그"가 가짜 |
| negative control | — | 일부러 틀린 입력으로 테스트가 실패하는지 확인 | 테스트가 실패할 줄 아는지 증명 |
| MSA / gauge R&R | measurement system analysis / repeatability & reproducibility | 측정 시스템 자체의 오차를 재는 절차 | 장비끼리 값이 다를 때 |
| UB | undefined behavior | C 표준이 결과를 정하지 않은 코드 (최적화하면 달라짐) | debug/release 차이의 1순위 |
| temperature inversion | — | 저전압 미세공정에서 **차가울수록 느려지는** 현상 [6][7] | "고온이 최악"이라는 상식이 틀릴 수 있음 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 디버그 6단계 — 재사용하는 말하기 틀

| 단계 | 하는 일 | 끝났다는 기준 (exit) | 흔한 실수 |
|---|---|---|---|
| ① **Reproduce** | 같은 조건으로 자동 loop, 재현률 측정, 실패 signature 정의, 실패 1회가 증거를 남기게 자동 수집 설정 | "k/N, signature S, 조건 C"를 한 줄로 쓸 수 있다 | 한 번 보고 바로 가설로 점프; signature 없이 "실패" 하나로 뭉뚱그림 |
| ② **Localize** | 무엇을 따라가나: 칩 · 보드 · 빌드 · 조건(V/F/T) · 블록 on/off · 테스트/체커. 상관표 → swap/bisect로 확인 | 실패와 **정확히 일치하는 변수 1개**(+ 그것을 바꾸면 사라지는 실험) | 여러 변수를 한꺼번에 바꿈; 팜 슬롯·케이블 같은 인프라를 배제 안 함 |
| ③ **Hypothesize** | 반증 가능한 가설 2–3개, 확률×확인 비용으로 순위 | 가설마다 "맞다면 X를 바꾸면 Y가 보인다" 예측이 있다 | 확인 안 되는 가설("타이밍 문제 같다"); 가장 그럴듯한 하나에 집착 |
| ④ **Instrument** | 실패 순간을 잡는 장치: SW가 오류 감지 시 GPIO 토글 → scope/LA 트리거, 카운터 스냅샷, trace buffer freeze, 레지스터 덤프 | 원인을 직접 보여 주는 증거(파형·트레이스·덤프) | 계측이 타이밍을 바꿔 버그가 사라짐(heisenbug) — 그 자체가 단서임을 기록 안 함 |
| ⑤ **Fix / workaround** | 원인 수정 또는 workaround; HW 버그면 errata + WA + 다음 stepping 수정 요청 | **같은 loop·같은 조건으로 통계적 확인** (≈ 3/p 무실패) | "10번 돌려 봤는데 괜찮다"; 증상만 가리는 fix(타임아웃 늘리기) |
| ⑥ **Regress** | 재발 방지: 회귀 테스트(nightly/PVT) · bring-up 체크리스트 · 입고 스크린 · errata 고정 테스트 | 테스트 ID가 회귀 목록에 있고, **WA를 빼면 실패하는 것**까지 확인 | 티켓만 닫음; 회귀 테스트가 그 버그를 실제로 잡는지 안 봄 |

**단계별 영어 문장 (그대로 말하기)** — 면접에서 "how would you debug X?"가 나오면 이 순서로 한 문장씩.

1. Reproduce — "First I want a number, not an anecdote: I loop the test under the same conditions until I can say it fails k out of N with this exact signature, and I set up automatic capture so every failure leaves evidence."
2. Localize — "Then I ask what the failure follows. I correlate across chips, boards, builds, voltage, frequency and temperature, and I confirm the strongest correlation with a swap or a bisect, changing one variable at a time."
3. Hypothesize — "I write down two or three hypotheses that make different predictions — if it's a margin issue, raising voltage should make it disappear; if it's a software race, it shouldn't care about voltage but should care about timing."
4. Instrument — "I instrument the exact moment of failure: the test toggles a GPIO when it detects the error, which triggers the scope or logic analyzer, and I snapshot the error counters and freeze the trace buffer."
5. Fix — "Once the root cause is proven, we fix it or work around it, and I verify with the same loop — for a one-in-a-thousand failure that's about three thousand clean runs before I call it fixed."
6. Regress — "Finally the test goes into the regression at the condition that exposed it, and I check that it fails when the fix is removed — otherwise the regression test doesn't actually protect us."

- 접속 문장: "Before I go deep, let me make sure I understand the symptom…"(clarify) · "That tells me it follows the chip, so…" · "I'd rule out the cheap things first — the farm slot, the cable, the test itself."
- 시간 관리: 면접 답은 ①② 에 40%를 쓴다. 대부분 지원자가 ③에서 바로 시작하는데, 면접관은 **재현·국소화를 체계적으로 하는지**를 본다 [추정].

### 2.2 HW vs SW vs board vs test — 판단표

[BV01 §2.11](2026-11-01_BV01_si_board_validation_big_picture.html)·[BV13 §2.3](2026-11-01_BV13_board_debug_scenarios.html)은 silicon/board/SW 3열이다. 여기서는 **"test(테스트·체커·장비) 자체가 틀림"** 열과 **FPGA 재현 · 이전 stepping · 체커 교차검증** 행을 더한 5열 버전.

| 실험 | → silicon (설계/공정) | → board | → SW/FW | → test (테스트·golden·장비) |
|---|---|---|---|---|
| **칩 swap** (소켓/rework, ABA) | 실패가 칩을 따라감 | 실패가 보드에 남음 | 무관 (모든 칩·보드 동일) | 무관 |
| **보드 swap** (같은 칩 lot·FW) | 같은 corner 칩 보드만 | 특정 보드·rev만 | 모든 보드 동일 | 모든 보드 동일 |
| **V/F 의존성** (shmoo) | 강함: Vmin 근처·고주파에서만, corner 칩에서 벽 낮음 | 강함: 특정 rail droop/SI, 그 보드만 벽 낮음 | 약함 (예외: 주파수가 바뀌면 race 창이 바뀜) | 없음 (단, timeout 하드코딩이면 주파수 의존처럼 보임) |
| **FPGA 재현** (같은 테스트 바이너리) | FPGA에서도 실패 → **RTL 로직 버그** 유력; FPGA 통과·실리콘 실패 → 타이밍/아날로그/물리 | FPGA 무관 | FPGA에서도 실패 (SW 로직) | FPGA에서도 실패, 같은 signature |
| **이전 stepping** (A0 vs A1, ECO 전후) | 새 stepping에서만 → ECO 회귀; 양쪽 → 원래 설계 | 무관 | 무관 | 무관 |
| **최소 SW** (Linux → bare-metal → JTAG 직접 쓰기) | 최소 시퀀스로도 재현 | 최소 시퀀스로도 재현 (+ 보드 의존) | 스택을 빼면 사라짐 | 다른 체커로 보면 "실패"가 아님 |
| **빌드 bisect** | 빌드 무관 | 빌드 무관 | 특정 커밋부터 | 특정 **테스트 커밋**부터 (제품 코드 아님!) |
| **체커/golden 교차검증** (다른 구현·다른 장비·negative control) | 두 체커 모두 실패 | 두 체커 모두 실패 | 두 체커 모두 실패 | **체커끼리 결과가 다름**; golden 모델 버전에 따라 달라짐 |
| **RTL 시뮬레이션** (실패 시퀀스를 DV가 재생) | 재현 → RTL 버그 확정 | 재현 안 됨 | 재현 안 됨 | 재현 안 됨 |

- **결정 순서 (싼 것부터)**: 테스트·인프라 배제(체커 교차검증, 팜 슬롯 swap) → 빌드 bisect(자동, 사람 시간 0) → 보드 swap → V/F/T 스윕 → 칩 swap(비쌈) → FPGA/RTL 재현(설계팀 시간).
- **"silicon bug"는 마지막 결론**: 보드·SW·테스트를 배제하고, 여러 칩에서 spec 내 조건으로 재현, 가능하면 RTL 시뮬레이션 재현까지. "칩을 따라간다"만으로는 **corner 마진**일 수 있다 (spec 밖이면 버그가 아니라 bin/스크린 문제).
- 이전 stepping이 **없는** A0에서는: FPGA 재현 + 같은 IP를 쓴 이전 칩(재사용 IP) + DV 시뮬레이션 재생이 대신한다.
- 한 줄: **"What does the failure follow?"** — 면접에서 이 질문을 소리 내어 말하는 것 자체가 시그널.

### 2.3 간헐 실패(1-in-1000) 플레이북 — 통계로 증명까지

| 순서 | 할 일 | 구체적으로 |
|---|---|---|
| 1 | **숫자로 만든다** | 팜에서 무인 loop. "3/3,000, signature `NPU_TIMEOUT@job_done`, 25 °C, OPP3" — 점추정 0.1%, 95% 상한 0.26% (§6 [6]) |
| 2 | **실패 1회가 증거를 남기게** | 오류 감지 즉시 GPIO 토글(scope/LA 트리거) · 카운터/상태 레지스터 스냅샷 · trace buffer freeze · DDR 링 로그 보존 · **리셋 전에** 덤프 (리셋이 증거를 지움) |
| 3 | **상관관계** | 결과 DB에 chip ID · lot/corner · board SN/rev · 팜 슬롯 · build hash · seed · V/F/T · 실행 순서 · 경과 시간을 붙여 조인. 실패와 일치하는 열 찾기 ([BV14 §2.12](2026-11-01_BV14_board_validation_plan_automation.html)) |
| 4 | **빈도 올리기 (stress)** | 의심 축을 극단으로: 저전압 margin, 고/저온(temperature inversion 주의), 최대 주파수, 동시 트래픽, 짧은 리셋 간격, 랜덤 seed 폭 확대. **signature가 같은지** 반드시 확인 (다른 버그를 키우면 안 됨) |
| 5 | **이분 탐색** | 빌드(`git bisect run`) · 조건(온도 계단) · 블록 on/off · 코드 구간(체크포인트 GPIO). flaky면 **단계마다 재실행 수**를 계산 (§2.3 표, §6 [1]) |
| 6 | **증명** | 고친 뒤 같은 loop로 실패 0회 × **ln(0.05)/ln(1−p) ≈ 3/p 회**. 1-in-1000 → 2,995회. 시간이 없으면 stress 조건에서(높은 p) 증명 + 정상 조건 soak |

**통계 표 (모두 §6 실제 계산값)**

| 질문 | 공식 | 예 |
|---|---|---|
| p를 한 번이라도 재현하려면 (95%)? | n = ln(0.05)/ln(1−p) ≈ 3/p | p=0.001 → 2,995회 |
| 고친 뒤 "p_new < p_old" (95%)? | 같은 식, 실패 0회 | p_old=0.01 → 299회 |
| 실패가 k번 보였을 때 상한? | Poisson 상한 λ_k / n (λ_0=3.00, λ_1=4.74, λ_3=7.75) | 1/3,000 → 상한 0.158% |
| flaky bisect 단계당 재실행 r? | (1−(1−p)^r)^S ≥ 0.95, S=단계 수 | p=0.05, S=7 → r=96 |
| 아무 효과 없는 fix가 59회 무실패를 통과할 확률? | (1−p)^n | p=0.05, n=59 → 시뮬레이션 4.94% |

- 함정: **"고쳤다" 판정은 독립 시행 가정**. 같은 보드·같은 칩에서만 3,000회면 그 칩에 대한 증명일 뿐 → 칩/보드 여러 개로 나눠 돌린다. 실패가 시간 경과(발열, 메모리 단편화, 카운터 wrap)와 관련 있으면 "짧은 3,000회"가 아니라 **장시간 soak**가 필요.
- 부분적으로 줄었다(1/1,000 → 1/5,000)면 "줄었다"와 "없어졌다"를 구분해서 보고: 원인이 하나 더 있거나, fix가 창(window)만 좁혔을 수 있다 → signature 재확인.

### 2.4 좋은 버그 리포트 — 템플릿 + 영어 예시 1개

| 칸 | 내용 | 체크 |
|---|---|---|
| Title | `[블록][stepping][조건] 증상 — 재현률` 한 줄로 검색 가능하게 | 제목만 보고 dedup 가능? |
| Severity / Priority | 영향(데이터 손상 > hang > 성능 > 로그) · 차단하는 일정 | S1–S4 기준 공유됨? |
| Environment | chip ID · stepping · lot/corner · board SN/rev · FW/test build hash · test ID + seed · V/F/T · 팜 슬롯 | 남이 똑같이 재현 가능? |
| Steps to reproduce | 최소 재현 명령 1–3줄 (repro package 경로) | 사람 손 없이 재현? |
| Expected vs Actual | spec 문단/요구사항 ID 인용 vs 관찰값 | "기대"의 출처가 있음? |
| Frequency | k/N + 조건별 표 (95% 상한) | 숫자 있음? |
| Evidence | 파형 원본 · 덤프 · 로그 · 카운터 · 첨부 경로 | 해석과 원본 분리? |
| Ruled out | 배제한 것과 **그 근거** | 리포트 신뢰도의 핵심 |
| Hypothesis | 현재 가설 + 반증 실험 | "의견"이 아니라 예측 |
| Ask / Next | 누구에게 무엇을 언제까지 | 받는 사람이 할 일이 명확? |

BV13 §2.5는 보드(DDR) 예시. 여기는 **SoC 기능 블록 + 전원관리** 예시 (§3.1 사례의 첫 escalation 시점).

```text
[예시 — 가상의 칩·수치, 실제 Rivian 데이터 아님]
Title:      [NPU][A0][DVFS OPP2->OPP4, -40C] NPU hangs after clock switch — 7/7,000 transitions at -40C, 0/7,000 at 25C
Severity:   S2 (hang, recoverable only by NPU reset; blocks DVFS feature sign-off for A0 milestone)
Environment: SoC A0, lot L07 wafer 3, corner TT (fused), boards VB-012/VB-017/VB-021 (rev B)
            validation FW 2.3.1-g8c41e0a, test dvfs_switch_stress seed 0x5EED, chamber -40C (thermocouple on lid)
            VDD_NPU nominal per OPP table, farm rack 2 slots 3/5/7
Repro:      ./farm run dvfs_switch_stress --board VB-012 --temp -40 --iters 7000   (repro pkg: //repro/NPU-1187/)
Expected:   After the PLL reports lock, switching the NPU clock mux completes and the next job finishes within 2 ms
            (DVFS requirement REQ-PM-031)
Actual:     NPU job never completes; NPU status = BUSY, no error bit; CPU and other blocks keep running
Frequency:  -40C: 7/7,000 (point 0.10%, 95% upper 0.21%); 0C: 1/7,000; 25C: 0/7,000; 85C: 0/7,000
            same on all 3 boards (3/2/2 of 7) -> not board-specific
Evidence:   scope on debug clock-out pin (NPU_CLK/16): runt pulses for ~3 us right after the mux switch on failing
            iterations only; GPIO trigger from FW at "lock seen" and "mux switched"; PLL status register dump
Ruled out:  FW build (same with 2.2.0 and 2.3.1); board (3 boards, same rate); farm slot (moved, same);
            test bug (negative control: forcing an invalid OPP is flagged; job checksum checker verified on FPGA)
Hypothesis: PLL lock indicator asserts before the output is stable at cold; FW samples LOCK once and switches.
            Prediction: requiring LOCK stable for N consecutive reads over >=20 us should drop the rate to 0.
Ask:        PLL/analog team: lock-detect behavior and spec for "lock -> safe to switch" at -40C (by Wed)
            Clock/PM FW: prototype the stable-lock poll; I will run 3,000 transitions at -40C on 3 boards
```

- 말로 할 때: "My bug report always has the ruled-out line — that's what makes the other team trust it."

### 2.5 Errata 수명주기

[BV01 §2.6·§3.5](2026-11-01_BV01_si_board_validation_big_picture.html)가 erratum **문서 양식**을 다룬다. 여기는 **흐름과 validation 엔지니어가 각 단계에서 하는 일**.

```text
 발견 ──► triage ──► root cause 확정 ──► workaround 설계 ──► WA 검증 ──► 문서화/배포 ──► 다음 stepping 수정 ──► 수정 검증 ──► 종료
 (val)    (val+DV)   (design+DV: RTL    (design+FW: 순서/   (val: 통계    (errata DB,     (design: metal ECO    (val: WA 끈   (또는 "No fix"
                      sim 재현)          chicken bit/퓨즈)    + 성능 비용)   SDK, 고객공지)  or base layer)       상태로 재현 0) + 영구 WA)
```

| 단계 | 산출물 | validation이 하는 일 | 함정 |
|---|---|---|---|
| 발견 | 버그 티켓 (§2.4) | 재현률·signature·조건 | 기존 erratum과 dedup 안 함 |
| root cause | RTL sim 재현 / 설계 확인 | 실패 시퀀스를 DV가 재생할 수 있는 최소 형태로 | "아마 이거" 상태로 errata 작성 |
| workaround | WA 후보 (싼 것부터: 순서/지연 → chicken bit → 퓨즈 → 보드 → stepping) | 후보별 **효과(3/p 무실패)와 비용(성능·전력·면적 없음)** 측정 | WA가 다른 기능을 깨는지 회귀 안 함 |
| 문서화 | erratum: 조건·영향·WA·영향 stepping | "Found by"·검증 통계 칸 채우기; SW팀이 WA를 **기본 적용**하게 | WA가 문서에만 있고 SDK 기본값이 아님 |
| 회귀 고정 | **errata 테스트 2벌**: WA on → PASS 기대, WA off → FAIL 기대(A0) | 두 개 다 nightly에 | WA-off 테스트가 없으면 WA가 조용히 빠져도 모름 |
| 다음 stepping | 수정 포함 여부 결정 (Fix in B0 / No fix planned) | 우선순위 근거 제공: 빈도·영향·WA 비용 | 성능 비용 큰 WA를 "있으니까 됐다"로 방치 |
| 수정 검증 | B0에서 **WA off 테스트가 PASS** + 같은 통계 | stepping 감지해서 WA 자동 on/off하는 FW 경로도 테스트 | B0에서 WA를 계속 켜 둬서 수정을 실제로 검증 안 함 |
| SLT/양산 | 마진성 버그면 스크린 조건 추가 | SLT 콘텐츠로 이관 ([N06F](2026-11-01_N06F_plans_npu_soc_safety.html) F5) | 기능 버그를 스크린으로 막으려 함 (모든 칩에 있음) |

- 한 줄: **"An erratum isn't closed when it's documented; it's closed when the workaround is the default, a test proves it works, and a second test proves the next stepping no longer needs it."**

### 2.6 크로스팀 escalation — 누구에게, 언제, 무엇을 들고

| 증거가 가리키는 곳 | 상대 팀 | 들고 갈 것 | 그 팀이 원하는 형태 |
|---|---|---|---|
| RTL 로직 (FPGA/시뮬 재현) | design/RTL · DV | 최소 레지스터 시퀀스, 실패 시점 상태, FPGA 재현 여부 | **5줄 repro + 파형** (전체 시스템 로그 X) |
| 타이밍 마진 (V/F/T 의존, corner 칩) | physical design/timing · silicon char | shmoo 지도, corner/lot, on-die sensor 값 | Vmin/Fmax 숫자, 경로 힌트 |
| 아날로그/PHY/PLL | analog · PHY 팀 | scope 원본, 온도 계단 데이터, 레지스터 상태 | 측정 조건(프로브·대역폭) 명시 |
| 보드 (보드에 남음) | board HW · SI/PI | 측정점·good/bad delta·BOM/rework | 회로 수정 제안까지 |
| SW/FW (빌드 따라감) | FW/driver | first-bad commit, 최소 재현 | 커밋 해시 + 테스트 명령 |
| 테스트/인프라 | 우리 팀 | — | 같은 채널에서 인정 + 회귀 추가 |
| 일정·출하 영향 | program/product | 영향·WA 비용·위험 3줄 | 결정할 수 있는 선택지 2–3개 |

**escalation 기준** (Staff가 먼저 정해 두는 것): S1(데이터 손상·안전)은 재현률 무관하게 당일 공유 · 마일스톤 차단은 24시간 내 · 그 외는 증거 6칸이 찰 때. 그리고 **"누가 owner인지 모호하면 내가 owner"** — 결론 날 때까지 회의·데이터를 내가 끈다.

**Staff 시그널 문장 (영어)**

- "Before we run the experiment, let's agree on what result would make this a board issue versus a silicon issue — then the data decides, not the meeting."
- "I'll bring a five-line reproduction and the raw waveform; my interpretation is in a separate section so you can disagree with it."
- "This is blocking thirty percent of the nightly results, so I'd like to split it: firmware workaround this week, root cause in parallel."
- "It turned out to be our test — I've posted that in the same thread where I raised it, and the checker now has a negative control."
- "Ownership is unclear, so I'll drive it: I'll run the daily sync until we have a root cause and an owner for the fix."
- "I want the regression test to fail with the workaround removed; otherwise it isn't protecting us."

## 3. 실전 적용 — 예시 + 새 시나리오 드릴 6개

### 3.1 [예시] 한 건 end-to-end — "DVFS 전환 후 NPU hang, 1-in-1000 at cold"

Rivian 자체 SoC의 NPU·DVFS 구조는 공개 정보 없음 → 블록 구조·수치 전부 `[예시]`/`[추정]`.

| 단계 | 한 일 | 결과 |
|---|---|---|
| ① Reproduce | nightly `dvfs_switch_stress`가 주 1–2회 hang. 팜에서 7,000 전환 loop × 온도 4개 × 보드 3장 | -40 °C 7/7,000, 25 °C 0/7,000. signature: NPU BUSY, 에러 비트 없음, 다른 블록 정상 |
| ② Localize | 상관: 보드 무관(3장 모두), 빌드 무관(2개), **온도 강한 의존**, OPP2→OPP4(PLL 재설정이 있는 전환)만, PLL 재설정 없는 전환은 0 | "PLL을 다시 lock하는 전환 + 저온" 하나로 좁혀짐 |
| ③ Hypothesize | H1 PLL lock 판정이 너무 이름 (예측: lock 후 대기 늘리면 0) · H2 새 OPP 전압이 저온에서 부족 (temperature inversion; 예측: 전압 +3%면 0) · H3 FW가 mux 전환 중 NPU 클럭 게이팅 해제 순서 race (예측: 온도 무관해야 함 → 약함) | H1·H2 가 다른 예측을 함 → 실험 2개로 가름 |
| ④ Instrument | FW가 "LOCK 읽음"·"mux 전환" 시점에 GPIO 토글 → scope 트리거; NPU 클럭 debug 출력(분주)을 같은 화면에; PLL 상태 레지스터를 100 ns 간격으로 polling 로그 | 실패 회차에서만 mux 직후 ~3 µs runt pulse; LOCK 비트가 한 번 1 → 0 → 1 (chatter) |
| ⑤ Fix/WA | 전압 +3%: 7/7,000 → 6/7,000 (H2 기각). LOCK 연속 N회 안정 확인(≥ 20 µs) 후 전환: **0/3,000 × 3장 at -40 °C** (각 보드에서 1-in-1000 상한 95%) + 25 °C 10,000 soak | WA = FW. analog 팀: lock detector 필터 B0에서 수정 |
| ⑥ Regress | `dvfs_switch_stress` cold 버전을 주간 PVT 캠페인에; WA-off 버전(A0에서 FAIL 기대) 추가; erratum `ERR-0xx` `[예시]` 작성; SDK 클럭 드라이버 기본값으로 WA | B0 도착 시 WA-off PASS로 수정 확인 |

- 말할 포인트: ② 에서 "PLL 재설정 있는 전환만"이라는 **조건 쪼개기**가 결정적이었다는 것, ③ 에서 **가설마다 다른 예측**을 세워 실험 2개로 가른 것, ⑤ 에서 전압 실험이 **가설을 기각**한 것(실패를 말하는 게 신뢰를 준다).

### 3.2 시나리오 N1 · NPU 결과가 온도에 따라 달라짐

> 겹침 확인: [BV13 S7](2026-11-01_BV13_board_debug_scenarios.html)은 "고온에서 보드/DDR이 **실패**", deck §7 #9는 "벤치마크 **성능** 20% 변동". 여기는 **계산 결과(정확도·bit-exact)가 온도에 따라 바뀜** — 정답성 문제.

#### 증상

- 같은 입력 텐서·같은 모델·같은 FW로 NPU 추론. 25 °C에서는 golden과 bit-exact, 85 °C 챔버에서는 출력 텐서의 0.01% 원소가 ±1 LSB 다름, 105 °C에서는 top-1 결과가 가끔 바뀜 `[예시]`.

#### 먼저 물어볼 것 (clarify)

- "Is the reference bit-exact INT8, or FP16 with a tolerance? Does it differ by one LSB or by large amounts?"
- "Is the mismatch at the same tensor elements every time, or random positions?"
- "Does the NPU clock or the scheduler change with temperature — thermal throttling, DVFS, a different tiling plan?"
- "Is this on every chip, or a few? Any ECC-corrected counts on the NPU SRAM?"
- "Does the same thing happen at -40 °C?"

#### 가설 (순위)

| 순위 | 가설 | 왜 그럴듯 | 맞다면 보이는 것 | 가르는 실험 |
|---|---|---|---|---|
| 1 | **Thermal throttling → 다른 실행 경로**: 고온에서 클럭이 내려가면 런타임이 다른 tiling/분할·다른 accumulation 순서 선택 → FP16은 결합법칙이 안 맞아 결과가 조금 다름 | 흔하고, ±1 LSB 패턴과 일치 | throttle 이벤트 로그와 mismatch가 1:1; **고정 클럭**이면 고온에서도 bit-exact | 클럭·OPP 고정 + throttle off(안전 한계 내)로 85 °C 재실행; 런타임의 tiling plan 로그 diff |
| 2 | **SRAM/레지스터 파일 마진**: 고온 누설로 비트 flip, ECC 없는 버퍼(예: accumulator) | 105 °C에서 큰 오류 | 위치 랜덤, ECC corrected 카운터 상승, 저전압에서 악화 | V×T shmoo; 메모리 BIST/패턴 테스트를 NPU SRAM에; ECC 카운터 |
| 3 | **타이밍 경로 마진** (특정 연산자 데이터패스) | 특정 칩·특정 연산자만 | 같은 연산자·같은 비트 위치; 주파수 낮추면 사라짐 | 연산자별 단위 테스트 × 주파수 스윕; temperature inversion 때문에 **저온도** 확인 [6][7] |
| 4 | **입력/양자화 경로가 온도 의존** (센서 아님 — 고정 텐서인데도? → 텐서가 정말 고정인지) | 테스트 셋업 실수 | 입력 해시가 실행마다 다름 | 입력·가중치·출력 해시를 매 실행 기록 |
| 5 | **golden 비교 기준이 틀림** (FP16 tolerance가 너무 빡빡) | test 쪽 | 허용 오차 정의 변경 시 사라짐 | 다른 golden(CPU FP32 reference) 교차검증 |

#### 무엇을 재고 계측하나

- 매 추론마다: 입력/가중치/출력 **해시**, NPU 클럭·OPP, throttle 상태, on-die 온도 센서(챔버 온도가 아니라 **die 온도**), SRAM ECC corrected/uncorrected 카운터, 런타임의 실행 계획 ID.
- 불일치 **패턴 특징화** ([V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) 방식): 원소 위치(타일 경계?), 비트 위치(LSB만? 상위 비트?), 크기(±1 vs 큰 값), 연산자별.
- 온도 계단 25→45→65→85→105 °C, 그리고 **-40 °C** (저전압 미세공정에선 저온이 느린 코너일 수 있음 [6][7]).

#### 흔한 root cause

- ±1 LSB, 위치가 실행 계획 따라 달라짐 → **결정론(determinism) 부족**: 클럭/스케줄 변화가 accumulation 순서를 바꿈 (HW 버그 아님, 하지만 안전 관점에서 **요구사항**이 bit-exact인지 정해야 함).
- 큰 값·랜덤 위치·ECC 카운터 상승 → **SRAM 마진** (silicon/공정, 스크린 조건 후보).
- 특정 연산자·특정 비트 → **데이터패스 타이밍 마진** (silicon char·physical design).

#### 고치고 회귀로 굳히기

- 결정론 문제: 런타임이 온도/클럭과 무관하게 같은 실행 계획을 쓰도록 하거나, INT8 경로는 bit-exact를 요구사항으로 명시 + FP16은 허용 오차를 문서화. 회귀: **"추론 결과 해시 × 온도 × OPP" 매트릭스** 테스트를 PVT 캠페인에.
- 마진 문제: shmoo로 벽 위치 → Vmin 가드밴드 조정 또는 SLT 스크린 조건; NPU SRAM 패턴 테스트를 SLT에.

<details><summary>🎤 English spoken answer (≈2 min)</summary>

"First I'd pin down what 'different' means — one LSB in a few elements, or large errors — and whether the positions are stable, because those point to completely different causes. Then I'd log, for every run, hashes of the input, weights and output, the NPU clock and operating point, the throttling state, the die temperature from the on-chip sensor rather than the chamber setpoint, and the SRAM ECC counters. If the mismatches are small and line up with throttling events, my first hypothesis is that the runtime picks a different execution plan at a lower clock and the accumulation order changes; I'd test that by fixing the clock and the plan and rerunning hot — if it becomes bit-exact, it's a determinism issue, not a hardware bug, and we need a clear requirement on bit-exactness. If the errors are large, random and the ECC counters rise, it's a memory margin problem, and I'd run a voltage-temperature shmoo and a pattern test on the NPU SRAM. If it's one operator and one bit position on a few chips, it smells like a datapath timing path, so I'd sweep frequency — and I'd also test cold, because at low voltage in modern nodes cold can be the slow corner. Whatever the cause, the regression becomes an output-hash matrix across temperature and operating points in the PVT campaign."

</details>

### 3.3 시나리오 N2 · 회귀가 빌드 X부터 깨짐 — flaky 테스트로 bisect

> 겹침: BV13 §2.4·BV14 §2.12는 "`git bisect run`을 쓴다"까지. 여기는 **테스트가 flaky하고, 빌드가 FW + 테스트 + FPGA 이미지 + 툴체인 여러 축일 때** 실제로 어떻게 하나 + 몇 번 돌려야 하나(§6 시뮬레이터).

#### 증상

- nightly에서 `ddr_qos_mixed_traffic`가 빌드 1412부터 가끔 실패(1412 이후 10번 중 3번 빨간 불), 1290까지는 30일 연속 녹색. 사이에 커밋 128개 `[예시]`.

#### 먼저 물어볼 것

- "What changed between the builds — firmware only, or also the test code, the FPGA bitstream, the toolchain, the farm itself?"
- "Is the failure signature identical every time?"
- "What's the failure rate on a bad build — once in three runs, or once in fifty?"
- "Did the hardware pool change in that window — new boards, a new chip lot, a different rack?"
- "Can every build in the range still be built and flashed?"

#### 가설 (순위)

| 순위 | 가설 | 확인 |
|---|---|---|
| 1 | FW 커밋이 원인 (타이밍 변화로 원래 있던 race가 드러남 포함) | bisect → first-bad 커밋; 그 커밋이 "원인"인지 "노출"인지 별도 판단 |
| 2 | **테스트 코드** 변경 (체커가 더 엄격해짐, seed 범위 확대) | 테스트 리포와 제품 리포를 **따로** bisect (2축) |
| 3 | 툴체인/빌드 옵션 변경 (§3.6과 연결) | 같은 소스를 옛 툴체인으로 |
| 4 | 인프라/하드웨어 풀 변화 (새 보드가 그 즈음 팜에 들어옴) | 옛 빌드를 **지금** 팜에서 다시 돌림 — 옛 빌드도 지금 실패하면 빌드 문제가 아님 |

#### 무엇을 재고 계측하나

- **먼저 "옛 good 빌드를 오늘 다시"** 돌린다: 오늘도 good이면 빌드 축, 오늘은 bad면 HW/인프라 축 (가장 싸고 가장 자주 건너뛰는 단계).
- bad 빌드의 실패율 p 측정 → 단계당 재실행 r 계산 (§6 [1]): p=0.3이면 r≈11–14, p=0.05면 r=96.
- 판정 스크립트는 **signature 일치 실패만 bad**(exit 1–127), 빌드/플래시 불가는 **125(skip)**, 인프라 오류는 판정하지 말고 재시도 [1].
- 여러 축이 섞였으면 축을 고정: 테스트 리포 고정 + FW bisect, 그다음 반대.

#### 흔한 root cause

- 커밋이 **타이밍을 바꿔** 숨어 있던 HW/FW race를 노출 (예: 로그 출력 제거로 빨라짐) — first-bad 커밋 작성자에게 넘기면 "내 코드 문제 아님" 논쟁 → 그 커밋의 diff가 바꾼 **타이밍**을 설명해 주고 원래 race를 찾는다.
- 테스트가 엄격해져 원래 있던 버그를 잡기 시작 (좋은 일 — 제품 버그로 재분류).
- 툴체인 업그레이드 + UB (§3.6).

#### 고치고 회귀로 굳히기

- 자동 bisect를 CI에 내장: 새 실패 signature → 자동으로 p 추정 → r 계산 → bisect → first-bad 커밋 작성자에게 티켓 (사람 시간 0).
- flaky 정책: 회귀가 "녹색"이라는 판정 자체에 재실행 수를 정의 (1회 통과 ≠ good).

<details><summary>🎤 English spoken answer (≈2 min)</summary>

"Before bisecting I do one cheap experiment: rerun the last known-good build on today's farm. If it still passes, the cause is in the builds; if it now fails, the hardware pool or infrastructure changed and bisecting software would waste days. Then I list every axis that changed — firmware, test code, FPGA image, toolchain — and bisect one axis at a time with the others pinned. The important part is that the test is flaky. If a bad build fails only one run in twenty, a single run per bisect step will call bad builds good and the bisect lands in the wrong place — in my simulation with 128 builds, one run per step finds the right commit about one percent of the time. With seven bisect steps and a five percent failure rate I need about ninety-six runs per step for ninety-five percent confidence, stopping early on the first failure. That's expensive, so first I look for a stress that raises the failure rate — at thirty percent, fourteen runs per step is enough. And the check script only counts failures with the same signature, because unrelated flakes on good builds otherwise drag the bisect off course. Once I have the first bad commit, I still ask whether it caused the bug or just exposed a race by changing timing."

</details>

### 3.4 시나리오 N3 · 한 보드만 부팅이 2배 느림

> 겹침: BV13 S4(재부팅 루프)·S8(일부 보드 **실패**)·S12(rev 차이)와 다름 — 기능은 다 **통과**하는데 **시간**만 다름. "실패가 아닌 이상치"도 버그일 수 있다는 것이 포인트.

#### 증상

- 팜 보드 20장 중 VB-009만 power-on → Linux 로그인 프롬프트(또는 bare-metal 테스트 준비 완료)까지 4.1 s, 나머지는 2.0 ± 0.1 s `[예시]`. 테스트는 모두 PASS.

#### 먼저 물어볼 것

- "Is it slow on every boot, or occasionally? Cold boot and warm reset both?"
- "Same image, same boot media type, same board revision and BOM?"
- "Do we have per-stage timestamps — BootROM, first-stage loader, DDR training, kernel?"
- "Does anything else on that board look different — temperature, current draw, a peripheral that's missing?"

#### 가설 (순위)

| 순위 | 가설 | 맞다면 보이는 것 |
|---|---|---|
| 1 | **부트 미디어가 fallback 속도** (eMMC 고속 모드 tuning 실패 → 하위 모드, QSPI가 1-line 모드) | 이미지 로드 단계만 2–4배; 부트로더 로그에 bus 모드/클럭 |
| 2 | **없는/고장 난 장치 대기 timeout** (I2C 장치 probe 재시도, PCIe 링크 training 대기, PHY autoneg 대기) | 특정 단계에 정확히 고정된 지연 (예: 1.00 s — timeout 상수와 일치) |
| 3 | **DDR training 재시도** (첫 시도 실패 후 재training 성공) | training 단계만 길고 로그에 retry; 그 보드 eye가 좁음 → 마진 경고 |
| 4 | 클럭 소스 fallback (외부 osc 대신 내부 RC, PLL 낮은 설정) / strap이 다른 boot mode | 전 단계가 **균일하게** 느림 (모든 게 2배 → 클럭) |
| 5 | 그 보드에만 다른 환경 (부트로더 env, board-ID 분기, 디버그 로그 레벨) | 설정 diff |

#### 무엇을 재고 계측하나

- **단계별 타임스탬프**: 각 부트 단계 시작에 GPIO 토글(또는 free-running 카운터 값을 공유 메모리/로그에) → scope/LA로 good 보드와 **단계별 diff**. "전체가 2배"인지 "한 단계가 +2 s"인지가 가설 1–5를 바로 가른다.
- 부트로더/커널 로그 타임스탬프(`printk` 시간), eMMC/SD 모드·클럭 로그, I2C probe 결과, PCIe LTSSM 대기 로그.
- 클럭 확인: 클럭 debug 출력 핀 주파수, PLL 설정 레지스터, strap 래치값.
- ABA: 부트 미디어 swap(eMMC 모듈/SD 카드) → 따라가면 미디어, 남으면 보드.

#### 흔한 root cause

- eMMC/SD 고속 모드 tuning 실패 → 레거시 속도 (보드 SI 마진: 그 보드 trace·부품) — **기능은 되니까 아무도 안 봄**, 하지만 같은 마진 문제가 고온에서 부팅 실패로 커짐.
- 빠진 부품/불량 장치를 기다리는 timeout (조립 결함이 "느림"으로만 보임).
- DDR training 재시도 → 그 보드는 **마진이 작은 보드** (나중에 고온 memtest 실패 후보).

#### 고치고 회귀로 굳히기

- 근본 수정은 원인별 (보드 rework, 부품 교체, 마진 → 보드팀).
- **회귀/모니터링**: 팜이 부팅 단계별 시간을 매번 기록하고 **보드별 baseline 대비 이상치 알림** (예: 단계 시간 > median + 5×MAD) — PASS/FAIL만 보는 회귀는 이걸 못 잡는다. 입고 스크린에 "부트 미디어 모드 = 기대 고속 모드" 체크 추가.

<details><summary>🎤 English spoken answer (≈90 s)</summary>

"A board that passes everything but boots twice as slowly is telling me something degraded silently, so I treat it as a bug. The first thing is per-stage timing: I toggle a GPIO or log a free-running counter at each boot stage and compare stage by stage against a good board. If everything is uniformly twice as slow, I suspect the clock — a fallback oscillator, a different PLL setting or a strap. If one stage grows by a fixed amount like exactly one second, it's a timeout waiting for a device that's missing or broken. If it's the image-load stage, the boot media probably fell back to a slower bus mode because high-speed tuning failed, which is a signal-integrity margin on that board. And if DDR training is longer, it's retrying, which flags a low-margin board that I'd expect to fail first at temperature. I confirm with an A-B-A swap of the boot media and a look at the bootloader's bus-mode log. The lasting fix is in the farm: record stage times on every boot and alert on outliers against each board's baseline — a pass/fail-only regression would never have caught this."

</details>

### 3.5 시나리오 N4 · 같은 양을 쟀는데 장비마다 값이 다름

> 겹침: BV11(장비)·BV12(측정)는 장비 사용법. 여기는 **측정끼리 충돌할 때 어느 것을 믿나** — measurement system 자체의 디버그.

#### 증상

- NPU 최대 부하 시 VDD_NPU rail 전류: 보드의 전류 모니터 IC(I2C로 읽음) 평균 8.2 A, bench DMM + shunt 9.1 A, scope + 전류 프로브 피크 14 A / 평균 9.0 A `[예시]`. 전력 예산 문서는 "≤ 9 A". 누구 말이 맞나?

#### 먼저 물어볼 것

- "What exactly is the requirement — average over what window, peak, or RMS?"
- "Where is each instrument measuring — before or after the regulator, on which shunt?"
- "What are the bandwidth, sampling rate and averaging settings of each? When were they calibrated?"
- "Is the workload identical and time-aligned across the measurements?"

#### 가설 (순위)

| 순위 | 가설 | 맞다면 보이는 것 |
|---|---|---|
| 1 | **측정 정의가 다름** (평균 창·피크·RMS, 전류 모니터의 변환시간×averaging) | 같은 신호를 같은 창으로 계산하면 값이 수렴; 모니터 IC는 설정에 따라 한 값이 ms~s 단위 평균 — 예: TI INA226은 변환시간 140 µs–8.244 ms × averaging 1–1024 [8] |
| 2 | **측정 위치가 다름** (레귤레이터 입력 vs 출력, rail 분기 전후) | 전압×전류(전력)로 환산하면 효율만큼 차이 |
| 3 | **대역폭/샘플링 부족 → aliasing** (부하가 µs 단위 버스트인데 느린 ADC가 샘플) | 부하 패턴 주기를 바꾸면 모니터 값이 이상하게 변함 |
| 4 | 교정/설정 오류 (shunt 저항값 설정 틀림, 프로브 degauss·offset 안 함, 온도 계수) | 알려진 부하로 측정 시 고정 비율 오차 |
| 5 | 측정이 대상에 영향 (shunt 삽입으로 전압 강하 → DVFS가 다르게 반응) | shunt 넣으면 성능/클럭이 바뀜 |

#### 무엇을 재고 계측하나

- **알려진 기준으로 각 장비 확인**: 정밀 전자부하(또는 교정된 저항 부하)로 1 A, 5 A, 10 A DC를 흘려 세 장비 비교 → 고정 비율 오차(교정)와 offset 분리.
- **같은 창으로 재계산**: scope 파형(충분한 대역폭·샘플링)을 원본으로 저장 → 모니터 IC의 평균 창과 같은 창으로 평균 계산 → 모니터 값과 비교.
- 측정 위치 그림 (레귤레이터 입력/출력, 분기점) 그리고 전압도 같이 → 전력으로 환산해 비교.
- 반복성: 같은 장비 10회 반복 표준편차 (repeatability) vs 장비 간 차이 (reproducibility) — gauge R&R 개념.

#### 흔한 root cause

- "9 A 이하"의 정의가 없었음 (평균? 1 ms 창 피크? 레귤레이터 설계는 피크·과도 응답이 중요, 열 설계는 평균이 중요) → 요구사항 버그.
- 모니터 IC의 shunt 값/보정 계수가 FW에 잘못 들어감 → SW 버그.
- 프로브 offset/degauss 미실시, 접지 루프 → 측정 셋업 문제.

#### 고치고 회귀로 굳히기

- 요구사항을 **측정 방법까지 포함**해 다시 씀: "VDD_NPU 전류, 레귤레이터 출력, 1 ms 이동평균 최대 ≤ X A, 10 µs 피크 ≤ Y A, 측정 대역폭 ≥ Z".
- 팜 자동화에 **측정 셀프 체크**: 각 캠페인 시작 시 알려진 부하로 측정 장비 확인, 장비 시리얼·교정일을 결과 DB에 기록.

<details><summary>🎤 English spoken answer (≈90 s)</summary>

"When instruments disagree, I debug the measurement before the chip. First I ask what the requirement actually means — average over what window, peak, or RMS — because a current monitor that averages over milliseconds and a scope that sees microsecond bursts can both be right. Then I check where each one measures: input or output of the regulator, before or after a rail splits. Next I calibrate against something known: a precision load at a few DC currents through all three setups tells me whether there's a fixed ratio error, an offset, or a bad shunt value in the firmware. Then I take the scope's raw waveform, with enough bandwidth, and compute the same averaging window the monitor uses; if they agree, the disagreement was definitional, not physical. I also check whether the measurement disturbs the system — a series shunt can drop voltage and change how DVFS behaves. The fix is usually to rewrite the requirement with the measurement method in it, and to make the farm verify its instruments against a known load at the start of every campaign, with instrument serial and calibration date stored with each result."

</details>

### 3.6 시나리오 N5 · debug 빌드에선 통과, release 빌드에선 실패

> 겹침 없음 (기존 노트는 HW/보드 쪽). bare-metal C 검증 FW에서 아주 흔하고, Don의 C 강점을 보여 줄 수 있는 문제.

#### 증상

- `-O0 -g` 빌드에서 `uart_dma_loopback` 1,000/1,000 통과, `-O2` 빌드에서 DMA 완료 대기 중 영원히 멈추거나(약 30%) 데이터 비교 실패 `[예시]`.

#### 먼저 물어볼 것

- "What differs besides the optimization level — asserts, logging, LTO, stack size, linker script?"
- "Does it hang or produce wrong data, and is it deterministic?"
- "Does it pass at -O2 with -g, and at -O1?"
- "Is the failing code polling hardware registers or flags set in an ISR?"

#### 가설 (순위)

| 순위 | 가설 | 왜 release에서만 | 확인 |
|---|---|---|---|
| 1 | **`volatile` 누락**: MMIO 레지스터나 ISR이 바꾸는 플래그를 일반 변수로 polling | -O2가 읽기를 loop 밖으로 끌어내 무한 loop | 디스어셈블리에서 loop 안에 load가 있나; 변수에 `volatile` |
| 2 | **barrier/ordering 누락**: descriptor 쓰기 → doorbell 쓰기 순서를 컴파일러/CPU가 바꿈 | -O0은 우연히 순서대로 + 느림 | `dmb`/`dsb` 또는 kernel식 `wmb()` 추가 시 사라지나 |
| 3 | **UB**: 미초기화 지역 변수, strict aliasing 위반(-O2부터 `-fstrict-aliasing` on [9]), 부호 있는 정수 오버플로, 범위 밖 접근 | 최적화가 UB를 가정하고 코드 제거/재배열 | `-fno-strict-aliasing` 실험, UBSan/ASan을 host 빌드에서, `-Wall -Wextra -Wuninitialized` |
| 4 | **타이밍**: release가 빨라서 HW가 요구하는 대기(레지스터 쓰기 후 N 사이클, 리셋 해제 후 대기)를 못 지킴 | debug 빌드의 느림이 지연 역할 | 해당 지점에 명시적 지연/상태 polling 추가 시 사라지나; 데이터시트 요구 시간 |
| 5 | **assert/로그에 side effect** (`assert(init_dma() == 0)`), 디버그 printf가 barrier 역할 | NDEBUG로 호출 자체가 사라짐 | assert 안 코드 grep |
| 6 | 스택/메모리 배치 차이 (inlining으로 스택 증가, 링커 배치 바뀌어 DMA 버퍼가 캐시 라인 공유) | 배치가 바뀌어 잠재 버그 노출 | map 파일 diff, 스택 페인팅(0xDEADBEEF 채우고 high-water 측정) |

#### 무엇을 재고 계측하나

- **차이를 하나씩 좁히기**: -O0 → -O1 → -O2; `-O2 -g`로 디버깅 가능한 release; 파일별 최적화 레벨 bisect (실패 파일 찾기) — 빌드 축 bisect의 변형.
- 멈춘 지점에서 JTAG halt → PC 확인 → 디스어셈블리: polling loop에 load 명령이 있는지.
- host에서 같은 로직을 `-fsanitize=address,undefined`로 (HW 접근은 stub) — Don의 N05 C 드릴 빌드 방식 그대로.
- HW 쪽 증거: LA로 doorbell 쓰기 시점 vs descriptor 쓰기 시점 (버스 트레이스가 있으면 순서 확인).

#### 흔한 root cause

- MMIO 포인터에 `volatile` 없음 / ISR 공유 플래그 `volatile` 없음 (가장 흔함).
- descriptor OWN 비트 쓰기 전에 barrier 없음 (deck §7 #1과 연결 — 여기선 **컴파일러 재배열**이 트리거).
- HW 요구 대기 시간을 "코드가 느려서" 지키고 있었음 → 명시적 대기·상태 polling으로.

#### 고치고 회귀로 굳히기

- 레지스터 접근을 **HAL 매크로/inline 함수로 통일** (`readl`/`writel`류: volatile + 필요 시 barrier) → 직접 포인터 접근 금지 (lint 규칙).
- CI가 **debug와 release 둘 다** 빌드하고, 실리콘/FPGA 회귀는 **출하 설정(release)**으로 돌림. host 단위 테스트는 sanitizer로.
- 해당 HW 대기 요구사항을 데이터시트/IP 문서 근거와 함께 HAL에 명시.

<details><summary>🎤 English spoken answer (≈90 s)</summary>

"When a test passes at -O0 and fails at -O2, I assume the code was relying on something the compiler is allowed to change. I narrow it down like any bisect: -O1 versus -O2, then per-file optimization levels to find the file, and I build -O2 with debug info so I can halt it with JTAG. If it hangs in a polling loop, I look at the disassembly — if there's no load inside the loop, the register or ISR flag isn't volatile, and the compiler hoisted the read. If it produces wrong data, I check ordering: a descriptor written after the doorbell because there's no barrier, which -O0 happened to hide. Then undefined behavior — uninitialized locals, strict aliasing violations, which GCC starts exploiting at -O2 — so I compile the same logic on the host with the address and undefined-behavior sanitizers. And timing: sometimes the debug build was slow enough to satisfy a hardware wait requirement nobody wrote down. The prevention is structural: all register access goes through one HAL accessor with volatile and barriers, CI builds both configurations, and the silicon regression always runs the release configuration we actually ship."

</details>

### 3.7 시나리오 N6 · 동시 실행할 때만 데이터 손상

> 겹침: deck §7 #1(DMA 데이터 가끔 틀림 → cache)·LP15 S6(Arm에서만 DMA 손상)·deck #16(sensor ring)은 **단일 경로**의 coherency/ordering. 여기는 **여러 master(ISP·NPU·DMA·CPU)를 동시에 돌릴 때만** — 동시성 매트릭스와 **자기 기술(self-describing) 데이터 패턴**이 핵심.

#### 증상

- ISP 단독, NPU 단독, DMA memcpy 단독은 각각 24시간 무오류. 셋을 동시에 돌리면 2–3시간마다 DMA 목적지 버퍼에서 64 B 한 덩어리가 틀림 `[예시]`.

#### 먼저 물어볼 것

- "What is the corruption's shape — size, alignment, and what the wrong data looks like: zeros, stale data, or somebody else's data?"
- "Is it every pair of masters or only one combination?"
- "Are any buffers shared or adjacent — same cache line, same page?"
- "Is DMA coherent with the CPU caches here, and is the IOMMU/SMMU on?"
- "Does it depend on DDR frequency or voltage?"

#### 가설 (순위)

| 순위 | 가설 | 손상 모양이 이렇다면 | 가르는 실험 |
|---|---|---|---|
| 1 | **버퍼 겹침/할당 버그** (두 master가 같은 영역을 받음, 크기 계산 오류) | **다른 master가 쓴 데이터**가 보임 | 자기 기술 패턴(아래)로 "누가 썼나" 바로 확인; 할당 로그 |
| 2 | **캐시 라인 공유** (DMA 버퍼와 CPU 데이터가 같은 라인, 비coherent DMA) | 64 B(캐시 라인) 단위, **오래된(stale) 데이터** | 버퍼를 캐시 라인 정렬·패딩 (커널 DMA API 문서도 같은 요구 [3]) → 사라지나 |
| 3 | **SW 동시성 버그** (드라이버 공유 상태에 lock/atomic 없음, descriptor ring 인덱스 race) | descriptor 단위, 순서 꼬임 | 코어 1개로 제한 / lock 추가 / 로그 시퀀스 번호 |
| 4 | **IOMMU/SMMU TLB 무효화 race** (매핑 해제 후 다른 master가 옛 매핑으로 씀) | 페이지(4 KiB) 단위, 해제된 버퍼 주소 | 무효화 후 sync 확인; IOMMU bypass로 재현 여부 |
| 5 | **interconnect/메모리 컨트롤러 HW 버그** (특정 QoS·bank 충돌·write 병합 조합) | 특정 주소 비트·bank 패턴, SW 바꿔도 남음 | 최소 bare-metal 트래픽 생성기 조합으로 재현; DDR 주파수 의존; DV에 트래픽 패턴 전달 |
| 6 | **전원 droop** (동시 최대 부하 → rail 강하 → 마진) | 랜덤, 전압 내리면 악화 | rail scope 트리거, 전압 +3% |

#### 무엇을 재고 계측하나

- **자기 기술 데이터 패턴**: 각 producer가 쓰는 모든 64 B 블록에 `[producer ID][buffer ID][sequence #][address][CRC]`를 넣는다. 손상된 블록을 읽으면 **"누가, 언제, 어디에 쓰려던 데이터인가"**가 바로 나온다 — 다른 master의 데이터면 가설 1/4, 같은 producer의 이전 sequence면 stale(가설 2), 쓰레기면 5/6.
- **동시성 매트릭스**: master 쌍(ISP+NPU, ISP+DMA, NPU+DMA)과 셋 모두 → 실패가 어떤 **쌍**에서 나오는지. 그다음 그 쌍의 트래픽 강도(대역폭)를 올려 빈도 증가 (stress).
- 손상 주소 분포: 정렬(64 B? 4 KiB?), 주소 비트 패턴(bank/row 비트), 시간(트래픽 버스트와 상관).
- 실패 감지 즉시 GPIO → LA/버스 트레이스 freeze, interconnect 에러/timeout 레지스터, IOMMU fault 레지스터, 메모리 컨트롤러 ECC/에러 카운터.
- 최소 SW로 축소: Linux 드라이버 → bare-metal 트래픽 생성기 (DMA·NPU에 합성 job) → 같은 손상이 나면 HW 쪽.

#### 흔한 root cause

- 캐시 라인 공유/정렬 (stale 64 B) — 가장 흔함, SW 수정.
- 드라이버 race (동시 job 제출 시 ring 인덱스).
- IOMMU 무효화 순서.
- 드물게 진짜 interconnect/메모리 컨트롤러 버그 → errata + QoS 설정 WA.

#### 고치고 회귀로 굳히기

- 원인별 수정 + **동시성 soak 테스트를 회귀에**: 모든 master 조합 × 자기 기술 패턴 × 최대 대역폭, nightly 4시간 + 주간 48시간 (실패 간격 2–3시간이면 "고쳤다"는 0 실패로 최소 ≈ 3 × 3 h ≈ 9시간 이상, 여러 보드로 나눠서).
- 버퍼 할당기에 정렬·guard 영역 + 해제 후 poison (0x6B 등)으로 use-after-free 즉시 탐지.

<details><summary>🎤 English spoken answer (≈2 min)</summary>

"Corruption that only shows up under concurrency means an interaction, so I want the corrupted data to tell me who wrote it. I'd switch every producer to a self-describing pattern: each 64-byte block carries the producer ID, buffer ID, a sequence number, its intended address and a CRC. When a block is wrong, I read it and immediately know whether it's another master's data — an allocation overlap or a stale IOMMU mapping — the same producer's older data, which means a stale cache line, or garbage, which points at hardware or power. In parallel I run a concurrency matrix — each pair of masters and then all three — to find the smallest combination that fails, and I push that pair's bandwidth up to make it fail more often. I look at the shape: 64-byte aligned suggests cache lines, 4 KiB suggests pages, a fixed address-bit pattern suggests the memory controller. Then I shrink the software: if a bare-metal traffic generator on the same masters reproduces it, it's hardware and I hand the traffic pattern to DV; if not, it's our driver or cache maintenance. Whatever the fix, a multi-master soak with the tagged patterns stays in the nightly and weekly regression, run across several boards because one failure every few hours needs many hours of clean running to prove a fix."

</details>

## 4. 꼬리 질문 체인

### 체인 A — "A test fails 1 in 1000 on silicon. Walk me through it."

<details><summary>답 펼치기</summary>

- **Q.** — "I start by turning it into a reliable number and a self-documenting failure: loop it on the farm, define the failure signature, and capture state at the moment of failure with a GPIO trigger, counter snapshots and a frozen trace. Then I correlate across chips, boards, builds and V/F/T to see what the failure follows, and confirm with a swap or bisect. I stress the suspected axis to raise the rate, test hypotheses that make different predictions, and finish with a statistical proof and a regression test." · 한국어: 숫자·증거 → 무엇을 따라가나 → stress → 가설 → 통계 증명 → 회귀.
- **꼬리1. How many runs before you say it's fixed?** — "Zero failures in about three over p runs gives ninety-five percent confidence the rate is below p — so roughly three thousand clean runs for one in a thousand; exactly 2,995. I split them across several chips and boards so I'm not proving it for one part only." · 한국어: 3/p, 여러 칩·보드로 나눠서.
- **꼬리2. You can't afford three thousand runs per experiment.** — "Then I find a stress that multiplies the failure rate — cold, low voltage, max traffic — as long as the signature stays the same. Experiments run at the stressed condition, and only the final proof runs at nominal, plus a long soak." · 한국어: stress로 p를 올려 실험, 최종 증명만 정상 조건.
- **꼬리3. After the fix it's one in five thousand, not zero. Ship?** — "Not as 'fixed'. Either there's a second cause or the fix only narrowed a window. I'd compare signatures of the remaining failures; if it's the same signature, the root cause isn't fully understood. If it's a different one, it's a new bug with its own severity decision — and that decision belongs with product, with the data in front of them." · 한국어: 줄었음 ≠ 없어짐; signature로 같은 버그인지 판단.

</details>

### 체인 B — "How do you decide if it's hardware, software, the board, or the test?"

<details><summary>답 펼치기</summary>

- **Q.** — "I ask what the failure follows. If it follows the chip in a swap, it's silicon; if it stays with the board, it's the board; if it starts at a build, it's software; and if a second checker or a negative control disagrees, it's the test. I start with the cheap checks — the test and the farm slot — and end with the expensive ones like chip swaps and RTL simulation." · 한국어: 칩/보드/빌드/체커 중 무엇을 따라가나, 싼 것부터.
- **꼬리1. How would you know the test itself is wrong?** — "I verify the checker independently: a negative control that must fail, a second golden — for example a CPU reference instead of the C model — and I check the test's own history; if the first bad commit is in the test repo, it's the test or the test just became strict enough to catch a real bug." · 한국어: negative control, 두 번째 golden, 테스트 리포 bisect.
- **꼬리2. It's A0 — there's no previous stepping to compare.** — "Then FPGA is my previous stepping: the same test binary on the FPGA image. If it fails there too, it's logic — RTL or software. If FPGA passes and silicon fails, it's timing, analog, power or physical, and a V/F/T sweep tells me which. I can also compare against an earlier chip that reused the same IP." · 한국어: A0에선 FPGA 재현이 이전 stepping 역할.
- **꼬리3. FPGA passes, silicon fails, no V/F dependence at all.** — "Then I look at what FPGA doesn't model: reset values and power-on state, memory initialization, real PHYs, clock-domain crossings that FPGA clocks happen to line up, and blocks that are stubbed on the FPGA. I diff register dumps between the two platforms right after init — a different starting state is common." · 한국어: FPGA가 모델링 안 하는 것 — 초기값·CDC·stub; 초기화 직후 덤프 diff.

</details>

### 체인 C — "The design team says it's not a silicon bug. What do you do?"

<details><summary>답 펼치기</summary>

- **Q.** — "I'd rather agree on the experiment than argue the conclusion. I'd ask them what evidence would convince them — for example, the failure reproducing with a ten-line bare-metal sequence on three chips within spec conditions — and then I go produce exactly that." · 한국어: 결론 전에 판정 기준 합의.
- **꼬리1. What do you bring them?** — "A minimal reproduction they can replay in simulation, the register sequence, the state at failure, raw waveforms, and a ruled-out list with evidence. Their time is best spent on a five-line repro, not my full system log." · 한국어: RTL sim에서 재생 가능한 최소 재현.
- **꼬리2. They reproduce it in simulation — now what?** — "Now it's an erratum candidate, and my job shifts to the workaround: evaluate candidates from cheapest — sequence change, chicken bit, fuse — measure each one's effectiveness statistically and its cost, and make sure firmware applies it by default. And I push for a fix decision in the next stepping with data on frequency and impact." · 한국어: WA 평가·기본 적용·다음 stepping 결정 근거.
- **꼬리3. And if it turns out to be your team's bug?** — "I say so in the same channel where I raised it, add the negative control or check that would have caught it, and move on. That's what makes the next escalation credible." · 한국어: 같은 채널에서 인정 + 회귀.

</details>

### 체인 D — "Walk me through the lifecycle of an erratum."

<details><summary>답 펼치기</summary>

- **Q.** — "Discovery and triage by validation, root cause confirmed by design and DV, a workaround designed and statistically verified, documentation with conditions, implication, workaround and affected steppings, a decision on fixing it in the next stepping, and finally verification on the new stepping." · 한국어: 발견 → RC → WA → 검증 → 문서 → stepping 수정 → 수정 검증.
- **꼬리1. How do you verify a workaround?** — "Same loop and conditions that exposed the bug, zero failures for about three over p runs across several chips, plus a regression run to show the workaround doesn't break anything else, and a measurement of its cost — performance, power, latency." · 한국어: 효과(3/p)와 부작용과 비용.
- **꼬리2. How do you make sure B0 actually fixed it?** — "I keep two tests in the regression: workaround on, expected to pass everywhere, and workaround off, expected to fail on A0 and pass on B0. Without the workaround-off test, nobody would notice if B0 still needed it — or if the workaround silently disappeared from firmware." · 한국어: WA-on/WA-off 두 테스트.

</details>

### 체인 E — "Your regression broke somewhere in the last 100 builds and the test is flaky. How do you find it?"

<details><summary>답 펼치기</summary>

- **Q.** — "First I rerun the last good build on today's farm to rule out hardware or infrastructure changes. Then I measure the failure rate on a bad build, because that sets how many reruns each bisect step needs. With git bisect run, my check script exits zero only after enough clean runs, a non-zero code on a failure with the matching signature, and 125 to skip builds that can't be built." · 한국어: 옛 good 빌드 재확인 → p 측정 → 단계당 재실행 수 → 자동 판정.
- **꼬리1. How many reruns per step?** — "For seven steps and ninety-five percent overall confidence, each step needs a miss probability under about 0.7 percent; at a five percent failure rate that's ninety-six runs per step, stopping early on the first failure. In my simulation one run per step found the right build one percent of the time." · 한국어: p=0.05 → 96회, 1회면 정답률 1%.
- **꼬리2. Good builds also fail occasionally for unrelated reasons.** — "Then I only count failures with the regression's signature; in my simulation, counting any failure dropped accuracy from about 98 to 23 percent with a one-in-two-hundred background flake." · 한국어: signature 필터 없으면 23%.

</details>

## 5. Don 경험 연결

- "At SK hynix I automated chamber-based reliability testing, so I'm used to turning an intermittent interface failure into a rate with conditions — [숫자 채우기: chambers / DUTs / cycles per campaign] — and using shmoo plots to separate a margin problem from a functional one."
- "During pre-silicon FPGA bring-up at Solidigm, the most useful thing I learned was to hand the RTL team a minimal register sequence they could replay in simulation; that's how [숫자 채우기: N] bugs got fixed before RTL freeze instead of being argued about."
- "At Apple I root-cause PCIe, I2C, SPMI and RFFE issues on new chipsets with a DSO and protocol analyzers, where the first question is always whether the wire shows a correct request with a wrong response — that separates our software from the other chip. [숫자 채우기: one concrete case — symptom, tool, conclusion]"
- "I also built factory test nodes aimed at latent defects, which taught me that a fix isn't real until the stress that exposed it runs in the regression [숫자 채우기: escape rate before/after if shareable]."

## 6. 실습 / 코드

### 6.1 flaky 테스트 bisect 시뮬레이터 — 실제 실행 [확인됨]

- 파일: [flaky_bisect_sim.py](onsitePrep/n06/h/flaky_bisect_sim.py) · [run.sh](onsitePrep/n06/h/run.sh) · 출력 [run_output.txt](onsitePrep/n06/h/run_output.txt). Python stdlib만, `python3 -I`, seed 고정.
- 모델: 빌드 128개, first-bad k는 랜덤. bad 빌드는 회당 p_bad로 실패(signature `SIG_REGRESSION`), good 빌드도 p_noise로 무관한 flake(`SIG_NOISE`). 단계마다 최대 r회 실행, 첫 실패에서 중단(early stop) → BAD, r회 무실패 → GOOD.
- 실행: `onsitePrep/n06/h/run.sh` (2026-11-01, macOS)

```text
=== N06H flaky bisect simulator: N=128 builds, bisect steps=7 ===

[1] Analytic reruns per step for 95% overall confidence (worst case: every step on a bad build)
   p_fail(bad build)  r/step  naive r=1 detect/step  1/p rule of thumb
                0.50       8                   0.50                  2
                0.20      23                   0.20                  5
                0.10      47                   0.10                 10
                0.05      96                   0.05                 20
                0.01     490                   0.01                100

[2] Monte Carlo, p_bad=0.05 (1-in-20), no noise, 4000 bisects each
  naive: 1 run per step                        r=1    correct=  1.00%  mean_runs=     7.0  mean_|offset|=59.32
  1/p rule of thumb (20 runs)                  r=20   correct= 24.82%  mean_runs=   104.9  mean_|offset|=17.03
  3/p rule of three (60 runs)                  r=60   correct= 84.97%  mean_runs=   267.5  mean_|offset|= 1.60
  analytic 95% (r=96), early stop on fail      r=96   correct= 97.78%  mean_runs=   405.8  mean_|offset|= 0.19

[3] Same, but good builds also flake: p_noise=0.005 (1-in-200, different signature)
  r=96, count ANY failure as bad               r=96   correct= 22.82%  mean_runs=   416.1  mean_|offset|=18.50
  r=96, count only matching signature          r=96   correct= 97.53%  mean_runs=   404.2  mean_|offset|= 0.24

[4] Stress first: if a stress condition raises p_bad 0.05 -> 0.30
  stressed, analytic 95% (r=14)                r=14   correct= 97.92%  mean_runs=    60.4  mean_|offset|= 0.20

[5] Proving the fix: clean runs needed so that p_new < p_old at 95% (rule of three)
  p_old=0.05   ->    59 clean runs   (3/p approx =      60)
  p_old=0.01   ->   299 clean runs   (3/p approx =     300)
  p_old=0.001  ->  2995 clean runs   (3/p approx =    3000)
  sanity: fix that does NOTHING (p stays 0.05) still passes 59 clean runs in 4.94% of 20000 tries (expect ~5%)

[6] Rate estimate with failures seen: 95% one-sided upper bound (Poisson approx)
   0 fails /  3000 runs -> point=0.00000  upper95=0.00100  (lambda_upper=3.00)
   1 fails /  3000 runs -> point=0.00033  upper95=0.00158  (lambda_upper=4.74)
   3 fails /  3000 runs -> point=0.00100  upper95=0.00258  (lambda_upper=7.75)
   3 fails /  1000 runs -> point=0.00300  upper95=0.00775  (lambda_upper=7.75)
  10 fails / 10000 runs -> point=0.00100  upper95=0.00170  (lambda_upper=16.96)
```

### 6.2 출력 읽는 법 (면접에서 말할 숫자)

- **[2]** 1회씩 bisect하면 정답률 1%, 평균 59 빌드 빗나감. "1/p번이면 되겠지"(20회)도 25%. rule of three(60회)도 85% — 단계가 7개라 오류가 **곱해지기** 때문. 분석식 96회에서 97.8% (모든 단계가 bad 빌드에 걸리는 최악 가정이라 실제로는 95%보다 높게 나옴).
- **[3]** good 빌드의 1-in-200 무관한 flake를 "bad"로 세면 정답률 23%로 붕괴 → **signature 필터**로 97.5% 복구. 판정 스크립트에서 signature 비교가 왜 필수인지의 숫자.
- **[4]** stress로 p를 0.05 → 0.30으로 올리면 단계당 14회, 총 평균 60회로 같은 정확도 (≈ 7배 절약). "stress first"의 숫자.
- **[5]** 고친 뒤 증명 횟수 = ln(0.05)/ln(1−p) — 3/p와 거의 같음. 아무 효과 없는 fix도 59회 무실패를 4.94% 확률로 통과 = 정확히 "5% 위험"의 의미.
- **[6]** 실패가 보였을 때의 상한: 3/3,000이면 "0.1%"가 아니라 "최대 0.26%까지 가능"이라고 보고.
- 한계 `[추정]`: 독립 시행 가정(실제로는 보드·칩·시간 상관이 있음), bad 빌드 실패율이 모든 bad 빌드에서 같다고 가정, good 빌드의 회귀 signature 오탐 0 가정. 실전 개선: 단계마다 bad 판정은 1회 실패로 확정(이미 early stop), good 판정에만 많은 횟수를 쓰는 **비대칭** 구조 — 위 시뮬레이션이 이미 그렇게 함.

## 7. 자주 틀리는 것

- **가설부터 말하기** — "타이밍 문제 같아요"로 시작하면 감점 [추정]. 재현률·signature·무엇을 따라가나가 먼저.
- **"10번 돌렸는데 괜찮아요"** — 1-in-1000은 10번으로 아무것도 증명 못 함. 3/p (2,995회)를 말할 수 있어야 함.
- **flaky 테스트를 1회씩 bisect** — 정답률 1% (§6). 단계당 재실행 수를 p에서 계산.
- **signature 없이 "실패"를 하나로 셈** — 무관한 flake가 bisect·상관분석·fix 증명을 모두 오염 (§6 [3]: 98% → 23%).
- **test/체커/측정 장비를 의심하지 않음** — 판단표의 4번째 열. negative control 없는 테스트는 "통과"도 증거가 아님.
- **"칩을 따라간다 = silicon 버그"** — corner 마진일 수 있음; spec 내 조건 재현 + RTL sim 재현이 있어야 설계 버그.
- **고온만 최악이라고 가정** — 저전압 미세공정은 저온이 느린 코너일 수 있음(temperature inversion [6][7]). 온도 스윕은 양방향.
- **stress로 다른 버그를 키움** — 빈도가 올라도 signature가 다르면 다른 문제.
- **errata를 문서로만 끝냄** — WA가 SDK 기본값이 아니고, WA-off 테스트가 없으면 B0 수정도 WA 누락도 못 봄.
- **PASS/FAIL만 보는 회귀** — 부팅 2배 느림·마진 감소 같은 "통과하는 이상치"를 못 잡음. 시간·카운터·eye 폭 같은 연속값도 트렌드로.

## 8. 출처

1. git-scm.com — git-bisect documentation (bisect run exit codes 0 / 1–127 except 125 / 125 = skip; old/new terms): https://git-scm.com/docs/git-bisect
2. Wikipedia — Rule of three (statistics) (0 events in n trials → 95% upper bound ≈ 3/n; general −ln(α)/n): https://en.wikipedia.org/wiki/Rule_of_three_(statistics)
3. Linux kernel docs — Dynamic DMA mapping Guide (DMA buffers should not share cache lines with other data on DMA-incoherent caches; stack/image addresses not valid for DMA): https://docs.kernel.org/core-api/dma-api-howto.html
4. GCC — Options That Control Optimization (`-fstrict-aliasing` enabled at -O2, -O3, -Os): https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
5. USC (Pedram group), ISLPED'14 — FinFET temperature effect inversion (worst-case delay at low temperature): https://coldflux.usc.edu/~massoud/Papers/DTM-FinFET-TempEffeInv-islped14.pdf
6. Embedded.com — temperature inversion and worst-case timing corners (worst setup corner cold at low supply): https://www.embedded.com/?p=4231192
7. Design-Reuse / Moortec blog — worst-case conditions, high-Vt vs low-Vt temperature behavior: https://us.design-reuse.com/industryexpertblogs/48251/moortec-worst-case-conditions-performance.html
8. TI INA226 datasheet SBOS547B (conversion time 140 µs–8.244 ms, averaging up to 1024): https://www.ti.com/document-viewer/INA226/datasheet/GUID-1B4807CB-4D77-4AF3-9180-6F2B7D584658
9. 같은 GCC 문서 [4] (최적화 레벨별 활성 옵션) — debug/release 차이 근거.

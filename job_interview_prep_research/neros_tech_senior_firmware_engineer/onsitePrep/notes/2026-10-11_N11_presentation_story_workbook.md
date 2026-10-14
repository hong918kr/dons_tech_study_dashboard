# 🎤 N11 · 1시간 발표 스토리 워크북 — 채우고, 줄이고, 리허설하기

> 발표 주제 [확인됨 Don 10-11]: **"내가 무엇을 했고, 지금 무슨 일을 하는지 소개 + 어떤 부분이 challenging problem이었나"**. 원칙과 슬라이드 뼈대는 [N03 발표 가이드](2026-10-01_N03_presentation_1hour.md)에 있다. 이 노트는 **실제로 채워 넣는 워크북**이다: 스토리 3개를 표로 채우고 → 2분 · 5분 · 15분 버전으로 말해 보고 → 슬라이드에 옮기고 → 리허설 기록을 남긴다. `(Don: …)` 칸은 레쥬메에 없는 실제 디테일이라 Don만 채울 수 있다. **지어내지 않는다.** 기밀은 [N03 5절](2026-10-01_N03_presentation_1hour.md) 규칙으로 일반화.

## 0. 이 발표가 사실상 결정하는 것

| 듣는 사람 [추정] | 이 발표에서 확인하려는 것 |
|---|---|
| Adam (Director of FW) | 레벨 — 시니어로서 **판단 · 리더십 · 깊이** |
| Michael (Test lead) | 테스트 인프라를 세울 사람인가 — **측정 · 재현 · 재발 방지** |
| Platform · FW 엔지니어 | 진짜 펌웨어를 써 본 사람인가 — **코드 · 레지스터 · 버스 수준** |
| 전원 | 설명을 잘하는가, 질문에 정직한가 |

> 그리고 Don에게는 **개발자급 깊이를 보여 줄 유일한 무대**다 — 오퍼 단계에서 레벨 · 범위를 이야기할 근거가 여기서 쌓인다 ([컨텍스트 파일 §5 10-03 전략](../../neros_tech_senior_firmware_engineer_context.md)).

## 1. 시간 배분 (리크루터에게 확인 전 기본값)

| 분 | 파트 | 내용 |
|---|---|---|
| 0–3 | 오프닝 | 한 문장 정체성 + 오늘 순서 |
| 3–12 | **Part 1 · 내가 해 온 일** | 경력 지도 (SK hynix → Solidigm → Apple), 지금 하는 일 |
| 12–32 | **Part 2 · 가장 어려웠던 문제 (메인 스토리)** | 증상 → 가설 → 측정 → 원인 → 수정 → **재발 방지 테스트** |
| 32–40 | Part 3 · 짧은 스토리 1~2개 | 테스트 인프라(플랫폼 · SDK) / bring-up |
| 40–45 | **Part 4 · Neros에서** | factory test FW · HIL · 테스트를 펌웨어 팀의 도구로 |
| 45–60 | Q&A | 7절 예상 질문 |

- 확인할 것 (Devin 콜): 발표 길이에 Q&A가 포함인지, 화면 공유 · 슬라이드 형식, 패널이 누구인지 → [P01 2절](../plan/2026-10-11_P01_onsite_10day_plan.md)

## 2. 오프닝 한 문장 (영어, 외워 둔다)

> "I'm a firmware engineer who works where silicon meets the system — I've written production firmware for SSD controllers, root-caused failures when new chips meet the full product, and built test infrastructure other engineers relied on. Today I'll walk you through what I've done, one problem I'm proud of solving, and what I'd build here."

## 3. Part 1 · 경력 지도 — 레쥬메 범위 그대로

| 시기 | 회사 · 역할 | 한 줄 (레쥬메 근거) | 발표에서 강조 | (Don: 보탤 디테일) |
|---|---|---|---|---|
| 2018-10 ~ 2021-05 | SK hynix · App SW | 챔버 테스트 자동화 **web 플랫폼 + SDK** (온도 · 스케줄링 · eSSD 상태 · UART 시퀀스) | **다른 엔지니어가 매일 쓴 테스트 인프라** | (Don: 사용자 수 · 플랫폼 스택) |
| 2021-05 ~ 2022-07 | SK hynix · Senior FW | NVMe **telemetry 디버그 기능** (MS · Dell · HPE), NAND 데이터패스 FW, reliability system, JTAG · 스코프 · LA | 고객이 쓰는 디버그 기능 · 측정 | (Don: telemetry가 잡아낸 실제 사례 하나) |
| 2022-07 ~ 2025-11 | Solidigm · Staff FW | **PCIe 6 차세대 production FW**, FPGA bring-up (Cortex-R8/R82/M0+), SoC verification (I2C · SPI · DMA · PCIe), error reporting/handling scheme | **production FW를 직접 쓴 사람** | (Don: 맡은 모듈 · 팀 규모) |
| 2025-12 ~ | Apple · RF-HW Chipset Integration | 새 실리콘 통합 루트코즈 (PCIe · I2C · SPMI · RFFE), bring-up → NPI → MP, **factory test-node 아키텍처**, stress 기반 latent defect | **시스템에서만 터지는 버그 + 공장 테스트** | (Don: 일반화 가능한 범위) |

> Part 1은 9분 안에 끝낸다 — 각 시기에서 **한 가지씩만**. 나머지는 질문으로 나오게 둔다.

## 4. Part 2 · 메인 스토리 — 후보 고르기

[N03 4절](2026-10-01_N03_presentation_1hour.md)의 후보 A~D 중 **하나를 메인**으로 고른다. 고르는 기준 (위에서부터 중요):

- [ ] Don이 **직접** 원인을 찾았다
- [ ] 간헐적 · 재현이 어려웠다
- [ ] HW · FW · 칩 · 여러 팀이 얽혔다
- [ ] 측정 도구(스코프 · protocol analyzer · Trace32)로 증거를 잡았다
- [ ] 결말이 있다: 수정 + **재발 방지 테스트**
- [ ] 기밀을 일반화해도 이야기가 선다

| 후보 | 출처 | Neros와의 연결 | 고르면 |
|---|---|---|---|
| A. 새 칩의 시스템 레벨 인터페이스 실패 | Apple (PCIe/I2C/SPMI/RFFE) | "비행에서만 재현되는 버그" (Flight SW 공고 문구)와 같은 성격 | **추천 메인** (가장 최근 · 크로스팀) |
| B. FPGA 단계 FW vs RTL 버그 가리기 | Solidigm | bring-up · 디버깅 둘 다 | 보조 또는 Part 3 |
| C. 고객 qualification에서 터진 문제 | SK hynix · Solidigm (telemetry · error handling) | 필드 데이터로 루트코즈 | 보조 |
| D. 고속 인터페이스 마진 (shmoo) | SK hynix | 측정 · 시각 자료 | 보조 |

결정: 메인 = ______ · 보조 = ______ (Don)

## 5. 메인 스토리 채우기 — 이 표가 슬라이드 10장이 된다

| 칸 | 질문 | (Don) |
|---|---|---|
| 상황 | 어떤 제품 · 어떤 단계(bring-up / NPI / MP)? 내 역할은? | |
| 증상 | 한 문장. 얼마나 자주? 어디서만? | |
| 왜 어려웠나 | 간헐성 · 재현 · 여러 팀 · 관측 불가 중 무엇? | |
| 처음 가설 3개 | 각각을 어떻게 배제했나 | |
| 재현율을 높인 방법 | stress · 온도 · 전압 · 반복 · 스크립트 | |
| 결정적 증거 | 어떤 도구로 무엇을 봤나 (파형 · 트레이스 · 로그) | |
| 원인 | 일반화된 표현으로 | |
| 수정 | 무엇을 바꿨나 · 누구와 합의했나 | |
| **재발 방지** | 어떤 테스트 · 체크를 추가했나 (테스트 트랙의 핵심 장면) | |
| 결과 · 숫자 | 기간 · 영향 범위 · 이후 재발 여부 | |
| 배운 것 | 다음 프로젝트에서 바꾼 방식 | |

슬라이드 매핑 (Part 2, 20분):

| # | 슬라이드 | 칸 |
|---|---|---|
| 1 | 상황과 내 역할 | 상황 |
| 2 | 증상 (그림 · 파형 하나) | 증상 |
| 3 | 왜 어려웠나 | 왜 어려웠나 |
| 4 | 가설 트리 (3갈래) | 처음 가설 3개 |
| 5 | 재현 리그 · 스크립트 | 재현율을 높인 방법 |
| 6 | 결정적 증거 | 결정적 증거 |
| 7 | 원인 (블록도 위에 표시) | 원인 |
| 8 | 수정과 합의 | 수정 |
| 9 | **재발 방지 테스트** | 재발 방지 |
| 10 | 결과와 배운 것 | 결과 · 배운 것 |

## 6. 같은 스토리를 세 길이로 — 소리 내어 연습

| 버전 | 언제 쓰나 | 구조 |
|---|---|---|
| **2분** | 1:1에서 "hardest bug?"가 또 나올 때 | 증상 1문장 → 왜 어려웠나 1문장 → 결정적 증거 → 원인 → 추가한 테스트 |
| **5분** | 발표 Q&A에서 "다른 예는?" | 2분 버전 + 가설 3개 + 재현 방법 |
| **15~20분** | 발표 Part 2 | 5절 슬라이드 10장 |

2분 버전 틀 (영어로 채우기):

> "The symptom was __. It was hard because __. I built __ to reproduce it __ times more often, and the key evidence was __ on the __. The root cause turned out to be __. We fixed it by __, and I added __ so it can't come back silently."

## 7. 발표 중 · 끝 예상 질문 — 답의 고리

| 질문 | 답의 고리 |
|---|---|
| "What exactly did *you* do vs the team?" | 내가 한 행동을 동사로 (측정했다 · 스크립트를 짰다 · 가설을 배제했다) |
| "How did you know it was fixed?" | 재현 리그에서 N회 무재현 + 추가한 테스트가 회귀를 막음 |
| "What would you do differently?" | 더 일찍 재현 자동화 / 텔레메트리를 먼저 심었으면 |
| "How did you prioritize hypotheses?" | 측정 비용이 싸고 가능성 높은 것부터, 한 번에 하나씩 |
| "How does this apply to drones?" | 비행에서만 재현되는 버그 = 시스템 레벨 문제 → 로그 · 텔레메트리 · HIL로 재현 |
| "You haven't used Betaflight/PX4." | 정직하게 + "FW를 읽고 테스트 인프라를 세우는 건 스택과 무관하게 가져올 수 있다" |
| "How would you set up factory test here?" | [N08](2026-10-01_N08_factory_test_firmware.md) · [S07](../../technicalScreenPrep/site/design/2026-10-03_S07_manufacturing_factory_test.html) — 스테이션 분리 · 측정값 기록 · golden unit |
| "Why test instead of platform?" | (트랙 전략) 테스트 인프라를 펌웨어를 아는 사람이 세워야 한다는 메시지로 — 레벨 이야기는 하지 않는다 |

## 8. Part 4 · Neros에서 (5분) — 들은 사실만으로

| 들은 사실 | 연결 |
|---|---|
| factory test FW가 없다 · 필요하다 [확인됨 10-01 Michael] | "Day 1 priority: factory test firmware and station flow" |
| 테스트 FW가 가장 급하다 · 연내 최대 5명 [확인됨 10-01 Michael] | "Build the shared driver/fixture layer so each new test engineer is productive fast" |
| FW 17 · Test 3 · full stack [확인됨 10-08] | "Test infrastructure that firmware engineers also use — host tests + HIL in CI" |
| STM32 사용 [확인됨 10-01] | "HAL seams so modules run on the host and on the rig" |

> 마지막 슬라이드 문장: **"At Neros, I'd build the factory test firmware and HIL infrastructure that lets 17 firmware engineers ship to 1,000+ drones a week with confidence."**

## 9. 리허설 기록

| 회차 | 날짜 | 길이 | 잘된 것 | 고칠 것 |
|---|---|---|---|---|
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |

- 1회차: 혼자, 녹음. 2회차: 슬라이드 넘기며 타이머. 3회차: 누군가 앞에서 + 질문 받기
- 기준: Part 2가 20분을 넘지 않는다 · 각 슬라이드 첫 문장이 바로 나온다 · "um"이 적다

## 체크

- [ ] 4절 메인 · 보조 스토리 결정
- [ ] 5절 표 11칸 채우기
- [ ] 6절 2분 버전을 영어로 써서 소리 내어 3번
- [ ] 슬라이드 뼈대 20장 (제목 + 첫 문장)
- [ ] 리허설 3회 기록

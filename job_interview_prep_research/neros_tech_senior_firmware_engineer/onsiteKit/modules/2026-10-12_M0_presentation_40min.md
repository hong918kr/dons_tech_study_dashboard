# 🎤 M0 발표 40분 + Q&A — "소프트웨어 · 자동화 · 테스트 · 아티팩트 · 대시보드 · 짧은 스토리"

> 리크루터가 말한 키워드를 그대로 목차로 삼는다: **software, more software, what automation · SW test, system test, artifacts, dashboard, details · short story, problem-solving strategy**. 40분 발표 + 20분 Q&A. 자료는 자유 형식 → **PDF 한 파일**.

> 🆕 **10-14 확정 [K01](../plan/2026-10-14_K01_onsite_day_schedule_panel.md)**: 10-22(목) **11:00~12:00 패널 6명** — Balapranesh Elango · Michael Aguayo · Adam Kibit · Michael Honar · Gil Vargas · Jon Kotowski. **PDF/PPT를 48시간 전(10-20 11:00 PDT)까지 제출, 안 내면 온사이트 취소 가능** → 10-19 제출 목표. 민감 정보가 있으면 30분 일찍 와서 그쪽 노트북으로 공유해야 하므로 **처음부터 민감 정보 없이** 만든다.

## 1. 리크루터 키워드 → 무엇을 보여 줘야 하나

| 키워드 (필기) | 그들이 실제로 알고 싶은 것 [추정] | Don 재료 (레쥬메 범위) |
|---|---|---|
| software, more software | 이 사람이 **코드를 짜서 도구를 만드는 사람**인가 (FW만 아는 사람이 아니라) | SK hynix App SW: 챔버 테스트 자동화 web 플랫폼 + SDK |
| what automation did you use | 사람 손을 어떤 구조로 없앴나. 도구 선택 이유 | 같은 플랫폼: 온도 · 스케줄링 · eSSD 상태 · UART 시퀀스 자동화 |
| SW test | 단위/모듈 수준 테스트를 설계해 봤나 | Solidigm: RTL freeze 전 SoC verification (I2C, SPI, DMA, PCIe controller, SRAM/DRAM) |
| system test | HW+FW 합쳐진 상태에서 시험해 봤나 | SK hynix reliability system (NAND, PCIe 3/4/5) · Apple stress 기반 latent defect · factory test-node |
| artifacts | 테스트가 남기는 결과물: 로그, 리포트, 파형, 버그 리포트 | (Don: 실제로 만든 리포트 형식 — 지어내지 않기) |
| dashboard | 결과를 사람이 어떻게 보나 | 챔버 자동화 web 플랫폼의 화면 · NVMe telemetry |
| details | 말만이 아니라 깊이 | 구조도 1장 + 코드/로그 일부 1장 (재구성본) |
| short story | 한 문제를 끝까지 판 이야기 | Apple 새 실리콘 통합 루트코즈 또는 Solidigm FPGA bring-up |
| strategy for problem solving | **어떤 순서로 생각하는 사람인가** | 7절의 6단계 틀 |

> **Neros에서 이게 중요한 이유:** FW Test 팀은 3명이고 factory test FW가 아직 없다 [확인됨 이전 라운드]. "테스트 인프라를 처음부터 세울 수 있는 사람"인지 보는 자리다.

## 1-1. 패널 6명에 맞추기

| 청중 | 관심 [추정] | 슬라이드에서 챙길 것 |
|---|---|---|
| Adam (FW Director) | 팀에 무엇을 더하나 · 레벨 | 15번 Neros 90일 그림 |
| Michael Honar (FW Test Mgr) | 테스트 사고방식 · 같이 일할 사람 | 9~11번 테스트 설계 · system test |
| Michael Aguayo (Sr FW) | FW 깊이 · 디버깅 | 12~14번 스토리의 측정 · 증거 |
| Jon Kotowski (Full Stack [추정]) | 도구 · 데이터 · 대시보드 | 5~7번 구조도 · **데이터 모델**(run id · DUT serial · FW 버전 · 결과 · 로그 링크) |
| Balapranesh Elango · Gil Vargas | 확인 못 함 | 약어는 처음 쓸 때 풀어 쓰기 (SSD/NVMe를 모르는 사람 기준) |

> 섞인 청중이므로 **SSD · NVMe 맥락 설명은 한 문장으로 끝내고** "이게 드론에서는 무엇에 해당하는지"로 바로 옮긴다 (예: NVMe telemetry → 비행 블랙박스 로그).

## 2. 40분 구성 (슬라이드 16장)

| # | 분 | 슬라이드 | 핵심 한 줄 (영어로 말할 문장) |
|---|---|---|---|
| 1 | 1 | 제목 | "Building test systems that tell you when hardware and firmware are actually ready." |
| 2 | 2 | 경력 지도 (4칸 타임라인) | "App software, then firmware, then silicon integration — the common thread is test and debug." |
| 3 | 1 | 오늘 순서 | "Automation, SW test, system test, then one debugging story." |
| 4 | 3 | A. 자동화 플랫폼 — 문제 | "Before this, a person babysat every chamber run." (Don: 이전 상태 수치) |
| 5 | 3 | A. 구조도 | 스케줄러 → 장비 제어(온도) → DUT 제어(UART 시퀀스) → 결과 저장 → web 대시보드 |
| 6 | 2 | A. 자동화 기술 선택 | "Why web + SDK: test authors write sequences, not plumbing." (Don: 실제 스택) |
| 7 | 2 | A. 대시보드 · 아티팩트 | 재구성 화면: 실행 목록 · pass/fail · 로그 링크 · 추세 |
| 8 | 3 | B. SW test — pre-silicon | "Firmware-driven tests for I2C, SPI, DMA, PCIe before RTL freeze." |
| 9 | 3 | B. 테스트 케이스 설계 방법 | 정상 · 경계 · 오류 주입 · 동시성 — 표 한 장 |
| 10 | 3 | C. system test — stress | "Latent defects only show up under stress, so we designed the stress." |
| 11 | 2 | C. factory test-node | "What a factory test must prove in seconds, not hours." |
| 12 | 1 | D. 스토리 — 증상 | 무엇이 언제 어떻게 깨졌나 |
| 13 | 3 | D. 스토리 — 전략 | 7절 6단계 중 실제로 쓴 것 · 버린 가설 |
| 14 | 2 | D. 스토리 — 결과 · 재발 방지 | 고친 것 + 프로세스에 남긴 것 (테스트 추가 등) |
| 15 | 3 | E. Neros에 가져올 것 | factory test FW · HIL 회귀 · 결과 대시보드 — 90일 그림 |
| 16 | 1 | 마무리 + 질문 | "Happy to go deeper on any of these." |

> 시간이 남으면 늘리지 말고 Q&A를 길게. **40분을 넘기면 감점**이 더 크다 [추정].

## 3. 슬라이드별 채울 칸 (Don이 채움 — 지어내지 않기)

| 칸 | 내용 |
|---|---|
| A-1 자동화 전 상태 | (Don: 사람이 하던 일 · 걸리던 시간 · 실수 종류) |
| A-2 구조 | (Don: 구성 요소 이름 · 언어 · DB · 통신 방식) |
| A-3 사용자 | (Don: 누가 썼나 · 몇 명 · 몇 대 챔버) |
| A-4 효과 | (Don: 줄어든 시간 · 늘어난 실행 수 — 숫자가 없으면 "정성적"으로) |
| B-1 SoC verification 범위 | (Don: 블록별 테스트 몇 개 · 어떤 버그를 잡았나 · RTL 팀과 어떻게 주고받았나) |
| C-1 stress 조건 | (Don: 온도 · 전압 · 시간 · 패턴) |
| C-2 factory test-node | (Don: 공개 가능한 수준의 구조만) |
| D 스토리 | 5절 표 |

> **NDA:** 실제 화면 캡처 금지. **다시 그린 구조도 · 일반화한 숫자**만. 첫 슬라이드에 "Details generalized; no proprietary data." 한 줄을 넣으면 질문받을 때 편하다.

## 4. 짧은 스토리 고르기

| 후보 | 장점 | 단점 | Neros 연결 |
|---|---|---|---|
| Apple 새 실리콘 통합 루트코즈 (PCIe/I2C/SPMI/RFFE) | 가장 최근 · HW/FW 경계 · 계측 장비 | NDA 범위가 좁을 수 있음 | 새 보드 bring-up · 서브시스템 통합 (M3과 직결) |
| Solidigm FPGA 이미지/FW bring-up | 처음부터 세운 이야기 | SSD 맥락 설명이 길어짐 | 처음 켜는 보드에서 어디부터 보나 |
| SK hynix 챔버 자동화 플랫폼 | 자동화 · 대시보드 키워드에 정확히 맞음 | "문제 해결" 긴장감이 약함 | factory test 인프라 |
| SK hynix NVMe telemetry 디버그 기능 | 고객 이슈 → 관측 기능을 만듦 | 오래됨 | 드론 텔레메트리 · 블랙박스 |

> 추천: **A(자동화)는 4~7번 슬라이드에서 이미 다루니, 스토리는 Apple 또는 Solidigm**. 둘 다 M3 "새 서브시스템을 어디서부터 테스트하나"와 연결된다.

## 5. 스토리 표 (STAR + 전략)

| 칸 | 내용 |
|---|---|
| 상황 (1문장) | (Don: …) |
| 증상 · 영향 | (Don: 무엇이 실패 · 얼마나 자주 · 누가 막혔나) |
| 처음 세운 가설 3개 | (Don: …) |
| 어떻게 좁혔나 | (Don: 재현 조건 · 계측 · 버린 가설과 이유) |
| 결정적 증거 | (Don: 파형 · 로그 · 레지스터) |
| 해결 | (Don: …) |
| 재발 방지 | (Don: 추가한 테스트 · 체크리스트 · 모니터링) |
| 배운 것 (1문장) | (Don: …) |

## 6. "어떤 전략을 택했나" — 6단계 틀

1. **Reproduce** — 재현 조건을 줄여서 매번 나오게 만든다. 안 나오면 빈도를 숫자로
2. **Bound** — 시간(언제부터) · 공간(어느 보드/로트/FW) · 조건(온도/전압)으로 범위를 자른다
3. **Split HW vs FW** — 알려진 정상 FW로 바꿔 보기, 알려진 정상 보드로 바꿔 보기
4. **Instrument** — 추측 대신 측정: 스코프/LA, 로그 타임스탬프, 카운터
5. **Fix one thing** — 한 번에 하나만 바꾸고 다시 측정
6. **Prevent** — 이 버그를 잡는 테스트를 회귀에 넣는다 ← **테스트 엔지니어의 마무리**

> 영어 한 줄: "I try to make it reproducible first, then cut the search space in half each step — and I don't call it done until there's a test that would have caught it."

## 7. Q&A 대비 · 역질문

| 예상 질문 | 답의 방향 |
|---|---|
| Why move from FW dev to test? | 해 온 일의 공통점이 "준비됐는지 증명하는 일". 테스트 인프라는 팀 전체 속도를 올린다 |
| What would you automate first here? | 매일 사람이 반복하는 것 · 실패 시 비용이 큰 것 순서. 먼저 현재 흐름을 1~2주 관찰 |
| How do you decide pass/fail for something new? | 요구사항 → 측정 가능한 기준 → 마진. 기준이 없으면 특성화(characterization) 먼저 |
| Tools you'd use for dashboards? | 형식보다 **데이터 모델**(run id · DUT serial · FW 버전 · 결과 · 로그 링크)이 먼저 |
| Hardest part of your story? | 버린 가설과 이유를 말하기 |
| Have you used STM32 / RTOS? | 레쥬메에 직접 명시 없음 → Cortex-M/R bare-metal 경험으로 연결, 모르면 모른다고 + 배우는 방법 |

| 면접관 | 역질문 2개 |
|---|---|
| Adam (FW Director) | "What does the test team own today, and what do you want it to own in a year?" · "What slows FW releases down the most right now?" |
| Michael Honar (FW Test Mgr) | "What does a typical day of the test team look like — bench, HIL, factory?" · "Where do test results live today?" |
| Michael B (Sr FW) | "What would make you trust a test result from the test team?" · "Which subsystem breaks most often in integration?" |

## 8. 리허설 기록

| 날짜 | 걸린 시간 | 막힌 슬라이드 | 고칠 것 |
|---|---|---|---|
| | | | |

## 9. 제출 전 점검 (10-19)

- [ ] PDF로 내보냄 · 다른 컴퓨터(또는 폰)에서 열어 폰트 · 그림 확인
- [ ] 회사 내부 화면 캡처 · 고객 이름 · 내부 코드명 · 실제 수치 없음
- [ ] 첫 장에 "Details generalized; no proprietary data."
- [ ] 파일 이름: `Don_<성>_Neros_Onsite_Presentation.pdf`
- [ ] 슬라이드 번호 (Q&A 때 "7번 슬라이드"로 가리키기 쉽게)
- [ ] 제출 메일 발송 + 받았다는 답장 확인

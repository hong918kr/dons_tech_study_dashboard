# 🗓️ P01 · 온사이트 10일 계획 — 스토리 · 코딩 · system design

> **온사이트 초대 확정** — Adam(09-18) → Michael Honor(10-01) → Jon Kotowski(10-08 technical screen) 3라운드를 통과했다 [확인됨 Don 10-11]. 구성 [확인됨 2026-09-16 Devin · 2026-10-11 Don]: **Tour 30분 → 내 경력 발표 1시간(내가 한 일 · 지금 하는 일 · challenging problem) → 1:1 여러 개(기본 C/C++ · ring buffer 같은 코딩 · generic / "necessary" system design)**. Adam에 따르면 **Senior Firmware Engineer, Platform** 쪽 엔지니어도 면접관으로 들어올 수 있다. 리크루터 Devin과 **10-12(월) 오전에 온사이트 날짜를 정하는 통화**가 있다(통화 시간 미정). 날짜가 정해지기 전이므로 **D-10 = 10-11, 온사이트 ≈ 10-21로 가정**하고 짰다 — 날짜가 정해지면 D 번호 기준으로 옮기면 된다. 예상 문제는 [N10](../notes/2026-10-11_N10_onsite_expected_problems.md), 발표는 [N11](../notes/2026-10-11_N11_presentation_story_workbook.md).

## 0. 무엇에 시간을 쓰나 (비중)

| 블록 | 비중 | 이유 |
|---|---|---|
| **발표 1시간** | **40%** | 가장 큰 블록 · 준비 시간 최장 · 개발 깊이를 보여 줄 유일한 무대 |
| 1:1 코딩 (C/C++) | 25% | ring buffer는 스크린에서 했으니 **패킷 파서 · 비트 · CRC · 메모리 풀 · 타이머** 쪽으로 |
| system design | 20% | generic이지만 Platform 엔지니어면 logging · OTA · 프로토콜 · config, Michael이면 테스트 인프라 |
| 개념 질문 · 행동 · Tour | 15% | 매일 조금씩 |

## 1. 매일 고정 루틴 (45분)

| 분 | 할 것 | 자료 |
|---|---|---|
| 20 | 코딩 손 풀기 1문제 — 빈 파일에서 + `assert` 테스트 | [N10 1절](../notes/2026-10-11_N10_onsite_expected_problems.md) · [L05](../../technicalScreenPrep/site/lecture/2026-10-04_L05_warmup.html) |
| 15 | 개념 질문 5개 — 가리고 30초씩 영어로 | [N10 2절](../notes/2026-10-11_N10_onsite_expected_problems.md) · [N07](../notes/2026-10-01_N07_embedded_quiz_bank.md) |
| 10 | 메인 스토리 2분 버전 소리 내기 | [N11 6절](../notes/2026-10-11_N11_presentation_story_workbook.md) |

## 2. 내일 Devin 콜에서 물어볼 것 (10-12) — 날짜를 정하면서 함께 확인

| # | 질문 (영어) | 왜 |
|---|---|---|
| 1 | "What date and time slots work for the onsite, and what's the order of the sessions?" | 일정 · 순서 (발표가 처음인지) |
| 2 | "Who will be on the panel and in each 1:1 — names and roles?" | Platform 엔지니어 포함 여부, 준비 방향 |
| 3 | "For the one-hour presentation — is Q&A included in the hour? Slides on my laptop or theirs? Any topics to avoid or emphasize for the firmware test role?" | 발표 길이 · 형식 · 기대치 |
| 4 | "Are the 1:1 coding sessions on a whiteboard or a laptop, and can I choose C, C++ or Python again?" | 연습 환경 맞추기 |
| 5 | "Is the system design session whiteboard-based? Any particular domain?" | 범위 |
| 6 | "Travel and logistics — flights, hotel, parking, arrival time, ID or citizenship paperwork for the site?" | Torrance 방문, 방산 시설 출입 |
| 7 | "Is there any feedback from the previous rounds I should know about?" | 무엇을 더 보여 줄지 |

- **아직 꺼내지 않는 것**: 레벨 · 밴드 · 트랙 변경 (오퍼 단계 전략 그대로 — [컨텍스트 파일 §5](../../neros_tech_senior_firmware_engineer_context.md))
- 통화 후: 답을 컨텍스트 파일 §5 · §6과 이 계획의 날짜에 반영

## 3. 10일 일정 (온사이트 ≈ D-day)

| D | 날짜(가정) | 발표 | 코딩 | system design · 기타 |
|---|---|---|---|---|
| D-10 | 10-11 (일) | [N11](../notes/2026-10-11_N11_presentation_story_workbook.md) 4절: **메인 · 보조 스토리 결정** | 패킷 파서 상태 머신 읽기 → 빈 파일 1회 | Devin 콜 질문 정리 (2절) |
| D-9 | 10-12 (월) | 5절 표 11칸 채우기 | 비트 필드 · 엔디언 (L03 2 · 6절) | **Devin 콜** → 날짜 확정 후 이 표 조정 |
| D-8 | 10-13 (화) | 3절 경력 지도 + **슬라이드 뼈대 20장** (제목 + 첫 문장) | CRC 테이블 빈 파일 (L10) | 개념 질문 5개 |
| D-7 | 10-14 (수) | Part 2 슬라이드 10장 초안 | 패킷 파서 빈 파일 2회째 (20분 목표) | **design S: logging 시스템** 10분 화이트보드 |
| D-6 | 10-15 (목) | **리허설 1** (혼자 · 녹음 · 타이머) | 메모리 풀 빈 파일 | **design S: 펌웨어 업데이트 (부트로더 → 롤아웃)** |
| D-5 | 10-16 (금) | 리허설 1에서 고칠 것 반영 · Part 4 슬라이드 | 소프트 타이머 · tick wrap | **design S: MCU ↔ companion 프로토콜** |
| D-4 | 10-17 (토) | **리허설 2** (슬라이드 넘기며 · 60분 전체) | C++: 인터페이스 + RAII (L04 4 · 7절) | design A: config store · 센서 파이프라인 |
| D-3 | 10-18 (일) | Q&A 질문 7개 답 다듬기 (N11 7절) | **모의 1:1 데이**: 코딩 2문제 × 45분 (타이머) | **모의 design 45분**: HIL 팜 또는 factory test |
| D-2 | 10-19 (월) | **리허설 3** (사람 앞 · 질문 받기) | 틀린 것만 다시 | 행동 질문 · 역질문 확정 (N10 5 · 6절) |
| D-1 | 10-20 (화) | 2분 · 5분 버전만 | S등급 3개 훑기 | 짐 · 이동 · 일찍 잔다 (5절) |
| D-day | 10-21 (수) | | | 당일 체크리스트 (5절) |

- 날짜가 더 늦어지면: 리허설 사이 간격을 늘리고 D-3의 모의 1:1 데이를 한 번 더
- 날짜가 더 빠르면: 발표(메인 스토리 · 슬라이드 · 리허설 2회)를 먼저, 코딩은 S등급 3개만

## 4. 이미 있는 자료 지도 — 새로 공부하지 말고 연결해서

| 주제 | 자료 |
|---|---|
| 발표 원칙 · 슬라이드 20장 뼈대 · 기밀 규칙 | [N03](../notes/2026-10-01_N03_presentation_1hour.md) |
| 레쥬메 스토리라인 13개 | [FTE N09](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N09_resume_storylines.html) |
| C/C++ 기본기 · 퀴즈 | [N04](../notes/2026-10-01_N04_c_cpp_fundamentals.md) · [N07](../notes/2026-10-01_N07_embedded_quiz_bank.md) · [L03](../../technicalScreenPrep/site/lecture/2026-10-04_L03_embedded_c_basics.html) · [L04](../../technicalScreenPrep/site/lecture/2026-10-04_L04_embedded_cpp_basics.html) |
| 코딩 (검증된 C 코드) | [code/](../code/packet_parser_sm.c) — 패킷 파서 · 메모리 풀 · 소프트 타이머 · ring buffer 5종 (`make -C code test`) · [practice 7세트 42문제](../../practice/html/index.html) |
| 스크린 재구성 · CRC | [L09](../../technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html) · [L10](../../technicalScreenPrep/site/lecture/2026-10-09_L10_crc_table.html) |
| system design | [N06 (7문제)](../notes/2026-10-01_N06_generic_system_design.md) · [S00~S09 직군별 테스트 설계](../../technicalScreenPrep/site/design/2026-10-03_S00_overview.html) · [technicalScreenPrep N04](../../technicalScreenPrep/site/notes/2026-10-02_N04_system_design_test_tooling.html) |
| factory test FW | [N08](../notes/2026-10-01_N08_factory_test_firmware.md) |
| 회사 · 스택 | [N01](../notes/2026-10-01_N01_company_stack_map.md) · [N02 STM32](../notes/2026-10-01_N02_stm32_what_it_means.md) · [C02 드론 스택 용어](../company/2026-10-02_C02_drone_tech_stack_glossary.md) |
| 면접관 · 행동 | [N09](../notes/2026-10-01_N09_interviewers_behavioral.md) |

## 5. 온사이트 전날 · 당일 체크리스트

### 전날

- [ ] 슬라이드 파일 2곳 (노트북 + USB · 클라우드), 화면 공유 · HDMI 어댑터
- [ ] 발표 2분 · 5분 버전 한 번씩
- [ ] N10 S등급 3개 훑기 (패킷 파서 · 비트 필드 · CRC)
- [ ] 역질문 면접관별 1개씩 메모
- [ ] 이동 · 숙소 · 도착 시간 · 신분증 (방산 시설 출입)
- [ ] 7시간 수면

### 당일

- [ ] 물 · 펜 · 종이 (화이트보드용 손 그림 연습했던 블록도)
- [ ] Tour: 생산 라인에서 **어디서 · 무엇을 테스트하는지** 질문 2개 준비
- [ ] 발표 시작 전 화면 · 마이크 · 타이머
- [ ] 1:1마다: 질문 → 시그니처 → 구현 → **묻기 전에 테스트** → 확장
- [ ] 끝나고 바로: 받은 질문 · 면접관 이름 · 반응 메모 → 컨텍스트 파일 §6

## 6. 진행 기록 (매일 한 줄)

| 날짜 | 한 것 | 막힌 것 / 내일 |
|---|---|---|
| 10-11 | | |
| 10-12 | | |
| 10-13 | | |
| 10-14 | | |
| 10-15 | | |
| 10-16 | | |
| 10-17 | | |
| 10-18 | | |
| 10-19 | | |
| 10-20 | | |

# 🧭 START HERE — Neros 온사이트 준비, 어디서부터 볼까

> 온사이트 구성은 **Tour 30분 → 내 경력 발표 1시간 → 1:1 기술 여러 개**(C/C++ · 코딩 · generic system design)다. **온사이트 초대 확정** — Adam(09-18) → Michael Honor(10-01) → Jon Kotowski(10-08 technical screen) 3라운드 통과. 날짜는 **10-12 Devin 통화에서 결정**. ring buffer는 스크린에서 했으니 이번엔 **발표가 가장 오래 걸리는 준비**다. 남은 시간에 맞는 코스 하나만 골라 위에서부터 내려간다.

## 🆕 10-12 Devin 콜로 형식 확정 → 새 키트로

> 발표 40분 · embedded system design · **Python 코딩** · HW/test design. 이 폴더는 형식 확정 전 자료다. 지금은 **[onsiteKit](../../onsiteKit/index.html)** 에서 시작.

## 🗓️ (이전) 온사이트 10일 계획 (10-11~)

> **[P01 10일 계획](plan/2026-10-11_P01_onsite_10day_plan.md)** (Devin 콜 질문 · 날짜별 일정 · 체크리스트) → **[N10 예상 문제 뱅크](notes/2026-10-11_N10_onsite_expected_problems.md)** (1:1 코딩 S/A/B · 개념 20 · system design 10 · Platform 엔지니어 관점) → **[N11 발표 스토리 워크북](notes/2026-10-11_N11_presentation_story_workbook.md)** (스토리 표 · 2/5/20분 버전 · 리허설 기록). 새 C 코드: [패킷 파서 상태 머신](code/packet_parser_sm.c) · [메모리 풀](code/mem_pool.c) · [소프트 타이머](code/soft_timer.c) (`make -C code test`)

## 🧾 10-08 technical screen 반영 (먼저 읽기)

> 스크린은 **C 코딩만**: ring buffer(외부 버퍼 · bulk all-or-nothing · peek offset) → 보너스 extract_packet(CRC). → **통과, 온사이트 초대**. 들은 정보: **FW 17명 · Test 3명 · + full stack**. 재구성 · 참고 풀이 · **온사이트 우선순위(발표 → C 심화 → 테스트 인프라 system design → factory test FW 비전)**: [L09](../../technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html)

## ⏱️ 시간이 2~3시간뿐일 때 — 빠른 코스

| 순서 | 시간 | 볼 것 | 왜 |
|---|---|---|---|
| 1 | 15분 | [P01 10일 계획](plan/2026-10-11_P01_onsite_10day_plan.md) — 0절 비중, 2절 Devin 질문 | 전체 그림과 우선순위 |
| 2 | 60분 | [N11 발표 워크북](notes/2026-10-11_N11_presentation_story_workbook.md) — **4절 메인 스토리 고르기**, 5절 표 채우기 | 가장 오래 걸리는 준비 |
| 3 | 40분 | [N10 예상 문제](notes/2026-10-11_N10_onsite_expected_problems.md) 1절 S등급 — [패킷 파서](code/packet_parser_sm.c)를 보지 않고 1회 | 스크린 다음 단계의 코딩 |
| 4 | 20분 | [N10 3절](notes/2026-10-11_N10_onsite_expected_problems.md) — system design S등급 하나를 10분 화이트보드 | Platform 엔지니어 대비 |
| 5 | 20분 | [N10 2절](notes/2026-10-11_N10_onsite_expected_problems.md) 개념 질문 10개 | 퀴즈형 대비 |

## ⏱️ 온사이트 날짜가 잡히면 — 전체 코스

1. **발표**: [N03](notes/2026-10-01_N03_presentation_1hour.md) 슬라이드 20장 → 리허설 3회. 재료: [N08 factory test FW](notes/2026-10-01_N08_factory_test_firmware.md) (Part 4), [레쥬메 스토리라인 13개](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N09_resume_storylines.html)
2. **ring buffer**: [N05](notes/2026-10-01_N05_ring_buffer_onsite.md) → [C 코드 5개](code/ring_buffer.c) → 더 깊게 [링버퍼 완전정복](../../research/ringbuffer_research/html/index.html)
3. **C/C++**: [N04 C/C++ 기본기](notes/2026-10-01_N04_c_cpp_fundamentals.md) — 30초 답 + 함정 코드 읽기
4. **임베디드 퀴즈**: [N07](notes/2026-10-01_N07_embedded_quiz_bank.md) — 버스, PCIe, 8b/10b, MCU, RTOS, 신호
5. **system design**: [N06](notes/2026-10-01_N06_generic_system_design.md) — 프레임워크 → 로깅·OTA·프로토콜·factory test 4문제 먼저
6. **STM32**: [N02](notes/2026-10-01_N02_stm32_what_it_means.md) — "STM 써 봤나?" 답과 UART DMA·캐시·RDP
7. **사람과 당일**: [N09](notes/2026-10-01_N09_interviewers_behavioral.md) — 면접관별 대응, 행동 질문, Tour, 체크리스트

## ⏱️ 온사이트 전날·당일 아침 (1시간)

| 순서 | 시간 | 볼 것 |
|---|---|---|
| 1 | 15분 | [N03](notes/2026-10-01_N03_presentation_1hour.md) — 슬라이드별 첫 문장, Part 4 스크립트 |
| 2 | 15분 | [N05](notes/2026-10-01_N05_ring_buffer_onsite.md) — 질문 리스트와 작성 순서 |
| 3 | 10분 | [N06](notes/2026-10-01_N06_generic_system_design.md) — 프레임워크 단계만 |
| 4 | 10분 | [N09](notes/2026-10-01_N09_interviewers_behavioral.md) — 0절 이미 한 말, 6절 당일 체크리스트 |
| 5 | 10분 | [N07](notes/2026-10-01_N07_embedded_quiz_bank.md) — A절 Michael 퀴즈 복기 |

## 🗂️ 자료 지도 — 라운드별

| 라운드 | 자료 |
|---|---|
| Tour 30분 | [N09](notes/2026-10-01_N09_interviewers_behavioral.md) 4절 · [N01](notes/2026-10-01_N01_company_stack_map.md) |
| 발표 1시간 | [N03](notes/2026-10-01_N03_presentation_1hour.md) · [N08](notes/2026-10-01_N08_factory_test_firmware.md) · [FW Test N09 스토리라인](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N09_resume_storylines.html) |
| 1:1 기본 C/C++ | [N04](notes/2026-10-01_N04_c_cpp_fundamentals.md) · [Neros C 연습 05·06](../../practice/html/index.html) |
| 1:1 ring buffer | [N05](notes/2026-10-01_N05_ring_buffer_onsite.md) · [C 코드](code/ring_buffer.c) · [링버퍼 완전정복](../../research/ringbuffer_research/html/index.html) |
| 1:1 system design | [N06](notes/2026-10-01_N06_generic_system_design.md) · [N08](notes/2026-10-01_N08_factory_test_firmware.md) |
| 임베디드 퀴즈 (누구든) | [N07](notes/2026-10-01_N07_embedded_quiz_bank.md) · [N02](notes/2026-10-01_N02_stm32_what_it_means.md) |
| 테스트·디버깅 시나리오 | [FW Test N08 시나리오](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N08_test_and_debug_scenarios.html) |
| 회사·지원 경과 | [N01](notes/2026-10-01_N01_company_stack_map.md) · [Neros 컨텍스트 파일](../neros_tech_senior_firmware_engineer_context.md) §6 |
| 🏢 회사 파악 Q&A (Don이 물어본 것) | [C01 autonomy란? Hark Embedded AI와 비슷한가](company/2026-10-02_C01_autonomy_vs_embedded_ai.md) · [C02 드론 스택 데이터 흐름·용어 사전](company/2026-10-02_C02_drone_tech_stack_glossary.md) · 이전 답: [STM 사용의 의미 (N02)](notes/2026-10-01_N02_stm32_what_it_means.md) |

## 💻 C 코드 채점

```bash
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/neros_tech_senior_firmware_engineer/onsitePrep
make -C code test     # ring buffer 5종: 기본 · SPSC lock-free · 덮어쓰기 · 구조체 · DMA circular RX
python3 build_notes_site.py   # md 고친 뒤 site/ 다시 만들기
```

## 🏢 회사 파악 — 물어본 질문 모음

| # | 질문 | 날짜 |
|---|---|---|
| C01 | [Neros의 autonomy는 무슨 뜻이고, Hark Embedded AI와 비슷한가?](company/2026-10-02_C01_autonomy_vs_embedded_ai.md) | 10-02 |
| C02 | [드론 기술 스택은 어떻게 흐르고, IMU·PX4·EKF·GNC·ELRS·SITL 같은 용어는 각각 어디에 있나?](company/2026-10-02_C02_drone_tech_stack_glossary.md) | 10-02 |
| (N02) | [STM을 쓴다는 건 무슨 의미인가? 자체 실리콘은?](notes/2026-10-01_N02_stm32_what_it_means.md) | 10-01 |
| (N01) | [FW·임베디드 8개 직군으로 본 회사 스택](notes/2026-10-01_N01_company_stack_map.md) | 10-01 |

## 📌 온사이트에서 기억할 다섯 줄

- 발표: **사례 하나를 바닥까지**, 마지막은 "Neros에 factory test FW와 HIL을 세우겠다"
- ring buffer: 질문 먼저(타입, 용량, 생산자·소비자 수, ISR?, 넘칠 때 정책) → 2의 거듭제곱 + free-running index → 내가 테스트
- 퀴즈: **한 문장 정의 → 왜 → 예시/숫자**, 30초
- system design: 요구사항을 숫자로 → 블록도 → 예산 → 실패 모드 → **테스트**
- 자체 실리콘이 없다 → "칩을 고칠 수 없으니 보드 bring-up, errata 우회, second-source 검증, factory·HIL 테스트로 잡는다"

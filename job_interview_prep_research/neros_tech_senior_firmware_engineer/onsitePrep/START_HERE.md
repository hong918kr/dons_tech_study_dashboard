# 🧭 START HERE — Neros 온사이트 준비, 어디서부터 볼까

> 온사이트 구성은 **Tour 30분 → 내 경력 발표 1시간 → 1:1 기술 여러 개**(C/C++ · ring buffer · generic system design)다. 날짜는 아직 미정. **발표와 ring buffer가 가장 오래 걸리니 그 둘부터** 시작한다. 남은 시간에 맞는 코스 하나만 골라 위에서부터 내려간다.

## ⏱️ 지금 바로 (결과 통보 전, 2~3시간)

| 순서 | 시간 | 볼 것 | 왜 |
|---|---|---|---|
| 1 | 15분 | [P00 게임 플랜](plan/2026-10-01_P00_onsite_game_plan.md) | 전체 그림과 우선순위 |
| 2 | 20분 | [N01 회사 스택 지도](notes/2026-10-01_N01_company_stack_map.md) — 0절 요약, 4절 면접관별 관심사 | 누가 무엇을 물을지 |
| 3 | 60분 | [N03 발표](notes/2026-10-01_N03_presentation_1hour.md) — **4절 메인 사례 고르기**, 표 채우기 | 가장 오래 걸리는 준비 |
| 4 | 40분 | [N05 ring buffer](notes/2026-10-01_N05_ring_buffer_onsite.md) → 보지 않고 1회 작성 | 확정된 코딩 주제 |
| 5 | 20분 | [N07 퀴즈 뱅크](notes/2026-10-01_N07_embedded_quiz_bank.md) — A절 Michael 퀴즈 복기 | PCIe를 SW 관점으로 다시 |

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

# 🎯 P00 · 온사이트 게임 플랜

> Neros 온사이트(Torrance) 대비 전체 계획. 구성은 **Tour 30분 → 내 경력 발표 1시간 → 1:1 기술 인터뷰 여러 개**(기본 C/C++, ring buffer 구현, generic system design)다 [확인됨: 리크루터 09-16]. 날짜는 아직 없다. 결과 통보 전에 **발표 초안과 ring buffer**부터 시작하고, 날짜가 잡히면 D-day에서 거꾸로 맞춘다.

## 1. 지금 상황 (2026-10-01)

- 10-01 FW Test HM **Michael Honor** 인터뷰를 끝냈다. 퀴즈형이었고 분위기가 좋았다. "결과 곧 알려 주겠다"
- 들은 정보: FW 15명, Test는 Michael 밑에 2명, **테스트 FW가 가장 급해서 연내 최대 5명 채용**, **factory test FW가 아직 없음**, **STM32 사용 = 자체 실리콘 없음**, 개발 약 70 / 전체 약 260
- 온사이트에 **Senior FW Platform HM**도 들어올 수 있다 [Don]
- 공고: Firmware Test Engineer는 아직 열려 있다 (10-01 기준). 복수 채용 중

## 2. 핵심 전략 세 줄

1. **발표로 주도권을 잡는다**: bring-up → 디버깅 사례 한 개를 바닥까지 → 테스트 인프라 → **"Neros에 factory test FW와 HIL을 세우겠다"**로 마무리
2. **확정 주제는 완벽하게**: ring buffer는 25분 안에 질문하고, 쓰고, 테스트하고, 꼬리 질문까지 막는다
3. **임베디드 기본기 퀴즈는 30초 답 틀로**: "한 문장 정의 → 왜 → 예시/숫자". Michael 스타일이 다시 나올 가능성이 높다

## 3. 우선순위와 자료

| 순위 | 준비 | 자료 | 예상 시간 |
|---|---|---|---|
| 1 | **발표 1시간** — 사례 고르기, 슬라이드 20장, 리허설 3회 | [N03 발표](../notes/2026-10-01_N03_presentation_1hour.md) | 8~10시간 |
| 2 | **ring buffer** — 맨손으로 3번, 꼬리 질문 | [N05 ring buffer](../notes/2026-10-01_N05_ring_buffer_onsite.md) · [코드](../code/ring_buffer.c) | 3시간 |
| 3 | **임베디드 퀴즈** — Michael 복기, 버스, MCU, RTOS | [N07 퀴즈 뱅크](../notes/2026-10-01_N07_embedded_quiz_bank.md) | 3시간 |
| 4 | **C/C++ 기본기** | [N04 C/C++](../notes/2026-10-01_N04_c_cpp_fundamentals.md) | 3시간 |
| 5 | **generic system design** — 프레임워크 + 7문제 중 4개 | [N06 system design](../notes/2026-10-01_N06_generic_system_design.md) | 4시간 |
| 6 | **factory test FW** — 발표 Part 4와 설계 질문에 끌어 쓰기 | [N08 factory test](../notes/2026-10-01_N08_factory_test_firmware.md) | 2시간 |
| 7 | **STM32와 회사 스택** | [N02 STM32](../notes/2026-10-01_N02_stm32_what_it_means.md) · [N01 스택 지도](../notes/2026-10-01_N01_company_stack_map.md) | 2시간 |
| 8 | **면접관별 대응·행동 질문·당일 준비** | [N09](../notes/2026-10-01_N09_interviewers_behavioral.md) | 1시간 |

## 4. 날짜가 잡히기 전 (지금 ~ 통보)

- [ ] N03 4절: 발표 메인 사례 고르고 표 채우기 (가장 오래 걸린다. 기억을 끌어내야 해서)
- [ ] N05: ring buffer를 보지 않고 1회 작성 → `make -C code test`로 내 코드도 돌려 보기
- [ ] N07 A절: Michael 퀴즈 복기 답안을 소리 내어 (특히 PCIe를 SW 관점으로)
- [ ] N01, N02 한 번 읽기

## 5. 날짜가 잡히면 — D-day 역산

| 시점 | 할 일 |
|---|---|
| D-7 ~ D-5 | 발표 슬라이드 초안 20장. N04 C/C++ 퀴즈 전부 한 번. 링버퍼 레벨 4(SPSC)까지 ([링버퍼 완전정복](../../research/ringbuffer_research/html/index.html)) |
| D-4 ~ D-3 | 발표 리허설 1·2회 (녹음). N06 system design 4문제(로깅, OTA, 프로토콜, factory test)를 화이트보드에. N08 정독 |
| D-2 | 발표 리허설 3회 (타이머). N07 전체 30초 답 속도전. ring buffer 맨손 2회째 |
| D-1 | 가볍게: N09 당일 체크리스트, 이미 한 말 다시 읽기, 면접관 LinkedIn, 발표 자료 PDF 백업. **일찍 자기** |
| D-day 아침 | 발표 첫 문장들, ring buffer 순서, system design 프레임워크 8단계만 훑기 |

## 6. 라운드별 마음가짐

| 라운드 | 한 줄 |
|---|---|
| Tour | 질문 3개로 정보를 모아 발표와 1:1에서 쓴다 ([N09](../notes/2026-10-01_N09_interviewers_behavioral.md) 4절) |
| 발표 | 40분 본 발표, 20분 질문. 사례 하나를 바닥까지. 마지막은 Neros의 문제로 |
| C/C++ | 짧고 정확하게, 모르면 아는 데까지 말하고 어떻게 확인할지 |
| ring buffer | 질문 먼저 → 설계 말하기 → 코드 → 내가 테스트 → 꼬리 질문 |
| system design | 숫자로 요구사항을 고정하고, 블록도, 예산, 실패 모드, **테스트**로 끝낸다 |

## 7. 이미 있는 자료 (복습용)

- [링버퍼 완전정복 7레벨 42문제](../../research/ringbuffer_research/html/index.html)
- [Neros C 연습 7토픽 42문제](../../practice/html/index.html): 01 ring/logging, 02 telemetry framing, 03 config store, 04 IPC/budget, 05 embedded core, 06 ISR timing, 07 sensors/actuators
- [드론 아키텍처 노트](../../practice/html/00_drone_architecture.html)
- [FW Test HM 준비 사이트](../../firmwareTestEngineerPrep/site/start.html): N08 테스트·디버깅 시나리오, N09 레쥬메 스토리라인 13개
- Adam 인터뷰 준비 노트: `../2026-09-18_adam_interview_D-6h_prep.md`, `../neros_hm_adam_technical_prep_2026-09-18.md`

## 체크

- [ ] 온사이트 날짜와 형식(발표 화면 공유 방식, 1:1 몇 개, 화이트보드인지 노트북인지)을 리크루터에게 확인
- [ ] 발표 메인 사례 결정
- [ ] ring buffer 맨손 3회
- [ ] 발표 리허설 3회
- [ ] system design 4문제 화이트보드
- [ ] 당일 체크리스트 완료

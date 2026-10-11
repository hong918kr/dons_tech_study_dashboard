# 🧭 START HERE — 10-08 technical screen, 어디서부터 볼까

> 2026-10-08(목) 11:00, 1시간, **코딩 위주**. 주제는 pytest · ring buffer · Python/C · system design, 면접관은 Full Stack 엔지니어로 추정되는 Jon Kotowski. 자료는 노트 5개 + Python 문제 7개(starter · 모범답안 · `run.py` 채점) + **매일 타이핑 드릴 100개** + **직군별 테스트 설계 10편**이다. **남은 시간에 맞는 코스 하나만** 골라 위에서부터 내려간다.

## 🧾 스크린 끝 (10-08) — 재구성 · 온사이트 방향

> **[L09 스크린 재구성 + 온사이트 준비 방향](lecture/2026-10-09_L09_screen_recap.md)** · **[L10 CRC 테이블 방식](lecture/2026-10-09_L10_crc_table.md)** — C ring buffer(외부 버퍼 · bulk all-or-nothing · peek offset) · 보너스 extract_packet(CRC) 참고 풀이와 테스트, 회고 칸, 온사이트 우선순위. 온사이트 자료: [onsitePrep](../onsitePrep/index.html)

## ✅ (지난) 마지막 체크업 (10-07 밤 2시간 + 10-08 9:00~11:40)

> **[L08 pytest 처음부터 — 아주 쉽게](lecture/2026-10-08_L08_pytest_from_zero.md)** (20분) → **[L07 마지막 체크업](lecture/2026-10-07_L07_final_checkup.md)** — 오늘 밤 · 내일 오전 시간표, ring buffer · pytest · C 치트시트, 말할 문장, 역질문, 체크리스트. ⚠️ 면접 시간 12:00 vs 11:00 (첫 안내) 초대 메일로 확인.

## 🎓 먼저 — 특별강의 L00 · L01 · L02

| # | 강의 | 내용 |
|---|---|---|
| L00 | [특별강의: ring buffer · pytest · Python LeetCode 15](lecture/2026-10-04_L00_special_lecture.md) | 가장 나올 것부터: 개념 → 코드 → Tier S/A/B 문제 |
| L01 | [문제 풀기 전 필수 Python 문법](lecture/2026-10-04_L01_python_syntax.md) | enqueue/dequeue(deque) · 스택 · heapq · dict/Counter · 슬라이스 · 정렬 · C와 다른 함정 |
| L02 | [pytest 말고 — 임베디드 테스트 프레임워크 10종](lecture/2026-10-04_L02_embedded_test_frameworks.md) | Unity · CMock · Ceedling · CppUTest · GoogleTest · unittest · Robot · behave · OpenHTF · py-micro-hil, 전부 실행 검증 |
| L03 | [임베디드 C 기본기 — 테스트 엔지니어용 가벼운 복습](lecture/2026-10-04_L03_embedded_c_basics.md) | 고정 폭 타입 · 비트 · 포인터 · 문자열 · memmove · 엔디언 · volatile/static · 패딩 · 매크로 · ring · CRC · tick wrap · ISR · 퀴즈 20 |
| L04 | [임베디드 C++ 기본기 — 테스트 엔지니어용](lecture/2026-10-04_L04_embedded_cpp_basics.md) | 쓰는 것/끄는 것 · 참조 · enum class/constexpr · RAII · 템플릿 RingBuffer · 인터페이스 DI · 테스트 seam · extern "C" · 함정 · 퀴즈 15 |
| L05 | [🔥 매일 몸풀기 — Python 8 + C 6](lecture/2026-10-04_L05_warmup.md) | 한 문제 = 구현 + 테스트 + main. Two Sum(dict) 2 · 투 포인터 2 · 이진 탐색 · BST · ring buffer · 괄호 / reverse str · linked list · strcpy · atoi · ring buffer 2종(count · free-running). `new.py` → playground 빈 파일 (계속 추가) |
| L10 | [🧮 CRC 테이블 방식](lecture/2026-10-09_L10_crc_table.md) | 스크린의 CRC.c — 파라미터 7개 · 비트 방식 → 테이블 생성 → 조회, 한 바이트 손 계산, CRC-8/16/32 · 니블 테이블 · 테스트 |
| L09 | [🧾 10-08 스크린 재구성 · 온사이트 방향](lecture/2026-10-09_L09_screen_recap.md) | Task 1 C ring buffer · Task 2 extract_packet 참고 풀이 + 테스트 13개, 데드락 엣지, 온사이트 우선순위 |
| L08 | [🐣 pytest 처음부터](lecture/2026-10-08_L08_pytest_from_zero.md) | 단어 6개(테스트 함수 · assert · raises · parametrize · fixture · Mock) — 예제 + 실제 실행 화면 |
| L07 | [✅ 마지막 체크업](lecture/2026-10-07_L07_final_checkup.md) | 밤 2시간 + 아침 9:00~11:40 시간표 · 치트시트 · 대사 · 체크리스트 |
| L06 | [🎯 스크린 예상 시나리오 6개](lecture/2026-10-05_L06_screen_scenarios.md) | A ring buffer→pytest→확장(~40%) · B 텔레메트리 로그 처리 · C 버그 있는 코드에 테스트 · D C로 · E 드라이버+mock · F LeetCode — 1시간 진행표 · 꼬리 질문 사다리 · 모의 문제 2개(실행 검증) |

> **[특별강의: ring buffer · pytest · Python LeetCode 15](lecture/2026-10-04_L00_special_lecture.md)** — Part 1 ring buffer 개념 → Part 2 pytest 개념 → Part 3 LeetCode Tier S(622 · 346 · 359 · 146) → A → B. 코드는 전부 pytest 37개 통과본 (`python3 lecture/build_lecture.py --test`).

## ⌨️ 매일 — 타이핑 드릴 (10-03 ~ 10-07, 아침 20분 + 저녁 20분)

| Day | 날짜 | 주제 | 정답 목록 |
|---|---|---|---|
| 1 | 10-03 (토) | 컨테이너 · 루프 · 문자열 / C 타입 · 포인터 · 비트 | [D1](drills/2026-10-03_D1_basics.md) |
| 2 | 10-04 (일) | 클래스 · 예외 · with · generator / C 메모리 · 함수 포인터 | [D2](drills/2026-10-03_D2_classes_memory.md) |
| 3 | 10-05 (월) | bytes · struct · 비트 필드 · CRC / C 엔디언 · 레지스터 | [D3](drills/2026-10-03_D3_bytes_bits.md) |
| 4 | 10-06 (화) | ring buffer · 스레드 · 상태 머신 / C ring · atomics | [D4](drills/2026-10-03_D4_ring_threads.md) |
| 5 | 10-07 (수) | pytest · fixture · mock / C 단위 테스트 · fake | [D5](drills/2026-10-03_D5_pytest_ctests.md) |

- **브라우저**: [드릴 트레이너](site/drills.html) — Day 탭 → 따라 치기(첫 회) → 기억해서 치기 → 비교 (줄 단위, 공백 무시) → ✓/✗ 기록 · 🎲 무작위 10 · ✗ 다시 볼 것
- **🧪 빈 파일 연습장**: [playground](playground/README.md) — 템플릿 없이 `playground/py/` · `playground/c/`에 처음부터 쓰고 `make py|test|c F=…`로 실행
- **에디터 (실제 실행·채점)**: `python3 drills/drill.py new 1` → `drills/workspace/day1/`의 20개 파일을 채우고 → `python3 drills/drill.py check 1`
- 저녁엔 그날 ✗ 항목 + 전날 ✗ 항목. **10-06(화) 저녁 기준**: Day 4 P4-01(ring buffer)을 15분 안에 못 치면 스크린에서 ring buffer는 C로 하겠다고 먼저 제안

## ⏱️ 인터뷰까지 30분 남았다면 — 당일 직전 코스

| 순서 | 시간 | 볼 것 | 왜 |
|---|---|---|---|
| 1 | 5분 | [P00 게임 플랜](plan/2026-10-02_P00_tech_screen_game_plan.md) — "기억할 다섯 줄" | 전체 틀 |
| 2 | 10분 | [문제 01 ring buffer](python/problems/01_ring_buffer.md) — 빈 파일에 핵심만 | 손 풀기 |
| 3 | 10분 | [N05 말하면서 코딩](notes/2026-10-02_N05_live_coding_english.md) — 1절 자기소개, 2절 대사 | 첫인상과 진행 |
| 4 | 5분 | [N05](notes/2026-10-02_N05_live_coding_english.md) 6절 역질문 3개 메모 | 마지막 5분 |

## ⏱️ 2~3시간 — 핵심 코스

| 순서 | 시간 | 볼 것 | 왜 |
|---|---|---|---|
| 1 | 15분 | [N01 인터뷰어와 형식](notes/2026-10-02_N01_interviewer_and_format.md) — 1절 공고 표, 3절 1시간 예상 | 누가 무엇을 볼지 |
| 2 | 30분 | ★ [문제 01](python/problems/01_ring_buffer.md) starter로 → [N03](notes/2026-10-02_N03_ring_buffer_python_c.md) 2절 Python↔C 표 | 확정 주제 |
| 3 | 40분 | ★ [N02 pytest 실전](notes/2026-10-02_N02_pytest_in_practice.md) 1~2절 → [문제 02](python/problems/02_pytest_ring_buffer.md) mutant 9/9 | 확정 주제 |
| 4 | 30분 | ★ [N04 system design](notes/2026-10-02_N04_system_design_test_tooling.md) — 0절 10분 틀, 3절 CI 결과 대시보드 | Full Stack의 홈그라운드 |
| 5 | 20분 | [N05](notes/2026-10-02_N05_live_coding_english.md) 2~4절 | 말과 Python 습관 |

## ⏱️ D-6 전체 코스

1. **큰 그림**: [P00 게임 플랜](plan/2026-10-02_P00_tech_screen_game_plan.md) → [N01 인터뷰어와 형식](notes/2026-10-02_N01_interviewer_and_format.md)
2. **ring buffer**: [문제 01](python/problems/01_ring_buffer.md) → [N03 Python · C · 꼬리 질문](notes/2026-10-02_N03_ring_buffer_python_c.md) → [문제 05 thread-safe](python/problems/05_blocking_ring.md) → [문제 06 stream framer](python/problems/06_stream_framer.md)
3. **pytest**: [N02 pytest 실전](notes/2026-10-02_N02_pytest_in_practice.md) → [문제 02 테스트 쓰기](python/problems/02_pytest_ring_buffer.md) → [문제 03 mock](python/problems/03_mock_serial_driver.md) → [문제 04 HIL conftest](python/problems/04_hil_conftest.md)
4. **Python/C 경계**: [문제 07 ctypes로 C 링버퍼 테스트](python/problems/07_ctypes_c_ring_buffer.md)
5. **system design**: [N04 테스트 프레임워크 · HIL 팜 · CI 결과 대시보드](notes/2026-10-02_N04_system_design_test_tooling.md) → [S00 직군별 테스트 지도](design/2026-10-03_S00_overview.md) → S01 Flight · S08 통합/릴리즈 · S07 공장 → [S09 시나리오 카드 16장](design/2026-10-03_S09_scenario_cards.md) 매일 2장
6. **말하기와 당일**: [N05 말하면서 코딩 · 역질문 · 체크리스트](notes/2026-10-02_N05_live_coding_english.md)

## 🗂️ 자료 지도 — 새로 만든 것 vs 재사용 (복제 안 함)

| 주제 | 이 폴더 | 재사용 (링크) |
|---|---|---|
| 면접관 · 형식 | [N01](notes/2026-10-02_N01_interviewer_and_format.md) | [Neros 컨텍스트 파일](../neros_tech_senior_firmware_engineer_context.html) §5 · §6 |
| pytest | [N02](notes/2026-10-02_N02_pytest_in_practice.md) · 문제 02 · 03 · 04 | [FTE N04 pytest 프레임워크](../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html) · [FTE 문제 04 시리얼 드라이버](../firmwareTestEngineerPrep/site/problems/04_serial_dut_driver.html) · [FTE 문제 08 retry](../firmwareTestEngineerPrep/site/problems/08_retry_and_wait.html) |
| ring buffer (Python) | [N03](notes/2026-10-02_N03_ring_buffer_python_c.md) · 문제 01 · 05 · 06 | — |
| ring buffer (C) | 문제 07 (ctypes로 테스트) | [onsitePrep N05 플레이북](../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html) · [C 코드 5종](../onsitePrep/site/code/index.html) · [링버퍼 완전정복](../research/ringbuffer_research/html/index.html) |
| system design | [N04](notes/2026-10-02_N04_system_design_test_tooling.md) | [onsitePrep N06 (9절 HIL 팜)](../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.html) · [FTE N02 HIL](../firmwareTestEngineerPrep/site/notes/2026-09-28_N02_hil_for_drones.html) · [FTE N05 CI/Git](../firmwareTestEngineerPrep/site/notes/2026-09-28_N05_ci_cd_git.html) |
| Python 개념 · 습관 | [N05](notes/2026-10-02_N05_live_coding_english.md) 4절 · [N06 Python 스레드 vs C (GIL 실측)](notes/2026-10-03_N06_python_threads_vs_c.md) | [FTE N03 Python for test automation](../firmwareTestEngineerPrep/site/notes/2026-09-28_N03_python_for_test_automation.html) · [FTE Python 문제 12개](../firmwareTestEngineerPrep/site/coding.html) |
| 경험 스토리 | [N05](notes/2026-10-02_N05_live_coding_english.md) 1절 | [FTE N09 레쥬메 스토리라인 13개](../firmwareTestEngineerPrep/site/notes/2026-09-29_N09_resume_storylines.html) · [FTE N07 스토리와 영어](../firmwareTestEngineerPrep/site/notes/2026-09-28_N07_stories_and_english.html) |
| 온사이트 (다음 단계) | — | [onsitePrep START HERE](../onsitePrep/index.html) |

## 🏗️ 직군별 펌웨어 테스트 설계 (system design 대비)

| # | 직군 | 대표 테스트베드 |
|---|---|---|
| S00 | [전체 지도 · 공통 부품 키트 · 답변 틀](design/2026-10-03_S00_overview.md) | 직군 × 테스트 레벨 매트릭스 |
| S01 | [Flight Software](design/2026-10-03_S01_flight_software.md) | SITL + HITL(센서 주입 · DShot 캡처) · thrust stand |
| S02 | [Radio link & Connectivity](design/2026-10-03_S02_radio_link_connectivity.md) | 차폐 박스 + 감쇠기 + 채널 에뮬레이터 + SDR 재머 |
| S03 | [Ground & Peripherals](design/2026-10-03_S03_ground_and_peripherals.md) | 스틱 DAC 주입 · 화면 캡처 · 기준 기체 |
| S04 | [Video / FPGA pipeline](design/2026-10-03_S04_video_fpga_pipeline.md) | glass-to-glass 지연 리그 · 패턴 CRC · 비트 오류 주입 |
| S05 | [Platform firmware](design/2026-10-03_S05_platform_firmware.md) | 빌드 매트릭스 · fuzz · 전원 차단 리그 · ABI/footprint 게이트 |
| S06 | [Embedded Linux & autonomy](design/2026-10-03_S06_embedded_linux_autonomy.md) | 부팅/OTA 팜 · 타이밍 벤치 · 로그 재생 |
| S07 | [Manufacturing · factory test](design/2026-10-03_S07_manufacturing_factory_test.md) | 스테이션 4종 · takt 계산 · golden unit · GR&R |
| S08 | [System integration · release](design/2026-10-03_S08_system_integration_release.md) | iron bird · 버전 매트릭스 · 릴리즈 게이트 · 비행 로그 |
| S09 | [시나리오 카드 16장](design/2026-10-03_S09_scenario_cards.md) | 10분 타이머 연습용 |

## 💻 Python 문제 7개

| # | 주제 | 종류 | 채점 |
|---|---|---|---|
| 01 | [ring buffer 구현](python/problems/01_ring_buffer.md) | 구현 | 테스트 18개 |
| 02 | [pytest로 남의 ring buffer 테스트](python/problems/02_pytest_ring_buffer.md) | 테스트 작성 | **mutant 9개** 잡기 |
| 03 | [mock으로 시리얼 드라이버 테스트](python/problems/03_mock_serial_driver.md) | 테스트 작성 | **mutant 9개** 잡기 |
| 04 | [HIL 리그용 conftest.py](python/problems/04_hil_conftest.md) | 프레임워크 | pytester 채점기 5개 |
| 05 | [thread-safe blocking ring](python/problems/05_blocking_ring.md) | 구현 | 테스트 9개 |
| 06 | [바이트 스트림 → 프레임](python/problems/06_stream_framer.md) | 구현 | 테스트 13개 |
| 07 | [C ring buffer를 ctypes로 테스트](python/problems/07_ctypes_c_ring_buffer.md) | Python/C | 테스트 13개 (`cc` 필요) |

```bash
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/neros_tech_senior_firmware_engineer/technicalScreenPrep
python3 python/run.py 01          # 내 풀이 (python/starters/) — .venv 의 pytest 를 자동으로 씀
python3 python/run.py 02 --sol    # 모범답안 (python/solutions/)
python3 python/run.py all --sol   # 전체 → 요약: 01✓ 02✓ …
python3 build_notes_site.py       # md 고친 뒤 site/ 와 이 index.html 다시 만들기
```

- venv가 없으면: `python3 -m venv .venv && .venv/bin/pip install pytest`
- 테스트 작성 문제(02 · 03)는 **정상 구현에서 통과 + 버그 심은 mutant마다 실패**해야 점수. `lib/mutants/`는 다 푼 뒤에 열기

## 📌 인터뷰 직전 기억할 다섯 줄

- 시작은 **질문**: 타입 · 용량 · full 정책 · 동시성 · "Python or C?"
- ring buffer: `head` + `count`, tail은 계산, capacity 전부 사용, 정책 플래그 + `dropped`
- 다 짜면 **"Let me add tests"** → 0 · 1 · full · full+1 · wraparound · 예외, parametrize
- mock은 경계만(포트 · 시계), DI > patch, 실제 리그 테스트 1개는 남긴다
- design: 결과는 **데이터** — 멱등 업로드, infra vs product 분리, flip-rate flaky, MR 코멘트가 진짜 UI

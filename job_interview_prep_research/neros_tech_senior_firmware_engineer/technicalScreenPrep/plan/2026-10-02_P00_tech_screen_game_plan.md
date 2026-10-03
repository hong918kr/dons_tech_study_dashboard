# 🎯 P00 · 게임 플랜 — 10-08(목) 11am technical screen까지 D-6

> 2026-10-02 리크루터 연락: Michael Honor 인터뷰가 좋았고, **온사이트 전에 technical screen 1세션**을 추가한다. 2026-10-08(목) 11:00, 1시간, **코딩 위주** 예상. 주제는 **pytest · ring buffer · Python/C 둘 다 가능 · system design**. 면접관 Jon Kotowski — 공개 검색상 Neros Full Stack Engineer [추정, 데이터 브로커 출처]. 6일 동안 ① ring buffer를 Python으로 12분, C로 5분 ② pytest로 "남의 코드 테스트"를 25분 ③ 테스트 인프라 system design을 10분 요약, 이 세 가지를 손과 입에 붙인다.

## 이 인터뷰가 무엇인가

- **경로**: Adam Kibit(09-18, Director of FW) → FW Test 전환(09-27) → Michael Honor(10-01, FW Test 리드, 퀴즈형, Python 미출제) → **이번 technical screen(10-08)** → 온사이트(Tour · 발표 1h · 1:1 C/C++ · ring buffer · system design) [확인됨 리크루터들]
- **왜 추가됐나** [추정]: Michael 라운드에서 비어 있던 **코딩 · Python · pytest**를 확인하고, 온사이트 비용을 쓰기 전에 거르려는 것. 긍정 신호지만 **통과해야 온사이트**
- **면접관 관점** [추정]: Full Stack → 임베디드 퀴즈보다 코드 품질 · 테스트 · 설계 소통. 자세히: [N01 인터뷰어와 형식](../notes/2026-10-02_N01_interviewer_and_format.md)

## 1시간 예상과 배분 [추정]

| 시간 | 내용 | 내가 할 것 |
|---|---|---|
| 0–5 | 인사 · 자기소개 | 45초 버전 ([N05](../notes/2026-10-02_N05_live_coding_english.md) 1절) — web 플랫폼 + SDK 강조 |
| 5–30 | 코딩 1 · ring buffer | 질문 2분 → Python 12분 → 손 시뮬레이션 → 꼬리(thread-safe, deque, C 버전) |
| 30–45 | 코딩 2 · pytest | 방금 짠 것 또는 주어진 코드에 fixture · parametrize · raises · mock |
| 45–55 | system design | 테스트 프레임워크 / HIL 팜 / CI 결과 대시보드 중 하나를 10분 틀로 |
| 55–60 | 역질문 | Full Stack용 3개 |

## 핵심 메시지

> "I write the C that runs on the device and the Python that proves it works — and I've built test tooling other engineers depended on."

- 코딩으로 증명: **구현 → 내가 먼저 테스트 → 꼬리 질문에서 C와 동시성**
- 설계로 증명: 테스트 결과를 **데이터**로 다룬다 (구조화 결과, flaky 점수, infra vs product 실패 분리)

## 매일 고정 (10-03 갱신)

| 시간 | 할 일 | 자료 |
|---|---|---|
| 아침 20분 | 그날 Day 드릴 Python 10 + C 10 — 정답 안 보고 | [드릴 트레이너](../site/drills.html) · `python3 drills/drill.py new N` |
| 저녁 20분 | ✗ 항목 다시 + 전날 ✗ | 트레이너 "✗ 다시 볼 것" 탭 |
| 저녁 10분 | 시나리오 카드 무작위 1장, 10분 타이머 | [S09](../design/2026-10-03_S09_scenario_cards.md) |
| **10-06 저녁 판단** | P4-01 ring buffer를 15분 안에 못 치면 → 스크린에서 "ring buffer는 C로, 테스트는 pytest로" 제안 | [N05](../notes/2026-10-02_N05_live_coding_english.md) 2절 |

- 드릴 Day ↔ 날짜: D1 10-03 · D2 10-04 · D3 10-05 · D4 10-06 · D5 10-07
- system design은 N04에 더해 **직군별 테스트 설계** [S00](../design/2026-10-03_S00_overview.md) → S01 → S08 → S07 순서로 (월 10-05 블록에 포함)

## D-6 일정

### 금 10-02 (D-6) — 셋업 + 큰 그림 (1.5시간)

| 시간 | 할 일 | 자료 |
|---|---|---|
| 20분 | 면접관 · 형식 분석 읽기, 공고 표에서 "이 사람의 일상 도구" 5개 | [N01](../notes/2026-10-02_N01_interviewer_and_format.md) |
| 10분 | venv 확인: `python3 python/run.py all --sol` 전부 ✓ | [START HERE](../START_HERE.md) 채점 명령 |
| 60분 | ★ [문제 01](../python/problems/01_ring_buffer.md) starter로 풀기 (15분 타이머, 말하면서) → [N03](../notes/2026-10-02_N03_ring_buffer_python_c.md) 1~2절 | ring buffer |

### 토 10-03 (D-5) — pytest 손 (3시간)

| 시간 | 할 일 | 자료 |
|---|---|---|
| 40분 | pytest 문법 · mock 패턴 · 15분 순서 | [N02](../notes/2026-10-02_N02_pytest_in_practice.md) 1~3절 |
| 50분 | ★ [문제 02](../python/problems/02_pytest_ring_buffer.md) 25분 타이머 → mutant 9/9 될 때까지 | pytest |
| 50분 | ★ [문제 03](../python/problems/03_mock_serial_driver.md) 25분 타이머 → mutant 9/9 | mock |
| 40분 | 개념 복습(필요한 절만) | [FTE N04 pytest](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html) 4·5·10절 |

### 일 10-04 (D-4) — ring buffer 확장 + C (2.5시간)

| 시간 | 할 일 | 자료 |
|---|---|---|
| 40분 | [문제 05 blocking ring](../python/problems/05_blocking_ring.md) | 동시성 꼬리 질문 |
| 30분 | C 버전 종이에 5분 × 2회, 꼬리 질문 영어 답 | [N03](../notes/2026-10-02_N03_ring_buffer_python_c.md) 3~4절 · [onsitePrep N05](../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html) 8절 |
| 40분 | [문제 06 stream framer](../python/problems/06_stream_framer.md) | "UART chunk → frame" 꼬리 |
| 30분 | `make -C ../onsitePrep/code test` 돌리고 SPSC 코드 읽기 | [ring_buffer_spsc.c](../../onsitePrep/site/code/ring_buffer_spsc.c.html) |

### 월 10-05 (D-3) — system design (2.5시간)

| 시간 | 할 일 | 자료 |
|---|---|---|
| 30분 | 10분 틀 · 문제 A 프레임워크 | [N04](../notes/2026-10-02_N04_system_design_test_tooling.md) 0~1절 |
| 50분 | ★ 문제 C CI 결과 대시보드 — 스키마 · API · flaky를 종이에, 영어 요약 소리 내기 | N04 3절 |
| 40분 | 문제 B HIL 팜 + onsite N06 9절 합치기 | N04 2절 · [onsitePrep N06](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.md) |
| 30분 | [문제 04 HIL conftest](../python/problems/04_hil_conftest.md) — 설계를 코드로 | conftest |

### 화 10-06 (D-2) — 모의 인터뷰 1회 (2시간)

| 시간 | 할 일 |
|---|---|
| 60분 | **풀 리허설**: 자기소개 → 문제 01 (빈 파일) → 테스트 → 문제 05 말로 → N04 문제 C 10분 → 역질문. 녹음해서 들어 보기 |
| 30분 | 리허설에서 막힌 곳만 다시 |
| 30분 | [N05](../notes/2026-10-02_N05_live_coding_english.md) 2~4절 대사 · C 습관 표 |

### 수 10-07 (D-1) — 가볍게 (1시간)

- [ ] 문제 01 · 02를 빈 파일로 한 번씩 (각 12분 / 20분)
- [ ] [문제 07 ctypes](../python/problems/07_ctypes_c_ring_buffer.md) 해설만 읽기 — "C를 Python으로 테스트" 한 문단 말하기
- [ ] 역질문 3개 확정, 일찍 자기

### 목 10-08 (D-day) — 당일

- [N05 7절 당일 체크리스트](../notes/2026-10-02_N05_live_coding_english.md)

## 우선순위 (시간이 모자라면 ★만)

| 순위 | 항목 | 이유 |
|---|---|---|
| ★1 | 문제 01 Python ring buffer + 테스트 | 리크루터가 이름을 짚은 주제 + 온사이트에도 나옴 |
| ★2 | 문제 02 pytest로 테스트 쓰기 | 리크루터 주제 1번, 테스트 팀 정체성 |
| ★3 | N04 문제 C (CI 결과) 10분 요약 | Full Stack 면접관의 홈그라운드 [추정] |
| 4 | 문제 03 mock | pytest 꼬리 질문 |
| 5 | 문제 05 thread-safe | ring buffer 꼬리 질문 |
| 6 | C 버전 5분 | "Python/C 둘 다" |
| 7 | 문제 06 · 04 · 07 | 여유 있으면 |

## 기억할 다섯 줄

- 시작은 **질문**: 타입 · 용량 · full 정책 · 동시성 · 언어 선택
- ring buffer: `head` + `count`, tail은 계산. capacity **전부** 사용. full 정책은 플래그 + `dropped`
- 다 짜면 **"Let me add tests"** → 0 · 1 · full · full+1 · wraparound · 예외 · parametrize
- mock은 **경계만**(포트 · 시계), DI가 patch보다 낫다. 한 개는 실제 리그에서
- system design: 결과는 **데이터** — 구조화 업로드(멱등), infra vs product 분리, flip-rate flaky, MR 코멘트가 진짜 UI

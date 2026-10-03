# 🕵️ N01 · 인터뷰어와 형식 — Full Stack 엔지니어가 FW Test 후보를 볼 때

> 10-08(목) 11am 1시간 technical screen의 면접관 **Jon Kotowski**는 공개 데이터 브로커 기준 Neros **Full Stack Engineer**다 [추정: Datanyze · ZoomInfo 검색 스니펫, 둘 다 데이터 브로커]. 펌웨어 사람이 아닌 면접관이 FW Test 후보를 본다는 건, 임베디드 퀴즈보다 **코드 품질 · 테스트 · 설계 소통**을 볼 가능성이 크다는 뜻이다 [추정]. 이 노트는 그 사람의 관점에서 1시간을 예상한다.

## 0. 확정된 것과 추정한 것

| 항목 | 내용 | 신뢰도 |
|---|---|---|
| 일시 | 2026-10-08(목) 11:00, 1시간 | [확인됨 2026-10-02 리크루터] |
| 위치 | 온사이트 **전에** 추가된 technical screen 1세션 | [확인됨] |
| 형식 | **코딩 위주** 예상 | [확인됨 리크루터 표현 "코딩 위주 예상"] |
| 주제 | pytest · ring buffer · Python/C 둘 다 가능 · system design | [확인됨 리크루터] |
| 면접관 | Jon Kotowski | [확인됨 리크루터] |
| 직함 | Full Stack Engineer @ Neros | [추정: Datanyze 단일 출처 + ZoomInfo 스니펫] |
| 경력 | 과거 Hoonigan Software Engineer, Silvertrac Software 모바일 앱 개발(2019–2022) 등 | [추정: ZoomInfo 스니펫, 동명이인 가능성] |
| LinkedIn | 로그인 조회 · 친구 신청 **하지 않음** (Don 지시) | — |

- 바로 전 라운드(10-01 Michael Honor, FW Test 리드)는 **임베디드 퀴즈형**이었고 Python은 안 나왔다 → 이번 라운드가 그 빈칸(Python · pytest · 코딩)을 채우는 자리로 보인다 [추정]
- 온사이트 전에 세션을 하나 더 넣었다 = 온사이트 비용(Tour + 발표 1h + 1:1 여러 개)을 쓰기 전에 **코딩 실력을 한 번 더 확인**하려는 것 [추정]. 긍정 신호로 읽되, 통과해야 온사이트

## 1. Neros의 Full Stack 엔지니어는 무엇을 하나 — 공고로 본 그림

Neros 공고 **Full-Stack Software Engineer III** (Greenhouse 5174689007, 2026-09-30 갱신, base $152.5–213.5K) [확인됨 공고]:

| 공고 문구 | 이 사람이 면접에서 볼 법한 것 [추정] |
|---|---|
| "critical internal and customer-facing software **tools**" | 테스트 **툴**도 결국 internal tool. "누가 쓰나, 어떻게 배포하나"를 묻는 감각 |
| React / React Native, Node.js, TypeScript, "**linting and testing frameworks**" | 테스트를 당연하게 여긴다. 코드에 테스트가 없으면 감점 |
| Python; **Django, Flask** | Python 코딩 라운드를 직접 진행할 수 있다. pytest는 그 사람의 일상 도구 |
| SQL / NoSQL, Redis, ORM, **time-series DB** | system design에서 "결과를 어디에 저장하나", "스키마는?"까지 내려올 수 있다 |
| distributed systems, **event-driven**, microservices | "테스트 결과 이벤트를 어떻게 모으나" 같은 데이터 흐름 질문 |
| CI (GitHub Actions, GitLab CI, Jenkins), Docker/K8s | "이 테스트를 CI에서 어떻게 돌리나" |
| Nice: **InfluxDB, Grafana**, telemetry, Bazel, FPV | 텔레메트리 대시보드 · 테스트 결과 대시보드는 이 사람의 영역 |

형제 공고(같은 날 스냅샷)도 같은 방향이다 [확인됨 공고]:

- **Lead, Software Application**: 운용자·엔지니어가 쓰는 web/desktop/Android 툴과 백엔드, "record and analyze **flight data**", 데이터레이크, **offline-first**
- **Ground Software Manager**: GCS, 텔레메트리, "partnering with the **Test organization** on shared infrastructure"
- **Principal SWE, Manufacturing Systems**: MES, serialization, genealogy, 감사 추적 — **factory test 결과가 흘러 들어갈 곳**

> 결론 [추정]: Jon은 **테스트 조직이 만든 데이터와 툴을 받아 쓰는 쪽**이거나, 테스트 인프라의 웹/백엔드 부분(결과 DB, 대시보드, 리그 예약)을 함께 만들 사람일 수 있다. FW Test 엔지니어가 들어오면 **같이 일할 동료**로서 "이 사람 코드를 내가 리뷰하고 싶은가"를 볼 것이다.

## 2. 이 면접관이 채점할 것 (추정 루브릭)

| 축 | 무엇을 보나 | Don이 보여 줄 것 |
|---|---|---|
| 코드 품질 | 이름, 함수 크기, 읽히는 구조, 관용적 Python | dataclass, generator, context manager, type hint 적당히. C 습관(인덱스 루프, 수동 길이 관리) 줄이기 → [N05](2026-10-02_N05_live_coding_english.md) 4절 |
| 테스트 | 테스트를 **스스로** 쓰나, 무엇을 경계로 보나 | 구현 후 "Let me add tests" → 경계 · wraparound · 예외 · parametrize |
| 문제 정의 | 바로 코딩 vs 질문 먼저 | 첫 2분은 질문: 타입, 용량, 정책, 동시성, 입력 크기 |
| 소통 | 생각을 말로, 막혔을 때 | 계획 → 구현 → 테스트 순서를 미리 말하기. 침묵 30초 이상 금지 |
| 설계 | 요구사항 → 구성 요소 → 데이터 흐름 → trade-off | [N04](2026-10-02_N04_system_design_test_tooling.md) 6단계 틀 |
| 도메인 다리 | FW·HW 지식을 웹 사람에게 번역 | "리그 = 비싼 공유 자원 = DB 커넥션 풀처럼", "DUT 로그 = 서비스 로그" |

- **반대로 덜 볼 것** [추정]: PCIe/8b10b 같은 HW 퀴즈, 레지스터 비트, 드론 도메인 세부. 나와도 깊게는 안 판다
- **위험**: 면접관이 C를 잘 모르면 C로 답할 때 평가가 어려워진다 → **기본은 Python**, C는 "the embedded version would…"로 짧게 비교. 단, ring buffer를 C로 써 달라고 하면 바로 쓸 수 있게 [onsitePrep C 코드](../../onsitePrep/site/code/index.html) 준비

## 3. 1시간 진행 예상

| 시간 | 예상 내용 | 비중 | 대응 자료 |
|---|---|---|---|
| 0–5분 | 인사, 자기소개 짧게, 면접관 소개 | 낮음 | [N05](2026-10-02_N05_live_coding_english.md) 1절 45초 버전 |
| 5–30분 | **코딩 1: ring buffer** (Python 또는 C) → 꼬리 질문(thread-safe, full 정책, 복잡도) | **높음** | [문제 01](../python/problems/01_ring_buffer.md) · [05](../python/problems/05_blocking_ring.md) · [N03](2026-10-02_N03_ring_buffer_python_c.md) |
| 30–45분 | **코딩 2: pytest** — 방금 짠 것의 테스트, 또는 주어진 코드/드라이버 테스트(mock, fixture) | **높음** | [문제 02](../python/problems/02_pytest_ring_buffer.md) · [03](../python/problems/03_mock_serial_driver.md) · [N02](2026-10-02_N02_pytest_in_practice.md) |
| 45–55분 | **system design** 짧게 — 테스트 프레임워크 / HIL 팜 / CI 결과 대시보드 | 중간 | [N04](2026-10-02_N04_system_design_test_tooling.md) |
| 55–60분 | 역질문 | 중간 | [N05](2026-10-02_N05_live_coding_english.md) 6절 |

- 변형 A [추정]: 코딩 하나를 길게(35분) — ring buffer 구현 → 테스트 → 동시성 → 스트림 파싱까지 한 줄기로. 그러면 [문제 06](../python/problems/06_stream_framer.md)이 후반부
- 변형 B [추정]: system design이 20분 이상 — Full Stack 면접관의 홈그라운드라 오히려 길어질 수 있다. "결과 저장 · API · 대시보드"까지 그릴 준비
- 도구 [추정]: CoderPad / HackerRank / 공유 IDE. **실행 가능한 환경이면 실제로 테스트를 돌린다**. 실행 불가 환경이면 "I'd run this with pytest" 하고 예상 출력을 말로

## 4. Don의 포지셔닝 — 이 면접관에게 할 한 문장

> "I'm a firmware engineer who builds test infrastructure — I write the C that runs on the device and the Python that proves it works, and I've built tooling other engineers depend on."

- 증거: SK hynix 챔버 테스트 **web 플랫폼 + SDK** (온도·스케줄링·eSSD 상태·UART 시퀀스, 다른 엔지니어가 사용) — **Full Stack 면접관과 공통 언어가 가장 많은 경력**. 웹 플랫폼의 구체 스택은 (Don: 프론트/백엔드/DB 무엇이었는지 채우기 — 레쥬메에 없음)
- 증거: Apple factory test-node 아키텍처, Solidigm production FW + error reporting/handling scheme
- **지어내지 말 것**: React/Django 경험은 레쥬메에 없다. "I'm not a web developer, but I've built and maintained a web-based test platform that other teams used" 수준까지만

## 5. 이 면접관에게 하면 좋은 질문 3개 (역질문 후보)

- "How does the software team consume data from firmware test today — logs, a database, dashboards? Where would a test engineer's output land?"
- "What does the boundary between Test and the application/backend team look like — who owns the test-result pipeline?"
- "What's one piece of internal tooling you wish existed for the test or factory side?"

## 체크

- [ ] 1절 공고 표를 보고 "이 사람이 일상적으로 쓰는 것" 5개를 입으로 말해 보기
- [ ] 2절 루브릭 6축 중 내가 약한 2개 고르기 → N05로 보강
- [ ] 4절 한 문장을 20초 안에 영어로
- [ ] SK hynix 웹 플랫폼의 실제 스택을 기억해서 (Don: …) 채우기

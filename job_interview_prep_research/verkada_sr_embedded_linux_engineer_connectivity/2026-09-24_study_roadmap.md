# 스터디 로드맵 — 어디서부터 어디까지

> **상황**: 2026-09-25 Verkada 1차 기술 인터뷰 (2파트: problem solving + system design)
> **쓰는 법**: 위에서부터 한 단계씩. 각 단계의 **파란 링크를 누르면 읽어야 할 그 장으로 바로** 간다. 다 하면 "이 장 완료 표시"를 눌러 진도를 남긴다.
> **오늘 밤은 1장(필수 코스)만 끝내면 된다.** 2장은 여유 있을 때, 3장은 내일 아침, 5장은 인터뷰 이후 장기 코스다.

---

## 0장. 파일이 어디 있고 어떻게 여나

모든 자료는 한 폴더 안에 있다.

```sh
/Users/donh/workspace/dons_tech_study_dashboard/job_interview_prep_research/verkada_sr_embedded_linux_engineer_connectivity/
```

### 가장 쉬운 방법 — 두 가지

1. **Finder에서 `START_HERE.command` 더블클릭** → 기본 브라우저에 전체 목차가 열린다.
2. 터미널에서:

```sh
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/verkada_sr_embedded_linux_engineer_connectivity
open index.html          # 전체 목차 (여기서 다른 모든 문서로 이동)
open 2026-09-24_study_roadmap.html   # 이 로드맵
```

한 번 브라우저에서 열면 **그다음부터는 문서 안의 링크를 클릭**해서 이동하면 된다. 즐겨찾기에 `index.html` 하나만 등록해 두면 충분하다.

### 문서 목록

| 문서 (클릭하면 열림) | 무엇 | 파일명 |
|---|---|---|
| [🏠 전체 목차](index.html) | 모든 자료로 가는 입구 | `index.html` |
| **이 로드맵** | 읽는 순서 | `2026-09-24_study_roadmap.md/.html` |
| [🎯 예상 문제 (D-1)](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html) | 내일 볼 핵심 문서 | `2026-09-24_verkada_sep_25_2026_..._1st_tech_interview_prep.md/.html` |
| [🧭 학습 가이드 (12장)](2026-09-19_verkada_concurrency_study_guide.html) | 개념을 처음부터 | `2026-09-19_verkada_concurrency_study_guide.md/.html` |
| [⚡ 빈출 10문제](verkada_concurrency_top10.html) | 문제별 깊은 해설 | `verkada_concurrency_top10.md/.html` |
| [📚 노트 허브](notes_site/index.html) | 기초 11 + 복습 8편 | `notes_site/index.html` |
| [🧩 문제 은행](verkada_prep/index.html) | 78문제 드릴 대시보드 | `verkada_prep/index.html` |
| [💻 코드 브라우저](notes_site/code/index.html) | C 파일 36개 읽기 | `notes_site/code/index.html` |
| [📋 회사 컨텍스트](verkada_sr_embedded_linux_engineer_connectivity_context.html) | 회사·적합도·역질문 | `..._context.md/.html` |

손으로 코드를 짜는 건 터미널에서 한다.

```sh
cd concurrency_practice && make run N=01     # 통짜 문제 10개 (면접 시뮬레이션)
cd verkada_prep && make prob N=02_condvar_queues   # 세분화 문제 78개 (드릴)
```

---

## 1장. 오늘 밤 필수 코스 (약 2시간 30분)

> 이것만 끝내면 내일 볼 준비는 된 것이다. 순서를 바꾸지 말 것 — 뒤 단계가 앞 단계를 전제한다.

### 단계 1 · 세션이 어떻게 흘러가는지 (15분)

- [ ] [예상 문제 1장 — 세션 예상 구조와 첫 3분](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#1장-세션-예상-구조와-첫-3분) 전체

**얻어야 할 것**: 시작하자마자 물을 **질문 3개**를 외운다. 코딩 전 30초 설계 선언 문장을 한 번 소리 내어 읽는다.

### 단계 2 · bounded queue를 손으로 (45분) ★ 오늘 밤의 핵심

- [ ] 타이머 45분 켜고, 빈 파일에서 직접 구현 → `cd concurrency_practice && make run N=01`
- [ ] 막히면 그때만 [예상 문제 2장 P1의 모범 구현](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#2장-tier-1-거의-확실히-나오는-3문제)을 본다

**얻어야 할 것**: 손이 기억하는 상태. 특히 `while` 조건에 `closed`가 들어가는 것, close에서 `broadcast` 두 번.

### 단계 3 · 같은 문제를 말하면서 한 번 더 (20분)

- [ ] 코드를 지우고 다시 짜되, 이번엔 **혼잣말로 내레이션**하면서
- [ ] [예상 문제 2장 P1의 follow-up 표](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#2장-tier-1-거의-확실히-나오는-3문제)의 질문 7개에 소리 내어 답하기

**얻어야 할 것**: "왜 while인가", "왜 broadcast인가", "소비자가 여럿이면?"에 막힘없이 답하기.

### 단계 4 · 더블 버퍼링 (20분)

- [ ] [예상 문제 2장 P2](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#2장-tier-1-거의-확실히-나오는-3문제) 읽기
- [ ] 소유권 3상태(FREE → WRITER → READY → READER)를 **종이에 직접 그리기**
- [ ] 개념이 흐릿하면 [학습 가이드 6장](2026-09-19_verkada_concurrency_study_guide.html#6장-실시간-데이터-처리-패턴)의 6.5~6.7만

**얻어야 할 것**: "락은 데이터가 아니라 소유권을 보호한다"를 그림과 함께 설명하기. 더블 vs 트리플 trade-off 한 문장.

### 단계 5 · 테스트 가능한 설계 (15분)

- [ ] [예상 문제 2장 P3](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#2장-tier-1-거의-확실히-나오는-3문제) 읽기
- [ ] "하드웨어 없이 어떻게 테스트하나" 3층 답변을 **소리 내어 1회**

**얻어야 할 것**: 의존성 주입 + 시계 주입 + 단위/통합/시스템 3층. 마지막에 챔버 자동화 경험을 붙이는 것까지.

### 단계 6 · 개념 속사포 (20분)

- [ ] [예상 문제 6장 — 속사포 25문항](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#6장-개념-속사포-나올-확률-높은-25문항) — **답을 먼저 말하고** 펼쳐 확인
- [ ] 틀린 문항 번호만 메모 → 내일 아침에 그 문항만 다시

**얻어야 할 것**: volatile, release/acquire, 데드락 4조건, 종료 5단계를 각 30초 안에.

### 단계 7 · 말하기 (10분)

- [ ] [예상 문제 7장 — 말하기](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#7장-말하기-그대로-쓸-문장들) 소리 내어 읽기 (특히 7.1 오프닝, 7.3 상황별 문장)

### ✅ 여기까지가 오늘 밤 필수. 자러 간다.

새 주제를 지금 시작하지 않는다. 잠이 시험 성적이다.

---

## 2장. 여유가 있으면 (+60분, 우선순위 순)

- [ ] (15분) [예상 문제 3장 — Tier 2 4문제](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#3장-tier-2-충분히-나올-수-있는-4문제) — 각 문제의 "핵심 한 줄"만
- [ ] (20분) [예상 문제 5장 — 시스템 설계 3제](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#5장-파트-b-시스템-설계-예상-3제) + 설계 A 흐름도를 종이에 그려보기
- [ ] (15분) 약한 개념만 [학습 가이드](2026-09-19_verkada_concurrency_study_guide.html)에서 골라 읽기 → 6장의 "증상별 찾아보기" 표 참고
- [ ] (10분) [예상 문제 4장 — Tier 3](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#4장-tier-3-나오면-반가운-4문제-실무형-코딩) 훑기

---

## 3장. 내일 아침 (35분)

- [ ] (5분) [치트시트 8장](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#8장-인터뷰-중-치트시트-한-화면) 전체 훑기
- [ ] (10분) bounded queue **골격만** 손으로 타이핑 (8.1 참고 없이 먼저, 그다음 대조)
- [ ] (5분) [말하기 7장](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#7장-말하기-그대로-쓸-문장들) 소리 내어
- [ ] (10분) [설계 5장](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#5장-파트-b-시스템-설계-예상-3제)의 7단계와 설계 A 흐름 1회
- [ ] (2분) 어제 틀린 속사포 문항만 다시
- [ ] (3분) [9장 — 역질문 5개와 환경 점검](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#9장-오늘-밤-내일-아침-직전-5분)

---

## 4장. 세션 직전 5분

- [ ] [치트시트 8장](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#8장-인터뷰-중-치트시트-한-화면)을 **다른 창에 따로 띄워두기**
- [ ] 첫 3분 질문 3개 암기 확인: 생산자·소비자 수 / 가득 차면 블록인가 drop인가 / 종료는 drain인가 discard인가
- [ ] 코딩 언어는 **C로 하겠다고 먼저 말하기** (C++도 가능하다고 덧붙이기)
- [ ] 종이·펜·물 준비, 에디터 폰트 키우기

---

## 5장. 인터뷰 이후 — 온사이트 대비 장기 코스

> 내일이 끝나면 이 순서로 넓힌다. 하루 2~3단계씩.

### 5.1 개념 (학습 가이드 12장, 약 100분)

- [ ] [0~2장 — 세션 이해 · 스레드 모델 · thread safety](2026-09-19_verkada_concurrency_study_guide.html#0장-시작하기-이-세션이-무엇을-보는가)
- [ ] [3~4장 — 뮤텍스 · 조건 변수](2026-09-19_verkada_concurrency_study_guide.html#3장-동기화-도구-①-뮤텍스-mutex)
- [ ] [5장 — atomic과 메모리 순서](2026-09-19_verkada_concurrency_study_guide.html#5장-동기화-도구-③-atomic과-메모리-순서)
- [ ] [6장 — 실시간 버퍼링 패턴](2026-09-19_verkada_concurrency_study_guide.html#6장-실시간-데이터-처리-패턴)
- [ ] [7장 — 모듈화·테스트 설계](2026-09-19_verkada_concurrency_study_guide.html#7장-모듈화-테스트-가능한-임베디드-소프트웨어)
- [ ] [8장 — 시스템 설계](2026-09-19_verkada_concurrency_study_guide.html#8장-시스템-설계-파트-도구를-조합하는-법)
- [ ] [9장 — Embedded Linux · 네트워킹 · 셀룰러](2026-09-19_verkada_concurrency_study_guide.html#9장-jd-보강-embedded-linux-네트워킹-셀룰러-최소-개념) ← Don의 최대 갭
- [ ] [10~11장 — 인터뷰 진행 · 최종 점검](2026-09-19_verkada_concurrency_study_guide.html#10장-인터뷰-당일-생각을-말로-보여주는-법)

### 5.2 손으로 (문제 은행 78문제, 세트별 40~70분)

터미널: `cd verkada_prep && make prob N=<세트>` · 해답: `make sol N=<세트>` · 레이스 검사: `make tsan N=<세트>`

- [ ] `02_condvar_queues` (10문제) ★ 최우선
- [ ] `04_realtime_buffers` (8문제) ★
- [ ] `07_modular_design` (10문제) ★
- [ ] `01_threads_mutex` (10문제)
- [ ] `03_atomics_lockfree` (10문제)
- [ ] `06_net_parsing` (10문제) — Connectivity 도메인
- [ ] `05_linux_io` (10문제) — 갭 보강
- [ ] `08_practical_dsa` (10문제)

문제 카드는 [문제 은행 대시보드](verkada_prep/index.html)에서 힌트 3단계와 함께 볼 수 있고, 소스는 [코드 브라우저](notes_site/code/index.html)에서 읽을 수 있다.

### 5.3 통짜 연습 (면접 시뮬레이션 10문제)

`cd concurrency_practice && make run N=NN` — 한 문제를 처음부터 끝까지 45분 안에.

- [ ] `N=01` bounded queue ★
- [ ] `N=03` double buffer ★
- [ ] `N=10` testable driver ★
- [ ] `N=02` SPSC ring · `N=06` thread pool · `N=05` drop policy
- [ ] `N=04` seqlock · `N=07` periodic timer · `N=08` config swap · `N=09` lock order

각 문제의 쉬운 해설은 [기초 노트](notes_site/index.html)에 같은 번호로 있다.

---

## 6장. 막혔을 때 — 증상별로 어디를 보나

| 증상 | 여기로 |
|---|---|
| "while인지 if인지 헷갈린다" | [학습 가이드 4장 (4.4)](2026-09-19_verkada_concurrency_study_guide.html#4장-동기화-도구-②-조건-변수-condition-variable) |
| "종료할 때 뭘 해야 하는지 순서가 안 떠오른다" | 같은 장 4.7, 또는 [예상 문제 8.5](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#8장-인터뷰-중-치트시트-한-화면) |
| "release/acquire가 뭔지 설명이 안 된다" | [학습 가이드 5장 (5.5)](2026-09-19_verkada_concurrency_study_guide.html#5장-동기화-도구-③-atomic과-메모리-순서) |
| "volatile로 되는 거 아니야?" | 같은 장 5.3 |
| "더블 버퍼 소유권이 헷갈린다" | [학습 가이드 6장 (6.5~6.6)](2026-09-19_verkada_concurrency_study_guide.html#6장-실시간-데이터-처리-패턴) |
| "데드락·우선순위 역전 설명이 막힌다" | [학습 가이드 3장 (3.4~3.5)](2026-09-19_verkada_concurrency_study_guide.html#3장-동기화-도구-①-뮤텍스-mutex) |
| "테스트 얘기를 어떻게 풀어야 할지" | [학습 가이드 7장 (7.6~7.8)](2026-09-19_verkada_concurrency_study_guide.html#7장-모듈화-테스트-가능한-임베디드-소프트웨어) |
| "설계 질문에서 어디서 시작할지" | [예상 문제 5장 5.0](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#5장-파트-b-시스템-설계-예상-3제) |
| "영어로 어떻게 말하지" | [예상 문제 7장](2026-09-24_verkada_sep_25_2026_senior_embedded_linux_engineer_connectivity_1st_tech_interview_prep.html#7장-말하기-그대로-쓸-문장들) |
| "회사·팀 얘기, 역질문" | [컨텍스트 파일](verkada_sr_embedded_linux_engineer_connectivity_context.html) |
| "문제를 더 풀고 싶다" | [문제 은행](verkada_prep/index.html) · [빈출 10문제](verkada_concurrency_top10.html) |

---

## 7장. 오늘 밤 진도 체크 (한눈에)

- [ ] 단계 1 — 세션 구조와 첫 3분 질문
- [ ] 단계 2 — bounded queue 45분 구현
- [ ] 단계 3 — 말하면서 재구현 + follow-up 답변
- [ ] 단계 4 — 더블 버퍼 그림
- [ ] 단계 5 — 테스트 3층 답변
- [ ] 단계 6 — 속사포 25문항
- [ ] 단계 7 — 말하기 문장
- [ ] 취침 (새 주제 시작 금지)

> 내일 아침에는 이 로드맵 **3장**부터 다시 열면 된다.

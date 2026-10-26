# 🏗️ R09 스케일러블 · 재사용 가능한 테스트 플랫폼 아키텍처
> 테스트 의도(WHAT) · 프로토콜/드라이버(HOW) · 벤치/자원(WHERE)을 갈라 놓고, DUT를 상속이 아니라 capability 조합으로 모델링하면, 기체 · 펌웨어 스택 · 벤치가 늘어나도 한 층만 고치면 된다. 같은 테스트 라이브러리를 책상 → CI HIL → nightly → release → 공장 → RMA까지 다시 쓰는 법, 그리고 데이터 · 벤치 운영 · 조직까지.

---

## 0. 이 단원 지도

이 단원은 Don이 직접 물은 질문에 대한 답이다. "어떤 식으로 테스트 인프라를 가지고 가야 스케일러블하게 리유저블한 플랫폼으로 자리 잡을 수 있는가."

아래 [R08](2026-10-24_R08_hil_bench_design_instruments.md)이 "벤치 한 대를 어떻게 짓나"라면, 이 단원은 "벤치 수십 대 · 기체 여러 종 · 펌웨어 스택 여러 개 · 엔지니어 수십 명이 함께 쓰는 시스템을 어떻게 짓나"다.

| 절 | 내용 | 한 줄 |
|---|---|---|
| 1 | 왜 중요한가 | Neros 공고 문구로 본 "플랫폼" 수요 |
| 2 | 원칙 | 흔들리면 안 되는 설계 원칙 10개 |
| 3 | 레이어 구조 | CI 트리거부터 하드웨어까지 9층 + 옆 기둥 |
| 4 | Gemini 상속 HAL 비판 | 상속 → capability 조합. 무엇을 바꾸면 어디만 고치나 |
| 5 | 프로토콜 어댑터 층 | CRSF/MSP/MAVLink/DShot 을 "같은 모양"으로 정규화 |
| 6 | 테스트 러너와 라이브러리 | pytest를 뼈대로 쓰는 법 · fixture = 층 |
| 7 | 수명주기 재사용 | 책상 · CI · soak · qual · 공장 EOL · RMA |
| 8 | CI/CD 통합 | 단계별 게이트 · runner tag · 벤치 풀 · flaky 정책 |
| 9 | 데이터 | 결과 스키마 · 시계열 · 로그 색인 · 회귀 탐지 · 추적성 |
| 10 | 벤치 fleet 운영 | 인벤토리 · 헬스체크 · 교정 · 원격 · 규모 계산 |
| 11 | 조직과 프로세스 | 내부 제품으로서의 플랫폼 · 로드맵 Phase 0~4 |
| 12 | Build vs Buy | labgrid · LAVA · OpenHTF · Robot · NI · dSPACE ... |
| 13 | 예제 코드 | `platform_sketch.py` 해설과 실행 결과 |
| 14 | SSD → 드론 | 챔버 플랫폼 SDK 경험을 드론 HIL 플랫폼으로 옮기기 |
| 15~18 | 체크리스트 · 해 볼 것 · 확인 안 된 것 · 링크 | |

예제 코드: [`../code/platform_sketch.py`](../code/platform_sketch.py) (표준 라이브러리만, 자체 테스트 포함).

---

## 1. 왜 중요한가 (FTE 관점)

### 1.1 공고가 말하는 것

Neros Greenhouse 스냅샷(2026-10-23, 109개 공고)에서 테스트 인프라 관련 문구를 뽑으면 이렇다. 짧게 원문 인용한다.

| 공고 | 원문 (짧게) | 이 단원과의 연결 |
|---|---|---|
| Firmware Test Engineer | "Build and enhance automated testing frameworks and tools" | 플랫폼 자체를 만드는 일 |
| Firmware Test Engineer | "Integrate automated tests into CI/CD pipelines to enable continuous testing" | 8절 CI 게이트 |
| Firmware Test Engineer | "Monitor the test results and ensure the stability of builds before releases" | 9절 결과 DB · 대시보드 |
| Firmware Test Engineer | "Hands-on experience building, setting up HIL test systems" | R08 + 10절 |
| Firmware Test Engineer | "Experience with a CI/CD tools - Gitlab CI, Jenkins" | 8절 |
| Firmware Test Engineer (nice) | "Experience with Bazel, Cmake, make or another embedded build system" | 8.4절 |
| Firmware Test Engineer (nice) | "Betaflight, ExpressLRS, PX4, Ardupilot, etc." | 4절: 이질적 스택을 한 플랫폼으로 |
| Manager, Autonomy Evaluation, Data & Test | "the regression and simulation infrastructure every software change is tested against" | 8절 |
| Manager, Autonomy Evaluation, Data & Test | "Own the release criteria ... the conditions under which a build is blocked" | 8절 게이트 정의 |
| Manager, Autonomy Evaluation, Data & Test | "multi-tier regression system spanning unit and module tests, open-loop replay ..., closed-loop simulation, and hardware-in-the-loop testing" | 3절 · 7절 |
| Senior Platform Engineer | "integration of a HIL bench into automated release gating" | 8절 |
| Senior Platform Engineer | "latency to be enforced as a release gate" | 9절 메트릭 회귀 |
| Senior Platform Engineer | "flight logs support deterministic replay" | 9절 아티팩트 |
| Full-Stack Software Engineer III | "Bazel build system" · "InfluxDB, Graphana" | 8.4절 · 9.3절 |
| Senior Data Platform Engineer | "relational database (PostgreSQL or equivalent)" | 9절 결과 DB |
| Electrical Engineer - Board Design and Test | "traceability systems, MES platforms, and test result databases" | 7절 공장 · 9절 |
| Senior DevOps Engineer | "CI/CD pipelines and runners in secure environments (e.g., GitLab CI)" | 8절 runner |
| Lead, Platform Software | "treating every other engineering team as your customer" | 11절 내부 제품 |

> 공고 표기 그대로 "Graphana" 라고 적혀 있다 (정식 표기는 Grafana).

### 1.2 그래서 무엇이 문제인가

- 기체가 여럿이다. Betaflight 포크 FPV, PX4 + Jetson 자율 변형, 라디오, ESC, GCS [추정 · 공고의 "Betaflight, PX4" 문구 근거]. 기체마다 스크립트를 따로 짜면 N개 기체 × M개 테스트 = N×M 개의 코드가 된다.
- 벤치는 비싸고 적다. 커밋은 많다. 누가 언제 어느 벤치를 쓰는지 정하는 장치가 없으면 사람이 슬랙으로 "B3 쓰는 중" 을 외치게 된다.
- HIL은 본질적으로 흔들린다(케이블, RF, 전원, 타이밍). 흔들림 관리가 없으면 CI 빨간불을 아무도 믿지 않는다.
- 결과가 흩어지면 "이 회귀가 언제부터?" 에 답 못 한다.
- 테스트팀이 새로 꾸려지는 중이라면 [추정 — Don이 온사이트에서 받은 인상], 지금 정하는 구조가 3년을 간다. 잘못된 추상화는 나중에 갈아엎기 가장 비싼 부채다.

### 1.3 플랫폼이 "자리 잡았다"의 정의

Don의 질문에 있는 "자리 잡는다"를 측정 가능한 말로 바꾸면:

| 신호 | 측정 방법 |
|---|---|
| 다른 팀이 스스로 테스트를 추가한다 | 테스트 코드 커밋 작성자 중 테스트팀 외 비율 |
| 새 기체 추가가 "설정 + 몇 개 어댑터" 로 끝난다 | 새 기체 bring-up 에서 플랫폼 코어 변경 줄 수 |
| CI 빨간불이 신뢰받는다 | 빨간불 중 "진짜 회귀" 비율 (flaky/infra 제외) |
| 릴리스 결정이 대시보드로 된다 | 릴리스 회의에서 쓰는 근거가 플랫폼 리포트인가 |
| 공장 · 필드 팀도 같은 걸 쓴다 | EOL/RMA 스테이션이 같은 테스트 라이브러리 사용 여부 |
| 벤치 가동률이 보인다 | 벤치별 utilization, MTTR(벤치 고장 → 복구) |

---

## 2. 설계 원칙 10개

| # | 원칙 | 한 줄 설명 | 어기면 생기는 일 |
|---|---|---|---|
| P1 | WHAT / HOW / WHERE 분리 | 테스트 의도 · 프로토콜/드라이버 · 벤치/자원을 다른 층에 둔다 | 기체 하나 늘면 테스트 전부 수정 |
| P2 | Test-as-code, Limits-as-data | 절차는 코드, 합격 기준은 데이터 (제품/단계별 파일) | 공장 limit 바꾸려고 코드 리뷰 + 재배포 |
| P3 | Firmware measures, host judges | 시간 민감한 측정은 펌웨어/계측기가, 판정은 호스트가 | Python 타이머로 µs 를 재는 착각 |
| P4 | 모든 결과는 추적 가능 | (fw sha, hw rev, dut serial, bench id, 계측기 교정, 테스트 코드 sha, limit 버전) | "이거 어느 빌드였지?" |
| P5 | Idempotent setup/teardown | 이전 테스트가 어떻게 끝났든 같은 출발점 | 앞 테스트 실패가 뒤 테스트 전부를 오염 |
| P6 | 무인 복구 | 벤치는 사람 없이 전원 사이클 · 재플래시 · 복구 | 밤새 첫 테스트에서 멈춘 벤치 |
| P7 | 결정론 먼저, 통계는 필요한 곳에 | 같은 입력 → 같은 결과를 먼저 만든다. RF/타이밍은 분포로 | 랜덤 실패를 "가끔 그래요" 로 넘김 |
| P8 | Capability 선언 기반 선택 | 테스트는 필요 능력을 선언, DUT 는 가진 능력을 선언 | if product == ... 가 테스트 곳곳에 |
| P9 | 안전이 기본값 | 프롭 없음 · 전류 제한 · E-stop · arm 은 명시적 허가 | 벤치 위 모터 사고 |
| P10 | 플랫폼은 제품 | API 안정성 · 버전 · 문서 · 온보딩 · 지원 채널 | 만든 사람만 쓰는 스크립트 더미 |

### 2.1 P1 — WHAT / HOW / WHERE

같은 "RC 끊기면 failsafe 가 150 ms 안에 걸려야 한다" 라는 테스트를 생각해 보자.

| 층 | 이 테스트에서 | 바뀌는 이유 |
|---|---|---|
| WHAT (의도) | arm → RC 끊기 → disarm/failsafe 확인 → 지연 측정 | 요구사항이 바뀔 때만 |
| HOW (방법) | Betaflight면 CRSF 송신 중단, PX4면 MAVLink RC override 중단 또는 RC 입력 차단 | 펌웨어 스택/프로토콜이 바뀔 때 |
| WHERE (자원) | 벤치 B3의 USB-UART `/dev/ttyUSB2`, PSU 채널 1, 로직 분석기 채널 0~3 | 벤치 배선이 바뀔 때 |

세 가지가 한 파일에 섞여 있으면, 셋 중 하나만 바뀌어도 그 파일을 고친다. 셋을 가르면 바뀐 층만 고친다. 이게 이 단원 전체의 뼈대다.

### 2.2 P2 — Limits-as-data

- 같은 테스트도 단계마다 기준이 다르다. 개발 중엔 느슨하게, release qual 엔 스펙대로, 공장 EOL 은 guard band 를 둔 더 좁은 값으로.
- 기준이 코드에 박혀 있으면 공장 엔지니어가 limit 하나 바꾸려고 FW 테스트팀에 PR 을 부탁해야 한다.
- 그래서 limit 은 `limits/<product>/<stage>.yaml` 같은 데이터 파일. 버전이 있고, 결과 레코드에 limit 버전이 같이 찍힌다.
- OpenHTF 가 이걸 언어 차원에서 지원한다. measurement 를 선언하고 spec(합격 범위)을 붙이면, 범위 밖 measurement 하나가 전체 테스트를 실패로 만든다 (OpenHTF README).

### 2.3 P3 — Firmware measures, host judges

- 호스트 Python 의 `time.time()` 으로 failsafe 지연을 재면 USB-UART 지연 · OS 스케줄링 · GIL 이 다 섞인다. ms 단위 판정엔 못 쓴다.
- 정답: 시간 민감한 값은 (a) 로직 분석기/오실로스코프 같은 계측기, 또는 (b) 펌웨어 내부 타임스탬프(blackbox, ULog, debug 카운터)가 잰다. 호스트는 그 값을 받아 limit 과 비교만 한다.
- 이건 Don이 SSD 에서 NVMe telemetry 디버그 기능을 만들던 사고방식과 같다. 장치가 스스로 상태를 기록하고, 호스트는 꺼내서 판단한다.
- 자세한 계측 방법은 [R08](2026-10-24_R08_hil_bench_design_instruments.md).

### 2.4 P4 — 추적성 키

결과 한 줄에 반드시 들어가야 하는 키:

| 키 | 예 | 없으면 |
|---|---|---|
| `fw_sha` | git commit sha (dirty 여부 포함) | 회귀 구간 찾기 불가 |
| `fw_build_id` | CI pipeline id / Bazel 결과 해시 | 같은 sha 다른 빌드 옵션 구분 불가 |
| `product` / `hw_rev` | `fpv_bf` / `FC-H7-revB` | 하드웨어 리비전 문제를 FW 로 오인 |
| `dut_serial` | 보드 시리얼 | 특정 보드 불량 추적 불가 |
| `bench_id` | `B3` | 벤치 문제를 FW 로 오인 |
| `instrument_cal` | PSU/스코프 모델 · 시리얼 · 교정 만료일 | 품질 감사에서 측정값 무효 |
| `test_sha` | 테스트 저장소 sha | 테스트가 바뀐 건지 FW 가 바뀐 건지 모름 |
| `limits_version` | `fpv_bf/release@v12` | 기준이 바뀐 건지 성능이 바뀐 건지 모름 |
| `run_id` / `trigger` | `nightly-2026-10-24` / `merge_request` | 어떤 게이트에서 나온 결과인지 모름 |
| `env` | 온도, 펌웨어 설정 해시(diff dump) | 설정 차이를 회귀로 오인 |

### 2.5 P5 · P6 — Idempotent 와 무인 복구

- setup 은 "지금 상태를 가정하지 않는다". 전원 off → on → 알려진 펌웨어 플래시 → 설정 초기화 → 헬스 체크.
- teardown 은 실패해도 실행된다 (`finally`). 그리고 두 번 불러도 안전해야 한다 (전원 off 를 두 번 해도 문제 없음).
- 복구 사다리: 소프트 리셋 → 전원 사이클 → 부트로더 강제 진입 후 재플래시 → SWD 로 mass erase 후 재플래시 → 벤치를 "unhealthy" 로 표시하고 풀에서 뺀 뒤 사람 호출.
- labgrid 의 strategy 가 정확히 이 개념이다. "off → bootloader → shell" 같은 상태 전이를 보드별로 구현한 driver (labgrid docs: "the board- or project-specific actions necessary to transition from one state to another").

### 2.6 P7 — 결정론 먼저

- SITL, 파서 단위 테스트, 리플레이는 결정론으로 만든다: seed 고정, 시뮬레이션 시간, 고정 입력 로그.
- HIL 은 완전한 결정론이 불가능하다. 그러면 "N 회 반복해서 분포로 판정" 한다. 예: failsafe 지연 p99 < 150 ms (100회).
- 결정론이 가능한 테스트에서 랜덤 실패가 나면 그건 버그다 (테스트든 FW 든). 통계로 덮으면 안 된다.

---

## 3. 레이어 아키텍처

### 3.1 전체 그림

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │ L9  CI 트리거 · 사람 트리거                                                   │
 │     GitLab MR / merge / schedule(nightly) / release tag / 수동(desk, RMA)     │
 └───────────────────────────────┬─────────────────────────────────────────────┘
                                 │ "fw=abc123, suite=smoke, product=fpv_bf"
 ┌───────────────────────────────▼─────────────────────────────────────────────┐
 │ L8  Orchestrator / Scheduler                                                │
 │     어떤 테스트를 어떤 제품 · 어떤 벤치 풀에서 → 큐 · 우선순위 · 타임박스 · 샤딩│
 └───────────────────────────────┬─────────────────────────────────────────────┘
 ┌───────────────────────────────▼─────────────────────────────────────────────┐ ┌──────────────────┐
 │ L7  Bench reservation (lock)                                                │ │ Inventory/Config │
 │     벤치 풀(label) · 획득/해제 · 교정 만료/unhealthy 제외 · 하트비트        │◄┤ DB: 벤치 · DUT · │
 └───────────────────────────────┬─────────────────────────────────────────────┘ │ 계측기 · 배선 ·  │
 ┌───────────────────────────────▼─────────────────────────────────────────────┐ │ 교정 · 제품 desc │
 │ L6  Test runner (pytest)                                                    │ └──────────────────┘
 │     수집 · fixture · marker · parametrize · junitxml · 플러그인(결과 업로드) │ ┌──────────────────┐
 └───────────────────────────────┬─────────────────────────────────────────────┘ │ Artifact store   │
 ┌───────────────────────────────▼─────────────────────────────────────────────┐ │ fw .elf/.hex ·   │
 │ L5  Test library (시나리오 = WHAT)                                          │─►│ blackbox · ULog ·│
 │     arm/disarm, rc_loss_failsafe, low_voltage, latency, soak ... + limits   │ │ 스코프 캡처 · pcap│
 └───────────────────────────────┬─────────────────────────────────────────────┘ └──────────────────┘
 ┌───────────────────────────────▼─────────────────────────────────────────────┐ ┌──────────────────┐
 │ L4  DUT abstraction (capability 조합)                                       │ │ Results DB       │
 │     dut.cap("rc_input") / ("telemetry") / ("power") / ("flash") ...         │─►│ (Postgres):      │
 └───────────────────────────────┬─────────────────────────────────────────────┘ │ run · result ·   │
 ┌───────────────────────────────▼─────────────────────────────────────────────┐ │ measurement      │
 │ L3  Protocol adapters (HOW)                                                 │ └────────┬─────────┘
 │     CRSF · MSP · MAVLink · DShot decode · SBUS · CLI dump · ULog/BBL parse  │ ┌────────▼─────────┐
 └───────────────────────────────┬─────────────────────────────────────────────┘ │ Time-series DB   │
 ┌───────────────────────────────▼─────────────────────────────────────────────┐ │ (Influx/Timescale│
 │ L2  Instrument / transport drivers                                          │─►│ 텔레메트리·전류) │
 │     pyserial · SCPI(PSU, DMM) · 로직 분석기 API · J-Link/OpenOCD · RF 감쇠기│ └────────┬─────────┘
 └───────────────────────────────┬─────────────────────────────────────────────┘ ┌────────▼─────────┐
 ┌───────────────────────────────▼─────────────────────────────────────────────┐ │ Dashboards ·     │
 │ L1  Hardware                                                                │ │ Alerting         │
 │     FC · ESC · RX/TX · Jetson · GCS · PSU · 스코프 · 감쇠기 · 릴레이 · 지그 │ │ (Grafana, 메일,  │
 └─────────────────────────────────────────────────────────────────────────────┘ │ 슬랙, MR 코멘트) │
                                                                                 └──────────────────┘
```

### 3.2 층별 책임표

| 층 | 책임 | 예시 기술 | 이 층만 바뀌는 경우 | 이 층이 몰라야 하는 것 |
|---|---|---|---|---|
| L9 트리거 | 언제 무엇을 돌릴지 이벤트 발생 | GitLab CI pipeline, schedule, tag | 게이트 정책 변경 | 벤치 배선 |
| L8 오케스트레이터 | 테스트 × 제품 × 벤치 풀 매칭, 큐, 시간 예산 | GitLab runner tag, Jenkins, 자체 스케줄러, LAVA scheduler | 우선순위 · 샤딩 정책 변경 | 프로토콜 |
| L7 예약 | 물리 벤치의 상호 배제 | labgrid coordinator/place, Jenkins Lockable Resources, GitLab `resource_group` | 벤치 추가 · 고장 | 테스트 내용 |
| L6 러너 | 테스트 수집 · 실행 · 리포트 | pytest, Robot Framework, OpenHTF, twister | 리포트 포맷 변경 | 기체 종류 |
| L5 라이브러리 | 테스트 의도 · 시나리오 · 판정 | Python 모듈 + limit YAML | 요구사항 변경 | 어떤 UART 인지 |
| L4 DUT 추상화 | capability 를 묶어 제공 | 자체 `Dut` 클래스 + 제품 descriptor | 새 기체(= 새 descriptor) | 벤치 위치 |
| L3 프로토콜 | 바이트 ↔ 의미 | pymavlink, 자체 CRSF/MSP 코덱, DShot 디코더 | 새 펌웨어 스택 · 새 프로토콜 | 테스트 의도 |
| L2 드라이버 | 계측기 · 전송 제어 | pyserial, pyvisa(SCPI), pylink, 벤더 SDK | 계측기 모델 교체 | 프로토콜 |
| L1 하드웨어 | 실물 | [R08](2026-10-24_R08_hil_bench_design_instruments.md) | 배선 · 지그 변경 | — |

### 3.3 옆 기둥 (side services)

| 서비스 | 담는 것 | 쓰는 쪽 | 후보 기술 |
|---|---|---|---|
| Inventory / Config DB | 벤치 · DUT 시리얼 · hw rev · 계측기 · 배선 맵 · 교정 만료 · 제품 descriptor | L7, L8, L4 | Git 저장소의 YAML 로 시작 → Postgres |
| Artifact store | FW 바이너리, blackbox(BBL), ULog, 스코프/로직 캡처, 콘솔 로그, pcap | L5, 사람 | S3 호환 object storage (MinIO 등), content-addressed 경로 |
| Results DB | run · result · measurement 행 | 대시보드, 릴리스 게이트 | Postgres (공고에 PostgreSQL 언급) |
| Time-series DB | 고속 텔레메트리, soak 동안 전류/온도 | 대시보드, 회귀 탐지 | InfluxDB (공고 언급), TimescaleDB (Postgres 확장) |
| Dashboards | pass rate · 지연 추세 · 벤치 헬스 | 전 엔지니어 · 매니저 | Grafana (공고 언급) |
| Alerting | 회귀 · 벤치 다운 · flaky 급증 | 담당자 | Grafana alert, CI MR 코멘트, 메신저 |

### 3.4 데이터 흐름 한 번 따라가기

1. 개발자가 Betaflight 포크에 MR 을 올린다.
2. L9: GitLab 이 pipeline 을 만든다. 단위 테스트 + SITL 잡은 일반 runner 에서, HIL smoke 잡은 `hil-fpv` tag runner 로 간다.
3. L8: HIL 잡이 "smoke suite × fpv_bf" 를 요청한다. 스케줄러가 예상 시간을 보고 벤치 1대 또는 2대에 샤딩한다.
4. L7: 풀 `hil-fpv` 에서 healthy · 교정 유효 · 비어 있는 벤치 B2 를 잠근다.
5. L6: pytest 가 `--product fpv_bf --bench B2 -m smoke` 로 시작. fixture 가 inventory 에서 B2 의 배선 맵을 읽는다.
6. L4/L3/L2: `dut` fixture 가 descriptor 를 보고 `CrsfRcInput(serial=/dev/ttyUSB2)`, `MspTelemetry(...)`, `ScpiPsu(...)` 를 조립.
7. L5: `test_rc_loss_failsafe` 가 실행. 로직 분석기가 DShot 가 0 이 되는 시각을 잼, 결과 measurement 기록.
8. 결과: junit XML 은 GitLab MR 화면으로, 구조화 레코드는 Results DB 로, 캡처 파일은 artifact store 로.
9. L7: `finally` 에서 벤치 해제. teardown 이 전원 off.
10. 대시보드: "fpv_bf smoke pass rate" 에 점 하나 추가. 실패면 MR 에 코멘트.

---

## 4. Gemini의 상속 HAL 비판 → capability 조합 모델

### 4.1 Gemini가 제안한 것

PDF 16~20쪽에서 Gemini는 이런 구조를 권한다.

```python
# Gemini 제안 요약 (PDF 17~18쪽)
class NerosDrone:                      # base: 인터페이스
    def arm_motors(self): raise NotImplementedError
    def inject_rc_loss(self): raise NotImplementedError
    def get_motor_output(self): raise NotImplementedError

class ArcherBetaflight(NerosDrone):    # MSP + CRSF
    def arm_motors(self): self.msp_client.send_arm_command()
    def inject_rc_loss(self): self.crsf_emulator.stop_sending()

class ArcherAIPX4(NerosDrone):         # MAVLink
    def arm_motors(self): self.mavlink_client.command_long_send(...)
    def inject_rc_loss(self): self.mavlink_client.stop_rc_override()
```

그리고 "제품 정보 객체 ↔ 테스트 케이스 객체를 나누는 구조를 ArduPilot `Tools/autotest` 에서 영감 받았다" 는 스토리를 권한다. `Archer`, `Archer AI` 는 [Gemini 주장 · 미확인] 제품 이름이다.

### 4.2 맞는 점

- "테스트 시나리오는 밑단이 Betaflight 인지 PX4 인지 몰라야 한다" 는 방향은 옳다. P1 그 자체다.
- 물리 벤치 · 계측기(PSU, 로직 분석기, RF 감쇠기)는 스택과 무관하게 공유된다는 지적도 옳다 (PDF 19쪽).
- 판정을 공통 계측 장비로 일관되게 한다는 생각도 좋다.

### 4.3 문제점

> ⚠️ Gemini 정정: ArduPilot autotest 는 "제품 객체와 테스트 케이스 객체를 분리한 구조" 가 아니다. `Tools/autotest/vehicle_test_suite.py` 에 per-vehicle 테스트가 상속하는 base class 가 있고 (ArduPilot dev 문서: "Contains a base class inherited by the per-vehicle testing routines"), `arducopter.py` · `arduplane.py` · `rover.py` 같은 파일에 기체별 테스트가 **같은 클래스의 메서드로** 들어 있다. 즉 "기체 = 테스트 스위트 클래스" 이고 테스트와 기체가 한 클래스에 붙어 있다. 단일 저장소 · SITL 전용이라 그게 잘 작동하는 것이지, 여러 펌웨어 스택 · 여러 물리 벤치를 다루는 구조의 근거로 들기엔 부정확하다. 실행은 `./Tools/autotest/autotest.py build.Plane test.Plane.ThrottleFailsafe` 처럼 한다.

> ⚠️ Gemini 정정: "테스트 케이스 1,000개를 새로 짤 필요 없이 '제품 객체' 하나만 추가로 매핑" (PDF 18쪽) 은 과장이다. 기체마다 지원하는 기능이 다르다(Companion 유무, GPS 유무, 전원 측정 가능 여부). 기능 차이를 표현할 방법이 없으면 base class 의 `NotImplementedError` 가 런타임에 터지고, 테스트가 `if isinstance(drone, ArcherAIPX4)` 로 분기하기 시작한다.

상속 HAL 의 구체적 문제를 표로:

| # | 문제 | 상속에서 어떻게 생기나 | 증상 |
|---|---|---|---|
| 1 | God object | base class 에 모든 기체의 모든 기능 메서드가 쌓임 | `NerosDrone` 메서드 80개, 대부분 `NotImplementedError` |
| 2 | 조합 폭발 | (스택 × 무선 × 전원 측정 유무 × companion 유무) 마다 subclass | `ArcherBetaflightElrsWithPsu`, `...NoPsu` |
| 3 | 기능 유무를 표현 못 함 | 메서드가 "있지만 안 됨" | 테스트가 실행 중에 터짐. 스킵 사유가 안 남음 |
| 4 | HOW 와 WHERE 가 섞임 | subclass 생성자에서 `/dev/ttyUSB0` 열기 | 같은 기체도 벤치마다 subclass/설정 분기 |
| 5 | 프로토콜 재사용 불가 | MAVLink 코드가 `ArcherAIPX4` 안에 묻힘 | GCS 테스트나 ArduPilot 변형에서 MAVLink 재구현 |
| 6 | 테스트하기 어려움 | fake 를 만들려면 subclass 전체를 흉내 | 플랫폼 자체 단위 테스트가 없음 |
| 7 | 한 기체 = 한 프로토콜 가정 | 실제로는 FPV 가 CRSF(제어) + MSP(설정) + DShot(출력) 을 동시에 | 메서드 하나에 프로토콜 3개 혼재 |
| 8 | 테스트 단계별 차이 표현 불가 | 책상 셋업엔 PSU 가 없음 | 책상에선 전체 suite 가 깨짐 |

### 4.4 대안 — capability 조합 (composition)

핵심 아이디어:

- DUT 는 클래스 계층이 아니라 **capability 들의 묶음**이다. `dut.cap("rc_input")`, `dut.cap("telemetry")`, `dut.cap("power")`, `dut.cap("flash")`, `dut.cap("companion")`.
- 각 capability 는 작은 인터페이스(Protocol)다. 구현은 여러 개. `RcInput` 의 구현은 `CrsfRcInput`, `MavlinkRcInput`, (나중에) `SbusRcInput`.
- 어떤 DUT 가 어떤 구현을 갖는지는 **데이터**(제품 descriptor)로 정한다.
- 테스트는 필요한 capability 를 **선언**한다. selector 가 DUT 가 가진 capability 와 비교해 실행/스킵을 정하고, 스킵 사유를 남긴다.

```text
          Gemini (상속)                              제안 (조합)
   ┌──────────────────────┐              ┌─────────────────────────────────┐
   │      NerosDrone       │              │ Dut(serial, product, hw_rev)    │
   │ arm() rc_loss() ...   │              │   caps = {                      │
   └──────────┬───────────┘              │     rc_input : CrsfRcInput ─────┼─► CRSF codec ─► UART drv
       ┌──────┴───────┐                  │     telemetry: MspTelemetry ────┼─► MSP codec  ─► UART drv
 ┌─────▼─────┐  ┌─────▼──────┐           │     power    : ScpiPsu ─────────┼─► SCPI drv
 │ ArcherBF  │  │ ArcherAIPX4│           │     flash    : DfuFlasher ──────┼─► dfu-util
 │ (MSP+CRSF │  │ (MAVLink   │           │   }                             │
 │  +UART    │  │  +UART     │           └─────────────────────────────────┘
 │  +PSU...) │  │  +PSU...)  │             descriptor(YAML) 가 조립을 결정
 └───────────┘  └────────────┘             테스트는 requires=("rc_input","telemetry")
```

제품 descriptor 예시 (YAML 로 쓰면):

```text
# products/fpv_bf.yaml   (가상 예시 [추정])
product: fpv_bf
stack: betaflight-fork
hw_rev: FC-H7-revB
capabilities:
  rc_input:  crsf          # 제어 주입
  telemetry: msp           # 상태 읽기 · 설정
  motor_out: dshot_capture # 로직 분석기로 DShot 디코드
  power:     scpi_psu
  flash:     dfu
limits: limits/fpv_bf/${stage}.yaml
```

```text
# products/auto_px4.yaml  (가상 예시 [추정])
product: auto_px4
stack: px4
hw_rev: FC-H7-revC+Jetson
capabilities:
  rc_input:  mavlink        # RC_CHANNELS_OVERRIDE / MANUAL_CONTROL
  telemetry: mavlink        # HEARTBEAT / SYS_STATUS
  power:     scpi_psu
  flash:     swd
  companion: jetson_mavlink
  log:       ulog
limits: limits/auto_px4/${stage}.yaml
```

> YAML 에서 들여쓴 부분은 렌더러의 목록이 아니라 코드펜스 안의 데이터다.

벤치 배선(WHERE)은 제품 descriptor 와 **따로** 둔다:

```text
# benches/B3.yaml
bench: B3
pool: hil-fpv
dut: { product: fpv_bf, serial: FPV-0003 }
wiring:
  rc_input:  { port: /dev/serial/by-id/usb-FTDI_A1B2-if00, baud: 420000 }
  telemetry: { port: /dev/serial/by-id/usb-STM_VCP_0003 }
  power:     { visa: "TCPIP::10.0.3.21::INSTR", channel: 1, cal_due: 2027-03-01 }
  motor_out: { analyzer: la-03, channels: [0,1,2,3] }
safety: { props: none, current_limit_a: 5.0, estop: relay-03 }
```

> CRSF 420000 baud 는 ELRS/CRSF 에서 흔한 값이지만 수신기 설정에 따라 다르다. 정확한 값은 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) 참고. `by-id` 경로를 쓰는 건 USB 재열거로 `/dev/ttyUSB*` 번호가 바뀌는 문제 때문이다.

### 4.5 코드로 보면

[`../code/platform_sketch.py`](../code/platform_sketch.py) 에서 뽑은 핵심:

```python
ADAPTERS = {                                   # 문자열 → 구현. 새 프로토콜 = 한 줄
    ("rc_input", "crsf"): CrsfRcInput,
    ("rc_input", "mavlink"): MavlinkRcInput,
    ("telemetry", "msp"): MspTelemetry,
    ("telemetry", "mavlink"): MavlinkTelemetry,
    ("power", "scpi_psu"): Power,
    ("flash", "dfu"): Flash,
    ("companion", "jetson_mavlink"): Companion,
}

@test_case("rc_input", "telemetry", tags=("smoke", "failsafe"))
def test_rc_loss_failsafe(ctx):
    rc, tm = ctx.dut.cap("rc_input"), ctx.dut.cap("telemetry")
    for _ in range(5):
        rc.send(throttle=0.0, arm_switch=True)
    ctx.check("armed_before_loss", bool(tm.status()["armed"]))
    latency = rc.stop()                         # 계측기/펌웨어가 잰 값
    ctx.measure("failsafe_latency_ms", latency, "ms")   # limit 은 제품 데이터에서
    ctx.check("disarmed_on_failsafe", not tm.status()["armed"])
```

- 테스트에 `if product == ...` 가 한 줄도 없다.
- FPV 는 limit `(0, 150) ms`, PX4 변형은 `(0, 600) ms` 처럼 **데이터에서** 다른 기준을 받는다 (값은 가상).
- 책상 셋업(`fpv_bf_desk`)에는 `power` 가 없으므로 `test_low_voltage_warning` 은 "missing capability: power" 사유로 스킵된다. 터지지 않는다.

### 4.6 무엇을 추가하면 어디를 고치나

| 변화 | 고치는 곳 | 안 고치는 곳 |
|---|---|---|
| 새 기체 변형 (같은 스택, 다른 보드) | `products/<new>.yaml` + `limits/<new>/*.yaml` | 테스트 라이브러리, 어댑터, 러너 |
| 새 펌웨어 스택 (예: ArduPilot 변형 추가) | L3 어댑터 몇 개 (대부분 MAVLink 재사용) + descriptor | 테스트 라이브러리 (capability 같으면 그대로) |
| 새 프로토콜 (예: SBUS 로 RC 주입) | `SbusRcInput` 클래스 1개 + 레지스트리 1줄 | 테스트, 다른 어댑터 |
| 새 벤치 타입 (예: RF 감쇠기 달린 벤치) | `benches/*.yaml`, L2 감쇠기 드라이버, 새 capability `rf_link` | 기존 테스트 (rf_link 필요 없는 테스트는 그대로) |
| 계측기 모델 교체 (PSU 벤더 변경) | L2 드라이버 1개 | L3 이상 전부 |
| 요구사항 변경 (failsafe 기준 강화) | `limits/*` 데이터 | 코드 전부 |
| 새 테스트 단계 (예: 공장 EOL) | L6 러너 선택(OpenHTF 류) + limit 파일 + 운영자 UI | L5 시나리오 대부분 재사용 |
| 새 게이트 정책 (merge 때 soak 추가) | L9 CI 설정 | L1~L7 |

### 4.7 조합 모델의 함정 (공정하게)

| 함정 | 설명 | 대응 |
|---|---|---|
| capability 이름 난립 | `rc`, `rc_input`, `rc_inject` 가 섞임 | capability 목록을 플랫폼 팀이 관리 (짧은 enum + 문서) |
| 인터페이스가 최소공배수로 빈약해짐 | MAVLink 만의 기능을 표현 못 함 | 공통 인터페이스 + `dut.cap("mavlink").raw` 같은 탈출구. 단, 탈출구 쓰는 테스트는 해당 capability 를 requires 에 선언 |
| 정규화로 정보 손실 | MSP 와 MAVLink 의 "armed" 의미가 미묘하게 다름 | 정규화 dict 에 원본 필드도 같이 두고, 의미 차이를 어댑터 문서에 명시 |
| 런타임 조립 오류 | descriptor 오타가 실행 중에야 발견 | descriptor 스키마 검증을 CI 의 첫 단계로 (플랫폼 저장소 자체 테스트) |
| capability 간 의존 | `motor_out` 측정엔 `power` 가 켜져 있어야 함 | requires 에 둘 다 선언. 또는 capability 가 다른 capability 를 요구하는 선언 |

---

## 5. 프로토콜 어댑터 층 (L3)

### 5.1 어댑터가 하는 일

| 일 | 설명 |
|---|---|
| 인코드 | 의미("throttle 0, arm on") → 바이트 프레임 |
| 디코드 | 바이트 → 의미 + 원본 + 타임스탬프 |
| 프레이밍/재동기 | 깨진 바이트 속에서 sync 바이트 찾기, CRC 확인, 1바이트 밀고 재시도 |
| 정규화 | 스택별 필드 → 공통 모양 (`armed`, `failsafe`, `vbat`, `mode`) |
| 타임스탬프 | 수신 시각(호스트) + 장치 시각(있으면) 둘 다 보존 |
| 결함 주입 | CRC 깨기, 프레임 누락, 지연, 버스트 (테스트용 hook) |

### 5.2 프로토콜별 메모

| 프로토콜 | 방향 | 플랫폼에서 쓰는 곳 | 라이브러리 후보 | 주의 |
|---|---|---|---|---|
| CRSF | 호스트 → FC (RC), FC → 호스트 (텔레메트리) | RC 주입, link stats | 자체 구현 (작음) | baud · 프레임 주기는 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) |
| MSP | 양방향 요청/응답 | Betaflight 상태 · 설정 · 버전 | 자체 구현, Betaflight Configurator 소스 참고 | 명령 번호 · 버전 차이 [확인 필요] |
| MAVLink | 양방향 | PX4/ArduPilot 명령 · 텔레메트리 · 파라미터 · 로그 | pymavlink, MAVSDK | dialect · 메시지 버전 |
| DShot | FC → ESC | 모터 출력 판정 (로직 분석기 디코드) | 자체 디코더 또는 분석기 SDK | 속도 등급별 비트 타이밍 · bidirectional 여부 |
| CLI (Betaflight) | 텍스트 | `diff all` 로 설정 스냅샷 → 결과에 해시 | pyserial | 설정 drift 탐지 |
| Blackbox (BBL) | 로그 파일 | 고속 내부 값 (P3) | blackbox-tools 디코더 [확인 필요] | 로깅 속도가 루프 타이밍에 영향 |
| ULog | 로그 파일 | PX4 내부 값 | pyulog | 토픽 이름 버전 차이 |

### 5.3 정규화된 텔레메트리 레코드 예

```text
{"t_host_ns": 1729760000123456789, "t_dev_us": 81234567, "src": "msp",
 "armed": true, "failsafe": false, "vbat": 16.62, "mode": "ANGLE",
 "raw": {"MSP_STATUS": {"flags": "0x00000001"}}}
```

- `raw` 를 남겨 두면 정규화가 틀렸을 때 다시 해석할 수 있다.
- 두 시각을 다 남기면 나중에 시각 정렬(time alignment)을 다시 할 수 있다. 공고(Manager, Autonomy Evaluation)의 "time alignment across sensors" 와 같은 문제다.

### 5.4 어댑터 자체의 테스트

- 어댑터는 플랫폼의 가장 아래 소프트웨어라서, 여기 버그는 모든 테스트 결과를 오염시킨다.
- 그래서 어댑터는 하드웨어 없이 테스트: 녹음된 바이트 스트림 재생, 알려진 프레임 왕복(encode → decode), property-based fuzz.
- Hypothesis(Python property-based testing)로 "아무 바이트 열을 넣어도 파서가 죽지 않고, 정상 프레임은 쓰레기 사이에 끼워도 찾아낸다" 같은 성질을 검사한다.

```python
# 개념 예 (Hypothesis 설치 필요 — 본 저장소 예제는 표준 라이브러리만 쓰므로 여기만 예시)
from hypothesis import given, strategies as st

@given(noise_a=st.binary(max_size=64), noise_b=st.binary(max_size=64))
def test_parser_finds_frame_in_noise(noise_a, noise_b):
    frame = encode_rc_frame(channels=[992] * 16)
    frames = list(parse_stream(noise_a + frame + noise_b))
    assert any(f.channels == [992] * 16 for f in frames)
```

> 992 는 CRSF 채널 중앙값으로 흔히 쓰이는 값이지만 확인 후 사용 [확인 필요]. `encode_rc_frame`/`parse_stream` 은 가상의 함수 이름.

---

## 6. 테스트 러너와 테스트 라이브러리 (L5 · L6)

### 6.1 왜 pytest 를 뼈대로

| pytest 기능 | 플랫폼에서의 역할 |
|---|---|
| fixture (scope: session/module/function) | 층 조립. `bench` → `dut` → `rc`/`telemetry`. teardown 은 `yield` 뒤 |
| marker (`@pytest.mark.smoke`) | 단계 선택 (`-m smoke`), capability 선언 (`@pytest.mark.requires("power")`) |
| parametrize | 같은 테스트를 여러 입력 · 여러 제품에 |
| `conftest.py` + 플러그인 hook | 커스텀 옵션 (`--product`, `--bench`), 결과 업로드, 스킵 로직 |
| `--junitxml` | GitLab MR 테스트 리포트, Jenkins 결과 |
| pytest-xdist | 하드웨어 없는 테스트 병렬화. HIL 에선 "벤치당 워커 1개" 로만 |
| 생태계 | labgrid 플러그인, pytest-embedded(Espressif), pytest-rerunfailures |

Don은 pytest 실무 경험이 없다 (NOTES_SPEC). 다만 챔버 SDK 에서 "시퀀스 정의 → 스케줄링 → 상태 모니터링" 을 직접 만들었으니, pytest 는 그 중 "시퀀스 실행기 + 리포트" 부분을 표준 도구로 대체하는 것이라고 보면 된다.

### 6.2 fixture 로 층을 조립하기

```python
# conftest.py (개념 예시)
import pytest

def pytest_addoption(parser):
    parser.addoption("--product", required=True)
    parser.addoption("--bench", required=True)
    parser.addoption("--stage", default="dev")       # dev | ci | release | factory

@pytest.fixture(scope="session")
def bench(request):
    b = inventory.load_bench(request.config.getoption("--bench"))   # WHERE
    lease = reservation.acquire(b.id, who=os.environ.get("CI_JOB_ID", "local"))
    yield b
    lease.release()                                                 # 실패해도 해제

@pytest.fixture(scope="session")
def dut(request, bench):
    product = request.config.getoption("--product")
    d = build_dut(product, bench)                                   # HOW 조립
    d.recover_to("known_good")                                      # P5/P6
    yield d
    d.safe_off()

@pytest.fixture
def limits(request, dut):
    return load_limits(dut.product, request.config.getoption("--stage"))

def pytest_collection_modifyitems(config, items):
    # capability 선언 기반 자동 스킵 (사유가 리포트에 남는다)
    caps = inventory.product_caps(config.getoption("--product"))
    for item in items:
        for m in item.iter_markers("requires"):
            missing = [c for c in m.args if c not in caps]
            if missing:
                item.add_marker(pytest.mark.skip(reason=f"missing capability: {missing}"))
```

```python
# tests/failsafe/test_rc_loss.py
import pytest

@pytest.mark.smoke
@pytest.mark.requires("rc_input", "telemetry", "motor_out")
@pytest.mark.req("SYS-FS-001")               # 요구사항 ID → 추적성 매트릭스 (9.6절)
def test_rc_loss_failsafe(dut, limits, record):
    dut.cap("rc_input").stream(throttle=0.0, arm=True, seconds=1.0)
    assert dut.cap("telemetry").status().armed
    t0 = dut.cap("rc_input").stop()           # 끊은 시각(계측기 기준)
    t1 = dut.cap("motor_out").wait_all_zero(timeout_s=2.0)
    record.measure("failsafe_latency_ms", (t1 - t0) * 1e3, limits["failsafe_latency_ms"])
```

> `record.measure` 는 가상의 fixture. pytest 기본 assert 만으로는 "측정값 + limit" 을 구조화해 남기기 어렵다. 그래서 OpenHTF 의 measurement 개념을 pytest fixture 로 흉내 내는 게 흔한 패턴이다 [추정 — 업계 관행, 특정 출처 없음].

### 6.3 labgrid · pytest-embedded 와 비교

| 도구 | 하는 일 | 이 단원 층에 대응 |
|---|---|---|
| labgrid pytest 플러그인 | `--lg-env` 로 환경 YAML 지정, `target` · `strategy` · `env` fixture 제공 (labgrid usage 문서) | L4(target) + L2(driver) + P6(strategy) |
| labgrid 환경 YAML | `targets:` 아래 `resources:` (패시브 정보: 시리얼 포트 등) 와 `drivers:` (동작) | 4.4절 bench YAML 과 같은 생각 |
| pytest-embedded (Espressif) | `--embedded-services serial,esp,idf,jtag,qemu,...` 로 서비스 조합, `dut.expect(...)` 로 출력 대기 | capability 조합의 실제 사례 |
| twister (Zephyr) | `--generate-hardware-map map.yml` 로 연결된 보드 탐색 → `--device-testing --hardware-map map.yml`, 테스트가 요구하는 `--fixture` 가 없으면 스킵 | WHERE 데이터 + capability(fixture) 선언 |

> pytest-embedded 의 "service 를 골라 끼운다" 와 twister 의 "fixture 를 요구하고 하드웨어 맵이 제공한다" 는 둘 다 이 단원의 capability 모델과 같은 발상이다. Gemini 식 상속보다 업계 오픈소스 쪽 증거가 조합 쪽에 많다.

### 6.4 테스트 라이브러리 구성

```text
neros-test/                      (가상 구조 [추정])
├─ platform/                     # L2~L4, L7 클라이언트. 플랫폼 팀 소유. 버전 붙여 배포
│  ├─ drivers/ (psu_scpi.py, la_*.py, jlink.py, attenuator.py)
│  ├─ protocols/ (crsf.py, msp.py, mavlink_adapter.py, dshot_decode.py)
│  ├─ caps/ (rc_input.py, telemetry.py, power.py, flash.py, motor_out.py)
│  └─ dut.py, inventory.py, reservation.py, results.py
├─ products/   *.yaml            # 제품 descriptor
├─ benches/    *.yaml            # 벤치 배선 · 풀 · 안전
├─ limits/<product>/<stage>.yaml
├─ tests/                        # L5 시나리오. FW 개발자도 기여
│  ├─ smoke/ failsafe/ power/ link/ latency/ soak/ factory/
│  └─ conftest.py
├─ tools/  (bench_health.py, flaky_report.py, trace_matrix.py)
└─ ci/     (gitlab-ci 템플릿)
```

- `platform/` 과 `tests/` 를 분리하는 게 핵심. 플랫폼은 API 를 약속하고, 테스트는 그 API 만 쓴다.
- 큰 조직이면 둘을 다른 저장소로 나누고 플랫폼을 버전 붙은 패키지로 배포. 작은 팀이면 한 저장소(monorepo)에서 디렉터리 소유권(CODEOWNERS)으로 시작. Neros 공고에 Bazel 이 있으니 monorepo 일 가능성 [추정].

### 6.5 Robot Framework 는?

- 키워드 기반이라 비개발자(테크니션, 공장 엔지니어)가 읽기 쉽다.
- Renode 가 Robot Framework 와 통합되어 있다. `renode-test my_test.robot` 로 실행하고, `Wait For Line On Uart` 같은 키워드를 쓴다 (Renode 문서).
- 단점: 복잡한 로직 · 통계 판정은 결국 Python 라이브러리로 내려간다. 두 언어를 유지하게 된다.
- 권장: 뼈대는 pytest. 운영자용 절차(공장 · RMA)만 필요하면 그 위에 얇은 UI 또는 OpenHTF.

---

## 7. 수명주기 재사용 — 같은 테스트, 다른 무대

### 7.1 여섯 무대

```text
  개발 책상 ─► CI HIL(merge) ─► Nightly 전체+soak ─► Release qual ─► 공장 EOL ─► 필드/RMA
   (1대,      (벤치 풀,         (벤치 풀,            (qual 매트릭스,  (스테이션,    (반품 분석,
   사람)       자동, 빠름)        자동, 오래)           사인오프)        운영자, 초)    사람+자동)
       └────────────── 같은 L2~L5 (드라이버 · 프로토콜 · capability · 시나리오) ──────────────┘
```

### 7.2 무대별로 바뀌는 것

| 무대 | 누가 | 트리거 | 시나리오 선택 | Limit | 시퀀스 | UI | 결과 저장 | 시간 예산 |
|---|---|---|---|---|---|---|---|---|
| 개발 책상 | FW 개발자 | 손으로 `pytest` | 원하는 것만 | dev (느슨) | 자유 | 터미널 | 로컬 + 선택 업로드 | 분 |
| CI HIL (merge) | CI | MR / merge | `-m smoke` | ci | 고정, 실패 시 계속 | MR 리포트 | Results DB | 10~20분 [추정 목표] |
| Nightly | 스케줄 | cron | 전체 + soak | ci | 고정, 반복 N회 | 대시보드 | Results DB + TSDB | 수 시간 |
| Release qual | 릴리스 담당 | tag | qual 매트릭스 (제품 × hw rev × 조건) | release (스펙) | 고정, 문서화 | 리포트 PDF/HTML | Results DB, 보존 | 일 |
| 공장 EOL | 운영자 | 지그에 장착 | 공장 subset | factory (guard band) | 고정, 첫 실패에서 정지 가능 | 큰 버튼 · 바코드 | MES / 결과 DB | 초~분 |
| 필드 / RMA | 지원 · 품질 | 반품 접수 | 증상별 + 전체 진단 | release + 진단 전용 | 탐색적 | 반자동 | RMA 레코드 | 시간 |

### 7.3 공장 EOL 은 왜 OpenHTF 류가 맞나

- OpenHTF(Google, "not an official Google product")는 테스트를 **phase**(메타데이터가 붙은 Python 함수)의 순서로 정의하고, **measurement** 에 spec 을 붙이며, **plug** 로 장비/DUT 와 통신한다. 결과는 record 로 남고 웹 frontend 가 station 별로 보여 준다 (OpenHTF README).
- 이 개념들이 이 단원의 층과 1:1 로 대응한다.

| OpenHTF | 이 단원 |
|---|---|
| plug | L2 드라이버 + L3 어댑터 (capability) |
| phase | L5 시나리오의 한 단계 |
| measurement + spec | `ctx.measure` + limits-as-data |
| attachment | artifact store 의 파일 |
| station | 벤치 / 공장 스테이션 |
| test record + output callback | Results DB 업로드 |

- 핵심 재사용 포인트: **plug(=capability 구현)를 공유**하면, CI 에서 쓰는 CRSF 어댑터와 공장에서 쓰는 CRSF 어댑터가 같은 코드다. 공장에서만 새 버그가 생기는 일이 줄어든다.
- pytest 시나리오를 OpenHTF phase 로 감싸는 얇은 어댑터를 두거나, 반대로 공장용으로 OpenHTF 를 쓰고 시나리오 함수는 `ctx` 하나만 받게 만들어 양쪽에서 호출할 수 있게 한다. `platform_sketch.py` 의 테스트 함수가 `ctx` 하나만 받는 이유가 이것이다.
- 공장 쪽 공고(Electrical Engineer - Board Design and Test)에 "test result databases", "traceability systems, MES platforms" 가 있다. FW 테스트 플랫폼의 결과 스키마를 공장과 맞춰 두면 같은 DB 를 쓸 수 있다.

### 7.4 공장 무대에서 추가로 신경 쓸 것

| 항목 | 이유 |
|---|---|
| 사이클 타임 | 초 단위. 플래시 · 부팅 · 측정을 병렬화. 불필요한 sleep 제거 |
| Guard band | 계측 불확도만큼 limit 을 안쪽으로 → 불량 통과 방지 |
| 운영자 오류 방지 | 바코드 스캔으로 시리얼 입력, 장착 감지 센서 |
| 재시험 정책 | 몇 번까지 재시험 허용? 재시험 통과는 따로 집계 (숨은 수율 손실) |
| 첫 실패 정지 vs 전체 실행 | 수율 분석엔 전체 실행이 정보가 많고, 처리량엔 첫 실패 정지가 빠름 |
| 펌웨어 provisioning | 생산 펌웨어 굽기 · 시리얼/키 주입 · 잠금. 공고(Autonomy Platform & Runtime Lead)의 "provisioning at the factory" |

### 7.5 RMA / 필드 반품

- 반품 기체의 blackbox/ULog 를 먼저 artifact store 에 올리고, 같은 시리얼의 공장 EOL 기록 · 마지막 FW 버전과 자동으로 엮는다. 이 조인이 되려면 시리얼이 모든 레코드의 키여야 한다 (P4).
- "필드에서 본 실패를 재현하는 테스트" 를 만들면 그 테스트는 nightly 에 영구 편입된다. 필드 → 회귀 테스트로 돌아오는 고리가 플랫폼 가치의 핵심.

---

## 8. CI/CD 통합

### 8.1 게이트 단계

| 단계 | 트리거 | 무엇을 | 어디서 | 시간 예산 [추정 목표] | 실패하면 |
|---|---|---|---|---|---|
| G0 per-commit | push | 빌드 (전 타깃), 정적 분석, 단위 테스트, 파서 fuzz(짧게), SITL smoke | 일반 runner (컨테이너) | 10분 이내 | MR 머지 불가 |
| G1 merge gate | MR 머지 직전 / merge train | G0 + HIL smoke (제품별 1벤치), 설정 dump diff | `hil-*` tag runner + 벤치 풀 | 20분 이내 | 머지 불가 (flaky quarantine 제외) |
| G2 nightly | schedule | 전체 HIL suite × 모든 제품 × 벤치 회전, N회 반복, 2~4시간 soak, 성능 메트릭 | 벤치 풀 전체 (밤 시간) | 밤 사이 (8~10시간) | 아침에 담당자 triage, 회귀면 이슈 자동 생성 |
| G3 release | release tag | qual 매트릭스 (제품 × hw rev × 온도/전압 코너), 장시간 soak, 비행 시험 사인오프 | 벤치 + 환경 챔버 + 비행장 | 일 단위 | 릴리스 보류, 예외는 문서화된 승인 |
| G4 post-release | 필드 로그 유입 | 필드 로그 리플레이, 텔레메트리 이상 탐지 | 데이터 플랫폼 | 지속 | 핫픽스 / 회귀 테스트 추가 |

- G3 의 "예외 승인 프로세스" 는 공고(Manager, Autonomy Evaluation, Data & Test)의 "the process for reviewing exceptions" 와 같은 개념이다.
- 비행 시험은 자동화 플랫폼 밖이지만, 결과(ULog/BBL + 체크리스트)는 같은 Results DB 에 같은 키로 들어와야 릴리스 판단이 한 화면에서 된다.

### 8.2 GitLab CI 예시

Don은 GitLab CI 실무 경험이 없다 (NOTES_SPEC). 아래는 공식 키워드 의미만 확인한 개념 예시다.

```text
# .gitlab-ci.yml (개념 예시)
stages: [build, unit, sitl, hil_smoke]

build:
  stage: build
  tags: [docker]                       # 일반 runner
  script: [bazel build //firmware/... ]

unit:
  stage: unit
  tags: [docker]
  script: [bazel test //... --test_tag_filters=-hil --test_output=errors]
  artifacts: { reports: { junit: bazel-testlogs/**/test.xml } }

hil_smoke_fpv:
  stage: hil_smoke
  tags: [hil-fpv]                      # 벤치가 붙은 runner 만 이 잡을 받음
  resource_group: bench-pool-fpv       # 같은 그룹 잡은 한 번에 하나 (벤치 1대일 때)
  timeout: 30m
  retry:
    max: 1
    when: [runner_system_failure, stuck_or_timeout_failure]   # 인프라 실패만 재시도
  script:
    - pytest tests -m smoke --product fpv_bf --bench "$BENCH_ID" --stage ci --junitxml=report.xml
  artifacts:
    when: always
    paths: [captures/, logs/]
    reports: { junit: report.xml }
```

키워드 의미 (GitLab CI YAML 문서에서 확인):

| 키워드 | 의미 | HIL 에서의 쓰임 |
|---|---|---|
| `tags` | "List of tags that are used to select a runner" | 벤치가 연결된 호스트의 runner 에 `hil-fpv` 같은 tag |
| `resource_group` | 같은 그룹의 잡은 파이프라인이 달라도 동시에 하나만. 세마포어처럼 동작. 물리 장치마다 하나씩 둘 수 있음 | 벤치 1대 = 그룹 1개. 벤치 풀(여러 대)에는 부족 → 8.3절 |
| `retry:max` | 0, 1, 2 만 허용 | HIL 은 1 로 제한 |
| `retry:when` | `always`, `script_failure`, `runner_system_failure` 등 실패 종류별. `stuck_or_timeout_failure` 는 GitLab 19.1 에서 deprecated (대체 값 사용) | **테스트 실패(script_failure)는 재시도하지 않는다.** 인프라 실패만 |
| `artifacts:reports:junit` | JUnit XML 을 MR 화면 테스트 리포트로 | pytest `--junitxml` 출력 연결 [키워드 존재는 확인, 세부 동작 확인 필요] |

> 위 예시의 `stuck_or_timeout_failure` 는 deprecated 라서 실제로 쓸 땐 현재 문서의 대체 값으로 바꿔야 한다 [확인 필요: 대체 값 이름].

### 8.3 벤치 풀과 예약 — 어디서 잠글까

| 방식 | 어떻게 | 장점 | 단점 |
|---|---|---|---|
| runner = 벤치 1:1 | 벤치마다 호스트 + runner, `concurrent = 1` | 가장 단순. GitLab 이 큐잉 | 벤치 고장 시 runner 를 손으로 멈춰야. 테스트 중 다른 벤치 못 빌림 |
| GitLab `resource_group` | 벤치당 그룹 | 설정만으로 | 풀(아무 빈 벤치) 개념이 약함 |
| Jenkins Lockable Resources | `lock(label: 'hil-fpv', quantity: 1, variable: 'BENCH')` | label = 풀. 빈 것 아무거나. 이름은 env 로 받음 (플러그인 문서) | Jenkins 운영 필요 |
| labgrid coordinator | place 를 acquire/release. coordinator 가 상호 배제 담당, 클라이언트는 exporter 를 통해 직접 자원 접근 (labgrid 문서) | 원격 접근 · 콘솔 · 전원 제어까지 한 세트, 사람도 같은 CLI 로 예약 | gRPC 서비스 운영, 학습 비용 |
| LAVA | device type · device 별 큐 · health check 내장 | 대규모 보드 팜 검증된 구조 | Linux 부팅 보드 중심. MCU FC 엔 무거움 [추정] |
| 자체 예약 서비스 | Postgres 행 lock + 하트비트 + TTL | 정확히 원하는 정책 (교정 만료 제외 등) | 직접 유지보수 |

권장 진화 경로 [추정 · Don 의견으로 제시할 것]:

1. 벤치 2~3대: runner = 벤치 1:1 + `resource_group`. 하루면 된다.
2. 벤치 5~10대 + 사람도 벤치 원격 사용: labgrid 도입 (coordinator · exporter). CI 잡 안에서 place 를 acquire.
3. 벤치 수십 대 + 여러 팀: 자체 예약/스케줄러 (교정 · 헬스 · 우선순위 · 공정성) 를 labgrid 위에 또는 대체로.

Jenkins 예시 (Lockable Resources 플러그인):

```text
pipeline {
  agent { label 'hil-host' }
  stages {
    stage('HIL smoke') {
      steps {
        lock(label: 'hil-fpv', quantity: 1, variable: 'BENCH') {
          sh 'pytest tests -m smoke --product fpv_bf --bench "$BENCH" --junitxml=report.xml'
        }
      }
    }
  }
  post { always { junit 'report.xml' } }
}
```

### 8.4 Bazel 과 HIL

Don은 Bazel 실무 경험이 없다 (NOTES_SPEC). 공고(Firmware Test Engineer nice-to-have, Senior Firmware Engineer Platform, Lead Platform Software, Full-Stack)에 반복해서 나온다.

| Bazel 개념 | 테스트 플랫폼에서의 의미 |
|---|---|
| hermetic build | 같은 입력 → 같은 출력. "내 PC 에선 되는데" 제거. FW 바이너리 해시가 추적 키가 된다 |
| `*_test` 타깃 | 단위 테스트 · SITL 테스트를 `bazel test //...` 로 |
| 테스트 캐시 | 입력이 안 바뀐 테스트는 다시 안 돌림 → G0 빠름. 단, HIL 은 외부 상태라 캐시하면 안 됨 |
| `tags = ["external"]` | "test has an external dependency; disable test caching" (Bazel Test Encyclopedia) → HIL 타깃에 |
| `tags = ["exclusive"]` | "run no other test at the same time" → 같은 호스트에서 벤치 자원 충돌 방지 |
| `tags = ["manual"]` | `//...` 같은 wildcard 에 포함 안 됨 → HIL 테스트가 일반 CI 에 끌려오지 않게 |
| `--test_tag_filters=-hil` | 태그로 포함/제외 |
| `size` / `timeout` | timeout 미지정 시 size 로 결정: small 60s, medium 300s, large 900s, enormous 3600s |
| `XML_OUTPUT_FILE` | 테스트가 JUnit 기반 XML 을 쓸 위치. 안 쓰면 Bazel 이 로그를 감싼 기본 XML 생성 |
| `TEST_UNDECLARED_OUTPUTS_DIR` | 여기 쓴 파일은 `outputs.zip` 으로 묶여 `bazel-testlogs` 아래 → 캡처 파일 수집 |
| `--runs_per_test` | 반복 실행. 각 실행에 `TEST_RUN_NUMBER` 설정 → flaky 측정 |
| `flaky = True` / `--flaky_test_attempts` | 실패 시 재시도, 섞이면 FLAKY 상태 [재시도 횟수 정확한 값은 확인 필요] |
| remote cache / remote execution | 빌드 · 단위 테스트를 공유 캐시로 → 개발자와 CI 가 결과 공유. HIL 은 원격 실행 대상 아님 |

```text
# BUILD (개념 예시)
py_test(
    name = "hil_smoke_fpv",
    srcs = ["hil_entry.py"],
    args = ["-m", "smoke", "--product", "fpv_bf"],
    data = ["//firmware:fpv_bf.hex", "//limits:fpv_bf"],
    size = "large",
    tags = ["hil", "manual", "external", "exclusive"],
)
```

- 이렇게 하면 "어떤 FW 바이너리 + 어떤 limit 으로 돌렸는지" 가 Bazel 의존 그래프로 고정된다. 추적성(P4)이 빌드 시스템 수준에서 생긴다.
- 실제로 HIL 을 Bazel 타깃으로 감쌀지, CI 잡에서 pytest 를 직접 부를지는 팀 선택. 벤치 예약이 Bazel 바깥(L7)에 있어야 한다는 점만 지키면 된다.

### 8.5 시간 예산과 샤딩

- G1 은 시간 예산이 생명이다. 머지 게이트가 40분이면 개발자는 게이트를 우회하는 방법을 찾는다.
- 테스트마다 과거 실행 시간(p90)을 Results DB 에서 가져와, 벤치 k 대에 **최장 처리 시간 우선(LPT)** 으로 나눠 담는다. 단순 greedy 지만 효과가 좋다.
- 각 벤치 샤드는 setup(전원 · 플래시)을 한 번만 하고 여러 테스트를 돈다. 플래시는 테스트당이 아니라 샤드당.
- 실패 시 "나머지 계속" 이 기본. 첫 실패에서 멈추면 정보를 버린다. 단, 안전 관련 실패(과전류)는 즉시 정지.
- 시간 예산 초과 잡은 실패가 아니라 "timeout" 으로 따로 분류. 원인은 대부분 벤치/인프라다.

### 8.6 HIL flakiness 관리

용어부터 나누자.

| 결과 | 의미 | 누구에게 |
|---|---|---|
| pass | 기준 충족 | — |
| fail | 측정값이 limit 밖 또는 기능 실패 | FW 담당 |
| error | 테스트 코드/플랫폼 예외 (예: 시리얼 포트 열기 실패) | 플랫폼 팀 |
| infra | 벤치 예약 실패, runner 사망, 계측기 무응답 | 벤치 운영 |
| skip | capability 없음 · 조건 불충족 (사유 필수) | — |
| quarantined | 알려진 flaky. 실행은 하되 게이트에 반영 안 함 | 테스트 소유자 |

정책 예시 [추정 · 업계 관행 기반 제안]:

| 정책 | 내용 |
|---|---|
| 자동 재시도 | 인프라 실패만 1회. 테스트 fail 은 재시도로 덮지 않는다. 재시도 사실은 결과에 기록 |
| flaky 판정 | 같은 fw_sha 에서 pass 와 fail 이 섞이면 flaky (`platform_sketch.py` 의 `flaky_report`) |
| 벤치 상관 | 실패가 한 벤치에 몰리면 FW 보다 벤치 의심 → 벤치 헬스체크 트리거 |
| quarantine | flaky 율이 임계(예: 2%) 넘으면 게이트에서 제외 + 담당자 · 기한 있는 이슈 자동 생성 |
| quarantine 만료 | 기한(예: 2주) 지나면 다시 게이트에 넣거나 테스트 삭제. 영구 quarantine 금지 |
| flake SLO | 예: G1 게이트 "진짜 회귀가 아닌 빨간불" 비율 < 1% / 주. 대시보드로 공개 |
| 재현 도구 | `--runs_per_test` 또는 `pytest --count`(pytest-repeat) 로 N회 반복, 벤치 고정 vs 회전 비교 |

- **벤치 회전**이 중요하다. 같은 테스트가 늘 같은 벤치에서 돌면 벤치 문제와 FW 문제를 구분할 수 없다. `platform_sketch.py` 의 장난감 스케줄러는 "가장 빨리 비는 벤치" 만 봐서 회전이 고르지 않다 (실행 결과에서 B1 에 몰림). 실제 스케줄러는 nightly 에서 의도적으로 회전시켜야 한다.
- HIL flaky 의 흔한 진짜 원인: USB 허브 전원, 케이블 접촉, `/dev/ttyUSB*` 재번호, 부팅 대기 시간 부족, 호스트 CPU 부하, RF 간섭, 전원 램프 속도, 이전 테스트가 남긴 설정. 이 목록이 벤치 헬스체크 항목이 된다 (10절).

---

## 9. 데이터 — 결과 · 시계열 · 아티팩트 · 회귀 · 추적성

### 9.1 결과 스키마 (Postgres)

```text
products(product PK, stack, descriptor_sha)
duts(serial PK, product FK, hw_rev, mfg_date, status)          -- status: active / golden / spare / rma / scrapped
benches(bench_id PK, pool, host, healthy, notes)
instruments(asset_id PK, kind, model, serial, fw_version, cal_due, bench_id FK)
runs(run_id PK, trigger, pipeline_id, fw_sha, fw_build_id, test_sha, limits_version,
     stage, started_at, finished_at, requested_by)
results(result_id PK, run_id FK, test_name, product, dut_serial FK, bench_id FK,
        status, error, retried, started_at, duration_ms, config_hash)
measurements(result_id FK, name, value, unit, low, high, passed)
result_instruments(result_id FK, asset_id FK, cal_due_at_run)  -- 그 시점 교정 정보 스냅샷
artifacts(artifact_id PK, result_id FK, kind, uri, sha256, bytes)  -- kind: bbl, ulog, la_capture, console, pcap
requirements(req_id PK, text, source)                          -- 예: SYS-FS-001
test_requirements(test_name, req_id)                           -- marker 에서 자동 수집
```

- measurement 를 행으로 두면 "failsafe_latency_ms 의 fw 버전별 분포" 같은 질문이 SQL 한 줄이다.
- `cal_due_at_run` 처럼 **그 시점의 값을 스냅샷**으로 남긴다. 나중에 계측기 정보가 바뀌어도 과거 결과의 근거가 유지된다.
- `config_hash`: Betaflight `diff all` 또는 PX4 파라미터 덤프의 해시. 설정 drift 를 회귀로 오인하지 않게.

### 9.2 결과 레코드 (JSON lines)

플랫폼 러너는 결과를 우선 JSON lines 로 쓰고, 업로더가 DB 에 넣는다. 네트워크가 끊겨도 로컬 파일이 남는다 (필드 · 공장에서 중요).

```text
{"run_id": "nightly-0", "test": "test_rc_loss_failsafe", "status": "pass", "error": null,
 "fw_sha": "deadbee", "product": "fpv_bf", "stack": "betaflight-fork", "hw_rev": "FC-H7-revB",
 "dut_serial": "FPV-0002", "bench_id": "B2", "instrument_cal_due": "2027-03-01",
 "t_start": 0, "duration_s": 40,
 "measurements": [{"name": "failsafe_latency_ms", "value": 101.881, "low": 0.0, "high": 150.0,
                   "unit": "ms", "passed": true}, ...]}
```

(실제 `platform_sketch.py` 출력은 한 줄이다. 여기선 읽기 쉽게 줄을 나눴다.)

### 9.3 시계열 저장

| 질문 | 저장 위치 |
|---|---|
| 이 테스트가 통과했나, 측정값은? | Results DB (행 수 적음, 관계형 조인 필요) |
| 4시간 soak 동안 전류 · 온도 · CPU 부하 · 링크 품질 추이 | Time-series DB |
| 비행/벤치 중 FC 내부 고속 값 (PID, 자이로) | 파일(BBL/ULog) → artifact store, 필요시 요약만 TSDB |

InfluxDB 를 쓸 때 주의 [일반 지식 · 세부 버전 확인 필요]:

- tag(색인됨, 문자열)와 field(값)를 구분한다. `bench_id`, `product`, `fw_sha` 는 tag 후보, 전류 · 온도는 field.
- tag 조합 수(series cardinality)가 폭발하면 성능이 무너진다. `run_id` 나 `dut_serial` 을 무심코 tag 로 넣으면 시리즈가 계속 늘어난다. 어떤 키를 tag 로 둘지 설계가 필요.
- 대안: TimescaleDB(Postgres 확장)면 Results DB 와 같은 엔진에서 조인이 쉽다. 공고에 InfluxDB · Grafana · PostgreSQL 이 다 나오므로 [추정] 이미 있는 스택에 맞추는 게 우선.

### 9.4 아티팩트 색인

- 파일은 object storage 에, 메타데이터(종류 · sha256 · 크기 · 어느 result 에 속하는지)는 DB 에.
- 경로 규칙 예: `artifacts/<fw_sha>/<run_id>/<test>/<bench>/<kind>.<ext>`. 또는 content-addressed (`sha256/ab/cd...`) + DB 매핑.
- 보존 정책: pass 는 30일, fail 은 1년, release qual 은 영구 [추정 예시 · 정책은 팀 결정].
- BBL/ULog 는 업로드 시 요약(최대 지연, 루프 시간 히스토그램, 에러 카운터)을 뽑아 measurement 로도 저장. 그래야 파일을 안 열고도 추세를 본다.
- 공고(Manager, Autonomy Evaluation / Senior Data Platform Engineer)에 "log catalog and query layer", 로그 색인이 나온다. FW 테스트 아티팩트도 같은 카탈로그에 넣으면 비행 로그와 벤치 로그를 같은 질의로 찾는다 [추정].

### 9.5 대시보드

| 대시보드 | 축 | 보는 사람 | 질문 |
|---|---|---|---|
| 게이트 건강 | 날짜 × pass/fail/flaky/infra 비율 | 전원 | CI 빨간불을 믿을 수 있나 |
| 제품 × FW 버전 pass rate | fw 버전(시간순) × 제품 | FW 리드, 릴리스 | 언제부터 깨졌나 |
| 성능 메트릭 추세 | fw 버전 × failsafe 지연 p50/p99, 루프 시간, CPU | FW 개발자 | 느려졌나 |
| 벤치 헬스 | 벤치 × infra 실패율, 가동률, 교정 만료 D-day | 벤치 운영 | 어느 벤치를 고쳐야 하나 |
| flaky 보드 | 테스트 × flaky 율, quarantine 목록, 기한 | 테스트 소유자 | 무엇을 고쳐야 하나 |
| 커버리지 / 추적성 | 요구사항 × 최근 결과 | 릴리스, 품질 | 이 요구사항 증거가 있나 |
| 공장 수율 | 날짜 × 스테이션 × FPY | 생산 · 품질 | 어느 단계에서 떨어지나 |

### 9.6 회귀 탐지 — 통과/실패 말고 "값의 이동"

pass/fail 만 보면 limit 안에서 천천히 나빠지는 것을 못 본다. 예: failsafe 지연이 100 → 120 → 140 ms (limit 150). 모두 pass 지만 추세는 나쁘다.

| 방법 | 설명 | 쓰는 곳 |
|---|---|---|
| 기준선 비교 | 현재 fw 의 N회 분포 vs 직전 release 분포. 중앙값 차이 + 비모수 검정(Mann-Whitney U) | nightly 메트릭 |
| CUSUM | 누적합으로 평균 이동 감지. 작은 이동이 쌓이면 경보 | 연속 nightly 추세 |
| Change-point detection | 시계열에서 분포가 바뀐 지점 탐색 → 그 구간의 커밋 범위 제시 | 회귀 구간 bisect 범위 좁히기 |
| 제어 차트 (SPC) | 평균 ± 3σ 관리 한계 | 공장 측정값. 공고(Senior Quality Engineer)에 "SPC" 언급 |
| 자동 bisect | change-point 가 가리킨 커밋 범위에서 벤치로 이분 탐색 | 밤 시간 남는 벤치 활용 |

- 주의: 벤치 · 계측기 차이를 통제해야 한다. 같은 벤치 · 같은 DUT 에서 비교하거나, 벤치를 랜덤 효과로 보고 비교.
- 성능 메트릭은 "limit" 과 별도로 "회귀 허용폭" 을 둔다. 예: p99 가 직전 release 대비 10% 이상 나빠지면 경보. 공고(Senior Platform Engineer)의 "latency to be enforced as a release gate" 를 구현하는 방법이다.

### 9.7 추적성 매트릭스 자동 생성

- 테스트에 `@pytest.mark.req("SYS-FS-001")` 을 단다.
- 수집 단계에서 marker 를 읽어 `test_requirements` 테이블을 채운다.
- 릴리스 리포트: 요구사항마다 "연결된 테스트 · 최근 결과 · fw sha · 증거 아티팩트 링크".
- 테스트 없는 요구사항, 요구사항 없는 테스트를 자동으로 보여 준다.
- 방산 고객 대상이면 이 매트릭스가 계약 산출물일 수 있다. 공고(Project Engineer)에 "contractual engineering deliverables" [문맥상 추정].

---

## 10. 벤치 fleet 운영

### 10.1 인벤토리

| 항목 | 내용 | 저장 |
|---|---|---|
| 벤치 | id, 풀, 호스트, 위치, 소유자, 상태 | `benches/*.yaml` → DB |
| DUT | 시리얼, 제품, hw rev, 상태(active/golden/spare/rma), 누적 사용 시간 · 플래시 횟수 | DB |
| 계측기 | 자산 번호, 모델, 시리얼, 펌웨어 버전, 교정 만료 | DB |
| 배선 맵 | capability → 포트/채널 | 벤치 YAML (git) |
| 소모품 | USB 케이블, 커넥터, 프롭 없는 모터 | 교체 이력 |

- 시작은 Git 저장소의 YAML 이 정답이다. 리뷰 · 이력이 공짜다. 수십 대를 넘고 상태가 자주 바뀌면 DB 로.
- USB 장치는 `/dev/serial/by-id/` 또는 udev rule 로 고정 이름. labgrid 도 udev 기반으로 USB 장치를 ManagedResource 로 발견한다 (labgrid 문서).

### 10.2 헬스체크

| 언제 | 무엇 |
|---|---|
| 잡 시작 전 (preflight) | 포트 존재, 계측기 `*IDN?` 응답, PSU 출력 off 상태, 교정 유효, 디스크 여유 |
| 매일 아침 / 밤 시작 전 | golden DUT 로 smoke 실행 → 결과가 기준 범위인지 |
| 실패 후 | 벤치 상관 실패 감지 시 자동 헬스체크 → 실패하면 풀에서 제외 |
| 주간 | 케이블 · 커넥터 육안 점검, 플래시 횟수 많은 DUT 교체 검토 |

- **golden unit**: 알려진 좋은 펌웨어가 든 알려진 좋은 보드. 벤치가 정상인지 판정하는 기준. FW 테스트 대상과 분리 보관.
- 헬스체크 실패 → 벤치 `healthy=false` → 스케줄러가 자동 제외 (`platform_sketch.py` 의 `usable()`), 담당자 알림.

### 10.3 교정 추적

- 계측기 교정 만료가 지나면 그 계측기로 잰 값은 품질 감사에서 무효가 될 수 있다.
- 스케줄러가 교정 만료 벤치를 **자동 제외** (예제의 B5). D-30 부터 대시보드 경고.
- 결과마다 그 시점 교정 정보를 스냅샷 (9.1 `result_instruments`).
- 계측기 펌웨어 버전도 기록. 계측기 펌웨어 업데이트가 측정 동작을 바꿀 수 있다. 업데이트는 계획적으로, 한 벤치씩.

### 10.4 원격 접근

- 엔지니어가 집/사무실에서 벤치를 예약하고, 콘솔 · 전원 · 플래시를 원격으로. labgrid client 가 이 용도 (place 예약, 콘솔, 전원 제어).
- 방산 환경이면 네트워크 분리 · 접근 통제가 강하다. 공고(Senior DevOps Engineer)에 "secure environments" [세부는 추정].
- 원격 사용 중 벤치는 CI 풀에서 빠진다. 예약 시간 제한(TTL) 필수. "B3 를 3일째 잡고 있음" 방지.

### 10.5 예비 · golden 유닛 전략

| 종류 | 용도 | 대략 비율 [추정 예시] |
|---|---|---|
| active DUT | 벤치에 장착, 매일 테스트 | 벤치당 1 |
| golden DUT | 벤치 헬스 판정 기준 | 풀당 1~2 |
| spare DUT | 고장 · 소모 시 즉시 교체 | active 의 20~30% |
| 예비 계측기 | 교정 보낼 때 대체 | 기종당 1 |

- 플래시 횟수 · 전원 사이클 횟수를 DUT 별로 세면 소모성 고장(플래시 마모, 커넥터 마모)을 예측할 수 있다. Don의 SSD 경험(NAND 신뢰성)과 정확히 같은 사고.

### 10.6 규모 계산

예시 가정 [전부 가상 숫자]:

| 변수 | 값 |
|---|---|
| 하루 머지 수 M | 40 |
| G1 HIL smoke 1회 시간 (setup 포함) | 12분 |
| 제품 수 P (G1 에서 각각 돌림) | 2 |
| 근무 시간 중 머지 집중 구간 | 10시간 |
| 목표 벤치 가동률 (여유 포함) | 60% |
| nightly 전체 suite (제품당, 1회) | 90분 |
| nightly 반복 횟수 R | 3 |
| 밤 시간 창 | 8시간 |

계산:

```text
G1 수요(제품당)  = M × 12분 = 40 × 12 = 480 분/일 = 8 벤치·시간
제품당 필요 벤치 = 8 시간 / (10 시간 × 0.6) = 1.33 → 2대
G1 전체          = 2 제품 × 2대 = 4대

nightly 수요     = P × R × 90분 = 2 × 3 × 90 = 540 분 = 9 벤치·시간
nightly 가용     = 4대 × 8시간 = 32 벤치·시간 → 충분 (남는 시간에 soak · 자동 bisect)

대기 시간 감각   : 가동률이 80% 를 넘으면 큐 대기 시간이 급격히 늘어난다 (대기행렬 일반 성질)
                  → 머지 게이트 벤치는 60% 근처로 운영
```

- 머지 게이트 벤치와 nightly/soak 벤치를 섞어 쓰면, soak 가 낮에 끝나지 않아 게이트가 막힌다. 풀을 나누거나 우선순위 선점(preemption) 규칙을 둔다.
- 비용이 벽이면 G1 을 "변경된 영역에 해당하는 제품만" 돌린다 (Bazel 의존 그래프로 영향 범위 계산). 다만 공통 코드 변경은 전 제품.

---

## 11. 조직과 프로세스

### 11.1 플랫폼을 내부 제품으로

공고(Lead, Platform Software)의 "treating every other engineering team as your customer" 가 테스트 플랫폼에도 그대로 맞는다.

| 제품 요소 | 테스트 플랫폼에서 |
|---|---|
| API | capability 인터페이스, fixture 이름, marker 이름, 결과 스키마 |
| 버전 | 플랫폼 패키지 semver. 인터페이스 깨는 변경은 major + 마이그레이션 노트 |
| 문서 | "첫 테스트 30분 안에 쓰기" 튜토리얼, capability 목록, 벤치 예약 방법 |
| 온보딩 | FW 개발자가 자기 기능 테스트를 직접 추가 → 테스트팀은 리뷰 · 플랫폼 개선 |
| 지원 | 채널 · 당번 · "벤치 이상" 신고 양식 |
| 품질 | 플랫폼 자체 CI (fake DUT 로 단위 테스트 — `platform_sketch.py` 의 self_test 처럼) |
| 지표 | 11.4절 |

### 11.2 소유 모델

| 대상 | 소유 | 기여 |
|---|---|---|
| L2~L4, L7, L8 (플랫폼 코어) | 테스트 플랫폼 팀 | 누구나 PR, 플랫폼 팀 승인 |
| L5 시나리오 | 기능 소유 팀 (FW · GCS · 라디오) | 테스트팀이 리뷰 · 템플릿 제공 |
| limits | 요구사항 소유자 + 품질 | 변경은 리뷰 필수, 이력 보존 |
| 벤치 하드웨어 | 테스트팀 + 테크니션 | 배선 변경은 YAML PR 로 |
| CI 게이트 정책 | FW 리드 + 테스트 리드 합의 | — |
| quarantine 결정 | 테스트 소유자, 기한 필수 | 플랫폼 팀이 자동화 |

- "테스트는 테스트팀이 다 짠다" 모델은 확장이 안 된다. 테스트팀은 **테스트를 쉽게 쓰게 만드는 팀**이 되어야 한다.

### 11.3 성숙도 로드맵 (Phase 0 ~ 4)

전제 [추정]: Don 이 온사이트에서 받은 인상으로는 FW 테스트팀이 새로 꾸려지는 중이다. 아래는 그런 스타트업 테스트팀을 위한 제안이지 Neros 현황이 아니다.

| Phase | 기간 감각 | 목표 | 산출물 | 완료 기준 |
|---|---|---|---|---|
| 0 현황 파악 | 2~4주 | 지금 무엇이 있는지 · 가장 아픈 곳 | 기존 스크립트 · 벤치 · 수동 절차 목록, 실패 사례 10개 | 우선순위 합의 |
| 1 벤치 1대 자동화 | 1~2개월 | 한 제품 · 한 벤치 · smoke 5~10개를 사람 없이 | 드라이버 · 어댑터 · capability 최소 세트, pytest suite, 결과 JSON | 매일 nightly 가 자동으로 돈다 |
| 2 CI 게이트 | +1~2개월 | 머지 게이트에 HIL smoke | runner tag, 예약, junit 리포트, flaky 정책 v1 | G1 이 켜지고 빨간불 신뢰도 측정 시작 |
| 3 다제품 · 다벤치 | +3~6개월 | 두 번째 스택(PX4 변형) 추가 + 벤치 풀 | descriptor · limits-as-data, 스케줄러, Results DB, 대시보드 | 새 제품 추가가 코어 변경 없이 |
| 4 플랫폼화 | +6개월~ | 다른 팀 자급 · 공장/RMA 재사용 · 회귀 탐지 | 문서 · 온보딩, OpenHTF 류 공장 연동, change-point 경보, 추적성 리포트 | 테스트 커밋의 절반 이상이 테스트팀 외 |

- Phase 1 에서 처음부터 capability 구조로 시작하는 게 핵심. 제품 하나일 때는 과해 보여도, 비용이 거의 없고 Phase 3 에서 갈아엎는 비용을 없앤다.
- 반대로 Phase 1 에 스케줄러 · DB · 대시보드까지 만들면 과설계. JSON 파일 + 간단한 HTML 리포트로 충분.

### 11.4 플랫폼 팀 지표

| 지표 | 의미 |
|---|---|
| G1 소요 시간 p50/p90 | 개발자 체감 |
| 빨간불 중 진짜 회귀 비율 | 신뢰도 |
| flaky 수 · quarantine 평균 기간 | 부채 |
| 벤치 가동률 · MTTR | 운영 |
| 새 테스트 추가 리드타임 | 플랫폼 사용성 |
| 필드 이슈 중 "테스트로 잡을 수 있었던" 비율 | 커버리지 갭 |
| 필드 이슈 → 회귀 테스트 편입 시간 | 학습 고리 |

---

## 12. Build vs Buy

| 선택지 | 무엇 | 장점 | 단점 | 이 단원 층 | 드론 FTE 적합도 [의견] |
|---|---|---|---|---|---|
| pytest | Python 테스트 러너 | 생태계 · fixture · 플러그인 · 개발자 친숙 | 측정/limit · 운영자 UI 는 직접 | L6 | 뼈대로 강력 추천 |
| labgrid | 보드 제어 · 원격 · 예약 (resource/driver/strategy, coordinator/exporter, pytest 플러그인) | 오픈소스, 예약 · 원격 · 상태 전이 내장 | Linux 보드 중심 설계, MCU/RF 계측기 드라이버는 직접 | L2 · L4 · L7 | 벤치 수 늘 때 도입 가치 큼 |
| LAVA | 대규모 보드 팜 (Linaro) | 대규모 검증, health check | 무겁고 Linux 부팅 중심 | L7 · L8 | Jetson 쪽엔 고려, FC 엔 과함 [추정] |
| OpenHTF | 하드웨어 테스트 프레임워크 (phase/measurement/plug) | measurement + spec, 결과 record, station UI | 개발 활동도 · 커뮤니티 작음 [확인 필요] | L5 · L6 (공장) | 공장 EOL 에 적합 |
| Robot Framework | 키워드 기반 | 비개발자 가독성, Renode 통합 | 복잡 로직 어려움, 두 언어 유지 | L6 | 운영자 절차 한정 |
| twister | Zephyr 테스트 러너 | 하드웨어 맵 · fixture 개념 | Zephyr 전용 | L6 · L7 | 개념만 차용 |
| pytest-embedded | Espressif pytest 플러그인 | service 조합, QEMU/Wokwi | ESP 중심 | L4 · L6 | 개념 차용, serial service 정도 |
| Renode | 시스템 에뮬레이터 (Antmicro) | 하드웨어 없이 CI 에서 FW 실행, Robot 통합 | 주변장치 모델 정확도 · 커스텀 보드 모델링 비용 | L1 대체 (가상 HIL) | STM32 FC 를 CI 에서 돌릴 수 있는지 PoC 가치 [확인 필요: 대상 MCU 지원] |
| QEMU | 범용 에뮬레이터 | 무료, Linux 쪽 강함 | MCU 주변장치 모델 제한 | L1 대체 | Jetson 사용자 공간 쪽 |
| NI TestStand | 상용 시퀀서 | 공장 · 운영자 UI · 리포트 · 지원 | 라이선스 비용, Windows · LabVIEW 생태계, git 친화성 낮음 [일반 평가] | L6 · L8 | 공장 라인에 이미 있으면 연동 |
| NI VeriStand | 실시간 HIL 플랫폼 | 실시간 I/O, 모델 실행 | 고가, 항공/자동차 중심 | L1 · L2 | 센서 시뮬레이션 HIL 이 필요할 때 |
| dSPACE / Speedgoat | 실시간 HIL 시뮬레이터 하드웨어 | 플랜트 모델 실시간 실행, 인증 이력 | 매우 고가, Simulink 중심 | L1 · L2 | 소형 드론 FW 회귀엔 과함 [의견] |
| 자체 구축 | 위 조합 + 자체 코드 | 정확히 맞춤 | 유지보수 · 인력 | 전 층 | 현실적으로 "오픈소스 조합 + 얇은 자체 코어" |

결론 [Don 의견으로 제시할 수 있는 안]: **pytest(러너) + 자체 capability 코어(얇게) + labgrid 류 예약/원격 + Postgres/Grafana(이미 있는 스택) + 공장은 OpenHTF 류 또는 기존 MES 연동**. 상용 HIL 시뮬레이터는 센서 주입이 핵심 요구가 될 때 재검토.

---

## 13. 예제 코드 — `platform_sketch.py`

파일: [`../code/platform_sketch.py`](../code/platform_sketch.py). Python 3.9+, 표준 라이브러리만, 전부 fake.

### 13.1 구성

| 부분 | 이 단원 절 | 내용 |
|---|---|---|
| `FakeAirframe` | L1 대체 | arm, RC 프레임, failsafe 지연(+잡음), 저전압 경고. `rc_link_glitch` 로 벤치 배선 불량 흉내 |
| `RcInput` / `CrsfRcInput` / `MavlinkRcInput` | 4.4, 5 | 같은 인터페이스, 다른 프로토콜 |
| `Telemetry` / `MspTelemetry` / `MavlinkTelemetry` | 5.3 | 정규화된 status dict |
| `Power`, `Flash`, `Companion` | 4.4 | capability. `Power.off()` 는 idempotent |
| `ADAPTERS`, `PRODUCTS` | 4.4~4.6 | 문자열 → 구현 레지스트리, 제품 descriptor(데이터) |
| `build_dut` | L4 | descriptor 로 DUT 조립 |
| `@test_case(...)` + 테스트 4개 | L5, P8 | 필요한 capability 선언 |
| `Ctx.measure` / `Measurement` | P2, P3 | 측정 + limit 은 제품 데이터에서 |
| `select` | P8 | 실행/스킵 + 사유 |
| `Bench.acquire/release/usable` | L7, 10.3 | lock, 교정 만료 · unhealthy 제외 |
| `setup` / `teardown` / `run_one` | P5 | idempotent setup, `finally` teardown, pass/fail/error 구분 |
| `schedule` | L8 | 가장 빨리 비는 맞는 벤치에 배정 (이산 시간) |
| `flaky_report` | 8.6 | stable_pass / stable_fail / flaky, quarantine, 벤치 상관 의심 |
| `self_test` | 11.1 | 플랫폼 자체 테스트 |

### 13.2 자체 테스트가 확인하는 것

1. 책상 셋업(`fpv_bf_desk`)에선 power · companion 테스트가 사유와 함께 스킵된다.
2. PX4 변형은 모든 테스트가 선택되고, RC 어댑터가 `MavlinkRcInput` 으로 조립된다.
3. 같은 failsafe 테스트가 PX4 limit(600 ms, 가상)으로 판정된다. teardown 후 전원이 꺼져 있다.
4. 벤치 lock: 이중 획득은 `BenchBusy`, 남의 lock 해제는 무시, 이중 해제는 안전.
5. 20회 nightly: 교정 만료 벤치 B5 와 다른 제품 벤치 D1 은 안 쓰인다. FPV 에 companion 테스트는 "unschedulable". 모든 레코드에 추적 키가 있다.
6. B3 의 배선 불량(프레임 8% 유실)이 `test_arm_disarm` 을 flaky 로 만들고, `suspect_bench == "B3"` 로 짚어 낸다.
7. FW 가 failsafe 를 400 ms 로 늦추면 모든 벤치에서 실패 → `stable_fail` (flaky 가 아니라 진짜 회귀).

### 13.3 실행 결과 (2026-10-24, 로컬 실행)

```text
$ python3 platform_sketch.py
== sample result record (JSON line) ==
{"run_id": "nightly-0", "test": "test_rc_loss_failsafe", "status": "pass", "error": null, "fw_sha": "deadbee", "product": "fpv_bf", "stack": "betaflight-fork", "hw_rev": "FC-H7-revB", "dut_serial": "FPV-0002", "bench_id": "B2", "instrument_cal_due": "2027-03-01", "t_start": 0, "duration_s": 40, "measurements": [...]}
== selector on desk setup ==
 run    : ['test_arm_disarm', 'test_rc_loss_failsafe']
 skipped: [('test_low_voltage_warning', 'missing capability: power'), ('test_companion_heartbeat', 'missing capability: companion')]
== flaky report (10 nightly runs, fw deadbee) ==
  test_arm_disarm/fpv_bf/deadbee {'runs': 10, 'fail_rate': 0.2, 'verdict': 'flaky', 'quarantine': True, 'suspect_bench': 'B3', 'per_bench': {'B1': '0/2', 'B2': '0/4', 'B3': '2/4'}}
  test_rc_loss_failsafe/fpv_bf/deadbee {'runs': 10, 'fail_rate': 0.0, 'verdict': 'stable_pass', ..., 'per_bench': {'B1': '0/9', 'B2': '0/1'}}
  ...
SELF-TEST PASS
```

- 마지막 줄의 `per_bench` 를 보면 `test_rc_loss_failsafe` 는 B1 에서 9번, B3 에서는 0번 돌았다. 장난감 스케줄러가 벤치를 고르게 회전시키지 않아서다. B3 의 배선 불량이 이 테스트에선 드러나지 않은 이유다. 8.6절의 "벤치 회전" 이 왜 필요한지 보여 주는 실제 예.
- `--jsonl` 옵션으로 결과만 JSON lines 로 뽑아 DB 업로더 연습에 쓸 수 있다.

### 13.4 이 스케치를 키우는 순서

1. fake 어댑터 하나를 진짜로 바꾼다: `CrsfRcInput` 을 pyserial + CRSF 인코더로 (책상에서 FC 1개, 프롭 없이).
2. `Ctx` 를 pytest fixture 로 옮긴다 (6.2절).
3. `PRODUCTS` 와 limit 을 YAML 파일로 뺀다 (표준 라이브러리만 고집하면 JSON).
4. `schedule` 에 벤치 회전과 LPT 샤딩을 넣는다.
5. JSON lines 를 SQLite 에 넣고 `flaky_report` 를 SQL 로 다시 쓴다.

---

## 14. SSD 검증에서는 이랬다 → 드론에서는 이렇다

Don의 이전 경험(데이터센터 SSD, 2018-10~2025-11): 챔버 테스트 플랫폼 SDK(온도 제어 · 스케줄링 · eSSD 상태 모니터링 · UART 테스트 시퀀스 자동화, 다른 엔지니어가 사용), web 기반 test automation, chip reliability system, NVMe telemetry 디버그 기능. 최근: factory test-node 아키텍처, stress 기반 latent defect 발굴.

### 14.1 매핑 표

| 플랫폼 요소 | SSD 검증에서는 이랬다 | 드론 FTE 에서는 이렇다 |
|---|---|---|
| 환경 자극 | 챔버 온도 제어 | PSU 전압 램프, RF 감쇠기, (필요시) 환경 챔버, 진동 |
| 스케줄링 | 챔버 · 슬롯에 테스트 배정 | 벤치 풀에 테스트 × 제품 배정 (L8) |
| DUT 상태 모니터링 | eSSD 상태 모니터링 | MSP/MAVLink 텔레메트리, blackbox/ULog |
| 테스트 시퀀스 | UART 테스트 시퀀스 자동화 | CRSF/MAVLink 시나리오 (L5) |
| 플랫폼 사용자 | 다른 엔지니어가 SDK 사용 | FW · GCS · 라디오 개발자가 테스트 직접 추가 (11.1) |
| 웹 UI | web 기반 test automation | 대시보드 · 벤치 예약 · 결과 탐색 |
| 장치 내부 계측 | NVMe telemetry 디버그 기능 | blackbox/ULog, debug 카운터 → "firmware measures, host judges" (P3) |
| 신뢰성 | NAND · PCIe 커버리지, latent defect | soak, 전원 사이클 반복, 커넥터/플래시 마모, 공장 스트레스 |
| 공장 | factory test-node 아키텍처 | EOL 스테이션, provisioning, 수율 · SPC (7.3) |
| 프로토콜 | NVMe, PCIe, I2C/SPMI/RFFE | CRSF, MSP, MAVLink, DShot, UART/SPI/I2C |
| 안전 | 열 · 전원 | **회전체** (프롭) · LiPo · RF 송출 규제 — 훨씬 위험 |
| 결정론 | 대체로 결정론적 호스트 명령 | RF · 제어 루프 · 물리 → 통계 판정 비중 증가 |
| 릴리스 근거 | qual 리포트 | qual + **비행 시험** + 현장(전장) 피드백 |
| 이질성 | 제품군 내 컨트롤러/NAND 변형 | 완전히 다른 펌웨어 스택 (Betaflight 포크 vs PX4) 공존 |
| 툴체인 | (사내 도구) | GitLab CI / Jenkins / Bazel — Don 은 실무 경험 없음, 학습 필요 |

### 14.2 이야기로 엮기

Don이 할 수 있는 정직한 이야기의 뼈대:

1. "SSD 에서 챔버 플랫폼 SDK 를 만들 때 핵심은 '온도 · 스케줄 · 모니터링 · 시퀀스' 를 엔지니어가 조립해 쓰게 하는 것이었다. 다른 엔지니어가 그 SDK 로 자기 테스트를 만들었다."
2. "드론에서 같은 문제는 '기체 · 펌웨어 스택 · 벤치' 가 여러 개라는 것. 그래서 테스트 의도 · 프로토콜 · 벤치를 나누고, DUT 를 capability 조합으로 모델링하겠다."
3. "장치가 스스로 재고 호스트가 판정하는 구조는 NVMe telemetry 디버그 기능을 만들 때 쓴 원칙과 같다. 드론에선 blackbox/ULog 와 계측기 캡처가 그 역할."
4. "factory test-node 를 설계해 봤기 때문에, 같은 테스트 라이브러리를 공장 EOL 까지 끌고 가는 그림이 자연스럽다. limit 은 데이터로, 단계마다 다르게."
5. "GitLab CI · Bazel · pytest 는 실무로 써 보지 않았다. 대신 이 구조를 작은 스케치로 만들어 봤다 (`platform_sketch.py`). 실제 도구로 옮기는 건 빠르게 할 수 있다."

> 숫자 · 회사 내부 도구 이름 · 고객 이름은 넣지 않는다 (NOTES_SPEC). "몇 대 챔버 · 몇 명 사용자" 같은 숫자는 Don이 직접 확인한 것만.

### 14.3 다른 점에서 오는 함정

| SSD 습관 | 드론에서 조심할 점 |
|---|---|
| 테스트 대상이 단일 펌웨어 | 스택이 여러 개 → 추상화가 처음부터 필요 |
| 호스트 명령 → 응답이 대부분 | 연속 스트림(RC 50~500 Hz [확인 필요: 주기는 링크 설정 따라], 텔레메트리) → 시간 정렬 · 버퍼링 |
| 챔버 안은 사람과 분리 | 벤치 위 모터 · LiPo → 안전 인터록이 플랫폼 기능 |
| 결과가 비교적 결정론 | RF · 물리 → 분포 판정, flaky 관리 비중 큼 |
| 릴리스 = 랩 qual | 비행 시험 · 전장 피드백이 릴리스 근거에 포함 |

---

## 15. 테스트 아이디어 · 설계 체크리스트

### 15.1 플랫폼 설계 리뷰 체크리스트

- [ ] 테스트 코드에 제품 이름 분기(`if product ==`)가 없다
- [ ] 테스트 코드에 포트 경로 · IP · 채널 번호가 없다 (전부 벤치 YAML)
- [ ] 테스트 코드에 합격 숫자가 없다 (전부 limits 데이터)
- [ ] 모든 테스트가 필요한 capability 를 선언한다
- [ ] 스킵에는 항상 사유가 남는다
- [ ] setup 이 이전 상태를 가정하지 않는다 (전원 사이클 · 재플래시 · 설정 초기화)
- [ ] teardown 이 `finally` 로 보장되고 두 번 불러도 안전하다
- [ ] 결과 레코드에 fw_sha, hw_rev, dut_serial, bench_id, 계측기 교정, test_sha, limits_version 이 있다
- [ ] pass / fail / error / infra / skip 이 구분된다
- [ ] 시간 민감한 판정은 계측기나 펌웨어 타임스탬프로 한다
- [ ] 벤치 예약이 있고 TTL 이 있다
- [ ] 교정 만료 · unhealthy 벤치는 자동 제외된다
- [ ] 테스트 fail 은 자동 재시도로 덮지 않는다
- [ ] flaky 는 기한 있는 quarantine 으로 관리된다
- [ ] nightly 가 벤치를 회전시킨다
- [ ] 플랫폼 코어에 fake DUT 기반 자체 테스트가 있다
- [ ] 새 제품 추가 절차가 문서로 있고, 코어 변경 없이 된다
- [ ] 안전 인터록(프롭 없음, 전류 제한, E-stop)이 소프트웨어로도 확인된다
- [ ] 아티팩트(BBL/ULog/캡처)가 결과와 연결되어 검색된다
- [ ] 요구사항 → 테스트 → 결과 추적성이 자동 생성된다

### 15.2 플랫폼 자체를 시험하는 테스트 아이디어

| 아이디어 | 무엇을 검증 |
|---|---|
| 벤치 B 의 USB 를 테스트 중간에 뽑는다 | infra 로 분류되나, 벤치가 unhealthy 로 빠지나, 락이 풀리나 |
| PSU 를 응답 없게 만든다 (네트워크 차단) | preflight 가 잡나, 타임아웃이 무한 대기가 아닌가 |
| 이전 테스트가 arm 상태로 죽게 만든다 | 다음 테스트 setup 이 안전 상태로 복구하나 |
| 같은 잡을 두 파이프라인에서 동시에 | 예약이 상호 배제하나 |
| 교정 만료일을 어제로 바꾼다 | 스케줄러가 제외하나, 대시보드 경고 |
| descriptor 에 없는 capability 이름 | CI 의 스키마 검증이 머지 전에 잡나 |
| 결과 DB 를 끊는다 | 로컬 JSON lines 가 남고 나중에 업로드되나 |
| 일부러 느려진 FW (지연 +30%) | limit 안이어도 회귀 탐지가 경보하나 |
| 벤치 하나에만 잡음 주입 | flaky + 벤치 상관이 짚어 내나 (`platform_sketch.py` 6번) |

---

## 16. 직접 해 볼 것

1. `python3 code/platform_sketch.py` 실행 → 출력의 flaky 리포트를 읽고, B3 의 `rc_link_glitch` 를 0.2 로 바꿔 판정이 어떻게 바뀌는지 본다.
2. `schedule()` 을 고쳐 nightly 에서 벤치를 라운드로빈으로 회전시킨다. 그 뒤 `test_rc_loss_failsafe` 의 `per_bench` 분포가 고르게 바뀌는지, B3 문제가 이 테스트에서도 드러나는지 확인.
3. 새 제품 `fpv_bf_sbus` (RC 를 SBUS 로 주입) 를 추가한다. `SbusRcInput` 클래스 + `ADAPTERS` 한 줄 + `PRODUCTS` 항목만으로 되는지 확인 — 테스트 함수는 건드리지 않는다.
4. `--jsonl` 출력을 SQLite 에 넣고 (`sqlite3` 표준 모듈), "제품별 failsafe 지연 평균/최대" 와 "벤치별 실패율" 을 SQL 로 뽑는다.
5. CUSUM 을 30줄로 구현해, fw 버전마다 failsafe 지연 평균을 3 ms 씩 늘린 가짜 시계열에서 몇 번째 버전에 경보가 뜨는지 본다.
6. pytest 를 설치해 6.2절 `conftest.py` 개념 예시를 fake DUT 로 실제 동작하게 만든다 (`--product`, `requires` marker 자동 스킵, `--junitxml`).
7. labgrid 를 설치해 `labgrid-client` 로 로컬 coordinator · exporter 를 띄우고, USB-UART 하나를 place 로 등록해 예약/해제를 해 본다. (USB-UART 어댑터가 없으면 사기 전에 Don과 예산 확인.)
8. GitLab 에 개인 프로젝트를 만들고 Mac 에 runner 를 등록, tag `hil-desk` 로 6번 pytest 를 돌려 MR 화면에 junit 리포트가 뜨는지 확인. `resource_group` 으로 두 파이프라인 동시 실행을 막아 본다.
9. Renode 로 STM32 보드 예제를 돌리고 `renode-test` 로 Robot 테스트 하나 실행 → "하드웨어 없는 CI 단계" 감 잡기. Betaflight 타깃 MCU 가 지원되는지 확인.
10. OpenHTF 예제 하나를 돌려, phase · measurement · plug 가 `platform_sketch.py` 의 어디에 대응하는지 표로 다시 써 본다.
11. Betaflight 를 실제 FC 에 올려 CRSF 주입을 하는 단계로 가면: **프롭 제거**, 전류 제한 PSU 사용, LiPo 대신 벤치 PSU. 모터 회전 실험은 프롭 없이, 손 · 케이블이 닿지 않게 고정. FC · 수신기 · PSU 구매는 사기 전에 Don과 예산 확인.

---

## 17. 확인 안 된 것

| 항목 | 상태 |
|---|---|
| Neros 실제 제품 구성 (Archer, Archer AI 등) | [Gemini 주장 · 미확인] |
| Neros 테스트팀 현재 구조 · 성숙도 | [추정 — Don 의 온사이트 인상] |
| Neros 가 GitLab CI 와 Jenkins 중 무엇을 주로 쓰는지 | 공고는 "Gitlab CI, Jenkins" 둘 다 언급. DevOps 공고는 GitLab CI 예시. [확인 필요] |
| Neros 가 monorepo + Bazel 인지 | 공고에 Bazel 반복 등장 → [추정] |
| InfluxDB/Grafana/Postgres 가 테스트 결과에 쓰이는지 | 공고는 다른 팀(Full-Stack, Data Platform) 문맥 → [추정] |
| ArduPilot autotest base class 정확한 이름 | `vehicle_test_suite.py` 의 base class 존재는 문서 확인. 클래스 이름(`TestSuite` 로 기억)은 [확인 필요] |
| GitLab `retry:when` 에서 `stuck_or_timeout_failure` 대체 값 이름 | deprecated 확인, 대체 값 [확인 필요] |
| `artifacts:reports:junit` 세부 동작 | 키워드 존재 확인, 세부 [확인 필요] |
| Bazel `flaky=True` / `--flaky_test_attempts` 기본 재시도 횟수 | 출처마다 표현 다름 [확인 필요: Bazel user manual] |
| OpenHTF 최근 개발 활동도 | README 수치만 확인, 최근 커밋 날짜 [확인 필요] |
| Renode 가 Betaflight 타깃 MCU(STM32F4/F7/H7)와 그 주변장치를 얼마나 지원하는지 | [확인 필요] |
| LAVA 가 MCU 보드에 쓰이는 사례 | [확인 필요] — LAVA 아키텍처 문서 URL 이 404 |
| CRSF baud(420000) · 채널 중앙값(992) · RC 프레임 주기 | [확인 필요] — R05 에서 확인 |
| 규모 계산 · 시간 예산 · quarantine 임계값 숫자 | 전부 가상 예시 |
| InfluxDB 버전별 tag/field · cardinality 동작 차이 | [확인 필요] |

---

## 18. 참고 링크

같은 노트 세트:

- [R00 개요 · Gemini 정정 목록](2026-10-24_R00_overview_and_gemini_corrections.md)
- [R01 FTE vs SSD 검증](2026-10-24_R01_fte_vs_ssd_validation.md)
- [R02 드론 시스템 · 오픈소스 지도](2026-10-24_R02_drone_system_and_open_source_map.md)
- [R03 Betaflight / INAV 내부와 테스트](2026-10-24_R03_betaflight_inav_internals_and_testing.md)
- [R04 PX4 / ArduPilot 와 테스트 생태계](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)
- [R05 ELRS · CRSF · MSP · MAVLink · DShot](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)
- [R06 Companion · 영상 · Linux · RTOS](2026-10-24_R06_companion_video_linux_rtos.md)
- [R07 테스트 방법론 피라미드](2026-10-24_R07_test_methodology_pyramid.md)
- [R08 HIL 벤치 설계 · 계측기](2026-10-24_R08_hil_bench_design_instruments.md) — 이 단원의 L1 · L2
- [R10 로드맵 · 실습 · 참고 자료](2026-10-24_R10_roadmap_labs_references.md)
- 예제 코드: [`../code/platform_sketch.py`](../code/platform_sketch.py)

외부 (이번에 확인한 것):

- labgrid overview (resource / driver / strategy, coordinator / exporter / place): https://labgrid.readthedocs.io/en/latest/overview.html
- labgrid usage (pytest 플러그인 `--lg-env`, `--lg-coordinator`, fixture `env`/`target`/`strategy`): https://labgrid.readthedocs.io/en/latest/usage.html
- OpenHTF: https://github.com/google/openhtf
- pytest-embedded (Espressif): https://docs.espressif.com/projects/pytest-embedded/en/latest/
- Zephyr twister 명령행 옵션: https://docs.zephyrproject.org/latest/develop/twister/commandline.html
- Renode + Robot Framework 테스트: https://renode.readthedocs.io/en/latest/introduction/testing.html
- Bazel Test Encyclopedia (tags, size/timeout, XML_OUTPUT_FILE, TEST_UNDECLARED_OUTPUTS_DIR): https://bazel.build/reference/test-encyclopedia
- Bazel flaky 관리 (Aspect 교육 자료): https://training.aspect.build/bazel-101/flakiness
- GitLab CI YAML 레퍼런스 (tags, resource_group, retry): https://docs.gitlab.com/ci/yaml/
- Jenkins Lockable Resources: https://plugins.jenkins.io/lockable-resources/
- ArduPilot autotest 프레임워크: https://ardupilot.org/dev/docs/the-ardupilot-autotest-framework.html
- PX4 MAVSDK 통합 테스트 (`test/mavsdk_tests/`): https://docs.px4.io/main/en/test_and_ci/integration_testing_mavsdk.html

외부 (이번에 열어 보지 않음 — 이름만, 읽을 때 확인):

- pytest 문서 (fixture, marker, parametrize, junitxml): https://docs.pytest.org/
- pytest-xdist: https://pytest-xdist.readthedocs.io/
- Hypothesis: https://hypothesis.readthedocs.io/
- Robot Framework: https://robotframework.org/
- LAVA: https://docs.lavasoftware.org/
- Allure Report: https://allurereport.org/
- InfluxDB: https://docs.influxdata.com/
- TimescaleDB: https://docs.timescale.com/
- Grafana: https://grafana.com/docs/
- NI TestStand / VeriStand, dSPACE, Speedgoat: 각 벤더 사이트

공고 근거: `../research/job_board_snapshots/2026-10-23_neros_jobs.json` (Firmware Test Engineer, Manager Autonomy Evaluation Data & Test, Senior Platform Engineer, Full-Stack Software Engineer III, Senior Data Platform Engineer, Senior DevOps Engineer, Lead Platform Software, Electrical Engineer - Board Design and Test, Senior Quality Engineer).

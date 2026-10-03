# 🏗️ N04 · System design — 테스트 프레임워크 · HIL 팜 · CI 결과 대시보드 (test·tooling 중심)

> 면접관이 Full Stack 엔지니어라면 [추정] system design은 그 사람의 홈그라운드다. "드론을 설계하라"보다 **"테스트 인프라를 설계하라"**가 나올 가능성이 높고, 그러면 대화가 데이터 모델 · API · 저장소 · 대시보드까지 내려온다. 진행 틀과 임베디드 쪽 문제 7개(로깅, OTA, 프로토콜, acquisition, config, factory test, HIL 팜)는 [onsitePrep N06](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.md)에 있다. 이 노트는 그중 **test·tooling 쪽을 웹/백엔드 깊이까지** 확장한다.

## 0. 10분짜리 축약 틀 (1시간 스크린의 마지막 10~15분용)

| 분 | 단계 | 말할 것 |
|---|---|---|
| 0–2 | 요구사항 | 누가 쓰나, 규모(리그 수 · 테스트 수 · 하루 실행 수), 무엇이 "성공"인가 |
| 2–3 | 숫자 | 대략 계산 한 줄 — 저장량, 동시성 |
| 3–6 | 블록도 | 상자 5~7개, 데이터가 흐르는 방향 |
| 6–8 | 핵심 결정 2개 | 각각 trade-off 한 문장 |
| 8–9 | 실패 모드 | 리그가 죽으면 · 결과가 유실되면 · flaky |
| 9–10 | 테스트와 다음 단계 | "v1은 이것, 규모가 커지면 이것" |

- 긴 버전(45분 틀, 요구사항 질문 목록, ASCII 블록도 스타일): [onsitePrep N06 1~2절](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.md)
- **시작 대사**: "Let me make sure I understand who uses this and at what scale, then I'll sketch the components and we can go deep wherever you like."

## 1. 문제 A · HW 테스트 프레임워크 (pytest 기반)

> "Design the test framework our firmware test team will use for the next three years."

### 요구사항 질문

- 테스트 대상: FC(STM32 [확인됨 10-01 Michael]), 라디오, 카메라? 제품 라인이 여러 개인가?
- 실행 장소: 개발자 랩톱(리그 없음) · CI 리그 · 공장 스테이션 — **같은 테스트를 세 곳에서?**
- 작성자: 테스트 엔지니어만 vs FW 개발자도 → API가 얼마나 쉬워야 하나

### 계층 (FTE에서 정리한 것의 확장)

```
 test_*.py            ← 시나리오만: "arm → throttle 20% → motors spin → disarm"
 ────────────────────
 fixtures/conftest    ← rig, dut, power, rc_link, motor_capture  (scope · 정리 · 실패 증거)
 ────────────────────
 device drivers       ← DutCli(serial), CRSF injector, DShot capture, PSU(SCPI), flasher
 ────────────────────
 transports           ← pyserial, socket, USB, debug probe (pyOCD/OpenOCD)
 ────────────────────
 rig config (YAML)    ← 어떤 포트에 무엇이 붙었나 · 보드 리비전 · 펌웨어 경로
```

| 결정 | 선택 | trade-off |
|---|---|---|
| 러너 | pytest | fixture · marker · plugin 생태계 / Robot Framework는 비개발자 친화적이지만 디버깅이 어렵다 |
| 리그 차이 흡수 | YAML config + 드라이버 인터페이스 | 실제/시뮬레이션 리그를 같은 테스트로 (`--rig=sim`) |
| 결과 | JUnit XML + **구조화된 이벤트(JSON)** + 아티팩트(로그, 캡처) | JUnit만으로는 측정값(전류, 지연)이 안 남는다 |
| 측정값 | `record_property` / 커스텀 fixture로 `metric("vbat_sag_mV", 412)` | 시계열 분석의 원천 → 문제 C |

- 코드로 보여 주기: [문제 04 HIL conftest](../python/problems/04_hil_conftest.md) — CLI 옵션, marker skip, session 리그, 실패 시 로그
- 개념 원본: [FTE N04 11절 HIL 프레임워크 아키텍처](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html) · [FTE N02 HIL 리그 블록도](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N02_hil_for_drones.html)

### 영어 2분 요약

> "Tests describe scenarios only. Underneath, fixtures own setup, cleanup and evidence; drivers wrap each instrument; transports are swappable; and a YAML file describes each rig. The same test runs on a laptop with a simulated rig, on CI rigs, and on factory stations with a different config. Every run emits JUnit for CI plus structured events with measurements, so we can trend values over time, not just pass or fail."

## 2. 문제 B · HIL 팜 — 리그 예약 · 스케줄링 · 헬스

> "We'll have 30 HIL rigs across 4 board revisions. Design how CI and engineers share them."

- 임베디드 관점(리그 블록도, 전원, 프로브)은 [onsitePrep N06 9절 HIL 테스트 팜 + CI](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.md). 여기선 **소프트웨어 서비스** 쪽

### 가정 숫자

- 리그 30대 = rev A·B·C·D × 제품 2종, PR당 HIL 스위트 15분, 하루 PR 60개 + nightly 1회 전체(2시간 × 리그당)
- 필요 리그-시간: 60 × 15분 = 15h + nightly → 30대면 낮 시간 여유, **PR 몰리는 오후에 대기열** 발생

### 블록도

```
 GitLab CI job ──► Rig Scheduler API ──► lease(rig_id, ttl) ──► Runner on rig host
        ▲                 │  ▲                                    │ pytest --rig-config rigs/r12.yaml
        │                 ▼  │ heartbeat / health                 ▼
   status/result     Postgres (rigs, leases, queue)          Results Ingest (문제 C)
                          ▲
   Engineer UI ───────────┘  (예약 · 리그 상태 · 수동 회수)
```

### 데이터 모델

```sql
CREATE TABLE rigs (
  id TEXT PRIMARY KEY, product TEXT, board_rev TEXT, host TEXT,
  state TEXT CHECK (state IN ('idle','leased','quarantined','offline')),
  last_health_at TIMESTAMPTZ, capabilities JSONB          -- {"rc_inject": true, "motor_capture": 4}
);
CREATE TABLE leases (
  id BIGSERIAL PRIMARY KEY, rig_id TEXT REFERENCES rigs(id), holder TEXT,  -- CI job id or user
  acquired_at TIMESTAMPTZ, expires_at TIMESTAMPTZ, released_at TIMESTAMPTZ
);
```

### 핵심 결정과 trade-off

| 결정 | 선택 | 왜 |
|---|---|---|
| 배정 방식 | **capability 매칭 lease + TTL** | "rev C, motor_capture" 요구 → 맞는 리그 중 idle. TTL로 죽은 job이 리그를 영원히 잡지 않게 |
| 동시성 | DB 트랜잭션 `SELECT … FOR UPDATE SKIP LOCKED` | 두 job이 같은 리그를 잡는 race 방지. 큐 서비스를 따로 둘 필요가 v1엔 없다 |
| 헬스 | lease 전 **스모크(전원 · 플래시 · CLI 응답)** 30초 | 실패하면 리그를 quarantine 하고 job은 다른 리그로 → 인프라 실패가 제품 실패로 보이지 않게 |
| 공정성 | PR 우선, nightly는 빈 시간에, 사람 예약은 상한 | 사람이 리그를 하루 종일 잡는 문제 |
| GitLab 통합 | 리그 호스트마다 runner + **tag** (`hil-revC`) vs 중앙 스케줄러 | 처음엔 runner tag로 충분. 리그가 늘고 capability가 다양해지면 스케줄러 |

### 실패 모드

- job이 죽어 lease 미반환 → TTL + heartbeat, 만료 시 **리그 전원 사이클 후** idle
- 리그가 조용히 망가짐(케이블, 프로브) → 스모크 실패율을 리그별로 추적, 임계치 넘으면 자동 quarantine + 알림
- 펌웨어가 보드를 brick → 프로브로 복구 플래시 단계가 스모크에 포함

### 영어 2분 요약

> "Rigs are a scarce shared resource, so I treat them like a connection pool with leases. A CI job asks for capabilities — board rev, motor capture — and gets a lease with a TTL. Before handing it out, a 30-second smoke test checks power, flashing and the CLI; a rig that fails is quarantined so infrastructure problems never look like firmware regressions. Version one can be GitLab runner tags; when the fleet and capabilities grow, a small scheduler service on Postgres with row locking handles matching and fairness."

## 3. 문제 C · CI 테스트 결과 파이프라인 + 대시보드 ★ (Full Stack 면접관이 가장 좋아할 문제 [추정])

> "Every CI run and every rig produces test results. Design the system that stores them and answers: what broke, when, and is it flaky?"

### 요구사항 질문

- 누가 보나: FW 개발자(내 PR이 깼나?), 테스트 리드(어느 테스트가 flaky?), 매니저(릴리즈 가능?), 공장(수율)
- 무엇을 답해야 하나: 실패 목록, **처음 깨진 커밋**, 테스트별 pass율 추이, flaky 순위, 리그별 실패율, 측정값 추이
- 보존: 결과 1년, 로그·아티팩트 90일?

### 가정 숫자

- 하루 실행 300회(PR · nightly · 리그) × 테스트 2,000개 = **60만 행/일**, 행당 ~200B → 120MB/일, 1년 ~45GB → Postgres 한 대로 충분 (파티셔닝)
- 측정값(metric): 실행당 100개 → 3만/일 → 시계열 DB 또는 같은 Postgres
- 아티팩트(로그, 캡처): 실행당 50MB → 15GB/일 → **object storage**(S3/MinIO), DB에는 링크만

### 블록도

```
 pytest (CI · rig · factory) ──JUnit XML + events.jsonl──► Ingest API ──► Postgres
            │                                                │  (runs, results, metrics)
            └── artifacts (logs, captures) ──► S3/MinIO ◄────┘ (URL only)
                                                             │
                                  nightly job: flaky score · first-bad-commit
                                                             ▼
                      Dashboard (web)  ·  Grafana for metrics  ·  GitLab MR comment bot
```

### 데이터 모델

```sql
CREATE TABLE runs (
  id BIGSERIAL PRIMARY KEY, pipeline_id TEXT, git_sha TEXT, branch TEXT,
  kind TEXT,                     -- pr | nightly | factory
  rig_id TEXT, board_rev TEXT, fw_version TEXT,
  started_at TIMESTAMPTZ, finished_at TIMESTAMPTZ
);
CREATE TABLE results (
  run_id BIGINT REFERENCES runs(id), test_id TEXT,   -- "tests/hil/test_arm.py::test_arm[revC]"
  outcome TEXT,                  -- passed | failed | skipped | error(infra)
  duration_ms INT, attempt SMALLINT DEFAULT 1, message TEXT, artifact_url TEXT,
  PRIMARY KEY (run_id, test_id, attempt)
);
CREATE INDEX ON results (test_id, run_id);
CREATE TABLE metrics (run_id BIGINT, test_id TEXT, name TEXT, value DOUBLE PRECISION, unit TEXT);
```

### 핵심 결정과 trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| 수집 포맷 | JUnit XML(호환) + `events.jsonl`(측정값 · 리그 정보) | JUnit은 모든 CI가 이해하지만 측정값 칸이 없다 |
| 업로드 | CI job 마지막 단계에서 **멱등 업로드** (`pipeline_id + rig_id`로 upsert) | 재시도해도 중복 행이 안 생김 |
| `failed` vs `error` 구분 | 리그·드라이버 예외 = `error(infra)` | 제품 실패율과 인프라 실패율을 분리해야 신뢰가 생긴다 |
| flaky 판정 | 같은 `git_sha`에서 pass와 fail이 둘 다 → flaky 후보. 테스트별 최근 N회 **flip rate** | 단순 실패율은 "진짜 깨짐"과 "가끔 깨짐"을 구분 못 한다 |
| 처음 깨진 커밋 | nightly가 결과 시계열에서 마지막 pass → 첫 fail 구간 탐색, 구간이 크면 **자동 git bisect job** | → [FTE N05 git bisect](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N05_ci_cd_git.html) |
| 측정값 저장 | v1은 Postgres `metrics` + Grafana, 커지면 InfluxDB/Timescale | 공고에 InfluxDB · Grafana가 있다 [확인됨 Full-Stack III 공고] |
| 대시보드 | 기성(Allure, ReportPortal, Grafana) vs 자체 | 처음엔 Grafana + 간단한 페이지, MR 코멘트 봇이 **가장 많이 보이는 UI** |

### flaky 점수 — 코드 한 조각 (면접에서 화이트보드로)

```python
def flip_rate(outcomes):
    """최근 N회 결과(['pass','fail',...])에서 결과가 바뀐 비율. 0이면 안정, 1에 가까우면 들쭉날쭉."""
    flips = sum(a != b for a, b in zip(outcomes, outcomes[1:]))
    return flips / max(1, len(outcomes) - 1)
```

- 항상 실패 → flip 0, 실패율 1 → **진짜 깨짐** / 번갈아 → flip 높음 → **flaky** / 가끔 한 번 → 낮은 실패율 + 낮은 flip → 감시
- FTE에서 풀었던 결과 분석 문제와 연결: [FTE 문제 07 CI 결과 분석기](../../firmwareTestEngineerPrep/site/problems/07_ci_results_analyzer.html)

### API (최소)

| Method | Path | 용도 |
|---|---|---|
| POST | `/runs` | run 메타 등록 → `run_id` |
| POST | `/runs/{id}/results` | JUnit/JSONL 업로드 (멱등) |
| GET | `/tests/{test_id}/history?limit=50` | 추이 · 아티팩트 링크 |
| GET | `/flaky?window=14d` | flaky 순위 |
| GET | `/runs/{id}/diff?base=main` | 이 PR이 새로 깬 테스트만 |

### 실패 모드

- 업로드 실패 → CI job이 결과 파일을 아티팩트로도 보관, 재업로드 가능 (멱등)
- 테스트 이름 변경 → 이력이 끊김 → 안정적인 `test_id` 규칙 + rename 매핑 테이블
- 대시보드는 아무도 안 본다 → **MR 코멘트**("이 PR이 새로 깬 테스트 2개, flaky 1개는 무시해도 됨")가 진짜 UI

### 영어 2분 요약

> "Every run uploads JUnit plus a JSON lines file with measurements and rig metadata to an ingest API — idempotently, keyed by pipeline and rig. Results go to Postgres, partitioned by time; logs and captures go to object storage with only URLs in the database. I separate product failures from infrastructure errors at the source. A nightly job computes a flip-rate flakiness score and the first bad commit, kicking off a bisect if the range is large. Grafana covers the measurement trends, but the most valuable UI is a merge-request comment that says exactly which tests this change newly broke."

## 4. 문제 D · Factory test 결과 → 추적성 (짧게)

- 임베디드 쪽 설계(스테이션, provisioning, 보정, 키 주입)는 [onsitePrep N06 8절](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.md) + [onsitePrep N08 factory test FW](../../onsitePrep/site/notes/2026-10-01_N08_factory_test_firmware.md)
- Full Stack 쪽 연결: 시리얼 번호 단위로 **station · fixture · fw_version · 측정값 · 판정**을 문제 C의 같은 스키마(`kind='factory'`)로 → MES로 이벤트 발행 (Neros가 Manufacturing Systems 엔지니어를 뽑는 중 [확인됨 공고 5156414007]: serialization · genealogy · traceability)
- 핵심 한 문장: **"A field failure should trace back to the exact station, fixture, firmware and measurements for that serial number."**
- 공장은 **offline이 정상 상태** → 스테이션이 로컬에 버퍼링 후 동기화 (Lead, Software Application 공고의 "offline is a normal operating state" [확인됨 공고])

## 5. Don 경험 → 설계 근거 문장 (레쥬메 범위)

| 설계 포인트 | Don 경험 | 말할 문장 |
|---|---|---|
| 프레임워크를 다른 팀이 쓴다 | SK hynix 챔버 테스트 **web 플랫폼 + SDK** | "I've built a test platform other engineers used daily, so I design the API for the test author, not for myself." |
| 장시간 · 스케줄링 · 상태 | 챔버 플랫폼의 온도 · 스케줄링 · eSSD 상태 | "Long-running hardware tests need scheduling, state tracking and recovery — that was the core of the chamber platform." |
| 인프라 실패 vs 제품 실패 | Apple factory test-node 아키텍처, stress 기반 latent defect | (Don: test-node에서 인프라 오류를 어떻게 구분했는지 — 레쥬메에 없음) |
| 측정값 · 텔레메트리 | NVMe telemetry 디버그 기능 (MS/Dell/HPE) | "Telemetry only helps if it's structured and queryable — same principle for test measurements." |

## 체크

- [ ] 0절 10분 틀로 문제 C를 소리 내어 한 번 (타이머)
- [ ] 문제 C의 `runs`/`results` 스키마를 보지 않고 종이에
- [ ] 문제 B의 "lease + TTL + smoke + quarantine"을 30초로
- [ ] flaky 정의(flip rate)와 "infra error 분리"를 한 문장씩 영어로
- [ ] onsitePrep N06 9절 HIL 팜을 한 번 읽고 이 노트 2절과 합치기

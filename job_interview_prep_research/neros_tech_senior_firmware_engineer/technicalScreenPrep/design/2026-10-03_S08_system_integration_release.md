# 🛩️ S08 · System integration · Release — 무엇을 만들고, 어떻게 테스트하나

> 직군별 테스트(S01~S07)가 각자 초록불이어도 **기체 한 대와 지상 장비 한 세트를 합치면** 깨질 수 있다: FC 펌웨어 × 라디오 펌웨어 × companion 이미지 × GCS 버전 × board rev 조합, 공중↔지상 end-to-end 지연, 릴리즈 순서. 이 노트는 그 전체를 묶는 시스템 레벨 테스트 — **iron bird(전 기체 HIL 벤치)**, 호환성 매트릭스, 릴리즈 게이트(PR → nightly → RC → flight test → field), 비행 로그 자동 분석, 필드 데이터 → 회귀 테스트 루프 — 를 설계한다. 그리고 FW Test 팀이 2명에서 5명으로 클 때 무엇부터 세울지도 다룬다.

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | 여러 팀의 산출물을 **한 릴리즈 단위(bundle)**로 묶어 검증하는 시스템: iron bird 벤치, 호환성 매트릭스, 릴리즈 게이트, 비행시험 로그 파이프라인, 필드 피드백 루프 |
| 주로 고장 나는 곳 | 팀 사이 **인터페이스**(메시지 버전, 단위, 타이밍), 버전 조합, 업데이트 순서, 실제 RF·진동·온도에서만 나는 문제, 릴리즈 프로세스 자체 |
| 대표 테스트베드 | iron bird (실제 FC + 라디오 + companion + GCS + 영상, 모터는 부하/더미), air↔ground RF 벤치(attenuator), SITL 팜, flight test + 로그 자동 분석 |
| 합격 기준 예 | RC → 모터 지연 p99 < 30 ms [추정], 영상 glass-to-glass p95 < 50 ms [추정], 2시간 soak 리셋 0, 호환성 매트릭스 지원 조합 100% green, 릴리즈 블로커 0 |
| Don 연결 | Apple: 새 실리콘이 전체 HW/SW 시스템을 만날 때 깨지는 크로스바운더리 이슈 루트코즈, bring-up→NPI→MP / Solidigm: Google·Meta NVMe 2.0 기능, production FW 릴리즈 |

## 1. 이 직군 이해하기 — Neros 공고 근거

- **FW Test 팀의 범위 자체가 시스템이다**: "validate the Neros **drone & ground control software**", "Integrate automated tests into CI/CD", "**Ensure Build Stability**: Monitor the test results and ensure the stability of builds **before releases**" [확인됨 공고 4941340007]
- **Flight SW ↔ Test 경계**: "partnering with the Test organization on **SITL/HITL and flight-test execution**", "Drive predictable, safe software **releases**", "Define and maintain the software **interfaces** consumed by autonomy and adjacent teams" [확인됨 공고 5164187007] · "Write the unit and integration tests for your firmware, partnering with our Test organization on SITL/HITL" , "code to bench to flight test" [확인됨 공고 5195301007]
- **Ground ↔ Test 경계**: Ground Software Manager — "Own the team's test strategy and unit/integration testing, **partnering with the Test organization on shared infrastructure**" [확인됨 공고 5173033007]
- **Autonomy 쪽 평가 체계(참고 모델)**: Manager, Autonomy Evaluation, Data & Test — "multi-tier regression system spanning unit and module tests, **open-loop replay against logged flights, closed-loop simulation, and hardware-in-the-loop testing on production flight computers**, including **latency and deadline-miss measurement**", "Own the **release criteria** … the conditions under which a build is blocked", "on-vehicle capture, field egress, ingest … **deterministic replay**" [확인됨 공고 5230189007]
- **팀 규모**: FW 15명, FW Test 2명, 테스트 FW가 가장 급함 → **연내 최대 5명** [확인됨 10-01 Michael]
- 해석 [추정]: 유닛 테스트는 각 FW 팀, 직군별 HIL은 Test와 공동, **여러 팀 산출물을 묶는 integration과 릴리즈 게이트는 Test 조직의 고유 영역**이 될 가능성이 크다

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
   운용자 손                                    기체 (air side)
 ┌───────────────┐   C2 / RC (ELRS·LoRa 등)   ┌──────────────────────────────────────┐
 │ GCS / 핸드셋   │ ─────────────────────────► │ Radio RX ─CRSF─► FC (STM32)           │
 │ (ground SW)   │                            │                   │ 제어 루프 → ESC → 모터│
 │               │ ◄──────── telemetry ────── │                   │ MAVLink/telemetry    │
 │ 영상 표시      │ ◄──────── video ────────── │ Camera ─► VTX / video encoder          │
 └───────────────┘                            │ Companion (Linux, autonomy) ◄─UART/ETH─► FC │
         ▲                                    └──────────────────────────────────────┘
         │                                                     │ 비행 로그 (FC blackbox + companion log)
         └──── 릴리즈 bundle: {FC fw, radio fw (air/ground), companion image, GCS version, board rev}
                                                               ▼
                                 log ingest → 자동 분석 → 회귀 테스트 케이스 → CI
```

- 각 화살표 하나가 **다른 팀의 인터페이스**다. 시스템 테스트는 화살표를 검증하고, 직군별 테스트(S01~S06)는 상자를 검증한다
- 직군별 상세: [S01 flight](2026-10-03_S01_flight_software.md) · [S02 radio](2026-10-03_S02_radio_link_connectivity.md) · [S03 ground & peripherals](2026-10-03_S03_ground_and_peripherals.md) · [S04 video/FPGA](2026-10-03_S04_video_fpga_pipeline.md) · [S05 platform](2026-10-03_S05_platform_firmware.md) · [S06 embedded Linux & autonomy](2026-10-03_S06_embedded_linux_autonomy.md) · [S07 factory](2026-10-03_S07_manufacturing_factory_test.md) · 개요 [S00](2026-10-03_S00_overview.md)

## 3. 무엇이 고장 나나

| failure mode | 증상 | 어느 테스트 레벨에서 잡나 |
|---|---|---|
| 메시지 버전 불일치 (FC ↔ companion, FC ↔ GCS) | 필드 누락, 잘못된 값, 연결 거부 | contract test (CI), 호환성 매트릭스 (nightly) |
| 단위·좌표계 불일치 (deg vs rad, NED vs ENU) | autonomy 명령이 반대 방향 | SITL closed-loop, iron bird |
| end-to-end 지연 증가 | 조종감 저하, 영상 지연 | iron bird 지연 측정 (nightly, 추세) |
| 업데이트 순서 의존 | 라디오만 업데이트 → 바인딩 실패 | 업그레이드 경로 테스트 (release) |
| 장시간 누적 문제 (메모리 누수, 카운터 overflow, 로그 파티션 가득) | 30분 뒤 리셋, 로그 끊김 | soak test 2~8시간 (nightly/release) |
| 실제 RF·진동·온도 | 진동 시 IMU aliasing, 모터 노이즈로 desense | flight test, 환경 시험 (release) |
| 전원 이벤트 (배터리 sag, 핫스왑) | 브라운아웃 리셋, 설정 손상 | iron bird 전원 주입 (nightly) |
| failsafe 연쇄 (링크 손실 → RTH → GPS 없음) | 예기치 않은 모드 전환 | SITL 시나리오 + iron bird 링크 차단 |
| 릴리즈 bundle 구성 오류 | 잘못된 조합 출하 | bundle manifest 검증 (release gate) |
| 필드에서만 나는 문제 | 크래시 리포트, 반품 | 필드 텔레메트리 → 재현 → 회귀 케이스 |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | 각 FW 개발자 | 함수·모듈 로직 | 호스트 | PR |
| component | 각 팀 + Test | 펌웨어 하나 + 주변장치 (FC만, 라디오만) | 직군별 HIL 리그 (S01~S06) | PR / nightly |
| integration (contract) | Test | 팀 간 메시지 스키마·버전 호환, 녹취된 트래픽 재생 | 호스트 + SITL | PR (스키마 변경 시 필수) |
| HIL (iron bird) | **Test** | 전체 air+ground 체인, 지연, failsafe, 전원 이벤트, soak | iron bird 벤치 2~3대 | nightly · RC |
| system (flight test) | Test + Flight SW + 테스트 파일럿 | 실제 비행 시나리오, 진동·RF·환경 | 시험장 | release candidate |
| field | Test + Quality | 필드 텔레메트리, 크래시 리포트, 반품 상관 | 배치된 기체 | 지속 → 회귀 케이스로 환류 |

- **원칙**: 비싼 레벨일수록 적게, 대신 **거기서 찾은 버그는 반드시 더 싼 레벨의 회귀 테스트로 내려보낸다** (flight test 버그 → SITL 시나리오 또는 iron bird 케이스)

## 5. 테스트베드 설계

### 5.1 iron bird — 전 기체 HIL 벤치

```text
            ┌──────────────── iron bird bench ────────────────┐
 RC 주입     │ ground radio ──[attenuator 0–90 dB]── air radio  │    영상 캡처
 (CRSF/SDR) ─►  (+ GCS PC/핸드셋)                    │         │◄── HDMI/USB 캡처 + 광센서
            │                                      ▼         │    (glass-to-glass)
 전원 주입   │ programmable PSU ──► PDB ──► FC (STM32) ──DShot──► ESC + 모터(부하) 또는 DShot 캡처
 (sag, 노이즈)│                                 │   ▲          │
            │            IMU 주입(선택) / 진동대 ┘   │UART/ETH   │
            │                         Companion (Linux) ──────┘ │
            │  logic analyzer (CRSF·DShot·UART 타임스탬프)        │
            └──────────── runner host (pytest, GitLab runner) ─┘
                               │ artifacts: 로그, 캡처, 지연 히스토그램
                               ▼
                         results DB (N04 문제 C)
```

- 핵심 측정: **RC 입력 edge → DShot 출력 edge** 지연(로직 애널라이저 두 채널), **카메라 앞 LED → 화면 광센서** glass-to-glass 지연
- attenuator로 **링크 마진을 조절**해 failsafe·재연결을 재현 가능하게 만든다 (실제 비행에선 재현 불가)
- 모터: 프로펠러 없이 부하 모터 또는 DShot 캡처만 (안전, 24시간 무인 운영)

### 5.2 장비 표 (iron bird 1대) [추정]

| 장비 | 용도 | 대략 비용 |
|---|---|---|
| 실제 기체 전자부 일체 (FC, ESC, 라디오 air/ground, companion, 카메라, VTX) | DUT | 기체 BOM 수준 |
| 프로그래머블 attenuator + shield box/RF 케이블 | 링크 마진 제어 | $3–10K |
| 프로그래머블 PSU (시퀀스 출력) | 배터리 sag·노이즈 주입 | $1–3K |
| 로직 애널라이저 (8~16ch) | 지연·프로토콜 타임스탬프 | $0.5–2K |
| 영상 캡처 + 광센서/LED 지그 | glass-to-glass 지연 | $0.5K |
| SWD 프로브, USB 허브, 릴레이 보드 | 플래시·복구·전원 사이클 | $0.5K |
| runner PC | pytest, GitLab runner | $1–2K |
| (선택) 진동대·온도 챔버 | 환경 조건 | 공용 장비 |

### 5.3 자동화 구조 [추정]

- **bundle manifest** (`bundle.yaml`): `{fc: <sha>, radio_air: <ver>, radio_ground: <ver>, companion: <image>, gcs: <ver>, board_rev: C}` — 모든 테스트 결과에 manifest 해시를 붙인다
- fixture: `iron_bird` (session: 리그 예약·전원), `bundle` (session: manifest 대로 전 장비 플래시·버전 read-back), `link(attenuation_db)`, `rc` (CRSF 주입), `motors` (DShot 캡처), `video_latency`, `power_profile`
- 시나리오는 데이터로: `scenarios/link_loss_rth.yaml` (단계·기대 상태 전환·시간 한도)
- 리그 공유·예약은 HIL 팜과 같은 방식 → [N04 문제 B](../notes/2026-10-02_N04_system_design_test_tooling.md), 결과 저장은 [N04 문제 C](../notes/2026-10-02_N04_system_design_test_tooling.md)

### 5.4 호환성 매트릭스 — 조합 폭발 다루기

- 차원 예 [추정]: FC fw 3 (현재, 직전, RC) × radio fw 2 × companion 2 × GCS 2 × board rev 3 = **72 조합**
- 전부 nightly로 돌리지 않는다: ① **지원 정책**으로 줄이기 ("N과 N-1만 지원") ② **pairwise**(모든 두 차원 쌍을 최소 한 번)로 72 → ~12 조합 ③ 필드 텔레메트리로 **실제 많이 쓰이는 조합**에 가중치
- 인터페이스 계약은 매트릭스보다 싸게: 메시지 스키마에 버전 필드, 녹취된 트래픽을 새 버전 파서에 재생하는 **contract test**를 PR에서

### 5.5 릴리즈 게이트

| 게이트 | 무엇을 돌리나 | 시간 | 블록 조건 |
|---|---|---|---|
| PR | unit, 빌드 전 타깃, 정적 분석, contract test, 직군별 HIL 스모크 | < 20분 | 하나라도 실패 |
| nightly | 직군별 HIL 전체, iron bird 시나리오, pairwise 매트릭스, SITL 회귀, 지연 추세 | 2~4시간 | 신규 실패 → 다음 날 아침 triage |
| RC (release candidate) | iron bird soak 8시간, 전체 매트릭스(지원 조합), 업그레이드·다운그레이드 경로 | 1~2일 | 블로커 0, 지연 회귀 없음 |
| flight test | 체크리스트 비행 (호버, 기동, failsafe, 링크 한계), 로그 자동 분석 | 1~3일 | 분석 지표 기준 통과 + 파일럿 sign-off |
| field (단계 배포) | 소수 기체 → 확대, 텔레메트리 감시 | 1~2주 | 크래시율·리셋율 기준 초과 시 중단·롤백 |

### 5.6 비행 로그 자동 분석 · 필드 루프

- 비행 직후 FC blackbox + companion 로그 + GCS 로그 업로드 → **시간 정렬**(공통 타임스탬프 또는 이벤트 정렬) → 자동 지표: 리셋 수, 루프 deadline miss, 링크 RSSI·LQ 분포, failsafe 이벤트, 진동 스펙트럼, 배터리 sag
- 기준 초과 지표는 **티켓 + 원본 로그 링크**로 자동 생성
- 필드: 크래시 리포트(리셋 원인 레지스터, 마지막 로그 링 버퍼) → 같은 파이프라인 → 재현되면 SITL/iron bird 회귀 케이스로 등록 → 다음 릴리즈 게이트에 포함
- autonomy 쪽은 이미 이 구조(open-loop replay, deterministic replay)를 공고에 명시 [확인됨 공고 5230189007] → FW 쪽도 같은 데이터 플랫폼을 공유하자고 제안할 수 있다 [추정]

### 5.7 팀 2명 → 5명: 무엇부터 세우나 [추정]

| 단계 | 인원 | 먼저 세울 것 | 이유 |
|---|---|---|---|
| 0–3개월 | 2→3 | FC HIL 리그 2대 + PR 스모크, 결과 DB 최소판, factory test FW v1 (S07) | 가장 급한 것(테스트 FW, 공장) + 매일 쓰이는 피드백 |
| 3–6개월 | 3→4 | iron bird 1대 + nightly 시나리오, bundle manifest, 지연 측정 | 팀 간 인터페이스 회귀를 처음 잡기 시작 |
| 6–12개월 | 4→5 | 호환성 pairwise, RC soak, 비행 로그 자동 분석, 필드 크래시 루프 | 릴리즈를 데이터로 결정 |
| 역할 분담 | — | 프레임워크·인프라 1, 직군별 리그(flight/radio/ground) 2, 공장 1, 릴리즈·데이터 1 | 리그 소유자가 분명해야 flaky가 관리된다 |

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 stimulus | 관측 | 합격 기준 (예) [추정] |
|---|---|---|---|---|
| I01 | bundle 플래시·버전 확인 | manifest 대로 전 장비 업데이트 | 각 장비 버전 read-back | manifest와 100% 일치 |
| I02 | RC → 모터 지연 | CRSF 채널 step, 1,000회 | RC edge → DShot edge | p50 < 10 ms, p99 < 30 ms |
| I03 | 영상 glass-to-glass | LED 점멸 → 카메라 → 화면 광센서 | 지연 분포 | p95 < 50 ms, 프레임 drop < 0.1% |
| I04 | 링크 손실 failsafe | attenuator로 링크 끊기 → 복구 | 모드 전환 시각, 재연결 시간 | 설정된 시간 내 failsafe, 재연결 < 2초 |
| I05 | 링크 마진 스윕 | attenuation 0→90 dB 1 dB 단위 | LQ, telemetry 손실, 제어 지연 | 마진 기준 dB 이상에서 LQ ≥ 기준 |
| I06 | 배터리 sag | PSU 16.8V → 13V 순간 강하 | 리셋 여부, 경고 텔레메트리 | 리셋 0, 저전압 경고 1초 내 |
| I07 | FC ↔ companion 계약 | 녹취된 메시지 재생 (N-1 ↔ N) | 파싱 오류, 필드 의미 | 오류 0, 알려진 필드 값 일치 |
| I08 | companion 크래시 | companion 프로세스 kill/재부팅 | FC 동작, 모드 | FC 비행 유지(안전 모드), companion 복귀 후 재연결 |
| I09 | 업그레이드 경로 | N-1 bundle → N, 순서 바꿔서 | 바인딩, 설정 유지 | 지원 순서 모두 성공, 설정 손실 0 |
| I10 | soak | 8시간 시나리오 루프 | 리셋, 메모리, 로그 연속성, 지연 추세 | 리셋 0, 메모리 증가 < 1%/h, 지연 p99 추세 평탄 |
| I11 | 필드 크래시 재현 | 필드 로그의 입력 시퀀스 재생 (SITL → iron bird) | 동일 실패 재현 여부 | 수정 후 회귀 케이스 통과 |
| I12 | 비행 로그 분석 기준 | flight test 로그 자동 분석 | deadline miss, RSSI, 진동 | miss 0, 진동 피크 < 기준 g |

## 7. 면접 시나리오 — "Design how we test and release a full drone + ground system"

### 요구사항 질문 (영어)

- "What's in a release — do all components ship together as a bundle, or independently?"
- "Which version combinations must we support in the field — only the latest pair, or N and N-1?"
- "How often do you want to release, and what does a failed release cost — a recall, a grounded fleet?"
- "What's the flight test capacity per week, and who signs off?"
- "Do fielded drones send telemetry or crash data back, or only what we recover physically?"

### 가정 숫자 [추정]

- 컴포넌트 5종(FC, radio air/ground, companion, GCS), board rev 3, 지원 정책 N/N-1 → 매트릭스 72 → pairwise 12
- 릴리즈 2주 주기, iron bird 3대, nightly 4시간, flight test 주 2일
- 팀: 현재 FW Test 2명 → 5명 [확인됨 10-01 Michael: 최대 5명 채용]

### 블록도

- 2절 데이터 흐름 + 5.1 iron bird + 5.5 릴리즈 게이트 표를 순서대로 그린다

### 핵심 결정 & trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| 릴리즈 단위 | **bundle manifest** (조합을 한 버전으로) | 테스트할 조합이 줄고 추적이 쉬움 / 팀별 독립 배포 속도 감소 |
| 호환성 | N/N-1 정책 + pairwise + contract test | 비용을 다항식으로 / 드문 3-way 상호작용은 놓칠 수 있음 → 필드 텔레메트리로 보완 |
| iron bird 구성 | 실제 전자부 + 부하 모터, attenuator로 RF | 재현성·24시간 운영 / 실제 공력·진동은 없음 → flight test가 필요 |
| 게이트 엄격도 | PR은 빠르게(<20분), nightly는 넓게, RC는 길게 | 개발 속도와 커버리지 균형 / nightly 실패 triage 규율 필요 |
| flaky 처리 | quarantine + 소유자 + 기한, infra 실패 분리 | 신뢰 유지 / 관리 비용 |
| 비행 로그 | 자동 업로드·분석, 지표로 sign-off 보조 | 파일럿 감에만 의존하지 않음 / 데이터 파이프라인 구축 비용 |

### 실패 모드

- iron bird가 하나뿐이고 고장 → 최소 2대, 리그 헬스 체크(S07 golden unit 개념) [추정]
- nightly 실패를 아무도 안 봄 → 매일 아침 triage 로테이션, 새 실패만 알림
- flight test에서 찾은 버그가 회귀 테스트로 안 내려옴 → 버그 종료 조건에 "회귀 케이스 링크" 필수
- 릴리즈 압박으로 게이트 우회 → 예외는 기록·승인(누가, 왜) — autonomy 공고의 "process for reviewing exceptions"와 같은 개념 [확인됨 공고 5230189007]

### 영어 2분 요약

> "I'd treat a release as a bundle — one manifest pinning the flight controller firmware, both radio firmwares, the companion image, the ground software version and the supported board revisions — and attach that manifest to every test result. Component teams keep their own unit and component HIL tests; the test team owns what crosses team boundaries. Interfaces get cheap contract tests on every PR by replaying recorded traffic across versions. Every night, iron bird benches — real electronics end to end, with programmable attenuators for the link and a programmable supply for battery events — run failsafe scenarios, measure stick-to-motor and glass-to-glass latency, and cover the version matrix pairwise instead of exhaustively. A release candidate adds an eight-hour soak and the upgrade paths, then flight test, where logs are uploaded and analyzed automatically. Fielded units feed crash data back into the same pipeline, and every bug found in flight or in the field must come back as a regression test at the cheapest level that can reproduce it."

## 8. 꼬리 질문

**Q. How do you choose what to test on the iron bird versus in flight?**

> "Anything that depends on timing, versions, failsafes or power events goes on the bench, because I can repeat it a thousand times. Flight test is for what the bench can't model — aerodynamics, real vibration and real RF environments — and its findings get pushed back down to the bench or simulation."

**Q. The version matrix is too big. What do you do?**

> "Shrink it by policy first — support N and N-1 only. Then pairwise coverage instead of exhaustive, weighted by what's actually deployed. And make interfaces versioned and contract-tested, so most incompatibilities fail on the PR instead of on a bench."

**Q. A release candidate passes everything but fails in flight test. Now what?**

> "Pull the logs, find the first divergence from a known-good flight, and decide whether the bench could have caught it. If yes, the missing test goes in before the fix. If no, I document why — it might mean adding vibration or RF realism to the bench."

**Q. How do you measure end-to-end latency reliably?**

> "With external instruments, not the software's own timestamps: a logic analyzer on the RC input and the motor output, and a light sensor on the screen for video. Then I track the distribution — p50 and p99 — per release, because the tail is what the pilot feels."

**Q. With two people, where do you start?**

> "With what blocks the most people every day: a reliable PR smoke on real flight-controller hardware and a results database everyone can see — plus factory test firmware, since production can't wait. The iron bird and the full matrix come once those feedback loops are trusted."

## 9. Don 경험 연결 (레쥬메 범위)

| 이 노트의 포인트 | Don 경험 | 말할 문장 |
|---|---|---|
| 팀 경계의 버그 | Apple: 새 실리콘이 전체 HW/SW 시스템을 만날 때 PCIe/I2C/SPMI/RFFE 이슈 **루트코즈** | "My job at Apple is exactly the bugs that live between teams — so I design integration tests around interfaces, not components." (Don: 대표 사례 한 개) |
| 단계적 릴리즈 | **bring-up → NPI → MP** | "I've moved products through bring-up, NPI and mass production, so I think of release gates as evidence that grows at each stage." |
| 고객 요구 기능 검증 | Solidigm: **Google/Meta NVMe 2.0 기능**, production FW | "Shipping firmware features to hyperscale customers meant every release had to be proven against their requirements before it left." (Don: 당시 릴리즈 검증 방식) |
| 필드 데이터 루프 | SK hynix: **NVMe telemetry 디버그 기능** (MS/Dell/HPE) | "I built telemetry so field failures could be debugged from data — the same loop I'd build from fielded drones back to regression tests." |
| 장시간 · 신뢰성 | SK hynix **chip reliability system**, 챔버 테스트 플랫폼 | "Long soak and reliability testing at scale is where I started — the chamber platform scheduled and monitored those runs." |

## 체크

- [ ] 5.5 릴리즈 게이트 표를 보지 않고 5단계와 블록 조건 말하기
- [ ] iron bird 블록도를 종이에 3분 안에, 지연 측정 두 개(RC→모터, glass-to-glass) 포함
- [ ] "N/N-1 정책 → pairwise → contract test" 를 30초 영어로
- [ ] 7절 영어 2분 요약 소리 내어 두 번
- [ ] 5.7 "2명 → 5명" 표를 Michael에게 할 역질문 하나로 바꿔 보기

# 🗺️ R10 Don의 학습 로드맵 · 실습 랩 · 참고 자료

> SSD FW 검증에서 드론 FTE로 건너가는 8주 계획. 0원 랩(소스 읽기 · 유닛 테스트 · SITL · 파서 · 로그 분석 · CI)부터 시작하고, 하드웨어 랩(미니 HIL · latency 측정)은 예산을 확인한 다음에 한다. 끝에 하드웨어 구매 단계, 포트폴리오 저장소 구조, 참고 자료, 용어집 60개 이상을 붙였다.

---

## 0. 이 단원을 읽는 법

- R00~R09는 "무엇을 아는가"를 다룬다. R10은 "**무엇을 하고, 무엇을 만들어 남기는가**"를 다룬다.
- 랩 명령어는 2026-10-24에 공식 문서와 GitHub 소스로 확인했다. 하지만 **Don의 Mac이나 Lima VM에서 직접 돌려 보지는 않았다**. 그래서 명령어 블록에는 모두 `검증 안 함 · 가이드`를 붙였다.
- Neros 내부 일은 공고 문구를 빼면 전부 `[추정]`이다. Gemini가 말한 제품 이름은 `[Gemini 주장 · 미확인]`으로 쓴다.
- 하드웨어 랩(L10 · L11)은 **사기 전에 Don과 예산을 확인한다**. 가격은 모두 `[확인 필요]` 추정치다.
- Yocto 같은 무거운 빌드는 Don의 규칙대로 가이드만 쓴다. 이 단원의 빌드(Betaflight SITL, ArduPilot SITL, PX4 SIH)는 수 분~수십 분 정도라서 실제로 해 볼 만하다 [추정].

### 단원 연결

| 단원 | 이 로드맵에서 쓰이는 곳 |
|---|---|
| [R00](2026-10-24_R00_overview_and_gemini_corrections.md) | 1주차 첫날 — 전체 지도 · Gemini 정정 목록 |
| [R01](2026-10-24_R01_fte_vs_ssd_validation.md) | 1주차 — SSD 검증과 드론 FTE 비교 (스킬 갭 표의 근거) |
| [R02](2026-10-24_R02_drone_system_and_open_source_map.md) | 1~2주차 — 보드 · 버스 · 신호 경로 |
| [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) | 3주차 — L1 · L2 · L3 · L7 |
| [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) | 5주차 — L4 · L5 |
| [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) | 4주차 — L6 · L11 |
| [R06](2026-10-24_R06_companion_video_linux_rtos.md) | 6주차 — L9 (Renode), companion 쪽 테스트 |
| [R07](2026-10-24_R07_test_methodology_pyramid.md) | 4~6주차 — L8, 테스트 기법 카탈로그 |
| [R08](2026-10-24_R08_hil_bench_design_instruments.md) | 7주차 — L10 · L11 |
| [R09](2026-10-24_R09_scalable_reusable_test_platform.md) | 7~8주차 — L12 · L13, 포트폴리오 |

---

## 1. 요약

- Don은 **테스트 플랫폼을 만들어 본 사람**이다. 챔버 테스트 플랫폼 SDK(온도 제어 · 스케줄링 · eSSD 상태 모니터링 · UART 테스트 시퀀스)를 다른 엔지니어가 쓰게 만들었다. 이 경험은 드론 HIL 플랫폼과 모양이 거의 같다.
- 빠진 것은 세 덩어리다. ① **도메인**(비행 제어 · RC 링크 · 모터 · 비행 물리), ② **도구 문화**(pytest · GitLab CI/Jenkins · Bazel), ③ **드론 오픈소스 실물 경험**(Betaflight · ELRS · PX4 · ArduPilot).
- 그래서 계획은 "**0원으로 시뮬레이터에서 먼저 테스트를 만들고, 그 테스트를 나중에 실물 보드로 옮긴다**"이다. 이건 R07의 피라미드(unit → SITL → HITL → HIL)를 Don 자신의 학습에 그대로 쓰는 것이다.
- 8주 끝의 산출물: 공개 GitHub 저장소 하나(`drone-fw-test-harness` 가칭), 랩 노트 13개, HIL 설계 문서 1개.

## 2. 왜 중요한가 (FTE 관점)

- FTE JD의 필수 항목은 "HIL 테스트 시스템 구축, Python 테스트 자동화, I2C · SPI · UART · Ethernet, GitLab CI · Jenkins, Git"이다 [확인됨 공고, K05 인용]. 우대는 "C/C++, Bazel · CMake · make, RF 제품 테스트, FPV Drone software including Betaflight, ExpressLRS, PX4, Ardupilot"이다.
- 공고의 단어를 하나씩 "직접 해 본 증거"로 바꾸는 것이 이 로드맵의 목적이다. 읽기만 하면 남는 게 없다. 랩마다 **남는 파일(artifact)**을 정했다.
- 드론 회사의 테스트 엔지니어는 "테스트 케이스를 많이 쓰는 사람"보다 "**다른 사람이 테스트를 쉽게 쓰게 만드는 사람**"을 더 원한다 [추정]. Don의 SDK 경험이 바로 이 축이다. 포트폴리오도 이 방향으로 짠다.

---

## 3. 스킬 갭 지도 — Don의 지금 → 드론 FTE 목표

표의 "Don 지금"은 NOTES_SPEC에 적힌 경험 범위만 쓴다. 숫자나 사내 이름은 쓰지 않는다.

| 스킬 | Don 지금 | 목표 (드론 FTE) | 메우는 법 | 단원 · 랩 |
|---|---|---|---|---|
| 테스트 플랫폼 설계 | 챔버 테스트 플랫폼 SDK를 만들고 다른 엔지니어가 사용 | 여러 기체 · 여러 FW 스택을 덮는 HIL 플랫폼 | 같은 구조를 드론 용어로 다시 그림 (HAL · 장비 드라이버 · 시나리오) | [R09](2026-10-24_R09_scalable_reusable_test_platform.md) · L13 |
| UART 테스트 시퀀스 | eSSD UART 테스트 시퀀스 자동화 | CRSF · MSP · MAVLink 주입 · 판정 | CRSF 파서 + MSP 클라이언트 직접 작성 | [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · L6 · L7 |
| 버스 디버그 | PCIe · I2C · SPMI · RFFE 루트코즈, DSO · protocol analyzer | I2C/SPI 센서, UART RC 링크, DShot 파형 디버그 | 로직 분석기로 CRSF · DShot 캡처 · 디코드 | [R08](2026-10-24_R08_hil_bench_design_instruments.md) · L10 · L11 |
| 스트레스 · latent defect | stress 기반 latent defect 발굴, shmoo | 전압 저하 · 링크 손실 · 진동 · 온도 스트레스 | brown-out · 링크 손실 시나리오를 HIL 시나리오로 정의 | [R07](2026-10-24_R07_test_methodology_pyramid.md) · L10 |
| 텔레메트리 · 로그 | NVMe telemetry 디버그 기능, health monitoring FW | Blackbox · ULog · DataFlash 분석, 회귀 비교 | Blackbox 디코드 + step response 지표 자동화 | [R07](2026-10-24_R07_test_methodology_pyramid.md) · L8 |
| 에러 처리 설계 | error reporting/handling scheme | failsafe · arming 차단 플래그 · 상태 기계 검증 | failsafe 시나리오 테스트 (SITL) | [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · L3 · L4 |
| MCU FW | Cortex-R/M · Xtensa FW | STM32 F4/F7/H7 FC, ESP32 ELRS | Betaflight 스케줄러 소스 읽기, Renode | [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · L1 · L9 |
| factory test | factory test-node 아키텍처 | 드론 생산 라인 보드 테스트 · EOL | factory node 설계를 FC/ESC 보드 테스트로 옮김 | [R08](2026-10-24_R08_hil_bench_design_instruments.md) · [R09](2026-10-24_R09_scalable_reusable_test_platform.md) |
| web 자동화 · 대시보드 | web 기반 test automation | 테스트 결과 DB · 대시보드 (빌드별 회귀) | JUnit XML → 정적 리포트, 나중에 DB | [R09](2026-10-24_R09_scalable_reusable_test_platform.md) · L12 |
| pytest | **실무 경험 없음** | 플랫폼 표준 러너 | fixture · parametrize · marker · junitxml을 랩마다 사용 | L2 · L4 · L6 · L7 · L12 |
| GitLab CI · Jenkins | **실무 경험 없음** | 필수 (JD) | GitHub Actions로 개념 익힘 → GitLab CI yaml로 번역 | L12 |
| Bazel | **경험 없음** | 우대 (JD) | 작은 C 파서를 Bazel로 감싸는 실습 (선택) | L12 확장 |
| Betaflight · ELRS | **경험 없음** | 우대 (JD), 온사이트 질문 | 소스 읽기 · 유닛 테스트 · SITL · ELRS native test | L1 · L2 · L3 · L6 |
| PX4 · ArduPilot | 없음 [추정] | 우대 (JD) | SITL + 자동 테스트 프레임워크 따라 하기 | L4 · L5 |
| 드론 비행 · 물리 | **비행 경험 없음** | PID · 필터 · 모터 응답을 "판정 기준"으로 이해 | Beard & McLain 책 일부, Brian Douglas 영상, 시뮬레이터 비행 | §8 참고 자료 |
| RF 링크 테스트 | RF 하드웨어 칩셋 통합 | LQ · RSSI · 링크 마진, 감쇠기 | ELRS 구조 이해 → (하드웨어 생기면) 감쇠 실험 | [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · L10 확장 |
| 안전 | 챔버 · 전원 안전 [추정] | LiPo · 프롭 · 모터 안전 규칙 | 프롭 없이, 전류 제한 전원, LiPo 충전 규칙 | §6 안전 |

> 한 줄 정리: Don은 **"플랫폼 · 버스 · 스트레스 · 텔레메트리"** 축은 이미 있다. 8주 동안 채울 것은 **"드론 도메인 + pytest/CI 문화 + 오픈소스 실물"**이다.

---

## 4. 8주 로드맵 (4단계)

### 4.1 큰 그림

```text
 Phase 1 (W1-W2)         Phase 2 (W3-W4)            Phase 3 (W5-W6)              Phase 4 (W7-W8)
 지도 그리기              Betaflight + RC 링크        PX4/ArduPilot + 방법론        HIL + 플랫폼
 ┌──────────────┐        ┌──────────────────┐       ┌─────────────────────┐      ┌────────────────────┐
 │ R00 R01 R02  │──────▶ │ R03 R05          │─────▶ │ R04 R06 R07         │────▶ │ R08 R09            │
 │ L1 (읽기)    │        │ L2 L3 L6 L7      │       │ L4 L5 L8 L9         │      │ L10 L11 L12 L13    │
 │ 0원          │        │ 0원              │       │ 0원                 │      │ 하드웨어 (예산 확인) │
 └──────────────┘        └──────────────────┘       └─────────────────────┘      └────────────────────┘
       │                        │                           │                           │
       ▼                        ▼                           ▼                           ▼
  1쪽 지도 노트          crsf 파서 + 테스트,          SITL 자동 테스트 2개,         저장소 v0.1 공개,
  스케줄러 호출 그림      BF unit test 로그            로그 지표 스크립트             HIL 설계 문서
```

- 한 주 = 약 8~10시간 [추정] (평일 1시간 + 주말 4~5시간). 결과 대기 중이라 실제로는 더 빠를 수도 있다.
- 각 주는 **읽기 → 랩 → 남는 파일** 순서로 끝낸다. 남는 파일이 없으면 그 주는 끝난 게 아니다.
- 하드웨어가 늦어지면 Phase 4는 L12 · L13(0원)만 먼저 하고 L10 · L11은 뒤로 미룬다.

### 4.2 주별 표

| 주 | 단계 | 읽기 | 랩 | 남는 파일 (포트폴리오 · GitHub) |
|---|---|---|---|---|
| W1 | 1 지도 | [R00](2026-10-24_R00_overview_and_gemini_corrections.md), [R01](2026-10-24_R01_fte_vs_ssd_validation.md), Oscar Liang FC 입문 글 | 환경 준비 (Lima VM, Python venv, git) | `notes/W1_ssd_vs_drone.md` — 내 말로 쓴 비교 1쪽 |
| W2 | 1 지도 | [R02](2026-10-24_R02_drone_system_and_open_source_map.md), Betaflight wiki 개요 | **L1** Betaflight 스케줄러 소스 읽기 | `notes/L1_scheduler.md` + 호출 그림 (ASCII) |
| W3 | 2 Betaflight | [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) | **L2** BF unit test, **L3** BF SITL | `make test` 로그 요약, SITL 실행 스크린샷 · 메모 |
| W4 | 2 RC 링크 | [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md), [R07](2026-10-24_R07_test_methodology_pyramid.md) 앞부분 | **L6** CRSF 파서 + fuzz, **L7** MSP 클라이언트 (SITL 대상) | 저장소 `protocols/` + `tests/` 첫 커밋 |
| W5 | 3 자율 스택 | [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) | **L4** ArduPilot SITL + pymavlink 테스트, **L5** PX4 SIH + MAVSDK | `sim_tests/` — failsafe → RTL 테스트, takeoff/land 테스트 |
| W6 | 3 방법론 | [R07](2026-10-24_R07_test_methodology_pyramid.md) 나머지, [R06](2026-10-24_R06_companion_video_linux_rtos.md) | **L8** 로그 → step response 지표, **L9** Renode STM32 | `analysis/` 스크립트 + 리포트 예시, Renode robot 테스트 |
| W7 | 4 HIL | [R08](2026-10-24_R08_hil_bench_design_instruments.md) | **L12** CI (0원), **L10** 미니 HIL (하드웨어 있으면) | `.github/workflows/ci.yml`, 배선도 · 사진 |
| W8 | 4 플랫폼 | [R09](2026-10-24_R09_scalable_reusable_test_platform.md) | **L11** latency 측정 (하드웨어), **L13** 설계 문서 | `docs/hil_platform_design.md`, latency 히스토그램, 저장소 v0.1 태그 |

### 4.3 단계별 "끝났다"의 기준

| 단계 | 끝났다고 말할 수 있는 상태 |
|---|---|
| Phase 1 | 조종기 stick → 모터까지 신호 경로를 종이 없이 그릴 수 있다. Betaflight의 gyro/PID가 왜 스케줄러 밖(실시간 우선순위)에서 도는지 소스 위치와 함께 설명할 수 있다 |
| Phase 2 | CRSF 프레임을 손으로 디코드할 수 있다. Betaflight 유닛 테스트 하나를 일부러 깨뜨리고 고칠 수 있다. SITL에 MSP로 붙어 상태를 읽을 수 있다 |
| Phase 3 | SITL에서 "arm → takeoff → 링크 끊김 → RTL 확인" 테스트가 pytest로 자동 통과/실패한다. 로그에서 지표를 뽑아 숫자로 비교할 수 있다 |
| Phase 4 | CI가 push마다 유닛 · SITL 테스트를 돌린다. (하드웨어 있으면) 실물 FC에 CRSF를 주입하고 DShot 변화까지 지연 분포를 그렸다. HIL 플랫폼 설계를 남에게 설명하는 문서가 있다 |

---

## 5. 실습 랩 (L1~L13)

### 랩 한눈에

| 랩 | 이름 | 단원 | 시간 [추정] | 비용 | 환경 |
|---|---|---|---|---|---|
| L1 | Betaflight 스케줄러 · task 소스 읽기 | R03 | 3~4시간 | 0원 | 브라우저 또는 Mac clone |
| L2 | Betaflight 유닛 테스트 (Lima VM) | R03 · R07 | 2~4시간 | 0원 | Lima Ubuntu VM |
| L3 | Betaflight SITL + 내장 harness | R03 | 3~5시간 | 0원 | Lima VM (headless) |
| L4 | ArduPilot SITL + pymavlink failsafe 테스트 | R04 | 4~6시간 | 0원 | Lima VM 또는 Mac |
| L5 | PX4 SIH + MAVSDK-Python 테스트 | R04 | 3~5시간 | 0원 | Mac 또는 Lima VM |
| L6 | CRSF 파서 + fuzz 테스트 | R05 | 3~4시간 | 0원 | Mac Python |
| L7 | MSP 클라이언트 (SITL 대상) | R03 · R05 | 3~4시간 | 0원 | Mac ↔ Lima SITL |
| L8 | Blackbox 로그 디코드 + step response 지표 | R07 | 3~5시간 | 0원 | Mac Python |
| L9 | Renode STM32 + CI | R06 · R07 | 3~5시간 | 0원 | Lima VM / Actions |
| L10 | 미니 HIL 벤치 (실물 FC) | R08 | 1~2일 | 약 $150~ [확인 필요] | 실물 · 예산 확인 |
| L11 | RC → 모터 latency 측정 | R05 · R08 | 1일 | L10 + 로직 분석기 | 실물 · 예산 확인 |
| L12 | GitHub Actions CI (pytest + SITL) | R09 | 3~5시간 | 0원 | GitHub |
| L13 | 설계 문서 "Neros식 HIL 플랫폼" | R09 | 4~6시간 | 0원 | 글쓰기 |

> 모든 명령어 블록은 **검증 안 함 · 가이드**다. 2026-10-24 기준 공식 문서 · 소스에서 옮겼고, 버전이 바뀌면 달라질 수 있다.

---

### L1 Betaflight 스케줄러 · task 소스 읽기

- **목표**: "Betaflight에서 가장 먼저 도는 task는 무엇이고 왜인가"를 소스 위치와 함께 1쪽으로 설명한다.
- **사전 준비**: git, 코드 브라우저(VS Code 또는 GitHub 웹). 빌드는 필요 없다.
- **비용 · 시간**: 0원 · 3~4시간.

확인한 사실 (2026-10-24, master 브랜치 GitHub API):

- `src/main/scheduler/scheduler.c`, `scheduler.h`가 있다.
- `scheduler.h`의 우선순위 enum: `TASK_PRIORITY_REALTIME = -1` (주석: "Task will be run outside the scheduler logic"), `LOWEST = 1`, `LOW = 2`, `MEDIUM = 3`, `MEDIUM_HIGH = 4`, `HIGH = 5`, `MAX = 255`.
- task id에 `TASK_GYRO`, `TASK_FILTER`, `TASK_PID`, `TASK_RX`, `TASK_SERIAL` 등이 있다.
- task 테이블은 `src/main/fc/tasks.c`에 있다.

```text
# 검증 안 함 · 가이드
git clone --depth 1 https://github.com/betaflight/betaflight.git
cd betaflight
grep -n "TASK_PRIORITY" src/main/scheduler/scheduler.h
grep -n "DEFINE_TASK\|TASK_PRIORITY_REALTIME" src/main/fc/tasks.c | head -40
grep -n "void scheduler(" -A 80 src/main/scheduler/scheduler.c | less
grep -rn "scheduler()" src/main/main.c src/platform/*/
```

단계:

1. `main.c`에서 `init()` 다음 무한 루프가 `scheduler()`를 부르는 곳을 찾는다.
2. `scheduler()` 안에서 gyro 시간(gyro 샘플 주기)에 맞춰 **실시간 task(GYRO → FILTER → PID)**를 먼저 처리하는 부분을 찾는다 [확인 필요: 함수 이름 · 순서는 버전마다 다름].
3. 남는 시간에 나머지 task 중 하나를 고르는 규칙을 찾는다. 키워드: `dynamicPriority`, `taskAgeCycles`, `checkFunc` [확인 필요].
4. `tasks.c`에서 RX, SERIAL(MSP), BATTERY, OSD, BLACKBOX task의 주기와 우선순위를 표로 옮긴다.
5. 1쪽 노트를 쓴다: "최우선은 gyro 동기 실시간 task, 이유는 제어 루프 주기 흔들림(jitter)이 비행 품질을 바로 망치기 때문".

- **예상 결과**: task 이름 · 주기 · 우선순위 표 1개, 호출 그림 1개.
- **성공 기준**: 소스 줄 번호를 3곳 이상 인용한다. "RX task가 늦으면 무엇이 늦어지나"를 설명할 수 있다.
- **확장**: CLI `tasks` 명령 출력 형식을 소스(`cli.c`)에서 찾아, 출력이 무엇을 재는지 적는다. 같은 걸 INAV `src/main/scheduler/`와 비교.
- **테스트 관점**: "새 task 추가로 PID 루프 주기가 흔들리지 않는가"는 좋은 회귀 테스트다. SITL이나 HIL에서 CLI `tasks` · `status`(CPU load)를 빌드마다 저장하면 된다 [추정].

---

### L2 Betaflight 유닛 테스트 빌드 (Lima VM)

- **목표**: Betaflight googletest 유닛 테스트를 돌리고, 하나를 일부러 깨뜨려 실패를 읽는다.
- **사전 준비**: Lima Ubuntu VM (x86 에뮬레이션 필요 없음 — 테스트는 호스트 아키텍처로 빌드됨 [추정]).
- **비용 · 시간**: 0원 · 2~4시간.

확인한 사실:

- 최상위 `Makefile`에 `test`, `junittest`, `test-all`, `test-representative`, `test_help`, `test_%` 타깃이 있다 (`cd src/test && $(MAKE) $@`).
- `src/test/Makefile`은 기본 컴파일러로 `clang-18`을 찾고, 없으면 `clang`으로 내려간다. gcc는 "experimental" 경고를 낸다.
- Betaflight CI(`.github/workflows/ci.yml`) 테스트 job은 `sudo apt-get install -y libblocksruntime-dev clang-18` 후 `make EXTRA_FLAGS=-Werror checks`, `make EXTRA_FLAGS=-Werror test-all`을 돌린다.
- `src/test/unit/`에 `rx_crsf_unittest.cc`, `flight_failsafe_unittest.cc`, `pid_unittest.cc`, `arming_prevention_unittest.cc`, `msp_serial_unittest.cc`, `scheduler_stubs.c` 등이 있다.

```text
# 검증 안 함 · 가이드  (Lima VM 안)
limactl shell default            # 또는 Don이 쓰는 인스턴스 이름 (예: elinux)
sudo apt-get update
sudo apt-get install -y build-essential git clang-18 libblocksruntime-dev
git clone https://github.com/betaflight/betaflight.git && cd betaflight
make test_help | head -50        # 테스트 목록 보기
make test_rx_crsf_unittest       # 하나만 먼저 [확인 필요: 타깃 이름 형식]
make test 2>&1 | tee ~/bf_test.log
make junittest                   # JUnit XML 결과 (CI 연동용)
```

단계:

1. `make test_help`로 테스트 목록을 본다.
2. CRSF 테스트 하나를 먼저 돌린다. 실패하면 컴파일러 버전부터 의심한다 (`make test_versions`).
3. 전체 `make test`를 돌리고 통과 · 실패 개수를 적는다.
4. `rx_crsf_unittest.cc`에서 CRC 기대값 하나를 바꿔 실패를 만든다. 실패 메시지 형식을 기록하고 되돌린다.
5. `junittest`로 XML이 어디에 생기는지 확인한다 (L12에서 씀).

- **예상 결과**: googletest 형식 `[  PASSED  ]` 줄들, 전체 소요 시간.
- **성공 기준**: 전체 통과 로그 + 일부러 깬 테스트 1개의 실패 로그.
- **확장**: "Betaflight 유닛 테스트가 덮지 않는 것" 목록 쓰기 (타이밍, 실제 UART 오류, 인터럽트 경합 등) → HIL이 왜 필요한지의 근거가 된다. `src/test/unit/`의 `.cc.txt`로 꺼져 있는 테스트가 왜 꺼졌는지 git log로 추적.
- **SSD 연결**: SSD FW도 호스트에서 도는 모듈 테스트와 실제 보드 테스트가 따로였다. 같은 경계를 Betaflight에서 찾는 랩이다.

---

### L3 Betaflight SITL (+ 저장소 안 harness)

- **목표**: 하드웨어 없이 Betaflight를 PC 프로그램으로 돌리고, MSP로 상태를 읽고, RC를 UDP로 넣는다.
- **사전 준비**: L2의 Lima VM. Gazebo는 선택 (무거우니 처음엔 생략).
- **비용 · 시간**: 0원 · 3~5시간.

확인한 사실 (betaflight.com SITL 문서 · 소스):

- 빌드: `make TARGET=SITL`, 결과물 `obj/main/betaflight_SITL.elf`. 자동조종(flight plan) 기능까지 넣으려면 문서는 `make TARGET=SITL EXTRA_FLAGS="-DENABLE_FLIGHT_PLANNING=1"`을 쓴다.
- 플랫폼 소스는 `src/platform/SIMULATOR/` (`sitl.c`, `udplink.c`), 타깃은 `src/platform/SIMULATOR/target/SITL/`.
- 포트: 9001 SITL → RealFlight (raw PWM), 9002 SITL → 시뮬레이터 (모터), 9003 시뮬레이터 → SITL (FDM 상태), 9004 외부 → SITL (RC 입력), TCP 5761 = UART1 (MSP · CLI). UARTx는 `tcp://127.0.0.1:576x`.
- Configurator 연결: `websockify 127.0.0.1:6761 127.0.0.1:5761` 후 온라인 Configurator에서 `ws://127.0.0.1:6761` (문서: 2025.12 이후 온라인 Configurator 필요).
- **중요 발견**: 저장소에 `src/test/sitl/sitl_harness.py`가 있다. 설명: "SITL end-to-end harness: UDP RC + FDM feeds, MSP introspection, scenario runner". RC는 UDP 9004 (`double timestamp + 16 x uint16`), FDM은 UDP 9003, MSP는 TCP 5761. 시나리오는 rx-loss 정책(DISABLE / CONTINUE / LAND)과 geofence를 확인한다. `USE_FLIGHT_PLAN`으로 빌드한 SITL이 필요하다.

```text
# 검증 안 함 · 가이드  (Lima VM 안, betaflight 폴더)
make TARGET=SITL EXTRA_FLAGS="-DENABLE_FLIGHT_PLANNING=1"
ls -la obj/main/betaflight_SITL.elf
./obj/main/betaflight_SITL.elf &          # eeprom.bin이 현재 폴더에 생김
python3 src/test/sitl/sitl_harness.py --help
python3 src/test/sitl/sitl_harness.py --binary obj/main/betaflight_SITL.elf --scenario all
python3 src/test/sitl/sitl_harness.py --binary obj/main/betaflight_SITL.elf --scenario rx_continue -v
```

단계:

1. SITL을 빌드하고 실행한다. 로그에 UDP · TCP 서버가 열렸다는 줄이 나오는지 본다.
2. `nc 127.0.0.1 5761`로 붙어 `#`(CLI 진입)을 보내 본다 [확인 필요: SITL UART1 기본 기능이 MSP라 CLI 진입이 되는지].
3. `sitl_harness.py`를 `--help`부터 읽는다. 시나리오 목록, pass/fail 판정 방법, 출력 형식을 노트에 옮긴다.
4. `--scenario all`을 돌려 결과를 저장한다.
5. **이 harness의 구조를 해부한다**: RC 송신 스레드, FDM 송신, MSP 조회, 시나리오 함수. L13 설계 문서의 "참고 구현"이 된다.

- **예상 결과**: 시나리오별 PASS/FAIL 줄 [확인 필요: 실제 형식].
- **성공 기준**: 최소 1개 시나리오 통과 + harness 구조 그림 1개.
- **확장**: harness를 pytest로 감싼다 (`@pytest.mark.parametrize("scenario", [...])`). Mac에서 Lima 포트 포워딩으로 붙기 (Lima는 localhost 포워딩 기본 지원 [확인 필요: UDP 포워딩 여부]). Gazebo 연결은 가이드만: `betaflight/aeroloop_gazebo`, `betaflight/betaloop`.
- **Gemini 비교**: Gemini는 SITL을 "`make TARGET=SITL`로 빌드해서 CI에서 Python으로 가상 센서값을 넣는다"고 했다. 방향은 맞다. 하지만 이미 공식 저장소에 harness가 있다는 건 말하지 않았다.

> ⚠️ Gemini 정정: Gemini 표의 SITL 경로 `src/main/target/SITL/`는 지금 master 기준으로 `src/platform/SIMULATOR/target/SITL/`이다 (공식 문서 일부도 옛 경로 `src/main/target/SITL/pg.ld`를 아직 언급함).

---

### L4 ArduPilot SITL + pymavlink "autotest 스타일" 테스트

- **목표**: ArduPilot SITL에서 arm → takeoff → RC failsafe → RTL 진입을 pymavlink 스크립트로 자동 판정한다. 그다음 공식 autotest의 같은 테스트와 비교한다.
- **사전 준비**: Lima VM(Ubuntu) 권장. Mac 직접 설치 스크립트도 있다 (`Tools/environment_install/install-prereqs-mac.sh`) [Apple Silicon 동작 확인 필요].
- **비용 · 시간**: 0원 · 4~6시간 (첫 설치가 오래 걸림).

확인한 사실:

- 설치 스크립트: `Tools/environment_install/install-prereqs-ubuntu.sh`, `install-prereqs-mac.sh` 등이 있다.
- SITL 실행: `sim_vehicle.py -v copter --console --map -w` (처음엔 `-w`로 가상 EEPROM 초기화).
- 공식 autotest: `./Tools/autotest/autotest.py build.Copter`, 개별 테스트 `test.Copter.<이름>`, 목록 `--list`.
- `Tools/autotest/arducopter.py`에 `ThrottleFailsafe`, `GCSFailsafe`, `RCProtocolFailsafe` 등이 있다. `ThrottleFailsafe`는 `SIM_RC_FAIL` 파라미터로 RC 손실을 흉내 내고 `FS_THR_ENABLE`로 정책을 바꾼 뒤 `wait_mode(...)`로 모드 변화를 확인한다.

```text
# 검증 안 함 · 가이드  (Lima VM)
git clone --recurse-submodules https://github.com/ArduPilot/ardupilot.git
cd ardupilot
Tools/environment_install/install-prereqs-ubuntu.sh -y
. ~/.profile
./waf configure --board sitl && ./waf copter        # BUILD.md 방식 [확인 필요]
sim_vehicle.py -v copter --console --map -w          # 첫 실행
./Tools/autotest/autotest.py --list | grep -i failsafe
./Tools/autotest/autotest.py build.Copter test.Copter.ThrottleFailsafe
```

내가 쓸 pymavlink 테스트 뼈대 (pytest). `sim_vehicle.py`가 MAVProxy를 띄우고 UDP로 내보내는 포트에 붙는다 [확인 필요: 14550/14551 중 어디가 비어 있는지].

```python
# 검증 안 함 · 가이드 — tests/sitl/test_ardupilot_rc_failsafe.py
import time
import pytest
from pymavlink import mavutil

CONN = "udpin:0.0.0.0:14551"   # [확인 필요] sim_vehicle.py --out 으로 맞출 것


@pytest.fixture(scope="module")
def mav():
    m = mavutil.mavlink_connection(CONN)
    m.wait_heartbeat(timeout=30)
    yield m
    m.close()


def set_param(m, name, value):
    m.mav.param_set_send(m.target_system, m.target_component, name.encode(), float(value),
                         mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
    t0 = time.time()
    while time.time() - t0 < 5:
        msg = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=1)
        if msg and msg.param_id == name:
            return
    raise TimeoutError(name)


def wait_mode(m, mode, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        m.recv_match(type="HEARTBEAT", blocking=True, timeout=1)
        if m.flightmode == mode:
            return time.time() - t0
    raise AssertionError(f"mode {m.flightmode} != {mode} after {timeout}s")


def test_rc_loss_triggers_rtl(mav):
    set_param(mav, "FS_THR_ENABLE", 1)          # 1 = RTL (Copter 문서 기준) [확인 필요]
    mav.set_mode("GUIDED")
    mav.arducopter_arm()
    mav.motors_armed_wait()
    mav.mav.command_long_send(mav.target_system, mav.target_component,
                              mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0, 0, 0, 0, 0, 0, 0, 10)
    time.sleep(15)                               # 실제로는 GLOBAL_POSITION_INT 고도를 기다릴 것
    set_param(mav, "SIM_RC_FAIL", 1)             # RC 끊김 흉내
    dt = wait_mode(mav, "RTL", timeout=10)
    set_param(mav, "SIM_RC_FAIL", 0)
    assert dt < 5.0                               # 판정 기준은 내가 정한 값 — 근거를 노트에 적을 것
```

단계:

1. 공식 `ThrottleFailsafe`를 먼저 돌려 통과를 본다.
2. `arducopter.py`의 `ThrottleFailsafe` 함수를 읽고 subtest 구조(`start_subtest`, `wait_mode`, `delay_sim_time`)를 표로 옮긴다.
3. 위 뼈대를 내 저장소에 넣고 고도 대기 · 타임아웃 · 정리(teardown)를 보강한다.
4. 테스트를 3번 연속 돌려 시간 분포를 본다 (시뮬레이터도 흔들린다 → flaky 관리 연습).

- **예상 결과**: `1 passed`, RTL 진입까지 걸린 시간 숫자.
- **성공 기준**: 내 테스트와 공식 테스트 둘 다 통과. 일부러 `FS_THR_ENABLE=0`으로 바꾸면 내 테스트가 **실패**한다 (테스트가 진짜로 무언가를 확인한다는 증거).
- **확장**: 판정을 "모드"에서 "행동"으로 바꾸기 — 홈 쪽으로 거리가 줄어드는지 `GLOBAL_POSITION_INT`로 확인. `SIM_` 파라미터로 GPS 끊김 · 배터리 저하 넣기. `--speedup` 옵션으로 시간 단축 [확인 필요].
- **SSD 연결**: 공식 autotest는 "시나리오 + 대기 함수 + 판정"이 라이브러리로 정리돼 있다. Don이 만든 챔버 SDK의 "시퀀스 + 상태 모니터링"과 같은 모양이다.

---

### L5 PX4 SITL (SIH) + MAVSDK-Python 테스트

- **목표**: PX4를 GUI 없이(SIH) 돌리고 MAVSDK-Python으로 arm → takeoff → land를 판정한다. PX4 공식 `mavsdk_tests` 구조를 읽는다.
- **사전 준비**: Mac(공식 macOS 스크립트, CI가 Apple Silicon에서 돎) 또는 Lima VM.
- **비용 · 시간**: 0원 · 3~5시간.

확인한 사실:

- macOS 설정: `xcode-select --install`, `ulimit -S -n 2048`을 `~/.zshrc`에 추가, clone 후 `./Tools/setup/macos.sh --sim-tools`, `source .venv/bin/activate`. 문서: "PX4 CI exercises this setup on Apple Silicon runners only".
- Gazebo: `make px4_sitl gz_x500`.
- SIH(headless): `make px4_sitl_sih sihsim_quadx` (문서가 `px4_sitl_sih` 타깃을 쓰라고 강조. `px4_sitl`은 Gazebo 라이브러리도 빌드함).
- 공식 통합 테스트: `test/mavsdk_tests/mavsdk_test_runner.py test/mavsdk_tests/configs/sih-sitl.json --speed-factor 10` (MAVSDK C++ 라이브러리 시스템 설치 필요).
- MAVSDK-Python: 지금 README는 `pip3 install mavsdk-grpc`, `from mavsdk_grpc import System`, `await drone.connect(system_address="udpin://0.0.0.0:14540")`. 예제 `takeoff_and_land.py`는 `drone.action.arm()`, `takeoff()`, `land()`를 쓴다. 옛 이름은 `pip install mavsdk`, `from mavsdk import System` [확인 필요: 두 패키지의 관계 · 지금 PyPI 최신].

```text
# 검증 안 함 · 가이드  (Mac)
xcode-select --install
echo "ulimit -S -n 2048" >> ~/.zshrc      # 새 터미널 열기
git clone https://github.com/PX4/PX4-Autopilot.git && cd PX4-Autopilot
git submodule update --init --recursive --force
./Tools/setup/macos.sh --sim-tools
source .venv/bin/activate
make px4_sitl_sih sihsim_quadx             # pxh> 프롬프트가 뜨면 성공
```

```python
# 검증 안 함 · 가이드 — tests/sitl/test_px4_takeoff_land.py
import asyncio
import pytest
from mavsdk_grpc import System   # 옛 버전이면 from mavsdk import System


async def wait_altitude(drone, target_m, timeout):
    async def _w():
        async for pos in drone.telemetry.position():
            if pos.relative_altitude_m >= target_m:
                return pos.relative_altitude_m
    return await asyncio.wait_for(_w(), timeout)


@pytest.mark.asyncio   # pytest-asyncio 필요
async def test_takeoff_and_land():
    drone = System()
    await drone.connect(system_address="udpin://0.0.0.0:14540")
    async for s in drone.core.connection_state():
        if s.is_connected:
            break
    async for h in drone.telemetry.health():
        if h.is_global_position_ok and h.is_home_position_ok:
            break
    await drone.action.arm()
    await drone.action.takeoff()
    alt = await wait_altitude(drone, 2.0, timeout=30)
    assert alt >= 2.0
    await drone.action.land()
    async for in_air in drone.telemetry.in_air():
        if not in_air:
            break
```

- **예상 결과**: `1 passed`. PX4 콘솔에 takeoff · landing 메시지.
- **성공 기준**: 내 테스트 통과 + `mavsdk_test_runner.py`의 config JSON(모델 · 테스트 케이스 · 속도 배수)을 읽고 구조를 표로 정리.
- **확장**: 비행 후 ULog(`build/px4_sitl_sih/rootfs/log/...` [확인 필요])를 `pyulog`(`ulog_info`, `ulog2csv`)로 읽어 최대 기울기 같은 지표를 판정에 추가 → L8과 연결. RC 손실은 PX4 파라미터 `COM_RCL_EXCEPT` · `NAV_RCL_ACT` 등으로 [확인 필요].
- **L4와 비교**: ArduPilot은 pymavlink로 저수준 메시지를 직접 다룬다. PX4 테스트는 MAVSDK 고수준 API를 쓴다. 플랫폼 설계에서는 둘 다 같은 "기체 인터페이스(HAL)" 뒤에 숨긴다 ([R09](2026-10-24_R09_scalable_reusable_test_platform.md)).

---

### L6 CRSF 파서 + fuzz 테스트

- **목표**: CRSF 스트림 파서를 만들고, 깨진 바이트 · 가짜 헤더 · 잘린 프레임에서도 다시 동기화(resync)하는지 대량 무작위 테스트로 확인한다.
- **사전 준비**: Python 3.9+. 출발점은 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)의 [`../code/crsf.py`](../code/crsf.py)와 기존 연습 [`../code/t2_packets.py`](../../onsiteKit/code/typealong/t2_packets.py), [`../code/t7_struct_crc.py`](../../onsiteKit/code/typealong/t7_struct_crc.py).
- **비용 · 시간**: 0원 · 3~4시간.

CRSF 기본 (자세한 건 R05): `[addr][len][type][payload...][crc]`, len = type + payload + crc 바이트 수, CRC-8 poly 0xD5를 type부터 payload 끝까지 계산, RC 채널 프레임(type 0x16)은 16채널 × 11비트 = 22바이트 payload [R05에서 확인].

단계:

1. `crsf.py`를 읽고 자체 테스트를 돌린다: `python3 -I ../code/crsf.py`.
2. 정상 프레임 생성기를 만든다 (채널 값 → 11비트 패킹 → CRC).
3. 변형기(mutator)를 만든다: 1바이트 뒤집기, 바이트 삭제, 바이트 삽입, 프레임 중간 자르기, 가짜 sync(0xC8) 삽입, len을 최대값보다 크게.
4. 속성(property)을 정한다: ① 정상 프레임은 전부 나온다, ② CRC가 틀린 프레임은 절대 나오지 않는다, ③ 깨진 구간 뒤 첫 정상 프레임은 반드시 잡는다, ④ 버퍼는 상한을 넘지 않는다.
5. 시드를 고정한 무작위 루프 10만 회. 실패하면 시드와 입력을 저장해 재현한다.
6. 같은 파서를 C로 옮기고(T2 C 버전을 출발점으로) Python과 C 결과를 비교하는 차분(differential) 테스트를 붙인다.

```python
# 검증 안 함 · 가이드 — tests/protocols/test_crsf_fuzz.py (모양만)
import random
import pytest

SEEDS = range(20)

@pytest.mark.parametrize("seed", SEEDS)
def test_resync_after_garbage(seed):
    rng = random.Random(seed)
    good = [make_rc_frame([rng.randrange(172, 1812) for _ in range(16)]) for _ in range(50)]
    stream, expected = corrupt_between(good, rng)      # 앞뒤에 쓰레기 · 가짜 0xC8
    out = list(CrsfParser().feed_all(stream))
    assert out == expected                               # 살아남아야 할 프레임만, 순서대로
```

- **예상 결과**: 수십~수백 개 parametrize 케이스 통과.
- **성공 기준**: 일부러 넣은 버그(예: CRC 실패 시 프레임 통째로 버림 → 그 안의 진짜 sync를 잃음)를 fuzz가 **잡아낸다**.
- **확장**: `hypothesis` 라이브러리로 속성 기반 테스트. Betaflight `src/main/rx/crsf.c`의 프레임 처리와 내 파서의 차이 적기. ELRS 쪽 테스트도 본다 — `src/test/test_crsf`, `test_crc`, `test_fhss`가 있고, ELRS CI는 `PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" pio test -e native`로 돌린다 (`src/platformio.ini`의 `[env:native]`).

```text
# 검증 안 함 · 가이드  (ELRS native 테스트, Mac 또는 Lima)
python3 -m pip install platformio
git clone https://github.com/ExpressLRS/ExpressLRS.git && cd ExpressLRS/src
PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_ISM_2400" pio test -e native
```

---

### L7 MSP 클라이언트 (L3 SITL 대상)

- **목표**: Python MSP v1/v2 클라이언트를 만들어 SITL(TCP 5761)에서 `MSP_STATUS`, `MSP_ATTITUDE`를 읽고, RC를 넣어 arm 상태가 바뀌는지 확인한다.
- **사전 준비**: L3 SITL 실행 중. 출발점: [`../code/msp_client.py`](../code/msp_client.py)(R03/R05의 MSP v1/v2 스케치), [`../code/t3_retry.py`](../../onsiteKit/code/typealong/t3_retry.py)(요청 · 응답 · 타임아웃 · 재시도), `sitl_harness.py`의 MSP 부분.
- **비용 · 시간**: 0원 · 3~4시간.

확인한 사실: `sitl_harness.py`는 `MSP_STATUS = 101`, `MSP_RAW_GPS = 106`, `MSP_ATTITUDE = 108`, `MSP_BOXIDS = 119`, `MSP_ACC_CALIBRATION = 205`를 쓴다. RC 입력은 MSP가 아니라 UDP 9004로 넣는다.

단계:

1. MSP v1 요청 프레임 `$M<` + size + cmd + payload + XOR을 만든다 (형식은 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)).
2. 응답 파서: `$M>` (정상) / `$M!` (에러). 스트림 resync는 L6 파서 구조를 재사용한다.
3. `MSP_ATTITUDE` 응답을 `struct`로 풀어 roll · pitch · yaw를 출력한다 [확인 필요: 단위 · 스케일은 Betaflight `msp.c`에서 확인].
4. RC를 UDP 9004로 보내 arm 스위치를 켠다. `MSP_STATUS`의 arming 플래그가 바뀌는지 본다. 안 되면 "arming disable flags"를 읽어 이유를 출력한다.
5. pytest fixture로 감싼다: `sitl` fixture(프로세스 시작 · 종료 · eeprom 초기화), `msp` fixture(연결).

- **예상 결과**: 자세 값이 주기적으로 찍힘. arm 시도 결과가 플래그로 설명됨.
- **성공 기준**: "arm이 왜 안 되는가"를 MSP로 읽은 플래그 이름으로 답할 수 있다. 이건 실물 HIL에서 가장 먼저 부딪힐 문제다 [추정].
- **확장**: MSP v2(`$X<`, CRC-8 DVB-S2)도 지원. `msp_serial_unittest.cc`와 비교. 같은 클라이언트를 pyserial로 바꿔 실물 FC USB에 붙이기 (L10).

---

### L8 Blackbox 로그 디코드 + step response 지표

- **목표**: Betaflight blackbox 로그(`.bbl`)를 CSV로 풀고, setpoint 대비 gyro 응답에서 상승 시간 · 오버슈트 · 정착 시간 · 지연을 숫자로 뽑아 "빌드 A vs 빌드 B" 회귀 비교를 한다.
- **사전 준비**: 공개된 샘플 로그 [확인 필요: 어디서 받을지 — blackbox-log-viewer 저장소나 포럼], 또는 L3 SITL에서 blackbox를 켜서 만든 로그 [확인 필요: SITL blackbox 저장 지원 여부]. 출발점: [R07](2026-10-24_R07_test_methodology_pyramid.md)의 [`../code/step_response_oracle.py`](../code/step_response_oracle.py), [`../code/t4_window.py`](../../onsiteKit/code/typealong/t4_window.py).
- **비용 · 시간**: 0원 · 3~5시간.

```text
# 검증 안 함 · 가이드
git clone https://github.com/betaflight/blackbox-tools.git && cd blackbox-tools
make                                 # blackbox_decode 생성 [확인 필요: 최신 BF 로그 형식 지원 여부]
./obj/blackbox_decode ~/logs/LOG00001.BBL     # → .csv 생성
python3 -I ../code/step_response_oracle.py    # 자체 테스트 먼저
```

단계:

1. CSV 열 이름에서 `setpoint[0..2]`, `gyroADC[0..2]`, `motor[0..3]`, `time` 열을 찾는다 [확인 필요: 열 이름].
2. setpoint가 계단처럼 바뀐 구간을 자동으로 찾는다.
3. 구간마다 `step_response_oracle.py`로 지표를 뽑는다.
4. 두 로그(예: PID 값이 다른 두 비행)를 비교해 표 하나로 출력한다. 기준(예: 오버슈트 +20% 넘으면 경고)은 내가 정하고 근거를 적는다.
5. PX4라면 같은 일을 `pyulog`(`ulog2csv`)로 한다.

- **예상 결과**: 축별 지표 표, 회귀 판정 1줄.
- **성공 기준**: 같은 로그를 두 번 넣으면 차이 0. 일부러 gyro에 지연을 넣은 합성 데이터에서 지연 지표가 그만큼 늘어난다 (오라클을 오라클로 검증).
- **확장**: PlotJuggler로 시각 확인. PIDtoolbox 개념(주파수 응답) 읽기 [확인 필요: 현재 배포처]. 결과를 JSON으로 저장해 L12 CI 아티팩트로 남기기.
- **SSD 연결**: NVMe telemetry로 상태를 꺼내 판정하던 일과 같다. 다른 점은 "정답이 하나가 아니라 **허용 범위**"라는 것.

---

### L9 Renode STM32 + CI

- **목표**: STM32 펌웨어를 실물 없이 Renode에서 돌리고, UART 출력을 Robot Framework 테스트로 판정하고, GitHub Actions에서 돌린다.
- **사전 준비**: Renode (Linux 패키지 · macOS 패키지, releases 페이지) [확인 필요: Apple Silicon 네이티브 여부]. Lima VM이 안전하다.
- **비용 · 시간**: 0원 · 3~5시간.

확인한 사실:

- Renode 저장소에 `scripts/single-node/stm32f4_discovery.resc` 예제가 있다.
- 테스트는 `.robot` 파일 (`Resource ${RENODEKEYWORDS}`), 실행은 `renode-test my_test.robot`. 결과는 `robot_output.xml`, `log.html`, `report.html`.
- Antmicro의 `antmicro/renode-test-action`이 있다 (예: `uses: antmicro/renode-test-action@v4`, `tests-to-run: 'tests/**/*.robot'`) [확인 필요: 최신 버전 태그].
- ArduPilot 조직에 `ArduPilot/renode` 포크가 있다 [확인 필요: 무엇에 쓰는지].

```text
# 검증 안 함 · 가이드  (Renode 모니터 안)
(monitor) start @scripts/single-node/stm32f4_discovery.resc
```

```text
# 검증 안 함 · 가이드 — tests/renode/uart_hello.robot (모양만)
*** Settings ***
Suite Setup     Setup
Suite Teardown  Teardown
Test Teardown   Test Teardown
Resource        ${RENODEKEYWORDS}

*** Test Cases ***
Firmware Should Print Banner
    Execute Command         include @scripts/single-node/stm32f4_discovery.resc
    Create Terminal Tester  sysbus.usart2
    Start Emulation
    Wait For Line On Uart   Hello    timeout=5
```

단계:

1. 예제 `.resc`를 그대로 돌려 UART 창에 출력이 나오는지 본다.
2. 내가 빌드한 작은 STM32F4 펌웨어(예: libopencm3 또는 Zephyr 샘플)를 올린다 [확인 필요: 어떤 UART가 연결되는지].
3. Robot 테스트를 쓰고 `renode-test`로 돌린다.
4. GitHub Actions에 `renode-test-action`을 붙인다.
5. (도전) Betaflight 타깃 하나를 Renode에서 부팅 시도 — 대부분 주변장치 모델 부족으로 막힐 것 [추정]. 막힌 지점을 기록하는 것 자체가 결과물.

- **예상 결과**: `report.html`에 테스트 1개 통과.
- **성공 기준**: CI에서 초록불. 펌웨어 문자열을 바꾸면 빨간불.
- **확장**: Renode에서 I2C 센서 모델 붙이기, 시간 조작(가상 시간)으로 타임아웃 테스트를 빠르게.
- **위치**: 피라미드에서 SITL과 HIL 사이의 "에뮬레이션" 칸 ([R07](2026-10-24_R07_test_methodology_pyramid.md)). 레지스터 수준 드라이버를 실물 없이 시험할 수 있다는 점이 SITL과 다르다.

---

### L10 미니 HIL 벤치 (실물 FC + USB-UART + 로직 분석기)

> **사기 전에 Don과 예산 확인.** 아래 가격은 모두 [확인 필요] 추정치.
> **안전**: 프롭은 절대 달지 않는다. 처음에는 ESC · 모터도 연결하지 않는다. 전원은 USB 또는 **전류 제한 벤치 전원**으로. LiPo는 이 랩에서 쓰지 않는다.

- **목표**: 책상 위에 FC 1개를 놓고, Python이 ① CRSF를 주입하고 ② MSP로 상태를 읽고 ③ 로직 분석기로 모터 출력(DShot)을 캡처해 "RC 손실 → 모터 출력 0까지 시간"을 자동 판정한다.
- **사전 준비**: Betaflight FC (STM32F405/F722급), USB-UART 어댑터 (3.3 V, 420000 baud 지원 — FTDI FT232R/FT232H 또는 CP2102N [확인 필요: 각 칩의 420k 지원]), 로직 분석기, 점퍼선. 선택: ELRS RX.
- **비용 · 시간**: 약 $100~150 [확인 필요] · 1~2일.

```text
                ┌─────────────── Mac / Lima (pytest) ───────────────┐
                │  crsf_tx.py     msp_client.py     la_capture.py    │
                └──────┬──────────────┬──────────────────┬──────────┘
                  USB  │         USB  │             USB  │
               ┌───────▼──────┐ ┌─────▼─────┐   ┌────────▼────────┐
               │ USB-UART 3V3 │ │ FC USB    │   │ Logic analyzer  │
               │ TX ──────────┼─┼──▶ UARTx RX (CRSF 입력)          │
               │ GND ─────────┼─┼── GND     │   │ CH0: CRSF 선    │
               └──────────────┘ │ MOTOR1 ───┼───┼─▶ CH1: DShot M1 │
                                │ (ESC 없음)│   │ CH2: GPIO 토글  │
                                └───────────┘   └─────────────────┘
```

단계:

1. **무전원 점검**: 멀티미터로 5 V · 3V3 · GND 사이 단락 확인 (R08 보드 bring-up 절차).
2. Betaflight Configurator로 펌웨어 확인, CLI `diff all`을 저장해 기준 설정으로 쓴다.
3. 포트 탭에서 UARTx를 Serial RX로, 수신기 프로토콜을 CRSF로 [확인 필요: CLI 명령은 `serialrx_provider = CRSF`].
4. Python으로 CRSF RC 프레임을 주기적으로(예: 250 Hz [추정]) 보낸다. Configurator 수신기 탭에서 막대가 움직이는지 본다.
5. Motors 탭은 **ESC · 모터가 없을 때만** 써서 DShot 출력을 로직 분석기로 캡처한다. DShot은 sigrok 공식 디코더가 없다 → 캡처를 내보내 [`../code/dshot.py`](../code/dshot.py)로 펄스 폭 → 프레임 디코드하거나 Saleae 확장 [확인 필요].
6. 시나리오: arm(스위치 채널) → 스로틀 약간 → CRSF 송신 중단 → MSP로 failsafe 단계 읽기 + DShot이 0(또는 disarm 값)으로 떨어질 때까지 시간 측정.
7. 30회 반복해 분포를 기록한다.

- **예상 결과**: 반복마다 "링크 끊김 → failsafe → 모터 정지" 시간. Betaflight failsafe에는 stage 1/stage 2와 guard time이 있다 ([Betaflight Failsafe 문서](https://betaflight.com/docs/wiki/guides/current/Failsafe), 기본값 [확인 필요]).
- **성공 기준**: pytest 하나가 장비 3개를 묶어 PASS/FAIL을 낸다. 측정값이 문서상 설정값과 설명 가능한 범위로 맞는다.
- **확장**: ELRS RX를 붙여 TX 없이 bind 상태를 보기 (TX 모듈이나 조종기 필요). 전류 제한 전원으로 전압을 천천히 낮춰 brown-out 동작 기록 (FC 단독, 모터 없음). RF 감쇠기 실험은 가이드만 (SMA 감쇠기 · 차폐 필요, 전파 규정 확인).
- **SSD 연결**: Don이 해 본 "UART 테스트 시퀀스 + 상태 모니터링 + 계측기" 조합과 같다. 다른 점은 판정 대상이 **시간**이라는 것.

---

### L11 RC → 모터 latency 측정

> **사기 전에 Don과 예산 확인.** L10 장비 + (선택) ELRS TX/RX 한 쌍.
> **안전**: 프롭 없음. 모터를 쓰려면 ESC · 모터를 바이스에 고정하고 전류 제한 전원으로. LiPo를 쓰면 방화 백, 충전 중 자리 비우지 않기, 부푼 팩은 폐기.

- **목표**: 온사이트 latency 질문을 실제 숫자 분포로 답한다. 구간별(조종기 출력 → RX 출력 → FC 처리 → DShot)로 나눈다.
- **사전 준비**: L10 완료. K05의 latency 표와 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)의 구간 설명.
- **비용 · 시간**: L10 + ELRS 한 쌍 약 $60~120 [확인 필요] · 1일.

단계:

1. **FC 구간만 먼저 (하드웨어 최소)**: USB-UART가 CRSF로 스로틀을 계단 입력 → 같은 로직 분석기에 CRSF 선(CH0)과 DShot 선(CH1). 시간 0 = 계단이 들어간 CRSF 프레임의 마지막 바이트 끝(CRC가 확인되는 시점), 시간 끝 = DShot 스로틀 값이 바뀐 첫 프레임.
2. 1000번 반복. 스크립트가 캡처 파일을 디코드해 지연을 계산한다.
3. 히스토그램 · 평균 · p99 · 최대. rc smoothing 설정을 바꿔 다시 잰다 (smoothing이 지연에 주는 영향).
4. **RF 구간 (ELRS 있을 때)**: TX 모듈 입력 CRSF(CH2)와 RX 출력 CRSF(CH0)를 같이 캡처. packet rate(예: 250 / 500 Hz)를 바꿔 분포 변화를 본다.
5. 결과를 1쪽 리포트로: 방법, 시간 기준, 분포, 설명 안 되는 꼬리.

```text
 CRSF in (CH0)  ──────[frame n: throttle 1000→1500]──────────────────────────
 DShot  (CH1)   ─[1000]─[1000]─[1000]─────[ 1500 ]─[ 1500 ]───────────────────
                         |<──────────── latency sample ─────────▶|
 GPIO   (CH2)   ___|‾|___|‾|___|‾|___   (선택: PID 루프마다 토글 → FC 내부 쪼개기, 펌웨어 수정 필요)
```

- **예상 결과**: FC 구간 지연은 "CRSF 처리 + RX task 대기 + smoothing + PID 루프 + DShot 프레임"의 합. 숫자는 측정 전까지 모른다 [확인 필요].
- **성공 기준**: 같은 조건 두 번의 분포가 겹친다 (측정이 재현된다). 설정을 바꿨을 때 분포가 설명 가능한 방향으로 움직인다.
- **확장**: bidirectional DShot eRPM으로 "모터가 실제로 빨라지기 시작한 시점"까지 (모터 필요, 안전 문구 준수). glass-to-glass(영상)는 고속 카메라 · LED · 포토다이오드로 가이드만.

---

### L12 GitHub Actions CI (pytest + SITL)

- **목표**: 내 저장소에 push할 때마다 ① 파서 유닛 테스트 ② ArduPilot 또는 Betaflight SITL 테스트 1개가 자동으로 돌고, JUnit 결과가 남게 한다. 그다음 같은 파이프라인을 **GitLab CI yaml로 번역**한다 (JD 필수 항목).
- **사전 준비**: GitHub 계정, L6 · L4(또는 L3) 결과.
- **비용 · 시간**: 0원 (공개 저장소 무료 러너 [확인 필요: 현재 무료 한도]) · 3~5시간.

```text
# 검증 안 함 · 가이드 — .github/workflows/ci.yml
name: ci
on: [push, pull_request]
jobs:
  unit:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.11" }
      - run: pip install -r requirements.txt
      - run: pytest tests/protocols -q --junitxml=reports/unit.xml
      - uses: actions/upload-artifact@v4
        if: always()
        with: { name: unit-report, path: reports/ }
  sitl-betaflight:
    runs-on: ubuntu-latest
    needs: unit
    steps:
      - uses: actions/checkout@v4
      - run: git clone --depth 1 https://github.com/betaflight/betaflight.git bf
      - run: make -C bf TARGET=SITL EXTRA_FLAGS="-DENABLE_FLIGHT_PLANNING=1"
      - run: pytest tests/sitl -m betaflight --bf-binary bf/obj/main/betaflight_SITL.elf --junitxml=reports/sitl.xml
```

```text
# 검증 안 함 · 가이드 — 같은 일을 .gitlab-ci.yml로 (개념 번역)
stages: [unit, sitl]
unit:
  stage: unit
  image: python:3.11
  script:
    - pip install -r requirements.txt
    - pytest tests/protocols -q --junitxml=reports/unit.xml
  artifacts:
    when: always
    reports: { junit: reports/unit.xml }
sitl_betaflight:
  stage: sitl
  image: ubuntu:24.04
  script:
    - apt-get update && apt-get install -y build-essential git python3-pytest
    - git clone --depth 1 https://github.com/betaflight/betaflight.git bf
    - make -C bf TARGET=SITL EXTRA_FLAGS="-DENABLE_FLIGHT_PLANNING=1"
    - pytest tests/sitl -m betaflight --bf-binary bf/obj/main/betaflight_SITL.elf --junitxml=reports/sitl.xml
  artifacts:
    reports: { junit: reports/sitl.xml }
```

단계:

1. 유닛 job만 먼저 초록불.
2. SITL job: 빌드 캐시(`actions/cache`)로 시간을 줄인다. SITL 프로세스는 pytest fixture가 띄우고 반드시 끈다 (좀비 프로세스 금지).
3. flaky 정책: SITL 테스트는 실패 시 1회 재시도 + "재시도로 통과" 표시 ([R07](2026-10-24_R07_test_methodology_pyramid.md)).
4. GitLab 번역본을 저장소 `ci/gitlab-ci.example.yml`로 둔다. 차이(artifacts.reports.junit, image, stages)를 README에 표로.
5. (확장) HIL job은 **self-hosted runner + 장비 태그**로 표현한다: `runs-on: [self-hosted, hil-fc-f7]` / GitLab `tags: [hil-fc-f7]`. 실제 장비가 없어도 설계는 보여 줄 수 있다.

- **예상 결과**: PR 화면에 체크 2개, 아티팩트로 JUnit XML.
- **성공 기준**: 파서에 버그를 넣은 PR이 빨간불로 막힌다.
- **확장**: Bazel로 C 파서 빌드 · 테스트 (`cc_library`, `cc_test`) — JD 우대 항목 맛보기. Jenkinsfile 버전도 한 장 [확인 필요: 문법].

---

### L13 설계 문서 — "Neros식 HIL 플랫폼" (포트폴리오)

- **목표**: L1~L12에서 배운 것을 묶어, 여러 기체 · 여러 FW 스택(Betaflight 포크 + PX4 [추정])을 덮는 HIL 플랫폼 설계 문서를 쓴다. 남에게 설명할 수 있는 형태로.
- **사전 준비**: [R08](2026-10-24_R08_hil_bench_design_instruments.md), [R09](2026-10-24_R09_scalable_reusable_test_platform.md).
- **비용 · 시간**: 0원 · 4~6시간.

문서 목차 (그대로 써도 됨):

1. 문제: 무엇을, 누가, 얼마나 자주 테스트하나 (빌드마다 · 매일 밤 · 릴리스 전).
2. 요구사항: 기체 종류 추가 비용, 장비 공유, 결과 추적, 안전.
3. 구조 그림: 테스트 케이스 → 시나리오 라이브러리 → 기체 인터페이스(HAL: Betaflight=MSP+CRSF, PX4/ArduPilot=MAVLink) → 장비 드라이버(전원 · 로직 분석기 · USB-UART · 디버거) → 벤치 inventory.
4. 피라미드 배치: unit / SITL / Renode / HIL / 비행 — 각 층이 잡는 버그.
5. CI 연동: PR → unit+SITL, nightly → HIL, 릴리스 → HIL 전체 + 비행 체크리스트.
6. 결과 저장: JUnit + 측정값(JSON) + 원시 캡처, 빌드별 대시보드.
7. 안전 인터록: 프롭 금지 벤치, 전류 제한, 비상 정지, 소프트웨어 kill.
8. 단계별 출시 계획 (MVP 2주 → 3개월).
9. 위험과 모르는 것 [확인 필요] 목록.

- **성공 기준**: 처음 보는 엔지니어가 "새 기체를 추가하려면 무엇을 써야 하나"를 문서만 보고 답할 수 있다.
- **주의**: Gemini 예시의 클래스 이름 `ArcherBetaflight`, `ArcherAIPX4`는 Neros 제품명 기반이라 `[Gemini 주장 · 미확인]`이다. 문서에는 `BetaflightVehicle`, `MavlinkVehicle`처럼 중립 이름을 쓴다.

---

## 6. 하드웨어 구매 목록 (단계별)

> **사기 전에 Don과 예산 확인.** 가격은 2026-10 기준 기억에 의한 추정치이며 모두 [확인 필요]. 실제 구매 전 판매처에서 확인한다.

### 6.1 Tier 0 — 0원

| 항목 | 이미 있는 것 | 쓰는 랩 |
|---|---|---|
| Mac (Apple Silicon) | 있음 | L1 · L5 · L6 · L8 |
| Lima Linux VM | 있음 (EmbeddedLinux 노트에서 사용) | L2 · L3 · L4 · L9 |
| GitHub 계정 · Actions | 있음 [추정] | L12 |
| 시뮬레이터 (BF SITL · ArduPilot SITL · PX4 SIH · Renode) | 무료 | L3 · L4 · L5 · L9 |

### 6.2 Tier 1 — 약 $150 (책상 위 FC 벤치)

| 항목 | 예시 사양 | 대략 가격 [확인 필요] | 왜 |
|---|---|---|---|
| Betaflight FC | STM32F405 또는 F722, 30.5 mm 스택, 공식 지원 타깃 | $35~70 | L10 대상 |
| ELRS 2.4 GHz 수신기 | CRSF 출력, 작은 보드 | $15~25 | 실물 RX 출력 관찰 |
| USB-UART 3.3 V | FT232H 또는 FT232R / CP2102N (420 kbaud 확인) | $10~25 | CRSF 주입 |
| 저가 로직 분석기 | 24 MHz 8채널 (fx2lafw, sigrok 지원) | $10~20 | CRSF · DShot 캡처 (DShot600은 비트 1.67 µs라 24 MHz면 여유 있음 [추정]) |
| 점퍼선 · 납땜 소모품 · 헤더 | — | $10~20 | 배선 |
| 멀티미터 | 기본형 (이미 있을 수 있음) | $0~30 | 무전원 단락 점검 |

### 6.3 Tier 2 — 약 $500 (측정 · 링크 · 모터)

| 항목 | 예시 사양 | 대략 가격 [확인 필요] | 왜 |
|---|---|---|---|
| Tier 1 전부 | — | ~$150 | — |
| 전류 제한 벤치 전원 | 0~30 V, 0~5 A, 전류 제한 | $60~120 | 안전한 전원 · brown-out 실험 |
| ELRS TX 모듈 또는 ELRS 내장 조종기 | 2.4 GHz | $50~120 | RF 구간 latency (L11) |
| Saleae Logic 8 (또는 동급) | Python 자동화 API(`logic2-automation`) | $400~500 (Tier 2의 대부분) | 자동화 · 높은 신뢰도. 예산 부족하면 저가 분석기 + sigrok 유지 |
| ESC + 모터 1개 (작은 크기) + 고정 지그 | 4in1 ESC는 과함, 단일 ESC 권장 [추정] | $30~60 | bidirectional DShot eRPM, **프롭 없이** |
| SMA 감쇠기 세트 (선택) | 고정 감쇠 | $20~60 | 링크 마진 실험 (가이드만, 차폐 · 규정 확인) |

> 결정 팁: Saleae 대신 저가 분석기로 Tier 2를 $200대로 낮출 수 있다. 자동화가 목적이면 Saleae API가 편하지만, sigrok-cli도 스크립트로 쓸 수 있다 (`sigrok-cli` 명령 [확인 필요]).

### 6.4 안전 규칙 (모든 하드웨어 랩)

- **프롭은 벤치에서 절대 달지 않는다.** 모터 테스트도 프롭 없이.
- 처음 전원은 **전류 제한 전원**으로, 낮은 제한(예: 0.3~0.5 A [추정])에서 시작해 소비 전류를 확인한 뒤 올린다.
- 전원 넣기 전 **5 V · 3V3 · VBAT ↔ GND 단락 검사** (멀티미터).
- **LiPo**: 전용 충전기 · 밸런스 충전, 방화 백 안에서, 충전 중 자리 비우지 않기, 부풀거나 찍힌 팩은 쓰지 말고 폐기 규칙대로. 벤치 실험은 가능하면 LiPo 대신 벤치 전원.
- ESC · 모터는 바이스나 지그에 고정. 손 · 선 · 공구가 모터 근처에 없게.
- USB와 배터리를 동시에 연결할 때 접지 루프 · 역류 주의 (FC 설계마다 다름 [확인 필요]).
- RF: 안테나 없이 송신하지 않는다 (송신 모듈 손상). 전파 규정(출력 · 주파수) 지키기.
- 비상 정지: 전원 스위치를 손 닿는 곳에. 소프트웨어 kill(disarm 명령)도 테스트 코드 teardown에 항상.

---

## 7. 포트폴리오 프로젝트 — 오픈소스 "drone FW test harness"

### 7.1 목표

- "**나는 재사용 가능한 테스트 플랫폼을 만든다**"를 코드로 보여 준다. 테스트 케이스 모음이 아니라 **다른 사람이 테스트를 쉽게 추가하는 틀**.
- 0원으로 시작해 하드웨어가 생기면 같은 테스트가 실물에서도 돈다 (같은 시나리오, 다른 백엔드).

### 7.2 폴더 구조 (가칭 `drone-fw-test-harness`)

```text
drone-fw-test-harness/
├── README.md
├── pyproject.toml                 # 패키지 + pytest 설정, marker 등록
├── harness/                       # 재사용 라이브러리 (테스트가 아님)
│   ├── protocols/
│   │   ├── crsf.py                # L6 — 인코더 · 스트림 파서
│   │   ├── msp.py                 # L7 — v1/v2 클라이언트 (출발: code/msp_client.py)
│   │   └── dshot.py               # L11 — 펄스 폭 → 프레임 디코드 (출발: code/dshot.py)
│   ├── vehicles/                  # 기체 인터페이스 (HAL)
│   │   ├── base.py                # arm() disarm() set_rc() cut_link() read_state()
│   │   ├── betaflight_sitl.py     # UDP RC + MSP TCP
│   │   ├── betaflight_serial.py   # USB-UART CRSF + USB MSP (실물)
│   │   └── mavlink_vehicle.py     # ArduPilot / PX4 (pymavlink)
│   ├── instruments/               # 장비 드라이버
│   │   ├── logic_analyzer.py      # sigrok-cli 또는 Saleae API 래퍼
│   │   └── power_supply.py        # SCPI (나중에)
│   ├── scenarios/                 # 공통 시나리오 (기체와 무관)
│   │   ├── failsafe.py            # 링크 끊김 → 기대 행동
│   │   └── latency.py             # 계단 입력 → 출력 변화 시간 분포
│   └── analysis/
│       ├── step_response.py       # L8
│       └── stats.py               # 분포 · p99 · 히스토그램 텍스트
├── tests/
│   ├── protocols/                 # 순수 유닛 · fuzz (CI 항상)
│   ├── sitl/                      # marker: betaflight / ardupilot / px4
│   └── hil/                       # marker: hil (self-hosted만)
├── benches/
│   └── desk-f7.yaml               # 벤치 inventory: 포트 · 장비 · 배선 채널
├── ci/
│   ├── github-actions.yml         # 실제 사용
│   └── gitlab-ci.example.yml      # 번역본 (JD용)
└── docs/
    ├── hil_platform_design.md     # L13
    ├── labs/                      # L1~L12 노트
    └── safety.md
```

### 7.3 README 목차

1. 한 줄 소개: "Reusable test harness for drone flight-controller firmware — same scenarios run on SITL and on a desk HIL bench."
2. 왜 만들었나 (SSD 테스트 플랫폼 경험 → 드론으로) — 회사 내부 이름 없이.
3. 30초 데모: `pytest tests/sitl -m ardupilot` 출력 일부.
4. 구조 그림 (§7.2 압축본).
5. 지원 백엔드 표: Betaflight SITL / ArduPilot SITL / PX4 SIH / 실물 Betaflight FC.
6. 새 기체 추가하는 법 (`vehicles/base.py` 구현 5개 메서드).
7. 새 시나리오 추가하는 법.
8. HIL 벤치 배선 · 안전.
9. 측정 결과 예시 (latency 히스토그램, failsafe 시간).
10. 한계 · 다음 할 일.

### 7.4 이 저장소가 보여 주는 것 (JD 대응)

| JD 단어 | 저장소의 증거 |
|---|---|
| HIL test systems | `vehicles/betaflight_serial.py`, `instruments/`, `benches/`, `tests/hil/` |
| Python test automation | 전체, pytest fixture · marker · parametrize |
| UART · I2C · SPI | CRSF · MSP over UART, (확장) I2C 센서 에뮬레이션 |
| GitLab CI · Jenkins | `ci/gitlab-ci.example.yml` (+ Jenkinsfile 확장) |
| Betaflight · ExpressLRS · PX4 · ArduPilot | 백엔드 4종, ELRS 출력 CRSF 관찰 |
| Bazel (우대) | (확장) C 파서 Bazel 빌드 |
| RF 제품 테스트 (우대) | (확장) ELRS packet rate별 latency 분포 |

---

## 8. 참고 자료 (분야별)

모든 링크는 2026-10-24에 열리는 것을 확인했다 (HTTP 200 또는 페이지 제목 확인). 내용 정확성은 각 랩에서 다시 확인한다.

### 8.1 공식 문서

| 자료 | 링크 | 왜 보나 |
|---|---|---|
| Betaflight Wiki | <https://betaflight.com/docs/wiki> | 설정 · 기능 공식 설명의 출발점 |
| Betaflight Development | <https://betaflight.com/docs/development> | 빌드 · 개발 문서 모음 |
| Betaflight SITL | <https://betaflight.com/docs/development/SITL> | L3 포트 · Configurator 연결 |
| Betaflight SITL + Gazebo (Autopilot) | <https://betaflight.com/docs/development/autopilot/SITL_Autopilot_Testing_Gazebo> | 최신 SITL 빌드 플래그 · Gazebo Harmonic |
| Betaflight Failsafe | <https://betaflight.com/docs/wiki/guides/current/Failsafe> | L10 판정 기준 |
| Betaflight MSP Extensions | <https://betaflight.com/docs/development/API/MSP-Extensions> | L7 MSP 확장 |
| ExpressLRS 문서 | <https://www.expresslrs.org> | packet rate · telemetry ratio · 바인딩 |
| PX4 User/Dev Guide | <https://docs.px4.io/main/en/> | 구조 · 시뮬레이션 · 테스트 |
| PX4 macOS 개발 환경 | <https://docs.px4.io/main/en/dev_setup/dev_env_mac.html> | L5 설치 |
| PX4 SIH | <https://docs.px4.io/main/en/sim_sih/> | L5 headless 시뮬 |
| PX4 MAVSDK 통합 테스트 | <https://docs.px4.io/main/en/test_and_ci/integration_testing_mavsdk.html> | 공식 테스트 러너 구조 |
| PX4 HITL | <https://docs.px4.io/main/en/simulation/hitl.html> | SITL과 HITL 차이 |
| PX4 로그 분석 | <https://docs.px4.io/main/en/log/flight_log_analysis.html> | ULog 분석 방법 |
| ArduPilot Dev Wiki | <https://ardupilot.org/dev/> | 개발 문서 출발점 |
| ArduPilot SITL (Linux) | <https://ardupilot.org/dev/docs/setting-up-sitl-on-linux.html> | L4 실행 |
| ArduPilot SITL로 테스트하기 | <https://ardupilot.org/dev/docs/using-sitl-for-ardupilot-testing.html> | SIM_ 파라미터로 고장 주입 |
| ArduPilot autotest 프레임워크 | <https://ardupilot.org/dev/docs/the-ardupilot-autotest-framework.html> | L4 공식 테스트 실행법 |
| ArduPilot macOS 빌드 | <https://ardupilot.org/dev/docs/building-setup-mac.html> | Mac 직접 설치 |
| ArduPilot Copter 무선 failsafe | <https://ardupilot.org/copter/docs/radio-failsafe.html> | FS_THR_ENABLE 값의 뜻 |
| MAVLink 개발자 가이드 | <https://mavlink.io/en/> | 메시지 · 프레임 · 서명 |
| MAVLink Python (mavgen) | <https://mavlink.io/en/mavgen_python/> | pymavlink 사용 |
| MAVSDK-Python | <https://mavsdk.mavlink.io/main/en/python/> | L5 API |
| Renode 문서 | <https://renode.readthedocs.io/en/latest/> | L9 · Robot 테스트 |
| PlatformIO 유닛 테스트 | <https://docs.platformio.org/en/latest/advanced/unit-testing/index.html> | ELRS native 테스트 이해 |
| pytest | <https://docs.pytest.org/en/stable/> | Don의 갭 1순위 |
| Hypothesis | <https://hypothesis.readthedocs.io/> | 속성 기반 fuzz (L6 확장) |
| GitHub Actions | <https://docs.github.com/en/actions> | L12 |
| sigrok 프로토콜 디코더 목록 | <https://sigrok.org/wiki/Protocol_decoders> | UART · SPI · I2C · SBUS(`sbus_futaba`)는 있음, DShot · CRSF 전용은 없음 |
| sigrok 지원 하드웨어 | <https://sigrok.org/wiki/Supported_hardware> | 저가 분석기 고를 때 |
| Saleae Logic2 Automation | <https://saleae.github.io/logic2-automation/> | Python으로 캡처 자동화 |
| Lima | <https://lima-vm.io> | Mac에서 Linux VM |

### 8.2 소스 저장소

| 저장소 | 링크 | 왜 보나 |
|---|---|---|
| Betaflight | <https://github.com/betaflight/betaflight> | `src/main/scheduler/`, `src/main/fc/tasks.c`, `src/main/rx/crsf.c`, `src/test/unit/`, `src/test/sitl/sitl_harness.py` |
| Betaflight Configurator | <https://github.com/betaflight/betaflight-configurator> | MSP 사용 예 |
| Blackbox Log Viewer | <https://github.com/betaflight/blackbox-log-viewer> | 로그 시각화 |
| blackbox-tools | <https://github.com/betaflight/blackbox-tools> | `blackbox_decode` CSV 변환 (L8) |
| betaloop · aeroloop_gazebo | <https://github.com/betaflight/betaloop> · <https://github.com/betaflight/aeroloop_gazebo> | SITL + Gazebo 연결 |
| msp-controller | <https://github.com/cs8425/msp-controller> | SITL에 RC 넣는 예전 방법 (문서에서 링크) |
| INAV | <https://github.com/iNavFlight/inav> | Betaflight 계열 + 위치 유지 비교 |
| ExpressLRS | <https://github.com/ExpressLRS/ExpressLRS> | `src/test/` (test_crsf, test_crc, test_fhss, test_ota …), `src/lib/` |
| EdgeTX | <https://github.com/EdgeTX/edgetx> | `radio/src/pulses/` — 조종기 출력 |
| TBS CRSF 스펙 | <https://github.com/tbs-fpv/tbs-crsf-spec> | CRSF 프레임 원문 |
| CRSF WG | <https://github.com/crsf-wg/crsf> | 커뮤니티 CRSF 문서 |
| PX4-Autopilot | <https://github.com/PX4/PX4-Autopilot> | `test/mavsdk_tests/`, `src/modules/` |
| pyulog | <https://github.com/PX4/pyulog> | `ulog_info`, `ulog2csv` |
| Flight Review | <https://github.com/PX4/flight_review> | 로그 리포트 웹 구조 (대시보드 참고) |
| ArduPilot | <https://github.com/ArduPilot/ardupilot> | `Tools/autotest/` (arducopter.py, vehicle_test_suite.py) |
| pymavlink | <https://github.com/ArduPilot/pymavlink> | L4 |
| MAVProxy | <https://github.com/ArduPilot/MAVProxy> | SITL 콘솔 · 포워딩 |
| MAVSDK-Python | <https://github.com/mavlink/MAVSDK-Python> | L5 (패키지 이름 변화 주의) |
| Renode | <https://github.com/renode/renode> | L9 |
| renode-test-action | <https://github.com/antmicro/renode-test-action> | L9 CI |
| ArduPilot/renode | <https://github.com/ArduPilot/renode> | ArduPilot이 Renode를 쓰는 흔적 [확인 필요: 용도] |
| libsigrokdecode | <https://github.com/sigrokproject/libsigrokdecode> | 디코더 직접 작성 예시 |
| Saleae logic2-automation | <https://github.com/saleae/logic2-automation> | L11 자동화 |
| PlotJuggler | <https://github.com/facontidavide/PlotJuggler> | 로그 · CSV 시각화 |

### 8.3 책

| 책 | 링크 | 왜 보나 |
|---|---|---|
| Beard & McLain, *Small Unmanned Aircraft: Theory and Practice* (Princeton UP, 2012) | <https://github.com/randybeard/uavbook> (uavbook.byu.edu가 여기로 이동), 시뮬 코드 <https://github.com/byu-magicc/mavsim_public> | 좌표계 · 운동 방정식 · 센서 · 상태 추정 · 제어를 한 권으로. 저장소에 작업 중 PDF와 장별 슬라이드 링크가 있음 (이용 조건 [확인 필요]) |
| OSTEP (이미 공부 중) | — | RTOS · 스케줄링 개념의 바탕 (Don의 기존 노트) |
| 제어 교재 (예: Åström & Murray, *Feedback Systems*) | [확인 필요: 무료 배포 링크] | PID · 주파수 응답 기초 — 선택 |

### 8.4 영상

| 채널 · 시리즈 | 링크 | 왜 보나 |
|---|---|---|
| Brian Douglas (YouTube 채널) | <https://www.youtube.com/@BrianDouglas> | 제어 이론을 그림으로 — PID · 루프 셰이핑 직관 |
| MATLAB Tech Talks (Brian Douglas 진행, "Understanding PID Control", "Understanding Kalman Filters", "Drone Simulation and Control" 등) | <https://www.youtube.com/@MATLAB> | PID · Kalman · 드론 제어 시리즈 [확인 필요: 시리즈 제목 · 재생목록 URL] |
| Joshua Bardwell | <https://www.youtube.com/@JoshuaBardwell> | FPV 실물: Betaflight 설정 · ELRS · 배선 · 문제 해결 |

### 8.5 블로그 · 가이드

| 글 | 링크 | 왜 보나 |
|---|---|---|
| Oscar Liang — PID 설명 | <https://oscarliang.com/pid/> | FPV PID 직관 |
| Oscar Liang — Blackbox 사용법 | <https://oscarliang.com/blackbox/> | L8 로그 읽기 |
| Oscar Liang — Blackbox로 필터 · PID 튜닝 | <https://oscarliang.com/pid-filter-tuning-blackbox/> | 로그에서 무엇을 보는가 (판정 기준 후보) |
| Oscar Liang — RPM 필터 | <https://oscarliang.com/rpm-filter/> | bidirectional DShot · RPM 필터 |
| Oscar Liang — DShot | <https://oscarliang.com/dshot/> | DShot 개요 |
| Oscar Liang — ExpressLRS 설정 | <https://oscarliang.com/setup-expresslrs-2-4ghz/> | ELRS 플래시 · 설정 |
| Oscar Liang — ELRS vs Crossfire | <https://oscarliang.com/expresslrs/> | 링크 비교 |
| Oscar Liang — Betaflight 첫 설정 | <https://oscarliang.com/betaflight-firmware-setup/> | L10 설정 순서 |
| Oscar Liang — Betaflight 플래시 | <https://oscarliang.com/flash-update-betaflight/> | L10 펌웨어 올리기 |
| Memfault Interrupt — Renode 소개 | <https://interrupt.memfault.com/blog/intro-to-renode> | L9 Cortex-M 에뮬레이션 실전 |
| Antmicro — Renode GitHub Action | <https://antmicro.com/blog/2024/10/renode-github-action-for-automated-testing-in-simulation> | L9 CI |

### 8.6 표준 · 스펙

| 표준 | 어디 | 왜 보나 |
|---|---|---|
| CRSF | TBS 스펙 저장소 · CRSF WG | 프레임 · CRC-8 0xD5 · 타입 표 |
| MAVLink v2 | mavlink.io | 프레임 · CRC_EXTRA · 서명 |
| MSP | Betaflight 소스 `src/main/msp/`, INAV `docs/development/msp/` | 명령 번호 · payload |
| DShot | 공식 표준 문서 없음 [확인 필요] — Betaflight 소스 · 커뮤니티 글이 사실상 기준 | 16비트 프레임 · CRC |
| SBUS | 공식 공개 스펙 없음 [확인 필요] — 리버스 엔지니어링 문서 | 25바이트 · 100 kbaud 8E2 반전 |
| ULog | <https://docs.px4.io/main/en/dev_log/ulog_file_format.html> | PX4 로그 형식 |
| JUnit XML | (사실상 표준) | CI 결과 교환 |
| DO-178C · MIL-STD-810 | [확인 필요] | 방산 · 항공 SW/환경 시험 — Neros가 어떤 수준을 쓰는지는 [추정 불가] |

---

## 9. SSD 검증에서는 이랬다 → 드론에서는 이렇다 (로드맵 관점 요약)

| 항목 | SSD 검증 (Don 경험) | 드론 FTE | 이 로드맵에서 메우는 곳 |
|---|---|---|---|
| 테스트 대상 | eSSD FW + NAND · PCIe | FC FW + RC 링크 + ESC + companion | R02 · L1 |
| 주 인터페이스 | PCIe/NVMe, UART 디버그 | UART(CRSF · MSP · MAVLink), SPI(IMU), DShot | L6 · L7 · L10 |
| 시뮬레이션 | 호스트 모듈 테스트 [추정] | SITL(물리 시뮬 포함), Renode | L3 · L4 · L5 · L9 |
| 환경 스트레스 | 챔버 온도, 전원, stress | 진동 · 전압 저하 · 링크 손실 · 재밍 · 온도 | L10 · R07 |
| 판정 | 데이터 무결성 · 성능 수치 · 에러 로그 | **시간 응답**(latency · failsafe 시간), 제어 품질(step response), 안전 상태 | L8 · L11 |
| 로그 | NVMe telemetry, 디버그 FW 출력 | Blackbox · ULog · DataFlash | L8 |
| 플랫폼 | 챔버 SDK (스케줄링 · 모니터링 · UART 시퀀스) | HIL 벤치 + SITL 팜 + CI | L12 · L13 |
| 안전 | 전원 · 열 | **프롭 · 모터 · LiPo · RF** | §6.4 |
| 마지막 관문 | 신뢰성 시험 · 고객 qualification [추정] | **실제 비행 시험** · 테스트 파일럿 협업 | R07 (비행 테스트 층) |

---

## 10. 테스트 아이디어 · 체크리스트

### 10.1 로드맵 진행 체크리스트

- [ ] W1 Lima VM에 Betaflight 빌드 의존성 설치 (clang-18, libblocksruntime-dev)
- [ ] W1 SSD vs 드론 비교 1쪽 (내 말로)
- [ ] W2 L1 스케줄러 노트 + 소스 줄 인용 3곳 이상
- [ ] W3 L2 `make test` 전체 통과 로그 + 일부러 깬 실패 1개
- [ ] W3 L3 SITL 실행 + `sitl_harness.py` 시나리오 1개 통과 + 구조 그림
- [ ] W4 L6 CRSF fuzz가 심어 둔 버그를 잡음
- [ ] W4 L7 MSP로 arming disable 이유 읽기
- [ ] W5 L4 RC failsafe → RTL 테스트 통과, FS_THR_ENABLE=0에서 실패 확인
- [ ] W5 L5 PX4 SIH takeoff/land 테스트 통과
- [ ] W6 L8 step response 지표 · 회귀 비교 표
- [ ] W6 L9 Renode Robot 테스트 1개 CI 초록불
- [ ] W7 L12 GitHub Actions 2 job + GitLab 번역본
- [ ] W7 (예산 확인 후) Tier 1 하드웨어 주문
- [ ] W8 L10 실물 FC failsafe 시간 30회 분포
- [ ] W8 L11 FC 구간 latency 1000회 히스토그램
- [ ] W8 L13 설계 문서 + 저장소 v0.1 태그

### 10.2 랩에서 꼭 넣을 테스트 습관

- [ ] 모든 테스트는 **일부러 버그를 넣으면 실패하는지** 한 번 확인한다 (테스트의 테스트)
- [ ] 무작위 테스트는 시드를 출력하고 실패 입력을 파일로 저장한다
- [ ] 시간 판정은 한 번이 아니라 **분포**(평균 · p99 · 최대)로
- [ ] fixture teardown에서 항상 disarm · 프로세스 종료 · 포트 닫기
- [ ] 결과는 JUnit XML + 측정값 JSON 두 가지로 남긴다
- [ ] 판정 기준(임계값)은 숫자와 함께 **근거**를 적는다 (문서 · 측정 · 가정)
- [ ] 하드웨어 테스트 시작 전 안전 체크 (프롭 없음 · 전류 제한 · 단락 검사)

---

## 11. 용어집 (테스트 관점)

| 용어 | 뜻 | 테스트 관점 한 줄 |
|---|---|---|
| FC (Flight Controller) | IMU를 읽고 PID로 모터 명령을 만드는 MCU 보드 | HIL의 주 DUT. UART · USB · DShot 핀이 테스트 손잡이 |
| ESC (Electronic Speed Controller) | FC 명령을 받아 BLDC 모터 3상 전류를 스위칭 | DShot 입력 · eRPM 출력 · 과전류 보호 시험 |
| 4in1 ESC | ESC 4개를 한 보드에 | 한 채널 고장이 다른 채널에 영향 주는지 |
| PDB (Power Distribution Board) | 배터리 전원을 ESC · FC에 나누는 보드 | 전압 강하 · 노이즈가 FC로 넘어오는지 |
| BEC (Battery Eliminator Circuit) | 배터리 전압을 5 V · 9 V 등으로 낮추는 레귤레이터 | brown-out · 부하 변동 시 레일 안정성 |
| VTX (Video Transmitter) | 카메라 영상을 무선으로 보내는 송신기 | 출력 · 채널 설정 명령(MSP/SmartAudio) 확인, 발열 |
| OSD (On-Screen Display) | 영상 위에 배터리 · 상태 글자를 겹침 | OSD task가 실시간 루프를 방해하지 않는지 |
| IMU | 자이로 + 가속도계 센서 | SPI 읽기 주기 · 데이터 이상값 처리 |
| Gyro loop | 자이로 샘플 주기에 맞춘 처리 루프 | 주기 jitter 측정 (CLI `tasks`, GPIO 토글) |
| PID loop | 목표값과 측정값 차이로 출력 계산하는 제어 루프 | step response · 오버슈트 · 정착 시간 |
| PID | 비례 · 적분 · 미분 제어 | 계수 변경 회귀를 로그 지표로 판정 |
| Feedforward | 스틱 변화율로 출력을 미리 더하는 항 | stick 입력 대비 지연 감소 확인 |
| I-term relax | 빠른 스틱 동작 중 적분 누적을 줄이는 기능 | 급기동 후 bounce-back 크기 비교 |
| Anti-gravity | 스로틀 급변 시 I 이득을 일시적으로 올림 | 스로틀 펀치 때 자세 흔들림 |
| TPA (Throttle PID Attenuation) | 높은 스로틀에서 PID 이득 감소 | 스로틀 구간별 진동 지표 |
| Rates | 스틱 → 회전 속도 변환 곡선 | setpoint 계산 유닛 테스트 대상 |
| Mixer | 롤 · 피치 · 요 · 스로틀을 모터별 출력으로 섞음 | 모터 순서 · 방향 · 포화(saturation) 처리 |
| Airmode | 스로틀 0에서도 PID 제어 유지 | 저스로틀 자세 유지 시나리오 |
| Gyro filter (LPF) | 자이로 노이즈를 거르는 저역통과 필터 | 지연과 노이즈의 맞교환 측정 |
| Notch filter | 특정 주파수만 깎는 필터 | 공진 주파수 억제 확인 (스펙트럼) |
| Dynamic notch | 노이즈 주파수를 추적하는 notch | 추적 성능 — `dyn_notch_filter_unittest` |
| RPM filter | eRPM으로 모터 노이즈 주파수를 알고 깎는 필터 | bidirectional DShot이 없으면 꺼짐 — 의존성 테스트 |
| DShot | FC → ESC 디지털 프로토콜, 16비트(스로틀 11 + telemetry 1 + CRC 4) | 로직 분석기 디코드, CRC 오류 주입 |
| DShot300/600 | 비트레이트별 DShot | 타이밍 여유 · 케이블 길이 영향 |
| Bidirectional DShot | ESC가 같은 선으로 eRPM을 돌려줌 | eRPM 오류율 · RPM 필터 동작 |
| eRPM | 전기적 RPM (기계 RPM × 극쌍 수) | 모터 응답 시점 측정 기준 |
| PWM / OneShot / Multishot | 예전 아날로그 펄스 ESC 프로토콜 | 레거시 지원 회귀 |
| CRSF (Crossfire) | TBS가 만든 RC · 텔레메트리 시리얼 프로토콜, CRC-8 0xD5 | 파서 resync · fuzz · 주입의 주 통로 |
| ELRS (ExpressLRS) | 오픈소스 RC 링크 (ESP32/ESP8285 + Semtech LoRa/FLRC) | packet rate별 latency · LQ |
| EdgeTX | 오픈소스 조종기 펌웨어 (OpenTX 후속) | latency 첫 구간, Lua 스크립트 |
| SBUS | Futaba 계열 25바이트 RC 프로토콜, 100 kbaud 8E2 반전 | 11비트 언패킹 · failsafe 플래그 |
| FHSS | 주파수 도약 확산 | TX↔RX 동기 · 도약 표 유닛 테스트 (ELRS `test_fhss`) |
| Packet rate | RC 패킷 전송 빈도 (예: 50~1000 Hz) | latency 분포의 주 변수 |
| Telemetry ratio | 다운링크 텔레메트리에 쓰는 패킷 비율 | 업링크 지연과의 맞교환 |
| LQ (Link Quality) | 최근 패킷 수신 비율 (%) | 감쇠 · 거리별 링크 마진 기준 |
| RSSI | 수신 신호 세기 (dBm) | LQ와 함께 링크 마진 판정 |
| SNR | 신호 대 잡음비 | 감도 한계 근처 동작 |
| Bind | TX와 RX를 짝짓기 | 재부팅 · 재bind 시나리오 |
| Failsafe | 링크 끊김 시 안전 동작 (Betaflight stage 1/2, ArduPilot FS_THR) | 끊김 → 행동까지 시간 · 복구 |
| Guard time | failsafe 단계로 넘어가기 전 기다리는 시간 | 판정 임계값의 근거 |
| GPS Rescue / RTL | GPS로 홈 복귀 | SITL 시나리오 (L4 RTL) |
| Arming | 모터 출력을 허용하는 상태 전환 | arming 조건 · 차단 플래그 검증 |
| Arming disable flags | arm을 막는 이유 비트들 | MSP로 읽어 실패 이유 자동 보고 |
| Disarm | 모터 출력 중지 | teardown · 비상 정지 경로 |
| Turtle mode (Flip over after crash) | 뒤집힌 기체를 모터 역회전으로 뒤집음 | 모드 전환 · 안전 조건 |
| Modes (Angle · Horizon · Acro) | 자기 수평 유지 여부가 다른 비행 모드 | 모드 전환 회귀 |
| Blackbox | FC가 고속으로 남기는 비행 로그 (Betaflight) | 회귀 비교 · 판정 아티팩트 |
| Blackbox Explorer | blackbox 로그 뷰어 | 사람 눈 확인 · 자동화 전 탐색 |
| MSP (MultiWii Serial Protocol) | FC 설정 · 상태 조회 명령 프로토콜 | Python 테스트의 상태 읽기 손잡이 |
| CLI (Betaflight) | 시리얼 텍스트 명령 (`diff all`, `status`, `tasks`) | 설정 스냅숏 · CPU load 기록 |
| Configurator | Betaflight 설정 GUI 앱 | 수동 확인 · MSP 사용 예 |
| Target | 보드별 빌드 설정 (예: SITL, MATEKF405) | 타깃 매트릭스 빌드 CI |
| Unified target / config | 공통 바이너리 + 보드 설정 파일 [확인 필요: 현재 방식] | 설정 파일 회귀 |
| MAVLink | 드론 · GCS · companion 간 메시지 프로토콜 | pymavlink · MAVSDK로 테스트 구동 |
| GCS | 지상 관제 소프트웨어 (QGC, Mission Planner) | 사람 쪽 인터페이스 회귀 |
| QGroundControl | PX4 중심 GCS | 파라미터 · 미션 업로드 경로 |
| MAVProxy | 명령줄 GCS · 포워더 | SITL 포트 포워딩 |
| SITL | 펌웨어를 PC 프로그램으로 실행 + 물리 시뮬 | CI에서 비행 로직 테스트 |
| SIH | PX4의 내장 단순 물리 시뮬 (headless) | 빠른 CI용 |
| HITL | 실제 FC 보드 + PC 물리 시뮬 (센서 값을 시뮬이 공급) | 실보드 타이밍 + 가상 비행 |
| HIL | 실물 보드 · 장비를 묶은 자동 시험 벤치 (넓은 뜻) | FTE의 주 무대 |
| Renode | 다중 MCU · 주변장치 에뮬레이터 | 레지스터 수준 드라이버를 실물 없이 시험 |
| uORB | PX4 내부 publish/subscribe 메시지 버스 | 토픽 주기 · 지연 모니터링 |
| EKF2 | PX4 상태 추정(확장 칼만 필터) | 센서 고장 주입 시 추정 건전성 |
| ULog | PX4 로그 형식 | pyulog로 판정 자동화 |
| DataFlash | ArduPilot 로그 형식 (.bin) | MAVExplorer · pymavlink로 분석 |
| Autotest | ArduPilot `Tools/autotest` SITL 테스트 프레임워크 | 시나리오 라이브러리 설계 참고서 |
| MAVSDK | MAVLink 고수준 SDK (C++/Python) | PX4 통합 테스트 API |
| NuttX | PX4가 쓰는 POSIX 계열 RTOS | 태스크 · 스택 · 부하 모니터링 |
| ChibiOS | ArduPilot(STM32)이 쓰는 RTOS | 스레드 타이밍 · 메모리 |
| Bare-metal 스케줄러 | Betaflight처럼 RTOS 없이 협력형 스케줄러 | task 굶주림(starvation) 테스트 |
| Companion computer | Jetson 등 상위 컴퓨터 (ROS 2, 비전) | FC ↔ companion 링크 끊김 시나리오 |
| microDDS (uXRCE-DDS) | PX4 ↔ ROS 2 브리지 | 토픽 지연 · 재연결 |
| Glass-to-glass latency | 카메라 앞 사건 → 화면 표시까지 지연 | 고속 카메라 · LED 측정 |
| Step response | 계단 입력에 대한 출력 변화 | 제어 품질 오라클 (L8) |
| Jitter | 주기의 흔들림 | 분포로 판정 (p99) |
| Brown-out | 전압 저하로 MCU가 불안정해지는 현상 | 전류 제한 전원으로 재현 |
| Watchdog (IWDG) | 멈춘 MCU를 리셋 | 공중 리셋 후 복구 동작 [확인 필요: BF 동작] |
| SWD / J-Link | ARM 디버그 · 플래시 인터페이스 | 자동 플래시 · RTT 로그 |
| DFU | USB 부트로더로 플래시 | 벤치 자동 플래시 경로 |
| Logic analyzer | 디지털 신호 다채널 캡처 장비 | 공통 시간축으로 구간 지연 측정 |
| Attenuator (RF 감쇠기) | 신호 세기를 정해진 dB만큼 줄임 | 거리 · 링크 마진 모사 |
| Self-hosted runner | 내 장비에서 도는 CI 러너 | HIL 벤치를 CI에 연결 |
| JUnit XML | 테스트 결과 교환 형식 | CI 대시보드 공통 입력 |
| Flaky test | 같은 조건에서 가끔 실패하는 테스트 | 재시도 정책 · 격리 · 원인 추적 |
| Oracle | 기대값을 판정하는 기준 · 코드 | step response · pymavlink 대조 |
| LiPo | 리튬 폴리머 배터리 | 충전 · 보관 · 폐기 안전 |

(65개 이상)

---

## 12. 직접 해 볼 것 (이번 주)

1. Lima VM에 `clang-18 libblocksruntime-dev` 설치 후 Betaflight `make test_help` (L2 첫 단계, 30분).
2. GitHub 웹에서 `src/main/scheduler/scheduler.c`를 열고 `TASK_PRIORITY_REALTIME`이 어떻게 처리되는지 따라가기 (L1, 1시간).
3. `src/test/sitl/sitl_harness.py`를 처음부터 끝까지 읽고 시나리오 이름을 표로 (L3 준비, 1시간).
4. 하드웨어 Tier 1 목록을 Don이 보고 예산 결정 (§6).

---

## 13. 확인 안 된 것

- Betaflight SITL이 **macOS에서 직접 빌드되는지** — 공식 문서는 Ubuntu 기준. 그래서 Lima VM을 기본으로 했다 [확인 필요].
- `make test_rx_crsf_unittest`처럼 개별 테스트 타깃 이름 형식 — `test_%` 규칙은 확인, 실제 이름은 `make test_help`로 확인 [확인 필요].
- `sitl_harness.py`의 시나리오 목록 · 출력 형식 · 필요한 빌드 플래그 이름(`USE_FLIGHT_PLAN` vs 문서의 `ENABLE_FLIGHT_PLANNING`) 관계 [확인 필요].
- SITL TCP 5761에서 CLI 진입 가능 여부 [확인 필요].
- Lima의 UDP 포트 포워딩 동작 (SITL RC 9004를 Mac에서 보내려면) [확인 필요].
- ArduPilot `install-prereqs-mac.sh`의 Apple Silicon 동작, `sim_vehicle.py` 기본 출력 포트(14550/14551) [확인 필요].
- `FS_THR_ENABLE=1`의 정확한 의미(현재 버전에서 RTL인지) [확인 필요 — Copter radio-failsafe 문서].
- MAVSDK-Python 패키지 이름 변화(`mavsdk` → `mavsdk-grpc`)의 시점 · 호환성 [확인 필요].
- PX4 SIH 실행 시 MAVSDK 포트 14540 기본 여부 [확인 필요].
- `blackbox_decode`가 최신 Betaflight 로그 형식을 완전히 지원하는지, CSV 열 이름 [확인 필요].
- SITL에서 blackbox 기록이 되는지 [확인 필요].
- Renode Apple Silicon 네이티브 지원, `renode-test-action` 최신 태그 [확인 필요].
- 저가 USB-UART 칩(FT232R · CP2102N · CH340)의 420000 baud 정확도 [확인 필요].
- DShot · CRSF용 sigrok/Saleae 디코더 — sigrok 공식 목록에는 없음(확인). Saleae 마켓플레이스 확장 존재 여부 [확인 필요].
- Betaflight failsafe 기본 guard time · stage 값 [확인 필요].
- 하드웨어 가격 전부 [확인 필요].
- Neros가 실제로 쓰는 CI(GitLab/Jenkins), HIL 구성, Bazel 사용 범위 — 공고 문구 외 전부 [추정].
- Gemini가 말한 `Archer`, `Archer AI`, `Crossbow GCS`, `APEX` — [Gemini 주장 · 미확인].

> ⚠️ Gemini 정정: Gemini는 "SPI 센서 주입은 Linux PC가 8 kHz를 못 맞추니 보조 MCU/FPGA 에뮬레이터가 필요하다"고 했다. 방향은 맞지만, 학습 단계에서는 **SITL(물리 시뮬이 gyro 값을 공급)과 Renode로 먼저** 같은 목적을 0원으로 이룰 수 있다. 센서 에뮬레이터는 Phase 4 이후 설계 문서에서만 다룬다.

> ⚠️ Gemini 정정: Gemini는 `betaflight-log-viewer`라는 이름을 썼다. 공식 저장소 이름은 `betaflight/blackbox-log-viewer`다.

---

## 14. 참고 링크 (이 단원에서 실제로 확인한 출처)

- Betaflight `Makefile` (test · junittest · test-all 타깃): <https://github.com/betaflight/betaflight/blob/master/Makefile>
- Betaflight `src/test/Makefile` (clang-18 기본): <https://github.com/betaflight/betaflight/blob/master/src/test/Makefile>
- Betaflight CI (`ci.yml`: clang-18 · libblocksruntime-dev · test-all): <https://github.com/betaflight/betaflight/blob/master/.github/workflows/ci.yml>
- Betaflight `scheduler.h` (우선순위 enum): <https://github.com/betaflight/betaflight/blob/master/src/main/scheduler/scheduler.h>
- Betaflight `src/test/unit/`: <https://github.com/betaflight/betaflight/tree/master/src/test/unit>
- Betaflight `src/test/sitl/sitl_harness.py`: <https://github.com/betaflight/betaflight/tree/master/src/test/sitl>
- Betaflight SITL 타깃 README: <https://github.com/betaflight/betaflight/blob/master/src/platform/SIMULATOR/target/SITL/README.md>
- Betaflight SITL 문서: <https://betaflight.com/docs/development/SITL>
- Betaflight SITL + Gazebo 문서: <https://betaflight.com/docs/development/autopilot/SITL_Autopilot_Testing_Gazebo>
- ArduPilot SITL Linux: <https://ardupilot.org/dev/docs/setting-up-sitl-on-linux.html>
- ArduPilot autotest: <https://ardupilot.org/dev/docs/the-ardupilot-autotest-framework.html>
- ArduPilot `Tools/environment_install/`: <https://github.com/ArduPilot/ardupilot/tree/master/Tools/environment_install>
- ArduPilot `arducopter.py` (ThrottleFailsafe, SIM_RC_FAIL): <https://github.com/ArduPilot/ardupilot/blob/master/Tools/autotest/arducopter.py>
- ArduPilot macOS: <https://ardupilot.org/dev/docs/building-setup-mac.html>
- PX4 macOS: <https://docs.px4.io/main/en/dev_setup/dev_env_mac.html>
- PX4 SIH: <https://docs.px4.io/main/en/sim_sih/>
- PX4 MAVSDK 테스트: <https://docs.px4.io/main/en/test_and_ci/integration_testing_mavsdk.html>
- MAVSDK-Python README: <https://github.com/mavlink/MAVSDK-Python>
- pyulog README: <https://github.com/PX4/pyulog>
- ExpressLRS `src/test/`, `platformio.ini` `[env:native]`, CI `build.yml` (`pio test -e native`): <https://github.com/ExpressLRS/ExpressLRS/tree/master/src/test>
- Renode 테스트 문서: <https://renode.readthedocs.io/en/latest/introduction/testing.html>
- Renode STM32F4 예제: <https://github.com/renode/renode/blob/master/scripts/single-node/stm32f4_discovery.resc>
- sigrok 디코더 목록: <https://sigrok.org/wiki/Protocol_decoders>
- Beard & McLain 저장소: <https://github.com/randybeard/uavbook>
- K05 학습 지도: [../../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md](../../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md)
- 다른 단원 코드: [../code/crsf.py](../code/crsf.py) · [../code/msp_client.py](../code/msp_client.py) · [../code/dshot.py](../code/dshot.py) · [../code/mavlink_v2_heartbeat_codec.py](../code/mavlink_v2_heartbeat_codec.py) · [../code/step_response_oracle.py](../code/step_response_oracle.py) · [../code/bench_drivers.py](../code/bench_drivers.py) · [../code/platform_sketch.py](../code/platform_sketch.py)
- 이전 연습 코드: [../code/t1_fifo.py](../../onsiteKit/code/typealong/t1_fifo.py) · [../code/t2_packets.py](../../onsiteKit/code/typealong/t2_packets.py) · [../code/t3_retry.py](../../onsiteKit/code/typealong/t3_retry.py) · [../code/t4_window.py](../../onsiteKit/code/typealong/t4_window.py) · [../code/t5_seq.py](../../onsiteKit/code/typealong/t5_seq.py) · [../code/t6_fsm.py](../../onsiteKit/code/typealong/t6_fsm.py) · [../code/t7_struct_crc.py](../../onsiteKit/code/typealong/t7_struct_crc.py)
- 다른 단원: [R00](2026-10-24_R00_overview_and_gemini_corrections.md) · [R01](2026-10-24_R01_fte_vs_ssd_validation.md) · [R02](2026-10-24_R02_drone_system_and_open_source_map.md) · [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) · [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · [R06](2026-10-24_R06_companion_video_linux_rtos.md) · [R07](2026-10-24_R07_test_methodology_pyramid.md) · [R08](2026-10-24_R08_hil_bench_design_instruments.md) · [R09](2026-10-24_R09_scalable_reusable_test_platform.md)

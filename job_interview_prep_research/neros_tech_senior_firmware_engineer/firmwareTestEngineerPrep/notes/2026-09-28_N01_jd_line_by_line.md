# 📕 N01 · JD 한 줄씩 — 무슨 뜻이고, 내가 뭘 말할까

> Firmware Test Engineer JD의 업무 6줄, 필수 요건 7줄, 우대 사항 4줄을 차례로 본다. 줄마다 ① 실무에서 무슨 일인지 ② HM이 확인하고 싶은 것 ③ Don이 꺼낼 경험(레쥬메 근거) ④ 영어 한 문장을 정리했다. 갭은 숨기지 말고, 가까운 경험과 학습 계획으로 답한다.

## 전체 그림 먼저

이 JD를 한 문장으로 줄이면 이렇다. **"드론과 GCS 소프트웨어가 릴리즈 전에 깨지지 않게, 실제 하드웨어를 붙인 자동화 테스트(HIL)를 만들고 CI에 연결해 매 빌드를 지키는 사람."**

- 테스트 대상: "Neros drone & ground control software", 즉 flight controller FW(Betaflight 계열 [추정]), 라디오(ExpressLRS 계열 [추정]), GCS
- 수단: Python 테스트 자동화, HIL 리그, GitLab CI 또는 Jenkins
- 우대 사항에 Bazel·CMake·make와 C/C++이 있다: 테스트 엔지니어도 **FW 빌드를 직접 돌리고 코드를 읽기를** 원한다
- 우대 사항에 RF 제품 테스트가 있다: 라디오 링크 테스트(거리·감쇠·재밍)도 범위일 가능성 [추정]

| 구분 | Don이 강한 곳 | Don의 갭 |
|---|---|---|
| 필수 | HIL 구축(챔버 플랫폼, reliability system), Python 자동화, I2C/SPI/UART, Git | "software testing 5년+"라는 직함, GitLab CI/Jenkins |
| 우대 | C/C++ 7년, RF 제품(Apple RF), make | Bazel, Betaflight/ELRS/PX4 |

## Responsibilities (업무)

### 1. Automated Test Development — 드론·GCS SW 검증용 테스트 스위트 설계·개발·유지

- **실무**: 기능별 테스트 케이스를 Python으로 작성한다. arm/disarm, failsafe, 모드 전환, 텔레메트리 값 범위, 설정 저장·복원, 펌웨어 업데이트 등. 새 기능이 머지되면 그 기능의 테스트도 추가한다
- **HM이 보고 싶은 것**: 테스트 케이스를 **어떻게 도출하는지**(요구사항 → 동등 분할, 경계값, 부정 케이스, 상태 전이), 유지보수 가능하게 쓰는지
- **Don의 경험**: Apple에서 "scenario and stress-based case studies"로 MP 전에 latent defect를 찾았고, SK hynix에서 chip reliability system의 인터페이스 커버리지를 설계했다
- **영어**: "I start from the requirement and the failure modes — happy path, boundaries, negative cases, and state transitions — and then I add stress cases, because in my experience the latent defects show up under stress, not in the happy path."
- 연습: [문제 11 HIL 테스트 설계](../python/problems/11_hil_test_design.md)

### 2. Test Framework Development — 자동화를 쉽게 해 주는 프레임워크와 툴 구축·개선

- **실무**: 테스트 케이스 작성자가 하드웨어 세부를 몰라도 되게 계층을 나눈다. `tests → 키워드/헬퍼 라이브러리 → 디바이스 드라이버(시리얼, MSP, CRSF, 전원 릴레이) → transport`. fixture, 설정 파일, 로그·아티팩트 수집, 리포트
- **HM이 보고 싶은 것**: 다른 사람이 쓰는 도구를 만들어 본 적이 있는지, API 설계 감각
- **Don의 경험**: **가장 강한 카드.** SK hynix 챔버 테스트 플랫폼 SDK: 온도 제어, 테스트 시나리오 스케줄링, eSSD 상태 모니터링, UART 기반 테스트 시퀀스 자동화 API를 만들었고 **다른 엔지니어들이 그걸로 대규모 챔버를 썼다**
- **영어**: "I built and owned a test-platform SDK that other engineers used to run large-scale chamber testing — temperature control, scheduling, device status monitoring, and automated UART test sequences. So I've designed a framework where my users were other engineers."
- 연습: [N04 pytest와 프레임워크](2026-09-28_N04_pytest_framework.md), [문제 04 시리얼 DUT 드라이버](../python/problems/04_serial_dut_driver.md)

### 3. CI/CD Integration — 자동화 테스트를 CI/CD 파이프라인에 통합

- **실무**: `.gitlab-ci.yml`에 테스트 stage를 추가한다. MR마다 빌드 → 유닛 → SITL → HIL smoke, 야간에 HIL 전체 회귀·soak. 하드웨어가 붙은 self-hosted runner(tag), 리그 동시 사용 방지(`resource_group`), JUnit 리포트
- **HM이 보고 싶은 것**: CI를 **직접 써 봤는지**. 이 부분이 Don의 갭이다
- **Don의 경험**: 레쥬메에 GitLab CI/Jenkins가 없다. 가장 가까운 건 챔버 플랫폼의 **테스트 스케줄링**과 web 기반 테스트 자동화 플랫폼
- **정직한 프레이밍**: "I haven't owned a GitLab CI config end-to-end — at SK hynix the scheduling and orchestration lived in our own chamber platform. But the problems are the same: queueing jobs onto shared hardware, collecting artifacts, and making results trustworthy. I've been going through GitLab CI specifically — tagged runners for hardware, resource groups for exclusive rig access, and JUnit reports."
- 연습: [N05 CI/CD와 Git](2026-09-28_N05_ci_cd_git.md), [문제 07 CI 결과 분석](../python/problems/07_ci_results_analyzer.md)

### 4. Ensure Build Stability — 테스트 결과를 모니터링해 릴리즈 전 빌드 안정성 확보

- **실무**: 매일 아침 야간 결과를 triage한다. 실패를 **FW 버그 / 테스트 버그 / 리그·환경 문제 / flaky**로 분류하고, 회귀라면 어느 커밋에서 깨졌는지 찾아(`git bisect`) 개발자에게 넘긴다. 릴리즈 게이트
- **HM이 보고 싶은 것**: 실패를 보고 **원인까지 좁히는 능력**. 여기가 FW 개발자 출신의 강점이다
- **Don의 경험**: 7년간 FW 루트코즈. Apple에서 PCIe/I2C/SPMI/RFFE 인터페이스 실패를 신호·버스·시스템 레벨에서 좁혔다 (DSO, protocol analyzer)
- **영어**: "When a nightly run fails, the first question is whether it's the firmware, the test, or the rig. Because I've written firmware for seven years, I can go from a failing test to the log, to the bus capture, to the commit — and hand the developer a root cause instead of just a red build."

### 5. Quality Assurance — 커버리지 확보, 테스트 모범 사례 정착

- **실무**: 요구사항 대비 커버리지 매트릭스, 테스트 코드 리뷰, flaky 테스트 정책(quarantine), 테스트 이름·구조 규칙
- **HM이 보고 싶은 것**: 커버리지를 **어떻게 재는지**. 라인 커버리지가 아니라 요구사항이나 위험 기반 커버리지
- **Don의 경험**: reliability system에서 "high-speed interface testing coverage (V6/7 NAND, PCIe 3/4/5)"를 설계했다
- **영어**: "I think about coverage as risk coverage, not line coverage — which interfaces, which modes, which environmental corners. For the NAND and PCIe reliability system, coverage meant every interface generation and speed we shipped."

### 6. Documentation — 테스트 케이스·테스트 계획·결과 문서화

- **실무**: 테스트 계획(범위, 환경, 합격 기준), 케이스 설명(docstring으로부터 자동 생성 가능), 릴리즈 테스트 리포트
- **HM이 보고 싶은 것**: 문서를 귀찮아하지 않는지. 방산에서는 추적성(어떤 FW가 어떤 테스트를 통과하고 출하됐는지)이 중요하다 [추정]
- **영어**: "I like documentation that's generated from the tests themselves — docstrings and markers feeding the test plan — so it doesn't drift. For release, a report that ties the firmware hash to the exact test results."

## You should have (필수 요건)

### 5+ years of software testing with a focus on embedded systems and HIL testing

- **해석**: 직함이 "test"가 아니어도 된다. 실제 하드웨어로 embedded 테스트를 한 기간이 5년 넘는지를 본다
- **Don의 계산**: 2018-10 ~ 2021-05 챔버 테스트 플랫폼과 자동화 (약 2.5년), 2021-05 ~ 2025-11 FW 개발 중 reliability system·shmoo·health monitoring debug FW (테스트 비중 일부), 2025-12 ~ Apple factory test-node. **테스트 인프라 경험은 7년 내내 이어졌다**고 말할 수 있다
- **주의**: "5년 동안 테스트 엔지니어였다"고 과장하지 말 것. "7 years in embedded, and test infrastructure on real hardware has been part of every role"

### Hands-on experience building, setting up HIL test systems

- **해석**: 리그를 **직접 조립하고 배선하고 코드로 제어**해 봤는가
- **Don의 경험**: 챔버 시스템 + 다수의 eSSD + UART + 온도 제어를 소프트웨어로 조율한 것은 본질적으로 HIL이다. reliability system은 "designed and built"라고 레쥬메에 적혀 있다
- **영어**: "The chamber platform was HIL at scale — real drives in thermal chambers, UART to each device, and software controlling temperature and sequencing the tests."
- 공부: [N02 HIL](2026-09-28_N02_hil_for_drones.md)

### Strong development skills with a scripting language (e.g. Python) for test automation

- **해석**: 리크루터가 "Python을 물어본다"고 한 부분. 테스트 스크립트 수준이 아니라 **프레임워크를 만들 수 있는 Python**인지 본다
- **Don의 경험**: Skills에 Python, "test automation script for a web-based platform", SDK API
- 공부: [N03 Python](2026-09-28_N03_python_for_test_automation.md), 문제 01~11

### Familiarity with embedded communication protocols — I2C, SPI, UART, Ethernet

- **Don의 경험**: I2C·SPI는 FPGA 단계 SoC verification, Apple I2C 루트코즈, UART는 테스트 시퀀스 자동화, PCIe/SPMI/RFFE는 덤이다. Ethernet은 약하므로 Python socket과 UDP 정도로 준비한다
- 공부: [N06 프로토콜](2026-09-28_N06_protocols_and_drone_stack.md), [문제 05 레지스터](../python/problems/05_register_bitfields.md)

### Experience with CI/CD tools — GitLab CI, Jenkins

- **Don의 갭 (❌)**. 개념과 설정 파일을 읽을 수 있게 준비하고, 위 3번의 정직한 프레이밍을 쓴다

### Experience using Git including development workflows

- **Don의 경험**: Git/Perforce. branch → MR → review → rebase, `git bisect`를 말할 수 있게 준비한다 → [N05](2026-09-28_N05_ci_cd_git.md)

### Ability to thrive in a fast-paced work environment

- 스타트업 속도. "iterated at startup speed". SSD NPI 일정, Apple NPI→MP 일정 경험으로 답한다

## Nice to have (우대 사항)

| 항목 | Don | 한 줄 답 |
|---|---|---|
| C/C++ | ✅ 7년 production embedded C | "I can read and fix the firmware under test, not just file bugs." |
| Bazel, CMake, make | 🟡 make 수준 | "I've built multi-core firmware images with make-based flows; I haven't used Bazel yet." |
| RF 제품 테스트 | ✅ Apple RF-Hardware Chipset Integration | "At Apple I'm on RF chipset integration — RFFE bus, factory test nodes for RF parts." |
| Betaflight/ELRS/PX4/ArduPilot | ❌ | "I've been reading Betaflight's MSP/CLI and ELRS's CRSF — that's what I'd automate against first." |

## HM이 이 JD로 던질 만한 질문 Top 8

1. "Walk me through a test system you built." → 챔버 플랫폼 스토리 ([N07](2026-09-28_N07_stories_and_english.md) 스토리 A)
2. "How would you set up HIL for our flight controller?" → [N02](2026-09-28_N02_hil_for_drones.md) 모범 답안
3. "A test fails 1 in 20 runs. What do you do?" → flaky triage ([N05](2026-09-28_N05_ci_cd_git.md))
4. "How do you decide what to test?" → 위험 기반 커버리지 + 케이스 도출 기법
5. "How comfortable are you in Python? Show me." → 라이브 코딩 (문제 01·04·08)
6. "Have you used GitLab CI / Jenkins?" → 정직한 프레이밍
7. "You've been a developer — why test?" → [N07](2026-09-28_N07_stories_and_english.md)
8. "What would you do in your first 30 days?" → 현재 테스트 현황 파악 → 가장 아픈 flaky/수동 테스트 하나를 자동화 → CI에 smoke 연결

## 체크

- [ ] 업무 6줄 각각에 대해 영어 한 문장을 막힘없이 말할 수 있다
- [ ] "5년 테스트 경력"을 과장 없이 7년 경력으로 연결해 설명할 수 있다
- [ ] CI 갭을 20초 안에 정직하게 인정하고 바로 가까운 경험으로 넘어갈 수 있다
- [ ] Top 8 질문의 첫 문장을 준비했다

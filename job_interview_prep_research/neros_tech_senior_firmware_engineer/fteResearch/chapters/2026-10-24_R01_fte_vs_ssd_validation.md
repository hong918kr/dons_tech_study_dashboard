# 🔍 R01 FTE란 무엇인가 — 데이터센터 SSD 검증과 무엇이 같고 다른가

> 드론 스타트업의 Firmware Test Engineer(FTE)는 "닫힌 루프 물리 시스템"을 자동으로 판정하는 장치와 소프트웨어를 만드는 사람이다. SSD 검증의 자동화 · 플랫폼 · 스트레스 감각은 그대로 쓰이지만, **오라클(합격 기준)** · **실시간 · 제어 · RF** · **릴리스 속도**는 완전히 새로 익혀야 한다.

---

## 0. 이 단원을 읽는 법

이 단원은 연구 노트 묶음의 "기준점"이다. Don의 머릿속 모델은 데이터센터 SSD 검증이다. 그 모델을 버리지 말고, **어디가 맞고 어디가 틀리는지** 하나씩 표로 대조한다.

읽는 순서를 권한다.

1. 2절 — 용어 정리 (QA · Validation · Verification · Test Dev · Factory Test). 여기가 흐리면 나머지가 다 흐려진다.
2. 3 · 4절 — Neros 공고 문장으로 본 FTE의 일과 "고객".
3. 5절 — SSD vs 드론 큰 비교표. 이 단원의 몸통.
4. 6절 — 오라클 문제. Don에게 가장 새로운 부분.
5. 7 · 8절 — 흔한 오해와 Gemini 정정.
6. 9 ~ 11절 — 스킬 맵 · 하루 · 조직 성숙도.
7. 12절 이후 — 비교 칸 · 체크리스트 · 직접 해 볼 것.

표시 규칙은 [R00](2026-10-24_R00_overview_and_gemini_corrections.md)과 같다.

- [확인됨 공고] — `../research/job_board_snapshots/2026-10-23_neros_jobs.json` 원문에 있는 문장
- [추정] — Neros 내부 사정. 공고 밖의 내용은 전부 추정
- [Gemini 주장 · 미확인] — Gemini 대화에서 나왔지만 근거를 못 찾은 것
- [확인 필요] — 공식 문서 · 코드로 아직 확인 못 한 것

---

## 1. 왜 중요한가 (FTE 관점)

FTE라는 직함은 회사마다 뜻이 다르다. 데이터센터 SSD 회사에서 "FW validation"은 보통 **호스트가 시키는 일을 펌웨어가 정확히 했는지** 보는 일이다. 입력은 호스트가 만들고, 정답은 "쓴 데이터가 그대로 읽힌다" 처럼 분명하다.

드론에서는 입력의 절반이 **물리 세계**에서 온다. 바람, 진동, 배터리 전압 강하, 전파 방해. 출력은 모터 회전이고, 그 결과가 다시 센서 입력이 된다. 즉 **닫힌 루프(closed loop)** 다. 이 차이 하나가 테스트 설계 · 장비 · 판정 · 일정 전부를 바꾼다.

Don이 이 차이를 정확히 알아야 하는 이유는 세 가지다.

- **말이 통하려면.** Flight SW 엔지니어가 "SITL에서 통과했는데 HITL에서 진동 때문에 깨졌다"고 할 때 무엇을 뜻하는지 알아야 테스트를 같이 설계할 수 있다.
- **과대평가 · 과소평가를 피하려면.** SSD 경험 중 무엇이 그대로 쓰이는지 (테스트 플랫폼 SDK, UART 시퀀스 자동화, 스트레스 기반 결함 발굴) 알면 자신 있게 말할 수 있다. 무엇이 비어 있는지 (제어 이론, RF 링크, Betaflight, CI) 알면 학습 순서가 생긴다.
- **오라클을 설계하려면.** "합격/불합격을 무엇으로 정하나"는 FTE의 핵심 산출물이다. SSD에서 쓰던 "데이터 비교 + latency percentile" 만으로는 드론의 대부분을 판정할 수 없다.

---

## 2. 용어 정리 — 테스트 일을 부르는 여러 이름

### 2.1 다섯 가지 일

| 이름 | 질문 | 주로 보는 것 | 대표 산출물 | 드론 회사에서 흔한 위치 |
|---|---|---|---|---|
| Verification | "설계대로 만들었나?" (built it right) | 요구사항 · 스펙 대비 동작 | 요구사항별 test case, traceability matrix | FW 팀 unit/integration test, Design Assurance의 EV/DV |
| Validation | "맞는 것을 만들었나?" (built the right thing) | 실제 사용 조건에서 임무 달성 | 비행 시험 보고서, 운용자 피드백 | Flight Test, 필드 시험, 고객 시연 |
| QA (Quality Assurance) | "프로세스가 품질을 보장하나?" | 프로세스 · 리뷰 · 기준 · 결함 추적 | 테스트 정책, 결함 지표, 감사 | 소프트웨어 QA 또는 공장 Quality Engineer |
| Test Development (Test Dev / SDET) | "테스트를 자동으로, 반복 가능하게 돌릴 수 있나?" | 테스트 프레임워크 · 장비 드라이버 · CI | 프레임워크, HIL 벤치, 대시보드 | **FTE 공고의 본체** |
| Factory / Production Test | "이 한 대가 정상으로 조립됐나?" | 개별 유닛의 결함 (납땜, 부품, 캘리브레이션) | 테스트 스테이션, 시리얼별 기록, FPY | Manufacturing Test, Quality, Manufacturing Systems |

핵심 차이를 한 줄씩 적는다.

- Verification과 Validation은 **질문**이 다르다. 같은 HIL 벤치에서 둘 다 할 수 있다.
- Test Dev는 **방법**이다. 무엇을 검증할지보다 "어떻게 자동으로, 싸게, 반복 가능하게" 를 만든다.
- Factory Test는 **대상**이 다르다. 설계 결함이 아니라 제조 결함을 찾는다. 같은 테스트 코드를 재사용하더라도 목표 시간(초 단위)과 판정 기준이 다르다.

### 2.2 V-model 위에 놓아 보기

```text
 요구사항(임무: "RC 끊기면 N초 안에 안전 동작")          Validation: 비행 시험 · 필드
        \                                                   /
   시스템 설계 (FC + RX + ESC + GCS)              시스템 테스트: HIL 벤치 전체 체인
          \                                               /
     모듈 설계 (RX task, failsafe FSM)          통합 테스트: SITL / HITL
            \                                           /
       코드 (crsf.c, failsafe.c)               unit test (googletest 등)
              \_______________________________________/
                         (구현)

 Factory test는 이 V 밖에 있다: 설계가 끝난 뒤 "한 대 한 대" 를 본다.
```

> SSD에서도 같은 V가 있었다. 차이는 오른쪽 위 끝이다. SSD는 "고객사 qual 랩"이 끝이고, 드론은 "실제 비행 · 실제 전장"이 끝이다. 끝이 물리 세계일수록 그 앞 단계에서 걸러야 할 것이 많아진다.

### 2.3 Neros 공고에서 보이는 "테스트 계열" 직무들 [확인됨 공고]

Neros 공고 109개 중 테스트 · 품질과 관련된 것만 모았다. 조직 이름은 공고 문장에서 나온 것이고, 실제 보고 체계는 [추정] 이다.

| 공고 | 공고가 말하는 일 (요약) | 위치 |
|---|---|---|
| **Firmware Test Engineer** | drone · ground control 소프트웨어 test suite, test framework, CI/CD 연동, 릴리스 전 build stability | 이 단원의 주인공 |
| Manager, Autonomy Evaluation, Data & Test | autonomy release criteria, regression gate, unit → open-loop replay → closed-loop sim → HIL의 multi-tier regression, latency · deadline-miss 측정 | autonomy 쪽 테스트 조직 |
| Electrical Engineer III, Design Assurance (외 Senior · Manager) | 보드 bring-up · characterize · validate, worst-case circuit analysis, HIL setup, thermal · vibration · HALT/HASS, EV/DV, requirements traceability | 하드웨어 검증 |
| Electrical Engineer - Board Design and Test | 제조 테스트 하드웨어: bed-of-nails, fixture, ICT · functional test · programming · calibration | 공장 테스트 하드웨어 |
| Lead RF Test & Integration Engineer | RF qualification, pass/fail criteria · release gates, EVT/DVT/PVT, GR&R, factory handoff, manufacturing acceptance | RF 검증 · 양산 |
| Flight Test Operations Lead / Manager | 비행 시험 일정 · 인력 · 시설, flight test throughput | 비행 시험 운영 |
| FPV Test Pilot | 비행 시험 · qualification, 실패 발견 · 보고, Blackbox analysis 우대 | 비행 시험 실행 |
| Quality Engineer (Electronics / Electro-Mechanical) | 라인 품질, MRB, FPY · defect Pareto · escapes, SPC | 공장 품질 |
| Principal Software Engineer, Manufacturing Systems | MES, serialization, genealogy, rework | 공장 소프트웨어 |

> Flight SW, Flight SW Manager, Ground SW 공고에는 모두 "partnering with our **Test organization** on SITL/HITL and flight-test execution" 류 문장이 있다 [확인됨 공고]. 즉 **"Test organization"이 따로 있고, 그 조직이 SITL/HITL을 맡는다**. FTE는 아마 그 조직 안에 있다 [추정].

---

## 3. Neros FTE 공고 문장 분해

### 3.1 원문 [확인됨 공고, updated 2026-10-06]

> What you will be doing: **Automated Test Development** — Design, develop, and maintain test suites to validate the Neros drone & ground control software. **Test Framework Development** — Build and enhance automated testing frameworks and tools. **CI/CD Integration** — Integrate automated tests into CI/CD pipelines. **Ensure Build Stability** — Monitor the test results and ensure the stability of builds before releases. **Quality Assurance** — comprehensive test coverage, enforcing testing best practices. **Documentation** — test cases, test plans, test results.

> You should have: 5+ years of software testing with a focus on embedded systems and **HIL testing**; hands-on experience **building, setting up HIL test systems**; Python for test automation; I2C, SPI, UART, Ethernet; **Gitlab CI, Jenkins**; Git. Nice to have: C/C++; **Bazel**, CMake, make; **testing RF products**; **Betaflight, ExpressLRS, PX4, Ardupilot**.

### 3.2 문장별로 "실제로 하는 일"

| 공고 문장 | 실제로 하는 일 (풀어 쓰기) | SSD 검증에서 대응되는 것 | Don에게 새로운 점 |
|---|---|---|---|
| test suites to validate drone & ground control software | FC 펌웨어 · RX/TX 펌웨어 · 조종기/GCS 펌웨어를 겨냥한 test case 묶음 | NVMe 명령 · 전원 · 성능 test suite | 대상이 여러 보드 · 여러 펌웨어 · 여러 프로토콜 |
| test framework and tools | HIL 벤치 위 장비 드라이버, 프로토콜 클라이언트(MSP · CRSF · MAVLink), 결과 수집 | 챔버 테스트 플랫폼 SDK (Don이 만든 것) | 장비 종류가 다름 (RF 감쇠기, 로직 분석기, 전류 측정) |
| CI/CD integration | 코드 push → 빌드 → unit test → SITL → HIL 벤치 예약 → 결과 게시 | 야간 회귀를 사람이 돌리거나 스케줄러로 돌림 | GitLab CI · Jenkins 실무 경험 없음 |
| ensure build stability before releases | 릴리스 후보 빌드를 gate. 깨진 빌드 막기, flaky test 관리 | 펌웨어 릴리스 전 qual pass | 릴리스 주기가 훨씬 짧음 [추정] |
| test coverage, best practices | FW 팀이 쓰는 테스트 방식 표준화, 커버리지 지도 | 테스트 플랜 · 커버리지 매트릭스 | "다른 팀에게 테스트를 쓰게 만드는" 역할 비중 |
| documentation | test plan · case · result 문서 | 동일 | 방산 쪽 traceability 요구 가능성 [추정] |
| building HIL test systems | 실제 보드 + 계측기 + 자극기 + 소프트웨어를 하나의 벤치로 | 챔버 + 랙 + 호스트 + UART | HIL에서 "가짜 물리 세계" 를 만들어야 함 |
| I2C, SPI, UART, Ethernet | 프로토콜 레벨 디버그와 주입 | UART · PCIe · I2C/SPMI/RFFE 루트코즈 (최근 경험) | 거의 그대로 이전 |
| testing RF products | 링크 품질 · 감도 · 지연 · 간섭 시험 | 최근 RF 칩셋 통합 경험이 다리 | conducted/radiated, LQ/RSSI 지표 |
| Betaflight, ELRS, PX4, ArduPilot | 테스트 대상 오픈소스의 구조와 기존 테스트 도구 | 없음 | 전부 새로 (R03 · R04 · R05) |

### 3.3 공고에 **없는** 것도 보자

FTE 공고에는 "control algorithm 설계", "PID tuning", "RF 회로 설계", "factory fixture 하드웨어 설계"가 없다. 이런 일은 Flight SW, RF Test, Design Assurance, Board Design and Test 공고에 있다 [확인됨 공고]. 즉 FTE는 **그 사람들이 만든 것을 자동으로 판정하는 시스템** 을 만드는 쪽이다. 제어 이론은 "직접 설계할 만큼" 이 아니라 **"판정 기준을 이해하고 이상한 것을 알아볼 만큼"** 필요하다.

> ⚠️ Gemini 정정: Gemini는 FTE를 "모든 펌웨어 팀의 수문장(Gatekeeper) … 합격/불합격을 판정" 이라고 썼다. 공고 문장은 "Monitor the test results and ensure the stability of builds before releases" 까지다. FTE가 **릴리스 거부권**을 가지는지는 공고로 알 수 없다 [확인 필요]. Autonomy 쪽은 "Own the release criteria … the conditions under which a build is blocked" 가 Manager, Autonomy Evaluation 공고에 명시돼 있다. FTE 자리에서 gate를 "운영"하는지 "결정"하는지는 구분해서 말해야 한다.

---

## 4. 테스트 팀의 "고객"은 누구인가

SSD 검증에서 Don의 플랫폼 SDK는 "다른 엔지니어가 사용" 했다. 즉 내부 고객이 있었다. 드론 FTE도 똑같이 내부 고객을 가진다. 다만 고객 수가 많고 요구가 서로 다르다.

### 4.1 고객 지도

```text
                         ┌──────────────────────────┐
                         │   Test organization      │
                         │  (FTE · HIL bench · CI)  │
                         └────────────┬─────────────┘
       ┌──────────────┬───────────────┼───────────────┬──────────────┬──────────────┐
       ▼              ▼               ▼               ▼              ▼              ▼
  Flight SW      Ground SW        Platform        HW / Design    Manufacturing   Flight test
  (FC fork,      (조종기/GCS,     (runtime,       Assurance      (factory test   / Field
  PX4, modes)    link, latency)   build, SDK)     (보드, RF)     station, MES)   (pilot, 로그)
       │              │               │               │              │              │
  "SITL/HITL"   "HIL + field     "Bazel 빌드,    "HIL station,  "같은 테스트를   "비행에서만 나는
  "flight-test" test"            hermetic"       qual"          초 단위로"      버그 재현"
```

### 4.2 고객별 요구

| 고객 | 원하는 것 | FTE가 주는 것 | 공고 근거 | 갈등 지점 |
|---|---|---|---|---|
| Flight SW | 내 PR이 비행 로직을 안 깼다는 빠른 신호, SITL/HITL 결과 | PR마다 도는 SITL 시나리오, 야간 HIL, 실패 시 로그 · 블랙박스 | "partnering with our Test organization on SITL/HITL and flight-test execution" [확인됨 공고] | 테스트 시간 vs PR 속도, flaky 테스트 신뢰 |
| Ground SW | 조종기 → 링크 → 기체 control path의 latency · reliability 측정 | 구간별 latency 측정 벤치, link degrade 시나리오 | "Own latency and reliability across the operator's control path", "partnering with our test organization on hardware-in-the-loop" [확인됨 공고] | 측정 방법 합의 (어디서 어디까지가 latency인가) |
| Platform | 공통 runtime · 빌드가 모든 타깃에서 안 깨짐 | 타깃별 build + smoke test 매트릭스, 바이너리 크기 · 성능 회귀 | "fast, reproducible, hermetic builds across every target", "treating every other engineering team as your customer" [확인됨 공고] | Bazel 규칙과 테스트 러너의 경계 |
| HW / Design Assurance | 새 보드가 펌웨어와 함께 동작 | 보드 rev별 HIL smoke, 전원 · 통신 bring-up 스크립트 재사용 | "Design and build test fixtures, hardware-in-the-loop (HIL) setups, and automated test scripts" [확인됨 공고] | 누가 HIL station을 소유하나 [추정] |
| Manufacturing / Quality | 한 대씩 빠르게 합격 판정, 결과를 시리얼에 연결 | 개발용 테스트를 factory용으로 줄인 버전, 결과 포맷 | Board Design and Test (functional test · programming · calibration), Manufacturing Systems (serialization · genealogy) [확인됨 공고] | 개발 테스트(분 단위, 깊게) vs 공장(초 단위, 넓게) |
| Flight test / Field | 비행에서 나온 문제를 벤치에서 재현 | 블랙박스 · 텔레메트리 파서, 로그 → 시나리오 변환 | FPV Test Pilot "identify failures … report back", "Blackbox analysis" 우대 [확인됨 공고] | 재현 안 되는 버그, 로그 부족 |
| Autonomy (간접) | 생산 flight computer 위 latency · deadline-miss | 공통 HIL 인프라 공유 가능성 | Autonomy Evaluation 공고 "hardware-in-the-loop testing on production flight computers" [확인됨 공고] | 두 테스트 조직의 도구 중복 [추정] |

> SSD에서는 고객이 "펌웨어 팀 + 고객사 qual 팀" 정도였다. 드론에서는 **같은 벤치를 쓰려는 고객이 6~7종** 이다. 그래서 R09에서 다루는 "재사용 가능한 플랫폼" 설계가 처음부터 중요하다 ([R09](2026-10-24_R09_scalable_reusable_test_platform.md)).

---

## 5. 큰 비교 — SSD FW 검증 vs 드론 FW 테스트

### 5.1 시스템 모양부터 다르다

```text
 [SSD: open loop, host-driven]

   Test host ──NVMe cmd──▶ SSD controller FW ──▶ NAND
   (fio/vdbench)  ◀──completion + data──
        │
        └─ 판정: 데이터 비교, latency 분포, 에러 카운트, SMART/telemetry
   입력은 전부 호스트가 만든다. 같은 입력 → (거의) 같은 결과.


 [드론: closed loop, physics-driven]

        ┌─────────────── 물리 세계 (바람, 진동, 배터리, 전파) ───────────────┐
        ▼                                                                      │
   IMU/baro/GPS ──SPI/I2C/UART──▶ FC FW (gyro→filter→PID→mixer) ──DShot──▶ ESC ──▶ 모터 · 프롭
        ▲                              ▲                                          │
        │                              │ CRSF                                     │
        │                         RX ◀~~RF~~ TX ◀── 조종기 ◀── 사람(파일럿)        │
        └──────────────────────────────────────────────────────────────────────────┘
   입력의 일부는 출력의 결과다. 같은 명령 → 매번 조금씩 다른 결과.
```

이 그림에서 나오는 결론 세 가지.

- **루프를 끊어야 테스트가 된다.** HIL은 "물리 세계" 상자를 가짜로 바꾸는 일이다. 센서 입력을 시뮬레이터가 만들고, 모터 출력을 계측해서 시뮬레이터에 돌려준다. 끊는 위치에 따라 SITL · HITL · HIL이 나뉜다 ([R07](2026-10-24_R07_test_methodology_pyramid.md)).
- **사람이 루프 안에 있다.** 파일럿의 손감각("flight feel")이 품질 기준이 된다. Flight SW 공고에 "preserving flight feel that expert pilots depend on" 이 있다 [확인됨 공고].
- **보드가 여러 개다.** SSD는 컨트롤러 하나(+ NAND)가 DUT였다. 드론은 FC · RX · TX · ESC · 조종기 · GCS · companion · payload가 각자 펌웨어를 가진다 ([R02](2026-10-24_R02_drone_system_and_open_source_map.md)).

### 5.2 큰 비교표

| 차원 | 데이터센터 SSD FW 검증 | 드론 FW 테스트 (FPV/방산 스타트업) | 그대로 옮겨오는 것 | Don에게 새로운 것 |
|---|---|---|---|---|
| 시스템 성격 | open loop, 호스트 명령 → 응답 | closed loop, 센서 ↔ 제어 ↔ 액추에이터 ↔ 물리 | 상태 머신 · 에러 경로 관점 | 제어 루프, 피드백, 안정성 개념 |
| 주 인터페이스 | NVMe over PCIe (+ SMBus/I2C 관리, UART 디버그) | UART(CRSF · SBUS · MSP · MAVLink), SPI(IMU), I2C(baro · mag), DShot, USB VCP, Ethernet, RF | UART · I2C · SPI 디버그, protocol analyzer 사용 | 프로토콜 수가 많고 대부분 비동기 시리얼 + RF |
| 표준 · 스펙 | NVMe base spec, OCP Datacenter NVMe SSD spec, JEDEC JESD218/219, PCIe | 대부분 사실상 표준(de facto): CRSF, MSP, DShot, MAVLink. 오픈소스 코드가 곧 스펙 | 스펙 대 구현 차이 찾기 | "스펙 문서 대신 코드를 읽는" 습관 |
| 워크로드 | fio · vdbench 등 결정적 workload, 블록 크기 · QD · R/W 비율 | 비행 프로파일: 스틱 입력 시퀀스, 미션, 바람 · 진동, RC 끊김 | 파라미터 sweep, 매트릭스 테스트 | 입력이 시간 함수(trajectory)라는 점 |
| 판정(oracle) | 데이터 무결성 비교, latency QoS percentile, 에러 카운트 | 허용 오차 밴드, step response 지표, latency 분포, failsafe 시간, 골든 로그 비교, 통계적 합격률 | percentile · 분포로 보는 습관 | 연속 신호의 합격 기준 정하기 (6절) |
| 실시간 요구 | ms 단위 QoS (명령 완료 시간), 내부 FTL 스케줄링 | µs 단위 hard-ish deadline: gyro/PID 루프 주기 (예: 8 kHz → 125 µs) | 펌웨어 스케줄링 · 우선순위 이해 (Cortex-R/M 경험) | deadline miss가 곧 비행 품질 저하 |
| 전원 | 갑작스런 전원 차단(SPO), PLP 캐패시터 hold-up, 데이터 보존 | LiPo 전압 sag, brownout, 대전류 노이즈, 비행 중 리셋 | 전원 이벤트 주입 자동화 | 전원 이상이 "기체 추락"으로 이어짐, 전류가 훨씬 큼 |
| 수명 · 신뢰성 | endurance(TBW/DWPD), data retention, NAND wear | 진동 · 충격 · 열 · 커넥터 마모, 모터 · 프롭 소모, 비행 시간 | stress 기반 latent defect 발굴 (최근 경험) | 기계적 스트레스, 소모품 |
| 환경 | 온도 챔버, 습도, 서버 airflow | 진동, 충격(추락), EMI(모터 · ESC · VTX), 비 · 먼지, 저온 배터리, 전파 방해 | 챔버 · 온도 제어 · 스케줄링 (Don의 SDK) | EMI · 진동이 센서 신호에 직접 들어감 |
| RF | 없음 (또는 무관) | 제어 링크, 영상 링크, 텔레메트리, 항재밍, FHSS | 최근 RF 칩셋 통합 경험 | LQ · RSSI · SNR, 감쇠기, conducted vs radiated |
| DUT 종류 | 같은 SSD 모델 수백~수천 대 (랙) | 서로 다른 보드 여러 종 × 하드웨어 rev × 펌웨어 버전 | DUT 풀 관리, 상태 모니터링 | 조합 폭발 (보드 × rev × FW × 설정) |
| DUT 규모 | 대형 fleet, 장기간 병렬 실행 | 벤치 수가 적고 비쌈 [추정], 비행 시험은 하루 몇 회 | 스케줄링 · 예약 | 벤치 시간이 병목, SITL로 최대한 내리기 |
| 텔레메트리 · 로그 | NVMe telemetry log page, SMART, vendor log, UART 로그 | Blackbox(Betaflight), ULog(PX4), DataFlash(ArduPilot), CRSF/MAVLink 텔레메트리 | 로그 파서 · 디버그 기능 설계 (NVMe telemetry 경험) | 고속 시계열(kHz) 로그, 비행 재구성 |
| 결정성 | 높은 편. 같은 workload → 비슷한 결과 (NAND 상태 · GC는 변동) | 낮음. 센서 노이즈 · 타이밍 · 물리 | 반복 실행 · 통계 | flaky가 기본값이라는 전제 |
| 안전 | 데이터 손실이 최악 | 사람 부상 · 화재(LiPo) · 기체 손실이 최악 | 안전 interlock 개념 | 프롭 제거 · 전류 제한 · 비상 정지가 벤치 기본 |
| 주기 | 펌웨어 릴리스 · qual 사이클이 월 단위 | 스타트업: 주 · 일 단위 빌드, 비행 시험과 맞물림 [추정] | 회귀 자동화 | 빠른 속도에서 "충분히 좋은" 테스트를 고르는 판단 |
| 최종 판정자 | 고객사 qual 랩, 데이터센터 fleet 운영 | 비행 시험 · 파일럿 · 필드(전장) | 고객 이슈 재현 | 실제 비행이 최종 오라클 |
| 컴플라이언스 | NVMe 적합성, OCP 요구, 고객사 spec | 방산 요구 (traceability, 공급망 · 원산지, export) [추정], RF 규제 | 요구사항 매트릭스 | 국방 맥락의 문서화 · 보안 |
| 빌드 · CI | 사내 빌드 시스템 (Don은 GitLab CI/Jenkins 경험 없음) | GitLab CI · Jenkins · Bazel (공고) | 자동화 사고방식 | CI 도구 실무, Bazel |
| 보안 | 암호화, secure boot, sanitize | 링크 암호화 · 바인딩, 펌웨어 서명, 탈취 대비 [추정] | secure boot 개념 | 적대적 환경(EW) 가정 |

### 5.3 주제별로 깊게

#### 5.3.1 인터페이스: "하나의 큰 표준" vs "여러 개의 작은 사실상 표준"

SSD는 NVMe라는 큰 표준이 있다. 명령 셋, 에러 코드, 로그 페이지가 문서로 정해져 있다. 적합성 테스트(예: UNH-IOL NVMe conformance)도 있다. 테스트는 "표준대로 하는가"에서 시작한다.

드론은 다르다. CRSF는 TBS가 만든 프로토콜인데 지금 많이 쓰는 구현은 ELRS와 Betaflight 코드다. Betaflight 코드만 봐도 CRSF baud가 `416666`(Rev10 문서 기준)과 `420000` 두 값으로 정의되어 있다 (`src/main/rx/crsf_protocol.h`). 즉 **"코드가 스펙"** 이고, 버전마다 조금씩 다르다. 테스트는 "이 버전의 이 코드가 상대편 버전과 맞는가"를 봐야 한다. 상호운용성(interop) 매트릭스가 SSD보다 훨씬 중요하다.

| 항목 | SSD (NVMe) | 드론 (CRSF 예) |
|---|---|---|
| 스펙 출처 | NVM Express 공식 문서 | 제조사 문서 + 오픈소스 코드 |
| 버전 관리 | spec revision 명확 | 펌웨어 릴리스마다 미세 변화 [확인 필요] |
| 적합성 도구 | 공인 conformance 프로그램 존재 | 없음. 각 팀이 직접 만든다 |
| 테스트 포인트 | 명령별 정상 · 에러 응답 | 프레임 형식, CRC, 타이밍, 손상 프레임, 상대편 버전 호환 |

#### 5.3.2 워크로드: fio job 파일 vs 비행 시나리오

SSD에서 workload는 "4K random write, QD32, 70/30" 처럼 **몇 개 숫자** 로 정의된다. 결정적이고, 길게 돌릴수록 통계가 좋아진다.

드론에서 workload는 **시간에 따라 변하는 입력** 이다. 예를 들면 이렇다.

- 스틱 입력 시퀀스: 0 → 50% throttle 계단, roll 축 sine sweep
- 환경 프로파일: 바람 돌풍, 진동 스펙트럼, 배터리 방전 곡선
- 이벤트: 3초에 RC 링크 끊김, 5초에 복구, GPS 손실
- 미션: 이륙 → 웨이포인트 → 착륙 (PX4/ArduPilot 계열)

ArduPilot autotest는 이런 시나리오를 Python 코드로 짠다. 시뮬레이션 기체에 `self.set_rc(ch, value)` 로 RC 입력을 넣는다 (ArduPilot 공식 문서). PX4는 MAVSDK 기반 C++ 테스트를 `test/mavsdk_tests/mavsdk_test_runner.py` 로 SITL에서 돌린다 (PX4 공식 문서). 둘 다 **"시나리오 = 코드"** 다. fio job 파일보다 프로그램에 가깝다 ([R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)).

#### 5.3.3 전원: PLP vs brownout

SSD 검증에서 전원 시험의 핵심은 **sudden power-off 후 데이터가 살아 있는가** 다. PLP(power loss protection) 캐패시터가 남은 데이터를 NAND에 내리는 시간을 버티는지 본다. 판정은 "전원 복구 후 마지막으로 완료 응답한 데이터가 모두 읽히는가" 로 분명하다.

드론에서 전원 문제는 모양이 다르다.

- **전압 sag**: 스로틀을 확 올리면 LiPo 전압이 순간 떨어진다. FC · RX가 brownout 리셋되면 비행 중 제어를 잃는다.
- **노이즈**: ESC 스위칭 전류가 전원 레일에 노이즈를 얹는다. 센서 · RX가 영향을 받는다.
- **비행 중 리셋 복구**: FC가 리셋됐을 때 얼마나 빨리, 어떤 상태로 돌아오는가.
- **저전압 경고 · 동작**: 배터리 임계값에서 경고 · 착륙 같은 동작을 하는가.

판정 기준이 "데이터"가 아니라 **"제어를 유지했는가, 안전하게 행동했는가"** 로 바뀐다. 장비는 비슷하다 (프로그래머블 전원, 전류 측정, 스코프). Don의 전원 이벤트 자동화 감각은 그대로 쓰인다.

#### 5.3.4 수명 · 신뢰성: endurance vs 기계적 스트레스

SSD는 JEDEC JESD218(요구사항 · endurance 시험 방법)과 JESD219(workload)가 있다. TBW 같은 숫자로 수명을 말한다.

드론에서는 "수명"이 주로 기계 · 전기 쪽이다. 진동으로 커넥터가 풀리고, 추락 충격으로 납땜이 깨지고, 모터 베어링이 닳는다. Design Assurance 공고에 "thermal, vibration, and HALT/HASS" 가 있다 [확인됨 공고]. 펌웨어 테스트가 여기에 기여하는 방식은 **스트레스 중에 펌웨어가 이상을 감지 · 보고하는가** 를 보는 것이다. Don의 최근 경험인 "stress 기반 latent defect 발굴" 과 같은 결이다.

#### 5.3.5 환경: 챔버 vs 진동 · EMI · 전파

| 환경 요인 | SSD | 드론 | 펌웨어 테스트가 보는 것 |
|---|---|---|---|
| 온도 | 챔버 sweep, 스로틀링 | 저온 배터리 성능, 고온 FC · VTX | 센서 bias 변화, 온도 보정, 열 스로틀링 |
| 진동 | 거의 무관 | 모터 · 프롭 진동이 gyro에 직접 | 필터 설정, gyro 노이즈, 블랙박스 스펙트럼 |
| EMI | 서버 섀시 내부 | ESC · 모터 · VTX가 바로 옆 | UART/I2C 에러율, RX 감도 저하, baro 노이즈 |
| 전파 방해 | 없음 | 적의 재밍 (방산) | 링크 손실 감지, failsafe, 항재밍 동작 |
| 충격 | 낙하 시험 정도 | 추락이 일상 | 리셋 · 저장 데이터 보존 · 재무장 방지 |

#### 5.3.6 DUT fleet: 같은 것 많이 vs 다른 것 여러 개

SSD 랙에는 같은 모델이 수백 대 꽂힌다. 테스트 플랫폼의 어려움은 **규모** 다 (스케줄링, 상태 모니터링, 로그 수집). Don의 챔버 테스트 플랫폼 SDK가 정확히 이 문제를 풀었다.

드론 HIL 벤치는 보통 **종류가 많고 수량은 적다** [추정]. FC 2~3종, RX/TX, ESC, 조종기, GCS 보드. 어려움은 **조합** 이다. 보드 × 하드웨어 rev × 펌웨어 버전 × 설정(파라미터) 조합이 터진다. 그래서 R09의 "capability 기반 DUT 모델 · 벤치 inventory" 가 필요하다 ([R09](2026-10-24_R09_scalable_reusable_test_platform.md)).

#### 5.3.7 주기: 월 단위 qual vs 주 단위 비행

SSD에서는 펌웨어 하나를 고객사 qual에 넣고 몇 달 돌리는 일이 흔하다. 테스트가 길어도 된다. 대신 빠지면 안 된다.

Neros 공고는 "iterated at startup speed", "get capability into the hands of warfighters in months, not years" 라고 말한다 [확인됨 공고]. Flight SW 공고는 "Your work goes from code to bench to flight test" 라고 한다. 테스트는 **빨라야 쓸모가 있다**. PR마다 몇 분, 야간에 몇 시간, 비행 시험 전에 확인. 테스트 계층을 나누는 이유다 ([R07](2026-10-24_R07_test_methodology_pyramid.md)).

#### 5.3.8 최종 판정자: 고객사 qual 랩 vs 비행 · 전장

SSD 고객사는 자기 랩에서 자기 workload로 qual을 돌린다. 문제가 나오면 로그를 받아 재현한다.

드론의 최종 판정자는 비행 시험과 필드다. FPV Test Pilot 공고가 "~80% time field testing", "Perform high volume qualification on late-stage engineering products" 라고 한다 [확인됨 공고]. Data Platform 공고는 "recovering flight data from vehicles in the field … searchable and replayable" 이라고 한다 [확인됨 공고]. 즉 **필드 로그 → 벤치 재현 → 회귀 테스트** 루프가 조직 차원에서 만들어지고 있다. FTE는 이 루프의 "벤치 재현 → 회귀" 부분을 맡는다 [추정].

#### 5.3.9 방산 맥락

방산 드론은 소비자 FPV와 테스트 요구가 다르다. 확실한 것과 추정을 나눠 적는다.

- **항재밍 · EW**: Neros 공고에 "contested environments where GPS and communications are denied" 가 있다 (Autonomy Evaluation) [확인됨 공고]. 링크가 끊기는 것이 "예외"가 아니라 "기본 시나리오" 다.
- **traceability**: Design Assurance Manager 공고에 "requirements traceability" 가 있다 [확인됨 공고]. 펌웨어 테스트에도 요구사항 → test case → 결과 연결이 요구될 가능성이 크다 [추정].
- **serialization · genealogy**: Manufacturing Systems 공고 [확인됨 공고]. 어떤 펌웨어 버전이 어떤 시리얼에 들어갔는지 기록.
- **export · 보안**: 방산 제품은 export 규제 · 데이터 취급 규칙이 있다. Neros가 어느 규정을 어떻게 따르는지는 공고로 확인 못 함 [확인 필요].
- **공급망 요건**: 미국 국방 조달의 부품 원산지 규정 등이 테스트 장비 · 부품 선택에 영향을 줄 수 있다 [추정, 확인 필요].

---

## 6. 오라클 문제 — 무엇으로 합격/불합격을 정하나

### 6.1 오라클이란

테스트 오라클(test oracle)은 "이 출력이 맞는가"를 판정하는 기준 · 장치다. 입력을 넣는 것보다 판정하는 것이 어려운 경우가 많다. 이걸 **oracle problem** 이라고 부른다.

SSD는 오라클이 쉬운 편이었다. 드론은 어렵다. 이 차이가 FTE 업무의 핵심 난이도를 만든다.

### 6.2 SSD의 오라클 (익숙한 것)

| 오라클 | 방법 | 왜 쉬운가 |
|---|---|---|
| 데이터 무결성 | 쓴 패턴(LBA · 시드 포함)을 읽어서 비교 | 정답이 비트 단위로 정확 |
| 명령 응답 | NVMe status code가 스펙대로인가 | 스펙이 정답표 |
| latency QoS | 완료 시간 분포의 percentile (예: 99.99%) 이 기준 이하인가 | 숫자 하나로 요약 가능 |
| 에러 카운트 | UECC · CRC · timeout 수가 0 또는 기준 이하 | 셀 수 있음 |
| 상태 | SMART · telemetry 값이 범위 안인가 | 정의된 필드 |

### 6.3 드론의 오라클 (새로운 것)

드론의 출력은 대부분 **연속 신호** 다 (자세 각도, 모터 명령, 위치). 정답이 "하나의 값"이 아니라 "허용 가능한 모양" 이다.

| 오라클 종류 | 예 | 장점 | 함정 |
|---|---|---|---|
| 불변식(invariant) | "disarm 상태에서 DShot 명령은 항상 0 (또는 motor stop 값)" | 단순 · 강력 · 결정적 | 무엇이 불변인지 정하는 데 도메인 지식 필요 |
| 상태 전이 | "RC 링크 끊김 → `FAILSAFE_RX_LOSS_DETECTED` → 설정된 procedure" | FSM 로그로 판정 가능 | 상태 이름 · 전이 규칙이 버전마다 바뀔 수 있음 |
| 시간 기준 | "링크 끊김부터 failsafe 진입까지 T 이내" | 측정 가능한 숫자 | T는 설정값 + 지터. 분포로 봐야 함 |
| 허용 오차 밴드 | "목표 각도 ± X도 안에 Y초 안에 들어옴" | 연속 신호에 쓸 수 있음 | 밴드 폭이 너무 넓으면 회귀를 놓치고, 좁으면 flaky |
| step response 지표 | rise time, overshoot, settling time, steady-state error | 제어 품질을 숫자로 | 시뮬레이터 정확도에 좌우 |
| 추종 오차 | setpoint vs 실제 rate의 RMS 오차 | 한 숫자로 요약 | 큰 순간 오차를 평균이 가림 |
| latency 분포 | 구간별 지연의 p50 / p99 / max, 히스토그램 | SSD QoS 경험과 같은 사고방식 | 측정 기준점(시간 원점) 합의 필요 |
| deadline miss | gyro/PID task 주기 위반 횟수, task 실행 시간 | 실시간성의 직접 지표 | 측정 자체가 타이밍을 바꿈 (probe effect) |
| 골든 로그 비교 | 이전 정상 빌드의 블랙박스와 새 빌드 비교 | 회귀 탐지에 좋음 | 비결정성 때문에 정확 비교 불가 → 허용 오차 · 정렬 필요 |
| 통계적 합격률 | "N번 반복해서 실패 0", "합격률 ≥ p" | flaky 환경에서 정직함 | N이 커야 의미 있음 (6.5) |
| 차분(differential) 오라클 | 내 CRSF 파서 vs 레퍼런스 구현(Betaflight 코드 · pymavlink) | 정답을 남의 구현에서 빌림 | 레퍼런스도 틀릴 수 있음 |
| 사람 오라클 | 파일럿의 "느낌이 다르다" | 최종 품질에 가장 가까움 | 자동화 불가, 재현성 낮음 → 블랙박스로 뒷받침 |

> 실전 감각: 좋은 드론 테스트는 **결정적인 오라클(불변식 · 상태 전이 · 프로토콜 정합성)을 최대한 많이** 쓰고, 연속 신호 오라클(밴드 · step response)은 **회귀 비교** 용도로 쓴다. 연속 신호에 절대 기준을 거는 것은 마지막 수단이다 [추정, 일반적 실무 감각].

### 6.4 예시 1 — failsafe 테스트의 판정문

Betaflight의 failsafe 상태는 `src/main/flight/failsafe.h` 의 enum으로 정의되어 있다: `FAILSAFE_IDLE`, `FAILSAFE_RX_LOSS_DETECTED`, `FAILSAFE_LANDING`, `FAILSAFE_LANDED`, `FAILSAFE_RX_LOSS_MONITORING`, `FAILSAFE_RX_LOSS_RECOVERED`, `FAILSAFE_GPS_RESCUE`, `FAILSAFE_AUTOPILOT`. procedure는 `FAILSAFE_PROCEDURE_AUTO_LANDING`, `FAILSAFE_PROCEDURE_DROP_IT`, `FAILSAFE_PROCEDURE_GPS_RESCUE` (빌드 옵션에 따라) 이다 (master 브랜치 기준, 2026-10-24 확인).

이걸 바탕으로 판정문을 쓰면 이렇다. 숫자는 일부러 기호로 둔다 (실제 값은 설정 · 요구사항에서 온다).

```text
TC-FS-01  RC link loss → failsafe (props OFF, HIL bench)

전제: armed, throttle = 50%, failsafe_procedure = DROP_IT
자극: t0 에 CRSF 주입 중단 (FTDI 출력 끊기 또는 RF 감쇠기 최대)
관측: (a) MSP/텔레메트리 또는 디버그 로그의 failsafe 상태
      (b) 로직 분석기의 DShot 라인 (FC → ESC)
판정:
  P1  t0 + T_detect 안에 상태가 RX_LOSS_DETECTED 가 된다
  P2  그 후 DShot 명령이 motor stop 값이 된다 (DROP_IT)
  P3  P1 ~ P2 사이 DShot 명령이 throttle 50%보다 커지지 않는다   ← 불변식
  P4  링크 복구 후 recovery 조건 전에 모터가 다시 돌지 않는다     ← 안전 불변식
반복: N = 200 회, 지연은 분포로 기록 (p50 / p99 / max)
합격: P1~P4 를 N 회 모두 만족, max(지연) ≤ T_detect + margin
```

SSD의 "SPO 후 데이터 검증" 시험과 모양이 같다. 이벤트 주입 → 상태 확인 → 불변식 → 반복 → 분포. 다른 점은 관측 대상이 **데이터가 아니라 모터 명령과 상태** 라는 것이다.

### 6.5 예시 2 — step response 지표 계산 (표준 라이브러리만)

연속 신호 오라클을 숫자로 바꾸는 가장 흔한 방법이다. 아래 코드는 개념 설명용이다. 실제 데이터는 블랙박스 · SITL 로그에서 온다.

```python
# step_metrics.py — 개념 예시. Python 3.9+, 표준 라이브러리만.
from typing import List, Optional, Tuple

def step_metrics(t: List[float], y: List[float], target: float,
                 band: float = 0.05) -> Tuple[Optional[float], float, Optional[float]]:
    """t: 시간(s), y: 응답, target: 목표값(계단 크기). 시작값은 0 가정.
    반환: (rise_time 10→90%, overshoot 비율, settling_time ±band)"""
    t10 = next((ti for ti, yi in zip(t, y) if yi >= 0.1 * target), None)
    t90 = next((ti for ti, yi in zip(t, y) if yi >= 0.9 * target), None)
    rise = (t90 - t10) if (t10 is not None and t90 is not None) else None
    overshoot = max(0.0, (max(y) - target) / target)
    settle = None
    for i in range(len(y)):
        if all(abs(v - target) <= band * abs(target) for v in y[i:]):
            settle = t[i]
            break
    return rise, overshoot, settle

if __name__ == "__main__":
    # 2차 시스템 비슷한 가짜 응답 (이산 시뮬레이션)
    dt, wn, zeta = 0.001, 30.0, 0.5
    x, v, ts, ys = 0.0, 0.0, [], []
    for k in range(2000):
        a = wn * wn * (1.0 - x) - 2 * zeta * wn * v
        v += a * dt
        x += v * dt
        ts.append(k * dt)
        ys.append(x)
    r, os_, s = step_metrics(ts, ys, 1.0)
    assert r is not None and 0.0 < r < 0.2, r
    assert 0.05 < os_ < 0.3, os_        # zeta=0.5 → 이론 overshoot 약 16%
    assert s is not None and s < 1.0, s
    print(f"rise={r:.3f}s overshoot={os_*100:.1f}% settle={s:.3f}s  OK")
```

> 회귀 판정은 이 숫자들의 **이전 빌드 대비 변화** 로 거는 것이 안전하다. 예: "overshoot가 이전 기준선보다 x%p 이상 커지면 경고". 절대 기준은 Flight SW 팀과 합의해야 한다 [추정].

### 6.6 몇 번 돌려야 하나 — 통계적 합격의 최소 감각

SSD에서는 대형 fleet이 통계를 공짜로 줬다. 드론 HIL 벤치는 적어서 반복 횟수를 직접 정해야 한다.

간단한 규칙 하나: **"rule of three"**. 독립 시행 n번에서 실패가 0번이면, 실제 실패 확률의 95% 신뢰 상한은 대략 3/n 이다.

| 반복 n (실패 0) | 실패 확률 95% 상한 (≈ 3/n) | 의미 |
|---|---|---|
| 10 | ≈ 30% | 거의 아무 말도 못 함 |
| 30 | ≈ 10% | "자주 깨지진 않는다" 정도 |
| 100 | ≈ 3% | smoke test 수준 |
| 300 | ≈ 1% | 회귀 gate로 쓸 만함 |
| 3000 | ≈ 0.1% | 야간 soak 수준 |

이 표가 주는 교훈은 두 가지다.

- **짧은 PR 테스트는 "희귀한 버그"를 못 잡는다.** 희귀한 버그는 야간 · 주말 soak와 필드 로그가 잡는다.
- **flaky 테스트는 재시도로 덮지 말고 분포로 기록** 해야 한다. "10번 중 1번 실패" 는 정보다.

### 6.7 골든 로그 비교의 함정

골든 로그(이전 정상 빌드의 로그)와 비교하는 방법은 SSD의 "golden pattern compare" 와 비슷해 보이지만 다르다.

| 함정 | 이유 | 대응 |
|---|---|---|
| 시간 정렬 | 두 로그의 시작 시각 · 샘플링 위상이 다르다 | 이벤트(arm, step 시작)로 정렬 후 비교 |
| 비결정성 | 센서 노이즈 · 스케줄링 지터 | 허용 오차 밴드, 통계 지표로 비교 |
| 로그 형식 변화 | 펌웨어 버전마다 필드가 바뀔 수 있음 | 파서 버전 관리, 필드 이름 기반 비교 |
| 의도된 변화 | 튜닝 변경은 "회귀"가 아님 | 기준선 갱신 절차 (누가 승인하나) |
| 시뮬레이터 변화 | SITL 쪽 물리 모델이 바뀌면 결과가 바뀜 | 시뮬레이터 버전도 고정 · 기록 |

---

## 7. SSD 검증 엔지니어가 드론 FTE로 올 때 흔한 오해

| # | 오해 | 바로잡기 | 근거 |
|---|---|---|---|
| 1 | "테스트 = 호스트가 명령 보내고 응답 비교" | 드론은 닫힌 루프. 출력이 다음 입력을 바꾼다. 판정은 대부분 연속 신호의 모양 | 5.1절 |
| 2 | "같은 입력이면 같은 결과" | 센서 노이즈 · 지터 · 물리 때문에 매번 다르다. 통계와 허용 오차가 기본 | 6.6절 |
| 3 | "DUT = 펌웨어 하나" | FC · RX · TX · ESC · 조종기 · GCS · companion · payload. 각자 펌웨어. 경계가 버그의 주 서식지 | R02 |
| 4 | "스펙 문서를 보면 된다" | CRSF · MSP · DShot은 코드가 곧 스펙. 버전마다 다르다 (예: CRSF baud 정의가 두 개) | Betaflight `crsf_protocol.h` |
| 5 | "latency는 ms 단위 QoS" | FC 내부는 µs 단위 루프 주기. 링크는 packet rate에 따른 대기, 끝단 영상은 glass-to-glass | K05, R05 |
| 6 | "HIL = 진짜 하드웨어에서 돌리는 것" | HIL의 어려움은 "가짜 물리 세계"를 만드는 것. 센서 입력을 어떻게 속일지가 핵심 | R07, R08 |
| 7 | "SITL에서 통과했으면 된다" | SITL은 타이밍 · 진동 · EMI · 전원을 거의 모른다. 계층마다 잡는 버그가 다르다 | R07 |
| 8 | "테스트는 길고 철저할수록 좋다" | 스타트업에서는 늦은 테스트는 안 쓰인다. PR용 빠른 계층과 야간 깊은 계층을 나눈다 | 5.3.7절 |
| 9 | "비행 시험은 테스트 팀 일이 아니다" | 비행이 최종 오라클. 비행 데이터를 벤치 재현으로 바꾸는 것이 테스트 팀 몫 [추정] | FPV Test Pilot · Data Platform 공고 |
| 10 | "제어 이론을 몰라도 된다" | 설계까지는 아니어도 PID · 필터 · step response · 안정성은 판정 기준을 정하려면 필요 | 6.3절 |
| 11 | "RF는 HW 팀 영역" | 링크 손실 · 재밍은 펌웨어 failsafe 테스트의 주 자극. 감쇠기 · LQ · RSSI는 FTE 도구 | FTE 공고 "testing RF products" |
| 12 | "안전은 시험 절차서 한 줄" | 프롭 달린 모터 · LiPo는 부상 · 화재 위험. 벤치 설계에 interlock · 전류 제한 · 프롭 제거가 기본 | R08 |
| 13 | "로그는 디버그용 부산물" | 블랙박스 · ULog는 판정 데이터 자체. 파서와 비교 도구가 테스트 인프라의 일부 | R03, R04 |
| 14 | "펌웨어 버전만 고정하면 재현된다" | 파라미터(CLI `diff all` 등) · 하드웨어 rev · 시뮬레이터 버전 · 벤치 배선까지 고정 · 기록해야 재현 | R09 |
| 15 | "CI는 빌드 서버 일" | 공고 필수 요건. 테스트를 CI에 넣고, 벤치를 CI 자원으로 만드는 것이 FTE 업무 | FTE 공고 |
| 16 | "Betaflight는 수동 비행만 한다" | 2025.12 릴리스에서 GPS 기반 Altitude Hold · Position Hold가 들어왔다. 버전에 따라 기능 범위가 다르다 | Betaflight 공식 문서 |

---

## 8. Gemini 내용 중 이 단원과 관련된 정정

> ⚠️ Gemini 정정: "Betaflight … 복잡한 칼만 필터나 무거운 자율비행 코드가 없어서 '손 놓고 제자리 가만히 떠 있기(GPS Position Hold)'나 '지도 찍고 혼자 찾아가기'는 거의 못 합니다." → Betaflight 2025.12부터 **GPS 기반 Altitude Hold · Position Hold** 가 공식 기능으로 들어왔다 (빌드 옵션 `ALTITUDE_HOLD`, `POSITION_HOLD`). 웨이포인트 미션 같은 본격 자율비행은 여전히 PX4/ArduPilot/INAV 쪽이 강하다. 그리고 Betaflight 소스의 `src/main/flight/failsafe.h` 에는 `FAILSAFE_AUTOPILOT` 상태까지 있다 (master 기준). 버전에 따라 말이 달라지므로 "어느 버전의 Betaflight인가"를 항상 붙여야 한다.

> ⚠️ Gemini 정정: "Firmware Test Engineer = 모든 펌웨어 팀의 수문장(Gatekeeper) … 합격/불합격을 판정" → 공고는 "ensure the stability of builds before releases" 까지만 말한다. gate 결정권은 [확인 필요]. (3.3절)

> ⚠️ Gemini 정정: "J-Link … CPU 부하 없이 내부 상태(RTT 로그)를 초고속으로" → RTT는 타깃 RAM 버퍼에 쓰고 디버거가 읽어 가는 방식이라 오버헤드가 **작지만 0은 아니다**. µs 단위 타이밍을 측정할 때는 RTT 출력 자체가 probe effect가 될 수 있다. 측정용으로는 GPIO 토글 + 로직 분석기가 더 안전하다 [확인 필요: 구체 오버헤드 수치].

> ⚠️ Gemini 정정: "감쇠기 0dB에서 90dB까지 올리며 '드론이 20km 떨어졌을 때'" → 감쇠량과 거리의 대응은 송신 출력 · 안테나 이득 · 주파수 · 환경(path loss 모델)에 따라 달라진다. "N dB = M km" 로 단정할 수 없다. conducted 시험은 **링크 마진(감도 한계까지 남은 dB)** 을 재는 것으로 이해하는 게 정확하다 ([R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md), [R08](2026-10-24_R08_hil_bench_design_instruments.md)).

> ⚠️ Gemini 정정: "Bazel (Neros가 매우 강조하는 빌드 시스템)" → FTE 공고에서 Bazel은 **Nice to have** 이고 "Bazel, Cmake, make or another embedded build system" 으로 나열된다. Platform 공고는 "hermetic builds" 를 말하지만 Bazel을 쓴다고 단정할 근거는 공고 문장 밖이다 [확인 필요].

> Gemini가 쓴 제품 이름 — Archer, Archer AI, Crossbow GCS, APEX — 은 전부 [Gemini 주장 · 미확인]. 이 단원에서는 제품 이름 대신 "FPV 기체", "자율 기체", "GCS", "payload adapter" 로 쓴다.

> Gemini가 말한 "Neros 항재밍 펌웨어의 모태가 ELRS" 는 [Gemini 주장 · 미확인]. 공고에서 확인되는 것은 FTE · Senior FW · Ground SW 등 공고에 ExpressLRS가 언급된다는 것까지다 (K05 표).

나머지 Gemini 정정 목록은 [R00](2026-10-24_R00_overview_and_gemini_corrections.md)에 모은다.

---

## 9. 스킬 맵 — 무엇이 옮겨오고 무엇을 메워야 하나

### 9.1 바로 옮겨오는 것 (Don의 경험 범위 안에서만)

| Don의 경험 | 드론 FTE에서의 대응 | 어떻게 말할 수 있나 | 주의 |
|---|---|---|---|
| 챔버 테스트 플랫폼 SDK (온도 제어 · 스케줄링 · eSSD 상태 모니터링 · UART 테스트 시퀀스 자동화, 다른 엔지니어가 사용) | HIL 벤치 프레임워크: 장비 제어 · 벤치 예약 · DUT 상태 모니터링 · 시퀀스 자동화 | "다른 엔지니어가 쓰는 테스트 플랫폼을 만들어 본 경험" — 4절 고객 지도와 같은 문제 | 장비 종류 · 프로토콜은 다름 |
| UART 테스트 시퀀스 자동화 | CRSF · MSP · MAVLink 주입, 손상 프레임 주입 | 시리얼 자극 → 응답 판정 패턴이 같음 | 프로토콜 스펙은 새로 익힘 (R05) |
| web 기반 test automation | 테스트 결과 대시보드, 벤치 예약 UI | 결과를 사람이 보는 형태로 만드는 경험 | 공고의 InfluxDB · Django 등은 미경험 |
| health monitoring debug FW · NVMe telemetry 디버그 기능 | 블랙박스 · 텔레메트리 · 디버그 채널 설계와 해석 | "펌웨어가 스스로 상태를 보고하게 만드는 쪽" 경험 | 고속 시계열(kHz) 로그는 새로움 |
| error reporting/handling scheme | failsafe · fault reporting · 상태 전이 테스트 | 에러 경로를 설계한 사람이 에러 경로 테스트를 잘 짠다 | 드론 failsafe 의미론은 새로 |
| factory test-node 아키텍처 (최근) | factory test station, 개발 테스트 → 공장 테스트 재사용 | 공장 테스트 구조 경험이 직접 연결 | 드론 공장 규모 · MES 연동은 [추정] |
| stress 기반 latent defect 발굴 (최근) | soak · 진동 · 열 · 전원 스트레스 중 펌웨어 이상 탐지 | "스트레스로 숨은 결함을 끌어내는" 방법론 | 기계적 스트레스는 새로움 |
| PCIe/I2C/SPMI/RFFE 루트코즈, DSO · protocol analyzer (최근) | I2C · SPI · UART 버스 디버그, 로직 분석기 기반 latency 측정 | 계측기 기반 디버그가 공고 필수 요건(I2C · SPI · UART)과 일치 | DShot · CRSF 디코드는 새로 |
| RF 하드웨어 칩셋 통합 (최근) | RF 제품 테스트 (공고 우대) | RF 칩 bring-up 경험이 RF 링크 테스트의 다리 | 링크 레벨 지표 · FHSS는 새로 |
| chip reliability system (NAND, PCIe 3/4/5 커버리지) | 커버리지 매트릭스 사고 | "무엇을 덮었고 무엇이 비었나"를 표로 관리 | — |
| shmoo | 파라미터 sweep (전압 × 온도 × 설정) | margin 탐색 방법이 같음 | 대상이 제어 파라미터 · 링크 조건 |
| Cortex-R/M · Xtensa FW | STM32(Cortex-M) FC, ESP32(Xtensa 계열 포함) 기반 ELRS 하드웨어 | MCU 펌웨어 구조 · 인터럽트 · 스케줄링 이해 | ELRS 하드웨어 칩 구성은 R05에서 확인 |

### 9.2 메워야 할 것

| 빈칸 | 왜 필요한가 | 최소 목표 | 어디서 | 우선순위 |
|---|---|---|---|---|
| 제어 기초 (PID, 필터, step response, 안정성) | 오라클 설계 · 이상 탐지 | step response 지표를 계산하고 해석, PID 각 항이 무엇을 하는지 설명 | 6.5절, R03 | 높음 |
| Betaflight 내부 (scheduler · task · RX · failsafe · blackbox) | 공고 우대 + 온사이트에서 실제로 나옴 | task 우선순위 구조와 RX → PID → DShot 경로를 코드로 설명 | R03 | 높음 |
| RC 링크 (ELRS · CRSF) | latency · 패킷 · RF 테스트 | CRSF 프레임 파서 + packet rate가 latency에 주는 영향 | R05 | 높음 |
| pytest 실무 | 공고 "Python for test automation" | fixture · parametrize · marker · 결과 리포트 | R07, R09 | 높음 |
| GitLab CI / Jenkins | 공고 필수 | 빌드 → unit → SITL 파이프라인 하나를 직접 구성 | R09, R10 | 높음 |
| SITL/HITL 개념과 도구 | Flight SW 공고 "SITL/HITL" | Betaflight SITL 빌드 · 실행, ArduPilot autotest 구조 이해 | R04, R07 | 중간~높음 |
| PX4 · ArduPilot 테스트 생태계 | 공고 우대, 좋은 교재 | MAVSDK tests · autotest의 시나리오 · 판정 방식 읽기 | R04 | 중간 |
| Bazel | 공고 우대 | 작은 C 프로젝트를 Bazel로 빌드 · 테스트 | R09, R10 | 중간 |
| 비행 · FPV 감각 | 파일럿 피드백 이해 | 시뮬레이터 비행, 블랙박스 읽기 | R10 | 중간 |
| 방산 요구 (traceability 등) | 문서 · 감사 | 요구사항 → test case → 결과 연결 방식 | R09 | 낮음~중간 |

### 9.3 한 장 그림

```text
            Don의 기존 자산                               드론 FTE에 필요한 것
  ┌───────────────────────────────┐            ┌───────────────────────────────────┐
  │ 테스트 플랫폼 SDK (챔버)       │ ─────────▶ │ HIL 벤치 프레임워크 (R08, R09)     │
  │ UART 시퀀스 자동화             │ ─────────▶ │ CRSF/MSP/MAVLink 주입 (R05)        │
  │ health monitoring · telemetry │ ─────────▶ │ blackbox · ULog 판정 (R03, R04)     │
  │ error reporting scheme        │ ─────────▶ │ failsafe 테스트 (6.4절)            │
  │ factory test-node             │ ─────────▶ │ factory test station (R09)         │
  │ stress · latent defect        │ ─────────▶ │ soak · 진동 · 열 · 전원 (R07)       │
  │ DSO · protocol analyzer       │ ─────────▶ │ latency 측정 · 버스 디버그 (R08)    │
  └───────────────────────────────┘            ├───────────────────────────────────┤
                                               │ 새로: 제어 기초 · RF 링크 지표       │
                                               │ 새로: Betaflight/PX4/ArduPilot      │
                                               │ 새로: pytest · GitLab CI · Bazel    │
                                               └───────────────────────────────────┘
```

---

## 10. 드론 스타트업 FTE의 하루 · 한 주 [추정]

이 절은 전부 [추정] 이다. 공고 문장(빠른 반복, 벤치 → 비행, CI, 릴리스 전 안정성)과 오픈소스 프로젝트의 일반적 흐름에서 그린 그림이다. Neros 실제 일정과 다를 수 있다.

### 10.1 하루

| 시간대 | 일 | 산출물 |
|---|---|---|
| 아침 | 야간 회귀(SITL + HIL soak) 결과 확인. 새 실패를 분류: 진짜 회귀 / flaky / 벤치 문제 | 실패 분류, 담당 팀에 이슈 |
| 오전 | 벤치 문제 수리 (케이블 · 전원 · DUT 펌웨어 꼬임), 벤치 상태 대시보드 갱신 | 벤치 가용률 회복 |
| 오전~점심 | Flight SW · Ground SW 와 새 기능 테스트 설계 (예: 새 failsafe 동작, 새 링크 모드) | test case 초안, 오라클 합의 |
| 오후 | 프레임워크 코드: 새 장비 드라이버, 새 프로토콜 클라이언트, CI job 추가 | PR, 리뷰 |
| 오후 | 비행 시험에서 나온 로그를 받아 벤치 재현 시도 | 재현 시나리오 → 회귀 테스트 |
| 저녁 | 릴리스 후보가 있으면 gate 테스트 실행 · 결과 정리, 야간 job 예약 | 릴리스 노트용 테스트 요약 |

### 10.2 한 주

| 요일 감각 | 일 |
|---|---|
| 주 초 | 지난주 필드 · 비행 시험 이슈 정리, 이번 주 릴리스 후보 범위 확인 |
| 주 중 | 새 기능 테스트 추가, 벤치 확장 (새 보드 rev), 비행 시험 전 HIL 확인 |
| 비행 시험 날 | 테스트 빌드 확인, 로그 설정 확인, 시험 후 로그 수집 · 파싱 |
| 주 말미 | 릴리스 후보 gate, flaky 목록 정리, 다음 주 우선순위 |

### 10.3 SSD 검증과 시간 감각 비교

| 항목 | SSD 검증 (일반적) | 드론 스타트업 FTE [추정] |
|---|---|---|
| 한 테스트 사이클 | 수일~수주 (endurance · qual) | 수분 (PR) ~ 하룻밤 (soak) |
| 릴리스 | 분기 · 고객 일정 | 주 · 비행 시험 일정 |
| 피드백 출처 | 고객사 qual, fleet 이슈 | 비행 시험, 필드 |
| 가장 흔한 아침 일 | 장기 테스트 상태 확인 | 야간 실패 분류 |

---

## 11. 테스트 조직 성숙도 — Level 0 ~ Level 4

이 모델은 이 노트가 정의한 분류다 (업계 표준 모델이 아님). Neros가 어느 단계인지는 [추정] 도 하지 않는다. 공고들이 Level 2~4의 요소(CI 연동, multi-tier regression, 필드 데이터 플랫폼)를 **만들려고 하는 중** 이라는 것만 보인다.

### 11.1 단계 표

| Level | 이름 | 모습 | 판정 | 병목 | 다음 단계로 가는 열쇠 |
|---|---|---|---|---|---|
| 0 | 수동 비행 시험 | 빌드 → 파일럿이 날려 보고 "괜찮다" | 사람 느낌 | 파일럿 시간, 재현 불가 | 블랙박스 로그를 매번 남기고 보기 |
| 1 | 수동 벤치 + 로그 | 책상 위 보드에 Configurator · CLI로 확인, 블랙박스 수동 분석 | 사람 + 로그 | 사람이 반복해야 함 | 스크립트로 자극 · 관측 자동화 |
| 2 | 스크립트 HIL | Python으로 MSP · CRSF 주입, 장비 제어, 로그 자동 수집. 사람이 실행 | 자동 판정 일부 | 벤치 한 대, 실행 · 결과 관리 수동 | 테스트를 CI에 연결, unit · SITL 계층 추가 |
| 3 | CI 연동 다계층 | PR → unit → SITL, 야간 HIL. 결과 대시보드. 릴리스 gate | 자동 + 통계 | 벤치 수 · flaky · 조합 폭발 | 벤치 farm, DUT 모델, 결과 DB, 필드 로그 연결 |
| 4 | fleet 규모 자동 HIL + 필드 루프 | 벤치 farm 예약 · 병렬, 필드 텔레메트리 → 자동 재현 시나리오, 공장 테스트와 코드 공유, traceability | 자동 + 통계 + 필드 근거 | 데이터 · 비용 · 조직 간 합의 | 지속 개선 |

### 11.2 그림

```text
 Level 4  ┌──────────────────────────────────────────────────────────────┐
          │ field telemetry ─▶ 자동 재현 ─▶ 회귀 추가 ─▶ bench farm ─▶ gate │
          └──────────────────────────────────────────────────────────────┘
 Level 3  ┌────────────────────────────────────────────┐
          │ PR: unit + SITL │ nightly: HIL │ dashboard │
          └────────────────────────────────────────────┘
 Level 2  ┌──────────────────────────────┐
          │ python scripts + 1 HIL bench │
          └──────────────────────────────┘
 Level 1  ┌──────────────────────┐
          │ desk + CLI + blackbox │
          └──────────────────────┘
 Level 0  ┌──────────┐
          │ 날려 보기 │
          └──────────┘
```

### 11.3 SSD 쪽과 겹쳐 보기

| Level | SSD 검증 조직에서의 대응 (일반적) | Don의 경험과의 연결 |
|---|---|---|
| 1 | 엔지니어가 UART 콘솔로 수동 확인 | — |
| 2 | 시퀀스 스크립트 자동화 | UART 테스트 시퀀스 자동화 |
| 3 | 챔버 · 랙 스케줄링 + 결과 수집 + web | 챔버 테스트 플랫폼 SDK, web 기반 test automation |
| 4 | fleet telemetry 기반 이슈 재현 | NVMe telemetry 디버그 기능 (일부) |

> 이 표가 말해 주는 것: Don은 SSD 쪽에서 **Level 2~3 플랫폼을 만들어 본 사람** 이다. 드론에서 새로운 것은 Level 3의 "CI 도구"와 Level 4의 "필드 루프 · 조합 관리" 쪽이다 ([R09](2026-10-24_R09_scalable_reusable_test_platform.md)).

---

## 12. SSD 검증에서는 이랬다 → 드론에서는 이렇다

| 주제 | SSD 검증에서는 이랬다 | 드론에서는 이렇다 |
|---|---|---|
| DUT | 같은 모델 SSD 여러 대 | FC · RX · TX · ESC · 조종기 · GCS · companion, 각자 펌웨어 |
| 자극 | 호스트 NVMe 명령, fio/vdbench workload | CRSF · MSP · MAVLink 주입, 가짜 센서, 전원 · RF 조작 |
| 관측 | completion, 데이터, SMART, telemetry log | DShot 파형, 블랙박스, 텔레메트리, 상태 머신 |
| 판정 | 데이터 비교, status code, latency percentile | 불변식, 상태 전이, 허용 오차, step response, 분포, 통계 |
| 결정성 | 비교적 높음 | 낮음. 반복과 통계가 기본 |
| 실시간 | ms 단위 QoS | µs 단위 루프 주기, deadline miss |
| 전원 이벤트 | SPO · PLP · 데이터 보존 | sag · brownout · 비행 중 리셋 · 저전압 동작 |
| 환경 | 온도 챔버 | 진동 · EMI · 온도 · 재밍 · 충격 |
| 링크 | PCIe 링크 (안정적) | RF 링크 (끊김이 정상 시나리오) |
| 스펙 | NVMe · OCP · JEDEC 문서 | 오픈소스 코드가 사실상 스펙 |
| 로그 | telemetry log page, vendor log | Blackbox · ULog · DataFlash, kHz 시계열 |
| 주기 | 월 단위 qual | 주 · 일 단위 반복, 비행 시험 |
| 최종 판정 | 고객사 qual, fleet | 비행 시험, 필드 |
| 안전 | 데이터 손실 | 부상 · 화재 · 기체 손실 |
| 고객 | FW 팀, 고객사 | Flight SW · Ground SW · Platform · HW · 공장 · 비행 시험 |
| 플랫폼 문제 | 규모 (많은 DUT) | 조합 (다양한 DUT × rev × FW × 설정) |

---

## 13. 테스트 아이디어 · 체크리스트

### 13.1 "처음 보는 드론 테스트 대상" 을 받았을 때 묻는 질문

- [ ] 이 테스트는 Verification인가 Validation인가 Factory인가 (2절)
- [ ] 누가 이 결과를 쓰나 (고객: Flight SW / Ground SW / Platform / HW / 공장 / 비행 시험)
- [ ] 루프를 어디서 끊나 (unit / SITL / HITL / HIL / 비행)
- [ ] 자극은 무엇으로 넣나 (CRSF · MSP · MAVLink · 가짜 센서 · 전원 · RF)
- [ ] 관측은 무엇으로 하나 (DShot 파형 · 블랙박스 · 텔레메트리 · 상태 로그)
- [ ] 오라클은 결정적인가 (불변식 · 상태 전이) 연속 신호인가 (밴드 · step response)
- [ ] 반복 횟수 N과 그 N이 말해 주는 실패 확률 상한 (6.6절)
- [ ] 재현에 필요한 모든 버전을 기록하나 (FW · 파라미터 · HW rev · 시뮬레이터 · 벤치 배선)
- [ ] 안전: 프롭 제거, 전류 제한, 비상 정지, LiPo 취급
- [ ] 결과가 어디에 남나 (CI · 대시보드 · 시리얼 기록)

### 13.2 SSD 감각을 드론으로 옮기는 테스트 아이디어

| SSD에서 하던 시험 | 드론 버전 | 오라클 |
|---|---|---|
| SPO 반복 + 데이터 검증 | 비행 중(armed, 프롭 없음) FC 전원 순간 차단 반복 | 재부팅 후 자동 arm 안 됨, 설정 · 블랙박스 손상 없음 |
| 명령 timeout · abort 주입 | CRSF 프레임 중간 끊기 · 손상 CRC · 가짜 sync | 파서 resync, failsafe 오동작 없음, 상태 전이 정확 |
| 온도 sweep + 성능 측정 | 온도별 gyro 노이즈 · task 실행 시간 측정 | 기준선 대비 변화, deadline miss 0 |
| latency QoS percentile | CRSF 입력 → DShot 변화 지연 분포 | p99 · max가 기준선 대비 악화 없음 |
| endurance soak | 수 시간 armed soak (프롭 없음, 전류 제한) | 리셋 0, 메모리 · 스택 이상 없음, 로그 연속성 |
| shmoo (전압 × 온도) | 전원 전압 × packet rate × 감쇠량 sweep | 링크 유지 영역 지도, failsafe 경계 |
| telemetry 필드 검증 | 블랙박스 헤더 · 필드 · 샘플 수 검증 | 예상 주기 · 필드 존재 · 누락 없음 |
| 펌웨어 업데이트 중 전원 차단 | FC · RX 펌웨어 업데이트 중 차단 | 복구 경로 존재, brick 없음 [확인 필요: 각 펌웨어의 복구 방식] |

### 13.3 이 단원을 이해했는지 자가 점검

- [ ] Verification / Validation / QA / Test Dev / Factory Test 차이를 한 줄씩 말할 수 있다
- [ ] Neros FTE 공고의 필수 · 우대 항목을 외우지 않고 구조로 말할 수 있다
- [ ] 테스트 팀의 고객 6종과 각자 원하는 것을 말할 수 있다
- [ ] 닫힌 루프가 테스트를 왜 어렵게 하는지 그림으로 설명할 수 있다
- [ ] 드론 오라클 종류를 5개 이상 말하고, 결정적 오라클을 우선하는 이유를 설명할 수 있다
- [ ] rule of three로 "실패 0 / 100회" 가 말해 주는 것을 설명할 수 있다
- [ ] Don의 경험 중 바로 옮겨오는 것 5개와 메워야 할 것 5개를 말할 수 있다
- [ ] 성숙도 Level 0~4를 설명하고 Don이 해 본 단계를 짚을 수 있다

---

## 14. 직접 해 볼 것

하드웨어 없이 할 수 있는 것부터 적는다. 하드웨어가 필요한 것은 **사기 전에 Don과 예산 확인**.

1. **공고 문장 표 만들기 (30분)** — FTE · Flight SW · Ground SW · Platform · Autonomy Evaluation 공고에서 "test" 가 들어간 문장만 뽑아 3.2절 표 형식으로 정리한다. `../research/job_board_snapshots/2026-10-23_neros_jobs.json` 에서 grep.
2. **step response 지표 돌려 보기 (30분)** — 6.5절 코드를 `zeta` 0.3 · 0.7 · 1.0 으로 바꿔 overshoot · settling 변화를 본다. "튜닝이 바뀌면 지표가 어떻게 움직이나" 감각.
3. **rule of three 직접 확인 (20분)** — 실패 확률 p를 정하고 n번 시행을 많이 시뮬레이션해서 "0 실패" 비율이 5%가 되는 p가 3/n 근처인지 확인 (표준 라이브러리 `random`).
4. **failsafe 판정문 써 보기 (1시간)** — 6.4절 TC-FS-01을 참고해 "저전압 경고", "GPS 손실", "MSP arm 명령 거부(조건 불충족)" 세 개를 같은 형식으로 쓴다. 자극 · 관측 · 불변식 · 반복 · 합격 기준 다섯 칸을 채운다.
5. **Betaflight failsafe 코드 읽기 (1~2시간)** — `src/main/flight/failsafe.h` · `failsafe.c` 를 읽고 상태 전이 그림을 ASCII로 그린다. 각 전이를 테스트 케이스 하나로 바꾼다 ([R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md)).
6. **ArduPilot autotest 한 개 읽기 (1시간)** — `Tools/autotest/arducopter.py` 에서 failsafe 관련 테스트 하나를 골라 "자극 · 관측 · 판정" 으로 분해한다. SSD 테스트 스크립트와 무엇이 같은지 적는다 ([R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)).
7. **내 경험 → 드론 번역 연습 (30분)** — 9.1절 표의 각 행을 영어 한 문장으로 바꿔 말해 본다. 숫자 · 내부 이름은 넣지 않는다.
8. **(하드웨어, 예산 확인 후) 프롭 없는 failsafe 벤치** — FC + USB-UART로 CRSF 주입 + 로직 분석기로 DShot 관측. 모터는 연결하지 않거나 프롭을 반드시 제거하고, 전원은 전류 제한. LiPo를 쓰면 충전 · 보관 안전 규칙을 먼저 지킨다 ([R08](2026-10-24_R08_hil_bench_design_instruments.md)).

---

## 15. 확인 안 된 것

| 항목 | 상태 | 확인 방법 |
|---|---|---|
| Neros Test organization의 구성 · 보고 체계 | [추정] | 공고 밖 정보 없음. 인터뷰 · 입사 후 |
| FTE가 릴리스 gate를 결정하는지 운영하는지 | [확인 필요] | 공고 문장 이상 없음 |
| Neros HIL 벤치 수 · 구성 | [추정] | — |
| Neros 릴리스 주기 (주 · 일) | [추정] | 공고는 "startup speed" 까지만 |
| Neros가 Bazel을 실제로 쓰는지 | [확인 필요] | FTE 공고에서는 Nice to have로 나열 |
| Neros 제품 이름 (Archer 등) | [Gemini 주장 · 미확인] | 공식 자료 확인 전까지 쓰지 않음 |
| ELRS가 Neros 항재밍 펌웨어의 기반인지 | [Gemini 주장 · 미확인] | — |
| 방산 규정 (export · 공급망 · traceability) 적용 범위 | [확인 필요] | 공고에 traceability(Design Assurance)만 확인 |
| CRSF 스펙 버전별 차이 범위 | [확인 필요] | R05에서 ELRS · Betaflight 코드 대조 |
| J-Link RTT 오버헤드 수치 | [확인 필요] | SEGGER 문서 |
| Betaflight failsafe enum의 버전별 변화 | [확인 필요] | master 기준만 확인 (2026-10-24). 릴리스 태그별 비교 필요 |
| OCP Datacenter NVMe SSD spec의 latency 요구 구체 값 | [확인 필요] | 이 단원에서는 숫자를 쓰지 않음 |
| JESD218 클래스별 retention · UBER 값 | [확인 필요] | 이 단원에서는 숫자를 쓰지 않음 |

---

## 16. 참고 링크

공고 · 내부 자료

- Neros 공고 스냅샷: `../research/job_board_snapshots/2026-10-23_neros_jobs.json` (Firmware Test Engineer updated 2026-10-06, Firmware Engineer - Flight Software, Senior Firmware Engineer - Ground Software, Senior Firmware Engineer - Platform, Lead Platform Software, Manager Autonomy Evaluation Data & Test, Design Assurance, Lead RF Test & Integration, FPV Test Pilot, Senior Data Platform Engineer, Manufacturing Systems)
- K05 오픈소스 지도: `../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md`
- Gemini 대화 (출발 자료, 검증 대상): `/Users/donh/Downloads/Neros 오픈소스 및 테스트 분석_prompt_gemini.pdf`

오픈소스 · 공식 문서 (2026-10-24 확인)

- [Betaflight GitHub](https://github.com/betaflight/betaflight) — `src/main/scheduler/scheduler.c`, `src/main/fc/tasks.c` (GYRO · FILTER · PID = `TASK_PRIORITY_REALTIME`, RX = `TASK_PRIORITY_HIGH`), `src/main/rx/crsf.c`, `src/main/blackbox/blackbox.c`, `src/test/` (`unit/`, `sitl/`, `Makefile`)
- [Betaflight `crsf_protocol.h`](https://github.com/betaflight/betaflight/blob/master/src/main/rx/crsf_protocol.h) — `CRSF_BAUDRATE` 416666 / 420000, `CRSF_SYNC_BYTE` 0xC8, `CRSF_FRAME_SIZE_MAX` 64
- [Betaflight `failsafe.h`](https://github.com/betaflight/betaflight/blob/master/src/main/flight/failsafe.h) — failsafe 상태 · procedure enum
- [Betaflight SITL 문서](https://betaflight.com/docs/development/SITL) — `make TARGET=SITL`, Gazebo UDP 9002/9003
- [Betaflight 2025.12 Position Hold 가이드](https://betaflight.com/docs/wiki/guides/current/Position-Hold-2025-12)
- [Betaflight 2025.12 Release Notes](https://betaflight.com/docs/wiki/release/Betaflight-2025-12-Release-Notes)
- [ArduPilot AutoTest Framework](https://ardupilot.org/dev/docs/the-ardupilot-autotest-framework.html) — `./Tools/autotest/autotest.py build.Copter test.Copter`, `self.set_rc(ch, value)`
- [PX4 MAVSDK Integration Testing](https://docs.px4.io/main/en/test_and_ci/integration_testing_mavsdk.html) — `test/mavsdk_tests/mavsdk_test_runner.py`, Catch2

SSD 쪽 표준 (개념 참고, 숫자 미인용)

- [JEDEC JESD218 (SSD Requirements and Endurance Test Method)](https://www.jedec.org/node/2854)
- [JEDEC JESD219 (SSD Endurance Workloads)](https://www.jedec.org/node/5866)
- [SNIA SDC25 OCP Storage Project Update (OCP Datacenter NVMe SSD spec 버전 흐름)](https://www.snia.org/sites/default/files/2025-09/SNIA-SDC25-Stenfort-OCP-Storage-Project-Update.pdf)

다른 단원

- [R00 개요 · Gemini 정정](2026-10-24_R00_overview_and_gemini_corrections.md)
- [R02 드론 시스템 · 오픈소스 배치도](2026-10-24_R02_drone_system_and_open_source_map.md)
- [R03 Betaflight / INAV 내부와 테스트](2026-10-24_R03_betaflight_inav_internals_and_testing.md)
- [R04 PX4 / ArduPilot 테스트 생태계](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)
- [R05 RC 링크 프로토콜](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)
- [R06 Companion · 영상 · Linux · RTOS](2026-10-24_R06_companion_video_linux_rtos.md)
- [R07 테스트 방법론 피라미드](2026-10-24_R07_test_methodology_pyramid.md)
- [R08 HIL 벤치 설계](2026-10-24_R08_hil_bench_design_instruments.md)
- [R09 스케일러블 테스트 플랫폼](2026-10-24_R09_scalable_reusable_test_platform.md)
- [R10 로드맵 · 랩 · 참고 자료](2026-10-24_R10_roadmap_labs_references.md)

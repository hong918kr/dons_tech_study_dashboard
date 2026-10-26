# 🔺 R07 테스트 방법론 — 피라미드(static→unit→SITL→HITL→HIL→비행→공장→필드)와 기법 카탈로그

> 드론 FW 테스트는 10층 피라미드다. 아래층은 싸고 빠르고 매 커밋마다, 위층은 비싸고 느리지만 "진짜"다. 각 층이 무엇을 잡고 무엇을 못 잡는지, CI 어디에 거는지, 그리고 층을 가로지르는 기법(고장 주입 · 지연 분포 · 제어 품질 오라클 · 통계적 판정)과 문서 템플릿을 정리한다.

---

## 0. 이 단원을 읽는 법

- 1~2절: 왜 피라미드인가, 그리고 SITL / HITL / HIL 용어 정리 (Gemini가 섞어 쓴 부분 정정)
- 3절: 피라미드 10층을 한 층씩 — 잡는 것 · 못 잡는 것 · 비용 · CI 위치 · 오픈소스 예
- 4절: CI 게이트 배치 (per-commit · nightly · release)
- 5~6절: 기법 카탈로그 (큰 표 + 기법별 상세)
- 7절: 제어 시스템 테스트 오라클 + 예제 코드 [`step_response_oracle.py`](../code/step_response_oracle.py)
- 8절: 통계적 판정과 flaky 테스트
- 9절: 문서 템플릿 (테스트 계획 · RTM · 테스트 케이스 · 결함 보고 · 릴리스 체크리스트)
- 10절: 방산 맥락 (추적성 · 형상 관리 · 재현성 · 표준은 참고만)
- 11절: SSD 검증 → 드론 비교
- 12~15절: 체크리스트 · 직접 해 볼 것 · 확인 안 된 것 · 참고 링크

관련 단원: 개요 [R00](2026-10-24_R00_overview_and_gemini_corrections.md) · FTE vs SSD [R01](2026-10-24_R01_fte_vs_ssd_validation.md) · 시스템 지도 [R02](2026-10-24_R02_drone_system_and_open_source_map.md) · Betaflight [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · PX4/ArduPilot [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) · 링크 프로토콜 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · Companion/Linux [R06](2026-10-24_R06_companion_video_linux_rtos.md) · HIL 벤치 [R08](2026-10-24_R08_hil_bench_design_instruments.md) · 플랫폼 [R09](2026-10-24_R09_scalable_reusable_test_platform.md) · 로드맵 [R10](2026-10-24_R10_roadmap_labs_references.md)

---

## 1. 왜 중요한가 (FTE 관점)

FW Test Engineer의 일은 "테스트를 많이 돌리는 것"이 아니다. **어떤 결함을 어느 층에서 가장 싸게 잡을지 정하고, 그 층을 자동으로 돌게 만드는 것**이다.

드론은 SSD보다 이 결정이 더 중요하다. 이유는 세 가지다.

1. **위층이 매우 비싸고 위험하다.** 비행 시험 한 번은 배터리 · 조종사 · 장소 · 날씨 · 기체 파손 위험을 쓴다. SSD 챔버는 밤새 무인으로 돌지만, 비행은 사람이 붙어야 한다.
2. **물리 세계가 루프 안에 있다.** 제어 루프의 "정답"은 하나의 값이 아니라 응답 모양(rise time · overshoot · 노이즈)이다. 판정 기준(오라클)을 만드는 것 자체가 일이다.
3. **안전 결함은 되돌릴 수 없다.** 모터가 부팅 직후 도는 버그, failsafe가 늦는 버그는 사람을 다치게 한다. 그래서 failsafe · arming 같은 안전 경로는 **여러 층에서 중복으로** 테스트한다.

> FW Test JD 문구 [확인됨 공고]: "HIL 테스트 시스템 구축", "drone · ground control 소프트웨어 테스트 스위트", "CI/CD 연동", "릴리스 전 빌드 안정성". Flight Software JD: "partnering with our Test organization on SITL/HITL and flight-test execution". → Test 조직이 **SITL · HITL · 비행 시험 실행**까지 맡는다 [추정, 공고 문구 근거].

---

## 2. 용어부터 바로잡기 — SITL · SIH · HITL · HIL bench

같은 단어가 프로젝트마다 다른 뜻으로 쓰인다. 면접이나 회의에서 "HIL"이라고 하면 **무엇이 진짜고 무엇이 가짜인지**를 먼저 확인해야 한다.

### 2.1 무엇이 진짜이고 무엇이 가짜인가

| 이름 | FW가 도는 곳 | 센서 데이터 | 물리(동역학) | 버스 · 전기 | 대표 예 |
|---|---|---|---|---|---|
| **Unit test** | 호스트 PC (x86/ARM Mac) | 없음 (함수 입력) | 없음 | 없음 | Betaflight `src/test/unit/*_unittest.cc` (googletest) |
| **SITL** (Software-In-The-Loop) | 호스트 PC — 실제 FW 소스를 PC용으로 빌드 | 시뮬레이터가 계산 | 시뮬레이터 (Gazebo 등) | 없음 (UDP/TCP 소켓) | Betaflight `make TARGET=SITL`, PX4 `make px4_sitl`, ArduPilot `sim_vehicle.py` |
| **SIH** (Simulation-In-Hardware, PX4) | 실제 FC 보드 **또는** PC | FC 안의 시뮬레이션 모듈이 생성 | **FC 안에서** 계산 (uORB) | 실제 FC의 MCU · 메모리 | PX4 SIH (외부 시뮬레이터 불필요) |
| **HITL** (PX4 용어) | **실제 FC 보드** | PC 시뮬레이터 → MAVLink `HIL_SENSOR` / `HIL_GPS` | PC 시뮬레이터 | USB/UART **통신선만 실제**, 센서 버스는 안 씀 | PX4 HITL + Gazebo Classic |
| **HIL bench** (일반 산업 용어) | 실제 보드들 (FC · RX · ESC …) | 실제 센서 **또는** 전기적으로 주입 (SPI 에뮬레이터, 리플레이) | 없음 또는 간단 모델 | **실제 버스 · 실제 전원 · 실제 핀** + 계측기 | 책상 위 FC + ELRS + 전원 공급기 + 로직 분석기 ([R08](2026-10-24_R08_hil_bench_design_instruments.md)) |
| **통합 리그** | 완성 기체 | 실제 센서 | 실제 (프롭 없음 / 추력대 / 테더) | 전부 실제 | 추력 측정대, 테더 호버 |
| **비행 시험** | 완성 기체 | 실제 | 실제 대기 | 전부 실제 | 스크립트 기동 + 블랙박스 로그 |

### 2.2 핵심 구분

- **SITL**: "코드 로직이 맞나". 타이밍 · 메모리 · 버스 · 인터럽트는 PC 것이라 **가짜**다.
- **PX4 HITL**: "실제 MCU에서 이 FW가 실시간으로 돌 수 있나 (CPU · 메모리 · 스케줄링)". 하지만 센서 드라이버 · SPI · DMA · 실제 IMU 노이즈는 **우회**된다. 센서 값이 MAVLink 메시지로 들어오기 때문이다.
- **HIL bench**: "실제 전기 신호에서 맞게 동작하나". 실제 UART 프레이밍 · 실제 DShot 파형 · 실제 전원 강하 · 실제 버스 에러를 본다. 비행 물리는 보통 없다.

```text
         진짜 ◀──────────────────────────────────────────────▶ 가짜
코드      unit   SITL   SIH(PC)  SIH(FC)  PX4 HITL   HIL bench   rig    flight
MCU        .      .       .        ●         ●          ●         ●       ●
센서드라이버 .      .       .        .         .       ●(주입)      ●       ●
버스/전기   .      .       .        .      통신선만      ●         ●       ●
물리       .    sim     sim      FC내부     sim       .(또는 모델)  일부     ●
비용/회    µs     s      s        s        분         분~시간      시간    반나절
```

> ⚠️ Gemini 정정: Gemini는 "PX4는 테스트 PC가 MAVLink의 `HIL_SENSOR` 패킷만 쏴주면 펌웨어가 알아서 가상 비행 시뮬레이션(SITL/HITL) 모드로 잘 돌아간다"고 했다. 부분만 맞다. PX4 문서 기준으로 (1) MAVLink 시뮬레이터 API(`HIL_SENSOR` · `HIL_GPS` 입력, `HIL_ACTUATOR_CONTROLS` 출력, TCP 4560)는 **Gazebo Classic 같은 외부 시뮬레이터용** 경로이고, (2) 새 **Gazebo(gz)** SITL은 MAVLink가 아니라 `gz_bridge`로 Gazebo transport를 직접 쓴다. (3) **HITL은 "community supported"**로 표시돼 있고 문서상 Gazebo Classic만 나오며 `pwm_out_sim` 모듈이 들어간 펌웨어가 필요하다. (4) 외부 시뮬레이터 없이 FC 안에서 물리를 돌리는 **SIH**가 대안으로 안내된다. 그리고 HITL은 센서 드라이버를 우회하므로 "실제 보드 테스트"와 같은 말이 아니다.

> ⚠️ Gemini 정정: Gemini는 "SITL/HIL 테스트 벤치"를 하나의 그림에 묶고, 3단계(브링업 → SITL/유닛 → HIL)로 끝냈다. 실제로는 그 위에 **통합 리그 · 비행 시험 · 공장(EOL) · 필드 텔레메트리** 층이 있고, 그 아래에 **정적 분석**이 있다. 그리고 "HIL"이 PX4 HITL(센서는 MAVLink로 가짜)인지 HIL bench(전기 신호 진짜)인지 구분해야 한다. 이 단원은 10층으로 다시 정리한다.

---

## 3. 드론 FW 테스트 피라미드 — 10층

### 3.1 전체 그림

```text
                         ┌──────────────────────┐
                    L10  │ Field telemetry loop │  출하 후 · 상시 · 실제 임무 데이터
                         ├──────────────────────┤
                    L9   │ Factory / EOL test   │  대당 수 분 · 전수
                       ┌─┴──────────────────────┴─┐
                  L8   │ Flight test (scripted)    │  릴리스 후보 · 반나절/회
                       ├───────────────────────────┤
                  L7   │ Integration rig            │  props off · 추력대 · 테더
                     ┌─┴───────────────────────────┴─┐
                L6   │ HIL bench (real boards/buses)  │  nightly + 일부 per-merge
                     ├────────────────────────────────┤
                L5   │ HITL (real FC, sim sensors)     │  nightly
                   ┌─┴────────────────────────────────┴─┐
              L4   │ SITL (host build + physics sim)     │  per-merge (짧은 셋) / nightly (긴 셋)
                   ├─────────────────────────────────────┤
              L3   │ Component/driver tests on target     │  per-merge (보드 팜이 있으면)
                 ┌─┴─────────────────────────────────────┴─┐
            L2   │ Unit tests (googletest, host-compiled)    │  per-commit
                 ├───────────────────────────────────────────┤
            L1   │ Static analysis · build checks · size     │  per-commit (수 초 ~ 수 분)
                 └───────────────────────────────────────────┘
   아래로 갈수록: 싸다 · 빠르다 · 많다 · 결정적(deterministic)이다
   위로 갈수록:  비싸다 · 느리다 · 적다 · 진짜다 · 흔들린다(flaky)
```

### 3.2 한 표로 보기

| 층 | 잡는 것 | 못 잡는 것 | 1회 비용/시간 [추정] | CI 위치 | 오픈소스 예 |
|---|---|---|---|---|---|
| L1 정적 분석 · 빌드 검사 | 미정의 동작 · 널 · 범위 초과 · 경고 · 스택 과다 · flash 초과 · 타깃별 빌드 깨짐 | 동작이 맞는지 | 초~분, 사람 0 | per-commit (차단) | Betaflight CI `make EXTRA_FLAGS=-Werror checks`, `cppcheck-result.xml` 타깃 |
| L2 Unit | 필터 수식 · 파서 · 상태 머신 · 경계값 · 회귀 | 타이밍 · HW · 통합 | 초~분 | per-commit (차단) | Betaflight `make test` / `test-all` / `junittest`, PX4 gtest, ArduPilot `waf tests` [확인 필요] |
| L3 타깃 위 컴포넌트/드라이버 | 실제 MCU의 레지스터 · DMA · IRQ · 드라이버 init · 컴파일러 최적화 차이 | 비행 로직 · 물리 | 분, 보드 1대 | per-merge (보드 팜 있으면) | Q01의 테스트 FW 명령들 (`imu id` 등) |
| L4 SITL | 모드 전환 · failsafe 정책 · 미션 · 네비게이션 · 파라미터 조합 · 회귀 비행 | 실시간 타이밍 · 버스 · 드라이버 · 센서 노이즈 실물 | 분 (가속 가능) | per-merge 짧은 셋, nightly 전체 | Betaflight `src/test/sitl/sitl_harness.py`, ArduPilot `Tools/autotest/autotest.py`, PX4 `mavsdk_test_runner.py` |
| L5 HITL | 실제 MCU에서 CPU 부하 · 스케줄 · 메모리 · 통신 처리 | 센서 드라이버 · 실제 버스 신호 · 전원 | 분~십수 분 | nightly | PX4 HITL (Gazebo Classic, community) |
| L6 HIL bench | 실제 UART/SPI/DShot 신호 · 지연 분포 · 깨진 패킷 · 전원 강하 · RF 감쇠 · 부팅/리셋 · FW 업데이트 | 비행 물리 · 진동 · 실제 공기역학 | 분~시간, 벤치 1대 | nightly + 릴리스 (일부 smoke는 per-merge) | 공개 표준 구현 없음 → 회사마다 직접 ([R08](2026-10-24_R08_hil_bench_design_instruments.md)) |
| L7 통합 리그 | 모터 · ESC · 전원 계통 · EMI(모터 켠 상태) · 진동이 센서에 주는 영향 · 열 | 비행 중 공기역학 · 바람 · 실제 링크 거리 | 시간, 사람 1명 | 릴리스 후보 | 추력 측정대, 테더 호버 |
| L8 비행 시험 | 제어 품질 · 실제 링크 · 진동 · 바람 · 조종사 체감 · 임무 성공 | 드문 고장 조합 (반복 수가 적다) | 반나절, 사람 2~3명 | 릴리스 후보 (게이트) | 블랙박스 로그 + 스크립트 기동 |
| L9 공장/EOL | 조립 결함 · 부품 불량 · 보정값 · FW 버전 · 링크 바인딩 | 설계 결함 (이미 위에서 잡았어야) | 대당 분 | 생산 라인 | 회사 내부 [추정] |
| L10 필드 텔레메트리 | 실제 임무에서만 나오는 조합 · 장기 열화 · 드문 리셋 | 원인 (증상만 보일 때가 많음) | 상시 | 출하 후 → 다시 아래층 테스트로 | Betaflight 블랙박스, PX4 ULog/Flight Review |

> 핵심 원칙: **결함이 발견되면 "그걸 잡을 수 있었던 가장 낮은 층"에 테스트를 추가한다.** 비행 시험에서 발견한 failsafe 버그는 SITL 시나리오와 unit test로 내려 보낸다. 이것이 피라미드가 시간이 갈수록 단단해지는 방법이다.

---

### 3.3 L1 — 정적 분석 · 빌드 검사

**무엇을 하나**

- 컴파일러 경고를 에러로 (`-Werror`). Betaflight CI는 실제로 `make EXTRA_FLAGS=-Werror checks`와 `make EXTRA_FLAGS=-Werror test-all`을 돌린다 (GitHub Actions `ci.yml`, 2026-10 master 기준 확인).
- 정적 분석기: cppcheck (Betaflight Makefile에 `cppcheck-result.xml` 타깃이 있음), clang-tidy, 상용 도구 (Coverity, Polyspace, PC-lint 등) [도구 선택은 회사마다].
- 코딩 규칙: MISRA C:2012 계열 (자동차 기원, 방산 · 항공에서도 자주 참고). 오픈소스 FC FW는 MISRA를 따르지 않는다 → 포크에 전부 적용하면 경고 폭탄. **새 코드 / 안전 경로만** 적용하는 게 현실적 [추정].
- 스택 사용량: GCC `-fstack-usage`가 함수별 `.su` 파일을 만든다. 호출 그래프와 합쳐 최악 스택 깊이를 추정 (인터럽트 중첩은 따로 더한다).
- 바이너리 크기: `arm-none-eabi-size`로 `.text` / `.data` / `.bss`를 타깃별로 기록하고 **이전 커밋과 비교**. F4 계열처럼 flash가 빡빡한 타깃은 기능 추가가 곧 빌드 실패다.
- 타깃 매트릭스 빌드: 지원하는 모든 보드 타깃이 빌드되는지. Betaflight CI는 `make targets-ci-grouped-print`로 그룹을 만들어 병렬 빌드한다.

**잡는 것 / 못 잡는 것**

| 잡는 것 | 못 잡는 것 |
|---|---|
| 초기화 안 한 변수, 배열 범위 초과, 정수 오버플로 가능성, 널 역참조 | 알고리즘이 틀린 것 (PID 계수 부호 등) |
| 특정 타깃에서만 깨지는 `#ifdef` 조합 | 런타임 타이밍 |
| flash/RAM 한계 초과 (빌드 실패로 바로 드러남) | 동적 힙 단편화 (FC FW는 보통 정적 할당이라 덜 중요) |
| 스택 과다 함수 | 재귀 · 함수 포인터 경로 (분석기가 놓침) |

**CI 위치**: per-commit, **차단(blocking)**. 수 분 안에 끝나야 한다. 오래 걸리는 심층 분석(전체 프로젝트 Polyspace 등)은 nightly.

**예: 크기 회귀 게이트**

```text
target        text    data    bss    Δtext(vs main)  limit   verdict
STM32F405   487120    9812  71240        +1,204      +4096   PASS
STM32F7X2   502336   10104  88000        +1,180      +4096   PASS
STM32H743   611904   12288 160512          +960      +4096   PASS
flash_free(F405) = 37,168 B  (warn < 32 KiB)                  WARN
```

---

### 3.4 L2 — Unit test (호스트 컴파일, googletest)

**무엇을 하나**

FW 소스 파일 하나(또는 몇 개)를 PC용 컴파일러로 빌드하고, 의존하는 함수는 테스트 파일 안에서 **가짜(stub)**로 만든다. Betaflight는 `src/test/unit/`에 `xxx_unittest.cc` 파일이 있고, `src/test/Makefile`에 테스트마다 `xxx_unittest_SRC`(어떤 소스를 같이 빌드하나)와 `xxx_unittest_DEFINES`(어떤 기능 플래그를 켜나)가 정의돼 있다.

2026-10 master 기준 `*_unittest.cc` 파일은 **89개**였다 (GitHub tree API로 셈). 테스트 대상 예: `rx_crsf_unittest`, `flight_failsafe_unittest`, `arming_prevention_unittest`, `scheduler_unittest`, `pid_unittest`, `common_filter_unittest`, `dyn_notch_filter_unittest`, `motor_output_unittest`, `rx_spi_expresslrs_unittest`, `blackbox_unittest`, `cli_unittest`, `msp_serial_unittest`.

Make 타깃 (Betaflight 최상위 Makefile 주석 그대로):

| 명령 | 뜻 |
|---|---|
| `make test` | 테스트 스위트 실행 |
| `make junittest` | 같은 것 + JUnit XML 결과 파일 (CI 대시보드용) |
| `make test-representative` | 대표 subset (확장 테스트는 타깃 하나씩만) |
| `make test-all` | 타깃별로 확장된 테스트까지 전부 (CI가 이걸 돌림) |
| `make test_help` | 테스트 목록 |
| `make test_<name>` | 테스트 하나만 |

**잘 맞는 대상**

| 대상 | 왜 unit이 좋은가 | 예 |
|---|---|---|
| 프로토콜 파서 | 입력 바이트 → 출력 구조체, 순수 함수에 가까움 | CRSF 프레임 · CRC-8 · resync · 길이 상한 ([R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)) |
| 필터 | 입력 수열 → 출력 수열, 수학적 정답 있음 | biquad 주파수 응답, PT1 시정수 |
| 상태 머신 | 이벤트 수열 → 상태 수열 | arming 금지 플래그, failsafe 단계 |
| 스케줄러 정책 | 가짜 시계로 시간 제어 | task 선택 규칙, 지연 계산 |
| 수학 유틸 | 경계값 · 오버플로 | 고정소수점 변환, 각도 wrap |

**못 잡는 것**: 실제 타이밍, 인터럽트 경쟁(race), DMA, 컴파일러 최적화 차이(호스트 gcc/clang ≠ arm-none-eabi-gcc `-O2`), 엔디안 · 정렬(alignment) 차이.

**기법 확장**

- **Property-based testing**: "아무 바이트열을 넣어도 파서가 죽지 않고, 유효 프레임을 넣으면 같은 값이 나온다" 같은 성질을 무작위 입력 수천 개로 검사. Python은 Hypothesis 라이브러리, C/C++은 RapidCheck 등 [확인 필요: C++ 라이브러리 현황]. FC FW 파서를 Python으로 감싸거나(ctypes) 같은 로직을 Python 참조 구현으로 만들어 비교.
- **Mutation testing**: 코드를 일부러 조금 바꿔서(`<` → `<=`, 상수 +1) 테스트가 그 변화를 잡는지 본다. 테스트 스위트의 **품질**을 재는 방법. C/C++용 도구로 Mull 등 [확인 필요].
- **커버리지**: gcov/lcov로 line · branch 커버리지. 안전 경로(failsafe · arming)는 branch 커버리지를 따로 본다. 항공 DO-178C의 MC/DC는 개념만 참고 (10절).

**CI 위치**: per-commit, 차단. 전체가 수 분 안.

---

### 3.5 L3 — 타깃 위 컴포넌트 / 드라이버 테스트

**무엇을 하나**: 실제 MCU 보드에 **테스트용 FW**(또는 제품 FW의 진단 명령)를 올려서 드라이버 하나씩 확인. [Q01](../../onsiteKit/qa/2026-10-20_Q01_fw_side_of_subsystem_integration.md)의 7층(BSP → 버스 → 디바이스 → 데이터 경로 → 자원 → 고장 → 수명주기)이 그대로 이 층이다.

| 테스트 | 방법 | 판정 |
|---|---|---|
| IMU ID · 설정 readback | SPI로 WHO_AM_I 읽기, 설정 쓰고 다시 읽기 | 값 일치 |
| IMU 데이터 주기 | DRDY 인터럽트마다 GPIO 토글 → 로직 분석기 | 주기 오차 · 누락 0 |
| 정지 상태 가속도 | 평평한 곳에서 N초 평균 | \|a\| ≈ 1 g ± 한계 |
| UART DMA 수신 | 알려진 패턴을 연속 송신 | 바이트 유실 0, overrun 카운터 0 |
| DShot 출력 타이밍 | 로직 분석기 디코드 | 비트 폭 · 프레임 주기 · CRC |
| flash 쓰기/지우기 | 블랙박스 영역 쓰기 → 읽기 비교 | 데이터 일치, 소요 시간 |
| 부팅 시간 | 전원 인가 → 첫 루프 GPIO | 상한 이하 |

**잡는 것**: 실제 레지스터 · 클럭 설정 오류, DMA 채널 충돌, 인터럽트 우선순위 문제, 최적화 레벨에 따른 `volatile` 누락 버그.

**못 잡는 것**: 전체 비행 로직, 다른 서브시스템과의 상호작용(통합 전).

**CI 위치**: 보드 팜(board farm — 여러 FC를 USB 허브 · 전원 릴레이에 물려 놓은 랙)이 있으면 per-merge. 없으면 nightly. 보드 팜 설계는 [R09](2026-10-24_R09_scalable_reusable_test_platform.md).

> SSD 연결: Don이 만든 shmoo · health monitoring 디버그 FW와 같은 결이다 — **제품 로직을 빼고 측정 기능만 남긴 FW**로 하드웨어를 본다.

---

### 3.6 L4 — SITL (실제 FW를 호스트용으로 빌드 + 물리 시뮬레이터)

**무엇을 하나**: FW 소스를 PC 프로그램으로 빌드한다. 하드웨어 계층(센서 드라이버 · 타이머 · UART)만 소켓으로 바꾸고, 나머지(스케줄러 · RX 처리 · PID · mixer · failsafe · 모드 로직)는 **같은 코드**다. 시뮬레이터가 기체 물리를 계산해 센서 값을 보내고, FW가 모터 명령을 돌려준다.

**Betaflight SITL** (2026-10 master에서 확인)

- 빌드: `make TARGET=SITL` → `obj/main/betaflight_SITL.elf` (타깃 README 기준)
- 플랫폼 코드: `src/platform/SIMULATOR/` (`sitl.c`, `udplink.c`, `sitl_gyro.h`, `target/SITL/`)
- 포트 (타깃 README): betaflight → 시뮬레이터 `udp://127.0.0.1:9002`, 시뮬레이터 → betaflight `udp://127.0.0.1:9003`, UART는 `tcp://127.0.0.1:576x`, 설정은 `eeprom.bin` (8192 B)
- **E2E 하네스**: `src/test/sitl/sitl_harness.py` — 문서 문자열에 따르면 RC 채널을 UDP :9004로(`double timestamp + 16 x uint16`), FDM 상태를 UDP :9003로(18 doubles), **MSP를 TCP :5761**로 읽어 모드 · arming 금지 플래그를 확인하고, 시나리오별로 `--config`로 `eeprom.bin`을 만든다. 시나리오는 모드 연결, **rx-loss 정책(DISABLE / CONTINUE / LAND)**, geofence(LAND / RTH). `USE_FLIGHT_PLAN`으로 빌드된 SITL이 필요하다고 적혀 있다.
- README의 시뮬레이터 예는 오래된 것이다 (Gazebo 8 + ArduCopterPlugin, Ubuntu 16.04 테스트). 현재 커뮤니티에서 쓰는 다른 시뮬레이터 연동은 [확인 필요].

**ArduPilot SITL / autotest** (공식 문서 확인)

- `./Tools/autotest/autotest.py build.Copter test.Copter` — SITL 빌드 → 실행 → 테스트 → 종료
- 하나만: `./Tools/autotest/autotest.py build.Plane test.Plane.ThrottleFailsafe`
- `--no-clean`(재빌드 생략), `--gdb --debug`, `--valgrind --debug`(메모리 오류), `--list`
- 테스트는 차량별 파일(`arducopter.py` 등)에 있고 `vehicle_test_suite.py`의 기반 클래스를 상속. `self.set_rc(ch, value)`로 가짜 RC 입력
- **회귀 이분 탐색**: `Tools/autotest/bisect-helper.py` + `git bisect run` → 특정 테스트를 깬 커밋을 자동으로 찾는다
- 주의: `--speedup`을 너무 높이면 MAVProxy가 못 따라가 "Set RC override timeout" 같은 **가짜 실패**가 난다 → 시뮬레이션 가속도 flaky의 원인

**PX4 SITL / MAVSDK 통합 테스트** (공식 문서 확인)

- 빌드: `DONT_RUN=1 make px4_sitl gazebo-classic mavsdk_tests`
- 실행: `test/mavsdk_tests/mavsdk_test_runner.py test/mavsdk_tests/configs/sih-sitl.json --speed-factor 10`
- 하나만: `... --model quadx --case 'Fly square Multicopter Missions including Return'`
- 테스트 프레임워크는 **Catch2** (헤더 전용)
- 포트: QGC UDP 14550, MAVSDK/MAVROS UDP 14540, 시뮬레이터 TCP 4560

**SITL이 잡는 것 / 못 잡는 것**

| 잡는 것 | 못 잡는 것 |
|---|---|
| failsafe 정책 순서 (RX loss → stage 1 → stage 2 → disarm/land) | 실제 IMU 노이즈 · 진동 · aliasing |
| 모드 전환 조건 (스위치 조합, arming 금지) | 실제 CPU 부하 · 루프 지터 (PC는 훨씬 빠르고 OS 스케줄링이 끼어듦) |
| 미션 / GPS rescue / geofence 로직 | 드라이버 · DMA · 인터럽트 |
| 파라미터 조합 회귀 (수백 개 설정 파일 일괄) | 전원 · EMI · 버스 에러 실물 |
| CLI/MSP 명령 처리 | 링크 지연의 실제 분포 |

**CI 위치**: 짧은 smoke 셋(부팅 · arm · 호버 · disarm · rx-loss 1개)은 per-merge. 전체 시나리오 × 파라미터 매트릭스는 nightly. 가속(`--speed-factor`)은 nightly에서만 공격적으로.

> ⚠️ Gemini 정정: Gemini는 1단계를 "리눅스 서버(GitLab CI)에서 `make test`"로 설명했다. Betaflight 업스트림 CI는 **GitHub Actions**이고 `make EXTRA_FLAGS=-Werror checks` + `make EXTRA_FLAGS=-Werror test-all`을 돌린다. GitLab CI · Jenkins는 **Neros 공고에 나온 도구**이므로 "Neros 포크는 GitLab CI에서 돌릴 것"이 맞는 표현이다 [추정]. 또한 업스트림에는 이미 SITL E2E 하네스(`src/test/sitl/sitl_harness.py`)가 있다 — Gemini는 이를 언급하지 않았다.

---

### 3.7 L5 — HITL (실제 FC + 시뮬레이션 센서/물리)

**무엇을 하나 (PX4 의미)**: 실제 FC 보드에 FW를 올리고 `SYS_AUTOSTART`를 HIL 기체(예: HIL Quadcopter X = 1001)로 설정한다. PC의 Gazebo Classic이 USB/UART로 FC와 MAVLink를 주고받는다. FC는 실제 MCU에서 실시간으로 추정기 · 제어기를 돌린다.

| 이 층에서만 보이는 것 | 이 층에서도 안 보이는 것 |
|---|---|
| 실제 MCU에서의 CPU 사용률 · 스케줄 여유 | 센서 드라이버 · SPI · IMU 실물 노이즈 |
| 실제 컴파일러 · 최적화 · 메모리 배치 | 모터 · ESC · 전원 |
| MAVLink 통신 부하가 루프에 주는 영향 | 실제 RC 링크 |
| flash에 저장된 파라미터 처리 | 진동 |

**Betaflight에는?**: Betaflight 업스트림에는 PX4식 HITL 모드가 없다 [확인 필요]. 비슷한 효과를 내려면 (1) MSP로 RC를 주입(`MSP_SET_RAW_RC`)하고 실제 IMU는 책상 위 정지 상태로 두거나, (2) 포크에 "리플레이 자이로" 드라이버를 넣어 기록된 IMU 데이터를 루프에 먹이는 방법이 있다 (6.6절). (2)는 FW 수정이 필요하므로 **테스트 빌드 전용 플래그**로 격리해야 한다 [추정].

**CI 위치**: nightly. 보드가 필요하므로 보드 팜과 같이 운영.

---

### 3.8 L6 — HIL bench (실제 보드 · 실제 버스 · 주입 자극 · 계측기)

이 층이 FW Test JD의 "HIL 테스트 시스템 구축"의 중심이다 [추정, 공고 문구 근거]. 벤치 하드웨어 설계는 [R08](2026-10-24_R08_hil_bench_design_instruments.md), 여러 벤치를 플랫폼으로 묶는 건 [R09](2026-10-24_R09_scalable_reusable_test_platform.md).

```text
  ┌──────────── Test host (Linux, Python runner) ─────────────┐
  │ test cases · oracles · limits · result DB · artifacts      │
  └──┬─────────┬──────────┬──────────┬───────────┬────────────┘
     │SCPI     │SWD       │UART(CRSF)│RF atten.  │Logic analyzer API
  ┌──▼───┐ ┌───▼───┐ ┌────▼────┐ ┌───▼────┐ ┌────▼─────┐
  │ PSU  │ │J-Link │ │USB-UART │ │step att│ │ LA 8ch   │
  └──┬───┘ └───┬───┘ └────┬────┘ └───┬────┘ └┬───┬───┬─┘
     │VBAT     │SWD/RTT   │          │SMA    │   │   │
  ┌──▼─────────▼──────────▼──┐   ┌───▼───────▼┐  │   │
  │  FC (DUT)  ◀──CRSF───────┼───│ ELRS RX    │  │   │
  │            ──DShot──────▶│ESC│(DUT)       │  │   │
  └──────────────────────────┘   └────────────┘  │   │
       ▲ probe: CRSF in ─────────────────────────┘   │
       ▲ probe: DShot out ───────────────────────────┘
```

| 잡는 것 | 못 잡는 것 |
|---|---|
| 실제 UART 프레이밍 · baud 오차 · 깨진 프레임 처리 | 비행 물리 · 진동 · 바람 |
| 입력 → 출력 지연 **분포** (CRSF → DShot) | 실제 RF 환경 (다중 경로 · 간섭) — 감쇠기는 경로 손실만 흉내 |
| 전원 강하 · brownout · 리셋 복구 | 모터 부하 시 EMI (모터가 없거나 무부하면) |
| failsafe 출력 (DShot 0 / disarm)이 **실제 핀에서** 몇 ms에 나오나 | 장기 열화 |
| FW 업데이트 중 전원 차단 · 부트로더 복구 | |
| RF 감쇠로 링크 품질(LQ) · 패킷 손실 곡선 | |

**CI 위치**: per-merge에는 5~10분 smoke (부팅 · 버전 확인 · arm/disarm · RX loss 1회 · 지연 100회). nightly에 전체 (지연 수천 회 · 전원 스윕 · 퍼징 · 장시간). 릴리스 후보는 전체 + 여러 보드 리비전.

> ⚠️ Gemini 정정: Gemini 그림은 "RF 감쇠기로 0 dB → 90~95 dB 올리면 드론이 20 km 멀어졌을 때"를 흉내 낸다고 했다. 감쇠기는 **경로 손실(평균 신호 세기)**만 흉내 낸다. 다중 경로 페이딩 · 도플러 · 간섭 · 안테나 패턴은 없다. 그리고 "몇 dB = 몇 km"는 주파수 · 안테나 이득 · 송신 출력 · 수신 감도에 따라 달라지므로 그대로 쓰면 안 된다. 벤치 감쇠 테스트의 정답은 "km"가 아니라 **"감쇠 X dB에서 LQ/패킷 손실이 Y, failsafe가 Z dB에서 발생"** 같은 링크 마진 곡선이다 ([R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)).

---

### 3.9 L7 — 통합 리그 (완성 기체 · 프롭 없음 / 추력대 / 테더)

> ⚠️ 안전: 프롭을 단 모터 회전은 손가락 절단 · 파편 위험이 있다. 프롭 장착 테스트는 고정된 추력대 · 보호망 · 비상 정지 스위치 · 전류 제한 전원으로만 하고, LiPo 충전/방전은 방화 백 · 감독 하에 한다. 이 층의 실습은 Don 혼자 집에서 하지 않는다.

| 형태 | 무엇을 보나 | 예 |
|---|---|---|
| 프롭 없이 모터만 | ESC 프로토콜 · bidirectional DShot eRPM · 모터 순서/방향 · 전류 · 온도 | 4모터 동시 스로틀 스윕 중 FC 리셋 없음, 레일 전압 |
| 추력 측정대 (load cell) | 스로틀 → 추력 곡선 · 효율 · 응답 시간 | 모터/프롭 조합 비교, ESC FW 변경 회귀 |
| 테더 호버 | 실제 공기 중 제어 루프 · 진동이 IMU에 주는 영향 · 필터 효과 | 블랙박스로 자이로 노이즈 PSD, 모터 노이즈 피크 |
| 진동대 위 FC | 진동 스펙트럼이 추정기/필터에 주는 영향 | 특정 주파수에서 aliasing |

**잡는 것**: 모터 켠 상태의 EMI가 IMU · GPS · 수신기에 주는 영향, 전원 계통 ripple, 진동, 열.
**못 잡는 것**: 실제 비행 기동 · 바람 · 장거리 링크.
**CI 위치**: 자동 CI보다는 **릴리스 후보 게이트** (사람이 붙는 반자동). 추력대는 스크립트로 자동화 가능.

---

### 3.10 L8 — 비행 시험 (스크립트 기동 · 텔레메트리 · chase)

**원칙**: 비행 시험도 "날려 보고 느낌"이 아니라 **테스트 케이스**다. 기동이 정해져 있고, 기록할 데이터가 정해져 있고, 판정 기준이 정해져 있다.

| 항목 | 내용 |
|---|---|
| 기동 카드 (test card) | 호버 30초 → roll/pitch/yaw 계단 입력 각 5회 → 스로틀 펀치 → 급정지 → RX 끊기(조종사 동의 하, 안전 고도) → 착륙 |
| 기록 | 블랙박스 (자이로 · setpoint · PID 항 · 모터 출력 · RC), 링크 LQ/RSSI, 배터리 전압/전류, 영상(chase 카메라) |
| 자동 판정 | 블랙박스 → 스크립트 → step response · 노이즈 PSD · 모터 포화 비율 · 배터리 sag (7절) |
| 사람 판정 | 조종사 코멘트 (oscillation · propwash · 반응성) — 정량 판정과 같이 기록 |
| 조건 기록 | 기체 ID · FW 해시 · 설정 diff · 배터리 ID/사이클 · 기온 · 바람 · 장소 · 조종사 |

**잡는 것**: 실제 제어 품질 · 진동 · 바람 · 실제 링크.
**못 잡는 것**: 드문 고장 조합 (반복 수 부족), 극한 환경.
**CI 위치**: 릴리스 후보마다 (게이트). 로그 업로드 → 자동 분석 → 이전 릴리스와 비교 리포트는 CI 잡으로 만들 수 있다.

> Don 경험 범위: Don은 **드론 비행 경험이 없다**. 비행 시험은 test pilot / technician과 협업하는 영역이고, FTE가 기여하는 부분은 **기동 카드 설계 · 로그 자동 분석 · 회귀 비교**다 [추정].

---

### 3.11 L9 — 공장 / EOL (End-Of-Line) 테스트

| 단계 | 내용 | 판정 |
|---|---|---|
| ICT/보드 테스트 | 단락 · 레일 전압 · 전류 | 범위 |
| FW 프로그래밍 | 부트로더 + 제품 FW, 해시 확인 | 해시 일치 |
| 센서 보정 | 가속도 6면 · 자이로 바이어스 · 나침반 | 잔차 한계 |
| 링크 바인딩 | RX ↔ TX 바인딩, LQ 확인 | LQ ≥ 한계 |
| 모터/ESC | 순서 · 방향 · 전류 (프롭 없이) | 범위 |
| 최종 기록 | 시리얼 · FW 버전 · 보정값 · 결과 → DB | 저장 완료 |

**설계 원칙**: 대당 시간(사이클 타임)이 비용이다. 개발 HIL 테스트를 그대로 쓰면 너무 길다 → **같은 드라이버 · 같은 결과 형식**을 쓰되 항목은 결함 모드 기준으로 고른다. 결과 형식은 Q01의 한 줄 형식(`RESULT imu.whoami PASS value=0x47 limit=0x47`)처럼 기계가 읽게.

> SSD 연결: Don의 최근 경험(factory test-node 아키텍처, stress 기반 latent defect 발굴)이 이 층과 바로 연결된다. 공장 테스트는 "설계 검증"이 아니라 **"이 한 대가 설계대로 만들어졌나"**를 본다는 차이를 분명히 말할 수 있으면 좋다.

---

### 3.12 L10 — 필드 텔레메트리 루프

출하된 기체가 실제 임무에서 남기는 로그가 다음 테스트의 재료다.

```text
 field logs ──▶ ingest/parse ──▶ anomaly rules ──▶ triage ──▶ repro at lowest layer ──▶ new test
   (blackbox,     (FW hash,       (reset count,    (사람)     (unit / SITL / HIL)      (회귀 스위트)
    ULog, link)    config)         failsafe 이벤트,
                                   LQ drop, sag)
```

| 신호 | 의미 | 다음 행동 |
|---|---|---|
| 예기치 않은 리셋 (reset reason 레지스터) | watchdog · brownout · hard fault | HIL 전원 강하 / 장시간 soak 재현 |
| failsafe 발생 빈도 증가 | 링크 · 안테나 · 간섭 · FW 회귀 | RF 감쇠 곡선 비교, FW 버전별 분포 |
| 모터 포화 비율 높음 | 기체 무게 증가 · 튜닝 · 배터리 | 비행 로그 비교 |
| CPU load / loop 지연 경고 | 기능 추가로 여유 감소 | HIL 성능 회귀 (6.13절) |

**방산 맥락**: 필드 로그는 보안 등급 · 반출 규정이 걸릴 수 있다. 어떤 데이터가 돌아오는지는 고객 계약에 달렸다 [추정].

> SSD 연결: Don의 NVMe telemetry 디버그 기능 경험 — 필드에서 무엇을 남겨야 나중에 원인을 찾을 수 있는지 **FW에 미리 심는 것**이 같은 문제다. 드론에서는 블랙박스 필드 · 리셋 원인 · 에러 카운터를 설계하는 일이다.

---

## 4. CI 게이트 배치 — per-commit · per-merge · nightly · release

### 4.1 게이트 표

| 게이트 | 목표 시간 [추정] | 포함 층 | 실패하면 |
|---|---|---|---|
| **per-commit** (push마다) | < 10분 | L1 전체 (경고=에러, 정적 분석 빠른 셋, 타깃 매트릭스 빌드, 크기 diff) + L2 전체 | merge 불가 |
| **per-merge** (MR/PR 머지 직전, main 대상) | < 30분 | 위 + L4 SITL smoke + L6 HIL smoke (보드 1~2대) | merge 불가 |
| **nightly** | 수 시간 | L3 전체 · L4 전체 매트릭스 · L5 HITL · L6 전체 (지연 분포 · 퍼징 · 전원 스윕 · soak 몇 시간) | 다음 날 아침 triage, 원인 커밋 bisect |
| **weekly / soak** | 24~72시간 | L6 장시간 soak, 온도 챔버 사이클 (있다면) | 릴리스 후보 차단 사유 |
| **release candidate** | 며칠 | 위 전부 + L7 리그 + L8 비행 카드 + 보드 리비전 매트릭스 + 문서(9절) | 릴리스 불가 |
| **production** | 대당 분 | L9 | 해당 유닛 불합격 |

### 4.2 그림 — 커밋 하나가 지나가는 길

```text
 git push ─▶ [L1 static+build+size] ─▶ [L2 unit] ─┐   (per-commit, 차단)
                                                  ▼
 MR ready ─────────────▶ [L4 SITL smoke] ─▶ [L6 HIL smoke (board farm)] ─▶ merge
                                                  │
 nightly 02:00 ─▶ [L3 target] [L4 full] [L5 HITL] [L6 full: latency×5000, fuzz, power sweep]
                         │ fail? ─▶ auto bisect ─▶ owner 알림 ─▶ 아침 triage
 RC tag ─▶ [soak 72h] [L7 rig] [L8 flight cards] [release checklist 9.5] ─▶ release
```

### 4.3 설계 규칙

- **per-commit은 결정적(deterministic)이어야 한다.** 하드웨어 · 시간 · 랜덤에 기대는 테스트는 per-commit에 넣지 않는다. flaky가 하나라도 있으면 개발자가 CI를 무시하기 시작한다.
- **HIL smoke는 짧고 강하게.** 하드웨어 자원은 공유되고 비싸다. per-merge HIL은 "이 빌드가 보드를 벽돌로 만들지 않나 + 안전 경로 1개" 정도.
- **nightly 실패는 자동 bisect.** ArduPilot `bisect-helper.py`처럼 "테스트 이름 + 범위"로 원인 커밋을 찾는 도구를 만든다.
- **결과는 기계가 읽는 형식 + 원자료 보존.** JUnit XML (Betaflight `make junittest`) 같은 표준 형식으로 CI 대시보드에, 원자료(로직 분석기 캡처 · 블랙박스)는 아티팩트 저장소에 FW 해시와 함께.
- **게이트를 넘는 기준이 문서에 있어야 한다.** "nightly에서 지연 p99가 X ms를 넘으면 RC 차단" 같은 규칙이 릴리스 체크리스트(9.5절)와 연결된다.

> Don 경험 범위: Don은 GitLab CI · Jenkins 실무 경험이 없다. 대신 web 기반 test automation과 챔버 테스트 플랫폼 SDK(스케줄링 포함)를 만들었다 — "테스트를 언제 · 어느 자원에서 · 어떤 순서로 돌릴지"를 정하는 스케줄러 문제는 같다. CI 도구 자체는 [R10](2026-10-24_R10_roadmap_labs_references.md)의 실습으로 채운다.

---

## 5. 기법 카탈로그 — 한 표

층(어디서)과 기법(어떻게)은 다른 축이다. 같은 "고장 주입"도 unit에서는 stub이 에러를 리턴하게 하고, HIL에서는 실제 선을 끊는다.

| # | 기법 | 질문 | 주로 쓰는 층 | 오라클(판정) | 비용 |
|---|---|---|---|---|---|
| T1 | 요구사항 기반 테스트 | 요구사항이 말한 대로 하나 | 전 층 | 요구사항의 수치 · 동작 | 중 |
| T2 | 경계값 / 동치 분할 | 경계에서 깨지지 않나 | L2 · L4 · L6 | 경계 안/밖 동작 | 저 |
| T3 | 상태 머신 커버리지 | 모든 상태 · 전이를 지나갔나 | L2 · L4 | 전이표 | 저~중 |
| T4 | 고장 주입 | 깨졌을 때 안전하게 실패하나 | L2 · L4 · L6 · L7 | 안전 상태 · 보고 · 복구 | 중~고 |
| T5 | 센서 주입 | 원하는 센서 상황을 재현하나 | L4 · L5 · L6 | 추정기/제어 반응 | 중~고 |
| T6 | 프로토콜 퍼징 | 이상한 입력에 죽지 않나 | L2 · L6 | crash 없음 · 카운터 · 복구 | 저~중 |
| T7 | 타이밍 / 지연 | 얼마나 빠르고 얼마나 흔들리나 | L3 · L6 · L7 | 분포 (p50/p99/max) | 중 |
| T8 | soak / 내구 | 오래 돌리면 무너지나 | L6 · L7 | 리셋 0 · 누수 없음 · 드리프트 | 고 (시간) |
| T9 | 환경 (열 · 진동 · EMI) | 환경 스트레스에서 동작하나 | L6 · L7 · 외부 시험소 | 기능 + 성능 한계 | 고 |
| T10 | 전원 | 전압 범위 · 강하 · 노이즈에서 동작하나 | L6 · L7 | 리셋 없음 / 안전 리셋 · 경고 | 중 |
| T11 | RF | 링크 마진 · 간섭에서 어떻게 되나 | L6 · L8 | LQ · 손실 · failsafe 지점 | 중~고 |
| T12 | 성능 회귀 | 새 빌드가 더 느려졌나 | L3 · L5 · L6 | CPU load · 루프 지터 · task 시간 | 저~중 |
| T13 | 제어 품질 회귀 | 새 빌드가 더 나쁘게 나나 | L4 · L7 · L8 | step response · 노이즈 PSD · 포화 | 중~고 |
| T14 | 로그 기반 오라클 | 로그만 보고 판정할 수 있나 | L4~L10 | 로그 규칙 | 저 (자동화 후) |
| T15 | 골든 유닛 비교 | 기준 장비와 같은가 | L6 · L9 | 기준 대비 차이 | 중 |
| T16 | 통계적 판정 | 몇 번 돌려야 믿을 수 있나 | 전 층 (확률적 테스트) | 신뢰 하한 | 중 |
| T17 | 회귀 이분 탐색 | 어느 커밋이 깼나 | L2 · L4 · L6 | 테스트 통과/실패 | 저 |
| T18 | 업데이트 / 부트 | 업데이트 중 끊겨도 살아나나 | L6 · L9 | 부팅 · 버전 · 복구 | 중 |

---

## 6. 기법별 상세

### 6.1 T1 요구사항 기반 테스트

요구사항 한 줄마다 테스트 케이스가 최소 하나. 요구사항이 숫자를 안 주면 **테스트 엔지니어가 숫자를 물어봐야 한다**. "failsafe가 빨리 동작해야 한다"는 테스트할 수 없다. "RC 신호 손실 후 X ms 안에 stage 1 진입, Y s 후 모터 출력 0 또는 착륙 절차"는 테스트할 수 있다.

| 나쁜 요구사항 | 테스트 가능한 요구사항 |
|---|---|
| 지연이 낮아야 한다 | CRSF 프레임 끝 → DShot 출력 변화 지연 p99 ≤ X ms, max ≤ Y ms (N ≥ 1000회, 루프 Z kHz 설정) |
| 링크가 끊기면 안전해야 한다 | RX 데이터 없음 → stage 1(설정된 failsafe 값) → `failsafe_delay` 후 stage 2(설정된 procedure) → 출력 확인 |
| 배터리 부족 시 경고 | 셀당 V_warn 이하 N초 지속 → OSD 경고 + 비퍼, V_crit 이하 → 설정된 동작 |
| 부팅 시 모터가 돌면 안 된다 | 전원 인가 ~ arm 전까지 DShot 출력 = 0(또는 disarm 명령)만, 모든 리셋 원인에 대해 |

> ⚠️ Gemini 정정: Gemini는 "RC Loss 시 **정확히 0.5초 뒤** 모터 제어 신호가 0으로 떨어지는지"를 판정 예로 들었다. Betaflight 2026-10 master의 `failsafe.c` 기본값은 `failsafe_delay = 15` (0.1초 단위 → **stage 1 1.5초**), `failsafe_procedure = FAILSAFE_PROCEDURE_DROP_IT`, `failsafe_throttle = 1000`, 그리고 RX 데이터 실패 판정 최소 주기 `PERIOD_RXDATA_RECOVERY = 100` ms, failsafe 갱신 주기 `PERIOD_RXDATA_FAILURE = 10` ms이다. 즉 기본 설정에서 "0.5초 뒤 0"은 맞지 않는다. 더 중요한 교훈: **오라클은 매직 넘버가 아니라 DUT의 실제 설정(`diff all`)에서 계산해야 한다.** Neros 포크의 값은 모른다 [추정 불가 · 확인 필요].

### 6.2 T2 경계값 / 동치 분할

입력 공간을 "같은 동작을 하는 구간(동치 클래스)"으로 나누고, 각 구간의 대표값 + **경계 바로 안/밖**을 테스트한다.

| 대상 | 동치 클래스 | 경계값 |
|---|---|---|
| CRSF len 필드 | 너무 작음 / 정상 / 너무 큼 | 최소-1, 최소, 최대, 최대+1 (최대 프레임 64바이트 기준) [확인 필요: Betaflight 상한 상수] |
| RC 채널 값 (µs) | 범위 아래 / 정상 / 범위 위 | `rx_min_usec`-1, `rx_min_usec`, `rx_max_usec`, `rx_max_usec`+1 [확인 필요: 기본값] |
| 배터리 전압 | 경고 위 / 경고~위험 / 위험 아래 | V_warn ± 1 LSB, V_crit ± 1 LSB, 히스테리시스 |
| failsafe_delay | 최소 / 보통 / 최대 | `FAILSAFE_DELAY_MIN`(=1), 200(`FAILSAFE_DELAY_MAX`), 범위 밖은 CLI가 거부하나 |
| 루프 주기 | 지원 주파수 목록 | 가장 높은 값에서 CPU load, 가장 낮은 값에서 필터 |
| 시간 카운터 | wrap 전 / wrap 후 | `millis()` 32비트 wrap (~49.7일), `micros()` 32비트 wrap (~71.6분) |

> 마지막 줄이 중요하다. 32비트 µs 카운터는 약 71.6분에 wrap한다. 장시간 soak(T8)에서만 드러나는 버그의 단골이다. unit에서 가짜 시계로 wrap 직전 값부터 시작하면 몇 ms 만에 재현된다 — **위층 버그를 아래층으로 내리는** 좋은 예.

### 6.3 T3 상태 머신 커버리지 (arming · failsafe FSM)

**상태 커버리지**(모든 상태 방문) < **전이 커버리지**(모든 화살표) < **전이 쌍 커버리지**(연속 두 전이). 안전 FSM은 최소 전이 커버리지 + "있으면 안 되는 전이"(금지 전이) 테스트.

```text
 Betaflight failsafe (개념도 — 상세 상태 이름은 R03에서 소스로 확인)

   ┌─────────┐ rx lost  ┌───────────────┐ failsafe_delay ┌──────────────────┐
   │ IDLE    │────────▶│ STAGE 1       │───────────────▶│ STAGE 2          │
   │(normal) │         │ (failsafe 값  │                │ procedure:       │
   └────▲────┘         │  유지, 회복 가능)│                │ DROP / LAND /    │
        │ rx ok ≥      └──────┬────────┘                │ GPS_RESCUE       │
        │ recovery_delay      │ rx ok                   └────────┬─────────┘
        └─────────────────────┘                                  │ landed/timeout
                                                     ┌───────────▼──────────┐
                                                     │ DISARMED · RX_LOSS   │
                                                     │ (arm 금지, 회복 대기) │
                                                     └──────────────────────┘
```

| 전이 | 테스트 | 기대 |
|---|---|---|
| IDLE → STAGE 1 | RX 출력 끊기 (HIL: CRSF 송신 중단, SITL: RC feed 중단) | failsafe 값 적용, 플래그 |
| STAGE 1 → IDLE | `failsafe_delay` 전에 신호 복구 | 제어 회복, disarm 안 됨 |
| STAGE 1 → STAGE 2 | 신호 계속 없음 | 설정된 procedure |
| STAGE 2 → DISARMED | DROP: 즉시 / LAND: `failsafe_landing_time` 후 | 모터 0 |
| DISARMED → re-arm | 신호 복구 후 바로 arm 시도 | **금지되어야 함** (회복 조건 · 스위치 리셋 필요) |
| 금지: 부팅 → ARMED | arm 스위치를 켠 채 부팅 | arm 금지 플래그 |
| 금지: 스로틀 높음 → ARMED | 스로틀 > 최소로 arm 시도 | 거부 |

Betaflight는 이미 `flight_failsafe_unittest`, `arming_prevention_unittest`, `flight_failsafe_rescue_unittest`가 있다 → 포크에서 바꾼 부분이 이 테스트를 **깨는지/커버하는지** 확인하는 것이 첫 일 [추정].

### 6.4 T4 고장 주입 (fault injection)

| 고장 | unit (L2) | SITL (L4) | HIL bench (L6) | 리그 (L7) | 기대 동작 (예) |
|---|---|---|---|---|---|
| 버스 에러 (SPI/I2C NACK · timeout) | stub이 에러 리턴 | 해당 없음 | 버스 라인을 MOSFET로 GND에 붙이기 · 칩 select 차단 [추정 방법] | 커넥터 분리 | 에러 카운터 · 센서 장애 플래그 · 대체/안전 모드 |
| 센서 stuck (값 고정) | 같은 값 반복 | FDM에 고정값 | 리플레이 주입 | — | 장애 검출 (분산 0) |
| 센서 노이즈 증가 | 노이즈 추가 | 노이즈 추가 | 리플레이 + 노이즈 | 진동 | 필터 성능 · 추정 품질 |
| 센서 dropout (샘플 누락) | 콜백 생략 | FDM 끊기 | DRDY 차단 | — | 타임아웃 검출 |
| RC loss | 가짜 RX 상태 | RC feed 중단 | CRSF 송신 중단 · RF 감쇠 최대 | TX 끄기 | failsafe FSM (6.3) |
| 깨진 프레임 | 바이트 배열 | — | USB-UART로 깨진 CRSF | — | CRC 실패 카운트, 다음 프레임 정상 |
| brownout | 해당 없음 | 해당 없음 | 프로그래머블 PSU 전압 강하 프로파일 | 배터리 sag | 리셋 원인 기록 · 안전 재부팅 · disarm 상태 |
| watchdog | 해당 없음 | 해당 없음 | 테스트 빌드의 "hang" 명령 [추정] | — | 리셋 · 원인 기록 · 재부팅 후 disarm |
| flash full | 가짜 flash 크기 | eeprom.bin 크기 | 블랙박스 꽉 채우기 | — | 로깅 중단해도 비행 영향 0 |
| clock drift | 가짜 시계 비율 | 시뮬레이션 시간 비율 | 외부 클럭 소스 변경 [추정] / UART baud ±% | — | baud 허용 오차 내 통신 유지 |
| 설정 손상 | 깨진 PG 데이터 | eeprom.bin 깨기 | flash 영역 덮어쓰기 | — | 기본값 복구 + 경고 |

**원칙**

- **하나의 고장에 세 가지 질문**: 검출하나(detect) · 안전 상태로 가나(safe) · 보고/기록하나(report). 복구(recover)는 고장 종류에 따라.
- **고장 주입 장치 자체를 검증한다.** "전압 강하를 넣었다"는 스코프로 실제 파형을 캡처해서 증거로 남긴다. 주입이 안 됐는데 PASS가 나오는 것이 가장 나쁜 실패다.
- **주입 시점이 중요하다.** 같은 brownout도 부팅 중 · arm 직후 · flash 쓰기 중에 다르게 터진다. 시점을 파라미터로.

> SSD 연결: Don의 chip reliability · stress 기반 latent defect 발굴 경험 — "스트레스를 가해 잠재 결함을 드러낸다"는 생각이 같다. SSD의 전원 차단(sudden power loss) 테스트 ↔ 드론의 brownout · FW 업데이트 중 전원 차단 테스트가 가장 가까운 짝이다 (SPO 테스트 세부는 Don이 명시한 경험 범위 밖이므로 비유로만).

### 6.5 T5 센서 주입 — 네 가지 방법

| 방법 | 어떻게 | 장점 | 단점 | 쓰는 층 |
|---|---|---|---|---|
| A. MAVLink `HIL_SENSOR` | 시뮬레이터가 MAVLink로 센서 값 전송 (PX4 HITL/SITL 외부 시뮬레이터 경로) | 표준, 도구 많음 | 드라이버 · SPI 우회, 실시간 대역폭 한계 | L4 · L5 (PX4) |
| B. SITL 소켓 | FDM 패킷(UDP)으로 상태 전달 (Betaflight `udplink.c`) | 간단, 빠름, 가속 가능 | PC 타이밍 | L4 (Betaflight) |
| C. FW 내부 리플레이 | 테스트 빌드에서 자이로 드라이버 대신 기록된 IMU 버퍼를 루프에 공급 | 실제 MCU · 실제 루프 타이밍, 실제 비행 데이터 재사용 | FW 수정 필요 (테스트 전용 플래그로 격리), 버퍼 크기 = RAM | L5 · L6 |
| D. SPI 에뮬레이터 (MCU/FPGA) | 실제 IMU 자리에 SPI slave를 흉내 내는 보드, DRDY를 8 kHz로 생성 | FW는 **완전히 제품 그대로**, 드라이버까지 검증 | 만들기 어려움 (타이밍 · 레지스터 맵 흉내), 비용 | L6 |

> ⚠️ Gemini 정정: Gemini는 "PC는 OS 타이머 때문에 8 kHz를 못 맞추니 **반드시** 보조 MCU/FPGA 센서 에뮬레이터가 필요하다"는 식으로 썼다. PC 직접 실시간 주입이 어렵다는 건 맞지만, 대안이 그것만은 아니다. (B) SITL은 시뮬레이션 시간을 쓰므로 실시간이 필요 없고, (C) FW 내부 리플레이는 보드 하나로 된다. (D) SPI 에뮬레이터는 "드라이버까지 제품 그대로"가 꼭 필요할 때 쓰는 **가장 비싼** 선택지다. 그리고 IMU 에뮬레이터는 실제 IMU의 레지스터 맵 · FIFO · 설정 응답까지 흉내 내야 하므로 IMU 종류마다 작업이 따로 든다 [추정].

**리플레이 데이터 출처**: 비행 블랙박스 로그의 자이로/가속도 원자료(필터 전 값이 기록되도록 debug 모드 설정 필요 [확인 필요: Betaflight `debug_mode` GYRO_SCALED 등]). 로그 샘플 주기가 루프 주기보다 낮으면(`blackbox_sample_rate`) 그대로 리플레이할 수 없다 → 리플레이용 로그는 full rate로 따로 수집.

### 6.6 T6 프로토콜 퍼징

| 종류 | 방법 | 어디 |
|---|---|---|
| 무작위 바이트 | 완전 랜덤 스트림 | L2 (파서 함수), L6 (UART) |
| 구조 인식(mutation) | 유효 프레임에서 1바이트 · 1비트 바꾸기, len 바꾸기, CRC만 맞추고 내용 이상하게 | L2 · L6 |
| 커버리지 기반 | libFuzzer / AFL++로 파서를 호스트 빌드 [도구는 일반 지식, Betaflight 연동은 확인 필요] | L2 (nightly) |
| 타이밍 퍼징 | 프레임 사이 간격을 0 ~ 큰 값으로, 프레임 중간 끊기 | L6 |

**오라클**: crash/hard fault 0, watchdog 리셋 0, 유효 프레임은 계속 정상 처리(퍼징 사이사이에 정상 프레임 섞어 검증), 에러 카운터가 주입량과 맞음, 루프 지터 증가가 한계 이내.

**CRSF 예 (resync 검증)**: `[garbage x N][valid frame][1바이트 잘린 frame][valid frame]` → 두 유효 프레임은 반드시 디코드, 잘린 프레임은 CRC 실패 또는 길이 불일치로 버림. 상세 형식은 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md).

### 6.7 T7 타이밍 / 지연 테스트 — 숫자 하나가 아니라 분포

**왜 분포인가**: 링크 지연에는 무작위 대기가 들어 있다. ELRS 500 Hz면 패킷 주기 2 ms → stick 변화가 다음 패킷까지 평균 ~1 ms 기다린다. FC 루프 · RX task 주기도 위상이 랜덤이다. 그래서 1회 측정값은 의미가 없고, **p50 · p95 · p99 · max**와 히스토그램이 필요하다.

**측정 방법**

| 방법 | 해상도 | 무엇을 재나 | 주의 |
|---|---|---|---|
| 로직 분석기 다채널 (같은 timebase) | 샘플레이트 의존 (예: 100 MS/s → 10 ns) | 인터페이스 사이 지연 (CRSF in → DShot out) | 프로토콜 디코드 후 "같은 값이 나타난 시각" 매칭 |
| GPIO 토글 계측 | 수십 ns | FW 내부 구간 (RX task 시작/끝, PID 끝) | 테스트 빌드 전용, 토글 자체 비용 |
| DWT cycle counter (Cortex-M) | 1 CPU 사이클 | 함수 실행 시간 | 결과를 밖으로 꺼내는 경로 필요 (RTT · 로그) |
| Betaflight CLI `tasks` | µs | task별 주기 · 최대/평균 실행 시간 | 출력 형식 [확인 필요], 관측이 부하를 바꿈 |
| 고속 카메라 | ~1 ms | glass-to-glass, stick → 모터 | 사람 판독 or 영상 처리 |

**지연 측정 설계 체크**

1. 자극(stimulus)을 **알려진 시각에** 넣는다 (USB-UART로 CRSF 계단 입력, 또는 실제 TX).
2. 입력과 출력을 **같은 계측기**로 잡는다 (두 계측기의 시계는 다르다).
3. 수천 번 반복, 자극 시각을 루프 주기에 대해 **랜덤 위상**으로 (항상 같은 위상이면 분포가 아니라 한 점만 본다).
4. 결과는 분포 + 이상치 원자료. [`step_response_oracle.py`](../code/step_response_oracle.py)의 `latency_summary()` · `histogram_text()`가 그 요약.
5. 조건 기록: 루프 주파수 · RX 프로토콜 · packet rate · rc smoothing 설정 · FW 해시.

```text
 latency CRSF→DShot (self-test 예시 데이터, 단위 ms)
   0.800 | ############################ 1230
   1.300 | ############################# 1242
   1.800 | ############################## 1304
   2.300 | ############################ 1224
   2.800 |  0
   3.300 |  0
   3.800 |  0
   4.300 | # 25      ← 꼬리: 패킷 하나 잃고 다음 주기까지 기다린 경우
 n=5025 p50=1.811 p95=2.713 p99=2.792 max=4.8
```

이 가짜 데이터에서 p99(2.79 ms)는 꼬리(4.8 ms)를 **못 본다** — 꼬리가 0.5%라서. 그래서 판정에 **max(또는 p99.9)**를 같이 둔다. 꼬리는 원인이 따로 있다(패킷 손실 · 우선순위 역전 · flash 쓰기).

### 6.8 T8 soak / 내구 테스트

| 무엇 | 얼마나 [추정] | 관측 |
|---|---|---|
| 연속 동작 (arm 없이, 모든 task 활성) | 24~72시간 | 리셋 0, 에러 카운터 증가율, CPU load 드리프트 |
| arm/disarm 반복 | 수천 회 | 상태 누수, arm 실패 0 |
| 전원 on/off 반복 | 수천 회 | 부팅 실패 0, 설정 손상 0, 부팅 시간 분포 |
| failsafe 진입/복구 반복 | 수천 회 | 매번 같은 전이 · 시간 |
| 블랙박스 로그 채우기/지우기 | flash 수명 고려 | 쓰기 실패 · 시간 증가 |
| 32비트 µs wrap 통과 | ≥ 72분 | wrap 순간 지연 · 스케줄 이상 없음 |

> SSD 연결: Don의 챔버 테스트 플랫폼 SDK(온도 제어 · 스케줄링 · eSSD 상태 모니터링)와 그대로 같은 구조다 — 장시간 시퀀스를 돌리며 상태를 주기적으로 수집하고, 이상이 생긴 순간의 앞뒤를 남기는 것. 드론에서는 "상태 모니터링"이 MSP/CLI/RTT 폴링이 된다.

### 6.9 T9 환경 시험 (열 · 진동 · EMI)

| 환경 | 드론에서 보는 것 | 개발 단계 방법 | 정식 시험 (참고) |
|---|---|---|---|
| 고온/저온 | IMU 바이어스 드리프트 · 크리스탈 · 배터리 성능 · MCU 과열 · flash | 온도 챔버에 FC/기체, 기능 + 센서 드리프트 측정 | MIL-STD-810H Method 501.7 (High Temperature) · 502.7 (Low Temperature) |
| 온도 사이클 | 납땜 · 커넥터 | 사이클 반복 후 기능 | 810H Method 503.x (Temperature Shock) [확인 필요: 번호] |
| 진동 | IMU aliasing · 커넥터 이탈 · 필터 성능 | 진동대 위 FC, 블랙박스 PSD | 810H Method 514.8 (Vibration) |
| 충격 | 낙하 · 착륙 충격 후 동작 | 낙하 시험 | 810H Method 516.8 (Shock) |
| 고도(저압) | 기압계 · 냉각 | 저압 챔버 | 810H Method 500.6 (Low Pressure/Altitude) |
| EMI/EMC | 모터 · ESC · VTX가 IMU · GPS · RX에 주는 간섭, 외부 방사에 대한 내성 | 모터 켠 상태(L7)에서 센서 노이즈 · GPS SNR · RX LQ 비교 | MIL-STD-461 계열 (예: CE102 · RE102 · RS103) [확인 필요: 개정판 · 적용 여부] |

> 이름 확인: 810H 방법 번호 500.6 · 501.7 · 502.7 · 514.8 · 516.8은 공개 시험 보고서들에서 확인했다 (참고 링크). 전체 목록은 DoD ASSIST의 원문으로 확인해야 한다 [확인 필요]. Neros 제품이 어떤 시험을 요구받는지는 모른다 [추정].

> SSD 연결 (가장 강한 연결점): Don은 **챔버 테스트 플랫폼**을 만들었다. 온도 챔버 안에서 DUT를 돌리며 온도 프로파일과 기능 테스트를 같이 스케줄하는 일은 드론 FC/RX 보드에도 그대로 옮겨진다. 차이는 (1) 드론은 IMU처럼 **온도에 따라 값이 변하는 센서**가 있어 "기능 PASS"뿐 아니라 **드리프트 곡선**이 결과물이고, (2) 진동 · EMI처럼 **기체가 스스로 만드는 환경**이 있다는 것.

### 6.10 T10 전원 테스트

| 테스트 | 방법 | 판정 |
|---|---|---|
| 동작 전압 범위 | PSU로 최소 셀 수 ~ 최대 셀 수 전압 스윕 | 전 구간 기능 · 레일 정상 |
| 전압 강하 (sag) | 고부하 시 배터리 sag를 PSU 프로파일로 재현 | 리셋 없음 or 의도된 동작 |
| brownout | 레일이 BOR 임계 근처를 천천히 통과 · 빠르게 통과 | 반쯤 살아 있는 상태(좀비) 없음, 리셋 원인 기록 |
| 역전압 · 과전압 (하드웨어 보호) | 하드웨어 설계 검증 영역 — FW는 측정 · 기록 | — |
| 전원 노이즈 | ESC 스위칭 노이즈 (L7) | ADC 전압 측정 정확도, 센서 노이즈 |
| 전류 측정 정확도 | 기준 전류계 vs FC 전류 센서 | 오차 % |
| 핫플러그 순서 | USB 먼저 / 배터리 먼저 | 상태 일관성, 모터 안전 |
| FW 업데이트 중 전원 차단 | 업데이트 진행 중 임의 시점 차단 (여러 시점) | 부트로더로 복구 가능 |

> ⚠️ 안전: LiPo 대신 **전류 제한된 프로그래머블 PSU**를 쓴다. 고전압 · 고전류 실험 전 Don과 장비 · 예산 확인.

### 6.11 T11 RF 테스트

| 테스트 | 장비 [추정] | 결과물 |
|---|---|---|
| 링크 마진 곡선 | 동축 케이블 + 스텝 감쇠기 (차폐 필수) | 감쇠(dB) vs LQ · RSSI · 패킷 손실 · failsafe 지점 |
| packet rate별 지연 | 감쇠 고정, rate 변경 | 지연 분포 (T7) |
| 간섭 내성 | 신호 발생기로 대역 내 간섭 | 손실률 변화 |
| 텔레메트리 회귀 | 같은 조건에서 FW 버전 비교 | 차이 |
| 실외 거리 시험 | 비행 or 지상 고정 | 실제 마진 (감쇠 곡선과 비교) |

방산 맥락에서 재밍 대응은 핵심 기능일 수 있으나 세부는 공개 정보가 없다 [추정]. RF 테스트 장비 · 규제(송신 출력 · 대역)는 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · [R08](2026-10-24_R08_hil_bench_design_instruments.md).

### 6.12 T12 성능 회귀 (CPU load · 루프 지터 · task 시간)

| 지표 | 출처 | 회귀 규칙 예 [추정 숫자] |
|---|---|---|
| CPU load | CLI `status` | 이전 릴리스 대비 +5%p 이상 → 경고 |
| task별 max/avg 실행 시간 | CLI `tasks` | gyro/PID task max가 루프 주기의 X% 초과 → 실패 |
| 루프 주기 지터 | GPIO 토글 + 로직 분석기 | p99 jitter ≤ Y µs |
| 스택 high-water | 테스트 빌드 stack painting | 여유 < 20% → 경고 |
| RAM/flash | 빌드 산출물 (L1) | 한계 대비 |

**주의**: 같은 FW라도 설정(루프 주파수 · 필터 · OSD · 블랙박스 rate)이 다르면 부하가 다르다. 성능 회귀는 **고정된 설정 세트**(대표 설정 3~5개)로만 비교한다.

### 6.13 T13 제어 품질 회귀 (블랙박스 기반)

| 지표 | 계산 | 무엇을 의미 |
|---|---|---|
| step response | setpoint 계단 vs gyro 응답 → rise · overshoot · settling (7절) | 튜닝 · 필터 지연 변화 |
| 노이즈 PSD | 자이로 (필터 전/후) FFT/Welch | 모터 노이즈 피크 · 필터 효과 |
| 모터 포화 비율 | 모터 출력이 최대/최소에 붙은 시간 % | 여유 부족 · 튜닝 과다 |
| tracking error RMS | setpoint − gyro | 전체 추종 성능 |
| D-term 노이즈 · 모터 출력 노이즈 | 고주파 성분 에너지 | 모터 발열 위험 |

Betaflight 커뮤니티 도구로 PIDtoolbox · Blackbox Explorer 등이 있다 [확인 필요: 현재 유지 상태]. FTE가 할 일은 이것을 **CI에서 돌아가는 스크립트**로 만들어 "이번 릴리스 후보 vs 지난 릴리스" 리포트를 자동으로 내는 것 [추정].

### 6.14 T14 로그 기반 오라클

테스트가 끝난 뒤 **로그만 보고** 판정하는 규칙. 사람 개입 없이 nightly · 비행 로그 · 필드 로그에 같은 규칙을 쓸 수 있다.

| 규칙 | 로그 출처 |
|---|---|
| 리셋 원인이 POR 외에 없다 | 부팅 로그 · reset reason |
| arm 중 `RX_LOSS` 이벤트 없음 (해당 시나리오 제외) | 블랙박스 이벤트 |
| 모든 failsafe 진입 후 `failsafe_delay` ± 허용 안에 stage 2 | 블랙박스 플래그 시각 |
| 자이로 샘플 간격 최대 ≤ 2 × 주기 | 블랙박스 시간 필드 |
| 에러 카운터 증가 0 (예: SPI 에러, CRC 에러) | CLI `status` 전후 diff |

**함정**: 로그가 없는 것은 PASS가 아니다. "이벤트 0개"와 "로그 수집 실패"를 구분하는 **존재 확인**(heartbeat 필드 · 샘플 수)이 먼저다. [`step_response_oracle.py`](../code/step_response_oracle.py)의 `judge()`도 측정값이 None이면 FAIL로 처리한다.

### 6.15 T15 골든 유닛 비교

알려진 좋은 보드(golden unit)와 DUT를 **같은 자극**으로 돌려 차이를 본다.

| 쓰임 | 주의 |
|---|---|
| 새 보드 리비전 검증 (rev B vs golden rev A) | golden도 늙는다 — 주기적으로 기준 장비로 재보정, golden 2대 이상 |
| 공장에서 계측기 대신 (상대 비교) | golden의 측정 불확도가 그대로 전파 |
| FW 회귀 (같은 보드, 다른 FW) | 하드웨어 차이를 없애려고 같은 보드에서 A/B |

### 6.16 T17 회귀 이분 탐색 · T18 업데이트/부트

- **bisect**: nightly에서 실패한 테스트 이름 + 마지막 통과 커밋 → `git bisect run <script>`. HIL 테스트를 bisect하려면 **빌드 → 플래시 → 테스트**를 스크립트 하나로 만들어야 하고, flaky면 bisect가 엉뚱한 커밋을 가리킨다 → 각 단계를 N번 돌려 다수결 (8절).
- **업데이트/부트**: DFU · 부트로더 경로, 업데이트 중 전원 차단 (여러 시점), 다른 버전 간 설정 마이그레이션 (`diff all` 복원), 롤백. 방산 장비는 현장 업데이트 절차 자체가 납품물일 수 있다 [추정].

---

## 7. 제어 시스템의 테스트 오라클

### 7.1 왜 어려운가

파서는 "이 바이트 → 이 값"이라는 정답이 있다. 제어 루프는 출력이 **시간에 따른 모양**이고, 센서 노이즈 · 기체 차이 · 배터리 상태에 따라 매번 조금씩 다르다. 그래서 정답 "값" 대신 **허용 범위를 가진 특징량**으로 판정한다.

### 7.2 오라클 종류

| 오라클 | 설명 | 예 |
|---|---|---|
| 특징량 한계 | 응답에서 숫자를 뽑아 한계와 비교 | overshoot ≤ 25%, settling ≤ 0.25 s |
| 기준 대비 (회귀) | 이전 릴리스 결과와 비교, 차이가 한계 이내 | rise time 변화 ≤ ±10% |
| 모델 기반 | 기대 응답 모델(전달함수)과의 오차 | RMS 오차 ≤ X |
| 불변식(invariant) | 항상 성립해야 하는 성질 | disarm 중 모터 출력 = 0, 각도 모드에서 각도 ≤ 최대 각 |
| 메타모픽(metamorphic) | 입력 관계 → 출력 관계 | 계단 크기 2배 → (선형 영역에서) 응답 2배, roll 좌/우 대칭 |
| 통계 | 여러 번의 분포로 판정 | p99 지연, 통과율 하한 (8절) |

> 메타모픽 테스트는 "정답을 모를 때" 특히 유용하다. 예: roll +200 deg/s 계단과 −200 deg/s 계단의 overshoot는 (대칭 기체라면) 거의 같아야 한다. 예제 코드 self-test [2]가 같은 생각.

### 7.3 step response 특징량 정의

```text
  y ▲         overshoot
    │        ╭─╮  ← peak
 sp ┼ ─ ─ ─ ╱─ ╰─────────────── ± band (예: 5%)
    │      ╱
90% ┼ ─ ─ ╱
    │    ╱
10% ┼ ─ ╱
    │──╱
    └──┼──┼────┼──────┼────────▶ t
      t0 t10  t90   t_settle
  rise = t90 − t10      overshoot = (peak − sp) / |step|
  settling = 마지막으로 band 밖이던 다음 시각 − t0
  ss_error = sp − mean(y, 끝 10%)
```

### 7.4 예제 코드 — `step_response_oracle.py`

파일: [`../code/step_response_oracle.py`](../code/step_response_oracle.py) (Python 3.9+, 표준 라이브러리만)

| 함수 | 하는 일 |
|---|---|
| `step_metrics(t, sp, y, band, ss_window)` | rise time · overshoot % · settling time · steady-state error |
| `latency_summary(samples)` | n · min · mean · stdev · p50 · p95 · p99 · max |
| `histogram_text(samples)` | 리포트용 ASCII 히스토그램 |
| `judge(test_id, measured, limits)` | `RESULT <id>.<name> PASS\|FAIL value=… lo=… hi=…` 줄들 + 전체 판정 |
| `flaky_check(results, required_rate)` | STABLE_PASS / STABLE_FAIL / FLAKY / NEED_MORE |
| `runs_needed_zero_fail(rate, conf)` | 0 실패로 통과율 주장하려면 몇 번 |
| `simulate_second_order(...)` | self-test용 가짜 2차 시스템 (8 kHz 샘플) |

실행 결과 (2026-10-24, `python3 -I step_response_oracle.py`, 요약):

```text
[1] 2차 시스템 zeta=0.5: 이론 overshoot = exp(-pi*z/sqrt(1-z^2)) = 16.3%
      {'step_time': 0.05, 'rise_time': 0.0409, 'overshoot_pct': 16.254, 'settling_time': 0.2014, ...}
  ok   overshoot 16.25% ~= 16.30%
[3] 이득 부족 플랜트 (gain=0.9) → steady-state error 10% → judge FAIL
      RESULT ctl.roll_step.overshoot_pct PASS value=0 lo=None hi=25.0
      RESULT ctl.roll_step.settling_time FAIL value=None lo=None hi=0.25
      RESULT ctl.roll_step.ss_error_pct FAIL value=10 lo=None hi=2.0
[4] 정상 플랜트 + 노이즈 → judge PASS
      RESULT ctl.roll_step.overshoot_pct PASS value=10.47 lo=None hi=25.0
      RESULT ctl.roll_step.settling_time PASS value=0.1335 lo=None hi=0.25
      RESULT ctl.roll_step.ss_error_pct PASS value=0.02582 lo=None hi=2.0
[6] 지연 분포 ... p50=1.811 p95=2.713 p99=2.792 max=4.8 → PASS
[7] 20/20 → NEED_MORE, 19/20 → FLAKY, 300/300 → STABLE_PASS, 250/250 → NEED_MORE
SELF-TEST PASS
```

**읽는 법**

- [1]은 이론값(2차 시스템 overshoot 공식)과 비교해 **오라클 자체를 검증**한다. 오라클 코드도 테스트 대상이다.
- [3]에서 settling이 None이다 — 응답이 목표의 ±5% 안에 끝까지 못 들어왔기 때문. "측정 못 함"은 FAIL로 친다.
- [4]는 노이즈가 있어도 판정이 안정적인지. 실제 블랙박스는 노이즈가 훨씬 크다 → 판정 전에 저역 필터 또는 여러 계단의 평균 응답을 쓴다 (`band`를 노이즈보다 넓게).

**블랙박스에 붙이려면** [추정 · 확인 필요]: Blackbox 로그를 CSV로 바꾼 뒤(`blackbox_decode` 도구) `time`, `setpoint[0]`(roll), `gyroADC[0]` 열을 뽑아 계단 구간을 잘라서 `step_metrics()`에 넣는다. 실제 비행에서는 완벽한 계단이 드물므로 **조종사 기동 카드의 계단 입력**이나 SITL의 스크립트 계단이 필요하다. 열 이름은 Betaflight 버전마다 다를 수 있다.

---

## 8. 통계적 판정과 flaky 테스트

### 8.1 몇 번 돌려야 하나

"한 번도 실패 안 했다"로 통과율을 주장하려면 횟수가 필요하다. N번 연속 성공일 때 통과율의 95% 단측 하한은 (0.05)^(1/N)이다. **rule of three**: 0 실패 N회 → 실패율 상한 ≈ 3/N (95%).

| 주장하고 싶은 통과율 (95% 신뢰) | 0 실패로 필요한 횟수 |
|---|---|
| 90% | 29 |
| 95% | 59 |
| 99% | 299 |
| 99.9% | 2995 |

(`runs_needed_zero_fail()`로 계산. 99% → 299는 self-test에서 확인.)

**의미**: "failsafe가 1000번 중 1번 늦는다"는 버그는 nightly에서 50번 돌려서는 거의 안 보인다. 안전 경로의 확률적 결함은 **soak에서 수천 회** 돌려야 한다. 또는 원인을 찾아 **결정적으로 재현하는 unit test**로 내린다.

### 8.2 flaky 다루기

| 원인 | 예 | 처리 |
|---|---|---|
| 테스트 자체 타이밍 가정 | `sleep(0.5)` 후 상태 확인 | 조건 대기 (polling + timeout)로 |
| 시뮬레이션 가속 과다 | ArduPilot `--speedup` 높을 때 RC override timeout | 가속 낮추기, 타임아웃을 시뮬레이션 시간으로 |
| 공유 자원 경합 | 같은 USB 허브의 다른 벤치 | 자원 잠금 · 격리 |
| 장비 상태 | 로직 분석기 버퍼 · 케이블 접촉 | 장비 self-check를 테스트 앞에 |
| **진짜 FW 결함** | 경쟁 조건 · 우선순위 역전 | **재시도로 덮지 말 것** — 버그로 등록 |

**규칙**

- 자동 재시도(retry)는 **결과를 FLAKY로 기록**하고 통과로 세지 않는다. "재시도로 통과"는 통계적으로 실패율을 숨긴다.
- flaky 비율을 테스트별로 추적한다 (대시보드). 비율이 오르면 FW 회귀 신호일 수 있다.
- 격리(quarantine) 목록은 기한과 담당자가 있어야 한다.
- 예제 `flaky_check()`: 19/20 통과 → FLAKY (조사 대상), 20/20 → NEED_MORE (99% 주장에 부족), 300/300 → STABLE_PASS.

> 참고: 코드의 Wilson 하한(양측)은 보고용, 0 실패 판정은 단측 정확 하한 (1−0.95)^(1/N)을 쓴다. Wilson은 더 보수적이라 300/300에서도 0.987로 99%에 못 미친다 — 처음 self-test가 이 차이로 실패했고, 판정 기준을 명시적으로 고쳤다. **판정 통계를 문서에 적어 두는 이유**가 이것이다.

### 8.3 SSD 쪽과 비교

SSD 신뢰성 시험(Don의 chip reliability · PCIe 커버리지 경험)은 **샘플 수 × 시간**으로 신뢰도를 말한다. 드론 FW 회귀에서는 같은 생각을 **반복 횟수 × 조건 매트릭스**로 적용한다. 차이는 드론은 1회 비용이 층마다 6자릿수 이상 차이 난다는 것 (unit µs vs 비행 반나절) — 그래서 통계가 필요한 테스트는 최대한 **아래층으로** 내린다.

---

## 9. 테스트 설계 산출물 — 템플릿

방산 고객에게는 "테스트를 했다"보다 **"무엇을 왜 어떻게 했고 결과가 무엇이었는지 다시 만들 수 있다"**가 납품물이다 [추정]. 아래 다섯 가지가 최소 세트.

### 9.1 테스트 계획 (Test Plan) — 목차 템플릿

```text
TEST PLAN  <제품/FW 이름>  <버전>  문서 ID: TP-<nnn>  rev <x>
 1. 범위        : 대상 FW 버전 범위, 보드 리비전, 기체 구성, 제외 항목
 2. 참조        : 요구사항 문서 ID, 인터페이스 규격(CRSF/MSP/DShot), 이전 시험 보고
 3. 테스트 전략  : 피라미드 층별로 무엇을 어디서 (이 단원 3절 표 형태)
 4. 환경        : SITL 버전, HIL 벤치 ID와 구성, 계측기(모델·교정일), 리그, 비행장
 5. 진입 조건    : 빌드 통과, unit 100%, 알려진 차단 결함 없음
 6. 종료 조건    : 요구사항 커버리지 100%, 차단 결함 0, 통계 기준(8절) 충족
 7. 일정·자원    : 벤치 시간, 비행 시험 슬롯, 담당자
 8. 위험        : 장비 부족, 비행 날씨, 보드 수량
 9. 산출물      : RTM, 테스트 케이스, 결과 보고, 결함 목록, 원자료 위치
10. 안전        : 프롭·LiPo·RF 송신 규칙, 비상 정지 절차
```

### 9.2 요구사항 추적 매트릭스 (RTM)

| Req ID | 요구사항 (요약) | 층 | Test Case ID | 마지막 결과 | FW 해시 | 증거 위치 |
|---|---|---|---|---|---|---|
| SYS-FS-001 | RC 손실 시 stage 1 진입 ≤ X ms | L2 · L4 · L6 | TC-FS-001-U / -S / -H | PASS / PASS / PASS | `a1b2c3d` | `artifacts/a1b2c3d/fs/` |
| SYS-FS-002 | stage 2 후 모터 출력 0 (DROP) | L4 · L6 | TC-FS-002-S / -H | PASS / FAIL | `a1b2c3d` | DR-0142 |
| SYS-ARM-003 | arm 스위치 켠 채 부팅 시 arm 금지 | L2 · L6 | TC-ARM-003-U / -H | PASS / PASS | `a1b2c3d` | … |
| SYS-LAT-001 | CRSF→DShot p99 ≤ X ms, max ≤ Y ms | L6 | TC-LAT-001-H | PASS (n=5000) | `a1b2c3d` | `lat_hist.png` |
| SYS-PWR-004 | 레일 강하 후 안전 재부팅 · 원인 기록 | L6 | TC-PWR-004-H | PASS | `a1b2c3d` | 스코프 캡처 |

규칙: 모든 요구사항은 최소 하나의 테스트, 모든 테스트는 최소 하나의 요구사항(아니면 "탐색 테스트"로 따로 분류). 안전 요구사항은 **두 층 이상**.

### 9.3 테스트 케이스 템플릿

```text
TEST CASE  TC-FS-002-H
  Title       : RC loss → stage 2 DROP → motor output zero (HIL)
  Requirement : SYS-FS-002
  Layer       : L6 HIL bench
  Priority    : Safety-critical / per-merge smoke: no / nightly: yes / release: yes
  Setup       : bench HIL-03 (FC rev C + ELRS RX), PSU 16.8 V 3 A limit, NO PROPS,
                FW <hash>, config = configs/fs_drop.txt (diff all 적용 후 readback 확인)
                LA ch0 = CRSF into FC, ch1..4 = DShot M1..M4, 100 MS/s
  Preconditions: armed via CRSF arm switch, throttle 1300, 10 s stable, error counters snapshot
  Stimulus    : stop CRSF TX at t_stop (random phase), keep stopped 5 s
  Oracle      : (a) stage1 flag within PERIOD_RXDATA_RECOVERY + margin
                (b) DShot all motors = 0 / disarm at t_stop + failsafe_delay*100 ms ± tol
                (c) after CRSF resume: re-arm 금지 확인
                (d) no reset (reset reason unchanged), error counters delta 기록
  Limits      : tol = [확인 필요: 요구사항 값], repetitions N = 50 (nightly), 300 (release)
  Pass rule   : all N pass; any FAIL → defect; mixed → FLAKY (8.2)
  Cleanup     : disarm, PSU off, save LA capture + blackbox + CLI status/tasks diff
  Artifacts   : artifacts/<hash>/TC-FS-002-H/<run_id>/
  Notes       : 수치는 DUT 설정에서 계산 (매직 넘버 금지)
```

### 9.4 결함 보고 (Defect Report) 템플릿

```text
DEFECT  DR-0142    severity: S1 safety / S2 function / S3 performance / S4 cosmetic
  Title        : DROP failsafe leaves M3 at idle DShot value for ~X ms on HIL-03
  Found in     : TC-FS-002-H, run 17/50 (FLAKY 3/50)
  FW / config  : <hash>, configs/fs_drop.txt, bench HIL-03, board rev C, serial #
  Repro steps  : 1) … 2) … 3) …   (스크립트 명령 한 줄: runner run TC-FS-002-H -n 50)
  Expected     : all motors zero at t_stop + 1.5 s ± tol
  Actual       : M3 idle for X ms extra in 3/50 runs; others OK
  Evidence     : LA capture link, blackbox, CLI tasks before/after, histogram
  Reproducibility: 3/50 (6%), Wilson 95% interval …
  Lowest layer reproduced: L6 only (SITL 0/500) → timing/driver suspect
  Suspected area: DShot DMA update ordering [가설]
  Impact       : safety — motor not stopped promptly
  Owner / status / fix commit / verified-by test run
```

**좋은 결함 보고의 핵심**: 재현 명령 한 줄, 재현율(분수), **재현된 가장 낮은 층**, 원자료 링크. "가장 낮은 층"을 적으면 개발자가 어디서 디버그할지 바로 안다.

### 9.5 릴리스 자격(qualification) 체크리스트

- [ ] 태그된 커밋 해시 · 빌드 환경(컴파일러 버전, 컨테이너 이미지 해시) 기록
- [ ] L1: 모든 타깃 빌드, 경고 0, 정적 분석 신규 위반 0, 크기 한계 이내
- [ ] L2: unit 100% 통과, 커버리지 리포트 첨부 (안전 모듈 branch 커버리지 기록)
- [ ] L4: SITL 전체 시나리오 통과, 파라미터 매트릭스 통과
- [ ] L5/L6: HIL 전체 통과, 안전 테스트 N회 (8.1 기준) 통과, FLAKY 0 또는 원인 규명
- [ ] 지연 분포 리포트 (p50/p95/p99/max) — 이전 릴리스 대비 비교
- [ ] 성능 회귀: CPU load · task max · 스택 여유 — 이전 릴리스 대비
- [ ] soak ≥ 정해진 시간 (리셋 0)
- [ ] L7: 리그 시험 (모터 · 전원 · EMI 관찰) 통과
- [ ] L8: 비행 카드 전부 수행, 제어 품질 리포트 (step response · PSD · 포화) 이전 대비
- [ ] 열린 결함 목록 — S1 0, S2는 승인된 예외만
- [ ] RTM 100% (요구사항 → 테스트 → 결과 → 증거)
- [ ] 설정 기본값 diff (이전 릴리스 대비) 검토 · 승인
- [ ] 업데이트 경로 시험 (이전 버전 → 이번 버전, 설정 마이그레이션, 전원 차단)
- [ ] 릴리스 노트 · 알려진 제한사항
- [ ] 서명(승인자) · 날짜 · 증거 아카이브 위치

---

## 10. 방산 맥락 — 추적성 · 형상 관리 · 재현성 · 증거

> 이 절의 Neros 관련 내용은 전부 [추정]이다. 공고에는 방산 표준 이름이 직접 나오지 않았다 [확인 필요: 109개 공고 재검색].

### 10.1 무엇이 SSD 상용 검증과 다른가

| 항목 | 상용 (SSD 데이터센터 고객) | 방산 (드론) [추정] |
|---|---|---|
| 고객이 원하는 증거 | qual 결과 · 고객 테스트 통과 | 요구사항별 시험 증거 · 추적성 · 시험 절차서 |
| 형상 관리 | FW 버전 · 보드 리비전 | 같음 + 시험 장비 · 시험 SW 버전 · 설정까지 형상 항목 |
| 재현성 | 고객 이슈 재현 | 시험 결과를 **나중에 다른 사람이** 같은 절차로 재현 |
| 보안 | 고객 데이터 | 로그 · 소스 · 시험 데이터 반출 통제, 인원 자격 [추정] |
| 환경 | 데이터센터 온도 범위 | 넓은 온도 · 진동 · 충격 · EMI (MIL-STD-810H · 461 참고) |

### 10.2 표준 — 참고만 (Neros 적용 여부 모름)

| 표준 | 무엇 | 이 단원과의 연결 | 확인 상태 |
|---|---|---|---|
| **DO-178C** | 항공 소프트웨어 개발 보증. 소프트웨어 레벨 A~E (A가 가장 엄격). 레벨에 따라 구조 커버리지 요구 — A: MC/DC, B: decision, C: statement | 9.2 RTM, 3.4 커버리지, 요구사항 기반 테스트(T1) | 일반 지식, 원문은 유료 [확인 필요: 세부 목표 수] |
| DO-330 | 도구 자격(tool qualification) — 검증에 쓰는 도구를 믿을 근거 | **테스트 프레임워크 · 오라클 자체의 검증** (7.4 [1]) | 이름만 [확인 필요] |
| **DO-160G** | 항공 탑재 장비 환경 시험 (온도 · 진동 · 전원 · EMI 등) | T9 · T10 | 이름만 [확인 필요: 섹션 번호] |
| **MIL-STD-810H** | 군 장비 환경 시험 방법 (Method 500.6 저압, 501.7 고온, 502.7 저온, 514.8 진동, 516.8 충격 등) | T9 | 방법 번호는 공개 보고서로 확인, 원문 미확인 |
| MIL-STD-461 (G) | 군 장비 EMI/EMC | T9 EMI | [확인 필요: 현행 개정판] |
| MIL-STD-882E | 시스템 안전 (위험 분석) | 안전 요구사항 → 테스트 우선순위 | [확인 필요] |
| IEC 61508 | 일반 기능 안전 (SIL 1~4) | 안전 기능 · 진단 커버리지 개념 비유 | 일반 지식 |
| ISO 26262 | 자동차 기능 안전 (ASIL A~D) | HIL · fault injection · 요구사항 추적 문화의 비유 | 일반 지식 |

**주의**: 소형 군용 드론(특히 FPV급)이 DO-178C 인증을 받는 경우는 드물다고 알려져 있다 [추정 · 확인 필요]. 이 표준들은 "이렇게 엄격한 세계에서는 무엇을 증거로 삼는가"를 배우는 **참고서**로 쓰고, 면접 · 업무에서 "Neros가 DO-178C를 한다"고 말하면 안 된다.

### 10.3 재현성을 만드는 것들

| 무엇 | 어떻게 |
|---|---|
| FW | 커밋 해시 + 재현 가능한 빌드 (고정 컨테이너 이미지, 컴파일러 버전) |
| 설정 | `diff all` 결과를 테스트마다 저장, 테스트 시작 시 적용 후 readback 비교 |
| 테스트 코드 | 테스트 저장소 해시도 결과에 기록 |
| 벤치 | 벤치 ID · 배선 리비전 · 장비 시리얼 · 교정일 |
| 환경 | 온도 · 전원 설정 · 감쇠값 |
| 랜덤 | 퍼징 · 랜덤 위상의 seed 기록 |
| 원자료 | 캡처 · 로그를 결과와 같은 run_id로 보관 |

> SSD 연결: Don의 error reporting/handling scheme과 NVMe telemetry 경험은 "나중에 원인을 찾을 수 있도록 무엇을 남길지 설계"라는 같은 질문이다. 방산에서는 이것이 **고객에게 보여 줄 증거**가 된다는 점만 다르다.

---

## 11. SSD 검증에서는 이랬다 → 드론에서는 이렇다

| 주제 | SSD 검증 (Don 경험 범위) | 드론 FW 테스트 |
|---|---|---|
| 피라미드 모양 | FW unit · 시뮬레이터 → 보드 · 챔버 · 신뢰성 → 고객 qual | unit · SITL → HITL · HIL → 리그 → 비행 → 공장 → 필드 |
| "진짜" 층 | 챔버 안 eSSD + 호스트 워크로드 | 비행 시험 (사람 · 날씨 · 위험) |
| 무인 장시간 | 챔버 · 신뢰성 시험은 밤새 무인 | HIL soak는 무인 가능, 비행은 불가 → **아래층 비중을 키워야** |
| 판정 | 데이터 무결성 (비트 단위 정답) · 성능 수치 · 에러 카운터 | 제어 품질은 **모양** (특징량 + 허용 범위), 지연은 **분포** |
| 환경 스트레스 | 온도 챔버 (Don이 플랫폼 SDK 구축) | 온도 + **진동 · EMI를 기체가 스스로 만든다** |
| 전원 고장 | 전원 차단 시 데이터 보호 | brownout 시 모터 안전 · 재부팅 시 disarm |
| 프로토콜 | NVMe · PCIe (Don: PCIe 3/4/5 커버리지) | CRSF · MSP · DShot · MAVLink (작고 단순, 대신 실시간) |
| 관측 수단 | UART 로그 · NVMe telemetry · protocol analyzer | 블랙박스 · MSP/CLI · RTT · 로직 분석기 |
| 공장 | 대량 · 표준화 | 대량화 진행 중일 수 있음 [추정], Don의 factory test-node 경험 직결 |
| qual 매트릭스 | 용량 × FW × 온도 × 워크로드 × 호스트 | 보드 리비전 × FW × 설정(필터 · 루프 · 프로토콜) × 링크 × 기체 |
| 릴리스 게이트 | qual 통과 + 고객 승인 | 9.5 체크리스트 + 비행 카드 + (방산) 증거 패키지 |
| CI | Don은 web 기반 test automation 경험, GitLab CI/Jenkins 경험 없음 | GitLab CI · Jenkins · Bazel (JD) → 학습 필요 |

**한 문장 요약**: SSD에서 Don이 한 일(챔버 플랫폼 · 장시간 스케줄링 · 상태 모니터링 · 스트레스로 잠재 결함 찾기 · 공장 test-node)은 드론 피라미드의 **L6 HIL · soak · 환경 · L9 공장**에 거의 그대로 옮겨진다. 새로 배워야 하는 것은 **L4 SITL과 L7~L8의 제어 품질 오라클**, 그리고 CI 도구다.

---

## 12. 테스트 아이디어 / 체크리스트

### 12.1 새 FW 기능이 들어올 때 (층 고르기)

- [ ] 이 기능의 요구사항에 **숫자**가 있나? 없으면 받아낸다 (6.1)
- [ ] 순수 로직 부분은 unit으로 (파서 · 필터 · 상태 머신)
- [ ] 모드 · 정책 · 시나리오는 SITL로 (failsafe · 모드 전환)
- [ ] 실시간 · 드라이버 · 전기 신호가 걸리면 HIL로
- [ ] 진동 · 모터 · EMI가 걸리면 리그로
- [ ] 제어 품질이 바뀌면 비행 카드 + 블랙박스 오라클
- [ ] 안전 관련이면 두 층 이상 + 고장 주입 + 통계 횟수
- [ ] 공장에서 확인할 항목이 생기나? (보정 · 바인딩)
- [ ] 필드에서 무엇을 기록해야 나중에 원인을 찾나? (블랙박스 필드 · 카운터)

### 12.2 HIL 테스트 하나를 만들 때

- [ ] 자극을 알려진 시각에 넣는가, 위상은 랜덤인가
- [ ] 입력 · 출력을 같은 timebase로 잡는가
- [ ] 오라클 수치를 DUT 설정에서 계산하는가 (매직 넘버 없음)
- [ ] 주입 장치 자체가 동작했는지 증거를 남기는가
- [ ] "측정 못 함"을 FAIL로 처리하는가
- [ ] 전후 상태 덤프 (`status` · `tasks` · 에러 카운터)를 남기는가
- [ ] 반복 횟수와 판정 통계가 정해져 있는가
- [ ] 정리(cleanup) 후 다음 테스트에 상태가 새지 않는가 (설정 복원 · 전원 사이클)
- [ ] 안전: 프롭 없음 · 전류 제한 · 비상 정지

### 12.3 Neros 포크를 처음 볼 때 (가설 목록) [추정]

- [ ] 업스트림 Betaflight unit test 중 포크에서 꺼진/깨진 것이 있나
- [ ] 포크 변경 파일 vs 해당 unit test 커버리지
- [ ] `sitl_harness.py` 같은 SITL E2E가 포크 CI에 있나
- [ ] HIL 벤치가 몇 대, 무엇을 자동으로 판정하나
- [ ] 릴리스 게이트가 문서로 있나
- [ ] 비행 시험 로그가 자동 분석되나
- [ ] 결함 보고에 "재현된 가장 낮은 층"이 있나

---

## 13. 직접 해 볼 것

| # | 실습 | 산출물 | 하드웨어 | 시간 [추정] |
|---|---|---|---|---|
| 1 | `python3 -I code/step_response_oracle.py` 실행, `simulate_second_order`의 zeta · gain · noise를 바꿔 판정이 어떻게 바뀌는지 | 메모 + 한계값 감각 | 없음 | 30분 |
| 2 | Betaflight 클론 → `make test_help` · `make test` (Lima VM) → `make junittest`로 JUnit XML 확인 | 통과 로그, XML | 없음 | 반나절 |
| 3 | `rx_crsf_unittest.cc`, `flight_failsafe_unittest.cc` 읽고 **경계값 하나 · 금지 전이 하나** 테스트 추가 | 로컬 패치 | 없음 | 2~3시간 |
| 4 | `make TARGET=SITL` 빌드 → `src/test/sitl/sitl_harness.py --scenario all` 실행 (필요 빌드 플래그 확인) | 실행 로그, 시나리오 목록 정리 | 없음 | 반나절 |
| 5 | ArduPilot `./Tools/autotest/autotest.py build.Copter test.Copter.<하나>` 실행, 테스트 하나 구조 읽기 | "Betaflight에 옮기면" 1쪽 | 없음 | 반나절 |
| 6 | PX4 `DONT_RUN=1 make px4_sitl gazebo-classic mavsdk_tests` → SIH 설정으로 테스트 하나 | 실행 로그 | 없음 | 반나절 (빌드 무거움) |
| 7 | 6.2 `micros()` wrap 아이디어: 가짜 시계로 wrap 직전에서 시작하는 unit test 작성 (스케줄러 또는 failsafe) | 테스트 코드 | 없음 | 2시간 |
| 8 | 9.3 템플릿으로 TC 5개 작성 (RC loss · 깨진 CRSF · brownout · 지연 · 부팅 모터 0) + 9.2 RTM | 문서 | 없음 | 2시간 |
| 9 | HIL 미니 벤치: FC + USB-UART CRSF 주입 + 로직 분석기로 CRSF→DShot 지연 1000회 → `latency_summary()` | 히스토그램 | FC · 로직 분석기 · 전류 제한 PSU — **사기 전에 Don과 예산 확인**, 프롭 금지 | 1일 |
| 10 | 블랙박스 로그 하나(공개 샘플 [확인 필요])를 CSV로 바꿔 `step_metrics()`에 넣기 | step response 리포트 | 없음 | 반나절 |

---

## 14. 확인 안 된 것

- Betaflight CLI `tasks` · `status`의 정확한 출력 형식과 열 이름 [확인 필요]
- Betaflight `sitl_harness.py`가 업스트림 CI(`ci.yml`)에서 실제로 실행되는지 — `ci.yml`의 Test job에는 `checks`와 `test-all`만 보였다 [확인 필요: 다른 workflow]
- Betaflight SITL 현재 권장 시뮬레이터 (README는 Gazebo 8 · Ubuntu 16.04 기준으로 오래됨) [확인 필요]
- Betaflight에 PX4식 HITL 모드가 없는지 [확인 필요]
- CRSF 최대 프레임 길이 상수 · `rx_min_usec`/`rx_max_usec` 기본값 [확인 필요]
- 블랙박스 CSV 열 이름 (`setpoint[0]`, `gyroADC[0]`)과 debug 모드 이름 [확인 필요]
- ArduPilot unit test 명령 (`./waf tests` 등) [확인 필요]
- C/C++ property-based · mutation testing 도구 현황 (RapidCheck · Mull) [확인 필요]
- DO-178C 레벨별 목표 수 · DO-160G 섹션 번호 · MIL-STD-461 현행 개정판 · 810H Method 503 번호 [확인 필요]
- 소형 군용 드론에 어떤 표준이 실제로 요구되는지, Neros가 무엇을 따르는지 [추정 · 확인 불가]
- Neros 포크의 failsafe 설정 · CI 구조 · HIL 벤치 수 · 비행 시험 절차 [추정]
- Gemini가 말한 제품 이름(Archer, Archer AI, Crossbow GCS, APEX) [Gemini 주장 · 미확인]
- 이 단원의 시간/비용 추정치(층별 1회 비용, 게이트 목표 시간) [추정]

---

## 15. 참고 링크

확인에 쓴 자료 (2026-10-24 접근)

- Betaflight 저장소 (master): https://github.com/betaflight/betaflight
- Betaflight SITL 타깃 README: https://github.com/betaflight/betaflight/blob/master/src/platform/SIMULATOR/target/SITL/README.md
- Betaflight SITL E2E 하네스: https://github.com/betaflight/betaflight/blob/master/src/test/sitl/sitl_harness.py
- Betaflight unit tests: https://github.com/betaflight/betaflight/tree/master/src/test/unit
- Betaflight CI workflow: https://github.com/betaflight/betaflight/blob/master/.github/workflows/ci.yml
- Betaflight failsafe 소스: https://github.com/betaflight/betaflight/blob/master/src/main/flight/failsafe.c
- PX4 HITL: https://docs.px4.io/main/en/simulation/hitl.html
- PX4 Simulation (MAVLink 시뮬레이터 API · 포트 · SIH): https://docs.px4.io/main/en/simulation/
- PX4 MAVSDK 통합 테스트: https://docs.px4.io/main/en/test_and_ci/integration_testing_mavsdk.html
- ArduPilot AutoTest 프레임워크: https://ardupilot.org/dev/docs/the-ardupilot-autotest-framework.html
- MIL-STD-810H 방법 번호 확인용 공개 시험 보고서 예: https://asus.com/tw/event/militarygradereport/asusexpertbookb5_b5604cm.pdf
- MIL-STD-810H 개요: https://systelusa.com/blog/what-is-mil-std-810h/

더 읽을 것 (미확인 · 일반 참고)

- Hypothesis (Python property-based testing): https://hypothesis.readthedocs.io/
- googletest: https://github.com/google/googletest
- DoD ASSIST (MIL-STD 원문 검색): https://quicksearch.dla.mil/
- RTCA (DO-178C · DO-160G 발행처): https://www.rtca.org/

단원 링크: [R00](2026-10-24_R00_overview_and_gemini_corrections.md) · [R01](2026-10-24_R01_fte_vs_ssd_validation.md) · [R02](2026-10-24_R02_drone_system_and_open_source_map.md) · [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) · [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) · [R06](2026-10-24_R06_companion_video_linux_rtos.md) · [R08](2026-10-24_R08_hil_bench_design_instruments.md) · [R09](2026-10-24_R09_scalable_reusable_test_platform.md) · [R10](2026-10-24_R10_roadmap_labs_references.md)

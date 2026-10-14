# 🎯 N10 · 온사이트 예상 문제 뱅크 — 1:1 C/C++ · 개념 질문 · system design · 행동

> **온사이트 초대 확정** (Adam → Michael Honor → Jon Kotowski 통과, 날짜는 10-12 Devin 통화에서 결정). 온사이트 구성 [확인됨 2026-09-16 Devin · 2026-10-11 Don 재확인]: **Tour 30분 → 내 경력 발표 1시간 → 1:1 여러 개 (기본 C/C++, ring buffer 같은 코딩, generic system design)**. Adam이 **Senior Firmware Engineer, Platform** 쪽 엔지니어도 면접관으로 들어올 수 있다고 했다. 스크린에서 ring buffer를 이미 했으므로 **이번엔 ring buffer 자체는 건너뛸 가능성이 높다** [추정] — 대신 그 **확장**(ISR-safe, 패킷 파서) 또는 **다른 전형 문제**가 나올 것이다. 이 노트는 그 후보를 우선순위로 모으고, 각 문제를 **이미 있는 자료 · 실행 검증된 코드**로 연결한다. 발표는 [N11 스토리 워크북](2026-10-11_N11_presentation_story_workbook.md), 일정은 [P01 10일 계획](../plan/2026-10-11_P01_onsite_10day_plan.md).

## 0. 한눈에 — 라운드별로 무엇이 나오나

| 라운드 | 누가 [추정] | 무엇 | 준비 자료 |
|---|---|---|---|
| Tour 30분 | Adam 또는 Michael | 공장 · 테스트 랩 · 리그를 보며 대화 | 5절 · [N09](2026-10-01_N09_interviewers_behavioral.md) 4절 |
| 발표 1시간 | 패널 (Adam · Michael · 엔지니어 몇 명) | 내가 하는 일 · 가장 어려웠던 문제 · 디버깅 · bring-up | [N11](2026-10-11_N11_presentation_story_workbook.md) · [N03](2026-10-01_N03_presentation_1hour.md) |
| 1:1 C/C++ | FW 엔지니어 / Platform 엔지니어 | 코딩 1~2문제 + 개념 질문 | 1절 · 2절 |
| 1:1 system design | Platform 엔지니어 또는 Michael | generic · "necessary" system design | 3절 · [N06](2026-10-01_N06_generic_system_design.md) |
| (가능) 테스트 팀 1:1 | Test 엔지니어 · full stack | 테스트 설계 · HIL · Python | 4절 |

> 팀 정보 [확인됨 Don 10-08]: FW 17명 · Test 3명 · + full stack. 10-01 Michael: **테스트 FW가 가장 급함 · factory test FW 없음 · STM32**.

---

## 1. 1:1 코딩 예상 문제 — 우선순위

ring buffer는 이미 봤으니 [추정] 아래가 후보다. **S = 가장 유력**, A = 유력, B = 대비.

| 등급 | 문제 | 면접관이 할 말 (예상) | 왜 나올까 | 자료 (실행 검증) |
|---|---|---|---|---|
| **S** | **바이트 단위 패킷 파서 (상태 머신)** | "Bytes arrive one at a time from a UART ISR. Parse SYNC/LEN/PAYLOAD/CRC frames." | 스크린 Task 2(extract_packet)의 자연스러운 다음 단계 — ISR에서 버퍼 재스캔 없이 | [packet_parser_sm.c](../code/packet_parser_sm.c) (신규) · [L09 5절](../../technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html) |
| **S** | **비트 · 레지스터 조작** | "Write set/clear/read for a 3-bit field at bit 4 of a 32-bit register." · "Swap endianness." | 펌웨어 면접의 기본 중 기본 | [L03 2 · 6절](../../technicalScreenPrep/site/lecture/2026-10-04_L03_embedded_c_basics.html) · [practice 05](../../practice/html/05_embedded_core.html) |
| **S** | **CRC (테이블)** | "Implement CRC-8 / CRC-16 — table-driven. How do you verify it?" | 스크린에서 CRC.c가 테이블이었다 | [L10](../../technicalScreenPrep/site/lecture/2026-10-09_L10_crc_table.html) |
| **A** | **고정 블록 메모리 풀** | "We can't use malloc. Give me an allocator for fixed-size messages." | "no heap" 펌웨어의 단골 | [mem_pool.c](../code/mem_pool.c) (신규) |
| **A** | **소프트웨어 타이머 / tick wrap** | "Implement software timers on a 1 ms tick. Periodic and one-shot." | 시간 처리 + wrap 함정 | [soft_timer.c](../code/soft_timer.c) (신규) · [practice 06](../../practice/html/06_isr_timing.html) |
| **A** | **로깅 라이브러리 (Platform)** | "Design and implement a logging API for the MCU — levels, timestamps, cheap in an ISR." | Platform 공고의 logging · telemetry | [practice 01](../../practice/html/01_ring_logging.html) |
| **A** | **config store (Platform)** | "Store parameters in flash so a power cut never corrupts them." | Platform 공고의 configuration | [practice 03](../../practice/html/03_config_store.html) · [N06 문제 5](2026-10-01_N06_generic_system_design.md) |
| **A** | **텔레메트리 직렬화** | "Serialize this struct for the radio link — portable, versioned." | 패딩 · 엔디언 · 버전 | [practice 02](../../practice/html/02_telemetry_framing.html) · [L03 8절](../../technicalScreenPrep/site/lecture/2026-10-04_L03_embedded_c_basics.html) |
| A | ring buffer **확장**만 | "Make your ring buffer safe for an ISR producer." / "Feed it from DMA." | 스크린의 연장 질문 | [N05](2026-10-01_N05_ring_buffer_onsite.md) · [ring_buffer_spsc.c](../code/ring_buffer_spsc.c) · [dma_circular_rx.c](../code/dma_circular_rx.c) |
| B | 문자열 · 메모리 | memcpy / memmove / strcpy / atoi / reverse | 짧은 워밍업 | [L05 C 6개](../../technicalScreenPrep/site/lecture/2026-10-04_L05_warmup.html) |
| B | 연결 리스트 | reverse · 중간 삭제 · 사이클 검출 | 포인터 감각 | [L05 C 02](../../technicalScreenPrep/site/lecture/2026-10-04_L05_warmup.html) |
| B | IPC · 메시지 큐 (Platform) | "Two tasks exchange fixed messages — design the queue." | Platform 공고의 IPC | [practice 04](../../practice/html/04_ipc_budget.html) |
| B | 센서 · 액추에이터 경로 | "IMU burst read → filter → PWM/DShot." | 드론 도메인 | [practice 07](../../practice/html/07_sensors_actuators.html) |
| B | C++ 클래스 설계 | "Wrap this I2C sensor in a C++ class that's testable." | Platform · autonomy가 C++ | [L04 7 · 8절](../../technicalScreenPrep/site/lecture/2026-10-04_L04_embedded_cpp_basics.html) |

### 1.1 S등급 3개 — 핵심 세 줄씩

**패킷 파서 상태 머신**
- 상태 4개: `WAIT_SYNC → WAIT_LEN → IN_PAYLOAD → WAIT_CRC`. 바이트마다 O(1), 스트림을 다시 훑지 않는다 → ISR에서도 된다
- CRC는 바이트가 들어올 때마다 누적 (LEN + PAYLOAD). 끝에 받은 CRC와 비교
- **불가능한 LEN이면 바로 resync** (기다리면 링크가 멈춘다 — 스크린 재구성에서 찾은 버그). 카운터(`crc_errors`, `len_errors`)로 링크 품질을 보이게

**비트 · 레지스터**
- 필드 쓰기 = **read-modify-write**: `reg = (reg & ~(mask << shift)) | ((val & mask) << shift)`
- ISR도 같은 레지스터를 만지면 그 사이에 끼어든다 → 인터럽트 잠깐 끄기 또는 set/reset 전용 레지스터(STM32 `BSRR`)
- `volatile`은 "매번 읽고 쓴다"일 뿐 원자성은 아님

**CRC 테이블**
- `crc = T[crc ^ byte]` (CRC-8), `crc = (crc << 8) ^ T[(crc >> 8) ^ byte]` (CRC-16)
- 표는 `static const` → flash. `T[1]`이 poly
- 검증은 `"123456789"` check 값 (CRC-8/SMBUS `0xF4`, CRC-16/CCITT-FALSE `0x29B1`)

### 1.2 어떤 문제든 같은 진행 (스크린에서 통했던 방식)

| 시점 | 할 것 |
|---|---|
| 0–2분 | 질문 3개: 입력 형태 · 경계 · 정책 ("What should happen when …?") |
| 2–3분 | 자료구조와 함수 시그니처를 먼저 적는다 (스크린처럼 struct부터) |
| 구현 | 작게 · 이름을 스펙처럼 · 경계 먼저 |
| 직후 | **묻기 전에 테스트**: 정상 1 + 가장 위험한 경계 1~2 (C면 `assert` + main) |
| 확장 | ISR · DMA · 멀티코어 · 메모리 제약에서 무엇이 바뀌는지 말로 |

---

## 2. 개념 질문 (말로) — 가장 나올 법한 20개

| # | 질문 | 30초 답의 핵심 | 더 깊게 |
|---|---|---|---|
| 1 | `volatile`은 언제? 그걸로 충분한가? | 레지스터 · ISR 공유 변수. 원자성 · 순서 보장은 아님 → atomics / critical section | [L03 7절](../../technicalScreenPrep/site/lecture/2026-10-04_L03_embedded_c_basics.html) |
| 2 | ISR에서 하면 안 되는 것 | 블로킹 · printf · malloc · 긴 작업. 플래그 · 링에 넣고 main에서 처리 | [L03 16절](../../technicalScreenPrep/site/lecture/2026-10-04_L03_embedded_c_basics.html) |
| 3 | `static`의 의미 2가지 | 함수 안: 값 유지 / 파일 범위: 내부 링크 | [N04](2026-10-01_N04_c_cpp_fundamentals.md) |
| 4 | 변수는 메모리 어디에? | .text · .rodata · .data · .bss · stack · heap | [L03 15절](../../technicalScreenPrep/site/lecture/2026-10-04_L03_embedded_c_basics.html) |
| 5 | 구조체 패딩 · 직렬화 | 정렬 때문에 빈 바이트 → wire 포맷은 바이트 단위로 | [L03 8절](../../technicalScreenPrep/site/lecture/2026-10-04_L03_embedded_c_basics.html) |
| 6 | Cortex-M 리셋 시 무슨 일이? | 벡터 테이블 → SP · Reset_Handler → .data 복사 · .bss 0 → SystemInit → main | [N07 C14](2026-10-01_N07_embedded_quiz_bank.md) |
| 7 | HardFault 디버깅 | 스택 프레임에서 PC · LR · CFSR/HFSR · 주소 확인 → map 파일 | [N07 C15](2026-10-01_N07_embedded_quiz_bank.md) |
| 8 | DMA circular / double buffer | half · full 인터럽트, 처리하는 동안 다른 절반 채움 | [N07 C3](2026-10-01_N07_embedded_quiz_bank.md) |
| 9 | RTOS 우선순위 역전 | 낮은 태스크가 락을 쥐고, 중간이 선점 → priority inheritance | [N07 D절](2026-10-01_N07_embedded_quiz_bank.md) |
| 10 | 부트로더 · 펌웨어 업데이트 | A/B 슬롯 · 서명 검증 · 롤백 · 전원 차단 내성 | [N07 C9 · C10](2026-10-01_N07_embedded_quiz_bank.md) |
| 11 | 워치독 | IWDG/WWDG, "킥"은 건강 확인을 통과했을 때만 | [N07 C6](2026-10-01_N07_embedded_quiz_bank.md) |
| 12 | UART / I2C / SPI 차이 | 핀 · 속도 · 주소 · 동기 여부 (Michael이 이미 물음) | [N07 A1](2026-10-01_N07_embedded_quiz_bank.md) |
| 13 | I2C 버스 멈춤 복구 | SCL 9클럭 토글 → STOP | [N07 B4](2026-10-01_N07_embedded_quiz_bank.md) |
| 14 | CRC vs 체크섬 | 1비트 · burst 오류 검출 | [L10 15장](../../technicalScreenPrep/site/lecture/2026-10-09_L10_crc_table.html) |
| 15 | C++를 MCU에서 쓸 때 끄는 것 | 예외 · RTTI · heap · iostream | [L04 1절](../../technicalScreenPrep/site/lecture/2026-10-04_L04_embedded_cpp_basics.html) |
| 16 | RAII | 자원 수명 = 객체 수명 (critical section guard) | [L04 4절](../../technicalScreenPrep/site/lecture/2026-10-04_L04_embedded_cpp_basics.html) |
| 17 | 가상 함수 비용 · 대안 | vptr + 간접 호출 / 템플릿 주입 | [L04 7 · 8절](../../technicalScreenPrep/site/lecture/2026-10-04_L04_embedded_cpp_basics.html) |
| 18 | 하드웨어 없이 펌웨어 테스트 | HAL seam + mock, host 빌드 + ASan, HIL은 타이밍 | [L02](../../technicalScreenPrep/site/lecture/2026-10-04_L02_embedded_test_frameworks.html) |
| 19 | PCIe를 SW에서 어떻게? | enumeration · config space · BAR · MMIO · DMA · MSI (Michael 질문의 SW 관점 답) | [N07 A2](2026-10-01_N07_embedded_quiz_bank.md) |
| 20 | 캐시와 DMA (Cortex-M7) | invalidate / clean 또는 non-cacheable 영역 | [N07 C11](2026-10-01_N07_embedded_quiz_bank.md) |

---

## 3. System design — "generic / necessary" 문제 후보

> 리크루터 표현: **generic system design**(드론 설계가 아니라 일반적인 것) [확인됨]. Platform 엔지니어가 들어오면 **플랫폼 라이브러리 · 부트 · 업데이트**, Michael이면 **테스트 인프라**로 갈 가능성 [추정]. 진행 틀은 [N06 1절](2026-10-01_N06_generic_system_design.md) (요구사항 숫자 → 블록도 → 예산 → 실패 모드 → **테스트**).

| 등급 | 문제 (면접관이 할 말) | 핵심 결정 3개 | 자료 |
|---|---|---|---|
| **S** | "Design a logging system for a device with an MCU and a Linux companion." | 고정 크기 바이너리 레코드 + 링(ISR-safe) · 레벨 · 타임스탬프 동기 / 저장: 플래시 순환 + 크래시 시 보존 영역 / 업로드 · 압축 · 레이트 제한 | [N06 문제 1](2026-10-01_N06_generic_system_design.md) · [S05](../../technicalScreenPrep/site/design/2026-10-03_S05_platform_firmware.html) · [practice 01](../../practice/html/01_ring_logging.html) |
| **S** | "Design firmware update for a fleet — bootloader to rollout." | A/B 슬롯 + 서명 + health check 후 commit · 단계적 롤아웃 · 전원 차단 내성 테스트 | [N06 문제 2](2026-10-01_N06_generic_system_design.md) · [S06](../../technicalScreenPrep/site/design/2026-10-03_S06_embedded_linux_autonomy.html) |
| **S** | "Design the protocol between the flight MCU and the companion computer." | 프레이밍(SYNC · LEN · CRC) · 메시지 타입 · 버전 · ACK/재전송 정책 · 대역폭 예산 | [N06 문제 3](2026-10-01_N06_generic_system_design.md) · [packet_parser_sm.c](../code/packet_parser_sm.c) |
| **A** | "Design a parameter/config store." | A/B 레코드 + CRC + 시퀀스 번호 · 스키마 버전 · 기본값 | [N06 문제 5](2026-10-01_N06_generic_system_design.md) · [practice 03](../../practice/html/03_config_store.html) |
| **A** | "Design a sensor data acquisition pipeline." | 타임스탬프(하드웨어) · DMA + 더블 버퍼 · 백프레셔 · 드롭 카운터 | [N06 문제 4](2026-10-01_N06_generic_system_design.md) |
| **A** | "Design a HIL test farm + CI for our firmware." | 리그 lease · 스모크 · quarantine · 결과 파이프라인 · flaky 관리 | [N06 문제 7](2026-10-01_N06_generic_system_design.md) · [S08](../../technicalScreenPrep/site/design/2026-10-03_S08_system_integration_release.html) · [technicalScreenPrep N04](../../technicalScreenPrep/site/notes/2026-10-02_N04_system_design_test_tooling.html) |
| **A** | "Design factory test & provisioning for 1,000+ units a week." | 스테이션 분리 · takt · golden unit · 추적성(MES) | [N06 문제 6](2026-10-01_N06_generic_system_design.md) · [S07](../../technicalScreenPrep/site/design/2026-10-03_S07_manufacturing_factory_test.html) · [N08](2026-10-01_N08_factory_test_firmware.md) |
| B | "Design a health monitor / watchdog strategy." | 태스크별 체크인 · 워치독 킥 조건 · 리셋 원인 기록 · 안전 상태 | [N07 C6](2026-10-01_N07_embedded_quiz_bank.md) |
| B | "Design a pub-sub message bus on an MCU." | 고정 토픽 · 정적 할당 · 구독자 큐 · 우선순위 · 오버런 정책 | [practice 04](../../practice/html/04_ipc_budget.html) · [mem_pool.c](../code/mem_pool.c) |
| B | "Design the build system for multiple boards and targets." | 크로스 컴파일 · 보드별 설정 · 재현 가능한 빌드 · 매트릭스 CI (Bazel은 개념만) | [S05](../../technicalScreenPrep/site/design/2026-10-03_S05_platform_firmware.html) |

### 3.1 Platform 엔지니어가 들어오면 — 그 사람의 관점 [추정]

| 그 사람이 매일 하는 일 (공고) | 물을 법한 것 | 답의 무게중심 |
|---|---|---|
| logging · telemetry · IPC · config **라이브러리** | "API를 어떻게 설계하나 — 다른 팀이 쓰는데" | **인터페이스 안정성**(버전 · 하위 호환), 자원 예산(RAM · CPU), 테스트 가능성 |
| MCU + Linux 둘 다 | "같은 라이브러리를 두 타깃에서" | HAL 경계 · 조건부 컴파일 최소화 · host 테스트 |
| 크로스 컴파일 빌드 (Bazel) | "빌드 매트릭스를 어떻게 지키나" | CI에서 소비자 팀 빌드까지 · footprint diff |

> 한 문장: **"For a platform library, the customers are other engineers — so I design for a stable interface, a fixed resource budget, and tests that run on the host in every PR."**

---

## 4. 테스트 팀 1:1이 끼면 (Michael · Test 엔지니어 · full stack)

| 질문 | 자료 |
|---|---|
| "How would you test this firmware module without hardware?" | [L02](../../technicalScreenPrep/site/lecture/2026-10-04_L02_embedded_test_frameworks.html) · [L04 8절](../../technicalScreenPrep/site/lecture/2026-10-04_L04_embedded_cpp_basics.html) |
| "Design a HIL rig for the flight controller." | [S01](../../technicalScreenPrep/site/design/2026-10-03_S01_flight_software.html) · [FTE N02](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N02_hil_for_drones.html) |
| "Write pytest tests for this." | [L08](../../technicalScreenPrep/site/lecture/2026-10-08_L08_pytest_from_zero.html) · [L00 Part 2](../../technicalScreenPrep/site/lecture/2026-10-04_L00_special_lecture.html) |
| "What would you build first here?" (factory test FW 없음) | [S09 카드 16](../../technicalScreenPrep/site/design/2026-10-03_S09_scenario_cards.html) · [N08](2026-10-01_N08_factory_test_firmware.md) |
| "A test fails 1 in 20 runs." | [FTE N08 디버깅 시나리오](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N08_test_and_debug_scenarios.html) |

## 5. Tour · 행동 질문

| 질문 | 답의 방향 |
|---|---|
| (Tour 중) "What do you notice?" | 생산 라인에서 **어디서 테스트하는지 · 무엇을 측정하는지**를 묻는다 — 테스트 엔지니어의 눈 |
| "Why Neros? Why test?" | "I've always been the firmware engineer who builds the test infrastructure around it — and here factory test and HIL need to be built from scratch." |
| "Tell me about a conflict / disagreement." | (Don: FW vs HW 팀 경계 사례) — 데이터로 합의한 이야기 |
| "A time you were wrong." | (Don: 가설이 틀렸고 측정으로 바로잡은 사례) |
| "How do you decide something is done?" | Michael 라운드 답 유지: 80%에서 내보내고 피드백으로 보완 + **회귀 테스트로 고정** |
| "Leaving Apple after ~10 months?" | (Don: 이미 Devin · Adam에게 한 설명과 같게) |

- 행동 질문 · 면접관별 대응 원본: [N09](2026-10-01_N09_interviewers_behavioral.md)

## 6. 역질문 — 면접관별

| 면접관 | 질문 |
|---|---|
| Adam (Director) | "Where do you see firmware test sitting a year from now — part of each team, or a central platform?" |
| Michael (Test lead) | "What's the first thing you'd want the new hire to own — factory test firmware, the HIL farm, or CI?" |
| Platform 엔지니어 | "How do teams consume the platform libraries today, and where do they break most often?" |
| Test 엔지니어 · full stack | "What does a failing test look like today from the rig to the dashboard?" |

## 체크

- [ ] 1절 S등급 3개를 빈 파일로 (패킷 파서 · 비트 필드 · CRC 테이블)
- [ ] 1절 A등급 중 2개 (메모리 풀 · 소프트 타이머) `make -C code test` 통과본과 비교
- [ ] 2절 개념 20개를 가리고 30초씩
- [ ] 3절 S등급 3개를 10분 타이머로 화이트보드
- [ ] 3.1절 Platform 한 문장을 영어로

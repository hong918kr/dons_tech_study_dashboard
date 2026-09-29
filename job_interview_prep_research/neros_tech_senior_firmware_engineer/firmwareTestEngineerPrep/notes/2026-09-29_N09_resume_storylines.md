# 📖 N09 · 스토리라인 2탄 — 레쥬메 bullet을 시나리오 스토리로

> N08이 "드론 문제 → 어떻게 테스트·디버그하나"였다면, N09는 거꾸로 **Don의 레쥬메 bullet 하나하나를 테스트·디버깅 스토리로** 풀어 쓴 것이다. bullet마다 ① 레쥬메 원문 ② 스토리 뼈대(STAR) ③ Neros 드론으로 옮기면 ④ 영어 60초 버전 ⑤ 꼬리 질문 ⑥ 채울 칸이 있다. 13개 스토리를 **하나의 커리어 줄거리**로 엮는 법도 맨 앞에 정리했다.

## ⚠️ 먼저 읽을 것 — 이 노트의 스토리는 "재구성 예시"다

- 레쥬메 문장은 **사실**이다. 하지만 스토리 속 구체적인 상황(어떤 버그, 어떤 증상, 몇 대, 몇 %)은 레쥬메에 없기 때문에 **그 역할에서 흔히 일어나는 전형적인 형태로 재구성**했다
- 재구성한 부분은 `〔예시〕`로 표시했다. **실제로 겪은 일로 바꿔서** 써야 한다. 기억과 다르면 그 부분은 말하지 않는다
- 숫자는 전부 `(Don: …)` 칸으로 비워 뒀다. 기억나는 숫자만 쓴다. 면접관은 숫자 하나를 더 파고든다
- Apple 이야기는 칩 이름, 제품명, 일정, 내부 툴 이름 없이 **일반화해서** 말한다

## 0. 전체 줄거리 — 13개 스토리를 한 줄로 엮기

Don의 경력은 "테스트 → 개발 → 통합"으로 바뀐 것처럼 보이지만, 테스트 포지션 관점에서는 **한 줄로 이어진다**:

| 시기 | 역할 | 테스트 관점의 한 줄 | 스토리 |
|---|---|---|---|
| 2018–2021 | SK hynix Application SW | **테스트 인프라를 만드는 사람**으로 시작: 챔버 자동화, SDK, 사내 테스트 인프라 | S12, S13 |
| 2021–2022 | SK hynix Senior FW | **테스트할 수 있게 FW를 만드는 사람**: telemetry 디버그, reliability system, shmoo, 알고리즘 검증 | S8, S9, S10, S11 |
| 2022–2025 | Solidigm Staff FW | **실리콘 이전부터 검증하는 사람**: FPGA bring-up, SoC verification, 성능 튜닝, 에러 처리, 고객 요구 기능 | S4, S5, S6, S7 |
| 2025– | Apple Embedded Systems | **시스템 레벨에서 결함을 잡는 사람**: 새 실리콘 통합 루트코즈, factory test-node, 마진 sign-off | S1, S2, S3 |

> **영어 한 줄 요약**: "I started my career building test infrastructure, then spent years writing firmware designed to be testable and debuggable, and now I catch integration defects at the system level before mass production. Testing real hardware is the thread through all of it."

**30분~45분 인터뷰에서 쓸 3개만 고르면**: S12(챔버 SDK: HIL과 프레임워크) · S2(factory test-node: stress와 latent defect) · S1 또는 S11(루트코즈나 마진: 디버깅 실력)

## 질문 → 스토리 빠른 매핑

| 면접 질문 | 1순위 | 2순위 |
|---|---|---|
| Tell me about a test system/framework you built | S12 | S10 |
| HIL experience? | S12 | S2 |
| Hardest bug / root cause | S1 | S4 |
| How do you decide what to test / coverage? | S10 | S2 |
| Flaky / intermittent failure | S1 | S11 |
| Margin, stress, environmental testing | S11 | S3 |
| Test before hardware exists (SIL/simulation) | S4 | S9 |
| Performance testing / regression | S5 | S7 |
| Observability, logging, telemetry | S8 | S6 |
| Fault injection / negative testing | S6 | S9 |
| Requirements from a customer / acceptance test | S7 | S8 |
| Cross-team collaboration | S13 | S1 |
| Your users were other engineers | S12 | S8 |

---

## Part A · Apple (2025-12 ~ 현재) — 시스템 레벨 결함

### S1. 새 실리콘이 전체 시스템을 만날 때 나는 인터페이스 실패 루트코즈

**📌 레쥬메**: "Lead integration of new silicon into shipping consumer platforms – owning root-cause analysis of fundamental and interface level failures (PCIe, I2C, SPMI, RFFE) that surface when a new chip meets the full HW/SW system"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 새 RF 칩이 실제 제품 플랫폼에 들어갔다. 칩 단독 validation은 통과했는데, 전체 시스템에서 〔예시: 특정 전원 상태 전환 뒤 제어 버스 응답이 간헐적으로 없음〕 |
| Task | 칩, 보드, FW, 드라이버 중 **어느 쪽 문제인지 아무도 소유하지 않은** 실패를 끝까지 추적한다 |
| Action | ① 재현 조건을 좁힘: 〔예시: 전원 전환 + 특정 트래픽 동시〕 ② DSO와 protocol analyzer로 실패 순간의 버스 트랜잭션과 신호 캡처 ③ 가설을 하나씩 제거: 타이밍, 전압, 시퀀스, FW 상태 ④ 원인 팀(칩 벤더, 보드, FW)에 데이터로 설명 |
| Result | 〔예시: 시퀀스·타이밍 수정〕 (Don: 실제 결과) + **같은 조건을 재현하는 테스트를 test-node에 추가** |

**🔁 Neros 드론으로 옮기면**: 새 IMU나 두 번째 공급처 부품(second-source)이 들어간 보드 변형에서 모터를 돌릴 때만 센서가 가끔 끊긴다 → N08의 D2와 같은 구조다

**🎤 영어 60초**

> At Apple I own root cause for failures that show up when a new chip meets the full system — interface-level problems on buses like I2C, SPMI, RFFE, and PCIe. The hard part is that the chip passed its own validation, so nobody owns the failure. My approach is to first make it reproducible and narrow the trigger conditions, then capture the failing transaction on a protocol analyzer and the signals on a scope, and eliminate hypotheses one variable at a time — timing, voltage, sequencing, firmware state. Once I have evidence, it's much easier to get the right team to fix it. And the last step is always turning that trigger condition into a test case, so it's caught automatically on the next build.

**❓ 꼬리 질문**
- "How did you narrow it down?" → 한 번에 변수 하나만 바꾼 과정을 순서대로 말한다
- "How did you convince the other team?" → 캡처 데이터를 보여 줬다. 의견이 아니라 증거로
- "What would you automate from that?" → 재현 조건을 stress 테스트로 만들어 factory test-node나 회귀에 넣는다

**✍️ 채울 칸**: (Don: 실제 버스 종류 하나 / 증상 한 줄 / 결정적 증거 / 해결 / 걸린 기간)

### S2. Factory test-node 아키텍처 — stress로 latent defect를 MP 전에

**📌 레쥬메**: "Drive the full cycle: Silicon/system bring up -> NPI -> MP - designing and leading factory test-node architecture: Build scenario and stress-based case studies to proactively [surface] latent silicon/integration defects before MP"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | bring-up → NPI → MP로 가면서 생산 대수가 급격히 늘어난다. 기능 테스트만으로는 **드물게 나오는 결함**을 못 잡는다 |
| Task | 공장 테스트 단계(test-node)에서 무엇을, 어떤 순서로, 얼마나 오래 테스트할지 설계한다 |
| Action | ① 과거 실패와 인터페이스별 위험으로 시나리오 도출 ② **stress 겹치기**: 〔예시: 전원 전환 반복, 동시 트래픽, 온도 조건〕 ③ 테스트 시간과 커버리지의 균형 (공장은 시간이 곧 비용) ④ 결과를 설계팀에 피드백 |
| Result | (Don: MP 전에 잡은 결함 유형 — 공개 가능한 수준으로) |

**🔁 Neros 드론으로 옮기면**: 주당 약 1,200대 생산. 출하 전 **EOL(End-of-Line) 테스트**와 FW 릴리즈 회귀가 같은 문제다. "몇 초 안에 무엇을 확인해야 불량 기체가 전장에 안 가나". **Neros와 가장 강하게 연결되는 스토리**다

**🎤 영어 60초**

> At Apple I designed the factory test-node architecture for RF chipset integration, going from bring-up through NPI to mass production. The insight that drove it: most latent defects don't show up in a functional test — they need stress. So I built scenario- and stress-based cases, stacking conditions like repeated power transitions and concurrent traffic, to surface silicon and integration defects before MP. In a factory you also have a time budget per unit, so part of the design is choosing which stresses give the most coverage per second. At Neros's production rate, I think the same trade-off applies both to end-of-line testing and to the firmware release gate.

**❓ 꼬리 질문**
- "How did you choose which tests go into the factory?" → 위험(발생 가능성 × 영향) + 테스트 시간당 검출력
- "What escaped?" → 정직하게. 놓친 것이 있었다면 그 뒤에 무엇을 추가했는지
- "How is that different from firmware CI testing?" → 공장은 **유닛 불량**을, CI는 **코드 회귀**를 잡는다. 같은 HIL 리그와 라이브러리를 둘 다에 재사용할 수 있다

**✍️ 채울 칸**: (Don: test-node에 넣은 stress 종류 2~3개 / 잡은 결함 유형 / 테스트 시간 제약)

### S3. 신호·버스 레벨 디버그와 하드웨어 safety margin sign-off

**📌 레쥬메**: "Debug at signal, bus, and system level with DSOs and protocol Analyzer: analyze and sign off on hardware safety margins (reliability vs performance/power)"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 인터페이스가 "동작한다"는 것과 "**양산에서 충분한 여유를 가지고** 동작한다"는 다르다 |
| Task | 신뢰성과 성능·전력 사이에서 마진이 충분한지 판단하고 sign-off한다 |
| Action | 〔예시: 전압·타이밍 조건을 스윕하며 통과/실패 경계를 찾고, 정상 동작점과 경계 사이 거리를 측정〕 DSO로 신호 품질, protocol analyzer로 트랜잭션 오류를 함께 본다 |
| Result | (Don: 마진이 부족해 조정한 사례가 있었는지, 혹은 데이터로 sign-off한 과정) |

**🔁 Neros 드론으로 옮기면**: 배터리 전압 범위 전체, 모터 급가속 전압 강하, 온도 → 이 조건들에서 IMU SPI, 수신기 UART, 센서 I2C의 **마진**을 확인하는 테스트 (N08의 T3, T8)

**🎤 영어 45초**

> Part of my role at Apple is signing off hardware margins — reliability versus performance and power. "It works" isn't enough; the question is how far the operating point is from the failure edge. I measure that by sweeping conditions until things break, with a scope on the signals and a protocol analyzer on the bus. For a drone I'd apply the same thinking to the battery voltage range and motor-induced droop: find the edge, and make sure the normal operating point has real margin from it.

**✍️ 채울 칸**: (Don: 어떤 조건을 스윕했는지 / 판단 기준)

---

## Part B · Solidigm Staff FW (2022–2025) — 실리콘 이전부터 검증

### S4. FPGA 단계 FW bring-up과 SoC verification (RTL freeze 전)

**📌 레쥬메**: "Performed new FPGA Image/FW bring-up ARM Cortex R8/R82/M0+ and PCIe/NVMe subsystem IP" · "Performed Soc verification (e.g., multi-core interconnection I2C, SPI, DMA, PCIe Controller, and SRAM/DRAM bring-up in the early stage chip development phase with FPGA before RTL freeze)"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 칩이 아직 없다. FPGA에 올린 RTL 위에서 FW를 올려야 하고, RTL freeze 전에 하드웨어 버그를 찾아야 수정 비용이 싸다 |
| Task | 멀티코어(R8/R82/M0+) FW를 FPGA에서 부팅시키고, I2C, SPI, DMA, PCIe 컨트롤러, SRAM/DRAM을 하나씩 검증한다 |
| Action | ① 가장 작은 단위부터: 레지스터 읽기/쓰기 → 단일 트랜잭션 → DMA → 멀티코어 동시 접근 ② 각 블록마다 **검증용 테스트 FW**를 만들어 반복 실행 ③ 실패 시 FW 버그인지 RTL 버그인지 FPGA 이미지 문제인지 가름 (Trace32, JTAG) |
| Result | 〔예시: RTL freeze 전에 버그를 설계팀에 넘김〕 (Don: 실제로 찾은 HW 이슈 종류) |

**🔁 Neros 드론으로 옮기면**: 새 FC 보드 revision이 오면 **bring-up 테스트 스위트**를 가장 먼저 돌린다: 각 버스와 주변장치의 smoke 테스트 → 그다음 기능 테스트. 그리고 "하드웨어가 나오기 전에 테스트한다"는 발상은 **SITL**(N02)과 같다

**🎤 영어 60초**

> At Solidigm I brought up firmware on FPGA images of our next-generation controller before RTL freeze — multi-core Cortex-R8, R82, and M0+, plus the PCIe and NVMe subsystems. That was really verification work: I validated I2C, SPI, DMA, the PCIe controller, and SRAM and DRAM block by block, starting from single register accesses up to concurrent multi-core traffic. The key skill was deciding whether a failure was firmware, RTL, or the FPGA build — because a real RTL bug found before freeze is cheap, and after tape-out it's very expensive. For a new drone board revision I'd do the same thing: a bring-up suite that exercises every bus before any functional testing.

**❓ 꼬리 질문**
- "How did you tell a firmware bug from an RTL bug?" → 가장 작은 재현 케이스로 줄이고, 레지스터 레벨에서 기대값과 실제값 비교, 설계 엔지니어와 파형(시뮬레이션) 대조
- "Did you automate it?" → (Don: 반복 실행을 어떻게 했는지)

**✍️ 채울 칸**: (Don: 가장 기억나는 FPGA 단계 이슈 하나 / 어떻게 판별했나)

### S5. 실리콘 validation 팀과 FW 성능 튜닝

**📌 레쥬메**: "Conducted firmware performance tuning with the silicon validation team through the prototyping platform" · "Implemented firmware algorithms flow through the internal simulation tool regardless of device performance"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 성능 목표가 있는데, 프로토타입 플랫폼과 실제 실리콘은 속도가 다르다 |
| Task | FW 성능 병목을 찾고, 측정이 믿을 만한지 확인한다 |
| Action | ① 측정 방법부터 합의 (무엇을, 어디서, 몇 번) ② 병목 구간 계측 ③ 알고리즘 흐름은 **시뮬레이션 툴에서 먼저 검증**해 하드웨어 속도와 무관하게 로직 정확성을 확인 ④ 변경 전후 비교 |
| Result | (Don: 개선 폭이나 발견한 병목 종류) |

**🔁 Neros 드론으로 옮기면**: **성능 회귀 테스트**. 제어 루프 시간, 텔레메트리 rate, CPU 사용률을 매 빌드 측정해서 느려지면 실패 처리 (N08의 D9). 그리고 "로직은 시뮬레이션에서, 타이밍은 하드웨어에서" 나눠 검증하는 원칙 = SITL과 HIL의 역할 분담

**🎤 영어 45초**

> I tuned firmware performance together with the silicon validation team on a prototyping platform. The lesson that carries over is to separate what you validate where: algorithm correctness in simulation, where it doesn't depend on device speed, and timing and throughput on real hardware. For a flight controller I'd track loop time, CPU load, and telemetry rates on every build, so a performance regression fails CI instead of showing up in flight.

**✍️ 채울 칸**: (Don: 측정 지표 / 병목 사례)

### S6. Production FW의 error reporting/handling scheme

**📌 레쥬메**: "Implemented the feature of an error reporting/handling scheme in the production firmware"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 현장에서 문제가 나면 **디바이스가 스스로 무엇이 잘못됐는지 남겨야** 원인을 찾을 수 있다 |
| Task | 에러를 분류하고 보고하고 처리하는 체계를 production FW에 구현한다 |
| Action | 〔예시: 에러 코드 체계, 심각도별 처리(재시도·복구·보고), 에러 정보 저장 위치〕 ② **테스트는 고장 주입으로**: 에러 경로는 정상 동작에서 거의 실행되지 않으므로 일부러 에러를 만들어 경로를 검증 |
| Result | (Don: 여러 모듈이 이 scheme을 어떻게 썼는지) |

**🔁 Neros 드론으로 옮기면**: FC의 리셋 원인 기록, 에러 카운터, 크래시 정보 → 테스트 프레임워크가 **매 테스트마다 수집**해서 "assert는 통과했지만 에러 카운터가 올라갔다"도 잡는다 (N08의 D7). 에러 경로 테스트 = 고장 주입

**🎤 영어 45초**

> I implemented the error reporting and handling scheme in our production SSD firmware. What that taught me as a tester is that error paths are the least-executed code in the product, so they need deliberate fault injection — otherwise the first time they run is in the field. On a drone I'd make the test framework read error counters and the reset cause after every test, so a test fails if the device logged an error, even when the functional assertions passed.

**✍️ 채울 칸**: (Don: 에러 분류 방식 / 테스트한 방법)

### S7. Google/Meta 요구사항 기반 NVMe 2.0 성능 critical 기능

**📌 레쥬메**: "Designed/Implemented a performance-critical customer (Google/Meta) requirement features in NVMe 2.0"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 고객이 스펙과 성능 요구사항을 정해 줬다. 합격 여부는 고객 기준으로 판정된다 |
| Task | 요구사항을 기능으로 구현하고, **요구사항 하나하나가 검증됐다는 증거**를 만든다 |
| Action | 〔예시: 요구사항 → 테스트 케이스 추적표, 스펙 경계값 테스트, 성능 측정 조건 고정〕 |
| Result | (Don: 고객 qualification 결과) |

**🔁 Neros 드론으로 옮기면**: 고객은 US Army, USMC다. 요구사항 → 테스트 → 결과를 **추적 가능하게** 남기는 게 방산에서는 특히 중요하다 [추정]. JD의 "Documentation: test plans and results"와 연결된다

**🎤 영어 40초**

> I built performance-critical NVMe 2.0 features driven by requirements from hyperscale customers. When a customer defines pass or fail, you need traceability: every requirement maps to a test, and every release has results tied to the exact firmware build. I'd expect the same discipline for military customers.

**✍️ 채울 칸**: (Don: 공개 가능한 수준의 기능 종류 / 검증 방식)

---

## Part C · SK hynix Senior FW (2021–2022) — 테스트할 수 있게 만드는 FW

### S8. NVMe telemetry 기반 time-sensitive 디버그 기능 (MS/Dell/HPE)

**📌 레쥬메**: "Designed, implemented, and launched a time-sensitive debugging feature based on NVMe telemetry protocol to qualify the customer's data center requirement policy in an ASIC environment (Microsoft, DELL, HPE)"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 데이터센터 고객 요구 정책에 맞게, 문제가 났을 때 드라이브의 내부 상태를 **표준 telemetry 인터페이스로** 꺼낼 수 있어야 했다 |
| Task | time-sensitive: 문제 시점의 정보가 사라지기 전에 잡아서 남긴다 |
| Action | 〔예시: 무엇을 남길지(상태, 이벤트, 타임스탬프), 버퍼 크기, 캡처 트리거, 성능 영향 최소화〕 ② ASIC 환경에서 검증 ③ 고객 qualification 대응 |
| Result | 출시, 고객 qualification에 사용 (Don: 구체 결과) |

**🔁 Neros 드론으로 옮기면**: HIL 테스트가 실패했을 때 **무엇을 자동으로 남기나**: FC 블랙박스 로그, 텔레메트리, 버스 캡처, FW 해시. "실패 순간의 정보를 잃지 않는다"는 원칙 (N08의 D6)

**🎤 영어 60초**

> At SK hynix I designed and shipped a time-sensitive debug feature built on the NVMe telemetry protocol, used to qualify drives against data-center requirements for customers like Microsoft, Dell, and HPE. The core problem was capturing the right internal state at the moment something went wrong, before it was lost, without hurting performance. That's exactly the mindset I'd bring to HIL: when a test fails, the framework should automatically preserve the flight controller logs, telemetry, bus captures, and the firmware hash — because an intermittent failure you can't reconstruct is a failure you can't fix.

**❓ 꼬리 질문**
- "How did you decide what to log?" → 디버깅에 실제로 필요했던 질문에서 거꾸로
- "Performance overhead?" → (Don: 실제 방식)

**✍️ 채울 칸**: (Don: 캡처한 정보 종류 / 트리거 방식 / 오버헤드 관리)

### S9. Xtensa NAND 데이터패스 FW와 media defense 알고리즘 (FPGA)

**📌 레쥬메**: "Implemented, tested, and debugged Xtensa-based FW of the primary data path in the NAND interface layer and designed and implemented media defense algorithms on the FPGA, and committed to hardware bring-up"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | NAND는 에러가 나는 매체다. media defense 알고리즘은 **에러가 날 때** 제대로 동작해야 의미가 있다 |
| Task | 데이터패스 FW와 방어 알고리즘을 구현하고 검증한다 |
| Action | 〔예시: 정상 데이터로는 방어 경로가 안 타므로, 에러 조건을 만들어 알고리즘이 반응하는지 확인〕 FPGA에서 반복 검증, 하드웨어 bring-up 참여 |
| Result | (Don: 결과) |

**🔁 Neros 드론으로 옮기면**: 드론의 방어 로직(failsafe, 센서 이상 감지, 링크 손실 처리)도 **이상 상황을 주입해야** 테스트된다. RC 링크 끊기, 센서 값 고정, 전압 강하 (N08의 T4, N02)

**🎤 영어 40초**

> I implemented the NAND data-path firmware on Xtensa and designed media defense algorithms on FPGA. Defense logic only matters when things go wrong, so testing it means creating the bad conditions on purpose. On a drone, failsafe and sensor-fault handling are the same: you have to inject link loss, stuck sensors, and voltage sag to know they work.

**✍️ 채울 칸**: (Don: 알고리즘이 막는 에러 종류 / 검증 방법)

### S10. 고속 인터페이스 커버리지를 위한 chip reliability system

**📌 레쥬메**: "Designed and built a chip reliability system for high-speed interface testing coverage (V6/7 NAND, PCIe 3/4/5)"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | NAND 세대(V6, V7)와 PCIe 세대(3, 4, 5)가 여러 개다. 조합이 늘어날수록 수동 테스트로는 커버리지를 설명할 수 없다 |
| Task | 인터페이스 세대별 신뢰성 테스트를 **체계적으로 커버하는 시스템**을 설계·구축한다 |
| Action | 〔예시: 커버리지 매트릭스(인터페이스 × 속도 × 조건), 테스트 조합 자동 생성, 결과 수집과 비교〕 |
| Result | (Don: 커버한 범위, 찾은 이슈) |

**🔁 Neros 드론으로 옮기면**: 제품군(Archer, Archer AI, Bandit, GCS, 라디오) × 보드 revision × FW 버전 × 수신기 종류 → **테스트 매트릭스**. pytest parametrize와 CI matrix가 바로 이것이다 (N04, N05)

**🎤 영어 60초**

> I designed and built a chip reliability system for high-speed interface coverage across NAND V6 and V7 and PCIe Gen 3, 4, and 5. With that many generations and speeds, the real question becomes: can you prove what you've covered? So coverage was a matrix — interface, generation, speed, condition — and the system's job was to run that matrix and make gaps visible. At Neros I'd expect a similar matrix — product line, board revision, receiver type, firmware version — and that maps naturally onto pytest parametrization and CI matrix jobs.

**❓ 꼬리 질문**
- "Matrix explosion — how do you keep it tractable?" → 위험 기반 우선순위, pairwise 조합, MR마다 smoke·야간 전체
- "Line coverage?" → 이 맥락에서는 요구사항·위험·조합 커버리지가 더 의미 있다

**✍️ 채울 칸**: (Don: 매트릭스 축 / 실행 방식 / 찾은 이슈)

### S11. SoC·SI 팀과 shmoo 테스트, module health monitoring debug FW

**📌 레쥬메**: "Collaborated with the SoC/Signal Integrity team to build debug firmware features (e.g., shmoo testing, module health monitoring sequence regarding the high-speed interface engineering between SoC, DRAM, and NAND)"

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | SoC–DRAM–NAND 사이 고속 인터페이스는 전압과 타이밍 마진이 좁다. SI 팀은 마진 데이터가 필요하다 |
| Task | 마진을 측정할 수 있는 **debug FW 기능**을 만든다 |
| Action | **shmoo**: 전압·타이밍 파라미터를 2차원으로 스윕하며 통과/실패를 기록 → 통과 영역(eye)의 크기와 동작점 위치 확인. **health monitoring**: 인터페이스 상태를 주기적으로 점검하는 시퀀스 |
| Result | (Don: SI 팀이 이 데이터로 무엇을 결정했는지) |

**🔁 Neros 드론으로 옮기면**: **N08 연결표의 핵심 브리지.** baud 허용 범위 스윕, SPI 최대 클럭 스윕, 전원 전압 스윕, RF 감쇠 스윕이 전부 shmoo다. 그리고 health monitoring = FC가 비행 중에 센서·링크 상태를 스스로 점검해 텔레메트리로 보내는 것

**🎤 영어 60초**

> With the SoC and signal-integrity teams I built debug firmware for shmoo testing and health monitoring on the high-speed interfaces between the controller, DRAM, and NAND. A shmoo sweeps voltage and timing and maps where the link passes and fails, so you can see how much margin the operating point really has. I'd apply exactly that to drone testing: sweep the UART baud offset, the SPI clock, the supply voltage, or RF attenuation, and plot the pass/fail edge. It turns "it works on my bench" into a margin number you can track across hardware revisions.

**✍️ 채울 칸**: (Don: 스윕한 파라미터 / shmoo 결과로 바뀐 것)

---

## Part D · SK hynix Application SW (2018–2021) — 테스트 인프라로 시작

### S12. ⭐ 챔버 테스트 자동화 플랫폼과 test platform SDK

**📌 레쥬메**: "Led and integrated test automation script for a web-based platform to enable scalable testing capability and simultaneous analysis of chip reliability testing / Deployed test platform SDK: various APIs to regulate temperature, monitored and scheduled all test scenarios and client (eSSD) status, automated all running UART-based sequences of test cases, facilitating the use of the massive chamber systems for other engineers."

**🎬 스토리 뼈대** (이 포지션의 **1번 스토리**. N07 스토리 A의 확장판)

| STAR | 내용 |
|---|---|
| Situation | 대형 챔버 여러 대, eSSD 다수. 신뢰성 테스트를 확장하고 여러 결과를 동시에 분석해야 했다 (Don: 챔버 수, 디바이스 수) |
| Task | 다른 엔지니어들이 챔버를 **직접 쉽게** 쓸 수 있는 플랫폼과 SDK를 만든다 |
| Action | ① web 기반 플랫폼에 테스트 자동화 스크립트 통합 ② SDK API: 온도 제어, 시나리오 스케줄링, eSSD 상태 모니터링 ③ **UART 기반 테스트 시퀀스 전부 자동화** ④ 여러 테스트의 동시 분석 |
| Result | 다른 엔지니어들이 대규모 챔버를 스스로 운용 (Don: 사용자 수, 절감 효과) |

**🔁 Neros 드론으로 옮기면 — 거의 1:1**

| 챔버 플랫폼 | Neros HIL |
|---|---|
| 챔버 (온도 제어) | HIL 리그 (전원, RC 주입, 모터 캡처) |
| eSSD 다수 | FC·라디오 보드 다수 |
| UART 테스트 시퀀스 | 시리얼 CLI, MSP, CRSF |
| 시나리오 스케줄링 | GitLab CI 파이프라인과 runner |
| 디바이스 상태 모니터링 | 리그 헬스체크, 리셋 원인, 에러 카운터 |
| 다른 엔지니어용 SDK | FW 엔지니어가 테스트를 쉽게 추가하는 프레임워크 |

**🎤 영어 90초** (가장 많이 연습할 것)

> My first role at SK hynix was building test infrastructure for chip reliability testing. We had large thermal chambers full of enterprise SSDs, and the goal was to scale testing and analyze many runs at once. I integrated the test automation into a web-based platform and built an SDK on top: APIs to control chamber temperature, schedule test scenarios, monitor every device's status, and run all the UART-based test sequences automatically. The important part is who used it — other engineers ran the chambers themselves through the SDK. So I learned what a test framework needs to be for its users: simple APIs, reliable device handling when a drive hangs mid-test, and results you can trust. When I look at your HIL needs, it maps almost one-to-one: the chamber becomes the rig, the SSDs become flight controllers and radios, UART sequences become CLI, MSP, and CRSF, and scheduling becomes CI.

**❓ 꼬리 질문**
- "How did you handle a device that hangs mid-test?" → 타임아웃, 상태 모니터링으로 감지, 인프라 실패와 디바이스 실패를 구분해 기록 (Don: 실제 처리 방식)
- "What language/stack?" → (Don: 실제 기술 스택 — Python 비중을 정확하게)
- "What would you do differently now?" → 테스트 결과를 CI와 연결, 실패 시 아티팩트 자동 수집, 코드 리뷰와 버전 관리된 테스트 정의

**✍️ 채울 칸**: (Don: 규모 / 기술 스택 / 가장 어려웠던 문제 / 사용자 피드백)

### S13. 외부 벤더·IT·보안·네트워크 팀과 사내 SSD 테스트 인프라 구축

**📌 레쥬메**: "Developed in-house SSD test infrastructure system with third-party software through cross-functional collaboration with external vendors, internal IT, Security, and Network server teams."

**🎬 스토리 뼈대**

| STAR | 내용 |
|---|---|
| Situation | 테스트 인프라는 코드만으로 안 된다. 서드파티 SW, 서버, 네트워크, 보안 정책이 얽힌다 |
| Task | 여러 팀 사이에서 인프라를 실제로 돌아가게 만든다 |
| Action | 〔예시: 벤더 SW 통합, 서버·네트워크 구성 요청, 보안 요구 충족〕 (Don: 가장 까다로웠던 조율) |
| Result | 사내 테스트 인프라 가동 |

**🔁 Neros 드론으로 옮기면**: HIL 리그는 FW팀, EE팀, IT(runner 서버, 네트워크), 보안(방산: 접근 통제, ITAR [추정])과 엮인다. "Fast-paced environment"에서 **여러 팀 사이의 막힌 것을 뚫는 능력**

**🎤 영어 30초**

> Test infrastructure is never just code. At SK hynix I built in-house SSD test infrastructure with third-party software, and it required working with external vendors and our IT, security, and network teams. A HIL lab has the same shape — firmware, electrical, IT for the runners, and security — and I'm comfortable being the person who gets those pieces working together.

**✍️ 채울 칸**: (Don: 서드파티 SW 종류 / 조율 사례)

---

## Part E · 스토리 전달 요령

- **60초 규칙**: 첫 답은 60초 안에. 면접관이 파고들 곳을 고르게 한다. 90초 버전은 S12 하나만
- **"I"로 말한다**: 팀이 한 일과 내가 한 일을 구분한다. "We built…"만 반복하면 기여가 안 보인다
- **끝은 항상 테스트로**: "…and that became a test case / regression test." 테스트 엔지니어 인터뷰의 핵심 신호
- **Neros로 다리 놓기**: 스토리 끝에 한 문장 "At Neros, that maps to…" (각 스토리의 🔁 칸)
- **모르는 디테일은 말하지 않는다**: `〔예시〕` 부분을 실제 기억으로 못 채웠다면 그 스토리는 원칙 수준으로만 말한다

## 이틀 안에 할 것

1. **S12, S2, S1**을 먼저 채운다. 3개면 45분 인터뷰의 경험 질문 대부분을 막을 수 있다
2. 그다음 **S11 (shmoo)**, **S10 (커버리지)**, **S8 (telemetry)**. 테스트 설계 질문에 끌어 쓰기 좋다
3. S4~S7, S9, S13은 꼬리 질문 대비용으로 한 번 읽기만

## 체크

- [ ] 0절 전체 줄거리를 영어 한 줄로 말할 수 있다
- [ ] S12의 `(Don: …)` 칸을 채우고 90초 버전을 녹음했다
- [ ] S2, S1의 `〔예시〕`를 실제 기억으로 바꿨다 (Apple 기밀은 일반화)
- [ ] S11 shmoo 브리지를 드론 예시 두 개와 함께 말할 수 있다
- [ ] 질문 → 스토리 매핑표를 보고 질문마다 어떤 스토리로 답할지 바로 떠오른다
- [ ] 모든 스토리를 "…and that became a test"로 끝낼 수 있다

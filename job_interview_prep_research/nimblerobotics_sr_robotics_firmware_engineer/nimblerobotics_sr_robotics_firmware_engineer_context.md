# Nimble Robotics — Senior Robotics Firmware Engineer · Context

> **최종 갱신**: 2026-10-17 · **상태**: ❌ 불합격 (리크루터 콜 후)
> **JD**: https://job-boards.greenhouse.io/nimblerobotics/jobs/4384912009 · **위치**: San Francisco, CA (**full-time SF office** [확인됨 리크루터]) · **연봉 밴드**: $180,000 – $230,000 + "generous equity"
> **사용 레쥬메**: `Resume_Firmware_Engineer_2026_Sep_DonHong.pdf`
> 신뢰도: `[확인됨]` 공식/복수 출처 · `[추정]` 단일·2차 출처 또는 추론. 리크루터 공식 안내가 항상 우선.

<!-- 상태 값: 🔍 조사중 → 📨 지원완료 → 📞 리크루터 → 💻 폰스크린 → 🏢 온사이트 → 🎉 오퍼 | ❌ 불합격 | ⏸ 보류 -->

---

## 0. TL;DR

- **회사**: 창고 작업 전부(보관·피킹·패킹·분류·키팅)를 하는 범용 모바일 매니퓰레이터 "warehouse superhumanoid"를 직접 만들고, 그 로봇으로 **자체 무인 풀필먼트 센터(로봇 3PL)를 운영**하는 SF 로보틱스 스타트업. FedEx가 투자·전략 파트너 [확인됨] [1][2]
- **역할**: 로봇의 모터/액추에이터 실시간 제어, 센서, CAN-FD/Ethernet 통신, safe-state·fault handling, 보드 bring-up, 양산 로봇 장애 triage(on-call 포함)를 맡는 시니어 펌웨어 엔지니어 [JD]
- **적합도**: ⭐⭐⭐½ — MCU bare-metal·bring-up·버스 디버그·error handling은 강함. 다만 **모터 제어·CAN·로보틱스 양산 4년+** 이 비어 있음 · **커리어 방향**: ⭐⭐⭐⭐½ — physical AI/robotics 피벗 목표 그 자체
- **핵심 어필**: ① 신규 silicon/FPGA FW bring-up (Cortex-R8/R82/M0+, bare-metal) ② PCIe/I2C/SPMI/RFFE 인터페이스 레벨 root cause (DSO·protocol analyzer) ③ production FW의 error reporting/handling 설계 + NPI→MP 팩토리 테스트 설계
- **최대 갭/리스크**: 모터 제어(FOC·commutation·encoder)·CAN/CAN-FD·functional safety 경험 없음 / JD의 "4+ yrs production robotics or automation" / 밴드 상단 $230K는 Apple·Hark($250K 구두) 대비 낮을 수 있음 / 주 50–60시간 문화 [추정] [6]
- **예상 인터뷰 포맷**: 리크루터 → HM 기술 스크린(1h) → **72h take-home**(SW + robotics/embedded) → 코딩·도메인·시스템 디자인 → **CEO 라운드** [추정 — SWE 후기 기반] [7][8]
- **결과 (2026-10-17)**: 리크루터 콜 진행 후 ❌ 불합격 통보 → 포지션 종료. 회고는 6장 참고
- ~~**다음 액션**: Nar Ammar에게 회신 → 첫 콜에서 레벨·팀(actuation vs. sensing vs. safety)·take-home 여부·onsite 일수·equity 확인. 콜 전에 BLDC/FOC + CAN 개념 1회독.~~ (종료)

---

## 1. 포지션 요약 (JD 핵심)

| 항목 | 내용 |
|---|---|
| 팀 / 조직 | **Robot team** [확인됨 리크루터 10-07] — hardware·AI·infrastructure 팀과 협업 [JD] |
| 레벨 | Senior (4+ yrs) [JD] |
| 위치 / 근무형태 | San Francisco, CA. 근무형태 미기재 → 하드웨어 팀 특성상 onsite [추정]. 조립·운영은 Fort Worth(Haslet, TX) [추정] [4] |
| 연봉 밴드 / Equity | $180K–$230K base + "generous equity" [확인됨 JD] · $1B 밸류 이후 지분 → 업사이드 제한적일 수 있음 |
| 비자 · US Person · 클리어런스 | 기재 없음 (민간 물류 → 해당 없음 [추정]) |
| 특이사항 | **production on-call rotation** 필수 [JD] |

**주요 업무**
- 실시간 모터·액추에이터 제어, 센서 통합, manipulation, 안전·고신뢰 동작용 FW 설계·구현 리드
- MCU ↔ 주변장치 / 상위 compute 간 통신 소유: **CAN/CAN-FD, serial bus, Ethernet** — deterministic·low-latency
- safety-critical 서브시스템의 **safe-state & fault handling** 설계
- EE/HW와 보드 bring-up, HW–SW 통합 (신규 플랫폼·업그레이드)
- latency·정밀도·신뢰성을 지표로 측정하며 actuation/control 성능 개선
- 개발·**HIL 테스트**·양산 환경 디버깅, 설계 리뷰 리드, 아키텍처 결정
- 엔지니어·테크니션 멘토링, 양산 로봇 FW 이슈 triage

**필수 자격**
- EE/CS/Robotics 학위 (Don: 응용수학 — "related field"로 커버 가능)
- real-time control, motor/actuator drive, sensor interfacing, 통신 프로토콜 경험
- **4+ yrs firmware/embedded — production robotics or automation systems**
- Rust / C / C++ 중 하나 강함
- 실시간 제약 MCU 개발 (RTOS or bare-metal)
- CAN·SPI·I²C·UART로 센서·액추에이터 통합
- 복잡한 고신뢰 임베디드 시스템 설계·유지, 테스트·문서·코드리뷰 문화
- on-call 지원 의지

**우대 사항**
- 창고·제조·물류 자동화 환경
- multi-DOF 로봇 (articulated arm, humanoid, AV)
- **FOC, commutation, encoder calibration**
- functional safety: IEC 60204-1 stop category, ISO 13849, IEC 61508

**기술 키워드**: `C` `C++` `Rust` `RTOS` `bare-metal` `CAN-FD` `EtherCAT/Ethernet` `SPI` `I2C` `UART` `BLDC/FOC` `encoder` `HIL` `safe-state` `IEC 61508` `ISO 13849`

---

## 2. 회사 분석

### 2.1 무엇을 하는 회사인가
- Nimble Robotics (브랜드명 "Nimble", nimble.ai). 2017년 Simon Kalouche(Stanford/CMU 로보틱스 출신, CEO)가 창업 [확인됨] [3][5]
- 미션: "robot AGI"로 **Autonomous Supply Chain** 구축 — 공장/창고에서 문 앞까지 [JD]
- 핵심은 **로봇을 팔지 않고 직접 운영**한다는 점: 브랜드(PUMA, AdoreMe 등)의 e-commerce 주문을 Nimble 로봇이 돌리는 풀필먼트 센터에서 처리하는 **"fully autonomous 3PL"** 모델 [확인됨] [1][2]

### 2.2 제품 · 고객 · 비즈니스 모델
- **General Warehouse Superhumanoid**: 단일 하드웨어 플랫폼으로 storage·retrieval·picking·packing·sorting·kitting 전부 수행하는 범용 **mobile manipulator** (상체는 humanoid형, 하체는 비인간형 이동부 [추정]) [1][4]
- AI: 자체 robot foundation model("robot AGI") 학습 + cloud 기반 재고 슬로팅/공급망 최적화 [1]
- 고객: **FedEx**(peak 350K orders/day, 500K+ SKU 규모 센터 계획), PUMA(6년 파트너), AdoreMe(피킹 99% 자동화), Fresh Clean Threads [확인됨 회사 사이트] [1]
- 운영 거점: NJ, Dallas, SF, LA, Chicago, Atlanta (회사 사이트 기준) + Indianapolis 등 확장 예정 [1]
- 수익: 3PL 서비스 매출 (보관·주문당 과금) → **로봇 가동률(uptime)이 곧 매출**. 이 포지션이 "uptime"과 "on-call"을 강조하는 이유 [추정]

### 2.3 단계 · 펀딩 · 규모
| 설립 | 펀딩 총액 / 최근 라운드 | 밸류에이션 | 주요 투자자 | 인원 | HQ |
|---|---|---|---|---|---|
| 2017 | ~$221M+ / Series C $106M (2025-04) | $1B | FedEx(리드), Cedar Pine, DNS Capital, 기존 투자자 | ~110명 (1년 +17%) [추정] | 488 8th St, San Francisco |

- 보드/어드바이저: **Fei-Fei Li, Marc Raibert(Boston Dynamics 창업자), Sebastian Thrun** [확인됨 JD] [3]
- 이전: Series A, Series B $50M (2021) [5]
- 레이오프 보고 없음 [추정] [6]

### 2.4 최근 뉴스 (최근 12개월)
- 2025-04: FedEx 전략 제휴 + Series C $106M, $1B 밸류 — FedEx Fulfillment 확장에 Nimble 3PL 모델 사용 [확인됨] [2][3]
- FedEx는 Nimble 외에도 Berkshire Grey, Dexterity 등과 **멀티 벤더 로보틱스 전략** → Nimble 독점 아님 [추정] [4]
- 2026 상반기: Haslet(TX) 조립·제조(Electro-Mechanical Assembler, NPI TPM, Manufacturing Dev Eng) 채용 활발 → **로봇 양산 확대 단계** [추정] [4]
- JD 문구가 "warehouse superhumanoid" → "supply chain superhumanoid, thousands of tasks"로 확대 → 차세대 플랫폼 개발 중 [추정 JD]

### 2.5 경쟁사 · 시장 포지션
| 구분 | 회사 | Nimble과 차이 |
|---|---|---|
| 피킹/매니퓰레이션 AI | Dexterity, Berkshire Grey, Covariant(→Amazon), RightHand | 대부분 고정형 셀/로봇 판매. Nimble은 이동형 범용 + 직접 운영 |
| 휴머노이드 | Figure, Agility (Digit @ GXO), Apptronik, Tesla Optimus | 범용 휴머노이드 vs. 창고 특화 superhumanoid |
| 창고 시스템 | Symbotic, AutoStore, Ocado, Locus | 대규모 고정 인프라 vs. 로봇 하나로 전 공정 |
| 3PL | ShipBob, ShipMonk, FedEx/UPS 자체 | 사람 기반 vs. 무인 |

포지션: "vertically integrated" (HW + AI + 운영) 가 차별점이자 리스크(자본·운영 부담).

### 2.6 엔지니어링 문화 · 평판 · 보상
- JD 자체가 "hardcore and obsessed", "high-intensity" → **강도 높은 문화** 명시 [확인됨 JD]
- Glassdoor: "Good culture but long hours expected" — 주 **50–60시간**이 일반적이라는 후기 [추정 단일 출처] [6]
- 인터뷰 경험 긍정 52%, 난이도 3.8/5, 채용까지 평균 34일 (SWE 기준) [추정] [7]
- 보상: base $180–230K. Apple 현 TC나 Hark 구두 $250K 대비 base는 낮음 → equity 수량·strike·409A 반드시 확인
- 복리후생: unlimited PTO, 의료, 401k (매칭 언급 없음), 주차비 지원 [JD]

### 2.7 이 포지션이 실제로 할 일 (추론)
- 로봇 관절/그리퍼/이동부의 **모터 드라이버 보드 FW** (STM32/TI C2000급 MCU [추정]) — 전류 루프·위치 루프, encoder 읽기, 온도/전류 보호
- 상위 compute(Linux/Jetson급 [추정]) ↔ 관절 MCU들 사이 **CAN-FD 또는 EtherCAT/Ethernet 버스 프로토콜**과 타이밍 보장
- E-stop, 토크 off, brake 등 **safe-state 머신**과 fault 계층 (IEC 60204-1 Stop Category 0/1/2)
- HIL 리그 구축/개선, 양산 로봇 로그 기반 장애 triage → **Don의 Apple 팩토리 테스트·SSD error handling 경험과 직결**
- 차세대 플랫폼 보드 bring-up

### 2.8 리스크
- 3PL 직접 운영 = 자본집약. 다음 라운드/수익성 경로 확인 필요
- FedEx 멀티 벤더 → 대형 고객 의존도·경쟁
- on-call + 장시간 근무 문화
- 레벨 "Senior" — Don의 Solidigm Staff 타이틀 대비 레벨 다운 체감 가능
- 모터 제어 경험 없음 → "senior인데 도메인 처음"으로 평가될 위험

---

## 3. 적합도 분석 (Don ↔ JD)

### 3.1 요구사항 매칭표
| JD 요구사항 | Don 경험 (레쥬메 근거) | 매칭 |
|---|---|---|
| C/C++ 강함 | "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)" | ✅ 강함 |
| 실시간 MCU (RTOS or bare-metal) | "FW bring-up ARM Cortex R8/R82/M0+" · Xtensa FW | ✅ 강함 (bare-metal). RTOS는 🟡 |
| SPI·I²C·UART 통합 | "I2C, SPI, DMA, PCIe Controller... bring-up" · UART 기반 테스트 자동화 | ✅ |
| CAN / CAN-FD / Ethernet | 레쥬메에 없음 | ❌ 갭 |
| 보드 bring-up & HW–SW 통합 | "Silicon/system bring up -> NPI -> MP", "integration of new silicon" | ✅ 강함 |
| 디버그 (dev/HIL/production) | "Debug at signal, bus, and system level with DSOs and protocol Analyzer", JTAG/Trace32 | ✅ 강함 |
| Safe-state & fault handling | "error reporting/handling scheme in the production firmware" · "sign off on hardware safety margins" | 🟡 전이 가능 (기능안전 표준은 ❌) |
| real-time motor/actuator control | 없음 | ❌ 갭 (가장 큼) |
| sensor interfacing | I2C/SPMI 기반 칩 통합 | 🟡 |
| 성능 지표 기반 개선 | "firmware performance tuning with the silicon validation team" | ✅ |
| 4+ yrs production robotics/automation | 7년+ production FW이나 SSD/RF 도메인 | 🟡 (연차 ✅, 도메인 ❌) |
| 테스트·코드리뷰·문서 | 팩토리 test-node 아키텍처, 테스트 플랫폼 SDK | ✅ |
| 학위 EE/CS/Robotics | Berkeley Applied Math | 🟡 related field |
| FOC·encoder (우대) | 없음 | ❌ |
| Functional safety 표준 (우대) | 없음 | ❌ |
| Rust | 없음 | 🟡 (C/C++로 대체 가능) |

### 3.2 강점 — 어필 포인트
1. **"새 HW가 처음 켜지는 순간"의 전문가** — FPGA 이미지/FW bring-up(Solidigm), 신규 silicon 통합(Apple). JD의 "board bring-up for new robot platforms" 직결.
2. **버스/신호 레벨 root cause** — PCIe/I2C/SPMI/RFFE를 DSO·프로토콜 분석기로 잡아온 경험 → CAN 버스 에러·타이밍 이슈 디버그로 바로 전이.
3. **Production 신뢰성 사고방식** — production FW의 error reporting/handling, Google/Meta/MSFT 데이터센터 요구사항, NVMe telemetry 기반 디버그 기능 → "robot uptime", "triage production issues", fault handling.
4. **NPI→MP & 팩토리 테스트** — 스트레스 시나리오로 latent defect를 MP 전에 잡는 설계 → HIL 테스트·양산 로봇 품질.
5. **수학 배경** — Applied Math → 제어이론(PID/FOC, Clarke/Park 변환) 학습 속도 어필.

### 3.3 갭 & 보완 전략
| 갭 | 인터뷰 전 할 일 | 답변 프레이밍 |
|---|---|---|
| 모터 제어 (BLDC/FOC/encoder) | FOC 파이프라인(Clarke→Park→PI→inv Park→SVPWM) 화이트보드로 설명 가능하게. 저가 BLDC + STM32 B-G431B-ESC1 / SimpleFOC로 주말 실습 | "Control loop 자체는 새롭지만, 결정적 타이밍·ISR 지연·ADC/PWM 동기화는 bare-metal에서 다뤄온 문제" |
| CAN / CAN-FD | 프레임 구조, arbitration, bit timing, error states(error active/passive/bus-off), CAN-FD BRS, CANopen/heartbeat 정리. STM32 FDCAN loopback 실습 | "I2C/SPMI arbitration·ACK·에러 카운터 디버그 경험의 연장선" |
| Functional safety | IEC 60204-1 Stop Cat 0/1/2, ISO 13849 PL, IEC 61508 SIL, watchdog/redundancy/safe torque off 개념 1페이지 | "SSD에서 power-loss·error 경로를 '절대 데이터 손상 없음' 기준으로 설계 — 같은 fail-safe 사고" |
| RTOS | FreeRTOS 태스크/우선순위 역전/ISR→task deferral 정리 (기존 Hark J11 RTOS 노트 재사용) | bare-metal 스케줄러·인터럽트 설계 경험 강조 |
| 로보틱스 도메인 연차 | 로봇 아키텍처(관절 MCU ↔ 상위 compute, 1kHz 루프) 흐름 이해 | "도메인 전환, 엔지니어링 근육은 동일" + 피벗 동기 진정성 |

### 3.4 종합 평가 · 커리어 방향 정합성
- **적합도 ⭐⭐⭐½**: 펌웨어 엔지니어링 기본기·bring-up·디버그·production 신뢰성은 시니어급으로 충분. 그러나 JD의 중심축인 **실시간 모터 제어 + CAN**이 비어 있어 도메인 라운드가 최대 관문.
- **커리어 방향 ⭐⭐⭐⭐½**: robotics + physical AI("robot AGI") 회사의 액추에이션 FW → Don의 피벗 목표에 가장 직접적. 단, AI accelerator·on-device 추론과는 거리 있음(그건 AI 팀).
- 연락이 먼저 왔다는 건 레쥬메의 bring-up/디버그 키워드가 먹혔다는 신호 → 첫 콜에서 "모터 제어는 배우는 중"을 솔직하게 + 학습 계획 제시하는 게 유리.

### 3.5 레쥬메 튜닝 제안 (이 JD용)
- "Led bring-up of new silicon and FPGA platforms (ARM Cortex-R8/R82/M0+) on bare-metal C/C++, validating I2C, SPI, DMA, and interrupt-driven peripherals under real-time constraints."
- "Designed fault detection, error reporting, and safe-recovery handling in production firmware deployed across hyperscale data centers (Google, Meta, Microsoft)."
- "Built factory test-node architecture and stress-based test scenarios that caught latent hardware/firmware integration defects before mass production (NPI → MP)."
- "Root-caused bus-level failures (PCIe, I2C, SPMI, RFFE) with oscilloscopes, protocol analyzers, and JTAG/Trace32 across development, validation, and production."

---

## 4. 예상 인터뷰

### 4.1 프로세스
```
Recruiter (Nar Ammar) 콜 30m
  → HM 기술 스크린 1h (FW 경험 딥다이브 + 개념)
  → Take-home 72h (embedded/robotics 과제 가능성) [추정]
  → 온사이트/버추얼 루프: 코딩 1 · 도메인 기술(FW/모터/CAN) 1 · 시스템 설계 1 · behavioral
  → CEO (Simon Kalouche) 라운드
```
- 위 흐름은 Nimble SWE/robotics 후기 기반 [추정] [7][8]. 펌웨어 포지션은 take-home 내용이 다를 수 있음 → 리크루터에게 직접 확인.
- 라운드별 피드백은 1–2일 내로 빠르다는 후기 [8].

### 4.2 실제 보고된 질문
- [확인됨 Glassdoor] SWE: 72시간 take-home 2개(1개는 software, 1개는 robotics 챌린지), 이후 기술·half-behavioral·CEO 라운드 [8]
- [확인됨 Glassdoor] Robotics: planning·controls 개념, 기본 DSA, system design [8]
- [추정 단일 후기] 알고리즘 라운드에서 LeetCode hard 1문제가 나온 사례, aiortc(WebRTC) 기반 72h 과제 사례 — SWE/infra 직군 [8]
- 펌웨어 직군 후기는 아직 없음

### 4.3 JD 기반 예상 기술 질문
| 카테고리 | 질문 | 왜 나올지 (JD 근거) |
|---|---|---|
| C/C++ 코딩 | ISR-safe lock-free ring buffer (SPSC) 구현, CAN 프레임 pack/unpack (비트필드·엔디안), 고정소수점 PI 제어기 + anti-windup | "C, C++", 결정적 동작 |
| C/C++ 코딩 | 디바운스/상태머신으로 fault state machine 구현 (NORMAL→WARN→FAULT→SAFE) | safe-state & fault handling |
| 임베디드 개념 | `volatile`·memory barrier, ISR 길이/우선순위·nesting, 우선순위 역전, watchdog 설계, jitter 측정법 | real-time constraints |
| 버스 / 프로토콜 | CAN arbitration·bit stuffing·error frame·bus-off 복구, CAN-FD BRS/64B payload, bus load 계산, CAN vs EtherCAT vs SPI 선택 기준 | "CAN/CAN-FD, serial, Ethernet... deterministic, low-latency" |
| 도메인 | BLDC 6-step vs FOC, Clarke/Park, 전류 루프 vs 속도·위치 루프 주기, encoder(incremental/absolute) offset calibration, PWM-ADC 동기 샘플링 | motor/actuator control, FOC (우대) |
| 도메인 | E-stop 신호 들어왔을 때 FW 동작 순서, Stop Category 0/1/2 차이, STO, 상위 compute heartbeat 끊기면? | safety-critical, IEC 60204-1 |
| 디버깅 시나리오 | "양산 로봇 몇 대에서 간헐적으로 관절 fault — 어떻게 triage?" / "CAN 에러 카운터가 올라간다" / "모터가 특정 속도에서 진동" | HIL·production triage, on-call |
| 시스템 설계 | 20+ 관절 로봇의 FW 아키텍처: 관절 MCU ↔ 버스 ↔ 메인 compute, 1kHz 제어 루프 타이밍 예산, OTA FW 업데이트·버전 관리, 로깅/텔레메트리 | architecture decisions, uptime |

### 4.4 Don 경험 딥다이브 (레쥬메 bullet별)
| 레쥬메 bullet | 예상 꼬리질문 | STAR 소재 |
|---|---|---|
| Apple: PCIe/I2C/SPMI/RFFE 인터페이스 failure RCA | 가장 어려웠던 버스 이슈? 신호 레벨에서 무엇을 봤나? HW vs FW 원인을 어떻게 갈랐나? | 신규 칩 통합 중 간헐 실패 → DSO/analyzer로 타이밍 마진 증명 → 수정 |
| Apple: factory test-node 아키텍처 | 어떤 stress 시나리오? 커버리지 vs 테스트 시간 트레이드오프? | NPI 단계 latent defect 사전 검출 사례 |
| Solidigm: Cortex-R8/R82/M0+ FPGA bring-up | bring-up 첫날 순서? 아무것도 안 뜰 때 어디부터? | FPGA 이미지 신규 → 클럭/리셋/메모리 → 주변장치 순서 |
| Solidigm: error reporting/handling scheme | 에러 분류·우선순위, 복구 불가 시 safe-state는? | production FW 에러 계층 설계 → 로봇 fault 계층으로 매핑해서 설명 |
| Hynix: NVMe telemetry 디버그 기능 | 필드에서 재현 안 되는 버그를 어떻게 잡나? | 텔레메트리로 고객사 데이터센터 이슈 root cause → "양산 로봇 triage"로 연결 |
| Hynix: chip reliability system / shmoo | 마진 측정 방법론 | 데이터 기반 개선 = JD "measurable metrics" |

### 4.5 행동 · 동기 질문
- **Why Nimble?** → 범용 로봇이 실제 매출을 내는 몇 안 되는 회사(실 고객 PUMA/FedEx, 실제 운영 센터). 데모가 아닌 **production 로봇 FW**를 하고 싶다. 내 bring-up·production 신뢰성 경험이 "uptime = revenue" 모델에 바로 기여.
- **Why leave Apple (1년 미만)?** → Apple에서 신규 HW 통합의 끝까지(NPI→MP)를 경험했고, 이제 그 기술을 robotics/physical AI에 쓰고 싶다는 장기 방향이 명확. (짧은 재직기간은 "방향성 결정"으로 프레이밍, 부정적 언급 금지)
- **High-intensity 문화 OK?** → SSD 양산 일정·고객 escalation 경험으로 답. on-call 수용 의사 명확히.
- **모터 제어 경험 없는데?** → 솔직 + 이미 시작한 학습(FOC 실습 등) 구체적으로.
- Core values(relentlessly resourceful / legendary / humble / dependable) 각각에 STAR 하나씩 준비.

### 4.6 Tell me about yourself (영어 초안, 60~90초)
> I'm an embedded firmware engineer with about seven years of experience bringing new hardware to life and keeping it reliable in production. At SK hynix and Solidigm, I worked on production SSD firmware — bringing up new ARM Cortex-R and M0+ platforms on FPGA in bare-metal C and C++, validating I2C, SPI, DMA, and PCIe subsystems before silicon, and designing error reporting and handling for firmware shipped to Google, Meta, and Microsoft data centers. Today at Apple, I integrate new wireless silicon into shipping products: I root-cause interface failures across PCIe, I2C, SPMI, and RFFE with scopes and protocol analyzers, and I designed factory test architecture to catch latent defects before mass production. What excites me about Nimble is that your robots are already doing real work for real customers, so firmware reliability directly drives uptime and revenue. I want to bring my bring-up, debugging, and production-reliability experience to robot actuation and control, and I've been ramping up on motor control and CAN to do exactly that.

### 4.7 역질문
1. 이 포지션은 관절 액추에이터/모터 드라이버, 센서, safety 중 어디에 가장 무게가 있나요? 팀 구성(펌웨어 인원)은?
2. 관절 MCU와 메인 compute 사이 버스는 CAN-FD인가요, EtherCAT/Ethernet인가요? 제어 루프 주기는?
3. 양산 로봇 fleet에서 FW 업데이트·텔레메트리·on-call은 어떻게 운영되나요? on-call 빈도는?
4. 차세대 "supply chain superhumanoid" 플랫폼에서 FW 쪽 가장 큰 기술 과제는?
5. 처음 90일에 성공적이라고 볼 결과물은? (+ equity: 수량·strike·vesting·409A, onsite 일수)

### 4.8 준비 체크리스트
- [ ] Nar Ammar 회신 + 콜 일정 (콜 전 이 문서 TL;DR·4.7 확인)
- [ ] 리크루터 콜에서 확인: 레벨, 팀/HM, take-home 유무·내용, onsite 일수, equity 구조, 인터뷰 루프
- [ ] FOC/BLDC 1페이지 요약 (Clarke/Park, 전류루프, SVPWM, encoder calibration)
- [ ] CAN/CAN-FD 1페이지 요약 (arbitration, bit timing, error states, bus load 계산)
- [ ] Functional safety 1페이지 (IEC 60204-1 Stop Cat 0/1/2, ISO 13849 PL, IEC 61508 SIL, STO)
- [ ] C 실습: SPSC ring buffer, 고정소수점 PI + anti-windup, fault state machine — `anduril_firmware_engineer/` 문제은행·`don-c-prac-master/` 재사용
- [ ] RTOS 복습 — Hark J11 RTOS 노트 재사용
- [ ] STAR 6개 (4.4 표) + core values 4개 매핑
- [ ] 레쥬메 3.5 bullet 반영 버전 준비
- [ ] (선택) STM32 + BLDC/SimpleFOC 주말 실습 → 인터뷰에서 "직접 돌려봤다" 소재

---

## 5. 진행 로그

| 날짜 | 이벤트 | 메모 |
|---|---|---|
| 2026-10-06 | Nar Ammar(Nimble)가 먼저 연락 · 컨텍스트 파일 생성 (JD·회사 조사) | Nar Ammar의 직함(리크루터/HM)은 미확인 — 공개 검색 결과 없음 |
| 2026-10-07 | Nar Ammar(리크루터) 메일: Robot team, full-time SF office, 30분 intro video call 요청 (다음 주 7am–2pm PT) | 레쥬메 + 가능 시간 회신 |
| 2026-10-17 | 리크루터 intro call 진행 → ❌ 불합격 통보 | 사유는 따로 기록 안 함. 포지션 종료 |

---

## 6. 인터뷰 노트 & 회고

### 2026-10-17 · Recruiter intro call → 불합격 · 리크루터 Nar Ammar
- 진행: 30분 intro video call (Robot team, full-time SF office) → 불합격 통보
- 사유: 따로 기록 안 함 (기록할 게 생기면 여기에 추가)
- 회고 / 다음 회사에 가져갈 교훈 [추정 — 0·3장 분석 기준]:
  - JD의 중심축이 **실시간 모터 제어 + CAN + 로보틱스 양산 연차**인 자리는 bring-up·디버그 강점만으로는 1차 필터를 넘기 어렵다 → robotics 액추에이션 FW 자리를 노리려면 FOC/CAN 실습 포트폴리오를 먼저 만들어 둔다
  - 리크루터가 먼저 연락한 경우에도 리크루터 콜 자체가 스크리닝이다 → 콜 전에 "도메인 갭 + 이미 진행 중인 학습"을 30초 답변으로 준비
  - 재사용 자산: 이 파일의 4.3 예상 질문표(CAN·FOC·safe-state·HIL triage), 4.6 Tell-me-about-yourself 초안 → 다른 robotics FW 지원 때 그대로 활용

<!-- 라운드마다 추가:
### YYYY-MM-DD · <라운드명> · 면접관 <역할>
- 받은 질문:
- 내 답 / 결과:
- 잘한 점 / 아쉬운 점:
- 다음 라운드에 반영할 것:
-->

---

## 7. 출처

1. https://nimble.ai/ — 회사 사이트: 제품·고객·운영 거점 (확인일 2026-10-06)
2. https://nimble.ai/news/nimble-closes-106-million-series-c-funding-round-at-1b-valuation-scales-fully-autonomous-fulfillment-with-fedex — Series C $106M / $1B, FedEx 제휴 (확인일 2026-10-06)
3. https://www.therobotreport.com/nimble-picks-up-106m-scale-general-purpose-fulfillment-robot/ — Series C 보도 (확인일 2026-10-06)
4. https://embodyvc.com/companies/nimble-robotics/ — 회사 프로필: mobile manipulator, Fort Worth 조립, FedEx 멀티벤더 (확인일 2026-10-06)
5. https://www.freightwaves.com/news/nimble-robotics-raises-$50m-for-fulfillment-automation — Series B, 창업자 (확인일 2026-10-06)
6. https://www.glassdoor.es/Opiniones/Opinión-empleado-Nimble-Robotics-E4136036-RVW103419338.htm — 직원 리뷰 "long hours" · https://www.trueup.io/co/nimble-robotics — 인원·레이오프 (확인일 2026-10-06)
7. https://www.glassdoor.com/Interview/Nimble-Robotics-Interview-E4136036-RVW104001910.htm — 인터뷰 통계·take-home 후기 (확인일 2026-10-06)
8. https://www.glassdoor.co.uk/Interview/Nimble-Robotics-Interview-E4136036-RVW92858884.htm — 72h take-home ×2, CEO 라운드, robotics 질문 주제 (확인일 2026-10-06)
9. https://boards-api.greenhouse.io/v1/boards/nimblerobotics/jobs/4384912009 — JD 원문 (updated 2026-08-27, 확인일 2026-10-06)

---

## 부록 A. JD 원문 (2026-10-06 수집)

<details>
<summary>펼치기</summary>

**Senior Robotics Firmware Engineer** — San Francisco, CA

**About Nimble**

Nimble is an AI robotics company building the autonomous supply chain to power fast, efficient and economical commerce. We're training robot AGI to power a proprietary generalist supply chain superhumanoid, the first robot in the world capable of performing thousands of tasks across the supply chain. We've raised over $220M at over $1B valuation and formed a strategic alliance with FedEx to build a national network of autonomous warehouses capable of generating many billions in annual revenue. We are a hardcore and obsessed team of the world's best engineers and operators. If you are obsessed with your craft, enjoy a high-intensity and fast moving high impact environment, are super high agency in getting hard things done and want to be part of building the world's most legendary robotics company at the most pivotal moment in history, we want to work with you.

We are on a mission to empower and inspire mankind to accomplish legendary feats by inventing robots that liberate us from the menial. We will accomplish this by training robot AGI to invent and build the Autonomous Supply Chain – everything from the inside of factories and warehouses to your front door – powered by generalist superhumanoids.

Our founding team comes from the AI labs at Stanford and Carnegie Mellon and our board of directors include famed robotics and AI legends including Fei-Fei Li (Chief Scientist of AI at Google and Director of Stanford's AI Lab), Marc Raibert (founder of Boston Dynamics), and Sebastian Thrun (founder of GoogleX, Waymo; Stanford Professor and considered the father of autonomous vehicles).

Let's be legendary.

**Nimble's Core Values:** Be relentlessly resourceful - Challenge conventions and overcome obstacles. Be legendary - Be the very best and do work that inspires. Be humble - Prioritize growth, learning, and doing whatever is needed to further the mission. Be dependable - Take ownership and deliver with high agency.

**The Role**

We are looking for a Senior Robotics Firmware Engineer to build the core firmware powering our next-generation autonomous robots. In this role, you will develop the systems that allow our robots to operate with exceptional reliability, precision, and efficiency in real production environments. You will work across robot control, sensing and runtime execution — building robust, production-grade firmware that scales as we deploy more robots into high-throughput warehouse operations. You'll collaborate closely with our hardware, AI, and infrastructure teams to integrate frontier AI capabilities into practical, real-world robotics systems and to continually improve robot uptime, performance, and overall intelligence.

**Responsibilities**

- Lead design and implementation of embedded firmware for real-time motor and actuator control, sensor integration, manipulation, and safe, high-reliability operation.
- Own the low-level communication between microcontrollers and peripherals or higher-level compute stack, including CAN/CAN-FD, serial buses, and Ethernet, ensuring deterministic, low-latency, and robust behavior.
- Design safe-state and fault handling behavior for safety-critical subsystems.
- Partner closely with electrical and hardware engineering on board bring-up and software–hardware integration for new robot platforms and upgrades.
- Improve actuation and control performance, such as latency, precision, and reliability, using measurable metrics and data-driven iteration.
- Debug and resolve issues across development, HIL testing and production environments.
- Participate in and lead technical design reviews and drive architecture decisions for core embedded systems.
- Mentor cross-functional engineers and technicians, and raise the bar on firmware reliability, testing, and operational excellence.
- Triage firmware-related issues to ensure reliability of production robots, and help diagnose and resolve operational issues when they arise

**Qualifications**

- Bachelor, Master, or PhD in Electrical Engineering, Computer Science, Robotics, or a related engineering field.
- Strong experience with embedded firmware domains, e.g. real-time control, motor or actuator drive, sensor interfacing, and communication protocols
- 4+ years of firmware or embedded systems engineering experience building production robotics or automation systems.
- Strong proficiency in at least one of the following programming languages: Rust, C, C++.
- Experience developing on microcontrollers with real-time constraints, whether RTOS-based or bare-metal.
- Experience integrating firmware with sensors, actuators, and communication buses such as CAN, SPI, I²C, and UART.
- Proven ability to design, implement, and maintain complex, high-reliability embedded systems.
- Familiarity with software best practices including testing, documentation, code reviews, and robust architectural design.
- Excellent communication, ownership, and cross-functional collaboration skills.
- Willingness to support production operations through an on-call rotation.

**Nice to have**

- Exposure to automation environments such as warehousing, manufacturing, or logistics
- Experience with multi-DOF robotics system, e.g. articulated arms, humanoid, autonomous vehicles.
- Experience with motor control techniques such as field-oriented control, commutation, and encoder calibration.
- Experience with functional-safety standards and safe design, e.g. IEC 60204-1 stop categories, ISO 13849, or IEC 61508.

**Compensation**

The pay range for this position at the start of employment is expected to be between $180,000 and $230,000/year. Your exact offer may vary depending on multiple individualized factors, including job-related knowledge, skills, and experience. In addition to cash compensation, this position will also receive generous equity for this position.

**Culture:** We embrace challenges and strive to make the impossible possible each day. We're not in this to do what's easy or to be mediocre. We want to create something legendary and leave our mark on the world. We're ambitious, we're gritty, we're humble and we're relentlessly resourceful in pursuit of our goals. If this sounds like you then you might be a great fit!

**Benefits:** Unlimited Flexible Time Off · Health Insurance (medical, dental, vision) · Paid Parental Leave · Commuter Benefits (fully paid parking) · Referral Bonus · 401k · Equity

</details>

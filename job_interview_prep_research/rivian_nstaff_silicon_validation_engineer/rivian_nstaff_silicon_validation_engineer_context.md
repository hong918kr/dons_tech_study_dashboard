# Rivian — Staff Embedded Software Engineer, Silicon & Sensing Systems (Silicon Validation) · Context

> **최종 갱신**: 2026-10-17 · **상태**: 📞 리크루터
> **JD**: https://careers.rivian.com/careers-home/jobs/34042?lang=en-us · **위치**: Palo Alto, CA (607 Hansen Way) · onsite 추정 · **연봉 밴드**: base $186,000 – $232,000 (Bay Area) + bonus + equity
> **사용 레쥬메**: `Resume_Firmware_Engineer_2026_Sep_DonHong.pdf`
> 신뢰도: `[확인됨]` 공식/복수 출처 · `[추정]` 단일·2차 출처 또는 추론. 리크루터 공식 안내가 항상 우선.

<!-- 상태 값: 🔍 조사중 → 📨 지원완료 → 📞 리크루터 → 💻 폰스크린 → 🏢 온사이트 → 🎉 오퍼 | ❌ 불합격 | ⏸ 보류 -->
<!-- 디렉토리명 rivian_nstaff_silicon_validation_engineer 는 미리 만들어져 있던 이름을 유지. JD 본문 Role Summary의 "Staff Silicon Validation Software Engineer"와 대응. -->

---

## 0. TL;DR

- **회사**: 미국 EV 제조사(R1T/R1S/EDV 밴, 2026년 R2 출시). 2025-12 Autonomy & AI Day에서 **자체 AI 가속기 SoC "RAP1"(5nm, ACM3 = 1,600 sparse INT8 TOPS)** 공개 → 이 포지션은 그 칩을 검증하는 팀 [2][3][4]
- **역할**: Autonomy Hardware & Sensing Systems 팀에서 **C 베어메탈 SW로 차세대 SoC(자체 AI accelerator 포함)의 기능 블록·페리페럴을 enable/validate**, 자동 테스트 툴·커버리지 리포트, throughput/latency/utilization 벤치마크 툴 제작 [1]
- **적합도**: ⭐⭐⭐⭐½ — Solidigm에서 한 "pre-silicon FPGA bring-up + SoC verification(I2C/SPI/DMA/PCIe/SRAM/DRAM) + silicon validation 팀과 perf 튜닝"이 JD 업무와 거의 1:1. 갭은 Zynq Petalinux · MIPI/ISP · AI accel 실무 · 학위(Applied Math) 정도 · **커리어 방향**: ⭐⭐⭐⭐⭐ — "in-house AI accelerator SoC" + autonomy = Don의 AI accel/physical AI 피벗 목표의 정중앙
- **핵심 어필**: ① pre-/post-silicon bring-up (Cortex-R8/R82/M0+, FPGA 프로토타입, RTL freeze 전 SoC verification) ② 페리페럴·고속 인터페이스(PCIe 3/4/5/6, I2C, SPI, DMA, DRAM, SPMI, RFFE) 루트코즈 ③ 테스트 인프라·자동화(Python, chamber 테스트 플랫폼 SDK, shmoo, factory test-node 설계) + perf 튜닝
- **최대 갭/리스크**: **레벨** — Apple ICT3라고 밝힘 → Staff(RIV-6)가 아닌 Senior로 다운레벨 제안 위험. Solidigm Staff 타이틀 + Apple 현재 TC로 Staff 앵커링 필요. 그 외 Petalinux/Zynq, camera(MIPI CSI-2/ISP), "Why leave Apple after ~10 months"
- **예상 인터뷰 포맷**: 리크루터 → HM 연결 → **Technical screen** → **Onsite 4라운드 (Technical 2 + Behavioral 2)** [확인됨: 리크루터 10-17]
- **다음 액션**: ① 리크루터에게 레쥬메 전달(요청받음) + **타깃 레벨(Staff) 재확인** ② HM 콜 대비 60–90초 TMAY + "SoC 검증 경험" 2분 스토리 ③ 4.3 질문표 중 메모리 테스트·레지스터 드라이버·DMA·perf 벤치마크 C 코딩 우선 연습

---

## 1. 포지션 요약 (JD 핵심)

| 항목 | 내용 |
|---|---|
| 팀 / 조직 | Autonomy Hardware and Sensing Systems — Rivian next-gen HW platform + **in-house AI accelerator SoC** [1][확인됨]. SoC validation 팀과 협업 |
| 직함 | 공고 제목: Staff Embedded Software Engineer, Silicon & Sensing Systems · 본문: "Staff Silicon Validation Software Engineer" [1] |
| 레벨 | Staff (Rivian 사다리상 RIV-6 추정 [10][추정]) · 경력 요건 4–15년 [1] |
| 위치 / 근무형태 | Palo Alto, CA (607 Hansen Way) [1] · 랩 업무 특성상 onsite 위주 [추정] |
| 연봉 밴드 / Equity | **base $186K–$232K** (Bay Area) + annual performance bonus + equity [1][확인됨] · levels.fyi RIV-6 TC ≈ $358K (base ~$221K, stock ~$112K, bonus ~$25K) [10][추정] |
| 비자 · US Person · 클리어런스 | 언급 없음 (상용 자동차 — ITAR 무관) [1] |
| 공고일 | 2026-10-05 게시, 10-15 이후 마감 가능 [1] |

**주요 업무**
- SoC validation 팀과 함께 **SoC validation plan** 정제·실행
- **C embedded SW**로 차세대 HW의 SoC 검증 — SoC 내부 functional block과 peripheral interface enable
- **자동 테스트 툴** 개발 + **테스트 커버리지 리포트** 생성
- HW 시스템 성능 평가 **벤치마크 툴** — throughput, latency, resource utilization, IO/compute frequency 메트릭
- 검증 진행·결과를 Rivian 트래킹 시스템에 문서화 (traceability)

**필수 자격**
- EE/CE 또는 관련 분야 학사/석사
- 관련 분야 4–15년
- Embedded C 능숙
- 분석력, 글/말 커뮤니케이션, 팀 플레이어

**우대 사항**
- platform/embedded SW 프로젝트
- Python, Linux shell 스크립팅
- **AMD Zynq Petalinux** 개발
- **AI accelerator** 경험
- SoC 페리페럴: JTAG, I2C, UART, SPI, Ethernet, DDR, PCIe
- **카메라 데이터 처리**: MIPI, ISP, video encoder

**기술 키워드**: `Embedded C` `bare-metal` `SoC validation` `post-silicon` `FPGA prototyping` `AMD Zynq` `Petalinux` `AI accelerator / NPU` `JTAG` `I2C` `UART` `SPI` `Ethernet` `DDR` `PCIe` `MIPI CSI-2` `ISP` `video encoder` `Python` `benchmark` `test coverage`

---

## 2. 회사 분석

### 2.1 무엇을 하는 회사인가
Rivian Automotive(NASDAQ: RIVN) — 전기 픽업 R1T, SUV R1S, Amazon 등 상용 배송 밴(EDV)을 만드는 미국 EV 회사. 2026년 중형 SUV **R2** 양산 시작(Normal, Illinois 공장) [6][7]. 최근에는 **자율주행 + 자체 실리콘** 회사로의 전환을 강하게 내세움 [2][3].

### 2.2 제품 · 고객 · 비즈니스 모델
- 차량: R1T / R1S / EDV(상용 밴) / **R2**(2026-06 일반 고객 인도 시작) [6][확인됨]
- 자율주행 스택: Gen 3 Autonomy 하드웨어 = **ACM3**(자체 RAP1 칩 기반) + LiDAR, R2에 2026년 말부터 탑재 예정 · Large Driving Model · Universal Hands-Free(미국·캐나다 350만 마일 이상 도로) [2][3][확인됨]
- **RAP1 칩**: 5nm, 프로세싱+메모리를 하나의 multi-chip module에 통합, **RivLink** 저지연 칩 간 interconnect로 멀티칩 확장, 자체 AI compiler + platform SW, TSMC 생산(Bloomberg 보도) [2][3][확인됨]. Arm CPU 코어 + Rivian이 직접 설계한 neural engine [4][추정]
- SW/전장: Volkswagen과 **Rivian and VW Group Technologies** 합작법인(최대 $5.8B 규모, 2024-11 출범) — Rivian의 zonal 아키텍처·SW를 VW에 공급 [9][확인됨]
- 로보택시: Uber와 자율주행 로보택시 파트너십 보도(2026 Q1) [6][추정 — 단일 2차 출처, 확인 필요]

### 2.3 단계 · 펀딩 · 규모
| 설립 | 펀딩 총액 / 최근 라운드 | 밸류에이션 | 주요 투자자 | 인원 | HQ |
|---|---|---|---|---|---|
| 2009 | 상장사(2021 IPO) · VW JV 최대 $5.8B · DOE 대출 $6B(조지아 공장, 2025-01 승인) [9] | 상장 시가총액 변동 | Amazon, Volkswagen | ~1.5만 명 [추정] | Irvine, CA (실리콘·자율주행은 Palo Alto) |

### 2.4 최근 뉴스 (최근 12개월)
- 2025-12 **Autonomy & AI Day (Palo Alto)**: RAP1 칩, ACM3, RivLink, Large Driving Model 공개. SVP Electrical HW **Vidya Rajagopalan**(ex-Tesla Model 3, 2020 합류): "자체 실리콘이라 공급사 칩보다 SW 개발을 거의 1년 먼저 시작" · "**first silicon 훨씬 전에 in-house HW prototyping platform에서 SW를 돌렸다**" [2][3][4] ← 이 팀이 FPGA 프로토타입(→ Zynq/Petalinux 우대 이유) 위에서 pre-silicon 검증을 한다는 강한 신호
- 2026 Q2: 12,613대 생산 / 12,194대 인도(가이던스 상회), 매출 $1.658B(+27% YoY), consolidated gross profit $179M [6][8][확인됨]
- 2026-06: R2 일반 고객 인도 시작 [6]
- 2026 Q3 (10-02 발표): 19,751대 생산 / 19,248대 인도 — 분기 최고, 연간 65–70K 인도 가이던스 재확인. Q3 실적 발표 **10-29** 예정 [7][확인됨]
- 2025: 소규모 레이오프(비용 절감, R2 준비) [9][추정]

### 2.5 경쟁사 · 시장 포지션
- 자체 자율주행 칩 보유 OEM: **Tesla**(FSD/AI5), 중국 OEM(NIO·XPeng 자체 칩). 대부분 OEM은 NVIDIA DRIVE Orin/Thor · Mobileye · Qualcomm Snapdragon Ride를 구매
- Rivian은 Gen 2(ACM2)까지 NVIDIA 기반 → Gen 3부터 자체 RAP1으로 전환 [3][추정]
- 칩 회사 관점에서 경쟁 = 인재 경쟁: Tesla, Apple, NVIDIA, 그리고 Bay Area AI accel 스타트업

### 2.6 엔지니어링 문화 · 평판 · 보상
- 평판(종합): 미션·제품 만족도 높고, 적자/구조조정 리스크와 조직 변동에 대한 불만이 섞임 [추정 — Glassdoor/Blind 일반 경향]
- 보상: levels.fyi Software Engineer 기준 RIV-3 $130K ~ RIV-8 $487K, **Staff(RIV-6) ≈ $358K TC** (2026-04 갱신) [10][추정]. JD base 밴드 $186–232K와 일치
- 비교: Apple ICT3 SWE 중앙값 TC ≈ $244K [11][추정] — 하지만 Don은 "레벨 대비 컴펜이 매우 높다"고 리크루터에게 말함 → Don 실제 TC 기준으로 협상
- 상장사 RSU(RIVN) — 주가 변동성 높음, refresh·vesting 스케줄 확인 필요

### 2.7 이 포지션이 실제로 할 일 (추론)
- **Pre-silicon**: Zynq/FPGA 프로토타입(또는 emulator) 위에서 SoC 블록별 bare-metal 테스트 작성 — 레지스터 R/W, reset/clock 시퀀스, 인터럽트, DMA, 메모리 컨트롤러 [추정]
- **Post-silicon bring-up**: RAP1 후속/차세대 칩 A0 도착 → JTAG로 boot, DDR training, PCIe/Ethernet/MIPI 링크 업, NPU(neural engine) 기능·성능 검증 [추정]
- **Benchmark**: NPU throughput(TOPS 실측, utilization), DMA bandwidth, 메모리 latency, RivLink 칩간 latency, IO/compute frequency별 perf·power 커브 [추정]
- **Automation**: Python 회귀 테스트 프레임워크, 커버리지 리포트, Jira류 트래킹 [추정]
- **Sensing**: 카메라 파이프라인(MIPI CSI-2 → ISP → encoder) 검증 [추정, JD 우대사항 근거]
- 즉, **Don이 Solidigm(SSD controller)에서 한 pre-silicon/FPGA SoC verification을 AI accelerator SoC로 옮기는 자리**

### 2.8 리스크
- **레벨 다운**: ICT3라고 밝힌 상태 → HM이 Senior로 보고 Staff 밴드 미적용 가능. (Solidigm Staff 2022–2025 + 총 8년 경력 강조)
- **보상**: Staff base 상한 $232K — Apple 현재 TC가 "매우 높다"면 base는 상한·equity/사이닝으로 메워야 함
- **Apple 재직 ~10개월**: 짧은 재직 이유 질문 필수
- 회사 재무: 아직 자동차 부문 적자, 주가 변동성 → RSU 가치 불확실
- 위치: Palo Alto — Cupertino 기준 통근 무난

---

## 3. 적합도 분석 (Don ↔ JD)

### 3.1 요구사항 매칭표
| JD 요구사항 | Don 경험 (레쥬메 근거) | 매칭 |
|---|---|---|
| SoC validation plan 정제·실행 (validation 팀 협업) | "Performed SoC verification (multi-core interconnection I2C, SPI, DMA, PCIe Controller, SRAM/DRAM bring-up ... with FPGA before RTL freeze)" · "Conducted firmware performance tuning with the silicon validation team" | ✅ 강함 |
| C embedded SW로 SoC 기능 블록·페리페럴 enable | "Developed using embedded C and C++ on bare-metal (pre/post-silicon)" · "new FPGA Image/FW bring-up ARM Cortex R8/R82/M0+ and PCIe/NVMe subsystem IP" | ✅ 강함 |
| 자동 테스트 툴 + 커버리지 리포트 | "Led and integrated test automation script ... Deployed test platform SDK ... automated all running UART-based sequences of test cases" · Apple "factory test-node architecture" | ✅ 강함 |
| 벤치마크 툴 (throughput/latency/utilization/frequency) | "firmware performance tuning" · "performance-critical customer (Google/Meta) features in NVMe 2.0" · "shmoo testing" | 🟡 성능 튜닝 경험 有, "벤치마크 툴" 자체 제작은 프레이밍 필요 |
| 문서화·traceability | Jira/Perforce · "sign off on hardware safety margins" | ✅ |
| EE/CE 학위 | B.A. Applied Mathematics (UC Berkeley) | 🟡 "related field" — 8년 HW 경력으로 커버 |
| 4–15년 | 2018–현재 ≈ 8년 (FW 2021~ ≈ 5.5년) | ✅ |
| Python / Linux shell | "Python, Linux" · 테스트 자동화·SDK | ✅ |
| AMD Zynq Petalinux | 직접 경험 없음 (FPGA 프로토타입 bring-up은 있음) · Embedded Linux 스터디 A–L 진행 | 🟡 / ❌ 실무 갭 |
| AI accelerator | 실무 없음 · Hark Embedded AI 스터디 노트 97편(HW 아키텍처·프로파일링·실리콘 선정 포함) | 🟡 이론 준비됨 |
| JTAG / I2C / UART / SPI | Trace32(Lauterbach), JTAG, I2C/SPI SoC verification, UART 테스트 자동화 | ✅ |
| DDR / PCIe | DRAM bring-up, PCIe 3/4/5/6, NVMe, Apple PCIe 루트코즈 | ✅ 강함 |
| Ethernet | 레쥬메 근거 없음 | ❌ 갭 (개념 수준 준비) |
| Camera MIPI / ISP / video encoder | 없음 (Apple RFFE/SPMI는 MIPI Alliance 계열 — 연결고리로만) | ❌ 갭 |
| 랩 장비 디버깅 | "DSOs and protocol analyzer" · "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer" | ✅ |

### 3.2 강점 — 어필 포인트
1. **Pre-silicon → post-silicon 풀사이클을 SoC 쪽에서 해봄**: Solidigm에서 RTL freeze 전 FPGA로 I2C/SPI/DMA/PCIe/SRAM/DRAM bring-up — Rajagopalan이 강조한 "first silicon 전 프로토타이핑 플랫폼에서 SW 먼저" 문화와 정확히 일치 [4]
2. **고속 인터페이스·페리페럴 루트코즈**: PCIe(3~6), NAND, DRAM, I2C/SPMI/RFFE — Apple에서 "new chip meets full HW/SW system" 문제를 소유
3. **검증 인프라 빌더**: chamber 테스트 플랫폼 SDK, UART 자동화, shmoo, module health monitoring, factory test-node 설계 → "automatic testing tools + coverage report"
4. **성능 관점**: silicon validation 팀과 FW perf 튜닝, Google/Meta perf-critical 기능 → 벤치마크 툴(throughput/latency) 이야기로 연결
5. **Staff 경험**: Solidigm Staff Engineer(2022–2025) — 크로스펑셔널(SoC/SI 팀, 디자인 엔지니어) 리드

### 3.3 갭 & 보완 전략
| 갭 | 인터뷰 전 할 일 | 답변 프레이밍 |
|---|---|---|
| Zynq / Petalinux | PS(Cortex-A53/R5) + PL 구조, AXI(-Lite/-Stream/DMA), `petalinux-create/-config/-build`, device tree overlay, UIO/`/dev/mem`로 PL IP 레지스터 접근 1페이지 정리. Embedded Linux 노트 C(부트)·D(device tree) 복습 | "FPGA 프로토타입 위 SoC bring-up은 Solidigm에서 매일 했다 — 플랫폼이 Xilinx/Petalinux로 바뀌는 건 툴 차이, 검증 방법론은 동일" |
| AI accelerator | Hark 노트 E(HW 아키텍처)·D(성능 모델)·K(프로파일링)·M(실리콘 선정) 재독. NPU 검증 포인트: MAC array, SRAM tiling, DMA descriptor, INT8 정확도(golden model 비교), utilization 측정 | "NPU도 결국 DMA + 메모리 서브시스템 + compute engine의 SoC 블록 — 내가 검증해 온 것들 위에 compute가 얹힌 것. golden-model 비교 기반 테스트 설계 가능" |
| Camera MIPI CSI-2 / ISP | CSI-2 D-PHY 레인·packet(short/long, VC/DT), ISP 파이프(BLC→demosaic→AWB→CCM→gamma), 테스트 패턴 제너레이터로 검증하는 법 정리 | "RFFE/SPMI도 MIPI Alliance 스펙 — 스펙 읽고 프로토콜 애널라이저로 디버깅하는 방식은 동일" |
| Ethernet | MAC/PHY(MII/RGMII), MDIO, loopback(PHY/MAC) 테스트, throughput 측정(iperf), automotive Ethernet(100/1000BASE-T1) 개념 | 짧게 인정 + 학습 계획 |
| EE/CE 학위 아님 | — | 8년 HW-근접 경력, 신호·버스 레벨 디버깅 실적으로 대체. 수학 배경 = 성능 모델링/통계적 검증에 강점 |
| 레벨 (ICT3) | 리크루터에게 레벨 앵커 다시 전달 (4.5 참고) | "Apple에선 RF 도메인 전환이라 ICT3로 들어갔지만 TC는 Staff 수준, 직전 Solidigm Staff" |

### 3.4 종합 평가 · 커리어 방향 정합성
- **종합 적합도 ⭐⭐⭐⭐½** — 필수요건(Embedded C, SoC validation, 4–15년) 전부 ✅, 우대 중 JTAG/I2C/UART/SPI/DDR/PCIe/Python ✅. 갭은 우대사항(Zynq·카메라·AI accel)에 몰려 있음
- **커리어 방향 ⭐⭐⭐⭐⭐** — "in-house AI accelerator SoC" 검증은 AI accel 칩 경력으로 바로 이어지는 타이틀. autonomy/physical AI 도메인까지. 지금까지 지원한 포지션 중 방향 정합성 최고 수준
- 리스크는 기술이 아니라 **레벨·보상 협상**

### 3.5 레쥬메 튜닝 제안 (이 JD용)
- Solidigm 첫 bullet을 맨 위로: "Validated pre-silicon SoC on FPGA prototypes before RTL freeze — brought up multi-core interconnect, I2C/SPI, DMA, PCIe controller, and SRAM/DRAM using bare-metal C on Arm Cortex-R8/R82/M0+."
- 벤치마크 키워드 추가: "Built firmware-level benchmarks with the silicon validation team to measure throughput, latency, and resource utilization, driving performance tuning on prototype and first silicon."
- 자동화+커버리지: "Developed automated test frameworks (Python/UART) that scheduled and analyzed thousands of reliability test cases across chamber systems, with pass/fail and coverage reporting."
- Apple bullet에 "new silicon" 강조: "Own post-silicon integration of new wireless chipsets — root-causing PCIe/I2C/SPMI/RFFE interface failures from signal level (DSO, protocol analyzer) to system level."

---

## 4. 예상 인터뷰

### 4.1 프로세스
```
리크루터 콜 (10-17 완료) → 레쥬메 전달 → HM에게 전달/연결
  → Technical Screening (1회, 형식 미정 — 코딩 + 경험 딥다이브 추정)
  → Onsite 4라운드: Technical ×2 + Behavioral ×2   [확인됨: 리크루터]
  → 오퍼 / 레벨링
```
- 공개 후기(일반 SWE) 기준: 리크루터 → 기술 폰스크린 → HM sync → 4–5 라운드 (coding/design/behavioral), 전체 1–2주~수주 [14][15][추정]
- 2026 Irvine 후기: 1차 인터뷰가 **본인 프로젝트 하나를 깊게** 파고듦 + 도메인 경험 질문 [13][추정]
- 2019 Glassdoor 후기(오래됨): 2:1 + 1:1 ×3, "함수 호출 시 스택에서 일어나는 일", LeetCode Easy 2문제 [12][추정]

### 4.2 실제 보고된 질문
- [추정] 함수 호출 시 프로그램 스택에서 무슨 일이 일어나는가 (2019, Irvine) [12]
- [추정] LeetCode Easy 2문제 (2019) [12]
- [추정] 지원자 프로젝트 하나 상세 딥다이브 + 관련 도메인(HV) 경험 (2026) [13]
- [추정] LRU cache 구현, string/sliding window/hashmap 문제 (aggregator) [14]
- 이 팀(Silicon & Sensing) 고유 후기는 미발견 → 아래 JD 기반 예상에 집중

### 4.3 JD 기반 예상 기술 질문
| 카테고리 | 질문 | 왜 나올지 (JD 근거) |
|---|---|---|
| C 코딩 | 메모리 테스트 구현: walking-1s/0s, address-in-address, **March C-** (DDR/SRAM 검증) | DDR 우대 · "enable SoC internal functional blocks" |
| C 코딩 | 레지스터 field read-modify-write 매크로/함수 (mask/shift, `volatile`, W1C 비트) | 레지스터 레벨 검증 = 일상 업무 |
| C 코딩 | ring buffer / DMA descriptor 링 구현 · CRC32 계산 · 비트 카운트/reverse | Embedded C 필수 |
| C 코딩 | 간단한 테스트 프레임워크: 테스트 등록(함수 포인터 테이블), 실행, pass/fail 집계, 리포트 | "automatic testing tools + coverage reports" |
| C 코딩 | 사이클 카운터로 memcpy/DMA bandwidth·latency 측정 코드 (warm-up, 반복, 평균/분산) | 벤치마크 툴 업무 |
| 임베디드 개념 | `volatile`, memory barrier(DMB/DSB), cache coherency와 DMA (clean/invalidate) | DMA·accel 검증의 단골 버그 |
| 임베디드 개념 | 부트 시퀀스: BootROM → BL → DDR init → main; reset/clock/power domain enable 순서 | SoC bring-up |
| 임베디드 개념 | 인터럽트 검증: GIC 설정, edge/level, 인터럽트 storm, ISR 지연 측정 | 기능 블록 enable |
| 버스 / 프로토콜 | I2C(ACK/NACK, clock stretching, 버스 hang 복구) · SPI 모드(CPOL/CPHA) · UART framing | 우대사항 그대로 |
| 버스 / 프로토콜 | PCIe link training/LTSSM, enumeration, BAR, MSI/MSI-X, 링크 다운 디버그 | PCIe 우대 · Don 강점 → 깊게 들어올 것 |
| 버스 / 프로토콜 | DDR training(write leveling, read gate/eye), ECC, 메모리 컨트롤러 검증 | DDR 우대 |
| 버스 / 프로토콜 | Ethernet MAC/PHY, MDIO, loopback 테스트 | Ethernet 우대 (갭) |
| 도메인 (AI accel) | NPU 검증 전략: golden model(numpy) 대비 비트정확 비교, INT8 양자화, tiling, utilization이 낮을 때 원인(메모리 바운드 vs compute 바운드, roofline) | "in-house AI accelerator SoC" |
| 도메인 (AI accel) | TOPS 스펙 vs 실측 차이를 어떻게 측정·설명하나 | 벤치마크 툴 |
| 도메인 (카메라) | MIPI CSI-2 구조(lane, VC/DT, short/long packet), ISP 블록, 테스트 패턴으로 검증 | camera 우대 |
| 도메인 (Zynq) | PS/PL, AXI, Petalinux로 PL IP 테스트하는 흐름, device tree, UIO | Zynq Petalinux 우대 |
| 디버깅 시나리오 | "새 칩에서 DMA 전송 데이터가 가끔 깨진다" — 어떻게 좁혀가나 (cache, alignment, 타이밍, 전압/온도 shmoo) | 검증 핵심 역량 |
| 디버깅 시나리오 | "pre-silicon에서 통과한 테스트가 post-silicon에서 실패" — 원인 분류 (FPGA vs ASIC 클럭/타이밍, 아날로그 PHY, 모델 차이) | 프로토타입 → 실리콘 흐름 |
| 시스템 설계 | SoC validation plan을 처음부터 짠다면: 블록 리스트, 우선순위, 커버리지 정의, 자동화, 회귀, 트래킹 | "refine and execute the SoC validation plan" |
| 시스템 설계 | 수백 개 테스트를 여러 보드에서 야간 회귀로 돌리는 인프라 (Python 오케스트레이션, JTAG/UART 제어, 결과 DB, 대시보드) | 자동화 + traceability |

### 4.4 Don 경험 딥다이브 (레쥬메 bullet별)
| 레쥬메 bullet | 예상 꼬리질문 | STAR 소재 |
|---|---|---|
| Solidigm: SoC verification on FPGA before RTL freeze (I2C/SPI/DMA/PCIe/SRAM/DRAM) | 어떤 버그를 RTL freeze 전에 잡았나? FPGA와 실 실리콘의 차이는? 테스트를 어떻게 구조화했나? | RTL 버그 하나를 FPGA 테스트로 찾아 디자인 엔지니어와 루트코즈 → 수정 반영된 사례 (숫자: 찾은 버그 수, 일정 단축) |
| Solidigm: FW perf tuning with silicon validation team | 무엇을 측정했나(IOPS/latency/QoS)? 병목은 어디였나? 측정 툴은? | perf 회귀를 찾아 원인(DMA/큐/캐시) 규명 → 개선 % |
| Solidigm: Cortex R8/R82/M0+ bring-up | 멀티코어 부트 순서, 코어간 통신(mailbox/IPC), cache/MPU 설정 | 첫 이미지가 안 뜰 때 JTAG(Trace32)로 디버깅 |
| SK hynix: chip reliability system for high-speed interface (NAND V6/7, PCIe 3/4/5) · shmoo | shmoo 축(전압/온도/주파수) 설계, 마진 판정 기준, 자동화 | 마진 부족 발견 → SI 팀과 협업 |
| SK hynix: NVMe telemetry 디버깅 기능 | 설계 결정, 오버헤드, 고객 요구 | 고객(MS/Dell/HPE) 이슈를 데이터로 해결 |
| SK hynix: test platform SDK + UART 자동화 | 아키텍처, 스케일, 다른 엔지니어 채택 | 수동 → 자동화로 처리량 N배 (→ "automatic testing tools") |
| Apple: new silicon integration, PCIe/I2C/SPMI/RFFE 루트코즈 | 가장 어려웠던 인터페이스 실패, DSO/프로토콜 애널라이저로 어떻게 찾았나 | 신호 레벨 원인 → 시스템 레벨 수정 (기밀 범위 내에서) |
| Apple: factory test-node architecture, stress-based case studies | latent defect를 MP 전에 어떻게 드러냈나 | stress 시나리오 설계 → MP 전 결함 포착 |

### 4.5 행동 · 동기 질문 (Behavioral 2라운드 대비)
- **Why Rivian?** → "Rivian은 자체 AI accelerator(RAP1)를 만든 몇 안 되는 OEM. 칩 → 컴파일러 → 차량까지 수직통합된 곳에서, 내가 SSD controller에서 해 온 pre-/post-silicon SoC 검증을 AI accelerator로 확장하고 싶다. 내가 테스트한 칩이 실제 도로 위 자율주행에 들어간다는 게 동기."
- **Why leave Apple (~10개월)?** → 부정적 표현 금지. "Apple에서 integration 쪽(새 칩이 시스템에 들어갈 때의 문제)을 배웠다. 내 커리어 핵심은 silicon 자체를 bring-up·검증하는 것이고, 앞으로 AI accelerator 실리콘에 집중하고 싶다. 이 자리는 그 둘을 합친 것."
- **레벨 질문 대응** (HM이 물으면): "Apple ICT3 was a deliberate entry point to switch into the RF/wireless domain; my compensation is well above the typical ICT3 band. Before Apple I was a Staff Engineer at Solidigm, leading pre-silicon SoC bring-up for a PCIe 6 controller. I'm targeting Staff scope here — owning the validation plan for a block end-to-end."
- 예상 behavioral 질문 + STAR 준비:
  - 일정 압박 속에서 품질(검증 커버리지) vs 속도 트레이드오프를 결정한 경험
  - 디자인/HW 팀과 루트코즈 책임을 두고 이견이 있었던 경험 (데이터로 설득)
  - 모호한 요구사항에서 스스로 범위를 정해 리드한 경험 (Staff 시그널)
  - 실패/놓친 버그와 이후 프로세스 개선
  - 다른 엔지니어를 멘토링하거나 툴을 만들어 팀 생산성을 올린 경험 (테스트 SDK)
  - 우선순위 충돌(여러 팀 요청) 처리

### 4.6 Tell me about yourself (영어 초안, 60~90초)
> I'm Don — I've spent about eight years working right at the boundary between silicon and software. At SK hynix and Solidigm I started in test automation, building the platform that ran chip-reliability tests across large chamber systems, then moved into firmware, where I became a Staff Engineer on the next-generation PCIe 6 SSD controller team. A big part of that job was pre-silicon validation: bringing up Cortex-R and M cores, the PCIe and NVMe subsystem, DMA, I2C/SPI and SRAM/DRAM on FPGA prototypes before RTL freeze, and then tuning performance with the silicon validation team once real silicon arrived. Since late 2025 I've been at Apple in the wireless chipset integration group, where I own root-cause analysis when a new chip meets the full system — PCIe, I2C, SPMI and RFFE failures, debugged from the scope and protocol analyzer up to system level. What draws me to this role is that Rivian is building its own AI accelerator. I'd like to take the pre- and post-silicon validation work I've done on storage controllers and apply it to an AI SoC that ends up driving real vehicles.

### 4.7 역질문
1. RAP1 후속/차세대 SoC는 현재 어느 단계인가요 (pre-silicon FPGA/emulation vs first silicon bring-up)? 첫 6개월에 이 역할이 집중할 블록은?
2. Validation 팀과 이 팀(Embedded SW) 사이 역할 분담 — validation plan의 오너는 누구이고, DV(pre-silicon 시뮬레이션) 팀과는 어떻게 커버리지를 나누나요?
3. 테스트 인프라 현황: 회귀 자동화, 보드 팜, 커버리지 리포트는 이미 있나요, 아니면 이 역할이 만드는 건가요?
4. Neural engine 검증에서 golden model/compiler 팀과는 어떻게 협업하나요?
5. 이 자리에서 Staff로 성공한다는 건 1년 후 어떤 모습인가요? (레벨 기대치 확인)

### 4.8 준비 체크리스트
- [ ] 리크루터에게 레쥬메 전달 + Staff 레벨 타깃·현재 TC 범위 메시지 (레벨 앵커링)
- [ ] TMAY 영어 60–90초 소리 내어 3회
- [ ] Solidigm pre-silicon SoC verification STAR 2개 + Apple 루트코즈 STAR 1개 (숫자 포함)
- [ ] C 코딩: March C- 메모리 테스트 · 레지스터 RMW/W1C · DMA descriptor 링 · 사이클 카운터 벤치마크 · 테스트 러너 (각각 백지 구현) — `../anduril_firmware_engineer/` 문제은행, `../don-c-prac-master/`에서 비트조작·ring buffer 재사용
- [ ] 개념 1페이지: cache/DMA coherency, barrier, 부트 시퀀스, GIC
- [ ] PCIe LTSSM·enumeration·MSI-X / DDR training 복습 (Don 강점 영역 — 깊게 물어볼 것)
- [ ] AI accel: [Embedded AI 노트](../../dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/site/index.html) E(HW 아키텍처)·D(성능 모델)·K(프로파일링)·M(실리콘) 재독 → NPU 검증 전략 2분 답변
- [ ] Zynq/Petalinux 1페이지 + [Embedded Linux 노트](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/index.html) C(부트)·D(device tree) 복습
- [ ] MIPI CSI-2 / ISP / Ethernet MAC-PHY 개념 1페이지
- [ ] SoC validation plan 설계 질문 — 화이트보드 구조 연습
- [ ] Behavioral 6개 STAR (4.5) 영어로 정리
- [ ] Q3 실적(10-29) 발표 후 회사 뉴스 업데이트

---

## 5. 진행 로그

| 날짜 | 이벤트 | 메모 |
|---|---|---|
| 2026-10-17 | 리크루터 연락 · 통화 | Rivian 쪽에서 먼저 연락. 레쥬메 요청 → 바로 HM에게 전달하겠다고 함. 현재 레벨 질문 → "Apple ICT3, 직전 회사(Solidigm)는 Staff, Apple은 레벨은 낮지만 컴펜이 매우 높다"고 답함. 프로세스 안내: HM 연결 → technical screening → 통과 시 onsite 4개 (technical 2 + behavioral 2) |
| 2026-10-17 | 컨텍스트 파일 생성 (JD 조사) | JD 원문은 Rivian careers API에서 수집 |

---

## 6. 인터뷰 노트 & 회고

<!-- 라운드마다 추가:
### YYYY-MM-DD · <라운드명> · 면접관 <역할>
- 받은 질문:
- 내 답 / 결과:
- 잘한 점 / 아쉬운 점:
- 다음 라운드에 반영할 것:
-->

### 2026-10-17 · 리크루터 콜
- 받은 질문: 현재 레벨
- 내 답: Apple ICT3 · 이전 회사 Staff · Apple 컴펜이 레벨 대비 매우 높음
- 메모: Staff 포지션에 ICT3로 들어가면 다운레벨 위험 → HM 콜/스크린에서 Staff 범위의 오너십 사례(validation plan 주도, 크로스팀 리드)를 의도적으로 보여줄 것

---

## 7. 출처

1. https://careers.rivian.com/careers-home/jobs/34042?lang=en-us — JD 원문 (Rivian careers API `careers.rivian.com/api/jobs?keywords=...`로 수집) (확인일 2026-10-17)
2. https://www.barchart.com/story/news/36577582/rivian-unveils-custom-silicon-next-gen-autonomy-platform-and-deep-ai-integration-at-inaugural-autonomy-ai-day — Rivian Autonomy & AI Day 보도자료: RAP1, ACM3 1,600 TOPS, RivLink (확인일 2026-10-17)
3. https://fortune.com/2025/12/15/rivian-ceo-ev-maker-new-chips-autonomous-driving-robotaxi/ — Fortune: 자체 칩·자율주행 전략, Rajagopalan 인용 (확인일 2026-10-17)
4. https://www.popsci.com/?p=728693 — PopSci: Arm CPU + Rivian 설계 neural engine, prototyping platform 인용 (확인일 2026-10-17)
5. https://www.repairerdrivennews.com/2025/12/15/rivian-shares-ai-centric-autonomy-plans-in-house-chip-manufacturing/ — Autonomy plans 요약 (확인일 2026-10-17)
6. https://electrek.co/2026/07/30/rivian-rivn-q2-2026-earnings-r2-deliveries-revenue-up-27/ — Q2 2026 실적, R2 인도 (확인일 2026-10-17)
7. https://insideevs.com/news/810613/rivian-q3-sales-2026-r2/ — Q3 2026 생산/인도 (확인일 2026-10-17)
8. https://www.businesswire.com/news/home/20260702205746/en/Rivian-Releases-Q2-2026-Production-and-Delivery-Figures-Raises-Full-Year-Delivery-Outlook-and-Sets-Date-for-Second-Quarter-2026-Financial-Results — Q2 2026 생산/인도 (확인일 2026-10-17)
9. https://finance.yahoo.com/news/rivian-balances-layoffs-1-billion-160444782.html — VW JV $1B 집행·레이오프 (2025) (확인일 2026-10-17)
10. https://www.levels.fyi/companies/rivian/salaries/software-engineer — Rivian 레벨별 보상 (RIV-6 ≈ $358K) (확인일 2026-10-17)
11. https://www.metaintro.com/blog/apple-careers-guide-2026-roles-pay-hiring-tips — Apple ICT3 TC 중앙값 (levels.fyi 인용, 2차) (확인일 2026-10-17)
12. https://www.glassdoor.com.hk/Interview/Rivian-Interview-E630579-RVW33377268.htm — 2019 Rivian 인터뷰 후기 (확인일 2026-10-17)
13. https://prachub.com/interview-experiences/rivian-software-engineer-interview-project-discussion-and-high-voltage-experience-739d75280c — 2026 Rivian SWE 후기 (확인일 2026-10-17)
14. https://www.interviewquery.com/guides/rivian-software-engineer — Rivian SWE 인터뷰 가이드 (aggregator) (확인일 2026-10-17)
15. https://www.techprep.app/blog/rivian-interview-process — Rivian 인터뷰 프로세스 (aggregator) (확인일 2026-10-17)

---

## 부록 A. JD 원문 (2026-10-17 수집)

<details>
<summary>펼치기</summary>

**Staff Embedded Software Engineer, Silicon & Sensing Systems** — 607 Hansen (Palo Alto, California) · Full Time · Req 34042 · posted 2026-10-05

About Rivian

Rivian is on a mission to keep the world adventurous forever. This goes for the emissions-free Electric Adventure Vehicles we build, and the curious, courageous souls we seek to attract. As a company, we constantly challenge what's possible, never simply accepting what has always been done. We reframe old problems, seek new solutions and operate comfortably in areas that are unknown. Our backgrounds are diverse, but our team shares a love of the outdoors and a desire to protect it for future generations.

Role Summary

Join as Staff Silicon Validation Software Engineer in Autonomy Hardware and Sensing Systems team. You will be part of the team to build Rivian's next generation hardware platform including Rivian in-house AI accelerator SoC to enhance vehicle performance and autonomous driving capabilities.

Responsibilities

- Work w/ SoC validation team to refine and execute the SoC validation plan
- Use C embedded software to validate SoC for the next generation hardware design, enable SoC internal functional blocks and peripheral interfaces.
- Develop automatic testing tools and generate test coverage reports.
- Build benchmark tools to evaluate performance of the hardware system, including throughput, latency, resource utilization, IO or compute frequency metrics
- Document validation progress and results in Rivian's tracking system, to maintain good traceability

Qualifications

Required:
- Bachelors/Masters Degree in Electrical Engineering, Computer Engineering or related field
- 4-15 years experience in related fields.
- Proficient in Embedded C software development.
- Good analytical skills, written and verbal communication skills, and a team player

Preferred:
- With projects focusing on platform/embedded software development
- Experience with scripting languages like Python, Linux shell, etc.
- Prior experience in AMD Zynq Petalinux development
- Prior experience in AI accelerator is a plus
- Prior experience in SoC peripheral interfaces like JTAG, I2C, UART, SPI, ethernet, DDR, PCIe is a plus
- Prior experience in camera data processing like MIPI interfaces, ISP, video encoder is a plus

Pay Disclosure

Pay Range: The salary range for this role is $186,000 - $232,000 annually for Bay Area based applicants. This is the lowest to highest salary we in good faith believe we would pay for this role at the time of this posting. An employee's position within the salary range will be based on several factors including, but not limited to, specific competencies, relevant education, qualifications, certifications, experience, skills, geographic location, shift, and organizational needs. The successful candidate may be eligible for annual performance bonus and equity awards.

Benefits: We offer a comprehensive package of benefits for full-time and part-time employees, their spouse or domestic partner, and children up to age 26, including but not limited to paid vacation, paid sick leave, and a competitive portfolio of insurance benefits including life, medical, dental, vision, short-term disability insurance, and long-term disability insurance to eligible employees. You may also have the opportunity to participate in Rivian's 401(k) Plan and Employee Stock Purchase Program if you meet certain eligibility requirements. Full-time employee coverage is effective on their first day of employment. Part-time employee coverage is effective the first of the month following 90 days of employment. More information about benefits is available at rivianbenefits.com.

You can apply for this role through careers.rivian.com (or through internal-careers-rivian.icims.com if you are a current employee). This job is not expected to be closed any sooner than 10/15/2026.

(Equal Opportunity / Candidate Data Privacy / AI-in-hiring 정책 문단은 생략 — 표준 문구)

</details>

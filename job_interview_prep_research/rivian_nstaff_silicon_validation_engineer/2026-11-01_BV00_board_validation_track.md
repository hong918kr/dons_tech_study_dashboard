# BV 트랙 · Board validation / bring-up — 새 보드·새 칩을 살리고 검증하기
<!-- theme: pastel -->

> **왜 따로 트랙인가**: 10-30 스크린에서 이 포지션이 **silicon validation + board validation, 주로 C** 라고 확인됨 [확인됨] → 온사이트 도메인 라운드에서 "새 보드가 왔다, 뭐부터 하나?" 류 질문이 나올 확률 높음 [추정]. 면접관 Manjiang 경력(FPGA/HAPS 프로토타이핑 · bring-up · post-si validation · PVT · production SLT)과 정확히 겹치는 축 · **범위**: 보드에 처음 전원을 넣는 날부터 → 전원·클럭·리셋 → JTAG → 부트 → DDR → 인터페이스 → 마진/PVT → 검증 계획·자동화까지 · **관계**: 온사이트 플랜의 **N04를 이 트랙이 대체**. PCIe 쪽은 [LP 트랙](2026-10-31_LP00_linux_pcie_track.html), 인터페이스 기초는 [LP00A primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **작성**: 2026-11-01 리스트 → **같은 날 BV01–BV16 전부 완성** (코드 `onsitePrep/bv02–bv14/`, 전부 실행 검증) · **원본**: [온사이트 플랜](2026-10-31_rivian_onsite_plan.html)

## 0. TL;DR

- **16개 노트 (BV01–BV16)**, 6개 묶음: A 큰 그림·회로도 · B 전원·클럭·리셋 · C 첫 전원 인가·JTAG·부트 · D 메모리·인터페이스 · E 측정·마진 · F 디버그·계획·실전.
- **P0 9개부터**: **BV01 큰 그림 · BV02 회로도 · BV03 전원 · BV05 리셋/스트랩 · BV06 첫 전원 인가 · BV07 JTAG · BV09 DDR · BV13 디버그 시나리오 · BV14 검증 계획/자동화**.
- **외울 한 줄**: bring-up은 **아래 계층부터 하나씩 증명**한다 → **전원(rail·순서) → 클럭(osc·PLL lock) → 리셋(해제·strap) → JTAG(IDCODE·halt) → SRAM 코드 실행·UART → DDR(training·memtest) → 인터페이스(I2C·SPI·Ethernet·PCIe·MIPI) → 성능·마진(PVT·shmoo) → 자동화·회귀**.
- 노트마다 공통 구조: **🔰 초보 설명 → 정확한 개념 → 보드에서 볼 것·잴 것(장비·레지스터·로그) → 꼬리 질문 체인 + 영어 모범 답 → Don 경험 연결 → 실습/코드 → 함정 → 출처**.
- 실습은 보드가 없으므로 **검증 가능한 것만**: C 메모리 테스트(March C-·walking 1s·address/data line), I2C 버스 복구 bit-bang 시뮬레이션, 전원 시퀀스 체커, shmoo 플롯 생성, Python 보드 팜 자동화 골격(mock) — `onsitePrep/bvXX/`.

## 0.5 🔰 처음 보는 사람을 위한 큰 그림 — "보드 bring-up"이 뭔가

> 비유: 새 보드 bring-up = **새로 지은 건물에 처음 전기를 넣고 입주하는 과정**. ① 전기 배선 합선 없나(전원 rail 저항 확인) ② 차단기 올리고 층마다 전압 나오나(rail 측정·순서) ③ 시계·엘리베이터 박자 맞나(클럭·PLL) ④ 관리실이 "준비 완료" 방송(리셋 해제) ⑤ 관리인이 들어가 점검(JTAG로 CPU 멈추고 레지스터 확인) ⑥ 방송 시스템 테스트(UART "hello") ⑦ 창고(DDR) 정리·검사 ⑧ 각 층 설비(인터페이스) 점검 ⑨ 한여름·한겨울에도 버티나(온도·전압 코너, PVT) ⑩ 매일 자동 점검 체계(회귀 자동화).

```text
 첫 전원 인가 날 (Day 0) ─────────────────────────────────────────────────────────────▶ 양산 (SLT)
 ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
 │ ① 전원    │─▶│ ② 클럭    │─▶│ ③ 리셋    │─▶│ ④ JTAG   │─▶│ ⑤ 부트    │─▶│ ⑥ DDR    │─▶│ ⑦ 인터페이스│
 │ rail·순서 │  │ osc·PLL  │  │ 해제·strap│  │ IDCODE   │  │ SRAM·UART│  │ training │  │ I2C·SPI· │
 │ 전류·PGOOD│  │ refclk   │  │ boot mode│  │ halt·read│  │ BootROM  │  │ memtest  │  │ ETH·PCIe │
 └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘
       │ 실패하면 다음 단계로 가지 말 것 — 아래 계층이 증명되기 전 위 계층 증상은 믿지 않는다
       ▼
 ┌──────────────── ⑧ 특성화·마진: 전압·온도·주파수 코너(PVT), shmoo, eye margin ─────────────────┐
 └──────────────── ⑨ 검증 계획·보드 팜·회귀 자동화·HW/SW 버그 가르기·보드 리비전(EVT/DVT/PVT) ──┘
```

| 단계 | 쉬운 말 | 무엇으로 확인하나 | 실패하면 흔한 원인 |
|---|---|---|---|
| ① 전원 | 각 전압선(rail)에 정해진 전압이 **정해진 순서로** 들어오나 | 전류 제한 걸린 bench PSU, DMM, scope(ramp·ripple), PMIC PGOOD | 합선, 레귤레이터 실장 불량, 순서 오류, PMIC 설정(OTP) |
| ② 클럭 | 칩의 심장 박동(crystal/oscillator)이 뛰고, 칩 안 PLL이 **lock** 됐나 | scope로 osc 핀, 클럭 출력 핀(debug clock out), PLL lock 비트 | 크리스탈 부하 커패시터, osc 전원, PLL 설정 |
| ③ 리셋 | "이제 시작해도 돼" 신호(reset)가 전원·클럭 안정 **뒤에** 풀렸나, 그 순간 strap 핀 값이 맞나 | scope로 reset 핀 타이밍, strap 저항 값, reset reason 레지스터 | 리셋 너무 이름, strap 저항 잘못, watchdog 재부팅 루프 |
| ④ JTAG | 디버거로 칩이 **살아 있는지** 확인(IDCODE 읽기, CPU 멈추기, 메모리 읽기) | Trace32 / OpenOCD, IDCODE, DAP | JTAG 전압 레벨, TCK 너무 빠름, 칩이 리셋에 갇힘 |
| ⑤ 부트 | 칩 내장 ROM이 boot 장치에서 첫 코드를 읽어 실행 → UART에 첫 글자 | UART 콘솔, BootROM 에러 코드, JTAG로 PC 확인 | boot mode strap, flash 이미지 헤더, 클럭 설정 |
| ⑥ DDR | 큰 메모리 칩과 신호 타이밍을 맞추고(training) 깨지지 않는지 검사 | training 로그, March C- 등 memtest, eye margin | 파라미터(타이밍·임피던스), 레이아웃, VREF, 온도 |
| ⑦ 인터페이스 | 센서·플래시·네트워크·PCIe·카메라가 **연결되고 데이터가 맞나** | I2C 스캔, SPI flash ID, loopback, PRBS, 링크 상태 | 핀 mux, 풀업, 전압 레벨, PHY 설정 |
| ⑧ 마진 | 정상 조건이 아니라 **끝 조건**(고온·저전압·고속)에서도 되나 | thermal chamber, 전압 margining, shmoo | 마진 부족 → HW 수정 또는 스펙 조정 |
| ⑨ 운영 | 수십 장 보드에서 매일 자동으로 돌리고 결과로 판단 | 보드 팜, Python 오케스트레이션, 결과 DB | 재현성 없음, 보드 간 편차 |

- **Silicon validation vs board validation (이 포지션은 둘 다)**
  - **Silicon validation** = 칩(SoC) 자체가 설계대로 동작하나 — 칩 안 블록(CPU·NPU·DDR 컨트롤러·PCIe 컨트롤러…)의 기능·성능·마진.
  - **Board validation** = 그 칩을 올린 **보드**(전원·클럭·메모리·커넥터·센서)가 설계대로 동작하나 — 회로·부품·레이아웃 문제를 찾음.
  - 첫 실리콘 bring-up 때는 둘이 섞여 있어서 **"칩 문제냐 보드 문제냐 SW 문제냐"를 가르는 능력**이 핵심 (→ BV13, BV14).
- **보드 종류 (이름을 알아야 대화가 됨)**: validation board / **EVB**(evaluation board) / **CRB**(customer reference board) = 검증용으로 테스트 포인트·점퍼·전류 측정 저항이 많은 보드 · product board = 실제 차량에 들어가는 보드 · **socketed board** = 칩을 소켓에 꽂아 바꿔 끼울 수 있는 보드(여러 칩 샘플 비교·PVT용) · FPGA/HAPS 프로토타입 = 실리콘 전에 RTL을 FPGA에 올린 것(Don이 Solidigm에서 한 것).

## 1. 노트 목록 한눈에

| ID | 노트 | 묶음 | 우선 | 연결된 면접 질문 유형 | 기존 자료 |
|---|---|---|---|---|---|
| **BV01** | [**Silicon vs board validation 큰 그림 · pre-si → A0 → PVT → SLT**](2026-11-01_BV01_si_board_validation_big_picture.html) | A | **P0** | "Walk me through bringing up a new SoC" | [deck §6](2026-10-25_rivian_hm_screen_prep.html) |
| **BV02** | [**회로도 · 레이아웃 · BOM 읽기 (bring-up 관점)**](2026-11-01_BV02_schematic_layout_reading.html) | A | **P0** | "What do you look for in a schematic review?" | [EL P4](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P4_schematic_to_dt.html) |
| **BV03** | [**전원 트리 · power sequencing · PMIC · rail 측정**](2026-11-01_BV03_power_tree_sequencing_pmic.html) | B | **P0** | "First power-on — what do you check?" | |
| BV04 | [클럭: crystal/oscillator · PLL · refclk · jitter](2026-11-01_BV04_clocks_osc_pll_refclk.html) | B | P1 | "The core runs but is slow / UART garbage" | |
| **BV05** | [**리셋 · strap 핀 · boot mode · reset reason · watchdog**](2026-11-01_BV05_reset_strap_boot_mode.html) | B | **P0** | "Board keeps rebooting" | |
| **BV06** | [**첫 전원 인가 날 체크리스트 (smoke test → hello)**](2026-11-01_BV06_first_power_on_checklist.html) | C | **P0** | "The board just arrived. What's your Day 1?" | [EL P1](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html) |
| **BV07** | [**JTAG · SWD · CoreSight · boundary scan**](2026-11-01_BV07_jtag_swd_boundary_scan.html) | C | **P0** | "JTAG can't read IDCODE. Why?" | [EL M11](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/M/2026-10-17_M11_hw_linked_debugging.html) |
| BV08 | [부트 흐름 · bring-up 테스트 펌웨어 (BootROM → FSBL → DDR → 앱)](2026-11-01_BV08_boot_flow_bringup_firmware.html) | C | P1 | "How is your bring-up test firmware structured?" | [C 정리](2026-10-29_c_basics_cram.html) |
| **BV09** | [**DDR/LPDDR bring-up · training · 메모리 테스트 (March C-)**](2026-11-01_BV09_ddr_bringup_training_memtest.html) | D | **P0** | "DDR fails on some boards / at hot" · "Write a memory test" | [hmScreenPrep](hmScreenPrep/index.html) |
| BV10 | [보드 인터페이스 테스트: I2C · SPI · UART · GPIO · Ethernet · PCIe · MIPI](2026-11-01_BV10_board_interface_tests.html) | D | P1 | "How do you validate every interface on the board?" | [LP00A](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) |
| BV11 | [랩 장비: DMM · PSU · scope · LA · protocol analyzer · power analyzer](2026-11-01_BV11_lab_instruments_measurement.html) | E | P1 | "How would you measure X?" | |
| BV12 | [SI/PI 기초 · margining · shmoo · PVT 특성화](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) | E | P1 | "What's a shmoo plot? How do you run PVT?" | [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html) |
| **BV13** | [**보드 디버그 시나리오 12선 (증상 → 잴 것 → 가설 → 다음 행동)**](2026-11-01_BV13_board_debug_scenarios.html) | F | **P0** | "It doesn't boot — debug it" | [LP15](2026-10-31_LP15_debug_scenarios.html) |
| **BV14** | [**보드 검증 계획 · 보드 팜 · 자동화 · HW/SW 버그 가르기**](2026-11-01_BV14_board_validation_plan_automation.html) | F | **P0** | "Write a validation plan for this board" | [LP14](2026-10-31_LP14_validation_methodology.html) |
| BV15 | [자동차 보드: 온도 등급 · 12V 전원 이벤트 · EMC · 기능 안전 (가볍게)](2026-11-01_BV15_automotive_board_considerations.html) | F | P2 | "What's different about automotive?" | |
| BV16 | [모의 면접 Q 뱅크 (30문항 + 꼬리 + 영어 답)](2026-11-01_BV16_mock_interview_qbank.html) | F | P1 | 전부 | BV01–BV15 종합 |

## 2. 노트별 상세 목차 + 꼬리 질문 체인

### A. 큰 그림 · 회로도

#### BV01 · Silicon vs board validation 큰 그림 (P0)

- 📄 **본문 노트**: [BV01 노트 열기](2026-11-01_BV01_si_board_validation_big_picture.html)
- 🔰 **쉽게 말하면**: 칩 하나가 태어나서 차에 들어가기까지 **검증 단계 지도**. 실리콘 전에는 시뮬레이션·에뮬레이터·FPGA로 SW를 미리 만들고(pre-silicon), 첫 칩(A0)이 오면 보드에 올려 살리고(bring-up), 기능·성능을 검증하고(post-silicon validation), 온도·전압 끝 조건을 재고(PVT/characterization), 마지막으로 공장에서 모든 칩을 걸러내는 테스트(SLT)를 만든다. 이 노트는 각 단계에서 **validation SW 엔지니어가 하는 일**을 정리한다.

- **핵심 개념**
  - 단계: architecture/spec → RTL → simulation/DV(UVM) → **emulation(Palladium/Zebu) · FPGA prototyping(HAPS/VCU)** → tape-out → **A0 silicon** → bring-up → post-si functional validation → performance → **PVT / characterization(shmoo)** → B0/C0 stepping(에라타 수정) → qualification → **production test(ATE) · SLT(system-level test)**
  - 누가 뭘 하나: DV(RTL 검증) / silicon validation(칩 기능·성능) / board(HW) 엔지니어 / SI·PI 엔지니어 / validation SW(테스트 FW·자동화 = 이 포지션) / product SW(드라이버)
  - Pre-si에서 미리 할 것: 테스트 FW·드라이버를 FPGA에서 먼저 돌려 두기 → A0 도착 후 **"같은 바이너리"**로 바로 bring-up (Don의 Solidigm 경험 그대로)
  - Silicon bug vs board bug vs SW bug, errata·workaround, stepping
  - 보드 리비전: **EVT → DVT → PVT** (Engineering / Design / Production Validation Test) — 보드 쪽 단계 이름
- **보드에서 볼 것**: 단계별 산출물 표 (test plan, bring-up checklist, errata list, shmoo report, SLT 프로그램)
- **꼬리 질문 체인**
  - Q. "Walk me through how you'd bring up a brand-new SoC on a new board." → 꼬리1 "What would you have ready before the silicon arrives?" → 꼬리2 "How do you reuse the FPGA work?" → 꼬리3 "What's different between FPGA and silicon that bites you?" (속도·클럭 비율·아날로그 PHY·전원 도메인·DDR PHY 없음)
  - Q. "What's the difference between silicon validation and design verification?" → "Why do we still find bugs post-silicon if DV had 100% coverage?" → "How do you get a silicon bug fixed — what goes into an errata?"
  - Q. "What's SLT and how is it different from ATE testing?" → "What would you put in an SLT program for an AI SoC?"
- **Don 연결**: Solidigm — RTL freeze 전 FPGA에서 Cortex-R8/R82/M0+·PCIe/NVMe IP bring-up, SoC verification(I2C/SPI/DMA/SRAM/DRAM) → 실리콘 validation 팀과 perf 튜닝 · SK hynix — chamber 신뢰성 테스트 플랫폼(PVT·shmoo 쪽)

#### BV02 · 회로도 · 레이아웃 · BOM 읽기 (P0)

- 📄 **본문 노트**: [BV02 노트 열기](2026-11-01_BV02_schematic_layout_reading.html)
- 🔰 **쉽게 말하면**: 회로도(schematic) = **보드의 배선 지도**. bring-up 전에 이 지도에서 "전원은 어디서 와서 어디로 가나, 리셋 선은 누가 잡나, 부트 모드는 어떤 저항으로 정하나, 디버거는 어디 꽂나, 측정은 어디서 하나(test point)"를 미리 찾아 체크리스트로 만든다. 레이아웃은 그 선이 실제 기판에서 **어떻게 그려졌나**(길이·층·간격).

- **핵심 개념**
  - 회로도 페이지 구조: block diagram → power tree page → SoC 핀 페이지(전원·GND·IO 뱅크) → 메모리 → 인터페이스 → 커넥터 → 디버그
  - 읽는 순서 (bring-up 체크리스트로 바꾸기): ① power tree & sequencing ② clock sources ③ reset 회로(supervisor·RC·버튼) ④ **strap / boot mode 저항** ⑤ 디버그 헤더(JTAG·UART) ⑥ test point·전류 측정 저항(shunt) ⑦ 풀업/풀다운(I2C·open-drain·enable) ⑧ IO 전압 뱅크·level shifter ⑨ DNP(미실장) 옵션
  - 기호: net label, off-page connector, DNP, 0 Ω 저항(옵션 선택용), ferrite bead, decoupling cap
  - 레이아웃 리뷰 포인트(검증 SW가 알아야 하는 만큼): 차동쌍·길이 맞춤(DDR/PCIe), 레퍼런스 플레인, decoupling 위치, AC coupling cap(PCIe), test point 접근성
  - BOM: 부품 대체(alternate part), 리비전 차이, 실장 확인
- **꼬리 질문 체인**
  - Q. "You get the schematic a week before boards arrive. What do you do with it?" → "What's on your bring-up checklist from the schematic?" → "How do you know which rails must come up first?" → "What would you ask the HW engineer to add for debuggability?" (test point, 전류 shunt, 0 Ω 옵션, 디버그 헤더, LED)
  - Q. "An I2C device doesn't respond. What do you look at in the schematic?" → (풀업 유무·값, 전압 도메인, 주소 strap 핀, 레벨 시프터, 전원 enable)
  - Q. "Why are there 0-ohm resistors and DNP parts everywhere on a validation board?"
- **Don 연결**: Apple 무선 칩셋 통합 — 회로도로 PCIe/I2C/SPMI/RFFE 신호 추적, DSO·protocol analyzer로 확인 · 실습: 공개 EVB 회로도(예: Raspberry Pi CM4 IO board, Zynq ZCU102) 한 장으로 체크리스트 만들어 보기

### B. 전원 · 클럭 · 리셋

#### BV03 · 전원 트리 · power sequencing · PMIC · rail 측정 (P0)

- 📄 **본문 노트**: [BV03 노트 열기](2026-11-01_BV03_power_tree_sequencing_pmic.html)
- 🔰 **쉽게 말하면**: SoC는 전압 하나가 아니라 **여러 전압(rail)** 을 먹는다(코어 0.75 V, IO 1.8 V/3.3 V, DDR 1.1 V, PHY 아날로그 0.8 V …). 이것들은 **정해진 순서와 속도로** 켜져야 하고(sequencing), 순서가 틀리면 칩이 이상하게 동작하거나 손상된다. PMIC는 이 rail들을 만들고 순서를 지키는 **전원 관리 칩**. 첫 전원 인가 때는 **전류 제한을 건 전원 공급기**로 조심스럽게 켜고 rail을 하나씩 잰다.

- **핵심 개념**
  - Power tree: 입력(12 V 차량 / 5 V USB-C / bench) → buck(스위칭, 효율↑, ripple 있음) / LDO(선형, 깨끗·효율↓) → 각 rail
  - Sequencing: power-up 순서(보통 core → IO → analog/PHY, 또는 데이터시트 지정), ramp rate, **PGOOD** 체인, enable 체인, power-down 역순, 리셋은 마지막 rail PGOOD 이후
  - PMIC: OTP(공장 설정), I2C/PMBus로 전압·순서 읽기/쓰기, fault 레지스터(OV/UV/OC/OT), telemetry
  - SoC 쪽 전원 도메인: always-on, power island, power gating, retention, AVS/DVFS
  - 측정: rail 전압(DMM), ramp·overshoot·순서(scope 다채널), ripple/noise(scope 20 MHz BW limit + 짧은 ground spring), 전류(shunt + power analyzer), droop(부하 계단 때 순간 전압 하강), 열(열화상)
  - 첫 인가: 전원 넣기 전 **rail-GND 저항 측정**(합선 찾기), 전류 제한, 전류 예상치와 비교
- **보드에서 볼 것**: power tree 표(rail · 전압 · 허용 오차 · 순서 · 측정 위치 · 예상 전류), scope 캡처 체크 항목, PMIC fault 레지스터 덤프
- **꼬리 질문 체인**
  - Q. "Walk me through powering on a new board for the first time." → 꼬리1 "Why current-limited?" → 꼬리2 "Current is 3× higher than expected. What next?" (열화상·rail 하나씩 분리·0 Ω 제거) → 꼬리3 "One rail is 10% low only under load — what is it?" (droop, 레귤레이터 한계, 피드백 저항, 레이아웃 IR drop)
  - Q. "Why does power sequencing matter?" → "What can happen if IO comes up before core?" (latch-up·back-powering·잘못된 strap 읽힘) → "How would you verify sequencing on every board automatically?"
  - Q. "How do you measure ripple correctly?" → "Why does your measurement show 200 mV of noise but the HW engineer sees 20 mV?" (긴 ground lead·BW limit)
  - Q. "What's AVS/DVFS and how would you validate it?"
- **Don 연결**: SSD 컨트롤러 전원 이벤트(PLP, power loss) 테스트·전력 측정(Power Analyzer) · `[숫자 채우기]` 측정해 본 rail/전류 예시
- **실습/코드**: 전원 시퀀스 체커 (scope CSV 형태 입력 → 각 rail 10%/90% 시점·순서·간격을 스펙과 비교, Python) — mock 데이터로 검증

#### BV04 · 클럭: crystal/oscillator · PLL · refclk · jitter (P1)

- 📄 **본문 노트**: [BV04 노트 열기](2026-11-01_BV04_clocks_osc_pll_refclk.html)
- 🔰 **쉽게 말하면**: 칩 안의 모든 회로는 **박자(클럭)** 에 맞춰 움직인다. 보드 위의 작은 수정 진동자(crystal, 예: 24 MHz)가 기본 박자를 만들고, 칩 안 **PLL**이 이걸 곱해서 GHz 박자를 만든다. 박자가 안 뛰면 아무것도 안 되고, 박자가 흔들리면(jitter) 고속 링크(PCIe·DDR)가 깨진다. PCIe는 보드에서 **100 MHz 기준 클럭(refclk)** 을 따로 받는다.

- **핵심 개념**
  - crystal(수동, 칩 안 발진 회로 필요, load cap) vs oscillator(능동, 전원 필요, 바로 출력) vs clock generator/buffer(여러 출력, PCIe refclk)
  - PLL: ref → ×N/M → VCO → 분주, **lock** 비트·lock 시간, bypass 모드(bring-up 시 PLL 없이 osc로 먼저)
  - 클럭 트리·클럭 게이팅: 블록을 쓰려면 clock enable + reset deassert 둘 다 (bring-up 단골 버그)
  - 측정: scope(주파수·진폭, 프로브 부하 주의 — crystal 핀 직접 찍으면 멈출 수 있음), frequency counter, **debug clock out 핀**(PLL/분주 출력을 핀으로 내서 재기)
  - jitter(주기 흔들림), phase noise, spread spectrum(SSC, EMI 줄이려고 일부러 흔듦 — PCIe SRIS와 연결)
  - 증상: UART 글자 깨짐(baud 오차 = 클럭 오차), 타이머 시간이 틀림, PLL lock 안 됨 → 칩이 낮은 속도로 멈춤
- **꼬리 질문 체인**
  - Q. "UART prints garbage on a new board. What do you suspect?" → "How do you prove it's the clock?" (오실로 bit 폭 측정, clock out 핀) → "The crystal is fine but PLL doesn't lock. Next?"
  - Q. "Why does PCIe need a 100 MHz refclk and what's SSC?" → "What's the difference between common clock and SRIS?" → [LP03](2026-10-31_LP03_link_training_ltssm.html) 연결
  - Q. "Why can probing a crystal pin stop the oscillator?"
- **실습/코드**: PLL 설정 계산기(C: 입력 ref, 목표 freq → N/M/P 후보와 VCO 범위 검사) · UART baud 오차 계산

#### BV05 · 리셋 · strap 핀 · boot mode · reset reason · watchdog (P0)

- 📄 **본문 노트**: [BV05 노트 열기](2026-11-01_BV05_reset_strap_boot_mode.html)
- 🔰 **쉽게 말하면**: 리셋 = **"이제 처음부터 시작해"** 신호. 전원과 클럭이 안정된 뒤에야 풀려야 한다. 리셋이 풀리는 **그 순간** 칩은 몇몇 핀(strap 핀)의 High/Low를 읽어서 "어디서 부팅할까(flash? SD? UART?), 어떤 모드로?"를 결정한다. 그래서 strap 저항 하나가 잘못되면 칩이 엉뚱한 곳에서 부팅한다. watchdog은 SW가 멈추면 강제로 리셋하는 감시견.

- **핵심 개념**
  - 리셋 종류: POR(power-on reset) · external reset pin · warm/cold · system vs core reset · watchdog reset · SW reset(예: Cortex-M AIRCR SYSRESETREQ) · debug reset
  - reset supervisor / voltage monitor, RC 지연, reset tree(누가 누구를 리셋), 블록별 reset deassert(SoC 내부 reset controller)
  - **Strap 핀**: 리셋 해제 시점 샘플링, 풀업/풀다운 값과 IO 누설·외부 장치 간섭(다른 칩이 그 핀을 driving), 샘플된 값 읽는 레지스터
  - Boot mode: BootROM이 보는 boot 장치 순서, fallback(USB/UART 다운로드 모드 — 벽돌 복구용)
  - **Reset reason / reset cause 레지스터**: 왜 리셋됐나(POR·WDT·SW·brown-out·thermal) — 재부팅 루프 디버그 1순위
  - watchdog: 타임아웃·윈도우 watchdog·디버거 halt 시 WDT 멈춤 설정
- **꼬리 질문 체인**
  - Q. "The board keeps rebooting every few seconds. How do you debug it?" → 꼬리1 "Where do you look first?" (reset reason, UART 마지막 로그, scope로 reset 핀·rail) → 꼬리2 "Reset reason says watchdog, but the code kicks it." → 꼬리3 "Reset reason says brown-out." (전원 droop → BV03)
  - Q. "What's a strap pin and what can go wrong with it?" → "The strap is right on the schematic but the chip boots in the wrong mode." (다른 장치가 리셋 중 그 핀을 driving, 풀 저항 너무 약함, 리셋 시점 IO 전원 미안정)
  - Q. "What's the right order of power, clock and reset?" → "Who guarantees it on the board?"
- **실습/코드**: reset reason 디코드 + 부팅 카운터(noinit 영역) 패턴 C 예제 · strap 값 디코드 함수

### C. 첫 전원 인가 · JTAG · 부트

#### BV06 · 첫 전원 인가 날 체크리스트 (smoke test → hello) (P0) ★ 면접 1순위

- 📄 **본문 노트**: [BV06 노트 열기](2026-11-01_BV06_first_power_on_checklist.html)
- 🔰 **쉽게 말하면**: "보드가 방금 도착했다. 첫날 뭐 하나?" 에 대한 **단계별 체크리스트**. 눈으로 보기 → 전원 넣기 전 저항 재기 → 전류 제한 걸고 전원 → rail·클럭·리셋 확인 → JTAG 연결 → SRAM에 작은 코드 올려 LED/UART → 그다음 부트 장치·DDR. 각 단계마다 **통과 기준과 실패 시 다음 행동**을 적는다. 면접에서 가장 많이 나오는 보드 질문.

- **핵심 개념**
  - Day 0 전: 회로도 기반 체크리스트(BV02), 예상 전류 표, 테스트 FW 준비(FPGA에서 검증된 것), 장비 준비, 보드 시리얼·리비전 기록
  - Step 1 육안·현미경 검사(부품 방향·누락·솔더 브리지) → Step 2 rail-GND 저항 → Step 3 전류 제한 첫 전원(낮은 제한 → 점진적) → Step 4 rail·순서·PGOOD → Step 5 클럭 → Step 6 리셋 해제 → Step 7 JTAG IDCODE·halt·SRAM read/write → Step 8 SRAM에 코드 로드·GPIO 토글/UART → Step 9 boot 장치에서 부트 → Step 10 DDR → Step 11 인터페이스
  - 기록: bring-up log(날짜·보드 SN·측정값·rework), 이슈 트래커, 보드별 상태표
  - "한 번에 하나만 바꾸기", 정상 보드와 비교(golden board), rework 기록
- **꼬리 질문 체인**
  - Q. "The board just arrived. Walk me through your first day." → 꼬리1 "What do you measure before applying power?" → 꼬리2 "Power is fine but JTAG sees nothing." (→ BV07) → 꼬리3 "JTAG works, but code in SRAM doesn't print anything." (UART 핀 mux·클럭·baud·TX/RX 뒤바뀜)
  - Q. "You have 5 boards and 2 of them behave differently. What do you do?" → (공통/차이 정리, BOM·rework·칩 lot 비교, 칩 교체 swap test)
  - Q. "How do you avoid destroying the only A0 silicon you have?"
- **Don 연결**: Solidigm 새 FPGA image/FW bring-up 순서 · Apple 신규 보드 무선 칩셋 첫 bring-up `[숫자 채우기]`

#### BV07 · JTAG · SWD · CoreSight · boundary scan (P0)

- 📄 **본문 노트**: [BV07 노트 열기](2026-11-01_BV07_jtag_swd_boundary_scan.html)
- 🔰 **쉽게 말하면**: JTAG = 칩에 달린 **정비용 뒷문**. 디버거(Trace32, J-Link, OpenOCD)가 이 4–5개 선으로 칩에 말을 걸어 "너 누구야(IDCODE)?", "CPU 멈춰", "이 주소 메모리 읽어 줘"를 할 수 있다. 칩이 부팅을 못 해도 쓸 수 있어서 bring-up의 생명줄. **Boundary scan**은 JTAG로 칩 핀을 직접 흔들어 보며 **보드 배선이 맞게 연결됐는지**(납땜 불량·단선) 검사하는 기능 — board validation 용어.

- **핵심 개념**
  - 신호: TCK·TMS·TDI·TDO·(TRST), SWD: SWCLK·SWDIO, VTref(디버거 전압 기준)
  - TAP 상태 머신(16 상태: Test-Logic-Reset, Shift-IR/DR …), IR/DR, IDCODE 구조(version·part·manufacturer), **scan chain**(여러 칩 직렬), IR 길이
  - Arm CoreSight: DP(Debug Port) → AP(Access Port: MEM-AP/APB-AP/AXI-AP) → 코어 debug 레지스터, CTI(멀티코어 동시 halt), ETM/ETB/TPIU trace
  - 디버거 기능: halt/step/breakpoint(HW bp 개수 제한), memory/register read·write, flash 프로그래밍, **connect under reset**, 멀티코어 attach, semihosting
  - Boundary scan(IEEE 1149.1): BSR, EXTEST/SAMPLE/PRELOAD, **BSDL 파일**, 보드 interconnect test(open/short 검출), DDR 핀 연결 확인
  - 보안: JTAG lock/secure debug, 양산 칩은 막혀 있음
- **꼬리 질문 체인**
  - Q. "JTAG can't read IDCODE on a new board. What could be wrong?" → 꼬리1 "How do you narrow it down?" (VTref·전원, TCK 낮추기, TDO 스코프, chain 길이, TRST/리셋 상태, 칩이 리셋에 갇힘) → 꼬리2 "IDCODE reads but you can't halt the core." (코어 전원 도메인 off, 클럭 없음, debug 권한, WFI) → 꼬리3 "How would you debug the very first instruction after reset?" (connect under reset·vector catch)
  - Q. "What's boundary scan and why would a board team use it?" → "How would you test DDR connectivity without DDR training?"
  - Q. "How do you halt all cores at the same time on a multicore SoC?" (CTI)
- **Don 연결**: Trace32(Lauterbach)로 Cortex-R8/R82/M0+ 멀티코어 bring-up·디버그 — **강점 카드**
- **실습/코드**: TAP 상태 머신 시뮬레이터 + IDCODE 디코드 (C, macOS에서 실행) · OpenOCD 설정 파일 읽는 법 `[미실행]`

#### BV08 · 부트 흐름 · bring-up 테스트 펌웨어 (P1)

- 📄 **본문 노트**: [BV08 노트 열기](2026-11-01_BV08_boot_flow_bringup_firmware.html)
- 🔰 **쉽게 말하면**: 칩이 켜지면 **내장 ROM 코드 → 1단계 부트로더(작아서 칩 안 SRAM에 들어감) → DDR 초기화 → 큰 부트로더/OS** 순서로 깨어난다. Bring-up 초기에는 DDR이 아직 안 되니까 **SRAM에서만 도는 작은 테스트 펌웨어**를 만들어 하나씩 블록을 켜고 검사한다. 이 노트는 그 부트 흐름과 테스트 FW 구조(테스트 목록·결과 출력·자동화 연동)를 다룬다.

- **핵심 개념**
  - 부트: BootROM(boot mode 읽기·이미지 인증·로드) → FSBL/SPL(클럭·DDR init) → TF-A(BL31)/U-Boot → Linux 또는 bare-metal 앱 · Zynq UltraScale+: PMU FW·FSBL·BOOT.BIN [LP16 연결]
  - Bring-up 테스트 FW 구조: startup(벡터·스택·.bss·.data) → 최소 UART 드라이버 → **test registry**(이름·함수·타임아웃) → 결과 포맷(PASS/FAIL + 레지스터 덤프, 기계가 파싱하기 쉽게) → 커맨드 셸(UART로 테스트 선택) → JTAG/semihosting 대체 출력
  - 테스트 종류: 레지스터 reset 값·RW 테스트, 메모리 테스트, 인터럽트 트리거, DMA, 타이머, 블록별 directed test, self-checking
  - FPGA ↔ 실리콘 재사용: 클럭 비율·타임아웃 파라미터화, 플랫폼 HAL 분리
  - 이미지: 링커 스크립트(SRAM/DDR 영역), 이미지 헤더, 다운로드(JTAG load / UART / USB)
- **꼬리 질문 체인**
  - Q. "How would you structure a bring-up test firmware?" → "How does the automation know a test passed?" → "How do you keep it running on FPGA and silicon?" → "What do you do when a test hangs the system?" (watchdog·타임아웃·JTAG로 PC 확인·리셋 후 다음 테스트)
  - Q. "Walk me through the boot flow of an Arm SoC." → "Where does DDR get initialized and why there?" → "How do you debug a hang before the UART is up?"
  - Q. "Write a register reset-value / read-write test." → "What registers must you NOT write?" (W1C·RO·side-effect·self-clearing)
- **Don 연결**: Solidigm bare-metal C pre/post-silicon 테스트 FW · SK hynix 테스트 플랫폼 SDK
- **실습/코드**: test registry + 결과 포맷 + 레지스터 RW 테스트(mock 레지스터 파일) C 예제, ASan 빌드로 검증

### D. 메모리 · 인터페이스

#### BV09 · DDR/LPDDR bring-up · training · 메모리 테스트 (P0)

- 📄 **본문 노트**: [BV09 노트 열기](2026-11-01_BV09_ddr_bringup_training_memtest.html)
- 🔰 **쉽게 말하면**: DDR은 **엄청 빠른 박자로 데이터를 주고받는 외부 메모리 칩**. 너무 빨라서 선 길이 차이·온도·전압에 따라 데이터가 도착하는 시점이 조금씩 달라진다. 그래서 켤 때마다 컨트롤러가 **"언제 데이터를 잡아야 정확한가"를 스스로 찾는 training** 을 한다. 그다음 **메모리 테스트**(모든 칸에 쓰고 다시 읽기, 여러 패턴)로 깨지지 않는지 확인한다. SoC bring-up에서 가장 자주 막히는 단계.

- **핵심 개념**
  - 구성: memory controller(명령 스케줄링) + **PHY**(아날로그 타이밍) + DRAM 칩, 채널·랭크·뱅크·row/column, DDR4/DDR5/LPDDR4x/LPDDR5(자동차 SoC는 LPDDR 많음)
  - 초기화 순서: 전원·클럭 → reset → ZQ calibration(임피던스) → mode register 설정(MR) → **training**: CA training · write leveling · read gate(DQS gate) · read eye(DQ/DQS deskew) · write eye · **VREF training**
  - 파라미터 출처: 메모리 데이터시트 타이밍(tRCD/tRP/tRAS…), 벤더 PHY 툴·초기화 코드, 보드 레이아웃(길이)
  - 메모리 테스트: data bus test(walking 1s/0s), **address bus test**(2의 거듭제곱 주소 — 단락/단선), **March C-**(셀 결함), moving inversion, random pattern, 캐시 끄고/켜고, DMA 부하로 stress, 고온
  - ECC(SECDED, 에러 카운터·주입), refresh, 온도와 refresh 주기
  - Margin: training 결과(eye 폭) 레지스터 덤프, 보드·온도별 비교 → BV12
- **꼬리 질문 체인**
  - Q. "DDR training fails on a new board. How do you debug?" → 꼬리1 "Which training step fails tells you what?" → 꼬리2 "It passes at room temp but fails at hot." (마진 부족·VREF·refresh) → 꼬리3 "Only 2 of 10 boards fail." (보드 편차·솔더·칩 lot — swap test)
  - Q. "Write a memory test." → "Why does walking-1s on the data bus not find address-line shorts?" → "What does March C- detect that a simple write/read doesn't?" → "Why disable the cache — or why not?"
  - Q. "What's write leveling and why is it needed?" (fly-by 토폴로지 → DQS와 CK 도착 시간 차이)
  - Q. "How would you validate ECC?" (에러 주입, 카운터, 인터럽트, 정정/비정정 경로)
- **Don 연결**: Solidigm SRAM/DRAM bring-up(FPGA) · SK hynix NAND/메모리 신뢰성·shmoo — 강점 카드 `[숫자 채우기]`
- **실습/코드**: **C 메모리 테스트 세트** (data bus · address bus · device/March C- · 결함 주입 시뮬레이션으로 각 테스트가 무엇을 잡는지 증명) — macOS ASan 빌드로 검증. 코딩 라운드 대비도 겸함.

#### BV10 · 보드 인터페이스 테스트 (P1)

- 📄 **본문 노트**: [BV10 노트 열기](2026-11-01_BV10_board_interface_tests.html)
- 🔰 **쉽게 말하면**: 칩과 메모리가 살아나면, 보드 위의 **모든 연결(센서·플래시·네트워크·카메라·PCIe 장치)** 이 제대로 이어졌는지 하나씩 확인한다. 원칙은 "가장 간단한 것으로 연결부터 증명 → 그다음 속도와 데이터 정확도". 예: I2C는 주소 스캔, SPI flash는 ID 읽기, UART는 loopback, Ethernet은 PHY 레지스터 읽기 → loopback → PRBS, PCIe는 링크 상태, 카메라는 테스트 패턴.

- **핵심 개념** (인터페이스별 "연결 증명 → 기능 → 성능·에러" 3단계)
  - I2C: 풀업·전압 확인 → 주소 스캔 → WHO_AM_I 레지스터 → stuck bus 복구(SCL 9 클럭) → 속도별·클럭 스트레칭
  - SPI: mode(CPOL/CPHA) → flash JEDEC ID(0x9F) → read/write/erase → 고속 클럭·지연 샘플링
  - UART: TX 확인 → 외부 loopback(TX-RX 단락) → baud 오차·흐름 제어
  - GPIO: 출력 토글 scope, GPIO-GPIO loopback, 인터럽트 엣지
  - Ethernet: PHY strap/주소 → **MDIO로 PHY ID 레지스터(2·3)** → link up(autoneg) → PHY loopback / MAC loopback / 외부 loopback → PRBS·BER, 카운터(CRC 에러) — 자동차 100/1000BASE-T1
  - PCIe: 링크 상태(LnkSta)·열거 → [LP 트랙](2026-10-31_LP00_linux_pcie_track.html)
  - MIPI CSI-2: 센서 I2C 설정 → 센서 **test pattern**(color bar) → 프레임 카운트·CRC/ECC 에러 카운터 → 실제 영상 · serializer/deserializer(GMSL/FPD-Link) [추정: Rivian 카메라 링크 구성 미확인]
  - eMMC/UFS/QSPI flash, USB, CAN(자동차)
  - 보드 테스트 스위트: 위를 모아 **자동 실행 + PASS/FAIL 표** (→ BV14)
- **꼬리 질문 체인**
  - Q. "How would you validate every interface on a new board?" → "What's the minimum test that proves connectivity?" → "How do you go from 'it works' to 'it's validated'?" (속도·온도·에러율·장시간)
  - Q. "I2C bus is stuck low. What do you do?" → "Why 9 clocks?" → "How do you prevent it?"
  - Q. "Ethernet link doesn't come up." → "What loopback modes would you use to isolate MAC vs PHY vs cable?" → "How do you measure BER?"
  - Q. "How do you validate a camera input without a real scene?" (센서 test pattern, CSI-2 RX 에러 카운터)
- **실습/코드**: I2C 버스 복구 bit-bang 시뮬레이션 (가상 GPIO 모델에서 slave가 SDA를 잡고 있는 상황 재현 → 9 클럭 + STOP으로 해제), C

### E. 측정 · 마진

#### BV11 · 랩 장비와 측정 (P1)

- 📄 **본문 노트**: [BV11 노트 열기](2026-11-01_BV11_lab_instruments_measurement.html)
- 🔰 **쉽게 말하면**: 보드 검증은 결국 **재는 일**. 전압은 멀티미터(DMM), 시간에 따라 변하는 신호는 오실로스코프, 여러 디지털 선의 0/1 순서는 로직 애널라이저, 프로토콜 내용(I2C 주소, PCIe 패킷)은 프로토콜 애널라이저, 전류·전력은 파워 애널라이저. 각 장비를 **언제, 어떻게(프로브·트리거·대역폭) 쓰고, 어떻게 잘못 재는지** 아는 게 핵심.

- **핵심 개념**
  - DMM(전압·저항·도통), bench PSU(전류 제한·원격 감지·리모트 제어 SCPI), electronic load
  - **Oscilloscope**: 대역폭(신호 상승 시간 대비 3–5배), 샘플링, 프로브(passive 10×·active·differential·current), **ground lead 길이**, 트리거(edge·pulse width·serial decode), 다채널 타이밍, 측정(rise time·overshoot·ripple), BW limit
  - Logic analyzer: 채널 수·샘플 속도·임계 전압, 프로토콜 디코드(I2C/SPI/UART)
  - Protocol analyzer: PCIe(LeCroy/Teledyne), USB, Ethernet — 언제 scope 대신 쓰나
  - Power analyzer / current probe / shunt, 열화상 카메라, thermal chamber/thermostream, BERT(고속 링크)
  - 자동화: **SCPI over LAN/USB (PyVISA)** 로 장비 제어 → BV14
- **꼬리 질문 체인**
  - Q. "How would you measure the power-up sequence of 6 rails?" → "Your scope has 4 channels." (트리거 기준 rail 공유·두 번 캡처·LA 사용·PGOOD 신호 활용)
  - Q. "When do you use a logic analyzer vs a scope vs a protocol analyzer?"
  - Q. "Your rise-time measurement doesn't match the spec. What could be wrong with your setup?" (스코프·프로브 대역폭, 프로브 부하, ground loop)
  - Q. "How would you automate measurements across 50 boards?" (SCPI/PyVISA, 결과 DB)
- **Don 연결**: Apple — DSO·protocol analyzer로 PCIe/I2C/SPMI/RFFE 루트코즈 · 레쥬메 "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer"
- **실습/코드**: PyVISA 스타일 장비 추상화 + mock scope로 rail 측정 자동화 스크립트 (Python, mock으로 실행 검증)

#### BV12 · SI/PI 기초 · margining · shmoo · PVT 특성화 (P1)

- 📄 **본문 노트**: [BV12 노트 열기](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)
- 🔰 **쉽게 말하면**: "된다"와 "**여유 있게** 된다"는 다르다. 고속 신호는 선을 지나며 찌그러지고(SI, signal integrity), 전원은 부하에 따라 흔들린다(PI, power integrity). 검증은 **조건을 일부러 나쁘게 만들어(전압 ↓, 온도 ↑, 속도 ↑)** 어디까지 버티는지 재는 것. 전압×주파수 격자를 PASS/FAIL로 칠한 그림이 **shmoo plot**, 공정(Process)·전압(Voltage)·온도(Temperature) 끝 조건 조합이 **PVT 코너**.

- **핵심 개념**
  - SI: 반사·임피던스 불연속, 손실(insertion loss), crosstalk, **eye diagram**(eye height/width), jitter, BER(10^-12 등)과 PRBS
  - PI: IR drop, droop(부하 계단), PDN 임피던스, decoupling, 노이즈가 타이밍에 주는 영향
  - Margining: 전압 margining(PMIC로 rail ±%), 타이밍 margining(DDR training 결과, PCIe Lane Margining at Receiver — Gen4+), 주파수 margining
  - **Shmoo**: 축 2개(예: Vcore × freq) 스윕 → PASS/FAIL 지도 → 동작 영역·마진·이상 패턴(구멍·벽) 해석
  - **PVT**: process corner(FF/TT/SS 칩 — skew lot), voltage(nominal ±5–10%), temperature(자동차 −40~125 °C) · 샘플 수·통계(분포·Cpk)
  - 특성화 결과로 하는 일: 스펙 결정, AVS 테이블, 양산 스크린 조건, HW 수정 요청
- **꼬리 질문 체인**
  - Q. "What's a shmoo plot and how do you read it?" → "There's a hole in the middle of the passing region — what does that mean?" → "How would you automate a shmoo on 20 boards in a chamber?"
  - Q. "How would you characterize DDR margin across PVT?" → "How many samples is enough?" → "What would you report to the design team?"
  - Q. "What's an eye diagram and what closes the eye?"
  - Q. "What are process corners and how do you get them in post-silicon?" (skew lot / corner lot)
- **Don 연결**: SK hynix chamber 신뢰성 테스트 플랫폼 자동화 · NAND/PCIe 3/4/5 고속 인터페이스 신뢰성 · **shmoo** — 강점 카드 `[숫자 채우기]`
- **실습/코드**: shmoo 데이터 생성(모델) + 텍스트/PNG shmoo 플롯 + 마진 계산 Python 스크립트

### F. 디버그 · 계획 · 실전

#### BV13 · 보드 디버그 시나리오 12선 (P0) ★ 도메인 라운드 단골

- 📄 **본문 노트**: [BV13 노트 열기](2026-11-01_BV13_board_debug_scenarios.html)
- 🔰 **쉽게 말하면**: "이런 증상이면 어떻게 디버그하나?" 12가지. 원칙은 PCIe와 같다 — **아래 계층(전원·클럭·리셋)부터 증명**하고, **한 번에 하나만 바꾸고**, **정상 보드와 비교**하고, **HW냐 SW냐를 실험으로 가른다**(swap test·다른 FW·다른 보드).

| # | 증상 | 먼저 볼 것 | 주요 가설 |
|---|---|---|---|
| 1 | 전원 넣자마자 **전류 제한 걸림** | rail-GND 저항, 열화상 | 합선, 부품 방향, 솔더 브리지 |
| 2 | rail 정상인데 **JTAG IDCODE 안 읽힘** | VTref, TCK 낮춤, reset 핀, 클럭 | 칩 리셋 갇힘, osc 없음, JTAG 레벨·배선 |
| 3 | JTAG 되는데 **UART 출력 없음** | PC 위치(halt), 핀 mux, baud, scope TX | 클럭 설정, 핀 mux, TX/RX 뒤바뀜 |
| 4 | **계속 재부팅** | reset reason, 마지막 로그, rail scope | watchdog, brown-out, 예외 → 리셋 |
| 5 | 엉뚱한 **boot mode** | strap 샘플 레지스터, strap 저항 | 저항 값, 다른 장치가 핀 driving |
| 6 | **DDR training 실패** | 실패 단계, training 로그 | 파라미터, VREF, 레이아웃, 솔더 |
| 7 | 상온 OK, **고온에서만 실패** | 온도 스윕, 에러 위치·패턴 | 마진 부족, refresh, 클럭 jitter |
| 8 | **10장 중 2장만** 실패 | BOM·rework·칩 lot 비교 | 보드 편차, 실장 불량, 칩 corner |
| 9 | **cold boot OK, warm reset 실패** | 리셋 경로별 초기화 | 리셋 안 되는 블록, PMIC 상태 유지 |
| 10 | 부하 시 **랜덤 hang** | rail droop scope, 전류 | PI(droop), DVFS 전환, 열 |
| 11 | I2C 장치 **NACK** | scope SCL/SDA, 주소, 풀업 | 주소 strap, 전원 off, 레벨, stuck bus |
| 12 | 보드 rev B에서 **새로 생긴** 실패 | rev A↔B 변경 목록 | ECO 영향, 부품 대체 |

- 각 시나리오 본문: 증상 → **잴 것·볼 것** → 가설 우선순위 → **HW/SW 가르는 실험** → 결론·재발 방지(회귀 테스트·체크리스트 추가) → **영어 2분 답변**
- **꼬리 질문 패턴**: "What's the first thing you check?" → "That's fine. What next?" → "How do you prove it's the board and not the silicon or the software?" → "How do you make sure it never comes back?"
- **Don 연결**: Apple 무선 칩셋 PCIe/I2C 루트코즈 스토리를 이 틀로 재작성 `[숫자 채우기]`

#### BV14 · 보드 검증 계획 · 보드 팜 · 자동화 · HW/SW 버그 가르기 (P0)

- 📄 **본문 노트**: [BV14 노트 열기](2026-11-01_BV14_board_validation_plan_automation.html)
- 🔰 **쉽게 말하면**: 한 장의 보드를 살리는 것과 **수십 장의 보드를 매일 검증하는 체계**는 다른 일. 검증 계획(무엇을·어떤 조건에서·몇 장·통과 기준), 보드 팜(전원·UART·JTAG를 원격으로 제어하는 랙), 자동화(Python으로 테스트 실행·결과 수집·대시보드), 그리고 실패가 나오면 **HW 문제인지 SW 문제인지 가르고** 해당 팀에 넘기는 프로세스. Staff 레벨 시그널이 가장 많이 나오는 주제.

- **핵심 개념**
  - 검증 계획 구조: 범위 → 요구사항 추적(spec 항목 ↔ 테스트) → 테스트 매트릭스(기능·인터페이스·전원·클럭·리셋·성능·PVT·스트레스·soak) → 조건(온도·전압·보드 수·칩 corner) → 통과 기준 → 일정·우선순위 → 리스크
  - 보드 팜: 원격 전원(PDU·relay), UART 서버, JTAG 프로브 풀, 부트 모드 제어(GPIO/relay), 카메라/장비, 보드 인벤토리(SN·rev·rework·칩 lot)
  - 자동화: Python 오케스트레이터(pytest 스타일), 장비 SCPI, 결과 DB·대시보드, 야간 회귀, flaky 테스트 관리, 재현 패키지(로그·덤프·조건)
  - **HW/SW triage**: 재현성 → 다른 보드/칩(swap) → 다른 FW 버전(bisect) → 조건 스윕 → 측정 증거 → 버그 리포트(증상·조건·재현률·증거·가설) · errata/workaround
  - 보드 리비전 EVT/DVT/PVT, ECO, 회귀 범위 결정
  - 메트릭: 커버리지(요구사항 대비), pass rate, MTBF/soak 시간, 버그 burn-down
- **꼬리 질문 체인**
  - Q. "Write a validation plan for this new board." → 꼬리1 "How do you prioritize with only 3 weeks?" → 꼬리2 "How many boards do you need and why?" → 꼬리3 "How do you know you're done?"
  - Q. "How would you build automation for 50 boards running nightly?" → "A test fails 1 in 200 runs. What do you do?" → "How do you keep the farm healthy?"
  - Q. "How do you decide whether a failure is hardware or software?" → "The HW team says it's software. How do you convince them?" (데이터·swap·측정) ← behavioral과 겹침
- **Don 연결**: SK hynix chamber 테스트 플랫폼 SDK·자동화 · Solidigm 검증 · factory test-node 설계 — **Staff 시그널 카드**
- **실습/코드**: 보드 팜 오케스트레이터 골격 (Python: 보드 인벤토리 → 전원 제어(mock) → UART 로그 파싱 → 결과 JSON/표) — mock 실행 검증

#### BV15 · 자동차 보드 고려사항 (P2)

- 📄 **본문 노트**: [BV15 노트 열기](2026-11-01_BV15_automotive_board_considerations.html)
- 🔰 **쉽게 말하면**: 차 안의 보드는 **−40 °C 겨울부터 엔진룸 열기까지**, 시동 걸 때 배터리 전압이 확 떨어지는 순간(cold crank)까지 견뎌야 한다. 그래서 부품 온도 등급(AEC-Q100 Grade), 12 V 전원 이벤트, 전자파(EMC), 기능 안전(ISO 26262)이 검증 조건에 들어간다. 깊게는 몰라도 **용어와 "검증 조건이 이렇게 달라진다"** 정도는 말할 수 있으면 됨.

- **핵심 개념**: AEC-Q100 Grade 0–3(온도 범위), 12 V 시스템 전원 이벤트(cold crank·load dump — ISO 16750), EMC(CISPR 25) 개요, ISO 26262 ASIL·safety mechanism(ECC·lockstep·BIST·watchdog) 검증 관점, 자동차 Ethernet(100/1000BASE-T1)·카메라 SerDes(GMSL/FPD-Link) 개요, 장수명·추적성
- **꼬리 질문**: "What's different about validating an automotive board vs a consumer one?" → "How would temperature grade change your test plan?" → "How do you validate a safety mechanism like ECC or a watchdog?" (fault injection)
- **Don 연결**: 데이터센터 SSD(고신뢰성·PLP·온도 테스트) ↔ 자동차 신뢰성 공통점

#### BV16 · 모의 면접 Q 뱅크 (P1)

- 📄 **본문 노트**: [BV16 노트 열기](2026-11-01_BV16_mock_interview_qbank.html)
- BV01–BV15에서 **30문항** 추출, 각 문항에 꼬리 3단 + 영어 30–60초 답 + 한국어 핵심 + 해당 BV 노트 링크
- 사용법: 질문만 보고 소리 내 답 → 펼쳐서 비교 → 막힌 것만 해당 BV 노트로

## 3. 진행 순서 제안

| 순서 | 노트 | 이유 | 예상 시간 |
|---|---|---|---|
| 1 | **BV06 + BV01** | "Day 1" 질문과 전체 지도. 나머지 노트의 허브 | 2 h |
| 2 | **BV03 + BV05** | 전원·리셋 — 보드 디버그 질문의 1차 가설 | 2 h |
| 3 | **BV07** | JTAG — Don 강점(Trace32), 말로 정리만 | 1 h |
| 4 | **BV09** + 메모리 테스트 코드 | DDR 질문 + 코딩 라운드(메모리 테스트) 겸용 | 2–3 h |
| 5 | **BV13** | 시나리오 질문, 다른 노트를 끌어오는 허브 | 2 h |
| 6 | **BV14** | Staff 시그널(계획·자동화·triage) + Don 강점 | 1.5 h |
| 7 | BV02 · BV12 · BV11 | 회로도·shmoo·장비 — 꼬리 질문 방어 | 각 1 h |
| 8 | BV16 Q 뱅크 | D-3 모의 면접용 | 1 h |
| (여유) | BV04 · BV08 · BV10 · BV15 | | |

## 4. Don이 확인하거나 채울 것 (세분화 체크리스트)

> 노트 본문에 `[숫자 채우기]` 로 남겨 둔 칸 + 면접 전에 Don만 답할 수 있는 것.

- [ ] **Solidigm FPGA bring-up**: 보드/플랫폼 이름(HAPS? 자체 FPGA 보드?), 처음 살릴 때 막혔던 것 1개, 걸린 기간 → BV01·BV06·BV08
- [ ] **실리콘 bring-up 직접 경험 여부**: Solidigm/SK hynix에서 첫 실리콘(A0) 도착 시 Don이 직접 한 일 vs 옆에서 본 일 — 과장 없이 구분 → BV01
- [ ] **DRAM bring-up**: FPGA에서 한 DRAM bring-up이 컨트롤러 IP 설정 수준인지 training까지인지 → BV09
- [ ] **shmoo/PVT 숫자**: SK hynix chamber 온도 범위, 보드/샘플 수, 자동화로 줄인 시간 → BV12·BV14
- [ ] **측정 장비 실사용**: 써 본 scope/LA/protocol analyzer 모델, 가장 기억나는 측정 1건 (Apple 루트코즈) → BV11·BV13
- [ ] **Apple 루트코즈 스토리 1개**를 BV13 틀(증상 → 측정 → 가설 → HW/SW 가르기 → 결론)로 숫자 포함 정리
- [ ] (리크루터 콜) 도메인 라운드가 **보드 쪽인지 PCIe/시스템 쪽인지** 물어보기 → 우선순위 조정

## 5. 노트 작성 규칙

- 파일: `2026-11-01_BVxx_<slug>.md` + HTML 렌더 (이 리스트 페이지가 인덱스), 코드: `onsitePrep/bvXX/`
- 섹션: **0 TL;DR → 1 🔰 처음 보는 사람을 위한 설명(비유·그림·용어 사전) → 2 핵심 개념(정확한 버전) → 3 보드에서 볼 것·잴 것 → 4 꼬리 질문 체인(English Q + 영어 모범 답 + 한국어 핵심, `<details>`) → 5 Don 경험 연결 → 6 실습/코드(검증된 것만) → 7 자주 틀리는 것 → 8 출처**
- 사실 신뢰도: 수치·레지스터·스펙 번호는 확인된 것만, 불확실하면 `[추정]`, 실행 안 한 코드는 `[미검증]`
- 작성 스펙 전문: `onsitePrep/BV_NOTES_SPEC.md`

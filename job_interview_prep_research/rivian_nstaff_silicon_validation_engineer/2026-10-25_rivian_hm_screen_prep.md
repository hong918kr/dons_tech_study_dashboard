<!-- theme: pastel -->
# Rivian — Technical Screen 준비 Deck (Manjiang Zhang · 45분)

> **작성일**: 2026-10-25(일) · **갱신**: 2026-10-28(수) · **인터뷰**: ~~2026-10-28(수)~~ → **2026-10-30(금)** 45분 (Don 일정으로 연기 · 시간 확인 필요) · **면접관**: **Manjiang Zhang**, Sr. Engineer/Manager (Silicon Validation) @ Rivian — 10-28 **다시 Manjiang으로 확정** · ~~Iris Lee~~ (잠시 변경 예정이었음, §1B에 보관)
> **같이 볼 것**: [코딩 연습 사이트 (V1–V12 + L1–L5 + D)](hmScreenPrep/index.html) · [포지션 컨텍스트](rivian_nstaff_silicon_validation_engineer_context.html)
> 신뢰도: `[확인됨]` = LinkedIn 경력란·공개 특허·JD · `[추정]` = 그것을 바탕으로 한 판단. LinkedIn은 열람만 했고 연결 요청·팔로우·메시지는 하지 않았다.

---

## 0. TL;DR

- **(10-29 추가) 전날 2–3시간만 있으면 → [C 기본기 벼락치기](2026-10-29_c_basics_cram.html)**: strcpy·memmove · revstr · atoi · 64-bit timer(hi-lo-hi) · ring buffer · GENMASK/FIELD_PREP·W1C · bit tricks · endian · 개념 퀵파이어. 코드는 `cBasicsCram/cram_all.c` (ALL PASS).

- **(10-28 재확정) 면접관 Manjiang은 20년차 "하드웨어 사람"이다** [확인됨]: Cisco 패킷 프로세서 ASIC(2006–10) → Marvell **Ethernet PHY ASIC**(2010–12) → Brocade FPGA 패킷·암호화(2012–15) → **Baidu USA 자율주행 컴퓨트 HW 아키텍트·팀 리드**(2015–22, FPGA 센서 데이터 처리·알고리즘 가속, L4 차량에 배포) → **Rivian**(2022-12~, 자율주행 실리콘: 아키텍처 리뷰, **FPGA/HAPS 프로토타이핑, silicon bring-up, post-silicon validation, PVT characterization, production SLT**, bring-up용 FPGA 플랫폼 개발)
- **함의**: 이 사람은 SW 알고리즘 퀴즈보다 **"실리콘이 처음 나왔을 때 네가 뭘 어떤 순서로 하느냐"**를 볼 확률이 높다. 질문은 bring-up 순서, FPGA vs silicon 차이, PVT/shmoo, 테스트 자동화, 인터페이스(PCIe·DDR·Ethernet·카메라) 디버깅으로 올 것 [추정]
- **Don의 최고 카드 3장**: ① Solidigm **RTL freeze 전 FPGA SoC 검증 + Cortex-R bring-up** ② SK hynix **shmoo / 고속 인터페이스 reliability 시스템** (= 그의 PVT characterization 언어) ③ Apple **새 칩 시스템 통합 루트코즈 + factory test-node** (= 그의 SLT 언어)
- **45분 예상**: 소개 5 → 경험 딥다이브 15 → 기술 질문/디버깅 시나리오 15 → (짧은 코딩 or 설계 5–10) → 역질문 5 [추정]
- **Iris Lee 건 (10-28 잠깐 변경됐다가 취소)**: 분석은 §1B에 보관. 그때 만든 **§5.5 Q25–Q38**과 **코딩 L1–L5**는 버리지 않음 — PCIe·Zynq Petalinux·Ethernet·벤치마크는 **JD 우대사항**이라 Manjiang도 물을 수 있음. 특히 [L2 PCIe](hmScreenPrep/problems/L2_pcie_cap_walk/index.html)(§5.3 Q16과 연결)와 [L1 Zynq](hmScreenPrep/problems/L1_uio_mmio_poll/index.html)(Q23)는 P1로 유지
- **역할 해석 (10-27)**: 이 자리는 **silicon validation의 SW 담당**이 맞고, **RTL은 필수가 아님** (JD에 RTL/Verilog 0회, 같은 조직 RTL·DV 공고는 따로 Verilog/UVM 요구). 필요한 건 HW를 SW 쪽에서 정확히 다루는 능력 + RTL은 "읽을 줄 아는 정도"면 플러스 (§0.5)
- **이번 주 플랜 (10-28 Manjiang 재확정 기준)**: 수(오늘, 일정 있음) §1 + §3 TMAY + S1/S2 숫자 · 목 V2 → V1 → V3 → V7 + D + §5.1·§5.3 + 모의 면접 · 금 아침 치트시트만 (§10)

---

## 0.5 역할 해석 — silicon validation을 하는 자리인가? RTL을 읽어야 하나? (10-27 추가)

### 결론 먼저
- **네, silicon validation을 하는 자리입니다** [확인됨 — JD]. JD Role Summary 첫 문장이 직함을 **"Staff Silicon Validation Software Engineer"**로 적고 있습니다. 공고 제목의 "Embedded Software Engineer"는 HR 직군명이고, 실제 하는 일은 silicon validation입니다.
- **"Work w/ SoC validation team"의 뜻**: 칩을 검증하는 SoC validation 팀(Manjiang 팀: lab·보드·FPGA/HAPS·PVT·SLT)이 따로 있고, 이 자리는 그 팀과 함께 **검증 계획을 다듬고(refine) 직접 실행하는(execute) SW 담당**입니다 [추정 — JD 문장 + 면접관 직무]. 남의 테스트를 지원만 하는 자리가 아니라, **테스트 코드·자동화 툴·커버리지 리포트·벤치마크를 만드는 사람**입니다.
- **RTL은 필수가 아닙니다** [확인됨 — JD + 같은 조직 공고 비교]. 34042 JD에는 RTL·Verilog·SystemVerilog·UVM이 **한 번도 안 나옵니다**. 필수 요건의 기술 항목은 "Proficient in Embedded C" 하나뿐입니다. 반면 같은 Palo Alto 실리콘 조직의 다른 공고는 RTL을 분명히 요구합니다.
  - RTL Design Engineer (req 33461): "Strong proficiency in Verilog and SystemVerilog"
  - Sr. Staff ASIC Design Verification Engineer (req 30325): SystemVerilog/UVM, formal(SVA)
  - 즉 RTL을 쓰고 시뮬레이션으로 검증하는 사람은 **따로 있고**, 이 자리는 그 RTL이 FPGA나 실제 칩에 올라간 뒤 **C 코드로 돌려 보는 쪽**입니다.
- **"HW를 아주 잘 알아야 한다"는 반만 맞습니다** [추정]. 회로나 RTL을 설계할 줄 알 필요는 없습니다. 하지만 **HW를 SW 쪽에서 정확하게 다룰 줄 아는 것**은 필수입니다: 레지스터 스펙, 블록 다이어그램, 버스 프로토콜, clock/reset/power 순서, cache·DMA·interrupt, 스코프·로직 애널라이저·JTAG. 이건 Don이 Solidigm·Apple에서 매일 하던 일입니다.
- **RTL을 "조금 읽을 줄 아는 것"은 플러스입니다**. 테스트가 실패했을 때 디자이너와 대화하려면 Verilog 레지스터 블록 하나쯤은 읽을 수 있어야 편합니다. 이번 인터뷰용으로는 아래 §0.5.5의 30분 크래시 코스면 충분합니다.

### 0.5.1 JD 문장별 의미

| JD 문장 | 실제 의미 | RTL 필요? |
|---|---|---|
| "Join as **Staff Silicon Validation Software Engineer** in Autonomy Hardware and Sensing Systems team" | 직무 자체가 silicon validation의 SW 담당 | ❌ |
| "**Work w/ SoC validation team** to refine and execute the SoC validation plan" | 검증팀(HW 쪽)과 계획을 같이 다듬고, 그중 SW로 하는 부분을 직접 실행 | ❌ |
| "Use **C embedded software** to validate SoC … **enable** SoC internal functional blocks and peripheral interfaces" | 레지스터 스펙을 보고 블록을 켜는 bare-metal C 코드 + 그 블록을 검증하는 테스트 | 🟡 스펙이 애매할 때 RTL을 같이 보면 좋음 |
| "Develop **automatic testing tools** and generate **test coverage reports**" | 테스트 러너·회귀 자동화·커버리지 리포트 (V5) | ❌ |
| "Build **benchmark tools** … throughput, latency, resource utilization, IO or compute frequency" | 성능 측정 툴: DMA·NPU·메모리 대역폭, frequency별 성능 (V4, V7) | ❌ |
| "Document validation progress … **traceability**" | 요구사항 → 테스트 → 결과 연결 (Jira류) | ❌ |
| Required: "**Proficient in Embedded C**" | 유일한 필수 기술 | ❌ |
| Preferred: Zynq Petalinux · AI accel · JTAG/I2C/UART/SPI/Ethernet/DDR/PCIe · MIPI/ISP | FPGA 프로토타입 플랫폼 + 인터페이스 경험. 전부 **SW 쪽에서 HW를 다루는 경험** | ❌ |

### 0.5.2 칩 개발 흐름에서 이 자리의 위치

```
 Architecture ─► RTL Design ─► DV (simulation, UVM) ─► Emulation / FPGA·HAPS ─► Tape-out ─► First silicon
  (SoC Architect)  (req 33461)   (req 30325)            ▲ pre-silicon 검증            │
                                                        │                             ▼
                                                        │        Bring-up ─► Post-silicon validation ─► PVT char ─► Production test (ATE / SLT)
                                                        │           ▲                  ▲                    ▲              ▲
                                                        └───────────┴──────────────────┴────────────────────┴──────────────┘
                                                           ★ 이 자리 [추정]: 같은 C 테스트·툴을 FPGA 단계부터 실리콘(·양산)까지 계속 돌림
                                                           ◆ Manjiang 팀: FPGA/HAPS 플랫폼, bring-up, post-si validation, PVT, SLT
```

- RTL을 직접 다루는 건 왼쪽 두 칸(RTL Design, DV)입니다.
- 이 자리는 **FPGA 프로토타입이 나오는 시점부터** 들어갑니다. Zynq Petalinux가 우대사항인 이유가 여기 있습니다: Zynq 보드가 FPGA 프로토타입·테스트 플랫폼입니다 [추정].
- Solidigm에서 Don이 한 "RTL freeze 전 FPGA로 SoC verification + 실리콘 나온 뒤 silicon validation 팀과 perf 튜닝"이 정확히 ★ 위치입니다.

### 0.5.3 비슷한 직무 비교 — 헷갈리지 않게

| | DV 엔지니어 (Design Verification) | **Silicon Validation SW (이 자리)** | HW/Lab Validation 엔지니어 | Firmware / Driver 엔지니어 |
|---|---|---|---|---|
| 언제 | tape-out 전 | FPGA 단계 + 실리콘 이후 | 실리콘 이후 | 제품 단계 |
| 무엇을 돌리나 | RTL 시뮬레이션 | **실제 칩/FPGA 위 C 테스트** | 보드·장비(스코프, chamber, PSU) | 제품용 FW/드라이버 |
| 주 언어 | SystemVerilog, UVM | **C**, Python | Python, 장비 스크립트 | C, C++ |
| RTL과의 관계 | 매일 읽고 디버깅 | **필요할 때 읽음 (디자이너와 대화용)** | 거의 안 봄 | 안 봄 |
| 결과물 | 커버리지 closure, sign-off | **검증 테스트·툴·벤치마크·커버리지 리포트** | PVT/margin 데이터, 보드 이슈 | 출시 SW |
| Don 경험 | ❌ | ✅ Solidigm FPGA SoC verification, SK hynix 테스트 플랫폼 | 🟡 SK hynix chamber·shmoo, Apple scope/analyzer | ✅ SSD FW |

### 0.5.4 필요한 HW 지식 — 어디까지 알아야 하나

| 수준 | 내용 | Don 상태 |
|---|---|---|
| **꼭 필요** | 레지스터 스펙 읽기 (RW/RO/W1C/SC, reset 값) | ✅ (V2) |
| **꼭 필요** | 메모리 맵·MMIO·cache/DMA coherency·interrupt 흐름 | ✅ SSD FW (V3, D3) |
| **꼭 필요** | 인터페이스 프로토콜 개념: PCIe link training, DDR training, I2C/SPI/UART, Ethernet, MIPI | ✅ PCIe·I2C·SPI / 🟡 DDR·Ethernet·MIPI (V8–V10) |
| **꼭 필요** | clock·reset·power-up 순서, PLL lock | ✅ bring-up 경험 (§5.1 Q1) |
| **꼭 필요** | 랩 장비로 디버깅: 스코프, 로직/프로토콜 애널라이저, JTAG | ✅ Apple |
| **있으면 좋음** | Verilog 레지스터 블록·간단한 FSM 읽기, 파형(waveform) 보기 | ❌ → §0.5.5로 30분 |
| **있으면 좋음** | AXI(VALID/READY handshake), FPGA ILA(내부 신호 캡처) | 🟡 개념만 → 아래 메모 |
| **있으면 좋음** | PVT, shmoo, Vmin/Fmax, ATE vs SLT | ✅ SK hynix shmoo (V7) |
| **필요 없음** | RTL 설계, 합성, timing closure, UVM 작성, 아날로그 회로 설계 | — |

- **AXI 한 줄**: 보내는 쪽이 `VALID`를 올리고 받는 쪽이 `READY`를 올린 **같은 clock edge에서만** 데이터가 넘어갑니다. 채널은 5개(쓰기 주소 AW, 쓰기 데이터 W, 쓰기 응답 B, 읽기 주소 AR, 읽기 데이터 R). 응답 `SLVERR`/`DECERR` = 버스 에러 (V3에 나옴).
- **ILA 한 줄**: Xilinx FPGA 안에 넣는 로직 애널라이저. 테스트가 실패하면 trigger 조건을 걸어 내부 신호를 캡처하고 파형을 디자이너와 같이 봅니다. 실리콘에는 없어서, 실리콘에서는 debug register·trace·JTAG에 의존합니다.

### 0.5.5 RTL 30분 크래시 코스 — "읽을 줄 아는 정도"

[V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html)의 IRQ 레지스터(offset 0x08)가 실제 RTL로는 이렇게 생겼다고 보면 됩니다 (교육용으로 단순화한 SystemVerilog).

```verilog
// SIF IRQ register (offset 0x08): [2:0] pending (W1C), [8] BUSY (RO), [18:16] IRQ_EN (RW)
module sif_irq_reg (
  input  logic        clk, rst_n,
  input  logic        wr_en,          // bus write strobe for this register
  input  logic [31:0] wdata,
  input  logic [2:0]  hw_irq_set,     // one-cycle pulses from the block's event logic
  input  logic        busy,           // live status from the datapath
  output logic [31:0] rdata,
  output logic        irq_out
);
  logic [2:0] pending;
  logic [2:0] irq_en;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      pending <= 3'b000;                                   // reset value 0
      irq_en  <= 3'b000;
    end else begin
      if (wr_en) irq_en <= wdata[18:16];                   // RW
      pending <= (pending & ~(wr_en ? wdata[2:0] : 3'b000))  // W1C: writing 1 clears
                 | hw_irq_set;                             // HW set wins in the same cycle
    end
  end

  assign rdata   = {13'b0, irq_en, 7'b0, busy, 5'b0, pending};  // reserved bits read as 0
  assign irq_out = |(pending & irq_en);                         // level interrupt to the GIC
endmodule
```

**읽는 순서 (이 5개만 보면 됨)**
1. **포트 목록**: 무엇이 들어오고 나가나. `wr_en`/`wdata`는 버스에서, `hw_irq_set`/`busy`는 블록 내부에서 옵니다.
2. **`if (!rst_n)` 블록**: 각 비트의 **reset 값** → V2의 RESET 체크 기대값.
3. **`always_ff` 안의 대입**: 비트마다 SW 쓰기에 어떻게 반응하나.
   - `irq_en <= wdata` → RW
   - `pending & ~wdata` → W1C
   - 대입이 없는 비트(`busy`) → RO
4. **`assign rdata`**: 읽으면 비트가 어디에 놓이나, reserved는 0인가 → V2 레지스터 맵 표와 같아야 합니다.
5. **경합 (같은 cycle 충돌)**: SW가 1을 써서 지우는 cycle에 HW가 같은 비트를 세우면? 이 코드는 `| hw_irq_set`이 마지막이라 **HW가 이깁니다**. 스펙에 이게 안 적혀 있으면 **디자이너에게 물어볼 질문 + 검증 코너 케이스**가 됩니다. 이걸 짚을 수 있으면 HW 매니저에게 아주 좋은 신호입니다.

- **문법 최소 사전**: `<=` = clock edge에 반영되는 대입(flip-flop), `assign` = 항상 연결된 조합 논리(wire), `3'b000` = 3비트 이진수 000, `{a, b}` = 비트 이어 붙이기, `|x` = x의 모든 비트 OR.
- **파형(waveform) 읽기**: 가로축은 clock, 세로는 신호. "`wr_en`이 1인 cycle에 `wdata[2:0]`이 뭐였고, 다음 cycle에 `pending`이 어떻게 바뀌었나"를 따라가면 됩니다.

### 0.5.6 면접에서 RTL 이야기가 나오면 — 영어 답

> 실제로 한 범위만 말하세요. 아래 `[ ]`는 Don이 사실 여부를 확인해서 채우거나 지우는 부분입니다.

<details>
<summary>Q. "Have you worked with RTL? Can you read Verilog?"</summary>

"I'm not an RTL designer, and I haven't written production Verilog. My side of the boundary is the software that exercises the RTL. At Solidigm, when our firmware tests failed on the FPGA image before RTL freeze, my job was to narrow the failure down to a minimal reproducer — which register, which sequence, which condition — and hand that to the design engineer [with the logs / register trace]. I can read a register block or a simple state machine in Verilog to check reset values and side effects, and I'd get faster at reading RTL and waveforms in the first weeks here."

- 핵심: **정직하게 "설계는 안 했다"** → **내가 RTL과 만나는 지점(재현·축소·디자이너 핸드오프)** → **읽기 정도는 된다 + 빨리 늘릴 수 있다**.
- 이 자리는 RTL을 요구하지 않으니, 이 답이면 감점이 아닙니다.
</details>

<details>
<summary>Q. "A test fails. How do you decide if it's an RTL bug, an FPGA build issue, or a software bug?"</summary>

"First, I make it reproducible and as small as possible. Then I separate the layers:
1. **Software**: does the same test pass on the previous FPGA build, or on another platform like emulation? Did my test change? I check my sequence against the register spec.
2. **FPGA build**: is it only this bitfile? I check timing reports, clock constraints, and whether the block was even included in this build. FPGA-only artifacts are things like RAM models and clock-domain differences.
3. **RTL**: if it reproduces on a second platform, or in the DV simulation with the same sequence, it's likely RTL. I capture the internal signals with an ILA on the FPGA and sit with the designer over the waveform.

Then I file it with the minimal repro, the build ID and the expected-versus-actual values, so it's traceable."
</details>

<details>
<summary>Q. "How would you work with the DV team?"</summary>

"DV and silicon validation should cover each other's blind spots. I'd start from DV's test plan and coverage, so I don't repeat what simulation already proved. I'd focus post-silicon tests on what simulation can't see: real frequency, analog interfaces like the DDR and PCIe PHYs, long-running stress, and system-level interactions across many blocks. Where possible, I'd share test sequences, so the same scenario runs in simulation, on the FPGA and on silicon. And every silicon escape goes back to DV as a new test or coverage point."
</details>

### 0.5.7 이번 인터뷰에서 이 내용을 어떻게 쓸까
- RTL 얘기가 먼저 나오지 않으면 **굳이 꺼낼 필요는 없습니다**. 대신 S1(Solidigm FPGA)을 말할 때 "worked with the design engineers on root cause"를 넣으면 RTL과 만나는 지점이 자연스럽게 드러납니다.
- 역할 범위가 궁금하면 역질문으로 확인하세요: §9의 **2번**(DV팀·PVT팀과 일을 어떻게 나누나, 블록 검증 계획은 누가 갖나)과 **3번**(같은 테스트가 FPGA·emulation·실리콘에서 그대로 도나)이 바로 이 질문입니다. 답을 들으면 RTL을 얼마나 보게 될지도 알 수 있습니다.

---

## 1. 면접관 분석 — Manjiang Zhang (10-28 재확정)

### 1.1 경력 [확인됨 — LinkedIn 경력란]
| 기간 | 회사 · 직함 | 한 일 (원문 요약) |
|---|---|---|
| 2022-12 – 현재 (3년 11개월) | **Rivian** · Sr. Engineer/Manager · Palo Alto on-site | 자율주행 실리콘 개발: architecture reviews, **FPGA/HAPS prototyping**, **silicon bring-up**, **post-silicon validation**, **PVT characterization**, **production-level SLT**. bring-up·검증용 FPGA 기반 플랫폼 개발 |
| 2015-09 – 2022-12 (7년 4개월) | **Baidu USA** · Sr. Hardware Architect and Team Lead | 자율주행 차량용 compute HW 팀 구축·리드: **FPGA sensor data processing**, SW 알고리즘 가속. Baidu·Apollo 파트너 L4 차량에 HW/SW 플랫폼 배포, **road test 이슈 해결** |
| 2012-12 – 2015-09 | Brocade · Sr. Hardware Engineer | FPGA 기반 패킷 처리·데이터 암호화·보안 프로토콜 HW |
| 2010-12 – 2012-12 | Marvell · Sr. Hardware Design Engineer | **Ethernet PHY ASIC** 설계 |
| 2006-12 – 2010-12 | Cisco · Hardware Engineer | 하이엔드 스위치용 **packet processor 칩** 여러 개 설계 |
| 학력 | Shandong University | — |

- 위치: Sunnyvale 거주, 근무는 Palo Alto on-site [확인됨]. Don과 2nd 연결(공통 지인 7명) [확인됨]

### 1.2 공개 특허 (Baidu USA, 공동 발명자) [확인됨 — Justia 특허 목록 [2]]
- **센서 데이터 timestamping** (2018 출원, 2021 등록) · **센서 동기화의 온라인 검증**
- **Sensor processing unit 진단**: 센서 처리 앱을 로드·테스트, **UART 또는 Ethernet** 링크 사용
- 차량 호스트 시스템의 compute node 간 **clock synchronization**(외부 시간원 대비 캘리브레이션) (2022)
- **Secure boot**: 부팅 중 스토리지 파티션의 marker 기반 검증 (2022)
- **다중 카메라 스트림**을 체인 연결된 data-gathering node로 결합 (2025, Apollo/Baidu)

### 1.3 이 사람이 뭘 중요하게 볼까 [추정]
| 신호 | 근거 | Don이 할 것 |
|---|---|---|
| **Bring-up 순서·방법론** | 현재 업무 = bring-up · post-si validation | "첫 실리콘이 오면 첫 주에 뭘 하나"를 순서대로 말할 수 있게 (§5.1 Q1) |
| **FPGA/HAPS ↔ silicon 갭** | FPGA/HAPS 프로토타이핑 + bring-up 플랫폼 직접 개발 | Solidigm FPGA 경험을 "무엇이 FPGA에선 안 보이고 실리콘에서만 보였나"로 말하기 |
| **PVT · shmoo · margin** | PVT characterization | SK hynix shmoo 이야기 = 같은 언어. Vmin/Fmax, corner, temperature inversion |
| **SLT vs ATE** | production SLT | Apple factory test-node, SK hynix chamber 시스템 → SLT 관점으로 재포장 |
| **Ethernet·패킷·카메라·시간동기** | Marvell PHY, Cisco, Baidu 센서·특허 | 우대사항 갭이지만 개념은 정확히 (§5.3, V9/V10/V12) |
| **현장 문제 해결** | "resolved road test issues" | 필드/고객 이슈 루트코즈 스토리 (SK hynix 고객 telemetry, Apple) |
| **SW를 HW 팀의 도구로** | 매니저로서 SW 엔지니어에게 기대하는 것 | 자동화·커버리지·벤치마크 툴을 "검증팀 속도를 올리는 도구"로 말하기 |

- HW 출신이라 **C 코드는 실무형**(레지스터·메모리 테스트·DMA)으로 짧게 물을 가능성 > LeetCode [추정]. 그래도 짧은 코딩 대비는 연습 사이트로.
- **Iris 때 만든 자료 중 Manjiang에게도 쓸 것**: PCIe 링크 디버깅(Q16) ↔ [L2](hmScreenPrep/problems/L2_pcie_cap_walk/index.html) · Zynq(Q23) ↔ §5.5 Q25–Q27 · [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) · 벤치마크(Q9) ↔ Q31 RFC 2544 · 센서 sync(Q24, 그의 특허) ↔ [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html)
- 영어: 비원어민 매니저일 가능성 → **천천히, 구조적으로(First/Second/Third), 숫자로** 말하기가 가장 잘 먹힘 [추정]

---

## 1B. (변경 취소) Iris Lee — 참고용 보관

> 10-28 오전에 면접관이 Iris Lee로 바뀐다고 했다가 같은 날 다시 Manjiang으로 확정됨. 온사이트 4라운드에서 만날 수 있으니 분석은 남겨 둔다.

> LinkedIn은 프로필·경력 페이지 두 장만 **열람**했다 (연결 요청·팔로우·메시지·클릭 없음). LinkedIn이 profile search limit 안내를 띄워 학력 등 나머지는 보지 않았다.

### 1B.1 경력 [확인됨 — LinkedIn 경력란, 2026-10-28 열람]
| 기간 | 회사 · 직함 | 한 일 (원문 요약) |
|---|---|---|
| 2026-08 – 현재 | **Rivian** · **Principal System Engineer** · Palo Alto | (설명 없음) — 매니저에서 시니어 IC로 전환 |
| 2022-12 – 2026-08 (3년 9개월) | **Rivian** · **Senior Manager, System Validation** | (설명 없음) |
| 2019-07 – 2022-12 | **Tensor Auto (당시 AutoX)** · Tech Lead / Manager | 자율주행용 고성능·저지연·고신뢰·**ruggedized compute platform** 아키텍처·설계·개발 리드. EE·System SW·**FPGA**·기구 팀을 묶어 1·2세대 컴퓨트 플랫폼 출시 |
| 2018-04 – 2019-07 | **BYTON** · Staff Software Engineer, Autonomous Driving | HW/SW·**차량 플랫폼 bring-up**, **카메라·LiDAR·radar·GPS/IMU 통합**. Intel·NXP·NVIDIA·AMD Xilinx 기반 L3–L5 HW 플랫폼 평가·검증. 장애물 감지·ACC 시스템/차량 통합·테스트. **Zynq UltraScale+ MPSoC**에서 **bootloader·PetaLinux·Ubuntu** 개발·최적화 |
| 2014-11 – 2018-04 | **Baidu USA** · Staff Software Architect, Autonomous Driving | **HW 플랫폼 bring-up**·시스템 통합·**양산(manufacturing) 지원**. **MPSoC 기반 sensor box** (bootloader, Linux kernel, rootfs, 앱). Intel·Renesas·NXP·NVIDIA 플랫폼으로 Apollo 개발. **PCIe switch fabric** 기반 HPC 클러스터 설계 |
| 2013-09 – 2014-11 | **Huawei** · Sr. Staff Software Engineer | **PCIe fabric** 서버 아키텍처: 여러 호스트가 **SR-IOV**로 PCIe 디바이스 공유. **PCIe fabric failover** 프로토타입 |
| 2002 – 2013 | **Sunrise Telecom** · Sr. Engineer → Software Manager → Sr. Software Manager (VP 직속) | Ethernet·Fibre Channel·광 **테스트 장비** 임베디드 SW: **BER 테스트**, 트래픽 대역폭 제어, **RFC 2544 성능 벤치마크**, 802.3 LLC/SNAP, MPLS. 10GE/40GE/OTN/SONET. 임베디드 SW + **FPGA** 개발 팀 관리 (onshore/offshore) |

- 위치: San Jose 거주 [확인됨]. Don과 2nd 연결(공통 지인 4명) [확인됨]. 최근 게시물 없음.
- Manjiang과 **같은 시기(2022-12) Rivian 합류**, 둘 다 **Baidu USA 자율주행** 출신 [확인됨] → 같은 팀을 함께 꾸린 사이일 가능성 [추정]

### 1B.2 이 사람이 뭘 중요하게 볼까 [추정]
| 신호 | 근거 | Don이 할 것 |
|---|---|---|
| **실제로 돌아가는 C 코드** | Staff/Sr Staff SW 엔지니어·SW 매니저 20년 | 30분짜리 코딩을 깔끔하게: API → 에러 처리 → 테스트 순 (§8, L1–L5) |
| **Linux on MPSoC (Zynq·PetaLinux)** | BYTON·Baidu에서 직접 bootloader·kernel·rootfs·PetaLinux | 부트 흐름·device tree·UIO/드라이버를 정확한 용어로 (§5.5 Q25–Q27, Q38). 실무 없음은 정직하게 |
| **PCIe 깊이** | Huawei PCIe fabric·SR-IOV·failover, Baidu PCIe switch fabric | Don의 최강 카드. enumeration·BAR·AER·LTSSM·Gen6 FLIT까지 (Q28–Q30, [L2](hmScreenPrep/problems/L2_pcie_cap_walk/index.html)) |
| **벤치마크 방법론** | RFC 2544·BER 테스트 장비 10년 | "throughput = zero-loss 최대 rate", 측정 정확도, 신뢰도 계산 (Q31–Q32, [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html), [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html)) |
| **센서 통합·시간 동기** | BYTON 카메라·LiDAR·radar·GPS/IMU | PPS·PTP·HW timestamp (Q33, [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html), [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html)) |
| **시스템 레벨 검증** | Rivian System Validation Sr Manager | IP → SoC → 보드 → 컴퓨트 플랫폼 → 차량으로 이어지는 검증 피라미드 (Q34–Q35, §6.5) |
| **플랫폼 bring-up·양산** | Baidu "bring-up, system integration, manufacturing support" | Apple factory test-node, SK hynix chamber 플랫폼 스토리 (S4, S5) |
| **팀 운영 감각** | SW+FPGA 팀 매니저, onshore/offshore | Staff 시그널: 툴을 만들어 다른 팀이 쓰게 함, 계획을 소유함 |

- **코딩 확률이 Manjiang보다 높다** [추정]. 공유 에디터에서 C로 30분 정도를 가정하고 연습. 언어를 고를 수 있으면 C (JD 필수 = Embedded C).
- Principal(IC)이라 **기술 디테일을 끝까지 파고들 수 있다**. 모르는 건 "I don't know, here's how I'd find out"이 제일 낫다.

### 1B.3 Don ↔ Iris 접점 (스몰토크·연결 고리)
- **PCIe**: 그녀의 Huawei SR-IOV/fabric ↔ Don의 PCIe Gen6 controller + NVMe. "multi-host"나 "failover"가 나오면 NVMe의 multi-path, SR-IOV VF 개념과 연결
- **테스트 장비 SW** ↔ SK hynix 테스트 플랫폼 SDK (S5): "I've built the test platform side too"
- **벤치마크** ↔ Solidigm perf 튜닝 (S7): 측정 → 병목 → 개선 숫자
- **센서 박스 bring-up** ↔ Apple 새 칩 시스템 통합 루트코즈 (S3)

---

## 2. 45분 예상 구성 & 전략

**Manjiang 기준 (10-28 재확정)** [추정]
```
0–5   인사 · 팀 소개 · "Tell me about yourself"          → §3 (90초, 마지막 문장으로 관심 유도)
5–20  레쥬메 딥다이브: Solidigm FPGA/bring-up, shmoo, Apple → §4 스토리 S1–S4
20–35 기술 질문: bring-up 순서, PVT, PCIe/DDR, 디버깅 시나리오 → §5 · §7
35–40 (가능) 짧은 C 문제 or 검증 설계 화이트보드        → §6 · §8
40–45 역질문                                              → §9
```

<details>
<summary>(취소) Iris 기준 구성</summary>

```
0–5   인사 · 팀 소개 · "Tell me about yourself"                → §3 Iris판 (90초)
5–15  레쥬메 딥다이브: PCIe · 테스트 플랫폼 자동화 · 성능 튜닝    → §4 S8 · S5 · S7 · S1
15–35 C 코딩 (공유 에디터, 15–25분) 또는 시스템 설계 화이트보드  → §8 L1–L5 · V2 · V3 / §6.5 · §6.6
35–40 Linux·PCIe·벤치마크·센서 개념 질문                       → §5.5 Q25–Q38
40–45 역질문                                                    → §9 Iris판
```
- **코딩 원칙**: 질문 먼저 (입력 크기·에러 처리·스레드 여부) → 시그니처 → 정상 케이스 → 엣지 → 코드 → 테스트 하나를 손으로 돌리기 → "실제 HW/Linux에서 달라지는 것" 한 마디
- **Linux 질문 원칙**: 실무 범위를 정확히. "I've built kernels and device trees in QEMU labs; I haven't shipped a PetaLinux product" 식으로 경계를 먼저 말하고 개념은 자신 있게

</details>

- **원칙 1 — 그림을 그리며 말하라**: "Let me walk through it in the order I'd actually do it on first silicon." 순서·단계로 답하면 따라오기 쉽다
- **원칙 2 — 내 경험으로 착지**: 모든 개념 답변 끝에 "At Solidigm, we hit exactly this when…" 한 문장
- **원칙 3 — 모르면 정직 + 접근법**: Zynq/MIPI/Ethernet 실무는 없다고 말하고, 어떻게 빨리 익힐지·어떻게 검증할지로 전환
- **원칙 4 — Staff 시그널**: "I owned / I defined the plan / I built the tool other engineers used" 동사 사용. (레벨: ICT3라고 밝혔으므로 HM 앞에서 스코프로 증명)

---

## 3. Tell me about yourself — Manjiang 버전 (영어, 90초)

> I'm Don. I've spent about eight years at the boundary between silicon and software, mostly on the validation side.
> At SK hynix and Solidigm, I started by building the test platform that ran chip-reliability tests across large thermal-chamber systems, and I built shmoo and margin-testing features for the high-speed interfaces between the controller, DRAM and NAND — PCIe Gen3 to Gen5.
> Then, as a Staff firmware engineer on the PCIe Gen6 SSD controller, I did pre-silicon work on FPGA prototypes before RTL freeze: bringing up Cortex-R8, R82 and M0+ cores, the PCIe and NVMe subsystem, DMA, I2C, SPI, and SRAM and DRAM, writing bare-metal C tests, and working with the design engineers on root cause. When silicon came back, I tuned performance together with the silicon validation team.
> Since late 2025 I've been at Apple in wireless chipset integration, where I own root cause when a new chip meets the full system — PCIe, I2C, SPMI and RFFE failures, from the scope and protocol analyzer up — and I designed factory test nodes that catch latent defects before mass production.
> So the path from FPGA prototype to bring-up, to characterization, to production test is the work I know. What excites me here is applying it to an AI accelerator that ends up in a vehicle.

- 마지막 문장 다음 침묵 → 그가 "FPGA" 또는 "shmoo" 중 하나를 집어 물어볼 것. 둘 다 S1·S2로 준비됨

<details>
<summary>(취소) Iris 버전 TMAY</summary>

> ⚠ 사실과 다른 표현이 있으면 고쳐서 쓰세요 (특히 "end to end from the host" 부분).

> I'm Don. For about eight years I've worked where silicon meets the system, on the software side.
> At SK hynix and Solidigm, I started by building the test platform and SDK that ran chip-reliability tests across large thermal-chamber systems — scheduling, temperature control, result collection — plus margin testing for the PCIe Gen3 to Gen5 interfaces.
> Then, as a Staff firmware engineer on a PCIe Gen6 SSD controller, I brought up the multi-core SoC on FPGA prototypes before RTL freeze — PCIe and NVMe, DMA, I2C, SPI, SRAM and DRAM — in bare-metal C, and later tuned performance with the silicon validation team, measuring throughput and latency end to end from the host.
> At Apple, I own root cause when a new wireless chip meets the full system — PCIe, I2C, SPMI and RFFE failures, from the scope and protocol analyzer up to the OS — and I designed factory test nodes that screen latent defects before mass production.
> What draws me to this role is that you validate the whole chain, from the SoC up to a compute platform with real sensors in a vehicle. I'd like to bring the platform and automation side of validation to Rivian's own AI accelerator.

- 마지막 문장 다음 → 그녀가 **PCIe**나 **테스트 플랫폼**, **perf**를 집어 물을 확률이 높음 → S8 · S5 · S7

</details>

---

## 4. 경험 딥다이브 — 스토리 뱅크 (STAR, 영어 핵심 문장)

**Manjiang 기준 우선순위 (10-28 재확정)**: **S1 FPGA bring-up** → **S2 shmoo** → **S3 Apple 루트코즈** → **S4 factory test-node** → S7 perf → S8 PCIe(Q16 PCIe 질문이 나오면) → S5 자동화

> ⚠ `[ ]` 칸은 Don이 실제 숫자로 채울 것. 숫자 없는 스토리는 약하다. 기밀은 "a PCIe controller block", "a wireless chipset"처럼 일반화.

### S1 · Pre-silicon FPGA SoC 검증에서 RTL 버그를 잡은 이야기 (Solidigm) — 최우선
- **S**: PCIe Gen6 SSD controller, RTL freeze 전, FPGA 프로토타입 이미지 [ ]개 블록
- **T**: 내가 맡은 블록 [ ](예: DMA / I2C / PCIe controller / DRAM controller)의 bring-up과 기능 검증
- **A**: bare-metal C 테스트를 블록별로 작성 → register sanity → 기본 데이터 경로 → stress(동시성·경계) 순. 실패 시 FPGA 내부 신호(ILA/디버그 레지스터) + Trace32로 좁혀서 디자인 엔지니어와 재현 케이스 공유
- **R**: RTL freeze 전 버그 [ ]건 발견, 그 중 [ ]건은 실리콘에서 찾았으면 respin급. 일정 [ ]주 단축
- **영어 핵심**: "Because we had the firmware tests running on the FPGA image before RTL freeze, we found [N] RTL issues while they were still cheap to fix — one of them was [short description], which would have been a respin on silicon."
- **꼬리질문 대비**: FPGA에선 클럭이 [ ]MHz로 느려서 못 본 문제는? → 타이밍·아날로그 PHY·전원·실제 메모리 latency 비율이 다르다 (§5.1 Q2)

### S2 · Shmoo / 고속 인터페이스 margin 시스템 (SK hynix)
- **S**: V6/V7 NAND, PCIe 3/4/5 고속 인터페이스 reliability 커버리지 부족
- **T**: SoC/SI 팀과 마진 측정·health monitoring 디버그 펌웨어 기능 구축
- **A**: shmoo 축 정의(전압 [ ] / 타이밍 delay·Vref [ ] / 온도 chamber [ ]), 각 포인트 반복 [ ]회로 marginal 판정, 자동 sweep + 결과 맵 생성
- **R**: 마진 부족 포인트 [ ] 발견 → 설정 [ ] 변경 / 테스트 시간 [ ] → [ ]
- **영어 핵심**: "We swept voltage and timing at three temperature corners and repeated each point to separate a hard wall from a marginal region. That's how we found [issue], which only showed up hot and at low voltage."
- 연결: 그의 **PVT characterization** 업무와 같은 언어 → [V7 shmoo 문제](hmScreenPrep/problems/V7_shmoo_margin/index.html)

### S3 · 새 칩 통합 인터페이스 루트코즈 (Apple)
- **S**: 새 wireless 칩이 full system에 들어갈 때 [PCIe 링크 / I2C / SPMI / RFFE] 실패
- **A**: 증상 분류(항상/간헐, 보드별/온도별) → 프로토콜 애널라이저·DSO로 신호/프로토콜 레벨 확인 → SW 타이밍 vs HW 마진 분리
- **R**: 원인 [ ] → 수정 [ ] → 재발 방지로 factory test에 케이스 추가
- **영어 핵심**: "The first question I always ask is whether it's a protocol problem or a signal problem — so I put the analyzer and the scope on the bus before touching the software."

### S4 · Factory test-node / 스트레스 기반 latent defect (Apple) = SLT 언어
- **영어 핵심**: "I designed test nodes around stress scenarios — temperature, power transitions, concurrent traffic — so that latent silicon or integration defects show up before mass production instead of in the field."
- 그의 **production SLT** 업무와 연결: "SLT catches what structural ATE tests miss, because it runs the real workload in the real system."

### S5 · 테스트 플랫폼 SDK + 자동화 (SK hynix) = "automatic testing tools"
- **R 숫자**: chamber [ ]대, 동시 DUT [ ]개, 수동 대비 [ ]배, 사용자 엔지니어 [ ]명
- **영어 핵심**: "I turned a manual, per-engineer process into a shared platform — scheduling, temperature control, UART sequencing and result collection — that other teams adopted."

### S6 · 고객 telemetry 디버깅 기능 (SK hynix) = 필드 이슈 해결
- 그의 "resolved road test issues"와 같은 결: 현장에서 재현 안 되는 문제를 데이터로 해결
- **영어 핵심**: "When you can't reproduce a field failure, you need the device to tell you what happened. I built the telemetry path so we could debug customer failures from logs instead of guessing."

### S7 · Perf 튜닝 with silicon validation team (Solidigm) = benchmark
- **영어 핵심**: "We measured throughput and latency at each stage of the data path, found the bottleneck was [ ], and got [ ]% back."
- **Iris 꼬리질문 대비**: 어떻게 측정했나(어느 지점에 timestamp? 측정 오버헤드?) · 반복성(몇 번, p50/p99) · "throughput"의 정의(손실 없는 최대 rate? QD는?) → RFC 2544 사고방식으로 답 (Q31)

### S8 · PCIe 이슈 루트코즈 (Solidigm Gen6 controller 또는 Apple) — Q16 대비 (10-28 추가)
- **S**: [ ] (예: Gen[ ] x[ ] 링크가 특정 호스트/조건에서 downtrain · enumeration 실패 · AER 에러 증가 · warm reset 후 디바이스 사라짐)
- **T**: 내 역할 [ ] — controller FW 쪽 / 시스템 통합 쪽 루트코즈 owner
- **A**: LTSSM 상태·링크 상태 레지스터 확인 → 프로토콜 애널라이저로 TS1/TS2·equalization 단계 확인 → AER/에러 카운터 → [원인 좁히기: 보드 SI / refclk / PERST# 타이밍 / FW 초기화 순서 / 호스트 BIOS 설정]
- **R**: [ ] (재현율 [ ]% → 0, 영향 범위 [ ], 회귀 테스트 추가)
- **영어 핵심**: "The link trained at Gen[ ] but fell back to Gen[ ] on [condition]. The analyzer showed [ ] during equalization, which pointed us to [ ]. After [fix], the link held at full speed across [N] boots, and I added that as a regression check."
- **꼬리질문 대비**: config space에서 뭘 보나 → Link Cap vs Link Status, AER, Device Status ([L2](hmScreenPrep/problems/L2_pcie_cap_walk/index.html)) · Gen6 차이 → PAM4, FLIT mode, FEC (Q30)

---

## 5. 개념 Q&A (예상 질문 → 영어 모범 답)

> 사용법: 질문만 보고 30초 영어로 답한 뒤 펼쳐서 비교.

### 5.1 Validation 방법론 (가장 확률 높음)

<details>
<summary>Q1. First silicon arrives on your desk tomorrow. What do you do in the first week?</summary>

1. **Power & clocks first**: check rails and power sequencing, reset release, reference clocks — on the scope, before any software.
2. **Debug access**: connect JTAG, halt the boot core, read the chip ID / version register to prove the debug path and basic bus fabric work.
3. **Minimal boot**: run a tiny bare-metal image from on-chip SRAM — UART "hello" so I have a console.
4. **Register sanity** of each block: reset values, RW bits (V2 style) — fast, catches integration and address map issues.
5. **Memory**: on-chip SRAM test, then DDR init + training, then memory tests (walking ones, March C-) at low frequency first.
6. **Peripherals and high-speed links** in dependency order: I2C/SPI/UART → PCIe/Ethernet link-up → camera input.
7. **Compute blocks** (NPU) with the golden-model tests already proven on FPGA.
8. Every result goes into the tracker with chip ID, board, temperature, voltage — so later PVT data is comparable.

"The key is that all of these tests already ran on the FPGA, so on silicon I'm only debugging the difference."
</details>

<details>
<summary>Q2. What problems do you catch on FPGA/HAPS prototypes, and what can only show up on silicon?</summary>

- **FPGA catches**: functional RTL bugs, register map / integration errors, firmware–hardware interface bugs, driver sequencing, most DMA and interrupt logic, and it lets software start months early.
- **FPGA misses**: real timing (it runs maybe 10–100× slower, so CPU-to-memory latency ratios differ), analog/mixed-signal blocks (PHYs, PLLs, DDR PHY training, SerDes), power and thermal behavior, clock domain crossing issues that only appear at real frequency, and memory macros replaced by FPGA RAM.
- "So on FPGA I validate function; on silicon I validate timing, analog interfaces, power and margin — PVT."
</details>

<details>
<summary>Q3. Pre-silicon options — simulation vs emulation vs FPGA prototyping. When do you use which?</summary>

- **RTL simulation (UVM)**: full visibility, very slow (Hz–kHz) — DV team, block level.
- **Emulation (Palladium / ZeBu / Veloce)**: MHz range, good visibility, expensive, used for full-chip and boot/firmware runs.
- **FPGA prototyping (HAPS, Zynq/VCU boards)**: tens of MHz, less visibility, cheapest per seat — best for firmware and driver development and long-running tests.
- "As the software validation engineer, I'd write tests once with a thin HAL so the same test runs on emulation, FPGA and silicon."
</details>

<details>
<summary>Q4. What is PVT characterization and how would software help?</summary>

- Run the chip across **Process corners** (SS/TT/FF parts), **Voltage** (sweep around nominal) and **Temperature** (e.g. −40 °C to 125 °C for automotive) and measure pass/fail and performance — Vmin, Fmax, margin, power.
- Software role: an automated sweep framework — Python controls the power supply, thermal forcer and on-chip PLL/PMIC registers; the device runs a defined test set at each point and reports results; the host builds shmoo plots and margin tables.
- Notable: at advanced nodes like 5 nm, **cold can be the slow corner at low voltage** (temperature inversion), so don't assume hot is worst.
- "At SK hynix I built the shmoo features for exactly this — voltage and timing sweeps at multiple temperatures, repeating each point to find marginal regions."
</details>

<details>
<summary>Q5. ATE vs SLT — what's the difference and why do you need both?</summary>

- **ATE**: structural tests on a tester — scan/ATPG, MBIST, IDDQ, parametric — fast, high fault coverage for manufacturing defects, but not running real software.
- **SLT (system-level test)**: the chip boots on a real board and runs real workloads (boot, DDR traffic, PCIe, NPU inference) — catches defects that only appear under real system interaction, timing paths not covered by scan, power-delivery issues.
- Trade-off: SLT costs test time per part → choose a short, high-yield workload set; post-silicon validation tests are the source for SLT content.
- "My factory test-node work at Apple is the same idea — stress scenarios that expose latent defects before shipment."
</details>

<details>
<summary>Q6. How do you write a validation plan for a new IP block?</summary>

1. Read the spec → list **features** and **registers** → each becomes a test item with an ID and requirement link.
2. Categorize: register sanity, functional (normal paths), error/corner (illegal programming, overflow, reset in the middle), performance, power states, interaction with other blocks (concurrency, interrupts, DMA).
3. Prioritize: must-pass for bring-up vs full coverage vs stress.
4. Define **pass criteria** and what platform each runs on (FPGA / silicon / both).
5. Track coverage (requirements with a passing test / total) and review it with design and DV — avoid duplicating what DV already covers in simulation; focus on what needs real silicon.
</details>

<details>
<summary>Q7. How do you know your test itself is correct?</summary>

- Run it against a **known-good reference** (golden model, previous chip, FPGA).
- **Negative testing**: inject a fault (wrong value, disabled clock, corrupted CRC) and confirm the test fails — a test that never fails proves nothing.
- Review with the designer, check that each check actually reads hardware (not cached values), and keep tests deterministic with seeds.
</details>

<details>
<summary>Q8. A test fails 1 in 1000 runs on silicon. How do you approach it?</summary>

1. Make it **more frequent**: loop it, add stress (temperature, voltage down, concurrent traffic).
2. **Correlate**: which chips, boards, temperatures, frequencies? One chip → silicon defect/margin; all chips → design or software.
3. Capture **state at failure**: trigger the logic analyzer / scope on a GPIO toggled by the test when it detects the error; dump registers and trace buffers.
4. Bisect: disable features, change one variable at a time.
5. Separate software race vs hardware margin: does it depend on voltage/frequency (hardware) or on timing of software events (race)?
</details>

<details>
<summary>Q9. How do you build a benchmark tool for throughput, latency and utilization? (JD)</summary>

- Use a **hardware cycle counter** with known frequency; measure many iterations after warm-up; subtract measurement overhead; report min / mean / p50 / p99 / max, not just average.
- Throughput = bytes / time; utilization = achieved / theoretical peak (e.g. MAC utilization = achieved ops / (MACs × 2 × freq)).
- Sweep parameters — transfer size, number of channels, IO and compute frequency — and plot curves to find where it becomes memory-bound vs compute-bound (roofline).
- Control noise: fixed clocks, interrupts off, caches in known state. → [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html)
</details>

<details>
<summary>Q10. What does "test coverage report" mean for silicon validation?</summary>

- **Functional coverage**: features / registers / requirements exercised and passing, per block, per platform.
- **Code coverage** of the test firmware is secondary.
- Report = requirement → tests → latest status → chip/board/conditions, so anyone can trace a requirement to evidence (traceability). → [V5](hmScreenPrep/problems/V5_test_runner_coverage/index.html)
</details>

### 5.2 Embedded C · SoC 기초

<details>
<summary>Q11. Why volatile? Is it enough for MMIO?</summary>

volatile forces every access to actually happen and in program order relative to other volatile accesses — needed for MMIO and variables changed by ISRs/HW. It is **not** a memory barrier: the CPU or interconnect can still reorder or buffer writes. For device ordering use barriers (DMB/DSB on Arm, `dma_wmb` / `wmb`) and read-back to flush posted writes when timing matters.
</details>

<details>
<summary>Q12. Cache and DMA coherency — what do you do?</summary>

Non-coherent DMA: before the device reads a buffer, **clean** (write back) the CPU cache; before the CPU reads a buffer the device wrote, **invalidate**. Align buffers to cache lines so an unrelated variable doesn't share a line. With a coherent interconnect (ACE/CHI, IO coherency) it's handled in hardware, but you still need ordering barriers. → [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html), [D3](hmScreenPrep/problems/D_debug_drills/index.html)
</details>

<details>
<summary>Q13. Write-1-to-clear status bits — what's the trap?</summary>

Doing read-modify-write (`reg |= BIT`) on a W1C register writes back 1s for every pending bit and clears them all. Write only the bit you mean: `reg = BIT`. → [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html)
</details>

<details>
<summary>Q14. What happens at boot from reset to main()?</summary>

Reset vector → boot ROM (configures minimal clocks, reads boot straps, loads next stage from flash/eMMC/UART, verifies signature for secure boot) → first-stage bootloader (sets up clocks/PLLs, DDR init and training, loads next image) → set up stack, copy .data, zero .bss, set up MMU/caches and vector table → main(). On a multi-core SoC, secondary cores are held in reset or a spin loop until released.
</details>

<details>
<summary>Q15. Interrupt validation on a new SoC — what do you check?</summary>

Each interrupt source routes to the right GIC line (SPI number) and core; edge vs level type; priority and masking; clear-at-source works; no spurious or lost interrupts under high rate; latency from event to ISR measured with a GPIO toggle on the scope.
</details>

### 5.3 인터페이스 (그가 HW 출신이라 깊이 물을 수 있음)

<details>
<summary>Q16. PCIe link doesn't come up on first silicon. Walk me through debug.</summary>

1. Basics: reference clock present and in spec, PERST# timing, power rails, lane polarity and lane reversal settings.
2. Read the **LTSSM state** from the controller debug register — where is it stuck? Detect (receiver detection — electrical/termination), Polling (bit/symbol lock, training sets), Configuration (lane/link width negotiation), Recovery loop (signal integrity / equalization issues at Gen3+).
3. Force a lower speed (Gen1) and lower width (x1) — if it trains, it's signal integrity / equalization.
4. Use a protocol analyzer or the PHY's eye-monitor / margining features; check equalization presets.
5. Once in L0: config space enumeration, BAR assignment, MSI/MSI-X, DMA traffic and AER error counters.
"I worked on PCIe from Gen3 through Gen6 targets at SK hynix and Solidigm, and on PCIe integration failures at Apple."
</details>

<details>
<summary>Q17. DDR bring-up — what is training and what can go wrong?</summary>

Training aligns timing between the controller/PHY and the DRAM: ZQ calibration (impedance), command/address training (LPDDR), **write leveling** (DQS vs CK at each DRAM), **read gate training**, **read/write eye centering** (per-bit DQ deskew, DQS position) and **Vref training**. Failures show up as training errors, or as passing training but data errors under stress or temperature. After training: memory tests (walking ones on data and address lines, March C-, random patterns with DMA at full bandwidth), and margin checks at PVT corners. → [V1](hmScreenPrep/problems/V1_march_memtest/index.html)
</details>

<details>
<summary>Q18. Ethernet — MAC vs PHY, and how would you validate it? (his Marvell PHY background)</summary>

- **MAC**: framing, FCS, addressing, flow control, DMA to memory. **PHY**: PCS (encoding — 8b/10b, 64b/66b), PMA/PMD (SerDes, analog line). Interface between them: MII/RGMII/SGMII/USXGMII; management through **MDIO/MDC** (Clause 22 / 45 registers).
- Validation: register access over MDIO, autonegotiation / link status, then loopbacks from the inside out — MAC internal → PCS → PHY near-end → remote/far-end → external cable — so a failure is localized to a stage. Traffic with sequence numbers + FCS check, counters (CRC errors, runts, drops), line-rate throughput (64-byte frames: 14.88 Mpps at 10 G), PTP timestamping if supported.
- Automotive: 100BASE-T1 / 1000BASE-T1 single pair.
- Honest: "I haven't validated an Ethernet MAC myself; my high-speed experience is PCIe, and the methodology — loopback stages, PRBS, error counters — is the same." → [V10](hmScreenPrep/problems/V10_eth_frame_fcs/index.html), [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html)
</details>

<details>
<summary>Q19. MIPI CSI-2 camera input — how does it work and how would you validate a receiver?</summary>

- Sensor sends over D-PHY (clock lane + 1–4 data lanes, HS for data / LP for control) or C-PHY. Packets: short (frame start/end, line start/end) and long (pixel data with a data type like RAW10/RAW12/YUV, word count, ECC on header, CRC on payload). Virtual channels let several streams share a link.
- Validation without a camera: use the receiver's or an FPGA's **test pattern generator**, check frame/line counts, ECC/CRC error counters, word count per line, dropped frames, timestamps at frame start, then the ISP output against a reference image.
- Bandwidth sanity: 1920×1080 × 30 fps × 12 bits ≈ 0.75 Gbps + blanking overhead → fits 2 lanes at 1 Gbps/lane.
- Note: RFFE and SPMI, which Don debugged at Apple, are also MIPI Alliance specs. → [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html)
</details>

<details>
<summary>Q20. I2C bus is stuck low. What happened and how do you recover?</summary>

Usually a slave was in the middle of sending a 0 bit when the master reset. Recovery: toggle SCL up to 9 times until the slave releases SDA, then send STOP. Prevent: reset slaves with the master, add timeouts. Debug: logic analyzer on SCL/SDA, check pull-up rise time. → [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html)
</details>

<details>
<summary>Q21. JTAG — what do you use it for in bring-up?</summary>

Boundary scan for board connectivity; debug access (halt cores, read/write memory and registers via the Arm debug port / AP) before any software works; load images into SRAM; read trace. Trace32 (Lauterbach) is what Don used daily — scripts to automate register dumps at failure.
</details>

### 5.4 AI accelerator · Zynq · 시간 동기

<details>
<summary>Q22. How would you validate an NPU / AI accelerator block?</summary>

1. **Golden model**: bit-exact reference (from the compiler team or a C model) for each operator — GEMM/conv, activation, pooling, requantization.
2. **Stimuli**: directed corner values (−128, 127, zero-point, saturation), random with seeds, and real network layers.
3. **Structure-aware tests**: tile sizes and partial tiles, every SRAM bank, DMA patterns in and out, concurrent jobs, back-to-back descriptors.
4. **Compare** bit-exact; when it fails, characterize the pattern of mismatches (only last tile? only one channel? off by one = rounding?) — that points to the hardware unit.
5. **Performance**: achieved vs theoretical TOPS, utilization, memory bandwidth bound or compute bound; and **power** with "power virus" workloads for worst-case current. → [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html)
</details>

<details>
<summary>Q23. What do you know about Zynq / Petalinux? (JD preferred — Don's gap)</summary>

Zynq = Arm processing system (PS: Cortex-A9/A53 + R5 on MPSoC) + FPGA fabric (PL) connected by AXI. Petalinux = AMD's Yocto-based tools to build the Linux image (FSBL, U-Boot, kernel, device tree, rootfs) for that board. In a validation setup the PL often hosts a model or a traffic generator and the PS runs Linux test software that accesses PL registers via a driver, UIO or /dev/mem and moves data with AXI DMA.
"I haven't used Petalinux itself; I've done the same FPGA-based bring-up on a different flow, and I've been studying embedded Linux — U-Boot, device tree, drivers — so ramping on Petalinux is a matter of weeks."
</details>

<details>
<summary>Q24. Sensor time synchronization — how would you check cameras are in sync? (his patents)</summary>

Timestamp at the hardware frame-start event (SOF at the CSI-2 receiver), all on one PTP-disciplined clock (gPTP / IEEE 802.1AS on automotive Ethernet). Check per-stream frame-counter gaps, period jitter, and inter-camera skew per frame against a limit; validate end-to-end with a physical reference (LED strobe visible to all cameras, scope on trigger lines). Read 64-bit timestamps carefully (latched hi/lo) to avoid tearing. → [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html)
</details>

### 5.5 Linux · Zynq · PCIe · 벤치마크 · 센서 · 시스템 검증 (10-28 Iris용으로 추가)

> Manjiang 재확정 후: 우선순위는 낮지만 JD 우대사항과 겹친다. **Q25(Zynq 부트) · Q28–Q30(PCIe) · Q31(RFC 2544) · Q32(BER)**만 훑어 두기.

<details>
<summary>Q25. Walk me through the boot flow of a Zynq UltraScale+ MPSoC running PetaLinux.</summary>

1. **BootROM** (runs on the PMU/CSU) reads the boot-mode pins and loads the **FSBL** from QSPI / SD / eMMC into on-chip memory (OCM).
2. **FSBL** initializes PS clocks, MIO and **DDR** (the `psu_init` generated from the hardware design / XSA), loads the **PMU firmware**, optionally programs the **PL bitstream**, then loads **ATF (BL31)** at EL3 and **U-Boot** at EL2.
3. **U-Boot** loads the kernel `Image`, the **device tree** and the rootfs (or a FIT image) and boots **Linux**.
4. All the boot pieces are packed into `BOOT.BIN` by `bootgen` from a `.bif` file (`petalinux-package --boot`).
- Debug: if nothing prints after FSBL, check PMU FW and ATF are in `BOOT.BIN`, the DDR config in the XSA, the UART pin mux and the boot-mode pins. Use **JTAG boot mode** to load each stage by hand and isolate which one fails.
- 정직 포인트: "I haven't shipped a PetaLinux product. I've worked through the same stages — U-Boot, device tree, kernel, rootfs — in QEMU labs, and on the SSD side I owned the bare-metal boot of a multi-core SoC."
</details>

<details>
<summary>Q26. How do you add a custom PL IP block and use it from Linux with PetaLinux?</summary>

`petalinux-create -t project --template zynqMP` → `petalinux-config --get-hw-description <design.xsa>` (imports the address map and generates the base device tree) → add or override the IP's node in `project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi` (compatible string, `reg`, `interrupts`) → `petalinux-config -c kernel` for the driver options → `petalinux-build` → `petalinux-package --boot --fsbl --fpga --u-boot --pmufw`. Underneath it is **Yocto** (bitbake recipes in `meta-user`). For a quick validation tool, bind the node to **generic-uio**; for production, write a platform driver.
</details>

<details>
<summary>Q27. Userspace register access: /dev/mem vs UIO vs a kernel driver?</summary>

- **/dev/mem + mmap**: fastest to start. Needs root and `CONFIG_STRICT_DEVMEM` relaxed. No interrupts. Open with `O_SYNC` for an uncached mapping. Easy to crash the system with a wrong address.
- **UIO** (`generic-uio` in the device tree): maps only that device's region, and gives **interrupts** through `read()`/`poll()` on `/dev/uioN` (write 1 to re-enable). Good for validation tools.
- **Kernel driver**: needed for DMA (DMA API, IOMMU), sharing between processes, power management, and anything that must be robust.
- Pitfall: if the PL clock is off or the bitstream isn't loaded, an access can **hang the AXI bus or raise SError**. Check the FPGA manager state and clocks first. → [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html)
</details>

<details>
<summary>Q28. How does PCIe enumeration work?</summary>

A depth-first walk from the root port:
1. Read the Vendor ID at each bus/device/function (0xFFFF means no device).
2. For a bridge (Type 1 header), assign primary/secondary/subordinate bus numbers and recurse.
3. Size every BAR: write all 1s, read back, size = ~(value & mask) + 1, and restore.
4. Assign memory/IO windows and enable Memory Space and Bus Master in the Command register.

Config space is reached through **ECAM**: address = base + (bus << 20 | dev << 15 | fn << 12 | offset), with 4 KiB per function. Then walk the capability lists: standard from 0x34, extended from 0x100. → [L2](hmScreenPrep/problems/L2_pcie_cap_walk/index.html)
</details>

<details>
<summary>Q29. PCIe switch fabric, SR-IOV, multi-host — explain (her Huawei/Baidu work)</summary>

- **Switch**: one upstream port plus several downstream ports. Inside, each port is a virtual PCI-to-PCI bridge, and traffic is routed by address or ID.
- **SR-IOV**: one Physical Function (PF) exposes many Virtual Functions (VFs). Each VF has its own routing ID (`PF RID + First VF Offset + n × VF Stride`) and its own BARs, so you can pass it straight to a VM (VFIO) with no hypervisor in the data path.
- **Multi-host**: usually **NTB** (non-transparent bridge — address translation between two hosts' domains) or a partitioned switch. Failover uses hot-plug, **DPC** (Downstream Port Containment) and AER recovery.
- Don's link: "On the NVMe side we had the same ideas — multiple queues and functions, and namespaces shared across hosts. I'd validate SR-IOV by checking VF config space, BARs, isolation (one VF can't reach another's memory) and per-VF throughput under load."
</details>

<details>
<summary>Q30. PCIe errors and link health — what do you check, and what's new in Gen6?</summary>

- **Errors**: correctable errors (bad TLP/DLLP, replay timer → retried by the link layer — they hurt performance but not data), non-fatal, and fatal. Read them from **AER** (Uncorrectable/Correctable Error Status) and the root port's error messages. **Surprise down** = the link was lost.
- **Link health**: Link Capabilities vs Link Status (did it **downtrain** in speed or width?), LTSSM recovery count, Gen4+ **lane margining** at the receiver (time/voltage margin per lane — the PCIe version of a shmoo).
- **Gen6**: **PAM4** signalling at 64 GT/s, **FLIT mode** (fixed 256-byte flits), lightweight **FEC** + CRC — so a higher raw bit error rate is expected and corrected, and you watch FEC-corrected counts as an early warning.
</details>

<details>
<summary>Q31. What is RFC 2544 and how would you apply it to an on-chip data path? (her Sunrise Telecom work)</summary>

RFC 2544 defines four benchmark tests for a network device:
1. **Throughput**: the highest rate with **zero frame loss**, found by binary search with fixed-length trials (≥ 60 s each).
2. **Latency**: measured at the throughput rate, using timestamped frames.
3. **Frame loss rate**: loss % from 100 % of line rate, stepping down by 10 %.
4. **Back-to-back**: the longest burst at line rate with no loss.

All four are run at standard frame sizes, 64 to 1518 bytes. Line rate counts the 20 bytes of preamble + IFG: 10GbE at 64 B = 14.88 Mfps.
On the SoC: "throughput" of a DMA, PCIe or camera-to-DDR path = the highest sustained rate with no FIFO overflow or drop, swept over transfer size. Latency comes from hardware timestamps at both ends. Report pps and bps, because small transfers are limited per transaction, not per byte. → [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html)
</details>

<details>
<summary>Q32. How long must you run a BER test to claim BER < 1e-12?</summary>

With zero errors observed, the confidence level CL needs N ≥ −ln(1 − CL) / BER bits. At 95 % that is ≈ **3 / BER = 3 × 10¹² bits**, which is about **300 s at 10 Gb/s** (≈ 46 s at 64 Gb/s per lane). If you allow errors, use the Poisson / chi-square form. Use PRBS31 for stress and PRBS7 for quick checks, and the checker must re-sync after a slip. → [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html)
</details>

<details>
<summary>Q33. How are AV sensors connected and synchronized? (her BYTON work)</summary>

- **Cameras**: GMSL2 / FPD-Link III SerDes over coax → deserializer → **MIPI CSI-2** into the SoC.
- **LiDAR**: usually Ethernet (UDP point packets) with PTP and/or PPS timestamps.
- **Radar**: CAN-FD or automotive Ethernet.
- **GNSS/IMU**: UART/SPI plus a **PPS** line.
- **Time base**: GPS **PPS + NMEA** (which second it is) disciplines the platform clock, and **gPTP (802.1AS)** distributes it over Ethernet. Cameras are often hardware-triggered (FSYNC), aligned to the LiDAR's rotation phase.
- **What to validate**: hardware-timestamp points, offset to GPS, cross-sensor skew, missed-PPS / holdover behaviour, and drift over temperature. → [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html) · [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html)
</details>

<details>
<summary>Q34. How does silicon validation connect to system validation? (her Rivian role)</summary>

It's a pyramid: **IP → SoC (silicon validation) → board → compute module / ECU → vehicle (system validation)**. Each level has entry and exit criteria.
- Silicon validation hands up **errata, margins and known limits**.
- System validation hands down failures that need chip-level root cause.
- Keep one **traceability chain**: requirement → test → result → tracker (the JD asks for this).
- Reuse test content upward: the same SoC-level stress and benchmark tools run on the full platform in HIL rigs and long soak tests.
- "My job would be to make the SoC tests reusable at the platform level, and to give system validation a fast way to tell 'chip problem' from 'board or software problem'."
</details>

<details>
<summary>Q35. What's different about validating a ruggedized automotive compute platform?</summary>

- **Environment**: temperature (−40 °C up to 85/105 °C), power (cold crank, load dump — ISO 16750), vibration and shock, EMC (CISPR 25).
- **Behaviour**: thermal throttling has to stay within the latency budget, plus watchdog / safety-MCU behaviour and functional safety (ISO 26262 safety mechanisms: ECC, lockstep, BIST).
- **Process**: long soak runs and a DVP&R test plan.
- Don's link: SK hynix chamber platform (temperature-controlled automated testing) and Apple factory test-node design.
</details>

<details>
<summary>Q36. You benchmark something on Linux and results vary run to run. What do you control?</summary>

- **CPU**: `performance` governor and fixed frequency, pin the thread (`taskset`), isolate a core (`isolcpus` / `nohz_full`), move IRQ affinity off it.
- **Memory**: `mlockall()` and pre-fault buffers (no page faults during the run), huge pages for large buffers.
- **Timing**: use `CLOCK_MONOTONIC_RAW`, run warm-up iterations, report p50/p99/max over N runs.
- **Thermal**: watch for throttling and record the temperature.
- **Tools**: `perf stat` (cycles, cache misses), `ftrace` / `trace-cmd` for latency, `cyclictest` for scheduling jitter (PREEMPT_RT). → [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html)
</details>

<details>
<summary>Q37. Lock-free single-producer single-consumer ring — why does memory ordering matter?</summary>

The producer must write the record **before** it publishes the new head (store-release), and the consumer must read head with load-acquire **before** it reads the record. On Arm (weakly ordered) a relaxed store can become visible before the data, so the consumer reads stale data. x86 hides this because it's TSO. `volatile` does not order memory between cores. Use C11 `atomic_store_explicit(..., memory_order_release)` / `memory_order_acquire`, or barriers (`dmb ish`). Put head and tail on separate cache lines to avoid false sharing. If the producer is a DMA engine, you also need cache maintenance or coherent memory. → [L5](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html)
</details>

<details>
<summary>Q38. When would you write a kernel driver for a validation block, and what does it look like?</summary>

When you need **DMA, interrupts at high rate, sharing between processes, or robustness**. A minimal platform driver:
- `of_match_table` matches the device-tree `compatible`.
- `probe()` → `devm_ioremap_resource()` → `devm_request_threaded_irq()`.
- DMA buffers come from `dma_alloc_coherent()`, or streaming `dma_map_single()` plus `dma_sync_*` (cache maintenance).
- Expose it to the test tool through a char device / `ioctl`, or through sysfs / debugfs for counters.
Teardown is automatic with `devm_*`. For quick register poking during validation, UIO is enough (Q27).
</details>

---

## 6. 검증 설계 문제 (화이트보드형)

### 6.1 "Design the validation infrastructure for our next SoC" — 답 구조
```
[Test content]  per-block tests in C (HAL: FPGA / emulator / silicon)  ← requirement IDs
      │
[Target runtime] bare-metal test runner on the SoC: registry, timeouts, result lines over UART
      │
[Host orchestration] Python: power/reset control, JTAG load (Trace32 scripts), UART capture,
                     PSU + thermal forcer for PVT, retries, board farm scheduling
      │
[Results DB] test, chip ID, board, V/F/T, firmware hash, pass/fail, logs
      │
[Reports] coverage per block & requirement, trend, shmoo plots, failure triage queue → tracker
```
- 핵심 말: "Write the test once, run it on every platform." · "Every result carries chip ID and conditions so validation data can feed PVT and SLT."
- 확장: nightly regression, flaky 정책(재시도 후 flaky 태그), 테스트별 런타임 예산

### 6.2 "Design a test plan for a DMA engine"
- 레지스터 sanity → single transfer (sizes 1B..max, 정렬/비정렬) → descriptor chain / scatter-gather → ring wrap → 동시 채널 → 에러(잘못된 주소, 정렬 위반, abort) → 인터럽트/코얼레싱 → bandwidth/latency 측정 → PVT 하에서 데이터 무결성 → [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html)

### 6.3 "How would you measure and report NPU performance?"
- 마이크로벤치(단일 GEMM 크기 sweep) + 실제 레이어 + 엔드투엔드 네트워크; 지표 = latency(p50/p99), throughput(inferences/s), MAC utilization, DDR bandwidth, power(W) → TOPS/W; 주파수 sweep으로 compute/IO 바운드 구분

### 6.4 "Same test passes on FPGA, fails on silicon"
- 분류: ① 타이밍/주파수 의존(FPGA 클럭이 느림) ② 아날로그/PHY ③ 메모리 모델 차이 ④ 리셋/전원 시퀀스 ⑤ 테스트가 FPGA 특성(초기값, latency)에 우연히 의존
- 행동: 실리콘에서 주파수 낮춰 보기, 전압 올려 보기(margin?), 여러 칩 비교, 레지스터 덤프 diff(FPGA vs silicon)

### 6.5 (Iris) "Design an automated regression system for 50 boards — FPGA platforms and silicon"
```
[Board farm]   per board: network PDU (power cycle), UART console, JTAG, Ethernet; boot via TFTP/NFS or SD mux
[Scheduler]    Jenkins or LAVA: job = (build ID, test set, board type); board health check before each job
[Target side]  bare-metal test runner or Linux test binaries → structured RESULT lines (V5 format)
[Host side]    Python: flash, boot, run, collect logs, timeouts → auto power-cycle on hang
[Results DB]   test, board/chip ID, build hash, conditions (V/F/T), pass/fail, duration, logs
[Triage]       flaky detection (rerun policy), auto-bisect on new failures, dedup by signature → tracker ticket
[Reports]      coverage per requirement/block, trend per build, dashboard for system validation
```
- 핵심 말: "A board that's broken must never look like a test failure — every job starts with a board health check." · "Every result carries the build hash and the board ID, so any failure is reproducible."

### 6.6 (Iris) "How would you measure sensor-to-inference latency on the platform?"
- **Stage timestamps** on one time base (PTP/PPS-disciplined): exposure start or FSYNC → CSI-2 frame start at the receiver → ISP done → frame in DDR → NPU job start → NPU done → result published
- Hardware timestamps where possible (capture units, NPU completion interrupt timestamp), software timestamps only with the measured ISR latency noted
- Run at the real sensor rate **under load** (all cameras + LiDAR + other NPU jobs). Report p50/p99/max per stage, and the budget vs measured per stage
- Validate the measurement itself: an LED strobe in front of the camera and a scope on the FSYNC and GPIO-at-result lines gives a ground-truth end-to-end number

---

## 7. 디버깅 시나리오 (말로 푸는 문제)

| # | 시나리오 | 답의 뼈대 (영어 키워드) |
|---|---|---|
| 1 | DMA로 복사한 데이터가 가끔 몇 바이트 틀림 | cache clean/invalidate · buffer alignment to cache line · descriptor ordering (OWN last + barrier) · 같은 패턴인가(고정 offset → 주소 비트) · 전압 올리면 사라지나(margin) |
| 2 | 부팅이 JTAG 붙이면 되고 떼면 안 됨 | 디버거가 클럭/전원 도메인을 켜 둠 · 리셋 타이밍 · 디버그 레지스터 의존 코드 · watchdog |
| 3 | 고온(105 °C)에서만 DDR 에러 | training 결과가 상온 기준 → 온도 보정/주기적 retraining · refresh rate (고온에서 2×) · 마진 shmoo로 확인 |
| 4 | 인터럽트가 가끔 누락 | edge vs level 설정 · clear 순서(소스 먼저 clear 후 GIC EOI) · W1C RMW 버그 · 코얼레싱 |
| 5 | PCIe가 Gen4에서 Recovery 반복, Gen1은 OK | 신호 무결성/equalization · preset sweep · lane별 에러 카운터 · 레퍼런스 클럭 지터 |
| 6 | 칩 A만 실패, 칩 B·C는 통과 | 칩 단위 결함/코너 · 칩 swap(보드 vs 칩 분리) · Vmin 비교 · 그 칩 ATE 데이터 |
| 7 | NPU 결과가 마지막 몇 열만 틀림 | partial tile 처리 버그 · 경계 마스크 · golden 비교로 mismatch 패턴 특징화 → [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) |
| 8 | 카메라 프레임이 가끔 drop, 로그에 ECC 에러 | D-PHY 신호(lane skew, HS settle 설정) · 프레임 카운터·CRC 카운터 · 버퍼 언더런(메모리 대역폭) |
| 9 | 벤치마크 결과가 실행마다 20% 차이 | DVFS/thermal throttling · 인터럽트 · 캐시 상태 · 측정 오버헤드 · 반복·p50 보고 |
| 10 | Ethernet loopback에서 FCS 에러만 증가 | 어느 루프백 단계부터인가(MAC→PCS→PHY) · RGMII 타이밍 지연 설정 · 클럭 · PRBS로 PHY 단독 확인 |
| 11 (Iris) | warm reboot 후 PCIe endpoint가 `lspci`에서 사라짐 | PERST# 타이밍 · refclk 안정 시점 · endpoint FW 준비 전 config 접근(CRS 응답) · hot reset vs fundamental reset · AER/LTSSM 상태 · bus rescan으로 재현 |
| 12 (Iris) | PetaLinux에서 PL 레지스터 `mmap` 읽으면 프로세스 멈춤 / SError | 비트스트림 미로드 · PL 클럭/리셋 미해제 · 주소가 AXI 주소 맵 밖 · interconnect timeout 없음 · `O_SYNC` · device tree `reg` 확인 → [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) |
| 13 (Iris) | LiDAR와 카메라 timestamp가 정확히 1초(또는 18초) 차이 | PPS ↔ NMEA 초 매칭 off-by-one · GPS vs UTC leap second(18 s) · PTP domain/epoch 차이 · counter wrap → [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html) |
| 14 (Iris) | 1518 B에선 line rate인데 64 B에선 throughput이 반토막 | 바이트가 아니라 **패킷당** 처리 한계(pps) · 패킷당 인터럽트 → coalescing/NAPI · descriptor refill 속도 · CPU affinity → [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) |
| 15 (Iris) | 부팅이 FSBL 메시지 후 멈춤 (U-Boot 안 뜸) | `BOOT.BIN`에 PMU FW·ATF 누락 · XSA의 DDR 설정 · UART MIO 설정 · boot mode 핀 · JTAG boot로 단계 분리 (Q25) |
| 16 (Iris) | 센서 로거가 부하가 몰릴 때만 프레임을 잃음 | 링 버퍼 high-water / drop 카운터 · consumer 스레드 우선순위 · 스토리지 쓰기 지연(p99) · 메모리 ordering 버그(Arm에서만) → [L5](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html) |

- 공통 프레임: **Reproduce → Localize (which layer / which chip / which condition) → Hypothesize → Instrument (scope, analyzer, counters, GPIO trigger) → Fix → Add a regression test**

---

## 8. 코딩 — 연습 사이트

👉 **[hmScreenPrep/index.html](hmScreenPrep/index.html)** — 각 문제: 영어 문제 + `stub.c` + 테스트 + 한국어 풀이 + 차트 + 면접관 포인트. 실행: `cd hmScreenPrep && ./run.sh V2` / `./run.sh V2 sol` / `./check_all.sh`

**Manjiang 기준 우선순위 (10-28 재확정)** — L1–L5는 Iris용으로 만들었지만 JD 우대사항(PCIe·Zynq·벤치마크)이라 P1/P2로 유지

| 우선 | ID | 주제 | 왜 (면접관·JD 근거) |
|---|---|---|---|
| P0 | [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) | 레지스터 RMW · W1C · register sanity test | 모든 블록 enable의 첫 단계 |
| P0 | [V1](hmScreenPrep/problems/V1_march_memtest/index.html) | 메모리 테스트 (walking-1s, address bus, March C-) | DDR 우대 · bring-up |
| P0 | [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) | DMA descriptor ring + self-test | DMA는 NPU·카메라 검증의 기반 |
| P0 | [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html) | 벤치마크 통계 (cycle counter wrap, p99, MB/s) | JD: benchmark tools |
| P0 | [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html) | Shmoo sweep · Vmin · margin | 그의 PVT characterization |
| P0 | [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) | INT8 GEMM golden model + bit-exact compare | in-house AI accelerator |
| P0 | [D](hmScreenPrep/problems/D_debug_drills/index.html) | 디버그 드릴 10개 | "what's wrong with this code" |
| P1 | [L2](hmScreenPrep/problems/L2_pcie_cap_walk/index.html) | PCIe config space: capability walk · link 상태 · SR-IOV · AER · BAR sizing | Q16 PCIe 링크 디버깅 · Don의 최강 분야 |
| P1 | [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) | Zynq PetaLinux userspace 레지스터 접근 (`/dev/mem` · UIO) | JD 우대 Zynq Petalinux · 그의 FPGA 플랫폼 |
| P1 | [V5](hmScreenPrep/problems/V5_test_runner_coverage/index.html) | 테스트 러너 + 커버리지 리포트 | JD: automatic testing tools, coverage |
| P1 | [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html) | PRBS7/31 + CRC32 | 링크 loopback 검증 |
| P1 | [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html) | I2C scan + 9-clock recovery | 페리페럴 우대 |
| P1 | [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html) | MIPI CSI-2 패킷/ECC/CRC 스트림 체커 | camera 우대 · 그의 Baidu 센서 처리 |
| P1 | [V10](hmScreenPrep/problems/V10_eth_frame_fcs/index.html) | Ethernet frame/FCS + loopback | 그의 Marvell PHY · Cisco |
| P1 | [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) | 멀티 카메라 timestamp·sync 체커 | 그의 Baidu 특허 (sensor sync) |
| P2 | [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html) | GPS PPS 시간 동기 + LiDAR/카메라 매칭 | 그의 clock sync 특허와 연결 |
| P2 | [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) | RFC 2544 throughput 이진 탐색 | benchmark 방법론 |
| P2 | [L5](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html) | lock-free SPSC 링 (C11 atomics) | 일반 시스템 C |

<details>
<summary>(취소) Iris 기준 우선순위</summary>

L2 → L1 → V2 → L3 → V3 → L5 → L4 → D → V4 (P0: L2 · L1 · V2 · L3 · V3 · L5)

</details>

- 코딩이 나오면 말하는 순서: **clarify (width? endianness? volatile? error codes?) → API signature → normal case → edge cases → code → walk a test → complexity & what changes on real HW**

---

## 9. 역질문

**Manjiang판 (10-28 재확정)** — 3개만 고르면: 1 · 3 · 6

1. "Where is the next-generation SoC today — still on HAPS/FPGA, or approaching first silicon? What would this role own in the first six months?"
2. "How do the software validation engineers split work with the DV team and with your post-silicon / PVT team — who owns the validation plan for a block?"
3. "You built FPGA platforms for bring-up — do validation tests run unchanged across FPGA, emulation and silicon today, or is that something you'd want this role to build?"
4. "How does post-silicon validation content flow into production SLT? Is the same test framework used?"
5. "What's the hardest bug your team found on RAP1 that you wish had been caught earlier?" (경험 많은 HW 매니저가 좋아하는 질문)
6. "What does success look like for a Staff engineer on your team after one year?" (레벨 기대치 확인)

<details>
<summary>(취소) Iris판</summary>

1. "You moved from leading system validation to a principal system engineer role. How does silicon validation feed into system-level validation of the compute platform today?"
2. "On the sensor side — cameras, LiDAR, radar — where do most integration issues show up between the SoC inputs and the full sensor box?"
3. "Do the validation tests run bare-metal, on Linux on the Zynq-based platforms, or both? Is the framework shared between the FPGA platforms and silicon?"
4. "How do you benchmark the platform end to end — for example, sensor-to-inference latency — and who owns that number?"
5. "How big is the software validation group, and how does it split work with the silicon validation and DV teams?"
6. "What does success look like for a Staff engineer on this team after one year?" (레벨 기대치 확인)

</details>

---

## 10. D-2 플랜 (10-28 수 → 10-30 금) — Manjiang 재확정 기준

| 날짜 | 할 일 | 시간 |
|---|---|---|
| 10-28 (수) 오늘 · 일정 있음 | §1 Manjiang 다시 읽기 (10m) · §3 TMAY Manjiang판 소리 내어 2회 · §4 **S1·S2** `[ ]` 숫자 채우기 · 시간 되면 [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) 1개 (30m) | 1–1.5h |
| 10-29 (목) | 코딩: [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html)(어제 못 했으면) → [V1](hmScreenPrep/problems/V1_march_memtest/index.html) → [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) → [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html) 타이머 30분씩 · [D](hmScreenPrep/problems/D_debug_drills/index.html) 말로 · 개념: §5.1 Q1–Q8 · §5.3 Q16–Q19 · §0.5.6 RTL 질문 · §7 시나리오 1–10 · **45분 모의 면접** (TMAY → S1 → Q1 → Q2 → "Can you read RTL?" → Q16 → 시나리오 1개 → 역질문) | 4h |
| 10-30 (금) 당일 | 아침: §0 · §0.5 결론 · §1.3 · §3 · §9만 훑기 · 종이와 펜(그림용) · 물 · 연결 10분 전 | 30m |

<details>
<summary>(취소) Iris 기준 플랜 (10-28)</summary>

| 날짜 | 할 일 | 시간 |
|---|---|---|
| 10-28 (수) 오늘 · 일정 있음 | §1 Iris 분석 (10m) · §3 TMAY Iris판 소리 내어 2회 · §4 **S8 PCIe** `[ ]` 채우기 · 시간 되면 [L2](hmScreenPrep/problems/L2_pcie_cap_walk/index.html) 1개 (30m) | 1–1.5h |
| 10-29 (목) | 코딩: [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) → [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) → [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) → [L5](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html) 타이머 30분씩 (§0 한국어 소개 먼저) · 개념: §5.5 Q25–Q38 소리 내어 · §7 시나리오 11–16 · **45분 모의 면접** (TMAY → S8 → L2 or L5 코딩 20분 → Q25 → Q31 → 역질문) | 4h |
| 10-30 (금) 당일 | 아침: §0 · §1.2 · §3 Iris판 · §5.5 Q25/Q28/Q31 요점 · §9 Iris판만 · 종이와 펜 · 연결 10분 전 | 30m |

</details>

<details>
<summary>이전 플랜 (10-27 기준, Manjiang 면접관 가정)</summary>

| 날짜 | 할 일 | 시간 |
|---|---|---|
| 10-27 (화) 오늘 | **§0.5 역할 해석 + RTL 크래시 코스** (30m) · §3 TMAY 소리 내어 3회 · §4 스토리 S1–S4 `[ ]` 숫자 채우기 · [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) → [V1](hmScreenPrep/problems/V1_march_memtest/index.html) 타이머 30분씩 (§0 한국어 소개 먼저) | 2–3h |
| 10-28 (수) 일정 있음 | 짬 날 때만: §5.1 Q1–Q8 영어로 소리 내어 답하기 · §0.5.6 RTL 질문 답 2개 입에 붙이기 | 1h |
| 10-29 (목) | [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) · [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html) · [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html) · [D](hmScreenPrep/problems/D_debug_drills/index.html) · §5.3 인터페이스 Q16–Q19 · §7 시나리오 10개 말로 · **45분 모의 면접 1회** (TMAY → S1 → Q1 → "Can you read RTL?" → Q16 → 시나리오 1개 → 역질문) | 3–4h |
| 10-30 (금) 당일 | 아침: §0 · §0.5 결론 · §1.3 · §3 · §9만 훑기 · 종이와 펜(그림용) · 물 · 연결 10분 전 | 30m |

</details>

<details>
<summary>이전 플랜 (10-28 수 기준, 연기로 폐기)</summary>

| 날짜 | 할 일 | 시간 |
|---|---|---|
| 10-25 (일) 오늘 | §3 TMAY 소리 내어 3회 · §4 스토리 S1/S2/S3 `[ ]` 숫자 채우기 · §5.1 Q1–Q8 영어로 답해 보기 | 2–3h |
| 10-26 (월) | ⚠ **Scout 스크린 10:00 PT 우선** · 오후: [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) → [V1](hmScreenPrep/problems/V1_march_memtest/index.html) → [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) 타이머 30분씩 · [D](hmScreenPrep/problems/D_debug_drills/index.html) | 2–3h |
| 10-27 (화) | [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html) · [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html) · [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) · §5.3 인터페이스 Q16–Q19 · §7 시나리오 10개 말로 · **45분 모의 면접 1회** (TMAY → S1 → Q1 → Q16 → 시나리오 1개 → 역질문) | 3h |
| 10-28 (수) 당일 | 아침: §0 · §1.3 · §3 · §9만 훑기 · 종이와 펜(그림용) · 물 · 연결 10분 전 | 30m |

</details>

### 당일 체크리스트
- [ ] 금요일 10-30 **변경된 시간**·형식(Zoom/전화, 코딩 툴 여부) 리크루터 확인 메일/캘린더 초대 받았는지 확인
- [ ] 캘린더 초대 참석자가 **Manjiang Zhang**인지 최종 확인 (또 바뀌면 §1B Iris 자료로 전환)
- [ ] 레쥬메 PDF 한 장 옆에 띄워 두기
- [ ] 스토리 S1–S4 숫자 메모 (한 줄씩)
- [ ] 역질문 3개 선택
- [ ] 끝나고 바로 받은 질문 기록 → 컨텍스트 §6 회고

---

## 11. 출처

1. https://www.linkedin.com/in/manjiang/ — Manjiang Zhang LinkedIn 프로필·경력란 (로그인 열람만, 2026-10-25)
2. https://patents.justia.com/inventor/manjiang-zhang — Baidu USA 특허 목록 (검색 요약 기준, 2026-10-25)
3. https://careers.rivian.com/careers-home/jobs/34042?lang=en-us — JD (2026-10-17 수집, 컨텍스트 부록 A)
4. 회사·RAP1 출처는 [포지션 컨텍스트](rivian_nstaff_silicon_validation_engineer_context.html) §7 참고
5. https://careers.rivian.com/api/jobs?keywords=RTL%20Design — 같은 조직 비교 공고: RTL Design Engineer (req 33461, "Strong proficiency in Verilog and SystemVerilog"), Sr. Staff ASIC Design Verification Engineer (req 30325, SystemVerilog/UVM), SOC Architect (req 31232) (확인일 2026-10-27)
6. https://www.linkedin.com/in/iris-lee-8720368/ — Iris Lee LinkedIn 프로필·경력란 (로그인 열람만, 2026-10-28)

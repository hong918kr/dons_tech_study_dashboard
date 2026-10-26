<!-- theme: pastel -->
# Rivian — HM Technical Screen 준비 Deck (Manjiang Zhang · 45분)

> **작성일**: 2026-10-25(일) · **인터뷰**: **2026-10-28(수)** 45분 (시간 확인 필요) · **면접관**: Manjiang Zhang, Sr. Engineer/Manager (Silicon Validation) @ Rivian
> **같이 볼 것**: [코딩 연습 사이트 (V1–V12 + D)](hmScreenPrep/index.html) · [포지션 컨텍스트](rivian_nstaff_silicon_validation_engineer_context.html)
> 신뢰도: `[확인됨]` = LinkedIn 경력란·공개 특허·JD · `[추정]` = 그것을 바탕으로 한 판단. LinkedIn은 열람만 했고 연결 요청·팔로우·메시지는 하지 않았다.

---

## 0. TL;DR

- **Manjiang은 20년차 "하드웨어 사람"이다** [확인됨]: Cisco 패킷 프로세서 ASIC(2006–10) → Marvell **Ethernet PHY ASIC**(2010–12) → Brocade FPGA 패킷·암호화(2012–15) → **Baidu USA 자율주행 컴퓨트 HW 아키텍트·팀 리드**(2015–22, FPGA 센서 데이터 처리·알고리즘 가속, L4 차량에 배포) → **Rivian**(2022-12~, 자율주행 실리콘: 아키텍처 리뷰, **FPGA/HAPS 프로토타이핑, silicon bring-up, post-silicon validation, PVT characterization, production SLT**, bring-up용 FPGA 플랫폼 개발)
- **함의**: 이 사람은 SW 알고리즘 퀴즈보다 **"실리콘이 처음 나왔을 때 네가 뭘 어떤 순서로 하느냐"**를 볼 확률이 높다. 질문은 bring-up 순서, FPGA vs silicon 차이, PVT/shmoo, 테스트 자동화, 인터페이스(PCIe·DDR·Ethernet·카메라) 디버깅으로 올 것 [추정]
- **Don의 최고 카드 3장**: ① Solidigm **RTL freeze 전 FPGA SoC 검증 + Cortex-R bring-up** ② SK hynix **shmoo / 고속 인터페이스 reliability 시스템** (= 그의 PVT characterization 언어) ③ Apple **새 칩 시스템 통합 루트코즈 + factory test-node** (= 그의 SLT 언어)
- **45분 예상**: 소개 5 → 경험 딥다이브 15 → 기술 질문/디버깅 시나리오 15 → (짧은 코딩 or 설계 5–10) → 역질문 5 [추정]
- **이번 주 플랜**: 일(오늘) 스토리 + 개념 · 월 Scout 스크린(10:00) 후 V2/V1/V3 · 화 V4/V7/V11 + 모의 면접 · 수 아침 치트시트만 (§10)

---

## 1. 면접관 분석 — Manjiang Zhang

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
- 영어: 비원어민 매니저일 가능성 → **천천히, 구조적으로(First/Second/Third), 숫자로** 말하기가 가장 잘 먹힘 [추정]

---

## 2. 45분 예상 구성 & 전략

```
0–5   인사 · 팀 소개 · "Tell me about yourself"          → §3 (90초, 마지막 문장으로 관심 유도)
5–20  레쥬메 딥다이브: Solidigm FPGA/bring-up, shmoo, Apple → §4 스토리 S1–S4
20–35 기술 질문: bring-up 순서, PVT, PCIe/DDR, 디버깅 시나리오 → §5 · §7
35–40 (가능) 짧은 C 문제 or 검증 설계 화이트보드        → §6 · §8
40–45 역질문                                              → §9
```
- **원칙 1 — 그림을 그리며 말하라**: "Let me walk through it in the order I'd actually do it on first silicon." 순서·단계로 답하면 HW 매니저가 따라오기 쉽다
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

---

## 4. 경험 딥다이브 — 스토리 뱅크 (STAR, 영어 핵심 문장)

> ⚠ `[ ]` 칸은 Don이 실제 숫자로 채울 것. 숫자 없는 스토리는 HW 매니저에게 약하다. 기밀은 "a PCIe controller block", "a wireless chipset"처럼 일반화.

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

- 공통 프레임: **Reproduce → Localize (which layer / which chip / which condition) → Hypothesize → Instrument (scope, analyzer, counters, GPIO trigger) → Fix → Add a regression test**

---

## 8. 코딩 — 연습 사이트

👉 **[hmScreenPrep/index.html](hmScreenPrep/index.html)** — 각 문제: 영어 문제 + `stub.c` + 테스트 + 한국어 풀이 + 차트 + 면접관 포인트. 실행: `cd hmScreenPrep && ./run.sh V2` / `./run.sh V2 sol` / `./check_all.sh`

| 우선 | ID | 주제 | 왜 (면접관·JD 근거) |
|---|---|---|---|
| P0 | [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) | 레지스터 RMW · W1C · register sanity test | 모든 블록 enable의 첫 단계 |
| P0 | [V1](hmScreenPrep/problems/V1_march_memtest/index.html) | 메모리 테스트 (walking-1s, address bus, March C-) | DDR 우대 · bring-up |
| P0 | [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) | DMA descriptor ring + self-test | DMA는 NPU·카메라 검증의 기반 |
| P0 | [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html) | 벤치마크 통계 (cycle counter wrap, p99, MB/s) | JD: benchmark tools |
| P0 | [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html) | Shmoo sweep · Vmin · margin | 그의 PVT characterization |
| P0 | [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) | INT8 GEMM golden model + bit-exact compare | in-house AI accelerator |
| P0 | [D](hmScreenPrep/problems/D_debug_drills/index.html) | 디버그 드릴 10개 | "what's wrong with this code" |
| P1 | [V5](hmScreenPrep/problems/V5_test_runner_coverage/index.html) | 테스트 러너 + 커버리지 리포트 | JD: automatic testing tools, coverage |
| P1 | [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html) | PRBS7/31 + CRC32 | 링크 loopback 검증 |
| P1 | [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html) | I2C scan + 9-clock recovery | 페리페럴 우대 |
| P1 | [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html) | MIPI CSI-2 패킷/ECC/CRC 스트림 체커 | camera 우대 · 그의 Baidu 센서 처리 |
| P1 | [V10](hmScreenPrep/problems/V10_eth_frame_fcs/index.html) | Ethernet frame/FCS + loopback | 그의 Marvell PHY · Cisco |
| P1 | [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) | 멀티 카메라 timestamp·sync 체커 | 그의 Baidu 특허 (sensor sync) |

- 코딩이 나오면 말하는 순서: **clarify (width? endianness? volatile? error codes?) → API signature → normal case → edge cases → code → walk a test → complexity & what changes on real HW**

---

## 9. 역질문 (Manjiang 맞춤)

1. "Where is the next-generation SoC today — still on HAPS/FPGA, or approaching first silicon? What would this role own in the first six months?"
2. "How do the software validation engineers split work with the DV team and with your post-silicon / PVT team — who owns the validation plan for a block?"
3. "You built FPGA platforms for bring-up — do validation tests run unchanged across FPGA, emulation and silicon today, or is that something you'd want this role to build?"
4. "How does post-silicon validation content flow into production SLT? Is the same test framework used?"
5. "What's the hardest bug your team found on RAP1 that you wish had been caught earlier?" (경험 많은 HW 매니저가 좋아하는 질문)
6. "What does success look like for a Staff engineer on your team after one year?" (레벨 기대치 확인)

---

## 10. D-3 플랜 (10-25 일 → 10-28 수)

| 날짜 | 할 일 | 시간 |
|---|---|---|
| 10-25 (일) 오늘 | §3 TMAY 소리 내어 3회 · §4 스토리 S1/S2/S3 `[ ]` 숫자 채우기 · §5.1 Q1–Q8 영어로 답해 보기 | 2–3h |
| 10-26 (월) | ⚠ **Scout 스크린 10:00 PT 우선** · 오후: [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html) → [V1](hmScreenPrep/problems/V1_march_memtest/index.html) → [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) 타이머 30분씩 · [D](hmScreenPrep/problems/D_debug_drills/index.html) | 2–3h |
| 10-27 (화) | [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html) · [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html) · [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) · §5.3 인터페이스 Q16–Q19 · §7 시나리오 10개 말로 · **45분 모의 면접 1회** (TMAY → S1 → Q1 → Q16 → 시나리오 1개 → 역질문) | 3h |
| 10-28 (수) 당일 | 아침: §0 · §1.3 · §3 · §9만 훑기 · 종이와 펜(그림용) · 물 · 연결 10분 전 | 30m |

### 당일 체크리스트
- [ ] 인터뷰 시간·형식(Zoom/전화, 코딩 툴 여부) 리크루터에게 확인
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

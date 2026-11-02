# N06E · 연습③ 고속 I/O · 센서 — PCIe · Ethernet · MIPI CSI-2 카메라 · 시간 동기 테스트 플랜
<!-- theme: pastel -->
> **묶음**: N06 블록 연습 ③ · **우선**: P0 ★ (E2 Ethernet · E3 카메라 · E4 시간 동기 = 면접관 Marvell Ethernet PHY ASIC + Baidu sensor timestamp/sync/multi-camera 특허 분야 [확인됨: deck §1]) · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06D 데이터 이동·메모리](2026-11-01_N06D_plans_dma_memory.html) / [N06F NPU·SoC·안전](2026-11-01_N06F_plans_npu_soc_safety.html) · **관련**: [LP14 PCIe 검증 방법론](2026-10-31_LP14_validation_methodology.html) · [LP00A 입문 primer (Ethernet §4, MIPI §5)](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [BV10 보드 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) · [BV12 margin·shmoo·PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [deck §5.4 Q24, §6.6, §7](2026-10-25_rivian_hm_screen_prep.html) · [L3 RFC 2544](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) · [L4 PPS discipline](hmScreenPrep/problems/L4_pps_time_discipline/index.html) · [V9 CSI-2 checker](hmScreenPrep/problems/V9_csi2_packet_parse/index.html) · [V10 Ethernet FCS](hmScreenPrep/problems/V10_eth_frame_fcs/index.html) · [V12 다중 카메라 timestamp](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) · **코드**: [onsitePrep/n06/e/](onsitePrep/n06/e/run.sh) — [e_numbers.py](onsitePrep/n06/e/e_numbers.py) · [csi2_err_inject.py](onsitePrep/n06/e/csi2_err_inject.py) · [실행 출력](onsitePrep/n06/e/run_output.txt)

## 0. TL;DR · 사용법

- **이 페이지**: "Design a test plan for our PCIe / Ethernet / camera pipeline / time sync" 를 **10분 안에** 말하기 위한 블록 4개. 각 블록 = 🔰 설명 → clarify → **테스트 매트릭스 1장** → P0/P1/P2 → 장비 → 잘 터지는 버그 → 10분 영어 뼈대 → 꼬리 질문 → Don 연결.
- **★ 블록**: **E3 CSI-2 카메라 파이프라인**(가장 확률 높음 — ADAS SoC의 입력) · **E2 automotive Ethernet MAC+PHY**(면접관이 PHY 설계자 출신 → 깊이 질문 가능) · **E4 시간 동기**(면접관 특허 분야). **E1 PCIe**는 [LP14](2026-10-31_LP14_validation_methodology.html)를 한 장으로 압축만.
- **처음이면**: Ethernet/MIPI 기초는 각 블록 §0 🔰 → 더 필요하면 [LP00A §4 Ethernet, §5 MIPI](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html). 이 페이지는 **"IP 블록 검증 관점"** 만 추가한다 (보드 측정은 [BV10](2026-11-01_BV10_board_interface_tests.html)).
- **외울 숫자 (전부 코드로 계산, §E2/E3/E4에 출력 붙임)**:
  - 1000BASE-T1 64 B line rate = **1,488,095 fps** (프레임당 672 ns) · 100BASE-T1 1518 B = **8,127 fps**
  - "BER < 1e-12 at 95%" = 에러 0으로 **3.0e12 bit** → 1 Gb/s **50분**, 100 Mb/s **8.3시간**
  - CSI-2 헤더 ECC: 1-bit **정정**, 2-bit **검출**, 그러나 **3-bit은 93%가 조용히 오정정** (실행 결과) → ECC만 믿지 말고 WC·frame 구조 검사를 같이
  - PTP: 경로 비대칭 200 ns → offset 오차 **100 ns (= 비대칭/2)**, 100 ppm 오실레이터는 125 ms 동안 **12.5 µs** 흐름
  - Rolling shutter 2160줄 @30 fps → 첫 줄과 끝 줄 **32 ms 차이** → 30 m/s면 **0.96 m** — "timestamp가 어느 순간이냐"를 먼저 정의
- **연습법**: 블록 하나 고르고 **타이머 10분** → §1의 7단계 프레임대로 **소리 내어** 말하기 → §6 뼈대와 비교 → §7 꼬리 질문을 `<details>` 열기 전에 먼저 답해 보기 → 빠진 카테고리를 [N06I 연습 기록](2026-11-01_N06I_mock_10min_qbank.html)에 적기.
- **30초 영어 (headline Q: "How would you validate the camera input path on our SoC?")**: "I'd split it into the CSI-2 receiver and the ISP. For the receiver I start with a known source — the internal or sensor test pattern — and compare frames in DDR bit-exactly, then sweep lanes, data rates, data types and virtual channels, inject header-ECC, CRC and framing errors to prove every error counter and recovery path, and measure margin with HS-settle and voltage/temperature sweeps. For the ISP I feed RAW frames from memory and compare each stage against the C model. Then system level: all cameras at full rate with NPU traffic on DDR, zero dropped frames over a 24-hour soak, and frame timestamps checked against a PTP-disciplined reference."

## 1. 공통 틀 복습

7단계 10분 프레임 ([N06A](2026-11-01_N06A_methodology_test_plan_template.html)에서 자세히):

| 단계 | 시간 | 말할 것 | 영어 시작 문장 |
|---|---|---|---|
| ① Clarify | 30초 | IP 출처·모드 범위·레퍼런스 모델·플랫폼 | "Before I start, a few scoping questions…" |
| ② 블록 그림 | 1분 | 버스·IRQ·DMA·PHY·상대 장치 | "Let me draw where it sits…" |
| ③ 매트릭스 | 3분 | 12 카테고리를 빠짐없이 | "I'd organize the plan by category…" |
| ④ 우선순위·플랫폼 | 1분 | P0 bring-up / FPGA vs Si | "On A0 the first week I'd run…" |
| ⑤ 통과 기준·커버리지 | 1분 | 숫자 + 통계 (rule of three) | "Pass means a number, for example…" |
| ⑥ 자동화·회귀 | 1분 | 하네스·결과 DB·nightly | "All of it runs in the harness…" |
| ⑦ 리스크·첫 주 | 1분 | 아날로그·interop·DV가 못 덮는 곳 | "The biggest risks I'd attack first are…" |

12 카테고리 (매트릭스의 행 이름으로 그대로 쓴다):

| # | 카테고리 | 고속 I/O에서 특히 볼 것 |
|---|---|---|
| ① | REG 레지스터 | reset 값, W1C 에러 상태, 카운터 clear-on-read 여부 |
| ② | FUNC 기본 기능 | 루프백·패턴 생성기로 **알려진 데이터** 비교 |
| ③ | CFG 설정 조합 | 속도 × 폭(lane) × 모드 × 포맷 매트릭스 |
| ④ | BND 경계 | 최소/최대 프레임·패킷, ring wrap, 정렬 |
| ⑤ | INT 인터럽트 | 완료·에러·링크 변화, coalescing |
| ⑥ | ERR 에러/negative | **오류 주입** → 카운터·drop·복구 |
| ⑦ | RST 리셋/전원 | 트래픽 중 리셋, 저전력 진입·탈출, 링크 재수립 시간 |
| ⑧ | STRESS 동시성 | 모든 포트 동시 + DDR 경합 |
| ⑨ | PERF 성능 | throughput·latency·drop, % of line rate |
| ⑩ | PVT/margin | BER·eye·settle sweep, 온도 챔버 |
| ⑪ | SOAK | 24–72 h, 에러 0 |
| ⑫ | SYS/interop | 실제 상대 PHY·센서·SW 스택 |

## E1 · PCIe controller (RC/EP) — LP14 한 장 압축

> 새 내용 최소. 원본: [LP14](2026-10-31_LP14_validation_methodology.html) (12 영역 · rule of three · EP framework · 하네스), 링크 트레이닝은 [LP03](2026-10-31_LP03_link_training_ltssm.html), 디버그는 [LP15](2026-10-31_LP15_debug_scenarios.html).

### 0. 🔰 이 블록이 뭔가

- **비유**: 칩과 칩 사이의 **전용 고속도로**. 차선(lane) 수와 제한 속도(Gen)를 개통식(link training)에서 정하고, 그 위로 "남의 메모리를 읽고 쓰는" 트럭(TLP)이 다닌다. 트럭이 망가지면 **고속도로 자체가 재전송**(ACK/NAK)한다.
- **정확히**: PCI Express 컨트롤러 = Transaction/Data Link/PHY(MAC 부분) 로직 + SerDes PHY. SoC에서 Root Complex(RC, 호스트) 또는 Endpoint(EP, 장치) 모드. 자동차 AI SoC에서는 NVMe SSD, 다른 SoC, Wi-Fi/5G 모뎀, 외부 가속기와 연결 [추정].

```text
  CPU ─┬─ NoC/AXI ─┬─ PCIe controller (RC/EP) ─ PIPE ─ SerDes PHY ═══ x4 lanes ═══► 상대 (NVMe / SoC / FPGA)
       │           │    ├ iATU (주소 변환)        ▲ refclk 100 MHz, PERST#, CLKREQ#
  GIC ◄┴── MSI/INTx┘    ├ eDMA (내장 DMA)
                        └ AER · LTSSM debug regs
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요 |
|---|---|---|---|
| LTSSM | Link Training & Status State Machine | 개통식 진행 순서 | 링크 안 뜸 디버그 1순위 |
| AER | Advanced Error Reporting | 에러 종류별 장부 | 신호 품질 지표 |
| LMR | Lane Margining at Receiver | 수신기 샘플 지점을 옮겨 여유 측정 | 16 GT/s+ 필수 |
| iATU | internal Address Translation Unit | CPU 주소 ↔ PCIe 주소 변환 창 | 열거·BAR 버그 |

### 1. 먼저 물어볼 것 (clarify)

- "Is the controller licensed IP (for example DesignWare) and how much did the vendor already validate?" → 가정: 라이선스 IP + 자체 PHY 통합, 벤더 compliance는 IP 단위만.
- "RC only, EP only, or both? Which max Gen and width?" → 가정: RC+EP, Gen4 x4.
- "Do we have an FPGA image with this controller and a partner card?" → 가정: HAPS 이미지 + FPGA XDMA 파트너.
- "What are the system use cases — NVMe, chip-to-chip?" → 가정: NVMe 로깅 + SoC 간 링크.
- "Is PCI-SIG compliance in scope for us or the SI team?" → 가정: 측정은 SI팀, 컴플라이언스 모드 진입 SW는 우리.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| REG | config space·capability 체인·컨트롤러 레지스터 reset 값 | RC/EP 각각 | 둘 다 | 문서와 100% 일치, RO 쓰기 무시 | P0 |
| FUNC | 링크 업 + 열거 + BAR R/W | Gen1 x1부터 목표까지 | 둘 다 | 기대 speed/width, DLActive+ | P0 |
| FUNC | DMA 데이터 무결성 (eDMA, pci-epf-test, XDMA) | 1 B–MB, 비정렬, 4 KiB 경계 교차 | 둘 다 | CRC 일치 100%, timeout 0 | P0 |
| CFG | speed × width 매트릭스, lane reversal/polarity | Gen1–4 × x1/x2/x4 | Si (FPGA는 저속만) | 모든 칸 학습 성공 | P1 |
| CFG | MPS/MRRS 조합, IOMMU on/off | 128/256/512 B | 둘 다 | 데이터 일치 + 성능 기록 | P1 |
| BND | 64-bit BAR·큰 BAR·iATU 창 경계 | BAR 4 KiB–수 GB | 둘 다 | 할당 성공, 창 밖 접근 = 정의된 에러 | P1 |
| INT | INTx/MSI/MSI-X 전 벡터 | 고빈도, affinity 변경 | 둘 다 | raise 수 = 수신 수 | P0 |
| ERR | AER 주입 (aer-inject, 컨트롤러 LCRC 주입), DPC | correctable/non-fatal/fatal | 둘 다 | 보고·분류·복구, 패닉 0 | P1 |
| RST | cold/PERST#/hot reset/FLR 루프 | 각 ≥ 3,000 사이클 | Si | 0 실패 → 95% 상한 1e-3 | P0 |
| RST | ASPM L1/L1.2, D3↔D0, suspend/resume | 1만 회 | Si | 복귀 후 기능·속도 유지 | P1 |
| STRESS | DMA 최대 + DDR 경합 + 다른 master | 동시 채널 | Si | 에러 0, 성능 저하 기록 | P1 |
| PERF | read/write throughput, latency | 크기 × outstanding | Si | ≥ 이론 데이터율의 [X]% [추정 목표] | P1 |
| PVT/margin | LMR (pcilmr), EQ preset sweep, AER at corner | V × T × SS/TT/FF | Si | 모든 lane EW/EH ≥ 권장치 | P1 |
| SOAK | 72 h 트래픽 + 온도 ramp | 챔버 | Si | 자율 downtrain 0, AER 기준 이하 | P2 |
| SYS | 여러 파트너 interop (NVMe 3종, x86, SoC 대 SoC) | 실제 드라이버 | Si | 모든 파트너 기능 PASS | P1 |

### 3. 우선순위 · A0 첫 주

- **P0 (A0 첫 주)**: ① 전원·refclk·PERST# 타이밍 스코프 확인 → ② 컨트롤러 레지스터 sanity → ③ Gen1 x1 링크 업 → 목표 Gen/width → ④ 열거 + BAR + MSI 1개 → ⑤ 기본 DMA 루프백 + 링크 루프 100회.
- **P1**: 매트릭스·인터럽트 전부·리셋 루프 3,000회·에러 주입·interop. **P2**: PVT margin 지도, soak, 성능 튜닝.
- **안 할 것**: TLP 순서 규칙·LTSSM 상태 전이 coverage·credit 계산 = **DV가 VIP로 덮음**. 우리는 "실제 SerDes·보드·파트너·온도"에서만 보이는 것.

### 4. 장비 · 셋업 · 관찰 지점

- protocol analyzer/interposer, exerciser, FPGA 파트너(XDMA), 스코프(PERST#·refclk·전원), 챔버, 프로그래머블 전원/PDU.
- 관찰: LnkSta/LnkSta2, AER 카운터 delta, 컨트롤러 LTSSM debug 레지스터, `lspci -vvv`, `pcilmr`, dmesg — 상세 [LP13](2026-10-31_LP13_linux_pcie_tools.html).

### 5. 잘 터지는 버그

- PERST# deassert가 refclk 안정 전 → 간헐 링크 실패 (리셋 루프가 잡음).
- Gen4 EQ Phase 3 timeout → Gen1 fallback (speed 매트릭스 + EQ 상태 기록).
- non-coherent DMA에서 cache maintenance 누락 → 가끔 데이터 틀림 (패턴+CRC, 반복).
- MSI 주소 inbound 매핑 누락 → MSI 안 옴 (벡터 전수 테스트).
- 고온에서만 Receiver Error 급증 (PVT AER 행).

### 6. 10분 영어 답 뼈대

1. "Scope: RC and EP modes, Gen4 x4, licensed controller with our PHY integration; FPGA image and an FPGA partner exist."
2. "Picture: controller behind the NoC, iATU, eDMA, MSI to the GIC, SerDes with refclk and PERST#."
3. "Categories: registers and capabilities; link-up and enumeration; DMA integrity across size, alignment and the 4 KB boundary; every speed-width combination."
4. "Interrupts: every INTx, MSI and MSI-X vector, count raised equals count received."
5. "Errors: AER injection in software and real LCRC errors from the controller's injector; recovery without a kernel panic."
6. "Reset and power: cold, PERST#, hot reset and FLR loops — and I say which one, because they test different paths; ASPM L1 substates and D3."
7. "Performance against theoretical data rate per MPS and MRRS."
8. "Margin: lane margining with pcilmr, equalization preset sweeps, AER counts at voltage and temperature corners."
9. "Interop with several NVMe drives, an x86 host and SoC-to-SoC."
10. "Pass criteria are numbers: 3,000 clean cycles per reset type gives a 95% upper bound of 1e-3."
11. "Platforms: functional tests first on FPGA, so on silicon I debug only the analog and timing difference."
12. "Automation: PDU, UART, PERST# GPIO, results DB with chip, board, build, V and T; logs captured at the moment of failure."
13. "First week on A0: power and refclk on the scope, Gen1 x1, then target speed, one MSI, one DMA."
14. "Not repeating DV: TLP ordering and LTSSM coverage are theirs."

### 7. 꼬리 질문

- Q1. "A thousand clean cycles — is that enough?"

<details><summary>답</summary>

"It depends on the failure rate I need to rule out. Zero failures in a thousand cycles only says the rate is below about 1 in 334 at 95% confidence — the rule of three. For 1e-3 I need about 3,000 clean cycles spread across several boards and chips, and I state which reset type, because remove-rescan doesn't even retrain the link."
- 한국어 핵심: 0/N → 상한 3/N, 1e-3이면 3,000회, 여러 보드, 리셋 종류 명시.

</details>

- Q2. "How do you test DMA without a real endpoint?"

<details><summary>답</summary>

"If our controller supports endpoint mode, a second board runs Linux's pci-epf-test function and the host runs pci_endpoint_test with checksums. Otherwise an FPGA card with an XDMA-style IP gives me loopback memory and lets me misbehave on purpose — delayed completions, injected errors."
- 한국어 핵심: EP framework 또는 FPGA XDMA 파트너 → 오동작 주입까지 가능.

</details>

- Q3. "Gen4 trains but drops to Gen1 at 105 °C. What's in your plan for that?"

<details><summary>답</summary>

"That's exactly the PVT row: equalization status and lane margining per lane at temperature, with the AER receiver-error count. I'd sweep presets at the hot corner, look for one weak lane pointing to the board or package, and correlate with refclk jitter. The fix might be a preset table or a board change, and the regression adds a hot-corner link loop."
- 한국어 핵심: 고온 EQ/LMR lane별, preset sweep, 약한 lane → 보드, 회귀에 hot-corner 루프.

</details>

### 8. Don 연결

- "At SK hynix I validated PCIe Gen3–Gen5 interface reliability on SSDs, including shmoo-style margin sweeps in temperature chambers, and at Solidigm I brought up a PCIe Gen6 SSD controller on FPGA before silicon — so link loops with statistics and FPGA-first test content are habits, not theory. [숫자 채우기: 챔버 수 · 사이클 수]"

## E2 · Ethernet MAC + PHY (automotive 100BASE-T1 / 1000BASE-T1, RGMII/SGMII) ★

### 0. 🔰 이 블록이 뭔가

- **비유 (택배 회사)**: **MAC** = 택배 접수 창구. 소포(프레임)에 받는 사람·보내는 사람 주소를 붙이고, 끝에 **파손 검사 스티커(FCS = CRC-32)** 를 붙이고, 들어온 소포는 스티커를 검사해 깨진 건 버린다. **PHY** = 트럭 운전사 + 도로. 디지털 비트를 **전선 위 전압 파형**으로 바꾸고, 상대 운전사와 "몇 km/h로 갈까, 누가 앞장설까"를 정한다. 둘 사이 **RGMII/SGMII** = 창구와 트럭 사이 **하역장**. **MDIO** = 창구에서 운전사에게 거는 **무전기** (설정·상태 확인 전용).
- **자동차 Ethernet이 다른 점**: 일반 PC Ethernet(1000BASE-T)은 전선 **4쌍**, 자동차 **BASE-T1**은 **1쌍**(가볍고 싸게). 그리고 "누가 앞장설까(master/slave)"를 협상하지 않고 **설계 때 고정**(strap 핀이나 레지스터) → 차에 시동 걸자마자 빨리 링크가 떠야 하니까.
- **정확한 정의** [확인됨 [2]]:
  - **100BASE-T1** = IEEE **802.3bw-2015**, Clause 96, PAM3, 66.66 MBd, 1쌍, 15 m (link segment type A).
  - **1000BASE-T1** = IEEE **802.3bp-2016**, Clause 97, PAM3 750 MBd, 80B/81B + **RS-FEC**, 최대 40 m (link segment type B; type A는 15 m [추정]).
  - 더 빠른 2.5/5/10GBASE-T1 = 802.3ch-2020 (PAM4) [2]. 10BASE-T1S = 802.3cg (multidrop 버스) [2].
  - PAM3 = 전압 **3단계**(−1, 0, +1)로 심볼당 1비트 이상 실어 보냄 → 같은 데이터율에 주파수를 낮춰 EMI·케이블 요구를 줄인다.
  - RS-FEC = Reed-Solomon 순방향 오류 정정: 수신 쪽이 **재전송 없이** 일부 심볼 오류를 고침 → 1000BASE-T1은 FEC가 있어 "FCS 에러 0"만 보면 **margin이 줄어드는 걸 늦게 안다** → FEC 정정 카운터·SQI를 같이 봐야 한다 [추정 — 카운터 노출은 PHY마다].

```text
  ┌────────────────────────── SoC ──────────────────────────┐        ┌──────── 외부 T1 PHY 칩 (또는 SoC 내장) ────────┐
  │ CPU ─ NoC ─┬─ Ethernet MAC (GEM/EQOS 류 IP) ────────────┼─RGMII──┼─ PCS(80B/81B, RS-FEC) ─ PMA(PAM3, echo cancel)─┼═ 1쌍 UTP ═► 상대 ECU
  │            │   ├ TX/RX DMA descriptor ring ◄─► DDR       │ /SGMII │   ├ master/slave strap                        │   (zonal, 카메라, 스위치)
  │  GIC ◄─IRQ─┤   ├ FCS gen/check · 필터 · VLAN · csum offload│        │   ├ loopback 모드 · test mode · PRBS gen/check │
  │            │   ├ PTP 타임스탬프 유닛 (PHC) ── PPS/이벤트 핀 │        │   └ SQI · 케이블 진단(TDR)                     │
  │            │   └ MDIO master ───────────────────────────┼─MDC/MDIO┼─► Clause 22 / Clause 45 레지스터             │
  └──────────────────────────────────────────────────────────┘        └────────────────────────────────────────────────┘
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| MAC | Media Access Control | 프레임 조립/검사·DMA (창구) | 우리가 검증하는 SoC IP |
| PHY | Physical layer device | 비트 ↔ 아날로그 파형 (운전사) | 면접관의 전문 분야 (Marvell) |
| PCS / PMA / PMD | Physical Coding Sublayer / Physical Medium Attachment / Dependent | PHY 안의 부호화 / 변조·SerDes / 실제 송수신 | loopback 지점 이름 |
| RGMII | Reduced Gigabit MII | 4비트 × 125 MHz 양 edge, 병렬 | **클럭-데이터 2 ns 지연** 설정이 1번 버그 |
| SGMII | Serial GMII | 1.25 Gbaud 직렬 차동 1쌍 | SerDes, 핀 적음 |
| MDIO / MDC | Management Data I/O / Clock | 2선 관리 버스 (무전기) | PHY ID 읽기 = 첫 연결 증명 |
| Clause 22 / 45 | IEEE 802.3 관리 레지스터 규격 | 22 = 32개 레지스터, 45 = MMD(하위 블록)별 64K | T1 기능은 주로 C45 |
| master / slave | — | 기준 클럭을 누가 낼지 | T1은 협상 없이 고정, 둘 다 master면 링크 안 뜸 |
| FCS | Frame Check Sequence | 프레임 끝 CRC-32 | 수신 에러의 1차 지표 |
| PRBS / BER | Pseudo-Random Bit Sequence / Bit Error Rate | 의사난수 패턴으로 비트 오류율 측정 | margin을 숫자로 |
| SQI | Signal Quality Index | T1 PHY가 주는 신호 품질 등급 (보통 0–7) [추정] | FCS보다 먼저 나빠짐 |
| RFC 2544 | IETF 벤치마크 방법 | throughput·latency·frame loss·back-to-back | 성능 행의 표준 |
| TC8 / TC12 | OPEN Alliance 기술위원회 | TC8 = ECU 테스트 스펙, TC12 = 100/1000BASE-T1 PHY 적합성·IOP | "표준 테스트가 있나?" 답 |
| TSN / gPTP | Time-Sensitive Networking / generalized PTP (802.1AS) | 시간 동기·트래픽 스케줄링 | E4와 연결 |

- 기초(프레임 구조, auto-neg, C22 BMCR/BMSR 비트)는 [LP00A §4](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), 보드 측 loopback 사다리·PHY strap은 [BV10 §2.5](2026-11-01_BV10_board_interface_tests.html).

#### 0.1 관리 레지스터 — 꼭 알 것 (Linux uapi 헤더 기준 [8])

| 경로 | 레지스터 | 내용 | 검증에서 |
|---|---|---|---|
| C22 reg 0 | BMCR | reset(bit15), **loopback(bit14)**, speed, AN enable | PHY loopback 진입 |
| C22 reg 1 | BMSR | link status(bit2, **latch-low** → 두 번 읽기) | 링크 끊김 이력 |
| C22 reg **2 / 3** | **PHYSID1 / PHYSID2** | OUI + model + revision | `0xFFFF` = PHY 무응답 |
| C45 MMD 1 (PMA/PMD) reg **2100** (0x834) | BASE-T1 PMA/PMD control | bit14 (0x4000) = **master** 설정, 하위 4비트 = type (1000BASE-T1 선택 등) | master/slave 강제 |
| C45 MMD 1 reg 18 | BASE-T1 PMA/PMD extended ability | 지원 타입 | 능력 확인 |
| C45 MMD 3 (PCS) reg 2304 / 2305 | 1000BASE-T1 PCS control / status | status bit2 (0x0004) = PCS link up, bit7 = fault | PCS 단 링크 |
| C45 MMD 7 (AN) reg 512 / 513 | BASE-T1 AN control / status | (T1은 보통 AN 안 씀) | 설정 확인 |

- MMD 번호 (Linux `mdio.h`): 1 PMA/PMD · 3 PCS · 7 AN · 30/31 vendor [8]. PHY 벤더 고유 기능(PRBS, test mode, SQI, loopback 종류)은 대부분 **vendor MMD** → 레지스터 번호는 데이터시트에서만 [추정: 칩마다 다름].

### 1. 먼저 물어볼 것 (clarify)

- "Which MAC IP and which PHY — is the PHY integrated on the SoC or an external part, and which BASE-T1 speeds?" → 가정: 라이선스 MAC (GEM/EQOS 계열) + 외부 1000BASE-T1 PHY over RGMII, 일부 포트는 SGMII로 스위치에.
- "Is the switch or TSN shaping (Qbv/Qav) in our silicon, or only timestamping?" → 가정: MAC에 PTP 타임스탬프 + Qav/Qbv shaper 있음, 스위치는 외부.
- "Which master/slave assignment and link-up time requirement does the vehicle architecture have?" → 가정: SoC 쪽 master, link-up 요구 수백 ms 이하 [추정].
- "Do we need OPEN Alliance TC8/TC12 conformance results, or does the PHY vendor provide them?" → 가정: PHY 단독 적합성은 벤더, **SoC MAC + 우리 보드의 ECU 단 TC8 Layer 1–2**는 우리 [4].
- "FPGA image — does it have the MAC with a real PHY daughter card?" → 가정: MAC는 FPGA에서 RGMII로 상용 PHY 보드와 연결 가능, T1 PHY 아날로그는 실리콘/실보드에서만.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| REG | MAC 레지스터 reset 값·RW·W1C, 통계 카운터 clear 동작 | 전 레지스터 | 둘 다 | 문서와 100% 일치, 카운터 wrap/clear 문서대로 | P0 |
| REG | MDIO C22 PHYSID(reg 2/3), C45 MMD 스캔, MDC 주파수 | 주소 0–31, MDC 최소/최대 | 둘 다 | 기대 ID, 읽기 1만 회 0 오류 | P0 |
| FUNC | MAC 내부 loopback: 패턴 프레임 송수신 비교 | 64–1518 B, 랜덤 payload | 둘 다 | 1e6 프레임 FCS/payload 오류 0 | P0 |
| FUNC | PHY loopback (xMII/PCS/analog 각 단) | 100M/1G | Si (FPGA는 상용 PHY로 xMII만) | 단별 오류 0 → 고장 위치 분리 | P0 |
| FUNC | 상대 PHY와 링크 업 (forced master/slave 양쪽) | 각 역할 × 100/1000BASE-T1 | Si | 링크 업 100%, 반대 역할 쌍만 성공 | P0 |
| CFG | RGMII 지연 모드 × 속도, SGMII | rgmii/-id/-txid/-rxid × 10/100/1000 | 둘 다 | 기대 조합만 오류 0, 틀린 조합은 **실패해야** (negative) | P1 |
| CFG | 오프로드: IPv4/IPv6/TCP/UDP checksum, VLAN insert/strip, 필터(unicast/multicast/hash) | 조합 표 | 둘 다 | 기대 동작, 잘못된 checksum 프레임 플래그 | P1 |
| BND | 최소 64 B / 최대 1518·1522(VLAN) / jumbo, runt(<64)·giant(초과) | 경계 ±1 B | 둘 다 | 정상은 통과, runt/giant는 drop + 해당 카운터 +1 | P1 |
| BND | descriptor ring wrap, 버퍼 정확히 가득, 프레임이 버퍼 여러 개에 걸침 | ring 크기 2–N | 둘 다 | 데이터 일치, 누락 0 | P1 |
| INT | TX/RX 완료, coalescing(개수·타이머), 링크 변화 IRQ | coalesce 1/8/64, 타이머 | 둘 다 | IRQ 수·지연이 설정과 일치 | P1 |
| ERR | 트래픽 생성기로 bad-FCS·alignment·짧은 프레임 주입 | 비율 1e-3–1 | Si (FPGA 가능) | 100% drop + 정확한 카운터 | P0 |
| ERR | RX FIFO/DDR 지연으로 overflow 유도, DMA bus error, 링크 끊김 중 트래픽 | 강제 조건 | 둘 다 | overflow 카운터 +, 복구 후 정상, hang 0 | P1 |
| RST | MAC soft reset·PHY reset 트래픽 중, suspend/resume | 1,000 회 | Si | 복구 100%, 누수/hang 0 | P1 |
| RST | 전원 on → 링크 업 시간, (있으면) TC10 sleep/wake-up | 1,000 회 | Si | p99 < 요구치 [추정 수백 ms] | P0 |
| STRESS | 모든 포트 양방향 line rate + DDR 경합(카메라·NPU) | 64 B·IMIX | Si | drop 0 또는 정의된 우선순위대로만 | P1 |
| PERF | RFC 2544 throughput (zero loss) | 64/128/256/512/1024/1280/1518 B, trial ≥ 60 s | Si | 1518 B 100% line rate, 64 B ≥ [X]% [추정 목표] | P0 |
| PERF | RFC 2544 latency, frame loss curve, back-to-back | latency 120 s × 20회, b2b 2 s × 50회 | Si | latency p99 < [X] µs [추정], 곡선 기록 | P1 |
| PVT/margin | PRBS BER, SQI, (FEC 정정 카운트) | V × T(−40…+105/125 °C) × 케이블 길이 | Si | 3e12 bit 오류 0 (1G에서 50분/코너) | P1 |
| PVT/margin | PHY test modes(송신 파형) — 스코프로 droop·jitter·distortion·PSD | 100BASE-T1 TM1–5 [6][7] | Si (보드) | OPEN Alliance PMA 한계 내 [5] | P2 |
| SOAK | 72 h 양방향 트래픽 + 온도 사이클 | 챔버 | Si | FCS 0, 링크 drop 0, SQI 하락 없음 | P1 |
| SYS | interop: 2–3개 벤더 PHY·차량 스위치·zonal ECU | TC12 IOP 항목 참고 [3] | Si | 링크 업 100%, 처리량 동일 | P1 |
| SYS | PTP HW 타임스탬프 정확도·TSN shaper | → E4 | Si | E4 기준 | P1 |
| SYS | 케이블 진단(TDR): open/short/정상 케이블 | `ethtool --cable-test` [9] | Si | 결함 종류·거리 ±[X] m [추정] | P2 |

### 3. 우선순위 · A0 첫 주

- **P0 bring-up**:
  1. MDIO로 **PHYSID(reg 2/3)** 읽기 → `0xFFFF`면 PHY 전원·리셋·클럭·strap 주소부터.
  2. **MAC 내부 loopback** (PHY·보드 빼고 MAC+DMA만) → 1e6 프레임 비교.
  3. **PHY xMII loopback** → RGMII 지연 설정 검증 (여기서 깨지면 MAC↔PHY 구간).
  4. 상대 PHY와 **forced master/slave** 링크 업 + `iperf3`/패턴 트래픽, FCS 카운터 0.
  5. 링크 업 시간 · 링크 루프 100회.
- **P1**: 설정 조합·경계·에러 주입·RFC 2544·PRBS·interop·soak 준비. **P2**: 송신 파형 test mode(SI팀과), 케이블 진단, PVT 전 코너 지도.
- **안 할 것 (DV 몫)**: CRC-32 계산 로직 자체, 프레임 파서의 모든 필드 조합, descriptor 상태 머신 전이 coverage. **PHY 단독 적합성(PMA/PCS)** 은 PHY 벤더·OPEN Alliance 시험소 결과를 받는다 [3] — 우리는 **SoC + 우리 보드 + 우리 소프트웨어**로 된 ECU 단(TC8 Layer 1 류 [4])과 통합 이슈에 집중.

### 4. 장비 · 셋업 · 관찰 지점

```text
                    MDIO/MDC ─────────────┐
  ┌──── DUT 보드 ────┐  RGMII   ┌────────▼───┐  1쌍 T1   ┌──────────────────────────────┐
  │ SoC MAC + DMA    ├─────────►│ T1 PHY     ├══════════►│ 트래픽 생성기/분석기 (T1 media │
  │ PHC ── PPS out ──┼──┐       └────────────┘  ▲        │ converter 경유 또는 T1 포트)  │
  └──────────────────┘  │                       │        │  RFC 2544 · bad-FCS 주입      │
         UART/JTAG      │   스코프 (PPS·RGMII    │        └──────────────────────────────┘
         PDU 전원        └──► clk/data 타이밍)    └─ 채널: 케이블 길이 15/40 m, 커넥터, (챔버 안)
```

- **장비**: T1 지원 트래픽 생성기 또는 **T1↔1000BASE-T media converter** + 일반 트래픽 생성기 [추정: 랩 구성], 레퍼런스 T1 PHY 보드 2–3 벤더, 스코프(RGMII setup/hold, PHY test mode 파형 — compliance 픽스처는 SI팀), 온도 챔버, PTP grandmaster(E4).
- **카운터·로그**: MAC 통계(rx_frames, rx_crc_errors, rx_length/runt/giant, rx_fifo_overflow, tx_underrun), PHY: BMSR latch-low link, PCS link/fault, FEC corrected/uncorrected, PRBS error count, SQI.
- **Linux 도구**:

```bash
ethtool eth0                         # 속도·링크·master-slave 상태
ethtool -s eth0 master-slave forced-master   # T1 역할 강제 [9]
ethtool -S eth0                      # 드라이버 통계 카운터 (이름은 드라이버마다)
ethtool -t eth0 offline              # self-test (offline / online / external_lb) [9]
ethtool --cable-test eth0            # TDR 케이블 진단 (PHY 드라이버 지원 시) [9]
ethtool -T eth0                      # HW timestamp 능력 (→ E4)
phytool read eth0/1/0x02             # C22 PHYSID1 (phytool 설치 시) [추정]
iperf3 -c <peer> -u -b 950M -l 1472  # UDP 근사 line rate
```

- bare-metal: MAC 레지스터 직접 + MDIO 드라이버 + 패턴 프레임 생성/비교 루프 (FCS는 HW가 붙이고, 소프트웨어는 payload CRC/시퀀스 번호로 누락·순서 검사 → [V10](hmScreenPrep/problems/V10_eth_frame_fcs/index.html)).

**[실행 결과] 성능 행의 숫자** (`onsitePrep/n06/e/e_numbers.py` §1·§2, 2026-11-01 실제 출력):

```text
== 1. RFC 2544 line rate (frames/s), frame size incl. FCS ==
 size B   100M fps     1G fps  1G ns/frame  1G payload Mb/s
     64     148809    1488095          672            547.6
    128      84459     844594         1184            743.2
    256      45289     452898         2208            862.3
    512      23496     234962         4256            928.6
   1024      11973     119731         8352            963.6
   1280       9615      96153        10400            970.8
   1518       8127      81274        12304            975.3
   9018       1383      13830        72304            995.8  (jumbo, not in RFC 2544)
PASS 1G@64B = 1,488,095 fps ; 100M@1518B = 8,127 fps

== 2. zero-error run length to claim BER < target at 95% (N = -ln(0.05)/BER bits) ==
BER 1e-10  100BASE-T1             bits 2.996e+10  time      300 s
BER 1e-10  1000BASE-T1            bits 2.996e+10  time       30 s
BER 1e-12  100BASE-T1             bits 2.996e+12  time     8.32 h
BER 1e-12  1000BASE-T1            bits 2.996e+12  time     2996 s
PASS 1e-12 @ 1 Gb/s ~= 2996 s (50 min)
```

| 출력 | 의미 (면접에서 말할 것) |
|---|---|
| 64 B = 1,488,095 fps, 672 ns/frame | 소프트웨어/DMA가 프레임 하나를 **672 ns 안에** 처리해야 line rate → 작은 프레임은 **pps 한계** 시험 ([deck §7 #14](2026-10-25_rivian_hm_screen_prep.html), [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html)) |
| 64 B payload 547.6 Mb/s | line rate여도 유효 데이터율은 55% — 헤더+preamble+IFG 오버헤드 (wire 오버헤드 20 B 가정: preamble 7 + SFD 1 + IFG 12) |
| 1e-12 @ 100 Mb/s = 8.3 h | 100BASE-T1에서 "BER 1e-12" 주장은 코너당 하룻밤 → 코너 수를 줄이거나 1e-10(5분)으로 스크리닝 후 최악 코너만 장시간 |

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 플랜에서 잡는 행 |
|---|---|---|
| 링크는 up, 1G에서만 FCS 에러·손실 (100M은 OK) | **RGMII 지연을 MAC과 PHY가 둘 다 넣음/둘 다 안 넣음** | CFG RGMII 지연 매트릭스 + PHY xMII loopback |
| T1 링크가 전혀 안 뜸 | 양쪽 다 master (strap 겸용 핀이 외부 회로로 끌림) | FUNC forced master/slave 쌍 테스트 |
| 64 B에서 throughput 반토막, 1518 B는 line rate | 프레임당 IRQ, descriptor refill 느림 (pps 병목) | PERF RFC 2544 7 크기 + INT coalescing |
| 고온에서 FCS는 0인데 SQI 하락·FEC 정정 급증 | margin 소진, FEC가 가리고 있음 | PVT 행에서 SQI·FEC 카운터를 **통과 기준에 포함** |
| 몇 시간에 한 번 RX가 멈춤 | ring wrap 경계 버그, OWN bit ordering / barrier 누락 | BND ring wrap + SOAK |
| 링크 재수립 후 PTP 오프셋 점프 | 링크 재학습마다 PHY 지연(latency)이 달라짐 → 보정값 고정 | RST 링크 루프 중 E4 오프셋 기록 [추정] |
| bad-FCS 프레임이 카운터는 올라가는데 소프트웨어로 전달됨 | 필터/promiscuous 설정 조합 버그 | ERR 주입 + CFG 필터 조합 |
| MDIO 읽기가 가끔 0xFFFF | MDC가 PHY 최대 주파수 초과, turnaround 타이밍 | REG MDIO 1만 회 + MDC sweep |

### 6. 10분 영어 답 뼈대

1. "Scope first: licensed MAC with DMA, external 1000BASE-T1 PHY over RGMII, one SGMII port to a switch, PTP timestamping in the MAC; PHY standalone conformance comes from the PHY vendor."
2. "Picture: MAC behind the NoC with descriptor rings in DDR, MDIO to the PHY, RGMII data, a single twisted pair to the partner ECU."
3. "Bring-up ladder, each step isolates one segment: MDIO PHY ID registers 2 and 3; MAC internal loopback; PHY xMII loopback, which tests the RGMII delay; then a real partner with forced master and slave."
4. "Configuration matrix: RGMII delay modes by speed — including combinations that must fail, as a negative control — checksum offload, VLAN, filters."
5. "Boundaries: 64, 1518 and 1522 bytes, runts and giants must be dropped and counted; descriptor ring wrap."
6. "Errors: a traffic generator injects bad FCS and short frames; I force RX overflow and DMA errors and check every counter and recovery."
7. "Reset and power: MAC and PHY reset during traffic, suspend-resume, and link-up time from power-on as a distribution."
8. "Performance: RFC 2544 throughput at the seven standard sizes with 60-second trials, latency, frame loss curve, back-to-back. At 64 bytes gigabit is 1.49 million frames per second — 672 nanoseconds per frame — so that's the per-packet test."
9. "Margin: PRBS BER and SQI across voltage, temperature and cable length; since 1000BASE-T1 has RS-FEC, I track FEC-corrected counts, not just FCS errors."
10. "Statistics: claiming BER below 1e-12 needs 3 times 10^12 clean bits — 50 minutes at gigabit, 8 hours at 100 meg — so I screen at 1e-10 and run the long test at the worst corner."
11. "Soak 72 hours; interop with two or three PHY vendors and the real switch, using OPEN Alliance TC12 interop items as a checklist."
12. "Platforms: MAC, DMA and offloads on FPGA with a commercial PHY card; T1 analog, margin and interop only on silicon."
13. "Automation: traffic generator API, ethtool counters and PHY registers snapshotted per run into the results DB."
14. "Risks I'd attack first: RGMII timing, master-slave straps, small-frame performance."

### 7. 꼬리 질문

- Q1. "Link is up, but you see FCS errors only at 1 Gb/s. Walk me through it."

<details><summary>답</summary>

"I'd walk the loopback ladder. If MAC internal loopback is clean but PHY xMII loopback shows errors, the problem is between MAC and PHY — most often the RGMII clock-to-data delay is added twice or not at all, which only breaks at 125 MHz double-data-rate. I'd check the phy-mode setting against the PHY's internal delay strap and the board, then scope the RGMII clock and data. If the xMII loopback is clean, I move outward to the PHY analog side, SQI and the cable."
- 한국어 핵심: loopback 사다리로 구간 자르기 → MAC↔PHY면 RGMII 지연 중복/누락 1순위.

</details>

- Q2. "How would you prove the PHY has enough margin, not just that it works?"

<details><summary>답</summary>

"Push it toward failure and measure the distance. I run PRBS with the PHY's checker across temperature, supply voltage and cable length, and read SQI and FEC-corrected counts, because FEC hides errors until it runs out. The BER target sets the run length — zero errors in 3e12 bits for 1e-12 at 95%. And for transmit quality, the PHY test modes on a scope with OPEN Alliance PMA limits, together with the SI team."
- 한국어 핵심: PRBS×PVT×케이블, SQI·FEC 정정 카운트, 3e12 bit, test mode 파형은 SI팀과.

</details>

- Q3. "Throughput is line rate at 1518 bytes but half at 64. Is that a hardware bug?"

<details><summary>답</summary>

"Not necessarily — it's a per-packet limit, not a per-byte one. At 64 bytes gigabit means 1.49 million frames per second, 672 nanoseconds each. I'd check interrupt coalescing, descriptor refill rate, cache misses and CPU affinity, and use the MAC counters to see whether frames are dropped in the MAC FIFO or in software. If the hardware counter shows FIFO overflow while software keeps up, it's DMA or memory latency; otherwise it's the software path."
- 한국어 핵심: pps 한계, 672 ns/프레임, MAC FIFO 카운터로 HW vs SW 가르기.

</details>

- Q4. "Are there standard tests for automotive Ethernet you'd reuse?"

<details><summary>답</summary>

"Yes. IEEE 802.3bw and 802.3bp define the PHYs and their transmitter test modes, and the OPEN Alliance publishes PMA, PCS, PHY-control and interoperability test suites for 100 and 1000BASE-T1 in TC12, plus ECU-level test specifications in TC8. I wouldn't redo PHY conformance the vendor already passed, but I'd use those test lists as a checklist for our ECU — our MAC, our board, our software."
- 한국어 핵심: 802.3bw/bp + OPEN Alliance TC12(PHY 적합성·IOP) · TC8(ECU) → 체크리스트로 재사용.

</details>

- Q5. "Why did automotive go to single-pair, and what does that change for validation?"

<details><summary>답</summary>

"Weight, cost and connector count — one unshielded pair instead of four. It changes three things for validation: no auto-negotiation, so master-slave is fixed and a wrong strap means no link; link-up time becomes a requirement; and the channel is harsh — temperature range, EMC — so margin across temperature and cable length matters more than in an office."
- 한국어 핵심: 1쌍 = 무게·비용 → master/slave 고정, link-up 시간 요구, 혹독한 채널 → margin 중시.

</details>

### 8. Don 연결

- "I haven't validated an Ethernet MAC directly, but the method is the one I used for PCIe on SSDs: a loopback ladder to isolate segments, PRBS and counters for margin, statistics for the run length, and chamber sweeps. At Apple I root-caused interface issues with a DSO and protocol analyzer, which is how I'd attack an RGMII timing problem. [숫자 채우기: 루트코즈 건수]"

## E3 · MIPI CSI-2 receiver + ISP 카메라 파이프라인 ★

### 0. 🔰 이 블록이 뭔가

- **비유 (컨베이어 벨트 + 사진 현상소)**: 카메라 센서는 사진을 **한 줄씩** 잘라서 컨베이어(CSI-2)에 올린다. 각 줄 상자에는 **라벨**(헤더: 어느 카메라 = VC, 무슨 형식 = DT, 몇 바이트 = WC)이 붙고, 라벨 오타는 **ECC**가 고치고, 상자 내용물 파손은 **CRC**가 알려 준다. 사진 한 장의 시작/끝엔 **"프레임 시작(FS)" / "프레임 끝(FE)" 팻말**. SoC 쪽 **CSI-2 수신기(RX)** 가 상자를 풀어 DDR에 쌓고, **ISP** 는 "현상소" — 센서 원본(RAW, 색 모자이크)을 사람이/NPU가 쓸 수 있는 컬러 영상으로 만든다.
- **도로(물리층) = D-PHY**: 데이터 선 1–4쌍 + 클럭 선 1쌍, 센서 → SoC 한 방향. **두 가지 모드**: 줄을 보낼 땐 빠른 **HS**(작은 전압 차동, 수 Gb/s), 쉬는 동안엔 느린 **LP**(1.2 V 단일 종단, 제어 신호). LP→HS로 바뀌는 순간 수신기가 잠깐 귀를 막는 시간 = **HS settle** — 이 값이 틀리면 첫 바이트를 놓친다 (단골 버그).
- **차량에서는**: 센서와 SoC가 수 m 떨어져 있어 D-PHY(수십 cm)로는 못 감 → 카메라 쪽 **serializer** + SoC 보드 쪽 **deserializer**(GMSL/FPD-Link, 또는 표준 MIPI A-PHY)가 동축 1가닥으로 연결하고, deserializer가 카메라 4대를 **VC 0–3**으로 묶어 CSI-2 포트 하나로 넣는다 [추정: 일반적 구성, Rivian 부품 미확인].
- **정확한 정의** [확인됨 [11]]: MIPI CSI-2 최신 v4.2 (2025-12), 최대 **32 VC**, PHY = C-PHY · D-PHY (짧은 거리) · A-PHY (최대 15 m). 기초 패킷 구조는 [LP00A §5.3](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html).

```text
 카메라 모듈 (×N)                         SoC
 ┌───────────┐  coax ┌──────────┐ CSI-2  ┌──────────────────────────────────────────────────────────────┐
 │ 센서 ─ SER ├══════►│ DESER    ├═D-PHY═►│ D-PHY RX ─► CSI-2 RX ctrl ─► (VC/DT 라우팅) ─► ISP ─► DDR ─► NPU │
 └───▲───────┘       │ VC0..3   │ 4 lane │ (lane     (헤더 ECC, CRC,   │ line buffer   BLC→demosaic  │       │
     │ I2C(CCI) 터널  └──▲───────┘        │  deskew,   FS/FE 짝, WC,    │ 에러 IRQ      →AWB→CCM→      ▼       │
     └─────────────────┘ I2C             │  settle)   frame/line 카운트)│               gamma→NR→YUV   encoder │
       FSYNC (트리거) ◄──── PHC 주기 출력 (E4) ── SoF timestamp latch (E4) ── GIC IRQ ── DMA ── NoC QoS        │
                                         └──────────────────────────────────────────────────────────────┘
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| CSI-2 | Camera Serial Interface 2 | 센서 → SoC 영상 프로토콜 | 이 블록 |
| D-PHY / C-PHY | MIPI 물리층 | 차동쌍 레인 / 3선 trio | 속도·레인 매트릭스 |
| HS / LP | High-Speed / Low-Power | 데이터 모드 / 제어·휴식 모드 | 전환 타이밍 버그 |
| THS-SETTLE | HS settle time | LP→HS 전환 직후 수신기가 무시하는 시간, 85 ns+6 UI ~ 145 ns+10 UI [14] | 설정 잘못 = 첫 바이트 오류 |
| SoT / sync byte | Start of Transmission | HS 시작을 알리는 동기 패턴 [추정: 흔히 0xB8로 표기] | SoT 에러 카운터 |
| VC | Virtual Channel | 한 링크 안 논리 스트림 번호 (카메라 구분) | 다중 카메라 |
| DT | Data Type | RAW8/10/12, YUV, embedded data… | unpack 로직 |
| WC | Word Count | long packet payload 바이트 수 (= 한 줄) | 불일치 = 에러 |
| ECC | Error Correcting Code (헤더) | 1-bit 정정, 2-bit 검출 | **3-bit은 오정정 가능** (§4 실행) |
| CRC-16 | payload 검사 | 줄 내용 파손 검출 (정정 X, 재전송 X) | 프레임 drop 원인 |
| FS / FE / LS / LE | Frame/Line Start/End | short packet 팻말 (frame 번호 포함) | drop·순서 검출 |
| TPG | Test Pattern Generator | 센서·RX·deserializer 안의 패턴 생성기 | 알려진 입력 = golden 비교 |
| ISP | Image Signal Processor | RAW → 컬러 영상 현상소 | 단계별 golden |
| SER / DESER | Serializer / Deserializer (GMSL, FPD-Link, A-PHY) | 긴 케이블용 변환기 | lock·에러 카운터 |
| FSYNC | Frame sync trigger | 여러 카메라를 같은 순간 노출시키는 펄스 | E4 동기 |

#### 0.1 에러 종류 — 실제 공개 IP의 예 (AMD MIPI CSI-2 RX Subsystem PG232 interrupt status [12])

| 계층 | 에러 비트 (PG232 이름) | 뜻 | 테스트에서 주입하는 법 |
|---|---|---|---|
| PHY | SoT error / SoT sync error (ErrSoTHS, ErrSotSyncHS) | HS 시작 패턴 이상 / 동기 완전 실패 | HS settle 잘못 설정, 패턴 생성기의 SoT 변조 |
| PHY | Stop state, Incorrect lane configuration | 레인이 정지 상태 / 활성 레인 > 설정 | 레인 수 불일치 설정 |
| 패킷 | ECC 1-bit (corrected) / ECC 2-bit | 헤더 1비트 정정됨 / 정정 불가 | 패턴 생성기로 헤더 비트 flip |
| 패킷 | CRC error (ErrCrc) | payload CRC 불일치 | payload 비트 flip |
| 패킷 | Unsupported Data Type (ErrID) | 모르는 DT | 미지원 DT 송신 |
| 패킷 | Word Count corruption | 헤더 WC보다 적게 들어옴 | 짧은 패킷 |
| 프레임 | Frame sync error (VCn) | FE가 FS와 짝이 안 맞음 | FS 생략 |
| 프레임 | Frame level error (VCn) | FS–FE 사이 CRC 에러 있었음 | 프레임 중간 CRC 에러 |
| 버퍼 | Stream line buffer full | 하류(ISP/DMA)가 못 따라감 | DDR 경합·낮은 클럭 |

- C-PHY 모드에선 헤더를 ECC 대신 **여러 사본 + CRC**로 보호 (같은 비트가 "all header CRC copies fail" 의미로 재사용) [12]. CSI-2 v2.0+ VCX(VC 확장)를 쓰면 VC 4비트(16개)·ECC 6비트 [13] — 정확한 비트 배치는 스펙 확인 필요 [추정].
- 다른 벤더 IP는 이름이 다르지만 **계층(PHY / 패킷 / 프레임 / 버퍼)** 은 같다 → 매트릭스의 ERR 행을 이 4계층으로 짠다.

### 1. 먼저 물어볼 것 (clarify)

- "Is the CSI-2 receiver licensed IP with its own D-PHY, and which PHY modes — D-PHY only or C-PHY too, which version and max lane rate?" → 가정: 라이선스 RX + D-PHY v2.x급, 4 lane × 최대 2.5 Gb/s, 포트 여러 개 [추정].
- "How many cameras per port and how are they aggregated — virtual channels through a deserializer?" → 가정: 포트당 deserializer 1개, 카메라 4대 VC0–3, 총 카메라 8–11대 [추정].
- "Is there a test pattern generator in the receiver or the deserializer, and does the ISP have a memory-to-memory mode?" → 가정: 둘 다 있음 → 센서 없이 RX와 ISP를 따로 검증 가능.
- "Is there a bit-exact C model of the ISP from the architecture team?" → 가정: 있음 (단계별 on/off 가능).
- "What does 'frame timestamp' mean in our system — start of exposure, FSYNC, or frame start at the receiver?" → 가정: SoF at RX에 PHC 시간 latch (E4).
- "What's the latency and drop budget — zero drops at full load?" → 가정: 정상 부하에서 drop 0, 센서→DDR latency 한 줄 시간 수준 [추정].

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| REG | RX·D-PHY·ISP 레지스터 reset 값, 에러 상태 W1C, 카운터 | 전 레지스터 | 둘 다 | 문서 100% 일치 | P0 |
| FUNC | RX 내부 TPG(또는 deserializer TPG) → RX → DDR, **bit-exact** | 1080p RAW12 | 둘 다 | 1만 프레임 픽셀 불일치 0 | P0 |
| FUNC | 실제 센서 test pattern(color bar/ramp) → DDR golden 비교 | 센서 1대 | Si (FPGA는 센서 보드 있으면) | 프레임 불일치 0 | P0 |
| CFG | 레인 수 × 레인 속도 × DT 매트릭스 | 1/2/4 lane × 80 Mb/s–최대 × RAW8/10/12/YUV422/embedded | 둘 다 (속도 상한은 Si) | 모든 칸 bit-exact | P1 |
| CFG | VC 0–3 (+ VCX 지원 시 4–15) 라우팅, VC별 DT 다르게 | 카메라 4대 혼합 | 둘 다 | VC별 프레임이 올바른 버퍼로, 섞임 0 | P0 |
| CFG | continuous vs non-continuous clock, LS/LE 있음/없음 | 각 조합 | 둘 다 | 수신 정상 | P1 |
| BND | 최소/최대 폭·높이, WC가 packing과 안 맞는 폭(RAW10 폭 4의 배수 아님), 최대 프레임 크기 | 경계 ±1 | 둘 다 | 정상은 통과, 규칙 위반은 정의된 에러 | P1 |
| INT | FS/FE/line-count IRQ, 에러 IRQ, IRQ mask | 30/60 fps | 둘 다 | IRQ 수 = 프레임 수, 누락 0 | P0 |
| ERR | 패턴 생성기로 ECC 1-bit / 2-bit / **3-bit**, CRC, 미지원 DT, WC 오류, FS 누락, 레인 수 불일치 주입 | 각 1,000회 | 둘 다 (PHY 계층은 Si) | 1-bit = 정정+카운트, 2-bit = drop+카운트, CRC = 해당 줄/프레임 표시, **다음 FS에서 자동 복구** | P0 |
| RST | stream on/off 반복, 프레임 중간 RX reset, 센서/링크 끊김 후 재연결 | 1만 회 | Si | 다음 프레임부터 정상, hang 0 | P1 |
| RST | 저전력(클럭 게이팅) 진입·탈출, suspend/resume | 1,000 회 | Si | 첫 프레임 정상 | P2 |
| STRESS | 모든 카메라 최대 rate + NPU·인코더 DDR 경합 | 8–11 카메라 | Si | **drop 0**, line buffer full 0 | P0 |
| STRESS | DDR 대역폭 일부러 줄이기(QoS 낮춤) → 넘침 거동 | 단계별 | Si | 정의된 drop + 카운터, 다른 VC 영향 없음 | P1 |
| PERF | 센서 SoF → 프레임 DDR 도착 latency, ISP throughput (pixel/clock) | 해상도 × fps | Si | p99 < [X] ms [추정 예산] | P1 |
| PVT/margin | HS settle sweep, 레인 속도 sweep, (deskew 켬/끔) at V × T 코너 | 코너별 | Si | 설정값 양쪽으로 margin ≥ [X] step [추정], 에러 0 | P1 |
| PVT/margin | 장시간 패턴 수신 BER | 4 lane × 2.5G = 10 Gb/s → 1e-12에 300 s | Si | CRC·ECC 에러 0 / 3e12 bit | P1 |
| ISP | 단계별 golden: RAW를 DDR에서 ISP로(mem-to-mem) → C 모델과 bit-exact | 단계 on/off × 해상도 × 비트 깊이 | 둘 다 | 불일치 0 (또는 허용 오차 정의) | P0 |
| ISP | 랜덤·극단 입력(포화, 0, 검은 레벨 근처, 고립 핫픽셀), 타일/줄 경계 | seed 기록 | 둘 다 | bit-exact | P1 |
| SOAK | 24 h 전 카메라 스트리밍 + 온도 사이클 | 챔버 | Si | drop 0, 에러 0, frame 번호 연속 | P1 |
| SYS | 실제 센서 + SER/DESER: lock, 역방향 I2C, 케이블 길이별 에러 | 케이블 짧음/최대 | Si | lock 유지, 에러 0 | P1 |
| SYS | Linux V4L2 스택에서 캡처 (media-ctl 파이프라인) | 실제 드라이버 | Si | 300 프레임 fps·drop 정상 | P1 |
| SYS | 프레임 timestamp·다중 카메라 skew | → E4 | Si | E4 기준 | P1 |

### 3. 우선순위 · A0 첫 주

- **P0 bring-up**:
  1. 센서/deserializer **I2C 응답**(chip ID) → deserializer **lock** 상태 ([BV10 §2.7](2026-11-01_BV10_board_interface_tests.html)).
  2. **RX 또는 deserializer TPG → DDR bit-exact** (센서·렌즈·조명 변수 제거).
  3. 1 lane 저속 → 4 lane 목표 속도, 에러 카운터 0 확인.
  4. 센서 test pattern 한 대 → VC 4개 동시.
  5. 프레임 카운터·FS/FE IRQ 수 = DMA 완료 수.
- **P1**: 에러 주입 전 계층, ISP 단계별 golden, 전 카메라 동시 + DDR 경합, PVT settle sweep, soak 준비. **P2**: 저전력, VCX, 이미지 품질 튜닝 지원.
- **안 할 것**: 헤더 ECC/CRC 계산기·패킷 파서 상태 전이 coverage는 **DV**, 이미지 품질(색·노이즈 "좋아 보이나")은 **카메라 튜닝 팀** — validation은 "**설계 의도대로 bit-exact인가 + 실제 링크에서 에러 0인가**".

### 4. 장비 · 셋업 · 관찰 지점

```text
  (A) 센서 없이 RX만          (B) 실제 체인                     (C) ISP만
  MIPI 패턴 생성기 ─D-PHY─► RX   센서 ─SER═coax═DESER─D-PHY─► RX     DDR(RAW 파일) ─► ISP(mem2mem) ─► DDR
  (에러 주입, settle·skew 조절)   (I2C 터널, FSYNC, 케이블 길이)      C 모델 출력과 bit-exact diff
                 ▲ 스코프/D-PHY 프로토콜 분석기로 LP→HS 전환 캡처
```

- **장비**: MIPI D-PHY 패턴 생성기/분석기(에러·타이밍 변조 주입) [추정: 상용 장비], deserializer 평가 보드(TPG 내장), 실제 카메라 모듈 + 케이블 세트, 스코프(D-PHY LP/HS 전환), 온도 챔버, LED 스트로브(E4).
- **관찰**: RX 에러 상태/카운터(§0.1 계층별), frame/line 카운터, FS frame 번호 연속성, line buffer full, DMA 완료 수, ISP 단계별 출력 CRC, DESER lock·에러 카운터.
- **Linux**: `media-ctl -p`(파이프라인 그래프), `v4l2-ctl --stream-mmap --stream-count=300`(fps·drop), `v4l2-ctl --set-ctrl test_pattern=1` → [LP00A §5.6](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [BV10 §3.5](2026-11-01_BV10_board_interface_tests.html). 패킷 수준 검사기 코드 → [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html).

**[실행 결과 1] 링크·DDR 예산** (`e_numbers.py` §3, 예시 구성, 블랭킹+프로토콜 오버헤드 20%는 가정):

```text
1x 8MP 3840x2160 RAW12 30fps
   payload 2.99 Gb/s, with overhead 3.58 Gb/s, DDR write (16b/px) 0.50 GB/s
   4 lanes x 1.5 Gb/s =  6.0 Gb/s  util  59.7%  OK
   4 lanes x 2.5 Gb/s = 10.0 Gb/s  util  35.8%  OK
4x 3MP 1920x1536 RAW12 30fps (VC0-3 on one port)
   payload 4.25 Gb/s, with overhead 5.10 Gb/s, DDR write (16b/px) 0.71 GB/s
   4 lanes x 1.5 Gb/s =  6.0 Gb/s  util  84.9%  TIGHT
   4 lanes x 2.5 Gb/s = 10.0 Gb/s  util  51.0%  OK
4x 2MP 1920x1080 RAW10 60fps (VC0-3)
   payload 4.98 Gb/s, with overhead 5.97 Gb/s, DDR write (16b/px) 1.00 GB/s
   4 lanes x 1.5 Gb/s =  6.0 Gb/s  util  99.5%  TIGHT
PASS Linux pixel_rate(750 MHz, 4 lanes, 12 bpp) = 500 Mpix/s
   WC RAW10 width 1920 = 2400 B
   WC RAW12 width 1920 = 2880 B
```

- 해석: 카메라 4대를 VC로 한 포트에 묶으면 **링크 사용률 85–100%** 가 쉽게 나온다 → STRESS 행은 "가장 빡빡한 실제 구성"으로 정한다. DDR 쪽은 RAW12를 16비트로 풀어 쓰면 4대 × 3MP × 30 fps = 0.71 GB/s — NPU 트래픽과 겹치면 line buffer full이 나는 지점을 찾는 게 STRESS의 목적.

**[실행 결과 2] 에러 검출 능력 — "test of the test"** (`csi2_err_inject.py`, 헤더 30비트의 모든 1/2/3-bit 패턴 전수, payload는 linear syndrome으로 20만 회씩, seed 2026):

```text
== CSI-2 header ECC sanity ==
ecc(0x000A2B) = 0x2E  expect 0x2E  PASS
ecc(0x000100) = 0x1A  expect 0x1A  PASS
ecc(0x09602B) = 0x1D  expect 0x1D  PASS

== header error injection on DI/WC 0x0B402C (all patterns over 30 bits) ==
1-bit errors:    30 patterns  corrected_right=30  detected=0  MISCORRECTED=0  undetected=0
2-bit errors:   435 patterns  corrected_right=0  detected=435  MISCORRECTED=0  undetected=0
3-bit errors:  4060 patterns  corrected_right=0  detected=280  MISCORRECTED=3780  undetected=0
PASS all 1-bit corrected: True   PASS all 2-bit detected: True
3-bit: 3780/4060 = 93.1% silently MISCORRECTED -> header looks valid but VC/DT/WC is wrong

== payload CRC-16 injection, 2880-byte RAW12 line, 200000 trials per class, seed 2026 ==
1-bit         undetected     0/200000
2-bit         undetected     0/200000
3-bit         undetected     0/200000
burst<=16     undetected     0/200000
burst 17-32   undetected     5/200000
4-bit random  undetected     2/200000
PASS guaranteed classes (1/2/3-bit, burst<=16) all detected: True
model cross-check vs real CRC on 200 random 3-bit errors: PASS
```

| 출력 | 의미 → 플랜에 반영 |
|---|---|
| 1-bit 30/30 정정, 2-bit 435/435 검출 | SEC-DED 보장 범위 → ERR 행 통과 기준 "1-bit = 정정+카운트, 2-bit = drop+카운트"는 **반드시** 성립해야 함 |
| **3-bit 93.1% 오정정** | 6비트 신드롬 중 홀수 무게 값이 거의 다 "1비트 정정"으로 해석됨 → 헤더 3비트 오류는 **틀린 VC/DT/WC가 정상처럼 보임** → WC 범위 검사·FS/FE 짝·frame 번호 연속성이 **두 번째 방어선**이어야 하고, 플랜은 3-bit 주입으로 그 방어선을 시험한다 |
| CRC: 1/2/3-bit·16비트 이하 burst 0 누락 | CRC-16 (x^16+x^12+x^5+1)이 보장하는 클래스 — 주입 테스트에서 하나라도 놓치면 RX 버그 |
| 4-bit 무작위·긴 burst 2–5/20만 누락 | ≈2^-16(1.5e-5)의 이론 누락률과 같은 크기 → 링크 BER이 높으면 "CRC 통과한 틀린 줄"이 생김 → margin(PVT 행)이 중요한 이유 |

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 플랜에서 잡는 행 |
|---|---|---|
| 가끔 프레임 drop + ECC/SoT 에러 ([deck §7 #8](2026-10-25_rivian_hm_screen_prep.html)) | HS settle 설정이 레인 속도와 안 맞음, 레인 skew, deskew 미사용 | PVT settle sweep, CFG 속도 매트릭스 |
| TPG는 완벽, 실제 센서만 가끔 깨짐 | 센서 쪽 LP-11 진입 타이밍, continuous clock 설정 불일치 | CFG clock 모드, SYS 실제 센서 |
| 카메라 4대 중 하나가 다른 버퍼에 섞임 | VC 라우팅 테이블·VCX 비트 처리 버그 | CFG VC 라우팅, 3-bit 헤더 오류 주입 |
| 1919 폭 RAW10에서 hang | WC가 packing 규칙과 안 맞을 때 처리 정의 안 됨 | BND 비정상 폭 |
| 모든 카메라 + NPU 돌릴 때만 줄 단위 깨짐 | line buffer overflow (DDR QoS), 에러 비트를 아무도 안 봄 | STRESS + line buffer full 카운터를 통과 기준에 |
| 프레임 중 reset 후 영원히 프레임 안 옴 | RX가 FS를 기다리지 않고 중간 패킷에서 동기 잃음 | RST 프레임 중간 reset |
| ISP 출력이 마지막 타일/열만 1 LSB 다름 | 반올림·경계 처리, partial tile | ISP 단계별 golden, 경계 입력 |
| 고온에서만 deserializer lock 풀림 | 케이블·PoC 전원·SerDes margin | SYS 케이블 길이 × PVT |

### 6. 10분 영어 답 뼈대

1. "Scope: licensed CSI-2 receiver with D-PHY, four lanes, up to 2.5 gigabits per lane, four cameras per port as virtual channels through a deserializer, then the ISP; there's a TPG and a bit-exact ISP C model."
2. "Picture: sensor, serializer, coax, deserializer, D-PHY into the receiver, VC and data-type routing, ISP, DDR, NPU; I2C tunnelled for sensor control; FSYNC and frame timestamps from the PTP clock."
3. "Strategy: separate the receiver from the ISP, and start from known inputs. The receiver's or deserializer's test pattern into DDR, compared bit-exactly, removes the sensor, lens and lighting."
4. "Configuration matrix: lane count by lane rate by data type — RAW8, 10, 12, YUV, embedded data — and VC routing with different types per VC."
5. "Boundaries: min and max sizes, widths that break the RAW10 packing rule, max frame."
6. "Errors at four layers — PHY, packet, frame, buffer: with a pattern generator I inject 1-, 2- and 3-bit header errors, CRC errors, unsupported data types, word-count errors and a missing frame start. One-bit must be corrected and counted, two-bit dropped, and the receiver must resync at the next frame start."
7. "Why 3-bit: SEC-DED miscorrects most 3-bit errors — I ran it, 93 percent — so the word-count and frame-structure checks are the second line of defence and need their own tests."
8. "ISP: memory-to-memory, RAW from DDR, each stage compared to the C model, with saturated, zero and boundary inputs."
9. "Stress: every camera at full rate with NPU and encoder traffic on DDR — zero dropped frames and zero line-buffer overflows; four 3-megapixel cameras on one port already use about 85 percent of a 6-gigabit link."
10. "Margin: HS-settle and lane-rate sweeps at voltage and temperature corners; at 10 gigabits aggregate, 3e12 clean bits — five minutes — shows BER below 1e-12."
11. "Soak 24 hours with frame numbers continuous; real sensors with the longest cable."
12. "Platforms: TPG, VC routing and ISP golden on FPGA; D-PHY timing, margin and real cameras on silicon."
13. "Automation: frame and error counters plus per-frame CRCs logged per run; failing frames dumped."
14. "First week: I2C and deserializer lock, TPG bit-exact at one lane, then four lanes, then one real camera, then four VCs."

### 7. 꼬리 질문

- Q1. "Frames drop occasionally and you see ECC errors in the log. How do you debug?"

<details><summary>답</summary>

"First classify: are they corrected single-bit or uncorrectable, which lane and camera, and do they correlate with temperature or lane rate? ECC errors point to the physical layer — HS settle not matching the lane rate, lane skew, or a marginal cable. I'd sweep the settle setting to find both edges of the passing window, and compare with the test pattern generator, which removes the sensor. If ECC is clean but frames still drop, it's downstream — line buffer overflow from DDR bandwidth — and the overflow counter tells me."
- 한국어 핵심: 1-bit/2-bit·lane·조건 분류 → settle sweep으로 창 양 끝 → TPG로 센서 분리 → ECC 깨끗하면 하류(line buffer).

</details>

- Q2. "How do you validate the ISP without judging image quality by eye?"

<details><summary>답</summary>

"Bit-exactness against the architecture team's C model, stage by stage. I run the ISP memory-to-memory with RAW frames from DDR — real captures plus synthetic corner inputs like saturated pixels, black level, isolated hot pixels and tile boundaries — and enable stages one at a time so a mismatch points to one block. Image quality tuning is a different team; validation answers 'does silicon match the design'."
- 한국어 핵심: C 모델 bit-exact, mem-to-mem, 단계별 on/off, 극단 입력 — 화질은 튜닝팀.

</details>

- Q3. "You don't have the real cameras yet. What can you do?"

<details><summary>답</summary>

"Most of the plan. The receiver's or deserializer's test pattern generator, or a MIPI pattern generator instrument, gives known packets including deliberate errors; the ISP runs from memory; and on FPGA I can run VC routing and DMA with a model of the sensor. What I can't do without the real chain is cable margin and real sensor timing, so those stay as silicon items."
- 한국어 핵심: TPG/패턴 생성기 + ISP mem2mem + FPGA 모델 → 실 카메라 필요한 건 케이블 margin·센서 타이밍만.

</details>

- Q4. "How many cameras can one port take?"

<details><summary>답</summary>

"Do the budget: pixels times bits per pixel times frame rate, plus blanking and protocol overhead — I assume about 20 percent until I have the sensor timing. Four 3-megapixel RAW12 cameras at 30 fps is 4.25 gigabits of payload, about 5.1 with overhead — 85 percent of four lanes at 1.5 gigabits, 51 percent at 2.5. Then DDR: unpacked to 16 bits, that's 0.7 gigabytes per second just to write. I test at the tightest real configuration."
- 한국어 핵심: 픽셀×bpp×fps×(1+오버헤드) vs lane×rate, DDR 쓰기까지 → 가장 빡빡한 구성으로 STRESS.

</details>

- Q5. "Your ECC test passes. Are you sure the receiver handles header corruption?"

<details><summary>답</summary>

"Only for one and two bits — that's all SEC-DED guarantees. With three bit errors, a Hamming SEC-DED code mostly miscorrects to a different valid header — I enumerated all 4,060 patterns and 93 percent miscorrected. So I also inject 3-bit errors and check that the word-count range check, the frame start and end pairing, and the frame-number continuity catch them, and that a wrong VC can't leak data into another camera's buffer."
- 한국어 핵심: SEC-DED 보장 = 1·2비트뿐, 3비트 93% 오정정 → WC·FS/FE·frame 번호·VC 격리 시험.

</details>

### 8. Don 연결

- "MIPI isn't new to me at the protocol level — at Apple I debugged RFFE and SPMI, which are MIPI specs, with protocol analyzers, reading parity and command frames. CSI-2 is much faster and streams video, so I'd lean on pattern generators and bit-exact comparison the same way I validated NAND and PCIe data paths with known patterns. [숫자 채우기: RFFE/SPMI 이슈 건수]"

## E4 · 시간 동기 · 센서 timestamp (PTP/gPTP · PPS · FSYNC · 카메라-LiDAR 정렬)

### 0. 🔰 이 블록이 뭔가

- **비유 (합창단의 지휘자와 메트로놈)**: 차 안의 컴퓨터·카메라·LiDAR·레이더는 각자 **손목시계**(오실레이터)를 차고 있는데, 시계마다 조금씩 빠르거나 느리다(ppm). **PTP** = 지휘자(grandmaster)가 "지금 몇 시"를 네트워크로 계속 알려 주고, 각자 **편지가 오가는 데 걸린 시간**을 재서 빼는 방식. **PPS** = GPS가 매 초 정각에 치는 **종소리**. **FSYNC** = "지금 다 같이 셔터 눌러!" 신호. 센서 퓨전은 "같은 순간의 데이터"를 묶어야 하므로, 시계가 틀리면 **차가 30 m/s로 달릴 때 1 ms 오차 = 3 cm 어긋남**.
- **정확한 정의**:
  - **IEEE 1588 (PTP)**: 메시지 교환 4개 타임스탬프 t1(master 송신) t2(slave 수신) t3(slave 송신) t4(master 수신)로 **offset = ((t2−t1) − (t4−t3))/2**, **delay = ((t2−t1) + (t4−t3))/2**. 가정: **왕복 경로가 대칭**. 비대칭이면 offset 오차 = 비대칭/2 (§4 실행으로 확인).
  - **IEEE 802.1AS (gPTP)**: 1588의 Ethernet/TSN 프로파일. peer-to-peer delay 측정, 링크마다 rate ratio 보정. 목표 "7 hop 이내 1 µs" (AVB 시절 요구에서 유래) [16].
  - **자동차 프로파일**: grandmaster를 **고정**(BMCA 안 씀), 빠른 시작 — AUTOSAR Time Sync, IEEE 802.1DG [17][18].
  - **HW timestamp**: MAC 또는 PHY가 PTP 이벤트 메시지가 지나가는 순간 **PHC**(PTP Hardware Clock) 값을 찍음. SW timestamp는 IRQ 지연으로 µs 단위 흔들림 → 검증 대상은 HW 경로. Linux: `SO_TIMESTAMPING`(TX/RX_HARDWARE), `ethtool -T`로 능력 확인 [15].
  - **면접관 관련 [확인됨: deck §1.2]**: 센서 데이터 timestamping, 센서 동기화의 온라인 검증, compute node 간 clock sync, 다중 카메라 스트림 결합 특허 — 이 블록 질문은 **깊게** 나올 수 있다.

```text
   GPS ── PPS + 시각(NMEA) ──┐
                             ▼
  ┌──────────── SoC ───────────────────────────────────────────────┐   gPTP (802.1AS) over T1 Ethernet
  │ PHC (MAC 내 PTP 카운터, ns)  ◄── servo (ptp4l/phc2sys 또는 FW) ◄─┼──── Sync/Follow_Up/Pdelay ────► zonal ECU, LiDAR, 레이더
  │   ├ PPS in capture (ts2phc)                                     │
  │   ├ periodic out ─► FSYNC ──────────────────────────────────────┼──► 카메라 (노출 시작)
  │   ├ event latch ◄── CSI-2 RX SoF ◄──── 카메라 프레임 (E3)        │
  │   └ cross-timestamp ─► CPU generic timer / NPU 타임베이스        │
  └─────────────────────────────────────────────────────────────────┘
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| PTP / gPTP | Precision Time Protocol / generalized PTP | 네트워크 시계 맞추기 | 센서 퓨전 시간축 |
| PHC | PTP Hardware Clock | MAC/PHY 안 하드웨어 시계 | timestamp의 기준 |
| grandmaster (GM) | — | 기준 시계 | 자동차는 고정 |
| offset / path delay | — | 시계 차이 / 전송 지연 | 대칭 가정 |
| asymmetry | 경로 비대칭 | 가는 길 ≠ 오는 길 | 오차 = asym/2, 프로토콜이 못 봄 |
| ppm | parts per million | 시계 빠르기 오차 | 100 ppm = 초당 100 µs |
| servo | 제어 루프 | 시계를 조금씩 당기는 PI 제어 | 수렴 시간·jitter |
| holdover | — | GM 잃었을 때 혼자 버티기 | drift 한계 |
| PPS | Pulse Per Second | 초 정각 펄스 | 외부 기준, 스코프로 비교 |
| FSYNC | Frame sync | 카메라 동시 노출 트리거 | 다중 카메라 skew |
| SoF timestamp | Start-of-Frame | 프레임 시작 순간 시각 | "어느 순간인가" 정의 필요 |
| rolling shutter | — | 줄마다 노출 시각이 다름 | 위아래 수십 ms 차이 |
| TAI / UTC / GPS time | — | 시간 척도, TAI−UTC 37 s, GPS−UTC 18 s | 1초·18초 어긋남 버그 ([deck §7 #13](2026-10-25_rivian_hm_screen_prep.html)) |

### 1. 먼저 물어볼 것 (clarify)

- "What's the time-accuracy requirement between sensors — for example camera-to-LiDAR within a microsecond, or a millisecond?" → 가정: 노드 간 PHC < 1 µs, 카메라 간 노출 skew < 수십 µs [추정].
- "Where is the timestamp taken — MAC, PHY, or both — and at which point of the camera frame?" → 가정: MAC timestamp + CSI-2 RX SoF latch, 노출 시작은 FSYNC 기준으로 보정.
- "Who is grandmaster — our SoC, a central gateway, GPS? Static or BMCA?" → 가정: 고정 GM (자동차 프로파일), GPS PPS로 discipline.
- "Which clocks have to agree — PHC, the CPU's timer, the NPU, the ISP?" → 가정: PHC가 기준, 나머지는 cross-timestamp로 매핑.
- "Is the servo in Linux (ptp4l/phc2sys) or in firmware on a safety core?" → 가정: 둘 다 시나리오 존재 [추정].

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| REG | PHC 카운터 읽기(hi/lo latch로 tearing 없음), 시간 설정·주파수 조정 레지스터 | 1e6 연속 읽기 | 둘 다 | 단조 증가, tearing 0, 조정 해상도 문서대로 | P0 |
| FUNC | PTP 이벤트 메시지 HW timestamp TX/RX | Sync, Delay_Req, Pdelay (L2 0x88F7 / UDP) | Si (FPGA 가능) | 모든 이벤트 메시지에 timestamp, 누락 0 | P0 |
| FUNC | gPTP slave로 GM에 lock | 자동차 프로파일 (고정 GM) | Si | 수렴 후 offset < 목표 [추정 100 ns] | P0 |
| CFG | rx filter 모드(PTP v2 L2/L4 event, ALL), one-step vs two-step | 모드별 | 둘 다 | 설정대로만 timestamp [15] | P1 |
| BND | ns 카운터 wrap(초 경계), 32-bit 필드 wrap, 큰 시간 점프(step) | 경계 시각 직전·직후 | 둘 다 | 계산 오류 0, 점프 후 재수렴 | P1 |
| INT | PPS in capture IRQ, periodic out, event latch IRQ | 1 Hz–kHz | 둘 다 | IRQ 누락 0 | P1 |
| ERR | GM 끊김(holdover), 메시지 손실·중복·순서 뒤바뀜, 잘못된 domain | 주입 | Si | holdover drift < [X] µs/분 [추정], 재연결 시 수렴 | P1 |
| RST | 링크 재수립 후 offset (PHY latency가 링크마다 다름), MAC reset, suspend/resume | 1,000 회 | Si | 재수렴 시간 p99 < [X] s, 오프셋 점프가 기준 내 | P1 |
| PERF | **timestamp 정확도 절대값**: PPS out(DUT) vs GM PPS를 스코프로 | 1 h 통계 | Si | 평균·표준편차·max < 목표 | P0 |
| PERF | servo 수렴 시간, 정상 상태 jitter | 시작 조건 여러 개 | Si | 수렴 < [X] s, jitter p99 < [X] ns | P1 |
| PVT/margin | 온도 sweep 중 offset·주파수 보정량 (오실레이터 drift) | −40…+105 °C ramp | Si | 추종 실패 0, offset 기준 내 | P1 |
| STRESS | 최대 트래픽 중 PTP (큐 지연이 timestamp에 안 섞이나) | line rate 부하 | Si | 부하 유무 offset 차이 < [X] ns | P1 |
| SYS | 카메라 FSYNC: PHC periodic out → 여러 카메라, 노출 시작 skew | 카메라 4–11대 | Si | skew < [X] µs, jitter 기록 | P1 |
| SYS | 프레임 SoF timestamp ↔ frame counter: drop·중복·주기 jitter·카메라 간 skew | 24 h | Si | drop 0, 주기 오차 abs(dt − T) < [X] µs ([V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html)) | P0 |
| SYS | 카메라–LiDAR 정렬 end-to-end: LED 스트로브(카메라에 보임)+스코프로 ground truth | 정지/회전 | Si | 측정 skew와 timestamp 차이 일치 ± [X] µs | P1 |
| SYS | 시간 척도: GPS PPS/NMEA → TAI/UTC 변환, leap second | 경계 날짜 시뮬 | 둘 다 | 1 s·18 s·37 s 오차 0 ([L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html)) | P1 |
| SYS | 도메인 간 cross-timestamp: PHC ↔ CPU timer ↔ NPU 완료 시각 | 반복 | Si | 매핑 오차 < [X] ns | P2 |
| SOAK | 72 h lock 유지 + 온도 사이클 | 챔버 | Si | 최대 절대 offset 기준 내, lock 손실 0 | P1 |

### 3. 우선순위 · A0 첫 주

- **P0**: ① PHC 읽기/쓰기·주파수 조정 동작, hi/lo latch ② PTP 패킷에 HW timestamp가 찍히나 (`ethtool -T`, `ptp4l -m` 로그) ③ GM에 lock ④ **PPS out을 GM PPS와 스코프로 비교** — SW가 보고하는 offset이 아니라 **물리적 비교**가 진짜 통과 기준 ⑤ CSI-2 SoF timestamp + frame counter가 들어오나.
- **P1**: 에러·holdover·리셋 후 재수렴, 온도, 부하, FSYNC 다중 카메라 skew, LED 스트로브 end-to-end. **P2**: 도메인 간 cross-timestamp 정밀도, 장거리 hop 수.
- **안 할 것**: PTP 프로토콜 상태 머신의 모든 메시지 조합은 SW 스택(linuxptp) 테스트·적합성 툴 몫, 카운터 증가 로직 자체는 DV. 우리는 **물리적 시간 정확도**와 **하드웨어 latch 지점**.

### 4. 장비 · 셋업 · 관찰 지점

```text
  ┌─ GM (GPS-disciplined PTP grandmaster) ──── PPS ref ─────────────┐
  │        │ gPTP over T1 (media converter/스위치)                   ▼
  │        ▼                                               ┌─────────────────┐
  │   ┌─ DUT SoC ──────────┐  PPS out ────────────────────►│ 스코프 / TIC     │  ch1: GM PPS, ch2: DUT PPS
  │   │ PHC ─ servo        │  FSYNC ─► 카메라 ×4 ─────────►│ (time interval   │  ch3: FSYNC, ch4: LED 구동
  │   │ CSI-2 SoF latch    │◄─ 카메라 프레임                 │  counter)       │
  │   └────────────────────┘                               └─────────────────┘
  │   LED 스트로브 (모든 카메라 시야, 스코프 ch4와 같은 신호) → 프레임 속 LED 켜진 줄로 실제 노출 시각 역산
```

- **장비**: PTP grandmaster(GPS), 스코프 또는 **TIC**(time interval counter), PTP 지원 스위치/탭, 지연 비대칭을 일부러 만드는 케이블/스위치, LED 스트로브 + 카메라, 온도 챔버.
- **관찰**: servo가 보고하는 offset·path delay·주파수 보정(ppb), PPS 비교 결과(진짜 오차), timestamp 누락 카운트, frame counter·SoF timestamp 로그, 온도.
- **Linux (linuxptp)**:

```bash
ethtool -T eth0                     # HW TX/RX timestamp 능력, PHC 번호 (/dev/ptpN)
ptp4l -i eth0 -f automotive-slave.cfg -m   # gPTP slave, offset/path delay 로그 (설정 파일 이름은 linuxptp 예제 기준) [추정]
phc2sys -s eth0 -c CLOCK_REALTIME -O 0 -m  # PHC → 시스템 시계
ts2phc -c eth0 -s nmea -m                  # GPS PPS로 PHC discipline [추정: 옵션은 버전마다]
testptp -d /dev/ptp0 -p 1000000000         # PPS 주기 출력 (커널 tools/testing/selftests/ptp) [추정]
```

**[실행 결과] 시간 예산 숫자** (`e_numbers.py` §4, 실제 출력):

```text
== 4. PTP / gPTP timing math ==
asymmetry 200 ns -> measured offset 1100 ns, mean path delay 500 ns, error 100 ns (= asym/2)
freq error   100 ppm over sync interval    125 ms -> drift   12500.0 ns between corrections
freq error   100 ppm over sync interval   1000 ms -> drift  100000.0 ns between corrections
freq error   0.1 ppm over sync interval    125 ms -> drift      12.5 ns between corrections
timestamp clock 125 MHz -> 8 ns resolution; 250 MHz -> 4 ns
rolling shutter 2160 rows @ 30 fps (+90 blank lines): line time 14.81 us, top-to-bottom 32.0 ms
   at 15 m/s the car moves 0.48 m between first and last row
   at 30 m/s the car moves 0.96 m between first and last row
PASS asymmetry error = asym/2
```

| 출력 | 의미 → 플랜 |
|---|---|
| 비대칭 200 ns → 오차 100 ns | 프로토콜은 비대칭을 **볼 수 없다** → PPS 물리 비교가 유일한 진실, PHY/MAC 지연 보정값(TX/RX latency)을 링크 속도·모드별로 측정해 넣어야 함 |
| 100 ppm × 125 ms = 12.5 µs | rate ratio(주파수) 보정 없이 offset만 맞추면 µs 단위로 흐름 → servo가 주파수까지 추종하는지 시험. (125 ms는 gPTP 기본 sync 간격으로 흔히 쓰이는 값 [추정]) |
| 8 ns 해상도 | 타임스탬프 4개가 들어가는 계산 → 정확도 목표 수십 ns 이하라면 PHC 클럭 주파수부터 확인 |
| rolling shutter 32 ms | 카메라 "frame timestamp" = 어느 줄·어느 순간인지 **정의서**가 필요, LiDAR와 정렬할 때 줄 시각 보정 — 면접관 특허 주제와 직결 [추정 연결] |

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 플랜에서 잡는 행 |
|---|---|---|
| SW offset은 ±20 ns인데 PPS 비교는 300 ns 어긋남 | 경로 비대칭, PHY latency 보정값 틀림·누락 | PERF PPS 물리 비교 |
| 링크 재수립마다 offset이 수십 ns 점프 | PHY 내부 지연이 링크 학습마다 달라짐 (FEC/PCS 정렬) [추정] | RST 링크 루프 중 PPS 비교 |
| 64-bit PHC 읽기가 가끔 1초 튐 | hi/lo를 따로 읽는 tearing (latch 미사용) | REG 1e6 연속 읽기 |
| LiDAR와 카메라가 정확히 1 s 또는 18 s 차이 | PPS↔NMEA 초 매칭 off-by-one, GPS vs UTC leap second | SYS 시간 척도 ([deck §7 #13](2026-10-25_rivian_hm_screen_prep.html)) |
| 부하 걸릴 때만 offset 흔들림 | timestamp를 MAC 큐 뒤에서 찍음 / SW timestamp로 fallback | STRESS line rate 중 PTP |
| 카메라 4대 skew가 한 대만 큼 | FSYNC 배선 지연, 그 카메라만 free-run 설정 | SYS FSYNC skew |
| 고온에서 lock 풀림 | 오실레이터 drift가 servo 추종 범위 밖 | PVT 온도 ramp |

### 6. 10분 영어 답 뼈대

1. "Scope: gPTP slave on automotive Ethernet with a static grandmaster, MAC hardware timestamps, a PHC that drives FSYNC to the cameras and latches CSI-2 frame start; GPS PPS available on the bench."
2. "Picture: grandmaster, Ethernet to the SoC, PHC, servo, PPS out, FSYNC to cameras, frame-start latch from the receiver."
3. "First, define what each timestamp means — for a rolling-shutter camera the first and last row differ by about 32 milliseconds at 30 fps, so 'frame time' has to be specified."
4. "Register level: PHC read with a hi-lo latch so 64-bit reads never tear, time set and frequency adjust resolution."
5. "Functional: every PTP event message gets a hardware timestamp; the slave locks to the grandmaster."
6. "The real pass criterion is physical: the DUT's PPS output against the grandmaster's PPS on a scope or time-interval counter, mean, sigma and max over an hour — because the protocol can't see path asymmetry; 200 nanoseconds of asymmetry is a 100-nanosecond error it reports as zero."
7. "Errors and recovery: grandmaster loss and holdover drift, lost and duplicated messages, re-convergence after link re-training, where PHY latency can change."
8. "Stress and PVT: offset under line-rate traffic, and through a temperature ramp, since a 100 ppm oscillator drifts 12.5 microseconds per 125-millisecond interval if frequency isn't tracked."
9. "Camera side: FSYNC skew across all cameras, frame-counter gaps and period jitter per stream, inter-camera skew per frame."
10. "End-to-end ground truth: an LED strobe visible to every camera with the drive signal on the scope, so measured image time matches the timestamps."
11. "Time scales: PPS-to-NMEA second matching and the 18-second GPS-UTC offset — classic one-second and eighteen-second bugs."
12. "Soak 72 hours with max offset logged; everything in the harness with the scope measurements automated."
13. "FPGA covers PHC logic, latches and message timestamping; absolute accuracy and drift only on silicon."

### 7. 꼬리 질문

- Q1. "PTP says the offset is 10 nanoseconds. Do you believe it?"

<details><summary>답</summary>

"No — that's the servo's own estimate, which assumes a symmetric path. I compare a physical output, the PPS from our PHC, against the grandmaster's PPS on a scope or time-interval counter. Any asymmetry — different TX and RX latency in the PHY, a switch, cable — shows up there as a constant offset of half the asymmetry, and I calibrate the latency corrections until that's within budget."
- 한국어 핵심: servo offset은 대칭 가정의 자기 평가 → PPS 물리 비교, 비대칭/2 상수 오차 보정.

</details>

- Q2. "How do you verify multiple cameras are actually synchronized?"

<details><summary>답</summary>

"Two levels. From logs: per stream, frame-counter gaps, duplicates and period jitter, and per frame the skew between cameras' frame-start timestamps on the same PTP time base. Then ground truth: an LED strobe that all cameras see, driven from a signal I also put on the scope, so I can compare the row where the LED turns on in each image to the timestamps. That catches a camera that ignores FSYNC or a wrong latch point."
- 한국어 핵심: 로그(카운터·jitter·skew) + LED 스트로브 ground truth로 latch 지점 오류까지.

</details>

- Q3. "Should the timestamp be in the MAC or the PHY?"

<details><summary>답</summary>

"Closer to the wire is better: the PHY sees the frame closer to the medium, so variable latency inside the PHY — PCS and FEC alignment — doesn't enter the timestamp. A MAC timestamp needs a calibrated PHY latency per speed and mode, and that calibration must hold after every link re-train. Either way, validation measures it against PPS rather than trusting the datasheet number."
- 한국어 핵심: PHY가 매체에 가까워 유리, MAC이면 PHY latency 보정·재학습 후 유지 확인, 결국 PPS로 측정.

</details>

- Q4. "LiDAR and camera timestamps differ by exactly one second. What happened?"

<details><summary>답</summary>

"Exactly one second is almost always a labeling error, not a clock error: the PPS edge was paired with the wrong NMEA second, off by one. Exactly 18 seconds would be GPS time versus UTC, 37 seconds TAI versus UTC. I'd check which time scale each sensor reports and the pairing rule for PPS and the time message, and add a test that crosses those boundaries."
- 한국어 핵심: 정확히 1 s = PPS↔NMEA 짝 off-by-one, 18 s = GPS−UTC, 37 s = TAI−UTC.

</details>

### 8. Don 연결

- "At SK hynix I built telemetry for customer debugging, where event timelines from different sources had to be aligned to find root cause — the same discipline as defining what a timestamp means and checking it against an independent reference. I haven't run PTP on silicon; I'd learn the linuxptp tooling quickly, but the measurement plan — physical PPS comparison, statistics, temperature — is what I'd bring. [숫자 채우기: telemetry 기능 이름·채택 고객 수]"

## 자주 틀리는 것

1. **"BER 테스트 몇 분 돌렸다"** — 1e-12 at 95%는 에러 0으로 3e12 bit: 1 Gb/s 50분, 100 Mb/s 8.3시간. 시간을 계산 없이 말하면 Staff 신호 감점.
2. **RFC 2544를 "iperf 한 번"으로 대체** — throughput은 **zero loss** 기준·7 프레임 크기·trial ≥ 60 s, latency 120 s × 20회 [1]. 64 B가 진짜 시험.
3. **FCS 에러 0 = margin 충분** — 1000BASE-T1은 RS-FEC가 오류를 가린다. SQI·FEC 정정 카운트를 같이 본다.
4. **T1에서 auto-neg 기대** — BASE-T1은 보통 master/slave **고정**. 둘 다 master면 링크 없음.
5. **CSI-2 ECC가 헤더를 지켜 준다** — SEC-DED는 1-bit 정정·2-bit 검출까지. 3-bit은 93%가 오정정(실행 결과) → WC·FS/FE·frame 번호 검사가 필수.
6. **카메라 검증을 실제 센서로 시작** — 센서·렌즈·조명·SER/DESER 변수가 섞인다. TPG → bit-exact가 먼저.
7. **ISP를 눈으로 판정** — validation은 C 모델 bit-exact. 화질은 튜닝팀.
8. **PTP offset 로그를 정확도로 보고** — 비대칭은 프로토콜이 못 본다. PPS 물리 비교가 진짜.
9. **"frame timestamp"를 정의 없이 사용** — 노출 시작? FSYNC? SoF at RX? rolling shutter면 위아래 수십 ms 차이.
10. **PCIe "1,000 cycle PASS"를 리셋 종류 없이 말하기** — remove/rescan은 링크 재학습이 아니다 ([LP14 §2.4](2026-10-31_LP14_validation_methodology.html)).

## 출처

1. RFC 2544 — Benchmarking Methodology for Network Interconnect Devices (frame sizes §9.1, trial 길이, §26.1–26.6): https://www.rfc-editor.org/rfc/rfc2544
2. Wikipedia — Ethernet over twisted pair (BASE-T1 표: 802.3bw/bp/ch/cg, clause, PAM3, symbol rate, reach): https://en.wikipedia.org/wiki/Ethernet_over_twisted_pair
3. OPEN Alliance TC12 — Interoperability & Compliance Tests for 100 and 1000BASE-T1 PHYs (문서 목록·다른 TC 목록): https://opensig.org/tech-committee/tc12-interoperability-compliance-tests-for-1000base-t1-phys
4. OPEN Alliance — 1000BASE-T1 Ethernet ECU Test Specification Layer 1 v1.1 (TC8): https://opensig.org/wp-content/uploads/2025/05/1000BASE-T1-Ethernet-ECU-Test-Specification-Layer-1-v1.1.pdf
5. OPEN Alliance — 100BASE-T1 PMA Test Suite v1.3: https://opensig.org/wp-content/uploads/2024/01/Open_Alliance_100BASE-T1_PMA_Test_Suite_v1.3.pdf
6. UNH-IOL — Clause 96 (100BASE-T1) PMA Test Suite v1.0 (test mode ↔ 시험 항목): https://www.iol.unh.edu/sites/default/files/testsuites/ethernet/CL96_PMA/Clause96_PMA_Test_Suite_v1.0.pdf
7. Teledyne LeCroy blog — Automotive Ethernet compliance, test modes: https://blog.teledynelecroy.com/2017/08/automotive-ethernet-compliance-five.html
8. Linux include/uapi/linux/mdio.h (MMD 번호, BASE-T1 PMA/PCS/AN 레지스터): https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/mdio.h
9. ethtool(8) man page (master-slave, cable-test, self-test): https://man.archlinux.org/man/ethtool.8.en.raw ; LKML "Add support for SQI and master-slave": https://lkml.iu.edu/hypermail/linux/kernel/2006.1/03829.html
10. TI SNLA371 — DP83TG720 1000BASE-T1 compliance/PMA testing app note: https://ti.com/lit/SNLA371
11. MIPI Alliance — CSI-2 specification page (v4.2, 32 VC, C/D/A-PHY): https://www.mipi.org/specifications/csi-2
12. AMD PG232 MIPI CSI-2 RX Subsystem — Interrupt Status Register: https://docs.amd.com/r/en-US/pg232-mipi-csi2-rx/Interrupt-Status-Register
13. Intel MIPI CSI-2 IP User Guide — limitations (VCX 4-bit VC, ECC 6-bit): https://www.intel.com/content/www/us/en/docs/programmable/813926/25-1/limitations-and-known-issues.html
14. Linux phy-mipi-dphy.h (THS-SETTLE 정의): https://git.yoctoproject.org/linux-yocto/tree/include/linux/phy/phy-mipi-dphy.h?h=v5.2.22 ; Efinix D-PHY timing (TCLK-SETTLE 95–300 ns): https://www.efinixinc.com/docs-html/dst20/topics/ref_mipi_timing.html
15. Linux kernel — Timestamping (SO_TIMESTAMPING, hwtstamp rx_filter): https://docs.kernel.org/networking/timestamping.html
16. IEEE 802.1 AVB — Garner, assumptions for error sources (1 µs / 7 hops): https://grouper.ieee.org/groups/802/1/files/public/docs2008/as-garner-assumptions-for-error-sources-time-synch-0109-v04.pdf
17. IEEE 802.1DG — automotive time sync profile slides: https://grouper.ieee.org/groups/802/1/files/public/docs2019/dg-potts-time-sync-profile-0919-v2.pdf
18. AUTOSAR PRS Time Synchronization Protocol (BMCA 미지원): https://www.autosar.org/fileadmin/standards/R20-11/FO/AUTOSAR_PRS_TimeSyncProtocol.pdf

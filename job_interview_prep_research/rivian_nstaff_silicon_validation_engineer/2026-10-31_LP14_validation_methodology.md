# LP14 · Linux에서 PCIe 검증 방법론 · 테스트 플랜 · 자동화
<!-- theme: pastel -->

> **묶음**: E 도구 · 검증 · 디버그 · **우선**: P0 (JD "silicon + board validation", 온사이트 설계형 질문 "How would you validate a PCIe controller on our new SoC?") · **선행 노트**: [LP13 관측 도구](2026-10-31_LP13_linux_pcie_tools.html) · [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · [LP11 리셋](2026-10-31_LP11_reset_types.html) · **이전 노트**: [LP13](2026-10-31_LP13_linux_pcie_tools.html) · **다음 노트**: [LP15 디버그 시나리오](2026-10-31_LP15_debug_scenarios.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [deck §6 검증 설계](2026-10-25_rivian_hm_screen_prep.html) · [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html) · **코드**: [link_cycle.py](onsitePrep/lp14/link_cycle.py) · [QEMU 실행 로그](onsitePrep/lp14/outputs/qemu_link_cycle_log.txt) · [macOS sim 로그](onsitePrep/lp14/outputs/macos_sim_runs.txt)

## 0. TL;DR

- 이 노트로 답할 것: "새 SoC의 PCIe 컨트롤러를 어떻게 검증하나", "링크 트레이닝 합격 기준은?", "몇 번 돌려야 하나, 왜?", "500번에 1번 실패하면?", "테스트 자체가 맞는지 어떻게 아나", "실제 endpoint 없이 DMA는 어떻게 테스트하나".
- **외울 것 ① — 12개 영역 테스트 플랜**: link bring-up 루프 · speed/width 매트릭스 · equalization · 신호 품질(AER/margin) · 열거/리소스 · 인터럽트 · DMA 기능 · 성능 · 전원(ASPM/D-state) · 리셋/복구 · 에러 경로 · soak. 각 항목마다 **목적 · 방법 · 숫자로 된 합격 기준 · 도구**.
- **외울 것 ② — rule of three**: N번 돌려 실패 0이면 실패율의 95% 상한 ≈ **3/N**. 1,000 사이클 무실패 = "1/334보다 낮다"까지만 말할 수 있다. 1/500 문제를 95% 확률로 한 번이라도 보려면 **≈1,500 사이클**, 수정 후 "1/500 미만"을 주장하려면 다시 **≈1,500 무실패**. (스크립트로 계산 검증: 1-0.05^(1/1000)=0.00299)
- **외울 것 ③ — 트리거 종류가 다르면 검증하는 것이 다르다**: `remove`+`rescan`은 SW 재열거일 뿐 링크는 L0 유지(LP13에서 실측), FLR은 함수만, SBR/hot reset·Link Disable은 링크 재학습, PERST#/전원 사이클이 진짜 cold 경로. "1,000 cycle 통과"라고 할 때 **어느 리셋인지**를 같이 말한다.
- 실습: [link_cycle.py](onsitePrep/lp14/link_cycle.py) — 트리거 → 링크 대기 → speed/width/DLActive/AER 판정 → CSV → 실패율 + Clopper-Pearson 상한. macOS `--sim`으로 실패 주입까지 검증, **QEMU 게스트의 실제 NVMe에서 sysfs 백엔드로 4가지 방식 × 30사이클 실행**, 그리고 **negative control(일부러 틀린 기대값)로 하네스가 실패를 잡는지 확인**.
- 30초 영어 답 (headline Q: "How would you validate a PCIe controller on our new SoC?"):
  - "I'd start from the spec and the controller's feature list and turn it into a test plan with numeric pass criteria: link bring-up across cold boot, warm reset, hot reset and PERST# for at least a thousand to a few thousand cycles — always the expected speed and width with DLActive set; every supported speed-and-width combination; equalization and margin at PVT corners using AER counters and lane margining; enumeration and BAR assignment; every interrupt mode; DMA data integrity across sizes, alignments and boundaries; throughput against the theoretical peak; ASPM and D-state transitions; reset and error recovery with injected errors; and long soak. The same tests run on the FPGA prototype first, so on silicon I'm debugging only the difference, and everything runs in an automated harness that controls power, captures the console, stores every result with chip ID and conditions, and collects logs automatically on failure."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 새로 지은 다리(링크) 검사

새 SoC의 PCIe 컨트롤러 = **새로 지은 다리**. 다리를 개통하기까지 하는 일을 생각하면 용어가 다 정리된다.

- **Verification(설계 검증)** = **설계도 검사**. "도면대로 그렸나?" — 칩이 만들어지기 전, RTL을 시뮬레이션/에뮬레이션으로 돌려 스펙과 일치하는지 확인 (DV 팀, UVM).
- **Validation(검증, 실물 검증)** = **실제 트럭을 몰아 보는 것**. "실물 다리가 진짜 트럭을, 진짜 날씨에서 버티나?" — 실리콘/보드에서 실제 소프트웨어로. 이 직무의 본업.
- **Characterization(특성화)** = **한계 시험**. "몇 톤까지, 영하 몇 도까지, 바람 몇 m/s까지 버티나?" — 전압·온도·주파수(PVT)를 바꿔 가며 margin(여유)을 숫자로 잰다. shmoo plot이 대표.
- **Compliance(규격 인증)** = **정부 인증 시험**. "표준 차량 규격과 맞나?" — PCI-SIG의 정해진 시험(전기·프로토콜·상호운용성)을 통과해야 **Integrators List**에 오른다.
- **SLT / production test(양산 테스트)** = **출고 전 개별 검사**. 다리 하나가 아니라 **칩 수백만 개**를 빠르게 걸러내는 짧은 테스트.

```text
 칩 일생에서 PCIe 검증이 나오는 곳

 [RTL 설계] ──► [Pre-silicon]                ──► [Tape-out] ──► [Post-silicon]                                   ──► [양산]
               simulation (UVM, DV팀)                         bring-up ─► validation ─► characterization(PVT) ─► SLT / ATE
               emulation (Palladium/ZeBu)                     "링크 뜨나"   "기능 전부"    "margin 얼마나"         "빠르게 거르기"
               FPGA prototype (HAPS, Zynq)                    ▲
                 └─ 같은 테스트를 먼저 돌려 둠 ───────────────┘  (silicon day 1에 바로 실행)
```

### 1.2 PCIe 검증이 특히 어려운 이유 (처음 듣는 사람용)

1. **아날로그 + 디지털**: 링크 트레이닝은 SerDes(고속 직렬 송수신기)의 전기적 품질에 달려 있어, 같은 칩도 보드·온도·전압에 따라 결과가 달라진다 → **반복과 통계**가 필수.
2. **상대방이 있다**: 링크는 혼자 못 뜬다. 상대(endpoint/root port)의 구현이 다양 → **여러 파트너**와 테스트(상호운용성).
3. **간헐적 실패**: "1,000번 중 2번 안 뜬다"가 흔하다 → 사람이 손으로 못 함 → **자동화 하네스**.
4. **SW가 보이는 것이 제한적**: LTSSM 상태는 표준 레지스터에 없다(LP13) → 컨트롤러 debug 레지스터, 애널라이저, 스코프를 같이 써야 한다.

### 1.3 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| DV | Design Verification | 칩 만들기 전 RTL 시뮬레이션 검증 | validation과 중복 피하기 |
| pre-silicon | Pre-silicon validation | 칩이 나오기 전 FPGA/에뮬레이터에서 SW 검증 | 테스트를 미리 준비 |
| emulation | Hardware Emulation (Palladium, ZeBu, Veloce) | 전용 장비에서 RTL을 MHz로 실행, 가시성 좋음 | 부팅·펌웨어 초기 검증 |
| FPGA prototyping | FPGA Prototyping (HAPS, Zynq/VCU) | FPGA에 RTL을 올려 수십 MHz로 실행 | 장시간 SW 테스트, Don의 Solidigm 경험 |
| bring-up | Silicon Bring-up | 첫 실리콘에서 전원·클럭·리셋·부팅·링크를 처음 살리는 단계 | "첫 주에 뭐 하나" 질문 |
| PVT | Process, Voltage, Temperature | 공정 편차·전압·온도의 조합(corner) | margin은 corner에서 깨진다 |
| shmoo | Shmoo plot | 두 변수(예: 전압×주파수)를 격자로 sweep해 pass/fail을 칠한 지도 | margin 시각화, Don의 SK hynix 경험 |
| margin | Margin | 실패 경계까지 남은 여유 (시간·전압) | 양산·필드 신뢰성 예측 |
| BER | Bit Error Rate | 비트 몇 개당 에러 1개 (PCIe 목표 보통 1e-12 수준) | "에러 0"의 통계적 의미 |
| eye diagram | Eye Diagram | 수신 신호를 겹쳐 그린 "눈" 모양, 눈이 크게 열릴수록 좋음 | 신호 품질 |
| LMR | Lane Margining at Receiver | Gen4+ 수신기가 샘플링 지점을 옮겨 보며 margin을 보고하는 표준 기능 | HW 장비 없이 SW로 eye 근사 |
| equalization (EQ) | Link Equalization | Gen3+에서 송신 preset/계수를 상대와 협상해 신호를 보정 | 고속 링크 실패 1순위 |
| compliance | PCI-SIG Compliance | 표준 시험 통과 → Integrators List | 제품 출시 조건 |
| CEM | Card Electromechanical Spec | 카드·커넥터·슬롯의 전기/기계 규격 (PERST# 타이밍 등) | 보드 검증 기준 |
| CBB / CLB | Compliance Base Board / Load Board | 카드(CBB) 또는 메인보드 슬롯(CLB)을 스코프에 연결하는 PCI-SIG 시험 보드 | 전기 컴플라이언스 |
| SLT | System Level Test | 실제 보드에서 실제 워크로드로 칩을 거르는 양산 테스트 | validation 테스트가 소재가 됨 |
| ATE | Automatic Test Equipment | 테스터에서 scan/BIST 같은 구조 테스트 | SLT와 상보 |
| AER | Advanced Error Reporting | PCIe 에러 종류별 기록 | 신호 품질 지표 |
| FLR | Function Level Reset | 함수 하나만 리셋 | 리셋 경로 검증 |
| SBR | Secondary Bus Reset | 브리지가 아래 링크에 hot reset을 보냄 | 링크 재학습 루프 |
| PERST# | PCIe Reset (fundamental) | 보드 리셋 핀 | 가장 "진짜" 리셋 |
| EPF / EPC | Endpoint Function / Endpoint Controller | Linux가 SoC를 **endpoint로** 동작시키는 프레임워크의 함수/컨트롤러 | 테스트 상대(partner) 만들기 |
| XDMA / QDMA | AMD(Xilinx) DMA/Bridge Subsystem for PCIe / Queue DMA | FPGA용 PCIe DMA IP | FPGA로 테스트 파트너 만들기 |
| harness | Test Harness | 테스트를 자동으로 돌리고 결과를 모으는 틀 | 1,000 사이클을 사람이 못 돌림 |
| negative control | Negative Control | 일부러 실패해야 하는 조건으로 테스트가 실패를 잡는지 확인 | "테스트가 맞나?"의 답 |
| Clopper-Pearson | Exact binomial confidence interval | 실패 k번/N번에서 실패율의 정확한 신뢰구간 | rule of three의 일반형 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Verification · Validation · Characterization · Compliance — 정확한 구분

| 구분 | 질문 | 대상 · 플랫폼 | 산출물 | PCIe 예 |
|---|---|---|---|---|
| Verification | "설계가 스펙대로인가?" | RTL, 시뮬레이터/에뮬레이터, VIP(검증 IP) | coverage, 버그 | LTSSM 상태 전이 coverage, TLP 순서 규칙 |
| Validation | "실물이 실제 사용 조건에서 동작하나?" | 실리콘 + 보드 + SW | 테스트 결과, 버그, 에라타 | 링크 업 루프, DMA 무결성, 상호운용성 |
| Characterization | "어디까지 버티나? (margin)" | 여러 칩(SS/TT/FF) × V × T × F | shmoo, margin 표, Vmin/Fmax | lane margining, EQ preset sweep, BER at corners |
| Compliance | "표준 인증 시험을 통과하나?" | PCI-SIG 정의 시험·픽스처 | Integrators List 등재 | TX eye/jitter (CBB/CLB + SigTest), 프로토콜(LTSSM) 시험, interop |
| SLT / Production | "이 칩 한 개가 불량인가?" | 양산 소켓, 짧은 테스트 | bin, yield | 링크 업 + 짧은 DMA + 에러 0 |

- PCI-SIG 컴플라이언스 [추정 — 벤더/테스트랩 자료 기준, pcisig.com 직접 확인 못 함(403)] [5][6]: Compliance Workshop(시험 + 교육) 또는 Authorized Test Lab에서 시험 → 필수 시험 100% + 상호운용성 80% 통과 시 **Integrators List** 등재. 전기 시험은 PCI-SIG가 제공하는 **CBB**(카드 시험용) / **CLB**(슬롯 시험용) 픽스처와 스코프 캡처를 분석하는 **SigTest** 소프트웨어 사용.
- 이 직무에서의 위치: **validation + characterization의 SW 쪽** — 테스트 콘텐츠(C), 하네스(Python), PVT sweep 자동화, 결과 DB. 전기 컴플라이언스 측정 자체는 SI/HW 팀이 하지만, 그때 칩을 **Compliance 모드로 보내는 SW**(LnkCtl2 Enter Compliance, Transmit Margin)는 validation SW가 제공하는 경우가 많다 [추정].

### 2.2 단계별: pre-silicon → bring-up → validation → PVT → SLT

| 단계 | 플랫폼 | 목표 | PCIe 내용 | 종료 기준(예) |
|---|---|---|---|---|
| Pre-silicon (emulation) | Palladium/ZeBu + PCIe speed bridge 또는 transactor | 부팅 ROM·드라이버 초기 시퀀스 | 컨트롤러 레지스터 초기화, iATU, config 접근, 간단 DMA | 모든 bring-up 테스트가 에뮬레이터에서 PASS |
| Pre-silicon (FPGA) | HAPS / Zynq·VCU 보드 (PHY는 FPGA GT 또는 저속) | 장시간 기능 테스트, 드라이버 개발 | 열거, MSI/MSI-X, DMA 패턴, 리셋 경로, 에러 처리 | 기능 테스트 PASS, **실리콘용 테스트 바이너리/하네스 준비 완료** |
| Bring-up (first silicon) | 평가 보드 | "살아 있나" | 전원/refclk/PERST# 스코프 → 컨트롤러 레지스터 sanity → **Gen1 x1부터** 링크 업 → 목표 속도 | 기대 speed/width로 링크 업, 열거, 기본 DMA |
| Validation | 여러 보드 + 여러 파트너 장치 | 기능 전부 + 상호운용성 | §2.3 12개 영역 전부 | 테스트 플랜 항목별 PASS, 버그 트래커 정리 |
| Characterization (PVT) | SS/TT/FF 칩 × 온도 챔버 × 전압 | margin | EQ preset sweep, lane margining, BER, 링크 루프 at corner | margin 표가 스펙/목표 이상 |
| SLT / production | 양산 핸들러 + SLT 보드 | 불량 칩 거르기 | 짧은 링크 업 + DMA + AER 0 | 테스트 시간 예산 내 yield/escape 목표 |

- 핵심 원칙 (deck §6.1과 같은 말): **"Write the test once, run it on every platform."** — 얇은 HAL(하드웨어 추상층)로 같은 C 테스트가 에뮬레이터·FPGA·실리콘에서 돈다. 실리콘 첫날에는 **FPGA와의 차이만** 디버그한다.
- FPGA에서 못 보는 것 (deck §5.1 Q2): 실제 SerDes/PHY 아날로그, 실제 클럭 비율·타이밍, 전원/열 → **링크 트레이닝 margin, EQ, BER은 실리콘에서만**.

### 2.3 테스트 플랜 — 12개 영역 (LP00 표 확장)

요약 표 (LP00 원본 유지 + 열 추가):

| 영역 | 테스트 | 판정 기준 | Linux에서 쓰는 것 | Bare-metal에서 |
|---|---|---|---|---|
| Link bring-up | cold/warm boot, hot reset, PERST# 루프 **N≥1000** | 매번 기대 speed/width, DLActive, 시간 내 | 전원 제어 + sysfs/lspci 수집 (`link_cycle.py`) | LTSSM/LnkSta 레지스터 polling 루프 + GPIO PERST# |
| Speed/width 매트릭스 | Gen1–GenN × x1/x2/x4… 강제 | 모든 조합 학습 성공 | `setpci` LnkCtl2 Target Link Speed + Retrain | 같은 레지스터 직접 |
| Equalization | preset sweep (지원 시) | EQ phase 성공, 에러율/margin | LnkSta2, 컨트롤러 레지스터, `pcilmr` | EQ 제어 레지스터 |
| 신호 품질 | 트래픽 중 AER correctable 카운트, PVT corner | 임계값 이하, 추세 | `aer_dev_correctable`, `pcilmr` | AER cap 레지스터, 컨트롤러 에러 카운터 |
| 열거·리소스 | BAR 크기, 64-bit, 다중 장치, 스위치 | 리소스 할당 성공 | dmesg, `/proc/iomem` | 자체 열거 코드 |
| 인터럽트 | INTx/MSI/MSI-X 각 vector | 전부 전달, 누락 0 | `/proc/interrupts`, 테스트 드라이버 | GIC/ITS 직접 |
| DMA 기능 | 크기·정렬·경계(4 KiB 교차) sweep, 패턴 비교 | 데이터 일치(CRC) | 테스트 드라이버, **pci-epf-test + pci_endpoint_test** | DMA 엔진 직접 |
| 성능 | read/write throughput, latency, MPS/MRRS 설정별 | % of 이론 peak | `fio`(NVMe), 커스텀 DMA 벤치 | cycle counter |
| 전원 | ASPM L1/L1.2 진입·탈출 반복, D3 ↔ D0 | 복귀 후 동작·성능 | sysfs `link/`, runtime PM | PM 레지스터 |
| 리셋·복구 | FLR, hot reset, surprise down | 복구 후 정상, 커널 패닉 없음 | `reset`, `reset_subordinate`, `remove/rescan` | 리셋 시퀀스 직접 |
| 에러 경로 | AER 에러 주입 | 보고·복구 동작 | `aer-inject`, DWC `rasdes_err_inj`, QEMU 주입 | 컨트롤러 에러 주입 |
| Soak | 장시간 트래픽 + 온도 | 에러 0, 링크 유지 | 자동화 harness | 장시간 루프 |

#### 2.3.1 Link bring-up 루프

- **목적**: 링크가 **매번** 올라오는지. 간헐 실패(1/500)는 여기서만 잡힌다.
- **방법**: 트리거 종류별 루프 (§2.4) — cold boot(전원), warm reboot, PERST# 토글, hot reset(SBR), Link Disable 토글, (참고용) FLR, remove/rescan. 각 사이클: 트리거 → **시간 제한 안에** DLActive 대기 → speed/width/EQ 상태/AER 스냅샷 → 판정.
- **합격 기준(숫자로)**: 예) "Gen4 x4, DLActive+, 링크 업 시간 < [X] ms, AER correctable 증가 0, **N=3,000 사이클 무실패**(→ 95% 상한 1e-3)". 링크 업 시간 분포(p50/p99/max)도 기록 — 평균은 같아도 꼬리가 길어지면 margin 저하 신호.
- **도구**: Linux `link_cycle.py`(§6), 전원 제어(PDU/릴레이/랩 전원), PERST# GPIO, 시리얼 콘솔. Bare-metal: 컨트롤러 LTSSM 레지스터를 타임스탬프와 함께 polling해 상태 전이 로그를 남김.
- **흔한 버그**: PERST# deassert가 refclk 안정 전(CEM 타이밍 위반), 리셋 후 컨트롤러 초기화 순서(PHY PLL lock 전에 LTSSM enable), 고온에서만 Polling↔Detect 반복, 특정 파트너에서만 Gen4 EQ 실패, warm reset 후 장치가 RRS(CRS)를 너무 오래 보냄.

#### 2.3.2 Speed/width 매트릭스

- **목적**: 지원하는 **모든** 속도×폭 조합에서 학습 + 동작. 실제 제품은 최고 속도만 쓰지만, 파트너가 느리면 낮은 조합이 쓰인다.
- **방법**: LnkCtl2 Target Link Speed를 Gen1…GenN으로 설정 → LnkCtl Retrain → LnkSta 확인. 폭은 컨트롤러 설정(최대 lane 수 제한 레지스터)이나 보드 strap/lane 마스킹으로. 각 조합에서 짧은 DMA + AER 확인. lane reversal·polarity inversion 보드 변형도.
- **합격**: 매트릭스 모든 칸 PASS, 각 칸에서 BWMgmt/ABWMgmt 이벤트 기록 일치.
- **도구**: `setpci -s <port> CAP_EXP+30.w=<n>:f` + `CAP_EXP+10.w=20:20`, sysfs `current_link_*`.
- **흔한 버그**: Gen3→Gen1 다운 후 다시 Gen4로 올라가지 못함, x2 설정에서 lane 번호 매핑 오류, speed change 중 TLP 손실.

#### 2.3.3 Equalization (Gen3+)

- **목적**: 8 GT/s 이상에서 송신 preset/계수 협상(Phase 0–3)이 모든 조건에서 성공하는지, 그리고 **어떤 preset이 margin을 가장 크게 주는지**.
- **방법**: preset sweep (P0–P10 등) 강제 또는 컨트롤러가 선택한 preset 기록, 각 설정에서 margin(§2.6)과 에러 카운트 측정. 애널라이저로 EQ 단계 교환 확인.
- **합격**: LnkSta2 `EqualizationComplete+ Phase1+ Phase2+ Phase3+`, 선택된 preset에서 margin ≥ 목표.
- **흔한 버그**: Phase 3 timeout으로 Gen3/4 실패 후 낮은 속도로 fallback, 특정 채널 길이에서만 실패, 리타이머 경유 시 EQ 실패.

#### 2.3.4 신호 품질 (에러 카운터 · margin)

- **목적**: "동작한다"가 아니라 "**얼마나 여유 있게** 동작하나".
- **방법**: 최대 트래픽(DMA 루프/fio) 중 AER correctable(Receiver Error, Bad TLP, Bad DLLP, Replay Timer Timeout, Replay Num Rollover) 카운트를 **전후 delta**로, PVT corner마다. Gen4+면 `pcilmr`로 lane별 시간/전압 margin. 컨트롤러 lane별 카운터(DWC `rasdes_event_counters`).
- **합격**: 예) "1시간 트래픽당 correctable < [X]건, uncorrectable 0, 모든 lane EW/EH ≥ pcilmr 권장치".
- **흔한 버그/발견**: 특정 lane만 margin 작음(보드 라우팅/커넥터), 고온 + 저전압에서 Receiver Error 급증, refclk 지터.

#### 2.3.5 열거 · 리소스

- **목적**: 다양한 장치/토폴로지에서 BAR·버스 번호·브리지 창이 올바르게 잡히나.
- **방법**: 작은 BAR/큰 BAR(수 GB), 32/64-bit, prefetchable, 스위치 뒤 다중 장치, hotplug 후 rescan, SR-IOV VF. DT `ranges`/iATU outbound 창 크기 경계.
- **합격**: dmesg에 `can't assign` 없음, `/proc/iomem` 계층 정상, 드라이버 probe 성공.
- **흔한 버그**: iATU 창보다 큰 64-bit BAR, prefetch 창 미지원, 버스 번호 부족, 첫 config read를 링크 업 전에 해서 hang(LP15 #10).

#### 2.3.6 인터럽트 (INTx · MSI · MSI-X)

- **목적**: 모든 모드·모든 벡터가 전달되고, 누락/중복이 없나.
- **방법**: 테스트 endpoint(pci-epf-test, FPGA)가 벡터 k를 N번 raise → 호스트에서 count 비교. 동시에 여러 벡터, 높은 rate, CPU affinity 변경 중.
- **합격**: raise 수 = 수신 수 (`/proc/interrupts` delta), 지연 p99 < [X] µs.
- **흔한 버그**: MSI 주소가 ITS/GICv2m doorbell로 iATU inbound 매핑 안 됨, MSI-X 마스크/PBA 처리, Bus Master off라서 MSI write 못 함, INTx 공유 시 clear 순서.

#### 2.3.7 DMA 기능

- **목적**: 데이터가 **비트 단위로** 맞게 오가나 (양방향).
- **방법**: 크기(1 B…MB), 정렬(비정렬 시작/끝), 4 KiB 경계 교차(PCIe 규칙: 요청은 4 KiB 경계를 넘으면 안 됨 → 컨트롤러가 쪼개야 함), MPS/MRRS 조합, scatter-gather, 동시 채널, IOMMU on/off, cache coherent/non-coherent. 패턴(증가값, walking 1, PRBS) + CRC 비교.
- **합격**: 전 조합 데이터 일치, 타임아웃 0, AER 0.
- **도구**: Linux `pci-epf-test` + `pci_endpoint_test`(§2.7) — READ/WRITE/COPY를 memcpy·DMA 두 방식으로, 체크섬 비교. FPGA XDMA 루프백(§2.8). NVMe면 fio `verify=crc32c`.
- **흔한 버그**: ARM non-coherent에서 cache maintenance 누락(LP08), 4 KiB 경계 분할 버그, 마지막 부분 burst, DMA mask(32-bit) 무시.

#### 2.3.8 성능

- **목적**: 이론치 대비 몇 %인가, 병목은 어디인가 ([성능 메트릭 노트](2026-10-30_perf_metrics_ssd_vs_soc.html) 방법론 그대로).
- **방법**: 읽기/쓰기 throughput을 크기·outstanding 요청 수·MPS(128/256/512)·MRRS별로, latency(p50/p99)도. 이론치 = GT/s × lanes × 인코딩 효율(128b/130b) − TLP/DLLP 오버헤드.
- **합격**: 예) "Gen4 x4에서 큰 블록 read ≥ 이론 데이터율의 [X]%".
- **흔한 버그/발견**: MRRS가 128 B로 잡혀 read 대역폭 저하, 컨트롤러 outstanding tag 수 제한(Little's law: in-flight = BW × latency), ASPM 켜져 latency 튐.

#### 2.3.9 전원 (ASPM · L1 substates · D-state · runtime PM)

- **목적**: 저전력 상태 진입·탈출을 수만 번 반복해도 링크·기능·성능이 유지되나.
- **방법**: ASPM L0s/L1/L1.1/L1.2 각각 enable(sysfs `link/l1_aspm` 등) → idle/트래픽 교대 반복, D0↔D3hot(runtime PM `power/control=auto`), 시스템 suspend/resume. 탈출 latency 측정.
- **합격**: 복귀 후 DMA/인터럽트 정상, 링크 speed 유지, exit latency ≤ LnkCap 보고값, **resume 후 장치가 사라지지 않음**.
- **흔한 버그**: L1.2 탈출 시 CLKREQ# 처리, resume 후 config 복원 누락(LP15 #9), LTR 값 오류로 L1.2 진입 안 함.

#### 2.3.10 리셋 · 복구

- **목적**: 모든 리셋 종류 후 깨끗이 복구 + 커널/드라이버가 버티나.
- **방법**: FLR, SBR(hot reset), Link Disable, PERST#, surprise down(케이블/전원 제거), 트래픽 중 리셋. 커널 `reset_method` 순서 확인.
- **합격**: 리셋 후 열거·드라이버 재초기화 성공, `not ready … giving up` 없음, 커널 패닉/hung task 0.
- **흔한 버그**: 리셋 후 100 ms 대기 규칙 위반, endpoint가 RRS를 오래 보냄, sticky 레지스터(AER) 미초기화, DMA 진행 중 리셋 시 IOMMU fault.

#### 2.3.11 에러 경로 (fault injection)

- **목적**: 에러가 **보고되고, 분류되고, 복구되는지**. 실제 에러는 드물어서 일부러 만든다.
- **방법**: Linux `aer-inject`(CONFIG_PCIEAER_INJECT + `/dev/aer_inject`, 커널이 에러를 "받은 것처럼" 주입 — SW 경로만) [7], **컨트롤러 HW 주입**(DWC `rasdes_err_inj/tx_lcrc` 등 — 실제 LCRC 에러 → NAK/replay → AER) [8], exerciser로 잘못된 TLP, 에뮬레이터/QEMU 주입(LP13 §3.10에서 실측). DPC(Downstream Port Containment) 동작.
- **합격**: correctable → 카운터+로그만, non-fatal → 드라이버 `error_detected` 콜백 → 복구, fatal → 링크 리셋 후 복구 또는 깔끔한 장치 제거. 패닉 0.
- **흔한 버그**: 드라이버에 AER 콜백 없음 → 복구 실패, 에러 폭주 시 로그 폭주(ratelimit), sticky 상태 미클리어.

#### 2.3.12 Soak (장시간)

- **목적**: 시간·온도가 쌓여야 나오는 문제 (메모리 누수, 카운터 wrap, 열 drift).
- **방법**: 12–72시간 최대 부하 트래픽 + 주기적 스냅샷(`pcie_snapshot.py` 매 N분) + 온도 profile(챔버 ramp).
- **합격**: 링크 유지(BWMgmt/ABWMgmt 이벤트 0 = 자율 downtrain 없음), AER 기준 이하, 데이터 에러 0.
- **흔한 버그**: 고온에서 자율 speed 저하, 32-bit 카운터 wrap, 로그 파일이 디스크를 채움(하네스 버그!).

### 2.4 트리거(리셋) 종류별로 무엇을 검증하나

| 트리거 | Linux에서 | 링크 재학습? | 무엇을 검증 | 이번 랩 결과 |
|---|---|---|---|---|
| remove + rescan | `echo 1 > remove`, `echo 1 > /sys/bus/pci/rescan` | ❌ (링크는 L0 유지) | 열거·리소스·드라이버 probe 경로 | ✅ 30/30, 링크 그대로 (LP13 §3.11에서 LnkSta 2011 유지 확인) |
| FLR | `echo 1 > reset` (reset_method에 `flr`) | ❌ (함수 상태만) | 함수 리셋·드라이버 재초기화 | ✅ 30/30 |
| Secondary Bus Reset (hot reset) | `echo 1 > <bridge>/reset_subordinate` 또는 `reset_method=bus` | ✅ (Hot Reset → Detect부터) [추정: 실제 HW 기준] | 링크 재학습 + 장치 재초기화 | ✅ 30/30 (QEMU: 장치 리셋은 되나 LTSSM 에뮬레이션은 없음) |
| Link Disable 토글 | LnkCtl bit4 1→0 (`setpci`) | ✅ (Disabled → Detect) | 링크 down/up, surprise down 처리 | 🟡 하네스는 30/30이지만 **QEMU가 LD를 무시**함을 별도 probe로 확인(LnkSta 2011 유지) → 의미 없는 PASS |
| PERST# 토글 | 보드 GPIO / 컨트롤러 레지스터 (hook) | ✅ fundamental | 리셋 타이밍(CEM), 장치 FW 부팅 | [미실행] (hook 인터페이스만) |
| 전원 사이클 (cold) | PDU/릴레이 (hook) | ✅ 전체 | 전원 시퀀스, refclk, 장치 cold boot | [미실행] |
| warm reboot | `reboot` + 콘솔 대기 | ✅ (플랫폼 따라) | BIOS/부트로더/커널 전체 경로 | [미실행] |

- 면접 포인트: "**A thousand remove-rescan cycles proves the enumeration path, not link training.**" 이걸 구분하는 것 자체가 Staff 레벨 신호 [추정].
- 🟡 Link Disable 결과가 보여 주는 교훈 = §2.10 "테스트가 맞는지 어떻게 아나": 트리거가 **실제로 효과가 있었는지**(DLActive가 한 번이라도 내려갔는지)를 하네스가 확인해야 한다. → `link_cycle.py` 개선 TODO: 트리거 직후 DLActive- 관측 여부를 CSV에 기록 (§6).

### 2.5 통계 — 몇 번 돌려야 하나 (rule of three)

- 실패율 p인 사건을 독립적으로 N번 시도했을 때 **실패 0번일 확률 = (1−p)^N**.
- "실패 0번"을 봤을 때, 그럴 확률이 5% 이상인 p까지만 배제 못 한다 → (1−p)^N = 0.05 → **p_upper = 1 − 0.05^(1/N) ≈ −ln(0.05)/N = 2.996/N ≈ 3/N** (ln(1−p) ≈ −p 근사).
- 실패가 k번 있으면 **Clopper-Pearson**(정확한 이항 신뢰구간) 상한을 쓴다 — `link_cycle.py`가 이항 CDF를 bisection으로 풀어 계산.

| 상황 | 95% 상한 (스크립트 출력, 검증됨) | 말로 |
|---|---|---|
| 0 / 300 | 0.00994 (1 in 101) | "1% 미만이라고만 말할 수 있다" |
| 0 / 1,000 | 0.00299 (1 in 334) — 3/N = 0.00300 | "1/334 미만" |
| 2 / 1,000 | 0.00628 (1 in 159) | 관측 0.2%, 상한 0.63% |
| 2 / 2,000 (sim) | 0.00314 (1 in 318) | |
| 1/500을 주장하려면 | **1,497 무실패** (3/p = 1,500) | "수정 확인에 1,500 사이클" |
| 1/1000을 주장하려면 | 2,995 무실패 | |
| 1/500 버그를 95% 확률로 한 번 보려면 | ln(0.05)/ln(1−0.002) = 1496.4 → 올림 **1,497** 사이클 | "재현에만 1,500 사이클 필요" |

- **사이클 시간 → 일정**: 사이클당 30 s(전원 사이클+부팅)면 1,500 사이클 = 12.5 시간 → **보드 여러 대 병렬** + 밤샘 → 하네스가 필요한 이유. remove/rescan(수십 ms)은 빠르지만 다른 것을 검증한다(§2.4).
- 가정과 함정:
  - **독립성**: 같은 보드·같은 칩을 1,000번 = 그 보드에 대한 결론. 칩/보드 간 편차는 **여러 칩 × 여러 보드**로 나눠(stratify) 돌려야 한다. 실패가 특정 보드에 몰리면 그건 "율"이 아니라 "불량 보드".
  - **조건 고정**: 온도·전압이 변하면 p가 변한다 → 조건별로 따로 집계.
  - **BER에도 같은 공식**: 에러 0으로 BER < 1e-12를 95%로 보이려면 ≈ 3×10^12 비트. 16 GT/s 한 lane이면 3e12 / 16e9 ≈ **190초** (인코딩 무시 근사) — 그래서 BER 측정은 lane당 수 분 단위.
  - 다중 비교: 12개 영역 × 여러 corner → 하나쯤 우연히 튀는 것은 정상. 재실행 정책 필요 (단, flaky를 "재시도로 PASS"로 숨기면 안 됨 — 재시도 횟수를 결과에 기록).

### 2.6 Margining — Lane Margining at Receiver, eye/BER, shmoo

- **왜**: 링크가 "뜬다"는 것은 margin이 0보다 크다는 것뿐. 양산 편차·온도·노화 후에도 뜰지 예측하려면 **여유량**을 재야 한다.
- **Lane Margining at Receiver (LMR)** — pcilmr(8) man page 요지 [1] [확인됨 VM]:
  - **16.0 GT/s 이상 지원 포트는 필수**(리타이머 pseudo port 포함). 링크가 **L0인 상태에서** 수신기의 샘플링 지점을 시간/전압 방향으로 옮겨 보며 에러를 세어 margin을 보고 → 시스템을 멈추지 않고, HW 장비 없이 eye의 근사(마름모 4점)를 얻는다.
  - **timing margining 필수, voltage margining은 16 GT/s에서 선택 / 32 GT/s 이상에서 필수**.
  - extended capability ID 0x27 (`setpci`의 `ECAP_LMR`).
  - Linux 도구 = **`pcilmr`** (pciutils 3.11+ 포함, VM에 설치돼 있음 [확인됨 `/usr/bin/pcilmr`]): `pcilmr --scan`(가능한 링크 목록), `pcilmr <port BDF>`(한 링크), `pcilmr --full`. 요구 조건: 현재 링크 16 or 32 GT/s, downstream 장치 D0. 실행 중 ASPM off, HW autonomous speed/width disable을 **도구가 설정했다가 원복**.
  - 기본 판정값(PCIe Base 5.0 §8.4.2, PHY Test Spec 5.0 기준, man page 표): 16 GT/s 최소 EW 18.75 ps(30% UI)·EH 15 mV, 권장 23.75 ps·21 mV / 32 GT/s 최소 9.375 ps·15 mV, 권장 10.157 ps·19.75 mV. 제조사 기준이 있으면 그걸로 (`-g` 옵션).
  - 이번 랩에서는 실행 불가 [미실행] — QEMU 링크가 2.5 GT/s이고 LMR cap 없음.
- **eye / BER**: 진짜 eye는 BERT·고속 스코프 + 컴플라이언스 픽스처(CBB/CLB). LMR은 **실제 시스템 안 수신기 관점**이라 보드/채널 포함 margin을 대량으로(모든 보드, 모든 lane) 모으기 좋다.
- **shmoo**와 결합 (Don의 SK hynix 경험): 축 = 코어/PHY 전압 × 온도 (또는 EQ preset × 채널), 각 점 = "링크 루프 N회 PASS율 + LMR margin" → margin 지도. "벽(hard wall) vs 점진적 marginal 영역" 구분은 각 점을 **반복**해야 보인다.
- **관련 도구**: Meta/OCP **pcicrawler** — fleet 규모에서 PCIe 토폴로지 + **link speed/width mismatch** + AER(JSON) 수집하는 Python CLI [9]. 우리 `pcie_snapshot.py`가 같은 아이디어의 미니 버전.

### 2.7 Linux PCI Endpoint Framework — SoC를 테스트 상대로 (★ "real endpoint 없이 DMA 테스트")

- **아이디어**: PCIe 컨트롤러가 endpoint 모드를 지원하는 SoC(DWC 기반 다수 — 6.18 트리에 `pcie-designware-ep.c`, `pci-layerscape-ep.c`, `pcie-qcom-ep.c`, `pcie-rcar-ep.c`, `pcie-rockchip-ep.c` 등 [확인됨 소스])를 **Linux로 endpoint로 만들고**, 거기에 소프트웨어로 정의한 테스트 장치(`pci-epf-test`)를 올린다. 상대 호스트(x86 PC 또는 다른 SoC)는 `pci_endpoint_test` 드라이버로 BAR·IRQ·DMA 테스트를 돈다 [2][3][4].

```text
   HOST (Root Complex)                                   SoC-under-test (Endpoint mode)
 ┌────────────────────────────┐   PCIe link    ┌─────────────────────────────────────────┐
 │ pci_endpoint_test driver   │◄══════════════►│ EPC driver (e.g. pcie-designware-ep)    │
 │  /dev/pci-endpoint-test.0  │                │   └─ EPF: pci_epf_test (configfs로 생성) │
 │ kselftest pci_endpoint_test│  BAR0 test regs│      BAR0: MAGIC/COMMAND/STATUS/SRC/DST/│
 │  (BAR, INTx/MSI/MSI-X,     │                │            SIZE/CHECKSUM/IRQ_TYPE/IRQ_NUM│
 │   READ/WRITE/COPY,         │  DMA (memcpy   │      명령 받으면 호스트 버퍼를 읽고/쓰고 │
 │   memcpy & DMA)            │   or eDMA)     │      CRC32 체크섬으로 검증               │
 └────────────────────────────┘                └─────────────────────────────────────────┘
```

- **EP 쪽 설정 순서** (`Documentation/PCI/endpoint/pci-test-howto.rst`, 6.18.55 [확인됨 문서]):

```bash
# [미실행 - QEMU virt에는 endpoint controller(EPC)가 없음]  EP(SoC) 쪽
ls /sys/class/pci_epc/                          # 예: 51000000.pcie_ep
mount -t configfs none /sys/kernel/config
cd /sys/kernel/config/pci_ep/
mkdir functions/pci_epf_test/func1              # pci_epf_test 장치 생성
echo 0x104c > functions/pci_epf_test/func1/vendorid
echo 0xb500 > functions/pci_epf_test/func1/deviceid
echo 32     > functions/pci_epf_test/func1/msi_interrupts
echo 2048   > functions/pci_epf_test/func1/msix_interrupts
ln -s functions/pci_epf_test/func1 controllers/51000000.pcie_ep/   # EPC에 bind
echo 1 > controllers/51000000.pcie_ep/start      # 링크 시작
```

```bash
# [미실행] HOST 쪽
lspci                                  # 01:00.0 Unassigned class [ff00]: ... Device b500
modprobe pci_endpoint_test             # /dev/pci-endpoint-test.0
make -C tools/testing/selftests/pci_endpoint      # 커널 트리에서 빌드
./pci_endpoint_test                    # TAP: BAR0-5, CONSECUTIVE_BAR, LEGACY/MSI/MSIX IRQ, memcpy/dma READ/WRITE/COPY
./pci_endpoint_test -f pci_ep_bar -f pci_ep_basic -v memcpy -T COPY_TEST -v dma   # DMA memcpy 없는 컨트롤러는 dma COPY 제외
```

- 주의 — **`pcitest` 도구 상태**: 예전 문서/블로그의 `tools/pci/pcitest`는 6.18.55 소스 트리에 **없다**(`tools/pci` 디렉터리 부재) — 대신 **kselftest `tools/testing/selftests/pci_endpoint/pci_endpoint_test.c`** 가 같은 uapi(`include/uapi/linux/pcitest.h`: `PCITEST_BAR`, `PCITEST_MSI`, `PCITEST_MSIX`, `PCITEST_READ/WRITE/COPY`, `PCITEST_SET_IRQTYPE`, `PCITEST_DOORBELL`…)를 쓴다 [확인됨 소스]. 면접에서는 "pci-epf-test with the host pci_endpoint_test driver and its kselftest" 로 말하면 안전.
- 테스트 장치 레지스터 (pci-test-function.rst [3]): COMMAND bit0 legacy IRQ / bit1 MSI / bit2 MSI-X / bit3 read / bit4 write / bit5 copy, STATUS bit0–5 각 성공/실패, bit6 IRQ raised, bit7–8 주소 invalid.
- 랩 커널 설정: `CONFIG_PCI_ENDPOINT=y`, `CONFIG_PCI_ENDPOINT_CONFIGFS=y`, `CONFIG_PCI_EPF_TEST=m`, `CONFIG_PCI_ENDPOINT_TEST=m` [확인됨 `.config`] — 하지만 QEMU `virt`에는 EPC가 없어서 실행은 실제 보드 필요.
- 쓰임: **SoC 대 SoC**(같은 칩 두 개를 back-to-back — RC 모드와 EP 모드 동시 검증), **SoC 대 x86**(x86을 "검증된 파트너"로), 자동차 칩간 PCIe(LP17) 검증.

### 2.8 FPGA 테스트 파트너 — XDMA / QDMA

- **왜**: endpoint framework는 SoC가 EP 모드를 지원해야 하고 DMA 성능/에러 주입이 제한적. **FPGA 카드**(예: AMD UltraScale+/Versal 보드)를 endpoint로 쓰면 원하는 트래픽·에러·지연을 만들 수 있다.
- AMD(Xilinx) IP:
  - **DMA/Bridge Subsystem for PCIe (XDMA, PG195)** [10]: H2C(host→card)/C2H(card→host) 채널 최대 4개씩, scatter-gather descriptor, AXI-MM/AXI-Stream, 사용자 IRQ. 드라이버: Xilinx `dma_ip_drivers` (`/dev/xdma0_h2c_0`, `/dev/xdma0_c2h_0` 문자 장치 + 테스트 스크립트) [11], 그리고 mainline `drivers/dma/xilinx/xdma.c`(dmaengine 플랫폼 드라이버) [확인됨 6.18 소스 존재].
  - **QDMA (PG302)**: 큐 기반(수천 개 큐), SR-IOV — 고성능/가상화 검증용 [추정 세부].
  - **UltraScale+ Integrated Block for PCIe (PG213)**: 하드 PCIe 블록 자체.
- 대표 셋업: FPGA에 XDMA + BRAM/DDR 루프백 → 호스트(우리 SoC, RC 모드)가 H2C로 패턴 쓰고 C2H로 읽어 비교 → **크기/정렬/동시 채널 sweep**. FPGA 쪽 ILA(내부 로직 분석기)로 TLP 수준 디버그 가능.
- Zynq UltraScale+ 자체도 PS-GTR PCIe(Gen2, RC/EP)와 PL 쪽 PCIe 블록을 가짐 → LP16.
- 상용 대안: PCIe **exerciser**(규격 위반 TLP, 에러 주입, LTSSM 강제), **protocol analyzer**.

### 2.9 자동화 하네스 아키텍처

```text
 ┌──────────────────────────────── HOST (Python, Linux PC / CI runner) ────────────────────────────────┐
 │  Scheduler (Jenkins/LAVA/pytest)  ──►  Test runner (per board)                                     │
 │     job = {build hash, test set, board type, conditions}      │                                    │
 │                                                                ├─ power.cycle()   ── PDU / relay / lab PSU (SCPI)
 │                                                                ├─ perst.toggle()  ── GPIO / debug MCU
 │                                                                ├─ console.expect() ── UART (pyserial), timeout→power cycle
 │                                                                ├─ jtag.load()     ── Trace32 / OpenOCD (bare-metal 이미지)
 │                                                                ├─ thermal.set()   ── 챔버 / thermal forcer
 │                                                                └─ dut.run(test)   ── SSH (Linux) 또는 UART 명령 (bare-metal)
 │                                                                         │ RESULT lines / JSON
 │  On FAIL hook: dmesg, lspci -vvxxxx, sysfs snapshot, controller LTSSM regs, analyzer trigger ──┐  │
 │                                                                         ▼                        ▼  │
 │  Results DB (SQLite/Postgres):  run_id, test_id, cycle, verdict, reason, chip_id/lot, board_id,     │
 │     fw/kernel/build hash, V/T/F, link speed/width, t_up, AER deltas, log artifact paths             │
 │                                                                         │                           │
 │  Regression compare: vs last good build / same board → new FAIL? rate change? (Clopper-Pearson CI)  │
 │  Triage: dedup by failure signature (reason string + LTSSM state) → tracker ticket, auto-bisect     │
 │  Reports: plan coverage (requirement → test → status), trend per build, shmoo / margin maps         │
 └─────────────────────────────────────────────────────────────────────────────────────────────────────┘
         ▲ board farm: N boards × (PDU outlet, UART, JTAG, Ethernet, PERST# GPIO, temp sensor)
```

- 설계 원칙:
  1. **보드 헬스 체크 먼저** — 망가진 보드가 테스트 실패로 보이면 안 된다 (deck §6.5).
  2. **모든 결과에 조건 태그** — chip ID, board ID, build hash, 온도, 전압 → 나중에 PVT/SLT로 재사용.
  3. **실패 순간 증거 수집** — 상태가 사라지기 전에(`--on-fail` 훅). 재부팅 후엔 LTSSM 상태·AER 로그가 사라진다.
  4. **타임아웃은 계층별로** — 링크 대기(초), 테스트(분), 잡(시간). hang이면 전원 사이클로 복구하고 "hang"으로 분류.
  5. **결정론** — 시드, 테스트 순서, 설정 전부 기록 → 재현 가능.
  6. **회귀 비교는 통계로** — "어제 0/1000, 오늘 1/1000"은 유의하지 않을 수 있다 → 신뢰구간 겹침을 본다.

### 2.10 "1-in-500 failure" 디버그 접근

1. **특성화 먼저(재현율을 숫자로)**: 1,500 사이클 이상으로 "실패율 ≈ 1/500"을 확정 + **실패 시그니처 분류**(timeout? downtrain? AER burst? 어느 LTSSM 상태에서 멈춤?).
2. **상관관계 찾기**: 보드별·칩별·온도별·리셋 종류별·부팅 후 시간별로 실패를 나눠 본다. 특정 보드에 몰리면 HW, 모든 보드에 균일하면 설계/FW 타이밍 쪽 [추정 휴리스틱].
3. **실패를 잡을 계측 배치**: 하네스 `--on-fail`에서 LTSSM 상태 레지스터·AER·dmesg 즉시 덤프 → 애널라이저/스코프를 **실패 조건에서 트리거**(예: 링크 업 타임아웃 GPIO를 스코프 트리거로) → PERST#·refclk·전원 동시 캡처.
4. **확률을 올리는 스트레스**: 온도↑, 전압↓, 리셋 간격 단축, Gen 높이기 → 1/500이 1/20이 되면 디버그가 25배 빨라진다 (단, 같은 메커니즘인지 시그니처로 확인).
5. **bisect**: Gen1 강제(SI vs 로직), 다른 파트너 장치, ASPM off, 리셋 후 대기 시간 늘리기, FW 초기화 순서 변경 → 각 변경마다 **충분한 N**으로 (실패 0/200은 1/500 문제에선 아무 의미 없음!).
6. **근본 원인 + 수정 확인**: 수정 후 **≥1,500 무실패**(1/500 미만을 95%로) + 원래 스트레스 조건에서도.
7. **회귀 테스트로 고정**: 그 시그니처를 잡는 체크를 nightly에 추가.

### 2.11 테스트 자체가 맞는지 어떻게 아나 (★ 꼬리 질문 단골)

| 방법 | 내용 | 이번 노트에서 한 것 |
|---|---|---|
| **Negative control** | 일부러 실패해야 하는 조건(틀린 기대값, 장치 없는 슬롯)에서 FAIL이 나오는지 | ✅ QEMU에서 Gen4 x4 기대 → 5/5 FAIL (§3.3) |
| **Fault injection** | 알려진 결함을 넣고 검출되는지 | ✅ sim 모드 실패율 주입 → 5종 실패 모드 모두 검출 (§3.2), LP13 AER 주입 → 스냅샷이 잡음 |
| **트리거 효과 확인** | 리셋이 실제로 링크를 내렸나(DLActive 관측) | ✅ QEMU Link Disable 무효를 probe로 발견 → 🟡 처리 |
| Known-good / known-bad 하드웨어 | 골든 보드 + 일부러 망가뜨린 보드(lane 하나 끊은 케이블 등) | [미실행] |
| 독립 측정과 교차 확인 | SW 판정 vs 애널라이저/스코프, SW 타이머 vs HW 타임스탬프 | [미실행] — `t_up`은 TCG에서 polling 오버헤드 포함(~20–30 ms)이라 절대값 의미 없음 |
| 통계 sanity | 시뮬레이션에서 알려진 p로 돌려 추정값이 맞는지 | ✅ p=0.002, N=2000 → 2건(기대 4, 포아송 범위 내), 상한 0.00314 ≥ 0.002 |
| 리뷰·추적성 | 요구사항 ID ↔ 테스트 ↔ 판정 기준 표, 설계자 리뷰 | 테스트 플랜 표(§2.3) |
| 측정 오버헤드 | 하네스 polling이 시스템을 바꾸지 않나 (예: config read가 ASPM 진입 방해) | 기록만 [추정 리스크] |

- 영어 핵심: "A test that has never failed hasn't been tested. I always run a negative control and inject faults to prove the harness catches them, and I check that the trigger actually did something — in my QEMU lab the link-disable toggle passed 30 out of 30, but a separate probe showed the emulator ignored the bit, so that PASS meant nothing."

### 2.12 Bare-metal vs Linux — 검증에서 어떻게 나누나 (LP16 예고)

| | Bare-metal C | Linux |
|---|---|---|
| 잘하는 것 | 레지스터 수준 제어, 정확한 타이밍, 리셋/LTSSM polling, 부팅 직후, pre-silicon과 같은 코드 | 실제 드라이버 스택, 장시간 soak, 성능(fio), 에러 복구 경로, 많은 장치/토폴로지 |
| 단점 | 열거·드라이버를 직접 써야 함 | 타이밍 비결정성, 커널이 상태를 바꿈(AER W1C, ASPM 정책) |
| 이 직무 | 주력 (JD "mainly bare-metal C") [추정 — 리크루터/스크린 정보] | 시스템 통합·soak·고객 시나리오 |

### 2.13 Ethernet / MIPI 비교 (짧게)

| 검증 항목 | PCIe | Ethernet | MIPI CSI-2 / D-PHY |
|---|---|---|---|
| 링크 업 루프 | LTSSM → DLActive, N 사이클 | auto-negotiation → link up, N 사이클 (`ethtool`) | LP→HS 전환, 프레임 수신 시작 |
| 강제 조합 | Target Link Speed × width | speed/duplex 강제 | lane 수 × data rate (DT) |
| 신호 품질 지표 | AER RxErr, LMR margin | FCS/CRC 에러, PHY SQI, PRBS 루프백 | ECC/CRC 에러, HS settle 마진 |
| 테스트 파트너 | pci-epf-test, FPGA XDMA | 루프백 플러그, 트래픽 생성기(RFC 2544) | 센서 에뮬레이터, 패턴 생성기 |
| BER 통계 | rule of three (bits) | 같음 (패킷/비트) | 같음 (프레임/비트) |

## 3. Linux에서 볼 것 — 실제 실행 결과

> 2026-10-31 실제 실행. ① macOS(이 Mac) `--sim` ② Lima VM 안 QEMU 게스트(kernel 6.18.55-trace, QEMU NVMe at 01:00.0 under root port 00:01.0)에서 **sysfs 백엔드**. QEMU 랩 구성은 [LP13 §3](2026-10-31_LP13_linux_pcie_tools.html) 과 같은 [run_qemu.sh](onsitePrep/lp13/lab/run_qemu.sh).

### 3.1 통계 계산 (macOS, 실제)

```text
$ python3 link_cycle.py --stats 0 1000
failures 0 / 1000 cycles = 0.0000%
95% upper bound on failure rate: 0.00299 (= 1 in 334)
rule of three check: 3/N = 0.00300, exact 1-0.05^(1/N) = 0.00299
$ python3 link_cycle.py --stats 2 1000
failures 2 / 1000 cycles = 0.2000%
95% upper bound on failure rate: 0.00628 (= 1 in 159)
$ python3 link_cycle.py --need 0.002
zero-failure cycles to claim p < 0.002 at 95%: 1497 (3/p = 1500)
```

- 독립 확인 (별도 python 한 줄): `1-0.05**(1/1000) = 0.0029912`, `ln(0.05)/ln(1-1/500) = 1496.37` → 스크립트와 일치. 2/1000 상한 0.00628은 포아송 근사 6.30/1000과도 일치.

### 3.2 시뮬레이션 모드 — 하네스가 실패를 잡는지 (macOS, 실제)

```text
$ python3 link_cycle.py --sim --cycles 2000 --sim-fail-rate 0.002 --seed 1 \
      --expect-speed 16 --expect-width 4 --csv outputs/sim_2000.csv \
      --on-fail 'echo "  [on-fail hook] collect dmesg/lspci for cycle {cycle}"'
  [on-fail hook] collect dmesg/lspci for cycle 578
cycle   578 FAIL  timeout: present=False DLActive=False Train=True
  [on-fail hook] collect dmesg/lspci for cycle 1578
cycle  1578 FAIL  timeout: present=False DLActive=False Train=True
----------------------------------------------------------------
method=sim backend=sim cycles=2000
failures 2 / 2000 cycles = 0.1000%
95% upper bound on failure rate: 0.00314 (= 1 in 318)
first failure at cycle 578
link-up time ms: p50=6.34 p99=10.18 max=11.19

$ python3 link_cycle.py --sim --cycles 200 --sim-fail-rate 0.05 --seed 7 --expect-speed 16 --expect-width 4 --timeout 0.2
cycle     5 FAIL  timeout: present=False DLActive=False Train=True
cycle    52 FAIL  AER correctable +23 > 0
cycle    98 FAIL  width x2 != x4
cycle   127 FAIL  speed 2.5 != 16.0
...
failures 9 / 200 cycles = 4.5000%
95% upper bound on failure rate: 0.07721 (= 1 in 13)
```

| 관찰 | 의미 |
|---|---|
| 실제 p=0.002인데 관측 2/2000=0.1% | 운이 좋은 쪽(기대 4건). 그래도 상한 0.314%는 진짜 p를 포함 → 신뢰구간이 제 역할 |
| 실패 모드 5종 모두 이유 문자열로 분류 | timeout(링크 안 뜸) · speed downtrain · width downtrain · AER burst · (slow train은 timeout으로) → **시그니처별 집계** 가능 |
| `on-fail` 훅이 실패 사이클 번호와 함께 즉시 실행 | 실 장비에서는 `dmesg`, `lspci -vvxxxx`, LTSSM 레지스터 덤프 |
| CSV: `cycle,verdict,t_up_ms,speed_gts,width,dl_active,aer_cor_delta,reason` | DB에 그대로 적재 |

### 3.3 QEMU 게스트 — 실제 sysfs 백엔드, 4가지 트리거 × 30 사이클 (실제)

```text
# cat /sys/bus/pci/devices/0000:01:00.0/reset_method
flr bus

##### method=remove-rescan
method=remove-rescan backend=sysfs cycles=30
failures 0 / 30 cycles = 0.0000%
95% upper bound on failure rate: 0.09503 (= 1 in 11)
rule of three check: 3/N = 0.10000, exact 1-0.05^(1/N) = 0.09503
link-up time ms: p50=22.35 p99=29.27 max=29.27
cycle,verdict,t_up_ms,speed_gts,width,dl_active,aer_cor_delta,reason
1,PASS,19.97,2.5,1,True,0,

##### method=flr       ... failures 0 / 30 ... link-up time ms: p50=25.29 p99=30.68
##### method=sbr       ... failures 0 / 30 ... link-up time ms: p50=29.85 p99=32.29
##### method=link-disable ... failures 0 / 30 ... link-up time ms: p50=30.81 p99=33.37

##### negative control: expect Gen4 x4 on a Gen1 x1 link - harness MUST fail
cycle     1 FAIL  speed 2.5 != 16.0; width x1 != x4
...
cycle     5 FAIL  speed 2.5 != 16.0; width x1 != x4
failures 5 / 5 cycles = 100.0000%

##### probe: does QEMU drop the link on LnkCtl.LinkDisable?
before: LnkSta=2011
LD=1:   LnkSta=2011 EP vendor=1b36
LD=0:   LnkSta=2011 EP vendor=1b36
```

| 관찰 | 해석 |
|---|---|
| 4가지 모두 30/30 PASS, 상한 9.5% | N=30으로는 "10% 미만"밖에 말 못 한다 — 짧은 smoke test일 뿐 |
| `reset_method = flr bus` | `--method flr`의 `echo 1 > reset`은 FLR을 먼저 시도 |
| remove-rescan 사이클마다 dmesg에 `[1b36:0010] type 00 … BAR 0 … assigned … nvme nvme0: pci function` | 재열거 + 드라이버 probe가 매번 실제로 일어남 (로그: [qemu_link_cycle_log.txt](onsitePrep/lp14/outputs/qemu_link_cycle_log.txt)) |
| negative control 5/5 FAIL + 이유 정확 | 판정 로직이 실제 sysfs 값으로 동작함을 증명 |
| Link Disable probe: LD=1에도 `LnkSta=2011`, vendor 읽힘 | **QEMU는 Link Disable을 에뮬레이트하지 않음** → link-disable 30/30 PASS는 "트리거 무효"인 가짜 PASS. §2.11 교훈 |
| `t_up` 20–33 ms | TCG 에뮬레이션에서 Python config read 오버헤드 포함 — 절대값 의미 없음, 실 HW에서는 스코프/HW 타임스탬프로 교차 확인 |

### 3.4 실 장비에서 사이클마다 수집할 것 (체크리스트)

```bash
# 사이클 전/후 (root)
P=/sys/bus/pci/devices/0000:00:01.0; D=/sys/bus/pci/devices/0000:01:00.0
cat $D/current_link_speed $D/current_link_width          # 협상 결과
setpci -s 00:01.0 CAP_EXP+12.w                             # LnkSta raw (DLActive bit13, Train bit11)
setpci -s 00:01.0 CAP_EXP+32.w                             # LnkSta2 (EQ phases)
cat $P/aer_dev_correctable $P/aer_rootport_total_err_cor   # AER 누적 → delta
cat /sys/kernel/debug/dwc_pcie_*/ltssm_status 2>/dev/null  # DWC SoC면 LTSSM 상태
dmesg --since "-1min" | grep -iE "pci|aer|link"            # util-linux dmesg 옵션 [추정: 버전 따라]
python3 pcie_snapshot.py --out snap_cycle_$N.json          # LP13 스냅샷
```

## 4. 꼬리 질문 체인

### 체인 A — "How would you validate a PCIe controller on our new SoC?" (LP00 원본)

Q → 꼬리1 "What's your pass criterion for link training?" → 꼬리2 "How many cycles and why?" → 꼬리3 "A 1-in-500 failure shows up — how do you debug it?" → 꼬리4 "How do you know the test itself is right?" → 꼬리5 "How do you prove the fix?"

<details>
<summary>A-Q. How would you validate a PCIe controller on our new SoC?</summary>

- **EN**: "I'd build the plan from the controller's feature list and the spec, with numeric pass criteria per area. Twelve areas: link bring-up loops across all reset types, the speed-and-width matrix, equalization, signal quality with AER counters and lane margining at PVT corners, enumeration and resources, every interrupt mode, DMA integrity across sizes and alignments, throughput against theoretical peak, power-state transitions, reset and recovery, error injection, and soak. Tests are written once in C with a thin HAL, run on the FPGA prototype first, and on silicon they're driven by an automated harness that logs every result with chip ID and conditions."
- 한국어 핵심: 12개 영역 + 숫자 기준 + 같은 테스트 FPGA→실리콘 + 하네스.
</details>

<details>
<summary>A-꼬리1. What's your pass criterion for link training?</summary>

- **EN**: "Every cycle, within a time limit, the downstream port shows DLActive set and Train clear, the negotiated speed and width equal the expected value — the minimum of both sides' capabilities — equalization phases completed for Gen3 and above, and zero new AER errors during training. I also record the link-up time distribution, because a growing tail is an early sign of lost margin. And the criterion includes the trigger type — cold power cycle, PERST#, hot reset — because they exercise different paths."
- 한국어 핵심: 매 사이클 DLActive+, Train-, 기대 speed/width, EQ 완료, AER delta 0, 시간 제한 + 분포, 트리거 종류 명시.
</details>

<details>
<summary>A-꼬리2. How many cycles, and why?</summary>

- **EN**: "It comes from the failure rate I need to rule out. With zero failures in N trials, the 95% upper bound is about three over N — the rule of three — because (1 minus p) to the N equals 0.05 gives p roughly 3 over N. So a thousand clean cycles only proves the rate is below about 1 in 330. If the target is 1 in 1,000, I need about 3,000 clean cycles, spread over several boards and chips so I'm not just characterizing one unit. Then I budget the time — at 30 seconds per power cycle that's a day per board, which is why the harness runs boards in parallel."
- 한국어 핵심: (1−p)^N=0.05 → p≈3/N. 1/1000 목표면 3,000, 여러 보드로 분산, 시간 예산.
</details>

<details>
<summary>A-꼬리3. A 1-in-500 failure shows up. How do you debug it?</summary>

- **EN**: "First quantify it and classify the signature — timeout versus downtrain versus error burst, and which LTSSM state it's stuck in, captured automatically at the moment of failure. Then correlate: does it follow a board, a chip, temperature, the reset type, time since power-on? Then I try to raise the rate with stress — hotter, lower voltage, faster reset cadence — and confirm it's the same signature, because debugging at 1 in 20 is 25 times faster. With the analyzer and scope triggered on the failure, I bisect: force Gen1, swap the partner, disable ASPM, lengthen the post-reset delay. And every bisect step needs enough cycles — zero out of 200 means nothing for a 1-in-500 bug."
- 한국어 핵심: 정량화·시그니처 → 상관관계 → 스트레스로 재현율↑ → 계측 트리거 → bisect(각 단계 충분한 N).
</details>

<details>
<summary>A-꼬리4. How do you know the test itself is right?</summary>

- **EN**: "I make it fail on purpose. A negative control — expect Gen4 x4 on a link that's Gen1 x1 — has to fail every cycle, and injected faults have to be caught with the right reason. I also verify that the trigger actually did something: in my QEMU lab a link-disable loop passed 30 out of 30, but a separate probe showed the emulator ignored the Link Disable bit, so the PASS was meaningless. On hardware I'd cross-check the software verdict against the analyzer or scope for a sample of cycles, and run known-good and known-bad boards."
- 한국어 핵심: negative control, fault injection, 트리거 효과 확인(QEMU LD 사례), HW 교차 확인, 골든/불량 보드.
</details>

<details>
<summary>A-꼬리5. You fixed it. How do you prove the fix?</summary>

- **EN**: "Same statistics in reverse: to claim the rate is now below 1 in 500 at 95% confidence I need about 1,500 clean cycles, ideally under the stress condition that made it reproducible, and on the boards that failed before. Then the check that catches that signature goes into the nightly regression so it can't come back silently."
- 한국어 핵심: 1/500 미만 주장 = 1,500 무실패 + 스트레스 조건 + 실패했던 보드 + 회귀 테스트 고정.
</details>

### 체인 B — "How do you test DMA when you don't have a real endpoint device?"

Q → 꼬리1 "How does the endpoint test function work?" → 꼬리2 "What if the SoC can't do endpoint mode?" → 꼬리3 "What DMA cases matter most?"

<details>
<summary>B-Q. How do you test DMA without a real endpoint device?</summary>

- **EN**: "Three options. If the SoC's controller supports endpoint mode, Linux's PCI endpoint framework lets me turn a second board into a test device with pci-epf-test, configured through configfs, and the host runs the pci_endpoint_test driver and its kselftest for BAR, interrupt and DMA read, write and copy tests with checksums. Otherwise an FPGA card with a DMA IP like AMD's XDMA acts as a programmable partner with loopback memory. And for some paths a controller loopback mode or an exerciser."
- 한국어 핵심: endpoint framework(pci-epf-test + pci_endpoint_test) / FPGA XDMA 루프백 / loopback·exerciser.
</details>

<details>
<summary>B-꼬리1. How does pci-epf-test work?</summary>

- **EN**: "On the endpoint side I create the function in configfs, set vendor and device IDs and the number of MSI and MSI-X vectors, link it to the endpoint controller and write start. It exposes a BAR with test registers — magic, command, status, source and destination address, size, checksum, IRQ type. The host driver writes a command, like 'read this many bytes from this host buffer', the endpoint does it with memcpy or its DMA engine and reports a CRC, and the host compares. Note the old pcitest tool is gone in recent kernels; the kselftest pci_endpoint_test replaces it."
- 한국어 핵심: configfs 생성→bind→start, BAR0 테스트 레지스터, 명령/CRC, kselftest가 pcitest 대체.
</details>

<details>
<summary>B-꼬리2. What if the SoC can't run in endpoint mode?</summary>

- **EN**: "Then an FPGA partner. With XDMA, the host-to-card and card-to-host channels move data between host memory and FPGA memory; I write a pattern, read it back, compare, and sweep size, alignment, channel count. The big advantage is that I can also make the FPGA misbehave on purpose — delay completions, inject errors — which a real device won't do on demand."
- 한국어 핵심: XDMA H2C/C2H 루프백, sweep, 일부러 오동작(지연·에러) 가능.
</details>

<details>
<summary>B-꼬리3. Which DMA cases matter most?</summary>

- **EN**: "The boundaries: transfers that cross a 4 KB boundary, because requests must not cross it and the controller has to split them; unaligned starts and ends; sizes around the max payload and max read request; scatter-gather chains; concurrent channels; and on ARM, non-coherent buffers where a missing cache clean or invalidate corrupts data only sometimes. Each with a pattern and a CRC, and with the IOMMU on and off."
- 한국어 핵심: 4 KiB 경계, 비정렬, MPS/MRRS 경계, SG, 동시성, non-coherent 캐시, IOMMU on/off.
</details>

### 체인 C — "How does this differ between bare-metal and Linux?" (→ LP16)

<details>
<summary>C-Q. Would you validate PCIe bare-metal or under Linux?</summary>

- **EN**: "Both, for different things. Bare-metal C gives me deterministic control right after reset — polling the LTSSM register, timing PERST#, forcing speeds — and it's the same code I ran pre-silicon. Linux gives me the real driver stack, long soak, performance with real workloads, and the error-recovery path through AER. So bring-up, link-training loops and register-level tests are bare-metal; system integration and soak are Linux."
- 한국어 핵심: bare-metal = 결정론·레지스터·pre-silicon 동일 코드 / Linux = 드라이버·soak·성능·복구.
</details>

<details>
<summary>C-꼬리1. What does Linux get in your way?</summary>

- **EN**: "It changes state behind you: the AER driver clears status registers, ASPM policy changes link power states, runtime PM can put the device in D3. So in validation I pin those — ASPM policy, runtime PM off where needed — and read counters rather than status bits."
- 한국어 핵심: 커널이 상태를 바꿈(AER W1C, ASPM, runtime PM) → 고정하고 카운터를 읽는다.
</details>

### 체인 D (추가) — "How do you measure margin, not just pass/fail?"

Q → 꼬리1 "What's lane margining?" → 꼬리2 "How long to show BER 1e-12?" → 꼬리3 "How would you present it?"

<details>
<summary>D-Q. How do you measure margin instead of just pass/fail?</summary>

- **EN**: "By pushing the link toward failure in a controlled way and measuring how far it is. On the system side, Lane Margining at the Receiver — mandatory from 16 GT/s — lets software move the sampling point in time and voltage and count errors per lane while the link stays in L0; pciutils' pcilmr does that. On the platform side I sweep voltage and temperature, and equalization presets, and repeat link loops at each point. That gives a shmoo map instead of a single PASS."
- 한국어 핵심: LMR(16 GT/s+ 필수, L0 중, lane별) + pcilmr + PVT/preset sweep → shmoo.
</details>

<details>
<summary>D-꼬리1. What exactly does lane margining report?</summary>

- **EN**: "For each lane, how many steps it can move the sampler left and right in time, and up and down in voltage, before the error count exceeds a limit — timing is required, voltage is optional at 16 GT/s and required at 32 and above. pcilmr turns that into eye width and height and grades it against reference values from the spec — for 16 GT/s the minimum is about 30 percent of a UI and 15 millivolts."
- 한국어 핵심: lane별 시간/전압 step margin, 16 GT/s 최소 EW 30% UI·EH 15 mV (pcilmr 기본값).
</details>

<details>
<summary>D-꼬리2. How long do you need to run to show a BER of 1e-12?</summary>

- **EN**: "Same rule of three, applied to bits: with zero errors I need about three times ten to the twelve bits for 95% confidence. At 16 gigatransfers per second on one lane that's roughly 190 seconds. So per-lane BER checks are minutes, and that's why margin scans are done at a few points rather than everywhere."
- 한국어 핵심: 3e12 비트 / 16e9 ≈ 190 s per lane.
</details>

<details>
<summary>D-꼬리3. How would you present the results?</summary>

- **EN**: "A margin table per lane, per board, per corner, with the limit lines, plus a shmoo plot for the sweeps — and the worst case called out with the conditions, because design cares about the worst lane on the worst board at the worst corner, not the average."
- 한국어 핵심: lane×보드×corner 표 + shmoo, 최악 조건을 강조.
</details>

### 체인 E (추가) — "Design the automation for this."

Q → 꼬리1 "What happens when a board hangs?" → 꼬리2 "How do you avoid flaky results hiding bugs?"

<details>
<summary>E-Q. Design the automation harness for PCIe validation.</summary>

- **EN**: "A Python host per board farm: each board has a network PDU outlet, a UART console, JTAG, a GPIO for PERST#, Ethernet, and a temperature sensor. A scheduler runs jobs defined by build hash, test set and board type. Each job starts with a board health check, then loops the test: trigger, wait with timeout, collect link state and counters, judge, and on failure collect dmesg, an lspci dump and controller registers immediately. Every result goes into a database tagged with chip ID, board, build, voltage and temperature, and a regression step compares failure rates with confidence intervals against the last good build."
- 한국어 핵심: 보드별 PDU/UART/JTAG/PERST#/온도, 헬스 체크, 루프+즉시 로그, DB 태그, 신뢰구간 비교.
</details>

<details>
<summary>E-꼬리1. What happens when a board hangs?</summary>

- **EN**: "Every layer has a timeout. If the console goes quiet past the limit, the harness power-cycles the board, records the cycle as a hang with whatever was captured, runs the health check, and continues — a hang is a result, not a crash of the harness. If the health check fails twice, the board is quarantined so it doesn't pollute the statistics."
- 한국어 핵심: 계층별 timeout → 전원 사이클 → "hang"으로 기록 → 헬스 체크 실패 시 격리.
</details>

<details>
<summary>E-꼬리2. How do you avoid flaky results hiding real bugs?</summary>

- **EN**: "Retries are allowed but always recorded — a test that passes on retry is reported as flaky, never as a clean pass, and the flaky rate per test is trended. Failures are deduplicated by signature so one new signature stands out even among known flaky ones."
- 한국어 핵심: 재시도는 기록, flaky ≠ PASS, 시그니처로 dedup.
</details>

## 5. Don 경험 연결

- "At SK hynix I built the chamber reliability test platform — scheduling, temperature control, UART sequencing and result collection — which is the same harness architecture: automate the stress, tag every result with conditions, and collect evidence on failure. We also built shmoo and margin features for the NAND and PCIe 3/4/5 high-speed interfaces, repeating each point to separate a hard wall from a marginal region." `[숫자 채우기: 챔버 수 / 동시 DUT / 엔지니어 사용자 수 / 발견한 margin 이슈 1건]`
- "At Solidigm, on the PCIe Gen6 SSD controller, we brought up Cortex-R and M cores and the PCIe/NVMe subsystem IP on FPGA before RTL freeze and verified I2C, SPI, DMA, SRAM and DRAM blocks with bare-metal tests — so on silicon we were debugging only what FPGA couldn't show: PHY, timing and margin." `[숫자 채우기: RTL freeze 전 발견 버그 수]`
- "At Apple I designed factory test nodes around stress scenarios to expose latent defects before shipment — that's the SLT end of this same flow — and I root-caused PCIe and other interface failures with a scope and a protocol analyzer." `[숫자 채우기: 사례 1개]`
- 통계 문장 (Don 수학 배경): "I'm careful with 'it passed a thousand times' — with the rule of three that only bounds the failure rate below about 1 in 330 at 95% confidence."

## 6. 실습 / 코드

- **[link_cycle.py](onsitePrep/lp14/link_cycle.py)** — 링크 트레이닝 스트레스 하네스 골격 (Python 3, 외부 모듈 없음)
  - 백엔드 2개: `SysfsBackend`(실 Linux: `remove-rescan` / `flr` / `sbr`(`reset_subordinate`) / `link-disable`(LnkCtl bit4 토글 + rescan) / `hook`(PDU·PERST# 등 외부 명령)), `SimBackend`(`--sim`/`--dry-run`: 5가지 실패 모드를 확률로 주입, `--seed`로 결정론).
  - 루프: `trigger()` → `wait_link()`(present + DLActive + Vendor ID 읽힘까지 polling, timeout) → `judge()`(speed/width/AER delta) → CSV 한 줄 → 실패 시 `--on-fail` 명령(`{cycle}` 치환) → `--stop-on-fail` 옵션.
  - 요약: 실패율, **Clopper-Pearson 95% 상한**(이항 CDF bisection, lgamma 로그 공간), k=0일 때 rule-of-three 비교, 첫 실패 사이클, 링크 업 시간 p50/p99/max. `--stats K N`, `--need P` 계산 전용 모드.
  - 검증 [확인됨]: macOS에서 `--stats`/`--need`(독립 계산과 일치), `--sim` 2,000 사이클 + 실패 주입 200 사이클(5종 모두 검출), QEMU 게스트 sysfs 백엔드 4방식 × 30 + negative control 5/5 FAIL. 로그: [macos_sim_runs.txt](onsitePrep/lp14/outputs/macos_sim_runs.txt), [sim_2000.csv](onsitePrep/lp14/outputs/sim_2000.csv), [qemu_link_cycle_log.txt](onsitePrep/lp14/outputs/qemu_link_cycle_log.txt), 게스트 스크립트 [guest_link_cycle.sh](onsitePrep/lp14/lab/guest_link_cycle.sh).
  - 재현: `limactl shell elinux -- bash -c "EXTRA=$PWD/onsitePrep/lp14/link_cycle.py bash onsitePrep/lp13/lab/run_qemu.sh onsitePrep/lp14/lab/guest_link_cycle.sh"`
  - 미검증/한계: `hook` 방식과 실제 하드웨어 PERST#/전원 사이클 [미검증], `sbr`·`link-disable`은 QEMU에서 LTSSM을 실제로 돌리지 않음(§2.4), `t_up`은 polling 오버헤드 포함.
  - 다음 개선 TODO (면접에서 말할 거리): ① 트리거 직후 DLActive- 관측 여부 기록(트리거 효과 확인), ② LnkSta2 EQ 비트·`ltssm_status` 수집, ③ 결과를 SQLite에 적재 + 이전 런과 신뢰구간 비교, ④ 보드별 병렬 실행.

```python
# link_cycle.py 핵심 — 0 실패일 때 rule of three, k 실패일 때 Clopper-Pearson
def upper_bound(k, n, conf=0.95):
    alpha = 1.0 - conf
    lo, hi = 0.0, 1.0
    for _ in range(200):                  # binom_cdf(k, n, p) is decreasing in p
        mid = (lo + hi) / 2
        if binom_cdf(k, n, mid) > alpha:
            lo = mid
        else:
            hi = mid
    return hi                             # k=0 -> 1 - alpha**(1/n) ~ 3/n
```

## 7. 자주 틀리는 것

1. **"1,000번 통과했으니 문제없다"** — 95% 상한 1/334일 뿐. 목표 실패율에서 N을 역산(3/p).
2. **remove/rescan 루프를 링크 트레이닝 테스트라고 보고** — 링크는 L0 유지(LP13 실측). 리셋 종류를 명시.
3. **트리거가 실제로 효과가 있었는지 확인 안 함** — QEMU Link Disable처럼 무효 트리거의 100% PASS.
4. **한 보드로만 통계** — 독립 시행이 아님. 칩·보드·corner로 층화.
5. **수정 확인을 200 사이클로** — 1/500 문제에서 0/200은 확률 67%로 그냥 나온다((1−0.002)^200 ≈ 0.67).
6. **AER status 레지스터를 폴링** — 커널이 W1C로 지움. 누적 카운터 delta.
7. **합격 기준을 말로만** ("링크가 잘 뜨면") — 숫자(speed/width/시간/에러 수/N/신뢰도)와 트리거 조건까지.
8. **FPGA 결과로 margin/성능 결론** — FPGA는 PHY·타이밍이 다름. 기능만.
9. **flaky를 재시도로 PASS 처리** — 재시도 횟수 기록, flaky 별도 집계.
10. **pcitest를 최신 커널 도구로 말함** — 6.18 트리엔 없음, kselftest `pci_endpoint_test`.

## 8. 출처

1. pcilmr(8) man page — pciutils 3.14.0 (Lima VM `man pcilmr`; Lane Margining 요구조건·판정값) · https://man7.org/linux/man-pages/man8/pcilmr.8.html
2. PCI Test User Guide (pci-test-howto) · https://docs.kernel.org/PCI/endpoint/pci-test-howto.html
3. PCI Test Function (pci-test-function, 테스트 레지스터) · https://docs.kernel.org/PCI/endpoint/pci-test-function.html
4. Configuring PCI Endpoint Using CONFIGFS · https://docs.kernel.org/PCI/endpoint/pci-endpoint-cfs.html
5. (웹 검색 결과 요약으로 확인, 본문 미열람) Embedded.com — How to test your board and system designs for PCIe compliance and interoperability · https://www.embedded.com/?p=4026048
6. (웹 검색 결과 요약으로 확인) Keysight — PCI Express Design and Test from Electrical to Protocol (CBB/CLB/SigTest) · https://www.keysight.com/mx/en/assets/7018-04665/brochures/5992-0340.pdf.html
7. PCI Express AER driver guide (aer-inject) · https://docs.kernel.org/PCI/pcieaer-howto.html
8. debugfs-dwc-pcie ABI (rasdes_err_inj, event counters, ltssm_status) · https://www.kernel.org/doc/Documentation/ABI/testing/debugfs-dwc-pcie
9. Meta Engineering — Open-sourcing PCIcrawler · https://engineering.fb.com/2020/08/05/open-source/pcicrawler/ · repo https://github.com/opencomputeproject/ocp-diag-pcicrawler
10. AMD PG195 DMA/Bridge Subsystem for PCI Express (XDMA) · https://docs.amd.com/r/en-US/pg195-pcie-dma
11. Xilinx dma_ip_drivers (XDMA/QDMA Linux drivers) · https://github.com/Xilinx/dma_ip_drivers
12. Linux 6.18.55 소스 (로컬 `/var/lab/src`): `tools/testing/selftests/pci_endpoint/`, `include/uapi/linux/pcitest.h`, `drivers/pci/pci-sysfs.c` (`reset_subordinate`), `drivers/pci/pci.c` (`pci_reset_fn_methods`), `drivers/dma/xilinx/xdma.c` · https://github.com/torvalds/linux
13. Clopper–Pearson interval / rule of three (통계 배경) · https://en.wikipedia.org/wiki/Rule_of_three_(statistics)
14. 내부: [deck §5.1 · §6](2026-10-25_rivian_hm_screen_prep.html), [성능 메트릭 §7](2026-10-30_perf_metrics_ssd_vs_soc.html), [LP13](2026-10-31_LP13_linux_pcie_tools.html)

# LP 트랙 · Linux에서의 PCIe — 개발 · 통합 · 검증 스터디노트 리스트
<!-- theme: pastel -->

> **왜 따로 트랙인가**: 10-30 기술 스크린에서 PCIe를 **Linux 도구 수준**(lspci, LTSSM, L0, "L0 실패 시 lspci")으로 파고듦 → 온사이트 도메인 라운드에서 더 깊게 나올 확률 높음 [추정] · **범위**: Linux에서 PCIe를 ① 개발(드라이버) ② 통합(새 SoC/보드에 붙이기) ③ 검증(테스트·자동화·디버그) 할 때 **무엇을 하고, 무엇을 보고, 무엇을 물어보나** · **형식**: 노트마다 "꼬리 질문 체인"(Q → 꼬리1 → 꼬리2 …) 포함 · **관계**: 온사이트 플랜의 N03(PCIe 심화)을 이 트랙이 대체, [N01](2026-10-31_N01_pcie_lspci_ltssm.html)은 LP03·LP13의 출발점 · **작성**: 2026-10-31 리스트 → **2026-11-01 LP00A 입문 primer + LP01–LP17 본문 완성** (LP18 Q 뱅크만 남음) · **입문**: [LP00A PCIe·Ethernet·MIPI primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html)

## 0. TL;DR

- **18개 노트 (LP01–LP18)**, 6개 묶음: A 기초 모델 · B 링크 · C 리소스/드라이버 · D 인터럽트·DMA·에러·PM·리셋 · E 검증·디버그 · F 실전.
- **P0 7개부터** (스크린 질문 직결 + JD "integration/validation"): **LP03 링크 · LP13 도구 · LP14 검증 방법론 · LP15 디버그 시나리오 · LP05 SoC host controller 통합 · LP06 endpoint 드라이버 · LP04 열거/리소스**.
- 노트마다 공통 구조: **핵심 개념 → Linux에서 볼 정보(명령·sysfs·로그) → 꼬리 질문 체인 + 영어 모범 답 → Don 경험 연결 → (있으면) 실습/C 코드**.
- 실습 환경: 기존 **Lima VM + QEMU 랩**(EmbeddedLinux 노트에서 만든 것)에 QEMU **`edu` PCI 장치**(1234:11e8)를 붙여 드라이버·sysfs·lspci를 직접 확인 — Mac에서 lspci를 못 쓰던 문제 해결.

## 0.5 🔰 처음 보는 사람을 위한 큰 그림 — PCIe · Ethernet · MIPI

> 전체 입문 설명(그림·용어 사전·면접 답)은 **[LP00A I/O 입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html)**. 여기는 리스트를 읽기 위한 최소한만.

```text
            ┌──────────────────────── SoC (예: Rivian 자체 AI 칩) ────────────────────────┐
 카메라 ──MIPI CSI-2──▶ CSI-2 RX ─▶ ISP ─▶ DDR ◀─▶ CPU / NPU                            │
            │                                    │                                      │
            │                              PCIe Root Port ──PCIe 링크──▶ 다른 칩 (가속기·SSD·통신 모듈)
            │                              Ethernet MAC ─▶ PHY ──케이블──▶ 차량 네트워크 (다른 ECU)
            │   I2C/SPI/UART/JTAG = 느린 제어·디버그용 선 (센서 설정, 콘솔, 디버거)                │
            └──────────────────────────────────────────────────────────────────────────────┘
```

| | **PCIe** | **Ethernet** | **MIPI CSI-2** |
|---|---|---|---|
| 한 줄 비유 | 칩과 칩 사이의 **고속도로 + 주소 체계**. CPU가 장치 레지스터를 "내 메모리처럼" 읽고 쓰고, 장치는 DMA로 메모리를 직접 읽고 씀 | 컴퓨터(ECU)끼리 **우편 봉투(frame)** 를 주고받는 네트워크. 받는 사람 주소(MAC)를 써서 보냄 | 카메라 센서 → SoC로 **사진 픽셀을 한 줄씩 흘려보내는 전용 수도관** (한 방향) |
| 거리 | 보드 안 / 짧은 케이블 (수십 cm) | 수 m ~ 100 m | 보드 안 수십 cm (차에서는 serializer로 연장) |
| 구성 요소 | Root Complex(CPU 쪽) · Switch · Endpoint(장치) | MAC(SoC 안 컨트롤러) · PHY(전기 신호 담당 칩) · MDIO(PHY 설정선) | D-PHY(전기 레인) · CSI-2 프로토콜(패킷) · I2C(센서 설정) |
| "연결됨"의 의미 | LTSSM이 **L0** 도달 → 데이터 링크 활성(DLActive) | auto-negotiation 후 **link up** (속도·duplex 합의) | 센서가 HS 모드로 프레임 전송 시작, Frame Start/End 패킷 수신 |
| 에러 감지 | LCRC + ACK/NAK 재전송, AER 레지스터에 기록 | 프레임 끝 **FCS(CRC32)** — 틀리면 버림, 카운터 증가 | 헤더 ECC, payload CRC — 카운터/인터럽트 |
| Linux에서 보는 법 | `lspci`, `/sys/bus/pci/` | `ip link`, `ethtool` | `media-ctl`, `v4l2-ctl` (V4L2) |
| Don 경험 | ✅ SSD controller(Gen3–6) | ❌ 갭 (개념 수준 준비) | 🟡 MIPI 계열 RFFE/SPMI는 Apple에서 사용 |

- **PCIe를 처음 볼 때 꼭 알아야 할 5개 단어** (LP 노트 전체에 반복해서 나옴)
  - **Link / Lane**: 두 칩을 잇는 연결 하나 = link. link는 lane(송신 1쌍 + 수신 1쌍의 차동선) 여러 개 묶음 → x1, x4, x16. 차선 수가 많을수록 넓은 도로.
  - **LTSSM (Link Training and Status State Machine)**: 전원이 들어온 뒤 두 칩이 "너 거기 있니? → 속도 몇까지 돼? → 차선 몇 개 쓸까?" 를 맞추는 **악수 절차**. 끝나면 **L0**(정상 데이터 전송 상태).
  - **Config space**: 모든 PCIe 장치가 가진 **표준 신분증 + 설정판**(256 B ~ 4 KiB). Vendor ID, Device ID, BAR, capability 목록이 들어 있음. `lspci`는 이걸 읽어서 보여주는 도구.
  - **BAR (Base Address Register)**: 장치가 "내 레지스터 창을 이 크기만큼 CPU 메모리 주소에 붙여 줘"라고 요청하는 칸. OS가 주소를 정해 써 주면 CPU가 그 주소로 장치 레지스터를 읽고 씀(MMIO).
  - **Enumeration(열거)**: 부팅 때 OS/펌웨어가 버스를 전부 훑어 "누가 붙어 있나" 확인하고 번호(BDF)와 주소(BAR)를 나눠주는 과정. 링크가 L0가 아니면 장치가 응답을 못 해서 **목록에 안 나옴** ← 스크린 질문의 핵심.
- **검증 엔지니어 입장에서 세 인터페이스의 공통점**: ① 링크가 올라오나(training/negotiation) ② 데이터가 안 깨지나(CRC·에러 카운터) ③ 원하는 속도가 나오나(throughput/latency) ④ 리셋·전원·온도 반복에도 유지되나(stress/soak). 이 4가지 틀로 답하면 처음 보는 인터페이스 질문도 구조적으로 답할 수 있다.

## 1. 노트 목록 한눈에

| ID | 노트 | 묶음 | 우선 | 스크린 질문 연결 | 기존 자료 |
|---|---|---|---|---|---|
| LP01 | [PCIe 토폴로지와 Linux의 PCI 모델](2026-10-31_LP01_pcie_topology_linux_model.html) | A 기초 | P1 | | [N01](2026-10-31_N01_pcie_lspci_ltssm.html) |
| LP02 | [Config space · capability · extended capability](2026-10-31_LP02_config_space_capabilities.html) | A 기초 | P1 | lspci로 뭘 보나 | [L2](hmScreenPrep/problems/L2_pcie_cap_walk/index.html) |
| **LP03** | [**링크 트레이닝 · LTSSM · speed/width · equalization**](2026-10-31_LP03_link_training_ltssm.html) | B 링크 | **P0** | **L0 전 과정 · L0 확인 · L0 실패** | [N01](2026-10-31_N01_pcie_lspci_ltssm.html) |
| **LP04** | [**열거(enumeration)와 리소스 할당 · rescan · hotplug**](2026-10-31_LP04_enumeration_resources_hotplug.html) | C 리소스 | **P0** | L0 실패 시 lspci | |
| **LP05** | [**SoC PCIe host controller 통합 (DT · 드라이버 · iATU · PERST#)**](2026-10-31_LP05_soc_host_controller_integration.html) | C 리소스 | **P0** | | |
| **LP06** | [**Endpoint 드라이버 개발 (probe → BAR → DMA → IRQ)**](2026-10-31_LP06_endpoint_driver_dev.html) | C 드라이버 | **P0** | | [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) |
| LP07 | [인터럽트: INTx · MSI · MSI-X · GIC ITS](2026-10-31_LP07_interrupts_intx_msi_msix.html) | D | P1 | | |
| LP08 | [DMA · IOMMU(SMMU) · 캐시 일관성](2026-10-31_LP08_dma_iommu_coherency.html) | D | P1 | | [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) |
| LP09 | [에러 처리: AER · DPC · 에러 주입 · 복구](2026-10-31_LP09_aer_dpc_error_handling.html) | D | P1 | | |
| LP10 | [전원 관리: D-state · ASPM · L1 substates · runtime PM](2026-10-31_LP10_power_mgmt_aspm.html) | D | P2 | | |
| LP11 | [리셋: PERST# · hot reset · FLR · link disable](2026-10-31_LP11_reset_types.html) | D | P1 | | |
| LP12 | [SR-IOV · VFIO · ACS · P2P (가볍게)](2026-10-31_LP12_sriov_vfio_acs_p2p.html) | D | P2 | | |
| **LP13** | [**Linux PCIe 관측 도구 총정리 (lspci · setpci · sysfs · dmesg · debugfs · tracing)**](2026-10-31_LP13_linux_pcie_tools.html) | E 도구 | **P0** | **lspci로 뭘 보나** | [N01 §2](2026-10-31_N01_pcie_lspci_ltssm.html) |
| **LP14** | [**Linux에서 PCIe 검증 방법론 · 테스트 플랜 · 자동화**](2026-10-31_LP14_validation_methodology.html) | E 검증 | **P0** | | [deck §6](2026-10-25_rivian_hm_screen_prep.html) |
| **LP15** | [**디버그 시나리오 10선 (증상 → 볼 것 → 가설 → 다음 행동)**](2026-10-31_LP15_debug_scenarios.html) | E 디버그 | **P0** | L0 실패 시 lspci | [N01 §5](2026-10-31_N01_pcie_lspci_ltssm.html) |
| LP16 | [Bare-metal vs Linux · Zynq UltraScale+ PCIe · PCI endpoint framework](2026-10-31_LP16_baremetal_vs_linux_zynq.html) | F 실전 | P1 | | [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) |
| LP17 | [Gen5/Gen6 · retimer · 자동차/칩간 PCIe](2026-10-31_LP17_gen5_gen6_retimer_automotive.html) | F 실전 | P2 | | [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html) |
| LP18 | 모의 면접 Q 뱅크 (30문항 + 꼬리 체인 + 영어 답) | F 실전 | P1 | 전부 | LP01–LP17 종합 |

## 2. 노트별 상세 목차 + 꼬리 질문 체인

### A. 기초 모델

#### LP01 · PCIe 토폴로지와 Linux의 PCI 모델 (P1)

- 📄 **본문 노트**: [LP01 노트 열기](2026-10-31_LP01_pcie_topology_linux_model.html)
- 🔰 **쉽게 말하면**: PCIe는 **나무(tree) 모양**이다. 뿌리(Root Complex, CPU 쪽) → 가지(Switch) → 잎(Endpoint, 실제 장치). 각 장치는 아파트 주소처럼 **버스:장치.함수(BDF, 예: 01:00.0)** 번호를 받는다. 이 노트는 그 나무를 Linux가 어떤 구조체와 sysfs 폴더로 표현하는지 설명한다.

- **핵심 개념**
  - Root Complex · Root Port · Switch(Upstream/Downstream Port) · Endpoint · Bridge(Type 1) vs Device(Type 0)
  - BDF(Bus:Device.Function), domain/segment, 버스 번호(primary/secondary/subordinate)
  - Linux 객체: `struct pci_dev`, `struct pci_bus`, `struct pci_host_bridge`, `pci_driver`
  - 설정 공간 접근 경로: ECAM(ACPI **MCFG** 테이블 / DT `reg`) vs legacy CF8/CFC(x86)
- **Linux에서 볼 것**: `lspci -tv`, `ls -l /sys/bus/pci/devices/`, `/sys/devices/pci0000:00/` 트리, `dmesg | grep -i "PCI host bridge"`
- **꼬리 질문 체인**
  - Q. "How does Linux see a PCIe device?" → 꼬리1 "What's the difference between a type 0 and type 1 header?" → 꼬리2 "How are bus numbers assigned when there's a switch?" → 꼬리3 "Where does the kernel get the ECAM base address — on x86 vs on an ARM SoC?"
  - Q. "What's a PCIe switch from software's point of view?" → "How many BDFs does a 4-port switch use?" → "Why does each downstream port have its own bus?"
- **Don 연결**: Solidigm PCIe controller(endpoint 쪽) 경험 → "I know the endpoint side from the inside; here's how the host sees it."

#### LP02 · Config space · capability · extended capability (P1)

- 📄 **본문 노트**: [LP02 노트 열기](2026-10-31_LP02_config_space_capabilities.html)
- 🔰 **쉽게 말하면**: 모든 PCIe 장치는 **표준 양식의 신분증 겸 설정판(config space)** 을 갖고 있다. 앞 64 B는 모두 같은 양식(ID, 명령 비트, BAR), 그 뒤는 **capability라는 옵션 카드들이 연결 리스트로** 이어진다(전원 관리, MSI, PCIe 링크 정보, 에러 보고…). `lspci -vvv` 출력 = 이 판을 해석한 것.

- **핵심 개념**
  - 표준 헤더(0x00–0x3F): Vendor/Device ID, Command(Memory Space, Bus Master, INTx Disable), Status(Capabilities List), Class, Header Type, BAR0–5, Cap Pointer(0x34)
  - Capability list(0x40–0xFF): PM(0x01), MSI(0x05), **PCIe(0x10)**, MSI-X(0x11)
  - **Extended capability(0x100–0xFFF, ECAM 필요)**: AER(0x0001), Device Serial Number, LTR, L1 PM Substates, ACS, SR-IOV, DPC, Secondary PCIe(Lane Error Status), Physical Layer 16/32 GT/s, DOE
  - PCIe capability 안의 Device/Link/Slot/Root 레지스터 묶음 (Cap/Ctl/Sta + 2번 세트)
- **Linux에서 볼 것**: `lspci -vvv`, `lspci -xxxx`, `setpci -s BDF CAP_EXP+0x12.w`, `setpci ECAP_AER+0x10.l`, `/sys/bus/pci/devices/*/config` (바이너리, `hexdump -C`)
- **꼬리 질문 체인**
  - Q. "What's in configuration space?" → "How do you find the PCIe capability?" → "Why can't you see AER with legacy config access?" (256 B 한계 → ECAM 4 KiB) → "What happens if the cap list loops or points to garbage?"
  - Q. "What does the Command register's Bus Master bit do?" → "What breaks if the driver forgets it?" (DMA·MSI가 안 나감) 
- **실습/코드**: `/sys/.../config`를 읽어 cap list를 걷는 C 사용자 프로그램 (= [N01 §6](2026-10-31_N01_pcie_lspci_ltssm.html)의 디코더를 sysfs에 연결)

### B. 링크

#### LP03 · 링크 트레이닝 · LTSSM · speed/width · equalization (P0) ★ 스크린 직결

- 📄 **본문 노트**: [LP03 노트 열기](2026-10-31_LP03_link_training_ltssm.html)
- 🔰 **쉽게 말하면**: 전원이 켜진 뒤 두 칩이 서로를 찾고(Detect) → 비트 박자를 맞추고(Polling) → 차선 수를 정하고(Configuration) → 정상 통신(L0)에 들어가는 **악수 절차(LTSSM)**. 처음엔 가장 느린 2.5 GT/s로 시작해서, 성공하면 더 빠른 속도로 올라간다(Recovery + equalization). 스크린 질문 4개 중 3개가 여기서 나왔다.

- **핵심 개념**
  - 전기적 준비: 전원 → refclk(common clock / SRIS) → **PERST#** (전원 안정 후 ≥100 ms) → 20 ms 안에 training 시작
  - LTSSM: Detect → Polling → Configuration → L0 → Recovery(speed change, Gen3+ Equalization Phase 0–3) → L0, 그 외 L0s/L1/L2/Disabled/Loopback/Hot Reset/Compliance
  - DLL: InitFC1/InitFC2 → **DL_Active**, ACK/NAK, replay
  - 협상 결과: LnkCap vs LnkSta, downgrade(속도/폭), lane reversal, polarity inversion, autonomous bandwidth change
  - 변경 방법: LnkCtl2 Target Link Speed + LnkCtl Retrain Link, Hardware Autonomous Width Disable
- **Linux에서 볼 것**: root port와 endpoint 양쪽 `lspci -vvv` (LnkCap/LnkSta/LnkCtl2/LnkSta2), `current_link_speed`·`current_link_width` sysfs, `BWMgmt+`/`ABWMgmt+`, 컨트롤러 드라이버의 link-up 로그, (지원 시) LTSSM 상태 debugfs [추정: 컨트롤러별 상이], AER correctable(Receiver Error)
- **꼬리 질문 체인** (스크린 질문 + 다음 단계)
  - Q. "What happens before L0?" → "What exactly is detected in Detect?" (receiver termination) → "What are TS1 and TS2 for?" → "Why does the link start at 2.5 GT/s even for a Gen5 device?" → "What is equalization and why only from Gen3?"
  - Q. "Can you confirm L0 with lspci?" → "Then what does DLActive actually mean?" → "Why look at the root port and not the endpoint?" → "How would you get the real LTSSM state?"
  - Q. "If L0 fails, does the device show up in lspci?" → "And if it dies after boot?" (`rev ff`, `Unknown header type 7f`) → "How would you bring it back without rebooting?" (retrain / remove+rescan / hot reset)
  - Q. "The link comes up at Gen3 instead of Gen4. What do you check?" → "How do you force Gen1 to bisect?" → "What would an analyzer show you?"
- **Don 연결**: Solidigm Gen6 controller link bring-up, Apple PCIe 루트코즈 (deck S8 — 숫자 채우기)
- **실습/코드**: `setpci`로 Target Link Speed 바꾸고 retrain → `lspci`로 결과 확인하는 스크립트 (QEMU에서는 링크 시뮬레이션 한계 → 개념/명령 중심)

### C. 리소스 · 드라이버

#### LP04 · 열거와 리소스 할당 · rescan · hotplug (P0)

- 📄 **본문 노트**: [LP04 노트 열기](2026-10-31_LP04_enumeration_resources_hotplug.html)
- 🔰 **쉽게 말하면**: 부팅 때 OS(또는 펌웨어)가 **버스를 하나씩 두드려 보며 "거기 누구 있어요?"** 하고 묻는 과정(열거)과, 찾은 장치마다 **번호와 메모리 주소 창(BAR)을 나눠주는 과정**(리소스 할당). 늦게 올라온 장치를 다시 찾는 rescan, 실행 중 꽂고 빼는 hotplug도 포함.

- **핵심 개념**
  - 누가 열거하나: x86 = 펌웨어(UEFI)가 먼저 + 커널 재확인 / ARM SoC = **커널이 DT 기반으로 직접**
  - BAR sizing(all-1s), 32/64-bit, prefetchable, bridge window(memory / prefetchable memory / I/O), 주소 공간 부족
  - 리소스 실패 로그: `BAR 0: no space for [mem size 0x…]`, `failed to assign` → `pci=realloc`, `pci=hpmemsize=`
  - 100 ms 규칙, **CRS**(Configuration Request Retry Status) 처리
  - 런타임: `remove` / `rescan`, `pciehp`(native hotplug), surprise removal
- **Linux에서 볼 것**: `dmesg | grep -E "pci 0000|BAR|bridge window"`, `/proc/iomem`, `lspci -vv`의 `Region 0: Memory at … [size=…]`, `Memory behind bridge`, `/sys/bus/pci/rescan`, `/sys/bus/pci/slots/`
- **꼬리 질문 체인**
  - Q. "Walk me through enumeration." → "How does BAR sizing work?" → "What if a 64-bit BAR needs more space than the bridge window?" → "What does `pci=realloc` do?"
  - Q. "A device trained late, after boot. How does it appear?" → "What's the difference between rescan and hotplug?" → "What does the kernel do on surprise removal?"
  - Q. "Device is visible but the driver fails with 'can't assign BAR'. Root cause?" → "How would you fix it on an ARM SoC?" (DT `ranges`의 메모리 윈도우 크기)

#### LP05 · SoC PCIe host controller 통합 (P0) ★ integration의 핵심

- 📄 **본문 노트**: [LP05 노트 열기](2026-10-31_LP05_soc_host_controller_integration.html)
- 🔰 **쉽게 말하면**: 새 SoC에서 PCIe가 동작하려면 **SoC 안의 PCIe 컨트롤러 회로(IP) + 전기 신호 담당 PHY + 클럭·리셋**을 순서대로 켜고, Linux에게 "PCIe 창이 메모리 어디에 있고 인터럽트는 어디로 가는지"를 **device tree**로 알려줘야 한다. integration(통합) 업무의 핵심이 바로 이 작업.

- **핵심 개념**
  - 구조: SoC 쪽 **controller IP(예: Synopsys DesignWare, Cadence, Xilinx)** + **PHY** + 클럭/리셋/전원 도메인
  - Linux 드라이버 위치: `drivers/pci/controller/` (예: `dwc/` 계열, Xilinx ZynqMP용 드라이버), 공통 프레임워크 `pci_host_probe`
  - **Device Tree 바인딩**: `compatible`, `reg`(DBI·config 영역), `ranges`(CPU 주소 → PCI 주소 윈도우), `bus-range`, `num-lanes`, `max-link-speed`, `interrupt-map`(INTx), `msi-parent`/`msi-map`, `reset-gpios`(**PERST#**), `clocks`, `phys`, `power-domains`
  - **iATU / address translation**: outbound(CPU → PCI: config/mem/IO 창), inbound(PCI → 메모리: DMA)
  - 드라이버 흐름: 클럭·전원 on → PHY init → PERST# 해제(타이밍) → link 시작 → **link-up 대기(timeout)** → 열거
- **Linux에서 볼 것**: `dmesg | grep -i pcie` (link up/timeout 메시지), DT 확인 `/proc/device-tree/…` 또는 `dtc -I fs`, `/proc/iomem` 의 PCI 윈도우, 컨트롤러 레지스터 `devmem`
- **꼬리 질문 체인**
  - Q. "How would you bring up the PCIe root complex on a new SoC under Linux?" → "What goes in the device tree?" → "What's the `ranges` property mapping?" → "Link is up but the first config read hangs — what now?" (iATU/윈도우 설정, 클럭 도메인, 100 ms)
  - Q. "Who controls PERST# and why does the timing matter?" → "What happens if PERST# is released before refclk is stable?"
  - Q. "Same endpoint works on an x86 host but not on our ARM SoC. Why?" → coherency, IOMMU, inbound 윈도우, MSI 경로(GIC ITS), DMA mask
- **Don 연결**: Apple — 새 칩을 시스템에 붙일 때 PCIe 루트코즈 (integration 쪽 시야)

#### LP06 · Endpoint 드라이버 개발 (P0)

- 📄 **본문 노트**: [LP06 노트 열기](2026-10-31_LP06_endpoint_driver_dev.html)
- 🔰 **쉽게 말하면**: 장치가 열거된 뒤 실제로 쓰려면 **드라이버**가 필요하다. 드라이버는 probe 함수에서 장치를 켜고(enable) → 레지스터 창(BAR)을 매핑하고 → DMA를 허용하고 → 인터럽트를 등록한다. 검증 툴은 드라이버 없이 user space에서 레지스터를 직접 만지는 방법(UIO/VFIO)도 쓴다.

- **핵심 개념**
  - `struct pci_driver`, `pci_device_id` 테이블, `MODULE_DEVICE_TABLE`, probe/remove
  - probe 순서: `pcim_enable_device` → `pcim_iomap_region`(BAR 매핑, 구 API `pcim_iomap_regions`는 deprecated) → `pci_set_master` → `dma_set_mask_and_coherent(DMA_BIT_MASK(64))` (장치 실제 주소 폭으로 설정 — 현재 DMA API HOWTO 기준 64-bit mask는 실패하지 않으므로 "64 실패 시 32로 fallback" 패턴은 구식, LP06 함정 #12) → `pci_alloc_irq_vectors(MSI-X/MSI/INTx)` → `request_irq` → 장치 init
  - MMIO 접근: `readl/writel`(barrier 포함) vs `readl_relaxed`, posted write flush(read-back)
  - 사용자 공간 경로: **UIO / VFIO** (드라이버 없이 검증 툴 만들 때)
- **Linux에서 볼 것**: `lspci -k`(바인딩된 드라이버), `/sys/bus/pci/drivers/<drv>/bind|unbind`, `new_id`, `/proc/interrupts`, `dmesg`
- **꼬리 질문 체인**
  - Q. "Write the skeleton of a PCIe driver probe." → "Why `pci_set_master`?" → "Why set a DMA mask, and what if the device only has 32 address bits?" → "MSI vs MSI-X vs INTx?" → "Why do you need a read-back after a write?"
  - Q. "How would you build a validation tool that pokes device registers without writing a kernel driver?" → UIO/VFIO, `resource0` mmap → "Risks?" (IOMMU, 동시 접근, 인터럽트)
- **실습/코드**: QEMU `edu` 장치용 최소 드라이버(BAR0 읽기, MSI 인터럽트, DMA) + sysfs `resource0` mmap 사용자 프로그램

### D. 인터럽트 · DMA · 에러 · 전원 · 리셋

#### LP07 · 인터럽트: INTx · MSI · MSI-X · GIC ITS (P1)

- 📄 **본문 노트**: [LP07 노트 열기](2026-10-31_LP07_interrupts_intx_msi_msix.html)
- 🔰 **쉽게 말하면**: 장치가 CPU에게 "일 끝났어!"라고 알리는 방법. 옛날 방식 **INTx**는 가상의 전선, 요즘 방식 **MSI/MSI-X**는 "특정 주소에 특정 값을 메모리 쓰기" 하면 그게 인터럽트가 되는 방식(편지함에 쪽지 넣기). ARM SoC에서는 그 쓰기를 **GIC ITS**가 받아서 CPU 인터럽트로 바꾼다.

- **핵심 개념**: INTx(level, 공유, `interrupt-map`) → MSI(메모리 write로 인터럽트, 최대 32 vector) → MSI-X(테이블·PBA in BAR, 2048 vector, vector별 mask), ARM은 MSI 쓰기가 **GIC ITS**(또는 컨트롤러 내장 MSI)로 감, affinity
- **Linux에서 볼 것**: `lspci -vv`의 `MSI: Enable+ Count=…`, `MSI-X: Enable+ Count=…`, `/proc/interrupts`, `/proc/irq/*/smp_affinity`, `dmesg`의 MSI 할당
- **꼬리 질문 체인**: "Interrupts aren't arriving from the device." → "How do you tell whether the device sent it?" (analyzer의 MWr TLP, 장치 상태 레지스터) → "MSI enabled but nothing in /proc/interrupts?" (Bus Master, MSI 주소/ITS 매핑, 마스크) → "Why would INTx work but MSI not?"

#### LP08 · DMA · IOMMU(SMMU) · 캐시 일관성 (P1)

- 📄 **본문 노트**: [LP08 노트 열기](2026-10-31_LP08_dma_iommu_coherency.html)
- 🔰 **쉽게 말하면**: **DMA**는 장치가 CPU를 거치지 않고 메모리를 직접 읽고 쓰는 것(택배 기사가 집 안까지 직접 배달). **IOMMU**는 장치가 접근할 수 있는 메모리를 제한하는 경비원 겸 주소 번역기. ARM에서는 CPU 캐시와 DMA가 서로 다른 내용을 볼 수 있어서(**캐시 일관성** 문제) 데이터가 깨지는 버그가 자주 난다.

- **핵심 개념**: DMA API(coherent vs streaming, `dma_map_single`, `dma_sync_*`), DMA mask, swiotlb bounce buffer, **IOMMU**(ARM SMMU / Intel VT-d), IOMMU group, ATS/PRI(개념만), ARM non-coherent DMA와 캐시 유지, inbound 윈도우
- **Linux에서 볼 것**: `dmesg | grep -i -E "smmu|iommu|DMAR"` (fault 로그), `/sys/kernel/iommu_groups/`, `iommu=`/`iommu.passthrough=` 부트 옵션
- **꼬리 질문 체인**: "DMA data is corrupted on our SoC but fine on x86." → "Coherent or not?" → "Where do you put the cache maintenance?" → "SMMU context fault in dmesg — what does it mean?" → "How would you test DMA correctness in validation?" (패턴·CRC·경계 정렬·크기 sweep)

#### LP09 · 에러 처리: AER · DPC · 에러 주입 · 복구 (P1)

- 📄 **본문 노트**: [LP09 노트 열기](2026-10-31_LP09_aer_dpc_error_handling.html)
- 🔰 **쉽게 말하면**: PCIe는 전송 중 에러를 스스로 감지하고 재전송한다. 그 기록을 남기는 장부가 **AER(Advanced Error Reporting)**. 고칠 수 있는 에러(correctable)가 늘어나면 신호 품질 저하 신호, 못 고치는 에러(uncorrectable)는 장치 리셋이 필요. **DPC**는 문제가 생긴 링크를 격리하는 차단기. 검증에서는 일부러 에러를 넣어(injection) 복구 경로도 테스트한다.

- **핵심 개념**: 에러 분류(correctable: Receiver Error, Bad TLP, Bad DLLP, Replay Num Rollover, Replay Timer Timeout / uncorrectable non-fatal: Completion Timeout, Unsupported Request, Poisoned TLP / fatal: Surprise Down, Malformed TLP, DL Protocol Error), AER 레지스터(status/mask/severity, header log), root port에서 수집, **DPC**(Downstream Port Containment), 드라이버 복구 콜백(`error_detected` → `slot_reset` → `resume`)
- **Linux에서 볼 것**: `dmesg` AER 메시지 디코드, `/sys/bus/pci/devices/*/aer_dev_correctable|aer_dev_nonfatal|aer_dev_fatal`, `aer_rootport_total_err_*`, `lspci -vvv`의 `UESta/CESta`, 에러 주입 `aer-inject`(CONFIG_PCIEAER_INJECT)
- **꼬리 질문 체인**: "We see correctable errors climbing. Is that a problem?" → "Which ones point to signal integrity?" → "How would you use the counts in validation?" (PVT corner별 추세, 임계값) → "How do you test that the error path itself works?" (에러 주입)

#### LP10 · 전원 관리: D-state · ASPM · L1 substates · runtime PM (P2)

- 📄 **본문 노트**: [LP10 노트 열기](2026-10-31_LP10_power_mgmt_aspm.html)
- 🔰 **쉽게 말하면**: 쓰지 않을 때 전기를 아끼는 기능. 장치 자체를 재우는 **D-state**(D0 깨어 있음 ~ D3 잠), 링크를 재우는 **ASPM(L0s/L1)** 과 더 깊은 잠 **L1.1/L1.2**. 잘 자고 잘 깨는지(진입/탈출 지연, 깨어난 뒤 장치가 사라지지 않는지)가 검증 포인트.

- **핵심 개념**: D0/D3hot/D3cold, ASPM L0s/L1, **L1.1/L1.2**(CLKREQ#), LTR, 진입/탈출 latency, runtime PM, suspend/resume 후 config 복원
- **Linux에서 볼 것**: `LnkCtl: ASPM …`, `L1SubCtl1`, `/sys/module/pcie_aspm/parameters/policy`, `/sys/bus/pci/devices/*/link/l1_aspm`, `pcie_aspm=off`, `power/runtime_status`
- **꼬리 질문 체인**: "Latency spikes / device disappears after idle." → "How do you rule out ASPM?" → "What's the trade-off?" → "How would you validate L1.2 entry and exit?"

#### LP11 · 리셋: PERST# · hot reset · FLR · link disable (P1)

- 📄 **본문 노트**: [LP11 노트 열기](2026-10-31_LP11_reset_types.html)
- 🔰 **쉽게 말하면**: 장치를 처음 상태로 되돌리는 방법이 여러 가지다: 전체 전원/핀 리셋(**PERST#**), 링크를 통해 보내는 **hot reset**, 장치 함수 하나만 리셋하는 **FLR**, 링크를 껐다 켜는 **link disable**. 각각 "무엇을 리셋하고 무엇은 안 하는지"가 다르고, 검증 루프에서 어떤 걸 쓰는지가 면접 질문이 된다.

- **핵심 개념**: fundamental reset(PERST#, cold/warm) vs hot reset(Secondary Bus Reset, 링크로 전파) vs **FLR**(함수 단위) vs link disable/enable, 리셋 후 config 복원과 100 ms
- **Linux에서 볼 것**: `/sys/bus/pci/devices/*/reset`, `reset_method`, `setpci`로 Bridge Control SBR, `lspci -vv`의 `DevCap: … FLReset+`
- **꼬리 질문 체인**: "Which reset would you use in a validation loop and why?" → "What does each one NOT reset?" → "After reset the device returns 0xFFFF for a while — why?" (CRS·준비 시간)

#### LP12 · SR-IOV · VFIO · ACS · P2P (P2, 가볍게)

- 📄 **본문 노트**: [LP12 노트 열기](2026-10-31_LP12_sriov_vfio_acs_p2p.html)
- 🔰 **쉽게 말하면**: 장치 하나를 여러 개의 가상 장치로 쪼개는 **SR-IOV**, 장치를 VM이나 user space 프로그램에 통째로 넘겨주는 **VFIO**, 장치끼리 직접 통신(P2P)할 때 보안을 지키는 **ACS**. 차량 중앙 컴퓨터에서 가상화로 기능을 분리할 때 맥락으로만 가볍게.

- **핵심 개념**: PF/VF, `sriov_numvfs`, VFIO 패스스루, ACS와 IOMMU group, peer-to-peer DMA
- **꼬리 질문**: "What's SR-IOV for?" → "Why do IOMMU groups matter for passthrough?" (자동차에서는 가상화된 도메인 분리 맥락 정도)

### E. 도구 · 검증 · 디버그

#### LP13 · Linux PCIe 관측 도구 총정리 (P0) ★ "lspci로 뭘 보나"의 확장판

- 📄 **본문 노트**: [LP13 노트 열기](2026-10-31_LP13_linux_pcie_tools.html)
- 🔰 **쉽게 말하면**: "lspci로 뭘 볼 수 있나?"의 확장판. `lspci`(목록·트리·전체 디코드), `setpci`(레지스터 직접 읽기/쓰기), `/sys/bus/pci/`(파일로 보는 장치 정보), `dmesg`(커널 로그) 등 **Linux에서 PCIe 상태를 들여다보는 모든 창**을 정리하고, 출력 한 줄 한 줄을 읽는 법을 연습한다.

- **핵심 내용**
  - `lspci` 옵션 전부(-t -v -vv -vvv -nn -k -x -xxx -xxxx -D -s -d)와 **출력 필드 읽는 법**(Region, Capabilities, DevCap/DevCtl/DevSta, LnkCap/LnkSta, AER)
  - `setpci`(레지스터 이름 문법 `CAP_EXP+0x12.w`, `ECAP_AER`), 쓰기 마스크 문법
  - sysfs 속성: `vendor/device/class/config/resource*/current_link_*/max_link_*/enable/reset/remove/rescan/driver/msi_irqs/aer_*`
  - 커널 로그: 열거·BAR·AER·link 메시지 패턴
  - `/proc/iomem`, `/proc/interrupts`, `/proc/bus/pci/`
  - debugfs·tracing: 컨트롤러 드라이버별 debugfs [추정], `ftrace`/`trace-cmd`로 드라이버 함수 추적
  - 사용자 공간 접근: `devmem`/`pcimem`(BAR 직접), VFIO
  - 장치별: NVMe면 `nvme-cli`, `fio` (Don의 SSD 경험)
  - HW 도구: 프로토콜 애널라이저 / 인터포저 / exerciser, 스코프(PERST#, refclk)
  - macOS 대응: `system_profiler SPPCIDataType`, `ioreg` (스크린에서 말한 Mac 경험 연결)
- **꼬리 질문 체인**
  - Q. "What can you see with lspci?" → "Which fields do you check first for a link problem?" → "How do you read a register lspci doesn't decode?" (setpci / `-xxxx`) → "How would you script this for 1,000 boot cycles?"
  - Q. "Without lspci (bare-metal or minimal rootfs), how do you get the same info?" → sysfs `config` / ECAM 직접
- **실습/코드**: Python 스크립트 — 모든 장치의 link speed/width/AER 카운터를 sysfs에서 수집해 JSON으로 저장 (검증 자동화의 기본 블록)

#### LP14 · Linux에서 PCIe 검증 방법론 · 테스트 플랜 · 자동화 (P0) ★ JD "validation"

- 📄 **본문 노트**: [LP14 노트 열기](2026-10-31_LP14_validation_methodology.html)
- 🔰 **쉽게 말하면**: **검증(validation)** = 실제 칩이 스펙대로, 모든 조건(전압·온도·반복)에서 동작하는지 증명하는 일. 무엇을(링크·DMA·인터럽트·전원·리셋·에러), 몇 번, 어떤 합격 기준으로 테스트하고, 어떻게 자동화하는지를 테스트 플랜 표로 정리한다. JD의 핵심 업무.

- **핵심 내용 (테스트 플랜 템플릿)**

| 영역 | 테스트 | 판정 기준 | Linux에서 쓰는 것 |
|---|---|---|---|
| Link bring-up | cold/warm boot, hot reset, PERST# 루프 **N=1000회** | 매번 기대 speed/width, DLActive | 전원 제어 + sysfs/lspci 수집 스크립트 |
| Speed/width 매트릭스 | Gen1–GenN × x1/x2/x4… 강제 | 모든 조합 학습 성공 | `setpci` Target Link Speed + retrain |
| Equalization | preset sweep (지원 시) | eye/에러율, EQ phase 성공 | LnkSta2, 컨트롤러 레지스터 |
| 신호 품질 | 트래픽 중 AER correctable 카운트, PVT corner | 임계값 이하, 추세 | `aer_dev_correctable` |
| 열거·리소스 | BAR 크기, 64-bit, 다중 장치, 스위치 | 리소스 할당 성공 | dmesg, `/proc/iomem` |
| 인터럽트 | INTx/MSI/MSI-X 각 vector | 전부 전달, 누락 0 | `/proc/interrupts`, 테스트 드라이버 |
| DMA 기능 | 크기·정렬·경계(4 KiB 교차) sweep, 패턴 비교 | 데이터 일치(CRC) | 테스트 드라이버, **pci-epf-test + pcitest** |
| 성능 | read/write throughput, latency, MPS/MRRS 설정별 | % of 이론 peak | `fio`(NVMe), 커스텀 DMA 벤치 |
| 전원 | ASPM L1/L1.2 진입·탈출 반복, D3 ↔ D0 | 복귀 후 동작·성능 | sysfs PM, runtime PM |
| 리셋·복구 | FLR, hot reset, surprise down | 복구 후 정상, 커널 패닉 없음 | `reset`, `remove/rescan` |
| 에러 경로 | AER 에러 주입 | 보고·복구 동작 | `aer-inject` |
| Soak | 장시간 트래픽 + 온도 | 에러 0, 링크 유지 | 자동화 harness |

  - **Linux PCI endpoint framework**: SoC를 **endpoint 모드**로 설정하고 `pci-epf-test` 기능 + 호스트의 `pcitest` 도구로 BAR·MSI·DMA 테스트 → SoC 대 SoC / SoC 대 x86 검증에 바로 쓰임
  - 자동화: Python harness(전원 사이클, 시리얼 콘솔, 결과 수집, 회귀 비교), 실패 시 로그·레지스터 덤프 자동 수집
  - pre-silicon(FPGA)에서 같은 테스트 먼저 → silicon day 1에 실행
- **꼬리 질문 체인**
  - Q. "How would you validate a PCIe controller on our new SoC?" → "What's your pass criterion for link training?" → "How many cycles and why?" → "A 1-in-500 failure shows up — how do you debug it?" → "How do you know the test itself is right?"
  - Q. "How do you test DMA when you don't have a real endpoint device?" → endpoint framework / FPGA exerciser / loopback
  - Q. "How does this differ between bare-metal and Linux?" → LP16
- **Don 연결**: SK hynix 챔버 테스트 플랫폼(자동화·스케줄링·결과 수집) + shmoo(margin) + Solidigm FPGA 검증

#### LP15 · 디버그 시나리오 10선 (P0) ★ 모의 면접 형식

- 📄 **본문 노트**: [LP15 노트 열기](2026-10-31_LP15_debug_scenarios.html)
- 🔰 **쉽게 말하면**: 면접 단골 "이런 증상이면 어떻게 디버그하나?" 10가지. 전원·클럭·리셋 → 링크 → 열거 → 드라이버 → 인터럽트/DMA 순서로 **아래 계층부터 하나씩 확인**하는 방법을 시나리오마다 연습한다.

| # | 증상 | 먼저 볼 것 | 주요 가설 |
|---|---|---|---|
| 1 | 장치가 `lspci`에 **없음** | root port LnkSta, dmesg link 로그, PERST#/refclk 스코프 | 전원/리셋/클럭, Detect 실패, 너무 이른 config 접근 |
| 2 | **Gen1/Gen3로만** 학습 (기대 Gen4/5) | LnkSta vs LnkCap, LnkSta2 EQ 비트, AER Receiver Error | SI·equalization, retimer, Target Link Speed 설정 |
| 3 | **x4 대신 x2/x1** | LnkSta width, lane 상태 | lane 불량/단선, lane reversal 설정, 커넥터 |
| 4 | 보이는데 드라이버 probe 실패 (`BAR … no space`) | dmesg BAR 로그, `/proc/iomem`, DT `ranges` | bridge window 부족, 64-bit BAR 미지원 윈도우 |
| 5 | **MSI가 안 옴** | `lspci -vv` MSI Enable, `/proc/interrupts`, Bus Master | MSI 주소/ITS 매핑, 마스크, Bus Master off |
| 6 | **DMA 데이터 깨짐** (ARM에서만) | IOMMU/SMMU 로그, 캐시 설정 | non-coherent 캐시 유지 누락, DMA mask, inbound 윈도우 |
| 7 | AER correctable 폭증 | 에러 종류, 온도/전압과 상관관계 | 신호 margin, 커넥터, EQ |
| 8 | 부하·고온에서 **링크 끊김** | AER Surprise Down, DPC, 링크 재학습 로그 | margin, 전원 droop, refclk 지터 |
| 9 | **resume 후 장치 사라짐 / rev ff** | PM 로그, ASPM/L1.2 설정, 리셋 경로 | config 복원 실패, L1.2 탈출 문제, PERST# 처리 |
| 10 | 첫 config read에서 **시스템 hang** | 컨트롤러 레지스터, iATU 설정 | 링크 down 상태에서 접근, 윈도우 미설정, 응답 없는 completion |

- 각 시나리오 본문: 증상 → **볼 정보(명령)** → 가설 우선순위 → bisect 방법(Gen1 강제, 다른 슬롯/보드/호스트, ASPM off, IOMMU passthrough) → 결론과 회귀 테스트 → **영어 2분 답변**
- **꼬리 질문 패턴**: "What's the first thing you check?" → "That looks fine. What next?" → "How do you prove it's hardware and not software?" → "How would you prevent it from coming back?"

### F. 실전

#### LP16 · Bare-metal vs Linux · Zynq UltraScale+ PCIe · PCI endpoint framework (P1)

- 📄 **본문 노트**: [LP16 노트 열기](2026-10-31_LP16_baremetal_vs_linux_zynq.html)
- 🔰 **쉽게 말하면**: 이 포지션은 **주로 bare-metal C**(OS 없이 칩 위에서 바로 도는 코드). Linux가 자동으로 해주는 일(열거, 주소 할당, 인터럽트 설정)을 bare-metal에서는 직접 해야 한다. AMD Zynq UltraScale+(ARM + FPGA 칩)에서 PCIe를 쓰는 방법과 FPGA로 테스트 상대 장치를 만드는 방법도 포함.

- **핵심 내용**: 이 포지션은 **주로 bare-metal C** → Linux 도구가 하는 일을 직접(ECAM, 열거, LTSSM 레지스터 polling, MSI 설정) / 언제 Linux를 쓰나(드라이버·시스템 통합·장기 soak) / Zynq UltraScale+ PS PCIe와 PetaLinux, FPGA 쪽 DMA IP(예: XDMA/QDMA)를 endpoint로 써서 테스트 상대 만들기 / Linux endpoint framework
- **꼬리 질문**: "Would you validate PCIe bare-metal or under Linux?" → "What does Linux give you that bare-metal doesn't, and vice versa?" → "How would you build a test partner endpoint on an FPGA?"
- **Don 연결**: Solidigm FPGA 프로토타입에서 PCIe endpoint 쪽 bring-up

#### LP17 · Gen5/Gen6 · retimer · 자동차/칩간 PCIe (P2)

- 📄 **본문 노트**: [LP17 노트 열기](2026-10-31_LP17_gen5_gen6_retimer_automotive.html)
- 🔰 **쉽게 말하면**: 속도가 올라갈수록(Gen5 32 GT/s, Gen6 64 GT/s) 신호가 약해져서 **PAM4(한 번에 2비트), FEC(오류 정정 코드), retimer(중계기)** 같은 기술이 필요해진다. 자동차 안에서 칩끼리 PCIe를 쓰는 맥락과 기능 안전 관점. Don의 Solidigm Gen6 경험을 강점으로 쓰는 노트.

- **핵심 내용**: Gen5 32 GT/s NRZ, **Gen6 64 GT/s PAM4 · FLIT mode · FEC**, retimer/redriver와 LTSSM 영향, 자동차에서 칩간·모듈간 PCIe, 기능 안전 관점(에러 감지·격리)
- **꼬리 질문**: "What changes in validation for Gen6?" → "How does FEC change what correctable errors mean?" → "Where does a retimer show up in the LTSSM?"
- **Don 연결**: Solidigm **PCIe Gen6 SSD controller** — 강점 카드

#### LP18 · 모의 면접 Q 뱅크 (P1)

- LP01–LP17에서 **30문항** 추출, 각 문항에 꼬리 3단 + 영어 30초 답 + 한국어 핵심
- 사용법: 질문만 보고 소리 내 답 → 펼쳐서 비교 → 막힌 것만 해당 LP 노트로

## 3. 진행 순서 제안

| 순서 | 노트 | 이유 | 예상 시간 |
|---|---|---|---|
| 1 | **LP03 + LP13** | 스크린 질문 그대로. N01을 확장 | 2–3 h |
| 2 | **LP15** | 시나리오 질문은 도메인 라운드 단골, 다른 노트를 끌어오는 허브 | 2 h |
| 3 | **LP14** | JD "validation" 핵심, Don 강점(자동화·shmoo)과 연결 | 2 h |
| 4 | **LP05 + LP04** | integration: SoC host controller, DT, 리소스 | 2–3 h |
| 5 | **LP06** + QEMU `edu` 실습 | 드라이버 개발 질문 + 직접 lspci/sysfs 경험 만들기 | 3 h |
| 6 | LP09 · LP07 · LP08 · LP11 | 꼬리 질문 방어 | 각 1 h |
| 7 | LP16 · LP01 · LP02 | 정리·연결 | 각 0.5–1 h |
| 8 | LP18 모의 Q 뱅크 | D-3 모의 면접용 | 1 h |
| (여유) | LP10 · LP12 · LP17 | | |

## 4. 노트 작성 규칙 (작성 단계에서 적용)

- 파일: `2026-MM-DD_LPxx_<slug>.md` + HTML 렌더 (이 리스트 페이지가 인덱스)
- 섹션: **0 TL;DR(한국어) → 1 핵심 개념 → 2 Linux에서 볼 것(명령·출력 예시·필드 해석) → 3 꼬리 질문 체인(English Q + 영어 모범 답 + 한국어 핵심) → 4 Don 경험 연결 문장 → 5 실습/코드(있으면 검증된 것만) → 6 자주 틀리는 것**
- 사실 신뢰도 표기: spec 수치·커널 경로·sysfs 이름은 검증된 것만, 불확실하면 `[추정]`
- 실습은 Lima VM + QEMU(`-device edu`, 필요하면 PCIe root port/switch 에뮬레이션)에서 실제 출력 캡처해 붙이기

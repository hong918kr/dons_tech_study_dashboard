# LP05 · SoC PCIe host controller 통합 (DT · 드라이버 · iATU · PERST#)
<!-- theme: pastel -->

> **묶음**: C 리소스/드라이버 · **우선**: P0 (JD "SoC integration" 직결) · **선행 노트**: [LP04 열거/리소스](2026-10-31_LP04_enumeration_resources_hotplug.html), [LP03 LTSSM](2026-10-31_LP03_link_training_ltssm.html) · **다음 노트**: [LP06 endpoint 드라이버](2026-10-31_LP06_endpoint_driver_dev.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [N01](2026-10-31_N01_pcie_lspci_ltssm.html) · **코드**: `onsitePrep/lp05/` (DT 창 축소 실험 스크립트 + 실제 로그), 실측 dmesg는 [LP06 랩](onsitePrep/lp06/run_guest_lab.log)과 같은 QEMU 부팅 · **커널 기준**: Linux 6.18.55 소스(랩 VM `/var/lab/src`)에서 이름·값 직접 확인

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"새 SoC에서 PCIe root complex를 Linux로 살리려면 뭘 하나?"**, **"device tree에 뭐가 들어가나 / `ranges`는 무슨 뜻인가?"**, **"link는 올라왔는데 첫 config read가 멈춘다"**, **"x86에선 되는데 우리 ARM SoC에선 안 된다"**, **"PERST#는 누가, 언제 푸나?"**.
- **외울 것 ①: bring-up 순서** = 전원(regulator) → refclk → PHY(SerDes) init + controller reset 해제 → **PERST# assert 유지 ≥100 ms** (`PCIE_T_PVPERL_MS`) → LTSSM enable → PERST# deassert → **link-up 폴링 (DWC: 10회 × 90 ms)** → Gen3+ 면 **100 ms 더 대기** (`PCIE_RESET_CONFIG_WAIT_MS`) → `pci_host_probe()` 열거.
- **외울 것 ②: DT 핵심 4종** = `reg`/`reg-names`(DBI·config·atu 레지스터 창) · **`ranges`(CPU 주소 → PCI 주소 창: I/O, 32-bit MEM, 64-bit prefetchable MEM)** · `interrupt-map`(INTx) + `msi-map`(MSI → GIC ITS) · `reset-gpios`(PERST#) + `clocks`/`resets`/`phys`/`power-domains`.
- **외울 것 ③: iATU** (internal Address Translation Unit, DesignWare 컨트롤러의 주소 변환표) = **outbound**(CPU가 쓰는 주소 → PCIe TLP 주소; config는 매 접근마다 CFG0/CFG1 타입으로 재프로그램) + **inbound**(장치 DMA 주소 → SoC 메모리 주소, `dma-ranges`).
- 실측(이 노트에서 직접 돌림): QEMU virt의 `ranges` 32-bit MEM 창을 512 KiB로 줄였더니 `BAR 0 [mem size 0x00100000]: can't assign; no space` → `Some PCI device resources are unassigned, try booting with pci=realloc` → 드라이버 probe `-EINVAL`. **"DT 창이 작으면 장치는 보이는데 드라이버가 실패한다"** 를 실제 로그로 보여줄 수 있음.
- **30초 영어 답 (headline: "How would you bring up PCIe RC on a new SoC?")**: "I split it into hardware sequencing and software description. The device tree describes the controller: register regions like DBI and the config window, the `ranges` that map CPU address windows to PCI memory and I/O space, interrupts — `interrupt-map` for INTx and `msi-map` to the GIC ITS — plus clocks, resets, PHY, power domain and the PERST# GPIO. The driver, usually a vendor glue on top of the DesignWare core, then powers the slot, starts refclk, initializes the PHY, holds PERST# for at least 100 ms, enables the LTSSM, releases PERST#, and polls for link-up. Once the link is up it programs the iATU outbound windows and hands off to `pci_host_probe` for enumeration. When something fails I check, in order: LTSSM state in the vendor debug register, link-up, the iATU and `ranges` windows, then MSI routing and the inbound/DMA path."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 아파트 단지와 외부 고속도로

- **SoC 내부** = 하나의 아파트 단지. 단지 안에는 내부 도로(**AXI/NoC 인터커넥트**)가 있고, 집집마다(DRAM, UART, GPU…) 번지수(**물리 주소**)가 있다. CPU는 "0xF020_0000번지에 편지 보내" 하듯이 주소에 읽고 쓰기만 한다.
- **PCIe** = 단지 밖으로 나가는 **고속도로**. 고속도로의 차는 "편지"가 아니라 **TLP**(Transaction Layer Packet: PCIe에서 주고받는 패킷 — "메모리 읽기", "메모리 쓰기", "설정 읽기" 같은 요청서)로 다닌다. 주소 체계도 단지 내부와 다르다(**PCI 주소 공간**).
- **Host controller (= Root Complex 쪽 컨트롤러 IP)** = 단지와 고속도로를 잇는 **인터체인지 + 톨게이트**. 단지 내부 편지를 받아 TLP로 바꿔 고속도로에 내보내고(outbound), 고속도로에서 들어온 TLP를 단지 내부 편지로 바꿔 DRAM에 배달한다(inbound).
- **iATU** = 톨게이트에 붙은 **주소 변환표**. "단지 번지 0xF020_0000~0xF0FF_FFFF로 온 편지는 고속도로 주소 0xF020_0000~ 메모리 쓰기 TLP로 바꿔라", "0xF000_0000 창으로 온 건 버스 1번 장치 0의 **설정(config) 읽기** TLP로 바꿔라" 같은 줄들이 들어 있다.
- **PHY (SerDes)** = 실제 **도로 포장과 차선**. 디지털 비트를 수 GHz 아날로그 신호로 바꿔 구리선에 싣는 회로. 차선 수 = **lane 수(x1/x2/x4…)**.
- **refclk** = 양쪽 톨게이트가 공유하는 **신호등 박자**(100 MHz 기준 클럭). 박자가 안정되지 않으면 차가 못 다닌다.
- **PERST#** = 고속도로 진입로의 **공사중 차단기**(PCIe Reset 신호, `#` = active-low: 전압이 LOW일 때 "리셋 중"). 전원과 박자가 안정될 때까지 내려 두었다가, 준비되면 올려서 "이제 링크 학습 시작해도 돼"라고 알린다.
- **Device Tree (DT)** = 단지 **설계도**. ARM SoC는 x86 PC처럼 BIOS/ACPI가 "PCIe 컨트롤러가 여기 있고 창은 이만큼"이라고 알려주지 않는 경우가 많아서, 커널에게 **설계도 파일(.dts → .dtb)** 로 컨트롤러 위치·주소 창·인터럽트·클럭·리셋 핀을 알려 준다.
- **"Integration(통합)"** 이란 = 새 단지(SoC)에 인터체인지(컨트롤러 IP)를 설치하고, 도로 포장(PHY), 신호등(클럭), 차단기(PERST#), 전기(전원 도메인)를 연결하고, 설계도(DT)를 정확히 그리고, 교통관제 소프트웨어(Linux 드라이버)를 맞추는 **전체 작업**이다. 검증 엔지니어는 "이게 다 맞게 연결됐나"를 증명하는 사람.

### 1.2 그림: SoC 안에서 PCIe host controller가 놓이는 자리

```text
 +--------------------------------- SoC ------------------------------------------+
 |                                                                                 |
 |  CPU cores ---+                                                                 |
 |               |      AXI / NoC interconnect (단지 내부 도로)                       |
 |  DRAM ctrl ---+----------+--------------------+-------------------+              |
 |               |          | (AXI slave:        | (AXI master:      | (APB:        |
 |  GIC + ITS ---+          |  CPU->PCIe MMIO)   |  PCIe DMA->DRAM)  |  레지스터)     |
 |  (인터럽트)    |          v                    ^                   v              |
 |  SMMU --------+   +--------------------------------------------------------+     |
 |  (IOMMU)          |  PCIe controller IP (예: Synopsys DesignWare "DWC")      |     |
 |                   |   - DBI 레지스터: 자기 자신(root port)의 config space     |     |
 |                   |   - iATU: outbound/inbound 주소 변환표                   |     |
 |                   |   - MAC / Data Link / LTSSM (링크 학습 상태기계)          |     |
 |                   |   - (옵션) 내장 MSI 컨트롤러, eDMA                        |     |
 |                   |   - vendor glue("app"/"elbi"/"apb" 레지스터: LTSSM enable,|     |
 |                   |     link-up 상태, 인터럽트 모음)                          |     |
 |                   +---------------------------+----------------------------+     |
 |   clocks(CRU) ---> aclk/pclk/aux/pipe         | PIPE interface (병렬 데이터)       |
 |   resets      ---> pwr / pipe / core          v                                   |
 |   power domain --> PD_PCIE        +------------------------+                     |
 |                                   |  PHY = SerDes (TX/RX)  | <--- refclk 100 MHz |
 |   GPIO ---------------------------+------------------------+                     |
 +----|---------------------------------------|--------------------------------------+
      | PERST# (GPIO, active-low)              | lanes (TX+/-, RX+/- 차동쌍) x1..x16
      v                                        v
 +------------------------------------------------------------------+
 |  Endpoint (NVMe SSD, NIC, FPGA, 카메라 브리지 ...)                   |
 +------------------------------------------------------------------+
```

- CPU가 장치 레지스터를 읽을 때 경로: CPU → 인터커넥트 → **AXI slave 포트** → iATU **outbound** 변환 → TLP → PHY → 장치.
- 장치가 DMA로 DRAM에 쓸 때 경로: 장치 → TLP → 컨트롤러 → (iATU **inbound** 변환) → **SMMU**(있으면 주소 재변환/권한 검사) → **AXI master 포트** → DRAM.
- 장치가 MSI(메모리 쓰기 형태의 인터럽트)를 보낼 때: 장치 → "주소 X에 값 Y 쓰기" TLP → 컨트롤러 → **GIC ITS의 doorbell 주소**에 쓰기 → CPU 인터럽트. (또는 컨트롤러 내장 MSI 블록이 받아서 SPI 하나로 모아 줌.)

### 1.3 x86 PC와 무엇이 다른가 (왜 "통합"이 일이 되는가)

| 항목 | x86 PC | ARM SoC (임베디드/자동차) |
|---|---|---|
| 컨트롤러 발견 | 펌웨어(BIOS/UEFI)가 ACPI `MCFG` 테이블로 ECAM 주소 알려 줌 | **DT**의 `compatible`로 **vendor 드라이버**를 고름 |
| 링크 학습 | 펌웨어가 이미 다 해 둠 | **Linux 드라이버가 직접** PHY init·PERST#·LTSSM enable |
| config 접근 | 표준 ECAM (주소 하나로 모든 장치의 config) | DWC 계열은 **iATU를 매번 재프로그램**하는 창 방식이 흔함 (최신은 ECAM 모드도) |
| 주소 창 | 펌웨어가 정함 | DT `ranges`로 SoC 설계자가 정함 (작게 잡으면 BAR 할당 실패) |
| 인터럽트 | IOAPIC/MSI가 표준적 | `interrupt-map`, `msi-map`, **GIC ITS** 연결을 DT로 기술 |
| 캐시 일관성 | 장치 DMA가 보통 cache-coherent | **non-coherent**인 SoC 많음 → DMA API가 cache flush/invalidate 해야 함 (`dma-coherent` 속성) |
| IOMMU | VT-d | SMMU, `iommu-map`으로 RID → Stream ID |

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| RC | Root Complex | CPU 쪽 PCIe의 뿌리. SoC 안 host controller + root port | 이 노트의 주인공 |
| Root Port (RP) | Root Port | RC에서 링크 하나가 나가는 "포트". Linux에선 bus 0의 PCI-to-PCI bridge로 보임 | `lspci`에 `PCI bridge`로 보이는 그것 |
| EP | Endpoint | 링크 끝의 장치(SSD, NIC) | LP06의 주인공 |
| IP | Intellectual Property block | 칩 회사가 사 오는 설계 블록(컨트롤러 회로) | DWC/Cadence 등 IP 종류에 따라 드라이버가 갈림 |
| DWC | Synopsys DesignWare Cores PCIe | 가장 흔한 PCIe 컨트롤러 IP | Linux `drivers/pci/controller/dwc/` 가 거대함 |
| DBI | Data Bus Interface | DWC 컨트롤러 **자기 자신의 레지스터**(root port config + port logic)에 접근하는 창 | LTSSM 상태, 링크 속도 설정, iATU가 여기(또는 근처) |
| ELBI / APP | External Local Bus Interface / application regs | vendor가 DWC 바깥에 붙인 **glue 레지스터** | "LTSSM enable" 비트, link-up 상태가 여기 있는 경우 많음 |
| iATU | internal Address Translation Unit | DWC의 **주소 변환표**(창 여러 개) | 잘못 잡으면 config/MMIO가 엉뚱한 곳으로 가거나 hang |
| ECAM | Enhanced Configuration Access Mechanism | config space를 **고정된 메모리 창 하나**에 펼쳐 놓는 표준 방식 (bus<<20 + dev<<15 + fn<<12 + reg) | QEMU virt, ZynqMP NWL, x86이 사용 |
| TLP | Transaction Layer Packet | PCIe 패킷(MRd/MWr/CfgRd0/CfgRd1/Msg…) | 주소 변환의 결과물 |
| CfgRd0 / CfgRd1 | Type 0 / Type 1 Configuration Read | Type 0 = "바로 아래 장치에게", Type 1 = "더 아래 버스로 전달해 줘" | iATU의 CFG0/CFG1 타입이 이것을 만듦 |
| BDF | Bus/Device/Function | PCI 장치 주소 (예 `01:00.0`) | config TLP의 목적지 |
| RID | Requester ID | 요청 보낸 장치의 BDF 16비트 | `msi-map`/`iommu-map`이 RID로 매핑 |
| PHY / SerDes | Physical layer / Serializer-Deserializer | 병렬 데이터 ↔ 고속 직렬 아날로그 신호 변환기 | PHY init 실패 = 링크 영원히 안 뜸 |
| PIPE | PHY Interface for PCI Express | 컨트롤러 MAC ↔ PHY 사이 표준 인터페이스 | `pipe` 클럭/리셋이 DT에 나옴 |
| refclk | Reference Clock (100 MHz) | 양쪽이 공유하는 기준 박자 | PERST# 해제 전 ≥100 µs 안정 필요 |
| PERST# | PCI Express Reset (active-low) | 장치에게 "리셋 중" 을 알리는 사이드밴드 핀 | 타이밍 틀리면 장치가 학습 시작 못 함 |
| LTSSM | Link Training and Status State Machine | 링크 학습 상태기계 (Detect → Polling → Configuration → L0) | DWC는 `PCIE_PORT_DEBUG0`(0x728)[5:0]에서 읽음 |
| L0 | Link state L0 | 링크 정상 동작 상태 | "link up" |
| DT | Device Tree | 하드웨어 설계도를 커널에 전달하는 데이터 | 통합의 절반은 DT 작성 |
| `ranges` | DT ranges property | **CPU 주소 창 → PCI 주소 창** 매핑 목록 | BAR가 들어갈 공간을 결정 |
| `dma-ranges` | DT dma-ranges | **PCI(장치) 주소 → CPU/메모리 주소** 매핑 (inbound) | DMA 가능 범위 |
| INTx | Legacy interrupt INTA–INTD | 옛날식 선(line) 인터럽트, 메시지로 에뮬레이션 | `interrupt-map`으로 GIC SPI에 연결 |
| MSI / MSI-X | Message Signaled Interrupts | 장치가 특정 주소에 **메모리 쓰기**를 해서 인터럽트를 냄 | ARM에선 GIC ITS/v2m 또는 컨트롤러 내장 MSI |
| GIC ITS | Generic Interrupt Controller Interrupt Translation Service | ARM GICv3의 MSI 수신기: (DeviceID, EventID) → LPI 인터럽트 | `msi-map`이 RID → DeviceID |
| SMMU | System Memory Management Unit (ARM IOMMU) | 장치 DMA 주소를 페이지 테이블로 변환/차단 | `iommu-map`이 RID → Stream ID |
| Gen1/2/3/4/5 | PCIe 세대 | 2.5 / 5 / 8 / 16 / 32 GT/s per lane | `max-link-speed` = 세대 번호 |
| PD | Power Domain | 칩 안에서 전원을 따로 켜고 끄는 구역 | PD 꺼져 있으면 레지스터 접근 자체가 hang |

## 2. 핵심 개념 (정확한 버전)

### 2.1 컨트롤러 IP 종류와 Linux 드라이버 위치

| IP 계열 | 쓰는 SoC 예 | Linux 위치 (6.18) | 특징 |
|---|---|---|---|
| **Synopsys DesignWare (DWC)** | Rockchip RK3588, NXP i.MX8M, Qualcomm, NVIDIA Tegra194/234, TI Keystone, AMD Versal Gen2 MDB | `drivers/pci/controller/dwc/` — 코어 `pcie-designware.c` + host `pcie-designware-host.c` + glue `pcie-dw-rockchip.c`, `pci-imx6.c`, `pcie-qcom.c`, `pcie-tegra194.c`, `pcie-amd-mdb.c` … | DBI + **iATU** + (옵션) 내장 MSI(`PCIE_MSI_ADDR_LO` 0x820…) + eDMA |
| **Cadence** | TI J721E/J7200/J784S4(**TDA4 자동차 SoC**), AM64 | `drivers/pci/controller/cadence/` (`pcie-cadence-host.c`, `pci-j721e.c`) | 자체 outbound region 테이블 |
| **AMD/Xilinx ZynqMP PS (NWL)** | Zynq UltraScale+ MPSoC PS-side PCIe | `pcie-xilinx-nwl.c` (`compatible = "xlnx,nwl-pcie-2.11"`) | **ECAM** 직접, 브리지 레지스터 `breg`, 내장 MSI(64개) |
| **AMD/Xilinx PL IP (XDMA/QDMA bridge)** | FPGA PL에 넣은 "DMA/Bridge Subsystem for PCIe" root-port 모드 | `pcie-xilinx-dma-pl.c` (`"xlnx,xdma-host-3.00"`, `"xlnx,qdma-host-3.00"`), 옛 AXI PCIe `pcie-xilinx.c` | PL 설계(Vivado)에 따라 BAR/창이 바뀜 |
| **AMD Versal CPM** | Versal CPM4/CPM5 | `pcie-xilinx-cpm.c` | 하드 블록 |
| **PLDA** | StarFive JH7110, Microchip PolarFire SoC | `drivers/pci/controller/plda/` | |
| **Generic ECAM** | QEMU virt, 펌웨어가 다 해 둔 서버 SoC | `pci-host-generic.c` + `pci-host-common.c` (`"pci-host-ecam-generic"`) | 링크 학습 코드 없음 — 펌웨어/에뮬레이터가 이미 해 둠 |

- 핵심: **같은 DWC IP라도 SoC마다 glue가 다르다**. glue는 "LTSSM enable 비트가 어느 레지스터에 있나", "link-up을 어디서 읽나", "클럭/리셋/PHY 순서"를 안다. DWC 코어는 "iATU 프로그램", "링크 대기", "MSI", "root port 설정"을 공통으로 한다.

### 2.2 Linux 호출 흐름 (DWC 기준, 6.18 소스로 확인)

```text
platform_driver.probe()  (vendor glue, 예: rockchip_pcie_probe)
 ├─ 리소스 획득: reg-names "dbi","apb","config" / clocks / resets / phys / reset-gpios
 ├─ reset_control_assert → regulator_enable(vpcie3v3) → phy_init/power_on
 ├─ reset_control_deassert → clk_bulk_prepare_enable
 └─ dw_pcie_host_init(pp)                         [pcie-designware-host.c]
     ├─ devm_pci_alloc_host_bridge()
     ├─ dw_pcie_host_get_resources()   : "config" 창, DT ranges → bridge->windows
     │    └─ dw_pcie_get_resources()   : "dbi","dbi2","atu","dma","elbi" + num-lanes, max-link-speed
     ├─ pp->ops->init()                : glue 훅 (dw_pcie_host_ops.init)
     ├─ MSI: msi-parent/msi-map 있으면 외부(ITS) 사용, 없으면 내장 dw_pcie_msi_host_init()
     ├─ dw_pcie_version_detect(), dw_pcie_iatu_detect()   → "iATU: unroll T, N ob, M ib, align ..K, limit ..G"
     ├─ dw_pcie_setup_rc()             : root port config, iATU outbound(MEM/IO) + inbound(dma-ranges)
     ├─ if (!dw_pcie_link_up()) dw_pcie_start_link()   → glue ops->start_link (PERST#·LTSSM)
     ├─ dw_pcie_wait_for_link()        : 실패해도 계속 ("link may come up later")
     └─ pci_host_probe(bridge)         [drivers/pci/probe.c]
          ├─ pci_scan_root_bus_bridge()        : config read로 장치 탐색 (열거)
          ├─ pci_assign_unassigned_root_bus_resources() : BAR/브리지 창 배정
          └─ pci_bus_add_devices()             : 드라이버 매칭 → EP 드라이버 probe (LP06)
```

| 훅 구조체 | 멤버 (6.18 `pcie-designware.h`) | 누가 구현 |
|---|---|---|
| `struct dw_pcie_host_ops` | `init`, `deinit`, `post_init`, `msi_init`, `pme_turn_off` | glue (RC 모드 전용) |
| `struct dw_pcie_ops` | `cpu_addr_fixup`, `read_dbi`, `write_dbi`, `write_dbi2`, **`link_up`**, **`get_ltssm`**, **`start_link`**, **`stop_link`** | glue (RC/EP 공통) |
| RK3588 예 | `rockchip_pcie_host_ops = { .init }`, `dw_pcie_ops = { .link_up, .start_link, .stop_link, .get_ltssm }` | `pcie-dw-rockchip.c` |

### 2.3 Link-up 대기: 이름과 값 (6.18 소스에서 확인 [확인됨])

| 상수 | 값 | 위치 | 의미 |
|---|---|---|---|
| `PCIE_T_PVPERL_MS` | 100 | `drivers/pci/pci.h` | 전원 안정 → PERST# 해제까지 최소 시간 (CEM spec T_PVPERL) |
| `PCIE_T_PERST_CLK_US` | 100 | 〃 | refclk 안정 → PERST# 해제까지 최소 (T_PERST-CLK) |
| `PCIE_RESET_CONFIG_WAIT_MS` | 100 | 〃 | 5 GT/s 초과 포트: **링크 학습 완료 후 100 ms** 지나야 첫 config 요청 (PCIe r6.0 §6.6.1) |
| `PCIE_LINK_WAIT_MAX_RETRIES` | 10 | 〃 | link-up 폴링 횟수 |
| `PCIE_LINK_WAIT_SLEEP_MS` | 90 | 〃 | 폴링 간격 → 최대 약 **0.9 s** |
| `LINK_WAIT_MAX_IATU_RETRIES` / `LINK_WAIT_IATU` | 5 / 9 ms | `dwc/pcie-designware.h` | iATU enable 비트가 읽혀 나올 때까지 재시도 |
| ZynqMP NWL `LINK_WAIT_MAX_RETRIES` | 10 × 90–100 ms | `pcie-xilinx-nwl.c` | 같은 패턴 |

- `dw_pcie_wait_for_link()` 동작 (소스 요약): `dw_pcie_link_up()`을 최대 10번 × 90 ms 폴링 → 실패 시 `"Phy link never came up"` (dev_info) + `-ETIMEDOUT` → 성공 시 `max_link_speed > 2`면 `msleep(100)` → Link Status 읽어서 `"PCIe Gen.%u x%u link up"` 출력.
- `dw_pcie_link_up()` 기본 구현: `PCIE_PORT_DEBUG1`(DBI 0x72C)의 **bit 4 LINK_UP = 1 이고 bit 29 LINK_IN_TRAINING = 0**. glue가 `ops->link_up`을 주면 그걸 씀 (Rockchip은 자기 APB 레지스터).
- LTSSM 상태: `PCIE_PORT_DEBUG0`(0x728) `[5:0]`, `enum dw_pcie_ltssm`: `DETECT_QUIET=0x0`, `DETECT_ACT=0x1`, `POLL_ACTIVE=0x2`, `POLL_COMPLIANCE=0x3`, `POLL_CONFIG=0x4`, `CFG_LINKWD_START=0x7` … `RCVRY_LOCK=0xd`, `RCVRY_SPEED=0xe`, **`L0=0x11`**, `L0S=0x12`, `L1_IDLE=0x14`, `L2_IDLE=0x15`, `DISABLED=0x19`, `LPBK_*=0x1a–0x1c`. debugfs에 `ltssm_status` 파일도 있음 (`pcie-designware-debugfs.c`).
- **config 접근 방어**: `dw_pcie_other_conf_map_bus()`는 매 접근마다 `dw_pcie_link_up()`을 먼저 확인해서 링크가 내려가 있으면 `NULL` → 0xFFFFFFFF 반환. 이유(소스 주석): link down 상태의 config TLP가 **SError**(ARM 비동기 버스 에러 → 커널 패닉)를 내는 플랫폼이 있기 때문. 단 "racy" — 확인 직후 링크가 죽으면 여전히 SError 가능.

### 2.4 iATU: outbound / inbound 레지스터와 config TLP 생성

**레지스터 배치 (6.18 `pcie-designware.h`)**

```text
[구형 viewport 방식]  DBI + 0x900 PCIE_ATU_VIEWPORT : bit31 = 방향(1=inbound), [index] 선택
                      DBI + 0x904 ... 선택된 region의 레지스터 (공유 창)
[unroll 방식, 4.80+]  atu_base(기본 DBI + 0x300000 = DEFAULT_DBI_ATU_OFFSET (0x3<<20))
                      region 주소 = atu_base + (index << 9) + (inbound ? 0x100 : 0)

region 레지스터 (unroll offset)
 +0x00 REGION_CTRL1 : [4:0] TYPE  0x0=MEM 0x2=IO 0x4=CFG0 0x5=CFG1 0x10=MSG, bit13 INCREASE_REGION_SIZE, [22:20] func
 +0x04 REGION_CTRL2 : bit31 ENABLE, bit30 BAR_MODE(inbound), bit28 CFG_SHIFT_MODE(ECAM용), bit22 INHIBIT_PAYLOAD
 +0x08 LOWER_BASE   : CPU 쪽(부모 버스) 시작 주소 하위 32
 +0x0C UPPER_BASE   :                     상위 32
 +0x10 LIMIT        : 끝 주소 하위 32
 +0x14 LOWER_TARGET : PCI 쪽 주소 하위 32  (CFG 타입이면 [31:24]=bus [23:19]=dev [18:16]=fn)
 +0x18 UPPER_TARGET : PCI 쪽 주소 상위 32
 +0x20 UPPER_LIMIT  : 끝 주소 상위 32 (4.60a+)
```

- **outbound 프로그램 순서** (`dw_pcie_prog_outbound_atu`): base/limit/target 쓰고 → CTRL1에 type → **CTRL2에 ENABLE** → ENABLE 비트가 읽힐 때까지 최대 5회 × 9 ms 확인 (안 되면 `"Outbound iATU is not being enabled"`). 주소/크기는 `region_align`(보통 4K/64K) 정렬, 창이 `region_limit`을 넘으면 `-EINVAL`.
- `dw_pcie_iatu_setup()`: DT `ranges`에서 만든 `bridge->windows` 중 **MEM 창마다 outbound region 1개**, IO 창 1개(region 부족하면 config 창과 공유 → `cfg0_io_shared`), `dma-ranges`마다 **inbound region 1개**. region 수가 부족하면 `"Ranges exceed outbound iATU size (%d)"` 경고.
- **config TLP 생성** (`dw_pcie_other_conf_map_bus`, 핵심 로직):

```c
/* 6.18 pcie-designware-host.c 요약 (원문 축약) */
busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) | PCIE_ATU_FUNC(PCI_FUNC(devfn));
type   = pci_is_root_bus(bus->parent) ? PCIE_ATU_TYPE_CFG0   /* root port 바로 아래 버스 → Type 0 */
                                      : PCIE_ATU_TYPE_CFG1;  /* 그 아래(스위치 너머) → Type 1 */
atu.parent_bus_addr = pp->cfg0_base;  /* DT reg "config" 창의 CPU 주소 */
atu.pci_addr = busdev;                 /* TLP의 목적지 BDF */
dw_pcie_prog_outbound_atu(pci, &atu);  /* 매 접근마다 region 재프로그램 */
return pp->va_cfg0_base + where;       /* CPU는 그냥 이 주소를 readl() */
```

- 해석: root bus(bus 0)에는 root port 자신만 있고, 그 config는 **DBI로 직접** 읽는다(`dw_pcie_own_conf_map_bus`). bus 1(= root port의 secondary bus)의 장치는 링크 바로 건너편이니 **Type 0**. bus 2 이상은 스위치를 거쳐야 하니 **Type 1** (스위치가 자기 secondary/subordinate 범위를 보고 Type 0으로 바꿔 전달).
- 결과적으로 **config 창 하나(예 RK3588: 1 MiB)를 매번 다른 BDF로 돌려 쓴다** → 동시 접근은 `pp->lock`/config 경로 락으로 직렬화. 최신 DWC는 `ecam_enabled` + `CFG_SHIFT_MODE`로 진짜 ECAM처럼 쓰는 모드도 있음 (`dw_pcie_config_ecam_iatu`).
- **inbound**: `dma-ranges`가 있으면 `dw_pcie_prog_inbound_atu(type MEM, parent_bus_addr, pci_addr, size)`. 없을 때 RC의 inbound가 변환 없이 통과(pass-through)하는지는 IP 설정·버전에 따라 다름 [추정] — 데이터시트로 확인할 항목.

### 2.5 ECAM (generic host, QEMU virt, ZynqMP)

- config 주소 = `ECAM_base + (bus << 20) + (dev << 15) + (fn << 12) + reg` (`PCIE_ECAM_BUS_SHIFT 20`, `PCIE_ECAM_DEVFN_SHIFT 12`, `include/linux/pci-ecam.h`). 256 bus × 1 MiB = **256 MiB** 창.
- QEMU virt: `reg = <0x40 0x10000000 0x00 0x10000000>` → ECAM 0x40_1000_0000, 256 MiB → dmesg `ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]` (실측 §3.1).
- ZynqMP NWL: `cfg` 창 0x80_0000_0000, 256 MiB, 드라이버가 `PCIE_ECAM_OFFSET(bus->number, devfn, where)`로 바로 계산 — iATU 재프로그램 없음.

### 2.6 Device Tree 바인딩 상세

**공통 골격** (dt-schema `pci-bus-common.yaml` + `pci-host-bridge.yaml`): `device_type = "pci"`, `#address-cells = <3>`, `#size-cells = <2>`, `#interrupt-cells = <1>`, 노드 이름 `pcie@...`.

| 속성 | 형식 | 의미 / 함정 |
|---|---|---|
| `compatible` | 문자열 목록 | 드라이버 선택 키. 예 `"rockchip,rk3588-pcie", "rockchip,rk3568-pcie"` (구체적 → 일반 순) |
| `reg` + `reg-names` | (주소, 크기) 쌍 + 이름 | DWC: `dbi`(필수), `dbi2`, `atu`, `dma`, `elbi`/`app`/`apb`(glue), **`config`**(config 창). 이름으로 찾으므로 **순서보다 이름이 중요** |
| **`ranges`** | `<phys.hi phys.mid phys.lo  cpu_addr(부모 #address-cells)  size(2셀)>` 반복 | **CPU 주소 → PCI 주소 창**. 아래 디코드 참고 |
| `dma-ranges` | 같은 형식, 방향 반대 | 장치가 보는 주소 → 메모리 주소 (inbound). 없으면 1:1 |
| `bus-range` | `<첫 버스 끝 버스>` | 이 host bridge가 쓸 버스 번호 범위 (각 ≤255) |
| `linux,pci-domain` | 정수 | PCI domain(segment) 번호 고정 (`0000:` 부분) |
| `num-lanes` | 1/2/4/8/16/32 | 사용할 lane 수. 보드 배선에 맞게 **보드 dts에서 override** 하는 게 흔함 |
| `max-link-speed` | 세대 번호 (1=2.5, 2=5, 3=8, 4=16 GT/s…; dt-schema 기준 1–4 [확인됨, fetch 시점]) | 이보다 높게 학습 안 하게 Link Control 2 Target Speed 제한. **SI 문제 디버그 시 1로 낮추는 첫 번째 실험** |
| `interrupts` + `interrupt-names` | GIC SPI 목록 | 컨트롤러 자신의 인터럽트 (sys, msg, legacy, err, msi …) |
| `interrupt-map-mask` + `interrupt-map` | 마스크 4셀 + 엔트리 (child addr 3셀, child pin 1셀, parent phandle, parent addr, parent irq spec) | **INTx(INTA–D) → GIC SPI** 연결. 마스크로 BDF의 device 비트와 pin만 비교 |
| `msi-parent` | phandle | 모든 장치의 MSI를 이 컨트롤러로 (장치 구분 없음) |
| `msi-map` | `<rid-base msi-controller msi-base length>` | **RID → MSI 컨트롤러의 DeviceID** (GIC ITS는 DeviceID로 장치 구분) |
| `iommu-map` | `<rid-base iommu iommu-base length>` | RID → SMMU **Stream ID** |
| `reset-gpios` (공통) / 일부 드라이버 `perst-gpios` | GPIO 지정자 | **PERST# 핀**. 극성 플래그(ACTIVE_LOW/HIGH)와 드라이버 코드의 0/1 의미가 맞물려야 함 (§7 함정) |
| `wake-gpios` | GPIO | WAKE# |
| `clocks` + `clock-names` | phandle 목록 | aclk(AXI master/slave), dbi, pclk(APB), aux(저전력), **pipe**(PHY→MAC) |
| `resets` + `reset-names` | phandle 목록 | controller power-up/core/pipe 리셋 |
| `phys` + `phy-names` | PHY phandle | SerDes PHY (예 `pcie30phy`, ZynqMP `psgtr`) |
| `power-domains` | PD phandle | PD 꺼져 있으면 DBI 접근이 **hang/SError** |
| `vpcie3v3-supply` 등 | regulator | 슬롯 3.3V/12V 전원 |
| `dma-coherent` | boolean | 장치 DMA가 CPU 캐시와 일관됨 → DMA API가 cache maintenance 생략. **잘못 넣으면 데이터 깨짐** |
| `supports-clkreq`, `aspm-no-l0s` 등 | boolean | ASPM/L1SS 관련 ([LP10](2026-10-31_LP10_power_mgmt_aspm.html)) |

**`ranges`의 phys.hi 비트 (IEEE 1275 PCI binding, 커널 `arch/powerpc/kernel/pci_of_scan.c` 주석 / `drivers/of/address.c` `of_bus_pci_get_flags`로 확인)**

```text
phys.hi:  n p t 0 0 0 s s | b b b b b b b b | d d d d d f f f | r r r r r r r r
          31 30 29     25 24 23          16  15      11 10  8  7              0
 n  = 1이면 non-relocatable (주소 고정)
 p  = 1이면 prefetchable          -> IORESOURCE_PREFETCH
 t  = aliased / below 1MB
 ss = 00 config, 01 I/O, 10 32-bit memory, 11 64-bit memory
 b/d/f/r = bus/device/function/register (interrupt-map의 child unit address에서 의미, ranges에선 0)

자주 보는 값
 0x01000000 = I/O space
 0x02000000 = 32-bit memory, non-prefetchable
 0x03000000 = 64-bit memory, non-prefetchable
 0x42000000 = 32-bit memory, prefetchable
 0x43000000 = 64-bit memory, prefetchable
 (0x82000000 처럼 bit31 n이 켜진 값도 보임 = non-relocatable)
```

**워크드 예제 1 — QEMU virt (실제, `/var/lab/out/virt.dts`)**

```text
ranges = <0x1000000 0x00 0x00        0x00 0x3eff0000   0x00 0x10000
          0x2000000 0x00 0x10000000  0x00 0x10000000   0x00 0x2eff0000
          0x3000000 0x80 0x00        0x80 0x00         0x80 0x00>;
          |--- PCI 주소 (3셀) ---|   |-- CPU 주소 --|  |-- 크기 --|

 #1  0x01000000 → I/O.        PCI I/O 0x0000      ← CPU 0x3eff_0000, 64 KiB
 #2  0x02000000 → MEM32 NP.   PCI 0x1000_0000     ← CPU 0x1000_0000 (1:1), 0x2eff_0000 (~751 MiB)
 #3  0x03000000 → MEM64 NP.   PCI 0x80_0000_0000  ← CPU 0x80_0000_0000 (1:1), 0x80_0000_0000 (512 GiB)
```

- dmesg와 1:1 대응 (실측 §3.1): `IO 0x003eff0000..0x003effffff -> 0x0000000000`, `MEM 0x0010000000..0x003efeffff -> 0x0010000000`, `MEM 0x8000000000..0xffffffffff -> 0x8000000000`.
- #3은 **64-bit non-prefetchable**이고 크기 > 4 GiB → 커널 경고 `Memory resource size exceeds max for 32 bits` (`drivers/pci/of.c`: non-prefetchable MEM 창 크기의 상위 32비트가 0이 아니면 경고). 이유: PCI-to-PCI bridge의 **non-prefetchable 창 레지스터는 32-bit만** 표현 가능 (`PCI_MEMORY_BASE` 0x20) → 스위치 너머로는 그 창을 못 넘김. 실제 SoC DT는 64-bit 창을 `0x43000000`(prefetchable)로 쓰는 게 관례.

**워크드 예제 2 — INTx `interrupt-map` 디코드 (QEMU virt, 실제)**

```text
interrupt-map-mask = <0x1800 0x00 0x00 0x07>;   // phys.hi 중 device 번호 하위 2비트(bit12:11) + pin(0..7)만 비교
interrupt-map = <0x0000 0 0 1  &gic(0x8002)  0 0  0x00 0x03 0x04   // dev0 INTA -> GIC SPI 3, level-high
                 0x0000 0 0 2  &gic          0 0  0x00 0x04 0x04   // dev0 INTB -> SPI 4
                 ...
                 0x0800 0 0 1  &gic          0 0  0x00 0x04 0x04   // dev1 INTA -> SPI 4  (회전 = swizzle)
                 0x1000 0 0 1  &gic          0 0  0x00 0x05 0x04   // dev2 INTA -> SPI 5
                 0x1800 0 0 1  &gic          0 0  0x00 0x06 0x04>; // dev3 INTA -> SPI 6
 엔트리 1개 = 10셀: child addr 3 + child pin 1 + parent phandle 1 + parent #address-cells(GIC=2) 2 + GIC irq spec 3
 GIC spec <0 N 4> = SPI N, IRQ_TYPE_LEVEL_HIGH -> Linux hwirq = 32 + N
```

- 실측 검증 (LP06 랩 2): edu를 root port(00:02.0) 아래 01:00.0에 두고 INTx 강제 → `/proc/interrupts`에 **`GIC-0  37 Level  edu_lp06`**. 계산: bridge swizzle 후 pin = INTA, root port slot 2 → `0x1000 0 0 1` → SPI 5 → 32+5 = **37** ✓.

**워크드 예제 3 — MSI (QEMU virt, 실제)**: `msi-map = <0x00 0x8003 0x00 0x10000>` → RID 0x0000부터 0x10000개(=모든 BDF)를 phandle 0x8003(`v2m@8020000`, `arm,gic-v2m-frame` — GICv2m MSI frame)으로, DeviceID = RID + 0. 실측 `/proc/interrupts`: `23: … GICv2m-PCI-MSI-0000:00:02.0 0 Edge edu_lp06`.

### 2.7 실제 SoC DTS 주석 읽기 — RK3588 `pcie3x4` (PCIe 3.0 x4, DWC)

출처: `arch/arm64/boot/dts/rockchip/rk3588-extra.dtsi` (6.18.55) + 보드 `rk3588-rock-5b-5bp-5t.dtsi`.

```text
pcie3x4: pcie@fe150000 {
  compatible = "rockchip,rk3588-pcie", "rockchip,rk3568-pcie";  // → pcie-dw-rockchip.c (DWC glue)
  #address-cells = <3>; #size-cells = <2>;                      // PCI 주소 3셀, 크기 2셀 (표준)
  bus-range = <0x00 0x0f>;                                      // 이 컨트롤러는 bus 0..15 (16개)만
  clocks = <&cru ACLK_PCIE_4L_MSTR>,   // aclk_mst: AXI master — 장치 DMA가 DRAM으로 나가는 포트
           <&cru ACLK_PCIE_4L_SLV>,    // aclk_slv: AXI slave  — CPU의 MMIO/config가 들어오는 포트
           <&cru ACLK_PCIE_4L_DBI>,    // aclk_dbi: DBI 레지스터 접근
           <&cru PCLK_PCIE_4L>,        // pclk: APB glue 레지스터
           <&cru CLK_PCIE_AUX0>,       // aux: 저전력(L1/L2) 상태용 보조 클럭
           <&cru CLK_PCIE4L_PIPE>;     // pipe: PHY→MAC PIPE 클럭 (PHY가 살아야 나옴)
  clock-names = "aclk_mst", "aclk_slv", "aclk_dbi", "pclk", "aux", "pipe";
  device_type = "pci";
  interrupts = <GIC_SPI 263 ..>, <GIC_SPI 262 ..>, <GIC_SPI 261 ..>, <GIC_SPI 260 ..>, <GIC_SPI 259 ..>;
  interrupt-names = "sys", "pmc", "msg", "legacy", "err";      // 컨트롤러 자체 이벤트 (link 변화, PM, 메시지, INTx 묶음, 에러)
  #interrupt-cells = <1>;
  interrupt-map-mask = <0 0 0 7>;                               // device 번호 무시, pin만 비교 (root port 1개라 swizzle 불필요)
  interrupt-map = <0 0 0 1 &pcie3x4_intc 0>, ... <0 0 0 4 &pcie3x4_intc 3>;  // INTA..D → 아래 legacy intc의 0..3
  linux,pci-domain = <0>;                                       // 0000: 도메인
  max-link-speed = <3>;                                         // Gen3 (8 GT/s)까지
  msi-map = <0x0000 &its1 0x0000 0x1000>;                       // RID 0x0000-0x0fff → GIC ITS1 DeviceID 동일 (16 bus × 256 devfn = 0x1000)
  iommu-map = <0x0000 &mmu600_pcie 0x0000 0x1000>;              // RID → SMMU(MMU-600) StreamID 동일
  num-lanes = <4>;                                              // x4 (Rock 5B+ 보드 dts는 <2>로 override!)
  phys = <&pcie30phy>; phy-names = "pcie-phy";                  // PCIe 3.0 SerDes PHY
  power-domains = <&power RK3588_PD_PCIE>;                      // PCIe 전원 도메인
  ranges = <0x01000000 0x0 0xf0100000  0x0 0xf0100000  0x0 0x00100000>,   // I/O 1 MiB
           <0x02000000 0x0 0xf0200000  0x0 0xf0200000  0x0 0x00e00000>,   // MEM32 NP 14 MiB @0xF020_0000
           <0x03000000 0x9 0x00000000  0x9 0x00000000  0x0 0x40000000>;   // MEM64 1 GiB @0x9_0000_0000
  reg = <0xa 0x40000000 0x0 0x00400000>,   // "dbi"   4 MiB @0xA_4000_0000 (iATU는 dbi+0x300000 — EP 노드의 "atu" 0xA_4030_0000과 일치)
        <0x0 0xfe150000 0x0 0x00010000>,   // "apb"   Rockchip glue 레지스터 (LTSSM enable, link 상태)
        <0x0 0xf0000000 0x0 0x00100000>;   // "config" 1 MiB @0xF000_0000 — iATU CFG0/CFG1로 매번 retarget
  reg-names = "dbi", "apb", "config";
  resets = <&cru SRST_PCIE0_POWER_UP>, <&cru SRST_P_PCIE0>;
  reset-names = "pwr", "pipe";
  status = "disabled";                                          // SoC dtsi는 끔 → 보드 dts가 "okay"로 켬

  pcie3x4_intc: legacy-interrupt-controller {                   // "legacy" SPI 260 하나를 INTA..D 4개로 펼치는 작은 intc
    interrupt-controller; #address-cells = <0>; #interrupt-cells = <1>;
    interrupt-parent = <&gic>; interrupts = <GIC_SPI 260 IRQ_TYPE_EDGE_RISING 0>;
  };
};

/* 보드 레벨 (rk3588-rock-5b-5bp-5t.dtsi) */
&pcie3x4 {
  pinctrl-0 = <&pcie3_rst>;
  reset-gpios = <&gpio4 RK_PB6 GPIO_ACTIVE_HIGH>;   // PERST# 핀. ACTIVE_HIGH로 적고 드라이버가 0=assert, 1=deassert로 씀 (§7)
  vpcie3v3-supply = <&vcc3v3_pcie30>;                // M.2 슬롯 3.3V
  status = "okay";
};
```

- 읽는 법 요약: **SoC dtsi = 칩에 고정된 것**(주소, 클럭, 인터럽트, PHY), **보드 dts = 배선에 따라 바뀌는 것**(PERST# GPIO, 전원 레귤레이터, 실제 lane 수, status).
- RK3588 드라이버의 실제 순서 (`pcie-dw-rockchip.c` probe 확인): 리소스 → `reset_control_assert` → `regulator_enable(vpcie3v3)` → `rockchip_pcie_phy_init` → `reset_control_deassert` → `clk_init` → `dw_pcie_host_init` → `start_link`: **PERST# assert(값 0) → LTSSM enable → `msleep(PCIE_T_PVPERL_MS)` → PERST# deassert(값 1)**. 소스 주석: refclk이 RC의 PHY에서 나오는지 외부 OSC에서 나오는지 몰라서 LTSSM enable 시점에 PERST#를 풀고 100 µs 대신 100 ms를 넉넉히 준다고 설명.

**비교 — ZynqMP PS PCIe (`arch/arm64/boot/dts/xilinx/zynqmp.dtsi`, NWL)**

```text
pcie: pcie@fd0e0000 {
  compatible = "xlnx,nwl-pcie-2.11";                   // pcie-xilinx-nwl.c
  msi-controller; msi-parent = <&pcie>;                // 자기 자신이 MSI 컨트롤러 (ITS 아님), msi0/msi1 SPI 2개로 64 vector
  interrupt-names = "misc", "dummy", "intx", "msi1", "msi0";
  reg = <0x0 0xfd0e0000 0x0 0x1000>,                   // "breg"   AXI-PCIe bridge 레지스터
        <0x0 0xfd480000 0x0 0x1000>,                   // "pcireg" PCIe 속성/상태 (PS_LINKUP 비트)
        <0x80 0x00000000 0x0 0x10000000>;              // "cfg"    ECAM 256 MiB @0x80_0000_0000
  ranges = <0x02000000 0x0 0xe0000000 0x0 0xe0000000 0x0 0x10000000>,  // MEM32 NP 256 MiB @0xE000_0000
           <0x43000000 0x6 0x00000000 0x6 0x00000000 0x2 0x00000000>;  // MEM64 **prefetchable** 8 GiB @0x6_0000_0000
  bus-range = <0x00 0xff>;
  interrupt-map-mask = <0x0 0x0 0x0 0x7>; interrupt-map = <... &pcie_intc 0x1..0x4>;
  /* iommus = <&smmu 0x4d0>; */                        // SMMU 연결이 주석 처리되어 있음 — 기본은 IOMMU 없이 동작
  power-domains = <&zynqmp_firmware PD_PCIE>;          // PMU 펌웨어가 전원 관리
};
&pcie { status = "okay"; phys = <&psgtr 0 PHY_TYPE_PCIE 0 0>; };   // zcu102: PS-GTR 트랜시버 lane 0, refclk는 si5341 클럭칩
```

- ZynqMP PS PCIe는 Gen2 x4까지 [추정 — UG1085로 확인할 것]. Petalinux에서 PL 쪽 XDMA를 root port로 쓰면 `xlnx,xdma-host-3.00` 노드가 Vivado 설계에서 생성된 DT(`pl.dtsi`)에 들어감.

### 2.8 Bring-up 순서와 타이밍 다이어그램

```text
시간 →
VDD (3.3V/12V, 코어)   ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
                           |<---------- T_PVPERL >= 100 ms ---------->|
REFCLK 100MHz          ________/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/
                                        |<-- T_PERST-CLK >= 100 us -->|
PHY / ctrl reset       ‾‾‾‾‾‾‾‾‾‾‾‾\_______(PHY init, PLL lock, pipe clk 나옴)_____________
PERST# (active-low)    ______________________________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
                       (계속 LOW = 리셋 유지)                         ^ deassert
LTSSM enable (glue)    ____________________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
LTSSM state                                                     Detect→Polling→Config→L0(Gen1)→Recovery→L0(목표속도)
link-up poll (DWC)                                                   [90ms][90ms]...(최대 10회)
Gen3+: 추가 대기                                                                 |<- 100 ms ->|
pci_host_probe() 열거                                                                          ▶ CfgRd0 ...
```

1. **전원**: regulator(슬롯 3.3V/12V) + SoC 내부 PD on. (PD가 꺼진 채 DBI를 읽으면 hang/SError.)
2. **refclk**: SoC PLL 또는 외부 클럭 버퍼/클럭칩 (ZCU102는 si5341). PERST# 해제 전 ≥100 µs 안정.
3. **PHY init + controller reset 해제 + 클럭 enable**: PHY PLL lock → PIPE clock이 나와야 컨트롤러 MAC 레지스터가 정상.
4. **PERST# 유지**: 전원 안정 후 ≥100 ms (T_PVPERL).
5. **LTSSM enable** (glue 레지스터 비트; DWC 코어 입력 신호 이름은 `app_ltssm_enable` [추정]) → **PERST# deassert**.
6. **link-up 대기**: LTSSM이 L0 + DL_Active. DWC 기본 0.9 s.
7. **Gen3 이상이면 100 ms 추가** (장치가 config 요청에 응답 준비할 시간; 그 전에 보내면 **CRS**(Configuration Request Retry Status; PCIe 6.0부터 이름이 RRS = Request Retry Status) 응답이 올 수 있음).
8. **iATU outbound/inbound** 프로그램 (`dw_pcie_setup_rc`, 실제로는 link 전 단계에서 이미 함) → **`pci_host_probe`** 열거 → BAR 배정 → EP 드라이버 probe.

- **PERST#는 누가?** RC 쪽(SoC GPIO 또는 컨트롤러 전용 핀)이 **드라이버 코드로** 제어. 슬롯/M.2 표준상 host가 구동. 일부 보드는 CPLD/PMIC가 제어하거나, 펌웨어(U-Boot)가 이미 풀어 둔 상태로 Linux에 넘어옴 → Linux가 다시 assert/deassert하면 장치가 리셋됨 (의도적이어야 함).

### 2.9 통합 디버그 체크리스트

**증상 A: "Phy link never came up" (링크가 영원히 안 뜸)**

1. LTSSM 상태 레지스터 읽기 (DWC `0x728[5:0]` 또는 vendor glue, debugfs `ltssm_status`): **Detect.Quiet(0x0)에 머묾** → 상대 receiver 감지 실패: 장치 전원, PERST# 극성/타이밍, lane 배선, AC coupling cap. **Polling(0x2–0x4) 반복** → refclk/SSC 불일치, bit lock 실패, lane polarity. **Config(0x7–0xc)** → lane 번호/폭 협상 실패(lane reversal). **Recovery 루프** → SI/equalization 문제 → `max-link-speed = <1>`로 낮춰서 Gen1이면 뜨는지.
2. PHY PLL lock 상태 비트, pipe clock 존재 여부.
3. PERST# 핀을 오실로스코프로: 실제로 HIGH로 올라가는가? (DT 극성 실수 1순위)
4. refclk 존재/주파수/SSC 설정 (common clock vs separate refclk).

**증상 B: "link up인데 첫 config read에서 멈춘다 / SError / 0xFFFFFFFF만 나온다"**

1. **config 창**(`reg-names "config"`)이 실제로 SoC 메모리 맵의 PCIe AXI slave 영역 안인가? DT 주소 오타 → 아무도 응답 안 하는 주소 → 인터커넥트 hang.
2. **iATU**: CFG0 region이 enable 되었나 (`"Outbound iATU is not being enabled"` 로그), `region_align` 위반은 없나, viewport vs unroll 오검출, `atu` reg 주소 틀림. `cpu_addr_fixup` 필요한 SoC(인터커넥트가 상위 비트를 잘라먹음)인데 안 넣음.
3. **타이밍**: Gen3+인데 100 ms 대기 전에 config → CRS → 일부 컨트롤러는 CRS를 재시도로 처리 못 하고 hang/abort.
4. **클럭 도메인**: `aclk_slv`/`aclk_dbi` 꺼짐, PD 꺼짐 → 레지스터 접근 자체가 hang.
5. 장치가 **아직 리셋 중**(PERST#가 너무 빨리 풀렸거나 다시 assert됨).
6. 버스 에러를 abort 대신 0xFFFFFFFF로 바꾸는 컨트롤러 설정(`"error response"` 모드)이 있는지 데이터시트 확인.

**증상 C: "열거는 되는데 BAR 할당 실패 / 드라이버 probe -EINVAL·-ENOMEM"**

1. dmesg `can't assign; no space` → DT `ranges` 창이 너무 작음 / 32-bit BAR인데 64-bit 창만 있음 / prefetchable 64-bit BAR인데 prefetchable 창 없음. (§6.1에서 실제로 재현)
2. `pci=realloc`, 창 확장, 브리지 창 정렬(1 MiB 단위) 고려.

**증상 D: "같은 EP가 x86에선 되는데 우리 ARM SoC에선 안 됨"**

| 의심 지점 | x86에서 숨겨지던 이유 | ARM에서 확인할 것 |
|---|---|---|
| 캐시 일관성 | x86 DMA는 snoop(coherent) | `dma-coherent` 속성 유무, 드라이버가 DMA API(`dma_map_*`, `dma_sync_*`)를 제대로 쓰나, 가상주소 직접 전달 버그 |
| DMA mask / 주소 범위 | x86 RAM이 0부터 | ARM RAM이 0x4000_0000 이상·4 GiB 위에 있음 → 32-bit DMA 장치는 SWIOTLB/IOMMU 필요 (LP06 랩에서 28-bit mask가 `-5`로 실패하는 것 실측) |
| inbound 창 | 펌웨어가 전체 메모리 열어 둠 | `dma-ranges`/inbound iATU가 RAM 전체를 덮는가 |
| IOMMU | VT-d 꺼진 경우 많음 | SMMU stream ID(`iommu-map`) 틀리면 DMA가 fault (`arm-smmu ... Unhandled context fault`) |
| MSI 경로 | x86 MSI는 고정 주소(0xFEEx_xxxx) | `msi-map`/ITS DeviceID 매핑, ITS 테이블, 내장 MSI 컨트롤러 주소가 inbound에서 걸러지지 않는지 |
| INTx | 펌웨어가 PIRQ 라우팅 | `interrupt-map` 오류 → 인터럽트 안 옴/폭주 |
| Relaxed ordering / MPS / MRRS | 펌웨어가 맞춰 줌 | 컨트롤러가 지원하는 MPS(Max Payload Size)보다 장치가 크게 → Malformed TLP |
| PERST#/전원 순서 | 슬롯 표준 준수 | 보드 고유 시퀀스 |
| 64-bit MMIO | 64-bit BAR 문제없음 | 64-bit 창 없으면 64-bit BAR를 32-bit 공간에 넣어야 함 |

## 3. Linux에서 볼 것

### 3.1 [실측] QEMU virt `pci-host-generic` 부팅 로그 (6.18.55, 2026-10-31, `onsitePrep/lp06/run_guest_lab.log`)

```text
[    0.225177] pci-host-generic 4010000000.pcie: host bridge /pcie@10000000 ranges:
[    0.225616] pci-host-generic 4010000000.pcie:       IO 0x003eff0000..0x003effffff -> 0x0000000000
[    0.225848] pci-host-generic 4010000000.pcie:      MEM 0x0010000000..0x003efeffff -> 0x0010000000
[    0.225880] pci-host-generic 4010000000.pcie:      MEM 0x8000000000..0xffffffffff -> 0x8000000000
[    0.226154] pci-host-generic 4010000000.pcie: Memory resource size exceeds max for 32 bits
[    0.226394] pci-host-generic 4010000000.pcie: ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]
[    0.227136] pci-host-generic 4010000000.pcie: PCI host bridge to bus 0000:00
[    0.227255] pci_bus 0000:00: root bus resource [bus 00-ff]
[    0.227281] pci_bus 0000:00: root bus resource [io  0x0000-0xffff]
[    0.227299] pci_bus 0000:00: root bus resource [mem 0x10000000-0x3efeffff]
[    0.227304] pci_bus 0000:00: root bus resource [mem 0x8000000000-0xffffffffff]
[    0.227963] pci 0000:00:00.0: [1b36:0008] type 00 class 0x060000 conventional PCI endpoint
[    0.230111] pci 0000:00:02.0: [1234:11e8] type 00 class 0x00ff00 conventional PCI endpoint
[    0.230132] pci 0000:00:02.0: BAR 0 [mem 0x00000000-0x000fffff]
[    0.231167] pci 0000:00:02.0: BAR 0 [mem 0x10000000-0x100fffff]: assigned
```

| 줄 | 해석 |
|---|---|
| `pci-host-generic 4010000000.pcie` | DT 노드 `pcie@10000000`(이름은 옛 low-ECAM 주소)지만 실제 `reg`는 0x40_1000_0000 (QEMU "highmem ECAM") → 장치 이름은 reg 주소로 붙음 |
| `IO ... -> 0x0` | `ranges` #1: CPU 0x3eff_0000 64 KiB = PCI I/O 0x0000–0xffff |
| `MEM 0x10000000..0x3efeffff -> 0x10000000` | `ranges` #2: 32-bit MEM 1:1 |
| `MEM 0x8000000000..0xffffffffff` | `ranges` #3: 64-bit MEM 512 GiB 1:1 |
| `Memory resource size exceeds max for 32 bits` | #3이 **non-prefetchable**(0x03000000)인데 4 GiB 초과 → 브리지 NP 창(32-bit) 한계 경고 (§2.6) |
| `ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]` | `reg` 256 MiB = 256 bus × 1 MiB |
| `root bus resource [...]` | host bridge가 자식들에게 나눠 줄 수 있는 창 = `ranges` 그대로 |
| `[1b36:0008] type 00 class 0x060000` | QEMU의 host bridge 장치 (00:00.0). 링크 학습 단계가 없는 에뮬레이션이라 "link up" 로그가 없음 |
| `[1234:11e8] ... BAR 0 [mem 0x00000000-0x000fffff]` → `assigned 0x10000000` | edu의 1 MiB BAR를 sizing 후 32-bit MEM 창 맨 앞에 배정 ([LP04](2026-10-31_LP04_enumeration_resources_hotplug.html)) |

```text
[실측] /proc/iomem
10000000-3efeffff : pcie@10000000          ← ranges #2 창
  10000000-100fffff : 0000:00:02.0         ← edu BAR0
4010000000-401fffffff : PCI ECAM           ← reg (config)
8000000000-ffffffffff : pcie@10000000      ← ranges #3 창
  8000000000-8000003fff : 0000:00:01.0     ← virtio-net 64-bit pref BAR4
    8000000000-8000003fff : virtio-pci-modern
```

### 3.2 [실측] root port를 하나 끼운 경우 (`-device pcie-root-port -device edu,bus=rp1`, `run_guest_lab2.log`)

```text
[    0.264360] pci 0000:00:02.0: [1b36:000c] type 01 class 0x060400 PCIe Root Port
[    0.266098] pci 0000:00:02.0: bridge configuration invalid ([bus 00-00]), reconfiguring
[    0.266617] pci 0000:01:00.0: [1234:11e8] type 00 class 0x00ff00 conventional PCI endpoint
[    0.268251] pci 0000:00:02.0: bridge window [mem 0x10000000-0x101fffff]: assigned
[    0.268314] pci 0000:00:02.0: bridge window [mem 0x8000000000-0x80001fffff 64bit pref]: assigned
[    0.268606] pci 0000:01:00.0: BAR 0 [mem 0x10000000-0x100fffff]: assigned
[    0.268634] pci 0000:00:02.0: PCI bridge to [bus 01]
[    0.271471] pcieport 0000:00:02.0: enabling device (0000 -> 0003)
current_link_speed = 16.0 GT/s PCIe      (root port sysfs)
current_link_width = 32
```

- `type 01` = Type 1 header(브리지). `bridge configuration invalid ([bus 00-00]), reconfiguring` = 펌웨어가 secondary bus 번호를 안 정해 둠 → 커널이 bus 01 배정 (임베디드 SoC에서 정상적으로 보이는 메시지).
- 브리지 창이 edu BAR(1 MiB)를 감싸도록 1 MiB 정렬로 잡힘(`0x10000000-0x101fffff`).
- `pcieport` = root port 서비스 드라이버(AER, PME, hotplug, bwctrl).
- **함정**: QEMU 기본 root port는 링크를 **16 GT/s x32로 "보고"** 함 — 에뮬레이션 값이지 학습 결과가 아님. 실제 하드웨어에서 같은 sysfs가 학습 결과를 보여 줌.

### 3.3 [예시 출력] DWC SoC가 정상일 때와 실패할 때 (메시지 형식은 6.18 소스에서 확인, 숫자는 설명용)

```text
rockchip-dw-pcie a40000000.pcie: host bridge /pcie@fe150000 ranges:
rockchip-dw-pcie a40000000.pcie:       IO 0x00f0100000..0x00f01fffff -> 0x00f0100000
rockchip-dw-pcie a40000000.pcie:      MEM 0x00f0200000..0x00f0ffffff -> 0x00f0200000
rockchip-dw-pcie a40000000.pcie:      MEM 0x0900000000..0x093fffffff -> 0x0900000000
rockchip-dw-pcie a40000000.pcie: iATU: unroll T, 8 ob, 8 ib, align 64K, limit 8G
rockchip-dw-pcie a40000000.pcie: PCIe Gen.3 x4 link up
rockchip-dw-pcie a40000000.pcie: PCI host bridge to bus 0000:00
--- 실패 케이스 ---
rockchip-dw-pcie a40000000.pcie: Phy link never came up        ← dw_pcie_wait_for_link 0.9 s 타임아웃
rockchip-dw-pcie a40000000.pcie: Outbound iATU is not being enabled
rockchip-dw-pcie a40000000.pcie: Ranges exceed outbound iATU size (8)
```

- `iATU: unroll T, 8 ob, 8 ib, align 64K, limit 8G` = unroll 방식, outbound 8개/inbound 8개 region, 정렬 64 KiB, region 하나 최대 8 GiB. **ranges 창 수 > ob 수**면 일부 창이 버려짐.
- `PCIe Gen.3 x4 link up` = Link Status의 CLS(현재 속도)/NLW(협상 폭) 그대로. DT `max-link-speed`/`num-lanes` 기대값과 비교.

### 3.4 명령 모음

| 명령 | 보는 것 |
|---|---|
| `dmesg \| grep -iE "pcie\|pci_bus\|iATU\|link"` | 컨트롤러 probe, ranges 해석, link up/timeout |
| `dtc -I fs -O dts /proc/device-tree 2>/dev/null \| less` (또는 `/sys/firmware/devicetree/base/`) | **커널이 실제로 받은 DT** (U-Boot overlay 후 결과) — 빌드한 dts와 다를 수 있음 |
| `hexdump -C /proc/device-tree/<pcie 노드>/ranges` | ranges 셀 원본 (big-endian) |
| `cat /proc/iomem \| grep -i -A3 pci` | ranges 창과 BAR 배치 |
| `ls /sys/kernel/debug/dwc_pcie_*/` → `ltssm_status`, `rasdes_*` | DWC debugfs (6.18: LTSSM 상태, RAS DES 에러 카운터/주입) |
| `devmem 0x<DBI_base+0x728> 32` | DWC LTSSM 상태 직접 (`[5:0]`, 0x11=L0) — **PD/clock 꺼져 있으면 hang 주의** |
| `devmem 0x<DBI_base+0x72C> 32` | DEBUG1: bit4 LINK_UP, bit29 IN_TRAINING |
| `cat /sys/bus/pci/devices/0000:00:00.0/current_link_speed` | root port에서 본 협상 속도 |
| `cat /sys/kernel/debug/clk/clk_summary \| grep -i pcie` | PCIe 클럭 enable 여부·주파수 |
| `cat /sys/kernel/debug/pm_genpd/pm_genpd_summary` | 전원 도메인 상태 |
| `cat /sys/kernel/debug/gpio` | PERST# GPIO 현재 값 (`reset` 라벨) |
| `echo 1 > /sys/bus/pci/rescan` | 늦게 뜬 링크의 장치 재열거 ([LP04](2026-10-31_LP04_enumeration_resources_hotplug.html)) |

## 4. 꼬리 질문 체인

### 체인 1 — "How would you bring up the PCIe root complex on a new SoC under Linux?"

<details><summary>Q → 꼬리1 DT → 꼬리2 ranges → 꼬리3 config read hang (모범 답 펼치기)</summary>

**Q. How would you bring up the PCIe root complex on a new SoC under Linux?**
"First I'd identify the controller IP — most SoCs use DesignWare, so there's a DWC core in `drivers/pci/controller/dwc` and I'd write or reuse a vendor glue. Then the hardware sequence: enable the power domain and slot regulators, start refclk, init the PHY, release controller resets, hold PERST# for at least 100 ms, enable the LTSSM and release PERST#, then poll for link-up. After link-up I check speed and width against the expected values, wait the extra 100 ms for Gen3 and above, and let `pci_host_probe` enumerate. I'd bring it up at Gen1 x1 first, then raise speed and width."
- 한국어 핵심: IP 확인 → 전원/refclk/PHY/리셋 → PERST# 100 ms → LTSSM → link-up → 열거. **Gen1부터 올린다.**

**꼬리1. What goes in the device tree?**
"Register regions with names — DBI, the config window, sometimes a separate ATU or app region. The `ranges` for I/O, 32-bit memory and 64-bit prefetchable memory; `bus-range`; `num-lanes` and `max-link-speed`; the controller interrupts plus `interrupt-map` for INTx and `msi-map` or `msi-parent` for MSI; `iommu-map` if there's an SMMU; and clocks, resets, PHYs, power domain, the slot supplies and the PERST# GPIO. I keep chip-fixed things in the SoC dtsi and board wiring — PERST# GPIO, regulators, actual lane count — in the board dts."
- 한국어 핵심: reg/ranges/irq/msi/iommu/clk/reset/phy/pd/PERST#. **SoC dtsi vs 보드 dts 분리.**

**꼬리2. What's the `ranges` property mapping?**
"Each entry maps a PCI address range to a CPU physical range. The first three cells are the PCI address — the top cell has the space code, 01 for I/O, 10 for 32-bit memory, 11 for 64-bit memory, and bit 30 for prefetchable — then the CPU address and the size. So `0x02000000 0 0xf0200000 0 0xf0200000 0 0xe00000` is a 14 MB non-prefetchable 32-bit window, identity-mapped. Linux turns these into host bridge windows, and on DesignWare each window becomes an outbound iATU region. If a window is too small, BAR assignment fails — I actually reproduced that in QEMU by shrinking the window, and got 'can't assign; no space'."
- 한국어 핵심: phys.hi = 공간 코드(ss) + prefetch 비트. 창마다 outbound iATU 하나. 작으면 BAR 실패.

**꼬리3. Link is up but the first config read hangs — what now?**
"Link-up only proves the physical and data link layers; a config read additionally needs the AXI path, the config window and the iATU. I'd check that the config window address in the DT is inside the controller's slave aperture, that the outbound iATU region for CFG0 actually got enabled and is aligned, and that the bus and DBI clocks and power domain are on. Then timing: for Gen3 and above, software must wait 100 ms after training before the first config request, otherwise the device may return CRS and some controllers don't handle that. I'd also make sure the controller is set to return all-ones on errors rather than raise an SError."
- 한국어 핵심: link ≠ config 경로. **config 창 주소 → iATU enable/정렬 → 클럭·PD → 100 ms/CRS → 에러 응답 모드.**

**꼬리4 (추가). How do you tell 'device not responding' from 'our host bridge is broken'?**
"I read the root port's own config space through DBI — that doesn't go over the link. If the root port's Link Status shows DL_Active and the right speed but reads to bus 1 fail, the problem is the outbound path or the device. Then an analyzer or the controller's debug counters tell me whether a CfgRd0 TLP actually went out and whether a completion came back."
- 한국어 핵심: root port(DBI)는 링크 안 탐 → 경계 나누기. TLP가 나갔나/completion이 왔나.

</details>

### 체인 2 — "Who controls PERST# and why does the timing matter?"

<details><summary>Q → 꼬리1 refclk 전 해제 → 꼬리2 극성 → 꼬리3 펌웨어가 이미 풀어 둠 (모범 답 펼치기)</summary>

**Q. Who controls PERST# and why does the timing matter?**
"The host drives PERST# — on an SoC that's usually a GPIO the controller driver toggles, described by `reset-gpios`. It tells the endpoint that power and reference clock are not yet valid. The CEM spec requires PERST# to stay asserted at least 100 ms after power is stable and at least 100 microseconds after refclk is stable. Linux encodes those as `PCIE_T_PVPERL_MS` and `PCIE_T_PERST_CLK_US`. When PERST# is released the endpoint starts its LTSSM, so if it's released early the device can start training with an unstable clock or before its own internal reset finished."
- 한국어 핵심: host(GPIO, `reset-gpios`)가 구동. **전원 후 ≥100 ms, refclk 후 ≥100 µs.**

**꼬리1. What happens if PERST# is released before refclk is stable?**
"The endpoint's PLL can't lock or locks to a drifting clock, so receiver detect or bit lock fails. You typically see the LTSSM stuck in Detect or bouncing between Polling and Detect, or it trains at a degraded speed and later goes into Recovery loops with correctable errors. Some devices never recover until another PERST# cycle. On the scope I'd capture PERST#, refclk and the power rail together to check the sequence."
- 한국어 핵심: PLL lock 실패 → Detect/Polling 반복, 저속 학습, Recovery 루프. **스코프로 3신호 동시 캡처.**

**꼬리2 (추가). The DT says `GPIO_ACTIVE_HIGH` for PERST#, but PERST# is active-low. Bug?**
"Not necessarily — it depends on the driver's convention. The Rockchip DWC glue writes 0 to assert and 1 to release, so it pairs with `GPIO_ACTIVE_HIGH` in the board DT, while the Qualcomm driver treats 1 as 'assert' and expects `GPIO_ACTIVE_LOW`. Mixing them inverts the reset, and the device either stays in reset forever or never sees a reset pulse. So I check the driver code and then confirm on a scope."
- 한국어 핵심: DT 극성 플래그 × 드라이버의 0/1 의미가 짝. Rockchip(0=assert) vs Qcom(1=assert). **스코프로 확정.**

**꼬리3 (추가). U-Boot already brought the link up. Should Linux reset it again?**
"It's a choice. Re-asserting PERST# gives a known state and exercises the full sequence, which is what I'd want in validation. But some designs keep the link to avoid re-training time or because a boot device sits behind it; then the driver checks link-up first — DWC's host init only calls start_link if the link isn't already up. In validation I'd test both paths, because warm boot with an already-trained link is a different code path."
- 한국어 핵심: DWC는 이미 link up이면 start_link 생략. **검증은 두 경로 다.**

</details>

### 체인 3 — "Same endpoint works on an x86 host but not on our ARM SoC. Why?"

<details><summary>Q → 꼬리1 coherency → 꼬리2 MSI 경로 → 꼬리3 DMA mask (모범 답 펼치기)</summary>

**Q. Same endpoint works on an x86 host but not on our ARM SoC. Why?**
"x86 firmware hides a lot: DMA is cache-coherent, RAM starts at zero, MSI goes to a fixed address, and the firmware already sized the windows. On an ARM SoC I'd look at five things: cache coherency and whether the DT says `dma-coherent` correctly; the inbound window and IOMMU stream-ID mapping for DMA; the MSI path through `msi-map` to the GIC ITS; the DMA mask versus where RAM actually is; and the `ranges` windows for 64-bit BARs. The symptom usually tells me which — corrupted data points to coherency, timeouts with no interrupt point to MSI, and SMMU faults point to the IOMMU map."
- 한국어 핵심: coherency · inbound/IOMMU · MSI(ITS) · DMA mask · 64-bit 창. **증상으로 분류.**

**꼬리1. How does cache coherency break it?**
"If the SoC's PCIe master isn't hardware-coherent, the CPU may read stale cache lines after the device DMA'd new data, or the device reads memory before the CPU's writes were flushed. The DMA API handles it with cache maintenance in `dma_map` and `dma_sync`, but only if the driver uses the API and the DT doesn't wrongly claim `dma-coherent`. A wrong `dma-coherent` flag shows up as intermittent data corruption that disappears when you add prints."
- 한국어 핵심: non-coherent면 DMA API가 flush/invalidate. **잘못된 `dma-coherent` = 간헐적 데이터 깨짐.**

**꼬리2. MSI enabled but no interrupts arrive on the ARM board.**
"MSI is just a memory write, so I follow the write: the device must have Bus Master enabled, the MSI address programmed in its capability must be the ITS doorbell or the controller's MSI target, the inbound path must not block or translate that address, and `msi-map` must give the ITS the right DeviceID for this RID. With an analyzer I can see whether the MWr TLP left the device; if it did, it's on the SoC side. Falling back to INTx is a quick way to split the problem."
- 한국어 핵심: MSI = MWr. Bus Master → MSI 주소 → inbound → DeviceID 매핑. **INTx로 바꿔서 분리.**

**꼬리3 (추가). Why would a 32-bit-DMA device fail only on ARM?**
"On many ARM SoCs, RAM starts above 1 GB or even above 4 GB, so a device with a 32-bit or smaller DMA mask can't reach most memory directly. The kernel either bounces through SWIOTLB, uses the IOMMU to remap into the device's range, or fails `dma_set_mask`. I reproduced the failure in QEMU: QEMU virt RAM starts at 0x4000_0000, and asking for a 28-bit mask returned -EIO."
- 한국어 핵심: ARM RAM은 높은 주소 → 좁은 DMA mask 실패/bounce. **실측: 28-bit → -5.**

</details>

### 체인 4 (추가) — "Explain the iATU. How does a config read become a Type 0 or Type 1 TLP?"

<details><summary>Q → 꼬리1 Type0/1 → 꼬리2 region 부족 → 꼬리3 ECAM과 차이 (모범 답 펼치기)</summary>

**Q. Explain the iATU.**
"The iATU is DesignWare's address translation table. Each outbound region has a base, a limit, a target and a type: CPU accesses that hit the region become PCIe requests of that type with the address rebased to the target. Memory windows from `ranges` become MEM regions; I/O becomes an IO region. Inbound regions do the reverse for device DMA, from `dma-ranges`. Regions have alignment and size limits and a fixed count, which the driver prints at boot."
- 한국어 핵심: region = base/limit/target/type. outbound(ranges), inbound(dma-ranges). 수·정렬 제한.

**꼬리1. How are Type 0 vs Type 1 config requests generated?**
"For config, the driver reprograms one outbound region on every access: type CFG0 or CFG1, and the target holds bus, device and function in bits 31:24, 23:19, 18:16. If the target bus is directly below the root port it uses CFG0, so the device on the link answers; for deeper buses it uses CFG1 so switches forward it. The root port's own registers are read through DBI, not over the link."
- 한국어 핵심: 매 접근 재프로그램, target에 BDF, 바로 아래 = CFG0, 더 아래 = CFG1, 자기 자신 = DBI.

**꼬리2. What if there aren't enough iATU regions?**
"The driver warns 'Ranges exceed outbound iATU size' and drops windows, and I/O may share the config region. The fix is to merge windows in the DT or drop the I/O window, which modern devices rarely need."
- 한국어 핵심: 창 합치기/I/O 창 제거.

**꼬리3. How is that different from ECAM?**
"ECAM maps the whole config space linearly — bus shifted by 20, device-function by 12 — so each device has a fixed 4 KB slot in a 256 MB window and no reprogramming is needed. It's simpler and lock-free but costs address space. QEMU virt and ZynqMP's PS controller use ECAM; newer DesignWare can emulate it with a shift mode."
- 한국어 핵심: ECAM = 선형 256 MiB, 재프로그램 없음.

</details>

### 체인 5 (추가) — "Link trains at Gen1 but not at Gen3. What do you do from the integration side?"

<details><summary>Q → 꼬리1 DT로 무엇을 → 꼬리2 검증 계획 (모범 답 펼치기)</summary>

**Q. Link trains at Gen1 but not at Gen3.**
"Gen1 and Gen2 use fixed de-emphasis, Gen3 adds 128b/130b and equalization, so failing only at Gen3 usually points to signal integrity or equalization presets. I'd read the LTSSM state and Link Status 2 equalization bits, check lane-by-lane error counters, and try different TX presets — the DT now has `eq-presets-8gts` for that. Then compare with a known-good board and check channel loss against the budget."
- 한국어 핵심: Gen3 = EQ. LTSSM/EQ 비트/lane 카운터 → preset 바꿔 보기 (`eq-presets-8gts`).

**꼬리1. What can you change in DT for bring-up?**
"`max-link-speed` to cap the speed, `num-lanes` to test a single lane, equalization presets, and ASPM-disable properties to remove power-state transitions while debugging."
- 한국어 핵심: speed cap, lane 줄이기, EQ preset, ASPM 끄기.

**꼬리2. How would you turn this into a validation plan?**
"A matrix: speed Gen1 to max, width x1 to max, cold boot versus warm reset versus PERST# toggle, temperature and voltage corners, many iterations each, logging link-up time, final speed and width, and AER correctable counts. Any run that lands at a lower speed or width is a failure even if the link is up."
- 한국어 핵심: 속도×폭×리셋 종류×코너×반복, downtrain도 실패로. ([LP14](2026-10-31_LP14_validation_methodology.html))

</details>

## 5. Don 경험 연결

- "At Solidigm I brought up our PCIe Gen6 SSD controller pre-silicon on FPGA — that's the endpoint side of this same sequence. Bringing up PERST#, refclk and link training on an emulation platform taught me to separate 'did the LTSSM reach L0' from 'does the config and memory path work', which is exactly the split I'd use for a root complex on a new SoC."
- "At Apple I integrated wireless chipsets over PCIe into new systems, so I've debugged from the host side: link not training, devices enumerating but failing DMA, and interrupt paths. I used a protocol analyzer and a scope on PERST# and refclk to root-cause issues between the SoC and the module. [사례 1개 구체화: 증상 → 원인 → 수정 `[숫자 채우기]`]"
- "At SK hynix I automated reliability tests for PCIe Gen3/4/5 interfaces in chambers, including shmoo across voltage and temperature, which is how I'd validate a new host controller integration — not one boot, but a matrix of speeds, widths, reset types and corners. [반복 횟수/코너 수 `[숫자 채우기]`]"
- 주의: SoC 쪽 DT/Linux glue 드라이버를 직접 작성했다고 말하지 말 것 — "endpoint/SoC 양쪽을 디버그했고, host controller bring-up 순서는 이렇게 이해한다" 수준으로.

## 6. 실습 / 코드

### 6.1 [실측] DT `ranges` 창을 줄여서 BAR 할당 실패 재현 — `onsitePrep/lp05/run_small_window.sh`

- 방법: `/var/lab/out/virt.dts`의 32-bit MEM 창 크기 `0x2eff0000` → `0x80000`(512 KiB)로 바꾸고 `dtc`로 재컴파일, QEMU `-dtb small.dtb -device edu` 부팅, LP06 드라이버 insmod.
- 실제 출력 (`onsitePrep/lp05/run_small_window.log`, 6.18.55, 2026-10-31):

```text
[    0.217950] pci_bus 0000:00: root bus resource [mem 0x10000000-0x1007ffff]          ← 512 KiB 창
[    0.220725] pci 0000:00:02.0: BAR 0 [mem 0x00000000-0x000fffff]                     ← edu는 1 MiB 필요
[    0.221584] pci 0000:00:02.0: BAR 0 [mem size 0x00100000]: can't assign; no space
[    0.221613] pci 0000:00:02.0: BAR 0 [mem size 0x00100000]: failed to assign
[    0.221933] pci 0000:00:01.0: BAR 1 [mem size 0x00001000]: can't assign; no space   ← ROM이 창을 먼저 차지
[    0.222132] pci 0000:00:01.0: BAR 1 [mem 0x10000000-0x10000fff]: assigned           ← 재시도에서 ROM 해제 후 배정
[    0.222222] pci_bus 0000:00: Some PCI device resources are unassigned, try booting with pci=realloc
10000000-1007ffff : pcie@10000000                                                    ← /proc/iomem: edu 없음
[    0.480689] edu_lp06 0000:00:02.0: error -EINVAL: BAR0
[    0.480787] edu_lp06 0000:00:02.0: probe with driver edu_lp06 failed with error -22
```

- 해석: **장치는 열거됨**(sysfs에 보임, `lspci`에도 보일 것) **하지만 BAR에 주소가 없음** → 드라이버의 `pcim_iomap_region()`이 `-EINVAL`. 32-bit BAR라서 512 GiB짜리 64-bit 창으로도 못 감. 커널이 먼저 큰 것부터 배치하고 실패하면 일부를 풀었다가(`releasing`) 다시 시도하는 과정도 보임.
- 면접용 한 줄: "Device visible but driver fails with 'can't assign BAR' → check the DT `ranges` window size and type before suspecting the driver."

### 6.2 [실측] INTx `interrupt-map` 계산 검증

- LP06 랩 2 (`onsitePrep/lp06/run_guest_lab2.log`): edu @ 01:00.0 (root port 00:02.0 아래), `force_intx=1` → `/proc/interrupts` `15: 4 GIC-0 37 Level edu_lp06`. §2.6 디코드(slot 2 INTA → SPI 5 → hwirq 37)와 일치.

### 6.3 [의사코드] 최소 host controller bring-up (DWC 스타일, 실제 드라이버 아님)

```c
/* [의사코드] — 컴파일/실행 안 함. 이름은 6.18 DWC API를 따랐지만 구조 설명용. */
static int mysoc_pcie_probe(struct platform_device *pdev)
{
    struct mysoc_pcie *p = ...;                     /* glue private */
    struct dw_pcie *pci = &p->pci;

    /* 0. 리소스: reg-names "dbi","apb","config" / clocks / resets / phy / reset-gpios */
    p->apb  = devm_platform_ioremap_resource_byname(pdev, "apb");
    p->perst = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);  /* 극성 규칙은 DT 플래그와 짝 */
    p->phy  = devm_phy_get(&pdev->dev, "pcie-phy");

    /* 1. 전원 */
    regulator_enable(p->vpcie3v3);                  /* 슬롯 3.3V */
    pm_runtime_enable(&pdev->dev); pm_runtime_get_sync(&pdev->dev);   /* power-domains */

    /* 2. 리셋 잡은 상태로 PHY/클럭 */
    reset_control_assert(p->rst);
    phy_init(p->phy); phy_power_on(p->phy);         /* SerDes PLL lock, refclk 출력(내부 refclk인 경우) */
    reset_control_deassert(p->rst);
    clk_bulk_prepare_enable(p->num_clks, p->clks);  /* aclk_mst/slv/dbi, pclk, aux, pipe */

    /* 3. DWC 코어에 맡김: get_resources → iATU detect → setup_rc → start_link → wait → pci_host_probe */
    pci->ops = &mysoc_dw_ops;                       /* .link_up / .start_link / .get_ltssm */
    pci->pp.ops = &mysoc_host_ops;                  /* .init */
    return dw_pcie_host_init(&pci->pp);
}

static int mysoc_start_link(struct dw_pcie *pci)    /* dw_pcie_ops.start_link */
{
    struct mysoc_pcie *p = to_mysoc(pci);
    gpiod_set_value_cansleep(p->perst, 1);          /* PERST# assert (논리 1 = assert 규칙이라고 가정) */
    writel(LTSSM_ENABLE, p->apb + APP_CTRL);        /* vendor glue 레지스터 [가상 이름] */
    msleep(PCIE_T_PVPERL_MS);                       /* 100 ms: 전원/refclk 안정 보장 */
    gpiod_set_value_cansleep(p->perst, 0);          /* PERST# deassert → EP가 LTSSM 시작 */
    return 0;                                       /* 이후 코어가 dw_pcie_wait_for_link(): 10 × 90 ms */
}

static bool mysoc_link_up(struct dw_pcie *pci)      /* dw_pcie_ops.link_up */
{
    u32 v = readl(to_mysoc(pci)->apb + APP_STATUS); /* [가상 이름] */
    return (v & PHY_LINKUP) && (v & DL_ACTIVE);
}
```

### 6.4 [미실행] 더 해 볼 랩

- QEMU `-machine virt,highmem-ecam=off`로 부팅해서 ECAM이 low 주소(0x3f00_0000)로 바뀌고 bus 수가 줄어드는 것 확인 [미실행].
- `ranges` #3을 `0x43000000`(prefetchable)로 바꿔 `exceeds max for 32 bits` 경고가 사라지는지 확인 [미실행].
- `interrupt-map`의 한 엔트리를 일부러 틀리게 해서 INTx 모드 edu가 `no interrupt within 2 s`로 실패하는 것 재현 [미실행].

## 7. 자주 틀리는 것

1. **"link up = 다 됐다"** — link up은 PHY/DLL까지. config 경로(AXI 창 + iATU + 클럭/PD)는 별개. 첫 config read hang은 대부분 이쪽.
2. **PERST# 극성**: DT 플래그(`GPIO_ACTIVE_LOW/HIGH`)와 드라이버의 `gpiod_set_value(…, 0/1)` 의미가 짝. Rockchip glue는 0=assert(ACTIVE_HIGH와 짝), Qualcomm은 1=assert(ACTIVE_LOW와 짝). 섞으면 영원히 리셋 or 리셋 펄스 없음.
3. **`ranges` 64-bit 창을 non-prefetchable(0x03000000)로**: 루트 버스에선 동작하지만 브리지 NP 창은 32-bit → 스위치 너머 64-bit NP BAR는 못 감. 관례는 `0x43000000`. (QEMU virt에서 실제로 경고 나옴)
4. **CPU 주소 ≠ PCI 주소일 수 있음**: `ranges`가 1:1이 아닐 때 BAR 레지스터에 써진 값(PCI 주소)과 `/proc/iomem`·sysfs `resource`의 주소(CPU 주소)가 다르다. `lspci -v`의 `Memory at ...`은 기본적으로 **커널이 본 주소(CPU 쪽)**, `lspci -b`(bus-centric)는 **카드가 버스에서 보는 주소** ([LP13](2026-10-31_LP13_linux_pcie_tools.html)). 애널라이저 TLP 주소와 비교할 땐 `-b` 쪽.
5. **Gen3+ 100 ms 규칙**은 "PERST# 해제 후"가 아니라 **"링크 학습 완료 후"** 100 ms (5 GT/s 초과 포트). 5 GT/s 이하는 Conventional Reset 종료 후 100 ms.
6. **`num-lanes`를 SoC dtsi 값 그대로**: 보드가 x2로 배선됐는데 x4로 두면 학습은 x2로 되거나 lane 검출 지연. 보드 dts에서 override (Rock 5B+ 실제 예).
7. **`dma-coherent`를 "있으면 빠르니까" 넣기**: 하드웨어가 coherent가 아니면 간헐적 데이터 깨짐. 반대로 빼면 느려질 뿐 정확함.
8. **`msi-map` 길이/베이스 실수**: RID 범위가 `bus-range`보다 작으면 일부 버스의 MSI가 매핑 안 됨 (RK3588은 16 bus × 256 = 0x1000으로 정확히 맞춤).
9. **PD/클럭 꺼진 상태에서 `devmem`으로 DBI 읽기** → 시스템 hang. 디버그 도구 자체가 버스를 멈출 수 있다.
10. **QEMU의 LnkSta 값(16 GT/s x32)을 실제 학습 결과로 믿기** — 에뮬레이션 기본값.

## 8. 출처

1. Linux 6.18.55 소스 (랩 VM `/var/lab/src/linux-6.18.55`, 온라인 대응: https://elixir.bootlin.com/linux/v6.18/source) — `drivers/pci/controller/dwc/pcie-designware.h`, `pcie-designware.c` (`dw_pcie_wait_for_link`, `dw_pcie_link_up`, `dw_pcie_prog_outbound_atu`), `pcie-designware-host.c` (`dw_pcie_host_init`, `dw_pcie_other_conf_map_bus`, `dw_pcie_iatu_setup`), `pcie-dw-rockchip.c`, `pcie-qcom.c`, `pcie-designware-debugfs.c`
2. Linux 6.18.55 `drivers/pci/pci.h` (PCIE_T_PVPERL_MS, PCIE_T_PERST_CLK_US, PCIE_RESET_CONFIG_WAIT_MS, PCIE_LINK_WAIT_*) · `drivers/pci/probe.c` (`pci_host_probe`) · `drivers/pci/of.c` (32-bit 경고) · `drivers/of/address.c` (`of_bus_pci_get_flags`) · `include/linux/pci-ecam.h`
3. Linux 6.18.55 `drivers/pci/controller/pcie-xilinx-nwl.c`, `pcie-xilinx-dma-pl.c`, `pcie-xilinx-cpm.c`, `pci-host-generic.c`, `cadence/pci-j721e.c`
4. Linux 6.18.55 DTS: `arch/arm64/boot/dts/rockchip/rk3588-extra.dtsi` (pcie3x4), `rk3588-rock-5b-5bp-5t.dtsi`, `rk3588-rock-5b-plus.dts`, `arch/arm64/boot/dts/xilinx/zynqmp.dtsi`, `zynqmp-zcu102-revA.dts`
5. Linux 6.18.55 `Documentation/devicetree/bindings/pci/snps,dw-pcie.yaml` (reg-names/interrupt-names)
6. dt-schema `pci-bus-common.yaml` — https://github.com/devicetree-org/dt-schema/blob/main/dtschema/schemas/pci/pci-bus-common.yaml
7. PCI phys.hi 셀 형식 주석 — Linux `arch/powerpc/kernel/pci_of_scan.c` (예: https://code.woboq.org/linux/linux/arch/powerpc/kernel/pci_of_scan.c.html)
8. QEMU virt DT 실물: 랩 VM `/var/lab/out/virt.dts` (pcie@10000000 노드 → `onsitePrep/lp05/virt_pcie_node.dts`)
9. Linux PCI driver guide (probe/remove 순서, posted write) — https://docs.kernel.org/PCI/pci.html
10. 실측 로그: `onsitePrep/lp06/run_guest_lab.log`, `onsitePrep/lp06/run_guest_lab2.log`, `onsitePrep/lp05/run_small_window.log` (2026-10-31, QEMU 10.2.1, Linux 6.18.55)

# LP16 · Bare-metal vs Linux · Zynq UltraScale+ PCIe · PCI endpoint framework
<!-- theme: pastel -->

> **묶음**: F 실전 · **우선**: P1 (이 포지션 = 주로 bare-metal C + JD "AMD Zynq Petalinux") · **선행 노트**: [LP15 디버그 시나리오](2026-10-31_LP15_debug_scenarios.html), [LP04 열거·리소스](2026-10-31_LP04_enumeration_resources_hotplug.html), [LP05 SoC host controller](2026-10-31_LP05_soc_host_controller_integration.html) · **다음 노트**: [LP17 Gen5/Gen6·retimer·자동차](2026-10-31_LP17_gen5_gen6_retimer_automotive.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [L1 UIO/MMIO 연습](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) · **코드**: `onsitePrep/lp16/bm_pcie_bringup.c` (가짜 ECAM, macOS ASan/UBSan ALL PASS, 출력 `onsitePrep/lp16/bm_pcie_bringup.out`)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"Would you validate PCIe bare-metal or under Linux?"**, "Linux가 해 주는 걸 bare-metal에선 어떻게 하나?", "Zynq UltraScale+에서 PCIe는 어디에 있나 (PS vs PL)?", "FPGA로 테스트 상대(endpoint)를 어떻게 만드나?", "실장치 없이 DMA를 어떻게 테스트하나?"
- **외울 것 ① bare-metal bring-up 8단계**: 링크 업 polling → 100 ms 대기 → ECAM DFS 스캔 + 버스 번호 → BAR sizing(all-1s, decode off) → 큰 BAR부터 정렬 할당 → bridge window → Memory+Bus Master enable → MSI 주소/데이터. ECAM 주소 = `base + (bus<<20 | dev<<15 | fn<<12 | off)`.
- **외울 것 ② Zynq UltraScale+ MPSoC PS-PCIe** = PCIe **v2.1, 최대 Gen2 x4, Root Port 또는 Endpoint**, AXI-PCIe bridge + **4채널 DMA**, 레인은 PS-GTR(다른 프로토콜과 공유) [1][2]. Linux 드라이버 `pcie-xilinx-nwl.c` (`xlnx,nwl-pcie-2.11`) [3], bare-metal 드라이버 embeddedsw `pciepsu` [4]. **PL 쪽** 고속 PCIe는 PG213 integrated block + PG195 **XDMA** / PG302 **QDMA** [5][6][7].
- **외울 것 ③ 테스트 상대 3종**: (a) FPGA XDMA endpoint + `dma_ip_drivers` `xdma` 드라이버(`/dev/xdma0_h2c_0` 등, `dma_to_device`/`dma_from_device`) [8], (b) **Linux PCI endpoint framework** — 두 번째 SoC를 EP로: configfs `/sys/kernel/config/pci_ep/` + `pci-epf-test` ↔ 호스트 `pci_endpoint_test` (6.14+ kselftest, 예전 `pcitest`) [9][10], (c) 상용 장치(NVMe/NIC) — 동작은 확실하지만 제어·에러 주입은 약함.
- **30초 영어 ("bare-metal or Linux?")**: "Both, in sequence, for different reasons. I bring up and characterize the controller bare-metal first — link training, the speed and width matrix, BAR and MSI programming, error injection — because there's no driver stack between me and the registers, it's deterministic, it runs on the FPGA prototype before Linux even boots, and when it fails I know it's the hardware or my ten lines of code. Then I move to Linux for system integration: the real host driver, the IOMMU, AER and DPC recovery, power management, long soak and performance at scale — that's what ships. The same test intent runs in both, so a failure in Linux can be bisected by re-running the bare-metal version."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "풀서비스 이사업체 vs 직접 트럭 몰기"

- **Linux** = 풀서비스 이사업체. 주소(BAR) 배정, 짐 목록(열거), 엘리베이터 예약(IOMMU 매핑), 초인종 연결(MSI), 사고 보험(AER/DPC)까지 **알아서** 해 준다. 대신 업체 내부 절차가 길어서, 문제가 생기면 "업체 직원 누가 실수했나"를 찾기 어렵다.
- **Bare-metal** = 직접 트럭을 빌려서 이사. 모든 단계를 **내가 손으로** 한다 — 귀찮지만 무엇이 일어났는지 100% 안다. 검증(validation)에서는 이게 장점: 실패하면 원인은 **하드웨어 아니면 내 코드 몇 줄**이다.
- **이 포지션(Rivian silicon & board validation)** 은 "주로 C, bare-metal" [JD/스크린 확인] → Linux가 PCIe에 해 주는 일을 **직접 할 줄 아는지**가 핵심 질문이 된다.

### 1.2 비유 — "테스트 상대(test partner)"

- 새 SoC의 PCIe **root port**(본사 출입구)를 검증하려면 반대편에 **무언가가 붙어 있어야** 링크가 뜬다. 상용 NVMe를 꽂으면 동작은 하지만, "BAR를 1 GiB로 바꿔 줘", "일부러 completion을 늦게 줘", "에러 TLP를 보내 줘" 같은 **실험 요청은 못 들어준다**.
- 그래서 **FPGA에 PCIe endpoint를 만들어** 원하는 대로 행동하는 "스파링 파트너"를 쓴다. 또는 **두 번째 SoC를 endpoint 모드**로 돌려 Linux endpoint framework로 테스트 기능을 올린다.

### 1.3 Zynq UltraScale+ MPSoC 한 장 그림

```text
 ┌──────────────────────── Zynq UltraScale+ MPSoC ─────────────────────────────┐
 │  PS (Processing System, 하드 실리콘)            PL (Programmable Logic, FPGA)    │
 │  ┌──────────┐ ┌────────┐                       ┌─────────────────────────────┐ │
 │  │ A53 x4   │ │ R5F x2 │  ← Linux(PetaLinux)   │ 사용자 RTL / IP              │ │
 │  │ (APU)    │ │ (RPU)  │    or bare-metal      │  ┌───────────────────────┐  │ │
 │  └────┬─────┘ └───┬────┘                       │  │ PCIe integrated block │  │ │
 │       │  AXI 인터커넥트 │                         │  │ (PG213 PCIE4, 일부 소자)│  │ │
 │  ┌────┴───────────┴──────────────┐  AXI HP/HPM │  │ + XDMA(PG195)/QDMA(PG302)│ │ │
 │  │ PS-PCIe 컨트롤러 (NWL)         │◄──────────► │  └──────────┬────────────┘  │ │
 │  │ PCIe v2.1, Gen2 ≤ x4, RP 또는 EP│             │             │ GTH/GTY 트랜시버│ │
 │  │ AXI-PCIe bridge + 4ch DMA      │             └─────────────┼───────────────┘ │
 │  └────┬───────────────────────────┘                           │                 │
 │       │ PS-GTR x4 (PCIe/USB3/SATA/DP/SGMII 가 레인을 나눠 씀)    │                 │
 └───────┼────────────────────────────────────────────────────────┼─────────────────┘
         ▼                                                        ▼
   PCIe 슬롯/커넥터 (Gen2)                                   PCIe 엣지/케이블 (Gen3/Gen4 등, 소자별)
```

- 정리: **PS-PCIe**는 "칩에 박힌" Gen2 컨트롤러 (CPU 쪽에서 바로 씀). **PL-PCIe**는 FPGA fabric 쪽 하드 블록 + DMA IP — 더 빠른 세대·더 많은 레인이 가능하고 **endpoint로 만들어 테스트 상대**로 쓰기 좋다 [추정: 정확한 Gen/레인 상한은 소자(part)별 데이터시트 확인 필요].

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| Bare-metal | (OS 없음) | 리셋 후 내 C 코드가 바로 하드웨어를 만지는 실행 환경 | 이 포지션의 기본 작업 환경 |
| ECAM | Enhanced Configuration Access Mechanism | config space를 **메모리 주소처럼** 읽고 쓰는 창 (함수당 4 KiB) | bare-metal에서 lspci를 대신하는 유일한 길 |
| BAR sizing | (BAR 크기 측정) | BAR에 all-1s 쓰고 다시 읽어 "몇 비트가 고정인가"로 크기 계산 | 직접 주소 배정의 출발점 |
| Bridge window | (브리지 메모리 창) | root port/스위치가 "이 범위는 내 아래로" 보내는 주소 구간 | 안 열면 아래 장치에 MMIO 못 감 |
| Bus Master | (Command 레지스터 bit 2) | 장치가 **스스로** 메모리 읽기/쓰기(DMA·MSI)를 시작할 권한 | 안 켜면 DMA·MSI 모두 불가 |
| iATU | internal Address Translation Unit | 컨트롤러가 CPU 주소 ↔ PCIe 주소를 바꾸는 창 (DWC 용어) | config/MMIO/inbound DMA 창 설정 |
| PS / PL | Processing System / Programmable Logic | Zynq의 고정 CPU 영역 / FPGA 영역 | PCIe가 어느 쪽에 있느냐에 따라 드라이버·성능이 다름 |
| PS-GTR | PS Gigabit Transceiver | PS 쪽 고속 직렬 레인 4개 (여러 프로토콜이 공유) | PCIe에 몇 레인 줄지 Vivado에서 결정 |
| NWL | (Northwest Logic, PS-PCIe 코어 IP 이름) | ZynqMP PS-PCIe 컨트롤러 코어 | Linux 드라이버 이름 `pcie-xilinx-nwl.c` |
| XDMA | DMA/Bridge Subsystem for PCI Express (PG195) | PL에 넣는 PCIe DMA IP, H2C/C2H 채널 최대 4+4 | FPGA endpoint 테스트 상대의 표준 |
| QDMA | Queue DMA Subsystem (PG302) | 큐 기반 DMA, 최대 2048 큐 | 다중 큐·SR-IOV 테스트 |
| H2C / C2H | Host-to-Card / Card-to-Host | DMA 방향: 호스트 메모리 → 카드 / 카드 → 호스트 | 테스트 매트릭스의 축 |
| EPC / EPF | Endpoint Controller / Endpoint Function | Linux endpoint framework의 컨트롤러 드라이버 / 기능 드라이버 | SoC를 endpoint로 쓰는 표준 방법 |
| configfs | (커널 설정 파일시스템) | 디렉터리·파일 생성으로 커널 객체를 만드는 인터페이스 | EPF 생성·바인딩 방법 |
| PetaLinux | AMD(Xilinx) Linux 빌드 도구 | Vivado 하드웨어 정보(XSA)로 부트로더·커널·DT·rootfs를 빌드 | JD 우대 키워드 |
| XSA | Xilinx Support Archive | Vivado가 내보내는 하드웨어 설명 파일 | PetaLinux/Vitis 입력 |
| FSBL | First Stage Boot Loader | ZynqMP에서 PS 초기화(PS-GTR 포함)를 하는 첫 부트로더 | PCIe 레인 설정이 여기서 적용됨 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Linux가 해 주는 것 vs bare-metal에서 손으로 하는 것

| 일 | Linux가 해 주는 것 (어디서) | Bare-metal에서 직접 | 함정 |
|---|---|---|---|
| ECAM 매핑 | DT `reg` "cfg"/ACPI MCFG로 창을 `ioremap`, `pci_host_common` / 컨트롤러 드라이버 | MMU 페이지 테이블에 **Device-nGnRE**(캐시 안 됨)로 매핑하거나 MMU off | 캐시 가능 메모리로 매핑하면 config read가 캐시에서 나옴 |
| 링크 업 대기 | 컨트롤러 드라이버 (DWC `dw_pcie_wait_for_link`, NWL `nwl_wait_for_link`) — 10회 × 90 ms [11][3] | vendor link-up 비트 polling + timeout (예: NWL `PS_LINKUP_OFFSET` 0x238 bit0) [3] | timeout 없이 무한 루프 |
| 첫 config까지 대기 | `PCIE_RESET_CONFIG_WAIT_MS` 100 ms [11] | 링크 업 후 `delay_ms(100)` | 너무 이르면 장치 없음/CRS |
| 열거 | `pci_scan_root_bus_bridge()` → `pci_scan_child_bus()` (probe.c) | DFS: dev 0–31 × fn 0–7, Vendor ID ≠ 0xFFFF, header type bit 7(multi-function) | bridge의 **버스 번호를 먼저** 써야 아래가 보임 |
| BAR sizing | `__pci_read_base()` (probe.c) | all-1s write → read → 복원, 64-bit는 상위 dword도 | sizing 중 Memory decode **off**, 64-bit BAR 두 칸 소비 |
| BAR 할당 | `pci_bus_assign_resources()` (setup-bus.c / setup-res.c), 큰 것부터·정렬 | 창(window)에서 `align_up(next, size)`로 잘라 씀 | 정렬 낭비, 64-bit pref 창 부재 → "no space" |
| Bridge window | `pci_setup_bridge()` | Memory Base/Limit(0x20/0x22), Pref Base/Limit(0x24/0x26) + Upper32(0x28/0x2C) | 1 MiB 단위, base > limit = 닫힘 |
| Enable | `pci_enable_device()` + `pci_set_master()` | Command 레지스터 bit 1(Memory) + bit 2(Bus Master) | Command/Status가 같은 dword — **32-bit RMW 금지** (Status RW1C) |
| MSI/MSI-X | `pci_alloc_irq_vectors()`, irqdomain (GIC ITS, GICv2m, 컨트롤러 내장) | MSI cap에 Address(ITS doorbell)/Data 쓰고 Enable, GIC/ITS 테이블 직접 설정 | Arm ITS는 DeviceID(=RID 매핑) 테이블까지 필요 |
| DMA 매핑·캐시 | DMA API (`dma_map_single`, `dma_sync_*`) + IOMMU | 물리 주소 직접 사용, **캐시 clean/invalidate 직접** (Arm `DC CVAC`/`DC IVAC`), SMMU는 bypass 또는 직접 설정 | non-coherent에서 캐시 유지 누락 → 데이터 깨짐 (LP15 S6) |
| 에러 보고 | AER/DPC 드라이버, 로그·sysfs 카운터, recovery 콜백 | AER cap(0x01) 레지스터를 직접 읽고 W1C로 지우기, 컨트롤러 misc IRQ 처리 | 지우지 않으면 다음 에러가 안 보임 |
| LTSSM 관측 | 대부분 직접 노출 안 함 (DWC debugfs `ltssm_status` 정도) [12] | vendor 레지스터 직접 polling·로깅 (DWC `PORT_LINK_DEBUG0` 0x728) [13] | **bare-metal이 오히려 더 잘 보임** |
| 리셋 | `pci_reset_function()` (FLR, SBR), hotplug | PERST# GPIO, Secondary Bus Reset bit, FLR bit 직접 | 리셋 후 다시 100 ms + 재열거 |
| 전원 관리 | ASPM 정책, runtime PM, D3cold | 링크 제어 레지스터 직접 (보통 끔) | 검증 시 PM 상태를 명시적으로 고정 |
| 도구 | `lspci`, `setpci`, sysfs, dmesg | 내 printf 디코더 (lspci 대체), JTAG 메모리 뷰 | 디코더 자체 검증 필요 |

### 2.2 Bare-metal bring-up 순서 (코드와 1:1)

```text
 1. 클럭·리셋 해제, PHY init (PS-GTR/SerDes)         ← SoC/보드별 (FSBL이 하기도)
 2. PERST# 해제 (전원 안정 후 ≥ 100 ms)              ← GPIO
 3. link-up polling (≤ 10 × 90 ms)                   ← vendor 레지스터
 4. 100 ms 대기 후 첫 config 요청                     ← spec 규칙
 5. scan_bus(0): Vendor ID, header type
      └ Type 1(bridge)?  primary/secondary/subordinate=0xFF 쓰기 → scan_bus(secondary) → subordinate 확정
      └ Type 0(endpoint)? size_bars()
 6. assign_bars(): 큰 BAR부터, 자기 크기로 정렬, 창 넘으면 "no space"
 7. bridge window 프로그램 (1 MiB 단위)
 8. Command |= Memory | Bus Master (BAR 미할당 함수는 제외)
 9. MSI: Address = 인터럽트 컨트롤러 doorbell, Data = 이벤트 ID, Enable
10. 링크 상태 리포트: root port LnkSta (speed, width, DLActive)
```

- ECAM 주소 (bus 8 bit, dev 5 bit, fn 3 bit, 4 KiB): `addr = base + (bus << 20 | dev << 15 | fn << 12 | off)` → 256 bus면 256 MiB 창 (LP15 실제 출력의 `40000000-4fffffff : PCI ECAM`과 일치).
- BAR 디코드: bit 0 = I/O(1)/Memory(0), bit [2:1] = 타입 (`00` 32-bit, `10` 64-bit), bit 3 = prefetchable, 크기 = `~(readback & ~0xF) + 1` (64-bit는 상위 dword 결합) [14].
- 레지스터 offset은 전부 `pci_regs.h`와 대조 (Command 0x04, Header Type 0x0E, BAR0 0x10, Primary/Secondary/Subordinate 0x18/0x19/0x1A, Memory Base/Limit 0x20/0x22, Pref 0x24/0x26, Upper32 0x28/0x2C, Cap ptr 0x34, MSI flags +0x02, addr lo +0x04, addr hi +0x08, data(64-bit) +0x0C) [14].

### 2.3 Zynq UltraScale+ MPSoC PS-PCIe (하드 컨트롤러)

| 항목 | 내용 | 확인 |
|---|---|---|
| 규격 | PCI Express **v2.1** compliant, AXI-PCIe bridge + DMA 모듈 | [확인됨: Xilinx wiki → UG1085 참조] [1][2] |
| 속도·폭 | **최대 Gen2 x4** (x1/x2/x4) | [확인됨] [1][2] |
| 모드 | **Root Port** 또는 **Endpoint** | [확인됨] [1][2] |
| DMA | **4채널** DMA, 채널마다 송신/수신 선택, PCIe 쪽 또는 AXI 쪽에서 제어, SG(scatter-gather) 큐가 PCIe·AXI 메모리 어디에나 | [확인됨] [2] |
| 레인 | PS-GTR 4레인 (USB3/SATA/DP/SGMII와 공유) — Vivado PS 설정(PCW)에서 할당, **FSBL**이 적용. ZCU102 PetaLinux BSP 기본은 **x2 Gen2** | [확인됨: Xilinx wiki] [1] |
| Linux RP 드라이버 | `drivers/pci/controller/pcie-xilinx-nwl.c`, compatible `"xlnx,nwl-pcie-2.11"`, Kconfig `PCIE_XILINX_NWL` (RP만 지원) | [확인됨] [3][15] |
| DT reg-names / interrupts | `reg-names = "breg", "pcireg", "cfg"`, `interrupt-names = "msi0", "msi1", "intx", "dummy", "misc"`, `msi-controller` | [확인됨: Xilinx wiki 예시] [1] |
| 링크 비트 | `PS_LINKUP_OFFSET` 0x238: bit0 PCIe link up, bit1 PHY ready | [확인됨] [3] |
| ECAM | 드라이버가 `E_ECAM_CONTROL`에 enable/크기 설정, `E_ECAM_BASE_LO/HI`에 cfg 창 주소 | [확인됨] [3] |
| Bare-metal 드라이버 | embeddedsw `XilinxProcessorIPLib/drivers/pciepsu`: `XPciePsu_CfgInitialize()`, `XPciePsu_EnumerateBus()`, `XPciePsu_ReadConfigSpace()`, 예제 `xpciepsu_rc_enumerate_example.c`, EP용 `xpciepsu_ingress_ep_enable_example.c`/`..._egress_...` | [확인됨] [4] |
| EP 모드 호스트 쪽 | x86 호스트용 DMA 드라이버 `zynqmp-pspcie-epdma`, Device ID **0xA808**을 Vivado에서 설정해야 probe, PIO 테스트 전 EP에서 bare-metal ingress 예제 실행(BAR2 → PS-DDR, 1 MB) | [확인됨: Xilinx wiki] [2] |

- **실제 부팅 로그 (Xilinx wiki의 ZCU102 예시, 발췌)** [1]:

```text
nwl-pcie fd0e0000.pcie: PCI host bridge to bus 0000:00
pci_bus 0000:00: root bus resource [bus 00-ff]
pci_bus 0000:00: root bus resource [mem 0xe1000000-0xefffffff]
pci 0000:00:00.0: BAR 8: assigned [mem 0xe1000000-0xe10fffff]
pci 0000:01:00.0: BAR 0: assigned [mem 0xe1000000-0xe100ffff 64bit]
pci 0000:00:00.0: PCI bridge to [bus 01-0c]
pci 0000:00:00.0: bridge window [mem 0xe1000000-0xe10fffff]
```

| 줄 | 해석 |
|---|---|
| `nwl-pcie fd0e0000.pcie` | NWL 드라이버, 컨트롤러 레지스터 base 0xFD0E0000 |
| `root bus resource [mem 0xe1000000-0xefffffff]` | DT `ranges`의 32-bit 메모리 창 (~240 MiB) |
| `00:00.0: BAR 8: assigned [mem 0xe1000000-0xe10fffff]` | "BAR 8" = 구 커널 표기로 **bridge의 memory window** 리소스 (1 MiB 단위로 열림) |
| `01:00.0: BAR 0 ... 64bit` | 끝 장치(예: NIC)의 64-bit BAR가 창 맨 앞에 배치 |
| `PCI bridge to [bus 01-0c]` | root port의 secondary=1, subordinate=0x0c (스위치 뒤 여러 버스 대비) |

- **면접 포인트**: ZynqMP PS-PCIe는 Gen2라서, **Rivian SoC(아마 Gen4/5급 [추정])의 고속 PHY 검증 상대로는 부족**하다. 이 경우 PL의 PCIe 하드 블록(소자에 따라 Gen3/Gen4 [추정]) + XDMA/QDMA endpoint, 또는 Versal(CPM, `pcie-xilinx-cpm.c`)이 테스트 상대가 된다.

### 2.4 PL 쪽 PCIe IP와 Linux 드라이버

| IP / 드라이버 | 무엇 | 역할 | 확인 |
|---|---|---|---|
| **PG213** UltraScale+ Devices Integrated Block for PCI Express (PCIE4) | FPGA 안의 PCIe **하드 블록** 래퍼 (PHY + 링크·트랜잭션 계층) | 사용자 RTL이 TLP를 직접 다룸 | [확인됨: PG195가 PG213을 integrated block 가이드로 참조] [5] |
| **PG195** DMA/Bridge Subsystem for PCI Express (**XDMA**) | integrated block 위의 DMA 엔진 + AXI 브리지. **최대 4 H2C + 4 C2H 채널**, AXI-MM 또는 AXI-Stream, Bridge 모드(AXI↔PCIe) | **endpoint**: 호스트 ↔ FPGA 메모리 DMA / **root port** 모드도 가능 | [확인됨] [5] |
| **PG302** QDMA Subsystem | 큐 기반 DMA: **2048 큐 세트**(H2C/C2H/CMPT 링), 2048 MSI-X, 4 PF | 다중 큐, SR-IOV, 고성능 | [확인됨: PG302 5.0] [6] |
| `pcie-xilinx-dma-pl.c` | Linux **RP** 드라이버: PL XDMA/QDMA를 root port로 쓸 때. compatible `xlnx,xdma-host-3.00`, `xlnx,qdma-host-3.00`, Kconfig `PCIE_XILINX_DMA_PL` ("Soft IP which can act as Root Port", `ARCH_ZYNQMP`) | ZynqMP에서 PL로 Gen3/Gen4 RP 만들기 | [확인됨] [15][16] |
| `pcie-xilinx-nwl.c` | PS-PCIe RP 드라이버 (§2.3) | PS Gen2 RP | [확인됨] [3] |
| `pcie-xilinx.c` | "Xilinx AXI PCIe" host bridge 드라이버, compatible `xlnx,axi-pcie-host-1.00.a` | 구형 AXI PCIe 브리지 IP를 RP로 | [확인됨] [15] |
| `pcie-xilinx-cpm.c` | Versal **CPM** host bridge | Versal 하드 PCIe | [확인됨] [15] |
| `dma_ip_drivers` XDMA (`xdma.ko`, out-of-tree) | **호스트 쪽** 드라이버: FPGA가 endpoint일 때 호스트 OS에서 DMA 채널을 char device로 | 테스트 상대를 쓰는 SW | [확인됨] [8] |

### 2.5 PetaLinux / Vivado 흐름 (개요)

```text
 Vivado                                    PetaLinux                                   보드
 ──────                                    ─────────                                   ────
 Block Design: Zynq US+ PS IP             petalinux-create -t project --template zynqMP
   - PS-PCIe enable, 레인(PS-GTR) 선택     petalinux-config --get-hw-description=<dir/xsa>
   - RP/EP, refclk, BAR(EP)                 (DT generator가 PS/PL IP 노드 자동 생성)
   - (PL) XDMA/QDMA IP + AXI 연결          petalinux-config -c kernel   (PCIE_XILINX_NWL/DMA_PL)
 Generate bitstream → Export XSA ──────►   petalinux-config -c rootfs   (pciutils 등)
                                           project-spec/meta-user/.../system-user.dtsi (DT 수정)
                                           petalinux-build
                                           petalinux-package --boot --fsbl --fpga --u-boot ──► BOOT.BIN + image.ub/Image
                                                                                         ──► SD/JTAG(petalinux-boot --jtag)
```

- 명령 이름은 PetaLinux 표준 흐름 [추정: 정확한 옵션은 릴리스마다 다름, 최근 AMD는 Yocto 기반 흐름(EDF)으로 이동 중이라는 보도도 있음 — 버전 확인 필요].
- Xilinx wiki 기준 rootfs에 `pciutils` 넣는 위치: "Filesystem Packages → console/utils → pciutils" [1].
- bare-metal 대안: 같은 XSA로 **Vitis**에서 standalone BSP + `pciepsu`/`xdmapcie` 예제 빌드 → JTAG 로드 [4] [추정: Vitis 메뉴 세부].

### 2.6 FPGA XDMA endpoint를 테스트 상대로

```text
  [Rivian SoC 또는 Zynq PS = Root Port]  ══ PCIe 링크 ══  [FPGA: XDMA endpoint]
   Linux + xdma.ko (dma_ip_drivers)                     BAR0: AXI-Lite user 레지스터 (/dev/xdma0_user)
   /dev/xdma0_h2c_0 ── write() ──► H2C DMA ──────────►  AXI-MM → BRAM/DDR
   /dev/xdma0_c2h_0 ◄── read() ◄── C2H DMA ◄──────────  AXI-MM ← BRAM/DDR
   /dev/xdma0_events_N ◄── 사용자 IRQ (MSI/MSI-X)       usr_irq_req
   /dev/xdma0_control  ── XDMA 내부 레지스터
```

- 장치 노드 이름 (드라이버 소스 `devnode_names`): `xdma%d_user`, `xdma%d_control`, `xdma%d_xvc`, `xdma%d_events_%d`, `xdma%d_h2c_%d`, `xdma%d_c2h_%d`, `xdma%d_bypass_*` [8].
- 도구: `dma_to_device` 옵션 `-d device`(기본 `/dev/xdma0_h2c_0`) `-a address`(AXI 주소) `-s size` `-o offset` `-c count` `-f infile` `-w outfile` `-v` [8]. `dma_from_device`는 대칭(C2H). 스크립트 `load_driver.sh`, `run_test.sh`(MM/스트리밍 테스트), `perform_hwcount.sh`(64 B–4 MB throughput → `hw_log_h2c.txt`/`hw_log_c2h.txt`), 폴링 모드 `insmod xdma.ko poll_mode=1` [8].
- **왜 좋은 테스트 상대인가**: ① BAR 크기·개수·64-bit/pref를 비트스트림으로 바꿈 → S4 "no space" 경계 테스트 ② DMA 크기·정렬·경계 sweep → S6 ③ MSI/MSI-X vector 수 조절, polling vs interrupt 비교 → S5 ④ 사용자 RTL로 **일부러 이상 행동**(completion 지연·누락, poisoned TLP [추정: IP 옵션/사용자 로직 필요]) → 호스트 AER/timeout 경로 검증 ⑤ ILA(내부 로직 분석기)로 FPGA 쪽 TLP 관찰.
- 주의: `dma_ip_drivers`는 out-of-tree이고 주로 x86 호스트에서 검증된 것으로 보임 → Arm SoC 호스트에서는 DMA mask·캐시 일관성 패치가 필요할 수 있음 [추정].

### 2.7 Linux PCI endpoint framework (EPC / EPF)

```text
   Host (Root Complex)                         Endpoint SoC (Linux)
   ───────────────────                         ────────────────────
   pci_endpoint_test 드라이버  ◄══ PCIe ══►   EPC 드라이버 (예: DWC EP 모드)  ← /sys/class/pci_epc/<ctrl>
   kselftest pci_endpoint_test                    ▲  bind (configfs symlink)
   (6.14 이전: tools/pci/pcitest)                  │
                                               EPF 드라이버 pci-epf-test (또는 -ntb, -vntb, -mhi)
                                               /sys/kernel/config/pci_ep/functions/pci_epf_test/func1
```

- **EPC** = endpoint controller 드라이버 (SoC의 PCIe 컨트롤러를 EP 모드로), **EPF** = endpoint function 드라이버 (그 EP가 "무슨 장치인 척" 할지). mainline EPF: `pci-epf-test.c`, `pci-epf-ntb.c`, `pci-epf-vntb.c`, `pci-epf-mhi.c` [17].
- EP 쪽 절차 (문서 그대로의 흐름) [9]:

```bash
ls /sys/class/pci_epc/                           # 예: 51000000.pcie_ep
mount -t configfs none /sys/kernel/config
cd /sys/kernel/config/pci_ep/
mkdir functions/pci_epf_test/func1
echo 0x104c > functions/pci_epf_test/func1/vendorid
echo 0xb500 > functions/pci_epf_test/func1/deviceid
echo 32     > functions/pci_epf_test/func1/msi_interrupts
echo 2048   > functions/pci_epf_test/func1/msix_interrupts
ln -s functions/pci_epf_test/func1 controllers/51000000.pcie_ep/
echo 1 > controllers/51000000.pcie_ep/start      # 링크 시작
```

- 호스트 쪽: `lspci`에 `01:00.0 Unassigned class [ff00]: Texas Instruments Device b500` 류로 보임 → `pci_endpoint_test` 드라이버 바인드 → `make -C tools/testing/selftests/pci_endpoint` 후 `pci_endpoint_test` 실행 → TAP 출력 `# PASSED: 16 / 16 tests passed.` (BAR, legacy IRQ/MSI/MSI-X, read/write/copy) [9]. DMA COPY 테스트는 많은 컨트롤러에서 실패하므로 필터 옵션으로 제외하라고 문서가 안내 [9].
- `pcitest`(예전 `tools/pci/pcitest.c`)는 **6.14에서 kselftest로 이동·삭제** — 구 커널 자료와 명령이 다름에 주의 [10].
- **장점**: 같은 SoC 두 개(또는 SoC + x86)로 BAR/MSI/DMA 테스트가 **표준화**되고 CI 자동화 쉬움. **단점**: 상대도 같은 신규 실리콘이면 "누가 틀렸나"가 모호 → 알려진 양품 상대(FPGA/x86)와 교차.

### 2.8 "Would you validate under Linux or bare-metal?" — 판단 표

| 기준 | Bare-metal | Linux |
|---|---|---|
| 언제 가능 | **Day 0** (FPGA 프로토타입, first silicon, DDR만 돼도) | 부트로더·DT·드라이버가 준비된 뒤 |
| 가시성 | 레지스터·LTSSM 직접, 타이밍 결정적 | 추상화 뒤, 일부만 노출 (sysfs/debugfs) |
| 결정성 | 높음 (인터럽트·스케줄러 없음) | 낮음 (다른 작업·PM·IRQ가 섞임) |
| 에러 주입·특수 시퀀스 | 자유로움 (PERST# 토글, Gen 강제, 레지스터 직접) | 가능하지만 커널이 "도와주려다" 개입 |
| 링크/PHY 특성화 (speed×width 매트릭스, EQ preset sweep, margin) | ✅ 적합 | 🟡 setpci/pcilmr로 일부 |
| 시스템 통합 (IOMMU, AER/DPC 복구, PM, hotplug) | ❌ 직접 다 만들어야 함 | ✅ 적합 (제품이 실제로 쓰는 경로) |
| 성능 at scale, 장시간 soak, 실제 워크로드 | 🟡 최소 DMA 벤치 | ✅ fio, 실제 드라이버 |
| 실패 시 원인 범위 | HW 또는 내 코드 몇 줄 | HW, 커널, 드라이버, 설정… 넓음 |
| 결론 | **bring-up·특성화·최소 재현** | **통합·회귀·soak·고객 경로** |

- 실무 순서: bare-metal smoke(링크 업·열거·BAR·MSI·DMA 1회) → bare-metal 특성화(매트릭스·sweep·PVT) → Linux bring-up(DT·드라이버) → Linux 회귀(soak·PM·에러 복구) → Linux에서 실패 시 **같은 의도의 bare-metal 테스트로 재현해 bisect**.

## 3. Linux에서 볼 것

### 3.1 ZynqMP PS-PCIe RP가 올라왔는지

```bash
dmesg | grep -iE 'nwl-pcie|Link is|PHY link'      # "Link is UP" / "Link is DOWN" / "PHY link never came up" [3]
lspci -tv                                        # 00:00.0 root port 아래 01:00.0
sudo lspci -s 00:00.0 -vvv | grep -E 'LnkCap|LnkSta'   # Gen2(5GT/s) x2/x4 기대
cat /proc/interrupts | grep -i msi               # "Xilinx MSI" irqchip 사용 여부 [3]
```

```text
[예시 출력]
nwl-pcie fd0e0000.pcie: Link is UP
        LnkCap: Port #0, Speed 5GT/s, Width x4, ASPM L0s L1
        LnkSta: Speed 5GT/s, Width x2
```

- 해석: ZCU102 BSP 기본이 **x2 Gen2**이므로 `Width x2`는 "downgraded"가 아니라 **FSBL/PCW 설정** 결과일 수 있다 → x4가 필요하면 Vivado PCW에서 레인 재할당 후 FSBL 재생성 [1].

### 3.2 FPGA XDMA endpoint (호스트 쪽)

```bash
lspci -nn | grep -i xilinx                        # 예: [10ee:9038] 등, Vivado에서 설정한 ID
sudo ./load_driver.sh                             # insmod xdma.ko → /dev/xdma0_*
ls /dev/xdma0_*                                   # h2c_0, c2h_0, user, control, events_0..15
sudo ./dma_to_device   -d /dev/xdma0_h2c_0 -a 0x0 -s 4096 -c 1 -f pattern.bin
sudo ./dma_from_device -d /dev/xdma0_c2h_0 -a 0x0 -s 4096 -c 1 -w readback.bin
cmp pattern.bin readback.bin && echo DMA-OK
```

- `[미실행]` — 실보드 없음. 옵션 이름은 소스(`dma_to_device.c` long_opts)에서 확인 [8].

### 3.3 Endpoint framework (호스트 쪽 기대 출력)

```text
[예시 출력]  (문서 예시 형식)
00:00.0 PCI bridge: Texas Instruments Device 8888 (rev 01)
01:00.0 Unassigned class [ff00]: Texas Instruments Device b500
TAP version 13
1..16
...
# PASSED: 16 / 16 tests passed.
```

### 3.4 bare-metal로 같은 정보 — 이 노트 C 코드의 실제 출력

```text
[1] happy path: link comes up on the 3rd poll
  pci 0000:00:00.0: [1234:0100] type 01 class 0x060400
  pci 0000:01:00.0: [1234:0200] type 00 class 0x010802
  pci 0000:01:00.1: [1234:0201] type 00 class 0x120000
  pci 0000:01:00.1: BAR 0 [mem 0x600000000-0x6000fffff 64bit pref]: assigned
  pci 0000:01:00.0: BAR 0 [mem 0xe1000000-0xe1003fff 64bit]: assigned
  pci 0000:01:00.1: BAR 2 [mem 0xe1004000-0xe1004fff]: assigned
  root port LnkSta=0x2042: Gen2 x4 DLActive+
  -> PASS
[2] link never trains (Detect fails): only the root port is visible
  pcie: PHY link never came up
  pci 0000:00:00.0: [1234:0100] type 01 class 0x060400
  root port LnkSta=0x0000: Gen0 x0 DLActive-
  -> PASS
[3] prefetchable window 64 KiB < 1 MiB BAR: 'no space'
  ...
  pci 0000:01:00.1: BAR 0 [mem size 0x100000 64bit pref]: can't assign; no space
  pci 0000:01:00.1: not assigned; can't enable device
```

| 줄 | 해석 |
|---|---|
| `[1234:0100] type 01 class 0x060400` | Type 1 header + class 0x0604(PCI-to-PCI bridge) = root port. 형식은 실제 커널 열거 로그(`pci 0000:00:01.0: [1af4:1041] type 00 class 0x020000`, LP15 VM 출력)와 같게 맞춤 |
| `01:00.0` / `01:00.1` | 버스 번호 1은 **root port의 secondary bus에 1을 쓴 뒤에야** 보임 (테스트가 "쓰기 전 0xFFFF"를 assert) |
| BAR 순서 (1 MiB → 16 KiB → 4 KiB) | 큰 것부터 할당 → 정렬 낭비 최소 (Linux도 같은 원리) |
| `LnkSta=0x2042` | bit 13 DLLLA(0x2000) + width 4(0x040) + speed 2(0x2) = Gen2 x4 DLActive+ |
| `[2]` | 링크가 안 뜨면 endpoint는 안 보이고 root port만 — LP15 S1과 동일 |
| `[3]` | 창이 BAR보다 작으면 Linux와 같은 문구로 실패, 그 함수는 enable 안 함 — LP15 S4 |

## 4. 꼬리 질문 체인

### 4.1 LP00 체인 — "Would you validate PCIe bare-metal or under Linux?"

<details><summary>답 펼치기</summary>

- **Q. Would you validate PCIe bare-metal or under Linux?** — "Both, in a deliberate order. Bare-metal first for bring-up and characterization — it's available on day zero on the FPGA prototype, it's deterministic, I can touch every register and toggle PERST# or force a speed without a kernel stepping in, and when it fails the suspect list is short. Then Linux for integration, soak, performance and error recovery, because that's the path the product uses." · 한국어: 둘 다, 순서대로 — bare-metal(bring-up·특성화) → Linux(통합·soak·복구).
- **꼬리1. What does Linux give you that bare-metal doesn't, and vice versa?** — "Linux gives me a complete, production-grade stack: resource allocation, IOMMU, MSI domains, AER and DPC recovery, power management, hotplug, and real drivers and workloads like fio. Bare-metal gives me visibility and control: direct LTSSM and vendor registers, exact timing, fault injection, and no hidden policy like ASPM or retrain quirks." · 한국어: Linux = 완성된 스택·실제 경로 / bare-metal = 가시성·제어·결정성.
- **꼬리2. How would you build a test partner endpoint on an FPGA?** — "Put an XDMA or QDMA subsystem on the PCIe hard block in endpoint mode, connect its AXI master to BRAM or DDR and an AXI-Lite BAR to a register block, and on the host use the XDMA driver's character devices for H2C and C2H transfers. Because I control the bitstream, I can change BAR sizes, vector counts and add custom logic to misbehave — delay completions, generate errors — to test the host's corner cases. ILA gives me visibility on the FPGA side." · 한국어: XDMA/QDMA EP + BRAM/DDR + AXI-Lite BAR, 비트스트림으로 BAR·vector·이상 행동 조절.
- **꼬리3. Your bare-metal test passes and Linux fails. What now?** — "That's a gift: the hardware path works, so the difference is in what Linux configures. I diff the config space and controller registers between the two — MPS, ASPM, BARs, MSI, IOMMU — then apply Linux's settings one at a time in the bare-metal test until it fails." · 한국어: 두 환경의 config/레지스터 diff → Linux 설정을 하나씩 bare-metal에 적용.

</details>

### 4.2 "How do you test DMA when you don't have a real endpoint device?"

<details><summary>답 펼치기</summary>

- **Q. How do you test DMA without a real endpoint?** — "Three options. An FPGA endpoint with XDMA — the most controllable. The Linux PCI endpoint framework: a second board, or the same SoC type, in endpoint mode running pci-epf-test, and the host runs the pci_endpoint_test suite for BARs, interrupts and read/write/copy transfers. Or, for the controller's own DMA, a loopback or internal-memory mode if the IP supports it. Pre-silicon, a transactor or BFM in emulation." · 한국어: FPGA XDMA / endpoint framework / loopback·BFM.
- **꼬리1. What's the risk of using the same new SoC as both ends?** — "Common-mode bugs: if both sides share the same PHY or controller bug, they may agree with each other and the test passes. I'd always cross-check against a known-good partner — an FPGA or an x86 root complex." · 한국어: 공통 버그는 서로 맞춰져 통과 → 양품 상대와 교차.
- **꼬리2. What's in your DMA test matrix?** — "Direction, size from one byte to megabytes, source and destination alignment, 4 KiB and other boundary crossings, MPS and MRRS settings, IOMMU on and off, interrupt versus polling, concurrent channels, and CRC comparison of every buffer." · 한국어: 방향·크기·정렬·경계·MPS/MRRS·IOMMU·IRQ/poll·동시 채널·CRC.

</details>

### 4.3 Zynq 특화

<details><summary>답 펼치기</summary>

- **Q. Where is PCIe on a Zynq UltraScale+ MPSoC?** — "Two places. The PS has a hard PCIe 2.1 controller, up to Gen2 x4, root port or endpoint, with an AXI bridge and a four-channel DMA, using the shared PS-GTR lanes; Linux drives it with the NWL driver. The PL can have PCIe integrated blocks on the GT transceivers with XDMA or QDMA, which is faster and very flexible — that's what I'd use as a test partner." · 한국어: PS-PCIe(Gen2 x4, NWL) + PL 하드 블록(XDMA/QDMA).
- **꼬리1. The link comes up x2 but you configured a x4 card. Why?** — "On ZynqMP the PS-GTR lane assignment comes from the PS configuration in Vivado and is applied by the FSBL — the ZCU102 BSP defaults to x2 Gen2. So first I check the hardware design and FSBL before suspecting the board." · 한국어: PCW/FSBL 레인 설정 먼저 (ZCU102 BSP 기본 x2).
- **꼬리2. How does PetaLinux know about your PCIe IP?** — "Vivado exports the XSA, PetaLinux imports it with get-hw-description, and the device-tree generator creates the PCIe node — compatible, reg-names breg/pcireg/cfg, interrupts, ranges. I override details in system-user.dtsi and enable the controller driver in the kernel config." · 한국어: XSA → DT generator → system-user.dtsi.
- **꼬리3. PS-PCIe is only Gen2 — how is that useful for validating a Gen4/Gen5 SoC?** — "Not for PHY characterization at speed. It's useful for protocol-level and software bring-up — enumeration, BARs, MSI, DMA flows — at low speed. For high-speed validation I'd use a PL integrated block or a Versal/other FPGA that matches the target generation, or a commercial exerciser." · 한국어: 프로토콜·SW 수준엔 유용, 고속 PHY엔 PL/Versal/익서사이저.

</details>

### 4.4 Bare-metal 코드 세부

<details><summary>답 펼치기</summary>

- **Q. Walk me through BAR sizing.** — "Disable memory decode in the Command register, save the BAR, write all ones, read back, restore. The low four bits are type and prefetch flags; the remaining writable bits give the size: size equals the inverse of the masked value plus one. For a 64-bit BAR I do the same on the upper dword and combine. Then re-enable decode." · 한국어: decode off → 저장 → all-1s → 읽기 → 복원 → `~(v & ~0xF)+1`.
- **꼬리1. Why disable decode during sizing?** — "While the BAR holds all ones, the device would claim a huge, bogus address range; any other access to that range would be misrouted." · 한국어: all-1s 동안 엉뚱한 주소를 디코드하므로.
- **꼬리2. Why not write the Command register with a 32-bit read-modify-write?** — "Command and Status share a dword, and Status has write-one-to-clear bits. Reading Status and writing the same value back clears any error bits that were set. Use a 16-bit access." · 한국어: Status RW1C를 지워 버림 → 16-bit 접근.
- **꼬리3. What does the scan do with a bridge?** — "Program primary, secondary and a temporary subordinate of 0xFF so config requests are forwarded, recurse into the secondary bus, then set subordinate to the highest bus found. Without the secondary bus number nothing behind the bridge is reachable." · 한국어: 버스 번호 먼저 → 재귀 → subordinate 확정.

</details>

## 5. Don 경험 연결

- "At Solidigm I brought up the PCIe and NVMe subsystem IP on FPGA prototypes before RTL freeze, bare-metal on Cortex-R8, R82 and M0+, so I've lived the endpoint side of this — BARs, doorbells, DMA engines and MSI-X from the device's point of view — and the Rivian role is the mirror image from the root-complex side." ([숫자 채우기]: FPGA 플랫폼 종류, 찾은 RTL 버그 수)
- "The test-partner idea is what we did there too: when the host side was the unknown, we used a known-good host; when our controller was the unknown, we needed a controllable partner to inject corner cases." ([숫자 채우기]: 사례 1개)
- "I haven't shipped on Zynq or PetaLinux specifically, but the flow — export hardware, generate the device tree, enable the controller driver, then validate bare-metal and under Linux — is the same methodology I used on our FPGA platform; the toolchain names change, the validation logic doesn't." (갭 정직하게 + 브릿지)
- "I also wrote performance and SoC verification tests for I2C, SPI, DMA and SRAM/DRAM before RTL freeze, so writing a bare-metal enumeration and DMA test like this one is the kind of thing I'd have running on day one of silicon."

## 6. 실습 / 코드

### 6.1 `onsitePrep/lp16/bm_pcie_bringup.c` — 검증됨

- 무엇: bare-metal 스타일 RC bring-up 골격 (link-up polling → 100 ms → DFS 스캔 + 버스 번호 → BAR sizing → 큰 BAR부터 할당 → root port 창 → Memory+Bus Master → MSI) + **가짜 ECAM 백엔드** (함수별 4 KiB config + 쓰기 마스크로 BAR sizing·RO 비트 의미를 그대로 흉내, 링크 다운이면 all-1s, secondary bus가 맞아야 bus 1 응답).
- 빌드·실행 (macOS) [확인됨]:

```bash
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined onsitePrep/lp16/bm_pcie_bringup.c -o /tmp/bm_lp16 && /tmp/bm_lp16
# → 5개 테스트 PASS, "ALL PASS" (전체 출력: onsitePrep/lp16/bm_pcie_bringup.out)
cc ... -DBAREMETAL_TARGET -fsyntax-only onsitePrep/lp16/bm_pcie_bringup.c   # 실 MMIO 접근 함수 문법 확인만
```

| 테스트 | 무엇을 증명 | LP15 연결 |
|---|---|---|
| [1] happy path | 버스 번호 쓰기 전 bus 1 = 0xFFFF, 이후 3개 함수 열거, BAR 주소·정렬, 창 값, Command 비트, MSI 도착 | 정상 기준 |
| [2] link never up | 10 × 90 ms 후 "PHY link never came up", root port만 보임, DLActive- | S1 |
| [3] window too small | "can't assign; no space", 해당 함수만 enable 안 함 | S4 |
| [4] MSI needs Bus Master | Bus Master 끄면 MSI 미도착, 켜면 도착 | S5 |
| [5] cap loop guard | 자기 자신을 가리키는 capability list에서도 종료 | 방어 코드 |

- 핵심 코드 발췌 (BAR sizing — decode off, 64-bit 결합; 설명용으로 축약, 원본은 `size_bars()`):

```c
uint16_t cmd = (uint16_t)rd(c, b, d, f, PCI_COMMAND, 2);
wr(c, b, d, f, PCI_COMMAND, 2, cmd & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO));
for (unsigned i = 0; i < 6; i++) {
    unsigned off = PCI_BASE_ADDRESS_0 + 4 * i;
    uint32_t orig = rd(c, b, d, f, off, 4);
    wr(c, b, d, f, off, 4, 0xFFFFFFFFu);
    uint32_t lo = rd(c, b, d, f, off, 4);
    wr(c, b, d, f, off, 4, orig);
    if (lo == 0) continue;                       /* unimplemented */
    bool is64 = ((lo >> 1) & 3u) == 2u;
    uint64_t mask = lo & ~0xFull;
    if (is64) { /* same dance on the upper dword */ mask |= (uint64_t)hi << 32; }
    else      mask |= 0xFFFFFFFF00000000ull;
    size = ~mask + 1;  if (is64) i++;            /* upper half consumed */
}
wr(c, b, d, f, PCI_COMMAND, 2, cmd);
```

- 실 타깃 경로(`#ifdef BAREMETAL_TARGET`: volatile 8/16/32-bit MMIO + `dsb sy`)는 **문법 확인만** — 실제 ZynqMP/SoC에서 실행은 `[미검증]`. 실제 ZynqMP에서는 AMD의 `pciepsu` 드라이버(`XPciePsu_EnumerateBus`)가 같은 역할 [4].

### 6.2 랩 레시피 `[미실행]`

- **QEMU endpoint framework**: QEMU에는 EP 모드 컨트롤러 에뮬레이션이 일반적이지 않음 [추정] → EPF 실습은 실보드(DWC EP 지원 SoC 2개, 또는 SoC + x86)가 필요.
- **QEMU로 bare-metal ECAM 실습**: `qemu-system-aarch64 -M virt`의 generic ECAM(`pci-host-ecam-generic`) 위에 `-device pcie-root-port,id=rp1,chassis=1 -device edu,bus=rp1` → 이 노트 C 코드의 `plat_ops_t`를 실제 ECAM 주소로 바꿔 bare-metal 이미지로 실행 [미실행: ECAM base는 QEMU virt DT에서 확인 필요].

## 7. 자주 틀리는 것

1. **ECAM 창을 캐시 가능한 메모리로 매핑** — config read가 캐시 값을 돌려줌. Arm은 Device 속성으로.
2. **Command 레지스터를 32-bit RMW로 씀** — 같은 dword의 Status RW1C 비트를 지워 에러 증거 소실.
3. **BAR sizing 중 decode를 켜 둠** — all-1s 주소를 장치가 잠깐 디코드.
4. **64-bit BAR를 2개의 32-bit BAR로 취급** — 상위 dword를 다음 BAR로 sizing하면 쓰레기 크기.
5. **bridge 버스 번호를 안 쓰고 bus 1을 스캔** — 아무것도 안 보임 (C 코드 테스트 [1]의 첫 assert).
6. **링크 down 상태에서 다운스트림 config 접근** — SoC에 따라 hang (LP15 S10). 링크 업 가드 + 100 ms 대기.
7. **ZynqMP x2를 "다운그레이드"로 오진** — PCW/FSBL 레인 설정(ZCU102 BSP 기본 x2 Gen2)일 수 있음 [1].
8. **`pcitest` 명령을 최신 커널에서 찾음** — 6.14부터 kselftest `pci_endpoint_test` [10].
9. **같은 신규 SoC 두 개만으로 EP 테스트 후 "통과"** — 공통 버그는 안 잡힘, 양품 상대와 교차.
10. **XDMA 드라이버를 in-tree로 착각** — `dma_ip_drivers`는 out-of-tree, mainline에는 RP 쪽 `pcie-xilinx-dma-pl.c`만 있음 [8][15].

## 8. 출처

1. AMD/Xilinx Wiki, "Linux ZynqMP PS-PCIe Root Port Driver" (v2.1, Gen2 x4, RP/EP, ZCU102 x2 Gen2 BSP 기본, DT 예시, 부팅 로그) — https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842417/Linux+ZynqMP+PS-PCIe+Root+Port+Driver
2. AMD/Xilinx Wiki, "Zynq UltraScale+ MPSoC PS-PCIe End Point Driver" (4채널 DMA, Device ID 0xA808, ingress BAR2→PS-DDR) — https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/2141323327/Zynq+UltraScale+MPSoC+PS-PCIe+End+Point+Driver
3. Linux kernel, `drivers/pci/controller/pcie-xilinx-nwl.c` — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/pcie-xilinx-nwl.c
4. Xilinx embeddedsw, `XilinxProcessorIPLib/drivers/pciepsu` (src + examples), `xdmapcie` — https://github.com/Xilinx/embeddedsw/tree/master/XilinxProcessorIPLib/drivers/pciepsu
5. AMD PG195 DMA/Bridge Subsystem for PCI Express (Introduction, Configurable Components: 4 H2C + 4 C2H) — https://docs.amd.com/r/en-US/pg195-pcie-dma/Introduction , https://docs.amd.com/r/en-US/pg195-pcie-dma/Configurable-Components-of-the-Subsystem
6. AMD PG302 QDMA Subsystem for PCI Express (Features: 2048 queues, 2048 MSI-X) — https://docs.amd.com/r/5.0-English/pg302-qdma/Features
7. AMD PG213 UltraScale+ Devices Integrated Block for PCI Express — https://docs.amd.com/r/zh-CN/pg213-pcie4-ultrascale-plus/%E5%BC%95%E8%A8%80
8. Xilinx dma_ip_drivers, XDMA Linux kernel driver (readme, `tools/dma_to_device.c`, `xdma/xdma_cdev.c`) — https://github.com/Xilinx/dma_ip_drivers/tree/master/XDMA/linux-kernel
9. Linux kernel docs, "PCI Test User Guide" (endpoint framework configfs, pci_endpoint_test kselftest) — https://docs.kernel.org/PCI/endpoint/pci-test-howto.html
10. LKML, "[PATCH v6 0/4] Migrate PCI Endpoint Subsystem tests to Kselftest" (pcitest 제거, v6.14) — https://lkml.iu.edu/hypermail/linux/kernel/2501.2/05464.html
11. Linux kernel, `drivers/pci/pci.h` (`PCIE_LINK_WAIT_MAX_RETRIES`, `PCIE_LINK_WAIT_SLEEP_MS`, `PCIE_RESET_CONFIG_WAIT_MS`) — https://github.com/torvalds/linux/blob/master/drivers/pci/pci.h
12. Linux kernel, `drivers/pci/controller/dwc/pcie-designware-debugfs.c` — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware-debugfs.c
13. Linux kernel, `drivers/pci/controller/dwc/pcie-designware.h` — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware.h
14. Linux kernel, `include/uapi/linux/pci_regs.h` — https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
15. Linux kernel, `drivers/pci/controller/Kconfig` + 디렉터리 (`pcie-xilinx.c`, `pcie-xilinx-dma-pl.c`, `pcie-xilinx-nwl.c`, `pcie-xilinx-cpm.c`) — https://github.com/torvalds/linux/tree/master/drivers/pci/controller
16. Linux kernel DT binding, `xlnx,xdma-host.yaml` — https://kernel.org/doc/Documentation/devicetree/bindings/pci/xlnx%2Cxdma-host.yaml
17. Linux kernel, `drivers/pci/endpoint/functions/` (pci-epf-test, -ntb, -vntb, -mhi) — https://github.com/torvalds/linux/tree/master/drivers/pci/endpoint/functions
18. AMD UG1137 Zynq UltraScale+ MPSoC Software Developer Guide, PCI Express (UG1085 "Controller for PCI Express" 참조) — https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/PCI-Express

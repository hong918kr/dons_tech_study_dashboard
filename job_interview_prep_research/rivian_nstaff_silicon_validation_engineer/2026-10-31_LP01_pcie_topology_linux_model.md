# LP01 · PCIe 토폴로지와 Linux의 PCI 모델
<!-- theme: pastel -->

> **묶음**: A 기초 모델 · **우선**: P1 · **선행 노트**: [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **다음 노트**: [LP02 config space](2026-10-31_LP02_config_space_capabilities.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · **관련**: [N01 lspci·LTSSM](2026-10-31_N01_pcie_lspci_ltssm.html) · **코드**: [onsitePrep/lp02/pcicfg_walk.c](onsitePrep/lp02/pcicfg_walk.c) (QEMU 실측에도 사용) · **실측**: Lima VM(Apple VZ, ACPI) + QEMU `virt`(DT, root port 2개 + 4-port switch)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"Linux는 PCIe 장치를 어떻게 보나?"**, **"Type 0 vs Type 1 header?"**, **"switch가 있으면 버스 번호가 어떻게 매겨지나?"**, **"ECAM base는 어디서 오나 (x86 vs ARM SoC)?"**, **"4-port switch는 BDF를 몇 개 쓰나?"**
- 외울 것 1 — **PCIe는 점대점(point-to-point) 링크인데 소프트웨어에는 "PCI 버스 + 브리지 나무"로 보인다.** Root Port와 switch의 각 포트는 전부 **가상 PCI-to-PCI 브리지(Type 1 header)**, 실제 장치는 **Type 0 header**.
- 외울 것 2 — **브리지마다 primary / secondary / subordinate 버스 번호 3개**: "내가 서 있는 버스 / 내 바로 아래 버스 / 내 아래 가장 큰 버스". 깊이 우선(depth-first)으로 번호를 매긴다. 4-port switch = **함수 5개(upstream 1 + downstream 4), 버스 5개(내부 버스 1 + 포트별 4)** — QEMU 실측: root port `[01-06]` → upstream `01:00.0 [02-06]` → downstream `02:00.0..02:03.0` → 버스 03..06.
- 외울 것 3 — **ECAM 주소** = `base + (bus << 20 | dev << 15 | fn << 12 | offset)`, 함수마다 4 KiB. base는 **x86/ARM 서버 = ACPI MCFG 테이블**, **임베디드 ARM = Device Tree 호스트 노드의 `reg`** (예: `compatible = "pci-host-ecam-generic"`). 실측: Lima `ECAM at [mem 0x40000000-0x4fffffff]` (MCFG), QEMU virt `ECAM at [mem 0x4010000000-0x401fffffff]` (DT).
- Linux 객체: **`pci_host_bridge` → 루트 `pci_bus` → `pci_dev`(함수 하나) → 브리지면 `pci_dev->subordinate` = 자식 `pci_bus`**, 드라이버는 `pci_driver`(id_table + probe). sysfs `/sys/devices/.../pci0000:00/0000:00:01.0/0000:01:00.0/...` 경로 자체가 토폴로지.
- **영어 30초 (headline Q: "How does Linux see a PCIe device?")**: "Physically PCIe is a tree of point-to-point links, but software sees it as PCI buses connected by bridges. The root complex exposes a host bridge; every root port and every switch port is a virtual PCI-to-PCI bridge with a Type 1 header, and real devices have Type 0 headers. Linux finds the config-space window — ECAM from the ACPI MCFG table on x86 or servers, or from the device-tree `reg` on an embedded SoC — then scans depth-first: read vendor ID at each BDF, and for each bridge assign primary, secondary and subordinate bus numbers and recurse. Each function becomes a `pci_dev` on a `pci_bus`, shows up in sysfs and lspci, and gets matched to a `pci_driver` by vendor and device ID."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 신도시의 도로와 주소

PCIe를 처음 보면 "점대점 고속 직렬 링크"라는 설명과 "버스·브리지·버스 번호"라는 설명이 같이 나와서 헷갈립니다. 두 설명이 **둘 다 맞습니다**. 전선은 점대점이고, 소프트웨어가 보는 주소 체계는 옛날 PCI 버스 방식을 그대로 흉내 냅니다. 이유는 하나 — **옛날 PCI용 OS·드라이버를 고치지 않고 그대로 쓰려고**.

비유로 먼저 잡겠습니다.

- **CPU와 메모리 = 시청**. 시청에서 바깥 동네로 나가는 문이 **Root Complex(RC)** 입니다 — "CPU 세계(메모리 주소)"와 "PCIe 세계"를 잇는 관문.
- RC에서 뻗어 나가는 **각 도로의 입구 = Root Port(RP)**. 도로 하나에는 **딱 한 집(또는 교차로 하나)** 만 연결됩니다. 이게 "점대점(point-to-point)"의 뜻입니다. 옛날 PCI는 한 버스(골목)에 집 여러 채가 같이 붙어 있었다면, PCIe는 도로 하나 = 상대 하나.
- 한 도로를 여러 집에 나눠 주려면 **교차로 = Switch** 를 둡니다. 교차로는 들어오는 쪽 1개(**Upstream Port**)와 나가는 쪽 여러 개(**Downstream Port**)가 있고, 교차로 내부에도 짧은 **내부 도로(내부 가상 버스)** 가 하나 있습니다.
- 도로 끝의 **집 = Endpoint(EP)** — NVMe SSD, NIC, GPU, 가속기, FPGA 카드.
- **도로 번호 = Bus 번호(0–255)**, 도로 위 **번지 = Device 번호(0–31)**, 집 안의 **호수 = Function 번호(0–7)**. 셋을 합친 주소 **BDF(Bus:Device.Function)** 가 장치의 "주민등록 주소". 여러 도시(독립된 RC 묶음)가 있으면 앞에 **Domain(=Segment) 번호**가 붙습니다: `0000:03:00.0`.
- **다리 = Bridge**. 한 도로에서 다른 도로로 넘어가는 지점. PCIe에서는 **Root Port와 switch의 모든 포트가 소프트웨어 눈에는 "다리"** 입니다. 다리 표지판에는 "**나는 몇 번 도로 위에 있고(primary), 내 바로 건너편은 몇 번 도로(secondary)이고, 나를 건너가면 최대 몇 번 도로까지 있다(subordinate)**"가 적혀 있습니다. 우편물(설정 요청)은 이 표지판만 보고 "이 다리를 건너야 하나?"를 결정합니다.
- **명함 = Configuration Space(설정 공간)**. 모든 집과 다리는 표준 양식의 명함(256 B 또는 4 KiB)을 갖고 있어서, 시청은 주소만 알면 명함을 읽어 "누구세요(vendor/device ID), 어떤 종류(class), 메모리 얼마나 필요하세요(BAR)"를 알 수 있습니다. 명함 양식이 두 가지: **집용(Type 0 header)** 과 **다리용(Type 1 header — 버스 번호 3개와 "내 뒤로 가는 주소 범위" 칸이 있음)**.
- **인구 조사 = Enumeration(열거)**. 부팅 때 OS(또는 펌웨어)가 0번 도로부터 모든 번지를 두드려 명함을 읽고, 다리를 만나면 건너편 도로에 새 번호를 붙이며 깊이 우선으로 내려갑니다. 이 결과가 `lspci` 목록입니다.

### 1.2 그림: 이 노트에서 실제로 띄운 QEMU 토폴로지

```text
                       CPU / DRAM
                           │
             ┌─────────────┴──────────────┐
             │  Root Complex (호스트 브리지) │   00:00.0  [1b36:0008]  Type 0, class 0600
             │  ECAM window 0x40_1000_0000  │   (config 요청을 PCIe TLP로 바꿔 주는 관문)
             └──────┬──────────────┬───────┘
          bus 00    │              │
      ┌─────────────┴──┐      ┌────┴───────────┐
      │ Root Port      │      │ Root Port      │
      │ 00:01.0 Type 1 │      │ 00:02.0 Type 1 │
      │ pri00 sec01    │      │ pri00 sec07    │
      │ sub06          │      │ sub07          │
      └──────┬─────────┘      └──────┬─────────┘
   bus 01    │ (링크 1개)       bus 07│ (링크)
      ┌──────┴─────────────────┐     │
      │ Switch Upstream Port   │   07:00.0 igb NIC (Type 0, SR-IOV PF)
      │ 01:00.0  Type 1        │
      │ pri01 sec02 sub06      │
      └──────┬─────────────────┘
   bus 02    │  ← switch 내부 가상 버스
   ┌─────────┼───────────┬─────────────┐
 02:00.0   02:01.0     02:02.0       02:03.0      ← Downstream Port 4개 (각각 Type 1)
 sec03     sec04       sec05         sec06
   │ bus03   │ bus04     │ bus05       │ bus06
 03:00.0   04:00.0     05:00.0      (비어 있음: 링크 없음 → DLActive-)
  NVMe     e1000e NIC  edu (QEMU 교육용 장치)
```

- 점대점 링크는 그림에서 **세로선 하나하나**: RP↔Upstream, 각 Downstream↔Endpoint, RP2↔igb.
- 링크 하나 = 버스 하나 (bus 01, 03, 04, 05, 06, 07). 링크 건너편 장치는 그 버스의 **device 0** 이라서 BDF가 `XX:00.0` 꼴입니다.
- **bus 02만 링크가 아님** — switch 칩 내부의 가상 버스. downstream port들이 여기에 device 0,1,2,3으로 붙어 있습니다.

### 1.3 왜 이렇게 복잡하게 "가짜 PCI 버스"를 흉내 내나

1. **소프트웨어 호환**: 1990년대 PCI용으로 만든 열거 코드, 드라이버, BIOS가 그대로 돌아감. PCIe 장치는 레지스터 위치까지 PCI와 같은 config header를 갖습니다.
2. **라우팅이 단순해짐**: 스위치는 "이 요청의 bus 번호가 내 secondary~subordinate 범위 안인가?"만 보면 어느 포트로 보낼지 압니다 (config/ID 라우팅). 메모리 요청은 "주소가 내 memory window 안인가?"만 봅니다.
3. **각 포트를 독립 관리**: downstream port 하나하나가 브리지이므로 포트마다 링크 상태(LnkSta), hotplug 슬롯, AER(에러 보고), 리셋(Secondary Bus Reset)을 따로 갖습니다 — 검증할 때 "어느 포트의 링크가 문제인가"를 포트 단위로 볼 수 있는 이유.

### 1.4 config space에 어떻게 "손을 대나" — 두 가지 문

설정 공간(명함)은 메모리처럼 그냥 있는 게 아니라, **RC가 대신 읽어다 주는** 공간입니다. CPU가 RC에게 부탁하는 방법이 두 가지:

| 방법 | 비유 | 실제 |
|---|---|---|
| **CF8/CFC** (x86 전용, 옛날 방식) | 창구 직원에게 "3번 도로 0번지 0호 명함 18번째 줄 보여주세요" 쪽지를 내고(0xCF8에 주소 쓰기), 창구에서 답을 받음(0xCFC 읽기) | I/O 포트 2개. 두 단계라 락이 필요, 표준으로는 **256 B까지만** |
| **ECAM** (Enhanced Configuration Access Mechanism) | 모든 집의 명함을 한 벽에 번호순으로 붙여 둔 게시판. 주소 계산만 하면 바로 읽음 | 물리 메모리 창 하나(버스 256개면 256 MiB). 그 주소에 load/store → RC가 config TLP로 변환. **함수당 4 KiB 전부** |

ECAM 게시판이 메모리 어디에 붙어 있는지는 하드웨어가 정하고, OS는 **펌웨어가 알려줘야** 압니다: x86·ARM 서버는 **ACPI MCFG 테이블**, 임베디드 ARM은 **Device Tree**.

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| RC | Root Complex | CPU/메모리 세계와 PCIe 세계를 잇는 관문 (SoC 안의 PCIe 호스트 컨트롤러 + 주변 로직) | config/메모리 요청을 TLP로 바꿔 내보내고, 장치의 DMA를 메모리로 들여보냄 |
| Host bridge | — | RC를 소프트웨어가 보는 모습. 보통 `00:00.0` (class 0600) | Linux의 `pci_host_bridge` 객체, 루트 버스 0의 부모 |
| RP | Root Port | RC에서 나가는 PCIe 링크 하나의 RC쪽 끝 | 링크 상태(LnkSta)·AER·hotplug를 이 포트에서 봄 — "장치가 안 보일 때" 제일 먼저 보는 곳 |
| Switch | — | 링크 하나를 여러 링크로 펼치는 칩 | Upstream 1 + Downstream N, 내부 가상 버스 1개 |
| USP / DSP | Upstream / Downstream Switch Port | 스위치의 위쪽 포트 / 아래쪽 포트 | 둘 다 Type 1 브리지. DSP마다 새 버스 |
| EP | Endpoint | 실제 기능을 하는 장치 (SSD, NIC, 가속기) | Type 0 header, BAR로 MMIO 요청 |
| RCiEP | Root Complex integrated Endpoint | 링크 없이 RC 안에 붙은 장치 (SoC 내장 블록) | 링크 레지스터가 없거나 의미 없음 |
| Bridge (P2P) | PCI-to-PCI Bridge | 한 버스에서 다른 버스로 요청을 넘기는 장치 | 버스 번호·주소 window를 가진 Type 1 header |
| Type 0 / Type 1 header | — | config space 앞 64 B의 두 양식: 장치용 / 브리지용 | 0x0E Header Type 하위 7비트로 구분. 0x10–0x3F 배치가 다름 |
| BDF | Bus:Device.Function | 함수 하나의 주소. bus 8비트, device 5비트, function 3비트 | lspci·sysfs·dmesg 어디서나 쓰는 ID. TLP의 Requester ID와 같은 16비트 |
| devfn | device+function | Linux에서 `dev<<3 \| fn` 로 합친 8비트 | `PCI_DEVFN()`, `PCI_SLOT()`, `PCI_FUNC()` 매크로 |
| Domain / Segment | PCI Segment Group | 버스 0–255 묶음 하나. 독립 host bridge마다 다를 수 있음 | `0000:` 접두사. 다중 RC SoC·서버에서 `0001:` 등장 |
| Primary / Secondary / Subordinate | — | 브리지의 "내가 있는 버스 / 바로 아래 버스 / 아래 최대 버스" | config 요청 라우팅의 근거. 잘못되면 아래 장치가 안 보임 |
| Enumeration | 열거 | 모든 BDF를 읽어 장치를 찾고 버스 번호·주소를 배정하는 과정 | 끝나야 lspci에 보이고 드라이버 probe |
| ECAM | Enhanced Configuration Access Mechanism | config space를 메모리 주소로 매핑한 창 | 4 KiB 전체(확장 capability) 접근, bare-metal에서도 그대로 사용 |
| CF8/CFC | Configuration Mechanism #1 | x86 I/O 포트 0xCF8(주소)/0xCFC(데이터) 방식 | 레거시, 표준은 256 B만 |
| MCFG | (ACPI) PCI Memory-mapped Configuration table | 펌웨어가 OS에 "ECAM base, segment, bus 범위"를 알려주는 ACPI 표 | x86·ARM 서버(SBSA)에서 ECAM 위치의 출처 |
| DT | Device Tree | 임베디드에서 하드웨어 배치를 기술하는 데이터 | PCIe 호스트 노드의 `reg`, `ranges`, `bus-range` |
| TLP | Transaction Layer Packet | PCIe 링크 위로 다니는 요청/응답 패킷 | config read도 TLP (CfgRd0/CfgRd1) |
| Type 0 / Type 1 config request | — | 목적지가 바로 아래 버스면 Type 0, 더 아래면 Type 1 | 브리지가 secondary 버스에 도착한 Type 1을 Type 0으로 바꿈 |
| `pci_dev` / `pci_bus` / `pci_host_bridge` / `pci_driver` | Linux 커널 구조체 | 함수 하나 / 버스 하나 / RC 하나 / 드라이버 하나 | 커널 PCI 코어의 기본 객체 (§2.5) |
| portdrv (`pcieport`) | PCIe port bus driver | Root/Switch 포트에 붙는 커널 드라이버 | AER·PME·hotplug·대역폭 알림 "서비스"를 포트별로 생성 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 구성 요소와 Device/Port Type

PCIe capability의 Capabilities Register(+0x02) bits[7:4] "Device/Port Type"이 그 함수가 토폴로지에서 무엇인지 스스로 알려줍니다 [확인됨: `pci_regs.h` `PCI_EXP_FLAGS_TYPE`]:

| 값 | `PCI_EXP_TYPE_*` | 의미 | header |
|---|---|---|---|
| 0x0 | ENDPOINT | PCIe Endpoint | Type 0 |
| 0x1 | LEG_END | Legacy Endpoint (I/O 요청 등 옛 기능 허용) | Type 0 |
| 0x4 | ROOT_PORT | Root Port | Type 1 |
| 0x5 | UPSTREAM | Switch Upstream Port | Type 1 |
| 0x6 | DOWNSTREAM | Switch Downstream Port | Type 1 |
| 0x7 | PCI_BRIDGE | PCIe-to-PCI/PCI-X Bridge | Type 1 |
| 0x8 | PCIE_BRIDGE | PCI/PCI-X-to-PCIe Bridge | Type 1 |
| 0x9 | RC_END | Root Complex Integrated Endpoint | Type 0 |
| 0xA | RC_EC | Root Complex Event Collector | Type 0 |

- Linux dmesg가 장치를 처음 찾을 때 이 값을 그대로 찍습니다 (QEMU 실측, §3.2): `type 01 class 0x060400 PCIe Root Port`, `PCIe Switch Upstream Port`, `PCIe Switch Downstream Port`, `type 00 class 0x010802 PCIe Endpoint`. PCIe capability가 없는 장치는 `conventional PCI endpoint` (QEMU `edu`, Lima의 virtio 장치 전부).
- **Switch = USP 1개 + 내부 버스 + DSP N개**. USP와 DSP 모두 class 0x0604(PCI bridge), Type 1 header. 내부 버스 위에서 DSP들은 device 번호 0..N-1을 가집니다 (QEMU에서 `addr=0..3`으로 지정).
- **Root Port도 브리지**: host bridge(00:00.0)와 같은 버스 0 위에 있고, secondary 쪽이 링크입니다.

### 2.2 BDF · devfn · domain

```text
 16-bit Routing/Requester ID  (TLP 헤더, AER 로그, SR-IOV VF 계산에 쓰이는 값)
 15            8 7        3 2      0
 ┌──────────────┬──────────┬────────┐
 │  Bus [7:0]   │ Dev[4:0] │ Fn[2:0]│        lspci 표기:  DDDD:BB:DD.F  (domain:bus:device.function)
 └──────────────┴──────────┴────────┘        Linux:  devfn = PCI_DEVFN(dev, fn) = (dev & 0x1f) << 3 | (fn & 7)
                                               ARI 켜면 Dev+Fn 8비트 전부를 function 번호로 (함수 256개)
```

- 버스 256 × 장치 32 × 함수 8 = 한 segment에 최대 65,536 함수.
- **PCIe 링크 아래의 장치는 항상 device 0** (점대점이라 그 버스에 상대가 하나뿐). 그래서 DSP는 device ≠ 0인 Type 0 config 요청을 내보내지 않고, Linux도 PCIe 다운스트림 포트 아래에서는 device 0만 스캔합니다 (`pci_scan_slot`의 "only slot 0" 처리) [확인됨: `drivers/pci/probe.c` `only_one_child()` — 주석 "A PCIe Downstream Port normally leads to a Link with only Device 0 on it (PCIe spec r3.1, sec 7.3.1)", 예외는 `PCI_SCAN_ALL_PCIE_DEVS` 플래그]. 예외: **ARI**(Alternative Routing-ID Interpretation)를 쓰면 device 필드까지 function 번호로 써서 함수 256개 — SR-IOV VF가 많을 때 필요 (QEMU igb에 ARI ext cap `[150]` 실측).
- **Domain(segment)**: host bridge마다 독립된 0–255 버스 공간. 단일 RC 시스템은 `0000` 하나. 다중 컨트롤러 SoC(예: 자동차 SoC에 PCIe 컨트롤러 3–4개)는 DT `linux,pci-domain` 또는 자동 할당으로 `0000`, `0001`, … — QEMU virt DT에 `linux,pci-domain = <0x00>` 실측.
- **multi-function**: Header Type(0x0E) bit 7 = 1이면 function 1–7도 스캔. 아니면 function 0만.

### 2.3 Type 0 vs Type 1 header — 어디가 다른가 (상세 비트는 [LP02](2026-10-31_LP02_config_space_capabilities.html))

| offset | Type 0 (Endpoint) | Type 1 (Bridge / Port) |
|---|---|---|
| 0x00–0x0F | **공통**: Vendor ID, Device ID, Command, Status, Revision, Class Code, Cache Line, Latency, **Header Type**, BIST | 공통 |
| 0x10–0x27 | **BAR0–BAR5** (6개) | **BAR0–BAR1** (0x10, 0x14) → **0x18 Primary Bus, 0x19 Secondary Bus, 0x1A Subordinate Bus**, 0x1B Sec Latency, 0x1C/0x1D I/O Base/Limit, 0x1E Secondary Status, **0x20/0x22 Memory Base/Limit**, **0x24/0x26 Prefetchable Base/Limit** |
| 0x28–0x2F | CardBus CIS, **Subsystem Vendor/Device ID** | **0x28/0x2C Prefetchable Base/Limit Upper 32** |
| 0x30–0x33 | Expansion ROM Base | **0x30/0x32 I/O Base/Limit Upper 16** |
| 0x34 | **Capabilities Pointer** | Capabilities Pointer |
| 0x38 | (reserved) | Expansion ROM Base (bridge용 `PCI_ROM_ADDRESS1`) |
| 0x3C–0x3D | Interrupt Line / Pin | Interrupt Line / Pin |
| 0x3E–0x3F | Min_Gnt / Max_Lat | **Bridge Control** (bit 6 = **Secondary Bus Reset** → hot reset, [LP11](2026-10-31_LP11_reset_types.html)) |

- [확인됨: `include/uapi/linux/pci_regs.h` 6.18 — `PCI_PRIMARY_BUS 0x18`, `PCI_SECONDARY_BUS 0x19`, `PCI_SUBORDINATE_BUS 0x1a`, `PCI_MEMORY_BASE 0x20`, `PCI_PREF_MEMORY_BASE 0x24`, `PCI_PREF_BASE_UPPER32 0x28`, `PCI_IO_BASE_UPPER16 0x30`, `PCI_ROM_ADDRESS1 0x38`, `PCI_BRIDGE_CONTROL 0x3e`, `PCI_BRIDGE_CTL_BUS_RESET 0x40`]
- Bridge의 **window**: "이 주소 범위의 메모리 요청은 아래로 넘긴다". Memory Base/Limit은 상위 12비트만 의미 → 1 MiB 단위. 실측 root port 00:01.0 `20: 00 10 90 10` → Base 0x1000 → `0x10000000`, Limit 0x1090 → `0x109fffff` (= lspci `Memory behind bridge: 10000000-109fffff [size=10M]`).

### 2.4 버스 번호 — primary / secondary / subordinate와 depth-first 열거

**규칙** (PCI-to-PCI Bridge spec, Linux `pci_scan_bridge_extend()`):

1. 버스 N을 스캔하며 device 0–31(함수 0–7)의 Vendor ID를 읽는다. `0xFFFF` = 없음.
2. Type 1 브리지를 만나면: `primary = N`, `secondary = 지금까지 쓴 최대 번호 + 1`, `subordinate = 0xFF`(임시로 활짝 열어 둠) 로 쓴다.
3. secondary 버스로 **재귀**(깊이 우선) — 그 아래 브리지들도 같은 방식.
4. 돌아오면 `subordinate = 그 아래에서 실제로 쓴 최대 번호` 로 줄인다.
5. 다음 장치로 진행.

**Config 요청 라우팅**: 브리지는 bus 번호 B로 가는 Type 1 config 요청을 받으면

- `B == secondary` → **Type 0으로 변환**해 아래 링크(버스)로 전달
- `secondary < B ≤ subordinate` → **Type 1 그대로** 아래로 전달
- 범위 밖 → 무시 (다른 브리지 몫)

**워크드 예제 — 이 노트의 QEMU 실측 (§3.2 dmesg와 1:1 대응)**

```text
 스캔 순서          발견                         쓰는 값 (pri/sec/sub)     dmesg 근거
 bus 00 dev 00   host bridge 00:00.0 (Type 0)   -
 bus 00 dev 01   Root Port 00:01.0             00 / 01 / ff(임시)       "bridge configuration invalid ([bus 00-00]), reconfiguring"
   bus 01 dev 00   Switch USP 01:00.0          01 / 02 / ff
     bus 02 dev 00   DSP 02:00.0               02 / 03 / ff
       bus 03          NVMe 03:00.0 (Type 0)    → sub=03                "busn_res: [bus 03-ff] end is updated to 03"
     bus 02 dev 01   DSP 02:01.0               02 / 04 / ff
       bus 04          e1000e 04:00.0           → sub=04                "[bus 04-ff] end is updated to 04"
     bus 02 dev 02   DSP 02:02.0               02 / 05 / ff
       bus 05          edu 05:00.0              → sub=05
     bus 02 dev 03   DSP 02:03.0               02 / 06 / ff
       bus 06          (빈 슬롯)                 → sub=06                "[bus 06-ff] end is updated to 06"
   USP 01:00.0 sub 확정 = 06                                            "[bus 02-ff] end is updated to 06"
 RP 00:01.0 sub 확정 = 06                                               "[bus 01-ff] end is updated to 06"
 bus 00 dev 02   Root Port 00:02.0             00 / 07 / ff → 07
   bus 07          igb 07:00.0                                          "[bus 07-ff] end is updated to 07"
```

결과 표 (실측 `setpci`/`lspci -vvv` 값):

| 브리지 | 종류 | primary | secondary | subordinate | 아래에 있는 것 |
|---|---|---|---|---|---|
| 00:01.0 | Root Port | 00 | 01 | **06** | switch 전체 (bus 01–06) |
| 01:00.0 | Switch USP | 01 | 02 | 06 | switch 내부 버스 02 + DSP 4개 아래 03–06 |
| 02:00.0 | Switch DSP | 02 | 03 | 03 | NVMe |
| 02:01.0 | Switch DSP | 02 | 04 | 04 | e1000e |
| 02:02.0 | Switch DSP | 02 | 05 | 05 | edu |
| 02:03.0 | Switch DSP | 02 | 06 | 06 | 빈 슬롯 (번호는 그래도 하나 받음) |
| 00:02.0 | Root Port | 00 | 07 | 07 | igb |

**"4-port switch는 BDF를 몇 개 쓰나?"** — **함수(BDF) 5개**: USP 1 (`01:00.0`) + DSP 4 (`02:00.0`–`02:03.0`). **버스 번호 5개를 새로 소비**: 내부 버스 1 (`02`) + DSP마다 1 (`03`–`06`). 거기에 switch를 매단 root port 링크 자체가 버스 `01`. 아래 endpoint는 각 DSP 버스의 device 0.

**"왜 DSP마다 버스가 따로?"** — DSP는 P2P 브리지이고, **브리지의 secondary 쪽은 정의상 새 버스**입니다. 게다가 PCIe 링크는 점대점이라 그 버스엔 장치가 하나뿐 — 링크 = 버스. 덕분에 포트마다 독립된 라우팅(버스 범위·주소 window), 링크 상태, hotplug, 리셋, 에러 격리(DPC)가 가능합니다.

**Hotplug 여유 번호**: 버스 번호는 열거 때 꽉 채워 주므로, 나중에 switch가 꽂히면 번호가 모자랄 수 있습니다. Linux는 hotplug 가능한 브리지에 `pci=hpbussize=nn`(기본 `DEFAULT_HOTPLUG_BUS_SIZE 1`)만큼 여유를 줍니다 [확인됨: `drivers/pci/pci.c`]. 이 랩 커널은 `CONFIG_HOTPLUG_PCI_PCIE` 꺼져 있어 빈 DSP 02:03.0도 버스 06 하나만 받았습니다 [실측]. 펌웨어(UEFI/ACPI)가 미리 번호를 정해 두는 x86에서는 펌웨어 정책이 우선.

### 2.5 Linux의 PCI 객체 모델

```text
 struct pci_host_bridge  (RC 하나)          bus->ops = 이 RC의 config read/write 방법 (ECAM 등)
   .bus ───────────────┐                    .windows = RC가 아래로 넘기는 MMIO/IO 창 (DT ranges / ACPI _CRS)
   .native_aer, ...    │                    .domain_nr
                       ▼
 struct pci_bus  number=0x00 (루트 버스)
   .devices ──► pci_dev 00:00.0 (host bridge)
            ──► pci_dev 00:01.0 (Root Port) ── .subordinate ──► struct pci_bus number=0x01
            ──► pci_dev 00:02.0 (Root Port) ── .subordinate ──► pci_bus 0x07 ──► pci_dev 07:00.0
                                                                │  .self = 00:01.0 (이 버스를 만든 브리지)
                                                                │  .parent = bus 0x00
                                                                └─ .devices ──► pci_dev 01:00.0 (USP) ── .subordinate ──► pci_bus 0x02 ...

 struct pci_dev (함수 하나):  .bus, .subordinate(브리지면), .devfn, .vendor/.device, .class, .hdr_type,
                              .pcie_cap/.msi_cap/.msix_cap/.pm_cap (capability offset 캐시), .cfg_size(256 또는 4096),
                              .resource[] (BAR + 브리지 window), .driver
 struct pci_driver:           .name, .id_table(vendor/device/class 매칭표), .probe(), .remove(), .err_handler(AER 복구), .sriov_configure
```

- [확인됨: `include/linux/pci.h` 6.18 — 위 필드 이름 그대로 존재. `pci_bus.self` 주석 "Bridge device as seen by parent", `pci_dev.subordinate` "Bus this device bridges to"]
- **Root/switch 포트에는 `pcieport` 드라이버**(portdrv)가 붙고, 포트별 기능을 **"서비스" 장치**로 쪼갭니다. 이름 `BDF:pcieXYZ` = `((port type − 4) << 8) | service` [확인됨: `drivers/pci/pcie/portdrv.c` `get_descriptor_id`]. service 비트: PME 0x01 · AER 0x02 · Hotplug 0x04 · DPC 0x08 · BWCTRL 0x10. 실측: root port에 `pcie001 pcie002 pcie004 pcie010`, DSP(type 6 → 0x200)에 `pcie204` (hotplug).
- **열거 흐름 (요약)**: 호스트 드라이버 probe(`pci-host-generic` 등) → `pci_host_probe()` → `pci_scan_root_bus_bridge()` → 버스마다 `pci_scan_child_bus()` → `pci_scan_slot()` → `pci_scan_device()`(Vendor ID 읽기, CRS/RRS 재시도) → `pci_setup_device()`(header type, class, BAR sizing) → 브리지면 `pci_scan_bridge_extend()` 재귀 → `pci_assign_unassigned_bus_resources()`(window·BAR 주소 배정) → `pci_bus_add_devices()` → sysfs 등록 + 드라이버 매칭 → `probe()`. 자세한 리소스 배정은 [LP04](2026-10-31_LP04_enumeration_resources_hotplug.html).

### 2.6 Config 접근: CF8/CFC vs ECAM

**CF8/CFC (x86 Configuration Mechanism #1)**

```text
 0xCF8 (32-bit write)
 31   30..24     23..16   15..11   10..8    7..2      1..0
 ┌──┬─────────┬────────┬────────┬───────┬─────────┬────┐
 │EN│reserved │  Bus   │ Device │  Fn   │ Reg[7:2]│ 00 │     → 그다음 0xCFC(+reg&3) 에서 8/16/32-bit 읽기/쓰기
 └──┴─────────┴────────┴────────┴───────┴─────────┴────┘
```

- Reg 필드가 8비트(dword 64개) → **256 B까지만**. 확장 config(0x100–0xFFF: AER, SR-IOV…)는 못 봄.
- Linux `arch/x86/pci/direct.c`의 `PCI_CONF1_ADDRESS(bus, devfn, reg)` = `0x80000000 | ((reg & 0xF00) << 16) | (bus << 16) | (devfn << 8) | (reg & 0xFC)` [확인됨]. `reg[11:8]`을 bits 27:24에 넣는 부분은 일부 AMD 칩의 확장 config 접근용 비표준 확장 [추정: 칩셋별 활성화 필요].
- 주소 쓰기 → 데이터 접근 2단계라 **전역 락**(`pci_config_lock`)이 필요 — ECAM은 단일 load/store라 그럴 필요가 적음.

**ECAM (PCIe spec "Enhanced Configuration Access Mechanism")**

```text
 addr = ECAM_BASE + (Bus << 20) + (Dev << 15) + (Fn << 12) + Offset          Offset 0x000..0xFFF
        │            │ 8 bit       │ 5 bit       │ 3 bit       │ 12 bit
        └ 펌웨어가 알려줌   1 MiB/버스      32 KiB/장치     4 KiB/함수
 버스 256개 = 256 MiB 창.  Linux: PCIE_ECAM_OFFSET(bus, devfn, where) = bus<<20 | devfn<<12 | (where & 0xfff)
```

- [확인됨: `include/linux/pci-ecam.h` — `PCIE_ECAM_BUS_SHIFT 20`, `PCIE_ECAM_DEVFN_SHIFT 12`, `PCIE_ECAM_REG_MASK 0xfff`]. devfn<<12 = dev<<15 | fn<<12 로 같은 식.
- **계산 예 (실측 값으로)**: QEMU ECAM base `0x4010000000`, NVMe `03:00.0`의 PCIe cap이 `0x80`, Link Status는 cap+0x12 = `0x92` → `0x4010000000 + (3<<20) + 0x92 = 0x4010300092`. Lima VM: base `0x40000000`, virtio-net `00:01.0` → `0x40000000 + (1<<15) = 0x40008000`.
- **ECAM은 "정렬된 접근"만 보장**: 많은 컨트롤러가 config에 대해 32-bit 정렬 접근만 지원하고, 8/16-bit는 읽고 마스킹해야 합니다 (`pci_generic_config_read32` 계열) — bare-metal 코드에서 흔한 함정.

### 2.7 ECAM base는 어디서 오나 — ACPI MCFG vs Device Tree

| 플랫폼 | 출처 | 커널 경로 | 실측 |
|---|---|---|---|
| x86 PC/서버 | ACPI **MCFG** 테이블 (+ hotplug host bridge는 `_CBA` 메서드) | `arch/x86/pci/mmconfig-shared.c` → `pci_mmcfg` | — |
| ARM 서버 (SBSA/ServerReady), Apple VZ VM | ACPI **MCFG** + DSDT의 `PNP0A08` host bridge `_CRS`(창) | `drivers/acpi/pci_mcfg.c` `pci_mcfg_lookup()` → `pci_ecam_create()` | **Lima**: `ACPI: MCFG table detected, 1 entries` → `acpi PNP0A08:00: ECAM at [mem 0x40000000-0x4fffffff] for [bus 00-ff]` |
| 임베디드 ARM/RISC-V SoC | **Device Tree** 호스트 노드 `reg` (+ `ranges` = MMIO/IO 창, `bus-range`) | `drivers/pci/controller/pci-host-generic.c` (`pci-host-ecam-generic`) 또는 SoC 전용 드라이버 | **QEMU virt**: `compatible = "pci-host-ecam-generic"; reg = <0x40 0x10000000 0x00 0x10000000>` → `ECAM at [mem 0x4010000000-0x401fffffff]` |
| DesignWare 등 SoC 컨트롤러 | DT `reg-names = "dbi", "config", ...` — 루트 포트는 DBI(컨트롤러 레지스터)로, 그 아래는 **iATU outbound window**를 매번 버스/devfn으로 다시 프로그램하는 "가짜 ECAM" | `drivers/pci/controller/dwc/pcie-designware-host.c` | [LP05](2026-10-31_LP05_soc_host_controller_integration.html)에서 상세 |

**실측 MCFG 바이트 해석 (Lima VM `/sys/firmware/acpi/tables/MCFG`)**

```text
00000000: 4d43 4647 3c00 0000 0171 4150 504c 4520  MCFG<....qAPPLE      ← 시그니처 "MCFG", 길이 0x3c=60 B
00000010: 4170 706c 6520 567a 0100 0000 4141 504c  Apple Vz....AAPL
00000020: 2704 1820 0000 0000 0000 0000 0000 0040  '.. ...........@     ← 0x24–0x2B reserved, 0x2C부터 entry 1
00000030: 0000 0000 0000 00ff 0000 0000            ............
```

| offset | 바이트 | 필드 | 값 |
|---|---|---|---|
| 0x2C | `00 00 00 40 00 00 00 00` | Base Address (64-bit LE) | **0x0000_0000_4000_0000** |
| 0x34 | `00 00` | PCI Segment Group | 0 |
| 0x36 | `00` | Start Bus | 0x00 |
| 0x37 | `ff` | End Bus | 0xFF → 창 크기 256 × 1 MiB = 256 MiB → `0x40000000-0x4fffffff` ✓ dmesg와 일치 |
| 0x38 | `00 00 00 00` | reserved | |

- [확인됨: 36 B ACPI 헤더 + 8 B reserved + 16 B entry = 60 B = 0x3C, 테이블 길이 필드와 일치]

## 3. Linux에서 볼 것

### 3.1 명령 한눈에

| 명령 | 보는 것 |
|---|---|
| `lspci -tv` | **트리**: `-[domain:bus]-+-dev.fn-[sec-sub]----dev.fn 이름` |
| `lspci -D` / `lspci -nn` | domain 포함 BDF / 숫자 ID |
| `sudo lspci -s <bridge> -vv` | `Bus: primary=.. secondary=.. subordinate=..`, `Memory behind bridge`, PCIe cap의 포트 type |
| `ls -l /sys/bus/pci/devices/` | BDF → 실제 장치 경로 symlink (경로 = 토폴로지) |
| `ls /sys/class/pci_bus/` | 존재하는 버스 번호 목록 |
| `readlink -f /sys/bus/pci/devices/0000:03:00.0` | 이 장치의 조상 브리지 체인 |
| `cat /sys/bus/pci/devices/<bdf>/{vendor,device,class}` | ID |
| `dmesg \| grep -i -E "host bridge\|ECAM\|MCFG\|busn_res\|PCI bridge to"` | config 창 출처, 버스 번호 배정 과정 |
| `grep -i -E "pci\|ecam" /proc/iomem` (root) | ECAM 창, 버스별 MMIO window 계층 |
| `sudo setpci -s <bdf> SECONDARY_BUS.b` / `SUBORDINATE_BUS.b` | 버스 번호 레지스터 직접 |
| `lspci -F dump.txt -tv` | 다른 머신에서 떠 온 `lspci -x` 덤프로 오프라인 분석 (버그 리포트 분석에 유용) |

### 3.2 실측 A — QEMU `virt` (DT, root port 2 + switch 4 port) 

QEMU 게스트(BusyBox initramfs)에는 pciutils가 없어서, 게스트에서 [pcicfg_walk -x](onsitePrep/lp02/pcicfg_walk.c)로 각 함수의 `/sys/bus/pci/devices/*/config`를 `lspci -x` 형식으로 덤프 → Lima 호스트에서 `lspci -F`로 디코드했습니다 (덤프 원본: [onsitePrep/lp02/qemu_virt_switch_dump.txt](onsitePrep/lp02/qemu_virt_switch_dump.txt)).

**`lspci -F qemu_virt_switch_dump.txt -tv`** [실측]

```text
-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge
           +-01.0-[01-06]----00.0-[02-06]--+-00.0-[03]----00.0  Red Hat, Inc. QEMU NVM Express Controller
           |                               +-01.0-[04]----00.0  Intel Corporation 82574L Gigabit Network Connection
           |                               +-02.0-[05]----00.0  Device 1234:11e8
           |                               \-03.0-[06]--
           \-02.0-[07]----00.0  Intel Corporation 82576 Gigabit Network Connection
```

- `-[0000:00]-` : domain 0000, 루트 버스 00.
- `+-01.0-[01-06]` : 버스 00의 device 01 function 0 = root port. 대괄호 = **secondary–subordinate** 버스 범위.
- `----00.0-[02-06]` : 버스 01의 00.0 = switch USP (sec 02, sub 06).
- `+-00.0-[03]----00.0 NVMe` : 버스 02의 DSP 00.0, 아래 버스 03의 00.0이 NVMe. 범위가 한 개면 `[03]`.
- `\-03.0-[06]--` : 빈 DSP — 버스 번호는 받았지만 아래에 아무것도 없음 (링크가 없음, §3.4).
- `\-02.0-[07]` : 두 번째 root port, 아래 igb.

**게스트 dmesg 발췌** [실측, 줄별 해석]

```text
pci-host-generic 4010000000.pcie: host bridge /pcie@10000000 ranges:
pci-host-generic 4010000000.pcie:       IO 0x003eff0000..0x003effffff -> 0x0000000000
pci-host-generic 4010000000.pcie:      MEM 0x0010000000..0x003efeffff -> 0x0010000000
pci-host-generic 4010000000.pcie:      MEM 0x8000000000..0xffffffffff -> 0x8000000000
pci-host-generic 4010000000.pcie: ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]
pci-host-generic 4010000000.pcie: PCI host bridge to bus 0000:00
pci 0000:00:00.0: [1b36:0008] type 00 class 0x060000 conventional PCI endpoint
pci 0000:00:01.0: [1b36:000c] type 01 class 0x060400 PCIe Root Port
pci 0000:00:01.0: bridge configuration invalid ([bus 00-00]), reconfiguring
pci 0000:01:00.0: [104c:8232] type 01 class 0x060400 PCIe Switch Upstream Port
pci 0000:02:00.0: [104c:8233] type 01 class 0x060400 PCIe Switch Downstream Port
pci 0000:03:00.0: [1b36:0010] type 00 class 0x010802 PCIe Endpoint
pci_bus 0000:03: busn_res: [bus 03-ff] end is updated to 03
pci 0000:05:00.0: [1234:11e8] type 00 class 0x00ff00 conventional PCI endpoint
pci_bus 0000:02: busn_res: [bus 02-ff] end is updated to 06
pci_bus 0000:01: busn_res: [bus 01-ff] end is updated to 06
pci 0000:00:01.0: PCI bridge to [bus 01-06]
pcieport 0000:00:01.0: AER: enabled with IRQ 16
```

| 줄 | 해석 |
|---|---|
| `pci-host-generic 4010000000.pcie` | DT 노드 `pcie@10000000`(첫 `reg` 셀)에 `pci-host-generic` 드라이버가 바인드. 장치 이름은 ECAM 물리 주소 |
| `ranges: IO … MEM … MEM …` | DT `ranges` = RC가 PCI 쪽으로 넘겨주는 CPU 주소 창: I/O 64 KiB, 32-bit MMIO `0x10000000–0x3efeffff`, 64-bit MMIO `0x80_0000_0000–`. BAR는 이 안에서만 배정됨 |
| `ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]` | DT `reg = <0x40 0x10000000 0x0 0x10000000>` = base 0x40_1000_0000, 크기 256 MiB = 버스 256개 |
| `PCI host bridge to bus 0000:00` | `pci_host_bridge` 생성, 루트 버스 0 |
| `type 01 class 0x060400 PCIe Root Port` | header Type 1, class 0604 (PCI bridge), PCIe cap의 port type 4 |
| `bridge configuration invalid ([bus 00-00]), reconfiguring` | 펌웨어가 버스 번호를 안 써 놨음(DT 부팅, UEFI 없음) → **커널이 직접 번호 배정**. x86에서 BIOS가 이미 배정했다면 이 줄 대신 그 값을 유지 |
| `busn_res: [bus 03-ff] end is updated to 03` | §2.4의 4단계: 임시 subordinate ff를 실제 최대값으로 줄임 |
| `conventional PCI endpoint` | PCIe capability가 없는 장치 (QEMU edu). → cfg_size 256, 확장 capability 없음 |
| `PCI bridge to [bus 01-06]` | root port 00:01.0의 최종 secondary–subordinate |
| `pcieport … AER: enabled` | portdrv가 root port에 AER 서비스 연결 ([LP09](2026-10-31_LP09_aer_dpc_error_handling.html)) |

**sysfs — `ls -l /sys/bus/pci/devices`** [실측, BusyBox 색 코드 제거]

```text
0000:00:00.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:00.0
0000:00:01.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0
0000:00:02.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:02.0
0000:01:00.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0
0000:02:00.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:00.0
0000:02:03.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:03.0
0000:03:00.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:00.0/0000:03:00.0
0000:04:00.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:01.0/0000:04:00.0
0000:05:00.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:02.0/0000:05:00.0
0000:07:00.0 -> ../../../devices/platform/4010000000.pcie/pci0000:00/0000:00:02.0/0000:07:00.0
```

- `/sys/bus/pci/devices/` 는 **평평한 목록(symlink)**, 실체는 `/sys/devices/...` 의 **나무**. 경로를 읽으면 `RC(platform 장치) → pci0000:00(루트 버스) → root port → USP → DSP → endpoint` 가 그대로 보입니다.
- DT 시스템은 `platform/<ecam주소>.pcie/pci0000:00`, ACPI 시스템은 `/sys/devices/pci0000:00` 바로 아래 (§3.3).
- 같은 디렉터리 안에 `pci_bus/0000:03` 같은 **버스 객체**와 `0000:00:01.0:pcie002` 같은 **portdrv 서비스 장치**도 있음 (실측 `find`):

```text
./platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:00:01.0:pcie001      ← PME
./platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:00:01.0:pcie002      ← AER
./platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:00:01.0:pcie004      ← Hotplug
./platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:00:01.0:pcie010      ← Bandwidth notification
./platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:00.0/0000:02:00.0:pcie204   ← DSP(type6) hotplug
./platform/4010000000.pcie/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:00.0/pci_bus/0000:03
```

**함수별 요약** (게스트 스크립트 실측: `class`, 바인드된 `driver`, `config` 파일 크기)

```text
0000:00:00.0 class=0x060000 driver=         cfgsize=256
0000:00:01.0 class=0x060400 driver=pcieport cfgsize=4096
0000:01:00.0 class=0x060400 driver=pcieport cfgsize=4096
0000:02:00.0 class=0x060400 driver=pcieport cfgsize=4096
0000:03:00.0 class=0x010802 driver=         cfgsize=4096     ← nvme 드라이버가 모듈이라 안 붙음 (initramfs에 없음)
0000:04:00.0 class=0x020000 driver=e1000e   cfgsize=4096
0000:05:00.0 class=0x00ff00 driver=         cfgsize=256      ← PCIe cap 없음 → 256 B만
0000:07:00.0 class=0x020000 driver=igb      cfgsize=4096
```

**`/proc/iomem`** [실측] — 브리지 window가 버스 계층대로 중첩

```text
10000000-3efeffff : pcie@10000000
  10000000-109fffff : PCI Bus 0000:01
    10000000-109fffff : PCI Bus 0000:02
      10000000-101fffff : PCI Bus 0000:03
      10200000-104fffff : PCI Bus 0000:04
      10500000-106fffff : PCI Bus 0000:05
      10700000-108fffff : PCI Bus 0000:06
  10a00000-10bfffff : PCI Bus 0000:07
4010000000-401fffffff : PCI ECAM
```

**브리지 한 개를 `-vv`로 — root port 00:01.0** [실측 발췌]

```text
00:01.0 PCI bridge: Red Hat, Inc. QEMU PCIe Root port (prog-if 00 [Normal decode])
	Bus: primary=00, secondary=01, subordinate=06, sec-latency=0
	I/O behind bridge: 1000-5fff [size=20K] [16-bit]
	Memory behind bridge: 10000000-109fffff [size=10M] [32-bit]
	Prefetchable memory behind bridge: 0000008000000000-0000008000bfffff [size=12M] [64-bit]
	BridgeCtl: Parity- SERR+ NoISA- VGA- VGA16- MAbort- >Reset- FastB2B-
	Capabilities: [54] Express (v2) Root Port (Slot+), IntMsgNum 0
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-
```

- `Bus:` = 0x18/0x19/0x1A. `>Reset-` = Bridge Control bit 6 (Secondary Bus Reset) 꺼짐.
- `Express (v2) Root Port (Slot+)` = PCIe cap @0x54, port type 4, 슬롯 구현.

**`setpci`로 같은 값** (덤프 파일 대상, `-A dump -O dump.name=` 은 `lspci -F`와 같은 libpci "dump" 접근 방식) [실측]

```bash
$ setpci -A dump -O dump.name=dump.txt -s 00:01.0 SECONDARY_BUS.b      # → 01
$ setpci -A dump -O dump.name=dump.txt -s 00:01.0 SUBORDINATE_BUS.b    # → 06
$ setpci -A dump -O dump.name=dump.txt -s 00:01.0 CAP_EXP+0x12.w       # → 2011  (LnkSta: x1, 2.5GT/s, DLActive+)
$ setpci -A dump -O dump.name=dump.txt -s 02:03.0 CAP_EXP+0x12.w       # → 0011  (빈 DSP: DLActive-)
```

### 3.3 실측 B — Lima VM (Apple Virtualization.framework, ACPI)

```text
$ sudo lspci -tv
-[0000:00]-+-00.0  Apple Inc. Device 1a05
           +-01.0  Red Hat, Inc. Virtio 1.0 network device
           +-05.0  Red Hat, Inc. Virtio 1.0 console
           +-06.0  Red Hat, Inc. Virtio 1.0 block device
           +-07.0  Red Hat, Inc. Virtio 1.0 block device
           +-08.0  Red Hat, Inc. Virtio 1.0 file system
           +-09.0  Red Hat, Inc. Virtio 1.0 socket
           +-0a.0  Red Hat, Inc. Virtio 1.0 RNG
           +-0b.0  Red Hat, Inc. Virtio 1.0 balloon
           \-0c.0  Apple Inc. Device 1a06

$ ls -l /sys/bus/pci/devices
0000:00:00.0 -> ../../../devices/pci0000:00/0000:00:00.0
0000:00:01.0 -> ../../../devices/pci0000:00/0000:00:01.0
...
0000:00:0c.0 -> ../../../devices/pci0000:00/0000:00:0c.0
```

- **브리지가 하나도 없는 평평한 버스 0** — 모든 장치가 host bridge와 같은 버스. 진짜 PCIe 링크가 없는 가상 장치들이라 dmesg도 전부 `type 00 … conventional PCI endpoint`. "PCIe 토폴로지"를 보려면 그래서 QEMU 실측(§3.2)이 필요했습니다.
- ACPI 시스템이라 sysfs 경로가 `platform/...` 없이 `/sys/devices/pci0000:00/`.

```text
$ sudo journalctl -k | grep -i -E "mcfg|ecam|root bridge|host bridge|root bus"     [실측]
ACPI: MCFG 0x000000036FB9F998 00003C (v01 APPLE  Apple Vz 00000001 AAPL 20180427)
ACPI: MCFG table detected, 1 entries
ACPI: PCI Root Bridge [PCI0] (domain 0000 [bus 00-ff])
acpi PNP0A08:00: _OSC: platform retains control of PCIe features (AE_ERROR)
acpi PNP0A08:00: ECAM area [mem 0x40000000-0x4fffffff] reserved by PNP0C02:00
acpi PNP0A08:00: ECAM at [mem 0x40000000-0x4fffffff] for [bus 00-ff]
PCI host bridge to bus 0000:00
pci_bus 0000:00: root bus resource [mem 0x50000000-0x6ffdffff window]
pci_bus 0000:00: root bus resource [mem 0x380000000-0x77fffffff window]
```

| 줄 | 해석 |
|---|---|
| `ACPI: MCFG … 00003C` | MCFG 테이블 60 B (entry 1개) — §2.7 바이트 해석 |
| `PCI Root Bridge [PCI0] (domain 0000 [bus 00-ff])` | DSDT의 `PNP0A08`(PCIe host bridge) 객체 = `pci_host_bridge` |
| `_OSC: platform retains control …` | `_OSC` = OS와 펌웨어가 AER·hotplug·PME 등의 **소유권을 협상**하는 ACPI 메서드. 여기선 펌웨어가 안 넘겨 줌 → `pci_host_bridge.native_aer` 등 0 [추정: AE_ERROR라 협상 자체 실패] |
| `ECAM at [mem 0x40000000-0x4fffffff]` | MCFG에서 온 base, 256 MiB |
| `root bus resource [mem … window]` | `_CRS`에서 온 MMIO 창 (DT의 `ranges`에 해당) |

### 3.4 토폴로지로 "장치가 안 보이는" 문제 좁히기 (N01 연결)

- 빈 DSP `02:03.0` 실측: `LnkSta … DLActive-`, `SltSta: … PresDet-`, `Bus: secondary=06, subordinate=06` 인데 `lspci -tv`에 `\-03.0-[06]--` 로 아래가 비어 있음. → **장치가 안 보일 때는 `lspci -tv`로 "어느 포트 아래여야 하는지" 찾고, 그 포트(Type 1)의 LnkSta/SltSta를 본다** — [N01 §4](2026-10-31_N01_pcie_lspci_ltssm.html)의 논리 그대로.
- 브리지 버스 범위가 잘못되면(예: subordinate가 secondary보다 작음, 펌웨어 버그) 아래 장치에 config 요청이 라우팅되지 않아 **링크는 멀쩡한데 장치가 안 보임** → dmesg `bridge configuration invalid`, `busn_res` 줄 확인, `pci=assign-busses`(x86)로 재배정 [확인됨: kernel-parameters].

## 4. 꼬리 질문 체인

### 체인 A — "How does Linux see a PCIe device?"

<details><summary>Q. How does Linux see a PCIe device? → 답 보기</summary>

**A.** "Software sees PCIe as a PCI hierarchy: a host bridge for the root complex, and every root port and switch port is a virtual PCI-to-PCI bridge. At boot the host controller driver — for example `pci-host-generic` on an ARM board — gets the ECAM window, then the PCI core scans bus 0 depth-first, reading the vendor ID at every bus/device/function. Every function that answers becomes a `struct pci_dev` on a `struct pci_bus`, gets its BARs sized and assigned, shows up under `/sys/bus/pci/devices`, and is matched to a `pci_driver` by vendor/device or class ID, which then calls `probe`."
- 한국어 핵심: 호스트 드라이버가 ECAM 창 확보 → 깊이 우선 스캔 → 함수마다 `pci_dev` → BAR 배정 → sysfs → id_table 매칭 → probe.

</details>

<details><summary>꼬리1. What's the difference between a Type 0 and Type 1 header? → 답 보기</summary>

**A.** "Both share the first sixteen bytes — IDs, command, status, class, header type. Type 0 is for endpoints: six BARs, subsystem IDs and an expansion ROM. Type 1 is for bridges and ports: only two BARs, and then the primary, secondary and subordinate bus numbers at 0x18 to 0x1A, plus the I/O, memory and prefetchable base/limit windows that tell the bridge which addresses to forward downstream, and the bridge control register with secondary bus reset. The header type byte at 0x0E tells you which one, and bit 7 says multi-function."
- 한국어 핵심: 공통 16 B 이후 Type 0 = BAR 6개, Type 1 = BAR 2개 + 버스 번호 3개 + 주소 window + Bridge Control.

</details>

<details><summary>꼬리2. How are bus numbers assigned when there's a switch? → 답 보기</summary>

**A.** "Depth-first. When the scan finds a bridge on bus N it writes primary N, secondary = the next free number, subordinate = 0xFF temporarily, recurses into the secondary bus, and on the way back trims subordinate to the highest bus actually found below. For a root port with a four-port switch: the root port gets secondary 1, the switch upstream port is device 0 on bus 1 and gets secondary 2 — that's the internal bus — the four downstream ports are devices 0 to 3 on bus 2 and get buses 3, 4, 5, 6, and the subordinates roll back up to 6. I actually ran this in QEMU and the kernel log shows exactly that: 'busn_res [bus 03-ff] end is updated to 03' and so on."
- 한국어 핵심: 임시 ff → 재귀 → 실제 최대값으로 줄임. 4-port switch 예: RP[01-06] → USP[02-06] → DSP 03/04/05/06.

</details>

<details><summary>꼬리3. Where does the kernel get the ECAM base address — on x86 vs on an ARM SoC? → 답 보기</summary>

**A.** "On x86, and on ARM servers that follow SBSA, it's the ACPI MCFG table: each entry gives a base address, a segment number and a start and end bus; the host bridge itself is a PNP0A08 device in the DSDT with its windows in `_CRS`. On an embedded ARM SoC there's usually no ACPI, so it comes from the device tree: the PCIe host node's `reg` gives the config window, `ranges` gives the MMIO and I/O windows, and `bus-range` the buses. For a fully ECAM-compliant controller that's the `pci-host-ecam-generic` binding; many SoC controllers like DesignWare instead have a DBI register space and generate config cycles through an outbound ATU window, so their driver provides custom config accessors."
- 한국어 핵심: x86/서버 = ACPI MCFG (+DSDT `PNP0A08` `_CRS`), 임베디드 = DT `reg`/`ranges`/`bus-range`. DesignWare류는 DBI + iATU로 config 생성.

</details>

<details><summary>꼬리4. Then what's the ECAM address of a given register? → 답 보기</summary>

**A.** "Base plus bus shifted left 20, device left 15, function left 12, plus the register offset — so each bus is 1 MiB, each device 32 KiB, each function 4 KiB, and 256 buses need a 256 MiB window. For example the Link Status of a device at 03:00.0 whose PCIe capability is at 0x80 is base + 0x300000 + 0x92."
- 한국어 핵심: `base + bus<<20 + dev<<15 + fn<<12 + off`, 함수당 4 KiB, 256 버스 = 256 MiB.

</details>

### 체인 B — "What's a PCIe switch from software's point of view?"

<details><summary>Q. What's a PCIe switch from software's point of view? → 답 보기</summary>

**A.** "It's a small tree of PCI-to-PCI bridges. The upstream port is one Type 1 function on the bus below the root port; behind it is an internal virtual bus; and each downstream port is another Type 1 function on that internal bus. Each port has its own PCIe capability with port type 5 for upstream and 6 for downstream, its own link registers, and usually its own AER and hotplug slot. Routing is purely by those bridge registers — bus-number ranges for config and ID-routed packets, memory windows for memory requests."
- 한국어 핵심: switch = USP(Type1, type 5) + 내부 버스 + DSP들(Type1, type 6). 라우팅은 브리지 레지스터(버스 범위, 주소 window)로.

</details>

<details><summary>꼬리1. How many BDFs does a 4-port switch use? → 답 보기</summary>

**A.** "Five functions: one upstream port plus four downstream ports. And it consumes five bus numbers: the internal bus plus one secondary bus per downstream port — on top of the bus for the link between the root port and the switch. So a root port with that switch spans six buses even before you count anything behind the endpoints."
- 한국어 핵심: 함수 5개, 새 버스 5개(내부 1 + DSP 4), RP 링크 버스까지 RP 범위는 6개.

</details>

<details><summary>꼬리2. Why does each downstream port have its own bus? → 답 보기</summary>

**A.** "Because each downstream port is a bridge, and by definition a bridge's secondary side is a new bus. Physically, that bus is the point-to-point link to the device below, so there's exactly one device on it, at device number 0. That gives you per-port isolation: each port has its own address windows, link status, hotplug slot, secondary bus reset and error containment, which is exactly what you want when you validate or debug one link at a time."
- 한국어 핵심: 브리지 secondary = 새 버스 = 점대점 링크 1개, 장치는 device 0. 포트별 격리(window·링크·hotplug·리셋·DPC).

</details>

<details><summary>꼬리3. An empty downstream port — what does lspci show? → 답 보기</summary>

**A.** "The downstream port itself always shows up, because it's inside the switch and its config space is reachable over the upstream link. It still gets a secondary bus number, but nothing appears behind it in `lspci -tv`. Its Link Status shows DLActive minus, and if it's a slot, Slot Status shows Presence Detect minus. In my QEMU run, port 02:03.0 shows exactly that: bus 06 with nothing below, LnkSta 0x0011 with DLActive cleared."
- 한국어 핵심: 포트는 항상 보임, 아래는 비어 있음, `DLActive-`, `PresDet-`.

</details>

### 체인 C (추가) — Root Complex 내부와 접근 경로

<details><summary>Q. What is the root complex, really, on an SoC? → 답 보기</summary>

**A.** "On an SoC it's the PCIe controller IP configured in root-complex mode plus the glue around it: the PHY, an AXI slave port that turns CPU loads and stores in certain address windows into memory or config TLPs, an AXI master port that turns inbound DMA TLPs into memory accesses, address translation units, and the MSI or ITS connection to the interrupt controller. Software sees its root port as bus 0 device 0 or similar, and the outbound windows become the host bridge windows in the device tree `ranges`."
- 한국어 핵심: SoC의 RC = RC 모드 PCIe 컨트롤러 IP + PHY + AXI slave(아웃바운드: CPU → TLP) + AXI master(인바운드 DMA) + ATU + MSI 연결. DT `ranges` = 아웃바운드 창.

</details>

<details><summary>꼬리1. Why is ECAM preferred over CF8/CFC? → 답 보기</summary>

**A.** "CF8/CFC is an x86 I/O-port mechanism: you write an address to 0xCF8, then access data at 0xCFC. It's two steps, so it needs a global lock, it's x86-only, and its register field only covers 256 bytes, so you can't reach extended capabilities like AER or SR-IOV. ECAM is a flat memory window: one load or store per access, any architecture, and the full 4 KiB per function."
- 한국어 핵심: CF8/CFC = x86, 2단계(락), 256 B. ECAM = 메모리 창, 1회 접근, 4 KiB.

</details>

<details><summary>꼬리2. What happens when the CPU reads ECAM for a device that isn't there? → 답 보기</summary>

**A.** "The root complex sends a config read, nobody claims it, and the completion comes back as Unsupported Request — or nothing comes back and it times out. The root complex then returns all ones to the CPU, so the vendor ID reads 0xFFFF, which software treats as 'no device'. Some SoC controllers instead raise an external abort or SError on an unsupported request, which is why their Linux drivers sometimes need special handling or quirks during enumeration."
- 한국어 핵심: 없는 장치 → UR/타임아웃 → RC가 all-ones 반환 → `0xFFFF` = 없음. 일부 SoC는 SError → 드라이버 quirk.

</details>

### 체인 D (추가) — 도메인과 멀티 RC

<details><summary>Q. When would you see a domain other than 0000? → 답 보기</summary>

**A.** "Whenever there's more than one independent host bridge, each with its own 0–255 bus space: multi-socket servers, systems with several MCFG entries, or an SoC with multiple PCIe controllers — automotive SoCs often have three or four. In the device tree each controller is its own node and gets its own domain number, either automatically or from `linux,pci-domain`. lspci shows it with `-D`, and sysfs always includes it, like `0001:01:00.0`."
- 한국어 핵심: 독립 host bridge마다 domain. 멀티 소켓·MCFG entry 여러 개·멀티 컨트롤러 SoC. `lspci -D`, DT `linux,pci-domain`.

</details>

<details><summary>꼬리1. Can two devices have the same bus number then? → 답 보기</summary>

**A.** "Yes — 0000:01:00.0 and 0001:01:00.0 are different devices behind different root complexes. Inside one domain bus numbers are unique. Requester IDs in TLPs don't carry the domain, which is fine because packets never cross between domains through PCIe itself — peer-to-peer across root complexes goes through the SoC fabric, if it's supported at all."
- 한국어 핵심: 도메인이 다르면 같은 BDF 가능. TLP의 Requester ID에는 domain 없음 — 도메인 간 P2P는 SoC 패브릭 문제.

</details>

## 5. Don 경험 연결

- "At Solidigm I worked on the PCIe Gen6 SSD controller from the endpoint side — the config space, BARs and capabilities the host enumerates are things our subsystem IP had to present correctly in pre-silicon FPGA bring-up, so I know how the host's view is built from the device's side. On a validation platform I'd start every board bring-up by checking that the tree in `lspci -tv` matches the schematic — right root port, right bus numbers, nothing missing behind a switch."
- "At SK hynix I automated chamber reliability tests on PCIe Gen3/4/5 SSDs; part of every run was re-enumerating after power cycles and checking each device was back at the same BDF with the expected link speed and width — [숫자 채우기: 사이클 수 / 장비 대수]."
- "At Apple, when a wireless chip over PCIe didn't come up, the first split was topological: is the root port there, is the link up on that port, and only then is it the endpoint — the same structure I'd use here with `lspci -tv` and the port's Link Status."

## 6. 실습 / 코드

### 6.1 QEMU에서 switch 토폴로지 띄우기 [검증됨: 2026-10-31 Lima VM `elinux`, QEMU 10.2.1, 랩 커널 6.18.55]

```bash
# Lima VM 안에서 (rootfs가 RO 상태라 /tmp 사용)
mkdir -p /tmp/LP01 && cd /tmp/LP01 && rm -rf r && mkdir r
cp -a /var/lab/build/rootfs/. r/ && cp /tmp/LP02/pcicfg_walk.static r/pcicfg_walk   # 정적 빌드 도구 (LP02 §6)
cat > r/run.sh <<'EOF'
dmesg | grep -i -E "pci|ecam"
ls -l /sys/bus/pci/devices
echo "=== DUMPS"
for d in /sys/bus/pci/devices/*; do /pcicfg_walk -x ${d##*/}; done     # lspci -x 형식 덤프
echo "=== END"
EOF
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
truncate -s 16M nvme.img
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -nic none \
  -kernel /var/lab/out/Image -initrd i.gz -append "console=ttyAMA0 LAB_CMD=sh~/run.sh" \
  -device pcie-root-port,id=rp1,chassis=1,slot=1,bus=pcie.0,addr=0x1 \
  -device x3130-upstream,id=up1,bus=rp1 \
  -device xio3130-downstream,id=dp0,bus=up1,chassis=2,slot=0,addr=0x0 \
  -device xio3130-downstream,id=dp1,bus=up1,chassis=2,slot=1,addr=0x1 \
  -device xio3130-downstream,id=dp2,bus=up1,chassis=2,slot=2,addr=0x2 \
  -device xio3130-downstream,id=dp3,bus=up1,chassis=2,slot=3,addr=0x3 \
  -drive if=none,id=nv0,file=nvme.img,format=raw -device nvme,drive=nv0,serial=LP01NVME,bus=dp0 \
  -device e1000e,bus=dp1 -device edu,bus=dp2 \
  -device pcie-root-port,id=rp2,chassis=1,slot=2,bus=pcie.0,addr=0x2 -device igb,bus=rp2 > qemu.log 2>&1
# 덤프 부분만 잘라 호스트(Lima)의 진짜 lspci로 해석
tr -d '\r' < qemu.log | sed -n '/^=== DUMPS/,/^=== END/p' | grep -v '^===' > dump.txt
lspci -F dump.txt -tv
sudo lspci -F dump.txt -vvv -s 02:03.0
```

- 포인트: `pcie-root-port`마다 **chassis/slot 쌍이 유일**해야 함 (QEMU `docs/pcie.txt`). `x3130-upstream`은 root port에, `xio3130-downstream`은 upstream port에 `bus=`로 붙임. `-nic none`으로 기본 virtio-net을 빼야 버스 0이 깔끔.
- QEMU root port의 LnkCap이 `16GT/s x32`로 보이는 건 QEMU 기본값(`x-speed`/`x-width` 속성) — 실제 SI가 있는 링크가 아님 [실측: LnkCap 0x00300604].
- 덤프 원본(12개 함수, 각 4 KiB 또는 256 B): [onsitePrep/lp02/qemu_virt_switch_dump.txt](onsitePrep/lp02/qemu_virt_switch_dump.txt) — Lima/Linux 어디서나 `lspci -F`로 재현 가능.

### 6.2 bare-metal 버전 — ECAM으로 버스 스캔 (스케치) [미검증]

```c
/* Depth-first bus scan over ECAM, the same algorithm as pci_scan_bridge_extend(), minus resources.
 * cfg32() must be an aligned 32-bit MMIO read; writes go to the bridge bus-number register at 0x18. */
static unsigned next_bus = 1;

static unsigned scan_bus(uintptr_t ecam, unsigned bus)
{
    unsigned max = bus;
    for (unsigned dev = 0; dev < 32; dev++) {
        for (unsigned fn = 0; fn < 8; fn++) {
            uintptr_t f = ecam + ((uintptr_t)bus << 20 | dev << 15 | fn << 12);
            uint32_t id = mmio_rd32(f + 0x00);
            if ((id & 0xffff) == 0xffff) { if (fn == 0) break; else continue; }
            uint8_t ht = (uint8_t)(mmio_rd32(f + 0x0c) >> 16);
            if ((ht & 0x7f) == 1) {                           /* Type 1: bridge / port */
                unsigned sec = next_bus++;
                mmio_wr32(f + 0x18, 0xff << 16 | sec << 8 | bus);   /* sub=ff (temp), sec, pri */
                unsigned sub = scan_bus(ecam, sec);
                mmio_wr32(f + 0x18, sub << 16 | sec << 8 | bus);    /* trim subordinate */
                if (sub > max) max = sub;
            }
            if (fn == 0 && !(ht & 0x80)) break;              /* single-function device */
        }
    }
    return max;
}
```

- 실제 펌웨어에선 여기에 링크 대기(DL_Active 후 100 ms 규칙, [LP03](2026-10-31_LP03_link_training_ltssm.html)), CRS/RRS 재시도, BAR sizing·window 배정([LP04](2026-10-31_LP04_enumeration_resources_hotplug.html))이 붙음. 0x18 dword 쓰기는 0x1B(sec latency)까지 0으로 덮으므로 실제 코드는 read-modify-write.

## Ethernet/MIPI 비교

| 항목 | PCIe | Ethernet (SoC MAC + PHY) | MIPI CSI-2 |
|---|---|---|---|
| 토폴로지 | RC → (switch) → EP 트리, 점대점 링크 | MAC ↔ PHY(MDIO로 관리) ↔ 케이블 ↔ 상대/스위치 | 센서(TX) → SoC CSI-2 receiver(RX) 점대점 |
| 소프트웨어가 장치를 "찾는" 방법 | **열거**: config space 스캔 (자기 기술 장치) | MAC은 DT/ACPI에 고정, PHY는 MDIO 주소 0–31 스캔해 PHY ID 레지스터 읽기 | 찾지 않음 — DT에 센서·링크(데이터 레인 수 등)를 고정 기술, 센서는 I2C(CCI)로 ID 확인 |
| 주소 | BDF (+domain) | MAC 주소(프레임), MDIO PHY 주소(관리) | 가상 채널(VC) + 데이터 타입 |

## 7. 자주 틀리는 것

1. **"PCIe는 버스다"** — 물리적으로는 점대점 링크. "버스"는 소프트웨어 모델이고, 링크 하나 = 버스 하나 (switch 내부 버스만 예외).
2. **Root Port를 endpoint로 착각** — Root Port는 Type 1 브리지이고 **링크와 무관하게 항상 lspci에 보임**. 장치가 안 보이면 먼저 그 포트를 본다.
3. **4-port switch = BDF 4개라고 답함** — upstream port도 함수 → **5개**, 버스는 내부 버스 포함 5개 추가.
4. **"버스 번호는 고정"** — 열거 순서·hotplug·펌웨어 정책에 따라 바뀜. 스크립트에서 BDF를 하드코딩하지 말고 vendor/device/경로(slot)로 찾기.
5. **subordinate = secondary라고 생각** — subordinate는 "아래 **최대** 버스". switch가 있으면 다름 (RP 00:01.0: sec 01, sub 06).
6. **ECAM base를 하드코딩** — x86/서버는 MCFG, 임베디드는 DT에서 읽는 것. SoC마다 다르고, DesignWare류는 아예 순수 ECAM이 아닐 수 있음.
7. **CF8/CFC로 AER을 찾으려 함** — 256 B까지만 → 확장 capability는 ECAM 필수 ([LP02](2026-10-31_LP02_config_space_capabilities.html)).
8. **`/sys/bus/pci/devices`를 트리로 착각** — 평평한 symlink 목록. 트리는 `readlink -f` 또는 `/sys/devices/...`.
9. **ECAM에서 8/16-bit 접근** — 컨트롤러에 따라 32-bit 정렬 접근만 됨. 읽고 시프트/마스크.
10. **PCIe 링크 아래 device 1–31 스캔** — 점대점이라 device 0만 (ARI면 그 device 0이 함수 256개). Linux도 downstream port 아래는 device 0만 스캔 (`only_one_child()`, 특수 플랫폼은 `PCI_SCAN_ALL_PCIE_DEVS`).

## 8. 출처

1. Linux 6.18 `include/uapi/linux/pci_regs.h` (Lima VM `/var/lab/src/linux-6.18.55`), 업스트림: https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
2. Linux `include/linux/pci-ecam.h`, `include/linux/pci.h`, `drivers/pci/probe.c`, `drivers/pci/pci.c`, `drivers/pci/pcie/portdrv.c`, `arch/x86/pci/direct.c`, `drivers/acpi/pci_mcfg.c`, `drivers/pci/controller/pci-host-generic.c` — https://github.com/torvalds/linux/tree/master/drivers/pci
3. Linux sysfs PCI 문서: https://docs.kernel.org/PCI/sysfs-pci.html
4. Linux PCI 드라이버 작성 가이드: https://docs.kernel.org/PCI/pci.html
5. PCIe port bus driver: https://docs.kernel.org/PCI/pciebus-howto.html
6. DT binding `pci-host-ecam-generic`: https://www.kernel.org/doc/Documentation/devicetree/bindings/pci/host-generic-pci.yaml
7. QEMU PCIe 가이드 (root port chassis/slot, switch): https://github.com/qemu/qemu/blob/master/docs/pcie.txt
8. pciutils `lspci(8)` / `setpci(8)`: https://man7.org/linux/man-pages/man8/lspci.8.html , https://man7.org/linux/man-pages/man8/setpci.8.html
9. OSDev wiki PCI / PCI Express (CF8/CFC, ECAM 개요): https://wiki.osdev.org/PCI , https://wiki.osdev.org/PCI_Express
10. 실측: Lima VM `elinux` (kernel 7.0.0-38-generic, Apple VZ) `lspci`, `journalctl -k`, `/sys/firmware/acpi/tables/MCFG`; QEMU 10.2.1 `-machine virt` + 랩 커널 6.18.55 게스트 로그 (2026-10-31)

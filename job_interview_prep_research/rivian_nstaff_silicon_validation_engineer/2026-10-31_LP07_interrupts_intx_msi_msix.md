# LP07 · 인터럽트: INTx · MSI · MSI-X · GIC ITS
<!-- theme: pastel -->

> **묶음**: D 인터럽트·DMA·에러 · **우선**: P1 · **선행 노트**: [LP02 config space](2026-10-31_LP02_config_space_capabilities.html), [LP06 endpoint 드라이버](2026-10-31_LP06_endpoint_driver_dev.html) · **다음 노트**: [LP08 DMA, IOMMU, 캐시](2026-10-31_LP08_dma_iommu_coherency.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **코드**: `onsitePrep/lp07/msi_cap_decode.c` (macOS ALL PASS), `onsitePrep/lp07/qemu_lab/` (QEMU GICv3 ITS + SMMUv3 실측) · **작성**: 2026-10-31

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "INTx, MSI, MSI-X 차이?", "MSI가 ARM SoC에서 CPU까지 어떻게 가나?", "**장치가 인터럽트를 안 보낸다 / 안 들어온다** — 어떻게 디버그?", "왜 MSI는 DMA 데이터와 순서 문제가 없나?"
- **외울 것 3개**
  1. **MSI = 장치가 하는 '메모리 쓰기(Memory Write TLP)' 한 번**이다. 주소 = 인터럽트 컨트롤러의 doorbell 레지스터, 데이터 = "몇 번 인터럽트인지". 그래서 **Bus Master가 꺼져 있으면 MSI도 못 보낸다.**
  2. **INTx = 진짜 선이 없는 '가상 선'**: 장치가 `Assert_INTx` / `Deassert_INTx` **메시지 TLP**를 보내 "선을 올렸다/내렸다"를 흉내 낸다. level-triggered, 4개(A–D)를 여러 장치가 **공유**, 브리지를 지날 때 **swizzle**.
  3. **ARM(GICv3) 경로**: MSI 쓰기 → **ITS의 `GITS_TRANSLATER`(ITS base + 0x10040)** → (DeviceID = Requester ID를 `msi-map`으로 변환, EventID = MSI data) → **LPI** → 특정 CPU. 컨트롤러 내장 MSI(DWC "iMSI-RX")나 GICv2m(SPI 기반)도 있다.
- 숫자: MSI 최대 **32** vector(2의 거듭제곱, data 하위 비트를 장치가 바꿈), MSI-X 최대 **2048** vector(테이블은 **BAR 안**, entry 16 B, vector별 mask), 동시에 MSI와 MSI-X 중 **하나만** enable.
- Linux: `pci_alloc_irq_vectors(dev, 1, N, PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_INTX)` → `pci_irq_vector(dev, i)` → `request_irq`. (**`PCI_IRQ_LEGACY`는 옛 이름** — `PCI_IRQ_INTX`로 rename, 6.18 헤더엔 아예 없음 [확인됨])
- 관측: `lspci -vv`의 `MSI-X: Enable+ Count=…`, `/proc/interrupts`의 `ITS-PCI-MSIX-<BDF>` 줄, `/sys/bus/pci/devices/<BDF>/msi_irqs/`, `/proc/irq/<n>/effective_affinity_list`.
- **영어 30초 (headline: "MSI vs INTx, and how does an MSI reach the CPU on ARM?")**: "INTx is the legacy model — PCIe has no sideband wires, so the device sends Assert_INTx and Deassert_INTx messages that emulate a level-triggered, shared line, and bridges swizzle INTA–INTD. MSI replaces that with a plain posted memory write: the OS programs an address and a data value into the device's MSI or MSI-X capability, and when the device wants an interrupt it writes that data to that address. On a GICv3 system the address is the ITS translator register; the ITS takes the requester ID as DeviceID and the data as EventID and turns it into an LPI on a target CPU. Because it's a posted write, it can't pass the device's earlier DMA writes, so when the interrupt fires the data is already in memory. MSI-X adds a per-vector table in a BAR — up to 2048 vectors, each with its own address, data and mask — which is what multi-queue NICs and NVMe use."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 왜 인터럽트가 필요한가 — "다 됐어요" 알림

- CPU가 장치(네트워크 카드, SSD, 카메라 컨트롤러)에게 일을 시키면, 장치는 **시간이 걸리는 일**(패킷 수신, DMA 복사)을 한다.
- CPU가 "다 됐니? 다 됐니?" 하고 계속 레지스터를 읽는 방법이 **polling**. 간단하지만 CPU를 낭비한다.
- 그래서 장치가 일이 끝나면 **CPU의 어깨를 두드리는 신호**를 보낸다 = **인터럽트(interrupt)**. CPU는 하던 일을 잠깐 멈추고 **ISR(Interrupt Service Routine: 인터럽트가 오면 실행되는 함수)** 을 실행한다.

### 1.2 비유: 아파트 초인종 vs 우편함 쪽지

- **INTx (옛날 방식) = 복도에 깔린 공용 초인종 선 4개(A, B, C, D)**
  - 여러 집이 같은 선을 공유한다. 벨이 울리면 경비원(CPU)은 **"누가 눌렀지?" 하고 그 선에 연결된 집을 하나씩 다 확인**해야 한다(공유 인터럽트 → 모든 핸들러 호출).
  - 누가 버튼을 **누르고 있는 동안** 벨이 계속 울린다 = **level-triggered(레벨 트리거: 신호가 '켜져 있는 상태'로 인터럽트를 표현)**. 집주인이 처리하고 손을 떼야(장치의 상태 비트를 지워야) 멈춘다.
  - PCIe에는 이 선이 **물리적으로 없다**. 그래서 장치가 **"버튼 눌렀음" / "버튼 뗐음" 쪽지(메시지 TLP)** 를 보내서 선을 흉내 낸다 = **가상 선(virtual wire)**.
- **MSI (Message Signaled Interrupt) = 지정된 우편함에 쪽지 넣기**
  - 관리사무소(OS)가 장치에게 미리 알려 준다: "일 끝나면 **이 주소의 우편함**(인터럽트 컨트롤러 레지스터)에 **이 숫자**(벡터 번호)를 써서 넣어라."
  - 장치는 그냥 **메모리에 4바이트 쓰기**를 한다. 그 주소가 사실은 인터럽트 컨트롤러라서, 쓰기가 도착하면 인터럽트가 된다.
  - 공유 없음, 선 없음, 누가 보냈는지 숫자로 바로 앎.
- **MSI-X = 우편함 주소록 책**
  - MSI는 "주소 1개 + 숫자 묶음(최대 32개)"인데, MSI-X는 **vector마다 주소·숫자·잠금(mask)을 따로 적은 표(table)** 를 장치 메모리(BAR) 안에 둔다. 최대 2048줄.
  - 그래서 "수신 큐 0번 인터럽트는 CPU0, 1번은 CPU1…" 처럼 **큐마다 다른 CPU로 보낼 수 있다**.
- **GIC ITS (ARM) = 우편 분류 센터**
  - ARM SoC에서는 우편함이 **ITS(Interrupt Translation Service)** 라는 분류 센터다. 쪽지에는 "보낸 사람(DeviceID)"과 "내용 번호(EventID)"가 있고, ITS가 표를 보고 "**이건 CPU 3번에게 가는 LPI 8195번**" 으로 바꿔서 전달한다.

### 1.3 그림: 세 방식의 경로

```text
[INTx: 가상 선]                         [MSI / MSI-X: 메모리 쓰기]
 Endpoint                                Endpoint
   | Assert_INTA  (Message TLP)            | MWr addr=doorbell data=N  (posted write TLP)
   v                                       v
 Switch/Bridge  (INTA→INTB… swizzle)     Switch (그냥 메모리 쓰기처럼 라우팅)
   |                                       |
 Root Port → "INTA 선이 올라감"           Root Complex → 메모리 맵에서 doorbell 주소 = GIC ITS
   |                                       |
 interrupt-map → GIC SPI (level)          ITS: (DeviceID=RID, EventID=N) → LPI → CPUk
   |                                       |
 CPU: ISR (공유면 여러 개 호출)            CPU: 그 vector 전용 ISR
   | ISR가 장치 상태 비트 clear             (deassert 필요 없음: edge 같은 1회성)
   v
 Endpoint → Deassert_INTA (선 내려감)
```

### 1.4 그림: MSI 한 번이 CPU에 도착하기까지 (ARM GICv3)

```text
 ① 드라이버 probe: pci_alloc_irq_vectors() → 커널이 ITS에 "이 DeviceID의 EventID 0..k" 등록
 ② 커널이 장치 MSI-X 테이블에 써 둠:   entry[i] = { addr = GITS_TRANSLATER, data = EventID i }
 ③ 장치: DMA로 데이터 다 씀 (MWr, MWr, MWr …)
 ④ 장치: MSI-X entry[i] 내용대로 MWr(addr, data) 1개 전송   ← 앞의 DMA write를 추월 못 함(posted 순서)
 ⑤ Root Complex → (SMMU가 있으면 MSI IOVA 변환) → ITS 레지스터에 도착
 ⑥ ITS: DeviceTable[DeviceID] → ITT[EventID] → LPI INTID(≥8192) + Collection → 대상 Redistributor
 ⑦ CPUk의 GIC CPU interface → IRQ 예외 → Linux generic IRQ → 드라이버 ISR
```

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| IRQ | Interrupt ReQuest | "CPU야 나 좀 봐줘" 신호, Linux에선 번호(virq)로 관리 | `/proc/interrupts` 첫 열 |
| ISR | Interrupt Service Routine | 인터럽트 오면 도는 함수 (`request_irq`로 등록) | 오래 걸리면 시스템 지연 |
| INTx | INTA/INTB/INTC/INTD | PCI 시절 4개 인터럽트 핀. PCIe에서는 메시지로 흉내 | 공유·level → 느리고 까다로움 |
| Assert_INTx / Deassert_INTx | (PCIe Message) | "가상 선 올림 / 내림" 메시지 TLP | 장치가 deassert 안 하면 인터럽트 폭주 |
| level-triggered | 레벨 트리거 | 신호가 켜져 있는 **동안** 계속 인터럽트 | ISR에서 원인 clear 필수 |
| edge-triggered | 엣지 트리거 | 신호가 **바뀌는 순간** 한 번 | MSI는 사실상 이쪽(1회성) |
| swizzle | (핀 회전) | 브리지 아래 장치 번호에 따라 INTA→INTB… 돌리기 | DT `interrupt-map` 계산 근거 |
| `interrupt-map` | DT property | "PCI 주소+핀 → GIC 인터럽트 번호" 변환표 | INTx 라우팅 버그의 원인 1순위 |
| MSI | Message Signaled Interrupt | 장치가 특정 주소에 특정 값을 **메모리 쓰기** → 인터럽트 | 공유 없음, 순서 보장 |
| MSI-X | MSI eXtended | vector별 주소/값/mask 표를 BAR에 둔 MSI | 멀티큐 NIC/NVMe 필수 |
| vector | (인터럽트 벡터) | 장치 하나가 가진 인터럽트 번호 하나 | 큐 1개 ↔ vector 1개 설계 |
| PBA | Pending Bit Array | MSI-X vector가 mask된 동안 "보낼 게 있었음" 표시 비트 | unmask 시 보내짐 |
| doorbell (MSI) | — | MSI 쓰기를 받는 인터럽트 컨트롤러 레지스터 주소 | 주소 틀리면 쓰기가 허공으로 |
| TLP | Transaction Layer Packet | PCIe 위를 다니는 패킷 (MRd, MWr, Cpl, Msg …) | MSI = MWr TLP 1개 |
| posted write | — | 응답(completion)을 기다리지 않는 쓰기 | MSI·DMA write 모두 posted → 순서 규칙 적용 |
| Bus Master | Command reg bit 2 | 장치가 **스스로** 메모리 읽기/쓰기(DMA·MSI) 해도 되는 허가 | 꺼져 있으면 MSI 안 나감 |
| GIC | Generic Interrupt Controller | ARM 표준 인터럽트 컨트롤러 (v2/v3/v4) | SoC 인터럽트의 종착지 |
| SPI (GIC) | Shared Peripheral Interrupt | GIC의 일반 장치 인터럽트 (INTID 32–1019) | INTx·GICv2m MSI가 여기로 |
| LPI | Locality-specific Peripheral Interrupt | GICv3의 메시지 기반 인터럽트 (INTID ≥ 8192) | ITS MSI는 LPI가 됨 |
| ITS | Interrupt Translation Service | (DeviceID, EventID) → LPI + 대상 CPU 변환기 | ARM PCIe MSI 중심 |
| `GITS_TRANSLATER` | ITS Translation Register | MSI가 써지는 ITS 레지스터 (ITS base + 0x10040) [5] | MSI address의 정체 |
| DeviceID / EventID | — | 보낸 장치 ID(보통 RID 기반) / MSI data 값 | `msi-map`이 RID→DeviceID |
| RID / BDF | Requester ID / Bus:Device.Function | TLP에 찍힌 보낸 장치 주소 (16-bit) | ITS·SMMU가 장치를 구분하는 키 |
| `msi-map` | DT property | RID → (MSI 컨트롤러, DeviceID) 변환 | 틀리면 MSI가 엉뚱한 장치로 해석 |
| GICv2m | GICv2 MSI frame | MSI 쓰기를 SPI로 바꾸는 간단한 프레임 (data = SPI 번호) | Lima/Apple VZ VM이 이것 |
| iMSI-RX | (Synopsys DWC) | PCIe 컨트롤러 내장 MSI 수신기 → SPI 1개로 묶어 CPU에 | 많은 SoC가 ITS 없이 이걸 씀 |
| affinity | IRQ affinity | 어떤 CPU가 그 인터럽트를 처리할지 | 성능·지연 튜닝 |
| LAPIC | Local APIC (x86) | x86 CPU별 인터럽트 컨트롤러, MSI 주소 0xFEExxxxx | x86 비교용 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 INTx — 가상 선(virtual wire)

**config 레지스터** (`pci_regs.h` [2]):

| 위치 | 이름 | 의미 |
|---|---|---|
| 0x3D | Interrupt Pin | 0 = INTx 안 씀, 1 = INTA … 4 = INTD (장치가 고정, read-only) |
| 0x3C | Interrupt Line | 펌웨어/OS가 적어 두는 메모(하드웨어 동작에 영향 없음). Linux는 의미 있게 안 씀 |
| Command(0x04) bit 10 | `PCI_COMMAND_INTX_DISABLE` (0x400) | 1이면 장치가 INTx 메시지를 보내지 않음. MSI/MSI-X를 켜면 Linux가 세팅 → lspci `DisINTx+` |
| Status(0x06) bit 3 | `PCI_STATUS_INTERRUPT` (0x08) | 장치가 지금 INTx를 assert 중인지 (lspci `INTx+`) |

**동작**
- PCIe에는 INTA#~INTD# 핀이 없다. 장치는 내부 인터럽트 조건이 생기면 **`Assert_INTA` 메시지 TLP**를 upstream으로 보내고, 조건이 사라지면 **`Deassert_INTA`** 를 보낸다. Switch/Root Port는 아래쪽에서 온 같은 핀의 assert를 **OR**해서 "가상 선"을 유지한다 [확인됨: PCIe spec 개념, 일반 설명].
- **level** 의미: 장치가 deassert 할 때까지 인터럽트가 유지됨 → ISR이 장치 레지스터의 원인 비트를 지우지 않으면 **인터럽트 폭풍(storm)**. Linux는 처리 안 된 인터럽트가 계속 오면 `irq N: nobody cared` 후 그 IRQ를 끈다.
- **공유**: 한 SPI에 여러 장치 → `request_irq(..., IRQF_SHARED, ...)`, 각 ISR이 "내 장치가 보낸 건가?"를 레지스터로 확인 후 `IRQ_NONE`/`IRQ_HANDLED` 반환.

**swizzle (핀 회전)** — 브리지(Root Port/Switch Downstream Port) 아래 장치의 INTx는 브리지 위쪽에서 이렇게 보인다:

```text
 upstream_pin = ((device_pin - 1) + device_number) % 4 + 1      (pin: 1=INTA..4=INTD)
 예) 슬롯(device) 1의 INTA → 위에서는 INTB,  device 2의 INTA → INTC
 ※ PCIe 포트 아래 장치는 거의 항상 device 0 → 브리지 하나 지날 땐 회전 없음
   Linux: pci_swizzle_interrupt_pin(), pci_common_swizzle()
```

**DT `interrupt-map`** — host bridge 노드에 "(PCI 주소의 device 비트, 핀) → GIC 인터럽트" 표를 둔다. **실측 (QEMU virt, 2026-10-31 dumpdtb)** [8]:

```text
pcie@10000000 {
    compatible = "pci-host-ecam-generic";
    interrupt-map-mask = <0x1800 0x00 0x00 0x07>;      ← PCI 주소 hi 셀에서 device 하위 2비트(bit 11–12)만, + 핀 3비트
    interrupt-map = <0x0000 0 0  1  &gic  0 0  0 3 4   ← dev 0, INTA → GIC SPI 3, level-high(4)
                     0x0000 0 0  2  &gic  0 0  0 4 4   ← dev 0, INTB → SPI 4
                     ...
                     0x0800 0 0  1  &gic  0 0  0 4 4   ← dev 1, INTA → SPI 4  (회전!)
                     0x0800 0 0  2  &gic  0 0  0 5 4   ← dev 1, INTB → SPI 5 ...>;
    msi-map   = <0x0 &its   0x0 0x10000>;              ← RID 0x0000~0xffff → ITS, DeviceID = RID
    iommu-map = <0x0 &smmu  0x0 0x10000>;              ← RID → SMMU StreamID = RID   (LP08)
    dma-coherent;                                      ← (LP08)
};
```

- 해석: PCI 주소 hi 셀의 bit 11–15가 device 번호 → `0x0800` = device 1, `0x1800` mask = device mod 4. 즉 위 공식의 swizzle을 **표로 펼쳐 둔 것**. 실제 원문은 `onsitePrep/lp07/qemu_lab/virt_pcie_node.dts`.
- SoC bring-up에서 INTx가 안 오면: ① 이 표의 SPI 번호가 SoC TRM과 맞는지 ② trigger type(level-high) ③ 컨트롤러 드라이버가 INTx를 자체 irq domain으로 처리하는 경우(DWC 등은 `legacy-interrupt-controller` 하위 노드) 그 SPI가 맞는지.

### 2.2 MSI capability (ID 0x05) 레이아웃

**Message Control (cap + 0x02, 16-bit)** — `pci_regs.h` [2]:

| 비트 | 매크로 | 의미 |
|---|---|---|
| 0 | `PCI_MSI_FLAGS_ENABLE` 0x0001 | MSI Enable |
| 3:1 | `PCI_MSI_FLAGS_QMASK` 0x000e | **Multiple Message Capable**: 장치가 원하는 vector 수 = 2^값 (0→1 … 5→32) |
| 6:4 | `PCI_MSI_FLAGS_QSIZE` 0x0070 | **Multiple Message Enable**: OS가 허락한 수 = 2^값 |
| 7 | `PCI_MSI_FLAGS_64BIT` 0x0080 | 64-bit 주소 가능 (주소 레지스터 하나 더 있음) |
| 8 | `PCI_MSI_FLAGS_MASKBIT` 0x0100 | **Per-vector masking** 가능 (Mask/Pending 레지스터 있음) |

**레지스터 배치** (offset은 cap 시작 기준):

```text
            32-bit MSI                     64-bit MSI (+ per-vector mask)
 +0x00  [Next|CapID=05][MsgCtl]       +0x00  [Next|CapID=05][MsgCtl]
 +0x04  Message Address (lo)          +0x04  Message Address lo   (PCI_MSI_ADDRESS_LO)
 +0x08  Message Data (16b)            +0x08  Message Address hi   (PCI_MSI_ADDRESS_HI)
 +0x0C  Mask Bits     (MASK_32)       +0x0C  Message Data (16b)   (PCI_MSI_DATA_64)
 +0x10  Pending Bits  (PENDING_32)    +0x10  Mask Bits            (PCI_MSI_MASK_64)
                                      +0x14  Pending Bits         (PCI_MSI_PENDING_64)
```

- **여러 vector일 때**: 주소는 **하나**. 장치가 Message Data의 **하위 log2(MME) 비트를 vector 번호로 바꿔서** 쓴다 → OS는 data 값을 정렬된(하위 비트 0인) 블록으로 할당해야 함. 그래서 MSI vector 수는 2의 거듭제곱, 최대 32, 그리고 **vector마다 다른 CPU로 보내기 어렵다**(같은 주소 → x86에선 같은 destination).
- Mask Bits: bit i = vector i mask (MASKBIT 지원 시만). 없으면 OS는 "MSI 전체 Enable 끄기" 외엔 막을 방법이 없음.

### 2.3 MSI-X capability (ID 0x11) 레이아웃

```text
 cap +0x00  [Next | CapID=0x11] [Message Control]
            Message Control: bit15 Enable (0x8000) | bit14 Function Mask (0x4000) | bit10:0 Table Size = N-1 (0x07FF)
 cap +0x04  Table Offset/BIR : bit2:0 BIR(BAR index 0–5) | bit31:3 offset (8-byte aligned)
 cap +0x08  PBA   Offset/BIR : bit2:0 BIR               | bit31:3 offset

 BAR[BIR] + Table offset:   (entry i = 16 B, PCI_MSIX_ENTRY_SIZE)
   +0x0  Message Address lo      (PCI_MSIX_ENTRY_LOWER_ADDR)
   +0x4  Message Address hi      (PCI_MSIX_ENTRY_UPPER_ADDR)
   +0x8  Message Data (32-bit)   (PCI_MSIX_ENTRY_DATA)
   +0xC  Vector Control: bit0 Mask (PCI_MSIX_ENTRY_CTRL_MASKBIT), bit31:16 Steering Tag (TPH, 최근 정의)
 BAR[BIR] + PBA offset:  bit i = vector i pending (64-bit 단위 QWORD 배열)
```

- Table Size 필드 11비트 → **최대 2048 vector**. 필드 값은 **N-1** (lspci `Count=`는 +1 한 값).
- **테이블이 BAR(MMIO) 안에 있다** → ① BAR가 할당·enable 안 되면 MSI-X 설정 불가 ② VFIO/가상화에서 테이블 영역은 hypervisor가 가로챔 ③ 장치 FW가 BAR 이 영역을 다른 용도로 쓰면 버그.
- Function Mask(bit 14) = 전체 vector 일괄 mask. vector별 mask가 있어서 **IRQ affinity 변경 중 안전하게 막고 바꿀 수 있음**.

**실측 bytes 해석 (Apple VZ virtio-blk 00:06.0, Lima VM)**:

```text
000080 3c 00 00 00 | 11 00 01 80 | 02 00 00 00 | 22 00 00 00
                     ^^ CapID 0x11 (MSI-X), next 0x00 (마지막)
                           MsgCtl = 0x8001 → Enable+ (bit15), Function Mask-, Table Size field 1 → Count=2
                                   Table = 0x00000002 → BIR 2, offset 0x0
                                                 PBA = 0x00000022 → BIR 2, offset 0x20  (= 2 entries × 16 B 바로 뒤)
```

### 2.4 왜 MSI는 "메모리 쓰기"이고, 그게 왜 좋은가 (순서 보장)

- PCIe ordering 규칙: **posted write(MWr)는 같은 방향의 앞선 posted write를 추월하지 못한다** (Relaxed Ordering 속성이 없는 한). DMA 데이터 쓰기도 MWr, MSI도 MWr → **MSI가 도착했을 때 그 앞의 DMA 데이터는 이미 메모리에 도착해 있다**.
- INTx는 **메시지 경로/사이드밴드**라서 데이터 쓰기보다 먼저 CPU에 닿을 수 있다 → INTx 드라이버는 ISR에서 **장치 레지스터를 한 번 읽어서**(read는 앞선 posted write를 밀어냄 = flush) 데이터 도착을 보장해야 한다. 커널 MSI-HOWTO가 이 점을 MSI의 장점으로 명시 [1].
- 주의 [추정]: 장치가 DMA write에 **Relaxed Ordering(RO)** 비트를 켜면 MSI가 데이터를 추월할 수 있다. 그래서 장치 설계상 MSI 쓰기는 RO를 켜지 않는 것이 규칙(스펙상 MSI는 RO 금지 권고). 검증 항목으로 "RO 켠 상태에서 MSI-데이터 순서" 테스트가 의미 있다.

### 2.5 ARM 경로 ① — GICv3 ITS

```text
 MSI MWr ──► GITS_TRANSLATER (ITS base + 0x10040, include/linux/irqchip/arm-gic-v3.h)
            ├─ DeviceID : 버스가 전달하는 sideband ID. PCIe에선 RID(BDF)를 msi-map(DT) / IORT(ACPI)로 변환
            └─ EventID  : 쓰여진 data (32-bit)
 ITS 테이블 (메모리에 커널이 만들어 둠):
   Device Table[DeviceID] → 그 장치의 ITT(Interrupt Translation Table) 주소
   ITT[EventID]           → (LPI INTID ≥ 8192, Collection ID)
   Collection Table[ICID] → 대상 Redistributor (= 대상 CPU)
 LPI 설정/펜딩은 메모리 테이블(LPI property / pending table)
```

- **실측 (QEMU virt `gic-version=3`, kernel 6.18.55)** dmesg:

```text
ITS [mem 0x08080000-0x0809ffff]
ITS@0x0000000008080000: allocated 8192 Devices @42c50000 (indirect, esz 8, psz 64K, shr 1)
ITS@0x0000000008080000: allocated 8192 Interrupt Collections @42c60000 (flat, esz 8, psz 64K, shr 1)
GICv3: using LPI property table @0x0000000042c70000
GICv3: CPU0: using allocated LPI pending table @0x0000000042c80000
```

  - 줄별: ITS 레지스터 프레임 위치 / Device Table(장치별 ITT 포인터) 메모리 / Collection Table / LPI 설정 테이블 / CPU별 pending 테이블. → "ITS는 메모리 테이블 기반 변환기"를 로그로 보여줄 수 있다.
- `msi-map = <rid-base &its msi-base length>`: RID 0x0100(01:00.0) → DeviceID 0x0100. **멀티 컨트롤러 SoC**에선 컨트롤러마다 `msi-base`를 달리 줘서 DeviceID가 겹치지 않게 한다 — 겹치면 두 장치의 MSI가 같은 ITT로 해석되는 끔찍한 버그.
- **SMMU와 같이 있을 때**: MSI 쓰기도 DMA처럼 SMMU를 통과한다. Linux는 ITS doorbell을 장치 주소공간의 **MSI IOVA 창(`MSI_IOVA_BASE` 0x8000000, 길이 0x100000, arm-smmu-v3.h)** 에 매핑한다. 실측: `/sys/kernel/iommu_groups/3/reserved_regions` → `0x0000000008000000 0x00000000080fffff msi` [확인됨]. 그래서 SMMU 환경에서 MSI-X 테이블의 주소는 ITS의 물리 주소가 아니라 **이 창 안의 IOVA**다 [추정: 이번 랩에선 /dev/mem 제한으로 테이블 직접 못 읽음].

### 2.6 ARM 경로 ② — GICv2m, 컨트롤러 내장 MSI

- **GICv2m**: ITS 없는 단순 MSI 프레임. 장치가 `frame base + 0x40 (V2M_MSI_SETSPI_NS)`에 **SPI 번호를 data로** 쓰면 그 SPI가 울린다 (irq-gic-v2m.c) [5].
  - **실측 (Lima VM, Apple VZ)**: `GICv2m: range[mem 0x1fff0000-0x1fff0fff], SPI[128:255]` 이고, virtio-blk MSI-X 테이블을 `resource2` mmap으로 읽으니 `entry0 addr=0x1fff0040 data=0x8d(141)`, `entry1 addr=0x1fff0040 data=0x8e(142)` → **주소 = 0x1fff0000 + 0x40, data = SPI INTID** 로 정확히 맞는다 [확인됨].
  - 관찰: entry1의 Vector Control이 `0x1`(MASKED)로 읽혔는데 같은 IRQ의 카운트는 증가 중이었다. VZ 에뮬레이션의 읽기값 특성으로 보임 [추정] — 실제 HW에서 활성 vector가 masked면 버그 신호.
- **Synopsys DWC 내장 MSI ("iMSI-RX")**: ITS가 없는 많은 SoC(i.MX, Rockchip, TI 일부 등 [추정: 플랫폼별 상이])에서 PCIe 컨트롤러가 **자기 앞으로 오는 특정 주소의 MWr**을 가로채 내부 상태 레지스터에 기록하고, 묶어서 **SPI 1개**로 CPU에 알린다. Linux `pcie-designware-host.c`: `PCIE_MSI_ADDR_LO 0x820`, `PCIE_MSI_INTR0_ENABLE 0x828`, `MASK 0x82C`, `STATUS 0x830`, 컨트롤러당 32 vector [확인됨: 6.18 소스] [5].
  - 함의: ① 모든 MSI가 SPI 하나로 묶임 → **vector별 CPU affinity 불가** (chained handler) ② doorbell 주소는 커널이 `dmam_alloc_coherent`로 잡은 더미 메모리 주소 — **inbound(DMA) 윈도우 안**이어야 함 ③ 장치가 이 주소로 DMA를 하면 MSI로 오인.

### 2.7 x86 경로 (짧게)

- MSI 주소 `0xFEExxxxx`: bit 19:12 = destination **APIC ID**, bit 3 RH, bit 2 DM. data bit 7:0 = **vector**(IDT 번호), bit 10:8 delivery mode, bit 15 trigger. 쓰기가 LAPIC로 직접 간다(Root Complex가 이 주소 범위를 인터럽트로 해석).
- Intel VT-d **Interrupt Remapping**: 주소/데이터를 "인덱스"로 바꿔 IOMMU 테이블에서 실제 목적지 결정 → ARM ITS와 같은 역할(격리 + 변환).

### 2.8 비교표

| 항목 | INTx | MSI | MSI-X |
|---|---|---|---|
| 전달 방식 | Assert/Deassert **메시지** (가상 선) | **MWr** 1개 | **MWr** 1개 |
| vector 수 | 1 (핀 1개) | 1–32 (2^n) | 1–2048 |
| 공유 | 공유 가능/흔함 | 안 함 | 안 함 |
| trigger | level | (사실상) edge | (사실상) edge |
| 설정 위치 | config 0x3C/0x3D + DT `interrupt-map` | config space cap | cap(config) + **table/PBA(BAR)** |
| vector별 mask | 없음 (INTx Disable 전체) | 옵션(Mask Bits) | 항상 있음 |
| vector별 CPU | 불가 | 사실상 불가(주소 1개) | 가능 |
| DMA 데이터 순서 | 보장 안 됨 → 레지스터 read 필요 | 보장(posted 순서) | 보장 |
| Bus Master 필요 | 아니오 | **예** | **예** |
| 대표 사용 | 레거시, 부트로더, 일부 SoC EP | 소형 장치 | NVMe, 멀티큐 NIC, GPU |

## 3. Linux에서 볼 것

### 3.1 드라이버 API (현재 커널 이름)

```c
/* probe() 안에서 — 6.18 include/linux/pci.h 기준 플래그 이름 */
int nvec = pci_alloc_irq_vectors(pdev, 1, MY_NQUEUES + 1,
                                 PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_INTX);  /* 선호 순서대로 시도 */
if (nvec < 0)
        return nvec;                       /* 하나도 못 받음 */
for (int i = 0; i < nvec; i++) {
        int irq = pci_irq_vector(pdev, i); /* vector i → Linux virq 번호 */
        ret = request_irq(irq, my_isr, 0, "mydev", &q[i]);   /* INTx로 떨어졌으면 IRQF_SHARED 필요 */
}
/* remove(): free_irq(...) 후 pci_free_irq_vectors(pdev); */
```

- 플래그 (6.18 `pci.h` [확인됨] [3]): `PCI_IRQ_INTX` (1<<0), `PCI_IRQ_MSI` (1<<1), `PCI_IRQ_MSIX` (1<<2), `PCI_IRQ_AFFINITY` (1<<3, 큐 vector를 CPU에 자동 분산 = **managed affinity**), `PCI_IRQ_ALL_TYPES`.
- **`PCI_IRQ_LEGACY`**: 2023-11 rename 커밋으로 `PCI_IRQ_INTX`의 별칭이 됐고(머지 2023-12 → v6.8 사이클 [추정]), 이후 제거 패치(2024-03) — 6.18 헤더 grep 결과 없음 [확인됨] [4]. 옛 코드/블로그에서 보이면 같은 뜻.
- `pci_alloc_irq_vectors_affinity()`(+ `struct irq_affinity`)로 "앞 1개는 관리용, 나머지만 분산" 같은 설정. NVMe가 admin queue용으로 이렇게 씀.
- 장치가 MSI로 인터럽트를 보내려면 **`pci_set_master()`** 가 선행돼야 한다(Bus Master Enable).

### 3.2 `lspci -vv` — 실제 출력 (Lima VM, Apple VZ, kernel 7.0.0-38-generic)

```text
00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)
	Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV+ VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
	Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
	Interrupt: pin A routed to IRQ 16
	Region 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]
	Region 2: Memory at 50001540 (32-bit, non-prefetchable) [size=64]
	Capabilities: [84] MSI-X: Enable+ Count=2 Masked-
		Vector table: BAR=2 offset=00000000
		PBA: BAR=2 offset=00000020
	Kernel driver in use: virtio-pci
```

| 줄 | 해석 |
|---|---|
| `BusMaster+` | Command bit 2. **MSI 쓰기도 bus master 동작** → 이게 `-`면 MSI 불가 |
| `DisINTx+` | Command bit 10. MSI-X 켜서 Linux가 INTx를 막음 (정상) |
| `Status: … INTx-` | 지금 INTx assert 안 함 |
| `Interrupt: pin A routed to IRQ 16` | 장치는 INTA 핀을 "가지고" 있고 그 경로의 Linux IRQ가 16. **MSI-X 사용 중이어도 표시됨** (`/sys/.../irq`도 16) — 착각 주의 |
| `Region 2 … [size=64]` | MSI-X 테이블·PBA가 사는 BAR (BIR=2) |
| `MSI-X: Enable+ Count=2 Masked-` | MSI-X 켜짐, vector 2개 (config 필드는 1 = N-1), Function Mask 꺼짐 |
| `Vector table: BAR=2 offset=0` / `PBA: BAR=2 offset=0x20` | 테이블 2×16 B = 0x20 바로 뒤에 PBA |

- QEMU e1000e (실측, `onsitePrep/lp07/qemu_lab/guest_output_2026-10-31.txt`): config 0xd0 `05 e0 80 00` → **MSI cap: 64bit+, Count 1, Enable-** / 0xa0 `11 00 04 80 03 00 00 00 03 20 00 00` → **MSI-X Enable+, Count=5, table BAR3+0, PBA BAR3+0x2000**. 즉 장치가 MSI와 MSI-X를 **둘 다 가지고 있어도 하나만** 켜진다.

### 3.3 `/proc/interrupts` 읽기 — 실제 출력 2종

**(a) Lima VM (Apple VZ, GICv2m, 6 CPU) 발췌** [7]

```text
           CPU0       CPU1       CPU2       CPU3       CPU4       CPU5
 10:    1392780    1453124    1413256    1421932    1414455    1421572    GICv3  27 Level     arch_timer
 28:          0          0          0          0          0          0 GICv2m-PCI-MSIX-0000:00:06.0   0 Edge      virtio2-config
 29:     261989          0          0          0          0          0 GICv2m-PCI-MSIX-0000:00:06.0   1 Edge      virtio2-req.0
 33:       1882          0          0          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 34:       1393          0          0          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   2 Edge      virtio0-output.0
Err:          0
```

| 열 | 의미 |
|---|---|
| `29:` | Linux virq 번호 (`pci_irq_vector()`가 돌려준 값, `/proc/irq/29/`) |
| CPU0..5 카운트 | 그 CPU가 처리한 횟수. 전부 CPU0 → effective affinity가 CPU0 하나 |
| `GICv2m-PCI-MSIX-0000:00:06.0` | irq chip/domain 이름: **어느 MSI 컨트롤러 + MSI-X + 어느 BDF** |
| `1` | 그 domain 안의 hwirq = **MSI-X 테이블 index 1** |
| `Edge` | MSI는 edge 처리 (`arch_timer`는 GIC PPI 27, Level) |
| `virtio2-req.0` | `request_irq`의 name (드라이버가 붙임) |

**(b) QEMU virt GICv3 ITS + e1000e, 2 CPU (실측)** [8]

```text
 15:          0          0    GICv3 106 Edge      arm-smmu-v3-evtq
 19:         24          0 ITS-PCI-MSIX-0000:01:00.0   0 Edge      eth0-rx-0
 20:          0          0 ITS-PCI-MSIX-0000:01:00.0   1 Edge      eth0-tx-0
 21:          3          0 ITS-PCI-MSIX-0000:01:00.0   2 Edge      eth0
 24:          0          5 ITS-PCI-MSIX-0000:00:01.0   0 Edge      PCIe PME, aerdrv, PCIe bwctrl
```

- `ITS-PCI-MSIX-…` = ITS 경로. e1000e는 rx/tx/other 3개 vector 사용(테이블은 5칸).
- `24: … PCIe PME, aerdrv, PCIe bwctrl` = **Root Port의 MSI-X 1개를 PCIe port service 3개(PME, AER, bandwidth control)가 공유** (이건 같은 함수 내부 공유). 이 랩에서 AER 에러 5건을 주입해서 **CPU1 카운트가 0→5** 로 증가 (LP09 §3).
- `arm-smmu-v3-evtq` = SMMU 이벤트 큐 인터럽트 (LP08에서 fault 시 증가).

### 3.4 sysfs / procfs

```bash
ls /sys/bus/pci/devices/0000:00:06.0/msi_irqs/          # → 28 29  (이 장치가 가진 MSI/MSI-X virq들)
cat /sys/bus/pci/devices/0000:00:06.0/msi_irqs/29        # → msix   (msi 또는 msix)
cat /proc/irq/29/smp_affinity                            # → 3f     (허용 CPU 비트마스크: CPU0-5)
cat /proc/irq/29/smp_affinity_list                       # → 0-5
cat /proc/irq/29/effective_affinity_list                 # → 0      (실제로 배달되는 CPU)
echo 2 | sudo tee /proc/irq/29/smp_affinity_list         # CPU2로 옮기기 (managed IRQ면 -EIO 거부)
systemctl is-active irqbalance                           # → inactive (Lima VM 실측)
```

- **smp_affinity vs effective_affinity**: 앞은 "허용 집합", 뒤는 "실제로 고른 CPU". GICv3의 SPI/LPI는 한 번에 **CPU 하나**로만 라우팅 → 허용이 0-5여도 실제는 0.
- **managed affinity**(`PCI_IRQ_AFFINITY`로 받은 큐 IRQ): 커널이 CPU별로 고정, 사용자가 못 바꿈, CPU offline 되면 그 IRQ도 shutdown. NVMe/멀티큐 NIC가 이것.
- **irqbalance**: 사용자 공간 데몬이 주기적으로 `smp_affinity`를 재배치. 지연 측정/검증 때는 **꺼서 재현성 확보**하거나 고정 정책.
- 부트 옵션 `pci=nomsi` [6]: 시스템 전체 MSI 끔(모두 INTx로) — "MSI 문제인가?" A/B 테스트용. 장치 하나만은 드라이버 모듈 파라미터(예: 일부 드라이버 `use_msi=0`) 또는 quirk `PCI_DEV_FLAGS_NO_MSI`. `pcie_ports=…`의 `nomsi`는 PME용만.
- dmesg: `pcieport 0000:00:01.0: PME: Signaling with IRQ 24`, `AER: enabled with IRQ 24` (실측) — port service가 받은 IRQ.

### 3.5 디버그 체인: "인터럽트가 안 들어온다"

```text
 [0] 정말 안 오나?  /proc/interrupts 카운트가 0인가, 아니면 "드라이버가 처리 못 함"인가 (spurious, nobody cared)
 [1] 장치가 보냈나? → 장치 자체 interrupt status/cause 레지스터(BAR) 확인, 프로토콜 애널라이저에서 MWr to doorbell 주소 TLP 존재?
 [2] 보낼 수 있는 상태인가?
      lspci: BusMaster+ ?  MSI-X Enable+ ?  Masked- (Function Mask) ?
      MSI-X 테이블 entry i: Vector Control mask=0 ?  addr/data가 0이 아님?
      PBA bit i = 1 이면 "보내려 했는데 mask라 대기 중"
 [3] 보냈는데 어디서 사라졌나? (ARM)
      주소 = ITS GITS_TRANSLATER (또는 SMMU MSI IOVA 창) 맞나?  컨트롤러 outbound/inbound 창 밖이면 버려짐
      msi-map: 이 BDF의 DeviceID가 커널이 ITS에 등록한 것과 같은가? (컨트롤러가 RID를 바꾸는지 TRM 확인)
      SMMU: dmesg에 그 StreamID의 F_TRANSLATION(MSI IOVA) fault? (LP08)
      내장 MSI(DWC): PCIE_MSI_INTR0_STATUS 비트가 섰는데 SPI가 GIC에서 enable/route 됐나?
 [4] CPU까지 왔나? GIC 쪽: LPI enable(property table), 대상 CPU online, IRQ disabled(spurious로 꺼졌나)
 [5] A/B: pci=nomsi 로 INTx 동작하면 → MSI 경로(주소/ITS/SMMU/BusMaster) 문제로 좁혀짐
```

## 4. 꼬리 질문 체인

### Chain 1 (LP00): "Interrupts aren't arriving from the device."

<details>
<summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 보기)</summary>

- **Q. Interrupts aren't arriving from the device. How do you debug it?**
  - EN: "First I'd split it into 'the device didn't send' versus 'it was sent but lost on the way'. I check /proc/interrupts for the vector, then the device's own interrupt-cause register to see if it thinks it raised something. Then the config side with lspci: Bus Master enabled, MSI-X enabled, function mask off, and the table entry for that vector unmasked with a sane address and data. If all of that looks right, I follow the write: is the address the ITS translator or the MSI window the kernel set up, and does the DeviceID mapping match."
  - 한: "안 보냄 vs 보냈는데 사라짐"으로 반 나누고, 장치 → config → 경로 → GIC 순서.
- **꼬리1. How do you tell whether the device actually sent it?**
  - EN: "The ground truth is a protocol analyzer: an MSI is just a memory write TLP to the doorbell address with the vector's data, so I trigger on writes to that address from the device's requester ID. Without an analyzer, I look at the device's internal status — the cause bit is set — and the MSI-X pending bit array: if the PBA bit is set, the device wanted to send but the vector was masked."
  - 한: 애널라이저에서 MWr(doorbell) 찾기, 없으면 cause 레지스터 + PBA.
- **꼬리2. MSI is enabled but nothing shows up in /proc/interrupts. What are the usual suspects?**
  - EN: "Bus Master Enable off — an MSI is a DMA write, so without it nothing leaves the device. A wrong or untranslated address: on ARM the MSI has to reach the ITS through the controller's inbound path and, with an SMMU, through the reserved MSI IOVA window. A DeviceID mismatch from a wrong msi-map, so the ITS drops or misroutes it. And masking: function mask or the per-vector mask still set."
  - 한: BusMaster, 주소/SMMU MSI 창, msi-map DeviceID, mask.
- **꼬리3. Why would INTx work but MSI not?**
  - EN: "Because they use completely different paths. INTx is a message that the root port turns into a GIC SPI through the interrupt-map, so it doesn't need a doorbell address, an ITS, an IOMMU mapping, or bus mastering. MSI depends on all of those. So 'INTx works, MSI doesn't' points me at the MSI controller setup, msi-map, the SMMU MSI mapping, or the controller's internal MSI block not being enabled — not at the device itself."
  - 한: INTx는 SPI 경로, MSI는 doorbell+ITS+SMMU+BusMaster → 그 쪽이 범인.
- **꼬리4 (추가). And the reverse — MSI works, INTx doesn't?**
  - EN: "Then I'd look at the interrupt-map in the device tree — wrong SPI number or trigger type — and the swizzle if the device is behind a switch, plus whether the controller's legacy interrupt sub-domain is wired. Also some endpoints set the INTx Disable bit or simply don't implement INTx in that mode."
  - 한: `interrupt-map` SPI/trigger, swizzle, 컨트롤러 legacy irq domain.

</details>

### Chain 2: "Compare INTx, MSI and MSI-X."

<details>
<summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 보기)</summary>

- **Q. Compare INTx, MSI and MSI-X.**
  - EN: "INTx emulates four shared, level-triggered pins with Assert and Deassert messages. MSI is a memory write with one address and up to 32 vectors encoded in the low data bits. MSI-X puts a table in a BAR with up to 2048 entries, each with its own address, data and mask bit, plus a pending bit array. Only one of MSI or MSI-X can be enabled at a time."
  - 한: 가상 핀 4개 공유 / 주소 1개 + 32 / BAR 테이블 2048 + vector별 mask.
- **꼬리1. Why do NVMe and multi-queue NICs insist on MSI-X?**
  - EN: "Because they want one interrupt per queue and each queue's interrupt on the CPU that owns that queue. MSI-X gives each vector its own address, so on x86 each can target a different APIC, and on ARM each EventID maps to its own LPI with its own collection. With plain MSI all vectors share one address, so you can't really spread them."
  - 한: 큐당 vector + CPU별 배달 = MSI-X.
- **꼬리2. What's the pending bit array for?**
  - EN: "When a vector is masked and the device has an event, it can't send the write, so it sets the pending bit instead. When software unmasks the vector, the device sends the message. It lets the OS safely mask a vector — for example while changing its affinity — without losing interrupts."
  - 한: mask 중 발생 → PBA에 기록 → unmask 시 전송.
- **꼬리3. Where does the MSI-X table live, and what can go wrong because of that?**
  - EN: "In a memory BAR at the offset given by the capability. So the BAR has to be assigned and memory decoding enabled before MSI-X can be programmed, the device firmware must not reuse that region, and under virtualization the hypervisor traps that range."
  - 한: BAR 안 → BAR 할당/decode 필요, FW가 그 영역 재사용 금지, 가상화 trap.

</details>

### Chain 3: "How does an MSI reach a CPU on an ARM SoC?"

<details>
<summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 보기)</summary>

- **Q. How does an MSI get to a CPU on an ARM SoC?**
  - EN: "On GICv3 the MSI address is the ITS translator register. The write carries the requester ID as a sideband DeviceID — mapped through msi-map in the device tree or IORT in ACPI — and the data is the EventID. The ITS looks up the device's translation table, gets an LPI number and a collection, and the collection points to a redistributor, so the LPI is delivered to that CPU. Simpler SoCs use GICv2m, where the data is just an SPI number, or the PCIe controller's own MSI block that funnels everything into one SPI."
  - 한: GITS_TRANSLATER, DeviceID=RID(msi-map), EventID=data → ITT → LPI → collection → CPU.
- **꼬리1. What happens if msi-map is wrong?**
  - EN: "The ITS sees a DeviceID it has no device-table entry for, or worse, one that belongs to another device. In the first case the interrupt is dropped, and the ITS may log an error; in the second it fires the other device's LPI. It's a classic multi-controller bring-up bug, because each root complex needs a distinct DeviceID range."
  - 한: 없는 DeviceID → drop, 겹치면 남의 인터럽트.
- **꼬리2. How does an IOMMU change the MSI path?**
  - EN: "The MSI is a DMA write, so it's translated by the SMMU like any other. Linux reserves an MSI IOVA window — on arm-smmu-v3 it's at 0x8000000 — and maps the ITS doorbell there, so the address programmed into the device is an IOVA, not the physical ITS address. You can see it in the iommu group's reserved_regions as an 'msi' region."
  - 한: SMMU 통과, MSI IOVA 창(0x8000000) → reserved_regions 'msi'.
- **꼬리3. What's the downside of a controller-internal MSI block like DesignWare's?**
  - EN: "All the vectors are demultiplexed from one SPI by a chained handler, so you can't give each vector its own CPU affinity, and the doorbell is a dummy address that must stay inside the inbound window. It's fine for a single endpoint, but for a multi-queue device the ITS path scales much better."
  - 한: 하나의 SPI로 묶임 → affinity 불가, doorbell 주소 창 제약.

</details>

### Chain 4: "Why doesn't MSI have the DMA-vs-interrupt race?"

<details>
<summary>Q → 꼬리1 → 꼬리2 (답 보기)</summary>

- **Q. A device DMAs data then raises an interrupt. Why is MSI safer than INTx here?**
  - EN: "Both the DMA data and the MSI are posted memory writes on the same path, and PCIe ordering doesn't let a posted write pass an earlier posted write. So when the MSI lands, the data is already in memory. INTx travels as a separate message and can arrive first, so an INTx driver has to read a device register in the handler — the read completion can't pass the earlier writes, which flushes them."
  - 한: posted write 순서 보장 vs INTx는 read로 flush 필요.
- **꼬리1. Can that guarantee break?**
  - EN: "Yes, if the device sets Relaxed Ordering on its data writes, or if the data goes through a different path — another root port, a peer device. And the guarantee is only to the point of coherency; on a non-coherent SoC the CPU still has to invalidate its cache before reading the buffer, which is LP08."
  - 한: Relaxed Ordering, 다른 경로, 그리고 캐시 무효화는 별개.
- **꼬리2. How would you validate that ordering on new silicon?**
  - EN: "A stress test where the device writes an incrementing sequence number at the end of each buffer and then fires the MSI; the ISR checks the sequence and a CRC of the buffer. Run it with and without relaxed ordering, at different payload sizes and under memory traffic, and count any interrupt that arrives before its data."
  - 한: 버퍼 끝 시퀀스 + CRC, RO on/off, 부하.

</details>

### Chain 5 (추가): "Interrupt rate is too high / latency is bad."

<details>
<summary>Q → 꼬리1 → 꼬리2 (답 보기)</summary>

- **Q. A device generates 500k interrupts per second and the CPU is saturated. What do you do?**
  - EN: "First see where they land in /proc/interrupts — if they all hit CPU0, spread them with multiple MSI-X vectors and affinity, or turn on managed affinity. Then reduce the rate: interrupt moderation or coalescing in the device, and NAPI-style polling in the driver, where one interrupt switches to polling until the queue is empty."
  - 한: 분산(MSI-X+affinity) → 줄이기(coalescing, NAPI).
- **꼬리1. Why can't I change smp_affinity for some IRQs?**
  - EN: "Those are managed interrupts allocated with PCI_IRQ_AFFINITY — the kernel pins each queue vector to a CPU set and shuts it down when those CPUs go offline, so writes to smp_affinity are rejected. That's by design for NVMe and multi-queue NICs."
  - 한: managed affinity라서.
- **꼬리2. In a latency test, what interrupt settings do you fix?**
  - EN: "Disable irqbalance, pin the device's vectors and the test thread to known CPUs, record effective_affinity, and fix the device's coalescing settings — otherwise run-to-run variation is mostly interrupt placement."
  - 한: irqbalance off, affinity 고정, coalescing 고정.

</details>

## 5. Don 경험 연결

- "At Solidigm I worked on the PCIe Gen6 SSD controller from the device side, so I know MSI-X from the inside: the table and PBA sit in a BAR the controller firmware owns, and an interrupt is just a posted write that the NVMe completion path issues after the completion entry is written — which is exactly why the host can trust the completion when the interrupt lands. [숫자 채우기: 큐/벡터 수]"
- "During pre-silicon FPGA bring-up of the Cortex-R8/R82 cores, interrupt routing was one of the first things to validate — wrong interrupt mapping or a masked vector looks exactly like 'the device is dead', so I always check the cause register and the mask state before blaming the link."
- "At Apple I root-caused interface issues with a DSO and protocol analyzer; for an interrupt problem the analyzer answers the first question directly — is there a memory write to the doorbell address from this requester ID or not."
- "For validation automation at SK hynix, the same idea applies to counters: I'd log /proc/interrupts deltas per vector alongside the test, so a missing or stuck interrupt shows up as data, not as a hang. [숫자 채우기]"

## 6. 실습 / 코드

### 6.1 `onsitePrep/lp07/msi_cap_decode.c` — MSI/MSI-X capability 디코더 (macOS 컴파일·실행 ✅)

- 입력: **실측 bytes** (QEMU e1000e config, Apple VZ virtio-blk config + MSI-X 테이블) + 합성 MSI 1개(8/32 vector, mask).
- 하는 일: capability list를 0x34부터 걷기(TTL 가드) → MSI Message Control/주소/데이터/마스크, MSI-X Table/PBA BIR+offset, 테이블 entry(주소/데이터/mask) 디코드, GICv2m doorbell 판정.
- 빌드: `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined msi_cap_decode.c -o /tmp/msi && /tmp/msi`
- 실제 출력 (`onsitePrep/lp07/msi_cap_decode.out.txt`):

```text
[e1000e 8086:10d3, QEMU, real bytes]
  MSI   @0xd0: MsgCtl=0x0080 Enable- Count=1/1 64bit+ Maskable-
               Address=0x0000000000000000 Data=0x0000
  MSI-X @0xa0: MsgCtl=0x8004 Enable+ Count=5 Masked-
               Vector table: BAR=3 offset=00000000  (80 B)
               PBA: BAR=3 offset=00002000  (8 B, 1 bit/vector)
[virtio-blk 1af4:1042, Apple VZ, real bytes]
  MSI-X @0x84: MsgCtl=0x8001 Enable+ Count=2 Masked-
               Vector table: BAR=2 offset=00000000  (32 B)
               PBA: BAR=2 offset=00000020  (8 B, 1 bit/vector)
    entry 0: addr=0x1fff0040 data=0x0000008d vctl=0x00000000 unmasked  -> GICv2m doorbell, data = SPI 141
    entry 1: addr=0x1fff0040 data=0x0000008e vctl=0x00000001 MASKED  -> GICv2m doorbell, data = SPI 142
[synthetic MSI, 8 of 32 vectors]
  MSI   @0x50: MsgCtl=0x01bb Enable+ Count=8/32 64bit+ Maskable+
               Address=0x0000000008090040 Data=0x0020 MaskBits=0x000000f0
               vector 0 -> MWr 0x8090040 data 0x0020
               ...
               vector 7 -> MWr 0x8090040 data 0x0027  (masked)
ALL PASS
```

- 연습: 합성 케이스의 MME를 3→2로 바꾸면 vector 몇 개가 되고, data 하위 몇 비트가 바뀌나? (답: 4개, 하위 2비트)

### 6.2 QEMU 랩 (실행 ✅) — GICv3 ITS + SMMUv3 + PCIe root port

- 스크립트: `onsitePrep/lp07/qemu_lab/host.sh` (Lima VM에서 실행, `/tmp/LP07` 사용 — VM rootfs가 emergency read-only라 /var/tmp 대신 tmpfs) + `run.sh`(게스트 안).
- 머신: `-machine virt,gic-version=3,iommu=smmuv3 -device pcie-root-port,id=rp1,chassis=1,slot=1 -device e1000e,bus=rp1 -device pcie-root-port,id=rp2,chassis=2,slot=2 -device edu,bus=rp2`, 커널 `/var/lab/out/Image` (6.18.55) + BusyBox initramfs.
- 얻은 것: §3.3(b)의 `/proc/interrupts`, §2.5 ITS dmesg, §2.1 DT `interrupt-map`/`msi-map`, config 덤프 전부. 원본: `onsitePrep/lp07/qemu_lab/guest_output_2026-10-31.txt`.
- Lima VZ 쪽 원본 캡처: `onsitePrep/lp07/lima_vz_capture_2026-10-31.txt`.
- 다음 확장 [미실행]: edu 장치용 커널 모듈을 써서 `pci_alloc_irq_vectors(…, PCI_IRQ_MSI)` → edu의 "raise interrupt" 레지스터(0x60)로 MSI 발생 → `/proc/interrupts`에 `ITS-PCI-MSI-0000:02:00.0` 줄 확인 (LP06 실습과 합치기).

## 7. 자주 틀리는 것

1. **"MSI는 사이드밴드 신호"** — 아니다. 일반 **Memory Write TLP**다. 그래서 Bus Master·주소 변환·IOMMU 영향을 다 받는다.
2. **`Interrupt: pin A routed to IRQ 16`을 보고 "INTx 쓰는 중"이라고 판단** — MSI-X가 켜져 있어도 이 줄은 나온다. `MSI-X: Enable+`와 `/proc/interrupts`의 chip 이름(`…-PCI-MSIX-…`)으로 판단.
3. **MSI-X `Count=`와 Table Size 필드를 혼동** — 필드는 N-1. 디코더/스크립트 작성 시 +1.
4. **MSI와 MSI-X를 동시에 켤 수 있다** — 불가. 장치가 둘 다 가지고 있어도 하나만.
5. **MSI vector는 아무 개수나** — 2의 거듭제곱(최대 32), data 하위 비트를 장치가 덮어쓰므로 OS가 정렬해서 할당.
6. **"INTx는 edge"** — level이다. ISR에서 원인을 안 지우면 storm → `nobody cared` → IRQ disabled.
7. **`smp_affinity`를 쓰면 무조건 바뀐다** — managed IRQ는 거부, effective_affinity는 1 CPU만.
8. **MSI가 순서를 보장하니 캐시 무효화도 필요 없다** — 순서 보장은 "메모리(PoC)에 도착"까지. non-coherent SoC에서는 여전히 `dma_sync_*_for_cpu` 필요 (LP08).
9. **`PCI_IRQ_LEGACY`** — 최신 커널에선 `PCI_IRQ_INTX`. 면접에서 옛 이름을 써도 되지만 "renamed to INTX"라고 덧붙이면 최신성 어필.
10. **ARM에서 MSI 주소 = ITS 물리 주소** — SMMU가 있으면 MSI IOVA(0x8000000 창)일 수 있다.

## 8. 출처

1. Linux kernel docs — The MSI Driver Guide HOWTO (Documentation/PCI/msi-howto.rst): https://docs.kernel.org/PCI/msi-howto.html (raw: https://raw.githubusercontent.com/torvalds/linux/master/Documentation/PCI/msi-howto.rst)
2. Linux `include/uapi/linux/pci_regs.h` (PCI_MSI_*, PCI_MSIX_*, PCI_COMMAND_INTX_DISABLE): https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/pci_regs.h
3. Linux `include/linux/pci.h` (PCI_IRQ_INTX/MSI/MSIX/AFFINITY): https://raw.githubusercontent.com/torvalds/linux/master/include/linux/pci.h
4. PCI_IRQ_LEGACY → PCI_IRQ_INTX rename series (Damien Le Moal, 2023-11): https://patchwork.ozlabs.org/project/linux-pci/cover/20231122060406.14695-1-dlemoal@kernel.org/ · removal patch (2024-03): https://lkml.iu.edu/hypermail/linux/kernel/2403.3/03880.html
5. Linux 6.18.55 source (Lima VM `/var/lab/src`): `drivers/irqchip/irq-gic-v2m.c` (V2M_MSI_SETSPI_NS 0x040), `include/linux/irqchip/arm-gic-v3.h` (GITS_TRANSLATER 0x10040), `drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.h` (MSI_IOVA_BASE), `drivers/pci/controller/dwc/pcie-designware.h` (PCIE_MSI_*) — mirror: https://elixir.bootlin.com/linux/v6.18/source
6. Linux kernel-parameters (`pci=nomsi`, `pcie_ports=`): https://raw.githubusercontent.com/torvalds/linux/master/Documentation/admin-guide/kernel-parameters.txt
7. 실측: Lima VM `elinux` (Apple VZ, kernel 7.0.0-38-generic) `lspci -vv`, `/proc/interrupts`, MSI-X table mmap — `onsitePrep/lp07/lima_vz_capture_2026-10-31.txt`
8. 실측: QEMU 10.2.1 `virt,gic-version=3,iommu=smmuv3` + kernel 6.18.55 — `onsitePrep/lp07/qemu_lab/guest_output_2026-10-31.txt`, `virt_pcie_node.dts`

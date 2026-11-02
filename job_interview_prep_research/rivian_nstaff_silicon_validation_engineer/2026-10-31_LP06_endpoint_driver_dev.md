# LP06 · Endpoint 드라이버 개발 (probe → BAR → DMA → IRQ)
<!-- theme: pastel -->

> **묶음**: C 리소스/드라이버 · **우선**: P0 · **선행 노트**: [LP05 host controller 통합](2026-10-31_LP05_soc_host_controller_integration.html), [LP04 열거/리소스](2026-10-31_LP04_enumeration_resources_hotplug.html) · **다음 노트**: [LP07 인터럽트 INTx/MSI/MSI-X](2026-10-31_LP07_interrupts_intx_msi_msix.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), 연습 [L1 UIO/MMIO](hmScreenPrep/problems/L1_uio_mmio_poll/index.html), [V3 DMA ring](hmScreenPrep/problems/V3_dma_desc_ring/index.html) · **코드**: `onsitePrep/lp06/` — 커널 드라이버 `edu_lp06.c`, 사용자 공간 `edu_mmap.c`(sysfs resource0), `uio_edu.c`(uio_pci_generic), 랩 스크립트·**실제 로그 3개** · **검증**: Linux 6.18.55 + QEMU 10.2.1 `edu` 장치에서 빌드·insmod·실행, ALL PASS

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"PCIe 드라이버 probe 뼈대를 써 봐라"**, **"왜 `pci_set_master`? 왜 DMA mask? MSI vs INTx? 왜 write 뒤에 read-back?"**, **"커널 드라이버 없이 레지스터를 찌르는 검증 툴은 어떻게 만드나 (resource0 mmap / UIO / VFIO), 위험은?"**, **"드라이버가 어떻게 장치에 붙나 (id table, modalias, bind/unbind, driver_override)"**.
- **외울 것 ① probe 순서 (6줄)**: `pcim_enable_device` → `pcim_iomap_region(pdev, 0, name)` (6.18에서 `pcim_iomap_regions`는 **DEPRECATED**) → `pci_set_master` → `dma_set_mask_and_coherent(DMA_BIT_MASK(장치 능력))` (현행 DMA API 문서: **64-bit mask는 절대 실패 안 함**, "64 시도 후 32 fallback"은 잘못된 관용구로 명시 — 장치가 64-bit 지원하면 64, 아니면 32를 처음부터 고름) → `pci_alloc_irq_vectors(pdev, 1, N, PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_INTX)` + `pci_irq_vector()` → `devm_request_irq` / `pci_request_irq` → 장치 init. remove는 **"장치 조용히 → IRQ → DMA 정지 → 버퍼 해제 → unmap → disable"**, `pcim_*`/`devm_*`이면 대부분 자동.
- **외울 것 ② 하드웨어 비트**: `pcim_enable_device` = COMMAND bit1 **Memory Space Enable** (`enabling device (0000 -> 0002)`), `pci_set_master` = bit2 **Bus Master Enable**(DMA와 MSI 둘 다 장치가 메모리 write를 하는 것이라 필요), MSI 사용 시 커널이 bit10 **INTx Disable** 세팅 → 실측 COMMAND = **0x0406**.
- **외울 것 ③ MMIO**: `writel`은 **posted**(완료 응답 없음) → 순서/도착 보장이 필요하면 **같은 장치의 레지스터를 `readl`로 읽어서 flush**. `readl/writel`은 일반 메모리(DMA 버퍼)와의 순서 barrier 포함, `_relaxed`는 같은 장치 내 순서만.
- **실측 하이라이트**: QEMU virt + edu에서 내 드라이버가 `ID=0x010000ed`, factorial(10)=3628800 (MSI IRQ), DMA 256 B 왕복 PASS. **28-bit DMA mask는 `-5`로 실패**(virt RAM이 0x4000_0000부터라서) → "x86에선 되는데 ARM에선 안 되는" 전형을 직접 재현. INTx 모드는 `GIC-0 37 Level`, MSI 모드는 `GICv2m-PCI-MSI ... Edge`.
- **30초 영어 답 (headline: "Write the skeleton of a PCIe driver probe")**: "I'd declare a `pci_device_id` table with `MODULE_DEVICE_TABLE` so udev can autoload the module. In probe: `pcim_enable_device` to turn on memory decoding, `pcim_iomap_region` to claim and map BAR0, `pci_set_master` because both DMA and MSI are memory writes from the device, then `dma_set_mask_and_coherent` with the width the device really supports — 64 bits never fails, so the old try-64-then-32 pattern isn't needed. Then `pci_alloc_irq_vectors` asking for MSI-X, MSI or INTx, `request_irq` on `pci_irq_vector`, and finally initialize the device and enable its interrupts last. With the managed `pcim_` and `devm_` APIs, the error paths and remove mostly unwind themselves; in remove I only quiesce the hardware — stop interrupts and DMA — before the core frees the rest."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 회사에 새로 온 장비와 전담 매니저

- **PCIe 장치(endpoint)** = 회사에 새로 들어온 **장비**(예: 고속 복사기). 장비에는 **창구**가 있고(레지스터), 창구에 서류를 넣거나(write) 표시판을 읽어서(read) 일을 시킨다.
- **드라이버** = 그 장비 **전담 매니저**(커널 코드). 매니저가 하는 일: 장비 전원 켜기, 창구 위치 확인, 장비가 창고(RAM)에 직접 드나들 수 있게 허락, 장비가 "다 했어요" 벨을 누르면 받기.
- **Linux 드라이버 모델** = **인력 사무소 매칭**.
  - **bus**(PCI 버스) = 사무소. 장비 목록과 매니저 목록을 둘 다 가지고 있다.
  - **device**(`struct pci_dev`) = 열거(enumeration) 때 발견된 장비 한 대. "vendor 0x1234, device 0x11e8" 같은 **명찰**이 있다.
  - **driver**(`struct pci_driver`) = 매니저. "나는 이런 명찰을 가진 장비를 다룰 수 있다"는 **자격증 목록**(`pci_device_id` 테이블)을 낸다.
  - 사무소가 명찰과 자격증이 맞는 쌍을 찾으면 **`probe()`**(첫 출근 인수인계)를 부른다. 장비가 빠지거나 매니저가 그만두면 **`remove()`**.
- **BAR** (Base Address Register: 장치가 "내 레지스터 창구를 이 크기만큼 메모리 주소에 매핑해 달라"고 요청하는 칸) = 창구의 **번지**. 열거 때 커널이 번지를 정해 BAR에 적어 준다 ([LP04](2026-10-31_LP04_enumeration_resources_hotplug.html)). 드라이버는 그 물리 주소를 **`ioremap`** 해서 CPU가 포인터로 접근할 수 있게 만든다.
- **MMIO** (Memory-Mapped I/O: 장치 레지스터를 메모리 주소처럼 읽고 쓰는 방식) = 창구에 서류 넣기. `writel(값, 주소)` / `readl(주소)`.
- **DMA** (Direct Memory Access: CPU를 거치지 않고 장치가 RAM을 직접 읽고 쓰는 것) = 장비가 **창고(RAM)에 직접 가서 물건을 가져오고 갖다 놓는 것**. 매니저는 "창고 몇 번 칸을 써라"(DMA 주소)만 알려 준다. 장비가 갈 수 있는 창고 범위가 **DMA mask**.
- **IRQ** (Interrupt Request: 장치가 CPU에게 "일 끝났어/봐 줘"라고 알리는 신호) = **초인종**. 옛날식은 전용 선(INTx), 요즘은 "특정 주소에 쪽지를 쓰는 것"(MSI/MSI-X).
- **User-space 검증 툴** = 매니저(커널 드라이버) 없이 **직원이 직접 창구를 만지는 것**. 빠르게 레지스터를 찌르기엔 좋지만, 창고 출입(DMA)과 초인종(IRQ)을 안전하게 다루려면 **UIO**나 **VFIO** 같은 출입증 시스템이 필요하다.

### 1.2 그림: 드라이버가 probe에서 하는 일

```text
   열거 완료 (LP04): pci_dev 생성, BAR0 = 0x1000_0000 (1 MiB) 배정, COMMAND = 0x0000
        │
        ▼  bus가 id_table 매칭 (vendor 0x1234, device 0x11e8)
  ┌───────────────────────── edu_probe(pdev, id) ─────────────────────────┐
  │ 1. pcim_enable_device()  → COMMAND.MEM=1  ("enabling device 0000->0002")│
  │ 2. pcim_iomap_region(0)  → BAR0 물리 0x1000_0000 → 커널 가상 0xffff8000_8050_0000 │
  │ 3. pci_set_master()      → COMMAND.BME=1  (장치가 메모리에 write 가능)      │
  │ 4. dma_set_mask_and_coherent(N bits) → "이 장치는 N비트 주소까지 DMA 가능"   │
  │ 5. pci_alloc_irq_vectors(MSI|INTx) + devm_request_irq(handler)           │
  │    → MSI면 COMMAND.INTxDisable=1, MSI capability에 주소/데이터 기록         │
  │ 6. 장치 init: 레지스터 설정, 인터럽트 enable (마지막에!)                     │
  └───────────────────────────────────────────────────────────────────────┘
        │
        ▼ 이제 동작
  CPU ──writel(5, bar0+0x08)──────▶ [edu: factorial 계산]
  CPU ◀──readl(bar0+0x20)──────────  status.COMPUTING 폴링
  edu ──MWr(MSI 주소, 데이터)──────▶ GICv2m/ITS ──▶ CPU IRQ ──▶ edu_isr(): status 읽고 ACK
  edu ──MRd/MWr(DMA 주소)─────────▶ RAM (dma_alloc_coherent로 받은 버퍼)
```

### 1.3 그림: 사용자 공간에서 장치를 만지는 3가지 길

```text
            user space                              kernel                         hardware
 ┌─────────────────────────┐
 │ (a) mmap /sys/bus/pci/   │──mmap──▶ pci_mmap_resource ──────────────▶ BAR0 (MMIO만)
 │     devices/BDF/resource0│          (드라이버 없어도 됨, root 필요)       IRQ/DMA 없음
 ├─────────────────────────┤
 │ (b) /dev/uio0            │──mmap──▶ uio_pci_generic ────────────────▶ BAR0
 │     read() = IRQ 대기     │◀─count── INTx handler (INTx Disable로 mask)   INTx만, DMA 보호 없음
 ├─────────────────────────┤
 │ (c) /dev/vfio/<group>    │──ioctl─▶ vfio-pci + IOMMU ───────────────▶ BAR, MSI/MSI-X(eventfd),
 │     + device fd           │          DMA는 IOMMU가 허락한 범위만             DMA 안전 (DPDK/SPDK/QEMU)
 └─────────────────────────┘
```

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| `struct pci_dev` | PCI device object | 커널이 열거한 장치 하나 (BDF, ID, BAR, irq…) | probe의 인자 |
| `struct pci_driver` | PCI driver object | 이름 + id 테이블 + probe/remove/shutdown + err_handler | 드라이버 등록 단위 |
| `pci_device_id` | PCI device ID entry | vendor/device/subvendor/subdevice/class/class_mask/driver_data | 매칭 조건 |
| `MODULE_DEVICE_TABLE` | module device table | id 테이블을 모듈 메타데이터(alias)로 내보냄 | udev **자동 로드** |
| modalias | module alias | `pci:v00001234d000011E8sv...` 같은 장치 설명 문자열 | uevent → `modprobe` 매칭 |
| probe / remove | — | 바인딩 시 초기화 / 해제 | 이 노트의 뼈대 |
| devres / managed API | device resource management | `devm_*`/`pcim_*`: 드라이버 분리 시 **자동 해제** | 에러 경로 단순화 |
| BAR | Base Address Register | 장치 레지스터/메모리 창의 주소·크기 칸 | MMIO의 시작점 |
| MMIO | Memory-Mapped I/O | 레지스터를 메모리 주소처럼 접근 | `readl/writel` |
| `__iomem` | — | "이 포인터는 MMIO다"라는 sparse 주석 | 직접 역참조 금지, 접근자 사용 |
| posted write | — | 완료 응답(completion) 없는 PCIe 메모리 write | write 직후엔 장치 도착 보장 없음 |
| non-posted | — | 응답을 기다리는 요청 (MRd, config, I/O) | read-back flush의 원리 |
| BME | Bus Master Enable (COMMAND bit 2) | 장치가 요청(DMA, MSI)을 낼 수 있게 허락 | `pci_set_master` |
| DMA mask | — | 장치가 생성할 수 있는 주소 비트 수 | 틀리면 DMA 손상/실패 |
| coherent DMA | consistent DMA | CPU·장치가 동시에 보는 버퍼 (`dma_alloc_coherent`) | 디스크립터 링 |
| streaming DMA | — | 한 번 쓰고 끝나는 버퍼 매핑 (`dma_map_single`) | 데이터 버퍼, sync 필요 |
| SWIOTLB | software I/O TLB | 장치가 못 닿는 메모리를 bounce buffer로 복사 | 성능 저하 원인 |
| INTx | legacy INTA–D | 레벨 트리거 공유 선 인터럽트 (PCIe에선 메시지로 에뮬) | `IRQF_SHARED`, 반드시 원인 확인 |
| MSI | Message Signaled Interrupt | 장치가 정해진 주소에 데이터 write → 인터럽트 (최대 32 vector) | edge, 공유 없음 |
| MSI-X | MSI eXtended | BAR 안 테이블로 vector별 주소/데이터/mask (최대 2048) | 큐마다 vector (NVMe, NIC) |
| UIO | Userspace I/O | 커널 최소 stub + 사용자 공간 드라이버 | 검증 툴, INTx만 |
| VFIO | Virtual Function I/O | IOMMU로 보호된 사용자 공간 장치 접근 | DPDK/SPDK/QEMU passthrough |
| driver_override | — | sysfs로 "이 장치는 이 드라이버만" 강제 | vfio-pci 바인딩 |
| new_id | — | 실행 중 드라이버 id 테이블에 ID 추가 | uio_pci_generic은 이걸로만 바인딩 |
| edu | QEMU educational device | 1234:11e8, factorial/IRQ/DMA 연습용 가상 PCI 장치 | 이 노트 랩 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 드라이버 모델과 매칭

```c
static const struct pci_device_id edu_ids[] = {
	{ PCI_DEVICE(0x1234, 0x11e8) },          /* vendor, device; subvendor/subdevice = PCI_ANY_ID */
	/* { PCI_DEVICE_CLASS(PCI_CLASS_STORAGE_EXPRESS, 0xffffff) },  class 매칭 예 (nvme) */
	{ }                                       /* 끝 표시 (0으로 채운 항목) */
};
MODULE_DEVICE_TABLE(pci, edu_ids);            /* → modinfo의 alias: 줄 */

static struct pci_driver edu_driver = {
	.name = "edu_lp06", .id_table = edu_ids,
	.probe = edu_probe, .remove = edu_remove,
	.dev_groups = edu_groups,                 /* sysfs 속성 자동 생성/삭제 */
	/* .shutdown, .err_handler (AER 복구: LP09), .sriov_configure (LP12) */
};
module_pci_driver(edu_driver);                /* module_init/exit + pci_register_driver 축약 */
```

- **매칭 순서** (`drivers/pci/pci-driver.c` `pci_match_device`, 6.18 확인): ① `driver_override`가 설정돼 있으면 **그 이름의 드라이버만** ② `new_id`로 추가된 **dynamic id** 목록 ③ 정적 `id_table`. (driver_override인데 dynid/테이블이 없으면 더미 id로 매칭)
- **자동 로드 경로**: 열거 → uevent `MODALIAS=pci:v00001234d000011E8sv00001AF4sd00001100bc00scFFi00` (실측, 장치 sysfs `modalias`) → udev가 `modprobe <modalias>` → `modules.alias`에서 `pci:v00001234d000011E8sv*sd*bc*sc*i*` (실측 `modinfo edu_lp06.ko`) 와이일드카드 매칭 → 모듈 로드 → bus가 probe.
- modalias 읽기: `v`=vendor, `d`=device, `sv`/`sd`=subsystem vendor/device(QEMU는 1AF4:1100), `bc`=base class(00), `sc`=subclass(FF), `i`=prog-if(00).

### 2.2 probe 단계별: API → 하드웨어에서 일어나는 일 → 실패하면

| # | API (6.18) | 하드웨어/커널에서 일어나는 일 | 실패/함정 |
|---|---|---|---|
| 1 | `pcim_enable_device(pdev)` | D0로 깨움, 배정된 BAR 종류에 맞춰 COMMAND **IO(bit0)/MEM(bit1)** 세팅, 브리지도 enable, **managed 표시**(`is_managed`) | BAR 미배정이면 enable은 되지만 다음 단계 실패 (LP05 §6.1 실측) |
| 2 | `pcim_iomap_region(pdev, bar, name)` | `pci_request_region`(/proc/iomem에 이름 등록, 중복 claim 방지) + `pci_iomap`(ioremap) → `void __iomem *` 또는 `IOMEM_ERR_PTR` | 6.18: `pcim_iomap_regions()`·`pcim_iomap_table()`은 **DEPRECATED** (devres.c 주석 "Do not use it in new code") — 반환값은 `IS_ERR()`로 검사 |
| 3 | `pci_set_master(pdev)` | COMMAND **BME(bit2)** = 1 | 빼먹으면 DMA·MSI가 **조용히** 안 됨 (장치는 요청을 못 냄) |
| 4 | `dma_set_mask_and_coherent(dev, DMA_BIT_MASK(n))` — n = 장치가 실제로 내는 주소 비트 수 | DMA API에 장치 주소 능력 등록. direct mapping에선 **32비트 이상은 항상 성공**(`dma_direct_supported`: mask ≥ 32-bit이면 1), 그 미만은 RAM 위치와 비교. DMA API HOWTO: 64-bit은 never fail, "64 → 32 fallback"은 wrong 패턴(옛 드라이버엔 많음), 32 미만 mask만 반환값 검사 | 실측: 28-bit → **-5 (-EIO)** (virt RAM 0x4000_0000~). 잘못 크게 잡으면 장치가 주소 상위 비트를 잘라 엉뚱한 RAM을 씀 |
| 5 | `pci_alloc_irq_vectors(pdev, min, max, PCI_IRQ_MSIX \| PCI_IRQ_MSI \| PCI_IRQ_INTX)` | MSI-X → MSI → INTx 순으로 시도, 할당된 vector 수 반환. MSI/MSI-X면 capability에 주소/데이터 기록 + **INTx Disable** | 옛 이름 `PCI_IRQ_LEGACY`는 6.18 `pci.h`에 **없음** → **`PCI_IRQ_INTX`** (옛 예제 코드 복붙 시 컴파일 에러). managed 장치면 해제도 자동(`pcim_setup_msi_release`) |
| 6 | `pci_irq_vector(pdev, i)` + `devm_request_irq(dev, irq, isr, flags, name, data)` 또는 `pci_request_irq(pdev, i, isr, thread_fn, data, fmt)` | 핸들러 등록. INTx면 `IRQF_SHARED` | `pci_request_irq`는 **managed 아님** → `pci_free_irq` 필요. 핸들러 등록 **전에** 장치 인터럽트를 켜면 spurious IRQ |
| 7 | 장치 init | 레지스터 설정, 링/큐 생성, **인터럽트 enable은 마지막** | |
| 8 | `pci_set_drvdata(pdev, priv)` | remove/sysfs에서 private 데이터 찾기 | 핸들러가 drvdata를 쓰면 request_irq 전에 세팅 |

- docs.kernel.org PCI 가이드가 권하는 순서도 같다: enable → 리소스 요청 → DMA mask → 공유 데이터 할당 → (config 접근) → IRQ 등록 → 장치 init → DMA 엔진 enable.

### 2.3 remove / shutdown / 에러 경로

- 권장 remove 순서 (docs.kernel.org/PCI/pci.html §1.5): **① 장치가 IRQ를 못 내게 → ② free_irq → ③ DMA 정지 → ④ DMA 버퍼 해제(streaming 먼저, coherent 나중) → ⑤ 다른 서브시스템에서 unregister → ⑥ iounmap, pci_disable_device → ⑦ release_region**.
- managed API를 쓰면 devres가 **등록 역순**으로 풀어 준다: `devm_request_irq` → `pci_alloc_irq_vectors` → `pcim_iomap_region` → `pcim_enable_device`. 그래서 내 `edu_remove()`는 **하드웨어 조용히(STATUS IRQ enable 끄기)** 만 한다. 단, **DMA 정지는 자동이 아님** — 장치가 계속 DMA 중이면 해제된 메모리에 쓴다 (가장 위험한 버그).
- `pci_disable_device()`는 **BME만 끈다** (`do_pci_disable_device`: COMMAND_MASTER만 clear, 6.18 확인) — Memory Space Enable은 남음. 실측: 두 번째 insmod에서 `enabling device` 메시지가 다시 안 나옴(MEM 비트가 이미 1).
- `.shutdown`: reboot/kexec 시 호출 — **DMA 정지 필수**(kexec 후 새 커널 메모리를 장치가 덮어쓰는 버그).
- surprise removal(케이블 뽑힘, 링크 다운): MMIO read가 **0xFFFFFFFF** → 드라이버는 `readl() == ~0`을 "장치 사라짐" 신호로 처리하고 `pci_device_is_present()`로 확인 ([LP09](2026-10-31_LP09_aer_dpc_error_handling.html)).

### 2.4 MMIO 접근자와 posted write

| 접근자 | 폭 | 순서 보장 | 언제 |
|---|---|---|---|
| `readl` / `writel` | 32 | little-endian 변환 + **일반 메모리와의 barrier**: arm64 `writel` = `__io_bw()`(= `dma_wmb()`) 후 store, `readl` = load 후 `__io_ar()` | 기본값. "DMA 버퍼 채우고 → doorbell write" 순서 보장 |
| `readl_relaxed` / `writel_relaxed` | 32 | **같은 장치 접근끼리**만 순서, 일반 메모리와는 barrier 없음 | 핫패스에서 DMA와 무관한 레지스터 연속 접근 |
| `readb/w/q`, `writeb/w/q` | 8/16/64 | 동일 | `writeq`는 64-bit 단일 접근. 32-bit 플랫폼에선 `lo_hi_writeq`(두 번 쪼갬) |
| `ioread32` / `iowrite32` | 32 | I/O BAR와 MEM BAR 모두 처리 (`pci_iomap` 결과) | BAR 종류 모를 때 |
| `readl_poll_timeout(addr, val, cond, delay_us, timeout_us)` | 32 | `<linux/iopoll.h>` | 상태 비트 폴링 + 타임아웃 (edu factorial) |
| `memcpy_toio` / `memcpy_fromio` | 가변 | | BAR 안 버퍼 복사 |

- **posted write 문제**: `writel(RESET, bar+CTRL); udelay(10);` → write가 아직 컨트롤러/스위치 버퍼에 있으면 10 µs가 장치 기준으로는 더 짧을 수 있음. 해결: **`writel(...); (void)readl(bar + CTRL);`** — read는 non-posted라 completion이 와야 끝나고, PCIe ordering 규칙상 **앞선 posted write를 추월할 수 없다** → read가 돌아왔다 = write가 장치에 도착했다.
- 단, read-back은 **같은 장치**의 레지스터여야 의미가 있다. 리셋 중이라 MMIO가 응답 못 하는 상황이면 docs 권고대로 **config read**로 flush (MMIO read가 hard-fail하는 플랫폼 있음).
- **edu 특유 함정 (실측으로 확인한 소스 사실)**: edu는 `0x80` 이상 레지스터를 **4 또는 8바이트 단위**로만 받고, switch가 `case 0x80`, `0x88`… 만 디코드 → `0x84`에 상위 32비트를 따로 쓰면 무시됨. 그래서 내 드라이버는 DMA 주소를 `writeq`로 한 번에 쓴다. 실제 장치 데이터시트에서도 "64-bit 레지스터를 lo/hi 어느 순서로, 어느 쪽이 latch 트리거인지"를 꼭 확인할 것.
- MMIO 트레이싱: 6.18에는 `rwmmio` tracepoint(`CONFIG_TRACE_MMIO_ACCESS`)가 `readl/writel` 안에 들어 있음 → 드라이버의 레지스터 접근 순서를 ftrace로 볼 수 있음 [LP13].

### 2.5 DMA 기초 (깊은 내용은 [LP08](2026-10-31_LP08_dma_iommu_coherency.html))

- **DMA 주소 ≠ CPU 물리 주소 ≠ 커널 가상 주소**. 장치에게는 반드시 DMA API가 준 `dma_addr_t`를 줌. 실측: `cpu=ffff000003ebb000 dma=0x0000000043ebb000` — IOMMU 없는 virt라 DMA 주소 = 물리 주소(0x43eb_b000), CPU는 linear map 가상 주소.
- **coherent**: `dma_alloc_coherent(dev, size, &handle, GFP_KERNEL)` — 디스크립터 링, 상태 블록. non-coherent SoC에선 uncached로 매핑.
- **streaming**: `dma_map_single(dev, buf, len, DMA_TO_DEVICE)` → 장치에 주소 전달 → 완료 후 `dma_unmap_single` (또는 `dma_sync_single_for_cpu/device`). **반드시 `dma_mapping_error()` 확인**.
- 28-bit edu 실험이 보여 주는 것: 장치 mask(28)보다 RAM(0x4000_0000=30비트 이상)이 높고 IOMMU도 없으면 `dma_set_mask`가 거부 → SWIOTLB도 같은 RAM이라 못 도움. **해결은 IOMMU(SMMU)로 장치 주소 공간을 낮은 주소에 매핑하거나, 장치가 더 넓은 주소를 지원하게 하는 것**.

### 2.6 인터럽트 기초 (깊은 내용은 [LP07](2026-10-31_LP07_interrupts_intx_msi_msix.html))

| | INTx | MSI | MSI-X |
|---|---|---|---|
| 신호 방식 | Assert_INTx/Deassert_INTx 메시지 (레벨 에뮬) | 장치가 MSI 주소에 데이터 **MWr** | BAR 안 테이블의 entry별 MWr |
| vector 수 | 핀 1개 (공유) | 1–32 (2의 거듭제곱, 연속) | 최대 2048, 각자 주소/데이터/mask |
| 트리거 | **Level** (원인 제거 전까지 계속) | Edge | Edge |
| 공유 | `IRQF_SHARED`, 핸들러가 "내 것 아님 → `IRQ_NONE`" | 공유 없음 | 공유 없음 |
| 핸들러 필수 동작 | **장치 쪽 원인 clear** (안 하면 interrupt storm) | 장치 status ACK (edu는 MSI여도 ACK 필요) | 동일 |
| ARM 경로 | `interrupt-map` → GIC SPI (실측 `GIC-0 37 Level`) | `msi-map` → GICv2m/ITS (실측 `GICv2m-PCI-MSI-0000:00:02.0 0 Edge`) | ITS |
| 필요 비트 | COMMAND bit10 = 0 | BME=1, MSI cap Enable, INTx Disable=1 | BME=1, MSI-X Enable, Function Mask=0 |

### 2.7 sysfs에서 바인딩 조작

| 경로 | 쓰기 값 | 효과 |
|---|---|---|
| `/sys/bus/pci/drivers/<drv>/unbind` | BDF | 드라이버 분리 (remove 호출) |
| `/sys/bus/pci/drivers/<drv>/bind` | BDF | 지정 드라이버에 붙이기 (id 매칭 필요) |
| `/sys/bus/pci/drivers/<drv>/new_id` | `"vvvv dddd [svvv sddd class mask]"` | dynamic id 추가 + 맞는 미바인딩 장치 즉시 probe |
| `/sys/bus/pci/drivers/<drv>/remove_id` | 같은 형식 | dynamic id 제거 |
| `/sys/bus/pci/devices/<BDF>/driver_override` | 드라이버 이름 (빈 문자열 = 해제) | 그 이름의 드라이버만 붙게 강제 |
| `/sys/bus/pci/drivers_probe` | BDF | 지금 규칙으로 다시 매칭 시도 |
| `/sys/bus/pci/devices/<BDF>/remove` / `/sys/bus/pci/rescan` | 1 | 장치 삭제 / 재열거 |
| `/sys/bus/pci/devices/<BDF>/reset` | 1 | FLR 등 함수 리셋 ([LP11](2026-10-31_LP11_reset_types.html)) |
| `/sys/bus/pci/devices/<BDF>/resource0` | (mmap) | BAR0 직접 매핑 |
| `/sys/bus/pci/devices/<BDF>/config` | (read/pwrite) | config space (비root는 앞 64 B만 읽힘) |
| `/sys/bus/pci/devices/<BDF>/enable` | 1/0 | `pci_enable_device` 유사 (resource0 mmap 전 MEM 켜기) |

### 2.8 User-space 검증 툴: resource0 vs UIO vs VFIO

| | sysfs `resource0` mmap | UIO `uio_pci_generic` | VFIO `vfio-pci` |
|---|---|---|---|
| 커널 드라이버 | 없어도 됨 (있어도 mmap 가능 — 위험) | stub이 바인딩 (`new_id`로만, id_table 없음) | vfio-pci 바인딩 (`driver_override`) |
| MMIO | ✅ | ✅ (`/dev/uioN` mmap, map N = offset N×page) | ✅ (region info + mmap) |
| IRQ | ❌ | 🟡 **INTx만**: 커널이 INTx Disable로 mask, `read()`가 카운트 반환, 사용자가 COMMAND bit10 clear로 unmask | ✅ INTx/MSI/MSI-X → **eventfd** |
| DMA | ❌ (해도 보호 없음) | ❌ 보호 없음 — 커널 주석: "insecure when used with devices doing DMA"; fd close 시 `pci_clear_master` | ✅ `VFIO_IOMMU_MAP_DMA`로 **IOMMU가 허락한 범위만** |
| 필요 조건 | root(CAP_SYS_ADMIN), lockdown 해제 | `CONFIG_UIO_PCI_GENERIC`, INTx mask 지원 장치(`pci_intx_mask_supported`) | **IOMMU + IOMMU group** (또는 unsafe noiommu 모드) |
| 쓰임 | 빠른 레지스터 peek/poke, bring-up 스크립트 | 간단한 사용자 공간 드라이버 | DPDK/SPDK, QEMU passthrough, 프로덕션급 사용자 공간 |
| 위험 | 커널 드라이버와 동시 접근 → 상태 꼬임, 읽기 부작용 레지스터(read-to-clear) | DMA 잘못 쓰면 커널 메모리 덮어쓰기 | IOMMU group 단위 할당 (ACS, [LP12](2026-10-31_LP12_sriov_vfio_acs_p2p.html)) |

## 3. Linux에서 볼 것

### 3.1 명령 모음

| 명령 | 보는 것 |
|---|---|
| `lspci -nnk -s 01:00.0` | ID, **`Kernel driver in use:`**, **`Kernel modules:`**(alias로 후보 모듈) |
| `lspci -vvv -s 01:00.0` | BAR(`Region 0: Memory at ...`), `Control: ... BusMaster+ ... DisINTx+`, `Capabilities: [40] MSI: Enable+ Count=1/1 64bit+ Address=... Data=...` |
| `cat /sys/bus/pci/devices/<BDF>/{vendor,device,class,irq,modalias,resource}` | lspci 없는 BusyBox 환경 대체 |
| `od -A x -t x1 -N 64 /sys/bus/pci/devices/<BDF>/config` | config header 원본 |
| `cat /proc/iomem` | BAR가 claim 됐나 (드라이버 이름이 붙음) |
| `cat /proc/interrupts` | vector별 카운트, 타입(Level/Edge), 컨트롤러(GIC/GICv2m/ITS) |
| `ls -l /sys/bus/pci/devices/<BDF>/driver` | 바인딩된 드라이버 |
| `ls /sys/bus/pci/devices/<BDF>/msi_irqs/` | MSI/MSI-X vector 번호 |
| `modinfo x.ko` | alias, parm, vermagic |
| `dmesg -w` + `echo 'module edu_lp06 +p' > /sys/kernel/debug/dynamic_debug/control` | `dev_dbg` 켜기 |
| `udevadm monitor --kernel --property` | uevent의 MODALIAS 확인 (systemd 환경) |

### 3.2 [실측] edu config header (`onsitePrep/lp06/run_guest_lab.log` [4])

```text
pci:v00001234d000011E8sv00001AF4sd00001100bc00scFFi00            ← modalias
0x0000000010000000 0x00000000100fffff 0x0000000000040200          ← resource 1행 = BAR0 start end flags
000000 34 12 e8 11 00 00 10 00 10 00 ff 00 00 00 00 00
000010 00 00 00 10 00 00 00 00 00 00 00 00 00 00 00 00
000020 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 00 11
000030 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00
```

| offset | 바이트 | 해석 (`pci_regs.h` 기준) |
|---|---|---|
| 0x00 | `34 12` `e8 11` | Vendor 0x1234, Device 0x11e8 (little-endian) |
| 0x04 | `00 00` | COMMAND = 0 (아직 드라이버 없음 → MEM/BME 꺼짐) |
| 0x06 | `10 00` | STATUS bit4 = **Capabilities List** 있음 |
| 0x08 | `10` `00 ff 00` | Revision 0x10, Class = 0x00ff00 (prog-if 00, subclass ff, base 00 = "unclassified/other") |
| 0x10 | `00 00 00 10` | **BAR0 = 0x1000_0000**, 하위 비트 0 = 32-bit, non-prefetchable 메모리 BAR |
| 0x2C | `f4 1a 00 11` | Subsystem Vendor 0x1af4, Subsystem 0x1100 |
| 0x34 | `40` | Capability pointer → 0x40 (edu.c는 `msi_init`만 호출하므로 MSI capability) |
| 0x3C | `00` `01` | Interrupt Line 0 (ARM에선 의미 없음), **Interrupt Pin 1 = INTA** |
| resource flags `0x40200` | | `IORESOURCE_MEM`(0x200) + `IORESOURCE_SIZEALIGN`(0x40000) |

### 3.3 [실측] 드라이버 probe dmesg (dma_bits=40, MSI)

```text
[    0.597335] edu_lp06 0000:00:02.0: BAR0 [mem 0x10000000-0x100fffff] mapped at ffff800080500000
[    0.597453] edu_lp06 0000:00:02.0: DMA mask 40 bits OK
[    0.597688] edu_lp06 0000:00:02.0: irq 23 via MSI
[    0.597709] edu_lp06 0000:00:02.0: ID reg = 0x010000ed (major 1 minor 0)
[    0.597728] edu_lp06 0000:00:02.0: liveness: wrote 0x12345678 read 0xedcba987 (OK)
[    0.597774] edu_lp06 0000:00:02.0: factorial(5) by polling = 120
[    0.597883] edu_lp06 0000:00:02.0: factorial(10) irq: IRQ received, irq_status=0x00000001 (expect 0x00000001) count=1
[    0.597899] edu_lp06 0000:00:02.0: factorial(10) = 3628800
[    0.597923] edu_lp06 0000:00:02.0: raise irq: IRQ received, irq_status=0x00ab0000 (expect 0x00ab0000) count=2
[    0.598110] edu_lp06 0000:00:02.0: dma: coherent buf cpu=ffff000003ebb000 dma=0x0000000043ebb000
[    0.702412] edu_lp06 0000:00:02.0: dma ram->edu: IRQ received, irq_status=0x00000100 (expect 0x00000100) count=3
[    0.804763] edu_lp06 0000:00:02.0: dma edu->ram: IRQ received, irq_status=0x00000100 (expect 0x00000100) count=4
[    0.805069] edu_lp06 0000:00:02.0: dma: round trip 256 bytes OK (dst[0..3]=03 0a 11 18)
[    0.805865] edu_lp06 0000:00:02.0: dma test: PASS
```

| 줄 | 해석 |
|---|---|
| `BAR0 [mem 0x10000000-0x100fffff] mapped at ffff8000...` | 물리 BAR(LP05 `ranges` 32-bit 창 맨 앞) → vmalloc 영역의 ioremap 가상 주소 |
| `DMA mask 40 bits OK` | QEMU `-device edu,dma_mask=0xffffffffff`와 짝 |
| `irq 23 via MSI` | Linux virq 23, GICv2m 경유 (`/proc/interrupts` 참고) |
| `ID reg = 0x010000ed` | edu.rst 형식 `0xRRrr00ed`: major 1, minor 0 |
| `liveness ... 0xedcba987` | `~0x12345678` — MMIO write/read 경로 정상 |
| `factorial(5) by polling = 120` | write 0x08 → STATUS.COMPUTING 폴링 → read |
| `factorial(10) irq ... irq_status=0x1` | STATUS bit7(IRQFACT) 켜고 계산 → 완료 IRQ 값 0x1 (`FACT_IRQ`) |
| `raise irq ... 0x00ab0000` | 0x60에 쓴 값이 IRQ status로 OR → 소프트웨어 트리거 IRQ |
| `dma=0x43ebb000` | IOMMU 없음 → DMA 주소 = 물리 주소. 28비트(0x0fff_ffff) 초과 |
| `ram->edu` 0.702 / `edu->ram` 0.804 | edu DMA 타이머가 **100 ms(가상 시간)** 후 수행 (edu.c `timer_mod(... + 100)`) → 두 전송 간격 ≈ 100 ms |
| `round trip 256 bytes OK (03 0a 11 18)` | 패턴 `i*7+3` (3, 10, 17, 24) 그대로 돌아옴 |

### 3.4 [실측] 28-bit DMA mask 실패 (진짜 edu 기본값)

```text
[    0.560685] edu_lp06 0000:00:02.0: enabling device (0000 -> 0002)                ← COMMAND.MEM 세팅 (pcim_enable_device)
[    0.560870] edu_lp06 0000:00:02.0: BAR0 [mem 0x10000000-0x100fffff] mapped at ffff800080500000
[    0.560979] edu_lp06 0000:00:02.0: dma_set_mask_and_coherent(28 bits) failed: -5
[    0.561924] edu_lp06 0000:00:02.0: dma test skipped (no usable DMA mask)
```

- `-5` = `-EIO`. `kernel/dma/direct.c` `dma_direct_supported()`: mask < 32-bit이면 `mask >= (가장 낮은 DMA 가능 주소 한계)`를 요구 → virt RAM 0x4000_0000~이라 28비트(256 MiB)로는 RAM에 못 닿음 → 거부. x86이었다면 RAM이 0부터라 ZONE_DMA로 통과했을 것 [추정 — x86 미실행].

### 3.5 [실측] COMMAND 레지스터와 /proc/interrupts

```text
000004 0406                         ← COMMAND = 0x0406 : bit1 MEM + bit2 BusMaster + bit10 INTx Disable (MSI 사용 중)
 23:          4 GICv2m-PCI-MSI-0000:00:02.0   0 Edge      edu_lp06        ← MSI 모드 (랩 1)
 15:          4 GIC-0  37 Level     edu_lp06                              ← INTx 모드 (랩 2, force_intx=1, root port 아래)
 22:          4 GIC-0  37 Level     uio_pci_generic                       ← UIO (랩 3, Image-dom)
```

- MSI 줄 읽기: `GICv2m-PCI-MSI-0000:00:02.0` = 어느 장치의 MSI 도메인인지, `0` = 그 장치의 vector 0, `Edge`. 4 = factorial IRQ + raise IRQ + DMA 2회.
- INTx 줄: `GIC-0 37` = GIC hwirq 37 = SPI 5 ([LP05 §2.6](2026-10-31_LP05_soc_host_controller_integration.html) interrupt-map 계산과 일치), `Level`. 랩 2의 COMMAND = `0006` (INTx Disable 꺼짐).

### 3.6 [실측] unbind → 사용자 공간 mmap → bind, driver_override

```text
=== [10] unbind -> user-space mmap of resource0 -> bind
ls: /sys/bus/pci/devices/0000:00:02.0/driver: No such file or directory          ← unbind 성공
resource0 mmap OK: ID=0x010000ed (edu)
liveness: wrote 0xcafef00d read 0x35010ff2 (OK)
factorial(12) = 479001600 after 2 status polls (OK)
edu_mmap exit=0
[    1.094066] edu_lp06 0000:00:02.0: dma test: PASS                             ← bind → probe 재실행
=== [11] driver_override demo: steer to no driver, then back
ls: /sys/bus/pci/devices/0000:00:02.0/driver: No such file or directory          ← override=none_such → 아무도 못 붙음
lrwxrwxrwx ... /sys/bus/pci/devices/0000:00:02.0/driver -> ../../../../../bus/pci/drivers/edu_lp06   ← override 해제 → 다시 붙음
=== [12] rmmod
[    1.392350] edu_lp06 0000:00:02.0: remove: 4 interrupts handled
```

- unbind 후에도 resource0 mmap이 동작: pcim이 BME는 껐지만 **MEM 비트는 남아 있어서** MMIO가 응답 (§2.3). 실제 장비에서는 `echo 1 > .../enable`로 MEM을 켜야 할 때가 있음.

### 3.7 [예시 출력] 실제 하드웨어 `lspci -nnk -vvv` 에서 드라이버 관련 줄 (설명용)

```text
01:00.0 Non-Volatile memory controller [0108]: Vendor Device [xxxx:yyyy]
        Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR+ FastB2B- DisINTx+
        Region 0: Memory at 80000000 (64-bit, non-prefetchable) [size=16K]
        Capabilities: [b0] MSI-X: Enable+ Count=33 Masked-
                Vector table: BAR=0 offset=00002000
                PBA: BAR=0 offset=00003000
        Kernel driver in use: nvme
        Kernel modules: nvme
```

- `Mem+ BusMaster+ DisINTx+` = probe가 끝까지 갔다는 하드웨어 증거 (= COMMAND 0x0406). 드라이버 문제 디버그 시 **가장 먼저 보는 줄**: `BusMaster-`면 DMA/MSI 불가, `Mem-`면 MMIO 무응답.
- `MSI-X: Enable+ Count=33` = 33개 vector (NVMe: admin 1 + I/O 큐 32), 테이블은 BAR0 안 0x2000.

## 4. 꼬리 질문 체인

### 체인 1 — "Write the skeleton of a PCIe driver probe."

<details><summary>Q → pci_set_master → DMA mask → MSI/MSI-X/INTx → read-back (모범 답 펼치기)</summary>

**Q. Write the skeleton of a PCIe driver probe.**
"Allocate a private struct with `devm_kzalloc`. `pcim_enable_device`, then `pcim_iomap_region` for BAR0 and check `IS_ERR`. `pci_set_master`. `dma_set_mask_and_coherent` with the device's real address width. `pci_alloc_irq_vectors` with min 1 and the max I need, allowing MSI-X, MSI and INTx, then `devm_request_irq` on `pci_irq_vector(pdev, 0)`. Store drvdata, initialize the device, and enable interrupts on the device last. Every step returns an error with `dev_err_probe`, and because everything is managed, the unwind is automatic. I wrote exactly this for QEMU's edu device and ran it."
- 한국어 핵심: enable → iomap_region → set_master → DMA mask(장치 능력대로) → irq vectors → request_irq → init(인터럽트 마지막). managed라 unwind 자동. **직접 돌려 봤다.**

**꼬리1. Why `pci_set_master`?**
"It sets Bus Master Enable in the command register. Without it the device isn't allowed to initiate transactions, so its DMA reads and writes are blocked — and MSI too, since an MSI is just a memory write from the device. It's a classic silent failure: MMIO works, but DMA never completes and interrupts never arrive."
- 한국어 핵심: COMMAND bit2. DMA와 MSI 둘 다 장치가 요청을 내는 것. **MMIO는 되는데 DMA/IRQ만 안 됨 = BME 의심.**

**꼬리2. Why set a DMA mask, and what if 64-bit fails?**
"The mask tells the DMA layer which bus addresses the device can generate, so it never hands the device an address it would truncate. Per the current DMA API docs, a 64-bit mask never fails — the kernel bounces or remaps if needed — so the old 'try 64, fall back to 32' idiom is actually discouraged; you pick the mask from what the hardware supports. Masks of 32 bits or more are always accepted with direct mapping; smaller ones depend on where RAM is, and that's the case where you must check the return value. In my lab a 28-bit mask failed with -EIO because the ARM virt machine's RAM starts at 1 GB."
- 한국어 핵심: 주소 잘림 방지. **64-bit은 never fail(문서), 64→32 fallback은 구식.** <32-bit만 실패 가능 → 반환값 검사. **28-bit -EIO 실측.**

**꼬리3. MSI vs MSI-X vs INTx?**
"INTx is a shared, level-triggered virtual wire, so the handler must check the device's status and return IRQ_NONE if it's not ours, and must clear the cause or it fires forever. MSI is a memory write, edge-like, not shared, up to 32 contiguous vectors with one address. MSI-X has a table in a BAR with up to 2048 vectors, each with its own address, data and mask, so you can give every queue its own vector and CPU. I prefer MSI-X, then MSI, then INTx, which is what `pci_alloc_irq_vectors` does with all three flags."
- 한국어 핵심: INTx 공유·레벨(원인 clear 필수), MSI 32개·edge, MSI-X 2048·vector별 mask·affinity.

**꼬리4. Why do you need a read-back after a write?**
"PCIe memory writes are posted — the CPU gets no completion, and the write may sit in buffers in the root complex or a switch. A read to the same device is non-posted and, by ordering rules, can't pass the earlier write, so when the read returns, the write has reached the device. I use it after a reset or before a delay that the hardware timing depends on, and after disabling interrupts on the device so I know it took effect."
- 한국어 핵심: posted write는 도착 보장 없음. 같은 장치 read는 추월 불가 → flush. 리셋·지연·IRQ disable 뒤.

**꼬리5 (추가). `readl` vs `readl_relaxed`?**
"Both are single, little-endian accesses ordered with respect to other accesses to the same device. The non-relaxed versions also order against normal memory: `writel` has a barrier before it so the DMA descriptors I wrote to RAM are visible before the doorbell, and `readl` has a barrier after so I don't read DMA data before checking the status. Relaxed is for hot paths where no DMA buffer is involved."
- 한국어 핵심: 일반 메모리(DMA 버퍼)와의 순서 차이. doorbell 앞 writel 필수.

</details>

### 체인 2 — "How would you build a validation tool that pokes device registers without writing a kernel driver?"

<details><summary>Q → resource0/UIO/VFIO → 위험 → 인터럽트·DMA (모범 답 펼치기)</summary>

**Q. Validation tool without a kernel driver?**
"Three levels. For quick register access, mmap `/sys/bus/pci/devices/<BDF>/resource0` with `O_SYNC` and use volatile 32-bit accesses — no driver needed, as long as memory decoding is enabled. If I need interrupts, bind `uio_pci_generic` with `new_id`; `read()` on `/dev/uioN` blocks until an INTx arrives. If I need DMA or MSI safely, use VFIO: bind `vfio-pci`, map DMA buffers through the IOMMU, and get MSI-X as eventfds. I've run the first two against QEMU's edu device."
- 한국어 핵심: resource0(레지스터만) → UIO(INTx) → VFIO(DMA+MSI, IOMMU 보호). **두 개 실측.**

**꼬리1. Risks?**
"With resource0, the kernel driver may still be bound, so two agents touch the same registers — and some registers have read side effects, like read-to-clear status. There's no protection on DMA: if I program a DMA address from user space without an IOMMU, the device can overwrite any physical memory, which is why `uio_pci_generic` is documented as insecure for DMA devices and clears bus mastering on close. Kernel lockdown can also forbid resource mmap. VFIO fixes the DMA problem but needs an IOMMU and takes the whole IOMMU group."
- 한국어 핵심: 동시 접근, read-to-clear, DMA 보호 없음, lockdown, VFIO는 IOMMU group 단위.

**꼬리2. How does UIO deliver an interrupt for a PCI device?**
"`uio_pci_generic` only supports INTx. Its kernel handler checks the Interrupt Status bit and sets INTx Disable in the command register to stop the level interrupt, then wakes the reader. User space reads a 4-byte event count, services and acks the device, then clears INTx Disable through the config file to re-arm. My lab got counts 1, 2, 3, 4 in a row with that loop."
- 한국어 핵심: INTx Disable로 mask → read 카운트 → 장치 ACK → config로 unmask. **실측 count 1→4.**

**꼬리3 (추가). Why can't UIO do MSI?**
"Generic UIO has a single interrupt and no way to program and route MSI vectors or mask them per vector, and MSI isn't shared so masking via the command register doesn't apply. VFIO exposes MSI and MSI-X through ioctls that set up eventfds per vector. For a production user-space driver like DPDK, VFIO is the standard."
- 한국어 핵심: UIO는 단일 IRQ·INTx mask 방식. MSI/MSI-X는 VFIO eventfd.

</details>

### 체인 3 (추가) — "Your driver doesn't load automatically when the device appears. Debug it."

<details><summary>Q → modalias → bind 수동 → driver_override (모범 답 펼치기)</summary>

**Q. Driver doesn't autoload.**
"Autoload is uevent to udev to modprobe by modalias. I'd check the device's `modalias` in sysfs, compare it with `modinfo` aliases of my module — if `MODULE_DEVICE_TABLE` is missing there's no alias — and make sure `depmod` ran so `modules.alias` has it. Then I'd load the module by hand: if it binds, it's the autoload path; if it doesn't, the ID table doesn't match, maybe because of subsystem IDs or a class mask."
- 한국어 핵심: modalias ↔ modinfo alias ↔ depmod. 수동 로드로 경로/매칭 분리.

**꼬리1. Another driver grabbed the device first. How do you switch it?**
"Unbind it from the current driver through its `unbind` file, write my driver's name to the device's `driver_override`, then trigger `drivers_probe`. That's exactly how people move a NIC to `vfio-pci`. I demoed it on edu: overriding to a non-existent driver left it unbound, clearing the override let my driver take it back."
- 한국어 핵심: unbind → driver_override → drivers_probe. **실측 데모.**

**꼬리2. `new_id` vs `driver_override`?**
"`new_id` adds an ID to a driver's dynamic table, so every matching device can bind to it — useful for a new device revision. `driver_override` is per device and restricts that one device to one driver, which is safer when several identical devices exist and only one should go to VFIO."
- 한국어 핵심: new_id = 드라이버 쪽 전역, driver_override = 장치 하나 지정.

</details>

### 체인 4 (추가) — "Interrupts work at first, then the system hangs with an interrupt storm (or interrupts stop)."

<details><summary>Q → INTx 레벨 → MSI 손실 → 디버그 순서 (모범 답 펼치기)</summary>

**Q. Interrupt storm after a while.**
"With INTx that's almost always the handler not clearing the cause on the device. INTx is level-triggered, so the line stays asserted and the handler runs forever; the kernel eventually says 'nobody cared' and disables the IRQ. Also on a shared line, the handler must return IRQ_NONE when its device has nothing pending. Fix: read status, ack exactly those bits, and read back to flush the ack before returning."
- 한국어 핵심: INTx 레벨 → 원인 ACK 안 하면 storm → "nobody cared". 공유 시 IRQ_NONE. ACK 후 read-back.

**꼬리1. With MSI the interrupts just stop.**
"MSI is edge-like, so if the device only sends a new MSI when its status goes from zero to non-zero, and the driver didn't ack everything, the device never generates another one. Other causes: the vector got masked, bus mastering got cleared by a reset, or the MSI address changed after an affinity move and the device didn't update. I check `/proc/interrupts` counts, the device's status register, and the MSI capability with lspci."
- 한국어 핵심: edge라 ACK 누락 시 재발생 없음, mask, BME 꺼짐, 주소 변경. /proc/interrupts + status + lspci MSI.

**꼬리2. How do you prove whether the device sent the interrupt?**
"Read the device's own interrupt status register — if it's set, the device thinks it raised one. Then on real hardware, a protocol analyzer shows whether the MWr to the MSI address went out. If the device sent it but the CPU didn't get it, it's the host side: ITS mapping, IOMMU interrupt remapping, or a masked vector."
- 한국어 핵심: 장치 status → analyzer의 MWr → host 쪽(ITS/IOMMU/mask).

</details>

## 5. Don 경험 연결

- "At Solidigm I worked on the device side of this — PCIe/NVMe subsystem IP in our Gen6 SSD controller, verified pre-silicon on FPGA with Cortex-R cores. So when I write a host driver I think about what the device sees: when Bus Master gets enabled, when the MSI-X table is programmed, and what happens to in-flight DMA on reset."
- "At SK hynix my reliability platform drove PCIe SSDs from Linux user space for long chamber runs. For a silicon validation team I'd build the same kind of tool: resource mmap for register checks, UIO or VFIO when the test needs interrupts or DMA, and a small kernel driver only where the test needs kernel services. [툴/언어/규모 `[숫자 채우기]`]"
- "This week I wrote a minimal driver for QEMU's edu device — probe with managed APIs, MSI and INTx paths, a DMA round trip, plus user-space access through sysfs resource0 and uio_pci_generic — and I hit the classic ARM issue that a 28-bit DMA mask fails because RAM starts above 1 GB."
- 주의: 리눅스 PCIe 커널 드라이버를 프로덕션으로 썼다고 과장하지 말 것 — "device side는 깊게, host driver는 구조를 알고 직접 작은 것을 돌려 봤다".

## 6. 실습 / 코드

### 6.1 파일 (`onsitePrep/lp06/`)

| 파일 | 내용 | 상태 |
|---|---|---|
| `edu_lp06.c` | 커널 드라이버: probe(§2.2 순서), ISR(status 읽고 ACK), factorial 폴링/IRQ, raise IRQ, DMA 왕복, sysfs `fact` 속성, 모듈 파라미터 `dma_bits`, `force_intx` | ✅ 6.18.55 빌드 경고 0, QEMU에서 PASS |
| `edu_mmap.c` | 사용자 공간: `resource0` mmap → ID, liveness, factorial(12) | ✅ 정적 빌드 `-Werror`, 게스트에서 exit 0 |
| `uio_edu.c` | 사용자 공간 드라이버: `/dev/uio0` mmap + INTx 대기 루프 | ✅ Image-dom 게스트에서 exit 0 |
| `Makefile` | kbuild (`KDIR=/var/lab/out/linux-build`) + `edu_mmap` | ✅ |
| `run_lab.sh` + `guest_lab.sh` / `guest_lab2.sh` | VM에서 빌드 → initramfs → QEMU 부팅 → 게스트 스크립트 | ✅ |
| `run_uio.sh` + `guest_uio.sh` | Image-dom + uio_pci_generic 랩 | ✅ |
| `run_guest_lab.log`, `run_guest_lab2.log`, `run_uio.log` | **실제 출력** | ✅ |

### 6.2 실행 방법

```bash
# macOS에서 (Lima VM elinux). 2026-10-31 VM 루트 fs가 emergency read-only → 스크립트가 /tmp/LP06(tmpfs)로 자동 대체
limactl shell --workdir / elinux -- bash $PWD/onsitePrep/lp06/run_lab.sh                      # 랩 1: MSI, DMA, mmap, bind/override
limactl shell --workdir / elinux -- env GUEST=guest_lab2.sh \
  EDU="-device pcie-root-port,id=rp1,chassis=1 -device edu,bus=rp1,dma_mask=0xffffffffff" \
  bash $PWD/onsitePrep/lp06/run_lab.sh                                                        # 랩 2: root port 아래 + INTx
limactl shell --workdir / elinux -- bash $PWD/onsitePrep/lp06/run_uio.sh                      # 랩 3: uio_pci_generic
```

- QEMU 핵심 인자: `-machine virt -cpu cortex-a57 -m 512M -kernel /var/lab/out/Image -initrd i.gz -device edu,dma_mask=0xffffffffff -append "console=ttyAMA0 LAB_CMD=sh~/guest_lab.sh"`.
- `modinfo` 실측: `alias: pci:v00001234d000011E8sv*sd*bc*sc*i*`, `vermagic: 6.18.55 SMP preempt mod_unload aarch64`, `parm: dma_bits`, `parm: force_intx`.

### 6.3 드라이버 핵심 부분 (전체는 `edu_lp06.c`)

```c
static irqreturn_t edu_isr(int irq, void *data)
{
	struct edu *edu = data;
	u32 st = readl(edu->bar0 + EDU_REG_IRQ_STATUS);

	if (!st)
		return IRQ_NONE;	/* shared INTx line: not ours */
	writel(st, edu->bar0 + EDU_REG_IRQ_ACK);	/* edu.rst: ACK is required even with MSI */
	edu->last_irq_status = st;
	edu->irq_count++;
	complete(&edu->done);
	return IRQ_HANDLED;
}

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	...
	ret = pcim_enable_device(pdev);                         /* 1. COMMAND.MEM */
	edu->bar0 = pcim_iomap_region(pdev, 0, DRV_NAME);       /* 2. request + ioremap BAR0 */
	if (IS_ERR(edu->bar0))
		return dev_err_probe(dev, PTR_ERR(edu->bar0), "BAR0\n");
	pci_set_master(pdev);                                   /* 3. COMMAND.BME */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_bits));   /* 4. */
	flags = force_intx ? PCI_IRQ_INTX : (PCI_IRQ_MSI | PCI_IRQ_INTX);
	nvec = pci_alloc_irq_vectors(pdev, 1, 1, flags);        /* 5. MSI → INTx fallback */
	edu->irq = pci_irq_vector(pdev, 0);
	ret = devm_request_irq(dev, edu->irq, edu_isr,
			       pdev->msi_enabled ? 0 : IRQF_SHARED, DRV_NAME, edu);   /* 6. */
	pci_set_drvdata(pdev, edu);
	edu_selftest(edu);                                      /* 7. device init / test */
	return 0;
}
```

- DMA 왕복 핵심: `dma_alloc_coherent(PAGE_SIZE)` → `writeq(handle, SRC); writeq(0x40000, DST); writeq(256, CNT); writeq(START|IRQ, CMD)` → IRQ 0x100 대기 → 반대 방향(`CMD |= 0x2`, DST = handle+2048) → `memcmp`.
- 경계 (edu.c 소스로 확인): DMA는 장치 내부 버퍼 `0x40000..0x40fff`(4 KiB) 범위 밖이면 QEMU가 `LOG_GUEST_ERROR`만 남기고 무시. 전송 중(`CMD.START=1`)에는 SRC/DST/CNT write가 무시됨.

### 6.4 [실측] UIO 랩 출력 (`run_uio.log`, Image-dom 6.18.55-dom)

```text
uio_pci_generic 0000:00:02.0: enabling device (0000 -> 0002)
/sys/bus/pci/devices/0000:00:02.0/driver -> ../../../../../bus/pci/drivers/uio_pci_generic
uio0
uio_pci_generic                 ← /sys/class/uio/uio0/name
0x0000000010000000              ← maps/map0/addr = BAR0
0x0000000000100000              ← maps/map0/size = 1 MiB
irq=22
uio map0 (BAR0) ID=0x010000ed
raise 0x10 -> uio count=1 irq_status=0x00000010 OK
raise 0x20 -> uio count=2 irq_status=0x00000020 OK
raise 0x40 -> uio count=3 irq_status=0x00000040 OK
factorial(7) irq: count=4 irq_status=0x00000001 fact=5040 OK
uio_edu exit=0
 22:          4 GIC-0  37 Level     uio_pci_generic
```

- `uio_pci_generic`는 id_table이 `NULL`("only dynamic id's") → `echo "1234 11e8" > /sys/bus/pci/drivers/uio_pci_generic/new_id`로 바인딩.
- 루프: 커널이 INTx Disable로 mask → `read()`가 카운트 반환 → 사용자가 edu 0x64로 ACK(레벨 소스 제거) → config COMMAND bit10 clear로 unmask.

### 6.5 [미실행] VFIO 랩 레시피

- 필요: IOMMU. QEMU `-machine virt,iommu=smmuv3` + `Image-dom`(`CONFIG_ARM_SMMU_V3=y`, `CONFIG_VFIO_PCI=y`) [미실행].
- 순서 (docs.kernel.org VFIO 예제 순서): `echo vfio-pci > .../driver_override; echo BDF > /sys/bus/pci/drivers_probe` → `/dev/vfio/vfio`(container) `VFIO_GET_API_VERSION`/`VFIO_CHECK_EXTENSION` → `/dev/vfio/<group>` `VFIO_GROUP_GET_STATUS` → `VFIO_GROUP_SET_CONTAINER` → `VFIO_SET_IOMMU(VFIO_TYPE1_IOMMU)` → `VFIO_IOMMU_MAP_DMA`(IOVA 0x1000에 버퍼 매핑) → `VFIO_GROUP_GET_DEVICE_FD` → `VFIO_DEVICE_GET_REGION_INFO(BAR0)` + mmap → edu DMA SRC=IOVA로 전송 → `VFIO_DEVICE_SET_IRQS`(MSI → eventfd; 문서 예제엔 호출 없음). 28-bit edu도 **IOVA를 낮게 잡으면 DMA 가능** — §3.4 실패의 IOMMU 해법을 보여 줄 수 있음 [미실행].

## 7. 자주 틀리는 것

1. **`pci_set_master` 누락** → MMIO는 정상, DMA·MSI만 조용히 실패. lspci `BusMaster-` 확인.
2. **`pcim_iomap_regions()` + `pcim_iomap_table()`을 새 코드에** — 6.18에서 DEPRECATED. `pcim_iomap_region()` + `IS_ERR()`.
3. **`pci_request_irq`를 managed로 착각** → remove에서 `pci_free_irq` 안 하면 모듈 언로드 후 핸들러가 남음. `devm_request_irq`는 자동.
4. **장치 인터럽트를 `request_irq` 전에 켬** → 핸들러 없는 IRQ, INTx면 storm/"nobody cared".
5. **INTx 핸들러가 원인 확인 없이 `IRQ_HANDLED`** → 공유 라인에서 다른 장치 인터럽트를 삼킴. 원인 없으면 `IRQ_NONE`.
6. **MSI면 ACK 필요 없다고 생각** → edu처럼 status를 안 지우면 다음 이벤트 때 MSI가 다시 안 오는 장치가 많음 (edge 재발생 조건).
7. **write 직후 "장치에 도착했다"고 가정** → posted. 같은 장치 read-back으로 flush.
8. **64-bit 레지스터를 32-bit 두 번으로 쪼개 씀** → 장치가 하위 write에서 latch하면 반쪽 값으로 동작. edu는 0x84 무시. 데이터시트의 lo/hi 순서 확인.
9. **DMA mask를 장치 능력보다 크게** → 장치가 상위 비트를 잘라 엉뚱한 RAM에 씀(QEMU edu의 `edu_clamp_addr`가 바로 이 동작을 흉내). 작게 잡으면 ARM에서 실패(§3.4).
10. **remove에서 DMA 정지 없이 버퍼 해제** → use-after-free를 하드웨어가 함. devres는 DMA를 멈춰 주지 않는다. `.shutdown`도 같음(kexec).
11. **커널 드라이버가 붙은 채로 resource0 mmap 툴 실행** → 두 주체가 같은 레지스터 조작. 검증 툴은 unbind 후 사용하거나 드라이버의 debugfs를 경유.
12. **"64-bit 시도 → 실패하면 32-bit" 관용구를 정답처럼 말하기** — 옛 드라이버엔 많지만 현행 DMA API HOWTO는 64-bit mask가 never fail이라 이 패턴을 wrong이라고 명시. 면접에선 "장치 능력대로 고르고, 32 미만 mask만 실패 검사"가 최신 답 (LP00 목록의 "64→32 fallback" 문구는 이 노트로 정정).
13. **옛 예제의 `PCI_IRQ_LEGACY`** — 6.18에선 없음, `PCI_IRQ_INTX`.

## 8. 출처

1. QEMU edu 장치 스펙 — https://gitlab.com/qemu-project/qemu/-/raw/master/docs/specs/edu.rst (레지스터 맵, IRQ/DMA 동작)
2. QEMU edu 구현 — https://gitlab.com/qemu-project/qemu/-/raw/master/hw/misc/edu.c (ID 0x010000ed, DMA 100 ms 타이머, 28-bit 기본 mask, 0x80+ 4/8B 접근, `msi_init`)
3. Linux PCI driver guide — https://docs.kernel.org/PCI/pci.html (probe/remove 순서, `pci_set_master`, DMA mask, MMIO posting)
4. Linux 6.18.55 소스 (랩 VM `/var/lab/src/linux-6.18.55`; 온라인 https://elixir.bootlin.com/linux/v6.18/source) — `drivers/pci/devres.c` (`pcim_enable_device`, `pcim_iomap_region`, `pcim_iomap_regions` DEPRECATED), `drivers/pci/irq.c` (`pci_request_irq`), `drivers/pci/msi/msi.c` (`pcim_setup_msi_release`), `drivers/pci/pci-driver.c` (`pci_match_device`, `new_id`), `drivers/pci/pci.c` (`do_pci_disable_device`), `drivers/pci/setup-res.c` ("enabling device"), `include/linux/pci.h` (`PCI_IRQ_INTX`), `kernel/dma/direct.c` (`dma_direct_supported`), `include/asm-generic/io.h` + `arch/arm64/include/asm/io.h` (readl/writel barrier), `drivers/uio/uio_pci_generic.c`
5. Linux `include/uapi/linux/pci_regs.h` (COMMAND/STATUS 비트, header offset) — 6.18.55
6. Linux DMA API HOWTO ("never fail" 64-bit mask, 64→32 fallback은 wrong) — https://docs.kernel.org/core-api/dma-api-howto.html
7. UIO HOWTO (uio_pci_generic: new_id 바인딩, Interrupt Disable 비트로 재활성화) — https://docs.kernel.org/driver-api/uio-howto.html · VFIO (ioctl 순서 예제) — https://docs.kernel.org/driver-api/vfio.html
8. 실측 로그: `onsitePrep/lp06/run_guest_lab.log`, `run_guest_lab2.log`, `run_uio.log` (2026-10-31, QEMU 10.2.1, Linux 6.18.55 / 6.18.55-dom)

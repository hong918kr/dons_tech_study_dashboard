# LP08 · DMA · IOMMU(SMMU) · 캐시 일관성
<!-- theme: pastel -->

> **묶음**: D 인터럽트·DMA·에러 · **우선**: P1 · **선행 노트**: [LP07 인터럽트](2026-10-31_LP07_interrupts_intx_msi_msix.html), [LP06 endpoint 드라이버](2026-10-31_LP06_endpoint_driver_dev.html) · **다음 노트**: [LP09 AER, DPC, 복구](2026-10-31_LP09_aer_dpc_error_handling.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **관련 실습**: [V3 DMA descriptor ring](hmScreenPrep/problems/V3_dma_desc_ring/index.html) · **코드**: `onsitePrep/lp08/noncoherent_dma_sim.c` (macOS ALL PASS), `onsitePrep/lp08/edu_dma_fault.c` (QEMU SMMUv3 실제 fault 재현) · **작성**: 2026-10-31

## 0. TL;DR

- 이 노트로 답할 것: "**우리 SoC에서만 DMA 데이터가 깨진다 (x86에선 멀쩡)**", "coherent vs streaming DMA?", "DMA mask가 뭐고 32-bit 장치는?", "IOMMU/SMMU는 왜 쓰고 fault 로그는 어떻게 읽나?", "DMA를 검증(validation)할 때 무엇을 sweep하나?"
- **외울 것 3개**
  1. **주소는 3종류**: CPU 가상 주소(VA) · 물리 주소(PA) · **장치가 쓰는 DMA 주소(`dma_addr_t`, IOMMU가 있으면 IOVA)**. 드라이버는 장치에게 **절대 VA/PA를 직접 주지 말고 DMA API가 돌려준 `dma_addr_t`만** 준다.
  2. **Non-coherent SoC 규칙** (arm64 Linux 실제 구현): 장치에 넘길 때(map / `sync_for_device`) = **clean**(dirty 줄 write-back), CPU가 결과를 볼 때(unmap / `sync_for_cpu`) = **invalidate**. 그리고 **DMA 버퍼는 캐시 라인 단독 소유**(arm64 `ARCH_DMA_MINALIGN` = 128 B).
  3. **IOMMU = 장치용 MMU**: RID → StreamID(`iommu-map`) → 장치별 page table. 장점 = 격리·32-bit 장치 구제·scatter를 연속으로 보이게. fault는 `arm-smmu-v3 … event: F_TRANSLATION client: <BDF> sid: … iova: …` 로 찍힌다(실측).
- DMA API 2종: **coherent** `dma_alloc_coherent()` (descriptor ring, 오래 사는 공유 구조, 매번 sync 불필요) vs **streaming** `dma_map_single/sg()` + `dma_sync_single_for_cpu/device()` (패킷·블록 데이터, 1회성, 방향 지정 `DMA_TO_DEVICE / FROM_DEVICE / BIDIRECTIONAL`).
- 검증 방법론 한 줄: **패턴(PRBS·address-as-data·walking 1s) × 크기 sweep(1 B … max, MPS/MRRS/4 KiB 경계 ±1) × 정렬 sweep(0–63/127) × 동시성(CPU 부하·다채널) × IOMMU on/off → CRC/guard 비교 + 처리량**.
- **영어 30초 (headline: "DMA data is corrupted on our ARM SoC but fine on x86")**: "On x86 the PCIe root complex snoops the CPU caches, so DMA is coherent for free. On many ARM SoCs the PCIe controller isn't on a coherent port, so the device reads and writes DRAM behind the cache. If the driver doesn't use the DMA API correctly — clean before the device reads, invalidate before the CPU reads — or if a receive buffer shares a cache line with other data, you get stale or overwritten bytes. I'd first check whether the device tree marks the controller dma-coherent and whether that's true in hardware, then look for drivers that hand raw physical addresses to the device or skip dma_sync calls, and test with cache-line-aligned buffers and patterns that make stale data obvious."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 DMA가 왜 필요한가 — "택배 트럭"

- 1 GB를 장치에서 메모리로 옮길 때 CPU가 4바이트씩 읽어서 쓰면 CPU가 그 일만 하다 끝난다.
- 그래서 장치가 **직접 메모리를 읽고 쓰게** 한다 = **DMA(Direct Memory Access)**. PCIe에서는 장치가 **Memory Read/Write TLP**를 스스로 보내는 것이고, 이를 허락하는 스위치가 **Bus Master Enable**(Command 레지스터 bit 2)이다.
- 비유: CPU = 사무실 직원, DRAM = 창고, 장치의 DMA 엔진 = **택배 트럭**. 직원이 "창고 3번 선반의 상자들을 실어 가" 라고 **주소 목록(descriptor)** 만 주면 트럭이 알아서 옮긴다.

### 1.2 주소가 왜 세 종류인가 — "별명 · 지번 · 택배사 송장 주소"

```text
 CPU 프로그램 ── VA (가상 주소: 프로세스/커널이 쓰는 '별명')
                 │ CPU MMU 페이지 테이블
                 ▼
 DRAM ───────── PA (물리 주소: 실제 '지번')
                 ▲
                 │ (IOMMU 없으면) 버스 주소 = PA (+ SoC inbound 창 오프셋이 있을 수도)
                 │ (IOMMU 있으면) IOMMU(SMMU) 페이지 테이블
 장치 DMA ───── DMA 주소 / IOVA (I/O Virtual Address: 택배사 송장 주소)
```

- 장치는 CPU의 VA를 모른다. 그렇다고 PA를 줘도 되는 게 아니다: ① IOMMU가 있으면 장치는 IOVA로 말해야 하고 ② SoC에 따라 PCIe 쪽에서 본 DRAM 주소가 CPU 쪽 PA와 **오프셋이 다를 수 있고**(inbound 창) ③ 장치가 32-bit 주소만 낼 수 있으면 4 GiB 위 메모리는 못 닿는다.
- 그래서 Linux는 **DMA API**가 "이 버퍼를 장치가 볼 때의 주소" `dma_addr_t`를 계산해 준다.

### 1.3 캐시 일관성(coherency) — "개인 메모장 vs 공용 창고"

- CPU는 창고(DRAM)에 매번 가지 않고 **개인 메모장(캐시)** 에 적어 두고 일한다. 메모장에 고친 내용(dirty line)은 **나중에야** 창고에 반영된다.
- 트럭(DMA)은 **창고만 본다**. 그러면:
  - CPU가 메모장에만 써 둔 데이터를 트럭이 가져가면 → **옛날 데이터** 배송 (TX 깨짐)
  - 트럭이 창고에 새 상자를 넣었는데 CPU는 메모장의 옛 기록을 봄 → **옛날 데이터** 읽음 (RX 깨짐)
- **Coherent 시스템**(x86, 일부 ARM 서버/고급 SoC): 하드웨어가 트럭이 창고에 갈 때 **CPU 메모장도 엿본다(snoop)** → 소프트웨어 걱정 없음.
- **Non-coherent 시스템**(많은 임베디드 ARM SoC의 PCIe): 소프트웨어가 직접 "메모장 내용을 창고에 반영해라(**clean**)", "메모장의 이 부분은 버려라(**invalidate**)"를 해야 한다. Linux DMA API가 이걸 대신 해 준다 — **올바르게 부르기만 하면**.

### 1.4 IOMMU — "경비실 출입 명부"

- IOMMU가 없으면 DMA 장치는 **아무 물리 주소나** 읽고 쓸 수 있다. 버그 있는 펌웨어나 악성 장치가 커널 메모리를 덮어쓸 수 있다.
- IOMMU(ARM: **SMMU**, Intel: **VT-d**, AMD-Vi)는 장치마다 **"너는 이 주소들만 출입 가능"** 명부(page table)를 둔다. 장치가 낸 IOVA → 명부에서 PA로 변환, 명부에 없으면 **차단 + fault 로그**.
- 덤: 32-bit 장치도 IOVA를 4 GiB 아래로 주면 4 GiB 위 메모리에 닿는다. 흩어진 페이지(scatter-gather)를 장치에겐 연속 IOVA로 보이게 할 수도 있다.

### 1.5 그림: SoC 안의 DMA 경로

```text
          +------------------- SoC -------------------+
          |  CPU cores ── L1/L2/L3 caches             |
          |        │           ▲ snoop? (coherent일 때만)
          |        ▼           │                       |
          |   Interconnect (CCI/CMN, ACE/CHI) ──── DRAM controller ── DRAM
          |        ▲                                   |
          |        │  (coherent port or non-coherent AXI port)
          |      SMMU  ← StreamID = f(RID) by iommu-map
          |        ▲
          |  PCIe Root Complex (controller) — inbound 창(iATU): PCIe 주소 → AXI 주소
          +--------│----------------------------------+
                   │ PCIe link: MRd/MWr TLP (주소 = IOVA)
               Endpoint DMA 엔진 ── descriptor ring(coherent 메모리)에 적힌 버퍼 주소 사용
```

### 1.6 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| DMA | Direct Memory Access | 장치가 CPU 없이 메모리를 직접 읽고 씀 | 고속 I/O의 기본 |
| Bus Master | Command reg bit 2 | 장치가 스스로 메모리 요청을 내도 되는 허가 | 꺼지면 DMA·MSI 모두 불가 (`pci_set_master`) |
| VA / PA | Virtual / Physical Address | CPU가 쓰는 주소 / DRAM 실제 주소 | 장치에게 주면 안 되는 주소들 |
| `dma_addr_t` | DMA address | 장치가 써야 하는 주소 (DMA API가 계산) | 장치에 줄 유일한 주소 |
| IOVA | I/O Virtual Address | IOMMU가 변환하는 장치 쪽 가상 주소 | SMMU fault 로그에 찍힘 |
| IOMMU / SMMU | I/O MMU / System MMU (ARM) | 장치용 MMU: 주소 변환 + 접근 제어 | 격리·보안·32-bit 구제 |
| StreamID (SID) | — | SMMU가 장치를 구분하는 ID (PCIe는 RID 기반) | `iommu-map`이 RID→SID |
| STE / CD | Stream Table Entry / Context Descriptor | SMMUv3에서 SID별 설정 / 주소공간(페이지테이블) 정보 | fault 원인 추적 |
| VT-d / DMAR | Intel Virtualization Tech for Directed I/O / DMA Remapping | Intel의 IOMMU (ACPI DMAR 테이블) | x86 비교 |
| IOMMU group | — | IOMMU가 서로 **구분 못 하는** 장치 묶음 | VFIO 할당 단위 (LP12) |
| ACS | Access Control Services | 스위치에서 P2P를 막고 위로 올려보내는 기능 | 그룹을 잘게 나눔 |
| passthrough / identity | — | IOMMU 변환 끄기(IOVA = PA) | 성능↑ 격리↓ (`iommu.passthrough=1`) |
| strict / lazy | — | unmap 시 IOTLB 즉시 무효화 / 모아서 | 보안 vs 성능 (`iommu.strict`) |
| ATS / PRI / PASID | Address Translation Services / Page Request Interface / Process Address Space ID | 장치가 변환을 캐시 / 페이지폴트 요청 / 프로세스별 주소공간 | SVA, 가속기 (개념) |
| coherent DMA | — | HW가 캐시를 snoop → SW 캐시 관리 불필요 | DT `dma-coherent` |
| non-coherent DMA | — | HW snoop 없음 → clean/invalidate 필요 | 임베디드 ARM 흔함 |
| clean / invalidate | (cache maintenance) | dirty 줄을 DRAM에 씀 / 캐시 줄을 버림 | 순서·범위 틀리면 데이터 깨짐 |
| cache line | — | 캐시가 다루는 최소 단위 (보통 64 B) | 버퍼가 줄을 공유하면 사고 |
| `ARCH_DMA_MINALIGN` | — | DMA 버퍼 최소 정렬 (arm64 128) | kmalloc 정렬 근거 |
| coherent API | `dma_alloc_coherent` | CPU·장치가 늘 같은 값을 보는 메모리 할당 | descriptor ring |
| streaming API | `dma_map_single/sg` | 기존 버퍼를 1회성으로 장치에 빌려줌 | 데이터 버퍼 |
| DMA mask | — | 장치가 낼 수 있는 주소 비트 수 (32/40/64) | 4 GiB 위 접근 가능 여부 |
| swiotlb | software IO TLB | 장치가 못 닿는 버퍼를 닿는 곳에 **복사(bounce)** | 느림, 로그로 확인 |
| inbound window | (iATU inbound / `dma-ranges`) | PCIe 주소 → SoC 내부 주소 변환 창 | 창 밖 DMA = 에러 |
| descriptor ring | — | 장치에게 줄 작업 목록(원형 큐) | OWN 비트 + barrier |
| MPS / MRRS | Max Payload Size / Max Read Request Size | TLP 하나의 최대 데이터 / 읽기 요청 최대 크기 | 성능·sweep 경계 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Bus mastering과 DMA TLP

- Endpoint의 DMA 쓰기 = **MWr** (posted, 응답 없음). DMA 읽기 = **MRd** → Root Complex가 **CplD**(Completion with Data)로 응답, 큰 읽기는 RCB(Read Completion Boundary, 64/128 B) 단위로 여러 CplD로 쪼개져 올 수 있음.
- **4 KiB 경계 규칙**: 요청 하나(MRd/MWr)는 주소+길이가 **4 KiB 경계를 넘으면 안 된다** → DMA 엔진이 쪼개야 함. 검증에서 "경계 −1/+1 바이트" 케이스가 중요한 이유.
- 크기 제약: MWr payload ≤ **MPS**(Device Control의 Max_Payload_Size), MRd 요청 ≤ **MRRS**. 둘 다 `lspci -vv`의 `DevCtl: MaxPayload 256 bytes, MaxReadReq 512 bytes`.
- Command bit 2 (`PCI_COMMAND_MASTER`)가 0이면 장치의 요청은 나가지 않는다(Root Port가 막거나 장치가 안 냄). Linux: `pci_set_master()`. 실측(LP08 랩): 드라이버 없는 QEMU edu는 `Command before = 0x0000` → 직접 0x0006으로 켜고 나서야 DMA가 나갔다.

### 2.2 Linux DMA API — coherent vs streaming

| 항목 | Coherent (consistent) | Streaming |
|---|---|---|
| 함수 | `dma_alloc_coherent(dev, size, &dma_handle, GFP_KERNEL)` / `dma_free_coherent` | `dma_map_single(dev, cpu_ptr, size, dir)` / `dma_unmap_single`, `dma_map_sg` / `dma_unmap_sg`, `dma_map_page` |
| 메모리 | API가 **새로 할당** (non-coherent HW면 uncached/WC 매핑 [추정: 아키텍처별 상이]) | **기존 버퍼**(skb, bio 페이지)를 빌려줌 |
| 캐시 관리 | 불필요 (항상 같은 값) — 단 **쓰기 순서는 barrier 필요** (`dma_wmb()` 등) | map/unmap/sync가 clean/invalidate 수행 |
| 수명 | 길다 (드라이버 수명) | 짧다 (I/O 1회) |
| 용도 | descriptor ring, 상태 블록, 메일박스 | 패킷 데이터, 블록 I/O 데이터 |
| 성능 | non-coherent HW에서 CPU 접근 느림(uncached) | CPU는 캐시된 채 빠름, map/unmap 비용 |

**방향(enum dma_data_direction)**: `DMA_TO_DEVICE`(CPU가 쓰고 장치가 읽음, TX), `DMA_FROM_DEVICE`(장치가 쓰고 CPU가 읽음, RX), `DMA_BIDIRECTIONAL`(둘 다, 비쌈), `DMA_NONE`(디버그용).

**규칙 (dma-api-howto [1])**
- map 이후 unmap 전까지 버퍼는 **장치 소유**. CPU가 중간에 보려면 `dma_sync_single_for_cpu()` → 보고/고치고 → `dma_sync_single_for_device()` → 장치에 다시 넘김.
- `dma_map_*` 결과는 반드시 **`dma_mapping_error(dev, handle)`** 로 확인 (swiotlb 고갈, IOVA 고갈).
- 스택 변수, `vmalloc` 메모리(물리 연속 아님)에 `dma_map_single` 금지 — 페이지 단위로 `dma_map_page`/sg.
- **arm64 실제 구현** (`arch/arm64/mm/dma-mapping.c`, 6.18, 소스 확인 [확인됨] [3]):

```c
void arch_sync_dma_for_device(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{       /* map / sync_for_device: 방향과 무관하게 clean (to PoC) */
        dcache_clean_poc(start, start + size);
}
void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{       /* unmap / sync_for_cpu: TO_DEVICE면 아무것도 안 함, 아니면 invalidate */
        if (dir == DMA_TO_DEVICE) return;
        dcache_inval_poc(start, start + size);
}
```

  - 왜 FROM_DEVICE도 map 때 **clean**인가: 그 버퍼에 dirty 줄이 남아 있으면 DMA 도중 캐시가 그 줄을 **자연 evict**하면서 DMA가 쓴 데이터를 덮어쓴다 → 미리 깨끗하게.
  - 왜 unmap 때 **다시 invalidate**인가: DMA 도중 CPU가 **투기적으로(speculative prefetch)** 그 줄을 다시 캐시에 가져올 수 있다 → 끝난 뒤 한 번 더 버려야 최신 DRAM 값을 읽음. (코드 §6.1이 이 두 경우를 재현)

### 2.3 DMA mask와 32-bit 장치

```c
/* probe: 장치가 실제로 낼 수 있는 폭을 그대로 선언 (dma-api-howto "Recommended code") */
if (dev_supports_64bit)
        ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
else
        ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
if (ret)
        return ret;    /* 32-bit 이하 mask에서만 실패 가능 */
```

- **흔한 오해**: "64-bit 시도 → 실패하면 32-bit fallback" 코드. 현재 DMA API HOWTO는 이걸 **"Wrong code"로 명시**한다 — `dma_set_mask_and_coherent()`는 **32보다 큰 mask에서는 절대 실패하지 않는다**(플랫폼 한계는 커널이 bus limit/IOMMU/swiotlb로 내부 처리) [1]. 그래서 mask는 "장치 능력"을 정직하게 선언하는 것이고, fallback 분기는 의미가 없다. (LP06 노트와 같은 입장)
- descriptor는 32-bit, 데이터는 64-bit 같은 장치는 `dma_set_mask(64)` + `dma_set_coherent_mask(32)`를 따로 건다.

- mask = 장치가 낼 수 있는 주소 비트. 32-bit 장치 + 4 GiB 넘는 DRAM이면 Linux는 ① IOMMU로 4 GiB 아래 IOVA를 주거나 ② `ZONE_DMA32`에서 할당(coherent) ③ **swiotlb bounce**(streaming).
- sysfs 실측: Lima virtio-blk `dma_mask_bits=64`, `consistent_dma_mask_bits=64` / QEMU 랩 e1000e `64`, root port·edu(드라이버 없음) `32`(기본값) [확인됨].
- SoC 함정: 장치는 64-bit인데 **PCIe 컨트롤러 inbound 창이나 interconnect가 40-bit** 같은 제한 → DT `dma-ranges`로 표현해야 커널이 bus-level mask를 걸어 줌. 안 하면 상위 주소 DMA가 조용히 엉뚱한 곳으로.

### 2.4 swiotlb (bounce buffer)

- 장치가 버퍼 주소에 닿지 못할 때(mask), 또는 non-coherent arm64에서 **작은 kmalloc 버퍼가 캐시 라인 정렬이 아닐 때**(`CONFIG_DMA_BOUNCE_UNALIGNED_KMALLOC`, 6.5+ [추정: 도입 버전]), 또는 기밀 VM(암호화 메모리)에서 → 커널이 미리 잡아 둔 **bounce 영역에 복사**해서 그 주소를 장치에 준다. unmap 때 다시 복사.
- 실측 dmesg [5] [6]:

```text
[Lima VM, 12 GiB]   software IO TLB: mapped [mem 0x00000000f9e00000-0x00000000fde00000] (64MB)
[QEMU 랩, 512 MiB]   software IO TLB: SWIOTLB bounce buffer size adjusted to 0MB
                     software IO TLB: mapped [mem 0x000000005d540000-0x000000005d5c0000] (0MB)
```

  - 해석: 기본 64 MB bounce 풀(4 GiB 아래). 메모리가 전부 32-bit 범위인 작은 머신은 bounce가 필요 없다고 판단해 축소.
- 증상: 처리량이 이상하게 낮고 `perf`에 `memcpy`/`swiotlb_bounce`가 보이면 bounce 중. 풀 고갈 시 `swiotlb buffer is full` + `dma_mapping_error`. 부트 옵션 `swiotlb=<slabs>`.

### 2.5 IOMMU 개념 — ARM SMMUv3 중심

```text
 PCIe TLP (RID=02:00.0, addr=IOVA 0x1000)
    │  iommu-map: RID → StreamID (QEMU virt: identity, SID 0x200)
    ▼
 SMMUv3 Stream Table[SID] = STE ──► (stage 1) CD[SSID/PASID] ──► 페이지 테이블 (CPU와 같은 ARM LPAE 형식)
                                 └► (stage 2, 가상화) VMID별 테이블
    │ 변환 성공 → PA로 interconnect에
    └ 실패 → 쓰기는 버림/읽기는 abort, Event Queue에 이벤트(F_TRANSLATION, F_PERMISSION, C_BAD_STE …) → IRQ
```

- **격리**: 장치마다 자기 IOVA 공간. 드라이버가 map한 버퍼만 보인다 → 버그 DMA가 커널을 못 덮음 (대신 fault 로그로 드러남 = 검증에 유리).
- **기본 도메인 타입** (실측 dmesg 둘 다): `iommu: Default domain type: Translated`, `iommu: DMA domain TLB invalidation policy: strict mode`.
  - `iommu.passthrough=1` → identity(변환 없음), `iommu.strict=0` → lazy invalidation(빠르지만 unmap 직후 잠깐 접근 가능) [2].
  - Ubuntu 7.0 커널 config 실측: `CONFIG_IOMMU_DEFAULT_DMA_STRICT=y` [확인됨].
- **Intel VT-d**: ACPI DMAR 테이블, Root/Context table(BDF 기반) → 2nd-level/1st-level 페이지 테이블. fault는 `DMAR: [DMA Read NO_PASID] Request device [..] fault addr … [fault reason …]` 형식 [추정: 커널 버전별 문구 차이].
- **IOMMU group** [5]: IOMMU가 TLP의 출처를 확실히 구분할 수 없는 장치들(예: ACS 없는 스위치 아래 형제 장치들은 P2P로 서로 직접 갈 수 있음)을 한 그룹으로 묶음 → VFIO는 그룹 단위로 넘김. **ACS**가 P2P를 막고 위로 올려보내면 그룹이 쪼개진다. 실측 QEMU 랩: 장치마다 그룹 0–4 (root port 각각 별도) [확인됨]. 자세한 건 LP12.
- **ATS / PRI / PASID** (개념): 장치가 IOTLB를 자체 캐시(ATS: Translation Request → 변환 결과 캐시, 이후 "Translated" TLP), 페이지 없으면 PRI로 OS에 page fault 요청, PASID로 프로세스별 주소공간 → **SVA(Shared Virtual Addressing)**: 가속기가 사용자 프로세스 포인터를 그대로 사용. 검증 포인트: invalidation 완료 순서, ATS 캐시 stale.

### 2.6 ARM coherent vs non-coherent — DT `dma-coherent`

- 하드웨어: PCIe 컨트롤러(또는 SMMU)가 interconnect의 **coherent 포트(ACE-Lite/CHI I/O coherent)** 에 붙어 snoop을 하면 coherent. 일반 AXI 포트면 non-coherent.
- 소프트웨어는 이를 **DT 속성 `dma-coherent`** (ACPI는 `_CCA`)로만 안다. 실측: QEMU virt `pcie@10000000 { … dma-coherent; }`, `smmuv3@9050000 { … dma-coherent; }` [확인됨].
- **두 방향의 거짓말 모두 위험**:
  - HW non-coherent인데 DT에 `dma-coherent` → 커널이 clean/invalidate를 **생략** → 랜덤 데이터 깨짐 (가장 흔한 bring-up 버그 [추정]).
  - HW coherent인데 속성 누락 → 정확하지만 **느림**(불필요한 cache maintenance, coherent 메모리가 uncached).
- SMMU 페이지 테이블 walk 자체의 coherency도 별도(SMMU 노드의 `dma-coherent`).

### 2.7 무엇이 깨지는가 — 증상 카탈로그

| 원인 | 증상 | 확인 방법 |
|---|---|---|
| TX 전 clean 누락 | 장치가 옛 데이터/0을 송신, 패턴의 **캐시 라인 단위 블록**이 틀림 | 64 B 단위로 틀린 위치가 몰림 |
| RX 후 invalidate 누락 | CPU가 이전 내용 읽음, 재시도하면 맞기도 함 | 두 번째 읽기 전 invalidate하면 사라짐 |
| map 때만 invalidate, unmap 때 없음 | 드물게, 부하 높을 때만 깨짐 (speculative fill) | 부하 + 긴 DMA에서 빈도↑ |
| 버퍼가 다른 변수와 캐시 라인 공유 | DMA 데이터의 **앞/뒤 끝부분**이 깨지거나 옆 변수가 깨짐 | 정렬 sweep에서 offset≠0일 때만 실패 |
| 장치에 VA/PA 직접 전달 | IOMMU 있으면 즉시 fault, 없으면 엉뚱한 메모리 오염 | SMMU fault 로그, KASAN |
| mask 미설정/오설정 | 4 GiB 위 버퍼에서만 실패 | 메모리 많이 쓴 뒤에만 재현 |
| inbound 창 밖 주소 | completion timeout / UR / 데이터 안 옴 | 컨트롤러 에러 레지스터, AER (LP09) |
| descriptor 쓰기 순서 (OWN 먼저) | 장치가 반쯤 쓴 descriptor 처리 | `dma_wmb()` 위치 확인 (V3) |

### 2.8 SoC 컨트롤러의 inbound 창

- Root Complex로 **들어오는**(장치→메모리) TLP 주소를 SoC 내부 AXI 주소로 바꾸는 것이 **inbound ATU**(DWC iATU 등). DT에서는 host bridge의 **`dma-ranges`** 로 표현 [7] (`ranges`는 반대 방향 = outbound, CPU→장치 BAR).
- 예 [예시]: `dma-ranges = <0x02000000 0x0 0x00000000  0x0 0x80000000  0x0 0x80000000>` → PCIe 주소 0x0–0x7fffffff가 SoC 주소 0x80000000부터 매핑 → 이 SoC에서는 **bus address ≠ PA** (오프셋 0x80000000). DMA API가 이 오프셋을 반영해 `dma_addr_t`를 준다 — 드라이버가 PA를 직접 쓰면 틀림.
- 창 밖 주소로 오는 TLP: 컨트롤러가 UR/CA로 응답하거나 버림 → 장치 쪽에선 completion timeout. **MSI doorbell 주소도 이 창/매핑 안**이어야 함 (LP07).

### 2.9 Descriptor ring과 barrier (→ V3 실습)

```text
 coherent 메모리 (dma_alloc_coherent):  desc[0..N-1] = { buf_dma_addr, len, ctrl(OWN|IRQ|EOP), status }
 SW submit:  desc.addr/len 채움 → dma_wmb() → desc.ctrl |= OWN → (wmb) → doorbell MMIO write
 HW:         OWN=1 인 desc 처리 → 데이터 DMA → status 기록 → OWN=0 → MSI
 SW reap:    OWN==0 확인 → dma_rmb() → status/len 읽기 → 버퍼 dma_unmap(→ invalidate) → 상위로
```

- descriptor 자체는 coherent라 캐시 관리 불필요하지만 **순서**(필드 먼저, OWN 나중)는 `dma_wmb()`가 보장. MMIO doorbell 전에는 `wmb()`(또는 `writel`의 내장 barrier).
- 데이터 버퍼는 streaming → map/unmap. V3 문제의 guard-byte self-test(앞뒤 16 B)는 "1-byte overrun" 같은 엔진 버그를 잡는 검증 패턴: [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html).

## 3. Linux에서 볼 것

### 3.1 명령 모음

```bash
dmesg | grep -i -E "smmu|iommu|DMAR|AMD-Vi|swiotlb|software IO TLB"   # IOMMU 초기화/fault, bounce 풀
ls /sys/kernel/iommu_groups/                         # 그룹 목록 (IOMMU 없으면 비어 있음)
find /sys/kernel/iommu_groups/ -name '*:*'           # 그룹별 장치
readlink /sys/bus/pci/devices/0000:01:00.0/iommu_group
cat /sys/kernel/iommu_groups/3/type                  # DMA / DMA-FQ / identity / blocked
cat /sys/kernel/iommu_groups/3/reserved_regions      # MSI 창 등 IOVA 예약 구역
cat /sys/bus/pci/devices/0000:01:00.0/dma_mask_bits  # 장치 DMA mask
lspci -vv -s 01:00.0 | grep -E "BusMaster|MaxPayload|MaxReadReq"
cat /proc/cmdline                                    # iommu.passthrough= / iommu.strict= / swiotlb=
```

- 디버그 커널 옵션: `CONFIG_DMA_API_DEBUG=y`(+ `dma_debug=`) — unmap 누락, 방향 틀림, sync 누락, 중복 map을 경고. `CONFIG_IOMMU_DEBUGFS`(Ubuntu 7.0 config에서 꺼져 있음 [확인됨]).

### 3.2 실제 출력 ① — SMMUv3 translation fault (QEMU 랩, kernel 6.18.55) [5]

edu 장치(02:00.0, 드라이버 없음 → 빈 DMA 도메인)에게 IOVA 0x1000에서 64 B 읽기 DMA를 시킨 결과 (`onsitePrep/lp08/edu_dma_fault.c`):

```text
[    0.710743] arm-smmu-v3 9050000.smmuv3: event 0x10 received:
[    0.710793] arm-smmu-v3 9050000.smmuv3: 	0x0000020000000010
[    0.710811] arm-smmu-v3 9050000.smmuv3: 	0x0000020800000000
[    0.710816] arm-smmu-v3 9050000.smmuv3: 	0x0000000000001000
[    0.710821] arm-smmu-v3 9050000.smmuv3: 	0x0000000000000000
[    0.710856] arm-smmu-v3 9050000.smmuv3: event: F_TRANSLATION client: 0000:02:00.0 sid: 0x200 ssid: 0x0 iova: 0x1000 ipa: 0x0
[    0.710893] arm-smmu-v3 9050000.smmuv3: unpriv data read s1 "Input address caused fault" stag: 0x0
[    0.710927] arm-smmu-v3 9050000.smmuv3: event 0x10 received:
...                                                   (iova 0x1004, 0x1008 … 0x1024 반복)
```

| 줄 | 해석 |
|---|---|
| `event 0x10 received` | SMMUv3 이벤트 번호 0x10 = **F_TRANSLATION** (변환 테이블에 매핑 없음) |
| 4개의 64-bit 원본 | Event Queue 레코드 32 B 그대로. 첫 워드 하위 8비트 = 이벤트 ID 0x10, 상위 32비트 = **StreamID 0x200**; 세 번째 워드 = **입력 주소(IOVA) 0x1000** |
| `client: 0000:02:00.0 sid: 0x200` | 커널이 SID를 장치로 역매핑. `iommu-map` identity라 SID = RID = bus 2 << 8 → **어느 장치가 범인인지 바로 나옴** |
| `ssid: 0x0` | SubstreamID(PASID) 0 = 기본 주소공간 |
| `iova: 0x1000` | 장치가 접근한 주소. **드라이버가 장치에 준 주소가 맞는지** 대조 |
| `unpriv data read s1` | 비특권, 데이터(명령 아님), **읽기**, stage 1 변환에서 실패 |
| `"Input address caused fault"` | 사람용 원인 문자열 |
| 0x1004, 0x1008 … 반복 | QEMU edu가 4 B 단위로 접근해서 이벤트가 여러 개. **쓰기 방향 fault는 같은 실행에서 로그가 안 보였는데 ratelimit으로 추정** [추정] |

- 또 하나의 실측: `GICv3 106 Edge arm-smmu-v3-evtq` 인터럽트 카운트가 0 → 2로 증가.
- **중요 관찰**: 장치 쪽에서는 `DMA … : cmd done` — **장치는 실패를 모른다**(QEMU edu 모델). 실제 HW에서도 SMMU에 막힌 **쓰기는 조용히 버려지고**, 읽기는 에러 completion(UR/CA)을 받아 장치가 AER로 보고할 수도 있다 [추정: SMMU/RC 구현별]. → 검증에서 "DMA 완료 = 데이터 맞음"으로 가정하지 말고 **데이터를 비교**해야 하는 이유.
- 읽는 순서: ① `client`/`sid`로 장치 특정 ② `iova`가 드라이버가 map한 범위인지(unmap 후 사용? 길이 초과? 장치에 PA를 줬나?) ③ read/write 방향 ④ 이벤트 종류 (F_TRANSLATION = 매핑 없음, F_PERMISSION = 읽기전용에 쓰기 등, C_BAD_STE = SMMU 설정 자체 문제).

### 3.3 실제 출력 ② — IOMMU 그룹과 예약 구역 (QEMU 랩)

```text
[    0.289073] arm-smmu-v3 9050000.smmuv3: ias 44-bit, oas 44-bit (features 0x01088705)
[    0.291000] arm-smmu-v3 9050000.smmuv3: allocated 65536 entries for cmdq
[    0.291729] arm-smmu-v3 9050000.smmuv3: allocated 32768 entries for evtq
[    0.294528] pci 0000:00:00.0: Adding to iommu group 0
[    0.294654] pci 0000:00:01.0: Adding to iommu group 1
[    0.295025] pci 0000:01:00.0: Adding to iommu group 3
$ find /sys/kernel/iommu_groups/ -name '*:*'
/sys/kernel/iommu_groups/3/devices/0000:01:00.0
/sys/kernel/iommu_groups/1/devices/0000:00:01.0
/sys/kernel/iommu_groups/4/devices/0000:02:00.0
...
$ cat /sys/kernel/iommu_groups/3/reserved_regions
0x0000000008000000 0x00000000080fffff msi
$ cat /sys/kernel/iommu_groups/3/type
DMA
```

- `ias/oas 44-bit` = 입력(IOVA)/출력(PA) 주소 폭. `cmdq`/`evtq` = SMMU가 커널 명령(TLB 무효화 등)을 받는 큐 / fault를 보고하는 큐.
- `reserved_regions … msi` = ITS doorbell을 매핑하는 MSI IOVA 창 (LP07 §2.5). DMA API는 이 범위를 IOVA로 할당하지 않는다.
- `type DMA` = translated 기본 도메인 (`identity`면 passthrough).
- 대조: **Lima VM(Apple VZ)은 vIOMMU가 없어서 `/sys/kernel/iommu_groups/`가 비어 있음** — dmesg에 `iommu: Default domain type: Translated`가 찍혀도 실제 IOMMU 장치가 없으면 그룹이 안 생긴다 [확인됨].

### 3.4 x86 DMAR fault 형식 [예시 출력]

```text
DMAR: DRHD: handling fault status reg 2
DMAR: [DMA Write NO_PASID] Request device [3b:00.0] fault addr 0xfff00000 [fault reason 0x05] PTE Write access is not set
```

- [예시 출력] (형식은 drivers/iommu/intel/dmar.c의 printk 기준으로 재구성, 값은 가상). 읽는 법은 SMMU와 같다: **누가(BDF) · 어디를(addr) · 무엇을(Read/Write) · 왜(reason)**.

### 3.5 DMA 검증 방법론 (validation)

| 축 | 값 | 왜 |
|---|---|---|
| 데이터 패턴 | all-0, all-1, 0x55/0xAA, walking-1/0, incrementing, **address-as-data**(각 워드에 자기 주소), **PRBS**(의사난수, 시드 기록) | stuck bit, 비트 간섭, 주소 꼬임(address-as-data가 즉시 위치를 알려줌), 일반 랜덤 |
| 크기 | 1, 2, 3, 4, 63, 64, 65, MPS−1/MPS/MPS+1, MRRS±1, 4095/4096/4097, 큰 값(MB), 최대 descriptor 길이 | TLP 분할, 꼬리 처리, 경계 |
| 정렬 | src/dst offset 0…63 (또는 127), 페이지 경계 직전 시작 | 캐시 라인 공유, byte enable, 엔진 정렬 버그 |
| 4 KiB 경계 | 경계를 0/1/N 바이트 넘는 버퍼 | 요청 분할 규칙 |
| 방향/종류 | H2D, D2H, D2D(엔진 내부), P2P(가능하면) | 각 경로 별도 버그 |
| 동시성 | 다채널 동시, CPU가 인접 메모리에 쓰기 부하, 다른 DMA 장치 동시 | coherency·arbitration·순서 |
| 시스템 설정 | IOMMU translated/passthrough, strict/lazy, swiotlb 강제(`swiotlb=force`), ASPM on/off | 경로별 차이 |
| 검사 | **CRC32/해시 비교 + 앞뒤 guard 영역**(V3) + 엔진 status + SMMU/AER 로그 0건 | "완료"와 "정확"은 다름 |
| 성능 | GB/s vs 이론치(링크 속도×폭×인코딩 효율 − TLP 오버헤드), latency(doorbell→MSI), CPU 사용률 | MPS/MRRS·RO 설정 영향 |
| 장시간 | 수 시간 soak, PVT 코너(온도·전압) | 드문 SI/coherency 버그 |
| 음성 테스트 | 의도적 잘못된 IOVA(→ SMMU fault 1건 기대), 잘못된 descriptor | 에러 경로 동작 확인 (LP09와 연결) |

- 실패 분석 팁: 틀린 바이트의 **위치 분포**를 본다 — 64 B 단위로 뭉침 = 캐시 문제, 4 KiB 단위 = 페이지/경계/IOMMU, MPS 단위 = TLP 분할, 특정 비트만 = 데이터 라인 SI/stuck.

## 4. 꼬리 질문 체인

### Chain 1 (LP00): "DMA data is corrupted on our SoC but fine on x86."

<details>
<summary>Q → 꼬리1 → 꼬리2 → 꼬리3 → 꼬리4 (답 보기)</summary>

- **Q. DMA data is corrupted on our SoC but fine on x86. Where do you start?**
  - EN: "The biggest difference is coherency: x86 snoops CPU caches on DMA, many ARM SoCs don't. So I'd confirm whether the PCIe path is actually coherent in hardware, whether the device tree says dma-coherent, and whether the driver uses the DMA API properly. Then I look at the failure pattern — if the bad bytes cluster in cache-line-sized chunks, it's almost certainly cache maintenance."
  - 한: coherency 차이 → HW 실제 vs DT `dma-coherent` vs 드라이버 → 틀린 위치 패턴.
- **꼬리1. How do you know if it's coherent or not?**
  - EN: "From the SoC integration: whether the controller or SMMU sits on an I/O-coherent port of the interconnect, and whether snooping is enabled in the interconnect configuration. Software only knows through the dma-coherent property or ACPI _CCA. An experiment helps too: write a pattern from the CPU without any cache clean and see if the device reads it correctly."
  - 한: 인터커넥트 포트(ACE-Lite/CHI), DT/ACPI 속성, clean 없이 실험.
- **꼬리2. Where do you put the cache maintenance?**
  - EN: "Ideally nowhere by hand — the streaming DMA API does it. dma_map or sync_for_device cleans the buffer before the device touches it; dma_unmap or sync_for_cpu invalidates it before the CPU reads results. On arm64 the clean happens for every direction, and the invalidate happens at unmap because the CPU can speculatively refill lines during the transfer. The other half is alignment: a receive buffer must not share a cache line with anything the CPU writes."
  - 한: map=clean, unmap=invalidate(투기 fill 대비), 버퍼 라인 단독.
- **꼬리3. You see an SMMU context fault in dmesg. What does it tell you?**
  - EN: "It tells me which device — the stream ID maps back to a BDF — what address it used, whether it was a read or a write, and the fault type. A translation fault means the IOVA wasn't mapped: typically use after unmap, a length overrun, or the driver handing the device a physical address instead of the DMA handle. A permission fault means it wrote to a read-only mapping, which usually means the wrong DMA direction."
  - 한: SID→BDF, IOVA, R/W, 종류 → unmap 후 사용/길이 초과/PA 전달/방향 틀림.
- **꼬리4. How would you test DMA correctness in validation?**
  - EN: "A matrix: patterns like PRBS and address-as-data, sizes swept around 1 byte, the max payload, the max read request and the 4 KiB boundary, source and destination offsets across a cache line, with CPU load and multiple channels running concurrently, IOMMU on and off. Every transfer is checked with a CRC plus guard bytes before and after the buffer, and the SMMU and AER logs must stay clean. Then throughput against the theoretical link rate."
  - 한: 패턴 × 크기 × 정렬 × 동시성 × IOMMU → CRC + guard + 로그 0 + 성능.

</details>

### Chain 2: "Coherent vs streaming DMA?"

<details>
<summary>Q → 꼬리1 → 꼬리2 (답 보기)</summary>

- **Q. When do you use dma_alloc_coherent and when dma_map_single?**
  - EN: "Coherent for long-lived structures that both sides poll — descriptor rings, completion queues, status blocks — because no sync is needed. Streaming for the data buffers that come from the network or block layer and are used once in one direction; you map, the device does the transfer, you unmap, and the API does the cache work."
  - 한: 링/상태 = coherent, 데이터 = streaming.
- **꼬리1. If coherent memory needs no sync, why do you still need barriers?**
  - EN: "Coherency says both see the same value eventually, not in what order. When I fill a descriptor and then set the OWN bit, the device must not see OWN before the address and length, so I put dma_wmb between them, and a write barrier before the MMIO doorbell. On the reap side, dma_rmb after seeing OWN cleared, before reading the status."
  - 한: 값 일치 ≠ 순서, `dma_wmb`/`dma_rmb`/`wmb`.
- **꼬리2. Why not just make everything coherent?**
  - EN: "On a non-coherent SoC coherent memory is mapped uncached or write-combined, so CPU access to big data buffers would be slow, and you can't hand an existing skb or page-cache page to the device without copying. Streaming lets the CPU work on cached data and only pays for maintenance at handoff."
  - 한: non-coherent에선 coherent = uncached → 느림, 기존 버퍼 재사용 불가.

</details>

### Chain 3: "Our device only does 32-bit DMA, the board has 16 GB."

<details>
<summary>Q → 꼬리1 → 꼬리2 (답 보기)</summary>

- **Q. A device only supports 32-bit DMA addresses on a 16 GB system. What happens?**
  - EN: "The driver declares a 32-bit DMA mask — it shouldn't try 64 and fall back, because setting a mask above 32 bits never fails; you declare what the device can really do. With an IOMMU, the kernel just allocates IOVAs below 4 GB and maps them to wherever the memory really is, so it's transparent. Without an IOMMU, coherent allocations come from ZONE_DMA32, and streaming mappings of high buffers get bounced through swiotlb, which costs a memcpy per transfer."
  - 한: IOMMU면 IOVA<4G, 없으면 DMA32 + swiotlb bounce.
- **꼬리1. How do you notice bouncing in practice?**
  - EN: "Throughput lower than expected with CPU time in memcpy or swiotlb functions in perf, and if the pool runs out, 'swiotlb buffer is full' messages and mapping errors. The pool size is printed at boot as 'software IO TLB: mapped … 64MB'."
  - 한: perf에 memcpy/swiotlb, `swiotlb buffer is full`, 부트 로그 풀 크기.
- **꼬리2. What if the device claims 64-bit but the SoC interconnect only has 40 address bits?**
  - EN: "Then the platform has to describe that limit, usually with dma-ranges on the host bridge, so the kernel applies a bus DMA limit on top of the device mask. Otherwise addresses above 40 bits silently alias or fault, and it only shows up once memory gets fragmented high."
  - 한: `dma-ranges`로 bus limit 표현, 안 하면 상위 주소에서만 깨짐.

</details>

### Chain 4 (추가): "Would you run with the IOMMU in passthrough?"

<details>
<summary>Q → 꼬리1 → 꼬리2 (답 보기)</summary>

- **Q. Would you turn the IOMMU off or use passthrough for performance?**
  - EN: "For production on a vehicle platform I'd keep translation on — isolation protects the kernel from a misbehaving device or firmware, which matters for safety and security. For performance characterization I'd measure both: passthrough and lazy invalidation remove map/unmap and IOTLB costs, and the difference tells me how much of the overhead is the IOMMU."
  - 한: 운영은 translated(격리), 성능 분석은 둘 다 측정.
- **꼬리1. Why is the IOMMU useful for validation specifically?**
  - EN: "Because a stray DMA becomes a logged fault with the device, address and direction, instead of silent memory corruption somewhere else. I'd even run negative tests that expect exactly one fault."
  - 한: 잘못된 DMA가 '조용한 오염' 대신 '로그'가 됨.
- **꼬리2. What's an IOMMU group and why does ACS matter?**
  - EN: "A group is the smallest set of devices the IOMMU can isolate from each other. If a switch doesn't have ACS, devices under it can talk peer-to-peer without going through the IOMMU, so they land in the same group and must be assigned together. ACS forces that traffic upstream so each device can be its own group."
  - 한: 격리 최소 단위, ACS 없으면 P2P 때문에 묶임.

</details>

### Chain 5 (추가): "What's the difference between a PCIe address and a CPU physical address on your SoC?"

<details>
<summary>Q → 꼬리1 (답 보기)</summary>

- **Q. Is the address the device puts in a TLP the same as the CPU physical address?**
  - EN: "Not necessarily. Without an IOMMU it's the bus address, which equals physical only if the controller's inbound window is identity-mapped; many SoCs add an offset or limit, described by dma-ranges. With an IOMMU it's an IOVA. That's why drivers only use dma_addr_t from the DMA API."
  - 한: inbound 창 오프셋/IOVA → `dma_addr_t`만 사용.
- **꼬리1. A DMA to a valid buffer times out with no SMMU fault. Ideas?**
  - EN: "The address may be outside the controller's inbound window, so the root complex rejects it before the SMMU — the device sees an unsupported request or a completion timeout, which should show up as an AER error on the device or root port. I'd check the iATU inbound configuration against dma-ranges."
  - 한: inbound 창 밖 → UR/CTO → AER 확인 (LP09).

</details>

## 5. Don 경험 연결

- "At Solidigm I verified DMA and SRAM/DRAM paths in the SSD controller SoC before RTL freeze, on FPGA, so the validation matrix here — patterns, size and alignment sweeps around the payload and 4 KiB boundaries, guard bytes, CRC compare — is how I already think about DMA. [숫자 채우기: 몇 개 DMA 채널/케이스]"
- "On the NVMe side the controller is the DMA master: it reads submission queues and writes completions and data into host memory, so I know the device-side view of PRP/SGL lists and why a completion must not pass its data."
- "On the Cortex-R82 bring-up, cache maintenance and memory attributes were exactly the class of bug that looks random — data wrong only in 64-byte chunks — so I'd use the error-location pattern first to tell a cache bug from a signal-integrity bug. [구체 사례 있으면 채우기]"
- "Working with the silicon validation team on performance tuning, I'd compare measured DMA throughput against link rate minus TLP overhead, and treat max payload and read request settings as test parameters, not constants."

## 6. 실습 / 코드

### 6.1 `onsitePrep/lp08/noncoherent_dma_sim.c` — non-coherent DMA 버그 시뮬레이션 (macOS 컴파일·실행 ✅)

- 모델: 64 B 라인 write-back 캐시 + DRAM + "DRAM만 보는" DMA 엔진. 캐시 관리 함수 `clean()`/`invalidate()`는 arm64 `dcache_clean_poc`/`dcache_inval_poc` 의미 그대로.
- 4가지 버그를 일부러 만들고, Linux 방식(map=clean, unmap=invalidate, 라인 정렬)으로 고쳐지는지 확인. ASan/UBSan 켜고 실행 (처음엔 ASan이 시뮬레이터 자체의 배열 범위 버그를 잡아서 고침).
- 실제 출력 (`onsitePrep/lp08/noncoherent_dma_sim.out.txt`):

```text
non-coherent DMA model: 64-byte lines, write-back cache, DMA sees DRAM only
  TX without clean (device reads stale DRAM)                 CORRUPT
  TX with clean before DMA                                   data OK
  RX without invalidate (CPU hits stale lines)               CORRUPT
  RX with invalidate before CPU reads                        data OK
  RX + speculative fill, maintenance only at map             CORRUPT
  RX + speculative fill, invalidate at unmap (Linux way)     data OK
  RX buffer shares a line with a CPU variable                CORRUPT
  RX buffer cache-line aligned (own lines)                   data OK
ALL PASS (each bug reproduced, each fix works)
```

- 면접용 포인트: 4번째 케이스는 **invalidate를 아무리 잘 해도** 해결이 안 된다 — CPU가 같은 라인의 변수를 쓰면 그 dirty 라인이 evict될 때 DMA 데이터를 덮는다. 해결책은 **정렬/패딩**(`____cacheline_aligned`, `ARCH_DMA_MINALIGN`, 별도 할당)이다.

### 6.2 `onsitePrep/lp08/edu_dma_fault.c` — 실제 SMMUv3 fault 만들기 (QEMU에서 실행 ✅)

- QEMU edu(1234:11e8) DMA 레지스터 [4] (0x80 src, 0x88 dst, 0x90 count, 0x98 cmd)를 `resource0` mmap으로 직접 프로그래밍, `config` 파일로 Command에 Memory+BusMaster를 켬 → 매핑 안 된 IOVA 0x1000을 읽고/쓰게 함.
- 빌드: Lima VM에서 `gcc -static -O2 -Wall -Wextra -Werror` (macOS 불가 — Linux sysfs 전용). 실행 스크립트: `onsitePrep/lp08/qemu_lab/host8.sh`, `run8.sh`. 원본 로그: `onsitePrep/lp08/qemu_lab/guest_output_2026-10-31.txt`.
- 결과: §3.2의 `F_TRANSLATION client: 0000:02:00.0 sid: 0x200 … iova: 0x1000` (실측). 장치 쪽은 `cmd done` = 실패를 모름.
- 확장 [미실행]: edu용 커널 모듈에서 `dma_alloc_coherent`로 정상 버퍼를 만들어 DMA → fault 0건 + 데이터 일치 확인 (양성 테스트), `iommu.passthrough=1`로 부팅해서 같은 잘못된 DMA가 **로그 없이** PA 0x1000을 건드리는지 비교 (IOMMU 가치 시연).

## 7. 자주 틀리는 것

1. **장치에 `virt_to_phys()` 값을 준다** — IOMMU/inbound 오프셋이 있으면 틀림. `dma_addr_t`만.
2. **"coherent 메모리면 barrier 필요 없다"** — 순서는 별개. OWN 비트 전 `dma_wmb()`.
3. **map 후 CPU가 버퍼를 만진다** — unmap 또는 `sync_for_cpu` 전까지 장치 소유.
4. **RX는 invalidate만 하면 된다** — 라인 공유 시 eviction이 DMA 데이터를 덮음. 정렬이 필수.
5. **invalidate는 DMA 시작 전에 한 번이면 된다** — speculative fill 때문에 끝난 뒤(unmap)에도 필요.
6. **`dma_mapping_error()` 생략** — swiotlb/IOVA 고갈 시 0 또는 쓰레기 주소로 DMA.
7. **DT `dma-coherent`를 "있으면 좋은 것"으로 붙인다** — HW가 non-coherent면 조용한 데이터 깨짐.
8. **SMMU fault가 없으면 DMA는 맞다** — passthrough/IOMMU 없음에선 fault 자체가 안 남. 데이터 비교가 진짜 판정.
9. **장치가 "완료"라고 하면 데이터도 맞다** — SMMU에 막힌 쓰기는 조용히 버려질 수 있음(실측 edu: `cmd done`).
10. **4 KiB 경계/MPS는 SW와 무관** — 엔진/IP 버그가 경계에서 많이 나므로 sweep 필수.

## 8. 출처

1. Linux kernel docs — Dynamic DMA mapping Guide (core-api/dma-api-howto): https://docs.kernel.org/core-api/dma-api-howto.html · DMA API (core-api/dma-api): https://docs.kernel.org/core-api/dma-api.html
2. Linux kernel-parameters (`iommu.passthrough=`, `iommu.strict=`, `swiotlb=`): https://raw.githubusercontent.com/torvalds/linux/master/Documentation/admin-guide/kernel-parameters.txt
3. Linux 6.18.55 source (Lima VM `/var/lab/src`): `arch/arm64/mm/dma-mapping.c` (arch_sync_dma_for_device/cpu), `arch/arm64/include/asm/cache.h` (ARCH_DMA_MINALIGN 128), `include/linux/dma-map-ops.h` (DMA_BOUNCE_UNALIGNED_KMALLOC), `drivers/iommu/arm/arm-smmu-v3/` — mirror: https://elixir.bootlin.com/linux/v6.18/source
4. QEMU edu device spec (DMA registers): https://www.qemu.org/docs/master/specs/edu.html
5. 실측: QEMU 10.2.1 `virt,gic-version=3,iommu=smmuv3` + kernel 6.18.55 — `onsitePrep/lp08/qemu_lab/guest_output_2026-10-31.txt`, `onsitePrep/lp07/qemu_lab/guest_output_2026-10-31.txt` (iommu groups, reserved_regions, swiotlb)
6. 실측: Lima VM `elinux` (Apple VZ, kernel 7.0.0-38-generic) — swiotlb 64MB, `iommu: Default domain type`, `/boot/config-*` IOMMU 옵션
7. Devicetree schema — `iommu-map` (pci-iommu.yaml): https://raw.githubusercontent.com/devicetree-org/dt-schema/main/dtschema/schemas/pci/pci-iommu.yaml · host bridge 공통(`dma-ranges` 등, pci-bus-common.yaml): https://raw.githubusercontent.com/devicetree-org/dt-schema/main/dtschema/schemas/pci/pci-bus-common.yaml

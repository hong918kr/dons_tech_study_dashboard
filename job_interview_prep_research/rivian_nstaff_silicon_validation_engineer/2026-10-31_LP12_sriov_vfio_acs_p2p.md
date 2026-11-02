# LP12 · SR-IOV · VFIO · ACS · P2P (가볍게)
<!-- theme: pastel -->

> **묶음**: D 고급 기능 · **우선**: P2 (가볍게 — "What's SR-IOV for?", "Why do IOMMU groups matter?" 두 질문만 확실히) · **선행 노트**: [LP08 DMA/IOMMU](2026-10-31_LP08_dma_iommu_coherency.html) · [LP11 리셋](2026-10-31_LP11_reset_types.html) · **다음 노트**: [LP13 Linux PCIe 도구](2026-10-31_LP13_linux_pcie_tools.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **코드**: [onsitePrep/lp12/sriov_iommu_probe.sh](onsitePrep/lp12/sriov_iommu_probe.sh) (QEMU SMMUv3 + igb에서 실행 ✅, Lima VM에서 실행 ✅)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "SR-IOV는 왜 있나, PF/VF는?", "VF는 BDF를 어떻게 얻나?", "VFIO 패스스루에 IOMMU group이 왜 중요한가?", "ACS가 뭐고 group을 왜 바꾸나?", "P2P DMA는 왜 스위치가 중요한가?", "차량 중앙 컴퓨터에서 이걸 왜 신경 쓰나?"
- **외울 것 1 — SR-IOV**: 장치 하나(**PF**, Physical Function: 관리 권한 있는 진짜 function)가 가벼운 **VF**(Virtual Function: 자기 큐·BAR·MSI-X를 가진 복제 function)를 여러 개 만들어 VM마다 하나씩 직접 넘김 → 하이퍼바이저가 패킷을 중계하지 않아도 됨. VF의 Routing ID = **PF RID + First VF Offset + (n−1) × VF Stride** (QEMU igb 실측: offset 128, stride 2 → `01:10.0`, `01:10.2`).
- **외울 것 2 — VFIO와 IOMMU group**: VFIO는 장치를 user space/VM에 넘기는 커널 프레임워크. **IOMMU**(장치 DMA 주소를 변환·검사하는 MMU)가 격리를 보장하는 최소 단위가 **IOMMU group** — 그룹 안 장치끼리는 IOMMU를 안 거치고 서로 DMA할 수 있을지 몰라서, **그룹 통째로만** 넘길 수 있다.
- **외울 것 3 — ACS**: **ACS** (Access Control Services: 스위치/root port가 peer-to-peer 요청을 위로 돌려보내 IOMMU를 거치게 하는 규칙)가 있는 포트 아래는 그룹이 잘게 쪼개진다. Linux가 요구하는 비트 = **SV | RR | CR | UF** (Source Validation, Request Redirect, Completion Redirect, Upstream Forwarding). 반대로 **P2P DMA**는 ACS redirect가 꺼져야 스위치 안에서 직접 가므로 **격리 vs P2P 성능이 서로 반대** 방향.
- **30초 영어 답 ("What's SR-IOV for, and why do IOMMU groups matter for passthrough?")**: "SR-IOV lets one physical device expose many lightweight virtual functions, each with its own config space, BARs and interrupts, so each VM gets a VF directly and the data path bypasses the hypervisor. The physical function keeps the management role. To hand a VF to a VM safely, the IOMMU has to translate and restrict that function's DMA, and the kernel can only guarantee isolation per IOMMU group — the set of devices that might talk to each other without going through the IOMMU, for example peers behind a switch without ACS. VFIO requires you to own the whole group, so the topology and ACS support decide what you can actually pass through."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 은행 창구, 출입증, 검문소

- **SR-IOV**: 은행 지점(물리 장치) 하나에 **본점 관리 창구(PF)** 와 고객 전용 **무인 창구 여러 개(VF)** 가 있다. 고객(VM)마다 자기 창구를 직접 쓰니 지점장(하이퍼바이저)이 돈을 대신 옮겨 줄 필요가 없다. 창구 개수·설정은 관리 창구(PF 드라이버)만 바꿀 수 있다.
- **IOMMU**: 창구 직원이 금고(시스템 메모리)에 갈 때 **출입증 검사기** — "이 직원(장치)은 이 칸(주소 범위)만" 허용. 없으면 장치가 아무 메모리나 DMA로 쓸 수 있다 → VM 하나가 장치를 통해 호스트 메모리를 덮어쓸 수 있음.
- **IOMMU group**: 검사기를 안 거치고 **옆 창구끼리 서류를 직접 건넬 수 있는 창구들의 묶음**. 이 묶음은 나눠서 다른 고객에게 줄 수 없다.
- **ACS**: 지점 내부 규칙 "창구끼리 직접 건네지 말고 **반드시 검문소(위쪽 root complex → IOMMU)를 거쳐라**". ACS가 있는 포트면 창구가 각자 분리된 그룹이 된다.
- **P2P DMA**: 반대로 GPU가 NVMe의 메모리에 **직접** 쓰는 지름길. 빠르지만 ACS가 "검문소 거쳐"를 강제하면 지름길이 막힌다.

### 1.2 그림 — PF/VF, ACS, 그룹

```text
                        Root Complex  ── IOMMU (Arm SMMUv3 / Intel VT-d)
                              │
          ┌───────────────────┼──────────────────────────┐
     Root Port 00:02.0     Root Port 00:03.0        Root Port 00:04.0
     [ACS SV RR CR UF ✓]   [ACS ✓]                  [ACS ✓]
          │                   │                          │
     igb PF 01:00.0        nvme 02:00.0           Switch Upstream 03:00.0
       ├─ VF 01:10.0                                     │
       └─ VF 01:10.2                         ┌───────────┴───────────┐
                                     Switch DP 04:00.0        Switch DP 04:01.0
                                     [ACS ✗]                  [ACS ✗]
                                         │                        │
                                     e1000e 05:00.0            edu 06:00.0

 QEMU 실측 IOMMU groups:  PF·VF 각각 따로(5, 10, 11), nvme 6,
                          {04:00.0, 05:00.0} = 8,  {04:01.0, 06:00.0} = 9   ← ACS 없는 DP는 자기 아래 장치와 한 묶음
```

### 1.3 그림 — ACS Request Redirect가 하는 일

```text
 ACS 없음 (P2P 직통):                          ACS RR/CR 켬 (위로 우회):
   EP-A ──TLP(dst=EP-B BAR)──▶ Switch ──▶ EP-B   EP-A ──▶ Switch ──▶ Root Complex ─IOMMU 검사─▶ Switch ──▶ EP-B
   IOMMU는 이 트래픽을 못 봄 → 격리 불가           IOMMU가 변환/차단 가능 → 격리 OK, 대신 지연·대역폭 손해
```

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| SR-IOV | Single Root I/O Virtualization | 장치 하나가 VF 여러 개를 만들어 VM에 직접 할당 | 가상화 I/O 성능 |
| PF | Physical Function | SR-IOV 관리 권한 있는 진짜 function | VF 생성·설정 |
| VF | Virtual Function | 큐·BAR·MSI-X만 가진 가벼운 function | VM에 직접 할당 |
| RID / BDF | Routing ID / Bus:Device.Function | PCIe 주소 (16비트) | VF도 고유 RID 필요 |
| ARI | Alternative Routing-ID Interpretation | Device 번호 5비트를 function에 써서 함수 256개 | VF 많을 때 필요 |
| VFIO | Virtual Function I/O (Linux 프레임워크) | 장치를 user space/VM에 안전하게 넘김 | DPDK, QEMU passthrough |
| vfio-pci | — | PCI 장치용 VFIO 드라이버 | `driver_override` 로 바인딩 |
| IOMMU | I/O Memory Management Unit | 장치 DMA 주소 변환·권한 검사 (Arm: SMMU) | 격리의 실체 |
| IOMMU group | — | 격리 가능한 최소 장치 묶음 | VFIO는 그룹 단위 |
| ACS | Access Control Services | 포트가 P2P를 막거나 위로 돌려보내는 기능 | 그룹 크기 결정 |
| P2P DMA | Peer-to-Peer DMA | 장치 → 다른 장치 BAR로 직접 DMA | GPU↔NVMe, 지연·대역폭 |
| ATS | Address Translation Services | 장치가 IOMMU 변환 결과를 캐시 | ACS DT/TB와 연관 |
| driver_override | — | sysfs: 이 장치는 이 드라이버만 매칭 | vfio-pci 바인딩 표준 방법 |
| noiommu | VFIO no-IOMMU mode | IOMMU 없이 VFIO (격리 없음, 커널 taint) | 임베디드 user-space 드라이버 실험 |
| passthrough | — | VM에 실제 장치를 통째로 넘김 | SR-IOV VF 또는 전체 장치 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 SR-IOV Extended Capability (ID 0x10)

- 오프셋은 `pci_regs.h` `PCI_SRIOV_*` 와 대조 [1] [확인됨]:

| 오프셋 | 필드 | 뜻 | QEMU igb 실측 |
|---|---|---|---|
| +0x04 | SR-IOV Capabilities | bit0 VF Migration Capable, bit 31:21 IntMsgNum | Migration- |
| +0x08 | SR-IOV Control | **bit0 VF Enable**, bit1 Migration En, bit3 **VF MSE** (Memory Space Enable), bit4 **ARI Capable Hierarchy** | 0x0010 → enable 후 **0x0019** |
| +0x0C | InitialVFs | | 8 |
| +0x0E | **TotalVFs** | HW가 만들 수 있는 최대 | 8 |
| +0x10 | **NumVFs** | SW가 쓰는 현재 개수 | 0 → 2 |
| +0x14 | **First VF Offset** | 첫 VF RID = PF RID + 이 값 | 128 |
| +0x16 | **VF Stride** | 다음 VF까지 간격 | 2 |
| +0x1A | **VF Device ID** | VF의 Device ID (VF config의 ID 칸은 0xFFFF) | 0x10ca |
| +0x1C / +0x20 | Supported / System Page Size | VF BAR 정렬 단위 | 0x553 / 0x1 (4 KiB) |
| +0x24…0x38 | **VF BAR0–5** | VF 하나당 크기, 전체 = 크기 × NumVFs 연속 배치 | Region 0 @ 0x8000000000, Region 3 @ 0x8000020000 |

- **RID 계산**: PF `01:00.0` = RID 0x0100. VF1 = 0x0100 + 128 = 0x0180 → bus 01, dev 0x10, fn 0 = `01:10.0`. VF2 = +2 = 0x0182 = `01:10.2`. 실측 `virtfn0 -> 0000:01:10.0`, `virtfn1 -> 0000:01:10.2` 와 일치 ✅.
- **VF config 특이점**: VF의 Vendor ID/Device ID 칸은 0xFFFF로 읽힘(장치 ID는 PF의 SR-IOV cap에서), 커널 주석도 "Reading Vendor ID for VFs … returns ~0" 이라 리셋 후 폴링에 Command 레지스터를 쓴다 ([LP11 §2.7](2026-10-31_LP11_reset_types.html)) [확인됨: pci.c 주석]. VF는 FLR 필수, BAR은 PF의 VF BAR에서 오프셋으로 결정.

### 2.2 Linux에서 VF 만들기

| sysfs (PF 디렉터리) | 뜻 [2] |
|---|---|
| `sriov_totalvfs` | 만들 수 있는 최대 (드라이버가 HW TotalVFs보다 **줄일 수 있음**) |
| `sriov_numvfs` | 쓰기: 0 → N 으로 VF 생성, 0으로 제거 (N→M 직접 변경은 안 되고 0 거쳐야 함) |
| `sriov_offset`, `sriov_stride`, `sriov_vf_device` | 위 cap 필드 |
| `sriov_drivers_autoprobe` | 0이면 VF 생겨도 드라이버 자동 probe 안 함 (vfio에 넘길 때 편함) |
| `sriov_vf_total_msix`, VF의 `sriov_vf_msix_count` | VF별 MSI-X 벡터 수 조정 (지원 드라이버만) |
| `virtfnN` (PF) / `physfn` (VF) | 서로 가리키는 심볼릭 링크 |

- 드라이버 쪽: PF 드라이버가 `.sriov_configure(pdev, num_vfs)` 콜백 구현 → 안에서 `pci_enable_sriov(pdev, n)` / `pci_disable_sriov(pdev)` [2]. igb는 `pci_sriov_set_totalvfs(pdev, 7)` 로 **sysfs totalvfs를 7로 제한** (HW cap은 8) — `igb_main.c` 실측 + 코드 [확인됨].
- 커널 설정: `CONFIG_PCI_IOV=y` (Lima Ubuntu 커널, 랩 커널 모두 y 확인).

### 2.3 VFIO — 장치를 user space/VM에 넘기기

```text
 1) IOMMU가 장치를 그룹에 넣음:   /sys/kernel/iommu_groups/<N>/devices/<BDF>
 2) 그룹의 모든 장치를 호스트 드라이버에서 떼기 (또는 vfio-pci에 바인딩)
       echo vfio-pci > /sys/bus/pci/devices/<BDF>/driver_override
       echo <BDF>    > /sys/bus/pci/devices/<BDF>/driver/unbind
       echo <BDF>    > /sys/bus/pci/drivers_probe
 3) /dev/vfio/<N> (group 인터페이스) 또는 /dev/vfio/devices/vfioX (cdev, iommufd) 생김
 4) user space(QEMU, DPDK, 테스트 툴)가 열어서: BAR mmap, config 읽기/쓰기, IRQ → eventfd,
    DMA 매핑 ioctl (IOMMU 페이지 테이블에 user 메모리를 IOVA로 등록), 리셋(VFIO_DEVICE_RESET → pci_reset_function)
```

- **그룹 전체 요구**: 그룹 안 장치가 하나라도 호스트 드라이버에 붙어 있으면 그룹을 열 수 없다 (격리가 깨지니까) [3].
- **noiommu 모드**: `vfio.enable_unsafe_noiommu_mode=1` — IOMMU 없는 SoC에서 user-space 드라이버용. 모듈 설명 그대로 "no device isolation, no DMA translation, … will taint the kernel" [확인됨: vfio_main.c]. 검증 툴을 user space로 빠르게 만드는 데는 쓸 만하지만 격리는 없음.
- Lima VM 커널: `CONFIG_VFIO=m`, `CONFIG_VFIO_PCI=m`, `CONFIG_VFIO_NOIOMMU=y`, `CONFIG_VFIO_DEVICE_CDEV=y` [실제 출력].

### 2.4 ACS 비트 (Extended Capability ID 0x0D)

| 비트 | 이름 | 하는 일 (포트 기준) |
|---|---|---|
| 0 (`PCI_ACS_SV`) | Source Validation | 들어온 요청의 Requester ID가 그 포트 아래 버스 범위인지 검사 (RID 위조 차단) |
| 1 (`PCI_ACS_TB`) | Translation Blocking | ATS로 "이미 변환됨" 표시된 요청을 차단 |
| 2 (`PCI_ACS_RR`) | P2P Request Redirect | 다른 downstream 포트로 가는 요청을 **위(upstream)로 돌려보냄** |
| 3 (`PCI_ACS_CR`) | P2P Completion Redirect | completion도 위로 |
| 4 (`PCI_ACS_UF`) | Upstream Forwarding | 위로 올라온 요청을 다시 같은 포트로 내려보내지 않고 계속 위로 |
| 5 (`PCI_ACS_EC`) | P2P Egress Control | 포트별 허용/차단 벡터로 세밀 제어 |
| 6 (`PCI_ACS_DT`) | Direct Translated P2P | 변환 완료(ATS) 요청은 redirect 없이 직통 허용 |

- 비트 정의: `pci_regs.h` [1] [확인됨]. Linux 그룹 판단에 쓰는 마스크: `REQ_ACS_FLAGS = SV | RR | CR | UF` (`drivers/iommu/iommu.c`) [4] [확인됨].
- 누가 ACS를 가져야 하나 (`pci_acs_enabled()` 주석, PCIe 3.0 6.12 인용) [4] [확인됨]: **root port·switch downstream port는 단일/멀티펑션 상관없이 ACS로 P2P 능력을 표시해야** 함. endpoint·switch upstream port는 **멀티펑션일 때만** 해당(단일 function이면 ACS 불필요로 간주). conventional PCI·PCIe-to-PCI bridge 아래는 항상 "격리 불가".
- **그룹 알고리즘** (`pci_device_group()`) 요약 [4] [확인됨]: ① DMA alias(같은 RID로 보이는 장치)부터 합침 ② 위로 올라가며 **처음으로 ACS 경로가 켜진 bridge를 만나면 멈춤**, 그 전까지 지나친 bridge가 이미 그룹이 있으면 거기에 합류 ③ 같은 슬롯의 격리 안 된 멀티펑션 형제와 합침. → QEMU에서 e1000e(05:00.0)는 ACS 없는 DP 04:00.0과 같은 그룹 8 (실측).
- 부팅 옵션: `pci=disable_acs_redir=<dev>[;...]` (P2P를 위해 redirect 끔), `pci=config_acs=<flags>@<dev>` (비트별 강제) — `kernel-parameters.txt` [5] [확인됨]. 흔히 보이는 `pcie_acs_override=` 는 **메인라인이 아닌 배포판/커뮤니티 패치** — 격리를 거짓으로 주장하게 만드니 검증·제품에선 금지 [추정].

### 2.5 P2P DMA

- 커널 `drivers/pci/p2pdma.c` / 문서 `driver-api/pci/p2pdma` [6]: provider(BAR 메모리를 내놓는 장치, 예: NVMe CMB)와 client(그 메모리로 DMA하는 장치)의 **거리(distance)** 를 계산.
  - 같은 스위치 아래, 경로에 **ACS redirect 없음** → `PCI_P2PDMA_MAP_BUS_ADDR` (스위치 안에서 직통, 가장 좋음).
  - 경로 어딘가 ACS redirect → root complex 경유 → `PCI_P2PDMA_MAP_THRU_HOST_BRIDGE`, 단 **host bridge가 화이트리스트**에 있어야 (root complex가 P2P 전달을 제대로 하는 칩만) — 아니면 `NOT_SUPPORTED`.
  - 실패 시 dmesg 예: `"ACS redirect is set between the client and provider (%s)"`, `"to disable ACS redirect for this path, add the kernel parameter: pci=disable_acs_redir=%s"` [확인됨: p2pdma.c 문자열].
- sysfs: provider 장치 아래 `p2pmem/{size, available, published, allocate}` [확인됨: p2pdma.c 속성 이름]. QEMU igb/nvme에는 없음(CMB 미설정).
- **왜 스위치가 중요한가**: P2P 성능은 "같은 스위치 아래 + ACS redirect 끔"일 때 최고 → 그런데 그러면 **IOMMU 격리가 깨져 그룹이 합쳐짐**. 격리(ACS on)와 P2P(ACS off)는 트레이드오프.

### 2.6 자동차 맥락 [추정]

- 중앙 컴퓨터(central compute) 하나에 ADAS/자율주행, 인포테인먼트, 계기판, 텔레매틱스를 **하이퍼바이저 위 VM/파티션**으로 나눠 돌리는 구조가 업계 흐름 (예: Volvo가 QNX Hypervisor로 인포테인먼트+계기판을 NVIDIA DRIVE 위에서 공유, 업계 기사) [7] [추정 — Rivian 구조는 공개 정보 없음].
- PCIe가 들어오는 지점: 중앙 SoC ↔ NVMe 저장장치(여러 도메인이 공유 → **SR-IOV NVMe**로 도메인별 VF), ↔ Ethernet 스위치/NIC(SR-IOV NIC VF를 도메인별로), ↔ 가속기/모뎀. 특허 중에는 SR-IOV 중앙 스토리지로 여러 도메인 컨트롤러를 서비스하는 구조도 있다 [8] [추정].
- 검증 관점: **IOMMU group이 도메인 경계와 맞는가** (ADAS VF와 인포테인먼트 VF가 같은 그룹이면 분리 불가), VF FLR이 다른 VF에 영향 없는가, 한 VM의 잘못된 DMA가 SMMU fault로 막히는가(LP08), 한 VF의 에러가 PF/다른 VF를 죽이지 않는가(LP09).
- Rivian 역할(실리콘·보드 검증, bare-metal C)에선 직접 하이퍼바이저를 짜지 않더라도 "SoC의 SMMU/ACS가 이 격리를 HW로 보장하는지 검증"은 범위 안일 수 있음 [추정].

## 3. Linux에서 볼 것

### 3.1 Lima VM (Apple VZ, Ubuntu 7.0.0-38) — IOMMU 없음 [실제 출력]

```bash
$ sudo sh onsitePrep/lp12/sriov_iommu_probe.sh
== IOMMU groups (/sys/kernel/iommu_groups)
   (none: no IOMMU driver bound to the PCI host -> no groups, VFIO needs noiommu mode)
== ACS on bridges/ports (ext cap 0x0D)
== SR-IOV
$ ls /sys/kernel/iommu_groups | wc -l
0
```

- 해석: Apple VZ 게스트는 가상 IOMMU를 주지 않음 → 그룹 0개 → vfio-pci는 noiommu 모드에서만. 장치는 전부 virtio(conventional PCI 형태, PCIe/ACS/SR-IOV cap 없음).

### 3.2 QEMU `virt,iommu=smmuv3` — 그룹·ACS·SR-IOV [실제 출력]

```text
[    0.274847] arm-smmu-v3 9050000.smmuv3: ias 44-bit, oas 44-bit (features 0x01088705)
[    0.279835] pci 0000:00:00.0: Adding to iommu group 0
== IOMMU groups (/sys/kernel/iommu_groups)
   group 0 (DMA): 0000:00:00.0
   group 1 (DMA): 0000:00:01.0
   group 2 (DMA): 0000:00:02.0
   group 3 (DMA): 0000:00:03.0
   group 4 (DMA): 0000:00:04.0
   group 5 (DMA): 0000:01:00.0
   group 6 (DMA): 0000:02:00.0
   group 7 (DMA): 0000:03:00.0
   group 8 (DMA): 0000:04:00.0 0000:05:00.0
   group 9 (DMA): 0000:04:01.0 0000:06:00.0
== ACS on bridges/ports (ext cap 0x0D)
   0000:00:02.0 ACSCap: SV+ TB+ RR+ CR+ UF+ EC- DT+
   0000:00:02.0 ACSCtl: SV+ TB- RR+ CR+ UF+ EC- DT-
   (00:03.0, 00:04.0 동일)
== SR-IOV
   PF 0000:01:00.0 driver=igb totalvfs=7 numvfs=0 offset=128 stride=2 vf_device=10ca
      SR-IOV ecap@0x160: Ctrl=0x0010 (bit0 VF Enable, bit3 VF MSE) TotalVFs=8 NumVFs=0 FirstVFOffset=128 VFStride=2 VFDeviceID=0x10ca
```

| 관찰 | 해석 |
|---|---|
| `type = DMA` | 그룹의 기본 도메인이 변환(Translated) 모드 — dmesg `iommu: Default domain type: Translated` |
| root port 3개 ACSCap `SV TB RR CR UF DT` | QEMU `pcie-root-port` 는 ACS 광고. ACSCtl에서 커널이 **SV RR CR UF**를 켬 (= REQ_ACS_FLAGS), TB·DT는 끔 |
| xio3130 downstream port에는 ACS cap 없음 | `lspci` 에도 `04:00.0: no ACS` |
| group 8 = {04:00.0, 05:00.0} | ACS 없는 DP 아래 e1000e는 DP와 합쳐짐 → e1000e만 VM에 넘길 수 없고 DP까지 vfio에 묶어야 함 |
| totalvfs=7 vs TotalVFs=8 | igb 드라이버가 7로 제한 (§2.2) [9] |
| Ctrl=0x0010 | ARI Capable Hierarchy(bit4)만 켜짐 — 커널이 ARI 가능한 계층에서 설정 |

### 3.3 VF 생성 → vfio-pci 바인딩 [실제 출력]

```text
### enable 2 VFs
sriov_totalvfs=7
enabled 2 VFs on 0000:01:00.0
   group 10 (DMA): 0000:01:10.0
   group 11 (DMA): 0000:01:10.2
   PF 0000:01:00.0 driver=igb totalvfs=7 numvfs=2 offset=128 stride=2 vf_device=10ca
      SR-IOV ecap@0x160: Ctrl=0x0019 ... NumVFs=2 FirstVFOffset=128 VFStride=2 VFDeviceID=0x10ca
      virtfn0 -> 0000:01:10.0 driver=igbvf
      virtfn1 -> 0000:01:10.2 driver=igbvf
           +-02.0-[01]--+-00.0  Intel Corporation 82576 Gigabit Network Connection
           |            +-10.0  Intel Corporation 82576 Virtual Function
           |            \-10.2  Intel Corporation 82576 Virtual Function
[    2.222751] igbvf 0000:01:10.0: Intel(R) 82576 Virtual Function
[    2.225515] igbvf 0000:01:10.2: PF still in reset state. Is the PF interface up?
### bind VF to vfio-pci
before: driver=igbvf iommu_group=10
after : driver=vfio-pci  /dev/vfio: 10 vfio
crw-------    1 0        0         234,   0 Nov  1 05:05 10
crw-rw-rw-    1 0        0          10, 196 Jan  1  1970 vfio
### disable VFs while bound
rc=0
0
```

- 해석:
  - Ctrl 0x0010 → **0x0019** = VF Enable(bit0) + VF MSE(bit3) 켜짐.
  - VF마다 **자기 IOMMU group** (10, 11) — PF 아래 같은 버스지만 VF는 각자 RID가 있고 SR-IOV 장치는 VF 간 P2P를 내부에서 하지 않는다고 간주 (igb는 멀티펑션 ACS 판단 대상 아님) [추정: 커널은 VF를 PF와 별도 그룹으로 두는 것이 일반적, quirk에 따라 다름].
  - `igbvf … PF still in reset state. Is the PF interface up?` — PF 넷 인터페이스를 `ip link set up` 안 해서 생기는 정상 경고 (PF가 VF 메일박스에 응답 안 함).
  - vfio-pci 바인딩 후 **`/dev/vfio/10`** (그룹 번호 = 파일 이름) 생성. 이걸 QEMU `-device vfio-pci,host=01:10.0` 이나 user-space 테스트 툴이 연다.
  - VF가 vfio에 묶여 있어도 `sriov_numvfs=0` 이 성공(rc=0)하고 VF가 사라짐 → 사용 중인 VM이 있으면 위험. 실무에선 순서: VM 정지 → unbind → numvfs=0.

### 3.4 자주 쓰는 명령 모음

```bash
# 그룹 보기 (실제 하드웨어 공통)
for g in /sys/kernel/iommu_groups/*; do echo "group ${g##*/}: $(ls $g/devices)"; done
readlink /sys/bus/pci/devices/0000:01:10.0/iommu_group
lspci -s 00:02.0 -vvv | grep -A2 "Access Control"      # ACSCap / ACSCtl
lspci -s 01:00.0 -vvv | grep -A10 "SR-IOV"             # IOVCap/IOVCtl/Total VFs/offset/stride
echo 4 > /sys/bus/pci/devices/0000:01:00.0/sriov_numvfs
dmesg | grep -i -E "smmu|iommu|DMAR|vfio|p2pdma|ACS"
```

## 4. 꼬리 질문 체인

### 체인 A (LP00): "What's SR-IOV for?" → "Why do IOMMU groups matter for passthrough?" → ACS? → 왜 두 장치가 한 그룹이 되나?

<details><summary>Q. What's SR-IOV for?</summary>

- **A**: "To share one physical device across many VMs or partitions without the hypervisor in the data path. The physical function creates virtual functions — each a real PCIe function with its own routing ID, BARs, queues and MSI-X — and each VM gets a VF directly, so DMA and interrupts go straight between the device and the guest. The PF driver keeps control: how many VFs, MAC addresses, rate limits. The alternative, software emulation like virtio with a backend, costs CPU and latency."
- 한국어 핵심: 하이퍼바이저를 데이터 경로에서 빼기. PF = 관리, VF = 각 VM 직결.

</details>

<details><summary>꼬리1. Why do IOMMU groups matter for passthrough?</summary>

- **A**: "When a VM owns a device, the device's DMA has to be confined to that VM's memory, and the IOMMU enforces that per requester ID. But the IOMMU can only enforce it for traffic that reaches it. If two devices can exchange transactions directly — peers behind a switch without ACS, functions of a multifunction device without isolation, or devices that alias to the same requester ID behind a PCIe-to-PCI bridge — the kernel puts them in one group, and VFIO only lets you assign whole groups. So the group layout tells you what you can safely give to different VMs."
- 한국어 핵심: IOMMU는 자기를 거치는 트래픽만 막음 → 직접 통할 수 있는 장치는 한 그룹 → VFIO는 그룹 단위.

</details>

<details><summary>꼬리2. What does ACS actually do?</summary>

- **A**: "Access Control Services is a set of controls on switch downstream ports and root ports. Source Validation checks that requests coming up from a port carry a requester ID from that port's bus range. Request and Completion Redirect send peer-to-peer traffic up to the root complex instead of across the switch, so the IOMMU sees it. Upstream Forwarding makes sure redirected traffic keeps going up. Linux requires those four — SV, RR, CR, UF — on the path to treat devices below as isolated."
- 한국어 핵심: SV(RID 검사), RR/CR(P2P를 위로), UF(계속 위로) — Linux는 이 4개 요구.

</details>

<details><summary>꼬리3. Two devices you wanted in different VMs ended up in the same group. Why, and what can you do?</summary>

- **A**: "Usually a bridge on the path doesn't support ACS — a switch downstream port, or a root port on a platform without ACS — or it's a multifunction device without ACS between its functions, or a PCIe-to-PCI bridge that makes them alias. I saw the first case in QEMU: an endpoint behind a switch port with no ACS capability landed in that port's group. Fixes are hardware: a switch with ACS, a different slot, or a vendor ACS quirk if the silicon really isolates but doesn't advertise it. The ACS override patch just lies to the kernel, so I wouldn't ship it."
- 한국어 핵심: 경로 bridge의 ACS 부재/멀티펑션/alias. 해결은 HW(ACS 스위치, 슬롯, 검증된 quirk). override 패치는 거짓 격리.

</details>

### 체인 B (추가): "How does a VF get its bus/device/function?" → ARI? → VF BAR?

<details><summary>Q. How does a VF get its BDF?</summary>

- **A**: "From the PF's SR-IOV capability: VF number n has routing ID equal to the PF's routing ID plus First VF Offset plus n minus one times VF Stride. In QEMU's igb model, offset 128 and stride 2 put the PF at 01:00.0 and the first two VFs at 01:10.0 and 01:10.2. Because that can exceed eight functions per device, the hierarchy needs ARI, which reinterprets the device number bits as function bits so you get up to 256 functions."
- 한국어 핵심: RID = PF + Offset + (n−1)×Stride. 8개 넘으면 ARI.

</details>

<details><summary>꼬리1. How are VF BARs assigned?</summary>

- **A**: "The PF's SR-IOV capability has VF BAR registers that describe the size of each VF's BAR; the actual region is that size times NumVFs, laid out contiguously, and each VF's BAR is at base plus its index times the size. So the OS must reserve the whole window before enabling VFs — a classic failure on embedded platforms is that the PCIe host's memory window is too small for the VF BAR aperture and enabling VFs fails with a resource error."
- 한국어 핵심: VF BAR 크기 × NumVFs 연속 창, 호스트 메모리 창 부족이면 VF enable 실패. [추정: 실패 메시지 형태]

</details>

### 체인 C (추가): "Can a GPU DMA directly into an NVMe drive's memory?" → 무엇이 막나?

<details><summary>Q. Can one PCIe device DMA directly into another's BAR?</summary>

- **A**: "Yes, that's peer-to-peer DMA — for example writing into an NVMe controller memory buffer from a NIC or GPU. Linux has a p2pdma framework that computes the path between provider and client. It's best when both are under the same switch and no port on the path has ACS redirect set, so the switch routes it directly. If the path goes through the root complex, it only works when the host bridge is on a whitelist of root complexes known to forward peer traffic."
- 한국어 핵심: p2pdma. 같은 스위치 + ACS redirect 없음이 최선, RC 경유는 화이트리스트 필요.

</details>

<details><summary>꼬리1. So what's the trade-off with ACS?</summary>

- **A**: "Isolation versus peer performance. ACS redirect forces peer traffic up through the IOMMU, which is what makes per-device isolation possible, but it adds latency and uses root complex bandwidth, or breaks P2P entirely. Turning it off with pci=disable_acs_redir gives you direct P2P but merges those devices into one isolation domain. On a car's central compute you'd decide per link which matters more — a camera-to-accelerator path inside one safety domain might want P2P, while anything crossing a domain boundary needs ACS."
- 한국어 핵심: ACS on = 격리, off = P2P 성능. 도메인 경계를 넘는 경로는 ACS 필수. [추정: 차량 예]

</details>

### 체인 D (추가, 자동차): "How would you partition PCIe devices between an ADAS VM and an infotainment VM?"

<details><summary>Q. How would you split PCIe devices between a safety domain and an infotainment domain on one SoC?</summary>

- **A**: "Start from the IOMMU groups, because that's the hardware isolation boundary: devices for different domains must be in different groups, which means ACS on the root ports and switch ports between them. For shared devices like storage or a NIC, use SR-IOV and give each domain a VF, with the PF owned by the hypervisor or a service partition. Then validate it: one domain's VF can't DMA outside its memory — the SMMU should fault — a VF reset or error doesn't take down the other domain's VF, and bandwidth under contention meets the safety domain's requirement."
- 한국어 핵심: 그룹 = HW 격리 경계 → 도메인별 다른 그룹(ACS) → 공유 장치는 SR-IOV VF → 검증: SMMU fault, VF 리셋/에러 격리, 대역폭. [추정]

</details>

## 5. Don 경험 연결

- "At Solidigm I worked on the PCIe/NVMe subsystem IP of a Gen6 SSD controller. Data-center NVMe drives are where SR-IOV and multiple functions show up, so I'm familiar with the device-side view: VF configuration space, per-function reset, and per-function queues and interrupts. [숫자 채우기: SR-IOV/멀티펑션을 실제로 다뤘는지 — 아니면 이 문장은 'familiar with the requirements'로 낮추기]"
- "My validation experience is more on the link and SoC side than on hypervisors, but on a central compute the IOMMU groups are the hardware isolation boundary. I'd verify isolation the same way I verify any IP: inject a bad DMA from one domain and make sure the SMMU faults, and reset one VF while the others carry traffic."
- 주의: VFIO/하이퍼바이저 실무 경험이 레쥬메에 없음 → "I've studied/ran it in QEMU" 수준으로 정직하게.

## 6. 실습 / 코드

- [onsitePrep/lp12/sriov_iommu_probe.sh](onsitePrep/lp12/sriov_iommu_probe.sh) — POSIX sh. 모드: 보고만 / `enable <PF> <n>` / `vfio <BDF>`. **실행 확인 ✅**:
  - QEMU 10.2 `-machine virt,iommu=smmuv3` + `pcie-root-port` + `igb`(SR-IOV 8 VF 에뮬레이션) + xio3130 스위치, 랩 커널 6.18.55 (igb, igbvf, vfio-pci, arm-smmu-v3 built-in) → §3.2, §3.3 출력.
  - Lima VM (`sudo sh …/sriov_iommu_probe.sh`) → §3.1 출력 (그룹 없음).
- QEMU 실행 명령은 [LP10 §6.1](2026-10-31_LP10_power_mgmt_aspm.html)과 동일 (`LAB_CMD=sh~/run12.sh`).
- **[미실행]** 다음 단계: VF를 vfio-pci에 묶은 뒤 nested QEMU 게스트에 `-device vfio-pci,host=01:10.0` 로 패스스루 (TCG 안의 TCG라 매우 느림), 또는 작은 C 프로그램으로 `/dev/vfio/10` 열어 `VFIO_GROUP_GET_DEVICE_FD` → BAR mmap → config 읽기. P2P는 QEMU nvme `cmb_size_mb=` 로 CMB를 만들면 `p2pmem/` 이 생길 가능성 [추정].

## 7. 자주 틀리는 것

1. **"VF는 PF와 같은 BDF를 공유"** ❌ — VF마다 고유 RID (offset/stride 계산), 그래서 IOMMU가 VF별로 격리 가능.
2. **"sriov_totalvfs = HW TotalVFs"** ❌ — 드라이버가 줄일 수 있음 (igb 7 vs 8 실측).
3. **"IOMMU만 있으면 장치 하나씩 넘길 수 있다"** ❌ — ACS 없는 경로면 그룹이 합쳐져 통째로만.
4. **ACS 비트를 "보안 기능이라 켜면 좋다"로만 이해** — P2P 성능/동작과 반대 방향. 경로별 결정.
5. **`pcie_acs_override` 를 해결책으로 말하기** ❌ — 메인라인 아님, 격리를 거짓 보고.
6. **vfio에 묶인 VF가 있어도 numvfs=0은 막힌다** ❌ — 실측상 성공하고 VF가 사라짐 → 운용 순서 주의.
7. **noiommu VFIO = 안전한 user-space 드라이버** ❌ — 격리 없음, 커널 taint.
8. **VF config에서 Vendor/Device ID를 읽으면 된다** ❌ — 0xFFFF. ID는 PF SR-IOV cap의 VF Device ID.
9. **"P2P는 항상 root complex를 거친다"** ❌ — 같은 스위치 아래 + ACS redirect off면 스위치 안에서 직통 (가장 빠름).

## 8. 출처

1. Linux `include/uapi/linux/pci_regs.h` (`PCI_SRIOV_*`, `PCI_ACS_*`, ext cap IDs) — v6.18.55 소스 grep: https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
2. Linux PCI IOV HOWTO + sysfs ABI (`sriov_*`): https://docs.kernel.org/PCI/pci-iov-howto.html , https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-bus-pci
3. Linux VFIO 문서 (groups, containers, device cdev): https://docs.kernel.org/driver-api/vfio.html
4. Linux `drivers/iommu/iommu.c` (`pci_device_group`, `REQ_ACS_FLAGS`), `drivers/pci/pci.c` (`pci_acs_enabled`): https://github.com/torvalds/linux/blob/master/drivers/iommu/iommu.c , https://github.com/torvalds/linux/blob/master/drivers/pci/pci.c
5. Linux kernel parameters (`pci=disable_acs_redir=`, `pci=config_acs=`): https://docs.kernel.org/admin-guide/kernel-parameters.html
6. Linux P2PDMA 문서 및 `drivers/pci/p2pdma.c`: https://docs.kernel.org/driver-api/pci/p2pdma.html , https://github.com/torvalds/linux/blob/master/drivers/pci/p2pdma.c
7. Promwad, "Hypervisors in Software-Defined Vehicles" / "Secure Partitioning in Automotive Hypervisors" (업계 기사, 참고 수준): https://promwad.com/news/hypervisors-in-software-defined-vehicles-partitioning-compute , https://promwad.com/news/secure-partitioning-in-automotive-hypervisors-central-compute
8. US patent 12306780 "Centralized storage device, in-vehicle electronic system…" (SR-IOV 중앙 스토리지, 참고 수준): https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12306780
9. igb 드라이버 `drivers/net/ethernet/intel/igb/igb_main.c` (`pci_sriov_set_totalvfs(pdev, 7)`): https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/igb/igb_main.c

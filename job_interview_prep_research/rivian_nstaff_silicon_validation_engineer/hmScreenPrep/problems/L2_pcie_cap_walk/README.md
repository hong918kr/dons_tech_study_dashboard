# L2 · PCIe Config Space Check — Capability Walk, Link Status, SR-IOV, BAR Sizing

> **ID**: L2 · **Priority**: P0 · **Pattern**: linked-list walk with loop guard + bitfield decode + write-ones/readback/restore · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh L2` / `./run.sh L2 sol`

## 0. 문제 한국어로 먼저 보기

> 영어 원문(§1)을 읽기 전에 **무슨 상황이고 뭘 만들어야 하는지**만 빠르게 잡는 요약입니다. 풀이 힌트는 없습니다.

**한 줄 요약**: enumeration이 끝난 뒤 PCIe switch 뒤의 endpoint를 하나씩 검사하는 **bring-up / system-validation 도구**(pass/fail을 내는 `lspci -vvv`)의 핵심 함수들을 짭니다.

**상황**
- 장치마다 4 KiB config space가 있고, `cfg_rd32(rid, off)` / `cfg_wr32(rid, off, val)`로만 접근합니다. **4바이트 정렬된 32-bit 접근만** 됩니다. 8/16-bit 필드는 dword를 읽어서 꺼내야 합니다.
- `rid`(Requester ID) = `bus << 8 | dev << 3 | fn`. 없는 장치는 **all-ones(0xFFFFFFFF)** 를 돌려줍니다.
- 파일 아래쪽 **fake**가 장치 6개(+ 비어 있는 주소 1개)를 흉내 냅니다. 쓰기 가능 비트 mask, **RW1C**(1을 쓰면 지워지는) Status 에러 비트, 크기 mask를 돌려주는 BAR까지 모델링합니다.
  - `00:01.0` switch downstream port (Type 1 header)
  - `01:00.0` 정상 NVMe SSD (Gen4 x4)
  - `02:00.0` 같은 SSD인데 link가 낮게 잡힘, AER correctable 에러 기록됨
  - `03:00.0` capability list가 깨진 NIC (pointer 하위 비트 쓰레기, 순환)
  - `04:00.0` SR-IOV PF 가속기 (Gen5 x8, 8 GiB BAR)
  - `05:00.0` multi-function 가속기 (BAR 3종, Status에 에러 비트 기록됨)
  - `06:00.0` 없음

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `pci_probe(rid)` | 장치 존재 + header 종류 확인 | Vendor ID `0xFFFF` → `-ENODEV`. Header Type `[6:0]` ≠ 0 → `-ENOTSUP`. 아니면 0 |
| `pci_find_cap(rid, id)` | standard capability list에서 ID 찾기 | Status bit 4(Capabilities List)가 1일 때만. `0x34`의 byte에서 시작, pointer 하위 2비트 mask. pointer < `0x40` → `-EINVAL`, 48 hop 초과 → `-ELOOP`, 끝까지 없음 → `-ENOENT`. 찾으면 offset 반환 |
| `pci_find_ext_cap(rid, id)` | extended capability list (`0x100`부터) | header = ID`[15:0]` · ver`[19:16]` · next`[31:20]`. `0x100`의 header가 0 또는 `0xFFFFFFFF` → `-ENOENT`, 중간에서 그러면 `-EINVAL`. next 0 → 끝(`-ENOENT`), 0 < next < `0x100` → `-EINVAL`, 480 hop 초과 → `-ELOOP` |
| `pci_link_check(rid, li)` | link 속도/폭이 최대치인지 | PCI Express cap(ID `0x10`)의 Link Capabilities(`+0x0C`: max speed `[3:0]`, max width `[9:4]`)와 Link Status(`+0x12`, 16-bit: 현재 speed `[3:0]`, width `[9:4]`) 비교. `LinkInfo` 채우고 `LINK_SPEED_DOWN \| LINK_WIDTH_DOWN` flag 반환(0 = 정상). 현재 speed나 width가 0 → `-ENOLINK`. cap 못 찾으면 그 에러 그대로 |
| `pcie_speed_mts(code)` | speed code → MT/s | 1..6 → 2500, 5000, 8000, 16000, 32000, 64000. 나머지 0 |
| `pci_sriov_vfs(pf, sub_bus, si, vf_rid, max)` | SR-IOV VF들의 RID 계산 | SR-IOV ext cap(ID `0x0010`): TotalVFs `+0x0E`, NumVFs `+0x10`, First VF Offset `+0x14`, VF Stride `+0x16`. VF n(1부터)의 RID = PF RID + offset + (n−1) × stride. NumVFs > TotalVFs → `-EINVAL`. 어떤 VF의 bus가 `sub_bus`(위 bridge의 subordinate bus) 초과 → `-ERANGE`. 성공 시 NumVFs 반환, `vf_rid[]`에 최대 `max`개 |
| `pci_aer_status(rid, uncor, cor)` | AER 에러 상태 읽기 | AER ext cap(ID `0x0001`): Uncorrectable Status `+0x04`, Correctable Status `+0x10`. 하나라도 0이 아니면 1, 깨끗하면 0, AER 없으면 `-ENOENT` |
| `pci_size_bars(rid, bar)` | BAR0..5 크기 재기 | BAR마다 저장 → `0xFFFFFFFF` 쓰기 → 읽기 → 복원. 64-bit BAR는 2개가 한 쌍. 재는 동안 Command의 IO/MEM decode(bit 1:0)를 끄고 끝나면 복원. 구현된 BAR 개수 반환(64-bit 쌍 = 1개). BAR5가 64-bit라고 하면 `-EINVAL`. **끝난 뒤 config space가 전과 완전히 같아야 함** |

**예시를 말로 풀면**
- **`01:00.0` 정상 SSD**: `0x34` → `0x40`(Power Mgmt) → `0x50`(MSI-X) → `0x70`(PCI Express). PCIe cap은 `0x70`. extended는 `0x100`(AER) → `0x148`(Serial No.) → `0x158`(Secondary PCIe). Link Capabilities `0x00400C44`, Link Status `0x1044` → 둘 다 Gen4(16000 MT/s) x4 → flag 0.
- **`02:00.0`**: 능력은 Gen4 x4인데 Link Status가 `0x1022` = Gen2 x2 → speed와 width 둘 다 down(flag 3). AER Correctable Status `0x41`(Receiver Error + Bad TLP) → 1.
- **`03:00.0`**: `0x40`의 next가 `0x53`(하위 2비트 쓰레기) → mask하면 `0x50`(MSI). `0x50`의 next가 다시 `0x40` → PCIe cap을 찾으면 끝없이 돎 → `-ELOOP`.
- **`04:00.0` SR-IOV PF**: NumVFs 8, offset `0x80`, stride 2 → VF RID `0x0480, 0x0482, …, 0x048E` = `04:10.0`, `04:10.2`, … `04:11.6`. offset이 `0xF8`이면 뒤쪽 VF가 bus 5로 넘어감 → bridge가 bus 4까지만 덮으면 `-ERANGE`.
- **`05:00.0` BAR**: 64-bit prefetchable 16 MiB, 32-bit 4 KiB, I/O 256 B → 3개. 재고 나서 Command `0x0007`과 Status의 에러 비트가 그대로 남아 있어야 함.

**지켜야 할 조건**
- C11, `malloc` 금지. config 접근은 `cfg_rd32`/`cfg_wr32`로만, **정렬된 dword만** (fake가 비정렬 접근을 셉니다).
- `pci_probe`를 뺀 함수들은 장치가 `pci_probe`를 통과했다고 가정해도 됩니다.
- 어떤 입력에서도 무한 루프 금지.

## 1. Problem

Your validation tool runs after PCIe enumeration and checks every endpoint behind a switch: is it there, are its capability lists sane, did its link train to full speed and width, how are its SR-IOV VFs laid out, are AER errors logged, and how big are its BARs. Think of it as an `lspci -vvv` that returns pass/fail and can run before Linux exists.

Config space is reached only through two accessors. Each function has a 4 KiB little-endian config space; only **aligned 32-bit** accesses exist, so 8/16-bit fields are extracted from dwords. A missing function reads all-ones. The functions are a **fake** at the bottom of `stub.c` that models writable-bit masks, RW1C Status error bits, and BARs that read back their size mask.

```c
uint32_t cfg_rd32(uint16_t rid, uint16_t off);              /* off 4-aligned, < 4096 */
void     cfg_wr32(uint16_t rid, uint16_t off, uint32_t val);
#define RID(b, d, f) ((uint16_t)((b) << 8 | (d) << 3 | (f)))

typedef struct { uint8_t max_speed, max_width, cur_speed, cur_width; } LinkInfo;
#define LINK_SPEED_DOWN 1
#define LINK_WIDTH_DOWN 2

typedef struct {
    uint16_t total_vfs, num_vfs, first_off, stride;
    uint32_t first_rid, last_rid;          /* 32-bit on purpose: may exceed 0xFFFF */
} SriovInfo;

#define BAR_IO 1u  /* BAR_MEM64 2u, BAR_PREF 4u, BAR_UPPER 8u (upper half of a 64-bit BAR) */
typedef struct { uint64_t base, size; uint32_t flags; } PciBar;

int      pci_probe(uint16_t rid);
int      pci_find_cap(uint16_t rid, uint8_t id);            /* offset or -errno */
int      pci_find_ext_cap(uint16_t rid, uint16_t id);       /* offset or -errno */
int      pci_link_check(uint16_t rid, LinkInfo *li);        /* DOWN flags or -errno */
uint32_t pcie_speed_mts(uint8_t code);
int      pci_sriov_vfs(uint16_t pf, uint8_t sub_bus, SriovInfo *si, uint16_t *vf_rid, int max);
int      pci_aer_status(uint16_t rid, uint32_t *uncor, uint32_t *cor);
int      pci_size_bars(uint16_t rid, PciBar bar[6]);        /* # implemented BARs or -errno */
```

Requirements:

- **`pci_probe`**: Vendor ID `0xFFFF` → `-ENODEV`. Header Type `[6:0]` (byte `0x0E`, bit 7 is the multi-function flag) not 0 → `-ENOTSUP`. Else 0.
- **`pci_find_cap`**: only if Status bit 4 (Capabilities List) is set, else `-ENOENT`. Start at the byte at `0x34`; mask the low 2 bits of every pointer; a pointer below `0x40` → `-EINVAL`; more than 48 hops → `-ELOOP`; next = 0 → `-ENOENT`. Cap header: ID `[7:0]`, next `[15:8]`.
- **`pci_find_ext_cap`**: start at `0x100`. Header: ID `[15:0]`, version `[19:16]`, next `[31:20]`. A header of 0 or `0xFFFFFFFF` at `0x100` → `-ENOENT` (no extended caps); later in the list → `-EINVAL`. next = 0 → `-ENOENT`; `0 < next < 0x100` → `-EINVAL`; more than 480 hops → `-ELOOP`.
- **`pci_link_check`**: in the PCI Express capability (ID `0x10`) read Link Capabilities (`+0x0C`: max speed `[3:0]`, max width `[9:4]`) and Link Status (`+0x12`, 16-bit: current speed `[3:0]`, negotiated width `[9:4]`). Fill `LinkInfo`; return `LINK_SPEED_DOWN | LINK_WIDTH_DOWN` as applicable (0 = full speed and width); current speed or width 0 → `-ENOLINK`; propagate the `pci_find_cap` error.
- **`pcie_speed_mts`**: codes 1..6 → 2500, 5000, 8000, 16000, 32000, 64000 MT/s; anything else → 0.
- **`pci_sriov_vfs`**: SR-IOV ext cap (ID `0x0010`): TotalVFs `+0x0E`, NumVFs `+0x10`, First VF Offset `+0x14`, VF Stride `+0x16`. VF *n* (1-based) has RID = PF RID + offset + (n−1)·stride. Fill `si` (also on `-EINVAL`/`-ERANGE`; `first_rid = last_rid = 0` when NumVFs is 0). NumVFs > TotalVFs → `-EINVAL`. Any VF bus number > `sub_bus` (the subordinate bus of the bridge above, so also > 255) → `-ERANGE`. Otherwise return NumVFs and write the first `min(NumVFs, max)` RIDs to `vf_rid`.
- **`pci_aer_status`**: AER ext cap (ID `0x0001`): Uncorrectable Error Status `+0x04`, Correctable Error Status `+0x10`. Return 1 if either is non-zero, 0 if both are clean, `-ENOENT` without AER.
- **`pci_size_bars`**: for BAR0..5 (`0x10`..`0x24`): save, write `0xFFFFFFFF`, read back, restore. Memory BAR: type `[2:1]` (`10b` = 64-bit, the next BAR is its upper half), prefetchable `[3]`; I/O BAR: bit 0 = 1, address from bit 2. Turn **memory and I/O decode off** (Command bits 1:0) while sizing and restore Command afterwards. Fill `bar[6]` (unimplemented BAR → all zero; upper half of a 64-bit BAR → `flags = BAR_UPPER`, size 0). Return the number of implemented BARs (a 64-bit pair counts once), `-EINVAL` if BAR5 claims to be 64-bit. **Config space must be bit-for-bit unchanged afterwards.**

### Example

```
01:00.0  0x34 -> 0x40 PM (0x00035001) -> 0x50 MSI-X (0x001F7011) -> 0x70 PCIe (0x00020010)
         pci_find_cap(0x10) = 0x70, pci_find_cap(0x05) = -ENOENT
         ext: 0x100 AER (0x14820001) -> 0x148 DSN (0x15810003) -> 0x158 SecPCIe (0x00010019)
         LnkCap 0x00400C44, LnkSta 0x1044 -> Gen4 x4 of Gen4 x4 -> 0
         BAR0 0xA0000004 -> 64-bit, 16 KiB (0x4000); returns 1
02:00.0  LnkSta 0x1022 -> Gen2 x2 of Gen4 x4 -> SPEED_DOWN | WIDTH_DOWN (3); AER cor = 0x41 -> 1
03:00.0  0x40 (next 0x53 -> 0x50) -> 0x50 MSI (next 0x40) -> ... find_cap(0x10) = -ELOOP, find_cap(0x05) = 0x50
04:00.0  NumVFs 8, offset 0x80, stride 2, sub_bus 4 -> 8 VFs 0x0480 .. 0x048E (04:10.0 .. 04:11.6)
         offset 0xF8 -> last VF 0x0506 on bus 5 -> -ERANGE
05:00.0  BAR0/1 64-bit pref 16 MiB @ 0x40_0100_0000, BAR2 4 KiB @ 0x9010_0000, BAR4 I/O 256 B @ 0x2000 -> 3
06:00.0  pci_probe = -ENODEV;   00:01.0 (Type 1) pci_probe = -ENOTSUP
```

### Constraints

- C11, no malloc. Config access only through `cfg_rd32`/`cfg_wr32`, aligned dwords only.
- Functions other than `pci_probe` may assume the device passed `pci_probe`.
- No input may cause an infinite loop.

### Clarifying questions to ask (first 3 minutes)

1. Is config access through ECAM (4 KiB per function) or legacy CF8/CFC (256 B, no extended caps)? Are byte/word accesses allowed?
2. Does the tool only report, or may it retrain a downtrained link / clear AER status?
3. Do we compare against the device's own Link Capabilities, or against a golden topology table (expected speed/width per slot)?
4. Is anything else using the device while we size BARs (DMA in flight, a driver bound)? Decode off is not safe on a live device.
5. Has firmware already set NumVFs and VF Enable, or should the tool enable SR-IOV itself?
6. Can devices be hot-removed or surprise-down during the check?

## 2. Your workspace

`./run.sh L2`로 바로 테스트하세요. 테스트 6묶음(CHECK 68개)이 있고, 파일 아래쪽 fake 장치들은 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 8/16-bit 필드는 dword를 읽고 shift</summary>

`cfg_rd16(rid, off) = cfg_rd32(rid, off & ~3) >> (8 * (off & 2))`, `cfg_rd8`은 `(off & 3)`로 같은 방식입니다. Status는 `0x04` dword의 위 16비트, Cap Ptr는 `0x34` dword의 byte 0, Link Status(`+0x12`)는 `cap + 0x10` dword의 위 16비트입니다. 이 helper 두 개를 먼저 만들면 나머지가 다 짧아집니다.

</details>

<details>
<summary>Hint 2 — 연결 리스트는 hop 수로 막는다</summary>

순환 검출에 visited 비트맵이나 Floyd 알고리즘까지 필요 없습니다. standard 영역에 들어갈 수 있는 cap은 최대 (256 − 64) / 4 = 48개, extended는 (4096 − 256) / 8 = 480개입니다. 그보다 많이 돌았다면 순환입니다(Linux `PCI_FIND_CAP_TTL`과 같은 방식). 각 hop에서 pointer 하위 2비트 mask → 범위 검사 → 읽기 순서를 지킵니다.

</details>

<details>
<summary>Hint 3 — BAR 크기는 "가장 낮은 쓰기 가능 비트"</summary>

all-ones를 쓰고 읽은 값에서 type 비트(메모리 `[3:0]`, I/O `[1:0]`)를 지운 mask `m`에 대해 `size = m & (~m + 1)`입니다. 64-bit BAR는 위 dword mask를 붙인 64-bit 값으로 계산합니다(4 GiB 이상이면 아래 dword mask는 0). Command는 **16-bit 값만** 써서 decode를 끄세요. dword 전체를 다시 쓰면 위 16비트 Status의 RW1C 에러 비트가 지워집니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — config space는 두 개의 연결 리스트

config space는 앞 64 B 고정 header 뒤에 **연결 리스트 두 개**가 있습니다. standard 리스트는 `0x34`에서 시작하는 8-bit pointer 체인(`0x40`–`0xFF`), extended 리스트는 `0x100`에서 시작하는 12-bit pointer 체인입니다. 기능(PCIe, MSI-X, AER, SR-IOV …)은 이 리스트를 따라가서 ID로 찾습니다. 고정 offset을 가정하면 장치마다 깨집니다.

<div class="chart" data-src="chart1.svg" data-caption="01:00.0의 두 capability 리스트. 각 dword에서 ID와 next를 꺼내 따라갑니다. 03:00.0처럼 next가 앞을 가리키면 끝나지 않으므로 hop 상한(48 / 480)으로 -ELOOP를 냅니다"></div>

검사 도구는 HW가 틀렸을 수 있다는 전제로 짭니다. 그래서 리스트 walk에도 세 가지 방어가 들어갑니다.
- **mask**: pointer 하위 2비트는 reserved. 장치가 쓰레기를 넣어도 SW가 지웁니다(`0x53` → `0x50`).
- **범위**: standard pointer가 header 안(`< 0x40`)을 가리키거나 extended next가 `0x100` 아래면 깨진 리스트입니다.
- **hop 상한**: 순환을 무조건 끝냅니다.

### 4.2 BAR sizing — write ones, read back, restore

BAR에서 주소 비트 중 **장치가 쓰기를 허용하는 비트**만 1로 읽힙니다. 크기가 2^k이면 bit k 아래 주소 비트는 0으로 고정되어 있으므로, 쓰기 가능한 가장 낮은 비트가 곧 크기입니다.

<div class="chart" data-src="chart2.svg" data-caption="BAR2는 bit 12부터 쓰기 가능 → 4 KiB. I/O BAR4는 위 16비트가 0으로 고정돼 '~mask + 1'이 0xFFFF0100이 되지만, 가장 낮은 1 비트(bit 8)는 정확히 256 B. 8 GiB BAR는 아래 dword mask가 0이라 위 dword까지 합쳐야 보입니다"></div>

- **decode off**: all-ones를 쓰는 순간 BAR가 잠깐 `0xFFFFF000` 같은 엉뚱한 주소를 decode합니다. 그 사이 다른 master의 접근이 이 장치로 잘못 갈 수 있어서 Command의 IO/MEM을 먼저 끕니다(Linux `__pci_read_base`도 같은 일을 합니다).
- **16-bit Command 쓰기**: `0x04` dword의 위 절반은 Status이고 에러 비트가 RW1C입니다. 읽은 dword를 그대로 다시 쓰면 `05:00.0`의 Received Master Abort 기록(`0x2000`)이 사라집니다. 검사 도구가 증거를 지우는 셈입니다. → [V2](../V2_reg_field_rmw/index.html)의 W1C RMW 함정과 같은 이야기입니다.

### 4.3 손으로 따라가기 (trace)

`pci_find_cap(01:00.0, 0x10)`:

| hop | 읽는 곳 | dword | ID | next (mask 후) | 동작 |
|---|---|---|---|---|---|
| – | `0x04` | `0x00100006` | – | – | Status = `0x0010`, bit 4 = 1 → 진행 |
| – | `0x34` | `0x00000040` | – | `0x40` | 시작 pointer |
| 0 | `0x40` | `0x00035001` | `0x01` (PM) | `0x50` | 다음 |
| 1 | `0x50` | `0x001F7011` | `0x11` (MSI-X) | `0x70` | 다음 |
| 2 | `0x70` | `0x00020010` | `0x10` (PCIe) | `0x00` | 일치 → **return `0x70`** |

`pci_find_ext_cap(01:00.0, 0x0019)`:

| hop | pos | header | ID | ver | next = `(h >> 20) & 0xFFC` |
|---|---|---|---|---|---|
| 0 | `0x100` | `0x14820001` | `0x0001` AER | 2 | `0x148` |
| 1 | `0x148` | `0x15810003` | `0x0003` DSN | 1 | `0x158` |
| 2 | `0x158` | `0x00010019` | `0x0019` | 1 | 일치 → **return `0x158`** |

`pci_link_check` (PCIe cap `0x70`, LnkCap = dword `0x7C`, LnkSta = dword `0x80` 위 16비트):

| 장치 | LnkCap | max | LnkSta | 현재 | 반환 |
|---|---|---|---|---|---|
| `01:00.0` | `0x00400C44` | speed 4, width `0x44 >> 4` = 4 | `0x1044` | Gen4 (16000 MT/s) x4 | 0 |
| `02:00.0` | `0x00400C44` | Gen4 x4 | `0x1022` | Gen2 (5000 MT/s) x2 | 3 (SPEED \| WIDTH) |
| `02:00.0` 패치 | 〃 | 〃 | `0x1024` | Gen4 x2 | 2 (WIDTH) |
| `02:00.0` 패치 | 〃 | 〃 | `0x0000` | speed 0, width 0 | `-ENOLINK` |

bit 12(`0x1000`, Slot Clock Configuration)가 Link Status에 같이 들어 있으니 `[3:0]`, `[9:4]` mask가 꼭 필요합니다.

`pci_sriov_vfs(04:00.0, …)` (SR-IOV cap `0x160`, TotalVFs 16):

| 경우 | offset / stride | first_rid | last_rid | sub_bus | 반환 |
|---|---|---|---|---|---|
| 기본 | `0x80` / 2 | `0x0480` (04:10.0) | `0x048E` (04:11.6) | 4 | **8**, `vf[4]` = `0x0488` (04:11.0) |
| offset 패치 | `0xF8` / 2 | `0x04F8` (04:1f.0) | `0x0506` (05:00.6) | 4 | `-ERANGE` (VF5–VF8이 bus 5) |
| 〃 | 〃 | 〃 | 〃 | 5 | 8 |
| offset 패치 | `0xFC80` / 2 | `0x10080` | `0x1008E` | 255 | `-ERANGE` (bus 256) |
| NumVFs 17 | – | – | – | – | `-EINVAL` (> TotalVFs 16) |
| NumVFs 0 | – | 0 | 0 | 4 | 0 |

`0xFC80`줄이 `uint16_t` 함정입니다. 16-bit로 더하면 `0x0400 + 0xFC80` = `0x10080`이 `0x0080`(00:10.0)으로 감겨 **bus 0의 엉뚱한 장치**처럼 보이고 검사를 통과해 버립니다.

`pci_size_bars(05:00.0)` (Command `0x0007` → 재는 동안 `0x0004` → 복원 `0x0007`):

| BAR | 원래 값 | all-ones 후 읽기 | mask | size | 결과 |
|---|---|---|---|---|---|
| 0 | `0x0100000C` | `0xFF00000C` | 위와 합쳐 `0xFFFFFFFF_FF000000` | `0x01000000` (16 MiB) | MEM64 \| PREF, base `0x40_0100_0000` |
| 1 | `0x00000040` | `0xFFFFFFFF` | (BAR0의 위 절반) | – | `BAR_UPPER` |
| 2 | `0x90100000` | `0xFFFFF000` | `0xFFFFF000` | `0x1000` (4 KiB) | 32-bit, base `0x9010_0000` |
| 3 | `0x00000000` | `0x00000000` | 0 | – | 미구현 |
| 4 | `0x00002001` | `0x0000FF01` | `0x0000FF00` | `0x100` (256 B) | I/O, base `0x2000` |
| 5 | `0x00000000` | `0x00000000` | 0 | – | 미구현 |

→ 반환 **3**, config space 4 KiB 전체가 전과 같음, `decode_on_sizing` 0. `04:00.0`의 BAR0/1은 lo `0x0000000C`(mask 0) + hi `0xFFFFFFFE` → **8 GiB**(`0x2_0000_0000`), base `0x60_0000_0000`, 반환 1.

AER: `02:00.0` Correctable `0x00000041`(bit 0 Receiver Error, bit 6 Bad TLP) → 1. `04:00.0` Uncorrectable `0x00004000`(bit 14 Completion Timeout) → 1. `01:00.0`은 둘 다 0 → 0.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `cfg_rd16`/`cfg_rd8` helper 두 개로 모든 sub-dword 필드를 처리합니다. 실제 HW에서도 일부 root complex는 byte/word config 접근을 지원하지 않아서 이 패턴이 그대로 쓰입니다.
- `pci_find_cap`의 루프 순서: hop 상한 → 범위 검사 → 읽기. 범위 검사를 읽기 뒤에 두면 `0x3C` 같은 header 안 주소를 이미 읽은 뒤입니다(읽기에 부작용이 있는 레지스터면 사고).
- `pci_find_ext_cap`에서 `0x100`의 0 / all-ones는 "없음"(conventional PCI, CF8/CFC 접근, 없는 장치), 중간의 all-ones는 "깨짐 또는 장치가 사라짐"으로 구분합니다.
- SR-IOV RID는 `uint32_t`로 계산합니다. RID는 stride ≥ 0이라 단조 증가하므로 **마지막 VF만** bus 범위를 보면 됩니다.
- `pci_size_bars`는 BAR를 하나도 안 건너뛰고 6개 모두 4회씩 접근 + Command 3회 = config 접근 **27회**입니다(`01:00.0`, `05:00.0` 모두). 64-bit BAR의 위 절반은 쌍으로 처리하고 `i++`로 넘어갑니다.

### 4.5 복잡도

- `pci_find_cap` ≤ 48 hop, `pci_find_ext_cap` ≤ 480 hop, hop당 config read 1회. `pci_link_check`는 정상 장치에서 config read 7회.
- `pci_size_bars`: 27회 고정. 실제 HW에서 config read는 non-posted TLP라 수 µs 걸리므로, 장치당 전체 검사는 수백 µs 수준입니다. 추가 메모리 O(1).

---

## 5. 임베디드 관점으로 말할 것

- **Enumeration(DFS bus numbering)**: root port에서 시작해 깊이 우선으로 bus 번호를 매깁니다. 각 bridge에 primary / secondary / subordinate bus를 쓰고, 아래를 다 돌고 나서 subordinate를 확정합니다. **switch = upstream port 1개 + 내부 virtual bus + downstream port N개**이고, port마다 Type 1 header(virtual PCI-PCI bridge)입니다. 그래서 switch 하나가 bus 번호를 최소 2개 먹고, SR-IOV VF가 다른 bus로 넘어가면 그 bus까지 subordinate 범위에 들어가야 합니다(이 문제의 `sub_bus` 검사).
- **ECAM 주소**: `base + (bus << 20 | dev << 15 | fn << 12 | off)`. 장치가 없으면 root complex가 UR로 끝내고 all-ones를 돌려줍니다. 그래서 `0xFFFF` = absent입니다. 리셋 직후에는 Vendor ID `0x0001`(CRS/RRS)이 올 수 있고, 그동안 재시도합니다.
- **LTSSM과 downtrain**: Detect → Polling → Configuration → L0, 속도 올리기는 Recovery를 거칩니다. Gen3부터는 equalization(Phase 0–3, preset/coefficient 협상)이 있습니다. 속도가 떨어지는 원인은 signal integrity(insertion loss, crosstalk, connector/via), refclk(SSC, jitter, common vs SRIS), equalization 실패, retimer 설정입니다. 폭이 떨어지면 특정 lane이 detect나 deskew에서 탈락한 것이고, 보통 물리 문제(lane short/open, 패키지·보드)입니다. 그래서 Link Status를 읽는 한 줄이 HW 문제를 잡는 첫 검사가 됩니다.
- **AER과 surprise down**: correctable(Receiver Error, Bad TLP, Replay Timer Timeout)은 link가 재전송으로 버티고 있다는 뜻이고, downtrain과 같이 나오는 경우가 많습니다(`02:00.0`). uncorrectable(Completion Timeout, Poisoned TLP, Surprise Down)은 데이터나 장치가 날아간 것입니다. surprise down은 downstream port 쪽 AER과 DPC(Downstream Port Containment)가 처리하고, endpoint를 읽으면 all-ones가 옵니다. 검사 도구는 이때 멈추지 않고 "장치 사라짐"으로 기록해야 합니다.
- **SR-IOV / multi-host / NTB**: SR-IOV는 PF 하나가 VF를 여러 개 만들고, VF마다 RID가 있어 VM이나 container에 직접 붙입니다(IOMMU가 RID로 격리). 장치를 **여러 host**가 나눠 쓰려면 MR-IOV 대신 PCIe fabric switch가 host마다 partition을 나누고 VF를 다른 host 쪽 도메인에 매핑합니다. host 사이는 NTB(Non-Transparent Bridge)가 주소와 RID를 변환해 잇습니다. failover는 standby 경로로 다시 routing하는 일이라, 그 순간 config space가 all-ones였다가 돌아오는 동작을 견디는 검사가 필요합니다.
- **production / system-validation gate로 만들기**: 기대값 표(golden topology: 슬롯별 BDF, VID/DID, 기대 speed/width, 필수 cap, BAR 크기, VF 수)를 두고 매 부팅·매 SLT 단계에서 비교합니다. 결과는 장치별 pass/fail + 원시 레지스터 값으로 남깁니다. AER은 "0이어야 함"보다 soak 시간당 correctable 개수 기준이 현실적이고, 기록 후 RW1C로 지웁니다. Ethernet 쪽 상대는 [L3](../L3_rfc2544_throughput/index.html), 결과 집계는 [V5](../V5_test_runner_coverage/index.html)입니다.
- **FPGA vs 첫 실리콘**: FPGA(HAPS/Zynq)에서는 PCIe hard IP가 Gen3/Gen4 정도에 config space 대부분을 IP가 만들어 주므로, 리스트 구조와 SW 흐름 검증이 목적입니다. 첫 실리콘에서는 같은 코드로 실제 PHY를 봅니다. link가 목표 speed/width에 붙는지, equalization 결과, Lane Margining(Gen4+ ext cap `0x0027`)으로 eye margin을 잽니다. 그 margin을 전압·온도 코너에서 shmoo로 그리는 것이 [V7](../V7_shmoo_margin/index.html)입니다.

## 5.1 면접관 Iris Lee(SW·시스템 출신 Principal)가 파고들 포인트

- **"One endpoint behind the switch trained x2 instead of x4. Walk me through the debug."** → link 양쪽(downstream port와 endpoint)의 LnkCap/LnkSta를 비교하고, LnkCtl2 Target Link Speed, AER correctable 개수, retrain(LnkCtl bit 5) 후 재현 여부를 봅니다. 그다음 vendor LTSSM 로그로 어떤 lane이 Detect·Polling에서 빠졌는지 확인하고, 슬롯·케이블을 바꿔 보드 문제인지 장치 문제인지 가릅니다. *"I compare both ends of the link first, retrain to see if it's stable, then use the LTSSM trace to find which lane dropped — width loss is almost always physical."*
- **"How does SR-IOV relate to sharing one device across multiple hosts?"** → SR-IOV는 single-root 안에서 VF를 나누는 기능이고, multi-host 공유는 fabric switch가 host별 partition을 만들고 VF를 각 host 도메인에 매핑하거나, NTB로 host 사이 주소를 변환해서 합니다. 어느 쪽이든 VF RID가 bridge bus 범위 안에 있어야 하고 IOMMU가 RID로 격리합니다. *"SR-IOV splits the device; the fabric switch or an NTB decides which host sees which VF, and the RID math has to fit every bridge's bus range."*
- **"A device goes surprise-down in the middle of your check. What does your tool do?"** → config read가 all-ones로 돌아오고, hop 상한 덕분에 리스트 walk가 멈추지 않습니다. 도구는 Vendor ID를 다시 읽어 "removed"로 기록하고, downstream port의 AER Surprise Down과 DPC 상태를 같이 남깁니다. fabric failover라면 경로가 복구된 뒤 다시 검사합니다. *"All-ones is data, not a crash: every loop is bounded, and I re-read Vendor ID to classify it as removal, then log the downstream port's AER and DPC status."*
- **"How does this become a production gate, not a debug script?"** → golden topology 표와 비교해 장치별 pass/fail, 실패 시 원시 레지스터 덤프를 남기고, 같은 코드를 bootloader(bare-metal), Linux 진단, SLT에서 돌립니다. 기준은 버전 관리하고, AER은 soak 동안의 개수 기준으로 판정합니다. *"One check library, a versioned golden topology, and the same pass/fail criteria from FPGA bring-up to the factory line."*
- **"Why size BARs yourself instead of trusting lspci?"** → bring-up 초기에는 Linux가 없고, 있어도 커널이 이미 주소를 배정한 뒤라 "장치가 요청한 크기"와 "배정된 창"이 섞입니다. 직접 재면 RTL의 BAR mask가 스펙과 맞는지 확인할 수 있습니다. 반대로 live 장치에서는 decode를 끄는 게 위험하니 그때는 lspci/sysfs 값을 씁니다. *"Before Linux, sizing the BAR is how I verify the RTL's BAR mask; on a live system I read sysfs instead, because toggling decode under a driver is unsafe."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| How is the ECAM address formed, and what does legacy CF8/CFC lose? | `base + bus<<20 \| dev<<15 \| fn<<12 \| off`, 함수당 4 KiB. CF8/CFC는 I/O port 방식이라 offset 8비트(256 B)만 되어서 extended cap(AER, SR-IOV)을 못 봅니다. 그 경우 `0x100`이 all-ones/0 → `-ENOENT`가 맞는 동작입니다. |
| Vendor ID reads 0x0001 right after reset. Why? | CRS(Gen6부터 RRS) SW Visibility입니다. 장치가 아직 초기화 중이라 root port가 Vendor ID 자리에 `0x0001`을 돌려줍니다. 재시도해야 하고, 스펙상 리셋 후 100 ms 기다린 뒤 접근하며 장치는 최대 1초 안에 준비돼야 합니다. bring-up 중 이 상태가 계속되면 장치 FW가 멈춘 것입니다. |
| The endpoint says Gen5 but the switch port only does Gen4. Is Gen4 a failure? | 아닙니다. 기대값은 두 끝 LnkCap(실제로는 LnkCap2 Supported Link Speeds Vector)의 최소값, 그리고 LnkCtl2 Target Link Speed입니다. 실무 도구는 상대편 port의 cap까지 읽거나 golden 표의 기대 speed와 비교합니다. |
| How do you size VF BARs? | SR-IOV cap `+0x24`..`+0x38`의 VF BAR0–5를 같은 write-ones 방식으로 잽니다. 결과는 **VF 하나의** 크기이고 전체 창은 그 × NumVFs입니다. System Page Size(`+0x20`)에 따라 크기가 달라지니 그걸 먼저 설정합니다. |
| How do you clear AER status, and when? | RW1C라서 읽은 값을 그대로 다시 쓰면 그 비트만 지워집니다. 반드시 **기록 먼저, 지우기 나중**. uncorrectable이면 Header Log(`+0x1C`..)와 First Error Pointer도 같이 남깁니다. mask된 에러도 status 비트는 서지만 보고(ERR_* 메시지)는 안 됩니다. |
| Why must decode be off while sizing? What does Linux do? | all-ones를 쓴 순간 BAR가 잘못된 주소 범위를 decode합니다. 다른 master(CPU, P2P DMA)의 접근이 그 범위에 걸리면 이 장치로 갑니다. Linux `__pci_read_base`는 Command의 decode를 끄고 재고, host bridge처럼 끌 수 없는 장치는 `mmio_always_on`으로 예외 처리합니다. |
| An accelerator needs a 32 GiB window. What changes? | 64-bit prefetchable BAR + Resizable BAR(ext cap `0x0015`)로 지원 크기 목록을 받고, bridge의 prefetchable window(64-bit)도 그만큼 열어야 합니다. 4 GiB 이상이라 아래 dword mask가 0인 경우(`04:00.0`)를 반드시 처리해야 합니다. |
| Link is Gen4 x4 but measured throughput is half of expected. Where do you look? | Device Control의 MPS/MRRS(작으면 TLP overhead 증가), ASPM L1 진입·탈출 지연, flow-control credit 부족, Relaxed Ordering, IOMMU translation 비용, 그리고 AER Replay 증가(link는 붙었지만 재전송 중). 측정은 [V4](../V4_perf_bench_stats/index.html) 방식으로 min/median/p99. |
| What is different with ARI? | ARI(ext cap `0x000E`)가 켜지면 device 번호 5비트가 없어지고 function이 8비트(0–255)가 됩니다. 그래서 VF RID `0x0480`은 "dev 16 fn 0"이 아니라 "function 128"로 읽습니다. RID 계산식은 같고 해석만 다릅니다. VF가 많은 SR-IOV 장치는 사실상 ARI가 필요합니다. |

## 7. 자주 하는 실수

- 16-bit 필드를 `cfg_rd32(rid, cap + 0x12)`처럼 비정렬로 읽음 → 이 fake와 많은 root complex에서 all-ones (테스트 6의 `bad_access == 0`)
- pointer 하위 2비트를 mask 안 함 → `0x53`에서 비정렬 읽기, `03:00.0`에서 MSI를 못 찾음 (테스트 3)
- hop 상한 없이 `while (pos)` → 순환 리스트에서 테스트가 영원히 멈춤. 없는 장치(all-ones)도 `0xFC` → `0xFC` 순환을 만듭니다
- Status bit 4를 안 보고 `0x34`부터 읽음 → cap list가 없는 장치에서 쓰레기를 따라감 (테스트 3 마지막)
- Link Status를 mask 없이 비교 → Slot Clock Configuration(bit 12) 때문에 항상 "다름"
- VF RID를 `uint16_t`로 더함 → `0xFC80` 경우가 bus 0으로 감겨 통과 (테스트 4)
- BAR 크기를 `~mask + 1`로 계산 → I/O BAR가 `0xFFFF0100`(테스트 5), 64-bit BAR에서 아래 dword mask만 보면 8 GiB BAR가 "미구현"
- 64-bit BAR의 위 절반을 다음 BAR로 또 재서 BAR 개수가 하나 더 나옴
- Command를 dword로 다시 씀 → Status의 RW1C 에러 비트가 지워져 증거 손실 (테스트 5의 `memcmp`)
- decode를 끄지 않고 sizing, 또는 끈 decode를 복원 안 함 → `decode_on_sizing`, Command 비교에서 걸림

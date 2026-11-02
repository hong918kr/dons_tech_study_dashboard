# LP04 · 열거(enumeration)와 리소스 할당 · rescan · hotplug
<!-- theme: pastel -->

> **묶음**: C 리소스 · **우선**: P0 (스크린 질문 "L0 실패 시 lspci에 나오나?"의 뒷부분 + JD integration) · **선행 노트**: [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · [LP02 config space](2026-10-31_LP02_config_space_capabilities.html) · **다음 노트**: [LP05 SoC host controller 통합](2026-10-31_LP05_soc_host_controller_integration.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · **코드**: [lp04/enum_sim.c](onsitePrep/lp04/enum_sim.c) · [QEMU 실측 로그 (정상)](onsitePrep/lp03/qemu_lab/run1_boot_lspci_iomem.log) · [QEMU 실측 로그 (BAR no space)](onsitePrep/lp04/qemu_lab/highmem_off_bar_no_space.log)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"Walk me through enumeration" · "How does BAR sizing work?" · "BAR가 bridge window보다 크면?" · "pci=realloc이 뭐 하나?" · "부팅 후 늦게 올라온 장치는?" · "rescan vs hotplug" · "'can't assign; no space' 루트코즈 (ARM SoC)"**.
- **외울 것 1 — 열거 = config read로 하는 깊이 우선 탐색(DFS)**: bus 0의 dev 0..31에 Vendor ID read → **0xFFFF = 아무도 없음** (RRS 응답 0x0001 = "아직 준비 중, 다시") → bridge(Type 1)를 만나면 `primary=현재, secondary=다음 번호, subordinate=0xFF` 로 열어 두고 **그 밑부터 먼저** 다 스캔 → 돌아오면서 subordinate를 실제 마지막 번호로 줄임. PCIe downstream port 밑은 **device 0만** 스캔.
- **외울 것 2 — BAR sizing**: decode 끄기(Command Mem/IO=0) → 원래값 저장 → **0xFFFFFFFF 쓰기 → 읽기** → 하위 type 비트(mem 4비트/IO 2비트) 마스크 → **size = ~value + 1** → 원래값 복원. 64-bit BAR는 상위 dword까지 같이. **실측(QEMU)**: edu BAR0 readback `fff00000` → 1 MiB, NVMe 64-bit BAR `ffffffff:ffffc004` → 16 KiB.
- **외울 것 3 — 주소 창(window)은 트리 구조**: 모든 BAR는 **자기 크기로 정렬**, bridge는 non-prefetchable mem(32-bit, 1 MiB 단위) / prefetchable mem(64-bit 가능, 1 MiB 단위) / I/O(4 KiB 단위) **3개 창**으로 아래 전체를 감싸야 한다. 자식 하나라도 안 들어가면 `BAR 2 [mem size 0x40000000 64bit pref]: can't assign; no space` → `failed to assign`. **실측**: QEMU virt `highmem=off` + 1 GiB BAR → 이 메시지, `pci=realloc`을 줘도 실패 (공간 자체가 없음).
- **누가 열거하나**: x86 = **펌웨어(UEFI/BIOS)가 먼저** 버스 번호·BAR를 다 배정, Linux는 대부분 **그대로 claim**하고 빈 것만 채움. ARM SoC + DT generic host = **Linux가 전부 새로 배정** (`pci_host_probe()`: preserve_config 아니면 "this will reassign everything"), 예외는 DT `linux,pci-probe-only` / ACPI `_DSM` func 5 = 0.
- **런타임**: `echo 1 > /sys/bus/pci/devices/<BDF>/remove` → `echo 1 > /sys/bus/pci/rescan` 는 **SW만 다시 스캔** (링크를 다시 올리지 않음). hotplug(pciehp)는 **슬롯 이벤트(Presence Detect / Data Link Layer State Changed)** 를 인터럽트로 받아 자동으로 add/remove — 링크가 DL_Active가 돼야 장치가 붙는다.
- **30초 영어 답 ("Walk me through enumeration")**: "Enumeration is a depth-first walk using configuration reads. Starting at bus 0, software reads the Vendor ID at each device number — 0xFFFF means nothing's there, and a Request Retry Status means the device isn't ready yet, so retry. When it finds a bridge, it programs the primary and secondary bus numbers with subordinate temporarily at 0xFF, scans everything below first, then shrinks subordinate to the highest bus it found. For each function it sizes the BARs by writing all ones and reading back, then assigns addresses top-down so every BAR is naturally aligned and every bridge's memory, prefetchable and I/O windows cover its children. On x86 the firmware usually did this already and Linux claims it; on an ARM SoC with a device-tree host bridge Linux does the whole assignment itself from the DT ranges."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "새 아파트 단지에 주소와 우편함 크기를 배정하는 관리인"

- **단지(PCI domain/segment)** 안에 **동(bus)** 이 있고, 동마다 **호수(device 0–31)**, 한 호에 **방(function 0–7)** 이 있다. 주소 = **BDF (Bus:Device.Function)**, 예 `01:00.0`.
- **엘리베이터/복도(bridge, switch 포트)** 가 동과 동을 잇는다. 관리인(열거 SW)은 처음에 **0동만** 안다. 0동을 돌며 "누구 계세요?"(Vendor ID read)를 묻고, 대답이 없으면(0xFFFF) 빈 집.
- 엘리베이터를 발견하면 "이 엘리베이터 위층은 이제부터 **1동**(secondary)이고, 그 위로 몇 동까지 있는지는 아직 모르니 **일단 끝까지(0xFF)** 네 담당"이라고 써 붙인 뒤, **바로 그 위층부터 끝까지 먼저** 돌아본다(깊이 우선). 다 돌고 내려오면 "네 담당은 사실 1~5동이었다"(subordinate=5)로 고친다.
- 각 집은 "**우편함(BAR)이 몇 칸 필요하다**"를 신고한다 — 관리인이 우편함 칸에 "전부 1"을 써 보면 집이 "내가 쓸 수 없는 칸은 0으로 남긴다" → 몇 칸이 0으로 남았는지로 **크기**를 안다(BAR sizing).
- 그다음 관리인은 **CPU 주소 지도(물리 주소 공간)** 에서 각 우편함 자리를 잡아 준다. 엘리베이터는 "**내 위층 전체 우편함은 이 주소 범위 안**"이라는 **창(window)** 을 갖고, 그 범위의 우편물만 위로 올려 보낸다. 큰 우편함(예: GPU/가속기 메모리 1 GiB)이 창에 안 들어가면 그 집은 우편을 못 받는다 = `can't assign; no space`.

```text
 CPU 물리 주소 공간 (QEMU virt, 실측 /proc/iomem 요약)
 0x10000000 ┬────────────────────────────────────────── pcie@10000000 32-bit MEM 창 (DT ranges)
            │ 10000000-101fffff  PCI Bus 01   (root port 00:01.0의 mem 창)
            │    10000000-10003fff  01:00.0 NVMe BAR0 16K
            │ 10200000-106fffff  PCI Bus 02   (root port 00:02.0 -> switch)
            │    10200000-104fffff  PCI Bus 04 (DSP 03:00.0) -> e1000e BAR0/1/3, ROM
            │    10500000-106fffff  PCI Bus 05 (DSP 03:01.0) -> edu BAR0 1M
            │ 10700000-108fffff  PCI Bus 06   (빈 슬롯 00:03.0 — hotplug 대비로 2M 예약)
 0x3efeffff ┴──────────────────────────────────────────
 0x4010000000-401fffffff  PCI ECAM (config space 256 MiB = 256 bus x 1 MiB)
 0x8000000000 ┬──────────────────────────────────────── 64-bit MEM 창 (DT ranges, 512 GiB)
              │ 8000000000-80001fffff PCI Bus 01 (prefetchable 창) ...
```

### 1.2 PCI 토폴로지를 SW가 보는 방식

```text
                 CPU
                  |
           [Host Bridge / Root Complex]   00:00.0
              |            |           |
        [Root Port]   [Root Port]  [Root Port]       bus 0 (RC 내부의 "가상" 버스)
         00:01.0       00:02.0      00:03.0          Type 1 header (= PCI-to-PCI bridge)
           | link        | link        | (빈 슬롯)
           |             |
      01:00.0 NVMe   [Switch USP] 02:00.0            Type 0 = endpoint, Type 1 = bridge
                         | (switch 내부 가상 bus 3)
                  +------+------+
             [DSP] 03:00.0   [DSP] 03:01.0            switch = USP 1개 + DSP N개, 각각 bridge
                |               |
            04:00.0 e1000e   05:00.0 edu
```

- **스위치는 SW에게 "bridge 여러 개"로 보인다**: USP 1개 + DSP N개, 그리고 그 사이 내부 버스 1개. 4-port 스위치(USP 1 + DSP 3)면 BDF 4개 + 버스 번호 최소 1(내부)+3(각 DSP 밑) 소모.
- **각 PCIe 링크 = 버스 하나**, 링크는 점대점이라 그 버스에는 **device 0 하나만** (그래서 Linux `only_one_child()`가 DSP 밑은 dev 0만 스캔) [확인됨][2].

### 1.3 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| Enumeration | — | config read로 트리를 돌며 장치를 찾고 번호·주소를 배정하는 과정 | "lspci에 나온다" = 열거 성공 |
| BDF | Bus:Device.Function | 장치 주소. bus 8비트, dev 5비트, fn 3비트 | 모든 명령의 인자 |
| Domain / Segment | — | 독립된 BDF 공간(호스트 브리지마다 하나일 수 있음), `0000:` | 여러 컨트롤러 SoC |
| Config space | Configuration Space | 함수마다 4 KiB 레지스터 공간 (0x00–0xFF 표준, 0x100+ extended) | 열거·BAR·capability 전부 여기 |
| ECAM | Enhanced Configuration Access Mechanism | config space를 메모리 주소로 접근: `base + (bus<<20 \| dev<<15 \| fn<<12 \| off)` | bus당 1 MiB → 256 bus = 256 MiB (QEMU 실측과 일치) |
| Type 0 / Type 1 header | — | 0 = endpoint (BAR 6개), 1 = bridge (BAR 2개 + 버스번호 + 창 레지스터) | Header Type(0x0E) [6:0]로 구분 |
| Primary / Secondary / Subordinate bus | — | bridge 위쪽 버스 / 바로 아래 버스 / 아래 가장 큰 버스 번호 | 이 범위로 config 요청을 아래로 전달 |
| Vendor ID 0xFFFF | — | 응답 없음(Unsupported Request/master abort)이면 HW가 all-1s 반환 | "장치 없음" 판정 |
| RRS (= CRS) | Request Retry Status (구 Configuration Request Retry Status) | "링크는 됐는데 아직 준비 중" 응답. SV(Software Visibility) 켜면 Vendor ID **0x0001** 로 보임 | 늦게 부팅하는 장치 FW, 리셋 직후 |
| BAR | Base Address Register | 장치가 "내 레지스터/메모리를 이 크기만큼 CPU 주소에 매핑해 달라"고 요청하는 칸 | 드라이버 MMIO의 시작점 |
| BAR sizing | — | BAR에 all-1s 쓰고 읽어 크기 알아내기 | 면접 단골 |
| 32-bit / 64-bit BAR | — | BAR type 비트 [2:1] = 00 (32-bit) / 10 (64-bit, BAR 2칸 사용) | 큰 BAR는 4 GiB 위로 |
| Prefetchable | — | 읽어도 부작용 없는 메모리(BAR bit3 = 1) → 미리 읽기/write-combining 가능 | bridge의 prefetchable 창으로 감 |
| I/O BAR | — | x86 I/O 포트 공간(bit0 = 1). ARM에서는 메모리로 흉내 | 레거시, 작음 |
| Bridge window | — | bridge 아래 장치들의 주소 범위 (I/O, MEM, Prefetchable MEM) | 창 밖 주소는 아래로 안 감 |
| Resource | — | 커널 `struct resource`: 주소 범위 + 플래그. `/proc/iomem` 트리 | 충돌·부족 진단 |
| `ranges` (DT) | — | CPU 주소 ↔ PCI 주소 창 정의 (I/O, 32-bit MEM, 64-bit MEM) | ARM에서 **창 크기의 원천** |
| `bus-range` (DT) | — | 이 호스트 브리지가 쓰는 버스 번호 범위 | ECAM 크기·버스 고갈 |
| `_CRS` / MCFG | Current Resource Settings / Memory-mapped Config table | ACPI에서 창 범위 / ECAM 주소를 알려주는 방법 | x86·ARM 서버, Lima VM |
| `pci=realloc` | — | 펌웨어 배정이 부족하면 bridge 창을 풀고 다시 크게 배정 | 공간이 "있는데" 배치가 나쁠 때만 효과 |
| `pci=hpmemsize=` | — | hotplug 슬롯 밑에 미리 예약할 메모리 창 크기 (기본 2 MiB) | 나중에 꽂을 큰 장치 대비 |
| Rescan / Remove | — | sysfs로 SW 열거를 다시 하거나 장치를 SW에서 빼기 | 링크가 늦게 뜬 경우 |
| pciehp | PCIe native hotplug driver | 슬롯 이벤트(PresDet, DLL State Changed, Attention Button)로 장치 추가/제거 | 슬롯에 `HotPlug+` 필요 |
| acpiphp | ACPI PCI hotplug | 펌웨어(ACPI)가 hotplug를 관리하고 OS에 알림 | _OSC로 누가 관리할지 협상 |
| _OSC | Operating System Capabilities | OS가 "PCIe hotplug/AER/PME를 내가 직접 할게"를 펌웨어에 요청 | 허락 못 받으면 native 기능 off |
| Surprise removal | — | 미리 알리지 않고 장치가 빠짐 (케이블 뽑힘, 링크 사망) | 드라이버가 0xFFFFFFFF 읽기를 견뎌야 함 |
| DPC | Downstream Port Containment | 치명 에러 시 포트가 링크를 끊어 오염 차단 | surprise down의 동반자 (LP09) |

## 2. 핵심 개념 (정확한 버전)

### 2.1 누가 열거하나 — x86 vs ARM

| 플랫폼 | 1차 열거 | Linux가 하는 일 | 커널 근거 [확인됨] |
|---|---|---|---|
| x86 PC/서버 (UEFI) | **펌웨어**가 버스 번호, BAR, bridge 창, (옵션 ROM) 전부 배정 | `pcibios_resource_survey()` → 펌웨어 배정을 `pci_claim_resource()`로 **claim**, 그 뒤 `fs_initcall(pcibios_assign_resources)` → `pci_assign_unassigned_resources()` 로 **빈 것만** 배정 | `arch/x86/pci/i386.c` [3] |
| ARM SoC + DT generic host (`pci-host-ecam-generic`, DWC 등) | 보통 **없음** (U-Boot가 했어도 Linux는 무시) | `pci_host_probe()` → preserve_config면 claim, **아니면 "this will reassign everything"** → `pci_assign_unassigned_root_bus_resources()` | `drivers/pci/probe.c` [2] |
| ARM + DT, 펌웨어 배정 유지 원할 때 | 펌웨어 | DT `linux,pci-probe-only = <1>` (호스트 노드 또는 `/chosen`) → `of_pci_preserve_config()` true | `drivers/pci/of.c` [2] |
| ARM 서버 / VM (ACPI) | 펌웨어 | ACPI `_DSM` function **5** (`DSM_PCI_PRESERVE_BOOT_CONFIG`) 가 0을 반환하면 보존, 아니면 재배정 | `drivers/pci/pci-acpi.c` [2] |

- **실측 근거 1 (QEMU `-kernel` 부팅, 펌웨어 없음)**: dmesg `pci 0000:00:01.0: bridge configuration invalid ([bus 00-00]), reconfiguring` — 아무도 bridge 버스 번호를 안 써 놨으니 커널이 직접 배정.
- **실측 근거 2 (Lima VM = Apple VZ, ACPI)**: 부팅 로그에 `00:01.0: BAR 0 [mem 0x380060000-0x38006ffff 64bit]` (펌웨어 값) → 이후 `00:01.0: BAR 0 [mem 0x380000000-0x38000ffff 64bit]: assigned` 로 **다시 배정됨** (현재 `/proc/iomem`도 0x380000000) → `_DSM` 보존 요청이 없어서 arm64가 재배정한 것으로 보임 [추정: 원인 해석]. 같은 로그에 `_OSC: platform retains control of PCIe features (AE_ERROR)` — OS가 native hotplug/AER 권한을 못 받음.
- **`pci=` 옵션 (자주 쓰는 것)** [확인됨: kernel-parameters.txt, pci.c][3]:

| 옵션 | 효과 |
|---|---|
| `pci=realloc` (`=on/off`) | 배정 실패 시 bridge 창을 **해제(releasing)하고 더 크게 재배정** 시도 |
| `pci=hpmemsize=nnM` | hotplug bridge 밑 non-pref + pref 창 예약 크기 (기본 각 **2 MiB**) |
| `pci=hpmmiosize=` / `hpmmioprefsize=` | 둘을 따로 |
| `pci=hpiosize=` | hotplug I/O 창 (기본 256 B → 실제 bridge I/O 창 최소 4 KiB 단위) |
| `pci=hpbussize=` | hotplug bridge 밑 예약 버스 수 (기본 1) |
| `pci=assign-busses` (x86) | 펌웨어 버스 번호 무시하고 커널이 재배정 |
| `pci=nocrs` / `use_crs` (x86) | ACPI `_CRS` 창 무시 / 사용 |
| `pcie_ports=native` / `compat` | _OSC와 무관하게 native 서비스(hotplug/AER/PME/DPC) 강제 / 끄기 |
| `pci=resource_alignment=` | 특정 장치 BAR 정렬 강제 (VFIO 등) [추정: 세부 문법] |

### 2.2 깊이 우선 스캔 알고리즘 (worked example)

```text
scan_bus(bus):
  for dev in 0..31 (DSP 바로 아래면 dev 0만):
     vid = cfg_read16(bus, dev, 0, VENDOR_ID)
     if vid == 0xFFFF: continue                 # 아무도 없음
     if vid == 0x0001: retry with backoff        # RRS: 아직 준비 중 (SV 켜진 경우)
     header = cfg_read8(..., HEADER_TYPE)        # bit7 = multi-function -> fn 1..7도 스캔
     size BARs
     if header[6:0] == 1:                        # bridge
         next = ++max_bus
         write PRIMARY=bus, SECONDARY=next, SUBORDINATE=0xFF   # "아래 전부 네 것"
         sub = scan_bus(next)                    # 먼저 끝까지 내려감 (DFS)
         write SUBORDINATE=sub                   # 실제 끝으로 줄임
  return max_bus
```

| 단계 | 하는 일 (위 1.2 토폴로지) | max_bus |
|---|---|---|
| 1 | bus 0: dev0 host bridge, dev1 RP1 발견 → RP1 sec=1, sub=FF | 1 |
| 2 | bus 1 (DSP 밑 → dev0만): NVMe 발견, bridge 아님 → RP1 sub=1 | 1 |
| 3 | bus 0 dev2 RP2 → sec=2, sub=FF | 2 |
| 4 | bus 2 dev0: switch USP (bridge) → sec=3, sub=FF | 3 |
| 5 | bus 3 (switch 내부, dev 0..31 전부 스캔): dev0 DSP0 → sec=4 → bus 4: e1000e → DSP0 sub=4 | 4 |
| 6 | bus 3 dev1 DSP1 → sec=5 → bus 5: edu → DSP1 sub=5 → USP sub=5 → RP2 sub=5 | 5 |
| 7 | bus 0 dev3 RP3 → sec=6 → bus 6 dev0 = 0xFFFF (빈 슬롯) → RP3 sub=6 | 6 |

- 이게 **QEMU 실측 결과와 정확히 같다**: `lspci -tv` = `01.0-[01]`, `02.0-[02-05]----00.0-[03-05]--+-00.0-[04]` … `03.0-[06]--`, dmesg `pci_bus 0000:01: busn_res: [bus 01-ff] end is updated to 01` (sub=FF로 열었다가 줄이는 장면 그대로), `pci_bus 0000:02: busn_res: [bus 02-ff] end is updated to 05`.
- **빈 슬롯에도 버스 번호를 준다** (bus 06) — 나중에 hotplug로 꽂히면 쓸 자리. Linux는 hotplug bridge에 추가 버스를 나눠 줌 (`pci_scan_child_bus_extend()`: hotplug bridge 수로 남은 버스를 분배, 최소 `pci=hpbussize`) [확인됨][2]. 스위치가 붙을 슬롯에 버스 1개만 남기면 나중에 스위치를 꽂을 때 **버스 번호 부족**.
- Linux의 실제 스캔은 **2 pass**: pass 0 = 펌웨어가 이미 올바르게 설정한 bridge는 건드리지 않고 따라 들어감, pass 1 = 설정이 없거나 틀린 bridge를 재설정 (`"bridge configuration invalid ... reconfiguring"`) [확인됨][2].
- **CRS/RRS 처리**: Root Port의 Root Control **RRS Software Visibility Enable** 이 켜져 있으면 RRS 응답이 Vendor ID 0x0001로 보이고 SW가 재시도 (Linux는 지원 시 자동으로 켬, `PCI_EXP_RTCAP_RRS_SV`) [확인됨][2]. 꺼져 있으면 **root complex가 HW로 재시도** → CPU의 config read가 장치가 준비될 때까지 멈춰 있을 수 있음 (긴 stall) [추정: 구현 의존]. Linux 열거는 Vendor ID RRS를 **최대 60 s** (`pci_bus_read_dev_vendor_id(bus, devfn, &l, 60*1000)`), 1 ms부터 2배씩 늘리며 재시도하고, 1 s 넘으면 `"not ready after %dms; waiting"`, 포기 시 `"not ready after %dms; giving up"` [확인됨][2]. QEMU 실측: `RootCap: CRSVisible-` (QEMU root port는 미지원).
- **Vendor ID 판정의 실제 코드**: `PCI_POSSIBLE_ERROR(l)`(all-1s), `0x00000000`, `0x0000ffff`, `0xffff0000` 을 "없음"으로 처리 — 고장난 보드가 0이나 반쪽 1을 돌려주는 경우까지 방어 [확인됨][2].

### 2.3 BAR 형식과 sizing (worked numeric example)

```text
 Memory BAR (32-bit dword)                      I/O BAR
 31                    4   3    2  1   0         31              2  1  0
 +----------------------+----+------+---+       +-----------------+---+---+
 | base address (RW)    |Pref| Type | 0 |       | base (RW)       | R | 1 |
 | 하위 비트 = 크기만큼 0 | 0/1| 00=32|   |       +-----------------+---+---+
 +----------------------+----+ 10=64+---+
                                 (64-bit면 다음 BAR가 상위 32비트)
```

**알고리즘** (bare-metal / 펌웨어 / Linux `__pci_read_base()` 공통):

1. Command 레지스터에서 **Memory Space / I/O Space decode 끄기** (all-1s 값이 잠깐 진짜 주소로 디코드되면 다른 장치와 충돌).
2. 원래 BAR 값 저장.
3. BAR에 `0xFFFFFFFF` 쓰기, 다시 읽기. (64-bit이면 상위 BAR에도)
4. 하위 type 비트 제거: memory는 `& ~0xF`, I/O는 `& ~0x3`.
5. **size = (~masked) + 1** (32-bit 연산, 64-bit는 64-bit 연산).
6. 원래 값 복원, decode 복원.

| 예 | readback | 해석 | 크기 |
|---|---|---|---|
| **QEMU edu BAR0 (실측)** | `fff00000` | bit0=0 mem, [2:1]=00 32-bit, bit3=0 non-pref. `~0xfff00000 = 0x000fffff` → +1 | **0x100000 = 1 MiB** (lspci `[size=1M]`) |
| **QEMU NVMe BAR0/1 (실측)** | low `ffffc004`, high `ffffffff` | low bit[2:1]=10 → **64-bit**, bit3=0 non-pref. 64-bit 값 `0xFFFFFFFF_FFFFC000` → `~` = `0x3FFF` → +1 | **0x4000 = 16 KiB** (lspci `[size=16K]`) |
| e1000e BAR2 I/O (시뮬) | `ffffffe1` | bit0=1 I/O, `& ~3` = `0xffffffe0` → `~` = `0x1f` → +1 | **32 B** |
| 가속기 BAR2 64-bit pref (시뮬) | `ffffffff:f000000c` | `0xC` = 64-bit + prefetchable, `0xFFFFFFFF_F0000000` → | **256 MiB** |
| 미구현 BAR | `00000000` | 쓰기 불가 = 없음 | — |

- 왜 동작하나: 장치는 **크기보다 작은 주소 비트를 0으로 고정**(hard-wired)한다 → "1을 써도 0으로 남는 비트 수" = log2(크기). 그래서 **BAR 크기는 항상 2의 거듭제곱이고, 배정 주소는 자기 크기로 정렬**되어야 한다(natural alignment).
- 실측 로그: [run2_setpci_bar_rescan.log](onsitePrep/lp03/qemu_lab/run2_setpci_bar_rescan.log) — `edu BAR0 old=10500000 readback_after_all1s=fff00000 restored=10500000`, `nvme BAR0 old=10000004 BAR1 old=00000000 readback=ffffffff:ffffc004`.
- **함정**: (1) decode를 안 끄고 sizing → 그 순간 0xFFFFxxxx 주소가 디코드돼 다른 MMIO와 충돌 가능. (2) 64-bit BAR의 상위를 sizing 안 하고 32-bit로 계산 → 4 GiB 이상 BAR를 잘못 읽음. (3) 복원 잊음. (4) Expansion ROM BAR(0x30)은 bit0이 enable 비트 → 별도 처리.

### 2.4 Bridge window — 3개의 창과 정렬 규칙

| 창 | 레지스터 (Type 1 header) | 인코딩 / 단위 | 범위 |
|---|---|---|---|
| I/O | `IO_BASE` 0x1C / `IO_LIMIT` 0x1D (+ upper16 0x30/0x32) | [7:4] = addr[15:12] → **4 KiB 단위** | 16-bit (선택적 32-bit) |
| Memory (non-prefetchable) | `MEMORY_BASE` 0x20 / `MEMORY_LIMIT` 0x22 | [15:4] = addr[31:20] → **1 MiB 단위**, limit 하위 = `0xFFFFF` | **32-bit만** |
| Prefetchable memory | `PREF_MEMORY_BASE` 0x24 / `PREF_MEMORY_LIMIT` 0x26 + `PREF_BASE_UPPER32` 0x28 / `PREF_LIMIT_UPPER32` 0x2C | 1 MiB 단위, 하위 nibble = 1이면 **64-bit 지원** | 32 또는 64-bit |

- 실측 (QEMU root port 00:02.0, setpci 원시값 → 해석):

```text
PRIMARY=00 SECONDARY=02 SUBORDINATE=05
MEMORY_BASE=1020 MEMORY_LIMIT=1060        -> 0x10200000 - 0x106FFFFF   (lspci: Memory behind bridge: 10200000-106fffff [size=5M])
PREF_MEMORY_BASE=0021 PREF_MEMORY_LIMIT=0071, UPPER32=00000080/00000080
                                           -> 0x80_0020_0000 - 0x80_007F_FFFF (하위 nibble 1 = 64-bit 창)
IO_BASE=20 IO_LIMIT=40                     -> 0x2000 - 0x4FFF          (lspci: I/O behind bridge: 2000-4fff [size=12K])
```

- **규칙**:
  1. **non-prefetchable 창은 32-bit** → 64-bit **non-pref** BAR(QEMU NVMe BAR0처럼)도 bridge 밑에 있으면 **4 GiB 아래**에 놓여야 한다 (dmesg 실측 `01:00.0: BAR 0 [mem 0x10000000-0x10003fff 64bit]: assigned`).
  2. **prefetchable 창만 4 GiB 위로** 갈 수 있음 → 큰 GPU/가속기 BAR는 **64-bit + prefetchable**이어야 high window에 들어감.
  3. 창은 **아래 모든 자식 창 + BAR를 포함**해야 하고 **겹치면 안 됨**. 1 MiB 단위라 작은 BAR 몇 개라도 bridge 하나당 최소 1 MiB 소비.
  4. 창 base > limit = **창 닫힘**. 시뮬레이터 빈 슬롯/link down DSP가 이 상태.
  5. Linux는 hotplug bridge(SltCap `HotPlug+`)에 **추가 예약**(`add_size`)을 붙임: 실측 `00:03.0: bridge window [mem 0x00100000-0x000fffff 64bit pref] to [bus 06] add_size 200000 add_align 100000` → 빈 슬롯 00:03.0에도 `bridge window [mem 0x10700000-0x108fffff]` (2 MiB) 배정 = **`pci=hpmemsize` 기본값 2 MiB가 실제로 적용된 장면**.

### 2.5 Linux의 리소스 배정 순서

```text
pci_host_probe()                                  (DT/ACPI host bridge driver가 호출)
  pci_scan_root_bus_bridge()     -> 스캔 + BAR sizing (__pci_read_base): dmesg "BAR 0 [mem 0x00000000-0x00003fff 64bit]" (아직 0 기준)
  (preserve_config ? pci_bus_claim_resources())   -> 펌웨어 배정 유지
  pci_assign_unassigned_root_bus_resources()
     pci_bus_size_bridges()      -> 아래에서 위로: 자식 크기 합 + 정렬 -> 각 bridge 창 크기 계산
                                    dmesg "bridge window [...] to [bus 01] add_size 200000 add_align 100000"
     __pci_bus_assign_resources() -> 위에서 아래로: 창 배정 -> 그 안에 BAR 배정
                                    dmesg "bridge window [mem 0x10000000-0x101fffff]: assigned"
                                          "BAR 0 [mem 0x10000000-0x10003fff 64bit]: assigned"
     실패 시 (realloc 모드면) 실패한 bridge 창을 "releasing" 후 재시도
  pci_bus_add_devices()          -> device_add -> 드라이버 probe
```

- 실패 메시지 (6.18 형식, `drivers/pci/setup-res.c`) [확인됨][2]: `"%s %pR: can't assign; no space"`, `"%s %pR: failed to assign"`, `"%s %pR: can't assign; bogus alignment"`, `"%s %pR: can't claim; no compatible bridge window"`, `"%s %pR: can't claim; address conflict with %s %pR"`, 드라이버 enable 시 `"%s %pR: not assigned; can't enable device"`. **구형 커널(≈6.6 이전) 형식**: `BAR 0: no space for [mem size 0x...]`, `BAR 0: failed to assign [mem size 0x...]` — 면접관 기억은 이쪽일 수 있음 [추정: 버전 경계].
- 드라이버 쪽: `pci_enable_device()` 가 BAR가 배정 안 됐으면 실패 → probe가 `-ENOMEM`/`-EINVAL`로 끝나고 `"can't enable device"`. 그래서 증상은 "**lspci에는 보이는데 드라이버가 안 붙음**".

### 2.6 ARM SoC: DT `ranges` / `bus-range` 가 창 크기의 원천

- QEMU virt DT의 실제 pcie 노드 (lab `/var/lab/out/virt.dts`, 실측):

```text
pcie@10000000 {
    compatible = "pci-host-ecam-generic";
    device_type = "pci";
    reg = <0x40 0x10000000 0x00 0x10000000>;          // ECAM: CPU 0x40_1000_0000, 256 MiB
    bus-range = <0x00 0xff>;                           // 256 buses (ECAM 1 MiB/bus)
    #address-cells = <3>; #size-cells = <2>;
    ranges = <0x01000000 0x00 0x00000000  0x00 0x3eff0000  0x00 0x00010000     // I/O  64 KiB
              0x02000000 0x00 0x10000000  0x00 0x10000000  0x00 0x2eff0000     // MEM32 ~752 MiB
              0x03000000 0x80 0x00000000  0x80 0x00000000  0x80 0x00000000>;   // MEM64 512 GiB
};
```

- `ranges` 한 항목 = (PCI 주소 3셀: 첫 셀 상위 바이트가 공간 종류 — `0x01` I/O, `0x02` 32-bit MEM, `0x03` 64-bit MEM, `0x4x` = prefetchable 비트) + (CPU 주소 2셀) + (크기 2셀). 커널 로그와 1:1: `IO 0x003eff0000..0x003effffff -> 0x0000000000`, `MEM 0x0010000000..0x003efeffff -> 0x0010000000`, `MEM 0x8000000000..0xffffffffff -> 0x8000000000` (실측).
- **실측 실패 재현**: `-machine virt,highmem=off` → 64-bit MEM 항목이 사라지고 ECAM도 `[bus 00-0f]` (16 MiB = 16 bus)로 줄어듦 + `ivshmem-plain` 1 GiB 64-bit pref BAR → 752 MiB 창에 안 들어감:

```text
pci-host-generic 3f000000.pcie:      MEM 0x0010000000..0x003efeffff -> 0x0010000000
pci-host-generic 3f000000.pcie: ECAM at [mem 0x3f000000-0x3fffffff] for [bus 00-0f]
pci 0000:01:00.0: BAR 2 [mem 0x00000000-0x3fffffff 64bit pref]
pci 0000:00:01.0: bridge window [mem size 0x40000000 64bit pref]: can't assign; no space
pci 0000:00:01.0: bridge window [mem size 0x40000000 64bit pref]: failed to assign
pci 0000:01:00.0: BAR 2 [mem size 0x40000000 64bit pref]: can't assign; no space
pci 0000:01:00.0: BAR 2 [mem size 0x40000000 64bit pref]: failed to assign
```

- **SoC에서의 fix**: 컨트롤러가 실제로 디코드할 수 있는 CPU 주소 창(SoC 메모리 맵 + iATU outbound 창 수/크기)을 확인하고 DT `ranges`에 **64-bit prefetchable 창(0x43000000)** 을 충분히 크게 추가. 32-bit 공간만 있는 SoC면 장치 쪽 BAR 크기를 줄이는(resizable BAR, 장치 FW 설정) 수밖에 없음. `bus-range`가 작으면 스위치 추가 시 **버스 번호 부족** → `busn_res` 관련 실패 [추정: 메시지 형식].
- DWC 계열은 `ranges`를 **iATU outbound 영역**으로 프로그램한다 → 창 개수가 iATU 영역 수로 제한되기도 함 (LP05) [추정].

### 2.7 런타임: remove / rescan / hotplug / surprise removal

| 동작 | 무엇이 일어나나 | 링크에 영향? |
|---|---|---|
| `echo 1 > /sys/bus/pci/devices/<BDF>/remove` | 드라이버 unbind → `pci_dev` 삭제 (bridge면 아래 전체) → 버스 번호/리소스 해제 | ❌ 링크는 그대로 (L0 유지) |
| `echo 1 > /sys/bus/pci/rescan` | 모든 root bus를 다시 스캔, 새 장치에 리소스 배정 → 드라이버 probe | ❌ 링크를 올리지 않음: **이미 DL_Active인 링크 아래만** 찾음 |
| `echo 1 > /sys/bus/pci/devices/<bridge>/rescan` (또는 `.../pci_bus/<bus>/rescan`) | 그 bridge 아래만 | ❌ |
| pciehp 이벤트 | 슬롯 PresDet 변화 / Data Link Layer State Changed / Attention Button → 전원 on(PwrCtrl 있으면) → **링크 up 대기** → 스캔·배정·probe. 제거 시 반대 | ✅ 슬롯 전원·링크를 관리 |
| ACPI hotplug (acpiphp) | 펌웨어가 Notify로 OS에 알림 → OS가 스캔 | 펌웨어 의존 |
| Surprise removal | 링크 down → pciehp `Slot(%s): Link Down` / `Card not present` → **`pci_dev_set_disconnected()`** 로 장치를 "끊김" 표시(이후 config 접근은 HW 안 건드리고 all-1s) → 드라이버 remove | — |

- **실측 (QEMU)**: NVMe remove → `lspci`에서 01:00.0 사라짐 → rescan → `pci 0000:01:00.0: [1b36:0010] type 00 class 0x010802 PCIe Endpoint` … `BAR 0 [mem 0x10000000-0x10003fff 64bit]: assigned` 로 **같은 주소로 다시** 붙음. 스위치 통째(02:00.0) remove → `busn_res: [bus 03-05] is released`, `e1000e ... removed PHC` → rescan 시 USP/DSP/e1000e/edu 전부 재열거·재배정, e1000e 드라이버 재probe (`eth0: (PCI Express:2.5GT/s:Width x1)`). 로그: [run2_setpci_bar_rescan.log](onsitePrep/lp03/qemu_lab/run2_setpci_bar_rescan.log).
- **pciehp 조건**: 포트 Slot Capabilities `HotPlug+` (Linux `set_pcie_hotplug_bridge()`가 `PCI_EXP_SLTCAP_HPC` 로 `is_hotplug_bridge` 설정) [확인됨][2] + `CONFIG_HOTPLUG_PCI_PCIE=y` + _OSC로 OS가 native hotplug 권한 획득(또는 `pcie_ports=native`). 인터럽트가 없으면 `pciehp.pciehp_poll_mode=1` 폴링 [확인됨][4].
- pciehp 로그 문자열 (`drivers/pci/hotplug/pciehp_ctrl.c`) [확인됨][4]: `Slot(%s): Card present`, `Slot(%s): Link Up`, `Slot(%s): Link Down`, `Slot(%s): Card not present`.
- 랩 한계: **lab 커널 전부 `# CONFIG_HOTPLUG_PCI_PCIE is not set`** (bool 옵션이라 모듈로 추가 불가) → hotplug 실측 못 함 [미실행] (§6.3 레시피). QEMU 포트 서비스 목록에 `0000:00:01.0:pcie004` (HP 서비스 디바이스)는 있지만 바인딩할 드라이버가 없음.

### 2.8 "부팅 후 늦게 링크가 올라온 장치" 시나리오

```text
 t=0      커널 PCI 스캔: RP 아래 vendor read -> 0xFFFF (링크가 아직 L0 아님) -> 장치 없음
 t=5s     카드 FW 늦게 부팅 / PERST# 늦게 풀림 -> LTSSM L0 -> DL_Active
          (a) 슬롯이 pciehp 관리:  DLL State Changed 인터럽트 -> "Slot(1): Link Up" -> 자동 열거
          (b) hotplug 없음:        아무 일도 없음 -> lspci에 안 보임
              -> echo 1 > /sys/bus/pci/rescan  -> 이제 보임 (단, 그 RP 아래 창/버스가 충분히 예약돼 있어야 BAR 배정 성공)
```

- (b)에서 함정: 부팅 때 그 root port 아래가 비어 있었으면 창이 0 또는 hotplug 기본(2 MiB)만 → 큰 BAR 장치는 rescan해도 `can't assign; no space` → `pci=hpmemsize=`, `pci=realloc`, 또는 펌웨어/DT에서 예약.

## 3. Linux에서 볼 것

### 3.1 명령 모음

| 목적 | 명령 |
|---|---|
| 열거 로그 | `dmesg \| grep -E "pci 0000\|pci_bus\|BAR\|bridge window\|no space\|failed to assign"` |
| 호스트 브리지 창 | `dmesg \| grep -E "host bridge\|ranges\|root bus resource\|ECAM"` |
| 주소 트리 | `sudo cat /proc/iomem` (root 아니면 주소가 0으로 가려짐), `cat /proc/ioports` |
| BAR / 창 | `lspci -vv -s <BDF>` 의 `Region N:`, `Memory behind bridge`, `Prefetchable memory behind bridge`, `I/O behind bridge`, `Bus: primary= secondary= subordinate=` |
| 원시 BAR (커널이 지운 BAR도) | `lspci -b -vv -s <BDF>` (bus-centric, config space 값 그대로), `setpci -s <BDF> BASE_ADDRESS_2.l` |
| sysfs 리소스 | `cat /sys/bus/pci/devices/<D>/resource` (start end flags × 13줄: BAR0–5, ROM, bridge 창), `resource0..5` 파일(mmap용) |
| SW 재열거 | `echo 1 > /sys/bus/pci/devices/<D>/remove`, `echo 1 > /sys/bus/pci/rescan` |
| 슬롯 | `ls /sys/bus/pci/slots/`, `lspci -vv`의 `SltCap/SltSta`, `/sys/bus/pci_express/devices/*:pcie004` |
| 버스 번호 | `cat /sys/bus/pci/devices/<bridge>/secondary_bus_number`, `subordinate_bus_number` |

### 3.2 실측 — 정상 열거 dmesg (QEMU virt, 펌웨어 없음, 발췌 + 해석)

```text
pci-host-generic 4010000000.pcie: host bridge /pcie@10000000 ranges:
pci-host-generic 4010000000.pcie:       IO 0x003eff0000..0x003effffff -> 0x0000000000
pci-host-generic 4010000000.pcie:      MEM 0x0010000000..0x003efeffff -> 0x0010000000
pci-host-generic 4010000000.pcie:      MEM 0x8000000000..0xffffffffff -> 0x8000000000
pci-host-generic 4010000000.pcie: ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]
pci_bus 0000:00: root bus resource [bus 00-ff]
pci 0000:00:01.0: [1b36:000c] type 01 class 0x060400 PCIe Root Port
pci 0000:00:01.0: PCI bridge to [bus 00]
pci 0000:00:01.0: bridge configuration invalid ([bus 00-00]), reconfiguring
pci 0000:01:00.0: [1b36:0010] type 00 class 0x010802 PCIe Endpoint
pci 0000:01:00.0: BAR 0 [mem 0x00000000-0x00003fff 64bit]
pci_bus 0000:01: busn_res: [bus 01-ff] end is updated to 01
pci 0000:04:00.0: [8086:10d3] type 00 class 0x020000 PCIe Endpoint
pci 0000:04:00.0: BAR 0 [mem 0x00000000-0x0001ffff]
pci 0000:04:00.0: BAR 2 [io  0x0000-0x001f]
pci 0000:04:00.0: ROM [mem 0x00000000-0x0007ffff pref]
pci_bus 0000:02: busn_res: [bus 02-ff] end is updated to 05
pci 0000:00:01.0: bridge window [mem 0x00100000-0x000fffff 64bit pref] to [bus 01] add_size 200000 add_align 100000
pci 0000:00:01.0: bridge window [mem 0x10000000-0x101fffff]: assigned
pci 0000:00:01.0: bridge window [mem 0x8000000000-0x80001fffff 64bit pref]: assigned
pci 0000:01:00.0: BAR 0 [mem 0x10000000-0x10003fff 64bit]: assigned
pci 0000:00:01.0: PCI bridge to [bus 01]
pci 0000:00:01.0:   bridge window [mem 0x10000000-0x101fffff]
pcieport 0000:00:01.0: enabling device (0000 -> 0003)
e1000e 0000:04:00.0: enabling device (0000 -> 0002)
```

| 줄 | 해석 |
|---|---|
| `ranges:` 3줄 | DT `ranges` → **I/O 64K, MEM32 ~752M, MEM64 512G** 창. 이게 배정 가능한 전체 공간 |
| `ECAM at ... for [bus 00-ff]` | config 공간 256 MiB = 256 bus × 1 MiB |
| `[1b36:000c] type 01 class 0x060400 PCIe Root Port` | vendor:device, header type 1 (bridge), class 0x0604 (PCI bridge) |
| `bridge configuration invalid ([bus 00-00]), reconfiguring` | 펌웨어가 버스 번호를 안 써 둠 → 커널이 2nd pass에서 배정 |
| `BAR 0 [mem 0x00000000-0x00003fff 64bit]` | sizing 결과 16 KiB (아직 주소 없음 → 0 기준 범위로 표시) |
| `busn_res: [bus 01-ff] end is updated to 01` | subordinate=FF로 열었다가 실제 끝(01)으로 줄임 = DFS |
| `add_size 200000 add_align 100000` | hotplug 가능 슬롯이라 2 MiB 추가 예약, 1 MiB 정렬 |
| `bridge window [...]: assigned` → `BAR 0 [...]: assigned` | 창 먼저(위→아래), 그 안에 BAR |
| `enabling device (0000 -> 0003)` | Command 레지스터 Memory+IO decode 켬 (port 드라이버). e1000e는 `0002` (mem만) |

### 3.3 실측 — `/proc/iomem` 와 `lspci -vv` Region 줄

```text
10000000-3efeffff : pcie@10000000
  10000000-101fffff : PCI Bus 0000:01
    10000000-10003fff : 0000:01:00.0                    <- NVMe BAR0 (드라이버 없음 -> 소유자 이름 없음)
  10200000-106fffff : PCI Bus 0000:02
    10200000-106fffff : PCI Bus 0000:03
      10200000-104fffff : PCI Bus 0000:04
        10200000-1027ffff : 0000:04:00.0               <- e1000e ROM 512K
        10280000-1029ffff : 0000:04:00.0
          10280000-1029ffff : e1000e                   <- 드라이버가 request_mem_region 한 표시
      10500000-106fffff : PCI Bus 0000:05
        10500000-105fffff : 0000:05:00.0               <- edu BAR0 1M
  10700000-108fffff : PCI Bus 0000:06                  <- 빈 슬롯 hotplug 예약
4010000000-401fffffff : PCI ECAM
8000000000-ffffffffff : pcie@10000000
  8000000000-80001fffff : PCI Bus 0000:01              <- prefetchable 창 (자식 없어도 hotplug 예약 2M)
```

```text
01:00.0 Non-Volatile memory controller: Red Hat, Inc. QEMU NVM Express Controller (rev 02) (prog-if 02 [NVM Express])
	Control: I/O- Mem- BusMaster- ...
	Region 0: Memory at 10000000 (64-bit, non-prefetchable) [disabled] [size=16K]
04:00.0 Ethernet controller: Intel Corporation 82574L Gigabit Network Connection
	Control: I/O- Mem+ BusMaster+ ...
	Region 0: Memory at 10280000 (32-bit, non-prefetchable) [size=128K]
	Region 2: I/O ports at 2000 [disabled] [size=32]
	Expansion ROM at 10200000 [virtual] [disabled] [size=512K]
00:02.0 PCI bridge: Red Hat, Inc. QEMU PCIe Root port
	Bus: primary=00, secondary=02, subordinate=05, sec-latency=0
	I/O behind bridge: 2000-4fff [size=12K] [16-bit]
	Memory behind bridge: 10200000-106fffff [size=5M] [32-bit]
```

- `[disabled]` = 그 장치 Command 레지스터의 **Memory Space(또는 I/O) decode가 꺼짐**. NVMe는 lab 커널에서 nvme가 모듈(=m)이고 로드 안 됨 → 아무도 `pci_enable_device()` 안 함 → `Mem-`. 주소는 배정됐지만 아직 응답 안 함. e1000e는 I/O BAR만 `[disabled]` (드라이버가 mem만 enable: `0000 -> 0002`).
- `/proc/iomem` 에서 BDF 아래에 **드라이버 이름**(`e1000e`)이 있으면 드라이버가 그 영역을 claim한 것.
- Lima VM(Apple VZ, ACPI)은 bridge 없이 bus 0에 virtio 장치들이 바로 붙음 (`380000000-77fffffff : PCI Bus 0000:00`, 각 virtio BAR0 64 KiB 64-bit). 로그: [lima_vm_lspci.log](onsitePrep/lp04/qemu_lab/lima_vm_lspci.log).

### 3.4 실측 — 배정 실패 (`highmem=off` + 1 GiB BAR) 와 `pci=realloc`

```text
$ lspci -vv -s 01:00.0
01:00.0 RAM memory: Red Hat, Inc. QEMU Inter-VM shared memory device (rev 01)
	Control: I/O- Mem+ BusMaster- ...
	Region 0: Memory at 10000000 (32-bit, non-prefetchable) [size=256]
                                                     <- BAR 2 줄이 아예 없음!
$ lspci -b -vv -s 01:00.0
	Region 0: Memory at 10000000 (32-bit, non-prefetchable)
	Region 2: Memory at <unassigned> (64-bit, prefetchable)
$ setpci -s 01:00.0 BASE_ADDRESS_2.l BASE_ADDRESS_3.l
0000000c                                             <- 주소 0 + type 비트(64-bit pref)만
00000000
$ cat /sys/bus/pci/devices/0000:01:00.0/resource | sed -n 3p
0x0000000000000000 0x0000000000000000 0x0000000000000000   <- 커널이 리소스를 비워버림
$ lspci -s 00:01.0 -vv | grep Prefetchable
	Prefetchable memory behind bridge: [disabled] [64-bit]
```

- **해석**: 실패한 BAR는 커널이 resource를 지워서 **기본 `lspci -vv`(sysfs 기반)에서 Region 줄이 사라진다** → "BAR가 원래 없는 장치"로 오해하기 쉬움. **`lspci -b`** 로 원시 config 값을 보면 `<unassigned>`.
- **`pci=realloc` 실측** ([highmem_off_pci_realloc.log](onsitePrep/lp04/qemu_lab/highmem_off_pci_realloc.log)): `bridge window [mem 0x10000000-0x101fffff]: releasing` … `00:02.0: bridge window ... releasing` (다른 bridge 창까지 풀고 재배치 시도) → 그래도 `BAR 2 [mem size 0x40000000 64bit pref]: can't assign; no space` 8회. **realloc은 "공간은 있는데 배치가 비효율적일 때" 고치는 것이지, 창 자체(752 MiB)보다 큰 BAR는 못 구한다.**
- 결론: fix = **창을 키운다** (QEMU면 `highmem=on`, SoC면 DT `ranges`에 64-bit pref 창, x86이면 BIOS "Above 4G decoding" / "MMIO high size" 설정).

### 3.5 sysfs `resource` 파일 읽는 법

- 13줄 = BAR0..BAR5, ROM, (bridge면) 창 3~4개. 각 줄 = `start end flags`. flags 하위 비트는 `IORESOURCE_*` (예: `0x40200` = MEM `0x200` | SIZEALIGN 등 [추정: 비트 조합 해석]). 실패한 BAR는 `0 0 0`.
- `resourceN` 파일은 user space에서 **mmap해서 BAR에 직접 접근**하는 통로 (UIO/테스트 툴, LP06·LP13).

## 4. 꼬리 질문 체인

### 체인 1 — "Walk me through enumeration."

Q → 꼬리1 "How does BAR sizing work?" → 꼬리2 "What if a 64-bit BAR needs more space than the bridge window?" → 꼬리3 "What does pci=realloc do?" → 꼬리4 "Why must a BAR be naturally aligned?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — (TL;DR 30초 답) "It's a depth-first walk with config reads. At each bus/device the Vendor ID tells you if something's there — 0xFFFF is nothing, 0x0001 with RRS visibility means retry. A bridge gets primary and secondary programmed with subordinate at 0xFF, you recurse, then shrink subordinate. Then you size BARs, compute bridge windows bottom-up and assign addresses top-down."
  - 한국어 핵심: Vendor ID read DFS, bridge는 sub=FF로 열고 내려갔다 줄임, BAR sizing → 창 크기(아래→위) → 주소 배정(위→아래).
- **꼬리1.** — "Disable memory and I/O decode in the Command register, save the BAR, write all ones, read it back. The device hard-wires the address bits below its size to zero, so after masking the low type bits — four for memory, two for I/O — the size is the bitwise NOT plus one. For a 64-bit BAR you do the upper dword too and compute in 64 bits. Then restore. For example, a readback of fff00000 is a 1 MB 32-bit non-prefetchable BAR; ffffc004 with an upper of ffffffff is a 16 KB 64-bit BAR — those are the actual values I read from QEMU's edu and NVMe devices."
  - 한국어 핵심: decode off → all-1s 쓰고 읽기 → type 비트 마스크 → `~v+1` → 복원. 64-bit는 상위까지.
- **꼬리2.** — "Then it can't be assigned: the kernel prints 'can't assign; no space' and 'failed to assign' for the bridge window and the BAR, and the device shows up in lspci but the driver fails to enable it. First question is whether the BAR is prefetchable — only the prefetchable window can go above 4 GB; a 64-bit non-prefetchable BAR behind a bridge is stuck below 4 GB. Then whether the host bridge even has a big enough window — on an ARM SoC that's the DT ranges, on x86 the 'above 4G decoding' setting and the ACPI _CRS."
  - 한국어 핵심: no space → 드라이버 실패. pref인지(4 GiB 위 가능), 호스트 창(DT ranges / Above 4G) 확인.
- **꼬리3.** — "It lets the kernel release bridge windows that firmware or the first pass assigned and resize them to fit everything, including hotplug reservations. It fixes bad packing — windows too small or in the wrong place — but it can't create address space. I reproduced that in QEMU: with the 64-bit window removed, a 1 GB BAR still fails with realloc, you can see the kernel releasing windows and retrying."
  - 한국어 핵심: 창을 풀고 다시 크게 배치 — 배치 문제는 고치지만 공간 자체 부족은 못 고침 (QEMU 실측).
- **꼬리4.** — "Because the device decodes by comparing only the upper address bits — the low bits are hard-wired to zero, so the base must be a multiple of the size. That's also why sizes are powers of two, and why a few small BARs can waste a lot of space once you add 1 MB bridge-window granularity."
  - 한국어 핵심: 하위 비트 고정 → base는 크기의 배수. 2의 거듭제곱. bridge 1 MiB 단위라 낭비 발생.

</details>

### 체인 2 — "A device trained late, after boot. How does it appear?"

Q → 꼬리1 "What's the difference between rescan and hotplug?" → 꼬리2 "What does the kernel do on surprise removal?" → 꼬리3 "Rescan found it but the BAR failed. Why?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "It doesn't, by itself. At boot the config read below that root port returned all ones, so there's no pci_dev. If the slot is hotplug-capable and pciehp owns it, the Data Link Layer State Changed event fires when the link comes up and pciehp enumerates it. Otherwise I'd do a rescan from sysfs once the root port shows DLActive."
  - 한국어 핵심: 자동으로는 안 나옴. pciehp 슬롯이면 DLL State Changed로 자동, 아니면 DLActive 확인 후 rescan.
- **꼬리1.** — "Rescan is purely software — it re-walks the buses and assigns resources for anything new, but it doesn't touch the link; if the link isn't up, rescan finds nothing. Hotplug is a slot-level mechanism: the port has presence detect, maybe power control and attention buttons, it raises interrupts, and pciehp powers the slot, waits for link up, enumerates, and on removal tears it down. Remove plus rescan is my quick way to re-probe a device without rebooting."
  - 한국어 핵심: rescan = SW 재스캔(링크 무관), hotplug = 슬롯 이벤트 기반으로 전원·링크·열거·제거 관리.
- **꼬리2.** — "pciehp sees link down or presence lost, logs 'Link Down' / 'Card not present', marks the device and everything below it disconnected so further config accesses return all ones without touching hardware, and calls the driver's remove. Drivers have to tolerate MMIO reads returning 0xFFFFFFFF in the meantime. If it was an uncorrectable error rather than a clean unplug, AER reports Surprise Down and DPC may have already contained the port."
  - 한국어 핵심: Link Down 로그 → `pci_dev_set_disconnected` → 드라이버 remove. 드라이버는 0xFFFFFFFF 읽기를 견뎌야. AER/DPC 연계.
- **꼬리3.** — "Because at boot there was nothing below that port, so the bridge got only the default hotplug reservation — 2 MB of memory and prefetchable by default — and the new device's BAR is bigger. Fix with pci=hpmemsize, pci=realloc, or reserving windows in firmware or the DT; on QEMU there are mem-reserve and pref64-reserve properties on the root port for the same purpose."
  - 한국어 핵심: 빈 슬롯은 기본 2 MiB 예약만 → 큰 BAR 실패. hpmemsize/realloc/펌웨어 예약.

</details>

### 체인 3 — "Device is visible but the driver fails with 'can't assign BAR'. Root cause?"

Q → 꼬리1 "How would you fix it on an ARM SoC?" → 꼬리2 "And on x86?" → 꼬리3 "Why does lspci show no Region line for it?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "The device enumerated, so the link and config path are fine — it's an address space problem. I'd read dmesg for the 'no space' line to get the size and type, look at /proc/iomem to see what windows exist and how full they are, and check the bridge chain above the device: each bridge's prefetchable window has to cover it. Common causes are a big 64-bit prefetchable BAR with no 64-bit window, or a hotplug slot with too small a reservation."
  - 한국어 핵심: 열거 OK → 주소 공간 문제. dmesg 크기/타입, /proc/iomem, 위쪽 bridge 창 확인.
- **꼬리1.** — "The host bridge windows come from the DT ranges property — I'd check the SoC's address map and the controller's outbound translation windows, and add or enlarge a 64-bit prefetchable range, flags 0x43000000. Also bus-range if a switch is added. In QEMU I reproduced exactly this by turning off highmem: the 64-bit range disappears and a 1 GB BAR fails."
  - 한국어 핵심: DT `ranges`에 64-bit pref 창 추가/확대 (SoC 주소맵·iATU 확인), 필요시 `bus-range`.
- **꼬리2.** — "On x86 the firmware did the assignment, so enable 'Above 4G decoding' or increase MMIO high size in the BIOS, check the ACPI _CRS windows the kernel reports as root bus resources, and if firmware packed badly, pci=realloc."
  - 한국어 핵심: BIOS Above 4G decoding / MMIO 크기, `_CRS`, `pci=realloc`.
- **꼬리3.** — "Because the kernel clears the resource when assignment fails, and plain lspci takes BARs from sysfs. lspci -b shows the raw config value — 'Region 2: Memory at unassigned' — which proves the BAR exists and just wasn't placed."
  - 한국어 핵심: 실패 시 커널이 resource를 0으로 → 기본 lspci에서 사라짐. `lspci -b`로 원시값 확인.

</details>

### 체인 4 (추가) — "Who does enumeration on your platform — firmware or Linux?"

Q → 꼬리1 "Why would you want Linux to keep firmware's assignment?" → 꼬리2 "On bare metal, what's the minimum you'd implement?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "On x86, firmware enumerates and assigns everything, and Linux claims those assignments and only fills gaps. On an ARM SoC with a device-tree generic host bridge, Linux scans and reassigns everything itself — the host probe code literally says 'if we didn't claim, this will reassign everything' — unless the DT has linux,pci-probe-only. ACPI arm64 systems can ask Linux to preserve via the PCI boot configuration _DSM."
  - 한국어 핵심: x86 = 펌웨어 + claim / ARM DT = Linux 전부 재배정 (예외 probe-only, ACPI _DSM 5).
- **꼬리1.** — "When firmware or another agent depends on those addresses — a boot device, a display, a hypervisor, an always-on co-processor doing DMA, or secure firmware that has the BAR address baked in. Moving a BAR under them breaks things silently."
  - 한국어 핵심: 부팅 장치, 디스플레이, 하이퍼바이저, 다른 코어가 주소를 이미 쓰는 경우.
- **꼬리2.** — "ECAM accessors, a depth-first scan with Vendor ID and RRS handling, bus number programming, BAR sizing, a simple allocator that aligns each BAR to its size and each bridge window to 1 MB, and then enabling memory decode and bus mastering. That's what my enum_sim program does on a fake ECAM: root port, a switch with three downstream ports, two endpoints, one port with the link down."
  - 한국어 핵심: ECAM 접근, DFS + RRS, 버스 번호, BAR sizing, 정렬 allocator, Command enable — enum_sim.c.

</details>

### 체인 5 (추가) — "How many bus numbers and how much MMIO does a 4-port switch consume?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Each port is a bridge with its own BDF, so a switch with one upstream and three downstream ports is four functions. Bus numbers: one for the internal bus between the upstream and downstream ports, plus one per downstream port's link, so at least four — more if there are switches below or the kernel reserves extra for hotplug. MMIO: each downstream port window is at least 1 MB granular per type in use, plus hotplug reservations, so even small endpoints cost a few megabytes, and the upstream port window has to cover the sum."
  - 한국어 핵심: BDF 4개, 버스 ≥4 (내부 1 + DSP별 1), 창은 1 MiB 단위 + hotplug 예약.

</details>

## 5. Don 경험 연결

- "On the Solidigm Gen6 SSD controller I was on the endpoint side of this: during pre-silicon FPGA bring-up we had to make sure the BAR sizes and capability structures the host would see were right before RTL freeze — a wrong BAR size or a non-prefetchable 64-bit BAR shows up exactly as these 'no space' failures on the host. [숫자 채우기: BAR 구성 / 발견한 이슈]"
- "At SK hynix the chamber automation had to survive devices dropping off the bus at temperature corners — I'd detect it by config reads returning all ones and recover with remove and rescan or a reset, and log it as a failure rather than hang the test. [숫자 채우기]"
- "At Apple, on wireless chipset integration, enumeration timing was part of the root-cause work — a device whose firmware isn't ready answers with retry status, and if the host gives up early it simply isn't there. [사례 채우기]"

## 6. 실습 / 코드

### 6.1 [enum_sim.c](onsitePrep/lp04/enum_sim.c) — bare-metal 스타일 열거 시뮬레이터 (fake ECAM)

- 에뮬레이트하는 HW: 호스트 브리지 + **Root Port 1 → Switch (USP + DSP0/DSP1/DSP2)** → EP A(가속기: 1 MiB mem32 + **256 MiB 64-bit pref**), EP B(NIC: 128 KiB mem32 + 32 B I/O + 16 KiB 64-bit non-pref, **RRS 3회**), DSP2 = link down, Root Port 2 = 빈 슬롯. config 접근은 **실제 Type 1 bridge처럼 라우팅** (bus == secondary → 링크 위 dev 0, secondary < bus ≤ subordinate → 더 아래로, link down → all-1s). BAR 레지스터는 "크기 아래 비트 0 고정 + type 비트 RO" 의미로 에뮬레이트.
- "펌웨어" 쪽: DFS 스캔 + RRS 재시도 + 버스 번호(sub=FF → 축소) + BAR sizing(decode off, all-1s, 복원) + 1-pass 배정(BAR = 자기 크기 정렬, 창 = 1 MiB/4 KiB 정렬, 빈 창은 base>limit로 닫기) + Command enable. 자체 검증 CHECK 20여 개.
- macOS 컴파일·실행 **ALL PASS** [확인됨]: `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined enum_sim.c -o /tmp/enum_sim && /tmp/enum_sim`

```text
== 1. depth-first scan + BAR sizing
    03:00.0 BAR0: wrote ffffffff, read fff00000 -> mem32 size 0x100000
    03:00.0 BAR2: wrote ffffffff:ffffffff, read ffffffff:f000000c -> 64-bit pref size 0x10000000
    04:00.0 answered RRS (Vendor ID 0x0001) - device not ready, retry 1
    04:00.0 answered RRS (Vendor ID 0x0001) - device not ready, retry 2
    04:00.0 answered RRS (Vendor ID 0x0001) - device not ready, retry 3
    04:00.0 BAR0: wrote ffffffff, read fffe0000 -> mem32 size 0x20000
    04:00.0 BAR1: wrote ffffffff, read ffffffe1 -> I/O size 0x20
    04:00.0 BAR2: wrote ffffffff:ffffffff, read ffffffff:ffffc004 -> 64-bit size 0x4000

== 3. result (like lspci -tv + Region / behind-bridge lines)
00:00.0 [1b36:0008] Host bridge
00:01.0 [1b36:000c] Root Port 1  bus: primary=00 secondary=01 subordinate=05
  I/O behind bridge: 1000-1fff
  Memory behind bridge: 10000000-101fffff
  Prefetchable memory behind bridge: 8000000000-800fffffff
    01:00.0 [104c:8232] Switch USP  bus: primary=01 secondary=02 subordinate=05
      ...
        02:00.0 [104c:8233] Switch DSP0  bus: primary=02 secondary=03 subordinate=03
          Memory behind bridge: 10000000-100fffff
          Prefetchable memory behind bridge: 8000000000-800fffffff
            03:00.0 [1d0f:a000] EP A (accel)
              Region 0: Memory at 10000000 [size=0x100000] 32-bit
              Region 2: Memory at 8000000000 [size=0x10000000] 64-bit pref
        02:01.0 [104c:8233] Switch DSP1  bus: primary=02 secondary=04 subordinate=04
          I/O behind bridge: 1000-1fff
          Memory behind bridge: 10100000-101fffff
            04:00.0 [8086:10d3] EP B (nic, slow FW)
              Region 0: Memory at 10100000 [size=0x20000] 32-bit
              Region 1: I/O ports at 1000 [size=0x20]
              Region 2: Memory at 10120000 [size=0x4000] 64-bit
        02:02.0 [104c:8233] Switch DSP2 (link down)  bus: primary=02 secondary=05 subordinate=05
          (no windows: nothing behind this bridge)
00:02.0 [1b36:000c] Root Port 2 (empty slot, link down)  bus: primary=00 secondary=06 subordinate=06
  (no windows: nothing behind this bridge)

config reads issued: 161, highest bus: 6
ALL PASS (0 failures)
```

- 읽을 포인트: (1) 64-bit **non-pref** BAR(EP B BAR2)는 32-bit mem 창(0x1012_0000)에, 64-bit **pref**(EP A BAR2)는 high 창(0x80_0000_0000)에. (2) link down DSP2와 빈 RP2도 **버스 번호는 받지만 창은 닫힘**. (3) EP B는 RRS 3번 후 정상 열거. (4) 0x10000000 + 1 MiB(EP A) 다음 EP B가 **1 MiB 경계(0x10100000)** 에서 시작 = DSP1 창 정렬.
- Linux와의 차이: Linux는 크기 계산(아래→위, `pci_bus_size_bridges`)과 배정(위→아래)을 **분리**하고 큰 BAR부터 정렬해 낭비를 줄이며, hotplug 예약(add_size)과 realloc 재시도가 있다. 시뮬레이터는 펌웨어/U-Boot 수준의 단순 1-pass.

### 6.2 QEMU 실측 재현

- 정상 토폴로지 + remove/rescan + 손 BAR sizing: [run1_lp03.sh](onsitePrep/lp03/qemu_lab/run1_lp03.sh), [extra_lp03.sh](onsitePrep/lp03/qemu_lab/extra_lp03.sh) → [run2_setpci_bar_rescan.log](onsitePrep/lp03/qemu_lab/run2_setpci_bar_rescan.log).
- 배정 실패: [run2_lp04.sh](onsitePrep/lp04/qemu_lab/run2_lp04.sh) (`-machine virt,highmem=off -object memory-backend-ram,id=hm,size=1G -device pcie-root-port,id=rp1,chassis=1,slot=1 -device ivshmem-plain,memdev=hm,bus=rp1`), 인자로 `pci=realloc` 추가 가능 → [highmem_off_bar_no_space.log](onsitePrep/lp04/qemu_lab/highmem_off_bar_no_space.log), [highmem_off_pci_realloc.log](onsitePrep/lp04/qemu_lab/highmem_off_pci_realloc.log).
- Lima VM 원본 lspci/iomem: [lima_vm_lspci.log](onsitePrep/lp04/qemu_lab/lima_vm_lspci.log).
- 작업 위치: VM 루트 fs가 read-only(emergency_ro)라 `/tmp/LP03` 사용.

### 6.3 hotplug 랩 레시피 `[미실행]`

- 필요: `CONFIG_HOTPLUG_PCI_PCIE=y` 커널 (lab 커널은 전부 미설정, bool이라 모듈 불가 → 커널 재빌드 필요, 무거운 빌드라 보류).
- 절차: QEMU를 `-monitor unix:/tmp/LP04/mon.sock,server,nowait` 로 띄움 → 빈 root port(`rp3`)에 `device_add nvme,id=hp1,serial=hp,bus=rp3` → guest dmesg 기대: `pcieport 0000:00:03.0: pciehp: Slot(3): Card present`, `Slot(3): Link Up`, 이어서 `pci 0000:06:00.0: [1b36:0010] ...`, `BAR 0 ...: assigned` (빈 슬롯에 2 MiB 예약이 있어서 16 KiB BAR는 성공할 것) → `device_del hp1` → `Slot(3): Link Down`, `Card not present` [기대값, 미검증].
- 변형: `pref64-reserve=` / `mem-reserve=` 를 작게 하고 큰 BAR 장치(ivshmem 1 GiB)를 hotplug → `can't assign; no space` 재현 → `pci=hpmmioprefsize=2G` 로 해결 확인.

## 7. 자주 틀리는 것

1. **"rescan하면 링크가 다시 올라온다"** → rescan은 SW 재스캔. 링크가 DL_Active가 아니면 아무것도 못 찾는다. 링크 문제는 retrain/reset (LP03, LP11).
2. **Vendor ID 0xFFFF = "장치 고장"** → 0xFFFF는 **응답 없음**(없음/링크 다운/UR). 이미 열거된 장치가 0xFFFF면 링크가 죽은 것 (`rev ff`).
3. **RRS(0x0001)를 장치 없음으로 처리** → "준비 중"이니 재시도해야 함. bare-metal 열거기가 RRS를 모르면 느린 장치를 놓친다.
4. **BAR sizing 때 decode를 안 끔 / 복원 안 함** → all-1s 주소가 잠깐 디코드돼 충돌, 또는 장치가 엉뚱한 주소에 남음.
5. **64-bit BAR = 4 GiB 위에 놓을 수 있다** → bridge 뒤라면 **prefetchable이어야** 64-bit 창에 감. 64-bit non-pref는 32-bit 창 안 (실측 NVMe BAR0 = 0x10000000).
6. **`pci=realloc`이 만능** → 공간이 없으면 못 고침 (실측). 창을 키워야 한다.
7. **기본 lspci에 Region이 없으면 BAR가 없는 것** → 배정 실패 시 커널이 리소스를 지움. `lspci -b` / `setpci BASE_ADDRESS_N`.
8. **ARM에서도 펌웨어(U-Boot) 배정을 Linux가 따른다** → DT generic host는 기본으로 **전부 재배정**. 유지하려면 `linux,pci-probe-only`.
9. **빈 슬롯은 자원을 안 쓴다** → hotplug 슬롯은 버스 번호 + 2 MiB mem + 2 MiB pref + I/O 4 KiB를 예약 (실측 Bus 06).
10. **`[disabled]` = 배정 실패** → `[disabled]`는 Command decode off(드라이버가 아직 enable 안 함). 주소는 배정돼 있다. 실패는 `<unassigned>` 또는 Region 줄 없음.

## 8. 출처

1. Linux v6.18 `include/uapi/linux/pci_regs.h` (BAR, bridge window 레지스터 오프셋), `drivers/pci/pci.h` (RRS vendor ID `PCI_VENDOR_ID_PCI_SIG`) — 랩 VM `/var/lab/src/linux-6.18.55` 직접 확인 · https://elixir.bootlin.com/linux/v6.18/source/include/uapi/linux/pci_regs.h
2. Linux v6.18 `drivers/pci/probe.c` (pci_scan_child_bus_extend 2-pass, only_one_child, pci_bus_wait_rrs, pci_bus_generic_read_dev_vendor_id, pci_host_probe "reassign everything", set_pcie_hotplug_bridge, RRS SV enable), `drivers/pci/setup-res.c` (can't assign / failed to assign 메시지), `drivers/pci/setup-bus.c` (bridge window add_size, realloc), `drivers/pci/of.c` (of_pci_preserve_config, linux,pci-probe-only), `drivers/pci/pci-acpi.c` (DSM_PCI_PRESERVE_BOOT_CONFIG) — https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/probe.c · https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/setup-res.c
3. Linux v6.18 `drivers/pci/pci.c` (pci= 파싱: realloc, hp*size, 기본 hotplug 크기), `arch/x86/pci/i386.c` (pcibios_resource_survey, fs_initcall pcibios_assign_resources), `arch/x86/pci/common.c` (assign-busses, nocrs, use_crs), `Documentation/admin-guide/kernel-parameters.txt` (pci=, pcie_ports=) — https://docs.kernel.org/admin-guide/kernel-parameters.html
4. Linux v6.18 `drivers/pci/hotplug/pciehp_ctrl.c` (Slot Link Up/Down, Card present/not present), `pciehp_core.c` (pciehp_poll_mode, pci_dev_set_disconnected) — https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/hotplug/pciehp_ctrl.c
5. Linux `Documentation/ABI/testing/sysfs-bus-pci` (rescan, remove, resource) · `Documentation/PCI/sysfs-pci.rst` — https://docs.kernel.org/PCI/sysfs-pci.html
6. QEMU 10.2.1 `-device pcie-root-port,help` (bus-reserve, mem-reserve, pref32/64-reserve, hotplug, x-speed/x-width), QEMU virt DT (`/var/lab/out/virt.dts`, `pci-host-ecam-generic` ranges/bus-range)
7. 실측 로그 (이 노트용 QEMU/Lima 실행): [onsitePrep/lp03/qemu_lab/](onsitePrep/lp03/qemu_lab/run1_boot_lspci_iomem.log), [onsitePrep/lp04/qemu_lab/](onsitePrep/lp04/qemu_lab/highmem_off_bar_no_space.log)
8. [LP03 · 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · [N01 · PCIe 링크 디버그](2026-10-31_N01_pcie_lspci_ltssm.html)

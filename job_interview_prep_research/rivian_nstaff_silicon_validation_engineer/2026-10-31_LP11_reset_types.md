# LP11 · 리셋: PERST# · hot reset · FLR · link disable
<!-- theme: pastel -->

> **묶음**: D 고급 기능 · **우선**: P1 (검증 루프 설계 질문 "어떤 리셋을 쓰겠나"에 직결) · **선행 노트**: [LP10 전원 관리](2026-10-31_LP10_power_mgmt_aspm.html) · [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · [LP04 열거](2026-10-31_LP04_enumeration_resources_hotplug.html) · **다음 노트**: [LP12 SR-IOV/VFIO](2026-10-31_LP12_sriov_vfio_acs_p2p.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [N01 lspci/LTSSM](2026-10-31_N01_pcie_lspci_ltssm.html) · **코드**: [onsitePrep/lp11/reset_loop.sh](onsitePrep/lp11/reset_loop.sh) (QEMU에서 실행 ✅ — flr/pm/bus/sbr 전부)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "PCIe 리셋 종류는?", "각각 무엇을 리셋하고 무엇을 안 하나?", "검증 루프에 어떤 리셋을 쓰겠나?", "리셋 직후 장치가 한동안 0xFFFF를 돌려주는 이유는?", "Linux에서 리셋을 어떻게 거나?"
- **외울 것 1 — 범위가 큰 순서**: **Conventional reset** (cold = 전원 켜기, warm = 전원 유지한 채 **PERST#** 핀) ⊃ **Hot reset** (= **Secondary Bus Reset**: 위쪽 bridge의 Bridge Control 0x3E **bit 6** → 링크로 TS1 "Hot Reset" 비트 전파 → 아래 전부) ≈ **Link Disable** (LnkCtl **bit 4**, 링크 끊었다 다시) ⊃ **FLR** (Function Level Reset: DevCtl **bit 15**, 함수 하나만, **링크는 L0 유지**) ≈ **PM reset** (D3hot→D0, No_Soft_Reset=0일 때).
- **외울 것 2 — 시간 규칙**: PERST# 해제는 전원 안정 후 **100 ms** (T_PVPERL), refclk는 그보다 **100 µs** 먼저 (T_PERST-CLK). 리셋 후 첫 config 요청은 **100 ms 뒤** (5 GT/s 초과 링크면 **링크가 DL_Active 된 뒤 100 ms**). 장치는 준비 안 됐으면 **RRS (Request Retry Status, 옛 이름 CRS)** 로 "다시 물어봐" 응답 가능, SW는 **최소 1 s**는 기다려 줘야 한다. FLR도 **100 ms** 이내 완료.
- **외울 것 3 — 리셋이 안 건드리는 것**: **sticky 비트** (AER 에러 상태·header log 등, aux 전원 있으면 warm reset도 통과), FLR은 **링크와 다른 function**, 그리고 **장치 밖 상태** (IOMMU 매핑, 드라이버 메모리). Linux `/sys/bus/pci/devices/<BDF>/reset` 은 `reset_method` 순서 (**device_specific → acpi → flr → af_flr → pm → bus → cxl_bus**) 중 첫 번째 가능한 것 + **config 저장/복원**까지 해 준다.
- **QEMU 실측 (6.18.55 커널)**: FLR 110–130 ms (100 ms sleep 포함), PM 30–40 ms (D3hot 10 ms × 2), bus(SBR) 140–160 ms (root port, DLLLARC 있음), 스위치 downstream port SBR **1.13 s** (DLLLARC 없음 → 커널이 1000+100 ms 고정 대기). setpci로 커널 몰래 SBR 하면 **BAR0이 0으로 날아감** (실측).
- **30초 영어 답 ("Which reset would you use in a validation loop and why?")**: "It depends on what I'm trying to cover. For the link and the PHY — training, equalization, speed changes — I need resets that retrain the link: PERST#-driven warm reset for the full sequence, or hot reset through the Secondary Bus Reset bit, which is easy to loop from software. For function-level recovery — driver reset paths, firmware re-init — FLR is the right one because it's scoped to one function and keeps the link up, so it's fast and isolates function bugs from link bugs. Cold power cycles I run fewer of, but they're the only thing that covers power sequencing and sticky-bit behavior. In every loop I check after reset: device ID readable, config restored, link speed and width back to expected, AER clean, and the time it took to become ready."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 아파트 단지와 리셋 버튼

- PCIe 장치 트리는 **아파트 단지**: root complex = 관리사무소, root port/switch = 각 동으로 가는 **다리(bridge)**, endpoint = **세대**, function = 세대 안의 **방**(한 장치에 function이 여러 개 있을 수 있음).

| 비유 | PCIe 리셋 | 범위 | 링크는? |
|---|---|---|---|
| 단지 전체 정전 후 재가동 | **Cold reset** (전원 투입) | 전부 | 처음부터 training |
| 전기는 둔 채 관리실 "전체 리셋" 스위치 | **Warm reset** = **PERST#** 핀 assert | 그 핀이 연결된 장치 전부 | 처음부터 training |
| 다리 입구에서 "이 동 전체 재부팅" 방송 | **Hot reset** (Secondary Bus Reset) | 그 bridge 아래 전부 | 다시 training |
| 다리를 막았다가 다시 엶 | **Link Disable** → enable | 그 링크 아래 전부 (링크 다운 = 리셋 효과) | 다시 training |
| 한 방만 공장 초기화, 다리·이웃 방은 그대로 | **FLR** (Function Level Reset) | function 하나 | **L0 유지** |
| 그 방 전기만 껐다 켬 | **PM reset** (D3hot → D0) | function 하나 (장치가 허용할 때) | L1 갔다 L0 |

- **왜 종류가 많나?** 범위가 클수록 확실하지만 느리고 부작용이 크다. 다른 function이 일하고 있는데 링크를 끊으면 그쪽도 죽는다 → 가상화(VM에 VF 하나 넘김)에선 **FLR** 같은 작은 리셋이 필수 ([LP12](2026-10-31_LP12_sriov_vfio_acs_p2p.html)).

### 1.2 그림 — 어떤 리셋이 어디까지 퍼지나

```text
                    Root Complex (SoC)
                         │
          ┌──────────────┼───────────────────────┐
     Root Port A     Root Port B             Root Port C
   (Bridge Ctl bit6)     │                       │
          │ SBR ⇒ ① 아래 전부                 Switch Upstream Port
     ┌────┴────┐         │                       │  (내부 가상 버스)
   EP1.fn0  EP1.fn1    EP2                ┌──────┴──────┐
     ▲ FLR ⇒ ② 이 함수만               Switch DP1     Switch DP2  ← DP1 Bridge Ctl bit6 SBR ⇒ ③ EP3만
     │  (fn1, 링크 유지)                   │              │
                                          EP3            EP4

 PERST# ⇒ ④ 보드 배선에 따라: 보통 슬롯/장치마다 GPIO 하나 (SoC의 GPIO 또는 컨트롤러 전용 핀)
 Link Disable on Root Port C (LnkCtl bit4) ⇒ ⑤ 스위치 + EP3 + EP4 전부 (링크 다운 = 아래쪽 리셋)
```

### 1.3 그림 — 전원 투입부터 첫 config 읽기까지 (conventional reset 타이밍)

```text
 VDD (3.3V/12V) ___/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
                   │←────── T_PVPERL ≥ 100 ms ──────→│
 REFCLK      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                                       │←T_PERST-CLK≥100µs→│
 PERST#      ______________________________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  (low = 리셋 중)
                                                         │← ≤20 ms →│ LTSSM Detect 진입
 LTSSM                                                    Detect → Polling → Config → L0 (→ Recovery → 고속 L0)
 DL_Active   ______________________________________________________________________/‾‾‾‾‾‾‾‾
 SW config                                         (≤5GT/s: PERST# 해제 후 100 ms)  (>5GT/s: DL_Active 후 100 ms)
 응답                                               RRS RRS RRS ... → 정상 응답   (장치 준비 끝날 때까지, SW는 ≥1 s 허용)
```

- 숫자 출처: Linux `drivers/pci/pci.h` 상수 `PCIE_T_PVPERL_MS 100`, `PCIE_T_PERST_CLK_US 100` (CEM r5.1 2.9.2 인용), `PCIE_RESET_CONFIG_WAIT_MS 100`, 그리고 `pci.c` 주석 "a component must enter LTSSM Detect within 20ms", "software must allow at least 1s" [1] [확인됨].

### 1.4 그림 — Hot reset은 링크로 "방송"된다

```text
 SW: setpci BRIDGE_CONTROL bit6 = 1 (Secondary Bus Reset)     ...≥1 ms(커널은 2 ms)...    bit6 = 0
        │
 Downstream Port(LTSSM) ──TS1/TS2 [Hot Reset bit=1] ×N──▶ Upstream Port of child (LTSSM → Hot Reset 상태)
                                                               │ 자기 function들 리셋 (sticky 제외)
                                                               │ 스위치라면 자기 downstream 포트들로 다시 전파
 bit6=0 → Downstream Port: Detect부터 다시 training → L0 → DL_Active → (100 ms) → config OK
```

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| Conventional reset | — | PCIe spec의 큰 리셋 묶음: cold + warm (+ hot) | 링크·장치 전부 초기화 |
| Fundamental reset | — | 옛 spec 용어: cold/warm (하드웨어 신호 기반) | 면접에서 이 말로 물어볼 수 있음 |
| Cold reset | — | 전원 꺼졌다 켜짐 | sticky까지 초기화, 전원 시퀀싱 검증 |
| Warm reset | — | 전원 유지, PERST# assert | 보드 신호, SW에선 GPIO로 |
| PERST# | PCI Express Reset (active-low 사이드밴드 핀) | 보드가 장치에 거는 하드 리셋 선 | 타이밍 위반 = 링크 안 뜸 |
| Hot reset | — | 링크 프로토콜(TS1 비트)로 전달되는 리셋 | SW로 쉽게 반복 가능 |
| SBR | Secondary Bus Reset | bridge의 Bridge Control(0x3E) bit 6 | hot reset을 거는 SW 손잡이 |
| Link Disable (LD) | — | LnkCtl bit 4, 링크를 Disabled 상태로 | 링크 다운 → 아래쪽 리셋 효과 |
| FLR | Function Level Reset | 함수 하나만 리셋, DevCtl bit 15 | VF/VM 격리, 빠른 복구 |
| AF FLR | Advanced Features FLR | conventional PCI용 FLR (cap ID 0x13) | 오래된/PCI 장치 |
| PM reset | — | D3hot→D0 전이로 생기는 리셋 (No_Soft_Reset=0) | FLR 없는 장치의 대안 |
| No_Soft_Reset | PMCSR bit 3 | 1이면 D3hot→D0에서 상태 유지(리셋 안 됨) | PM reset 가능 여부 결정 |
| Sticky bit | — | 리셋을 통과해 남는 비트 (RWS/RW1CS/ROS) | 리셋 전 에러 원인을 리셋 후 읽음 |
| RRS / CRS | Request Retry Status / (옛) Configuration Request Retry Status | "아직 준비 중, 다시 물어봐" 응답 | 리셋 직후 0xFFFF·0x0001의 원인 |
| RRS SV | RRS Software Visibility (Root Control bit 4) | RRS를 SW에 Vendor ID 0x0001로 보여줌 | 커널 폴링 방식 결정 |
| Immediate Readiness | Status bit 0 | "나는 리셋 후 바로 준비됨" → 100 ms 생략 | 리셋 루프 시간 단축 |
| DRS / FRS | Device / Function Readiness Status | 장치가 "준비됨" 메시지를 보냄 | 고정 대기 대신 이벤트 |
| DLLLARC | Data Link Layer Link Active Reporting Capable (LnkCap bit 20) | downstream port가 DL_Active를 보고할 수 있음 | 없으면 커널이 1.1 s 고정 대기 (실측) |
| Surprise Down | — | 예고 없이 링크가 내려감 (AER uncorrectable) | 리셋 중엔 마스킹/예상해야 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Conventional reset — cold / warm / PERST#

- **Cold**: 주 전원 off→on. **Warm**: 전원 유지, PERST# assert. 둘 다 장치 입장에선 "모든 것 초기화, 링크 Detect부터" (sticky는 aux 전원 있을 때 warm에서 유지 [추정: spec 6.6.1 sticky 정의 해석]).
- PERST#는 **SoC/보드 설계 영역**: 보통 SoC GPIO 또는 PCIe 컨트롤러 전용 출력. Linux에선 컨트롤러 드라이버(예: DesignWare 계열 host 드라이버)가 DT의 `reset-gpios`/`perst-gpios` 로 토글하고 `PCIE_T_PVPERL_MS` 를 기다린다 [추정: 속성 이름은 바인딩마다 다름]. 실제로 TI j721e 드라이버가 PERST# 해제 전 지연이 부족해 100 ms(T_PVPERL)로 고친 패치 사례가 있다 [2].
- 타이밍 표 (§1.3 그림의 정리):

| 기호 | 값 | 의미 | 출처 |
|---|---|---|---|
| T_PVPERL | ≥ 100 ms | 전원 안정 → PERST# 해제 | CEM r5.1 2.9.2 via `pci.h` [1] |
| T_PERST-CLK | ≥ 100 µs | refclk 안정 → PERST# 해제 | 같음, Intel F-Tile UG [5] |
| Detect 진입 | ≤ 20 ms | PERST# 해제 → LTSSM Detect | `pci.c` 주석 (PCIe r4.0 6.6.1) |
| 첫 config까지 | ≥ 100 ms | ≤5 GT/s: 리셋 해제 후 / >5 GT/s: 링크 training 완료(DL_Active) 후 | `PCIE_RESET_CONFIG_WAIT_MS`, `pci.c` |
| 응답 없음 판정 | ≥ 1 s | 그 전엔 RRS로 대기 가능 | `pci.c` 주석 |
| Linux 최대 폴링 | 60 s | `PCIE_RESET_READY_POLL_MS 60000` | `pci.c` [확인됨] |

### 2.2 Hot reset — Secondary Bus Reset

- 레지스터: Type 1(bridge) header **Bridge Control 0x3E**, **bit 6 `PCI_BRIDGE_CTL_BUS_RESET`** [3] [확인됨]. `lspci -vv` 에서 `BridgeCtl: … >Reset-` 가 이 비트.
- 동작: downstream port가 TS1/TS2 ordered set의 **Hot Reset 비트**를 보내고 LTSSM이 Hot Reset 상태 → 아래 장치(스위치면 그 아래까지 재귀) 리셋 → 비트 해제 후 Detect부터 재training.
- Linux `pci_reset_secondary_bus()` [1] [확인됨]: 비트 set → `msleep(2)` ("PCI spec v3.0 7.6.4.2 requires minimum Trst of 1ms. Double this to 2ms") → clear. 그다음 `pci_bridge_wait_for_secondary_bus()`:
  - port 최대 속도 ≤ 5 GT/s: `msleep(100)` 후 장치 폴링, DLLLARC 있으면 DL_Active 확인.
  - > 5 GT/s: **DL_Active를 기다린 뒤** 100 ms (`"waiting 100 ms for downstream link, after activation"` 실제 dmesg, §3.4).
  - **DLLLARC 없는 포트**: `pcie_wait_for_link_delay()` 가 그냥 `msleep(1000 + 100)` → QEMU 스위치 downstream port에서 **1.13 s** 실측의 원인 [확인됨: 코드 + 측정].
- 링크가 **DL_Down** 으로 떨어지는 것도 아래 장치 입장에선 hot reset과 같은 효과 [추정: spec 6.6.1 "Link Down" 리셋 규칙] → Link Disable, surprise link down 모두 "리셋 후 상태"로 이어진다.

### 2.3 Link Disable / enable

- **LnkCtl bit 4 (`PCI_EXP_LNKCTL_LD`)** on the downstream port [3] [확인됨]. set → LTSSM Disabled (상대에게 TS1 Disable 비트) → 링크 다운 → 아래 장치 리셋 효과. clear → Detect부터.
- 쓰임: 링크 재training 강제(강한 버전), hotplug 슬롯 전원 끄기 전, **"링크 다운/업 반복" 검증**. 비교: **Retrain Link (bit 5)** 는 리셋 없이 Recovery만 (속도 변경 적용용).
- QEMU 실측: LD 비트를 써도 `LnkCtl=0000` 유지 → **QEMU는 Link Disable을 에뮬레이션하지 않음** (§3.5). 실 하드웨어에서 검증 필요 [미실행].

### 2.4 FLR — Function Level Reset

```text
 PCI Express capability
 +0x04 DevCap  bit 28  Function Level Reset Capability       (lspci DevCap: "FLReset+")
 +0x08 DevCtl  bit 15  Initiate FLR (endpoint) / Bridge Configuration Retry Enable (bridge)   (lspci DevCtl: "FLReset-")
 +0x0A DevSta  bit 5   Transactions Pending  ← FLR 전에 0이 되길 기다림
```

- 비트 위치: `PCI_EXP_DEVCAP_FLR 0x10000000`, `PCI_EXP_DEVCTL_BCR_FLR 0x8000`, `PCI_EXP_DEVSTA_TRPND 0x0020` [3] [확인됨].
- Linux `pcie_flr()` [1] [확인됨]: ① `pci_wait_for_pending_transaction()` (TRPND=0 대기, 타임아웃이면 경고 후 진행) ② DevCtl bit 15 set ③ **Immediate Readiness가 아니면 `msleep(100)`** — 주석 "a device must complete an FLR within 100ms, but may silently discard requests while the FLR is in progress" ④ `pci_dev_wait()`로 준비 폴링.
- **FLR이 리셋하는 것**: 그 function의 내부 상태, 대부분의 config 레지스터(Command, BAR, MSI 등 → 그래서 OS가 save/restore), 진행 중이던 요청.
- **FLR이 안 리셋하는 것**: **링크 상태** (L0 유지 — 같은 장치의 다른 function 영향 없음), **sticky 비트**, 링크 관련 레지스터(ASPM control 등 포트 공통 설정) [추정: spec 6.6.2 예외 목록 요약], 다른 function.
- SR-IOV **VF는 FLR 지원이 필수** — VF 하나를 VM에서 회수할 때 쓰는 리셋 [추정: SR-IOV spec 요구사항으로 널리 설명됨].

### 2.5 AF FLR · PM reset · bus · device_specific · acpi

| 방법 (Linux 이름) | 메커니즘 | 조건 (커널 probe) | 대기 |
|---|---|---|---|
| `device_specific` | quirk 테이블의 장치별 함수 | 해당 quirk 있을 때 | 장치마다 |
| `acpi` | ACPI `_RST` 메서드 | ACPI 플랫폼 + 메서드 존재 | 펌웨어 |
| `flr` | DevCtl bit 15 | DevCap FLR 비트 + quirk로 금지 안 됨 | 100 ms + 폴링 |
| `af_flr` | Advanced Features cap (ID 0x13): AF_CAP **TP(bit0)+FLR(bit1)** 둘 다, AF_CTRL bit 0 | cap 존재 | 100 ms + 폴링 |
| `pm` | PMCSR → D3hot → D0 | PM cap 있고 **No_Soft_Reset=0**, 현재 D0 | `d3hot_delay`(기본 10 ms) × 2 + 폴링 |
| `bus` | 핫플러그 슬롯 리셋 또는 **부모 bridge SBR** | 루트 버스 아님, **자기가 bridge 아님**, **그 버스에 다른 장치 없음** | §2.2 |
| `cxl_bus` | CXL port의 SBR mask 해제 후 SBR | CXL만 (여기선 생략) | — |

- 순서는 `pci_reset_fn_methods[]` 배열 그대로: `device_specific, acpi, flr, af_flr, pm, bus, cxl_bus` [1] [확인됨].
- `bus`의 조건(`pci_parent_bus_reset`): "버스에 나 말고 다른 장치가 있으면 -ENOTTY" → **멀티펑션 장치나 공유 버스에서는 bus 리셋이 function 단위 메뉴에서 빠진다** (옆 장치까지 리셋하니까) [확인됨].
- 핫플러그 슬롯이면 `bus`는 `pciehp_reset_slot` 경유 — SBR 동안 presence/link change 이벤트를 마스킹해 "장치가 빠졌다"로 오인하지 않게 함 [추정: pciehp 동작 요약].

### 2.6 무엇이 리셋되고 무엇이 남나 — 총정리

| | Cold | Warm (PERST#) | Hot (SBR) / Link down | FLR | PM (D3hot→D0, NSR=0) |
|---|---|---|---|---|---|
| 링크 재training | ✅ | ✅ | ✅ | ❌ (L0 유지) | ❌ (L1→L0) |
| 같은 장치 다른 function | ✅ 리셋 | ✅ | ✅ | ❌ 영향 없음 | ❌ |
| config (BAR/Command/MSI) | ✅ 초기화 | ✅ | ✅ | ✅ (대부분) | ✅ (대부분) |
| sticky (AER status, header log) | ✅ 초기화 | 🟡 aux 전원 있으면 유지 | ❌ 유지 | ❌ 유지 | ❌ 유지 [추정] |
| 장치 펌웨어/내부 상태 | ✅ | ✅ | ✅ | ✅ (function 범위) | 🟡 장치 구현 |
| 아래쪽(스위치 뒤) 장치 | ✅ | 🟡 배선 따라 | ✅ 전부 | ❌ | ❌ |
| SW 대기 | 100 ms+ | 100 ms+ | 100 ms (+DL_Active) | 100 ms | 10 ms×2 |
| 반복 루프 난이도 | 어려움 (전원 제어 HW 필요) | 중간 (GPIO) | 쉬움 (setpci/sysfs) | 쉬움 | 쉬움 |

- **장치 밖에 남는 것** (모든 리셋 공통): IOMMU 매핑, 호스트 메모리의 descriptor ring, 드라이버 소프트웨어 상태, root port 쪽 AER 로그. → 리셋 후 드라이버가 재초기화하지 않으면 **DMA가 옛 주소로** 갈 수 있음.

### 2.7 준비 시간 — RRS(CRS), Immediate Readiness, DRS/FRS, Readiness Time Reporting

- **RRS (Request Retry Status)**: PCIe r6.0에서 CRS를 RRS로 개명. 커널도 `PCI_EXP_RTCTL_RRS_SVE 0x0010` + 호환 매크로 `PCI_EXP_RTCTL_CRSSVE` 를 둔다 [3] [확인됨].
  - Root Control의 **RRS Software Visibility**가 켜져 있으면: Vendor ID config read에 RRS가 오면 HW가 **0x0001** 를 돌려줌 → 커널 `pci_bus_rrs_vendor_id()` 로 판별, 계속 폴링.
  - 꺼져 있으면: root complex가 HW에서 재시도하다가 포기하면 **0xFFFFFFFF** 를 합성 → 커널은 **Command 레지스터**를 읽어 ~0이 아닐 때까지 폴링 (Vendor ID는 VF/없는 장치도 ~0이라서) — `pci_dev_wait()` 주석 [1] [확인됨].
  - 폴링 간격은 1, 2, 4, … ms 지수 증가, 1 s 넘으면 `"not ready %dms after %s; waiting"`, 타임아웃이면 `"not ready … giving up"`, 1 s 넘어 성공하면 `"ready %dms after %s"`. **1 s 넘으면 한 번 링크 retrain 시도** (`pcie_failed_link_retrain`).
- **Immediate Readiness** (Status register **bit 0**, `PCI_STATUS_IMM_READY`) — 커널 `probe.c` 가 `dev->imm_ready = 1` 로 기억하고 FLR 후 100 ms sleep을 생략 [확인됨].
- **DRS** (Device Readiness Status 메시지, LnkCap2 bit 31 — lspci `LnkCap2: … DRS-` 로 보임, QEMU 실측 `DRS-`), **FRS** (Function Readiness Status, FLR/D3hot→D0 뒤 function 단위), **Readiness Time Reporting** ext cap (장치가 "나는 실제로 X ms면 준비됨" 광고) — 고정 100 ms 대기를 줄이기 위한 장치들. 6.18 `pci_regs.h` 에는 Readiness Time Reporting/FRS ext cap ID가 정의돼 있지 않음 → 리눅스 메인라인이 아직 적극 활용하지 않는다고 보는 게 안전 [추정].

## 3. Linux에서 볼 것

### 3.1 sysfs · API · 도구

| 경로/API | 하는 일 |
|---|---|
| `/sys/bus/pci/devices/<BDF>/reset` (write 1) | `pci_reset_function()` — 드라이버에 reset_prepare/done 알리고, config 저장 → `reset_method` 순서대로 시도 → 복원 |
| `/sys/bus/pci/devices/<BDF>/reset_method` | 읽기: 가능한 방법을 우선순위대로. 쓰기: `"flr"`, `"pm bus"` 등으로 **순서 바꾸기/제한**, `"default"` 로 원복, 빈 문자열 = 전부 금지 |
| `/sys/bus/pci/devices/<bridge>/reset_subordinate` (write 1) | bridge 아래 버스 전체 리셋 (2024-10 추가 ABI) [4] |
| `/sys/bus/pci/devices/<BDF>/remove` + `/sys/bus/pci/rescan` | 리셋은 아니지만 "제거 후 재열거" — config를 새로 할당 [7] |
| `pci_reset_function(dev)` / `pci_reset_function_locked()` | 드라이버/VFIO가 쓰는 커널 API |
| `pcie_flr(dev)` | FLR 직접 (조건 확인 없이) |
| `pci_bridge_secondary_bus_reset(bridge)` | SBR + 대기 (EXPORT_SYMBOL_GPL) |
| `pci_reset_bus(dev)` | 슬롯/버스 단위 리셋, 버스의 모든 장치 save/restore |
| `setpci -s <bridge> BRIDGE_CONTROL.w=0x40:0x40` | **커널 몰래** SBR (config 복원 안 됨 — §3.5) |
| `lspci -vv` 의 `DevCap: … FLReset+`, `BridgeCtl: … >Reset-`, `LnkCtl: … LnkDisable-` | 지원/현재 비트 |

- `reset_method` 쓰기 동작 (`pci-sysfs.c` `reset_method_store`) [1] [확인됨]: 이름마다 probe를 돌려 **지원 안 하면 -EINVAL + dmesg `"Unsupported reset method '%s'"`** (QEMU 실측 §3.3), device_specific이 가능한데 맨 앞이 아니면 경고.

### 3.2 QEMU 실측 토폴로지와 reset_method

- 랩: Lima VM 안 QEMU 10.2 `virt,iommu=smmuv3`, 랩 커널 6.18.55, BusyBox initramfs + Ubuntu lspci/setpci 복사 (LP10 §6.1과 같은 명령) [실제 출력]:

```text
-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge
           +-01.0  Red Hat, Inc. Virtio network device
           +-02.0-[01]----00.0  Intel Corporation 82576 Gigabit Network Connection
           +-03.0-[02]----00.0  Red Hat, Inc. QEMU NVM Express Controller
           \-04.0-[03-06]----00.0-[04-06]--+-00.0-[05]----00.0  Intel Corporation 82574L Gigabit Network Connection
                                           \-01.0-[06]----00.0  Device 1234:11e8
0000:01:00.0 reset_method=[flr pm bus]    (igb: FLReset+, NoSoftRst-, 버스에 혼자)
0000:02:00.0 reset_method=[flr bus]       (nvme: FLReset+, NoSoftRst+ → pm 불가)
0000:05:00.0 reset_method=[pm bus]        (e1000e: FLReset- → flr 불가)
0000:06:00.0 reset_method=[bus]           (edu: PM cap 없음, FLR 없음)
0000:00:02.0 reset_method=[]              (root port: bridge라 bus 불가, FLR 없음)
```

- 해석: `reset_method` 는 **probe 결과의 교집합**이다 — DevCap FLR 비트, PMCSR No_Soft_Reset, "버스에 혼자 + bridge 아님" 조건이 그대로 드러남. **검증 매트릭스를 짤 때 이 줄 하나로 장치별 가능한 리셋을 자동으로 결정**할 수 있다.

### 3.3 reset_loop.sh 실측 — 방법별 시간

- 스크립트: [onsitePrep/lp11/reset_loop.sh](onsitePrep/lp11/reset_loop.sh) — 매 회 config에서 직접 ID/Command/BAR0, upstream DL_Active, 드라이버 바인딩, AER 카운터를 확인 [실제 출력, 요약]:

```text
device  : 0000:01:00.0 id=0x10c98086 cmd=0x0406 bar0=0x10000000 driver=igb
upstream: 0000:00:02.0 DLLLARC=1
reset_method (kernel order): flr pm bus
method  : default -> /sys/bus/pci/devices/0000:01:00.0/reset ; iterations: 5
[1] PASS 130ms cmd=0x0406 cor=0
[2] PASS 120ms cmd=0x0406 cor=0
...
summary : method=default pass=5 fail=0 time_ms min=110 max=130 AER_cor 0->0
[    2.070462] igb 0000:01:00.0: resetting
[    2.173075] igb 0000:01:00.0: reset done

method=pm      (igb)     pass=3 fail=0 time_ms min=30  max=40
method=bus     (igb)     pass=3 fail=0 time_ms min=150 max=160
method=flr     (nvme)    pass=3 fail=0 time_ms min=110 max=120
method=pm      (e1000e)  pass=3 fail=0 time_ms min=30  max=41
method=sbr     (e1000e via 04:00.0 reset_subordinate)  pass=2 fail=0 time_ms min=1130 max=1140
method=default (edu -> bus) pass=2 fail=0 time_ms min=10 max=20
method 'af_flr' not supported by 0000:01:00.0 (see dmesg)
[    7.138270] igb 0000:01:00.0: Unsupported reset method 'af_flr'
```

| 방법 | 실측 | 왜 그 시간인가 (코드) |
|---|---|---|
| flr | 110–130 ms | `msleep(100)` + 폴링 + 드라이버 reset_prepare/done |
| pm | 30–40 ms | `pci_dev_d3_sleep` × 2 (d3hot_delay 10 ms) + 드라이버 |
| bus (root port 아래) | 140–160 ms | SBR 2 ms + 포트 LnkCap 16 GT/s라 "after activation" 경로: DL_Active 대기 + 100 ms |
| sbr (스위치 DP, DLLLARC 없음) | 1130–1140 ms | `msleep(PCIE_LINK_RETRAIN_TIMEOUT_MS + delay)` = 1000 + 100 ms |
| bus (edu, 드라이버 없음) | 10–20 ms | 아래 해석 참고 |

- 해석: 드라이버 없는 edu(06:00.0)가 10–20 ms로 빠른 건, `pci_bridge_wait_for_secondary_bus()` 가 `pci_bus_max_d3cold_delay()` 로 대기 시간을 정하는데 그 버스 장치(edu)에 **PM capability가 없어** `pci_pm_init()` 이 `d3cold_delay` 를 100 ms로 설정하지 않고(0 유지) → `if (!delay) return 0;` 으로 **대기 없이 바로 반환**하기 때문 [확인됨: 코드 경로; 같은 이유로 PM cap 없는 Lima virtio 장치들의 `d3cold_allowed=0`]. **"리셋이 빨리 끝났다 = 좋은 것"이 아니다** — 스펙 대기를 건너뛰면 실제 하드웨어에선 RRS/0xFFFF를 볼 수 있다.

### 3.4 dmesg — 커널이 하는 save/restore를 눈으로 (dyndbg)

- `echo "file drivers/pci/pci.c +p" > /sys/kernel/debug/dynamic_debug/control` 후 `bus` 리셋 1회 [실제 출력]:

```text
[    2.546177] igb 0000:01:00.0: save config 0x30: 0x00000000
[    2.546181] igb 0000:01:00.0: save config 0x34: 0x00000040
[    2.546185] igb 0000:01:00.0: save config 0x38: 0x00000000
[    2.546189] igb 0000:01:00.0: save config 0x3c: 0x00000116
[    2.546878] igb 0000:01:00.0: reset via bus
[    2.553211] pcieport 0000:00:02.0: waiting 100 ms for downstream link, after activation
[    2.680480] igb 0000:01:00.0: ready 0ms after bus reset
[    2.680575] igb 0000:01:00.0: restore config 0x3c: 0x00000100 -> 0x00000116
[    2.680587] igb 0000:01:00.0: restore config 0x1c: 0x00000004 -> 0x10040004
[    2.680593] igb 0000:01:00.0: restore config 0x18: 0x00000001 -> 0x00001001
[    2.680599] igb 0000:01:00.0: restore config 0x14: 0x00000000 -> 0x10020000
[    2.680604] igb 0000:01:00.0: restore config 0x10: 0x00000000 -> 0x10000000
[    2.680610] igb 0000:01:00.0: restore config 0x04: 0x00100000 -> 0x00100406
[    2.681494] igb 0000:01:00.0: reset done
```

| 줄 | 해석 |
|---|---|
| `save config 0x30…0x3c` | 리셋 전 표준 header 64 B를 dword 단위 저장 (일부만 보임) |
| `reset via bus` | reset_method에서 bus 선택 |
| `waiting 100 ms for downstream link, after activation` | root port가 >5 GT/s 지원 → DL_Active 후 100 ms 규칙 |
| `ready 0ms after bus reset` | 첫 폴링에서 바로 응답 (QEMU라 RRS 없음) |
| `restore config 0x10: 0x00000000 -> 0x10000000` | **리셋이 BAR0을 0으로 지웠고**, 커널이 원래 주소로 복원 |
| `restore config 0x04: 0x00100000 -> 0x00100406` | Command 레지스터: Memory Space(bit1)·Bus Master(bit2)·INTx Disable(bit10) 복원 |
| `0x3c: 0x00000100 -> 0x00000116` | Interrupt Line 복원 (Interrupt Pin=1은 RO라 남아 있음) |

- 스위치 포트 SBR의 1.1 s 실측 dmesg: `pcieport 0000:04:00.0: waiting 100 ms for downstream link, after activation` (1.239 s) → `e1000e 0000:05:00.0: ready 0ms after bus reset` (2.366 s). 메시지는 100 ms라고 하지만 DLLLARC가 없어서 실제로는 1100 ms sleep.

### 3.5 커널 몰래 하면 무슨 일이? — setpci SBR · Link Disable

```text
### raw SBR via setpci behind the kernel's back (root port 00:03.0 -> nvme 02:00.0, unbound)   [실제 출력]
before: nvme cmd=0000 bar0=10200004 bridgectl=0002
after : nvme cmd=0000 bar0=00000004 bridgectl=0002
recover: remove + rescan
rescanned: nvme cmd=0000 bar0=10200004
### link disable on root port 00:03.0
LnkCtl=0000 LnkSta=2011
LD=1: LnkCtl=0000 LnkSta=2011 nvme vendor=1b36
LD=0: LnkCtl=0000 LnkSta=2011 nvme vendor=1b36
```

- 사용한 명령: `setpci -s 00:03.0 BRIDGE_CONTROL.w=0x40:0x40; sleep 0.01; setpci -s 00:03.0 BRIDGE_CONTROL.w=0x00:0x40; sleep 1` (값:마스크 문법으로 bit 6만 건드림) [6].
- 해석 1: SBR 후 **BAR0 = 0x00000004** (주소 지워짐, 하위 비트 = 64-bit memory BAR 타입 표시만 남음) → 커널은 모르니 복원 안 함 → 드라이버가 붙어 있었다면 MMIO가 엉뚱한 곳 → **remove + rescan** 으로 재할당해 복구. 그래서 실무에선 sysfs `reset`/`reset_subordinate` 를 쓰거나, raw SBR 후엔 반드시 remove/rescan.
- 해석 2: LnkSta 0x2011 = speed 1(2.5 GT/s), width x1, bit 13 DL_Active. **QEMU는 LD 비트가 쓰기 불가** → 링크 다운 실험은 실 하드웨어에서만 가능.
- 실 하드웨어 SBR 레시피 [미실행]:

```bash
BR=0000:00:1c.0; EP=0000:01:00.0
echo 1 > /sys/bus/pci/devices/$EP/remove                # 드라이버 분리 + 커널에서 제거
setpci -s $BR BRIDGE_CONTROL.w=0x40:0x40; sleep 0.002
setpci -s $BR BRIDGE_CONTROL.w=0x00:0x40; sleep 1       # ≥100 ms, 넉넉히
setpci -s $BR CAP_EXP+0x12.w                            # LnkSta: bit13 DL_Active 확인
echo 1 > /sys/bus/pci/rescan                            # 재열거 → BAR 재할당 → 드라이버 probe
```

### 3.6 리셋 후 0xFFFF / "not ready" 패턴

| dmesg / 관찰 | 의미 |
|---|---|
| `lspci` 에 `rev ff`, `-vvv` 에서 `Unknown header type 7f` | config read가 전부 1 → 장치 응답 없음 (링크 다운 또는 아직 리셋 중) |
| `not ready 1023ms after FLR; waiting` | 1 s 넘게 RRS/0xFFFF → 커널은 계속 기다림 (최대 60 s) |
| `not ready 65535ms after bus reset; giving up` | 60 s 타임아웃 → 장치 죽음 처리 |
| `ready 1500ms after FLR` | 결국 준비됨 — 장치 펌웨어 부팅이 느림 (스펙상 1 s 넘는 RRS는 문제 소지) |
| `timed out waiting for pending transaction; performing function level reset anyway` | FLR 전 TRPND가 안 비워짐 → 진행 중 DMA가 있었음 (completion 유실 가능) |
| `Unsupported reset method 'xxx'` | reset_method에 지원 안 하는 방법을 씀 (QEMU 실측) |
| AER `Surprise Down` (root port) | 리셋/LD 중 예상된 링크 다운을 마스킹 안 함, 또는 진짜 링크 사고 |

### Ethernet/MIPI 비교 (짧게)

| | PCIe | Ethernet | MIPI CSI-2 |
|---|---|---|---|
| 하드 리셋 핀 | PERST# | PHY `RESET_N` (MAC이 GPIO로) | 센서 `XSHUTDOWN`/`RESET` GPIO |
| 소프트 리셋 | FLR, SBR | MDIO로 PHY **BMCR bit 15** (soft reset), MAC 소프트 리셋 | 센서 I2C 레지스터 soft reset, 수신기(CSI-2 RX) 리셋 |
| 링크 재협상 | SBR / Retrain / LD | **BMCR bit 9 restart auto-negotiation** | stream off/on, D-PHY LP-11 재진입 |
| 대기 규칙 | 100 ms, RRS | PHY 리셋 후 수 ms–수십 ms(데이터시트) | 센서 power-up 시퀀스(데이터시트) |

## 4. 꼬리 질문 체인

### 체인 A (LP00): "Which reset would you use in a validation loop and why?" → 각각 무엇을 안 리셋하나? → 리셋 후 0xFFFF가 한동안 나오는 이유?

<details><summary>Q. Which reset would you use in a validation loop, and why?</summary>

- **A**: "I pick by what the loop is supposed to exercise. If I'm validating the link — training, equalization, reaching the right speed and width every time — I need something that retrains: hot reset via Secondary Bus Reset is easy to drive from software thousands of times, and PERST# through a GPIO covers the full conventional reset path. If I'm validating the function — driver recovery, firmware re-init, VF reassignment — FLR is better because the link stays in L0, so a failure points at the function, not the PHY. Cold power cycles are slow and need power control hardware, so I run fewer, but they're the only way to cover power sequencing and sticky behavior. Every iteration checks: device readable, config restored, speed and width, AER, and time-to-ready."
- 한국어 핵심: 링크 검증 = SBR/PERST# (재training), function 검증 = FLR (링크 유지), cold는 적게 하지만 전원 시퀀싱·sticky 커버. 매 회 5가지 체크.

</details>

<details><summary>꼬리1. What does each one NOT reset?</summary>

- **A**: "Sticky bits survive hot reset and FLR, and survive warm reset when aux power is present — that's the AER uncorrectable and correctable status and the header log, so you can read why something failed after recovering. FLR doesn't touch the link or other functions on the same device. None of them reset state outside the device: IOMMU mappings, descriptor rings in host memory, the driver's view — and none of them clear the root port's own error status, because that's a different device. If the driver doesn't reinitialize after reset, DMA can target stale addresses."
- 한국어 핵심: sticky(AER status/log) 남음, FLR은 링크·다른 function 안 건드림, 장치 밖(IOMMU, ring, 드라이버, root port AER) 은 어떤 리셋도 안 지움.

</details>

<details><summary>꼬리2. Right after reset the device returns 0xFFFF for a while. Why?</summary>

- **A**: "Two possibilities. If the link isn't up yet, the config read can't reach the device and the root complex synthesizes all-ones. If the link is up but the device is still initializing, it answers with Request Retry Status — what used to be called CRS. With RRS software visibility enabled the root port returns vendor ID 0x0001 so software knows to retry; without it the hardware retries and eventually gives up with all-ones. The spec says wait 100 milliseconds — after link-up for links above 5 GT/s — and allow at least one second before declaring the device broken. Linux polls with exponential backoff up to 60 seconds and logs 'not ready … waiting'."
- 한국어 핵심: 링크 미완 → 0xFFFF 합성 / 링크 OK·장치 준비 중 → RRS(0x0001 또는 HW 재시도 후 0xFFFF). 100 ms·1 s 규칙, Linux 60 s 폴링.

</details>

<details><summary>꼬리3. How would you make that wait shorter on your own silicon?</summary>

- **A**: "Report readiness instead of relying on fixed waits. Setting the Immediate Readiness bit in the Status register tells software it can skip the 100 ms after FLR — Linux checks it. For conventional reset, Device Readiness Status lets the device send a message when it's ready, and Readiness Time Reporting advertises the real time needed. But you only set those if the hardware truly meets them across all corners, which is itself a validation item."
- 한국어 핵심: Immediate Readiness(Status bit0), DRS/FRS, Readiness Time Reporting — 단, 모든 코너에서 진짜 지킬 때만.

</details>

### 체인 B: "Hot reset versus FLR versus PERST#?" → SBR 구체 메커니즘? → 스위치 뒤 장치는?

<details><summary>Q. Explain hot reset, FLR and PERST#.</summary>

- **A**: "PERST# is a board-level sideband signal — the conventional, or fundamental, reset; with power staying on it's a warm reset. Hot reset is in-band: software sets the Secondary Bus Reset bit in the bridge's Bridge Control register, the downstream port sends training sets with the Hot Reset bit, and everything below that port resets and the link retrains. FLR is scoped to a single function: you set Initiate FLR in Device Control, the function resets its internal state within 100 milliseconds, and the link stays up."
- 한국어 핵심: PERST# = 보드 핀(warm/fundamental), hot = SBR → TS1 Hot Reset 비트 → 아래 전부 + 재training, FLR = function 하나 100 ms, 링크 유지.

</details>

<details><summary>꼬리1. Walk me through the SBR sequence in software.</summary>

- **A**: "Save the config state of every device below the bridge, set bit 6 of Bridge Control at offset 0x3E, hold it at least 1 ms — Linux uses 2 — clear it, then wait: for ports up to 5 GT/s, 100 ms; above that, wait for Data Link Layer Link Active in the port's Link Status and then 100 ms. Then poll the device until it stops returning retry status, and restore config. If the port doesn't support link-active reporting, Linux just sleeps 1.1 seconds. I measured exactly that in QEMU: about 140 ms behind a root port that reports DL_Active, about 1.13 seconds behind a switch port that doesn't."
- 한국어 핵심: save → bit6 set ≥1 ms → clear → (≤5GT: 100 ms / >5GT: DL_Active+100 ms) → RRS 폴링 → restore. DLLLARC 없으면 1.1 s (실측).

</details>

<details><summary>꼬리2. What happens to devices behind a switch when you SBR the root port?</summary>

- **A**: "The hot reset propagates: the switch's upstream port goes through hot reset, which resets the switch, and the switch propagates it to its downstream ports, so every device in that subtree resets and every link retrains. That's why Linux won't offer 'bus' as a per-function reset if there are other devices on the same bus — and why for a single endpoint behind a switch you'd SBR its own downstream port instead."
- 한국어 핵심: 스위치 통째로 전파 → 서브트리 전부 리셋. 그래서 Linux는 버스에 다른 장치 있으면 bus 리셋 제외.

</details>

### 체인 C (추가): "Your reset loop passes 999 times and fails once. How do you debug it?"

<details><summary>Q. The reset loop fails once in a thousand iterations. What do you do?</summary>

- **A**: "First make the failure capture itself: on the failing iteration, stop and dump everything before touching anything — the root port's Link Status, Link Status 2 for equalization, AER status on both ends since they're sticky, the controller's LTSSM debug register, and how long the device took to become ready. Then classify: did the link not train, train at the wrong speed or width, or train fine but the function didn't come back? Then correlate with conditions — temperature, timing of the reset relative to traffic, which method — and try to raise the rate: shorter waits, traffic during reset, corners. Once it reproduces at a useful rate I can put an analyzer on the link and trigger on the LTSSM state."
- 한국어 핵심: 실패 순간 멈추고 덤프(LnkSta/LnkSta2/AER sticky/LTSSM/준비시간) → 분류(링크/속도·폭/함수) → 조건 상관 → 재현율 올리기 → 애널라이저 트리거.

</details>

<details><summary>꼬리1. What would the time-to-ready histogram tell you?</summary>

- **A**: "A tight distribution well under 100 ms means the device is healthy and the fixed spec wait dominates. A long tail — some iterations taking close to a second — means the device's internal init has a slow path, maybe firmware loading or PLL lock, and it's one step away from crossing the 1-second limit at a bad corner. I'd log it per iteration and plot it per temperature."
- 한국어 핵심: 분포가 좁으면 정상, 긴 꼬리면 1 s 한계에 가까운 느린 경로 → 코너별 플롯.

</details>

### 체인 D (추가, bare-metal): "No Linux. How do you do an FLR in your validation firmware?"

<details><summary>Q. In bare-metal firmware, how do you issue an FLR and know it's done?</summary>

- **A**: "Find the PCIe capability through ECAM, check DevCap bit 28. Disable bus mastering in the Command register, wait for Transactions Pending in Device Status to clear, save the config dwords I care about, set bit 15 in Device Control, then wait 100 ms unless Immediate Readiness is set. After that, poll the Vendor ID or Command register — with retry status visibility enabled on my root port I look for 0x0001 — until I get a real value, with a timeout of at least one second. Then restore BARs and Command, re-enable bus mastering, and re-init the function."
- 한국어 핵심: DevCap FLR 확인 → BME off → TRPND 대기 → save → DevCtl bit15 → 100 ms → RRS 폴링(≥1 s 타임아웃) → restore → BME on.

</details>

## 5. Don 경험 연결

- "At SK hynix I automated a chamber reliability test platform for NAND and PCIe Gen3–5 interfaces; reset-and-retrain loops across temperature are the core of that kind of test, and the per-iteration checks — link speed and width, error counters, time to ready — are what I'd carry over to a new SoC. [숫자 채우기: 루프 횟수/챔버 온도 범위/발견한 이슈 1개]"
- "At Solidigm, on the Gen6 SSD controller, the device side had to handle FLR and hot reset correctly — complete within the spec time, return retry status while firmware was still initializing. Working pre-silicon on FPGA, I saw how much of the reset path is firmware timing, which is why I'd always measure time-to-ready, not just pass or fail. [숫자 채우기: 실제로 본 리셋 관련 이슈]"
- "At Apple I debugged PCIe issues on wireless chipsets with a DSO and a protocol analyzer — PERST# and REFCLK timing relative to power is exactly the kind of thing you only catch on a scope. [숫자 채우기: 구체 사례 있으면]"
- 주의: 구체 리셋 버그 경험이 없으면 "this is how I'd structure it"로 말하기.

## 6. 실습 / 코드

### 6.1 실행한 것 (검증됨)

- [onsitePrep/lp11/reset_loop.sh](onsitePrep/lp11/reset_loop.sh) — POSIX sh (BusyBox OK). 인자: `<BDF> [iterations] [default|flr|af_flr|pm|bus|sbr]`. **QEMU에서 실행 ✅** (igb/nvme/e1000e/edu, 방법 전부, §3.3). `af_flr` 미지원 경로(에러 처리)도 확인. Lima VM의 virtio 장치에는 `reset_method` 자체가 없어 실행 대상 없음 (그리고 디스크라 리셋 금지).
- dyndbg로 save/restore 관찰 (§3.4), raw setpci SBR로 BAR 소실 + remove/rescan 복구 (§3.5) — 실측.
- QEMU 실행 명령은 [LP10 §6.1](2026-10-31_LP10_power_mgmt_aspm.html)과 동일 (같은 initramfs, `LAB_CMD=sh~/run11.sh`).

### 6.2 검증 루프 설계표 — 어떤 리셋을 언제

| 목표 | 리셋 | 루프 횟수 감 [추정] | 매 회 체크 | 비고 |
|---|---|---|---|---|
| 링크 bring-up 안정성 | PERST# (GPIO) warm | 1k–10k | DL_Active 시간, speed/width, LnkSta2 EQ, AER | 보드 신호 타이밍 포함 |
| 전원 시퀀싱·sticky | cold (전원 릴레이/PMIC) | 100–1k | 위 + sticky 비트 초기화 여부 | 느림, 자동화 HW 필요 |
| 링크 재training 회귀 | hot reset (SBR / `reset_subordinate`) | 10k+ | 위 + 시간 분포 | SW만으로 가능, 가장 흔함 |
| 링크 다운/업 처리 | Link Disable → enable | 1k+ | AER Surprise Down 처리, DPC(LP09), hotplug 이벤트 | 에러 경로 검증 |
| function 복구 | FLR | 10k+ | config 복원, 드라이버 재초기화, DMA 정상 | 링크 문제와 분리 |
| FLR 없는 장치 | pm (D3hot→D0) | 1k+ | 위와 같음 | NoSoftRst=0일 때만 |
| VF 재할당 | VF FLR | 1k+ | 다른 VF 트래픽 무영향 | LP12 |
| 트래픽 중 리셋 | FLR/SBR during DMA | 1k+ | TRPND 대기 경고, completion timeout, IOMMU fault | 가장 버그가 많이 나오는 곳 |

### 6.3 실 하드웨어 레시피 [미실행]

```bash
# 장치별 가능한 리셋 목록 → 매트릭스 자동 생성
for d in /sys/bus/pci/devices/*; do [ -e $d/reset_method ] && echo "$(basename $d): $(cat $d/reset_method)"; done
# 루프 (root)
sh onsitePrep/lp11/reset_loop.sh 0000:01:00.0 1000 flr  | tee flr.log
sh onsitePrep/lp11/reset_loop.sh 0000:01:00.0 1000 sbr  | tee sbr.log
grep -c FAIL flr.log sbr.log
# 실패 시 즉시 덤프
lspci -s 00:1c.0 -vvv | grep -E "LnkSta|LnkSta2|UESta|CESta"
```

## 7. 자주 틀리는 것

1. **"Hot reset = FLR"** ❌ — hot reset은 bridge 아래 전체 + 링크 재training, FLR은 function 하나 + 링크 유지.
2. **"리셋하면 AER 상태도 지워진다"** ❌ — AER status/header log는 sticky. 리셋 후에도 원인 분석 가능 (그리고 다음 테스트 전에 직접 RW1C로 지워야 함).
3. **SBR 비트를 set만 하고 clear 안 함** ❌ — 비트가 1인 동안 계속 리셋 상태. set → ≥1 ms → clear.
4. **리셋 직후 바로 config 접근** ❌ — 100 ms 규칙(>5 GT/s는 DL_Active 후), RRS 폴링, 1 s.
5. **setpci로 SBR 후 그냥 사용** ❌ — BAR가 0 (실측). sysfs `reset`/`reset_subordinate` 쓰거나 remove/rescan.
6. **멀티펑션 장치 한 function에 bus 리셋** ❌ — 옆 function도 리셋. Linux는 그래서 메뉴에서 뺀다.
7. **FLR 전 트래픽 정지 안 함** — 진행 중 DMA completion이 유실/에러 → TRPND 대기 + Bus Master off 먼저.
8. **"리셋이 빨리 끝났다 = 좋다"** — 대기를 건너뛴 것일 수 있음 (edu 10 ms 사례). 스펙 대기 준수가 먼저.
9. **CRS라는 이름만 알고 RRS 모름** — PCIe 6.0부터 RRS, 커널 매크로도 RRS (호환 CRS 매크로 존재).
10. **PERST# 타이밍은 SW 문제 아님** ❌ — 컨트롤러 드라이버가 GPIO를 언제 놓느냐가 SW. j721e 같은 실제 패치 사례 [2].

## 8. 출처

1. Linux `drivers/pci/pci.c`, `drivers/pci/pci.h`, `drivers/pci/pci-sysfs.c`, `drivers/pci/probe.c` (v6.18.55 소스를 랩 VM에서 직접 읽음: `pci_reset_fn_methods[]`, `pcie_flr`, `pci_af_flr`, `pci_pm_reset`, `pci_reset_secondary_bus`, `pci_bridge_wait_for_secondary_bus`, `pcie_wait_for_link_delay`, `pci_dev_wait`, `reset_method_store`, timing 상수): https://github.com/torvalds/linux/tree/master/drivers/pci
2. LKML "PCI: j721e: Delay 100ms T_PVPERL from power stable to PERST# inactive" (2023): https://lists.infradead.org/pipermail/linux-arm-kernel/2023-July/850966.html , v5 "Delay T_PVPERL+TPERST_CLK before PERST# inactive": https://lkml.rescloud.iu.edu/2308.2/01716.html
3. Linux `include/uapi/linux/pci_regs.h` (Bridge Control, DevCap/DevCtl FLR, LnkCtl LD, AF cap, RRS, Immediate Readiness): https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
4. Linux ABI `Documentation/ABI/testing/sysfs-bus-pci` (`reset_method`, `reset_subordinate`): https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-bus-pci
5. Intel F-Tile Avalon Streaming IP for PCIe UG — PCI Express Resets (PERST#/refclk 관계): https://www.intel.com/content/www/us/en/docs/programmable/683140/25-3/pci-express-resets.html
6. pciutils `setpci` man page (CAP_/ECAP_ 이름, 값:마스크 문법) 및 `ls-caps.c`: https://github.com/pciutils/pciutils
7. Linux PCI 문서 (sysfs, error recovery 의 slot_reset 맥락): https://docs.kernel.org/PCI/sysfs-pci.html , https://docs.kernel.org/PCI/pci-error-recovery.html

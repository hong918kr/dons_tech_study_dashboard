# LP02 · Config space · capability · extended capability
<!-- theme: pastel -->

> **묶음**: A 기초 모델 · **우선**: P1 · **선행 노트**: [LP01 토폴로지](2026-10-31_LP01_pcie_topology_linux_model.html) · **다음 노트**: [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · **입문**: [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **관련**: [N01 lspci·LTSSM](2026-10-31_N01_pcie_lspci_ltssm.html), [L2 cap walk 문제](hmScreenPrep/problems/L2_pcie_cap_walk/index.html) · **코드**: [onsitePrep/lp02/pcicfg_walk.c](onsitePrep/lp02/pcicfg_walk.c) (macOS selftest ALL PASS · Lima VM 실기 · QEMU 게스트 실기)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"config space에 뭐가 있나?"**, **"PCIe capability를 어떻게 찾나?"**, **"legacy 접근으로 AER이 왜 안 보이나?"**, **"cap list가 루프/쓰레기면?"**, **"Bus Master 비트를 안 켜면 뭐가 깨지나?"**, **"BAR sizing은 어떻게?"**
- 외울 것 1 — **지도**: `0x00–0x3F` 표준 헤더(64 B) · `0x40–0xFF` capability 영역(→ 0x34에서 시작하는 연결 리스트, ID 1바이트 + next 1바이트) · `0x100–0xFFF` **extended capability**(4 KiB 중 나머지, 헤더 32-bit = ID 16 · ver 4 · next 12, **ECAM으로만 접근**).
- 외울 것 2 — **Command(0x04)**: bit0 I/O Space, **bit1 Memory Space**(BAR 디코드), **bit2 Bus Master**(장치가 DMA·**MSI** 를 낼 수 있음), bit10 INTx Disable. **Status(0x06) bit4 Capabilities List**. 에러 비트(bit 8, 11–15)는 **RW1C**(1을 써서 지움).
- 외울 것 3 — **PCIe capability(ID 0x10) 안의 offset**: DevCap +0x04 · DevCtl +0x08 · DevSta +0x0A · **LnkCap +0x0C · LnkCtl +0x10 · LnkSta +0x12** · SltCap +0x14 · SltCtl +0x18 · SltSta +0x1A · RootCtl +0x1C · DevCap2 +0x24 · DevCtl2 +0x28 · LnkCap2 +0x2C · LnkCtl2 +0x30 · LnkSta2 +0x32. 자주 쓰는 ext cap ID: **AER 0x0001 · DSN 0x0003 · ACS 0x000D · SR-IOV 0x0010 · LTR 0x0018 · Secondary PCIe 0x0019 · DPC 0x001D · L1SS 0x001E · PL 16GT 0x0026 · PL 32GT 0x002A · DOE 0x002E · PL 64GT 0x0031** (`pci_regs.h`로 검증).
- 함정 3개: 리스트는 **주소 순서가 아님**(실측 root port: 0x54 → 0x48 → 0x40), 걷기엔 **루프 가드(커널 48 / 480 hop)**, **non-root가 sysfs `config`를 읽으면 64 B만** 나옴 (실측).
- **영어 30초 (headline Q: "What's in configuration space?")**: "Every PCI function has a configuration space — 256 bytes for conventional PCI, 4 KiB for PCIe. The first 64 bytes are a standard header: vendor and device ID, command and status, class code, header type, the BARs where the device asks for MMIO space, and a capabilities pointer at 0x34. That pointer starts a linked list of capabilities in the rest of the first 256 bytes — power management, MSI, MSI-X, and the PCI Express capability with the device, link and slot registers. Above 0x100 there's a second linked list of extended capabilities — AER, SR-IOV, ACS, DPC, L1 substates, the 16 and 32 GT/s physical layer registers — and those you can only reach through ECAM, not the legacy 256-byte mechanism."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 장치마다 붙어 있는 "표준 이력서 + 자격증 묶음"

[LP01](2026-10-31_LP01_pcie_topology_linux_model.html)에서 PCIe 장치마다 "명함"이 있다고 했습니다. 정확히는 **표준 양식 이력서**에 가깝습니다.

- **1페이지 상단(0x00–0x3F, 64 B) = 표준 인적 사항**: 이름(Vendor ID = 회사, Device ID = 제품), 직업 분류(Class Code: "저장장치/NVMe", "네트워크/Ethernet"), 현재 상태(Status), 그리고 **"제게 일 시키려면 이만한 사무실이 필요합니다"(BAR)**. 양식은 모든 장치가 똑같아서 OS는 처음 보는 장치라도 여기까지는 읽을 수 있습니다.
- **스위치 패널(Command 레지스터)**: 이력서 한쪽에 OS가 켜고 끄는 스위치 몇 개가 있습니다. "사무실 문 열기(Memory Space Enable: CPU가 BAR 주소로 보내는 요청에 응답)", "**운전면허(Bus Master Enable: 장치가 스스로 메모리에 읽고 쓰기 = DMA, 그리고 MSI 인터럽트도 사실 '메모리 쓰기'라서 이게 꺼지면 인터럽트도 못 보냄)**", "옛날식 인터럽트 선 끄기(INTx Disable)".
- **나머지 1페이지(0x40–0xFF) = 자격증 스티커들(Capability list)**: "전원 관리 가능(PM)", "메시지 인터럽트 가능(MSI/MSI-X)", "**PCIe 장치임 + 내 링크 상태(PCI Express capability)**". 스티커 위치가 장치마다 달라서 **0x34번 칸에 "첫 스티커는 몇 번 줄에 있음"**이 적혀 있고, 각 스티커 끝에는 "다음 스티커 위치"가 적힌 **연결 리스트(linked list)** 입니다. 0이면 끝.
- **별지(0x100–0xFFF, 3.75 KiB) = 고급 자격증(Extended capability)**: 에러 기록부(AER), 가상화(SR-IOV), 저전력 상세(L1 PM Substates), 고속 링크 등화 결과(Physical Layer 16/32 GT/s) 등. 이것도 연결 리스트, 0x100에서 시작. **문제는 옛날 창구(x86 CF8/CFC)로는 1페이지(256 B)까지만 보여준다는 것** → 별지를 보려면 ECAM(메모리 게시판, LP01 §2.6)이 필요.

```text
  하나의 PCIe function의 config space (4096 B)
  0x000 ┌──────────────────────────────────────────────┐
        │ 표준 헤더 64 B  (Type 0 = 장치 / Type 1 = 브리지)  │  ← Vendor/Device ID, Command, Status, Class, BAR, 0x34 Cap Ptr
  0x040 ├──────────────────────────────────────────────┤
        │ capability 영역 (192 B)                        │  0x34 ─► [PM] ─► [MSI-X] ─► [PCIe cap] ─► 0
        │   각 항목: [ID 1B][next 1B][본문 …]              │        (주소 순서 아님, 위치는 장치 마음)
  0x100 ├──────────────────────────────────────────────┤  ← 여기부터는 ECAM(또는 동등 기능)으로만 보임
        │ extended capability 영역 (3840 B)               │  0x100 ─► [AER] ─► [DSN] ─► [SR-IOV] ─► 0
        │   각 항목 헤더 32-bit: [next 12b][ver 4b][ID 16b] │
  0xFFF └──────────────────────────────────────────────┘
        conventional PCI 장치는 0x000–0x0FF (256 B) 만 존재
```

### 1.2 BAR: "제 레지스터를 이만한 크기로 지도에 올려 주세요"

장치 안에는 레지스터나 버퍼가 있고, CPU는 그걸 **메모리 주소처럼(MMIO)** 읽고 쓰고 싶습니다. 그런데 장치는 자기가 CPU 주소 지도의 어디에 놓일지 모릅니다. 그래서:

1. 장치는 **BAR(Base Address Register)** 에 "**나는 16 KiB짜리 메모리 창이 필요하고, 64-bit 주소도 되고, 미리 읽어도(prefetch) 안전하지 않다**"는 정보를 **고정된 비트 패턴**으로 담아 둡니다.
2. OS(또는 펌웨어)는 BAR에 **전부 1(0xFFFFFFFF)** 을 써 보고 다시 읽습니다. 장치는 크기 때문에 바꿀 수 없는 하위 비트를 0으로 돌려줍니다 → 몇 비트가 0인지 보면 크기를 압니다 (**BAR sizing**).
3. OS가 실제 주소를 정해 BAR에 써 넣고, Command의 Memory Space를 켜면 그 주소 범위의 CPU 접근이 장치로 갑니다.

비유: 택배 회사(OS)가 "주소 칸에 전부 9를 써 보세요" 하면 장치가 "제가 바꿀 수 있는 자리만 9로 남겨요" 하고 돌려주는 것 — 바꿀 수 없는 자리 수가 곧 "필요한 땅 크기".

### 1.3 PCIe capability = 링크의 계기판

`lspci -vvv`에서 보던 `LnkCap`, `LnkSta`, `DevCtl` 같은 줄은 전부 **PCI Express capability(ID 0x10)** 라는 스티커 하나 안의 레지스터들입니다. 자동차 계기판처럼:

- **Cap(Capabilities) = 사양서**: "최대 Gen4, x4까지 가능", "최대 payload 512 B 가능" — 읽기 전용, 하드웨어가 정함.
- **Ctl(Control) = 운전자가 조작하는 레버**: "payload는 256 B로 써라", "ASPM 꺼라", "링크 재학습해라".
- **Sta(Status) = 현재 계기 바늘**: "지금 Gen3 x4로 달리는 중", "데이터 링크 활성(DLActive)", "에러 감지됨".
- Device(장치 전체) / Link(링크) / Slot(슬롯: hotplug, 존재 감지) / Root(루트 포트 전용) 4묶음 + PCIe 2.0 이후 추가된 "2번" 세트(DevCap2, LnkCap2…).

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| Config space | Configuration Space | 함수마다 있는 256 B(PCI) / 4 KiB(PCIe) 설정 레지스터 공간 | 열거·드라이버·검증 도구가 전부 여기서 시작 |
| Header | — | config space 앞 64 B 표준 양식 | Type 0(장치)/Type 1(브리지) |
| Vendor ID / Device ID | — | 제조사 번호(PCI-SIG 할당) / 제품 번호 | `0xFFFF` = 장치 없음, 드라이버 매칭 키 |
| Class Code | — | 24-bit 분류: base class / sub class / prog-if | 예 `010802` = 저장장치/NVM/NVMe. 클래스 드라이버 매칭 |
| Command | Command Register (0x04) | OS가 켜는 스위치: Mem/IO 디코드, Bus Master, INTx Disable | 꺼져 있으면 MMIO·DMA·MSI 불가 |
| Status | Status Register (0x06) | 장치 상태: Cap List 존재, 에러 기록 | 에러 비트는 RW1C |
| RW1C | Read / Write-1-to-Clear | 1을 쓰면 0으로 지워지는 상태 비트 | 0을 쓰면 아무 일 없음. read-modify-write 하면 다른 에러를 실수로 지움 |
| BAR | Base Address Register | 장치가 "이 크기·종류의 주소 창을 달라"고 요청하고 OS가 주소를 써 넣는 칸 | MMIO 주소의 출처, sizing 질문 단골 |
| Prefetchable | — | 읽어도 부작용 없음 → CPU/브리지가 미리·합쳐 읽어도 됨 | 64-bit pref 창 배치, write-combining 매핑 |
| Expansion ROM | — | 옵션 ROM(부트 코드) 주소 칸 (0x30) | 펌웨어 부팅, sysfs `rom` |
| Capability | — | 0x40–0xFF의 기능 블록 연결 리스트 | PM/MSI/MSI-X/PCIe 등을 찾는 방법 |
| Cap Pointer | Capabilities Pointer (0x34) | 첫 capability의 offset | Status bit4가 1일 때만 유효 |
| PM | Power Management cap (0x01) | D0–D3 전원 상태 제어 | 런타임 PM, D3cold ([LP10](2026-10-31_LP10_power_mgmt_aspm.html)) |
| MSI / MSI-X | Message Signaled Interrupts (0x05 / 0x11) | 인터럽트를 "특정 주소에 메모리 쓰기"로 보내는 방식 | Bus Master 필요. MSI-X는 BAR 안에 벡터 테이블 ([LP07](2026-10-31_LP07_interrupts_intx_msi_msix.html)) |
| PCIe cap | PCI Express Capability (0x10) | Device/Link/Slot/Root 레지스터 묶음 | LnkSta(속도·폭·DLActive), DevCtl(MPS/MRRS) |
| Extended capability | — | 0x100–0xFFF의 기능 블록 연결 리스트 (16-bit ID) | AER, SR-IOV, ACS, DPC, L1SS 등 |
| AER | Advanced Error Reporting (0x0001) | correctable/uncorrectable 에러 상태·마스크·심각도·헤더 로그 | margin/SI 문제의 증거 ([LP09](2026-10-31_LP09_aer_dpc_error_handling.html)) |
| DSN | Device Serial Number (0x0003) | 64-bit 고유 번호 | 같은 BDF에 같은 장치가 돌아왔는지 확인 |
| ACS | Access Control Services (0x000D) | 스위치/RP에서 P2P 우회 차단 등 | IOMMU 그룹 격리 ([LP12](2026-10-31_LP12_sriov_vfio_acs_p2p.html)) |
| SR-IOV | Single Root I/O Virtualization (0x0010) | 물리 함수(PF) 하나가 가상 함수(VF) 여러 개를 만듦 | VF RID = PF + offset + n×stride |
| LTR | Latency Tolerance Reporting (0x0018) | 장치가 "나는 이만큼 지연 견딤"을 보고 | 플랫폼 저전력 결정 |
| Secondary PCIe | (0x0019) | Gen3 이상 lane별 equalization 제어 + Lane Error Status | lane 단위 에러 위치 |
| DPC | Downstream Port Containment (0x001D) | 에러 시 포트가 링크를 끊어 오염 차단 | 서버/자동차 RAS |
| L1SS | L1 PM Substates (0x001E) | L1.1/L1.2 초저전력 | 모바일·자동차 전력 |
| PL 16/32/64 GT/s | Physical Layer 16.0/32.0/64.0 GT/s (0x0026/0x002A/0x0031) | 해당 속도의 equalization 상태·lane별 설정 | Gen4/5/6 링크 검증 |
| DOE | Data Object Exchange (0x002E) | config space 위 우편함(mailbox) 프로토콜 | SPDM 인증, CXL 디스커버리 |
| DVSEC / VSEC | (Designated) Vendor-Specific Extended Capability (0x0023 / 0x000B) | 벤더 전용 확장 블록 | 컨트롤러 디버그 레지스터가 여기 숨어 있기도 함 |
| MPS / MRRS | Max Payload Size / Max Read Request Size | TLP 하나에 실을 최대 데이터 / 읽기 요청 최대 크기 | 성능·호환성 (경로 전체에서 최소값) |
| sysfs `config` | `/sys/bus/pci/devices/<BDF>/config` | 커널이 노출하는 config space 바이너리 파일 | `hexdump`/`lspci`의 데이터 출처, root만 전체 |

## 2. 핵심 개념 (정확한 버전)

모든 offset·ID·비트는 Linux 6.18 `include/uapi/linux/pci_regs.h`와 대조했습니다 [확인됨, 출처 1]. 표기: `+0xNN` = capability 시작 기준 offset.

### 2.1 Type 0 헤더 (0x00–0x3F) 바이트 지도

```text
 offset  byte3          byte2          byte1          byte0
 0x00   [ Device ID                   ][ Vendor ID                   ]
 0x04   [ Status                      ][ Command                     ]
 0x08   [ Class: base  sub   prog-if                ][ Revision ID   ]
 0x0C   [ BIST         ][ Header Type ][ Latency Tmr ][ Cache Line Sz ]
 0x10   [ BAR0                                                        ]
 0x14   [ BAR1                                                        ]
 0x18   [ BAR2                                                        ]
 0x1C   [ BAR3                                                        ]
 0x20   [ BAR4                                                        ]
 0x24   [ BAR5                                                        ]
 0x28   [ CardBus CIS Pointer                                         ]
 0x2C   [ Subsystem ID                ][ Subsystem Vendor ID         ]
 0x30   [ Expansion ROM Base Address                                  ]
 0x34   [ reserved                                    ][ Cap Pointer  ]
 0x38   [ reserved                                                    ]
 0x3C   [ Max_Lat      ][ Min_Gnt     ][ Int Pin     ][ Int Line      ]
```

| offset | 크기 | 이름 (`pci_regs.h`) | 메모 |
|---|---|---|---|
| 0x00 | 16 | `PCI_VENDOR_ID` | `0xFFFF` = 없음 (all-ones 응답) |
| 0x02 | 16 | `PCI_DEVICE_ID` | |
| 0x04 | 16 | `PCI_COMMAND` | §2.3 |
| 0x06 | 16 | `PCI_STATUS` | §2.3 |
| 0x08 | 8 | `PCI_REVISION_ID` | `PCI_CLASS_REVISION` = 0x08 dword: 상위 24비트 class |
| 0x09 | 8 | `PCI_CLASS_PROG` | prog-if (예: NVMe = 0x02, xHCI = 0x30) |
| 0x0A | 16 | `PCI_CLASS_DEVICE` | `base<<8 \| sub` (예: 0x0108) |
| 0x0C | 8 | `PCI_CACHE_LINE_SIZE` | PCIe에선 기능적 의미 거의 없음 |
| 0x0D | 8 | `PCI_LATENCY_TIMER` | PCIe에선 0 고정(무의미) |
| 0x0E | 8 | `PCI_HEADER_TYPE` | [6:0] 0=normal, 1=bridge, 2=CardBus · bit7 = multi-function (`PCI_HEADER_TYPE_MFD 0x80`) |
| 0x0F | 8 | `PCI_BIST` | |
| 0x10–0x24 | 32×6 | `PCI_BASE_ADDRESS_0..5` | §2.4 |
| 0x2C / 0x2E | 16 | `PCI_SUBSYSTEM_VENDOR_ID` / `PCI_SUBSYSTEM_ID` | 보드 제조사 구분 (같은 칩, 다른 카드) |
| 0x30 | 32 | `PCI_ROM_ADDRESS` | bit0 enable, [31:11] 주소 |
| 0x34 | 8 | `PCI_CAPABILITY_LIST` | Status bit4 = 1일 때 유효 |
| 0x3C | 8 | `PCI_INTERRUPT_LINE` | OS가 적는 메모(하드웨어 무관) |
| 0x3D | 8 | `PCI_INTERRUPT_PIN` | 0 없음, 1–4 = INTA#–INTD# |

Type 1(브리지)은 0x18부터 다릅니다 — BAR 2개 + 버스 번호 + 주소 window + Bridge Control: [LP01 §2.3](2026-10-31_LP01_pcie_topology_linux_model.html).

### 2.2 Header Type · multi-function · PCIe에서 의미 잃은 필드

- Linux `pci_dev.hdr_type`은 bit7(MFD)을 뺀 값 [확인됨: 주석 "multi flag masked out"].
- Cache Line Size, Latency Timer, Min_Gnt/Max_Lat, Status의 66MHz/Fast B2B/DEVSEL은 **옛 병렬 PCI 버스용**. PCIe 장치는 대부분 0 또는 고정값 — `lspci`가 `66MHz- FastB2B- DEVSEL=fast` 를 찍는 건 그냥 PCI 호환 표시.

### 2.3 Command / Status 비트

| Command bit | 이름 (`PCI_COMMAND_*`) | 뜻 | 꺼지면 |
|---|---|---|---|
| 0 | `IO` 0x1 | I/O space BAR 디코드 | I/O BAR 접근 무시 |
| 1 | `MEMORY` 0x2 | **Memory space BAR 디코드** | MMIO 읽기 → UR → CPU엔 all-ones (`lspci`: `[disabled]`) |
| 2 | `MASTER` 0x4 | **Bus Master** — 장치가 요청(메모리 R/W) 발행 | **DMA 불가, MSI/MSI-X 불가** (MSI는 메모리 쓰기 TLP) |
| 3 | `SPECIAL` 0x8 | special cycle | PCIe 무의미 |
| 4 | `INVALIDATE` 0x10 | MWI | PCIe 무의미 (Lima virtio에선 1로 보임 — 가상 장치 값) |
| 6 | `PARITY` 0x40 | Parity Error Response | PCIe: poisoned TLP 보고와 연계 |
| 8 | `SERR` 0x100 | SERR# Enable | fatal/non-fatal 에러 메시지 보고 (`ERR_*` 전달) |
| 10 | `INTX_DISABLE` 0x400 | **INTx 끄기** | MSI/MSI-X 쓸 때 커널이 1로 세팅 (`DisINTx+`) |

| Status bit | 이름 (`PCI_STATUS_*`) | 뜻 | 속성 |
|---|---|---|---|
| 0 | `IMM_READY` 0x01 | Immediate Readiness (리셋 후 대기 불필요) | RO |
| 3 | `INTERRUPT` 0x08 | INTx 어서트 중 | RO |
| **4** | `CAP_LIST` 0x10 | **capability list 존재** (PCIe는 필수 1) | RO |
| 8 | `PARITY` 0x100 | Master Data Parity Error | RW1C |
| 11 | `SIG_TARGET_ABORT` 0x800 | Completer Abort 보냄 | RW1C |
| 12 | `REC_TARGET_ABORT` 0x1000 | Completer Abort 받음 | RW1C |
| 13 | `REC_MASTER_ABORT` 0x2000 | **Unsupported Request 받음** | RW1C |
| 14 | `SIG_SYSTEM_ERROR` 0x4000 | ERR_FATAL/NONFATAL 보냄 | RW1C |
| 15 | `DETECTED_PARITY` 0x8000 | poisoned TLP 받음 | RW1C |

- **RW1C 지우기**: `setpci -s BDF STATUS.w=0xf900` 처럼 지울 비트만 1. Command와 Status는 한 dword(0x04)에 붙어 있어서 **Command를 32-bit read-modify-write로 쓰면 Status의 에러 비트를 실수로 지울 수 있음** → 16-bit 쓰기 또는 Status 쪽을 0으로 마스크 [확인됨: L2 문제 fake가 이걸 모델링].

### 2.4 BAR 인코딩과 sizing

```text
 Memory BAR                                            I/O BAR
 31                         4  3   2 1   0             31                    2  1   0
 ┌───────────────────────────┬────┬─────┬───┐          ┌──────────────────────┬────┬───┐
 │ Base Address (사이즈 정렬)   │Pref│Type │ 0 │          │ Base Address          │ rsv│ 1 │
 └───────────────────────────┴────┴─────┴───┘          └──────────────────────┴────┴───┘
 Type [2:1]: 00 = 32-bit, 10 = 64-bit (다음 BAR가 상위 32비트), 01 = <1MiB(폐지)
 Pref  bit3: 1 = prefetchable
```

- [확인됨: `PCI_BASE_ADDRESS_SPACE_IO 0x01`, `PCI_BASE_ADDRESS_MEM_TYPE_64 0x04`, `PCI_BASE_ADDRESS_MEM_PREFETCH 0x08`, `PCI_BASE_ADDRESS_MEM_MASK ~0x0f`, `PCI_BASE_ADDRESS_IO_MASK ~0x03`]
- **Sizing 절차** (커널 `__pci_read_base()`와 같은 흐름):
  1. Command의 Mem/IO decode 끄기 (sizing 중 엉뚱한 주소에 응답 방지).
  2. 원래 값 저장 → `0xFFFFFFFF` 쓰기 → 읽기 → 원래 값 복원. 64-bit면 상위 BAR도 같이.
  3. 하위 플래그 비트(mem 4비트 / I/O 2비트) 마스크 → `size = ~mask + 1` (64-bit는 두 dword를 합쳐 64-bit로 계산).
  4. 읽은 값이 0이면 그 BAR는 구현 안 됨.
  5. Command 복원.
- **예**: 읽은 값 `0xFFFFC000` (32-bit mem) → `~0xFFFFC000 + 1 = 0x4000` = 16 KiB. 64-bit 쌍 `lo=0x0000000C, hi=0xFFFFFFFE` → mask `0xFFFFFFFE_00000000` → 8 GiB prefetchable. (코드: `bar_size()`, selftest에서 검증 §6)
- **실측 해석 — Lima virtio-net `00:01.0`**: `10: 04 00 00 80 03 00 00 00` → BAR0 = `0x80000004`(bit2=1 → 64-bit, non-pref) + BAR1 = `0x00000003` → 주소 `0x3_8000_0000`. 크기는 sysfs `resource` 줄 `0x0000000380000000 0x000000038000ffff` → 64 KiB. `lspci`: `Region 0: Memory at 380000000 (64-bit, non-prefetchable) [size=64K]` ✓.
- **실측 — QEMU e1000e** dmesg: `BAR 0 [mem 0x00000000-0x0001ffff]`(128 KiB) `BAR 2 [io 0x0000-0x001f]`(32 B I/O) `ROM [mem … pref]` — 커널이 sizing한 결과를 주소 배정 전(0 기준)에 찍은 것.
- **lspci는 크기를 어떻게 알까**: config를 다시 쓰지 않고 sysfs `resource`(커널이 sizing해 둔 값)를 읽음 → 그래서 덤프 파일(`-F`)로 보면 `[size=…]`가 빠짐 (실측 QEMU 덤프 디코드에서 size 없음).

### 2.5 Capability list (0x40–0xFF)

```text
 capability 항목 공통 머리           PCI_CAP_LIST_ID = 0, PCI_CAP_LIST_NEXT = 1, PCI_CAP_FLAGS = 2
  +0 [ Cap ID ]  +1 [ Next ptr ]  +2 [ cap-specific 16-bit ]  +4 … 본문
```

**걷는 법 (커널 `drivers/pci/pci.h` `PCI_FIND_NEXT_CAP()` 매크로와 같은 규칙)**

1. Vendor ID `0xFFFF`면 장치 없음.
2. **Status bit4(Cap List) = 0이면 리스트 없음**.
3. `p = cfg[0x34] & 0xFC` (하위 2비트는 reserved → 마스크).
4. `p != 0` 인 동안: `p < 0x40`이면 깨진 포인터 → 중단. `cfg[p]` = ID, `p = cfg[p+1] & 0xFC`.
5. **TTL 48** (`PCI_FIND_CAP_TTL 48` [확인됨: `drivers/pci/pci.h`]) 넘으면 루프로 보고 중단 — (0x100−0x40)/4 = 48 이 최대 가능한 항목 수.

**Standard capability ID** [확인됨: `PCI_CAP_ID_*`]

| ID | 이름 | 비고 |
|---|---|---|
| 0x01 | Power Management | PMC(+2), PMCSR(+4): D-state [1:0] |
| 0x03 | VPD | Vital Product Data |
| 0x05 | MSI | Message Control(+2): Enable bit0, Multiple Message Capable [3:1], 64-bit bit7, per-vector mask bit8 |
| 0x07 | PCI-X | |
| 0x09 | Vendor-Specific | 길이(+2)는 벤더 마음 — virtio가 이걸로 BAR 안 구조 위치를 알려줌 (실측) |
| 0x0D | Bridge Subsystem VID | 브리지의 subsystem ID (QEMU root port/switch에 실측) |
| 0x10 | **PCI Express** | §2.6 |
| 0x11 | MSI-X | Message Control(+2): Table Size [10:0] (값+1개), Function Mask bit14, Enable bit15 · Table(+4)/PBA(+8): BIR [2:0] + offset |
| 0x12 | SATA | |
| 0x13 | Advanced Features | (conventional PCI FLR) |
| 0x14 | Enhanced Allocation | |

**실측 — 순서는 주소 순이 아니다**: QEMU root port `00:01.0`의 0x34 = `0x54` → `[54] PCIe` → next `0x48` `[48] MSI-X` → next `0x40` `[40] Bridge SSVID` → `0x00`. `lspci -vvv`도 이 **리스트 순서대로** 출력 (`Capabilities: [54]` → `[48]` → `[40]`).

### 2.6 PCI Express capability (ID 0x10) 레지스터 지도

```text
 +0x00 [ PCIe Capabilities (16) ][ Next ][ ID=0x10 ]      +0x02 Capabilities reg: ver[3:0] type[7:4] slot[8] IntMsg[13:9] Flit[15]
 +0x04 [ Device Capabilities (32)                    ]
 +0x08 [ Device Status (16)      ][ Device Control (16) ]
 +0x0C [ Link Capabilities (32)                      ]
 +0x10 [ Link Status (16)        ][ Link Control (16)   ]
 +0x14 [ Slot Capabilities (32)                      ]     ← Slot 구현(+0x02 bit8)일 때
 +0x18 [ Slot Status (16)        ][ Slot Control (16)   ]
 +0x1C [ Root Capabilities (16)  ][ Root Control (16)   ]     ← Root Port / RCEC
 +0x20 [ Root Status (32)                            ]
 +0x24 [ Device Capabilities 2 (32)                  ]     ← cap version 2 이상
 +0x28 [ Device Status 2 (16)    ][ Device Control 2 (16)]
 +0x2C [ Link Capabilities 2 (32)                    ]
 +0x30 [ Link Status 2 (16)      ][ Link Control 2 (16)  ]
 +0x34 [ Slot Capabilities 2 (32)                    ]
 +0x38 [ Slot Status 2 (16)      ][ Slot Control 2 (16)  ]
```

| 레지스터 (offset) | 핵심 비트 (`PCI_EXP_*`) | 검증/디버그에서 쓰는 법 |
|---|---|---|
| Capabilities (+0x02) | ver `FLAGS_VERS 0x000f`, type `FLAGS_TYPE 0x00f0` (0 EP, 4 RP, 5 USP, 6 DSP, 9 RCiEP), slot `0x0100`, Flit `0x8000` | 포트 종류 판별, v1이면 +0x24 이후 없음 |
| DevCap (+0x04) | MPS 지원 `[2:0]` (128 << n), ExtTag `bit5`, L0s/L1 acceptable latency, RBER `bit15`, **FLR `bit28`** | FLR 가능 여부 ([LP11](2026-10-31_LP11_reset_types.html)) |
| DevCtl (+0x08) | 에러 보고 enable `[3:0]` (Corr/NonFatal/Fatal/UR), Relaxed Ordering `bit4`, **MPS `[7:5]`**, ExtTag `bit8`, No Snoop `bit11`, **MRRS `[14:12]`**, **Initiate FLR `bit15`** (EP) | MPS는 경로 최소값으로 맞춰야 함 → 틀리면 Malformed TLP |
| DevSta (+0x0A) | CED/NFED/FED/URD `[3:0]` (RW1C), AUX PD `bit4`, **Transactions Pending `bit5`** | FLR 전 TransPend 0 대기 |
| LnkCap (+0x0C) | Max Speed `[3:0]`, Max Width `[9:4]`, ASPM 지원 `[11:10]`, L0s/L1 exit latency, ClockPM `bit18`, Surprise Down 보고 `bit19`, **DLL Link Active Reporting `bit20`**, BW notif `bit21`, Port # `[31:24]` | 기대 링크 능력 |
| LnkCtl (+0x10) | ASPM `[1:0]`, RCB `bit3`, **Link Disable `bit4`**, **Retrain `bit5`**, Common Clock `bit6`, CLKREQ `bit8`, HW Autonomous Width Disable `bit9` | downstream port에서 retrain/disable |
| LnkSta (+0x12) | Speed `[3:0]`, **Width `[9:4]`**, **Link Training `bit11`**, Slot Clock `bit12`, **DLLLA `bit13`**, LBMS `bit14`, LABS `bit15` | [N01](2026-10-31_N01_pcie_lspci_ltssm.html)의 L0 판정 |
| SltCap (+0x14) | Attention btn/Power ctrl/MRL/indicators `[4:0]`, Surprise `bit5`, **Hot-Plug Capable `bit6`**, Power limit, Physical Slot # `[31:19]` | hotplug 지원 ([LP04](2026-10-31_LP04_enumeration_resources_hotplug.html)) |
| SltCtl (+0x18) | 각종 인터럽트 enable, indicator, Power Controller `bit10`, DLL State Changed Enable `bit12` | |
| SltSta (+0x1A) | Presence Detect Changed `bit3`, Command Completed `bit4`, **Presence Detect State `bit6`**, DLL State Changed `bit8` | 카드 꽂힘 여부 (실측 빈 DSP: PresDet-) |
| RootCtl (+0x1C) | SERR on Corr/NonFatal/Fatal `[2:0]`, PME int `bit3`, **CRS(RRS) SW Visibility `bit4`** | 열거 중 RRS(CRS) 처리 |
| RootCap (+0x1E) / RootSta (+0x20) | RRS SV `bit0` / PME requester ID `[15:0]`, PME status `bit16` | |
| DevCap2 (+0x24) | Completion Timeout ranges `[3:0]`, CTO Disable `bit4`, ARI `bit5`, AtomicOp, **LTR `bit11`**, OBFF, 10-bit tag | |
| DevCtl2 (+0x28) | **Completion Timeout Value `[3:0]`**, CTO Disable `bit4`, ARI Forwarding `bit5`, **LTR enable `bit10`** | 완료 타임아웃 → AER CmpltTO |
| LnkCap2 (+0x2C) | **Supported Link Speeds Vector `[7:1]`** (bit1 = 2.5 … bit6 = 64 GT/s), Crosslink `bit8` | Gen3+에서 LnkCap Max Speed는 이 벡터의 인덱스 |
| LnkCtl2 (+0x30) | **Target Link Speed `[3:0]`**, Enter Compliance `bit4`, HW Autonomous Speed Disable `bit5`, Transmit Margin `[9:7]` | Gen1로 낮춰 retrain 실험 |
| LnkSta2 (+0x32) | Current De-emphasis `bit0`, **EQ 8GT/s Complete `bit1`, Phase1/2/3 `bit2–4`, Link EQ Request `bit5`**, Flit Mode `bit10` | Gen3 EQ 성공 여부 |

- [확인됨: 모든 offset `PCI_EXP_DEVCAP 0x04 … PCI_EXP_SLTSTA2 0x3a`, 위 비트 마스크는 `pci_regs.h` 6.18 그대로. LnkSta2 EQ 비트 이름(1–5)은 `pci_regs.h`에 개별 매크로가 없고 pciutils `lib/header.h`/lspci 출력 기준 — 6.18 `pci_regs.h`에는 `PCI_EXP_LNKSTA2_FLIT 0x0400`만 있음 [확인됨: lspci 실측 출력 "EqualizationComplete- EqualizationPhase1- …"]]
- 커널 주석: DevCap2 이후 레지스터는 **cap version 2에서만 존재** → `pcie_capability_read_word()`가 `pcie_capability_reg_implemented()`(버전·포트 타입 검사)로 없는 레지스터는 0을 돌려줌 [확인됨: `drivers/pci/access.c`].
- **실측 바이트 디코드 — QEMU root port `00:01.0`, PCIe cap @0x54** (`lspci -xxx` 행 `50:`/`60:`/`80:`):

| offset | 바이트 | 레지스터 | 값 → 의미 |
|---|---|---|---|
| 0x54 | `10 48 42 01` | ID/Next/Caps | ID 0x10, next 0x48, Caps 0x0142 → v2, type 4 (Root Port), Slot+ |
| 0x58 | `20 80 00 00` | DevCap | 0x00008020 → MPS 128 B, ExtTag+(bit5), RBER+(bit15) |
| 0x5C | `0f 00 00 00` | DevCtl / DevSta | DevCtl 0x000f → 에러 보고 4종 enable (lspci `CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+`) |
| 0x60 | `04 06 30 00` | LnkCap | 0x00300604 → speed 4 (16 GT/s), width 0x20 = x32, ASPM L0s, DLLLARC+(bit20), BW notif+(bit21) |
| 0x64 | `00 00 11 20` | LnkCtl / LnkSta | LnkSta 0x2011 → 2.5 GT/s, x1, **DLActive+** |
| 0x68 | `7b 00 0a 00` | SltCap | 0x000a007b → HPC+(bit6), Physical Slot # = 0x000a007b >> 19 = 1 |
| 0x6C | `c0 03 50 00` | SltCtl / SltSta | SltSta 0x0050 → Command Completed+, **Presence Detect+** |
| 0x80 | `1e 00 00 00` | LnkCap2 | 0x1e → 2.5/5/8/16 GT/s 지원 |
| 0x84 | `04 00 00 00` | LnkCtl2 / LnkSta2 | Target Link Speed 4 (16 GT/s) |

### 2.7 Extended capability (0x100–0xFFF)

```text
 31             20 19   16 15                     0
 ┌────────────────┬───────┬────────────────────────┐
 │ Next Offset 12b│ Ver 4b│ Extended Cap ID 16b     │   PCI_EXT_CAP_ID(h) = h & 0xffff
 └────────────────┴───────┴────────────────────────┘   PCI_EXT_CAP_VER(h) = (h >> 16) & 0xf
  첫 항목은 항상 0x100. next = 0 이면 끝.                 PCI_EXT_CAP_NEXT(h) = (h >> 20) & 0xffc
  0x100의 dword가 0 이면 "확장 cap 없음", 0xFFFFFFFF면 "확장 공간 접근 불가"
```

- **걷는 법**: TTL = (4096 − 256) / 8 = **480** [확인됨: `drivers/pci/pci.h` `PCI_FIND_NEXT_EXT_CAP()` 매크로의 `__ttl`]. next가 0x100 미만(0 제외)이면 깨진 리스트.
- **실측 디코드 — QEMU igb `07:00.0`** (`lspci -xxxx`의 `100:`, `150:`, `160:` 행):

| offset | 바이트 | dword | ID / ver / next | lspci 표기 |
|---|---|---|---|---|
| 0x100 | `01 00 01 15` | 0x15010001 | 0x0001 AER / v1 / 0x150 | `[100 v1] Advanced Error Reporting` |
| 0x150 | `0e 00 01 16` | 0x1601000e | 0x000E ARI / v1 / 0x160 | `[150 v1] Alternative Routing-ID Interpretation (ARI)` |
| 0x160 | `10 00 01 00` | 0x00010010 | 0x0010 SR-IOV / v1 / 0 (끝) | `[160 v1] Single Root I/O Virtualization (SR-IOV)` |

  SR-IOV 본문(실측 `setpci`): TotalVFs(+0x0E) `0008`, First VF Offset(+0x14) `0080`, VF Stride(+0x16) `0002`, VF Device ID(+0x1A) `10ca` → VF n의 RID = PF RID 0x0700 + 0x80 + (n−1)×2 → 첫 VF `07:10.0` (`lspci`: `VF offset: 128, stride: 2, Device ID: 10ca`).
- **주의 — ID 공간이 둘**: standard 0x10 = PCI Express, extended 0x0010 = SR-IOV. 같은 숫자, 다른 리스트. `find_cap(0x10)`과 `find_ext_cap(0x10)`은 다른 것을 찾음.

**Extended capability ID 표** [확인됨: `PCI_EXT_CAP_ID_*`]

| ID | 이름 | 무엇에 쓰나 | 주요 offset (확인된 것만) |
|---|---|---|---|
| 0x0001 | **AER** | 에러 상태/마스크/심각도/헤더 로그 | UncorSta +0x04, UncorMask +0x08, UncorSev +0x0C, **CorSta +0x10**, CorMask +0x14, Cap&Ctl +0x18, Header Log +0x1C(16 B), RootCmd +0x2C, RootSta +0x30, ErrSrc +0x34 |
| 0x0002 / 0x0009 | Virtual Channel | QoS VC 설정 | |
| 0x0003 | **Device Serial Number** | 64-bit 일련번호 | low +0x04, high +0x08 (`PCI_EXT_CAP_DSN_SIZEOF 12`) |
| 0x0004 | Power Budgeting | | |
| 0x000B | VSEC | 벤더 전용 | |
| 0x000D | **ACS** | P2P 우회 제어, IOMMU 그룹 | Cap +0x04, Ctl +0x06 |
| 0x000E | ARI | 함수 256개 | |
| 0x000F | ATS | 장치 측 IOMMU 변환 캐시 | |
| 0x0010 | **SR-IOV** | VF 생성 | Cap +0x04, Ctrl +0x08, TotalVF +0x0E, NumVF +0x10, VF Offset +0x14, VF Stride +0x16, VF DID +0x1A, VF BAR0 +0x24 |
| 0x0015 | Resizable BAR | BAR 크기 협상 | |
| 0x0018 | **LTR** | 지연 허용치 보고 | Max Snoop +0x04, Max No-Snoop +0x06 |
| 0x0019 | **Secondary PCIe** | Gen3 lane EQ 제어, **Lane Error Status** | Lane EQ Control +0x0C |
| 0x001D | **DPC** | 포트 격리 | DPC Cap +0x04 |
| 0x001E | **L1 PM Substates** | L1.1/L1.2 | Cap +0x04, Ctl1 +0x08, Ctl2 +0x0C |
| 0x001F | PTM | 정밀 시간 측정 | |
| 0x0023 | DVSEC | 지정 벤더 전용 (CXL 등) | |
| 0x0025 | Data Link Feature | scaled flow control 등 | |
| 0x0026 | **Physical Layer 16.0 GT/s** | Gen4 EQ 상태, lane별 EQ | Lane EQ Control +0x20 |
| 0x002A | **Physical Layer 32.0 GT/s** | Gen5 EQ / modified TS | |
| 0x002E | **DOE** | mailbox (SPDM, CDAT) | Cap +0x04, Ctrl +0x08, Status +0x0C, Write +0x10, Read +0x14 |
| 0x0031 | **Physical Layer 64.0 GT/s** | Gen6 (PAM4/Flit) | |

**AER 비트** (자주 묻는 것만) [확인됨]: Correctable `RCVR 0x1`(Receiver Error) · `BAD_TLP 0x40` · `BAD_DLLP 0x80` · `REP_ROLL 0x100` · `REP_TIMER 0x1000` · `ADV_NFAT 0x2000` · `INTERNAL 0x4000` · `LOG_OVER 0x8000`. Uncorrectable `DLP 0x10` · `SURPDN 0x20` · `POISON_TLP 0x1000` · `FCP 0x2000` · `COMP_TIME 0x4000` · `COMP_ABORT 0x8000` · `UNX_COMP 0x10000` · `RX_OVER 0x20000` · `MALF_TLP 0x40000` · `ECRC 0x80000` · `UNSUP 0x100000` · `ACSV 0x200000`. 해석과 복구는 [LP09](2026-10-31_LP09_aer_dpc_error_handling.html).

### 2.8 왜 확장 영역은 ECAM이 필요한가 — 그리고 커널의 cfg_size 판단

- **CF8/CFC**: 주소 레지스터의 register 필드가 [7:2] (dword 64개) → **256 B**. 0x100 이상을 표현할 비트가 없음 (일부 AMD 칩 비표준 확장 제외 [추정]).
- **ECAM**: offset 12비트 → 4 KiB 전체. SoC의 비표준 config 접근(DesignWare iATU 등)도 보통 4 KiB 지원.
- **커널은 함수마다 `cfg_size`(256 또는 4096)를 정함** — `pci_cfg_space_size()` [확인됨: `drivers/pci/probe.c`]:
  - SR-IOV VF → 4096.
  - 버스가 `PCI_BUS_FLAGS_NO_EXTCFG` (예: 확장 config 미지원 호스트) → 256.
  - host bridge class 또는 **PCIe 장치(PCIe cap 있음)** → `pci_cfg_space_size_ext()`: 0x100 dword를 읽어 **all-ones이거나 0x000과 alias(같은 값 반복)면 256**, 아니면 4096.
  - PCI-X 266/533 → 같은 검사, 그 외 → 256.
- sysfs `config` 파일 크기 = `cfg_size` → **실측**: QEMU `edu`(PCIe cap 없음)와 Lima virtio 전부 **256**, QEMU PCIe 장치들 **4096**.

### 2.9 sysfs `config` 읽기 권한 — "lspci가 sudo를 원하는 이유"

```c
/* drivers/pci/pci-sysfs.c, pci_read_config() — Linux 6.18 [확인됨] */
unsigned int size = 64;
/* Several chips lock up trying to read undefined config space */
if (file_ns_capable(filp, &init_user_ns, CAP_SYS_ADMIN))
        size = dev->cfg_size;
else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
        size = 128;
```

- **일반 사용자 = 헤더 64 B만** → capability가 안 보임 → `lspci -vvv`가 `Capabilities: <access denied>` 를 출력 [실측: Lima `lspci -vvv -s 00:01.0` non-root]. root면 `cfg_size` 전체.
- 이유(커널 주석): 일부 칩은 정의 안 된 config 영역을 읽으면 멈춤.

## 3. Linux에서 볼 것

### 3.1 명령

| 명령 | 보는 것 |
|---|---|
| `sudo lspci -s BDF -vvv` | 헤더 + 모든 capability 디코드 (non-root면 cap 생략) |
| `sudo lspci -s BDF -xxx` / `-xxxx` | hex 256 B / 4 KiB (`-x` = 64 B) |
| `sudo lspci -s BDF -vvv -xxxx` | 디코드 + hex 같이 — offset 대조 공부에 최고 |
| `sudo hexdump -C /sys/bus/pci/devices/0000:BB:DD.F/config` | 같은 바이트를 커널 파일에서 직접 |
| `stat -c %s /sys/bus/pci/devices/*/config` | 256 vs 4096 (확장 공간 유무) |
| `sudo setpci -s BDF COMMAND.w` | 이름으로 레지스터 읽기 (`setpci --dumpregs` 이름 목록) |
| `sudo setpci -s BDF CAP_EXP+0x12.w` | **PCIe cap 위치를 몰라도** cap 기준 offset으로 (LnkSta) |
| `sudo setpci -s BDF ECAP_AER+0x10.l` | AER Correctable Status |
| `sudo setpci -s BDF ECAP_SRIOV+0x0e.w` | SR-IOV TotalVFs |
| `sudo setpci -s BDF COMMAND=0x0004:0x0004` | `값:마스크` 쓰기 — 마스크된 비트만 바꿈 (Bus Master on) [미실행: 쓰기는 VM에서 안 함] |
| `sudo setpci -s BDF ECAP_AER+0x10.l=0xffffffff` | AER correctable 상태 RW1C 지우기 [미실행] |
| `cat /sys/bus/pci/devices/BDF/resource` | BAR별 `start end flags` (sizing 결과) |
| `lspci -F dump.txt -vvv` | 떠 온 덤프를 오프라인 디코드 |

### 3.2 실측 — Lima VM virtio-net `00:01.0` (`sudo lspci -xxx`) 줄별 해석

```text
00:01.0 Ethernet controller: Red Hat, Inc. Virtio 1.0 network device (rev 01)
00: f4 1a 41 10 16 04 10 00 01 00 00 02 00 40 00 00
10: 04 00 00 80 03 00 00 00 00 15 00 50 00 00 00 00
20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 41 00
30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00
40: 09 50 10 01 00 00 00 00 00 00 00 00 38 00 00 00
50: 09 60 10 03 00 00 00 00 00 10 00 00 01 00 00 00
60: 09 74 14 02 00 00 00 00 00 40 00 00 08 00 00 00
70: 04 00 00 00 09 84 10 04 00 00 00 00 00 80 00 00
80: 11 00 00 00 11 00 02 80 02 00 00 00 32 00 00 00
90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

| 위치 | 바이트 | 해석 |
|---|---|---|
| 0x00 | `f4 1a` `41 10` | Vendor 0x1af4 (Red Hat/virtio), Device 0x1041 (virtio 1.0 net = 0x1040 + 1) — little-endian 주의 |
| 0x04 | `16 04` | Command 0x0416 → Mem+(bit1) BusMaster+(bit2) MWI(bit4) **DisINTx+**(bit10) — MSI-X 쓰는 중이라 INTx 끔 |
| 0x06 | `10 00` | Status 0x0010 → **Cap List+** |
| 0x08 | `01` `00 00 02` | Rev 01, class 0x020000 (Network / Ethernet) |
| 0x0D–0x0E | `40` `00` | Latency 0x40, Header Type 0 (single-function) |
| 0x10–0x17 | `04 00 00 80` `03 00 00 00` | BAR0/1 = 64-bit mem `0x3_8000_0000` (§2.4) |
| 0x18 | `00 15 00 50` | BAR2 = `0x50001500` 32-bit mem (MSI-X 테이블이 있는 BAR) |
| 0x2C | `f4 1a 41 00` | Subsystem 1af4:0041 |
| 0x34 | `40` | 첫 capability @0x40 |
| 0x3C–0x3D | `ff` `01` | Int Line 0xff(미사용), Int Pin 1 = INTA# |
| 0x40 | `09 50 10 01` | **Vendor-specific(0x09)**, next 0x50, len 0x10, virtio cfg_type 1 = CommonCfg; +0x04 BAR=0, +0x08 offset 0, +0x0C length 0x38 → lspci `VirtIO: CommonCfg BAR=0 offset=00000000 size=00000038` |
| 0x50 / 0x60 / 0x74 | `09 60…` `09 74…` `09 84…` | virtio ISR / Notify / DeviceCfg 구조 (각각 vendor cap) |
| 0x84 | `11 00 02 80` | **MSI-X(0x11)**, next 0 (끝), Message Control 0x8002 → Enable+(bit15), Table Size 2+1 = **3 벡터** |
| 0x88 / 0x8C | `02 00 00 00` / `32 00 00 00` | Table: BIR 2, offset 0 · PBA: BIR 2, offset 0x30 → lspci `Vector table: BAR=2 offset=00000000  PBA: BAR=2 offset=00000030` ✓ |

- **이 장치엔 PCIe capability(0x10)가 없음** → 가상 "conventional PCI" 장치, cfg_size 256, 확장 cap 없음. LnkSta도 없음 → Mac Lima에서는 링크 공부가 안 되는 이유.

### 3.3 실측 — non-root vs root (sysfs `config` 64 B 규칙)

```text
$ hexdump -C /sys/bus/pci/devices/0000:00:01.0/config | tail -3        # 일반 사용자
00000020  00 00 00 00 00 00 00 00  00 00 00 00 f4 1a 41 00  |..............A.|
00000030  00 00 00 00 40 00 00 00  00 00 00 00 ff 01 00 00  |....@...........|
00000040                                                                  ← 64 B에서 끝
$ ls -l /sys/bus/pci/devices/0000:00:01.0/config
-rw-r--r-- 1 root root   256 Oct 31 19:01 config                         ← 파일 크기는 256 (cfg_size)
```

- 파일 크기는 256인데 일반 사용자는 64 B만 읽힘 — §2.9 코드 그대로. 내 도구도 같은 상황을 감지 (§6.3).

### 3.4 실측 — QEMU 게스트의 PCIe 장치들 (`lspci -F` 디코드, 발췌)

**NVMe `03:00.0` — 드라이버 없음 → Command 전부 0** (nvme가 모듈이라 initramfs에 없음)

```text
03:00.0 Non-Volatile memory controller: Red Hat, Inc. QEMU NVM Express Controller (rev 02) (prog-if 02 [NVM Express])
	Control: I/O- Mem- BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-
	Region 0: Memory at 10000000 (64-bit, non-prefetchable) [disabled]
	Capabilities: [40] MSI-X: Enable- Count=65 Masked-
		Vector table: BAR=0 offset=00002000
		PBA: BAR=0 offset=00003000
	Capabilities: [80] Express (v2) Endpoint, IntMsgNum 0
		DevCap:	MaxPayload 128 bytes, PhantFunc 0, Latency L0s <64ns, L1 <1us
			ExtTag+ AttnBtn- AttnInd- PwrInd- RBE+ FLReset+ SlotPowerLimit 0W TEE-IO-
		LnkCap:	Port #0, Speed 2.5GT/s, Width x1, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp-
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-
	Capabilities: [60] Power Management version 3
```

| 줄 | 해석 |
|---|---|
| `Control: … Mem- BusMaster-` | 아무도 `pci_enable_device()` / `pci_set_master()`를 안 불렀음 → BAR는 **주소는 배정됐지만 디코드 꺼짐** |
| `Region 0: … [disabled]` | lspci가 Command bit1=0을 보고 붙이는 표시. 드라이버 bind 전 정상 상태 |
| `MSI-X: Enable- Count=65` | Message Control `0x0040` → Table Size 0x40+1 = 65 (NVMe: admin + I/O 큐) |
| `FLReset+` | DevCap bit28 → FLR 지원 |
| `LnkSta … DLActive-` | **endpoint의 DLActive는 의미 없음** (`LLActRep-`). 링크 판단은 위 DSP `02:00.0`의 `DLActive+`로 ([N01](2026-10-31_N01_pcie_lspci_ltssm.html)) |
| `Capabilities: [80] … [60]` | 0x40 → 0x80 → 0x60, 또 주소 역순 섞임 |

비교: 드라이버가 붙은 e1000e/igb는 `Command 0x0406` (Mem+ BusMaster+ DisINTx+) — 드라이버 probe가 `pci_enable_device_mem()` + `pci_set_master()` + MSI-X 설정을 한 결과 [실측: pcicfg_walk 출력 §6.4].

**root port `00:01.0` 확장 영역** (`100:` 행 `01 00 82 14` → AER v2 next 0x148, `148:` 행 `0d 00 01 00` → ACS v1 끝)

```text
	Capabilities: [100 v2] Advanced Error Reporting
		CESta:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr- CorrIntErr- HeaderOF-
		CEMsk:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr+ CorrIntErr+ HeaderOF+
		RootCmd: CERptEn+ NFERptEn+ FERptEn+
	Capabilities: [148 v1] Access Control Services
		ACSCap:	SrcValid+ TransBlk+ ReqRedir+ CmpltRedir+ UpstreamFwd+ EgressCtrl- DirectTrans+
```

- `CEMsk … AdvNonFatalErr+ CorrIntErr+ HeaderOF+` = Correctable Mask `0x0000e000` (bit 13–15 마스크) — 내 도구 출력 `CorMask 0x0000e000`과 일치.
- `RootCmd: CERptEn+ …` = AER Root Error Command(+0x2C) — portdrv AER 서비스가 켠 것 (dmesg `AER: enabled with IRQ 16`).

### 3.5 실측 — `setpci`

```text
# Lima VM (실기, sudo)
$ sudo setpci -s 00:01.0 COMMAND.w           → 0416
$ sudo setpci -s 00:01.0 0x00.l              → 10411af4            (Device<<16 | Vendor)
$ sudo setpci -s 00:0c.0 CAP_MSI+2.w         → 008a                (xHCI MSI Message Control)
$ sudo setpci -s 00:01.0 CAPABILITY_LIST.b   → setpci: Unknown register "CAPABILITY_LIST".   ← 이름은 CAPABILITIES

# QEMU 덤프 파일 대상 (-A dump -O dump.name=dump.txt)
-s 00:01.0 CAP_EXP+0x12.w      → 2011      LnkSta: 2.5GT/s x1 DLActive+
-s 00:01.0 ECAP_AER+0x10.l     → 00000000  Correctable Status 깨끗
-s 07:00.0 ECAP_SRIOV+0x0e.w   → 0008      TotalVFs 8
-s 07:00.0 ECAP_SRIOV+0x14.w   → 0080      First VF Offset 128
-s 07:00.0 ECAP_SRIOV+0x16.w   → 0002      VF Stride 2
```

- `008a` 해석: bit0 Enable=0, Multiple Message Capable `[3:1]` = 5 → 2^5 = 32 벡터, bit7 64-bit=1 → lspci `MSI: Enable- Count=1/32 Maskable- 64bit+` ✓.
- `CAP_EXP`, `CAP_MSI`, `ECAP_AER`, `ECAP_SRIOV` 같은 이름은 setpci가 **리스트를 걸어 그 cap의 위치를 찾은 뒤** offset을 더함 — 장치마다 cap 위치가 달라도 같은 명령이 통함.

## 4. 꼬리 질문 체인

### 체인 A — "What's in configuration space?"

<details><summary>Q. What's in configuration space? → 답 보기</summary>

**A.** "Each function has 256 bytes for conventional PCI or 4 KiB for PCIe. The first 64 bytes are the standard header: vendor and device ID, command and status, class code, header type, BARs, subsystem IDs and the capabilities pointer at 0x34. From 0x40 to 0xFF there's a linked list of capabilities — PM, MSI, MSI-X, the PCI Express capability. From 0x100 up there's a second linked list of extended capabilities like AER, SR-IOV, ACS, DPC and the physical-layer ones."
- 한국어 핵심: 64 B 헤더 → 0x40–0xFF cap 리스트 → 0x100–0xFFF ext cap 리스트.

</details>

<details><summary>꼬리1. How do you find the PCIe capability? → 답 보기</summary>

**A.** "Check that Status bit 4 — Capabilities List — is set, read the byte at 0x34, mask off the low two bits, and follow the chain: each entry has its ID in the first byte and the next pointer in the second. Stop at ID 0x10, which is PCI Express, or when next is zero. The order isn't by address, so you have to walk it — on a QEMU root port I saw 0x54, then 0x48, then 0x40. In Linux you'd just call `pci_find_capability(dev, PCI_CAP_ID_EXP)`, and `dev->pcie_cap` caches it; from user space `setpci CAP_EXP+offset` does the walk for you."
- 한국어 핵심: Status bit4 → 0x34 & 0xFC → (ID, next) 따라가기 → ID 0x10. 주소 순서 아님.

</details>

<details><summary>꼬리2. Why can't you see AER with legacy config access? → 답 보기</summary>

**A.** "AER is an extended capability, so it lives at 0x100 or above. The legacy x86 CF8/CFC mechanism only has a register field for the first 256 bytes, so there's no way to address it. You need ECAM — or the SoC's equivalent — which gives each function its full 4 KiB. Linux decides per function whether extended space is usable; if it isn't, the sysfs config file is only 256 bytes and lspci shows no extended capabilities."
- 한국어 핵심: AER은 0x100 이상 → CF8/CFC는 256 B → ECAM 필요. 커널 `cfg_size`가 256이면 안 보임.

</details>

<details><summary>꼬리3. What happens if the cap list loops or points to garbage? → 답 보기</summary>

**A.** "A naive walker hangs or reads nonsense, so you guard it. Mask the low two bits of every pointer, reject pointers below 0x40, and bound the number of hops — the kernel uses a TTL of 48 for the standard list, since that's the most four-byte entries that fit in 0x40 to 0xFF, and 480 for the extended list. Also treat all-ones as 'device gone' — if the link drops mid-walk every read returns 0xFF and you'd otherwise loop. My walker returns ELOOP or EINVAL in those cases, and that's exactly what I'd want a validation tool to flag as a firmware bug."
- 한국어 핵심: 포인터 & 0xFC, < 0x40 거부, hop 제한(48 / 480), all-ones = 장치 사라짐.

</details>

<details><summary>꼬리4. How is the extended capability header different? → 답 보기</summary>

**A.** "It's a 32-bit header instead of two bytes: a 16-bit ID, a 4-bit version and a 12-bit next offset in the top bits. The list always starts at 0x100, and a zero dword there means no extended capabilities, while all-ones usually means the extended space isn't reachable. The IDs are a separate number space — 0x10 in the standard list is PCI Express, but 0x0010 in the extended list is SR-IOV."
- 한국어 핵심: 32-bit 헤더 = ID16 · ver4 · next12, 0x100 시작, ID 공간 별도 (0x10 vs 0x0010).

</details>

### 체인 B — "What does the Command register's Bus Master bit do?"

<details><summary>Q. What does the Command register's Bus Master bit do? → 답 보기</summary>

**A.** "Bit 2 of Command, Bus Master Enable, allows the function to issue its own requests upstream — memory reads and writes. Without it, the device can be accessed by the CPU but can't initiate anything. In Linux the driver sets it with `pci_set_master()` after `pci_enable_device()`, which turns on memory and I/O decode."
- 한국어 핵심: bit2 = 장치가 스스로 요청(메모리 R/W) 발행 허가. `pci_set_master()`.

</details>

<details><summary>꼬리1. What breaks if the driver forgets it? → 답 보기</summary>

**A.** "Two things. DMA doesn't happen — the device can't read descriptors or write data, so you see timeouts with no PCIe errors. And MSI and MSI-X interrupts don't arrive, because an MSI is just a posted memory write to the interrupt controller's doorbell address — so the driver looks like it's missing interrupts. Register access from the CPU still works, which makes it confusing: the device probes fine, and then nothing moves."
- 한국어 핵심: DMA 안 됨 + MSI/MSI-X 안 옴 (MSI = 메모리 쓰기). CPU의 MMIO는 되므로 헷갈림.

</details>

<details><summary>꼬리2. And if Memory Space Enable is off? → 답 보기</summary>

**A.** "Then the BARs don't decode: CPU loads to the BAR address get an Unsupported Request and the root complex returns all-ones, writes are dropped, and lspci marks the region as disabled. That's the normal state before a driver binds — I saw it on an NVMe in QEMU where no driver was loaded: Command was zero and lspci said 'Region 0 … [disabled]'."
- 한국어 핵심: BAR 디코드 꺼짐 → MMIO 읽기 all-ones, 드라이버 bind 전 정상. 실측 NVMe Command 0x0000.

</details>

<details><summary>꼬리3. How would you check it from the shell, and how would you change it? → 답 보기</summary>

**A.** "Read it with `setpci -s BDF COMMAND.w` or look for `BusMaster+` in `lspci -vv`. To flip only that bit, use the value-colon-mask form, `setpci -s BDF COMMAND=0x4:0x4`, so you don't disturb the other bits — and do it as a 16-bit write, because Command shares a dword with Status, whose error bits are write-one-to-clear. In practice you don't do it behind a driver's back; it's a bring-up or debug trick."
- 한국어 핵심: `setpci COMMAND.w` 읽기, `COMMAND=0x4:0x4` 마스크 쓰기, 16-bit로 (Status RW1C 보호).

</details>

### 체인 C (추가) — BAR

<details><summary>Q. How does BAR sizing work? → 답 보기</summary>

**A.** "Disable memory and I/O decode, save the BAR, write all ones, read it back, restore it. The device hardwires the low address bits to zero according to its size, so after masking the flag bits — four bits for memory, two for I/O — size equals the inverted mask plus one. A 64-bit BAR uses two consecutive registers and you size them together. If it reads back zero the BAR isn't implemented. Then re-enable decode."
- 한국어 핵심: decode 끔 → 저장 → all-1s → 읽기 → 복원, `size = ~(val & ~flags) + 1`, 64-bit는 쌍.

</details>

<details><summary>꼬리1. What does prefetchable mean and why does it matter? → 답 보기</summary>

**A.** "Prefetchable means reads have no side effects, so a bridge or CPU may read ahead or merge accesses. That lets the region be mapped write-combining and placed in the bridge's prefetchable window, which is the one that can be 64-bit and above 4 GB. Registers with read-to-clear behavior must be non-prefetchable. Large memory like GPU or accelerator buffers is usually 64-bit prefetchable."
- 한국어 핵심: 읽기 부작용 없음 → 미리/합쳐 읽기 가능, WC 매핑, 64-bit pref window. 레지스터는 non-pref.

</details>

<details><summary>꼬리2. Why does lspci show sizes for the live system but not for a dump file? → 답 보기</summary>

**A.** "lspci never re-sizes BARs itself — writing all ones to a live device behind a driver would be dangerous. It reads the sizes the kernel already computed from sysfs `resource`. A dump only contains config bytes, so it can show the base address and flags but not the size."
- 한국어 핵심: lspci는 sysfs `resource`에서 크기를 가져옴. 덤프엔 없음.

</details>

### 체인 D (추가) — 권한과 도구

<details><summary>Q. Why does lspci -vvv show less when you're not root? → 답 보기</summary>

**A.** "The kernel's sysfs config read only returns the first 64 bytes — the standard header — to an unprivileged user, because some chips misbehave when undefined config space is read. So lspci can print IDs, command, status and BARs, but every capability is beyond 64 bytes, and it prints 'access denied'. As root you get the function's full config size: 256 or 4096 bytes."
- 한국어 핵심: non-root = 64 B (CardBus 128 B), root = cfg_size 전체. 실측 확인.

</details>

<details><summary>꼬리1. How would you write the same thing without lspci — say on bare metal? → 답 보기</summary>

**A.** "Exactly the same walk, just over ECAM instead of a sysfs file: compute base plus bus, device, function shifts, read the header, follow 0x34 for the standard list and 0x100 for the extended one, with the same guards, and decode the registers by offset. I wrote a small tool that does this over the sysfs file and a fake buffer for unit testing; on bare metal you'd swap the accessor for aligned 32-bit MMIO reads."
- 한국어 핵심: 같은 알고리즘, 접근자만 ECAM MMIO(32-bit 정렬)로 교체.

</details>

### 체인 E (추가) — PCIe capability로 검증 질문

<details><summary>Q. Which registers would you check right after a link comes up on new silicon? → 답 보기</summary>

**A.** "On the downstream port: Link Status for speed, width and DLActive, Link Status 2 for equalization complete at Gen3 and above, Slot Status for presence. On both ends: Link Capabilities against the spec sheet, Device Control for max payload and max read request — they must be consistent along the path — Device Status and AER for any errors latched during training, and the Physical Layer 16 or 32 GT/s capabilities for per-lane equalization results. I'd log all of it on every reset cycle so a regression shows up as a statistic, not an anecdote."
- 한국어 핵심: DSP LnkSta/LnkSta2/SltSta, 양쪽 LnkCap·DevCtl(MPS/MRRS)·DevSta·AER, PL16/32 lane EQ — 리셋 사이클마다 기록.

</details>

<details><summary>꼬리1. What's the difference between LnkCap max speed and LnkCap2? → 답 보기</summary>

**A.** "From PCIe 3.0 on, Link Capabilities 2 has a supported-speeds bit vector — bit 1 for 2.5, up to bit 6 for 64 GT/s — and the Max Link Speed field in Link Capabilities, as well as the Current Link Speed and Target Link Speed fields, are interpreted as indexes into that vector. Older devices without the vector use the classic encoding where 1 means 2.5 and 2 means 5. In practice the values line up, but a tool should read the vector when the capability version is 2."
- 한국어 핵심: Gen3+는 LnkCap2 벡터(bit1=2.5 … bit6=64)의 인덱스로 speed 필드 해석.

</details>

## 5. Don 경험 연결

- "At Solidigm the PCIe/NVMe subsystem IP had to present this config space correctly — capability chains, BAR sizing behavior, AER — and in pre-silicon FPGA bring-up we checked it from the host side before RTL freeze. So a broken capability chain or a BAR that doesn't size is a bug class I've chased from the device side [숫자 채우기: 예시 버그 1개]."
- "For SK hynix chamber reliability on Gen3/4/5 SSDs, the automation read Link Status and the AER correctable counters after every power cycle and temperature step; trending Receiver Error and Bad DLLP counts was how we saw margin erode before a link actually failed [숫자 채우기: 사이클 수/온도 범위]."
- "At Apple, when a PCIe wireless part misbehaved, the first thing I checked was whether Bus Master and MSI were actually enabled and whether AER had latched anything — that splits 'driver didn't set it up' from 'the link is corrupting packets' in a couple of minutes."

## 6. 실습 / 코드

### 6.1 [onsitePrep/lp02/pcicfg_walk.c](onsitePrep/lp02/pcicfg_walk.c) — 무엇을 하나

- `pcicfg_walk 0000:00:01.0` — `/sys/bus/pci/devices/<BDF>/config` 를 읽어 헤더(Command/Status/BAR/브리지 버스 번호·window) + **standard cap 리스트** + **PCIe cap 디코드** + **extended cap 리스트**(+AER/DSN) 출력.
- `pcicfg_walk -x <BDF>` — `lspci -xxxx` 형식 hex 덤프 → 다른 머신에서 `lspci -F`로 해석 가능 (LP01 QEMU 실측에 사용).
- `pcicfg_walk --selftest` — 가짜 config space(NVMe형 EP, root port, 루프, 깨진 포인터, all-ones, 256/64 B 잘린 뷰, BAR sizing 수학)로 assert. **macOS에서도 동작**.
- 모든 접근은 **경계 검사된 `rd8/rd16/rd32`** (범위 밖 = 0xFF, master abort 흉내) → ASan/UBSan 깨끗.

핵심 부분:

```c
int walk_std_caps(cfg_t c, cap_t *caps, int max)
{
    if (rd16(c, PCI_VENDOR_ID) == 0xffff)
        return -ENODEV;
    if (!(rd16(c, PCI_STATUS) & PCI_STATUS_CAP_LIST))
        return 0;
    unsigned p = rd8(c, PCI_CAPABILITY_LIST) & 0xfcu;          /* low 2 bits are reserved */
    int n = 0;
    for (int ttl = STD_CAP_TTL; p; ttl--) {                     /* STD_CAP_TTL = 48 */
        if (ttl == 0)
            return -ELOOP;
        if (p < 0x40 || p > 0xfc)                                 /* caps live in 0x40..0xff */
            return -EINVAL;
        if (p + 2 > c.len)
            return -ERANGE;                                       /* truncated view (non-root read) */
        if (n < max)
            caps[n] = (cap_t){ p, rd8(c, p), 0 };
        n++;
        p = rd8(c, p + 1) & 0xfcu;
    }
    return n;
}

int walk_ext_caps(cfg_t c, cap_t *caps, int max)
{
    if (c.len < PCI_CFG_SPACE_EXP_SIZE)
        return 0;                                                  /* 256 B view: no ext space */
    unsigned p = PCI_CFG_SPACE_SIZE;
    uint32_t h = rd32(c, p);
    if (h == 0 || h == 0xffffffffu)
        return 0;                                                  /* no extended capabilities */
    int n = 0;
    for (int ttl = EXT_CAP_TTL; ; ttl--) {                         /* EXT_CAP_TTL = 480 */
        if (ttl == 0)
            return -ELOOP;
        if (n < max)
            caps[n] = (cap_t){ p, PCI_EXT_CAP_ID(h), PCI_EXT_CAP_VER(h) };
        n++;
        p = PCI_EXT_CAP_NEXT(h);
        if (p == 0)
            return n;
        if (p < PCI_CFG_SPACE_SIZE)
            return -EINVAL;
        h = rd32(c, p);
        if (h == 0 || h == 0xffffffffu)
            return -EINVAL;
    }
}
```

### 6.2 macOS selftest [검증됨: 2026-10-31, ALL PASS]

```bash
cd onsitePrep/lp02
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined pcicfg_walk.c -o /tmp/pcicfg_walk && /tmp/pcicfg_walk --selftest
```

```text
== fake 01:00.0 NVMe endpoint  (4096 bytes of config space)
  [00] 144d:a808  class 010802 rev 00  header type 0 (Type 0 / device)
  [04] Command 0x0406: I/O- Mem+ BusMaster+ SERR- DisINTx+
  [06] Status  0x0010: Cap+ INTx- <MAbort- >SERR- DetParity-
  BAR0  [10] 0xfe000004  mem 64-bit non-prefetchable  base=0xfe000000  (BAR1 = upper half)
  cap  [40] id 0x01 Power Management   next 0x50
  cap  [50] id 0x11 MSI-X              next 0x70
  cap  [70] id 0x10 PCI Express        next 0x00
    PCIe cap v2, type 0 = Endpoint, Slot-
    DevCap [+04] 0x10000002  MaxPayload 512 B, FLR+
    DevCtl [+08] 0x2020      MaxPayload 256 B, MaxReadReq 512 B, CorrErr- NonFatal- Fatal- UnsupReq-
    LnkCap [+0c] 0x00400c44  Port #0, Speed 16GT/s, Width x4, ASPM L0s L1, DLActiveRep-
    LnkSta [+12] 0x1044      Speed 16GT/s, Width x4, Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-
    LnkCap2[+2c] 0x0000001e  SupportedSpeeds: 2.5GT/s 5GT/s 8GT/s 16GT/s
  ecap [100] id 0x0001 v2 AER                      next 0x148
      CorSta   0x00000041  (RxErr+ BadTLP+ BadDLLP- Rollover- Timeout-)  CorMask 0x00000000
  ecap [148] id 0x0003 v1 Device Serial Number     next 0x158
      Serial 01234567-89abcdef
  ecap [158] id 0x0019 v1 Secondary PCIe           next 0x000
== fake 02:00.0 dead link (all-ones)  (4096 bytes of config space)
  Vendor ID 0xffff: no function here (or the link is down)
== fake 00:01.0 root port  (4096 bytes of config space)
  Bus: primary=00 secondary=01 subordinate=01
  Memory window: 0x10000000-0x101fffff
  cap  [40] id 0x10 PCI Express        next 0x00
    PCIe cap v2, type 4 = Root Port, Slot+
    LnkSta [+12] 0x2043      Speed 8GT/s, Width x4, Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-
    SltCap [+14] 0x00080040  HotPlug+ PhysSlot #1 | SltCtl [+18] 0x0000 | SltSta [+1a] 0x0040 PresDet+ DLLStateChg-
SELFTEST ALL PASS
```

(출력 일부 줄 생략) assert 목록: cap 3개 순서/offset, `find_cap` ENOENT, ext AER/DSN/SecPCIe 위치, **256 B 뷰에서 AER 안 보임**, **64 B 뷰에서 `-ERANGE`**, 포인터 하위 비트 마스크, **std 루프 → `-ELOOP`**, 헤더 안을 가리키는 포인터 → `-EINVAL`, **ext 루프 → `-ELOOP`**, ext next < 0x100 → `-EINVAL`, all-ones → `-ENODEV`, root port 버스 번호·DLActive, **BAR sizing 6케이스**(16 B, 16 KiB, 64-bit 16 MiB, 64-bit 8 GiB, I/O 256 B, 미구현 0).

### 6.3 Lima VM 실기 [검증됨: gcc 15.2 `-Werror`, ASan 빌드 selftest PASS]

```bash
# Lima VM 안 (rootfs RO 상태 → /tmp 사용)
mkdir -p /tmp/LP02 && cd /tmp/LP02 && cp <repo>/onsitePrep/lp02/pcicfg_walk.c .
gcc -std=c11 -O2 -Wall -Wextra -Werror pcicfg_walk.c -o pcicfg_walk
gcc -std=c11 -O2 -Wall -Wextra -Werror -static pcicfg_walk.c -o pcicfg_walk.static   # QEMU initramfs용
./pcicfg_walk 0000:00:01.0          # 일반 사용자
sudo ./pcicfg_walk 0000:00:01.0     # root
```

```text
--- non-root
== 0000:00:01.0  (64 bytes of config space)
  [00] 1af4:1041  class 020000 rev 01  header type 0 (Type 0 / device)
  [04] Command 0x0416: I/O- Mem+ BusMaster+ SERR- DisINTx+
  [06] Status  0x0010: Cap+ INTx- <MAbort- >SERR- DetParity-
  BAR0  [10] 0x80000004  mem 64-bit non-prefetchable  base=0x380000000  (BAR1 = upper half)
  BAR2  [18] 0x50001500  mem 32-bit non-prefetchable  base=0x50001500
  [3d] Interrupt pin 1 (INTA..INTD = 1..4)
  capability list: beyond the bytes we could read (run as root)
  ext caps: none visible (no PCIe capability -> kernel exposes 256 B)
--- root
== 0000:00:01.0  (256 bytes of config space)
  ...
  cap  [40] id 0x09 Vendor-Specific    next 0x50
  cap  [50] id 0x09 Vendor-Specific    next 0x60
  cap  [60] id 0x09 Vendor-Specific    next 0x74
  cap  [74] id 0x09 Vendor-Specific    next 0x84
  cap  [84] id 0x11 MSI-X              next 0x00
  ext caps: none visible (no PCIe capability -> kernel exposes 256 B)
```

### 6.4 QEMU 게스트 실기 (PCIe 장치) [검증됨: 정적 빌드를 initramfs에 넣어 실행, LP01 §6.1 명령]

```text
== 0000:00:01.0  (4096 bytes of config space)
  [00] 1b36:000c  class 060400 rev 00  header type 1 (Type 1 / bridge)
  [04] Command 0x0407: I/O+ Mem+ BusMaster+ SERR- DisINTx+
  BAR0  [10] 0x10c00000  mem 32-bit non-prefetchable  base=0x10c00000
  Bus: primary=00 secondary=01 subordinate=06
  Memory window: 0x10000000-0x109fffff
  Prefetchable window: 0x8000000000-0x8000bfffff
  I/O window: 0x1000-0x5fff   BridgeCtl=0x0002 SecBusReset-
  cap  [54] id 0x10 PCI Express        next 0x48
    PCIe cap v2, type 4 = Root Port, Slot+
    DevCtl [+08] 0x000f      MaxPayload 128 B, MaxReadReq 128 B, CorrErr+ NonFatal+ Fatal+ UnsupReq+
    LnkCap [+0c] 0x00300604  Port #0, Speed 16GT/s, Width x32, ASPM L0s, DLActiveRep+
    LnkSta [+12] 0x2011      Speed 2.5GT/s, Width x1, Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-
    SltCap [+14] 0x000a007b  HotPlug+ PhysSlot #1 | SltCtl [+18] 0x03c0 | SltSta [+1a] 0x0050 PresDet+ DLLStateChg-
    LnkCap2[+2c] 0x0000001e  SupportedSpeeds: 2.5GT/s 5GT/s 8GT/s 16GT/s
  cap  [48] id 0x11 MSI-X              next 0x40
  cap  [40] id 0x0d Bridge Subsystem VID next 0x00
  ecap [100] id 0x0001 v2 AER                      next 0x148
      UncorSta 0x00000000  UncorMask 0x02400000  UncorSev 0x00462030
      CorSta   0x00000000  (RxErr- BadTLP- BadDLLP- Rollover- Timeout-)  CorMask 0x0000e000
  ecap [148] id 0x000d v1 ACS                      next 0x000
== 0000:05:00.0  (256 bytes of config space)
  [00] 1234:11e8  class 00ff00 rev 10  header type 0 (Type 0 / device)
  cap  [40] id 0x05 MSI                next 0x00
  ext caps: none visible (no PCIe capability -> kernel exposes 256 B)
== 0000:07:00.0  (4096 bytes of config space)
  [00] 8086:10c9  class 020000 rev 01  header type 0 (Type 0 / device)
  [04] Command 0x0406: I/O- Mem+ BusMaster+ SERR- DisINTx+
  BAR2  [18] 0x00006001  I/O  base=0x6000
  BAR3  [1c] 0x10a40004  mem 64-bit non-prefetchable  base=0x10a40000  (BAR4 = upper half)
  cap  [40] id 0x01 Power Management   next 0x50
  cap  [50] id 0x05 MSI                next 0x70
  cap  [70] id 0x11 MSI-X              next 0xa0
  cap  [a0] id 0x10 PCI Express        next 0x00
  ecap [100] id 0x0001 v1 AER                      next 0x150
  ecap [150] id 0x000e v1 ARI                      next 0x160
  ecap [160] id 0x0010 v1 SR-IOV                   next 0x000
```

- 도구 출력과 `lspci -F` 출력이 모든 offset·ID에서 일치 (root port cap 0x54/0x48/0x40, AER 0x100 → ACS 0x148, igb AER → ARI → SR-IOV, UncorMask `0x02400000` = lspci `UncorrIntErr+ TLPBlockedErr+`).
- 덤프 원본: [onsitePrep/lp02/qemu_virt_switch_dump.txt](onsitePrep/lp02/qemu_virt_switch_dump.txt).

### 6.5 확장 아이디어 [미실행]

- 같은 코드를 bare-metal로: `rd32()`를 ECAM MMIO(`*(volatile uint32_t *)(ecam + PCIE_ECAM_OFFSET(...))`)로 바꾸고 8/16-bit는 dword에서 추출 — [L2 문제](hmScreenPrep/problems/L2_pcie_cap_walk/index.html)가 바로 그 형태(정렬 dword 접근만, BAR sizing 포함).
- 검증 도구로: 기대값 YAML(장치별 cap 목록·LnkCap·MPS)과 비교해 pass/fail + AER 카운터 diff 기록 ([LP14](2026-10-31_LP14_validation_methodology.html)).

## Ethernet/MIPI 비교

| 항목 | PCIe config space | Ethernet PHY (MDIO/MII 관리 레지스터) | MIPI CSI-2 센서 |
|---|---|---|---|
| 표준 레지스터 | 64 B 헤더 + cap 리스트 | **Clause 22**: 레지스터 0–31 (0 BMCR 제어, 1 BMSR 상태: link status bit2, 2–3 PHY ID), 확장은 **Clause 45** MMD | 센서별 레지스터 맵 (I2C/CCI), 표준 거의 없음 |
| 기능 발견 | capability 연결 리스트 | BMSR/ESTATUS 능력 비트 (10/100/1000, autoneg 가능) | 데이터시트 |
| 링크 상태 | LnkSta (speed/width/DLActive) | BMSR link bit, autoneg 결과 레지스터 | CSI-2 RX 컨트롤러의 lane/에러 레지스터 |
| 에러 카운터 | AER Correctable/Uncorrectable | MAC 통계(FCS/CRC error), PHY 벤더 에러 카운터 | ECC/CRC 에러, packet header 에러 (RX 측) |

## 7. 자주 틀리는 것

1. **cap 리스트가 주소 순서라고 가정** — 순서는 장치 마음 (실측 0x54 → 0x48 → 0x40). 반드시 걷기.
2. **포인터 하위 2비트 마스크 안 함** — reserved 비트가 1인 장치가 있음 (`& 0xFC`).
3. **루프 가드 없음** — 펌웨어 버그·링크 다운(all-ones) 때 무한 루프. std 48 / ext 480.
4. **standard ID와 extended ID 혼동** — 0x10 = PCIe cap, 0x0010 = SR-IOV.
5. **Status 에러 비트를 0을 써서 지우려 함** — RW1C: 1을 써야 지워짐. Command 32-bit RMW로 Status 에러를 실수로 지우는 것도 흔함.
6. **Bus Master = DMA만이라고 생각** — **MSI/MSI-X도** 꺼짐 (MSI = 메모리 쓰기 TLP).
7. **endpoint의 `DLActive-`를 링크 다운으로 해석** — DLActive는 downstream port(LLActRep+)에서 의미. endpoint/USP는 `-`가 정상 (실측 NVMe, USP).
8. **BAR sizing 중 decode를 안 끄거나 원래 값을 복원 안 함** — 엉뚱한 주소 디코드, 64-bit 상위 BAR 누락.
9. **`lspci -vvv`가 cap을 안 보여줌 → 장치 문제라고 판단** — non-root면 64 B만 (실측). sudo.
10. **DevCap2/LnkCap2를 v1 cap에서 읽음** — cap version 1이면 +0x24 이후는 없음 (`pcie_capability_read_*` 사용).
11. **"확장 cap이 없음" vs "확장 공간 접근 불가" 혼동** — 0x100 dword가 0이면 전자, all-ones(또는 sysfs 256 B)면 후자 (컨트롤러/ECAM 문제 가능).

## 8. 출처

1. Linux 6.18 `include/uapi/linux/pci_regs.h` (Lima VM `/var/lab/src/linux-6.18.55`, 모든 offset/ID/비트 대조), 업스트림: https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
2. Linux `drivers/pci/pci-sysfs.c` (`pci_read_config` 64 B 규칙), `drivers/pci/probe.c` (`pci_cfg_space_size`), `drivers/pci/pci.h` (`PCI_FIND_CAP_TTL`, ext TTL) — https://github.com/torvalds/linux/tree/master/drivers/pci
3. Linux sysfs PCI 문서: https://docs.kernel.org/PCI/sysfs-pci.html
4. Linux PCI 드라이버 가이드 (`pci_enable_device`, `pci_set_master`, BAR): https://docs.kernel.org/PCI/pci.html
5. Linux AER 문서: https://docs.kernel.org/PCI/pcieaer-howto.html
6. pciutils `lspci(8)` / `setpci(8)` (`CAP_*`/`ECAP_*` 이름, `값:마스크` 쓰기, `-A dump`): https://man7.org/linux/man-pages/man8/lspci.8.html , https://man7.org/linux/man-pages/man8/setpci.8.html
7. pciutils `lib/header.h` (LnkSta2 등 비트 이름): https://github.com/pciutils/pciutils/blob/master/lib/header.h
8. OSDev wiki PCI (헤더 레이아웃, BAR sizing): https://wiki.osdev.org/PCI
9. 실측: Lima VM `elinux` (Ubuntu, kernel 7.0.0-38-generic, Apple VZ) `lspci -xxx/-vvv`, `setpci`, sysfs; QEMU 10.2.1 `virt` + 랩 커널 6.18.55 게스트 `pcicfg_walk` + `lspci -F` (2026-10-31)

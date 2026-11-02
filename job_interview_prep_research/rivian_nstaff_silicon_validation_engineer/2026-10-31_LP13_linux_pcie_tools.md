# LP13 · Linux PCIe 관측 도구 총정리 — lspci · setpci · sysfs · dmesg · debugfs · tracing
<!-- theme: pastel -->

> **묶음**: E 도구 · 검증 · 디버그 · **우선**: P0 (10-30 스크린 "What can you see with lspci?" 직결) · **선행 노트**: [N01 lspci·LTSSM](2026-10-31_N01_pcie_lspci_ltssm.html) · [LP02 config space](2026-10-31_LP02_config_space_capabilities.html) · [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · **이전 노트**: [LP12](2026-10-31_LP12_sriov_vfio_acs_p2p.html) · **다음 노트**: [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **코드**: [pcie_snapshot.py](onsitePrep/lp13/pcie_snapshot.py) · [QEMU 랩 스크립트](onsitePrep/lp13/lab/run_qemu.sh) · [실제 출력 모음](onsitePrep/lp13/outputs/qemu_lspci_vvv.txt)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "lspci로 뭘 보나", "lspci가 디코드 안 하는 레지스터는 어떻게 읽나", "lspci 없이(최소 rootfs / bare-metal) 같은 정보는?", "링크 상태를 1,000번 부팅 동안 어떻게 자동 수집하나", "Mac에서는?".
- **외울 것 ① — 모든 도구는 같은 config space를 읽는다.** lspci · setpci · sysfs `config` · macOS `ioreg` 전부 결국 장치의 **설정 공간(config space) 레지스터**를 보여주는 창문일 뿐. 도구 이름을 몰라도 "레지스터 수준"으로 답하면 된다.
- **외울 것 ② — 링크 문제는 위쪽 포트(root port / switch downstream port)의 `LnkSta`를 본다.** `DLActive+`(링크 살아 있음), `Train-`(학습 끝남), Speed/Width를 **양쪽 LnkCap 중 작은 값**과 비교. lspci의 `(downgraded)` 표시는 **아래쪽(endpoint) 장치에만** 붙는다 (pciutils 소스로 확인).
- **외울 것 ③ — root가 아니면 config space 64 바이트만 보인다** → `Capabilities: <access denied>`. sysfs `config` 파일도 같은 규칙(커널 `pci_read_config()`가 `CAP_SYS_ADMIN` 없으면 64 B로 자름). 실제로 Lima VM에서 재현함.
- 실제 랩: Lima VM(Apple VZ, virtio = conventional PCI라 링크 정보 없음) + **VM 안의 QEMU `-machine virt` + pcie-root-port + nvme/e1000e/스위치/빈 포트** 로 진짜 `LnkCap/LnkSta/AER` 출력을 뽑고, QEMU 모니터로 **AER 에러를 주입**해서 sysfs 카운터·dmesg까지 확인했다. `lspci -F <덤프>`로 오프라인 디코드도 검증.
- 30초 영어 답 (headline Q: "What can you see with lspci?"):
  - "lspci shows every PCI function the kernel enumerated, and with `-vvv` as root it decodes the whole configuration space: IDs and class, the command and status bits, the BARs and where they were mapped, the driver, and every capability — MSI/MSI-X, power management, and the PCI Express capability with Device, Link and Slot capability/control/status, plus extended capabilities like AER. For link debug I go to the downstream port above the device and compare LnkSta speed and width with both sides' LnkCap, check DLActive and Train, and then look at the AER status and the sysfs AER counters. Anything lspci doesn't decode I read directly with setpci or a `-xxxx` dump."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 아파트 단지와 관리사무소

PCIe 시스템을 **아파트 단지**라고 생각하자.

- 각 **장치(function)** = 한 세대(집). 집마다 현관 옆에 **표준 양식의 명판 + 계량기판**이 붙어 있다. 이게 **config space(설정 공간)**: "우리 집은 어느 회사(Vendor ID) 몇 호 모델(Device ID)이고, 전기를 얼마나 쓰고(BAR), 지금 상태는 어떻다(Status)"가 정해진 칸에 적혀 있다.
- **도로** = PCIe 링크. 단지 입구(root complex)에서 동(switch)을 거쳐 각 집까지 도로가 깔려 있다. 도로마다 **차선 수(width, x1/x4/x16)** 와 **제한 속도(speed, 2.5…64 GT/s)** 가 있다. 도로가 개통됐는지(링크 업 = L0, DLActive)는 도로 **입구 쪽 표지판**(downstream port의 Link Status)에 적혀 있다.
- **관리사무소 장부** = Linux 커널. 부팅 때 단지를 돌면서 집집마다 명판을 읽어 장부(`struct pci_dev`)를 만든다 = **열거(enumeration)**.
- 이제 도구들:
  - **`lspci`** = 장부와 명판을 **읽기 좋게 번역해 주는 열람 창구**. 숫자 대신 "Speed 16GT/s, Width x4" 처럼 사람말로 보여준다.
  - **`setpci`** = 명판/계량기판의 **특정 칸을 직접 읽거나 고쳐 쓰는 드라이버(공구)**. 번역은 없고 숫자 그대로.
  - **sysfs (`/sys/bus/pci/devices/...`)** = 관리사무소가 **집마다 게시판 파일**을 붙여 둔 것. `current_link_speed` 같은 파일을 `cat` 하면 바로 값이 나온다 → 스크립트로 자동화하기 제일 좋다.
  - **`dmesg`** = 관리 **일지**. "몇 시에 몇 동 몇 호 발견, 전기 배정, 에러 발생" 기록.
  - **debugfs / ftrace** = 관리사무소 **내부 CCTV**. 커널 함수가 언제 불렸는지, 컨트롤러 내부 상태(LTSSM)가 뭔지.
  - **프로토콜 애널라이저 / 스코프** = **도로 위 CCTV와 속도계**. 소프트웨어가 못 보는 것(도로가 왜 개통 안 되는지, 신호가 얼마나 깨끗한지)을 본다.

```text
                사람이 보는 것                      실제로 읽는 곳
 ┌───────────────────────────────┐
 │ lspci -vvv  (번역해서 보여줌)  │──┐
 │ setpci CAP_EXP+12.w (숫자 그대로)│──┤   libpci (pciutils 라이브러리)
 │ python pcie_snapshot.py        │──┤      │  access method: linux-sysfs (기본)
 └───────────────────────────────┘  │      ▼
                                    └──► /sys/bus/pci/devices/0000:01:00.0/config   ◄── cat/hexdump도 가능
 /sys/.../current_link_speed ─────────────► 커널 pci core (pci_read_config_*)
 dmesg / ftrace / debugfs ────────────────►      │
                                                 ▼  ECAM(메모리 매핑 config) 접근
                                          Root Complex ──링크──► 장치의 config 레지스터
 프로토콜 애널라이저 / 스코프 ────────────────────────▲ (링크 위를 직접 관찰)
```

- 핵심: **lspci는 마법이 아니라 "config 레지스터 번역기"**. 그래서 lspci가 없는 환경(BusyBox, bare-metal)에서도 **같은 레지스터를 직접 읽으면 같은 답**이 나온다. 이 노트 §3.13에서 BusyBox `hexdump`로 뜬 덤프를 다른 머신의 `lspci -F`로 번역해 똑같은 결과가 나오는 것을 실제로 확인했다.

### 1.2 "lspci로 뭘 보나"를 3단계로

1. **누가 있나** (목록): `lspci`, `lspci -tv` (트리) → "장치가 열거됐나?" 안 보이면 = config read 실패 = 링크가 안 올라왔거나 장치가 응답 안 함.
2. **어떻게 설정됐나** (`-v`/`-vv`): BAR가 어느 주소에 매핑됐나(`Region`), 드라이버가 붙었나(`Kernel driver in use`), 인터럽트(MSI/MSI-X)가 켜졌나, Bus Master가 켜졌나.
3. **링크/에러는 어떤가** (`-vvv` + root): PCI Express capability 안의 **LnkCap(할 수 있는 최대) vs LnkSta(지금 협상된 값)**, `DLActive`, AER(에러 보고) 상태.

### 1.3 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| config space | Configuration Space | 장치마다 있는 표준 양식 레지스터 묶음(256 B 기본, PCIe는 4 KiB) — 명판+계량기판 | 모든 도구가 읽는 원본 |
| BDF | Bus:Device.Function | 장치 주소 `01:00.0` (아파트 "동:호.방") | 모든 명령의 `-s` 인자 |
| domain/segment | PCI Segment Group | BDF 앞의 `0000:` — 독립된 단지 번호 | 호스트 브리지가 여러 개인 SoC에서 필수 (`-D`) |
| ECAM | Enhanced Configuration Access Mechanism | config space를 **메모리 주소**로 읽게 해 주는 창 (`base + bus<<20 + dev<<15 + fn<<12 + off`) | bare-metal에서 lspci 대신 직접 읽는 방법 |
| BAR | Base Address Register | 장치가 "내 레지스터를 이 크기만큼 메모리 주소에 매핑해 달라"고 요청하는 칸 | `Region 0: Memory at ...` 로 보임, MMIO 접근 주소 |
| capability | Capability List | config space 안에 연결 리스트로 이어진 기능 블록 (PM, MSI, PCIe, MSI-X) | `Capabilities: [80] Express` 의 `[80]`이 오프셋 |
| extended capability | Extended Capability | 0x100–0xFFF 영역의 기능 블록 (AER, ACS, LTR, L1SS, Lane Margining…) | root 권한 + ECAM 필요 |
| LnkCap / LnkCtl / LnkSta | Link Capabilities / Control / Status | 링크의 "최대 능력 / 설정 스위치 / 현재 상태" 레지스터 (PCIe cap +0x0C/+0x10/+0x12) | 링크 디버그의 1순위 |
| DLActive | Data Link Layer Link Active (LnkSta bit 13) | "데이터 링크 계층까지 올라왔다" = LTSSM이 L0에 도달 + flow control 초기화 완료 | "L0 확인"의 간접 증거 |
| LTSSM | Link Training and Status State Machine | 링크를 학습시키는 상태 기계 (Detect → Polling → Configuration → L0 …) | 표준 레지스터엔 없음 → vendor debug reg / debugfs |
| AER | Advanced Error Reporting | 에러를 종류별로 기록하는 extended capability + 커널 드라이버 | 신호 품질 추세, 장애 원인 |
| sysfs | (가상 파일 시스템 `/sys`) | 커널이 장치 정보를 파일로 보여 주는 게시판 | 스크립트 자동화의 기본 |
| libpci | pciutils library | lspci/setpci가 쓰는 공용 라이브러리, 여러 "access method" 지원 | `-A`, `-F` 옵션의 근거 |
| debugfs | Debug File System (`/sys/kernel/debug`) | 드라이버가 디버그용으로 여는 파일들 (ABI 보장 없음) | DWC 컨트롤러 `ltssm_status` 등 |
| ftrace | Function Tracer | 커널 함수 호출을 기록하는 내장 트레이서 (`/sys/kernel/tracing`) | 열거/드라이버 흐름 추적 |
| dynamic debug | dyndbg | 커널의 `pr_debug`/`dev_dbg` 메시지를 런타임에 켜는 기능 | PCI probe 상세 로그 |
| MMIO | Memory-Mapped I/O | 레지스터를 메모리 주소처럼 읽고 쓰는 방식 | BAR를 통해 장치 레지스터 접근 |
| VFIO | Virtual Function I/O | 장치를 커널 드라이버 대신 사용자 공간 프로그램에 안전하게(IOMMU 보호) 넘기는 프레임워크 | user-space 테스트 드라이버 |
| protocol analyzer | PCIe Protocol Analyzer | 링크 위의 TS1/TS2·TLP·DLLP를 실제로 캡처하는 HW 장비 | LTSSM/EQ 문제의 최종 증거 |
| interposer | Interposer | 슬롯과 카드 사이에 끼워 신호를 애널라이저로 빼 주는 보드 | 애널라이저 연결 수단 |
| exerciser | PCIe Exerciser | 원하는 트래픽/에러를 일부러 만들어 내는 장비 | 에러 경로·호환성 검증 |
| PERST# | PCIe Reset (Fundamental Reset 신호) | 보드가 장치에 주는 리셋 핀, `#`= active low | 링크가 안 뜰 때 스코프로 1순위 확인 |
| REFCLK | Reference Clock (100 MHz) | 양쪽 SerDes의 기준 클럭 | 지터/부재 시 링크 실패 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 lspci는 데이터를 어디서 가져오나 — libpci access method

- 근거: lspci/setpci man page와 커널 sysfs 코드 [1][4].

- `lspci -A help` 실제 출력 (Lima VM, pciutils 3.14): `linux-sysfs`, `linux-proc`, `dump`, `mmio-conf1`, `mmio-conf1-ext`, `ecam` [확인됨 VM 실행].
  - **linux-sysfs (기본)**: `/sys/bus/pci/devices/*/config` 파일을 읽는다. 커널이 대신 ECAM 접근.
  - **linux-proc**: 옛날 `/proc/bus/pci/BB/DD.F` 파일.
  - **dump**: `-F <file>` — 이전에 뜬 hex 덤프를 읽는다 (현장 버그 리포트 분석용).
  - **ecam**: `/dev/mem`으로 ECAM 영역을 **직접** 매핑 (`-O ecam.addrs=...` 또는 ACPI MCFG 테이블). 커널 PCI 드라이버가 없어도 된다 → "bare-metal과 같은 방식"을 Linux에서 흉내.
- **root vs non-root**: 커널 `drivers/pci/pci-sysfs.c`의 `pci_read_config()`는 `CAP_SYS_ADMIN`이 없으면 크기를 **64 바이트**로 자른다 (CardBus는 128) [확인됨 6.18.55 소스]. 주석 그대로 "Several chips lock up trying to read undefined config space". → 일반 사용자 `lspci -vvv`는 capability를 못 읽고 `Capabilities: <access denied>` 출력 (§3.1 실제 출력).
- man page 요약 [확인됨 lspci(8) 3.14]: "-xxx는 root만 (일부 장치가 정의 안 된 영역을 읽으면 crash)", "확장 config space 접근은 현재 linux_sysfs back-end에서만 지원".

### 2.2 lspci 옵션 전부 (man page 3.14 기준)

| 옵션 | 하는 일 | 언제 쓰나 |
|---|---|---|
| (없음) | `BB:DD.F class: vendor device (rev)` 한 줄씩 | 열거됐나? |
| `-t` / `-tv` | **트리**: 어느 포트 밑에 어느 장치. `-[03-05]` = 그 브리지의 secondary–subordinate 버스 범위 | 토폴로지, 장치가 어느 root port에 붙었나 |
| `-v` / `-vv` / `-vvv` | 상세도 1/2/3. `-vv`가 "useful 전부", `-vvv`는 "파싱 가능한 모든 것" | 링크/AER은 최소 `-vv` + root |
| `-k` | 커널 드라이버 + 후보 모듈 (`-v` 시 자동 포함) | probe 됐나? |
| `-n` / `-nn` | 숫자 ID만 / 이름+숫자 `[1b36:0010]` | 드라이버 id_table 매칭 확인 |
| `-x` | 표준 헤더 64 B hex | 빠른 덤프 |
| `-xxx` | 256 B 전체 (root만) | capability 원본 |
| `-xxxx` | **4096 B 확장 config** (AER, ACS, L1SS, LMR…) | 버그 리포트에 첨부 (`lspci -vvxxxx`) |
| `-b` | 커널이 아니라 **카드가 보는** 주소/IRQ (bus-centric) | iATU/주소 변환이 있는 SoC에서 CPU 주소 vs PCI 주소 비교 |
| `-D` | 항상 domain 표시 `0000:01:00.0` | 멀티 호스트 브리지 SoC, 스크립트 |
| `-P` / `-PP` | 브리지 경로로 장치 표시 `00:03.0/03:00.0/04:00.0/05:00.0` | 버스 번호가 재배정돼도 안 변하는 식별자 |
| `-s [[[dom]:]bus]:][dev][.[fn]]` | 장치 선택 (`-s 01:00.0`, `-s 0:` = bus 0 전부, `-s .4`) | 하나만 볼 때 |
| `-d [vendor]:[device][:class[:progif]]` | ID/클래스로 선택 (`-d 1b36:`, `-d ::0604` = 모든 PCI 브리지) | "모든 NVMe(`::0108`)", "모든 브리지" |
| `-m` / `-mm` / `-vmm` | **기계 판독용** 출력 (스크립트는 이것만 파싱하라고 man page가 권고) | 자동화 |
| `-M` | bus mapping 모드: 잘못 설정된 브리지 뒤까지 직접 스캔 (직접 HW 접근 모드에서만 의미) | 브리지 설정이 깨졌을 때 |
| `-q` / `-qq` / `-Q` | 모르는 ID를 DNS로 중앙 DB 조회 | 낯선 장치 |
| `-i <file>` / `-p <file>` | pci.ids / modules.pcimap 경로 지정 | 오프라인 |
| `-A <method>` / `-O k=v` / `-H1` `-H2` | access method 지정 (`-A ecam`, `-A dump`) | §2.1 |
| **`-F <file>`** | **`lspci -x…` 덤프 파일을 하드웨어 대신 읽는다** | 현장 덤프를 내 PC에서 디코드 (§3.13 검증) |
| `-G` | 라이브러리 디버그 레벨↑ | lspci 자체 문제 |

### 2.3 `-vvv` 필드 ↔ 레지스터 오프셋 대응표

- PCIe capability 내부 오프셋은 `include/uapi/linux/pci_regs.h`로 확인 [확인됨] [3]. lspci의 `Capabilities: [54] Express` 에서 `[54]`가 이 capability의 시작 오프셋 → `setpci`로는 `CAP_EXP+<off>`.

| lspci 줄 | 레지스터 | 위치 | 디버그 포인트 |
|---|---|---|---|
| `Control: I/O+ Mem+ BusMaster+ … DisINTx+` | Command | 0x04 (bit0 IO, bit1 Mem, bit2 Bus Master, bit10 INTx Disable) | `Mem-`면 BAR 접근 불가, `BusMaster-`면 DMA·MSI 불가 |
| `Status: Cap+ … <MAbort- >SERR-` | Status | 0x06 (bit4 Capabilities List) | `<MAbort+` = 이 장치가 보낸 요청이 응답 없이 끝남 |
| `Region N: Memory at … (64-bit, non-prefetchable) [size=16K]` | BAR0–5 | 0x10–0x24 | **size는 config에 없다** — 커널이 sizing 해서 sysfs `resource`로 준 값 (§3.13: `-F` 모드에선 size가 사라짐) |
| `Bus: primary=00, secondary=01, subordinate=01` | Type 1 헤더 | 0x18/0x19/0x1A | 브리지 아래 버스 범위 |
| `Memory behind bridge:` / `Prefetchable memory behind bridge:` | 브리지 윈도우 | 0x20–0x2C | BAR가 "no space"일 때 윈도우 크기 확인 |
| `BridgeCtl: … >Reset-` | Bridge Control | 0x3E bit6 = **Secondary Bus Reset** | hot reset 트리거 비트 |
| `Express (v2) Root Port (Slot+)` | PCIe Capabilities | cap+0x02 (bits 7:4 = port type) | 0=Endpoint, 4=Root Port, 5=Upstream, 6=Downstream |
| `DevCap` / `DevCtl` / `DevSta` | Device Cap/Ctl/Sta | cap+0x04 / +0x08 / +0x0A | MaxPayload, MaxReadReq, `CorrErr+` 등 에러 감지 플래그 |
| `LnkCap` | Link Capabilities | cap+0x0C | [3:0] Max Speed, [9:4] Max Width, bit20 `LLActRep` |
| `LnkCtl` | Link Control | cap+0x10 | bit4 Link Disable, bit5 Retrain, [1:0] ASPM |
| **`LnkSta`** | **Link Status** | **cap+0x12** | [3:0] 속도, [9:4] 폭, bit11 `Train`, bit12 `SlotClk`, **bit13 `DLActive`**, bit14 `BWMgmt`, bit15 `ABWMgmt` |
| `SltCap` / `SltCtl` / `SltSta` | Slot Cap/Ctl/Sta | cap+0x14 / +0x18 / +0x1A | `PresDet+` = 카드 감지(bit6), `LinkState` 변화(bit8 DLLSC) |
| `RootCtl` / `RootCap` / `RootSta` | Root | cap+0x1C / +0x1E / +0x20 | `CRSVisible` (CRS/RRS SW visibility) |
| `DevCap2` / `DevCtl2` | Device 2 | cap+0x24 / +0x28 | Completion Timeout 범위, 10-bit tag, LTR |
| `LnkCap2` | Link Capabilities 2 | cap+0x2C | Supported Speeds 벡터 (bit1 2.5 … bit6 64 GT/s) |
| `LnkCtl2` | Link Control 2 | cap+0x30 | [3:0] **Target Link Speed**, bit4 Enter Compliance, bit5 HW Autonomous Speed Disable |
| `LnkSta2` | Link Status 2 | cap+0x32 | Equalization Complete / Phase1-3 (bits 1–4), bit10 Flit Mode |
| `Capabilities: [100 v2] Advanced Error Reporting` | AER ext cap | ecap+0x04 UESta, +0x08 UEMsk, +0x0C UESvrt, +0x10 CESta, +0x14 CEMsk, +0x18 AERCap, +0x1C HeaderLog, +0x2C RootCmd, +0x30 RootSta, +0x34 ErrorSrc | §3.10 |

### 2.4 lspci의 `(downgraded)` / `(overdriven)` 표시 규칙 — 함정 ★

- pciutils `ls-caps.c`의 `link_compare()` [확인됨 GitHub 소스] [2] 요지:
  - LnkSta > LnkCap → `(overdriven)`
  - 같으면 → 아무것도 안 붙임 (현재 버전엔 `(ok)` 문자열이 없다. N01 §2.1 예시의 `(ok)`는 구버전 표기 [추정])
  - 작은데 **포트 타입이 Root Port / Downstream Port / PCIe-to-PCI bridge면 → 아무것도 안 붙임**
  - 그 외(Endpoint, Upstream Port)면 → `(downgraded)`
- 이유: downstream port의 LnkCap은 "내가 할 수 있는 최대"일 뿐, 상대가 느리면 낮게 협상되는 게 정상. **"downgraded"는 아래쪽 장치 기준으로만 의미가 있다**.
- 실제 랩 증거 (§3.3): root port `00:01.0`은 `LnkCap 16GT/s x4`, `LnkSta 2.5GT/s x1`인데 아무 표시 없음 (밑의 QEMU NVMe가 2.5 GT/s x1만 지원). 반대로 XIO3130 downstream port는 LnkCap이 `Speed unknown, Width x0`(에뮬레이션 버그)이라 `(overdriven)`이 찍힘.
- 그래서 자동화 스크립트도 **"링크 = (위 포트, 아래 장치)" 쌍으로 판단**해야 한다. `pcie_snapshot.py` 1차 버전은 root port를 혼자 판단해서 QEMU에서 **오탐 5건**을 냈고, 쌍 단위로 고친 뒤 0건이 됐다 (§6에 그대로 기록).

### 2.5 setpci 문법 총정리

- 형식: `setpci [opts] -s <BDF> | -d <id> <reg>[.<w>][=<val>[:<mask>]] ...` — **모든 숫자는 16진수** (man page).
- 레지스터 지정 방법 (man setpci(8) 3.14 [확인됨]) [1]:

| 방법 | 예 | 의미 |
|---|---|---|
| 숫자 오프셋 | `4.w` | 0x04에서 16-bit (= COMMAND) |
| 이름 | `COMMAND`, `VENDOR_ID`, `SECONDARY_BUS`, `BRIDGE_CONTROL` | 폭은 이름에 정해져 있어 생략 가능. 전체 목록 `setpci --dumpregs` |
| capability 이름 + 오프셋 | `CAP_EXP+12.w` | PCI Express cap 시작 + 0x12 = **LnkSta** |
| 확장 cap 이름 | `ECAP_AER+10.l` | AER + 0x10 = Correctable Error Status |
| 번호로 cap 지정 | `CAP10`, `ECAP27` | 이름 모를 때 ID로 (0x27 = Lane Margining at Receiver = `ECAP_LMR`) |
| 같은 cap 여러 개 | `ECAP_VNDR@1` | 두 번째 인스턴스 (0부터) |
| 폭 | `.b` `.w` `.l` (대소문자 무관) | 1/2/4 바이트. 리틀 엔디언이라 `VENDOR_ID+1.b` = 상위 바이트 |

- `setpci --dumpregs` 실제 목록 일부 [확인됨 VM]: `CAP_PM CAP_MSI CAP_EXP CAP_MSIX …`, `ECAP_AER ECAP_ACS ECAP_SRIOV ECAP_LTR ECAP_SECPCI ECAP_DPC ECAP_L1PM ECAP_PTM ECAP_DVSEC ECAP_DLNK ECAP_16GT ECAP_LMR ECAP_32GT ECAP_IDE ECAP_64GT`.
- 쓰기: `reg=value` (전체 덮어쓰기) 또는 **`reg=value:mask`** (mask가 1인 비트만 바꾸는 read-modify-write).
  - `setpci -s 00:01.0 CAP_EXP+10.w=20:20` → LnkCtl의 bit5(Retrain Link)만 1로.
  - `setpci -s 00:01.0 CAP_EXP+30.w=1:f` → LnkCtl2 Target Link Speed를 Gen1(1)로, 나머지 비트 유지.
  - `setpci -s 00:01.0 BRIDGE_CONTROL=40:40` → Secondary Bus Reset assert (반드시 다시 `=0:40`으로 해제하고 2 ms 이상 유지 + 이후 대기 규칙 지켜야 함 — 직접 하지 말고 sysfs `reset_subordinate`/`reset` 권장).
- 옵션: `-v` (실제 접근 출력), **`-D` (demo: 실제로 안 쓰고 무엇을 쓸지만 출력)**, `-f` (장치 없어도 조용히), `-r` (버스 스캔 생략, `-s`가 완전한 주소일 때).
- 실제 출력 [확인됨 QEMU 게스트]: `setpci -vD -s 00:01.0 CAP_EXP+10.w=20:20` →
  `0000:00:01.0 (cap 10 @54) @64 0000->(0020:0020)->0020` — "cap 0x10이 0x54에 있고, 0x54+0x10=0x64에 0000을 읽어 mask 0020으로 0020을 넣으면 결과 0020" 이라는 뜻.
- ⚠ 쓰기는 **커널 드라이버 몰래** 하드웨어를 바꾸는 것. ASPM/MPS/링크 속도를 바꾸면 커널의 캐시된 상태와 어긋난다 → 검증 랩에서만, 바꾼 값은 로그에 남기고 원복.

### 2.6 sysfs 속성 카탈로그 (`/sys/bus/pci/devices/<DDDD:BB:DD.F>/`)

- 근거: `Documentation/ABI/testing/sysfs-bus-pci`, `sysfs-bus-pci-devices-aer`, `Documentation/PCI/sysfs-pci.rst`, 그리고 ABI 문서에 없는 것은 `drivers/pci/pci-sysfs.c` [확인됨 6.18.55 소스] [4][5][6][7]. 실제 값은 §3.7.

| 속성 | 읽기/쓰기 | 내용 · 용도 | 근거 |
|---|---|---|---|
| `vendor` `device` `subsystem_vendor` `subsystem_device` `class` `revision` | ro | ID들 (`0x1b36`, `0x010802`=NVMe) | sysfs-pci.rst / ABI(revision) |
| `config` | rw, binary | **config space 원본**. non-root는 64 B만 | sysfs-pci.rst, pci-sysfs.c |
| `resource` | ro | BAR별 `start end flags` 한 줄씩 (CPU 물리 주소) | sysfs-pci.rst |
| `resourceN` | rw, **mmap** | BAR N을 사용자 공간에 mmap (I/O BAR는 read/write) | sysfs-pci.rst |
| `resourceN_wc` | mmap | prefetchable BAR의 **write-combining** 매핑 | sysfs-pci.rst |
| `resourceN_resize` | rw | Resizable BAR 크기 조정 | ABI |
| `rom` | rw | 옵션 ROM 읽기 (먼저 `1` 써서 enable) | sysfs-pci.rst |
| `irq` | ro | legacy IRQ 번호 (MSI 쓰면 의미 약함) | ABI |
| `msi_irqs/` | dir | 할당된 MSI/MSI-X 벡터마다 파일 하나 (`/proc/interrupts` 번호와 같음) | ABI |
| `msi_bus` | rw | 이 장치/버스 아래 MSI 허용 여부 | ABI |
| `enable` | rw | enable 카운터 (`echo 1`이면 +1) — 드라이버 없이 장치 enable | sysfs-pci.rst |
| `remove` | wo | `echo 1` → 커널 목록에서 제거 (전원 끄기 아님) | ABI |
| `rescan` (장치/버스별), `/sys/bus/pci/rescan` | wo | 재열거 | ABI |
| `reset` | wo | `echo 1` → **function reset** (`reset_method` 순서대로 시도) | ABI |
| `reset_method` | rw | 지원/활성 리셋 방법과 순서. 커널 목록: `device_specific acpi flr af_flr pm bus cxl_bus` | ABI + pci.c `pci_reset_fn_methods[]` |
| `reset_subordinate` | wo | **브리지 전용**: 아래 버스 전체 리셋 (2024-10 추가) | ABI |
| `driver` | symlink | 바인딩된 드라이버 | 드라이버 모델 |
| `driver_override` | rw | 이 장치에 붙을 드라이버 강제 (`vfio-pci`로 넘길 때) | ABI |
| `/sys/bus/pci/drivers/<drv>/bind` `unbind` `new_id` `remove_id` | wo | 드라이버 수동 연결/해제, ID 추가 | ABI |
| `current_link_speed` `current_link_width` | ro | **LnkSta** 디코드 (`2.5 GT/s PCIe`, `1`) | **ABI 문서엔 없음** → pci-sysfs.c `pcie_dev_attrs` (PCIe 장치에만 보임) |
| `max_link_speed` `max_link_width` | ro | **LnkCap**(속도는 LnkCap2 우선) 디코드 | pci-sysfs.c |
| `secondary_bus_number` `subordinate_bus_number` | ro | 브리지 버스 범위 | pci-sysfs.c (`pci_bridge_attrs`) |
| `numa_node` | rw | NUMA 노드 (-1 = 없음) | ABI |
| `local_cpus` `local_cpulist` | ro | 이 장치에 가까운 CPU 마스크/목록 | sysfs-pci.rst |
| `dma_mask_bits` `consistent_dma_mask_bits` | ro | 드라이버가 설정한 DMA 주소 폭 | pci-sysfs.c |
| `power_state` | ro | `D0` `D3hot` `D3cold` `unknown` `error` | ABI |
| `d3cold_allowed` | rw | D3cold 허용 | ABI |
| `power/` | dir | runtime PM: `control`(on/auto), `runtime_status`, `runtime_suspended_time`, wakeup 통계 | power ABI |
| `link/` | dir | ASPM 개별 제어 `l0s_aspm l1_aspm l1_1_aspm l1_2_aspm l1_1_pcipm l1_2_pcipm clkpm` (지원되는 것만 보임) | ABI |
| `aer_dev_correctable` `aer_dev_nonfatal` `aer_dev_fatal` | ro | **AER 에러 종류별 카운터** + `TOTAL_ERR_*` | sysfs-bus-pci-devices-aer |
| `aer_rootport_total_err_cor/_fatal/_nonfatal` | ro | root port(RCEC)에 **보고된** 메시지 수 (계층 전체 누적) | 같은 문서 |
| `aer/` | dir | AER 로그 ratelimit 설정 (6.16+) | 같은 문서 |
| `sriov_numvfs` `sriov_totalvfs` `virtfnN` `physfn` | rw/ro | SR-IOV | ABI |
| `modalias` | ro | `pci:v00001B36d00000010…` — 모듈 자동 로드 키 | ABI |
| `serial_number` | ro (admin) | Device Serial Number ext cap | ABI |
| `iommu_group` | symlink | IOMMU 그룹 (VFIO 패스스루 단위) | iommu ABI |

- 버스 쪽: `/sys/class/pci_bus/<dom:bus>/` (`cpuaffinity`, `rescan`, `legacy_io` 등), 슬롯: `/sys/bus/pci/slots/<n>/` (hotplug 슬롯이면 `power`, `address`).

### 2.7 AER 카운터 해석 규칙 (sysfs-bus-pci-devices-aer 요지)

- 근거 [6][8].

- 카운터는 "그 장치가 **보고한**(seen/reported) 에러"다. endpoint가 문제를 일으켜도 **상대(root port)가 받는 쪽에서 감지**하면 root port 카운터가 오른다 → 범인 ≠ 카운터가 오른 장치.
- 여러 에러가 ERR_COR 메시지 하나로 합쳐질 수 있어 `TOTAL_ERR_COR`가 항목 합과 다를 수 있다 — §3.10에서 RxErr 1 + BadTLP 1인데 TOTAL 1 (dmesg "Multiple Correctable error message") 실제 확인.
- 항목 이름이 커널 버전에 따라 다르다: ABI 문서 예시는 `Receiver Error`, 6.18.55 실제 출력은 `RxErr` [확인됨 QEMU] → 파서는 "이름 값" 일반 형식으로 짜야 한다.
- Root port의 `aer_rootport_total_err_*`는 그 아래 **계층 전체** 누적.

### 2.8 커널 로그(dmesg) 패턴 — 문자열은 커널 소스로 확인

- 근거: 실제 QEMU 로그 + 커널 소스 문자열 [10].

| 메시지 (형식) | 언제 / 의미 | 출처 |
|---|---|---|
| `pci-host-generic …: ECAM at [mem …] for [bus 00-ff]` | 호스트 브리지가 ECAM 창을 잡음 | QEMU 실제 |
| `pci 0000:01:00.0: [1b36:0010] type 00 class 0x010802 PCIe Endpoint` | 열거: config read 성공 = 이 장치는 링크가 살아 있다 | 실제 |
| `pci 0000:01:00.0: BAR 0 [mem 0x10000000-0x10003fff 64bit]: assigned` | 리소스 할당 성공. 실패면 `can't assign; no space` 류 | 실제 |
| `pci 0000:00:01.0: bridge configuration invalid ([bus 00-00]), reconfiguring` | 펌웨어가 버스 번호를 안 줘서 커널이 다시 매김 (QEMU/DT 시스템에선 정상) | 실제 |
| `pcieport 0000:00:01.0: AER: enabled with IRQ 16` | AER 서비스 드라이버 붙음 | 실제 |
| `… PCIe Bus Error: severity=Correctable, type=Physical Layer, (Receiver ID)` + `error status/mask=…` + `[ 0] RxErr (First)` | AER 에러 디코드 | 실제 (주입) |
| `… X.XXX Gb/s available PCIe bandwidth, limited by 2.5 GT/s PCIe x1 link at … (capable of …)` | 드라이버가 `pcie_print_link_status()` 호출 시 **downtrain 경고** | pci.c 문자열 [확인됨] |
| `e1000e … eth0: (PCI Express:2.5GT/s:Width x1)` | 드라이버 자체 링크 출력 | 실제 |
| `dwc … PCIe Gen.%u x%u link up` / `Phy link never came up` | **DesignWare 컨트롤러** 링크 결과 (많은 ARM SoC) | pcie-designware.c [확인됨] |
| `nwl-pcie … PHY link never came up` | Xilinx ZynqMP NWL 컨트롤러 | pcie-xilinx-nwl.c [확인됨] |
| `… not ready NNNms after FLR/bus reset/resume; giving up` | 리셋 후 장치가 RRS(=CRS) 응답만 계속 | pci.c [확인됨] |
| `pci 0000:01:00.0: device released` / `scanning [bus 01-01] behind bridge` | remove / rescan (dyndbg로 보임) | 실제 |

- 추천 grep: `dmesg | grep -iE "pci|pcie|aer|link|bar|nvme"` + 시간 순서로 "열거 → 리소스 → 드라이버 probe → 에러" 를 읽는다.

### 2.9 `/proc` 3종

- **`/proc/iomem`**: CPU 물리 주소 지도. `pcie@… → PCI Bus 0000:01 → 0000:01:00.0 → nvme` 계층으로 **어느 BAR가 어느 브리지 윈도우 안에 들어갔는지** 보인다. **non-root면 주소가 전부 `00000000-00000000`** (Lima VM 실제 확인 — 커널이 주소 노출을 막음).
- **`/proc/interrupts`**: 벡터별 인터럽트 횟수. `ITS-PCI-MSIX-0000:01:00.0 0 Edge nvme0q0` 처럼 **MSI 컨트롤러(GICv3 ITS / GICv2m)**, BDF, 벡터 번호, 핸들러 이름이 한 줄에. "MSI가 안 온다" 디버그의 첫 화면.
- **`/proc/bus/pci/devices`**: 옛 형식 요약 (`BBDF vendordevice irq BAR…`). `/proc/bus/pci/BB/DD.F`는 linux-proc 백엔드용 config 파일 (이번 게스트는 BusyBox라 해당 디렉터리 미생성 [관찰]).

### 2.10 debugfs · tracing · dynamic debug

- **DesignWare(DWC) 컨트롤러 debugfs** (Synopsys IP — Rockchip, NXP i.MX, Qualcomm, Samsung 등 다수 SoC가 사용) [확인됨 `Documentation/ABI/testing/debugfs-dwc-pcie`, 2025-02 추가] [9]:
  - `/sys/kernel/debug/dwc_pcie_<dev>/ltssm_status` → **현재 LTSSM 상태를 문자열+raw 값으로**. "lspci로 L0를 볼 수 있나?"의 Linux 답: 표준 레지스터로는 못 보지만 **컨트롤러 드라이버가 vendor 레지스터를 debugfs로 열어 주면** 볼 수 있다.
  - `rasdes_debug/lane_detect` (lane별 receiver detect), `rx_valid` (PIPE RxValid)
  - `rasdes_event_counters/<event>/{counter_enable,counter_value,lane_select}` — Receiver Error, LCRC Error, Replay Timeout, Deskew Error, Framing Error 등 35종, **lane별** 카운터
  - `rasdes_err_inj/<error>` — TX LCRC, ECRC, DLLP CRC, SEQ# 에러 등 **HW 에러 주입** 19종 → AER 경로 검증에 사용
- **PTM debugfs** `/sys/kernel/debug/pcie_ptm_*` (Precision Time Measurement 클럭 값) — 시간 동기 검증용.
- **ftrace (function tracer)**: tracepoint가 없는 PCI core 함수도 추적 가능. 6.18.55 소스에 PCI core 전용 `TRACE_EVENT`는 없음 [확인됨 grep] → `set_ftrace_filter`에 `pci_scan_single_device pci_setup_device pci_bus_read_dev_vendor_id …` 넣고 `function` tracer. 실제 결과 §3.11. 더 깊게는 `function_graph`, `trace-cmd record -p function_graph -g pci_rescan_bus`, kprobe.
- **AER tracepoint**: `ras:aer_event` (`include/ras/ras_event.h`) — `trace-cmd record -e ras:aer_event` 로 에러를 타임스탬프와 함께 수집, rasdaemon이 이걸 DB에 저장.
- **dynamic debug**: `echo 'file drivers/pci/probe.c +p' > /sys/kernel/debug/dynamic_debug/control` → 평소 숨은 `scanning [bus 01-01] behind bridge, pass 0` 같은 메시지가 나온다 (실제 §3.11). 부팅 시엔 커널 cmdline `dyndbg="file drivers/pci/* +p"`.

### 2.11 사용자 공간에서 BAR 직접 접근

| 방법 | 어떻게 | 주의 |
|---|---|---|
| sysfs `resourceN` mmap | `open("/sys/.../resource0") + mmap` | 가장 깔끔, root. 실제로 NVMe CAP/VS 레지스터 읽음 (§3.12) |
| `devmem` / `busybox devmem` | `devmem 0x10000008 32` (물리 주소) | `CONFIG_STRICT_DEVMEM`이면 RAM은 막히고 MMIO는 (드라이버가 `IO_STRICT_DEVMEM`까지 안 켰으면) 허용 |
| `pcimem` | 오픈소스 작은 C 도구: `pcimem /sys/bus/pci/devices/…/resource0 0x8 w` | sysfs resource 파일을 mmap하는 devmem 같은 것 [추정: 설치 안 해 봄] |
| UIO (`uio_pci_generic`) | IRQ를 `/dev/uioN` read로 받음 | IOMMU 보호 없음 → 테스트 랩 전용 |
| **VFIO** (`vfio-pci`) [15] | `driver_override`에 `vfio-pci` → `/dev/vfio/<group>` → BAR mmap, IRQ eventfd, **IOMMU로 보호된 DMA** | user-space 테스트 드라이버의 정석. IOMMU group 단위 |
| ⚠ 드라이버가 붙은 장치에 쓰기 | | 드라이버 상태와 충돌. 읽기만 하거나 unbind 후 |

### 2.12 장치별 도구 — NVMe (Don의 SSD 경력과 직결)

- **nvme-cli** (NVMe 레지스터 맵은 [13]): `nvme list` · `nvme id-ctrl /dev/nvme0` (모델, FW, MDTS…) · `nvme smart-log` (온도, media error, **unsafe shutdowns**) · `nvme error-log` · `nvme fw-log` · `nvme reset /dev/nvme0` (controller reset) · `nvme subsystem-reset` (NVM subsystem reset — PCIe 링크도 내려감). 이번 랩 게스트엔 미설치 [미실행].
- **fio**: PCIe 링크 대역폭 검증의 실전 도구. 예) `fio --name=seqrd --filename=/dev/nvme0n1 --rw=read --bs=128k --iodepth=64 --ioengine=io_uring --direct=1 --runtime=30` → 결과 GB/s를 링크 이론치(예: Gen4 x4 ≈ 7.88 GB/s 이론 데이터율, 128b/130b 후)와 비교. Lima VM에 fio 있음 [확인됨 `which fio`], 측정은 [미실행].
- 링크가 Gen3로 떨어지면 SSD 순차 읽기가 "딱 절반"이 되는 식으로 **성능 숫자가 링크 문제의 증상**이 된다 → [성능 메트릭 노트](2026-10-30_perf_metrics_ssd_vs_soc.html)의 "% of theoretical peak" 사고방식.

### 2.13 하드웨어 도구 — SW가 못 보는 곳

| 도구 | 보는 것 | 언제 |
|---|---|---|
| **프로토콜 애널라이저** (+ interposer / slot interposer / mid-bus probe) | TS1/TS2 ordered set, LTSSM 전이, **equalization preset 요청/응답**, TLP/DLLP, ACK/NAK, replay | 링크가 Polling↔Detect 반복, Gen4 진입 실패, 간헐 NAK |
| **Exerciser** | 규격 위반/에러 TLP를 일부러 생성, 원하는 LTSSM 상태로 몰기 | 에러 처리·AER 경로 검증, 상대 장치 호환성 |
| **오실로스코프** (고속 + 로직) | **PERST# 타이밍**(전원 안정 후 ≥100 ms에 deassert, CEM), **REFCLK** 주파수/지터, 전원 레일 droop | "lspci에 안 보임" 1단계 |
| **BERT / 고속 스코프 + 컴플라이언스 픽스처** (CBB/CLB) | TX eye, jitter, RX 스트레스 | 전기적 컴플라이언스, margin |
| Lane Margining at Receiver (SW로 하는 eye 측정) | `pcilmr` (pciutils 포함) — Gen4+ 수신기의 시간/전압 margin | HW 장비 없이 margin 추세 (LP14 §2.6) |

### 2.14 macOS 대응표 — 스크린에서 말한 "Mac 경험"을 다리로

- 실제 이 Mac(M2)에서 뽑은 값 [확인됨 2026-10-31]:
  - `system_profiler SPPCIDataType` → `Vendor ID: 0x8086, Device ID: 0x15f0, Link Width: x4, Link Speed: 2.5 GT/s, Link Status: Link up, MSI: Yes, Slot: Thunderbolt@3,0,0`
  - `ioreg -r -c IOPCIDevice -l` → root port `pci-bridge0@0`: `"IOPCIExpressLinkStatus" = 12307`, `"pcidebug" = "0:0:0(2:2)"`, `"IOPCIExpressCapabilities" = 66`; endpoint `centauri-control@0`: `"IOPCIExpressLinkStatus" = 4115`
- 해석: `12307 = 0x3013` → LnkSta 그대로! speed 3(8 GT/s), width 1, bit12 SlotClk, **bit13 DLActive=1** (root port). `4115 = 0x1013` → 8 GT/s x1, SlotClk, DLActive 0 (endpoint라 정상). `IOPCIExpressCapabilities` 66=0x42 → PCIe cap reg: version 2, **type 4 = Root Port**; endpoint 쪽 2 = type 0. 즉 **ioreg는 LnkSta/PCIe Capabilities 레지스터를 10진수로 보여 줄 뿐**이다.

| 하고 싶은 것 | Linux | macOS |
|---|---|---|
| 장치 목록 | `lspci` | `system_profiler SPPCIDataType`, `ioreg -c IOPCIDevice` |
| 트리 | `lspci -tv` | `ioreg -r -c IOPCIDevice` 들여쓰기 (`pcidebug "bus:dev:fn(sec:sub)"` [추정 의미]) |
| 링크 속도/폭 | `lspci -vvv` LnkSta, sysfs `current_link_*` | `Link Speed/Width` (system_profiler), `IOPCIExpressLinkStatus` (ioreg, raw LnkSta) |
| 최대 능력 | LnkCap, `max_link_*` | `IOPCIExpressLinkCapabilities(2)` |
| AER | `lspci -vvv` AER 블록, `aer_dev_*` | `IOPCIAERCapabilities` 속성 존재 [관찰]; 카운터 노출은 [추정: 없음] |
| 드라이버 | `lspci -k` | ioreg 트리에서 IOPCIDevice 아래 붙은 서비스 클래스 |

- 영어 다리: "On macOS I used system_profiler and ioreg — ioreg's IOPCIExpressLinkStatus is literally the Link Status register in decimal, so the same reasoning carries over: 0x3013 on the root port means 8 GT/s, x1, DLActive set."

### 2.15 Ethernet / MIPI 비교 (짧게)

| 질문 | PCIe (Linux) | Ethernet (Linux) | MIPI CSI-2 (Linux) |
|---|---|---|---|
| 링크 올라왔나 | 위 포트 `LnkSta DLActive+` | `ethtool eth0` → `Link detected: yes`, `ip link` `LOWER_UP` | 수신기 드라이버 상태, D-PHY LP→HS 전환, V4L2 `media-ctl -p` |
| 협상 속도/폭 | LnkSta Speed/Width vs LnkCap | `ethtool` Speed/Duplex, auto-negotiation 결과 | lane 수·data rate는 DT 설정 (자동 협상 없음) |
| 에러 카운터 | AER `aer_dev_correctable` (RxErr, BadTLP…) | `ethtool -S` (`rx_crc_errors`, FCS), `ip -s link` | CSI-2 수신기 ECC/CRC 에러 카운터 (드라이버별 debugfs/sysfs) |
| 레지스터 직접 | `setpci`, config space | `ethtool -d` (MAC 레지스터 덤프), `phytool`/MDIO | `devmem`로 수신기 레지스터 |

## 3. Linux에서 볼 것 — 실제 출력과 줄별 해석

> 모든 출력은 **2026-10-31 실제 실행** 결과. 환경 ① Lima VM `elinux` (Ubuntu, kernel 7.0.0-38, Apple VZ, pciutils 3.14) ② VM 안 QEMU 10.2.1 `-machine virt,gic-version=3` + lab kernel `6.18.55-trace` + BusyBox initramfs + VM에서 복사한 lspci/setpci/python3. 원본 로그: [qemu_explore_log.txt](onsitePrep/lp13/outputs/qemu_explore_log.txt), [qemu_aer_log.txt](onsitePrep/lp13/outputs/qemu_aer_log.txt), [qemu_lspci_vvv.txt](onsitePrep/lp13/outputs/qemu_lspci_vvv.txt), [lima_lspci_vvv.txt](onsitePrep/lp13/outputs/lima_lspci_vvv.txt).

### 3.1 Lima VM — `sudo lspci -vvv` (실제) 와 root/non-root 차이

```text
$ lspci
00:00.0 Host bridge: Apple Inc. Device 1a05
00:01.0 Ethernet controller: Red Hat, Inc. Virtio 1.0 network device (rev 01)
00:05.0 Communication controller: Red Hat, Inc. Virtio 1.0 console (rev 01)
00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)
...
00:0c.0 USB controller: Apple Inc. Device 1a06

$ sudo lspci -vvv -s 00:01.0
00:01.0 Ethernet controller: Red Hat, Inc. Virtio 1.0 network device (rev 01)
	Subsystem: Red Hat, Inc. Device 0041
	Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV+ VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
	Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
	Latency: 64
	Interrupt: pin A routed to IRQ 14
	Region 0: Memory at 380000000 (64-bit, non-prefetchable) [size=64K]
	Region 2: Memory at 50001500 (32-bit, non-prefetchable) [size=64]
	Capabilities: [40] Vendor Specific Information: VirtIO: CommonCfg
		BAR=0 offset=00000000 size=00000038
	Capabilities: [50] Vendor Specific Information: VirtIO: ISR
		BAR=0 offset=00001000 size=00000001
	Capabilities: [60] Vendor Specific Information: VirtIO: Notify
		BAR=0 offset=00004000 size=00000008 multiplier=00000004
	Capabilities: [74] Vendor Specific Information: VirtIO: DeviceCfg
		BAR=0 offset=00008000 size=00000011
	Capabilities: [84] MSI-X: Enable+ Count=3 Masked-
		Vector table: BAR=2 offset=00000000
		PBA: BAR=2 offset=00000030
	Kernel driver in use: virtio-pci
	Kernel modules: virtio_pci

$ lspci -vvv -s 00:01.0        # non-root
	...
	Region 2: Memory at 50001500 (32-bit, non-prefetchable) [size=64]
	Capabilities: <access denied>
```

| 줄 | 해석 |
|---|---|
| `00:01.0 Ethernet controller: … Virtio 1.0 network device (rev 01)` | BDF 00:01.0, class 0x0200, vendor 0x1af4(Red Hat), device 0x1041. Apple VZ 가상 장치 |
| `Control: Mem+ BusMaster+ … DisINTx+` | 메모리 디코드 on, DMA(bus master) on, **INTx 꺼짐 → MSI-X 사용 중**이라는 뜻 |
| `Status: Cap+` | capability 리스트 있음 (0x34 포인터 유효) |
| `Interrupt: pin A routed to IRQ 14` | legacy INTx 배선 정보 (MSI-X를 쓰므로 실제로는 미사용) |
| `Region 0 … (64-bit, non-prefetchable) [size=64K]` | BAR0 = 64-bit BAR(BAR0+BAR1 두 칸 사용), 0x3_8000_0000에 매핑 |
| `Capabilities: [40] Vendor Specific … VirtIO: CommonCfg` | ID 0x09 vendor cap — virtio가 "레지스터가 BAR 몇 번 어디에 있나"를 알려 주는 방식 |
| `Capabilities: [84] MSI-X: Enable+ Count=3` | MSI-X 벡터 3개, 테이블은 BAR2 offset 0, PBA는 0x30 |
| **PCI Express capability가 없다** | Apple VZ의 virtio 장치는 **conventional PCI** 로 보인다 → `LnkCap/LnkSta` 없음, sysfs `current_link_speed` 파일도 없음 (`pcie_dev_attrs_are_visible()`가 PCIe일 때만 노출) [확인됨 VM] |
| non-root `Capabilities: <access denied>` | sysfs `config`가 64 B로 잘려 0x40 이후 capability를 못 읽음 |

- 같은 VM에서 non-root `cat /proc/iomem` → `00000000-00000000 : PCI ECAM`, `00000000-00000000 : PCI Bus 0000:00` 처럼 **주소가 모두 0** — root로 보면 `40000000-4fffffff : PCI ECAM`, `380000000-77fffffff : PCI Bus 0000:00` [확인됨].
- 교훈: **"lspci에 링크 정보가 안 나온다" 원인 3가지** — (1) root 아님, (2) 장치가 conventional PCI(가상화 포함), (3) 링크 정보는 아래 장치가 아니라 위 포트에 있음.

### 3.2 QEMU 랩 토폴로지 — `lspci -tv`, `-PP`, `-d`

```text
$ lspci -tv
-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge
           +-01.0-[01]----00.0  Red Hat, Inc. QEMU NVM Express Controller
           +-02.0-[02]----00.0  Intel Corporation 82574L Gigabit Network Connection
           +-03.0-[03-05]----00.0-[04-05]----00.0-[05]----00.0  Device 1234:11e8
           \-04.0-[06]--

$ lspci -PP
00:03.0/03:00.0/04:00.0/05:00.0 Unclassified device [00ff]: Device 1234:11e8 (rev 10)

$ lspci -d ::0604            # 모든 PCI-to-PCI 브리지 (root port, switch port)
00:01.0 PCI bridge: Red Hat, Inc. QEMU PCIe Root port
...
03:00.0 PCI bridge: Texas Instruments XIO3130 PCI Express Switch (Upstream) (rev 02)
04:00.0 PCI bridge: Texas Instruments XIO3130 PCI Express Switch (Downstream) (rev 01)
```

| 부분 | 의미 |
|---|---|
| `-[0000:00]-` | domain 0, root bus 0 |
| `+-01.0-[01]----00.0 NVMe` | root port 00:01.0, 그 아래 버스 01 (secondary=subordinate=01), 거기 장치 00.0 → BDF **01:00.0** |
| `+-03.0-[03-05]----00.0-[04-05]----00.0-[05]----00.0` | root port 00:03.0 아래 버스 03–05: 03:00.0 = 스위치 **upstream port**, 그 아래 04:00.0 = **downstream port**, 그 아래 05:00.0 = edu 장치. 스위치 = "브리지 2단" |
| `\-04.0-[06]--` | **빈 root port** — 버스 06을 받았지만 장치 없음 (링크 미형성) |
| `-PP` 경로 | 버스 번호 대신 "포트 경로"로 식별 → 다른 장치가 hotplug 돼서 버스 번호가 밀려도 같은 이름 |

### 3.3 Root port `00:01.0` — `lspci -vvv` 줄별 해석 (실제, 핵심 ★)

```text
00:01.0 PCI bridge: Red Hat, Inc. QEMU PCIe Root port (prog-if 00 [Normal decode])
	Control: I/O+ Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
	Region 0: Memory at 10a00000 (32-bit, non-prefetchable) [size=4K]
	Bus: primary=00, secondary=01, subordinate=01, sec-latency=0
	Memory behind bridge: 10000000-101fffff [size=2M] [32-bit]
	Prefetchable memory behind bridge: 8000000000-80001fffff [size=2M] [32-bit]
	BridgeCtl: Parity- SERR+ NoISA- VGA- VGA16- MAbort- >Reset- FastB2B-
	Capabilities: [54] Express (v2) Root Port (Slot+), IntMsgNum 0
		DevCap:	MaxPayload 128 bytes, PhantFunc 0
		DevCtl:	CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+
			MaxPayload 128 bytes, MaxReadReq 128 bytes
		DevSta:	CorrErr- NonFatalErr- FatalErr- UnsupReq- AuxPwr- TransPend-
		LnkCap:	Port #0, Speed 16GT/s, Width x4, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep+ BwNot+ ASPMOptComp-
		LnkCtl:	ASPM Disabled; RCB 64 bytes, LnkDisable- CommClk-
			ExtSynch- ClockPM- AutWidDis- BWInt- AutBWInt- FltModeDis-
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-
		SltCap:	AttnBtn+ PwrCtrl+ MRL- AttnInd+ PwrInd+ HotPlug+ Surprise+
			Slot #1, PowerLimit 0W; Interlock+ NoCompl-
		SltSta:	Status: AttnBtn- PowerFlt- MRL- CmdCplt+ PresDet+ Interlock-
			Changed: MRL- PresDet- LinkState-
		RootCap: CRSVisible-
		RootCtl: ErrCorrectable- ErrNon-Fatal- ErrFatal- PMEIntEna- CRSVisible-
		LnkCap2: Supported Link Speeds: 2.5-16GT/s, Crosslink- Retimer- 2Retimers- DRS-
		LnkCtl2: Target Link Speed: 16GT/s, EnterCompliance- SpeedDis-
			 Transmit Margin: Normal Operating Range, EnterModifiedCompliance- ComplianceSOS-
		LnkSta2: Current De-emphasis Level: -6dB, EqualizationComplete- EqualizationPhase1-
			 EqualizationPhase2- EqualizationPhase3- LinkEqualizationRequest-
			 Retimer- 2Retimers- CrosslinkRes: unsupported, FltMode-
	Capabilities: [48] MSI-X: Enable+ Count=1 Masked-
	Capabilities: [100 v2] Advanced Error Reporting
		CESta:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr- CorrIntErr- HeaderOF-
		CEMsk:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr+ CorrIntErr+ HeaderOF+
		RootCmd: CERptEn+ NFERptEn+ FERptEn+
		ErrorSrc: ERR_COR: 0000 ERR_FATAL/NONFATAL: 0000
	Capabilities: [148 v1] Access Control Services
	Kernel driver in use: pcieport
```
(실제 출력에서 일부 줄 생략 — 전체는 [qemu_lspci_vvv.txt](onsitePrep/lp13/outputs/qemu_lspci_vvv.txt))

| 줄 | 해석 | 면접에서 말할 포인트 |
|---|---|---|
| `PCI bridge … Root Port` / `Bus: primary=00, secondary=01, subordinate=01` | Type 1 헤더(브리지). 아래로 버스 01 하나 | "The root port is a bridge in software" |
| `Memory behind bridge: 10000000-101fffff [size=2M]` | 아래 장치들 BAR가 들어갈 **창(window)**. NVMe BAR0 0x10000000이 이 안 | BAR "no space" 문제는 이 창부터 |
| `>Reset-` | BridgeCtl Secondary Bus Reset 비트 = 0 | hot reset은 이 비트 |
| `Express (v2) Root Port (Slot+)` | PCIe cap at 0x54, 타입 Root Port, 슬롯 구현됨 | |
| `DevCtl: CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+` | 에러 **보고 enable** 비트 (AER 드라이버가 켬) | |
| `DevSta: CorrErr- …` | 에러 **감지** 플래그 (W1C) — 지금 깨끗 | |
| `LnkCap: Speed 16GT/s, Width x4 … LLActRep+ BwNot+` | 이 포트의 최대 = Gen4 x4 (QEMU `x-speed=16,x-width=4`로 지정한 값). **LLActRep+ = DLActive 비트를 믿을 수 있다** | DLActive를 보기 전 LLActRep 확인 |
| `LnkSta: Speed 2.5GT/s, Width x1` (표시 없음) | 협상 결과 Gen1 x1 — 아래 NVMe의 LnkCap이 2.5 GT/s x1이라 정상. root port라서 lspci가 `(downgraded)`를 안 붙임 (§2.4) | "Compare with the partner's LnkCap" |
| `TrErr- Train- … DLActive+` | **학습 끝, 데이터 링크 활성 = L0 도달의 간접 증거** | Q3 "can lspci confirm L0?" 답 |
| `SltSta: PresDet+` / `Changed: LinkState-` | 카드 존재 감지, 링크 상태 변화 이벤트 없음 | hotplug/surprise down 단서 |
| `LnkCap2: Supported Link Speeds: 2.5-16GT/s` | 속도 벡터 | |
| `LnkCtl2: Target Link Speed: 16GT/s` | SW가 정한 목표 속도 — Gen1 강제 실험 시 여기에 1 | |
| `LnkSta2: EqualizationComplete- Phase1-…` | Gen3+ equalization 기록. 지금 Gen1이라 전부 `-` (정상) | 실제 HW에서 Gen4 x4로 떴는데 EQ `-`면 이상 |
| `Advanced Error Reporting` 블록 | UESta/CESta = 현재 에러 상태 (W1C), *Msk = 마스크, RootCmd = root port가 에러 메시지를 인터럽트로 보고 | §3.10 주입 실험 |
| `Kernel driver in use: pcieport` | port service driver (AER, PME, hotplug, bwctrl 서비스를 붙임) | `/proc/interrupts`에 `PCIe PME, aerdrv, PCIe bwctrl` |

### 3.4 Endpoint `01:00.0` (QEMU NVMe) — `lspci -vvv` 해석

```text
01:00.0 Non-Volatile memory controller: Red Hat, Inc. QEMU NVM Express Controller (rev 02) (prog-if 02 [NVM Express])
	Control: I/O- Mem+ BusMaster+ ... DisINTx+
	Region 0: Memory at 10000000 (64-bit, non-prefetchable) [size=16K]
	Capabilities: [40] MSI-X: Enable+ Count=65 Masked-
		Vector table: BAR=0 offset=00002000
		PBA: BAR=0 offset=00003000
	Capabilities: [80] Express (v2) Endpoint, IntMsgNum 0
		DevCap:	MaxPayload 128 bytes, PhantFunc 0, Latency L0s <64ns, L1 <1us
			ExtTag+ AttnBtn- AttnInd- PwrInd- RBE+ FLReset+ SlotPowerLimit 0W TEE-IO-
		DevCtl:	CorrErr- NonFatalErr- FatalErr- UnsupReq-
			RlxdOrd- ExtTag- PhantFunc- AuxPwr- NoSnoop- FLReset-
			MaxPayload 128 bytes, MaxReadReq 128 bytes
		LnkCap:	Port #0, Speed 2.5GT/s, Width x1, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp-
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-
	Capabilities: [60] Power Management version 3
		Status: D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-
	Kernel driver in use: nvme
```

| 줄 | 해석 |
|---|---|
| `prog-if 02 [NVM Express]` | class 0x010802 (Mass storage / NVM / NVMe) |
| `Region 0 … [size=16K]` | NVMe 레지스터(CAP, VS, CC, CSTS, doorbells) BAR |
| `MSI-X: Enable+ Count=65` | 큐마다 벡터 (admin + I/O). 테이블이 **BAR0 내부** 0x2000 |
| `DevCap: … FLReset+` | **FLR 지원** → sysfs `reset_method`가 `flr bus` (§3.7) |
| `LnkCap: 2.5GT/s x1 … LLActRep-` | endpoint는 DLActive 보고 능력 없음(보통) → **`DLActive-`는 정상**. endpoint의 DLActive로 링크를 판단하면 안 된다 |
| `LnkSta: 2.5GT/s, Width x1` (표시 없음) | LnkCap과 같아서 아무 표시 없음. 낮았다면 여기에 `(downgraded)` |
| `DevCtl: CorrErr- …` | 이 QEMU NVMe는 **AER ext cap 자체가 없음** → sysfs `aer_dev_*` 파일도 없음 [확인됨] |

### 3.5 빈 포트와 스위치 — "L0 안 되면 lspci에 나오나?"의 실물

```text
00:04.0 PCI bridge: Red Hat, Inc. QEMU PCIe Root port        ← 장치를 안 꽂은 root port
	Bus: primary=00, secondary=06, subordinate=06
		LnkCap:	Port #0, Speed 16GT/s, Width x32, ...  LLActRep+ BwNot+
		LnkSta:	Speed 16GT/s, Width x32
			TrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-
		SltSta:	Status: AttnBtn- PowerFlt- MRL- CmdCplt+ PresDet- Interlock-

04:00.0 PCI bridge: Texas Instruments XIO3130 PCI Express Switch (Downstream) (rev 01)
		LnkCap:	Port #0, Speed unknown, Width x0, ...
		LnkSta:	Speed 2.5GT/s (overdriven), Width x1 (overdriven)
```

- **빈 root port는 lspci에 그대로 나온다** (root complex 내부라 링크와 무관) — 아래에 장치는 없음. 판단 근거는 `DLActive-` + `PresDet-`.
- ⚠ **링크가 없는데 LnkSta가 "16GT/s x32"** 로 보인다 — QEMU가 기본값을 넣어 둔 것. 실제 HW에서도 링크 다운 시 Speed/Width 필드는 **의미 없는 값**일 수 있다 → **DLActive가 `-`면 speed/width를 믿지 말 것**. sysfs도 같은 값을 그대로 보여 준다 (`current_link_speed=16.0 GT/s PCIe width=32` 실제 확인).
- XIO3130 downstream의 `(overdriven)`은 QEMU 구형 장치 모델이 LnkCap을 0으로 둔 탓 (실제 HW라면 이상 신호) [관찰].
- 런타임 링크 다운 시 이미 열거된 endpoint는 남아 있고 config read가 0xFFFF → `rev ff`, `-vvv`에서 `Unknown header type 7f` (N01 §4) — 이번 QEMU에서는 LinkDisable이 에뮬레이트되지 않아 재현 불가 (§3.11 참고).

### 3.6 setpci 실제 읽기와 수동 디코드

```text
VENDOR_ID=1b36 DEVICE_ID=0010 COMMAND=0406
CAP_EXP@01:00.0 flags=0002 LnkCap=00000411 LnkSta=0011
RP 00:01.0 LnkCap=00300444 LnkCtl=0000 LnkSta=2011 LnkCtl2=0004 LnkSta2=0000
RP 00:04.0 (empty) LnkSta=0204 SltSta=0010
ECAP_AER first dword @01:00.0: setpci: 0000:01:00.0: Instance #0 of Extended capability 0001 not found - there are no capabilities with that id.
ECAP_AER on RP 00:01.0: 14820001
bus numbers RP 00:03.0: PRIMARY=00 SECONDARY=03 SUBORDINATE=05
```

| 값 | 비트 디코드 |
|---|---|
| `COMMAND=0406` | bit1 Mem(0x2) + bit2 BusMaster(0x4) + bit10 INTx Disable(0x400) → lspci `Mem+ BusMaster+ DisINTx+`와 일치 |
| EP `flags=0002` | PCIe cap version 2, bits[7:4]=0 → **Endpoint** |
| EP `LnkCap=00000411` | [3:0]=1 → 2.5 GT/s, (0x411>>4)&0x3F = 1 → x1, bit10(0x400) = ASPM L0s 지원 |
| RP `LnkCap=00300444` | speed 4 → 16 GT/s, width (0x444>>4)&0x3F = 4 → x4, bit20(0x100000) **LLActRep**, bit21(0x200000) BwNot |
| RP `LnkSta=2011` | speed 1, width 1, **bit13(0x2000) DLActive=1** → 링크 업 |
| 빈 포트 `LnkSta=0204` | speed 4, width 0x20=32, DLActive=0 → "값은 쓰레기, 링크 없음" |
| 빈 포트 `SltSta=0010` | bit4 Command Completed만, **bit6 PresDet=0** |
| `ECAP_AER … 14820001` | ext cap header: ID 0x0001(AER), version 2, next = 0x148 (ACS) |
| EP에 `ECAP_AER` 없음 | setpci는 "not found" 에러 — 스크립트는 반환 코드 확인 필요 |

- **Retrain 실험** (QEMU): `setpci -s 00:01.0 CAP_EXP+10.w=20:20` → LnkSta 그대로 `2011`. `CAP_EXP+30.w=1:f`(Target Gen1) 후 다시 읽으면 LnkCtl2가 **`0004` 그대로** → QEMU root port는 LnkCtl2 쓰기를 무시하고 재학습도 에뮬레이트하지 않음 [관찰]. 실제 HW에서는 Target Link Speed를 낮추고 Retrain하면 LnkSta 속도가 바뀌고 `BWMgmt+`가 선다 — 이게 "Gen1에서는 뜨나?" bisect 실험 (LP03/LP15).

### 3.7 sysfs 실제 값 (`/sys/bus/pci/devices/0000:01:00.0/`)

```text
vendor                     0x1b36
device                     0x0010
class                      0x010802
irq                        15
numa_node                  -1
local_cpulist              0-1
enable                     1
power_state                D0
current_link_speed         2.5 GT/s PCIe
current_link_width         1
max_link_speed             2.5 GT/s PCIe
max_link_width             1
reset_method               flr bus
driver_override            (null)
dma_mask_bits              64
modalias                   pci:v00001B36d00000010sv00001AF4sd00001100bc01sc08i02
driver -> ../../../../../../bus/pci/drivers/nvme
msi_irqs: 30 31 32
resource:
0x0000000010000000 0x0000000010003fff 0x0000000000140204
config size: 4096
RP secondary_bus_number=1 subordinate_bus_number=1
RP aer_rootport_total_err_cor=0
EP link/: l0s_aspm
runtime_status=active control=on
```

- `reset_method = flr bus` → `echo 1 > reset`은 먼저 **FLR**(DevCap FLReset+), 안 되면 **bus**(부모 브리지 Secondary Bus Reset) 순.
- `msi_irqs: 30 31 32` ↔ `/proc/interrupts`의 30/31/32번 `nvme0q0 nvme0q1 nvme0q2`.
- `resource` flags `0x140204` = IORESOURCE_MEM(0x200) | MEM_64(0x100000) | SIZEALIGN 등 [추정: 비트 의미는 `include/linux/ioport.h`].
- `config size: 4096` (root로 읽음) — 같은 파일을 non-root로 읽으면 64.

### 3.8 dmesg — 부팅 열거 순서 (실제, 발췌)

```text
pci-host-generic 4010000000.pcie: host bridge /pcie@10000000 ranges:
pci-host-generic 4010000000.pcie:      MEM 0x0010000000..0x003efeffff -> 0x0010000000
pci-host-generic 4010000000.pcie: ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]
pci 0000:00:01.0: [1b36:000c] type 01 class 0x060400 PCIe Root Port
pci 0000:00:01.0: bridge configuration invalid ([bus 00-00]), reconfiguring
pci 0000:01:00.0: [1b36:0010] type 00 class 0x010802 PCIe Endpoint
pci 0000:01:00.0: BAR 0 [mem 0x00000000-0x00003fff 64bit]
pci 0000:03:00.0: [104c:8232] type 01 class 0x060400 PCIe Switch Upstream Port
pci 0000:05:00.0: [1234:11e8] type 00 class 0x00ff00 conventional PCI endpoint
pci 0000:00:01.0: bridge window [mem 0x10000000-0x101fffff]: assigned
pcieport 0000:00:01.0: AER: enabled with IRQ 16
e1000e 0000:02:00.0 eth0: (PCI Express:2.5GT/s:Width x1) 52:54:00:12:34:56
nvme nvme0: pci function 0000:01:00.0
nvme nvme0: 2/0/0 default/read/poll queues
```

- 읽는 순서: ① 호스트 브리지 `ranges`(CPU 주소 ↔ PCI 주소 창, DT에서 옴) + ECAM 위치 → ② `[vendor:device] type 00/01 class` = **config read 성공한 장치들** → ③ BAR 크기 발견(아직 주소 0) → ④ 브리지 창/BAR **assigned** → ⑤ port service(AER/PME) → ⑥ 드라이버 probe.
- `type 01` = 브리지 헤더, `type 00` = 일반 장치. `conventional PCI endpoint` = PCIe cap 없음(edu).

### 3.9 `/proc/iomem` · `/proc/interrupts` · `/proc/bus/pci/devices` (실제)

```text
10000000-3efeffff : pcie@10000000
  10000000-101fffff : PCI Bus 0000:01
    10000000-10003fff : 0000:01:00.0
      10000000-10003fff : nvme
  10200000-104fffff : PCI Bus 0000:02
    10280000-1029ffff : 0000:02:00.0
      10280000-1029ffff : e1000e
4010000000-401fffffff : PCI ECAM

 30:         12          0 ITS-PCI-MSIX-0000:01:00.0   0 Edge      nvme0q0
 31:          1          0 ITS-PCI-MSIX-0000:01:00.0   1 Edge      nvme0q1
 16:          0          0 ITS-PCI-MSIX-0000:00:01.0   0 Edge      PCIe PME, aerdrv, PCIe bwctrl

0100	1b360010	f	        10000004	...
```

- iomem 들여쓰기 = **주소 포함 관계**: 호스트 창 ⊃ 브리지 창(PCI Bus 0000:01) ⊃ 장치 BAR ⊃ 드라이버가 `request_mem_region`으로 잡은 이름(nvme). 드라이버 이름이 없으면 "BAR는 받았지만 드라이버가 안 잡았다".
- interrupts: `ITS-PCI-MSIX` = GICv3 **ITS**(Interrupt Translation Service)를 통한 MSI-X. CPU0 열 숫자가 증가하면 인터럽트가 실제로 도착 중. (Lima VM은 `GICv2m-PCI-MSIX` — 다른 MSI 컨트롤러 [확인됨])
- `/proc/bus/pci/devices`: `0100` = bus 01, devfn 00 / `1b360010` = vendor+device / `f` = IRQ 15 / `10000004` = BAR0 값(하위 4 = 64-bit 메모리 BAR 플래그).

### 3.10 AER 실제 주입 → dmesg → sysfs 카운터 → lspci (★ 실험)

- 방법: QEMU HMP 모니터 `pcie_aer_inject_error -c rp1 0x1` (Correctable, Receiver Error = CESta bit0) 와 `-c rp1 0x40` (Bad TLP = bit6). 스크립트 [run_qemu.sh --inject-aer](onsitePrep/lp13/lab/run_qemu.sh). 두 번 실행해 재현 확인.

```text
pcieport 0000:00:01.0: AER: Multiple Correctable error message received from 0000:00:01.0
pcieport 0000:00:01.0: PCIe Bus Error: severity=Correctable, type=Physical Layer, (Receiver ID)
pcieport 0000:00:01.0:   device [1b36:000c] error status/mask=00000041/0000e000
pcieport 0000:00:01.0:    [ 0] RxErr                  (First)
pcieport 0000:00:01.0:    [ 6] BadTLP

$ cat /sys/bus/pci/devices/0000:00:01.0/aer_dev_correctable      # after
RxErr 1
BadTLP 1
BadDLLP 0
Rollover 0
Timeout 0
NonFatalErr 0
CorrIntErr 0
HeaderOF 0
TOTAL_ERR_COR 1
rootport_total_err_cor=1
CESta via setpci: 00000000  UESta: 00000000
	ErrorSrc: ERR_COR: 0008 ERR_FATAL/NONFATAL: 0000          (lspci -vvv)
```

| 관찰 | 의미 |
|---|---|
| `error status/mask=00000041/0000e000` | CESta = bit0(RxErr) + bit6(BadTLP), CEMsk = bit13–15 마스크(AdvNonFatal, CorrInt, HeaderOF) — lspci `CEMsk` 줄과 일치 |
| `type=Physical Layer` | RxErr는 물리 계층 에러(8b/10b·128b/130b 디코드, framing) → **신호 품질** 의심 |
| `Multiple Correctable … ` + `TOTAL_ERR_COR 1` | 두 에러가 한 ERR_COR 메시지로 처리됨 → 항목별 1씩인데 total 1 (§2.7) |
| setpci로 CESta를 읽으면 `00000000` | **커널 AER 드라이버가 처리 후 W1C로 지웠다** → 상태 레지스터만 폴링하는 스크립트는 에러를 놓친다. 카운터(sysfs)·dmesg·`ras:aer_event`를 보라 |
| `ErrorSrc: ERR_COR: 0008` | 마지막 ERR_COR의 Requester ID = 0x0008 = bus 0, dev 1, fn 0 = **00:01.0** 자기 자신 |
| `pcie_snapshot.py` 경고 | `0000:00:01.0: aer_dev_correctable total=1`, `aer_rootport_total_err_cor total=1` (§3.14) |

### 3.11 remove → rescan + dynamic debug + ftrace (실제)

```text
# echo "file drivers/pci/probe.c +p" > /sys/kernel/debug/dynamic_debug/control
# echo "pci_scan_single_device pci_setup_device pci_bus_read_dev_vendor_id ..." > /sys/kernel/tracing/set_ftrace_filter
# echo function > /sys/kernel/tracing/current_tracer
# echo 1 > /sys/bus/pci/devices/0000:01:00.0/remove
pci 0000:01:00.0: device released
after remove: (lspci -s 01:00.0 prints nothing) ; RP LnkSta=2011   ← 링크는 그대로 살아 있음!
# echo 1 > /sys/bus/pci/rescan
pci_bus 0000:00: scanning bus
pcieport 0000:00:01.0: scanning [bus 01-01] behind bridge, pass 0
pci_bus 0000:01: scanning bus
pci 0000:01:00.0: [1b36:0010] type 00 class 0x010802 PCIe Endpoint
pci 0000:01:00.0: BAR 0 [mem 0x10000000-0x10003fff 64bit]: assigned
nvme nvme0: pci function 0000:01:00.0
--- trace
  sh-80  [001] .....  7.142032: pci_bus_read_dev_vendor_id <-pci_device_is_present
  sh-80  [001] .....  8.331665: pci_scan_single_device <-pci_scan_slot
  sh-80  [001] .....  8.331814: pci_bus_read_dev_vendor_id <-pci_scan_single_device
```

- **`remove`는 SW만 지운다** — root port LnkSta는 `2011`(DLActive+) 그대로. 즉 remove+rescan 루프는 **링크 트레이닝을 다시 하지 않는다**. 링크 재학습까지 검증하려면 SBR/hot reset, Link Disable 토글, PERST#/전원 사이클이 필요 (LP14 §2.4 표).
- ftrace에서 `pci_bus_read_dev_vendor_id`가 슬롯마다 호출 = "**Vendor ID를 읽어 0xFFFF가 아니면 장치 있음**" — 열거의 본질이 그대로 보인다. remove 직전 호출은 `pci_device_is_present`(제거 전 존재 확인).
- `echo 1 > reset` → `reset OK`, nvme 드라이버가 큐를 다시 만듦 (`nvme nvme0: 2/0/0 default/read/poll queues`).

### 3.12 BAR 직접 읽기 — devmem / resource0 mmap (실제)

```text
BAR0=0x0000000010000000
# devmem 0x10000008 32           → 0x00010400        (NVMe VS register)
# devmem 0x10000000 64           → 0x004008200F0107FF (NVMe CAP register)
# python3 mmap(resource0)        → NVMe CAP=0x4008200f0107ff MQES=2048  VS=0x10400 -> NVMe 1.4
```

- NVMe 레지스터 맵: offset 0x00 CAP(64-bit), 0x08 VS → `0x00010400` = **NVMe 1.4**. `CAP.MQES`(bits 15:0)+1 = 2048 큐 엔트리.
- 같은 값을 두 경로(물리 주소 `devmem`, sysfs `resource0` mmap)로 읽어 일치 → **BAR 매핑 + Memory Space Enable + 장치 응답**까지 확인하는 가장 짧은 smoke test. (bare-metal의 "레지스터 sanity"와 같은 생각)

### 3.13 `lspci -F` — 덤프를 다른 머신에서 디코드 (검증 ★)

- 질문: "BusyBox만 있는 보드라면?" → 보드에서는 **덤프만** 뜨고, 디코드는 내 PC의 `lspci -F`로.
- 실험 1: 게스트에서 `lspci -xxxx > dump.txt` → Lima VM에서 `lspci -F dump.txt -vvv` → 게스트 원본 `-vvv`와 diff.
- 실험 2: 게스트에서 **lspci 없이** BusyBox `hexdump`로 sysfs `config`를 lspci 덤프 형식으로 출력:

```bash
for d in /sys/bus/pci/devices/*; do b=${d##*/}; echo "${b#0000:} sysfs dump"
  hexdump -v -e '16/1 " %02x" "\n"' $d/config | awk '{printf "%03x:%s\n", (NR-1)*16, $0}'; echo; done
```

- 결과 [확인됨]:
  - `lspci -F guest_sysfs_dump.txt -vvv` 와 `lspci -F guest_xxxx.txt -vvv` 는 **diff 0줄** (헤더 줄은 "BB:DD.F + 아무 텍스트", 오프셋은 2자리든 3자리든 OK).
  - 원본과의 차이는 두 종류뿐: ① `Region 0: … [size=4K]` 의 **size가 사라짐** (size는 config에 없고 커널 sysfs `resource`에서 오기 때문), ② 브리지 prefetch 창 표기(`[32-bit]` → `[64-bit]`, 원본은 커널 정보 사용) 그리고 `Kernel driver in use` 줄 없음.
  - 트리(`-F … -tv`)도 동일하게 재구성됨.
- 의미: **현장/고객 보드에서 `lspci -vvxxxx` 하나만 받아 오면 나머지는 사무실에서 원하는 형식으로 다시 볼 수 있다** (man page가 말하는 용도). 덤프 파일: [qemu_lspci_xxxx.txt](onsitePrep/lp13/outputs/qemu_lspci_xxxx.txt), [qemu_sysfs_config_dump.txt](onsitePrep/lp13/outputs/qemu_sysfs_config_dump.txt), 디코드 [qemu_lspci_F_decode.txt](onsitePrep/lp13/outputs/qemu_lspci_F_decode.txt).

### 3.14 `pcie_snapshot.py` 실제 출력 (QEMU 게스트)

```text
# python3 /pcie_snapshot.py --out /tmp/a.json            (before AER injection)
WARNING: 0000:00:04.0: empty downstream port, DLActive- (no device / link not up)
10 devices, 1 warnings
# (after injection)
WARNING: 0000:00:01.0: aer_dev_correctable total=1
WARNING: 0000:00:01.0: aer_rootport_total_err_cor total=1
WARNING: 0000:00:04.0: empty downstream port, DLActive- (no device / link not up)
10 devices, 3 warnings

0000:00:01.0 0x1b36 0x000c pcieport parent=None cur=2.5/x1 max=16.0/x4 Root Port DLActive=True
0000:00:04.0 0x1b36 0x000c pcieport parent=None cur=16.0/x32 max=16.0/x32 Root Port DLActive=False
0000:01:00.0 0x1b36 0x0010 nvme parent=0000:00:01.0 cur=2.5/x1 max=2.5/x1 Endpoint DLActive=False
0000:04:00.0 0x104c 0x8233 pcieport parent=0000:03:00.0 cur=2.5/x1 max=None/x0 Downstream Port DLActive=True
0000:05:00.0 0x1234 0x11e8 None parent=0000:04:00.0 cur=None/xNone max=None/xNone None DLActive=None
```

- JSON 한 장치 예 (발췌): `"link": {"current_link_speed": "2.5 GT/s PCIe", "max_link_width": 1, …}`, `"config": {"pcie_cap_offset": "0x80", "port_type": "Endpoint", "lnksta_raw": "0x0011", "dl_active": false}`.

## 4. 꼬리 질문 체인

### 체인 A — "What can you see with lspci?" (스크린 실제 질문 확장)

Q → 꼬리1 "Which fields do you check first for a link problem?" → 꼬리2 "How do you read a register lspci doesn't decode?" → 꼬리3 "How would you script this for 1,000 boot cycles?" → 꼬리4 "Why can't you trust DLActive on the endpoint?"

<details>
<summary>A-Q. What can you see with lspci?</summary>

- **EN**: "Everything the kernel enumerated, and as root, the decoded config space of each function. At the top level: bus/device/function, vendor and device ID, class, and the bound driver. With -vv: command and status bits — memory enable, bus master — the BARs and where they're mapped, the bridge windows, and all capabilities: MSI or MSI-X with vector counts, power management state, and the PCI Express capability with device, link and slot registers. With -vvv as root you also get the extended capabilities, like AER status and masks. And -tv gives me the topology tree, which tells me which root port a device hangs off."
- 한국어 핵심: lspci = config space 번역기. 목록·트리·BAR·드라이버·capability·링크·AER. root 아니면 64 B만.
</details>

<details>
<summary>A-꼬리1. Which fields do you check first for a link problem?</summary>

- **EN**: "I go to the downstream port above the device — the root port or switch port — not the endpoint. First DLActive: if the port reports LLActRep and DLActive is set, the data link layer is up, which means the LTSSM reached L0. Then Train should be clear, and then the negotiated speed and width in LnkSta against the minimum of both sides' LnkCap. If it's lower, I look at LnkSta2 for equalization phases, at LnkCtl2 target speed, and at the AER correctable counters, because receiver errors usually go with a marginal link."
- 한국어 핵심: 위 포트 → LLActRep → DLActive → Train → speed/width vs min(LnkCap 양쪽) → LnkSta2 EQ → AER.
</details>

<details>
<summary>A-꼬리2. How do you read a register lspci doesn't decode?</summary>

- **EN**: "setpci. I can address it by capability name plus offset, like CAP_EXP+0x12.w for Link Status or ECAP_AER+0x10.l for correctable error status, or by capability ID if setpci doesn't know the name, like ECAP27 for lane margining. For a full picture I take lspci -xxxx, which dumps the 4K extended config space, and decode the bits myself against the spec or pci_regs.h. For vendor registers outside config space — like the controller's LTSSM debug register — I'd use the BAR through sysfs resource mmap or devmem, or the controller's debugfs if the driver exposes it, like ltssm_status on DesignWare."
- 한국어 핵심: setpci 이름+오프셋 / ECAPxx / -xxxx 덤프 / BAR는 resourceN·devmem / DWC debugfs.
</details>

<details>
<summary>A-꼬리3. How would you script this for 1,000 boot cycles?</summary>

- **EN**: "I wouldn't parse lspci's human output — the man page itself says only the -m formats are stable. I read sysfs directly: current and max link speed and width, the AER counters, the driver link, and the raw config file for DLActive. Each boot writes one JSON snapshot with a timestamp and the board ID; the host side diffs it against the expected topology and flags any downgraded link, missing device or nonzero AER delta. That's exactly what my pcie_snapshot script does, and the key design point is to judge each link as a pair — the port above and the device below — otherwise you get false positives on root ports whose partner is simply slower."
- 한국어 핵심: lspci 파싱 X → sysfs JSON, 기대 토폴로지와 diff, 링크는 쌍으로 판단 (실제 오탐 경험).
</details>

<details>
<summary>A-꼬리4. Why can't you trust DLActive on the endpoint?</summary>

- **EN**: "Data Link Layer Link Active reporting is only required on downstream ports that support it — the LnkCap bit LLActRep tells you. Most endpoints don't implement it, so they read DLActive minus even when the link is perfectly fine. In my QEMU lab the NVMe endpoint shows DLActive minus while the root port above it shows plus."
- 한국어 핵심: DLActive는 LLActRep+인 downstream port에서만 의미.
</details>

### 체인 B — "Without lspci (bare-metal or minimal rootfs), how do you get the same info?"

Q → 꼬리1 "How do you find the PCIe capability?" → 꼬리2 "What if the device isn't there?" → 꼬리3 "How would you decode it later?"

<details>
<summary>B-Q. No lspci — how do you get the same information?</summary>

- **EN**: "On a minimal Linux rootfs I read the sysfs config file directly — it's the same bytes lspci reads — with hexdump, or the link attributes like current_link_speed. On bare metal there's no kernel, so I go through ECAM: base address plus bus shifted by 20, device by 15, function by 12, plus the register offset, and I read Vendor ID first; all-ones means nothing answered. That's literally what the kernel's enumeration does — in my ftrace capture you can see pci_bus_read_dev_vendor_id called per slot."
- 한국어 핵심: sysfs `config` hexdump / bare-metal은 ECAM 직접, Vendor ID 0xFFFF = 없음.
</details>

<details>
<summary>B-꼬리1. How do you find the PCIe capability in raw config space?</summary>

- **EN**: "Check the Capabilities List bit in Status, read the pointer at 0x34, and walk the list: each entry has an ID byte and a next pointer. ID 0x10 is PCI Express; Link Status is at plus 0x12. I mask the low two bits of the pointer and keep a loop guard, because a broken device can return a circular list. Extended capabilities start at 0x100 with a 32-bit header — ID in the low 16 bits, next pointer in bits 31 to 20."
- 한국어 핵심: Status bit4 → 0x34 → (ID, next) 순회, 0x10 = PCIe, 루프 가드; ext cap 0x100 헤더.
</details>

<details>
<summary>B-꼬리2. What if the device isn't there?</summary>

- **EN**: "Then config reads to it return all ones, because there's no completer — the root port completes with an Unsupported Request or the controller returns all ones. So enumeration simply doesn't find it. The root port itself is still readable, and its Link Status shows DLActive clear and its Slot Status shows presence detect, which tells me whether it's 'no card' or 'card but no link'."
- 한국어 핵심: 0xFFFF → 없음. root port의 DLActive·PresDet로 "카드 없음 vs 링크 실패" 구분.
</details>

<details>
<summary>B-꼬리3. How would you decode a raw dump later?</summary>

- **EN**: "Format it like lspci's -x output — a header line with the BDF, then offset colon bytes — and feed it to lspci -F on any Linux machine. I verified that: a BusyBox hexdump of the sysfs config files decodes with lspci -F identically to a native lspci -xxxx dump. The only thing you lose is BAR sizes, because those come from the kernel, not from config space."
- 한국어 핵심: `lspci -F` 오프라인 디코드, BAR size만 빠짐 (검증함).
</details>

### 체인 C (추가) — "Can lspci tell you the LTSSM state?" (스크린 Q3 변형)

Q → 꼬리1 "Then where do you get it?" → 꼬리2 "What if the controller driver doesn't expose it?" → 꼬리3 "When do you need an analyzer?"

<details>
<summary>C-Q. Can lspci show you the LTSSM state?</summary>

- **EN**: "Not directly — the LTSSM state isn't in the standard configuration registers. lspci gives indirect evidence: DLActive set on the downstream port means it got through L0 and flow-control init; Train set means it's in training; and the negotiated speed and width. If the link is down I won't even see the endpoint."
- 한국어 핵심: 표준 레지스터에 없음 → DLActive/Train/speed/width로 간접.
</details>

<details>
<summary>C-꼬리1. Then where do you get the actual state?</summary>

- **EN**: "From the controller. Most SoC PCIe controllers have a vendor debug register with the LTSSM state field — for example the Synopsys DesignWare core — and recent Linux kernels expose it in debugfs as dwc_pcie_<dev>/ltssm_status, along with per-lane event counters like receiver errors and deskew errors, and even error injection. On bare metal I'd poll that register in a tight loop and log the transitions with timestamps."
- 한국어 핵심: vendor debug reg, DWC debugfs `ltssm_status` + RAS DES 카운터/에러 주입.
</details>

<details>
<summary>C-꼬리2. What if the driver doesn't expose it?</summary>

- **EN**: "Then I read the register myself — the controller's own register space is usually a platform MMIO region from the device tree, not a PCI BAR, so devmem or a small debug module works. I'd also use the controller's lane-level status like receiver-detect per lane to see whether we even got past Detect."
- 한국어 핵심: DT의 컨트롤러 MMIO를 devmem/모듈로, lane별 receiver detect.
</details>

<details>
<summary>C-꼬리3. When do you need a protocol analyzer?</summary>

- **EN**: "When software can't tell me why a transition fails — for example the link bounces between Polling and Detect, or it trains at Gen1 but never completes equalization for Gen4. The analyzer shows the TS1 and TS2 ordered sets and the preset requests on the wire, which tells me whether it's the TX side, the RX side, or a protocol disagreement. And the scope comes before both: power, refclk and PERST# timing."
- 한국어 핵심: 상태 전이 원인 = 애널라이저 (TS1/TS2, EQ preset), 그 전에 스코프 (전원/refclk/PERST#).
</details>

### 체인 D (추가) — "You said you mostly used macOS. How does that map?"

Q → 꼬리1 "What does that number mean?" → 꼬리2 "So what would you check on Linux?"

<details>
<summary>D-Q. You mentioned macOS tools — how does that map to Linux?</summary>

- **EN**: "system_profiler gives the same summary — link width, link speed, link up — and ioreg exposes the raw registers as properties on each IOPCIDevice: IOPCIExpressLinkStatus, LinkCapabilities, and so on. They're the same PCIe capability registers lspci decodes, just printed as decimal numbers."
- 한국어 핵심: system_profiler = 요약, ioreg = raw 레지스터 10진수.
</details>

<details>
<summary>D-꼬리1. What does IOPCIExpressLinkStatus = 12307 mean?</summary>

- **EN**: "12307 is 0x3013. Bits 3 to 0 are the current speed, 3, so 8 GT/s; bits 9 to 4 are the width, 1, so x1; bit 12 is slot clock configuration; and bit 13 is Data Link Layer Link Active, which is set — that's a root port with the link up. That's exactly what I'd read as LnkSta on Linux."
- 한국어 핵심: 0x3013 → Gen3 x1, SlotClk, DLActive (이 Mac에서 실제로 뽑은 값).
</details>

<details>
<summary>D-꼬리2. So on Linux, what's your first command?</summary>

- **EN**: "lspci -tv to find the port above the device, then sudo lspci -vvv -s on that port for LnkCap, LnkSta and AER, and cat the sysfs current_link_speed and aer_dev_correctable if I want to script it."
- 한국어 핵심: `-tv` → 위 포트 `-vvv` → sysfs.
</details>

### 체인 E (추가) — "AER correctable errors keep increasing. Which device is at fault?"

Q → 꼬리1 "How do you collect them reliably?" → 꼬리2 "When do you worry?"

<details>
<summary>E-Q. AER correctable counters are increasing — which side is the problem?</summary>

- **EN**: "Not necessarily the device whose counter goes up. A correctable error is logged by the receiver that saw it — a receiver error at the root port means the bits coming from the endpoint's transmitter, through the channel, arrived damaged at the root port's receiver. So I look at which side is counting, which error type — physical layer receiver error versus data link layer bad TLP or replay — and whether it tracks temperature, a specific lane, or a specific board."
- 한국어 핵심: 카운터 오른 쪽 = 받은 쪽. 종류(PHY vs DLL)와 조건 상관관계.
</details>

<details>
<summary>E-꼬리1. How do you collect them reliably?</summary>

- **EN**: "Not by polling the status register — the kernel AER driver clears it after handling; in my lab setpci read zero right after the error was logged. I use the sysfs counters, which are cumulative, plus the ras:aer_event tracepoint or dmesg for timestamps, and snapshot them before and after each test step so I get deltas."
- 한국어 핵심: CESta는 커널이 W1C로 지움 → 누적 카운터 + tracepoint, 전후 delta.
</details>

<details>
<summary>E-꼬리2. When do you start to worry about correctable errors?</summary>

- **EN**: "Correctable means the link recovered — replay or retraining — so a few over a long soak can be acceptable, but a rate that increases with temperature or voltage, or concentrates on one lane, is a margin problem that will become uncorrectable or a link drop in the field. I'd set a per-hour threshold in the validation plan and trend it across PVT corners."
- 한국어 핵심: 비율·추세·lane 집중이 문제, 기준치를 테스트 플랜에 숫자로.
</details>

## 5. Don 경험 연결

- "At Solidigm I worked on the PCIe Gen6 SSD controller from the endpoint side — so when I read LnkSta on the host's root port, I know which controller-side state machine and registers produced it. During pre-silicon FPGA bring-up we checked link-up and the PCIe/NVMe subsystem registers with bare-metal reads, which is the same thing lspci does from the host side." `[숫자 채우기: FPGA 이미지 수 / 블록 수]`
- "At SK hynix my test platform automated reliability runs on PCIe 3, 4 and 5 interfaces in temperature chambers — the pattern was always: snapshot link state and error counters, apply stress, snapshot again, and trend the deltas. That's what my sysfs snapshot script does." `[숫자 채우기: 챔버 수, 동시 DUT 수]`
- "At Apple I root-caused PCIe and other interface failures on new wireless chipsets with a DSO and a protocol analyzer — the software view tells you that the link is degraded, the analyzer tells you why." `[숫자 채우기: 사례 1개 — 증상/원인/수정]`
- 스크린 회고 브릿지: "I've used the macOS equivalents more — ioreg shows IOPCIExpressLinkStatus, which is the raw Link Status register — so let me answer at the register level."

## 6. 실습 / 코드

- **[pcie_snapshot.py](onsitePrep/lp13/pcie_snapshot.py)** (외부 모듈 없음, Python 3)
  - 하는 일: `/sys/bus/pci/devices` 순회 → ID/드라이버/부모 포트/NUMA/power_state/msi_irqs → `current/max_link_speed/width` → AER 카운터 파싱(이름 일반화) → `config` 바이너리에서 capability 순회해 PCIe cap의 port type, LnkCap/LnkSta raw, Train/DLActive/BWMgmt → **링크 단위 판정**(아래 장치 기준 expected = min(자기 max, 부모 max)), 빈 downstream port의 DLActive-, 링크 열거됐는데 DLActive- (런타임 다운 의심), AER total>0 → JSON + 경고, `--strict`면 경고 시 exit 1.
  - 검증 [확인됨]:
    - macOS `python3 pcie_snapshot.py --selftest` → 가짜 sysfs 트리(Gen4 x4 root port 아래 Gen5-capable EP가 Gen3로 학습, 빈 root port, conventional 장치, AER 3건)로 8개 체크 **ALL PASS**.
    - Lima VM (root/non-root): virtio 10개, `link: null`(conventional), non-root는 `config_bytes 64` + "run as root" 노트 — 경고 0.
    - QEMU 게스트: §3.14 출력. **1차 버전은 root port를 단독 판정해서 오탐 5건**(`00:01.0 speed downgraded 2.5 < 16` 등) → 쌍 판정으로 수정 후 빈 포트 1건만 남음. AER 주입 후 3건.
  - 사용: `sudo python3 pcie_snapshot.py --out snap_$(date +%s).json --strict`
- **QEMU 랩 재현** (Lima VM 안에서, VM 루트 FS가 현재 read-only라 전부 `/tmp/LP13`에서 동작):
  - `limactl shell elinux -- bash onsitePrep/lp13/lab/run_qemu.sh onsitePrep/lp13/lab/guest_explore.sh` → §3.2–3.14 전부 ([guest_explore.sh](onsitePrep/lp13/lab/guest_explore.sh))
  - `… run_qemu.sh onsitePrep/lp13/lab/guest_aer.sh --inject-aer` → §3.10 ([guest_aer.sh](onsitePrep/lp13/lab/guest_aer.sh))
  - [build_initramfs.sh](onsitePrep/lp13/lab/build_initramfs.sh): BusyBox initramfs + VM의 lspci/setpci/pcilmr/python3/trace-cmd + 동적 라이브러리 + pci.ids + nvme 모듈.
  - 알려진 한계 [관찰]: QEMU는 LnkCtl2 Target Speed·Retrain·Link Disable을 에뮬레이트하지 않음, 엔드포인트 LnkCap은 2.5 GT/s x1 고정 → 진짜 downtrain은 selftest로만 검증. `lspci -k`의 "Unable to load libkmod resources" 경고는 initramfs에 modules.dep가 없어서 (무해).
- 미실행: nvme-cli, fio 측정, VFIO 바인딩, pcimem, `trace-cmd record -e ras:aer_event` [미실행] — 레시피만:

```bash
# [미실행] AER 이벤트를 타임스탬프로 수집
sudo trace-cmd record -e ras:aer_event -o aer.dat &   # 테스트 실행 후
sudo trace-cmd report aer.dat
# [미실행] VFIO로 장치를 user-space에 넘기기
echo vfio-pci | sudo tee /sys/bus/pci/devices/0000:01:00.0/driver_override
echo 0000:01:00.0 | sudo tee /sys/bus/pci/drivers/nvme/unbind
echo 0000:01:00.0 | sudo tee /sys/bus/pci/drivers_probe
ls -l /sys/bus/pci/devices/0000:01:00.0/iommu_group   # /dev/vfio/<group>
```

## 7. 자주 틀리는 것

1. **endpoint의 LnkSta만 보고 "링크 정상"** — DLActive는 위 포트(LLActRep+)에서 본다. endpoint `DLActive-`는 정상.
2. **root port의 `LnkSta < LnkCap`을 downgrade로 오판** — 파트너가 느리면 정상. 판정은 **min(양쪽 LnkCap)** 기준 (lspci도 그렇게 표시, 내 스크립트 1차 오탐 사례).
3. **링크 다운인데 Speed/Width를 믿음** — DLActive-면 그 값은 의미 없음 (QEMU 빈 포트 "16GT/s x32").
4. **non-root로 lspci 돌리고 "capability가 없다"** — `<access denied>`. sysfs `config`도 64 B. `/proc/iomem` 주소도 0.
5. **lspci 사람용 출력을 스크립트로 파싱** — 버전마다 바뀜(`(ok)` 사라짐, AER 이름 `Receiver Error`→`RxErr`). `-mm`/`-vmm` 또는 sysfs.
6. **AER 상태 레지스터 폴링으로 에러 수집** — 커널이 W1C로 지운다. 누적 카운터·tracepoint·dmesg.
7. **`remove`+`rescan`을 "링크 재학습 테스트"라고 부름** — SW 재열거일 뿐, 링크는 계속 L0 (실제 확인). 재학습은 SBR/Link Disable/PERST#/전원.
8. **BAR size가 config space에 있다고 생각** — sizing으로 알아내는 값, 커널이 sysfs `resource`로 보관 (`lspci -F`에서 사라지는 이유).
9. **setpci로 링크·ASPM을 바꾸고 원복 안 함** — 커널 캐시와 불일치, 다음 테스트 오염. 바꾼 값은 로그 + 원복.
10. **`current_link_speed`가 ABI 문서에 있다고 단정** — `sysfs-bus-pci` ABI 문서엔 없고 `pci-sysfs.c`에 정의 (안정적이지만 "문서화된 ABI"는 아님) [확인됨 6.18.55].

## 8. 출처

1. lspci(8), setpci(8), pcilmr(8) man pages (로컬: Lima VM) — pciutils 3.14.0 (Lima VM `man lspci`, `setpci --dumpregs`, `lspci -A help`) · https://man7.org/linux/man-pages/man8/lspci.8.html
2. pciutils source `ls-caps.c` (`link_compare`, LnkSta printing) · https://github.com/pciutils/pciutils/blob/master/ls-caps.c
3. Linux `include/uapi/linux/pci_regs.h` (로컬 6.18.55 소스 `/var/lab/src` 로 확인, 같은 파일 온라인: 레지스터 오프셋·비트) · https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
4. Linux `drivers/pci/pci-sysfs.c` (config 64 B 제한, link 속성, reset/reset_subordinate) · https://github.com/torvalds/linux/blob/master/drivers/pci/pci-sysfs.c
5. Documentation/ABI/testing/sysfs-bus-pci · https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-bus-pci
6. Documentation/ABI/testing/sysfs-bus-pci-devices-aer · https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-bus-pci-devices-aer
7. Accessing PCI device resources through sysfs (Documentation/PCI/sysfs-pci.rst) · https://docs.kernel.org/PCI/sysfs-pci.html
8. PCI Express AER driver guide (pcieaer-howto) · https://docs.kernel.org/PCI/pcieaer-howto.html
9. Documentation/ABI/testing/debugfs-dwc-pcie (ltssm_status, RAS DES counters, error injection) · https://www.kernel.org/doc/Documentation/ABI/testing/debugfs-dwc-pcie
10. Linux `drivers/pci/controller/dwc/pcie-designware.c` ("PCIe Gen.%u x%u link up"), `drivers/pci/pci.c` (`__pcie_print_link_status`, "not ready … giving up"), `include/ras/ras_event.h` (aer_event) · https://github.com/torvalds/linux/tree/master/drivers/pci
11. QEMU PCI Express guidelines (pcie-root-port, switch topology) · https://github.com/qemu/qemu/blob/master/docs/pcie.txt — `x-speed`/`x-width` 속성은 VM에서 `qemu-system-aarch64 -device pcie-root-port,help`로 확인
12. QEMU HMP `pcie_aer_inject_error` · https://www.qemu.org/docs/master/system/monitor.html
13. NVM Express Base Specification (CAP/VS register layout) · https://nvmexpress.org/specifications/
14. Meta/OCP pcicrawler (PCIe topology + link mismatch tool) · https://engineering.fb.com/2020/08/05/open-source/pcicrawler/
15. VFIO — Virtual Function I/O · https://docs.kernel.org/driver-api/vfio.html
16. N01 (이 트랙의 출발점) · [2026-10-31_N01_pcie_lspci_ltssm.html](2026-10-31_N01_pcie_lspci_ltssm.html)

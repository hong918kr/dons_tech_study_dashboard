# LP10 · 전원 관리: D-state · ASPM · L1 substates · runtime PM
<!-- theme: pastel -->

> **묶음**: D 고급 기능 · **우선**: P2 (여유 있을 때, 단 "idle 후 장치가 사라진다" 디버그 질문 방어용으로 §0·§4 체인 A는 꼭) · **선행 노트**: [LP09 AER/DPC](2026-10-31_LP09_aer_dpc_error_handling.html) · [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · **다음 노트**: [LP11 리셋](2026-10-31_LP11_reset_types.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [N01 lspci/LTSSM](2026-10-31_N01_pcie_lspci_ltssm.html) · **코드**: [onsitePrep/lp10/aspm_dump.sh](onsitePrep/lp10/aspm_dump.sh) (QEMU + Lima VM에서 실행 ✅)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "D-state와 L-state 차이는?", "ASPM이 뭐고 Linux에서 어떻게 끄나?", "L1.2는 무엇이 꺼지고 어떻게 깨어나나?", "idle 후에 latency가 튀거나 장치가 사라지면 어떻게 디버그하나?", "L1.2 진입/탈출을 어떻게 검증하나?"
- **외울 것 1 — 두 개의 축**: **D-state = 장치(function)의 전원 상태** (D0 켜짐 … D3hot 소프트 꺼짐, D3cold 전원 제거) / **L-state = 링크(전선)의 전원 상태** (L0 정상, L0s·L1 절전, L2·L3 꺼짐). 장치가 D3hot이면 링크는 L1, D3cold면 L2/L3. **ASPM은 D0인 채로 하드웨어가 알아서 L0s/L1로 들어갔다 나오는 것.**
- **외울 것 2 — 레지스터 3곳**: PCIe capability의 **LnkCap**(ASPM 지원 bit 11:10 + exit latency) · **LnkCtl**(ASPM 켜기 bit 1:0) · **DevCap**(endpoint가 견딜 수 있는 acceptable latency). L1 substate는 별도 확장 capability **L1SS (ID 0x1E)**, L1.2 진입 조건은 **LTR** (장치가 "나 X µs까지 기다릴 수 있어"라고 보고하는 메시지).
- **외울 것 3 — Linux 손잡이**: 부팅 옵션 `pcie_aspm=off` (전역 끄기) · `/sys/module/pcie_aspm/parameters/policy` (`default` / `performance` / `powersave` / `powersupersave`) · 링크별 `/sys/bus/pci/devices/<BDF>/link/l1_aspm`, `l1_2_aspm` … · 장치별 runtime PM `power/control` (`on`/`auto`) + `power/runtime_status`.
- **디버그 한 줄**: "idle 후 문제 → `pcie_aspm=off` 또는 policy `performance`로 재현이 사라지면 ASPM 계열, 그다음 L1.2만 끄기(`l1_2_aspm=0`) → CLKREQ#·T_POWER_ON·LTR을 의심". 단 **sysfs 값이 아니라 LnkCtl/L1SubCtl1 레지스터를 직접 확인** (QEMU에서 실제로 sysfs=1인데 레지스터=0인 걸 관찰, §3.5).
- **30초 영어 답 (헤드라인 질문: "Latency spikes or the device disappears after idle — what do you suspect?")**: "Idle-related problems on PCIe usually point at link power management. With ASPM the link drops to L1, or L1.2 where even the reference clock and common-mode voltage are off, and coming back costs exit latency — microseconds for L1, tens of microseconds for L1.2 if T_POWER_ON is programmed wrong or CLKREQ# isn't wired right. First I'd A/B it: boot with pcie_aspm=off or set the policy to performance and see if the symptom goes away. If it does, I narrow it down per state — disable only L1.2 through the link sysfs knobs — and check LnkCtl and the L1 substates control register on both ends, the LTR values, and AER on the root port. If the device disappeared, I'd expect a Surprise Down or the config reads returning all-ones, which means the L1.2 exit failed and the link never retrained."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 왜 전원 관리가 필요한가 — 비유부터

- 노트북·자동차 ECU는 배터리/발열 예산이 있다. PCIe 링크는 **놀고 있을 때도** 송수신기(SerDes: 직렬화·역직렬화 회로)가 계속 신호를 주고받으며 전기를 쓴다. 레인 하나당 수십~수백 mW 수준이라는 게 흔한 설명 [추정 — 공정·세대마다 다름].
- 그래서 PCIe는 **두 층의 절전**을 만들었다.

**비유: 사무실 건물(장치) + 건물 앞 도로(링크)**

| 비유 | PCIe 이름 | 뜻 |
|---|---|---|
| 직원이 근무 중 | **D0** | 장치가 완전히 동작 |
| 직원이 잠깐 졸고 있음 (선택 사항) | **D1 / D2** | 중간 절전 (거의 안 씀, optional) |
| 직원 퇴근, 건물 불은 꺼졌지만 전기는 들어옴 | **D3hot** | SW가 끈 상태, 전원은 살아 있어 config 레지스터는 대답함 |
| 건물 전원 차단 (경비실 비상전원만) | **D3cold** | 주 전원 제거. 깨우려면 전원 재인가 = 사실상 리셋 |
| 도로에 차가 다님 | **L0** | 링크 정상 |
| 한쪽 차선만 잠깐 신호 대기 | **L0s** | 한 방향 송신기만 짧게 쉼 (빠른 복귀, ns 단위) |
| 도로 양방향 통제 | **L1** | 양쪽 송신기 다 쉼, 복귀는 µs 단위 |
| 통제 + 가로등(시계)도 끔 | **L1.1** | 기준 클럭(refclk)도 끔, 전압 기준(common mode)은 유지 |
| 통제 + 가로등 + 도로 전기까지 끔 | **L1.2** | refclk + common mode 전압까지 끔 → 가장 절전, 복귀는 수십 µs |
| 도로 폐쇄, 비상등(Vaux)만 | **L2** | 주 전원 꺼짐, 보조 전원으로 깨우기 신호만 가능 |
| 도로 철거 | **L3** | 전원 완전히 없음 |

- **누가 스위치를 누르나?**
  - D-state: **소프트웨어(드라이버/OS)** 가 PM capability의 PMCSR 레지스터에 쓴다. ("너 D3hot으로 가")
  - L0s/L1 (ASPM): **하드웨어가 스스로** — 링크가 한동안 조용하면 자동 진입, 트래픽이 오면 자동 복귀. SW는 "해도 된다"는 허락(LnkCtl 비트)만 준다. 그래서 이름이 **ASPM (Active State Power Management: 장치가 D0 '활성' 상태인데도 링크가 알아서 절전)**.
  - L1.1/L1.2: ASPM L1 안에서 한 단계 더 내려가는 **하위 상태(substate)**. CLKREQ# 핀으로 "시계 꺼도 돼 / 다시 켜 줘"를 주고받는다.

### 1.2 그림 — 두 축이 어떻게 맞물리나

```text
  장치(Function) 전원 상태 = D-state            링크 전원 상태 = L-state
  (SW가 PMCSR에 써서 바꿈)                      (D0에서는 HW가 ASPM으로 자동)

  D0  (동작) ───────────────────────────────▶  L0 ⇄ L0s ⇄ L1 ⇄ L1.1 / L1.2   (ASPM, 자동)
   │ SW: PMCSR.PowerState = 3
   ▼
  D3hot (SW off, 전원 有) ──────────────────▶  L1   (PCI-PM L1: 장치가 PM_Enter_L1 요청)
   │ 플랫폼이 전원 제거 (PME_Turn_Off → PME_TO_Ack → L2/L3 Ready)
   ▼
  D3cold (전원 無) ─────────────────────────▶  L2 (Vaux 있음, WAKE#로 깨움) 또는 L3 (아무것도 없음)

  깨어날 때:  D3hot → D0 : 10 ms 대기 (Linux PCI_PM_D3HOT_WAIT)
             D3cold → D0 : 전원 재인가 + PERST# = conventional reset → 100 ms 이상 대기
```

### 1.3 그림 — 절전이 깊을수록 복귀가 느리다

```text
 절전량  ▲
  큼     │                                        ● L1.2  (refclk OFF, common mode OFF)
         │                              ● L1.1  (refclk OFF, common mode 유지)
         │                    ● L1     (TX/RX 둘 다 electrical idle)
         │          ● L0s   (한 방향 TX만 idle)
  작음   │ ● L0
         └──────────────────────────────────────────────────────────▶ 복귀 시간(exit latency)
           0     ~수십 ns–수 µs      ~수 µs–64 µs        +T_POWER_ON(수십 µs 가능)

 규칙: "복귀 시간 ≤ 장치가 견딜 수 있는 시간" 일 때만 켠다
       - L0s/L1: LnkCap exit latency  vs  DevCap acceptable latency   (정적 비교, enumeration 때)
       - L1.2   : LTR로 보고된 현재 허용 지연 ≥ LTR_L1.2_THRESHOLD    (동적, 매번 HW가 판단)
```

### 1.4 그림 — L1.2 진입/탈출과 CLKREQ#

- **CLKREQ#** (Clock Request, active-low 오픈드레인 사이드밴드 핀): 양쪽 누구든 "클럭 필요해"면 Low로 끌어내린다. 아무도 안 끌면 High → "클럭 꺼도 됨".

```text
 시간 ───────────────────────────────────────────────────────────────────▶
 링크     L0 ──▶ L1 (ASPM) ─▶ L1.2.Entry ─▶ L1.2.Idle ··· ─▶ L1.2.Exit ─▶ L1 ─▶ Recovery ─▶ L0
 CLKREQ#  ‾‾‾(low=요청)‾‾│  ↑ 양쪽 다 놓음(High)            ↓ 누군가 Low로 당김 (트래픽 생김)
 REFCLK   ~~~~~~~~~~~~~~~~~~~~~~~~│ 꺼짐 ................ │~~~~ 다시 켜짐
 common   ======================== │ 꺼짐 (L1.2만) ....... │ 복구에 T_CommonMode
 mode                                                     │←── T_POWER_ON ──→│ 이 시간 지나야 송신 시작
```

- 탈출 순서(요점): [8] CLKREQ# Low → refclk 복구 → **T_POWER_ON** 기다림 → common mode 복구(**Common_Mode_Restore_Time**) → L1 탈출 → **Recovery**(LTSSM이 짧게 재동기) → L0. T_POWER_ON을 너무 짧게 프로그램하면 PHY가 준비 안 된 채 송신 → **Recovery 실패 → 링크 다운** = "장치가 사라짐".

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| D-state | Device power state | 장치(function) 하나의 전원 단계 D0–D3 | 드라이버 suspend/resume의 실체 |
| D3hot | Device state 3, hot | SW가 끈 상태, 전원은 있음 → config 읽기 가능 | runtime PM의 기본 목적지 |
| D3cold | Device state 3, cold | 주 전원 제거 | 복귀 = 리셋, config 전부 날아감 |
| PM capability | Power Management Capability (ID 0x01) | D-state를 바꾸는 표준 레지스터 묶음 | PMCSR 비트 1:0이 현재 D-state |
| PMCSR | PM Control/Status Register | 현재 D-state, PME 활성/상태, No_Soft_Reset | `lspci` 의 `Status: D0 NoSoftRst+ PME-Enable-` 줄 |
| L-state | Link power state | 링크 전원 단계 L0/L0s/L1/L2/L3 | 같은 장치라도 링크는 따로 잔다 |
| ASPM | Active State Power Management | D0인 채 HW가 자동으로 L0s/L1 진입·탈출 | latency spike·링크 다운 버그의 단골 |
| L0s | L0 standby | 한 방향 송신기만 짧게 idle | ns급 복귀, 방향별 독립 |
| L1 | Link state 1 | 양방향 idle, PLL 등 일부 꺼짐 | µs급 복귀 |
| L1SS / L1.1 / L1.2 | L1 PM Substates | L1 안의 더 깊은 단계 (refclk, common mode 끔) | 모바일/차량 절전의 핵심, 버그도 핵심 |
| CLKREQ# | Clock Request signal | 클럭 필요 여부를 알리는 사이드밴드 핀 | 보드에 안 연결/잘못 풀업되면 L1.2 사망 |
| refclk | Reference clock (보통 100 MHz) | 양쪽 PHY가 쓰는 기준 시계 | L1.1/L1.2에서 꺼짐 |
| T_POWER_ON | — | L1.2 탈출 시 클럭 켠 뒤 송신 전 기다리는 시간 | 너무 작으면 탈출 실패 |
| Common_Mode_Restore_Time | — | L1.2 탈출 시 선로 기준 전압 복구 시간 | 위와 같음 |
| LTR | Latency Tolerance Reporting | 장치가 "나 지금 X ns까지 기다릴 수 있어"를 메시지로 보고 | L1.2 진입 여부 판단 기준 |
| exit latency | — | 절전 → L0 복귀에 걸리는 시간 (LnkCap에 광고) | 성능 영향 |
| acceptable latency | — | endpoint가 견딜 수 있는 최대 복귀 시간 (DevCap) | 이보다 길면 Linux가 ASPM 안 켬 |
| PME | Power Management Event | 장치가 "깨워 줘"를 알리는 이벤트(메시지 또는 WAKE# 핀) | Wake-on-LAN, D3에서 복귀 |
| WAKE# | — | L2(주 전원 off)에서 깨우는 사이드밴드 핀 | 링크가 죽었으니 메시지를 못 보냄 |
| Clock PM | Clock Power Management | L1/L2에서 CLKREQ#로 refclk을 끄는 기능 (LnkCap bit 18) | L1SS와 별개의 오래된 기능 |
| runtime PM | Linux runtime power management | 시스템은 켜진 채 장치만 idle이면 D3로 | `power/control=auto` |
| system suspend | s2idle / S3(mem) / hibernate | 시스템 전체 잠자기 | resume 후 config 복원 필요 |
| CCC | Common Clock Configuration (LnkCtl bit 6) | 양쪽이 같은 refclk를 쓰는지 | L0s/L1 exit latency 값이 달라짐 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 PM capability (Capability ID 0x01) — D-state 레지스터

- 레지스터 오프셋·비트는 Linux `include/uapi/linux/pci_regs.h` (6.18.55 소스에서 grep) 기준 [1] [확인됨].

```text
 PM capability (cap list 안 어딘가, 예: igb 0x40, QEMU nvme 0x60)
 +0x00  Cap ID = 0x01 | Next ptr
 +0x02  PMC  (PM Capabilities, 16-bit, RO)
          bit 2:0  Version            bit 3  PME clock      bit 5  DSI (Device Specific Init)
          bit 8:6  Aux current        bit 9  D1 support     bit 10 D2 support
          bit 15:11 PME support mask: D0, D1, D2, D3hot, D3cold 각 1비트
 +0x04  PMCSR (PM Control/Status, 16-bit)
          bit 1:0  PowerState (0=D0 1=D1 2=D2 3=D3hot)   ← D3cold는 여기 없음(전원이 없으니 못 읽음)
          bit 3    No_Soft_Reset (1이면 D3hot→D0 때 내부 리셋 안 함 = 상태 유지)
          bit 8    PME_En      bit 12:9 Data select   bit 14:13 Data scale
          bit 15   PME_Status (RW1C)
```

- 전이 시간 (Linux 상수, `drivers/pci/pci.h`) [확인됨]:
  - `PCI_PM_D2_DELAY 200` µs, `PCI_PM_D3HOT_WAIT 10` ms (D3hot↔D0), `PCI_PM_D3COLD_WAIT 100` ms.
  - D3cold → D0은 전원 재인가 + PERST# 해제 = **conventional reset** → 리셋 노트([LP11](2026-10-31_LP11_reset_types.html))의 100 ms 규칙이 그대로 적용.
- **No_Soft_Reset**: 0이면 D3hot→D0에서 장치가 스스로 리셋됨 → Linux는 이것을 **"pm" 리셋 방법**으로도 쓴다 (`pci_pm_reset`, LP11 §2). QEMU에서 실제로 igb `NoSoftRst-` (pm 리셋 가능), nvme `NoSoftRst+` (pm 리셋 불가 → `reset_method`에 pm 없음) 확인 (§3.3, LP11 §3).
- D3hot에서 장치는 **config 요청에만 응답**, 메모리/IO 요청은 처리 안 함 (BAR 접근하면 UR이나 0xFFFFFFFF) — "D3에 간 장치 BAR 읽고 hang/올-1"이 흔한 버그 [추정: 동작은 구현마다 다를 수 있음].

### 2.2 D-state ↔ L-state 매핑

| 장치 D-state | 허용 링크 상태 | 누가 진입시키나 | 메모 |
|---|---|---|---|
| D0 | L0, L0s, L1 (+L1.1/L1.2) | HW (ASPM), SW는 허락만 | ASPM은 D0 전용 |
| D1 / D2 | L1 | 장치가 D-state 바뀌면 PM_Enter_L1 DLLP로 요청 | optional, 드묾 |
| D3hot | L1 (PCI-PM L1, + PCI-PM L1.1/L1.2 가능) | 위와 같음 | L1SS의 "PCI-PM" 비트가 이 경우 |
| D3cold | L2 (Vaux 有) 또는 L3 | 플랫폼: PME_Turn_Off 메시지 → 장치 PME_TO_Ack → L2/L3 Ready → 전원 off | Linux `PCIE_PME_TO_L2_TIMEOUT_US 10000` (10 ms) [확인됨] |

- L1SS capability에 비트가 **두 쌍** 있는 이유: **ASPM L1.x** (D0에서 자동) vs **PCI-PM L1.x** (D3hot 등 SW 지시로 들어간 L1에서). Linux sysfs도 `l1_1_aspm`/`l1_2_aspm`과 `l1_1_pcipm`/`l1_2_pcipm`을 따로 둔다 (§3.2).

### 2.3 ASPM — 정적 협상: 지원 · 허용 지연 · 켜기

```text
 PCI Express capability (ID 0x10) 안
 +0x04 DevCap   bit 8:6  Endpoint L0s Acceptable Latency   bit 11:9 Endpoint L1 Acceptable Latency
                bit 28   FLR capable (LP11)
 +0x0C LnkCap   bit 11:10 ASPM Support (01=L0s 10=L1 11=both)
                bit 14:12 L0s Exit Latency   bit 17:15 L1 Exit Latency
                bit 18 Clock PM   bit 20 DLL Link Active Reporting Capable   bit 22 ASPM Optionality Compliance
 +0x10 LnkCtl   bit 1:0 ASPM Control (01=L0s 10=L1 11=both)   bit 4 Link Disable   bit 5 Retrain
                bit 6 Common Clock Config   bit 8 Enable Clock PM (CLKREQ#)
 +0x24 DevCap2  bit 11 LTR supported         +0x28 DevCtl2  bit 10 LTR enable
```

- 인코딩 (pciutils `ls-caps.c` 의 표와 동일, exit latency와 acceptable latency가 같은 표를 씀) [2] [확인됨]:

| 코드 | L0s (exit / acceptable) | L1 (exit / acceptable) |
|---|---|---|
| 0 | <64 ns | <1 µs |
| 1 | <128 ns | <2 µs |
| 2 | <256 ns | <4 µs |
| 3 | <512 ns | <8 µs |
| 4 | <1 µs | <16 µs |
| 5 | <2 µs | <32 µs |
| 6 | <4 µs | <64 µs |
| 7 | unlimited (>4 µs) | unlimited (>64 µs) |

- **Linux의 켜기 규칙** (`drivers/pci/pcie/aspm.c`) [3] [확인됨]:
  1. 링크 양쪽 LnkCap이 모두 지원하는 상태만 후보 (`aspm_support`).
  2. `pcie_aspm_check_latency()`: endpoint의 acceptable latency와 경로상의 exit latency 비교. **L0s는 방향별**(upstream/downstream 따로), **L1은 max(up, down) + 스위치 하나당 1 µs** 를 누적해서 비교. 넘으면 그 상태를 `aspm_capable`에서 뺀다.
  3. **L1 substates의 exit latency는 비교하지 않는다** — 커널 주석: "장치가 L1SS 지연을 광고하지 않으니 L1 exit latency에 포함됐다고 가정" → L1.2 지연 문제는 **LTR 메커니즘** 몫.
  4. pre-1.1 PCIe 장치는 기본 비활성: dmesg `"disabling ASPM on pre-1.1 PCIe device. You can enable it with 'pcie_aspm=force'"`.
- **L0s는 방향별 독립**: 각 송신기가 따로 L0s에 들어간다. L1은 양쪽 합의(DLLP handshake: PM_Active_State_Request_L1 → PM_Request_Ack) [추정: 세부 DLLP 이름은 spec 5.4.1].
- PCIe 6.0 **Flit mode**에서는 L0s가 없고 대신 **L0p**(일부 레인만 끄는 부분 폭 절전)가 생겼다 [추정 — LP17에서 확인 예정].

### 2.4 L1 PM Substates (Extended Capability ID 0x1E)

```text
 L1SS ext cap (0x100 이후 확장 config 영역)
 +0x00 Header: ID=0x001E, Version, Next
 +0x04 L1SS Capabilities (RO)
         bit 0 PCI-PM L1.2 supported   bit 1 PCI-PM L1.1 supported
         bit 2 ASPM L1.2 supported     bit 3 ASPM L1.1 supported   bit 4 L1 PM Substates supported
         bit 15:8  Port Common_Mode_Restore_Time (µs)
         bit 17:16 Port T_POWER_ON Scale (0=2µs 1=10µs 2=100µs)   bit 23:19 Port T_POWER_ON Value
 +0x08 L1SS Control 1
         bit 0 PCI-PM L1.2 En  bit 1 PCI-PM L1.1 En  bit 2 ASPM L1.2 En  bit 3 ASPM L1.1 En
         bit 15:8  Common_Mode_Restore_Time (upstream 포트에 프로그램)
         bit 25:16 LTR_L1.2_THRESHOLD_Value   bit 31:29 LTR_L1.2_THRESHOLD_Scale
 +0x0C L1SS Control 2
         bit 1:0 T_POWER_ON Scale   bit 7:3 T_POWER_ON Value
```

- 위 필드 전부 `pci_regs.h`의 `PCI_L1SS_CAP_*`, `PCI_L1SS_CTL1_*`, `PCI_L1SS_CTL2_*` 와 대조 [1] [확인됨]. T_POWER_ON scale 해석(2/10/100 µs)은 pciutils `l1pm_calc_pwron()` [2] [확인됨]. LTR threshold 시간 = value × 2^(5×scale) ns (pciutils `cap_ltr_scale`) [확인됨].
- **L1.1 vs L1.2**:
  - L1.1: refclk 꺼짐, **common mode 전압 유지** → 복귀 빠름.
  - L1.2: refclk + common mode 모두 꺼짐 → 가장 절전, 복귀에 T_POWER_ON + common mode restore 필요.
  - L1.2 진입은 **LTR이 허용할 때만**: 장치가 보고한 현재 허용 지연(snoop/no-snoop 중 작은 값) ≥ `LTR_L1.2_THRESHOLD` 일 때. LTR이 꺼져 있으면 L1.2 진입 조건을 만족할 수 없다 [추정: spec 5.5.1 해석 — LTR 미지원 장치는 L1.2를 쓰지 않는다는 게 일반적 설명].
- **Linux가 계산하는 값** (`aspm_calc_l12_info()`) [3] [확인됨]:
  - T_CommonMode = **양쪽 포트의 Common_Mode_Restore_Time 중 큰 값**. [9]
  - T_POWER_ON = **양쪽 포트 T_POWER_ON 중 큰 값** → 양쪽 Control 2에 같은 값 기록.
  - `LTR_L1.2_THRESHOLD = 2 + 4 + T_CommonMode + T_POWER_ON` (µs) — 주석: T(POWER_OFF) ≤ 2 µs, T(L1.2) ≥ 4 µs (PCIe r3.1 5.5.3.3.1 기준).
  - 값을 바꾸기 전 **L1.2 enable을 양쪽에서 끄고**(PCIe r5.0 5.5.4 요구) 프로그램 후 다시 켠다.
- 실제 버그 사례: Qualcomm root port가 T_POWER_ON을 잘못 광고/프로그램해 L1.2 탈출 타이밍 문제 → 2025-11 이후 "PCI: qcom: Program correct T_POWER_ON value for L1.2 exit timing" 패치와 DT 바인딩에 T_POWER_ON 속성 추가 논의 [4]. 이 값은 **보드(AC coupling cap, 클럭 버퍼) 의존**이라 실리콘 검증 + 보드 검증 둘 다의 대상.

### 2.5 LTR (Extended Capability ID 0x18) · OBFF

- 장치(endpoint)가 **LTR 메시지**로 "지금 나는 최대 X ns 지연을 견딤"을 upstream으로 보낸다. 버퍼가 꽉 차가면 작은 값, 한가하면 큰 값.
- LTR ext cap(+0x04 Max Snoop Latency, +0x06 Max No-Snoop Latency, 각 value 10비트 + scale 3비트)은 **SW가 "이것보다 큰 값은 보고하지 마"라는 상한**을 써 두는 곳 [1] [확인됨: `PCI_LTR_MAX_SNOOP_LAT 0x4`, `PCI_LTR_MAX_NOSNOOP_LAT 0x6`, `PCI_LTR_VALUE_MASK 0x3ff`, `PCI_LTR_SCALE_MASK 0x1c00`].
- 켜기: endpoint와 경로상 모든 포트 DevCtl2 bit 10 (`PCI_EXP_DEVCTL2_LTR_EN`). DevCap2 bit 11이 지원 여부.
- **OBFF** (Optimized Buffer Flush/Fill: 플랫폼이 "지금 DMA 몰아서 해" 창을 알려줌, DevCap2 bit 19:18) — 존재만 알면 됨.

### 2.6 PME와 깨우기

- 장치가 D1–D3에서 이벤트(패킷 수신 등) → PMCSR.PME_Status=1 → **PM_PME 메시지** (링크가 L1이면 링크를 깨워 보냄) → root port의 Root Status에 기록 → 인터럽트 → Linux PCIe port service "PME" 가 처리. QEMU dmesg 실제: `pcieport 0000:00:02.0: PME: Signaling with IRQ 30`.
- 링크가 L2(주 전원 off)면 메시지를 못 보내니 **WAKE#** 사이드밴드 핀 사용.
- PME_En(bit 8)을 켜 둬야 하고, PMC bit 15:11에 해당 D-state에서 PME 가능하다고 광고돼야 한다. QEMU igb는 `PME(D0-,D1-,D2-,D3hot-,D3cold-)` → 어떤 상태에서도 깨우기 불가.

### 2.7 system suspend / resume 과 config 복원

```text
 suspend:  driver ->suspend()  →  pci_save_state(dev)   (config header + PCIe cap + MSI/MSI-X + L1SS + LTR ...)
           →  pci_set_power_state(D3hot)  →  (플랫폼) D3cold 가능하면 전원 off
 resume :  전원 on / PERST# 해제 / 링크 재트레이닝  →  pci_set_power_state(D0) (D3hot→D0이면 10 ms)
           →  pci_restore_state(dev)  →  driver ->resume()  (장치 내부 상태·펌웨어는 드라이버 몫)
```

- `pci_save_state`/`pci_restore_state`는 **표준 config 레지스터만** 복원. [7] 장치 BAR 안의 MMIO 레지스터, 펌웨어 상태, 큐는 **드라이버가 다시 초기화**해야 한다.
- 실제 복원 동작은 LP11 실험에서 dmesg로 관찰: `restore config 0x10: 0x00000000 -> 0x10000000` (BAR0) 등 (LP11 §3.3).
- runtime suspend 시 **드라이버 없는(unbound) 장치는 D0에 그대로 두고 config만 저장** — 커널 주석 "If pci_dev->driver is not set (unbound), we leave the device in D0, but it may go to D3cold when the bridge above it runtime suspends" (`pci-driver.c`) [5] [확인됨] → QEMU에서 실제 관찰 (§3.6).

## 3. Linux에서 볼 것

### 3.1 전역 스위치 — Kconfig · 부팅 옵션 · policy

| 손잡이 | 값 | 효과 (aspm.c 코드 기준) [3] |
|---|---|---|
| `CONFIG_PCIEASPM` | y/n | ASPM 코드 자체 |
| `CONFIG_PCIEASPM_DEFAULT` / `_POWERSAVE` / `_POWER_SUPERSAVE` / `_PERFORMANCE` | 택1 | 부팅 시 기본 policy |
| 부팅 옵션 `pcie_aspm=off` | | `aspm_disabled = true` → ASPM 설정을 **건드리지 않음**, sysfs `link/` 속성도 숨김. dmesg `PCIe ASPM is disabled` |
| 부팅 옵션 `pcie_aspm=force` | | pre-1.1 장치·FADT가 막아도 강제로 관리. dmesg `PCIe ASPM is forcibly enabled` |
| `policy = default` | | `link->aspm_default` = **펌웨어(BIOS)가 켜 둔 상태 유지**. 단 6.18에서는 **DT 플랫폼이면 지원되는 L0s/L1을 기본으로 켬** (`pcie_aspm_override_default_link_state`) |
| `policy = performance` | | ASPM 전부 끄고 Clock PM도 끔 |
| `policy = powersave` | | L0s + L1 켬 (L1SS는 안 켬) |
| `policy = powersupersave` | | L0s + L1 + L1.1 + L1.2 전부 |

- Lima VM 실제 출력 (Ubuntu 7.0.0-38-generic, Apple VZ) [실제 출력]:

```bash
$ cat /sys/module/pcie_aspm/parameters/policy
[default] performance powersave powersupersave
$ grep -E "PCIEASPM" /boot/config-$(uname -r)
CONFIG_PCIEASPM=y
CONFIG_PCIEASPM_DEFAULT=y
# CONFIG_PCIEASPM_POWERSAVE is not set
# CONFIG_PCIEASPM_POWER_SUPERSAVE is not set
# CONFIG_PCIEASPM_PERFORMANCE is not set
```

- 해석: `[ ]`가 현재 값. Ubuntu 배포 커널은 default(펌웨어 존중). 쓰기는 `echo powersave | sudo tee /sys/module/pcie_aspm/parameters/policy` (QEMU에서 실제로 `rc=0 policy=default performance [powersave] powersupersave` 확인).
- 6.18 DT 기본값 실제 dmesg (QEMU virt = DT 플랫폼) [실제 출력]:

```text
[    0.260673] pci 0000:01:00.0: ASPM: default states L0s
[    0.261975] pci 0000:02:00.0: ASPM: default states L0s
[    0.263640] pci 0000:03:00.0: ASPM: default states L0s
[    0.269668] pci 0000:05:00.0: ASPM: default states L0s
```

- 해석: ARM SoC(DT 부팅)에서는 BIOS가 ASPM을 켜 주지 않으니 커널이 "지원되는 L0s/L1은 기본 on"으로 덮어쓴다는 메시지. **Rivian 같은 ARM SoC 플랫폼이면 커널 버전 업그레이드만으로 ASPM 동작이 바뀔 수 있다** → 회귀 테스트 포인트 [추정: Rivian 플랫폼이 DT인지 ACPI인지는 모름].

### 3.2 링크별 sysfs `link/` 디렉터리

- 경로: `/sys/bus/pci/devices/<BDF>/link/{clkpm, l0s_aspm, l1_aspm, l1_1_aspm, l1_2_aspm, l1_1_pcipm, l1_2_pcipm}` — 이름은 aspm.c `ASPM_ATTR(...)` 와 ABI 문서 `sysfs-bus-pci` 로 확인 [3][6] [확인됨]. 쓰기: `1/y/on` 켬, `0/n/off` 끔.
- **어느 장치 밑에 있나**: 링크의 **downstream 쪽 장치**(endpoint, 또는 스위치 upstream port) 디렉터리. root port 자신의 `link/`는 비어 있음 (그 포트의 위쪽에는 링크가 없으니까).
- **보이는 조건** (`aspm_ctrl_attrs_are_visible`): `pcie_aspm=off`면 전부 숨김, 그 링크가 그 상태를 지원(`aspm_capable`)할 때만 파일이 생김. `clkpm`은 `clkpm_capable`일 때만.
- QEMU 실제: endpoint `01:00.0`·`02:00.0`·`05:00.0`, 스위치 upstream `03:00.0` 에 `l0s_aspm=1`만 존재 (QEMU 포트가 L0s만 광고). 스위치 downstream `04:00.0`, root port `00:02.0`은 비어 있음.
- Lima VM 실제: virtio 장치 `0000:00:01.0/link/` 디렉터리는 **있지만 비어 있음** (PCIe capability 없음 → link state 없음).

```bash
$ ls -la /sys/bus/pci/devices/0000:00:01.0/link/      # Lima VM 실제 출력
total 0
drwxr-xr-x 2 root root 0 Oct 31 21:58 .
drwxr-xr-x 7 root root 0 Oct 31 21:58 ..
```

- 드라이버 내부 API: `pci_disable_link_state(pdev, PCIE_LINK_STATE_L1 | ...)` — 하드웨어 errata 있는 드라이버가 특정 상태를 영구히 끌 때 사용 (예: 여러 NIC/NVMe 드라이버) [추정: 구체 드라이버 목록 미확인].

### 3.3 `lspci -vvv` 에서 전원 관련 줄 읽기 (QEMU 실제 출력)

- QEMU 10.2 `-machine virt,iommu=smmuv3` + `pcie-root-port` + `igb`, 랩 커널 6.18.55, initramfs에 Ubuntu `lspci` 복사 [실제 출력]:

```text
00:02.0 PCI bridge: Red Hat, Inc. QEMU PCIe Root port          (root port)
	Capabilities: [54] Express (v2) Root Port (Slot+), IntMsgNum 0
		DevCap:	MaxPayload 128 bytes, PhantFunc 0
		LnkCap:	Port #0, Speed 16GT/s, Width x32, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep+ BwNot+ ASPMOptComp-
		LnkCtl:	ASPM Disabled; RCB 64 bytes, LnkDisable- CommClk-
			ExtSynch- ClockPM- AutWidDis- BWInt- AutBWInt- FltModeDis-
		LnkSta:	Speed 2.5GT/s, Width x1
		DevCap2: Completion Timeout: Not Supported, TimeoutDis- NROPrPrP- LTR-
		LnkCap2: Supported Link Speeds: 2.5-16GT/s, Crosslink- Retimer- 2Retimers- DRS-

01:00.0 Ethernet controller: Intel Corporation 82576 Gigabit Network Connection   (endpoint)
	Capabilities: [40] Power Management version 2
		Flags: PMEClk- DSI+ D1- D2- AuxCurrent=0mA PME(D0-,D1-,D2-,D3hot-,D3cold-)
		Status: D0 NoSoftRst- PME-Enable- DSel=0 DScale=0 PME-
	Capabilities: [a0] Express (v2) Endpoint, IntMsgNum 0
		DevCap:	MaxPayload 128 bytes, PhantFunc 0, Latency L0s <64ns, L1 <1us
			ExtTag+ AttnBtn- AttnInd- PwrInd- RBE+ FLReset+ SlotPowerLimit 0W TEE-IO-
		LnkCap:	Port #0, Speed 2.5GT/s, Width x1, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp-
		LnkCtl:	ASPM Disabled; RCB 64 bytes, LnkDisable- CommClk-
		DevCap2: Completion Timeout: Not Supported, TimeoutDis- NROPrPrP- LTR-
```

| 줄 | 해석 |
|---|---|
| `Power Management version 2` / `version 3` | PM capability 존재 (igb v2, QEMU nvme v3). Lima virtio 장치에는 **PM capability 자체가 없음** |
| `Flags: … D1- D2- … PME(D0-,…,D3cold-)` | PMC: D1/D2 미지원, 어떤 D-state에서도 PME(깨우기) 못 보냄 |
| `Status: D0 NoSoftRst- PME-Enable-` | PMCSR: 현재 D0, D3hot→D0 시 내부 리셋함(→ Linux "pm" 리셋 가능), PME 꺼짐 |
| `DevCap: … Latency L0s <64ns, L1 <1us` | endpoint acceptable latency (L0s 코드 0, L1 코드 0) |
| `LnkCap: … ASPM L0s, Exit Latency L0s <64ns` | 이 포트는 L0s만 지원, L1 미지원 → L1/L1SS는 원천 불가 |
| `ClockPM-` | CLKREQ# 기반 클럭 PM 미지원 |
| `LnkCtl: ASPM Disabled` | **실제 레지스터 값**: ASPM 꺼짐 (sysfs는 l0s_aspm=1이라고 말함 — §3.5) |
| `CommClk-` | Common Clock Configuration 안 됨 (가상 장치라 의미 없음) |
| `LTR-` (DevCap2) | LTR 미지원 → 실제 하드웨어라면 L1.2 불가 |
| `Width x32` (root port LnkCap) | QEMU 기본값의 특이점 (실제 칩이면 이상한 값) |
| `L1 PM Substates` 섹션 없음 | QEMU는 L1SS ext cap을 에뮬레이션하지 않음 → 이 랩에서 L1.2 실습 불가 |

- 실제 SoC/노트북 NVMe에서는 이런 줄을 보게 된다 [예시 출력 — 형식만, 값은 지어낸 것]:

```text
		LnkCap:	Port #0, Speed 16GT/s, Width x4, ASPM L1, Exit Latency L1 <8us
			ClockPM+ Surprise- LLActRep- BwNot- ASPMOptComp+
		LnkCtl:	ASPM L1 Enabled; RCB 64 bytes, LnkDisable- CommClk+
			ExtSynch- ClockPM+ AutWidDis- BWInt- AutBWInt-
		DevCap2: ... LTR+ ...
		DevCtl2: ... LTR+ ...
	Capabilities: [1e0 v1] L1 PM Substates
		L1SubCap: PCI-PM_L1.2+ PCI-PM_L1.1+ ASPM_L1.2+ ASPM_L1.1+ L1_PM_Substates+
			  PortCommonModeRestoreTime=10us PortTPowerOnTime=10us
		L1SubCtl1: PCI-PM_L1.2+ PCI-PM_L1.1+ ASPM_L1.2+ ASPM_L1.1+
			   T_CommonMode=0us LTR1.2_Threshold=32768ns
		L1SubCtl2: T_PwrOn=10us
	Capabilities: [1d0 v1] Latency Tolerance Reporting
		Max snoop latency: 1048576ns
		Max no snoop latency: 1048576ns
```

- 해석 포인트: `ClockPM+` + `LnkCtl ClockPM+` = CLKREQ# 사용 중. `L1SubCtl1` 의 `ASPM_L1.2+` 가 양쪽(root port와 endpoint) **모두** 켜져 있어야 L1.2. `T_CommonMode`는 upstream 포트(root port)에만 의미 있음. `LTR1.2_Threshold` 가 Linux 계산식(2+4+T_cm+T_pwron µs, 인코딩 반올림) 결과.

### 3.4 aspm_dump.sh — 모든 링크 한 번에 (QEMU 실제 출력)

- 스크립트: [onsitePrep/lp10/aspm_dump.sh](onsitePrep/lp10/aspm_dump.sh). POSIX sh + `od` + `awk` 만 사용 (BusyBox OK, lspci 불필요) — sysfs `config` 파일을 직접 읽고 capability list를 따라가 PM cap, PCIe cap, L1SS·LTR 비트를 디코드.

```text
ASPM policy: [default] performance powersave powersupersave
cmdline   : (no pcie_aspm=)
== 0000:00:02.0  [0x1b36:0x000c] class=0x060400 driver=pcieport
   sysfs: power_state=D0 d3cold_allowed=0 runtime: control=on status=active
   EXP@0x54 RootPort
   DevCap : AcceptableLat L0s <64ns L1 <1us  FLReset-
   LnkCap : ASPM L0s  ExitLat L0s <64ns L1 <1us  ClockPM-  DLLLARC+
   LnkCtl : ASPM Disabled  CommClk-  ClkReq-  LnkDisable-
   LnkSta : Speed code 1 Width x1 DLActive+
   LTR    : DevCap2.LTR- DevCtl2.LTR_EN-
   L1SS   : (no L1 PM Substates ext cap, or not root)
   sysfs link/:
== 0000:01:00.0  [0x8086:0x10c9] class=0x020000 driver=igb
   sysfs: power_state=D0 d3cold_allowed=1 runtime: control=on status=active
   PM@0x40: D1- D2- PME-from(D0..D3cold)=0x0  PMCSR: D0 NoSoftRst- PME-En- PME-Status-
   EXP@0xa0 Endpoint
   DevCap : AcceptableLat L0s <64ns L1 <1us  FLReset+
   LnkCap : ASPM L0s  ExitLat L0s <64ns L1 <1us  ClockPM-  DLLLARC-
   LnkCtl : ASPM Disabled  CommClk-  ClkReq-  LnkDisable-
   LnkSta : Speed code 1 Width x1 DLActive-
   LTR    : DevCap2.LTR- DevCtl2.LTR_EN-
   L1SS   : (no L1 PM Substates ext cap, or not root)
   sysfs link/: l0s_aspm=1
== 0000:02:00.0  [0x1b36:0x0010] class=0x010802 driver=
   sysfs: power_state=D0 d3cold_allowed=1 runtime: control=on status=active
   PM@0x60: D1- D2- PME-from(D0..D3cold)=0x0  PMCSR: D0 NoSoftRst+ PME-En- PME-Status-
   ...
== 0000:00:01.0  [0x1af4:0x1000] class=0x020000 driver=virtio-pci
   sysfs: power_state=D0 d3cold_allowed=0 runtime: control=on status=active
   (no PCI Express capability: conventional PCI / virtual device -> no ASPM)
```

- 해석: 스크립트 디코드가 lspci와 일치 (L0s <64ns, L1 <1us, FLReset+, NoSoftRst 차이) → 디코더 검증 겸용. `DLActive`는 **downstream port(root port) 쪽에서만 의미** (endpoint의 LnkSta DLActive는 보통 0 — DLLLARC가 downstream port 전용 기능이라). `L1 <1us` 는 LnkCap의 L1 exit 필드가 0이라는 뜻일 뿐, ASPM Support가 L0s만이니 L1은 실제로 못 씀.

### 3.5 교훈: sysfs ≠ 레지스터 (QEMU에서 실제로 본 불일치)

```text
### ASPM bits writable? (01:00.0 LnkCtl)                       [실제 출력]
sysfs l0s_aspm=1  LnkCtl=0000
after setpci write L0s: LnkCtl=0000
after sysfs l0s_aspm=0: sysfs=0 LnkCtl=0000
```

- 커널은 L0s를 켰다고 기억(`link->aspm_enabled`)하지만, QEMU가 LnkCtl의 ASPM Control 비트를 **쓰기 불가(read-only 0)** 로 에뮬레이션해서 레지스터는 계속 0. `setpci`로 직접 써도 0.
- 실리콘에서도 같은 종류의 문제가 생긴다 (RTL에서 비트가 RO로 묶임, 쓰기가 잘못된 함수로 감, 링크 다른 쪽만 켜짐). → **검증 스크립트는 sysfs가 아니라 config 레지스터를 양쪽 끝에서 읽어 확인**한다. `power_state` sysfs도 ABI 문서상 "커널이 기억하는 상태" [6] — 하드웨어 PMCSR과 다를 수 있다.

### 3.6 runtime PM — `power/` 디렉터리 (QEMU 실제 실험)

| 파일 | 의미 |
|---|---|
| `power/control` | `on` = 항상 켜 둠(기본) / `auto` = idle이면 runtime suspend 허용 |
| `power/runtime_status` | `active` / `suspended` / `suspending` / `resuming` / `unsupported` |
| `power/runtime_suspended_time`, `runtime_active_time` | 누적 ms — **residency(체류 시간) 측정**에 사용 |
| `power_state` | 커널이 아는 D-state (D0/D3hot/D3cold) |
| `d3cold_allowed` | 0이면 이 장치는 D3cold로 절대 안 보냄 |

```text
### runtime PM experiment on unbound nvme 02:00.0 and its root port 00:03.0   [실제 출력]
(echo auto > power/control 양쪽, sleep 2)
0000:02:00.0 control=auto runtime_status=suspended power_state=D0 suspended_ms=2059
0000:00:03.0 control=auto runtime_status=active power_state=D0 suspended_ms=0
0000:01:00.0 control=on runtime_status=active power_state=D0 suspended_ms=0
		Status: D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-     (lspci: PMCSR도 D0)
02:00.0 after config read: suspended D0                              (sysfs config 읽기는 깨우지 않음)
```

- 해석 1: 드라이버 없는 nvme는 runtime **"suspended"인데 D0** — 커널 코드 그대로 (§2.7). 상태 이름(`suspended`)과 하드웨어 상태(D0)는 다를 수 있다.
- 해석 2: root port는 `auto`인데도 active — `pcie_port_runtime_idle()` 이 `bridge_d3` 가 false면 `-EBUSY`. `pci_bridge_d3_possible()`: 이 포트는 `Slot+ HotPlug+` (native pciehp) 이고 **"OS가 직접 다루는 hotplug 포트는 2018년까지 벤더가 runtime D3를 검증 안 했다"** 는 이유로 false [5] [확인됨]. → 장치가 D3cold까지 내려가려면 **위쪽 bridge가 D3로 갈 수 있어야** 한다는 계층 규칙을 보여주는 실례.
- 해석 3: `/sys/bus/pci/devices/*/config` 를 읽는 것만으로는 깨어나지 않았다 (kernel cached config일 수 있음) [추정 — sysfs config read 경로가 runtime resume을 호출하는지는 버전마다 다름].

### 3.7 system suspend 진입점 (Lima/QEMU에서 본 것)

```text
$ cat /sys/power/state          # QEMU 랩 커널 실제
freeze mem disk
$ cat /sys/power/mem_sleep
[s2idle]
```

- `pm_test` 파일 없음 (CONFIG_PM_DEBUG 꺼짐) → 이 랩에서는 suspend/resume 실험 **[미실행]**. 실제 보드에서는 `echo devices > /sys/power/pm_test; echo mem > /sys/power/state` 로 "장치 suspend까지만 하고 5초 뒤 resume" 루프를 돌리는 게 표준 레시피 [추정: pm_test 동작은 Documentation/power/basic-pm-debugging 기반].
- resume 후 볼 것: `dmesg | grep -i -E "pci|aer|link"` (restore 오류, link down), LnkSta speed/width 회귀, `power_state`, 드라이버 동작(throughput), AER 카운터.

### 3.8 dmesg 패턴 · 흔한 버그 표

| 증상 | 흔한 원인 | 확인 방법 |
|---|---|---|
| idle 후 첫 요청 latency 튐 (수십 µs–ms) | L1/L1.2 exit latency, LTR 값이 너무 큼 | policy `performance` A/B, LTR cap 값, 트레이스 타임스탬프 |
| idle 후 장치 사라짐 (`rev ff`, config 0xFFFF) | **L1.2 탈출 실패** → Recovery 실패 → Surprise Down | root port AER `SurpriseDown`, LnkSta `DLActive-`, `l1_2_aspm=0` 로 재현 사라지나 |
| AER correctable (Receiver Error, Replay Timer Timeout) ASPM 전환 때 증가 | 탈출 직후 PHY 불안정, CDR lock 늦음 | AER 카운터 vs ASPM on/off, L1 entry/exit 횟수와 상관 |
| L1.2만 켜면 hang | CLKREQ# 미연결/오픈드레인 풀업 없음, 클럭 버퍼가 CLKREQ# 무시 | 보드 회로도, 스코프로 CLKREQ#·REFCLK 측정 |
| resume 후 성능 저하 | 링크가 낮은 speed로 재트레이닝, MPS/ASPM/L1SS 복원 누락 | LnkSta 비교, L1SubCtl1 비교 before/after |
| 부팅마다 ASPM 상태가 다름 | 펌웨어(BIOS/bootloader)가 켜고 커널 policy default가 유지 | `policy`, LnkCtl 덤프, 커널 버전별 DT 기본값 |
| NVMe가 "전원 관리" 문제 | NVMe 자체 **APST**(장치 내부 power state, NVMe 명령 레벨)와 ASPM 혼동 | `nvme_core.default_ps_max_latency_us=0` 와 `pcie_aspm=off` 를 따로 A/B [추정] |

### Ethernet/MIPI 비교 (짧게)

| | PCIe | Ethernet | MIPI D-PHY / CSI-2 |
|---|---|---|---|
| 짧은 idle 절전 | ASPM L0s/L1 (HW 자동) | **EEE** (Energy-Efficient Ethernet, IEEE 802.3az) **LPI** (Low Power Idle) — PHY끼리 협상, 트래픽 없을 때 송신 정지 | 레인이 **LP mode**(저전력 저속 상태)로 내려감, 프레임 사이 blanking |
| 깊은 절전 | L1.2 (클럭·전압 off), D3 | PHY power down, WoL(Wake-on-LAN) | **ULPS** (Ultra-Low Power State) |
| 대가 | exit latency, 링크 재동기 | wake time (µs), 일부 PHY 호환성 버그로 EEE 끄는 경우 흔함 | 탈출 시퀀스 타이밍(T_WAKEUP) 어긋나면 프레임 손실 |
| Linux | `pcie_aspm`, `link/` | `ethtool --show-eee / --set-eee` | 보통 드라이버 내부 |

## 4. 꼬리 질문 체인

### 체인 A (LP00): "Latency spikes / the device disappears after idle." → 어떻게 ASPM을 배제? → trade-off? → L1.2 진입·탈출 검증?

<details><summary>Q. We see latency spikes, and sometimes the device disappears after the system has been idle. What do you suspect?</summary>

- **A**: "The word 'idle' makes me think of link power management first. When the link is quiet, ASPM puts it into L1, and with L1 substates into L1.2, where the reference clock and common-mode voltage are turned off. Coming back costs exit latency, which shows up as a spike on the first request. If the exit sequence fails — wrong T_POWER_ON, CLKREQ# not wired or not honored by the clock buffer — the link can't retrain and the device drops off the bus, which looks like 'it disappeared'. I'd check AER on the root port for Surprise Down and the Link Status DLActive bit."
- 한국어 핵심: "idle 후" = 링크 절전(ASPM L1/L1.2) 탈출 문제 1순위, 사라짐 = L1.2 exit 실패 → 링크 다운.

</details>

<details><summary>꼬리1. How do you rule ASPM in or out quickly?</summary>

- **A**: "A/B test. Boot with pcie_aspm=off, or at runtime set the pcie_aspm policy to performance, which disables ASPM and clock PM. If the symptom disappears, it's link power management. Then I bisect by state: re-enable, and turn off only L1.2 with the per-link l1_2_aspm sysfs knob, then L1.1, then L1. And I always confirm in the actual registers — LnkCtl and L1 substates control on both ends — because sysfs reflects what the kernel thinks, not necessarily what the hardware has."
- 한국어 핵심: `pcie_aspm=off`/`performance`로 A/B → 상태별(L1.2→L1.1→L1) 이분 → 레지스터 양쪽 확인.

</details>

<details><summary>꼬리2. What's the trade-off? Why not just leave ASPM off?</summary>

- **A**: "Power and thermals versus latency. On an idle NVMe or NIC, L1.2 can take link power from hundreds of milliwatts down to almost nothing, which matters for battery and for always-on domains in a car. The cost is exit latency on the first transaction and more complexity — CLKREQ# routing, LTR tuning, firmware bugs. For a latency-critical path like a sensor or a camera pipeline you might keep L1.2 off or set LTR tight; for a storage or modem link that's idle most of the time you want it on. The decision should come from measured residency and wake latency, not a blanket off."
- 한국어 핵심: 절전·발열 vs 첫 요청 지연. 실측(residency, wake latency)으로 링크별 결정.

</details>

<details><summary>꼬리3. How would you validate L1.2 entry and exit on new silicon?</summary>

- **A**: "First static checks: L1 substates capability and control registers on both ends, T_POWER_ON and common-mode restore programmed to the max of the two ports, LTR enabled, and the L1.2 threshold matching what software computed. Then entry: force idle and confirm the link actually reaches L1.2 — via the controller's LTSSM or PM debug register, residency counters if the IP has them, a scope on CLKREQ# and REFCLK, or the analyzer. Then exit stress: thousands of idle-then-traffic cycles with randomized idle times around the threshold, checking for zero link downs, zero AER uncorrectables, and bounded wake latency. Run it across temperature and voltage corners, because exit timing margins shrink at the corners."
- 한국어 핵심: 정적(레지스터 양쪽) → 진입 확인(디버그 레지스터/스코프/애널라이저) → 탈출 스트레스 N천 회(랜덤 idle) → PVT 코너.

</details>

<details><summary>꼬리4. How do you measure that the link really entered L1.2, not just that it's enabled?</summary>

- **A**: "Enabled bits only mean it's allowed. To prove entry I need an observation point: many controllers have vendor debug registers showing the current LTSSM and L1 substate, or PM event counters; DesignWare-style cores expose these in port-logic space. On the board, CLKREQ# going high and REFCLK stopping on a scope is direct evidence. A protocol analyzer shows the electrical idle and the exit. From Linux alone, runtime_suspended_time only tells you about D-states, not link substates."
- 한국어 핵심: enable ≠ 진입. 벤더 디버그 레지스터/카운터, CLKREQ#·REFCLK 스코프, 애널라이저로 증명.

</details>

### 체인 B: "Explain D-states versus link states." → D3hot이면 링크는? → D3cold에서 돌아올 때 SW가 할 일?

<details><summary>Q. What's the difference between D-states and L-states?</summary>

- **A**: "D-states are per function and controlled by software through the PM capability's control register: D0 is on, D3hot is off but still powered so config space answers, D3cold means main power is removed. L-states are per link: L0 normal, L0s and L1 low-power, L2 and L3 off. They're related — a device in D3hot puts its link in L1, D3cold means L2 or L3 — but in D0 the link can still drop to L0s or L1 by itself, and that's ASPM."
- 한국어 핵심: D = 함수, SW가 PMCSR로 / L = 링크, D0에서는 HW가 ASPM으로.

</details>

<details><summary>꼬리1. The driver puts the device into D3hot. What happens on the link?</summary>

- **A**: "When the function is programmed to D3hot, the device initiates entry into L1 with a PM_Enter_L1 DLLP handshake with the upstream port, so the link goes to L1 — PCI-PM L1, not ASPM L1. With L1 substates enabled for PCI-PM, it can go further to L1.1 or L1.2. Config accesses still work: the port wakes the link to deliver them. Memory accesses to the BARs aren't serviced in D3hot, so the driver must not touch MMIO until it brings the device back to D0, which takes 10 milliseconds per the spec."
- 한국어 핵심: D3hot → PCI-PM L1 (SW 유발), config만 응답, D0 복귀 10 ms.

</details>

<details><summary>꼬리2. D3cold versus D3hot — what does software have to do on the way back?</summary>

- **A**: "Coming from D3cold is a full power cycle: power comes back, PERST# deasserts, the link trains from Detect, and the function is in its reset state, so it's a conventional reset with the same rules — wait at least 100 ms, longer for links above 5 GT/s until the link is active, handle retry-status completions. Then restore config space — Linux does it with pci_restore_state — and the driver has to reinitialize everything behind the BARs. From D3hot, if No_Soft_Reset is set, internal state is kept, otherwise it's also a reset, just without the link retraining."
- 한국어 핵심: D3cold 복귀 = conventional reset (100 ms 규칙 + config 복원 + 드라이버 재초기화). D3hot은 No_Soft_Reset에 따라.

</details>

### 체인 C: "What's LTR and why does L1.2 need it?" → threshold 계산? → LTR을 안 보내면?

<details><summary>Q. What's LTR and why is it tied to L1.2?</summary>

- **A**: "Latency Tolerance Reporting: the endpoint sends a message upstream saying how much latency it can currently tolerate for snoop and no-snoop requests — it changes with buffer fill. L1.2 has the longest exit because the clock and common-mode have to come back, so the port only enters L1.2 when the reported tolerance is above the L1.2 threshold programmed in L1 substates control 1. That way a device that's busy and can't wait never gets put in the deep state."
- 한국어 핵심: LTR = 장치가 견딜 지연 보고. 보고값 ≥ LTR_L1.2_THRESHOLD 일 때만 L1.2.

</details>

<details><summary>꼬리1. How is the L1.2 threshold computed?</summary>

- **A**: "In Linux, the ASPM code takes the larger Common_Mode_Restore_Time and the larger T_POWER_ON of the two ports, and sets the threshold to 2 plus 4 plus those two, in microseconds — 2 for T_POWER_OFF and 4 for the minimum L1.2 residency — so you only enter L1.2 if the device can tolerate the whole round trip. It programs T_POWER_ON into both ports and disables L1.2 on both ends while changing the values, as the spec requires."
- 한국어 핵심: threshold = 2 + 4 + max(T_cm) + max(T_pwron) µs, 프로그램 중 L1.2 잠시 off.

</details>

<details><summary>꼬리2. What if the endpoint doesn't support or never sends LTR?</summary>

- **A**: "Then the L1.2 entry condition based on LTR can't be met, so in practice the link won't use ASPM L1.2 — you'd see at most L1.1. If someone forces L1.2 anyway, you risk entering a state whose exit latency the device can't tolerate, and that's where buffer overflows, dropped packets or timeouts come from. So in bring-up I check DevCap2 LTR support, DevCtl2 LTR enable along the whole path, and the max snoop and no-snoop values."
- 한국어 핵심: LTR 없으면 L1.2 사실상 불가(최대 L1.1). 경로 전체 LTR enable 확인. [추정: spec 해석]

</details>

### 체인 D: "After resume the device works but is slower / throws errors." → D3cold에서 잃는 것? → Linux가 복원 못 하는 것?

<details><summary>Q. After suspend/resume the device works, but throughput dropped and we see correctable errors. Where do you look?</summary>

- **A**: "Compare before and after. First the link: did it retrain at the same speed and width — LnkSta against LnkCap — and did the equalization complete? A resume that comes back at Gen1 or narrower explains throughput. Then the configuration that the kernel restores: max payload, ASPM and L1 substates control, LTR — if one end got restored and the other didn't, you get mismatched settings and errors. Then AER counters per type: receiver errors and replay timeouts after resume point to the PHY or equalization."
- 한국어 핵심: resume 전후 diff — LnkSta(speed/width), 복원된 config(MPS/ASPM/L1SS/LTR), AER 타입별.

</details>

<details><summary>꼬리1. What state is lost going through D3cold?</summary>

- **A**: "Everything that isn't on auxiliary power: the whole configuration space goes back to reset values — BARs, command register, MSI or MSI-X setup, capability control registers — and all the device-internal state behind the BARs, including firmware state. Sticky bits survive only if the device keeps aux power. That's why the OS saves config before and restores after, and the driver re-initializes the hardware."
- 한국어 핵심: config 전부 + 장치 내부 상태 소실, sticky는 aux 전원 있을 때만.

</details>

<details><summary>꼬리2. What does Linux restore, and what does it not?</summary>

- **A**: "pci_save_state and pci_restore_state handle the standard header — BARs, command, interrupt line — plus the PCIe capability control registers, MSI and MSI-X, and extended capabilities the core knows about, like LTR, L1 substates, AER, resizable BAR. They don't touch vendor-specific config registers or anything in MMIO space; that's the driver's resume callback. A classic bug class is a vendor register that firmware set at boot, like a PHY tuning value, that nobody restores after D3cold."
- 한국어 핵심: 표준 config + 아는 ext cap만 복원. 벤더 레지스터·MMIO는 드라이버 몫 → 누락 버그 단골.

</details>

### 체인 E (추가, bare-metal): "On bare metal, how would you enable and verify ASPM L1?"

<details><summary>Q. Your validation firmware runs bare-metal. How do you enable ASPM L1 on a link?</summary>

- **A**: "The same steps Linux does, by hand. Walk the capability list on both the root port and the endpoint via ECAM, check LnkCap ASPM support for L1 on both, compare the L1 exit latency against the endpoint's acceptable latency in DevCap, make sure common clock configuration is set consistently and retrain if I changed it, then set the ASPM control bits in LnkCtl — upstream component first when enabling, downstream first when disabling. For L1 substates I'd program T_POWER_ON and the threshold with L1.2 disabled, then enable on the upstream port before the downstream one."
- 한국어 핵심: ECAM으로 양쪽 cap 확인 → latency 비교 → CCC → LnkCtl ASPM (켤 때 upstream 먼저, 끌 때 downstream 먼저) → L1SS는 양쪽 D0에서, L1.2 끈 채 Common_Mode/threshold 먼저 쓰고 enable. [확인됨: aspm.c 주석 "ASPM L1 must be disabled in the Downstream component prior to … Upstream, and enabled in the Upstream component prior to … Downstream", "both ports must be in D0 before enabling PCI PM substates"]

</details>

<details><summary>꼬리1. How do you know it worked without Linux?</summary>

- **A**: "Read back the registers on both ends, then observe: the controller's PM or LTSSM debug register should show L1 when traffic stops, and an exit when I issue an MMIO read. I'd time that read with a cycle counter to get the wake latency distribution, and loop it a few thousand times with randomized gaps, checking the AER status registers after each batch."
- 한국어 핵심: 레지스터 read-back + 디버그 레지스터 관찰 + 사이클 카운터로 wake latency 분포 + AER 확인.

</details>

## 5. Don 경험 연결

- "At Solidigm I worked on a PCIe Gen6 SSD controller and brought up the Cortex-R and M0+ subsystems on FPGA before silicon, so link power states were part of what the firmware had to handle — the SSD side has to answer L1 and L1.2 requests and LTR correctly, and the host-side Linux behavior is exactly what this note covers. [숫자 채우기: 어떤 PM 시나리오를 FPGA/실리콘에서 검증했는지]"
- "At SK hynix, the chamber reliability platform I automated ran PCIe Gen3–5 interfaces across temperature; power-state transitions are where marginal links fail first, so a wake-from-idle loop at temperature corners is the first test I'd add for a new SoC. [숫자 채우기: chamber 온도 범위, 루프 횟수]"
- "At Apple I root-caused PCIe and SPMI issues on wireless chipsets with a DSO and a protocol analyzer — the same tools you need to prove the link actually entered L1.2 and to see CLKREQ# and REFCLK during the exit."
- 주의: Don이 직접 L1.2 버그를 잡았다는 구체 사례가 없으면 "I'd approach it this way"로 말하기 (지어내지 않기).

## 6. 실습 / 코드

### 6.1 실행한 것 (검증됨)

- [onsitePrep/lp10/aspm_dump.sh](onsitePrep/lp10/aspm_dump.sh) — POSIX sh. **실행 확인 ✅**:
  - QEMU 10.2 `virt,iommu=smmuv3` + 랩 커널 6.18.55 + BusyBox initramfs (root port, igb, nvme, xio3130 스위치, e1000e, edu) → §3.4 출력. lspci 디코드와 대조 일치.
  - Lima VM (Ubuntu 7.0.0-38, `sudo sh aspm_dump.sh 00:00.0 00:01.0 00:06.0`) → virtio 장치는 "no PCI Express capability" 정상 처리.
- QEMU 실행 방법 (VM 안, `/tmp/LP10` 스크래치; VM rootfs가 emergency read-only라 `/var/tmp` 대신 tmpfs `/tmp` 사용):

```bash
# initramfs = /var/lab/build/rootfs + Ubuntu lspci/setpci(+libs, pci.ids) + 스크립트
timeout 150 qemu-system-aarch64 -machine virt,iommu=smmuv3 -cpu cortex-a57 -m 1G -smp 2 -nographic \
  -kernel /var/lab/out/Image -initrd i.gz \
  -device pcie-root-port,id=rp1,chassis=1,slot=1 -device igb,bus=rp1 \
  -device pcie-root-port,id=rp2,chassis=2,slot=2 -device nvme,serial=lp11nvme,bus=rp2 \
  -device pcie-root-port,id=rp3,chassis=3,slot=3 -device x3130-upstream,id=up1,bus=rp3 \
  -device xio3130-downstream,id=dp1,bus=up1,chassis=4,slot=0 -device e1000e,bus=dp1 \
  -device xio3130-downstream,id=dp2,bus=up1,chassis=5,slot=1 -device edu,bus=dp2 \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/run10.sh"
```

- 한계: QEMU는 **L1 미지원, L1SS·LTR ext cap 없음, LnkCtl ASPM 비트 쓰기 불가** → L1/L1.2 동작 자체는 이 랩으로 검증 불가. 레지스터 디코드·sysfs·runtime PM·policy 전환만 실측.

### 6.2 실리콘/보드에서 돌릴 실험 [미실행]

```bash
# A/B: ASPM이 원인인가?  (실제 하드웨어에서, root 필요)
cat /sys/module/pcie_aspm/parameters/policy
echo performance > /sys/module/pcie_aspm/parameters/policy     # ASPM+ClockPM off
./workload_latency_test                                          # 증상 재현?
echo powersupersave > /sys/module/pcie_aspm/parameters/policy  # 전부 on
D=/sys/bus/pci/devices/0000:01:00.0
echo 0 > $D/link/l1_2_aspm                                      # L1.2만 끄고 재현?
sh onsitePrep/lp10/aspm_dump.sh 00:1c.0 01:00.0                 # 양쪽 레지스터 확인 (sysfs 말고)
```

### 6.3 검증 계획 (새 SoC의 PCIe 전원 관리) [미실행 설계]

| # | 테스트 | 방법 | 통과 기준 |
|---|---|---|---|
| 1 | 레지스터 정적 점검 | 양쪽 LnkCap/LnkCtl/DevCap/L1SS/LTR 덤프 → 기대표와 diff | 지원 비트·T_POWER_ON·CM restore가 설계 문서와 일치 |
| 2 | ASPM 정책 매트릭스 | policy 4종 × 장치 종류 × 링크 speed | 각 조합에서 enumeration·throughput 정상, 레지스터가 정책대로 |
| 3 | L1/L1.2 진입 확인 | idle 유도 → 벤더 디버그 레지스터/스코프(CLKREQ#, REFCLK) | 지정 시간 내 진입, residency > 목표 % |
| 4 | 탈출 스트레스 | idle(랜덤, threshold 근처 포함) → MMIO read/DMA, 1만 회+ | link down 0, AER uncorrectable 0, wake latency p99 ≤ 스펙 |
| 5 | LTR 경계 | 장치 LTR 값을 threshold 위/아래로 바꿈 | threshold 아래면 L1.2 미진입 |
| 6 | D3hot/D3cold 루프 | runtime PM `auto` + 강제 idle, 또는 suspend/resume N회 | 매 회 config 복원 diff 0, LnkSta 동일, 드라이버 정상 |
| 7 | PVT 코너 | 위 4·6을 온도/전압 코너에서 | 코너별 실패율 0, 마진 기록 |
| 8 | 성능 회귀 | resume 후 / ASPM on 상태 throughput·latency | ASPM off 대비 허용 범위 내 |
| 9 | 에러 경로 | L1.2 탈출 중 강제 실패(T_POWER_ON 축소 등 의도적 오설정) | AER/DPC로 감지되고 복구 경로 동작 (LP09) |

- residency 측정: 컨트롤러 PM 카운터가 있으면 그것, 없으면 CLKREQ# 로직 애널라이저로 duty 측정. Linux `power/runtime_suspended_time`은 **D-state residency** 이지 링크 substate가 아님.

## 7. 자주 틀리는 것

1. **"ASPM = D3"** ❌ — ASPM은 D0인 채 링크만 절전. D3는 SW가 장치를 끄는 것.
2. **sysfs `link/l1_aspm=1` 이면 켜진 것** ❌ — 커널의 의도일 뿐. LnkCtl을 양쪽에서 읽어야 함 (QEMU에서 sysfs=1, 레지스터=0 실측).
3. **한쪽만 켜도 된다** ❌ — L1/L1SS는 링크 양쪽 합의. 지원도 양쪽 LnkCap 교집합.
4. **L1.2 exit latency도 Linux가 DevCap과 비교해 막아 준다** ❌ — 비교 안 함(커널 주석). L1.2는 LTR threshold로만 걸러짐.
5. **`pcie_aspm=off` = ASPM 끔** — 정확히는 "커널이 ASPM을 **관리하지 않음**". 펌웨어가 이미 켜 놓았으면 그대로 남을 수 있다 [추정 — 플랫폼에 따라 다름]. 확실히 끄려면 policy `performance`(커널이 직접 끔).
6. **root port의 `link/` 디렉터리에서 찾기** ❌ — 링크 속성은 downstream 쪽 장치(endpoint 등)에 붙는다.
7. **runtime_status=suspended → D3** ❌ — unbound 장치는 D0 유지(실측). `power_state`와 lspci PMCSR로 확인.
8. **CLKREQ#는 SW 문제** ❌ — 보드 배선/풀업/클럭 버퍼 지원 문제가 많다. L1.2 hang은 회로도부터.
9. **D3cold 복귀 후 바로 BAR 접근** ❌ — conventional reset 규칙(100 ms, RRS 재시도) + config 복원 후에.
10. **NVMe APST와 PCIe ASPM을 같은 것으로 취급** ❌ — APST는 NVMe 장치 내부 전원 상태(명령 레벨), ASPM은 링크.

## 8. 출처

1. Linux `include/uapi/linux/pci_regs.h` (v6.18.55 소스를 랩 VM에서 grep; 온라인: https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h)
2. pciutils `ls-caps.c`, `ls-ecaps.c` (latency 표, L1PM·LTR 디코드): https://github.com/pciutils/pciutils/blob/master/ls-caps.c , https://github.com/pciutils/pciutils/blob/master/ls-ecaps.c
3. Linux `drivers/pci/pcie/aspm.c` (policy, latency check, L1.2 계산, sysfs 속성, `pcie_aspm=`): https://github.com/torvalds/linux/blob/master/drivers/pci/pcie/aspm.c
4. LKML: "PCI: qcom: Program correct T_POWER_ON value for L1.2 exit timing" (2025-11): https://lkml.iu.edu/2511.0/03636.html , "schemas: pci: Document PCIe T_POWER_ON": https://lkml.iu.edu/2511.1/01649.html
5. Linux `drivers/pci/pci.c`, `drivers/pci/pci-driver.c`, `drivers/pci/pci.h`, `drivers/pci/pcie/portdrv.c` (D3 delays, bridge D3, runtime suspend): https://github.com/torvalds/linux/tree/master/drivers/pci
6. Linux ABI `Documentation/ABI/testing/sysfs-bus-pci` (link/*, power_state, d3cold_allowed): https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-bus-pci
7. Linux PCI power management 문서: https://docs.kernel.org/power/pci.html
8. Efinix PCIe controller UG — L1.2 operation / L1 substate register programming (벤더 예시): https://www.efinixinc.com/docs-html/xcvr-pcie-controller-ug/topics/pcie/l1_2_operation.html
9. LKML "PCI/ASPM: Account for downstream device's Port Common_Mode_Restore_Time": https://lkml.iu.edu/hypermail/linux/kernel/1711.1/04305.html

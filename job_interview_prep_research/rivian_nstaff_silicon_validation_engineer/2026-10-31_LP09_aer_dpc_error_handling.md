# LP09 · 에러 처리: AER · DPC · 에러 주입 · 복구
<!-- theme: pastel -->

> **묶음**: D 인터럽트·DMA·에러 · **우선**: P1 · **선행 노트**: [LP02 config space](2026-10-31_LP02_config_space_capabilities.html), [LP07 인터럽트](2026-10-31_LP07_interrupts_intx_msi_msix.html), [LP08 DMA, IOMMU](2026-10-31_LP08_dma_iommu_coherency.html) · **다음 노트**: [LP10 전원 관리](2026-10-31_LP10_power_mgmt_aspm.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **코드**: `onsitePrep/lp09/aer_decode.c` (macOS ALL PASS), QEMU AER 주입 랩 `onsitePrep/lp07/qemu_lab/` (실제 dmesg + sysfs 카운터) · **작성**: 2026-10-31

## 0. TL;DR

- 이 노트로 답할 것: "**correctable error가 계속 올라간다 — 문제인가?**", "어떤 에러가 신호 무결성(SI)을 가리키나?", "AER 로그 한 줄씩 읽어 봐", "uncorrectable error가 나면 Linux가 뭘 하나 (드라이버 콜백, reset)?", "DPC가 뭔가?", "에러 경로 자체는 어떻게 테스트하나(주입)?"
- **외울 것 3개**
  1. **3등급**: **Correctable**(HW가 자동 복구 — 재전송, 데이터 손실 없음) / **Uncorrectable Non-Fatal**(그 트랜잭션 하나가 깨짐, 링크는 정상) / **Uncorrectable Fatal**(링크 자체를 못 믿음 → reset 필수). 등급은 **Severity 레지스터로 SW가 바꿀 수 있다**(non-fatal ↔ fatal).
  2. **SI 지표 = Physical/Data Link 계층 correctable**: **RxErr**(물리층 수신 오류) · **BadTLP / BadDLLP**(LCRC 실패 → NAK/재전송) · **Replay Timer Timeout / REPLAY_NUM Rollover**. 반대로 **UR(Unsupported Request) / Completion Timeout / Malformed TLP** 는 대개 **SW·주소·장치 FW·RTL** 쪽.
  3. **흐름**: 에러를 감지한 장치 → `ERR_COR / ERR_NONFATAL / ERR_FATAL` **메시지**를 Root Port로 → Root Port AER가 출처 BDF(Error Source ID) 기록 + 인터럽트 → Linux `aerdrv`(portdrv 서비스)가 로그·카운터(`aer_dev_*`) → uncorrectable이면 드라이버 `error_detected → (mmio_enabled) → slot_reset → resume`. **DPC**는 Fatal 시 HW가 **즉시 링크를 끊어 오염 전파를 막는** 기능.
- 레지스터 위치: AER = extended capability ID **0x0001**, 오프셋 +0x04 UncorStatus / +0x08 Mask / +0x0C Severity / +0x10 CorStatus / +0x14 CorMask / +0x18 Cap&Ctl(FEP) / +0x1C Header Log(16 B) / +0x2C Root Cmd / +0x30 Root Status / +0x34 Error Source ID (`pci_regs.h` 확인 [확인됨]).
- 이번 노트의 강점: QEMU 랩에서 **실제로 에러를 주입**해서 커널 6.18의 진짜 AER 로그, 복구(`Root Port link has been reset` → `device recovery successful`), sysfs 카운터 증가를 캡처했다.
- **영어 30초 (headline: "We see correctable errors climbing. Is that a problem?")**: "Correctable means the hardware recovered — usually a link-level replay — so no data was lost, but it costs bandwidth and it's an early warning. I'd look at which bits: receiver errors, bad TLP or bad DLLP, and replay timeouts point to signal integrity — channel loss, equalization, a marginal connector or reference clock. Then I care about the rate, not the count: errors per hour, per lane if the device reports lane errors, against a baseline from a known-good board, and how it moves with temperature and voltage. A steady low rate at the corners can be acceptable; a rate that climbs with temperature, or any uncorrectable error, is a failure to root-cause."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 택배 배송 사고 보고 체계

- PCIe 링크 = 고속도로, TLP = 택배 상자.
- **Correctable (경미한 사고)**: 상자에 붙은 체크섬(LCRC)이 안 맞음 → 받는 쪽이 "다시 보내" (NAK) → **보내는 쪽이 자동 재전송**. 고객은 모른다. 하지만 **자주 일어나면 도로(신호)가 나쁘다는 신호**.
- **Uncorrectable Non-Fatal (이 택배만 사고)**: "없는 주소로 보냈음(Unsupported Request)", "답장이 안 옴(Completion Timeout)" → **그 트랜잭션 하나만 실패**, 도로는 멀쩡.
- **Uncorrectable Fatal (도로 붕괴)**: 상자 형식이 아예 엉망(Malformed TLP), 링크가 갑자기 끊김(Surprise Down), 링크 계층 규칙 위반(DLP) → **이 도로를 더는 못 믿음 → 도로 리셋**.
- **AER (Advanced Error Reporting)** = 표준 **사고 보고서 양식**(레지스터들) + **본부(Root Port)에 무전**(ERR_* 메시지)하는 체계.
- **DPC (Downstream Port Containment)** = 큰 사고가 나면 **진입로 차단기를 즉시 내리는 것**. 깨진 데이터가 시스템 안쪽으로 더 퍼지기 전에 HW가 링크를 끊는다.
- **에러 주입** = **소방 훈련**. 진짜 사고는 드물어서, 일부러 가짜 사고를 내서 보고·복구 절차가 동작하는지 확인.

### 1.2 그림: 누가 감지하고, 누가 듣고, 누가 처리하나

```text
  Endpoint (예: NVMe)                         Root Port (RC 안)                    Linux
  ┌──────────────────────┐   ERR_COR /       ┌────────────────────────┐   MSI/   ┌───────────────────────────┐
  │ PHY: RxErr 감지      │   ERR_NONFATAL /  │ AER cap:               │   MSI-X  │ portdrv "aer" 서비스        │
  │ DLL: BadTLP, Replay  │ ─ ERR_FATAL ────► │  Root Error Status 세트 │ ───────► │ aer_isr → 장치들 AER 읽기   │
  │ TL : UR, CTO, Malf   │   (Message TLP)   │  Error Source ID = BDF  │  (IRQ24) │ dmesg 출력 + sysfs 카운터   │
  │ AER cap에 비트 기록   │                   │  + 자기 자신 에러도     │          │ uncorr → err.c 복구 절차    │
  └──────────────────────┘                   └────────────────────────┘          │  error_detected → reset →  │
           ▲                                          │ (DPC 있으면 Fatal 시       │  slot_reset → resume        │
           └──────── link ────────────────────────────┘  HW가 즉시 link disable)  └───────────────────────────┘
```

- 핵심: **에러는 '감지한 쪽'이 보고**한다. 링크 품질 문제면 **받은 쪽(Receiver)** 이 보고 → 범인 endpoint는 깨끗하고 **Root Port 카운터가 오르는** 일이 흔하다 (sysfs ABI 문서도 이 점을 명시 [3]).

### 1.3 그림: 프로토콜 계층별로 무엇이 잡히나

```text
 Transaction Layer : UR, Completion Timeout, Completer Abort, Unexpected Cpl, Malformed TLP, Poisoned TLP, ECRC, ACS Violation, Rx Overflow, FCP
 Data Link Layer   : Bad TLP (LCRC), Bad DLLP, Replay Timer Timeout, REPLAY_NUM Rollover, DLP(시퀀스/ACK 규칙 위반), Surprise Down
 Physical Layer    : Receiver Error (심볼/블록 인코딩, framing, deskew …)
```

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| AER | Advanced Error Reporting | 에러 종류별 상태/마스크/등급 레지스터 + 보고 체계 (ext cap 0x0001) | PCIe 에러 관측의 표준 |
| baseline error reporting | — | AER 없이도 있는 최소 보고 (Device Status의 CED/NFED/FED/URD) | AER 없는 장치 대비 |
| Correctable (COR) | — | HW가 자동 복구한 에러 | SI 조기 경보 |
| Uncorrectable Non-Fatal | — | 트랜잭션 하나 실패, 링크 정상 | SW/주소/FW 버그 의심 |
| Uncorrectable Fatal | — | 링크를 믿을 수 없음 → reset | 반드시 root-cause |
| Severity | Uncorrectable Error Severity reg | 각 uncorrectable 비트를 Fatal(1)/Non-Fatal(0)로 지정 | 플랫폼 정책 |
| Mask | Error Mask reg | 1이면 그 에러를 **보고하지 않음**(status는 남을 수 있음) | 숨겨진 에러 주의 |
| ERR_COR / ERR_NONFATAL / ERR_FATAL | Error Messages | 에러를 Root로 알리는 Message TLP | Root Port가 수집 |
| Root Port | — | RC의 PCIe 포트, 에러 수집 지점 | AER 인터럽트 발생 |
| RCEC | Root Complex Event Collector | RC 내장 장치(RCiEP)의 에러 수집기 | SoC 내장 PCIe 기능 |
| Error Source ID | Root Port AER +0x34 | 마지막 ERR_COR / ERR_(NON)FATAL을 보낸 BDF | 범인 찾기 |
| FEP | First Error Pointer | 여러 비트가 섰을 때 **처음** 난 에러 번호 | 연쇄 에러 원인 |
| Header Log | — | 에러 일으킨 TLP 헤더 16 B 사본 | 어떤 요청이었나 (주소, BDF) |
| RxErr | Receiver Error | 물리층 수신 오류 | **SI 1순위 지표** |
| Bad TLP / Bad DLLP | — | 링크 계층 CRC 실패 → NAK | SI |
| Replay Timer Timeout | — | ACK가 제때 안 와서 재전송 | SI 또는 상대 지연(ASPM 등) |
| REPLAY_NUM Rollover | — | 같은 TLP 재전송 4번 연속 → 재학습 유발 | 심각한 링크 문제 |
| UR | Unsupported Request | 받은 쪽이 처리 못 하는 요청 (없는 주소, 꺼진 기능) | 대개 SW 버그/열거 중 정상 |
| CTO | Completion Timeout | 읽기 요청의 응답이 시간 내 안 옴 | 장치 hang, 링크 다운 |
| CA | Completer Abort | 응답자가 거부 | 장치 FW/HW |
| Malformed TLP | — | TLP 형식 위반 (길이, MPS 초과 등) | RTL/IP 버그, MPS 설정 불일치 |
| Poisoned TLP | — | 데이터가 오염됐다고 표시된 TLP (EP bit) | 상류 ECC 에러 전파 |
| ECRC | End-to-end CRC | TLP 끝단간 CRC (옵션) | 스위치 내부 손상 |
| Surprise Down | — | 예고 없이 링크가 끊김 | 전원/PERST/SI/hot-unplug |
| ANFE | Advisory Non-Fatal Error | 원래 uncorr인데 상황상 correctable로 **강등 보고** | UR on probe 등 |
| DPC | Downstream Port Containment | Fatal 시 HW가 그 포트 아래 링크를 즉시 끊음 (ext cap 0x1D) | 오염 전파 차단 |
| EDR | Error Disconnect Recover | ACPI 펌웨어가 DPC를 처리하고 OS에 알리는 방식 | firmware-first 서버 |
| firmware-first / native | — | AER를 펌웨어(APEI GHES)가 먼저 / OS가 직접 처리 | `_OSC` 협상 결과 |
| `_OSC` | ACPI Operating System Capabilities | OS↔펌웨어가 AER/DPC 등 제어권을 협상 | Linux가 AER 못 쓸 수 있음 |
| `pci_error_handlers` | — | 드라이버의 복구 콜백 묶음 | 복구 품질 |
| SBR / FLR | Secondary Bus Reset / Function Level Reset | 포트 아래 전체 리셋 / 기능 하나 리셋 | AER 복구 수단 (LP11) |
| aer-inject / EINJ | — | SW 에러 주입 모듈 / ACPI 펌웨어 에러 주입 | 에러 경로 검증 |
| FEC / Flit | Forward Error Correction / Flit mode | Gen6의 고정 크기 블록 + 오류정정 | Gen6에선 correctable 해석이 달라짐 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Baseline 보고 (AER 없어도 있는 것)

PCIe Capability 안 (`pci_regs.h` [1]):

| 레지스터 | 비트 | 의미 |
|---|---|---|
| Device Control (+0x08) | 0 CERE · 1 NFERE · 2 FERE · 3 URRE | Correctable / Non-Fatal / Fatal / UR **보고 enable**. 꺼져 있으면 장치는 ERR_* 메시지를 **안 보낸다** |
| Device Status (+0x0A) | 0 CED · 1 NFED · 2 FED · 3 URD | 감지됨(Detected) 플래그 (W1C) |
| Root Control (+0x1C) | 0 SECEE · 1 SENFEE · 2 SEFEE | Root Port가 받은 에러를 **System Error**로도 올릴지 |

- Linux 6.18 `pci_aer_init()`은 AER가 native일 때 장치 DevCtl에 CERE|NFERE|FERE|URRE를 켠다 [확인됨: 소스]. **랩에서 실제 겪은 함정**: QEMU e1000e는 DevCtl이 쓰기 불가(0x0000으로 남음) → 장치에 주입한 에러는 **메시지가 안 나가서 Root Port에 아무것도 안 보였다**. 그래서 Root Port 자체에 주입했다. 실 HW에서도 "에러 보고 enable 비트가 꺼져 있으면 AER가 조용하다"는 같은 원리.

### 2.2 에러 분류표 — 무엇이, 어느 계층에서, 무엇을 의심하게 하나

| 에러 (AER 비트) | 기본 등급 | 감지 계층 | 보고 주체(Linux agent) | 흔한 원인 / 의미 |
|---|---|---|---|---|
| Receiver Error (COR b0) | Correctable | Physical | Receiver | **SI**: 채널 손실, EQ 불충분, refclk jitter, 커넥터/리타이머, 전원 노이즈 |
| Bad TLP (COR b6) | Correctable | Data Link | Receiver | **SI** (LCRC 불일치) → NAK + replay |
| Bad DLLP (COR b7) | Correctable | Data Link | Receiver | **SI** (DLLP CRC) |
| REPLAY_NUM Rollover (COR b8) | Correctable | Data Link | Transmitter | 재전송 4회 연속 실패 → 링크 retrain. **심각한 SI** 또는 상대 장치 무응답 |
| Replay Timer Timeout (COR b12) | Correctable | Data Link | Transmitter | ACK 지연: SI(ACK DLLP 깨짐), 상대 지연(ASPM L1 exit, 혼잡), timer 설정 |
| Advisory Non-Fatal (COR b13) | Correctable | — | — | uncorr 에러가 "advisory"로 강등 (예: 열거 중 UR) — 대개 정상 |
| Corrected Internal (COR b14) | Correctable | General | Component | 장치 내부 ECC 정정 |
| Header Log Overflow (COR b15) | Correctable | General | Component | 에러가 너무 많아 헤더 기록 못 함 |
| Data Link Protocol (UNC b4) | **Fatal** | Data Link | Receiver | DLL 규칙 위반(ACK/NAK 시퀀스) — 컨트롤러 RTL 버그 또는 극심한 SI |
| Surprise Down (UNC b5) | **Fatal** | Data Link | Component | 링크가 예고 없이 Down: 장치 전원/리셋, PERST#, hot-unplug, SI |
| Poisoned TLP (UNC b12) | Non-Fatal | Transaction | Receiver | 상류 데이터가 이미 오염(메모리 ECC 등) — 원인은 다른 곳 |
| Flow Control Protocol (UNC b13) | **Fatal** | Transaction | Receiver | credit 규칙 위반 — RTL/FW 버그 |
| Completion Timeout (UNC b14) | Non-Fatal | Transaction | **Requester** | 응답 안 옴: 장치 hang, 링크 다운, 주소가 inbound 창 밖, timeout 값 너무 짧음 |
| Completer Abort (UNC b15) | Non-Fatal | Transaction | **Completer** | 응답자가 거부 (장치 내부 에러, 보안 정책) |
| Unexpected Completion (UNC b16) | Non-Fatal | Transaction | Receiver | 요청한 적 없는 completion (tag/BDF 꼬임, 라우팅 버그) |
| Receiver Overflow (UNC b17) | **Fatal** | Transaction | Receiver | 상대가 credit 무시 — RTL 버그 |
| Malformed TLP (UNC b18) | **Fatal** | Transaction | Receiver | 형식 위반: **MPS 설정 불일치**, 길이/필드 오류 → RTL/IP 또는 설정 버그 |
| ECRC Error (UNC b19) | Non-Fatal | Transaction | Receiver | 끝단간 CRC — 스위치/브리지 내부 손상 |
| Unsupported Request (UNC b20) | Non-Fatal | Transaction | Receiver | **SW**: 매핑 안 된 주소, 꺼진 BAR/기능, 없는 장치 config 접근 |
| ACS Violation (UNC b21) | Non-Fatal | Transaction | Receiver | P2P/격리 정책 위반 |
| Uncorrectable Internal (UNC b22) | **Fatal** | General | Component | 장치 내부 비정정 에러 (기본 masked) |

- 등급 기본값 근거: 실측 QEMU Root Port의 Severity 레지스터 `0x00462030` = **DLP, SDES, FCP, RxOF, MalfTLP, UncorrIntErr** 가 Fatal (§6.1 디코더 출력) — 위 표의 Fatal 목록과 일치 [확인됨].
- 비트 22–31(MC Blocked, AtomicOp/TLP Prefix/Poisoned/DMWr Egress Blocked, IDE Check, Misrouted IDE, PCRC, Translation Egress Blocked)은 최근 스펙 추가분 — 6.18/master `pci_regs.h`에 정의돼 있다 [확인됨].
- "agent" 열은 Linux `aer.c`의 `AER_GET_AGENT` 매핑 (Requester = CmpltTO, Completer = CmpltAbrt, Transmitter = Replay 계열, Component = 내부/SurpriseDown, 나머지 Receiver) [2].

### 2.3 AER extended capability 레지스터 맵 (`pci_regs.h` 대조 [확인됨])

```text
 AER cap base (ext cap ID 0x0001, 보통 0x100)
 +0x00  Ext Cap Header: [15:0] ID=0x0001 [19:16] Version [31:20] Next
 +0x04  Uncorrectable Error Status    (PCI_ERR_UNCOR_STATUS)  RW1C
 +0x08  Uncorrectable Error Mask      (PCI_ERR_UNCOR_MASK)    1 = 보고 안 함
 +0x0C  Uncorrectable Error Severity  (PCI_ERR_UNCOR_SEVER)   1 = Fatal, 0 = Non-Fatal
 +0x10  Correctable Error Status      (PCI_ERR_COR_STATUS)    RW1C
 +0x14  Correctable Error Mask        (PCI_ERR_COR_MASK)
 +0x18  Advanced Error Capabilities and Control (PCI_ERR_CAP)
          [4:0] First Error Pointer (PCI_ERR_CAP_FEP)
          b5 ECRC Gen Capable · b6 ECRC Gen Enable · b7 ECRC Check Capable · b8 ECRC Check Enable
          b11 TLP Prefix Log Present · b12 Completion Timeout Prefix/Header Log Capable
          b18 TLP logged in Flit Mode · [23:19] Logged TLP Size (Flit mode)   ← Gen6
 +0x1C  Header Log (16 B, TLP 헤더 DW0–DW3)
 ---- 아래는 Root Port / RCEC 만 ----
 +0x2C  Root Error Command:  b0 COR_EN · b1 NONFATAL_EN · b2 FATAL_EN   (AER 인터럽트 enable)
 +0x30  Root Error Status:   b0 ERR_COR Rcvd · b1 Multiple ERR_COR · b2 ERR_FATAL/NONFATAL Rcvd
                             b3 Multiple FATAL/NONFATAL · b4 First Uncorrectable Fatal
                             b5 Non-Fatal Msg Rcvd · b6 Fatal Msg Rcvd · [31:27] AER Interrupt Message Number
 +0x34  Error Source Identification: [15:0] ERR_COR Source BDF · [31:16] ERR_FATAL/NONFATAL Source BDF
 +0x38  TLP Prefix Log (최대 16 B)
```

**실측 bytes (QEMU pcie-root-port 00:01.0, config 0x100–0x12F)**:

```text
000100 01 00 82 14 | 00 00 00 00 | 00 00 40 02 | 30 20 46 00
       hdr: ID 0001, ver 2, next 0x148   UncSta 0   UncMask 0x02400000   UncSev 0x00462030
000110 00 00 00 00 | 00 e0 00 00 | a0 02 00 00 | 00 00 00 00
       CorSta 0      CorMask 0x0000e000  Cap&Ctl 0x2a0 (ECRC gen/chk capable, 미사용)
000120 00 00 00 00 00 00 00 00 00 00 00 00 | 07 00 00 00
                                             Root Error Command = 7 (COR/NONFATAL/FATAL 인터럽트 모두 on)
```

- CorMask `0xe000` = ANFE·CorrIntErr·HeaderOF masked (스펙 기본값). UncMask `0x02400000` = UncorrIntErr(b22) + TLP Prefix Blocked(b25) masked (b22는 스펙 기본 masked, b25는 이 환경에서 관측된 값 [추정: 커널/QEMU 중 누가 세팅했는지 미확인]).
- next `0x148` → ACS(0x0D) ext cap이 이어짐.

### 2.4 메시지 흐름과 Linux AER 드라이버

- 장치가 에러 감지 → status 비트 set → (mask 아니고, DevCtl 보고 enable이면) 등급에 맞는 **ERR_COR / ERR_NONFATAL / ERR_FATAL Message TLP** 를 upstream으로. 스위치는 그대로 위로 전달.
- Root Port: Root Error Status 비트, Error Source ID에 BDF 기록 → Root Error Command가 켜져 있으면 **인터럽트**(MSI/MSI-X). 실측 `/proc/interrupts`: `24: 0 5 ITS-PCI-MSIX-0000:00:01.0 0 Edge PCIe PME, aerdrv, PCIe bwctrl` — 주입 5건에 카운트 5 [확인됨].
- Linux 구조: `CONFIG_PCIEPORTBUS`의 **portdrv**가 Root Port에 붙고, 그 서비스로 `aer`(drivers/pci/pcie/aer.c), `dpc`, `pme`, hotplug, bwctrl. dmesg `pcieport 0000:00:01.0: AER: enabled with IRQ 24` [확인됨].
- ISR → 워커에서 Error Source ID로 장치 찾기 → 그 장치(그리고 같은 버스 장치들)의 AER status 읽기 → 출력/카운터 → status clear → uncorrectable이면 **복구(err.c `pcie_do_recovery`)**.
- **누가 AER를 갖나 (ACPI `_OSC`)**: 실측 Lima VM(Apple VZ):

```text
acpi PNP0A08:00: _OSC: OS requested [PCIeHotplug SHPCHotplug PME AER PCIeCapability LTR DPC]
acpi PNP0A08:00: _OSC: platform willing to grant [PCIeHotplug SHPCHotplug PME AER PCIeCapability LTR DPC]
acpi PNP0A08:00: _OSC: platform retains control of PCIe features (AE_ERROR)
```

  - 해석: OS가 AER 제어권을 요청했지만 협상이 에러로 끝나 **펌웨어가 제어권 유지** → Linux native AER는 동작 안 함(펌웨어 우선 또는 무처리). x86 서버에서 "AER 로그가 안 나온다"의 단골 원인. DT 기반 ARM SoC는 `_OSC`가 없어 **native**(QEMU 랩이 그 경우).
- 부트 옵션: `pci=noaer`(AER 끔 — 로그 폭주 회피용 땜질, 원인 해결 아님), `pcie_ports=native`(펌웨어 무시하고 강제, 위험).
- **ratelimit** (aer.c, 6.x): correctable·non-fatal 로그는 장치·종류별 **5초에 10건**(`DEFAULT_RATELIMIT_*`), Fatal/DPC는 제한 없음. sysfs `aer/correctable_ratelimit_*`로 조정 [3]. → **dmesg 줄 수로 에러율을 세면 틀린다. 카운터를 써라.**
- tracepoint `ras:aer_event` (`trace_aer_event`, aer.c) → `rasdaemon`이 DB로 수집 [확인됨: 소스의 trace 호출].

### 2.5 드라이버 복구 콜백 (`struct pci_error_handlers`, include/linux/pci.h [확인됨])

```c
struct pci_error_handlers {
        pci_ers_result_t (*error_detected)(struct pci_dev *dev, pci_channel_state_t error);
        pci_ers_result_t (*mmio_enabled)(struct pci_dev *dev);   /* MMIO 재허용, DMA는 아직 */
        pci_ers_result_t (*slot_reset)(struct pci_dev *dev);     /* 리셋 후 재초기화 */
        void (*reset_prepare)(struct pci_dev *dev);              /* FLR 등 리셋 전/후 */
        void (*reset_done)(struct pci_dev *dev);
        void (*resume)(struct pci_dev *dev);                     /* 정상 동작 재개 */
        void (*cor_error_detected)(struct pci_dev *dev);         /* correctable 상세 기록용 */
};
```

```text
 channel state:  pci_channel_io_normal (non-fatal: I/O 가능) / io_frozen (fatal: I/O 막힘) / io_perm_failure (죽음)
 return:         PCI_ERS_RESULT_CAN_RECOVER · NEED_RESET · DISCONNECT · RECOVERED · NONE

 error_detected(dev, state) ──► 모든 드라이버 CAN_RECOVER? ──yes──► mmio_enabled() ──► RECOVERED? ──► resume()
          │                                 │ no (누구든 NEED_RESET)             │ NEED_RESET
          │                                 ▼                                    ▼
          │                        reset (상위 포트 SBR, RCiEP는 FLR)  ──► slot_reset() ──► RECOVERED ──► resume()
          └─ DISCONNECT ──► 장치 포기 (remove, "device recovery failed")
```

- 커널 문서 규칙 [4][5]: **Non-Fatal** → `error_detected(…, io_normal)`; 모두 CAN_RECOVER면 reset 생략, 한 명이라도 NEED_RESET이면 reset. **Fatal** → `io_frozen`, CAN_RECOVER를 돌려줘도 **reset은 무조건**("fatal은 opt-out 불가").
- `err_handler`가 없는 드라이버의 장치는 **복구되지 않는다**(문서 FAQ) — 검증 체크리스트 항목.
- 실측 복구 로그 (Root Port에 Non-Fatal UR 주입 → 아래 e1000e까지 복구):

```text
[14.677441] pcieport 0000:00:01.0: AER: Uncorrectable (Non-Fatal) error message received from 0000:00:01.0
 ... (§3.1)
[14.848978] pcieport 0000:00:01.0: AER: Root Port link has been reset (0)
[14.849757] e1000e 0000:01:00.0: enabling device (0000 -> 0002)
[14.930944] pcieport 0000:00:01.0: AER: device recovery successful
[15.421510] e1000e 0000:01:00.0 eth0: NIC Link is Up 1000 Mbps Full Duplex, Flow Control: Rx/Tx
```

  - `Root Port link has been reset` = SBR 수행 (e1000e 드라이버가 NEED_RESET을 돌려준 것 [추정: e1000e `e1000_io_error_detected` 동작]). `enabling device` = `slot_reset`에서 재-enable. `device recovery successful` = err.c 최종 결과. 마지막 줄 = `resume` 후 링크 업.

### 2.6 DPC — Downstream Port Containment (ext cap 0x1D)

- **왜**: AER는 "보고 후 SW가 처리". 그 사이 오염된 데이터(예: Poisoned/잘못된 completion)가 더 퍼질 수 있다. DPC는 **Downstream Port(Root Port/Switch DSP)가 Fatal(옵션 Non-Fatal) 에러를 보면 HW가 즉시 링크를 Disabled로** 만들어 아래쪽 트래픽을 차단한다. 그 동안 위로 가는 요청은 UR/CA로 응답 → 시스템이 멈추지 않음.
- 레지스터 (`pci_regs.h` [확인됨]): DPC Capability(+0x04: 인터럽트 번호, RP Extensions, Poisoned TLP blocking, **SW Triggering 지원**), DPC Control(+0x06: b0 `EN_FATAL`, b1 `EN_NONFATAL`, b3 `INT_EN`), DPC Status(+0x08: b0 **Trigger Status**, [2:1] **Trigger Reason** = 0 unmasked uncorrectable / 1 ERR_NONFATAL 수신 / 2 ERR_FATAL 수신 / 3 확장(RP PIO, SW trigger), b4 RP Busy), DPC Source ID(+0x0A).
- 복구 흐름 (Linux `dpc.c` + `err.c`): DPC 트리거 → IRQ → `containment event, status:… ERR_FATAL received from 0000:01:00.0` 같은 로그 → 드라이버 `error_detected(io_frozen)` → 링크 다운 대기 → **Trigger Status를 SW가 clear → 링크 재학습** → `slot_reset` → `resume`. 링크가 안 돌아오면 장치 제거.
- dpc.c 메시지 형식 (6.18 소스 [확인됨]): `containment event, status:%#06x, %s received from %04x:%02x:%02x.%d`, `enabled with IRQ %d`, `error containment capabilities: Int Msg #%d, RPExt%c PoisonedTLP%c SwTrigger%c RP PIO Log %d, DL_ActiveErr%c`.
- Hotplug와의 관계: DPC는 link down을 유발하므로 hotplug 드라이버가 "장치 제거"로 오해하지 않도록 커널이 조정 (Surprise Down + DPC 조합은 NVMe 핫스왑 서버에서 흔함).
- 이번 랩: QEMU `pcie-root-port`에 DPC capability가 없고, Lima Ubuntu 커널엔 `CONFIG_PCIE_DPC=y`지만 VZ 장치는 conventional PCI라 DPC 미관측 → **DPC 로그는 미실측** [미검증].
- **EDR** (`CONFIG_PCIE_EDR`, edr.c): ACPI 시스템에서 펌웨어가 DPC를 소유할 때, 펌웨어가 OS에 "EDR event"를 보내 OS가 복구를 수행하는 방식. 로그 `EDR event received`. Lima `_OSC: OS supports [... EDR ...]` 에서 지원 선언 확인 [확인됨].

### 2.7 Gen6 메모: FEC와 Flit mode

- PCIe 6.0(64 GT/s)은 **PAM4** 신호 → 원시 BER이 크게 나빠짐 → **Flit mode**(고정 256 B 단위) + **경량 FEC**(Forward Error Correction) + 강한 CRC + 재전송 조합 [추정: 수치 세부는 스펙 원문 미확인].
- 검증 의미: FEC가 **보이지 않게 고친 에러는 AER RxErr로 안 올라올 수 있다** → Gen6에선 AER correctable 0이어도 마진이 충분하다는 뜻이 아님. FEC 정정 카운터 / Flit 오류 카운터(벤더·Gen6 ext cap)와 **Lane Margining**을 같이 봐야 한다 [추정].
- 커널 흔적: `PCI_EXP_FLAGS_FLIT`(Flit Mode Supported), `PCI_EXP_LNKSTA2_FLIT`(Flit Mode Status), AER `PCI_ERR_CAP_TLP_LOG_FLIT`(헤더가 Flit 형식으로 기록됨), `PCI_EXT_CAP_ID_PL_64GT` 0x31 [확인됨: pci_regs.h].
- Don 연결: Solidigm Gen6 컨트롤러 경험 → 면접 가산점 포인트.

## 3. Linux에서 볼 것

### 3.1 dmesg AER 로그 — 실제 출력, 줄별 해석 (QEMU 랩, kernel 6.18.55)

QEMU 모니터에서 Root Port(00:01.0)에 주입: `pcie_aer_inject_error -c rp1 0x1`(Correctable RxErr), `-c rp1 0x40`×2(BadTLP), `rp1 0x100000`(UR, Non-Fatal), `rp1 0x40000`(MalfTLP, Fatal). 모니터 응답 `OK id: rp1 root bus: 0000:00, bus: 0 devfn: 1.0`.

```text
[ 4.263643] pcieport 0000:00:01.0: AER: Correctable error message received from 0000:00:01.0
[ 4.264540] pcieport 0000:00:01.0: PCIe Bus Error: severity=Correctable, type=Physical Layer, (Receiver ID)
[ 4.264660] pcieport 0000:00:01.0:   device [1b36:000c] error status/mask=00000001/0000e000
[ 4.264821] pcieport 0000:00:01.0:    [ 0] RxErr                  (First)
[ 7.404312] pcieport 0000:00:01.0: PCIe Bus Error: severity=Correctable, type=Data Link Layer, (Receiver ID)
[ 7.404318] pcieport 0000:00:01.0:   device [1b36:000c] error status/mask=00000040/0000e000
[ 7.404325] pcieport 0000:00:01.0:    [ 6] BadTLP
[14.677441] pcieport 0000:00:01.0: AER: Uncorrectable (Non-Fatal) error message received from 0000:00:01.0
[14.678216] pcieport 0000:00:01.0: PCIe Bus Error: severity=Uncorrectable (Non-Fatal), type=Transaction Layer, (Receiver ID)
[14.678273] pcieport 0000:00:01.0:   device [1b36:000c] error status/mask=00100000/02400000
[14.678314] pcieport 0000:00:01.0:    [20] UnsupReq               (First)
[14.678627] pcieport 0000:00:01.0: AER:   TLP Header: 0x00000000 0x00000000 0x00000000 0x00000000
[23.822189] pcieport 0000:00:01.0: AER: Uncorrectable (Fatal) error message received from 0000:00:01.0
[23.822349] pcieport 0000:00:01.0: PCIe Bus Error: severity=Uncorrectable (Fatal), type=Transaction Layer, (Receiver ID)
[23.822366] pcieport 0000:00:01.0:   device [1b36:000c] error status/mask=00040000/02400000
[23.822383] pcieport 0000:00:01.0:    [18] MalfTLP                (First)
[23.977101] pcieport 0000:00:01.0: AER: Root Port link has been reset (0)
[24.053639] pcieport 0000:00:01.0: AER: device recovery successful
```

| 부분 | 해석 |
|---|---|
| `pcieport 0000:00:01.0:` (줄 앞) | 로그를 찍는 장치 = AER 정보를 읽은 장치. 여기선 Root Port 자신 |
| `AER: Correctable error message received from 0000:00:01.0` | Root Port가 받은 ERR_COR의 **Error Source ID** (보고자 BDF). 실제 장애에선 여기 endpoint BDF가 나옴 |
| `severity=Correctable` | 등급 (Non-Fatal / Fatal은 "Uncorrectable (…)") |
| `type=Physical Layer` | 비트 → 계층 (aer.c `AER_GET_LAYER_ERROR`) |
| `(Receiver ID)` | 그 에러의 **agent** — 누가 감지했나 (Requester/Completer/Transmitter/Receiver) |
| `device [1b36:000c]` | vendor:device (QEMU root port) |
| `error status/mask=00000001/0000e000` | 해당 status 레지스터 / mask 레지스터 원값 → §6.1 디코더로 풀 수 있음 |
| `[ 0] RxErr  (First)` | set된 비트 번호와 이름. **(First)** = First Error Pointer가 가리키는 비트 (uncorr에서 의미) |
| `TLP Header: 0x… ×4` | Header Log 16 B. 주입이라 0. 실제 UR이면 **어떤 주소/BDF로 간 요청인지**가 여기 |
| `Root Port link has been reset (0)` | 복구 중 SBR 수행 |
| `device recovery successful` | `pcie_do_recovery` 성공 |

**실세계 예 — Jetson Orin NX + Samsung NVMe (kernel 5.10, NVIDIA 포럼 [6])**

```text
pcieport 0004:00:00.0: AER: Corrected error received: 0004:01:00.0
nvme 0004:01:00.0: PCIe Bus Error: severity=Corrected, type=Data Link Layer, (Receiver ID)
nvme 0004:01:00.0: device [144d:a80a] error status/mask=00000040/0000e000
nvme 0004:01:00.0: [ 6] BadTLP
```

- 해석: 도메인 0004 Root Port가 **NVMe(01:00.0)로부터** ERR_COR 수신, NVMe 쪽 수신기가 LCRC 깨진 TLP를 받음(**host→device 방향 신호 품질** 의심), mask `e000` = 기본값. 5.10은 "Corrected", 6.x는 "Correctable"로 문구가 다르다.
- 커널 문서 예시 [4] (Fatal UR + TLP 헤더): `[20] UnsupReq (First)` + `TLP Header: 0x04000001 0x00200a03 0x05010000 0x00050100` → §6.1 디코더: **CfgRd0, requester 00:04.0, target 05:00.1 reg 0x000** = "00:04.0이 05:00.1의 config를 읽었는데 UR" (없는 장치 열거 같은 상황).
- **문구 변화 주의**: 현재 master `aer.c`는 비트 줄을 `[%2d] %-17s | %-11s | %-17s` (이름 · agent · layer)로 출력하도록 바뀌어 있다 [확인됨: master 소스]. 머지 버전은 미확인 [추정: 6.19/7.x]. 로그 파서를 만든다면 **두 형식 모두** 처리.

### 3.2 sysfs 카운터 — 실제 출력 (주입 전 → 후)

```text
[before] 0000:00:01.0/aer_dev_correctable: RxErr 0,BadTLP 0,BadDLLP 0,Rollover 0,Timeout 0,NonFatalErr 0,CorrIntErr 0,HeaderOF 0,TOTAL_ERR_COR 0
[after ] 0000:00:01.0/aer_dev_correctable: RxErr 1,BadTLP 2,BadDLLP 0,Rollover 0,Timeout 0,NonFatalErr 0,CorrIntErr 0,HeaderOF 0,TOTAL_ERR_COR 3
[after ] 0000:00:01.0/aer_dev_nonfatal: ... UnsupReq 1 ... TOTAL_ERR_NONFATAL 1
[after ] 0000:00:01.0/aer_dev_fatal:    ... MalfTLP 1 ... TOTAL_ERR_FATAL 1
[after ] 0000:00:01.0/aer_rootport_total_err_cor: 3
[after ] 0000:00:01.0/aer_rootport_total_err_nonfatal: 1
[after ] 0000:00:01.0/aer_rootport_total_err_fatal: 1
[after ] 0000:01:00.0/aer_dev_correctable: ... TOTAL_ERR_COR 0          ← 아래 e1000e는 0 (보고자가 아님)
```

- `aer_dev_*` (모든 AER 장치): **그 장치가 보고한** 에러별 카운트. `aer_rootport_total_err_*` (Root Port/RCEC만): **그 포트로 들어온 ERR_* 메시지 총수**(하위 전체 + 자기 자신).
- ABI 주의 [3]: 메시지 하나에 여러 비트가 실릴 수 있어 `TOTAL_*`가 개별 합과 다를 수 있음. 그리고 문제 장치가 아니라 **링크 상대(Root Port)의 카운터가 오를 수 있음**.
- 카운터는 **ratelimit과 무관하게** 증가 → 검증 자동화는 dmesg가 아니라 이 파일을 주기적으로 스냅샷/diff.
- 이름 주의: 실제 파일 내용은 aer.c 짧은 이름(`RxErr`, `Rollover` …)이다. ABI 문서의 예시 문자열("Receiver Error", "RELAY_NUM Rollover")은 옛 형식 — 파서는 실제 출력 기준으로.

### 3.3 `lspci -vvv` AER 블록 읽기 [예시 출력: 위 실측 레지스터 값으로 pciutils 형식 재구성]

```text
	Capabilities: [100 v2] Advanced Error Reporting
		UESta:	DLP- SDES- TLP- FCP- CmpltTO- CmpltAbrt- UnxCmplt- RxOF- MalfTLP- ECRC- UnsupReq- ACSViol-
		UEMsk:	DLP- SDES- TLP- FCP- CmpltTO- CmpltAbrt- UnxCmplt- RxOF- MalfTLP- ECRC- UnsupReq- ACSViol-
		UESvrt:	DLP+ SDES+ TLP- FCP+ CmpltTO- CmpltAbrt- UnxCmplt- RxOF+ MalfTLP+ ECRC- UnsupReq- ACSViol-
		CESta:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr-
		CEMsk:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr+
		AERCap:	First Error Pointer: 00, ECRCGenCap+ ECRCGenEn- ECRCChkCap+ ECRCChkEn-
		HeaderLog: 00000000 00000000 00000000 00000000
		RootCmd: CERptEn+ NFERptEn+ FERptEn+
		RootSta: CERcvd- MultCERcvd- UERcvd- MultUERcvd- FirstFatal- NonFatalMsg- FatalMsg- IntMsg 0
		ErrorSrc: ERR_COR: 0000 ERR_FATAL/NONFATAL: 0000
```

- 읽는 순서: **UESta/CESta에 `+` 있는가** → 있으면 UESvrt로 등급, UEMsk/CEMsk로 "보고는 됐나" → HeaderLog로 대상 → Root Port의 ErrorSrc로 출처 BDF.
- 같은 정보를 레지스터로: `setpci -s 01:00.0 ECAP_AER+0x04.l` (UncSta), `ECAP_AER+0x10.l` (CorSta). W1C라서 `setpci … ECAP_AER+0x10.l=0xffffffff` 로 clear (검증 스크립트의 baseline 리셋) [미검증: 이번 랩은 BusyBox라 setpci 없음].
- 인접 정보: Device Status `CorrErr+ NonFatalErr- FatalErr- UnsupReq+` (lspci `DevSta:`), 그리고 Gen3+ 장치의 **Secondary PCIe ext cap(0x19) Lane Error Status** — **레인별** 에러 → "x8 중 레인 5만 나쁨" 같은 보드 문제 특정 (LP02).

### 3.4 에러 주입 방법 4가지

| 방법 | 어디서 | 무엇을 | 비고 |
|---|---|---|---|
| **aer-inject** (`CONFIG_PCIEAER_INJECT` + 유저툴 `aer-inject`) | 리눅스 SW | AER 레지스터 읽기를 가로채 **가짜 status를 보이게** + AER 처리 트리거 → 드라이버 복구 경로 테스트 | 실제 TLP/링크엔 아무 일도 없음. Ubuntu 7.0 config: `# CONFIG_PCIEAER_INJECT is not set` [확인됨] → 모듈 빌드 필요 |
| **ACPI EINJ** (`/sys/kernel/debug/apei/einj/`) | 펌웨어 | `error_type` 0x40 PCIe Correctable / 0x80 Uncorrectable non-fatal / 0x100 fatal, `flags` 0x4 + `param4`=SBDF로 대상 지정 [7] | 서버/ACPI 플랫폼, 펌웨어 구현 의존 |
| **QEMU `pcie_aer_inject_error`** (HMP) | 에뮬레이터 | 지정 장치의 AER status set + 메시지 전송 | 이번 랩에서 사용. 장치 DevCtl 보고 비트가 켜져 있어야 메시지가 나감 |
| **HW: exerciser/analyzer, jammer, PHY 설정** | 실제 링크 | LCRC 깨뜨리기, NAK 강제, 레인 끊기, TX swing/EQ 낮추기, 장치 FW로 UR/CA 유발, 링크 강제 down(Surprise Down) | **진짜 SI·DLL 경로 검증**. SW 주입은 이걸 대체 못 함 |

- aer-inject 입력 예 (repo의 SPEC 문법 + `examples/correctable` 과 같은 형태, 미실행 [미검증]) [8]:

```text
AER
PCI_ID 0000:01:00.0
COR_STATUS BAD_TLP
HEADER_LOG 0 1 2 3
```

- 기타: 장치의 잘못된 BAR 영역 읽기 → UR, 존재하지 않는 장치 config 읽기, DPC SW trigger(DPC Cap의 SwTrigger 지원 시), `setpci`로 Severity를 바꿔 같은 에러를 Fatal로 만들어 reset 경로 시험.

### 3.5 검증에서 AER 카운트를 쓰는 법

```text
 1) Baseline:   known-good 보드 × N대 × 상온·정격전압, 링크 speed/width 확인 후 카운터 0으로 clear
               → 시간당 correctable rate의 분포 (대부분 0이어야 정상 [추정: 제품/채널별 기준 상이])
 2) Stress:     트래픽 최대(DMA 양방향, MPS/MRRS 최대) + ASPM on/off + 링크 speed별(Gen1..GenN)
 3) PVT:        온도 코너(cold/hot), 전압 코너(±x%), 공정 코너 칩 → 카운터 스냅샷을 온도/전압 로그와 같은 타임라인에
 4) 판정:       uncorrectable ≥ 1 → FAIL & root-cause
               correctable rate > 임계 (예: baseline × k, 또는 BER 환산) → FAIL/조사
               link speed/width 변화(LnkSta, bwctrl 이벤트) → FAIL
 5) 상관:       rate vs 온도 곡선, 특정 레인(Lane Error Status), 특정 슬롯/보드 → SI vs 칩 vs 보드 분리
 6) 마진:       Lane Margining at Receiver(Gen4+)로 eye 여유를 수치화 → "에러 0"을 "여유 얼마"로 바꿈
```

- BER 감각: Gen3+ 목표 BER ~1e-12. 8 GT/s × x1에서 1e-12면 **약 125초에 1비트** 오류 수준 → "몇 시간에 몇 개"가 의미 있는 단위 [추정: 계산 근사, 실제 판정은 제품 스펙].
- 자동화: 테스트 전후 `aer_dev_*`/`aer_rootport_total_err_*` diff + `lspci -vvv` LnkSta/DevSta 스냅샷 + 온도/전압 + dmesg AER 줄 → 한 레코드로 저장 → 대시보드에서 보드·코너별 추세.

## 4. 꼬리 질문 체인

### Chain 1 (LP00): "We see correctable errors climbing. Is that a problem?"

<details>
<summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 보기)</summary>

- **Q. We see correctable errors climbing. Is that a problem?**
  - EN: "Not immediately — correctable means the link recovered, usually by replaying a TLP, so no data was lost. But it costs bandwidth and latency, and it's an early warning that the link margin is shrinking. I'd look at which error bits, the rate over time, and whether it correlates with temperature, voltage or traffic."
  - 한: 데이터 손실은 없지만 마진 경고 → 종류·rate·상관관계.
- **꼬리1. Which ones point to signal integrity?**
  - EN: "Receiver errors at the physical layer, bad TLP and bad DLLP from LCRC failures, and replay timer timeouts and replay-number rollovers at the data link layer. Those are the bits that move with channel loss, equalization, reference clock jitter or a marginal connector. Advisory non-fatal or corrected internal errors are not SI — those are protocol or device-internal."
  - 한: RxErr, BadTLP, BadDLLP, Replay Timeout/Rollover = SI.
- **꼬리2. How would you use the counts in validation?**
  - EN: "Establish a baseline on known-good boards, then run stress at PVT corners and record the per-device counters from sysfs alongside temperature and voltage, as a rate per hour. Pass/fail is: no uncorrectable errors, no link downtrain, and correctable rate under a threshold derived from the baseline or a BER target. If the device has lane error status, I break it down per lane, which points at a specific trace or connector."
  - 한: baseline → PVT stress → sysfs 카운터 rate + 온도/전압 → 임계값, 레인별.
- **꼬리3. How do you test that the error path itself works?**
  - EN: "Error injection. In software, aer-inject or ACPI EINJ makes the kernel see an error so I can verify logging and the driver's recovery callbacks. For the real hardware path I'd use an exerciser or analyzer to corrupt LCRC or force NAKs, or degrade the transmitter settings, and check the device reports the right bit, the root port logs the right source, and recovery completes."
  - 한: SW 주입(aer-inject/EINJ) + HW 주입(exerciser) → 비트·출처·복구 확인.
- **꼬리4 (추가). The errors show on the root port, not on the endpoint. Does that clear the endpoint?**
  - EN: "No. Errors are reported by whoever detects them. If the root port's receiver sees bad TLPs, the problem is on the upstream direction — the endpoint's transmitter, the channel, or the root port's receiver equalization. The endpoint's counters being zero just means it received fine."
  - 한: 감지자가 보고 → 루트포트 카운터 = 상향 링크 문제, 엔드포인트 무죄 아님.

</details>

### Chain 2: "Walk me through what happens on an uncorrectable error."

<details>
<summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 보기)</summary>

- **Q. An endpoint hits an uncorrectable error. What happens, end to end?**
  - EN: "The endpoint sets the bit in its AER uncorrectable status, logs the TLP header, and if reporting is enabled sends ERR_NONFATAL or ERR_FATAL depending on the severity register. The root port records the source ID and raises its AER interrupt. Linux's AER service reads the device's registers, prints and counts the error, and then runs recovery: it calls each affected driver's error_detected, and depending on the answers and the severity it resets the link below the port, then calls slot_reset and resume."
  - 한: status+header log → ERR_* → Root Port source ID + IRQ → aerdrv 로그 → error_detected → reset → slot_reset → resume.
- **꼬리1. What's the difference in handling between non-fatal and fatal?**
  - EN: "For non-fatal the channel is still usable, so drivers get io_normal and can say they can recover without a reset; a reset only happens if one asks for it. For fatal the link is considered unreliable, drivers get io_frozen, and the kernel always resets — a driver can't opt out."
  - 한: non-fatal은 reset 선택적, fatal은 무조건.
- **꼬리2. What if the driver has no error handlers?**
  - EN: "Then the device isn't recovered — the kernel logs that it can't recover and the device stays broken until it's removed and rescanned or the system reboots. For a vehicle platform I'd make implementing error_detected, slot_reset and resume part of the driver acceptance criteria, and test it with injection."
  - 한: 복구 안 됨 → 드라이버 요구사항으로.
- **꼬리3. Where does DPC fit in?**
  - EN: "DPC is hardware containment on the downstream port: on a fatal error it disables the link immediately, so no more possibly corrupt traffic flows, and requests to the device get error completions instead of hanging the system. Software then runs the same recovery callbacks, clears the DPC status to let the link retrain, and resumes."
  - 한: HW가 즉시 링크 차단 → SW가 status clear → retrain → 같은 콜백.

</details>

### Chain 3 (추가): "You see Completion Timeouts."

<details>
<summary>Q → 꼬리1 → 꼬리2 (답 보기)</summary>

- **Q. The root port logs Completion Timeout. How do you debug it?**
  - EN: "Completion timeout is logged by the requester — the side that issued a read and never got an answer. So first, who's the requester: if it's the root port, a CPU MMIO read to the device timed out, meaning the device is hung, the link went down, or the address isn't decoded by the device. If it's the endpoint, its DMA read got no completion — often the address is outside the controller's inbound window or blocked by the IOMMU path. I'd check link status right after, the header log for the address, and the device's own state."
  - 한: requester가 보고 → RP면 MMIO→장치 hang/링크/주소, EP면 DMA→inbound 창/IOMMU.
- **꼬리1. Could the timeout value itself be the problem?**
  - EN: "Yes. Device Control 2 has a completion timeout range and disable bit. Some devices legitimately take long — for example coming out of a low-power state — so a too-short range gives false timeouts. I'd sweep the timeout setting and see if the errors move."
  - 한: DevCtl2 timeout 범위, 저전력 복귀 지연 → sweep.
- **꼬리2. UR versus CA?**
  - EN: "Unsupported Request means the receiver doesn't handle that request at all — unmapped address, function disabled, wrong type — usually a software or address-map issue. Completer Abort means the completer understood it but refused, which points to device-internal conditions or access policy."
  - 한: UR = 주소/SW, CA = 장치 거부.

</details>

### Chain 4 (추가): "Surprise Down during a test."

<details>
<summary>Q → 꼬리1 (답 보기)</summary>

- **Q. You get a Surprise Down error during a long stress test. Next steps?**
  - EN: "Surprise down means the link dropped without a proper shutdown. I'd correlate the timestamp with power and reset rails — PERST#, the endpoint's supply, the reference clock — and with temperature. If power was stable, it's the link itself: check correctable error trend just before the drop, replay rollovers, and LTSSM state from the controller's debug registers. I'd also confirm whether DPC or hotplug handled it and the device came back."
  - 한: 전원/PERST#/refclk/온도와 시간 상관 → 직전 correctable 추세 → LTSSM 디버그.
- **꼬리1. How do you tell a real link problem from the device crashing?**
  - EN: "If the device firmware crashed or reset itself, its LTSSM goes to Detect from its own side and it usually leaves crash logs or a reset reason. If it's the channel, you'd typically see recovery entries and correctable errors climbing first. An analyzer on the link shows which side stopped transmitting first."
  - 한: 장치 크래시 로그/리셋 사유 vs 직전 recovery·correctable 증가, 애널라이저로 누가 먼저 멈췄나.

</details>

## 5. Don 경험 연결

- "At SK hynix I automated chamber reliability testing for NAND and PCIe Gen3/4/5 high-speed interfaces, including shmoo — so using error counters as a function of temperature and voltage, against a baseline, with pass/fail thresholds, is exactly the workflow I'd apply to AER on a vehicle SoC. [숫자 채우기: 챔버 온도 범위 / 보드 수 / 테스트 시간]"
- "On the Solidigm PCIe Gen6 controller I worked with the device side of error reporting; at Gen6 the FEC corrects many errors invisibly, so I'd never treat zero AER correctables as proof of margin — I'd pair counters with lane margining."
- "At Apple I root-caused PCIe and other interface failures with a DSO and protocol analyzer; for AER the analyzer tells me which direction the NAKs are on, which tells me whose receiver is seeing the bad TLPs."
- "For validation automation I'd snapshot the sysfs AER counters before and after every test case and store them with the link state and chamber readings, because dmesg is rate-limited and you can't count from it."

## 6. 실습 / 코드

### 6.1 `onsitePrep/lp09/aer_decode.c` — AER 레지스터 디코더 (macOS 컴파일·실행 ✅)

- 비트 이름/오프셋 = `pci_regs.h`, 짧은 이름·계층 = `aer.c`, "hint" 열 = 검증 휴리스틱(스펙 아님).
- 입력: (A) Jetson 실로그 값, (B)(C) QEMU 랩 실측 값 + AER cap 원본 bytes, (C2) Root Status/Error Source 합성 값, (D) 커널 문서 TLP 헤더.
- 빌드: `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined aer_decode.c -o /tmp/aer && /tmp/aer`
- 실제 출력 (`onsitePrep/lp09/aer_decode.out.txt`):

```text
[A] Jetson Orin NX nvme 0004:01:00.0 (real dmesg, 5.10): status/mask=00000040/0000e000
  COR status/mask=00000040/0000e000
    [ 6] BadTLP         Correctable  Data Link   LCRC fail -> NAK+replay (SI)
    note: mask e000 = bits 13..15 (ANFE, CorrIntErr, HeaderOF) masked = spec default
[B] QEMU root port 00:01.0 (real, 6.18.55, injected UR)
  UNC status/mask=00100000/02400000
    [20] UnsupReq       NonFatal     Transaction SW bug: access to unmapped addr / disabled fn  (First)
[C] QEMU root port AER capability bytes @0x100 (real dump) + injected MalfTLP
  ext cap header=0x14820001 id=0x0001 ver=2 next=0x148
  UNC mask=02400000 severity=00462030  COR mask=0000e000  Cap&Ctl=000002a0 (FEP=0, ECRC gen/chk capable=y/y, enabled=n/n)
  default-Fatal bits in severity: DLP SDES FCP RxOF MalfTLP UncorrIntErr
  Root Error Command=7 (COR_EN=1 NONFATAL_EN=1 FATAL_EN=1)
  UNC status/mask=00040000/02400000
    [18] MalfTLP        Fatal        Transaction bad TLP format: RTL/IP bug, MPS mismatch  (First)
[C2] Root Error Status / Error Source decode (synthetic values)
  RootErrSta=00000055: ERR_COR_rcvd ERR_FATAL/NONFATAL_rcvd first_uncor_is_FATAL FATAL_msg_rcvd  AER IRQ msg#=0
  ErrSrcID=01000100: ERR_COR from 01:00.0, ERR_FATAL/NONFATAL from 01:00.0
[D] Documentation/PCI/pcieaer-howto.rst example header (UR, Fatal)
  TLP Header: 0x04000001 0x00200a03 0x05010000 0x00050100
    DW0 fmt=0 type=0x04 -> CfgRd0, length=1 DW
    DW1 requester=00:04.0 tag=0x0a lastBE=0x0 firstBE=0x3
    DW2 target=05:00.1 register=0x000
ALL PASS
```

- 연습: 이 디코더를 sysfs `aer_dev_*` 파서 + 온도 로그와 묶어 "코너별 correctable rate 표"를 만드는 스크립트로 확장 (LP14 자동화와 연결).

### 6.2 QEMU AER 주입 랩 (실행 ✅)

- 스크립트: `onsitePrep/lp07/qemu_lab/host.sh` (QEMU 부팅 + `-monitor unix:` 소켓에 `pcie_aer_inject_error` 5회 전송) / `run.sh` (게스트: 카운터 before/after, dmesg, `/proc/interrupts`). 원본: `onsitePrep/lp07/qemu_lab/guest_output_2026-10-31.txt`.
- 시행착오(실제): 첫 실행은 e1000e(`nic1`)에 주입 → **아무 로그도 없음**. 원인: e1000e DevCtl 보고 enable이 0 (QEMU 모델에서 쓰기 불가) → ERR_* 메시지 미전송. Root Port(`rp1`, DevCtl `0x000f`)로 바꾸자 정상 동작. → "보고 enable/mask 확인"이 디버그 1단계인 실례.
- 미실행 확장 [미실행]: ① `CONFIG_PCIEAER_INJECT=m` 빌드 + `aer-inject`로 드라이버 콜백 경로만 시험 ② e1000e 대신 자체 edu 드라이버에 `pci_error_handlers` 구현 + `pr_info`로 콜백 순서 출력 ③ DPC 있는 에뮬레이션/실 HW에서 `containment event` 로그 확보.

## 7. 자주 틀리는 것

1. **"correctable이니 무시"** — 데이터 손실은 없지만 rate 증가는 마진 감소 신호. 검증에선 rate로 판정.
2. **"에러가 찍힌 장치가 범인"** — 감지한 쪽이 보고. Root Port 카운터 = 상향 링크 문제일 수 있음.
3. **dmesg 줄 수로 에러를 센다** — correctable/non-fatal은 5초 10건 ratelimit. sysfs 카운터 사용.
4. **mask된 에러는 일어나지 않은 것** — mask는 "보고 안 함". status엔 남을 수 있고, 기본 mask(ANFE·CorrIntErr·HeaderOF, UncorrIntErr)를 기억.
5. **UR은 항상 버그** — 열거 중 없는 function 접근 등 정상 UR도 있음(그래서 ANFE로 강등되기도). 런타임 데이터 경로 UR은 버그.
6. **Fatal = 시스템 다운** — Linux는 reset + 드라이버 콜백으로 복구 시도. 드라이버에 handler가 없으면 복구 실패.
7. **AER 로그가 없으니 에러 없음** — `_OSC`로 펌웨어가 AER를 가져갔거나(Lima 실측 `platform retains control`), `pci=noaer`, 장치 DevCtl 보고 비트 off(QEMU 랩 실측).
8. **Severity는 고정** — SW가 바꿀 수 있다. 플랫폼마다 다르니 비교할 땐 Severity도 같이 기록.
9. **SW 주입(aer-inject) = HW 검증** — aer-inject는 레지스터 읽기만 속인다. PHY/DLL 경로는 exerciser/analyzer로.
10. **Gen6에서 correctable 0 = 마진 충분** — FEC가 숨긴다. margining과 FEC 카운터 병행.

## 8. 출처

1. Linux `include/uapi/linux/pci_regs.h` (PCI_ERR_*, PCI_EXP_DEVCTL/DEVSTA, PCI_EXP_DPC_*, Flit 관련): https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/pci_regs.h
2. Linux `drivers/pci/pcie/aer.c` (출력 형식, agent/layer 매핑, ratelimit, trace_aer_event): https://raw.githubusercontent.com/torvalds/linux/master/drivers/pci/pcie/aer.c · 6.18.55 소스(`dpc.c`, `edr.c`, `err.c`)는 Lima VM `/var/lab/src`, mirror https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/pcie
3. Linux ABI — sysfs-bus-pci-devices-aer (aer_dev_*, aer_rootport_total_err_*, ratelimit): https://raw.githubusercontent.com/torvalds/linux/master/Documentation/ABI/testing/sysfs-bus-pci-devices-aer
4. Linux kernel docs — The PCI Express Advanced Error Reporting Driver Guide HOWTO: https://docs.kernel.org/PCI/pcieaer-howto.html
5. Linux kernel docs — PCI Error Recovery: https://docs.kernel.org/PCI/pci-error-recovery.html · `include/linux/pci.h` (struct pci_error_handlers, pci_ers_result): https://raw.githubusercontent.com/torvalds/linux/master/include/linux/pci.h
6. NVIDIA Developer Forum — "PCIe Bus Error: severity=Corrected, type=Physical Layer, id=0010(Receiver ID)" (Jetson Orin NX, 5.10.104-tegra 로그): https://forums.developer.nvidia.com/t/pcie-bus-error-severity-corrected-type-physical-layer-id-0010-receiver-id/291143
7. Linux kernel docs — APEI Error INJection (EINJ): https://docs.kernel.org/firmware-guide/acpi/apei/einj.html
8. intel/aer-inject (user-space tool): https://github.com/intel/aer-inject
9. 실측: QEMU 10.2.1 `virt,gic-version=3,iommu=smmuv3` + pcie-root-port + e1000e, kernel 6.18.55, `pcie_aer_inject_error` — `onsitePrep/lp07/qemu_lab/guest_output_2026-10-31.txt`; Lima VM `_OSC` 로그 / Ubuntu 7.0 config (`CONFIG_PCIEAER_INJECT` not set, `CONFIG_PCIE_DPC=y`, `CONFIG_PCIE_EDR=y`)

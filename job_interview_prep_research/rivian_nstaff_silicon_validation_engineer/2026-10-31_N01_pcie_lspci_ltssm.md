# N01 · PCIe 링크 디버그 — lspci · LTSSM · "L0 전에 무슨 일이?"
<!-- theme: pastel -->

> **왜 이 노트**: 10-30 기술 스크린에서 실제로 받은 질문 (lspci로 뭘 보나 / L0 전 과정 / lspci로 L0 확인 가능? / L0 안 되면 lspci에 나오나) — Don이 "lspci 잘 모름"이라고 답한 부분 · **우선순위**: P0 (온사이트 technical에서 다시 파고들 확률 높음) · **코드**: `onsitePrep/pcie_lnksta_decode.c` (ALL PASS) · **목록**: [온사이트 플랜](2026-10-31_rivian_onsite_plan.html)

## TL;DR — 4개 질문 모범 답 (먼저 이것만 외우기)

| 질문 | 한 줄 답 |
|---|---|
| **lspci로 뭘 볼 수 있나?** | 열거(enumeration)된 모든 PCI 함수의 **config space**: BDF, vendor/device ID, class, BAR, 드라이버, capability. 링크는 `-vvv` 의 **LnkCap(최대 능력) vs LnkSta(현재 협상된 speed/width)**, AER 에러, ASPM 상태 |
| **L0 전에 무슨 일이?** | 전원 안정 → refclk 안정 → **PERST# deassert** → LTSSM **Detect**(상대 receiver 감지) → **Polling**(TS1/TS2, bit/symbol lock) → **Configuration**(lane 수·번호, polarity) → **L0 (Gen1 속도)** → **Recovery**로 speed change(+Gen3 이상은 equalization) → 목표 속도 L0 → DLL flow-control init → **DL_Active** → SW 열거 |
| **lspci로 L0 확인 가능?** | **LTSSM 상태 자체는 표준 config 레지스터에 없어서 직접은 못 본다.** 간접적으로: **downstream port(root port/switch)** 의 LnkSta `DLActive+` (Data Link Layer up = L0 통과) + `Train-` + 협상된 speed/width. 정확한 LTSSM 상태는 **컨트롤러 vendor debug 레지스터** 또는 **프로토콜 애널라이저** |
| **L0 안 되면 lspci에 나오나?** | **endpoint는 안 나온다** (config read가 응답 없음 → 0xFFFF → "장치 없음"). **root port는 나온다** — 그 포트의 LnkSta가 `DLActive-`, `Train+`, width x0 등으로 실패를 보여줌. 부팅 후 링크가 죽으면 이미 열거된 장치는 남아 있지만 `rev ff` / `Unknown header type 7f` 로 보임 |

- **영어 30초 (Q3+Q4 합친 버전)**: "lspci reads configuration space, and the LTSSM state isn't in the standard registers, so I can't see 'L0' directly. What I check is the Link Status register of the downstream port above the device — DLActive set means the data link layer is up, which only happens after the LTSSM reached L0 — and the negotiated speed and width against Link Capabilities. If the link never trains, the endpoint won't show up at all because config reads to it fail, but the root port still shows up with DLActive cleared. For the exact LTSSM state I'd read the controller's vendor debug register or put an analyzer on the link."

## 1. 면접 회고 — 무엇이 부족했고 다음엔 어떻게

- 실제로 한 답: "lspci는 잘 모른다, Mac에서는 써 봤는데 커맨드가 다르다."
- 문제: 면접관이 본 건 **명령어 암기가 아니라 "링크가 어디까지 올라왔는지를 어떤 레지스터로 판단하나"** 였을 가능성이 큼 [추정]. Don은 이 지식(LTSSM, Link Status, analyzer)을 Solidigm·Apple에서 이미 가지고 있음 → **도구 이름이 낯설어도 레지스터 수준으로 답을 이어갔으면 됐다**.
- 다음엔 이렇게 브릿지: "I've used the macOS equivalents more — system_profiler and ioreg show the same link speed and width — but under the hood they all read the same PCIe capability registers, so let me answer at that level: …"
- macOS 대응 [추정]: `system_profiler SPPCIDataType` (Link Width / Link Speed 표시), `ioreg -rc IOPCIDevice -l` (`IOPCIExpressLinkStatus` / `IOPCIExpressLinkCapabilities` 속성).

## 2. lspci 치트시트 (Linux)

| 명령 | 보는 것 |
|---|---|
| `lspci` | 장치 목록: `BB:DD.F class: vendor device (rev)` |
| `lspci -nn` | 이름 + 숫자 ID `[8086:xxxx]` — 드라이버 매칭 확인 |
| `lspci -tv` | **트리**: root port → switch → endpoint 토폴로지 (어느 포트 밑에 붙었나) |
| `lspci -k` | 바인딩된 **커널 드라이버**와 후보 모듈 |
| `sudo lspci -s 01:00.0 -vvv` | 한 장치의 전체 디코드: BAR, capabilities, **LnkCap/LnkCtl/LnkSta/LnkCtl2/LnkSta2**, DevSta, AER, ASPM. root 권한 없으면 capability 일부 안 보임 |
| `sudo lspci -s 01:00.0 -xxx` / `-xxxx` | config space **hex 덤프** 256 B / 4 KiB (extended config: AER, SR-IOV 등) |
| `lspci -D` | PCI domain(segment) 포함 `0000:01:00.0` |
| `sudo setpci -s 01:00.0 CAP_EXP+0x12.w` | PCIe capability의 **Link Status 레지스터를 직접 읽기** |
| `sudo setpci -s 00:1c.0 CAP_EXP+0x10.w=0x20:0x20` | root port **Link Control의 Retrain Link(bit 5)** 세트 → 재학습 |
| `cat /sys/bus/pci/devices/0000:01:00.0/current_link_speed` (`current_link_width`, `max_link_speed`, `max_link_width`) | sysfs로 같은 정보 |
| `echo 1 > /sys/bus/pci/devices/0000:01:00.0/remove` → `echo 1 > /sys/bus/pci/rescan` | 장치 제거 후 **재열거** (부팅 후에 링크가 올라온 경우) |
| `dmesg \| grep -i -E "pcie\|aer\|link"` | 컨트롤러 드라이버의 link up/down, AER 에러 로그 |
| `cat /sys/bus/pci/devices/…/aer_dev_correctable` | AER correctable 에러 카운터 (Receiver Error, Bad TLP, Bad DLLP, Replay Timer…) |

### 2.1 `lspci -vvv` 링크 부분 읽는 법 (예시 출력, 설명용)

```text
01:00.0 Non-Volatile memory controller: Vendor Device (rev 01)
    Capabilities: [70] Express (v2) Endpoint, MSI 00
        LnkCap: Port #0, Speed 16GT/s, Width x4, ASPM L1, Exit Latency L1 <64us
                ClockPM+ Surprise- LLActRep- BwNot- ASPMOptComp+
        LnkCtl: ASPM Disabled; RCB 64 bytes, Disabled- CommClk+
        LnkSta: Speed 8GT/s (downgraded), Width x4      ← 현재 pciutils는 일치하면 아무것도 안 붙임(옛 버전은 "(ok)"), "(downgraded)"는 endpoint/upstream 쪽에서만 표시 — LP13 참고
                TrErr- Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-
        LnkCtl2: Target Link Speed: 16GT/s, EnterCompliance- SpeedDis-
        LnkSta2: Current De-emphasis Level: -6dB, EqualizationComplete+ EqualizationPhase1+
                 EqualizationPhase2+ EqualizationPhase3+ LinkEqualizationRequest-
```

| 필드 | 의미 | 디버그 포인트 |
|---|---|---|
| **LnkCap** Speed/Width | 이 포트가 **할 수 있는** 최대 | 양쪽 LnkCap 중 작은 쪽이 기대값 |
| **LnkSta** Speed/Width | 지금 **협상된** 값 | `(downgraded)` = 기대보다 낮음 → SI, equalization, 레인 불량 |
| `Train+` | 링크 학습 중 (downstream port에서 의미) | 계속 `+` 면 학습이 안 끝남 |
| `DLActive+` | **Data Link Layer up** = LTSSM이 L0에 도달 + FC init 완료 | 보통 **downstream port** 쪽에서 의미 있음, `LLActRep+` (보고 능력) 있어야 유효. endpoint의 값은 보통 `-` 로 보여도 정상 |
| `TrErr+` | training error (구형) | |
| `BWMgmt+`/`ABWMgmt+` | 링크 속도/폭이 **바뀌었다** (자율 변경 포함) | 런타임 downtrain 감지 |
| LnkCtl2 Target Link Speed | SW가 정한 목표 속도 | Gen1로 낮춰서 링크가 뜨는지 실험 |
| LnkSta2 Equalization* | Gen3+ equalization 단계 성공 여부 | Phase 실패 → Gen3+ 진입 못함 |
| LnkCtl ASPM | L0s/L1 저전력 허용 | ASPM 켜면 L1 진입/탈출 latency·버그 → 문제시 끄고 비교 |

### 2.2 Link 관련 레지스터 비트 (PCIe capability 기준 offset)

| 레지스터 (offset) | 비트 | 내용 |
|---|---|---|
| Link Capabilities (+0x0C) | [3:0] | Max Link Speed (1=2.5, 2=5, 3=8, 4=16, 5=32, 6=64 GT/s) |
| | [9:4] | Max Link Width (x1…x16) |
| | [11:10] | ASPM 지원 |
| | 20 | DLL Link Active Reporting Capable (`LLActRep`) |
| Link Control (+0x10) | [1:0] | ASPM Control |
| | 4 | Link Disable |
| | 5 | **Retrain Link** |
| Link Status (+0x12) | [3:0] | Current Link Speed |
| | [9:4] | Negotiated Link Width |
| | 11 | **Link Training** |
| | 13 | **Data Link Layer Link Active** |
| | 14 / 15 | Link BW Management / Autonomous BW Status |
| Link Control 2 (+0x30) | [3:0] | Target Link Speed |
| Link Status 2 (+0x32) | 1–5 | Equalization 8GT/s Complete, Phase 1/2/3 Successful, Link EQ Request |

## 3. "L0 전에 일어나는 일" — 순서대로

### 3.1 전기적 준비 (플랫폼/보드 — board validation 영역 ★)

| 단계 | 내용 | 확인 방법 |
|---|---|---|
| 1. 전원 | 3.3V/12V(카드), 코어 전원 rail 안정 | 오실로스코프, PMIC 상태 |
| 2. refclk | 100 MHz 기준 클럭 안정 (common clock 또는 SRIS) | 스코프로 주파수/지터 |
| 3. **PERST#** | 전원 안정 후 **최소 100 ms** 지나서 deassert (CEM T_PVPERL) | PERST# 타이밍 스코프 캡처 |
| 4. LTSSM 시작 | PERST# deassert 후 **20 ms 안에** link training 시작 | |

### 3.2 LTSSM (Link Training and Status State Machine)

```text
 Detect ──► Polling ──► Configuration ──► L0 (2.5 GT/s)
 (RX 감지)   (TS1/TS2,     (lane 수·번호,      │
             bit/symbol    lane reversal,     ▼
             lock)         polarity)      Recovery ──► (Gen3+: Equalization Ph0–3) ──► L0 @ 목표 속도
                                             ▲                                       │
                                             └──── 에러/재학습/speed change ◄──────────┘
 그 밖의 상태: L0s · L1 · L2 (저전력) · Disabled · Loopback · Hot Reset · Compliance
```

| 상태 | 하는 일 | 여기서 막히는 흔한 원인 |
|---|---|---|
| **Detect** (Quiet/Active) | TX가 상대 **receiver termination**을 전기적으로 감지 → 어느 lane에 상대가 있나 | 카드 미장착, 전원/리셋 안 풀림, AC coupling cap 누락, 레인 단선 |
| **Polling** (Active/Configuration) | **TS1 → TS2** ordered set 교환, bit lock·symbol(block) lock, 속도 능력 광고. 실패하면 Compliance로 빠질 수 있음 | refclk 문제, SI(신호 품질), polarity |
| **Configuration** | link 번호·**lane 번호 협상**, 폭 결정(x4 → x2/x1로 줄 수 있음), lane reversal | 일부 lane 불량 → **width downgrade** |
| **L0** | 정상 동작. 처음엔 **Gen1 2.5 GT/s** | |
| **Recovery** | 더 높은 속도로 **speed change**, 에러 후 재동기화. Gen3+에서는 **Equalization Phase 0–3** (TX preset/coefficient 조정) | equalization 실패 → Gen1/2에 머묾 (`downgraded`) |
| L0 이후 | **Data Link Layer**: InitFC1/InitFC2로 flow control credit 교환 → **DL_Active** (`DLActive+`) → TLP 송수신 가능 | |

### 3.3 L0 이후 SW 열거까지

1. Root complex/SW가 **config read**로 Vendor ID 확인 (Gen1/2는 리셋 후 100 ms, 5 GT/s 초과 링크는 **link training 완료 후 100 ms** 기다린 뒤 config 요청 [spec 규칙]).
2. 장치가 아직 준비 안 됐으면 **CRS(Configuration Request Retry Status)** 로 응답 → 재시도.
3. 버스 번호 할당(bridge의 primary/secondary/subordinate), **BAR sizing**(all-1s 쓰고 읽기 → 크기), 메모리 주소 할당, MSI/MSI-X 설정.
4. 드라이버 probe → 장치 동작. 이 시점에 `lspci`에 보인다.

- 영어: "Before L0: power and refclk stable, PERST# released, then the LTSSM goes Detect — sensing the receiver termination — Polling, where TS1 and TS2 ordered sets get bit and symbol lock, Configuration, where width and lane numbers are negotiated, and L0 at 2.5 gig. Then it goes through Recovery to change speed, with equalization for Gen3 and above. After the data link layer finishes flow-control init, DL_Active is set and software can enumerate."

## 4. "L0가 안 되면 lspci에 나오나?" — 경우별 정리

| 상황 | endpoint가 lspci에? | root port(위 포트)의 LnkSta | 기타 단서 |
|---|---|---|---|
| 링크가 **Detect에서 멈춤** (카드 없음/전원 X) | ❌ | `DLActive-`, width x0, (슬롯이면 `SltSta PresDet-`) | dmesg "link down" / 컨트롤러 드라이버 "link never came up" 류 |
| **Polling/Configuration 반복** | ❌ | `Train+` 가 계속 또는 `DLActive-` | vendor LTSSM 레지스터가 Polling↔Detect 반복 |
| L0 도달, 하지만 **Gen1 x1** 등으로 downgrade | ✅ | `Speed 2.5GT/s (downgraded), Width x1 (downgraded)` | LnkSta2 equalization 비트, AER correctable 증가 |
| L0 도달 후 **런타임 링크 다운** | ✅ (이미 열거됨) 그러나 `rev ff`, `-vvv`에서 **`Unknown header type 7f`** | `DLActive-` | AER Surprise Down, dmesg AER 로그, 드라이버 timeout |
| 부팅 후 늦게 링크 업 | ❌ → rescan 후 ✅ | 이제 `DLActive+` | `echo 1 > /sys/bus/pci/rescan`, hotplug 지원 여부 |
| 링크 OK, **장치 준비 안 됨** | 처음엔 CRS 재시도, 시간 넘으면 ❌ | `DLActive+` | 장치 FW 부팅 시간, CRS SW visibility |

- 핵심 논리: **열거 = config read 성공**. config TLP는 DL_Active 이후에만 흐른다 → **L0 도달 없이는 endpoint를 읽을 방법이 없다** → lspci에 안 나옴. 반대로 root port는 root complex 안쪽이라 링크와 무관하게 항상 보임.

## 5. 디버그 플로우 — "장치가 lspci에 안 보인다" (★ 시나리오 질문 대비)

1. **보드 레벨**: 전원 rail, refclk, PERST# 타이밍 스코프 (§3.1). 카드/커넥터 재장착, 다른 슬롯/다른 카드로 교차 확인.
2. **root port 확인**: `lspci -tv` 로 어느 포트인지 → `lspci -s <rp> -vvv` 의 LnkSta (`DLActive`, `Train`, width/speed).
3. **LTSSM 상태**: 컨트롤러 vendor debug 레지스터 (예: Synopsys DesignWare 코어의 port-logic debug 레지스터에 LTSSM 상태 필드) → Detect에서 멈췄나, Polling 반복인가.
4. **조건 낮추기**: LnkCtl2 Target Link Speed = Gen1 → Retrain → 뜨면 **고속 SI/equalization 문제**. width 줄이기, polarity/lane reversal 설정 확인.
5. **애널라이저/인터포저**: TS1/TS2, equalization preset 교환을 직접 관찰.
6. **SW 쪽**: 너무 일찍 config 접근(100 ms 규칙), CRS 처리, BIOS/DT 설정(lane 수, refclk 모드), rescan.
7. **결론 기록**: 재현 조건 + 회귀 테스트 추가(N회 부팅 링크 업 + speed/width 체크 자동화).

- **Apple/Solidigm 경험 연결**: S8 스토리 ([deck §4](2026-10-25_rivian_hm_screen_prep.html)) — "link trained at Gen[ ] but fell back…" 숫자 채워 두기.

## 6. Bare-metal에서 같은 정보 읽기 (이 포지션은 OS 없이 C)

- Linux가 없으면 lspci도 없다 → **ECAM**(memory-mapped config)으로 직접: `addr = ECAM_BASE + (bus << 20 | dev << 15 | fn << 12 | offset)`.
- Capability list를 따라가 PCIe cap(ID 0x10)을 찾고 LnkSta(+0x12)를 디코드 → 이게 lspci가 하는 일 그 자체.

```c
/* ECAM: each function has 4 KiB of config space at base + bus<<20 | dev<<15 | fn<<12. */
static inline uintptr_t ecam_addr(uintptr_t base, unsigned bus, unsigned dev, unsigned fn, unsigned off)
{
    return base + (((uintptr_t)bus << 20) | ((uintptr_t)dev << 15) | ((uintptr_t)fn << 12) | off);
}

/* Returns the config offset of capability `id`, 0 if absent. Guard against a looping list. */
unsigned find_cap(const uint8_t *cfg, uint8_t id)
{
    if (rd16(cfg, PCI_VENDOR_ID) == 0xFFFF || !(rd16(cfg, PCI_STATUS) & PCI_STATUS_CAPLST))
        return 0;
    unsigned p = cfg[PCI_CAP_PTR] & 0xFCu;
    for (int guard = 48; p != 0 && guard > 0; guard--) {
        if (cfg[p] == id)
            return p;
        p = cfg[p + 1] & 0xFCu;
    }
    return 0;
}

bool decode_link(const uint8_t *cfg, link_info_t *li)
{
    unsigned e = find_cap(cfg, PCI_CAP_ID_EXP);
    if (e == 0)
        return false;
    uint32_t cap = rd32(cfg, e + EXP_LNKCAP);
    uint16_t sta = rd16(cfg, e + EXP_LNKSTA);
    li->cap_speed_x10 = speed_x10(cap & 0xFu);
    li->cap_width     = (cap >> 4) & 0x3Fu;
    li->sta_speed_x10 = speed_x10(sta & 0xFu);
    li->sta_width     = (sta >> 4) & 0x3Fu;
    li->training      = (sta & LNKSTA_TRAINING) != 0;
    li->dll_active    = (sta & LNKSTA_DLL_ACTIVE) != 0;
    li->dll_reporting = (cap & LNKCAP_DLLLA_CAP) != 0;
    return true;
}
```

- 테스트 출력(가짜 config space): `LnkCap 16.0 GT/s x4 | LnkSta 8.0 GT/s x4 DLActive+ (downgraded)`. 링크 실패 케이스(`Train+`, x0), 죽은 endpoint(전부 0xFF), **무한 루프 capability list**까지 테스트.
- 더 깊게: [L2 PCIe cap walk](hmScreenPrep/problems/L2_pcie_cap_walk/index.html) (extended capability, BAR sizing, CRS).

## 7. 온사이트에서 이어질 꼬리 질문 (예상)

| 질문 (English) | 핵심 답 |
|---|---|
| How does BAR sizing work? | BAR에 all-1s 쓰고 읽음 → 하위 고정 비트 마스크 → `size = ~(val & ~0xF) + 1`, 64-bit BAR는 두 개 묶음, 원래 값 복원 |
| TLP vs DLLP? | TLP = transaction(메모리/config/메시지, 완료) / DLLP = link 관리(ACK/NAK, flow control credit, PM) |
| What does ACK/NAK + replay do? | DLL이 TLP마다 LCRC·시퀀스 번호, NAK이나 replay timer 만료 시 재전송 → correctable 에러로 AER에 남음 |
| Posted vs non-posted? | memory write = posted (완료 없음) / read, config = non-posted (completion 필요) → MMIO write 후 read-back으로 flush |
| What's AER? | Advanced Error Reporting: correctable(Receiver Error, Bad TLP/DLLP, Replay) / uncorrectable non-fatal(Completion Timeout, UR) / fatal(Surprise Down, Malformed TLP). 카운터 증가 추세로 margin 판단 |
| Gen6 differences? | 64 GT/s, **PAM4**, **FLIT mode**, 경량 **FEC** + CRC → 에러 동작이 달라짐 |
| How would you validate a PCIe controller on new silicon? | link up 반복(N회 cold/warm/hot reset), 모든 speed×width 조합, equalization preset sweep, BER/에러 카운터 at PVT corner, DMA throughput, ASPM L1 진입/탈출, hot reset/FLR, AER 에러 주입 |
| Hot reset vs FLR vs PERST#? | PERST# = fundamental reset(보드 신호) / Hot reset = 링크로 전달(Secondary Bus Reset) / FLR = 함수 하나만 리셋 |

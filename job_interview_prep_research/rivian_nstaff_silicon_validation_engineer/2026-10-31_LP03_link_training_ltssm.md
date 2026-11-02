# LP03 · 링크 트레이닝 · LTSSM · speed/width · equalization
<!-- theme: pastel -->

> **묶음**: B 링크 · **우선**: P0 (10-30 기술 스크린 질문 4개 중 3개가 여기) · **선행 노트**: [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [LP02 config space](2026-10-31_LP02_config_space_capabilities.html) · [N01 (이 노트의 얕은 버전)](2026-10-31_N01_pcie_lspci_ltssm.html) · **다음 노트**: [LP04 열거·리소스·hotplug](2026-10-31_LP04_enumeration_resources_hotplug.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · **코드**: [lp03/pcie_set_speed_retrain.sh](onsitePrep/lp03/pcie_set_speed_retrain.sh) · [lp03/ltssm_trace.c](onsitePrep/lp03/ltssm_trace.c) · [QEMU 실측 로그](onsitePrep/lp03/qemu_lab/run1_boot_lspci_iomem.log) · [LnkSta 디코더 (N01)](onsitePrep/pcie_lnksta_decode.c)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"L0 전에 무슨 일이?" · "lspci로 L0 확인 가능?" · "L0 실패하면 lspci에 나오나?" · "Gen4가 Gen3로 떴다, 뭘 보나?" · "equalization이 뭐고 왜 Gen3부터?"** — 전부 레지스터·타이밍 수준으로.
- **외울 것 1 — 순서**: 전원 안정 → REFCLK 안정(PERST# 해제 ≥100 µs 전) → **PERST# 해제 (전원 안정 후 ≥100 ms)** → 20 ms 안에 LTSSM **Detect** → **Polling** (TS1/TS2, bit/symbol lock) → **Configuration** (link#, lane#, width) → **L0 @ 2.5 GT/s** → **Recovery** (speed change, Gen3+는 **Equalization Phase 0–3**) → L0 @ 목표 속도 → DLL **InitFC1/InitFC2 → DL_Active** → (>5 GT/s면 링크 학습 완료 후 100 ms 대기) → config read로 열거.
- **외울 것 2 — 레지스터**: PCIe cap +0x12 **Link Status**: [3:0] speed, [9:4] width, **bit11 LT(Train)**, **bit13 DLLLA(DLActive)**, bit14 LBMS, bit15 LABS. +0x10 Link Control: bit4 Link Disable, **bit5 Retrain**. +0x30 Link Control 2 [3:0] **Target Link Speed**. +0x32 Link Status 2: bit1 EQ Complete, bit2–4 Phase1–3 Successful, bit5 EQ Request. (커널 `pci_regs.h` + pciutils `header.h`로 검증)
- **외울 것 3 — LTSSM 상태는 표준 config 레지스터에 없다.** lspci로는 **위쪽 포트(root port / switch DSP)의 `DLActive+` + `Train-` + speed/width** 로 간접 판단. 진짜 상태는 **컨트롤러 vendor 레지스터** (예: Synopsys DWC `PCIE_PORT_DEBUG0` 0x728 [5:0], L0 = 0x11; Linux 6.18은 debugfs `dwc_pcie_<dev>/ltssm_status`) 또는 **프로토콜 애널라이저**.
- QEMU 실측: root port LnkCap `16GT/s x4` 인데 밑의 QEMU nvme가 `2.5GT/s x1` 만 지원 → root port LnkSta `2.5GT/s x1 DLActive+` (두 끝의 **작은 쪽**으로 협상). 빈 슬롯 root port는 `DLActive-` 인데 speed/width는 쓰레기값(`16GT/s x32`) → **DLActive를 먼저 보고, 그 다음에 speed/width를 믿는다**.
- **30초 영어 답 (헤드라인 질문 "What happens before L0, and can you confirm L0 with lspci?")**: "Before the LTSSM even starts, power has to be stable, the reference clock running, and PERST# released — at least 100 ms after power is good. The LTSSM then goes Detect, where the transmitter senses the far-end receiver termination; Polling, where both sides send TS1 and TS2 ordered sets to get bit and symbol lock; and Configuration, where the downstream port assigns the link and lane numbers and the width is settled. It always reaches L0 at 2.5 GT/s first, then goes through Recovery to change speed, with equalization phases for 8 GT/s and above. lspci can't show the LTSSM state directly because it's not in standard config space — I'd look at the root port's Link Status: Data Link Layer Link Active set, Link Training clear, and the negotiated speed and width compared with both sides' Link Capabilities. For the actual state I'd read the controller's vendor debug register, like the DesignWare PORT_DEBUG0 LTSSM field, or put an analyzer on the link."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "처음 만나는 두 사람이 무전기로 대화 채널을 맞추는 과정"

두 사람(A = 칩 쪽 **Root Port**, B = 카드 쪽 **Endpoint**)이 여러 개의 무전기 채널(= **lane**)로 연결되어 있다고 상상하자.

1. **스위치 켜기 (전원 + 기준 시계 + 리셋 해제)**: 두 사람 다 깨어 있어야 하고, 둘이 쓰는 메트로놈(= **REFCLK**, 100 MHz 기준 클럭)이 돌고 있어야 하며, 감독관이 "시작!"(= **PERST#** 신호 해제)을 외쳐야 한다.
2. **"거기 누구 있어요?" (Detect)**: A가 각 채널에 살짝 전압을 걸어보고 "반대편에 수신기가 꽂혀 있나"를 전기적으로 느낀다 (수신기 끝의 저항 = termination이 있으면 충전이 느리게 된다).
3. **"내 말 들려요? 리듬 맞춰요" (Polling)**: 정해진 인사말(= **TS1**, 그 다음 **TS2**)을 반복해서 보낸다. 받는 쪽은 이 반복 패턴으로 **비트 경계**(bit lock)와 **글자 경계**(symbol lock)를 찾는다. 선이 뒤집혀(+/-) 있으면 받는 쪽이 알아서 뒤집는다(polarity inversion).
4. **"채널 몇 개 쓸까? 몇 번 채널이 1번이야?" (Configuration)**: A(downstream 쪽)가 "우리 팀 번호(link#)는 0, 너 채널 0번은 lane 0" 처럼 번호를 붙이고, 살아 있는 채널 수로 폭(width: x1/x4/x16)을 정한다. 번호가 거꾸로 꽂혀 있으면 뒤집어서 맞춘다(lane reversal).
5. **"일단 천천히 말하자" (L0 @ 2.5 GT/s)**: 처음엔 **모든 세대가 공통으로 아는 가장 느린 속도(Gen1)** 로 대화를 시작한다.
6. **"우리 둘 다 빨리 말할 수 있네? 속도 올리자" (Recovery → speed change)**: 서로의 능력(TS 안의 "지원 속도" 칸)을 확인했으니 잠깐 멈추고 속도를 바꾼다.
7. **"빨리 말하니까 발음이 뭉개진다, 입 모양을 조정하자" (Equalization, 8 GT/s 이상)**: 고속에서는 선로가 신호를 뭉갠다. 그래서 "앞부분을 좀 세게(preshoot), 뒷부분은 약하게(de-emphasis)" 같은 송신 필터 값을 **서로 상대에게 요청하면서** 맞춘다. 이게 Phase 0–3.
8. **"이제 진짜 업무 시작" (Data Link Layer up = DL_Active)**: 물리 채널이 맞은 뒤, 한 단계 위 계층이 "내 수신 버퍼는 이만큼 비어 있어"(flow control credit)를 교환하면 **DL_Active**. 이때 Link Status의 **DLActive 비트**가 1이 되고, 그 다음에야 소프트웨어가 config read로 장치를 찾을(열거) 수 있다.

```text
     칩 (Root Complex)                               카드 / 다른 칩
 +----------------------+                      +----------------------+
 |  Root Port (DSP)     |  lane0  TX ===> RX   |  Endpoint (USP)      |
 |  = Downstream Port   |  lane0  RX <=== TX   |  = Upstream Port     |
 |  (링크를 "아래로"    |  lane1  ...          |  (링크를 "위로"      |
 |   내려보내는 쪽)     |  laneN  ...          |   올려보내는 쪽)     |
 +----------+-----------+                      +----------+-----------+
            |  REFCLK 100 MHz (common clock일 때 같은 소스) |
            +-----------------------+----------------------+
                                    |
                PERST#  (보드 신호, 칩/카드 리셋 해제) ------+
```

- **lane** = 송신용 차동쌍 1개 + 수신용 차동쌍 1개 (총 4가닥). x4 = lane 4개.
- 클럭선이 따로 없다: 데이터 안에 클럭이 묻혀 있고(embedded clock), 받는 쪽 **CDR**(Clock Data Recovery)이 데이터 전이(0→1, 1→0)로 클럭을 복원한다. 그래서 "전이가 충분한 패턴"(TS1/TS2, scrambling, 8b/10b)이 필요하다.

### 1.2 PCIe의 3층 구조 — 링크 트레이닝은 맨 아래층 일

```text
  소프트웨어(드라이버, lspci)  : config read/write, MMIO, DMA
 ------------------------------------------------------------------
  Transaction Layer (TL)      : TLP (Transaction Layer Packet: "메모리 읽어줘/써줘/config 읽어줘")
 ------------------------------------------------------------------
  Data Link Layer (DLL)       : DLLP (ACK/NAK, flow-control credit, 전원관리 메시지), LCRC, 재전송
                                 -> 초기화 끝나면 DL_Active  (Link Status bit13)
 ------------------------------------------------------------------
  Physical Layer (PHY)        : LTSSM, ordered set(TS1/TS2/EIOS/SKP...), 인코딩, equalization
                                 -> L0 도달 = "선로가 쓸 만해졌다"
 ------------------------------------------------------------------
  전기: 차동쌍, AC coupling 캐패시터, REFCLK, PERST#, 전원 (보드 / board validation 영역)
```

- **LTSSM이 L0에 가야 DLL이 시작**하고, **DLL이 DL_Active여야 TLP(config read 포함)가 흐른다**. 그래서 "링크가 L0에 못 가면 endpoint는 lspci에 안 나온다" — 상대에게 config read TLP를 보낼 방법 자체가 없다.

### 1.3 링크 트레이닝이 결정하는 6가지

| 무엇을 | 어디서 결정 | 실패하면 보이는 증상 |
|---|---|---|
| 상대가 있나 (어느 lane에) | Detect | endpoint 안 보임, root port `DLActive-` |
| 비트/심볼 경계 | Polling (TS1/TS2) | Detect↔Polling 반복 |
| 극성(+/- 뒤집힘) | Polling (자동 보정) | 보통 자동 해결, 안 되면 해당 lane 실패 |
| link 번호, lane 번호, **폭(width)**, lane reversal | Configuration | **width downgrade** (x16 → x8, x4 → x1) |
| **속도(speed)** | L0 이후 Recovery (speed change) | **speed downgrade** (Gen4 cap인데 Gen3/Gen1) |
| 송신 필터 값 (preset/coefficient) | Recovery.Equalization (8 GT/s 이상) | Gen3+ 진입 실패 → 낮은 속도에 머묾, 또는 L0↔Recovery 반복 + AER Receiver Error |

### 1.4 전체 타임라인 한 장

```text
 시간 ──────────────────────────────────────────────────────────────────────────────►

 PWR    ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
            |<------- T_PVPERL >= 100 ms ------->|
 REFCLK ________/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/
                              |<- T_PERST-CLK >= 100 us ->|
 PERST# ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|____________ (active-low: 여기서 "해제")
                                                    ^ 리셋 해제
 LTSSM                                              |<20ms>| Detect -> Polling -> Config -> L0(2.5G)
                                                                -> Recovery(speed, EQ ph0-3) -> L0(목표)
 DLL                                                                                 InitFC1 -> InitFC2 -> DL_Active
 SW                     (<=5 GT/s 포트: 리셋 해제 후 100 ms / >5 GT/s 포트: 링크 학습 완료 후 100 ms) -> config read
                         장치가 아직 준비 안 됐으면 RRS(=CRS) 응답 -> 재시도, 1.0 s 안에 준비가 원칙
```

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| Link | — | 두 포트 사이의 점대점(point-to-point) 연결 하나 | PCIe는 버스가 아니라 링크의 트리 |
| Lane | — | TX 차동쌍 + RX 차동쌍 한 세트 | x1/x4/x16 = lane 수 = 폭(width) |
| DSP | Downstream Port | 링크를 아래(카드 쪽)로 내려보내는 포트: root port, switch의 아래쪽 포트 | **Retrain/Link Disable/Target Speed는 DSP에서 건다**, LTSSM Configuration을 주도 |
| USP | Upstream Port | 링크를 위로 올려보내는 포트: endpoint, switch의 위쪽 포트 | DSP가 붙인 번호를 받아들이는 쪽 |
| LTSSM | Link Training and Status State Machine | 물리층의 상태 기계: Detect → Polling → Configuration → L0 … | "L0 전에 무슨 일이?"의 정답 그 자체 |
| L0 | — | 정상 동작 상태 (데이터 송수신 가능) | 목표 상태. 저전력 L0s/L1/L2와 구분 |
| REFCLK | Reference Clock | 100 MHz 기준 클럭 | 없거나 지터가 크면 Polling에서 lock 실패 |
| Common clock | — | 양쪽이 같은 REFCLK 소스를 씀 | Link Control bit6 CCC로 SW가 알림, Link Status bit12 SLC로 슬롯이 알림 |
| SRNS / SRIS | Separate Refclk with No SSC / with Independent SSC | 양쪽이 서로 다른 클럭을 씀 (SSC 없음 / 각자 SSC) | SRIS는 SKP를 더 자주 보내야 함, 지원 안 하는 쪽과 섞으면 링크 불안정 |
| SSC | Spread Spectrum Clocking | EMI를 줄이려 클럭 주파수를 살짝 흔듦 (0~-0.5%) | 클럭 구조 선택에 영향 |
| PERST# | PCI Express Reset (active-low) | 보드가 카드/칩에 주는 리셋 선. `#` = 0일 때 활성 | 해제 타이밍이 틀리면 링크가 아예 안 뜸 — **보드 검증 단골** |
| Fundamental / Conventional Reset | — | 전원 켜짐(cold) / PERST#(warm) 리셋 = 기본 리셋. Hot reset까지 합쳐 Conventional | 리셋 후 100 ms 규칙의 기준점 |
| Electrical Idle (EI) | — | 차동 전압을 거의 0으로 둔 "조용한" 상태 | 상태 전환·속도 변경 시 사용, EI 탈출이 Detect/Polling 진입 신호 |
| Receiver detection | — | TX가 공통모드 전압을 바꾸고 충전 시간으로 상대 termination 유무를 판단 | Detect의 실체. 상대 전원/AC cap 문제면 여기서 실패 |
| Ordered Set (OS) | — | 물리층끼리만 주고받는 고정 패턴 (TLP 아님) | TS1/TS2/EIOS/EIEOS/FTS/SKP/SDS |
| TS1 / TS2 | Training Sequence 1 / 2 | 링크 학습용 16-심볼 인사말. TS1 = 제안, TS2 = 확인 | link#, lane#, N_FTS, 지원 속도, 제어 비트를 실어 나름 |
| EIOS / EIEOS | Electrical Idle Ordered Set / EI Exit Ordered Set | "이제 조용히 할게" / "이제 깨어난다" 신호 | L0s/L1 진입, 속도 변경, 128b/130b block 정렬 |
| FTS / N_FTS | Fast Training Sequence / 필요한 FTS 개수 | L0s에서 빨리 돌아올 때 쓰는 짧은 학습 패턴, 몇 개 필요한지 TS1/TS2로 알려줌 | L0s exit latency |
| SKP OS | Skip Ordered Set | 양쪽 클럭 차이(ppm)를 메우는 "버려도 되는" 패턴 | SRIS에서 빈도↑ |
| Bit lock | — | CDR이 비트 경계를 맞춤 | Polling 첫 단계 |
| Symbol lock | — | 8b/10b에서 COM(K28.5) 문자로 10비트 글자 경계를 맞춤 | Gen1/2 |
| Block lock | — | 128b/130b에서 2비트 sync header + EIEOS로 130비트 블록 경계를 맞춤 | Gen3–5 |
| 8b/10b | — | 8비트를 10비트로 보내 전이·DC 균형 보장 (20% 손해) | Gen1/2 |
| 128b/130b | — | 128비트 + 2비트 헤더, scrambling으로 전이 확보 (~1.5% 손해) | Gen3–5 |
| FLIT / PAM4 / FEC | Flow control unIT / 4-level Pulse Amplitude Modulation / Forward Error Correction | Gen6: 한 심볼에 2비트(4레벨), 256 B 고정 크기 flit 단위 전송, 가벼운 FEC + CRC | 에러 동작과 검증 방법이 바뀜 (LP17) |
| GT/s | Giga-Transfers per second | 선로 위 초당 전송 횟수 (인코딩 전) | 2.5/5/8/16/32/64 = Gen1–6 |
| Recovery | — | L0에서 잠깐 나와 재동기화·속도 변경·equalization 하는 상태 | speed change의 통로, 많으면 링크가 불안정하다는 신호 |
| Equalization (EQ) | Link Equalization | 고속에서 송신 FIR 필터 값을 상대와 협상해 맞추는 과정 | 8 GT/s 이상 필수, Phase 0–3 |
| Preset (P0–P10) | Transmitter Preset | 미리 정한 송신 필터 조합 11개 (preshoot / de-emphasis) | EQ의 시작점, 애널라이저에 찍힘 |
| Preshoot / De-emphasis | — | 비트 앞부분 강조(pre-cursor) / 같은 비트 반복 시 뒤를 낮춤(post-cursor) | 선로의 고주파 손실 보상 |
| CTLE / DFE | Continuous Time Linear Equalizer / Decision Feedback Equalizer | 수신 쪽 보상 회로 | 수신기가 "이 preset이 좋다"를 판단하는 근거 |
| Lane reversal | — | lane 0..N 순서가 거꾸로 배선된 것을 Configuration에서 뒤집어 맞춤 | 보드 라우팅 자유도, 지원 안 하면 width 실패 |
| Polarity inversion | — | 차동쌍 +/- 가 바뀐 것을 수신 쪽이 자동 반전 | Polling에서 처리 |
| Width downgrade | — | 일부 lane 실패 시 더 좁은 폭으로 링크 형성 | `Width x4 (downgraded)` |
| DLL / DL_Active | Data Link Layer / Data Link Layer Link Active | 물리층 위 계층, flow control 초기화 끝난 상태 | **Link Status bit13 = lspci `DLActive+`** |
| InitFC1 / InitFC2 | Flow Control Initialization DLLP 1/2 | 버퍼(credit) 크기를 Posted/Non-posted/Completion별로 교환 | FC init이 끝나야 DL_Active |
| LT (Train) | Link Training | Link Status bit11: 지금 학습 중 | 계속 1이면 학습이 안 끝남 |
| LBMS / LABS | Link Bandwidth Management Status / Link Autonomous Bandwidth Status | 속도/폭이 바뀌었음을 알리는 RW1C 비트 (SW 요청 또는 신뢰성 문제 / 하드웨어 자율) | 런타임 downtrain 감지, Linux `bwctrl` |
| RRS (= 옛 CRS) | (Configuration) Request Retry Status | "아직 준비 중, 다시 물어봐" config 응답 | 링크는 떴는데 장치 FW가 늦을 때 |
| Analyzer / Interposer | Protocol Analyzer | 링크 중간에 꽂아 TS1/TS2, DLLP, TLP를 시간순으로 캡처 | LTSSM을 "직접" 보는 유일한 외부 수단 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 전기적 전제조건 — 보드 쪽 (board validation 영역 ★)

| 항목 | 규칙 / 수치 | 출처 · 비고 |
|---|---|---|
| 전원 → PERST# 해제 | **T_PVPERL ≥ 100 ms** (전원 안정 후 PERST# inactive까지) | CEM r5.1 §2.9.2 — 커널 `PCIE_T_PVPERL_MS 100` (`drivers/pci/pci.h`) [확인됨][1] |
| REFCLK → PERST# 해제 | **T_PERST-CLK ≥ 100 µs** (REFCLK 안정 후 PERST# inactive) | CEM r5.1 §2.9.2 — 커널 `PCIE_T_PERST_CLK_US 100` [확인됨][1] |
| PERST# 해제 → Detect | 컴포넌트는 Fundamental Reset 종료 후 **20 ms 안에 LTSSM Detect** 진입 | Base spec (AMD PG055가 인용) [확인됨][6] |
| 리셋 → 첫 config 요청 | DSP가 **≤5 GT/s**: Conventional Reset 종료 후 **≥100 ms** / DSP가 **>5 GT/s**: **링크 학습 완료 후 ≥100 ms** | PCIe r6.0 §6.6.1 — 커널 `PCIE_RESET_CONFIG_WAIT_MS 100` 주석에 원문 [확인됨][1] |
| 장치 준비 | Conventional Reset 후 **1.0 s 안에** 준비가 원칙, 그 동안 **RRS**로 연장 가능 (상한 없음) | 커널 `PCI_RESET_WAIT 1000`, `PCIE_RESET_READY_POLL_MS 60000` (60 s까지 기다림) [확인됨][2] |
| Retrain 완료 대기 | 커널은 Retrain 후 **1000 ms** 까지 기다림 | `PCIE_LINK_RETRAIN_TIMEOUT_MS 1000` [확인됨][1] |
| 컨트롤러 드라이버 link-up 대기 (DWC) | **10회 × 90 ms** 폴링 → 실패 시 `"Phy link never came up"`, 성공 + max speed > Gen2면 추가 100 ms | `dw_pcie_wait_for_link()` [확인됨][4] |

- **REFCLK 구조 3가지**:
  - **Common clock**: 같은 100 MHz 소스(필요시 SSC 포함)를 양쪽에 분배. 가장 흔함 (PC 슬롯, 대부분 SoC 보드).
  - **SRNS** (Separate Refclk, No SSC): 각자 클럭, SSC 없음 → ppm 차이만 SKP로 흡수.
  - **SRIS** (Separate Refclk, Independent SSC): 각자 클럭 + 각자 SSC → 주파수 차가 커서 SKP를 훨씬 자주 보내야 함. 양쪽 모두 SRIS 지원/설정이어야 함 — 케이블·자동차 모듈 간 연결에서 나옴 [추정].
  - SW 관점: Link Control **bit6 CCC (Common Clock Configuration)** 를 양쪽에 쓰고 retrain하면 L0s/L1 exit latency가 짧아진다(공통 클럭이면 재동기가 빠름). Link Status **bit12 SLC (Slot Clock Configuration)** = "슬롯이 플랫폼 클럭을 쓴다".
- **SoC 통합에서 실제로 하는 일** (LP05에서 자세히): 클럭·전원 on → PHY 초기화 → **PERST# GPIO 해제 (DT `reset-gpios`)** → 컨트롤러 wrapper의 **LTSSM enable 비트** 세트 (예: Qualcomm `PARF_LTSSM` 의 `LTSSM_EN` bit8, Rockchip `PCIE_CLIENT_ENABLE_LTSSM`) → link-up 폴링 → 열거. 커널 소스에 그대로 있음 [확인됨][4].

### 2.2 물리층 기초 — 인코딩, lock, ordered set

| 세대 | 속도 | 인코딩 | lane당 유효 대역 (커널 `PCIE_SPEED2MBS_ENC`) | lock 방식 |
|---|---|---|---|---|
| Gen1 | 2.5 GT/s | 8b/10b | 2000 Mb/s (= 250 MB/s) | bit lock + symbol lock (COM = K28.5) |
| Gen2 | 5.0 GT/s | 8b/10b | 4000 Mb/s | 동일 |
| Gen3 | 8.0 GT/s | 128b/130b | 8000×128/130 ≈ 7877 Mb/s | bit lock + **block lock** (sync header 2b + EIEOS) |
| Gen4 | 16.0 GT/s | 128b/130b | ≈ 15754 Mb/s | 동일 |
| Gen5 | 32.0 GT/s | 128b/130b | ≈ 31508 Mb/s | 동일 |
| Gen6 | 64.0 GT/s | **PAM4 + FLIT (1b/1b)** | 커널 표기 64000×1/1 | FLIT 모드: 256 B flit, FEC + CRC로 보호 |

- 커널 표 출처: `drivers/pci/pci.h` `PCIE_SPEED2MBS_ENC` [확인됨][1]. 이 값으로 `"... Gb/s available PCIe bandwidth, limited by ..."` 메시지를 계산한다(§3.6).
- **Gen6 한 줄**: 64 GT/s를 NRZ(2레벨)로 못 내서 **PAM4**(4레벨, 심볼당 2비트, 같은 32 GHz 대역)로 바꿨고, PAM4는 BER이 나빠서 **FEC**가 필수 → FEC는 고정 크기 블록이 편해서 **FLIT 모드**(256 B 단위). 링크 트레이닝 자체는 같은 LTSSM 틀이지만 Gen6 속도로 갈 때 FLIT 모드 협상이 추가되고, Link Status 2 **bit10 Flit Mode Status** (`PCI_EXP_LNKSTA2_FLIT 0x0400`)로 보인다 [확인됨][1].
- **Ordered set 종류**:

| OS | 언제 | 용도 |
|---|---|---|
| TS1 | Polling.Active, Configuration, Recovery | 파라미터 **제안** |
| TS2 | Polling.Configuration, Configuration.Complete, Recovery.RcvrCfg | 파라미터 **확인/수락** |
| EIOS | L0s/L1/L2 진입, 속도 변경 전, Disabled | "이제 전기적 idle로 간다" |
| EIEOS | EI 탈출 (Gen2+), 128b/130b 블록 정렬, EQ 중 주기적 | "깨어남" + 블록 경계 표시 |
| FTS | L0s → L0 | 빠른 재동기 (N_FTS 개) |
| SKP | L0 중 주기적 | 클럭 차 보상 (버려도 됨) |
| SDS | 128b/130b에서 L0 데이터 스트림 시작 | "이제부터 TLP/DLLP 데이터" |

### 2.3 TS1/TS2 안에는 뭐가 들어 있나 (8b/10b 기준 16 심볼)

| 심볼 | 이름 | 내용 |
|---|---|---|
| 0 | COM | K28.5 (symbol lock 기준). 128b/130b에서는 TS1 = 0x1E, TS2 = 0x2D 식별 바이트 [추정] |
| 1 | Link Number | 0–255 또는 **PAD**(아직 안 정해짐). Configuration에서 DSP가 채움 |
| 2 | Lane Number | 0–31 또는 **PAD**. Configuration.Lanenum에서 채움 |
| 3 | N_FTS | L0s 탈출에 필요한 FTS 개수 |
| 4 | Data Rate Identifier | 지원 속도 비트(2.5/5/8/16/32 GT/s), **speed_change** 비트, autonomous change / selectable de-emphasis 비트 [비트 위치는 추정] |
| 5 | Training Control | **Hot Reset**, **Disable Link**, **Loopback**, Disable Scrambling, Compliance Receive 비트 [비트 위치는 추정] |
| 6–15 | TS Identifier | TS1 = D10.2 (0x4A), TS2 = D5.2 (0x45). 8 GT/s+ EQ 단계에서는 6–9번에 preset/coefficient 정보 (EQ TS) |

- **핵심 포인트**: Hot Reset · Link Disable · Loopback은 **TS 안의 제어 비트로 상대에게 전달**된다. 그래서 root port의 Link Control `Link Disable`(bit4)이나 bridge의 Secondary Bus Reset을 세우면 LTSSM이 TS1에 해당 비트를 실어 보내고 → 상대 LTSSM이 Disabled / Hot Reset 상태로 간다 (LP11 리셋 노트와 연결).
- 출처: 필드 순서는 PCIe r6.0 §4.2.5.1 표(커널 메일링리스트 인용)와 공개 자료 [5][7]. 정확한 비트 위치는 spec 표로 재확인 필요 → 면접에서는 "symbol 4 = rates + speed_change, symbol 5 = training control (hot reset, disable, loopback)" 수준이면 충분.

### 2.4 LTSSM 전체 그림

```text
                        +--------------------------------------------------------+
                        |                                                        |
   PERST# 해제 / 리셋   v                                                        |
   ----------------> [ DETECT ]  Quiet --(12 ms 또는 EI exit)--> Active          |
                        |        (receiver termination 감지)                     |
                        | 상대 있음                                               |
                        v                                                        |
                     [ POLLING ]  Active (TS1 >= 1024개) --> Configuration (TS2) |
                        |   \--(24 ms 내 실패, 일부 lane만 EI exit 등)--> Compliance (테스트 패턴)
                        | bit/symbol(block) lock + polarity                      |
                        v                                                        |
                  [ CONFIGURATION ]  Linkwidth.Start -> Linkwidth.Accept ->      |
                        |            Lanenum.Wait -> Lanenum.Accept ->           |
                        |            Complete (TS2) -> Idle                      |
                        v                                                        |
     +------------->  [ L0 ]  <------------------------------+                   |
     |   (항상 2.5 GT/s로 첫 진입)  |  |  |                    |                   |
     |                  |          |  |  +--> [ L0s ] (한 방향씩, FTS로 복귀)    |
     |                  |          |  +-----> [ L1 ] --> (L1.1 / L1.2 substates) |
     |                  |          +--------> [ L2 ] (aux 전원만, 메인 전원 off) |
     |                  v                                                        |
     |            [ RECOVERY ]  RcvrLock -> (Speed) -> (Equalization Ph0..3) ->  |
     |                  |       RcvrCfg -> Idle                                  |
     +------------------+   speed change / 에러 후 재동기 / width 변경 / L1 탈출  |
                        |                                                        |
                        +--> [ HOT RESET ]  (TS Hot Reset 비트 / SBR) -----------+
                        +--> [ DISABLED ]   (Link Disable 비트)       ----------+
                        +--> [ LOOPBACK ]   (테스트: master가 보낸 걸 slave가 되돌림)
                        +--> 실패/타임아웃 --> DETECT 로 복귀
```

### 2.5 상태별 상세 (substate 포함)

| 상태.substate | 하는 일 | 다음으로 가는 조건 | 막히는 흔한 원인 |
|---|---|---|---|
| **Detect.Quiet** | TX는 Electrical Idle. 잠시 대기 | 12 ms 경과 또는 아무 lane이 EI 탈출 → Detect.Active [5] | 리셋이 안 풀림, LTSSM enable 안 함 |
| **Detect.Active** | TX가 각 lane에서 **receiver detection** (공통모드 전압 단계 → 충전 시간으로 상대 termination 판정) | 상대 감지된 lane 있음 → Polling. 없음 → Detect.Quiet로 반복 | 카드 미장착, 상대 전원 off, **AC coupling cap 누락/불량**, lane 단선 |
| **Polling.Active** | 감지된 모든 lane에서 **TS1 ≥ 1024개** 송신, 수신 쪽 bit lock·symbol lock | 8개 연속 TS1/TS2(link#/lane# = PAD) 수신 → Polling.Configuration. **24 ms** 타임아웃 시 조건에 따라 Polling.Compliance 또는 Detect [5] | **REFCLK 문제**, SI(신호 품질), 한쪽만 EI 탈출 |
| **Polling.Compliance** | 컴플라이언스 패턴 송신 (테스트 장비용, 또는 Enter Compliance 비트) | 테스트 종료/EI 탈출 → Polling.Active | 일부 lane만 감지되는 보드 문제로 의도치 않게 빠지기도 함 [추정] |
| **Polling.Configuration** | **TS2** 송신, **극성 반전**(필요 시) 적용 | 8개 연속 TS2 수신 + 그 후 16개 TS2 송신 → Configuration. 48 ms 타임아웃 → Detect [추정] | |
| **Configuration.Linkwidth.Start** | **DSP**가 TS1에 Link# 채워 보냄(Lane# = PAD). USP는 PAD로 응답하다가 Link#를 받음 | USP가 같은 Link#를 되돌림 → Linkwidth.Accept | lane 일부만 응답 → 폭 축소 후보 |
| **Configuration.Linkwidth.Accept** | 링크를 구성할 lane 집합 결정 | DSP가 Lane# 제안 시작 → Lanenum.Wait | |
| **Configuration.Lanenum.Wait / Accept** | DSP가 lane 0..N 번호 제안. USP는 수락하거나 **반대 순서**로 응답(lane reversal) | 양쪽 번호 일치 → Complete | lane reversal 미지원 + 역배선 → 실패 / 폭 축소 |
| **Configuration.Complete** | **TS2**로 Link#·Lane#·N_FTS·지원 속도 확정 | 8개 TS2 수신 + 16개 송신 → Idle | |
| **Configuration.Idle** | Logical Idle(데이터 0, scrambled) 송신, 128b/130b면 SDS 후 | 8개 연속 Idle 심볼 수신 + 16개 송신 → **L0**. 타임아웃(2 ms [추정]) → Detect 또는 Recovery | |
| **L0** | 정상: TLP/DLLP 송수신. **처음엔 항상 2.5 GT/s** | 속도 변경/에러/폭 변경 → Recovery. PM 요청 → L0s/L1/L2 | |
| **Recovery.RcvrLock** | TS1 송신, 다시 lock 확보 (속도 변경 시 새 속도에서) | 8개 연속 TS1/TS2 수신 → RcvrCfg (또는 EQ 필요 시 Equalization). 24 ms 타임아웃 [추정] | 고속에서 SI 부족 |
| **Recovery.Speed** | EIOS 후 Electrical Idle, **속도 변경** 수행 | 새 속도로 RcvrLock. 실패 시 **이전 속도로 되돌림** | equalization/SI 실패 → 낮은 속도로 복귀 = `(downgraded)` |
| **Recovery.Equalization** | 8 GT/s+ 처음 진입 시 **Phase 0–3** (§2.7) | 완료 → RcvrLock | preset 부적합, 채널 손실 과다 |
| **Recovery.RcvrCfg** | TS2 교환, speed_change 비트로 속도 변경 의사 확인 | 8 TS2 수신 → Idle (또는 Speed) | |
| **Recovery.Idle** | Idle 데이터 | → L0 (또는 Hot Reset/Disabled/Loopback/Configuration) | |
| **L0s** | 한 방향(TX만)씩 저전력. 복귀 시 FTS N_FTS개 | → L0 (실패 시 Recovery) | ASPM L0s 버그 → 끄고 비교 (LP10) |
| **L1** (+L1.1/L1.2) | 양방향 저전력, DLLP로 협상 (ASPM 또는 D3hot) | 탈출 → **Recovery** → L0 | L1 exit 후 링크 불안정 = 흔한 버그 |
| **L2** | 메인 전원 off, aux 전원(Vaux)만, WAKE#로 깨움 | 전원 복귀 → Detect | |
| **Disabled** | 링크 끔 (Link Control bit4 또는 TS Disable 비트) | Link Disable 해제 → Detect | |
| **Loopback** | Loopback master가 보낸 걸 slave가 되돌림 (BER 측정, 제조 테스트) | 종료 → Detect | |
| **Hot Reset** | TS Hot Reset 비트로 상대에 리셋 전파 (Secondary Bus Reset) | → Detect | |

- 1024개 TS1, 12 ms / 24 ms 수치 출처: 공개 요약 자료 [5]. 나머지 타임아웃 수치는 `[추정]` — 면접에선 "Polling 24 ms, Detect 12 ms 정도의 타임아웃이 있고 실패하면 Detect로 돌아간다" 수준이 안전.
- **DWC 컨트롤러가 보고하는 상태 코드** = 위 표와 거의 1:1 (`DETECT_QUIET 0x00 … CFG_IDLE 0x0c, RCVRY_LOCK 0x0d, RCVRY_SPEED 0x0e, RCVRY_RCVRCFG 0x0f, RCVRY_IDLE 0x10, L0 0x11, L0S 0x12, L1_IDLE 0x14, L2_IDLE 0x15, DISABLED 0x19, LPBK_ACTIVE 0x1b, HOT_RESET 0x1f, RCVRY_EQ0..3 0x20..0x23`) [확인됨][3] — 전체 표는 [ltssm_trace.c](onsitePrep/lp03/ltssm_trace.c).

### 2.6 왜 2.5 GT/s로 시작하고, 속도는 어떻게 올리나

- **왜 2.5 GT/s부터?** (1) **하위 호환**: 모든 PCIe 장치는 Gen1을 반드시 지원 → 상대 세대를 모르는 상태에서 확실히 통하는 유일한 속도. (2) **견고성**: 저속은 equalization 없이도 거의 모든 채널에서 통함 → 먼저 "대화 가능한 상태"를 만든 뒤 TS의 Data Rate Identifier로 **서로의 지원 속도를 교환**. (3) 고속 EQ 협상 자체가 동작 중인 링크(TS 교환)를 필요로 한다 (EQ Phase 0 preset 정보는 저속 TS2로 전달).
- **속도 변경 순서 (directed speed change)**: L0(2.5 GT/s) → DSP(보통)가 TS1에 **speed_change=1** + 목표 속도 → Recovery.RcvrLock → Recovery.RcvrCfg (TS2 교환) → **Recovery.Speed** (EIOS → EI → PLL/CDR 재설정) → 새 속도에서 Recovery.RcvrLock (8 GT/s 첫 진입이면 Recovery.Equalization 경유) → RcvrCfg → Idle → **L0 @ 새 속도**.
- **실패하면?** Recovery.Speed에서 새 속도 lock 실패 → **이전 속도로 되돌아가** L0. 그래서 SI 문제는 보통 "링크가 안 뜬다"가 아니라 **"낮은 속도로 뜬다"** 로 나타난다 → `LnkSta: Speed 8GT/s (downgraded)`.
- **누가 목표 속도를 정하나?** 하드웨어가 자율적으로 최고 공통 속도를 시도. SW는 DSP의 **Link Control 2 Target Link Speed [3:0]** 로 상한을 걸고 **Link Control Retrain Link(bit5)** 로 재학습을 시킨다. `Hardware Autonomous Speed Disable` (LnkCtl2 bit5, `PCI_EXP_LNKCTL2_HASD 0x0020`)로 하드웨어 자율 변경 금지 가능 [확인됨][1].
- **Linux 6.13+**: `pcie_set_target_speed()` (bwctrl.c)가 이 과정을 커널 API로 묶음 — TLS 쓰고 retrain하고 기다림. 쓰는 곳: 열 관리(PCIe thermal cooling device, `CONFIG_PCIE_THERMAL`), 그리고 **링크가 안 뜨는 장치를 2.5 GT/s로 낮춰 재시도하는 quirk** `pcie_failed_link_retrain()` (dmesg `"broken device, retraining non-functional downstream link at 2.5GT/s"`) [확인됨][3].

### 2.7 Equalization (8 GT/s 이상) — Phase 0–3, preset

**왜 필요한가**: 8 GT/s면 기본 주파수(Nyquist) 4 GHz. PCB 배선·커넥터는 고주파를 많이 깎아서(insertion loss) 비트가 옆 비트로 번진다(ISI, Inter-Symbol Interference). 그래서 **송신 쪽 3-tap FIR 필터**로 미리 찌그러뜨려 보낸다.

```text
  송신 FIR 3-tap:  C-1 (pre-cursor = "preshoot": 전이 직전 비트를 미리 강조)
                   C0  (main cursor)
                   C+1 (post-cursor = "de-emphasis": 같은 값이 반복되면 뒤를 낮춰 상대적으로 전이를 강조)

  비트열   0 0 1 1 1 0
  원래    ___|‾‾‾‾‾‾|___
  EQ 후   _ _/‾\‾‾‾\_|_      <- 전이 직후 높고, 같은 값 반복 구간은 낮게 (de-emphasis)
```

- Gen1/2는 고정 de-emphasis(-3.5 dB / -6 dB, Link Control 2의 Selectable De-emphasis)로 충분 → **협상 불필요**. 8 GT/s부터 채널마다 최적값이 달라서 **양쪽이 서로 상대 송신기 값을 요청하며 맞추는 협상**이 필요 → Recovery.Equalization.

| Phase | 누가 무엇을 | 목표 |
|---|---|---|
| **Phase 0** | (저속에서) DSP가 **EQ TS2**로 USP가 8 GT/s에서 쓸 **TX preset**(+ RX hint)을 알려줌. 값은 DSP의 **Lane Equalization Control** 레지스터(Secondary PCIe ext cap)에서 옴. 속도 변경 직후 USP는 그 preset으로 송신 | 첫 8 GT/s 송신값을 정함 |
| **Phase 1** | 양쪽이 8 GT/s에서 TS1 교환, 송신기 능력(FS/LF: full swing / low frequency 값) 교환 | **BER < 10⁻⁴** 정도로 "대화는 되는" 링크 확보 → 다음 단계 진행 가능 |
| **Phase 2** | **USP가 DSP의 송신기 값을 요청** (USP 수신기 기준으로 preset/coefficient를 바꿔 달라고 TS1로 반복 요청·평가) | DSP→USP 방향 최적화 (목표 BER 10⁻¹²) |
| **Phase 3** | **DSP가 USP의 송신기 값을 요청** (역할 반대) | USP→DSP 방향 최적화 |

- 출처: Phase 1–3의 역할(Phase 2 = USP가 DSP TX 조정 요청, Phase 3 = 반대) [확인됨][8], 11개 preset / P10 특수 [8][9]. Phase 0 세부와 BER 수치는 공개 자료 일반론 [추정].
- **16 GT/s, 32 GT/s도 각 속도마다 equalization을 다시** 한다 (예: 전부 Gen5면 8→16→32 단계마다 EQ) [9]. 16/32 GT/s preset은 Physical Layer 16.0/32.0 GT/s extended capability의 Lane Equalization Control에 있음 [추정]. Gen5에서는 "중간 속도 EQ 생략" 옵션이 추가됨 [추정].
- **Phase 2/3은 선택적**: DSP가 Phase 1 결과로 충분하다고 보면 끝낼 수 있음 [9] (Intel 문서: "Phases 2 and 3 are optional").
- **결과는 어디서 보나**: Link Status 2 — **bit1 Equalization 8.0GT/s Complete, bit2 Phase 1 Successful, bit3 Phase 2 Successful, bit4 Phase 3 Successful, bit5 Link Equalization Request** (pciutils `PCI_EXP_LINKSTA2_EQU_*` 0x02/0x04/0x08/0x10/0x20) [확인됨][10]. 16/32 GT/s 결과는 각 Physical Layer ext cap의 Status 레지스터 [추정]. 커널 `pci_regs.h`에는 LnkSta2 EQ 비트 정의가 없고 FLIT 비트만 있다 — lspci가 자체 정의로 디코드 [확인됨].
- **Preset 표 (8 GT/s, 근사값)** [추정: base spec Table 4-x로 재확인]:

| Preset | Preshoot (dB) | De-emphasis (dB) | 느낌 |
|---|---|---|---|
| P0 | 0 | -6.0 | 강한 de-emphasis |
| P1 | 0 | -3.5 | |
| P2 | 0 | -4.4 | |
| P3 | 0 | -2.5 | |
| P4 | 0 | 0 | **EQ 없음** (짧은 채널) |
| P5 | 1.9 | 0 | preshoot만 |
| P6 | 2.5 | 0 | |
| P7 | 3.5 | -6.0 | 긴 채널에 흔한 출발점 |
| P8 | 3.5 | -3.5 | |
| P9 | 3.5 | 0 | |
| P10 | 0 | 최대 boost (고정 아님) | 송신기 boost 한계 테스트용, **실링크에서 보이면 이상** [8] |

- 애널라이저 실사례: lane 0/2는 P6, lane 1/3은 P10으로 학습 → lane별 신호가 크게 달라 불안정 [8]. **"lane별 preset이 들쭉날쭉하면 채널/보드 문제를 의심"** 이 검증 엔지니어의 감각.

### 2.8 Lane reversal · polarity inversion · width 변경

- **Polarity inversion**: 차동쌍 D+/D- 가 바뀌어 배선돼도 **수신 쪽**이 Polling에서 TS1/TS2 비트 패턴이 반전된 걸 보고 자동 반전. 필수 기능 → 보드 라우팅 편의.
- **Lane reversal**: x4 링크에서 상대 lane 0..3 이 3..0 으로 연결돼도 Configuration.Lanenum에서 번호를 뒤집어 맞춤. **선택 기능** (한쪽만 지원해도 됨 [추정]) → 둘 다 미지원이면 x1로 떨어지거나 실패.
- **Width downgrade**: Detect/Configuration에서 응답 없는 lane은 빼고 더 좁은 폭(x16→x8→x4→x2→x1)으로 구성. 일부 lane이 단선·SI 불량이면 링크는 뜨지만 `Width x2 (downgraded)`.
- **Upconfigure / autonomous width change**: 런타임에 폭을 줄였다(전력 절감) 다시 늘리는 기능. Link Control **bit9 HAWD (Hardware Autonomous Width Disable)** 로 금지 가능 [확인됨][1]. 이런 하드웨어 자율 변경은 Link Status **bit15 LABS**로 보고, 신뢰성 문제로 인한 변경이나 SW retrain은 **bit14 LBMS**로 보고 → Link Capabilities **bit21 LBNC**(Bandwidth Notification Capable)이고 Link Control bit10/11 (LBMIE/LABIE) 켜면 인터럽트 → Linux **bwctrl** (§3.7).

### 2.9 L0 다음: Data Link Layer 초기화 → DL_Active

```text
  PHY: LTSSM L0 도달 ("LinkUp=1" 을 DLL에 알림)
  DLL: DL_Inactive --> DL_Init
          FC_INIT1: InitFC1-P, InitFC1-NP, InitFC1-Cpl DLLP 반복 송신 (VC0 credit: header/data)
                    상대 3종 모두 수신 -> FC_INIT2
          FC_INIT2: InitFC2-P/NP/Cpl 송신, 상대 InitFC2 (또는 UpdateFC/TLP) 수신
       --> DL_Active  ==> Link Status bit13 DLLLA = 1  (lspci "DLActive+")
  TL : 이제 TLP 가능 -> SW config read (Vendor ID) -> 열거 (LP04)
```

- **DLActive가 의미 있으려면** 그 포트의 Link Capabilities **bit20 DLLLARC** (lspci `LLActRep+`)가 1이어야 한다. 이 보고 기능은 **hotplug 가능한 DSP와 5 GT/s 초과 지원 DSP에서 필수** [추정: spec 7.5.3.6]. QEMU 실측: root port `LLActRep+`, QEMU xio3130 switch DSP는 `LLActRep-`, endpoint는 `LLActRep-` (§3.2).
- **Surprise Down**: DL_Active였던 링크가 갑자기 DL_Inactive가 되면 DSP가 **Surprise Down Error**(uncorrectable) 보고 — LnkCap bit19 SDERC(`Surprise-`/`Surprise+`)가 있어야 [확인됨][1]. 런타임 링크 사망의 대표 AER 로그 (LP09).
- **ACK/NAK + replay**: DL_Active 이후 모든 TLP는 시퀀스 번호 + LCRC. NAK 또는 replay timer 만료 → 재전송 → AER correctable `Replay Timer Timeout`, `REPLAY_NUM Rollover`, `Bad TLP`, `Bad DLLP`. 물리층 문제는 `Receiver Error`. **링크는 L0인데 correctable이 계속 늘면 margin 부족** (LP09/LP14).

### 2.10 Link 레지스터 레퍼런스 (커널 `include/uapi/linux/pci_regs.h` v6.18 대조 [확인됨][1])

| 레지스터 (PCIe cap +off) | 비트 / 마스크 | 커널 이름 | lspci 표기 | 의미 |
|---|---|---|---|---|
| Link Capabilities (+0x0C) | [3:0] | `PCI_EXP_LNKCAP_SLS` | `LnkCap: Speed` | Max Link Speed (1..6 = 2.5..64 GT/s) |
| | [9:4] | `_MLW` | `Width xN` | Max Link Width |
| | [11:10] | `_ASPMS` | `ASPM L0s L1` | ASPM 지원 |
| | 18 | `_CLKPM` | `ClockPM` | CLKREQ# 기반 클럭 PM |
| | 19 | `_SDERC` | `Surprise` | Surprise Down 보고 가능 |
| | 20 | `_DLLLARC` | `LLActRep` | DLLLA 보고 가능 (DLActive 비트 유효) |
| | 21 | `_LBNC` | `BwNot` | Bandwidth Notification 가능 |
| | [31:24] | `_PN` | `Port #` | 포트 번호 |
| Link Control (+0x10) | [1:0] | `PCI_EXP_LNKCTL_ASPMC` | `ASPM Disabled/L0s/L1` | ASPM 켜기 |
| | 4 | `_LD` | `LnkDisable` | **Link Disable** (DSP) |
| | 5 | `_RL` | (읽으면 0) | **Retrain Link** (DSP) |
| | 6 | `_CCC` | `CommClk` | Common Clock Configuration |
| | 9 | `_HAWD` | `AutWidDis` | HW 자율 폭 변경 금지 |
| | 10 / 11 | `_LBMIE` / `_LABIE` | `BWInt` / `AutBWInt` | BW 변경 인터럽트 enable |
| Link Status (+0x12) | [3:0] | `PCI_EXP_LNKSTA_CLS` | `LnkSta: Speed` | Current Link Speed |
| | [9:4] | `_NLW` | `Width xN` | Negotiated Link Width |
| | 11 | `_LT` (0x0800) | `Train` | **Link Training 진행 중** |
| | 12 | `_SLC` (0x1000) | `SlotClk` | Slot Clock Configuration |
| | 13 | `_DLLLA` (0x2000) | `DLActive` | **Data Link Layer Link Active** |
| | 14 | `_LBMS` (0x4000) | `BWMgmt` | BW 변경 (SW retrain / 신뢰성 문제), RW1C |
| | 15 | `_LABS` (0x8000) | `ABWMgmt` | 자율 BW 변경, RW1C |
| Link Capabilities 2 (+0x2C) | [7:1] | `PCI_EXP_LNKCAP2_SLS` | `Supported Link Speeds` | 지원 속도 벡터 (bit1=2.5 … bit6=64) |
| Link Control 2 (+0x30) | [3:0] | `PCI_EXP_LNKCTL2_TLS` | `Target Link Speed` | **목표 속도 상한** |
| | 4 | `_ENTER_COMP` | `EnterCompliance` | 컴플라이언스 모드 진입 |
| | 5 | `_HASD` | `SpeedDis` | HW 자율 속도 변경 금지 |
| | [9:7] | `_TX_MARGIN` | `Transmit Margin` | 송신 진폭 마진 (테스트) |
| Link Status 2 (+0x32) | 0 | (pciutils) | `Current De-emphasis Level` | -6 / -3.5 dB |
| | 1–5 | (pciutils `EQU_COMP/PHASE1-3/REQ`) | `EqualizationComplete/Phase1/2/3/LinkEqualizationRequest` | 8 GT/s EQ 결과 |
| | 10 | `PCI_EXP_LNKSTA2_FLIT` | `FltMode` | Flit Mode (Gen6) |

- **주의**: Link Control/Status 대부분은 "link를 내려보내는 포트(DSP)"에서 의미가 있다. **Retrain Link와 Link Disable은 DSP 전용** (endpoint에서는 reserved). lspci의 `(downgraded)` 표시도 **root port/DSP에서는 안 찍힌다** — pciutils `link_compare()`는 포트 타입이 Root Port / Downstream / PCIe-to-PCI bridge면 빈 문자열을 반환, LnkSta > LnkCap이면 `(overdriven)` [확인됨][10].

### 2.11 진짜 LTSSM 상태 보는 법

1. **컨트롤러 vendor 레지스터** (bare-metal에서 가장 먼저 할 일):
   - Synopsys DesignWare (DWC — NXP i.MX, Rockchip, Qualcomm, TI, Tegra, AMD MDB 등 다수 SoC): DBI 공간 **`PCIE_PORT_DEBUG0` = 0x728, bits[5:0] = LTSSM 상태** (`PORT_LOGIC_LTSSM_STATE_MASK 0x3f`, L0 = 0x11), **`PCIE_PORT_DEBUG1` = 0x72C, bit4 = link up, bit29 = link in training** — 커널 `dw_pcie_link_up()`가 이 두 비트로 link-up 판정 [확인됨][3][4].
   - Linux 6.18 debugfs: `/sys/kernel/debug/dwc_pcie_<device>/ltssm_status` → `"L0 (0x11)"` 형식 출력, 그리고 `rasdes_debug/lane_detect`, `rx_valid`, `rasdes_event_counter/*` (lane별 에러 카운터, DWC RAS DES 기능) [확인됨][4].
   - Qualcomm: `PARF_LTSSM` (LTSSM enable), Rockchip: `PCIE_CLIENT_LTSSM_STATUS` 등 wrapper 레지스터 [확인됨 정의 존재][4]. Xilinx/AMD, Cadence, PLDA는 각자 레지스터 (LP05/LP16).
2. **LTSSM 히스토리**: 일부 IP는 상태 전이 로그 FIFO/트레이스 버퍼 제공 [추정]. 없으면 **tight loop로 DEBUG0를 샘플링** → [ltssm_trace.c](onsitePrep/lp03/ltssm_trace.c) 처럼 전이열로 압축해 판정.
3. **프로토콜 애널라이저 + 인터포저** (Teledyne LeCroy, Keysight 등): TS1/TS2 내용(link#, lane#, rate, preset 요청), EIOS/EIEOS, EQ phase, DLLP(InitFC, ACK/NAK)를 시간순으로 → "누가 먼저 포기했나"를 판단. 오실로스코프와 시간 상관(time-correlated) 캡처로 lane별 preset의 전기적 효과까지 [8].
4. **표준 레지스터로 간접**: DSP `LnkSta` (`Train`, `DLActive`, speed/width), `LnkSta2` EQ 비트, AER correctable 카운터. **이게 lspci가 할 수 있는 전부**.

### 2.12 Ethernet/MIPI 비교 (짧게)

| | PCIe 링크 트레이닝 | Ethernet (예: 1000BASE-T / 10GBASE-KR) | MIPI D-PHY / C-PHY (CSI-2) |
|---|---|---|---|
| 상대 감지 | Receiver detection (termination) | Link pulse (FLP) / 신호 감지 | LP(저전력) 상태 전압 레벨 |
| 협상 | TS1/TS2로 width·lane#·speed | **Auto-negotiation** (속도·duplex·pause), KR은 link training으로 TX FIR 협상 (PCIe EQ와 매우 비슷) | 협상 거의 없음: lane 수·속도는 **DT/드라이버가 양쪽에 정적으로 설정** |
| 결과 확인 | LnkSta speed/width, DLActive | `ethtool eth0` Speed/Duplex/Link detected, PHY 레지스터 BMSR | 수신기 에러 카운터, 프레임 안 들어옴 = 설정 불일치 |
| 고속 보정 | EQ Phase 0–3 + preset | KR: coefficient update 프로토콜 | D-PHY는 deskew/calibration (고속 모드), skew 조정 |

- 면접 한 줄: "PCIe link training is closest to Ethernet auto-negotiation plus 10GBASE-KR link training: discover the partner, agree on width and speed, then tune TX equalization. MIPI D-PHY mostly doesn't negotiate — both ends are configured statically, so a mismatch shows up as no frames rather than a downgraded link."

## 3. Linux에서 볼 것

### 3.1 명령 모음

| 목적 | 명령 | 비고 |
|---|---|---|
| 트리에서 위쪽 포트 찾기 | `lspci -tv` | endpoint 바로 위 포트 = 그 링크의 DSP |
| 링크 디코드 | `sudo lspci -s <BDF> -vvv` | LnkCap/LnkCtl/LnkSta/LnkCap2/LnkCtl2/LnkSta2, root 아니면 cap 일부 안 보임 |
| 원시값 | `sudo setpci -s <BDF> CAP_EXP+0x12.w` (LnkSta), `+0x0c.l` (LnkCap), `+0x30.w` (LnkCtl2), `+0x32.w` (LnkSta2) | lspci 해석 없이 비트 확인 |
| sysfs | `/sys/bus/pci/devices/<D>/current_link_speed`, `current_link_width`, `max_link_speed`, `max_link_width` | `current_*`는 읽을 때마다 **LnkSta를 실시간으로 읽음** (`current_link_speed_show()` → `pcie_capability_read_word(PCI_EXP_LNKSTA)`) [확인됨][2] |
| 속도 상한 + retrain | `setpci -s <DSP> CAP_EXP+0x30.w=<gen>:0xf` → `setpci -s <DSP> CAP_EXP+0x10.w=0x20:0x20` | [스크립트](onsitePrep/lp03/pcie_set_speed_retrain.sh) |
| BW 변경 감시 | dmesg `bwctrl:`, `/sys/bus/pci_express/devices/*:pcie010` | 6.13+ |
| 컨트롤러 link-up 로그 | `dmesg \| grep -iE "link up\|link never\|Gen\."` | DWC: `"PCIe Gen.%u x%u link up"` / `"Phy link never came up"` |
| 진짜 LTSSM (DWC) | `cat /sys/kernel/debug/dwc_pcie_*/ltssm_status` | debugfs mount 필요 |
| 에러 카운터 | `/sys/bus/pci/devices/<D>/aer_dev_correctable` | Receiver Error / Bad TLP / Bad DLLP / Replay — LP09 |

### 3.2 실측 — QEMU virt, root port (x-speed=16, x-width=4) + QEMU NVMe

Lab: Lima VM 안 `qemu-system-aarch64 -machine virt` + lab 커널 6.18.55 + BusyBox initramfs에 Ubuntu pciutils 3.14 `lspci`/`setpci` 를 넣어 실행. 토폴로지 명령은 [run1_lp03.sh](onsitePrep/lp03/qemu_lab/run1_lp03.sh), 전체 로그 [run1_boot_lspci_iomem.log](onsitePrep/lp03/qemu_lab/run1_boot_lspci_iomem.log). QEMU `pcie-root-port`는 `x-speed=2_5/5/8/16/32/64`, `x-width=1/2/4/8/12/16/32` 속성을 지원 (기본 16 / 32) [확인됨: `-device pcie-root-port,help`].

```text
$ lspci -tv
-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge
           +-01.0-[01]----00.0  Red Hat, Inc. QEMU NVM Express Controller
           +-02.0-[02-05]----00.0-[03-05]--+-00.0-[04]----00.0  Intel Corporation 82574L Gigabit Network Connection
           |                               \-01.0-[05]----00.0  Device 1234:11e8
           \-03.0-[06]--

$ lspci -s 00:01.0 -vvv      (root port, 발췌)
	Capabilities: [54] Express (v2) Root Port (Slot+), IntMsgNum 0
		LnkCap:	Port #0, Speed 16GT/s, Width x4, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep+ BwNot+ ASPMOptComp-
		LnkCtl:	ASPM Disabled; RCB 64 bytes, LnkDisable- CommClk-
			ExtSynch- ClockPM- AutWidDis- BWInt- AutBWInt- FltModeDis-
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-
		SltCap:	AttnBtn+ PwrCtrl+ MRL- AttnInd+ PwrInd+ HotPlug+ Surprise+
		SltSta:	Status: AttnBtn- PowerFlt- MRL- CmdCplt+ PresDet+ Interlock-
		LnkCap2: Supported Link Speeds: 2.5-16GT/s, Crosslink- Retimer- 2Retimers- DRS-
		LnkCtl2: Target Link Speed: 16GT/s, EnterCompliance- SpeedDis-
		LnkSta2: Current De-emphasis Level: -6dB, EqualizationComplete- EqualizationPhase1-
			 EqualizationPhase2- EqualizationPhase3- LinkEqualizationRequest-
			 Retimer- 2Retimers- CrosslinkRes: unsupported, FltMode-
	Kernel driver in use: pcieport

$ lspci -s 01:00.0 -vvv      (그 밑의 NVMe endpoint, 발췌)
	Capabilities: [80] Express (v2) Endpoint, IntMsgNum 0
		LnkCap:	Port #0, Speed 2.5GT/s, Width x1, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp-
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-

$ setpci -s 00:01.0 CAP_EXP+0x0c.l CAP_EXP+0x10.w CAP_EXP+0x12.w CAP_EXP+0x30.w CAP_EXP+0x32.w
00300444      <- LnkCap
0000          <- LnkCtl
2011          <- LnkSta
0004          <- LnkCtl2
0000          <- LnkSta2
```

**한 줄씩 해석**:

| 줄 | 해석 |
|---|---|
| `LnkCap: Speed 16GT/s, Width x4` | root port가 **할 수 있는** 최대 = Gen4 x4 (QEMU `x-speed=16,x-width=4` 그대로). 원시값 `0x00300444`: [3:0]=4 (16 GT/s), [9:4]=0x04 (x4), bit20=1 (LLActRep+), bit21=1 (BwNot+) |
| `LLActRep+ BwNot+` | 이 포트의 DLActive 비트는 믿을 수 있음 + BW 변경 알림 가능 → Linux bwctrl 서비스가 붙음 (§3.7) |
| `LnkSta: Speed 2.5GT/s, Width x1` | 실제 협상 = **Gen1 x1**. 원시값 `0x2011`: [3:0]=1 (2.5 GT/s), [9:4]=1 (x1), bit13=1 (DLActive). 이유: 밑의 NVMe LnkCap이 `2.5GT/s x1` → **두 끝 중 작은 쪽** |
| `(downgraded)` 표시 없음 | pciutils는 **Root Port/DSP에서는 downgraded를 표시하지 않음** (상대가 원래 느릴 수 있으니까) — endpoint/USP에서만 자기 LnkCap과 비교해 표시 [10] |
| `Train- DLActive+` | 학습 끝났고 DLL up → LTSSM이 L0를 지났다는 **간접 증거** |
| `SltSta: PresDet+` | 슬롯에 카드 있음 (Presence Detect) — 링크와 별개의 신호 |
| `LnkCtl2: Target Link Speed: 16GT/s` | SW 목표 상한 = 최대치 (리셋 기본값) |
| `LnkSta2: EqualizationComplete-` | 8 GT/s 이상 안 갔으니 EQ 없음 (Gen3 이상으로 떴는데 이게 `-`면 이상) |
| endpoint `LLActRep- ... DLActive-` | endpoint(USP)에서는 DLActive 보고 능력이 없음 → **`DLActive-` 여도 정상**. 그래서 "L0 확인은 위쪽 포트에서" |

### 3.3 실측 — 빈 슬롯 root port (00:03.0, 장치 없음)

```text
		LnkCap:	Port #0, Speed 16GT/s, Width x32, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep+ BwNot+ ASPMOptComp-
		LnkSta:	Speed 16GT/s, Width x32
			TrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-
		SltSta:	Status: AttnBtn- PowerFlt- MRL- CmdCplt+ PresDet- Interlock-
$ cat /sys/bus/pci/devices/0000:00:03.0/current_link_speed  ->  16.0 GT/s PCIe   (x32)
```

- **`DLActive-` + `PresDet-`** = 링크 없음, 카드 없음. root port 자체는 **root complex 안에 있어서 항상 lspci에 보인다** (스크린 질문 4의 답).
- **함정**: LnkSta speed/width가 `16GT/s x32` 로 그럴듯하게 찍힌다 — QEMU가 빈 포트에 자기 최대값을 넣은 것 [확인됨: 실측]. 실제 하드웨어에서도 **링크가 없을 때 speed/width 필드는 의미 없다** (spec상 undefined인 경우가 많음 [추정]). sysfs `current_link_speed`도 똑같이 속는다 → **반드시 DLActive(또는 LT, 컨트롤러 link-up)를 먼저 확인**.

### 3.4 실측 — switch downstream port의 `(overdriven)`

```text
03:00.0 PCI bridge: Texas Instruments XIO3130 PCI Express Switch (Downstream)
		LnkCap:	Port #0, Speed unknown, Width x0, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp-
		LnkSta:	Speed 2.5GT/s (overdriven), Width x1 (overdriven)
			TrErr- Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-
```

- QEMU의 xio3130 DSP 모델은 LnkCap을 0으로 둔다 (QEMU 모델 한계 [확인됨: 실측]) → LnkSta(1) > LnkCap(0) 이라 pciutils가 `(overdriven)` 출력. 실제 칩에서 overdriven이 보이면 **레지스터 값이 이상하다는 뜻** (펌웨어가 LnkCap을 잘못 프로그램 / strap 오류) [추정].
- 이 DSP는 `LLActRep-` 인데 `DLActive+` → QEMU 특성. 실 HW에서는 `LLActRep-`면 DLActive 값을 근거로 쓰지 말 것.

### 3.5 실측 — sysfs 한 번에

```text
0000:00:01.0 cur=2.5 GT/s PCIe x1 max=16.0 GT/s PCIe x4      <- root port: 능력 Gen4 x4, 실제 Gen1 x1
0000:00:02.0 cur=2.5 GT/s PCIe x1 max=8.0 GT/s PCIe x4       <- root port 2 (x-speed=8)
0000:00:03.0 cur=16.0 GT/s PCIe x32 max=16.0 GT/s PCIe x32   <- 빈 슬롯! cur 값은 무의미
0000:01:00.0 cur=2.5 GT/s PCIe x1 max=2.5 GT/s PCIe x1       <- NVMe
0000:03:00.0 cur=2.5 GT/s PCIe x1 max=Unknown x0             <- QEMU DSP LnkCap=0
0000:04:00.0 cur=2.5 GT/s PCIe x1 max=2.5 GT/s PCIe x1       <- e1000e (82574L = Gen1 x1 칩)
0000:05:00.0 cur= x max= x                                   <- edu: conventional PCI (PCIe cap 없음) -> 파일 없음
```

- 자동화 팁: 링크 회귀 테스트는 **DSP의 DLActive + endpoint의 current_link_speed/width를 기대값과 비교** — 단, cur 값만 보면 빈 슬롯도 "16 GT/s"로 통과해 버린다.
- 드라이버도 자기 링크를 로그로 남김 (실측): `e1000e 0000:04:00.0 eth0: (PCI Express:2.5GT/s:Width x1) 52:54:00:12:34:56`.

### 3.6 dmesg 패턴

| 메시지 (커널 6.18 소스 기준) | 어디서 | 의미 |
|---|---|---|
| `<dev>: PCIe Gen.3 x4 link up` | DWC `dw_pcie_wait_for_link()` | 컨트롤러가 DEBUG1 link-up 확인 후 LnkSta 디코드 [확인됨][4] |
| `<dev>: Phy link never came up` | 같은 함수, 10×90 ms 실패 | **L0 못 감** → 이후 열거해도 endpoint 없음 |
| `pci 0000:01:00.0: 7.876 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x1 link at 0000:00:01.0 (capable of 31.504 Gb/s with 8.0 GT/s PCIe x4 link)` `[예시 출력]` | `pcie_report_downtraining()` → `__pcie_print_link_status()` (열거 시 endpoint/USP에 대해 자동) | **fabric 때문에 대역폭이 제한됨** + 범인 포트 BDF까지 알려줌 [확인됨: 형식 문자열][2] |
| `bwctrl: ...` | bwctrl 서비스 | BW 변경 알림 처리 (dev_fmt 접두어 `"bwctrl: "`) [확인됨][3] |
| `broken device, retraining non-functional downstream link at 2.5GT/s` | `pcie_failed_link_retrain()` quirk | 링크가 안 뜨고 LBMS가 서 있으면 2.5 GT/s로 낮춰 재시도 [확인됨][3] |
| `retraining failed` | 같은 quirk | 그래도 실패 |

- 위 "limited by" 메시지는 QEMU에서 재현 못 함 (QEMU endpoint들이 Gen1 x1만 광고해서 fabric 제한이 생기지 않음) → 형식만 소스에서 확인. 실 HW에서 이 한 줄이 **"Gen4가 Gen3로 떴다" 질문의 첫 증거**.

### 3.7 bwctrl (Bandwidth Controller, 커널 6.13+)

- 위치: `drivers/pci/pcie/bwctrl.c`, `CONFIG_PCIEPORTBUS`면 항상 빌드 (`obj-$(CONFIG_PCIEPORTBUS) += pcieportdrv.o bwctrl.o`) [확인됨][3].
- 역할 (소스 주석 요약): (1) **Bandwidth Notification 인터럽트**(LBMS/LABS)를 받아 캐시된 현재 링크 속도를 갱신 → sysfs·thermal에 반영, (2) `pcie_set_target_speed()` API로 **커널이 링크 속도를 바꾸는 통로** (thermal cooling device, failed-link quirk). LnkCap **LBNC**(`BwNot+`)가 있는 포트에만 붙음 [확인됨][3].
- 실측: QEMU에서 port service 디바이스 목록 — 이름 규칙 `<BDF>:pcie<(type-4)<<8 | service>` (`get_descriptor_id`). service 비트 = PME 0x01, AER 0x02, HP 0x04, DPC 0x08, **BWCTRL 0x10** [확인됨][3].

```text
$ ls /sys/bus/pci_express/devices/
0000:00:01.0:pcie001  0000:00:01.0:pcie002  0000:00:01.0:pcie004  0000:00:01.0:pcie010   <- root port: PME, AER, HP, BWCTRL
0000:00:02.0:pcie001  ...  0000:03:00.0:pcie204  0000:03:01.0:pcie204                  <- switch DSP(type 6): HP만
```

- 의미: root port 3개 모두 `pcie010` (bwctrl) 바인딩 — `BwNot+` 이므로. QEMU xio3130 DSP는 `BwNot-` 라 bwctrl 없음.
- **검증 관점**: 런타임 downtrain(신뢰성 문제로 HW가 속도를 낮춤)은 LBMS를 세움 → bwctrl이 잡아서 sysfs 값이 바뀐다 → 테스트는 **스트레스 전후 current_link_speed + AER correctable 차이**를 비교하면 된다.

### 3.8 실측 — setpci로 Target Link Speed + Retrain (QEMU에서는 무시됨)

```text
$ setpci -s 00:01.0 CAP_EXP+0x30.w=0x0001:0x000f     # TLS = 2.5 GT/s
$ setpci -s 00:01.0 CAP_EXP+0x10.w=0x0020:0x0020     # Retrain Link
LnkCtl=0000 LnkSta=2011 LnkCtl2=0004                 # TLS가 4 그대로 -> QEMU가 쓰기를 무시
$ setpci -s 00:01.0 CAP_EXP+0x10.w=0x0010:0x0010     # Link Disable
after LD=1: LnkSta=2011 nvme vendor=1b36             # 링크도 안 내려감, endpoint 계속 읽힘
```

- 결론 [확인됨: 실측]: **QEMU는 LTSSM을 모델링하지 않는다** — LnkSta는 시작 시 계산된 정적 값, Retrain/Link Disable/TLS 쓰기는 효과 없음. → QEMU는 **명령·sysfs·레지스터 디코딩 연습용**, 링크 동작 검증은 실 HW 필요. 로그: [run2_setpci_bar_rescan.log](onsitePrep/lp03/qemu_lab/run2_setpci_bar_rescan.log).
- 실 HW에서의 기대 동작 [미검증]: TLS=1 + RL → LnkSta `Train+` 잠깐 → `Speed 2.5GT/s`, `BWMgmt+` (SW retrain도 LBMS를 세움) → 커널처럼 LBMS를 1 써서 지움.

### 3.9 DWC debugfs `ltssm_status` `[예시 출력]`

```text
# mount -t debugfs none /sys/kernel/debug
# cat /sys/kernel/debug/dwc_pcie_a40000000.pcie/ltssm_status
L0 (0x11)
# (카드 뽑은 상태)
DETECT_QUIET (0x00)       또는 DETECT_ACT (0x01) 를 반복
```

- 형식은 `ltssm_status_show()`의 `seq_printf(s, "%s (0x%02x)\n", ...)` 와 디렉터리 이름 `dwc_pcie_%s` 에서 확인 [확인됨][4]. 장치 이름(`a40000000.pcie`)은 예시.

## 4. 꼬리 질문 체인

### 체인 1 — "What happens before L0?" (스크린 질문 그대로)

Q → 꼬리1 "What exactly is detected in Detect?" → 꼬리2 "What are TS1 and TS2 for?" → 꼬리3 "Why does the link start at 2.5 GT/s even for a Gen5 device?" → 꼬리4 "What is equalization and why only from Gen3?" → 꼬리5 "Walk me through the equalization phases."

<details><summary>모범 답 펼치기</summary>

- **Q. What happens before L0?** — "First the platform part: power rails stable, the 100 MHz reference clock running, and PERST# deasserted at least 100 ms after power is good. Within 20 ms the LTSSM enters Detect. Then Polling, where both sides exchange TS1 and TS2 ordered sets to get bit and symbol lock and fix polarity, and Configuration, where the downstream port assigns link and lane numbers and the width is settled. Then L0 at 2.5 GT/s, and a trip through Recovery to change to the target speed, with equalization from 8 GT/s up. After that the data link layer exchanges InitFC DLLPs and goes DL_Active, and only then can software send config requests."
  - 한국어 핵심: 전원·REFCLK·PERST# → Detect → Polling → Configuration → L0(Gen1) → Recovery(speed+EQ) → DL_Active → 열거.
- **꼬리1. What is detected in Detect?** — "The transmitter checks for the far-end receiver termination on each lane. It changes its common-mode voltage and measures how fast the line charges — with a 50-ohm-ish termination on the other side it charges slowly, with nothing there it's fast. It's purely electrical, no data. So a missing card, the partner not powered or still in reset, a missing AC coupling cap, or a broken lane all fail right here, and the LTSSM loops between Detect.Quiet and Detect.Active."
  - 한국어 핵심: 상대 termination을 전기적으로 감지 (데이터 아님) — 카드/전원/AC cap/단선은 여기서 걸림.
- **꼬리2. What are TS1 and TS2 for?** — "They're 16-symbol training ordered sets that only the physical layers exchange. They carry the link number, lane number, N_FTS, the supported data rates with a speed-change bit, and training control bits like hot reset, disable and loopback. TS1 is basically a proposal, TS2 is the confirmation — you move to the next state after receiving eight consecutive matching ones. And the repetitive pattern is what the receiver uses to get bit and symbol lock."
  - 한국어 핵심: TS1 = 제안, TS2 = 확인. link#, lane#, N_FTS, 지원 속도, 제어 비트(hot reset/disable/loopback).
- **꼬리3. Why start at 2.5 GT/s?** — "Because 2.5 GT/s is the only rate every PCIe device must support, so it's guaranteed to work before you know what the partner is. It's also robust enough to train without equalization. Once in L0, both sides have advertised their supported rates in the TS ordered sets, so the downstream port can do a directed speed change through Recovery. If the higher rate doesn't lock, it falls back to the previous rate — which is why signal integrity problems usually show up as a downgraded speed, not a dead link."
  - 한국어 핵심: 하위호환 + 견고성 + 능력 교환. 고속 실패 시 이전 속도로 복귀 → "downgraded".
- **꼬리4. What is equalization, why only from Gen3?** — "At 8 GT/s and above the channel loss is high enough that bits smear into each other, so the transmitter uses a three-tap FIR — preshoot and de-emphasis — and the receiver has CTLE and DFE. The right TX settings depend on the specific channel, so the two ports negotiate them: each receiver evaluates the signal and asks the other side's transmitter to change presets or coefficients. Gen1 and Gen2 just use a fixed de-emphasis, so there's nothing to negotiate. And it's done per rate — a Gen5 link equalizes at 8, 16 and 32."
  - 한국어 핵심: 고속 채널 손실 보상, 송신 FIR 값을 채널별로 협상 — Gen1/2는 고정 de-emphasis로 충분.
- **꼬리5. The phases?** — "Phase 0: at the lower rate the downstream port tells the upstream port which TX preset to use at the new rate, from its Lane Equalization Control register. Phase 1: both sides exchange TS1s at the new rate and get a link that's just good enough, around 1e-4 BER. Phase 2: the upstream port tunes the downstream port's transmitter. Phase 3: the reverse — the downstream port tunes the upstream port's transmitter, aiming for 1e-12. Phases 2 and 3 can be skipped if phase 1 is already good. The result shows up in Link Status 2: equalization complete and phase 1, 2, 3 successful."
  - 한국어 핵심: Ph0 preset 전달 → Ph1 일단 통하는 링크 → Ph2 USP가 DSP TX 조정 → Ph3 DSP가 USP TX 조정. LnkSta2 비트로 결과.

</details>

### 체인 2 — "Can you confirm L0 with lspci?"

Q → 꼬리1 "Then what does DLActive actually mean?" → 꼬리2 "Why look at the root port and not the endpoint?" → 꼬리3 "How would you get the real LTSSM state?" → 꼬리4 "What if LLActRep is minus?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Not directly. The LTSSM state isn't part of standard configuration space, so lspci has nothing to print for it. What I can see is indirect: on the root port above the device, Link Training clear, Data Link Layer Link Active set, and the negotiated speed and width. DL Active can only be set after the LTSSM reached L0 and flow control initialized, so it's a solid proxy — but it tells me the link is up now, not what happened during training or how many times it went through Recovery."
  - 한국어 핵심: 직접은 못 봄. DSP의 `Train-` + `DLActive+` + speed/width가 간접 증거.
- **꼬리1. DLActive?** — "It's bit 13 of Link Status — the data link layer state machine is in DL_Active. That means the physical layer reported link up, which is L0, and both sides exchanged InitFC1 and InitFC2 for posted, non-posted and completion credits. From that point TLPs, including config requests, can flow. If it drops while the device was in use, the port reports Surprise Down if it's capable."
  - 한국어 핵심: Link Status bit13 = DLL이 FC 초기화까지 끝낸 상태 = TLP 가능.
- **꼬리2. Why the root port?** — "Because the reporting is only required on downstream ports. Endpoints usually don't implement it — in my QEMU run the NVMe endpoint shows LLActRep minus and DLActive minus while the link is obviously working. Also, if the link is down, the endpoint isn't reachable at all, so the downstream port is the only place you can read anything. Retrain Link and Link Disable also live only on the downstream port."
  - 한국어 핵심: 보고 기능은 DSP에만 의무, endpoint는 `DLActive-`가 정상. 링크가 죽으면 endpoint는 읽을 수조차 없음.
- **꼬리3. Real LTSSM state?** — "From the controller's vendor registers. On a Synopsys DesignWare core it's PORT_DEBUG0 at offset 0x728, bits 5:0 — 0x11 is L0 — and recent Linux exposes it in debugfs as ltssm_status. On bare metal I'd sample that register in a tight loop after releasing PERST# and log the transitions, which tells me whether it's stuck in Detect, looping Detect-Polling, or bouncing in Recovery. For anything below that, like which presets were requested, I'd use a protocol analyzer with an interposer."
  - 한국어 핵심: vendor debug 레지스터 (DWC 0x728[5:0], L0=0x11) 샘플링 → 전이 로그. 더 깊이는 애널라이저.
- **꼬리4. LLActRep-?** — "Then the DLActive bit isn't valid on that port and I shouldn't rely on it. I'd fall back to Link Training being clear plus a successful config read of the device below — a vendor ID that isn't 0xFFFF — and the controller's own link-up bit. The kernel is even more conservative: in its link-wait code it polls DLLLA only if the port advertises it; otherwise it just sleeps a full second plus the delay and assumes the link came up."
  - 한국어 핵심: DLActive 무효 → LT 비트 + 아래 장치 config read 성공 + 컨트롤러 link-up 비트로 판단. 커널 `pcie_wait_for_link_delay()`는 이때 1000 ms + delay를 그냥 잔다 [확인됨][2].

</details>

### 체인 3 — "If L0 fails, does the device show up in lspci?"

Q → 꼬리1 "And if it dies after boot?" → 꼬리2 "How would you bring it back without rebooting?" → 꼬리3 "What does the kernel do with a link that never trains?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "The endpoint doesn't. Enumeration is just config reads, and config TLPs can't cross a link that isn't DL_Active, so the read to that bus returns all ones and the kernel treats vendor ID 0xFFFF as no device. The root port above it still shows up, because it's inside the root complex — and its Link Status tells you the story: DLActive minus, possibly Train stuck, and the speed and width fields aren't meaningful. On a DesignWare controller you'd also see 'Phy link never came up' in dmesg."
  - 한국어 핵심: endpoint ❌ (0xFFFF = 없음), root port ✅ + `DLActive-`. speed/width는 이때 무의미.
- **꼬리1. Dies after boot?** — "Then the pci_dev already exists, so the device is still listed, but every config read returns all ones — lspci shows 'rev ff' and with -vvv 'Unknown header type 7f'. The root port logs a Surprise Down or the driver hits completion timeouts, and if DPC is enabled the port contains the error. pciehp, if the slot is hotplug-capable, sees the link-down event and removes the device."
  - 한국어 핵심: 이미 열거된 장치는 남지만 `rev ff` / `Unknown header type 7f`. AER Surprise Down / DPC / pciehp.
- **꼬리2. Bring it back?** — "In order of intrusiveness: retrain from the downstream port — set Retrain Link, maybe with a lower Target Link Speed to bisect; if the device is gone, remove the stale device in sysfs and rescan; then a hot reset through the bridge's secondary bus reset, or toggling Link Disable; and on a SoC, re-sequencing PERST# through the controller driver. Each one isolates a different layer — PHY training, software state, or the device's own reset."
  - 한국어 핵심: retrain (+ TLS 낮추기) → remove/rescan → SBR/Link Disable → PERST# 재시퀀스.
- **꼬리3. Kernel and a link that never trains?** — "The controller driver times out on link-up — DesignWare polls about 900 ms — and enumeration continues without the endpoint. There's also a quirk in the PCI core: if a downstream link fails to come up but the bandwidth-management status bit shows the hardware kept retrying, the kernel forces the target speed to 2.5 GT/s and retrains, printing 'broken device, retraining non-functional downstream link at 2.5GT/s'. That's the same Gen1 bisect I'd do by hand."
  - 한국어 핵심: link-up 타임아웃 후 endpoint 없이 진행. 커널 quirk가 2.5 GT/s로 낮춰 재시도.

</details>

### 체인 4 — "The link comes up at Gen3 instead of Gen4. What do you check?"

Q → 꼬리1 "How do you force Gen1 to bisect?" → 꼬리2 "What would an analyzer show you?" → 꼬리3 "Could it be software?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "First, is it really a downgrade: compare Link Capabilities on both ends and Link Capabilities 2 supported speeds — the link only goes as fast as the slower side, and check Target Link Speed on the downstream port isn't capped. Then the kernel's 'limited by' message tells me which port is the bottleneck. If both sides are Gen4-capable, it's almost always equalization or signal integrity at 16 GT/s: I'd look at the 16 GT/s equalization status, AER correctable errors like Receiver Error, retimers in the path, and on the controller side the LTSSM history — did it enter Recovery.Speed for 16 and fall back."
  - 한국어 핵심: 양쪽 LnkCap/LnkCap2, TLS 상한, "limited by" 로그 → 그다음 16 GT/s EQ 상태·AER·retimer·LTSSM 히스토리.
- **꼬리1. Force Gen1?** — "On the downstream port, write Target Link Speed in Link Control 2 to 1, then set Retrain Link in Link Control, wait for Link Training to clear, and read Link Status. With setpci that's CAP_EXP+0x30.w=1:f and CAP_EXP+0x10.w=20:20. Then step up one generation at a time. If Gen1 and Gen2 are rock solid and it fails exactly at Gen3, it points at equalization; if even Gen1 has errors, it's more basic — refclk, a lane, or a connector."
  - 한국어 핵심: LnkCtl2 TLS=1 + LnkCtl RL → LT 해제 대기 → 한 단계씩 올림. 어느 속도에서 깨지나가 원인 분류.
- **꼬리2. Analyzer?** — "The TS1 and TS2 exchange with the advertised rates and the speed-change bit, so I can see whether the downstream port even asked for 16 GT/s. Then the equalization phases: which presets were requested per lane, whether phase 2 and 3 converged or timed out, and whether one lane trained to a weird preset like P10. And time-correlated with a scope, the electrical eye on that lane."
  - 한국어 핵심: speed_change 요청 여부, lane별 preset, EQ phase 타임아웃, 이상 preset(P10).
- **꼬리3. Software?** — "Sometimes. Firmware or the device tree can cap max-link-speed, a driver or the thermal framework can lower Target Link Speed through bwctrl, and Hardware Autonomous Speed Disable or ASPM settings change behavior. Also some Gen4 cards need a newer firmware or a specific preset in the root port's Lane Equalization Control. So I'd rule out configuration before blaming the board."
  - 한국어 핵심: DT `max-link-speed`, bwctrl/thermal의 TLS 변경, preset 레지스터 설정도 의심.

</details>

### 체인 5 (추가) — "The link is x8 instead of x16."

Q → 꼬리1 "Lane reversal vs polarity inversion?" → 꼬리2 "How do you find which lane is bad?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Width is decided in Detect and Configuration — lanes that don't detect a receiver or don't get lock are dropped and the link forms on the widest working subset. So I'd check both ends' Max Link Width, any bifurcation setting on the root port, and then per-lane status: which lanes detected a receiver, which got lock. On DesignWare there's a lane_detect debugfs entry, and lane-error counters in the Secondary PCIe capability's Lane Error Status."
  - 한국어 핵심: width는 Detect/Configuration에서 결정. 양쪽 MLW, bifurcation, lane별 detect/lock 확인.
- **꼬리1.** — "Polarity inversion is the plus and minus of one differential pair being swapped — mandatory, the receiver just inverts in Polling. Lane reversal is the lane order being backwards across the link, 0 to 15 landing on 15 to 0 — that's optional and handled in Configuration's lane-number step; if neither side supports it you may train at x1 or fail."
  - 한국어 핵심: polarity = 쌍 내부 +/- (필수, 자동) / lane reversal = lane 순서 (선택).
- **꼬리2.** — "Per-lane receiver detect and lock status from the controller, Lane Error Status in the Secondary PCIe extended capability, and then electrical: continuity, AC caps, and the eye per lane on a scope or with lane margining. Swapping the card into a known-good slot separates board from device."
  - 한국어 핵심: 컨트롤러 lane별 상태 + Lane Error Status + 전기 측정 + 슬롯 교차.

</details>

### 체인 6 (추가) — "Link is at full speed but you see correctable errors climbing."

Q → 꼬리1 "What is the LTSSM doing?" → 꼬리2 "How would you quantify margin?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Correctable errors like Receiver Error, Bad TLP, Bad DLLP and replay timeouts mean the link is working but retransmitting — the data link layer is covering for a marginal physical layer. I'd track the AER counters over time and under stress, temperature, and different traffic, and check whether the bandwidth-management bit is getting set, which would mean the hardware is retraining or downgrading on its own."
  - 한국어 핵심: correctable 증가 = 재전송으로 버티는 marginal 링크. 추세·조건별로 추적, LBMS 확인.
- **꼬리1.** — "Probably bouncing L0 to Recovery and back — each Recovery is a retrain that stalls traffic for microseconds. On the controller you'd see recovery entries in the LTSSM history or a recovery counter; Linux doesn't show that in lspci. A storm of recoveries is a classic sign of marginal equalization or a reference clock issue."
  - 한국어 핵심: L0↔Recovery 반복 (Recovery storm). vendor LTSSM 로그/카운터로 확인.
- **꼬리2.** — "Lane margining at the receiver, which is a standard capability from 16 GT/s, gives timing and voltage margin per lane without an external scope; plus BER tests with loopback or compliance patterns, and shmoo across voltage, temperature and preset. The deliverable is a margin number per lane per corner, not just pass or fail."
  - 한국어 핵심: Lane Margining at Receiver(16 GT/s+ 표준), loopback BER, 전압·온도·preset shmoo → lane별 margin 수치.

</details>

### 체인 7 (추가) — "How would you automate link bring-up validation on new silicon?"

Q → 꼬리1 "What would you log on a failure?" → 꼬리2 "Bare metal or Linux?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "A loop over reset types — cold power cycle, PERST#, hot reset, link disable — times N, and over every supported speed and width, including forcing each Target Link Speed. Each iteration checks link-up time against a budget, the final speed and width against expected, DL Active, equalization status, and that AER counters didn't move. Run it across voltage and temperature corners, and report a pass rate and a time-to-L0 distribution, not just one pass."
  - 한국어 핵심: 리셋 종류 × N회 × 모든 speed/width, link-up 시간·결과·EQ·AER 체크, 코너별 통계.
- **꼬리1.** — "The LTSSM transition trace from the vendor debug register sampled from PERST# release, Link Status and Status 2 on both sides, the AER registers, per-lane equalization results, temperature and voltage at that moment, and the iteration number so I can reproduce. Ideally a trigger out to an analyzer when the state machine goes somewhere unexpected."
  - 한국어 핵심: PERST# 이후 LTSSM 전이 로그, 양쪽 LnkSta/LnkSta2, AER, lane별 EQ, 환경값, 반복 번호, 애널라이저 트리거.
- **꼬리2.** — "Both. Bare metal gives me deterministic timing and direct access to PERST# and the controller registers, which is what silicon validation needs first. Linux is for system-level things — driver, hotplug, error recovery, power management — and it's where the customer software runs. The same register decode works in both; in Linux it's setpci and sysfs, bare metal it's ECAM pointers."
  - 한국어 핵심: bare-metal = 결정적 타이밍·레지스터 직접 접근 (실리콘 검증 1차), Linux = 시스템 수준.

</details>

## 5. Don 경험 연결

- "At Solidigm I worked on a PCIe Gen6 SSD controller from the endpoint side — during pre-silicon FPGA bring-up of the Cortex-R cores and the PCIe/NVMe subsystem, link-up was the first gate before any NVMe traffic, so I'm used to reading the controller's own LTSSM and link status registers rather than waiting for an OS to tell me. [구체 사례: 링크가 Gen[숫자 채우기]에서 멈췄던 원인 / 걸린 시간 채우기]"
- "At SK hynix I automated chamber reliability tests on NAND and PCIe Gen3/4/5 high-speed interfaces — that's exactly the 'link-up N times across temperature and voltage, record speed, width and error counters' loop, plus shmoo plots to show margin. [숫자 채우기: 반복 횟수 / 코너 수 / 찾은 이슈]"
- "At Apple I root-caused PCIe issues on wireless chipset integration with a scope and a protocol analyzer — so when lspci only tells me DLActive is clear, my next step is the analyzer trace: did TS1s go out, who stopped answering. [사례 한 줄 채우기]"
- 브릿지 문장 (스크린 회고 반영): "I've used the macOS tools more than lspci, but they all read the same Link Status register in the PCIe capability — so let me answer at the register level."

## 6. 실습 / 코드

### 6.1 [pcie_set_speed_retrain.sh](onsitePrep/lp03/pcie_set_speed_retrain.sh) — TLS 설정 + retrain + 읽기 (POSIX sh, BusyBox에서도 동작)

- 하는 일: (1) PCIe cap +0x02 port type 확인 (Root Port 4 / DSP 6만 허용), (2) LnkCap 최대 속도 넘으면 거부, (3) **진행 중인 학습이 끝날 때까지 대기** (PCIe r6.1 §7.5.3.7 implementation note — 커널 `pcie_retrain_link()`와 같은 이유), (4) LnkCtl2 TLS 쓰고 **readback으로 실제 반영 확인**, (5) Retrain Link, LT 해제 대기(최대 ~1 s), DLLLA 대기, (6) LBMS RW1C 클리어, (7) 링크 다운이면 speed/width를 믿지 않고 "DOWN" 보고, (8) 아래 bus의 endpoint sysfs 값 출력.
- **검증 상태**: QEMU virt에서 **스크립트 로직 실행 확인** [확인됨], 실제 링크 변경은 QEMU가 지원하지 않아 **실 HW 동작은 [미검증]**. 실측 출력 ([run3_retrain_script.log](onsitePrep/lp03/qemu_lab/run3_retrain_script.log)):

```text
### rp1 -> Gen1
port 00:01.0 (Root Port): LnkCap Gen4 x4 DLLLA-reporting=1
before : LnkSta=2011 [speed=2.5GT/s width=x1 LT=0 DLLLA=1 LBMS=0 LABS=0]  LnkCtl2=0004 (TLS=16GT/s)
WARN   : TLS write did not stick (LnkCtl2 reads 0004). Emulated port (QEMU) or HW-locked?
after  : LnkSta=2011 [speed=2.5GT/s width=x1 LT=0 DLLLA=1 LBMS=0 LABS=0]  LT-seen-during-retrain=0
RESULT : link now at 2.5GT/s (requested Gen1)
below  : 0000:01:00.0 current_link_speed=2.5 GT/s PCIe width=x1 vendor=0x1b36
### rp1 Gen5 (above cap)
ERROR: port max speed is Gen4 (16GT/s); refusing Gen5
### endpoint 01:00.0 (must refuse)
ERROR: 01:00.0 is port type 0, not a Root Port(4)/Downstream Port(6). Retrain Link only exists on downstream ports.
### empty rp3 00:03.0 Gen1
before : LnkSta=0204 [speed=16GT/s width=x32 LT=0 DLLLA=0 LBMS=0 LABS=0]  LnkCtl2=0004 (TLS=16GT/s)
RESULT : link DOWN (DLLLA=0) -> speed/width fields are not meaningful; check LTSSM debug reg / PERST# / refclk
below  : no function on bus 06 (link down => endpoint not enumerable)
```

### 6.2 [ltssm_trace.c](onsitePrep/lp03/ltssm_trace.c) — DWC LTSSM 샘플 → 전이 로그 + 판정

- bare-metal에서 `readl(dbi + 0x728) & 0x3f` 를 PERST# 해제 직후부터 샘플링했다고 가정, 중복 제거 후 전이열을 출력하고 6가지로 분류: NO_RECEIVER(Detect만), POLLING_LOOP, COMPLIANCE, CONFIG_FAIL, EQ_FAIL(EQ 후 Detect로 추락), RECOVERY_STORM(L0↔Recovery 반복), L0_STABLE. 상태 코드는 커널 `enum dw_pcie_ltssm` 그대로.
- macOS에서 spec 플래그로 컴파일·실행 **ALL PASS** [확인됨]. 출력 발췌:

```text
-- healthy bring-up
   DETECT_QUIET(0x00) -> DETECT_ACT(0x01) -> POLL_ACTIVE(0x02) -> POLL_CONFIG(0x04) -> CFG_LINKWD_START(0x07)
    -> CFG_LINKWD_ACEPT(0x08) -> CFG_LANENUM_WAI(0x09) -> CFG_LANENUM_ACEPT(0x0a) -> CFG_COMPLETE(0x0b) -> CFG_IDLE(0x0c)
    -> L0(0x11) -> RCVRY_LOCK(0x0d) -> RCVRY_SPEED(0x0e) -> RCVRY_EQ0(0x20) -> RCVRY_EQ1(0x21)
    -> RCVRY_EQ2(0x22) -> RCVRY_EQ3(0x23) -> RCVRY_LOCK(0x0d) -> RCVRY_RCVRCFG(0x0f) -> RCVRY_IDLE(0x10)
    -> L0(0x11)
   transitions=21  L0 entries=2  L0->Recovery=1  => L0_STABLE
-- empty slot / PERST# held
   DETECT_QUIET(0x00) -> DETECT_ACT(0x01) -> DETECT_QUIET(0x00) -> DETECT_ACT(0x01) -> ...
   transitions=8  L0 entries=0  L0->Recovery=0  => NO_RECEIVER (stuck in Detect)
-- recovery storm
   ... -> L0(0x11) -> RCVRY_LOCK(0x0d) -> RCVRY_RCVRCFG(0x0f) -> RCVRY_IDLE(0x10) -> L0(0x11) -> ...
   transitions=28  L0 entries=6  L0->Recovery=5  => RECOVERY_STORM (L0<->Recovery)
ALL PASS (0 failures)
```

- 샘플 데이터는 합성 (실 실리콘 trace 아님) — 판정 규칙의 연습용. 실 HW에서는 샘플링 주기가 상태 체류 시간보다 길면 짧은 상태(예: CFG_LINKWD_ACEPT)를 놓친다 → 하드웨어 trace FIFO가 있으면 그걸 우선.

### 6.3 QEMU 랩 재현 레시피

1. VM `/tmp/LP03`에 [mkinit_lp03.sh](onsitePrep/lp03/qemu_lab/mkinit_lp03.sh) 복사·실행 → lab BusyBox rootfs + Ubuntu `lspci`/`setpci` + 공유 라이브러리 + `pci.ids` 로 initramfs 생성. **주의**: BusyBox rootfs의 `/usr/bin/lspci`는 `busybox` 심볼릭 링크라 그대로 `cp` 하면 **busybox 바이너리를 덮어써서 init이 죽는다** (실제로 겪음 — rm 후 cp).
2. [run1_lp03.sh](onsitePrep/lp03/qemu_lab/run1_lp03.sh): `-device pcie-root-port,id=rp1,chassis=1,slot=1,x-speed=16,x-width=4 -device nvme,serial=deadbeef,bus=rp1 -device pcie-root-port,id=rp2,...,x-speed=8,x-width=4 -device x3130-upstream,id=usp,bus=rp2 -device xio3130-downstream,id=dsp0,bus=usp,chassis=3,slot=0 ... -device e1000e,bus=dsp0 -device edu,bus=dsp1 -device pcie-root-port,id=rp3,chassis=5,slot=3`.
3. 커널 인자 `LAB_CMD=sh~/lab.sh` 로 명령 실행 후 자동 종료. 부팅 ~5 s (TCG).
- 한계: LTSSM 미모델링(§3.8), QEMU nvme/e1000e/virtio 모두 Gen1 x1 광고 → "limited by" 메시지·EQ 비트는 재현 불가. Lima VM 자체(Apple VZ)는 virtio 장치가 **conventional PCI (PCIe capability 없음)** 라 LnkCap/LnkSta가 아예 없다 [확인됨: 실측].

## 7. 자주 틀리는 것

1. **"lspci로 L0 확인"** → LTSSM 상태는 표준 레지스터에 없다. 말할 것은 "DSP의 DLActive + Train + speed/width로 **간접** 확인, 정확히는 vendor 레지스터/애널라이저".
2. **endpoint의 `DLActive-`를 링크 다운으로 오해** → endpoint는 보통 `LLActRep-` 라 항상 `-`. 보고 기능(LLActRep)부터 확인.
3. **링크 없을 때 LnkSta speed/width를 믿음** → QEMU 빈 포트가 `16GT/s x32` 를 보고했다. sysfs `current_link_speed`도 마찬가지. **DLActive 먼저**.
4. **root port에서 `(downgraded)`를 찾음** → pciutils는 Root Port/DSP에선 표시 안 함. endpoint/USP 쪽에서 보거나 양쪽 LnkCap을 직접 비교.
5. **Retrain Link를 endpoint에 씀** → Retrain/Link Disable/TLS는 **DSP 레지스터**. endpoint에 쓰면 무의미 (스크립트가 거부하는 이유).
6. **"Gen4 장치니까 처음부터 16 GT/s로 학습"** → 항상 2.5 GT/s로 L0 먼저, Recovery로 올림. 그래서 SI 문제 = 링크 다운이 아니라 **속도 저하**.
7. **Equalization을 Gen1/2에도 있다고 말함** → Gen1/2는 고정 de-emphasis(-3.5/-6 dB), 협상형 EQ는 8 GT/s부터, 그리고 **속도마다 다시**.
8. **Phase 2/3 역할 반대로 말함** → Phase 2: **USP가 DSP 송신기를 조정 요청**, Phase 3: DSP가 USP 송신기를 조정 요청.
9. **100 ms 규칙을 "항상 리셋 후 100 ms"로만 기억** → >5 GT/s 포트는 **링크 학습 완료 후** 100 ms. 그리고 그 뒤에도 장치는 RRS로 최대 1 s(사실상 더) 버틸 수 있다.
10. **QEMU 결과를 실 HW 동작으로 일반화** → QEMU는 LTSSM 미모델링: TLS/Retrain/Link Disable 쓰기가 무시됨(실측). QEMU는 명령·디코딩 연습용.

## 8. 출처

1. Linux v6.18 `include/uapi/linux/pci_regs.h` (LNKCAP/LNKCTL/LNKSTA/LNKCTL2/LNKSTA2 비트), `drivers/pci/pci.h` (PCIE_T_PVPERL_MS, PCIE_T_PERST_CLK_US, PCIE_RESET_CONFIG_WAIT_MS + spec 원문 주석, PCIE_LINK_RETRAIN_TIMEOUT_MS, PCIE_SPEED2MBS_ENC) — 랩 VM `/var/lab/src/linux-6.18.55`에서 직접 확인. 온라인: https://elixir.bootlin.com/linux/v6.18/source/include/uapi/linux/pci_regs.h · https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/pci.h
2. Linux v6.18 `drivers/pci/pci.c` (PCI_RESET_WAIT, PCIE_RESET_READY_POLL_MS, pcie_retrain_link, __pcie_print_link_status), `drivers/pci/pci-sysfs.c` (current_link_speed_show), `drivers/pci/probe.c` (pcie_report_downtraining) — https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/pci.c
3. Linux v6.18 `drivers/pci/pcie/bwctrl.c`, `drivers/pci/pcie/portdrv.h` (service IDs), `drivers/pci/quirks.c` (pcie_failed_link_retrain), `drivers/pci/controller/dwc/pcie-designware.h` (PCIE_PORT_DEBUG0/1, enum dw_pcie_ltssm) — https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/pcie/bwctrl.c · https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/controller/dwc/pcie-designware.h
4. Linux v6.18 `drivers/pci/controller/dwc/pcie-designware.c` (dw_pcie_wait_for_link, dw_pcie_link_up), `pcie-designware-debugfs.c` (ltssm_status, rasdes), `pcie-qcom.c` (PARF_LTSSM), `pcie-dw-rockchip.c` — https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/controller/dwc/pcie-designware-debugfs.c
5. LTSSM Detect 12 ms / Polling.Active 1024 TS1 / 24 ms 요약 (커뮤니티 요약 자료, spec 2차 출처): https://mm.edrawsoft.cn/template/142150 · https://developer.aliyun.com/article/1083111
6. AMD PG055 AXI Bridge for PCIe — "FPGA Configuration Time Debug" (20 ms Detect, 100 ms 규칙 인용): https://docs.amd.com/r/en-US/pg055-axi-bridge-pcie/FPGA-Configuration-Time-Debug
7. LKML "schemas: pci: bridge: Document PCI L0s & L1 entry delay and nfts" (PCIe r6.0 §4.2.5.1 TS1/TS2 표 인용): https://lkml.iu.edu/hypermail/linux/kernel/2501.0/04380.html
8. Teledyne LeCroy blog "Debugging dynamic link behaviors" (EQ Phase 1–3 역할, lane별 P6/P10 사례): https://blog.teledynelecroy.com/2021/08/debugging-dynamic-link-behaviors-with.html?m=1
9. TI SNLA415 (DS160PT801 retimer app note, EQ 속도별 반복·11 presets): https://ti.com/document-viewer/lit/html/SNLA415/GUID-2EF38E10-E1F5-4993-9435-8EF294C54B1F · Intel PHY IP "Link Equalization for Gen3" (Phase 2/3 optional): https://www.intel.com/content/www/us/en/docs/programmable/683621/current/link-equalization-for-gen3.html
10. pciutils `ls-caps.c` (link_compare: downgraded/overdriven 규칙, LnkSta2 출력), `lib/header.h` (PCI_EXP_LINKSTA2_EQU_*): https://github.com/pciutils/pciutils/blob/master/ls-caps.c · https://github.com/pciutils/pciutils/blob/master/lib/header.h
11. QEMU 10.2.1 `-device pcie-root-port,help` (x-speed/x-width), 실측 로그: [onsitePrep/lp03/qemu_lab/](onsitePrep/lp03/qemu_lab/run1_boot_lspci_iomem.log)
12. [N01 · PCIe 링크 디버그](2026-10-31_N01_pcie_lspci_ltssm.html) (이 노트의 출발점)

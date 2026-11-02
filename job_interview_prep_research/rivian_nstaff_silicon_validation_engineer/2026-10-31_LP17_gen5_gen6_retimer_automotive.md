# LP17 · Gen5/Gen6 · retimer · 자동차/칩간 PCIe
<!-- theme: pastel -->

> **묶음**: F 실전 · **우선**: P2 (여유 시) — 단, Don의 **Solidigm PCIe Gen6 SSD controller** 강점 카드라 면접에서 먼저 꺼낼 수 있음 · **선행 노트**: [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html), [LP09 AER/DPC](2026-10-31_LP09_aer_dpc_error_handling.html), [LP16 bare-metal vs Linux](2026-10-31_LP16_baremetal_vs_linux_zynq.html) · **다음 노트**: LP18 모의 면접 Q 뱅크 (LP 트랙 목록 참조) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html), [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [LP15 디버그 시나리오](2026-10-31_LP15_debug_scenarios.html), [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html) · **코드**: 없음 (lspci/pcilmr 명령 레시피, `[미실행]` 표시)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "Gen6에서 검증이 뭐가 달라지나?", "FEC가 있으면 correctable 에러의 의미가 어떻게 바뀌나?", "retimer는 LTSSM에서 어디에 나타나나?", "retimer vs redriver?", "PCIe가 차 안에서 어디에 쓰이나, 장거리는 왜 다른 링크인가?", "기능 안전(ISO 26262) 관점에서 PCIe는 에러를 어떻게 감지·격리하나?"
- **외울 것 ① 숫자**: Gen5 = **32 GT/s NRZ, 128b/130b, ~3.94 GB/s/레인/방향**. Gen6 = **64 GT/s PAM4, FLIT mode (256 B flit = 236 TLP + 6 DLP + 8 CRC + 6 FEC)** [1]. Gen7 = **128 GT/s PAM4, x16 양방향 512 GB/s, 2025-06 발표** [2]. 채널 손실 예산 Gen4 ≈ 28 dB, Gen5 ≈ 36 dB [3][4].
- **외울 것 ② Gen6 = "에러가 나는 게 정상인 링크"**: PAM4는 눈(eye)이 3개로 쪼개져 raw 에러율이 NRZ보다 훨씬 높음 → **경량 FEC가 flit 안에서 고치고, 못 고치면 CRC가 잡아 replay**. 그래서 검증 판정은 "에러 0"이 아니라 **FEC 정정률·replay율·uncorrectable flit율의 임계값** [1][5].
- **외울 것 ③ retimer**: 프로토콜을 아는 중계기 — 링크를 **두 segment로 나누고 각 segment가 따로 equalization**. LTSSM 관점에서 retimer는 TS ordered set에 자기 존재를 표시 → downstream port의 **`LnkSta2` Retimer Presence Detected (bit 6) / Two Retimers (bit 7)**, 지원 여부는 `LnkCap2` bit 23/24 [6][7]. 한 링크에 **최대 2개**. retimer도 **Lane Margining** 대상 [8].
- **외울 것 ④ 자동차**: PCIe = **박스 안·짧은 거리** 칩간(SoC ↔ 가속기/NVMe/스위치), 자동차 등급 스위치 존재(Microchip Switchtec Gen4 automotive-qualified, AEC-Q100 Grade 2 −40~+105 °C) [9][10]. **수 m 이상**(카메라·존 컨트롤러)은 MIPI A-PHY(최대 15 m, CSI-2 직접 운반) / ASA Motion Link / 자동차 Ethernet [11][12] [추정: 실제 Rivian 구성은 비공개].
- **30초 영어 ("What changes in validation for Gen6?")**: "Three things. The signaling moves to PAM4, so raw bit errors are expected and a lightweight FEC plus a strong CRC inside fixed 256-byte flits handle them — validation shifts from 'zero errors' to measuring FEC correction rates, replay rates and uncorrectable-flit rates against thresholds, across PVT. The link layer changes to flit mode, so the error logging, replay behavior and new states like L0p need their own tests. And margins are thinner, so lane margining, equalization sweeps and retimer segments become first-class test items. I worked on a Gen6 SSD controller at Solidigm, so this is familiar territory."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "같은 도로에 더 많은 정보를 싣는 법"

- PCIe 세대가 올라갈 때마다 목표는 **레인당 속도 2배**. 지금까지는 "차를 더 빨리 달리게"(클럭 2배) 했다. 그런데 Gen5(32 GT/s)쯤 되자 도로(구리 PCB 배선)가 버티지 못한다 — 고주파일수록 신호가 많이 약해진다(손실).
- **Gen6의 아이디어**: 속도(박자)는 Gen5와 같게 두고, **한 박자에 2비트**를 싣는다. 신호 높이를 2단계(0/1, NRZ)가 아니라 **4단계(00/01/10/11, PAM4)** 로 나눈다. 같은 도로, 같은 박자, 정보 2배.
- 대가: 4단계로 나누면 각 단계 사이 간격이 1/3로 좁아져 **헷갈리기 쉽다(에러↑)**. 그래서 "택배 상자마다 **오류 정정 스티커(FEC)** 를 붙이고, 그래도 망가지면 **검수표(CRC)** 로 걸러서 재발송"하는 방식으로 바뀐다. 상자 크기도 **고정 256 B(flit)** 로 통일 — 정정 코드는 고정 크기 블록에서 잘 동작하기 때문.

```text
 NRZ (Gen1–Gen5)                  PAM4 (Gen6, Gen7)
 신호 레벨 2개 → 1 bit/UI          신호 레벨 4개 → 2 bit/UI
   1 ────┐    ┌──                  11 ──┐
         │    │                    10 ──┼─┐   ← 눈(eye)이 3개, 각 높이 ≈ NRZ의 1/3
   0 ────┘────┘                    01 ──┘ │      (≈ 9.5 dB SNR 손해 [추정: 20·log10(3)])
                                   00 ────┘
 32 GT/s = 32 GBaud               64 GT/s = 32 GBaud (박자는 Gen5와 같음 → 채널 Nyquist 16 GHz 동일)
```

### 1.2 비유 — retimer와 redriver

- 긴 도로 중간에 **확성기(redriver)** 를 두면 소리는 커지지만 **잡음도 같이 커진다** — 단순 아날로그 증폭·이퀄라이저, 프로토콜 모름.
- **통역사(retimer)** 는 말을 **완전히 알아듣고 깨끗하게 다시 말한다** — 신호를 복원(클럭·데이터 복구)해 새로 송신, PCIe 프로토콜을 이해하고 **LTSSM에 참여**한다. 대신 비싸고, 약간의 지연(latency)이 생기고, 자기도 링크 학습 과정의 일부가 되므로 **검증 대상이 하나 늘어난다**.

```text
  Root Port ══ segment A ══ [Retimer] ══ segment B ══ Endpoint
     (USP쪽 pseudo port)│(DSP쪽 pseudo port)
  - 각 segment가 따로 equalization (손실 예산을 둘로 나눔 → 총 도달거리 ≈ 2배 [3])
  - Configuration 중 TS에 "retimer 있음" 표시 → Root Port LnkSta2 "Retimer+"
  - 한 링크에 최대 2개 → "2Retimers+"
```

### 1.3 비유 — 자동차 안의 "도로 종류"

| 거리 | 비유 | 링크 | 예 |
|---|---|---|---|
| 칩 패키지 안 / 보드 위 수 cm–수십 cm | 건물 안 복도 | **PCIe** (또는 전용 die-to-die 링크) | 중앙 컴퓨터 SoC ↔ AI 가속기, NVMe, PCIe 스위치 |
| 박스 간 1–15 m, 카메라 | 도시 간 고속도로 | **MIPI A-PHY / ASA Motion Link / GMSL·FPD-Link류 SerDes** | 카메라 → 중앙 컴퓨터 (CSI-2를 실어 나름) |
| 차량 전체 백본 | 전국 도로망 | **자동차 Ethernet** (T1 계열) | 존(zonal) 컨트롤러 ↔ 중앙 컴퓨터 |

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| GT/s | Giga-Transfers per second | 레인 하나에서 1초에 보내는 심볼(비트 단위 전송) 수 | 세대를 구분하는 숫자 |
| NRZ | Non-Return-to-Zero | 신호 레벨 2개 (0/1) | Gen1–Gen5 |
| PAM4 | 4-level Pulse Amplitude Modulation | 신호 레벨 4개 → 한 박자에 2비트 | Gen6/7, 에러율↑ |
| UI / Baud | Unit Interval / 심볼율 | 한 심볼의 시간 / 초당 심볼 수 | PAM4는 GT/s ≠ GBaud |
| 8b/10b, 128b/130b | 라인 인코딩 | 데이터 8비트를 10비트로(오버헤드 20%) / 128을 130으로(1.5%) | 실효 대역폭 계산 |
| FLIT | Flow control unIT | Gen6의 고정 크기(256 B) 전송 단위 | TLP·DLLP를 flit에 담음 |
| FEC | Forward Error Correction | 받는 쪽이 재전송 없이 스스로 고치는 정정 코드 | PAM4 에러를 흡수 |
| CRC | Cyclic Redundancy Check | 데이터가 망가졌는지 검사하는 체크섬 | FEC가 못 고친 것을 잡아 replay |
| FBER | First Bit Error Rate | FEC 전 원시 비트 에러율 | Gen6 채널 품질 지표 |
| L0p | (L0 partial) | flit mode에서 **링크를 끊지 않고 폭만 줄이는** 저전력 상태 | 새 상태 → 새 테스트 |
| Retimer | (리타이머) | 신호를 복원해 재송신하는 프로토콜 인지 중계기 | LTSSM 참여, 두 segment |
| Redriver | (리드라이버) | 아날로그 증폭·EQ만 하는 중계기 | 프로토콜 무관, 잡음도 증폭 |
| Insertion loss | 삽입 손실 (dB) | 채널을 지나며 신호가 약해지는 양 | 세대별 예산 (Gen5 ≈ 36 dB) |
| Lane Margining | Lane Margining at Receiver | 링크를 끊지 않고 수신기 샘플링 위치를 옮겨 **여유(margin)** 를 측정 | 16 GT/s 이상 필수 |
| AEC-Q100 | Automotive Electronics Council 신뢰성 규격 | 자동차용 IC 인증 (Grade = 온도 범위) | 부품 선택·검증 온도 |
| ISO 26262 / ASIL | 자동차 기능 안전 표준 / Automotive Safety Integrity Level (A–D) | 고장이 위험으로 이어지지 않게 하는 개발·검증 규칙 | 에러 감지·격리 요구 |
| DPC | Downstream Port Containment | 심각한 에러 시 포트가 링크를 끊어 오염 확산 차단 | 안전 관점의 "격리" |
| MIPI A-PHY | MIPI Automotive PHY | 최대 15 m 자동차 SerDes, CSI-2 운반 | 카메라 장거리 링크 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 세대별 속도 표

| 세대 | 전송률 | 신호 | 인코딩 | 레인당 한 방향 (근사) | x16 한 방향 (근사) | 스펙 발표 |
|---|---|---|---|---|---|---|
| Gen1 | 2.5 GT/s | NRZ | 8b/10b | 0.25 GB/s | 4 GB/s | 2003 (1.0a) [추정: 1.0은 2002] |
| Gen2 | 5.0 GT/s | NRZ | 8b/10b | 0.5 GB/s | 8 GB/s | 2007 |
| Gen3 | 8.0 GT/s | NRZ | 128b/130b | 0.985 GB/s | 15.75 GB/s | 2010 |
| Gen4 | 16 GT/s | NRZ | 128b/130b | 1.97 GB/s | 31.5 GB/s | 2017 |
| Gen5 | 32 GT/s | NRZ | 128b/130b | 3.94 GB/s | 63 GB/s | 2019 |
| Gen6 | 64 GT/s | **PAM4** | **FLIT** (1b/1b + FEC/CRC) | ≈ 8 GB/s raw, flit 효율 반영 시 약간 낮음 | 128 GB/s raw (양방향 256) | 2022 |
| Gen7 | 128 GT/s | PAM4 | FLIT | ≈ 16 GB/s raw | 256 GB/s raw (양방향 **512 GB/s**) | **2025-06-11** [2] |
| Gen8 | 256 GT/s (목표) | — | — | — | — | 2028 목표 (로드맵) [13] |

- 계산법: `GT/s × 인코딩 효율 ÷ 8 = GB/s/레인/방향`. 예 Gen4: 16 × 128/130 ÷ 8 ≈ 1.97. Gen6 flit 효율은 236/256 TLP 바이트 기준 ≈ 92% [추정: 실제 효율은 DLLP·헤더 포함 방식에 따라 다르게 인용됨].
- 레지스터 인코딩 (`LnkCap`/`LnkSta` speed 필드): 1=2.5, 2=5, 3=8, 4=16, 5=32, **6=64 GT/s** (`PCI_EXP_LNKCAP_SLS_64_0GB 0x6`) [6]. sysfs `current_link_speed`는 `"64.0 GT/s PCIe"` 형식 [14]. lspci는 128 GT/s 문자열도 이미 지원 [7].

### 2.2 왜 Gen6에서 NRZ → PAM4인가

- 고속 신호의 손실은 **Nyquist 주파수**(심볼율의 절반)에서 결정된다. NRZ로 64 GT/s를 하면 Nyquist 32 GHz → PCB 손실이 감당 불가 수준 [추정: 정량은 채널마다 다름].
- PAM4는 64 GT/s를 **32 GBaud**로 보냄 → Nyquist 16 GHz, **Gen5와 같은 채널 주파수 영역** → 기존 보드 재료·커넥터 기술을 크게 바꾸지 않고 2배.
- 대가: 레벨 간격이 1/3 → SNR 손해 → raw BER이 Gen5의 1e-12 수준을 유지할 수 없음 → **FEC 필수**, FEC는 고정 블록이 필요 → **256 B flit** [1].

### 2.3 FLIT mode, FEC, CRC

```text
 256-byte FLIT
 ┌──────────────────────────── 236 B TLP bytes ─────────────────────┬─ 6 B DLP ─┬── 8 B CRC ──┬── 6 B FEC ──┐
 │ TLP 여러 개(또는 조각)가 고정 칸에 채워짐 (TLP별 LCRC 없음)           │ flit 순번·  │ flit 전체    │ 3-way 인터리브│
 │                                                                   │ Ack/Nak 등 │ 오류 검출     │ 정정 [추정]   │
 └───────────────────────────────────────────────────────────────────┴───────────┴─────────────┴─────────────┘
 수신: FEC로 정정 시도 ─► CRC 검사 ─► OK: 수락 / 실패: Nak → 송신측 replay (재전송)
```

- 레이아웃 236 + 6 + 8 + 6 = 256 B [확인됨: SNIA SDC] [1]. TLP마다 붙던 LCRC가 없어지고 **flit 단위 CRC**, DLLP 정보는 DLP 바이트로 [1].
- 경량 FEC + 강한 CRC 조합으로 **retry 확률 ~1e-6 수준**을 목표 [확인됨: Synopsys] [5]. FEC는 "약하게" 설계해서 지연을 아주 작게 유지 [추정: ns 수준].
- Gen6 장치는 flit mode를 협상하면 **낮은 속도에서도 flit mode** 로 동작 [추정: spec 동작, 원문 미확인].
- Linux/pciutils에서 보이는 것: `LnkSta2` **`FltMode+`** (bit 10, `PCI_EXP_LNKSTA2_FLIT 0x0400`) [6][7], Physical Layer 64.0 GT/s extended capability (ID 0x31) [6], **Flit Logging** extended capability (`Flit Error Counter` 제어·로그) 와 **Flit Error Injection** capability를 lspci가 디코드 [15].

### 2.4 FEC가 있으면 correctable 에러의 의미가 달라진다

| | Gen1–Gen5 (NRZ, LCRC per TLP) | Gen6+ (PAM4, flit FEC + CRC) |
|---|---|---|
| raw 비트 에러 | 드묾 (BER ~1e-12 목표) | **흔함** (PAM4 특성상 정상 동작에서도 발생) |
| 1차 방어 | 없음 → LCRC 실패 시 바로 Nak/replay | **FEC가 flit 안에서 정정** (재전송 없음) |
| 2차 방어 | replay (DLL) | CRC 실패 시 replay |
| AER correctable 의미 | "margin이 줄고 있다"는 직접 경고 | FEC가 다 고친 건 보통 AER에 안 뜸 [추정]; replay·CRC 실패가 의미 있는 신호 |
| 판정 기준 | 거의 0 + 추세 | **FEC 정정률·flit 에러 카운터·replay율**의 임계값 + 추세 (Flit Logging 카운터) |
| 검증 포인트 | BER, eye, AER 카운터 | FBER 분포, 정정 불가 flit 비율, replay가 성능에 주는 영향, burst 에러 내성 |

- 면접 문장: "With FEC, a nonzero error count is normal; what I track is how close we are to the FEC's correction limit — the corrected-error rate, uncorrectable flits and replays — and how that moves across temperature and voltage. A link that's 'clean' at room temperature but correcting near its limit is a field failure waiting to happen."

### 2.5 L0p — 끊지 않고 폭 줄이기

- **L0p**: flit mode에서만 있는 저전력 상태. L0 안에서 **일부 레인을 끄고 나머지로 계속 전송**, 다시 켤 때도 링크 전체 재학습 없이 [확인됨: Cadence/SNIA 요약] [1][16].
- 기존 방식: 폭을 바꾸려면 Recovery/재학습(트래픽 중단). L0p는 트래픽을 유지 → 검증 항목: 폭 전환 중 데이터 무결성, 전환 지연, 전환 반복 스트레스, 전환 중 에러 주입 [추정: 테스트 항목은 일반 원칙].

### 2.6 채널 손실 예산과 retimer/redriver

| 세대 | 대략적 손실 예산 (end-to-end, Nyquist) | 확인 |
|---|---|---|
| Gen3 | ≈ 22 dB | [추정] |
| Gen4 | ≈ 28 dB | [확인됨: TI] [3] |
| Gen5 | ≈ 36 dB (CEM 커넥터 하나 ≈ 1.5 dB) | [확인됨: Microchip/vendor 자료 인용] [4] |
| Gen6 | ≈ 32 dB 수준으로 자주 인용 | [추정] |

- 서버 보드에서도 CPU에서 먼 슬롯/케이블은 예산을 넘음 → 중계기 필요.

| | Redriver | Retimer |
|---|---|---|
| 동작 | 아날로그 증폭 + CTLE(연속시간 EQ) | CDR로 클럭·데이터 복구 후 재송신 |
| 프로토콜 인지 | ❌ (LTSSM에 보이지 않음) | ✅ (LTSSM 참여, TS에 존재 표시) |
| 잡음·지터 | 같이 증폭됨 | 리셋됨 (segment마다 새 예산) |
| 도달 거리 효과 | 위치 의존, 대략 +16 dB [3] | 총 도달거리 ≈ **2배** (예산이 segment별) [3] |
| 지연 | 매우 작음 | 추가 지연 있음 [추정: 수십 ns 이하] |
| EQ | 고정/수동 설정 | 각 segment에서 **링크 EQ 협상에 참여** |
| 검증 부담 | 설정값 튜닝, 링크는 하나 | **segment 2개 + retimer 자체 레지스터·FW** |

### 2.7 retimer가 LTSSM에서 보이는 곳

- retimer는 양쪽에 **pseudo port**(상류 쪽·하류 쪽)를 가지고, 각 segment에서 TS1/TS2를 주고받는다. Gen3 이상 equalization은 **segment별로** 진행 → retimer가 있으면 EQ 단계가 더 길고 실패 지점도 둘 [추정: 세부 순서는 spec retimer 장 참조].
- 존재 표시: Configuration 단계 TS2에 retimer가 비트를 세팅 → downstream port가 기록 [추정: 정확한 TS 심볼 위치]. 레지스터로 확인되는 사실 [확인됨] [6][7]:

| 레지스터 | 비트 | pciutils 이름 | lspci 표시 |
|---|---|---|---|
| Link Capabilities 2 (+0x2C) | 23 | `PCI_EXP_LNKCAP2_RETIMER` (0x00800000) | `LnkCap2: ... Retimer+` (= Retimer Presence Detect Supported) |
| Link Capabilities 2 | 24 | `PCI_EXP_LNKCAP2_2RETIMERS` (0x01000000) | `2Retimers+` |
| Link Status 2 (+0x32) | 6 | `PCI_EXP_LINKSTA2_RETIMER` (0x0040) | `LnkSta2: ... Retimer+` (= Retimer Presence Detected) |
| Link Status 2 | 7 | `PCI_EXP_LINKSTA2_2RETIMERS` (0x0080) | `2Retimers+` |
| Link Status 2 | 10 | `PCI_EXP_LNKSTA2_FLIT` (0x0400) | `FltMode+` |

- 주의: 커널 `pci_regs.h`(2026-10 확인)에는 LnkSta2 retimer 비트 define이 없고 FLIT만 있음 → **pciutils `lib/header.h`가 출처** [6][7].
- retimer는 **Lane Margining 대상**: `pcilmr -r`로 수신기(retimer pseudo port 포함)를 골라 margin 측정 [8].
- 디버그 함의: retimer가 있는 링크가 Gen5로 안 뜨면 **어느 segment**인지부터 (retimer 벤더 레지스터/툴로 segment별 LTSSM·EQ 결과 확인 [추정]).

### 2.8 Lane Margining at Receiver (LMR)

- **16 GT/s 이상 포트(retimer pseudo port 포함) 필수**. timing margin 필수, voltage margin은 16 GT/s에서 선택·32 GT/s 이상 필수 [확인됨: pcilmr man] [8].
- 링크가 **L0인 채로** 수신기 샘플 위치를 시간/전압 방향으로 옮겨 가며 에러가 날 때까지 → "눈이 얼마나 열려 있나". 측정 중 ASPM·자율 속도/폭 변경은 끔 [8].
- pcilmr 판정 참고값 (man page 표) [8]:

| 속도 | 수준 | eye width | eye height |
|---|---|---|---|
| 16 GT/s | Min | 18.75 ps (30% UI) | 15 mV |
| 16 GT/s | Rec | 23.75 ps (38% UI) | 21 mV |
| 32 GT/s | Min | 9.375 ps (30% UI) | 15 mV |
| 32 GT/s | Rec | 10.157 ps (33% UI) | 19.75 mV |

- pcilmr 현재 지원: 16·32 GT/s 링크 [8] → Gen6 margining은 벤더 툴/새 버전 필요 [추정].

### 2.9 자동차 / 칩간 PCIe

- **Rivian 맥락 (공개 정보)**: Gen 3 autonomy 하드웨어 ACM3가 자체 **RAP1** 칩 기반, 칩 간 저지연 interconnect **RivLink**로 멀티칩 확장 [17]. RivLink가 PCIe 기반인지는 **비공개** [추정: 모름 — 면접에서 질문 거리].
- **차 안 PCIe의 전형적 용도** [추정: 업계 일반]:
  - 중앙 컴퓨터 SoC ↔ AI 가속기 / 두 번째 SoC (칩간, 보드 위)
  - SoC ↔ NVMe 스토리지 (로깅·맵)
  - SoC ↔ PCIe 스위치 → 여러 장치 / 다중 호스트(NTB)
  - SoC ↔ 고속 Ethernet 스위치·NIC (PCIe로 연결)
- **자동차 등급 PCIe 스위치**: Microchip **Switchtec PFX/PSX/PAX Gen4 automotive-qualified** (2022 발표, ADAS용) [9], **PCI100x** Gen4 스위치 AEC-Q100 **Grade 2 (−40 ~ +105 °C)** 버전 [10]. Broadcom PEX 계열도 자동차 시장을 겨냥한다는 보도 [추정: 미확인].
- **PCI-SIG Automotive workgroup**: 2021년경 결성, 온도·노이즈·케이블 길이 요구를 다룸 [18].
- **장거리 비교** (박스 밖):

| 링크 | 거리 | 대역폭 | 주 용도 | 확인 |
|---|---|---|---|---|
| PCIe (보드/짧은 케이블) | 수십 cm (retimer로 연장) | 레인×세대 (Gen4 x4 ≈ 7.9 GB/s/방향) | 칩간, 스토리지, 가속기 | [확인됨: 속도] |
| MIPI A-PHY (IEEE 2977-2021) | **최대 15 m** | 비대칭, 최대 수십 Gbps (상위 gear는 향후) | 카메라·디스플레이 (CSI-2/DSI 직접) | [확인됨] [11] |
| ASA Motion Link | 수 m–15 m급 [추정] | 수 Gbps–수십 Gbps [추정] | 카메라·디스플레이, MIPI와 CSI-2 연동 liaison | [확인됨: liaison] [12] |
| 자동차 Ethernet (100BASE-T1 / 1000BASE-T1 / multi-gig T1) | 수 m–15 m [추정] | 100 Mbps–10 Gbps [추정] | 백본, 존 아키텍처, 센서 일부 | [추정] |

- Don이 말할 수 있는 정리: "PCIe is the in-box, chip-to-chip fabric; once you leave the box, cars use SerDes like A-PHY for cameras and automotive Ethernet for the backbone. The validation difference is environment — temperature, EMI, power transients and vibration — and lifetime: a car must keep the link healthy for over a decade."

### 2.10 기능 안전 (ISO 26262) 관점 — 짧게

- 기본 개념 [추정: 표준 일반론, 원문 미확인]: ASIL(A–D)에 따라 **안전 메커니즘**(고장 감지·격리·안전 상태 진입)과 그 **진단 커버리지(diagnostic coverage)** 를 정량화 (FMEDA), 잠재 고장(latent fault)은 시동 시 자가 진단 등으로 잡음.
- PCIe 쪽 **안전 메커니즘 후보**:

| 범주 | PCIe 기능 | 안전 관점 의미 |
|---|---|---|
| 감지 (링크) | LCRC/flit CRC + replay, FEC, 8b/10b·128b/130b 오류 검출 | 일시적 전송 오류 검출·복구 |
| 감지 (종단 간) | **ECRC** (end-to-end CRC), Poisoned TLP | 스위치 내부 손상까지 검출 [추정: ECRC는 선택 기능] |
| 감지 (응답 없음) | Completion Timeout | 상대 고장 감지 |
| 보고 | **AER** (correctable / non-fatal / fatal), 인터럽트 | 안전 감시자(safety monitor)에 통지 |
| 격리 | **DPC** (포트가 링크를 끊어 오염 확산 차단), ACS | 고장 전파 차단 (containment) |
| 복구 | hot reset, FLR, 재열거 | 안전 상태 후 기능 복귀 |
| 잠재 고장 | **에러 주입**(DWC RAS DES `rasdes_err_inj` [19], Flit Error Injection cap [15]), 시동 시 loopback/BIST | 안전 메커니즘 자체가 동작하는지 주기적 확인 |

- 검증 엔지니어 관점: 각 안전 메커니즘을 **에러 주입으로 발동시켜** "감지됨 → 보고됨 → 격리됨 → 시간 안에 안전 상태"를 측정 (fault-injection campaign) [추정: 일반 기능 안전 검증 방법].

## 3. Linux에서 볼 것

### 3.1 Gen5/Gen6 링크의 lspci (형식은 pciutils 소스 기준)

```text
[예시 출력]  (Gen6 x4 장치 아래 root port, retimer 1개)
        LnkCap: Port #0, Speed 64GT/s, Width x4, ASPM L1, Exit Latency L1 <32us
        LnkSta: Speed 64GT/s, Width x4
                TrErr- Train- SlotClk+ DLActive+ BWMgmt- ABWMgmt-
        LnkCap2: Supported Link Speeds: 2.5-64GT/s, Crosslink- Retimer+ 2Retimers+ DRS-
        LnkCtl2: Target Link Speed: 64GT/s, EnterCompliance- SpeedDis-
        LnkSta2: Current De-emphasis Level: -6dB, EqualizationComplete+ EqualizationPhase1+
                 EqualizationPhase2+ EqualizationPhase3+ LinkEqualizationRequest-
                 Retimer+ 2Retimers- CrosslinkRes: unsupported, FltMode+
        Capabilities: [xxx] Physical Layer 16.0 GT/s
        Capabilities: [xxx] Lane Margining at the Receiver
        Capabilities: [xxx] Physical Layer 32.0 GT/s
        Capabilities: [xxx] Physical Layer 64.0 GT/s
        Capabilities: [xxx] Flit Logging
```

| 필드 | 해석 |
|---|---|
| `LnkCap2 ... Retimer+ 2Retimers+` | 이 포트가 retimer 1개/2개 **존재 감지 기능을 지원** (bit 23/24) |
| `LnkSta2 ... Retimer+ 2Retimers-` | 학습 중 **retimer 1개가 감지됨** (bit 6=1, bit 7=0) → segment 2개짜리 링크 |
| `FltMode+` | flit mode로 동작 중 (bit 10) — Gen6 링크면 기대값 |
| `EqualizationPhase1/2/3+` | EQ 완료 (retimer 링크는 segment별로 진행되므로 실패 시 어느 segment인지 추가 확인) |
| `Physical Layer 16/32/64.0 GT/s` | 세대별 PHY 레이어 확장 cap (ID 0x26/0x2A/0x31) [6] — 레인별 EQ 상태·preset 정보 |
| `Lane Margining at the Receiver` | LMR cap (ID 0x27) [7] — pcilmr 대상 |
| `Flit Logging` | flit 에러 로그·**Flit Error Counter** (Gen6 신호 품질 계기판) [15] |

### 3.2 Lane margining 명령 `[미실행]`

```bash
sudo pcilmr --scan                       # 16 GT/s 이상으로 협상된 링크 목록 + 준비 상태
sudo pcilmr -c 0000:00:01.0              # 이 링크의 margining 능력만 출력
sudo pcilmr -l 0,1,2,3 -r 1,6 -e 4 -d 1 0000:00:01.0     # 레인 0–3, 수신기 1·6(retimer 포함 가능), 에러 한도 4, dwell 1 s
sudo pcilmr -o lmr_csv/ --full           # 준비된 모든 링크, CSV 저장
```

- 옵션 의미는 man page 기준 (`-c` 능력만, `-l` 레인, `-r` 수신기, `-e` 에러 한도, `-d` dwell, `-t/-T` timing 스텝, `-v/-V` voltage 스텝, `-o` CSV) [8]. 실행 전 BIOS/펌웨어의 자동 재-EQ·링크 저하 기능 끄기 권고 [8].
- 실행 환경 없음: Lima VM은 conventional PCI 가상 장치뿐(LP15 §3.1 실제 출력), QEMU의 링크 속도·margining 에뮬레이션은 제한적 [추정].

### 3.3 sysfs로 속도 감시

```bash
cat /sys/bus/pci/devices/0000:01:00.0/current_link_speed    # "64.0 GT/s PCIe"
cat /sys/bus/pci/devices/0000:01:00.0/max_link_speed
cat /sys/bus/pci/devices/0000:01:00.0/aer_dev_correctable  # Gen6에서는 해석 주의 (§2.4)
```

## 4. 꼬리 질문 체인

### 4.1 LP00 체인 — "What changes in validation for Gen6?"

<details><summary>답 펼치기</summary>

- **Q. What changes in validation for Gen6?** — "PAM4 signaling means raw errors are normal and FEC plus CRC in fixed 256-byte flits handle them, so pass/fail moves from zero errors to thresholds on FEC corrections, uncorrectable flits and replays across PVT. Flit mode changes the data link layer, so error logging, replay and new behaviors like L0p need tests. And margins are thinner, so margining, EQ sweeps and retimer segments become core test items." · 한국어: PAM4·FEC → 임계값 판정, flit mode·L0p → 새 테스트, 얇은 margin → margining·EQ·retimer.
- **꼬리1. How does FEC change what correctable errors mean?** — "Before Gen6, a correctable receiver error was already a warning sign. With FEC, the FEC fixes most raw errors silently inside the flit; what I watch is the FEC corrected-error rate relative to its limit, CRC failures that cause replays, and the flit error counters. The trend across temperature and voltage matters more than the absolute count." · 한국어: FEC가 조용히 고침 → 정정률·replay·flit 카운터의 추세가 핵심.
- **꼬리2. Where does a retimer show up in the LTSSM?** — "The retimer participates in training: it has pseudo ports on each side, the link becomes two segments that each equalize, and it advertises itself in the training sets during Configuration so the downstream port records Retimer Presence Detected in Link Status 2 — lspci shows 'Retimer+'. Up to two retimers are allowed. Its receivers are also margining targets." · 한국어: 두 segment·각자 EQ·TS로 존재 알림 → LnkSta2 bit 6/7.
- **꼬리3. A Gen6 link trains at Gen5 only, with a retimer. Where do you look?** — "Which segment failed: the retimer's own registers or vendor tool show per-segment LTSSM and equalization results. Then margining on each receiver, including the retimer pseudo ports, and a preset sweep per segment. I'd also test without the retimer on a short channel to see if the endpoint itself reaches 64 GT/s." · 한국어: segment별 상태 → 수신기별 margining → retimer 없이 짧은 채널 비교.

</details>

### 4.2 Why PAM4 / why flit

<details><summary>답 펼치기</summary>

- **Q. Why did PCIe switch from NRZ to PAM4 at Gen6?** — "Doubling NRZ to 64 GT/s would push the Nyquist frequency to 32 GHz, and copper channel loss there is unmanageable. PAM4 carries two bits per symbol, so 64 GT/s runs at 32 gigabaud — the same Nyquist as Gen5 — and existing channel technology still works. The price is a smaller eye, about a third of the amplitude, so you need FEC." · 한국어: Nyquist를 Gen5와 같게 유지, 대가는 SNR → FEC.
- **꼬리1. Why fixed-size flits?** — "FEC works on fixed-size blocks. Once the unit is fixed, it's efficient to move the CRC and the ack/nak sequence to the flit level instead of per TLP, which also removes per-TLP LCRC overhead." · 한국어: FEC는 고정 블록 필요 → CRC·Ack/Nak도 flit 단위.
- **꼬리2. What's the latency impact?** — "The FEC was chosen to be lightweight so its decode latency is very small — on the order of nanoseconds — and the strong CRC keeps the retry probability low, around one in a million per the vendor literature. For latency-sensitive traffic, replays matter more than FEC decode." · 한국어: FEC 지연 작음, replay가 더 중요. [추정: ns 수준]

</details>

### 4.3 Retimer vs redriver — 설계 선택

<details><summary>답 펼치기</summary>

- **Q. When would you use a retimer versus a redriver?** — "A redriver is cheap and adds almost no latency but just boosts and equalizes — it amplifies noise too and doesn't take part in the protocol, so it only helps a moderately long channel. When the channel exceeds the budget — around 36 dB at Gen5 — or crosses connectors and cables, a retimer resets the budget by splitting the link into two segments, roughly doubling reach, at the cost of latency, power, cost, and an extra component to validate." · 한국어: 예산 초과·커넥터·케이블이면 retimer, 적당한 손실이면 redriver.
- **꼬리1. How does a retimer change your validation plan?** — "Every link test now runs per segment: training and equalization success per segment, margining at the retimer's pseudo ports, retimer firmware and configuration versions as test variables, and latency and reset behavior through the retimer — hot reset, PERST#, power management." · 한국어: segment별 테스트 + retimer FW/설정을 변수로 + 리셋·PM 통과 확인.
- **꼬리2. How do you prove a failure is in the retimer?** — "Bypass it if the board allows, or move to a short channel without it; compare per-segment margins; and check whether the failing segment is upstream or downstream of the retimer." · 한국어: 우회·짧은 채널 비교 + segment별 margin.

</details>

### 4.4 자동차 / 기능 안전

<details><summary>답 펼치기</summary>

- **Q. How is PCIe used in a car, and how is validating it different?** — "Inside the central compute box it's the chip-to-chip fabric — SoC to accelerator, NVMe, switches — and there are automotive-qualified PCIe switches now. Long-reach links like cameras use automotive SerDes such as A-PHY, and the backbone uses automotive Ethernet. Validation differs in environment and lifetime: the full automotive temperature grade, power transients like cold crank, EMI and vibration, and the expectation that the link stays healthy for many years, so margin at corners matters more than a datacenter." · 한국어: 박스 안 칩간 = PCIe, 박스 밖 = SerDes/Ethernet, 차이는 환경·수명.
- **꼬리1. From a functional safety angle, how does PCIe detect and contain errors?** — "Detection is layered: CRC with replay and FEC on the link, ECRC and poisoned TLPs end to end, completion timeouts for a silent partner. AER reports errors to software, and DPC contains them by taking the link down at the downstream port so corrupted traffic doesn't propagate. For safety you also have to prove those mechanisms work, so you inject errors and measure detection and reaction time." · 한국어: 다층 감지 + AER 보고 + DPC 격리 + 에러 주입으로 증명.
- **꼬리2. What's a latent fault here, and how would you test for it?** — "A latent fault is a broken safety mechanism you don't notice until the real error happens — for example, AER reporting that never fires. You test it with periodic or start-up error injection, like the controller's error-injection features, and check the full chain from detection to the safety monitor." · 한국어: 감시 기능 자체 고장 → 시동 시 에러 주입으로 체인 확인. [추정: 일반 ISO 26262 개념]
- **꼬리3 (추가). Rivian's RAP1 uses RivLink between chips. What would you ask about it?** — "Whether RivLink is PCIe-based or a custom die-to-die link, what error detection and retry it has, how its training and margins are observable to software, and how it fails over — those decide how I'd validate it." · 한국어: RivLink 정체·에러 처리·관측성·failover를 질문 (면접 역질문 후보).

</details>

## 5. Don 경험 연결 — 강점 카드: Solidigm PCIe Gen6 SSD controller

- **카드 (영어, 30초)**: "At Solidigm I was a Staff engineer on a next-generation PCIe Gen6 SSD controller. I brought up the PCIe and NVMe subsystem IP on FPGA prototypes before RTL freeze, bare-metal on Cortex-R8/R82 and M0+, and later tuned performance with the silicon validation team. So the Gen6 changes — flit mode, FEC, PAM4 margins — aren't theoretical for me: [구체 사례 채우기: 예) FPGA에서 flit mode 협상/에러 로깅 경로 검증, Gen5 fallback 동작 확인]."
- 채울 것 `[숫자 채우기]`:
  - 컨트롤러 링크 구성: Gen6 x[숫자 채우기], FPGA 프로토타입에서 실제로 돌린 속도 Gen[숫자 채우기] (FPGA는 보통 목표 속도보다 낮음 → "pre-silicon은 프로토콜·FW, 고속 PHY는 실리콘에서" 문장과 연결)
  - 찾은 버그/이슈 수 [숫자 채우기], 그중 PCIe/NVMe 관련 [숫자 채우기]
  - perf 튜닝 결과: 순차/랜덤 [숫자 채우기]% 개선, 병목이 무엇이었나 [채우기]
- SK hynix 연결: "Before that I automated chamber reliability testing for high-speed interfaces including PCIe Gen3 to Gen5, with shmoo over voltage, temperature and frequency — which is the same discipline lane margining at corners needs." ([숫자 채우기]: 챔버 수·플랫폼 규모)
- Apple 연결: "At Apple I debug PCIe and other interfaces on new silicon with scopes and protocol analyzers, which is how I'd approach a retimer segment that won't equalize."
- 정직 경계: "I haven't designed retimer boards or done automotive functional-safety certification myself; I know the mechanisms and I'd lean on the safety team's FMEDA for coverage targets." [추정: Don 실제 경험 범위 확인 필요]

## 6. 실습 / 코드

- 이 노트는 실측 장비(Gen5/6 링크, retimer, 챔버)가 필요해서 **로컬 실행 결과 없음**.
- 레시피 `[미실행]`:
  1. Gen4/5 장치가 있는 Linux 호스트에서 `sudo lspci -vvv`로 `LnkCap2`/`LnkSta2`의 `Retimer`/`FltMode` 줄, 확장 cap 목록 캡처.
  2. `sudo pcilmr --scan` → `-c`로 능력 → 레인별 margin CSV → 온도 corner별 비교 (LP15 B1 연결).
  3. Target Link Speed를 한 단계씩 올리며 retrain (`setpci ... CAP_EXP+0x30.w=<n>:0xf` → `CAP_EXP+0x10.w=0x20:0x20`) 후 `LnkSta2` EQ 비트 기록 (LP15 S2 연결).
  4. Gen6 장치: Flit Logging cap의 Flit Error Counter 활성화 후 soak 중 카운터 추세 기록.
- 비트 디코드 연습: [N01](2026-10-31_N01_pcie_lspci_ltssm.html)의 `onsitePrep/pcie_lnksta_decode.c`에 LnkSta2 bit 6/7/10 디코드를 추가하는 것이 다음 단계 (미작성).

## 7. 자주 틀리는 것

1. **"Gen6 = 64 GHz 클럭"** — 64 GT/s PAM4는 32 GBaud, Nyquist 16 GHz (Gen5와 같은 채널 주파수).
2. **Gen6에서 correctable 에러 0을 기준으로 삼음** — FEC 환경에서 raw 에러는 정상. 정정률·replay·flit 에러 추세로 판정.
3. **retimer를 "강한 redriver"로 설명** — retimer는 CDR로 재생성하고 **LTSSM에 참여**, 링크를 segment로 나눈다. redriver는 프로토콜을 모른다.
4. **Retimer bit를 커널 `pci_regs.h`에서 찾음** — 2026-10 기준 커널 헤더엔 LnkSta2 retimer define이 없음; pciutils `header.h`가 출처 [6][7].
5. **`LnkCap2 Retimer+`를 "retimer가 있다"로 읽음** — LnkCap2는 **감지 지원 여부**, 실제 감지는 **LnkSta2**.
6. **Gen6 대역폭을 64 GT/s ÷ 8 = 8 GB/s 그대로 사용** — flit 오버헤드(CRC·FEC·DLP)가 있으므로 페이로드 효율은 그보다 낮다.
7. **pcilmr로 Gen6 margining 가능하다고 단정** — 현재 man page는 16/32 GT/s 지원 [8].
8. **차 안 모든 고속 링크를 PCIe로 가정** — 카메라 장거리는 A-PHY/ASA/SerDes, 백본은 Ethernet. RivLink 정체는 비공개.
9. **DPC를 "에러 처리 실패"로 봄** — 안전 관점에선 의도된 **격리(containment)** 메커니즘.
10. **PAM4 SNR 손해를 6 dB로 말함** — 레벨 간격 1/3 → 약 9.5 dB (20·log10 3) [추정: 단순 계산값].

## 8. 출처

1. SNIA SDC 2021/2022 PCIe 6.0 발표 (flit 256 B = 236 TLP + 6 DLP + 8 CRC + 6 FEC, PAM4 → FEC, L0p) — https://snia.org/sites/default/files/2025-05/SNIA-SDC21-Mazumder-PCIe6p0-A-High-Performance-Interconnect-for-Storage-Networking-Challenges.pdf , https://snia.org/sites/default/files/SDC/2022/SNIA-SDC22-DasSharma-PCIe%206.0-Specification-and-Beyond-Enabling-Storage.pdf
2. PCI-SIG PCIe 7.0 발표 보도 (2025-06-11, 128 GT/s, PAM4, x16 양방향 512 GB/s) — https://dev.aiwire.net/2025/06/11/pci-sig-releases-pcie-7-0-specification-to-support-bandwidth-demands-of-ai-at-128-0-gt-s/
3. TI, redriver/retimer 선택 가이드 (Gen4 28 dB, redriver ~16 dB, retimer 도달거리 2배) — https://www.ti.com/lit/pdf/snla401 , https://www.allaboutcircuits.com/industry-articles/choosing-the-right-redriver-or-retimer-device-to-extend-pcie-protocol-signal-range/
4. Astera Labs, PCIe Retimers vs. Redrivers (Gen5 손실 예산 맥락) — https://www.asteralabs.com/pci-express-retimers-vs-redrivers-an-eye-popping-difference/
5. Synopsys, "Successful PCI Express 6.0 Designs at 64GT/s with IP" (경량 FEC + CRC, retry 확률 ~1e-6) — https://www.synopsys.com/articles/pcie-6-designs.html
6. Linux kernel `include/uapi/linux/pci_regs.h` (LNKCAP SLS 0x6=64 GT/s, `PCI_EXP_LNKSTA2_FLIT`, `PCI_EXT_CAP_ID_PL_64GT 0x31`) — https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h
7. pciutils `lib/header.h` (`PCI_EXP_LNKCAP2_RETIMER`, `PCI_EXP_LINKSTA2_RETIMER`, `PCI_EXT_CAP_ID_LMR 0x27`) + `ls-caps.c` (LnkCap2/LnkSta2 출력 형식) — https://github.com/pciutils/pciutils/blob/master/lib/header.h , https://github.com/pciutils/pciutils/blob/master/ls-caps.c
8. pcilmr(8) man page (LMR 필수 조건, 옵션, 판정 표) — https://man7.org/linux/man-pages/man8/pcilmr.8.html
9. Microchip, "Industry's First Automotive-Qualified Gen 4 PCIe Switches" (Switchtec PFX/PSX/PAX) — https://www.microchip.com/en-us/about/news-releases/products/first-automotive-qualified-gen-4-pcie-switches
10. Microchip PCI100x Switchtec (AEC-Q100 Grade 2) — https://www.mouser.in/new/microchip/microchip-pci1001-3-4-5-pcie-switches
11. MIPI Alliance, A-PHY (최대 15 m, IEEE 2977-2021) — https://mipi.org/specifications/a-phy , https://www.eenewseurope.com/news/ieee-adopts-mipi-phy-long-reach-serdes
12. MIPI–ASA liaison (ASA-ML에서 native CSI-2) — https://autotechinsight.spglobal.com/news/5271924/mipi-alliance-and-automotive-serdes-alliance-enter-liaison-agreement-to-enable-native-mipi-csi-2-implementation-with-asa-ml-phy
13. PCIe 8.0 로드맵 (256 GT/s, 2028 목표) — https://convergedigest.com/pci-sig-targets-2028-release-of-pcie-8-0-at-256-gt-s/
14. Linux kernel `drivers/pci/probe.c` (속도 문자열 `"64.0 GT/s PCIe"`) + `pci-sysfs.c` (`current_link_speed`) — https://github.com/torvalds/linux/blob/master/drivers/pci/probe.c
15. pciutils `ls-ecaps.c` (Physical Layer 16/32/64 GT/s, Lane Margining, Flit Logging, Flit Error Injection 디코드) — https://github.com/pciutils/pciutils/blob/master/ls-ecaps.c
16. Cadence blog, "Unravelling PCIe 6.0 Flit Mode Challenges" — https://community.cadence.com/cadence_blogs_8/b/fv/posts/unravelling-pcie-6-0-flit-mode-challenges
17. Rivian 컨텍스트 노트 (Autonomy & AI Day: RAP1, ACM3, RivLink) — [Rivian context](rivian_nstaff_silicon_validation_engineer_context.html)
18. EE Times Asia, "PCIe Eyes Road Ahead with AI, Automotive" (PCI-SIG automotive workgroup) — https://www.eetasia.com/?p=51587
19. Linux kernel `drivers/pci/controller/dwc/pcie-designware-debugfs.c` (RAS DES error injection) — https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware-debugfs.c

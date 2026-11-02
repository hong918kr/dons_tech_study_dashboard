# BV02 · 회로도 · 레이아웃 · BOM 읽기 — bring-up 체크리스트로 바꾸기
<!-- theme: pastel -->

> **묶음**: A 큰 그림 · 회로도 · **우선**: P0 ("You get the schematic a week before boards arrive. What do you do with it?" · "What do you look for in a schematic review?" · "An I2C device doesn't respond — what do you look at in the schematic?") · **선행 노트**: [BV01 큰 그림](2026-11-01_BV01_si_board_validation_big_picture.html) · **이전 노트**: [BV01](2026-11-01_BV01_si_board_validation_big_picture.html) · **다음 노트**: [BV03 전원 트리·시퀀싱·PMIC](2026-11-01_BV03_power_tree_sequencing_pmic.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html) · **관련**: [BV05 리셋·strap](2026-11-01_BV05_reset_strap_boot_mode.html) · [BV06 첫 전원 인가 날](2026-11-01_BV06_first_power_on_checklist.html) · [BV10 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) · [EL P4 회로도 → DT](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P4_schematic_to_dt.html) · [EL P1 보드 bring-up 순서](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html) · [LP00A IO primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **코드**: [bringup_checklist.py](onsitePrep/bv02/bringup_checklist.py) · [cm4io_power.csv](onsitePrep/bv02/cm4io_power.csv) · [실행 로그](onsitePrep/bv02/outputs/run_cm4io.txt) · [측정 채점 로그](onsitePrep/bv02/outputs/run_cm4io_measured.txt) · [negative control 로그](onsitePrep/bv02/outputs/run_broken.txt)

## 0. TL;DR

- 이 노트로 답할 것: "보드 오기 일주일 전에 회로도를 받았다 — 뭘 하나", "회로도에서 bring-up 체크리스트에 뭘 뽑나", "어떤 rail이 먼저 올라와야 하는지 어떻게 아나", "디버그를 위해 HW 엔지니어에게 뭘 요청하나", "I2C 장치가 응답 안 하면 회로도에서 뭘 보나", "검증 보드엔 왜 0 Ω 저항과 DNP 부품이 많나", "SW 엔지니어로서 레이아웃 리뷰에서 뭘 보나".
- **외울 것 ① — 읽는 순서 9단계 (= 체크리스트 목차)**: ① 전원 트리·시퀀싱 ② 클럭 소스 ③ 리셋 회로 ④ **strap/boot mode 저항** ⑤ 디버그 헤더(JTAG·UART) ⑥ 테스트 포인트·전류 측정 shunt ⑦ 풀업/풀다운(I2C·open-drain·enable 기본값) ⑧ IO 전압 뱅크·level shifter ⑨ **DNP/0 Ω 옵션**. 각 항목은 "어디서 · 무엇으로 · 통과 기준"이 있는 체크리스트 줄이 된다.
- **외울 것 ② — 회로도에서 숫자를 검산한다**: 레귤레이터 출력 = **Vref × (1 + Rtop/Rbot)** — 공개 Raspberry Pi CM4 IO 보드 회로도에서 직접 계산: 5 V buck = 0.8 × (1 + 12k/2.2k) = **5.16 V**, PCIe 3.3 V buck = 0.8 × (1 + 47k/15k) = **3.31 V** [1][3]. I2C 풀업 = Rp(min) = (VDD − VOL)/IOL ≤ Rp ≤ Rp(max) = tr / (0.8473 × Cb) [7][8].
- **외울 것 ③ — Design-for-debug 요청 5종**: 모든 rail에 **test point**, 주요 rail에 **전류 측정 shunt**(또는 0 Ω 자리), 블록을 끊을 수 있는 **0 Ω/점퍼**, 보드 가장자리 **디버그 헤더(JTAG·UART, VTref 포함)**, strap을 바꿀 수 있는 **저항 자리/DIP 스위치** + 상태 LED.
- 실습: [bringup_checklist.py](onsitePrep/bv02/bringup_checklist.py) — 회로도에서 읽은 전원 트리를 CSV로 적으면 **의존성 순서(위상 정렬) · FB 분압 검산 · TP 없는 rail 경고(DFD ask) · Day-1 체크리스트 표 · 측정값 PASS/FAIL** 을 출력. CM4IO 실제 데이터로 실행 + 일부러 틀린 CSV(negative control)로 ERROR 3건 검출 확인 (§6, 실제 출력).
- 30초 영어 답 (headline Q: "You get the schematic a week before the boards arrive. What do you do with it?"):
  - "I turn it into a bring-up checklist and a list of questions. I walk it in bring-up order: the power tree — every rail, its regulator, what enables it and what it waits for, the expected voltage and current, and where I can probe it; then the clock sources, the reset circuit, and every strap and boot-mode resistor and what value the chip will sample; then the debug path — JTAG and UART headers and their voltage reference; then test points and current-sense resistors; pull-ups, IO voltage banks and level shifters; and finally every DNP and zero-ohm option. I sanity-check numbers like feedback dividers and pull-up values, and anything I can't measure or isolate becomes a design-for-debug request to the hardware team while there's still time to change the layout."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 회로도 = 지하철 노선도, 레이아웃 = 실제 터널 공사 도면, BOM = 자재 명세서

- **회로도(schematic)** = **지하철 노선도**. 역(부품 핀)과 노선(전선 = net)이 "무엇이 무엇과 연결되나"만 보여 준다. 실제 거리·깊이는 안 나온다. **같은 역 이름(net label)이 두 곳에 적혀 있으면 같은 역** — 선을 직접 안 그려도 연결된 것이다. 다른 페이지로 넘어가는 노선은 **off-page connector**(페이지 밖 화살표).
- **레이아웃(layout, PCB)** = **실제 터널 공사 도면**. 그 연결이 기판 위 몇 번째 층(layer)에서, 얼마나 길게, 무엇 옆으로 지나가나. 고속 신호(DDR·PCIe)는 노선도에선 똑같은 선이지만 공사 도면에선 **길이·간격·바닥(레퍼런스 플레인)** 이 성패를 가른다.
- **BOM(Bill of Materials)** = **자재 명세서**. "R27 자리에 12 kΩ 1% 저항, 제조사·품번은 이것, 이 자리(C61)는 비워 둠(DNP)". 회로도가 같아도 **BOM이 다르면 다른 보드**다 (부품 값·대체 부품·미실장).

bring-up 엔지니어에게 회로도는 **"전원 넣기 전에 머릿속으로 미리 보드를 켜 보는 도구"** 다. 어디에 프로브를 대고, 무엇을 기대하고, 안 되면 무엇을 끊어 볼지 — 이걸 보드가 오기 전에 표로 만들어 두면 Day 1이 반나절 일이 된다.

### 1.2 회로도 한 묶음의 구조 (보통 이런 순서로 페이지가 나온다)

```text
 p1  Block diagram / 목차        ── 보드 전체 그림: SoC, 메모리, 커넥터, 전원 입력. 여기서 지도를 잡는다
 p2  Power tree / power sequencing ─ 입력 → 레귤레이터 → 각 rail, EN/PGOOD 체인, 순서 표 (있으면 금광)
 p3  Power regulators (상세)      ── buck/LDO/PMIC 회로, 피드백 저항, 테스트 포인트
 p4+ SoC 핀 페이지들              ── 전원·GND 핀, IO 뱅크별 전압, 클럭 입력, 리셋, strap, JTAG
 pN  Memory (DDR/LPDDR, flash)    ── 데이터/주소 선, VREF, ZQ 저항, 종단
 pN  Interfaces                   ── Ethernet PHY, PCIe, MIPI CSI-2 커넥터, I2C 장치들, USB
 pN  Connectors / IO              ── 외부 커넥터 핀맵, ESD 보호, level shifter
 pN  Debug                        ── JTAG/SWD 헤더, UART, LED, 버튼, DIP 스위치, test point 모음
 마지막 Revision history / notes   ── rev 간 변경 목록 (ECO), DNP 규칙, 조립 변형(assembly variant)
```

### 1.3 기호 읽기 — 이것만 알면 회로도 80%를 읽는다

```text
  저항        ──/\/\/──   R27 12K 1%      값 + 오차. "0R"/"0Ω" = 그냥 전선인데 떼어낼 수 있는 다리
  커패시터    ──| |──     C47 10u         "nf"/"DNP" 표시 = 자리만 있고 실장 안 함
  인덕터      ──ᴖᴖᴖ──     L5 3.3µH        buck 출력, 또는 ferrite bead(FB, 고주파 노이즈 차단)
  다이오드    ──▶|──      D1              전류 한 방향. LED도 다이오드
  net label   ──── +5v    같은 이름 = 전기적으로 연결 (선이 없어도!)
  off-page    ──▷ GLOBAL_EN   다른 페이지로 이어짐 (◁ 들어옴 / ▷ 나감)
  test point  ──○ TP4     프로브를 댈 수 있는 패드/핀
  GND         ──⏚         접지. 여러 종류(AGND/DGND/PGND)가 있으면 어디서 합쳐지는지 확인
  jumper      ─[o o]─ J2  2핀을 캡으로 단락하면 연결
  active-low  nRESET, RESET_N, RESET#, 윗줄(overbar)   "0일 때 동작". 리셋·enable·interrupt에 흔함
  diff pair   PCIE_TX_P / PCIE_TX_N   고속 차동 신호 한 쌍 (+/− 두 선이 한 신호)
```

### 1.4 "회로도 → 체크리스트" 변환 흐름

```text
   회로도 페이지                 뽑아낼 것                          체크리스트 줄 (예)
 ┌──────────────┐   ┌───────────────────────────────┐   ┌────────────────────────────────────────┐
 │ power tree   │──▶│ rail 이름·전압·레귤레이터·부모·   │──▶│ "P5V: TP4에서 4.75–5.25 V, ≤ 3 A,       │
 │ regulators   │   │ EN/PGOOD·FB 저항·TP·예상 전류   │   │  VIN_12V 다음에 상승"                    │
 ├──────────────┤   ├───────────────────────────────┤   ├────────────────────────────────────────┤
 │ clock        │──▶│ crystal/osc 주파수·전원·출력 핀  │──▶│ "24 MHz osc: OUT 핀 scope, ±50 ppm"     │
 ├──────────────┤   ├───────────────────────────────┤   ├────────────────────────────────────────┤
 │ reset/strap  │──▶│ reset 소스·RC/supervisor·strap  │──▶│ "BOOT[2:0] = 010 (QSPI): R101 pull-up  │
 │              │   │ 저항 값·샘플 시점                │   │  fitted, R102 DNP; 리셋 후 strap reg 읽기"│
 ├──────────────┤   ├───────────────────────────────┤   ├────────────────────────────────────────┤
 │ debug        │──▶│ JTAG/UART 헤더 핀맵·VTref·레벨   │──▶│ "J5 pin1 VTref = 1.8 V → 디버거 설정"   │
 ├──────────────┤   ├───────────────────────────────┤   ├────────────────────────────────────────┤
 │ BOM / notes  │──▶│ DNP·0 Ω·점퍼·assembly variant   │──▶│ "R5 0R fitted / R4 DNP 육안 확인"        │
 └──────────────┘   └───────────────────────────────┘   └────────────────────────────────────────┘
                     → 숫자 검산 (FB 분압, 풀업)  → 측정 못 하는 곳 = DFD 요청 목록
```

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| schematic | Schematic diagram | 부품과 연결(net)만 그린 논리적 배선도 | bring-up 체크리스트의 원재료 |
| layout / PCB | Printed Circuit Board layout | 실제 기판 위 배선(층·길이·간격) 도면 | 고속 신호·전원 품질은 여기서 결정 |
| BOM | Bill of Materials | 부품 자리(ref des)별 값·품번·제조사·실장 여부 목록 | 같은 회로도라도 BOM이 다르면 다른 보드 |
| ref des | Reference Designator | 부품 이름표 (R27, C61, U16, J2, TP4, L5) | 실물 보드에 인쇄(silkscreen)돼 있어 찾을 수 있음 |
| net / net label | Net | 전기적으로 연결된 선 묶음과 그 이름 | 같은 이름 = 연결, 오타 하나로 단선 |
| off-page connector | Off-sheet connector | 다른 페이지로 넘어가는 net 표시 | 신호 추적할 때 페이지 넘나드는 길 |
| rail | Power rail | 전원선 하나 (예: +5V, VDD_CORE 0.75 V) | 전원 트리의 노드 |
| power tree | Power tree | 입력 전원 → 레귤레이터 → rail 들의 부모-자식 관계 | 순서·전류·측정 계획의 뼈대 |
| buck | Buck converter (step-down switching regulator) | 스위칭으로 전압을 낮춤, 효율 높음, ripple 있음 | 대전류 rail (코어·5 V) |
| LDO | Low-Dropout linear regulator | 저항처럼 전압을 깎아 냄, 깨끗하지만 열 발생 | PLL·아날로그 등 조용해야 하는 rail |
| PMIC | Power Management IC | 여러 rail + 순서 + 감시를 한 칩에서 | 순서·fault 레지스터를 I2C로 읽음 ([BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html)) |
| EN | Enable pin | 레귤레이터 "켜라" 입력 | 시퀀싱 체인의 연결 고리 |
| PGOOD / PG | Power Good | 레귤레이터가 "내 출력 전압이 목표 범위에 들어왔다"고 알려주는 신호선 | 다음 rail EN·리셋 해제에 연결 |
| FB divider | Feedback resistor divider | 출력 전압을 두 저항으로 나눠 레귤레이터 기준(Vref)과 비교 → 출력 결정 | 회로도에서 rail 전압을 검산하는 법 |
| strap pin | Bootstrap / strapping pin | 리셋 해제 순간 High/Low를 읽어 모드를 정하는 핀 | 부트 모드·주소·기능 선택 ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)) |
| pull-up / pull-down | Pull-up/down resistor | 아무도 안 잡을 때 선을 High/Low로 살짝 당겨 두는 저항 | open-drain·strap·enable 기본값 |
| open-drain | Open-drain (open-collector) output | Low로만 끌어내릴 수 있고 High는 풀업이 만듦 | I2C, 인터럽트, PGOOD, 리셋 공유선 |
| IO bank / VDDIO | IO voltage bank | SoC 핀 묶음마다 다른 IO 전압(1.8 V/3.3 V) | 상대 장치 전압과 안 맞으면 통신 불가·손상 |
| level shifter | Voltage level translator | 1.8 V ↔ 3.3 V 신호 전압 변환 칩 | 방향·enable·속도 제한 확인 |
| VTref | Target voltage reference | 디버거가 보드 IO 전압을 감지하는 핀 | JTAG 안 붙는 원인 1순위 ([BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html)) |
| test point (TP) | Test point | 프로브를 대려고 만든 패드/핀 | 없으면 BGA 밑 신호는 못 잰다 |
| shunt | Current-sense resistor | 아주 작은 저항(mΩ). 양단 전압 = 전류 × 저항 | rail별 전류 측정 |
| 0 Ω resistor | Zero-ohm link | 전선 역할 저항 — 떼면 연결이 끊김 | 블록 격리, 옵션 선택, 전류 측정 자리 |
| DNP / NF / NC | Do Not Populate / Not Fitted / Not Connected(문맥 주의) | 자리만 있고 부품을 안 단 것 | 옵션·디버그용 자리. NC는 "핀 미연결" 뜻도 있음 |
| assembly variant | BOM variant | 같은 PCB에 부품만 다르게 단 버전 | 보드 SN별로 어떤 variant인지 기록 |
| decoupling cap | Decoupling / bypass capacitor | 칩 전원 핀 옆 작은 커패시터, 순간 전류 공급 | 레이아웃에서 핀 가까이 있어야 효과 |
| ferrite bead | Ferrite bead | 고주파 노이즈를 막는 부품 (회로도에선 인덕터 비슷) | 아날로그/PLL rail 분리, 0 Ω처럼 떼서 격리 가능 |
| diff pair | Differential pair | +/− 두 선으로 한 신호를 보냄 (PCIe·MIPI·Ethernet·USB) | 길이 맞춤·극성(P/N) 확인 |
| AC coupling cap | AC coupling capacitor | 차동 고속 신호의 DC를 막는 직렬 커패시터 (PCIe TX 쪽) | 빠지거나 위치 틀리면 링크 실패 |
| stackup | Layer stackup | 기판 층 구성 (신호층·GND층·전원층 순서, 두께) | 임피던스·레퍼런스 플레인 |
| ECO | Engineering Change Order (보드) | rev 간 회로·부품 변경 기록 | rev B에서 새로 생긴 실패의 출발점 |
| DFD | Design for Debug | 디버그·측정이 가능하도록 설계에 넣는 것 | 이 노트의 "HW 팀 요청" |

## 2. 핵심 개념 (정확한 버전)

### 2.1 읽는 순서 9단계 — 무엇을 찾고, 무엇이 잘못될 수 있고, 체크리스트에 뭘 쓰나

| # | 찾을 것 | 회로도에서 보는 법 | 흔한 함정 | 체크리스트 줄 |
|---|---|---|---|---|
| ① | **전원 트리·시퀀싱** | 입력 커넥터에서 시작해 레귤레이터마다 VIN(부모)·EN(누가 켜나)·PGOOD(누구에게 알리나)·FB 저항·출력 net·TP를 따라감. SoC 데이터시트의 power-up 순서 요구와 대조 | EN이 VIN에 직결이라 순서 보장 안 됨, PGOOD 풀업 누락, FB 저항 값 오류, rail 이름 혼동 | rail별 "TP·기대 전압 창·최대 전류·부모·선행 조건" |
| ② | **클럭 소스** | crystal(2핀 + 부하 커패시터 2개) vs oscillator(4핀, 전원 필요) vs clock generator(여러 출력, I2C 설정), 주파수, 전원 rail, SoC 입력 핀, PCIe refclk 경로 | osc 전원이 늦게 켜지는 rail에 붙음, 부하 커패시터 값, refclk 종단 | "osc OUT scope: 주파수·진폭", "PLL lock 비트" ([BV04](2026-11-01_BV04_clocks_osc_pll_refclk.html)) |
| ③ | **리셋 회로** | POR/supervisor IC(전압 감시 + 지연), RC 지연, 버튼, PMIC의 reset 출력, 리셋 선을 **누가 driving 하나**(open-drain 공유선?) | 리셋이 rail보다 먼저 풀림, 디버거·다른 칩이 리셋을 잡고 있음 | "nRESET이 마지막 PGOOD 후 ≥ X ms에 해제" |
| ④ | **strap / boot mode** | SoC 데이터시트의 strap 표 ↔ 각 strap 핀의 풀업/풀다운 저항(실장/DNP), DIP 스위치, 그 핀을 공유하는 다른 장치 | 저항이 DNP인데 내부 풀업 기본값을 착각, 공유 장치가 리셋 중 핀을 driving, 저항 너무 약함 | "BOOT[2:0] 기대 값, 리셋 후 strap 샘플 레지스터로 확인" ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)) |
| ⑤ | **디버그 헤더** | JTAG/SWD 핀맵(표준 커넥터인지: Arm 10/20핀 등), **VTref가 어느 rail에 붙었나**, TRST/SRST 연결, UART TX/RX 방향·전압·USB-UART 브리지 | VTref rail이 DNP 0 Ω 뒤에 있음, TX/RX 교차 실수, UART가 1.8 V인데 3.3 V 어댑터 | "J5: VTref = 1.8 V, TCK 1 MHz부터" ([BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html)) |
| ⑥ | **test point · shunt** | TP 목록, 전류 측정 저항(mΩ, 4단자 Kelvin 연결), 측정용 0 Ω 자리 | 중요한 rail에 TP 없음, BGA 밑 신호를 잴 방법 없음 | "rail X: shunt R_s = 10 mΩ → 2 A면 20 mV" |
| ⑦ | **풀업/풀다운** | I2C SDA/SCL 풀업(값·전압·버스당 한 세트?), open-drain IRQ/PGOOD/리셋 풀업, enable 핀 기본값 | I2C 풀업 중복으로 너무 강함, 풀업이 꺼진 rail에 붙음, enable 기본값 때문에 부품이 리셋 동안 켜짐 | "I2C bus N: 풀업 R, VDD → Rp 범위 검산" |
| ⑧ | **IO 전압 뱅크 · level shifter** | SoC 뱅크별 VDDIO rail, 상대 장치 IO 전압, level shifter 방향·OE·속도 | 1.8 V 뱅크에 3.3 V 장치 직결, 자동 방향 감지 shifter가 강한 풀업과 충돌, shifter 전원 순서 | "뱅크 B의 VDDIO = 1.8 V 측정, 상대 장치 VIH 만족" |
| ⑨ | **DNP · 0 Ω · 점퍼 · variant** | 회로도의 DNP/nf 표시, 노트, BOM의 실장 열, 같은 자리 둘 중 하나만(either-or) 패턴 | 회로도와 BOM 불일치, 보드마다 rework 상태 다름 | "R5 fitted / R4 DNP 육안 확인, 보드 SN별 기록" |

### 2.2 숫자 검산 3종 — 회로도만으로 할 수 있는 것

**(a) 레귤레이터 출력 전압** (가변 출력 레귤레이터의 FB 분압)

```text
        Vout ──┬───────────
               │
              Rtop        Vout = Vref × (1 + Rtop / Rbot)
               │
   FB 핀 ──────┤  ← 레귤레이터는 이 점이 Vref가 되도록 Vout을 조절
               │
              Rbot
               │
              GND
```

- Vref는 레귤레이터 데이터시트 값 (예: Diodes AP64501 = **0.8 V ±1%**, VFB 792–808 mV) [3].
- 검산 결과가 nominal에서 벗어나면 회로도 오류 또는 의도된 여유(예: 케이블 drop 보상으로 5.16 V)일 수 있음 → HW 엔지니어에게 질문.

**(b) 전류 측정 shunt**

- V_shunt = I × R_shunt. 예: 10 mΩ에 2 A → **20 mV** — 그래서 DMM의 mV 레인지나 전류 감지 증폭기(INA 시리즈 등) 필요 [추정: 부품 예시].
- 회로도에서 shunt 양단 net이 **4단자(Kelvin)** 로 따로 빠져 있는지 확인 — 아니면 패드·납땜 저항까지 같이 재서 오차.

**(c) I2C 풀업 범위** [7][8][10]

```text
  Rp(min) = (VDD − VOL(max)) / IOL          ← 너무 작으면(강하면) 장치가 Low로 못 끌어내림
  Rp(max) = tr(max) / (0.8473 × Cb)         ← 너무 크면(약하면) 상승이 느려 규격 rise time 초과

  예: VDD 3.3 V, VOL 0.4 V, IOL 3 mA → Rp(min) ≈ 0.97 kΩ
      Fast-mode 400 kHz: tr(max) 300 ns, Cb 100 pF → Rp(max) ≈ 3.54 kΩ
      → 1–3.5 kΩ 사이. CM4의 GPIO2/3 I2C 풀업 1.8 kΩ [2]는 이 범위 안
```

- 회로도에서 볼 것: 버스에 **풀업이 몇 세트** 붙었나(여러 보드/모듈이 각자 달면 병렬로 너무 강해짐), 풀업 전원이 **항상 켜진 rail** 인가, 장치 수 × 핀 커패시턴스 + 배선으로 Cb 추정.

### 2.3 신호 이름 읽기

| 표기 | 뜻 | 예 |
|---|---|---|
| `n` 접두, `_N`, `_B`, `#`, overbar | active-low (0일 때 활성) | nRPIBOOT, EXT_nRESET, PERST# |
| `_P` / `_N` (차동쌍) | 차동 + / − | PCIE_TX_P / PCIE_TX_N |
| `_PG`, `PGOOD`, `PWROK` | power good | RUN_PG |
| `_EN`, `EN_` | enable | GLOBAL_EN |
| `VDD_*`, `VCC_*`, `+3V3`, `+1V8` | 전원 rail (V 자리에 소수점) | +3.3v, CM4_1V8 |
| `*_SDA/_SCL`, `*_MDIO/_MDC` | I2C / Ethernet PHY 관리 버스 | SCL0, SDA0 |
| `TP*`, `J*`, `U*`, `Q*`, `L*`, `FB*`, `Y*`/`X*` | 테스트 포인트·커넥터·IC·트랜지스터·인덕터·ferrite·crystal | TP4, J2, U16, Q1, L5 |

⚠ `_N`은 문맥 주의: 차동쌍의 N(negative)인지 active-low인지 — 쌍(`_P`)이 있으면 차동.

### 2.4 레이아웃 리뷰 — validation SW 엔지니어가 알아야 하는 만큼

SW 엔지니어가 레이아웃을 "설계"하진 않지만, **나중에 디버그할 수 있는가 / 이상 증상이 어디서 올 수 있나** 를 미리 보는 것은 기대된다 [추정: 면접 기대 수준].

| 항목 | 왜 보나 | 질문 형태 |
|---|---|---|
| **차동쌍·길이 맞춤** (DDR byte lane, PCIe/MIPI 레인) | skew가 크면 training 마진이 줄어듦 | "DDR DQS–DQ 길이 맞춤 규칙과 결과 리포트 있나요?" |
| **P/N 극성 swap · 레인 순서 반전** | PCIe는 링크 학습에서 극성 반전을 처리할 수 있지만 MIPI·일부 SerDes는 설정 필요 [추정: IP별] | "어느 레인이 swap 됐나요? SW 설정이 필요한가요?" (CM4IO PCIe도 라우팅 위해 TX/RX에 PN swap 표기 [1]) |
| **AC coupling cap 위치** (PCIe TX) | 누락·잘못된 쪽이면 링크 실패 | "AC cap이 TX 쪽에 있나요, 값은?" |
| **레퍼런스 플레인 연속성** | 고속 신호가 GND 분할을 넘으면 SI 악화 | "고속 레인이 플레인 split을 지나는 곳이 있나요?" |
| **decoupling 위치** | 핀에서 멀면 droop·노이즈 | "코어 rail decoupling이 BGA 바로 아래 있나요?" |
| **remote sense (Kelvin)** | 레귤레이터가 부하 지점 전압을 보나 | "VDD_CORE 레귤레이터 sense는 SoC 근처에서 받나요?" |
| **TP 접근성** | 커넥터·방열판·케이스 밑 TP는 못 씀 | "방열판 장착 상태에서 TP에 프로브가 들어가나요?" |
| **디버그 헤더 위치·방향** | 케이스/챔버 안에서 연결 가능해야 | "챔버 안에서 JTAG·UART 케이블 연결이 되나요?" |
| **열** | 핫스팟 근처 부품 (crystal·PLL) | "PMIC·SoC 사이 열 경로와 온도 센서 위치는?" |
| **커넥터 핀1 · 실크** | 거꾸로 꽂기·핀맵 실수 | "헤더 핀1 표시와 키(key)가 있나요?" |

### 2.5 BOM 읽기

| BOM 열 | 볼 것 | bring-up 관련 함정 |
|---|---|---|
| Ref des | 회로도와 1:1 | 회로도에만 있고 BOM에 없는 부품 = 미실장 확인 |
| Value / tolerance | FB·strap·풀업 저항 값 | 1% vs 5% — FB 저항은 1% 필요 |
| Manufacturer P/N | 정확한 부품 | **PMIC의 OTP 버전 접미사**가 다르면 기본 전압·순서가 다름 [추정: 벤더 일반 관행] |
| Alternate (2nd source) | 대체 부품 | 대체 crystal의 ESR·부하 커패시턴스 다름 → 발진 마진 |
| DNP / Populate 열 | 실장 여부 | assembly variant별로 다름 |
| Rev / ECO | rev 간 변경 | rev B 새 실패의 출발점 ([BV13](2026-11-01_BV13_board_debug_scenarios.html)) |

- 습관: 보드가 오면 **보드 SN · PCB rev · BOM variant · rework 목록 · 칩 ID/lot** 를 인벤토리에 기록 ([BV14](2026-11-01_BV14_board_validation_plan_automation.html)).

### 2.6 Design-for-debug — HW 팀에 요청할 것 (레이아웃 확정 전에!)

| 요청 | 이유 | 비용 |
|---|---|---|
| 모든 rail에 **TP** (+ GND TP 근처에) | 순서·ripple 측정, 짧은 ground 연결 | 패드 면적 |
| 주요 rail(코어·DDR·NPU [추정])에 **shunt 또는 0 Ω 자리** | rail별 전류·전력, 과전류 원인 격리 | 부품 1개, 아주 작은 전압 손실 |
| 블록 사이 **0 Ω / 점퍼** (rail 분리, 신호 분리) | 합선 rail 찾기, 장치 하나씩 격리 | 부품 |
| **디버그 헤더** 표준 핀맵 + VTref + nSRST/nTRST | JTAG가 생명줄 | 커넥터 |
| **UART 콘솔** 2개 이상 (메인 CPU + 보조 코어/보안 코어) | 멀티코어 bring-up 동시 관찰 | 핀 |
| **strap 변경 수단** (풀업/풀다운 저항 자리 둘 다, DIP 스위치) | boot mode·옵션 실험 | 부품 |
| PMIC/전원 감시 **I2C/PMBus 접근** 헤더 | 보드가 안 켜져도 PMIC fault 레지스터 읽기 | 핀 |
| **상태 LED** (각 PGOOD, 리셋, 하트비트 GPIO) | 장비 없이 상태 파악, 보드 팜 카메라 | LED |
| **원격 제어 가능 전원·리셋·boot mode** (보드 팜용 GPIO/relay 핀) | 자동화 ([BV14](2026-11-01_BV14_board_validation_plan_automation.html)) | 커넥터 |
| **clock out 디버그 핀**, 외부 클럭 주입 옵션 | PLL 확인, 클럭 문제 격리 | 핀·0 Ω |
| 고속 링크용 **probe 가능 지점 / 인터포저 자리** | 프로토콜 애널라이저 | 면적 |
| 온도 센서 근처 TP, 방열판 장착 시 접근성 | PVT 챔버 측정 | 기구 |

### 2.7 회로도 → SW 설정 (Device Tree 등) 연결

회로도에서 뽑은 정보는 체크리스트뿐 아니라 **SW 설정 파일**이 된다 → 상세는 [EL P4](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P4_schematic_to_dt.html).

| 회로도 정보 | SW 쪽 |
|---|---|
| I2C 버스 번호·장치 주소(주소 strap 핀 포함) | DT `i2c@…` 노드, `reg = <0x51>` |
| GPIO 번호·active-low 여부 | DT `gpios = <&gpio 17 GPIO_ACTIVE_LOW>` |
| 인터럽트 선 | DT `interrupts` / `interrupt-parent` |
| 레귤레이터·enable GPIO | DT regulator 노드, `vin-supply` |
| 핀 mux (UART·SPI 어느 핀) | pinctrl |
| 클럭 주파수 | `clock-frequency`, fixed-clock |

## 3. 보드에서 볼 것 · 잴 것 — 공개 회로도 worked example: Raspberry Pi CM4 IO Board

> 출처: Raspberry Pi 공개 문서 **cm4io-datasheet.pdf**(회로도 7장 포함, KiCad) [1]와 **cm4-datasheet.pdf** [2]. 회로도 그림은 옮기지 않고, 내가 읽은 내용을 표·ASCII로 재구성. 저항 값은 PSU·PCIe 시트를 확대 렌더링해서 직접 확인함 [확인됨].

### 3.1 이 보드가 뭔가

- CM4(Compute Module 4: BCM2711 SoC + PMIC + eMMC가 올라간 모듈)를 꽂는 **캐리어 보드**. Raspberry Pi 4의 인터페이스 대부분 + **PCIe Gen 2 x1 소켓**, 듀얼 HDMI, 기가비트 Ethernet(PoE), USB 2.0 허브, MIPI DSI ×2 · CSI-2 ×2, RTC, 팬 컨트롤러 [1].
- **"reference design이자 제품"** 으로 설계됨 [1] → EVB/CRB의 성격: 점퍼·DNP 헤더·TP가 있다. 자동차 SoC 검증 보드의 축소판으로 연습하기 좋다.

### 3.2 전원 트리 (회로도에서 재구성)

```text
 J19 DC jack (+12 V, tip +)  ──TP2
     │
     ▼
 Q1 ideal diode (역극성 보호, DMP3013SFV)
     │ +12V ───────────────────┬───────────────┬──────────────┬──────────────────┐
     │                         │               │              │                  │
     ▼                         ▼               ▼              ▼                  ▼
 U16 AP64501 buck          U15 AP64501 buck   PCIe 슬롯 +12V  팬 커넥터 (+12V,   J20 외부 전원
 (EN → VIN 직결)           (PCIe 전용)                        비조정)            커넥터 (+5V,+12V)
 FB: R27 12k / R28 2.2k    FB: R24 47k / R30 15k
 L5 3.3 µH                 L3 3.3 µH
     │ +5V  ──TP4 (GND TP6)    │ +3.3V ──TP1 → PCIe 슬롯 3.3V
     ▼
 CM4 모듈 (+5V 입력, 9 W 예산)
     │  모듈 안 PMIC (MXL7704)
     ├──▶ CM4 +3.3V 출력 (최대 600 mA) ── R5 0Ω ──▶ GPIO_VREF   (R4 DNP: +1.8V 선택 자리)
     └──▶ CM4 +1.8V 출력 (최대 600 mA)
 +5V ──▶ HDMI 5V (전류 제한 스위치) / USB VBUS (전류 제한 ≈1.2 A)
```

- 읽은 사실 [1]: 12 V→5 V DC-DC가 CM4를 먹이고, 12 V→3.3 V DC-DC는 **PCIe 슬롯 전용**. 12 V는 PCIe 슬롯·외부 전원 커넥터·팬에 직접. PCIe·팬을 안 쓰면 입력은 7.5–28 V 가능. CM4 예산 9 W. 외부 5 V(J20/PoE)로 먹일 땐 **L5를 떼라** — 그래야 보드의 +5 V·+3.3 V 레귤레이터가 안 켜지고 DC 잭으로 5 V가 역류하지 않음.
- 회로도 주석 [1]: "PCIe x1 takes a maximum of +12v @0.6A or +3.3v@3A, 10Watts combined", "TX and RX have PN swaps to improve routing".

### 3.3 숫자 검산 — FB 분압 (회로도 값 → 출력 전압) [확인됨]

| rail | 레귤레이터 | Rtop | Rbot | 계산 (Vref 0.8 V [3]) | 판정 |
|---|---|---|---|---|---|
| +5V | U16 AP64501 | R27 12 kΩ 1% | R28 2.2 kΩ 1% | 0.8 × (1 + 12/2.2) = **5.164 V** | ✅ CM4 요구 ≥ 4.75 V [2] 충족, nominal +3.3% (의도된 여유로 보임 [추정]) |
| +3.3V (PCIe) | U15 AP64501 | R24 47 kΩ 1% | R30 15 kΩ 1% | 0.8 × (1 + 47/15) = **3.307 V** | ✅ nominal +0.2% |

- 함정 관찰: 같은 시트에 R25 20 kΩ/R23 12 kΩ도 보이는데, 이건 **COMP 핀 보상 회로**(직렬 1.2 nF와 함께)의 저항이다 — 텍스트만 긁어서 읽으면 FB 저항으로 착각하기 쉽다. **핀 이름(FB vs COMP)을 따라가야** 한다.
- 또 하나: C59·C61·C63·C65는 **"nf"(not fitted)** — 보상·feed-forward 튜닝용 빈 자리 (DNP 패턴의 전형).

### 3.4 시퀀싱 — CM4 데이터시트의 power-up 순서 [2]

```text
 [CM4 datasheet §5.1의 순서를 그림으로 — 시간축은 비례 아님]
 +5V        ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   ① 단조 증가로 4.75 V 이상, 계속 유지
 GLOBAL_EN  ______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   ② 내부 RC 지연으로 +5V 뒤에 상승
 +3.3V      ________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   ③
 +1.8V      ____________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   ④ +3.3V 후 ≥ 1 ms
 RUN_PG     ____________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   ⑤ +1.8V 후 ≥ 10 ms
 EXT_nRESET ______________________________________/‾‾‾‾‾‾‾‾‾‾‾‾   ⑥ RUN_PG 후 ≥ 1 s
 nRPIBOOT   (USB 부팅하려면 +5V 상승 후 2 ms 이내에 Low여야 함)
 EEPROM_nWP (쓰기 보호하려면 전원 인가 전부터 Low)
 규칙: +5V 인가 전에 어떤 핀에도 전압을 걸지 말 것 (back-powering 금지)
```

- 측정 계획: scope 4채널 = +5V(TP4) · +3.3V(J8 핀1) · RUN_PG(J1 핀3 — 헤더 미실장이라 패드에) · EXT_nRESET, 트리거 = +5V 상승. +1.8V는 IO 보드에 **TP가 없다** → 두 번째 캡처로 +3.3V와 +1.8V를 같이 잡거나 CM4 커넥터 패드 근처 측정 → **DFD 요청 후보** (스크립트도 같은 경고를 냄, §6).
- GLOBAL_EN을 Low로 > 1 ms 당기면 CM4 전원 시스템이 재시작 [2] → 보드 팜에서 **원격 전원 사이클 핀**으로 쓸 수 있다 (J1 미실장 헤더에 relay 연결) — 회로도에서 자동화 훅을 찾는 예.
- 전류 기대값 [2]: GLOBAL_EN=0이면 ≈ 15 µA, 켜졌지만 SW 종료 상태 ≈ 8 mA, idle ≈ 400 mA, 동작 ≈ 1.4 A (5 V 기준, OS·작업에 따라 큼) → **bench PSU 전류 제한을 정하는 근거**.

### 3.5 Strap · 점퍼 · DNP · 0 Ω 옵션 표 (회로도/데이터시트에서 뽑은 것) [1][2]

| 자리 | 기본 상태 | 바꾸면 | bring-up에서 쓰는 법 |
|---|---|---|---|
| J2 핀 1-2 nRPIBOOT | 열림 | 단락 → eMMC 부팅 중단, **USB(rpiboot) 부팅** 강제 | "벽돌 복구" 경로 — eMMC 이미지가 망가졌을 때 (= SoC의 fallback boot mode) |
| J2 핀 3-4 EEPROM_nWP | 열림 (모듈 안 100 kΩ 풀업) | 단락 → 부트 EEPROM 쓰기 보호 | 제품은 Low 권장 [2]; 검증 중엔 열어서 부트로더 업데이트 |
| R5 0 Ω / R4 DNP | R5 실장 → GPIO_VREF = +3.3 V | R5 떼고 R4 실장 → **1.8 V IO** (둘 중 하나만!) | IO 뱅크 전압 옵션의 교과서 예. 납땜 필요 |
| L5 인덕터 | 실장 | 떼면 보드 +5V 레귤레이터 출력 차단 | 외부 5 V 주입(J20/PoE) 시 역류 방지; **rail 격리 지점**으로도 사용 가능 [추정] |
| J6 점퍼 2개 | 사용처에 따라 | 단락 → I2C를 CSI0/DSI0 커넥터로 연결 | 카메라 안 잡힐 때 첫 확인 대상 |
| J1 (미실장) GLOBAL_EN/RUN_PG | 헤더 없음 | 헤더 납땜 → 외부 전원 제어·리셋 | 보드 팜 원격 리셋/전원 |
| J3 (미실장) WL_nDIS / BT_nDIS | 헤더 없음 | GND 연결 → 무선/BT 비활성 | 기능 격리 실험 |
| C59/C61/C63/C65 | nf | 튜닝용 | 레귤레이터 루프 튜닝 자리 — 건드리지 않음 |
| J2 핀 13-14 | 열림 | 버튼 → 저전력 상태에서 깨우기 | 회로도 주석: CM4가 깨어 있으면 RUN_PG가 High라 효과 없음 |

### 3.6 I2C 장치 지도 — "I2C 장치가 응답 안 함"을 회로도로 푸는 예

| 장치 | 버스 · 7-bit 주소 | 활성화 조건 | 회로도에서 확인할 것 |
|---|---|---|---|
| RTC PCF85063AT | i2c-10 · **0x51** | `dtparam=i2c_vc=on` (config.txt) | 풀업 위치·전원(배터리 CR2032 경로), RTC 알람이 CM4 깨우기/리셋에 연결됨 |
| 팬 컨트롤러 EMC2301 | i2c-10 · **0x2f** | 위와 같음 | 팬 +12 V는 비조정 입력 → 12 V 없으면(PoE HAT) 팬 동작 안 함 |
| CSI0/DSI0 커넥터 장치 | (카메라/디스플레이 I2C) | **J6 점퍼 2개** 장착 | 점퍼 안 끼우면 버스 자체가 커넥터로 안 감 |

```text
 [예시 출력 — 실제 보드에서 캡처한 것 아님. 데이터시트 [1]의 주소로 기대 모습을 그린 것]
 $ sudo i2cdetect -y 10
      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
 00:                         -- -- -- -- -- -- -- --
 10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
 20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 2f
 30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
 40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
 50: -- 51 -- -- -- -- -- -- -- -- -- -- -- -- -- --
 60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
 70: -- -- -- -- -- -- -- --
```

- 줄별 해석: `2f` = 팬 컨트롤러 EMC2301 응답(ACK) · `51` = RTC 응답 · 나머지 `--` = 그 주소에서 아무도 ACK 안 함. `i2c-10` 자체가 없으면 → `dtparam=i2c_vc=on` 미설정(**SW 설정** 문제), 버스는 있는데 `2f`만 없으면 → 팬 컨트롤러 전원·주소·실장(**보드** 문제) 확인. 모든 주소가 응답하면(전부 숫자) → SDA가 Low에 붙은 **stuck bus** 의심 → [BV10](2026-11-01_BV10_board_interface_tests.html) 9-클럭 복구.
- 회로도 체크 순서 (I2C NACK): ① 풀업 있나·값·어느 rail에 붙었나 → ② 장치 VDD rail이 켜졌나(EN 체인) → ③ **주소 strap 핀**(ADDR/A0 핀이 어디 묶였나) → ④ IO 전압 일치·level shifter 방향/OE → ⑤ 장치 리셋/enable 핀 상태 → ⑥ 점퍼/0 Ω으로 버스가 끊겨 있지 않나 (J6 사례).

### 3.7 이 보드의 Day-1 체크리스트 — 스크립트 출력 (실제 실행)

[bringup_checklist.py](onsitePrep/bv02/bringup_checklist.py)에 §3.2–3.5를 CSV로 적어 넣고 실행한 **실제 출력**의 체크리스트 부분 (전체: [run_cm4io.txt](onsitePrep/bv02/outputs/run_cm4io.txt)):

| # | Phase | Item | Where | Expect |
|---|---|---|---|---|
| 1 | A pre-power | VIN_12V: resistance to GND (power OFF) | TP2 | not a short; compare with golden board |
| 2 | A pre-power | P5V: resistance to GND (power OFF) | TP4 | not a short; compare with golden board |
| 3 | A pre-power | P3V3_PCIE: resistance to GND (power OFF) | TP1 | not a short; compare with golden board |
| 4 | A pre-power | CM4_3V3: resistance to GND (power OFF) | J8 pin1 (HAT header) | not a short; compare with golden board |
| 5 | A pre-power | CM4_1V8: resistance to GND (power OFF) | (no TP!) | not a short; compare with golden board |
| 6 | B BOM/option | R5_R4_GPIO_VREF: GPIO_VREF select - only one of R4/R5 may be fitted | visual | R5=0R fitted (3.3V) / R4=nf (1.8V) |
| 7 | B BOM/option | L5: remove L5 when feeding external 5V via J20 or PoE | visual | fitted by default |
| 8 | B BOM/option | J2_1-2_nRPIBOOT: fit to force USB (rpiboot) boot - eMMC recovery | J2 | jumper open by default |
| 9 | B BOM/option | J2_3-4_EEPROM_nWP: fit to write-protect CM4 boot EEPROM | J2 | jumper open by default |
| 10 | B BOM/option | J6_CSI0_DSI0_I2C: fit both if CSI0 or DSI0 used (routes I2C) | J6 | jumpers per use case |
| 11 | B BOM/option | C59_C61_C63_C65: buck comp/feed-forward placeholders - DNP | visual | nf (not fitted) |
| 12 | C power-up | VIN_12V = 12.0 V (J19 barrel jack -> Q1 ideal diode (reverse-polarity)) | TP2 | 11.400..12.600 V |
| 13 | C power-up | P5V = 5.0 V (U16 AP64501 buck (5V@3A; EN tied to VIN), from VIN_12V) | TP4 | 4.750..5.250 V, <= 3000 mA |
| 14 | C power-up | P3V3_PCIE = 3.3 V (U15 AP64501 buck (PCIe slot only), from VIN_12V) | TP1 | 3.135..3.465 V, <= 3000 mA |
| 15 | C power-up | GLOBAL_EN (CM4 internal RC on GLOBAL_EN) | J1 pin1 (header not fitted) | high >= 0 ms after P5V |
| 16 | C power-up | CM4_3V3 = 3.3 V (CM4 on-module PMIC (MXL7704), from P5V) | J8 pin1 (HAT header) | 3.135..3.465 V, <= 600 mA |
| 17 | C power-up | CM4_1V8 = 1.8 V (CM4 on-module PMIC (MXL7704), from P5V) | (no TP!) | 1.710..1.890 V, <= 600 mA |
| 18 | C power-up | RUN_PG (CM4 power-good / run) | J1 pin3 (header not fitted) | high >= 10 ms after CM4_1V8 |
| 19 | C power-up | EXT_nRESET (CM4 nEXTRST output) | (no TP!) | high >= 1000 ms after RUN_PG |

- 해석: A(전원 넣기 전 저항 — 합선은 전원 넣기 전에 찾는다 [4]) → B(BOM·옵션 육안 확인 [5]) → C(전원 넣고 의존성 순서대로, 단계적으로 [5]) 세 단계. "(no TP!)" 두 줄 = **측정 수단을 HW 팀과 정해야 하는 곳**. 전압 창(±5%)은 내가 정한 합격 기준이라 데이터시트 수치(예: +5V ≥ 4.75 V)와 일치하는지 다시 확인해야 한다 — 이 보드에선 +5V 창 하한 4.75 V가 CM4 요구와 정확히 같다.

### 3.8 자동차 AI SoC 검증 보드라면 — 같은 틀의 체크리스트 템플릿 `[추정]`

> Rivian 보드 실물 정보 없음. 일반적인 카메라 중심 AI SoC(LPDDR, PCIe, 자동차 Ethernet, MIPI CSI-2 + SerDes)를 가정한 **틀**.

| 영역 | 회로도에서 뽑을 것 | 측정·확인 | 통과 기준 예 |
|---|---|---|---|
| 입력 전원 | 12 V 차량 입력 보호(역극성·과전압), 벤치 입력 커넥터 | DMM, 전류 제한 PSU | 무부하 전류 기대값 이내 |
| 코어/NPU rail | 멀티페이즈 레귤레이터, remote sense, shunt, AVS 제어 경로(I2C/PMBus) | TP + scope(ramp·ripple), shunt 전류 | 전압 창, ripple 기준, 순서 |
| LPDDR rail | VDD1/VDD2/VDDQ 순서, VREF, ZQ 저항 | 다채널 scope, 순서 | 메모리 데이터시트 순서 준수 ([BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)) |
| PMIC | 품번·OTP variant, fault 핀, I2C 주소 | I2C로 ID·fault 레지스터 | fault 0, 설정값 = 회로도 의도 ([BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html)) |
| 클럭 | 메인 osc, PCIe 100 MHz refclk 버퍼, Ethernet PHY 25 MHz [추정: 흔한 값], 카메라 SerDes 클럭 | scope/주파수 카운터, PLL lock | 주파수 ppm, lock |
| 리셋·strap | reset supervisor, boot mode strap, 보안/디버그 모드 strap | scope, strap 레지스터 | 기대 boot mode |
| 디버그 | JTAG(VTref), UART ×N, 보안 디버그 언락 경로 | 디버거 연결 | IDCODE, halt |
| PCIe | 레인 swap·극성, AC cap, PERST#·CLKREQ# 풀업 | LnkSta, 링크 속도/폭 ([LP 트랙](2026-10-31_LP00_linux_pcie_track.html)) | 기대 Gen/폭 |
| 자동차 Ethernet | PHY strap(주소·모드·master/slave), MDIO 풀업, 25 MHz 클럭 | MDIO로 PHY ID 레지스터 2·3 | ID 일치, link up ([BV10](2026-11-01_BV10_board_interface_tests.html)) |
| 카메라 | CSI-2 레인 매핑, deserializer I2C 주소/strap, PoC(전원 동축) 회로 | 센서 test pattern, CSI-2 에러 카운터 | 프레임 카운트·CRC 0 |
| 온도 | 온도 센서 위치, 팬/방열 | hwmon/온칩 센서 | 챔버 조건에서 판독 |

### 3.9 Linux가 올라온 뒤 회로도와 대조하기 `[예시 출력]`

```text
 [예시 출력 — 실제 보드 캡처 아님; 명령과 볼 포인트를 보여 주기 위한 모양]
 $ sudo cat /sys/kernel/debug/regulator/regulator_summary
  regulator          use open bypass  opmode  voltage  current   min     max
 ---------------------------------------------------------------------------
  regulator-dummy      3    2      0  unknown     0mV     0mA     0mV     0mV
  vdd_3v3              4    3      0  unknown  3300mV     0mA  3300mV  3300mV
  vdd_1v8              2    1      0  unknown  1800mV     0mA  1800mV  1800mV
 → DT가 아는 rail 목록 = 회로도의 rail 목록과 같은가? 빠진 rail은 DT에 없는 것(고정 rail이면 정상일 수 있음)

 $ gpioinfo | grep -i -E 'reset|en|boot'
 → 회로도의 enable/reset GPIO가 맞는 번호·active-low로 잡혔나

 $ cat /sys/class/hwmon/hwmon*/name ; cat /sys/class/hwmon/hwmon*/temp1_input
 → 온도 센서가 회로도에 있는 만큼 보이나 (값은 m°C 단위)
```

- 포인트: Linux 레벨에서도 **"회로도에 있는 것 = SW가 아는 것"** 을 대조하는 습관. 차이는 DT 버그이거나 보드 실장 차이.

### 3.10 다른 공개 EVB로 연습 — AMD/Xilinx ZCU102 (짧게)

- ZCU102(Zynq UltraScale+ MPSoC 평가 보드)의 사용자 가이드는 **UG1182** [6]. 보드에 **SW6 DIP 스위치로 boot mode** 를 고르는데(JTAG = 모드 핀 0000, 기본은 QSPI32, SD 부팅 설정 별도), "ON 쪽이 0" 이라는 점이 함정이다 [6][추정: 검색 요약 기준, UG1182 원문 표는 직접 확인 못 함].
- 보드에 **system controller** 가 있어 GUI(SCUI)로 rail 전압·전력과 DIP 스위치 상태를 읽고, BIST로 보드 기능을 점검할 수 있다 [6][추정: 같은 출처] → "PMBus로 rail 텔레메트리를 읽는 검증 보드"의 예. [LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html)과 함께 보면 Zynq boot 흐름까지 연결된다.
- 연습: UG1182 전원 장(power system) 표를 열어 §3.7처럼 CSV로 옮기고 스크립트를 돌려 보기 (§6 연습 2).

## 4. 꼬리 질문 체인

### 체인 A — "You get the schematic a week before boards arrive. What do you do with it?" (BV00 원본)

Q → 꼬리1 "What's on your bring-up checklist from the schematic?" → 꼬리2 "How do you know which rails must come up first?" → 꼬리3 "What would you ask the HW engineer to add for debuggability?" → 꼬리4 "The layout is already frozen. Now what?"

<details>
<summary>A-Q. You get the schematic a week before boards arrive. What do you do with it?</summary>

- **EN**: "I read it in bring-up order and turn it into two things: a checklist and a question list. Power tree first, then clocks, reset, straps, debug headers, test points, pull-ups and IO voltages, and finally every DNP and zero-ohm option. For each item I write where to measure, what I expect and the pass criterion. I check numbers — feedback dividers, pull-up values, strap values against the SoC's strap table. Anything I can't measure, isolate or control remotely goes to the hardware engineer as a design-for-debug request, and anything I don't understand becomes a question before the boards arrive rather than during bring-up."
- 한국어 핵심: bring-up 순서로 읽기 → 체크리스트(어디·기대·합격) + 질문 목록, 숫자 검산, 측정 불가 = DFD 요청.
</details>

<details>
<summary>A-꼬리1. What's on your bring-up checklist from the schematic?</summary>

- **EN**: "Before power: a resistance check of every rail to ground at its test point, compared with a golden board, and a visual check of the assembly options — which straps, zero-ohm links and jumpers are fitted. Then, in power-up order: each rail's voltage window and current limit, the enable and power-good chain, clock outputs and PLL lock, reset release timing, the strap values the chip should sample, JTAG with the correct VTref, and the UART pins and levels. Each line has the reference designator or test point, the instrument, and the expected value, so anyone on the team can execute it and the results go into the bring-up log against the board serial number."
- 한국어 핵심: 전원 전 저항·옵션 육안 → 순서대로 rail 창·전류·EN/PG·클럭·리셋·strap·JTAG VTref·UART, 줄마다 TP·장비·기대값, 보드 SN 기록.
</details>

<details>
<summary>A-꼬리2. How do you know which rails must come up first?</summary>

- **EN**: "Two sources, and they have to agree. The SoC and memory datasheets specify the required power-up order and timing — for example the CM4 datasheet requires 5 V, then the enable, then 3.3 V, then 1.8 V at least a millisecond later, power-good ten milliseconds after that, and reset a second after power-good. The schematic shows what the board actually implements: each regulator's input, what drives its enable and where its power-good goes. I draw that as a dependency graph and check it satisfies the datasheet. I actually wrote a small script that takes the power tree as a CSV, orders it, flags cycles and missing test points, and prints the checklist."
- 한국어 핵심: 데이터시트 요구 순서 vs 회로도 구현(VIN·EN·PG) → 의존성 그래프로 대조 (CM4 예 숫자), 스크립트화.
</details>

<details>
<summary>A-꼬리3. What would you ask the HW engineer to add for debuggability?</summary>

- **EN**: "A test point on every rail with a ground nearby; current-sense resistors or at least zero-ohm footprints on the big rails so I can measure per-rail current; zero-ohm links or jumpers between blocks so I can isolate a short or a misbehaving device; a standard JTAG header with VTref and system reset, plus UART consoles for each processor; footprints for both pull-up and pull-down on every strap, or a DIP switch; I2C or PMBus access to the PMIC from a header so I can read fault registers even when the SoC is dead; LEDs on power-good and reset; and remote control of power, reset and boot mode for the board farm."
- 한국어 핵심: rail TP · shunt/0 Ω · 블록 격리 0 Ω · JTAG(VTref·SRST)+UART · strap 양쪽 자리/DIP · PMIC 접근 헤더 · LED · 원격 전원/리셋/boot mode.
</details>

<details>
<summary>A-꼬리4. The layout is already frozen. Now what?</summary>

- **EN**: "Then I find what's already there and plan around the gaps. Vias, component pads and connector pins can serve as test points; a zero-ohm or ferrite bead in series can be lifted to isolate or to insert a current measurement; decoupling capacitor pads are good probe points for a rail. I write down which measurements will need rework — fine wires to a pad, a sense resistor in place of a zero-ohm — and get those agreed with the hardware team before the boards arrive. And the requests go on the list for the next board revision."
- 한국어 핵심: 기존 via·패드·커넥터·decap 패드를 TP로, 직렬 0 Ω/ferrite를 떼서 격리·전류 측정, rework 계획 사전 합의, 다음 rev 요청.
</details>

### 체인 B — "An I2C device doesn't respond. What do you look at in the schematic?" (BV00 원본)

Q → 꼬리1 "The pull-ups look right. What next?" → 꼬리2 "How do you pick the pull-up value?" → 꼬리3 "The scan shows a device at every address."

<details>
<summary>B-Q. An I2C device doesn't respond. What do you look at in the schematic?</summary>

- **EN**: "First the pull-ups — are there any, what value, and on which rail, because a pull-up on a rail that's off makes the bus dead. Then the device's own supply and its enable chain — is that rail actually up? Then the address strap pins, because the 7-bit address in the driver has to match how the address pins are tied. Then the voltage levels — is the device on the same IO voltage as the SoC bank, or is there a level shifter, and is it enabled and the right direction? Then the device's reset or enable pin, and finally whether a jumper or zero-ohm is breaking the bus — on the CM4 IO board, the camera I2C only reaches the connector if two jumpers are fitted."
- 한국어 핵심: 풀업(유무·값·rail) → 장치 전원·EN → 주소 strap → IO 전압·level shifter → 장치 리셋 → 점퍼/0 Ω 끊김 (CM4IO J6 예).
</details>

<details>
<summary>B-꼬리1. The pull-ups look right. What next?</summary>

- **EN**: "I go to the scope. I probe SCL and SDA at the device, trigger on the start condition, and check three things: the levels — does high reach the rail and low go below the threshold; the rise time against the spec for the speed mode; and whether the device ACKs the ninth bit. If there's no ACK, I check the address on the wire matches the expected one, including the read/write bit confusion between 7-bit and 8-bit addresses. If there's no traffic at all, it's the controller side — pin mux or the controller clock — which is a software or SoC configuration question."
- 한국어 핵심: scope로 레벨·rise time·9번째 비트 ACK, 7-bit/8-bit 주소 혼동, 트래픽 없음 = 핀 mux/컨트롤러 클럭(SW·SoC 설정).
</details>

<details>
<summary>B-꼬리2. How do you pick the pull-up value?</summary>

- **EN**: "It's bounded on both sides. The minimum comes from sink current: VDD minus VOL max, divided by the IOL the weakest device can sink — about one kilo-ohm at 3.3 volts and 3 milliamps. The maximum comes from rise time: the allowed rise time divided by 0.8473 times the bus capacitance — for 400 kHz with 100 picofarads that's about 3.5 kilo-ohms. So I estimate the bus capacitance from devices and trace length, pick something in that window, and make sure pull-ups aren't duplicated on several modules, because in parallel they get too strong."
- 한국어 핵심: Rp(min) = (VDD−VOL)/IOL ≈ 1 kΩ, Rp(max) = tr/(0.8473·Cb) ≈ 3.5 kΩ(400 kHz, 100 pF), 중복 풀업 주의.
</details>

<details>
<summary>B-꼬리3. The scan shows a device at every address.</summary>

- **EN**: "That means SDA is stuck low — every address looks like an ACK because the line never goes high. Usually a slave was interrupted mid-transfer, for example by a reset of the master only, and it's still holding SDA waiting for clocks. The recovery is to clock SCL up to nine times until SDA is released, then send a STOP. If it doesn't release, something is driving the line — a device that's partly powered, or a short — and that's back to the schematic and the scope."
- 한국어 핵심: 전 주소 응답 = SDA stuck low → SCL 최대 9클럭 + STOP 복구, 안 풀리면 부분 전원·합선 (BV10).
</details>

### 체인 C — "Why are there 0-ohm resistors and DNP parts everywhere on a validation board?" (BV00 원본)

Q → 꼬리1 "How would you use them during bring-up?" → 꼬리2 "What's the risk of having so many options?"

<details>
<summary>C-Q. Why are there 0-ohm resistors and DNP parts everywhere on a validation board?</summary>

- **EN**: "Because a validation board is built to answer questions, not to be cheap. A zero-ohm resistor is a link you can remove, so it lets you isolate a block, break a rail to find a short, or replace it with a sense resistor to measure current. Pairs of footprints where only one is fitted select options — a strap high or low, an IO bank at 1.8 or 3.3 volts, an alternative clock source. DNP footprints are placeholders for tuning or for a fix the designer anticipated — filter or compensation parts, termination, an extra pull-up. On the CM4 IO board, for example, the GPIO voltage is chosen by fitting R5 for 3.3 V or R4 for 1.8 V, never both."
- 한국어 핵심: 0 Ω = 떼어낼 수 있는 연결(격리·rail 분리·전류 측정), 쌍 패턴 = 옵션 선택, DNP = 튜닝·대비용 자리 (CM4IO R4/R5 예).
</details>

<details>
<summary>C-꼬리1. How would you use them during bring-up?</summary>

- **EN**: "If a rail is shorted, I lift the zero-ohm links downstream one by one until the short moves, which tells me which branch it's in. If a device misbehaves on a shared bus, I isolate it. If I need per-rail current, I swap the zero-ohm for a sense resistor. If the boot mode is wrong, I move the strap resistor. And every one of those changes is a rework that goes in the log against the board serial number, because three weeks later two boards behaving differently is often explained by a rework someone forgot."
- 한국어 핵심: 합선 분기 찾기, 공유 버스 장치 격리, 0 Ω → shunt 교체, strap 이동, 모든 rework를 보드 SN별 기록.
</details>

<details>
<summary>C-꼬리2. What's the risk of having so many options?</summary>

- **EN**: "Configuration drift. Boards that look identical can be in different states, so a failure might really be a difference in fitted parts. The mitigations are a BOM variant per build, a rework log per serial number, an inventory the automation reads before it runs, and a quick visual or electrical check of the key options at bring-up. Also, a zero-ohm link must not bypass a fuse or a protection element, and its current rating has to fit the rail."
- 한국어 핵심: 보드 간 설정 표류 → variant BOM·rework 로그·인벤토리·옵션 확인, 0 Ω가 보호 소자 우회 금지·전류 정격.
</details>

### 체인 D — "As a software person, what do you look for in a layout review?" (추가)

Q → 꼬리1 "DDR training margins are small on one byte lane. What in the layout would you check?"

<details>
<summary>D-Q. As a software person, what do you look for in a layout review?</summary>

- **EN**: "I'm not reviewing the design rules — the SI and layout engineers do that. I look for what affects how I validate and debug: whether every rail test point is reachable with the heatsink and in the chamber; where the debug headers are and whether cables fit; lane swaps and polarity inversions on PCIe, MIPI and Ethernet, because some need software configuration; where AC coupling caps are; whether regulators sense voltage at the load; and the length-matching reports for DDR and high-speed lanes, so that if one byte lane has a small margin later, I know whether layout is a suspect."
- 한국어 핵심: 설계 규칙은 SI/레이아웃 담당 — 나는 TP 접근성·헤더 위치·레인 swap/극성(SW 설정)·AC cap·remote sense·길이 맞춤 리포트.
</details>

<details>
<summary>D-꼬리1. DDR training margins are small on one byte lane. What in the layout would you check?</summary>

- **EN**: "Length matching of that byte lane's DQ and DQS against the others, and whether it changes layers or crosses a split in the reference plane; vias count; whether it runs near a switching regulator or another noisy aggressor; and decoupling and VREF routing near that part of the memory. Then I check whether the small margin follows the board or the chip — swap boards with the same chip — and compare it across several boards, because a layout issue shows up on every board at the same lane."
- 한국어 핵심: 해당 byte lane 길이 맞춤·층 변경·플레인 split·via·노이즈원 근접·VREF/decap → 보드/칩 swap, 모든 보드 같은 lane이면 레이아웃.
</details>

### 체인 E — "Two boards from the same build behave differently. What in the BOM/schematic do you check?" (추가)

<details>
<summary>E-Q. Two boards from the same build behave differently. What do you check?</summary>

- **EN**: "First that they're really the same: PCB revision, BOM variant, rework log, and the chip ID and lot on each. Then the assembly options that matter for the symptom — straps, zero-ohm links, jumpers — checked physically, not trusted from paperwork. Then alternate parts: the same reference designator can be populated with a second-source part — a crystal with a different load capacitance, or a PMIC with a different OTP configuration — and the BOM or the part marking tells me. If all that matches, I swap the chip or the suspect part between the boards and see whether the behavior follows it."
- 한국어 핵심: PCB rev·BOM variant·rework·칩 lot 비교 → 옵션 실물 확인 → 대체 부품(crystal·PMIC OTP) → swap으로 따라가는지.
</details>

## 5. Don 경험 연결

- **회로도로 신호 추적 (Apple)**: "Integrating wireless chipsets at Apple, the first step of every root-cause was the schematic — tracing PCIe, I2C, SPMI and RFFE lines from the SoC to the RF module, checking pull-ups, voltage domains and which side drives what — and then confirming on the bench with a scope and protocol analyzer. [숫자 채우기: 대표 사례 1개 — 증상, 회로도에서 찾은 원인, 측정 증거]"
- **보드 리뷰 관점 (SK hynix)**: "When I built the chamber test platform at SK hynix, I learned to read the test board design for what I'd need to measure — where the rails could be probed inside the chamber and how to control power remotely. [숫자 채우기: 보드 수/채널 수]"
- **pre-si 쪽 연결 (Solidigm)**: "On the FPGA platform, the equivalent of the schematic review was the platform pin and clock map — which interfaces were real, which were stubbed — and I kept that as data so tests could skip what the platform didn't support."
- 솔직 포인트: "I'm a firmware engineer, not a board designer — my value in a schematic review is turning it into a measurable bring-up plan and asking for debug access early." (과장 없이, 강점으로 전환)

## 6. 실습 / 코드

### 6.1 bringup_checklist.py — 회로도에서 읽은 전원 트리 CSV → 검산 + 체크리스트 (실행 검증됨)

- 파일: [bringup_checklist.py](onsitePrep/bv02/bringup_checklist.py) (Python 3 stdlib만) · 데이터: [cm4io_power.csv](onsitePrep/bv02/cm4io_power.csv) (§3의 CM4IO 실제 회로도/데이터시트 값) · [cm4io_power_broken.csv](onsitePrep/bv02/cm4io_power_broken.csv) (negative control) · [cm4io_measured_example.csv](onsitePrep/bv02/cm4io_measured_example.csv) (`[예시 값]` — 실제 측정 아님)
- 하는 일: ① parent/after 간선으로 **위상 정렬** (Kahn) → 전원 인가 순서, 정의 안 된 이름·**순환 의존** 검출 ② **FB 분압 검산** Vout = Vref × (1 + Rtop/Rbot) vs nominal ± tol ③ TP 없는 rail · 전류 예산 없는 rail **경고(DFD ask)** ④ A(전원 전 저항) / B(BOM 옵션) / C(전원 인가 순서) 체크리스트 Markdown 표 ⑤ `--measured`로 PASS/FAIL 채점, ERROR나 FAIL이 있으면 exit 1 (CI/자동화에서 사용).

```bash
cd onsitePrep/bv02
python3 -I bringup_checklist.py cm4io_power.csv                                    # 정상 데이터
python3 -I bringup_checklist.py cm4io_power.csv --measured cm4io_measured_example.csv  # 채점
python3 -I bringup_checklist.py cm4io_power_broken.csv                             # negative control
```

**실제 출력 1 — 정상 CSV의 검산·순서 부분** ([전체](onsitePrep/bv02/outputs/run_cm4io.txt)):

```text
## Schematic-reading findings

- [WARN] VIN_12V: no current budget - needed to set PSU current limit / spot shorts
- [INFO] P5V: FB divider 0.8 V x (1 + 12k/2.2k) = 5.164 V (+3.3% vs nominal 5.0 V, tol +/-5%)
- [INFO] P3V3_PCIE: FB divider 0.8 V x (1 + 47k/15k) = 3.307 V (+0.2% vs nominal 3.3 V, tol +/-5%)
- [WARN] CM4_1V8: no test point - DFD ask: add TP (or name a probe-able pad/pin)

## Power-up order (parent + after edges)

VIN_12V -> P5V -> P3V3_PCIE -> GLOBAL_EN -> CM4_3V3 -> CM4_1V8 -> RUN_PG -> EXT_nRESET
...
Summary: 0 ERROR, 2 WARN, 0 FAIL, 19 checklist items
exit=0
```

- 해석: FB 검산 두 줄이 §3.3 손계산과 일치. 순서는 CM4 데이터시트 §5.1과 같은 사슬(5V → GLOBAL_EN → 3.3V → 1.8V → RUN_PG → EXT_nRESET). WARN 2건: 입력 12 V의 전류 예산이 CSV에 없음(데이터시트는 "CM4 9 W 예산"만 줌 → PSU 전류 제한을 정하려면 HW 팀에 물어볼 것), +1.8 V에 TP 없음(→ DFD 요청).

**실제 출력 2 — 측정값 채점** ([전체](onsitePrep/bv02/outputs/run_cm4io_measured.txt), 측정값은 `[예시 값]`):

```text
| 13 | C power-up | P5V = 5.0 V (U16 AP64501 buck (5V@3A; EN tied to VIN), from VIN_12V) | TP4 | 4.750..5.250 V, <= 3000 mA | PASS (5.14) |
| 16 | C power-up | CM4_3V3 = 3.3 V (CM4 on-module PMIC (MXL7704), from P5V) | J8 pin1 (HAT header) | 3.135..3.465 V, <= 600 mA | PASS (3.31) |
| 17 | C power-up | CM4_1V8 = 1.8 V (CM4 on-module PMIC (MXL7704), from P5V) | (no TP!) | 1.710..1.890 V, <= 600 mA | FAIL (1.62) |
| 18 | C power-up | RUN_PG (CM4 power-good / run) | J1 pin3 (header not fitted) | high >= 10 ms after CM4_1V8 | PASS (14 ms) |
| 19 | C power-up | EXT_nRESET (CM4 nEXTRST output) | (no TP!) | high >= 1000 ms after RUN_PG | PASS (1020 ms) |
Summary: 0 ERROR, 2 WARN, 1 FAIL, 19 checklist items
exit=1
```

- 해석: 일부러 넣은 1.62 V(1.8 V −10%)를 FAIL로 잡고 exit 1 → 자동화 러너가 "다음 단계(JTAG)로 가지 말라"고 판단할 수 있다. (실제 보드라면 이 FAIL은 PMIC 설정·부하·측정 위치 문제를 의심 → [BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html))

**실제 출력 3 — negative control (일부러 틀린 회로도 읽기)** ([전체](onsitePrep/bv02/outputs/run_broken.txt)):

```text
- [ERROR] P3V3_PCIE: parent 'VIN12V' is not defined in the CSV
- [ERROR] CM4_3V3,CM4_1V8: dependency cycle in parent/after edges - sequencing cannot be satisfied
- [WARN] VIN_12V: no current budget - needed to set PSU current limit / spot shorts
- [ERROR] P5V: FB divider 0.8 V x (1 + 12k/1.5k) = 7.200 V (+44.0% vs nominal 5.0 V, tol +/-5%)
- [INFO] P3V3_PCIE: FB divider 0.8 V x (1 + 47k/15k) = 3.307 V (+0.2% vs nominal 3.3 V, tol +/-5%)
- [WARN] P3V3_PCIE: no test point - DFD ask: add TP (or name a probe-able pad/pin)
- [WARN] CM4_1V8: no test point - DFD ask: add TP (or name a probe-able pad/pin)
...
Summary: 3 ERROR, 3 WARN, 0 FAIL, 6 checklist items
exit=1
```

- 해석: 심어 둔 세 가지 오류(R28을 1.5 k로 잘못 읽음 → 7.2 V, parent 오타, 3.3V↔1.8V 순환)를 모두 ERROR로 잡음. "도구가 실패를 잡는지 확인"([LP14](2026-10-31_LP14_validation_methodology.html) negative control 원칙)을 여기에도 적용.
- 검증 범위: macOS `python3 -I`(3.9.6)로 실행, 위 출력은 실제 실행 결과. 실제 보드 측정은 없음 (`--measured` 값은 예시).

### 6.2 연습 레시피 `[미실행]`

1. **CM4IO 회로도 직접 읽기**: [cm4io-datasheet.pdf](https://datasheets.raspberrypi.com/cm4io/cm4io-datasheet.pdf) 4장(Circuit diagram)을 열어 §3.2 전원 트리를 손으로 다시 그리고, USB 허브 VBUS 전류 제한 스위치와 HDMI 5V 스위치를 CSV 행으로 추가 → 스크립트 재실행.
2. **ZCU102로 옮기기**: UG1182의 전원 표를 CSV로 옮겨 rail 수·TP 유무를 비교 (PMBus 텔레메트리 있는 보드 vs 없는 보드 — DFD 관점 비교).
3. **I2C 풀업 계산기**: §2.2(c) 두 식을 함수로 만들어 VDD 1.8/3.3 V × 100/400 kHz × Cb 50–400 pF 표 출력 (면접 전 숫자 감각용).

## 7. 자주 틀리는 것

1. **net label을 "그냥 이름"으로 봄** → 같은 이름이면 선이 없어도 연결. 오타 하나(`VDD_1V8` vs `VDD1V8`)가 단선이다. 반대로 페이지 간 신호는 off-page 표시를 따라가야 한다.
2. **DNP/nf 표시를 놓침** → 회로도엔 부품이 그려져 있어도 실장 안 됨. 그 자리가 VTref나 strap이면 "회로도상 맞는데 안 된다"가 된다. 항상 BOM의 실장 열과 실물로 확인.
3. **회로도만 믿고 BOM·rework를 안 봄** → 같은 PCB rev라도 variant·대체 부품·rework로 다른 보드. 보드 SN별 기록.
4. **FB 저항을 COMP/보상 저항과 혼동** → 핀 이름(FB vs COMP)을 따라가야 한다 (CM4IO에서도 같은 시트에 20 k/12 k 보상 저항이 있음).
5. **active-low 혼동** → `nRESET`이 Low = 리셋 중. "리셋 핀이 High니까 리셋 상태"라고 말하면 바로 지적당한다. `_N`이 차동쌍 N인지 active-low인지도 구분.
6. **풀업이 꺼진 rail에 붙은 걸 못 봄** → I2C·리셋·PGOOD 풀업 rail이 늦게 켜지거나 꺼져 있으면 버스·신호가 죽는다. 풀업의 "전원"까지 본다.
7. **IO 전압 확인 없이 디버거/UART 연결** → 1.8 V 뱅크에 3.3 V UART 어댑터 → 통신 불가나 손상. VTref·VDDIO 먼저.
8. **"strap 저항 맞으니 boot mode 맞다"** → 리셋 중 다른 장치가 그 핀을 driving하거나, 내부 풀업과 외부 저항이 경쟁, 샘플 시점에 IO rail 미안정 → strap 샘플 레지스터로 실제 값 확인 ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)).
9. **DFD 요청을 레이아웃 확정 후에 함** → 그때는 rework뿐. 회로도 리뷰 단계에서 요청해야 공짜에 가깝다.
10. **0 Ω 링크를 아무 데나 떼고 붙임** → 퓨즈·보호 소자를 우회하거나 전류 정격을 넘기면 안 된다 [9]. 변경은 반드시 기록.

## 8. 출처

1. Raspberry Pi — Compute Module 4 IO Board datasheet (전원 입력·L5·점퍼·R4/R5·RTC/팬 I2C 주소·회로도 7장, PSU/PCIe 시트의 AP64501·FB 저항·TP): https://datasheets.raspberrypi.com/cm4io/cm4io-datasheet.pdf
2. Raspberry Pi — Compute Module 4 datasheet (GPIO_VREF, Global_EN, RUN_PG, nRPI_BOOT, EEPROM_nWP, §5.1 power-up 순서, 전류 값, GPIO2/3 1.8 kΩ 풀업): https://datasheets.raspberrypi.com/cm4/cm4-datasheet.pdf
3. Diodes Inc. — AP64501 datasheet (VFB 0.8 V ±1%, 792–808 mV, EN 임계 1.18 V, R1 = R2 × (Vout/0.8 − 1)): https://www.diodes.com/datasheet/download/AP64501.pdf
4. Cadence PCB Resources — The Definitive Guide to PCB Bring-Up (전원 넣기 전 rail 저항 확인, 육안 검사): https://resources.pcb.cadence.com/blog/the-definitive-guide-to-pcb-bring-up
5. element14 Community — Powering Up and Debugging PCB Prototypes (test point·점퍼를 격리/측정에 쓰기, 단계적 전원 인가): https://community.element14.com/challenges-projects/project14/circuitprototypetechniques/b/blog/posts/powering-up-and-debugging-pcb-prototypes
6. AMD/Xilinx Wiki — Zynq UltraScale+ MPSoC BIST and SCUI Guide (ZCU102 SW6 boot mode, system controller GUI; 검색 요약으로 확인, 본문 표는 미확인): https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/1544716298/Zynq+Ultrascale+MPSOC+BIST+and+SCUI+Guide
7. Texas Instruments SLVA689 — I2C Bus Pullup Resistor Calculation (Rp(min)/Rp(max) 식): https://ti.com/lit/SLVA689
8. Microchip — External Pull-up Resistor Selection (Rp(max) = tr / (0.8473 × Cb)): https://onlinedocs.microchip.com/oxy/GUID-52E50759-273F-4F48-9593-D142ECE4D551-en-US-3/GUID-97BE05E5-5452-4D5C-95A4-F0B9E9815FFE.html
9. EmbeddedRelated (EE Shanty) — What shall we do with a zero-ohm resistor? (0 Ω 링크의 용도·정격): https://www.embeddedrelated.com/blogs-3/mpat/all/Circuit_Design.php
10. NXP UM10204 — I2C-bus specification and user manual (rise time·Cb 한계의 원 규격): https://www.nxp.com/docs/en/user-guide/UM10204.pdf

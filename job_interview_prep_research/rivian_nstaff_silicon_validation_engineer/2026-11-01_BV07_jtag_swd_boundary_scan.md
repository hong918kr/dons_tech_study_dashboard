# BV07 · JTAG · SWD · CoreSight · boundary scan — 칩의 정비용 뒷문
<!-- theme: pastel -->

> **묶음**: C 첫 전원 인가·JTAG·부트 · **우선**: P0 ("JTAG can't read IDCODE. Why?" — Don 강점 카드: Trace32 멀티코어) · **선행 노트**: [BV06 첫 전원 인가 체크리스트](2026-11-01_BV06_first_power_on_checklist.html), [BV05 리셋·strap](2026-11-01_BV05_reset_strap_boot_mode.html) · **다음 노트**: [BV08 부트 흐름·bring-up FW](2026-11-01_BV08_boot_flow_bringup_firmware.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html), [BV13 디버그 시나리오](2026-11-01_BV13_board_debug_scenarios.html), [LP16 bare-metal·Zynq](2026-10-31_LP16_baremetal_vs_linux_zynq.html), [EL M11 HW 연동 디버깅](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/M/2026-10-17_M11_hw_linked_debugging.html) · **코드**: [bv07/tapsim.c](onsitePrep/bv07/tapsim.c) (TAP 16상태 + 4-TAP chain + IDCODE autoprobe + IR 길이 측정 + boundary-scan interconnect test + 고장 주입, macOS ASan/UBSan 빌드·실행 PASS, 출력 [bv07/tapsim.out](onsitePrep/bv07/tapsim.out), [bv07/decode.out](onsitePrep/bv07/decode.out))

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"JTAG can't read IDCODE — why?"**, "IDCODE는 읽히는데 halt가 안 된다", "리셋 직후 첫 명령부터 디버그하려면?", "멀티코어를 동시에 멈추려면?", "boundary scan이 뭐고 보드 팀은 왜 쓰나?", "DDR training 없이 DDR 배선을 어떻게 검사하나?", "디버거가 메모리를 읽는 경로는?", "양산 칩의 JTAG lock".
- **외울 것 ① — JTAG 기본**: 선 4–5개 **TCK·TMS·TDI·TDO(·TRST)** + VTref. TMS는 TCK 상승 에지에 샘플, **TAP = 16상태 머신**, **TMS=1을 5클럭이면 어느 상태에서든 Test-Logic-Reset** (tapsim으로 16/16 검증). 리셋되면 DR = **IDCODE (있으면) 또는 BYPASS**. 모든 장치는 **daisy chain**으로 직렬: IR 길이 합·DR 길이 합.
- **외울 것 ② — IDCODE 32비트**: `[31:28] version · [27:12] part · [11:1] manufacturer (JEP106: [11:8] bank−1, [7:1] ID) · [0] = 1`. 예: **0x4BA00477** = ver 4 · part 0xBA00 · mfr 0x23B (bank 5, 0x3B = **Arm**) → Arm CoreSight JTAG-DP. **all-ones = TDO가 high(열림/전원 없음)**, **all-zeros = TDO가 low(쇼트)**.
- **외울 것 ③ — Arm 경로**: 디버거 → **DP** (JTAG-DP IR 4비트: ABORT 0x8 · DPACC 0xA · APACC 0xB · IDCODE 0xE · BYPASS 0xF) → CTRL/STAT에 **CDBGPWRUPREQ(bit28)·CSYSPWRUPREQ(bit30)** → **AP** (MEM-AP: CSW 0x00 · TAR 0x04 · DRW 0x0C) → 시스템 버스(메모리) 또는 debug APB(코어 debug 레지스터·**CTI**·ETM). 멀티코어 동시 halt = **CTI/CTM**. 리셋 직후 = **connect under reset + vector catch**.
- **Boundary scan (IEEE 1149.1)** = 핀마다 붙은 셀(BSR)로 **칩 기능 없이 핀을 직접 흔들고 읽어서** 보드 배선 open/short 검사. 명령: **BYPASS · SAMPLE/PRELOAD · EXTEST** (필수), IDCODE·HIGHZ·CLAMP 등 (선택). **BSDL** 파일이 칩의 BS 구조를 설명. DDR4/DDR5는 **TEN 핀 connectivity test 모드**, LPDDR5는 TEN 없음 → 컨트롤러 핀으로 저속 write/read [10].
- **30초 영어 답 ("JTAG can't read IDCODE")**: "I work up from the physical layer. First, is the target really powered and is VTref present at the header — without it the probe won't drive the right levels. Second, is the chip out of reset, and is the debugger itself holding reset or TRST? Third, slow TCK right down, to 100 kHz or less, and put a scope on TCK, TMS and TDO at the SoC. If TDO never moves and sits high, the chain is open, the TAP isn't powered, or JTAG is disabled by a strap or fuse; if it sits low, something's shorting it or a level shifter isn't powered. If TDO toggles but the IDCODE is garbage, it's chain configuration — another TAP in the chain, wrong IR lengths, or a JTAG router that has to be enabled — or signal integrity on TCK. An autoprobe at low speed tells me how many TAPs answer and what their IDCODEs are."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "건물의 정비용 뒷문 + 화물 기차"

- **JTAG = 정비용 뒷문.** 건물(칩)이 정문(부팅)으로 손님을 못 받는 상태여도, 관리인(디버거)은 뒷문으로 들어가 "이 건물 이름이 뭐야?"(IDCODE), "엘리베이터 멈춰"(CPU halt), "3층 창고 내용물 보여 줘"(메모리 읽기)를 할 수 있다. 그래서 bring-up의 생명줄이다.
- **선 4개의 역할**:
  - **TCK** (Test Clock) = 박자. 모든 동작은 이 박자에 맞춘다.
  - **TMS** (Test Mode Select) = **방향 지시**. 박자마다 0/1을 보내서 관리 로봇(TAP 상태 머신)을 "명령 받는 방(IR)으로 가라 / 데이터 방(DR)으로 가라 / 처음으로 돌아가라"고 조종한다.
  - **TDI** (Test Data In) = 들어가는 데이터. **TDO** (Test Data Out) = 나오는 데이터.
- **화물 기차 비유 (scan chain)**: 보드 위 JTAG 장치 여러 개는 **한 줄 기차**처럼 연결된다. TDI → 칩A → 칩B → 칩C → TDO. 박자마다 모든 화물칸(시프트 레지스터)의 짐이 한 칸씩 앞으로 밀린다. 그래서 칩C의 데이터가 제일 먼저 TDO로 나온다. 칩 하나만 상대하고 싶으면 나머지 칩은 **BYPASS**(화물칸 1칸짜리로 줄이기)로 둔다.
- **IR vs DR**: IR(Instruction Register) = "무슨 일을 할지" 적는 칸 (예: "IDCODE 보여 줘", "BYPASS 해", "핀 검사 모드"). DR(Data Register) = 실제 데이터가 오가는 칸 — IR에 뭘 썼느냐에 따라 **다른 DR이 연결된다** (IDCODE 32비트, BYPASS 1비트, boundary-scan 수백 비트, Arm DAP 접근 35비트…).
- **Boundary scan = 모든 핀에 검문소.** 칩의 각 핀과 내부 회로 사이에 작은 셀이 있어서, 검사 모드에선 내부 회로를 떼어 놓고 **JTAG으로 핀 값을 직접 정하고(drive) 읽을(capture) 수 있다.** 칩 A의 핀에서 1을 내보내고 칩 B의 핀에서 1이 읽히면 둘 사이 배선이 연결된 것 → **소프트웨어 없이 보드 납땜·배선 검사.**
- **SWD (Serial Wire Debug)** = Arm이 만든 **2선짜리 JTAG 대체품** (SWCLK + SWDIO 양방향). 핀이 적은 MCU에서 흔하고, 같은 Arm DP 뒤의 디버그 기능을 쓴다. 단 SWD는 **boundary scan을 못 한다** (그건 JTAG만).

### 1.2 그림 — 디버거에서 칩 속까지

```text
 PC (Trace32 / OpenOCD / J-Link SW)
   │ USB/Ethernet
 ┌─┴──────────┐   ribbon cable    ┌──────────────── 보드 ─────────────────────────────────────────────┐
 │ debug probe│══════════════════▶│ JTAG header: VTref GND TCK TMS TDI TDO nTRST nSRST                  │
 │ (level =   │                   │   │TDI                                                     ▲TDO     │
 │  VTref)    │                   │   ▼                                                        │         │
 └────────────┘                   │ ┌──────┐   ┌──────┐   ┌──────────────── SoC ──────────────┴──────┐  │
                                  │ │ CPLD │──▶│ FPGA │──▶│ BS TAP (IR 5) ──▶ Arm JTAG-DP (IR 4) ──▶ │  │
                                  │ │ IR 3 │   │ IR 6 │   │   │boundary cells     │DP               │  │
                                  │ └──────┘   └──────┘   │   ▼ on every pin      ▼                   │  │
                                  │                        │  [pins]        AP: MEM-AP ──▶ memory/SRAM │  │
                                  │                        │                 AP: APB-AP ──▶ core debug,│  │
                                  │                        │                                CTI, ETM   │  │
                                  │                        └───────────────────────────────────────────┘  │
                                  └──────────────────────────────────────────────────────────────────────┘
  TCK·TMS는 모든 TAP에 병렬로, TDI→TDO는 직렬(daisy chain). 이 그림 = tapsim.c의 시뮬레이션 체인.
```

### 1.3 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| JTAG | Joint Test Action Group (IEEE 1149.1) | 칩 테스트·디버그용 4–5선 직렬 인터페이스 표준 | bring-up 생명줄, boundary scan |
| TAP | Test Access Port | 칩 안의 JTAG 수신부 (상태 머신 + 레지스터들) | 체인의 "칸" 하나 = TAP 하나 |
| TAP controller | — | TMS로 움직이는 16상태 머신 | 모든 JTAG 동작의 순서 |
| TCK / TMS / TDI / TDO | Test Clock / Mode Select / Data In / Data Out | 박자 / 방향 지시 / 입력 / 출력 | 디버그 시 scope로 찍을 4선 |
| TRST (nTRST) | Test Reset | TAP만 리셋하는 선택적 핀 (active-low) | 풀다운된 채 방치되면 TAP이 영원히 리셋 → 응답 없음 |
| SRST (nSRST) | System Reset | 칩 전체 리셋 (JTAG 표준 밖이지만 헤더에 같이 있음) | connect under reset에 사용 |
| VTref | Voltage Target reference | 디버거가 타깃 IO 전압을 감지하는 핀 | 없으면 디버거 출력이 꺼지거나 레벨이 틀림 |
| RTCK | Return TCK | 타깃이 TCK를 되돌려 보내는 선 (adaptive clocking) | 코어 클럭이 느릴 때 TCK를 맞춤 |
| IR | Instruction Register | "어떤 DR을 연결할지" 고르는 레지스터 | 장치마다 길이 다름 (3–10비트 등) |
| DR | Data Register | IR이 고른 데이터 레지스터 (IDCODE, BYPASS, BSR…) | 실제 데이터 통로 |
| BYPASS | — | 1비트짜리 DR, 장치를 "건너뛰게" 함 | 체인에서 한 장치만 상대할 때 |
| IDCODE | identification code | 32비트 칩 신분증 | 칩이 살아 있다는 첫 증거, stepping 확인 |
| JEP106 | JEDEC Publication 106 | 반도체 제조사 ID 표 (bank + 7비트 코드) | IDCODE의 제조사 필드 해석 |
| Scan chain | — | 여러 TAP을 TDI→TDO로 직렬 연결한 것 | 길이·순서를 디버거 설정에 정확히 |
| BSR | Boundary-Scan Register | 핀마다 붙은 셀들을 이은 긴 레지스터 | boundary scan의 본체 |
| BSDL | Boundary-Scan Description Language | 칩의 BS 구조(핀·셀·IR 길이·opcode)를 적은 파일 (VHDL 문법 일부) | boundary-scan 도구의 입력 [12] |
| EXTEST / SAMPLE / PRELOAD | — | 핀을 외부에서 구동·검사 / 핀 값 몰래 찍기 / 출력 값 미리 넣기 | 보드 interconnect test |
| SWD | Serial Wire Debug | Arm의 2선(SWCLK·SWDIO) 디버그 프로토콜 | MCU 표준, boundary scan 불가 |
| SWO | Serial Wire Output | SWD와 함께 쓰는 1선 trace 출력 (ITM printf 등) | 멈추지 않고 로그 |
| CoreSight | — | Arm의 디버그·trace 구성 요소 아키텍처 | Arm SoC 디버그의 공통 구조 |
| ADIv5 / ADIv6 | Arm Debug Interface v5/v6 | DP·AP 규격 | DP/AP 레지스터 위치 |
| DAP | Debug Access Port | DP + AP들의 묶음 | 디버거가 들어가는 입구 |
| DP | Debug Port (JTAG-DP / SW-DP / SWJ-DP) | 외부 선(JTAG/SWD)을 받는 쪽 | 전원 요청·AP 선택 |
| AP / MEM-AP | Access Port / Memory Access Port | DP 뒤에서 실제 버스에 접근하는 포트 (AHB-AP, APB-AP, AXI-AP) | 메모리·레지스터 읽기 쓰기 |
| ROM table | — | AP 뒤에 있는 "이 칩에 어떤 디버그 부품이 어디 있나" 목차 | 디버거 자동 탐색 |
| CTI / CTM | Cross Trigger Interface / Matrix | 코어 간 halt·restart 신호를 이어 주는 배선 | 멀티코어 동시 halt [9] |
| ETM / ITM / STM | Embedded / Instrumentation / System Trace Macrocell | 명령 실행 기록 / SW printf식 trace / 시스템 이벤트 trace | 멈추지 않고 실행 흐름 기록 |
| ETB / ETF / ETR / TPIU | Embedded Trace Buffer/FIFO/Router, Trace Port Interface Unit | trace를 칩 안 버퍼 / 메모리 / 외부 핀으로 | Trace32의 강점 영역 |
| HW breakpoint | — | 코어 안 비교기로 거는 중단점 (개수 제한) | ROM·flash에선 이것만 가능 |
| Vector catch | — | 리셋·예외 벡터에 진입할 때 자동 halt | 리셋 직후 첫 명령 디버그 |
| Semihosting | — | 타깃 코드가 디버거를 통해 호스트 파일·콘솔 사용 | UART 없을 때 printf |
| Secure debug / JTAG lock | — | fuse·인증으로 디버그 접근을 막는 기능 | 양산 칩은 잠겨 있다 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 신호와 헤더

| 신호 | 방향 (디버거 기준) | 의미 | 보드에서 흔한 처리 / 함정 |
|---|---|---|---|
| TCK | 출력 | 테스트 클럭. TMS·TDI는 **상승 에지**에서 샘플, TDO는 **하강 에지**에서 바뀜 [7] | 긴 케이블·여러 TAP → 링잉. 직렬 저항, 낮은 속도부터 |
| TMS | 출력 | 상태 머신 조종 | 많은 장치가 내부 풀업 → 끊기면 1로 떠서 TLR에 머묾 [7] |
| TDI | 출력 | 체인 첫 장치로 들어가는 데이터 | 풀업 흔함 |
| TDO | 입력 | 체인 마지막 장치에서 나오는 데이터, Shift 상태 밖에선 high-Z | high-Z + 풀업 → 응답 없으면 **all ones** |
| nTRST | 출력 (선택) | TAP 비동기 리셋 | 풀다운인데 디버거가 안 구동하면 TAP이 계속 리셋 |
| nSRST | 양방향 | 시스템 리셋 | 디버거가 잡고 있으면 칩이 안 깨어남 (OpenOCD `reset_config`) [6] |
| VTref | 입력 | 타깃 IO 전압 감지 | 측정 필수: 1.8 V 칩에 3.3 V로 구동하면 손상 |
| RTCK | 입력 (선택) | adaptive clocking | 코어 클럭 의존 TAP에서 필요 |
| SWCLK / SWDIO | 출력 / 양방향 | SWD (TCK·TMS 핀을 공유: SWJ-DP) | JTAG↔SWD 전환 시퀀스 필요 |
| SWO | 입력 | SWD trace 출력 (TDO 핀 공유) | baud = trace 클럭 설정 |

- 커넥터: Arm 20핀(2.54 mm), Cortex Debug 10핀(1.27 mm), trace용 고밀도 커넥터(Mictor 등) — 이름과 핀 배치는 보드 회로도·디버거 문서로 확인. **핀 1 방향 뒤집힘**은 첫날 흔한 실수.

### 2.2 TAP 상태 머신 — 16상태

```text
                 ┌────────────────────────── TMS=1 ─────────────────────────────┐
                 ▼                                                               │
       ┌───────────────────┐                                                     │
  ┌──▶ │  Test-Logic-Reset │ ──TMS=0──▶ ┌──────────────┐                         │
  │1   └───────────────────┘            │ Run-Test/Idle│◀─0 (loop)               │
  └────────────┘ (TMS=1 loop)           └──────┬───────┘                         │
                                               │1                                │
          ┌──────────── DR 열 ─────────────────▼───┐       ┌──────── IR 열 ──────┴──────┐
          │  Select-DR-Scan ──────1──────────────────────▶ │  Select-IR-Scan ──1──▶ TLR │
          │        │0                               │       │        │0                  │
          │  Capture-DR  (DR에 값 적재: IDCODE 등)   │       │  Capture-IR (IR LSB = ..01)│
          │    │0      │1                           │       │    │0      │1              │
          │  Shift-DR ◀┘(0 loop: 한 클럭 1비트 이동) │       │  Shift-IR (0 loop)         │
          │    │1                                   │       │    │1                      │
          │  Exit1-DR ──0──▶ Pause-DR (0 loop)      │       │  Exit1-IR ──0──▶ Pause-IR  │
          │    │1              │1                   │       │    │1             │1       │
          │    │           Exit2-DR ──0──▶ Shift-DR │       │    │         Exit2-IR ─0─▶ Shift-IR
          │    ▼               │1                   │       │    ▼              │1       │
          │  Update-DR ◀───────┘ (DR 값 확정 적용)    │       │  Update-IR ◀──────┘ (새 명령 적용)
          │    │0 → Run-Test/Idle   │1 → Select-DR   │       │    │0 → RTI   │1 → Select-DR
          └─────────────────────────────────────────┘       └────────────────────────────┘
```

- **세 단계 패턴**: Capture(병렬로 값 적재) → Shift(직렬로 밀어냄, 동시에 TDI 값이 들어감) → Update(시프트 레지스터 값을 실제 레지스터에 확정). "Shift 중에는 출력이 흔들리지 않는다" — Update에서만 바뀐다.
- **Pause** = 디버거가 데이터를 준비하는 동안 쉬는 곳. **Exit1/Exit2** = 갈림길.
- **TMS=1 × 5 → Test-Logic-Reset** (어느 상태에서든). 이유: 상태 머신이 그렇게 설계됨 — tapsim에서 16상태 각각 검증, 최악 5클럭 (Capture/Shift/Pause 상태들) [7].
- Test-Logic-Reset 진입 시 IR = **IDCODE** (IDCODE가 있는 장치) 또는 **BYPASS** → 그래서 리셋 직후 DR을 그냥 읽으면 IDCODE가 나온다 (autoprobe의 원리) [5].
- Capture-IR 값의 **최하위 2비트는 반드시 `01`** — 디버거가 "IR 시프트가 제대로 되나"를 확인하는 데 쓴다 (OpenOCD `-ircapture` 기본 0x01, `-irmask` 0x03) [5].

### 2.3 IR · DR · 명령

| 명령 | 필수? | 연결되는 DR | 하는 일 |
|---|---|---|---|
| BYPASS | 필수 (opcode = **all ones**) | 1비트 (Capture 때 0 적재) | 이 장치를 건너뜀 |
| SAMPLE/PRELOAD | 필수 | BSR | SAMPLE: 칩이 정상 동작하는 중에 핀 값을 찍음 / PRELOAD: EXTEST 전에 출력 값을 미리 넣어 둠 [8] |
| EXTEST | 필수 | BSR | 핀을 BSR 값으로 구동·입력 핀 값을 캡처 → 보드 배선 검사 [8] |
| IDCODE | 선택 (있으면 리셋 기본) | 32비트 IDCODE | 신분증 |
| USERCODE | 선택 | 32비트 | 사용자 정의 (FPGA 비트스트림 버전 등) |
| INTEST | 선택 | BSR | 칩 내부 로직을 핀 셀로 검사 |
| HIGHZ / CLAMP | 선택 | BYPASS | 출력을 high-Z로 / BSR 값으로 고정하고 BYPASS |
| (벤더 전용) | — | 예: Arm DPACC/APACC 35비트, FPGA 설정 등 | 디버그·프로그래밍 |

- 정의 안 된 opcode는 보통 **BYPASS처럼 동작** (예: TI MSP432E4의 JTAG 명령 표: 0x0 EXTEST, 0x2 SAMPLE/PRELOAD, 0x8 ABORT, 0xA DPACC, 0xB APACC, 0xE IDCODE, 0xF BYPASS, 나머지는 BYPASS) [11].

### 2.4 IDCODE 해석

```text
 31      28 27                        12 11                    1   0
 ┌─────────┬────────────────────────────┬───────────────────────┬───┐
 │ version │        part number         │   manufacturer ID     │ 1 │
 │  4 bit  │          16 bit            │ 11 bit = JEP106       │   │
 └─────────┴────────────────────────────┴───────────────────────┴───┘
                                          [11:8] continuation 코드 개수 (= bank − 1)
                                          [7:1]  7비트 제조사 코드 (JEP106의 parity 비트는 뺌)

 0x4BA00477 → 0100 | 1011 1010 0000 0000 | 0100 0111 011 | 1
              ver 4 | part 0xBA00          | mfr 0x23B     | bit0 = 1
              mfr 0x23B → bank 5 (continuation 4개), code 0x3B → Arm Ltd
```

- 근거: IDCODE 필드 위치 [2][3], JEP106 인코딩(7데이터 비트 + parity, continuation 코드로 bank 지정) [4], Arm DP IDCODE 실례 0x4BA00477 [13]·OpenOCD 예시 `-expected-id 0x3ba00477` [5].
- **bit0 = 1인 이유**: 리셋 후 DR을 읽을 때 BYPASS 장치는 0을 내놓으므로, 첫 비트로 "IDCODE(32비트)냐 BYPASS(1비트)냐"를 구분한다 [2]. → 디버거가 체인을 **자동 탐색(autoprobe)** 할 수 있는 이유.
- **version 필드** = 같은 part의 리비전 → **실리콘 stepping (A0/A1/B0) 확인**에 쓴다. 디버거 설정에서 version을 무시하는 옵션(OpenOCD `-ignore-version`)이 있는 이유 [5].
- **IR 길이는 JTAG으로 확실히 읽을 방법이 없다** → BSDL·데이터시트에서 가져온다 (OpenOCD 문서) [5]. 총 IR 길이는 측정 가능 (§6 tapsim D: 1 다음 0 하나를 흘려서 몇 클럭 뒤 나오는지).

### 2.5 Scan chain 실무

- **DR 길이** = 선택된 DR들의 합 (예: 4개 중 1개만 IDCODE, 나머지 BYPASS → 32 + 3 = 35비트). 디버거는 원하는 장치 앞뒤의 BYPASS 비트 수만큼 **패딩**해서 시프트한다.
- **선언 순서**: OpenOCD는 **TDO에 가까운 장치부터** 선언하고, 순서가 하드웨어와 일치해야 한다 [5][14]. 예: STM32는 TAP이 2개(Cortex-M DAP + boundary-scan TAP) [14].
- **SoC 안에도 TAP이 여러 개**일 수 있다: Arm DAP + 칩 boundary-scan TAP + (보안/테스트 TAP). 일부 SoC는 **JTAG router**(예: TI의 ICEPick 류)가 있어서, 처음엔 router만 보이고 명령으로 다른 TAP을 체인에 붙여야 한다 `[추정: 칩마다 다름 — TRM 확인]`.
- 보드에는 **체인 선택 스위치/점퍼**(SoC만 / FPGA만 / 전부)가 흔하다 — 첫날 체인 문제의 단골 원인 `[추정]`.

### 2.6 Arm CoreSight — DP, AP, 그리고 메모리 읽기 경로

#### DP (Debug Port)

| 항목 | 내용 |
|---|---|
| 종류 | **JTAG-DP** (JTAG 전용), **SW-DP** (SWD 전용), **SWJ-DP** (둘 다, 핀 공유) |
| JTAG-DP IR | 4비트(또는 8비트 구성): ABORT 0x8 · DPACC 0xA · APACC 0xB · IDCODE 0xE · BYPASS 0xF [11][15] |
| DPACC/APACC DR | **35비트** = bit 0 RnW · bits 2:1 A[3:2] (레지스터 주소) · [34:3] 32비트 데이터. Capture 때 [2:0]에 **ACK**: OK/FAULT = 0b010, WAIT = 0b001 (ADIv5) [16] |
| CTRL/STAT | **CDBGPWRUPREQ bit28 / ACK bit29, CSYSPWRUPREQ bit30 / ACK bit31** — 디버그·시스템 전원 도메인 켜 달라는 요청과 응답 [17] |
| SELECT | 어느 AP, AP의 어느 레지스터 bank를 쓸지 |
| RDBUFF | AP 읽기는 **posted**: 결과는 다음 접근 때 나오고, 마지막 결과는 RDBUFF로 회수 |
| JTAG→SWD 전환 | SWDIO=1로 50+클럭 → 16비트 **0xE79E (LSB first)** → 다시 50+클럭 1 → DP IDCODE(DPIDR) 읽기 [18] |

#### AP (Access Port) — MEM-AP

| 레지스터 | 오프셋 (ADIv5) | 의미 |
|---|---|---|
| CSW | 0x00 | 접근 크기(8/16/32), 주소 자동 증가, 보안 속성 등 |
| TAR | 0x04 | 접근할 버스 주소 |
| DRW | 0x0C | 데이터 읽기/쓰기 → TAR 주소에 실제 버스 트랜잭션 |
| (ADIv6) | +0xD00 | ADIv6에선 같은 레지스터가 0xD00 영역으로 이동 [17] |

- AP 종류: **AHB-AP / AXI-AP** (시스템 버스 → DDR·SRAM·주변장치), **APB-AP** (debug APB → 코어 debug 레지스터·CTI·ETM·ROM table).

#### 디버거가 "0x10000000 읽기"를 하는 실제 순서

```text
 1. (JTAG) IR ← DPACC ; DR: write CTRL/STAT = CDBGPWRUPREQ | CSYSPWRUPREQ
 2. poll CTRL/STAT until CDBGPWRUPACK & CSYSPWRUPACK = 1      ← 여기서 멈추면: debug 전원 도메인 문제
 3. write SELECT = (AP 번호, bank 0)
 4. (JTAG) IR ← APACC ; write CSW = 32-bit, no auto-increment
 5. write TAR = 0x10000000
 6. read DRW                    → ACK=WAIT 이면 재시도 (버스가 바쁨/멈춤)
 7. (JTAG) IR ← DPACC ; read RDBUFF → 데이터 (posted read 회수)
    ACK=FAULT 또는 CTRL/STAT의 sticky error → 버스 에러 (주소 없음·전원 꺼진 블록·보안 거부)
```

#### 코어 debug (halt)

- **Cortex-M**: DHCSR @ 0xE000EDF0 — 쓰기 시 상위 16비트에 키 **0xA05F**, bit0 C_DEBUGEN, bit1 C_HALT. DEMCR bit0 **VC_CORERESET** = 리셋 벡터에서 halt (vector catch) [19]. 이 레지스터들은 AHB-AP로 메모리처럼 접근.
- **Cortex-A/R (Armv7/v8)**: APB-AP 뒤의 **external debug 레지스터**(코어별 debug base 주소, ROM table로 찾음)로 halt 요청. 멀티코어에서는 보통 **CTI를 통해 halt 이벤트를 보내는** 구조 `[추정: 코어별 TRM 확인]`.
- **CTI/CTM**: 각 코어 옆 CTI가 trigger(예: "코어 halt됨")를 CTM 채널(보통 4채널)로 내보내고, 다른 CTI들이 그 채널을 받아 자기 코어를 halt → **한 코어가 breakpoint에 걸리면 나머지도 거의 동시에 멈춤** [9]. 디버거가 자동으로 설정하지 않으면 직접 CTI 레지스터를 설정해야 할 수도 있다 (SEGGER 포럼 사례) [9].

### 2.7 디버거 기능 — 면접 단골

| 기능 | 무엇 | 주의점 |
|---|---|---|
| Halt / step / resume | 코어 멈추기·한 명령씩 | watchdog이 halt 중에도 돌면 리셋됨 → halt 시 WDT freeze 설정 |
| HW breakpoint | 코어 비교기 사용 → **ROM·flash에서도 가능** | 개수 제한: Cortex-A53 **6개 bp · 4개 wp** [20], Cortex-M4 FPB 명령 비교기 6개(+literal), DWT 4개 [20] |
| SW breakpoint | 해당 주소 명령을 BKPT로 바꿔 씀 | **RAM에서만** (ROM 불가, flash는 디버거가 지우고 다시 써야) |
| Watchpoint | 데이터 주소 읽기/쓰기 시 halt | "누가 이 변수를 망가뜨리나" |
| Memory/register R/W | MEM-AP 경로 | 캐시: 코어 캐시에 있는 값은 버스 접근으로 안 보일 수 있음 |
| **Connect under reset** | 리셋을 잡은 채 접속 → 리셋 해제 순간 바로 halt | 부팅 직후 JTAG 핀을 GPIO로 바꾸거나 저전력에 들어가는 코드 대응. OpenOCD `connect_assert_srst` (srst_nogate 필요) [6] |
| **Vector catch** | 리셋·HardFault 등 벡터 진입 시 halt | **리셋 후 첫 명령 디버그** (Cortex-M VC_CORERESET) [19] |
| Semihosting | 타깃이 특수 명령(Arm-M: `BKPT 0xAB`)으로 디버거에 I/O 요청 | 디버거 없이 실행하면 **멈추거나 HardFault** → 양산 FW에서 제거 |
| Flash programming | 디버거가 flash 알고리즘을 SRAM에 올려 실행 | SRAM·클럭이 살아 있어야 함 |
| Multicore / SMP | 여러 코어 동시 attach, CTI 동기 halt | 보조 코어는 부트 코어가 리셋을 풀어 줄 때까지 접근 불가일 수 있음 |
| Trace | ETM → ETB/ETR(칩 안) 또는 TPIU(외부 핀) | 멈추지 않고 "크래시 직전 무엇을 실행했나" |

### 2.8 Trace32 vs J-Link vs OpenOCD (vs 벤더 도구)

| 도구 | 강점 | 약점 | 언제 |
|---|---|---|---|
| Lauterbach **Trace32** | 거의 모든 코어·멀티코어/이기종(AMP), 강력한 trace(ETM, 대용량), PRACTICE 스크립트(`.cmm`), 실리콘 bring-up 표준 | 비쌈, 라이선스 | **새 SoC bring-up, 멀티코어 디버그, trace** — 실리콘 팀 표준 |
| SEGGER **J-Link** | 빠름, flash 프로그래밍 우수, 쉬움, 스크립트(JLinkScript) | 고급 trace·특이 SoC 지원은 제한적 | MCU, 빠른 다운로드, CI 연결 |
| **OpenOCD** (+GDB) | 오픈소스, Tcl 스크립트, 커스텀 칩에 설정 작성 가능, 자동화 쉬움 | 새 칩 지원은 직접 작성, 속도·안정성 편차 | 보드 팜 자동화, 비용 0, 커스텀 TAP |
| Arm DSTREAM / Development Studio | Arm 공식, CoreSight 지원 | 비쌈 | Arm 코어 중심 |
| AMD/Xilinx hw_server · XSDB · Vivado HW Manager | Zynq/FPGA JTAG (PL 설정 + PS 디버그) | AMD 장치 전용 | **Zynq 기반 검증 플랫폼** (JD의 Zynq/Petalinux) |

### 2.9 Boundary scan (IEEE 1149.1) — board validation의 무기

```text
           칩 내부 로직 (core)                     칩 내부 로직 (core)
                │  ▲                                     ▲
   ┌────────────▼──┴───────────┐           ┌─────────────┴─────────────┐
   │ BS cell: capture·shift·    │  PCB 배선 │ BS cell                    │
   │ update latch, mode mux     │══════════▶│ (입력 핀: capture)          │
   │ → 핀을 core 대신 update     │   net N   │                            │
   │   latch 값으로 구동 (EXTEST)│           │                            │
   └────────────┬──────────────┘           └─────────────┬──────────────┘
          TDI ──┘ (셀들이 한 줄로 이어져 BSR이 됨) ──▶ ... ──▶ TDO
   EXTEST 1회 = Shift(패턴 넣기) → Update(핀 구동) → Capture(받는 핀 읽기) → Shift(결과 빼기)
```

- **보드 interconnect test**: 구동 칩의 출력 셀로 **walking 1 / walking 0** 패턴을 내고 받는 칩의 입력 셀에서 읽는다. 결과로 **open**(항상 1 또는 0 — 풀업/풀다운 방향), **short**(이웃 net을 따라감, wired-AND/OR), **stuck-at**을 net 단위로 찾는다 (§6 tapsim F가 정확히 이것) [8].
- **PRELOAD 먼저**: EXTEST로 들어가는 순간 출력 핀이 쓰레기 값을 내보내지 않게 안전한 값을 미리 넣는다 (XJTAG도 EXTEST 전에 자동으로 PRELOAD) [8].
- **BSDL**: 핀 이름·BSR 셀 번호·셀 종류(입력/출력/제어/양방향)·IR 길이·opcode·IDCODE·"BS 모드로 들어가려면 이 핀을 이렇게(compliance enable)"를 기술 [12]. 도구(XJTAG, JTAG Technologies, Corelis, ASSET 등)가 BSDL + 넷리스트로 테스트를 자동 생성.
- **비-JTAG 장치 검사 (cluster test)**: DRAM·flash처럼 BS가 없는 장치는 **연결된 SoC/FPGA의 BS 핀으로 그 장치를 저속 구동**해서 검사 — 예: 메모리에 write 후 read [10].
- **"DDR training 없이 DDR 배선 검사"** (BV00 체인):
  - DDR4/DDR5 (soldered-down): DRAM의 **TEN 핀**으로 **Connectivity Test(CT) 모드** 진입 → DRAM 입력 핀들이 XOR 논리로 출력 핀에 연결되어, 셀 쓰기·읽기 없이 빠르게 open/short를 net 단위로 찾음 [10][21]. DDR5 DIMM은 커넥터에 TEN이 없어 write/read 방식 [10].
  - **LPDDR5는 TEN 핀이 없음** → DDR3 이하처럼 BS로 **저속 write/read 패턴** (느림: 한 word 쓰는 데 여러 번의 전체 scan이 필요) [10][21]. 자동차 AI SoC는 LPDDR이 흔하므로 이 경로 `[추정: Rivian SoC 메모리 종류]`.
  - 한계: SoC의 DDR PHY 핀에 BS 셀이 없거나 제한적일 수 있음 → 그땐 컨트롤러/PHY의 **loopback·BIST 모드**나, 기본 초기화 후 **모드 레지스터 읽기(LPDDR MRR로 제조사 ID 등)** 로 "최소 연결"을 확인 `[추정: 칩·PHY마다 다름]`. 저속 BS 검사는 고속 타이밍 문제(SI)는 못 잡는다 → training·eye margin은 [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)·[BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html).
- **고속 차동선(AC 결합: PCIe·SerDes)** 은 1149.1로 못 본다 → **IEEE 1149.6** (AC-coupled boundary scan) 이 따로 있다 `[추정: 지원 여부는 칩마다]`.
- 보드 팀이 쓰는 이유: **펌웨어 없이**, 칩 기능이 살기 전에, X-ray로도 애매한 **BGA 밑 납땜**을 net 단위로 검사 → 첫 프로토 진단 + 양산 테스트 + JTAG로 flash 프로그래밍.

### 2.10 Secure debug · JTAG lock

- Arm 디버그 인증 신호 4개: **DBGEN** (non-secure invasive: halt·breakpoint), **NIDEN** (non-secure non-invasive: trace), **SPIDEN** (secure invasive), **SPNIDEN** (secure non-invasive). 현재 상태는 **DBGAUTHSTATUS** 레지스터로 보인다. 이 신호를 **어떻게 켜고 끄는지는 SoC 벤더 구현** (fuse, 보안 블록) [22].
- 양산 칩: fuse로 디버그를 막거나(완전 lock), **authenticated debug**(디버거가 인증서/challenge-response로 증명하면 열어 줌 — 예: NXP의 debug authentication) [22]. RMA(고장 반품 분석)용 unlock 경로.
- bring-up·validation 영향: A0 샘플은 보통 **open** 상태 `[추정]`, 하지만 **"IDCODE는 읽히는데 halt·메모리 접근이 안 됨"** 의 원인이 될 수 있다 (DP는 응답하지만 AP/코어 접근 거부) `[추정: 구현마다]`. Validation 팀은 **lock이 실제로 막는지 negative test** (lock 후 halt 시도 → 거부, trace 거부, 메모리 읽기 거부) 도 계획에 넣는다.
- 자동차: 사이버보안 요구(ISO/SAE 21434)로 양산 차량 ECU의 디버그 포트 보호가 사실상 필수 `[추정]` ([BV15](2026-11-01_BV15_automotive_board_considerations.html)).

## 3. 보드에서 볼 것 · 잴 것

### 3.1 JTAG 첫 연결 체크리스트

| 항목 | 어떻게 | 통과 기준 | 실패 시 |
|---|---|---|---|
| 헤더 핀 배치·핀 1 | 회로도 vs 디버거 문서, 케이블 방향 | 일치 | 어댑터/케이블 교체 |
| VTref | DMM: 헤더 VTref–GND | 칩 IO 뱅크 전압 (예: 1.8 V) | IO rail·VTref 저항 확인 |
| 풀업/풀다운 | 회로도 + 전원 OFF 저항 측정 | TMS·TDI 풀업, nTRST 처리 확인 `[추정: 흔한 권장]` | nTRST가 풀다운인데 미연결 → 디버거로 구동하거나 rework |
| 리셋 상태 | scope: nSRST, SoC reset 핀 | 리셋 해제됨 (또는 의도한 connect-under-reset) | supervisor·디버거 reset 설정 |
| TCK 속도 | 디버거 설정 | **100 kHz–1 MHz로 시작** | 낮추고 RTCK 고려 |
| 파형 | scope: TCK·TMS·TDO (SoC 쪽 가까이) | TCK 에지 깨끗, TDO가 Shift 중 토글 | 링잉 → 직렬 저항/짧은 케이블, TDO 고정 → §3.3 tree |
| Autoprobe | 설정 없이 체인 스캔 | TAP 개수·IDCODE·총 IR 길이가 회로도와 일치 | chain 스위치·router·IR 길이 |
| DAP 전원 | CTRL/STAT ACK | CDBGPWRUPACK·CSYSPWRUPACK = 1 | debug 전원 도메인, §3.4 tree |

### 3.2 예시 로그와 해석

**성공: OpenOCD autoprobe** `[예시 출력]` (설정 없이 인터페이스만, 실제 문구는 버전마다 다름)

```text
Info : clock speed 100 kHz
Warn : There are no enabled taps.  AUTO PROBING MIGHT NOT WORK!!
Info : JTAG tap: auto0.tap tap/device found: 0x4ba00477 (mfg: 0x23b (ARM Ltd), part: 0xba00, ver: 0x4)
Info : JTAG tap: auto1.tap tap/device found: 0x2a5c1a2b (mfg: 0x515 (<unknown>), part: 0xa5c1, ver: 0x2)
Info : JTAG tap: auto2.tap tap/device found: 0x1beef093 (mfg: 0x049 (Xilinx), part: 0xbeef, ver: 0x1)
Warn : AUTO auto0.tap - use "jtag newtap auto0 tap -irlen 4 -expected-id 0x4ba00477"
Warn : AUTO auto1.tap - use "jtag newtap auto1 tap -irlen 2 -expected-id 0x2a5c1a2b"
...
```

- 1줄: 낮은 TCK. 2줄: 설정이 없어서 autoprobe 모드 [5]. 3–5줄: TDO에 가까운 순서로 TAP 3개가 IDCODE를 냄 (4번째 CPLD는 IDCODE가 없어서 BYPASS 1비트로만 보임). 6–7줄: OpenOCD가 제안하는 설정 줄 — **IR 길이 제안(`-irlen 2` 등)은 추측일 수 있으니 BSDL로 확인** [5]. (IDCODE 값은 §6 tapsim 체인과 같은 가상 값.)

**실패: TDO가 high에 고정** `[예시 출력]`

```text
Error: JTAG scan chain interrogation failed: all ones
Error: Check JTAG interface, timings, target power, etc.
```

- 해석: TDO가 계속 1 = TDO가 끊겼거나(풀업만 보임), TAP이 전원/리셋 상태라 high-Z, JTAG이 strap/fuse로 비활성. tapsim G1과 같은 현상 ("no TAP answered at all"). **all zeros** 면 반대로 TDO가 GND 쪽으로 쇼트되었거나 레벨 시프터 전원이 없는 쪽 (tapsim G2).

### 3.3 Debug tree ① — "IDCODE를 못 읽는다"

```text
 IDCODE 실패
  │
  ├─① 타깃 전원·VTref? ── 없음 → IO rail / VTref 배선 (BV03, BV06 S4)
  │
  ├─② 리셋 상태? ── SoC가 리셋에 잡혀 있음 / 디버거가 nSRST·nTRST를 잡고 있음
  │                  → reset_config 확인, nTRST 풀다운 방치 여부, supervisor (BV05)
  │
  ├─③ TCK 낮추기 (100 kHz → 10 kHz), RTCK 필요?
  │
  ├─④ scope로 SoC 근처 TCK·TMS·TDO
  │     ├─ TCK/TMS가 SoC까지 안 옴     → 케이블·헤더·핀1·레벨 시프터 방향/전원
  │     ├─ TDO 항상 1 (all ones)       → TDO 단선/DNP 저항, TAP 미전원, JTAG 비활성(strap·fuse·pinmux)
  │     ├─ TDO 항상 0 (all zeros)      → TDO 쇼트, 레벨 시프터 전원 없음
  │     └─ TDO 토글하지만 값이 이상    → ⑤
  │
  ├─⑤ 체인 구성
  │     ├─ autoprobe: TAP 개수·IDCODE가 회로도와 같은가?
  │     ├─ 총 IR 길이 측정 vs 선언 합 (tapsim D) → -irlen 수정
  │     ├─ 체인 선택 스위치/점퍼, FPGA·CPLD가 체인에 끼어 있음
  │     └─ JTAG router가 있어 다른 TAP을 먼저 enable 해야 함
  │
  ├─⑥ 신호 품질: 링잉·느린 에지 → 직렬 저항, 짧은 케이블, 속도 더 낮춤
  │
  └─⑦ 모드: JTAG vs SWD 선택(SWJ-DP), boot strap이 JTAG을 끄는 모드, secure lock
       → 다른 칩의 boundary scan으로 JTAG net 연결 확인, 전원 OFF 상태 DMM 연속성
```

### 3.4 Debug tree ② — "IDCODE는 OK, halt가 안 된다"

```text
 IDCODE OK (TAP·배선 살아 있음) → 문제는 DP 뒤쪽
  │
  ├─① CTRL/STAT 전원 요청 ACK가 안 옴 → debug/system 전원 도메인 off, 칩 저전력 상태
  │      (일부 칩 errata: CDBGPWRUPREQ만으론 ACK 안 됨 — 두 비트 같이 써야 [17])
  │
  ├─② AP 접근이 WAIT 무한 / FAULT / sticky error
  │      → AP 뒤 버스가 멈춤(hang), 전원 꺼진 블록, 잘못된 AP 번호, 보안 거부
  │      → ROM table로 구성 탐색, 다른 AP(APB-AP vs AHB-AP)로 시도
  │
  ├─③ 코어가 접근 불가
  │      ├─ 코어 전원 도메인 off / 코어 리셋 유지 (보조 코어는 부트 코어가 풀어 줄 때까지)
  │      ├─ 코어 클럭 없음 (PLL 미설정, clock gate) → BV04
  │      └─ 깊은 저전력 상태(WFI + 클럭 정지, power-down) → 디버그 레지스터 접근 제한
  │
  ├─④ 디버그 권한: DBGEN/SPIDEN 꺼짐 (fuse·보안) → DBGAUTHSTATUS 확인 [22]
  │
  ├─⑤ halt 직후 리셋됨: watchdog → WDT freeze-on-halt 설정, 또는 connect under reset
  │
  ├─⑥ 부팅 코드가 JTAG 핀을 GPIO로 바꾸거나 디버그를 끔 → connect under reset + vector catch
  │
  └─⑦ 디버거 설정 오류: 코어 타입, debug base 주소, CTI 주소, AP 번호 (ROM table과 비교)
```

### 3.5 OpenOCD 설정 예시 `[미실행]`

```bash
# soc_a0.cfg — 가상의 SoC + 보드 체인. [미실행] 주소·IDCODE·IR 길이는 tapsim 체인과 같은 가상 값.
adapter driver jlink                 ;# 또는 ftdi / cmsis-dap
transport select jtag
adapter speed 100                    ;# kHz. 첫 접속은 낮게, 안정되면 올림
reset_config srst_only srst_nogate connect_assert_srst   ;# 칩·보드가 지원할 때만 [6]
adapter srst delay 100               ;# 리셋 해제 후 JTAG 재개까지 대기 (ms)

# TDO에 가까운 TAP부터 선언 (OpenOCD FAQ) [14]
jtag newtap soc  cpu  -irlen 4 -expected-id 0x4ba00477    ;# Arm JTAG-DP
jtag newtap soc  bs   -irlen 5 -expected-id 0x2a5c1a2b    ;# SoC boundary-scan TAP (가상)
jtag newtap fpga tap  -irlen 6 -expected-id 0x1beef093    ;# FPGA (가상 part)
jtag newtap cpld tap  -irlen 3                            ;# IDCODE 없음

dap create soc.dap -chain-position soc.cpu
# Cortex-M 예:      target create soc.m0 cortex_m -dap soc.dap
# Armv8-A 예(가상 주소): cti create soc.cti0 -dap soc.dap -ap-num 1 -baseaddr 0x80420000
#                    target create soc.a0 aarch64 -dap soc.dap -dbgbase 0x80410000 -cti soc.cti0
```

- 읽는 법: ① 인터페이스·전송·속도 ② 리셋 선 연결 방식(`srst_only` = SRST만 연결, `srst_nogate` = 리셋 중에도 JTAG 가능, `connect_assert_srst` = 리셋 잡고 접속) [6] ③ 체인 선언(순서·IR 길이·기대 IDCODE) [5] ④ DAP 생성 ⑤ 타깃(코어) 생성 — Armv8은 debug base·CTI 주소가 필요하고 ROM table/TRM에서 얻는다.

### 3.6 Trace32 스크립트 골격 `[미실행 — 명령 이름은 기억 기반, Lauterbach 매뉴얼로 확인]`

```text
; bringup_attach.cmm
SYStem.RESet
SYStem.CPU <SoC 또는 코어 이름>     ; Trace32가 아는 칩이면 체인·주소 자동
SYStem.JtagClock 100kHz
SYStem.Mode Prepare                 ; 코어 없이 DAP만 → 메모리(AP) 접근 테스트
Data.dump <access class>:0x10000000 ; SRAM 읽기
SYStem.Mode Attach                  ; 실행 중인 코어에 붙기 (또는 SYStem.Up = 리셋 후 halt)
Register.view
Data.LOAD.Elf bu_sram.elf
Go
```

### 3.7 Linux 쪽에서 보이는 것 (참고)

- 커널이 올라온 뒤에도 CoreSight는 쓰인다: `drivers/hwtracing/coresight` (ETM/ETR trace를 `perf`로), CTI 드라이버 — `/sys/bus/coresight/devices/` [23]. 하드웨어 연동 디버깅 전반은 [EL M11](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/M/2026-10-17_M11_hw_linked_debugging.html).

## 4. 꼬리 질문 체인

### 4.1 BV00 체인 — "JTAG can't read IDCODE" → 좁히기 → halt 불가 → 리셋 직후 첫 명령

<details><summary>답 펼치기</summary>

- **Q. JTAG can't read IDCODE on a new board. What could be wrong?** — (TL;DR의 30초 답) "Physical layer first — power and VTref, reset state including TRST and the debugger's own SRST, a slow TCK, then the scope on TDO. Then chain configuration." · 한국어: 전원·VTref → 리셋(TRST·SRST) → 느린 TCK → TDO 파형 → 체인 설정.
- **꼬리1. How do you narrow it down?** — "The TDO waveform splits the tree. Stuck high means nothing is driving it: an open trace, a missing resistor, an unpowered or disabled TAP. Stuck low means a short or an unpowered level shifter. Toggling but wrong means configuration or signal integrity: I run an autoprobe at low speed, compare the TAP count and IDCODEs with the schematic, measure the total IR length by flushing ones and timing a single zero through the chain, and fix the declared IR lengths. If there's a chain-select switch or an FPGA in the chain, I isolate the SoC alone." · 한국어: TDO 고정 1/0/토글로 갈라서 → autoprobe·총 IR 길이 측정·체인 분리.
- **꼬리2. IDCODE reads but you can't halt the core.** — "Then the TAP and wiring are fine and the problem is behind the debug port. I check that the debug and system power-up requests in CTRL/STAT get acknowledged — if not, the debug power domain is off. Then whether AP accesses return WAIT forever or FAULT — a hung bus or a powered-down block. Then the core itself: power domain, reset — secondary cores are often held in reset until the boot core releases them — and clock. Then authentication: DBGEN may be off on a secured part. And if it halts but immediately resets, it's the watchdog." · 한국어: DP 뒤 — 전원 ACK → AP WAIT/FAULT → 코어 전원·리셋·클럭 → DBGEN → watchdog.
- **꼬리3. How would you debug the very first instruction after reset?** — "Connect under reset with a vector catch. The debugger holds system reset, connects to the debug port — which has to stay accessible during reset — sets the reset vector catch, on Cortex-M that's VC_CORERESET in DEMCR, and then releases reset; the core halts before executing the first instruction. That also rescues boards whose firmware disables the JTAG pins or goes to sleep right after boot." · 한국어: 리셋 잡고 접속 + vector catch(DEMCR VC_CORERESET) → 첫 명령 전 halt, JTAG 핀 끄는 FW 구제.

</details>

### 4.2 "What's boundary scan and why would a board team use it?" → DDR without training

<details><summary>답 펼치기</summary>

- **Q.** — "Boundary scan, IEEE 1149.1, puts a cell between every pin and the core logic, chained into a boundary-scan register you shift through JTAG. With EXTEST, the pins are driven from that register and inputs are captured, so you can drive a pattern out of one chip and read it at another without any firmware or the chip's functional logic. A board team uses it on first prototypes and in production to find opens and shorts net by net — especially under BGAs, where you can't probe and X-ray isn't conclusive — and to program flash. The tools generate the tests from the BSDL files and the netlist." · 한국어: 핀마다 셀, EXTEST로 구동·캡처, FW 없이 net 단위 open/short, BGA 밑, BSDL+넷리스트로 자동 생성.
- **꼬리1. How would you test DDR connectivity without DDR training?** — "Two ways. If the DRAM supports it — DDR4 or soldered-down DDR5 with the TEN pin reachable — put it into connectivity test mode: its pins become a combinational XOR network, so boundary-scan cells on the SoC side can test address, command and data nets quickly without accessing the memory array. LPDDR5 has no TEN pin, so you drive slow write-then-read patterns through the SoC's boundary-scan cells on the DDR pins. Both are slow-speed tests: they prove connectivity, not timing — margin is for training and eye scans later." · 한국어: TEN → CT 모드(XOR), LPDDR5는 BS로 저속 write/read, 연결만 증명·타이밍 아님.
- **꼬리2. What can't boundary scan test?** — "At-speed timing and signal integrity, anything on pins without boundary cells, analog, and AC-coupled high-speed differential links like PCIe — that needs IEEE 1149.6. It also only covers nets that touch at least one boundary-scan device." · 한국어: 고속 타이밍·SI, BS 셀 없는 핀, 아날로그, AC 결합 차동(1149.6 필요).
- **꼬리3. Why PRELOAD before EXTEST?** — "Because the moment EXTEST takes effect, outputs are driven from the update latches. If they hold garbage you could drive two outputs against each other or enable something on the board. PRELOAD puts known safe values in first." · 한국어: EXTEST 순간 쓰레기 값 구동 방지.

</details>

### 4.3 "How do you halt all cores at the same time on a multicore SoC?"

<details><summary>답 펼치기</summary>

- **Q.** — "With the cross-trigger network: each core has a CTI, and the CTIs connect through a cross-trigger matrix with a few channels. I configure each core's CTI so that its 'halted' event goes out on a channel and every core takes a debug request from that channel. Then when one core hits a breakpoint, the others halt within a few cycles instead of milliseconds later through the debugger. Restart works the same way so they resume together." · 한국어: 코어별 CTI + CTM 채널, halt 이벤트 브로드캐스트, restart도 동기.
- **꼬리1. Why does that matter for debugging?** — "In a multicore race, if the other core keeps running for the milliseconds it takes the debugger to stop it, the shared state you want to inspect has already changed. Synchronous halt freezes the whole system at the moment of interest." · 한국어: 디버거 순차 halt는 ms 지연 → 공유 상태 변함.
- **꼬리2. What if the cores are different, say Cortex-A and Cortex-R or an M0+ safety core?** — "Same mechanism if they're on the same cross-trigger network, but often they're in different debug domains. Trace32 handles that as an AMP session with separate windows per core and a sync setting. Some SoCs route CTI triggers into a system trigger mux, which also lets a core halt stop peripherals or timers." · 한국어: 이기종도 CTI 공유면 동일, Trace32 AMP 세션, 시스템 트리거 mux.

</details>

### 4.4 (추가) "How does a debugger actually read memory on an Arm SoC?"

<details><summary>답 펼치기</summary>

- **Q.** — "Through the Debug Access Port. The probe talks JTAG or SWD to the debug port. It first requests debug and system power-up in CTRL/STAT and waits for the acknowledges. Then it selects a memory access port — AHB or AXI for system memory, APB for debug registers — writes the CSW for the access size, writes the target address into TAR, and reads DRW, which makes the AP do a real bus transaction. Reads are posted, so the data comes back on the next access or from RDBUFF. A WAIT response means the bus is busy; a sticky error means the transaction faulted." · 한국어: DP 전원 요청 → AP 선택 → CSW → TAR → DRW → RDBUFF, WAIT/sticky error.
- **꼬리1. The debugger reads a variable and it's stale compared with what the core sees. Why?** — "The AP goes to memory over the bus, and the latest value may still be in the core's data cache. Either read through the core — the debugger can make the halted core do the load — or clean the cache first, or mark that region non-cacheable during debug." · 한국어: 버스 접근은 캐시를 안 거침 → 코어 경유 읽기·캐시 clean.
- **꼬리2. JTAG vs SWD — when would you choose each?** — "SWD needs only two pins and is usually faster per pin, so it's standard on MCUs. JTAG is needed for daisy chains with non-Arm devices and for boundary scan. Many Arm parts have an SWJ-DP that supports both on shared pins, switched with a defined line sequence." · 한국어: SWD = 2핀, JTAG = 체인·boundary scan, SWJ-DP는 둘 다.

</details>

### 4.5 (추가) "Production parts have JTAG locked. How do you debug an RMA or validate the lock?"

<details><summary>답 펼치기</summary>

- **Q.** — "The authentication signals — DBGEN, NIDEN, SPIDEN, SPNIDEN — are driven by the SoC's security logic from fuses or an authentication protocol. For RMA there's usually an authenticated-debug path where the debugger proves it holds a key or certificate and the chip re-opens debug. For validation I'd test both directions: on an open part everything works; after locking, halts, memory access and trace must be refused, and DBGAUTHSTATUS must reflect it — and the unlock must only work with valid credentials." · 한국어: 인증 신호는 fuse·인증 프로토콜, RMA는 authenticated debug, lock negative test.
- **꼬리1. Can a locked chip still return an IDCODE?** — "Often yes, depending on the implementation — the TAP and DP may still respond so tools can identify the part, while access ports or core debug are blocked. So 'IDCODE OK but nothing else works' on a production-fused board is a security question before it's a hardware one." · 한국어: 구현 따라 IDCODE는 응답, AP·코어는 차단 가능 `[추정]`.

</details>

## 5. Don 경험 연결

- "At Solidigm I brought up Cortex-R8, R82 and M0+ cores on our FPGA prototype with Trace32 — attaching to multiple cores, halting them together, using hardware breakpoints and watchpoints in ROM and early boot code, and loading test images over JTAG before any boot path existed. `[숫자 채우기: 코어 개수·세션 구성(SMP/AMP)·Trace32 스크립트 자동화 여부]`"
- "When JTAG didn't connect, the debugging order I used was always physical first — reference voltage, reset, a slow clock and a scope on TDO — and only then the chain configuration and the debug-port power-up. `[숫자 채우기: 실제 막혔던 사례 1개 — 예: 리셋 설정·클럭 비율·전원 도메인]`"
- "I also scripted the debugger for regression, so a nightly run could load an image, run a test and pull results out of memory without a person in the loop — the same idea scales to a board farm. `[숫자 채우기: 사용 여부·규모]`"
- 주의: boundary scan을 직접 해 본 적이 없다면 "**I haven't owned a boundary-scan test program, but I understand how EXTEST and BSDL-driven interconnect tests work and I've simulated one**" 처럼 정직하게 (§6 tapsim F).

## 6. 실습 / 코드

### 6.1 `onsitePrep/bv07/tapsim.c` — TAP 상태 머신 + scan chain 시뮬레이터 (빌드·실행 검증됨)

- 구성: IEEE 1149.1 16상태 전이표, TAP 4개 체인 (TDI → CPLD(IR 3, IDCODE 없음) → FPGA(IR 6) → SoC BS TAP(IR 5) → Arm JTAG-DP(IR 4) → TDO). Capture/Shift/Update 동작, BYPASS(capture 0)·IDCODE·BSR, 보드 net 4개에 결함 주입(short·open), TDO stuck 고장 주입. IDCODE는 **Arm JTAG-DP 0x4BA00477만 실제 값**, 나머지는 가상.
- 빌드·실행 (macOS):

```bash
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined onsitePrep/bv07/tapsim.c -o /tmp/tapsim && /tmp/tapsim
/tmp/tapsim decode 0x4BA00477 0x2BA01477 0x0B7B302F 0x00000000 0xFFFFFFFF 0x4BA00476
```

- 결과: 경고 0, ASan/UBSan 오류 0, exit 0. **실제 출력** (전체: [tapsim.out](onsitePrep/bv07/tapsim.out)):

```text
== A. TMS sequence -> state trace (start: arbitrary state Shift-DR) ==
  TMS = 1 1 1 1 1 0 1 1 0 0   (reset, idle, go to Shift-IR)
    TMS=1  Shift-DR         -> Exit1-DR
    TMS=1  Exit1-DR         -> Update-DR
    TMS=1  Update-DR        -> Select-DR-Scan
    TMS=1  Select-DR-Scan   -> Select-IR-Scan
    TMS=1  Select-IR-Scan   -> Test-Logic-Reset
    TMS=0  Test-Logic-Reset -> Run-Test/Idle
    TMS=1  Run-Test/Idle    -> Select-DR-Scan
    TMS=1  Select-DR-Scan   -> Select-IR-Scan
    TMS=0  Select-IR-Scan   -> Capture-IR
    TMS=0  Capture-IR       -> Shift-IR
  final state: Shift-IR

== B. from every state, TMS=1 x5 reaches Test-Logic-Reset ==
  Test-Logic-Reset : 0 clock(s)
  Run-Test/Idle    : 3 clock(s)
  Select-DR-Scan   : 2 clock(s)
  Capture-DR       : 5 clock(s)
  Shift-DR         : 5 clock(s)
  Exit1-DR         : 4 clock(s)
  Pause-DR         : 5 clock(s)
  Exit2-DR         : 4 clock(s)
  Update-DR        : 3 clock(s)
  Select-IR-Scan   : 1 clock(s)
  Capture-IR       : 5 clock(s)
  Shift-IR         : 5 clock(s)
  Exit1-IR         : 4 clock(s)
  Pause-IR         : 5 clock(s)
  Exit2-IR         : 4 clock(s)
  Update-IR        : 3 clock(s)
  result: 16/16 states reach TLR, worst case 5 clocks

== C. IDCODE autoprobe after reset (TDI=1, 192 clocks in Shift-DR) ==
  TAP 0 (from TDO):  IDCODE 0x4BA00477: version=0x4 part=0xBA00 mfr=0x23B (JEP106 bank 5, id 0x3B = Arm Ltd) bit0=1  -> OK
  TAP 1 (from TDO):  IDCODE 0x2A5C1A2B: version=0x2 part=0xA5C1 mfr=0x515 (JEP106 bank 11, id 0x15 = (not in this mini table)) bit0=1  -> OK
  TAP 2 (from TDO):  IDCODE 0x1BEEF093: version=0x1 part=0xBEEF mfr=0x049 (JEP106 bank 1, id 0x49 = Xilinx (AMD)) bit0=1  -> OK
  TAP 3 (from TDO): 1-bit BYPASS (no IDCODE)
  end-of-chain marker (all ones from TDI) at bit 97
  TAPs found: 4

== D. IR length: total + Capture-IR check (declared, TDO-first: 4 5 6 3) ==
  first 18 TDO bits after Capture-IR (TDO-first): 100010000100010100
  single 0 injected at bit 64 came out at bit 82 -> total IR length = 18
  TAP 0 declared irlen 4: capture LSBs at bit 0 = 10 (expect 1,0) ok
  TAP 1 declared irlen 5: capture LSBs at bit 4 = 10 (expect 1,0) ok
  TAP 2 declared irlen 6: capture LSBs at bit 9 = 10 (expect 1,0) ok
  TAP 3 declared irlen 3: capture LSBs at bit 15 = 10 (expect 1,0) ok
  declared sum 18 vs measured 18 -> chain config OK

== E. select SoC BS TAP (dev2) IDCODE, all others BYPASS ==
  DR length = 1 + 32 + 1 + 1 = 35; bit0 (dev3 bypass) = 0
  dev2 IDCODE taken from bits 1..32:  IDCODE 0x2A5C1A2B: version=0x2 part=0xA5C1 mfr=0x515 (JEP106 bank 11, id 0x15 = (not in this mini table)) bit0=1  -> OK

== F. boundary-scan interconnect test: SoC OUT cells -> 4 nets -> FPGA IN cells ==
  injected: net0<->net1 short (wired-AND), net2 open (receiver pulled up)
  pattern      driven  received  result
  walk1 0001   0001    0100      FAIL
  walk1 0010   0010    0100      FAIL
  walk1 0100   0100    0100      ok
  walk1 1000   1000    1100      FAIL
  walk0 1110   1110    1100      FAIL
  walk0 1101   1101    1100      FAIL
  walk0 1011   1011    1111      FAIL
  walk0 0111   0111    0111      ok
  diagnosis per net (net3..net0 shown above left->right):
    net0: 2 failing pattern(s)  -> follows neighbour: short
    net1: 2 failing pattern(s)  -> follows neighbour: short
    net2: 4 failing pattern(s)  -> always reads 1: open / stuck-high
    net3: 0 failing pattern(s)

== G. chain faults ==
== G1. TDO stuck high (e.g. TDO not connected / target unpowered, pull-up) ==
  end-of-chain marker (all ones from TDI) at bit 0  -> no TAP answered at all
  TAPs found: 0

== G2. TDO stuck low (e.g. shorted / VTref missing on level shifter) ==
  TAP 0 (from TDO): 1-bit BYPASS (no IDCODE)
  ...
  TAP 7 (from TDO): 1-bit BYPASS (no IDCODE)
  ... >8 BYPASS-looking TAPs in a row: implausible -> TDO stuck low?
  TAPs found: 8

== D. IR length: total + Capture-IR check (declared, TDO-first: 4 4 6 3) ==
  first 18 TDO bits after Capture-IR (TDO-first): 100010000100010100
  single 0 injected at bit 64 came out at bit 82 -> total IR length = 18
  TAP 0 declared irlen 4: capture LSBs at bit 0 = 10 (expect 1,0) ok
  TAP 1 declared irlen 4: capture LSBs at bit 4 = 10 (expect 1,0) ok
  TAP 2 declared irlen 6: capture LSBs at bit 8 = 01 (expect 1,0) MISMATCH
  TAP 3 declared irlen 3: capture LSBs at bit 14 = 01 (expect 1,0) MISMATCH
  declared sum 17 vs measured 18 -> CONFIG ERROR (fix -irlen)
```

- **해석 (줄 단위 핵심)**:
  - A: 임의 상태(Shift-DR)에서 TMS=1 다섯 번 → Test-Logic-Reset. 그다음 `0 1 1 0 0` = RTI → Select-DR → Select-IR → Capture-IR → Shift-IR. **"Shift-IR로 가는 TMS 시퀀스는?"** 이라는 질문의 답.
  - B: 16상태 모두 5클럭 이내 TLR — **TRST 핀이 없어도 TMS만으로 리셋 가능**한 이유.
  - C: 리셋 후 DR을 1로 채우며 읽으면 TDO에 가까운 TAP부터: 첫 비트 1 → 32비트 IDCODE, 0 → 1비트 BYPASS. 마지막에 TDI에서 넣은 1들이 그대로 나오면(all ones) 체인 끝. 32+32+32+1 = **97비트** 지점에서 끝 표시 → TAP 4개.
  - D: Capture-IR 패턴 `1000 10000 100010 100` — 각 TAP의 IR LSB가 `1,0` (= 표준의 `..01`, LSB first). 0 하나를 64번째 비트에 넣었더니 82번째로 나옴 → **총 IR 길이 18**. 마지막 블록: SoC TAP을 IR 4로 잘못 선언하면 경계가 어긋나 MISMATCH + 합 17≠18 → OpenOCD가 "IR capture error"류를 내는 상황.
  - E: 한 TAP만 IDCODE, 나머지 BYPASS → DR 35비트, 원하는 32비트는 **앞쪽 BYPASS 비트 수만큼 밀린 위치**에 있다 (디버거 패딩의 원리).
  - F: EXTEST walking-1/0. net2는 구동값이 0인 패턴에서만 실패하고 항상 1로 읽힘 → **open (풀업)**. net0·net1은 서로 따라감 → **short (wired-AND)**. net3 정상. 실제 boundary-scan 도구의 net 단위 진단과 같은 논리.
  - G1/G2: TDO가 1에 고정 → "아무 TAP도 응답 없음"(OpenOCD "all ones"), 0에 고정 → BYPASS 장치가 무한히 있는 것처럼 보임(현실성 없음 → 쇼트 의심).
- **IDCODE 디코더 실제 출력** ([decode.out](onsitePrep/bv07/decode.out)):

```text
  IDCODE 0x4BA00477: version=0x4 part=0xBA00 mfr=0x23B (JEP106 bank 5, id 0x3B = Arm Ltd) bit0=1  -> OK
  IDCODE 0x2BA01477: version=0x2 part=0xBA01 mfr=0x23B (JEP106 bank 5, id 0x3B = Arm Ltd) bit0=1  -> OK
  IDCODE 0x0B7B302F: version=0x0 part=0xB7B3 mfr=0x017 (JEP106 bank 1, id 0x17 = Texas Instruments) bit0=1  -> OK
  IDCODE 0x00000000: version=0x0 part=0x0000 mfr=0x000 (JEP106 bank 1, id 0x00 = (not in this mini table)) bit0=0  -> INVALID: all zeros (TDO stuck low)
  IDCODE 0xFFFFFFFF: version=0xF part=0xFFFF mfr=0x7FF (JEP106 bank 16, id 0x7F = (not in this mini table)) bit0=1  -> INVALID: all ones (TDO stuck high / no device / end of chain)
  IDCODE 0x4BA00476: version=0x4 part=0xBA00 mfr=0x23B (JEP106 bank 5, id 0x3B = Arm Ltd) bit0=0  -> INVALID: bit0 must be 1 for IDCODE
```

- 0x2BA01477 = Arm **SW-DP**의 흔한 DPIDR 값 형태 (같은 Arm 제조사 코드). 0x0B7B302F는 TI 제조사 코드(하위 12비트 0x02F)를 보여 주는 예시 값. 제조사 이름 표는 4개만 넣은 미니 표 — 전체 표는 OpenOCD `src/helper/jep106.inc` [4].
- 한계: 실제 칩의 타이밍(TDO 하강 에지), Pause 상태 사용, DPACC/APACC 35비트 트랜잭션은 모델링하지 않음. BSR은 셀 4개짜리 단순화.

### 6.2 확장 아이디어 `[미실행]`

- tapsim에 Arm JTAG-DP DPACC/APACC 35비트 스캔 + ACK(WAIT/OK) + MEM-AP(TAR/DRW) 모델을 넣어 §2.6의 "메모리 읽기 7단계"를 시뮬레이션.
- 실제 보드가 있다면: 저렴한 Cortex-M 보드 + CMSIS-DAP/J-Link + OpenOCD로 `scan_chain`, `mdw`, `halt`, `reset halt` (vector catch) 확인.

## 7. 자주 틀리는 것

1. **"IDCODE 실패 = 칩 고장"이라고 결론** — 대부분 물리층(VTref·리셋·TCK 속도·핀 1·체인 설정)이다. 칩 탓은 마지막.
2. **TCK를 처음부터 빠르게** — 리셋 직후·PLL 전에는 느린 TCK만 되는 칩이 많다. autoprobe는 느린 클럭으로 [5].
3. **nTRST 풀다운 방치** — 디버거가 TRST를 구동하지 않는 설정이면 TAP이 영원히 리셋 → all ones.
4. **IR 길이를 autoprobe 결과에서 그대로 복사** — JTAG으로 IR 길이를 확실히 알 방법은 없다. BSDL·데이터시트로 확인 [5].
5. **체인 선언 순서를 거꾸로** — OpenOCD는 TDO에 가까운 장치부터 [14].
6. **SW breakpoint를 ROM/flash에** — 명령을 바꿔 써야 하므로 RAM에서만. ROM·flash는 HW breakpoint (개수 제한!).
7. **semihosting 코드를 디버거 없이 실행** — BKPT에서 멈추거나 HardFault. 양산·보드 팜 이미지에서 제거.
8. **디버거 메모리 읽기 = 코어가 보는 값이라고 가정** — 캐시에 있는 최신 값은 AP의 버스 접근으로 안 보일 수 있다.
9. **"SWD로 boundary scan"** — 불가. boundary scan은 JTAG(1149.1) 전용.
10. **boundary scan으로 DDR "검증"을 했다고 말함** — 저속 연결성(open/short)만 증명. 타이밍·마진은 training·eye scan·memtest 영역.

## 8. 출처

1. IEEE 1149.1 개요 — TAP 16상태, TMS 상승 에지 샘플 (TI 데이터시트 SN74ABT18504 미러, "16 states", "returns to Test-Logic-Reset in no more than five TCK cycles if TMS is left high", TMS 내부 풀업) — https://html.alldatasheet.net/html-pdf/236419/TI/SN74ABT18504PM/253/7/SN74ABT18504PM.html
2. FPGARelated glossary "IDCODE" (bit0 = 1로 BYPASS와 구분) — https://www.fpgarelated.com/glossary/idcode
3. UrJTAG book, "Data file format" (part = IDCODE[27:12], version = [31:28]) — https://urjtag.sourceforge.io/book/_data_file_format.html
4. OpenOCD `src/helper/jep106.c` 주석 (IDCODE[11:1]에 JEP106 코드, bank/continuation 인코딩) + JEP106 표준 PDF (7비트 + odd parity) — https://git.jim.sh/jim/openocd/commits/branch/jim/src/helper/jep106.c , https://www.mikrocontroller.net/attachment/39268/jep106k.pdf
5. OpenOCD User's Guide, "TAP Declaration" (`jtag newtap` -irlen/-ircapture/-irmask/-expected-id, ircapture 하위 2비트 01, autoprobe는 느린 클럭, IR 길이는 BSDL로, `dap create`) — https://openocd.org/doc/html/TAP-Declaration.html
6. OpenOCD User's Guide, "Reset Configuration" (trst_only/srst_only/trst_and_srst, srst_gates_jtag/srst_nogate, connect_assert_srst, adapter srst delay) — https://openocd.org/doc/html/Reset-Configuration.html
7. (1번과 동일 TI 데이터시트 계열) Capture-DR/Shift-DR 동작 설명 — https://html.alldatasheet.com/html-pdf/27539/TI/SN54ABTH18504A/226/7/SN54ABTH18504A.html
8. XJTAG docs, "JTAG Instructions" (SAMPLE·PRELOAD·EXTEST, EXTEST 전 PRELOAD) + Corelis JTAG primer (EXTEST = interconnect test) — https://docs.xjtag.com/xjtag/current/concepts/instructions.html , https://www.slideshare.net/slideshow/jtagtechnicalprimerpdf/253135726
9. CTI/CTM: Nordic nRF5340 cross-trigger network (코어 동시 start/halt, 4채널), Infineon TRAVEO T2G KBA235072, ADI EE-402, SEGGER 포럼 (디버거가 CTI를 자동 설정하지 않는 경우) — https://docs.nordicsemi.com/r/jji3tsD71OQEGmAf3zhPOQ/Zewmimu_g45yIxegd6uwMQ , https://community.infineon.com/t5/Knowledge-Base-Articles/Routing-a-CTI-signal-e-g-CPU-halted-to-the-TRAVEO-T2G-trigger/ta-p/342363 , https://www.analog.com/media/en/technical-documentation/application-notes/EE402v01.pdf , https://forum.segger.com/index.php/Thread/7960-SOLVED-Multicore-Debugging-with-multiple-instance-and-Cross-Trigger-Interfaces
10. XJTAG, "DDR5 Testing with JTAG Boundary Scan" (CT 모드·TEN 핀, DDR5 DIMM은 TEN 없음, LPDDR5 TEN 없음 → write/read) — https://www.xjtag.com/about-jtag/testing-non-jtag-devices/ddr5/
11. TI SLAU723A (MSP432E4) JTAG instruction register 표 (EXTEST 0x0, SAMPLE/PRELOAD 0x2, ABORT 0x8, DPACC 0xA, APACC 0xB, IDCODE 0xE, BYPASS 0xF, 나머지 BYPASS) — https://ti.com/document-viewer/lit/html/SLAU723A/instruction-register-ir-marker-1162837
12. NXP AN13507 (BSDL = VHDL 기반 boundary-scan 기술 언어) — https://www.nxp.com.cn/docs/en/application-note/AN13507.pdf
13. Arm DAP IDCODE 실례: mbed 드라이버의 0x4ba00477 체크, Microchip 문서의 0x5BA00477 (version만 다름) — https://os.mbed.com/users/josewang/code/DDRO_Farrari//rev/bf2ce6c4e789
14. OpenOCD FAQ #13–14 (TDO에 가까운 장치부터 나열, 하드웨어 순서대로 선언, STM32 예: Cortex-M TAP + boundary-scan TAP) — https://openocd.org/doc/html/FAQ.html
15. Arm community, DAP는 4비트 또는 8비트 IR, 5개 명령 (ABORT·DPACC·APACC·IDCODE·BYPASS) + OpenOCD adi_v5_jtag 8비트 값 (0xF8/0xFA/0xFB/0xFE) — https://community.arm.com/support-forums/f/armds-forum/9057/dap-related-question
16. OpenOCD `src/target/adi_v5_jtag.c` (JTAG_ACK_WAIT 0x1, JTAG_ACK_OK_FAULT 0x2 (ADIv5), 3비트 헤더 bits 2:1 = A[3:2], bit 0 = RnW + 32비트 데이터) — https://openocd.org/doc/doxygen/html/adi__v5__jtag_8c_source.html
17. OpenOCD `arm_adi_v5.h` (CDBGPWRUPREQ 1<<28, CSYSPWRUPREQ 1<<30, MEM-AP CSW 0x00·TAR 0x04·DRW 0x0C, ADIv6 0xD00) + Nordic nRF9160 anomaly 160 (두 전원 요청 비트를 같이 써야) — https://openocd.org/doc/doxygen/html/arm__adi__v5_8h_source.html , https://infocenter.nordicsemi.com/topic/errata_nRF9160_Rev2/ERR/nRF9160/Rev2/latest/anomaly_160_32.html
18. JTAG→SWD 전환: TI SLAU723A (0xE79E LSB first, 전환 후 READID로 확인), Microchip SW-DP/JTAG-DP selection (50+ 클럭 SWDIO=1) — https://www.ti.com/document-viewer/lit/html/SLAU723A/jtag-to-swd-switching-jtag-swd , https://onlinedocs.microchip.com/oxy/GUID-EDB1839B-8CB3-4792-92DA-B3964E0885C3-en-US-3/GUID-40B3DC03-98F1-46EB-8608-DB96BB775CCF.html
19. Cortex-M debug: OpenOCD `cortex_m.h` (DBGKEY 0xA05F, C_DEBUGEN bit0, C_HALT bit1), Microchip "Enter the Debug Mode" (DEMCR.VC_CORERESET), libopencm3 `scs.h` — https://openocd.org/doc/doxygen/html/cortex__m_8h_source.html , https://onlinedocs.microchip.com/oxy/GUID-B9438730-F4D7-455B-A2FD-546AF83052A2-en-US-2/GUID-E326B478-51BB-44DF-9546-16B4F95A7082.html
20. HW breakpoint 개수: TI AM67 (Cortex-A53: 6 breakpoints, 4 watchpoints), AMD UG1087 ID_AA64DFR0_EL1 (A53), ST STM32G4 debug 교육자료 (FPB 6 comparators), Microchip DWT 4 comparators — https://ti.com/document-viewer/lit/html/SPRUJB3/GUID-A615D008-89FC-468D-8CDF-0470164D0F40 , https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/ID_AA64DFR0_EL1_31to0-A53_DBG_3-Register , https://www.st.com/resource/en/product_training/STM32G4-System-Debug_DBG.pdf
21. ASSET InterTech, "Testing DDR4 Memory with Boundary Scan/JTAG" / "JTAG DDR4 Connectivity Test Mode (JESD79-4)" — https://www.asset-intertech.com/?p=4939 , https://asset-intertech.com/?p=2763
22. Secure debug: NXP AN13814 / AN13037 (debug authentication, DBGEN·NIDEN·SPIDEN·SPNIDEN), Microchip Security Module debug 문서 — https://nxp.com/docs/en/application-note/AN13814.pdf , https://www.NXP.com/docs/en/application-note/AN13037.pdf
23. Linux kernel docs, "Coresight - HW Assisted Tracing on ARM" — https://www.kernel.org/doc/html/v5.7/trace/coresight/coresight.html

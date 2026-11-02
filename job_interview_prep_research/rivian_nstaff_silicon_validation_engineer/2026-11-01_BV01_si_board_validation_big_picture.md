# BV01 · Silicon vs board validation 큰 그림 — pre-si → A0 → bring-up → PVT → stepping → SLT
<!-- theme: pastel -->

> **묶음**: A 큰 그림 · 회로도 · **우선**: P0 (온사이트 도메인 라운드 "Walk me through bringing up a new SoC", 면접관 경력 = FPGA/HAPS 프로토타이핑 · bring-up · post-si validation · PVT · production SLT) · **선행 노트**: 없음 (트랙 시작점) · [BV 트랙 큰 그림 §0.5](2026-11-01_BV00_board_validation_track.html) · **다음 노트**: [BV02 회로도·레이아웃·BOM 읽기](2026-11-01_BV02_schematic_layout_reading.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html) · **관련**: [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html) · [LP16 bare-metal vs Linux (Zynq)](2026-10-31_LP16_baremetal_vs_linux_zynq.html) · [deck §6 검증 설계](2026-10-25_rivian_hm_screen_prep.html) · [BV06 첫 전원 인가 날](2026-11-01_BV06_first_power_on_checklist.html) · [BV12 shmoo·PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [BV14 검증 계획·자동화](2026-11-01_BV14_board_validation_plan_automation.html) · **코드**: 없음 (지도 노트 — §6에 연습 레시피 `[미실행]`, 실행 코드는 [BV02 체크리스트 스크립트](onsitePrep/bv02/bringup_checklist.py))

## 0. TL;DR

- 이 노트로 답할 것: "새 SoC를 새 보드에서 어떻게 살리나 (전체 흐름)", "실리콘 오기 전에 뭘 준비해 두나", "FPGA 작업을 실리콘에서 어떻게 재사용하나", "FPGA와 실리콘의 차이 중 무엇이 발목을 잡나", "DV(설계 검증)와 silicon validation은 뭐가 다른가", "DV 커버리지 100%인데 왜 실리콘에서 버그가 나오나", "errata에는 뭐가 들어가나", "SLT는 ATE와 뭐가 다른가", "EVT/DVT/PVT는 뭔가".
- **외울 것 ① — 칩 일생 한 줄**: spec → RTL → **sim/DV · emulation · FPGA(HAPS)** → **tape-out** → (fab 수개월) → **A0** 도착 → ATE로 살아 있는 칩 골라냄 → **bring-up**(전원→클럭→리셋→JTAG→부트→DDR→IO) → **post-si 기능·성능 validation** → **PVT 특성화(shmoo)** → 버그는 **errata + workaround**, 고칠 건 **stepping(A1 metal / B0 base)** → **qualification(신뢰성)** → **양산 테스트(ATE 구조 테스트 + SLT 시스템 테스트)**.
- **외울 것 ② — 보드는 따로 돈다**: 칩 stepping(A0/B0)과 별개로 보드는 **EVT → DVT → PVT** 리비전을 거친다. 보통 A0는 **검증 보드(EVB/CRB/socketed, 테스트 포인트 많음)**에서, 제품 폼팩터 보드는 나중에. 그래서 첫 실리콘 버그는 **"칩 vs 보드 vs SW"** 셋이 섞여 있다 → 가르는 실험(swap · 다른 FW · 측정)이 핵심 능력.
- **외울 것 ③ — validation SW 엔지니어의 일 = "실리콘 Day 1에 같은 바이너리를 바로 돌리는 것"**: pre-si에서 테스트 FW·HAL·자동화·체크리스트를 FPGA로 미리 검증 → 실리콘에선 **차이만 디버그**. FPGA vs 실리콘 차이(클럭 비율·속도 10–100×, 아날로그 PHY/DDR PHY 없음, 전원 도메인·리셋 타이밍, 퓨즈·보안, 온도·전압)를 **파라미터화**해 둔다.
- **ATE vs SLT 한 줄**: ATE = 비싼 테스터가 **scan/BIST 패턴으로 게이트 수준 결함**을 몇 초에 잡음(커버리지 측정 가능) · SLT = 칩을 **실제 보드처럼 꽂아 OS·워크로드를 수 분** 돌려서 ATE가 못 잡는 **상호작용·마진·아날로그·클럭 경계** 결함을 잡음(커버리지 지표 약함) [3][4].
- 30초 영어 답 (headline Q: "Walk me through how you'd bring up a brand-new SoC on a new board."):
  - "Most of the work happens before the silicon arrives. On the FPGA prototype I get the bring-up firmware, the HAL, the register and memory tests and the automation running, with every platform difference — clock ratios, timeouts, which PHYs exist — in one config file, plus a bring-up checklist from the schematic. When A0 arrives on the validation board, I go bottom-up and don't move on until each layer is proven: power rails and sequencing with a current-limited supply, clocks and PLL lock, reset release and strap values, JTAG IDCODE and halting a core, running from SRAM with UART output, then boot flow, DDR training and memory test, then each interface. After that it's functional and performance validation, PVT characterization, and turning every failure into either a silicon erratum, a board fix, or a software fix — with the regression running nightly on a board farm."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유: 신차 개발 (Rivian답게)

새 SoC 하나가 차에 들어가기까지 = **신차 한 모델이 공장에서 나오기까지**.

- **설계도 + 컴퓨터 시뮬레이션** (pre-silicon DV): 차를 만들기 전에 CAD와 충돌 시뮬레이션으로 "설계대로 동작하나"를 본다. 칩에선 RTL(하드웨어를 글로 쓴 코드)을 시뮬레이터로 돌린다. 매우 느리지만 **모든 신호를 다 볼 수 있다**.
- **실물 크기 목업 + 시험 주행 장치** (emulation / FPGA prototype): 진짜 엔진은 아니지만 비슷하게 움직이는 시작차를 만들어 **운전 SW(펌웨어)를 미리 개발**한다. 칩에선 RTL을 거대한 에뮬레이터나 FPGA 보드(HAPS)에 올려 수 MHz–수십 MHz로 돌린다. 진짜 칩보다 10–100배 느리고, 아날로그 부품(고속 PHY)은 가짜다.
- **금형 발주** (tape-out): 설계를 확정해 공장(fab)에 마스크를 보낸다. 이후 수정은 비싸고 오래 걸린다.
- **첫 시작차 출고** (A0 silicon): 공장에서 처음 나온 칩. 몇 개 안 되고 귀하다.
- **첫 시동** (bring-up): 배터리 연결 → 계기판 불 → 시동 → 기어 → 첫 주행. 칩에선 전원 → 클럭 → 리셋 → JTAG → 부팅 → 메모리 → 인터페이스.
- **기능·성능 시험** (post-si validation): 모든 기능(에어컨·내비·오토파일럿)과 성능(0–100 km/h)을 사양서대로 확인.
- **혹한·혹서·고지대 시험** (PVT characterization): −40 °C 겨울, 사막 더위, 배터리 전압이 낮을 때도 되나. 칩에선 공정 편차(Process)·전압(Voltage)·온도(Temperature) 끝 조건에서 **여유(margin)** 를 잰다 → shmoo plot.
- **리콜 대신 연식 변경** (stepping A0→A1→B0) + **알려진 문제 공지** (errata): 고칠 수 있는 건 다음 생산분에서 고치고, 이미 나간 건 "이렇게 피해 가세요(workaround)"를 문서로 낸다.
- **내구·인증 시험** (qualification): 수천 시간 가혹 조건 운행, 규격 인증. 칩에선 신뢰성 시험(HTOL 등)과 자동차 규격(AEC-Q100).
- **공장 출고 검사** (production test: ATE + SLT): 모든 차를 라인 끝에서 빠르게 검사. 칩에선 **ATE**(수 초짜리 구조 테스트)와 **SLT**(실제 보드처럼 꽂아 OS 돌리는 수 분짜리 시스템 테스트)로 **불량 칩을 걸러낸다**.

그리고 **차체·섀시(보드)** 는 엔진(칩)과 별도로 자기 시작차 단계를 거친다 — **EVT(기능 되나) → DVT(설계 확정, 인증 가능한가) → PVT(양산 라인에서 똑같이 만들어지나)**. 엔진 시작품을 차체 시작품에 얹어 테스트하니까, 문제가 나면 **엔진 탓인지 차체 탓인지 운전 SW 탓인지** 가려야 한다. 이게 이 포지션(silicon + board validation)의 핵심이다.

### 1.2 큰 지도 — 칩 트랙과 보드 트랙이 만나는 곳

```text
 칩(silicon) 트랙
 ┌─────────── pre-silicon ───────────┐        ┌──────────────────── post-silicon ────────────────────┐   ┌── 양산 ──┐
 spec → RTL → sim/DV(UVM) ─┐                   A0 도착 → ATE 선별 → bring-up → 기능 val → 성능 → PVT/shmoo   ATE (CP/FT)
                emulation ─┼─▶ TAPE-OUT ─▶ fab ─▶  │         (살아 있는 칩)  ▲                         │      SLT
   FPGA prototype (HAPS) ──┘   (마스크)   (수개월)  │                         │  errata·workaround       │      burn-in
        │                                         │                         └── A1(metal)/B0(base) ◀──┘
        │ ① 테스트 FW·HAL·자동화를 미리 돌려 둠      │                              stepping → 재검증 → qual(신뢰성)
        └──────────────── 같은 바이너리 ─────────────▶│
                                                  │
 보드(board) 트랙                                   ▼
 schematic → layout → fab/assembly → [validation/EVB/socketed 보드] → EVT → DVT → PVT → 양산 보드
   ▲ (BV02: 실리콘 오기 전 회로도로                 (A0가 처음 얹히는 곳: 테스트 포인트·점퍼·
     bring-up 체크리스트 작성)                       전류 측정 shunt 많음)
```

- 가로축 = 시간. 칩 쪽 tape-out → A0 사이 몇 달 [추정: 공정·패키지에 따라 대략 2–4개월]이 **pre-si 준비를 마무리하는 마지막 기회**.
- 세로로 만나는 점: **A0 칩이 검증 보드에 처음 올라가는 날** = bring-up Day 1 ([BV06](2026-11-01_BV06_first_power_on_checklist.html)).

### 1.3 "검증" 단어가 다섯 개 — 헷갈리지 않게

| 단어 | 쉬운 말 | 질문 | 누가 | 어디서 |
|---|---|---|---|---|
| **Verification (DV)** | 설계도 검사 | "RTL이 스펙대로 쓰였나?" | DV 엔지니어 | 시뮬레이터·formal·에뮬레이터 |
| **Validation** | 실물 시험 | "실제 칩·보드·SW가 진짜 조건에서 동작하나?" | silicon/board validation (이 포지션) | 실리콘 + 보드 + 실제 SW |
| **Characterization** | 한계 측정 | "얼마나 여유 있게 되나? (전압·온도·주파수)" | validation + 특성화 엔지니어 | 챔버·전압 margining·shmoo |
| **Qualification** | 내구·인증 | "수년간·규격 조건에서 안 망가지나?" | 신뢰성(quality) 팀 | 신뢰성 랩 (HTOL, ESD 등) |
| **Production test** | 출고 검사 | "이 칩 한 개가 불량인가?" | test(product) 엔지니어 | ATE · SLT 장비 |

- 핵심 구분: **verification은 "설계가 맞나", validation은 "만든 물건이 쓰임새에 맞나"**, production test는 "**이 한 개**가 제조 불량인가" (설계 버그를 찾는 게 아님).

### 1.4 누가 뭘 하나 (회의실에 앉는 사람들)

```text
            ┌────────────── 아키텍트 (spec) ──────────────┐
            ▼                                            ▼
   RTL 설계자 ──▶ DV 엔지니어 (UVM, coverage)        SoC 통합 / DFT (scan·BIST·JTAG 삽입)
            │                                            │
            ▼                                            ▼
   프로토타이핑 팀 (FPGA/HAPS 빌드)  ◀── 같이 일함 ──▶  validation SW (테스트 FW·HAL·자동화)  ← 이 포지션
            │                                            │
            ▼                                            ▼
   보드(HW) 엔지니어 · SI/PI 엔지니어 ── 보드 리뷰 ──▶  silicon validation / board validation (bring-up · 기능 · PVT)
                                                         │
                         ┌───────────────────────────────┼─────────────────────────────┐
                         ▼                               ▼                             ▼
               product SW (드라이버·OS·BSP)      test/product 엔지니어 (ATE·SLT)    quality/reliability (qual)
```

- **이 포지션 (Rivian Silicon & Sensing, Staff Embedded SWE)** = validation SW + silicon/board validation 겹치는 자리. 10-30 스크린에서 "silicon validation + board validation, 주로 C"로 확인됨 [확인됨]. 면접관 경력(FPGA/HAPS · bring-up · post-si · PVT · production SLT)은 이 그림의 왼쪽 아래 → 오른쪽 아래 전부를 지나온 사람 → 질문이 **단계 간 연결(handoff)** 로 올 가능성이 높다 [추정].

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| RTL | Register Transfer Level | 하드웨어를 Verilog/SystemVerilog 코드로 쓴 것 | 모든 pre-si 단계의 입력 |
| DV | Design Verification | RTL을 시뮬레이션으로 검사하는 일 (UVM 테스트벤치, 커버리지) | validation과 역할 구분 |
| UVM | Universal Verification Methodology | DV 표준 테스트벤치 프레임워크 (SystemVerilog) | DV 쪽 용어라 알아만 두기 |
| coverage | Functional/Code Coverage | "테스트가 설계의 어느 부분을 건드렸나" 측정 | "100%인데 왜 버그?" 질문 |
| formal | Formal Verification | 수학적으로 모든 입력을 증명하는 검사 | 작은 블록엔 강력, 전체 SoC엔 한계 |
| emulation | Hardware Emulation (Cadence Palladium, Synopsys ZeBu, Siemens Veloce) | 전용 장비에서 RTL을 ~MHz로 실행, 내부 신호 가시성 좋음 | 부팅 FW·드라이버 초기 검증 |
| FPGA prototype | FPGA-based Prototyping (Synopsys HAPS, S2C, 자체 보드) | FPGA에 RTL을 나눠 올려 수십 MHz로 실행 | SW를 실리콘 전에 개발 (Don Solidigm) |
| virtual platform | Virtual Prototype (QEMU, Arm FVP 등) | 칩을 SW 모델로 흉내 — 가장 빠르지만 타이밍 부정확 | 드라이버 아주 초기 개발 |
| tape-out | Tape-out | 설계를 확정해 fab에 마스크 데이터를 넘기는 시점 | 이후 수정 = 비용·몇 달 |
| mask | Photomask | 웨이퍼에 회로를 찍는 원판, 층(layer)마다 하나 | metal 층만 바꾸면 싸고 빠름 |
| A0 | First silicon stepping | 처음 나온 칩 버전 | bring-up 대상 |
| stepping | Silicon revision | 칩 버전. **글자 = base layer(트랜지스터) 변경, 숫자 = metal layer(배선)만 변경** [6] | B0는 비싸고 느림, A1은 빠름 |
| ECO | Engineering Change Order | tape-out 후 설계 수정; metal-only ECO는 미리 뿌려 둔 spare cell을 배선만 바꿔 활용 [14] | A1 stepping의 수단 |
| errata | Errata / Specification Update | "알려진 실리콘 버그 + 영향 + workaround + 고칠 stepping" 목록 [7] | 버그를 문서로 관리 |
| workaround | Workaround | 버그를 SW/FW/보드 설정으로 피해 가는 방법 | 다음 stepping 전까지 제품을 살림 |
| chicken bit | Chicken bit / defeature bit | 새 기능을 끌 수 있게 설계에 넣어 둔 비트 | 실리콘 버그 회피 수단 1순위 |
| bring-up | Silicon / Board Bring-up | 첫 칩·첫 보드를 전원부터 하나씩 살리는 단계 | 면접 1순위 질문 |
| post-si validation | Post-silicon Validation | 실제 칩에서 기능·성능·전력·호환성을 검증 [1][2] | 이 포지션 본업 |
| PVT | Process, Voltage, Temperature | 공정 편차·전압·온도의 끝 조건 조합 (corner) | 마진은 corner에서 깨진다 |
| skew lot / corner lot | Process corner lot | 일부러 공정을 빠르게(FF)/느리게(SS) 만든 칩 묶음 | post-si에서 process corner를 얻는 방법 |
| shmoo | Shmoo plot | 두 변수(예: 전압×주파수)를 격자로 바꿔 PASS/FAIL을 칠한 그림 | 마진 시각화 ([BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)) |
| binning | Speed/Power Binning | 칩을 성능·전력으로 등급 나누기 | 특성화 결과의 용도 |
| qualification | Reliability Qualification | 수명·가혹 조건 시험(예: HTOL, ESD, latch-up)과 규격 인증 | 자동차는 AEC-Q100 ([BV15](2026-11-01_BV15_automotive_board_considerations.html)) |
| HTOL | High Temperature Operating Life | 고온·고전압으로 오래 돌려 수명 가속 시험 | qual 대표 항목 |
| DFT | Design for Test | scan chain·BIST·JTAG 등 테스트용 회로를 설계에 넣는 것 | ATE 테스트의 기반 |
| DFD | Design for Debug | trace·on-chip 로직 애널라이저·debug 레지스터 등 디버그용 회로 | post-si 가시성 확보 [12] |
| scan / ATPG | Scan chain / Automatic Test Pattern Generation | 칩 안 flip-flop을 줄줄이 엮어 패턴을 밀어 넣고 결과를 읽음; 패턴은 툴이 자동 생성 | ATE 구조 테스트의 핵심 |
| BIST | Built-In Self-Test (MBIST: 메모리, LBIST: 로직) | 칩이 스스로 자기 메모리/로직을 검사 | ATE·현장(자동차 안전)에서 사용 |
| ATE | Automatic Test Equipment | 웨이퍼/패키지 칩을 핀 단위로 찌르는 고가 테스터 | 양산 1차 선별 |
| CP / FT | Chip Probe (wafer sort) / Final Test | 웨이퍼 상태 테스트 / 패키지 후 테스트 | ATE가 도는 두 지점 |
| SLT | System-Level Test | 칩을 실제 보드 비슷한 환경에서 OS·워크로드로 테스트 [3][4] | 면접관 경력 키워드 |
| burn-in | Burn-in | 고온·고전압으로 미리 혹사시켜 초기 불량을 걸러냄 | 자동차에서 SLT와 결합되기도 [3] |
| EVB / CRB | Evaluation Board / Customer Reference Board | 칩 검증·고객 참고용 보드 (점퍼·TP 많음) | A0가 처음 얹히는 보드 |
| socketed board | Socketed validation board | 칩을 납땜 대신 소켓에 꽂는 보드 | 칩 여러 개 바꿔 끼우며 비교·PVT |
| EVT / DVT / PVT | Engineering / Design / Production Validation Test | 보드(제품) 하드웨어 개발 단계 [10][11] | "칩 PVT"와 이름이 겹침 — 주의 |
| golden board/unit | Golden reference | 정상 동작이 확인된 기준 보드/칩 | 비교 실험(swap)의 기준 |

> ⚠ **"PVT"가 두 뜻**: 칩 특성화의 **P**rocess-**V**oltage-**T**emperature vs 보드 단계의 **P**roduction **V**alidation **T**est. 면접에서 말할 때 "PVT corners" / "PVT build"로 구분해서 말한다.

## 2. 핵심 개념 (정확한 버전)

### 2.1 Pre-silicon 엔진 4종 — 속도 vs 가시성

| 엔진 | 대략 속도 | 내부 가시성 | 주 용도 | 한계 |
|---|---|---|---|---|
| RTL simulation (VCS/Xcelium/Questa) | 전체 SoC 기준 매우 느림 (Hz–kHz 수준) [추정] | 모든 신호 | 블록·서브시스템 DV, 커버리지 | OS 부팅 불가능에 가까움 |
| Emulation (Palladium/ZeBu/Veloce) | ~MHz 수준 [추정] | 높음 (파형 덤프 가능) | 부팅 ROM·FW·드라이버, 전력 분석 | 비쌈, 공유 자원, 실제 IO 연결 제한 |
| FPGA prototype (HAPS 등) | 수십 MHz; 실리콘 대비 **10–100× 느림** ("within one or two orders of magnitude") [8] | 낮음 (내부 probe는 재빌드 필요) | 장시간 SW, OS, 실제 IO 연결, 스트레스 | 아날로그 PHY·DDR PHY 대체, 빌드 시간 수 시간 |
| Virtual platform (QEMU/FVP) | 매우 빠름 (실시간 근접) | SW 관점만 | 드라이버 골격·OS 포팅 | 타이밍·HW 버그 못 봄 |

- 숫자 감각 [8]: Android 부팅 ≈ 300억 사이클 → 2 GHz 실리콘 약 15초 vs **50 MHz FPGA 약 10분**. 그래서 FPGA는 "실리콘 전 SW를 실제로 돌릴 수 있는 유일한 곳"이지만, 실리콘 Day 1에 같은 테스트가 **수십 배 빠르게** 돈다 → 타임아웃·폴링 루프·WDT 값이 그대로면 깨진다(§2.9).
- "Shift-left" [12]: 예전엔 실리콘 후 하던 SW bring-up 상당 부분이 tape-out 전으로 당겨졌다. 하지만 pre-si는 상태 공간을 다 못 덮으니 post-si는 여전히 필요.

### 2.2 Tape-out → A0 도착까지

1. **Tape-out**: GDSII(레이아웃 데이터) → 마스크 제작 → 웨이퍼 공정.
2. **Wafer → 패키지**: 웨이퍼 테스트(CP) 프로그램은 초기엔 아주 단순할 수 있음 — test 엔지니어가 ATE 패턴(scan, MBIST)을 첫 실리콘에서 디버그하는 단계 [추정].
3. **A0 샘플 배포**: 수량이 적음 → 팀 간 배분, **"한 개도 태우지 말 것"**(전류 제한, 정전기, 소켓 보드 우선) → [BV06](2026-11-01_BV06_first_power_on_checklist.html).
4. 이 기간 validation SW가 할 일: FPGA 회귀 안정화, 실리콘용 빌드 설정, 회로도 기반 체크리스트([BV02](2026-11-01_BV02_schematic_layout_reading.html)), 장비·보드 팜 준비, 실리콘 전용 테스트(아날로그 PHY, PLL, 퓨즈, 온도 센서 — FPGA에 없던 것) 작성.

### 2.3 Bring-up — 아래 계층부터 증명

[BV00 §0.5](2026-11-01_BV00_board_validation_track.html)의 사슬 그대로: **전원 → 클럭 → 리셋/strap → JTAG → SRAM 코드·UART → 부트 → DDR → 인터페이스 → 성능·마진**. 원칙 3개 [1]:

- **아래 계층이 증명되기 전엔 위 계층 증상을 믿지 않는다** (DDR 에러가 사실은 rail droop일 수 있다).
- **한 번에 하나만 바꾼다**, 모든 변경·rework를 보드 SN과 함께 기록.
- **ATE에서 이미 확인된 것과 겹치지 않게**: 칩 ID, scan/MBIST 결과는 test 팀이 먼저 가지고 있다 → "이 칩이 ATE pass 칩인가"를 먼저 확인 [추정].

### 2.4 Post-silicon functional · performance validation

- **기능**: pre-si 검증 계획(vplan)과 짝이 되는 validation 계획으로 모든 기능을 체계적으로 → 스펙 위반은 버그로 기록 [1]. 대부분의 post-si 버그가 이 단계에서 나온다고 [1]은 설명.
- **실리콘에서만 보이는 것** [1]: 배선의 RC 지연·전압 droop·glitch(파라시틱), 연속적인 전압·온도 범위, crosstalk, 시뮬레이션이 안 해 본 조합(실사용 시나리오).
- **성능**: 대역폭·지연·처리량 vs 이론치, 성능 카운터 ([성능 메트릭 노트](2026-10-30_perf_metrics_ssd_vs_soc.html)), 성능 미달은 종종 통합 후에만 보임 [12].
- **실패 분류** [1]: 실패를 RTL 시뮬레이션에서 재현 시도 → 재현되면 RTL 버그(그리고 DV 구멍), 재현 안 되면 실리콘 고유 효과(특정 PVT corner 타이밍 등) 의심. 모든 escape는 vplan·회귀·제약 모델로 되돌려 반영.
- **가시성 문제** [2]: 실리콘은 내부를 거의 못 본다 → DFD(trace, debug 레지스터, on-chip 로직 애널라이저, scan dump)가 미리 설계에 들어가야 한다 [12].

### 2.5 PVT characterization

- 전압(nominal ± 수 %, PMIC margining), 온도(챔버/thermostream, 자동차는 넓은 범위), process(skew lot: FF/TT/SS)를 조합해서 **어디까지 PASS인지** 측정 → shmoo, 마진 표.
- 결과의 용도: 동작 스펙 확정, 속도/전력 binning, AVS(적응 전압) 테이블, 양산 스크린 조건(SLT·ATE 조건), 다음 stepping 수정 요청 [1].
- 상세 → [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html).

### 2.6 Steppings · errata · workaround

```text
 A0 ──(metal-only ECO: spare cell 재배선, 마스크 몇 장만 새로)──▶ A1 ──▶ A2
  │
  └──(base layer 변경: 트랜지스터부터 다시, 전체 마스크)──────────▶ B0 ──▶ B1 ──▶ C0 ...
```

- 이름 규칙 [6]: **글자 = base layer(트랜지스터) 변경, 숫자 = metal layer(배선)만 변경**. base 변경은 느리고 비싸며, 어떤 수정은 base에서만 가능.
- metal-only ECO는 미리 흩어 둔 **spare cell**을 metal 층만 바꿔 연결 → 마스크 수 장만 새로 [14] → 빠르고 싸다.
- 새 stepping을 내는 이유 [6]: 로직 버그 수정, 속도 개선, 수율 개선, bin 분포 개선, 테스트 시간 단축.
- **Errata (Specification Update)** 엔트리 구성 [7] (Intel은 A0 erratum과 workaround를 Specification Update로 공개, B0에서 고친 것도 같은 문서에 기록):

| 필드 | 내용 |
|---|---|
| ID · 제목 | 예: "ERR-017: PCIe link may fail to train after warm reset" `[예시]` |
| Problem | 증상과 조건 (어떤 모드·전압·온도·순서에서) |
| Implication | 사용자/시스템에 주는 영향 (데이터 손상? hang? 성능?) |
| Workaround | SW/FW 설정, chicken bit, 타이밍 지연, 보드 변경, 또는 "None" |
| Status | 영향받는 stepping, 수정 예정 stepping (Fixed in B0 / No fix planned) |

- **workaround 종류** (싼 것부터): ① FW/드라이버에서 순서·지연 변경 ② chicken bit로 기능 끄기 ③ 퓨즈/OTP 설정 ④ 보드 변경(풀업, 클럭 소스) ⑤ 다음 stepping에서 수정. validation SW는 **workaround가 진짜 효과 있는지 통계적으로 증명** ([LP14 rule of three](2026-10-31_LP14_validation_methodology.html))하고, 회귀 테스트에 고정.

### 2.7 Qualification (가볍게)

- 칩 수명·가혹 조건 신뢰성: HTOL(고온 동작 수명), ESD, latch-up, 온도 사이클 등 — 자동차는 **AEC-Q100** 체계 [추정: 항목 세부는 BV15에서]. validation SW 관점 연결점: qual 보드에서 돌릴 **스트레스 패턴/모니터 FW** 제공, 고장 시 로그.

### 2.8 Production test — ATE vs SLT

| 항목 | ATE 구조 테스트 (CP/FT) | SLT (System-Level Test) |
|---|---|---|
| 무엇을 보나 | 게이트 수준 결함 모델(stuck-at, bridging, delay) [3] | 칩 전체가 실제 사용처럼 동작하나 [3] |
| 자극 | ATPG 패턴을 scan/BIST로 주입 [3] | OS 부팅·실제 애플리케이션·스트레스 워크로드 [3] |
| 장비 | 고가 ATE, 핀 단위 정밀 제어 | 평가 보드형 소켓 보드 + SLT 핸들러 (범용 컴퓨터로도 가능) [3][5] |
| 시간 | 짧음 (초 단위) [추정] | 수 분 — 앞 단계의 몇 배 [3] |
| 품질 지표 | fault coverage로 측정 가능 | 체계적 지표 없음 [3] |
| 잡는 결함 | 대부분의 제조 결함, 진단 쉬움 | 구조 테스트가 놓친 것: 비동기·아날로그 인터페이스, clock gating, clock domain 경계, 비코어 타이밍, 저전압/DVFS에서의 soft 타이밍 실패, 온도 구배 의존 결함 [3] |
| 약점 | 실제 동작 패턴과 다른 활동 → 실사용 실패를 놓침 [3] | 커버리지 낮고 디버그 어려움 [5]; 비용·시간 |

- Teradyne 표현 [4]: ATE는 정밀·속도, SLT는 **실제 워크로드·시스템 상호작용·환경 스트레스**라는 맥락을 더함 — 대체가 아니라 보완.
- **SLT 프로그램 구성 요소** [3]: 실제 워크로드, 자기발열로 목표 접합 온도 도달 + 과열 방지 제어, 온도 조건(챔버·구배), **전압을 스펙 밖으로 올리고 내려 잠재 결함 스크린**(손상 안 나게 단계적으로), 열 특성화(열화상·온칩 센서), adaptive test(불량 의심 칩만 SLT).
- **validation → SLT 연결**: post-si에서 실패를 잘 잡은 테스트(특히 마진·상호작용형)가 SLT 후보가 된다. 단 SLT는 **분 단위 시간 예산·자동 판정·결정적(deterministic) 결과**가 필요 → validation 테스트를 그대로 쓰지 않고 "짧고, 스스로 판정하고, 재현성 있게" 다듬는다 [추정: 일반 관행].

### 2.9 FPGA vs 실리콘 — 발목 잡는 차이

| 차이 | FPGA/HAPS에서 | 실리콘에서 | 미리 할 일 |
|---|---|---|---|
| 클럭 속도·비율 | 수십 MHz, 블록 간 클럭 비율이 실제와 다를 수 있음 | GHz, 실제 비율 | 타임아웃·폴링·delay를 **틱이 아닌 시간 단위**로, 플랫폼 config에 클럭 값 |
| 고속 PHY (PCIe/Ethernet/MIPI SerDes) | 없음 → FPGA 벤더 PHY나 speed adaptor로 대체 [9] | 실제 아날로그 PHY, equalization, 캘리브레이션 | PHY 초기화·캘리브레이션 경로는 실리콘 전용 테스트로 따로 준비 |
| DDR | FPGA의 DDR4 하드 컨트롤러 + 메모리 모델로 LPDDR5 등을 흉내 [9] | 실제 DDR PHY + **training** | training·margin 테스트는 실리콘에서 처음 → 벤더 PHY 툴·로그 포맷 미리 확보 ([BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)) |
| PLL·클럭 생성 | FPGA MMCM/고정 클럭으로 대체 | 실제 PLL lock, 분주, spread spectrum | PLL lock 폴링·bypass 경로 테스트 ([BV04](2026-11-01_BV04_clocks_osc_pll_refclk.html)) |
| 전원 도메인·power gating | 보통 항상 켜짐 (전원 스위치 모델 없음) | 실제 power island, retention, 순서 | 도메인 on/off 순서, isolation, retention 테스트 |
| 리셋 타이밍 | 이상적인 리셋 | 리셋 해제 타이밍·순서가 시뮬레이션과 다름 [1] | reset 값 테스트·reset reason 확인 |
| 아날로그 블록 | 없음 (온도 센서·ADC·eFuse·PVT 모니터) | 있음 | 퓨즈 읽기·센서 값 sanity 테스트 |
| 보안·퓨즈 | 개발 모드 고정 | 퓨즈 상태에 따라 JTAG lock·boot 인증 | 디버그 퓨즈 상태 확인 절차 |
| 파티셔닝 | 여러 FPGA로 나눠 칩 간 신호 지연 | 없음 | FPGA에서만 나는 타이밍 버그를 실리콘 버그로 착각하지 않기 |
| 부분 RTL | 일부 블록이 stub/빠진 빌드 | 전체 | 테스트 목록에 "FPGA 미지원" 플래그 |
| 환경 | 실온·고정 전압 | PVT 전부 | 마진 테스트는 실리콘에서 새로 |

- 정리 한 줄: **디지털 로직의 기능은 FPGA에서 대부분 증명되고, 실리콘에서 새로 증명할 것은 "아날로그·타이밍·전원·속도·환경"** [13].

### 2.10 보드 리비전 · 보드 종류

| 보드 단계 | 목적 [10][11] | 실리콘과의 관계 [추정: 일반 관행] |
|---|---|---|
| **Validation / bring-up board** (EVB, CRB, socketed) | 칩 자체를 검증; 측정·디버그 편의 최우선 (TP, shunt, 점퍼, 0 Ω 옵션, 디버그 헤더) | A0가 처음 올라감; 칩 교체·비교 |
| **EVT** (Engineering Validation Test) | 제품 보드 설계가 의도대로 기능하나 (기능·성능 확인, 설계 실현성) | 보통 A0/A1 실리콘 |
| **DVT** (Design Validation Test) | 설계 확정, 규격·인증(EMC 등) 시험 가능 수준, 신뢰성 | 수정된 stepping으로 넘어가는 시점이 많음 |
| **PVT** (Production Validation Test) | 양산 라인·공정이 같은 품질로 반복 생산하나 | 양산 stepping |
| **MP** (Mass Production) | 양산 | |

- **소켓 보드가 왜 필요한가**: 같은 보드에 칩만 바꿔 끼워 **칩 vs 보드**를 가르고(swap test), skew lot 칩으로 process corner를 재고, 귀한 A0를 재사용. 단점: 소켓 접촉 저항·인덕턴스로 고속 신호·전원 품질이 납땜 보드와 다름 → 마진 측정은 납땜 보드로도 확인 [추정].
- **보드 리비전 변경 시 할 일**: rev A↔B 변경 목록(ECO, 부품 대체) 읽고 → 영향 받는 테스트만이 아니라 **전원·클럭 기본 테스트는 항상 재실행** → [BV13 #12](2026-11-01_BV13_board_debug_scenarios.html).

### 2.11 Silicon bug vs board bug vs SW bug — 가르는 기준

| 증거 | 실리콘 쪽으로 기움 | 보드 쪽으로 기움 | SW 쪽으로 기움 |
|---|---|---|---|
| **칩 swap** (소켓/rework) | 실패가 **칩을 따라감** | 실패가 **보드에 남음** | 모든 칩·보드에서 동일 |
| **보드 swap** | 모든 보드에서 같은 칩 실패 | 특정 보드(들)만 | 모든 보드 |
| **FW/SW 버전** | 버전 무관 | 버전 무관 | 특정 커밋부터 (bisect) |
| **조건 의존성** | 특정 전압/온도/주파수 corner, 특정 stepping | 특정 rail·레이아웃 위치·부품 lot | 타이밍과 무관, 로직 경로 의존 |
| **RTL 시뮬레이션 재현** [1] | 재현되면 RTL 버그 | 재현 안 됨 | 재현 안 됨 (SW 동작이 다를 수 있음) |
| **측정** | 내부 상태 이상 (debug 레지스터·trace) | rail droop·클럭 jitter·SI 문제가 스코프에 보임 | 측정 정상, 레지스터 설정 오류 |

- 실전 순서는 [BV14](2026-11-01_BV14_board_validation_plan_automation.html): 재현성 → swap → bisect → 조건 스윕 → 측정 증거 → 리포트.

### 2.12 실리콘 오기 전에 준비해 둘 것 (Readiness checklist)

| 분야 | 준비물 | 완료 기준 |
|---|---|---|
| 테스트 FW | bring-up FW (SRAM 전용, UART 최소 드라이버, test registry, PASS/FAIL 포맷) | FPGA에서 전 테스트 회귀 green |
| HAL / 플랫폼 config | 클럭 값·타임아웃·PHY 유무·메모리 맵을 플랫폼별 config로 분리 | `PLATFORM=fpga` ↔ `PLATFORM=si` 빌드 둘 다 생성 |
| 메모리 맵·레지스터 | 레지스터 정의(IP-XACT/SystemRDL에서 생성 [추정]), reset 값 테스트 | 실리콘 레지스터 dump와 diff 가능 |
| 디버그 | JTAG 스크립트(Trace32/OpenOCD): connect, halt, SRAM load, reset reason 읽기 | FPGA에서 스크립트 동작 확인 |
| 보드 | 회로도 기반 bring-up 체크리스트, 예상 rail·전류 표 ([BV02](2026-11-01_BV02_schematic_layout_reading.html)) | HW 엔지니어 리뷰 완료 |
| 실리콘 전용 테스트 | PLL lock, PHY calibration, DDR training 로그 파서, 퓨즈·온도 센서 읽기 | 코드 리뷰 + 가능한 부분 모델 검증 |
| 자동화 | 보드 팜(전원 relay, UART 서버, 결과 DB), 회귀 러너 | FPGA 보드로 야간 회귀 가동 |
| 장비 | scope·전류 프로브·PSU·챔버 예약, SCPI 스크립트 | 장비 원격 제어 동작 |
| 계획 | validation plan(영역·우선순위·합격 기준), 이슈 트래커, 보드 인벤토리 | 팀 합의 |
| 사람 | Day 1 당번, 에스컬레이션 경로(설계·보드·test 팀 연락처) | 공유됨 |

## 3. 보드에서 볼 것 · 잴 것 — 단계별 산출물과 역할

> 이 노트는 "지도"라 측정 상세는 각 노트로 연결. 여기서는 **각 단계에서 무엇이 나와야 다음으로 넘어가는가(산출물·exit criteria)** 와 **validation SW 엔지니어가 무엇을 하는가** 를 표로 고정한다.

### 3.1 Stage → deliverables 표

| 단계 | 입력 | 산출물 (deliverables) | 다음 단계로 넘어가는 기준 (exit) |
|---|---|---|---|
| Spec · architecture | 제품 요구사항 | 칩 spec, 메모리 맵, 전원·클럭·리셋 아키텍처 | spec 리뷰 완료 |
| RTL + DV | spec | RTL, DV 테스트벤치, 커버리지 리포트 | 커버리지 목표, 버그율 수렴 |
| Emulation / FPGA (pre-si) | RTL 스냅샷 | **bring-up FW, HAL, 테스트 스위트, 자동화, 회귀 결과**, FPGA 버그 리포트 | FPGA 회귀 green, 실리콘 전용 테스트 작성 완료 |
| Tape-out | sign-off | GDS, 마스크 | 타이밍·DRC·LVS sign-off |
| A0 도착 · ATE 선별 | 웨이퍼/패키지 | ATE pass 칩 + 칩 ID·lot 정보, 초기 scan/MBIST 결과 | 검증용 수량 확보 |
| Bring-up | A0 + 검증 보드 | **bring-up 체크리스트 결과, 보드별 상태표, rework 기록**, 첫 이슈 목록 | 부트·DDR·주요 IO 동작, 회귀 러너가 실리콘에서 돎 |
| 기능 validation | 안정적인 부트 | 테스트 계획 대비 실행·PASS율, 버그 리포트, 요구사항 추적표 | 계획 항목 실행 완료, 치명 버그 분류 완료 |
| 성능 validation | 기능 OK | 성능 리포트(이론 대비), 병목 분석 | 목표 대비 달성/차이 설명 |
| PVT 특성화 | skew lot, 챔버 | **shmoo·마진 리포트**, 동작 스펙 제안, AVS/binning 데이터 | 마진 기준 충족 or 수정 요청 |
| Errata · stepping | 버그 목록 | **errata 문서**, workaround 구현·검증, 다음 stepping 수정 목록 | 각 버그: fix/workaround/accept 결정 |
| Qualification | 최종 stepping | qual 리포트 (HTOL 등), 고장 분석 | 규격 통과 |
| Production (ATE + SLT) | 양산 stepping | ATE 프로그램, **SLT 프로그램**(워크로드·조건·판정), 수율 데이터 | 테스트 시간·커버리지·escape율 목표 |

### 3.2 Validation SW 엔지니어의 역할 — 단계별

| 단계 | 이 포지션이 하는 일 | 대표 결과물 | Don 경험 대응 |
|---|---|---|---|
| Spec | 테스트 가능성 리뷰: debug 레지스터·trace·chicken bit·reset reason·perf counter 요구 (DFD ask) | spec 코멘트, 테스트 계획 초안 | Solidigm 아키텍처 리뷰 `[숫자 채우기]` |
| DV 기간 | DV와 커버리지 중복 피하기, 시스템 레벨 시나리오 제안 | validation plan v0 | Solidigm RTL freeze 전 SoC verification (I2C/SPI/DMA/SRAM/DRAM) |
| FPGA / emulation | bring-up FW·HAL·테스트 스위트 작성, FPGA에서 디버그, RTL 버그 발견·리포트 | FW 저장소, FPGA 회귀, 버그 리포트 | Solidigm Cortex-R8/R82/M0+ FPGA bring-up, PCIe/NVMe IP |
| Tape-out 전후 | 실리콘 전용 테스트, 회로도 리뷰·체크리스트, 장비·보드 팜 준비 | readiness 체크리스트 (§2.12) | SK hynix 테스트 플랫폼 SDK |
| Bring-up | 전원·클럭·리셋·JTAG·부트·DDR·IO를 HW 엔지니어와 함께 살림, 문제 분류 | bring-up 로그, 이슈 트래커 | Apple 무선 칩셋 첫 보드 bring-up `[숫자 채우기]` |
| 기능·성능 | 테스트 실행 자동화, 실패 triage (칩/보드/SW), 성능 튜닝 | 결과 DB, 대시보드, 버그 | Solidigm silicon validation 팀과 perf 튜닝 |
| PVT | 챔버·전압 sweep 자동화, shmoo 생성, 마진 리포트 | shmoo 리포트 | SK hynix chamber 신뢰성 플랫폼, shmoo |
| Errata | workaround 구현·통계적 검증, 회귀 고정 | errata 엔트리 기여 | |
| Qual | 스트레스 FW, 모니터링 | qual FW | |
| SLT | validation 테스트를 짧고 자기 판정형으로 다듬어 SLT로 이관, 상관관계(validation 실패 ↔ SLT 실패) 분석 | SLT 테스트 콘텐츠 | SK hynix factory test-node 설계 `[숫자 채우기]` |

### 3.3 A0 첫 4주 타임라인 `[예시]`

```text
 [예시 — 실제 회사 일정 아님, 흐름 감각용]
 Week 0 (도착 전)  readiness 체크리스트 §2.12 완료, 보드 육안 검사·rail 저항 측정(칩 없는 보드/소켓 보드)
 Day 1            전원·순서 → 클럭 → 리셋 → JTAG IDCODE → halt → SRAM read/write → UART "hello"
 Day 2–3          BootROM 경로(boot mode strap), SRAM 테스트 FW 전체 실행, 레지스터 reset 값 diff
 Week 1           DDR training (벤더 툴), memtest; 첫 이슈 triage 회의 (칩/보드/SW 분류)
 Week 2           IO 하나씩 (I2C/SPI/UART/GPIO → Ethernet → PCIe → MIPI); 회귀 러너 실리콘 연결
 Week 3           Linux/RTOS 부팅 (product SW와 협업), 기능 validation 본격 시작
 Week 4           기능 회귀 야간 실행, 첫 PVT 스윕(실온 ± 전압), errata 후보 목록 v0
```

### 3.4 Bring-up 상태표 `[예시]` — 매일 스탠드업에서 보는 것

| 보드 SN | 보드 rev | 칩 ID / lot | 전원 | 클럭 | JTAG | UART | DDR | PCIe | 메모 |
|---|---|---|---|---|---|---|---|---|---|
| EVB-001 | A | A0 / lot X-TT | ✅ | ✅ | ✅ | ✅ | 🟡 | ❌ | DDR ch1 read-eye 마진 작음 |
| EVB-002 | A | A0 / lot X-TT | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 | PCIe Gen3에서 downtrain |
| EVB-003 | A (rework R123) | A0 / lot X-SS | ✅ | ✅ | ❌ | ❌ | | | VTref 0 V → 디버그 헤더 rail 미실장 (DNP) |

- 해석: 003은 칩이 아니라 **보드 rework/DNP 문제**(디버그 헤더 전원이 0 Ω 미실장) — 회로도 옵션 확인으로 해결 ([BV02](2026-11-01_BV02_schematic_layout_reading.html)). 002의 PCIe downtrain은 칩·보드·SW 어느 쪽인지 아직 미정 → 001과 칩 swap으로 가른다.

### 3.5 Errata 엔트리 예시 `[예시]`

```text
 [예시 — 가상의 칩, 실제 문서 아님]
 ID:           ERR-031
 Title:        DMA engine may drop the last descriptor when ring wraps at 4 KiB boundary
 Problem:      If a descriptor ring of N entries ends exactly at a 4 KiB boundary and the
               engine is restarted within 2 µs of the wrap, the last descriptor is skipped.
 Implication:  One buffer is never filled; driver times out. No data corruption observed.
 Workaround:   Driver allocates rings that do not end at a 4 KiB boundary (pad 64 B), or
               sets chicken bit DMA_CTRL.WRAP_SAFE=1 (≈1% throughput loss).
 Found by:     validation stress test dma_ring_wrap (1/3,000 runs at A0, 0/10,000 with workaround)
 Status:       A0, A1 affected. Fixed in B0.
```

- 포인트: **조건(언제)** · **영향(얼마나 나쁜가)** · **workaround와 비용** · **재현률·검증 통계** · **어느 stepping에서 고치나**. validation SW 엔지니어는 "Found by"와 workaround 검증 줄을 채우는 사람.

## 4. 꼬리 질문 체인

### 체인 A — "Walk me through how you'd bring up a brand-new SoC on a new board." (BV00 원본)

Q → 꼬리1 "What would you have ready before the silicon arrives?" → 꼬리2 "How do you reuse the FPGA work?" → 꼬리3 "What's different between FPGA and silicon that bites you?" → 꼬리4 "Something fails on day one — how do you tell chip from board from software?"

<details>
<summary>A-Q. Walk me through how you'd bring up a brand-new SoC on a new board.</summary>

- **EN**: "I go bottom-up and don't trust a symptom at one layer until the layer below is proven. Before power, a visual check and rail-to-ground resistance. Then power with a current-limited supply — every rail at the right voltage, in the right order, with sane current. Then clocks — oscillator running, PLLs locked. Then reset release and strap values. Then JTAG: read IDCODE, halt a core, read and write SRAM. Then load a small test from SRAM and get UART output. Only then the boot flow, DDR training and memory test, and each interface from simple connectivity to full speed. Every step has a written pass criterion, and every measurement and rework goes into a log against the board serial number."
- 한국어 핵심: 아래 계층부터, 각 단계 통과 기준, 전원→클럭→리셋→JTAG→SRAM/UART→부트→DDR→IO, 보드 SN별 기록.
</details>

<details>
<summary>A-꼬리1. What would you have ready before the silicon arrives?</summary>

- **EN**: "Bring-up firmware that runs from on-chip SRAM with a minimal UART driver and a test registry, already passing on the FPGA. A HAL with every platform difference in a config — clock rates, timeouts, which PHYs exist. JTAG scripts for connect, halt, load and reading reset reason. Silicon-only tests that the FPGA couldn't exercise — PLL lock, PHY calibration, DDR training log parsing, fuses and temperature sensors. A bring-up checklist from the schematic with expected rail voltages and currents and where to probe. And the automation — power control, UART capture, a results database — already running nightly on FPGA boards."
- 한국어 핵심: SRAM FW + HAL config + JTAG 스크립트 + 실리콘 전용 테스트 + 회로도 체크리스트 + 자동화(FPGA에서 이미 가동).
</details>

<details>
<summary>A-꼬리2. How do you reuse the FPGA work?</summary>

- **EN**: "The goal is the same binary, or at least the same source, on both — only the platform config changes. Tests are written against a thin HAL, so the FPGA build and the silicon build differ in clock values, timeouts and which tests are enabled. On day one I run the exact suite that was green on FPGA, so any failure is a difference between FPGA and silicon or board, not a bug in my test. The FPGA regression results become the baseline: same test, same expected values, and now I'm debugging only the delta."
- 한국어 핵심: 같은 소스/바이너리 + 플랫폼 config만 교체 → FPGA green = 기준선, 실리콘에선 차이만 디버그.
</details>

<details>
<summary>A-꼬리3. What's different between FPGA and silicon that bites you?</summary>

- **EN**: "Speed is the obvious one — silicon runs ten to a hundred times faster, so any timeout or delay written in loop iterations instead of time breaks, and race conditions the slow FPGA hid show up. Then everything analog the FPGA didn't have: the real SerDes PHYs for PCIe and Ethernet with calibration and equalization, the DDR PHY and training, real PLLs, temperature sensors and fuses. Power domains are usually always-on on the FPGA, so power gating and retention are tested for the first time on silicon. Reset timing differs from the ideal. And partitioning across FPGAs can create timing bugs that don't exist in silicon, so I don't port FPGA-only issues as silicon bugs."
- 한국어 핵심: 속도(타임아웃·레이스) · 아날로그 PHY/DDR PHY/PLL/센서/퓨즈 · 전원 도메인 · 리셋 타이밍 · FPGA 파티셔닝 착시.
</details>

<details>
<summary>A-꼬리4. Something fails on day one — how do you tell chip from board from software?</summary>

- **EN**: "By experiment, not opinion. Swap the chip — on a socketed board that's easy — and see whether the failure follows the chip or stays with the board. Run the same firmware on another board and the known-good FPGA build of the test. Bisect firmware versions if it's software-shaped. Sweep voltage and temperature: a failure that moves with conditions points to margin, which is silicon or board, not logic. And measure the lower layers — rails, clocks, reset — on the failing board. I write it up with the evidence so the right team takes it without a debate."
- 한국어 핵심: 칩 swap · 보드 swap · FW bisect · 조건 스윕 · 하위 계층 측정 → 증거로 리포트.
</details>

### 체인 B — "What's the difference between silicon validation and design verification?" (BV00 원본)

Q → 꼬리1 "Why do we still find bugs post-silicon if DV had 100% coverage?" → 꼬리2 "How do you get a silicon bug fixed — what goes into an erratum?" → 꼬리3 "What's the difference between A1 and B0?"

<details>
<summary>B-Q. What's the difference between silicon validation and design verification?</summary>

- **EN**: "Design verification asks whether the RTL matches the spec, in simulation, emulation or formal, with full visibility but very slow execution. Silicon validation asks whether the real chip, on a real board, with real software, works across real conditions — voltage, temperature, process — with very limited visibility but full speed. DV finds most logic bugs; validation finds what DV can't model or can't reach in time: analog and timing effects, long software sequences, system interactions, and margins."
- 한국어 핵심: DV = RTL vs spec (가시성↑ 속도↓), validation = 실물 + 실제 SW + 실제 조건 (속도↑ 가시성↓).
</details>

<details>
<summary>B-꼬리1. Why do we still find bugs post-silicon if DV had 100% coverage?</summary>

- **EN**: "Coverage only measures what the coverage model defines, so 100% means we hit everything we thought of, not everything that can happen. Simulation is orders of magnitude slower than silicon, so the deep sequences — an OS running for hours, rare interleavings — are never reached. And some things aren't in the RTL model at all: parasitic delays, voltage droop, crosstalk, analog PHY behavior, and corners of voltage and temperature. When we find one, I try to reproduce it in RTL simulation — if it reproduces, it's a logic bug and also a coverage hole to fix; if not, it's a silicon effect like a timing path at a corner."
- 한국어 핵심: 커버리지는 "생각한 것"만 · 속도 차이로 깊은 시퀀스 미도달 · 모델에 없는 물리 효과 · RTL 재현으로 분류.
</details>

<details>
<summary>B-꼬리2. How do you get a silicon bug fixed — what goes into an erratum?</summary>

- **EN**: "First a solid bug report: exact conditions, reproduction rate, the minimal test, evidence from debug registers or traces, and ideally a reproduction in RTL simulation so the designer can see it. Then the design team decides: fix in the next stepping, provide a workaround, or accept it. The erratum documents the problem and the conditions, the implication for the system, the workaround and its cost, and the steppings affected and fixed. My part is to prove the workaround actually works with enough runs, and to lock the test into regression so we notice if it ever comes back."
- 한국어 핵심: 재현률·최소 테스트·증거·RTL 재현 → fix/workaround/accept → errata(문제·영향·workaround·stepping) → workaround 통계 검증·회귀 고정.
</details>

<details>
<summary>B-꼬리3. What's the difference between A1 and B0?</summary>

- **EN**: "By the usual convention, the number changes for a metal-only revision and the letter changes for a base-layer revision. A metal-only fix rewires existing transistors, often spare cells placed for that purpose, so only a few masks are remade — it's faster and cheaper. A base-layer change means new transistors, a full mask set and a longer cycle. So a fix that needs new logic beyond the spare cells goes to B0, and small wiring fixes go to A1."
- 한국어 핵심: 숫자 = metal-only (spare cell 재배선, 마스크 몇 장), 글자 = base layer (전체 마스크, 느리고 비쌈).
</details>

### 체인 C — "What's SLT and how is it different from ATE testing?" (BV00 원본)

Q → 꼬리1 "What would you put in an SLT program for an AI SoC?" → 꼬리2 "How do validation tests turn into SLT content?" → 꼬리3 "SLT fails a chip that passed ATE. Now what?"

<details>
<summary>C-Q. What's SLT and how is it different from ATE testing?</summary>

- **EN**: "ATE testing is structural: the tester drives scan and BIST patterns generated against fault models — stuck-at, bridging, delay — so you get measurable fault coverage in seconds. System-level test puts the chip in a board-like socket and runs real workloads — boot the OS, run applications and stress — for minutes. It catches what structural tests miss: interactions across clock and power domains, analog and asynchronous interfaces, marginal timing at low voltage or during DVFS, temperature-dependent defects. It has no formal coverage metric and is harder to diagnose, so it complements ATE rather than replacing it."
- 한국어 핵심: ATE = 결함 모델 기반 scan/BIST, 커버리지 측정, 초 단위 · SLT = 실제 워크로드, 분 단위, 상호작용·마진 결함, 커버리지 지표 약함 → 보완 관계.
</details>

<details>
<summary>C-꼬리1. What would you put in an SLT program for an AI SoC?</summary>

- **EN**: "A short boot to a minimal OS or test executive, then workloads that hit each major block at realistic intensity together: the NPU running a representative inference with a known-answer check, the CPU cluster under stress, LPDDR at full bandwidth with data checking, camera input through the ISP using a test pattern, PCIe and Ethernet in loopback. I'd run it at a voltage guard-band below nominal and at temperature so marginal parts fail here and not in the car, read on-chip temperature and voltage monitors, and keep every check self-judging with a single pass/fail and a failure code so the handler can bin the part. The whole thing has to fit a time budget of minutes and be deterministic."
- 한국어 핵심: 부팅 → NPU known-answer + CPU 스트레스 + LPDDR 데이터 체크 + ISP 테스트 패턴 + PCIe/ETH loopback, 저전압 guard-band·고온, 자기 판정·실패 코드, 분 단위·결정적.
</details>

<details>
<summary>C-꼬리2. How do validation tests turn into SLT content?</summary>

- **EN**: "Validation tests are written to find bugs and give rich diagnostics; SLT tests are written to screen parts quickly and repeatably. So I take the validation tests that actually caught marginal or interaction failures, strip them down to a fixed duration, make them deterministic and self-checking, remove anything that depends on lab equipment, and add a failure code. Then I correlate: run the candidate content on parts that failed in validation or the field and on known-good parts, to show it separates them."
- 한국어 핵심: 실패를 실제로 잡은 테스트 → 시간 고정·결정적·자기 판정·장비 의존 제거·실패 코드 → 불량/정상 칩으로 상관관계 증명.
</details>

<details>
<summary>C-꼬리3. SLT fails a chip that passed ATE. Now what?</summary>

- **EN**: "First make sure it's the chip: re-test it in another SLT socket and on a validation board, because socket contact and board issues are common. If it follows the chip, capture the failure signature — which test, which block, at what voltage and temperature — and run it on a characterization setup: a shmoo usually shows whether it's a marginal part or a hard defect. Then hand it to failure analysis and to the test team to see whether an ATE pattern can be added to catch it earlier and cheaper. If many parts show the same signature, it may be a design marginality, and that goes back to validation and design."
- 한국어 핵심: 소켓/보드 탓 배제 → 시그니처·조건 → shmoo로 마진 vs 하드 결함 → FA·ATE 패턴 추가 → 다수면 설계 마진 이슈.
</details>

### 체인 D — "What are EVT, DVT and PVT? How do board revisions interact with silicon steppings?" (추가)

Q → 꼬리1 "Why bring up A0 on a validation board instead of the product board?" → 꼬리2 "Board rev B arrives. What do you re-test?"

<details>
<summary>D-Q. What are EVT, DVT and PVT?</summary>

- **EN**: "They're hardware build phases for the board or product. EVT — engineering validation — proves the design works functionally. DVT — design validation — is a design-frozen build mature enough for certification and reliability testing. PVT — production validation — proves the factory line builds it consistently at volume. Silicon steppings run on their own track, so typically early silicon goes on validation boards and EVT, and the fixed stepping lines up with DVT or PVT. I'd be careful with the word PVT, because in characterization it means process, voltage and temperature corners."
- 한국어 핵심: EVT 기능 · DVT 설계 확정·인증 · PVT 양산 라인 · 칩 stepping과 별도 트랙 · PVT 동음이의 주의.
</details>

<details>
<summary>D-꼬리1. Why bring up A0 on a validation board instead of the product board?</summary>

- **EN**: "Because on day one the question is whether the chip works, and I want to measure everything. A validation board has test points on every rail, current-sense resistors, jumpers and zero-ohm options to isolate blocks, debug headers, often a socket so I can swap chips, and configurable clocks and straps. A product board is optimized for size and cost, so when something fails I can't tell whether it's the chip or the board. Once the chip is proven on the validation board, the product board's failures are much easier to attribute."
- 한국어 핵심: 측정·격리·칩 교체 가능 → 칩 문제를 보드 문제와 분리. 제품 보드는 크기·비용 최적화라 디버그 어려움.
</details>

<details>
<summary>D-꼬리2. Board rev B arrives. What do you re-test?</summary>

- **EN**: "I start from the change list — every ECO and part substitution between rev A and B — and map each change to the tests it can affect. But power, clocks, reset and the basic bring-up checklist always re-run on every new revision, because a changed regulator or a moved part can affect everything above it. Then the full regression on several rev B boards, compared side by side with rev A results, so any new failure is clearly tied to the revision."
- 한국어 핵심: rev 변경 목록 → 영향 테스트 매핑 + 전원·클럭·리셋·기본 체크리스트는 항상 재실행 → 다수 보드 전체 회귀, rev A와 비교.
</details>

### 체인 E — "How do you measure how complete your validation is?" (추가 · Staff 시그널)

Q → 꼬리1 "How do you prioritize when the schedule is cut in half?"

<details>
<summary>E-Q. How do you measure how complete your validation is?</summary>

- **EN**: "Against requirements, not against how many tests I have. Every spec item and feature maps to tests in a traceability table, each with a numeric pass criterion and the conditions it must pass at — nominal, voltage and temperature corners, number of boards and chips. Completeness is the fraction of items executed and passing at their required conditions, plus statistical confidence for the intermittent ones. I also track the bug discovery rate — when it flattens across a new stepping or board rev and the escape analysis shows no new categories, that's the other signal we're close."
- 한국어 핵심: 요구사항 추적표 × 조건 × 합격 기준, 통계 신뢰도, 버그 발견률 수렴.
</details>

<details>
<summary>E-꼬리1. How do you prioritize when the schedule is cut in half?</summary>

- **EN**: "By risk and by what's hardest to fix later. First anything that would force a base-layer stepping — those need to be found earliest because the fix takes longest. Then anything on the critical path for the product — boot, DDR, camera input, the main data path. Then new IP over reused IP that's proven in a previous chip. Things with good software workarounds or low customer impact move later. I make the cut explicit and written, so everyone knows what is not covered."
- 한국어 핵심: 늦게 고치기 비싼 것(base layer) 먼저 · 제품 critical path · 신규 IP > 재사용 IP · 빠진 범위 명시.
</details>

## 5. Don 경험 연결

- **Pre-si → post-si handoff (핵심 카드)**: "At Solidigm I brought up Cortex-R8, R82 and M0+ subsystems on FPGA before RTL freeze, along with the PCIe/NVMe subsystem IP, and wrote SoC-level verification for I2C, SPI, DMA, SRAM and DRAM. Because those tests already ran on the prototype, when silicon came we could hand them over and spend our time on the silicon-specific differences — I then worked with the silicon validation team on performance tuning. [숫자 채우기: FPGA 플랫폼 이름, 기간, 발견한 RTL 버그 수]"
- **Characterization / PVT**: "At SK hynix I automated a chamber-based reliability test platform and did high-speed interface reliability work on PCIe Gen3 to Gen5 including shmoo — so I've seen how characterization data turns into margin decisions. [숫자 채우기: 온도 범위, 보드/샘플 수, 자동화로 줄인 시간]"
- **Triage 감각**: "At Apple, integrating wireless chipsets, I root-caused PCIe, I2C, SPMI and RFFE issues with a scope and protocol analyzer — the core skill was deciding with evidence whether it was our silicon, the board, or software."
- **솔직하게 구분**: 첫 실리콘(A0) 도착 시 직접 한 일 vs 옆에서 본 일은 BV00 §4 체크리스트대로 과장 없이 → "I owned the pre-silicon side and the handoff; on silicon day one my role was [숫자 채우기]."

## 6. 실습 / 코드

이 노트는 지도라서 실행 코드는 없다. 대신 **실행 검증된 관련 코드**와 **혼자 해 볼 연습 레시피**.

- ✅ 실행 검증됨: [BV02 bringup_checklist.py](onsitePrep/bv02/bringup_checklist.py) — 회로도에서 읽은 전원 트리 CSV → 의존성 순서·FB 분압 검산·DFD 경고·Day-1 체크리스트 표 (실행 결과는 [BV02 §6](2026-11-01_BV02_schematic_layout_reading.html)).
- `[미실행]` 연습 1 — **Readiness 체크리스트 셀프 리뷰**: §2.12 표를 Solidigm 때 실제 프로젝트에 대입해 "있었던 것 / 없었던 것 / 없어서 고생한 것"을 한 줄씩 적기 → 면접에서 "What would you do differently?" 답 재료.
- `[미실행]` 연습 2 — **FPGA↔실리콘 차이 파라미터 목록**: 자기가 쓴 bare-metal 테스트 하나를 골라 클럭·타임아웃·폴링 횟수·PHY 의존을 `platform_cfg` 구조체로 뽑아내는 리팩터를 종이에 설계 (C 구조체 필드 이름까지).
- `[미실행]` 연습 3 — **Errata 엔트리 쓰기**: LP 트랙 디버그 시나리오([LP15](2026-10-31_LP15_debug_scenarios.html)) 하나를 골라 §3.5 형식(Problem · Implication · Workaround · Found by · Status)으로 써 보기.

## 7. 자주 틀리는 것

1. **"Validation = 테스트 많이 돌리기"** → 아니다. 요구사항 추적 + 조건(PVT) + 숫자 기준 + 통계 신뢰도. "몇 개 돌렸다"가 아니라 "무엇을 어떤 조건에서 어느 확신으로 증명했나".
2. **Verification과 validation 혼용** → DV는 RTL vs spec, validation은 실물 vs 쓰임새. 면접에서 정확히 구분해 말하기.
3. **"PVT" 동음이의** → characterization의 Process-Voltage-Temperature vs 보드 빌드 Production Validation Test. 문맥을 붙여 말하기.
4. **SLT를 "ATE의 대체"로 설명** → 보완이다. 구조 테스트는 커버리지·진단성, SLT는 실사용 현실성 [3][4].
5. **A0 버그를 전부 "칩 버그"로 보고** → 첫 실리콘 시기엔 보드·SW·소켓·측정 셋업 문제가 섞여 있다. swap·bisect 증거 없이 설계 팀에 넘기면 신뢰를 잃는다.
6. **FPGA에서 green이면 실리콘에서도 된다고 가정** → 속도(타임아웃·레이스), 아날로그 PHY·DDR PHY·PLL·전원 도메인은 실리콘에서 처음 검증된다.
7. **FPGA 파티셔닝 버그를 실리콘 버그로 이관** → FPGA 간 신호 지연 등 프로토타입 고유 문제는 실리콘에 없다. 반대로 FPGA에서만 안 보이는 건 실리콘에서 처음 나온다.
8. **Stepping 이름을 아무렇게나** → 관례상 숫자 = metal-only, 글자 = base layer [6]. 회사마다 예외가 있으니 "by the usual convention"이라고 말하기.
9. **Workaround를 한 번 돌려 보고 "고쳐졌다"** → 원래 실패율 기준으로 충분한 무실패 횟수(rule of three, [LP14](2026-10-31_LP14_validation_methodology.html)) + 회귀 고정.
10. **Readiness를 SW만 생각** → 보드 체크리스트, 장비 예약, 보드 팜, 에스컬레이션 경로, 칩 배분까지가 준비다.

## 8. 출처

1. ChipVerify — Post-Silicon Validation (bring-up 순서, silicon vs simulation 차이, failure correlation, escape 피드백): https://chipverify.com/verification/post-silicon-validation
2. Mitra, Seshia, Nicolici — "Post-Silicon Validation: Opportunities, Challenges and Recent Advances", DAC 2010 (정의·가시성 문제): https://people.eecs.berkeley.edu/~sseshia/pubs/b2hd-mitra-dac10.html
3. "Exploring the Mysteries of System-Level Test" (arXiv 2103.06656) — 구조/기능/SLT 비교표, SLT가 잡는 결함, SLT 프로그램 구성: https://ar5iv.labs.arxiv.org/html/2103.06656
4. Teradyne — SLT in the AI Era: Validating Reliability at Scale (2026-08): https://teradyne.com/2026/08/24/slt-in-the-ai-era-validating-reliability-at-scale/
5. Semiconductor Engineering — The Future of Fault Coverage in Chips (SLT 커버리지·진단 한계): https://semiengineering.com/the-future-of-fault-coverage-in-chips
6. Wikipedia — Stepping level (글자 = base layer, 숫자 = metal layer, stepping 이유): https://en.wikipedia.org/wiki/Stepping_level
7. Intel PCN 102723 (IOP321 A0 → B0, errata/workaround는 Specification Update로 공개): https://cdrdv2-public.intel.com/796958/PCN102723-00.pdf
8. Synopsys — FPGA Prototyping for Scaled-Out Chip Design & Validation (실리콘 대비 1–2 자릿수, Android 부팅 300억 사이클 예): https://www.synopsys.com/blogs/chip-design/fpga-prototyping-scaled-out-chip-design-validation.html
9. S2C — FPGA Prototyping in Practice: Addressing Peripheral Connectivity Challenges (PHY 부재, speed adaptor, 메모리 모델): https://www.s2cinc.com/resources/lit/en/wp/fpga-prototyping-in-practice-addressing-peripheral-connectivity-challenges.pdf
10. OpenBOM — EVT vs DVT vs PVT: https://openbom.com/blog/evt-vs-dvt-vs-pvt-understanding-the-stages-of-product-development
11. Fictiv — EVT/DVT/PVT 단계 설명: https://www.fictiv.com/?p=1151
12. Semiconductor Engineering — Transforming Silicon Bring-Up (shift-left, pre-si 테스트 재사용, on-chip 모니터, 디버그 오버헤드): https://semiengineering.com/transforming-silicon-bring-up
13. Design & Reuse — T&VS: FPGA 프로토타입 검증 → 첫 실리콘 직접 검증 단계 사례: https://www.design-reuse.com/blog/54072-t-vs-provides-solution-to-post-silicon-validation/
14. ECO in VLSI (metal-only ECO, spare cell, 마스크 2–4장): https://akiitr.is-a.dev/asic/asic/ECO

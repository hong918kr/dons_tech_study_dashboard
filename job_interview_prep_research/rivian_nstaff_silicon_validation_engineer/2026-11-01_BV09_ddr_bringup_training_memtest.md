# BV09 · DDR/LPDDR bring-up · training · 메모리 테스트 (March C-)
<!-- theme: pastel -->

> **묶음**: D 메모리·인터페이스 · **우선**: P0 (도메인 질문 "DDR fails on some boards / at hot" + 코딩 라운드 "Write a memory test" 겸용) · **선행 노트**: [BV08 부트 흐름 · bring-up FW](2026-11-01_BV08_boot_flow_bringup_firmware.html) · [BV05 리셋·strap](2026-11-01_BV05_reset_strap_boot_mode.html) · [BV04 클럭](2026-11-01_BV04_clocks_osc_pll_refclk.html) · **다음 노트**: [BV10 보드 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) · **관련**: [BV12 SI/PI·margining·shmoo](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [BV13 디버그 시나리오](2026-11-01_BV13_board_debug_scenarios.html) · [LP03 PCIe 링크 트레이닝 (비교용)](2026-10-31_LP03_link_training_ltssm.html) · [LP08 DMA·캐시 일관성](2026-10-31_LP08_dma_iommu_coherency.html) · [V1 March 코딩 드릴](hmScreenPrep/problems/V1_march_memtest/index.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html) · **코드**: [bv09/memtest_matrix.c](onsitePrep/bv09/memtest_matrix.c) · [실행 결과](onsitePrep/bv09/memtest_matrix.out) · [bv09/march_cminus_whiteboard.c](onsitePrep/bv09/march_cminus_whiteboard.c) · **작성**: 2026-11-01

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"DDR training이 실패한다 / 고온에서만 / 10장 중 2장만 실패한다 — 어떻게 디버그?"**, **"메모리 테스트를 짜 봐라 (data bus · address bus · March C-)"**, **"write leveling이 뭐고 왜 필요?"**, **"ECC는 어떻게 검증?"**.
- **외울 것 ①: 3층 구조** — memory **controller**(디지털: 명령 스케줄링·refresh·ECC·주소 매핑) ↔ DFI ↔ **PHY**(아날로그: IO·delay line·VREF·training 엔진) ↔ PCB ↔ **DRAM**. training은 PHY가 "보드 배선 지연"을 측정해서 지연값을 맞추는 과정.
- **외울 것 ②: init/training 순서** — 전원·클럭 안정 → RESET_n 해제 → CKE → **ZQ 캘리브레이션**(임피던스) → **MR(mode register) 쓰기** → **CA training**(LPDDR4/DDR5) → **write leveling**(CK vs DQS) → **read gate**(DQS 언제 열까) → **read eye/deskew** → **write eye/deskew** → **VREF training**(2D) → 마지막 memtest. **어느 단계가 실패했는지 = 어느 신호 그룹이 문제인지**.
- **외울 것 ③: 테스트 순서 = 진단 순서** — **data bus(walking 1s, 한 주소) → address bus(2의 거듭제곱 주소) → device/March C-(모든 셀)**. 순서를 어기면 오진한다: 이 노트의 실측 결과에서 data line short와 "칩 없음"이 address test에서는 **"A0 주소선 고장"으로 잘못 보였다** (§6).
- March C- = `{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}`, **10N**, SAF·TF·AF·CFin·CFid·CFst 검출. 단, **word 단위 메모리에서 0/1 배경만 쓰면 같은 word 안의 비트끼리 붙은 고장은 못 본다** → data background 6개로 반복하면 잡힘 (실측).
- 고온 실패 = **마진 부족 + refresh(고온에서 셀 누설↑ → refresh 2배 필요) + VREF/임피던스 drift**. 보드 일부만 실패 = **swap test**(DRAM/SoC/보드 중 무엇을 따라가나).
- **30초 영어 답 (headline: "DDR training fails on a new board")**: *"First I make sure it's really a DDR problem: rails including VDDQ and VPP are in spec and quiet, the reference clock is clean, reset and CKE timing are right, and the controller config matches the actual part — type, density, speed, and the board's byte and bit swizzle. Then I read which training step failed and on which byte lane, because that localizes the problem: CA training points at the clock and command bus, write leveling at the CK-to-DQS relationship on one lane, read gate at latency config or DQS, and a narrow eye on a few bits at routing or solder on those balls. I drop the frequency to see if it's marginal or hard, compare lanes and boards, and only after training passes do I run the bus tests and March C- with caches off."*

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 DRAM이 뭐고 왜 bring-up에서 제일 자주 막히나

> 비유: DDR = **SoC(사무실) 옆에 붙은 거대한 창고**. 창고에는 수십억 개 사물함(cell)이 있고, 사무실과 창고 사이에는 **수십 가닥의 컨베이어 벨트(데이터선 DQ)** 와 **주문서 벨트(명령/주소선 CA)** 가 깔려 있다. 문제는 이 벨트가 **초당 수십억 번** 물건을 나른다는 것. 벨트마다 길이가 몇 mm씩 다르면 물건이 도착하는 시점이 조금씩 어긋나고, 너무 빨라서 그 "조금"이 치명적이다. 그래서 창고를 쓰기 전에 매번 **"벨트마다 언제 물건을 내려놓고 언제 집어야 하는지" 시험 운행(training)** 을 해서 타이밍표를 만든다. 그다음 창고 사물함이 진짜로 물건을 잘 보관하는지 **전수 검사(memtest)** 를 한다.

- 왜 bring-up에서 자주 막히나:
  - SoC 안 SRAM은 칩 내부라서 "그냥 된다". DDR은 **칩 밖 PCB 배선 + 다른 회사 칩(DRAM)** 이 끼어 있어서 보드마다·온도마다·전압마다 결과가 달라진다.
  - 설정값(타이밍·임피던스·배선 지연·비트 순서)이 **수백 개**이고 하나만 틀려도 "가끔" 깨진다 — 가장 디버그하기 싫은 종류.
  - DDR이 안 되면 그 위의 모든 것(U-Boot, Linux, NPU 모델 로딩)이 안 된다. 그래서 bring-up FW는 **SRAM에서 돌면서 DDR을 초기화**한다 (→ [BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html)).

### 1.2 세 조각: controller · PHY · DRAM

> 비유: **controller = 창고 관리 사무실**(어떤 주문을 먼저 처리할지, 언제 청소(refresh)할지 결정), **PHY = 하역장 작업반**(벨트 속도·타이밍을 실제로 맞추는 손), **DRAM = 창고 건물**.

```text
  SoC 내부                                                    PCB             DRAM 패키지
 ┌──────────────────────────────┐   DFI    ┌──────────────────┐   배선   ┌──────────────────────┐
 │ Memory Controller (디지털)     │ (표준    │ DDR PHY (아날로그) │ ───────▶ │ DRAM die             │
 │  - 명령 큐 · 스케줄링          │  인터    │  - IO 드라이버/ODT │ CK, CA  │  - bank × row × col   │
 │  - ACT/RD/WR/PRE/REF 생성     │  페이스) │  - delay line(탭)  │ ◀──────▶ │  - mode registers(MR) │
 │  - refresh 타이머 · 온도 대응   │ ◀──────▶ │  - VREF 생성기     │ DQ, DQS  │  - ZQ 핀 (240Ω 저항)  │
 │  - ECC 인코드/디코드 · 카운터   │          │  - training 엔진   │         │  - 온도 센서 (LPDDR)   │
 │  - 주소 매핑 (addr → bank/row) │          │    (종종 내장 MCU)  │         │                      │
 └──────────────────────────────┘          └──────────────────┘          └──────────────────────┘
        ▲ 설정: 타이밍 레지스터                ▲ 설정: 임피던스·지연·swizzle      ▲ 설정: MR 쓰기
        │ (tRCD, tRP, CL, tRFC …)             │ training 결과가 여기 저장        │ (CL, ODT, VREF …)
```

- **Controller**: "무엇을 언제" — 읽기/쓰기 요청을 DRAM 명령으로 바꾸고, 타이밍 규칙(tRCD 등)을 지키고, 주기적으로 refresh를 넣는다. ECC도 보통 여기.
- **PHY**: "정확히 몇 피코초에" — 신호를 실제로 내보내고 받는 아날로그 회로. 비트마다 **지연 탭(delay tap)** 이 있어서 training으로 찾은 값을 넣는다. 최신 PHY는 training 알고리즘을 **PHY 안의 작은 마이크로컨트롤러 펌웨어**로 돌린다 (예: NXP i.MX8M은 U-Boot SPL이 `lpddr4_pmu_train_1d/2d` imem/dmem 펌웨어 바이너리를 PHY에 올린다 [확인됨][9]).
- **DFI (DDR PHY Interface)**: controller와 PHY 사이의 업계 표준 인터페이스 — 그래서 controller IP와 PHY IP를 다른 회사 것으로 조합할 수 있다.
- **DRAM**: 셀 배열 + **MR(mode register)** — CL(읽기 지연), 버스트 길이, ODT, 드라이브 세기, VREF 등을 controller가 MR 쓰기 명령으로 설정.

### 1.3 주소: channel · rank · bank · row · column

> 비유: 창고 단지의 **주소 체계**. channel = **독립된 출입 도로**(도로마다 따로 트럭이 다님), rank = **같은 도로를 쓰는 창고 동**(한 번에 한 동만 응답), bank = **동 안의 독립된 층**(층마다 따로 셔터를 열어 둘 수 있음), row = **층의 한 통로**(통로 셔터를 열어야 물건을 꺼낼 수 있음 = ACTIVATE), column = **통로 안 선반 칸**.

```text
 CPU 물리주소 0x8_1234_5678 ──(controller 주소 매핑)──▶ ch1 · rank0 · BG2 · bank1 · row 0x1A2B · col 0x3C
                                                       │      │      │       │        │        │
 channel: 독립 버스(LPDDR4는 x16 채널) ─────────────────┘      │      │       │        │        │
 rank:    같은 버스, CS로 선택 ───────────────────────────────┘      │       │        │        │
 bank group / bank: 독립 셀 배열 (각자 row buffer 1개) ─────────────┴───────┘        │        │
 row:     ACT로 row buffer에 "펼침" (수 KB 페이지) ─────────────────────────────────┘        │
 column:  펼친 row에서 RD/WR로 burst 단위 접근 ──────────────────────────────────────────────┘

 접근 한 번:  ACT(row 열기) ─tRCD─▶ RD(col) ─CL─▶ 데이터 burst ··· PRE(row 닫기) ─tRP─▶ 다음 ACT
```

- 같은 bank의 **다른 row**를 연달아 접근하면 PRE→ACT를 매번 해야 해서 느리다(row miss). 메모리 테스트가 주소 순서를 바꾸면 이 경로를 일부러 자극할 수 있다.
- **주소 매핑 설정이 틀리면**(예: DRAM 밀도를 8Gb로 설정했는데 실제 16Gb) → training은 통과하고 **memtest에서 address aliasing** 으로 보인다. "training 통과 + 특정 주소 비트 패턴에서만 실패"의 1순위 가설.

### 1.4 왜 training이 필요한가 — "eye"

> 비유: 데이터선 위의 비트는 **회전문**처럼 일정 시간만 "열려 있는(유효한)" 구간이 있다. 수신기는 그 구간 **한가운데**에서 사진을 찍어야(샘플링) 정확하다. 배선 길이·온도·전압에 따라 문이 열리는 시점이 비트마다 다르므로, **각 비트마다 문이 언제 열리고 닫히는지 직접 재서 가운데를 찾는 것**이 training.

```text
 한 비트(DQ)의 "eye" (여러 번 겹쳐 그린 파형)       DQS(스트로브) = "지금 찍어라" 신호

   전압 ▲      ┌───────────────────┐               DQS  ──┐   ┌───┐   ┌───
   VDDQ │ ╲    │                   │    ╱                  └───┘   └───┘
        │  ╲   │   EYE (안전 구간)   │   ╱                     ▲ 원래 엣지
   VREF ┼───╳──┼─────────●─────────┼──╳───  ← 판정 기준      ▲ training으로 옮긴 위치 = eye 가운데
        │  ╱   │      ↑중앙 샘플      │   ╲
        │ ╱    │                   │    ╲
        └──────┴───────────────────┴──────▶ 시간
              left edge         right edge
              ◀──── eye 폭 (timing margin) ────▶ ,  위아래 = eye 높이 (voltage margin)
```

- **timing training**(1D): 지연 탭을 쓸어서 left/right edge를 찾고 가운데에 놓는다.
- **VREF training**(2D): 판정 기준 전압도 같이 쓸어서 **가로×세로 둘 다 가장 넓은 점**을 찾는다.
- 결과 레지스터의 **eye 폭/높이 = margin**. 이걸 보드끼리·온도별로 비교하는 것이 characterization (→ [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)).

### 1.5 fly-by와 write leveling — 그림 한 장

> 비유: 택배 트럭(CK·명령)이 **한 줄로 늘어선 집 4채를 차례로** 돈다(fly-by). 첫 집은 일찍, 마지막 집은 늦게 도착한다. 그런데 소포(DQ/DQS)는 집마다 **각자 직통 도로**로 간다. 그러면 집마다 "트럭 도착 시각"과 "소포 도착 시각"의 차이가 다르다 → 집마다 소포 출발 시각을 다르게 조정해야 한다 = **write leveling**.

```text
            CK/CMD/ADDR: fly-by (데이지 체인) ─────────────────────────────────▶ 종단(VTT)
 SoC ──┬──────────────┬──────────────┬──────────────┬──────────────┐
       │ +0 ps        │ +150 ps      │ +300 ps      │ +450 ps      │  ← CK 도착 지연 (예시 숫자)
    ┌──▼──┐        ┌──▼──┐        ┌──▼──┐        ┌──▼──┐
    │DRAM0│        │DRAM1│        │DRAM2│        │DRAM3│
    └──▲──┘        └──▲──┘        └──▲──┘        └──▲──┘
       │ DQ/DQS 바이트 lane 0~3 = 각자 point-to-point (거의 같은 길이)
 SoC ──┴──────────────┴──────────────┴──────────────┘

 write leveling: DRAM이 "DQS 상승 엣지 순간의 CK 값"을 DQ로 되돌려 줌 → controller가 DQS 지연을
                 0→1 바뀌는 지점까지 밀어서 lane마다 CK와 정렬 (DDR 스펙 tDQSS 만족)
```

- DDR3/DDR4 모듈·다칩 보드는 CK/CA를 fly-by로 깐다(분기 stub이 신호를 망치기 때문). 그래서 write leveling이 필수 [확인됨][4][5].
- LPDDR4/5는 보통 SoC 옆 1~2개 패키지에 point-to-point라 fly-by 스큐는 작지만, 그래도 CK↔DQS(LPDDR5는 **WCK2CK leveling**) 정렬 단계가 있다 [확인됨][6].

### 1.6 메모리 테스트는 왜 여러 개인가

> 비유: 창고 점검을 **세 단계**로 한다. ① **벨트 자체 점검**(data bus): 컨베이어 한 줄에 물건을 하나씩 올려 32개 벨트가 각각 움직이는지 — 사물함 하나만 써도 됨. ② **주소 표지판 점검**(address bus): "3번 통로"라고 써 붙인 표지판이 진짜 3번으로 가는지 — 1, 2, 4, 8… 번지만 확인하면 표지판 전선 하나하나를 다 확인한 셈. ③ **사물함 전수 검사**(device/March): 모든 사물함이 0과 1을 둘 다 보관하고, 옆 사물함을 건드려도 안 바뀌는지.
>
> 순서가 중요한 이유: 벨트가 고장이면 표지판 점검 결과도 엉터리, 표지판이 고장이면 사물함 검사 결과가 "사물함 불량"처럼 보이지만 실제로는 배선 문제.

### 1.7 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| DRAM | Dynamic Random Access Memory | 커패시터에 전하로 비트를 저장 → 새어 나가므로 주기적 refresh 필요 | 온도가 오르면 더 빨리 샌다 → 고온 실패 |
| DDR | Double Data Rate | 클럭의 상승·하강 엣지 둘 다에서 데이터 전송 | 속도 = MT/s (초당 전송 수) |
| LPDDR | Low-Power DDR | 모바일·자동차용 저전력 DDR, SoC 옆에 직접 붙임(모듈 없음) | 자동차 AI SoC 대부분 LPDDR4x/5 [추정] |
| Controller (MC) | Memory Controller | 요청 → DRAM 명령 변환, 타이밍·refresh·ECC | 타이밍 레지스터 설정이 여기 |
| PHY | Physical layer | 아날로그 IO + 지연 탭 + VREF + training 엔진 | training 결과·margin이 여기 |
| DFI | DDR PHY Interface | controller ↔ PHY 표준 인터페이스 | IP 조합 가능, 디버그 시 경계선 |
| CK | Clock | DRAM 명령 클럭 (차동쌍) | CA training·write leveling 기준 |
| CA / CMD/ADDR | Command/Address bus | 명령·주소선 | CA training 대상 |
| CS | Chip Select | 어느 rank가 명령을 받을지 | rank 선택 |
| CKE | Clock Enable | DRAM 클럭 사용/절전 진입 | init 순서에 등장 |
| DQ | Data (Q) | 데이터선 (보통 byte lane = DQ 8개) | read/write eye training 대상 |
| DQS | Data Strobe | "지금 데이터 찍어라" 신호, byte lane마다 1쌍 | read gate·write leveling 대상 |
| DM / DBI | Data Mask / Data Bus Inversion | 바이트 쓰기 마스크 / 0이 많으면 뒤집어 보내 전력 절감 | 바이트 쓰기 테스트, training 단계 있음 |
| WCK / RDQS | Write Clock / Read DQS (LPDDR5) | LPDDR5에서 DQS 대신 쓰는 단방향 고속 클럭 | WCK2CK leveling |
| Byte lane | — | DQ 8개 + DQS 1쌍 + DM 1개 묶음, 타이밍을 lane 단위로 맞춤 | 실패를 lane별로 보고하면 위치가 보임 |
| Rank | — | 같은 버스를 공유하는 DRAM 묶음, CS로 선택 | rank별 training 값 따로 |
| Bank / BG | Bank / Bank Group | DRAM 안 독립 셀 배열 / 그 묶음 | 동시 접근·타이밍 규칙 |
| Row / Column | — | 페이지(ACT로 열기) / 그 안의 칸 | 주소 매핑 오류 = aliasing |
| ACT / PRE / REF | Activate / Precharge / Refresh | row 열기 / 닫기 / 셀 재충전 | 타이밍 파라미터와 연결 |
| MR / MRS / MRW | Mode Register (Set/Write) | DRAM 설정 레지스터 / 쓰기 명령 | CL·ODT·VREF·training 모드 진입 |
| ZQ cal | ZQ calibration | DRAM이 외부 정밀 저항(240 Ω) 기준으로 출력 임피던스 보정 | 저항 빠지면 init 초반 실패 |
| ODT | On-Die Termination | 칩 안의 종단 저항 (반사 감소) | 값이 틀리면 eye가 좁아짐 |
| Ron | Output drive impedance | 출력 드라이버 세기 | eye 높이·링잉 |
| VREF | Reference voltage | 0/1 판정 기준 전압 | VREF training, 고온 drift |
| Write leveling | — | lane마다 DQS를 DRAM에 도착한 CK에 정렬 | fly-by 스큐 보정 |
| Read gate / DQS gating | — | 읽기 때 DQS 수신기를 "언제 열지" 찾기 | 노이즈를 DQS로 착각하지 않게 |
| Deskew / centering | — | 비트마다 지연을 맞추고 eye 가운데에 샘플 | 비트별 배선 차이 보정 |
| CBT | Command Bus Training (LPDDR4/5) | CA선을 CK에 정렬하는 training | LPDDR CA는 고속 |
| MPR | Multi-Purpose Register (DDR4) | DRAM이 내보내는 고정 패턴 → 쓰기 없이 read training | write가 아직 안 될 때 read부터 맞춤 |
| tRCD / tRP / tRAS / CL | — | ACT→RD / PRE→ACT / ACT→PRE 최소 / RD→데이터 | 데이터시트 값 → controller 레지스터 |
| tREFI / tRFC | Refresh interval / Refresh cycle time | refresh 명령 간격 / refresh 1회 소요 | 고온에서 tREFI 절반 |
| ECC / SECDED | Error-Correcting Code / Single-Error-Correct Double-Error-Detect | 1비트 오류 정정, 2비트 오류 검출 | CE/UE 카운터, 기능 안전 |
| CE / UE | Correctable / Uncorrectable Error | 고쳐진 오류 / 못 고친 오류 | CE 증가 = 마진 경고 |
| Inline ECC | — | 별도 ECC 칩 없이 같은 DRAM 일부에 ECC 저장 | LPDDR(폭 추가 불가)에서 사용 |
| Link ECC (LPDDR5) | — | 버스 전송 중 1비트 오류만 정정 | 셀 오류는 못 고침 |
| On-die ECC (DDR5) | — | DRAM 내부에서만 정정 (밖에서 안 보임) | 시스템 ECC 대체 아님 |
| Swizzle | — | PCB에서 DQ 비트/바이트 순서를 바꿔 배선 | PHY 설정에 반영 안 하면 training 실패 |
| March test | — | 모든 주소를 정해진 순서로 훑으며 읽기/쓰기 묶음 반복 | 셀 고장 모델별 검출 보장 |
| SAF / TF / CF / AF | Stuck-At / Transition / Coupling / Address-decoder Fault | 고정 / 전이 실패 / 이웃 간섭 / 주소 디코더 고장 | March가 무엇을 잡는지의 언어 |
| EDAC | Error Detection And Correction (Linux) | 커널의 ECC 오류 보고 서브시스템 | `/sys/devices/system/edac/mc/` |

## 2. 핵심 개념 (정확한 버전)

### 2.1 DDR4 · DDR5 · LPDDR4x · LPDDR5 — 자동차 SoC 관점 차이

| 항목 | DDR4 | DDR5 | LPDDR4 / 4x | LPDDR5 / 5X |
|---|---|---|---|---|
| 최대 속도(대표) | 3200 MT/s | 4800 → 6400+ MT/s | 4266 MT/s [확인됨][6] | 6400 MT/s (5X는 더 높음) [확인됨][6] |
| 채널 구조 | DIMM 64-bit (+8 ECC) | DIMM당 32-bit 서브채널 2개 [확인됨][7] | x16 채널 | x16 채널 |
| 배치 | DIMM 또는 보드 다운 | DIMM (모듈에 PMIC) [확인됨][7] | SoC 옆 패키지 / PoP | SoC 옆 패키지 / MCM |
| 클럭/스트로브 | CK + 양방향 DQS | CK + 양방향 DQS | CK + 양방향 DQS | CK(저속) + **WCK**(쓰기) + RDQS(읽기, 선택) [확인됨][6] |
| VREF | DQ VREF는 DRAM 내부 (MR6로 training) | 내부, CA/CS VREF도 | CA·DQ VREF 내부, MR로 설정 | 내부 |
| ECC 관련 | 사이드밴드 ECC (x72) | **on-die ECC** + 사이드밴드 [확인됨][7] | 폭 추가 없음 → 컨트롤러 inline ECC | **link ECC**(전송) 선택 + inline ECC [확인됨][6] |
| 특이 training | write leveling(fly-by) | CS/CA training, DFE [확인됨][7] | **CBT**, write leveling, read DQ cal | CBT, **WCK2CK leveling**, WCK-DQ training, DFE(쓰기) [확인됨][6] |
| 절전 | — | — | 2개 FSP(frequency set point) | 3개 FSP, DVFS [확인됨][6] |
| 자동차 의미 | 구형 ECU | 서버급, 차량엔 드묾 [추정] | ADAS 1~2세대 SoC 주력 [추정] | 현재 AI SoC 주력 (대역폭·전력) [추정] |

- **Rivian 추정**: RAP1은 "processing과 memory를 하나의 multi-chip module에 통합"했다고 발표됨 (context 파일) → **패키지 안 LPDDR5/5X** 일 가능성이 높다 [추정]. 그렇다면 보드 쪽 DDR 배선 문제는 줄고, **패키지/칩 쪽 training·VREF·온도 마진**과 **ECC/RAS**가 검증의 중심이 된다 [추정]. 질문할 거리: "Is the DRAM on-package for RAP1, and does the validation team own DDR PHY training firmware or does it come from the IP vendor?"
- **FSP (Frequency Set Point)**: LPDDR4/5 DRAM은 주파수별 MR 세트를 2~3개 저장 → 주파수를 바꿀 때 training 값을 다시 안 해도 되게 미리 각 주파수에서 training해 둔다. bring-up에서 "저속에선 되는데 고속 전환하면 죽는다"의 원인 후보.

### 2.2 초기화 + training 시퀀스 (정밀 버전)

```text
 [0] 전원 rail 순서 (VDD1 → VDD2 → VDDQ, DDR4는 VPP ≥ VDD)  ── 데이터시트 순서 · PGOOD 확인 (→ BV03)
 [1] 클럭 안정 (PHY PLL lock)                                ── PLL lock 비트 (→ BV04)
 [2] RESET_n low 유지 → 해제 (DDR4 예: 200 µs 이상 [확인됨][11]), CKE low 유지 후 high, tXPR 대기
 [3] MR 쓰기 (DDR4 예: MR3→MR6→MR5→MR4→MR2→MR1→MR0 순서를 쓰는 컨트롤러도 있음 [확인됨][11])
 [4] ZQ calibration (DDR4 ZQCL / LPDDR4·DDR5 ZQ start+latch) — 외부 240 Ω 기준 Ron·ODT 보정
 [5] CA / CS training (LPDDR4·5 CBT, DDR5 CS·CA training) — DRAM이 받은 CA 값을 DQ로 되돌려 줌 [확인됨][6]
 [6] Write leveling (DDR4) / WCK2CK leveling (LPDDR5) — DQS(또는 WCK)를 CK에 정렬, lane별
 [7] Read gate (DQS gate) training — DQS preamble 위치 찾기 (coarse = 클럭 단위, fine = 탭)
 [8] Read eye: DQ/DQS deskew + centering — DRAM 고정 패턴(MPR / read DQ cal 패턴) 사용 → 쓰기 불필요
 [9] Write eye: write DQS-to-DQ deskew + centering — 써 보고 읽어서 비교 (read가 먼저 돼야 함)
[10] (write latency calibration — 클럭 단위 지연 맞추기, PG150 단계 [확인됨][8])
[11] VREF training: read VREF(PHY 쪽) · write VREF(DRAM 쪽, DDR4 MR6) — 2D sweep → 재-centering
[12] 최종 sanity: 짧은 write/read 패턴 · memtest · (VT tracking / 주기적 retraining 켜기)
```

- **벤더마다 순서가 다르다** [확인됨]: AMD UltraScale(PG150)은 DQS gate → write leveling → read deskew/centering → write DQS-to-DQ → write latency → … → read/write VREF 순서로 문서화[8], Intel은 read deskew가 write leveling **앞**에 오고 두 번 이상 한다고 기술[4], Microchip PolarFire는 clock training → write leveling → read leveling(gate·DQ align·DQS centering) 순[5]. 면접에서는 "일반적 순서 + 벤더 차이 있음"이라고 말하면 된다.
- 단계별로 **누가 무엇을 보내고 누가 피드백하나**:

| 단계 | 송신 | DRAM이 하는 일 | 피드백 경로 | 찾는 값 |
|---|---|---|---|---|
| ZQ cal | controller가 ZQ 명령 | 내부 저항 배열을 외부 240 Ω에 맞춤 | 없음(완료 대기) | Ron, ODT 코드 |
| CA training (CBT) | controller가 CA 패턴 | 받은 CA 값을 DQ로 출력 | DQ | CA 지연, CA VREF |
| Write leveling | controller가 DQS 펄스 | DQS 상승 엣지 순간 CK 값을 샘플 | DQ (0 또는 1) | lane별 DQS 지연 (0→1 전이점) [확인됨][4] |
| Read gate | controller가 RD 명령 | 고정 패턴 읽기 | DQS 자체 | gate open 시점 (coarse+fine) |
| Read eye | controller가 RD (MPR/패턴) | 고정 패턴 출력 | DQ | 비트별 지연, eye 중앙 |
| Write eye | controller가 WR 후 RD | 저장 후 출력 | DQ | 쓰기 DQ 지연 |
| VREF | 위 반복 + VREF sweep | (write VREF는 MR로 변경) | DQ | eye가 가장 넓은 VREF |

- **Write leveling 내부 동작 (AMD PG313 기준)** [확인됨][4]: MRS로 write leveling 모드 진입 → controller가 DQS 펄스열 송출 → DRAM이 CK를 DQS로 샘플해 DQ로 회신 → coarse 탭을 올리며 `0 → X → 1` 패턴 탐색 → 마지막 stable 0으로 돌아가 fine 탭으로 noise 구간의 좌우 끝을 찾고 **가운데**에 둠 → 끝까지 못 찾으면 CAL_ERROR 코드 보고. 즉 **"못 찾았다" 에러가 lane 번호와 함께 나온다**.
- **Read gate가 왜 필요?**: DQS는 양방향이고 읽기 burst 사이에는 **tri-state(아무도 안 구동)** → 떠 있는 선의 노이즈를 스트로브로 착각하면 쓰레기 데이터. 그래서 "RD 명령 후 CL + 왕복 지연 뒤 preamble이 오는 그 순간"에만 수신기를 연다 [확인됨][5].

### 2.3 파라미터는 어디서 오나

| 출처 | 무엇 | 누가 주나 | 틀리면 증상 |
|---|---|---|---|
| DRAM 데이터시트 / JEDEC 스펙 | 타이밍(tRCD, tRP, tRAS, tRFC, tREFI, CL, CWL), MR 값, 밀도·row/col/bank 비트 수 | DRAM 벤더 (Micron/Samsung/SK hynix) | training 통과 후 memtest 실패, 특정 패턴만 실패 |
| SoC/PHY 벤더 툴 | PHY 레지스터, training 펌웨어, 임피던스/ODT 기본값 | SoC 벤더 (예: NXP DDR Tool = RPA 스프레드시트 + 스트레스 테스트 툴 → SPL용 init 코드 생성 [확인됨][10]) | training 실패 / 마진 작음 |
| 보드 레이아웃 | swizzle(비트·바이트 순서), 배선 길이(지연 초기값), 토폴로지, 종단 | 보드(HW) 엔지니어 | 특정 lane training 실패 |
| SI 시뮬레이션 | Ron/ODT 추천 조합, VREF 범위 | SI 엔지니어 | eye 좁음, 고온/저전압 실패 |
| 측정·characterization | 최종 ODT/Ron/VREF 고정값, 온도별 refresh | 검증팀 (= 이 포지션) | 일부 보드·코너 실패 |

- 실제 예: NXP 포럼에서 Hynix 부품 스트레스 테스트가 실패하다가 RPA의 ATxImpedance를 40 → 30으로 바꾸자 통과한 사례 [확인됨][10] — **"training은 통과했지만 임피던스 설정이 마진을 갉아먹고 있었다"** 의 전형.
- U-Boot의 `lpddr4_timing.c` (i.MX8M EVK)는 결국 **레지스터 주소/값 쌍의 표** — 툴이 생성하고 SPL이 그대로 쓴다 [확인됨][10]. 즉 bring-up 엔지니어가 보는 "DDR 설정"의 실체는 수백 줄짜리 레지스터 테이블.

### 2.4 ECC — SECDED · 카운터 · 주입 · scrub

> 비유: 택배 상자마다 **검수 스티커(check bits)** 를 붙여 보낸다. 받는 쪽은 스티커로 "물건 하나가 바뀌었으면 원래대로 고치고(SEC), 둘이 바뀌었으면 고칠 순 없지만 알아챈다(DED)".

- **SECDED**: 64 data bit + 8 check bit (Hamming + 전체 패리티) → 1비트 정정, 2비트 검출. DIMM의 x72 구조가 이것 [확인됨][7].
- **LPDDR에는 여분 폭이 없다** → controller가 같은 DRAM의 일부 영역에 ECC를 저장하는 **inline ECC** (용량·대역폭 일부 희생) [확인됨: Synopsys가 inline ECC를 별도 기능으로 언급][6]. 자동차 기능 안전(ISO 26262) 때문에 자동차 SoC는 LPDDR + inline ECC 조합이 흔하다 [추정].
- **ECC 종류 구분 (면접 함정)**: on-die ECC(DDR5, DRAM 내부, 밖에서 안 보임) ≠ link ECC(LPDDR5, 전송 오류만) ≠ system/inline ECC(controller, end-to-end). on-die ECC만 있으면 **버스 오류는 못 잡는다** [확인됨][7].
- **ECC init(scrub)**: ECC 메모리는 쓰기 전 check bit가 쓰레기 → 처음 읽으면 UE. 그래서 부트 초기에 **전체를 한 번 써서 초기화**. Zynq MPSoC는 FSBL이 이걸 한다 [확인됨][12]. SPL에서 ECC를 켰는데 init 순서를 잘못 잡아 문제가 된 사례도 U-Boot 메일링 리스트에 있다 [확인됨][12].
- **카운터와 보고**: controller의 CE/UE 카운트 레지스터 + 오류 주소/syndrome 레지스터 + 인터럽트. 예: Synopsys DDRC(ZynqMP)의 Linux `synopsys_edac` 드라이버는 ECC_ERRCNT(CE/UE 수), ECC_CEADDR0/1·ECC_CSYND0(CE 위치·syndrome)을 읽는다 [확인됨][13].
- **주입(injection)**: 같은 드라이버가 `CONFIG_EDAC_DEBUG`일 때 sysfs `inject_data_error`(주소 지정 → ECC_POISON0/1 레지스터 설정), `inject_data_poison`("CE" 또는 UE 선택)을 제공 [확인됨][13]. 주입 방식 일반: (a) controller의 poison/inject 레지스터, (b) ECC 끄고 데이터만 바꿔 쓴 뒤 ECC 켜고 읽기(check bit 불일치 유발), (c) 1비트/2비트 각각.
- **Patrol scrub**: 백그라운드로 전체 메모리를 읽어 CE를 미리 고쳐 쓰기 → 1비트 오류가 쌓여 2비트(UE)가 되기 전에 제거. Linux EDAC `sdram_scrub_rate`로 노출 [확인됨][14].

### 2.5 Refresh와 온도

- DRAM 셀은 커패시터 → 전하가 샌다. **온도가 높을수록 빨리 샌다** → refresh를 더 자주.
- DDR4: 8192번 REF / 64 ms → **tREFI 7.8 µs**, 85 °C 초과(확장 온도)에서는 **3.9 µs(2× refresh)** — DDR 세대 공통 관행 (DDR4 데이터시트 온도 표로 재확인 필요 [추정]); Intel도 고온 범위에서는 tREFI를 데이터시트 값으로 줄이라고 함 [확인됨][15].
- LPDDR4: refresh window 32 ms, tREFI ≈ 3.9 µs, **MR4가 DRAM 온도/필요 refresh 배율(0.5×~4×)을 알려 준다** [확인됨][15] → controller(또는 SW)가 MR4를 주기적으로 읽어 refresh 간격을 바꿔야 한다. TI 플랫폼 답변: HW는 MR4를 polling만 하고 간격 변경은 SW 몫 [확인됨][15].
- 실제 에라타: NXP i.MX8MM **ERR050805** — LPDDR4가 85 °C 이상에서 시작하면 controller의 자동 derating이 실패할 수 있음 → U-Boot는 자동 derating을 끄고 **고정 2× refresh** 적용 [확인됨][15]. 자동차(AEC-Q100 Grade 2 = −40~105 °C, Grade 1 = −40~125 °C) 보드에서 그대로 나올 법한 문제.
- 고온에서 같이 나빠지는 것: VREF/임피던스 drift(→ 주기적 ZQ cal·retraining), 타이밍 마진 축소, PDN 노이즈 증가.

### 2.6 메모리 테스트 알고리즘 (정밀)

#### 2.6.1 Data bus test — walking 1s (Barr)

- 한 주소에 `1<<0, 1<<1, …, 1<<31`을 차례로 쓰고 바로 읽는다. n비트 버스에 n개 패턴이면 각 데이터선이 다른 선과 **독립적으로** 0/1이 되는지 확인 (2ⁿ 불필요) [확인됨][2].
- 여기서는 **"쓰고 바로 읽기"가 괜찮다** — 목표가 배선이지 칩 존재가 아니므로. 대신 **칩이 없어도 통과할 수 있다** (버스 커패시턴스가 방금 쓴 값을 잠깐 붙잡아 둠) [확인됨][2] → 실측 F12에서 DBUS 통과 확인.
- 버스가 여러 칩으로 나뉘면(byte lane마다 다른 DRAM) **칩마다 한 주소씩** 돌려야 한다 [확인됨][2]. 실패 패턴의 set bit = 고장 데이터선; `exp ^ act` = 고장 비트 마스크.

#### 2.6.2 Address bus test — power-of-two offsets (Barr)

- 원리: 주소선 하나가 고장 나면 **그 비트만 다른 두 주소가 같은 셀**을 가리킨다(aliasing). 주소선은 log₂N개뿐이므로 offset `0, 1, 2, 4, 8, …`만 보면 모든 주소선을 한 번씩 토글한 셈 → **O(log N)** 접근 [확인됨][2].
- 알고리즘 [확인됨][2][3]:
  1. 모든 power-of-two offset에 `PATTERN`(0xAAAAAAAA) 쓰기.
  2. **stuck-high 검사**: offset 0(base)에 `ANTI`(0x55555555) 쓰기 → 모든 power-of-two offset이 여전히 `PATTERN`이어야 함. base를 `PATTERN`으로 복구.
  3. **stuck-low / short 검사**: 각 offset `t`에 `ANTI`를 쓰고 → base와 **t가 아닌** 모든 offset이 `PATTERN`인지 확인 → `t` 복구.
- **매 쓰기 후 다른 모든 위치를 확인해야 하는 이유**: 쓰기 하나가 다른 곳을 몰래 덮어쓸 수 있기 때문 [확인됨][2].
- **실측으로 본 미묘한 점** (§6 결과): base가 0 정렬이면 stuck-**low** 주소선(A4)도 offset 16이 base와 겹치므로 **step 2에서 먼저** 걸린다(F08 `s2:A4`). step 3이 꼭 필요한 건 **base와는 안 겹치고 두 offset끼리만 겹치는** 경우 — wired-OR short(A2·A3 → 4와 8이 둘 다 12로)(F11 `s3:A2`). Barr 주석의 "step 2 = stuck high, step 3 = stuck low/shorted"는 **의도**이고, 실제 어느 step에서 걸리는지는 고장 모양에 달렸다.
- 한계 [확인됨][2]: 칩 선택용 상위 주소선(테스트 범위 내에서 고정)과 버스 폭 때문에 고정되는 하위 비트는 테스트 불가. 예: 32-bit 주소 공간에서 128 KB 블록은 17비트만 검사. **base는 테스트 비트가 0이어야** 한다.
- DDR에서는 CPU 주소 비트 → bank/row/column 비트로 **매핑**되므로, "CPU 주소 A4 고장"이 실제로는 "column 비트 C1" 같은 다른 이름일 수 있다. 실패 주소를 controller 매핑으로 역변환해서 보고하면 HW 팀이 바로 핀을 찾는다.

#### 2.6.3 Device test — increment / invert (Barr)

- pass 1: 모든 주소에 증가값(`addr+1`) 쓰기 → pass 2: 확인 + 비트 반전값 쓰기 → pass 3: 반전값 확인. **모든 비트가 0과 1을 한 번씩 보관**하는지, 주소마다 값이 달라서 **칩 없음·aliasing**도 잡힘 [확인됨][2]. 4N 접근(실측).
- 한계: 접근 순서가 단방향이고 배경 패턴이 주소값이라 **coupling fault 보장이 없다** → 실측 F05(CFid) 놓침.

#### 2.6.4 March C- (정확한 정의)

```text
 March C- = { ⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0) }      길이 = 10N   [확인됨][1][16]
              M0      M1         M2         M3         M4       M5
 ⇑ 오름차순 · ⇓ 내림차순 · ⇕ 아무 방향 · r0 = "0을 기대하며 읽기" · w1 = "1 쓰기"
 접근 수: M0 = N, M1~M4 = 각 2N, M5 = N  → 10N  (실측: 1280 rd + 1280 wr, N = 256)
```

- 검출 고장 (Microchip 문서 기준) [확인됨][16]: **SAF**(stuck-at), **TF**(transition), **AF**(address decoder), **CFin**(inversion coupling), **CFid**(idempotent coupling), **CFst**(state coupling) — 모두 "unlinked"(고장끼리 얽히지 않은 경우).
- 왜 이 모양인가:
  - **각 원소는 "읽고 나서 쓴다"** → 앞 원소가 남긴 값이 그대로인지 먼저 확인. 이웃 셀 쓰기로 값이 바뀌었으면(coupling) 여기서 보인다.
  - **⇑와 ⇓를 둘 다** → aggressor(때리는 셀)가 victim(맞는 셀)보다 위에 있든 아래에 있든, 또 0→1·1→0 전이 둘 다를 커버.
  - **M5 ⇕(r0)** → M4 마지막 쓰기 이후 생긴 고장을 확인 (빼먹으면 9N, 커버리지 구멍).
  - **AF 검출**: 주소 디코더 고장(두 주소 → 한 셀, 또는 셀에 접근 불가)은 "w1 했는데 다른 주소에서 1이 보임"으로 걸린다(실측 F08~F11 모두 M1).
- 고장 모델 정의 (짧게):

| 약어 | 이름 | 뜻 | March C-에서 처음 걸리는 곳 (실측) |
|---|---|---|---|
| SAF0/1 | Stuck-At Fault | 셀 비트가 항상 0/1 | SA1: M1 `r0` · SA0: M2 `r1` |
| TF | Transition Fault | 0→1(또는 1→0) 전이가 안 됨 | 0→1 불가: M2 `r1` |
| CFin | Inversion Coupling | aggressor의 전이가 victim을 **뒤집음** | M2 (A > V 배치) |
| CFid | Idempotent Coupling | aggressor의 전이가 victim을 **특정 값으로** 만듦 | M4 (A < V, 0으로 강제) — 다른 테스트는 전부 놓침 |
| CFst | State Coupling | aggressor가 특정 **상태**일 때 victim이 고정 | (모델 미구현) |
| AF | Address decoder Fault | 주소 → 셀 매핑 고장 (aliasing, 접근 불가) | M1 |

- **놓치는 것**: retention(지연 없음 → 실측 F13 놓침), NPSF(이웃 패턴 민감 고장), linked fault 일부, 동적 고장(read disturb 등), **word 안 비트끼리 bridging**(0/1 배경만 쓸 때 → 실측 F07 놓침). 커버리지를 넓힌 변형: March C+(14N), March B(17N, linked 일부), March SS(22N) [확인됨][1].
- **Word-oriented memory 주의**: 이론의 March는 비트 단위 메모리 기준. 32-bit word에 `0x00000000/0xFFFFFFFF`만 쓰면 같은 word 안 두 비트는 **항상 같은 값** → 둘이 short돼도 안 보임. 해결: **data background**를 `0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF, 0x0000FFFF`(log₂32 + 1 = 6개)로 바꿔 반복 → 실측 MARCH-DB가 F07을 배경 1(`0x5555…`)에서 잡음. 60N.

#### 2.6.5 Moving inversions · random · bit fade · hammer (memtest86 계열)

| memtest86 테스트 | 하는 일 | 노리는 것 [확인됨][17] |
|---|---|---|
| Test 0 address walking ones | 주소 비트 walking-1 | 주소 고장 |
| Test 1–2 own address | 각 주소에 자기 주소값 | 주소 고장 (aliasing) |
| Test 3–5, 7 moving inversions | 패턴 채움 → ⇑(확인, 반전 쓰기) → ⇓(확인, 원래 쓰기) | hard error, data-sensitive (8-bit/random/32-bit 패턴) |
| Test 8, 11, 12 random number sequence | 재현 가능한 난수 + 보수 | data-sensitive |
| Test 9 modulo 20 | 20칸 간격 패턴 | 캐시/버퍼 때문에 moving inversion이 놓치는 것 |
| Test 10 bit fade | 채우고 수 분 대기 후 확인 | **retention** (시간이 지나면 사라지는 비트) |
| Test 13 hammer | 같은 bank row를 반복 접근 | **row hammer** (이웃 row 비트 플립) |
| Test 14 DMA | CPU 없이 DMA로 전송 후 검증 | 주변장치 경로 |

- moving inversions = MATS+ 계열 모양(5N). 실측에서 CFid·intra-word short를 놓침.
- random: 대부분 잡지만(실측 고장 13개 중 11개) **진단이 흐림**(xor가 난수처럼 나와 어느 선인지 안 보임) + CFid는 확률적으로 놓침(실측 놓침).
- bit fade: retention을 잡는 **유일한** 테스트(실측 F13). 대신 시간이 든다.

#### 2.6.6 캐시 · MMU · DMA · 실행 위치

- **캐시**: 캐시 켠 채 작은 영역을 테스트하면 **캐시를 테스트**하게 된다. bring-up 셀 테스트는 해당 영역을 **Device / Normal Non-cacheable**로 매핑하거나 캐시 off. 캐시 켠 테스트는 별도 목적(burst·line fill/evict 경로, 대역폭 + 무결성) — 이때는 **캐시보다 훨씬 큰 범위**를 써야 DRAM까지 간다. memtest86도 캐시 끄기 옵션 제공 [확인됨][17].
- **volatile**: 테스트 포인터는 `volatile uint32_t *` — 없으면 컴파일러가 "방금 쓴 값"을 그대로 돌려주는 최적화로 읽기를 없앤다.
- **write buffer / 순서**: Device 메모리 속성이면 순서 보장. Normal non-cacheable이면 쓰기 뒤 `DSB` 후 읽기가 안전 [추정 — 아키텍처별 확인].
- **DMA**: (a) CPU 루프보다 수십 배 빠른 fill/compare, (b) 여러 master 동시 트래픽으로 **controller 스케줄링·대역폭 스트레스**, (c) 캐시 일관성 버그(clean/invalidate 누락)는 "메모리 고장처럼" 보인다 → 먼저 non-cacheable 버퍼로 배제 (→ [LP08](2026-10-31_LP08_dma_iommu_coherency.html)).
- **실행 위치**: 테스트 코드·스택이 테스트 영역 안에 있으면 자기 자신을 지운다. DDR 테스트는 SRAM/TCM에서 실행, 또는 DDR을 반으로 나눠 A에서 돌며 B 테스트 → 코드 이동 → A 테스트. Barr: C 스택이 RAM을 필요로 하므로 경우에 따라 어셈블리 구현 [확인됨][2].
- **규모 계산**: 8 GB를 10N(64-bit 접근, 읽기+쓰기 각 5회) → 약 80 GB 트래픽. 실효 10 GB/s면 ≈ 8 s지만 uncached 단일 코어 루프는 그보다 훨씬 느리다 [추정] → bring-up은 bus test + 샘플 영역 March, 전체 March·bit fade·hammer는 야간 회귀/SLT로.

### 2.7 Linux 쪽 도구 (짧게)

| 도구 | 어디서 | 무엇 | 주의 |
|---|---|---|---|
| U-Boot `mtest` | 부트로더 | `mtest [start [end [pattern [iterations]]]]`, `CONFIG_CMD_MEMTEST`, `CONFIG_SYS_ALT_MEMTEST`=여러 고정 패턴 버전 [확인됨][18] | U-Boot 자신이 있는 영역 피하기 (기본 범위 `CONFIG_SYS_MEMTEST_START/END`) |
| `memtester` | Linux userspace | `memtester <size>[B/K/M/G] [runs]`, root로 mlock, `-p physaddr`(+`-d dev`)로 물리주소 지정 [확인됨][19] | 커널/다른 프로세스 영역을 `-p`로 덮으면 크래시; 가상주소라 물리 위치 진단 약함 |
| EDAC sysfs | 커널 | `/sys/devices/system/edac/mc/mcX/{ce_count, ue_count, ce_noinfo_count, reset_counters, seconds_since_reset, sdram_scrub_rate}`, `dimmX/{dimm_ce_count, dimm_ue_count, dimm_label, dimm_location}` [확인됨][14] | controller 드라이버가 있어야 mcX 생김 |
| rasdaemon / `ras-mc-ctl` | userspace | EDAC 이벤트를 DB로 기록·요약 | 장기 CE 추세 분석 |
| stressapptest 등 | userspace | 다중 스레드·DMA 섞인 부하 | 진단보다 "부하 + 에러 유무" |

## 3. 보드에서 볼 것 · 잴 것

### 3.1 DDR bring-up 체크리스트 (training 전 → 후)

| # | 확인 항목 | 어떻게 / 어디서 | 통과 기준 | 실패 시 다음 행동 |
|---|---|---|---|---|
| 1 | DDR rail (VDD1/VDD2/VDDQ, DDR4 VPP·VTT) 전압·순서 | DMM + scope 다채널, DRAM 가까운 decap 위치 | 데이터시트 범위·순서 | → BV03, PMIC fault 레지스터 |
| 2 | rail ripple / 부하 droop | scope 20 MHz BW limit + 짧은 GND 스프링, 메모리 트래픽 중 | 데이터시트 AC 허용치 | PDN 문제 → SI/PI 팀 (BV12) |
| 3 | PHY PLL lock, DDR 클럭 주파수 | lock 비트, debug clock out, CK 차동 프로브 | lock=1, 목표 주파수 | → BV04 |
| 4 | RESET_n · CKE 타이밍 | scope (RESET_n, CKE, 마지막 rail PGOOD) | 데이터시트 최소 시간 이상 | init 코드 지연값 |
| 5 | ZQ 저항 실장·값 | 회로도 + DMM (전원 off) | 240 Ω (1%) | 재실장 |
| 6 | 설정 vs 실물 일치 | BOM의 DRAM 파트넘버 ↔ init 테이블 (타입·밀도·속도·rank·x16/x8) | 완전 일치 | 테이블 재생성 |
| 7 | swizzle(비트/바이트 순서) 설정 | 레이아웃 netlist ↔ PHY 설정 | 일치 | 설정 수정 |
| 8 | training 결과 (단계별 pass/fail, lane별 탭·eye) | PHY 상태 레지스터, training FW 로그(UART), JTAG 덤프 | 전부 pass, eye 폭이 가이드 이상 | §3.3 표 |
| 9 | bus test → address test → March (캐시 off) | SRAM에서 도는 테스트 FW | 0 error | §6 매트릭스로 고장 종류 추정 |
| 10 | 저속 → 목표 속도 단계 상승 | 주파수 단계별 training + memtest | 목표 속도까지 pass | 마진 문제 (BV12) |
| 11 | 온도·전압 코너 | thermal chamber / forcer, PMIC margining ±% | 코너 전부 pass + 마진 기준 | refresh·ODT·VREF 재검토 |
| 12 | ECC 경로 | CE/UE 주입 → 카운터·인터럽트·주소 레지스터 | 주입한 대로 보고 | 드라이버/설정 |

### 3.2 training 로그 읽기

`[예시 출력]` — 가상의 LPDDR4x 2채널 bring-up FW UART 로그 (형식은 일반적인 PHY training FW 출력을 흉내 낸 것, 특정 벤더 아님):

```text
[DDR] cfg: LPDDR4X 4266MT/s 2ch x16 1rank 16Gb/ch  swizzle=board_revA  ODT=40 Ron=40
[DDR] ZQ cal ch0 ............ PASS (code 0x1C)
[DDR] ZQ cal ch1 ............ PASS (code 0x1B)
[DDR] CBT ch0 ............... PASS  CA vref=27.2%  ca_dly=[31 30 32 31 29 30]
[DDR] CBT ch1 ............... PASS  CA vref=26.8%  ca_dly=[30 31 30 30 31 29]
[DDR] WRLVL ch0 B0 dqs=118 B1 dqs=124   PASS
[DDR] WRLVL ch1 B0 dqs=121 B1 ----      FAIL  no 0->1 transition found (B1)
[DDR] training aborted at WRLVL ch1 byte1, err=0x09
```

| 줄 | 해석 |
|---|---|
| cfg | **설정이 실물과 같은지 먼저 본다** (타입·속도·밀도·swizzle 이름). 보드 rev가 바뀌었는데 swizzle 테이블이 revA면 여기서 이미 의심 |
| ZQ PASS | ZQ 저항·VDDQ 기본 OK. 두 채널 코드가 비슷 → 정상 |
| CBT PASS | CK와 CA는 살아 있음 → 클럭·명령 버스·리셋·전원은 대체로 OK. ca_dly가 비슷 → CA 배선 균일 |
| WRLVL ch0 | 두 lane 모두 전이점 발견. 값 차이(118 vs 124)는 lane 배선 길이 차이로 정상 범위 |
| WRLVL ch1 B1 FAIL | **ch1의 byte lane 1만** DQS가 CK 전이를 못 찾음 → 범위가 좁혀짐: ch1 B1의 **DQS 쌍(극성 뒤바뀜·단선), 그 lane의 DQ 피드백 비트(write leveling은 보통 특정 DQ 하나로 회신 → 그 비트 swizzle 설정 오류 가능), DRAM 그 바이트의 BGA 솔더, lane 지연이 탭 범위 밖** |
| err=0x09 | 에러 코드는 벤더 문서로 해석 (예: AMD PG313은 write leveling 전이 미발견을 CAL_ERROR 0x9로 정의 [확인됨][4] — 이 로그는 그것을 흉내 낸 예시) |

- **다음 행동**: ① 같은 보드 ch1 B1 DQS를 차동 프로브로 write leveling 중 측정(펄스가 DRAM 볼 근처까지 가나) ② swizzle 테이블에서 ch1 B1의 피드백 DQ 비트 확인 ③ 다른 보드에서도 ch1 B1인가? (같으면 설계/설정, 이 보드만이면 조립 → X-ray) ④ 주파수 낮춰 재시도(탭 범위 문제면 저속에서 통과).

### 3.3 "어느 단계가 실패했나 → 무엇을 의심하나"

| 실패 단계 | 1순위 가설 | 확인 방법 |
|---|---|---|
| init 전 / ZQ | 전원(VDDQ·VDD2) · RESET_n/CKE 순서 · ZQ 저항 · 설정한 DRAM 타입 틀림 | rail scope, ZQ 저항 DMM, init 코드 지연 |
| CA training (CBT) | CK 미출력/주파수 · CA 배선 · CA VREF 범위 · CS 선택 오류(rank) | CK 프로브, PLL lock, 저속 재시도 |
| Write leveling (특정 lane) | 그 lane DQS 쌍 · 피드백 DQ 비트 swizzle · BGA 솔더 · 지연 탭 범위 초과 | lane별 비교, 다른 보드 비교, X-ray |
| Write leveling (모든 lane) | CK 자체 문제 · write leveling 모드 MR 설정 · ODT 설정 | CK 품질, MR 값 |
| Read gate | read latency(CL)/추가 지연 설정 · DQS 수신 · 왕복 지연 범위 | 설정값, DQS 프로브 |
| Read eye (몇 비트만 좁음) | 그 비트 배선(길이·crosstalk) · 볼 솔더 · 비트 swizzle | 비트별 eye 표, 보드 간 비교 |
| Read eye (전체 좁음) | PHY 쪽 ODT · VREF · PDN 노이즈 · 클럭 jitter | ODT 조합 sweep, rail ripple |
| Write eye (read는 좋음) | DRAM 쪽 ODT · 드라이버 Ron · DRAM VREF(MR) · DM/DBI | MR 값, write VREF sweep |
| VREF training (중앙이 극단) | VREF 범위 설정 · 종단 불일치 · 전원 레벨 | 2D eye 그림 |
| 전부 pass → memtest 실패 | 주소 매핑/밀도 설정 · 타이밍(tRFC·tRCD) · refresh · 마진이 겨우 통과 | §6 매트릭스, 타이밍 완화(느슨하게) 후 재시험 |
| 전부 pass → 부하·시간 지나서 실패 | 온도 상승 → 마진 축소 · refresh · PDN(동시 스위칭) · retraining 미사용 | 온도 로깅, CE 추세 |

### 3.4 margin 읽어서 보드·온도 비교

`[예시 출력]` — training 결과 레지스터를 덤프해 표로 만든 것 (단위: 지연 탭, 1 탭 ≈ 일정 ps — 실제 값은 PHY 문서):

```text
 board  temp   ch0B0  ch0B1  ch1B0  ch1B1   (read eye width, taps)   min   verdict
 B01    25C     38     36     37     35                                35    ok
 B01    105C    31     30     31     22                                22    LOW  (ch1B1)
 B02    25C     39     37     38     36                                36    ok
 B02    105C    32     31     30     29                                29    ok
 B07    25C     37     35     36     24                                24    LOW  (ch1B1)
 guideline: >= 28 taps at all corners  [예시 기준]
```

- 읽는 법:
  - **모든 lane이 고온에서 비슷하게 줄어듦** → 정상적인 온도 의존 (B02).
  - **특정 lane만 유독 작음** (모든 보드 ch1B1 계열) → 설계(배선 길이·crosstalk·레퍼런스 플레인) 또는 설정(swizzle·ODT) — 레이아웃 팀에 lane 번호와 함께 보고.
  - **특정 보드만 작음** (B07 상온부터) → 조립(솔더), 부품 lot → swizzle 아님. swap test.
  - 보고 단위: 보드 × 온도 × 전압 × lane × (read/write) × (timing/VREF). 최소값과 가이드라인 대비 여유를 한 줄로.
- 벤더 도구 예: AMD MIG는 calibration 상태를 CAL_STATUS/ERROR STATUS 레지스터와 `init_calib_complete`로 노출 [확인됨][8]; NXP DDR Tool은 calibration + stress test + 코드 생성 [확인됨][10]. (per-bit margin GUI 기능은 벤더 문서로 재확인 [추정])

### 3.5 ECC 카운터 · Linux 확인

실측 (Lima VM `elinux`, Ubuntu aarch64, 2026-11-01) — EDAC 코어는 있지만 **ECC 메모리 controller 드라이버가 붙지 않아 mc0이 없다** (VM에는 DDRC가 없으니 정상):

```text
$ uname -r
7.0.0-38-generic
$ ls /sys/devices/system/edac/
mc  power  uevent
$ ls /sys/devices/system/edac/mc/
power  subsystem  uevent                  <- mc0/ 없음 = 등록된 memory controller 없음
$ grep -E "^CONFIG_EDAC(=|_SYNOPSYS|_ZYNQMP)|# CONFIG_EDAC_DEBUG" /boot/config-$(uname -r)
CONFIG_EDAC=y
# CONFIG_EDAC_DEBUG is not set           <- 이 커널로는 synopsys_edac 주입 sysfs가 안 생김
CONFIG_EDAC_SYNOPSYS=m                   <- ZynqMP/Synopsys DDRC용 드라이버 (모듈)
CONFIG_EDAC_ZYNQMP=m
```

- 해석: 실제 보드에서 ECC를 검증하려면 ① DT에 DDRC 노드(`xlnx,zynqmp-ddrc-2.40a` 등) + 드라이버 로드 → `mc0` 생성 확인 ② 주입을 쓰려면 **`CONFIG_EDAC_DEBUG=y` 커널**이 필요 [확인됨][13].

`[예시 출력]` — ECC 켠 보드에서 기대하는 모양:

```text
$ grep . /sys/devices/system/edac/mc/mc0/{mc_name,size_mb,ce_count,ue_count,seconds_since_reset}
/sys/devices/system/edac/mc/mc0/mc_name:Synopsys_DDR_Controller
/sys/devices/system/edac/mc/mc0/size_mb:4096
/sys/devices/system/edac/mc/mc0/ce_count:0
/sys/devices/system/edac/mc/mc0/ue_count:0
/sys/devices/system/edac/mc/mc0/seconds_since_reset:812
$ echo 0x10000000 > /sys/devices/system/edac/mc/mc0/inject_data_error   # 주소 지정 (EDAC_DEBUG 커널)
$ echo CE > /sys/devices/system/edac/mc/mc0/inject_data_poison          # CE 모드
$ md5sum /some/file/mapped/there >/dev/null   # 그 주소를 읽게 만든다 (방법은 플랫폼별)
$ dmesg | tail -1
EDAC MC0: 1 CE ... on mc#0csrow#0channel#0 (... page:0x10000 offset:0x0 ...)
$ cat /sys/devices/system/edac/mc/mc0/ce_count
1
```

| 줄 | 해석 |
|---|---|
| mc_name | 어느 controller 드라이버가 붙었나 (DT compatible과 일치해야) |
| ce_count / ue_count | 전체 누적 — **추세**가 중요 (초당·시간당 증가율), `reset_counters`로 시험 구간마다 0으로 |
| inject_* | 주입 경로 검증: 주소 → poison 레지스터 → 그 주소 읽기 → 카운터·dmesg·인터럽트 |
| dmesg 위치 | 보고된 위치(csrow/channel/page)가 **주입한 주소와 일치**하는지 = 주소 디코드 경로까지 검증 |

### 3.6 U-Boot / memtester

`[예시 출력]`:

```text
=> mtest 0x80000000 0x80100000 0x55aa55aa 4
Testing 80000000 ... 80100000:
Iteration:      4
Tested 4 iteration(s) with 0 errors.

$ sudo memtester 512M 1
memtester version 4.x (64-bit)
want 512MB (536870912 bytes)
got  512MB (536870912 bytes), trying mlock ...locked.
Loop 1/1:
  Stuck Address       : ok
  Random Value        : ok
  ...
  Walking Ones        : ok
```

- `mtest`의 마지막 줄 형식("Tested N iteration(s) with 0 errors.")은 U-Boot 문서 예와 같음 [확인됨][18]. memtester는 root로 mlock 성공 여부가 중요 — 실패하면 경고만 하고 계속 돌아 **결과가 부정확** [확인됨][19]. memtester 하위 테스트 이름은 버전별로 다를 수 있음 [추정].
- 둘 다 **가상/부트로더 관점**: 실패 시 물리 주소·lane으로의 역변환은 직접 해야 한다. bring-up 진단은 §6 같은 bare-metal 테스트가 더 정확.

## 4. 꼬리 질문 체인

### Chain 1 — "DDR training fails on a new board. How do you debug?"

<details><summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 펼치기)</summary>

**Q. DDR training fails on a new board. How do you debug?**
*"I treat it bottom-up. Before blaming training I confirm the DDR rails are in spec and sequenced, ripple is acceptable at the DRAM, the PHY PLL is locked at the right frequency, and RESET_n and CKE timing match the datasheet. Then I confirm the configuration matches the hardware: DRAM part number, density, ranks, speed, and the board's byte and bit swizzle. Only then do I look at the training log — which step failed, on which channel and byte lane — and I retry at a lower frequency to tell a hard fault from a marginal one."*
- 한국어 핵심: **전원·클럭·리셋 → 설정=실물 → 실패 단계·lane → 저속 재시도**.

**꼬리1. Which training step fails tells you what?**
*"Each step exercises a different group of signals. If ZQ or init fails, I suspect power, reset sequencing, the ZQ resistor, or a wrong memory type. CA training failing points at CK and the command bus. Write leveling failing on one byte lane points at that lane's DQS pair, the feedback DQ bit mapping, or solder on that byte. Read gate points at read latency configuration or DQS reception, and a narrow read or write eye on a few bits points at those specific traces or balls. If everything trains but the memory test fails, I look at address mapping, timing parameters, and refresh."*
- 한국어 핵심: **단계 = 신호 그룹. lane/bit 단위로 좁힌다**.

**꼬리2. It passes at room temperature but fails at hot.**
*"That says the design works but margin is thin, so I measure margin rather than guess. I compare training results — eye width and VREF window per lane — at 25 and at the hot corner to see whether all lanes shrink uniformly or one lane collapses. Then I check temperature-dependent mechanisms: refresh rate, because DRAM leaks faster and above 85 degrees the refresh interval typically has to halve; whether the controller actually tracks the LPDDR temperature register; impedance and VREF drift, which is why periodic ZQ calibration and retraining matter; and power integrity under load. A real example is an NXP i.MX8M Mini erratum where auto-derating above 85 degrees could fail, and the fix was a fixed 2x refresh."*
- 한국어 핵심: **고온 = 마진 부족 → lane별 eye 비교 + refresh(2×, MR4 추적) + ZQ/VREF drift + PI**.

**꼬리3. Only 2 of 10 boards fail.**
*"Board-to-board variation means assembly or component variation, not the design — unless those two boards share a lane that's already the weakest on every board. I compare training margins across all ten; if the passing boards have the same lane barely passing, it's a design margin issue and the two failures are just the tail. If the failing boards are clearly different, I do swap tests: move the DRAM or the SoC from a failing board to a good one, or use a socketed board, and see whether the failure follows the part or stays with the board. Following the board points at solder, X-ray for BGA voids, or a PCB fabrication issue; following the part points at a lot or a marginal die."*
- 한국어 핵심: **10장 margin 비교 → 약한 lane 공통이면 설계, 아니면 swap test(부품 따라가나 보드에 남나)**.

**꼬리4 (추가). How do you know your fix didn't just move the problem?**
*"I re-run the full matrix — all boards, both temperature corners, voltage margins — and compare the minimum margin, not just pass or fail. A setting that fixes one lane can shrink another, so the report is the worst-case margin before and after, per lane."*
- 한국어 핵심: **pass/fail 말고 최소 margin before/after**.

</details>

### Chain 2 — "Write a memory test."

<details><summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 펼치기)</summary>

**Q. Write a memory test.**
*"I'd write three tests and run them in order: a data bus test, an address bus test, and a cell test like March C-. The data bus test walks a one across all 32 bits at a single address, which proves each data line independently in 32 writes. The address bus test writes a pattern at power-of-two offsets and checks that writing the base or any one offset doesn't show up anywhere else, which finds stuck and shorted address lines in log N accesses. Then March C- covers every cell with ten operations per word. Each test reports address, expected, actual, and the XOR so the hardware team can map it to a pin."* (→ §6 화이트보드 코드)
- 한국어 핵심: **data bus → address bus → March C-, 실패는 {addr, exp, act, xor}**.

**꼬리1. Why does walking-1s on the data bus not find address-line shorts?**
*"Because it only ever touches one address. An address-line fault makes two addresses alias, and you can only see aliasing by writing one location and reading a different one. The data bus test writes and reads the same word, so the address lines never change. In my simulation a stuck or shorted address line passed the data bus test every time, and the address test caught it on the exact line."*
- 한국어 핵심: **한 주소만 쓰므로 주소선이 안 바뀜 — aliasing은 "A에 쓰고 B에서 읽어야" 보인다**.

**꼬리2. What does March C- detect that a simple write/read doesn't?**
*"A write-then-immediately-read test only proves a cell can hold a value for a nanosecond, and it reads before any neighbor is written. March C- reads every cell before rewriting it, after the whole array has been swept in both directions, so coupling faults — where writing one cell flips or forces another — get caught regardless of whether the aggressor is above or below the victim. It also catches transition faults and address decoder faults, and it's still linear, 10N. In my fault-injection run the naive test missed both coupling faults and all address faults; March C- caught all of them. The idempotent coupling fault was caught only by March C-, in the fourth element."*
- 한국어 핵심: **읽기-먼저 + 양방향 → CF·TF·AF. 실측: CFid는 March C-만 M4에서 잡음**.

**꼬리3. Why disable the cache — or why not?**
*"For bring-up cell testing I disable it or map the region non-cacheable, because otherwise I'm mostly testing the cache and the writes may never reach DRAM. But I also run with caches on as a separate test, over a range much bigger than the cache, because that exercises burst reads and writes, line fills and evictions — the access pattern real software uses — and that's where signal-integrity problems under back-to-back traffic show up. And I run DMA traffic in parallel to stress the controller from multiple masters."*
- 한국어 핵심: **셀 테스트는 non-cacheable, 별도로 캐시 on + 캐시보다 큰 범위 + DMA 동시 부하**.

**꼬리4 (추가). What does March C- miss?**
*"Retention faults, because there's no delay between writing and reading — that's a bit-fade test. Neighborhood pattern-sensitive faults and some linked faults need longer algorithms like March B or March SS. And on a 32-bit word memory with only all-zeros and all-ones, two bits in the same word that are shorted always carry the same value, so I repeat March C- with several data backgrounds like 5555 and 3333. In my simulation the plain March C- missed a data-line short and the six-background version caught it."*
- 한국어 핵심: **retention · NPSF · linked · word 안 bridging → bit fade, March B/SS, data background**.

**꼬리5 (추가). How do you test the DRAM you're running from, and how long does it take for 8 GB?**
*"I don't — code and stack run from on-chip SRAM or TCM during the DDR test, or I split DDR in two and relocate. For 8 GB, 10N with 64-bit accesses is about 80 GB of traffic, so seconds with DMA but much longer from an uncached CPU loop. So bring-up uses bus tests plus sampled March regions; full March, bit fade and hammer go to the nightly regression and SLT."*
- 한국어 핵심: **SRAM에서 실행 · 8 GB 10N ≈ 80 GB 트래픽 → DMA/샘플링, 전체는 야간/SLT**.

</details>

### Chain 3 — "What's write leveling and why is it needed?"

<details><summary>Q → 꼬리1 → 꼬리2 (답 펼치기)</summary>

**Q. What's write leveling and why is it needed?**
*"With multiple DRAMs, clock, command, and address are routed fly-by — daisy-chained past each device — to avoid stubs, so the clock arrives at each DRAM at a different time. The data strobes are point-to-point per byte lane, so each lane's DQS has a different skew relative to the clock at its DRAM. Write leveling fixes that: the DRAM enters a mode where it samples CK on the rising edge of DQS and returns that value on a DQ pin, and the controller sweeps the DQS delay per lane until it sees the zero-to-one transition. That aligns DQS to CK at each device to meet tDQSS."*
- 한국어 핵심: **fly-by → lane마다 CK 도착 시각 다름 → DRAM이 DQS로 CK를 샘플해 DQ로 회신 → 0→1 지점 탐색**.

**꼬리1. Does LPDDR need it?**
*"LPDDR is usually point-to-point next to the SoC, so the fly-by skew is small, but there's still a clock-to-strobe alignment step — LPDDR4 has write leveling, and LPDDR5 calls it WCK2CK leveling because writes use the separate WCK clock. LPDDR also adds command bus training, because the CA bus runs fast."*
- 한국어 핵심: **LPDDR도 정렬 단계 있음 (LPDDR5 = WCK2CK leveling) + CBT**.

**꼬리2. Write leveling fails on one byte lane only. What do you check?**
*"That lane's DQS pair — polarity and continuity, probing near the DRAM ball during the leveling sequence; the feedback DQ bit for that lane, because a swizzle mistake means the controller listens to the wrong pin; solder on that byte's balls, so X-ray; and whether the required delay is beyond the tap range, which I can test by changing frequency. Then I check if the same lane fails on other boards to separate design from assembly."*
- 한국어 핵심: **DQS 쌍 · 피드백 DQ swizzle · BGA 솔더 · 탭 범위 · 다른 보드도 같은 lane?**

</details>

### Chain 4 — "How would you validate ECC?"

<details><summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 펼치기)</summary>

**Q. How would you validate ECC?**
*"With error injection on every path. I inject single-bit errors and expect correction, the correctable counter to increment, the error address and syndrome registers to point at the injected location, and the interrupt to fire. I inject double-bit errors and expect detection without correction, the uncorrectable path — interrupt, bus error or abort — and correct logging. I check that injection at different addresses reports the right address, which also validates the address decoding in the error logs. And I verify ECC initialization: memory must be scrubbed before first read or you get false uncorrectable errors."*
- 한국어 핵심: **1비트 주입 → 정정·CE·주소·인터럽트 / 2비트 → 검출·UE 경로 / 주소 일치 / init scrub**.

**꼬리1. How do you inject errors?**
*"Most controllers have poison or inject registers — on the Synopsys controller in Zynq MPSoC, Linux exposes them through EDAC sysfs when the kernel has EDAC debug enabled. Without that, I disable ECC, write data with one or two bits flipped so the stored check bits don't match, re-enable ECC, and read back."*
- 한국어 핵심: **poison 레지스터 (Linux synopsys_edac `inject_data_error`, EDAC_DEBUG) 또는 ECC off 상태에서 데이터만 바꿔 쓰기**.

**꼬리2. ECC correctable counts are slowly climbing in the field or the chamber. What do you do?**
*"A correctable error rate is a margin signal, not something to ignore. I log the error addresses and look for a pattern: the same address or row suggests a weak cell — a candidate for retirement or a retention problem; the same bit position across many addresses suggests a lane — signal integrity or training; errors that track temperature suggest refresh or margin. I correlate with temperature and voltage logs, check patrol scrub is on so single-bit errors don't accumulate into double-bit, and reproduce on the bench with margining."*
- 한국어 핵심: **CE 증가 = 마진 경고. 같은 주소 = 셀 / 같은 비트 = lane / 온도 추종 = refresh·마진**.

**꼬리3 (추가). What's the difference between on-die ECC, link ECC, and system ECC?**
*"On-die ECC in DDR5 corrects inside the DRAM and is invisible to the controller, so it doesn't protect the bus. LPDDR5 link ECC protects the transfer on the channel only. System or inline ECC in the controller protects end to end and gives you counters and addresses, which is what functional safety needs. LPDDR has no extra width, so automotive SoCs use inline ECC, trading some capacity and bandwidth."*
- 한국어 핵심: **on-die(DRAM 안) ≠ link(전송) ≠ system/inline(end-to-end, 기능 안전)**.

</details>

### Chain 5 (추가) — "How do you characterize DDR margin across boards and temperature?"

<details><summary>Q → 꼬리1 → 꼬리2 (답 펼치기)</summary>

**Q. How do you characterize DDR margin?**
*"I read the training results per lane and bit — left and right edges and the VREF window — and I also do active margining: shift the delay or VREF away from the trained center until a memory test fails, in both directions. I do that across boards, temperatures and supply voltages, and report the minimum margin per lane against a guideline. That separates lanes that are weak on every board, which is a design issue, from boards that are weak, which is assembly."*
- 한국어 핵심: **훈련값 읽기 + 능동 margining(중앙에서 밀어서 실패점) × 보드 × 온도 × 전압 → lane별 최소값**.

**꼬리1. How many boards is enough?**
*"Enough to see the distribution, not just pass/fail — for early bring-up, every board we have, typically tens; for characterization, boards from multiple fab lots and DRAM lots, with corner silicon if available. I'd agree the numbers with the design team based on the margin we need to guarantee."*
- 한국어 핵심: **분포를 볼 만큼, lot 다양성 + corner silicon** (→ BV12).

**꼬리2. What would you report?**
*"A per-lane table of minimum margin at each corner, the worst case highlighted, compared to the guideline and to the previous board revision, with the test conditions, and any failures with their address, bit, and lane decoded."*
- 한국어 핵심: **lane × 코너 최소 margin + 이전 rev 대비 + 실패 디코드**.

</details>

### Chain 6 (추가) — "Training passes, but Linux crashes randomly under load."

<details><summary>Q → 꼬리1 (답 펼치기)</summary>

**Q. Training passes and memtest passes, but Linux crashes randomly under load.**
*"A short test at idle isn't the same as real traffic. I check what's different under load: temperature rising during the run, power-supply droop with all cores and the NPU active, and multi-master traffic patterns. I'd run memory stress with caches on and DMA in parallel, log temperature and ECC counters, and check whether periodic retraining and refresh rate adjustment are enabled. I'd also rule out software: cache maintenance bugs with DMA look exactly like memory corruption, so I repeat with non-cacheable buffers."*
- 한국어 핵심: **부하 = 온도·PDN droop·멀티 마스터 → 캐시 on + DMA 스트레스 + 온도/ECC 로그 + 캐시 일관성 버그 배제**.

**꼬리1. How do you tell a DDR problem from a software bug?**
*"Patterns: DDR errors cluster by bit or lane and correlate with temperature and voltage; software corruption clusters by data structure or buffer boundary and correlates with code paths. Changing DDR margin — lower frequency, higher voltage — making the crash go away is strong evidence for hardware; it making no difference points at software."*
- 한국어 핵심: **비트/lane·온도 상관 = HW, 버퍼 경계·코드 경로 상관 = SW, 마진 바꿔서 사라지면 HW** (→ BV14).

</details>

## 5. Don 경험 연결

- *"At Solidigm I brought up SRAM and DRAM paths on our FPGA prototype before RTL freeze, as part of SoC verification of I2C, SPI, DMA, SRAM and DRAM — so I've written exactly this kind of data-bus, address-bus and pattern test, and I know the FPGA proves the logic while silicon turns the same test into a margin test."* `[숫자 채우기: DRAM 컨트롤러 IP 설정 수준이었는지 training까지였는지, 테스트 개수]`
- *"At SK hynix I automated chamber reliability testing for high-speed NAND and PCIe interfaces, including shmoo across voltage and temperature, so reading margin per lane across boards and corners and separating design weakness from unit-to-unit variation is something I've done at scale."* `[숫자 채우기: 온도 범위, 샘플 수]`
- *"The memory test I'd write here is structured the way I'd want it in a regression system: every failure is a record with address, expected, actual, XOR, test and element, so it can be aggregated per board, lane and corner — which is how you turn 'it fails sometimes' into 'byte lane 1 on channel 1 loses margin above 95 degrees.'"*
- 정직하게: DDR PHY training 펌웨어를 직접 작성한 경험이 없다면 → *"I haven't written PHY training firmware myself; I've consumed training results and debugged on top of them. I understand what each step aligns and what its failure implies, which is what you need to triage."*

## 6. 실습 / 코드

### 6.1 결함 주입 매트릭스 — 어떤 테스트가 어떤 고장을 잡나 (실측)

- 코드: [onsitePrep/bv09/memtest_matrix.c](onsitePrep/bv09/memtest_matrix.c) — 256 word × 32-bit 가짜 메모리(주소선 A0–A7, 데이터선 D0–D31)에 고장을 하나씩 넣고, 매번 새로 "전원 투입"(0xA5A5A5A5)한 메모리에 9개 테스트를 돌린다. 메모리 모델은 `mem_wr/mem_rd` 안에만 있고 테스트는 그 두 함수로만 접근한다.
- 고장 모델:
  - 셀 SA0/SA1, TF(0→1 불가), CFin(aggressor 0→1 전이가 victim 반전), CFid(aggressor 0→1 전이가 victim을 0으로)
  - 데이터선 D7 stuck-high, D3–D4 short(wired-AND)
  - 주소선 A4 stuck-low, A6 stuck-high, A2–A3 short(wired-AND / wired-OR)
  - D31:24 바이트 lane에 **칩 없음**(읽으면 버스에 마지막으로 남은 값 = 버스 커패시턴스)
  - 약한 셀(저장한 1이 일정 시간 후 사라짐 = retention)
- 빌드/실행 (macOS, ASan+UBSan):

```bash
cd onsitePrep/bv09
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined memtest_matrix.c -o /tmp/memtest_matrix && /tmp/memtest_matrix
```

- **실제 출력** (2026-11-01, 전체는 [memtest_matrix.out](onsitePrep/bv09/memtest_matrix.out)):

```text
BV09 memory-test detection matrix  (N = 256 words x 32 bit, one fault per row)
cell = where the FIRST mismatch was seen ('-' = test PASSED = fault missed)
  Dk = data bit k | s2/s3:Ak = address test step 2/3, line k | Mk = March element | pk = pass k

fault \ test               NAIVE    DBUS     ABUS     DEVICE   MOVINV   MARCHC-  MARCH-DB RANDOM   BITFADE 
F00 clean (no fault)       -        -        -        -        -        -        -        -        -       
F01 cell w37.b5 SA0        p1       -        -        p1       p2       M2       b0.M2    p0       p0      
F02 cell w200.b30 SA1      p1       -        -        p1       p1       M1       b0.M1    p1       p1      
F03 TF w100.b4 no 0->1     p0       -        -        p2       p2       M2       b0.M2    p0       p0      
F04 CFin w20.b1^>w7.b3     -        -        -        p2       p2       M2       b0.M2    p0       p0      
F05 CFid w9.b0^>w50.b12=0  -        -        -        -        -        M4       b0.M4    -        -       
F06 data D7 stuck-hi       p0       D0       -        p1       p1       M1       b0.M1    p0       p1      
F07 data D3-D4 short       -        D3       s2:A0    p1       -        -        b1.M1    p0       -       
F08 addr A4 stuck-lo       -        -        s2:A4    p1       p1       M1       b0.M1    p0       -       
F09 addr A6 stuck-hi       -        -        s2:A6    p1       p1       M1       b0.M1    p0       -       
F10 addr A2-A3 shrt AND    -        -        s2:A2    p1       p1       M1       b0.M1    p0       -       
F11 addr A2-A3 shrt OR     -        -        s3:A2    p1       p1       M1       b0.M1    p0       -       
F12 no chip D31:24         -        -        s2:A0    p1       p1       M1       b0.M1    p0       -       
F13 weak w64.b9 fades      -        -        -        -        -        -        -        -        p0      

accesses on clean memory (reads + writes):
  NAIVE      512 rd +   512 wr =  1024  (4.0N)
  DBUS        32 rd +    32 wr =    64  (0.2N)
  ABUS        72 rd +    26 wr =    98  (0.4N)
  DEVICE     512 rd +   512 wr =  1024  (4.0N)
  MOVINV     512 rd +   768 wr =  1280  (5.0N)
  MARCHC-   1280 rd +  1280 wr =  2560  (10.0N)
  MARCH-DB  7680 rd +  7680 wr = 15360  (60.0N)
  RANDOM     512 rd +   512 wr =  1024  (4.0N)
  BITFADE    512 rd +   512 wr =  1024  (4.0N)
```

- 첫 실패 기록 일부 (실제 출력에서 발췌):

```text
  F05 CFid w9.b0^>w50.b12=0  MARCHC-  M4      addr=0x032 exp=0xffffffff act=0xffffefff xor=0x00001000
  F06 data D7 stuck-hi       DBUS     D0      addr=0x000 exp=0x00000001 act=0x00000081 xor=0x00000080
  F07 data D3-D4 short       DBUS     D3      addr=0x000 exp=0x00000008 act=0x00000000 xor=0x00000008
  F07 data D3-D4 short       ABUS     s2:A0   addr=0x001 exp=0xaaaaaaaa act=0xaaaaaaa2 xor=0x00000008
  F07 data D3-D4 short       MARCH-DB b1.M1   addr=0x000 exp=0x55555555 act=0x55555545 xor=0x00000010
  F08 addr A4 stuck-lo       ABUS     s2:A4   addr=0x010 exp=0xaaaaaaaa act=0x55555555 xor=0xffffffff
  F11 addr A2-A3 shrt OR     ABUS     s3:A2   addr=0x008 exp=0xaaaaaaaa act=0x55555555 xor=0xffffffff
  F12 no chip D31:24         ABUS     s2:A0   addr=0x001 exp=0xaaaaaaaa act=0x55aaaaaa xor=0xff000000
  F12 no chip D31:24         MARCHC-  M1      addr=0x001 exp=0x00000000 act=0xff000000 xor=0xff000000
  F13 weak w64.b9 fades      BITFADE  p0      addr=0x040 exp=0xffffffff act=0xfffffdff xor=0x00000200

self-check: PASS (clean memory passes all 9 tests, March C- = 10N, 6-bg March = 60N)
```

- **결과 해석 — 면접에서 말할 것 10개**:
  1. **DBUS는 데이터선 고장만** 잡고(F06, F07) 주소·셀 고장은 전부 놓친다 — 한 주소만 쓰므로 당연.
  2. **DBUS는 "칩 없음"(F12)도 통과** — 쓰자마자 읽으면 버스에 남은 값이 돌아온다. Barr 글의 경고 그대로 [확인됨][2]. NAIVE(쓰고 바로 읽기)도 같은 이유로 통과.
  3. **ABUS는 주소선 고장을 정확한 선 번호로** 지목(F08 A4, F09 A6, F10/F11 A2). 98 접근(0.4N)뿐.
  4. **그러나 ABUS를 data bus test 전에 돌리면 오진**: F07(데이터선 short)과 F12(칩 없음)가 **"s2:A0" = 주소선 A0 고장처럼** 보인다. xor가 `0x00000008`, `0xff000000`처럼 **일부 비트만** 다르면 데이터 쪽, `0xffffffff`(통째로 반대 패턴)면 진짜 aliasing — **xor 모양이 진단 단서**.
  5. **step 2 vs step 3**: stuck-low(A4)와 wired-AND short는 base와 겹쳐 step 2에서, wired-OR short는 offset끼리만 겹쳐 **step 3에서만** 잡힘(F11 `s3:A2`).
  6. **CFid(F05)는 March C-만** 잡았다(M4). aggressor(9) < victim(50)이고 victim을 0으로 강제하는 고장은 ⇓ 방향에서 victim이 1이 된 **뒤에** aggressor가 0→1 쓰기를 해야 드러난다 — 단방향 테스트(DEVICE, RANDOM)는 구조적으로 못 본다.
  7. **March C-도 F07(같은 word 안 데이터선 short)을 놓쳤다** — 배경이 0x00000000/0xFFFFFFFF뿐이면 D3과 D4가 항상 같은 값. 배경 6개(MARCH-DB)는 두 번째 배경 `0x55555555`에서 잡음(60N).
  8. **retention(F13)은 BITFADE만** — 쓰고 바로(수백 사이클 안에) 읽는 테스트는 시간이 지나야 사라지는 비트를 못 본다.
  9. **RANDOM은 넓게 잡지만 진단이 흐리다**: 주소 고장 xor가 `0xb578bcd1`처럼 난수 → "어느 선?"을 말해 주지 않는다. 그리고 CFid는 놓침.
  10. **비용**: DBUS 0.2N, ABUS 0.4N, March C- 10N, 배경 6개 60N. → bring-up 첫 순서는 싼 bus test로 배선을 증명하고, 비싼 셀 테스트는 그 뒤.

### 6.2 화이트보드용 March C- (함수 본문 30줄)

- 코드: [onsitePrep/bv09/march_cminus_whiteboard.c](onsitePrep/bv09/march_cminus_whiteboard.c) — 함수 부분(시작·끝 주석 사이) 30줄. `main()`은 smoke test.

```c
typedef struct { size_t idx; uint32_t exp, act; int elem; } MemFail;

/* March C-: {any(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); any(r0)}  = 10N
 * returns 0 = pass, -1 = fail (*f = first failure). m must be uncached / device memory. */
int march_cminus(volatile uint32_t *m, size_t n, MemFail *f)
{
    static const struct { int up, rd, wr; uint32_t rv, wv; } el[6] = {
        { 1, 0, 1, 0,           0           },   /* M0 any(w0)     */
        { 1, 1, 1, 0,           0xFFFFFFFFu },   /* M1 up(r0,w1)   */
        { 1, 1, 1, 0xFFFFFFFFu, 0           },   /* M2 up(r1,w0)   */
        { 0, 1, 1, 0,           0xFFFFFFFFu },   /* M3 down(r0,w1) */
        { 0, 1, 1, 0xFFFFFFFFu, 0           },   /* M4 down(r1,w0) */
        { 1, 1, 0, 0,           0           },   /* M5 any(r0)     */
    };
    for (int e = 0; e < 6; e++) {
        for (size_t i = 0; i < n; i++) {
            size_t a = el[e].up ? i : n - 1 - i;     /* no unsigned a >= 0 bug */
            if (el[e].rd) {
                uint32_t v = m[a];
                if (v != el[e].rv) {
                    f->idx = a; f->exp = el[e].rv; f->act = v; f->elem = e;
                    return -1;                       /* stop at FIRST failure */
                }
            }
            if (el[e].wr)
                m[a] = el[e].wv;
        }
    }
    return 0;
}
```

- 실행 (실제 출력):

```text
$ cc -isysroot .../MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
     march_cminus_whiteboard.c -o /tmp/march_wb && /tmp/march_wb
march_cminus whiteboard smoke test: ALL PASS
```

- smoke test 범위: 깨끗한 메모리 통과, 끝난 뒤 전부 0(M4의 배경), n = 0 (접근 없음), n = 1, 홀수 시작·길이, 통과 시 `f` 안 건드림. **고장 검출 자체는 같은 원소 표를 쓰는 6.1 매트릭스로 증명** (일반 배열에는 고장을 넣을 수 없으므로).
- 말하면서 쓸 포인트: ① 원소를 **표(데이터)로** 만들면 6개 루프 대신 1개 → 실수 줄고 March C+/B로 확장 쉬움 ② 내려가는 루프는 `n - 1 - i` (unsigned `a >= 0` 무한 루프 함정) ③ `volatile` + "uncached/Device 매핑" 주석 ④ 첫 실패에서 멈추고 `{idx, exp, act, elem}` 보고, 실전은 실패 링버퍼 ⑤ 확장: data background 루프 추가, 64-bit 접근, 영역 분할 멀티코어.

### 6.3 더 해 볼 것 `[미실행]`

- [V1 코딩 드릴](hmScreenPrep/problems/V1_march_memtest/index.html)을 stub부터 30분 안에 다시 풀기 (같은 3종 테스트, `-EINVAL` 경계 포함).
- `memtest_matrix.c`에 CFst(state coupling), NPSF(이웃 4셀 패턴), 행 단위 고장(row 전체 SA0 = wordline 단선)을 추가해 March C-와 March B의 차이를 보기 `[미실행]`.
- Lima VM에서 `memtester`는 설치돼 있지 않음(확인함, apt 금지) → 실보드/다른 환경에서 `memtester -p`로 물리 영역 테스트 `[미실행]`.

## 7. 자주 틀리는 것

1. **"training이 통과했으니 DDR은 OK"** — training은 타이밍을 맞췄다는 뜻일 뿐. 주소 매핑·타이밍 파라미터·refresh 오류는 memtest에서만 보인다. 반대로 memtest 통과도 "그 온도·그 전압에서"일 뿐.
2. **테스트 순서를 무시** — address test나 March를 먼저 돌리면 데이터선 고장이 "주소 고장"·"셀 고장"으로 오진된다 (실측 F07/F12 → `s2:A0`). 항상 data bus → address bus → cell.
3. **March C-의 원소를 틀리게 말함** — 정확히 `⇕(w0) ⇑(r0,w1) ⇑(r1,w0) ⇓(r0,w1) ⇓(r1,w0) ⇕(r0)`, 10N. M5를 빼먹으면 9N(커버리지 구멍), "w 다음 r"로 쓰면 coupling을 못 잡는다.
4. **word 메모리에 0/1 배경만** — 같은 word 안 비트 short를 못 본다 (실측 F07). data background 반복.
5. **캐시 켠 채 작은 영역 테스트** — 캐시를 테스트한 것. 그리고 `volatile` 빠짐 → 컴파일러가 읽기를 없앰.
6. **"쓰고 바로 읽기"로 충분하다고 생각** — 칩 없음·coupling·주소 고장을 놓친다 (실측 NAIVE: 고장 13개 중 4개만 검출 — 셀 SA0/SA1·TF·D7 stuck-high).
7. **ECC 켠 상태로 셀 테스트하고 "에러 없음"** — 1비트 고장이 자동 정정돼 숨는다. ECC off raw 테스트 또는 CE 카운터를 함께 확인. ECC init(scrub) 없이 읽으면 가짜 UE.
8. **on-die ECC = ECC 있음** — DDR5 on-die ECC는 버스 오류를 못 잡고 카운터도 controller에 안 보인다. 기능 안전에는 system/inline ECC.
9. **고온 실패를 "부품 불량"으로 단정** — 먼저 refresh 배율(MR4 추적, 2× refresh), ZQ/retraining 주기, lane별 margin 비교. 실제 에라타(i.MX8MM ERR050805)처럼 controller 자동 derating 문제일 수도.
10. **2/10 보드 실패를 바로 "조립 불량"** — 나머지 8장도 같은 lane이 겨우 통과하고 있으면 **설계 마진** 문제다. 분포를 먼저 본다.

## 8. 출처

1. Wikipedia, "March algorithm" — March C-(10N)·MATS+·March B·C+·SS 원소 표, 고장 클래스(SAF/TF/AF/CFin/CFid/CFst) — https://en.wikipedia.org/wiki/March_algorithm
2. Michael Barr, "Software-Based Memory Testing" (ESAcademy 사본) — data bus / address bus / device test, 칩 없음·버스 커패시턴스, 순서, 한계 — https://www.esacademy.com/en/library/technical-articles-and-documents/miscellaneous/software-based-memory-testing.html
3. barebox `common/memtest.c` (Barr 알고리즘 포트, address line test 주석: stuck-high / stuck-low / shorted) — https://git.pengutronix.de/cgit/barebox/tree/common/memtest.c
4. AMD PG313 (Versal NoC/DDR controller), "Write Leveling" — fly-by 스큐, tDQSS, DQS 송출 · DRAM DQ 피드백, 0→X→1 탐색, CAL_ERROR 0x9/0xA/0xB — https://docs.amd.com/r/en-US/pg313-network-on-chip/Write-Leveling (+ 검색 결과로 본 Intel Agilex EMIF DDR4 calibration 요약: read deskew가 write leveling 앞, 2회 이상 — https://www.intel.com/content/www/us/en/docs/programmable/683216/current/ddr4-read-calibration.html)
5. Microchip PolarFire, "Training Logic" — clock training, write leveling, DQS gate training, read DQ align, DQS centering, CTRLR_READY — https://onlinedocs.microchip.com/oxy/GUID-FF8061A7-7A15-470F-A6F5-E733C24D85F0-en-US-11/GUID-8BED0D38-E794-4F0E-93FD-5D61C3FA6D7E.html
6. Synopsys, "Key Features Designers Should Know About LPDDR5" — 6400 Mbps, WCK/RDQS, bank 모드, 3 FSP, link ECC, inline ECC, DFE — https://www.synopsys.com/articles/key-features-about-lpddr5.html (+ Cadence LPDDR5 VIP 페이지의 training 목록: CBT Mode1/2, write leveling, read DQ cal, Vref CA/DQ — https://www.cadence.com/zh_CN/home/tools/system-design-and-verification/verification-ip/simulation-vip/memory-models/dram/lpddr5.html · Cadence 블로그 "Device Trainings for High-Speed DRAMs"(검색 요약으로 확인: CBT 피드백은 DQ, DDR5 write leveling = LPDDR5 WCK2CK leveling) — https://community.cadence.com/cadence_blogs_8/b/fv/posts/device-trainings-for-high-speed-drams)
7. Micron "DDR5 Key Module Features" tech brief (서브채널 2개), Kingston DDR5 overview (on-die ECC, PMIC, DFE, RDIMM 40-bit 서브채널) — https://www.mouser.lt/pdfDocs/ddr5_key_module_features_tech_brief2.pdf · https://kingston.com/en/memory/ddr5-overview
8. AMD PG150 UltraScale Memory IP — DDR3/DDR4 calibration 단계 목차 순서, CAL_STATUS/ERROR STATUS, init_calib_complete — https://docs.amd.com/r/en-US/pg150-ultrascale-memory-ip
9. NXP community / U-Boot i.MX8MP 문서 — `lpddr4_pmu_train_1d/2d_imem/dmem.bin` 펌웨어 — https://community.nxp.com/t5/i-MX-Processors/LPDDR4-bin-binaries-for-i-MX8M/m-p/1367577 · https://git.iohub.dev/dany/u-boot-megous/src/branch/master/board/freescale/imx8mp_evk
10. NXP i.MX 8M Family DDR Tool (RPA + MSCALE DDR Tool, 코드 생성), 포럼 사례(ATxImpedance 40→30), i.MX8MQ EVK `lpddr4_timing.c` — https://community.nxp.com/docs/DOC-340179 · https://community.nxp.com/t5/i-MX-Processors/i-MX-8M-Nano-DDR-Tool/m-p/1095245 · https://android.googlesource.com/platform/external/u-boot/+/ce89d9cb4a32f3cd56754bc542c69dd37799be9d/board/freescale/imx8mq_evk/lpddr4_timing.c
11. Microchip PolarFire SoC MSS DDR4 초기화 (RESET_N 200 µs, CKE, tXPR, MR3→MR6→…→MR0, ZQCL) — https://onlinedocs.microchip.com/oxy/GUID-7F276F66-9418-456E-9FA3-8E7EE40C9E25-en-US-7/GUID-F9BA7020-CCC3-41FB-B5CE-E3CB5CA59204.html
12. Xilinx wiki "Zynq UltraScale+ FSBL" (DDR ECC init in FSBL) + U-Boot ML "Zynq MPSoC Dynamic DDR DIMM Configuration support" (ECC init after psu_init) — https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842019/FSBL · https://lists.denx.de/pipermail/u-boot/2021-May/449871.html
13. Linux `drivers/edac/synopsys_edac.c` — ECC_ERRCNT/ECC_CEADDR/ECC_CSYND, `inject_data_error`/`inject_data_poison` (CONFIG_EDAC_DEBUG), compatibles — https://raw.githubusercontent.com/torvalds/linux/master/drivers/edac/synopsys_edac.c
14. Linux `Documentation/ABI/testing/sysfs-devices-edac` — mcX/ce_count, ue_count, reset_counters, sdram_scrub_rate, dimmX/dimm_ce_count … — https://raw.githubusercontent.com/torvalds/linux/master/Documentation/ABI/testing/sysfs-devices-edac
15. Refresh·온도: arXiv 2302.03591 (LPDDR4 tREFI 3.9 µs = 32 ms/8K, MR4 0.5×~4×), TI E2E (MR4 polling vs SW 간격 변경), U-Boot verdin-imx8mm ERR050805 커밋(고정 2× refresh), Intel DDR4 고온 tREFI 안내 — https://arxiv.org/pdf/2302.03591 · https://e2e.ti.com/support/processors-group/processors/f/processors-forum/1235075/am625-ddr-refresh-rate · https://git01.mediatek.com/plugins/gitiles/filogic/uboot/+/bcdd9387ed9d682983c52658ff76daffd669c1ec · https://www.intel.com.au/content/www/au/en/support/programmable/articles/000076520.html
16. Microchip "SRAM – March C-Minus Algorithm" (원소, 10·n, SAF/TF/AF/CFin/CFst/CFid) — https://onlinedocs.microchip.com/oxy/GUID-D1946615-13DB-429F-8835-9FD61AEECC58-en-US-1/GUID-58171B11-CBE8-45BA-9D76-C57C942F4352.html
17. PassMark MemTest86 "Individual Test Descriptions" + Technical (캐시 off 옵션) — https://www.memtest86.com/tech_individual-test-descr.html · https://www.memtest86.com/technical.htm
18. U-Boot docs, "mtest command" — https://docs.u-boot.org/en/latest/usage/cmd/mtest.html
19. memtester (pyropus) + README (mlock/root, `-p physaddr`, `-d`) — https://pyropus.ca./software/memtester/ · https://github.com/jnavila/memtester/blob/master/README
